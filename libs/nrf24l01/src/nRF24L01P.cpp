//  Ported to Mynewt from https://os.mbed.com/users/Owen/code/nRF24L01P/file/8ae48233b4e4/nRF24L01P.cpp/
/**
 * @file nRF24L01P.cpp
 *
 * @author Owen Edwards
 * 
 * @section LICENSE
 *
 * Copyright (c) 2010 Owen Edwards
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * @section DESCRIPTION
 *
 * nRF24L01+ Single Chip 2.4GHz Transceiver from Nordic Semiconductor.
 *
 * Datasheet:
 *
 * http://www.nordicsemi.no/files/Product/data_sheet/nRF24L01P_Product_Specification_1_0.pdf
 */

/**
 * Includes
 */
#include <assert.h>
#include <errno.h>
#include "os/mynewt.h"
#include "hal/hal_spi.h"
#include "hal/hal_gpio.h"
#include "console/console.h"
#include "util.h"
#include "nRF24L01P.h"

/**
 * Defines
 *
 * (Note that all defines here start with an underscore, e.g. '_NRF24L01P_MODE_UNKNOWN',
 *  and are local to this library.  The defines in the nRF24L01P.h file do not start
 *  with the underscore, and can be used by code to access this library.)
 */

typedef enum {
    _NRF24L01P_MODE_UNKNOWN,
    _NRF24L01P_MODE_POWER_DOWN,
    _NRF24L01P_MODE_STANDBY,
    _NRF24L01P_MODE_RX,
    _NRF24L01P_MODE_TX,
} nRF24L01P_Mode_Type;

/*
 * The following FIFOs are present in nRF24L01+:
 *   TX three level, 32 byte FIFO
 *   RX three level, 32 byte FIFO
 */
#define _NRF24L01P_TX_FIFO_COUNT   3
#define _NRF24L01P_RX_FIFO_COUNT   3

#define _NRF24L01P_TX_FIFO_SIZE   32
#define _NRF24L01P_RX_FIFO_SIZE   32

#define _NRF24L01P_SPI_CMD_RD_REG            0x00
#define _NRF24L01P_SPI_CMD_WR_REG            0x20
#define _NRF24L01P_SPI_CMD_RD_RX_PAYLOAD     0x61   
#define _NRF24L01P_SPI_CMD_WR_TX_PAYLOAD     0xa0
#define _NRF24L01P_SPI_CMD_FLUSH_TX          0xe1
#define _NRF24L01P_SPI_CMD_FLUSH_RX          0xe2
#define _NRF24L01P_SPI_CMD_REUSE_TX_PL       0xe3
#define _NRF24L01P_SPI_CMD_R_RX_PL_WID       0x60
#define _NRF24L01P_SPI_CMD_W_ACK_PAYLOAD     0xa8
#define _NRF24L01P_SPI_CMD_W_TX_PYLD_NO_ACK  0xb0
#define _NRF24L01P_SPI_CMD_NOP               0xff


#define _NRF24L01P_REG_CONFIG                0x00
#define _NRF24L01P_REG_EN_AA                 0x01
#define _NRF24L01P_REG_EN_RXADDR             0x02
#define _NRF24L01P_REG_SETUP_AW              0x03
#define _NRF24L01P_REG_SETUP_RETR            0x04
#define _NRF24L01P_REG_RF_CH                 0x05
#define _NRF24L01P_REG_RF_SETUP              0x06
#define _NRF24L01P_REG_STATUS                0x07
#define _NRF24L01P_REG_OBSERVE_TX            0x08
#define _NRF24L01P_REG_RPD                   0x09
#define _NRF24L01P_REG_RX_ADDR_P0            0x0a
#define _NRF24L01P_REG_RX_ADDR_P1            0x0b
#define _NRF24L01P_REG_RX_ADDR_P2            0x0c
#define _NRF24L01P_REG_RX_ADDR_P3            0x0d
#define _NRF24L01P_REG_RX_ADDR_P4            0x0e
#define _NRF24L01P_REG_RX_ADDR_P5            0x0f
#define _NRF24L01P_REG_TX_ADDR               0x10
#define _NRF24L01P_REG_RX_PW_P0              0x11
#define _NRF24L01P_REG_RX_PW_P1              0x12
#define _NRF24L01P_REG_RX_PW_P2              0x13
#define _NRF24L01P_REG_RX_PW_P3              0x14
#define _NRF24L01P_REG_RX_PW_P4              0x15
#define _NRF24L01P_REG_RX_PW_P5              0x16
#define _NRF24L01P_REG_FIFO_STATUS           0x17
#define _NRF24L01P_REG_DYNPD                 0x1c
#define _NRF24L01P_REG_FEATURE               0x1d

#define _NRF24L01P_REG_ADDRESS_MASK          0x1f

// CONFIG register:
#define _NRF24L01P_CONFIG_PRIM_RX        (1<<0)
#define _NRF24L01P_CONFIG_PWR_UP         (1<<1)
#define _NRF24L01P_CONFIG_CRC0           (1<<2)
#define _NRF24L01P_CONFIG_EN_CRC         (1<<3)
#define _NRF24L01P_CONFIG_MASK_MAX_RT    (1<<4)
#define _NRF24L01P_CONFIG_MASK_TX_DS     (1<<5)
#define _NRF24L01P_CONFIG_MASK_RX_DR     (1<<6)

#define _NRF24L01P_CONFIG_CRC_MASK       (_NRF24L01P_CONFIG_EN_CRC|_NRF24L01P_CONFIG_CRC0)
#define _NRF24L01P_CONFIG_CRC_NONE       (0)
#define _NRF24L01P_CONFIG_CRC_8BIT       (_NRF24L01P_CONFIG_EN_CRC)
#define _NRF24L01P_CONFIG_CRC_16BIT      (_NRF24L01P_CONFIG_EN_CRC|_NRF24L01P_CONFIG_CRC0)

// EN_AA register:
#define _NRF24L01P_EN_AA_NONE            0

// EN_RXADDR register:
#define _NRF24L01P_EN_RXADDR_NONE        0

// SETUP_AW register:
#define _NRF24L01P_SETUP_AW_AW_MASK      (0x3<<0)
#define _NRF24L01P_SETUP_AW_AW_3BYTE     (0x1<<0)
#define _NRF24L01P_SETUP_AW_AW_4BYTE     (0x2<<0)
#define _NRF24L01P_SETUP_AW_AW_5BYTE     (0x3<<0)

// SETUP_RETR register:
#define _NRF24L01P_SETUP_RETR_NONE       0

// RF_SETUP register:
#define _NRF24L01P_RF_SETUP_RF_PWR_MASK          (0x3<<1)
#define _NRF24L01P_RF_SETUP_RF_PWR_0DBM          (0x3<<1)
#define _NRF24L01P_RF_SETUP_RF_PWR_MINUS_6DBM    (0x2<<1)
#define _NRF24L01P_RF_SETUP_RF_PWR_MINUS_12DBM   (0x1<<1)
#define _NRF24L01P_RF_SETUP_RF_PWR_MINUS_18DBM   (0x0<<1)

#define _NRF24L01P_RF_SETUP_RF_DR_HIGH_BIT       (1 << 3)
#define _NRF24L01P_RF_SETUP_RF_DR_LOW_BIT        (1 << 5)
#define _NRF24L01P_RF_SETUP_RF_DR_MASK           (_NRF24L01P_RF_SETUP_RF_DR_LOW_BIT|_NRF24L01P_RF_SETUP_RF_DR_HIGH_BIT)
#define _NRF24L01P_RF_SETUP_RF_DR_250KBPS        (_NRF24L01P_RF_SETUP_RF_DR_LOW_BIT)
#define _NRF24L01P_RF_SETUP_RF_DR_1MBPS          (0)
#define _NRF24L01P_RF_SETUP_RF_DR_2MBPS          (_NRF24L01P_RF_SETUP_RF_DR_HIGH_BIT)

// STATUS register:
#define _NRF24L01P_STATUS_TX_FULL        (1<<0)
#define _NRF24L01P_STATUS_RX_P_NO        (0x7<<1)
#define _NRF24L01P_STATUS_MAX_RT         (1<<4)
#define _NRF24L01P_STATUS_TX_DS          (1<<5)
#define _NRF24L01P_STATUS_RX_DR          (1<<6)

// RX_PW_P0..RX_PW_P5 registers:
#define _NRF24L01P_RX_PW_Px_MASK         0x3F

#define _NRF24L01P_TIMING_Tundef2pd_us     100000   // 100mS
#define _NRF24L01P_TIMING_Tstby2a_us          130   // 130uS
#define _NRF24L01P_TIMING_Thce_us              10   //  10uS
#define _NRF24L01P_TIMING_Tpd2stby_us        4500   // 4.5mS worst case
#define _NRF24L01P_TIMING_Tpece2csn_us          4   //   4uS

//  Number of microseconds per tick
//  #define USEC_PER_OS_TICK        1000000 / OS_TICKS_PER_SEC
//  #define USEC_PER_OS_TICK_LOG2   log(USEC_PER_OS_TICK) / log(2)  //  Log Base 2 of USEC_PER_OS_TICK

//  Approximate Log Base 2 of USEC_PER_OS_TICK. Truncate to integer so that (microsecs >> USEC_PER_OS_TICK_LOG2) will give a higher wait time.
#if (OS_TICKS_PER_SEC == 1000)
#define USEC_PER_OS_TICK        1000
#define USEC_PER_OS_TICK_LOG2   9  //  log(1000) / log(2) = 9.9, truncate to 9
#else
#error Missing definition for USEC_PER_OS_TICK_LOG2
#endif  //  OS_TICKS_PER_SEC

//  Halt upon error.
#define error(fmt, arg1, arg2) { console_printf(fmt, arg1, arg2); console_flush(); assert(0); }

static void wait_us(uint32_t microsecs) {
    //  Wait for the number of microseconds.
    //  Originally:    microsecs * OS_TICKS_PER_SEC / 1000000
    //  Equivalent to: microsecs / USEC_PER_OS_TICK
    //  Here we approximate with Log Base 2 to avoid division.  Always approximate to give higher not lower number of ticks.
    uint32_t ticks = (microsecs >> USEC_PER_OS_TICK_LOG2) + 1;  //  Add 1 to avoid 0 ticks.
    //  console_printf("wait %u ticks\n", (unsigned) ticks);
    os_time_delay(ticks);
}

/**
 * Methods
 */

nRF24L01P::nRF24L01P() {
    mode = _NRF24L01P_MODE_UNKNOWN;
}

int nRF24L01P::init(int spi_num0, int cs_pin0, int ce_pin0, int irq_pin0,
    int freq,       //  Frequency in kHz. Default is 2,476 kHz (channel 76)
    int power,
    int data_rate,
    int crc_width,  //  Default is NRF24L01P_CRC_8_BIT
    int tx_size,
    uint8_t auto_ack,
    uint8_t auto_retransmit,
    unsigned long long tx_address,     //  Pipe 0
    const unsigned long long *rx_addresses,  //  Pipes 1 to 5
    uint8_t rx_addresses_len
    ) {
    assert(rx_addresses);  assert(rx_addresses_len <= 5);
    mode = _NRF24L01P_MODE_UNKNOWN;
    spi_num = spi_num0;
    cs_pin = cs_pin0;
    ce_pin = ce_pin0;
    irq_pin = irq_pin0;

    //  Assume SPI and GPIO already initialised previously in nrf24l01_init().
    wait_us(_NRF24L01P_TIMING_Tundef2pd_us);    // Wait for Power-on reset

    disable();   //  Set CE Pin to low.
    deselect();  //  Set CS Pin to high.
    wait_us(_NRF24L01P_TIMING_Tundef2pd_us);    // Wait for Power-on reset

    setRegister(_NRF24L01P_REG_CONFIG, 0);      // Power Down
    wait_us(_NRF24L01P_TIMING_Tundef2pd_us);    // Wait for Power-down

    setRegister(_NRF24L01P_REG_STATUS, _NRF24L01P_STATUS_MAX_RT|_NRF24L01P_STATUS_TX_DS|_NRF24L01P_STATUS_RX_DR);   // Clear any pending interrupts

    //  Setup configuration.
    disableAllRxPipes();
    setRfFrequency(freq);
    setRfOutputPower(power);
    setAirDataRate(data_rate);
    setCrcWidth(crc_width);

    //  Set Pipe 0 for tx.
    setTxAddress(tx_address, DEFAULT_NRF24L01P_ADDRESS_WIDTH);
    setTransferSize(tx_size, NRF24L01P_PIPE_P0);

    if (auto_ack) { enableAutoAcknowledge(NRF24L01P_PIPE_P0); }
    else { disableAutoAcknowledge(); }

    if (auto_retransmit) { assert(0); /* TODO: enableAutoRetransmit(4000, 3); */ }
    else { disableAutoRetransmit(); }

    //  Set Pipes 1 to 5 for rx.
    for (int i = 0; i < rx_addresses_len; i++) {
        int pipe = NRF24L01P_PIPE_P1 + i;  //  rx pipes start at 1.
        setRxAddress(rx_addresses[i], DEFAULT_NRF24L01P_ADDRESS_WIDTH, pipe);
        setTransferSize(tx_size, pipe);
        if (auto_ack) { enableAutoAcknowledge(pipe); }
    }

    //  Flush rx and tx.
    flushTxRx();

    mode = _NRF24L01P_MODE_POWER_DOWN;
    return (0);
}

void nRF24L01P::powerUp(void) {
    console_printf("%spower up\n", _nrf); ////
    int config = getRegister(_NRF24L01P_REG_CONFIG);

    config |= _NRF24L01P_CONFIG_PWR_UP;

    setRegister(_NRF24L01P_REG_CONFIG, config);

    // Wait until the nRF24L01+ powers up
    wait_us( _NRF24L01P_TIMING_Tpd2stby_us );

    mode = _NRF24L01P_MODE_STANDBY;

}


void nRF24L01P::powerDown(void) {

    int config = getRegister(_NRF24L01P_REG_CONFIG);

    config &= ~_NRF24L01P_CONFIG_PWR_UP;

    setRegister(_NRF24L01P_REG_CONFIG, config);

    // Wait until the nRF24L01+ powers down
    wait_us( _NRF24L01P_TIMING_Tpd2stby_us );    // This *may* not be necessary (no timing is shown in the Datasheet), but just to be safe

    mode = _NRF24L01P_MODE_POWER_DOWN;

}


void nRF24L01P::setReceiveMode(void) {
    console_printf("%srx mode\n", _nrf); ////
    if ( _NRF24L01P_MODE_POWER_DOWN == mode ) { powerUp(); }

    int config = getRegister(_NRF24L01P_REG_CONFIG);

    config |= _NRF24L01P_CONFIG_PRIM_RX;

    setRegister(_NRF24L01P_REG_CONFIG, config);

    mode = _NRF24L01P_MODE_RX;

}


void nRF24L01P::setTransmitMode(void) {
    console_printf("%stx mode\n", _nrf); ////
    if ( _NRF24L01P_MODE_POWER_DOWN == mode ) { powerUp(); }

    int config = getRegister(_NRF24L01P_REG_CONFIG);

    config &= ~_NRF24L01P_CONFIG_PRIM_RX;

    setRegister(_NRF24L01P_REG_CONFIG, config);

    mode = _NRF24L01P_MODE_TX;

}

void nRF24L01P::enableRxInterrupt(void) {
    //  Enable rx interrupts.
    console_printf("%senable int\n", _nrf); ////
    int config = getRegister(_NRF24L01P_REG_CONFIG);

    config &= ~_NRF24L01P_CONFIG_MASK_RX_DR;
    setRegister(_NRF24L01P_REG_CONFIG, config);
}

void nRF24L01P::disableRxInterrupt(void) {
    //  Disable rx interrupts.
    console_printf("%sdisable int\n", _nrf); ////
    int config = getRegister(_NRF24L01P_REG_CONFIG);

    config |= _NRF24L01P_CONFIG_MASK_RX_DR;
    setRegister(_NRF24L01P_REG_CONFIG, config);
}

void nRF24L01P::enable(void) {

    ce_value = 1;
    hal_gpio_write(ce_pin, ce_value);  //  Set CE Pin to high.
    wait_us( _NRF24L01P_TIMING_Tpece2csn_us );

}


void nRF24L01P::disable(void) {

    ce_value = 0;
    hal_gpio_write(ce_pin, ce_value);  //  Set CE Pin to low.

}

void nRF24L01P::setRfFrequency(int frequency) {

    if ( ( frequency < NRF24L01P_MIN_RF_FREQUENCY ) || ( frequency > NRF24L01P_MAX_RF_FREQUENCY ) ) {

        error( "%sbad freq %d\r\n", _nrf, frequency );
        return;

    }

    int channel = ( frequency - NRF24L01P_MIN_RF_FREQUENCY ) & 0x7F;

    setRegister(_NRF24L01P_REG_RF_CH, channel);

}


int nRF24L01P::getRfFrequency(void) {

    int channel = getRegister(_NRF24L01P_REG_RF_CH) & 0x7F;

    return ( channel + NRF24L01P_MIN_RF_FREQUENCY );

}


void nRF24L01P::setRfOutputPower(int power) {

    int rfSetup = getRegister(_NRF24L01P_REG_RF_SETUP) & ~_NRF24L01P_RF_SETUP_RF_PWR_MASK;

    switch ( power ) {

        case NRF24L01P_TX_PWR_ZERO_DB:
            rfSetup |= _NRF24L01P_RF_SETUP_RF_PWR_0DBM;
            break;

        case NRF24L01P_TX_PWR_MINUS_6_DB:
            rfSetup |= _NRF24L01P_RF_SETUP_RF_PWR_MINUS_6DBM;
            break;

        case NRF24L01P_TX_PWR_MINUS_12_DB:
            rfSetup |= _NRF24L01P_RF_SETUP_RF_PWR_MINUS_12DBM;
            break;

        case NRF24L01P_TX_PWR_MINUS_18_DB:
            rfSetup |= _NRF24L01P_RF_SETUP_RF_PWR_MINUS_18DBM;
            break;

        default:
            error( "%sbad power %d\r\n", _nrf, power );
            return;

    }

    setRegister(_NRF24L01P_REG_RF_SETUP, rfSetup);

}


int nRF24L01P::getRfOutputPower(void) {

    int rfPwr = getRegister(_NRF24L01P_REG_RF_SETUP) & _NRF24L01P_RF_SETUP_RF_PWR_MASK;

    switch ( rfPwr ) {

        case _NRF24L01P_RF_SETUP_RF_PWR_0DBM:
            return NRF24L01P_TX_PWR_ZERO_DB;

        case _NRF24L01P_RF_SETUP_RF_PWR_MINUS_6DBM:
            return NRF24L01P_TX_PWR_MINUS_6_DB;

        case _NRF24L01P_RF_SETUP_RF_PWR_MINUS_12DBM:
            return NRF24L01P_TX_PWR_MINUS_12_DB;

        case _NRF24L01P_RF_SETUP_RF_PWR_MINUS_18DBM:
            return NRF24L01P_TX_PWR_MINUS_18_DB;

        default:
            error( "%sbad power %d\r\n", _nrf, rfPwr );
            return 0;

    }
}


void nRF24L01P::setAirDataRate(int rate) {

    int rfSetup = getRegister(_NRF24L01P_REG_RF_SETUP) & ~_NRF24L01P_RF_SETUP_RF_DR_MASK;

    switch ( rate ) {

        case NRF24L01P_DATARATE_250_KBPS:
            rfSetup |= _NRF24L01P_RF_SETUP_RF_DR_250KBPS;
            break;

        case NRF24L01P_DATARATE_1_MBPS:
            rfSetup |= _NRF24L01P_RF_SETUP_RF_DR_1MBPS;
            break;

        case NRF24L01P_DATARATE_2_MBPS:
            rfSetup |= _NRF24L01P_RF_SETUP_RF_DR_2MBPS;
            break;

        default:
            error( "%sbad data rate %d\r\n", _nrf, rate );
            return;

    }

    setRegister(_NRF24L01P_REG_RF_SETUP, rfSetup);

}


int nRF24L01P::getAirDataRate(void) {

    int rfDataRate = getRegister(_NRF24L01P_REG_RF_SETUP) & _NRF24L01P_RF_SETUP_RF_DR_MASK;

    switch ( rfDataRate ) {

        case _NRF24L01P_RF_SETUP_RF_DR_250KBPS:
            return NRF24L01P_DATARATE_250_KBPS;

        case _NRF24L01P_RF_SETUP_RF_DR_1MBPS:
            return NRF24L01P_DATARATE_1_MBPS;

        case _NRF24L01P_RF_SETUP_RF_DR_2MBPS:
            return NRF24L01P_DATARATE_2_MBPS;

        default:
            error( "%sbad data rate %d\r\n", _nrf, rfDataRate );
            return 0;

    }
}


void nRF24L01P::setCrcWidth(int width) {

    int config = getRegister(_NRF24L01P_REG_CONFIG) & ~_NRF24L01P_CONFIG_CRC_MASK;

    switch ( width ) {

        case NRF24L01P_CRC_NONE:
            config |= _NRF24L01P_CONFIG_CRC_NONE;
            break;

        case NRF24L01P_CRC_8_BIT:
            config |= _NRF24L01P_CONFIG_CRC_8BIT;
            break;

        case NRF24L01P_CRC_16_BIT:
            config |= _NRF24L01P_CONFIG_CRC_16BIT;
            break;

        default:
            error( "%sbad crc width %d\r\n", _nrf, width );
            return;

    }

    setRegister(_NRF24L01P_REG_CONFIG, config);

}


int nRF24L01P::getCrcWidth(void) {

    int crcWidth = getRegister(_NRF24L01P_REG_CONFIG) & _NRF24L01P_CONFIG_CRC_MASK;

    switch ( crcWidth ) {

        case _NRF24L01P_CONFIG_CRC_NONE:
            return NRF24L01P_CRC_NONE;

        case _NRF24L01P_CONFIG_CRC_8BIT:
            return NRF24L01P_CRC_8_BIT;

        case _NRF24L01P_CONFIG_CRC_16BIT:
            return NRF24L01P_CRC_16_BIT;

        default:
            error( "%sbad crc width %d\r\n", _nrf, crcWidth );
            return 0;

    }
}


void nRF24L01P::setTransferSize(int size, int pipe) {

    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {

        error( "%sbad tx size %d\r\n", _nrf, pipe );
        return;

    }

    if ( ( size < 0 ) || ( size > _NRF24L01P_RX_FIFO_SIZE ) ) {

        error( "%sbad tx size %d\r\n", _nrf, size );
        return;

    }

    int rxPwPxRegister = _NRF24L01P_REG_RX_PW_P0 + ( pipe - NRF24L01P_PIPE_P0 );

    setRegister(rxPwPxRegister, ( size & _NRF24L01P_RX_PW_Px_MASK ) );

}


int nRF24L01P::getTransferSize(int pipe) {

    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {

        error( "%sbad pipe %d\r\n", _nrf, pipe );
        return 0;

    }

    int rxPwPxRegister = _NRF24L01P_REG_RX_PW_P0 + ( pipe - NRF24L01P_PIPE_P0 );

    int size = getRegister(rxPwPxRegister);
    
    return ( size & _NRF24L01P_RX_PW_Px_MASK );

}


void nRF24L01P::disableAllRxPipes(void) {
    console_printf("%sdisable rx\n", _nrf); ////
    setRegister(_NRF24L01P_REG_EN_RXADDR, _NRF24L01P_EN_RXADDR_NONE);

}


void nRF24L01P::disableAutoAcknowledge(void) {

    setRegister(_NRF24L01P_REG_EN_AA, _NRF24L01P_EN_AA_NONE);

}

void nRF24L01P::enableAutoAcknowledge(int pipe) {

    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {

        error( "%sbad ack pipe %d\r\n", _nrf, pipe );
        return;

    }

    int enAA = getRegister(_NRF24L01P_REG_EN_AA);

    enAA |= ( 1 << (pipe - NRF24L01P_PIPE_P0) );

    setRegister(_NRF24L01P_REG_EN_AA, enAA);

}

void nRF24L01P::enableDynamicPayload(int pipe) {
    //  From https://os.mbed.com/teams/JNP3_IOT_2016Z/code/nRF24L01P/file/a7764d1566f7/nRF24L01P.cpp/ 
    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {
 
        error( "%sbad ack pipe %d\r\n", _nrf, pipe );
        return;
 
    }
    
    int feature = getRegister(_NRF24L01P_REG_FEATURE);
    feature |= ( 1 << 2 );
    setRegister(_NRF24L01P_REG_FEATURE, feature);
 
    int dynpd = getRegister(_NRF24L01P_REG_DYNPD);
    dynpd |= ( 1 << (pipe - NRF24L01P_PIPE_P0) );
    setRegister(_NRF24L01P_REG_DYNPD, dynpd);
 
}
 
 void nRF24L01P::disableDynamicPayload(void) {
    //  From https://os.mbed.com/teams/JNP3_IOT_2016Z/code/nRF24L01P/file/a7764d1566f7/nRF24L01P.cpp/    
    int feature = getRegister(_NRF24L01P_REG_FEATURE);
    //  Previously: feature &= !( 1 << 2 );
    feature &= ~( 1 << 2 );
    setRegister(_NRF24L01P_REG_FEATURE, feature);
}

void nRF24L01P::disableAutoRetransmit(void) {
    //  From https://os.mbed.com/teams/JNP3_IOT_2016Z/code/nRF24L01P/file/a7764d1566f7/nRF24L01P.cpp/    
    setRegister(_NRF24L01P_REG_SETUP_RETR, _NRF24L01P_SETUP_RETR_NONE);
    a_retr_enabled = false;
}

void nRF24L01P::enableAutoRetransmit(int delay, int count) {
    //  From https://os.mbed.com/teams/JNP3_IOT_2016Z/code/nRF24L01P/file/a7764d1566f7/nRF24L01P.cpp/    
    delay = (0x00F0 & (delay << 4));
    count = (0x000F & count);
 
    setRegister(_NRF24L01P_REG_SETUP_RETR, delay|count);
    a_retr_enabled = true;
 
}
 
int nRF24L01P::getRetrCount(){
    //  From https://os.mbed.com/teams/JNP3_IOT_2016Z/code/nRF24L01P/file/a7764d1566f7/nRF24L01P.cpp/    
    return getRegister(_NRF24L01P_REG_OBSERVE_TX) & 0x0F;
}

void nRF24L01P::setRxAddress(unsigned long long address, int width, int pipe) {

    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {

        error( "%sbad rx pipe %d\r\n", _nrf, pipe );
        return;

    }

    if ( ( pipe == NRF24L01P_PIPE_P0 ) || ( pipe == NRF24L01P_PIPE_P1 ) ) {

        int setupAw = getRegister(_NRF24L01P_REG_SETUP_AW) & ~_NRF24L01P_SETUP_AW_AW_MASK;
    
        switch ( width ) {
    
            case 3:
                setupAw |= _NRF24L01P_SETUP_AW_AW_3BYTE;
                break;
    
            case 4:
                setupAw |= _NRF24L01P_SETUP_AW_AW_4BYTE;
                break;
    
            case 5:
                setupAw |= _NRF24L01P_SETUP_AW_AW_5BYTE;
                break;
    
            default:
                error( "%sbad rx pipe %d\r\n", _nrf, width );
                return;
    
        }
    
        setRegister(_NRF24L01P_REG_SETUP_AW, setupAw);

    } else {
    
        width = 1;
    
    }

    int rxAddrPxRegister = _NRF24L01P_REG_RX_ADDR_P0 + ( pipe - NRF24L01P_PIPE_P0 );

    int cn = (_NRF24L01P_SPI_CMD_WR_REG | (rxAddrPxRegister & _NRF24L01P_REG_ADDRESS_MASK));

    select();  //  Set CS Pin to low.

    spiWrite(cn);

    while ( width-- > 0 ) {

        //
        // LSByte first
        //
        spiWrite((int) (address & 0xFF));
        address >>= 8;

    }

    deselect();  //  Set CS Pin to high.

    int enRxAddr = getRegister(_NRF24L01P_REG_EN_RXADDR);

    enRxAddr |= (1 << ( pipe - NRF24L01P_PIPE_P0 ) );

    setRegister(_NRF24L01P_REG_EN_RXADDR, enRxAddr);
}

/*
 * This version of setRxAddress is just a wrapper for the version that takes 'long long's,
 *  in case the main code doesn't want to deal with long long's.
 */
void nRF24L01P::setRxAddress(unsigned long msb_address, unsigned long lsb_address, int width, int pipe) {

    unsigned long long address = ( ( (unsigned long long) msb_address ) << 32 ) | ( ( (unsigned long long) lsb_address ) << 0 );

    setRxAddress(address, width, pipe);

}


/*
 * This version of setTxAddress is just a wrapper for the version that takes 'long long's,
 *  in case the main code doesn't want to deal with long long's.
 */
void nRF24L01P::setTxAddress(unsigned long msb_address, unsigned long lsb_address, int width) {

    unsigned long long address = ( ( (unsigned long long) msb_address ) << 32 ) | ( ( (unsigned long long) lsb_address ) << 0 );

    setTxAddress(address, width);

}


void nRF24L01P::setTxAddress(unsigned long long address, int width) {
    console_printf("%sset tx addr\n", _nrf); ////
    int setupAw = getRegister(_NRF24L01P_REG_SETUP_AW) & ~_NRF24L01P_SETUP_AW_AW_MASK;

    switch ( width ) {

        case 3:
            setupAw |= _NRF24L01P_SETUP_AW_AW_3BYTE;
            break;

        case 4:
            setupAw |= _NRF24L01P_SETUP_AW_AW_4BYTE;
            break;

        case 5:
            setupAw |= _NRF24L01P_SETUP_AW_AW_5BYTE;
            break;

        default:
            error( "%sbad tx addr width %d\r\n", _nrf, width );
            return;

    }

    setRegister(_NRF24L01P_REG_SETUP_AW, setupAw);

    int cn = (_NRF24L01P_SPI_CMD_WR_REG | (_NRF24L01P_REG_TX_ADDR & _NRF24L01P_REG_ADDRESS_MASK));

    select();  //  Set CS Pin to low.

    spiWrite(cn);

    while ( width-- > 0 ) {

        //
        // LSByte first
        //
        spiWrite((int) (address & 0xFF));
        address >>= 8;

    }

    deselect();  //  Set CS Pin to high.

}


unsigned long long nRF24L01P::getRxAddress(int pipe) {

    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {

        error( "%sbad rx pipe %d\r\n", _nrf, pipe );
        return 0;

    }

    int width;

    if ( ( pipe == NRF24L01P_PIPE_P0 ) || ( pipe == NRF24L01P_PIPE_P1 ) ) {

        int setupAw = getRegister(_NRF24L01P_REG_SETUP_AW) & _NRF24L01P_SETUP_AW_AW_MASK;

        switch ( setupAw ) {

            case _NRF24L01P_SETUP_AW_AW_3BYTE:
                width = 3;
                break;

            case _NRF24L01P_SETUP_AW_AW_4BYTE:
                width = 4;
                break;

            case _NRF24L01P_SETUP_AW_AW_5BYTE:
                width = 5;
                break;

            default:
                error( "%sbad rx addr width %d\r\n", _nrf, setupAw );
                return 0;

        }

    } else {

        width = 1;

    }

    int rxAddrPxRegister = _NRF24L01P_REG_RX_ADDR_P0 + ( pipe - NRF24L01P_PIPE_P0 );

    int cn = (_NRF24L01P_SPI_CMD_RD_REG | (rxAddrPxRegister & _NRF24L01P_REG_ADDRESS_MASK));

    unsigned long long address = 0;

    select();  //  Set CS Pin to low.

    spiWrite(cn);

    for ( int i=0; i<width; i++ ) {

        //
        // LSByte first
        //
        address |= ( ( (unsigned long long)( spiWrite(_NRF24L01P_SPI_CMD_NOP) & 0xFF ) ) << (i*8) );

    }

    deselect();  //  Set CS Pin to high.

    if ( !( ( pipe == NRF24L01P_PIPE_P0 ) || ( pipe == NRF24L01P_PIPE_P1 ) ) ) {

        address |= ( getRxAddress(NRF24L01P_PIPE_P1) & ~((unsigned long long) 0xFF) );

    }

    return address;

}

    
unsigned long long nRF24L01P::getTxAddress(void) {
    // console_printf("get tx addr\n"); ////
    int setupAw = getRegister(_NRF24L01P_REG_SETUP_AW) & _NRF24L01P_SETUP_AW_AW_MASK;

    int width;

    switch ( setupAw ) {

        case _NRF24L01P_SETUP_AW_AW_3BYTE:
            width = 3;
            break;

        case _NRF24L01P_SETUP_AW_AW_4BYTE:
            width = 4;
            break;

        case _NRF24L01P_SETUP_AW_AW_5BYTE:
            width = 5;
            break;

        default:
            error( "%sbad tx addr width %d\r\n", _nrf, setupAw );
            return 0;

    }

    int cn = (_NRF24L01P_SPI_CMD_RD_REG | (_NRF24L01P_REG_TX_ADDR & _NRF24L01P_REG_ADDRESS_MASK));

    unsigned long long address = 0;

    select();  //  Set CS Pin to low.

    spiWrite(cn);

    for ( int i=0; i<width; i++ ) {

        //
        // LSByte first
        //
        address |= ( ( (unsigned long long)( spiWrite(_NRF24L01P_SPI_CMD_NOP) & 0xFF ) ) << (i*8) );

    }

    deselect();  //  Set CS Pin to high.

    return address;
}


bool nRF24L01P::readable(int pipe) {
    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {

        error( "%sbad readable pipe %d\r\n", _nrf, pipe );
        return false;

    }

    int status = getStatusRegister();
    //  console_printf("rd %x\n", status);  ////
    return ( ( status & _NRF24L01P_STATUS_RX_DR ) && ( ( ( status & _NRF24L01P_STATUS_RX_P_NO ) >> 1 ) == ( pipe & 0x7 ) ) );

}


int nRF24L01P::readablePipe(void) {
    //  Return a pipe number that is readable now.  Return -1 if none are readable.
    int status = getStatusRegister();
    //  console_printf("rd %x\n", status);  ////
    if (! (status & _NRF24L01P_STATUS_RX_DR) ) { return -1; }  //  Nothing to read now.
    return ( status & _NRF24L01P_STATUS_RX_P_NO ) >> 1;  //  Return the pipe number.
}


int nRF24L01P::write(int pipe, char *data, int count) {

    // Note: the pipe number is ignored in a Transmit / write

    //
    // Save the CE state
    //
    int originalCe = ce_value;
    disable();  //  Set CE Pin to low.

    if ( count <= 0 ) return 0;

    if ( count > _NRF24L01P_TX_FIFO_SIZE ) count = _NRF24L01P_TX_FIFO_SIZE;

    // Clear the Status bit
    setRegister(_NRF24L01P_REG_STATUS, _NRF24L01P_STATUS_TX_DS);
	
    select();  //  Set CS Pin to low.

    spiWrite(_NRF24L01P_SPI_CMD_WR_TX_PAYLOAD);

    for ( int i = 0; i < count; i++ ) {

        spiWrite(*data++);

    }

    deselect();  //  Set CS Pin to high.

    int originalMode = mode;
    setTransmitMode();

    enable();  //  Set CE Pin to high.
    wait_us(_NRF24L01P_TIMING_Thce_us);
    disable();  //  Set CE Pin to low.

    while ( !( getStatusRegister() & _NRF24L01P_STATUS_TX_DS ) ) {

        // Wait for the transfer to complete

    }

    // Clear the Status bit
    setRegister(_NRF24L01P_REG_STATUS, _NRF24L01P_STATUS_TX_DS);

    if ( originalMode == _NRF24L01P_MODE_RX ) {

        setReceiveMode();

    }

    if (originalCe) { enable(); }   //  Set CE Pin to high.
    else { disable(); }             //  Set CE Pin to low.
    wait_us( _NRF24L01P_TIMING_Tpece2csn_us );

    return count;

}


int nRF24L01P::read(int pipe, char *data, int count) {

    if ( ( pipe < NRF24L01P_PIPE_P0 ) || ( pipe > NRF24L01P_PIPE_P5 ) ) {

        error( "%sbad rx pipe %d\r\n", _nrf, pipe );
        return -1;

    }

    if ( count <= 0 ) return 0;

    if ( count > _NRF24L01P_RX_FIFO_SIZE ) count = _NRF24L01P_RX_FIFO_SIZE;

    if ( readable(pipe) ) {

        select();  //  Set CS Pin to low.

        spiWrite(_NRF24L01P_SPI_CMD_R_RX_PL_WID);

        int rxPayloadWidth = spiWrite(_NRF24L01P_SPI_CMD_NOP);
        
        deselect();  //  Set CS Pin to high.
        //  console_printf("rx %d\n", rxPayloadWidth);
        if ( ( rxPayloadWidth < 0 ) || ( rxPayloadWidth > _NRF24L01P_RX_FIFO_SIZE ) ) {
    
            // Received payload error: need to flush the FIFO

            select();  //  Set CS Pin to low.
    
            spiWrite(_NRF24L01P_SPI_CMD_FLUSH_RX);
    
            spiWrite(_NRF24L01P_SPI_CMD_NOP);
            
            deselect();  //  Set CS Pin to high.
            
            //
            // At this point, we should retry the reception,
            //  but for now we'll just fall through...
            //

        } else {

            if ( rxPayloadWidth < count ) count = rxPayloadWidth;

            select();  //  Set CS Pin to low.
        
            spiWrite(_NRF24L01P_SPI_CMD_RD_RX_PAYLOAD);
        
            for ( int i = 0; i < count; i++ ) {
        
                *data++ = spiWrite(_NRF24L01P_SPI_CMD_NOP);
        
            }

            deselect();  //  Set CS Pin to high.

            // Clear the Status bit
            setRegister(_NRF24L01P_REG_STATUS, _NRF24L01P_STATUS_RX_DR);

            return count;

        }

    } else {

        //
        // What should we do if there is no 'readable' data?
        //  We could wait for data to arrive, but for now, we'll
        //  just return with no data.
        //
        return 0;

    }

    //
    // We get here because an error condition occured;
    //  We could wait for data to arrive, but for now, we'll
    //  just return with no data.
    //
    return -1;

}

void nRF24L01P::setRegister(int regAddress, int regData) {

    //
    // Save the CE state
    //
    int originalCe = ce_value;
    disable();

    int cn = (_NRF24L01P_SPI_CMD_WR_REG | (regAddress & _NRF24L01P_REG_ADDRESS_MASK));

    select();  //  Set CS Pin to low.

    spiWrite(cn);

    spiWrite(regData & 0xFF);

    deselect();  //  Set CS Pin to high.

    if (originalCe) { enable(); }   //  Set CE Pin to high.
    else { disable(); }             //  Set CE Pin to low.
    wait_us( _NRF24L01P_TIMING_Tpece2csn_us );

}


int nRF24L01P::getRegister(int regAddress) {

    int cn = (_NRF24L01P_SPI_CMD_RD_REG | (regAddress & _NRF24L01P_REG_ADDRESS_MASK));

    select();  //  Set CS Pin to low.

    spiWrite(cn);

    int dn = spiWrite(_NRF24L01P_SPI_CMD_NOP);

    deselect();  //  Set CS Pin to high.

    return dn;

}

int nRF24L01P::getStatusRegister(void) {

    select();  //  Set CS Pin to low.

    int status = spiWrite(_NRF24L01P_SPI_CMD_NOP);

    deselect();  //  Set CS Pin to high.

    return status;

}

void nRF24L01P::select(void) {
    hal_gpio_write(cs_pin, 0);  //  Select the module.
}

void nRF24L01P::deselect(void) {
    hal_gpio_write(cs_pin, 1);  //  Deselect the module.
}

bool nRF24L01P::getRPD(void) {
    //  From https://os.mbed.com/users/khuang/code/nRF24L01/file/b3ea38f27b69/nRF24L01P.cpp/
    uint8_t rpd = getRegister(_NRF24L01P_REG_RPD);
    return (rpd>0);
}
 
uint8_t nRF24L01P::getRSSI(void) {
    //  From https://os.mbed.com/users/khuang/code/nRF24L01/file/b3ea38f27b69/nRF24L01P.cpp/
    uint8_t rssi =0;
    for(int i=0; i<256; i++){
        rssi += getRPD();
        wait_us(50 * 1000);  //  50 milliseconds
        flushRx();
    }
    return rssi;
}

void nRF24L01P::flushRx(void) {
    //  Flush rx.  From https://os.mbed.com/users/Christilut/code/nRF24L01P/file/054a50936ab6/nRF24L01P.cpp/
    select();  //  Set CS Pin to low.
    spiWrite(_NRF24L01P_SPI_CMD_FLUSH_RX);

    spiWrite(_NRF24L01P_SPI_CMD_NOP);
    deselect();  //  Set CS Pin to high.
}
 
void nRF24L01P::flushTx(void) {
    //  Flush tx.  From https://os.mbed.com/users/Christilut/code/nRF24L01P/file/054a50936ab6/nRF24L01P.cpp/
    select();  //  Set CS Pin to low.
    spiWrite(_NRF24L01P_SPI_CMD_FLUSH_TX);

    spiWrite(_NRF24L01P_SPI_CMD_NOP);
    deselect();  //  Set CS Pin to high.
}

void nRF24L01P::flushTxRx(void) {
    //  Flush tx and rx.
    flushTx();
    flushRx();
}

uint8_t nRF24L01P::spiWrite(uint8_t val) {
    //  Write 8-bit val to the SPI port.  Return the result of the write.  
    //  Fail with an assertion error if SPI port was configured as slave.
    //  Need to provide this wrapper because hal_spi_tx_val() returns 16-bit 
    //  values that need to be truncated to 8 bits.
    uint16_t status = hal_spi_tx_val(spi_num, val);
    assert(status != 0xffff);  //  SPI configured wrongly as slave.
    return status & 0xff;      //  Return only 8 bits.
}
