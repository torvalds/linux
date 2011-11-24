//------------------------------------------------------------------------------
// <copyright file="sdio_lib.h" company="Atheros">
//    Copyright (c) 2007-2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef __SDIO_LIB_H___
#define __SDIO_LIB_H___

#ifdef UNDER_CE
#include "wince\sdio_lib_wince.h"
#endif /* WINCE */

        
    /* a message block */
typedef struct _SDMESSAGE_BLOCK {
    SDLIST  SDList;                   /* list entry */
    INT     MessageLength;            /* number of bytes in this message */
    UINT8   MessageStart[1];          /* message start */
}SDMESSAGE_BLOCK, *PSDMESSAGE_BLOCK;

    /* message queue */
typedef struct _SDMESSAGE_QUEUE {
    SDLIST          MessageList;        /* message list */
    OS_CRITICALSECTION MessageCritSection; /* message semaphore */
    SDLIST          FreeMessageList;    /* free message list */
    INT             MaxMessageLength;   /* max message block length */
}SDMESSAGE_QUEUE, *PSDMESSAGE_QUEUE;
                   
SDIO_STATUS _SDLIB_IssueConfig(PSDDEVICE        pDevice,
                               SDCONFIG_COMMAND Command,
                               PVOID            pData,
                               INT              Length);                                  
void _SDLIB_PrintBuffer(PUCHAR pBuffer, INT Length,PTEXT pDescription);  

PSDMESSAGE_QUEUE _CreateMessageQueue(INT MaxMessages, INT MaxMessageLength);
void _DeleteMessageQueue(PSDMESSAGE_QUEUE pQueue);
SDIO_STATUS _PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, INT MessageLength);
SDIO_STATUS _GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, INT *pBufferLength);

#ifdef CTSYSTEM_NO_FUNCTION_PROXIES
    /* OS port requires no proxy functions, use methods directly from the library */
#define SDLIB_IssueConfig       _SDLIB_IssueConfig
#define SDLIB_CreateMessageQueue    _CreateMessageQueue
#define SDLIB_DeleteMessageQueue    _DeleteMessageQueue
#define SDLIB_PostMessage           _PostMessage
#define SDLIB_GetMessage            _GetMessage
#define SDLIB_PrintBuffer           _SDLIB_PrintBuffer
#else

                        
SDIO_STATUS SDLIB_IssueConfig(PSDDEVICE        pDevice,
                              SDCONFIG_COMMAND Command,
                              PVOID            pData,
                              INT              Length); 
                                   
void SDLIB_PrintBuffer(PUCHAR pBuffer, INT Length,PTEXT pDescription);                  

PSDMESSAGE_QUEUE SDLIB_CreateMessageQueue(INT MaxMessages, INT MaxMessageLength);

void SDLIB_DeleteMessageQueue(PSDMESSAGE_QUEUE pQueue);

SDIO_STATUS SDLIB_PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, INT MessageLength);

SDIO_STATUS SDLIB_GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, INT *pBufferLength);
#endif /* CTSYSTEM_NO_FUNCTION_PROXIES */


SDIO_STATUS SDLIB_OSCreateHelper(POSKERNEL_HELPER pHelper,
                           PHELPER_FUNCTION pFunction, 
                           PVOID            pContext);                            

void SDLIB_OSDeleteHelper(POSKERNEL_HELPER pHelper);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Check message queue is empty
  
  @function name: SDLIB_IsQueueEmpty
  @prototype: BOOL SDLIB_IsQueueEmpty(PSDMESSAGE_QUEUE pQueue)
  @category: Support_Reference
  
  @input: pQueue - message queue to check

  @return: TRUE if empty else false
            
  @see also: SDLIB_CreateMessageQueue 
         
  @example: Check message queue :
              if (SDLIB_IsQueueEmpty(pInstance->pQueue)) {
                   .. message queue is empty
              }
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE BOOL SDLIB_IsQueueEmpty(PSDMESSAGE_QUEUE pQueue) {
    return SDLIST_IS_EMPTY(&pQueue->MessageList);
}


void SDLIB_Init(void);
void SDLIB_Cleanup(void);
       
#endif /* __SDIO_LIB_H___*/
