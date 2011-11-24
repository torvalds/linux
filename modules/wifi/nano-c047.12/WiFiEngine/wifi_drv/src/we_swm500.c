/* $Id: we_swm500.c,v 1.19 2008-05-19 15:03:24 peek Exp $ */
/*****************************************************************************

Copyright (c) 2006 by Nanoradio AB

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
Caution, there be dragons!

*****************************************************************************/
/** @defgroup we_swm500 WiFiEngine raw packet interface
 *
 * @brief This interface is used for development and testing purposes. It
 * gives access to the raw packets, bypassing much of WiFiEngine in
 * the process. This interface is _very dangerous_, use it at your peril.
 *
 * @{
 */
#include "driverenv.h"
#include "wifi_engine_internal.h"
#include "hicWrapper.h"

#define WE_SWM500_TID_MASK    0x0fffffff
#define WE_SWM500_TID_CMASK   0xf0000000
#define WE_SWM500_TID_MIB     0x10000000
#define WE_SWM500_TID_CONSOLE 0x20000000
#define WE_SWM500_TID_FLASH   0x30000000

/* send a raw hic command, command_mask specifies which commands to
 * allow, data/length is the formatter buffer, and tid returns a
 * transaction id that can be passed to WiFiEngine_SWM500_Reply to retreive
 * the response. 
 * HAVING MORE THAN ONE OUTSTANDING COMMAND WILL
 * PROBABLY BREAK THE DRIVER, BUT THIS IS NOT ENFORCED BY THIS
 * FUNCTION
 */
int
WiFiEngine_SWM500_Command(unsigned int command_mask, 
                  const void *data, 
                  size_t length, 
                  uint32_t *ret_tid)
{
   int status, size;
   uint8_t type, id;
   Blob_t blob;
   m80211_mlme_host_header_t hic_header;

   /* unpack the header so we know what type of packet this is */
   INIT_BLOB(&blob, (char*)data, length);
   HicWrapper_m80211_mlme_host_header_t(&hic_header, &blob, ACTION_UNPACK);
   size = BLOB_CURRENT_SIZE(&blob);
   BLOB_PAD(&blob, hic_header.hic.header_size - size);

   type = hic_header.hic.type;
   id = hic_header.hic.id;

   if(type == HIC_MESSAGE_TYPE_MIB) {
      uint32_t tid;
      mlme_mib_get_req_t get;
      mlme_mib_get_next_req_t get_next;
      mlme_mib_set_req_t set;
      mib_id_t mib_id;

      /* we need to unpack the MIB request, so we can inject our own
         transaction id */
      switch(id) {
         case MLME_GET_REQ:
            if((command_mask & WE_SWM500_MIBGET) == 0)
               return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;

            HicWrapper_mlme_mib_get_req_t(&get, &blob, ACTION_UNPACK);
            DE_MEMCPY(mib_id.octets, get.identifier, sizeof(mib_id.octets));
            status = wei_send_mib_get_binary(mib_id, &tid);
	    DE_ASSERT((tid & WE_SWM500_TID_CMASK) == 0);
            *ret_tid = WE_SWM500_TID_MIB | tid;
            break;

         case MLME_GET_NEXT_REQ:
            if((command_mask & WE_SWM500_MIBGET) == 0)
               return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;

	    HicWrapper_mlme_mib_get_next_req_t(&get_next, &blob, ACTION_UNPACK);
            status = wei_send_mib_get_next(get_next.getFirst, &tid);
	    DE_ASSERT((tid & WE_SWM500_TID_CMASK) == 0);
            *ret_tid = WE_SWM500_TID_MIB | tid;
            break;

         case MLME_SET_REQ:
           
            if((command_mask & WE_SWM500_MIBSET) == 0)
                return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
                
            set.value.ref = NULL;
            HicWrapper_mlme_mib_set_req_t(&set, &blob, ACTION_UNPACK);
            DE_MEMCPY(mib_id.octets, set.identifier, sizeof(mib_id.octets));
            status = wei_send_mib_set_binary(mib_id,
                                             &tid, 
                                             set.value.ref, 
                                             set.value.size,
                                             NULL);
            

	    DE_ASSERT((tid & WE_SWM500_TID_CMASK) == 0);
            *ret_tid = WE_SWM500_TID_MIB | tid;
            break;
         default:
            DE_TRACE_INT(TR_MIB, "bad mib id %d\n", id);
            return WIFI_ENGINE_FAILURE_INVALID_DATA;
      }
   } else if(type == HIC_MESSAGE_TYPE_CONSOLE) {
      if((command_mask & WE_SWM500_CONSOLE) == 0)
         return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;

      status = WiFiEngine_SendHICMessage((char*)data, length, NULL);
      *ret_tid = WE_SWM500_TID_CONSOLE;
      if(status != WIFI_ENGINE_SUCCESS) 
         return status;
   } else if(type == HIC_MESSAGE_TYPE_FLASH_PRG) {
      /* XXX we should really handle these more like mib requets */
      if((command_mask & WE_SWM500_FLASH) == 0)
         return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;

      status = WiFiEngine_SendHICMessage((char*)data, length, NULL);
      *ret_tid = WE_SWM500_TID_FLASH;
      if(status != WIFI_ENGINE_SUCCESS) 
         return status;
   } else {
      DE_TRACE_INT2(TR_SEVERE, "bad command sent %d/%d\n", type, id);
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }

   return WIFI_ENGINE_SUCCESS;
}

/* takes a buffer with multiple hic packets, executes the first, and
   updates date and length to point to next packet */
int
WiFiEngine_SWM500_Command_Multi(unsigned int command_mask, 
                                const void **data,
                                size_t *length, 
                                uint32_t *ret_tid,
                                int *ret_status)
{
   size_t len;

   if(*length < 2)
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   
   len = HIC_MESSAGE_LENGTH_GET(*data);

 
   if(*length < len) 
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
      
   *ret_status = WiFiEngine_SWM500_Command(command_mask,
                                           *data,
                                           len,
                                           ret_tid);

   *data = (const char*)*data + len;
   *length -= len;

   return WIFI_ENGINE_SUCCESS;
}

/* loops over all packets in a buffer, and optimistically executes
   them  */
int
WiFiEngine_SWM500_Command_All(unsigned int command_mask, 
                              const void *data,
                              size_t length)
{
   int status;
   int error;
   uint32_t tid;

   while(length > 0) {
      status = WiFiEngine_SWM500_Command_Multi(command_mask, &data, &length, 
                                               &tid, &error);
      if(status != WIFI_ENGINE_SUCCESS)
         return status;
   }
   return WIFI_ENGINE_SUCCESS;
}


/* get a reply to a previously issued command, tid is the transaction
 * id returned by WiFiEngine_SWM500_Command, data and length point to a buffer
 * to receive data */
int
WiFiEngine_SWM500_Reply(uint32_t tid, void *data, size_t *length)
{
   int status = WIFI_ENGINE_FAILURE;
   
   if((tid & WE_SWM500_TID_CMASK) == WE_SWM500_TID_CONSOLE ||
      (tid & WE_SWM500_TID_CMASK) == WE_SWM500_TID_FLASH) {
      status = WiFiEngine_GetConsoleReply(data, length);
      if(status == WIFI_ENGINE_FAILURE)  /* XXX this should probably
                                            be changed at the source */
         status = WIFI_ENGINE_FAILURE_DEFER;
      
      return status;
   } else if((tid & WE_SWM500_TID_CMASK) == WE_SWM500_TID_MIB) {
      status = WiFiEngine_GetMIBResponse_raw(tid & WE_SWM500_TID_MASK, 
                                             data, length);
   }

   return status;
}

/** @} */ /* End of we_swm500 group */

/* Local Variables: */
/* c-basic-offset: 3 */
/* indent-tabs-mode: nil */
/* End: */
