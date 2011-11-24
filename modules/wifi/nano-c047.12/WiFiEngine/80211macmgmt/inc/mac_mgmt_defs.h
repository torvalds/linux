/*****************************************************************************

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
Torshamnsgatan 39                       
164 40 Kista                            
SWEDEN

Module Description :
==================
Implementation of the Host Interface Controller module. The module
implements the interface between SPI/UART and the wrapper modules.

Revision History:
=================

--------------------------------------------------------------------
$Workfile:  
$Revision:   
--------------------------------------------------------------------

*****************************************************************************/
#ifndef MAC_MGMT_DEFS_H
#define MAC_MGMT_DEFS_H

#include "m80211_defs.h"
#include "mac_api_mlme.h"

/******************************************************************************
C O N S T A N T S / M A C R O S
******************************************************************************/
#define M80211_SSID_LIST_MAX_LENGTH          5
#define M80211_TIMESTAMP_LENGTH              8
#define M80211_SCAN_TYPE_PASSIVE             0
#define M80211_SCAN_TYPE_ACTIVE              1
#define M80211_PKMID_LIST_SIZE              16
#define M80211_PROTECT_LIST_SIZE             1

/* Type of MGMT_MAIN_STATE_BSS */
#define M80211_MLME_VALID_PARAM   0
#define M80211_MLME_INVALID_PARAM 1

/* Type of MGMT_MAIN_STATE_BSS */
#define M80211_MLME_WMM_UNSUPPORTED   0
#define M80211_MLME_WMM_SUPPORTED     1


/* Management reason codes */
#define M80211_MGMT_RC_UNSPECIFIED                1
#define M80211_MGMT_RC_NOT_VALID                  2
#define M80211_MGMT_RC_LEAVING_IBSS               3
#define M80211_MGMT_RC_INACTIVITY                 4
#define M80211_MGMT_RC_AP_TO_MANY_STA             5
#define M80211_MGMT_RC_CLASS2_FRAME               6
#define M80211_MGMT_RC_CLASS3_FRAME               7
#define M80211_MGMT_RC_LEAVING_BSS                8
#define M80211_MGMT_RC_ASS_NOT_AUTH               9
#define M80211_MGMT_RC_INVALID_INFO_ELEMENT      13
#define M80211_MGMT_RC_MIC_FAILURE               14
#define M80211_MGMT_RC_4_WAY_HANDSHAKE_TO        15
#define M80211_MGMT_RCGROUP_KEY_UPDATE_TO        16
#define M80211_MGMT_RC_INFO_ELEMENT_DIFFER       17
#define M80211_MGMT_RC_MCAST_CIPHER_NOT_VALID    18
#define M80211_MGMT_RC_UCAST_CIPHER_NOT_VALID    19
#define M80211_MGMT_RC_AKMP_NOT_VALID            20
#define M80211_MGMT_RC_UNSUPPORTED_RSNE_VERSION  21
#define M80211_MGMT_RC_INVALID_RSNE_CAPABILITIES 22
#define M80211_MGMT_RC_AUTHETICATION_FAILED      23

/* NRP status codes */
#define M80211_NRP_MLME_LEAVE_BSS_CFM_SUCCESS    0
#define M80211_NRP_MLME_LEAVE_BSS_CFM_FAILED     1
#define M80211_NRP_MLME_LEAVE_IBSS_CFM_SUCCESS   0
#define M80211_NRP_MLME_LEAVE_IBSS_CFM_FAILED    1


/******************************************************************************
T Y P E D E F ' S
******************************************************************************/




typedef struct 
{
   uint8_t          no_ssIDs;
   m80211_ie_ssid_t ssIDList[M80211_SSID_LIST_MAX_LENGTH];
}ssID_list_t;


typedef void (*mlme_api_cb_peer_status_ind_t)(mlme_peer_status_t  status,
                                              m80211_mac_addr_t * mac,
                                              m80211_mac_addr_t * bssid);

typedef struct
{
   mlme_api_cb_peer_status_ind_t peer_status_ind;
}mlme_asynch_cb_api_t;



/****************************************************************************/
/* Types for MAC PDUs                                                       */
/****************************************************************************/
typedef struct
{   
   uint64_t                                  timestamp; 
   uint16_t                                  mpilot_interval; 
   uint16_t                                  beacon_period; 
   uint16_t                                  capability_info;
   m80211_country_string_t                   country_string;
   m80211_max_regulatory_power_t             max_reg_power;
   m80211_max_tx_power_t                     max_tx_power;   
   m80211_tx_power_used_t                    tx_power_used;   
   m80211_trx_noise_floor_t                  trx_noise_floor;   
   m80211_ie_ds_par_set_t                    ds_parameter_set;   
}mac_mmpdu_mpilot_t;

typedef struct
{
   uint16_t                            algorithm_number;
   uint16_t                            sequence_number;
   m80211_mgmt_status_t                status_code;   
   m80211_ie_challenge_text_t          challenge_text; 
} mac_mmpdu_authenticate_t; 

typedef struct
{
   uint16_t reason;
} mac_mmpdu_deauthenticate_t;

typedef common_IEs_t           mac_mmpdu_probe_req_t;

typedef mac_mmpdu_beacon_ind_t mac_mmpdu_probe_rsp_t;

typedef struct
{   
   uint16_t                            capability_info;
   uint16_t                            listen_interval;
   common_IEs_t                        ie;
}mac_mmpdu_associate_req_t;

typedef struct
{   
   uint16_t                            capability_info;
   m80211_mgmt_status_t                status_code;
   uint16_t                            aid;
   common_IEs_t                        ie;
}mac_mmpdu_associate_rsp_t;

typedef struct
{   
   uint16_t                            capability_info;
   uint16_t                            listen_interval;
   m80211_mac_addr_t                   bssId; 
   common_IEs_t                        ie;
}mac_mmpdu_reassociate_req_t;

typedef mac_mmpdu_associate_rsp_t mac_mmpdu_reassociate_rsp_t;

typedef struct
{   
   uint16_t  reason_code;
}mac_mmpdu_disassociate_t;

typedef struct
{   
   m80211_radio_measurement_req_t      mreq_body;
}mac_mmpdu_radio_measurement_req_t;

typedef struct
{   
   m80211_radio_measurement_rep_t      mrep_body;
}mac_mmpdu_radio_measurement_rep_t;

typedef bool_t (*mlme_api_func_t)(m80211_mlme_mgmt_msg_t * input_par,
                                  m80211_mlme_mgmt_msg_t * output_par);

typedef union GENERATE_WRAPPER_FUNCTIONS(802.11_MAC)
{
   mac_mmpdu_beacon_ind_t              mac_mmpdu_beacon_ind;
   mac_mmpdu_authenticate_t            mac_mmpdu_authenticate;
   mac_mmpdu_deauthenticate_t          mac_mmpdu_deauthenticate;
   mac_mmpdu_probe_req_t               mac_mmpdu_mlme_probe_req;
   mac_mmpdu_probe_rsp_t               mac_mmpdu_probe_rsp;
   mac_mmpdu_associate_req_t           mac_mmpdu_associate_req;
   mac_mmpdu_associate_rsp_t           mac_mmpdu_associate_rsp;
   mac_mmpdu_reassociate_req_t         mac_mmpdu_reassociate_req;
   mac_mmpdu_reassociate_rsp_t         mac_mmpdu_reassociate_rsp;      
   mac_mmpdu_disassociate_t            mac_mmpdu_disassociate;
   mac_mmpdu_mpilot_t                  mac_mmpdu_mpilot;
   mac_mmpdu_radio_measurement_req_t   mac_mmpdu_radio_measurement_req;
   mac_mmpdu_radio_measurement_rep_t   mac_mmpdu_radio_measurement_rep;
} mac_mgmt_body_t;

typedef struct
{
   /*!
     This structure is a union of all UCOS messages
   */
   mac_mgmt_body_t   mac_mgmt_body;
}m80211_mac_mgmt_msg_t;

/***************************************************************************
 I N T E R F A C E  F U N C T I O N S
****************************************************************************/

#endif /* #ifndef MAC_MGMT_DEFS_H */
/* E N D  O F  F I L E *******************************************************/
