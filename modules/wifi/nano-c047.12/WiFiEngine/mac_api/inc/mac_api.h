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
#ifndef MAC_API_H
#define MAC_API_H
#include "mac_api_console.h"
#include "mac_api_data.h"
#include "mac_api_ifctrl.h"
#include "mac_api_mib.h"
#include "mac_api_mlme.h"
#include "mac_api_nvm.h"
#include "mac_api_echo.h"
#include "mac_api_dlm.h"

/* E X P O R T E D  D E F I N E S ********************************************/


#define MAC_API_PRIMITIVE_IS_REQ(_msg_id)\
   (((_msg_id) & MAC_API_PRIMITIVE_TYPE_BIT) == MAC_API_PRIMITIVE_TYPE_REQ)

#define MAC_API_PRIMITIVE_IS_CFM(_msg_id)\
   (((_msg_id) & MAC_API_PRIMITIVE_TYPE_BIT) == MAC_API_PRIMITIVE_TYPE_CFM)

#define MAC_API_SYSTEM_CAPABILITY_SCAN_COUNTRY_INFO_SET 0x00000001

/* Generic result codes from HIC API functions. */
typedef int mac_api_result_t;
#define MAC_API_RESULT_NO_CFM          0
#define MAC_API_RESULT_IMMIDATE_CFM    1
#define MAC_API_RESULT_SCATTERED_CFM   2
#define MAC_API_RESULT_ASYNCH_REQ      3

typedef void* mac_api_ctx_t;

/***************************************************************************
 W R A P P E R   G E N E R A T I O N
****************************************************************************/


typedef struct
{
   m80211_mlme_host_header_t        hic_host_header;
   m80211_mlme_status_cfm_t         hic_status_cfm;
   m80211_mlme_addr_and_short_ind_t hic_addr_and_type_ind;
   m80211_mlme_parameter_cfm_t      hic_parameter_cfm;
   m80211_mlme_association_cfm_t    hic_association_cfm;
   hic_message_header_t             hic_message_header;
   hic_message_control_t            hic_message_ctrl;
   m80211_mac_data_req_header_t     hic_data_header;
   m80211_mac_data_cfm_t            hic_data_cfm;
   mlme_mgmt_body_t                 hic_mlme_mgmt_body;
   hic_ctrl_msg_t                   hic_ctrl_msg;
   mac_mmpdu_beacon_ind_t           beacon_ind;
}hic_interface_wrapper_t;

/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_H */
/* END OF FILE ***************************************************************/
