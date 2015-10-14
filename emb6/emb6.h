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
 * 	 \addtogroup emb6
 * 	 @{
 *   \addtogroup stack_API Stack API
 *   @{
*/
/*! \file   emb6.h

    \author Peter Lehmann peter.lehmann@hs-offenburg.de

    \brief  emb6 API

	\version 0.0.1
*/

#ifndef EMB6_H_
#define EMB6_H_

/*==============================================================================
                      	  	  INCLUDES
==============================================================================*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

/*=============================================================================
                                BASIC CONSTANTS
==============================================================================*/
#undef  FALSE
#undef  TRUE
#define FALSE                               0 /* do not change                */
#define TRUE                                1 /* do not change                */
typedef void                                (*pfn_intCallb_t)(void *);

/* use only Exact-width integer types, linked with TMR_OVRFLOW_VAL 			  */
typedef uint32_t                            clock_time_t;
/* linked with clock_time_t, maximum value for clock_time_t variables	      */
#define TMR_OVRFLOW_VAL                     0xffffffff

/*==============================================================================
                                     MACROS
 =============================================================================*/
/** defines for the modulation type for RF transceiver */
#define MODULATION_QPSK100          0
#define MODULATION_BPSK20           1


/*==============================================================================
                        RPL Configuration
 =============================================================================*/

/*! \struct rpl_configuration
	\brief for dynamic RPL parameter configuration
*/
typedef struct rpl_configuration
{
    /* The DIO interval (n) represents 2^n ms. default value = 8 */
    uint8_t DIOintmin;
    /* Maximum amount of timer doublings. default value = 12 */
    uint8_t DIOintdoub;
    /* This value decides which DAG instance we should participate in by def.
     * default value = 0x1e (30) */
    uint8_t defInst;
    /* Initial metric attributed to a link when the ETX is unknown.
     * default value = 2 */
    uint8_t linkMetric;
    /* Default route lifetime unit. This is the granularity of time used in RPL
     * lifetime values, in seconds.
     * default value = 0xffff */
    uint16_t defRouteTimeUnit;
    /* Default route lifetime as a multiple of the lifetime unit.
     * default value = 0xff */
    uint8_t defRouteTime;
}s_rpl_conf_t;

/*! RPL configuration struct, do not change */
extern s_rpl_conf_t     rpl_config;

/*==============================================================================
                         MAC & PHY Parameter Configuration
 =============================================================================*/

/*! \struct mac_phy_configuration
*   \brief for initial mac_phy parameter configuration,
*   if changed during runtime RF-interface must be re-initialized
*/
typedef struct mac_phy_configuration
{
    /** MAC address, default value: { 0x00,0x50,0xc2,0xff,0xfe,0xa8,0xdd,0xdd}*/
    uint8_t mac_address[8];
    /** PAN ID, default value: 0xABCD */
    uint16_t pan_id;
    /** initial tx power, default value: 11 dBm */
    int8_t init_power;
    /** initial rx sensitivity, default value: -100 dBm */
    int8_t init_sensitivity;
    /** rf modulation type, default value BPSK20 */
    uint8_t modulation;
}s_mac_phy_conf_t;

/*! MAC configuration struct, do not change */
extern s_mac_phy_conf_t     mac_phy_config;

/*==============================================================================
                                     ENUMS
==============================================================================*/
/*! \enum e_radio_tx_status_t
    \brief Return code of an interface driver
*/
typedef enum {
    RADIO_TX_OK = 1,
    RADIO_TX_COLLISION = 2,
    RADIO_TX_NOACK = 3,
    RADIO_TX_ERR = 4
}e_radio_tx_status_t;

/*==============================================================================
                          SYSTEM STRUCTURES AND OTHER TYPEDEFS
 =============================================================================*/

//! We are using IEEE 802.15.4
#define UIP_CONF_LL_802154                  TRUE
#define UIP_CONF_LLH_LEN                    0
#define	PRINT_PCK_STAT                      FALSE
#define TIMESTAMP_PERIOD_SEC                10	// in sec

#ifdef LINKADDR_CONF_SIZE
#define LINKADDR_SIZE                       LINKADDR_CONF_SIZE
#else /* LINKADDR_SIZE */
#define LINKADDR_SIZE                       8
#endif /* LINKADDR_SIZE */


typedef union {
    unsigned char u8[LINKADDR_SIZE];
} linkaddr_t;

/** \brief 16 bit 802.15.4 address */
typedef struct uip_802154_shortaddr {
    uint8_t addr[2];
} uip_802154_shortaddr;
/** \brief 64 bit 802.15.4 address */
typedef struct uip_802154_longaddr {
    uint8_t addr[8];
} uip_802154_longaddr;

#if UIP_CONF_LL_802154
/** \brief 802.15.4 address */
typedef uip_802154_longaddr                 uip_lladdr_t;
#define UIP_802154_SHORTADDR_LEN            2
#define UIP_802154_LONGADDR_LEN             8
#define UIP_LLADDR_LEN                      UIP_802154_LONGADDR_LEN
#else /*UIP_CONF_LL_802154*/
#if UIP_CONF_LL_80211
/** \brief 802.11 address */
typedef uip_80211_addr uip_lladdr_t;
#define UIP_LLADDR_LEN 6
#else /*UIP_CONF_LL_80211*/
/** \brief Ethernet address */
typedef uip_eth_addr uip_lladdr_t;
#define UIP_LLADDR_LEN 6
#endif /*UIP_CONF_LL_80211*/
#endif /*UIP_CONF_LL_802154*/


/*==============================================================================
                         Netstack
 =============================================================================*/

typedef struct netstack {
        /*const struct netstack_socket*                   sock;*/

	const struct netstack_headerCompression*        hc;

	const struct netstack_llsec*                    llsec;

	const struct netstack_highMac*                  hmac;

	const struct netstack_lowMac*                   lmac;

	const struct netstack_framer*                   frame;

	const struct netstack_interface*                inif;

	uint8_t                                         c_configured;

#if STK_CFG_REFACTOR_EN
	const struct radio_drv_api      				*radio;    /* Radio driver with new API */
#endif

}s_ns_t;

typedef const struct netstack_socket{
    char *name;

    /* Initialize the BSD socket driver */
    void (* create)(s_ns_t* p_ns);

    /* Connect to remote node*/
    void (* connect)(void);

    void (* bind)(void);

    /* Send data to remote node*/
    void (* send)(void);

    /* Send data to remote node*/
    void (* sendto)(void);

    /* Close the BSD socket driver */
    void (* close)(s_ns_t* p_ns);

}s_nsSocket_t;

typedef const struct netstack_headerCompression {
    char *name;

    /** Initialize the network driver */
    void (* init)(s_ns_t* p_ns);

    /** Callback for getting notified of incoming packet. */
    void (* input)(void);
}s_nsHeadComp_t;

typedef void (* mac_callback_t)(void *ptr, int status, int transmissions);

/**
 * The structure of a link layer security driver.
 */
typedef const struct netstack_llsec {
    char *name;

    /** Initializes link layer security and thereafter starts upper layers. */
    void (* init)(s_ns_t* p_ns);

    /** Secures outgoing frames before passing them to NETSTACK_MAC. */
    void (* send)(mac_callback_t sent_callback, void *ptr);

    /**
     * Once the netstack_framer wrote the headers, the llsec driver
     * can generate a MIC over the entire frame.
     * \return Returns != 0 <-> success
     */
    int (* on_frame_created)(void);

    /**
     * Decrypts incoming frames;
     * filters out injected or replayed frames.
     */
    void (* input)(void);

    /** Returns the security-related overhead per frame in bytes */
    uint8_t (* get_overhead)(void);
}s_nsllsec_t;

typedef const struct netstack_highMac {
    char *name;

    /** Initialize the MAC driver */
    void (* init)(s_ns_t* p_ns);

    /** Send a packet from the Rime buffer  */
    void (* send)(mac_callback_t sent_callback, void *ptr);

    /** Callback for getting notified of incoming packet. */
    void (* input)(void);

    /** Turn the MAC layer on. */
    int8_t (* on)(void);

    /** Turn the MAC layer off. */
    int8_t (* off)(int keep_radio_on);

    /** Returns the channel check interval, expressed in clock_time_t ticks. */
    unsigned short (* channel_check_interval)(void);
}s_nsHighMac_t;

/* List of packets to be sent by LMAC layer */
typedef struct lmac_buf_list {
    struct lmac_buf_list    *next;
    struct queuebuf         *buf;
    void                    *ptr;
}s_nsLmacBufList_t;

typedef const struct netstack_lowMac {
    char *name;

    /** Initialize the RDC driver */
    void (* init)(s_ns_t* p_ns);

    /** Send a packet from the Rime buffer  */
    void (* send)(mac_callback_t sent_callback, void *ptr);

    /** Send a packet list */
    void (* send_list)(mac_callback_t sent_callback, void *ptr,
                       s_nsLmacBufList_t *list);

    /** Callback for getting notified of incoming packet. */
    void (* input)(void);

    /** Turn the MAC layer on. */
    int8_t (* on)(void);

    /** Turn the MAC layer off. */
    int8_t (* off)(int keep_radio_on);

    /** Returns the channel check interval, expressed in clock_time_t ticks. */
    unsigned short (* channel_check_interval)(void);
}s_nsLowMac_t;

typedef const struct netstack_framer {
    char *name;

    int8_t (* init)(s_ns_t* p_ns);

    int8_t (* length)(void);

    int8_t (* create)(void);

    int8_t (* create_and_secure)(s_ns_t* p_ns);

    int8_t (* parse)(void);
}s_nsFramer_t;

typedef const struct netstack_interface {
    char *name;

    int8_t (* init)(s_ns_t* p_ns);

    /** Prepare & transmit a packet. */
    int8_t (* send)(const void *pr_payload, uint8_t c_len);

    /** Turn the radio on. */
    int8_t (* on)(void);

    /** Turn the radio off. */
    int8_t (* off)(void);

    /** Set TX-Power */
    void (* set_txpower)(int8_t power);

    /** Get TX-Power */
    int8_t (* get_txpower)(void);

    /** Set Sensitivity */
    void (* set_sensitivity)(int8_t sens);

    /** Get Sensitivity */
    int8_t (* get_sensitivity)(void);

    /** Get RSSI Value */
    int8_t (* get_rssi)(void);

    /** Set Antenna Diversity */
    void (* ant_div)(uint8_t value);

    /** Set RF Switch*/
    void (* ant_rf_switch)(uint8_t value);

    /** Set promiscuous mode */
    void (* set_promisc)(uint8_t c_on_off);

}s_nsIf_t;

/*! Supported BSD-like socket interface */
/*extern const s_nsSocket_t udp_socket_driver;
extern const s_nsSocket_t tcp_socket_driver;*/



typedef uint16_t    STK_ERR;
typedef uint16_t    STK_DEV_ID;
typedef uint16_t    RADIO_ERR;
typedef uint8_t     RADIO_IOC_CMD;
typedef uint16_t    RADIO_IOC_VAL;

typedef struct apss_framer_api      APSS_FRAMER_DRV;
typedef struct radio_drv_api        RADIO_DRV_API;

/**
 * @addtogroup  STACK_ERROR_CODES   Stack error codes
 * @{
 */
#define STK_ERR_NONE                                    (STK_ERR) (  0u )
#define STK_ERR_BUSY                                    (STK_ERR) (  1u )
#define STK_ERR_TX_RADIO_SEND                           (STK_ERR) (  2u )
#define STK_ERR_TX_TIMEOUT                              (STK_ERR) (  3u )
#define STK_ERR_TX_NOPACK                               (STK_ERR) (  4u )
#define STK_ERR_INVALID_ARGUMENT                        (STK_ERR) (  5u )

#define STK_ERR_APSS_INVALID_ACK                        (STK_ERR) ( 12u )
#define STK_ERR_APSS_UNSUPPORTED_FRAME                  (STK_ERR) ( 13u )
#define STK_ERR_APSS_BROADCAST_LAST_STROBE              (STK_ERR) ( 14u )
#define STK_ERR_APSS_BROADCAST_NOACK                    (STK_ERR) ( 15u )
#define STK_ERR_APSS_CHANNEL_ACESS_FAILURE              (STK_ERR) ( 16u )
#define STK_ERR_APSS_INVALID_ADDR                       (STK_ERR) ( 17u )

#if 1   // Newly added
#define STK_ERR_APSS_TX_COLLISION_SAME_DEST             (STK_ERR) ( 21u )       /* a waking-up strobe is not destined for us, but aims to same destination  */
#define STK_ERR_APSS_TX_COLLISION_DIFF_DEST             (STK_ERR) ( 22u )       /* a waking-up strobe is destined neither for us nor our destination node   */
#endif

#define STK_ERR_CMD_INVALID                             (STK_ERR) ( 31u )

/**
 * @}
 */

#if STK_CFG_REFACTOR_EN
#define STK_APSS_CMD_NONE               (uint8_t) ( 0u )
#define STK_APSS_CMD_CSMA               (uint8_t) ( 1u )
#endif

/**
 * @addtogroup  RADIO_ERROR_CODES  Radio error codes
 * @{
 */
#define RADIO_ERR_NONE                  (RADIO_ERR) ( 0u )
#define RADIO_ERR_CMD_UNSUPPORTED       (RADIO_ERR) ( 1u )
#define RADIO_ERR_TX                    (RADIO_ERR) ( 2u )
#define RADIO_ERR_ONOFF                 (RADIO_ERR) ( 3u )
#define RADIO_ERR_INIT                  (RADIO_ERR) ( 4u )

/**
 * @}
 */
/**
 * @addtogroup  RADIO_IOC_CMD Radio I/O control commands
 * @{
 */
#define RADIO_IOC_CMD_TXPOWER_SET           (RADIO_IOC_CMD) (  1u )
#define RADIO_IOC_CMD_TXPOWER_GET           (RADIO_IOC_CMD) (  2u )
#define RADIO_IOC_CMD_SENS_SET              (RADIO_IOC_CMD) (  3u )
#define RADIO_IOC_CMD_SENS_GET              (RADIO_IOC_CMD) (  4u )
#define RADIO_IOC_CMD_RSSI_GET              (RADIO_IOC_CMD) (  5u )
#define RADIO_IOC_CMD_CCA_GET               (RADIO_IOC_CMD) (  6u )
#define RADIO_IOC_CMD_ANT_DIV_SET           (RADIO_IOC_CMD) (  7u )
#define RADIO_IOC_CMD_RF_SWITCH             (RADIO_IOC_CMD) (  8u )
#define RADIO_IOC_CMD_SYNC_SET              (RADIO_IOC_CMD) (  9u )
#define RADIO_IOC_CMD_SYNC_GET              (RADIO_IOC_CMD) ( 10u )
#define RADIO_IOC_CMD_STATE_GET             (RADIO_IOC_CMD) ( 11u )




#if 0 // not needed at the time being
#define RADIO_IOC_CMD_MACADDR_SET           (RADIO_IOC_CMD) ( 11u )
#define RADIO_IOC_CMD_MACADDR_GET           (RADIO_IOC_CMD) ( 12u )
#define RADIO_IOC_CMD_ED_GET                (RADIO_IOC_CMD) ( 20u )
#endif

/**
 * @}
 */


/**
 * @addtogroup  RADIO_IOC_VAL
 * @{
 */
#define RADIO_IOC_VAL_SYNC_STROBE               (RADIO_IOC_VAL) ( 0x930B )
#define RADIO_IOC_VAL_SYNC_DATA                 (RADIO_IOC_VAL) ( 0x51DE )
#define RADIO_IOC_VAL_STATE_NONE                (RADIO_IOC_VAL) ( 0u )
#define RADIO_IOC_VAL_STATE_IDLE                (RADIO_IOC_VAL) ( 1u )
#define RADIO_IOC_VAL_STATE_RX                  (RADIO_IOC_VAL) ( 2u )
#define RADIO_IOC_VAL_STATE_TX                  (RADIO_IOC_VAL) ( 3u )
/**
 * @}
 */


/**
 * @brief   Asynchronous Power Saving Scheme Framer API
 */
struct apss_framer_api
{
    char         *Name;

    void        (*Init        )(STK_ERR *p_err);

    void        (*Deinit      )(STK_ERR *p_err);

    uint8_t*    (*Create      )(uint8_t frame_type, uint16_t *p_len, uint32_t *p_delay, STK_ERR *p_err);

    void        (*Parse       )(uint8_t *p_pkt, uint16_t len, STK_ERR *p_err);
};


/**
 * @brief   Radio transceiver driver API
 */
struct radio_drv_api
{
    char     *Name;

    void    (*Init ) (s_ns_t *p_netstack, RADIO_ERR *p_err);

    void    (*On   ) (RADIO_ERR *p_err);                                            /*!< Open the driver        */

    void    (*Off  ) (RADIO_ERR *p_err);                                            /*!< Close the driver       */

    void    (*Send ) (const void *p_payload, uint8_t len, RADIO_ERR *p_err);        /*!< Write data to radio    */

    void    (*Recv ) (void *p_buf, uint8_t len, RADIO_ERR *p_err);                  /*!< Read data from radio   */

    void    (*Ioctrl)(RADIO_IOC_CMD cmd, RADIO_IOC_VAL *p_val, RADIO_ERR *p_err);   /*!< Input/Output control   */

    void    (*Task ) (void *p_arg);                                                 /*!< State machine handler  */
};


extern       APSS_FRAMER_DRV         XMACFramer;
extern       APSS_FRAMER_DRV         SmartMACFramer;
extern       RADIO_DRV_API           CC112xDrv;



/*! Supported headers compression handlers */
extern const s_nsHeadComp_t     sicslowpan_driver;


/*! This driver are pretending to be a hc layer
 *  for sniffing data and sending them via USART     */
extern const s_nsHeadComp_t     slipnet_driver;


/*! Supported link layer security handlers */
extern const s_nsllsec_t        nullsec_driver;


/*! Supported high mac handlers */
extern const s_nsHighMac_t      nullmac_driver;
extern const s_nsHighMac_t      simplemac_driver;

/*! Supported low mac handlers */
extern const s_nsLowMac_t       sicslowmac_driver;
extern const s_nsLowMac_t       nullrdc_driver;
extern const s_nsLowMac_t       apss_driver;


/*! Supported framers */
extern const s_nsFramer_t       framer_802154;
extern const s_nsFramer_t       no_framer;
extern const s_nsFramer_t       nullframer;


/*! Supported interfaces */
extern const s_nsIf_t           rf212_driver;
extern const s_nsIf_t           rf212b_driver;
extern const s_nsIf_t           rf230_driver;
extern const s_nsIf_t           native_driver;
extern const s_nsIf_t           fradio_driver;


/*==============================================================================
                                 API FUNCTIONS
 =============================================================================*/

/*============================================================================*/
/*!
\brief   initialize all stack functions

    This function inits the board support packet and the complete netstack

\return returns 0 if failed, 1 if success
*/
/*============================================================================*/
uint8_t emb6_init(s_ns_t * ps_netstack);

/*============================================================================*/
/*!
\brief   emb6 process function

    This function handles all events and timers of the emb6 stack in a loop

\param 	 delay sets a delay in µs at the end of the function

\return  none

*/
/*============================================================================*/
void emb6_process(uint16_t delay);

/*============================================================================*/
/*!
\brief   Function which assign a given pointer to a current network stack ptr


\return  pointer to a stack

*/
/*============================================================================*/
s_ns_t * emb6_get(void);

/*=============================================================================
                                	UTILS SECTION
==============================================================================*/
// Used in several files.
#define CCIF
#define CLIF

#ifndef	QUEUEBUF_CONF_NUM
#define QUEUEBUF_CONF_NUM                   4
#endif

#ifndef QUEUEBUF_CONF_REF_NUM
#define QUEUEBUF_CONF_REF_NUM               4
#endif


#endif /* EMB6_H_ */

/** @} */
/** @} */
