/*
 * This is the soul purpose of this file:
 *
 * scan, wps connect, scan, inform about 
 * final connect to host specific driver.
 *
 * This file deal with scanning, connecting and 
 * scanning a second time for the new network. 
 * Almost all of the work done here are related 
 * to WiFiEngine and almost nothing with the 
 * supplicant. The code is designed to be 
 * portable to other platforms but assume that 
 * the supplicant is compiled with this code.
 *
 */

#include "driverenv.h"
#include "wifi_engine.h"
#include "wifi_engine_internal.h"
#include "we_cm.h"
#include "wei_netlist.h"

#include "we_scan_suite.h"
#include "wps_scan_and_connect.h"
#include "common.h"
#include "config_ssid.h"

#ifdef EAP_WSC

/* supplicant stuff */
void wps_set_ssid(const char *pin, char *ssid_str, int wps_mode);

/*************************************************/

typedef struct wps_ctx {
   scan_suite_s scan;

   iobject_t *disconnected_h;
   iobject_t *wps_creds_h;

   wps_complete_cb_t complete_cb;

   WiFiEngine_net_t *net;
   WiFiEngine_net_t *final_net;

   /* bool_t */
   int disconnected;
   int creds_obtained;
   int err_multiple_nets;

   /* of final net */
   m80211_ie_ssid_t ssid;

   // from we_cm.h
   void *connection; 
   int max_connect_retrys;

   /* pin must be a NULL terminated string */
   char pin[32];
   int pin_len;

   /* needed for wps test case 5.1.8 */
   int num_of_nets;
   int num_of_parsed_nets;

   int scan_runs;
   char ma_ssid[MAX_SSID_LEN+1];
   int ma_ssid_len;
   
   int force_wps_connection;

   //void *priv;
} wps_ctx_s;

/*************************************************/

static int _scan_setup(scan_suite_s *ss, i_func_t scan_ind_cb, i_func_t scan_complete_cb);
static void wps_scan_ind_cb(wi_msg_param_t param, void* priv);
static void wps_connect(i_func_t func, wps_ctx_s* ctx);
//static void wps_creds_obtained_cb(wi_msg_param_t param, void* priv);
static void wps_disconnected_cb(wi_msg_param_t param, void* priv);
static void wps_connect_scan_ind_cb(wi_msg_param_t param, void* priv);
static int wps_timeout_expired(void *data, size_t data_len);

/*************************************************/

driver_timer_id_t wps_timeout_timer_id;
wps_ctx_s _wps_ctx;

#define GET_WPS_CTX (&_wps_ctx);

/*************************************************/


static int
_scan_setup(
      scan_suite_s *ss,
      i_func_t scan_ind_cb, 
      i_func_t scan_complete_cb)
{
   int s;

   s = WiFiEngine_CreateScanSuite(&ss);
   if(s != WIFI_ENGINE_SUCCESS)
      return WPS_CODE_INTERNAL_ERROR;

   s = WiFiEngine_ActivateScanSuite(ss, scan_ind_cb, scan_complete_cb);
   if(s != WIFI_ENGINE_SUCCESS) 
      return WPS_CODE_INTERNAL_ERROR;

   return WIFI_ENGINE_SUCCESS;
}


static void _free_net(WiFiEngine_net_t **net_p)
{
   WiFiEngine_net_t *net;

   if(!net_p) return;

   net = *net_p;
   if(!net) return;

   net->ref_cnt--;
   wei_netlist_free_net_safe(net);
   
   *net_p = NULL;
}

static void cleanup(wps_ctx_s* ctx)
{
   m80211_mac_addr_t bssid;

   DE_TRACE_STATIC(TR_WPS,"cleanup\n");
   
   ctx->num_of_nets = 0;
   ctx->num_of_parsed_nets = 0;
   ctx->err_multiple_nets = 0;
   ctx->scan_runs = 0;
   ctx->force_wps_connection = 0;

   /* timers */
   DriverEnvironment_CancelTimer(wps_timeout_timer_id);
   DriverEnvironment_FreeTimer(wps_timeout_timer_id);
   wps_timeout_timer_id = 0;

   /* free indications */
   we_ind_deregister_null(&ctx->disconnected_h);
   we_ind_deregister_null(&ctx->wps_creds_h);

   /* cancel scan */
   if(&ctx->scan != NULL)
		WiFiEngine_DeactivateScanSuite(&ctx->scan);

   /* issue disconnect */
   if(ctx->connection) we_cm_disconnect(ctx->connection);

   /* reset properties set by wps_connect() */
   DE_MEMSET(&bssid, 0xff, sizeof(m80211_mac_addr_t));
   WiFiEngine_SetDesiredBSSID(&bssid);
   WiFiEngine_SetSSID(NULL, 0);

   /* free context */
   _free_net(&ctx->net);
   _free_net(&ctx->final_net);
   
   /* this will disable wps in the supplicant */
   wps_set_ssid(NULL, NULL, 3);
}


static int wps_timeout_expired(void *data, size_t data_len)
{
   wps_ctx_s* ctx;

   DE_TRACE_STATIC(TR_WPS,"timeout!\n");

   ctx = GET_WPS_CTX;
   wps_abort(ctx, WPS_CODE_TIMEOUT);

   return 0;
}

/**
 * use this if unpatient
 *
 */
void wps_abort(void* wps_priv, int reason)
{
   wps_ctx_s* ctx;

   DE_TRACE_STATIC(TR_WPS,"abort\n");

   ctx = (wps_ctx_s*)wps_priv; 
   if(!ctx)
      return;

   cleanup(ctx);
   (*ctx->complete_cb)(reason);
}

/**
 * This is the start 
 *
 * param wps_complete_cb to be called after abort/timeout/success/failure
 * return context on success; use this context on call to wps_abort()
 *
 **/
void* wps_scan_and_connect(
      const char *ssid,
      char *pin,
      size_t pin_len,
      wps_complete_cb_t wps_complete_cb,
      int timeout)
{
   wps_ctx_s* ctx;
   scan_suite_s *ss;
   int s;

   wps_reset_credentials();

   ctx = GET_WPS_CTX;
   memset(ctx,0,sizeof(wps_ctx_s));
   ctx->max_connect_retrys = 3;
   ctx->complete_cb = wps_complete_cb;
   DE_ASSERT(wps_complete_cb);

   if(pin && pin_len > 0)
   {
      DE_ASSERT(pin_len < sizeof(ctx->pin));
      DE_ASSERT(DE_STRLEN(pin) == pin_len);
      DE_MEMCPY(&ctx->pin[0], pin,pin_len);
      ctx->pin_len = pin_len;
      /* must be a NULL terminated string */
      ctx->pin[pin_len] = 0;
   } else {
      ctx->pin_len = 0;
   }

   if(ssid) {
      DE_ASSERT(DE_STRLEN(ssid) <= MAX_SSID_LEN);
      ctx->ma_ssid_len = DE_STRLEN(ssid);
      DE_MEMCPY(&ctx->ma_ssid[0], ssid, ctx->ma_ssid_len );   
   }

   ss = &ctx->scan;
   /* will do MEMSET(ss,0,sizeof(ss)) */
   s = _scan_setup(ss, wps_scan_ind_cb, NULL );
   if(s != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_INT(TR_SEVERE, "s=%d\n",s);
      cleanup(ctx);
      (*ctx->complete_cb)(WPS_CODE_INTERNAL_FAILURE);
      return NULL;
   }
   ctx->scan.priv = ctx;

   DriverEnvironment_GetNewTimer(&wps_timeout_timer_id, 0);
   DriverEnvironment_RegisterTimerCallback(timeout*1000,
         wps_timeout_timer_id,
         wps_timeout_expired,
         1);
   
   ctx->err_multiple_nets=0;
   s = WiFiEngine_SetScanJobState(ss->scan_job.id, 1 /* start */ ,NULL);
   if(s != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_INT(TR_WARN, "s=%d\n",s);
      cleanup(ctx);
      (*ctx->complete_cb)(WPS_CODE_INTERNAL_FAILURE);
      return NULL;
   }

   return (void*)ctx;
}

#define MAX_SCAN_RUNS 4

static void wps_scan_ind_cb(wi_msg_param_t param, void* priv)
{
   WiFiEngine_net_t *net = (WiFiEngine_net_t*)param;
   scan_suite_s *ss = (scan_suite_s*)priv;
   wps_ctx_s* ctx;
   int s;
   int num_of_nets = 0;
   m80211_ie_ssid_t *ssid_ie;

   if(!ss) return;
   if(!net) return;
   if(!ss->priv) return;
   ctx = (wps_ctx_s*)ss->priv;

   ctx->num_of_parsed_nets++;
   
   DE_TRACE_INT(TR_WPS, "num_of_parsed_nets=%d\n", ctx->num_of_parsed_nets);
   
   /* This is needed in order to pass wps test case 5.1.8. 
      When two AP's are configured with PBC and they are both active then we
      should not allow connection. This means that we should parse the
      complete list of the active nets to see if we have more than one wps active AP's */ 
   if(!ctx->num_of_nets) 
   {
       DE_TRACE_STATIC(TR_WPS, "Refresh num_of_net from active list\n");
       WiFiEngine_GetScanList(NULL, &num_of_nets);
       ctx->num_of_nets = num_of_nets;
   }
   
   DE_TRACE_INT(TR_WPS, "num_of_nets=%d\n",ctx->num_of_nets);

   DE_ASSERT(ctx->num_of_parsed_nets <= ctx->num_of_nets);
   
   if((ctx->num_of_parsed_nets == ctx->num_of_nets))
	   ctx->scan_runs++;
 
    DE_TRACE_INT(TR_WPS, "scan_runs=%d\n",ctx->scan_runs);
   /* Some WPS APs (like broadcom) do no set 'Selected Registrar' attribute to 1 properly when using an
      external registrar (e.g vista). Allow such an AP to be selected for PIN registration after a couple
      of scan runs that do not find APs marked with 'Selected Registrar = 1'. This will allow the driver 
      to iterate through all APs that advertize WPS support without delaying connection with implementations
      that set 'Selected Registrar = 1' properly */
   if( (ctx->scan_runs < MAX_SCAN_RUNS) || (ctx->pin_len == 0) || ((ctx->scan_runs == MAX_SCAN_RUNS) && (ctx->num_of_parsed_nets == ctx->num_of_nets)) ) {
   if(we_filter_out_wps_configured(net)) {
      if((ctx->num_of_parsed_nets == ctx->num_of_nets))
         DE_TRACE_INT(TR_WPS, "this is the last scan ind, num_of_nets=%d\n",ctx->num_of_nets);
      else
         return;
   } 
   else {
		/* It's possible that an AP will update it's configuration during WPS processing.
		   In that case we should use the net with the latest configuration. */
		if(ctx->net) {
			if(DE_MEMCMP(ctx->net->bss_p->bss_description_p->ie.ssid.ssid, ctx->ma_ssid, ctx->ma_ssid_len) == 0) {
				DE_TRACE_STATIC(TR_WPS,"Found the same net again! Update the old one...n");
				ctx->err_multiple_nets = 0;
			}
		}   
		ctx->err_multiple_nets++; 
   }
   }
   else {
      DE_TRACE_STATIC(TR_WPS,"No active WPS APs found. Search for WPS capable APs\n");
	   
      /* we are only interested for PIN registration */
      if(ctx->pin_len > 0) {
         /* get all the WPS APs that advertise wps support */
         if(we_filter_out_wps_configured(net) != WIFI_NET_FILTER_WPS_NOT_CONFIGURED) {
            ctx->num_of_nets = ctx->num_of_parsed_nets = 0; 
            return;
         }
			
         DE_TRACE_STATIC(TR_WPS,"Found WPS capable AP. Check if it is the desired one\n");
         /* check if this net is the desired one */
         ssid_ie = &net->bss_p->bss_description_p->ie.ssid;
         if(ssid_ie->hdr.len != ctx->ma_ssid_len) {
            ctx->num_of_nets = ctx->num_of_parsed_nets = 0;
            return;
         }
         else {
            if(DE_MEMCMP(ssid_ie->ssid, ctx->ma_ssid, ctx->ma_ssid_len)) {
               ctx->num_of_nets = ctx->num_of_parsed_nets = 0;
               return;
         }
         }
         ctx->scan_runs = 0;
         ctx->err_multiple_nets = 1; /* ignore multiple nets since it's required only for PBC sessions */
         ctx->force_wps_connection = 1; /* we found our net, no need to wait anymore */
      }
   }
			
   DE_TRACE_INT(TR_WARN, "err_multiple_nets=%d\n",ctx->err_multiple_nets);
   
   if(!ctx->net && (ctx->num_of_parsed_nets == ctx->num_of_nets) && (ctx->err_multiple_nets == 0))
   {
       DE_TRACE_STATIC(TR_WPS,"No WPS net found in the list of active nets, get new list\n");
       WiFiEngine_GetScanList(NULL, &num_of_nets);
       ctx->num_of_nets = num_of_nets;
       ctx->num_of_parsed_nets = 0;
       return;
   }
   DE_TRACE_STATIC(TR_WPS,"found WPS net\n");

   if((ctx->num_of_parsed_nets < ctx->num_of_nets) || ((ctx->num_of_parsed_nets == ctx->num_of_nets) && !ctx->net))
   {
      ctx->net = net;
      net->ref_cnt++;
   }
   
   if(ctx->err_multiple_nets > 1)
   {
      DE_TRACE_STATIC(TR_WPS,"Oops; more than one!\n");
      cleanup(ctx);
      (*ctx->complete_cb)(WPS_CODE_MULTIPLE_NETS);
      return;
   }
   
   /* The connection should only initiated after parsing the complete
      list of active nets because of wps test case 5.1.8 */
   if((ctx->num_of_parsed_nets < ctx->num_of_nets) && !ctx->force_wps_connection)
      return;
   
   ctx->num_of_parsed_nets = 0;
   ctx->force_wps_connection = 0;

   s = WiFiEngine_SetScanJobState(ss->scan_job.id, 0 /* stop */ ,NULL);
   if(s != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_INT(TR_WARN, "s=%d\n",s);
   }
   WiFiEngine_DeactivateScanSuite(ss);
   wps_connect(wps_disconnected_cb, ctx);
}


static void wps_connect(i_func_t func, wps_ctx_s* ctx)
{
   char ssid_str[M80211_IE_MAX_LENGTH_SSID+1];
   m80211_ie_ssid_t *ssid_ie;

   ctx->disconnected_h = we_ind_register(
         WE_IND_CM_DISCONNECTED,
         "wps disconnected",
         func,
         NULL,
         1,
         ctx);
   DE_ASSERT(ctx->disconnected_h);

   ssid_ie = &ctx->net->bss_p->bss_description_p->ie.ssid;
   WiFiEngine_SetSSID(&ssid_ie->ssid[0], ssid_ie->hdr.len);
   WiFiEngine_SetDesiredBSSID(&ctx->net->bss_p->bssId);

   //WiFiEngine_SetAuthenticationMode(Authentication_8021X);
   WiFiEngine_SetAuthenticationMode(Authentication_Open);
   WiFiEngine_SetEncryptionMode(Encryption_Disabled);

   /*
    * 1: PBC
    * 2: PIN
    */
   wei_printSSID(ssid_ie, ssid_str, sizeof(ssid_str));
   if(ctx->pin_len > 0)
   {
      wps_set_ssid(ctx->pin, ssid_str, 2);
   } else {
      wps_set_ssid(NULL, ssid_str, 1);
   }

   ctx->connection = we_cm_connect(ctx->net);
   if(!ctx->connection)
   {
      cleanup(ctx);
      (*ctx->complete_cb)(WPS_CODE_INTERNAL_ERROR);
   }

#if 0
   ctx->wps_creds_h = we_ind_register(
         WE_IND_WPS_CREDENTIALS_OBTAINED,
         "wps creds",
         wps_creds_obtained_cb,
         NULL,
         1,
         ctx);
   DE_ASSERT(ctx->wps_creds_h);
#endif
}


#if 0
static void wps_creds_obtained_cb(wi_msg_param_t param, void* priv)
{
   scan_suite_s *ss = (scan_suite_s*)priv;
   wps_ctx_s* ctx;
   int s;

   DE_ASSERT(ss);
   DE_ASSERT(ss->priv);
   ctx = (wps_ctx_s*)ss->priv;

   if(!ctx) return;

   ctx->scan.priv = ctx;
   s = _scan_setup(&ctx->scan, wps_connect_scan_ind_cb, NULL );
   if(s != WIFI_ENGINE_SUCCESS)
   {
      (*ctx->complete_cb)(WPS_INTERNAL_ERROR);
      cleanup(ctx);
      return;
   }
   //WiFiEngine_TriggerScanJob(ss->scan_job.id,0);
   WiFiEngine_TriggerScanJob(0,0); // default
}
#endif


static void wps_disconnected_cb(wi_msg_param_t param, void* priv)
{
   m80211_ie_ssid_t ssid;
   scan_suite_s *ss = (scan_suite_s*)priv;
   wps_ctx_s* ctx;
   int s;
   int len;

   if(!ss) return;
   if(!ss->priv) return;
   ctx = (wps_ctx_s*)ss->priv;

   len = wps_get_ssid(&ssid.ssid[0], sizeof(ssid.ssid));
   if( len <= 0 )
   {
      /* NO SSID FOUND */
      if(ctx->max_connect_retrys>0)
      {
         DE_TRACE_INT(TR_WPS,"WPS disconnected; no credentials obtained;"
               " retry count %d\n", ctx->max_connect_retrys);
         wps_connect(wps_disconnected_cb, ctx);
         ctx->max_connect_retrys--;
      } else {
         DE_TRACE_STATIC(TR_WPS,"WPS disconnected; no credentials obtained\n");
         cleanup(ctx);
         (*ctx->complete_cb)(WPS_CODE_FAILURE);
      }
      return;
   }
   ssid.hdr.len = len;

   ctx->connection = NULL;

   /* this will disable wps in the supplicant */
   wps_set_ssid(NULL, NULL, 3);

   /* start scanning for the second net. Needed to get updated info about the newly configured AP */

   /* does MEMSET(ss,0,sizeof(ss)) */
   s = WiFiEngine_CreateScanSuite(&ss);
   if(s != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_INT(TR_WARN, "s=%d\n",s);
      cleanup(ctx);
      (*ctx->complete_cb)(WPS_CODE_INTERNAL_FAILURE);
      return;
   }

   s = WiFiEngine_ActivateScanSuite(ss, 
         wps_connect_scan_ind_cb, NULL);
   if(s != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_INT(TR_WARN, "s=%d\n",s);
      cleanup(ctx);
      (*ctx->complete_cb)(WPS_CODE_INTERNAL_FAILURE);
      return;
   }

   s = WiFiEngine_SetScanJobState(ss->scan_job.id, 1 /* start */ ,NULL);
   if(s != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_INT(TR_WARN, "s=%d\n",s);
      cleanup(ctx);
      (*ctx->complete_cb)(WPS_CODE_INTERNAL_FAILURE);
      return;
   }
   ctx->scan.priv = ctx;
   ss->scan_job.ssid = ssid;
   WiFiEngine_SetSSID(&ssid.ssid[0], ssid.hdr.len);
}

static void wps_connect_scan_ind_cb(wi_msg_param_t param, void* priv)
{
   WiFiEngine_net_t *net = (WiFiEngine_net_t*)param;
   scan_suite_s *ss = (scan_suite_s*)priv;
   wps_ctx_s* ctx;
   int s;

   if(!ss) return;
   if(!ss->priv) return;
   ctx = (wps_ctx_s*)ss->priv;

   /* should never happen */
   if(ctx->final_net)
      return;
   
   /* rely on that props have been set in wps_disconnected_cb() */
   if(we_filter_out_ssid(net))
      return;

   DE_TRACE_STATIC(TR_WPS,"final net found\n");

   net->ref_cnt++;
   ctx->final_net = net;

   s = WiFiEngine_SetScanJobState(ss->scan_job.id, 0 /* stop */ ,NULL);
   if(s != WIFI_ENGINE_SUCCESS)
   {
      /* eh? */
      DE_TRACE_INT(TR_WARN, "s=%d\n",s);
   }
   WiFiEngine_DeactivateScanSuite(ss);
   cleanup(ctx);
   (*ctx->complete_cb)(WPS_CODE_SUCCESS);
}

#endif //EAP_WSC
