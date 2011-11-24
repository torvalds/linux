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
#ifndef MAC_API_MLME_H
#define MAC_API_MLME_H
#include "mac_api_defs.h"
#include "m80211_stddefs.h"

#ifdef USE_IE_HANDLER
#include "ie_handler.h"
#endif /* USE_IE_HANDLER */

/* E X P O R T E D  D E F I N E S ********************************************/

/******************************************************************************/
/* DRIVER TO MAC MESSAGE ID's                                                 */
/******************************************************************************/
#define MLME_RESET_REQ                    (0  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_SCAN_REQ                     (1  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_POWER_MGMT_REQ               (2  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_JOIN_REQ                     (3  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_AUTHENTICATE_REQ             (4  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_DEAUTHENTICATE_REQ           (5  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_ASSOCIATE_REQ                (6  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_REASSOCIATE_REQ              (7  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_DISASSOCIATE_REQ             (8  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_START_REQ                    (9  | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_SET_KEY_REQ                  (10 | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_DELETE_KEY_REQ               (11 | MAC_API_PRIMITIVE_TYPE_REQ)
#define MLME_SET_PROTECTION_REQ           (12 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_BSS_LEAVE_REQ            (13 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_IBSS_LEAVE_REQ           (14 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_SETSCANPARAM_REQ         (15 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_ADD_SCANFILTER_REQ       (16 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_REMOVE_SCANFILTER_REQ    (17 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_ADD_SCANJOB_REQ          (18 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_REMOVE_SCANJOB_REQ       (19 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_GET_SCANFILTER_REQ       (20 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_SET_SCANJOBSTATE_REQ     (21 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_SET_SCANCOUNTRYINFO_REQ  (22 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_WMM_PS_PERIOD_START_REQ  (23 | MAC_API_PRIMITIVE_TYPE_REQ)
#if (DE_CCX == CFG_INCLUDED)
#define NRP_MLME_ADDTS_REQ                (24 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_DELTS_REQ                (25 | MAC_API_PRIMITIVE_TYPE_REQ)
#define NRP_MLME_GET_FW_STATS_REQ         (26 | MAC_API_PRIMITIVE_TYPE_REQ)
#endif //DE_CCX

/******************************************************************************/
/* MAC TO DRIVER MESSAGE ID's                                                 */
/******************************************************************************/
#define MLME_RESET_CFM                    (0  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_SCAN_CFM                     (1  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_POWER_MGMT_CFM               (2  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_JOIN_CFM                     (3  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_AUTHENTICATE_CFM             (4  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_AUTHENTICATE_IND             (5  | MAC_API_PRIMITIVE_TYPE_IND)
#define MLME_DEAUTHENTICATE_CFM           (6  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_DEAUTHENTICATE_IND           (7  | MAC_API_PRIMITIVE_TYPE_IND)
#define MLME_ASSOCIATE_CFM                (8  | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_ASSOCIATE_IND                (9  | MAC_API_PRIMITIVE_TYPE_IND)
#define MLME_REASSOCIATE_CFM              (10 | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_REASSOCIATE_IND              (11 | MAC_API_PRIMITIVE_TYPE_IND)
#define MLME_DISASSOCIATE_CFM             (12 | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_DISASSOCIATE_IND             (13 | MAC_API_PRIMITIVE_TYPE_IND)
#define MLME_START_CFM                    (14 | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_SET_KEY_CFM                  (15 | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_DELETE_KEY_CFM               (16 | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_SET_PROTECTION_CFM           (17 | MAC_API_PRIMITIVE_TYPE_CFM)
#define MLME_MICHAEL_MIC_FAILURE_IND      (18 | MAC_API_PRIMITIVE_TYPE_IND)
#define MLME_SCAN_IND                     (19 | MAC_API_PRIMITIVE_TYPE_IND)
#define NRP_MLME_BSS_LEAVE_CFM            (20 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_IBSS_LEAVE_CFM           (21 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_PEER_STATUS_IND          (22 | MAC_API_PRIMITIVE_TYPE_IND)
#define NRP_MLME_SETSCANPARAM_CFM         (23 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_ADD_SCANFILTER_CFM       (24 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_REMOVE_SCANFILTER_CFM    (25 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_ADD_SCANJOB_CFM          (26 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_REMOVE_SCANJOB_CFM       (27 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_GET_SCANFILTER_CFM       (28 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_SET_SCANJOBSTATE_CFM     (29 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_SCANNOTIFICATION_IND     (30 | MAC_API_PRIMITIVE_TYPE_IND)
#define NRP_MLME_SET_SCANCOUNTRYINFO_CFM  (31 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_WMM_PS_PERIOD_START_CFM  (32 | MAC_API_PRIMITIVE_TYPE_CFM)
#if (DE_CCX == CFG_INCLUDED)
#define NRP_MLME_ADDTS_CFM                (33 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_ADDTS_IND                (34 | MAC_API_PRIMITIVE_TYPE_IND)
#define NRP_MLME_DELTS_CFM                (35 | MAC_API_PRIMITIVE_TYPE_CFM)
#define NRP_MLME_GET_FW_STATS_CFM         (36 | MAC_API_PRIMITIVE_TYPE_CFM)
#endif //DE_CCX

/******************************************************************************/
/* PARAMETER DEFINITIONS                                                      */
/******************************************************************************/
#define M80211_CHANNEL_LIST_MAX_LENGTH  14

/* Power management modes */
#define M80211_MGMT_STA_ACTIVE      0
#define M80211_MGMT_STA_POWER_SAVE  1
#define M80211_MGMT_STA_DEFAULT     0xFF


typedef int32_t mlme_rssi_dbm_t;

typedef int32_t mlme_snr_db_t;
#define SNR_UNKNOWN -128

typedef uint8_t mac_api_mlme_rc_t;
#define MAC_API_MLME_RC_OK     0
#define MAC_API_MLME_RC_FAILED 1


typedef uint8_t mlme_peer_status_t;
#define MLME_PEER_STATUS_CONNECTED                 0x00
#define MLME_PEER_STATUS_NOT_CONNECTED             0x01
#define MLME_PEER_STATUS_TX_FAILED                 0x02
#define MLME_PEER_STATUS_RX_BEACON_FAILED          0x03
#define MLME_PEER_STATUS_ROUNDTRIP_FAILED          0x04
#define MLME_PEER_STATUS_RESTARTED                 0x05
#define MLME_PEER_STATUS_INCOMPATIBLE              0x06
#define MLME_PEER_STATUS_TX_FAILED_WARNING         0x82
#define MLME_PEER_STATUS_RX_BEACON_FAILED_WARNING  0x83


typedef enum
{
    IBSS_PEER_PM,
    IBSS_PEER_ACTIVE
}ibss_ps_state;

/* enum's used by SCAN */
typedef enum
{
    SCAN_SUCCESS,
    SCAN_FAILURE,
    SCAN_BUSY
}scanresult_t;

typedef enum
{
    PASSIVE_SCAN,
    ACTIVE_SCAN,
    ANY_SCANTYPE
}scantype_t;

typedef enum
{
    IDLE_MODE=1,
    CONNECTED_MODE,
    ANY_MODE
}scanmode_t;

typedef enum
{
    LONG_PREAMBLE = 0,
    SHORT_PREAMBLE = 1
}preamble_t;

typedef enum
{
    ABSOLUTE_THRESHOLD=0,
    RELATIVE_THRESHOLD
}thresholdtype_t;


typedef uint16_t scan_notification_flags_t;  /* Bit encoded field. See below */
#define SCAN_NOTIFICATION_FLAG_FIRST_HIT           0x0001
#define SCAN_NOTIFICATION_FLAG_JOB_COMPLETE        0x0002
#define SCAN_NOTIFICATION_FLAG_BG_PERIOD_COMPLETE  0x0004
#define SCAN_NOTIFICATION_FLAG_HIT                 0x0008
#define SCAN_NOTIFICATION_FLAG_DIRECT_SCAN_JOB     0x0010


typedef struct
{
    uint32_t                  probe_delay;
    uint32_t                  discon_scan_period;
    uint32_t                  conn_scan_period;
    uint16_t                  rate;
    uint16_t                  probes_per_ch;
    scan_notification_flags_t notification_policy;
    uint16_t                  pa_min_ch_time;
    uint16_t                  pa_max_ch_time;
    uint16_t                  ac_min_ch_time;
    uint16_t                  ac_max_ch_time;
    uint16_t                  as_min_ch_time;
    uint16_t                  as_max_ch_time;
    uint8_t                   preamble;
    uint8_t                   deliv_pol;
    uint32_t                  max_disconnect_period;    /* <>0 scanperiod doubles up to max. for disconnected */
    uint32_t                  max_connect_period;    /* <>0 scanperiod doubles up to max. for connected */
    uint8_t                   period_repetition;  /* setup of how many repetitions on each period */
}m80211_nrp_mlme_scan_config_t;

#ifndef USE_IE_HANDLER
typedef struct
{
    m80211_ie_ssid_t                    ssid;
    m80211_ie_supported_rates_t         supported_rate_set;
    m80211_ie_ext_supported_rates_t     ext_supported_rate_set;
    m80211_ie_request_info_t            req_info_set;
    m80211_ie_erp_t                     erp_info_set;
    m80211_ie_tim_t                     tim_parameter_set;
    m80211_ie_ds_par_set_t              ds_parameter_set;
    m80211_ie_cf_par_set_t              cf_parameter_set;
    m80211_ie_fh_par_set_t              fh_parameter_set;
    m80211_ie_ibss_par_set_t            ibss_parameter_set;
    m80211_ie_country_t                 country_info_set;
    m80211_ie_WMM_parameter_element_t   wmm_parameter_element;
    m80211_ie_WMM_information_element_t wmm_information_element;
    m80211_ie_qos_capability_t          qos_capability;
    m80211_ie_wpa_parameter_set_t       wpa_parameter_set;
    m80211_ie_wps_parameter_set_t       wps_parameter_set;
    m80211_ie_rsn_parameter_set_t       rsn_parameter_set;
    m80211_ie_wapi_parameter_set_t      wapi_parameter_set;
#if (DE_CCX == CFG_INCLUDED)
    m80211_ie_qbss_load_t               qbss_parameter_set;
    m80211_ie_ccx_parameter_set_t       ccx_parameter_set;
    m80211_ie_ccx_rm_parameter_set_t    ccx_rm_parameter_set;
    m80211_ie_ccx_cpl_parameter_set_t   ccx_cpl_parameter_set;
    m80211_ie_ccx_tsm_parameter_set_t   ccx_tsm_parameter_set;
    m80211_wmm_tspec_ie_t               wmm_tspec_parameter_set;
    m80211_ie_ccx_adj_parameter_set_t   ccx_adj_parameter_set;
    m80211_ie_ccx_reassoc_req_parameter_set_t   ccx_reassoc_req_parameter_set;
    m80211_ie_ccx_reassoc_rsp_parameter_set_t   ccx_reassoc_rsp_parameter_set;
#endif //DE_CCX
    m80211_ie_ht_capabilities_t         ht_capabilities;
    m80211_ie_ht_operation_t            ht_operation;
    m80211_remaining_IEs_t              remaining_sets;
} common_IEs_t;
#define COMMON_IES(_ie)   common_IEs_t _ie;
#else
#define COMMON_IES(_ie)
#endif

typedef struct
{
#if (DE_CCX == CFG_INCLUDED)
    uint32_t   driverenv_timestamp;
#endif //DE_CCX
    uint64_t   timestamp;
    uint16_t   beacon_period;
    uint16_t   capability_info;
    COMMON_IES(ie)
} mac_mmpdu_beacon_ind_t;
#define MAC_MMPDU_BEACON_IND_FIXED_PART   12

typedef mac_mmpdu_beacon_ind_t m80211_bss_description_t;

typedef struct
{
    uint8_t no_channels;
    uint8_t channelList[M80211_CHANNEL_LIST_MAX_LENGTH];
    uint8_t reserved;
}channel_list_t;

typedef struct
{
    m80211_mac_addr_t          bssId;
    uint8_t                    bssType;
    uint8_t                    dtim_period;
    uint64_t                   local_timestamp;
    mlme_rssi_dbm_t            rssi_info;
    mlme_snr_db_t              snr_info;
    uint32_t                   size_of_packed_bss_description;
#ifdef USE_IE_HANDLER
    /* Must be kept 32bit aligned. */
    mac_mmpdu_beacon_ind_t     bss_description;
#else
    m80211_bss_description_t*  bss_description_p;
#endif /* USE_IE_HANDLER */
}mlme_bss_description_t;

typedef struct
{
    uint8_t octet[16]; /* M80211_WPI_PN_SIZE, WPA uses first 8 */
} receive_seq_cnt_t;

typedef struct
{
    m80211_key_t                   key;
    uint16_t                       key_len;   /* Length in number of bits */
    uint8_t                        key_id;
    m80211_key_type_t              key_type;
    m80211_mac_addr_t              mac_addr;
    receive_seq_cnt_t              receive_seq_cnt;
    uint8_t                        config_by_authenticator; /* boolean type */
    m80211_cipher_suite_t          cipher_suite;
}m80211_set_key_descriptor_t;

typedef struct
{
    uint8_t               key_id;
    m80211_key_type_t     key_type;
    m80211_mac_addr_t     mac_addr;
}m80211_delete_key_descriptor_t;

typedef struct
{
    m80211_mac_addr_t     mac_addr;
    m80211_protect_type_t protect_type;
    m80211_key_type_t     key_type;
}m80211_protect_list_element_t;


typedef struct
{
    uint8_t               count;
    m80211_mac_addr_t     mac_addr;
    m80211_key_type_t     key_type;
    uint8_t               key_id;
    uint8_t               tsc[6];
    uint8_t               reserved;
}m80211_michael_mic_failure_ind_descriptor_t;

#if 0
typedef struct
{
    uint8_t              result;
    uint8_t              nmb_reponses;
    uint8_t              reserved[2];
}mlme_scan_ind_head_t;
#endif



/******************************************************************************/
/* TEMPLATE PRIMITIVES                                                         */
/******************************************************************************/
typedef struct
{
    mac_api_transid_t trans_id;
    uint16_t          aid;
    uint8_t           result;
    uint8_t           reserved;
    COMMON_IES(ie)
}m80211_mlme_association_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint8_t           result;
    uint8_t           reserved[3];
} m80211_mlme_status_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t          par;
}m80211_mlme_parameter_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t peer_sta;
    uint16_t          value;
}m80211_mlme_addr_and_short_ind_t;




/******************************************************************************/
/* DRIVER TO MAC PRIMITIVES                                                   */
/******************************************************************************/
typedef struct
{
    mac_api_transid_t trans_id;
    uint16_t          power_mgmt_mode;
    uint8_t           use_ps_poll;     /* 1 or 0 */
    uint8_t           receive_all_dtim; /* 1 or 0 */
}m80211_mlme_power_mgmt_req_t;

typedef struct
{
    mac_api_transid_t        trans_id;
    uint16_t                 join_timeout;
    uint16_t                 probe_delay;
    uint32_t                 operational_rate_mask;
    uint32_t                 basic_rate_mask;
    /* Must be 32bit aligned. */
    mlme_bss_description_t   bss;
}m80211_mlme_join_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint16_t          type;
    uint16_t          timeout;
    m80211_mac_addr_t peer_sta;
    uint8_t           reserved[2];
}m80211_mlme_authenticate_req_t;

typedef m80211_mlme_addr_and_short_ind_t m80211_mlme_deauthenticate_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint16_t          timeout;
    uint16_t          capability_info;
    uint16_t          listen_interval;
    m80211_mac_addr_t peer_sta;
    COMMON_IES(ie)
}m80211_mlme_associate_req_t;

typedef m80211_mlme_associate_req_t m80211_mlme_reassociate_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t peer_sta;
    uint8_t           reason;
    uint8_t           reserved;
}m80211_mlme_disassociate_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t bssId;
    uint8_t           set_default_mib; /* 1 or 0 */
    uint8_t           reserved;
}m80211_mlme_reset_req_t;

typedef struct
{
    mac_api_transid_t    trans_id;
    uint16_t             beacon_period;
    uint16_t             probe_delay;
    uint16_t             capability_info; /* QoS bit is Zero for WMM! */
    m80211_mac_addr_t    bssid;
    uint8_t              bss_type;
    uint8_t              dtim_period;
    uint8_t              reserved[2];
    uint32_t             supported_rate_mask;
    uint32_t             basic_rate_mask;
    COMMON_IES(ie)
}m80211_mlme_start_req_t;

typedef struct
{
    mac_api_transid_t           trans_id;
    m80211_set_key_descriptor_t set_key_descriptor;
}m80211_mlme_set_key_req_t;

typedef struct
{
    mac_api_transid_t              trans_id;
    m80211_delete_key_descriptor_t delete_key_descriptor;
}m80211_mlme_delete_key_req_t;

typedef struct
{
    mac_api_transid_t             trans_id;
    m80211_protect_list_element_t protect_list_element;
}m80211_mlme_set_protection_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t bssId;
    uint8_t           reserved[2];
}m80211_nrp_mlme_bss_leave_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t bssId;
    uint8_t           reserved[2];
}m80211_nrp_mlme_ibss_leave_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t          job_id;
    uint16_t          channel_interval;    /* kTU's */
    uint16_t          use_default_params;
    uint32_t          probe_delay;         /* kTU's */
    uint16_t          min_ch_time;         /* kTU's */
    uint16_t          max_ch_time;         /* kTU's */
}mlme_direct_scan_req_t;

typedef struct
{
    mac_api_transid_t             trans_id;
    m80211_nrp_mlme_scan_config_t config;
}m80211_nrp_mlme_set_scanparam_req_t;

#if 0
typedef struct
{
    mac_api_transid_t trans_id;
    /* this is expanded m80211_nrp_mlme_scan_config_t */
    uint32_t          probe_delay;
    uint32_t          discon_scan_period;
    uint32_t          conn_scan_period;
    uint16_t          rate;
    uint16_t          probes_per_ch;
    uint16_t          notification_pol;
    uint16_t          pa_min_ch_time;
    uint16_t          pa_max_ch_time;
    uint16_t          ac_min_ch_time;
    uint16_t          ac_max_ch_time;
    uint16_t          as_min_ch_time;
    uint16_t          as_max_ch_time;
    uint8_t           preamble;
    uint8_t           deliv_pol;
    uint32_t          max_disconnect_period;
    uint32_t          max_connect_period;
    uint8_t           period_repetition;
}m80211_nrp_mlme_scanparam_t;
#endif

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t filter_id;
    int32_t  rssi_threshold;
    uint32_t snr_threshold;
    uint16_t threshold_type;
    uint8_t  bss_type;
    uint8_t  reserved[3];
}m80211_nrp_mlme_add_scanfilter_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t filter_id;
}m80211_nrp_mlme_remove_scanfilter_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t job_id;
    uint32_t filter_id;
    uint16_t channels;
    uint8_t  prio;
    uint8_t  as_exclude;
    uint8_t  scan_type;
    uint8_t  scan_mode;
    uint8_t  run_every_nth_period;   /* 1=every, 2=every other, 3=every third ... */
    char     bssid[M802_ADDRESS_SIZE];
    uint8_t  ssid_id;
    uint8_t  ssid_len;
    char     ssid[M80211_IE_MAX_LENGTH_SSID];
}m80211_nrp_mlme_add_scanjob_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t job_id;
}m80211_nrp_mlme_remove_scanjob_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t filter_id;
}m80211_nrp_mlme_get_scanfilter_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t job_id;
    uint8_t  state;
    uint8_t  reserved[3];
}m80211_nrp_mlme_set_scanjobstate_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t  action;
    uint32_t  ssid_length;
    char     ssid[M80211_IE_MAX_LENGTH_SSID];
}m80211_nrp_mlme_scan_adm_ssid_pool_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t  action;
    uint32_t job_id;
    uint32_t  ssid_length;
    char     ssid[M80211_IE_MAX_LENGTH_SSID];
}m80211_nrp_mlme_scan_adm_job_ssid_req_t;

typedef struct
{
    mac_api_transid_t        trans_id;
#ifndef USE_IE_HANDLER
    m80211_ie_country_t      country_info_set;
#endif
}m80211_nrp_mlme_set_scancountryinfo_req_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint8_t           activity; /* 1--START, 0--STOP*/
    uint8_t           reserved[3];
}m80211_nrp_mlme_wmm_ps_period_start_req_t;


/******************************************************************************/
/* MAC TO DRIVER PRIMITIVES                                                   */
/******************************************************************************/

typedef m80211_mlme_status_cfm_t m80211_mlme_scan_cfm_t;

typedef struct
{
    mac_api_transid_t      trans_id;
    mlme_bss_description_t scan_ind_body;
}m80211_mlme_scan_ind_t;

typedef m80211_mlme_status_cfm_t m80211_mlme_power_mgmt_cfm_t;

typedef m80211_mlme_status_cfm_t m80211_mlme_join_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t peer_sta;
    uint16_t          type;
    uint16_t          result;
    uint8_t           reserved[2];
}m80211_mlme_authenticate_cfm_t;


typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t peer_sta;
    uint16_t          type;
}m80211_mlme_authenticate_ind_t;

typedef m80211_mlme_addr_and_short_ind_t m80211_mlme_deauthenticate_cfm_t;

typedef m80211_mlme_addr_and_short_ind_t m80211_mlme_deauthenticate_ind_t;

typedef m80211_mlme_association_cfm_t  m80211_mlme_associate_cfm_t;

typedef struct
{
    mac_api_transid_t    trans_id;
    m80211_mac_addr_t    peer_sta;
    uint8_t              reserved[2];
    COMMON_IES(ie)
}m80211_mlme_associate_ind_t;

typedef m80211_mlme_association_cfm_t  m80211_mlme_reassociate_cfm_t;

typedef struct
{
    mac_api_transid_t    trans_id;
    m80211_mac_addr_t    peer_sta;
    uint8_t              reserved[2];
    COMMON_IES(ie)
}m80211_mlme_reassociate_ind_t;

typedef m80211_mlme_status_cfm_t m80211_mlme_disassociate_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    m80211_mac_addr_t bssId;
    uint8_t           reason;
    uint8_t           reserved;
}m80211_mlme_disassociate_ind_t;

typedef m80211_mlme_status_cfm_t m80211_mlme_reset_cfm_t;

typedef m80211_mlme_status_cfm_t m80211_mlme_start_cfm_t;

typedef m80211_mlme_parameter_cfm_t m80211_mlme_set_key_cfm_t;

typedef m80211_mlme_parameter_cfm_t m80211_mlme_delete_key_cfm_t;

typedef m80211_mlme_parameter_cfm_t m80211_mlme_set_protection_cfm_t;

typedef struct
{
    mac_api_transid_t                           trans_id;
    m80211_michael_mic_failure_ind_descriptor_t michael_mic_failure_ind_descriptor;
}m80211_mlme_michael_mic_failure_ind_t;

typedef m80211_mlme_status_cfm_t m80211_nrp_mlme_bss_leave_cfm_t;

typedef m80211_mlme_status_cfm_t m80211_nrp_mlme_ibss_leave_cfm_t;

typedef struct
{
    mac_api_transid_t       trans_id;
    mlme_peer_status_t      status;
    m80211_mac_addr_t       peer_mac;
    m80211_mac_addr_t       bssid;
    uint8_t                 reserved;
}m80211_nrp_mlme_peer_status_ind_t;

typedef struct
{
    uint32_t trans_id;
    uint8_t  result;
    uint8_t  reserved[3];
}mlme_direct_scan_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t result;
}m80211_nrp_mlme_set_scanparam_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t filter_id;
    uint32_t result;
}m80211_nrp_mlme_add_scanfilter_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t filter_id;
    uint32_t result;
}m80211_nrp_mlme_remove_scanfilter_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t job_id;
    uint32_t result;
}m80211_nrp_mlme_add_scanjob_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t job_id;
    uint32_t result;
}m80211_nrp_mlme_remove_scanjob_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t filter_id;
    uint32_t result;
    int32_t rssi_threshold;
    uint32_t snr_threshold;
    uint16_t threshold_type;
    uint8_t  bss_type;
    uint8_t  reserved[3];
}m80211_nrp_mlme_get_scanfilter_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t job_id;
    uint32_t result;
}m80211_nrp_mlme_set_scanjobstate_cfm_t;

typedef struct
{
    mac_api_transid_t         trans_id;
    uint32_t                  job_id;
    scan_notification_flags_t flags;
    uint8_t                   filler[2];
}m80211_nrp_mlme_scannotification_ind_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t result;
}m80211_nrp_mlme_scan_adm_ssid_pool_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t result;
}m80211_nrp_mlme_scan_adm_job_ssid_cfm_t;

typedef struct
{
    mac_api_transid_t trans_id;
    char     bssid[M802_ADDRESS_SIZE];
}m80211_nrp_mlme_scan_bssid_remove_ind_t;

typedef struct
{
    mac_api_transid_t trans_id;
    uint32_t result;
}m80211_nrp_mlme_set_scancountryinfo_cfm_t;

typedef struct
{
    uint32_t trans_id;
    uint32_t  peer_ps_state;
}m80211_nrp_mlme_ibss_peer_ps_ind_t;

typedef struct
{
    mac_api_transid_t trans_id;
    mac_api_mlme_rc_t result;
    uint8_t           filler[3];
}m80211_nrp_mlme_wmm_ps_period_start_cfm_t;




/**********************************************************************************/
/*************************** END OF MESSAGE DEFINITIONS ***************************/
/**********************************************************************************/

typedef union
{
    mac_api_transid_t                         transid;
    mlme_direct_scan_req_t                    mlme_direct_scan_req;
    mlme_direct_scan_cfm_t                    mlme_direct_scan_cfm;
    m80211_mlme_scan_ind_t                    mlme_scan_ind;
    m80211_mlme_power_mgmt_req_t              mlme_power_mgmt_req;
    m80211_mlme_power_mgmt_cfm_t              mlme_power_mgmt_cfm;
    m80211_mlme_join_req_t                    mlme_join_req;
    m80211_mlme_join_cfm_t                    mlme_join_cfm;
    m80211_mlme_authenticate_req_t            mlme_authenticate_req;
    m80211_mlme_authenticate_cfm_t            mlme_authenticate_cfm;
    m80211_mlme_authenticate_ind_t            mlme_authenticate_ind;
    m80211_mlme_deauthenticate_req_t          mlme_deauthenticate_req;
    m80211_mlme_deauthenticate_cfm_t          mlme_deauthenticate_cfm;
    m80211_mlme_deauthenticate_ind_t          mlme_deauthenticate_ind;
    m80211_mlme_associate_req_t               mlme_associate_req;
    m80211_mlme_associate_cfm_t               mlme_associate_cfm;
    m80211_mlme_associate_ind_t               mlme_associate_ind;
    m80211_mlme_reassociate_req_t             mlme_reassociate_req;
    m80211_mlme_reassociate_cfm_t             mlme_reassociate_cfm;
    m80211_mlme_reassociate_ind_t             mlme_reassociate_ind;
    m80211_mlme_disassociate_req_t            mlme_disassociate_req;
    m80211_mlme_disassociate_cfm_t            mlme_disassociate_cfm;
    m80211_mlme_disassociate_ind_t            mlme_disassociate_ind;
    m80211_mlme_reset_req_t                   mlme_reset_req;
    m80211_mlme_reset_cfm_t                   mlme_reset_cfm;
    m80211_mlme_start_req_t                   mlme_start_req;
    m80211_mlme_start_cfm_t                   mlme_start_cfm;
    m80211_mlme_set_key_req_t                 mlme_set_key_req;
    m80211_mlme_set_key_cfm_t                 mlme_set_key_cfm;
    m80211_mlme_delete_key_req_t              mlme_delete_key_req;
    m80211_mlme_delete_key_cfm_t              mlme_delete_key_cfm;
    m80211_mlme_set_protection_req_t          mlme_set_protection_req;
    m80211_mlme_set_protection_cfm_t          mlme_set_protection_cfm;
    m80211_mlme_michael_mic_failure_ind_t     mlme_michael_mic_failure_ind;

    /* Nanoradio propriary messages */
    m80211_nrp_mlme_bss_leave_req_t           nrp_mlme_bss_leave_req;
    m80211_nrp_mlme_bss_leave_cfm_t           nrp_mlme_bss_leave_cfm;
    m80211_nrp_mlme_ibss_leave_req_t          nrp_mlme_ibss_leave_req;
    m80211_nrp_mlme_ibss_leave_cfm_t          nrp_mlme_ibss_leave_cfm;
    m80211_nrp_mlme_peer_status_ind_t         nrp_mlme_peer_status_ind;
    m80211_nrp_mlme_add_scanfilter_req_t      add_scanfilter_req;
    m80211_nrp_mlme_add_scanfilter_cfm_t      add_scanfilter_cfm;
    m80211_nrp_mlme_remove_scanfilter_req_t   remove_scanfilter_req;
    m80211_nrp_mlme_remove_scanfilter_cfm_t   remove_scanfilter_cfm;
    m80211_nrp_mlme_set_scanparam_req_t       set_scanparam_req;
    m80211_nrp_mlme_set_scanparam_cfm_t       set_scanparam_cfm;
    m80211_nrp_mlme_add_scanjob_req_t         add_scanjob_req;
    m80211_nrp_mlme_add_scanjob_cfm_t         add_scanjob_cfm;
    m80211_nrp_mlme_remove_scanjob_req_t      remove_scanjob_req;
    m80211_nrp_mlme_remove_scanjob_cfm_t      remove_scanjob_cfm;
    m80211_nrp_mlme_get_scanfilter_req_t      get_scanfilter_req;
    m80211_nrp_mlme_get_scanfilter_cfm_t      get_scanfilter_cfm;
    m80211_nrp_mlme_set_scanjobstate_req_t    set_scanjobstate_req;
    m80211_nrp_mlme_set_scanjobstate_cfm_t    set_scanjobstate_cfm;
    m80211_nrp_mlme_scannotification_ind_t    scannotification_ind;
    m80211_nrp_mlme_set_scancountryinfo_req_t set_scancountryinfo_req;
    m80211_nrp_mlme_set_scancountryinfo_cfm_t set_scancountryinfo_cfm;
    m80211_nrp_mlme_scan_adm_ssid_pool_req_t  scan_adm_ssid_pool_req;
    m80211_nrp_mlme_scan_adm_ssid_pool_cfm_t  scan_adm_ssid_pool_cfm;
    m80211_nrp_mlme_scan_adm_job_ssid_req_t   scan_adm_job_ssid_req;
    m80211_nrp_mlme_scan_adm_job_ssid_cfm_t   scan_adm_job_ssid_cfm;
    m80211_nrp_mlme_scan_bssid_remove_ind_t   scan_bssid_remove_ind;
    m80211_nrp_mlme_wmm_ps_period_start_cfm_t wmm_ps_period_start_cfm;
    m80211_nrp_mlme_wmm_ps_period_start_req_t wmm_ps_period_start_req;
#if (DE_CCX == CFG_INCLUDED)
    m80211_nrp_mlme_addts_req_t               addts_req;
    m80211_nrp_mlme_addts_cfm_t               addts_cfm;
#endif //DE_CCX
} mlme_mgmt_body_t;


typedef mlme_mgmt_body_t m80211_mlme_mgmt_msg_t;

/* G L O B A L  V A R I A B L E S ********************************************/

/* I N T E R F A C E  F U N C T I O N S **************************************/
#endif    /* MAC_API_MLME_H */
/* END OF FILE ***************************************************************/
