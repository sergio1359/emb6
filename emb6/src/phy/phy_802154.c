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
 * @file    phy_null.c
 * @date    16.10.2015
 * @author  PN
 */

/*
********************************************************************************
*                                   INCLUDES
********************************************************************************
*/
#include "emb6.h"
#include "phy_framer_802154.h"

#include "lib_tmr.h"
#include "lib_crc.h"
#include "packetbuf.h"

#define     LOGGER_ENABLE        LOGGER_PHY
#include    "logger.h"


/*
********************************************************************************
*                                   LOCAL DEFINES
********************************************************************************
*/


/*
********************************************************************************
*                          LOCAL FUNCTION DECLARATIONS
********************************************************************************
*/
static void PHY_Init(void *p_netstk, e_nsErr_t *p_err);
static void PHY_On(e_nsErr_t *p_err);
static void PHY_Off(e_nsErr_t *p_err);
static void PHY_Send(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err);
static void PHY_Recv(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err);
static void PHY_IOCtrl(e_nsIocCmd_t cmd, void *p_val, e_nsErr_t *p_err);

static uint8_t *PHY_insertHdr(uint8_t *p_data, uint16_t len);
static uint16_t PHY_insertCrc(uint8_t *p_data, uint16_t len);
static uint16_t PHY_crc16(uint8_t *p_data, uint16_t len);
static uint32_t PHY_crc32(uint8_t *p_data, uint16_t len);


/*
********************************************************************************
*                               LOCAL VARIABLES
********************************************************************************
*/
static s_ns_t   *PHY_Netstk;
static uint8_t   PHY_FcsLen;


/*
********************************************************************************
*                               GLOBAL VARIABLES
********************************************************************************
*/
const s_nsPHY_t PHYDrv802154 =
{
   "PHY 802154",
    PHY_Init,
    PHY_On,
    PHY_Off,
    PHY_Send,
    PHY_Recv,
    PHY_IOCtrl
};


/*
********************************************************************************
*                           LOCAL FUNCTION DEFINITIONS
********************************************************************************
*/

/**
 * @brief   Initialize driver
 *
 * @param   p_netstk    Pointer to netstack structure
 * @param   p_err       Pointer to a variable storing returned error code
 */
static void PHY_Init(void *p_netstk, e_nsErr_t *p_err)
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

    /* store pointer to netstack structure */
    PHY_Netstk = (s_ns_t *)p_netstk;

    /* set CRC size to default */
#if NETSTK_CFG_IEEE_802154G_EN
    PHY_FcsLen = 4; /* 32-bit CRC */

    /* use MR-FSK operation mode #1 by default:
     * - channel spacing
     * - total number of channels
     * - channel center frequency
     */
    uint8_t chan_num = 0;
    e_nsRfOpMode mode = NETSTK_RF_OP_MODE_1;

    PHY_Netstk->rf->ioctrl(NETSTK_CMD_RF_OP_MODE_SET, &mode, p_err);
    PHY_Netstk->rf->ioctrl(NETSTK_CMD_RF_CHAN_NUM_SET, &chan_num, p_err);
#else
    PHY_FcsLen = 2; /* 16-bit CRC */
#endif

    /* set returned error */
    *p_err = NETSTK_ERR_NONE;
}


/**
 * @brief   Turn driver on
 *
 * @param   p_err       Pointer to a variable storing returned error code
 */
static void PHY_On(e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    PHY_Netstk->rf->on(p_err);
}


/**
 * @brief   Turn driver off
 *
 * @param   p_err       Pointer to a variable storing returned error code
 */
static void PHY_Off(e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }
#endif

    PHY_Netstk->rf->off(p_err);
}


/**
 * @brief   Frame transmission handler
 *
 * @param   p_data      Pointer to buffer holding frame to send
 * @param   len         Length of frame to send
 * @param   p_err       Pointer to a variable storing returned error code
 */
static void PHY_Send(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }

    if ((len == 0) ||
        (p_data == NULL)) {
        *p_err = NETSTK_ERR_INVALID_ARGUMENT;
        return;
    }
#endif

#if LOGGER_ENABLE
    /*
     * Logging
     */
    uint16_t data_len = len;
    uint8_t *p_dataptr = p_data;
    LOG_RAW("PHY_TX: ");
    while (data_len--) {
        LOG_RAW("%02x", *p_dataptr++);
    }
    LOG_RAW("\r\n====================\r\n");
#endif

    uint8_t *p_pkt;
    uint16_t pkt_len;

    /* insert MAC checksum */
    pkt_len = PHY_insertCrc(p_data, len);

    /* insert PHY header */
    p_pkt = PHY_insertHdr(p_data, pkt_len);
    pkt_len += PHY_HEADER_LEN;

    /* Issue next lower layer to transmit the prepared frame */
    PHY_Netstk->rf->send(p_pkt, pkt_len, p_err);
}


/**
 * @brief   Frame reception handler
 *
 * @param   p_data      Pointer to buffer holding frame to receive
 * @param   len         Length of frame to receive
 * @param   p_err       Pointer to a variable storing returned error code
 */
static void PHY_Recv(uint8_t *p_data, uint16_t len, e_nsErr_t *p_err)
{
#if NETSTK_CFG_ARG_CHK_EN
    if (p_err == NULL) {
        return;
    }

    if ((len < PHY_HEADER_LEN) ||
        (p_data == NULL)) {
        *p_err = NETSTK_ERR_INVALID_ARGUMENT;
        return;
    }
#endif

#if LOGGER_ENABLE
    /*
     * Logging
     */
    uint16_t data_len = len;
    uint8_t *p_dataptr = p_data;
    LOG_RAW("\r\n====================\r\n");
    LOG_RAW("PHY_RX: ");
    while (data_len--) {
        LOG_RAW("%02x", *p_dataptr++);
    }
    LOG_RAW("\n\r");
#endif

    /*
     * Parse PHY header
     */
#if NETSTK_CFG_IEEE_802154G_EN
    uint8_t crc_size;
    uint16_t phr, psdu_len;
    uint32_t crc_exp, crc_act;

    /* achieve PHY header */
    phr = (p_data[0] << 8) | (p_data[1]);

    /* verify frame length field */
    psdu_len = phr & 0x07FF;
    if (len != (PHY_HEADER_LEN + psdu_len)) {
        *p_err = NETSTK_ERR_BAD_FORMAT;
        return;
    }
    p_data += PHY_HEADER_LEN;

    /* verify CRC */
    crc_exp = 0;
    crc_act = 0;

    if (phr & 0x1000) {
        /* 16-bit CRC was used in the received frame */
        crc_size = 2;
        psdu_len -= crc_size;

        /* obtain CRC of the received frame */
        memcpy(&crc_exp, &p_data[psdu_len], crc_size);
        crc_exp = ((crc_exp & 0x00FF) << 8) |
                  ((crc_exp & 0xFF00) >> 8);

        /* calculated actual CRC */
        crc_act = PHY_crc16(p_data, psdu_len);
    } else {
        /* 32-bit CRC was used in the received frame */
        crc_size = 4;
        psdu_len -= crc_size;

        /* obtain CRC of the received frame */
        memcpy(&crc_exp, &p_data[psdu_len], crc_size);
        crc_exp = ((crc_exp & 0x000000FF) << 24) |
                  ((crc_exp & 0x0000FF00) <<  8) |
                  ((crc_exp & 0x00FF0000) >>  8) |
                  ((crc_exp & 0xFF000000) >> 24);

        /* calculated actual CRC */
        crc_act = PHY_crc32(p_data, psdu_len);
    }

    if (crc_act != crc_exp) {
        *p_err = NETSTK_ERR_CRC;
        return;
    }
#else
    uint8_t psdu_len;
    uint16_t crc_exp, crc_act;

    /* verify frame length */
    psdu_len = *p_data;
    if (len != (PHY_HEADER_LEN + psdu_len)) {
        *p_err = NETSTK_ERR_BAD_FORMAT;
        return;
    }
    p_data += PHY_HEADER_LEN;

    /* verify CRC */
    psdu_len -= PHY_FcsLen;

    /* calculated actual CRC */
    crc_act = PHY_crc16(p_data, psdu_len);

    /* obtain CRC of the received frame */
    memcpy(&crc_exp, &p_data[psdu_len], PHY_FcsLen);
    crc_exp = ((crc_exp & 0x00FF) << 8) |
              ((crc_exp & 0xFF00) >> 8);
    if (crc_act != crc_exp) {
        *p_err = NETSTK_ERR_CRC;
        return;
    }
#endif

    /* Inform the next higher layer */
    PHY_Netstk->mac->recv(p_data, psdu_len, p_err);
}


/**
 * @brief    Miscellaneous commands handler
 *
 * @param   cmd         Command to be issued
 * @param   p_val       Pointer to a variable related to the command
 * @param   p_err       Pointer to a variable storing returned error code
 */
static void PHY_IOCtrl(e_nsIocCmd_t cmd, void *p_val, e_nsErr_t *p_err)
{
    uint8_t crc_size;

    /* Set returned error code to default */
    *p_err = NETSTK_ERR_NONE;

    switch (cmd) {
        case NETSTK_CMD_PHY_CRC_LEN_SET:
#if NETSTK_CFG_IEEE_802154G_EN
            crc_size = *((uint8_t *)p_val);
            if ((crc_size != 4) && (crc_size != 2)) {
                *p_err = NETSTK_ERR_INVALID_ARGUMENT;
            } else {
                PHY_FcsLen = crc_size;
            }
#else
            (void )&crc_size;
            *p_err = NETSTK_ERR_INVALID_ARGUMENT;
#endif
            break;

        case NETSTK_CMD_PHY_LAST_PKT_TX:
            /* Issue next lower layer to transmit the prepared frame */
            PHY_Netstk->rf->send(packetbuf_hdrptr(), packetbuf_totlen(), p_err);
            break;

        default:
            PHY_Netstk->rf->ioctrl(cmd, p_val, p_err);
            break;
    }
}


/**
 * @brief   Compute CRC-16 over a byte stream
 * @param   p_data  Point to first byte of the stream
 * @param   len     Length of the stream
 * @return  CRC-16 value of the stream
 */
static uint16_t PHY_crc16(uint8_t *p_data, uint16_t len)
{
    uint16_t ix;
    uint32_t crc_res;

    /* calculate CRC */
    crc_res = CRC16_INIT;
    for (ix = 0; ix < len; ix++) {
        crc_res = crc_16_update(crc_res, p_data[ix]);
    }

    return crc_res;
}

/**
 * @brief   Compute CRC-32 over a byte stream
 * @param   p_data  Point to first byte of the stream
 * @param   len     Length of the stream
 * @return  CRC-32 value of the stream
 */
static uint32_t PHY_crc32(uint8_t *p_data, uint16_t len)
{
    uint16_t ix;
    uint32_t crc_res;

    /* calculate CRC */
    crc_res = CRC32_INIT;
    for (ix = 0; ix < len; ix++) {
        crc_res = crc_32_update(crc_res, p_data[ix]);
    }

    /* add padding when length is less than 4 octets.
     * See IEEE-802.15.4g-2012, 5.2.1.9 */
    if (len < 4) {
        crc_res = crc_32_update(crc_res, 0x00);
    }
    crc_res ^= CRC32_INIT;

    return crc_res;
}


/**
 * @brief   Insert PHY header
 */
static uint8_t * PHY_insertHdr(uint8_t *p_data, uint16_t len)
{
    uint8_t *p_hdr;
    uint16_t hdr;

    /* get pointer to PHY header field */
    p_hdr = p_data - PHY_HEADER_LEN;

    /* compute header fields */
    hdr = len;

#if NETSTK_CFG_IEEE_802154G_EN
    if (PHY_FcsLen == 2) {
        hdr |= 0x1000;
    }

    /* write PHY header */
    p_hdr[0] = (hdr & 0xFF00) >> 8;
    p_hdr[1] = (hdr & 0x00FF);

#else
    /* write the header */
    p_hdr[0] = hdr & 0x7F;
#endif

    return p_hdr;
}

/**
 * @brief   Insert PHY checksum
 */
static uint16_t PHY_insertCrc(uint8_t *p_data, uint16_t len)
{
    uint32_t crc = 0;
    uint16_t ret_len;
    uint8_t *p_crc;


    /* get pointer to checksum field */
    p_crc = p_data + len;

    if (PHY_FcsLen == 4) {
        crc = PHY_crc32(p_data, len);
        p_crc[0] = (crc & 0xFF000000) >> 24;
        p_crc[1] = (crc & 0x00FF0000) >> 16;
        p_crc[2] = (crc & 0x0000FF00) >> 8;
        p_crc[3] = (crc & 0x000000FF);
    } else {
        crc = PHY_crc16(p_data, len);
        p_crc[0] = (crc & 0xFF00) >> 8;
        p_crc[1] = (crc & 0x00FF);
    }

    /* return total length of the packet */
    ret_len = len + PHY_FcsLen;
    return ret_len;
}


/*
********************************************************************************
*                               END OF FILE
********************************************************************************
*/
