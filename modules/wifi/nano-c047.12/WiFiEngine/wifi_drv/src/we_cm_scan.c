/** @defgroup we_roam WiFiEngine scan and connect 
 *
 * \brief  This module scan for (hidden) networks and connects to them if disconnected.
 * (use on linux/winXX, use with connection manager/roaming agent)
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
#include "we_cm.h"

/* Start the hole thing */
int WiFiEngine_sac_start(void);
int WiFiEngine_sac_stop(void);

/* When using Linux with preemption, there is a possible race condition in connect().
 * connect() can be called in one thread (wpa_supplicant) from WiFiEngine_sac_start(),
 * connect() can also be called in another thread (kernel) from scan_complete_ind().
 * The sac_mutex is used to solve this problem.
 */
static driver_mutex_t sac_mutex;

/* To compensate for lack of scan api */
typedef enum scan_state_e {
   SCAN_NOT_CONFIGURED,
   SCAN_STARTED,
   SCAN_STOPPED
} scan_state_t;

struct sac_state_s {
   scan_state_t   scan_state;
   uint32_t       scan_job_id;

   void *cm_session;

   struct iobject *scan_h;
   struct iobject *con_lost_h;
   struct iobject *core_dump_h;
};

static struct sac_state_s sac_state = { 
   SCAN_NOT_CONFIGURED,
   NO_SCAN_JOB,
   NULL,

   NULL,
   NULL,
   NULL
};

static void we_init(void);
static void scan_complete_ind(wi_msg_param_t param, void *priv);
static void disconnected_ind(wi_msg_param_t param, void *priv);
static void coredump_start_ind(wi_msg_param_t param, void *priv);

static void connect(WiFiEngine_net_t* net);
static int create_and_start_scan_job(net_profile_s *conf);
static void remove_scan_job(void);
static void stop_scan_job(void);
static void start_scan_job(void);

static void build_profile(net_profile_s *p)
{
   rBasicWiFiProperties *properties;
   properties = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);

   p->ssid = properties->desiredSSID;
   WiFiEngine_GetDesiredBSSID(&p->bssid);
}

static void we_init(void)
{
   we_ind_cond_register(&sac_state.scan_h,
         WE_IND_SCAN_COMPLETE, "SAC: WE_IND_SCAN_COMPLETE",
         scan_complete_ind, NULL,0,NULL);

   we_ind_cond_register(&sac_state.con_lost_h, 
         WE_IND_CM_DISCONNECTED, "SAC: WE_IND_CM_DISCONNECTED", 
         disconnected_ind, NULL,0,NULL);

   we_ind_cond_register(&sac_state.core_dump_h, 
         WE_IND_CORE_DUMP_START, "SAC: WE_IND_CORE_DUMP_START",
         coredump_start_ind, NULL,0,NULL);
}

/* This function is called directly from WiFiEngine_Initialize() */
void WiFiEngine_sac_init(void)
{
   DriverEnvironment_mutex_init(&sac_mutex);
}

int WiFiEngine_sac_start(void)
{
   net_profile_s         p;
   WiFiEngine_bss_type_t type;

   DE_ASSERT(DE_ENABLE_CM_SCAN == CFG_ON);

   /* check if started */
   if(sac_state.scan_h)
      return WIFI_ENGINE_SUCCESS;

   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");

#if (DE_BUILTIN_SUPPLICANT == CFG_INCLUDED)
   {
      int wait_for_wpa;
      //configure if we should get the "connect-done" when connected or when
      //the supplicant is done.
      wait_for_wpa = (wifiEngineState.config.encryptionLimit > Encryption_WEP);
      wei_cm_wpa_connect_wait(wait_for_wpa);
   }
#endif

   we_init();
   remove_scan_job();
   build_profile(&p);

   WiFiEngine_GetBSSType(&type);
   if(type == M80211_CAPABILITY_ESS) 
   {
      WiFiEngine_net_t* elected_net;
      
      /* filtering will be done by default we_filter_out_net(...) */
      elected_net = WiFiEngine_elect_net(NULL,NULL,TRUE);
      if (elected_net)
      {
         connect(elected_net);
         return WIFI_ENGINE_SUCCESS;
      }
      else
         return create_and_start_scan_job(&p);
   } 
   else
   {
      /* IBSS does not require scanning, just start the net! */
      connect(wei_find_create_ibss_net());
   }

   return WIFI_ENGINE_SUCCESS;
}

int WiFiEngine_sac_stop(void)
{
   DE_ASSERT(DE_ENABLE_CM_SCAN == CFG_ON);

   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");

   remove_scan_job();

   we_ind_deregister_null(&sac_state.scan_h);
   we_ind_deregister_null(&sac_state.con_lost_h); 
   we_ind_deregister_null(&sac_state.core_dump_h); 

   we_cm_disconnect(sac_state.cm_session);
   sac_state.cm_session = NULL;

   return WIFI_ENGINE_SUCCESS;
}

static void scan_complete_ind(wi_msg_param_t param, void *priv)
{
   m80211_nrp_mlme_scannotification_ind_t *ind = param;   
   WiFiEngine_bss_type_t type;

   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");

   if(ind == NULL || ind->job_id != sac_state.scan_job_id)
      return;

   WiFiEngine_GetBSSType(&type);
   if(type == M80211_CAPABILITY_ESS) {
      connect(WiFiEngine_elect_net(NULL,NULL,TRUE));
   } else {
      /* IBSS */
      connect(wei_find_create_ibss_net());
   }
}

static void disconnected_ind(wi_msg_param_t param, void *priv)
{
   if(param != sac_state.cm_session)
      return;

   if(sac_state.cm_session) { 
      sac_state.cm_session = NULL; 

      /* looking for new candidates is not desirable when we work under
         the supplicant! So this "auto reconnect" capability should be
         optional (controlled at the registry?) and normally disabled
      */
      if (0) {
         start_scan_job();
      }
   }
}

static void coredump_start_ind(wi_msg_param_t param, void *priv)
{
   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");
}

static void connect(WiFiEngine_net_t* net)
{
   if(!net)
      return;
   
   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");

   stop_scan_job();

   DriverEnvironment_mutex_down(&sac_mutex);
   /* If NULL perform connect, else it is already done. */
   if(sac_state.cm_session == NULL)
   {
      sac_state.cm_session = we_cm_connect(net);
   }
   DriverEnvironment_mutex_up(&sac_mutex);
}

static int create_and_start_scan_job(net_profile_s *conf)
{
   channel_list_t channels;

   if(sac_state.scan_state != SCAN_NOT_CONFIGURED)
      return WIFI_ENGINE_FAILURE;

   WiFiEngine_GetRegionalChannels(&channels);

   if (WiFiEngine_AddScanJob(&sac_state.scan_job_id, 
            conf->ssid, 
            conf->bssid, 
            ACTIVE_SCAN, /* needed to find hidden nets */
            channels, 
            IDLE_MODE,
            9 /* prio ??? */, 
            0  /* exclude ap */, 
            -1 /* filter */, 
            1  /* run_every_nth_period */, 
            NULL) 
         != WIFI_ENGINE_SUCCESS)
   {
      sac_state.scan_job_id = NO_SCAN_JOB;
      sac_state.scan_state = SCAN_NOT_CONFIGURED;
      return WIFI_ENGINE_FAILURE;
   }

   if (WiFiEngine_SetScanJobState(sac_state.scan_job_id, TRUE, NULL)
         != WIFI_ENGINE_SUCCESS) {
      WiFiEngine_RemoveScanJob(sac_state.scan_job_id, NULL);
      sac_state.scan_state = SCAN_NOT_CONFIGURED;
      sac_state.scan_job_id = NO_SCAN_JOB;
      return WIFI_ENGINE_FAILURE;
   }

   if (WiFiEngine_TriggerScanJob(sac_state.scan_job_id, 0)
       != WIFI_ENGINE_SUCCESS)
         DE_TRACE_STATIC(TR_CM, "Failed to trigger CM scan job");

   DE_TRACE_STATIC(TR_NOISE, "EXIT\n");
   sac_state.scan_state = SCAN_STARTED;

   return WIFI_ENGINE_SUCCESS;
}

static void stop_scan_job(void) {
   if(sac_state.scan_state == SCAN_STOPPED)
      return;

   if (WiFiEngine_SetScanJobState(sac_state.scan_job_id, FALSE, NULL)
         == WIFI_ENGINE_SUCCESS) {
      sac_state.scan_state = SCAN_STOPPED;
   }

}

static void start_scan_job(void) {
   if(sac_state.scan_state == SCAN_STARTED)
      return;

   if (WiFiEngine_SetScanJobState(sac_state.scan_job_id, TRUE, NULL)
         == WIFI_ENGINE_SUCCESS) {
      sac_state.scan_state = SCAN_STARTED;

      /* impatient! */
      WiFiEngine_TriggerScanJob(sac_state.scan_job_id, 0);
   }
}

static void remove_scan_job(void)
{
   if(sac_state.scan_state == SCAN_NOT_CONFIGURED)
      return;

   DE_TRACE_STATIC(TR_NOISE, "ENTRY\n");

   WiFiEngine_RemoveScanJob(sac_state.scan_job_id, NULL);
   sac_state.scan_state = SCAN_NOT_CONFIGURED;
   sac_state.scan_job_id = NO_SCAN_JOB;
}

void wei_cm_scan_unplug(void)
{
  remove_scan_job();

  we_ind_deregister_null(&sac_state.scan_h);
  we_ind_deregister_null(&sac_state.con_lost_h); 
  we_ind_deregister_null(&sac_state.core_dump_h); 
  
  we_cm_disconnect(sac_state.cm_session);
  sac_state.cm_session = NULL;
}
