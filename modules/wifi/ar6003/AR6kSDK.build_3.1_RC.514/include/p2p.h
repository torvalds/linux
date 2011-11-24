//------------------------------------------------------------------------------
// <copyright file="p2p.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// This file has the shared declarations between the host & target P2P modules.
// Author(s): ="Atheros"
//==============================================================================
#ifndef _P2P_H_
#define _P2P_H_

#define WPS_NONCE_LEN 16
#define WPS_DEV_TYPE_LEN 8
#define WPS_AUTHENTICATOR_LEN 8
#define WPS_HASH_LEN 32
#define WPS_SECRET_NONCE_LEN 16
#define WPS_KWA_LEN 8
#define WPS_MAX_PIN_LEN 8
#define P2P_MAX_SSID_LEN 32

#define WPS_DEV_OUI_WFA 0x0050f204
#define P2P_IE_VENDOR_TYPE 0x506f9a09

#define WLAN_EID_VENDOR_SPECIFIC 221

#define P2P_DEV_CAPAB_SERVICE_DISCOVERY BIT(0)

/* Config Methods */
#define WPS_CONFIG_USBA 0x0001
#define WPS_CONFIG_ETHERNET 0x0002
#define WPS_CONFIG_LABEL 0x0004
#define WPS_CONFIG_DISPLAY 0x0008
#define WPS_CONFIG_EXT_NFC_TOKEN 0x0010
#define WPS_CONFIG_INT_NFC_TOKEN 0x0020
#define WPS_CONFIG_NFC_INTERFACE 0x0040
#define WPS_CONFIG_PUSHBUTTON 0x0080
#define WPS_CONFIG_KEYPAD 0x0100

/* Attribute Types */
enum wps_attribute {
    ATTR_AP_CHANNEL = 0x1001,
    ATTR_ASSOC_STATE = 0x1002,
    ATTR_AUTH_TYPE = 0x1003,
    ATTR_AUTH_TYPE_FLAGS = 0x1004,
    ATTR_AUTHENTICATOR = 0x1005,
    ATTR_CONFIG_METHODS = 0x1008,
    ATTR_CONFIG_ERROR = 0x1009,
    ATTR_CONFIRM_URL4 = 0x100a,
    ATTR_CONFIRM_URL6 = 0x100b,
    ATTR_CONN_TYPE = 0x100c,
    ATTR_CONN_TYPE_FLAGS = 0x100d,
    ATTR_CRED = 0x100e,
    ATTR_ENCR_TYPE = 0x100f,
    ATTR_ENCR_TYPE_FLAGS = 0x1010,
    ATTR_DEV_NAME = 0x1011,
    ATTR_DEV_PASSWORD_ID = 0x1012,
    ATTR_E_HASH1 = 0x1014,
    ATTR_E_HASH2 = 0x1015,
    ATTR_E_SNONCE1 = 0x1016,
    ATTR_E_SNONCE2 = 0x1017,
    ATTR_ENCR_SETTINGS = 0x1018,
    ATTR_ENROLLEE_NONCE = 0x101a,
    ATTR_FEATURE_ID = 0x101b,
    ATTR_IDENTITY = 0x101c,
    ATTR_IDENTITY_PROOF = 0x101d,
    ATTR_KEY_WRAP_AUTH = 0x101e,
    ATTR_KEY_ID = 0x101f,
    ATTR_MAC_ADDR = 0x1020,
    ATTR_MANUFACTURER = 0x1021,
    ATTR_MSG_TYPE = 0x1022,
    ATTR_MODEL_NAME = 0x1023,
    ATTR_MODEL_NUMBER = 0x1024,
    ATTR_NETWORK_INDEX = 0x1026,
    ATTR_NETWORK_KEY = 0x1027,
    ATTR_NETWORK_KEY_INDEX = 0x1028,
    ATTR_NEW_DEVICE_NAME = 0x1029,
    ATTR_NEW_PASSWORD = 0x102a,
    ATTR_OOB_DEVICE_PASSWORD = 0x102c,
    ATTR_OS_VERSION = 0x102d,
    ATTR_POWER_LEVEL = 0x102f,
    ATTR_PSK_CURRENT = 0x1030,
    ATTR_PSK_MAX = 0x1031,
    ATTR_PUBLIC_KEY = 0x1032,
    ATTR_RADIO_ENABLE = 0x1033,
    ATTR_REBOOT = 0x1034,
    ATTR_REGISTRAR_CURRENT = 0x1035,
    ATTR_REGISTRAR_ESTABLISHED = 0x1036,
    ATTR_REGISTRAR_LIST = 0x1037,
    ATTR_REGISTRAR_MAX = 0x1038,
    ATTR_REGISTRAR_NONCE = 0x1039,
    ATTR_REQUEST_TYPE = 0x103a,
    ATTR_RESPONSE_TYPE = 0x103b,
    ATTR_RF_BANDS = 0x103c,
    ATTR_R_HASH1 = 0x103d,
    ATTR_R_HASH2 = 0x103e,
    ATTR_R_SNONCE1 = 0x103f,
    ATTR_R_SNONCE2 = 0x1040,
    ATTR_SELECTED_REGISTRAR = 0x1041,
    ATTR_SERIAL_NUMBER = 0x1042,
    ATTR_WPS_STATE = 0x1044,
    ATTR_SSID = 0x1045,
    ATTR_TOTAL_NETWORKS = 0x1046,
    ATTR_UUID_E = 0x1047,
    ATTR_UUID_R = 0x1048,
    ATTR_VENDOR_EXT = 0x1049,
    ATTR_VERSION = 0x104a,
    ATTR_X509_CERT_REQ = 0x104b,
    ATTR_X509_CERT = 0x104c,
    ATTR_EAP_IDENTITY = 0x104d,
    ATTR_MSG_COUNTER = 0x104e,
    ATTR_PUBKEY_HASH = 0x104f,
    ATTR_REKEY_KEY = 0x1050,
    ATTR_KEY_LIFETIME = 0x1051,
    ATTR_PERMITTED_CFG_METHODS = 0x1052,
    ATTR_SELECTED_REGISTRAR_CONFIG_METHODS = 0x1053,
    ATTR_PRIMARY_DEV_TYPE = 0x1054,
    ATTR_SECONDARY_DEV_TYP_ELIST = 0x1055,
    ATTR_PORTABLE_DEV = 0x1056,
    ATTR_AP_SETUP_LOCKED = 0x1057,
    ATTR_APPLICATION_EXT = 0x1058,
    ATTR_EAP_TYPE = 0x1059,
    ATTR_IV = 0x1060,
    ATTR_KEY_PROVIDED_AUTO = 0x1061,
    ATTR_802_1X_ENABLED = 0x1062,
    ATTR_APPSESSIONKEY = 0x1063,
    ATTR_WEPTRANSMITKEY = 0x1064,
    ATTR_REQUESTED_DEV_TYPE = 0x106a
};

enum p2p_wps_method {
    WPS_NOT_READY, WPS_PIN_LABEL, WPS_PIN_DISPLAY, WPS_PIN_KEYPAD, WPS_PBC
};

enum p2p_status_code {
    P2P_SC_SUCCESS = 0,
    P2P_SC_FAIL_INFO_CURRENTLY_UNAVAILABLE = 1,
    P2P_SC_FAIL_INCOMPATIBLE_PARAMS = 2,
    P2P_SC_FAIL_LIMIT_REACHED = 3,
    P2P_SC_FAIL_INVALID_PARAMS = 4,
    P2P_SC_FAIL_UNABLE_TO_ACCOMMODATE = 5,
    P2P_SC_FAIL_PREV_PROTOCOL_ERROR = 6,
    P2P_SC_FAIL_NO_COMMON_CHANNELS = 7,
    P2P_SC_FAIL_UNKNOWN_GROUP = 8,
    P2P_SC_FAIL_BOTH_GO_INTENT_15 = 9,
    P2P_SC_FAIL_INCOMPATIBLE_PROV_METHOD = 10,
    P2P_SC_FAIL_REJECTED_BY_USER = 11,
};

struct wps_parse_attr {
    /* fixed length fields */
    const A_UINT8 *version; /* 1 octet */
    const A_UINT8 *msg_type; /* 1 octet */
    const A_UINT8 *enrollee_nonce; /* WPS_NONCE_LEN (16) octets */
    const A_UINT8 *registrar_nonce; /* WPS_NONCE_LEN (16) octets */
    const A_UINT8 *uuid_r; /* WPS_UUID_LEN (16) octets */
    const A_UINT8 *uuid_e; /* WPS_UUID_LEN (16) octets */
    const A_UINT8 *auth_type_flags; /* 2 octets */
    const A_UINT8 *encr_type_flags; /* 2 octets */
    const A_UINT8 *conn_type_flags; /* 1 octet */
    const A_UINT8 *config_methods; /* 2 octets */
    const A_UINT8 *sel_reg_config_methods; /* 2 octets */
    const A_UINT8 *primary_dev_type; /* 8 octets */
    const A_UINT8 *rf_bands; /* 1 octet */
    const A_UINT8 *assoc_state; /* 2 octets */
    const A_UINT8 *config_error; /* 2 octets */
    const A_UINT8 *dev_password_id; /* 2 octets */
    const A_UINT8 *oob_dev_password; /* WPS_OOB_DEVICE_PASSWORD_ATTR_LEN (54)
                     * octets */
    const A_UINT8 *os_version; /* 4 octets */
    const A_UINT8 *wps_state; /* 1 octet */
    const A_UINT8 *authenticator; /* WPS_AUTHENTICATOR_LEN (8) octets */
    const A_UINT8 *r_hash1; /* WPS_HASH_LEN (32) octets */
    const A_UINT8 *r_hash2; /* WPS_HASH_LEN (32) octets */
    const A_UINT8 *e_hash1; /* WPS_HASH_LEN (32) octets */
    const A_UINT8 *e_hash2; /* WPS_HASH_LEN (32) octets */
    const A_UINT8 *r_snonce1; /* WPS_SECRET_NONCE_LEN (16) octets */
    const A_UINT8 *r_snonce2; /* WPS_SECRET_NONCE_LEN (16) octets */
    const A_UINT8 *e_snonce1; /* WPS_SECRET_NONCE_LEN (16) octets */
    const A_UINT8 *e_snonce2; /* WPS_SECRET_NONCE_LEN (16) octets */
    const A_UINT8 *key_wrap_auth; /* WPS_KWA_LEN (8) octets */
    const A_UINT8 *auth_type; /* 2 octets */
    const A_UINT8 *encr_type; /* 2 octets */
    const A_UINT8 *network_idx; /* 1 octet */
    const A_UINT8 *network_key_idx; /* 1 octet */
    const A_UINT8 *mac_addr; /* ETH_ALEN (6) octets */
    const A_UINT8 *key_prov_auto; /* 1 octet (Bool) */
    const A_UINT8 *dot1x_enabled; /* 1 octet (Bool) */
    const A_UINT8 *selected_registrar; /* 1 octet (Bool) */
    const A_UINT8 *request_type; /* 1 octet */
    const A_UINT8 *response_type; /* 1 octet */
    const A_UINT8 *ap_setup_locked; /* 1 octet */

    /* variable length fields */
    const A_UINT8 *manufacturer;
    A_UINT32 manufacturer_len;
    const A_UINT8 *model_name;
    A_UINT32 model_name_len;
    const A_UINT8 *model_number;
    A_UINT32 model_number_len;
    const A_UINT8 *serial_number;
    A_UINT32 serial_number_len;
    const A_UINT8 *dev_name;
    A_UINT32 dev_name_len;
    const A_UINT8 *public_key;
    A_UINT32 public_key_len;
    const A_UINT8 *encr_settings;
    A_UINT32 encr_settings_len;
    const A_UINT8 *ssid; /* <= 32 octets */
    A_UINT32 ssid_len;
    const A_UINT8 *network_key; /* <= 64 octets */
    A_UINT32 network_key_len;
    const A_UINT8 *eap_type; /* <= 8 octets */
    A_UINT32 eap_type_len;
    const A_UINT8 *eap_identity; /* <= 64 octets */
    A_UINT32 eap_identity_len;

    /* attributes that can occur multiple times */
#define MAX_CRED_COUNT 10
    const A_UINT8 *cred[MAX_CRED_COUNT];
    A_UINT32 cred_len[MAX_CRED_COUNT];
    A_UINT32 num_cred;

#define MAX_REQ_DEV_TYPE_COUNT 10
    const A_UINT8 *req_dev_type[MAX_REQ_DEV_TYPE_COUNT];
    A_UINT32 num_req_dev_type;
};


enum p2p_sublem_id {
    P2P_ATTR_STATUS = 0,
    P2P_ATTR_MINOR_REASON_CODE = 1,
    P2P_ATTR_CAPABILITY = 2,
    P2P_ATTR_DEVICE_ID = 3,
    P2P_ATTR_GROUP_OWNER_INTENT = 4,
    P2P_ATTR_CONFIGURATION_TIMEOUT = 5,
    P2P_ATTR_LISTEN_CHANNEL = 6,
    P2P_ATTR_GROUP_BSSID = 7,
    P2P_ATTR_EXT_LISTEN_TIMING = 8,
    P2P_ATTR_INTENDED_INTERFACE_ADDR = 9,
    P2P_ATTR_MANAGEABILITY = 10,
    P2P_ATTR_CHANNEL_LIST = 11,
    P2P_ATTR_NOTICE_OF_ABSENCE = 12,
    P2P_ATTR_DEVICE_INFO = 13,
    P2P_ATTR_GROUP_INFO = 14,
    P2P_ATTR_GROUP_ID = 15,
    P2P_ATTR_INTERFACE = 16,
    P2P_ATTR_OPERATING_CHANNEL = 17,
    P2P_ATTR_INVITATION_FLAGS = 18,
    P2P_ATTR_VENDOR_SPECIFIC = 221
};

#define P2P_MAX_REG_CLASSES 10
#define P2P_MAX_REG_CLASS_CHANNELS 20

#define P2P_WILDCARD_SSID "DIRECT-"
#define P2P_WILDCARD_SSID_LEN 7

struct p2p_channels {
    struct p2p_reg_class {
        A_UINT8 reg_class;
        A_UINT8 channel[P2P_MAX_REG_CLASS_CHANNELS];
        A_UINT8 channels;
    } reg_class[P2P_MAX_REG_CLASSES];
    A_UINT8 reg_classes;
};

#define P2P_NOA_DESCRIPTOR_LEN 13
struct p2p_noa_descriptor {
    A_UINT8   type_count; /* 255: continuous schedule, 0: reserved */
    A_UINT32  duration ;  /* Absent period duration in micro seconds */
    A_UINT32  interval;   /* Absent period interval in micro seconds */
    A_UINT32  start_time; /* 32 bit tsf time when in starts */
};

#define P2P_MAX_NOA_DESCRIPTORS 4
/*
 * Length = (2 octets for Index and CTWin/Opp PS) and
 * (13 octets for each NOA Descriptors)
 */
#define P2P_NOA_IE_SIZE(num_desc)     (2 + (13 * (num_desc)))

#define P2P_NOE_IE_OPP_PS_SET                     (0x80)
#define P2P_NOE_IE_CTWIN_MASK                     (0x7F)

struct p2p_sub_element_noa {
    A_UINT8        p2p_sub_id;
    A_UINT8        p2p_sub_len;
    A_UINT8        index;           /* identifies instance of NOA su element */
    A_UINT8        oppPS:1,         /* oppPS state of the AP */
                   ctwindow:7;      /* ctwindow in TUs */
    A_UINT8        num_descriptors; /* number of NOA descriptors */
    struct p2p_noa_descriptor noa_descriptors[P2P_MAX_NOA_DESCRIPTORS];
};

#define ETH_ALEN 6

struct p2p_ie {
    A_UINT8 dialog_token;
    const A_UINT8 *capability;
    const A_UINT8 *go_intent;
    const A_UINT8 *status;
    const A_UINT8 *listen_channel;
    const A_UINT8 *operating_channel;
    const A_UINT8 *channel_list;
    A_UINT8 channel_list_len;
    const A_UINT8 *config_timeout;
    const A_UINT8 *intended_addr;
    const A_UINT8 *group_bssid;
    const A_UINT8 *invitation_flags;

    const A_UINT8 *group_info;
    A_UINT32 group_info_len;
    const A_UINT8 *group_id;
    A_UINT32 group_id_len;

    const A_UINT8 *device_id;
    const A_UINT8 *manageability;

    const A_UINT8 *noa;
    A_UINT32 noa_len;
    const A_UINT8 *ext_listen_timing;

    const A_UINT8 *minor_reason_code;

    /* P2P Device Info */
    const A_UINT8 *p2p_device_info;
    A_UINT32 p2p_device_info_len;
    const A_UINT8 *p2p_device_addr;
    const A_UINT8 *pri_dev_type;
    A_UINT8 num_sec_dev_types;
    A_CHAR device_name[33];
    A_UINT8 dev_name_len;
    A_UINT16 config_methods;

    /* WPS IE */
    A_UINT16 dev_password_id;
    A_UINT16 wps_config_methods;
    const A_UINT8 *wps_pri_dev_type;
    A_CHAR   wps_device_name[33];
    A_UINT8  wps_dev_name_len;
    

    /* SSID IE */
    const A_UINT8 *ssid;
};

struct p2p_device {
    A_UINT16 listen_freq;
    A_UINT32 wps_pbc;
    A_CHAR wps_pin[WPS_MAX_PIN_LEN];
    A_UINT8 pin_len;
    enum p2p_wps_method wps_method;

    A_UINT8 p2p_device_addr[ETH_ALEN];
    A_UINT8 pri_dev_type[8];
    A_CHAR device_name[33];
    A_UINT16 config_methods;
    A_UINT8 dev_capab;
    A_UINT8 group_capab;

    A_UINT8 interface_addr[ETH_ALEN];

    /* Dev. Discoverability data */
    A_UINT8 dev_disc_dialog_token;
    A_UINT8 member_in_go_dev[ETH_ALEN];
    A_UINT8 member_in_go_iface[ETH_ALEN];
    A_UINT8 dev_disc_go_oper_ssid[WMI_MAX_SSID_LEN];
    A_UINT8 dev_disc_go_oper_ssid_len;
    A_UINT16 dev_disc_go_oper_freq;

    A_UINT32 go_neg_req_sent;
    enum p2p_go_state { UNKNOWN_GO, LOCAL_GO, REMOTE_GO } go_state;
    A_UINT8 dialog_token;
    A_UINT8 intended_addr[ETH_ALEN];

    A_CHAR country[3];
    struct p2p_channels channels;
    A_UINT16 oper_freq;
    A_UINT8 oper_ssid[P2P_MAX_SSID_LEN];
    A_UINT8 oper_ssid_len;

    A_UINT16 req_config_methods;

#define P2P_DEV_PROBE_REQ_ONLY BIT(0)
#define P2P_DEV_REPORTED BIT(1)
#define P2P_DEV_NOT_YET_READY BIT(2)
#define P2P_DEV_SD_INFO BIT(3)
#define P2P_DEV_SD_SCHEDULE BIT(4)
#define P2P_DEV_PD_PEER_DISPLAY BIT(5)
#define P2P_DEV_PD_PEER_KEYPAD BIT(6)
#define P2P_DEV_USER_REJECTED BIT(7)
#define P2P_DEV_PEER_WAITING_RESPONSE BIT(8)
#define P2P_DEV_PREFER_PERSISTENT_GROUP BIT(9)
#define P2P_DEV_WAIT_GO_NEG_RESPONSE BIT(10)
#define P2P_DEV_WAIT_GO_NEG_CONFIRM BIT(11)
#define P2P_DEV_GROUP_CLIENT_ONLY BIT(12)
#define P2P_DEV_FORCE_FREQ BIT(13)
#define P2P_DEV_PD_FOR_JOIN BIT(14)

    A_UINT32 flags;

    A_UINT32 status;
    A_UINT8 wait_count;
    A_UINT32 invitation_reqs;

    A_UINT16 ext_listen_period;
    A_UINT16 ext_listen_interval;

    A_UINT8 go_timeout;
    A_UINT8 client_timeout;
    A_UINT8 persistent_grp;
   
};
#endif /* _P2P_H_ */
