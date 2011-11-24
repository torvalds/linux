//------------------------------------------------------------------------------
// <copyright file="sdio_lib_c.c" company="Atheros">
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
#define MODULE_NAME  SDLIB_

#include "../include/ctsystem.h"
#include "../include/sdio_busdriver.h"
#include "../include/sdio_lib.h"
 
/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Issue an SDIO configuration command.

  @function name: SDLIB_IssueConfig
  @prototype: SDIO_STATUS _SDLIB_IssueConfig(PSDDEVICE        pDevice,
                                             SDCONFIG_COMMAND Command,
                                             PVOID            pData,
                                             INT              Length)

  @category: PD_Reference
  @input:  pDevice - the device that is the target of the command.
  @input:  Command - command to send, see example.
  @input:  pData - command's data
  @input:  Length length of pData
  
  @output: pData - updated on commands that return data.
  
  @return: SDIO Status
  
  @example: 
  
  @notes: 
   
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDLIB_IssueConfig(PSDDEVICE        pDevice,
                               SDCONFIG_COMMAND Command,
                               PVOID            pData,
                               INT              Length)
{
    SDCONFIG  configHdr; 
    SET_SDCONFIG_CMD_INFO(&configHdr,Command,pData,Length);
    return SDDEVICE_CALL_CONFIG_FUNC(pDevice,&configHdr);
}                
                                        
/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Print a buffer to the debug output

  @function name: SDLIB_PrintBuffer
  @prototype: void SDLIB_PrintBuffer(PUCHAR pBuffer, INT Length, PTEXT pDescription)
  @category: Support_Reference
  
  @input:  pBuffer - Hex buffer to be printed.
  @input:  Length - length of pBuffer.
  @input:  pDescription - String title to be printed above the dump.

  @output: none
   
  @return: none
  
  @notes:  Prints the buffer by converting to ASCII and using REL_PRINT() with 16 
           bytes per line.
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void _SDLIB_PrintBuffer(PUCHAR pBuffer, INT Length, PTEXT pDescription)
{
    TEXT  line[49];
    TEXT  address[5];
    TEXT  ascii[17];
    TEXT  temp[5];
    INT   i;
    UCHAR num;
    USHORT offset = 0;

    REL_PRINT(0,
              ("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n"));
    if (pDescription != NULL) {
        REL_PRINT(0, ("Description: %s \n\n",pDescription));
    } else {
        REL_PRINT(0, ("Description: NONE \n\n"));
    }
    REL_PRINT(0,
              ("Offset                   Data                               ASCII        \n"));
    REL_PRINT(0,
              ("--------------------------------------------------------------------------\n"));
 
    while (Length) {
        line[0] = (TEXT)0;
        ascii[0] = (TEXT)0;
        address[0] = (TEXT)0;
        sprintf(address,"%4.4X",offset);
        for (i = 0; i < 16; i++) {
            if (Length != 0) {
                num = *pBuffer;
                sprintf(temp,"%2.2X ",num);
                strcat(line,temp);
                if ((num >= 0x20) && (num <= 0x7E)) {
                    sprintf(temp,"%c",*pBuffer);
                } else {
                    sprintf(temp,"%c",0x2e);
                }            
                strcat(ascii,temp);
                pBuffer++;
                Length--;  
            } else {
                    /* pad partial line with spaces */
                strcat(line,"   ");
                strcat(ascii," ");   
            }
        } 
        REL_PRINT(0,("%s    %s   %s\n", address, line, ascii));
        offset += 16;
    }
    REL_PRINT(0,
              ("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n"));
  
}


static INLINE void FreeMessageBlock(PSDMESSAGE_QUEUE pQueue, PSDMESSAGE_BLOCK pMsg) {
    SDListInsertHead(&pQueue->FreeMessageList, &pMsg->SDList);
}
static INLINE void QueueMessageBlock(PSDMESSAGE_QUEUE pQueue, PSDMESSAGE_BLOCK pMsg) {
    SDListInsertTail(&pQueue->MessageList, &pMsg->SDList);
}
static INLINE void QueueMessageToHead(PSDMESSAGE_QUEUE pQueue, PSDMESSAGE_BLOCK pMsg) {
    SDListInsertHead(&pQueue->MessageList, &pMsg->SDList);    
}

static INLINE PSDMESSAGE_BLOCK GetFreeMessageBlock(PSDMESSAGE_QUEUE pQueue) {
    PSDLIST pItem = SDListRemoveItemFromHead(&pQueue->FreeMessageList);
    if (pItem != NULL) {
        return CONTAINING_STRUCT(pItem, SDMESSAGE_BLOCK , SDList);  
    }
    return NULL;
}
static INLINE PSDMESSAGE_BLOCK GetQueuedMessage(PSDMESSAGE_QUEUE pQueue) {
    PSDLIST pItem = SDListRemoveItemFromHead(&pQueue->MessageList);
    if (pItem != NULL) {
        return CONTAINING_STRUCT(pItem, SDMESSAGE_BLOCK , SDList);  
    }
    return NULL;
}

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Create a message queue

  @function name: SDLIB_CreateMessageQueue
  @prototype: PSDMESSAGE_QUEUE SDLIB_CreateMessageQueue(INT MaxMessages, INT MaxMessageLength)
  @category: Support_Reference
  
  @input: MaxMessages - Maximum number of messages this queue supports
  @input: MaxMessageLength - Maximum size of each message
 
  @return: Message queue object, NULL on failure
  
  @notes:  This function creates a simple first-in-first-out message queue.  The caller must determine 
           the maximum number of messages the queue supports and the size of each message.  This
           function will pre-allocate memory for each message. A producer of data posts a message
           using SDLIB_PostMessage with a user defined data structure. A consumer of this data 
           can retrieve the message (in FIFO order) using SDLIB_GetMessage. A message queue does not
           provide a signaling mechanism for notifying a consumer of data. Notifying a consumer is 
           user defined.
  
  @see also: SDLIB_DeleteMessageQueue, SDLIB_GetMessage, SDLIB_PostMessage.
  
  @example: Creating a message queue:
       typedef struct _MyMessage {
           UINT8 Code;
           PVOID pDataBuffer;
       } MyMessage;
            // create message queue, 16 messages max.
       pMsgQueue = SDLIB_CreateMessageQueue(16,sizeof(MyMessage));
       if (NULL == pMsgQueue) {
           .. failed
       }
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
PSDMESSAGE_QUEUE _CreateMessageQueue(INT MaxMessages, INT MaxMessageLength)
{
    PSDMESSAGE_QUEUE pQueue = NULL;
    SDIO_STATUS      status = SDIO_STATUS_SUCCESS;
    INT              ii;
    PSDMESSAGE_BLOCK pMsg;
    
    do {
        pQueue = (PSDMESSAGE_QUEUE)KernelAlloc(sizeof(SDMESSAGE_QUEUE));   
        
        if (NULL == pQueue) {
            status = SDIO_STATUS_NO_RESOURCES;
            break; 
        }
        SDLIST_INIT(&pQueue->MessageList);  
        SDLIST_INIT(&pQueue->FreeMessageList);  
        pQueue->MaxMessageLength = MaxMessageLength;       
        status = CriticalSectionInit(&pQueue->MessageCritSection);
        if (!SDIO_SUCCESS(status)) {
            break;   
        }       
            /* allocate message blocks */
        for (ii = 0; ii < MaxMessages; ii++) {
            pMsg = (PSDMESSAGE_BLOCK)KernelAlloc(sizeof(SDMESSAGE_BLOCK) + MaxMessageLength -1); 
            if (NULL == pMsg) {
                break;    
            }
            FreeMessageBlock(pQueue, pMsg);
        } 
       
        if (0 == ii) {
            status = SDIO_STATUS_NO_RESOURCES;
            break; 
        }
        
    } while (FALSE);      
  
    if (!SDIO_SUCCESS(status)) {
        if (pQueue != NULL) {
            _DeleteMessageQueue(pQueue);    
            pQueue = NULL;  
        } 
    }
    return pQueue;  
}

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Delete a message queue

  @function name: SDLIB_DeleteMessageQueue
  @prototype: void SDLIB_DeleteMessageQueue(PSDMESSAGE_QUEUE pQueue)
  @category: Support_Reference
  
  @input: pQueue - message queue to delete
  
  @notes: This function flushes the message queue and frees all memory allocated for
          messages.
  
  @see also: SDLIB_CreateMessageQueue
  
  @example: Deleting a message queue:
       if (pMsgQueue != NULL) {
            SDLIB_DeleteMessageQueue(pMsgQueue);
       }
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void _DeleteMessageQueue(PSDMESSAGE_QUEUE pQueue)
{
    PSDMESSAGE_BLOCK pMsg;
    SDIO_STATUS     status;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    status = CriticalSectionAcquireSyncIrq(&pQueue->MessageCritSection);
    
        /* cleanup free list */
    while (1) {
        pMsg = GetFreeMessageBlock(pQueue);
        if (pMsg != NULL) {
            KernelFree(pMsg); 
        } else {
            break;   
        }
    }  
        /* cleanup any in the queue */
    while (1) {
        pMsg = GetQueuedMessage(pQueue);
        if (pMsg != NULL) {
            KernelFree(pMsg); 
        } else {
            break;   
        }
    }
    
    status = CriticalSectionReleaseSyncIrq(&pQueue->MessageCritSection);  
    CriticalSectionDelete(&pQueue->MessageCritSection);  
    KernelFree(pQueue);
    
}

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Post a message queue

  @function name: SDLIB_PostMessage
  @prototype: SDIO_STATUS SDLIB_PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, INT MessageLength)
  @category: Support_Reference
  
  @input: pQueue - message queue to post to
  @input: pMessage - message to post
  @input: MessageLength - length of message (for validation)
  
  @return: SDIO_STATUS
  
  @notes: The message queue uses an internal list of user defined message structures.  When
          posting a message the message is copied into an allocated structure and queued.  The memory 
          pointed to by pMessage does not need to be allocated and can reside on the stack. 
          The length of the message to post can be smaller that the maximum message size. This allows
          for variable length messages up to the maximum message size. This 
          function returns SDIO_STATUS_NO_RESOURCES, if the message queue is full.  This
          function returns SDIO_STATUS_BUFFER_TOO_SMALL, if the message size exceeds the maximum
          size of a message.  Posting and getting messsages from a message queue is safe in any
          driver context.
            
  @see also: SDLIB_CreateMessageQueue , SDLIB_GetMessage
  
  @example: Posting a message
       MyMessage message;
           // set up message
       message.code = MESSAGE_DATA_READY;
       message.pData = pInstance->pDataBuffers[currentIndex];
           // post message       
       status = SDLIB_PostMessage(pInstance->pReadQueue,&message,sizeof(message));
       if (!SDIO_SUCCESS(status)) {
           // failed
       }
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, INT MessageLength)
{
    SDIO_STATUS status2;
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDMESSAGE_BLOCK pMsg;  
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    if (MessageLength > pQueue->MaxMessageLength) {
        return SDIO_STATUS_BUFFER_TOO_SMALL;
    }
   
    status = CriticalSectionAcquireSyncIrq(&pQueue->MessageCritSection);
    if (!SDIO_SUCCESS(status)) {
        return status;
    }
    
    do {        
            /* get a message block */
        pMsg = GetFreeMessageBlock(pQueue);       
        if (NULL == pMsg) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }
            /* copy the message */
        memcpy(pMsg->MessageStart,pMessage,MessageLength);
            /* set the length of the message */
        pMsg->MessageLength = MessageLength;
            /* queue the message to the list  */
        QueueMessageBlock(pQueue,pMsg);
    } while (FALSE);
    
    status2 = CriticalSectionReleaseSyncIrq(&pQueue->MessageCritSection);
    return status; 
}

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get a message from a message queue

  @function name: SDLIB_GetMessage
  @prototype: SDIO_STATUS SDLIB_GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, INT *pBufferLength) 
  @category: Support_Reference
  
  @input: pQueue - message queue to retreive a message from
  @input: pBufferLength - on entry, the length of the data buffer
  @output: pData - buffer to hold the message
  @output: pBufferLength - on return, contains the number of bytes copied

  @return: SDIO_STATUS
  
  @notes: The message queue uses an internal list of user defined message structures.  The message is
          dequeued (FIFO order) and copied to the callers buffer.  The internal allocation for the message
          is returned back to the message queue. This function returns SDIO_STATUS_NO_MORE_MESSAGES
          if the message queue is empty. If the length of the buffer is smaller than the length of 
          the message at the head of the queue,this function returns SDIO_STATUS_BUFFER_TOO_SMALL and
          returns the required length in pBufferLength.
            
  @see also: SDLIB_CreateMessageQueue , SDLIB_PostMessage
  
  @example: Getting a message
       MyMessage message;
       INT       length;
           // set length
       length = sizeof(message);
           // post message       
       status = SDLIB_GetMessage(pInstance->pReadQueue,&message,&length);
       if (!SDIO_SUCCESS(status)) {
           // failed
       }
       
  @example: Checking queue for a message and getting the size of the message
       INT       length;
           // use zero length to get the size of the message
       length = 0;
       status = SDLIB_GetMessage(pInstance->pReadQueue,NULL,&length);
       if (status == SDIO_STATUS_NO_MORE_MESSAGES) {
            // no messages in queue 
       } else if (status == SDIO_STATUS_BUFFER_TOO_SMALL) {
            // message exists in queue and length of message is returned
            messageSizeInQueue = length;
       } else {
            // some other failure
       }
       
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, INT *pBufferLength) 
{
    SDIO_STATUS status2;
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;  
    PSDMESSAGE_BLOCK pMsg; 
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    status = CriticalSectionAcquireSyncIrq(&pQueue->MessageCritSection);
    if (!SDIO_SUCCESS(status)) {
        return status;
    }
    
    do {
        pMsg = GetQueuedMessage(pQueue);
        if (NULL == pMsg) {
            status = SDIO_STATUS_NO_MORE_MESSAGES;
            break;    
        } 
        if (*pBufferLength < pMsg->MessageLength) {
                /* caller buffer is too small */
            *pBufferLength = pMsg->MessageLength;
                /* stick it back to the front */
            QueueMessageToHead(pQueue, pMsg);
            status = SDIO_STATUS_BUFFER_TOO_SMALL;
            break;
        }    
            /* copy the message to the callers buffer */
        memcpy(pData,pMsg->MessageStart,pMsg->MessageLength);
            /* return actual length */
        *pBufferLength = pMsg->MessageLength; 
            /* return this message block back to the free list  */
        FreeMessageBlock(pQueue, pMsg);
        
    } while (FALSE);
    
    status2 = CriticalSectionReleaseSyncIrq(&pQueue->MessageCritSection);
    
    return status;     
}

/* the following documents the OS helper APIs */

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Create an OS-specific helper task/thread

  @function name: SDLIB_OSCreateHelper
  @prototype: SDIO_STATUS SDLIB_OSCreateHelper(POSKERNEL_HELPER pHelper,
                                               PHELPER_FUNCTION pFunction, 
                                               PVOID            pContext)
  @category: Support_Reference
  
  @input: pHelper - caller allocated helper object
  @input: pFunction - helper function
  @input: pContext - helper context
  
  @return: SDIO_STATUS
  
  @notes: This function creates a helper task/thread that runs in a new execution context. The newly 
          created task/thread invokes the helper function. The thread/task exits when the helper
          function returns.  The helper function has the prototype of:
          THREAD_RETURN HelperFunction(POSKERNEL_HELPER pHelper)
          The helper function usually implements a while loop and suspends execution using
          SD_WAIT_FOR_WAKEUP().  On exit the helper function can return an OS-specific THREAD_RETURN
          code (usually zero). The helper function executes in a fully schedule-able context and
          can block on semaphores and sleep.
  
  @see also: SDLIB_OSDeleteHelper , SD_WAIT_FOR_WAKEUP
  
  @example: A thread helper function:
       THREAD_RETURN HelperFunction(POSKERNEL_HELPER pHelper) 
       {
           SDIO_STATUS status;
           PMYCONTEXT pContext = (PMYCONTEXT)SD_GET_OS_HELPER_CONTEXT(pHelper); 
                // wait for wake up 
           while(1) {
                  status = SD_WAIT_FOR_WAKEUP(pHelper);
                  if (!SDIO_SUCCESS(status)) {
                      break;
                  }
                  if (SD_IS_HELPER_SHUTTING_DOWN(pHelper)) {
                      //... shutting down
                      break;
                  }
                  // handle wakeup...
            }
            return 0;
       }
       
  @example: Creating a helper:
       status = SDLIB_OSCreateHelper(&pInstance->OSHelper,HelperFunction,pInstance);
       if (!SDIO_SUCCESS(status)) {
           // failed
       }
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Delete an OS helper task/thread

  @function name: SDLIB_OSDeleteHelper
  @prototype: void SDLIB_OSDeleteHelper(POSKERNEL_HELPER pHelper)
  @category: Support_Reference
  
  @input: pHelper - caller allocated helper object
  
  @notes: This function wakes the helper and waits(blocks) until the helper exits. The caller can
          only pass an OS helper structure that was initialized sucessfully by
          SDLIB_OSCreateHelper.  The caller must be in a schedulable context.  
  
  @see also: SDLIB_OSCreateHelper
  
  @example: Deleting a helper:
       if (pInstance->HelperCreated) {
               // clean up the helper if we successfully created it
           SDLIB_OSDeleteHelper(&pInstance->OSHelper);
       }
       
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/


