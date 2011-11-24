/** @defgroup we_cm_conn Connection Handler and frontend for optional internal roaming agent.
 *
 * This file provide an interface for queing inf connect and disconnect signals for the main state machine.
 * It also provide an interface and signaling scheme for managing multipple connections using the built-in
 * roaming agent.
 *
 * The main state maching uses WE_IND_80211_... indications to signal events. When using this connection
 * handler (and the romaing agent) the _80211_ signals should be ignored and replaced by their WE_IND_CM_...
 * equivalense.
 * 
 * approx size build with debugging
 * 5k connection manager
 * 16k roaming agent
 * ---
 * 20k total
 *
 **/

#include "driverenv.h"
#include "we_ind.h"
#include "wei_netlist.h"
#include "mac_api_mlme.h"
#include "wei_asscache.h"
#include "wifi_engine_internal.h"

#if (DE_CCX_ROAMING == CFG_INCLUDED)
extern WiFiEngine_net_t * ccxnet; //change for reassociation	
#endif 

void wei_roam_notify_user_disconnect(void);


// simulate that the roaming agent will not do anything for now
/*
static int wei_roam_notify_connected(void) { return FALSE; }
static int wei_roam_notify_connect_failed(void) { return FALSE; }
static int wei_roam_notify_disconnected(void) { return FALSE; }
static int wei_roam_notify_disconnecting(void) { return FALSE; }
*/

typedef struct cm_session {
   WEI_TQ_ENTRY(cm_session) session;
   int pending;            // is this session waiting in line to be activated
   int disconnect_req;      // has a disconnect been requested from the host

#if 0
   cm_session_state state;
   struct cm_conf *conf;
#endif
   WiFiEngine_net_t* net;
} cm_session_s;

typedef struct cm_ctx {
   WEI_TQ_HEAD(cm_session_head, cm_session) head;
   driver_lock_t lock;

   iobject_t *connect_failed;
   iobject_t *connected;
   iobject_t *disconnecting;
   iobject_t *disconnected;
   iobject_t *roam_bail;

#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
   iobject_t *ibss_connected;
   iobject_t *ibss_disconnected;
#endif

} cm_ctx_s;

cm_ctx_s *g_cm_ctx = NULL;

#define GET_CTX (g_cm_ctx)

static void notify_cm_connected(wi_msg_param_t param, void* priv);
static void notify_cm_connect_failed(wi_msg_param_t param, void* priv);
static void notify_cm_disconnecting(wi_msg_param_t param, void* priv);
static void notify_cm_disconnected(wi_msg_param_t param, void* priv);
static void notify_cm_roam_bail(wi_msg_param_t param, void* priv);
static void do_connect(cm_session_s *s);
static void cm_destroy(cm_session_s *s);

static int is_upper_auth(cm_ctx_s *ctx)
{
#if 0
   m80211_mlme_associate_req_t *req;

   req =   wei_asscache_get_req();
   DE_ASSERT(req);

   if(req->ie.wpa_parameter_set.hdr.hdr.len != 0)
      return TRUE;

   if(req->ie.rsn_parameter_set.hdr.len != 0)
      return TRUE;

   return FALSE;
#else
   if(ctx->connected->type == WE_IND_80211_CONNECTED)
      return FALSE;

   return TRUE;
#endif
}

static int was_upper_completated(cm_ctx_s *ctx)
{
   if(ctx->connected->type == WE_IND_80211_CONNECTED)
      return FALSE;

   return TRUE;
}

static void list_all(cm_ctx_s *ctx)
{
#if 0
   int i=0;
   cm_session_s *elm=NULL;

   WEI_TQ_FOREACH(elm, &ctx->head, session) {
      DE_TRACE_INT2(TR_CM, "session [%d] %p\n", i, (uintptr_t)elm); 
      i++;
   }
   if(i==0) {
      DE_TRACE_STATIC(TR_CM, "no sessions in queue\n");
   }
#endif
}


/* only do this once before using anything in this file */
void wei_cm_initialize(void **priv) {
   
   cm_ctx_s *ctx = (cm_ctx_s *)DriverEnvironment_Malloc(sizeof(cm_ctx_s));

#if 1
   // how do we access priv from we_cm_connect and we_cm_disconnect ?
   g_cm_ctx = ctx;
#endif

   DE_ASSERT(priv);

   DE_MEMSET(ctx,0,sizeof(cm_ctx_s));
   WEI_TQ_INIT(&ctx->head);
   DriverEnvironment_initialize_lock(&ctx->lock);

   *priv = ctx;

   we_ind_cond_register(&ctx->connect_failed,
         WE_IND_80211_CONNECT_FAILED, "WE_IND_80211_CONNECT_FAILED", 
         notify_cm_connect_failed, NULL,0,ctx);

   we_ind_cond_register(&ctx->connected,
         WE_IND_80211_CONNECTED, "WE_IND_80211_CONNECTED", 
         notify_cm_connected, NULL,0,ctx);

   we_ind_cond_register(&ctx->disconnecting,
         WE_IND_80211_DISCONNECTING, "WE_IND_80211_DISCONNECTING", 
         notify_cm_disconnecting, NULL,0,ctx);

   we_ind_cond_register(&ctx->disconnected,
         WE_IND_80211_DISCONNECTED, "WE_IND_80211_DISCONNECTED", 
         notify_cm_disconnected, NULL,0,ctx);

   we_ind_cond_register(&ctx->roam_bail,
         WE_IND_ROAM_BAIL, "WE_IND_ROAM_BAIL",
         notify_cm_roam_bail, NULL,0,ctx);

#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)

   we_ind_cond_register(&ctx->ibss_connected,
         WE_IND_80211_IBSS_CONNECTED, "WE_IND_80211_IBSS_CONNECTED",
         notify_cm_connected, NULL,0,ctx);

   we_ind_cond_register(&ctx->disconnected,
         WE_IND_80211_IBSS_DISCONNECTED, "WE_IND_80211_IBSS_DISCONNECTED", 
         notify_cm_disconnected, NULL,0,ctx);

#endif
   

   DE_TRACE_STATIC(TR_CM, "CM init complete\n"); 
}

void wei_cm_wpa_connect_wait(int enable)
{
   cm_ctx_s *ctx = GET_CTX;

   we_ind_deregister_null(&ctx->connected);
   ctx->connected = NULL;

   if(enable)
   {
      we_ind_cond_register(&ctx->connected,
            WE_IND_WPA_CONNECTED, "WE_IND_WPA_CONNECTED", 
            notify_cm_connected, NULL,0,ctx);
   } else {
      we_ind_cond_register(&ctx->connected,
            WE_IND_80211_CONNECTED, "WE_IND_80211_CONNECTED", 
            notify_cm_connected, NULL,0,ctx);
   }
}

void wei_cm_plug(void *priv)
{
   /* may not be necessary is hmg_traffic signals disconnect on init after coredump */
   #if 0
   cm_ctx_s *ctx = GET_CTX;
   cm_session_s *s = NULL;

   DriverEnvironment_acquire_lock(&ctx->lock);
   s = WEI_TQ_FIRST(&ctx->head);
   DriverEnvironment_release_lock(&ctx->lock);

   if(!s) return;

   DE_TRACE_STATIC(TR_CM, "CM pluged: will signal disconnected to terminate/reconnect prev session\n"); 
   
   /* this will trigger reconnect after coredump */
   notify_cm_disconnected(NULL, ctx);
   #endif
}

void wei_cm_unplug(void *priv)
{
   
   cm_ctx_s *ctx = (cm_ctx_s*)priv;
   cm_session_s *s = NULL;

   DE_TRACE_STATIC(TR_CM, "CM unpluged\n"); 
   if(!ctx) return;

   /* Remove all sessions, if there is an active connection
      indicate disconnect */
   
   DriverEnvironment_acquire_lock(&ctx->lock);
   while(!WEI_TQ_EMPTY(&ctx->head)) 
   {
      s = WEI_TQ_FIRST(&ctx->head);
      WEI_TQ_REMOVE(&ctx->head, s, session); 
      DriverEnvironment_release_lock(&ctx->lock);
      if(s->pending == 0) 
      {
         DriverEnvironment_indicate(WE_IND_CM_DISCONNECTED, s, sizeof(s));
      } 
      /* still first? */
      cm_destroy(s);
      DriverEnvironment_acquire_lock(&ctx->lock);
   }
   DriverEnvironment_release_lock(&ctx->lock);

}

void wei_cm_shutdown(void *priv)
{
   cm_ctx_s *ctx = (cm_ctx_s*)priv;
   cm_session_s *s = NULL;

   if(!ctx) return;

   we_ind_deregister_null(&ctx->connect_failed);
   we_ind_deregister_null(&ctx->connected);
   we_ind_deregister_null(&ctx->disconnecting);
   we_ind_deregister_null(&ctx->disconnected);
   we_ind_deregister_null(&ctx->roam_bail);

#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
   we_ind_deregister_null(&ctx->ibss_connected);
   we_ind_deregister_null(&ctx->ibss_disconnected);
#endif

   DriverEnvironment_acquire_lock(&ctx->lock);
   while(!WEI_TQ_EMPTY(&ctx->head)) {
      s = WEI_TQ_FIRST(&ctx->head);
      WEI_TQ_REMOVE(&ctx->head, s, session); 
      DriverEnvironment_release_lock(&ctx->lock);
      DriverEnvironment_indicate(WE_IND_CM_DISCONNECTED, s, sizeof(s));
      cm_destroy(s);
      DriverEnvironment_acquire_lock(&ctx->lock);
   }
   DriverEnvironment_release_lock(&ctx->lock);

   DriverEnvironment_Free(ctx);

   DE_TRACE_STATIC(TR_CM, "CM shutdown complete\n"); 
}

/* private initializer */
static cm_session_s*
cm_session_init(WiFiEngine_net_t* net)
{
   cm_session_s *s=NULL;

   s = (cm_session_s*)DriverEnvironment_Malloc(sizeof(cm_session_s));
   if(!s) return NULL;

   DE_MEMSET(s,0,sizeof(cm_session_s));

   s->pending = 1;
   net->ref_cnt++;
   s->net = net;

   return s;
}

static void cm_destroy(cm_session_s *s)
{
   DE_TRACE_PTR(TR_CM, "terminating session %p\n", s); 

   s->net->ref_cnt--;
   wei_netlist_free_net_safe(s->net);

   DriverEnvironment_Free(s);
}

/* must be locked before access */
static cm_session_s*
__test_and_set_connect_pending(cm_ctx_s *ctx) {
   cm_session_s *elm=NULL;

   elm = WEI_TQ_FIRST(&ctx->head);
   if(elm && elm->pending) {
      elm->pending = 0;
      return elm;
   }

   return NULL;
}


/*
 * TODO: Wrapper function for this is needed.
 *
 * in IBSS case use:
 *    WiFiEngine_SetSSID();
 *    WiFiEngine_SetIBSSMode();
 *      we_cm_connect( wei_find_create_ibss_net() );
 */
void*
we_cm_connect(WiFiEngine_net_t* net)
{
   cm_session_s *s=NULL, *first=NULL;
   cm_ctx_s *ctx = GET_CTX;

   if(!net) return NULL;

   s = cm_session_init(net);
   if(!s) return NULL;

   DE_TRACE_PTR(TR_CM, "New session %p\n", s); 

   DriverEnvironment_acquire_lock(&ctx->lock);
   WEI_TQ_INSERT_TAIL(&ctx->head, s, session);
   first = __test_and_set_connect_pending(ctx);
   DriverEnvironment_release_lock(&ctx->lock);

   if(first)
      do_connect(first);

   return s;
}

int
we_cm_disconnect(void* session_p)
{
   cm_ctx_s *ctx;
   cm_session_s *elm=NULL;
   cm_session_s *s = (cm_session_s *)session_p;
   int found = 0;

   if(!s)
      return WIFI_ENGINE_FAILURE;

   DE_TRACE_PTR(TR_CM, "user disconnect on session %p\n", s); 
   
   ctx = GET_CTX;
   
   DriverEnvironment_acquire_lock(&ctx->lock);
   WEI_TQ_FOREACH(elm, &ctx->head, session) {
      if (elm==s) found = 1;
   }
   if(!found) {
      DriverEnvironment_release_lock(&ctx->lock);
      DE_TRACE_PTR(TR_WARN, "session %p not in list\n", s);
      return WIFI_ENGINE_FAILURE;
   }
   if(s->pending) { 
      WEI_TQ_REMOVE(&ctx->head, s, session); 
   }
   s->disconnect_req = 1;
   DriverEnvironment_release_lock(&ctx->lock);
   if(s->pending) {
      DE_TRACE_PTR(TR_CM, "session %p was pending\n", s); 
      DriverEnvironment_indicate(WE_IND_CM_CONNECTING, s, sizeof(s));
      DriverEnvironment_indicate(WE_IND_CM_CONNECT_FAILED, s, sizeof(s));
      DriverEnvironment_indicate(WE_IND_CM_DISCONNECTED, s, sizeof(s));
#if (DE_CCX == CFG_INCLUDED)
      wifiEngineState.ccxState.LastAssociatedNetInfo.Disassociation_Timestamp = DriverEnvironment_GetTimestamp_msec();
#endif
      cm_destroy(s);
      return WIFI_ENGINE_SUCCESS;
   }

   WiFiEngine_Disconnect();

   return WIFI_ENGINE_SUCCESS;
}


static void do_connect(cm_session_s *s)
{
   DE_TRACE_PTR(TR_CM, "Connecting to %p\n", s); 

   DriverEnvironment_indicate(WE_IND_CM_CONNECTING, s, sizeof(s));
#if (DE_CCX_ROAMING == CFG_INCLUDED)
    ccxnet = NULL;    //change for reassociation	
#endif  
   WiFiEngine_Connect(s->net);
}

static void notify_cm_connected(wi_msg_param_t param, void* priv)
{
   cm_session_s *s=NULL;
   cm_ctx_s *ctx = (cm_ctx_s*)priv;

   DriverEnvironment_acquire_lock(&ctx->lock);
   s = WEI_TQ_FIRST(&ctx->head);
   DriverEnvironment_release_lock(&ctx->lock);

   if(!s) return;

   DE_TRACE_PTR(TR_CM, "connected to %p\n", s); 

   if(!wei_roam_notify_connected()) {
      DriverEnvironment_indicate(WE_IND_CM_CONNECTED, s, sizeof(s));
   }
}

static void notify_cm_connect_failed(wi_msg_param_t param, void* priv)
{
   cm_session_s *s=NULL;
   cm_ctx_s *ctx = (cm_ctx_s*)priv;

   DriverEnvironment_acquire_lock(&ctx->lock);
   s = WEI_TQ_FIRST(&ctx->head);
   DriverEnvironment_release_lock(&ctx->lock);

   if(!s) return;

   if(!wei_roam_notify_connect_failed()) {
      DE_TRACE_PTR(TR_CM, "connect failed with %p\n", s); 
      DriverEnvironment_indicate(WE_IND_CM_CONNECT_FAILED, s, sizeof(s));
   }
}

static void notify_cm_disconnecting(wi_msg_param_t param, void* priv)
{
   cm_session_s *s=NULL;
   cm_ctx_s *ctx = (cm_ctx_s*)priv;

   DriverEnvironment_acquire_lock(&ctx->lock);
   s = WEI_TQ_FIRST(&ctx->head);
   DriverEnvironment_release_lock(&ctx->lock);

   if(!s) return;

   if(s->disconnect_req) {
      DE_TRACE_PTR(TR_CM, "%p is disconnecting (user req)\n", s); 
      wei_roam_notify_user_disconnect();
      DriverEnvironment_indicate(WE_IND_CM_DISCONNECTING, s, sizeof(s));
      return;
   }

   if(!is_upper_auth(ctx))
   {
      if(!wei_roam_notify_disconnecting())
      {
         DE_TRACE_PTR(TR_CM, "%p is disconnecting\n", s); 
         DriverEnvironment_indicate(WE_IND_CM_DISCONNECTING, s, sizeof(s));
      }
      return;
   }

   /* upper auth */
   if(was_upper_completated(ctx))
   {
      if(!wei_roam_notify_disconnecting())
      {
         DE_TRACE_PTR(TR_CM, "%p is disconnecting\n", s); 
         DriverEnvironment_indicate(WE_IND_CM_DISCONNECTING, s, sizeof(s));
      }
   } else {
      if(!wei_roam_notify_connect_failed())
      {
         DE_TRACE_PTR(TR_CM, "connect (wpa) failed with %p\n", s); 
         DriverEnvironment_indicate(WE_IND_CM_CONNECT_FAILED, s, sizeof(s));
      }
   }
}

static void notify_cm_roam_bail(wi_msg_param_t param, void* priv)
{
   cm_session_s *s=NULL, *pending=NULL;
   cm_ctx_s *ctx = (cm_ctx_s*)priv;

   /* at this point the host thinks we are still connected. */

   list_all(ctx);

   DriverEnvironment_acquire_lock(&ctx->lock);
   s = WEI_TQ_FIRST(&ctx->head);
   if(s) {
      WEI_TQ_REMOVE(&ctx->head, s, session); 
      pending = __test_and_set_connect_pending(ctx);
   }
   DriverEnvironment_release_lock(&ctx->lock);

   if(!s) return;

   DE_TRACE_PTR(TR_CM, "%p is disconnecting\n", s); 

   DriverEnvironment_indicate(WE_IND_CM_DISCONNECTING, s, sizeof(s));

   DE_TRACE_PTR(TR_CM, "%p is now disconneced\n", s); 

   DriverEnvironment_indicate(WE_IND_CM_DISCONNECTED, s, sizeof(s));

#if (DE_CCX == CFG_INCLUDED)
   wifiEngineState.ccxState.LastAssociatedNetInfo.Disassociation_Timestamp = DriverEnvironment_GetTimestamp_msec();
#endif

   cm_destroy(s);
   wei_netlist_remove_current_net();

   list_all(ctx);

   if(pending)
      do_connect(pending);
}


static void notify_cm_disconnected(wi_msg_param_t param, void* priv)
{
   cm_session_s *s=NULL, *pending=NULL;
   cm_ctx_s *ctx = (cm_ctx_s*)priv;

   UNUSED(param);

   DriverEnvironment_acquire_lock(&ctx->lock);
   s = WEI_TQ_FIRST(&ctx->head);
   DriverEnvironment_release_lock(&ctx->lock);
   list_all(ctx);

   if(!s) return;

   DE_TRACE_PTR(TR_CM, "session %p 80211 disconnected\n", s); 

   if(s->disconnect_req) {
      wei_roam_notify_user_disconnect();
   } else {
      /* if roaming is active we will do nothing here but wait for WE_IND_ROAM_BAIL */
      if(wei_roam_notify_disconnected()) return;
   }

   DE_TRACE_PTR(TR_CM, "%p is now disconneced\n", s); 
   list_all(ctx);

   DriverEnvironment_acquire_lock(&ctx->lock);
   WEI_TQ_REMOVE(&ctx->head, s, session); 
   pending = __test_and_set_connect_pending(ctx);
   DriverEnvironment_release_lock(&ctx->lock);

   wei_netlist_remove_current_net();
   DriverEnvironment_indicate(WE_IND_CM_DISCONNECTED, NULL, 0);
#if (DE_CCX == CFG_INCLUDED)
   wifiEngineState.ccxState.LastAssociatedNetInfo.Disassociation_Timestamp = DriverEnvironment_GetTimestamp_msec();
#endif
   cm_destroy(s);

   list_all(ctx);

   if(pending)
      do_connect(pending);
}

