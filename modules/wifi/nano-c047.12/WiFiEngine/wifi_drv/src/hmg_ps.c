
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
/** @defgroup hmg_ps WiFiEngine internal state machine for power save.
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
#include "hmg_traffic.h"
#include "hmg_defs.h"
#include "hmg_ps.h"
#include "wei_netlist.h"
#include "macWrapper.h"
#include "we_ind.h"

/*****************************************************************************
                       POSSIBLE STATE CHANGES

     StateFunction_Ps_Disabled_Unconnected = ps_dis_unconnected:
     Not associated and power save is disabled.
     
     StateFunction_Ps_Enabled_Unconnected = ps_en_unconnected:
     Not associated and unconnected power save is enabled.
     
     StateFunction_Ps_Disabled_Connected = ps_dis_connected:
     Associated and power save is disabled.
     
     StateFunction_Ps_Enabled_Connected = ps_en_connected:
     Associated and 802.11 power save is enabled.
     
     ------------------------------------------------------------------------
     !  State function    ! Event                        ! New state
     ------------------------------------------------------------------------
     !ps_dis_unconnected  ! INTSIG_DISABLE_PS            !   -
     !                    ! INTSIG_ENABLE_PS             ! ps_en_unconnected
     !                    ! INTSIG_DEVICE_CONNECTED      ! ps_dis_connected
     !                    ! INTSIG_DEVICE_DISCONNECTED   !   -
     -------------------------------------------------------------------------
     !ps_en_unconnected   ! INTSIG_DISABLE_PS            ! ps_dis_unconnected
     !                    ! INTSIG_ENABLE_PS             !   -
     !                    ! INTSIG_DEVICE_CONNECTED      ! ps_en_connected
     !                    ! INTSIG_DEVICE_DISCONNECTED   !   -
     ------------------------------------------------------------------------- 
     !ps_dis_connected    ! INTSIG_DISABLE_PS            !   -
     !                    ! INTSIG_ENABLE_PS             ! ps_en_connected
     !                    ! INTSIG_DEVICE_CONNECTED      !   -
     !                    ! INTSIG_DEVICE_DISCONNECTED   ! ps_dis_unconnected
     -------------------------------------------------------------------------   
     !ps_en_connected     ! INTSIG_DISABLE_PS            ! ps_dis_connected
     !                    ! INTSIG_ENABLE_PS             !   -
     !                    ! INTSIG_DEVICE_CONNECTED      !   -
     !                    ! INTSIG_DEVICE_DISCONNECTED   ! ps_en_unconnected
     -------------------------------------------------------------------------      
*****************************************************************************/


/*****************************************************************************
T E M P O R A R Y   T E S T V A R I A B L E S
*****************************************************************************/

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/
#define OBJID_MYSELF    SYSDEF_OBJID_HOST_MANAGER_PS

#define EXECUTE_STATE(_StateFunction)   (StateFn_t)(_StateFunction)(msg, (wei_sm_queue_param_s *)sm_param)
#define INIT_STATE(_StateFunction)      (_StateFunction)(INTSIG_INIT, DUMMY)
#define EXIT_STATE(_StateFunction)      (_StateFunction)(INTSIG_EXIT, DUMMY)


/*****************************************************************************
L O C A L   D A T A T Y P E S
*****************************************************************************/

/*****************************************************************************
L O C A L   F U N C T I O N   P R O T O T Y P E S
*****************************************************************************/
static void HMG_entry_ps(ucos_msg_id_t msg, ucos_msg_param_t param);

static void connected_cb(wi_msg_param_t param, void* priv);
static void disconnecting_cb(wi_msg_param_t param, void* priv);
/*********************/
/* State functions   */
/*********************/
/* Main states */
DECLARE_STATE_FUNCTION(StateFunction_Init);
DECLARE_STATE_FUNCTION(StateFunction_Idle);
DECLARE_STATE_FUNCTION(StateFunction_Ps_Disabled_Unconnected);
DECLARE_STATE_FUNCTION(StateFunction_Ps_Disabled_Connected);
DECLARE_STATE_FUNCTION(StateFunction_Ps_Enabled_Unconnected);
DECLARE_STATE_FUNCTION(StateFunction_Ps_Enabled_Connected);

/*maps a enum-value to a name (used for debuging)*/
#ifdef WIFI_DEBUG_ON
static char* HMG_Signal_toToName(int msg);
#endif


/*****************************************************************************
 M O D U L E   V A R I A B L E S
*****************************************************************************/
static StateVariables_t stateVars = { StateFunction_Idle, StateFunction_Idle,
                                     StateFunction_Idle, StateFunction_Idle };
static int psStarted;
static int connected;

static struct ps_ctx_t {
   iobject_t* connected_h;
   iobject_t* disconnecting_h;
} ps_ctx;

/*********************/
/* Error class table */
/*********************/

/*****************************************************************************
G L O B A L   C O N S T A N T S / V A R I A B L E S
*****************************************************************************/

/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/
extern void* we_pm_request(ucos_msg_id_t sig);

/* This function is called from ucos */
void HMG_init_ps(void)
{
   /* Set the default state. */
   stateVars.newState_fn = StateFunction_Init; 
   stateVars.currentState_fn = StateFunction_Idle;
   stateVars.subState_fn = StateFunction_Idle;
   stateVars.newSubState_fn = StateFunction_Idle; 
   connected = FALSE; 
   psStarted = FALSE;

   ucos_register_object(OBJID_MYSELF,
                        SYSCFG_UCOS_OBJECT_PRIO_LOW,
                        (ucos_object_entry_t)HMG_entry_ps,
                        "HMG_POWER_SAVE");
}

/* This function is called from ucos */
void HMG_startUp_ps(void)
{

   /* Since there is no previous state, send the init signal explicitly. */
      wei_sm_queue_sig(INTSIG_INIT, OBJID_MYSELF, DUMMY, TRUE);
}


void HMG_entry_ps(ucos_msg_id_t msg, ucos_msg_param_t param)
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

void HMG_Unplug_ps(void)
{
   stateVars.newState_fn = StateFunction_Init;    
   stateVars.currentState_fn = StateFunction_Init; 
   stateVars.subState_fn = StateFunction_Idle;
   stateVars.newSubState_fn = StateFunction_Idle;
   connected = FALSE; 
   psStarted = FALSE;

   we_ind_deregister_null(&ps_ctx.connected_h);
   we_ind_deregister_null(&ps_ctx.disconnecting_h);
}

/* This function is called directly from WiFiEngine_Initialize() */
void wei_hmg_ps_init(void)
{
   DE_MEMSET(&ps_ctx,0,sizeof(ps_ctx));
   HMG_Unplug_ps();
}


/* This is only called on IOCTL_GET_STATUS, so it can be slow-dumb */
int HMG_GetState_ps()
{
   if (stateVars.subState_fn == StateFunction_Idle)
   {
      return Idle;
   }
 
   else if (stateVars.subState_fn == StateFunction_Init)
   {
      return PsInit;
   }   
   else if (stateVars.subState_fn == StateFunction_Ps_Disabled_Unconnected)
   {
      return PsDisabledUnconnected;
   }   
   else if (stateVars.subState_fn == StateFunction_Ps_Disabled_Connected)
   {
      return PsDisabledConnected;
   }    
   else if (stateVars.subState_fn == StateFunction_Ps_Enabled_Unconnected)
   {
      return PsEnabledUnconnected;
   }     
   else if (stateVars.subState_fn == StateFunction_Ps_Enabled_Connected)
   {
      return PsEnabledConnected;
   } 
   
   return Invalid;
}


/* This is only called on IOCTL_GET_STATUS, so it can be slow-dumb */
int HMG_GetSubState_ps()
{
    return Invalid;
}

static void connected_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_SM, "hmg_ps Device connected\n");     
   connected = TRUE;  
   
   we_pm_request(INTSIG_DEVICE_CONNECTED);
}

static void disconnecting_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_SM, "hmg_ps Device disconnected\n");  
   connected = FALSE;
   we_pm_request(INTSIG_DEVICE_DISCONNECTED);   
}


/*****************************************************************************
L O C A L   F U N C T I O N S
*****************************************************************************/

/****************************************************************************
** StateFunction_Idle
**
** Default state that shoud never be executed.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Idle)
{
   return (StateFn_ref)StateFunction_Idle;
}

/*** Main States ***/
/****************************************************************************
** StateFunction_Init
**
** Default state that shoud never be executed.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Init)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Init;
      
   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }
   
   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
         break;

      case INTSIG_CASE(INTSIG_EXIT):
         break;

      case INTSIG_CASE(INTSIG_EXECUTE):
      {  
         if(!WiFiEngine_GetRegistryPowerFlag())
         {
            /* Power save is permanently disabled */
            WiFiEngine_DisablePowerSave(wifiEngineState.ps_state_machine_uid);
            nextState_fn = (StateFn_ref)StateFunction_Ps_Disabled_Unconnected;             
         }
         else
         {
            DriverEnvironment_enable_target_sleep();         
            nextState_fn = (StateFn_ref)StateFunction_Ps_Enabled_Unconnected;
         }
      }
      break;

      case INTSIG_CASE(INTSIG_POWER_UP):
      {
         DE_TRACE_STATIC(TR_SM, "Enter State Init\n");  
         wifiEngineState.psMainState = PS_MAIN_INIT;
         ps_ctx.connected_h = we_ind_register(
                  WE_IND_80211_CONNECTED, "WE_IND_80211_CONNECTED",
                  connected_cb, NULL, 0, NULL);   
         DE_ASSERT(ps_ctx.connected_h != NULL);

         ps_ctx.disconnecting_h = we_ind_register(
                  WE_IND_80211_DISCONNECTED, "WE_IND_80211_DISCONNECTED",
                  disconnecting_cb, NULL, 0, NULL);  
         DE_ASSERT(ps_ctx.disconnecting_h != NULL);
 
         wei_sm_queue_sig(INTSIG_EXECUTE, OBJID_MYSELF, DUMMY, TRUE);
         
      }
      break;

      /* safe to ignore */
      case INTSIG_CASE(INTSIG_DEVICE_POWER_SLEEP):
      case INTSIG_CASE(INTSIG_DEVICE_DISCONNECTED):
         break;
            
      default:
         DE_TRACE_INT(TR_WARN, "Unhandled message type %x\n",msg);
         DE_TRACE_STATIC2(TR_WARN, "msg %s\n", HMG_Signal_toToName(msg));
         DE_SM_ASSERT(FALSE);
   }

   return (StateFn_ref)nextState_fn;
}


/****************************************************************************
** Substate StateFunction_Ps_Disabled_Unconnected
**
** In this state the host driver will: Initiate WMM ps period and initiate
** start of period.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Ps_Disabled_Unconnected)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Ps_Disabled_Unconnected;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
      {  
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Enter State Ps_Disabled_Unconnected\n");          

         if(psStarted)
         {            
            /* Request resource that prevents power save mechanism 
               for hic interface to start */                 
            wei_request_resource_hic(RESOURCE_DISABLE_PS);
            psStarted = FALSE;
            we_ind_send(WE_IND_PS_DISABLED, NULL );            
         }
         else
         {
            if(wifiEngineState.psMainState != PS_MAIN_INIT)
            {
               /* A transit from StateFunction_Ps_Disabled_Connected
                  has been performed. Inform power manager that the
                  state change is complete. */
               we_ind_send(WE_IND_PS_DISCONNECT_COMPLETE, NULL);
            }
         }
         wifiEngineState.psMainState = PS_MAIN_DISABLED_UNCONNECTED;
         break;
      }
      case INTSIG_CASE(INTSIG_EXIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit State Ps_Disabled_Unconnected\n"); 
         break;

      case INTSIG_CASE(INTSIG_DEVICE_CONNECTED):
           nextState_fn = (StateFn_ref)StateFunction_Ps_Disabled_Connected;  
         break;

      case INTSIG_CASE(INTSIG_ENABLE_PS):
         {     
            nextState_fn = (StateFn_ref)StateFunction_Ps_Enabled_Unconnected;
         }         
         break;

      case INTSIG_CASE(INTSIG_DISABLE_PS):
         /* Should not happen */
         we_ind_send(WE_IND_PS_DISABLED, NULL );
         DE_SM_ASSERT(FALSE);         
         break;


      case INTSIG_CASE(INTSIG_DEVICE_DISCONNECTED):
         /* Connection  failed */         
          we_ind_send(WE_IND_PS_DISCONNECT_COMPLETE, NULL);           
         break;

      case INTSIG_CASE(INTSIG_DISABLE_WMM_PS):
      {
         /* No action in this state */
         we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);
         break;
      }

      case INTSIG_CASE(INTSIG_ENABLE_WMM_PS):
      {
         /* No action in this state */
         we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);
         break;
      }
      
      default:
         DE_TRACE_INT(TR_SM, "Unhandled message type %x\n",msg);
         DE_SM_ASSERT(FALSE);
   }
   return nextState_fn;
}


/****************************************************************************
** Substate StateFunction_Ps_Disabled_Connected
**
** In this state the host driver will: Initiate WMM ps period and initiate
** start of period.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Ps_Disabled_Connected)
{   
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Ps_Disabled_Connected;
   uint32_t result;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
      {  
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Enter State Ps_Disabled_Connected\n");          
         wifiEngineState.psMainState = PS_MAIN_DISABLED_CONNECTED; 
         if(psStarted)
         {           
            /* Request resource that prevents power save mechanism 
               for hic interface to start */            
            wei_request_resource_hic(RESOURCE_DISABLE_PS);  
            psStarted = FALSE;
            we_ind_send(WE_IND_PS_DISABLED, NULL );              
         }
         else
         {
            /* A transit from StateFunction_Ps_Disabled_Unconnected
               has been performed. Inform power manager that the
               state change is complete. */
            we_ind_send(WE_IND_PS_CONNECT_COMPLETE, NULL); 
         }
    
         break;
      }
      case INTSIG_CASE(INTSIG_EXIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit State Ps_Disabled_Connected\n"); 
         break;

      case INTSIG_CASE(INTSIG_DEVICE_DISCONNECTED):
         nextState_fn = (StateFn_ref)StateFunction_Ps_Disabled_Unconnected;
         WiFiEngine_StopDelayPowerSaveTimer();
         break;
           
      case INTSIG_CASE(INTSIG_ENABLE_PS):
         {
            DE_TRACE_STATIC(TR_SM, "Sleep device\n");

            /* Power save is enabled */
            if(connected )
            {                  
               if(WiFiEngine_isAssocSupportingWmmPs() == WIFI_ENGINE_SUCCESS)
               {
                  if (!Mlme_Send(Mlme_CreateHICWMMPeriodStartReq, 0, wei_send_cmd))
                  {
                     DE_TRACE_STATIC(TR_SM, "Failed to create HIC_CTRL_WMM_PS_PERIOD_START_REQ in StateFunction_InitPowerSave\n");
                     DE_BUG_ON(1, "");
                  }
               }
               else
               {
                  if (!Mlme_Send(Mlme_CreatePowerManagementRequest, M80211_PM_ENABLED, wei_send_cmd))
                  {
                     DE_TRACE_STATIC(TR_SM, "Failed to create power management request in state CloseWait\n");
                     DE_BUG_ON(1, "");
                  }
               }
             }   
         }
         break;

      case MGMT_RX_CASE(MLME_POWER_MGMT_CFM):
         {
            if (Mlme_HandlePowerManagementConfirm((m80211_mlme_power_mgmt_cfm_t *)param->p))
            {  
               nextState_fn = (StateFn_ref)StateFunction_Ps_Enabled_Connected;
            }
            else
            {
               /* Failed to enable power save */
               DE_TRACE_STATIC(TR_SM, "Failed to enable power save\n");
            }

         }
         break;

      case MGMT_RX_CASE(NRP_MLME_WMM_PS_PERIOD_START_CFM):
         if (Mlme_HandleHICWMMPeriodStartCfm((m80211_nrp_mlme_wmm_ps_period_start_cfm_t *)param->p,&result ))
         {  
            if (!Mlme_Send(Mlme_CreatePowerManagementRequest, M80211_PM_ENABLED, wei_send_cmd))
            {
               DE_TRACE_STATIC(TR_SM, "Failed to create power management request in state CloseWait\n");
               DE_BUG_ON(1, "");
            }
         }
         else
         {
            /* Ignore */
            DE_TRACE_STATIC(TR_SM, "Failed to start wmm period\n");
         }
         break;

      case INTSIG_CASE(INTSIG_DISABLE_WMM_PS):
      {
         /* No action in this state */
         we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);
         break;
      }

      case INTSIG_CASE(INTSIG_ENABLE_WMM_PS):
      {
         /* No action in this state */
         we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);
         break;
      }
      

      case INTSIG_CASE(INTSIG_DEVICE_CONNECTED):
         /* Should not happen */
         we_ind_send(WE_IND_PS_CONNECT_COMPLETE, NULL); 
         DE_SM_ASSERT(FALSE);
         break;      

      case INTSIG_CASE(INTSIG_DISABLE_PS):
         /* Should not happen */
         we_ind_send(WE_IND_PS_DISABLED, NULL );
         DE_SM_ASSERT(FALSE);         
         break;

      default:
         DE_TRACE_INT(TR_SM, "Unhandled message type %x\n",msg);
         DE_SM_ASSERT(FALSE);
   }

   return nextState_fn;
}



/****************************************************************************
** Substate StateFunction_Ps_Enabled_Unconnected
**
** In this state the host driver will: Initiate WMM ps period and initiate
** start of period.
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Ps_Enabled_Unconnected)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Ps_Enabled_Unconnected;
   uint32_t result;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
      {
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Enter State Ps_Enabled_Unconnected\n");          
         wifiEngineState.psMainState = PS_MAIN_ENABLED_UNCONNECTED;

         if(!psStarted)
         {             
            /* Release resource that prevents power save mechanism 
               for hic interface to start */    
            wei_release_resource_hic(RESOURCE_DISABLE_PS); 
            psStarted = TRUE;
            we_ind_send(WE_IND_PS_ENABLED, NULL );
         }
         else
         {
            /* A transit from StateFunction_Ps_Enabled_Connected
               has been performed. Inform power manager that the
               state change is complete. */
            we_ind_send(WE_IND_PS_DISCONNECT_COMPLETE, NULL); 
         }

    
         break;
      }
      case INTSIG_CASE(INTSIG_EXIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit State Ps_Enabled_Unconnected\n"); 
         break;

      case INTSIG_CASE(INTSIG_DISABLE_PS):
         nextState_fn = (StateFn_ref)StateFunction_Ps_Disabled_Unconnected;
         break;

      case INTSIG_CASE(INTSIG_ENABLE_PS):
         /* Should not happen */
         we_ind_send(WE_IND_PS_ENABLED, NULL );
         DE_SM_ASSERT(FALSE);
         break;         

      case INTSIG_CASE(INTSIG_DEVICE_CONNECTED):
         {
            DE_TRACE_STATIC(TR_SM, "Device connected\n");

            if(connected )
            {                  
               if(WiFiEngine_isAssocSupportingWmmPs() == WIFI_ENGINE_SUCCESS)
               {
                  if (!Mlme_Send(Mlme_CreateHICWMMPeriodStartReq, 0, wei_send_cmd))
                  {
                     DE_TRACE_STATIC(TR_SM, "Failed to create HIC_CTRL_WMM_PS_PERIOD_START_REQ in StateFunction_InitPowerSave\n");
                     DE_BUG_ON(1, "");
                  }
               }
               else
               {
                  if (!Mlme_Send(Mlme_CreatePowerManagementRequest, M80211_PM_ENABLED, wei_send_cmd))
                  {
                     DE_TRACE_STATIC(TR_SM, "Failed to create power management request in state CloseWait\n");
                     DE_BUG_ON(1, "");
                  }
               }
            }
         }
         break;  

      case MGMT_RX_CASE(MLME_POWER_MGMT_CFM):
         {
            if (Mlme_HandlePowerManagementConfirm((m80211_mlme_power_mgmt_cfm_t *)param->p))
            {  
               nextState_fn = (StateFn_ref)StateFunction_Ps_Enabled_Connected;
            }
            else
            {
               /* Failed to enable power save */
               DE_TRACE_STATIC(TR_SM, "Failed to enable power save\n");
            }
         }
         break;

      case MGMT_RX_CASE(NRP_MLME_WMM_PS_PERIOD_START_CFM):
         if (Mlme_HandleHICWMMPeriodStartCfm((m80211_nrp_mlme_wmm_ps_period_start_cfm_t *)param->p,&result ))
         {  
            if (!Mlme_Send(Mlme_CreatePowerManagementRequest, M80211_PM_ENABLED, wei_send_cmd))
            {
               DE_TRACE_STATIC(TR_SM, "Failed to create power management request in state CloseWait\n");
               DE_BUG_ON(1, "");
            }
         }
         else
         {
            /* Ignore */
            DE_TRACE_STATIC(TR_SM, "Failed to start wmm period\n");
         }
         break;

      case INTSIG_CASE(INTSIG_DEVICE_DISCONNECTED):
         /* Connection  failed */         
          we_ind_send(WE_IND_PS_DISCONNECT_COMPLETE, NULL);           
         break;

      case INTSIG_CASE(INTSIG_DISABLE_WMM_PS):
      {
         /* No action in this state */
         we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);
         break;
      }

      case INTSIG_CASE(INTSIG_ENABLE_WMM_PS):
      {
         /* No action in this state */
         we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);
         break;
      }
      

      default:
         DE_TRACE_INT(TR_SM, "Unhandled message type %x\n",msg);
         DE_SM_ASSERT(FALSE);
   }

   return nextState_fn;

}

/*****************************************************************************
** Substate StateFunction_PS_Enabled_Connected
**
** In this state the host driver will: 
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Ps_Enabled_Connected)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Ps_Enabled_Connected;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
      {
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "StateFunction_Ps_Enabled_Connected\n");                    
         wifiEngineState.psMainState = PS_MAIN_ENABLED_CONNECTED;
         if(!psStarted)
         {           
            /* Release resource that prevents power save mechanism 
               for hic interface to start */    
            wei_release_resource_hic(RESOURCE_DISABLE_PS); 
            psStarted = TRUE;
            we_ind_send(WE_IND_PS_ENABLED, NULL );            
         }
         else
         {
            /* A transit from StateFunction_Ps_Enabled_Unconnected
               has been performed. Inform power manager that the
               state change is complete. */
            we_ind_send(WE_IND_PS_CONNECT_COMPLETE, NULL);  
         }
         break;
      }
      case INTSIG_CASE(INTSIG_EXIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit State Ps_Enabled_Connected\n");          
         break;

      case INTSIG_CASE(INTSIG_DISABLE_PS):
         DE_TRACE_STATIC(TR_SM, "Power save stopped \n");
         if (!Mlme_Send(Mlme_CreatePowerManagementRequest, M80211_PM_DISABLED, wei_send_cmd))
         {              
            DE_TRACE_STATIC(TR_SM, "Failed to create power management request\n");
            DE_BUG_ON(1, "");
         }
         break;

      case INTSIG_CASE(INTSIG_ENABLE_PS):
         /* Should not happen */
         we_ind_send(WE_IND_PS_ENABLED, NULL );
         DE_SM_ASSERT(FALSE);
         break;
               
      case INTSIG_CASE(INTSIG_DEVICE_DISCONNECTED):
         DE_TRACE_STATIC(TR_SEVERE, "Device disconnected \n");
         WiFiEngine_StopDelayPowerSaveTimer();            
         nextState_fn = (StateFn_ref)StateFunction_Ps_Enabled_Unconnected;
         break;

      case INTSIG_CASE(INTSIG_ENABLE_WMM_PS):
      {
         /* Ignore */
         DE_TRACE_STATIC(TR_SM, "Enable wmm power save\n");
         if(connected)
         {
            if (!Mlme_Send(Mlme_CreateHICWMMPeriodStartReq, 0, wei_send_cmd))
            {
               DE_TRACE_STATIC(TR_SM, "Failed to create HIC_CTRL_WMM_PS_PERIOD_START_REQ in StateFunction_InitPowerSave\n");
               DE_BUG_ON(1, "");
               
            }            
         }
         else
         {
            we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);  
            DE_TRACE_STATIC(TR_SM, "Not connected wmm will start next connect \n");
         }
         break;
      }


      case INTSIG_CASE(INTSIG_DISABLE_WMM_PS):
      {  
         DE_TRACE_STATIC(TR_SM, "Disable wmm power save \n");
         if(connected)
         {
            if (!Mlme_Send(Mlme_CreateHICWMMPeriodStartReq, 0, wei_send_cmd))
            {
               DE_TRACE_STATIC(TR_SM, "Failed to create HIC_CTRL_WMM_PS_PERIOD_START_REQ in StateFunction_InitPowerSave\n");
               DE_BUG_ON(1, "");
            }            
         }
         else
         {
            we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL); 
            DE_TRACE_STATIC(TR_SM, "Not connected update registry only \n");
         }
         break;
      }
      case INTSIG_CASE(INTSIG_LEGACY_PS_CONFIGURATION_CHANGED):
         DE_TRACE_STATIC(TR_SM, "Legacy power save re-configured \n");
         /* Not used anymore - use assert to find old users */
         DE_SM_ASSERT(FALSE); 
         break;            

      case MGMT_RX_CASE(MLME_POWER_MGMT_CFM):
         if (Mlme_HandlePowerManagementConfirm((m80211_mlme_power_mgmt_cfm_t *)param->p))
         {  
            DE_TRACE_STATIC(TR_SM, "PS disabled - succees\n");
            nextState_fn = (StateFn_ref)StateFunction_Ps_Disabled_Connected;
         }
         else 
         {
            /* Failed to disable power save */
            DE_TRACE_STATIC(TR_SM, "PS disabled - failed\n");
            DE_SM_ASSERT(FALSE);
         }
         break;

      case MGMT_RX_CASE(NRP_MLME_WMM_PS_PERIOD_START_CFM):
         {           
            uint32_t result;
            if (!Mlme_HandleHICWMMPeriodStartCfm((m80211_nrp_mlme_wmm_ps_period_start_cfm_t *)param->p,&result ))
            {  
               DE_TRACE_STATIC(TR_SM, "Failed to start wmm period\n");
            }
            we_ind_send(WE_IND_PS_WMM_REQ_COMPLETE, NULL);  
         }
         break;   

      default:
         DE_TRACE_INT(TR_SM, "Unhandled message type %x\n",msg);
         DE_SM_ASSERT(FALSE);
   }

   return nextState_fn;
}



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
      INTSIG_DEBUG_CASE(INTSIG_EXECUTE);
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
      INTSIG_DEBUG_CASE(INTSIG_DEVICE_DISCONNECTED );

      CTRL_DEBUG_CASE(HIC_CTRL_WAKEUP_IND);
      CTRL_DEBUG_CASE(HIC_CTRL_SET_ALIGNMENT_REQ);
      CTRL_DEBUG_CASE(HIC_CTRL_SET_ALIGNMENT_CFM);
      CTRL_DEBUG_CASE(HIC_CTRL_HL_SYNC_REQ);
      CTRL_DEBUG_CASE(HIC_CTRL_HL_SYNC_CFM);
      CTRL_DEBUG_CASE(HIC_CTRL_SLEEP_FOREVER_CFM);
      MGMT_RX_DEBUG_CASE(NRP_MLME_WMM_PS_PERIOD_START_REQ);
      MGMT_RX_DEBUG_CASE(NRP_MLME_WMM_PS_PERIOD_START_CFM);

      MGMT_TX_DEBUG_CASE(MLME_POWER_MGMT_REQ);
            
      MGMT_RX_DEBUG_CASE(MLME_POWER_MGMT_CFM);

      default:
         DE_TRACE_INT(TR_SEVERE, "Unknown Signal %x, please update this function!!!!\n" , msg);
         return "unknown";
   }

   #undef INTSIG_DEBUG_CASE
   #undef CTRL_DEBUG_CASE
   #undef MGMT_RX_DEBUG_CASE
   #undef MGMT_TX_DEBUG_CASE

}
#endif /* WIFI_DEBUG_ON */


/** @} */ /* End of hmg_ps group */

/******************************* END OF FILE ********************************/
