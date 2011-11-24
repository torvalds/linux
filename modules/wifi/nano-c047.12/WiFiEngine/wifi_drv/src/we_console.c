/* $Id: we_console.c,v 1.7 2008-05-19 14:57:32 peek Exp $ */
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
This module implements the WiFiEngine console interface

*****************************************************************************/
/** @defgroup we_console WiFiEngine firmware console
 *
 * @brief WiFiEngine functions for exchanging console commands with firmware
 *
 *  @{
 */

#include "driverenv.h"
#include "wifi_engine_internal.h"
#include "mlme_proxy.h"
#include "macWrapper.h"

struct console_cfm_container {
   uint16_t idnum;
   size_t reply_size;
   WEI_TQ_ENTRY(console_cfm_container) next;
   char reply[1];
};

/* List of received but unfetched (by the one who issued the query)
 * console replies */
static WEI_TQ_HEAD(,console_cfm_container) console_replies;
static size_t console_list_size;

int
wei_console_init(void)
{
   WEI_TQ_INIT(&console_replies);
   console_list_size = 0;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * Prune all queued console replies so list is at most len items long 
 */
static int wei_prune_console_reply_list(size_t len) 
{
   struct console_cfm_container *cfm;
   WIFI_LOCK();
   while(console_list_size > len) {
      cfm = WEI_TQ_FIRST(&console_replies);
      DE_ASSERT(cfm != NULL);
      DE_TRACE_INT(TR_CONSOLE, "Pruning CONSOLE reply id %d\n", cfm->idnum);
      WEI_TQ_REMOVE(&console_replies, cfm, next);
      console_list_size--;
      DriverEnvironment_Nonpaged_Free(cfm);
   }
   WIFI_UNLOCK();
   return 0;
}

void wei_clear_console_reply_list(void)
{
   wei_prune_console_reply_list(0);
}

int wei_queue_console_reply(char *replyPacked, size_t replySize)
{
   struct console_cfm_container *rc;

   rc = (struct console_cfm_container *)DriverEnvironment_Nonpaged_Malloc(sizeof(*rc) + replySize - 1);
   DE_MEMSET(rc, 0, sizeof *rc);
   rc->reply_size = replySize;
   DE_MEMCPY(rc->reply, replyPacked, replySize);

   WIFI_LOCK();
   rc->idnum = wifiEngineState.last_seq_num;
   wifiEngineState.last_seq_num++;
   WEI_TQ_INSERT_TAIL(&console_replies, rc, next);
   console_list_size++;
   WIFI_UNLOCK();

   wei_prune_console_reply_list(LIST_SIZE);

   DriverEnvironment_indicate(WE_IND_CONSOLE_REPLY, NULL, 0);

   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Send a raw HIC message to target. 
 * 
 * The inbuf is assumed to be a completeHIC-packet, no header will be
 * prepended.  The reply can be retrieved with a subsequent matching
 * call to WiFiEnging_GetConsoleReply().
 *
 * @param inbuf Input buffer.
 * @param inbuflen Input buffer length.
 * @param num Output buffer, will contain the transaction id of the request
 *            which should be used to retrieve the reply.
 *
 * @bug The transaction id returned is currently forgotten by the
 *      x_test firmware, and all replies have transaction id 0.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE_RESOURCES if the a cmd reply is pending
 * - WIFI_ENGINE_FAILURE on failure
 */
int
WiFiEngine_SendHICMessage(char *inbuf, size_t inbuflen, uint32_t *num)
{
   int status;
   char *sendbuf;
   uint16_t hic_len;
   uint16_t pad_len;
   uint8_t hic_hdr_len;
   char *payload;
   uint16_t payload_len;
   static uint16_t min_hic_size;

   BAIL_IF_UNPLUGGED;
   DE_TRACE_STATIC(TR_CONSOLE, "SendConsoleCmd()called\n");
   DE_TRACE_DATA(TR_CONSOLE, "OLD", inbuf, inbuflen);

   if(min_hic_size == 0)
      min_hic_size = wei_get_hic_hdr_min_size();

   if(inbuflen < min_hic_size) {
      DE_TRACE_INT(TR_CONSOLE, "input buffer too short (" TR_FSIZE_T ")\n", 
                   TR_ASIZE_T(inbuflen));
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   hic_len = HIC_MESSAGE_LENGTH_GET(inbuf);
   if(hic_len > inbuflen) {
      DE_TRACE_INT2(TR_CONSOLE, 
                    "input buffer too short %u < " TR_FSIZE_T "\n",
                    hic_len,
                    TR_ASIZE_T(inbuflen));
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   pad_len = HIC_MESSAGE_PADDING_GET(inbuf);
   if(pad_len > 0xfff8 || hic_len < pad_len + min_hic_size) {
      DE_TRACE_INT3(TR_CONSOLE, 
                    "input buffer has too much padding %u < %u + %u\n",
                    hic_len,
                    pad_len,
                    min_hic_size);
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   hic_hdr_len = HIC_MESSAGE_HDR_SIZE(inbuf);
   if(hic_hdr_len < min_hic_size) {
      DE_TRACE_INT(TR_CONSOLE, "hic header too short (%u)\n", hic_hdr_len);
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }

   payload = inbuf + hic_hdr_len;
   payload_len = hic_len - hic_hdr_len - pad_len;

   /* fixup a new packet */
   hic_hdr_len = wifiEngineState.config.tx_hic_hdr_size;
   hic_len = hic_hdr_len + payload_len;
   pad_len = WiFiEngine_GetPaddingLength(hic_len);
   hic_len += pad_len;

   sendbuf = (char *)DriverEnvironment_TX_Alloc(hic_len);
   if(sendbuf == NULL) {
      DE_TRACE_STATIC(TR_CONSOLE, "failed to allocate buffer\n");
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   
   /* setup hic header */
   DE_MEMSET(sendbuf, 0x70, hic_hdr_len);
   HIC_MESSAGE_LENGTH_SET(sendbuf, hic_len);
   HIC_MESSAGE_TYPE(sendbuf)     = HIC_MESSAGE_TYPE(inbuf);
   HIC_MESSAGE_ID(sendbuf)       = HIC_MESSAGE_ID(inbuf);
   HIC_MESSAGE_HDR_SIZE(sendbuf) = hic_hdr_len;
   HIC_MESSAGE_PADDING_SET(sendbuf, pad_len);

   /* copy payload */
   DE_MEMCPY(sendbuf + hic_hdr_len, payload, payload_len);

   /* init padding */
   DE_MEMSET(sendbuf + hic_hdr_len + payload_len, 0x50, pad_len);

   DE_TRACE_DATA(TR_CONSOLE, "NEW", sendbuf, hic_len);

   status = wei_send_cmd_raw(sendbuf, hic_len);
   if (status == WIFI_ENGINE_FAILURE_DEFER) {
      DriverEnvironment_TX_Free(sendbuf);
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   else if (status != WIFI_ENGINE_SUCCESS) {
      DriverEnvironment_TX_Free(sendbuf);
      DE_BUG_ON(1, "wei_send_cmd() failed in call from SendConsoleCmd()\n");
   }
   WIFI_LOCK();
   if (num)
      *num = wifiEngineState.current_seq_num;
   wifiEngineState.current_seq_num++;
   WIFI_UNLOCK();

    return WIFI_ENGINE_SUCCESS;
}
 
/*!
 * @brief Send a console command to target. 
 * 
 * The command should be a zero-terminated string with the command to
 * issue, it should end in a newline.
 *
 * The reply can be retrieved with a subsequent matching call to
 * WiFiEnging_GetConsoleReply().
 *
 * @param [in]  command Console command to execute.
 * @param [out] tid     Output buffer, will contain the 
 *                      transaction id of the request
 *                      which should be used to retrieve the reply.
 *
 * @bug The transaction id returned is currently forgotten by the
 *      x_test firmware, and all replies have transaction id 0.
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE on failure
 * @retval WIFI_ENGINE_FAILURE_RESOURCES on memory allocation failure.
 */
int
WiFiEngine_SendConsoleRequest(const char *command, uint32_t *tid)
{
        hic_message_context_t msg_ref;
        int status;
   
        Mlme_CreateMessageContext(msg_ref);
        if (Mlme_CreateConsoleRequest(&msg_ref, command, tid) == 0) {
                Mlme_ReleaseMessageContext(msg_ref);
                return WIFI_ENGINE_FAILURE;
        }

        status = wei_send_cmd(&msg_ref);
        Mlme_ReleaseMessageContext(msg_ref);

        if (status == WIFI_ENGINE_FAILURE_DEFER) 
                status = WIFI_ENGINE_FAILURE_RESOURCES;
        
        return status;
}

/*!
 * @brief Get the first available console reply or indication.
 *
 * @param outbuf Output buffer, will contain the raw packet.
 * @param buflen Output buffer length. Will contain the needed size on
 *               INVALID_LENGTH failure.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_INVALID_LENGTH if the output buffer was too small.
 * - WIFI_ENGINE_FAILURE if no reply is available.
 */
int   WiFiEngine_GetConsoleReply(void *outbuf, IN OUT size_t *buflen)
{
   struct console_cfm_container *c;
   
   BAIL_IF_UNPLUGGED;
   WIFI_LOCK();
   if((c = WEI_TQ_FIRST(&console_replies)) != NULL) {
      if (*buflen < c->reply_size) {
         *buflen = c->reply_size;
         WIFI_UNLOCK();
         return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      }
      *buflen = c->reply_size;
      WEI_TQ_REMOVE(&console_replies, c, next);
      console_list_size--;
      DE_MEMCPY(outbuf, c->reply, c->reply_size);
      DriverEnvironment_Nonpaged_Free(c);
      WIFI_UNLOCK();
      return WIFI_ENGINE_SUCCESS;
   }
   *buflen = 0;
   
   WIFI_UNLOCK();
   return WIFI_ENGINE_FAILURE;
}

/** @} */ /* End of we_console group */

