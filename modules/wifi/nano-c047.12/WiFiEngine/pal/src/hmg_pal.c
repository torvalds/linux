/* $Id: hmg_pal.c,v 1.279 2008/04/23 10:25:24 ulla Exp $ */

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
/** @defgroup hmg_pal WiFiEngine state machine for PAL.
 *
 * \brief This module implements the state machine for PAL implementing BT3.0 AMP support.
 *
 * @{
 */

#include "driverenv.h"
#include "registry.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "mlme_proxy.h"
#include "macWrapper.h"
#include "hmg_defs.h"
#include "hmg_pal.h"
#include "pal_init.h"
#include "pal_command_parser.h"


/*****************************************************************************
T E M P O R A R Y   T E S T V A R I A B L E S
*****************************************************************************/

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/
#define OBJID_MYSELF    SYSDEF_OBJID_HOST_MANAGER_PAL
			
#define EXECUTE_STATE(_StateFunction)   (StateFn_t)(_StateFunction)(msg, (wei_sm_queue_param_s *)sm_param)
#define INIT_STATE(_StateFunction)      (_StateFunction)(INTSIG_INIT, DUMMY)
#define EXIT_STATE(_StateFunction)      (_StateFunction)(INTSIG_EXIT, DUMMY)


/*****************************************************************************
L O C A L   D A T A T Y P E S
*****************************************************************************/

/*****************************************************************************
L O C A L   F U N C T I O N   P R O T O T Y P E S
*****************************************************************************/
static void HMG_entry_pal(ucos_msg_id_t msg, ucos_msg_param_t param);

static void connected_cb(wi_msg_param_t param, void* priv);
static void disconnecting_cb(wi_msg_param_t param, void* priv);

/*********************/
/* State functions   */
/*********************/
/* Main states */
DECLARE_STATE_FUNCTION(StateFunction_Init);
DECLARE_STATE_FUNCTION(StateFunction_Idle);
DECLARE_STATE_FUNCTION(StateFunction_Connected);
DECLARE_STATE_FUNCTION(StateFunction_NotConnected);

/*maps a enum-value to a name (used for debuging)*/
#ifdef WIFI_DEBUG_ON
static char* HMG_Signal_toToName(int msg);
#endif
char data_packet[100];

/*****************************************************************************
 M O D U L E   V A R I A B L E S
*****************************************************************************/
static StateVariables_t stateVars = { StateFunction_Idle, StateFunction_Idle,
                                      StateFunction_Idle, StateFunction_Idle };
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

/* This function is called from ucos */
void HMG_init_pal(void)
{
   /* Set the default state. */
   stateVars.newState_fn = StateFunction_Init; 
   stateVars.currentState_fn = StateFunction_Idle;
   stateVars.subState_fn = StateFunction_Idle;
   stateVars.newSubState_fn = StateFunction_Idle; 

   ucos_register_object(OBJID_MYSELF,
                        SYSCFG_UCOS_OBJECT_PRIO_LOW,
                        (ucos_object_entry_t)HMG_entry_pal,
                        "HMG Pal");   
}

/* This function is called from ucos */
void HMG_startUp_pal(void)
{

   /* Since there is no previous state, send the init signal explicitly. */
      wei_sm_queue_sig(INTSIG_INIT, OBJID_MYSELF, DUMMY, TRUE);
}


static void HMG_entry_pal(ucos_msg_id_t msg, ucos_msg_param_t param)
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

void HMG_Unplug_pal(void)
{
   stateVars.currentState_fn = StateFunction_Init; 
   stateVars.newState_fn = StateFunction_Init; 
   stateVars.subState_fn = StateFunction_Idle;
   stateVars.newSubState_fn = StateFunction_Idle;
   connected = FALSE; 

   we_ind_deregister_null(&ps_ctx.connected_h);
   we_ind_deregister_null(&ps_ctx.disconnecting_h);
}

/* This function is called directly from WiFiEngine_Initialize() */
void wei_hmg_pal_init(void)
{
   DE_MEMSET(&ps_ctx,0,sizeof(ps_ctx));
   
   HMG_Unplug_pal();
}

/****************************************************************************
** Connected callback
**
** Notification from connection manager that the device is connected to a network
*****************************************************************************/
static void connected_cb(wi_msg_param_t param, void* priv)
{
   	DE_TRACE_STATIC(TR_AMP, "hmg_pal device connected\n");  

   	connected = TRUE;  
#ifdef DEBUG_HMG_PAL   
	printk("CONNECTED\n");
#endif

   	wei_sm_queue_sig(INTSIG_DEVICE_CONNECTED, OBJID_MYSELF, DUMMY, TRUE);
}


/****************************************************************************
** Connected callback
**
** Notification from connection manager that the device is disconnected from a network
*****************************************************************************/
static void disconnecting_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_SM, "hmg_pal device disconnected\n");  
   connected = FALSE;    
   
   wei_sm_queue_sig(INTSIG_DEVICE_DISCONNECTED, OBJID_MYSELF, DUMMY, TRUE);
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
	{
	//printk ("Init Command PArser\n");
	//Init_Command_Parser();
	//nextState_fn = (StateFn_ref)StateFunction_NotConnected;	
	//wei_sm_queue_sig(INTSIG_INIT, OBJID_MYSELF, msg, TRUE);
	}
         break;

      case INTSIG_CASE(INTSIG_EXIT):
         break;

      case INTSIG_CASE(INTSIG_EXECUTE):
         Init_Command_Parser();
		   nextState_fn = (StateFn_ref)StateFunction_NotConnected;	
         break;

      case INTSIG_CASE(INTSIG_POWER_UP):
      {
         int s;
         DE_TRACE_STATIC(TR_SM, "Enter State Init\n");  

         s = we_ind_cond_register(&ps_ctx.connected_h,
                  WE_IND_80211_CONNECTED, "WE_IND_80211_CONNECTED",
                  connected_cb, NULL, 0, NULL);   
         DE_ASSERT(WIFI_ENGINE_SUCCESS == s);

         s = we_ind_cond_register(&ps_ctx.disconnecting_h,
                  WE_IND_80211_DISCONNECTING, "WE_IND_80211_DISCONNECTING",
                  disconnecting_cb, NULL, 0, NULL);  
         DE_ASSERT(WIFI_ENGINE_SUCCESS == s);
 
         if(WIFI_ENGINE_SUCCESS == s)
         {
            wei_sm_queue_sig(INTSIG_EXECUTE, OBJID_MYSELF, DUMMY, TRUE);
         }	 
      }
      break;

      case INTSIG_CASE(INTSIG_DEVICE_CONNECTED):
         nextState_fn = (StateFn_ref)StateFunction_Connected;   
         break;

      case INTSIG_CASE(INTSIG_DEVICE_DISCONNECTED):
         /* Ok to discard */
         break;
         		
	case INTSIG_CASE(INTSIG_DISABLE_PS):
         break;
   
      default:
         DE_TRACE_INT(TR_WARN, "Unhandled PAL message type %x\n",msg);
         DE_TRACE_STATIC2(TR_WARN, "msg %s\n", HMG_Signal_toToName(msg));
         DE_SM_ASSERT(FALSE);
   }

   return (StateFn_ref)nextState_fn;
}

/****************************************************************************
** Substate StateFunction_Connected
**
*****************************************************************************/
DEFINE_STATE_FUNCTION(StateFunction_Connected)
{
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_Connected;
   
   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
		{	
	
		}     
         break;

//For receiving data confirmations		 
/*	case INTSIG_CASE(HIC_AMP_DATA_CFM):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Data confirmation StateFunction_Connected\n"); 
		
		// Notify that data conf is received
		 number_of_completed_data_blocks(Trans id);
         break;
*/		 
//		 
      case INTSIG_CASE(INTSIG_EXIT):
	 DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit State StateFunction_Connected\n"); 
	  //HCI_Disconnect_Physical_Link_Evt((char*)param);
         
         break;
	 case INTSIG_CASE(INTSIG_EVAL_HCI):
        {

			eval_hci_cmd((char*)param);
		}
         break;
		 
      case INTSIG_CASE(INTSIG_DEVICE_DISCONNECTED):
		//Disconnect event
		HCI_Disconnect_Physical_Link_Evt((char*)param);
        nextState_fn = (StateFn_ref)StateFunction_NotConnected;   
         break;
	case INTSIG_CASE(INTSIG_DISABLE_PS):
         	//nextState_fn = (StateFn_ref)StateFunction_Connected;
         break;
      default:
         DE_TRACE_INT(TR_SM, "Unhandled PAL message type %x\n",msg);
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
DEFINE_STATE_FUNCTION(StateFunction_NotConnected)
{   
   StateFn_ref nextState_fn = (StateFn_ref)StateFunction_NotConnected;

   if( (msg != INTSIG_INIT) && (msg != INTSIG_EXIT)) {
      DE_TRACE_STATIC2(TR_SM, "handling msg %s\n", HMG_Signal_toToName(msg));
   }

   switch(msg) {
      case INTSIG_CASE(INTSIG_INIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Enter State StateFunction_NotConnected\n");   
	  
         break;
   
      case INTSIG_CASE(INTSIG_EXIT):
         DE_TRACE_STATIC(TR_SM_HIGH_RES, "Exit State StateFunction_NotConnected\n");
	

  
      break;
		 case INTSIG_CASE(INTSIG_EVAL_HCI):
         {
			
			eval_hci_cmd((char*)param);
		}
         break;

      case INTSIG_CASE(INTSIG_DEVICE_CONNECTED):
		 Physical_Link_Complete_Event(HCI_SUCCESS);
         nextState_fn = (StateFn_ref)StateFunction_Connected;
		 wei_sm_queue_sig(INTSIG_INIT, OBJID_MYSELF, DUMMY, TRUE);
         break;

      case INTSIG_CASE(INTSIG_DISABLE_PS):
         //nextState_fn = (StateFn_ref)StateFunction_Connected;
         break;

      default:
         DE_TRACE_INT(TR_SM, "Unhandled PAL message type %x\n",msg);
         DE_SM_ASSERT(FALSE);
   }

   return nextState_fn;
}




#ifdef WIFI_DEBUG_ON
static char* HMG_Signal_toToName(int msg)
{
   
   #define INTSIG_DEBUG_CASE(name)  case INTSIG_CASE(name): return #name
   #define CTRL_DEBUG_CASE(name)    case CTRL_RX_CASE(name): return #name
   #define MGMT_RX_DEBUG_CASE(name) case MGMT_RX_CASE(name): return #name
   #define MGMT_TX_DEBUG_CASE(name) case MGMT_TX_CASE(name): return #name
  

   switch(msg) {
      INTSIG_DEBUG_CASE(INTSIG_INIT);
      INTSIG_DEBUG_CASE(INTSIG_EXIT);
      INTSIG_DEBUG_CASE(INTSIG_EXECUTE);
      INTSIG_DEBUG_CASE(INTSIG_POWER_UP);
	 // INTSIG_DEBUG(INTSIG_EVAL_HCI);
	  
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


/** @} */ /* End of hmg_pal group */

/******************************* END OF FILE ********************************/
