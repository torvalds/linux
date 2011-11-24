
#ifndef WIFI_ENGINE_INTERNAL_H
#define WIFI_ENGINE_INTERNAL_H
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
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
This module implements the support functions internal to WiFiEngine.

*****************************************************************************/

#include "driverenv.h"
#include "registry.h"
#include "wei_list.h"
#include "wei_netlist.h"
#include "wifi_engine.h"
#include "m80211_defs.h"
#include "mac_api.h"
#include "mib_defs.h"

/*#define SCANTEST*/

typedef struct {
      union
      {
            m80211_mac_data_ind_header_t   mac_data_ind;
            m80211_mac_data_cfm_t   mac_data_cfm;
      } mac_data_body;
      
} mac_data_body_t;

typedef union
{
      mac_data_body_t               mac_data;
      mlme_mgmt_body_t              mac_mgmt;
      mac_mib_body_t                mac_mib;
      hic_ctrl_msg_body_t           mac_ctrl;    
} mac_msg_t;

#ifdef WITH_LOST_SCAN_WORKAROUND
#define LOST_SCAN_WAIT_PERIOD 5000
#endif
/* Interval in MS (kTU's) between channel changes in scan */
#define DEFAULT_CHANNEL_INTERVAL 100
/* FIFO */
#define QUEUE_SIZE 100

struct queue_s {
        void *v[QUEUE_SIZE];
        int   s[QUEUE_SIZE];
        int head;
        int tail;
};

extern struct queue_s cmd_queue; /* Queue of unsent commands   */

#define QUEUE_FULL(q) ((((q)->head + 1) % QUEUE_SIZE) == (q)->tail)
#define QUEUE_EMPTY(q) ( (q)->head == (q)->tail)

void init_queue(struct queue_s *q);

int enqueue(struct queue_s *q, void *el, int s);

void *dequeue(struct queue_s *q, int* s);

/* Doubly linked list */
#define LIST_SIZE 16

#define WE_CHECK(x) do {if(!(x)) return WIFI_ENGINE_FAILURE_INVALID_DATA;} while (0)

/* Sequence numbers for keeping track of MIB-request-confirm pairs */
typedef int main_states_t;
enum {
   driverIdle,
   driverDisconnected,
   driverJoining,
   driverStarting,
   driverAuthenticating,
   driverAssociating,
   driverAssociated,   
   driverConnected,
   driverDisassociating,
   driverDeauthenticating,
   driverDisconnecting
};


typedef int traffic_mode_t;
enum { 
   TRAFFIC_AUTO_CONNECT,
   TRAFFIC_TRANSPARENT 
};
typedef int ps_main_states_t;
enum { 
   PS_MAIN_INIT,
   PS_MAIN_DISABLED_UNCONNECTED,
   PS_MAIN_DISABLED_CONNECTED,      
   PS_MAIN_ENABLED_UNCONNECTED,
   PS_MAIN_ENABLED_CONNECTED  
};


typedef int connected_states_t;
enum {
   driverBSS,
   driverIBSS
};

/* Specifies the source of information */
typedef enum
{
   WEI_IE_TYPE_CAP, /* Capabilities (not really a IE) */
   WEI_IE_TYPE_WPA,
   WEI_IE_TYPE_RSN,
   WEI_IE_TYPE_WAPI
} wei_ie_type_t;
typedef enum
{
   WEI_CORE_DUMP_DISABLED,
   WEI_CORE_DUMP_ENABLED
} wifi_core_dump_state_t;

/* Dynamic driver configuration */
/*! 
 * About the encryption configuration:
 * The encryptionLimit specifies the highest encryption mode allowed in the driver.
 * The encryptionMode specifies the currently configured encryption mode.
 *
 */ 
typedef struct
{
   WiFiEngine_Encryption_t encryptionLimit;    /**< The highest allowed encryption mode */
   WiFiEngine_Encryption_t encryptionMode;     /**< The current encryption mode */
   WiFiEngine_Auth_t       authenticationMode; /**< The required authentication mode. */ 
   uint16_t                min_pdu_size;       /**< Minimum PDU size to be indicated by the device */
   uint16_t                pdu_size_alignment; /**< PDU size alignment from target */
   uint8_t                 rx_hic_hdr_size;    /**< HIC header size for RX data */
   uint8_t                 tx_hic_hdr_size;    /**< HIC header size for TX data */
   uint8_t                 host_attention;     /**< interrupt mode */
   uint8_t                 byte_swap_mode;     /**< Enable/disable byte swapping to support 8bit and 16bit SPI transfers */
   uint8_t                 host_wakeup;        /**< Set host wakeup interrupt pin, set to 0xFF to disable host wakeup */ 
   uint8_t                 force_interval;     /**< Force an interval between HIC messages from target [unit 1/10th msec] */
   uint8_t                 tx_window_size;     /**< Set tx_windows_size, six is default on linux and windows */
} wifi_config_t;

typedef struct
{
      int                       used;           /**< Is this state in use? */
      m80211_mac_addr_t         bssid;
      m80211_protect_type_t     prot;
      m80211_key_type_t         key_type;
      WiFiEngine_Encryption_t   key_enc_type;
      int                       dot11PrivacyInvoked;
} wifi_key_state_t;

typedef enum
{
   WE_CHIP_TYPE_MODULE,
   WE_CHIP_TYPE_COB,
   WE_USE_HTOL
}wifi_chip_type_t;

typedef int data_path_states_t;
enum 
{
   DATA_PATH_OPENED,
   DATA_PATH_CLOSED
};

/*! WiFiEngine state flags */
typedef uint32_t wifi_state_flags;
#define WES_FLAG_SCAN_IN_PROGRESS            1    /**< Waiting for scan results */
#define WES_FLAG_HW_PRESENT                  1<<1 /**< 0 if no hardware is present, 1 if hardware is present */
#define WES_FLAG_HW_NO_TRAFFIC_RCVD          1<<2 
#define WES_FLAG_8021X_PORT_OPEN             1<<4 /**< 802.1X port state (open or closed) */
#define WES_FLAG_MIC_FAILURE_RCVD            1<<5 /**< Michael MIC failure detected within the last 60 seconds */
#define WES_FLAG_ASSOC_BLOCKED               1<<6 /**< Association not allowed */

/* WES_FLAG_HEARTBEAT deprecated, use hb_rx_ts instead to get more accurate
 * timing.
 */

/*
 * WES_FLAG_PERMANENT_GROUP_KEYS was removed since we no longer delete keys
 * at deauthentication.
 */

#define WES_DEVICE_CONFIGURED                1<<7 /**< Set after device is configured. Used to during fw crash recovery */
#define WES_FLAG_SOFT_SHUTDOWN               1<<8 /**< Set if soft shutdowmn is activated */
#define WES_FLAG_PERIODIC_SCAN_ENABLED       1<<9 /**< Periodic scan in enabled */
#define WES_FLAG_WMM_ASSOC                   1<<10 /**< Current association is WMM */
#define WES_FLAG_COUNTRY_INFO_CHANGED        1<<11 /**< We need to update country in target info */
#define WES_FLAG_RAW_MODE_UNPLUGGED          1<<12 /**< Indicates if card in unplugged */
#define WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING 1<<13 /**< Set if delayed power save timer is running  */
#define WES_FLAG_CMD_TIMEOUT_RUNNING         1<<14  /**< Set if command timeout timer is running  */
#define WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING  1<<15  /**< Set if traffic timeout timer is running  */
#define WES_FLAG_QOS_ASSOC                   1<<16 /**< Current association uses QoS (either WMM or 11e) */


#define WES_SET_FLAG(x)   (wifiEngineState.flags |= (x))
#define WES_CLEAR_FLAG(x) (wifiEngineState.flags &= (~(x)))
#define WES_TEST_FLAG(x)  ((wifiEngineState.flags & (x)) != 0)

#define X_MAC_VERSION_SIZE 128

/* MIB triggers */
struct virtual_mib_trigger;

#if (DE_CCX == CFG_INCLUDED)
typedef struct
{
   m80211_mac_addr_t   MacAddress;
   uint16_t      ChannelNumber;
   uint16_t      ssid_len; 
   char          ssid[32];
   uint16_t      Disassociation_Timestamp;   
}Last_Associated_Net_Info_t;

typedef struct
{
   uint8_t  dialog_token;
   uint8_t  admission_state;
   uint8_t  active;  
   char     TSPEC_body[55]; 
   char     tsm_state;
   uint16_t    tsm_interval;
}Admission_Control_State_t;

enum ADDTS_STATUS {
   ADDTS_NOT_TRIED,
   ADDTS_REFUSED_RETRY,
   ADDTS_ACCEPTED,
   ADDTS_REFUSED_DO_NOT_RETRY_UNTIL_ROAMING
};
typedef struct trans_ts_s
{
    uint32_t          transid;
    uint32_t          start_ts;
    struct trans_ts_s*next;
}trans_ts_t;

typedef struct
{
    //char        uplink_packet_queue_delay_histogram[8];
    uint32_t    driver_pkt_cnt;
    uint32_t    driver_pkt_delay_total;
    uint16_t    uplink_packet_queue_delay_histogram[4];
    uint32_t    uplink_packet_transmit_media_delay;
    uint16_t    lastFwPktLoss;
    uint16_t    lastFwPktCnt;
    uint8_t     roaming_count;
    uint16_t    roaming_delay;
    trans_ts_t* trans_ts_list;
    bool_t      collect_metrics_enabled;
}ccx_metrics_t;

typedef struct
{
   int   SendAdjReq;
   int  scan_job_id;
   char scan_mode;
   char channel;
   uint16_t duration;
   char dialog_token[2];
   char measurement_token[2];
   driver_timer_id_t ccx_radio_measurement_timer_id;
   driver_timer_id_t ccx_traffic_stream_metrics_id;
   Last_Associated_Net_Info_t LastAssociatedNetInfo; 
   Admission_Control_State_t addts_state[8];
   int cpl_enabled;
   int cpl_value;
   int cpl_tx_value_dsss;
   int cpl_tx_value_ofdm;
   int man_tx_value_dsss;
   int man_tx_value_ofdm;
   int         KRK_set;
   char     KRK[16];
   uint32_t    request_number;
   uint8_t          resend_packet_mask;
   ccx_metrics_t    metrics;    /**< variables for Metrics collection */
   uint16_t         rac;        /**< Required Admission Capacity */
  //Traffic Stream Measurements related
   struct iobject *connect_handler;
}CCX_State_t;
#endif //DE_CCX


/*! \file wifi_engine_internal.h
 * \brief Container for the dynamic WiFiEngine state */
typedef struct
{
   uint8_t                dataReqPending;                  /**< Pending data req counter     */   
   uint8_t                dataReqByPrio[8];                /**< Pending data req counter by prio */   
   data_path_states_t     dataPathState;                   /**< Used when power save is enabled to open/close data path */
   uint8_t                txPktWindowMax;                  /**< Max size of the tx pkt window*/
   uint8_t                cmdReplyPending;                 /**< Cmd reply pending flag */
   int                    last_sent_msg_id;                /**< Last sent command */
   int                    last_sent_msg_type;              /**< Message type of last sent command*/
   driver_lock_t          lock;                            /**< WiFiEngine master lock */
   driver_lock_t          rlock;                           /**< WiFiEngine registry lock */
   driver_trylock_t       sm_lock;                         /**< WiFiEngine state machine lock */
   driver_trylock_t       send_lock;                       /**< WiFiEngine send process lock */
   driver_trylock_t       cmd_lock;                       /**< WiFiEngine send process lock */
   driver_lock_t          resource_hic_lock;               /**< WiFiEngine hic interface release lock */   
   uint32_t               flags;                           /**< Configuration flags */
   uint32_t               pkt_cnt;                         /**< Used as transaction ID for data. Only the lower 31 bits are used, the high bit is reserved. */
   uint32_t               last_eapol_trans_id;             /**< Transid for last sent EAPOL frame */
   uint32_t               prof_event_flags;                /**< Event flags for the hw profiler */
   ps_main_states_t       psMainState;                     /**< Power save main states */
   
   /* Sequence numbers for keeping track of MIB-request-confirm pairs */
   uint16_t               current_seq_num;    
   uint16_t               last_seq_num;
   main_states_t          main_state;                      /**< Operation in progress */
   connected_states_t     connected_state;                 /**< Either of BSS or IBSS */

   /* State cache for selected MIB params */
   uint8_t                excludeUnencryptedFlag;          /**< 0 if flag is disabled, 1 if enabled */
   uint16_t               frag_thres;
   mlme_rssi_dbm_t        snr;                             /**< The last fetched RSSI value (measured on beacons, may be old) */
   mlme_rssi_dbm_t        data_snr;                        /**< The last fetched RSSI value (measured on data-frames, may be old) */
   mlme_rssi_dbm_t        rssi;                            /**< The last fetched RSSI value (measured on beacons, may be old) */
   struct iobject*        rssi_beacon_h; 
   mlme_rssi_dbm_t        data_rssi;                       /**< The last fetched RSSI value (measured on data-frames, may be old) */
   struct iobject*        rssi_data_h; 
   uint16_t               rts_thres;                       /**< Current RTS threshold  */
  struct {
      uint8_t qpsk_index;
      uint8_t ofdm_index;
   } power_index;                                          /**< Power index struct */

   we_xmit_rate_t         current_tx_rate;                    /**< The last rate received in a data cfm message */
   we_xmit_rate_t         current_rx_rate;
   void                   *adapter;                        /**< Handle that identifies the device */
   WiFiEngine_stats_t     link_stats;                      /**< Link statistics */
   driver_timer_id_t      mic_cm_detect_timer_id;          /**< Id of mic countermeasures detect timer */
   driver_timer_id_t      mic_cm_assoc_holdoff_timer_id;   /**< Id of mic countermeasures hold off timer */
   driver_timer_id_t      monitor_traffic_timer_id;        /**< Id of monitor traffic timer (used in power save forest mode) */
#ifdef USE_IF_REINIT 
   driver_timer_id_t      inactivity_timer_id;             /**< Id of driver inactivity timer */
#endif
   driver_timer_id_t      cmd_timer_id;                    /**< Id of command timeout timer */
   driver_timer_id_t      ps_traffic_timeout_timer_id;     /**< Id of command timeout timer */

   driver_tick_t          activity_ts;                     /**< Timestamp of last activity. Used to detect driver inactivity. */
   driver_tick_t          cmd_tx_ts;                       /**< Timestamp of last sent command. Used to detect a hung firmware. */
   uint16_t               forceRestart;                    /**< Used to force a restart (command queue full). */

   driver_tick_t          hb_rx_ts;                        /**< Timestamp of last received heartbeat ind. Used to detect a hung firmware. */
   uint32_t               periodic_scan_interval;          /**< Interval between periodic scans */
   uint16_t               ps_queue_cnt;                    /**< Total number of data packets in queue (from upper layer) */  
   uint16_t               ps_data_ind_received;            /**< Set if a data ind is recieved - cleared if when no activity on tx data path*/  
   WiFiEngine_PowerMode_t power_mode;                      /**< Temporary holds power mode to change to */
   int                    is80211PowerSaveInhibit;         /**< Set to inhibit power save during critical sequences */
   int                    ps_inhibit_state;                /**< Bitmask used when inhibiy or allow power save */  
   int                    users;                           /**< Bitmask used when request/release hic interface */   
                                                        
   wei_netlist_state_t    net_state;      
   wifi_config_t          config;                          /**< Dynamic driver configuration */
   wifi_key_state_t       key_state;                       /**< Encryption key params */
   wifi_core_dump_state_t core_dump_state;                 /**< If core dump is enabled or not */
   char                   x_mac_version[X_MAC_VERSION_SIZE];  /**< ASCII-coded x_mac version */ 
   uint32_t               fw_capabilities;                 /**< Firmware capabilities */ 
   m80211_ie_country_t*   active_channels_ref;             /**< Channels allowed to send on with 
                                                             802.11d. Used to limit active scans. */
   wifi_chip_type_t       chip_type;                       /**< chip type either module or chip on board */
   char                   fpga_version[4];                 /**< Version of fpga in a null terminated ascii string */
#ifdef USE_NEW_AGE
   uint32_t               scan_count;                      /**< Ageing timer based on scan count */
#endif
   int                    vir_mib_trig_count;              /**< Last trigger number */
   WEI_TQ_HEAD(, virtual_mib_trigger) vir_trig_head;       /**< Linked list with virtual triggers */
   driver_mutex_t         trig_sem;                        /**< Mutex for blocking access to linked list */
   driver_msec_t          driver_start_ts;                 /**< Driver start timestamp */
   int                    (*eapol_handler)(const void*, size_t); /**< Handler for EAPOL frames */
   void*                  cm_priv;
   void*                  pm_priv;
   
   we_con_failed_s        last_con_failed_ind;
   we_con_lost_s          last_con_lost_ind;
   we_ps_control_t        *delay_ps_uid;
   we_ps_control_t        *dhcp_ps_uid;
   we_ps_control_t        *ibss_ps_uid;
   we_ps_control_t        *ps_state_machine_uid;   
   
   /* rate table that maps firmware bit fields, to IEEE rate codes */
   uint32_t               rate_table[32];
   unsigned int           rate_table_len;
   uint32_t               rate_bmask;  /* B rates as mask */
   uint32_t               rate_gmask;  /* G rates as mask */   
#if (DE_ENABLE_HT_RATES == CFG_ON)
   uint32_t               rate_htmask; /* supported HT rate mask */
   uint32_t               enabled_ht_rates;
#endif
#if (DE_CCX == CFG_INCLUDED)
   CCX_State_t      ccxState;
#endif //DE_CCX
} WiFiEngineState_t;
extern WiFiEngineState_t wifiEngineState;

#define WE_SUPPORTED_HTRATES() (wifiEngineState.rate_htmask & wifiEngineState.enabled_ht_rates)

/* access macros for the rate table */
#define WE_RATE_DOMAIN_SHIFT   30
#define WE_RATE_DOMAIN_MASK    0x03
#define WE_RATE_BITPOS_SHIFT   8
#define WE_RATE_BITPOS_MASK    0x1f
#define WE_RATE_CODE_SHIFT     0
#define WE_RATE_CODE_MASK      0xff
#define WE_RATE_ENTRY(D, B, R)                  \
   (((D) << WE_RATE_DOMAIN_SHIFT)               \
    | ((M80211_XMIT_RATE_##B##MBIT) << WE_RATE_BITPOS_SHIFT)      \
    | ((R) << WE_RATE_CODE_SHIFT))
#define WE_RATE_ENTRY_BG(B, N) WE_RATE_ENTRY(WE_RATE_DOMAIN_BG, B, (N))
#define WE_RATE_ENTRY_HT(B, N) WE_RATE_ENTRY(WE_RATE_DOMAIN_HT, B, (N))

#define WE_RATE_ENTRY_FIELD(E, N) (((E) >> WE_RATE_##N##_SHIFT) & WE_RATE_##N##_MASK)
#define WE_RATE_ENTRY_IS_BG(E) \
   (WE_RATE_ENTRY_FIELD(E, DOMAIN) == WE_RATE_DOMAIN_BG)
#define WE_RATE_ENTRY_IS_HT(E) \
   (WE_RATE_ENTRY_FIELD(E, DOMAIN) == WE_RATE_DOMAIN_HT)
#define WE_RATE_ENTRY_CODE(E) \
   WE_RATE_ENTRY_FIELD(E, CODE)
#define WE_RATE_ENTRY_BITPOS(E) \
   WE_RATE_ENTRY_FIELD(E, BITPOS)
#define WE_HAS_BRATES(_mask)   ((wifiEngineState.rate_bmask & _mask) != 0)
#define WE_HAS_GRATES(_mask)   ((wifiEngineState.rate_gmask & _mask) != 0)
#if (DE_ENABLE_HT_RATES == CFG_ON)
#define WE_HAS_HTRATES(_mask)  ((wifiEngineState.rate_htmask & _mask) != 0)
#endif


#define NTOH16(_p)  ( (uint16_t) ( *(uint8_t*)(_p) | (*((uint8_t*)(_p)+1)<<8) ) )
#define NTOH32(_p)  ( (uint32_t) ( *(uint8_t*)(_p) | (*((uint8_t*)(_p)+1)<<8) | (*((uint8_t*)(_p)+2)<<16) | (*((uint8_t*)(_p)+3)<<24) ) )
#define HTON16  NTOH16
#define HTON32  NTOH32



enum wei_param_type {
   VOID_POINTER,
   WRAPPER_STRUCTURE
};

typedef struct wei_sm_queue_param {
   enum wei_param_type type;
   void *p;
} wei_sm_queue_param_s;


#define DEFAULT_CAPABILITY_INFO (M80211_CAPABILITY_SHORT_PREAMBLE | M80211_CAPABILITY_SHORT_SLOTTIME)

#define LOCK_UNLOCKED 0
#define LOCK_LOCKED 1

#define WIFI_LOCK()              DriverEnvironment_acquire_lock((driver_lock_t *)&wifiEngineState.lock)
#define WIFI_UNLOCK()            DriverEnvironment_release_lock((driver_lock_t *)&wifiEngineState.lock)
#define WIFI_RESOURCE_HIC_LOCK()     DriverEnvironment_acquire_lock((driver_lock_t *)&wifiEngineState.resource_hic_lock)
#define WIFI_RESOURCE_HIC_UNLOCK()   DriverEnvironment_release_lock((driver_lock_t *)&wifiEngineState.resource_hic_lock)



#define REGISTRY_WLOCK()         DriverEnvironment_acquire_write_lock(&wifiEngineState.rlock)
#define REGISTRY_WUNLOCK()       DriverEnvironment_release_write_lock(&wifiEngineState.rlock)
#define REGISTRY_RLOCK()         DriverEnvironment_acquire_read_lock(&wifiEngineState.rlock)
#define REGISTRY_RUNLOCK()       DriverEnvironment_release_read_lock(&wifiEngineState.rlock)

#define BAIL_IF_UNPLUGGED do {                                          \
      if (!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)                           \
          || wifiEngineState.core_dump_state == WEI_CORE_DUMP_ENABLED)  \
         return WIFI_ENGINE_FAILURE;                                    \
   } while (0) 
#define WEI_SET_TRANSID(x) \
 do { WIFI_LOCK(); (x) = wifiEngineState.pkt_cnt; \
      wifiEngineState.pkt_cnt =  \
         (wifiEngineState.pkt_cnt & (1U<<31)) ? wei_transid_wrap() : wifiEngineState.pkt_cnt + 1; \
      WIFI_UNLOCK(); \
 } while (0)

#define WEI_SCAN_JOB_ID_HIDDEN_SSID 0xFFFFFFFF

#ifdef USE_IF_REINIT
#define WEI_ACTIVITY() \
  do { wifiEngineState.activity_ts = DriverEnvironment_GetTicks();} while (0)
#else
#define WEI_ACTIVITY()
#endif
#define WEI_CMD_TX() \
  do { wifiEngineState.cmd_tx_ts = DriverEnvironment_GetTicks();} while (0)

#define RESOURCE_USER_CMD_PATH  0x01
#define RESOURCE_USER_DATA_PATH 0x02
#define RESOURCE_USER_TX_TRAFFIC_TMO   0x04
#define RESOURCE_DISABLE_PS   0x10


int   wei_request_resource_hic(int id);
void  wei_release_resource_hic(int id);
char* wei_printSSID(m80211_ie_ssid_t *ssid, char *str, size_t len);

char* wei_print_mac(m80211_mac_addr_t *mac_addr, char *str, size_t len);

int wei_queue_mib_reply(uint8_t messageId, hic_message_context_t *msg_ref);

int wei_queue_console_reply(char *replyPacked, size_t replySize);

void wei_clear_mib_reply_list(void);

int wei_console_init(void);

void wei_clear_console_reply_list(void);

void wei_clear_cmd_queue(void);

bool_t wei_legacy_power_save(void);

uint16_t wei_qos_get_ac_value(uint16_t prio);

bool_t wei_is_any_ac_set(void);

bool_t wei_is_hmg_auto_mode(void);

int wei_network_status_busy(void);

void wei_load_mib_from_device(void);
int wei_init_cmd_path(void);

int wei_send_cmd(hic_message_context_t *ctx);

int wei_send_cmd_raw(char *cmd, int size);

int wei_unconditional_send_cmd(hic_message_context_t *ctx);

int wei_send_scan_req(uint32_t job_id, uint16_t channel_interval, uint32_t probe_delay, uint16_t min_ch_time, uint16_t max_ch_time);

int wei_handle_scan_ind(hic_message_context_t *msg_ref);

int wei_handle_scan_cfm(hic_message_context_t *msg_ref);

int wei_handle_set_scan_param_cfm(hic_message_context_t *msg_ref);

int wei_handle_add_scan_filter_cfm(hic_message_context_t *msg_ref);

int wei_handle_remove_scan_filter_cfm(hic_message_context_t *msg_ref);

int wei_handle_add_scan_job_cfm(hic_message_context_t *msg_ref);

int wei_handle_remove_scan_job_cfm(hic_message_context_t *msg_ref);

int wei_handle_set_scan_job_state_cfm(hic_message_context_t *msg_ref);

int wei_handle_get_scan_filter_cfm(hic_message_context_t *msg_ref);

#if 0
int wei_handle_scan_adm_pool_cfm(hic_message_context_t *msg_ref);

int wei_handle_scan_adm_job_ssid_cfm(hic_message_context_t *msg_ref);
#endif

int wei_handle_scan_bssid_remove_ind(hic_message_context_t *msg_ref);

int wei_handle_scan_notification_ind(hic_message_context_t *msg_ref);

int wei_handle_set_scancountryinfo_cfm(hic_message_context_t *msg_ref);

#if DE_PROTECT_FROM_DUP_SCAN_INDS == CFG_ON
int wei_exclude_from_scan(m80211_mac_addr_t* bssid_p, m80211_ie_ssid_t* ssid_p, int current_channel);
#endif

int wei_is_cmd_ind(int msgType, int msgId);

int wei_is_net_joinable(WiFiEngine_net_t *net);

int wei_equal_ssid(m80211_ie_ssid_t ssid1, m80211_ie_ssid_t ssid2);

int wei_desired_ssid(m80211_ie_ssid_t ssid);

int wei_equal_bssid(m80211_mac_addr_t bssid1, m80211_mac_addr_t bssid2);

int wei_is_bssid_bcast(m80211_mac_addr_t bssid);

void wei_copy_bssid(m80211_mac_addr_t *dst, m80211_mac_addr_t *src);

void wei_weinet2wenet(WiFiEngine_net_t *dst, WiFiEngine_net_t *src);

WiFiEngine_Encryption_t wei_cipher_suite2encryption(m80211_cipher_suite_t suite);

m80211_cipher_suite_t wei_cipher_encryption2suite(WiFiEngine_Encryption_t *sel);

WiFiEngine_Auth_t wei_akm_suite2auth(WiFiEngine_Encryption_t enc, m80211_akm_suite_t *suite);

int wei_encryption_mode_allowed(WiFiEngine_Encryption_t enc);
int wei_is_encryption_mode_denied(WiFiEngine_Encryption_t encType);

int  wei_filter_net_by_authentication(m80211_akm_suite_t *akm,
                                      wei_ie_type_t *type,
                                      WiFiEngine_net_t **net);

int wei_build_rsn_ie(void* context_p,
                     m80211_ie_rsn_parameter_set_t *dst,
                     m80211_cipher_suite_t group_suite,
                     m80211_cipher_suite_t pairwise_suite,
                     m80211_akm_suite_t akm_suite,
                     unsigned int nUsePMKID,
                     const m80211_bssid_info *pmkid);
int wei_build_wpa_ie(void* context_p,
                     m80211_ie_wpa_parameter_set_t *dst,
                     m80211_cipher_suite_t group_suite,
                     m80211_cipher_suite_t pairwise_suite,
                     m80211_akm_suite_t akm_suite);
#if (DE_CCX == CFG_INCLUDED)
int HandleRadioMeasurementReq(char channel, int duration, char mode);
int radio_measurement_cb(void *data, size_t len);
void ccx_handle_wakeup(void);
void wei_ccx_metrics_on_cfm(uint32_t trans_id);
void wei_ccx_add_trans(ccx_metrics_t * metrics_p, uint32_t transid);
void wei_ccx_free_trans(uint32_t transid);

#define CCX_AP_REPORT_PENDING       0x01
#define CCX_TS_METRICS_PENDING      0x02
#define CCX_RDIO_MEASURE_PENDING    0x04

#endif
int wei_build_wapi_ie(void* context_p,
            m80211_ie_wapi_parameter_set_t *dst,
            m80211_akm_suite_t akm_suite);
m80211_mlme_associate_req_t *wei_copy_assoc_req(m80211_mlme_associate_req_t *src);
m80211_mlme_associate_cfm_t *wei_copy_assoc_cfm(m80211_mlme_associate_cfm_t *src);
void wei_free_assoc_cfm(m80211_mlme_associate_cfm_t *p);
void wei_free_assoc_req(m80211_mlme_associate_req_t *p);
int wei_compare_cipher_suites(m80211_cipher_suite_t a, m80211_cipher_suite_t b);
int wei_cipher_suite_to_val(m80211_cipher_suite_t a);
int wei_get_highest_pairwise_suite_from_net(m80211_cipher_suite_t *suite,
                                            WiFiEngine_net_t *net,
                                            wei_ie_type_t type);

int wei_pmkid_init(void);
int wei_pmkid_unplug(void);

wei_sm_queue_param_s* wei_wrapper2param(void *p);

void wei_sm_init(void);
void wei_sm_drain_sig_q(void);
void wei_sm_execute(void);
void wei_sm_queue_sig(ucos_msg_id_t sig, 
                      SYSDEF_ObjectType dest, 
                      wei_sm_queue_param_s* p,
                      bool_t internal);


void wei_cb_init(void);
void wei_cb_shutdown(void);
void wei_cb_unplug(void);
void wei_cb_flush_pending_cb_tab(void);
void wei_cb_queue_pending_callback(we_cb_container_t *cbc);
we_cb_container_t *wei_cb_find_pending_callback(uint32_t trans_id);
int wei_cb_still_valid(we_cb_container_t *cbc);
int wei_mib_schedule_cb(int msgId, hic_interface_wrapper_t *msg_ref);

typedef int (*wei_update_transform)(char *dst, size_t dst_len);

int wei_send_mib_get_next(bool_t get_first, uint32_t *trans_id);

int wei_get_mib_with_update(const char *id, 
                            void *dst, 
                            size_t dstlen, 
                            wei_update_transform transform, 
                            we_indication_t we_ind);

int wei_channel_list_from_country_ie(channel_list_t *cl, m80211_ie_country_t *ie);
we_ch_bitmask_t wei_channels2bitmask(channel_list_t *src);
int wei_get_number_of_ptksa_replay_counters_from_net(WiFiEngine_net_t *net);


int wei_ratelist2mask(uint32_t  *rate_mask,
                      rRateList *rrates);
int wei_ratelist2ie(m80211_ie_supported_rates_t *sup_rates, 
                    m80211_ie_ext_supported_rates_t *ext_rates,
                    rRateList *rrates);
int wei_ie2ratelist(rRateList *rrates, m80211_ie_supported_rates_t *sup, 
                    m80211_ie_ext_supported_rates_t *ext);
void wei_prune_nonbasic_ratelist(rRateList *rates);

#if (DE_ENABLE_HT_RATES == CFG_ON)
int wei_htcap2mask(uint32_t*, const m80211_ie_ht_capabilities_t*);
int wei_htoper2mask(uint32_t*, const m80211_ie_ht_operation_t*);
#endif

#define wei_get_data_req_hdr_size()   (uint16_t)M80211_MAC_DATA_REQ_HEADER_SIZE
uint16_t wei_get_hic_hdr_min_size(void);

void wei_init_wmm_ie(m80211_ie_WMM_information_element_t *ie, int wmm_ps_enable);
bool_t wei_is_wmm_ps_enabled(void);

typedef struct
{
      char octets[MIB_IDENTIFIER_MAX_LENGTH];
} mib_id_t;

int   wei_send_mib_get_binary(mib_id_t mib_id, uint32_t *num);
int   wei_send_mib_set_binary(mib_id_t mib_id, uint32_t *num, const void *inbuf, size_t inbuflen,
                              we_cb_container_t *cbc);

int wei_get_mib_object(const mib_id_t*, mib_object_entry_t*);
int wei_have_mibtable(void);
void wei_free_mibtable(void);

int wei_schedule_data_cb_with_status(uint32_t trans_id, void *data, 
                                     size_t len, uint8_t result);
unsigned int wei_transid_wrap(void);
int wei_jobid_wrap(void);

void wei_save_con_failed_reason(we_con_failed_e type, uint8_t reason);
void wei_save_con_lost_reason(we_con_lost_e type, uint8_t reason);

void WiFiEngine_get_last_con_lost_reason(we_con_lost_s *dst);
void WiFiEngine_get_last_con_fail_reason(we_con_failed_s *dst);


int wei_initialize_auth(void);
int wei_reconfigure_auth(void);
int wei_shutdown_auth(void);

int wei_initialize_scan(void);
int wei_reinitialize_scan(void);
int wei_reconfigure_scan(void);
int wei_shutdown_scan(void);

int wei_initialize_mib(void);
int wei_reconfigure_mib(void);
int wei_shutdown_mib(void);
int wei_unplug_mib(void);

int wei_send_set_scancountryinfo_req(m80211_ie_country_t *ci);


int wei_reconfigure_roam(void);
int wei_initialize_roam(void);
int wei_shutdown_roam(void);

int we_lqm_configure_lqm_job(void);
int wei_lqm_shutdown(void);
int we_lqm_trigger_scan(int);

void wei_dump_notify_connect_failed(void);
void wei_dump_notify_connect_success(void);

/*
void wei_roam_notify_connect_failed(void);
void wei_roam_notify_connect_success(void);
void wei_roam_notify_disconnect(void);
*/

int wei_roam_notify_connected(void);
int wei_roam_notify_connect_failed(void);
int wei_roam_notify_disconnecting(void);
int wei_roam_notify_disconnected(void);

void wei_roam_notify_scan_ind(void);
void wei_roam_notify_data_ind(void);
void wei_roam_notify_data_cfm(m80211_std_rate_encoding_t rate_used);
void wei_roam_notify_scan_failed(void);
void wei_roam_notify_scan_complete(void);

void wei_ratemon_notify_data_cfm(m80211_std_rate_encoding_t rate);
void wei_ratemon_notify_data_ind(m80211_std_rate_encoding_t rate);

void wei_enable_aggregation(we_aggr_type_t type, uint32_t aggr_max_size);

WiFiEngine_net_t* wei_find_create_ibss_net(void);
bool_t wei_is_ibss_connected(m80211_nrp_mlme_peer_status_ind_t *ind);
bool_t wei_ibss_disconnect(m80211_nrp_mlme_peer_status_ind_t *ind);
void wei_ibss_remove_net(m80211_nrp_mlme_peer_status_ind_t *ind);

int wei_is_80211_connected(void);

char *wei_print_mib(const mib_id_t *mib_id, char *str, size_t size);

/* we_mcast.c */
void wei_multicast_init(void);
int wei_multicast_add(m80211_mac_addr_t);
int wei_multicast_remove(m80211_mac_addr_t);
int wei_multicast_clear(void);
int wei_multicast_set(const m80211_mac_addr_t*, unsigned int);
int wei_multicast_get(m80211_mac_addr_t*, unsigned int*);
void wei_multicast_set_mode(int);
int wei_multicast_get_mode(void);
int wei_multicast_update(void);

/* we_arp_filter.c */
void wei_arp_filter_init (void);
void wei_arp_filter_shutdown (void);
void wei_arp_filter_unplug (void);
void wei_arp_filter_configure (void);
int wei_arp_filter_add (ip_addr_t, unsigned int);
int wei_arp_filter_remove (ip_addr_t);
int wei_arp_filter_clear (void);
int wei_arp_filter_update (void);
int wei_arp_filter_have_dynamic_address (void);

int m80211_ie_is_wps_configured(m80211_ie_wps_parameter_set_t *ie);

void wei_handle_dlm_pkt(hic_message_context_t *msg_ref);

void wei_pm_shutdown(void *priv);
void wei_pm_initialize(void **priv); 

void wei_interface_init(void);
void wei_interface_shutdown(void);
void wei_interface_plug(void);
void wei_interface_unplug(void);

void wei_virt_trig_init(void);
void wei_virt_trig_unplug(void);

void wei_hmg_ps_init(void);
void wei_hmg_pal_init(void);
void wei_hmg_traffic_init(void);

void wei_data_init(void);
void wei_data_shutdown(void);
void wei_data_plug(void);
void wei_data_unplug(void);
void wei_ps_plug(void);
void wei_ps_unplug(void);

void wei_ps_ctrl_alloc_init(void);
void wei_ps_ctrl_shutdown(void);
void wei_ind_unplug(void);
void wei_ind_shutdown(void);

/* we_param.c */
int wei_remove_startup_mib_set(const mib_id_t *mib_id);
int wei_apply_wlan_parameters(void);
int we_wlp_shutdown(void);
int we_wlp_configure_device(void);

/* we_rate.c */
int wei_rate_configure(void);

/* we_cm_scan.c */
void WiFiEngine_sac_init(void);
void wei_cm_scan_unplug(void);

/* we_ccx.c */
#if (DE_CCX == CFG_INCLUDED)
void wei_ccx_plug(void);
void wei_ccx_unplug(void);
#endif
#define NO_SCAN_JOB 0xFFFFFFFF

/* we_pal.c */
int wei_pal_init(void);
int wei_pal_send_data(char* data_buf, int size, uint32_t trans_id);
extern net_rx_cb_t data_path_rx_cb;
extern net_rx_cb_t data_path_cfm_cb;

#endif /* WIFI_ENGINE_INTERNAL_H */
