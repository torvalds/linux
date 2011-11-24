/*******************************************************************************

            Copyright (c) 2004 by Nanoradio AB 

This software is copyrighted by and is the sole property of Nanoradio AB.
 All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                       http://www.wep.com
SWEDEN
*******************************************************************************/
/*----------------------------------------------------------------------------*/
/*! \file

\brief [this module handles things related to life, universe and everythig]

This module is part of the macll block.
Thing are coming in and things are coming out, bla bla bla.
]
*/
/*----------------------------------------------------------------------------*/
#ifndef MAC_API_DATA_H
#define MAC_API_DATA_H
#include "mac_api_defs.h"
#include "m80211_stddefs.h"

/* E X P O R T E D  D E F I N E S ********************************************/

/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_MAC_DATA_REQ        (0  | MAC_API_PRIMITIVE_TYPE_REQ)
#define HIC_MAC_DATA_RSP        (1  | MAC_API_PRIMITIVE_TYPE_RSP)

/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define HIC_MAC_DATA_IND        (0  | MAC_API_PRIMITIVE_TYPE_IND)
#define HIC_MAC_DATA_CFM        (1  | MAC_API_PRIMITIVE_TYPE_CFM)



/******************************************************************************/
/* PARAMETER DEFINITIONS                                                      */
/******************************************************************************/
typedef uint16_t mac_svc_t;
#define MAC_SVC_STRICT                 0x01      /* strictly ordered */
#define MAC_SVC_REORD                  0
#define MAC_SVC_URGENT_PRIO_OVERRIDE   0x02
#define MAC_SVC_KEY_SYNC               0x04
#define MAC_SVC_PAD_MASK_OFFSET        3
#define MAC_SVC_PAD_MASK_WIDTH         2
#define MAC_SVC_PAD_MASK               (((1<<MAC_SVC_PAD_MASK_WIDTH)-1)<<MAC_SVC_PAD_MASK_OFFSET)



#define MAC_DATA_CFM_MSG_SIZE          8

/******************************************************************************/
/* DRIVER TO MAC PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t trans_id;
   uint16_t          vlanid_prio;
   mac_svc_t         svc; /* XXX: Until something else causes us to no
                           * longer be backward compatible, this will
                           * contain both the svc value and rate coded
                           * into highest byte */
}m80211_mac_data_req_header_t;

#define M80211_MAC_DATA_REQ_HEADER_SIZE   8

#define HIC_DATA_REQ_SET_TRANS_ID(_pkt, _transid) HIC_PUT_ULE32(_pkt, _transid)

#define HIC_DATA_REQ_GET_TRANS_ID(_pkt) HIC_GET_ULE32(_pkt)

#define HIC_DATA_REQ_SET_VLANID_PRIO(_pkt,_prio)	\
   HIC_PUT_ULE16((unsigned char*)(_pkt) + 4, (_prio))
	
#define HIC_DATA_REQ_GET_VLANID_PRIO(_pkt)	\
   HIC_GET_ULE16((unsigned char*)(_pkt) + 4)

#define HIC_DATA_REQ_SET_SVC(_pkt, _svc)		\
   HIC_PUT_ULE16((unsigned char*)(_pkt) + 6, (_svc))
	
#define HIC_DATA_REQ_GET_SVC(_pkt)		\
   HIC_GET_ULE16((unsigned char*)(_pkt) + 6)

#define HIC_DATA_HEADER_SIZE(_pkt) \
   (8 + (HIC_DATA_REQ_GET_SVC(_pkt) >> MAC_SVC_PAD_MASK_OFFSET) & ((1 << MAC_SVC_PAD_MASK_WIDTH) - 1))



/******************************************************************************/
/* MAC TO DRIVER PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
   mac_api_transid_t          trans_id;
   uint8_t                    status;
   m80211_std_rate_encoding_t rate_used;
   uint8_t                    prio;
   uint8_t                    discarded_rx;
}m80211_mac_data_cfm_t;

#define HIC_DATA_CFM_SET_TRANS_ID(_pkt, _transid) HIC_DATA_REQ_SET_TRANS_ID(_pkt, _transid)
#define HIC_DATA_CFM_GET_TRANS_ID(_pkt)           HIC_DATA_REQ_GET_TRANS_ID(_pkt)

#define HIC_DATA_CFM_STATUS(_pkt)       (((uint8_t*)_pkt)[4])
#define HIC_DATA_CFM_RATE_USED(_pkt)    (((uint8_t*)_pkt)[5])
#define HIC_DATA_CFM_RATE_PRIO(_pkt)    (((uint8_t*)_pkt)[6])
#define HIC_DATA_CFM_DISCARDED_RX(_pkt) (((uint8_t*)_pkt)[7])

typedef m80211_mac_data_req_header_t m80211_mac_data_ind_header_t;

#define HIC_DATA_IND_SET_TRANS_ID(_pkt, _transid) HIC_DATA_REQ_SET_TRANS_ID(_pkt, _transid)
#define HIC_DATA_IND_GET_TRANS_ID(_pkt)           HIC_DATA_REQ_GET_TRANS_ID(_pkt)
#define HIC_DATA_IND_SET_VLANID_PRIO(_pkt)        HIC_DATA_REQ_GET_VLANID_PRIO(_pkt)
#define HIC_DATA_IND_SET_SVC(_pkt, _svc)          HIC_DATA_REQ_SET_SVC(_pkt, _svc)
#define HIC_DATA_IND_GET_SVC(_pkt)                HIC_DATA_REQ_GET_SVC(_pkt)
#define HIC_DATA_IND_RATE_USED(_pkt)              (((uint8_t*)_pkt)[7])
#define HIC_DATA_IND_PAYLOAD_BUF(_pkt)            (char*)(&((uint8_t*)_pkt)[8])
#define HIC_DATA_IND_PAYLOAD_SIZE(_msg_size)      (_msg_size - 8)


/**********************************************************************************/
/*************************** END OF MESSAGE DEFINITIONS ***************************/
/**********************************************************************************/


/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_DATA_H */
/* END OF FILE ***************************************************************/
