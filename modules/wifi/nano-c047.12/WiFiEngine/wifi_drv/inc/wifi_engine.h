
#ifndef WIFI_ENGINE_H
#define WIFI_ENGINE_H

#include "wei_tailq.h"

typedef int WiFiEngine_states_t;
/* States for the WiFiEngine state machine */
enum
{
   Invalid = 0,
   Idle,
   Unconnected,
   PassiveScan,
   ActiveScan,
   Power_Down_Disassociating,
   Connected,
   PowerInactive,
   PowerOff,
   PsInit,
   PsDisabledUnconnected,
   PsDisabledConnected,
   PsEnabledUnconnected,
   PsEnabledConnected,
   Halted
};

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

#ifndef WIFI_STATES_ONLY

/* #define WIFI_WEP_ONLY */


#ifndef WIFI_ENGINE_CLEAN_API
 #include "registry.h"
 #include "pkt_debug.h"
#endif /* ! WIFI_ENGINE_CLEAN_API */

#include "m80211_defs.h"

#ifndef driver_packet_ref
#define driver_packet_ref void*
#endif

typedef uint32_t ip_addr_t; /* IPv4 address in network byte order */

#define VLANID_MASK 0x7FF8
#define PRIO_MASK 0x0007

/*! Number of outstanding writes (really, data confirms) allowed */
#define WIFI_ENGINE_MAX_PENDING_REPLIES 6

#define WIFI_ENGINE_MAX_WEP_KEYS 4

/*! Negative values are more detailed error codes,
 * positive values are more detailed success codes.
 */

/* extended WIFI_ENGINE_FAILURE_RESOURCES codes */
#define WIFI_ENGINE_FAILURE_PS                     -1300
#define WIFI_ENGINE_FAILURE_DATA_QUEUE_FULL        -1201
#define WIFI_ENGINE_FAILURE_DATA_PATH              -1200
#define WIFI_ENGINE_FAILURE_NOT_CONNECTED          -1100
#define WIFI_ENGINE_FAILURE_LOCK                   -1002
#define WIFI_ENGINE_FAILURE_COREDUMP               -1001
/* XXX */
#define WIFI_ENGINE_FAILURE_NOT_SUPPORTED          -8 /**< The operation is not supported */
#define WIFI_ENGINE_FAILURE_ABORT                  -7 /**< The operation was aborted */
#define WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED        -6 /**< This function is not yet implemented */
#define WIFI_ENGINE_FAILURE_DEFER                  -5 /**< Try again later */
#define WIFI_ENGINE_FAILURE_INVALID_DATA           -4 /**< Data buffer contents was bad */
#define WIFI_ENGINE_FAILURE_RESOURCES              -3 /**< Not enough resources */
#define WIFI_ENGINE_FAILURE_NOT_ACCEPTED           -2 /**< NIC op was tried but failed */
#define WIFI_ENGINE_FAILURE_INVALID_LENGTH         -1 /**< Argument buffer size wrong */
#define WIFI_ENGINE_FAILURE                         0
#define WIFI_ENGINE_SUCCESS                         1
#define WIFI_ENGINE_SUCCESS_ABSORBED                2 /**< Packet was absorbed by the call */
#define WIFI_ENGINE_SUCCESS_DATA_CFM                3 /**< Packet was a data send confirm */
#define WIFI_ENGINE_SUCCESS_DATA_IND                4 /**< Packet was a data receive indication */
#define WIFI_ENGINE_FEATURE_DISABLED                5 /**< A required feature was disabled. The call failed but it should _NOT_ be a fatal error. */
#define WIFI_ENGINE_SUCCESS_AGGREGATED              6 /**< Start of aggregated packets */

/************ MUST BE NON-0 ************/
#define WIFI_NET_FILTER_AUTH                                1
#define WIFI_NET_FILTER_BSSID                               2
#define WIFI_NET_FILTER_CHANNEL                             3
#define WIFI_NET_FILTER_JOINABLE                            4
#define WIFI_NET_FILTER_MULTIDOMAIN                         5
#define WIFI_NET_FILTER_NO_IE                               6
#define WIFI_NET_FILTER_NULL                                7
#define WIFI_NET_FILTER_TYPE                                8
#define WIFI_NET_FILTER_WPS_NOT_CONFIGURED                  9
#define WIFI_NET_FILTER_ENC_NOT_NONE                       10
#define WIFI_NET_FILTER_ENC_WEP_OR_LESS                    11
#define WIFI_NET_FILTER_ENC_TKIP_OR_LESS                   14
#define WIFI_NET_FILTER_ENC_CCMP_OR_LESS                   13
#define WIFI_NET_FILTER_ENC_SMS4_OR_LESS                   12


#ifndef IN
    #define IN
#endif
#ifndef OUT
    #define OUT
#endif

#define MIC_CM_TIMEOUT (60*1000)
#define HEARTBEAT_DEF_TIMEOUT 1 /* seconds */
#define SCB_ERROR_REQUEST_TIMEOUT (1000)
#define COREDUMP_TIMEOUT (5000)
#define MONITOR_TRAFFIC_TIMEOUT (20) /* Used in power save forest mode*/
#define WMM_PS_AWAKE_WINDOW (10000) /* in microseconds */
#define PS_MAX_SLEEP_RETRIES (100)  /* max number of consecutive mlme_power_mgmt_cfm with reason not supported*/

/*! Power modes are ALWAYS_ON, DEEP_SLEEP and 802_11_PS
 */
typedef enum
{
   WIFI_ENGINE_PM_ALWAYS_ON,
   WIFI_ENGINE_PM_DEEP_SLEEP,
   WIFI_ENGINE_PM_SOFT_SHUTDOWN,
   WIFI_ENGINE_PM_802_11_PS
} WiFiEngine_PowerMode_t;

typedef enum
{
   WIFI_HMG_TRANSPARENT,
   WIFI_HMG_AUTO
} WiFiEngine_HmgMode_t;

typedef enum                  /* Encryption Method */
{
   Encryption_Disabled,       /**< Not encrypted */
   Encryption_WEP,            /**< WEP */
   Encryption_TKIP,           /**< TKIP */
   Encryption_CCMP,           /**< CCMP */
   Encryption_SMS4            /**< SMS4 */
} WiFiEngine_Encryption_t;

typedef enum                                          /* Authentication Method */
{
   Authentication_Open   = M80211_AUTH_OPEN_SYSTEM,   /**< Open System authentication mode */
   Authentication_Shared = M80211_AUTH_SHARED_KEY,    /**< Shared Key authentication mode (WEP key) */
   Authentication_Autoselect,                         /**< First try Open system, if that failes, try Shared Key authentication */
   Authentication_8021X,                              /**< 802.1X WEP/WPA/WPA2 */
   Authentication_WPA,                                /**< WPA version 1 security for infrastructure mode */
   Authentication_WPA_PSK,                            /**< WPA version 1 based on pre shared key */
   Authentication_WPA_None,                           /**< WPA version 1 security for ad hoc mode, based on pre shared key */
   Authentication_WPA_2,                              /**< WPA version 2 security for infrastructure mode */
   Authentication_WPA_2_PSK,                          /**< WPA version 2 based on pre shared key */
   Authentication_WAPI,
   Authentication_WAPI_PSK
} WiFiEngine_Auth_t;

typedef enum                                          /* BSS Type */
{
   weInfrastructure_BSS = M80211_CAPABILITY_ESS,        /**< Infrastructre BSS */
   weIndependent_BSS    = M80211_CAPABILITY_IBSS        /**< Independent BSS */
} WiFiEngine_bss_type_t;

typedef enum
{
   PHY_TYPE_AUTO = 0,   /**< No restriction */
   PHY_TYPE_DS,         /**< Only use DS band */
   PHY_TYPE_OFDM24,     /**< Only use the OFDM24 band */
   PHY_TYPE_INVALID     /**< Invalid network type */
} WiFiEngine_phy_t;

/* @{ */
typedef uint16_t WiFiEngine_scan_notif_pol_t;
#define WFE_SCAN_NOTIF_POL_FIRST               (1<<0)
#define WFE_SCAN_NOTIF_POL_JOB_COMPLETE        (1<<1)
#define WFE_SCAN_NOTIF_POL_ALL_JOBS_COMPLETE   (1<<2)
#define WFE_SCAN_NOTIF_POL_COMPLETE_WITH_HIT   (1<<3)
/* @} */

typedef struct
{
      int32_t filter_id;
      WiFiEngine_bss_type_t bss_type;
      int32_t rssi_thr;
      uint32_t snr_thr;
} WiFiEngine_scan_filter_t;

#ifdef WIFI_ENGINE_CLEAN_API
typedef enum {
   RISING=1,
   RSSIB_RISING,    /* RSSI Beacon */
   ANTENNABAR_RISING,
   RSSID_RISING,
   SNRB_RISING,
   SNRD_RISING,
   MISSEDBEACON_RISING,
   PER_RISING,
   FALLING=101,               /* Standard FALLING */
   RSSIB_FALLING,
   ANTENNABAR_FALLING,
   RSSID_FALLING,             /* RSSI Data */
   SNRB_FALLING,              /* SNR Beacon */
   SNRD_FALLING,              /* SNR Data */
   MISSEDBEACON_FALLING,
   PER_FALLING,
   MATCHING=200
} wei_supvevent_t;
#else
typedef uint16_t wei_supvevent_t;
#endif

typedef dot11CountersEntry_t WiFiEngine_stats_t;

typedef enum                        /* Network Status, mirror sNetworkStatus in registry.h */
{
   weNetworkStatus_Unknown,           /**< Unknown status of the network. */
   weNetworkStatus_StartBeacon,       /**< Fake host sending beacon*/
   weNetworkStatus_NoNet,             /**< A BSS with a specific SSID is not present. */
   weNetworkStatus_NetAware,          /**< There are a present network(s). */
   weNetworkStatus_Refused,           /**< The station was refused to join network. */
   weNetworkStatus_Timedout,          /**< The station was timed out when joining network. */
   weNetworkStatus_PassiveScanning,   /**< Passive Scanning is ongoing. */
   weNetworkStatus_ActiveScanning,    /**< Active Scanning is ongoing. */
   weNetworkStatus_Joined,            /**< Authenticating is ongoing. */
   weNetworkStatus_Start,             /**< Start ibss is ongoing. */
   weNetworkStatus_W4_NewSta,         /**< IBSS wait for peer sta beacon */
   weNetworkStatus_Authenticating,    /**< Authenticating is ongoing. */
   weNetworkStatus_Associating,       /**< Associating is ongoing. */
   weNetworkStatus_Connected          /**< The device is connected to network. */
} WiFiEngine_net_status_t;

typedef uint16_t we_ch_bitmask_t;

/*! Statistics for roaming agent */
typedef struct {
   bool_t blacklisted;
} wei_netlist_statistics_t;


typedef struct                                     /* Network descriptor. */
{
   wei_list_head_t            active_list;         /**< List head for the active list */
   wei_list_head_t            sta_list;            /**< List head for the sta list */
   int                        ref_cnt;             /**< Reference count for this object */
#ifdef USE_NEW_AGE
   uint32_t                   heard_at_scan_count; /**< Ageing timer based on scan count */
#endif
   long                       last_heard_tick;     /**< Ageing timer */
   int32_t                    measured_snr;
   WiFiEngine_net_status_t    status;              /**< Network status. */
   uint16_t                   listenInterval;      /**< Beacon listen interval. */
   m80211_mac_addr_t          bssId_AP;            /**< BSSID (for the network access point.) */
   m80211_mac_addr_t          peer_mac;            /**< MAC add of the peer sta (AP or sta)*/
   channel_list_t             channels;            /**< List of channels handled by the BSS. */
   uint32_t                   fail_count;          /**< Times we failed to connect */
   uint32_t                   join_time;           /**< Time we last tries to join */
   WiFiEngine_Auth_t          auth_mode;
   wei_netlist_statistics_t   net_statistics;      /**< Statistics for roaming agent */
   mlme_bss_description_t*    bss_p;               /**< Information about the network received in a beacon ind or probe rsp frame. */
} WiFiEngine_net_t;

typedef struct we_cb_container_s we_cb_container_t;
/*!
 * The WiFiEngine callback type.
 *
 * The callback must check the status parameter for WIFI_ENGINE_FAILURE_ABORT.
 * If the status is ABORT then the callback is executing in an arbitrary
 * context and may not access paged memory (the callback is being
 * cancelled by WiFiEngine).
 *
 * @param cbc The callback container describing the request. Memory
 *            allocated by WiFiEngine for the data member
 *            will be freed after the callback returns.
 */
typedef int (*we_callback_t)(we_cb_container_t *cbc);

typedef int (*we_ind_cb_t)(void *, size_t len);

/* This definition shall be moved to mac_api when multinet support is implemented in fw. */
typedef uint16_t mac_api_net_id_t;

#define NETWORK_ID_BSS_STA 0x0100
#define NETWORK_ID_IBSS    0x0200
#define NETWORK_ID_AMP     0x0300
#define NETWORK_ID_BSS_AP  0x0400


typedef int (*net_rx_cb_t) (mac_api_net_id_t net_id, char* data_buf, int size);

/*!
 * Structure used to store information about a callback function registered with
 * WiFiEngine.
 */
struct we_cb_container_s
{
   int           status;   /**< Return status of the operation that was completed.
                              Negative values are internal errors. Positive
                              values are status values from the operation. */
   uint32_t      trans_id; /**< Transaction ID of reply that should trigger scheduling of the callback */
   void          *data;    /**< Data buffer that will be passed to the callback at invocation time */
   void          *ctx;     /**< Usage-specific context. This will be passed unaltered to
                              the callback and can be used as a way to pass context
                              between the registrating function and the callback. */
   size_t        data_len; /**< Data buffer length in bytes */
   size_t        ctx_len;  /**< Context buffer length.      */
   int           repeating; /**< Specify if the callback should be replaced
                               on the pending callback queue after dispatch. */
   we_callback_t cb;       /**< Callback function pointer */

   WEI_TQ_ENTRY(we_cb_container_s) cb_all;
   WEI_TQ_ENTRY(we_cb_container_s) cb_cbq;
   WEI_TQ_ENTRY(we_cb_container_s) cb_pending;
};

/*!
 * Trigger direction. Must agree with NRX API
 */
#define WE_TRIG_THR_RISING  (1<<0) /**< Trigger when the value rises above the threshold */
#define WE_TRIG_THR_FALLING (1<<1) /**< Trigger when the value falls below the threshold */

/*!
 * Structure used to pass MIB trigger indication data to trigger
 * callbacks.
 */
#define WE_TRIG_TYPE_CFM 0
#define WE_TRIG_TYPE_IND 1
#define WE_TRIG_TYPE_CANCEL 2   /* Only used with vir trig */
typedef struct {
      uint32_t trig_id; /**< Trigger ID of the triggered trigger.   */
      uint32_t type;    /**< WE_TRIG_TYPE_CFM or WE_TRIG_TYPE_IND   */
      uint32_t result;  /**< Result status. Only valid if type == WE_TRIG_TYPE_CFM  */
      size_t len;       /**< Length of the data array that follows. */
      char data[1];     /**< Data array, may extend beyond the struct */
} we_mib_trig_data_t;

/* Cancel vir trig reason */
#define NRX_REASON_ONE_SHOT   1 /**< The trigger was a one shot trigger. */
#define NRX_REASON_RM_BY_USER 2 /**< The trigger was intentionally removed. */
#define NRX_REASON_REG_FAILED 3 /**< Registration in FW failed. */
#define NRX_REASON_SHUTDOWN   4 /**< WiFiEngine is shutting down. */

typedef struct {
      uint32_t trig_id; /**< Id of the trigger.  */
      uint32_t type;    /**< WE_TRIG_TYPE_IND or WE_TRIG_TYPE_CANCEL  */
      int32_t value;    /**< Value of mib. Only for type == WE_TRIG_TYPE_IND */
      uint32_t reason;  /**< Reason code. Only valid if type == WE_TRIG_TYPE_CANCEL  */
} we_vir_trig_data_t;

typedef enum
{
   WE_CONNECT_NOT_PRESENT,
   WE_CONNECT_WIFI_ENGINE_FAIL,
   WE_CONNECT_JOIN_FAIL,
   WE_CONNECT_AUTH_FAIL,
   WE_CONNECT_ASSOC_FAIL,
   WE_CONNECT_PEER_STATUS,
   WE_CONNECT_USER
} we_con_failed_e;

typedef struct we_connect_failed {
   we_con_failed_e type;
   uint32_t reason_code;
   void *param;
   size_t len;
} we_con_failed_s;

typedef enum
{
   WE_DISCONNECT_NOT_PRESENT,
   WE_DISCONNECT_DEAUTH,
   WE_DISCONNECT_DEAUTH_IND,
   WE_DISCONNECT_DISASS,
   WE_DISCONNECT_DISASS_IND,
   WE_DISCONNECT_USER,
   WE_DISCONNECT_PEER_STATUS_TX_FAILED,
   WE_DISCONNECT_PEER_STATUS_RX_BEACON_FAILED,
   WE_DISCONNECT_PEER_STATUS_ROUNDTRIP_FAILED,
   WE_DISCONNECT_PEER_STATUS_TX_FAILED_WARNING,
   WE_DISCONNECT_PEER_STATUS_RX_BEACON_FAILED_WARNING,
   WE_DISCONNECT_PEER_STATUS_RESTARTED,
   WE_DISCONNECT_PEER_STATUS_INCOMPATIBLE
} we_con_lost_e;

typedef struct we_disconnected {
   we_con_lost_e type;
   uint32_t reason_code;
   void *param;
   size_t len;
} we_con_lost_s;

typedef enum
{
    AGGR_ALL,
    AGGR_SCAN_IND,
    AGGR_ALL_BUT_DATA,
    AGGR_ONLY_DATA_CFM,
    AGGR_DATA,
    AGGR_LAST_MARK
}we_aggr_type_t;


typedef struct {
      uint8_t reason_code;
} we_conn_incompatible_ind_t;


typedef struct { /* Must match nrx_scan_notif_t in libnrx */
      int32_t pol;
      int32_t sj_id;
} we_scan_notif_t;

/* PMKID support */

#define M80211_PMKID_SIZE 16
typedef struct
{
   char octet[M80211_PMKID_SIZE];
} m80211_pmkid_value;

typedef struct
{
    m80211_mac_addr_t  BSSID;
    m80211_pmkid_value PMKID;
} m80211_bssid_info;

/* WiFiEngine indications */
typedef enum {

   WE_IND_80211_CONNECTING,    /* 0 */
   WE_IND_80211_CONNECT_FAILED,
   WE_IND_80211_CONNECTED,
   WE_IND_80211_DISCONNECTING,
   WE_IND_80211_DISCONNECTED,  /* 4 */
   WE_IND_80211_IBSS_DISCONNECTED,
   WE_IND_80211_IBSS_CONNECTED,

   WE_IND_CM_CONNECTING,       /* 7 */
   WE_IND_CM_CONNECT_FAILED,
   WE_IND_CM_CONNECTED,
   WE_IND_CM_DISCONNECTING,
   WE_IND_CM_DISCONNECTED,     /* 11 */

   WE_IND_ROAM_BAIL,

    // ---- will be removed ---
   WE_IND_AP_MISSING_IN_ACTION, /* (! indicate that we unexpectedly lost the AP */
   WE_IND_CONN_INCOMPATIBLE,    /* <! configuration incompatible with AP */
    // ------------------------

   WE_IND_AUTH_STATUS_CHANGE,   /* <! change in authentication mode */
   WE_IND_RSSI_TRIGGER,         /* <! RSSI level has passed the trigger value */
   WE_IND_PAIRWISE_MIC_ERROR,   /* <! pairwise MIC failure detected */
   WE_IND_GROUP_MIC_ERROR,      /* <! group MIC failure detected */
   WE_IND_MIC_ERROR,            /* <! MIC failure (with mic struct) */
   WE_IND_SLEEP_FOREVER,        /* <! target sleep? */
   WE_IND_CANDIDATE_LIST,       /* <! change in candidate list */

   WE_IND_ENABLE_TARGET_SLEEP,
   WE_IND_DISABLE_TARGET_SLEEP,
   WE_IND_ENABLE_TARGET_INTERFACE,
   WE_IND_DISABLE_TARGET_INTERFACE,
   WE_IND_ENABLE_TARGET_BOOT,
   WE_IND_DISABLE_TARGET_BOOT,

   WE_IND_SCAN_COMPLETE,        /* <! a scan has completed */
   WE_IND_SCAN_INDICATION,      /* <! partial scan result */

   WE_IND_ROAMING_START,        /* <! roaming agent is about to switch AP */
   WE_IND_ROAMING_COMPLETE,     /* <! roaming agent has switched AP */
   WE_IND_TX_QUEUE_FULL,        /* <! target tx queue is full */
   WE_IND_ACTIVITY_TIMEOUT,     /* <! driver has been inactive for a while */

   WE_IND_WPA_CONNECTED,        /* <! supplicant has finished connecting */
   WE_IND_WPA_DISCONNECTED,     /* <! supplicant has lost connection */
   WE_IND_BEACON,               /* <! Consecutive lost beacons above warning threshold */
   WE_IND_TXFAIL,               /* <! Trasnmission retries above threshold */

   WE_IND_CONSOLE_REPLY,        /* <! Console (or flash) reply received */

   WE_IND_MIB_DOT11RSSI,        /* <! notify that a new value has been received */
   WE_IND_MIB_DOT11RSSI_DATA,   /* <! notify that a new value has been received */
   WE_IND_MIB_DOT11SNR_BEACON,  /* <! notify that a new value has been received */
   WE_IND_MIB_DOT11SNR_DATA,    /* <! notify that a new value has been received */

   WE_IND_NOOP,                 /* <! used when an WE_IND_... must be supplied but has no meening */

   WE_IND_CORE_DUMP_START,
   WE_IND_CORE_DUMP_COMPLETE,

   WE_IND_PS_WAKEUP_IND,             /* <! notify that wakeup_ind has been recevied */
   WE_IND_PS_CONNECT_COMPLETE,       /* <! notify that ps state change for connect is complete */
   WE_IND_PS_DISCONNECT_COMPLETE,    /* <! notify that ps state change for disconnect is complete */
   WE_IND_PS_WMM_REQ_COMPLETE,       /* <! notify that wmm req is complete */
   WE_IND_HIC_PS_INTERFACE_ENABLED,  /* <! notify that hic ps interface is enabled */
   WE_IND_HIC_PS_INTERFACE_DISABLED, /* <! notify that hic ps interface is disabled */
   WE_IND_PS_ENABLED,                /* <! notify pm that hic ps is enabled */
   WE_IND_PS_DISABLED,               /* <! notify pm that hic ps is disbled */
   WE_IND_INTERFACE_DOWN_SENT,       /* <! hic_ctrl_interface_down has been sent  */
   WE_IND_JOIN_CFM,                  /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_AUTHENTICATE_CFM,          /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_ASSOCIATE_CFM,             /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_DEAUTHENTICATE_IND,        /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_LINK_LOSS_IND,             /* <! Used in transparent mode to indicate reception of peer status-beacon fail  */
   WE_IND_TX_FAIL_IND,               /* <! Used in transparent mode to indicate reception of peer status-tx fail  */
   WE_IND_DISASSOCIATE_IND,          /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_REASSOCIATE_CFM,           /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_DEAUTHENTICATE_CFM,        /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_DISASSOCIATE_CFM,          /* <! Used in transparent mode to indicate reception of a confirm  */
   WE_IND_MICHAEL_MIC_FAILURE_IND,   /* <! Used in transparent mode to indicate reception of a confirm  */

   WE_IND_MAC_TIME,                  /* <! notify that a new value has been received */
   WE_IND_MAC_ECHO_REPLY,            /* <! Indicates that we have received an echo-reply  */
   WE_IND_LAST_MARK                  /* <! Must be the last element in the enum */

} we_indication_t;

#include "we_ind.h"

typedef enum {
   WE_ROAM_REASON_NONE,         /* <! Roaming was never initiated */
   WE_ROAM_REASON_UNSPECIFIED,  /* <! Unspecified reason          */
   WE_ROAM_REASON_RSSI,         /* <! RSSI too low                */
   WE_ROAM_REASON_SNR,          /* <! SNR too low                 */
   WE_ROAM_REASON_DS,           /* <! Delay spread too high       */
   WE_ROAM_REASON_RATE,         /* <! TX rate too low             */
   WE_ROAM_REASON_TX_FAIL,      /* <! Too many consecutive TX failures */
   WE_ROAM_REASON_BEACON,       /* <! Too many consecutive missed beacons */
   WE_ROAM_REASON_CONNECT_FAIL, /* <! Connect to AP failed        */
   WE_ROAM_REASON_DISCONNECTED, /* <! AP sent disconnect request */
   WE_ROAM_REASON_INCOMPATIBLE, /* <! AP config incompatible with driver config */
   WE_ROAM_REASON_CORE_DUMP,    /* <! Reconnect after coredump */
   WE_ROAM_REASON_PEER_STATUS   /* <! user WiFiEngine_get_last_con_lost_reason for more info */
} we_roaming_reason_t;


typedef uint8_t we_xmit_rate_t;
#define WE_XMIT_RATE_1MBIT      2
#define WE_XMIT_RATE_2MBIT      4
#define WE_XMIT_RATE_5_5MBIT    11
#define WE_XMIT_RATE_6MBIT      12
#define WE_XMIT_RATE_9MBIT      18
#define WE_XMIT_RATE_11MBIT     22
#define WE_XMIT_RATE_12MBIT     24
#define WE_XMIT_RATE_18MBIT     36
#define WE_XMIT_RATE_22MBIT     44
#define WE_XMIT_RATE_24MBIT     48
#define WE_XMIT_RATE_33MBIT     66
#define WE_XMIT_RATE_36MBIT     72
#define WE_XMIT_RATE_48MBIT     96
#define WE_XMIT_RATE_54MBIT     108
#define WE_XMIT_RATE_NUM_RATES 14

#define WE_XMIT_RATE_INVALID   WE_XMIT_RATE_NUM_RATES
#define WE_XMIT_RATE_MIN_RATE  WE_XMIT_RATE_1MBIT
#define WE_XMIT_RATE_MAX_RATE  WE_XMIT_RATE_54MBIT

/* operate on bit-vector, A is bit-array (of arbitrary size), and B is
 * bit-number, OP is whatever operation should be performed */

#define WE_BITMASK_SIZE(A) (8 * sizeof((A)))
#define WE_BITMASK_UNITSIZE(A) (8 * sizeof((A)[0]))
#define WE_BITMASK_INDEX(A, B) ((B) / WE_BITMASK_UNITSIZE((A)))

#define WE_BITOP(A, OP, B)                                              \
   ((A)[WE_BITMASK_INDEX((A), (B))] OP (1 << ((B) % WE_BITMASK_UNITSIZE(A))))

#define WE_SET_BIT(A, B) WE_BITOP(A, |=, B)
#define WE_CLEAR_BIT(A, B) WE_BITOP(A, &= ~, B)
#define WE_TEST_BIT(A, B) WE_BITOP(A, &, B)

#define WE_BITMASK_FOREACH_SET(B, A)                    \
   for((B) = 0; (B) < WE_BITMASK_SIZE((A)); (B)++)      \
      if((A)[WE_BITMASK_INDEX((A), (B))] == 0) {        \
         /* skip this unit */                           \
         (B) = (WE_BITMASK_INDEX((A), (B)) + 1)         \
            * WE_BITMASK_UNITSIZE((A)) - 1;             \
         continue;                                      \
      } else if(WE_TEST_BIT((A), (B)))

typedef struct we_ratemask {
   uint32_t rates[4];
} we_ratemask_t;

#define WE_RATEMASK_SETRATE(M, R) WE_SET_BIT((M).rates, (R) & 0x7f)
#define WE_RATEMASK_CLEARRATE(M, R) WE_CLEAR_BIT((M).rates, (R) & 0x7f)
#define WE_RATEMASK_TESTRATE(M, R) WE_TEST_BIT((M).rates, (R) & 0x7f)

#define WE_RATEMASK_CLEAR(M) DE_MEMSET((M).rates, 0, sizeof((M).rates))

#define WE_RATEMASK_AND(M1, M2) do {                \
   unsigned int __ii;                       \
   for(__ii = 0; __ii < DE_ARRAY_SIZE((M1).rates); __ii++)  \
      (M1).rates[__ii] &= (M2).rates[__ii];         \
} while(0)

#define WE_RATEMASK_OR(M1, M2) do {             \
   unsigned int __ii;                       \
   for(__ii = 0; __ii < DE_ARRAY_SIZE((M1).rates); __ii++)  \
      (M1).rates[__ii] |= (M2).rates[__ii];         \
} while(0)

#define WE_RATEMASK_FOREACH(V, M)                               \
        for((V) = 0; (V) <= WE_XMIT_RATE_MAX_RATE; (V)++)   \
           if(WE_RATEMASK_TESTRATE((M), (V)))

#define WE_RATEMASK_FOREACH_REVERSE(V, M)                       \
        for((V) = WE_XMIT_RATE_MAX_RATE; (V) >= 0; (V)--)   \
           if(WE_RATEMASK_TESTRATE((M), (V)))

/* Used to control power save inhibit/allow */
typedef struct we_ps_control
{
   char name[32];
   uint32_t control_mask;
}we_ps_control_t;


typedef enum _FW_TYPE{
   FW_TYPE_X_MAC,
   FW_TYPE_X_TEST
} fw_type;

/******* WiFiEngine interface ******/

int we_filter_out_ssid(WiFiEngine_net_t* net);
/* Start/stop (we_init.c) */

unsigned int      WiFiEngine_Initialize(void*);
int               WiFiEngine_Shutdown(unsigned int driver_id);
int               WiFiEngine_Plug(void);
int               WiFiEngine_Unplug(void);
int               WiFiEngine_Configure_Device(int);
int               WiFiEngine_Restart_Heartbeat(void);
int               WiFiEngine_Set_Chip_Type(int chip_type);
int               WiFiEngine_Set_Fpga_Version(char *version);

int WiFiEngine_sac_start(void);
int WiFiEngine_sac_stop(void);

/* Target FW handling (we_fw.c) */
int   WiFiEngine_DownloadFirmware(fw_type type);
int   WiFiEngine_Firmware_LoadMIBTable(fw_type type);

/* Scan interface (we_scan.c) */

int WiFiEngine_FlushScanList(void);
int WiFiEngine_StartNetworkScan(void);
int WiFiEngine_TriggerScanJob(uint32_t job_id, uint16_t channel_interval);
int WiFiEngine_TriggerScanJobEx(uint32_t job_id, uint16_t channel_interval,
                                uint32_t probe_delay, uint16_t min_ch_time, uint16_t max_ch_time);
int WiFiEngine_GetScanList(WiFiEngine_net_t **list, int *entries);
int WiFiEngine_ReleaseScanList(WiFiEngine_net_t **list, int entries);
int WiFiEngine_ConfigureScan(preamble_t preamble,
                             uint8_t rate,
                             uint8_t probes_per_ch,
                             WiFiEngine_scan_notif_pol_t notif_pol,
                             uint32_t scan_period,
                             uint32_t probe_delay,
                             uint16_t pa_min_ch_time,
                             uint16_t pa_max_ch_time,
                             uint16_t ac_min_ch_time,
                             uint16_t ac_max_ch_time,
                             uint32_t as_scan_period,
                             uint16_t as_min_ch_time,
                             uint16_t as_max_ch_time,
                             uint32_t max_scan_period,
                             uint32_t max_as_scan_period,
                             uint8_t  period_repetition,
                             we_cb_container_t *cbc);

int WiFiEngine_ReConfigureScan(void);

int WiFiEngine_GetScanConfig(preamble_t* preamble,
                             uint8_t* rate,
                             uint8_t* probes_per_ch,
                             WiFiEngine_scan_notif_pol_t* notif_pol,
                             uint32_t* scan_period,
                             uint32_t* probe_delay,
                             uint16_t* pa_min_ch_time,
                             uint16_t* pa_max_ch_time,
                             uint16_t* ac_min_ch_time,
                             uint16_t* ac_max_ch_time,
                             uint32_t* as_scan_period,
                             uint16_t* as_min_ch_time,
                             uint16_t* as_max_ch_time,
                             uint32_t* max_scan_period,
                             uint32_t* max_as_scan_period,
                             uint8_t*  period_repetition);

int WiFiEngine_AddScanFilter(uint32_t *sf_id,
                             uint8_t bss_type,
                             int32_t rssi_thr,
                             uint32_t snr_thr,
                             uint16_t threshold_type,
                             we_cb_container_t *cbc);
int WiFiEngine_RemoveScanFilter(uint32_t sf_id,
                                we_cb_container_t *cbc);
int WiFiEngine_AddScanJob(uint32_t *sj_id,
                          m80211_ie_ssid_t ssid,
                          m80211_mac_addr_t bssid,
                          uint8_t scan_type,
                          channel_list_t ch_list,
                          int flags,
                          uint8_t prio,
                          uint8_t ap_exclude,
                          int sf_id,
                          uint8_t run_every_nth_period,
                          we_cb_container_t *cbc);
int WiFiEngine_RemoveScanJob(uint32_t sj_id,
                             we_cb_container_t *cbc);
int WiFiEngine_RemoveAllScanJobs(void);
int WiFiEngine_SetScanJobState(uint32_t sj_id,
                               uint8_t state,
                               we_cb_container_t *cbc);
int WiFiEngine_GetScanFilter(uint32_t sf_id,
                 WiFiEngine_scan_filter_t *filter);
int WiFiEngine_RegisterScanNotification(we_cb_container_t *cbc);
int WiFiEngine_DeregisterScanNotification(void);

/* Associate (we_assoc.c) */

int   WiFiEngine_Connect(WiFiEngine_net_t *net);
int   WiFiEngine_Join(WiFiEngine_net_t *net);
int   WiFiEngine_Authenticate(void);
int   WiFiEngine_Associate(void);
int   WiFiEngine_Disconnect(void);
int   WiFiEngine_LeaveIbss(void);
int   WiFiEngine_Deauthenticate(void);
int   WiFiEngine_Disassociate(void);
int   WiFiEngine_Reconnect(void);
int   WiFiEngine_GetBSSIDList(WiFiEngine_net_t *list, int *entries);
WiFiEngine_net_t *WiFiEngine_GetAssociatedNet(void);
WiFiEngine_net_t *WiFiEngine_GetNetBySSID(m80211_ie_ssid_t ssid);


/* Power save (we_ps.c) */
void   WiFiEngine_StopDelayPowerSaveTimer(void);
int    WiFiEngine_StartDelayPowerSaveTimer(void);
bool_t WiFiEngine_IsDelayPowerSaveTimerRunning(void);
void   WiFiEngine_PsSendInterface_Down(void);
bool_t WiFiEngine_IsReasonHost(hic_ctrl_wakeup_ind_t *ind);
bool_t WiFiEngine_IsInterfaceDown(char* cmd);
int    WiFiEngine_SleepDevice(void);
int    WiFiEngine_GetPowerMode(WiFiEngine_PowerMode_t *power_mode);
rPowerSaveMode   WiFiEngine_GetRegistryPowerFlag(void);
int    WiFiEngine_SetPowerMode(WiFiEngine_PowerMode_t power_mode, we_ps_control_t *ps_uid);
void   WiFiEngine_DHCPStart(void);
void   WiFiEngine_DHCPReady(void);
void   WiFiEngine_IndicateTXQueueLength(uint16_t length);
we_ps_control_t *WiFiEngine_PSControlAlloc(const char *name);
void   WiFiEngine_PSControlFree(we_ps_control_t *psc);
void   WiFiEngine_PSControlInhibit(we_ps_control_t *psc);
void   WiFiEngine_PSControlAllow(we_ps_control_t *psc);
void   WiFiEngine_InhibitPowerSave(we_ps_control_t *control);
we_ps_control_t *WiFiEngine_GetPowerSaveUid(const char *name);
void   WiFiEngine_DisablePowerSaveForAllUid(void);
void   WiFiEngine_AllowPowerSave(we_ps_control_t *control);
void   WiFiEngine_DisablePowerSave(we_ps_control_t *control);
int    WiFiEngine_isPowerSaveAllowed(void);
int    WiFiEngine_SoftShutdown(void);
int    WiFiEngine_isAssocSupportingWmmPs(void);
int    WiFiEngine_WmmPowerSaveEnable(uint32_t sp_len);
int    WiFiEngine_WmmPowerSaveConfigure(uint32_t tx_period, bool_t be, bool_t bk, bool_t vi, bool_t vo);
int    WiFiEngine_WmmPowerSaveDisable(void);
int    WiFiEngine_EnableLegacyPsPollPowerSave(bool_t enable);
bool_t WiFiEngine_LegacyPsPollPowerSave(void);
int    WiFiEngine_LegacyPsConfigurationChanged(void);
void   WiFiEngine_PsCheckQueues(void);
int    WiFiEngine_isPSCtrlAllowed(we_ps_control_t *control);


/* Data path/Packet handling (we_data.c) */

int   WiFiEngine_ProcessReceivedPacket(char *pkt, size_t pkt_len,
                                       char **stripped_pkt,
                                       size_t *stripped_pkt_len,
                                       uint16_t *vlanid_prio,
                                       uint32_t *transid);

int  WiFiEngine_DebugStatsProcessSendPacket(int status);
void WiFiEngine_DebugMsgProcessSendPacket(int status);
int   WiFiEngine_ProcessSendPacket(char *eth_hdr, size_t eth_hdr_len, size_t pkt_len,
                                   char *hdr, size_t* hdr_len_p,
                                   uint16_t vlanid_prio, uint32_t *transid);
uint16_t WiFiEngine_GetDataHeaderSize(void);
size_t   WiFiEngine_GetPaddingLength(size_t data_packet_size);

/* MIB (mibtable.c) */

#if DE_MIB_TABLE_SUPPORT == CFG_ON
int WiFiEngine_RegisterMIBTable(const void*, size_t, uint32_t);
#endif

/* MIB/Console (we_mib.c) */

int   WiFiEngine_SendMIBGet(const char *mib_id, uint32_t *num);
int   WiFiEngine_SendMIBSet(const char *mib_id, uint32_t *num, const void *inbuf, size_t inbuflen);

int   WiFiEngine_GetMIBResponse(uint32_t num, void *outbuf, IN OUT size_t *buflen);
int   WiFiEngine_GetMIBResponse_raw(uint32_t num, void *outbuf, IN OUT size_t *buflen);
int   WiFiEngine_SendHICMessage(char *inbuf, size_t inbuflen, uint32_t *num);
int   WiFiEngine_SendConsoleRequest(const char *comand, uint32_t *num);
int   WiFiEngine_GetConsoleReply(void *outbuf, IN OUT size_t *buflen);
int   WiFiEngine_GetMIBAsynch(const char *id, we_cb_container_t *cbc);
int   WiFiEngine_SetMIBAsynch(const char *id,
                  uint32_t *num,
                  const void *inbuf,
                  size_t inbuflen,
                  we_cb_container_t *cbc);
int   WiFiEngine_RatelimitMIBGet(const char *id, uint32_t period_ms,
                                 void *data, size_t len);
int WiFiEngine_RegisterMIBTrigger(int32_t *trig_id,
                  const char *id,
                  uint32_t gating_trig_id,
                  uint32_t          supv_interval,
                  int32_t           level,
                  wei_supvevent_t   event,       /* supvevent_t */
                  uint16_t          event_count,
                  uint16_t          triggmode,
                  we_cb_container_t *cbc);
int WiFiEngine_DeregisterMIBTrigger(uint32_t trig_id);
int WiFiEngine_SetGatingMIBTrigger(uint32_t trig_id, uint32_t gating_trig_id, we_cb_container_t *cbc);
int WiFiEngine_RegisterVirtualTrigger(int32_t         *trig_id,
                                     const char       *mib_id,
                                     uint32_t          gating_trig_id,
                                     uint32_t          supv_interval,
                                     int32_t           level,
                                     uint8_t           dir, /* rising/falling */
                                     uint16_t          event_count,
                                     uint16_t          triggmode,
                                     we_ind_cb_t       cb);
int WiFiEngine_DelVirtualTrigger(int32_t trig_id);
bool_t WiFiEngine_DoesVirtualTriggerExist(int32_t trig_id,
                                          const char *mib_id);
int WiFiEngine_ReregisterAllVirtualTriggers(void);
int WiFiEngine_RegisterVirtualIERTrigger(int *thr_id,
                                         uint32_t ier_thr,
                                         uint32_t per_thr,
                                         uint32_t chk_period,
                                         uint8_t dir,
                                         we_ind_cb_t cb);
int WiFiEngine_DelVirtualIERTrigger(int thr_id);


/* Utility functions (we_util.c)*/
WiFiEngine_net_t* WiFiEngine_CreateNet(void);
WiFiEngine_net_t* WiFiEngine_GetCurrentNet(void) ;
void  WiFiEngine_SetCurrentNet(WiFiEngine_net_t* net);
void  WiFiEngine_FreeCurrentNet(void);
int   WiFiEngine_IsDataSendOk(void);
int   WiFiEngine_IsCommandSendOk(void);
int   WiFiEngine_DataFrameDropped(const void*);
int   WiFiEngine_ScanInProgress(void);
int   WiFiEngine_HardwareState(void);
int   WiFiEngine_isDisconnectInProgress(void);
int   WiFiEngine_GetNetworkStatus(int *net_status);
bool_t WiFiEngine_isWMMAssociated(void);
void  WiFiEngine_RegisterAdapter(void *adapter);
void *WiFiEngine_GetAdapter(void);
int   WiFiEngine_GetCachedAssocReqIEs(void *dst, size_t *len);
int   WiFiEngine_GetCachedAssocCfm(m80211_mlme_associate_cfm_t *dst);
int   WiFiEngine_GetCachedAssocCfmIEs(void *dst, size_t *len);
void  WiFiEngine_RandomMAC(int32_t flags, void *mac, size_t len);
void  WiFiEngine_RandomBSSID(m80211_mac_addr_t *bssid);
int   WiFiEngine_PackIEs(char *buf, size_t buflen, size_t *outlen,
                         m80211_bss_description_t *bss_desc);
int   WiFiEngine_PackRSNIE(m80211_ie_rsn_parameter_set_t*, void*, size_t*);
int   WiFiEngine_PackWPAIE(m80211_ie_wpa_parameter_set_t*, void*, size_t*);
int   WiFiEngine_PackWMMIE(m80211_ie_WMM_information_element_t*, void*, size_t*);
int   WiFiEngine_PackWMMPE(m80211_ie_WMM_parameter_element_t*, void*, size_t*);
int   WiFiEngine_PackWPSIE(m80211_ie_wps_parameter_set_t*, void*, size_t*);
int   WiFiEngine_PackWAPIIE(m80211_ie_wapi_parameter_set_t*, void*, size_t*);
int   WiFiEngine_CreateSSID(m80211_ie_ssid_t *ssid, char *name, size_t len);

int   WiFiEngine_RegisterEAPOLHandler(int (*handler)(const void *, size_t));

/* 802.11 properties (we_prop.c). */

int   WiFiEngine_GetDesiredBSSID(m80211_mac_addr_t *bssid);
int   WiFiEngine_SetDesiredBSSID(const m80211_mac_addr_t *bssid);
int   WiFiEngine_GetBSSID(m80211_mac_addr_t *bssid);
int   WiFiEngine_SetBSSID(const m80211_mac_addr_t *bssid);
int   WiFiEngine_GetMACAddress(char *addr, IN OUT int *byte_count);
int   WiFiEngine_SetMACAddress(char *addr, int byte_count);
int   WiFiEngine_GetBeaconPeriod(unsigned long *period);
int   WiFiEngine_SetBeaconPeriod(unsigned long period);
int   WiFiEngine_GetDTIMPeriod(uint8_t *period);
int   WiFiEngine_GetActiveChannel(uint8_t *channel);
int   WiFiEngine_SetActiveChannel(uint8_t channel);

int   WiFiEngine_GetIBSSBeaconPeriod(uint16_t *period);
int   WiFiEngine_SetIBSSBeaconPeriod(uint16_t period);
int   WiFiEngine_GetIBSSDTIMPeriod(uint8_t *period);
int   WiFiEngine_SetIBSSDTIMPeriod(uint8_t period);
int   WiFiEngine_GetIBSSATIMWindow(uint16_t *period);
int   WiFiEngine_SetIBSSATIMWindow(uint16_t period);
int   WiFiEngine_SetIBSSTXChannel(uint8_t channel);
int   WiFiEngine_GetIBSSTXChannel(uint8_t *channel);
int   WiFiEngine_SetIBSSSupportedRates(uint8_t *rates, size_t len);

int   WiFiEngine_GetRSSI(int32_t *rssi_level, int poll);
int   WiFiEngine_GetDataRSSI(int32_t *rssi_level, int poll);
int   WiFiEngine_GetSNR(int32_t *snr_level, int poll);
int   WiFiEngine_GetDataSNR(int32_t *snr_level, int poll);

#define NOISE_UNKNOWN (-100)
struct we_noise_floor {
   int32_t noise_dbm[14];
};

int   WiFiEngine_GetNoiseFloor(struct we_noise_floor *noise_floor);
int   WiFiEngine_GetSSID(m80211_ie_ssid_t *ssid);
void  WiFiEngine_DisableSSID(void);
int   WiFiEngine_SetSSID(char *ssid, int byte_count);
int   WiFiEngine_SetSupportedRates(uint8_t*, size_t);

int   WiFiEngine_GetSupportedRates(we_ratemask_t *rates);
int   WiFiEngine_GetBSSRates(we_ratemask_t *rateMask);

int   WiFiEngine_GetRegionalChannels(channel_list_t *channels);
int   WiFiEngine_SetRegionalChannels(const channel_list_t *channels);
int   WiFiEngine_GetFragmentationThreshold(int *frag_threshold);
int   WiFiEngine_SetFragmentationThreshold(int frag_threshold);
int   WiFiEngine_GetRTSThreshold(int *rts_threshold);
int   WiFiEngine_SetRTSThreshold(int rts_threshold);
int   WiFiEngine_GetJoin_Timeout(int* joinTimeout);
int   WiFiEngine_SetJoin_Timeout(int joinTimeout);
int   WiFiEngine_GetBSSBasicRateSet(m80211_ie_supported_rates_t *rates);
int   WiFiEngine_GetBSSType(WiFiEngine_bss_type_t *bss_type);
int   WiFiEngine_SetBSSType(WiFiEngine_bss_type_t bss_type);
int   WiFiEngine_GetNetworkTypeInUse(WiFiEngine_phy_t *type);
int   WiFiEngine_GetSupportedNetworkTypes(WiFiEngine_phy_t *type,
                                          IN int index);
int   WiFiEngine_GetCurrentTxRate(unsigned int *rate);
int   WiFiEngine_GetCurrentRxRate(unsigned int *rate);
int   WiFiEngine_SetWMMEnable(rSTA_WMMSupport enable);
int   WiFiEngine_GetWMMEnable(rSTA_WMMSupport *enable);
int   WiFiEngine_ConfigureHICaggregation(we_aggr_type_t type, uint32_t aggr_max_size);

/* we_mcast.c */
#define WE_MCAST_FLAG_EXCLUDE   1 /* addresses in multicast list is filtered out */
#define WE_MCAST_FLAG_ALLMULTI  2 /* receive all multicast addresses */
#define WE_MCAST_FLAG_HOLD      4 /* hold firmware updates */
int   WiFiEngine_MulticastAdd(m80211_mac_addr_t);
int   WiFiEngine_MulticastRemove(m80211_mac_addr_t);
int   WiFiEngine_MulticastClear(void);
int   WiFiEngine_MulticastSetFlags(unsigned int);
int   WiFiEngine_MulticastClearFlags(unsigned int);
int   WiFiEngine_SetMulticastFilter(m80211_mac_addr_t*, size_t, int);

/* we_arp_filter.c */

/*!
 * ARP policy: Also located in fw and userspace.
 */
typedef enum {
   ARP_HANDLE_MYIP_FORWARD_REST = 0, /* FW handles my ip, forwards rest to host.  */
   ARP_HANDLE_MYIP_FORWARD_NONE = 1, /* Nothing is forwarded to host.             */
   ARP_HANDLE_NONE_FORWARD_MYIP = 2, /* Only forward my ip to host.               */
   ARP_HANDLE_NONE_FORWARD_ALL  = 3  /* Forward all arp requests to host.         */
} arp_policy_t;

#define ARP_FILTER_UNKNOWN   0
#define ARP_FILTER_STATIC    1
#define ARP_FILTER_DYNAMIC   2
#define ARP_FILTER_AUTOCONF  3

int WiFiEngine_ARPFilterAdd (ip_addr_t, unsigned int);
int WiFiEngine_ARPFilterRemove (ip_addr_t);
int WiFiEngine_ARPFilterClear (void);
int WiFiEngine_ARPFilterEnable (void);
int WiFiEngine_ARPFilterDisable (void);
int WiFiEngine_ConfigARPFilter (arp_policy_t, ip_addr_t);

       /* Not yet implemented */
int   WiFiEngine_GetTxPowerLevel(int *ofdm_level, int *qpsk_level);
int   WiFiEngine_SetTxPowerLevel(int ofdm_level, int qpsk_level);
#if (DE_CCX == CFG_INCLUDED)
int   WiFiEngine_SetTxPowerLevel_from_cpl_ie(int qpsk_level, int ofdm_level);
int   WiFiEngine_SendAddtsReq(uint8_t identifier);
int   WiFiEngine_SendDelts(uint8_t identifier);
#endif
int   WiFiEngine_GetNumberOfAntennas(unsigned long *count);
int   WiFiEngine_GetRxAntenna(unsigned long *n);
int   WiFiEngine_GetTxAntenna(unsigned long *n);
int   WiFiEngine_GetStatistics(WiFiEngine_stats_t* stats, int update);

/* Encryption/Authentication (we_auth.c) */

int   WiFiEngine_AddKey(uint32_t key_idx, size_t key_len, const void *key,
                        m80211_key_type_t key_type, m80211_protect_type_t prot,
                        int authenticator_configured, m80211_mac_addr_t *bssid,
                        receive_seq_cnt_t *rsc, int tx_key);
int   WiFiEngine_DeleteKey(uint32_t key_idx, m80211_key_type_t key_type,
                           m80211_mac_addr_t *bssid);
int   WiFiEngine_DeleteGroupKey(int key_idx);
int   WiFiEngine_DeleteAllKeys(void);
int   WiFiEngine_SetDefaultKeyIndex(int8_t index);
int   WiFiEngine_GetDefaultKeyIndex(int8_t *index);
int   WiFiEngine_SetExcludeUnencryptedFlag(int i);
int   WiFiEngine_GetExcludeUnencryptedFlag(int *i);
int   WiFiEngine_SetProtectedFrameBit(uint8_t mode);
int   WiFiEngine_GetProtectedFrameBit(uint8_t *mode);
int   WiFiEngine_SetRSNAEnable(uint8_t mode);
int   WiFiEngine_SetRSNProtection(m80211_mac_addr_t *bssid, m80211_protect_type_t prot,
                                  m80211_key_type_t key_type);
int   WiFiEngine_8021xPortOpen(void);
int   WiFiEngine_8021xPortClose(void);
#define WiFiEngine_Is8021xPortClosed() (!WES_TEST_FLAG(WES_FLAG_8021X_PORT_OPEN))
int   WiFiEngine_IsTxKeyPresent(void);
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
int   WiFiEngine_PMKID_Add(const m80211_mac_addr_t *bssid, 
                           const m80211_pmkid_value *pmkid);
int   WiFiEngine_PMKID_Remove(const m80211_mac_addr_t *bssid, 
                              const m80211_pmkid_value *pmkid);
int   WiFiEngine_PMKID_Clear(void);
unsigned int WiFiEngine_PMKID_Get(m80211_bssid_info *pmkid, 
                                  unsigned int npmkid, 
                                  const m80211_mac_addr_t *bssid);
int   WiFiEngine_PMKID_Set(const m80211_bssid_info *pmkid, 
                           unsigned int npmkid);
#endif
int   WiFiEngine_GetEncryptionMode(WiFiEngine_Encryption_t* encryptionMode);
int   WiFiEngine_SetEncryptionMode(WiFiEngine_Encryption_t  encryptionMode);
int   WiFiEngine_GetAuthenticationMode(WiFiEngine_Auth_t* authenticationMode);
int   WiFiEngine_SetAuthenticationMode(WiFiEngine_Auth_t  authenticationMode);
int   WiFiEngine_HandleMICFailure(m80211_michael_mic_failure_ind_descriptor_t *ind_p);
int   WiFiEngine_GetKey(uint32_t key_idx, char* key, size_t* key_len);

/* Nanaradio-specific properties (we_nr.c) */
#define WiFiEngine_isCoredumpEnabled() (wifiEngineState.core_dump_state == WEI_CORE_DUMP_ENABLED)
void  WiFiEngine_Init_Complete(void);
int   WiFiEngine_isMicCounterMeasureStarted(void);
int   WiFiEngine_ActivateRegionChannels(void);
int   WiFiEngine_SetLvl1AdaptiveTxRate(uint8_t mode);
int   WiFiEngine_SetHmgMode(WiFiEngine_HmgMode_t mode);
int   WiFiEngine_SetPSTrafficTimeout(uint32_t timeout);
int   WiFiEngine_SetPSWMMPeriod(uint32_t timeout);
int   WiFiEngine_SetPSPollPeriod(uint32_t timeout);
int   WiFiEngine_SetMibListenInterval(uint16_t interval);
int   WiFiEngine_SetReceiveAllDTIM(bool_t all_dtim);
int   WiFiEngine_SetUsePsPoll(bool_t use_ps_poll);
int   WiFiEngine_SetDtimBeaconSkipping(uint32_t skip);
int   WiFiEngine_LinksupervisionTimeout(uint32_t link_to, uint32_t null_to);
int   WiFiEngine_SetCompatibilityMask(uint16_t capability);
int   WiFiEngine_GetListenInterval(uint16_t *interval);
int   WiFiEngine_GetDelayStartOfPs(uint16_t *delay);
int   WiFiEngine_SetListenInterval(uint16_t interval);
int   WiFiEngine_SetReceiveDTIM(unsigned int enabled);
int   WiFiEngine_GetSdioClock(int *enable20MHzClock);
int   WiFiEngine_GetBootTargetFlag(int *flag);
int   WiFiEngine_GetDelayAfterReset(unsigned long *delay);
int   WiFiEngine_EnableLinkSupervision(int enable);
int   WiFiEngine_SetLinkSupervisionBeaconFailCount(unsigned int beacons);
int   WiFiEngine_SetLinkSupervisionBeaconWarningCount(unsigned int beacons);
int   WiFiEngine_SetLinkSupervisionBeaconTimeout(unsigned int timeout);
int   WiFiEngine_SetRegistryTxFailureCount(unsigned int count);
int   WiFiEngine_SetLinkSupervisionTxFailureCount(unsigned int count);
int   WiFiEngine_SetLinkSupervisionRoundtripCount(unsigned int count);
int   WiFiEngine_SetLinkSupervisionRoundtripSilent(unsigned int intervals);

#define HIC_CTRL_HOST_ATTENTION_GPIO 0
#define HIC_CTRL_HOST_ATTENTION_SDIO 1

int   WiFiEngine_SetMsgSizeAlignment(uint16_t, uint16_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
int   WiFiEngine_CommitPDUSizeAlignment(void);
int   WiFiEngine_GetReleaseInfo(char *dst, size_t len);
int   WiFiEngine_GetFirmwareReleaseInfo(char *dst, size_t len);
int   WiFiEngine_GetReadHistory(char *outbuf, int offset, size_t *outlen);
int   WiFiEngine_SetMultiDomainCapability(uint8_t mode);
int   WiFiEngine_SetBackgroundScanMode(uint8_t mode);
int   WiFiEngine_ConfigureBackgroundScan(uint16_t scan_period, uint16_t probe_delay,
                                         uint16_t min_channel_time, uint16_t max_channel_time,
                                         uint8_t *channel_list, int channel_list_len,
                                         m80211_ie_ssid_t *ssid);
int   WiFiEngine_GetNetIEs(char *dst, size_t *len, m80211_ie_ssid_t *ssid,
                           m80211_mac_addr_t *bssid);
int WiFiEngine_ConfigUDPBroadcastFilter(uint32_t bitmask);
int WiFiEngine_EnableBTCoex(bool_t enable);
int WiFiEngine_ConfigBTCoex(uint8_t bt_vendor,
                            uint8_t pta_mode,
                            uint8_t *pta_def,
                            int len,
                            uint8_t antenna_dual,
                            uint8_t antenna_sel0,
                            uint8_t antenna_sel1,
                            uint8_t antenna_level0,
                            uint8_t antenna_level1);
int WiFiEngine_SetAntennaDiversityMode(uint32_t antenna_mode, int32_t rssi_threshold);
int WiFiEngine_IsAssocWMM(void);
int WiFiEngine_SetTxPktWindow(uint8_t win_size);
int WiFiEngine_TxPktWindowIsFull(void);

#ifdef USE_IF_REINIT
int WiFiEngine_Quiesce(void);
int WiFiEngine_UnQuiesce(void);
int WiFiEngine_RegisterInactivityCallback(i_func_t cb);
int WiFiEngine_DeregisterInactivityCallback(void);
#endif

#ifdef USE_IF_REINIT
int WiFiEngine_SetActivityTimeout(uint32_t timeout, uint32_t inact_check_interval);
#endif

void WiFiEngine_Registry_LoadDefault(void);
void WiFiEngine_Registry_Read(char *registry_cache);
void WiFiEngine_Registry_Write(char* registry_cache);
#if 0
//deprecated by we_ind.c
int WiFiEngine_RegisterIndHandler(we_indication_t, we_ind_cb_t, const void*);
int WiFiEngine_DeregisterIndHandler(we_indication_t, we_ind_cb_t, const void*);
int WiFiEngine_IsIndHandlerRegistered(we_indication_t type, we_ind_cb_t cb);
#endif

/* Used in core dump mode (we_dump.c) */

#define WiFiEngine_CommandTimeoutStart()                        \
    while(!WES_TEST_FLAG(WES_FLAG_CMD_TIMEOUT_RUNNING)) {   \
           WiFiEngine__CommandTimeoutStart();                   \
           break;                                               \
        }

#define WiFiEngine_CommandTimeoutStop()                         \
    while(WES_TEST_FLAG(WES_FLAG_CMD_TIMEOUT_RUNNING)) {    \
           WiFiEngine__CommandTimeoutStop();                    \
           break;                                               \
        }

void WiFiEngine__CommandTimeoutStart(void);
void WiFiEngine__CommandTimeoutStop(void);


int    WiFiEngine_StartCoredump(void);
int    WiFiEngine_RequestCoredump(void);
int    WiFiEngine_RequestSuicide(void);
void   WiFiEngine_HandleCoreDumpPkt(char* pkt);

/* Debugging (we_dbg.c) */
int   WiFiEngine_GetState(void);
int   WiFiEngine_GetSubState(void);
int   WiFiEngine_GetCmdReplyPendingFlag(void);
int   WiFiEngine_GetDataReplyPendingFlag(void);
int   WiFiEngine_GetDataReplyWinSize(void);
int   WiFiEngine_GetDataRequestByAccess(unsigned int*, unsigned int*, unsigned int*, unsigned int*);
int   WiFiEngine_Tickle(void);

/* Convenience functions (we_chan.c) */
int   WiFiEngine_Channel2Frequency(uint8_t, unsigned long*);
int   WiFiEngine_Frequency2Channel(unsigned long, uint8_t*);

/* Convenience functions (we_rate.c) */
uint32_t                 WiFiEngine_rate_native2bps(we_xmit_rate_t rate);
we_xmit_rate_t          WiFiEngine_rate_bps2native(uint32_t rate);
uint8_t               WiFiEngine_rate_native2ieee(we_xmit_rate_t rate);
we_xmit_rate_t          WiFiEngine_rate_ieee2native(uint8_t rate);

int                     WiFiEngine_RateCodeToBitposition(unsigned int,
                                                         unsigned int,
                                                         unsigned int*);
#define WE_RATE_DOMAIN_BG  0U
#define WE_RATE_DOMAIN_HT  1U

int WiFiEngine_SetEnabledHTRates(uint32_t);
int WiFiEngine_GetEnabledHTRates(uint32_t*);
int WiFiEngine_GetRateIndexLinkspeed(uint32_t, uint32_t*);

/* HIC tunnelling stuff (we_swm500.c) */
#define WE_SWM500_MIBGET    (1 << 0)
#define WE_SWM500_MIBSET    (1 << 1)
#define WE_SWM500_CONSOLE   (1 << 2)
#define WE_SWM500_FLASH     (1 << 3)
int WiFiEngine_SWM500_Command(unsigned int, const void*, size_t, uint32_t*);
int WiFiEngine_SWM500_Reply(uint32_t, void*, size_t*);
int WiFiEngine_SWM500_Command_Multi(unsigned int, const void**, size_t*, uint32_t*, int*);
int WiFiEngine_SWM500_Command_All(unsigned int, const void*, size_t);

/* Callback completion interface (we_cb.c) */

void WiFiEngine_DispatchCallbacks(void);
void WiFiEngine_ScheduleCallback(we_cb_container_t *cbc);
int WiFiEngine_CancelCallback(we_cb_container_t *cbc);
int WiFiEngine_RunCallback(we_cb_container_t *cbc);
we_cb_container_t *WiFiEngine_BuildCBC(we_callback_t cb, void *ctx, size_t ctx_len, int repeating);
void WiFiEngine_FreeCBC(we_cb_container_t *cbc);

/* Roaming agent (we_roam.c) */
int WiFiEngine_roam_enable(bool_t enable);
int WiFiEngine_roam_enable_periodic_scan(bool_t enable);
int WiFiEngine_roam_measure_on_data(bool_t enable);
int WiFiEngine_is_roaming_in_progress(void);
int WiFiEngine_roam_configure_filter(bool_t enable_blacklist,
                                     bool_t enable_wmm,
                                     bool_t enable_ssid);
int WiFiEngine_roam_add_ssid_filter(m80211_ie_ssid_t ssid);
int WiFiEngine_roam_del_ssid_filter(m80211_ie_ssid_t ssid);
int WiFiEngine_roam_configure_rssi_thr(bool_t enable, int32_t roam_thr,
                                       int32_t scan_thr, uint32_t margin);
int WiFiEngine_roam_configure_snr_thr(bool_t enable, uint32_t roam_thr,
                                      uint32_t scan_thr, uint32_t margin);
int WiFiEngine_roam_configure_rate_thr(bool_t enable,
                                       m80211_std_rate_encoding_t roam_thr,
                                       m80211_std_rate_encoding_t scan_thr);
int WiFiEngine_roam_configure_ds_thr(bool_t enable, uint32_t roam_thr,
                                     uint32_t scan_thr);
int WiFiEngine_roam_configure_net_election(uint32_t k1, uint32_t k2);
bool_t WiFiEngine_is_roaming_enabled(void);
int WiFiEngine_roam_conf_auth(bool_t enable,
                              WiFiEngine_Auth_t auth_mode,
                              WiFiEngine_Encryption_t enc_mode);
we_roaming_reason_t WiFiEngine_GetRoamingReason(void);
int WiFiEngine_GetRxRate(m80211_std_rate_encoding_t *rate);
int WiFiEngine_GetTxRate(m80211_std_rate_encoding_t *rate);
int WiFiEngine_EnableRxRateMon(int32_t                    *thr_id,
                               uint32_t                   supv_interval,
                               m80211_std_rate_encoding_t thr_level,
                               uint8_t                    dir, /* rising/falling */
                               we_ind_cb_t                cb);
int WiFiEngine_EnableTxRateMon(int32_t                    *thr_id,
                               uint32_t                   supv_interval,
                               m80211_std_rate_encoding_t thr_level,
                               uint8_t                    dir, /* rising/falling */
                               we_ind_cb_t                cb);
int WiFiEngine_DisableRxRateMon(int32_t thr_id);
int WiFiEngine_DisableTxRateMon(int32_t thr_id);
void WiFiEngine_RateMonInit(void);
void WiFiEngine_RateMonShutdown(void);

void WiFiEngine_get_last_con_lost_reason(we_con_lost_s *dst);
void WiFiEngine_get_last_con_failed_reason(we_con_failed_s *dst);

int WiFiEngine_configure_delay_spread(uint32_t thr, uint16_t winsize);

/* BT 3.0 AMP support (we_pal.c) */
/* Used by process that has communication interface to BT stack to exchange HCI protocol */
int WiFiEngine_PAL_RegisterHCIInterface (net_rx_cb_t HCI_response_fn);
int WiFiEngine_PAL_HCIRequest(char* data_buf, int size);


/* Multi net Support (we_multi_net.c) */
int WiFiEngine_Transport_Reset(mac_api_net_id_t net_id, bool_t reset_target);
int WiFiEngine_Transport_Flush(mac_api_net_id_t net_id);
int WiFiEngine_Transport_Send(mac_api_net_id_t net_id, void* m802_1_data, int size, uint16_t vlanid_prio, uint32_t *trans_id_p);
int WiFiEngine_Transport_RegisterDataPath(mac_api_net_id_t net_id, net_rx_cb_t rx_handler, net_rx_cb_t cfm_handler);
int WiFiEngine_Supplicant_Set_PSK(mac_api_net_id_t net_id, char* pmk, int size);
int WiFiEngine_NetSetSSID(mac_api_net_id_t net_id, char *ssid, int size);
int WiFiEngine_NetStartBeacon(mac_api_net_id_t net_id);
int WiFiEngine_NetStopBeacon(mac_api_net_id_t net_id);
int WiFiEngine_NetConnect(mac_api_net_id_t net_id);
int WiFiEngine_NetDisconnect(mac_api_net_id_t net_id);


#endif /* WIFI_STATES_ONLY */

/* only used on linux */
/* Must match nrx_conn_lost_data in libnrx */
/* TODO: move to linux driver or update libnrx to new structures */

typedef enum
{
   WE_CONN_LOST_AUTH_FAIL,
   WE_CONN_LOST_ASSOC_FAIL,
   WE_CONN_LOST_DEAUTH,
   WE_CONN_LOST_DEAUTH_IND,
   WE_CONN_LOST_DISASS,
   WE_CONN_LOST_DISASS_IND
} we_conn_lost_type_t;

typedef struct {
      we_conn_lost_type_t type; /**< Identify the type of connection lost event */
      m80211_mac_addr_t   bssid; /**< BSSID of the AP/STA with which connection
                                  * was lost/refused. */
      uint8_t reason_code;          /**< Failure code from the "disconnect" message.
                                 * its meaning is defined in the 802.11 standard
                                 * for the respective message (see the type field
                                 * to figure out which one it was. */
} we_conn_lost_ind_t;

/* not sure if this is the right place for this ; needed by wifiengine internals and by the supplicant if on embOS */
typedef struct
{
   uint16_t        type;
   uint16_t        len;
}m80211_tlv_t;
int m80211_tlv_pars_next( uint8_t *buf, size_t len, size_t *start, m80211_tlv_t *tlv, void **tlv_data);
int m80211_tlv_find( uint8_t *buf, size_t len, uint16_t tlv_type, m80211_tlv_t *tlv, void **tlv_data);
#ifdef WPS_REMOVE_CONFIGURED_BIT
int m80211_ie_remove_wps_configured(m80211_ie_wps_parameter_set_t *ie);
int we_net_wps_remove_configured(WiFiEngine_net_t* net);
#endif
int we_filter_out_wps_configured(WiFiEngine_net_t* net);
/* default filters */

int we_filter_out_net(WiFiEngine_net_t* net);

int WiFiEngine_LoadWLANParameters(const void *param, size_t size);
int WiFiEngine_SendMIBSetFromNvmem(const void *inbuf, size_t len);

int WiFiEngine_SyncRequest(void);

#if (DE_CCX == CFG_INCLUDED)
int   WiFiEngine_GetLastAssocInfo(char *ssid, uint16_t* ssid_len, char *bssid, uint16_t* channel, uint16_t* msecs);
int   WiFiEngine_GetCurrentCPL(uint8_t* cpl);
int   WiFiEngine_SendAdjacentAPreport(void);
int   WiFiEngine_SendTrafficStreamMetricsReport(void);
int   WiFiEngine_GetTSPECbody(char* tspec_body);
int   WiFiEngine_is_in_joining_state(void);
int   WiFiEngine_store_cpl_state(int enabled, int value);
int   WiFiEngine_is_dss_net(WiFiEngine_net_t* net);
int   WiFiEngine_Set_CCKM_Key(char* key, size_t key_len);

#define DSSS_MAX_LVL 17
#define OFDM_MAX_LVL 14
#endif //CCX

#endif /* WIFI_ENGINE_H */
