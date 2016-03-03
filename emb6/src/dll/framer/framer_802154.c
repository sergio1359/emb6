/*
 *
 *  Copyright (c) 2008, Swedish Institute of Computer Science
 *  All rights reserved.
 *
 *  Additional fixes for AVR contributed by:
 *
 *      Colin O'Flynn coflynn@newae.com
 *      Eric Gnoske egnoske@gmail.com
 *      Blake Leverett bleverett@gmail.com
 *      Mike Vidales mavida404@gmail.com
 *      Kevin Brown kbrown3@uccs.edu
 *      Nate Bohlmann nate@elfwerks.com
 *
 *  Additional fixes for MSP430 contributed by:
 *        Joakim Eriksson
 *        Niclas Finne
 *        Nicolas Tsiftes
 *
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of the copyright holders nor the names of
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
*/

/**
 *  \file
 *  \brief 802.15.4 frame creation and parsing functions
 *
 *  This file converts to and from a structure to a packed 802.15.4
 *  frame.
 */

/*
 *  \brief This file is where the main functions that relate to frame
 *  manipulation will reside.
*/
/** \addtogroup mac
 *
 * @{
 */
/**
 *   \addtogroup frame802154
 *   @{
*/
#include "emb6.h"
#include "cc.h"
#include "framer.h"
#include "framer_802154.h"


/**
 *  \brief Structure that contains the lengths of the various addressing and security fields
 *  in the 802.15.4 header.  This structure is used in \ref frame802154_create()
 */
typedef struct {
  uint8_t dest_pid_len;    /**<  Length (in bytes) of destination PAN ID field */
  uint8_t dest_addr_len;   /**<  Length (in bytes) of destination address field */
  uint8_t src_pid_len;     /**<  Length (in bytes) of source PAN ID field */
  uint8_t src_addr_len;    /**<  Length (in bytes) of source address field */
  uint8_t aux_sec_len;     /**<  Length (in bytes) of aux security header field */
} field_length_t;

/*----------------------------------------------------------------------------*/
static uint8_t Framer802154_DSN;

CC_INLINE static uint8_t
addr_len(uint8_t mode)
{
  switch(mode) {
  case FRAME802154_SHORTADDRMODE:  /* 16-bit address */
    return 2;
  case FRAME802154_LONGADDRMODE:   /* 64-bit address */
    return 8;
  default:
    return 0;
  }
}
/*----------------------------------------------------------------------------*/
#if LLSEC802154_USES_EXPLICIT_KEYS
static uint8_t
get_key_id_len(uint8_t key_id_mode)
{
  switch(key_id_mode) {
  case FRAME802154_1_BYTE_KEY_ID_MODE:
    return 1;
  case FRAME802154_5_BYTE_KEY_ID_MODE:
    return 5;
  case FRAME802154_9_BYTE_KEY_ID_MODE:
    return 9;
  default:
    return 0;
  }
}
#endif /* LLSEC802154_USES_EXPLICIT_KEYS */

#if NETSTK_CFG_IEEE_802154G_EN
/**
 * @brief   This function returns total length of IE fields, including IE header
 *          and IE payloads
 */
static int ie_len(uint8_t *p_ie, uint16_t len)
{
    int ie_tot_len;
    uint16_t ie_hdr;
    uint8_t ie_len;
    uint8_t ie_id;
    uint8_t ie_type;
    uint8_t *p;
    uint8_t is_terminated;
    uint8_t is_ie_bad_formatted;

    ie_tot_len = len;
    if (p_ie) {
        /* use a temporary pointer to iterate IE fields */
        p = p_ie;

        /*
         * Parse Header IE list
         * - see IEEE-802.15.4e-2012, 5.2.4.22
         * - format:
         *      0...6    Length
         *      7...14   Element ID
         *      15       Type = 0
         *      0...127  IE Content
         */
        is_terminated = FALSE;
        is_ie_bad_formatted = FALSE;
        while ((is_terminated == FALSE) &&
               (is_ie_bad_formatted == FALSE)) {
            /*
             * the Header IE list is terminated with an IE List Termination IE
             * (ID = 0x7e or 0x7f) that has a content length of zero,
             * See IEEE-802.15.4e-2012, 5.2.4.22
             */
            ie_hdr = (uint16_t)(p[0] << 8) | (p[1]);
            ie_len = (ie_hdr) & 0x007Fu;
            ie_id =  (ie_hdr >> 7) & 0x00FFu;
            ie_type = (ie_hdr >> 15) & 0x0001u;    /* type is set to 0 */

            /* check header IE format, table 4b, IEEE-802.15.4e-2012 */
            is_ie_bad_formatted = (ie_id < 0x1a) ||
                                  ((ie_id > 0x21) && (ie_id < 0x7e)) ||
                                  (ie_id > 0x7f) ||
                                  (ie_type != 0);

            if (is_ie_bad_formatted == FALSE) {
                p += 2 + ie_len;
                len -= 2 + ie_len;
                is_terminated = (ie_len == 0) &&
                                ((ie_id == 0x7e) || (ie_id == 0x7f));
            }
        }

        /*
         * Parse Payload IE list
         * - see IEEE-802.15.4e-2012, 5.2.4.3
         * - format:
         *      0...10      Length
         *      11...14     ID
         *      15          Type = 1
         *      0...2047    IE Content
         */
        is_terminated = FALSE;
        while ((is_terminated == FALSE) &&
               (is_ie_bad_formatted == FALSE)) {
            /*
             * the Header IE list is terminated with a group ID of 0x0F
             * (ID = 0x7e or 0x7f) that has a content length of zero,
             * See IEEE-802.15.4e-2012, 5.2.4.22
             */
            ie_hdr = (uint16_t)(p[0] << 8) | (p[1]);
            ie_len = (ie_hdr) & 0x07ffu;
            ie_id = (ie_hdr >> 11) & 0x000fu;
            ie_type = (ie_hdr >> 15) & 0x0001u;    /* type is set to 1 */

            /* check payload IE format, table 4c, IEEE-802.15.4e-2012 */
            is_ie_bad_formatted = (ie_type != 1) ||
                                   ((ie_id != 0x00) &&
                                    (ie_id != 0x01) &&
                                    (ie_id != 0x0f));

            if (is_ie_bad_formatted == FALSE) {
                p += 2 + ie_len;
                len -= 2 + ie_len;
                is_terminated = (ie_len == 0) &&
                                (ie_id == 0x0f);
            }
        }
    }

    /* subtract remaining bytes after parsing IE fields */
    ie_tot_len -= len;
    return ie_tot_len;
}
#endif

/*----------------------------------------------------------------------------*/
static void
field_len(frame802154_t *p, field_length_t *flen)
{
  /* init flen to zeros */
  memset(flen, 0, sizeof(field_length_t));

  /* Determine lengths of each field based on fcf and other args */
  if(p->fcf.dest_addr_mode & 3) {
    flen->dest_pid_len = 2;
  }
  if(p->fcf.src_addr_mode & 3) {
    flen->src_pid_len = 2;
  }

  /* Set PAN ID compression bit if src pan id matches dest pan id. */
  if((p->fcf.dest_addr_mode & 3) && (p->fcf.src_addr_mode & 3) &&
     p->src_pid == p->dest_pid) {
    p->fcf.panid_compression = 1;

    /* compressed header, only do dest pid */
    flen->src_pid_len = 0;
  } else {
    p->fcf.panid_compression = 0;
  }

  /* determine address lengths */
  flen->dest_addr_len = addr_len(p->fcf.dest_addr_mode & 3);
  flen->src_addr_len = addr_len(p->fcf.src_addr_mode & 3);

#if LLSEC802154_SECURITY_LEVEL
  /* Aux security header */
  if(p->fcf.security_enabled & 1) {
      flen->aux_sec_len = 5
      #if LLSEC802154_USES_EXPLICIT_KEYS
              get_key_id_len(p->aux_hdr.security_control.key_id_mode);
      #endif /* LLSEC802154_USES_EXPLICIT_KEYS */
  }
#endif /* LLSEC802154_SECURITY_LEVEL */
}
/*----------------------------------------------------------------------------*/
/**
 *   \brief Calculates the length of the frame header.  This function is
 *   meant to be called by a higher level function, that interfaces to a MAC.
 *
 *   \param p Pointer to frame802154_t_t struct, which specifies the
 *   frame to send.
 *
 *   \return The length of the frame header.
*/
int
frame802154_hdrlen(frame802154_t *p)
{
  field_length_t flen;
  field_len(p, &flen);
  return 3 + flen.dest_pid_len + flen.dest_addr_len +
    flen.src_pid_len + flen.src_addr_len + flen.aux_sec_len;
}
/*----------------------------------------------------------------------------*/
/**
 *   \brief Creates a frame for transmission over the air.  This function is
 *   meant to be called by a higher level function, that interfaces to a MAC.
 *
 *   \param p Pointer to frame802154_t struct, which specifies the
 *   frame to send.
 *
 *   \param buf Pointer to the buffer to use for the frame.
 *
 *   \return The length of the frame header
*/
int
frame802154_create(frame802154_t *p, uint8_t *buf)
{
    int c;
    field_length_t flen;
    uint8_t pos;
#if LLSEC802154_USES_EXPLICIT_KEYS
    uint8_t key_id_mode;
#endif /* LLSEC802154_USES_EXPLICIT_KEYS */

    field_len(p, &flen);


#if NETSTK_CFG_IEEE_802154G_EN
    /* todo missing implementation for fully-supported IEEE802154g frame format
     * at the moment the frame is formated as following:
     * - Sequence Number Suppression is always zero, indicating sequence number
     *   field is present.
     * - IE List Present is always zero, indicating the information fields are
     *   absent in the frame.
     * - remaining fields are implemented as is in IEEE-802.15.4
     **/
#endif

    /* OK, now we have field lengths.  Time to actually construct */
    /* the outgoing frame, and store it in buf */
    buf[0] = (p->fcf.frame_type & 7)
            | ((p->fcf.security_enabled & 1) << 3)
            | ((p->fcf.frame_pending & 1) << 4)
            | ((p->fcf.ack_required & 1) << 5)
            | ((p->fcf.panid_compression & 1) << 6);

    buf[1] = ((p->fcf.dest_addr_mode & 3) << 2)
            | ((p->fcf.frame_version & 3) << 4)
            | ((p->fcf.src_addr_mode & 3) << 6);

    /* sequence number */
    buf[2] = p->seq;
    pos = 3;

    /* Destination PAN ID */
    if (flen.dest_pid_len == 2) {
        buf[pos++] = p->dest_pid & 0xff;
        buf[pos++] = (p->dest_pid >> 8) & 0xff;
    }

    /* Destination address */
    for (c = flen.dest_addr_len; c > 0; c--) {
        buf[pos++] = p->dest_addr[c - 1];
    }

    /* Source PAN ID */
    if (flen.src_pid_len == 2) {
        buf[pos++] = p->src_pid & 0xff;
        buf[pos++] = (p->src_pid >> 8) & 0xff;
    }

    /* Source address */
    for (c = flen.src_addr_len; c > 0; c--) {
        buf[pos++] = p->src_addr[c - 1];
    }
#if LLSEC802154_SECURITY_LEVEL
    /* Aux header */
    if(flen.aux_sec_len) {
        buf[pos++] = p->aux_hdr.security_control.security_level
#if LLSEC802154_USES_EXPLICIT_KEYS
        | (p->aux_hdr.security_control.key_id_mode << 3)
#endif /* LLSEC802154_USES_EXPLICIT_KEYS */
        memcpy(buf + pos, p->aux_hdr.frame_counter.u8, 4);
        pos += 4;

#if LLSEC802154_USES_EXPLICIT_KEYS
        key_id_mode = p->aux_hdr.security_control.key_id_mode;
        if(key_id_mode) {
            c = (key_id_mode - 1) * 4;
            memcpy(buf + pos, p->aux_hdr.key_source.u8, c);
            pos += c;
            buf[pos++] = p->aux_hdr.key_index;
        }
#endif /* LLSEC802154_USES_EXPLICIT_KEYS */
    }
#endif /* LLSEC802154_SECURITY_LEVEL */

    return (int) pos;
}
/*----------------------------------------------------------------------------*/
/**
 *   \brief Parses an input frame.  Scans the input frame to find each
 *   section, and stores the information of each section in a
 *   frame802154_t structure.
 *
 *   \param data The input data from the radio chip.
 *   \param len The size of the input data
 *   \param pf The frame802154_t struct to store the parsed frame information.
 */
int
frame802154_parse(uint8_t *data, int len, frame802154_t *pf)
{
    uint8_t *p;
    frame802154_fcf_t fcf;
    int c;
#if LLSEC802154_USES_EXPLICIT_KEYS
    uint8_t key_id_mode;
#endif /* LLSEC802154_USES_EXPLICIT_KEYS */

    if (len < 3) {
        return 0;
    }

    p = data;

    /* decode the FCF */
    fcf.frame_type = p[0] & 7;
    fcf.security_enabled = (p[0] >> 3) & 1;
    fcf.frame_pending = (p[0] >> 4) & 1;
    fcf.ack_required = (p[0] >> 5) & 1;
    fcf.panid_compression = (p[0] >> 6) & 1;

#if NETSTK_CFG_IEEE_802154G_EN
    /* todo missing implementation for fully-supported IEEE802154g frame format
     * at the moment the frame is formated as following:
     * - Sequence Number Suppression is ignored.
     * - IE List Present is ignored
     **/
    fcf.seq_suppression = (p[1] & 0x01);
    fcf.ie_list_present = (p[1] >> 1) & 0x01 ;
#endif
    fcf.dest_addr_mode = (p[1] >> 2) & 3;
    fcf.frame_version = (p[1] >> 4) & 3;
    fcf.src_addr_mode = (p[1] >> 6) & 3;

    /* copy fcf and seqNum */
    memcpy(&pf->fcf, &fcf, sizeof(frame802154_fcf_t));
    pf->seq = p[2];
    p += 3; /* Skip first three bytes */

    /* Destination address, if any */
    if (fcf.dest_addr_mode) {
        /* Destination PAN */
        pf->dest_pid = p[0] + (p[1] << 8);
        p += 2;

        /* Destination address */
        /*     l = addr_len(fcf.dest_addr_mode); */
        /*     for(c = 0; c < l; c++) { */
        /*       pf->dest_addr.u8[c] = p[l - c - 1]; */
        /*     } */
        /*     p += l; */
        if (fcf.dest_addr_mode == FRAME802154_SHORTADDRMODE) {
            linkaddr_copy((linkaddr_t *) &(pf->dest_addr), &linkaddr_null);
            pf->dest_addr[0] = p[1];
            pf->dest_addr[1] = p[0];
            p += 2;
        } else if (fcf.dest_addr_mode == FRAME802154_LONGADDRMODE) {
            for (c = 0; c < 8; c++) {
                pf->dest_addr[c] = p[7 - c];
            }
            p += 8;
        }
    } else {
        linkaddr_copy((linkaddr_t *) &(pf->dest_addr), &linkaddr_null);
        pf->dest_pid = 0;
    }

    /* Source address, if any */
    if (fcf.src_addr_mode) {
        /* Source PAN */
        if (!fcf.panid_compression) {
            pf->src_pid = p[0] + (p[1] << 8);
            p += 2;
        } else {
            pf->src_pid = pf->dest_pid;
        }

        /* Source address */
        /*     l = addr_len(fcf.src_addr_mode); */
        /*     for(c = 0; c < l; c++) { */
        /*       pf->src_addr.u8[c] = p[l - c - 1]; */
        /*     } */
        /*     p += l; */
        if (fcf.src_addr_mode == FRAME802154_SHORTADDRMODE) {
            linkaddr_copy((linkaddr_t *) &(pf->src_addr), &linkaddr_null);
            pf->src_addr[0] = p[1];
            pf->src_addr[1] = p[0];
            p += 2;
        } else if (fcf.src_addr_mode == FRAME802154_LONGADDRMODE) {
            for (c = 0; c < 8; c++) {
                pf->src_addr[c] = p[7 - c];
            }
            p += 8;
        }
    } else {
        linkaddr_copy((linkaddr_t *) &(pf->src_addr), &linkaddr_null);
        pf->src_pid = 0;
    }
#if LLSEC802154_SECURITY_LEVEL
    if(fcf.security_enabled) {
        pf->aux_hdr.security_control.security_level = p[0] & 7;
#if LLSEC802154_USES_EXPLICIT_KEYS
        pf->aux_hdr.security_control.key_id_mode = (p[0] >> 3) & 3;
#endif /* LLSEC802154_USES_EXPLICIT_KEYS */
        p += 1;

        memcpy(pf->aux_hdr.frame_counter.u8, p, 4);
        p += 4;

#if LLSEC802154_USES_EXPLICIT_KEYS
        key_id_mode = pf->aux_hdr.security_control.key_id_mode;
        if(key_id_mode) {
            c = (key_id_mode - 1) * 4;
            memcpy(pf->aux_hdr.key_source.u8, p, c);
            p += c;
            pf->aux_hdr.key_index = p[0];
            p += 1;
        }
#endif /* LLSEC802154_USES_EXPLICIT_KEYS */
    }
#endif /* LLSEC802154_SECURITY_LEVEL */

#if NETSTK_CFG_IEEE_802154G_EN
    /* parse Information Element Fields */
    uint16_t ie_tot_len = 0;
    if (fcf.ie_list_present) {
        ie_tot_len = ie_len(p, len - (p - data));
        if (ie_tot_len == 0) {
            /* IE fields are badly formatted, then terminate parsing process */
            return 0;
        } else {
            p += ie_tot_len;
        }
    }
#endif

    /* header length */
    c = p - data;
    /* payload length */
    pf->payload_len = (len - c);
    /* payload */
    pf->payload = p;

    /* return header length if successful */
    return c > len ? 0 : c;
}
/*----------------------------------------------------------------------------*/
void frame802154_setDSN(uint8_t dsn)
{
    Framer802154_DSN = dsn;
}
/*----------------------------------------------------------------------------*/
uint8_t frame802154_getDSN(void)
{
    return Framer802154_DSN;
}
/*----------------------------------------------------------------------------*/
uint8_t frame802154_broadcast(frame802154_t *p)
{
    uint8_t i = p->fcf.dest_addr_mode == FRAME802154_SHORTADDRMODE ? 2 : 8;
    while (i-- > 0) {
        if (p->dest_addr[i] != 0xff) {
            return 0;
        }
    }
    return 1;
}

/** \}   */
/** @} */