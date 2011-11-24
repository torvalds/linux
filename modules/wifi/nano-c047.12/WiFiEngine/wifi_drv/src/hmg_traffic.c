
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
*****************************************************************************/
/** @defgroup hmg WiFiEngine internal main state machine.
 *
 * \brief This module implements the main state machine for WiFiEngine.
 *
 * The state machine governs association and power save states.
 * It is fed signals from the ucos message handler and reacts to
 * the signals by changing states.
 *
 * The state machine consists of main states and sub states.  The main
 * states define the overall connection status of the driver: waiting
 * (PowerUp), connected (PowerActive), powered off (PowerOff). Sub
 * states control the various sequences necessary for association
 * (Infrastructure and IBSS) and the power save modes. The overall
 * state machine injects signals into the main and sub states. State
 * transitions may occur as a result of the signal. A signal
 * will continue to be sent until the overall state machine has
 * reached "steady state", that is the signal causes no new state
 * transitions (of either main or sub states).
 *
 * An external signal queue (wei_sm_queue_sig()) exists for signals
 * that need to be postponed until an external event happens (such as
 * a message response from the hardware). Since ucos will execute
 * until the signal queue is empty these signals need to be inserted
 * in the ucos message queue (wei_sm_execute()) at a later time and
 * the external queue holds them until then.
 *
 * A lock (wifiEngineState.sm_lock) protects the state machine from
 * concurrent execution. Entry to the state machine should always be
 * from wei_sm_execute() to ensure that the lock is handled properly.
 *
 * @{
 */

#include "driverenv.h"
#include "ucos.h"
#include "registry.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "mlme_proxy.h"
#include "hmg_ps.h"
#include "hmg_defs.h"
#include "hmg_traffic.h"
#include "wei_netlist.h"
#include "macWrapper.h"
#include "we_ind.h"

/*****************************************************************************
T E M P O R A R Y   T E S T V A R I A B L E S
*****************************************************************************/

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/
#define OBJID_MYSELF    SYSDEF_OBJID_HOST_MANAGER_TRAFFIC

#define EXECUTE_STATE(_StateFunction)   (StateFn_t)(_StateFunction)(msg, sm_param)
#define INIT_STATE(_StateFunction)      (_StateFunction)(INTSIG_INIT, DUMMY)
#define EXIT_STATE(_StateFunction)      (_StateFunction)(INTSIG_EXIT, DUMMY)

/*****************************************************************************
L O C A L   D A T A T Y P E S
*****************************************************************************/


static struct ps_ctx_t {
   iobject_t* coredump_started_h;
} ps_ctx;

/*****************************************************************************
L O C A L   F U N C T I O N   P R O T O T Y P E S
*****************************************************************************/
static void HMG_entry_traffic(ucos_msg_id_t msg, ucos_msg_param_t param);


/*********************/
/* State functions   */
/*********************/
/* Main states */
DECLARE_STATE_FUNCTION(StateFunction_PowerOff);
DECLARE_STATE_FUNCTION(StateFunction_Disconnected);
DECLARE_STATE_FUNCTION(StateFunction_Connecting);
DECLARE_STATE_FUNCTION(StateFunction_Connected);
DECLARE_STATE_FUNCTION(StateFunction_Disconnecting);
/* Sub states */
DECLARE_STATE_FUNCTION(StateFunction_Idle);


static StateFn_ref handle_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn);
static StateFn_ref handle_auto_connecting_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn);
static StateFn_ref handle_transparent_connecting_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn);
static StateFn_ref handle_connected_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn);
static StateFn_ref handle_disconnecting_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn);
#ifdef WITH_8021X_PORT
static void check_if_close_port(void);
#endif
static void coredump_started_cb(wi_msg_param_t param, void *priv);
static int soft_shutdown = FALSE;

/*maps a int to a name (used for debuging)*/
#ifdef WIFI_DEBUG_ON
static char* HMG_Signal_toToName(int);
#endif

/*****************************************************************************
 M O D U L E   V A R I A B L E S
*****************************************************************************/
static StateVariables_t stateVars = { StateFunction_Idle, StateFunction_Idle,
                                      StateFunction_Idle, StateFunction_Idle };
static m80211_mlme_deauthenticate_cfm_t deauthenticate_cfm;
  

/*********************/
/* Error class table */
/*********************/

/*****************************************************************************
G L O B A L   C O N S T A N T S / V A R I A B L E S
*****************************************************************************/
#if (DE_CCX_ROAMING == CFG_INCLUDED)
extern WiFiEngine_net_t * ccxnet;
#endif

/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/



void HMG_indication_handler(driver_ind_param_t *ind_param)
{
   /* wei_sm_queue_sig((ucos_msg_id_t)ind_param->msgId, OBJID_MYSELF, (mac_msg_t *)ind_param, TRUE); */
}

void HMG_init_traffic(void)
{
   /* Set the default state. */
   stateVars.newState_fn = StateFunction_PowerOff;
   stateVars.currentState_fn = StateFunction_PowerOff;
   stateVars.subState_fn = StateFunction_Idle;
   stateVars.newSubState_fn = StateFunction_Idle;

   ucos_register_object(OBJID_MYSELF,
                        SYSCFG_UCOS_OBJECT_PRIO_LOW,
                        (ucos_object_entry_t)HMG_entry_traffic,
                        "HMG_TRAFFIC");  
}

void HMG_startUp_traffic(void)
{
   /* Since there is no previous state, send the init signal explicitly. */
   wei_sm_queue_sig(INTSIG_INIT, OBJID_MYSELF, DUMMY, TRUE);
}

static void HMG_entry_traffic(ucos_msg_id_t msg, ucos_msg_param_t param)
{
   wei_sm_queue_param_s *sm_param = (wei_sm_queue_param_s*)param;

   /* Dispatch signal to the current state function. */
   do
   {
      if (stateVars.newState_fn != stateVars.currentState_fn)
      {
         /* Make a state transition.
            Note that the init and exit signals will be propagated to any substate.
         */
         EXIT_STATE(stateVars.currentState_fn);
         INIT_STATE(stateVars.newState_fn);
         stateVars.currentState_fn = stateVars.newState_fn;
      }
      else
      {
         stateVars.newState_fn = EXECUTE_STATE(stateVars.currentState_fn);
      }

      do
      {
         if (stateVars.newSubState_fn != stateVars.subState_fn)
         {
            /* Make a state transition.
               Note that the init and exit signals will be propagated to any substate.
            */
            EXIT_STATE(stateVars.subState_fn);
            INIT_STATE(stateVars.newSubState_fn);
            stateVars.subState_fn = stateVars.newSubState_fn;
         }
         else
         {
            stateVars.newSubState_fn = EXECUTE_STATE(stateVars.subState_fn);
         }
      } while (stateVars.newSubState_fn != stateVars.subState_fn);

   } while (stateVars.newState_fn != stateVars.currentState_fn);
   if(sm_param)
   {
      if(sm_param->type == WRAPPER_STRUCTURE) {
         WrapperFreeStructure(sm_param->p);
      }
      DriverEnvironment_Nonpaged_Free(sm_param);
   }
}

void HMG_Unplug_traffic(void)
{
   stateVars.currentState_fn = StateFunction_PowerOff;
   stateVars.newState_fn = StateFunction_PowerOff;
   stateVars.subState_fn = StateFunction_Idle;
   stateVars.newSubState_fn = StateFunction_Idle;
   we_ind_deregister_null(&ps_ctx.coredump_started_h);
   wifiEngineState.main_state = driverDisconnected;

}

void wei_hmg_traffic_init(void)
{
   HMG_init_traffic();
}

void HMG_resume_traffic(void)
{
   /* Set the default state. */
   stateVars.currentState_fn = StateFunction_Idle;
   stateVars.newState_fn = StateFunction_Disconnected;
   stateVars.subState_fn = StateFunction_Idle;
   stateVars.newSubState_fn = StateFunction_Idle;
}

/* This is only called on IOCTL_GET_STATUS, so it can be slow-dumb */
int HMG_GetState_traffic()
{
   if (stateVars.currentState_fn == StateFunction_Idle)
   {
      return Idle;
   }
   else if (stateVars.currentState_fn == StateFunction_Disconnected)
   {
      return Unconnected;
   }
   else if (stateVars.currentState_fn == StateFunction_Connected)
   {
      return Connected;
   }
   else if (stateVars.currentState_fn == StateFunction_PowerOff)
   {
      return PowerOff;
   }

   return Invalid;
}

/* This is only called on IOCTL_GET_STATUS, so it can be slow-dumb */
int HMG_GetSubState_traffic()
{
   if (stateVars.subState_fn == StateFunction_Idle)
   {
      return Idle;
   }

   return Invalid;
}

/*****************************************************************************
L O C A L   F U N C T I O N S
*****************************************************************************/


static void NotifyConnectFailed(void)
{
   // TODO: FIXME listen to coredump start/complete and figure out what to do!
   //wei_dump_notify_connect_failed();
   DriverEnvironment_indicate(WE_IND_80211_CONNECT_FAILED, NULL, 0);
}

static void coredump_started_cb(wi_msg_param_t param, void *priv)
{
   DE_TRACE_STATIC(TR_SM, "coredump_started_cb\n");
   switch(wifiEngineState.main_state)
   {
      case driverJoining:
      case driverStarting:
      case driverAuthenticating:
      case driverAssociating: 
      NotifyConnectFailed();   
      DriverEnvironment_indicate(WE_IND_80211_DISCONNECTED, NULL, 0);
      break;

      case driverAssociated:   
      case driverDisassociating:
      case driverDeauthenticating:
      case driverDisconnecting:         
      DriverEnvironment_indicate(WE_IND_80211_DISCONNECTED, NULL, 0);
      break;   

      default:
      break;

   }

}




/*** Main States ***/
/*****************************************************************************
** StateFunction_Disconnected
**
** In this state the state machine is disconnected.
** If INTSIG_NET_CONNECT is recevied a state change to
** statfunction StateFunction_Disconnecting is performed.
**
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Disconnected)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Disconnected;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Enter Main state Disconnected\n");
         wifiEngineState.main_state = driverDisconnected;
         DriverEnvironment_indicate(WE_IND_80211_DISCONNECTED, NULL, 0);
         if(soft_shutdown)
         {
            soft_shutdown = FALSE;
            wei_sm_queue_sig(INTSIG_PS_SLEEP_FOREVER, OBJID_MYSELF, NULL, TRUE);
         }
         
         break;

      case INTSIG_CASE(INTSIG_EXIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit Main state Disconnected\n");
         /* reset last con_failed event */
         wei_save_con_failed_reason(WE_CONNECT_NOT_PRESENT, M80211_MGMT_RC_NOT_VALID);
         break;

      case INTSIG_CASE(INTSIG_UNPLUG_TRAFFIC):
         /* The hardware has gone */
         nextState_fn = (StateFn_ref)StateFunction_PowerOff;
         break;

      case INTSIG_CASE(INTSIG_NET_CONNECT):
         wei_sm_queue_sig(INTSIG_NET_CONNECT, OBJID_MYSELF, NULL, TRUE);
         nextState_fn = (StateFn_ref)StateFunction_Connecting;
         break;

      case INTSIG_CASE(INTSIG_NET_JOIN):
         wei_sm_queue_sig(INTSIG_NET_JOIN, OBJID_MYSELF, NULL, TRUE);
         nextState_fn = (StateFn_ref)StateFunction_Connecting;
         break;  

      case INTSIG_CASE(INTSIG_NET_AUTHENTICATE):
         wei_sm_queue_sig(INTSIG_NET_AUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
         nextState_fn = (StateFn_ref)StateFunction_Connecting;
         break;         

      case INTSIG_CASE(INTSIG_NET_ASSOCIATE):
         wei_sm_queue_sig(INTSIG_NET_ASSOCIATE, OBJID_MYSELF, NULL, TRUE);
         nextState_fn = (StateFn_ref)StateFunction_Connecting;
         break;
         
      case INTSIG_CASE(INTSIG_NET_DISASSOCIATE):
         DE_TRACE_STATIC(TR_SM, "Already disassociated\n");
         break;

      case INTSIG_CASE(INTSIG_NET_DEAUTHENTICATE):
         DE_TRACE_STATIC(TR_SM, "Already deauthenticated\n");
         break;         
         
      case INTSIG_CASE(INTSIG_NET_START):
         wei_sm_queue_sig(INTSIG_NET_START, OBJID_MYSELF, NULL, TRUE);
         nextState_fn = (StateFn_ref)StateFunction_Connecting;
         break;         

      case INTSIG_CASE(INTSIG_POWER_UP):
         /* ignore */
         break;

      /* CTRL */
      case INTSIG_CASE(INTSIG_PS_SLEEP_FOREVER):   
         nextState_fn = (StateFn_ref)StateFunction_PowerOff;
         wei_sm_queue_sig(INTSIG_PS_SLEEP_FOREVER, OBJID_MYSELF, NULL, TRUE);
         break;

      /* MGMT */
      case MGMT_RX_CASE(MLME_JOIN_CFM):
         DE_SM_ASSERT(FALSE); break;
      case MGMT_RX_CASE(MLME_AUTHENTICATE_CFM):
         DE_SM_ASSERT(FALSE); break;
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_CFM):
         DE_SM_ASSERT(FALSE); break;
      case MGMT_RX_CASE(MLME_ASSOCIATE_CFM):
         DE_SM_ASSERT(FALSE); break;
      case MGMT_RX_CASE(MLME_REASSOCIATE_CFM):
         DE_SM_ASSERT(FALSE); break;
      case MGMT_RX_CASE(MLME_DISASSOCIATE_CFM):
         DE_SM_ASSERT(FALSE); break;
#if (DE_CCX == CFG_INCLUDED)
      case MGMT_RX_CASE(NRP_MLME_ADDTS_CFM):
	     DE_SM_ASSERT(FALSE); break;
#endif //DE_CCX

      default:
         DE_TRACE_STATIC2(TR_SM, "Unhandled message  %s\n", HMG_Signal_toToName(msg));
         DE_SM_ASSERT(FALSE);
   }

   return  nextState_fn;
}



/*****************************************************************************
** StateFunction_Connecting
**
** In this state the state machine tries to connect to an access point.
** If the connection succeeds a state change
** to function StateFunction_Connected is performed.
** If it fails a state change to StateFunction_Disconnected is performed.
** A state change to function StateFunction_Disconnecting is only
** performed from StateFunction_Connected.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Connecting)
{
   WiFiEngine_net_t *net = NULL;
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Connecting;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   net = wei_netlist_get_current_net();
   /* will always be pressent unless when unpluged */
   DE_SM_ASSERT(net);

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
         DE_TRACE_STATIC(TR_SM, "Enter Main state Connecting\n");
         DriverEnvironment_indicate(WE_IND_80211_CONNECTING, NULL, 0);
         break;

      case INTSIG_CASE(INTSIG_EXIT):
         /* ignore; I think! */
         /*
         DE_TRACE_STATIC(TR_SM, "Exit Main state Unconnected\n");
         if((!WiFiEngine_is_roaming_enabled()) && (wifiEngineState.main_state != driverAssociated))
         {
            DriverEnvironment_indicate(WE_IND_80211_DISCONNECTED, NULL, 0);
         }
         */
         break;

      case INTSIG_CASE(INTSIG_PS_SLEEP_FOREVER):   
         soft_shutdown = TRUE;
#if 0         
         nextState_fn = (StateFn_ref)StateFunction_PowerOff;
         wei_sm_queue_sig(INTSIG_PS_SLEEP_FOREVER, OBJID_MYSELF, NULL, TRUE);
#endif
         break;         

      case INTSIG_CASE(INTSIG_UNPLUG_TRAFFIC):
         DriverEnvironment_indicate(WE_IND_80211_CONNECT_FAILED, NULL, 0);
         DriverEnvironment_indicate(WE_IND_80211_DISCONNECTED, NULL, 0);
         nextState_fn = (StateFn_ref)StateFunction_PowerOff;
         break;

      case INTSIG_CASE(INTSIG_NET_CONNECT):
         DE_ASSERT(net);
         if (WES_TEST_FLAG(WES_FLAG_ASSOC_BLOCKED))
         {
            DE_TRACE_STATIC(TR_SM, "JOIN refused, association is blocked by WiFiEngine\n");
            nextState_fn = (StateFn_ref)StateFunction_Disconnected;
            NotifyConnectFailed();
            break;
         }

         /* Infrastructure */
         if (!Mlme_Send(Mlme_CreateJoinRequest, 0, wei_send_cmd))
         {
            nextState_fn = (StateFn_ref)StateFunction_Disconnected;
            NotifyConnectFailed();
         }
         else
         {
            wifiEngineState.main_state = driverJoining;
         }
         break;

      case INTSIG_CASE(INTSIG_NET_JOIN):
         DE_ASSERT(net);
         
         /* Infrastructure */
         if (!Mlme_Send(Mlme_CreateJoinRequest, 0, wei_send_cmd))
         {
            m80211_mlme_join_cfm_t cfm;

            cfm.result = M80211_MLME_RESULT_REFUSED;
            
            nextState_fn = (StateFn_ref)StateFunction_Disconnected;
            we_ind_send(WE_IND_JOIN_CFM,(void *)&cfm);
         }
         else
         {
            wifiEngineState.main_state = driverJoining;
         }
         break;

      case INTSIG_CASE(INTSIG_NET_AUTHENTICATE):
         DE_ASSERT(net);
         
         /* update net with selected authentication mode, having this
          * set to auto is error in non-hmgAutoMode */
         net->auth_mode = wifiEngineState.config.authenticationMode;

         /* Try to authenticate with the joined network. */
         if (!Mlme_Send(Mlme_CreateAuthenticateRequest, 0, wei_send_cmd))
         {
            m80211_mlme_authenticate_cfm_t cfm;

            cfm.peer_sta = net->bssId_AP;
            cfm.result = M80211_MLME_RESULT_REFUSED;
            
            we_ind_send(WE_IND_AUTHENTICATE_CFM,(void *)&cfm);
            wei_sm_queue_sig(INTSIG_NET_LEAVE_BSS, OBJID_MYSELF, NULL, TRUE); 
            SET_NEW_STATE(StateFunction_Disconnecting);
         }
         else
         {
            /* success */
            wifiEngineState.main_state = driverAuthenticating;
         }
         
         break;   

      case INTSIG_CASE(INTSIG_NET_ASSOCIATE):
         DE_ASSERT(net);
         
         /* Try to associate with the network. */

         if (!Mlme_Send(Mlme_CreateAssociateRequest, 0, wei_send_cmd))
         {
            m80211_mlme_associate_cfm_t cfm;

            cfm.result = M80211_MLME_RESULT_REFUSED;

            we_ind_send(WE_IND_ASSOCIATE_CFM,(void *)&cfm);
            wei_sm_queue_sig(INTSIG_NET_LEAVE_BSS, OBJID_MYSELF, NULL, TRUE);
            
            SET_NEW_STATE(StateFunction_Disconnecting);

         }
         else
         {
            wifiEngineState.main_state = driverAssociating;
         }
   
         break;         
      case INTSIG_NET_START:
         /* Start an ibss network */
         if (!Mlme_Send(Mlme_CreateStartRequest, 0, wei_send_cmd))
         {
            nextState_fn = (StateFn_ref)StateFunction_Disconnected;                        
            NotifyConnectFailed();
         }
         break;

      case INTSIG_CASE(INTSIG_NET_REASSOCIATE):
         /* Try to reassociate with the network.*/
         if (!Mlme_Send(Mlme_CreateReassociateRequest, 0, wei_send_cmd))
         {
            /* Send leave request to remove the instance created in fw */
            Mlme_Send(Mlme_CreateLeaveBSSRequest, 0, wei_send_cmd);
            wei_save_con_failed_reason(WE_CONNECT_ASSOC_FAIL, WE_CONNECT_WIFI_ENGINE_FAIL);
            NotifyConnectFailed();
         }
         break;

      case INTSIG_CASE(INTSIG_NET_LEAVE_IBSS):
         nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
         wei_sm_queue_sig(INTSIG_NET_LEAVE_IBSS, OBJID_MYSELF, NULL, TRUE);
         break;

      case INTSIG_CASE(INTSIG_NET_DEAUTHENTICATE):
         nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
         wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
         wei_save_con_failed_reason(WE_CONNECT_AUTH_FAIL, M80211_MGMT_RC_NOT_VALID);
         NotifyConnectFailed();
         break;

      case INTSIG_CASE(INTSIG_NET_DISASSOCIATE):
         nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
         wei_sm_queue_sig(INTSIG_NET_DISASSOCIATE, OBJID_MYSELF, NULL, TRUE);
         wei_save_con_failed_reason(WE_CONNECT_ASSOC_FAIL, M80211_MGMT_RC_NOT_VALID);
         NotifyConnectFailed();
         break;


      case MGMT_RX_CASE(NRP_MLME_BSS_LEAVE_CFM):
         DE_SM_ASSERT(FALSE);
         break;


      case MGMT_RX_CASE(HIC_MESSAGE_TYPE_CONSOLE):
      case MGMT_RX_CASE(HIC_MESSAGE_TYPE_FLASH_PRG):
         break;

      case MGMT_RX_CASE(NRP_MLME_PEER_STATUS_IND):
         DE_ASSERT(net);
         if(net->bss_p->bssType == M80211_CAPABILITY_ESS)
         {
            m80211_nrp_mlme_peer_status_ind_t *ind;
            ind = (m80211_nrp_mlme_peer_status_ind_t *)param->p;
            Mlme_HandleBssPeerStatusInd(ind);
            switch (ind->status)
            {
               case MLME_PEER_STATUS_CONNECTED:
                  DE_ASSERT(FALSE);

               case MLME_PEER_STATUS_RESTARTED:
                  /* Handled by firmware */  
                  break;               /* infrastructure */                        
               case MLME_PEER_STATUS_INCOMPATIBLE:      
               case MLME_PEER_STATUS_TX_FAILED:
               case MLME_PEER_STATUS_RX_BEACON_FAILED:
               case MLME_PEER_STATUS_NOT_CONNECTED:
               case MLME_PEER_STATUS_ROUNDTRIP_FAILED: 
                  {
                     nextState_fn = (StateFn_ref)StateFunction_Disconnecting;

                     if(wei_is_hmg_auto_mode())
                     {
                        wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
                        wei_save_con_failed_reason(WE_CONNECT_PEER_STATUS, ind->status);
                        NotifyConnectFailed();
                     }
                     else
                     {  
                        
                        we_ind_send(WE_IND_LINK_LOSS_IND,(void *)ind);
                     
                        if(wifiEngineState.main_state == driverAuthenticating)
                        {
                           /* 
                               Send leave and change state to disconnecting
                           */
                           wei_sm_queue_sig(INTSIG_NET_LEAVE_BSS, OBJID_MYSELF, NULL, TRUE);
                        }
                     }
                  }
                  break;
               case MLME_PEER_STATUS_TX_FAILED_WARNING:
               case MLME_PEER_STATUS_RX_BEACON_FAILED_WARNING:
                  /* safe to ignore */
               break;
            }
         }
         else
         {
            m80211_nrp_mlme_peer_status_ind_t *sta_ind_p;
            sta_ind_p = (m80211_nrp_mlme_peer_status_ind_t *)param->p;
            Mlme_HandleBssPeerStatusInd(sta_ind_p);
            if(wei_is_ibss_connected(sta_ind_p)) 
            {
               nextState_fn = (StateFn_ref)StateFunction_Connected;
            }    
         }
         break;         
      case MGMT_RX_CASE(MLME_JOIN_CFM):
      case MGMT_RX_CASE(MLME_AUTHENTICATE_CFM):
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_CFM):
      case MGMT_RX_CASE(MLME_ASSOCIATE_CFM):
      case MGMT_RX_CASE(MLME_REASSOCIATE_CFM):
      case MGMT_RX_CASE(MLME_START_CFM):
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_IND):
         nextState_fn = (StateFn_ref)handle_events(msg, param, nextState_fn);
         break;

      default:
         DE_TRACE_STATIC2(TR_SM, "Unhandled message  %s\n", HMG_Signal_toToName(msg));
         DE_SM_ASSERT(FALSE);
  }

  return  nextState_fn;
}


/*****************************************************************************
** StateFunction_Connected
**
** In this state the state machine is connected.
** If a disconnect event is received a state change
** to function StateFunction_Disonnecting is performed.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Connected)
{

   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Connected;
   WiFiEngine_net_t *net;

   net = wei_netlist_get_current_net();
   /* will always be pressent unless when unpluged */
   DE_SM_ASSERT(net);

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Enter state Connected\n");
         wifiEngineState.main_state = driverConnected;
         DriverEnvironment_indicate(WE_IND_80211_CONNECTED, NULL , 0);
         DriverEnvironment_indicate(WE_IND_AUTH_STATUS_CHANGE, NULL, 0);
         wei_dump_notify_connect_success();
         /* reset last con lost reason */
         wei_save_con_lost_reason(WE_DISCONNECT_NOT_PRESENT, M80211_MGMT_RC_NOT_VALID);
         if(soft_shutdown)
         {
            wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
         }
         break;

      case INTSIG_CASE(INTSIG_EXIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit state Connected\n");
         WES_CLEAR_FLAG(WES_FLAG_WMM_ASSOC);
         WES_CLEAR_FLAG(WES_FLAG_QOS_ASSOC);
         DriverEnvironment_indicate(WE_IND_80211_DISCONNECTING, NULL, 0);
         DriverEnvironment_SetPriorityThreadLow();
         break;

      case INTSIG_CASE(INTSIG_NET_RESELECT):
         DE_SM_ASSERT(FALSE);
         break;

      case INTSIG_CASE(INTSIG_NET_CONNECT):
         DE_TRACE_STATIC(TR_SM, "net_selection should only be received in state disconnected\n");
         DE_ASSERT(FALSE);
         break;

      case INTSIG_CASE(INTSIG_NET_START):
         DE_TRACE_STATIC(TR_SM, "net_start should only be received in state disconnected\n");
         DE_ASSERT(FALSE);
         break;
         
      case INTSIG_CASE(INTSIG_NET_DEAUTHENTICATE):
         wei_save_con_lost_reason(WE_DISCONNECT_DEAUTH, M80211_MGMT_RC_NOT_VALID);
         nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
         wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
         break;

      case INTSIG_CASE(INTSIG_PS_SLEEP_FOREVER): 
         soft_shutdown = TRUE;
         wei_save_con_lost_reason(WE_DISCONNECT_DEAUTH, M80211_MGMT_RC_NOT_VALID);
         nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
         wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
#if 0         
         nextState_fn = (StateFn_ref)StateFunction_PowerOff;
         wei_sm_queue_sig(INTSIG_PS_SLEEP_FOREVER, OBJID_MYSELF, NULL, TRUE);

#endif
         break;

      case INTSIG_CASE(INTSIG_NET_DISASSOCIATE):
         wei_save_con_lost_reason(WE_DISCONNECT_DISASS, M80211_MGMT_RC_NOT_VALID);
         nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
         wei_sm_queue_sig(INTSIG_NET_DISASSOCIATE, OBJID_MYSELF, NULL, TRUE);
         break;

      case INTSIG_CASE(INTSIG_NET_REASSOCIATE):
         /* Try to reassociate with the network.*/
         /* Current release does not support re-association when associated.
            It will cause firmware to deactivate power save and wait for a new connect.
            Also in case of RSN_elements(encryption)4-way handshake need to be
            re-negotiated to be able to send data again. */
#if 0            
         if (Mlme_Send(Mlme_CreateReassociateRequest, 0, wei_send_cmd))
         {
            wifiEngineState.main_state = driverAssociating;
         }
         else
         {
            // TODO: not _Connecting ???
            nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
            wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
         }
#endif
         break;

      case INTSIG_CASE(INTSIG_NET_LEAVE_IBSS):
         nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
         wei_sm_queue_sig(INTSIG_NET_LEAVE_IBSS, OBJID_MYSELF, NULL, TRUE);
         break;

      case INTSIG_CASE(INTSIG_UNPLUG_TRAFFIC): 
         return (StateFn_ref)StateFunction_PowerOff;

      /* MGMT */
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_IND):
      case MGMT_RX_CASE(MLME_DISASSOCIATE_IND):         
      case MGMT_RX_CASE(NRP_MLME_PEER_STATUS_IND):
      case MGMT_RX_CASE(MLME_REASSOCIATE_CFM):
      case MGMT_RX_CASE(MLME_AUTHENTICATE_IND):
      case MGMT_RX_CASE(MLME_MICHAEL_MIC_FAILURE_IND):
         nextState_fn = (StateFn_ref)handle_events(msg, param, nextState_fn);
         break;
         

      case MGMT_RX_CASE(MLME_JOIN_CFM):
      case MGMT_RX_CASE(MLME_AUTHENTICATE_CFM):
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_CFM):
      case MGMT_RX_CASE(MLME_ASSOCIATE_CFM):
      case MGMT_RX_CASE(MLME_DISASSOCIATE_CFM):
      case MGMT_RX_CASE(MLME_START_CFM):
         /* design flaw */
         DE_TRACE_INT(TR_SM, "SEVERE WARNING MGMT CFM %X\n", msg);
         DE_SM_ASSERT(FALSE);
         break;

#if (DE_CCX == CFG_INCLUDED)
      case MGMT_RX_CASE(NRP_MLME_ADDTS_CFM):
	     DE_TRACE_STATIC(TR_SM,"ADDTS Confirmation handled\n");
		 break;
#endif //DE_CCX
      default:
      {
         DE_TRACE_STATIC2(TR_SM, "Unhandled message  %s\n", HMG_Signal_toToName(msg));
         DE_SM_ASSERT(FALSE);
      }
      break;
  }

  return  nextState_fn;
}

/*****************************************************************************
** StateFunction_Disconnecting
**
** In this state a disconnect sequence is performed, when completed
** a state change to function StateFunction_Disonnected is performed.
******************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Disconnecting)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
   WiFiEngine_net_t *net = NULL;

   net = wei_netlist_get_current_net();
   /* will always be pressent unless when unpluged */
   DE_SM_ASSERT(net);

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
         wifiEngineState.main_state = driverDisconnecting;
         break;

      case INTSIG_CASE(INTSIG_EXIT): 
         break;

      case INTSIG_CASE(INTSIG_PS_SLEEP_FOREVER):   
         soft_shutdown = TRUE;
#if 0         
         nextState_fn = (StateFn_ref)StateFunction_PowerOff;
         wei_sm_queue_sig(INTSIG_PS_SLEEP_FOREVER, OBJID_MYSELF, NULL, TRUE);
#endif
         break;         

      case INTSIG_CASE(INTSIG_NET_DEAUTHENTICATE):
         if( wifiEngineState.main_state == driverDisconnecting)
         {
            wifiEngineState.main_state = driverDeauthenticating;
            Mlme_Send(Mlme_CreateDeauthenticateRequest, 0, wei_send_cmd);
         }
         break;

      case INTSIG_CASE(INTSIG_NET_DISASSOCIATE):
         if( wifiEngineState.main_state == driverDisconnecting)
         {
            DE_ASSERT(net);
            wifiEngineState.main_state = driverDisassociating;
            net = wei_netlist_get_current_net();
            if(net->bss_p->bssType == M80211_CAPABILITY_ESS) 
            {
               Mlme_Send(Mlme_CreateDisassociateRequest, 0, wei_send_cmd);
            } 
            else 
            {
               wei_sm_queue_sig(INTSIG_NET_LEAVE_IBSS, OBJID_MYSELF, DUMMY, TRUE);
            }
         }
         break;
         
      case INTSIG_CASE(INTSIG_NET_LEAVE_BSS):
         Mlme_Send(Mlme_CreateLeaveBSSRequest, 0, wei_send_cmd);
         break;

      case MGMT_RX_CASE(NRP_MLME_BSS_LEAVE_CFM):
          Mlme_HandleLeaveBSSConfirm((m80211_nrp_mlme_bss_leave_cfm_t *)param->p);

          if(!wei_is_hmg_auto_mode())
          {
             if(wifiEngineState.main_state == driverAuthenticating)
             {
               /* deauthenticate cfm has not been received */

             }
             else
             {
                we_ind_send(WE_IND_DEAUTHENTICATE_CFM,(void *)&deauthenticate_cfm);
             }
          }
          
          nextState_fn = (StateFn_ref)StateFunction_Disconnected;
         break;

      case MGMT_RX_CASE(NRP_MLME_IBSS_LEAVE_CFM):
         Mlme_HandleLeaveIBSSConfirm((m80211_nrp_mlme_ibss_leave_cfm_t *)param->p);
         nextState_fn = (StateFn_ref)StateFunction_Disconnected;
         break;        
      case INTSIG_CASE(INTSIG_NET_LEAVE_IBSS):
         wei_netlist_clear_sta_nets();
         Mlme_Send(Mlme_CreateLeaveIBSSRequest, 0, wei_send_cmd);
         break;

      /* ignore: may happen on disconnect during connecting */
      case MGMT_RX_CASE(MLME_JOIN_CFM) :
      case MGMT_RX_CASE(MLME_AUTHENTICATE_CFM):
      case MGMT_RX_CASE(MLME_ASSOCIATE_CFM):
      case MGMT_RX_CASE(MLME_REASSOCIATE_CFM):
         break;

      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_IND):  /* occur seldomly */
      case MGMT_RX_CASE(NRP_MLME_PEER_STATUS_IND): /* occur frequently */
         break;         

      /* MGMT */
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_CFM):
      case MGMT_RX_CASE(MLME_DISASSOCIATE_CFM):
         nextState_fn = (StateFn_ref)handle_events(msg, param, nextState_fn);         
         break;

      default:
         DE_TRACE_STATIC2(TR_SM, "Unhandled message  %s\n", HMG_Signal_toToName(msg));
         DE_SM_ASSERT(FALSE);
   }

   return nextState_fn;
}

/*****************************************************************************
** StateFunction_PowerOff
**
** In this state the host driver will: wait to be activated,
** The device has been powered off or removed completely.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_PowerOff)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_PowerOff;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
         /* nothing to do */
         break;

      case INTSIG_CASE(INTSIG_EXIT):
         /* nothing to do */
         break;

      case INTSIG_CASE(INTSIG_POWER_UP):
          ps_ctx.coredump_started_h = NULL;
          ps_ctx.coredump_started_h = we_ind_register(
                  WE_IND_CORE_DUMP_START, "WE_IND_CORE_DUMP_START",
                  coredump_started_cb, NULL, RELEASE_IND_AFTER_EVENT, NULL);   
         DE_ASSERT(ps_ctx.coredump_started_h != NULL);  
         nextState_fn = (StateFn_ref)StateFunction_Disconnected;
         break;

      case INTSIG_CASE(INTSIG_PS_SLEEP_FOREVER):
      {
         DE_SM_ASSERT(wifiEngineState.main_state != driverConnected);

         /* Delete all active scan jobs before sending sleep forever */
         WiFiEngine_RemoveAllScanJobs();

         if (Mlme_Send(Mlme_CreateSleepForeverReq, 0, wei_send_cmd))
         {
            DE_TRACE_STATIC(TR_SM, "Sending HIC_CTRL_SLEEP_FOREVER_REQ\n");            
         }
         else 
         {
            DE_TRACE_STATIC(TR_SM, "Mlme_CreateSleepForeverReq() failed in WiFiEngine_SoftShutdown()\n");              
            DE_BUG_ON(1, "Mlme_CreateSleepForeverReq() failed in WiFiEngine_SoftShutdown()\n");
         }
      }
      break;

      case CTRL_RX_CASE(HIC_CTRL_SLEEP_FOREVER_CFM):
         wifiEngineState.main_state = driverDisconnected;         
         Mlme_HandleHICInterfaceSleepForeverConfirm((hic_ctrl_sleep_forever_cfm_t *)param->p);
         DriverEnvironment_indicate(WE_IND_SLEEP_FOREVER, NULL, 0);
         break;

      default:
         DE_TRACE_STATIC2(TR_SM, "Unhandled message  %s\n", HMG_Signal_toToName(msg));
         DE_SM_ASSERT(FALSE);
   }

   return nextState_fn;
}

/*** Sub States ***/

/****************************************************************************
** StateFunction_Idle
**
** Default state that shoud never be executed.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Idle)
{
   //DE_TRACE_STATIC2(TR_SM, "State is Idle, handling msg %s\n", HMG_Signal_toToName(msg));
   return (StateFn_ref)StateFunction_Idle;
}


static StateFn_ref handle_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn)
{
   StateFn_ref Statefn = (StateFn_ref)fn; 
   
   if(Statefn == (StateFn_ref)StateFunction_Connecting)
   {    
      if(wei_is_hmg_auto_mode())
      {
         Statefn = handle_auto_connecting_events(msg, param, Statefn);
      }
      else
      {
         Statefn = handle_transparent_connecting_events(msg, param, Statefn);
      }
   }
   else if(Statefn == (StateFn_ref)StateFunction_Connected)
   {   
      Statefn = handle_connected_events(msg, param, Statefn);
   }
   else if(Statefn == (StateFn_ref)StateFunction_Disconnecting)
   {  
      Statefn = handle_disconnecting_events(msg, param, Statefn);      
   }
   else
   {
      DE_TRACE_STATIC(TR_SM, "Illegal hmg state\n");
      DE_ASSERT(FALSE);
   }
   return Statefn;
      
}


static int do_authenticate(int first_try,
                           WiFiEngine_net_t *net)
{
   switch(wifiEngineState.config.authenticationMode) {
      case Authentication_Autoselect:
         if(first_try
            && wifiEngineState.key_state.key_enc_type == Encryption_WEP) {
            DE_TRACE_STATIC(TR_AUTH, "authentication auto, trying shared\n");
            net->auth_mode = Authentication_Shared;
         } else {
            DE_TRACE_STATIC(TR_AUTH, "authentication auto, trying open\n");
            net->auth_mode = Authentication_Open;
         }
         break;
      default:
         net->auth_mode =
            wifiEngineState.config.authenticationMode;
         break;
   }

   DE_TRACE_INT3(TR_AUTH, "first_try = %u, auth_mode = %u (%u)\n",
                 first_try, 
                 wifiEngineState.config.authenticationMode, 
                 net->auth_mode);
   return Mlme_Send(Mlme_CreateAuthenticateRequest, 0, wei_send_cmd);
}

/****************************************************************************
** handle_auto_connecting_events
**
** Handle events when acting in auto connect mode.
*****************************************************************************/
static StateFn_ref handle_auto_connecting_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn)
{
   WiFiEngine_net_t *net;
   StateFn_ref nextState_fn = (StateFn_ref)fn;    

   net = wei_netlist_get_current_net();
   DE_ASSERT(net != NULL);
   
   switch(msg) 
   {
      case MGMT_RX_CASE(MLME_JOIN_CFM):
         {
            m80211_mlme_join_cfm_t *cfm = (m80211_mlme_join_cfm_t *)param->p;

            if (Mlme_HandleJoinConfirm(cfm))
            {
               if(net->bss_p->bssType == M80211_CAPABILITY_ESS)
               {
                  /* Try to authenticate with the joined network. */
                  if(!do_authenticate(TRUE, net))
                  {
                     /* failure */
                     nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
                     wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
                     wei_save_con_failed_reason(WE_CONNECT_WIFI_ENGINE_FAIL, cfm->result);
                     NotifyConnectFailed();
                  }
                  else
                  {
                     /* success */
                     wifiEngineState.main_state = driverAuthenticating;
                  }
               }
               else
               {
                  /* In ibss mlme_deauthenticate_req nor mlme_associate_req is sent,
                     connection success/failed is is indicated by message mlme_peer_status_ind */
                  DE_ASSERT(net->bss_p->bssType == M80211_CAPABILITY_IBSS);
               }
            }
            else
            {
               /* failure */
               nextState_fn = (StateFn_ref)StateFunction_Disconnected;
               wei_save_con_failed_reason(WE_CONNECT_JOIN_FAIL, cfm->result);
               NotifyConnectFailed();
            }
         }
         break;

         case MGMT_RX_CASE(MLME_AUTHENTICATE_CFM):
         {
            m80211_mlme_authenticate_cfm_t *cfm = (m80211_mlme_authenticate_cfm_t *)param->p;

            if (Mlme_HandleAuthenticateConfirm(cfm))
            {
               /* Try to associate with the network. */
#if (DE_CCX_ROAMING == CFG_INCLUDED)
	      int (*p)(hic_message_context_t*, int) = (ccxnet) ? Mlme_CreateReassociateRequest : Mlme_CreateAssociateRequest;
	      if (!Mlme_Send(p, 0, wei_send_cmd))
#else
               if (!Mlme_Send(Mlme_CreateAssociateRequest, 0, wei_send_cmd))
#endif
               {
                  nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
                  wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
                  wei_save_con_failed_reason(WE_CONNECT_WIFI_ENGINE_FAIL, cfm->result);
                  NotifyConnectFailed();
               }
               else
               {
                  wifiEngineState.main_state = driverAssociating;
               }
            }
            else
            {
               int failure = TRUE;
               if(wifiEngineState.config.authenticationMode == 
                  Authentication_Autoselect
                  && net->auth_mode == Authentication_Shared) {
                  /* try again with open */
                  failure = !do_authenticate(FALSE, net);
               }
               if(failure) {
                  nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
                  wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
                  wei_save_con_failed_reason(WE_CONNECT_AUTH_FAIL, cfm->result);
                  NotifyConnectFailed();
               }
            }
         }
         break;         

         case MGMT_RX_CASE(MLME_ASSOCIATE_CFM):
         {
            m80211_mlme_associate_cfm_t *cfm = (m80211_mlme_associate_cfm_t *)param->p;

            if (Mlme_HandleAssociateConfirm(cfm))
            {
#ifdef WITH_8021X_PORT
               check_if_close_port();
#endif /* WITH_8021X_PORT */
               nextState_fn = (StateFn_ref)StateFunction_Connected;
               wifiEngineState.main_state = driverAssociated;
            }
            else
            {
               /* Send leave request to remove the instance created in fw */
               nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
               wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
               wei_save_con_failed_reason(WE_CONNECT_ASSOC_FAIL, cfm->result);
               NotifyConnectFailed();
            }
            
         }
         break;

         case MGMT_RX_CASE(MLME_START_CFM):
         if(net->bss_p->bssType == M80211_CAPABILITY_ESS)
         {
            DE_BUG_ON(1, "Illegal net when starting ibss\n");
         }
         else
         {
            if (!Mlme_HandleStartConfirm((m80211_mlme_start_cfm_t *)param->p))
            {
               nextState_fn = (StateFn_ref)StateFunction_Disconnected;
               NotifyConnectFailed();
            }
            else
            {
               /* Wait for a peer sta to join */
               /* This is notifed in nrp_mlme_peer_status_ind */
            }
         }
         break;
         

         case MGMT_RX_CASE(MLME_REASSOCIATE_CFM):
         {
            m80211_mlme_associate_cfm_t *cfm = (m80211_mlme_associate_cfm_t *)param->p;

            if (Mlme_HandleReassociateConfirm(cfm))
            {
               nextState_fn = (StateFn_ref)StateFunction_Connected;
            }
            else
            {
               nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
               wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
               wei_save_con_failed_reason(WE_CONNECT_ASSOC_FAIL, cfm->result);
               NotifyConnectFailed();
            }
         }
         break;


         case MGMT_RX_CASE(MLME_DEAUTHENTICATE_IND):
         {
            m80211_mlme_deauthenticate_ind_t *ind = (m80211_mlme_deauthenticate_ind_t *)param->p;
            wei_save_con_failed_reason(WE_CONNECT_AUTH_FAIL, ind->value);

            nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
            wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
            NotifyConnectFailed();
         }
         break;

      default:
         DE_TRACE_INT(TR_SM, "Unhandled message %X\n",msg);
      break;

   }

   return nextState_fn;
}


/****************************************************************************
** handle_transparent_connecting_events
**
** Handle events when acting in auto connect mode.
*****************************************************************************/
static StateFn_ref handle_transparent_connecting_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn)
{
   StateFn_ref nextState_fn = (StateFn_ref)fn;     
   
   switch(msg) 
   {
      case MGMT_RX_CASE(MLME_JOIN_CFM):
         {
            m80211_mlme_join_cfm_t *cfm = (m80211_mlme_join_cfm_t *)param->p;
            if (Mlme_HandleJoinConfirm(cfm))
            {
               wifiEngineState.main_state = driverAuthenticating; 
            }
            else
            {
               /* failure */
               nextState_fn = (StateFn_ref)StateFunction_Disconnected; 
            }
            we_ind_send(WE_IND_JOIN_CFM,(void *)cfm);
         }
         break;

         case MGMT_RX_CASE(MLME_AUTHENTICATE_CFM):
         {
            m80211_mlme_authenticate_cfm_t *cfm = (m80211_mlme_authenticate_cfm_t *)param->p;

            if (Mlme_HandleAuthenticateConfirm(cfm))
            {
              wifiEngineState.main_state = driverAssociating;               
            }

            we_ind_send(WE_IND_AUTHENTICATE_CFM,(void *)cfm);
         }
         break;         

         case MGMT_RX_CASE(MLME_ASSOCIATE_CFM):
         {
            m80211_mlme_associate_cfm_t *cfm = (m80211_mlme_associate_cfm_t *)param->p;

            if (Mlme_HandleAssociateConfirm(cfm))
            {
#ifdef WITH_8021X_PORT
               check_if_close_port();
#endif /* WITH_8021X_PORT */               
               nextState_fn = (StateFn_ref)StateFunction_Connected;
               wifiEngineState.main_state = driverAssociated;
            }
            if(!wei_arp_filter_have_dynamic_address())
            {
               /* DHCP is needed */
               WiFiEngine_DHCPStart();
            }
            we_ind_send(WE_IND_ASSOCIATE_CFM,(void *)cfm);  
         }
         break;


         

         case MGMT_RX_CASE(MLME_REASSOCIATE_CFM):
         {
            m80211_mlme_reassociate_cfm_t *cfm = (m80211_mlme_reassociate_cfm_t *)param->p;

            if (Mlme_HandleReassociateConfirm(cfm))
            {
               nextState_fn = (StateFn_ref)StateFunction_Connected;
               wifiEngineState.main_state = driverAssociated;               
            }
            we_ind_send(WE_IND_REASSOCIATE_CFM,(void *)cfm);  
         }
         break;


         case MGMT_RX_CASE(MLME_DEAUTHENTICATE_IND):
         {
            m80211_mlme_deauthenticate_ind_t *ind = (m80211_mlme_deauthenticate_ind_t *)param->p;

            we_ind_send(WE_IND_DEAUTHENTICATE_IND,(void *)ind); 
            
            nextState_fn = (StateFn_ref)StateFunction_Disconnecting;
            wei_sm_queue_sig(INTSIG_NET_LEAVE_BSS, OBJID_MYSELF, NULL, TRUE);
         }
         break;

      default:
         DE_TRACE_INT(TR_SM, "Unhandled message %X\n",msg);
      break;

   }
   
   return nextState_fn;
}

   
/****************************************************************************
** handle_connected_events
**
** Handle events when acting in auto connect mode.
*****************************************************************************/
static StateFn_ref handle_connected_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn )
{
   WiFiEngine_net_t *net;
   StateFn_ref nextState_fn = (StateFn_ref)fn;      
    
   

   net = wei_netlist_get_current_net();
   
   switch(msg) 
   {
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_IND):
         {
            m80211_mlme_deauthenticate_ind_t*  ind = (m80211_mlme_deauthenticate_ind_t*)param->p;
            nextState_fn = (StateFn_ref)StateFunction_Disconnecting;

            Mlme_HandleDeauthenticateInd(ind);
            wei_save_con_lost_reason(WE_DISCONNECT_DEAUTH_IND, M80211_MGMT_RC_NOT_VALID);
            if(wei_is_hmg_auto_mode())
            {
               wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE);
            }
            else
            {
               we_ind_send(WE_IND_DEAUTHENTICATE_IND,(void *)ind);
            }
         }
         break;

      case MGMT_RX_CASE(MLME_DISASSOCIATE_IND):
         {
            m80211_mlme_disassociate_ind_t*  ind = (m80211_mlme_disassociate_ind_t*)param->p;
            nextState_fn = (StateFn_ref)StateFunction_Disconnecting;

            Mlme_HandleDisassociateInd(ind);
            wei_save_con_lost_reason(WE_DISCONNECT_DISASS_IND, M80211_MGMT_RC_NOT_VALID);
            if(wei_is_hmg_auto_mode())
            {           
               
               wei_sm_queue_sig(INTSIG_NET_DISASSOCIATE, OBJID_MYSELF, NULL, TRUE);
               
            }
            else
            {
               we_ind_send(WE_IND_DISASSOCIATE_IND,(void *)ind);  
            }
         }
         break;

      case MGMT_RX_CASE(NRP_MLME_PEER_STATUS_IND):
         DE_ASSERT(net);
         {
            m80211_nrp_mlme_peer_status_ind_t *ind;
            ind = (m80211_nrp_mlme_peer_status_ind_t *)param->p;
            Mlme_HandleBssPeerStatusInd(ind);
            switch (ind->status) 
            {
               case MLME_PEER_STATUS_CONNECTED:
               {
                  DE_ASSERT(net->bss_p->bssType == M80211_CAPABILITY_IBSS);
                  if(wei_is_ibss_connected(ind))
                  {
                     DriverEnvironment_indicate(WE_IND_80211_IBSS_CONNECTED, NULL, 0);
                  }
               }
               break;

               case MLME_PEER_STATUS_RESTARTED:
               /* Handled by firmware */
               break;

               /* ibss */
               case MLME_PEER_STATUS_NOT_CONNECTED:
               /* infrastructure */   
               case MLME_PEER_STATUS_INCOMPATIBLE:      
               case MLME_PEER_STATUS_TX_FAILED:
               case MLME_PEER_STATUS_RX_BEACON_FAILED:
               {

                  if(net->bss_p->bssType == M80211_CAPABILITY_ESS)
                  {
                     nextState_fn = (StateFn_ref)StateFunction_Disconnecting; 

                     if(wei_is_hmg_auto_mode())
                     {
                        
                         DriverEnvironment_indicate(WE_IND_AP_MISSING_IN_ACTION, 
                           &ind->status, sizeof(ind->status));                        
                         wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, NULL, TRUE); 
                     }
                     else
                     {                 
                        if(ind->status == MLME_PEER_STATUS_TX_FAILED)
                        {
                           /* There is no listener for this event yet */
                           //we_ind_send(WE_IND_TX_FAIL_IND,(void *)ind);
                           if(!we_ind_send(WE_IND_LINK_LOSS_IND,(void *)ind))
                           {
                              DE_TRACE_STATIC(TR_NOISE, "No available events\n");
                           }
                        }
                        else
                        {
                           if(!we_ind_send(WE_IND_LINK_LOSS_IND,(void *)ind))
                           {
                              DE_TRACE_STATIC(TR_NOISE, "No available events\n");
                           }
                        }
                     }
                  }
                  else
                  {
                     wei_ibss_remove_net(ind);
                     
                     if(wei_ibss_disconnect(ind))
                     {
                        /* No more ibss stations */
                        DriverEnvironment_indicate(WE_IND_80211_IBSS_DISCONNECTED, NULL, 0);
                        wei_sm_queue_sig(INTSIG_NET_LEAVE_IBSS, OBJID_MYSELF, NULL, TRUE); 
                        nextState_fn = (StateFn_ref)StateFunction_Disconnecting; 
                     }
                  }
               }
               break;
               case MLME_PEER_STATUS_RX_BEACON_FAILED_WARNING:
                   we_lqm_trigger_scan(MLME_PEER_STATUS_RX_BEACON_FAILED_WARNING);
                  break;
               case MLME_PEER_STATUS_TX_FAILED_WARNING:
                  DriverEnvironment_indicate(WE_IND_TXFAIL, NULL, 0);
                  break;
               case MLME_PEER_STATUS_ROUNDTRIP_FAILED: 
               {
                  we_conn_incompatible_ind_t data;
                  data.reason_code = 0;
                  DriverEnvironment_indicate(WE_IND_CONN_INCOMPATIBLE, &data, sizeof(data));
               }
               break;
               
               default:
                  DE_TRACE_STATIC(TR_ASSOC, "Unknown BSS peer status\n");
               break;                     
            }
         }
         break;

      case MGMT_RX_CASE(MLME_REASSOCIATE_CFM):
         {            
            m80211_mlme_reassociate_cfm_t *cfm = (m80211_mlme_reassociate_cfm_t *)param->p;
            if (Mlme_HandleReassociateConfirm((m80211_mlme_associate_cfm_t *)param->p))
            {
               DriverEnvironment_indicate(WE_IND_80211_CONNECTED, NULL, 0);
               wifiEngineState.main_state = driverConnected;
            }
            else
            {
               if(wei_is_hmg_auto_mode())
               {
                 wei_sm_queue_sig(INTSIG_NET_DEAUTHENTICATE, OBJID_MYSELF, DUMMY, TRUE);
               }
            }

            if(!wei_is_hmg_auto_mode())
            {
               we_ind_send(WE_IND_REASSOCIATE_CFM,(void *)cfm);
            }

         }
         break;

      case MGMT_RX_CASE(MLME_AUTHENTICATE_IND):
         Mlme_HandleAuthenticateInd((m80211_mlme_authenticate_ind_t *)param->p);
         break;

      case MGMT_RX_CASE(MLME_MICHAEL_MIC_FAILURE_IND):
         Mlme_HandleMichaelMICFailureInd((m80211_mlme_michael_mic_failure_ind_t *)param->p);
         break;

      default:
         DE_TRACE_INT(TR_SM, "Unhandled message %X\n",msg);         
         break;
         
   }

   return nextState_fn;   
}

/****************************************************************************
** handle_auto_disconnecting_events
**
** Handle events when acting in auto connect mode.
*****************************************************************************/
static StateFn_ref handle_disconnecting_events(ucos_msg_id_t msg, wei_sm_queue_param_s *param, StateFn_ref fn)
{
   StateFn_ref nextState_fn = (StateFn_ref)fn;      

   switch(msg) 
   {   
      case MGMT_RX_CASE(MLME_DEAUTHENTICATE_CFM):
         {
            m80211_mlme_deauthenticate_cfm_t *cfm = (m80211_mlme_deauthenticate_cfm_t *)param->p;
               
            Mlme_HandleDeauthenticateConfirm(cfm);
            if(!wei_is_hmg_auto_mode())
            {
               deauthenticate_cfm = *cfm;
               // we_ind_send(WE_IND_DEAUTHENTICATE_CFM,(void *)cfm);
            }

            Mlme_Send(Mlme_CreateLeaveBSSRequest, 0, wei_send_cmd);

         }
         break;

      case MGMT_RX_CASE(MLME_DISASSOCIATE_CFM):
         {
            m80211_mlme_disassociate_cfm_t *cfm = (m80211_mlme_disassociate_cfm_t *)param->p; 
            
            Mlme_HandleDisassociateConfirm((m80211_mlme_disassociate_cfm_t *)param->p);
            if(!wei_is_hmg_auto_mode())
            {
               we_ind_send(WE_IND_DISASSOCIATE_CFM,(void *)cfm);
            }
            else
            {
               wifiEngineState.main_state = driverDeauthenticating;
               Mlme_Send(Mlme_CreateDeauthenticateRequest, 0, wei_send_cmd);
            }
         }
         break;

      default:
         DE_TRACE_INT(TR_SM, "Unhandled message %X\n",msg);
         break;
   }

   return nextState_fn;   
}


/****************************************************************************
** check_if_close_port
**
** Check if 802.11X port shall be closed(4-way handshake complete)
*****************************************************************************/
#ifdef WITH_8021X_PORT
static void check_if_close_port(void)
{
   WiFiEngine_Auth_t auth;
   int close_policy = 0;
  
   if (!WiFiEngine_GetAuthenticationMode(&auth))
   {
      // Eh?
      return;
   }

   if (auth >= Authentication_8021X)
   {
      close_policy = 1;
   }

   // first check for changes
   if(WiFiEngine_Is8021xPortClosed())
   {
      if(close_policy)
      {
         // already closed
         return;
      }
   } else {
      if(!close_policy)
      {
         // already open
         return;
      }
   }

   if(close_policy)
   {
      DE_TRACE_INT(TR_AUTH, "8021x port changed to closed (auth mode %d)\n", auth);
      WiFiEngine_8021xPortClose();
   } else {
      DE_TRACE_INT(TR_AUTH, "8021x port changed to open (auth mode %d)\n", auth);
      WiFiEngine_8021xPortOpen();
   }
}
#endif /* WITH_8021X_PORT */

#ifdef WIFI_DEBUG_ON
static char* HMG_Signal_toToName(int msg)
{
   #define INTSIG_DEBUG_CASE(name) case INTSIG_CASE(name): return #name
   #define CTRL_DEBUG_CASE(name)   case CTRL_RX_CASE(name): return #name
   #define MGMT_RX_DEBUG_CASE(name)   case MGMT_RX_CASE(name): return #name
   #define MGMT_TX_DEBUG_CASE(name)   case MGMT_TX_CASE(name): return #name

   switch(msg) {
      INTSIG_DEBUG_CASE(INTSIG_INIT);
      INTSIG_DEBUG_CASE(INTSIG_EXIT);
      INTSIG_DEBUG_CASE(INTSIG_NET_IDLE);
      INTSIG_DEBUG_CASE(INTSIG_NET_LOST);
      INTSIG_DEBUG_CASE(INTSIG_NET_LIST);
      INTSIG_DEBUG_CASE(INTSIG_NET_RESELECT);
      INTSIG_DEBUG_CASE(INTSIG_NET_CONNECT);
      INTSIG_DEBUG_CASE(INTSIG_NET_JOIN);
      INTSIG_DEBUG_CASE(INTSIG_NET_AUTHENTICATE);
      INTSIG_DEBUG_CASE(INTSIG_NET_ASSOCIATE);
      INTSIG_DEBUG_CASE(INTSIG_NET_START);
      INTSIG_DEBUG_CASE(INTSIG_NET_DEAUTHENTICATE);
      INTSIG_DEBUG_CASE(INTSIG_NET_LEAVE_IBSS);
      INTSIG_DEBUG_CASE(INTSIG_NET_LEAVE_BSS);
      INTSIG_DEBUG_CASE(INTSIG_NET_REASSOCIATE);
      INTSIG_DEBUG_CASE(INTSIG_NET_DISASSOCIATE);
      INTSIG_DEBUG_CASE(INTSIG_POWER_UP);
      INTSIG_DEBUG_CASE(INTSIG_UNPLUG_TRAFFIC);
      INTSIG_DEBUG_CASE(INTSIG_UNPLUG_PS);
      INTSIG_DEBUG_CASE(INTSIG_DEVICE_POWER_SLEEP);
      INTSIG_DEBUG_CASE(INTSIG_ENABLE_WMM_PS);
      INTSIG_DEBUG_CASE(INTSIG_DISABLE_WMM_PS);
      INTSIG_DEBUG_CASE(INTSIG_LEGACY_PS_CONFIGURATION_CHANGED);
      INTSIG_DEBUG_CASE(INTSIG_DISABLE_PS);
      INTSIG_DEBUG_CASE(INTSIG_ENABLE_PS);
      INTSIG_DEBUG_CASE(INTSIG_DEVICE_CONNECTED);
      INTSIG_DEBUG_CASE(INTSIG_DEVICE_DISCONNECTED);
      INTSIG_DEBUG_CASE(INTSIG_PS_SLEEP_FOREVER);

      CTRL_DEBUG_CASE(HIC_CTRL_SLEEP_FOREVER_CFM);
      CTRL_DEBUG_CASE(HIC_CTRL_SET_ALIGNMENT_CFM);
      CTRL_DEBUG_CASE(HIC_CTRL_HL_SYNC_CFM);

      MGMT_RX_DEBUG_CASE(MLME_RESET_CFM);
      MGMT_RX_DEBUG_CASE(MLME_SCAN_CFM);
      MGMT_RX_DEBUG_CASE(MLME_POWER_MGMT_CFM);
      MGMT_RX_DEBUG_CASE(MLME_JOIN_CFM);
      MGMT_RX_DEBUG_CASE(MLME_AUTHENTICATE_CFM);
      MGMT_RX_DEBUG_CASE(MLME_AUTHENTICATE_IND);
      MGMT_RX_DEBUG_CASE(MLME_DEAUTHENTICATE_CFM);
      MGMT_RX_DEBUG_CASE(MLME_DEAUTHENTICATE_IND);
      MGMT_RX_DEBUG_CASE(MLME_ASSOCIATE_CFM);
      MGMT_RX_DEBUG_CASE(MLME_ASSOCIATE_IND);
      MGMT_RX_DEBUG_CASE(MLME_REASSOCIATE_CFM);
      MGMT_RX_DEBUG_CASE(MLME_REASSOCIATE_IND);
      MGMT_RX_DEBUG_CASE(MLME_DISASSOCIATE_CFM);
      MGMT_RX_DEBUG_CASE(MLME_DISASSOCIATE_IND);
      MGMT_RX_DEBUG_CASE(MLME_START_CFM);
      MGMT_RX_DEBUG_CASE(MLME_SET_KEY_CFM);
      MGMT_RX_DEBUG_CASE(MLME_DELETE_KEY_CFM);
      MGMT_RX_DEBUG_CASE(MLME_SET_PROTECTION_CFM);
      MGMT_RX_DEBUG_CASE(MLME_MICHAEL_MIC_FAILURE_IND);
      MGMT_RX_DEBUG_CASE(NRP_MLME_BSS_LEAVE_CFM);
      MGMT_RX_DEBUG_CASE(NRP_MLME_IBSS_LEAVE_CFM);
      MGMT_RX_DEBUG_CASE(NRP_MLME_PEER_STATUS_IND);
#if (DE_CCX == CFG_INCLUDED)
	  MGMT_RX_DEBUG_CASE(NRP_MLME_ADDTS_CFM);
#endif //DE_CCX

      MGMT_TX_DEBUG_CASE(MLME_RESET_REQ);
      MGMT_TX_DEBUG_CASE(MLME_SCAN_REQ);
      MGMT_TX_DEBUG_CASE(MLME_POWER_MGMT_REQ);
      MGMT_TX_DEBUG_CASE(MLME_JOIN_REQ);
      MGMT_TX_DEBUG_CASE(MLME_AUTHENTICATE_REQ);
      MGMT_TX_DEBUG_CASE(MLME_DEAUTHENTICATE_REQ);
      MGMT_TX_DEBUG_CASE(MLME_ASSOCIATE_REQ);
      MGMT_TX_DEBUG_CASE(MLME_REASSOCIATE_REQ);
      MGMT_TX_DEBUG_CASE(MLME_DISASSOCIATE_REQ);
      MGMT_TX_DEBUG_CASE(MLME_START_REQ);
      MGMT_TX_DEBUG_CASE(MLME_SET_KEY_REQ);
      MGMT_TX_DEBUG_CASE(MLME_DELETE_KEY_REQ);
      MGMT_TX_DEBUG_CASE(MLME_SET_PROTECTION_REQ);
      MGMT_TX_DEBUG_CASE(NRP_MLME_BSS_LEAVE_REQ);
      MGMT_TX_DEBUG_CASE(NRP_MLME_IBSS_LEAVE_REQ);

      default:
         DE_TRACE_INT(TR_ALL, "Unknown message in hmg_traffic %X", msg);
         DE_SM_ASSERT(FALSE);
         return "unknown";
   }

   #undef INTSIG_DEBUG_CASE
   #undef CTRL_DEBUG_CASE
   #undef MGMT_RX_DEBUG_CASE
   #undef MGMT_TX_DEBUG_CASE
}
#endif /* WIFI_DEBUG_ON */

/******************************* END OF FILE ********************************/
