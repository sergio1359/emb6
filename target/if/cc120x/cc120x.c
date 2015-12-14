/*
 * emb6 is licensed under the 3-clause BSD license. This license gives everyone
 * the right to use and distribute the code, either in binary or source code
 * format, as long as the copyright license is retained in the source code.
 *
 * The emb6 is derived from the Contiki OS platform with the explicit approval
 * from Adam Dunkels. However, emb6 is made independent from the OS through the
 * removal of protothreads. In addition, APIs are made more flexible to gain
 * more adaptivity during run-time.
 *
 * The license text is:
 *
 * Copyright (c) 2015,
 * Hochschule Offenburg, University of Applied Sciences
 * Laboratory Embedded Systems and Communications Electronics.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*============================================================================*/

/**
 * @file    cc120x.c
 * @date    12.11.2015
 * @author  PN
 * @brief   Implementation of TI transceiver CC120X
 */


/*
********************************************************************************
*                                   INCLUDES
********************************************************************************
*/
#include "emb6.h"


#include "cc120x.h"
#include "cc120x_cfg.h"
#include "cc120x_spi.h"

#include "lib_port.h"
#include "packetbuf.h"
#include "evproc.h"
#include "phy_framer_802154.h"

#define  LOGGER_ENABLE        LOGGER_RADIO
#include "logger.h"


/*
********************************************************************************
*                                LOCAL TYPEDEFS
********************************************************************************
*/
typedef enum rf_state
{
    RF_STATE_NON_INIT,
    RF_STATE_INIT,
    RF_STATE_SLEEP,
    RF_STATE_ERR,
    RF_STATE_IDLE,

    /* WOR Submachine states */
    RF_STATE_SNIFF,
    RF_STATE_RX_SYNC,
    RF_STATE_RX_PORTION_MIDDLE,
    RF_STATE_RX_PORTION_LAST,
    RF_STATE_RX_FINI,

    /* TX Submachine states */
    RF_STATE_TX_STARTED,
    RF_STATE_TX_BUSY,
    RF_STATE_TX_FINI,
    RF_STATE_TX_PORTION_MIDDLE,
    RF_STATE_TX_PORTION_LAST,

    /* CCA Submachine states */
    RF_STATE_CCA_BUSY,
    RF_STATE_CCA_FINI,

}e_rfState_t;


/*
********************************************************************************
*                                LOCAL DEFINES
********************************************************************************
*/
#define RF_SEM_POST(_event_)          evproc_putEvent(E_EVPROC_HEAD, _event_, NULL)
#define RF_SEM_WAIT(_event_)          evproc_regCallback(_event_, cc120x_eventHandler)

#define RF_CFG_MAX_PACKET_LENGTH            (uint16_t)(PHY_PSDU_MAX + PHY_HEADER_LEN)

#define RF_IS_IN_TX(_chip_status)           ((_chip_status) & 0x20)
#define RF_IS_IN_RX(_chip_status)           ((_chip_status) & 0x10)

#define RF_CCA_MODE_NONE                    (uint8_t)( 0x00 )
#define RF_CCA_MODE_RSSI_BELOW_THR          (uint8_t)( 0x24 )

#define RF_CHIP_STATE_IDLE                  (uint8_t)( 0x00 )
#define RF_CHIP_STATE_RX                    (uint8_t)( 0x10 )
#define RF_CHIP_STATE_TX                    (uint8_t)( 0x20 )

#define RF_MARC_STATUS_TX_FINI              (uint8_t)( 0x40 )
#define RF_MARC_STATUS_RX_FINI              (uint8_t)( 0x80 )

#define RF_GET_CHIP_STATE(_chip_status)     (uint8_t)((_chip_status) & 0x70)

/*!< Set RF to fixed packet length mode */
#define RF_SET_FIXED_PKT_MODE() \
    do {    \
        uint8_t _wr_byte = RF_FIXED_PACKET_LENGTH;          \
        cc120x_spiRegWrite(CC120X_PKT_CFG0, &_wr_byte, 1);  \
    } while (0)

/*!< Disable all RF interrupts */
#define RF_EXTI_DISABLED()  \
    do {    \
        bsp_extIntDisable(E_TARGET_EXT_INT_0);  \
        bsp_extIntDisable(E_TARGET_EXT_INT_1);  \
        bsp_extIntDisable(E_TARGET_EXT_INT_2);  \
        bsp_extIntClear(E_TARGET_EXT_INT_0);    \
        bsp_extIntClear(E_TARGET_EXT_INT_1);    \
        bsp_extIntClear(E_TARGET_EXT_INT_2);    \
    } while (0)

/*!< check if RF is in one of reception states */
#define RF_IS_RX_BUSY() \
    ((rf_state == RF_STATE_RX_SYNC)             ||  \
     (rf_state == RF_STATE_RX_PORTION_MIDDLE)   ||  \
     (rf_state == RF_STATE_RX_PORTION_LAST)     ||  \
     (rf_state == RF_STATE_RX_FINI))

/*!< Set RF to RX mode */
#define RF_GOTO_RX()    \
    do {    \
        uint8_t _chip_status;                                           \
        do {                                                            \
            _chip_status = cc120x_spiCmdStrobe(CC120X_SRX);             \
        } while (RF_GET_CHIP_STATE(_chip_status) != RF_CHIP_STATE_RX);  \
    } while (0)

/*
 * CC120x has 128-byte TX FIFO and 128-byte RX-FIFO
 * Threshold value is coded in opposite directions for the two FIFOs to give
 * equal margin to the overflow and underflow conditions when the threshold
 * is reached.
 *
 * Number of bytes in each FIFO is calculated as following:
 *      FIFO_SIZE = 128
 *      #Bytes_in_RX_FIFO = FIFO_THR + 1
 *      #Bytes_in_TX_FIFO = FIFO_SIZE - #Byte_in_RX_FIFO = FIFO_SIZE - (FIFO_THR + 1)
 *
 * i.e. (1) FIFO_THR=0 means that there are 127 bytes in the TX FIFO and 1 byte
 *          in the RX FIFO.
 *
 *      (2) FIFO_THR=127 means that there are 0 bytes in the TX FIFO and 128
 *          bytes in the RX FIFO
 *
 *      (3) FIFO_THR=120 means that
 *          #Bytes_in_RX_FIFO = FIFO_THR + 1 = 121
 *          #Bytes_in_TX_FIFO = FIFO_SIZE - (FIFO_THR + 1) = 128 - (120 + 1) = 7
 *          #Available_bytes_in_TX_FIFO = FIFO_THR + 1 = 121
 *
 */
#define RF_CFG_FIFO_THR                     (uint8_t)( 120u )
#define RF_CFG_MAX_VARIABLE_LENGTH          (uint8_t)( 255u )

#define RF_CFG_BYTES_IN_RX_FIFO             (uint8_t)( 121u )

#define RF_CFG_FIFO_SIZE                    (uint8_t)( 128u )
#define RF_CFG_AVAI_BYTES_IN_TX_FIFO        (uint8_t)( 121u )   // in TI example using 122?
#define RF_CFG_BYTES_IN_TX_FIFO             (uint8_t)( RF_CFG_FIFO_SIZE - RF_CFG_AVAI_BYTES_IN_TX_FIFO)

#define RF_FIXED_PACKET_LENGTH              (uint8_t)( 0x00u )

#define RF_INT_CFG_TX_FIFO_THR              E_TARGET_EXT_INT_0
#define RF_INT_CFG_TX_FINI                  E_TARGET_EXT_INT_1
#define RF_INT_CFG_TX_CCA_DONE              E_TARGET_EXT_INT_2

#define RF_INT_CFG_EDGE_TX_FIFO_THR         E_TARGET_INT_EDGE_FALLING
#define RF_INT_CFG_EDGE_TX_FINI             E_TARGET_INT_EDGE_FALLING
#define RF_INT_CFG_EDGE_TX_CCA_DONE         E_TARGET_INT_EDGE_RISING

#define RF_INT_CFG_RX_FIFO_THR              E_TARGET_EXT_INT_0
#define RF_INT_CFG_RX_SYNC                  E_TARGET_EXT_INT_1
#define RF_INT_CFG_RX_FINI                  E_TARGET_EXT_INT_2

#define RF_INT_CFG_EDGE_RX_FIFO_THR         E_TARGET_INT_EDGE_RISING
#define RF_INT_CFG_EDGE_RX_SYNC             E_TARGET_INT_EDGE_RISING
#define RF_INT_CFG_EDGE_RX_FINI             E_TARGET_INT_EDGE_FALLING


/*
********************************************************************************
*                                LOCAL VARIABLES
********************************************************************************
*/
static s_ns_t      *rf_netstk;
static uint8_t      rf_rxBuf[RF_CFG_MAX_PACKET_LENGTH];
static uint16_t     rf_rxBufLen;
static uint16_t     rf_byteLeft;
static e_rfState_t  rf_state = RF_STATE_NON_INIT;

static uint8_t      rf_fixedPktLenMode;
static uint8_t      rf_iterations;
static uint8_t     *rf_bufIx;
static uint8_t      rf_txLastPortion;
static uint8_t      rf_worEn;


/*
********************************************************************************
*                           LOCAL FUNCTIONS DECLARATION
********************************************************************************
*/
static void cc120x_Init (void *p_netstk, e_nsErr_t *p_err);
static void cc120x_On (e_nsErr_t *p_err);
static void cc120x_Off (e_nsErr_t *p_err);
static void cc120x_Send(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err);
static void cc120x_Recv(uint8_t *p_buf, uint16_t len, e_nsErr_t *p_err);
static void cc120x_Ioctl(e_nsIocCmd_t cmd, void *p_val, e_nsErr_t *p_err);

static void cc120x_rxByteLeftChk(void);
static void cc120x_isrRxSyncReceived(void *p_arg);
static void cc120x_isrRxFifoAboveThreshold(void *p_arg);
static void cc120x_isrRxPacketReceived(void *p_arg);
static void cc120x_isrTxFifoBelowThreshold(void *p_arg);
static void cc120x_isrTxPacketSent(void *p_arg);
static void cc120x_isrTxCcaDone(void *p_arg);

static void cc120x_eventHandler(c_event_t c_event, p_data_t p_data);
static void cc120x_configureRegs(const s_regSettings_t *p_regs, uint8_t len);
static void cc120x_calibrateRF(void);
static void cc120x_calibrateRCOsc(void);
static void cc120x_cca(e_nsErr_t *p_err);

static void cc120x_reset(void);
static void cc120x_chkPartnumber(e_nsErr_t *p_err);
static void cc120x_waitRdy(void);
static void cc120x_gotoSleep(void);
static void cc120x_gotoSniff(void);
static void cc120x_gotoIdle(void);

static void cc120x_txPowerSet(uint8_t power, e_nsErr_t *p_err);
static void cc120x_txPowerGet(uint8_t *p_power, e_nsErr_t *p_err);
static void cc120x_chanlSet(uint8_t chan, e_nsErr_t *p_err);


/*
********************************************************************************
*                           LOCAL FUNCTIONS DEFINITIONS
********************************************************************************
*/
static void cc120x_Init (void *p_netstk, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }

    if (p_netstk == NULL) {
        *p_err = NETSTK_ERR_INVALID_ARGUMENT;
        return;
    }
#endif

    uint8_t len;

    /* indicates radio is in state initialization */
    rf_state = RF_STATE_INIT;

    /* store pointer to global netstack structure */
    rf_netstk = (s_ns_t *)p_netstk;

    /* initialize SPI handle */
    cc120x_spiInit();

    /* reset the transceiver */
    cc120x_reset();

    /* check part number */
    cc120x_chkPartnumber(p_err);
    if (*p_err != NETSTK_ERR_NONE) {
        return;
    }

    /* configure RF register in eWOR mode by default */
    len = sizeof(cc120x_cfg_ieee802154g_chan0) / sizeof(s_regSettings_t);
    cc120x_configureRegs(cc120x_cfg_ieee802154g_chan0, len);

    /* calibrate radio */
    cc120x_calibrateRF();

    /* calibrate RC oscillator */
    cc120x_calibrateRCOsc();

    /* initialize local variables */
    RF_SEM_WAIT(NETSTK_RF_EVENT);
    memset(rf_rxBuf, 0, sizeof(rf_rxBuf));
    rf_rxBufLen = 0;
    rf_worEn = FALSE;   /* disable WOR mode by default */

    /* goto state sleep */
    cc120x_gotoSleep();
}


static void cc120x_On (e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    /* if currently in state sleep, then must move on state idle first */
    if (rf_state == RF_STATE_SLEEP) {
        cc120x_gotoIdle();
    }

    /* go to state sniff */
    cc120x_gotoSniff();

    /* indicate successful operation */
    *p_err = NETSTK_ERR_NONE;
}


static void cc120x_Off (e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    /* put transceiver to state idle */
    cc120x_gotoIdle();

    /* flush TXFIFO, RXFIFO */
    cc120x_spiCmdStrobe(CC120X_SFRX);
    cc120x_spiCmdStrobe(CC120X_SFTX);

    /* go to state sleep */
    cc120x_gotoSleep();

    /* indicate successful operation */
    *p_err = NETSTK_ERR_NONE;
}


static void cc120x_Send(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }

    if ((p_data == NULL) ||
        (len == 0)) {
        *p_err = NETSTK_ERR_INVALID_ARGUMENT;
        return;
    }
#endif

    if (rf_state != RF_STATE_SNIFF) {
        *p_err = NETSTK_ERR_BUSY;
    } else {
#if LOGGER_ENABLE
        /*
         * Logging
         */
        uint16_t data_len = len;
        uint8_t *p_dataptr = p_data;
        LOG_RAW("RADIO_TX: ");
        while (data_len--) {
            LOG_RAW("%02x", *p_dataptr++);
        }
        LOG_RAW("\n\r\n\r");
#endif

        /*
         * entry actions
         */
        LED_TX_ON();
        if (len > RF_CFG_MAX_PACKET_LENGTH) {
            /* packet length is out of range, and therefore transmission is
             * refused */
            *p_err = NETSTK_ERR_INVALID_ARGUMENT;
            return;
        }

        uint8_t reg_len, write_byte;

        /* go to state IDLE and flush TX FIFO */
        cc120x_spiCmdStrobe(CC120X_SIDLE);
        cc120x_spiCmdStrobe(CC120X_SFTX);

        /* disable RF external interrupts */
        RF_EXTI_DISABLED();

        /* configure RF GPIOs with infinite packet length mode */
        rf_fixedPktLenMode = FALSE;
        rf_txLastPortion = FALSE;
        reg_len = sizeof(cc120x_cfg_tx) / sizeof(s_regSettings_t);
        cc120x_configureRegs(cc120x_cfg_tx, reg_len);

        /* set packet length mode based on length of packet to send */
        if (len > RF_CFG_MAX_VARIABLE_LENGTH) {
            /*
             * do actions
             */
            rf_state = RF_STATE_TX_BUSY;
            rf_byteLeft = len;
            rf_bufIx = p_data;

            /* set fixed packet length */
            write_byte = len % (RF_CFG_MAX_VARIABLE_LENGTH + 1);
            cc120x_spiRegWrite(CC120X_PKT_LEN, &write_byte, 1);

            /* configure RF external interrupts */
            bsp_extIntRegister(RF_INT_CFG_TX_FIFO_THR, RF_INT_CFG_EDGE_TX_FIFO_THR, cc120x_isrTxFifoBelowThreshold);
            bsp_extIntRegister(RF_INT_CFG_TX_FINI, RF_INT_CFG_EDGE_TX_FINI, cc120x_isrTxPacketSent);

            bsp_extIntEnable(RF_INT_CFG_TX_FIFO_THR);
            bsp_extIntEnable(RF_INT_CFG_TX_FINI);

            /* write packet to send into TX FIFO */
            cc120x_spiTxFifoWrite(p_data, RF_CFG_FIFO_SIZE);
            rf_byteLeft -= RF_CFG_FIFO_SIZE;
            rf_bufIx += RF_CFG_FIFO_SIZE;
            rf_iterations = (rf_byteLeft / RF_CFG_AVAI_BYTES_IN_TX_FIFO);

            /* enter TX mode */
            cc120x_spiCmdStrobe(CC120X_STX);
        } else {
            /*
             * do actions
             */
            /* go to state TX_BUSY */
            rf_state = RF_STATE_TX_BUSY;
            rf_txLastPortion = TRUE;

            /* set fixed packet length mode */
            RF_SET_FIXED_PKT_MODE();
            rf_fixedPktLenMode = TRUE;

            /* set fixed packet length */
            write_byte = (uint8_t)(len % (RF_CFG_MAX_VARIABLE_LENGTH + 1));
            cc120x_spiRegWrite(CC120X_PKT_LEN, &write_byte, 1);

            /* using only interrupt PKT_SYNC_RXTX on falling edge is sufficient */
            bsp_extIntRegister(RF_INT_CFG_TX_FINI, RF_INT_CFG_EDGE_TX_FINI, cc120x_isrTxPacketSent);
            bsp_extIntEnable(RF_INT_CFG_TX_FINI);

            /* write packet to send into TX FIFO */
            cc120x_spiTxFifoWrite(p_data, len);

            /* enter TX mode */
            cc120x_spiCmdStrobe(CC120X_STX);
        }

        /* wait for packet to be sent */
        do {
            /* nothing */
        } while(rf_state != RF_STATE_TX_FINI);

        /*
         * Exit actions
         */
        LED_TX_OFF();
        *p_err = NETSTK_ERR_NONE;
        cc120x_gotoSniff();
    }
}


static void cc120x_Recv(uint8_t *p_buf, uint16_t len, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif


}


static void cc120x_Ioctl(e_nsIocCmd_t cmd, void *p_val, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    *p_err = NETSTK_ERR_NONE;
    switch (cmd) {
         case NETSTK_CMD_RF_TXPOWER_SET :
             cc120x_txPowerSet(*((uint8_t *)p_val), p_err);
             break;

         case NETSTK_CMD_RF_TXPOWER_GET :
             cc120x_txPowerGet(p_val, p_err);
             break;

         case NETSTK_CMD_RF_CCA_GET :
             cc120x_cca(p_err);
             break;

         case NETSTK_CMD_RF_IS_RX_BUSY:
             if (RF_IS_RX_BUSY()) {
                 *p_err = NETSTK_ERR_BUSY;
             }
             break;

         case NETSTK_CMD_RF_802154G_EU_CHAN:
             cc120x_chanlSet(*((uint8_t *)p_val), p_err);
             break;

         case NETSTK_CMD_RF_WOR_EN:
             if (p_val) {
                 rf_worEn = *((uint8_t *)p_val);
             } else {
                 *p_err = NETSTK_ERR_INVALID_ARGUMENT;
             }
             break;

         case NETSTK_CMD_RF_RSSI_GET:
         case NETSTK_CMD_RF_RF_SWITCH_SET:
         case NETSTK_CMD_RF_ANT_DIV_SET:
         case NETSTK_CMD_RF_SENS_SET:
         case NETSTK_CMD_RF_SENS_GET:
         default:
             /* unsupported commands are treated in same way */
             *p_err = NETSTK_ERR_CMD_UNSUPPORTED;
             break;
     }
}



/*
********************************************************************************
*                           STATE TRANSITION HANDLERS
********************************************************************************
*/
static void cc120x_gotoSleep(void)
{
    /* disable RF external interrupts */
    RF_EXTI_DISABLED();

    /* enter state Sleep */
    rf_state = RF_STATE_SLEEP;
    cc120x_spiCmdStrobe(CC120X_SPWD);
}


static void cc120x_gotoSniff(void)
{
    uint8_t reg_len;
    uint8_t write_byte;

    /* go to state IDLE and flush RX FIFO */
    cc120x_spiCmdStrobe(CC120X_SIDLE);
    cc120x_spiCmdStrobe(CC120X_SFRX);

    /* disable RF external interrupts */
    RF_EXTI_DISABLED();

    /* configure RF GPIOs */
    reg_len = sizeof(cc120x_cfg_rx_wor) / sizeof(s_regSettings_t);
    cc120x_configureRegs(cc120x_cfg_rx_wor, reg_len);

    /* infinite packet length mode by default */
    rf_fixedPktLenMode = FALSE;

    /* configure RF external interrupts */
    bsp_extIntRegister(RF_INT_CFG_RX_SYNC, RF_INT_CFG_EDGE_RX_SYNC, cc120x_isrRxSyncReceived);
    bsp_extIntRegister(RF_INT_CFG_RX_FIFO_THR, RF_INT_CFG_EDGE_RX_FIFO_THR, cc120x_isrRxFifoAboveThreshold);
    bsp_extIntRegister(RF_INT_CFG_RX_FINI, RF_INT_CFG_EDGE_RX_FINI, cc120x_isrRxPacketReceived);

    /* enable RX interrupts */
    bsp_extIntEnable(RF_INT_CFG_RX_SYNC);
    bsp_extIntEnable(RF_INT_CFG_RX_FIFO_THR);

    /* set receive mode */
    if (rf_worEn) {
        /* enable RX termination on bad packets */
        write_byte = 0x09;
        cc120x_spiRegWrite(CC120X_RFEND_CFG0, &write_byte, 1);

        /* enter state Sniff */
        cc120x_spiCmdStrobe(CC120X_SWOR);
    } else {
        /* disable RX termination on bad packets regardless of the RXOFF_MODE */
        write_byte = 0;
        cc120x_spiRegWrite(CC120X_RFEND_CFG0, &write_byte, 1);

        /* Strobe RX */
        uint8_t chip_status;
        do {
            chip_status = cc120x_spiCmdStrobe(CC120X_SRX);
        } while (RF_GET_CHIP_STATE(chip_status) != RF_CHIP_STATE_RX);
    }
    rf_state = RF_STATE_SNIFF;
}


static void cc120x_gotoIdle(void)
{
    uint8_t chip_status;

    do {
        chip_status = cc120x_spiCmdStrobe(CC120X_SIDLE);
    } while (RF_GET_CHIP_STATE(chip_status) != RF_CHIP_STATE_IDLE);
}


static void cc120x_reset(void)
{
    cc120x_spiCmdStrobe(CC120X_SRES);
    cc120x_waitRdy();
}


static void cc120x_chkPartnumber(e_nsErr_t *p_err)
{
    uint8_t part_number;
    uint8_t part_version;

    /* set returned error to default */
    *p_err = NETSTK_ERR_NONE;

    /* get part number */
    cc120x_spiRegRead(CC120X_PARTNUMBER, &part_number, 1);
    if (part_number != 0x20) {
        *p_err = NETSTK_ERR_INIT;
        return;
    }

    /* get part version */
    cc120x_spiRegRead(CC120X_PARTVERSION, &part_version, 1);
    if (part_version != 0x11) {
        *p_err = NETSTK_ERR_INIT;
        return;
    }
}


static void cc120x_waitRdy(void)
{
    rf_status_t chip_status;
    do {
        chip_status = cc120x_spiCmdStrobe(CC120X_SNOP);
    } while (chip_status & CC120X_STATE_CHIP_RDYn);
}


/*
********************************************************************************
*                       INTERRUPT SUBROUTINE HANDLERS
********************************************************************************
*/
static void cc120x_rxByteLeftChk(void)
{
    /* if incoming bytes can be stored in RX FIFO then set to fixed packet
     * length mode */
    if ((rf_byteLeft < (RF_CFG_MAX_VARIABLE_LENGTH + 1)) &&
        (rf_fixedPktLenMode == FALSE)) {
        /* set fixed packet length mode */
        RF_SET_FIXED_PKT_MODE();
        rf_fixedPktLenMode = TRUE;
    }

    /* disable RX FIFO THR when number of remaining bytes less than the
     * threshold and go to a state to receive the last packet portion */
    if (rf_byteLeft <= RF_CFG_BYTES_IN_RX_FIFO) {
        rf_state = RF_STATE_RX_PORTION_LAST;
        bsp_extIntDisable(RF_INT_CFG_RX_FIFO_THR);
    }
}


static void cc120x_isrRxSyncReceived(void *p_arg)
{
    /* avoid compiler warning of unused parameters */
    (void)&p_arg;

    /* achieve MARC_STATUS to determine what caused the interrupt */
    uint8_t marc_status;
    cc120x_spiRegRead(CC120X_MARC_STATUS1, &marc_status, 1);

    if (rf_state == RF_STATE_SNIFF) {
        uint8_t num_rx_bytes;
        uint16_t pkt_len;

        /* go to state RX SYCN */
        rf_state = RF_STATE_RX_SYNC;
        LED_RX_ON();

        /* wait until entire PHY header is received */
        do {
            cc120x_spiRegRead(CC120X_NUM_RXBYTES, &num_rx_bytes, 1);
        } while (num_rx_bytes < PHY_HEADER_LEN);

        /* parse PHY header for packet length */
        cc120x_spiRxFifoRead(rf_rxBuf, PHY_HEADER_LEN);
        pkt_len = phy_framer802154_getPktLen(rf_rxBuf, PHY_HEADER_LEN);

        /* make sure that the packet length is acceptable */
        if (pkt_len <= RF_CFG_MAX_PACKET_LENGTH) {
            rf_state = RF_STATE_RX_PORTION_MIDDLE;

            /* set RX buffer attributes in corresponds to the incoming packet */
            rf_rxBufLen = PHY_HEADER_LEN + pkt_len;
            rf_byteLeft = pkt_len;
            rf_bufIx = &rf_rxBuf[PHY_HEADER_LEN];

            /* check number of remaining bytes */
            cc120x_rxByteLeftChk();

            /* set fixed packet length */
            uint8_t write_byte;
            write_byte = rf_rxBufLen % (RF_CFG_MAX_VARIABLE_LENGTH + 1);
            cc120x_spiRegWrite(CC120X_PKT_LEN, &write_byte, 1);

            /* enable PKT_SYCN_RXTX interrupt on falling edge, indicating entire
             * packet arrives */
            bsp_extIntClear(RF_INT_CFG_RX_FINI);
            bsp_extIntEnable(RF_INT_CFG_RX_FINI);
        } else {
            /* goto sniff state */
            cc120x_gotoSniff();
        }

        /* clear ISR flag */
        bsp_extIntClear(RF_INT_CFG_RX_SYNC);
    }
}


static void cc120x_isrRxFifoAboveThreshold(void *p_arg)
{
    /* avoid compiler warning of unused parameters */
    (void)&p_arg;

    /* achieve MARC_STATUS to determine what caused the interrupt */
    uint8_t marc_status;
    cc120x_spiRegRead(CC120X_MARC_STATUS1, &marc_status, 1);

    /* only receive middle portions of packet here */
    if (rf_state == RF_STATE_RX_PORTION_MIDDLE) {

        /* read RF_CFG_BYTES_IN_RX_FIFO bytes from the RX FIFO */
        cc120x_spiRxFifoRead(rf_bufIx, RF_CFG_BYTES_IN_RX_FIFO);
        rf_byteLeft -= RF_CFG_BYTES_IN_RX_FIFO;
        rf_bufIx += RF_CFG_BYTES_IN_RX_FIFO;

        /* check number of remaining bytes */
        cc120x_rxByteLeftChk();

        /* clear ISR flag */
        bsp_extIntClear(RF_INT_CFG_RX_FIFO_THR);
    }
}


static void cc120x_isrRxPacketReceived(void *p_arg)
{
    /* avoid compiler warning of unused parameters */
    (void)&p_arg;

    uint8_t marc_status;
    uint8_t is_rx_ok;

    /* achieve MARC_STATUS to determine what caused the interrupt */
    cc120x_spiRegRead(CC120X_MARC_STATUS1, &marc_status, 1);

    /* check reception process result */
    is_rx_ok = (rf_state == RF_STATE_RX_PORTION_LAST) &&
               (marc_status == RF_MARC_STATUS_RX_FINI);

    if (is_rx_ok) {
        /* indicate that reception process has finished */
        rf_state = RF_STATE_RX_FINI;

        /* read remaining bytes */
        cc120x_spiRxFifoRead(rf_bufIx, rf_byteLeft);
        rf_byteLeft = 0;

        /* signal complete reception interrupt */
        RF_SEM_POST(NETSTK_RF_EVENT);

        /* clear ISR flag */
        bsp_extIntClear(RF_INT_CFG_RX_FINI);
        LED_RX_OFF();
    }
}


/**
 * @brief   This function runs every time the TX FIFO is drained below
 *          127 - FIFO_THR = 127 - 120 = 7 [bytes]
 * @param   p_arg
 */
static void cc120x_isrTxFifoBelowThreshold(void *p_arg)
{
    /* avoid compiler warning of unused parameters */
    (void)&p_arg;

    if (rf_txLastPortion == TRUE) {
        /* fill up the TX FIFO with remaining bytes */
        cc120x_spiTxFifoWrite(rf_bufIx, rf_byteLeft);
        rf_byteLeft = 0;

        /* disable interrupt RF_INT_CFG_TX_FIFO_THR */
        bsp_extIntDisable(RF_INT_CFG_TX_FIFO_THR);
    } else {
        /* fill up the TX FIFO */
        cc120x_spiTxFifoWrite(rf_bufIx, RF_CFG_AVAI_BYTES_IN_TX_FIFO);

        if ((rf_byteLeft < (RF_CFG_MAX_VARIABLE_LENGTH + 1 - RF_CFG_BYTES_IN_TX_FIFO)) &&
            (rf_fixedPktLenMode == FALSE)) {
            /* set fixed packet length mode */
            RF_SET_FIXED_PKT_MODE();
            rf_fixedPktLenMode = TRUE;
        }

        /* update TX attributes */
        rf_byteLeft -= RF_CFG_AVAI_BYTES_IN_TX_FIFO;
        rf_bufIx += RF_CFG_AVAI_BYTES_IN_TX_FIFO;

        if (!(--rf_iterations)) {
            rf_txLastPortion = TRUE;
        }
    }

    /* clear ISR flag */
    bsp_extIntClear(RF_INT_CFG_TX_FIFO_THR);
}


static void cc120x_isrTxPacketSent(void *p_arg)
{
    uint8_t marc_status;
    uint8_t is_tx_ok;

    /* achieve MARC_STATUS to determine what caused the interrupt */
    cc120x_spiRegRead(CC120X_MARC_STATUS1, &marc_status, 1);

    /* check TX process result */
    is_tx_ok = (marc_status == RF_MARC_STATUS_TX_FINI) &&
               (rf_state == RF_STATE_TX_BUSY) &&
               (rf_txLastPortion == TRUE);
    if (is_tx_ok) {
        /* TX process has successfully finished */
        rf_state = RF_STATE_TX_FINI;
        bsp_extIntClear(RF_INT_CFG_TX_FINI);
    } else {
        /* flush TX FIFO */
        cc120x_spiCmdStrobe(CC120X_SFTX);
    }
}

static void cc120x_isrTxCcaDone(void *p_arg)
{
    uint8_t marc_status;
    uint8_t is_cca_ok;
    e_nsErr_t err;


    /* achieve MARC_STATUS to determine what caused the interrupt */
    cc120x_spiRegRead(CC120X_MARC_STATUS1, &marc_status, 1);

    /* check reception process result */
    is_cca_ok = (rf_state == RF_STATE_CCA_BUSY);
    if (is_cca_ok) {
        rf_state = RF_STATE_CCA_FINI;
        bsp_extIntClear(RF_INT_CFG_TX_CCA_DONE);
    } else {
        err = NETSTK_ERR_FATAL;
        emb6_errorHandler(&err);
    }
}


static void cc120x_eventHandler(c_event_t c_event, p_data_t p_data)
{
    /* set the error code to default */
    e_nsErr_t err = NETSTK_ERR_NONE;

    /* finalize reception process */
    if (rf_state == RF_STATE_RX_FINI) {
        /*
         * entry action
         */
        rf_state = RF_STATE_IDLE;

        /*
         * do actions:
         * (1)  Retrieve the received frame whose length and CRC fields should
         *      be trimmed.
         * (2)  Signal the next higher layer of the received frame
         */

        /* The transceiver shall be ready for TX request before
         * signaling upper layer of the received frame */
        cc120x_gotoSniff();

        /*
         * exit actions
         */
#if LOGGER_ENABLE
        /*
         * Logging
         */
        uint16_t data_len = rf_rxBufLen;
        uint8_t *p_dataptr = rf_rxBuf;
        LOG_RAW("RADIO_RX: ");
        while (data_len--) {
            LOG_RAW("%02x", *p_dataptr++);
        }
        LOG_RAW("\n\r\n\r");
#endif

        rf_netstk->phy->recv(rf_rxBuf, rf_rxBufLen, &err);
        if (err != NETSTK_ERR_NONE) {
            rf_rxBufLen = 0;
            rf_bufIx = rf_rxBuf;
            memset(rf_rxBuf, 0, sizeof(rf_rxBuf));
        }
    }
}


/*
********************************************************************************
*                               MISCELLANEOUS
********************************************************************************
*/
static void cc120x_configureRegs(const s_regSettings_t *p_regs, uint8_t len)
{
    uint8_t ix;
    uint8_t data;

    for (ix = 0; ix < len; ix++) {
        data = p_regs[ix].data;
        cc120x_spiRegWrite(p_regs[ix].addr, &data, 1);
    }
}


static void cc120x_calibrateRF(void)
{
    uint8_t marc_state;


    /* calibrate radio and wait until the calibration is done */
    cc120x_spiCmdStrobe(CC120X_SCAL);
    do {
        cc120x_spiRegRead(CC120X_MARCSTATE, &marc_state, 1);
    } while (marc_state != 0x41);
}


static void cc120x_calibrateRCOsc(void)
{
    uint8_t temp;

    /* Read current register value */
    cc120x_spiRegRead(CC120X_WOR_CFG0, &temp,1);

    /* Mask register bit fields and write new values */
    temp = (temp & 0xF9) | (0x02 << 1);

    /* Write new register value */
    cc120x_spiRegWrite(CC120X_WOR_CFG0, &temp,1);

    /* Strobe IDLE to calibrate the RCOSC */
    cc120x_spiCmdStrobe(CC120X_SIDLE);

    /* Disable RC calibration */
    temp = (temp & 0xF9) | (0x00 << 1);
    cc120x_spiRegWrite(CC120X_WOR_CFG0, &temp, 1);
}


static void cc120x_cca(e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif


    uint8_t is_done;
    uint8_t marc_status0;
    rf_status_t chip_status;
    uint8_t cca_mode;
    uint8_t reg_len;

    /* set the returned error code to default */
    *p_err = NETSTK_ERR_NONE;

    /*
     * See also: TI CC120x User's guide, 6.11
     *
     * When an STX or SFSTXON command strobe is given while the CC120x is in
     * state RX, then the TX or FSTXON state is only entered if the clear
     * channel requirements are fulfilled (i.e. CCA_STATUS is asserted).
     * Otherwise the chip will remain in state RX.
     * If the channel then becomes available (i.e. after the previous CCA
     * failure), the radio will not enter TX or FSTXON before a new strobe
     * command is sent on the SPI interface. This feature is called TX on
     * CCA/LBT.
     */

    if (rf_state != RF_STATE_SNIFF) {
        *p_err = NETSTK_ERR_BUSY;
    } else {
        /*
         * Entry action
         */
        rf_state = RF_STATE_CCA_BUSY;


        /*
         * Do actions
         */
        /* enter CCA operation */
        cca_mode = RF_CCA_MODE_RSSI_BELOW_THR;
        cc120x_spiRegWrite(CC120X_PKT_CFG2, &cca_mode, 1);

        /* Strobe RX */
        do {
            chip_status = cc120x_spiCmdStrobe(CC120X_SRX);
            if (chip_status & 0x60) {
                /* RX FIFO error then flush RX FIFO */
                chip_status = cc120x_spiCmdStrobe(CC120X_SFRX);
            }
        } while (RF_GET_CHIP_STATE(chip_status) != RF_CHIP_STATE_RX);

        /* disable RF external interrupts */
        RF_EXTI_DISABLED();

        /* configure RF GPIOs to aid CCA operation */
        reg_len = sizeof(cc120x_cfg_cca) / sizeof(s_regSettings_t);
        cc120x_configureRegs(cc120x_cfg_cca, reg_len);

        /* configure external CCA interrupts */
        bsp_extIntRegister(RF_INT_CFG_TX_CCA_DONE, RF_INT_CFG_EDGE_TX_CCA_DONE, cc120x_isrTxCcaDone);
        bsp_extIntEnable(RF_INT_CFG_TX_CCA_DONE);

        /* Strobe TX to assert CCA */
        chip_status = cc120x_spiCmdStrobe(CC120X_STX);

        is_done = FALSE;
        marc_status0 = 0;
        do {
            /* read CCA_STATUS from the register MARC_STATUS0
             * (MAC_STATUS0 & 0x04) indicates value of TXONCCA_FAILED */
            cc120x_spiRegRead(CC120X_MARC_STATUS0, &marc_status0, 1);

            /* poll for radio status */
            chip_status = cc120x_spiCmdStrobe(CC120X_SNOP);

            /* check CCA attempt termination conditions */
            is_done = (rf_state != RF_STATE_CCA_BUSY) |         /* CCA_STATUS   */
                      (RF_IS_IN_TX(chip_status))      |         /* Channel free */
                      (marc_status0 & 0x04);                    /* Channel busy */
        } while (is_done == FALSE);

        /* disable external CCA interrupts */
        bsp_extIntDisable(RF_INT_CFG_TX_CCA_DONE);

        /* get result of CCA process */
        cc120x_spiRegRead(CC120X_MARC_STATUS0, &marc_status0, 1);
        if ((marc_status0 & 0x04)) {
            *p_err = NETSTK_ERR_CHANNEL_ACESS_FAILURE;
        }

        /*
         * Exit actions
         */
        /* reset CCA_MODE */
        cca_mode = RF_CCA_MODE_NONE;
        cc120x_spiRegWrite(CC120X_PKT_CFG2, &cca_mode, 1);

        /* put transceiver to state WOR */
        cc120x_gotoSniff();
    }
}


static void cc120x_txPowerSet(uint8_t power, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    uint8_t pa_power_ramp;

    /*
     * Output power = (PA_POWER_RAMP+1)/2 - 18 [dBm]
     * PA_POWER_RAMP = PA_CFG1[5:0] ~> PA_POWER_RAMP_MASK = 0x3F
     */
    pa_power_ramp = ((power + 18) * 2 - 1) & 0x3Fu;
    cc120x_spiRegWrite(CC120X_PA_CFG1, &pa_power_ramp, 1);
    cc120x_waitRdy();
    *p_err = NETSTK_ERR_NONE;
}


static void cc120x_txPowerGet(uint8_t *p_power, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }

    if (p_power == NULL) {
        *p_err = NETSTK_ERR_INVALID_ARGUMENT;
        return;
    }
#endif

    uint8_t pa_power_ramp;

    /*
     * Output power = (PA_POWER_RAMP+1)/2 - 18 [dBm]
     */
    cc120x_spiRegRead(CC120X_PA_CFG1, &pa_power_ramp, 1);
    pa_power_ramp &= 0x3F;
    *p_power = ((pa_power_ramp + 1) / 2) - 18;
    *p_err = NETSTK_ERR_NONE;
}

static void cc120x_chanlSet(uint8_t chan, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    uint8_t len;

    if (chan == 0) {
        /* reset the transceiver */
        cc120x_reset();

        /* configure RF register in eWOR mode by default */
        len = sizeof(cc120x_cfg_ieee802154g_chan0) / sizeof(s_regSettings_t);
        cc120x_configureRegs(cc120x_cfg_ieee802154g_chan0, len);

        /* calibrate radio */
        cc120x_calibrateRF();

        /* calibrate RC oscillator */
        cc120x_calibrateRCOsc();

        *p_err = NETSTK_ERR_NONE;
    } else {
        *p_err = NETSTK_ERR_INVALID_ARGUMENT;
    }
}

/*
********************************************************************************
*                               DRIVER DEFINITION
********************************************************************************
*/
const s_nsRF_t RFDrvCC120x =
{
   "CC120X",
    cc120x_Init,
    cc120x_On,
    cc120x_Off,
    cc120x_Send,
    cc120x_Recv,
    cc120x_Ioctl,
};


/*
********************************************************************************
*                                   END OF FILE
********************************************************************************
*/