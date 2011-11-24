// Copyright (c) 2004-2006 Atheros Communications Inc.
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
// Portions of this code were developed with information supplied from the 
// SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
//
//  The following conditions apply to the release of the SD simplified specification (“Simplified
//  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
//  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
//  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
//  Specification may require a license from the SD Card Association or other third parties.
//  Disclaimers:
//  The information contained in the Simplified Specification is presented only as a standard 
//  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
//  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
//  any damages, any infringements of patents or other right of the SD Card Association or any third 
//  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
//  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
//  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
//  information, know-how or other confidential information to any third party.
//
//
// The initial developers of the original code are Seung Yi and Paul Lever
//
// sdio@atheros.com
//
//

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_lib.h

@abstract: SDIO Library include

#notes: 
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef __SDIO_LIB_H___
#define __SDIO_LIB_H___

#ifdef UNDER_CE
#include "wince\sdio_lib_wince.h"
#endif /* WINCE */

#define CMD52_DO_READ  FALSE
#define CMD52_DO_WRITE TRUE

    /* read/write macros to any function */
#define Cmd52WriteByteFunc(pDev,Func,Address,pValue) \
                SDLIB_IssueCMD52((pDev),(Func),(Address),(pValue),1,CMD52_DO_WRITE)
#define Cmd52ReadByteFunc(pDev,Func,Address,pValue) \
                SDLIB_IssueCMD52((pDev),(Func),(Address),pValue,1,CMD52_DO_READ)
#define Cmd52ReadMultipleFunc(pDev,Func, Address, pBuf,length) \
                SDLIB_IssueCMD52((pDev),(Func),(Address),(pBuf),(length),CMD52_DO_READ)
                                  
   /* macros to access common registers */              
#define Cmd52WriteByteCommon(pDev, Address, pValue) \
                Cmd52WriteByteFunc((pDev),0,(Address),(pValue))
#define Cmd52ReadByteCommon(pDev, Address, pValue) \
                Cmd52ReadByteFunc((pDev),0,(Address),(pValue))
#define Cmd52ReadMultipleCommon(pDev, Address, pBuf,length) \
                Cmd52ReadMultipleFunc((pDev),0,(Address),(pBuf),(length)) 

#define SDLIB_SetupCMD52RequestAsync(f,a,w,wd,pR)   \
{                                                   \
    SDLIB_SetupCMD52Request((f),(a),(w),(wd),(pR)); \
    (pR)->Flags |= SDREQ_FLAGS_TRANS_ASYNC;         \
}
        
    /* a message block */
typedef struct _SDMESSAGE_BLOCK {
    SDLIST  SDList;                   /* list entry */
    UINT    MessageLength;            /* number of bytes in this message */
    UINT8   MessageStart[1];          /* message start */
}SDMESSAGE_BLOCK, *PSDMESSAGE_BLOCK;

    /* message queue */
typedef struct _SDMESSAGE_QUEUE {
    SDLIST          MessageList;        /* message list */
    OS_CRITICALSECTION MessageCritSection; /* message semaphore */
    SDLIST          FreeMessageList;    /* free message list */
    UINT            MaxMessageLength;   /* max message block length */
}SDMESSAGE_QUEUE, *PSDMESSAGE_QUEUE;
          
/* internal library prototypes that can be proxied */
SDIO_STATUS _SDLIB_IssueCMD52(PSDDEVICE     pDevice,
                        UINT8         FuncNo,
                        UINT32        Address,
                        PUINT8        pData,
                        INT           ByteCount,
                        BOOL          Write);                        
SDIO_STATUS _SDLIB_FindTuple(PSDDEVICE  pDevice,
                             UINT8      Tuple,
                             UINT32     *pTupleScanAddress,
                             PUINT8     pBuffer,
                             UINT8      *pLength);                               
SDIO_STATUS _SDLIB_IssueConfig(PSDDEVICE        pDevice,
                               SDCONFIG_COMMAND Command,
                               PVOID            pData,
                               INT              Length);                                  
void _SDLIB_PrintBuffer(PUCHAR pBuffer, INT Length,PTEXT pDescription);  
void _SDLIB_SetupCMD52Request(UINT8         FuncNo,
                              UINT32        Address,
                              BOOL          Write,
                              UINT8         WriteData,                                    
                              PSDREQUEST    pRequest);                             
SDIO_STATUS _SDLIB_SetFunctionBlockSize(PSDDEVICE        pDevice,
                                        UINT16           BlockSize);
                                        
SDIO_STATUS _SDLIB_GetDefaultOpCurrent(PSDDEVICE  pDevice, 
                                       SD_SLOT_CURRENT *pOpCurrent); 
PSDMESSAGE_QUEUE _CreateMessageQueue(INT MaxMessages, UINT MaxMessageLength);
void _DeleteMessageQueue(PSDMESSAGE_QUEUE pQueue);
SDIO_STATUS _PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, UINT MessageLength);
SDIO_STATUS _GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, UINT *pBufferLength);

#ifdef CTSYSTEM_NO_FUNCTION_PROXIES
    /* OS port requires no proxy functions, use methods directly from the library */
#define SDLIB_IssueCMD52        _SDLIB_IssueCMD52
#define SDLIB_SetupCMD52Request _SDLIB_SetupCMD52Request
#define SDLIB_FindTuple         _SDLIB_FindTuple
#define SDLIB_IssueConfig       _SDLIB_IssueConfig
#define SDLIB_SetFunctionBlockSize  _SDLIB_SetFunctionBlockSize
#define SDLIB_GetDefaultOpCurrent   _SDLIB_GetDefaultOpCurrent
#define SDLIB_CreateMessageQueue    _CreateMessageQueue
#define SDLIB_DeleteMessageQueue    _DeleteMessageQueue
#define SDLIB_PostMessage           _PostMessage
#define SDLIB_GetMessage            _GetMessage
#define SDLIB_PrintBuffer           _SDLIB_PrintBuffer
#else

/* proxied versions */
SDIO_STATUS SDLIB_IssueCMD52(PSDDEVICE     pDevice,
                             UINT8         FuncNo,
                             UINT32        Address,
                             PUINT8        pData,
                             INT           ByteCount,
                             BOOL          Write); 
                                               
void SDLIB_SetupCMD52Request(UINT8         FuncNo,
                             UINT32        Address,
                             BOOL          Write,
                             UINT8         WriteData,                                    
                             PSDREQUEST    pRequest);
                             
SDIO_STATUS SDLIB_FindTuple(PSDDEVICE  pDevice,
                        UINT8      Tuple,
                        UINT32     *pTupleScanAddress,
                        PUINT8     pBuffer,
                        UINT8      *pLength);   
                        
SDIO_STATUS SDLIB_IssueConfig(PSDDEVICE        pDevice,
                              SDCONFIG_COMMAND Command,
                              PVOID            pData,
                              INT              Length); 
                                   
SDIO_STATUS SDLIB_SetFunctionBlockSize(PSDDEVICE        pDevice,
                                       UINT16           BlockSize);
                                       
void SDLIB_PrintBuffer(PUCHAR pBuffer, INT Length,PTEXT pDescription);                  

SDIO_STATUS SDLIB_GetDefaultOpCurrent(PSDDEVICE  pDevice, SD_SLOT_CURRENT *pOpCurrent);

PSDMESSAGE_QUEUE SDLIB_CreateMessageQueue(INT MaxMessages, UINT MaxMessageLength);

void SDLIB_DeleteMessageQueue(PSDMESSAGE_QUEUE pQueue);

SDIO_STATUS SDLIB_PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, UINT MessageLength);

SDIO_STATUS SDLIB_GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, UINT *pBufferLength);
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


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Issue an I/O abort request
  
  @function name: SDLIB_IssueIOAbort
  @prototype: SDIO_STATUS SDLIB_IssueIOAbort(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input: pDevice - the device that is the target of this request

  @return: SDIO_STATUS
            
  @notes: This procedure can be called to issue an I/O abort request to an I/O function.
          This procedure cannot be used to abort a data (block) transfer already in progress.
          It is intended to be used when a data (block) transfer completes with an error and only if 
          the I/O function requires an abort action.  Some I/O functions may automatically
          recover from such failures and not require this action. This function issues
          the abort command synchronously and can potentially block.
          If an async request is required, you must allocate a request and use 
          SDLIB_SetupIOAbortAsync() to prepare the request.
          
  @example: Issuing I/O Abort synchronously :
              .. check status from last block operation:
              if (status == SDIO_STATUS_BUS_READ_TIMEOUT) {
                   .. on failure, issue I/O abort
                   status2 = SDLIB_IssueIOAbort(pDevice);
              }
            Issuing I/O Abort asynchronously:
                ... allocate a request
                ... setup the request:
                 SDLIB_SetupIOAbortAsync(pDevice,pReq);
                 pReq->pCompletion = myIOAbortCompletion;
                 pReq->pCompleteContext = pDevice; 
                 status = SDDEVICE_CALL_REQUEST_FUNC(pDevice,pReq);
   
   @see also: SDLIB_SetupIOAbortAsync              
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static INLINE SDIO_STATUS SDLIB_IssueIOAbort(PSDDEVICE pDevice) {
    UINT8 value = SDDEVICE_GET_SDIO_FUNCNO(pDevice);
    return Cmd52WriteByteCommon(pDevice,0x06,&value);
}   

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Setup an I/O abort request for async operation
  
  @function name: SDLIB_SetupIOAbortAsync
  @prototype: SDLIB_SetupIOAbortAsync(PSDDEVICE pDevice, PSDREQUEST pRequest)
  @category: PD_Reference
  
  @input: pDevice - the device that is the target of this request
          pRequest - the request to set up
            
  @see also: SDLIB_IssueIOAbort   
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDLIB_SetupIOAbortAsync(pDevice, pReq) \
        SDLIB_SetupCMD52RequestAsync(0,0x06,TRUE,SDDEVICE_GET_SDIO_FUNCNO(pDevice),(pReq))
               
               
#endif /* __SDIO_LIB_H___*/
