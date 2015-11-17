/**
 * @file    cc112x.c
 * @date    12.11.2015
 * @author  PN
 * @brief   Implementation of TI transceiver CC112X
 */


/*
********************************************************************************
*                                   INCLUDES
********************************************************************************
*/
#include "emb6.h"


#if NETSTK_CFG_RF_CC112X_EN
#include "cc112x.h"
#include "cc112x_cfg.h"
#include "cc112x_spi.h"

#include "lib_port.h"
#include "packetbuf.h"
#include "evproc.h"

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

    /* WOR Submachine states */
    RF_STATE_SNIFF,
    RF_STATE_RX_BUSY,
    RF_STATE_RX_FINI,

    /* TX Submachine states */
    RF_STATE_TX_STARTED,
    RF_STATE_TX_BUSY,
    RF_STATE_TX_FINI,

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
#define RF_SEM_WAIT(_event_)          evproc_regCallback(_event_, rf_eventHandler)

#define RF_ISR_ACTION_NONE                  (uint8_t)( 0u )
#define RF_ISR_ACTION_TX_FINISHED           (uint8_t)( 1u )
#define RF_ISR_ACTION_RX_FINISHED           (uint8_t)( 2u )

#define RF_IS_RX_BUSY()                 ((RF_State == RF_STATE_RX_BUSY) ||  \
                                         (RF_State == RF_STATE_RX_FINI))

#define RF_IS_IN_TX(_chip_status)       ((_chip_status) & 0x20)

#define RF_INT_TX_FINI                  E_TARGET_EXT_INT_0
#define RF_INT_RX_BUSY                  E_TARGET_EXT_INT_1
#define RF_INT_CCA_STATUS               E_TARGET_EXT_INT_2

#define RF_INT_EDGE_TX_FINI             E_TARGET_INT_EDGE_FALLING
#define RF_INT_EDGE_RX_BUSY             E_TARGET_INT_EDGE_RISING
#define RF_INT_EDGE_CCA_STATUS          E_TARGET_INT_EDGE_RISING

#define RF_CCA_MODE_NONE                (uint8_t)( 0x00 )
#define RF_CCA_MODE_RSSI_BELOW_THR      (uint8_t)( 0x24 )


/*
********************************************************************************
*                                LOCAL VARIABLES
********************************************************************************
*/
static s_ns_t      *RF_Netstk;
static uint8_t      RF_RxBuf[128];
static uint8_t      RF_RxBufLen;
static e_rfState_t  RF_State;

/*
********************************************************************************
*                           LOCAL FUNCTIONS DECLARATION
********************************************************************************
*/
static void cc112x_Init (void *p_netstk, e_nsErr_t *p_err);
static void cc112x_On (e_nsErr_t *p_err);
static void cc112x_Off (e_nsErr_t *p_err);
static void cc112x_Send(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err);
static void cc112x_Recv(uint8_t *p_buf, uint16_t len, e_nsErr_t *p_err);
static void cc112x_Ioctl(e_nsIocCmd_t cmd, void *p_val, e_nsErr_t *p_err);

static void rf_rxFifoRead(void);

static void rf_istTxFinished(void *p_arg);
static void rf_isrRxStarted(void *p_arg);
static void rf_isrCCADone(void *p_arg);
static void rf_eventHandler(c_event_t c_event, p_data_t p_data);

static void rf_configureRegs(const s_regSettings_t *p_regs, uint8_t len);
static void rf_calibrateRF(void);
static void rf_calibrateRCOsc(void);
static void rf_cca(e_nsErr_t *p_err);

static void rf_reset(void);
static void rf_waitRdy(void);
static void rf_gotoSleep(void);
static void rf_gotoSniff(void);
static void rf_gotoIdle(void);



/*
********************************************************************************
*                           LOCAL FUNCTIONS DEFINITIONS
********************************************************************************
*/
static void cc112x_Init (void *p_netstk, e_nsErr_t *p_err)
{
    /* set state to default */
    RF_State = RF_STATE_NON_INIT;

#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }

    if (p_netstk == NULL) {
        *p_err = NETSTK_ERR_INVALID_ARGUMENT;
        return;
    }
#endif

    /* indicates radio is in state initialization */
    RF_State = RF_STATE_INIT;

    /* store pointer to global netstack structure */
    RF_Netstk = (s_ns_t *)p_netstk;

    /* reset the transceiver */
    rf_reset();

    /* configure RF register in eWOR mode by default */
    uint8_t len = sizeof(rf_cfg_ieee802154g_chan0) / sizeof(s_regSettings_t);
    rf_configureRegs(rf_cfg_ieee802154g_chan0, len);

    /* calibrate radio */
    rf_calibrateRF();

    /* calibrate RC oscillator */
    rf_calibrateRCOsc();

    /*
     * configure RF to go to state Sniff
     */
    bsp_extIntClear(RF_INT_TX_FINI);
    bsp_extIntClear(RF_INT_RX_BUSY);

    bsp_extIntEnable(RF_INT_TX_FINI, RF_INT_EDGE_TX_FINI, rf_istTxFinished);
    bsp_extIntEnable(RF_INT_RX_BUSY, RF_INT_EDGE_RX_BUSY, rf_isrRxStarted);

    /* initialize local variables */
    RF_SEM_WAIT(NETSTK_RF_EVENT);
    memset(RF_RxBuf, 0, sizeof(RF_RxBuf));
    RF_RxBufLen = 0;

    /* goto state sleep */
    rf_gotoSleep();
}


static void cc112x_On (e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    /* if currently in state sleep, then must move on state idle first */
    if (RF_State == RF_STATE_SLEEP) {
        rf_gotoIdle();
    }

    /* go to state sniff */
    rf_gotoSniff();

    /* indicate successful operation */
    *p_err = NETSTK_ERR_NONE;
}


static void cc112x_Off (e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    /* if currently not in state sniff, then must move on state idle first */
    if (RF_State != RF_STATE_SNIFF) {
        /* TODO missing implementation */
    }

    /* go to state sleep */
    rf_gotoSleep();

    /* indicate successful operation */
    *p_err = NETSTK_ERR_NONE;
}


static void cc112x_Send(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err)
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

    if (RF_State != RF_STATE_SNIFF) {
        *p_err = NETSTK_ERR_BUSY;
    } else {
        LED_TX_ON();

        /*
         * entry actions
         */
        RF_State = RF_STATE_TX_STARTED;
        rf_gotoIdle();
        rf_calibrateRF();
        bsp_extIntClear(RF_INT_TX_FINI);
        bsp_extIntEnable(RF_INT_TX_FINI, RF_INT_EDGE_TX_FINI, rf_istTxFinished);

        /*
         * do actions
         */
        RF_State = RF_STATE_TX_BUSY;

        /* write packet to send into TX FIFO in following order: packet length
         * is written first, followed by actual data packet. Afterwards a strobe
         * STX is issued to commence transmission process */
        cc112x_spiTxFifoWrite((uint8_t *)&len, 1);
        cc112x_spiTxFifoWrite(p_data, len);

        rf_status_t status = cc112x_spiCmdStrobe(CC112X_STX);

        /* wait for packet to be sent */
        do {
            /* nothing */
        } while(RF_State != RF_STATE_TX_FINI);

        /* set returned error code */
        *p_err = NETSTK_ERR_NONE;
        LED_TX_OFF();

        /*
         * Exit actions
         */
        rf_gotoSniff();
    }
}


static void cc112x_Recv(uint8_t *p_buf, uint16_t len, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif


}


static void cc112x_Ioctl(e_nsIocCmd_t cmd, void *p_val, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    *p_err = NETSTK_ERR_NONE;
    switch (cmd) {
         case NETSTK_CMD_RF_TXPOWER_SET :
             break;

         case NETSTK_CMD_RF_TXPOWER_GET :
             break;

         case NETSTK_CMD_RF_CCA_GET :
             rf_cca(p_err);
             break;

         case NETSTK_CMD_RF_RSSI_GET :
             break;

         case NETSTK_CMD_RF_IS_RX_BUSY:
             if (RF_IS_RX_BUSY()) {
                 *p_err = NETSTK_ERR_BUSY;
             }
             break;

         case NETSTK_CMD_RF_802154G_EU_CHAN_0:
             /* TODO missing implementation */
             break;

         case NETSTK_CMD_RF_RF_SWITCH :
         case NETSTK_CMD_RF_ANT_DIV_SET :
         case NETSTK_CMD_RF_SENS_SET :
         case NETSTK_CMD_RF_SENS_GET :
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
static void rf_gotoSleep(void)
{
    /* clear interrupts */
    bsp_extIntClear(RF_INT_TX_FINI);
    bsp_extIntClear(RF_INT_RX_BUSY);
    bsp_extIntClear(RF_INT_CCA_STATUS);

    /* disable external interrupts */
    bsp_extIntDisable(RF_INT_TX_FINI);
    bsp_extIntDisable(RF_INT_RX_BUSY);
    bsp_extIntDisable(RF_INT_CCA_STATUS);

    /* the radio is put into state sleep when received strobe PowerDown */
    rf_waitRdy();
    cc112x_spiCmdStrobe(CC112X_SPWD);
    RF_State = RF_STATE_SLEEP;
}


static void rf_gotoSniff(void)
{
    bsp_extIntClear(RF_INT_RX_BUSY);
    bsp_extIntEnable(RF_INT_RX_BUSY, RF_INT_EDGE_RX_BUSY, rf_isrRxStarted);

    rf_waitRdy();
    cc112x_spiCmdStrobe(CC112X_SWOR);
    RF_State = RF_STATE_SNIFF;
}


static void rf_gotoIdle(void)
{
    rf_waitRdy();
    cc112x_spiCmdStrobe(CC112X_SIDLE);
}


static void rf_reset(void)
{
    rf_waitRdy();
    cc112x_spiCmdStrobe(CC112X_SRES);
}



static void rf_waitRdy(void)
{
    rf_status_t chip_status;
    do {
        chip_status = cc112x_spiCmdStrobe(CC112X_SNOP);
    } while (chip_status & CC112X_STATE_CHIP_RDYn);
}


/*
********************************************************************************
*                           TRANSMISSION HANDLERS
********************************************************************************
*/
static void rf_rxFifoRead(void)
{
    /* Read number of bytes in RX FIFO*/
    cc112x_spiRegRead(CC112X_NUM_RXBYTES, &RF_RxBufLen, 1);

    /*
     * the received frame is stored in the RX buffer as following
     * Octets   1       n       2
     * Field    Len     data    CRC
     */
    if (RF_RxBufLen < 3) {
        /* invalid frame */
    } else {
        /* Read actual data length */
        cc112x_spiRxFifoRead(&RF_RxBufLen, 1);

        /* Read all the bytes in the RX FIFO */
        cc112x_spiRxFifoRead(RF_RxBuf, RF_RxBufLen);
    }
}



/*
********************************************************************************
*                       INTERRUPT SUBROUTINE HANDLERS
********************************************************************************
*/
static void rf_istTxFinished(void *p_arg)
{
    if (RF_State == RF_STATE_TX_BUSY) {
        RF_State = RF_STATE_TX_FINI;
    }
}


static void rf_isrRxStarted(void *p_arg)
{
    if (RF_State == RF_STATE_SNIFF) {
        RF_State = RF_STATE_RX_BUSY;
        RF_SEM_POST(NETSTK_RF_EVENT);
        LED_RX_ON();
    }
}


static void rf_isrCCADone(void *p_arg)
{
    if (RF_State == RF_STATE_CCA_BUSY) {
        RF_State = RF_STATE_CCA_FINI;
    }
}


static void rf_eventHandler(c_event_t c_event, p_data_t p_data)
{
    e_nsErr_t err;


    /* set the error code to default */
    err = NETSTK_ERR_NONE;

    if (RF_State == RF_STATE_RX_BUSY) {
        /*
         * entry action
         */
        RF_State = RF_STATE_RX_FINI;

        /*
         * do actions:
         * (1)  Retrieve the received frame whose length and CRC fields should
         *      be trimmed.
         * (2)  Signal the next higher layer of the received frame
         */
        rf_rxFifoRead();
        RF_Netstk->phy->recv(RF_RxBuf, RF_RxBufLen, &err);

        /*
         * exit actions
         */
        rf_gotoSniff();
        LED_RX_OFF();
    }
}



/*
********************************************************************************
*                               MISCELLANEOUS
********************************************************************************
*/
static void rf_configureRegs(const s_regSettings_t *p_regs, uint8_t len)
{
    uint8_t ix;
    uint8_t data;

    for (ix = 0; ix < len; ix++) {
        data = p_regs[ix].data;
        cc112x_spiRegWrite(p_regs[ix].addr, &data, 1);
    }
}


static void rf_calibrateRF(void)
{
    uint8_t marc_state;


    /* calibrate radio and wait until the calibration is done */
    cc112x_spiCmdStrobe(CC112X_SCAL);
    do {
        cc112x_spiRegRead(CC112X_MARCSTATE, &marc_state, 1);
    } while (marc_state != 0x41);
}


static void rf_calibrateRCOsc(void)
{
    uint8_t temp;

    /* Read current register value */
    cc112x_spiRegRead(CC112X_WOR_CFG0, &temp,1);

    /* Mask register bit fields and write new values */
    temp = (temp & 0xF9) | (0x02 << 1);

    /* Write new register value */
    cc112x_spiRegWrite(CC112X_WOR_CFG0, &temp,1);

    /* Strobe IDLE to calibrate the RCOSC */
    cc112x_spiCmdStrobe(CC112X_SIDLE);

    /* Disable RC calibration */
    temp = (temp & 0xF9) | (0x00 << 1);
    cc112x_spiRegWrite(CC112X_WOR_CFG0, &temp, 1);
}


static void rf_cca(e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif


    uint8_t is_done;
    uint8_t attempt;
    uint8_t marc_status0;
    rf_status_t chip_status;
    uint8_t cca_mode;

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

    if (RF_State != RF_STATE_SNIFF) {
        *p_err = NETSTK_ERR_BUSY;
    } else {
        /*
         * Entry action
         */
        RF_State = RF_STATE_CCA_BUSY;

        /*
         * Do actions
         */
        bsp_extIntClear(RF_INT_CCA_STATUS);
        bsp_extIntEnable(RF_INT_CCA_STATUS, RF_INT_EDGE_CCA_STATUS, rf_isrCCADone);

        cca_mode = RF_CCA_MODE_RSSI_BELOW_THR;
        cc112x_spiRegWrite(CC112X_PKT_CFG2, &cca_mode, 1);

        attempt = 0;
        is_done = FALSE;
        do {
            /* Strobe TX to assert CCA */
            cc112x_spiCmdStrobe(CC112X_STX);

            do {
                /* poll for radio status */
                chip_status = cc112x_spiCmdStrobe(CC112X_SNOP);

                /* check CCA attempt termination conditions */
                is_done = (RF_State != RF_STATE_CCA_FINI) ||
                          (RF_IS_IN_TX(chip_status));
            } while (is_done == FALSE);

            /* read CCA_STATUS from the register MARC_STATUS0
             * MAC_STATUS0 = 0x0B indicates TX ON CCA failed */
            cc112x_spiRegRead(CC112X_MARC_STATUS0, &marc_status0, 1);
            if (marc_status0 == 0x0B) {
                *p_err = NETSTK_ERR_CHANNEL_ACESS_FAILURE;
            }

            /* check CCA process termination conditions */
            is_done = (++attempt > 3) ||
                      (*p_err != NETSTK_ERR_NONE);
        } while (is_done == FALSE);

        /*
         * Exit actions
         */
        cca_mode = RF_CCA_MODE_NONE;
        cc112x_spiRegWrite(CC112X_PKT_CFG2, &cca_mode, 1);
        rf_gotoSniff();
    }
}



/*
********************************************************************************
*                               DRIVER DEFINITION
********************************************************************************
*/
const s_nsRF_t RFDrvCC1120 =
{
   "CC112X",
    cc112x_Init,
    cc112x_On,
    cc112x_Off,
    cc112x_Send,
    cc112x_Recv,
    cc112x_Ioctl,
};


/*
********************************************************************************
*                                   END OF FILE
********************************************************************************
*/
#endif /* NETSTK_CFG_RF_CC112X_EN */