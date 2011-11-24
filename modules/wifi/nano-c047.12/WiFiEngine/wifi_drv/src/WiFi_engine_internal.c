
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

/** @defgroup wifi_engine_internal WiFiEngine internal support functions
 *
 * @brief This module implements the support functions internal to WiFiEngine.
 *
 *  @{
 */

#include "driverenv.h"
#include "ucos.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "registry.h"
#include "registryAccess.h"
#include "packer.h"
#include "mlme_proxy.h"
#include "hmg_defs.h"
#include "pkt_debug.h"
#include "wifi_engine_internal.h"

WiFiEngineState_t wifiEngineState;

struct signal_container
{
   ucos_msg_id_t     msg;
   SYSDEF_ObjectType dest;
   ucos_msg_param_t  param;
   WEI_TQ_ENTRY(signal_container) next;
};

static WEI_TQ_HEAD(, signal_container) sig_queue = 
   WEI_TQ_HEAD_INITIALIZER(sig_queue);

struct queue_s cmd_queue; /* Queue of unsent commands */

#define chk_rsn_ie(x) ((x) && ((x)->hdr.id != 0xFF))
#define chk_wpa_ie(x) ((x) && ((x)->hdr.hdr.id != 0xFF))

void init_queue(struct queue_s *q) 
{
   q->head = q->tail = 0;
}

int enqueue(struct queue_s *q, void *el, int s) 
{
   if (QUEUE_FULL(q)) 
      return 0;
   q->v[q->head] = el;
   q->s[q->head] = s;
   q->head = ((q->head + 1) % QUEUE_SIZE);
   return 1;
}

void *dequeue(struct queue_s *q, int* s) 
{
   void *el;
   
   if (QUEUE_EMPTY(q))
      return NULL;
   el = q->v[q->tail];
   *s = q->s[q->tail];
   q->v[q->tail] = NULL; /* Ease debugging */
   q->tail = (q->tail + 1) % QUEUE_SIZE;
   
   return el;
}

static void dump_queue(struct queue_s *q)
{
   int i;
   int count = 0;
   int last = (q->head + QUEUE_SIZE - 1) % QUEUE_SIZE;
   
   for (i = 0; i < QUEUE_SIZE; i++)
   {
      if(q->v[i] != NULL)
         count++;
   }
   
   /* tail is really first message on queue */
   DE_TRACE_INT5(TR_WEI, "Command queue: tail %d (" TR_FPTR "), head %d (" TR_FPTR "), count %d\n", 
                 (int)q->tail, TR_APTR(q->v[q->tail]), 
                 last, TR_APTR(q->v[last]), 
                 count);
   (void) last; /* Just mute compiler warning in case DE_TRACE_INT5 does nothing */
}

/* Doubly linked list */

/****/

char* wei_printSSID(m80211_ie_ssid_t *ssid, char *str, size_t len)
{
   size_t l;

   if(len <= ssid->hdr.len)
      l = len - 1;
   else
      l = ssid->hdr.len;
   
   DE_STRNCPY(str, ssid->ssid, l);
   str[l] = '\0';
   return str;
}

char* wei_print_mac(m80211_mac_addr_t *mac_addr, char *str, size_t len)
{
#if M80211_ADDRESS_SIZE != 6
#error fixme
#endif
   DE_SNPRINTF(str, len, "%02x-%02x-%02x-%02x-%02x-%02x", 
               (unsigned char)mac_addr->octet[0],
               (unsigned char)mac_addr->octet[1],
               (unsigned char)mac_addr->octet[2],
               (unsigned char)mac_addr->octet[3],
               (unsigned char)mac_addr->octet[4],
               (unsigned char)mac_addr->octet[5]);
   return str;
}

void wei_clear_cmd_queue()
{
   void *p;
   int l;

   while ( (p = dequeue(&cmd_queue, &l)) != NULL)
   {
      DriverEnvironment_TX_Free(p);
   }
}

/*!
 * @brief Checks if any ac is set to 1
 *
 * @return TRUE if any AC is set, else FALSE
 */
bool_t wei_is_any_ac_set(void)
{
   rBasicWiFiProperties *properties;

   properties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);

   return ((properties->QoSInfoElements.ac_be)||(properties->QoSInfoElements.ac_bk)||
      
           (properties->QoSInfoElements.ac_vi)||(properties->QoSInfoElements.ac_vo));
           
}



/*!
 * @brief Checks if hmg is running i auto mode
 *
 * @return TRUE if auto mode is set, else FALSE 
 */
bool_t wei_is_hmg_auto_mode(void)
{
   rHostDriverProperties *properties;

   properties = (rHostDriverProperties *)Registry_GetProperty(ID_hostDriver);

   return (properties->hmgAutoMode);
           
}

/*!
 * @brief Checks if wmm power is is enabled.
 *
 * @return TRUE if WMM power save is enabled, else FALSE 
 */
bool_t wei_is_wmm_ps_enabled(void)
{
   rBasicWiFiProperties *properties;  

   properties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);

   if((properties->enableWMMPs)&&(wei_is_any_ac_set()))
   {
      return TRUE;
   }

   return FALSE;
}


/*!
 * @brief In WMM different access categories (ac:s) are mapped
 *        to different priority. This functions returns an ac
 *        for a defined priority (defined by standard).
 *
 * @param prio 802.11D priority value to operate on.
 * @return the access category for prio
 */
uint16_t wei_qos_get_ac_value(uint16_t prio)
{
   rBasicWiFiProperties *properties;
   uint16_t ac;

   properties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   
   switch(prio)
   {
      case 1:
      case 2:
      {
         ac = properties->QoSInfoElements.ac_bk;
      }
      break;
      case 0:
      case 3:
      {
         ac = properties->QoSInfoElements.ac_be;
      }
      break;

      case 4:
      case 5:
      {  
         ac = properties->QoSInfoElements.ac_vi;
      }
      break;

      case 6:
      case 7:
      {
         ac = properties->QoSInfoElements.ac_vo;
      }
      break;

      default:
         DE_TRACE_STATIC(TR_SEVERE, "Illegal Qos priority\n");
         ac = 0;
         break;
   }

   return ac;
}
/*
 * Does the network status allow an association to start?
 * @param status Network status to evaluate
 * @return 1 if associated or if association is ongoing,
 * 0 otherwise.
 */
int wei_network_status_busy() 
{
   return (1 && wei_netlist_get_current_net());
}

/*!
 * \brief Send a command to the hardware device
 * 
 * This function is called internally by WiFiEngine.  It sends the
 * current command on to the bus driver send function.  The context
 * that is passed to this function should be allocated with
 * Mlme_CreateMessageContext() and initialized by Mlme_Create* and can
 * be freed immediately after this function returns (regardless of
 * return value).
 * @param ctx The message context describing the message to send.
 * @return WIFI_ENGINE_SUCCESS
 * WIFI_ENGINE_FAILURE_DEFER if the send queue is full,
 * try the send again later.
 * WIFI_ENGINE_FAILURE if the send failed (such as when no hardware is present).
 */
int wei_send_cmd(hic_message_context_t *ctx) 
{
   char *cmd;
   int size;

   DE_TRACE_STACK_USAGE;

   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) 
   {
      return WIFI_ENGINE_FAILURE_DEFER;
   }


   /* Silently discard - returning failure will cause
      DE_BUG_ON in some cases */
   if(WiFiEngine_isCoredumpEnabled())
   {
      return WIFI_ENGINE_SUCCESS;
   } 


   if (! packer_HIC_Pack(ctx))
   {
      return WIFI_ENGINE_FAILURE;
   }
   cmd = ctx->packed;
   size = ctx->packed_size;
   ctx->packed = NULL;

   return wei_send_cmd_raw(cmd, size);

}

/*!
 * \brief Unconditionally send a command to the hardware device
 * 
 * This function is called internally by WiFiEngine.  It sends the
 * current command on to the bus driver send function regardless of
 * the sleep state of the device. The command is not queued.  The
 * context that is passed to this function should be allocated with
 * Mlme_CreateMessageContext() and initialized by Mlme_Create* and can
 * be freed immediately after this function returns (regardless of
 * return value).
 * @param ctx The message context describing the message to send.
 * @return WIFI_ENGINE_SUCCESS or WIFI_ENGINE_FAILURE.
 */
int wei_unconditional_send_cmd(hic_message_context_t *ctx) 
{
   char *cmd;
   int size;
   DE_TRACE_STATIC(TR_CMD, "============> wei_unconditional_send_cmd\n");
   
   if (! packer_HIC_Pack(ctx))
   {
      /* Silent discard */
      return WIFI_ENGINE_SUCCESS;
   }
   
   cmd = ctx->packed;
   size = ctx->packed_size;
   ctx->packed = NULL;


#ifdef USE_IF_REINIT
   WEI_ACTIVITY();
#endif
   if(!(ctx->msg_id & MAC_API_PRIMITIVE_TYPE_RSP))
   {   
       wifiEngineState.cmdReplyPending = 1;
   }
   
   if (DriverEnvironment_HIC_Send(cmd, size)
       != DRIVERENVIRONMENT_SUCCESS) 
   {
      wifiEngineState.cmdReplyPending = 0;      
      return WIFI_ENGINE_FAILURE;
   }
/*   DE_TRACE_INT2("Sent cmd %d [%d]\n", (int)cmd[3], size); */

   WEI_CMD_TX();
   DE_TRACE_STATIC(TR_CMD, "<============ wei_unconditional_send_cmd\n");
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * \brief Send a command to the hardware device
 * 
 * This function is called internally by WiFiEngine.
 * It sends the current command on to the bus driver send function.
 * The buffer that is passed to this function should be allocated
 * with DriverEnvironment_TX_Alloc() and will be freed by the
 * bus driver send function then the transmission is complete
 * (if wei_send_cmd() returns WIFI_ENGINE_SUCCESS).
 * The command is queued in cmd_queue
 * @param cmd The command to send.
 * @param size The length of the cmd in bytes.
 * @return 
 * - WIFI_ENGINE_SUCCESS
 * - WIFI_ENGINE_FAILURE_DEFER if the queue is full, try the send again later.
 */
int wei_send_cmd_raw(char *cmd, int size) 
{  
   DE_TRACE_STATIC(TR_CMD_DEBUG, "============> wei_send_cmd_raw\n");
   
   DE_TRACE_STACK_USAGE;

   WIFI_LOCK();
   if (cmd) 
   {
      if (!enqueue(&cmd_queue, cmd, size)) 
      {
         DE_TRACE_STATIC(TR_SEVERE, "Cmd queue full\n");
         WIFI_UNLOCK();         
         if(!wifiEngineState.forceRestart)
         {
            /* This will force a restart */
            wifiEngineState.forceRestart = TRUE;
           if(registry.network.basic.cmdTimeout)
             WiFiEngine_CommandTimeoutStart();
         }

         return WIFI_ENGINE_FAILURE_DEFER;
      }
      DE_TRACE3(TR_WEI, "Queued cmd (SendCmdRaw) %p when cmdReplyPending is %d\n", 
      cmd,
      wifiEngineState.cmdReplyPending);
      dump_queue(&cmd_queue);
   }
 
   if (wifiEngineState.cmdReplyPending)
   {
      /* very chatty */
      /* can this be traced in some other way? */
      DE_TRACE_INT(TR_CMD, "cmdReplyPending: %d\n",wifiEngineState.cmdReplyPending);
      WIFI_UNLOCK();
      return WIFI_ENGINE_SUCCESS;
   }

   if (DriverEnvironment_acquire_trylock(&wifiEngineState.cmd_lock))
   {
      WIFI_UNLOCK();
      DE_TRACE_STATIC(TR_SEVERE, "Cmd path trylock locked - just queue command\n");
      return WIFI_ENGINE_SUCCESS;
   }
   WIFI_UNLOCK();

   if(cmd != NULL || !QUEUE_EMPTY(&cmd_queue))
   {
      if(!wei_request_resource_hic(RESOURCE_USER_CMD_PATH))
      {
         DE_TRACE_STATIC(TR_CMD, "Fw is sleeping - wait for wakeup indication\n");
         WIFI_LOCK();
         /* If wakeup indication has been received we can continue */
         if(!wei_request_resource_hic(RESOURCE_USER_CMD_PATH))
         {
            /* Wait for wakeup indication */
            DriverEnvironment_release_trylock(&wifiEngineState.cmd_lock);           
            WIFI_UNLOCK();
            return WIFI_ENGINE_SUCCESS;
         }
         WIFI_UNLOCK();
      }
   }

   
   cmd = (char*)dequeue(&cmd_queue, &size);
   
   if ((cmd == NULL))
   {
      /* Last command in queue - release resource */       
      if(!WiFiEngine_isCoredumpEnabled())
      {
         DE_TRACE_STATIC(TR_CMD, "Command queue empty\n");
         wei_release_resource_hic(RESOURCE_USER_CMD_PATH);  
      }
      
      DriverEnvironment_release_trylock(&wifiEngineState.cmd_lock);
      /* There is no confirm on hic_ctrl_interface_down
      as a consequence command queue will not be scheduled. */

      /* Check if any new commands has been queued */
      if(!QUEUE_EMPTY(&cmd_queue))
      {
         wei_request_resource_hic(RESOURCE_USER_CMD_PATH);
      }
      
      return WIFI_ENGINE_SUCCESS;
   }

   /* This needs to be set _before_ sending, or we may be interrupted
    * by a receive before the cmdReplyPending flag is set */
#ifdef USE_IF_REINIT
   WEI_ACTIVITY();
#endif
   WEI_CMD_TX();
   wifiEngineState.cmdReplyPending = 1; 
   DriverEnvironment_release_trylock(&wifiEngineState.cmd_lock);
   DE_TRACE_PTR(TR_CMD, "Sending cmd %p\n", cmd); 
   if(!WiFiEngine_isCoredumpEnabled()) {
      wifiEngineState.last_sent_msg_type = HIC_MESSAGE_TYPE(cmd);
      wifiEngineState.last_sent_msg_id = HIC_MESSAGE_ID(cmd);
   }
   if (DriverEnvironment_HIC_Send(cmd, size)
       != DRIVERENVIRONMENT_SUCCESS) 
   {
      wifiEngineState.cmdReplyPending = 0;
      DE_TRACE_STATIC(TR_SEVERE, "Setting cmdReplyPending to 0\n");

//    DriverEnvironment_release_trylock(&wifiEngineState.cmd_lock);
      return WIFI_ENGINE_FAILURE_DEFER;
   }


// DriverEnvironment_release_trylock(&wifiEngineState.cmd_lock);    
   if(registry.network.basic.cmdTimeout)
      WiFiEngine_CommandTimeoutStart();
   DE_TRACE_STATIC(TR_CMD_DEBUG, "<============ wei_send_cmd_raw\n");
   
   return WIFI_ENGINE_SUCCESS;
}

#if 0
/** Blocking send **/
/*! Struct for asynchronous MIB queries.
 */
struct cmd_cb_info
{
      void *data;
      size_t data_len;
      int status;
      de_event_t cmd_cb_gate;
};

int blocking_cmd_send_callback(we_cb_container_t *cbc)
{
   struct cmd_cb_info *ci;
   
   if ( WIFI_ENGINE_FAILURE_ABORT == cbc->status )
   {
      DE_TRACE_STATIC(TR_WEI, "CMD Send callback received ABORT status\n");
   } else
   {
      DE_TRACE_INT(TR_WEI, "got status %d\n", cbc->status);
   }
   if ( cbc->data )
   {
      DBG_PRINTBUF("blocking_cmd_send_callback() got data ", (unsigned char *)cbc->data, cbc->data_len);
      if ( cbc->ctx && cbc->ctx_len == sizeof *ci )
      {
         ci = (struct cmd_cb_info *)cbc->ctx;
         if ( cbc->ctx_len >= cbc->data_len )
         {
            DE_MEMCPY(ci->data, cbc->data, cbc->data_len);
            ci->data_len = cbc->data_len;
            ci->status = WIFI_ENGINE_SUCCESS;
         } else
         {
            DE_TRACE_STATIC(TR_SEVERE, "Input buffer to small. Failed to copy args.\n");
            ci->status = WIFI_ENGINE_FAILURE_INVALID_LENGTH;
         }
         DriverEnvironment_SignalEvent(&ci->cmd_cb_gate);
      } else
      {
         DE_TRACE_STATIC(TR_NOISE, "No context \n");
      }
   }
   
   return 1;
}

/*!
 * \brief Send a command to the hardware device and return when the
 *        result has arrives.
 * 
 * This function is called internally by WiFiEngine.  It sends the
 * current command on to the bus driver send function.  The context
 * that is passed to this function should be allocated with
 * Mlme_CreateMessageContext() and initialized by Mlme_Create* and can
 * be freed immediately after this function returns (regardless of
 * return value). The call blocks until the answer arrives.
 * @param ctx The message context describing the message to send.
 * @param rsp The return message. Free/allocate FIXME.
 * @return WIFI_ENGINE_SUCCESS
 * WIFI_ENGINE_FAILURE_DEFER if the send queue is full,
 * try the send again later.
 * WIFI_ENGINE_FAILURE if the send failed (such as when no hardware is present).
 */
int wei_send_cmd_blocking(hic_message_context_t *ctx, hic_message_context_t *rsp) 
{
   char *cmd;
   int size;

   DE_TRACE_STACK_USAGE;

   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) 
   {
      return WIFI_ENGINE_FAILURE_DEFER;
   }

   if (! packer_HIC_Pack(ctx))
   {
      return WIFI_ENGINE_FAILURE;
   }
   cmd = ctx->packed;
   size = ctx->packed_size;
   ctx->packed = NULL;

   return wei_send_cmd_raw(cmd, size);

}
#endif

/*!
 * Check if command is a indication or not.
 */
int wei_is_cmd_ind(int msgType, int msgId)
{
   switch (msgType)
   {
      case HIC_MESSAGE_TYPE_CTRL:
      {
         switch(msgId)
         {
            case HIC_CTRL_WAKEUP_IND:
               return 1;
            default:
               return 0;
         }
      }

      case HIC_MESSAGE_TYPE_MGMT:
      {
         switch(msgId)
         {
            case MLME_AUTHENTICATE_IND:
               return 1;
            case MLME_DEAUTHENTICATE_IND:
               return 1;
            case MLME_ASSOCIATE_IND:
               return 1;
            case MLME_REASSOCIATE_IND:
               return 1;
            case MLME_DISASSOCIATE_IND:
               return 1;
            case MLME_SCAN_IND:
               return 1;
            default:
               return 0;
         }
      }

      case HIC_MESSAGE_TYPE_CONSOLE:
      {
         switch(msgId)
         {
            case HIC_MAC_CONSOLE_IND:
               return 1;
            default:
               return 0;
         }
      }

      case HIC_MESSAGE_TYPE_CUSTOM:
      {
         switch(msgId)
         {
            case NRP_MLME_PEER_STATUS_IND:
               return 1;
            default:
               return 0;
         }
      }

      default:
         return 0;
   }
}


/*!
 * Check if net is joinable.
 * A net is joinable if the SSID matches the desiredSSID (from the registry),
 * if desiredSSID is set, AND the desiredBSSID (from the registry), if desiredBSSID
 * is set.
 * @param net Net descriptor to check.
 * @return 1 if net is joinable. 0 otherwise.
 */
int wei_is_net_joinable(WiFiEngine_net_t *net)
{
   if (registry.network.basic.desiredSSID.hdr.id == M80211_IE_ID_NOT_USED)
   {
      /* No SSID, go on BSSID */
      if (wei_equal_bssid(net->bssId_AP, registry.network.basic.desiredBSSID))
      {
         return 1;
      }
      return 0;
   }
   else
   {
      if (wei_desired_ssid(net->bss_p->bss_description_p->ie.ssid))
      {
         /* SSID valid, check bssid */
         /* BCAST BSSID means BSSID not set */
         if (wei_is_bssid_bcast(registry.network.basic.desiredBSSID))
         {
            return 1;
         }
         /* BSSID present, validate that too */
         if (wei_equal_bssid(net->bssId_AP, registry.network.basic.desiredBSSID))
         {
            return 1;
         }
         /* BSSID present but not matching */
         return 0;
      }
      /* SSID present but not matching */
   }
   return 0;
}

/*!
 * Compare ssid1 with ssid2. Return 1 if they are equal, 0 otherwise.
 */
int wei_equal_ssid(m80211_ie_ssid_t ssid1, m80211_ie_ssid_t ssid2)
{
   DE_TRACE_STACK_USAGE;
   
   if ( (ssid2.hdr.id  != ssid1.hdr.id)
     || (ssid2.hdr.len != ssid1.hdr.len) )
   {
      return 0;
   }
   return (DE_MEMCMP(ssid1.ssid, ssid2.ssid, ssid1.hdr.len) == 0);
}

/*!
 * Compare ssid with the desired ssid. Return 1 if they are equal, 0 otherwise.
 */
int wei_desired_ssid(m80211_ie_ssid_t ssid)
{
   rBasicWiFiProperties *basic;
   
   basic = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  

   return wei_equal_ssid(ssid, basic->desiredSSID);
}

/*!
 * Compare bssid1 with bssid2. Return 1 if they are equal, 0 otherwise.
 */
int wei_equal_bssid(m80211_mac_addr_t bssid1, m80211_mac_addr_t bssid2)
{
   return (DE_MEMCMP(bssid1.octet, bssid2.octet, sizeof bssid1.octet) == 0);
}

int wei_is_bssid_bcast(m80211_mac_addr_t bssid)
{
   m80211_mac_addr_t         bcast_bssid;
   int i;

   for (i = 0; i < M80211_ADDRESS_SIZE; i++)
   {
      bcast_bssid.octet[i] = (char)0xFF;
   }

   return wei_equal_bssid(bssid, bcast_bssid);
}

void wei_copy_bssid(m80211_mac_addr_t *dst, m80211_mac_addr_t *src)
{
   DE_MEMCPY(dst->octet, src->octet, sizeof(dst->octet));
}

void wei_weinet2wenet(WiFiEngine_net_t *dst, WiFiEngine_net_t *src)
{
   DE_MEMCPY(dst, src, sizeof *dst);
   dst->ref_cnt = 0;
}

WiFiEngine_Encryption_t
wei_cipher_suite2encryption(m80211_cipher_suite_t suite)
{
   switch(suite) {
      case M80211_CIPHER_SUITE_WEP40:
      case M80211_CIPHER_SUITE_WEP104:
      case M80211_CIPHER_SUITE_WEP:
         return Encryption_WEP;
      case M80211_CIPHER_SUITE_TKIP:
         return Encryption_TKIP;
      case M80211_CIPHER_SUITE_CCMP:
         return Encryption_CCMP;
      case M80211_CIPHER_SUITE_WPI: 
	return Encryption_SMS4;
      case M80211_CIPHER_SUITE_NONE:
         return Encryption_Disabled;
      default:
         DE_TRACE_INT(TR_SEVERE, "got unknown cipher suite: %d\n", suite);
         return Encryption_Disabled;
   }
}

m80211_cipher_suite_t wei_cipher_encryption2suite(WiFiEngine_Encryption_t *sel)
{
   switch (*sel)
   {
      case Encryption_Disabled:
         return M80211_CIPHER_SUITE_NONE;
      case Encryption_WEP:
         /* Target is using key size to check if WEP40 or WEP104 is to be used */
         return M80211_CIPHER_SUITE_WEP104;
      case Encryption_TKIP:
         return M80211_CIPHER_SUITE_TKIP;
      case Encryption_CCMP:
         return M80211_CIPHER_SUITE_CCMP;
      case Encryption_SMS4:
	return M80211_CIPHER_SUITE_WPI;
      default:
         return M80211_CIPHER_SUITE_NONE;
   }
}

WiFiEngine_Auth_t wei_akm_suite2auth(WiFiEngine_Encryption_t enc, m80211_akm_suite_t *suite)
{
   if (enc == Encryption_WEP)
   {
      return Authentication_Shared;
   }
   if (enc == Encryption_TKIP)
   {
      if (*suite == M80211_AKM_SUITE_PSK)
      {
         return Authentication_WPA_PSK;
      }
      return Authentication_WPA;
   }
   if (enc == Encryption_CCMP)
   {
      if (*suite == M80211_AKM_SUITE_PSK)
      {
         return Authentication_WPA_2_PSK;
      }
      return Authentication_WPA_2;
   }
   if (enc == Encryption_SMS4)
     {
       if (*suite == M80211_AKM_SUITE_PSK)
	 {
	   return Authentication_WAPI_PSK;
	 }
       return Authentication_WAPI;
     }
   DE_TRACE4(TR_SEVERE, "%s got unknown cipher suite. enc: %d, suite: %d\n", "wei_akm_suite2auth", enc, *suite);

   return Authentication_Open;
}

int wei_is_encryption_mode_denied(WiFiEngine_Encryption_t encType)
{
   switch (wifiEngineState.config.encryptionLimit)
   {
      case Encryption_Disabled :
         if (encType != Encryption_Disabled)
         {
            return WIFI_NET_FILTER_ENC_NOT_NONE;
         }
         break;
      case Encryption_WEP :
         if (encType > Encryption_WEP)
         {
            return WIFI_NET_FILTER_ENC_WEP_OR_LESS;
         }
         break;
      case Encryption_TKIP :
         if (encType > Encryption_TKIP)
         {
            return WIFI_NET_FILTER_ENC_TKIP_OR_LESS;
         }
         break;
      case Encryption_CCMP:
         if (encType > Encryption_CCMP)
         {
            return WIFI_NET_FILTER_ENC_CCMP_OR_LESS;
         }
         break;
      case Encryption_SMS4:
	if (encType > Encryption_SMS4)
         {
            return WIFI_NET_FILTER_ENC_SMS4_OR_LESS;
         }
	break;
   }
   
   return FALSE;
}

/*!
 * Is this encryption mode enabled/configured in the driver registry?
 * Decision matrix :
 * Registry mode : allowed mode
 *      Disabled : Disabled
 *           WEP : Disabled/WEP
 *           WPA : Disabled/WEP/WPA
 *          WPA2 : Disabled/WEP/WPA/WPA2
 */
int wei_encryption_mode_allowed(WiFiEngine_Encryption_t encType)
{
   return !wei_is_encryption_mode_denied(encType);
}

/*!
 * Decide if the net is compatible with the current authentication mode.
 * Return the AKM suite (if applicable) and the source type for the suite.
 *
 * @param akm The AKM suite that matches the current authentication mode.
 *            The value of this parameter is only valid if the type 
 *            parameter is different from WEI_IE_TYPE_CAP.
 * @param type The IE/CAP type that matches the current authentication mode.
 * @param net The net that have the right authentication mode
 * @return WIFI_ENGINE_SUCCESS if the net is compatible with the current
 *         authentication mode.
 *         WIFI_ENGINE_FAILURE if the net isn't compatible with the current
 *         authentication mode.
 */
int  wei_filter_net_by_authentication(m80211_akm_suite_t *akm,
                                      wei_ie_type_t *type,
                                      WiFiEngine_net_t **net)
{

   m80211_ie_rsn_parameter_set_t    *rsn_ie = NULL;
   m80211_ie_wpa_parameter_set_t    *wpa_ie = NULL;
   int                              i;
#if (DE_CCX == CFG_INCLUDED)
   BOOLEAN FoundAuthType = FALSE;
#endif

   if ( (*net == NULL)
     || ((*net)->bss_p == NULL)
     || ((*net)->bss_p->bss_description_p == NULL)
      )
   {
      return WIFI_ENGINE_FAILURE;
   }
   
   rsn_ie = &(*net)->bss_p->bss_description_p->ie.rsn_parameter_set;
   wpa_ie = &(*net)->bss_p->bss_description_p->ie.wpa_parameter_set;
   
   /* Get the current suite */
   switch(wifiEngineState.config.authenticationMode)
   {
      case Authentication_Open:
      case Authentication_Autoselect:
      {
         *type = WEI_IE_TYPE_CAP;
         return WIFI_ENGINE_SUCCESS;
      }
      
      case Authentication_Shared:
      {
         if ((*net)->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_PRIVACY)
         {
            *type = WEI_IE_TYPE_CAP;
            return WIFI_ENGINE_SUCCESS;
         }
      }
      break;
      case Authentication_8021X:
      {
            *type = WEI_IE_TYPE_CAP;
            return WIFI_ENGINE_SUCCESS;
      }

      case Authentication_WPA:
      {
             if (chk_wpa_ie(wpa_ie))
             {
                for (i = 0; i < wpa_ie->akm_suite_count; i++)
                {
                   if (M80211_AKM_SUITE_802X_PMKSA ==
                       M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(wpa_ie)[i].type)
                   {
                      *akm = M80211_AKM_SUITE_802X_PMKSA;
                      *type = WEI_IE_TYPE_WPA;
                      DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is WPA 802.1x \n");
#if (DE_CCX == CFG_INCLUDED)
		      FoundAuthType = TRUE;
#else                     
		      return WIFI_ENGINE_SUCCESS;
#endif
                   }
#if (DE_CCX == CFG_INCLUDED)
		   else if (M80211_AKM_SUITE_802X_CCKM ==
                       M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(wpa_ie)[i].type)
                   {
                      *akm = M80211_AKM_SUITE_802X_CCKM;
					  *type = WEI_IE_TYPE_WPA;
                      DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is CCKM \n");
                      return WIFI_ENGINE_SUCCESS;
                   }
#endif
                }
#if (DE_CCX == CFG_INCLUDED)
		if(FoundAuthType==TRUE)
		  return WIFI_ENGINE_SUCCESS;
#endif
             }
         DE_TRACE_STATIC(TR_AUTH, "WPA not allowed in net \n");
      }
      break;
      case Authentication_WPA_PSK:
      {
             if (chk_wpa_ie(wpa_ie))
             {
                for (i = 0; i < wpa_ie->akm_suite_count; i++)
                {
                   if (M80211_AKM_SUITE_PSK ==
                       M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(wpa_ie)[i].type)
                   {
                      *akm = M80211_AKM_SUITE_PSK;
                      *type = WEI_IE_TYPE_WPA;
                      DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is WPA PSK \n");
                      return WIFI_ENGINE_SUCCESS;
                   }
                }
             }
         DE_TRACE_STATIC(TR_AUTH, "WPA_PSK not allowed in net \n");
      }
      break;
      case Authentication_WPA_None:
      {
             if (chk_wpa_ie(wpa_ie))
             {
                for (i = 0; i < wpa_ie->akm_suite_count; i++)
                {
                   if (M80211_AKM_SUITE_PSK ==
                       M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(wpa_ie)[i].type)
                   {
                      *akm = M80211_AKM_SUITE_PSK;
                      *type = WEI_IE_TYPE_WPA;
                      DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is WPA PSK \n");
                      return WIFI_ENGINE_SUCCESS;
                   }
                }
             }
         DE_TRACE_STATIC(TR_AUTH, "WPA_PSK not allowed in net \n");
      }
      break;
      case Authentication_WPA_2:
      {
             if (chk_rsn_ie(rsn_ie))
             {
                for (i = 0; i < rsn_ie->akm_suite_count; i++)
                {
                   if (M80211_AKM_SUITE_802X_PMKSA ==
                       M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(rsn_ie)[i].type)
                   {
                      *akm = M80211_AKM_SUITE_802X_PMKSA;
                      *type = WEI_IE_TYPE_RSN;
                      DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is RSN 802.1x \n");
#if (DE_CCX == CFG_INCLUDED)
		      FoundAuthType = TRUE;
#else  
                      return WIFI_ENGINE_SUCCESS;
#endif
#if (DE_CCX == CFG_INCLUDED)
		   } 
		   else if (M80211_AKM_SUITE_802X_CCKM ==
			    M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(rsn_ie)[i].type)
		     {
		       *akm = M80211_AKM_SUITE_802X_CCKM;
		       *type = WEI_IE_TYPE_RSN;
		       DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is CCKM \n");
		       return WIFI_ENGINE_SUCCESS;
#endif
                   } else
                      DE_TRACE_INT2(TR_AUTH,"net rsn type = %d net = " TR_FPTR "\n", 
                                    M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(rsn_ie)[i].type, TR_APTR(net));
                }
#if (DE_CCX == CFG_INCLUDED)
		if(FoundAuthType==TRUE)
		  return WIFI_ENGINE_SUCCESS;
#endif
             }
         DE_TRACE_INT(TR_AUTH, "WPA2 not allowed in net, akm_suite_count=%d\n", rsn_ie->akm_suite_count);
      }
      break;
      case Authentication_WPA_2_PSK:
      {
             if (chk_rsn_ie(rsn_ie))
             {
                for (i = 0; i < rsn_ie->akm_suite_count; i++)
                {
                   if (M80211_AKM_SUITE_PSK ==
                       M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(rsn_ie)[i].type)
                   {
                      *akm = M80211_AKM_SUITE_PSK;
                      *type = WEI_IE_TYPE_RSN;
                      DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is RSN PSK \n");   
                      return WIFI_ENGINE_SUCCESS;
                   }
                }
             }
         DE_TRACE_STATIC(TR_AUTH, "WPA2_PSK not allowed in net \n");
      }
      break;
   case Authentication_WAPI:
     {
       *akm = M80211_AKM_SUITE_802X_PMKSA;
       *type = WEI_IE_TYPE_WAPI;
       DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is WAPI Cert \n");
       return WIFI_ENGINE_SUCCESS;
     }

   case Authentication_WAPI_PSK:
     {
       *akm = M80211_AKM_SUITE_PSK;
       *type = WEI_IE_TYPE_WAPI;
       DE_TRACE_STATIC(TR_AUTH, "Allowed AKM suite is WAPI PSK \n");
       return WIFI_ENGINE_SUCCESS;
     }

      default:
         DE_TRACE_STATIC(TR_AUTH, "Unknown authenticationMode \n");
         break;
   }
   
   return WIFI_ENGINE_FAILURE;
}

/*!
 * Build a RSN IE based on the specified cipher suites.
 *
 * @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
 */
int wei_build_rsn_ie(void* context_p,
   m80211_ie_rsn_parameter_set_t *dst,
   m80211_cipher_suite_t group_suite,
   m80211_cipher_suite_t pairwise_suite,
   m80211_akm_suite_t akm_suite,
   unsigned int nUsePMKID, const m80211_bssid_info *pmkid)
{
   int size;
   unsigned char *pmkid_dst;
   unsigned int i;
   
   dst->hdr.id = M80211_IE_ID_RSN;
   dst->hdr.len = 20;
   
#if (DE_CCX == CFG_INCLUDED)
   nUsePMKID = 0; // DIPA XXX Avoid using PMKIDs for now.
#endif
   if(nUsePMKID > 0)
      dst->hdr.len += 2 + nUsePMKID * sizeof(m80211_pmkid_value);
   dst->version = M80211_RSN_VERSION;

   DE_MEMCPY(dst->group_cipher_suite.id.octet, M80211_RSN_OUI, 3);
   dst->group_cipher_suite.type = group_suite;

   dst->pairwise_cipher_suite_count = 1;
   dst->akm_suite_count = 1;
   dst->pmkid_count = nUsePMKID;

   /* Allocate a pool for one pairwise and on akm selector. */
#if 0
   size = sizeof(m80211_akm_suite_selector_t) + sizeof(m80211_cipher_suite_selector_t)
        + sizeof(m80211_pmkid_value) * nUsePMKID;
#else
   /* macWrapper.c uses WrapperCopy_default for copying data. 
    * That code is broken and we must therefor allocate more 
    * data then nessesary to counter that. 
    * This is a workaround. */
   size = dst->hdr.len;
#endif
   dst->rsn_pool = (char*)WrapperAttachStructure(context_p, size);

   DE_MEMCPY(&(M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(dst)[0].id.octet), 
             M80211_RSN_OUI, 3);
   M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(dst)[0].type 
      = pairwise_suite;

#if (DE_CCX == CFG_INCLUDED)
   if (akm_suite == M80211_AKM_SUITE_802X_CCKM) 
     DE_MEMCPY(&(M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(dst)[0].id.octet), 
	       M80211_CCX_OUI, 3);
   else 
#endif
     DE_MEMCPY(&(M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(dst)[0].id.octet), 
	       M80211_RSN_OUI, 3);
   M80211_IE_RSN_PARAMETER_SET_GET_AKM_SELECTORS(dst)[0].type = akm_suite;

   dst->rsn_capabilities = 
      M80211_RSN_CAPABILITY_PTKSA_4_REPLAY_COUNTER 
      | M80211_RSN_CAPABILITY_GTKSA_4_REPLAY_COUNTER;


   pmkid_dst = (unsigned char*)M80211_IE_RSN_PARAMETER_SET_GET_PMKID_SELECTORS(dst);
   for(i = 0; i < nUsePMKID; i++) {
      DE_MEMCPY(pmkid_dst, pmkid[i].PMKID.octet, sizeof(pmkid[i].PMKID.octet));
      pmkid_dst += sizeof(pmkid[i].PMKID.octet);
   }

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Build a WAPI IE based on the specified cipher suites.
 *
 * @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
 */
int wei_build_wapi_ie(
		      void* context_p,
		      m80211_ie_wapi_parameter_set_t *dst,
		      m80211_akm_suite_t akm_suite)
{
  /*m80211_ie_wapi_parameter_set_t *scan_ind_wapi =
    &wei_netlist_get_current_net()->bss_p->bss_description_p->ie.wapi_parameter_set;*/
  int size;

  dst->hdr.hdr.id = 0x44;
  dst->hdr.hdr.len = 0x16;
  dst->hdr.version = M80211_WAPI_VERSION;

  /* Allocate a pool for one pairwise and on akm selector. */
  size = 20;
  dst->wapi_pool = (char*)WrapperAttachStructure(context_p, size);

  /* AKM Count */
  dst->wapi_pool[0] = 1;
  dst->wapi_pool[1] = 0;

  /* AKM Suite */
  dst->wapi_pool[2] = 0;
  dst->wapi_pool[3] = 0x14;
  dst->wapi_pool[4] = 0x72;
  dst->wapi_pool[5] = akm_suite;

  /* Unicast Suite Count */
  dst->wapi_pool[6] = 1;
  dst->wapi_pool[7] = 0;

  /* Unicast Suite */
  dst->wapi_pool[8] = 0;
  dst->wapi_pool[9] = 0x14;
  dst->wapi_pool[10] = 0x72;
  dst->wapi_pool[11] = 1; /* SMS4 */

  /* Multicast Suite Count */
  dst->wapi_pool[12] = 0;
  dst->wapi_pool[13] = 0x14;
  dst->wapi_pool[14] = 0x72;
  dst->wapi_pool[15] = 1; /* SMS4 */

  /* Capabilities */
  dst->wapi_pool[16] = 0;
  dst->wapi_pool[17] = 0;

  /* BKID Count */
  dst->wapi_pool[18] = 0;
  dst->wapi_pool[19] = 0;

  return WIFI_ENGINE_SUCCESS;
}

/*!
 * Build a WPA IE based on the specified cipher suites.
 *
 * @return WIFI_ENGINE_FAILURE, WIFI_ENGINE_SUCCESS
 */
int wei_build_wpa_ie(
   void* context_p,
   m80211_ie_wpa_parameter_set_t *dst,
   m80211_cipher_suite_t group_suite,
   m80211_cipher_suite_t pairwise_suite,
   m80211_akm_suite_t akm_suite)
{
   m80211_ie_wpa_parameter_set_t *scan_ind_wpa = 
      &wei_netlist_get_current_net()->bss_p->bss_description_p->ie.wpa_parameter_set;
   int size;
   int alloc_size;
   
   dst->hdr.hdr.id = M80211_IE_ID_VENDOR_SPECIFIC;
   dst->hdr.hdr.len = 0x16;
   dst->hdr.OUI_1 = 0x00;
   dst->hdr.OUI_2 = 0x50;
   dst->hdr.OUI_3 = 0xF2;
   dst->hdr.OUI_type = WPA_IE_OUI_TYPE;
   dst->version = M80211_RSN_VERSION;

   DE_MEMCPY(dst->group_cipher_suite.id.octet, M80211_WPA_OUI, 3);
   dst->group_cipher_suite.type = group_suite;

   dst->pairwise_cipher_suite_count = 1;
   dst->akm_suite_count = 1;

   /* Allocate a pool for one pairwise and on akm selector. */
   size = sizeof(m80211_akm_suite_selector_t) + sizeof(m80211_cipher_suite_selector_t);

   /* macWrapper.c uses WrapperCopy_default for copying data. 
    * That code is broken and we must therefor allocate more 
    * data then nessesary to counter that. 
    * This is a workaround. */
   alloc_size = dst->hdr.hdr.len;
   dst->rsn_pool = (char*)WrapperAttachStructure(context_p, alloc_size);

   DE_MEMCPY(&(M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(dst)[0].id.octet), 
             M80211_WPA_OUI, 3);
   M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(dst)[0].type 
      = pairwise_suite;

#if (DE_CCX == CFG_INCLUDED)
   if (akm_suite == M80211_AKM_SUITE_802X_CCKM) 
     DE_MEMCPY(&(M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(dst)[0].id.octet), 
	       M80211_CCX_OUI, 3);
   // M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(dst)[0].type = 0;
   else 
#endif
     DE_MEMCPY(&(M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(dst)[0].id.octet), 
             M80211_WPA_OUI, 3);
   M80211_IE_WPA_PARAMETER_SET_GET_AKM_SELECTORS(dst)[0].type = akm_suite;

   /* calculate if wpa ie in the scan ind is padded, if so do the same here */
   size = 14 
      + scan_ind_wpa->pairwise_cipher_suite_count*4 
      + scan_ind_wpa->akm_suite_count*4;

   if(scan_ind_wpa->hdr.hdr.len == size+2)
   {
      dst->hdr.hdr.len += 2;
      dst->rsn_capabilities = scan_ind_wpa->rsn_capabilities;
   }

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Copies a association request. The memory allocated should be freed by
 * a call to wei_free_assoc_req().
 *
 * @return A pointer to a new buffer containing the copied request.
 *         NULL on failure.
 */
m80211_mlme_associate_req_t *wei_copy_assoc_req(m80211_mlme_associate_req_t *src)
{
   m80211_mlme_associate_req_t *r;

   r = (m80211_mlme_associate_req_t *)WrapperAllocStructure(NULL, sizeof *r);
   if (NULL == r)
   {
      return NULL;
   }
   DE_MEMCPY(r, src, sizeof *r);
   WrapperCopy_common_IEs_t(r, &(r->ie), &(src->ie));
   return r;
}

/*!
 * Copies a association confirm. The memory allocated should be freed by
 * a call to wei_free_assoc_cfm().
 *
 * @return A pointer to a new buffer containing the copied confirm message.
 *         NULL on failure.
 */
m80211_mlme_associate_cfm_t *wei_copy_assoc_cfm(m80211_mlme_associate_cfm_t *src)
{
   m80211_mlme_associate_cfm_t *r;

   r = (m80211_mlme_associate_cfm_t *)WrapperAllocStructure(NULL, sizeof *r);
   if (NULL == r)
   {
      return NULL;
   }
   DE_MEMCPY(r, src, sizeof *r);
   WrapperCopy_common_IEs_t(r, &(r->ie), &(src->ie));

   return r;
}

void wei_free_assoc_cfm(m80211_mlme_associate_cfm_t *p)
{
   if (NULL == p)
   {
      DE_TRACE_STATIC(TR_SEVERE, "Tried to free NULL pointer \n");
      return;
   }

   WrapperFreeStructure(p);
}

void wei_free_assoc_req(m80211_mlme_associate_req_t *p)
{
   if (NULL == p)
   {
      DE_TRACE_STATIC(TR_SEVERE, "Tried to free NULL pointer \n");
      return;
   }
   WrapperFreeStructure(p);
}

/*!
 * Compare two cipher suites for "security" ranking.
 *
 * @return -1 if a is less secure than b
 *          0 if a and b are equal
 *          1 if a is more secure than b
 */
int wei_compare_cipher_suites(m80211_cipher_suite_t a, m80211_cipher_suite_t b)
{
   int aval, bval;

   aval = wei_cipher_suite_to_val(a);
   bval = wei_cipher_suite_to_val(b);
   if (aval < bval)
   {
      return -1;
   }
   if (aval > bval)
   {
      return 1;
   }

   return 0;
}

/*!
 * Return the cipher suite security level ranking.
 * Higher values means a more secure cipher suite.
 */
int wei_cipher_suite_to_val(m80211_cipher_suite_t a)
{
   
   switch (a)
   {
      case M80211_CIPHER_SUITE_WEP40:
      {
         return 1;
      }

      case M80211_CIPHER_SUITE_WEP104:
      {
         return 2;
      }

      case M80211_CIPHER_SUITE_TKIP:
      {
         return 3;
      }

      case M80211_CIPHER_SUITE_CCMP:
      {
         return 4;
      }

      default:
         return 0;

   }
   
}

/*!
 * Return the highest pairwise cipher suite found in the net that is also
 * allowed in the driver.
 *
 * @param suite Output buffer.
 * @param net Input buffer.
 * @param ie_type INPUT: Specifices the source of the cipher suite.
 * @return 
 * - WIFI_ENGINE_SUCCESS if a suite was found.
 * - WIFI_ENGINE_FAILURE otherwise.
 */
int wei_get_highest_pairwise_suite_from_net(m80211_cipher_suite_t *suite,
                                         WiFiEngine_net_t *net,
                                         wei_ie_type_t ie_type)
{
   m80211_cipher_suite_t tmp_suite;
   m80211_ie_rsn_parameter_set_t    *rsn_ie;
   m80211_ie_wpa_parameter_set_t    *wpa_ie;
   int i;

   rsn_ie = &net->bss_p->bss_description_p->ie.rsn_parameter_set;
   wpa_ie = &net->bss_p->bss_description_p->ie.wpa_parameter_set;

   *suite = M80211_CIPHER_SUITE_NONE;
   switch (ie_type)
   {
      case WEI_IE_TYPE_WAPI:
         *suite = M80211_CIPHER_SUITE_WPI;
         return WIFI_ENGINE_SUCCESS;

      case WEI_IE_TYPE_RSN:
      {
         if (rsn_ie->hdr.id != 0xFF) 
         {
            for (i = 0; i < rsn_ie->pairwise_cipher_suite_count; i++)
            {
               if (wei_compare_cipher_suites(M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(rsn_ie)[i].type, *suite) > 0)
               {
                  if (wei_encryption_mode_allowed(wei_cipher_suite2encryption(M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(rsn_ie)[i].type)))
                  {
                     *suite = M80211_IE_RSN_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(rsn_ie)[i].type;
                  }
               }
            }
            return WIFI_ENGINE_SUCCESS;
         }
      }
      break;
      case WEI_IE_TYPE_WPA:
      {
         if (wpa_ie->hdr.hdr.id != 0xFF) 
         {
            for (i = 0; i < wpa_ie->pairwise_cipher_suite_count; i++)
            {
               if (wei_compare_cipher_suites(M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(wpa_ie)[i].type, *suite) > 0)
               {
                  if (wei_encryption_mode_allowed(wei_cipher_suite2encryption(M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(wpa_ie)[i].type)))
                  {
                     *suite = M80211_IE_WPA_PARAMETER_SET_GET_PAIRWISE_CIPHER_SELECTORS(wpa_ie)[i].type;
                  }
               }
            }
            return WIFI_ENGINE_SUCCESS;
         }
      }
      break;

      case WEI_IE_TYPE_CAP:
      {
         if (net->bss_p->bss_description_p->capability_info & M80211_CAPABILITY_PRIVACY)
         {
            tmp_suite = M80211_CIPHER_SUITE_WEP104;
            if (wei_compare_cipher_suites(tmp_suite, *suite) > 0)
            {
               if (wei_encryption_mode_allowed(wei_cipher_suite2encryption(tmp_suite)))
               {
                  *suite = M80211_CIPHER_SUITE_WEP104;
               }
            }
         }
         return WIFI_ENGINE_SUCCESS;
      }

      default:
         DE_TRACE_INT(TR_SEVERE, "ERROR: Unknown IE type %d returned from wei_get_highest_pairwise_suite_from_net()\n", ie_type);
         
   }

   return WIFI_ENGINE_FAILURE;
}


void wei_sm_init(void)
{
   DE_ASSERT(WEI_TQ_EMPTY(&sig_queue));
}

void wei_sm_drain_sig_q(void)
{
   struct signal_container *c;
   
   WIFI_LOCK();
   while ((c = WEI_TQ_FIRST(&sig_queue)) != NULL) {
      WEI_TQ_REMOVE(&sig_queue, c, next);
      DriverEnvironment_Nonpaged_Free(c);
   }
   WIFI_UNLOCK();
}

void wei_sm_execute(void)
{
   struct signal_container *c;
   DE_TRACE_STATIC(TR_SM_HIGH_RES, "===> wei_sm_execute\n");

   WIFI_LOCK();
   if (!WEI_TQ_EMPTY(&sig_queue))
   {
      while ((c = WEI_TQ_FIRST(&sig_queue)) != NULL) {
         WEI_TQ_REMOVE(&sig_queue, c, next);
         WIFI_UNLOCK();
         ucos_send_msg(c->msg, c->dest, c->param);
         DriverEnvironment_Nonpaged_Free(c);
         WIFI_LOCK();
      }    
      WIFI_UNLOCK();
      
      /* Do what ever is in the pipe for UCOS processes. */
      if (DriverEnvironment_acquire_trylock(&wifiEngineState.sm_lock) == LOCK_UNLOCKED)
      {      
         ucos_executive(UCOS_MODE_UNTIL_EMPTY);
         DriverEnvironment_release_trylock(&wifiEngineState.sm_lock);
      }
      else 
      {
         DE_TRACE_STATIC(TR_WEI, "ucos_executive is busy, thread abstains.\n");
      }
   } else {
      WIFI_UNLOCK();
   }

   DE_TRACE_STATIC(TR_SM_HIGH_RES, "<=== wei_sm_execute\n"); 
}

wei_sm_queue_param_s*
wei_wrapper2param(void *p)
{
   wei_sm_queue_param_s *c;

   if(!p) return NULL;

   c = (wei_sm_queue_param_s*)DriverEnvironment_Nonpaged_Malloc(sizeof(wei_sm_queue_param_s));
   if(c) {
      /* Copy the message (which may actually be a linked list of buffers). */
      c->p = (void*)WrapperCopyStructure(p);
      c->type = WRAPPER_STRUCTURE;
   }

   return c;
}

void wei_sm_queue_sig(ucos_msg_id_t sig,
                      SYSDEF_ObjectType dest,
							 wei_sm_queue_param_s *p,
                      bool_t  internal)
{
   struct signal_container *c;

   if (0 == sig)
   {
      return;
   }

   if(internal)
   {
      ucos_send_msg(sig, dest, (ucos_msg_param_t)p);
   }
   else
   {
      c = (struct signal_container *)DriverEnvironment_Nonpaged_Malloc(sizeof *c);
      if (c == NULL)
      {
         DE_TRACE_STATIC(TR_SEVERE, "Failed to allocate memory for signal container in wei_queue_sm_sig(). Signal discarded.\n");
         return;
      }

      c->msg = sig;
      c->dest = dest; 
      c->param = (ucos_msg_param_t)p;   
      WIFI_LOCK();
      WEI_TQ_INSERT_TAIL(&sig_queue, c, next);
      WIFI_UNLOCK();
   }
 
}

int wei_channel_list_from_country_ie(channel_list_t *cl, m80211_ie_country_t *ie)
{
   int  i, chan;
   unsigned int idx;
   int  channel_info_count;

   channel_info_count = (ie->hdr.len - M80211_IE_LEN_COUNTRY_STRING) / M80211_IE_CHANNEL_INFO_TRIPLET_SIZE; 

   if (channel_info_count > M80211_CHANNEL_LIST_MAX_LENGTH)
      channel_info_count = M80211_CHANNEL_LIST_MAX_LENGTH;   
   idx = 0;
   for (i = 0; i < channel_info_count; i++)
   {
      if (ie->channel_info[i].first_channel == 0xFF) 
         continue; 
      for (chan = ie->channel_info[i].first_channel; chan < (ie->channel_info[i].first_channel + 
                                                             ie->channel_info[i].num_channels);
           chan++)
      {
         if (idx < sizeof cl->channelList)
         {
            cl->channelList[idx] = chan;
            idx++;
         }
         else
         {
            break;
         }
      }
   }
   cl->no_channels = idx;

   return 1;
}

we_ch_bitmask_t wei_channels2bitmask(channel_list_t *src)
{
   int i;
   we_ch_bitmask_t dst = 0;

   DE_ASSERT(src->no_channels < (sizeof dst) * CHAR_BIT);
   for (i = 0; i < src->no_channels; i++)
   {
      dst |= (1<<(src->channelList[i] - 1));
   }
   
   return dst;
}

int wei_get_number_of_ptksa_replay_counters_from_net(WiFiEngine_net_t *net)
{
   uint16_t cap;

   if (M80211_IE_ID_RSN != net->bss_p->bss_description_p->ie.rsn_parameter_set.hdr.id)
   {
      return 0;
   }
   cap = net->bss_p->bss_description_p->ie.rsn_parameter_set.rsn_capabilities;
   switch (cap & M80211_RSN_CAPABILITY_PTKSA_REPLAY_COUNTER_MASK)
   {
      case M80211_RSN_CAPABILITY_PTKSA_1_REPLAY_COUNTER :
         return 1;
      case M80211_RSN_CAPABILITY_PTKSA_2_REPLAY_COUNTER :
         return 2;
      case M80211_RSN_CAPABILITY_PTKSA_4_REPLAY_COUNTER :
         return 4;
      case M80211_RSN_CAPABILITY_PTKSA_16_REPLAY_COUNTERS :
         return 16;
      default:
         DE_TRACE_STATIC(TR_SEVERE, "Invalid replay counter field (broken code?) \n");
   }
   return 0;
}

int wei_ratelist2ie(m80211_ie_supported_rates_t *sup_dst, m80211_ie_ext_supported_rates_t *ext_dst,
                    rRateList *rrates)
{
   unsigned int i, sup_i, ext_i;

   sup_i = ext_i = 0;
   sup_dst->hdr.id = M80211_IE_ID_NOT_USED;
   ext_dst->hdr.id = M80211_IE_ID_NOT_USED;
   for (i = 0; i < rrates->len; i++)
   {
      if (sup_i < sizeof sup_dst->rates)
      {
         sup_dst->rates[sup_i] = rrates->rates[i];
         sup_i++;
      }
      else if (ext_i < sizeof ext_dst->rates)
      {
         ext_dst->rates[ext_i] = rrates->rates[i];
         ext_i++;
      }
      else
      {
         DE_TRACE_STATIC(TR_WEI, "Rate list overflow.\n");
         return FALSE;
      }
   }
   if (sup_i)
   {
      sup_dst->hdr.id = M80211_IE_ID_SUPPORTED_RATES;
      sup_dst->hdr.len = sup_i;
   }
   if (ext_i)
   {
      ext_dst->hdr.id = M80211_IE_ID_EXTENDED_SUPPORTED_RATES;
      ext_dst->hdr.len = ext_i;
   }
   return TRUE;
}

int wei_ratelist2mask(uint32_t  *rate_mask,
                      rRateList *rrates)
{
   unsigned int i, j;
   int result = TRUE;

   *rate_mask = 0;
   for (i = 0; i < rrates->len; i++) {
      for (j = 0; j < wifiEngineState.rate_table_len; j++) {
	 uint32_t rate = wifiEngineState.rate_table[j];
         if(!WE_RATE_ENTRY_IS_BG(rate))
            continue;
         if ((rrates->rates[i] & ~0x80) == WE_RATE_ENTRY_CODE(rate)) {
            *rate_mask |= (1 << WE_RATE_ENTRY_BITPOS(rate));
            break;
         }
      }
      if(j == wifiEngineState.rate_table_len)
         result = FALSE; /* list contains unsupported rate */
   }
  return result;
}

#if DE_ENABLE_HT_RATES == CFG_ON

int wei_htcap2mask(uint32_t *rate_mask,
                   const m80211_ie_ht_capabilities_t *ht_cap)
{
   unsigned int i, j;
   int result = TRUE;

   *rate_mask = 0;
   if(ht_cap->hdr.id != M80211_IE_ID_HT_CAPABILITIES)
      return result;
   WE_BITMASK_FOREACH_SET(i, ht_cap->supported_mcs_set.rx_mcs_bitmap) {
      for (j = 0; j < wifiEngineState.rate_table_len; j++) {
	 uint32_t rate = wifiEngineState.rate_table[j];
         if(!WE_RATE_ENTRY_IS_HT(rate))
            continue;
         if(i == WE_RATE_ENTRY_CODE(rate)) {
            *rate_mask |= (1 << WE_RATE_ENTRY_BITPOS(rate));
            break;
         }
      }
      if(j == wifiEngineState.rate_table_len)
         result = FALSE; /* list contains unsupported rate */
   }
   return result;
}

int wei_htoper2mask(uint32_t *rate_mask,
                    const m80211_ie_ht_operation_t *ht_oper)
{
   unsigned int i, j;
   int result = TRUE;

   *rate_mask = 0;
   if(ht_oper->hdr.id != M80211_IE_ID_HT_OPERATION)
      return result;
   WE_BITMASK_FOREACH_SET(i, ht_oper->basic_mcs_set) {
      for (j = 0; j < wifiEngineState.rate_table_len; j++) {
	 uint32_t rate = wifiEngineState.rate_table[j];
         if(!WE_RATE_ENTRY_IS_HT(rate))
            continue;
         if(i == WE_RATE_ENTRY_CODE(rate)) {
            *rate_mask |= (1 << WE_RATE_ENTRY_BITPOS(rate));
            break;
         }
      }
      if(j == wifiEngineState.rate_table_len)
         result = FALSE; /* list contains unsupported rate */
   }
   return result;
}

#endif /* DE_ENABLE_HT_RATES == CFG_ON */

int wei_ie2ratelist(rRateList *rrates, m80211_ie_supported_rates_t *sup, 
                    m80211_ie_ext_supported_rates_t *ext)
{
   int i;
   uint8_t *p;

   p = rrates->rates;
   rrates->len = 0;
   for (i = 0; i < sup->hdr.len; i++, rrates->len++, p++)
   {
      *p = sup->rates[i];
   }
   for (i = 0; i < ext->hdr.len; i++, rrates->len++, p++)
   {
      *p = ext->rates[i];
   }
   return TRUE;
}

#define IS_BASIC_RATE(x) ((x) & 0x80)

void wei_prune_nonbasic_ratelist(rRateList *rates)
{
   rRateList new_rate_list;
   unsigned int i;
   uint8_t *p;

   DE_ASSERT(rates->len <= DE_ARRAY_SIZE(rates->rates));

   DE_MEMSET(&new_rate_list, 0, sizeof new_rate_list);
   p = new_rate_list.rates;
   for (i = 0; i < rates->len; i++)
   {
      if (IS_BASIC_RATE(rates->rates[i]))
      {
         *p = rates->rates[i];
         p++;
      }
   }
   new_rate_list.len = (p - new_rate_list.rates);
   DE_MEMCPY(rates, &new_rate_list, sizeof *rates);
}

#undef IS_BASIC_RATE

void wei_init_wmm_ie(m80211_ie_WMM_information_element_t *ie, int wmm_ps_enable)
{
   rBasicWiFiProperties*         basicWiFiProperties; 

   basicWiFiProperties = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);
   
   ie->WMM_QoS_Info             = 0x00;
   ie->WMM_hdr.hdr.hdr.id       = M80211_IE_ID_VENDOR_SPECIFIC;
   ie->WMM_hdr.hdr.hdr.len      = 7;
   ie->WMM_hdr.hdr.OUI_1        = 0x00;
   ie->WMM_hdr.hdr.OUI_2        = 0x50;
   ie->WMM_hdr.hdr.OUI_3        = 0xF2;
   ie->WMM_hdr.hdr.OUI_type     = 0x02;
   ie->WMM_hdr.OUI_Subtype      = 0x00;
   ie->WMM_Protocol_Version     = 0x01;

   if(basicWiFiProperties->enableWMMPs && wmm_ps_enable)
   {

      if (basicWiFiProperties->QoSInfoElements.ac_be == 1)
      {
         ie->WMM_QoS_Info |= (1<<3);
      }
      if (basicWiFiProperties->QoSInfoElements.ac_bk == 1)
      {
        ie->WMM_QoS_Info |= (1<<2);
      }
      if (basicWiFiProperties->QoSInfoElements.ac_vi == 1)
      {
        ie->WMM_QoS_Info |= (1<<1);
      }
      if (basicWiFiProperties->QoSInfoElements.ac_vo == 1)
      {
        ie->WMM_QoS_Info |= 1;
      }
      ie->WMM_QoS_Info |= basicWiFiProperties->qosMaxServicePeriodLength << 4;
   }

      
}
/*!
 * @brief Schedule a callback with a data buffer and a result code.
 *
 * @param trans_id Id that identifies the callback.
 * @param data The data buffer. It must be allocated with 
 *             DriverEnvironment_Nonpaged_Free(). Can be NULL.
 * @param len Data buffer length in bytes. Ignored if data is NULL.
 * @param result Status code to be passed to the callback.
 * @return
 * - WIFI_ENGINE_SUCCESS
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if no matching callback was found.
 */
int wei_schedule_data_cb_with_status(uint32_t trans_id, void *data, 
                                     size_t len, uint8_t result)
{
   we_cb_container_t *cbc;
 
   cbc = wei_cb_find_pending_callback(trans_id);
   if (cbc == NULL)
   {
      /* weird failure code; this is not a failure, it just wasn't found! */
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }

   DE_ASSERT(cbc->data == NULL);

   if (len && data)
   {
      cbc->data = data;
      cbc->data_len = len;
   }
   else
   {
      cbc->data = NULL;
      cbc->data_len = 0;
   }
   cbc->status = result;

   WiFiEngine_ScheduleCallback(cbc);

   if (cbc->repeating)
   {
      wei_cb_queue_pending_callback(cbc);
   }

   return WIFI_ENGINE_SUCCESS;
}   

/*!
 * @brief Handle a trans_id wrap.
 *
 * Flush old callbacks to avoid collisions when the transid wraps.
 *
 * @return Returns the new initial trans_id.
 */
unsigned int wei_transid_wrap(void)
{
   wei_cb_flush_pending_cb_tab();

   return 1;
}

int wei_jobid_wrap(void)
{
   DE_TRACE_STATIC(TR_NOISE, "job_id wrapped.\n");
   return 1;
}

void wei_save_con_failed_reason(we_con_failed_e type, uint8_t reason)
{
   wifiEngineState.last_con_failed_ind.type = type;
   wifiEngineState.last_con_failed_ind.reason_code = reason;
}

void wei_save_con_lost_reason(we_con_lost_e type, uint8_t reason)
{
   wifiEngineState.last_con_lost_ind.type = type;
   wifiEngineState.last_con_lost_ind.reason_code = reason;
}

void WiFiEngine_get_last_con_lost_reason(we_con_lost_s *dst)
{
   DE_MEMCPY(dst, &wifiEngineState.last_con_lost_ind, sizeof(we_con_lost_s));
}

void WiFiEngine_get_last_con_failed_reason(we_con_failed_s *dst)
{
   DE_MEMCPY(dst, &wifiEngineState.last_con_failed_ind, sizeof(we_con_failed_s));
}

void wei_enable_aggregation(we_aggr_type_t type, uint32_t aggr_max_size)
{
   /* this table contains one bit for each possible firmware message,
    * there is a limit of max 64 ids per type */
   uint8_t aggr[HIC_MESSAGE_TYPE_NUM_TYPES * 64 / 8];

#define BITNUM(T, I) (((T) << 6) | ((I) & ~MAC_API_PRIMITIVE_TYPE_BIT))

   switch(type)
   {
      case AGGR_ALL:
         DE_MEMSET(aggr, 0xff, sizeof(aggr));
         break;
      case AGGR_ALL_BUT_DATA:
         DE_MEMSET(aggr, 0xff, sizeof(aggr));
         WE_CLEAR_BIT(aggr, BITNUM(HIC_MESSAGE_TYPE_DATA, HIC_MAC_DATA_IND));
         break;
      case AGGR_SCAN_IND:
         DE_MEMSET(aggr, 0, sizeof(aggr));
         WE_SET_BIT(aggr, BITNUM(HIC_MESSAGE_TYPE_MGMT, MLME_SCAN_IND));
         break;
      case AGGR_ONLY_DATA_CFM:
         DE_MEMSET(aggr, 0, sizeof(aggr));
         WE_SET_BIT(aggr, BITNUM(HIC_MESSAGE_TYPE_DATA, HIC_MAC_DATA_CFM));
         break;
      case AGGR_DATA:
         DE_MEMSET(aggr, 0, sizeof(aggr));
         WE_SET_BIT(aggr, BITNUM(HIC_MESSAGE_TYPE_DATA, HIC_MAC_DATA_CFM));
         WE_SET_BIT(aggr, BITNUM(HIC_MESSAGE_TYPE_DATA, HIC_MAC_DATA_IND));
         break;      
      default:
         DE_BUG_ON(TRUE,"Unknown type supplied to wei_enable_aggregation");
   }

   if (WiFiEngine_SendMIBSet(MIB_dot11AggregationFilter, 
                             NULL, 
                             aggr, 
                             sizeof(aggr)) != WIFI_ENGINE_SUCCESS){
      DE_TRACE_STATIC(TR_WARN, "MIBSET AGGR FILTER FAILED\n");
      return;
   }

   if (WiFiEngine_SendMIBSet(MIB_dot11Aggregation, 
                             NULL, 
                             &aggr_max_size, 
                             sizeof(aggr_max_size)) != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_STATIC(TR_WARN, "MIBSET AGGR FAILED\n");
      return;
   }
   DE_TRACE_STATIC(TR_MIB, "MIBSET AGGR SUCCESS\n");
}

/* upper layer only (media connected) */
int wei_is_80211_connected(void)
{
   return (wifiEngineState.main_state == driverConnected);
}


/** @} */ /* End of wifi_engine_internal group */
