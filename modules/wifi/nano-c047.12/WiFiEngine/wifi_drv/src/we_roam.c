/* $Id: we_roam.c,v 1.53 2008-05-19 15:08:55 miwi Exp $ */

/** @defgroup we_roam WiFiEngine roaming agent implementation
 *
 * \brief  This module implements WiFiEngine roaming agent.
 *
 * The roaming procedure will be initiated on the following events:
 * - New scan indications are revieved when the STA is disconnected
 *   (wei_roam_notify_scan_ind).
 * - The connection to the current AP is lost (wei_roam_notify_disconnect).
 * - The TX rate for the current AP falls below the configured threshold
 *   (wei_roam_notify_data_cfm).
 * - The RSSI level for the current AP falls below the configured threshold
 *   (notify_mib_trigger).
 * - The SNR level for the current AP falls below the configured threshold
 *   (notify_mib_trigger).
 * - The delay spread value for the current AP rises above the configured
 *   threshold (notify_mib_trigger).
 *
 * When romaing, the best network is elected by a combination of RSSI and
 * SNR values. AP's will also be filtered out accoring to WMM, blacklist and
 * SSID configuration.
 *
 * The roaming agent will update elected_net to point to the desired net.
 * During join, if elected_net is set, this net will be preferred over net's
 * matching the desired SSID.
 *
 * Prior to roaming, a directed scan can be issued on the following events:
 * - The TX rate for the current AP falls below the configured threshold
 *   for scanning (wei_roam_notify_data_cfm).
 * - The RSSI level for the current AP falls below the configured threshold
 *   for scanning (notify_mib_trigger).
 * - The SNR level for the current AP falls below the configured threshold
 *   for scanning (notify_mib_trigger).
 * - The delay spread value for the current AP rises above the configured
 *   threshold for scanning (notify_mib_trigger).
 *
 *
 * These are some of the normal state transissions that may occur
 *
 * elective/hc perfect: CONNECTED->DISCONNECTING->ROAMING->CONNECTED
 * case success: CONNECTED->DISCONNECTING->...->ROAMING->CONNECTED
 * case failure: CONNECTED->DISCONNECTING->...->DISCONNECTED
 *
 * case: ... success
 *
 * ROAMING->DISCONNECTING
 * ROAMING->DISCONNECTING->HC->DISCONNECTED
 * ROAMING->DISCONNECTING->ROAMING->DISCONNECTING->DISCONNECTED
 * ...
 *
 * 2 connection failures and stop after first scan:
 * ROAMING->DISCONNECTING->HC->ROAMING->DISCONNECTING->HC->DISCONNECTED
 *
 * Possible transissions:
 * ---------------------
 * STATE_CONNECTED -> [STATE_DISCONNECTING|STATE_DISCONNECTED]
 * STATE_DISCONNECTING > [STATE_HC|STATE_ROAMING|STATE_DISCONNECTED]
 * STATE_ROAMING -> [STATE_CONNECTED|STATE_DISCONNECTING]
 * STATE_HC > [STATE_ROAMING|STATE_DISCONNECTED]
 *
 * TODO:
 *
 * - DS implementation incomplete. get DS values from fw and eval at  state_connected_ev_scan_complete.
 * - refactor wei_roam_nofity_data_cfg to conform to state machine template.
 * - add command queue for ..._notify...() (may not be needed)
 *
 */

#include "driverenv.h"
#include "m80211_stddefs.h"
#include "driverenv_kref.h"
#include "we_ind.h"
#include "wifi_engine_internal.h"
#include "wei_netlist.h"
#include "hmg_traffic.h"
#include "registry.h"
#include "registryAccess.h"

/* 
   FIXME: this should be removed. Used to inform
   reassoc req when we need to roam using ccx 
*/
#if (DE_CCX_ROAMING == CFG_INCLUDED)
WiFiEngine_net_t * ccxnet;
#endif

/************ DEFINES AND MACROS : START ************/

#ifdef FILE_WE_ROAM_C
#undef FILE_NUMBER
#define FILE_NUMBER FILE_WE_ROAM_C
#endif //FILE_WE_ROAM_C

#define MAX_ALLOWED_SSID 8

//#define ROAM_ON_MIB_TRIGGER
//#define MISSING_SCAN_COMPLETE_WORKAROUND

#if !defined(WIFI_DEBUG_ON) || (DE_TRACE_MODE == CFG_OFF)
#define ROAM_TRACE_NET(prefix, net) {}
#else
#define ROAM_TRACE_NET(prefix, net) if(net) {                           \
      char ssid[M80211_IE_MAX_LENGTH_SSID + 1];                         \
      char bssid[32];                                                   \
      DE_TRACE6(TR_ROAM, "%s: mac=%s ssid=%s rssi=%d snr=%d\n",         \
                prefix,                            \
                wei_print_mac(&net->bssId_AP, bssid, sizeof(bssid)),              \
                wei_printSSID(&net->bss_p->bss_description_p->ie.ssid, ssid, sizeof(ssid)), \
                net->bss_p->rssi_info,               \
                net->bss_p->snr_info);               \
   }
#endif //(DE_TRACE_MODE == CFG_OFF)

/************ DEFINES AND MACROS : END ************/
/************ TYPE DECLARATIONS : START ***********/

#define NUM_STATES 5
typedef enum roam_state_e {
   /* state disconnected will be reached if roaming agent failed to roam. 
    * In disconneced state the roaming agent will remain unactive until the 
    * first connect success. */
   STATE_DISCONNECTED,     

   /* state connected is defined from first connect success to that we lose 
    * the AP or transit to state roaming|hc|ap_mia */
   STATE_CONNECTED,

   /* missing in action: beacon failure, tx failure, ... 
    * transit from connected */
   STATE_DISCONNECTING,

   /* Same as state ap mia but occur at a later time and of other causes. */
   STATE_HC,

   /* Apply to both elective and HC */
   STATE_ROAMING
} roam_state_t;

#define NUM_EVENTS 11
typedef enum roam_event_e {
   EVENT_INIT,
   EVENT_EXIT,
   EVENT_CONNECT_SUCCESS,
   EVENT_CONNECT_FAILED,
   EVENT_CONNECT_REFUCED,
   EVENT_DISCONNECTED,
   EVENT_SCAN_IND,
   EVENT_SCAN_COMPLETE,
   EVENT_SM_DISCONNECTING,
   EVENT_CORE_DUMP
} roam_event_t;

/* To compensate for lack of scan api */
typedef enum scan_state_e {
   SCAN_NOT_CONFIGURED,
   SCAN_STARTED,
   SCAN_STOPPED
} scan_state_t;

enum net_blacklist_e {
   NOT_BLACKLISTED = 0,
   BLACKLIST_TEMPORARILY,
   BLACKLIST_PERMEMANTLY
};
static void clear_all_tmp_blacklisted(void);

struct roam_config_s {

   /* Filters */
   bool_t enable_roaming;
   bool_t enable_periodic_scan;
   bool_t enable_blacklist_filter;
   bool_t enable_ssid_filter;
   bool_t enable_wmm_filter;
   m80211_ie_ssid_t ssid[MAX_ALLOWED_SSID];

   /* RSSI configuration */
   bool_t rssi_enable;
   int32_t rssi_roam_thr;
   int32_t rssi_scan_thr;
   int32_t rssi_scan_job_id;
   int32_t rssi_margin;

   /* SNR configuration */
   bool_t snr_enable;
   int32_t snr_roam_thr; /* SNR is in the range -1..~30 */
   int32_t snr_scan_thr;
   int32_t snr_margin;

   /* RSSI/SNR/... */
   bool_t measure_on_data;

   /* Delay spread configuration */
   bool_t ds_enable;
   uint32_t ds_roam_thr; /* [%] */
   uint32_t ds_scan_thr; /* [%] */
   uint32_t ds_thr;      /* [ns] */
   uint16_t ds_winsize;  /* Packets in each calculation */

   /* Rate configuration */
   bool_t rate_enable;
   m80211_std_rate_encoding_t rate_roam_thr;
   m80211_std_rate_encoding_t rate_scan_thr;

   /* Constants for estimating AP link quality */
   uint32_t k1;
   uint32_t k2;

   uint32_t  max_scan_count;
   uint32_t  max_connect_count;
   uint32_t  max_elect_count;

   bool_t auth_enable;
   WiFiEngine_Auth_t auth_mode;
   WiFiEngine_Encryption_t enc_mode;
};


struct roam_state_s {
   scan_state_t scan_state;
   roam_state_t state;
   we_roaming_reason_t last_roaming_reason;

#ifdef ROAM_ON_MIB_TRIGGER
   int32_t rssi_roam_trig_id;
#endif
   int32_t rssi_falling_scan_trig_id;
   int32_t rssi_rising_scan_trig_id;

   int32_t snr_falling_scan_trig_id;
   int32_t snr_rising_scan_trig_id;

   int32_t ds_falling_scan_trig_id;
   int32_t ds_rising_scan_trig_id;

   uint32_t rate_roam_done;
   uint32_t rate_scan_done;

   uint32_t connect_count;
   uint32_t  elect_count;

   uint32_t  scan_job_id;
   int32_t  scan_count;

   int32_t  rssi;
   int32_t  snr;

   bool_t rssi_scan;
   bool_t snr_scan;
   bool_t ds_scan;

   int32_t rssi_update_pending;

   struct iobject *core_dump_h;
   struct iobject *rssi_h;

   WiFiEngine_net_t* elected_net;
   m80211_mac_addr_t last_connected_net;
   uint16_t last_connected_net_cabability;

   driver_tick_t connected_at_tick;
};


#define EV_ARGS roam_event_t event
typedef void (*stfunc)(EV_ARGS);

/* state functions of type stfunc : start */
static void state_disconnected_ev_init(EV_ARGS);
static void state_disconnected_ev_connect_success(EV_ARGS);

static void state_connected_ev_init(EV_ARGS);
static void state_connected_ev_exit(EV_ARGS);
static void state_connected_ev_disconnected(EV_ARGS);
static void state_connected_ev_scan_ind(EV_ARGS);
static void state_connected_ev_scan_complete(EV_ARGS);
static void state_connected_ev_disconnecting(EV_ARGS);
static void state_connected_ev_core_dump(EV_ARGS);

static void state_disconnecting_ev_disconnected(EV_ARGS);
static void state_disconnecting_ev_core_dump(EV_ARGS);

static void state_hc_ev_init(EV_ARGS);
static void state_hc_ev_scan_ind(EV_ARGS);
static void state_hc_ev_scan_complete(EV_ARGS);
static void state_hc_ev_core_dump(EV_ARGS);

static void state_roaming_ev_init(EV_ARGS);
static void state_roaming_ev_connect_success(EV_ARGS);
static void state_roaming_ev_connect_failed(EV_ARGS);
static void state_roaming_ev_connect_refuced(EV_ARGS);
static void state_roaming_ev_disconnecting(EV_ARGS);
static void state_roaming_ev_disconnected(EV_ARGS);
static void state_roaming_ev_core_dump(EV_ARGS);

#if 0
static void reset_state_on_ev_connect_success(EV_ARGS);
#endif

static void wei_roam_core_dump_ind(wi_msg_param_t param, void *priv);

static void event_ignore(EV_ARGS);
static void event_assert(EV_ARGS);

/* state functions of type stfunc : end */

static void state_elective_ev_rssi_cb(wi_msg_param_t param, void *priv);

static void scan(void);
static void connect(WiFiEngine_net_t* net);
static void disconnect(void);
//static void clear_keys(WiFiEngine_net_t *net);

static WiFiEngine_net_t* elect_net(int);
static bool_t filter_out_net(WiFiEngine_net_t* net);
static uint32_t calc_link_quality(WiFiEngine_net_t* net);
static bool_t is_net_above_thresholds(WiFiEngine_net_t* net, bool_t with_margin);

static bool_t is_ibss(WiFiEngine_net_t* net);
static bool_t is_privacy_changed(WiFiEngine_net_t* net);
static bool_t ssid_matches(WiFiEngine_net_t* net);
static bool_t is_blacklisted(WiFiEngine_net_t* net);
static bool_t is_encryption_allowed(WiFiEngine_net_t* net);
static bool_t is_wmm_supported(WiFiEngine_net_t* net);
static bool_t is_self(WiFiEngine_net_t* candidate);
static bool_t is_old(WiFiEngine_net_t* candidate);
static bool_t is_down(WiFiEngine_net_t* candidate);
#if (DE_CCX == CFG_INCLUDED)
static bool_t is_ccx_aac_invalid(WiFiEngine_net_t* candidate);
#endif
static bool_t is_lq_above_thresholds(bool_t with_margin);
static bool_t is_net_lq_above(WiFiEngine_net_t* net, int rssi, int snr, bool_t with_margin);

static int reconfigure_mib_trigger(int32_t* id, const char *mib, int32_t thr,
                                   uint8_t dir, bool_t enable);
static void reconfigure_all_mib_triggers(void);
static int notify_mib_trigger(void *data, size_t len);
static void stop_all_mib_triggers(void);

static void refresh_lq(bool_t poll);
static void reset_elected_net(void);
static void set_elected_net(WiFiEngine_net_t *net);
static void reset_last_connected_net(void);
static void set_last_connected_net(WiFiEngine_net_t *net);


static int wei_roam_configure_scan(void);
static int wei_roam_start_periodic_scan(void);
static int wei_roam_stop_periodic_scan(void);
static void update_scan_based_on_triggers(void);
static void wei_roam_reset_scan(void);

/*********** TYPE DECLARATIONS : END ***********/
/*********** MAIN DECLARATIONS : START ***********/

const stfunc state_table[NUM_STATES][NUM_EVENTS] = {
   { /* STATE_DISCONNECTED */
      state_disconnected_ev_init,
      event_ignore,
      state_disconnected_ev_connect_success,
      event_ignore,
      event_ignore,
      event_ignore,
      event_ignore,
      event_ignore,
      event_ignore,
      event_ignore
   }, { /* STATE_CONNECTED */
      state_connected_ev_init,
      state_connected_ev_exit,
      event_assert,
      event_assert,
      event_assert,
      state_connected_ev_disconnected,
      state_connected_ev_scan_ind,
      state_connected_ev_scan_complete,
      state_connected_ev_disconnecting,
      state_connected_ev_core_dump
   }, { /* STATE_DISCONNECTING */
      event_ignore,
      event_ignore,
      event_assert,
      event_assert,
      event_assert,
      state_disconnecting_ev_disconnected,
      event_ignore,
      event_ignore,
      event_ignore,
      state_disconnecting_ev_core_dump
   }, { /* STATE_HC */
      state_hc_ev_init,
      event_ignore,
      event_assert,
      event_assert,
      event_assert,
      event_ignore,
      state_hc_ev_scan_ind,
      state_hc_ev_scan_complete,
      event_assert,
      state_hc_ev_core_dump
   }, { /* STATE_ROAMING */
      state_roaming_ev_init,
      event_ignore,
      state_roaming_ev_connect_success,
      state_roaming_ev_connect_failed,
      state_roaming_ev_connect_refuced,
      state_roaming_ev_disconnected,
      event_ignore,
      event_ignore,
      state_roaming_ev_disconnecting,
      state_roaming_ev_core_dump
   }
};

static struct roam_config_s roam_config;
static struct roam_state_s roam_state;

static uint32_t lqm_job_id;

/*********** MAIN DECLARATIONS : END ***********/

#define EVENT_ENTRY DE_TRACE_STATIC2(TR_ROAM, "[%s] ENTRY\n", state2str(roam_state.state));
//#define EVENT_ENTRY

static char* state2str(roam_state_t state) {
#if (DE_TRACE_MODE  == CFG_OFF)
    return "";
#else
   switch (state) {
   case STATE_DISCONNECTED:
      return "STATE_DISCONNECTED";
   case STATE_CONNECTED:
      return "STATE_CONNECTED";
   case STATE_DISCONNECTING:
      return "STATE_DISCONNECTING";
   case STATE_HC:
      return "STATE_HC";
   case STATE_ROAMING:
      return "STATE_ROAMING";
   }
   return "UNKNOWN";
#endif
}

static char* event2str(roam_event_t event) {
#if (DE_TRACE_MODE  == CFG_OFF)
    return "";
#else
   switch (event) {
   case EVENT_CONNECT_SUCCESS:
      return "EVENT_CONNECT_SUCCESS";
   case EVENT_CONNECT_FAILED:
      return "EVENT_CONNECT_FAILED";
   case EVENT_CONNECT_REFUCED:
      return "EVENT_CONNECT_REFUCED";
   case EVENT_DISCONNECTED:
      return "EVENT_DISCONNECTED";
   case EVENT_SCAN_IND:
      return "EVENT_SCAN_IND";
   case EVENT_SCAN_COMPLETE:
      return "EVENT_SCAN_COMPLETE";
   case EVENT_SM_DISCONNECTING:
      return "EVENT_SM_DISCONNECTING";
   case EVENT_EXIT:
      return "EVENT_EXIT";
   case EVENT_INIT:
      return "EVENT_INIT";
   case EVENT_CORE_DUMP:
      return "EVENT_CORE_DUMP";
   }
   return "UNKNOWN";
#endif
}

/* Should be called from WiFiEngine_Initialize(), e.g. when driver is loaded.
 * This function will setup default configuration for roaming.
 */
int wei_initialize_roam(void)
{
   int i;

   roam_state.rssi_falling_scan_trig_id = -1;
   roam_state.rssi_rising_scan_trig_id = -1;

   roam_state.snr_falling_scan_trig_id = -1;
   roam_state.snr_rising_scan_trig_id = -1;

   roam_state.ds_falling_scan_trig_id = -1;
   roam_state.ds_rising_scan_trig_id = -1;

   /* start scanning based on RSSI/SNR/DS */
   roam_state.rssi_scan = FALSE;
   roam_state.snr_scan = FALSE;
   roam_state.ds_scan = FALSE;

   roam_state.rate_roam_done = TRUE;
   roam_state.rate_scan_done = TRUE;

   DE_MEMSET(&roam_config, 0, sizeof(roam_config));

   roam_config.enable_roaming = FALSE;
   roam_config.enable_blacklist_filter = TRUE;
   roam_config.enable_ssid_filter = FALSE;
   roam_config.enable_wmm_filter = FALSE;
   for (i = 0; i < MAX_ALLOWED_SSID; i++)
      roam_config.ssid[i].hdr.id = M80211_IE_ID_NOT_USED;

   /* RSSI configuration */
   roam_config.rssi_enable = TRUE;
   roam_config.rssi_roam_thr = -65;
   roam_config.rssi_scan_thr = -55;
   roam_config.rssi_margin = 5;

   /* SNR configuration */
   roam_config.snr_enable = TRUE;
   roam_config.snr_roam_thr = 8;
   roam_config.snr_scan_thr = 12;
   roam_config.snr_margin = 4;

   /* RSSI/SNR/... */
   roam_config.measure_on_data = FALSE;

   /* Delay spread configuration */
   roam_config.ds_enable = FALSE;
   roam_config.ds_roam_thr = 95;
   roam_config.ds_scan_thr = 90;
   roam_config.ds_thr = 175;
   roam_config.ds_winsize = 24;

   /* Rate configuration */
   roam_config.rate_enable = FALSE;
   roam_config.rate_roam_thr = 11;
   roam_config.rate_scan_thr = 18;

   /* Constants for estimating AP link quality */
   roam_config.k1 = 1;
   roam_config.k2 = 1;

   /* Hard-cut configuration */
   roam_config.max_scan_count = 2;

   /* Scan job configured to scan for hidden ssid */
   roam_state.scan_job_id = NO_SCAN_JOB;
   roam_state.scan_state = SCAN_NOT_CONFIGURED;

   /* Periodic scan controlled by rssi triggers */
   roam_config.enable_periodic_scan = TRUE;

   /* Authentication */
   roam_config.auth_enable = FALSE;
   roam_config.auth_mode = Authentication_Open;
   roam_config.enc_mode = Encryption_Disabled;
   roam_config.max_connect_count = 2;
   roam_config.max_elect_count = 3;

   /* Misc */
   roam_state.state = STATE_DISCONNECTED;
   roam_state.last_roaming_reason = WE_ROAM_REASON_NONE;
   roam_state.rssi_update_pending = 0;

   roam_state.elected_net = NULL;
   reset_last_connected_net();

   return WIFI_ENGINE_SUCCESS;
}


/* Should be called from WiFiEngine_Shutdown(), e.g. when driver is unloaded. */
int wei_shutdown_roam(void)
{
   roam_state.state = STATE_DISCONNECTED;

   we_ind_deregister_null(&roam_state.core_dump_h);
   we_ind_deregister_null(&roam_state.rssi_h);

   wei_roam_reset_scan();
   stop_all_mib_triggers();

   reset_elected_net();
   roam_state.rssi_update_pending = 0;
   reset_last_connected_net();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * \brief Force encryption and authentication mode while roaming.
 *
 * This function can be used when roaming in WPA networks. In that case the
 * WPA supplicant should not scan and roam on its own, so we need another way of
 * configuring the encryption and authentication before association.
 *
 * We'll also make sure that the encryption and authentication mode is set
 * again before associating to a new AP due to roaming, in case they are
 * cleared by the WPA supplicant for some reason.
 *
 * @param enable When enabled, the forced encryption and authentication mode
 *        will be used.
 *
 * @param enc_mode Forced encryption mode.
 *
 * @param auth_mode Forced authentication mode.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE on error.
 */
int WiFiEngine_roam_conf_auth(bool_t enable,
                              WiFiEngine_Auth_t auth_mode,
                              WiFiEngine_Encryption_t enc_mode)

{
   if (enable != TRUE && enable != FALSE)
      return WIFI_ENGINE_FAILURE;

   roam_config.auth_enable = enable;
   roam_config.auth_mode = auth_mode;
   roam_config.enc_mode = enc_mode;

   if (roam_config.auth_enable) {
      WiFiEngine_SetAuthenticationMode(roam_config.auth_mode);
      WiFiEngine_SetEncryptionMode(roam_config.enc_mode);
   }
   else {
      WiFiEngine_SetAuthenticationMode(Authentication_Open);
      WiFiEngine_SetEncryptionMode(Encryption_Disabled);
   }

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Check if roaming is enabled.
 *
 * @return
 *        - TRUE if romaing is enabled.
 *        - FALSE is roaming is disabled.
 */
bool_t WiFiEngine_is_roaming_enabled(void)
{
   return roam_config.enable_roaming;
}


/* Should be called from WiFiEngine_Configure_Device() */
int wei_reconfigure_roam(void)
{
   //wei_netlist_remove_elected_net();
   return WIFI_ENGINE_SUCCESS;
}



/*!
 * \brief Configure TX rate thresholds for initiating scanning and roaming.
 *
 * MIB triggers will be registered for the configured thresholds. When a
 * trigger is received scanning or roaming will be initiated. When the
 * thresholds are reconfigured the mib triggers will be re-registered.
 *
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        TX rate mib triggers are received. When disabled, MIB triggers will
 *        not be registered.
 *
 * @param roam_thr When the current TX rate level falls below this threshold,
 *        then romaing will be initiated. To avoid problems with low initial
 *        rates, the current rate must first rise above the rate threshold and
 *        then fall below threshold.
 *
 * @param scan_thr When the current TX rate level falls below this threshold,
 *        then scanning will be initiated. To avoid problems with low initial
 *        rates, the current rate must first rise above the rate threshold and
 *        then fall below threshold.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE on error.
 *
 */
int WiFiEngine_roam_configure_rate_thr(bool_t enable,
                                       m80211_std_rate_encoding_t roam_thr,
                                       m80211_std_rate_encoding_t scan_thr)
{
   if (enable != TRUE && enable != FALSE)
      return WIFI_ENGINE_FAILURE;

   if (roam_thr > 108)
      return WIFI_ENGINE_FAILURE;

   if (scan_thr > 108)
      return WIFI_ENGINE_FAILURE;

   if (roam_thr > scan_thr)
      return WIFI_ENGINE_FAILURE;

   roam_config.rate_enable = enable;
   roam_config.rate_roam_thr = roam_thr;
   roam_config.rate_scan_thr = scan_thr;

   if (!roam_config.enable_roaming)
      return WIFI_ENGINE_SUCCESS;

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Configure delay spread thresholds for initiating scanning and roaming.
 *
 * MIB triggers will be registered for the configured thresholds. When a
 * trigger is received scanning or roaming will be initiated. When the
 * thresholds are reconfigured the mib triggers will be re-registered.
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        delay spread mib triggers are received. When disabled, MIB triggers
 *        will not be registered.
 *
 * @param roam_thr When the current delay spread level rises above this
 *        threshold, then romaing will be initiated.
 *
 * @param scan_thr When the current delay spread level rises above this
 *        threshold, then scanning will be initiated.
 *
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE on error.
 *
 */
int WiFiEngine_roam_configure_ds_thr(bool_t enable,
                                     uint32_t roam_thr,
                                     uint32_t scan_thr)
{
   if (enable != TRUE && enable != FALSE)
      return WIFI_ENGINE_FAILURE;

   if (roam_thr > 100)
      return WIFI_ENGINE_FAILURE;

   if (scan_thr > 100)
      return WIFI_ENGINE_FAILURE;

   if (roam_thr < scan_thr)
      return WIFI_ENGINE_FAILURE;

   roam_config.ds_enable = enable;
   roam_config.ds_roam_thr = roam_thr;
   roam_config.ds_scan_thr = scan_thr;

   if (!roam_config.enable_roaming)
      return WIFI_ENGINE_SUCCESS;

   reconfigure_all_mib_triggers();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Configure constants for calculating net link quality when electing
 *        AP.
 *
 * The constants will affect how much RSSI is prioritized compared to SNR when
 * electing the best net.
 *
 * @param k1 RSSI weight.
 *
 * @param k2 SNR weight.
 * * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE on error.
 *
 */
int WiFiEngine_roam_configure_net_election(uint32_t k1, uint32_t k2)
{
   roam_config.k1 = k1;
   roam_config.k2 = k2;

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Configure counter for scan/elect/connect.
 *
 * Values will be ignored if they are below 0.
 *
 * @param max_scan_count number of scan's to be done in state HC before disconnecting.
 * @param max_elect_count number of nets to elect on connect refuced/failure.
 * @param max_connect_count number of reconnect trys.
 *
 * * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 */
int WiFiEngine_roam_configure_counters( 
      int max_scan_count, 
      int max_elect_count,
      int max_connect_count)
{
   if( max_scan_count >= 0)
      roam_config.max_scan_count = max_scan_count;

   if( max_elect_count >= 0)
      roam_config.max_connect_count = max_connect_count;

   if( max_connect_count >= 0)
      roam_config.max_elect_count = max_elect_count;

   DE_TRACE_INT3(TR_ROAM, "reconfiguring max_scan_count: %d max_connect_count: %d max_elect_count: %d\n", 
         roam_config.max_scan_count, roam_config.max_connect_count, roam_config.max_elect_count);

   return WIFI_ENGINE_SUCCESS;
}

static void reset_elected_net(void)
{
   if(roam_state.elected_net) {
      roam_state.elected_net->ref_cnt--;
      roam_state.elected_net = NULL;
   }
}

static void set_elected_net(WiFiEngine_net_t *net)
{
   roam_state.elected_net = net;
   roam_state.elected_net->ref_cnt++;
}

static void reset_last_connected_net(void)
{
   /* use broadcast MAC address to indicate that
    * last_connected_net is invalid
    */
   DE_MEMSET(&roam_state.last_connected_net, 0xff,
             sizeof(roam_state.last_connected_net));
   roam_state.last_connected_net_cabability = 0;
}

static void set_last_connected_net(WiFiEngine_net_t *net)
{
   m80211_mac_addr_t* p_bssId = &net->bssId_AP;

   DE_ASSERT(M80211_IS_UCAST(p_bssId));
   wei_copy_bssid(&roam_state.last_connected_net, p_bssId);
   roam_state.last_connected_net_cabability = net->bss_p->bss_description_p->capability_info;
}

/*
 * \brief (Re)configure periodic scan
 */
static int wei_roam_configure_scan(void)
{
   m80211_mac_addr_t bcast_bssid;
   channel_list_t channels;
   WiFiEngine_net_t *associated_net;

   wei_roam_reset_scan();

   if (!roam_config.enable_periodic_scan)
      return WIFI_ENGINE_FAILURE;

   associated_net = wei_netlist_get_current_net();

   if (!associated_net) {
      DE_TRACE_STATIC(TR_ROAM, "Tryed to configure roam when not associated\n");
      return WIFI_ENGINE_FAILURE;
   }

   WiFiEngine_GetRegionalChannels(&channels);

   DE_MEMSET(&bcast_bssid, 0xFF, sizeof bcast_bssid);

   if (WiFiEngine_AddScanJob( &roam_state.scan_job_id,
                              associated_net->bss_p->bss_description_p->ie.ssid,
                              bcast_bssid,
                              ACTIVE_SCAN,
                              channels,
                              ANY_MODE,
                              10 /* prio ??? */,
                              0  /* exclude ap */,
                              -1 /* filter */,
                              1  /* run_every_nth_period */,
                              NULL)
         != WIFI_ENGINE_SUCCESS)
   {
      roam_state.scan_job_id = NO_SCAN_JOB;
      roam_state.scan_state = SCAN_NOT_CONFIGURED;
      return WIFI_ENGINE_FAILURE;
   }

   roam_state.scan_state = SCAN_STOPPED;

   DE_TRACE_INT(TR_ROAM, "roam scan job configured (job id %d)\n",
                roam_state.scan_job_id);

   if (is_net_above_thresholds(associated_net, FALSE)) {
      wei_roam_stop_periodic_scan();
   } else {
      ROAM_TRACE_NET("Below thresholds", associated_net);
      wei_roam_start_periodic_scan();
   }

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Configure RSSI thresholds for initiating scanning and roaming.
 *
 * MIB triggers will be registered for the configured thresholds. When a
 * trigger is received scanning or roaming will be initiated. When the
 * thresholds are reconfigured the mib triggers will be re-registered.
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        RSSI mib triggers are received. When disabled, MIB triggers will not
 *        be registered.
 *
 * @param roam_thr When the current RSSI level falls below this threshold,
 *        then romaing will be initiated.
 *
 * @param scan_thr When the current RSSI level falls below this threshold,
 *        then scanning will be initiated.
 *
 * @param margin The elected AP must have RSSI and RSSI levels that are better
 *        than the thresholds plus some margin. The margin is used to avoid
 *        roaming "oscillations". The elected AP must have a RSSI level that is
 *        better than the RSSI roaming threshold plus this margin.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE on error.
 *
 */
int WiFiEngine_roam_configure_rssi_thr(bool_t enable,
                                       int32_t roam_thr,
                                       int32_t scan_thr,
                                       uint32_t margin)
{
   if (enable != TRUE && enable != FALSE)
      return WIFI_ENGINE_FAILURE;

   if (!(-100 <= roam_thr && roam_thr <= 0))
      return WIFI_ENGINE_FAILURE;

   if (!(-100 <= scan_thr && scan_thr <= 0))
      return WIFI_ENGINE_FAILURE;

   if (roam_thr > scan_thr)
      return WIFI_ENGINE_FAILURE;

   if (margin > 100)
      return WIFI_ENGINE_FAILURE;

   roam_config.rssi_enable = enable;
   roam_config.rssi_roam_thr = roam_thr;
   roam_config.rssi_scan_thr = scan_thr;
   roam_config.rssi_margin = margin;

   if (!roam_config.enable_roaming)
      return WIFI_ENGINE_SUCCESS;

   reconfigure_all_mib_triggers();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Configure SNR thresholds for initiating scanning and roaming.
 *
 * MIB triggers will be registered for the configured thresholds. When a
 * trigger is received scanning or roaming will be initiated. When the
 * thresholds are reconfigured the mib triggers will be re-registered.
 *
 * @param enable When enabled, roaming and scanning will be performed when
 *        SNR mib triggers are received. When disabled, MIB triggers will not
 *        be registered.
 *
 * @param roam_thr When the current SNR level falls below this threshold,
 *        then romaing will be initiated.
 *
 * @param scan_thr When the current SNR level falls below this threshold,
 *        then scanning will be initiated.
 *
 * @param margin The elected AP must have RSSI and SNR levels that are better
 *        than the thresholds plus some margin. The margin is used to avoid
 *        roaming "oscillations". The elected AP must have a SNR level that is
 *        better than the SNR roaming threshold plus this margin.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE on error.
 *
 */
int WiFiEngine_roam_configure_snr_thr(bool_t enable,
                                      uint32_t roam_thr,
                                      uint32_t scan_thr,
                                      uint32_t margin)
{
   if (enable != TRUE && enable != FALSE)
      return WIFI_ENGINE_FAILURE;

   if (roam_thr > 40)
      return WIFI_ENGINE_FAILURE;

   if (scan_thr > 40)
      return WIFI_ENGINE_FAILURE;

   if (roam_thr > scan_thr)
      return WIFI_ENGINE_FAILURE;

   if (margin > 40)
      return WIFI_ENGINE_FAILURE;

   roam_config.snr_enable = enable;
   roam_config.snr_roam_thr = roam_thr;
   roam_config.snr_scan_thr = scan_thr;
   roam_config.snr_margin = margin;

   if (!roam_config.enable_roaming)
      return WIFI_ENGINE_SUCCESS;

   reconfigure_all_mib_triggers();

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Add SSID to SSID filter list.
 *
 * Add's an SSID to the SSID filter list. If SSID filtering is on, then when
 * electing nets, only AP's that has an SSID that matches one of the SSID's in
 * the SSID list will be elected. Otherwise only the desired SSID in the
 * registry will be elected.
 *
 * @param ssid The SSID to add.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE if the SSID list is full.
 *
 */
int WiFiEngine_roam_add_ssid_filter(m80211_ie_ssid_t ssid)
{
   int i;

   for (i = 0; i < MAX_ALLOWED_SSID; i++) {
      if (roam_config.ssid[i].hdr.id == M80211_IE_ID_NOT_USED)
         break;
   }

   if (i == MAX_ALLOWED_SSID)
      return WIFI_ENGINE_FAILURE;

   DE_ASSERT(ssid.hdr.id == M80211_IE_ID_SSID);
   DE_MEMCPY(&roam_config.ssid[i], &ssid, sizeof(ssid));
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * \brief Remove SSID from SSID filter list.
 *
 * Remove an SSID from the SSID filter list. If SSID filtering is on, then when
 * electing nets, only AP's that has an SSID that matches one of the SSID's in
 * the SSID list will be elected. Otherwise only the desired SSID in the
 * registry will be elected.
 *
 * @param ssid The SSID to remove.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE if the SSID could not be found in the list.
 *
 */
int WiFiEngine_roam_del_ssid_filter(m80211_ie_ssid_t ssid)
{
   int i;

   for (i = 0; i < MAX_ALLOWED_SSID; i++) {
      if (wei_equal_ssid(roam_config.ssid[i], ssid))
         break;
   }

   if (i == MAX_ALLOWED_SSID)
      return WIFI_ENGINE_FAILURE;

   roam_config.ssid[i].hdr.id = M80211_IE_ID_NOT_USED;
   return WIFI_ENGINE_SUCCESS;
}


/*
 * \brief Start periodic scan
 */
static int wei_roam_start_periodic_scan(void)
{
   if (!roam_config.enable_periodic_scan)
      return WIFI_ENGINE_FAILURE;

   if (roam_state.scan_state == SCAN_STARTED)
      return WIFI_ENGINE_SUCCESS;

   DE_ASSERT(roam_state.scan_state != SCAN_NOT_CONFIGURED);

   if (WiFiEngine_SetScanJobState(roam_state.scan_job_id, TRUE, NULL)
         != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_STATIC(TR_ROAM, "Failed to start scan\n");
      return WIFI_ENGINE_FAILURE;
   }

   roam_state.scan_state = SCAN_STARTED;
   DE_TRACE_STATIC(TR_ROAM, "roam scan job started\n");
   return WIFI_ENGINE_SUCCESS;
}


/*
 * \brief Stop periodic scan
 */
static int wei_roam_stop_periodic_scan(void)
{
   if (!roam_config.enable_periodic_scan)
      return WIFI_ENGINE_FAILURE;

   if (roam_state.scan_state == SCAN_STOPPED)
      return WIFI_ENGINE_SUCCESS;

   DE_ASSERT(roam_state.scan_state != SCAN_NOT_CONFIGURED);

   if (WiFiEngine_SetScanJobState(roam_state.scan_job_id, FALSE, NULL)
         != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_STATIC(TR_ROAM, "Failed to stop scan\n");
      return WIFI_ENGINE_FAILURE;
   }

   DE_TRACE_STATIC(TR_ROAM, "roam scan job stopped\n");
   roam_state.scan_state = SCAN_STOPPED;
   return WIFI_ENGINE_SUCCESS;
}


/*
 * \brief Reset periodic scan
 *
 * Will remove any periodic scan managed by the roaming agent.
 *
 */
static void wei_roam_reset_scan(void)
{
   if (roam_state.scan_state == SCAN_NOT_CONFIGURED)
      return;

   wei_roam_stop_periodic_scan();

   WiFiEngine_RemoveScanJob(roam_state.scan_job_id, NULL);
   roam_state.scan_state = SCAN_NOT_CONFIGURED;
   roam_state.scan_job_id = NO_SCAN_JOB;
}


/*!
 * \brief Get the last roaming reason.
 *
 * When a roaming decision is made the reason for this decision is
 * stored and can be retrieved with this function. Note that this
 * function is intended for debugging/tracing and is not guaranteed
 * to be 100% synchronized with the roaming agent.
 *
 * @return The roaming reason code.
 */
we_roaming_reason_t WiFiEngine_GetRoamingReason(void)
{
   return roam_state.last_roaming_reason;
}


/*!
 * \brief Enable or disable a periodic scan job.
 *
 * Configure a periodic scan job to be managed by the roaming
 * agent. It is used for probe requests with a specific ssid and is
 * started and stopped by mib triggers.
 *
 * @param enable will enable the periodic scan job.
 *        If the driver is connected it will also reconfigure
 *        the scan job.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE if parameters are invalid.
 *
 */
int WiFiEngine_roam_enable_periodic_scan(bool_t enable)
{
   DE_TRACE_INT(TR_ROAM, "ENTRY enable=%d\n", enable);

   if (enable != FALSE && enable != TRUE)
      return WIFI_ENGINE_FAILURE;

   roam_config.enable_periodic_scan = enable;

   if (!roam_config.enable_roaming)
      return WIFI_ENGINE_FAILURE;

   if (roam_state.state==STATE_CONNECTED) {
      wei_roam_configure_scan();
   }

   return WIFI_ENGINE_SUCCESS;
}

/* Measure RSSI/SNR/... on data frames or beacons */
int WiFiEngine_roam_measure_on_data(bool_t enable)
{
   DE_TRACE_INT(TR_ROAM, "ENTRY enable=%d\n", enable);

   if (enable != FALSE && enable != TRUE)
      return WIFI_ENGINE_FAILURE;

   if(roam_config.measure_on_data == enable) {
      // NOP
      return WIFI_ENGINE_SUCCESS;
   }

   roam_config.measure_on_data = enable;
   refresh_lq(TRUE);
   reconfigure_all_mib_triggers();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * \brief Enable or disable roaming.
 *
 * When roaming is disabled, no mib triggers will be registered and no
 * roaming will be performed due to other events.
 *
 * @param enable Set to TRUE to enable roaming, FALSE to disable.
 *
 */
int WiFiEngine_roam_enable(bool_t enable)
{
   DE_TRACE_INT(TR_ROAM, "ENTRY enable=%d\n", enable);

   if (enable != FALSE && enable != TRUE)
      return WIFI_ENGINE_FAILURE;

   roam_config.enable_roaming = enable;
   reconfigure_all_mib_triggers();
   wei_roam_configure_scan();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * \brief Check if roaming is in progress.
 *
 * When roaming is in progress a call to this function will return 1,
 * otherwise it will return 0.
 *
 * @return
 *  - 1 if roaming is in progress (we're between connects).
 *  - 0 if roaming is not in progress.
 */
int WiFiEngine_is_roaming_in_progress(void)
{
   if (roam_state.state==STATE_CONNECTED)
      return 0;

   if (roam_state.state==STATE_DISCONNECTED)
      return 0;

   return 1;
}

int is_roaming_active(void)
{
   if (roam_state.state==STATE_DISCONNECTED)
      return 0;

   return 1;
}


/*!
 * \brief Configure roaming filters.
 *
 * Enables and disables some rules that are used during net election.
 *
 * @param enable_blacklist Set to TRUE to enable, FALSE to disable.
 *        When blacklist is enabled, any AP's that we failed to connect to
 *        will be filtered out during the net election step.
 *
 * @param enable_wmm Set to TRUE to enable, FALSE to disable.
 *        When WMM filtering is enabled, any AP's that does not support WMM
 *        will be filter out if WMM is enabled on the STA.
 *
 * @param enable_ssid Set to TRUE to enable, FALSE to disable.
 *        When SSID filtering is enabled, any AP's that does not match any of
 *        the ssid's in the ssid list fill be filtered out.
 *        SSID's can be added and deleted through WiFiEngine_add_ssid_filter()
 *        and WiFiEngine_del_ssid_filter().
 *        When FALSE, we'll only roam to the SSID specified in the registry.
 *
 * @return
 *        - WIFI_ENGINE_SUCCESS on success,
 *        - WIFI_ENGINE_FAILURE if parameters are invalid.
 *
 */
int WiFiEngine_roam_configure_filter(bool_t enable_blacklist,
                                     bool_t enable_wmm,
                                     bool_t enable_ssid)
{
   if (enable_blacklist != FALSE && enable_blacklist != TRUE)
      return WIFI_ENGINE_FAILURE;

   if (enable_wmm != FALSE && enable_wmm != TRUE)
      return WIFI_ENGINE_FAILURE;

   if (enable_ssid != FALSE && enable_ssid != TRUE)
      return WIFI_ENGINE_FAILURE;

   roam_config.enable_blacklist_filter = enable_blacklist;
   roam_config.enable_wmm_filter = enable_wmm;
   roam_config.enable_ssid_filter = enable_ssid;
   return WIFI_ENGINE_SUCCESS;
}


static void refresh_lq(bool_t poll)
{
   /* Don't do any thing if we are disconnected.  */
   if (roam_state.state!=STATE_CONNECTED)
      return;

   /* Async call, this value is from the last async mib */
   if (roam_config.measure_on_data) {
      /* order is important if we don't want to register handlers for both rssi and snr */
      if (poll) {
         WiFiEngine_GetDataRSSI(&roam_state.rssi, poll);
         if(roam_config.snr_enable == TRUE) WiFiEngine_GetDataSNR(&roam_state.snr, poll);
      } else {
         if(roam_config.snr_enable == TRUE) WiFiEngine_GetDataSNR(&roam_state.snr, poll);
         WiFiEngine_GetDataRSSI(&roam_state.rssi, poll);
      }
   } else {
      if (poll) {
         WiFiEngine_GetRSSI(&roam_state.rssi, poll);
         if(roam_config.snr_enable == TRUE) WiFiEngine_GetSNR(&roam_state.snr, poll);
      } else {
         if(roam_config.snr_enable == TRUE) WiFiEngine_GetSNR(&roam_state.snr, poll);
         WiFiEngine_GetRSSI(&roam_state.rssi, poll);
      }
   }
}



/******************************** STATE FUNCTIONS *****************************/

/* Use this to increase robustness. This will enable the roaming agent to
 * get back up on its feat again if the state machine for some reason is
 * in the wrong state. May for example happen if IF_UNPLUG/IF_PLUG did not
 * handle shutdown in a correct way
 */
#if 0
static void reset_state_on_ev_connect_success(EV_ARGS)
{
   roam_event_t ev;

   EVENT_ENTRY

   roam_state.state = STATE_DISCONNECTED;

   ev = EVENT_CONNECT_SUCCESS;
   state_table[roam_state.state][ev](ev);
}
#endif

/* Main Engine */
void change_roam_state(roam_event_t event, roam_state_t next_state) {
   DE_TRACE_STATIC4(TR_ROAM, "roam_state change %s:%s => %s\n", state2str(roam_state.state),
                    event2str(event), state2str(next_state));
   state_table[roam_state.state][EVENT_EXIT](EVENT_EXIT);
   roam_state.state = next_state;
   state_table[roam_state.state][EVENT_INIT](EVENT_INIT);
}

void event_ignore(EV_ARGS) {
   //EVENT_ENTRY
   //DE_TRACE_STATIC3(TR_ROAM, "ignoring %s %s\n", state2str(roam_state.state), event2str(event));
}

void event_assert(EV_ARGS) {
   EVENT_ENTRY
   /* Use this to infor about logical errors or states that may not be accounted for yet. */
   /* in production mode this function should do nothing or state recovery check */
   DE_BUG_ON(1, "ASSERT: [%s] NOT ALLOWED IN [%s]\n",
             event2str(event), state2str(roam_state.state));
}

void wei_roam_notify_user_disconnect(void)
{
   if(roam_state.state == STATE_DISCONNECTED) return;

   DE_TRACE_STATIC(TR_ROAM, "User requested disconnect\n");

   wei_shutdown_roam();
}

static void state_disconnected_ev_init(EV_ARGS)
{
   EVENT_ENTRY
   DE_TRACE_STATIC(TR_ROAM, "indicate disconnect\n");
   /* do some cleanup */
   wei_roam_reset_scan();
   reset_last_connected_net();
   DriverEnvironment_indicate(WE_IND_ROAM_BAIL, NULL, 0);
   stop_all_mib_triggers();
}

static void state_disconnected_ev_connect_success(EV_ARGS)
{
   /* The connect attempt was not triggered by the roaming agent */
   EVENT_ENTRY

   /* roaming must be enabled before initial connect */
   if (!roam_config.enable_roaming)
      return;
   
   if(is_ibss(wei_netlist_get_current_net()))
      return;

   set_last_connected_net(wei_netlist_get_current_net());
   clear_all_tmp_blacklisted();
   wei_roam_configure_scan();
   change_roam_state(event, STATE_CONNECTED);
   /* do this after STATE_CONNECTED to start mib triggers */
   reconfigure_all_mib_triggers();
}

static void state_connected_ev_init(EV_ARGS)
{
   WiFiEngine_net_t* net;
   int status;

   EVENT_ENTRY

   roam_state.rssi_update_pending = 0;

   /* TODO: move to _CM_ */
   status = we_ind_cond_register( 
            &roam_state.core_dump_h, 
            WE_IND_CORE_DUMP_START, "WE_IND_CORE_DUMP_START", 
            wei_roam_core_dump_ind, NULL, 0, NULL);
   DE_ASSERT(status == WIFI_ENGINE_SUCCESS);

   /* Reset rate state */
   roam_state.rate_roam_done = TRUE;
   roam_state.rate_scan_done = TRUE;

   reset_elected_net();

   /* start/stop periodic scan after successful roam */
   net = wei_netlist_get_current_net();
   if (is_net_above_thresholds(net, FALSE)) {
      wei_roam_stop_periodic_scan();
   } else {
      ROAM_TRACE_NET("Below thresholds", net);
      wei_roam_start_periodic_scan();
   }

   roam_state.connected_at_tick = DriverEnvironment_GetTicks();
}

static void state_connected_ev_exit(EV_ARGS)
{
   EVENT_ENTRY
   roam_state.scan_count = roam_config.max_scan_count;
   roam_state.elect_count = roam_config.max_elect_count;
   roam_state.connect_count = roam_config.max_connect_count;

   /* in case elective failes */
   roam_state.last_roaming_reason = WE_ROAM_REASON_CONNECT_FAIL;
}

static void state_connected_ev_disconnecting(EV_ARGS)
{
   we_con_lost_s lost;

   EVENT_ENTRY

   if(!roam_state.elected_net) {
      DE_TRACE_STATIC(TR_ROAM, "HARD CUT ROAMING START\n");
   }

   WiFiEngine_get_last_con_lost_reason(&lost);
   switch(lost.type) {
   case WE_DISCONNECT_NOT_PRESENT:
      roam_state.last_roaming_reason = WE_ROAM_REASON_UNSPECIFIED;
      break;
   case WE_DISCONNECT_DEAUTH:
   case WE_DISCONNECT_DEAUTH_IND:
   case WE_DISCONNECT_DISASS:
   case WE_DISCONNECT_DISASS_IND:
   case WE_DISCONNECT_USER:
      roam_state.last_roaming_reason = WE_ROAM_REASON_DISCONNECTED;
      break;
   case WE_DISCONNECT_PEER_STATUS_TX_FAILED:
      roam_state.last_roaming_reason = WE_ROAM_REASON_TX_FAIL;
      break;
   case WE_DISCONNECT_PEER_STATUS_RX_BEACON_FAILED:
      roam_state.last_roaming_reason = WE_ROAM_REASON_BEACON;
      break;
   case WE_DISCONNECT_PEER_STATUS_ROUNDTRIP_FAILED:
   case WE_DISCONNECT_PEER_STATUS_RESTARTED:
   case WE_DISCONNECT_PEER_STATUS_INCOMPATIBLE:
      roam_state.last_roaming_reason = WE_ROAM_REASON_PEER_STATUS;
      break;

   /* should not cause disconnecting event */
   case WE_DISCONNECT_PEER_STATUS_TX_FAILED_WARNING:
   case WE_DISCONNECT_PEER_STATUS_RX_BEACON_FAILED_WARNING:
      DE_ASSERT(FALSE);
      break;
   }

   DriverEnvironment_indicate(WE_IND_ROAMING_START, NULL, 0);

   if(!roam_state.elected_net) {
      WiFiEngine_net_t* net = elect_net(0);
      if (net) set_elected_net(net);
   }
#if (DE_CCX_ROAMING == CFG_INCLUDED)
	if (wifiEngineState.ccxState.KRK_set) {
		ccxnet = roam_state.elected_net; //change for reassociation
	}
#endif
   change_roam_state(event, STATE_DISCONNECTING);
}

static void state_connected_ev_disconnected(EV_ARGS)
{
   EVENT_ENTRY
   DE_ASSERT(FALSE);
}

static void state_connected_ev_scan_ind(EV_ARGS)
{
#ifdef MISSING_SCAN_COMPLETE_WORKAROUND
   int status;
   /* trigger a sequence of cb functions to update rssi/snr values
      and finally call roam() */
   if (roam_config.measure_on_data) {
      status = we_ind_cond_register(&roam_state.rssi_h, 
                WE_IND_MIB_DOT11RSSI_DATA, 
                "WE_IND_MIB_DOT11RSSI_DATA", 
                state_elective_ev_rssi_cb, NULL, 1, NULL);
      DE_ASSERT(status == WIFI_ENGINE_SUCCESS);
   } else {
      status = we_ind_cond_register(&roam_state.rssi_h, 
                WE_IND_MIB_DOT11RSSI, 
                "WE_IND_MIB_DOT11RSSI", 
                state_elective_ev_rssi_cb, NULL, 1, NULL);
      DE_ASSERT(status == WIFI_ENGINE_SUCCESS);
   }

   
#endif
}

static void state_connected_ev_scan_complete(EV_ARGS)
{
   int status;
   EVENT_ENTRY
   // update RSSI -> elect -> roam

   /* No need to update rssi/snr if no candidates */
   if (!elect_net(0))
      return;

   /* no need to query for rssi more then once */
   if (roam_state.rssi_update_pending)
      return;
   roam_state.rssi_update_pending = 1;

   /* trigger a sequence of cb functions to update rssi/snr values
      and finally call roam() */
   if (roam_config.measure_on_data) {
      status = we_ind_cond_register(&roam_state.rssi_h, 
               WE_IND_MIB_DOT11RSSI_DATA, 
                "WE_IND_MIB_DOT11RSSI_DATA",
                state_elective_ev_rssi_cb, NULL, 1, NULL);
      DE_ASSERT(status == WIFI_ENGINE_SUCCESS);
   } else {
      status = we_ind_cond_register(&roam_state.rssi_h, 
                WE_IND_MIB_DOT11RSSI, 
                "WE_IND_MIB_DOT11RSSI",
                state_elective_ev_rssi_cb, NULL, 1, NULL);
      DE_ASSERT(status == WIFI_ENGINE_SUCCESS);
   }
   refresh_lq(TRUE);
}

static void state_connected_ev_core_dump(EV_ARGS)
{
   EVENT_ENTRY

   roam_state.last_roaming_reason = WE_ROAM_REASON_CORE_DUMP;
   DriverEnvironment_indicate(WE_IND_ROAMING_START, NULL, 0);

   if(0) {
      change_roam_state(event, STATE_ROAMING);
      /* wait for connect_(success|fail|refused) */
   } else {
      change_roam_state(event, STATE_DISCONNECTING);
      /* wait for event disconnected after coredump complete */
   }
}

static void state_disconnecting_ev_disconnected(EV_ARGS)
{
   EVENT_ENTRY


   if(roam_state.elect_count==0) {
      change_roam_state(event, STATE_DISCONNECTED);
      return;
   }

   if(roam_state.elected_net) {
      change_roam_state(event, STATE_ROAMING);
      return;
   }
   if (roam_state.scan_count) {
      change_roam_state(event, STATE_HC);
      return;
   }
   change_roam_state(event, STATE_DISCONNECTED);
}

static void state_disconnecting_ev_core_dump(EV_ARGS)
{
   EVENT_ENTRY
   /* wait for event disconnected after coredump complete */
}

static void state_hc_ev_init(EV_ARGS)
{
   WiFiEngine_net_t* net;

   EVENT_ENTRY

   if(roam_state.elected_net) {
      change_roam_state(event, STATE_ROAMING);
      return;
   }

   net = elect_net(0);
   if (net) {
      set_elected_net(net);
      change_roam_state(event, STATE_ROAMING);
      return;
   }

   if (roam_state.scan_count) {
      scan();
      return;
   }

   /* not waiting for scan result and no net to roam to */
   change_roam_state(event, STATE_DISCONNECTED);
}

static void state_hc_ev_scan_ind(EV_ARGS)
{
   WiFiEngine_net_t* net;

   //EVENT_ENTRY

   if(roam_state.elected_net) return;

   net = elect_net(0);
   if (net) {
      // stop scan job here if possible
      set_elected_net(net);
      change_roam_state(event, STATE_ROAMING);
      return;
   }
}

static void state_hc_ev_scan_complete(EV_ARGS)
{
   WiFiEngine_net_t* net;

   EVENT_ENTRY

   roam_state.scan_count--;

   if(roam_state.elected_net) {
      /* may have been set in state_hc_ev_scan_ind but not yet transendent to STATE_ROAMING */
      DE_TRACE_STATIC(TR_WARN, "net already elected, ignoring to prevent race\n");
      return;
   }
   
   net = elect_net(0);
   if (net) {
      set_elected_net(net);
      change_roam_state(event, STATE_ROAMING);
      return;
   }

   if (roam_state.scan_count) {
      DE_TRACE_INT(TR_ROAM, "scan retry (%d)\n", 1 + roam_config.max_scan_count - roam_state.scan_count);
      scan();
      return;
   }

   DE_TRACE_INT(TR_ROAM, "No more scanning (max: %d)\n", roam_config.max_scan_count);
   change_roam_state(event, STATE_DISCONNECTED);
}

static void state_hc_ev_core_dump(EV_ARGS)
{
   EVENT_ENTRY
   change_roam_state(event, STATE_DISCONNECTING);
   /* wait for event disconnected after coredump complete */
}

/*
 * In this state we expect that a connect attempt is in progress.
 * waiting for connect_success,fail,refuced
*/
static void state_roaming_ev_init(EV_ARGS)
{
   EVENT_ENTRY

   DE_TRACE_INT2(TR_ROAM, "elect_count: (%d) connect_count (%d)\n",
                  1 + roam_config.max_elect_count - roam_state.elect_count,
                  1 + roam_config.max_connect_count - roam_state.connect_count);

   DE_ASSERT(roam_state.elected_net);
   connect(roam_state.elected_net);
}

static void state_roaming_ev_connect_success(EV_ARGS)
{
   EVENT_ENTRY

   ROAM_TRACE_NET("Connected", wei_netlist_get_current_net());
   change_roam_state(event, STATE_CONNECTED);
   reset_elected_net();
   set_last_connected_net(wei_netlist_get_current_net());
   clear_all_tmp_blacklisted();
   DriverEnvironment_indicate(WE_IND_ROAMING_COMPLETE, NULL, 0);
}

/* Don't bail on first connection failed. Try to reconnect and on final
 * reconnect blacklist and re-elect. Repeat until all candidates have
 * been exhausted or max_elect_count has been reached.
 * Finaly indicate connection lost
 */
static void state_roaming_ev_connect_failed(EV_ARGS)
{
   WiFiEngine_net_t* net;

   EVENT_ENTRY

   DE_ASSERT(roam_state.connect_count>0);
   roam_state.connect_count--;

   /* TODO: check reason for failure; may not make any sence to try again */

   net = roam_state.elected_net;
   DE_ASSERT(net);
   if (roam_state.connect_count==0) {
      ROAM_TRACE_NET("Blacklist until connect success", net);
      net->net_statistics.blacklisted = BLACKLIST_TEMPORARILY;
      /* Set up for new elected net or if scan finds a new net */
      roam_state.connect_count = roam_config.max_connect_count;
      reset_elected_net();
      if(roam_state.elect_count>0) {
         net = elect_net(0);
         if(net) {
            roam_state.elect_count--;
            set_elected_net(net);
         }
      }
   }

   /* elected_net set, wait for disconnected ev and reconnect */
   change_roam_state(event, STATE_DISCONNECTING);
}


static void state_roaming_ev_disconnecting(EV_ARGS)
{
   EVENT_ENTRY
   state_roaming_ev_connect_failed(event);
}

static void state_roaming_ev_disconnected(EV_ARGS)
{
   EVENT_ENTRY
   /* waiting for a connect_success or connect_failed */
   DE_ASSERT(FALSE);
}

static void state_roaming_ev_core_dump(EV_ARGS)
{
   EVENT_ENTRY
   change_roam_state(event, STATE_DISCONNECTING);
   /* wait for event disconnected after coredump complete */
}


static void state_roaming_ev_connect_refuced(EV_ARGS)
{
   EVENT_ENTRY
   state_roaming_ev_connect_failed(event);
}

/* ============================================ */
/* Driving functions for the main state machine */
/* ============================================ */

int wei_roam_notify_connected(void)
{
   int first = is_roaming_active();
   roam_event_t ev = EVENT_CONNECT_SUCCESS;
   state_table[roam_state.state][ev](ev);

   if(!first && is_roaming_active())
      return FALSE;

#if (DE_CCX_ROAMING == CFG_INCLUDED)
   ccxnet = NULL;
#endif
 
   return is_roaming_active();
}

int wei_roam_notify_connect_failed(void)
{
   roam_event_t ev = EVENT_CONNECT_FAILED;
   state_table[roam_state.state][ev](ev);

   return is_roaming_active();
}

int wei_roam_notify_connect_refuced(void)
{
   roam_event_t ev = EVENT_CONNECT_REFUCED;
   state_table[roam_state.state][ev](ev);

   return is_roaming_active();
}

void wei_roam_notify_scan_ind(void)
{
   roam_event_t ev = EVENT_SCAN_IND;
   state_table[roam_state.state][ev](ev);
}

void wei_roam_notify_scan_failed(void)
{
   roam_event_t ev = EVENT_SCAN_COMPLETE;
   DE_TRACE_STATIC(TR_ROAM, "RECEIVED SCAN FAIL: treating it as a scan_complete for now!\n");
   state_table[roam_state.state][ev](ev);
}

void wei_roam_notify_scan_complete(void)
{
   roam_event_t ev = EVENT_SCAN_COMPLETE;
   state_table[roam_state.state][ev](ev);
}

int wei_roam_notify_disconnecting(void)
{
   roam_event_t ev = EVENT_SM_DISCONNECTING;
   state_table[roam_state.state][ev](ev);

   return is_roaming_active();
}


/* Should be called when WE_IND_80211_DISCONNECTED is about to be signalled to
 * driverenv.
 *
 * If roaming is enabled, WE_IND_80211_DISCONNECTED has not yet been signalled
 * to driverenv. We'll only issue this signal if we fail to roam since we don't
 * want to mess with the IP stack and upper layers.
 */
int wei_roam_notify_disconnected(void)
{
   int active = is_roaming_active();
   roam_event_t ev = EVENT_DISCONNECTED;
   roam_state.last_roaming_reason = WE_ROAM_REASON_UNSPECIFIED;
   state_table[roam_state.state][ev](ev);

   /* if active termination event will be controlled by WE_IND_ROAM_BAIL */
   return active;
}

static void wei_roam_core_dump_ind(wi_msg_param_t param, void *priv)
{
   roam_event_t ev = EVENT_CORE_DUMP;
   state_table[roam_state.state][ev](ev);
   return;
}


/* not really a state but a substate of state_connected_ev_... */
static void state_elective_ev_rssi_cb(wi_msg_param_t param, void *priv)
{
   WiFiEngine_net_t* net;
   DE_TRACE_STATIC(TR_ROAM, "ENTRY\n");

   /* indication callback will be deregisted by we_ind_send */

   roam_state.rssi_update_pending = 0;

   /* check so we are still connected */
   if (roam_state.state!=STATE_CONNECTED)
      return;

   if (!is_lq_above_thresholds(FALSE))
   {
      net = elect_net(1);
      if (net) {
         /* check so we are still connected */
         if (roam_state.state==STATE_CONNECTED) {
            DE_TRACE_STATIC(TR_ROAM, "ELECTIVE ROAMING START\n");
            set_elected_net(net);
            DriverEnvironment_indicate(WE_IND_ROAMING_START, NULL, 0);
            disconnect();
            change_roam_state(EVENT_SCAN_COMPLETE, STATE_DISCONNECTING);
         }
      }
   }
}


/* Should be called when we receive DATA_IND's. Currently it's not really used
 * for anything.
 */
void wei_roam_notify_data_ind(void)
{
   return;
}


/* Should be called when we receive DATA_CFM's */
void wei_roam_notify_data_cfm(m80211_std_rate_encoding_t rate_used)
{
   if (!roam_config.rate_enable)
      return;

   /* Clear roam done when a rate above the threshold is used.
    * We'll now roam the next time the rate falls below the threshold.
    */
   if (roam_state.rate_roam_done &&
         rate_used > roam_config.rate_roam_thr) {
      DE_TRACE_INT(TR_ROAM, "Clear roam_done, rate=%d\n", rate_used);
      roam_state.rate_roam_done = FALSE;
   }

   /* Clear scan done when a rate above the threshold is used
    * We'll now scan the next time the rate falls below the threshold.
    */
   if (roam_state.rate_scan_done &&
         rate_used > roam_config.rate_scan_thr) {
      DE_TRACE_INT(TR_ROAM, "Clear scan_done, rate=%d\n", rate_used);
      roam_state.rate_scan_done = FALSE;
   }

   /* Roam if the original rate falls below the threshold */
   if (!roam_state.rate_roam_done &&
         rate_used < roam_config.rate_roam_thr) {
      DE_TRACE_INT(TR_ROAM, "Rate falls below threshold (roam) rate=%d\n", rate_used);
      roam_state.last_roaming_reason = WE_ROAM_REASON_RATE;
      /* FIXME */
      //roam(elect_net(1));
      roam_state.rate_roam_done = TRUE;
   }

   /* Roam if the original rate falls below the threshold */
   if (!roam_state.rate_scan_done &&
         rate_used < roam_config.rate_scan_thr) {
      DE_TRACE_INT(TR_ROAM, "Rate falls below threshold (scan) rate=%d\n", rate_used);
      scan();
      roam_state.rate_scan_done = TRUE;
   }
}


static void update_scan_based_on_triggers(void)
{
   if (FALSE == roam_state.rssi_scan &&
         FALSE == roam_state.snr_scan &&
         FALSE == roam_state.ds_scan )
   {
      wei_roam_stop_periodic_scan();
   } else {
      wei_roam_start_periodic_scan();
   }
}


/* Will be called when any registered mib trigger reaches its configured
 * threshold.
 *
 * TODO: move into state machine
 */
static int notify_mib_trigger(void *data, size_t len)
{
   we_vir_trig_data_t* trig_data = (we_vir_trig_data_t*) data;
   DE_ASSERT(trig_data);

   DE_TRACE_STATIC(TR_ROAM, "ENTRY\n");

   if (trig_data->type == WE_TRIG_TYPE_CANCEL) {
      DE_TRACE_STATIC(TR_ROAM, "Trigger was cancelled\n");
      return 0;
   }

   if (trig_data->trig_id == roam_state.rssi_rising_scan_trig_id) {
      DE_TRACE_INT(TR_ROAM, "Mib trigger rising RSSI (stop scan), value=%d\n",
                   trig_data->value);
      roam_state.rssi_scan = FALSE;
   }

   if (trig_data->trig_id == roam_state.rssi_falling_scan_trig_id) {
      DE_TRACE_INT(TR_ROAM, "Mib trigger falling RSSI (start scan), value=%d\n",
                   trig_data->value);
      roam_state.rssi_scan = TRUE;
   }

   if (trig_data->trig_id == roam_state.snr_rising_scan_trig_id) {
      DE_TRACE_INT(TR_ROAM, "Mib trigger rising SNR (stop scan), value=%d\n",
                   trig_data->value);
      roam_state.snr_scan = FALSE;
   }

   if (trig_data->trig_id == roam_state.snr_falling_scan_trig_id) {
      DE_TRACE_INT(TR_ROAM, "Mib trigger falling SNR (start scan), value=%d\n",
                   trig_data->value);
      roam_state.snr_scan = TRUE;
   }

   if (trig_data->trig_id == roam_state.ds_rising_scan_trig_id) {
      DE_TRACE_INT(TR_ROAM, "Mib trigger rising DS (start scan), value=%d\n",
                   trig_data->value);
      roam_state.ds_scan = TRUE;
   }

   if (trig_data->trig_id == roam_state.ds_falling_scan_trig_id) {
      DE_TRACE_INT(TR_ROAM, "Mib trigger falling DS (stop scan), value=%d\n",
                   trig_data->value);
      roam_state.ds_scan = FALSE;
   }

   update_scan_based_on_triggers();

   return 0;
}


/* Do some extra scan */
static void scan(void)
{
   DE_TRACE_INT(TR_ROAM, "Trig scan job %d\n",
                roam_state.scan_state!=SCAN_NOT_CONFIGURED ?  roam_state.scan_job_id : 0);

   /* Scan quickly when not connected */
   if (wifiEngineState.main_state == driverConnected)
   {
      if (roam_state.scan_state!=SCAN_NOT_CONFIGURED)
         WiFiEngine_TriggerScanJob(roam_state.scan_job_id, DEFAULT_CHANNEL_INTERVAL);
      else
         WiFiEngine_TriggerScanJob(0, DEFAULT_CHANNEL_INTERVAL);
   }
   else
   {
      if (roam_state.scan_state!=SCAN_NOT_CONFIGURED)
         WiFiEngine_TriggerScanJob(roam_state.scan_job_id, 0);
      else
         WiFiEngine_TriggerScanJob(0, 0);
   }
}

/* Select best AP
 *
 * If in STATE_CONNECTED only nets that are above thresholds will be selected,
 * otherwise the best candidate in the netlist will do.
 */
static WiFiEngine_net_t* elect_net(int check_thresholds)
{
   WiFiEngine_net_t* candidate_net;
   wei_netlist_state_t *netlist;
   WiFiEngine_net_t* elected_net = NULL;
   uint32_t candidate_link_quality, elected_link_quality;

   netlist = wei_netlist_get_net_state();

   for (candidate_net = netlist->active_list_ref;
         candidate_net != NULL;
         candidate_net = WEI_GET_NEXT_LIST_ENTRY_NAMED(candidate_net,
                         WiFiEngine_net_t,
                         active_list)) {

      if (filter_out_net(candidate_net))
         continue;

      /* Candidate should be better than roaming thresholds, however if we
       * are disconnected then anything will do.
       */
      if(check_thresholds &&
         roam_state.state==STATE_CONNECTED &&
         !is_net_lq_above(candidate_net,
                  roam_state.rssi,
                  roam_state.snr,
                  TRUE)) {
         DE_TRACE_STATIC(TR_ROAM, "elective: candidate net not above thresholds\n");
         continue;
      }

      /* First match */
      if (elected_net == NULL) {
         elected_net = candidate_net;
         continue;
      }

      /* Keep the best net */
      candidate_link_quality = calc_link_quality(candidate_net);
      elected_link_quality = calc_link_quality(elected_net);

      if (candidate_link_quality > elected_link_quality)
         elected_net = candidate_net;
   }

   ROAM_TRACE_NET("elected_net", elected_net);
   return elected_net;
}


/* Filter out AP's based on roaming configuration */
static bool_t filter_out_net(WiFiEngine_net_t* net)
{
   if (is_ibss(net))
      return TRUE;

   if (!ssid_matches(net))
      return TRUE;

   if (is_privacy_changed(net))
      return TRUE;

   if (is_blacklisted(net))
      return TRUE;

   if (!is_encryption_allowed(net))
      return TRUE;

   if (!is_wmm_supported(net))
      return TRUE;

   if (is_self(net))
      return TRUE;

   if (is_old(net))
      return TRUE;

   if (is_down(net))
      return TRUE;

#if (DE_CCX == CFG_INCLUDED)
   if (is_ccx_aac_invalid(net))
      return TRUE;
#endif
   return FALSE;
}


/* Some comparison value for the link quality */
static uint32_t calc_link_quality(WiFiEngine_net_t* net)
{
   int score = 0;

   if (net->bss_p->rssi_info < -100) {
      DE_TRACE_STATIC(TR_ROAM, "Invalid rssi value\n");
      net->bss_p->rssi_info = -100;
   }
   else if (net->bss_p->rssi_info > 0) {
      DE_TRACE_STATIC(TR_ROAM, "Invalid rssi value\n");
      net->bss_p->rssi_info = 0;
   }

   /* Assume RSSI has range [-100, 0] */
   if (roam_config.rssi_enable)
      score += roam_config.k1 * (100 + net->bss_p->rssi_info);

   if (roam_config.snr_enable)
      score += roam_config.k2 * (net->bss_p->snr_info);

   return score;
}


/* Actual work */
static bool_t is_levels_above_thresholds(
   int rssi,
   int snr,
   bool_t with_margin)
{
   int32_t rssi_margin = 0;
   int32_t snr_margin = 0;

   if (with_margin) {
      rssi_margin = roam_config.rssi_margin;
      snr_margin = roam_config.snr_margin;
   }

   if (roam_config.rssi_enable &&
         rssi < roam_config.rssi_roam_thr + rssi_margin) {
      return FALSE;
   }

   if (roam_config.snr_enable &&
         snr > 0 &&
         snr < roam_config.snr_roam_thr + snr_margin) {
      return FALSE;
   }

   return TRUE;
}

static bool_t is_net_lq_above(WiFiEngine_net_t* net, 
      int rssi, int snr, bool_t with_margin)
{
   int net_snr, net_rssi;

   net_snr = net->bss_p->snr_info;
   net_rssi = net->bss_p->rssi_info;

   if (with_margin) {
      net_rssi -= roam_config.rssi_margin;
      net_snr -= roam_config.snr_margin;
   }

   DE_TRACE_INT4(TR_ROAM_HIGH_RES, "candidate rssi: %d > %d snr: %d > %d \n", 
   net_rssi, rssi, net_snr, snr);

   if (roam_config.rssi_enable && 
         net_rssi < rssi) {
      return FALSE;
   }

   if (roam_config.snr_enable &&
         net_snr > 0 && 
         net_snr < snr) {
      return FALSE;
   }

   return TRUE;
}

/*
 * Use this to filter out AP's that has lower RSSI and SNR levels than our
 * mib trigger thresholds.
 */
static bool_t is_net_above_thresholds(WiFiEngine_net_t* net, bool_t with_margin)
{
   if (is_levels_above_thresholds( net->bss_p->rssi_info, net->bss_p->snr_info,
                                   with_margin))
   {
      ROAM_TRACE_NET("Above thresholds", net);
      return TRUE;
   }

   ROAM_TRACE_NET("Below thresholds", net);
   return FALSE;
}


static void connect(WiFiEngine_net_t* net)
{
   ROAM_TRACE_NET("Roaming to", net);

   /* will be reassigned on connect */
   wei_netlist_remove_current_net();
   WiFiEngine_Connect(net);
}

/* Disconnect from AP and do some bookkeeping */
static void disconnect(void)
{
   WiFiEngine_bss_type_t type;
   int status;

   DE_TRACE_STATIC(TR_ROAM, "ENTRY\n");

   /* protect SM from queuing a disconnect before connecting,
      a bug in SM can cause this disconnect to be issued after 
        we have become connected causing a hc-roam loop */
   status = WiFiEngine_GetBSSType(&type);

   DE_ASSERT(status);
   if (type == M80211_CAPABILITY_ESS)
      WiFiEngine_Deauthenticate();
   else
      WiFiEngine_LeaveIbss();
}


static bool_t is_privacy_changed(WiFiEngine_net_t* net)
{
   uint16_t elected_privacy = 0;
   uint16_t last_net_privacy = 0;
   
   DE_ASSERT(net);

   elected_privacy = net->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_PRIVACY; 
   last_net_privacy = roam_state.last_connected_net_cabability & M80211_CAPABILITY_PRIVACY;
   if (last_net_privacy != elected_privacy)
      return TRUE;

   return FALSE;
}
static bool_t ssid_matches(WiFiEngine_net_t* net)
{
   int i;
   DE_ASSERT(net);

   if (!roam_config.enable_ssid_filter) {
      bool_t match;
      match = wei_desired_ssid(net->bss_p->bss_description_p->ie.ssid);
      if (match) {
         DE_TRACE_STATIC(TR_ROAM_HIGH_RES, "Desired SSID matched\n");
      }
      return match;
   }

   for (i = 0; i < MAX_ALLOWED_SSID; i++) {
      if (wei_equal_ssid(net->bss_p->bss_description_p->ie.ssid, roam_config.ssid[i]))
         return TRUE;
   }

   return FALSE;
}

static void clear_all_tmp_blacklisted(void) {
   WiFiEngine_net_t* net;
   wei_netlist_state_t *netlist;

   netlist = wei_netlist_get_net_state();
   for (net = netlist->active_list_ref;
         net != NULL;
         net = WEI_GET_NEXT_LIST_ENTRY_NAMED(net,
                         WiFiEngine_net_t,
                         active_list)) {

      if(net->net_statistics.blacklisted == BLACKLIST_TEMPORARILY)
         net->net_statistics.blacklisted = NOT_BLACKLISTED;
   }
}

static bool_t is_blacklisted(WiFiEngine_net_t* net)
{
   DE_ASSERT(net);

   if (!roam_config.enable_blacklist_filter)
      return FALSE;

   if (!net->net_statistics.blacklisted)
      return FALSE;

   DE_TRACE_STATIC(TR_ROAM_HIGH_RES, "Net is blacklisted\n");

   return TRUE;
}


static bool_t is_encryption_allowed(WiFiEngine_net_t* net)
{
   int status;
   m80211_akm_suite_t akm;
   wei_ie_type_t ie_type;
   status = wei_filter_net_by_authentication(&akm, &ie_type, &net);
   if (status != WIFI_ENGINE_SUCCESS)
      return FALSE;

   DE_TRACE_STATIC(TR_ROAM_HIGH_RES, "enc not allowed\n");

   return TRUE;
}


static bool_t is_wmm_supported(WiFiEngine_net_t* net)
{
   bool_t net_supports_wmm;
   int status;
   rSTA_WMMSupport sta_wmm_support;

   DE_ASSERT(net);

   if (!roam_config.enable_wmm_filter)
      return TRUE;

   status = WiFiEngine_GetWMMEnable(&sta_wmm_support);
   if (status != WIFI_ENGINE_SUCCESS)
      return FALSE;

   /* Don't care about WMM support in AP for this case */
   if (sta_wmm_support == STA_WMM_Disabled)
      return TRUE;

   net_supports_wmm =
      M80211_WMM_INFO_ELEM_IS_PRESENT(net->bss_p->bss_description_p) ||
      M80211_WMM_PARAM_ELEM_IS_PRESENT(net->bss_p->bss_description_p);

   if (net_supports_wmm)
      return TRUE;

   return FALSE;
}



/*
 * Measure link quality on associated net based on mib's.
 * Use 'refresh_lq()' to update mib's
 */
static bool_t is_lq_above_thresholds(bool_t with_margin)
{
   /* needed to update roam_state.rssi */
   refresh_lq(FALSE);

   if (is_levels_above_thresholds( roam_state.rssi, roam_state.snr,
                                   with_margin)) {
      DE_TRACE_INT2(TR_ROAM, "Above thresholds rssi %d snr %d\n", roam_state.rssi, roam_state.snr);
      return TRUE;
   }

   DE_TRACE_INT2(TR_ROAM, "Below thresholds rssi %d snr %d\n", roam_state.rssi, roam_state.snr);
   return FALSE;
}


/* Register a single mib trigger if the trigger type (SNR/RSSI/DS) is enabled.
 * Unregister the trigger first if it is already registered.
 *
 */
static int reconfigure_mib_trigger(int32_t* id, const char *mib, int32_t thr,
                                   uint8_t dir, bool_t enable)
{
   int status;

   DE_ASSERT(id);

   /* Remove any previously registered trigger */
   if (*id != -1) {
      status = WiFiEngine_DelVirtualTrigger(*id);
      if (status != WIFI_ENGINE_SUCCESS) {
         DE_TRACE_STATIC(TR_ROAM, "Failed to delete mib trigger\n");
         return WIFI_ENGINE_FAILURE;
      }
      *id = -1;
   }

   /* We're done if we disabled this trigger */
   if (!enable)
      return WIFI_ENGINE_SUCCESS;

   /* We dont want any triggers when roaming is disabled */
   if (!roam_config.enable_roaming)
      return WIFI_ENGINE_SUCCESS;

   /* Register trigger */
   status = WiFiEngine_RegisterVirtualTrigger(id, mib, 0, 1000000,
            thr, dir, 1, 1,
            notify_mib_trigger);
   if (status != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_STATIC(TR_ROAM, "Failed to register mib trigger\n");
      return WIFI_ENGINE_FAILURE;
   }

   return WIFI_ENGINE_SUCCESS;
}

static void stop_all_mib_triggers(void)
{
   reconfigure_mib_trigger(&roam_state.rssi_falling_scan_trig_id,0,0,0,FALSE);
   reconfigure_mib_trigger(&roam_state.rssi_rising_scan_trig_id,0,0,0,FALSE);

   reconfigure_mib_trigger(&roam_state.snr_falling_scan_trig_id,0,0,0,FALSE);
   reconfigure_mib_trigger(&roam_state.snr_rising_scan_trig_id,0,0,0,FALSE);

   reconfigure_mib_trigger(&roam_state.ds_falling_scan_trig_id,0,0,0,FALSE);
   reconfigure_mib_trigger(&roam_state.ds_rising_scan_trig_id,0,0,0,FALSE);
}

/* Re-register all MIB triggers that we base our scan and roaming decisions on.
 * This function will be called when roaming is enabled or when we the mib
 * trigger thresholds are changed.
 *
 */
static void reconfigure_all_mib_triggers(void)
{
   DE_TRACE_STATIC(TR_ROAM, "ENTRY\n");

   if (roam_state.state==STATE_DISCONNECTED) {
      stop_all_mib_triggers();
      return;
   }

   /* We're not handling an error where the trigger could not be
    * unregistered/registered.
    */
   if (roam_config.measure_on_data) {

      (void) reconfigure_mib_trigger(&roam_state.rssi_falling_scan_trig_id,
                                     MIB_dot11rssiDataFrame, roam_config.rssi_scan_thr,
                                     WE_TRIG_THR_FALLING, roam_config.rssi_enable);
      (void) reconfigure_mib_trigger(&roam_state.rssi_rising_scan_trig_id,
                                     MIB_dot11rssiDataFrame, roam_config.rssi_scan_thr,
                                     WE_TRIG_THR_RISING, roam_config.rssi_enable);

      (void) reconfigure_mib_trigger(&roam_state.snr_falling_scan_trig_id,
                                     MIB_dot11snrData, roam_config.snr_scan_thr,
                                     WE_TRIG_THR_FALLING, roam_config.snr_enable);
      (void) reconfigure_mib_trigger(&roam_state.snr_rising_scan_trig_id,
                                     MIB_dot11snrData, roam_config.snr_scan_thr,
                                     WE_TRIG_THR_RISING, roam_config.snr_enable);
   } else {
      (void) reconfigure_mib_trigger(&roam_state.rssi_falling_scan_trig_id,
                                     MIB_dot11rssi, roam_config.rssi_scan_thr,
                                     WE_TRIG_THR_FALLING, roam_config.rssi_enable);
      (void) reconfigure_mib_trigger(&roam_state.rssi_rising_scan_trig_id,
                                     MIB_dot11rssi, roam_config.rssi_scan_thr,
                                     WE_TRIG_THR_RISING, roam_config.rssi_enable);

      (void) reconfigure_mib_trigger(&roam_state.snr_falling_scan_trig_id,
                                     MIB_dot11snrBeacon, roam_config.snr_scan_thr,
                                     WE_TRIG_THR_FALLING, roam_config.snr_enable);
      (void) reconfigure_mib_trigger(&roam_state.snr_rising_scan_trig_id,
                                     MIB_dot11snrBeacon, roam_config.snr_scan_thr,
                                     WE_TRIG_THR_RISING, roam_config.snr_enable);
   }

   /* Delay spread configuration */
   roam_config.ds_enable = FALSE;
   (void) reconfigure_mib_trigger(&roam_state.ds_falling_scan_trig_id,
                                  MIB_dot11delaySpreadThresholdRatio,
                                  roam_config.ds_scan_thr,
                                  WE_TRIG_THR_FALLING, roam_config.ds_enable);
   (void) reconfigure_mib_trigger(&roam_state.ds_rising_scan_trig_id,
                                  MIB_dot11delaySpreadThresholdRatio,
                                  roam_config.ds_scan_thr,
                                  WE_TRIG_THR_RISING, roam_config.ds_enable);
}


#define DELAY_SPREAD_5 0
#define DELAY_SPREAD_10 1
#define DELAY_SPREAD_15 2
#define DELAY_SPREAD_25 3
#define DELAY_SPREAD_50 4
#define DELAY_SPREAD_75 5
#define DELAY_SPREAD_100 6
#define DELAY_SPREAD_125 7
#define DELAY_SPREAD_150 8
#define DELAY_SPREAD_175 9
#define DELAY_SPREAD_200 10
#define DELAY_SPREAD_225 11
#define DELAY_SPREAD_250 12
#define DELAY_SPREAD_NUM 13

#define SNR_5 0
#define SNR_10 1
#define SNR_15 2
#define SNR_20 3
#define SNR_25 4
#define SNR_30 5
#define SNR_NUM 6

static const uint16_t ds_mapping[DELAY_SPREAD_NUM][SNR_NUM] = {
   /* 5,    10,   15,   20,   25,   30 */
   { 1597, 1105,  771,  596,  510,  487 },     /*    5 [ns]  */
   { 1688, 1281,  994,  831,  754,  707 },     /*   10 [ns]  */
   { 1762, 1396, 1130,  977,  911,  872 },     /*   15 [ns]  */
   { 1832, 1542, 1329, 1211, 1134, 1107 },     /*   25 [ns]  */
   { 1947, 1739, 1583, 1483, 1462, 1421 },     /*   50 [ns]  */
   { 2010, 1841, 1721, 1646, 1615, 1607 },     /*   75 [ns]  */
   { 2059, 1910, 1809, 1765, 1734, 1718 },     /*  100 [ns]  */
   { 2085, 1955, 1881, 1837, 1816, 1818 },     /*  125 [ns]  */
   { 2102, 1999, 1925, 1890, 1879, 1867 },     /*  150 [ns]  */
   { 2128, 2041, 1963, 1941, 1935, 1930 },     /*  175 [ns]  */
   { 2136, 2056, 2001, 1977, 1972, 1967 },     /*  200 [ns]  */
   { 2152, 2076, 2028, 2009, 2003, 2000 },     /*  225 [ns]  */
   { 2168, 2097, 2055, 2041, 2040, 2028 },     /*  250 [ns]  */
};

/* Configure delay spread mibs. This function will be called when the
 * delay spread thresholds are changed by the user.
 * These mib settings will control how the MIB trigger variable is calculated.
 */
int WiFiEngine_configure_delay_spread(uint32_t thr, uint16_t winsize)
{
#define DSMAP(X) do {                                        \
      if(roam_config.ds_thr >= (X)) idx = DELAY_SPREAD_##X;  \
   } while(0)

#define SETMIB(MIB, VAL) do {                                           \
      int __cc =                                                        \
         WiFiEngine_SendMIBSet(MIB, NULL, (char*) & VAL,                \
                               sizeof(VAL));                            \
      if(__cc != WIFI_ENGINE_SUCCESS) {                                 \
         DE_TRACE_STATIC(TR_ROAM, "Failed to set ## MIB\n");                  \
         return WIFI_ENGINE_FAILURE;                                    \
      }                                                                 \
   } while(0)



   uint32_t idx = 0;
   roam_config.ds_thr = thr;
   roam_config.ds_winsize = winsize;

   DSMAP(5);
   DSMAP(10);
   DSMAP(15);
   DSMAP(25);
   DSMAP(50);
   DSMAP(75);
   DSMAP(100);
   DSMAP(125);
   DSMAP(150);
   DSMAP(175);
   DSMAP(200);
   DSMAP(225);

   DE_TRACE_INT6(TR_ROAM, "idx=%d thr=%d winsize=%d 5db=%d 10db=%d 20db=%d\n",
                 idx, roam_config.ds_thr, roam_config.ds_winsize,
                 ds_mapping[idx][SNR_5], ds_mapping[idx][SNR_10],
                 ds_mapping[idx][SNR_20]);

   SETMIB(MIB_dot11delaySpreadMinPacketLimit, roam_config.ds_winsize);
   SETMIB(MIB_dot11delaySpreadThresholdSnr5db, ds_mapping[idx][SNR_5]);
   SETMIB(MIB_dot11delaySpreadThresholdSnr10db, ds_mapping[idx][SNR_10]);
   SETMIB(MIB_dot11delaySpreadThresholdSnr20db, ds_mapping[idx][SNR_20]);

   return WIFI_ENGINE_SUCCESS;

#undef DSMAP
#undef SETMIB
}


/************** GENERIC SUPPORT FUNCTIONS : START *****************/

static bool_t is_ibss(WiFiEngine_net_t* net)
{
   DE_ASSERT(net);

   if (net->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_IBSS)
      return TRUE;

   return FALSE;
}


/*
 * Check if candidate net is associated net
 * will also do some sanity checks
 */
static bool_t is_self(WiFiEngine_net_t* candidate)
{
   WiFiEngine_net_t* net;

   if(WiFiEngine_is_roaming_in_progress())
      return FALSE;

   net = wei_netlist_get_current_net();

   if (!net)
      return FALSE;

   if (candidate == net)
      return TRUE;

   if (wei_equal_bssid(net->bssId_AP, candidate->bssId_AP)) {
#if 0
      // may occur with AP's with multiple SSID's
      // may occur if AP changes settings
      ROAM_TRACE_NET("associated net", net);
      ROAM_TRACE_NET("candidate net ", candidate);
      DE_BUG_ON(1, "bssid in netlist occur twice\n");
#endif
      return TRUE;
   }

   return FALSE;
}

#if (DE_CCX == CFG_INCLUDED)
static bool_t is_ccx_aac_invalid(WiFiEngine_net_t* candidate)
{
    uint16_t aac;

    if(candidate->bss_p->bss_description_p->ie.qbss_parameter_set.hdr.id != M80211_IE_ID_NOT_USED)
    {
        aac = candidate->bss_p->bss_description_p->ie.qbss_parameter_set.avail_adm_capa;

        if(aac > 0 && aac < wifiEngineState.ccxState.rac)   /*THRESHOLD ACCORDING TO SPEC: par.7.3.2.28 in 802.11-2007, p.177*/
        {
            return TRUE;
        }
    }
    return FALSE;
}
#endif
/* Check if candidate net dissapearence caused roaming */
static bool_t is_down(WiFiEngine_net_t* candidate)
{
   if ((roam_state.state == STATE_ROAMING || roam_state.state == STATE_HC) &&
       (roam_state.last_roaming_reason == WE_ROAM_REASON_UNSPECIFIED) &&
       wei_equal_bssid(candidate->bssId_AP, roam_state.last_connected_net))
          return TRUE;

   return FALSE;
}

/* TODO: move to driverenv and check for wraparound */
#define is_tick_less_then(_x, _y) (_x < _y)

static bool_t is_old(WiFiEngine_net_t* candidate)
{
   if (roam_state.state!=STATE_CONNECTED)
      return FALSE;

   if (is_tick_less_then(
            candidate->last_heard_tick, 
            roam_state.connected_at_tick))
      return TRUE;

   return FALSE;
}

/************** GENERIC SUPPORT FUNCTIONS : END *******************/


/** @} */ /* End of we_roam group */


/***************** LQM GOES HERE, NOT PART OF ROAMING AGENT ***************/


/*
 * Configure a scan job to be used if mlme_peer_status_ind is received
 * with status warning. The job will initiate sending of a probe
 * request to the associated AP.
 */
int wei_roam_configure_lqm_job(void)
{
   WiFiEngine_net_t* net;
   channel_list_t ch_list;

   net = wei_netlist_get_current_net();

   if (net)
   {
      ch_list.reserved = 0;
      ch_list.no_channels = 1;
      ch_list.channelList[0] = net->bss_p->bss_description_p->ie.ds_parameter_set.channel;

      WiFiEngine_AddScanJob(&lqm_job_id,
                            net->bss_p->bss_description_p->ie.ssid,
                            net->bssId_AP,
                            1,
                            ch_list,
                            CONNECTED_MODE,
                            1,
                            0,
                            -1,
                            1,
                            NULL);
   }

   return WIFI_ENGINE_SUCCESS;
}


/*
 * Activate the lqm job
 */
int wei_roam_trigger_lqm_job(void)
{
   wei_send_scan_req(lqm_job_id, 0, 0, 0, 0);

   return WIFI_ENGINE_SUCCESS;
}

