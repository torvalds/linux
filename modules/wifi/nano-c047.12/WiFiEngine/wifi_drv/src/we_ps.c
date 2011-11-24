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
This module implements the WiFiEngine Power Save interface.

*****************************************************************************/
/** @defgroup we_ps WiFiEngine Power Save interface
 *
 * @brief The WiFiEngine Power Save interface handles power save modes,
 * sleep and wake directives and traffic monitoring.
 *
 *  @{
 */
#include "driverenv.h"
#include "ucos.h"
#include "m80211_stddefs.h"
#include "registry.h"
#include "packer.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "hmg_defs.h"
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"


#ifdef FILE_WE_PS_C
#undef FILE_NUMBER
#define FILE_NUMBER FILE_WE_PS_C
#endif //FILE_WE_PS_C

typedef struct pm_session 
{
   WEI_TQ_ENTRY(pm_session) session;
   int pending;            /* is this session waiting in line to be activated */
   ucos_msg_id_t sig;
} pm_session_s;

typedef struct pm_ctx 
{
   WEI_TQ_HEAD(pm_session_head, pm_session) head;
   driver_lock_t lock;
   iobject_t *ps_enabled_completed;
   iobject_t *ps_disabled_completed;   
   iobject_t *ps_connect_complete; 
   iobject_t *ps_disconnect_complete;    
   iobject_t *ps_wmm_req_complete; 
} pm_ctx_s;

static struct ibss_ps_ctx_t {
   iobject_t* connecting_h;
   iobject_t* disconnecting_h;
} ibss_ps_ctx;

static pm_ctx_s *g_pm_ctx = NULL;

#define GET_CTX (g_pm_ctx)
static void pm_request_complete(wi_msg_param_t param, void* priv);
static pm_session_s* pm_session_init(ucos_msg_id_t sig);
static void pm_destroy(pm_session_s *s);
static void do_pm_request(pm_session_s *s);
static void pm_list_all(pm_ctx_s *ctx);
static int  pm_get_list_size(pm_ctx_s *ctx);
static void ps_connecting_cb(wi_msg_param_t param, void* priv);
static void ps_disconnecting_cb(wi_msg_param_t param, void* priv);

/* Local variables */
#define MAX_NUM_USER 16
static we_ps_control_t we_ps_control_data[MAX_NUM_USER];
static void we_ps_control_trace(void);

void wei_ps_plug(void)
{
   ibss_ps_ctx.connecting_h = NULL;
   ibss_ps_ctx.disconnecting_h = NULL;

   ibss_ps_ctx.connecting_h = we_ind_register(
            WE_IND_80211_CONNECTING, "WE_IND_80211_CONNECTING",
            ps_connecting_cb, NULL, 0, NULL); 
   
   DE_ASSERT(ibss_ps_ctx.connecting_h != NULL);

   ibss_ps_ctx.disconnecting_h = we_ind_register(
            WE_IND_80211_DISCONNECTING, "WE_IND_80211_DISCONNECTING",
            ps_disconnecting_cb, NULL, 0, NULL);  
   DE_ASSERT(ibss_ps_ctx.disconnecting_h != NULL);   

}

void wei_ps_unplug(void)
{
   pm_ctx_s *ctx = GET_CTX;
   pm_session_s *s = NULL; 
 
   DriverEnvironment_acquire_lock(&ctx->lock);
   while(!WEI_TQ_EMPTY(&ctx->head)) 
   {
      s = WEI_TQ_FIRST(&ctx->head);
      WEI_TQ_REMOVE(&ctx->head, s, session); 
      DriverEnvironment_release_lock(&ctx->lock);
      pm_destroy(s);
      DriverEnvironment_acquire_lock(&ctx->lock);
   }
   DriverEnvironment_release_lock(&ctx->lock);
   
   we_ind_deregister_null(&ibss_ps_ctx.connecting_h);
   we_ind_deregister_null(&ibss_ps_ctx.disconnecting_h);
}



static void ps_connecting_cb(wi_msg_param_t param, void* priv)
{
   WiFiEngine_net_t *net;
   uint16_t interval;

   DE_TRACE_STATIC(TR_PS, "connecting_cb\n");    
   net = wei_netlist_get_current_net();

   if(net == NULL)
   {
      return;
   }  
   
   if(net->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_IBSS)
   {  
      /* Ibss power save scheme is not implemented yet */
      WiFiEngine_InhibitPowerSave(wifiEngineState.ibss_ps_uid);    
   }

   WiFiEngine_GetDelayStartOfPs(&interval);
   if(interval != 0)
   { 
	   /* Delay start of power save according to value in registry.dat */
	   WiFiEngine_StartDelayPowerSaveTimer();
   }
}


static void ps_disconnecting_cb(wi_msg_param_t param, void* priv)
{
#if 0
   WiFiEngine_net_t *net;
#endif

   DE_TRACE_STATIC(TR_PS, "disconnecting_cb\n");    

   /* Ibss power save scheme is not implemented yet
      do not allow power save*/
   return;
}

/****************************************************************/
/* Power save manager methods. The purpose of PM is to queue    */
/* all requests that is to be sent to powersave state machine.  */
/* If a request is pending the new request will be queued.      */
/* The power save state machine will signal when a request      */
/* is completed.                                                */
/****************************************************************/
void wei_pm_initialize(void **priv) 
{
   
   pm_ctx_s *ctx = (pm_ctx_s *)DriverEnvironment_Malloc(sizeof(pm_ctx_s));
   g_pm_ctx = ctx;
   DE_ASSERT(priv);

   DE_MEMSET(ctx,0,sizeof(pm_ctx_s));
   WEI_TQ_INIT(&ctx->head);
   DriverEnvironment_initialize_lock(&ctx->lock);

   *priv = ctx;

   ctx->ps_enabled_completed = we_ind_register(
         WE_IND_PS_ENABLED, "WE_IND_PS_ENABLED", 
         pm_request_complete, NULL,0,ctx);
   ctx->ps_disabled_completed = we_ind_register(
         WE_IND_PS_DISABLED, "WE_IND_PS_DISABLED",
         pm_request_complete, NULL,0,ctx);
   ctx->ps_connect_complete = we_ind_register(
         WE_IND_PS_CONNECT_COMPLETE, "WE_IND_PS_CONNECT_COMPLETE",
         pm_request_complete, NULL,0,ctx);
   ctx->ps_disconnect_complete = we_ind_register(
         WE_IND_PS_DISCONNECT_COMPLETE, "WE_IND_PS_CONNECT_COMPLETE",
         pm_request_complete, NULL,0,ctx);
   ctx->ps_wmm_req_complete = we_ind_register(
         WE_IND_PS_WMM_REQ_COMPLETE, "WE_IND_PS_WMM_REQ_COMPLETE",
         pm_request_complete, NULL,0,ctx);
   
   
   pm_list_all(ctx);
}



/*
 * Returns all queued requests    
 */
static int  pm_get_list_size(pm_ctx_s *ctx)
{
   int i=0;
   pm_session_s *elm=NULL;

   WEI_TQ_FOREACH(elm, &ctx->head, session) 
   {        
      i++;
   }

   return i;
}




/*
 * List all queued requests    
 */
static void pm_list_all(pm_ctx_s *ctx)
{
   int i=0;
   pm_session_s *elm=NULL;

   WEI_TQ_FOREACH(elm, &ctx->head, session) 
   {
      DE_TRACE_INT2(TR_PS_DEBUG, "session [%d] %p\n", i, (void *)elm);
      DE_TRACE_INT2(TR_PS_DEBUG, "pending: %d sig: %d\n",elm->pending, elm->sig);       
      
      i++;
   }
   if(i==0) {
      DE_TRACE_STATIC(TR_PS_DEBUG, "no sessions in queue\n");
   }
}


/*
 * Return new request if it is pending.
 * The returned request is marked as not pending(on going).
 */
static pm_session_s* test_and_set_ps_req_pending(pm_ctx_s *ctx) 
{
   pm_session_s *elm=NULL;
   DE_TRACE_STATIC(TR_PS_DEBUG, "test_and_set_ps_req_pending\n"); 

   pm_list_all(ctx);
   elm = WEI_TQ_FIRST(&ctx->head);  
   DE_TRACE_PTR(TR_PS_DEBUG, "First session is %p\n", elm);
   if(elm)
   {
      DE_TRACE_INT2(TR_PS_DEBUG, "pending: %d sig: %d\n",elm->pending, elm->sig); 
   }


   if(elm && elm->pending) 
   {
      elm->pending = 0;
      return elm;
   }

   return NULL;
}


/*
 * Create a new request item. Execute the request
 * if there is no other request on going else
 * queue it.
 */
void* we_pm_request(ucos_msg_id_t sig)
{
   pm_session_s *s=NULL, *first=NULL;
   pm_ctx_s *ctx = GET_CTX;
   int nmb_items = 0;

   DE_TRACE_STATIC(TR_PS, "we_pm_request \n"); 
   pm_list_all(ctx);
   s = pm_session_init(sig);
   if(!s) return NULL;

   DE_TRACE_PTR(TR_PS_DEBUG, "New session %p\n", s); 



   DriverEnvironment_acquire_lock(&ctx->lock);
   WEI_TQ_INSERT_TAIL(&ctx->head, s, session);
   first = test_and_set_ps_req_pending(ctx);
   DriverEnvironment_release_lock(&ctx->lock);

   nmb_items = pm_get_list_size(ctx); 
   
   if(nmb_items > 5)
   {
      DE_TRACE_INT(TR_PS, "Warning pm queue is now: %d \n", nmb_items); 
   }        

   if(first)
      do_pm_request(first);

   return s;
}

/*
 * Create a new request item and set it to pending.
 */
static pm_session_s* pm_session_init(ucos_msg_id_t sig)
{
   pm_session_s *s=NULL;
   DE_TRACE_STATIC(TR_PS_DEBUG, "pm_session_init \n"); 
   s = (pm_session_s*)DriverEnvironment_Nonpaged_Malloc(sizeof(pm_session_s));
   if(!s) return NULL;

   DE_MEMSET(s,0,sizeof(pm_session_s));

   s->pending = 1;
   s->sig = sig;
   DE_TRACE_INT2(TR_PS_DEBUG, "pending: %d sig: %d\n",s->pending, s->sig); 

   return s;
}

/*
 * Deregister indications and free memory.
 */
void wei_pm_shutdown(void *priv)
{
   pm_ctx_s *ctx = (pm_ctx_s*)priv;
   pm_session_s *s = NULL;

   if(!ctx) return;

   we_ind_deregister_null(&ctx->ps_enabled_completed);
   ctx->ps_enabled_completed = NULL;
   we_ind_deregister_null(&ctx->ps_disabled_completed);
   ctx->ps_disabled_completed = NULL;
   we_ind_deregister_null(&ctx->ps_connect_complete);
   ctx->ps_connect_complete = NULL; 
   we_ind_deregister_null(&ctx->ps_disconnect_complete);
   ctx->ps_disconnect_complete = NULL;   
   we_ind_deregister_null(&ctx->ps_wmm_req_complete);
   ctx->ps_wmm_req_complete = NULL; 
   
   DriverEnvironment_acquire_lock(&ctx->lock);
   while(!WEI_TQ_EMPTY(&ctx->head)) 
   {
      s = WEI_TQ_FIRST(&ctx->head);
      WEI_TQ_REMOVE(&ctx->head, s, session); 
      DriverEnvironment_release_lock(&ctx->lock);
      pm_destroy(s);
      DriverEnvironment_acquire_lock(&ctx->lock);
   }
   DriverEnvironment_release_lock(&ctx->lock);

   DriverEnvironment_Nonpaged_Free(ctx);

   DE_TRACE_STATIC(TR_PS, "PM shutdown complete\n"); 
}


static void pm_destroy(pm_session_s *s)
{
   DE_TRACE_PTR(TR_PS_DEBUG, "terminating pm_req %p\n", s); 

   DriverEnvironment_Nonpaged_Free(s);
}

static void pm_request_complete(wi_msg_param_t param, void* priv)
{   
   pm_session_s *s=NULL, *pending=NULL;
   pm_ctx_s *ctx = (pm_ctx_s*)priv;

   if(priv == NULL)
   {
      return;
   }

   DriverEnvironment_acquire_lock(&ctx->lock);
   s = WEI_TQ_FIRST(&ctx->head);
   DriverEnvironment_release_lock(&ctx->lock);

   if(!s) return;

   if(!s->pending) 
   {
      DriverEnvironment_acquire_lock(&ctx->lock);
      WEI_TQ_REMOVE(&ctx->head, s, session); 
      pm_destroy(s);
   }
   
   pending = test_and_set_ps_req_pending(ctx);
   DriverEnvironment_release_lock(&ctx->lock);

   if(pending)
      do_pm_request(pending);   
}


static void do_pm_request(pm_session_s *s)
{
   DE_TRACE_INT(TR_PS, "do_pm_request sig: %d \n",s->sig);    
   wei_sm_queue_sig(s->sig, SYSDEF_OBJID_HOST_MANAGER_PS, DUMMY, FALSE);
      
   /* Let the pm state machine act upon the new message. */
   wei_sm_execute(); 
}


/****************************************************************/
/* End power save manager methods.                              */
/****************************************************************/

/*!
 * @brief Checks if associated net is supporting
 *        wmm power save.
 *
 *
 * @return true if wmm power save is supported else false
 */
int WiFiEngine_isAssocSupportingWmmPs(void)
{
   WiFiEngine_net_t *net;

   net = wei_netlist_get_current_net();

   if(net)
   {   
      if((WES_TEST_FLAG(WES_FLAG_WMM_ASSOC)&&(wei_is_wmm_ps_enabled()))&&
         (((M80211_WMM_PARAM_ELEM_IS_PRESENT(net->bss_p->bss_description_p)) &&
         (M80211_WMM_PARAM_ELEM_SUPPORT_PS(net->bss_p->bss_description_p)))
       ||((M80211_WMM_INFO_ELEM_IS_PRESENT(net->bss_p->bss_description_p)) &&
         (M80211_WMM_INFO_ELEM_SUPPORT_PS(net->bss_p->bss_description_p)))))
      {
         return WIFI_ENGINE_SUCCESS;
      }
      else
      {
         //Mats S, 2008-04-15
         //This trace can be removed when we have found out why we use
         //legacy-PS on WMM-PS enabled AP:s
         DE_TRACE_INT5(TR_WMM_PS, "Assoc not SupportingWmmPs (%u,%u,%u,%u,%u)\n",
                WES_TEST_FLAG(WES_FLAG_WMM_ASSOC),
                M80211_WMM_PARAM_ELEM_IS_PRESENT(net->bss_p->bss_description_p),
                M80211_WMM_PARAM_ELEM_SUPPORT_PS(net->bss_p->bss_description_p),
                M80211_WMM_INFO_ELEM_IS_PRESENT(net->bss_p->bss_description_p),
                M80211_WMM_INFO_ELEM_SUPPORT_PS(net->bss_p->bss_description_p));
      }
   }
   
   return WIFI_ENGINE_FAILURE_NOT_SUPPORTED;
}


/*!
 * @brief Turn on/off WmmPowerSave flag
 * 
 * @param sp_len Service period length in packets. 
 *
 * @return WIFI_ENGINE_SUCCESS if successful.
 */
int WiFiEngine_WmmPowerSaveEnable(uint32_t sp_len) 
{
   rBasicWiFiProperties *basic = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic);

   DE_TRACE_STATIC(TR_SM, "=============> WiFiEngine_WmmPowerSaveEnable\n"); 

   basic->enableWMMPs = TRUE; 
   
   switch (sp_len) 
   {
      case 0x00:
      case 0x01:
      case 0x02:
      case 0x03:           
         REGISTRY_WLOCK();
         basic->qosMaxServicePeriodLength = sp_len;
         REGISTRY_WUNLOCK();
         break;
      default:
         return WIFI_ENGINE_FAILURE;
   }

   if(wei_is_80211_connected() && WES_TEST_FLAG(WES_FLAG_WMM_ASSOC))
   {
      /* If connected start wmm power save else
         registry flag will be used when connecting */
      we_pm_request(INTSIG_ENABLE_WMM_PS);
   }

   DE_TRACE_STATIC(TR_SM, "<============= WiFiEngine_WmmPowerSaveEnable\n");
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Configure Wmm Power Save
 * 
 * @param tx_period Period in us after which Null-Data frames are sent
 *                  to request data delivery from the access point.
 *
 * @return WIFI_ENGINE_SUCCESS if successful (i.e. allways as no checks are done).
 */
int WiFiEngine_WmmPowerSaveConfigure(uint32_t tx_period, bool_t be, bool_t bk, bool_t vi, bool_t vo)
{
   rBasicWiFiProperties *basic = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_ASSERT(basic);

   REGISTRY_WLOCK();
   basic->wmmPsPeriod = tx_period;
   basic->QoSInfoElements.ac_be = be;
   basic->QoSInfoElements.ac_bk = bk;
   basic->QoSInfoElements.ac_vi = vi;
   basic->QoSInfoElements.ac_vo = vo;
   REGISTRY_WUNLOCK();

   WiFiEngine_SetPSWMMPeriod(tx_period);

   return WIFI_ENGINE_SUCCESS;     
}

/*!
 * @brief Disable Wmm Power Save
 *
 * If target sleeps a sequence of messages
 * is needed to bring up target to active mode. A new call to this
 * function will fail if the sequence is not complete. If target is
 * awake (not wmm) there is no need to perform the wakeup sequence.
 * If target is not awake or in wmm/wake the new mode is stored temporary
 * and will be changed when target is awake (not wmm).
 * 
 * @return WIFI_ENGINE_SUCCESS if successful (i.e. allways as no checks are done).
 *         WIFI_ENGINE_FAILURE_DEFER is the sequence to bring
           up target to active is not completed.         
 */
int WiFiEngine_WmmPowerSaveDisable(void) 
{
   rBasicWiFiProperties *basic = (rBasicWiFiProperties *)Registry_GetProperty(ID_basic);
   DE_TRACE_STATIC(TR_SM, "============> WiFiEngine_WmmPowerSaveDisable\n"); 

   DE_ASSERT(basic);

   REGISTRY_WLOCK();
   basic->enableWMMPs = FALSE;
   REGISTRY_WUNLOCK();

   if(wei_is_80211_connected() && WES_TEST_FLAG(WES_FLAG_WMM_ASSOC))
   {
      we_pm_request(INTSIG_DISABLE_WMM_PS);
   }

   DE_TRACE_STATIC(TR_SM, "<============ WiFiEngine_WmmPowerSaveDisable\n"); 
   return WIFI_ENGINE_SUCCESS;
}


  
/*!
 * @brief Enables / disables ps poll power save.
 *
 * @return WIFI_ENGINE_SUCCESS.
 */
int WiFiEngine_EnableLegacyPsPollPowerSave(bool_t enable)
{
   rPowerManagementProperties* powerManagement;    

   powerManagement = (rPowerManagementProperties*)Registry_GetProperty(ID_powerManagement);

   powerManagement->enablePsPoll = enable;
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Checks if ps poll power save is active.
 *
 *
 * @return true if ps poll power save
 */
bool_t WiFiEngine_LegacyPsPollPowerSave(void)
{
   rPowerManagementProperties* powerManagement;    

   powerManagement = (rPowerManagementProperties*)Registry_GetProperty(ID_powerManagement);

   return powerManagement->enablePsPoll;
}

/*!
 * @brief Configuration changed via NRX-api.
 *        Notify state machine.
 *
 * @return true if ps poll power save
 */
int WiFiEngine_LegacyPsConfigurationChanged(void)
{

 /* Obsolete all settings is done via mib:s */
#if 0   
   wei_sm_queue_sig(INTSIG_LEGACY_PS_CONFIGURATION_CHANGED, SYSDEF_OBJID_HOST_MANAGER_PS, DUMMY, FALSE);
  
   /* Let the driver act upon the new message. */
   wei_sm_execute();   
#endif
   return 0;
}


/*!
 * @brief Handles delay os power save.
 *
 * @return always 0
 */

static int delay_ps_timeout_cb(void *data, size_t len)
{
   DE_TRACE_STATIC(TR_NOISE, "====> delay_ps_timeout_cb\n");
     
   if(WES_TEST_FLAG(WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING))
   {
      WES_CLEAR_FLAG(WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING);
      WiFiEngine_AllowPowerSave(wifiEngineState.delay_ps_uid);      
      WiFiEngine_AllowPowerSave(wifiEngineState.dhcp_ps_uid);
   }
   DE_TRACE_STATIC(TR_NOISE, "<==== delay_ps_timeout_cb\n");   

   return 0;
}





/*!
 * @brief Start a timer to monoitor traffic (tx).
 *
 * Start a timer to delay start of power save.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_StartDelayPowerSaveTimer(void)
{
   DE_TRACE_STATIC(TR_NOISE, "====> WiFiEngine_StartDelayPowerSaveTimer\n");  
         
      if(WES_TEST_FLAG(WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING))
      {
         DE_TRACE_STATIC(TR_NOISE, "Timer already started ignore\n");
      }
      else
      {
         uint16_t interval;
                  
         WiFiEngine_GetDelayStartOfPs(&interval);
      
         if (DriverEnvironment_RegisterTimerCallback(interval, wifiEngineState.monitor_traffic_timer_id, delay_ps_timeout_cb, FALSE ) != 1)     
         {
            DE_TRACE_STATIC(TR_NOISE, "No monitor traffic callback registered, DE was busy\n");
         }
         else
         {
            WiFiEngine_InhibitPowerSave(wifiEngineState.delay_ps_uid); 
            WES_SET_FLAG(WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING);
      }
   }
   
   DE_TRACE_STATIC(TR_NOISE, "<==== WiFiEngine_StartDelayPowerSaveTimer\n");  
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Stop the timer that delays power save.
 *
 *
 * @return WIFI_ENGINE_SUCCESS
 */
void WiFiEngine_StopDelayPowerSaveTimer(void)
{
   if (!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) return;
   DE_TRACE_STATIC(TR_NOISE, "====> WiFiEngine_StopDelayPowerSaveTimer\n");  
   /* Stop timer */
 
   WIFI_LOCK();
   DriverEnvironment_CancelTimer(wifiEngineState.monitor_traffic_timer_id);
   WIFI_UNLOCK();

   if(WES_TEST_FLAG(WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING))
   {
      DE_TRACE_STATIC(TR_NOISE, "Timer is running decrement is80211PowerSaveInhibit\n");       
      WES_CLEAR_FLAG(WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING);      
      WiFiEngine_AllowPowerSave(wifiEngineState.delay_ps_uid); 
#if 0      
      DE_ASSERT(wifiEngineState.is80211PowerSaveInhibit >= 0);
#endif
   }
   else
   {
      DE_TRACE_STATIC(TR_NOISE, "Timer is not running skip decrement of is80211PowerSaveInhibit\n");       
   }
   DE_TRACE_INT(TR_NOISE, "is80211PowerSaveInhibit %d\n", wifiEngineState.is80211PowerSaveInhibit);          
   
   DE_TRACE_STATIC(TR_NOISE, "<==== WiFiEngine_StopDelayPowerSaveTimer\n");  
}

bool_t WiFiEngine_IsDelayPowerSaveTimerRunning(void)
{
   return WES_TEST_FLAG(WES_FLAG_IS_DELAYED_PS_TIMER_RUNNING);
}

/*!
 * @brief Put the device in power save state.
 *
 * Note that the success status from this call only means that the
 * process has been initiated, not that the device is in power save
 * state yet.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SleepDevice(void)
{
   wei_sm_queue_sig(INTSIG_DEVICE_POWER_SLEEP, SYSDEF_OBJID_HOST_MANAGER_PS, DUMMY, FALSE);
   
   /* Let the driver act upon the new message. */
   wei_sm_execute();

   return WIFI_ENGINE_SUCCESS;
}




/*!
 * @brief Get the NIC power mode.
 *
 * Retrieves the NIC power mode. Modes are enumerated by 
 * WiFiEngine_PowerMode_t. 
 *
 * @param power_mode Output buffer.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
rPowerSaveMode   WiFiEngine_GetRegistryPowerFlag(void)
{
   rPowerSaveMode mode;

   mode = registry.powerManagement.mode;

   return mode;

}

/*!
 * @brief Get the NIC power mode.
 *
 * Retrieves the NIC power mode. Modes are enumerated by 
 * WiFiEngine_PowerMode_t. 
 *
 * @param power_mode Output buffer.
 *
 * @return Always returns WIFI_ENGINE_SUCCESS
 */
int   WiFiEngine_GetPowerMode(WiFiEngine_PowerMode_t *power_mode)
{
   switch (registry.powerManagement.mode)
   {
      case PowerSave_Disabled_Permanently:
         *power_mode = WIFI_ENGINE_PM_ALWAYS_ON;
         break;
      case PowerSave_Enabled_Activated_From_Start:
      case PowerSave_Enabled_Deactivated_From_Start:   
         *power_mode = WIFI_ENGINE_PM_DEEP_SLEEP;
         break;
      default:
         DE_TRACE_STATIC(TR_SEVERE, "Invalid power mode detected in registry\n");
         return WIFI_ENGINE_FAILURE;
   }

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Get a user id to be used when inhibit/allow power save.
 *
 */
we_ps_control_t *WiFiEngine_GetPowerSaveUid(const char *name)
{
   we_ps_control_t *control;
   
   control = WiFiEngine_PSControlAlloc(name);
   DE_ASSERT(control != NULL);

   return control;
}

/*!
 * @brief Print allocated strings 
 *
 * @return Nothing.
 */
static void we_ps_control_trace(void)
{
   unsigned int i;
   char msg[64];
   if(!wifiEngineState.ps_inhibit_state) {
      DE_TRACE_STATIC(TR_PS, "No user has inhibited ps\n");
     return;
   }
   *msg = 0;
   for(i = 0; i < DE_ARRAY_SIZE(we_ps_control_data); i++) {
      if((wifiEngineState.ps_inhibit_state & 
          we_ps_control_data[i].control_mask) != 0) {
         DE_SNPRINTF(msg + strlen(msg), sizeof(msg) - strlen(msg), " %s", we_ps_control_data[i].name);
      }
   }
   DE_TRACE_STRING(TR_PS, "inhibit powersave:%s\n", msg);
}


/*!
 * @brief Set the NIC power mode.
 *
 * Sets the NIC power mode. Modes are  enumerated by 
 * WiFiEngine_PowerMode_t. If target sleeps a sequence of messages
 * is needed to bring uo target to active mode. A new call to this
 * function will fail if the sequence is not complete. If target is
 * awake (not wmm) there is no need to perform the wakeup sequence.
 * If target is not awake or in wmm/wake the new mode is stored temporary
 * and will be changed when target is awake (not wmm).
 *
 * @param power_mode Input buffer.
 *
 * @return Return WIFI_ENGINE_SUCCESS if ok
 *                WIFI_ENGINE_FAILURE_DEFER is the sequence to bring
 *                up target to active is not completed.
 */
int   WiFiEngine_SetPowerMode(WiFiEngine_PowerMode_t power_mode, we_ps_control_t *ps_uid)
{
   DE_TRACE_STATIC(TR_PS, "WiFiEngine_SetPowerMode called\n"); 

   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) {
      /* we don't have any hardware, so all we can do is note the
       * request for future use */
      switch(power_mode) {
         case WIFI_ENGINE_PM_ALWAYS_ON:
            registry.powerManagement.mode = PowerSave_Disabled_Permanently;
            break;
         case WIFI_ENGINE_PM_DEEP_SLEEP:
         case WIFI_ENGINE_PM_802_11_PS:
            registry.powerManagement.mode = PowerSave_Enabled_Activated_From_Start;
            break;
         default:
            DE_TRACE_INT(TR_PS, "Request for unknown power mode (%d) when unplugged\n", power_mode);  
            return WIFI_ENGINE_FAILURE_DEFER;
      }
      DE_TRACE_INT(TR_PS, "Registry power mode is %d\n", 
                   registry.powerManagement.mode);  
      return WIFI_ENGINE_SUCCESS;
   }
   switch(power_mode)
   {  
      case WIFI_ENGINE_PM_ALWAYS_ON:
      {   
          DE_TRACE_STATIC(TR_PS, "Disable power save\n");

         wifiEngineState.power_mode = WIFI_ENGINE_PM_ALWAYS_ON;               
         registry.powerManagement.mode = PowerSave_Disabled_Permanently;

         WiFiEngine_InhibitPowerSave(ps_uid);

      }
      break;
      case WIFI_ENGINE_PM_DEEP_SLEEP:
      case WIFI_ENGINE_PM_802_11_PS:
      {
         DE_TRACE_STATIC(TR_PS, "Enable power save\n");  
        
         wifiEngineState.power_mode = WIFI_ENGINE_PM_DEEP_SLEEP;
         registry.powerManagement.mode = PowerSave_Enabled_Activated_From_Start;
         
         WiFiEngine_AllowPowerSave(ps_uid);     
      }
      break;
      case WIFI_ENGINE_PM_SOFT_SHUTDOWN:
      {
         DE_TRACE_STATIC(TR_PS, "WIFI_ENGINE_PM_SOFT_SHUTDOWN\n");           
         WiFiEngine_SleepDevice();         
      }
      break;
      default:
         DE_TRACE_STATIC(TR_SEVERE, "Tried to set invalid power mode in registry\n");
         return WIFI_ENGINE_FAILURE;
   }

   DE_TRACE_INT(TR_PS, "Power power mode is %d\n", power_mode);  
   DE_TRACE_INT(TR_PS, "Registry power mode is %d\n",   registry.powerManagement.mode);  
   
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Set to inhibit power save during critical sequences .
 *
 */
void WiFiEngine_DisablePowerSaveForAllUid(void)
{ 
   we_ps_control_t psc;
   const char* name = "clear all ps_uid";
   
   DE_TRACE_STATIC(TR_NOISE, "====> WiFiEngine_DisablePowerSaveForAllUid\n");

   if(registry.powerManagement.mode == PowerSave_Disabled_Permanently)
   {
      wifiEngineState.power_mode = WIFI_ENGINE_PM_DEEP_SLEEP;
      registry.powerManagement.mode = PowerSave_Enabled_Activated_From_Start;
   }

   if(wifiEngineState.ps_inhibit_state)
   {
      psc.control_mask = wifiEngineState.ps_inhibit_state ;
      DE_STRNCPY(psc.name, name, sizeof(psc.name));
               
      WiFiEngine_PSControlAllow(&psc);  
   }
   
   DE_TRACE_STATIC(TR_NOISE, "<==== WiFiEngine_DisablePowerSaveForAllUid\n");   
}

void WiFiEngine_InhibitPowerSave(we_ps_control_t *control)
{ 
   
   DE_TRACE_STATIC(TR_NOISE, "====> WiFiEngine_InhibitPowerSave\n");

   WiFiEngine_PSControlInhibit(control);
   
   DE_TRACE_STATIC(TR_NOISE, "<==== WiFiEngine_InhibitPowerSave\n");   
}


/*!
 * @brief Set to allow power save during critical sequences .
 *
 */
void WiFiEngine_AllowPowerSave(we_ps_control_t *control)
{
      
   DE_TRACE_STATIC(TR_NOISE, "====> WiFiEngine_AllowPowerSave\n");   
  
   if(control != NULL)
   {
      WiFiEngine_PSControlAllow(control); 
   }

   DE_TRACE_STATIC(TR_NOISE, "<==== WiFiEngine_AllowPowerSave\n");
   
}

/*!
 * @brief Only set the user bit to prevent power save to start.
 *
 */
void WiFiEngine_DisablePowerSave(we_ps_control_t *control)
{
      
   DE_TRACE_STATIC(TR_NOISE, "====> WiFiEngine_DisablePowerSave\n");   
  
   wifiEngineState.ps_inhibit_state |= control->control_mask;
   we_ps_control_trace();
   
   DE_TRACE_STATIC(TR_NOISE, "<==== WiFiEngine_DisablePowerSave\n");
   
}


/*!
 * @brief Set to allow power save during critical sequences .
 *
 */
int WiFiEngine_isPSCtrlAllowed(we_ps_control_t *psc)
{
   int i;

   if(psc == NULL)
   {
      return 0;
   }

   WIFI_LOCK();
   i = (wifiEngineState.ps_inhibit_state & psc->control_mask);
   WIFI_UNLOCK();

   return (i==0);
}


/*!
 * @brief Allocate a ps controll identity to be used when
 *        WiFiEngine_InhibitPowerSave/Allow is called.
 *
 *
 * @param name user friendly name.
 *
 * @return Return allocated slot
 */
void wei_ps_ctrl_alloc_init(void)
{
   unsigned int i;

   for(i = 0; i < DE_ARRAY_SIZE(we_ps_control_data); i++) 
   {
      we_ps_control_data[i].control_mask = 0;
   }

   wifiEngineState.dhcp_ps_uid = WiFiEngine_PSControlAlloc("DHCP");
   wifiEngineState.delay_ps_uid = WiFiEngine_PSControlAlloc("DELAY");
   wifiEngineState.ibss_ps_uid = WiFiEngine_PSControlAlloc("IBSS");
   wifiEngineState.ps_state_machine_uid = WiFiEngine_GetPowerSaveUid("ps_state_machine");   
}

/*!
 * @brief deallocate a ps controll identity to be used when
 *        WiFiEngine_InhibitPowerSave/Allow is called.
 *
 *
 * @param name user friendly name.
 *
 * @return Return allocated slot
 */
void wei_ps_ctrl_shutdown(void)
{
   WiFiEngine_PSControlFree(wifiEngineState.dhcp_ps_uid);
   wifiEngineState.dhcp_ps_uid = NULL;
   WiFiEngine_PSControlFree(wifiEngineState.delay_ps_uid);
   wifiEngineState.delay_ps_uid = NULL;
   WiFiEngine_PSControlFree(wifiEngineState.ps_state_machine_uid );
   wifiEngineState.ps_state_machine_uid  = NULL;
   WiFiEngine_PSControlFree(wifiEngineState.ibss_ps_uid );
   wifiEngineState.ibss_ps_uid  = NULL;  
}


/*!
 * @brief Allocate a ps controll identity to be used when
 *        WiFiEngine_InhibitPowerSave/Allow is called.
 *
 *
 * @param name user friendly name.
 *
 * @return Return allocated slot
 */
we_ps_control_t *WiFiEngine_PSControlAlloc(const char *name)
{
   unsigned int i;
   DE_TRACE_STRING(TR_PS, "ENTRY: %s\n", name);
   for(i = 0; i < DE_ARRAY_SIZE(we_ps_control_data); i++) {
      if(we_ps_control_data[i].control_mask != 0) {
         continue;
      }
      WIFI_LOCK();
      if(we_ps_control_data[i].control_mask != 0) {
         WIFI_UNLOCK();
         continue;
      }
      we_ps_control_data[i].control_mask = (1 << i);
      WIFI_UNLOCK();
      DE_STRNCPY(we_ps_control_data[i].name, name, sizeof(we_ps_control_data[i].name));
      we_ps_control_data[i].name[sizeof(we_ps_control_data[i].name) - 1] = '\0';
      DE_TRACE_INT(TR_PS, "EXIT: allocate slot %d\n", i);
      return &we_ps_control_data[i];
   }
   DE_TRACE_STATIC(TR_PS, "EXIT: no free slots\n");
   return NULL;
}




/*!
 * @brief Deallocate a ps controll identity to be used when
 *        WiFiEngine_InhibitPowerSave/Allow is called.
 *
 * @param Allocated struct.
 *
 * @return Nothing.
 */
void WiFiEngine_PSControlFree(we_ps_control_t *psc)
{
   unsigned int i;
   
   DE_ASSERT(psc->control_mask != 0);
   DE_TRACE_STRING(TR_PS, "ENTRY: %s\n", psc->name);
   psc->control_mask = 0;
   for(i=0;i<sizeof(psc->name);i++)
   {
      psc->name[i] = 0;
   }
}

void WiFiEngine_PSControlAllow(we_ps_control_t *psc)
{
   DE_TRACE_STRING(TR_PS, "ENTRY: %s\n", psc->name);
   WIFI_LOCK();
   
   if(WiFiEngine_isPowerSaveAllowed())
   {
      WIFI_UNLOCK();
      DE_TRACE_STATIC(TR_PS, "No bits to clear - power save already enabled\n");
      return;
   }
   
   wifiEngineState.ps_inhibit_state &= ~psc->control_mask;
   we_ps_control_trace();
   WIFI_UNLOCK();

   if(WiFiEngine_isPowerSaveAllowed())
   {   
      DE_TRACE_STATIC(TR_NOISE, "Request to enable power save\n"); 
      we_pm_request(INTSIG_ENABLE_PS);    
      
   }  
    DE_TRACE_STATIC(TR_PS, "EXIT\n");  
}

void WiFiEngine_PSControlInhibit(we_ps_control_t *psc)
{
   int allow;
   DE_TRACE_STRING(TR_PS, "ENTRY: %s\n", psc->name);

   WIFI_LOCK();
   allow = WiFiEngine_isPowerSaveAllowed();
   wifiEngineState.ps_inhibit_state |= psc->control_mask;
   we_ps_control_trace();
   WIFI_UNLOCK();
   if(allow && WES_TEST_FLAG(WES_FLAG_HW_PRESENT))
   {
      DE_TRACE_STATIC(TR_NOISE, "Request to disable power save\n"); 
      we_pm_request(INTSIG_DISABLE_PS);
     
   }
    DE_TRACE_STATIC(TR_PS, "EXIT\n");  
}



/*!
 * @brief Set to allow power save during critical sequences .
 *
 */
int WiFiEngine_isPowerSaveAllowed()
{
   return wifiEngineState.ps_inhibit_state == 0;
}

/*!
 * @brief Indicates that upper layer has finished DHCP
 *        negotiation.
 *
 *
 * @return WIFI_ENGINE_SUCCESS
 */
void WiFiEngine_DHCPStart(void)
{
   DE_TRACE_STATIC(TR_PS, "==> WiFiEngine_DHCPStart\n"); 

   WiFiEngine_InhibitPowerSave(wifiEngineState.dhcp_ps_uid);
  
   DE_TRACE_STATIC(TR_PS, "<== WiFiEngine_DHCPStart\n");    
 }

/*!
 * @brief Indicates that upper layer has finished DHCP
 *        negotiation.
 *
 *
 * @return WIFI_ENGINE_SUCCESS
 */
void WiFiEngine_DHCPReady(void)
{
   DE_TRACE_STATIC(TR_PS, "==> WiFiEngine_DHCPReady\n"); 
   WiFiEngine_StopDelayPowerSaveTimer();
   WiFiEngine_AllowPowerSave(wifiEngineState.dhcp_ps_uid);

   DE_TRACE_STATIC(TR_PS, "<== WiFiEngine_DHCPReady\n");    
 }

/*!
 * @brief Put the device in deep sleep forever.
 *
 * Note that the wake-up should involve target reset and reload.
 *
 * @return WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SoftShutdown(void)
{

   wei_sm_queue_sig(INTSIG_PS_SLEEP_FOREVER, SYSDEF_OBJID_HOST_MANAGER_TRAFFIC, DUMMY, FALSE);
      
   /* Let the pm state machine act upon the new message. */
   wei_sm_execute(); 
   
   return WIFI_ENGINE_SUCCESS;   
}




/** @} */ /* End of we_ps group */

