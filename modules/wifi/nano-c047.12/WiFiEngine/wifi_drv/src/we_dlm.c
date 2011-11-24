/* $Id: $ */
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
/** @defgroup we_dlm dlm interface
 *
 * @brief WiFiEngine interface to fw Dynamic Loadable Modules
 *
 * Used for WPA/WAPI/...
 *
 *  @{
 */

#include "driverenv.h"
#include "we_dlm.h"
#include "wifi_engine_internal.h"

void we_dlm_dynamic_initialize(void);
void we_dlm_dynamic_shutdown(void);

static struct dlm_ops_t* default_find(uint32_t id, uint32_t size)
{
   id=id;
   size=size;
   DE_TRACE_STATIC(TR_SEVERE, "No DLM-handler installed\n");
   return NULL;
}

static struct {
   we_ps_control_t *ps_ctrl;
   dlm_ops_find find;
   struct dlm_ops_t* ops;
   uint32_t checksum;
   uint32_t offset;
   uint32_t size;
} dlm_state = {0, default_find, NULL, 0,0,0};

static uint32_t update_checksum(uint32_t checksum, char *data, size_t len)
{
   while(len>0)
   {
      checksum += (unsigned char)*data;
      data++;
      len--;
   }
   return checksum;
}

void we_dlm_initialize(void)
{
   DE_MEMSET(&dlm_state, 0, sizeof(dlm_state));
   we_dlm_dynamic_initialize();
   dlm_state.ps_ctrl = WiFiEngine_PSControlAlloc("DLM");
}

void we_dlm_shutdown(void)
{
   we_dlm_dynamic_shutdown();

   /* may not be needed */
   WiFiEngine_PSControlFree(dlm_state.ps_ctrl);
}

void we_dlm_register_adapter(dlm_ops_find cb)
{
   if(cb == NULL)
      cb = default_find;
   
   dlm_state.find = cb;
   dlm_state.ops = NULL;
}

static void create_dlm_load_fail_ind(hic_message_context_t *msg_ref)
{
   Mlme_CreateMessageContext(*msg_ref);
   
   msg_ref->msg_type = HIC_MESSAGE_TYPE_DLM;
   msg_ref->msg_id = HIC_DLM_LOAD_FAILED_IND;  

   (void)HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_dlm_load_failed_ind_t);   
}

static void create_dlm_req(
      hic_message_context_t *msg_ref,
      hic_dlm_load_req_t **req_pp,
      uint32_t remaining,
      uint32_t addr)
{
   hic_dlm_load_req_t *req;
   int len;

   Mlme_CreateMessageContext(*msg_ref);
   msg_ref->msg_type = HIC_MESSAGE_TYPE_DLM;
   msg_ref->msg_id = HIC_DLM_LOAD_REQ;

   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, msg_ref, hic_dlm_load_req_t);

   req->page.size = DE_MIN(remaining,(uint32_t)1400);
   len = (*dlm_state.ops->get_data)(
            dlm_state.ops,
            dlm_state.offset,
            req->page.size,
            &req->page.ref,
            &req->remaining_size);

   DE_ASSERT(len == req->page.size);

   req->address = addr;
   req->remaining_size = remaining - req->page.size;
   req->reserved = 0;

   req->checksum = update_checksum(
         dlm_state.checksum,
         req->page.ref,
         req->page.size);

   *req_pp = req;
}

static void create_dlm_req_and_update_state(
      hic_message_context_t *msg_ref,
      hic_dlm_load_req_t **req_pp,
      uint32_t remaining,
      uint32_t addr)
{
   create_dlm_req(msg_ref, req_pp, addr, remaining);

   dlm_state.offset += (*req_pp)->page.size;
   dlm_state.checksum = (*req_pp)->checksum;
}

static void dlm_send(hic_message_context_t *msg_ref)
{
   wei_unconditional_send_cmd(msg_ref);
   Mlme_ReleaseMessageContext(*msg_ref);
}


static void send_and_free(hic_message_context_t *msg_ref,
      hic_dlm_load_req_t *req)
{
   wei_unconditional_send_cmd(msg_ref);
   if(dlm_state.ops->free_data)
   {
      (*dlm_state.ops->free_data)(dlm_state.ops, req->page.ref);
   }
   Mlme_ReleaseMessageContext(*msg_ref);
}

static void handle_dlm_swap_ind(hic_dlm_swap_ind_t *ind)
{
   hic_message_context_t msg_ref;
   hic_dlm_load_req_t *req;

   WiFiEngine_InhibitPowerSave(dlm_state.ps_ctrl);


   dlm_state.size = ind->size;
   dlm_state.checksum = 0;
   dlm_state.offset = 0;
   dlm_state.ops = dlm_state.find(
         ind->load_memory_address,
         ind->size);

   if(dlm_state.ops == NULL)
   {
      DE_TRACE_INT2(TR_SEVERE, "failed to find DLM a:%x s:%u\n",
         ind->load_memory_address, ind->size);

      create_dlm_load_fail_ind(&msg_ref);
      dlm_send(&msg_ref);
      WiFiEngine_AllowPowerSave(dlm_state.ps_ctrl);
   }
   else
   {
      create_dlm_req_and_update_state( 
            &msg_ref,
            &req, 
            ind->load_memory_address,
            ind->size);

      DE_TRACE_INT4(TR_INITIALIZE, "DLM_REQ a:%x s:%u rs:%u c:%x\n",
            req->address,
            req->page.size,
            req->remaining_size,
            req->checksum);

      send_and_free(&msg_ref,req);
   }
}


static void handle_dlm_load_cfm(hic_dlm_load_cfm_t *cfm)
{
   hic_message_context_t msg_ref;
   hic_dlm_load_req_t *req;

   DE_ASSERT(dlm_state.ops);

   if(cfm->remaining_size == 0)
   {
      /* done */
      WiFiEngine_AllowPowerSave(dlm_state.ps_ctrl);
      return;
   }

   create_dlm_req_and_update_state( 
         &msg_ref,
         &req, 
         cfm->address,
         cfm->remaining_size);

   DE_TRACE_INT4(TR_INITIALIZE, "DLM_REQ a:%x s:%u rs:%u c:%x\n",
         req->address,
         req->page.size,
         req->remaining_size,
         req->checksum);

   send_and_free(&msg_ref,req);
}


void wei_handle_dlm_pkt(hic_message_context_t *msg_ref)
{
   switch(msg_ref->msg_id)
   {
      case HIC_DLM_LOAD_CFM:
         handle_dlm_load_cfm(HIC_GET_RAW_FROM_CONTEXT(msg_ref, hic_dlm_load_cfm_t));
         break;

      case HIC_DLM_SWAP_IND:
         handle_dlm_swap_ind(HIC_GET_RAW_FROM_CONTEXT(msg_ref, hic_dlm_swap_ind_t));
         break;

      default:
         DE_TRACE_INT(TR_WARN, "unknown DLM id: %x\n",msg_ref->msg_id);
         DE_ASSERT(FALSE);
   }
}

