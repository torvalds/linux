/* $Id: packer.c,v 1.18 2008-04-17 12:54:36 anob Exp $ */
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
/** @defgroup packer WiFiEngine internal message packing/unpacking interface.
 *
 * @brief This module is part of the hic unit. It performs message packing/unpacking services
 * for WiFiEngine.
 *
 *  @{
 */

#include "sysdef.h"
#include "packer.h"
#include "wifi_engine_internal.h"

static int
PackMessage(int message_type, 
            int message_id, 
            void* payload, 
            Blob_t* blob_p, 
            WrapperAction_t action)
{
   int   size = blob_p->index;

#define PACK(T) HicWrapper_##T((T*)payload, blob_p, action)

   /* Call the appropriate packing function. */
   switch ((message_type << 8) | message_id) { 
#define X(A, B) ((HIC_MESSAGE_TYPE_##A << 8) | (B))
      case X(MGMT, MLME_RESET_REQ):
         PACK(m80211_mlme_reset_req_t);
         break;

      case X(MGMT, MLME_SCAN_REQ):
         PACK(mlme_direct_scan_req_t);
         break;

      case X(MGMT, MLME_POWER_MGMT_REQ):
         PACK(m80211_mlme_power_mgmt_req_t);
         break;
               
      case X(MGMT, MLME_JOIN_REQ):
         PACK(m80211_mlme_join_req_t);
         break;
               
      case X(MGMT, MLME_AUTHENTICATE_REQ):
         PACK(m80211_mlme_authenticate_req_t);
         break;

      case X(MGMT, MLME_DEAUTHENTICATE_REQ):
         PACK(m80211_mlme_deauthenticate_req_t);
         break;
               
      case X(MGMT, MLME_ASSOCIATE_REQ):
         PACK(m80211_mlme_associate_req_t);
         break;

      case X(MGMT, MLME_REASSOCIATE_REQ):
         PACK(m80211_mlme_reassociate_req_t);
         break;
               
      case X(MGMT, MLME_DISASSOCIATE_REQ):
         PACK(m80211_mlme_disassociate_req_t);
         break;

      case X(MGMT, MLME_START_REQ):
         PACK(m80211_mlme_start_req_t);
         break;

      case X(MGMT, MLME_SET_KEY_REQ):
         PACK(m80211_mlme_set_key_req_t);
         break;

      case X(MGMT, MLME_DELETE_KEY_REQ):
         PACK(m80211_mlme_delete_key_req_t);
         break;

      case X(MGMT, MLME_SET_PROTECTION_REQ):
         PACK(m80211_mlme_set_protection_req_t);
         break;

      case X(MGMT, NRP_MLME_BSS_LEAVE_REQ):
         PACK(m80211_nrp_mlme_bss_leave_req_t);
         break;

#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
      case X(MGMT, NRP_MLME_IBSS_LEAVE_REQ):
         PACK(m80211_nrp_mlme_ibss_leave_req_t);
         break;
#endif

      case X(MGMT,  NRP_MLME_SETSCANPARAM_REQ):
         PACK(m80211_nrp_mlme_set_scanparam_req_t);  
         break;               

      case X(MGMT,  NRP_MLME_ADD_SCANFILTER_REQ):
         PACK(m80211_nrp_mlme_add_scanfilter_req_t);  
         break;               

      case X(MGMT,  NRP_MLME_REMOVE_SCANFILTER_REQ):
         PACK(m80211_nrp_mlme_remove_scanfilter_req_t);  
         break;               

      case X(MGMT,  NRP_MLME_ADD_SCANJOB_REQ):
         PACK(m80211_nrp_mlme_add_scanjob_req_t);  
         break;               

      case X(MGMT,  NRP_MLME_REMOVE_SCANJOB_REQ):
         PACK(m80211_nrp_mlme_remove_scanjob_req_t);  
         break;              
 
      case X(MGMT,  NRP_MLME_GET_SCANFILTER_REQ):
         PACK(m80211_nrp_mlme_get_scanfilter_req_t);  
         break;               

      case X(MGMT,  NRP_MLME_SET_SCANJOBSTATE_REQ):
         PACK(m80211_nrp_mlme_set_scanjobstate_req_t);  
         break;               

      case X(MGMT,  NRP_MLME_SET_SCANCOUNTRYINFO_REQ):
         PACK(m80211_nrp_mlme_set_scancountryinfo_req_t);  
         break;               

      case X(MGMT, NRP_MLME_WMM_PS_PERIOD_START_REQ):
         PACK(m80211_nrp_mlme_wmm_ps_period_start_req_t);
         break; 

#if (DE_CCX == CFG_INCLUDED)
      case X(MGMT, NRP_MLME_ADDTS_REQ):
         PACK(m80211_nrp_mlme_addts_req_t);
         break;

      case X(MGMT, NRP_MLME_DELTS_REQ):
         PACK(m80211_nrp_mlme_delts_req_t);
         break;

       case X(MGMT, NRP_MLME_GET_FW_STATS_REQ):
           PACK(m80211_nrp_mlme_fw_stats_req_t);
           break;
#endif //DE_CCX
      case X(CONSOLE, HIC_MAC_CONSOLE_REQ):
         PACK(hic_mac_console_req_t);
         break; 

      case X(CTRL, HIC_CTRL_INTERFACE_DOWN):
         PACK(hic_ctrl_interface_down_t);
         break;

      case X(CTRL, HIC_CTRL_HIC_VERSION_REQ):
         PACK(hic_ctrl_heartbeat_req_t);
         break;

      case X(CTRL, HIC_CTRL_HEARTBEAT_REQ):
         PACK(hic_ctrl_heartbeat_req_t);
         break;

      case X(CTRL, HIC_CTRL_SET_ALIGNMENT_REQ):
         PACK(hic_ctrl_set_alignment_req_t);
         break;

      case X(CTRL, HIC_CTRL_INIT_COMPLETED_REQ):
         PACK(hic_ctrl_init_completed_req_t);
         break;         
      case X(CTRL, HIC_CTRL_SCB_ERROR_REQ):
         PACK(hic_ctrl_scb_error_req_t);
         break;

      case X(CTRL, HIC_CTRL_SLEEP_FOREVER_REQ):
         PACK(hic_ctrl_sleep_forever_req_t);               
         break;               

      case X(CTRL, HIC_CTRL_COMMIT_SUICIDE):
         PACK(hic_ctrl_commit_suicide_req_t);               
         break;

      case X(CTRL, HIC_CTRL_HL_SYNC_REQ):
         PACK(hic_ctrl_hl_sync_req_t);
         break;

      case X(MIB, MLME_GET_REQ):
         PACK(mlme_mib_get_req_t);
         break;

      case X(MIB, MLME_GET_RAW_REQ):
         PACK(mlme_mib_get_raw_req_t);
         break;         
               
      case X(MIB, MLME_SET_REQ):
         PACK(mlme_mib_set_req_t);
         break;

      case X(MIB, MLME_SET_RAW_REQ):
         PACK(mlme_mib_set_raw_req_t);
         break;

      case X(MIB, MLME_GET_NEXT_REQ):
         PACK(mlme_mib_get_next_req_t);
         break;

      case X(MIB, MLME_MIB_SET_TRIGGER_REQ):
         PACK(mlme_mib_set_trigger_req_t);
         break;

      case X(MIB, MLME_MIB_REMOVE_TRIGGER_REQ):
         PACK(mlme_mib_remove_trigger_req_t);
         break;

      case X(MIB, MLME_MIB_SET_GATINGTRIGGER_REQ):
         PACK(mlme_mib_set_gatingtrigger_req_t);
         break;

      case X(DLM, HIC_DLM_LOAD_REQ):
         PACK(hic_dlm_load_req_t);
         break;
      case X(DLM, HIC_DLM_LOAD_FAILED_IND):
         PACK(hic_dlm_load_failed_ind_t);
         break;         
      default:
         DE_BUG_ON(1, "unknown message type %x.%x\n", 
                   message_type, message_id);
   }

#undef PACK
   return (int)(blob_p->index - size);
}

/*!
 * Pack a message struct into a HIC message.
 *
 * @param msg_ref Packing context.
 * @return
 *  - TRUE on success.
 *  - FALSE on failure.
 */
int packer_HIC_Pack(hic_message_context_t* msg_ref)
{
   Blob_t     blob;
   char*      hic_msg_p;
   uint32_t   hic_size       = 0;
   uint32_t   payload_size   = 0;
   uint32_t   nr_bytes_added;

   /* A hic message will always contain a header. */
   hic_size = wifiEngineState.config.tx_hic_hdr_size;

   /* Calculate payload size. */
   INIT_BLOB(&blob, NULL, BLOB_UNKNOWN_SIZE);
   PackMessage(msg_ref->msg_type, msg_ref->msg_id, msg_ref->raw, &blob, ACTION_GET_SIZE);
   payload_size = BLOB_CURRENT_SIZE(&blob);

   /* Add payload to meassge size. */
   hic_size += payload_size;
   
   /* Calculate alignment for the message. */
   nr_bytes_added = WiFiEngine_GetPaddingLength(hic_size);

   /* Allocate an apropriatly sized buffer, including padding. */
   hic_msg_p = (char*)DriverEnvironment_TX_Alloc(hic_size + nr_bytes_added);
   if (NULL == hic_msg_p)
   {
      return FALSE;
   }

   HIC_MESSAGE_LENGTH_SET(hic_msg_p, hic_size + nr_bytes_added);
   HIC_MESSAGE_PADDING_SET(hic_msg_p, nr_bytes_added);
   
   HIC_MESSAGE_TYPE(hic_msg_p)      = msg_ref->msg_type;
   HIC_MESSAGE_ID(hic_msg_p)        = msg_ref->msg_id;
   HIC_MESSAGE_HDR_SIZE(hic_msg_p)  = wifiEngineState.config.tx_hic_hdr_size;

   /* Initiate the packing context. */
   INIT_BLOB(&blob, hic_msg_p, hic_size);
   
   /* Skip header, since it's already filled in. */
   BLOB_SKIP(&blob, wifiEngineState.config.tx_hic_hdr_size);
   
   /* Pack the message payload. */
   PackMessage(msg_ref->msg_type, msg_ref->msg_id, msg_ref->raw, &blob, ACTION_PACK);

   /* Update mesage reference . */
   msg_ref->packed_size = hic_size + nr_bytes_added;  
   msg_ref->packed      = hic_msg_p;     
   
   return TRUE;
}

/*! Unpack and strip a HIC header */
void packer_HIC_Unpack(hic_message_context_t* msg_ref, Blob_t *blob)
{
   m80211_mlme_host_header_t  hic_header;
         
   /* Initiate the context for unpacking of header. 
      Before unpacking the header we do not know the size of the frame (or the header).
      A safe guess is that the header is equal or less than the corresponding struct. 
    */
   HicWrapper_m80211_mlme_host_header_t(&hic_header, blob, ACTION_UNPACK);

   /* Update the length of the unpacked buffer. */
   BLOB_BUF_RESIZE(blob, hic_header.length + sizeof(hic_header.length) 
                   - hic_header.hic.nr_padding_bytes_added);

   msg_ref->msg_type = hic_header.hic.type & ~MAC_API_MSG_DIRECTION_BIT;
   msg_ref->msg_id = hic_header.hic.id;
}

/*! Unpack a msg_ref where raw is allocated and packed points to after the HIC header */
int packer_Unpack(hic_message_context_t *msg_ref, Blob_t *blob)
{
   /* For messages with dynamic allocated memberers to be correctly linked, 
    * a reference to the main structure must be stored in the Blob. 
    * This is done by calling the attach structure macro.
    * Note that msg_ref->raw _must_ have been allocated with WrapperAllocStructure!
    */
   BLOB_ATTACH_WRAPPER_STRUCTURE(blob, msg_ref->raw);
#ifdef PRINT_TRANS_ID
#define UNPACK(T) HicWrapper_##T((T*)msg_ref->raw, blob, ACTION_UNPACK); \
   DE_TRACE_INT(TR_WEI, "RX trans_id %X\n", ((T*)msg_ref->raw)->trans_id)
#else
#define UNPACK(T) HicWrapper_##T((T*)msg_ref->raw, blob, ACTION_UNPACK);
#endif

   if(msg_ref->msg_type == HIC_MESSAGE_TYPE_AGGREGATION)
      return TRUE;

   if(msg_ref->msg_type == HIC_MESSAGE_TYPE_FLASH_PRG)
      return TRUE;

   /* Call the appropriate function for unpacking. */
   switch ((msg_ref->msg_type << 8) | msg_ref->msg_id) {
      case X(MGMT, MLME_RESET_CFM):
         UNPACK(m80211_mlme_reset_cfm_t);
         break;

      case X(MGMT, MLME_SCAN_CFM):
         UNPACK(mlme_direct_scan_cfm_t);
         break;

      case X(MGMT, MLME_SCAN_IND):
         UNPACK(m80211_mlme_scan_ind_t);
         break;

      case X(MGMT, MLME_POWER_MGMT_CFM):
         UNPACK(m80211_mlme_power_mgmt_cfm_t);   
         break;

      case X(MGMT, MLME_JOIN_CFM):
         UNPACK(m80211_mlme_join_cfm_t);   
         break;

      case X(MGMT, MLME_AUTHENTICATE_CFM):
         UNPACK(m80211_mlme_authenticate_cfm_t);   
         break;

      case X(MGMT, MLME_AUTHENTICATE_IND):
         /* The macro does not like typedefs... */
         /* UNPACK(m80211_mlme_authenticate_ind_t); */
         UNPACK(m80211_mlme_addr_and_short_ind_t);   
         break;

      case X(MGMT, MLME_DEAUTHENTICATE_CFM):
         UNPACK(m80211_mlme_deauthenticate_cfm_t);   
         break;
               
      case X(MGMT, MLME_DEAUTHENTICATE_IND):
         UNPACK(m80211_mlme_deauthenticate_ind_t);   
         break;

      case X(MGMT, MLME_ASSOCIATE_CFM):
         UNPACK(m80211_mlme_associate_cfm_t);   
         break;

      case X(MGMT, MLME_ASSOCIATE_IND):
         UNPACK(m80211_mlme_associate_ind_t);   
         break;

      case X(MGMT, MLME_REASSOCIATE_CFM):
         UNPACK(m80211_mlme_reassociate_cfm_t);   
         break;      

      case X(MGMT, MLME_REASSOCIATE_IND):
         UNPACK(m80211_mlme_reassociate_ind_t);   
         break;

      case X(MGMT, MLME_DISASSOCIATE_CFM):
         UNPACK(m80211_mlme_disassociate_cfm_t);   
         break;       

      case X(MGMT, MLME_DISASSOCIATE_IND):
         UNPACK(m80211_mlme_disassociate_ind_t);   
         break;

      case X(MGMT, MLME_START_CFM):
         UNPACK(m80211_mlme_start_cfm_t);   
         break;

      case X(MGMT, MLME_SET_KEY_CFM):
         UNPACK(m80211_mlme_set_key_cfm_t);   
         break;

      case X(MGMT, MLME_DELETE_KEY_CFM):
         UNPACK(m80211_mlme_delete_key_cfm_t);   
         break;

      case X(MGMT, MLME_SET_PROTECTION_CFM):
         UNPACK(m80211_mlme_set_protection_cfm_t);   
         break;

      case X(MGMT, MLME_MICHAEL_MIC_FAILURE_IND):
         UNPACK(m80211_mlme_michael_mic_failure_ind_t);   
         break;

      case X(MGMT, NRP_MLME_BSS_LEAVE_CFM):
         UNPACK(m80211_nrp_mlme_bss_leave_cfm_t);   
         break;

#if (DE_NETWORK_SUPPORT & CFG_NETWORK_IBSS)
      case X(MGMT, NRP_MLME_IBSS_LEAVE_CFM):
         UNPACK(m80211_nrp_mlme_ibss_leave_cfm_t);   
         break;
#endif
      case X(MGMT, NRP_MLME_PEER_STATUS_IND):
         UNPACK(m80211_nrp_mlme_peer_status_ind_t);   
         break;

      case X(MGMT, NRP_MLME_WMM_PS_PERIOD_START_CFM):
         UNPACK(m80211_nrp_mlme_wmm_ps_period_start_cfm_t);   
         break;

      case X(MGMT,  NRP_MLME_SETSCANPARAM_CFM):
         UNPACK(m80211_nrp_mlme_set_scanparam_cfm_t);  
         break;               

      case X(MGMT,  NRP_MLME_ADD_SCANFILTER_CFM):
         UNPACK(m80211_nrp_mlme_add_scanfilter_cfm_t);  
         break;               

      case X(MGMT,  NRP_MLME_REMOVE_SCANFILTER_CFM):
         UNPACK(m80211_nrp_mlme_remove_scanfilter_cfm_t);  
         break;               

      case X(MGMT,  NRP_MLME_ADD_SCANJOB_CFM):
         UNPACK(m80211_nrp_mlme_add_scanjob_cfm_t);  
         break;               

      case X(MGMT,  NRP_MLME_REMOVE_SCANJOB_CFM):
         UNPACK(m80211_nrp_mlme_remove_scanjob_cfm_t);  
         break;              
 
      case X(MGMT,  NRP_MLME_GET_SCANFILTER_CFM):
         UNPACK(m80211_nrp_mlme_get_scanfilter_cfm_t);  
         break;               

      case X(MGMT,  NRP_MLME_SET_SCANJOBSTATE_CFM):
         UNPACK(m80211_nrp_mlme_set_scanjobstate_cfm_t);  
         break;               

      case X(MGMT, NRP_MLME_SCANNOTIFICATION_IND):
         UNPACK(m80211_nrp_mlme_scannotification_ind_t);  
         break;               

      case X(MGMT, NRP_MLME_SET_SCANCOUNTRYINFO_CFM):
         UNPACK(m80211_nrp_mlme_set_scancountryinfo_cfm_t);  
         break;  

#if (DE_CCX == CFG_INCLUDED)
      case X(MGMT, NRP_MLME_ADDTS_CFM):
         UNPACK(m80211_nrp_mlme_addts_cfm_t);
         break;

      case X(MGMT, NRP_MLME_ADDTS_IND):
         UNPACK(m80211_nrp_mlme_addts_ind_t);
         break;

      case X(MGMT, NRP_MLME_DELTS_CFM):
         UNPACK(m80211_nrp_mlme_delts_cfm_t);
         break;

      case X(MGMT, NRP_MLME_GET_FW_STATS_CFM):
         UNPACK(m80211_nrp_mlme_fw_stats_cfm_t);
         break;
#endif //DE_CCX
      case X(CONSOLE, HIC_MAC_CONSOLE_CFM):
         UNPACK(hic_mac_console_cfm_t);   
         break;

      case X(CONSOLE, HIC_MAC_CONSOLE_IND):
         UNPACK(hic_mac_console_ind_t);   
         break;    


      case X(CTRL, HIC_CTRL_WAKEUP_IND):
         UNPACK(hic_ctrl_wakeup_ind_t);   
         break;               

      case X(CTRL, HIC_CTRL_HIC_VERSION_CFM):
         UNPACK(hic_ctrl_version_cfm_t);
         break;

      case X(CTRL, HIC_CTRL_HEARTBEAT_CFM):
         UNPACK(hic_ctrl_heartbeat_cfm_t);
         break;

      case X(CTRL, HIC_CTRL_HEARTBEAT_IND):
         UNPACK(hic_ctrl_heartbeat_ind_t);
         break;

      case X(CTRL, HIC_CTRL_SET_ALIGNMENT_CFM):
         UNPACK(hic_ctrl_set_alignment_cfm_t);
         break;

      case X(CTRL, HIC_CTRL_SCB_ERROR_CFM):
         UNPACK(hic_ctrl_scb_error_cfm_t);
         break;

      case X(CTRL, HIC_CTRL_SCB_ERROR_IND):
         UNPACK(hic_ctrl_scb_error_ind_t);
         break;               

      case X(CTRL, HIC_CTRL_SLEEP_FOREVER_CFM):
         UNPACK(hic_ctrl_sleep_forever_cfm_t);   
         break;              

      case X(CTRL, HIC_CTRL_INIT_COMPLETED_CFM):
         UNPACK(hic_ctrl_init_completed_cfm_t);   
         break;         

      case X(CTRL, HIC_CTRL_HL_SYNC_CFM):
         UNPACK(hic_ctrl_hl_sync_cfm_t);
         break;

      case X(MIB, MLME_GET_CFM):
         DE_MEMSET(msg_ref->raw, 0, msg_ref->raw_size);
         UNPACK(mlme_mib_get_cfm_t);
         break;

      case X(MIB, MLME_SET_CFM):
         UNPACK(mlme_mib_set_cfm_t);   
         break;

      case X(MIB, MLME_MIB_SET_TRIGGER_CFM):
         UNPACK(mlme_mib_set_trigger_cfm_t);   
         break;

      case X(MIB, MLME_MIB_REMOVE_TRIGGER_CFM):
         UNPACK(mlme_mib_remove_trigger_cfm_t);   
         break;

      case X(MIB, MLME_MIB_TRIGGER_IND):
         UNPACK(mlme_mib_trigger_ind_t);   
         break;

      case X(MIB, MLME_MIB_SET_GATINGTRIGGER_CFM):
         UNPACK(mlme_mib_set_gatingtrigger_cfm_t);   
         break;

      case X(DLM, HIC_DLM_LOAD_CFM):
         UNPACK(hic_dlm_load_cfm_t);
         break;

      case X(DLM, HIC_DLM_SWAP_IND):
         UNPACK(hic_dlm_swap_ind_t);
         break;

      default:
         DE_TRACE_INT2(TR_SEVERE, "Got unknown message_type (%x.%x)\n", msg_ref->msg_type, msg_ref->msg_id );
         return FALSE;
   }
#undef UNPACK

   return TRUE;
}


char* packer_DereferencePacket(driver_packet_ref packet, 
                               uint16_t*         packetSize, 
                               uint8_t*          packetType,
                               uint8_t*          packetId)
{
   Blob_t                     blob;
   m80211_mlme_host_header_t  hic_header;
   char*                      hic_frame_p;         
   uint8_t                    nr_padding_bytes_added;
   
   hic_frame_p = (char*)packet;
   /* Initiate the context for unpacking of the header. */
   INIT_BLOB(&blob, hic_frame_p, sizeof(m80211_mlme_host_header_t));
   HicWrapper_m80211_mlme_host_header_t(&hic_header, &blob, ACTION_UNPACK);
   
   /* Get packet tail padding from header */
   nr_padding_bytes_added = hic_header.hic.nr_padding_bytes_added;
   /* Update the length of the unpacked buffer. */
   BLOB_BUF_RESIZE(&blob, hic_header.length + sizeof(hic_header.length) 
                                            - nr_padding_bytes_added);

   if (packetSize != NULL)
   {
      *packetSize =  BLOB_BUF_SIZE(&blob) - BLOB_CURRENT_SIZE(&blob);
   }
   if (packetType!= NULL)
   {
      *packetType = hic_header.hic.type;
   }
   if (packetId != NULL)
   {
      *packetId = hic_header.hic.id;
   }

   /* Return start of the payload. */
   return BLOB_CURRENT_POS(&blob);
} /* packer_DereferencePacket */



/** @} */ /* End of packer group */
