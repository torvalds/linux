/* $Id: we_ps.c,v 1.95 2008-05-19 16:08:40 ulla Exp $ */
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
This module implements a generic transport layer.

*****************************************************************************/
/** @defgroup generic_transport generic transport layer independent of undelying
 *            platform.
 *
 * @brief The generic transport handels generic transport issues. The main task
 *        is to coordinate interface handling(cmd52) with upper layer.
 *        
 *
 *  @{
 */
#include "driverenv.h"
#include "m80211_stddefs.h"
#include "registry.h"
#include "packer.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "hmg_defs.h"
#include "hmg_traffic.h"
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "pkt_debug.h"
#include "state_trace.h"
#include "we_ind.h"


#define OR_USERS_WITH(_mask) \
do { \
   int users = wifiEngineState.users; \
   wifiEngineState.users |= _mask; \
   if(wifiEngineState.users != users) { \
      DE_TRACE_INT3(TR_PS, "id: %d users: %d=>%d  \n", id, users, wifiEngineState.users); \
   } \
} while(0)

#define SET_OK_TO_SEND(_reason) \
do { if(ok_to_send == FALSE) \
{ \
   ok_to_send = TRUE; \
   DE_TRACE_INT(TR_PS, "id: %d ok_to_send: FALSE=>TRUE" _reason "\n",id); \
}} while(0)


#define UNSET_OK_TO_SEND(_reason) \
do { if(ok_to_send == TRUE) \
{ \
   ok_to_send = FALSE; \
   DE_TRACE_INT(TR_PS, "id: %d ok_to_send: TRUE=>FALSE" _reason "\n",id); \
}} while(0)




typedef enum {
   INTERFACE_PS_DISABLED,
   INTERFACE_PS_OPEN,
   INTERFACE_PS_CLOSED_WAIT,      
   INTERFACE_PS_CLOSED,
   INTERFACE_PS_OPEN_WAIT,
   INTERFACE_PS_NMB_STATES
} interface_state_t; 

static int connected;
static interface_state_t state;
static int ok_to_send;
const char *state_name[] = { 
                             "INTERFACE_PS_DISABLED",
                             "INTERFACE_PS_OPEN",
                             "INTERFACE_PS_CLOSED_WAIT",
                             "INTERFACE_PS_CLOSED",
                             "INTERFACE_PS_OPEN_WAIT"
                           };



/*!
 * @brief Handles traffic timeout
 *
 * @return always 0
 */

static int ps_traffic_timeout_cb(void *data, size_t len)
{  
   rPowerManagementProperties *powerManagementProperties = NULL; 

   powerManagementProperties = (rPowerManagementProperties *)Registry_GetProperty(ID_powerManagement);
   
   DE_TRACE_STATIC(TR_PS, "====> ps_traffic_timeout_cb\n");

   if (state == INTERFACE_PS_DISABLED)
   {
      /*  power save has been disabled */
      WIFI_LOCK();
      WES_CLEAR_FLAG(WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING);  
      WIFI_UNLOCK();
      wei_release_resource_hic(RESOURCE_USER_TX_TRAFFIC_TMO);      
      return 0;
   }
   
   if(WES_TEST_FLAG(WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING))
   {
      WIFI_LOCK();
      WES_CLEAR_FLAG(WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING);  
      WIFI_UNLOCK();
      if(wifiEngineState.ps_queue_cnt == 0 && wifiEngineState.ps_data_ind_received == 0)
      {
         /* No queued data and no data ind received since last timeout */
         WIFI_LOCK();
         if (wifiEngineState.dataReqPending == 0)
         {
            /* No pending data cfm - close data path */
            wifiEngineState.dataPathState = DATA_PATH_CLOSED;  
            WIFI_UNLOCK();
         }
         else
         {
            /* Let datapath continue until all cfm:s has been received */ 
            WIFI_UNLOCK();
         }
         wei_release_resource_hic(RESOURCE_USER_TX_TRAFFIC_TMO);
      }
      else
      {
         DE_TRACE_STATIC(TR_PS, "restart the timer\n");
         if (DriverEnvironment_RegisterTimerCallback(powerManagementProperties->psTxTrafficTimeout, 
                  wifiEngineState.ps_traffic_timeout_timer_id, 
                  ps_traffic_timeout_cb,0) 
               != 1)
         {
             DE_TRACE_STATIC(TR_SEVERE,"No command to callback registered, DE was busy\n");
             /* Not possible to continue - go to sleep */
             WIFI_LOCK();
             wifiEngineState.dataPathState = DATA_PATH_CLOSED; 
             WIFI_UNLOCK();
             wei_release_resource_hic(RESOURCE_USER_TX_TRAFFIC_TMO);
         }
         else
         {
            /* Wait for a new callback */
            WES_SET_FLAG(WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING);
            wifiEngineState.ps_data_ind_received = 0;
            WiFiEngine_PsCheckQueues();
         }
         
      }
   }

   DE_TRACE_STATIC(TR_PS, "<==== ps_traffic_timeout_cb\n");   

   return 0;
}


static bool_t isLegacyPsNoPsPoll(void)
{
   int net_status;

   /* Check if we are using legacy power save no ps poll */
   WiFiEngine_GetNetworkStatus(&net_status); 
   
   if((net_status)&&(!WiFiEngine_LegacyPsPollPowerSave())&&(WiFiEngine_isAssocSupportingWmmPs() != WIFI_ENGINE_SUCCESS))
   {
      return TRUE;
   }

   return FALSE;
}


static void wakeup_ind_cb(wi_msg_param_t param, void* priv)
{  
   wifiEngineState.cmdReplyPending = 0;

   DE_TRACE_STATIC(TR_PS, "Wakeup ind \n"); 

   DE_TRACE_STRING(TR_PS_DEBUG,"State is: %s \n",state_name[state]);
   
   if(state == INTERFACE_PS_OPEN_WAIT)
   {
      DriverEnvironment_enable_target_sleep();      
      state = INTERFACE_PS_OPEN;  
      if(wifiEngineState.users & RESOURCE_DISABLE_PS)
      {  
         /* Power save mechansim for hic interface has been disabled */
         state = INTERFACE_PS_DISABLED; 
         we_ind_send(WE_IND_HIC_PS_INTERFACE_DISABLED, NULL);           
      }
      WiFiEngine_PsCheckQueues();    
   }
    DE_TRACE_STRING(TR_PS_DEBUG,"State is: %s \n",state_name[state]);
}
  
static void connected_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_PS, "Device connected\n");
   connected = TRUE;
  
}
  
static void disconnecting_cb(wi_msg_param_t param, void* priv)
{
   DE_TRACE_STATIC(TR_PS, "Device disconnected\n");  
   connected = FALSE;   
}

static struct interface_ctx_t {
   struct iobject* wakeup_ind_h;
   struct iobject* disconnecting_h;
   struct iobject* connected_h;
} interface_ctx;

static void inf_free_handlers(void)
{
   we_ind_deregister_null(&interface_ctx.wakeup_ind_h);
   we_ind_deregister_null(&interface_ctx.disconnecting_h);
   we_ind_deregister_null(&interface_ctx.connected_h);
}

void wei_interface_unplug(void)
{
   we_ind_deregister_null(&interface_ctx.wakeup_ind_h);
   we_ind_deregister_null(&interface_ctx.disconnecting_h);
   we_ind_deregister_null(&interface_ctx.connected_h);
}

void wei_interface_plug(void)
{
   connected = FALSE;
   ok_to_send = TRUE;
   state = INTERFACE_PS_DISABLED; 

   interface_ctx.wakeup_ind_h = we_ind_register(
            WE_IND_PS_WAKEUP_IND,
            "WE_IND_PS_WAKEUP_IND",
            wakeup_ind_cb,
            NULL,
            0,
            NULL); 
   DE_ASSERT(interface_ctx.wakeup_ind_h != NULL);
   
   interface_ctx.disconnecting_h = we_ind_register(
            WE_IND_80211_DISCONNECTING,
            "WE_IND_80211_DISCONNECTING",
            disconnecting_cb,
            NULL,
            0,
            NULL);
   DE_ASSERT(interface_ctx.disconnecting_h != NULL);
   
   interface_ctx.connected_h = we_ind_register(
            WE_IND_80211_CONNECTED,
            "WE_IND_80211_CONNECTED",
            connected_cb,
            NULL,
            0,
            NULL);
   DE_ASSERT(interface_ctx.connected_h != NULL);

}



/*!
 * @brief Requests to get the hic interface resource.
 *        If power save is disabled this request is
 *        always granted. If power save is enabled
 *        the request is granted if the interface
 *        is OPEN or DISABLED. The interface is OPEN when
 *        hic_ctrl_wakeup_ind is received.
 *
 * @param id Identifies the user that requests the interface.
 *
 * @return true if interface is OPEN or DISABLED else false
 */
int wei_request_resource_hic(int id)
{
   DE_TRACE_STATIC(TR_PS, "===========> wei_request_resource_hic\n"); 
   DE_TRACE_STRING(TR_PS_DEBUG,"State is: %s \n",state_name[state]);

   WIFI_RESOURCE_HIC_LOCK();
   
   if(wifiEngineState.core_dump_state == WEI_CORE_DUMP_ENABLED)
   {
      SET_OK_TO_SEND(" coredump_enabled");
      WIFI_RESOURCE_HIC_UNLOCK();
      return ok_to_send;
   }
   
   /* Or in new user bit */
   OR_USERS_WITH(id);
   
   switch(state)
   {
      case INTERFACE_PS_DISABLED:
         /* Power save handling of hic interface is disbled,
            always ok to send */
         SET_OK_TO_SEND(" power save mechanism for hic interface disabled");
         break;
      
      case INTERFACE_PS_OPEN:
         /* Interface is open ok to send command/data */  
         if(wifiEngineState.users & RESOURCE_DISABLE_PS)
         {  
            /* Power save handling of hic interface has been disabled */
            state = INTERFACE_PS_DISABLED;
            we_ind_send(WE_IND_HIC_PS_INTERFACE_DISABLED, NULL);          
         }
         SET_OK_TO_SEND(" interface is temporary open to send data");
         break;

      case INTERFACE_PS_CLOSED:
         /* Interface is closed and someone requests to use it.
            Notify firmware that we want to use the interface. 
            The interface is ready for use when wakeup_ind_cb() 
            is called
         */  
         UNSET_OK_TO_SEND(" interface ps closed");

         state = INTERFACE_PS_OPEN_WAIT;
         WEI_CMD_TX();
         /* Activate timer to supervise wakeup ind */
         wifiEngineState.cmdReplyPending = 1;
         if(registry.network.basic.cmdTimeout)
           WiFiEngine_CommandTimeoutStart();         

         /* Request to wakeup fw */
         DriverEnvironment_disable_target_sleep();
         break;

      case INTERFACE_PS_OPEN_WAIT:
         UNSET_OK_TO_SEND(" ps transit");
         break;
      case INTERFACE_PS_CLOSED_WAIT:
         UNSET_OK_TO_SEND(" ps transit");
         break;

      default:
         DE_ASSERT(FALSE);
   }
   
   WIFI_RESOURCE_HIC_UNLOCK();
   DE_TRACE_STATIC(TR_PS, "<=========== wei_request_resource_hic\n");    
   return ok_to_send;

}

/*!
 * @brief Requests to release the hic interface resource.
 *
 * @param id Identifies the user that requests the release.
 *
 * @return nothing.
 */
void wei_release_resource_hic(int id)
{
   int users;
   int thisUser;
   DE_TRACE_STATIC(TR_PS, "===========> wei_release_resource_hic\n"); 
   DE_TRACE_STRING(TR_PS_DEBUG,"State is: %s \n",state_name[state]);   
   WIFI_RESOURCE_HIC_LOCK(); 
   
   if((wifiEngineState.users & id) == 0)
   {
      /* nothing to release */
      WIFI_RESOURCE_HIC_UNLOCK();
      return;
   }
   thisUser = id;
   users = wifiEngineState.users;

   wifiEngineState.users &= ~id;

   /* trace this only while ps_enabled==TRUE */
   DE_TRACE_INT3(TR_PS, "id: %d users: %d=>%d \n",id, users,wifiEngineState.users);      

   if((id == RESOURCE_DISABLE_PS)&&(state == INTERFACE_PS_DISABLED))
   {
      /* The resource to disable power save mechansim for hic
         interface is released */
      state = INTERFACE_PS_OPEN; 
      we_ind_send(WE_IND_HIC_PS_INTERFACE_ENABLED, NULL );
   }

   if (wifiEngineState.users != 0)
   {
      /* There is someone that has requested to use the interface*/
      WIFI_RESOURCE_HIC_UNLOCK();
      return;
   }

   /* None is using the interface */
   switch(state)
   {
      case INTERFACE_PS_DISABLED:
      {;
         /* Interface has been disabled now it is time to enable it again */       
         state = INTERFACE_PS_CLOSED_WAIT;
         /* Inform firmware that ps hic interface is not used anymore */
         WiFiEngine_PsSendInterface_Down();
         state = INTERFACE_PS_CLOSED;  
      }
      break; 

      case INTERFACE_PS_OPEN:
      {
         rPowerManagementProperties *powerManagementProperties = NULL; 
         powerManagementProperties = (rPowerManagementProperties *)Registry_GetProperty(ID_powerManagement);
         
         /* Interface is temporary open - close it */   
         if(isLegacyPsNoPsPoll() && (thisUser == RESOURCE_USER_DATA_PATH) && (powerManagementProperties->psTxTrafficTimeout > 0))

         {
            /* Start a tx traffic timeout that will hold firware
               awake if more data is about to be sent */
            if(WES_TEST_FLAG(WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING))
            {
               /* Cancel the timer so we can restart it */
               DriverEnvironment_CancelTimer(wifiEngineState.ps_traffic_timeout_timer_id);
               WES_CLEAR_FLAG(WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING);
            }

            WIFI_RESOURCE_HIC_UNLOCK();
            wei_request_resource_hic(RESOURCE_USER_TX_TRAFFIC_TMO);
            WIFI_RESOURCE_HIC_LOCK();            
            
            if (DriverEnvironment_RegisterTimerCallback(powerManagementProperties->psTxTrafficTimeout, 
                     wifiEngineState.ps_traffic_timeout_timer_id,ps_traffic_timeout_cb,0) != 1)
            {
                DE_TRACE_STATIC(TR_SEVERE,"Failed to register timer callback\n");
                /* Skip timer and go to sleep */
                wifiEngineState.users = 0;
                wifiEngineState.dataPathState = DATA_PATH_CLOSED;
                state = INTERFACE_PS_CLOSED_WAIT;
                /* Inform firmware that ps hic interface is not used anymore */
                WiFiEngine_PsSendInterface_Down();
                state = INTERFACE_PS_CLOSED; 
            }
            else
            {
               WES_SET_FLAG(WES_FLAG_PS_TRAFFIC_TIMEOUT_RUNNING);
            }          

         }
         else
         { 
            wifiEngineState.dataPathState = DATA_PATH_CLOSED;
            state = INTERFACE_PS_CLOSED_WAIT;
            /* Inform firmware that ps hic interface is not used anymore */
            WiFiEngine_PsSendInterface_Down();
            state = INTERFACE_PS_CLOSED;
         }
      }      
      break;

      case INTERFACE_PS_CLOSED:
         DE_TRACE_STATIC(TR_PS, "State is closed no action\n"); 
      break;

      case INTERFACE_PS_OPEN_WAIT:
         DE_TRACE_STATIC(TR_PS, "Transit to OPEN state no action\n"); 
      break;         
      case INTERFACE_PS_CLOSED_WAIT:
         DE_TRACE_STATIC(TR_PS, "Transit to CLOSE state no action\n");          
      break;

      default:
         DE_TRACE_INT(TR_PS,"Unknown state is: %d \n",state);
         DE_ASSERT(FALSE);
      break;

   }

   WIFI_RESOURCE_HIC_UNLOCK();
   DE_TRACE_STRING(TR_PS_DEBUG,"State is: %s \n",state_name[state]);
   DE_TRACE_STATIC(TR_PS, "<=========== wei_release_resource_hic\n");    
}


/*!
 * @brief Check if any data or command has been queued.
 *
 *
 * @return Nothing
 */
void WiFiEngine_PsCheckQueues(void)
{
#if (DE_CCX == CFG_INCLUDED)
    if(connected)
    {
        DE_TRACE_STATIC(TR_PS, "NIKS: ccx_handle_wakeup called from wakeup_ind->WiFiEngine_PsCheckQueues\n");
        ccx_handle_wakeup();
    }
#endif

   if((wifiEngineState.ps_queue_cnt == 0) && QUEUE_EMPTY(&cmd_queue))
   {
      /* No command or data queued */
      DE_TRACE_STATIC(TR_PS, "Data and command queue empty\n");
   }
   else
   {
      if(wifiEngineState.ps_queue_cnt > 0)
      {
         DE_TRACE_STATIC(TR_PS, "wifiEngineState.ps_queue_cnt > 0\n");
         if(connected)
         {       
            wifiEngineState.dataPathState = DATA_PATH_OPENED; 
            DriverEnvironment_handle_driver_wakeup();       
         }
         else
         {
            DE_TRACE_STATIC(TR_PS, "WifiEngine main state not driverConnected ignore queue count\n");
            DE_TRACE_INT(TR_PS, "WifiEngine main state: %d \n", wifiEngineState.main_state );
         }
      }     
      if(!QUEUE_EMPTY(&cmd_queue))
      {
         DE_TRACE_STATIC(TR_PS, "!QUEUE_EMPTY(&cmd_queue)\n");
         /* Send a queued cmd if one is ready */
         wei_send_cmd_raw(NULL, 0);
      }     
   }
}

/*!
 * @brief Indicate length of IP stack TX queue.
 *
 * This is a number indicating how many packets are waiting to be
 * transmitted. Used for WMM PS.
 */
void WiFiEngine_IndicateTXQueueLength(uint16_t length)
{
   wifiEngineState.ps_queue_cnt = length;
}


/*!
 * @brief Sends end of service period to target.
 *        This message indicates that no more messages
 *        will be sent until next service period.
 *
 * @param ctx The message context describing the message to send.
 * @return WIFI_ENGINE_SUCCESS
 */
void WiFiEngine_PsSendInterface_Down(void)
{
   if (!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) return;
   
   DE_TRACE_STATIC(TR_PS_DEBUG, "====> WiFiEngine_PsSendInterface_Down\n");  

   if (!Mlme_Send(Mlme_CreateHICInterfaceDown, 0, wei_unconditional_send_cmd))
   {
      DE_TRACE_STATIC(TR_WARN, "Failed to create hic interface down\n");
      DE_ASSERT(FALSE);
   } 

   DE_TRACE_STATIC(TR_PS_DEBUG, "<==== WiFiEngine_PsSendInterface_Down\n");  
}


bool_t WiFiEngine_IsReasonHost(hic_ctrl_wakeup_ind_t *ind)
{
   if(ind->reasons == HIC_CTRL_WAKEUP_IND_HOST)
   {
      return TRUE;
   }
   
   return FALSE;   
}

bool_t WiFiEngine_IsInterfaceDown(char* cmd)
{
   uint8_t type;
   uint8_t id;
   char* p;

   p = cmd;
   p += 2;
   type = (uint8_t)*p;
   p++;
   id = (uint8_t)*p;
   
   
   if(type == HIC_MESSAGE_TYPE_CTRL)
   {
      if(id == HIC_CTRL_INTERFACE_DOWN)
      {
         return TRUE;
      }
   }
   
   return FALSE;   
}

void wei_interface_init(void)
{
   DE_MEMSET(&interface_ctx,0,sizeof(interface_ctx));
}

void wei_interface_shutdown(void)
{
   inf_free_handlers();
}

