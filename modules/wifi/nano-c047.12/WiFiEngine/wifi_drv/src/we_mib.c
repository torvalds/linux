/* $Id: we_mib.c,v 1.115 2008-05-19 15:03:23 peek Exp $ */
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
This module implements the WiFiEngine MIB interface

*****************************************************************************/
/** @defgroup we_mib WiFiEngine MIB interface
 *
 * @brief WiFiEngine MIB interface and support functions
 *
 * There are two alternative MIB interfaces. The first one is based on
 * transaction IDs. A MIB Set/Get request is posted
 * (WiFiEngine_SendMIBGet()/WiFiEngine_SendMIBSet()) and an identifier
 * is return in the posting call. When the reply from the hardware is
 * received by WiFiEngine it will queue the reply internally and can
 * be retrieved using WiFiEngine_GetMIBResponse(). The internal
 * reply queue has a limited size so if the MIB activity is high
 * and the reply retrieval call is late the reply may have been discarded.
 *
 * The preferred interface is the asynchronous callback based interface
 * (WiFiEngine_GetMIBAsynch()) which invoke a caller-supplied callback
 * when the reply is received from the hardware.
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
#include "we_dlm.h"

static int cache_add_mib(mib_id_t mib_id, const void *inbuf, size_t inbuflen);

char *mib_table = NULL;

struct mib_cfm_container {
   uint32_t idnum;
   uint8_t  msgId;
   size_t mib_object_size;
   /* The set confirm looks the same, except that it lacks the
      identifier and value at the end, so we "overload" the set
      confirm with the get confirm struct */
   mlme_mib_get_cfm_t reply;
   void *raw;
   size_t raw_size;
   WEI_TQ_ENTRY(mib_cfm_container) next;
};

static WEI_TQ_HEAD(, mib_cfm_container) mib_cfm_list = WEI_TQ_HEAD_INITIALIZER(mib_cfm_list);
static size_t mib_cfm_list_size;

static struct mib_cfm_container*
find_mib_reply(uint32_t id)
{
   struct mib_cfm_container *cfm;
   
   WEI_TQ_FOREACH(cfm, &mib_cfm_list, next) {
      if(cfm->idnum == id)
         return cfm;
   }
   return NULL;
}



/* the guts of sending a mib get. caller must free msg_ref, if call is
 * successful */
static int mib_get(hic_message_context_t *msg_ref, 
                   mib_id_t *mib_id,
                   uint32_t *num)
{
   uint32_t tid;

   Mlme_CreateMessageContext(*msg_ref);
#if DE_MIB_TABLE_SUPPORT == CFG_ON
   if (wei_have_mibtable())
   {
      if (Mlme_CreateMIBGetRawRequest(msg_ref, *mib_id, &tid) == 0) {
         //DE_BUG_ON(1, "CreateMIBGetRequest() failed in WiFiEngine_SendMIBGet()\n");
         Mlme_ReleaseMessageContext(*msg_ref);
         return WIFI_ENGINE_FAILURE;
      }
   }
   else
   {
      if (Mlme_CreateMIBGetRequest(msg_ref, *mib_id, &tid) == 0) {
         DE_BUG_ON(1, "CreateMIBGetRequest() failed in WiFiEngine_SendMIBGet()\n");
         Mlme_ReleaseMessageContext(*msg_ref);
         return WIFI_ENGINE_FAILURE;
      }
   }
#else
   if (Mlme_CreateMIBGetRequest(msg_ref, *mib_id, &tid) == 0) {
      Mlme_ReleaseMessageContext(*msg_ref);
      return WIFI_ENGINE_FAILURE;
   }
#endif /* DE_MIB_TABLE_SUPPORT */
   
   if(num)
      *num = tid;

   return WIFI_ENGINE_SUCCESS;
}


static int wei_mib_ascii2bin(mib_id_t *dst, const char *src)
{
   const char *p;
   int seen_digit = 0;
   unsigned int val;
   char *dp;

   DE_MEMSET(dst, 0, sizeof(*dst));
   dp = dst->octets;
   val = 0;
   for(p = src; *p != '\0' ;p++) {
      if(*p >= '0' && *p <= '9') {
         val = val * 10 + *p - '0';
         if(val > 127) {
            DE_TRACE_INT(TR_MIB, "MIB component too large %u\n", val);
            return FALSE;
         }
         if(dp - dst->octets >= sizeof(dst->octets)) {
            DE_TRACE_STATIC(TR_MIB, "too many MIB components\n");
            return FALSE;
         }
         seen_digit = 1;
      } 
      else if(*p == '.' || *p == ' ' || *p == '\t') {
         if(seen_digit) {
            *dp++ = (char)val;
            seen_digit = 0;
            val = 0;
         }
      } 
      else {
         DE_TRACE_STATIC(TR_MIB, "bad character in MIB string\n");
         return FALSE;
      }
   }
   if(seen_digit) {
      *dp++ = (char)val;
      seen_digit = 0;
   }
   return dp > dst->octets;
}

static int wei_mib_is_valid(const mib_id_t *mib_id)
{
   mib_object_entry_t mib_object;
   
   if(wei_have_mibtable()
      && wei_get_mib_object(mib_id, &mib_object) != WIFI_ENGINE_SUCCESS) {
      return FALSE;
   }

   return TRUE;
}

char *wei_print_mib(const mib_id_t *mib_id, char *str, size_t size)
{
   unsigned int i;
   size_t len;

   len = DE_SNPRINTF(str, size, "%u", (unsigned char)mib_id->octets[0]);
   for(i = 1; i < MIB_IDENTIFIER_MAX_LENGTH && mib_id->octets[i] != 0; i++) {
      len += DE_SNPRINTF(str + len, size - DE_MIN(size,len), ".%u", 
                         (unsigned char)mib_id->octets[i]);
   }
   return str;
}


/*!
 * @brief Send a MIB query
 *
 * Posts a MIB query to the hardware. The response data can 
 * be retrieved by a matching WiFiEngine_GetMIBResponse().
 *
 * @param mib_id A binary string identifying the MIB variable to be set.
 * @param num Pointer to the queue id number of the query. This should
 * be used when retrieving the result later.
 * 
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_RESOURCES if the response queue is full or
 *  or if a command is outstanding
 * - WIFI_ENGINE_FAILURE on failure
 */
int wei_send_mib_get_binary(mib_id_t mib_id, uint32_t *num)
{
   hic_message_context_t   msg_ref;
   int                status;
   
   BAIL_IF_UNPLUGGED;
   status = mib_get(&msg_ref, &mib_id, num);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   status = wei_send_cmd(&msg_ref);
   Mlme_ReleaseMessageContext(msg_ref);

   if (status == WIFI_ENGINE_FAILURE_DEFER) 
   {
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   else if (status != WIFI_ENGINE_SUCCESS) {
      DE_BUG_ON(1, "wei_send_cmd() failed\n");
   }
   return status;
}


/*!
 * @brief Send a MIB query
 *
 * Posts a MIB query to the hardware. The response data can 
 * be retrieved by a matching WiFiEngine_GetMIBResponse().
 *
 * @param id A string identifying the MIB variable to be queried.
 * @param num Pointer to the queue id number of the query. This should
 * be used when retrieving the result later.
 * 
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_RESOURCES if the response queue is full or
 *  or if a command is outstanding
 * - WIFI_ENGINE_FAILURE on failure
 */
int   WiFiEngine_SendMIBGet(const char *id, uint32_t *num)
{
   mib_id_t           mib_id;
   
   if(!wei_mib_ascii2bin(&mib_id, id)) {
      /* we can't easily print the string here, since it may not be
       * zero terminated */
      DE_TRACE_STATIC(TR_MIB, "failed to parse mib id\n");
      return WIFI_ENGINE_FAILURE;
   }
   return wei_send_mib_get_binary(mib_id, num);
}

/*!
 * @internal
 * @brief Query all MIBs in sequence.
 *
 * Posts a MIB query to the hardware. The response data can 
 * be retrieved by a matching WiFiEngine_GetMIBResponse().
 *
 * @param [in]  get_first If TRUE, get first mib, else get next mib.
 * @param [out] trans_id  Pointer to the transaction id number of the
 *                        query. This should be used when retrieving 
 *                        the result later.
 *
 * @note This function is only usful with x_test firmware.
 * 
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE_RESOURCES if the response queue is full or
 *                                       or if a command is outstanding
 * @retval WIFI_ENGINE_FAILURE on failure
 */
int wei_send_mib_get_next(bool_t get_first, uint32_t *trans_id)
{
   hic_message_context_t   msg_ref;
   int                status;
   mlme_mib_get_next_req_t *req;
   
   BAIL_IF_UNPLUGGED;

   Mlme_CreateMessageContext(msg_ref);

   /* Allocate a context buffer of the appropriate size. */
   req = HIC_ALLOCATE_RAW_CONTEXT(NULL, (&msg_ref), mlme_mib_get_next_req_t);
   if (req == NULL)
   {
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE;
   }
   msg_ref.msg_type = HIC_MESSAGE_TYPE_MIB;
   msg_ref.msg_id = MLME_GET_NEXT_REQ;
   
   WEI_SET_TRANSID(req->trans_id);
   if (trans_id != NULL)
   {
      *trans_id = req->trans_id;
   }
   req->getFirst = get_first;

   status = wei_send_cmd(&msg_ref);
   Mlme_ReleaseMessageContext(msg_ref);

   if (status == WIFI_ENGINE_FAILURE_DEFER) 
   {
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }

   DE_ASSERT(status == WIFI_ENGINE_SUCCESS);

   return status;
}

/*!
 * @brief Send a MIB set command with binary MIB id.
 *
 * Posts a MIB set command to the hardware. The response data can 
 * be retrieved by a matching WiFiEngine_GetMIBResponse().
 * 
 * Beware that setting some variables may have unintended consequences.
 * For instance, setting the MAC address of the adapter through MIB set 
 * will not change the MAC address in the driver (in the driver
 * registry) and confusion may ensue.
 *
 * @param mib_id A binary string identifying the MIB variable to be set.
 * @param num Pointer to the queue id number of the query. This should
 * be used when retrieving the result later.
 * @param inbuf Input buffer
 * @param inbuflen Length of the input buffer
 * @param cbc Optional callback container. This parameter can be NULL.
 *            Pointer to a caller-allocated callback-container.
 *            If non-NULL this should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF this call returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 *            The cbc->status member will contain the set cfm result value
 *            on successful invocation.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_RESOURCES if the a cmd reply is pending
 * - WIFI_ENGINE_FAILURE on failure
 */
int wei_send_mib_set_binary(mib_id_t mib_id,
			    uint32_t *num, 
			    const void *inbuf,
			    size_t inbuflen,
                we_cb_container_t *cbc)
{
   hic_message_context_t  msg_ref;
   mlme_mib_set_req_t     *legacy_req;
   int                    status;
   uint32_t tid;
   bool_t mlme_success = FALSE;
#if DE_MIB_TABLE_SUPPORT == CFG_ON
   bool_t   raw_mib_access = FALSE;
   mlme_mib_set_raw_req_t *raw_req;
#endif

   /* Don't remove this willy-nilly, we rely on WiFiEngine_SetMACAddress() 
    * to work ok even while not plugged */
   BAIL_IF_UNPLUGGED; 
   Mlme_CreateMessageContext(msg_ref);

#if DE_MIB_TABLE_SUPPORT == CFG_ON
   raw_mib_access = wei_have_mibtable();
   if ( raw_mib_access )
      mlme_success = Mlme_CreateMIBSetRawRequest(&msg_ref, mib_id, inbuf, inbuflen, &tid);
   else
#endif
      /* else: conditional if DE_MIB_TABLE_SUPPORT == CFG_ON, else always */
      mlme_success = Mlme_CreateMIBSetRequest(&msg_ref, mib_id, inbuf, inbuflen, &tid);

   if (mlme_success == TRUE)
   {
      if (cbc)
      {
         cbc->trans_id = tid;
         cbc->data = NULL;
         cbc->data_len = 0;
         wei_cb_queue_pending_callback(cbc);
      }
      status = wei_send_cmd(&msg_ref);
      if (status == WIFI_ENGINE_FAILURE_DEFER) 
      {
         DE_TRACE_STATIC(TR_MIB, "MIB set command send was deferred\n");
         Mlme_ReleaseMessageContext(msg_ref);
         return WIFI_ENGINE_FAILURE_RESOURCES;
      }
      else if (status != WIFI_ENGINE_SUCCESS) 
      {
         DE_BUG_ON(1, "wei_send_cmd() failed\n");
      }
   }
   else 
   {
      DE_TRACE_STATIC(TR_MIB, "CreateMIBSetRequest() failed in WiFiEngine_SendMIBSet()\n");
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE;
   }

#if DE_MIB_TABLE_SUPPORT == CFG_ON
   if (raw_mib_access)
   {
      raw_req = HIC_GET_RAW_FROM_CONTEXT(&msg_ref, mlme_mib_set_raw_req_t);
      if (raw_req->value.ref)
      {
         DriverEnvironment_Nonpaged_Free(raw_req->value.ref);
         raw_req->value.ref = NULL;
         raw_req->value.size = 0;
      }
   }
   else
#endif
   {
      legacy_req = HIC_GET_RAW_FROM_CONTEXT(&msg_ref, mlme_mib_set_req_t);
      if (legacy_req->value.ref)
      {
         DriverEnvironment_Nonpaged_Free(legacy_req->value.ref);
         legacy_req->value.ref = NULL;
         legacy_req->value.size = 0;
      }
   }
   Mlme_ReleaseMessageContext(msg_ref);
   if(num)
      *num = tid;

   if(WES_TEST_FLAG(WES_DEVICE_CONFIGURED))
      cache_add_mib(mib_id, inbuf, inbuflen);
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Send a MIB set command
 *
 * Posts a MIB set command to the hardware. The response data can 
 * be retrieved by a matching WiFiEngine_GetMIBResponse().
 * 
 * Beware that setting some variables may have unintended consequences.
 * For instance, setting the MAC address of the adapter through MIB set 
 * will not change the MAC address in the driver (in the driver
 * registry) and confusion may ensue.
 *
 * @param id A string identifying the MIB variable to be set.
 * @param num Pointer to the queue id number of the query. This should
 * be used when retrieving the result later.
 * @param inbuf Input buffer
 * @param inbuflen Length of the input buffer
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_RESOURCES if the a cmd reply is pending
 * - WIFI_ENGINE_FAILURE on failure
 */
int   WiFiEngine_SendMIBSet(const char *id, 
                            uint32_t *num, 
			    const void *inbuf,
                            size_t inbuflen)
{
   mib_id_t mib_id;

   if(!wei_mib_ascii2bin(&mib_id, id)) {
      /* we can't easily print the string here, since it may not be
       * zero terminated */
      DE_TRACE_STATIC(TR_MIB, "failed to parse mib id\n");
      return WIFI_ENGINE_FAILURE;
   }
   return wei_send_mib_set_binary(mib_id, num, inbuf, inbuflen, NULL);
}

/*!
 * @brief Send a MIB set command with asynchronous completion
 *
 * Posts a MIB set command to the hardware. The response data can 
 * be retrieved by a matching WiFiEngine_GetMIBResponse().
 * 
 * Beware that setting some variables may have unintended consequences.
 * For instance, setting the MAC address of the adapter through MIB set 
 * will not change the MAC address in the driver (in the driver
 * registry) and confusion may ensue.
 *
 * @param id A string identifying the MIB variable to be set.
 * @param num Pointer to the queue id number of the query. This should
 * be used when retrieving the result later.
 * @param inbuf Input buffer
 * @param inbuflen Length of the input buffer
 * @param cbc Optional callback container. This parameter can be NULL.
 *            Pointer to a caller-allocated callback-container.
 *            If non-NULL this should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF this call returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 *            The cbc->status member will contain the set cfm result value
 *            on successful invocation.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_RESOURCES if the a cmd reply is pending
 * - WIFI_ENGINE_FAILURE on failure
 */
int   WiFiEngine_SetMIBAsynch(const char *id, 
                             uint32_t *num, 
                             const void *inbuf,
                             size_t inbuflen,
                             we_cb_container_t *cbc)
{
   mib_id_t mib_id;

   if(!wei_mib_ascii2bin(&mib_id, id)) {
      /* we can't easily print the string here, since it may not be
       * zero terminated */
      DE_TRACE_STATIC(TR_MIB, "failed to parse mib id\n");
      return WIFI_ENGINE_FAILURE;
   }
   return wei_send_mib_set_binary(mib_id, num, inbuf, inbuflen, cbc);
}

static struct mib_cfm_container*
get_cfm(uint8_t msgId, 
        mlme_mib_get_cfm_t *reply,
        const void *raw, 
        size_t raw_size)
{
   struct mib_cfm_container *cfm;
   size_t size;
   
   DE_ASSERT(msgId == MLME_GET_CFM || msgId == MLME_SET_CFM);

   size = sizeof(*cfm) + raw_size;
   if (msgId == MLME_GET_CFM && reply->value.size > 0) {
#define ALIGN32(X) (((X) + 3) & ~3)
      DE_ASSERT(reply->value.size <= 4096);
      size = ALIGN32(size);
      size += reply->value.size;
   }

   cfm = (struct mib_cfm_container *)DriverEnvironment_Malloc(size);
   if(cfm == NULL)
      return cfm;

   cfm->msgId = msgId;
   cfm->idnum = reply->trans_id;
   cfm->raw_size = raw_size;
   cfm->raw = (cfm + 1);
   DE_MEMCPY(cfm->raw, raw, cfm->raw_size);
   if(msgId == MLME_GET_CFM) {
      DE_MEMCPY(&cfm->reply, reply, sizeof(mlme_mib_get_cfm_t));
      if((cfm->mib_object_size = cfm->reply.value.size) != 0) {
         cfm->reply.value.ref = ((char*)cfm->raw + ALIGN32(cfm->raw_size));
         DE_MEMCPY(cfm->reply.value.ref, reply->value.ref, cfm->reply.value.size);
      } else
         cfm->reply.value.ref = NULL;
   } else if(msgId == MLME_SET_CFM) {
      DE_MEMSET(&cfm->reply, 0, sizeof(cfm->reply));
      DE_MEMCPY(&cfm->reply, reply, sizeof(mlme_mib_set_cfm_t));
   }
   return cfm;
}

static void
free_mib_container(struct mib_cfm_container *c)
{
   DriverEnvironment_Free(c);
}

static void
wei_prune_mib_reply_list(size_t max_size) 
{
   struct mib_cfm_container *cfm;

   WIFI_LOCK();
   while(mib_cfm_list_size > max_size 
         && (cfm = WEI_TQ_FIRST(&mib_cfm_list)) != NULL) {
      WEI_TQ_REMOVE(&mib_cfm_list, cfm, next);
      mib_cfm_list_size--;
      DE_TRACE_INT(TR_MIB, "removing mib response %d\n", cfm->idnum);
      free_mib_container(cfm);
   }
   WIFI_UNLOCK();
}

void wei_clear_mib_reply_list(void)
{
   wei_prune_mib_reply_list(0);
}

/*!
 * Copies the contents of reply into the MIB get reply queue.
 * reply can be freed after this call.
 * @param msgId The type of message (get or set)
 * @param replyPacked Char array representation of the packed reply (including header)
 * @param replySize Length of the packed reply
 * @return 
 * - 1 on success
 * - 0 on failure
 */
int wei_queue_mib_reply(uint8_t msgId, hic_message_context_t *msg_ref) 
{
   struct mib_cfm_container *rc;
   mlme_mib_get_cfm_t *cfm;

   cfm = HIC_GET_RAW_FROM_CONTEXT(msg_ref, mlme_mib_get_cfm_t);
   
   wei_prune_mib_reply_list(LIST_SIZE);

   rc = get_cfm(msgId, cfm, msg_ref->packed, msg_ref->packed_size);
                
   if(rc == NULL) {
      DE_TRACE_STATIC(TR_MIB, "Failed to allocate buffer for received MIB reply \n");
      return 0;
   }
   
   WIFI_LOCK();
   WEI_TQ_INSERT_TAIL(&mib_cfm_list, rc, next);
   mib_cfm_list_size++;
   WIFI_UNLOCK();

   return 1;
}

int wei_mib_schedule_cb(int msgId, hic_interface_wrapper_t *msg)
{
   we_cb_container_t *cbc;
   size_t len;

   switch (msgId)
   {
      case MLME_GET_CFM:
      {
         mlme_mib_get_cfm_t *cfm;

         cfm = (mlme_mib_get_cfm_t *)msg;

         len = cfm->value.size;
         cbc = wei_cb_find_pending_callback(cfm->trans_id);
         if (cbc == NULL)
         {
            DE_TRACE_INT(TR_MIB_HIGH_RES, "Failed to find pending callback for %d\n",  cfm->trans_id);
            return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
         }
         DE_ASSERT(cbc->data == NULL);
         if (0 == len)
         {
            cbc->data = NULL;
            cbc->data_len = 0;
            cbc->status = cfm->result;
         }
         else
         {
            cbc->data = DriverEnvironment_Nonpaged_Malloc(len);
            if (NULL == cbc->data)
            {
               DE_TRACE_STATIC(TR_SEVERE, "Out of memory \n");
               cbc->status = WIFI_ENGINE_FAILURE_RESOURCES;
               cbc->data_len = 0;
            }
            else
            {
               DE_MEMCPY(cbc->data, cfm->value.ref, len);
               cbc->data_len = len;
               cbc->status = cfm->result;
            }
         }
      }
      break;
      case MLME_SET_CFM:
      {
         mlme_mib_set_cfm_t *cfm;

         cfm = (mlme_mib_set_cfm_t *)msg;

         cbc = wei_cb_find_pending_callback(cfm->trans_id);
         if (cbc == NULL)
         {
            DE_TRACE_INT(TR_MIB_HIGH_RES, "Failed to find pending callback for %d\n",  cfm->trans_id);
            return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
         }
         DE_ASSERT(cbc->data == NULL);
         cbc->status = cfm->result;
         cbc->data_len = 0;
      }
      break;
      case MLME_MIB_TRIGGER_IND:
      {
         mlme_mib_trigger_ind_t *ind;
         we_mib_trig_data_t     *td;

         ind = (mlme_mib_trigger_ind_t *)msg;

         len = ind->varsize;
         cbc = wei_cb_find_pending_callback(ind->trans_id);
         if (cbc == NULL)
         {
            DE_TRACE_INT(TR_MIB_HIGH_RES, "Failed to find pending callback for %d\n",  ind->trans_id);
            return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
         }
         DE_ASSERT(cbc->data == NULL);
         if (0 == len)
         {
            cbc->data = NULL;
            cbc->data_len = 0;
            cbc->status = WIFI_ENGINE_SUCCESS;
         }
         else
         {
            td = (we_mib_trig_data_t*)DriverEnvironment_Nonpaged_Malloc((len - 1) + sizeof *td);
            cbc->data = td;
            if (NULL == cbc->data)
            {
               DE_TRACE_STATIC(TR_SEVERE, "Out of memory \n");
               cbc->status = WIFI_ENGINE_FAILURE_RESOURCES;
               cbc->data_len = 0;
            }
            else
            {
	       td->trig_id = ind->trans_id;
               td->type = WE_TRIG_TYPE_IND;
	       td->len = len;
               DE_MEMCPY(td->data, &ind->value, len);
               cbc->data_len = (len - 1) + sizeof *td;
               cbc->status = WIFI_ENGINE_SUCCESS;
            }
         }
      }
      break;
      case MLME_MIB_SET_TRIGGER_CFM:
      {
         mlme_mib_set_trigger_cfm_t *cfm;
	 we_mib_trig_data_t     *td;

         cfm = (mlme_mib_set_trigger_cfm_t *)msg;

         cbc = wei_cb_find_pending_callback(cfm->trans_id);
         if (cbc == NULL)
         {
            DE_TRACE_INT(TR_MIB_HIGH_RES, "Failed to find pending callback for %d\n",  cfm->trans_id);
            return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
         }
         DE_ASSERT(cbc->data == NULL);
         td = (we_mib_trig_data_t*)DriverEnvironment_Nonpaged_Malloc(sizeof *td);
         cbc->data = td;
         if (NULL == cbc->data)
         {
            DE_TRACE_STATIC(TR_SEVERE, "Out of memory \n");
            cbc->status = WIFI_ENGINE_FAILURE_RESOURCES;
            cbc->data_len = 0;
         }
         else
         {
            td->trig_id = cfm->trans_id;
            td->type = WE_TRIG_TYPE_CFM;
            td->len = 0;
            td->result = cfm->result;
            cbc->data_len = sizeof *td;
            cbc->status = WIFI_ENGINE_SUCCESS;
         }
      }
      break;
      case MLME_MIB_REMOVE_TRIGGER_CFM:
      {
         mlme_mib_remove_trigger_cfm_t *cfm;

         cfm = (mlme_mib_remove_trigger_cfm_t *)msg;

         cbc = wei_cb_find_pending_callback(cfm->trans_id);
         if (cbc == NULL)
         {
            DE_TRACE_INT(TR_MIB_HIGH_RES, "Failed to find pending callback for %d\n",  cfm->trans_id);
            return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
         }
         DE_ASSERT(cbc->data == NULL);
         cbc->status = cfm->result;
         cbc->data_len = 0;
      }
      break;
      case MLME_MIB_SET_GATINGTRIGGER_CFM:
      {
         mlme_mib_set_gatingtrigger_cfm_t *cfm;

         cfm = (mlme_mib_set_gatingtrigger_cfm_t *)msg;

         cbc = wei_cb_find_pending_callback(cfm->trans_id);
         if (cbc == NULL)
         {
            DE_TRACE_INT(TR_MIB_HIGH_RES, "Failed to find pending callback for %d\n",  cfm->trans_id);
            return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
         }
         DE_ASSERT(cbc->data == NULL);
         cbc->status = cfm->result;
         cbc->data_len = 0;
      }
      break;
      default:
         DE_TRACE_INT(TR_SEVERE, "Unknown message ID %d\n", msgId);
         return WIFI_ENGINE_FAILURE;
   }  
   WiFiEngine_ScheduleCallback(cbc);

   if ( ! wei_cb_still_valid(cbc)) {
      /* this happens if callback itself calls WiFiEngine_CancelCallback */
      return WIFI_ENGINE_SUCCESS;
   }
   if (cbc->repeating)
   {
      wei_cb_queue_pending_callback(cbc);
   }
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get a MIB response
 *
 * Retrieve the data from a previously sent MIB query (with
 * WiFiEngine_SendMIBGet/Set(). 
 *
 * @param num ID of the response (returned by the request call)
 * @param outbuf Output buffer (allocated by caller)
 * @param buflen IN: size of the output buffer
 *               OUT: amount of data copied to the output buffer
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_INVALID_LENGTH if the output buffer was too small
 * - WIFI_ENGINE_FAILURE_INVALID_DATA if the variable was write-only
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if no matching reply was found
 * - WIFI_ENGINE_FAILURE on other failures
 */
int   WiFiEngine_GetMIBResponse(uint32_t num, void *outbuf, IN OUT size_t *buflen)
{
   struct mib_cfm_container *c;
   int status;
   
   BAIL_IF_UNPLUGGED;
   WIFI_LOCK();
   c = find_mib_reply(num);
   if(c == NULL) {
	   WIFI_UNLOCK(); /* this should be before return, shouldn't it? */
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }
   if (*buflen < c->mib_object_size) {
      *buflen = c->mib_object_size;
      WIFI_UNLOCK();
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   WEI_TQ_REMOVE(&mib_cfm_list, c, next);
   mib_cfm_list_size--;
   WIFI_UNLOCK();

   *buflen = c->mib_object_size;
   status = c->reply.result;
   if (status == MIB_RESULT_OK) 
   {
      if (c->reply.value.size < *buflen || c->reply.value.ref == 0) {
          /* This will generate bluescreen by Anders Dahl's testcases, so I prevent it
             TODO: Next step: check if status really should be MIB_RESULT_OK in this case!*/
          free_mib_container(c);
          *buflen = 0;
          return WIFI_ENGINE_FAILURE;
      }
      DE_MEMCPY(outbuf, c->reply.value.ref, *buflen);
      free_mib_container(c);
      return WIFI_ENGINE_SUCCESS;
   }

   free_mib_container(c);
   *buflen = 0;
   switch (status)
   {
      case MIB_RESULT_INVALID_PATH:
      case MIB_RESULT_NO_SUCH_OBJECT:
      case MIB_RESULT_OBJECT_NOT_A_LEAF:
         return WIFI_ENGINE_FAILURE_INVALID_DATA;
      case MIB_RESULT_SIZE_ERROR:
      case MIB_RESULT_SET_NOT_ALLOWED:
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      case MIB_RESULT_SET_FAILED:
      case MIB_RESULT_GET_FAILED:
      case MIB_RESULT_INTERNAL_ERROR:
      default:
         return WIFI_ENGINE_FAILURE;
   }
}

/*!
 * @brief Get a MIB response
 *
 * Retrieve the data from a previously sent MIB query (with
 * WiFiEngine_SendMIBGet/Set(). This differs from
 * WiFiEngine_GetMIBResponse in that it returns the raw HIC MIB
 * response packet.
 *
 * @param [in] num ID of the response (returned by the request call)
 * @param [out] outbuf Output buffer (allocated by caller)
 * @param [in,out] buflen IN: size of the output buffer
 *               OUT: amount of data copied to the output buffer
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_DEFER if no matching reply was found
 * - WIFI_ENGINE_FAILURE_INVALID_LENGTH if buflen is too small
 */

int WiFiEngine_GetMIBResponse_raw(uint32_t num, 
                                  void *outbuf, 
                                  IN OUT size_t *buflen)
{
   struct mib_cfm_container *c;
   
   BAIL_IF_UNPLUGGED;
   WIFI_LOCK();
   c = find_mib_reply(num);
   if(c == NULL) {
      WIFI_UNLOCK();
      return WIFI_ENGINE_FAILURE_DEFER;
   }
      
   /* We found it! */
   if (*buflen < c->raw_size) {
      *buflen = c->raw_size;
      WIFI_UNLOCK();
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   WEI_TQ_REMOVE(&mib_cfm_list, c, next);
   mib_cfm_list_size--;
   WIFI_UNLOCK();
   *buflen = c->raw_size;
   DE_MEMCPY(outbuf, c->raw, *buflen);
   free_mib_container(c);
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get a MIB object with completion callback.
 *
 * The callback will be passed the contents of the MIB response
 * (without headers).
 *
 * @param id MIB variable id string
 * @param cbc Pointer to a caller-allocated callback-container.
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            Memory for the data buffer and for the container
 *            itself will be freed automatically after the
 *            completion callback has been invoked and run to
 *            completion IF WiFiEngine_GetMIBAsynch() returned success.
 *            In all other cases the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 *
 * @return
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if the entry wasn't found.
 * - WIFI_ENGINE_FAILURE_RESOURCES if some resource (memory) was exhausted.
 */
int WiFiEngine_GetMIBAsynch(const char *id, we_cb_container_t *cbc) 
{
   hic_message_context_t   msg_ref;
   int                status;
   uint32_t trans_id;
   mib_id_t mib_id;
   
   BAIL_IF_UNPLUGGED;
   if(!wei_mib_ascii2bin(&mib_id, id)) {
      /* we can't easily print the string here, since it may not be
       * zero terminated */
      DE_TRACE_STATIC(TR_MIB, "failed to parse mib id\n");
      return WIFI_ENGINE_FAILURE;
   }
   status = mib_get(&msg_ref, &mib_id, &trans_id);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   cbc->trans_id = trans_id;
   cbc->data = NULL;
   cbc->data_len = 0;
   wei_cb_queue_pending_callback(cbc);

   status = wei_send_cmd(&msg_ref);
   Mlme_ReleaseMessageContext(msg_ref);

   if (status == WIFI_ENGINE_FAILURE_DEFER) 
   {
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   else if (status != WIFI_ENGINE_SUCCESS) {
      DE_BUG_ON(1, "wei_send_cmd() failed\n");
   }
   return status;
}

struct update_dst
{
   void *dst;
   size_t dstlen;
   wei_update_transform transform;
   we_indication_t we_ind;
};

int wei_get_mib_with_update_cb(we_cb_container_t *cbc)
{
   struct update_dst *dst;

   DE_ASSERT(cbc != NULL);
   dst = (struct update_dst *)cbc->ctx;
   if (cbc->data_len > dst->dstlen)
   {
      DE_TRACE_STATIC(TR_MIB, "Bad min value size \n");
      return 0;
   }
   DE_MEMCPY(dst->dst, cbc->data, cbc->data_len);
   if (dst->transform) /* Such as a endian-swap */
   {
      dst->transform((char*)dst->dst, dst->dstlen);
   }
   if(dst->we_ind != WE_IND_NOOP)
   {
      DriverEnvironment_indicate(dst->we_ind, NULL, 0);
   }
   DriverEnvironment_Free(dst);
   return 1;
}

/*!
 * MIB get with update. Query a MIB and copy the result (if the query
 * was successful) to the indicated destination buffer. Run the
 * provided transform function (optional) on the destination buffer
 * after the copy is complete. This can for instance be used to effect
 * an update with endian-swap.
 *
 * @param dst Output buffer.
 * @param dstlen Output buffer length.
 * @param id MIB identifier to query.
 * @param transform Destination buffer transformation function (can be NULL).
 * @param we_ind indication after a success. (WE_IND_NOOP for none)
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE on failure.
 */
int wei_get_mib_with_update(const char *id, 
                            void *dst, 
                            size_t dstlen, 
                            wei_update_transform transform, 
                            we_indication_t we_ind)
{
   we_cb_container_t *cbc;
   struct update_dst *upd_dst;
   int status;

   upd_dst = (struct update_dst *)DriverEnvironment_Malloc(sizeof *upd_dst);
   if (NULL == upd_dst)
   {
      DE_TRACE_STATIC(TR_MIB, "Failed to allocate update_dst\n");
      return WIFI_ENGINE_FAILURE;
   }
   upd_dst->dst = dst;
   upd_dst->dstlen = dstlen;
   upd_dst->transform = transform;
   upd_dst->we_ind = we_ind;

   DE_TRACE_STATIC(TR_MIB, "wei_get_mib_with_update()\n");

   cbc = WiFiEngine_BuildCBC(wei_get_mib_with_update_cb, upd_dst, sizeof *upd_dst, FALSE);
   if (NULL == cbc)
   {
      DE_TRACE_STATIC(TR_MIB, "Failed to build cbc\n");
      return WIFI_ENGINE_FAILURE;
   }
   status = WiFiEngine_GetMIBAsynch(id, cbc);
   if(status != WIFI_ENGINE_SUCCESS) {
      WiFiEngine_FreeCBC(cbc);
   }
   return status;
}

/************************************************************/
struct wei_ratelimit_mib {
   WEI_TQ_ENTRY(wei_ratelimit_mib)  rlm_all;
   driver_lock_t                    rlm_lock;
   mib_id_t                         rlm_id;
   driver_tick_t                    rlm_period_dt;
   driver_tick_t                    rlm_last_update;
   size_t                           rlm_size;
   unsigned char                    rlm_data[1];
};

static struct wei_ratelimit_mib_state {
   WEI_TQ_HEAD(, wei_ratelimit_mib)  rlms_all;
   driver_lock_t                     rlms_lock;
} wrlm_state;

#define WRMS_LOCK()   DriverEnvironment_acquire_lock(&wrlm_state.rlms_lock)
#define WRMS_UNLOCK() DriverEnvironment_release_lock(&wrlm_state.rlms_lock)

#define WRLM_LOCK(PM)   DriverEnvironment_acquire_lock(&(PM)->rlm_lock)
#define WRLM_UNLOCK(PM) DriverEnvironment_release_lock(&(PM)->rlm_lock)

static void wei_ratelimit_mib_init(void)
{
   WEI_TQ_INIT(&wrlm_state.rlms_all);
   DriverEnvironment_initialize_lock(&wrlm_state.rlms_lock);
}

static void wei_ratelimit_mib_shutdown(void)
{
   struct wei_ratelimit_mib *wrm;

   WRMS_LOCK();
   while((wrm = WEI_TQ_FIRST(&wrlm_state.rlms_all)) != NULL) {
      WEI_TQ_REMOVE(&wrlm_state.rlms_all, wrm, rlm_all);
      DriverEnvironment_Free(wrm);
   }
   WRMS_UNLOCK();
}

static int
wei_ratelimit_mib_update_cb(we_cb_container_t *cbc)
{
   struct wei_ratelimit_mib *wrm;

   if(cbc == NULL) {
      DE_TRACE_STATIC(TR_MIB, "called with NULL cbc\n");
      return 0;
   }

   if(cbc->status != 0) {
      DE_TRACE_INT(TR_MIB, "failed to update mib: %d\n", cbc->status);
      return 0;
   }
   if(cbc->ctx == NULL || cbc->ctx_len != sizeof(*wrm)) {
      DE_TRACE_STATIC(TR_MIB, "unexpected context\n");
      return 0;
   }
   wrm = (struct wei_ratelimit_mib*)cbc->ctx;
   if(cbc->data == NULL || cbc->data_len != wrm->rlm_size) {
      DE_TRACE_INT(TR_MIB, "unexpected mib size: %d\n", cbc->data_len);
      return 0;
   }
   WRLM_LOCK(wrm);
   DE_MEMCPY(&wrm->rlm_data[0], cbc->data, wrm->rlm_size);
   WRLM_UNLOCK(wrm);

   return 0;
}


static int
wei_ratelimit_mib_update(struct wei_ratelimit_mib *wrm, driver_tick_t now)
{
   hic_message_context_t msg_ref;
   we_cb_container_t *cbc;
   uint32_t trans_id;
   int status;

   /* first check if firmware has support for this MIB */
   if(!wei_mib_is_valid(&wrm->rlm_id)) {
      /* no point in continuing */
      return WIFI_ENGINE_FAILURE;
   }
   cbc = WiFiEngine_BuildCBC(wei_ratelimit_mib_update_cb, 
                             wrm, sizeof(*wrm), FALSE);
   if(cbc == NULL) {
      DE_TRACE_STATIC(TR_MIB, "Failed to build cbc\n");
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   status = mib_get(&msg_ref, &wrm->rlm_id, &trans_id);
   if(status != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_INT(TR_MIB, "mib_get returned %d\n", status);
      WiFiEngine_FreeCBC(cbc);
      return status;
   }

   cbc->trans_id = trans_id;
   wei_cb_queue_pending_callback(cbc);

   status = wei_send_cmd(&msg_ref);
   Mlme_ReleaseMessageContext(msg_ref);

   if(status == WIFI_ENGINE_SUCCESS) {
      WRLM_LOCK(wrm);
      wrm->rlm_last_update = now;
      WRLM_UNLOCK(wrm);
   } else {
      DE_TRACE_INT(TR_MIB, "wei_send_cmd returned %d\n", status);
      cbc->status = status;
      WiFiEngine_RunCallback(cbc);
   }
   return status;
}

#undef TIME_BEFORE /* XXX belongs in driverenv */
#define TIME_BEFORE(A, B) ((int)(A) - (int)(B) < 0)

static int
wei_ratelimit_mib_try_update(struct wei_ratelimit_mib *wrm)
{
   driver_tick_t now = DriverEnvironment_GetTicks();

   if(TIME_BEFORE(now, wrm->rlm_last_update + wrm->rlm_period_dt)) {
      return WIFI_ENGINE_SUCCESS;
   }
   return wei_ratelimit_mib_update(wrm, now);
}

static int
wei_ratelimit_mib_alloc(const mib_id_t *id,
                        size_t len,
                        uint32_t period_ms)
{
   struct wei_ratelimit_mib *wrm;

   wrm = DriverEnvironment_Malloc(sizeof(*wrm) + len);
   if(wrm == NULL) {
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   wrm->rlm_id = *id;
   DriverEnvironment_initialize_lock(&wrm->rlm_lock);
   wrm->rlm_period_dt = DriverEnvironment_msec_to_ticks(period_ms);
   wrm->rlm_size = len;

   /* force an update */
   wei_ratelimit_mib_update(wrm, DriverEnvironment_GetTicks());

   WRMS_LOCK();
   WEI_TQ_INSERT_TAIL(&wrlm_state.rlms_all, wrm, rlm_all);
   WRMS_UNLOCK();

   return WIFI_ENGINE_SUCCESS;
}

int
WiFiEngine_RatelimitMIBGet(const char *id, uint32_t period_ms,
                           void *data, size_t len)
{
   struct wei_ratelimit_mib *wrm;
   mib_id_t mib_id;

   if(!wei_mib_ascii2bin(&mib_id, id)) {
      return WIFI_ENGINE_FAILURE;
   }
   WRMS_LOCK();
   WEI_TQ_FOREACH(wrm, &wrlm_state.rlms_all, rlm_all) {
      if(DE_MEMCMP(mib_id.octets, wrm->rlm_id.octets, 
                   sizeof(mib_id.octets)) == 0) {
         break;
      }
   }
   WRMS_UNLOCK();
   
   if(wrm == NULL) {
      wei_ratelimit_mib_alloc(&mib_id, len, period_ms);
      return WIFI_ENGINE_FAILURE_DEFER;
   }
   
   if(len != wrm->rlm_size) {
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   WRLM_LOCK(wrm);
   DE_MEMCPY(data, wrm->rlm_data, wrm->rlm_size);
   WRLM_UNLOCK(wrm);

   wei_ratelimit_mib_try_update(wrm);

   return WIFI_ENGINE_SUCCESS;
}


/************************************************************/

/*!
 * @brief Register a MIB trigger.
 *
 * The registered callback will be executed any time the MIB trigger
 * is triggered.  The callback remains registered until the trigger is
 * removed.
 *
 * @param trig_id Output buffer for the trigger ID assigned by WiFiEngine.
 * @param id The MIB ID of the MIB to set the trigger on.
 * @param gating_trig_id A MIB triggers can be coupled to another MIB trigger so
 *        it only triggers when it and the other trigger (the gating trigger)
 *        are both triggered at the same time. 
 * ...
 * @param cbc Pointer to a caller-allocated callback-container.
 *            This should be completely filled out and the
 *            memory allocated for it must be of the type
 *            indicated in the mt member.
 *            Memory for the container will be freed automatically
 *            when the callback is cancelled.  If this call returns a
 *            failure code then the caller is responsible for freeing
 *            the cbc and the data buffer. Use WiFiEngine_BuildCBC()
 *            to construct the cbc properly.
 *            The callback is passed a pointer to we_mib_trig_data_t
 *            when called with a positive status parameter value.
 *            The we_mib_trig_data_t struct signifies a trigger ind message
 *            if the len member is larger than 0. Otherwise the callback
 *            is called because the trigger has been successfully installed
 *            and the result member contains the result code from the
 *            set mib trigger request.
 *            Note that the callback should be repeating (cbc->repeating == 1)
 *            or it will only be called once (for the confirm message for
 *            the set mib trigger request).
 * @return
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE on failure
 * - WIFI_ENGINE_FAILURE_RESOURCES if the input buffer was too small
 */
int WiFiEngine_RegisterMIBTrigger(int32_t *trig_id,
				  const char *id, 
				  uint32_t gating_trig_id,
				  uint32_t          supv_interval,
				  int32_t           level,
				  wei_supvevent_t   event,       /* supvevent_t */
				  uint16_t          event_count,
				  uint16_t          triggmode,
				  we_cb_container_t *cbc)
{
   hic_message_context_t   msg_ref;
   int                status;
   mib_id_t           mib_id;
   
   BAIL_IF_UNPLUGGED;
   if (NULL == cbc)
   {
      return WIFI_ENGINE_FAILURE;
   }
   if(!wei_mib_ascii2bin(&mib_id, id)) {
      /* we can't easily print the string here, since it may not be
       * zero terminated */
      DE_TRACE_STATIC(TR_MIB, "failed to parse mib id\n");
      return WIFI_ENGINE_FAILURE;
   }
   Mlme_CreateMessageContext(msg_ref);
   if (!Mlme_CreateMIBSetTriggerRequest(&msg_ref, mib_id, gating_trig_id, 
                                       supv_interval, level, event, event_count,
				       triggmode, (uint32_t *)trig_id))
   {
      DE_TRACE_STATIC(TR_MIB, "Mlme call failed\n");
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE;
   }

   cbc->trans_id = *trig_id;
   wei_cb_queue_pending_callback(cbc);

   status = wei_send_cmd(&msg_ref);
   if (status == WIFI_ENGINE_FAILURE_DEFER) 
   {
      DE_TRACE_STATIC(TR_MIB, "cmd send was deferred\n");
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   else if (status != WIFI_ENGINE_SUCCESS) 
   {
      DE_BUG_ON(1, "wei_send_cmd() failed\n");
   }

   Mlme_ReleaseMessageContext(msg_ref);

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Deregister a MIB trigger.
 *
 * The indicated MIB trigger is removed from the device and the callback
 * is cancelled (it will be called with status WIFI_ENGINE_FAILURE_ABORT).
 *
 * @param trig_id The id of the MIB trigger to be removed.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_INVALID_DATA if the trigger ID was invalid.
 * - WIFI_ENGINE_FAILURE_RESOURCES if the message could not be sent.
 * - WIFI_ENGINE_FAILURE on internal errors.
 */
int WiFiEngine_DeregisterMIBTrigger(uint32_t trig_id)
{
   hic_message_context_t   msg_ref;
   we_cb_container_t *cbc;
   int status = WIFI_ENGINE_FAILURE;

   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateMIBRemoveTriggerRequest(&msg_ref, trig_id))
   {
      status = wei_send_cmd(&msg_ref);
      if (status == WIFI_ENGINE_FAILURE_DEFER) 
      {
         DE_TRACE_STATIC(TR_MIB, "cmd send was deferred\n");
         Mlme_ReleaseMessageContext(msg_ref);
         return WIFI_ENGINE_FAILURE_RESOURCES;
      }
      else if (status != WIFI_ENGINE_SUCCESS) 
      {
         DE_BUG_ON(1, "wei_send_cmd() failed\n");
      }
   }
   else 
   {
      DE_TRACE_STATIC(TR_MIB, "Mlme call failed\n");
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);
   
   if((cbc = wei_cb_find_pending_callback(trig_id)) == NULL)
   {
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   WiFiEngine_CancelCallback(cbc);

   return WIFI_ENGINE_SUCCESS;
}



struct mib_cache_entry_t;
WEI_TQ_HEAD(mib_cache_head_t, mib_cache_entry_t);

struct mib_cache_entry_t {
   mib_id_t mib_id;
   size_t inbuflen;
   WEI_TQ_ENTRY(mib_cache_entry_t) next;
};

static struct mib_cache_head_t mib_cache_head =
   WEI_TQ_HEAD_INITIALIZER(mib_cache_head);


static int _cache_mib_equal(mib_id_t* mib_id1, mib_id_t* mib_id2)
{
   return DE_MEMCMP(mib_id1, mib_id2, sizeof(mib_id_t)) == 0;
}

static struct mib_cache_entry_t* _cache_mib_create(mib_id_t mib_id,
                                                   const void *inbuf,
                                                   size_t inbuflen)
{
   struct mib_cache_entry_t* mc;
      
   mc = (struct mib_cache_entry_t* )
      DriverEnvironment_Nonpaged_Malloc(sizeof(*mc) + inbuflen);
   if(mc == NULL) {
      DE_TRACE_STATIC(TR_MIB, "Failed to alloc mib_cache_entry\n");
      return NULL;
   }
   DE_MEMCPY(&mc->mib_id, &mib_id, sizeof(mib_id_t));

   mc->inbuflen = inbuflen;
   DE_MEMCPY(&mc[1], inbuf, mc->inbuflen);
   return mc;
}

static int _cache_mib_free(struct mib_cache_entry_t* mib_cache_entry)
{
   DriverEnvironment_Nonpaged_Free(mib_cache_entry);

   return WIFI_ENGINE_SUCCESS;
}


static int cache_add_mib(mib_id_t mib_id, const void *inbuf, size_t inbuflen)
{
   struct mib_cache_entry_t* mib_cache_entry;
   struct mib_cache_entry_t* e;
#ifdef WIFI_DEBUG_ON
   char str[32];
#endif

   DE_TRACE_STRING(TR_MIB, "Caching MIB: %s\n", 
                   wei_print_mib(&mib_id, str, sizeof(str)));
   
   WEI_TQ_FOREACH(e, &mib_cache_head, next) {
      if(_cache_mib_equal(&e->mib_id, &mib_id)) {
         DE_TRACE_STATIC(TR_MIB, "Replacing previous entry in mib cache\n");
         WEI_TQ_REMOVE(&mib_cache_head, e, next);
         _cache_mib_free(e);
         break;
      }
   }

   mib_cache_entry = _cache_mib_create(mib_id, inbuf, inbuflen);
   if(mib_cache_entry == NULL)
      return WIFI_ENGINE_FAILURE;
   
   WEI_TQ_INSERT_TAIL(&mib_cache_head, mib_cache_entry, next);
   return WIFI_ENGINE_SUCCESS;
}

int wei_initialize_mib(void)
{
   wei_ratelimit_mib_init();
   return WIFI_ENGINE_SUCCESS;
}

int wei_reconfigure_mib(void)
{
   struct mib_cache_entry_t* e;
   int exit_code = WIFI_ENGINE_SUCCESS;
   
   WEI_TQ_FOREACH(e, &mib_cache_head, next) {
      int status;

      status =  wei_send_mib_set_binary(e->mib_id, NULL, 
                                        (char*)&e[1], e->inbuflen,
                                        NULL);

      if(status != WIFI_ENGINE_SUCCESS) {
         DE_TRACE_STATIC(TR_MIB, "Failed to set mib from cache.\n");
         exit_code = WIFI_ENGINE_FAILURE;
         continue;         
      }
   }
   
   return exit_code;
}

int wei_unplug_mib(void)
{
   struct mib_cache_entry_t *e;
   while((e = WEI_TQ_FIRST(&mib_cache_head)) != NULL) {
      WEI_TQ_REMOVE(&mib_cache_head, e, next);
      _cache_mib_free(e);
   }

   DE_ASSERT(WEI_TQ_EMPTY(&mib_cache_head));
   
   return WIFI_ENGINE_SUCCESS;
}

int wei_shutdown_mib(void)
{
   wei_unplug_mib();
   wei_free_mibtable();
   wei_ratelimit_mib_shutdown();

   return WIFI_ENGINE_SUCCESS;
}



/** @} */ /* End of we_mib group */
/* Local Variables: */
/* c-basic-offset: 3 */
/* indent-tabs-mode: nil */
/* End: */
 
