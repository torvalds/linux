// Copyright (c) 2004, 2005 Atheros Communications Inc.
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
@file: sdio_lib_c.c

@abstract: OS independent SDIO library functions
@category abstract: Support_Reference Support Functions.

@notes: Support functions for device I/O 
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define MODULE_NAME  SDLIB_

#include "../include/ctsystem.h"
#include "../include/sdio_busdriver.h"
#include "../include/_sdio_defs.h"
#include "../include/sdio_lib.h"
#include "_sdio_lib.h"
 
#define _Cmd52WriteByteCommon(pDev, Address, pValue) \
                _SDLIB_IssueCMD52((pDev),0,(Address),(pValue),1,TRUE)
#define _Cmd52ReadByteCommon(pDev, Address, pValue) \
                _SDLIB_IssueCMD52((pDev),0,(Address),pValue,1,FALSE)
#define _Cmd52ReadMultipleCommon(pDev, Address, pBuf,length) \
                _SDLIB_IssueCMD52((pDev),0,(Address),(pBuf),(length),FALSE) 

/* inline version */
static INLINE void _iSDLIB_SetupCMD52Request(UINT8         FuncNo,
                                             UINT32        Address,
                                             BOOL          Write,
                                             UINT8         WriteData,                                    
                                             PSDREQUEST    pRequest) {                                
    if (Write) {
        SDIO_SET_CMD52_ARG(pRequest->Argument,CMD52_WRITE,
                           FuncNo,
                           CMD52_NORMAL_WRITE,Address,WriteData);
    } else {
        SDIO_SET_CMD52_ARG(pRequest->Argument,CMD52_READ,FuncNo,0,Address,0x00);
    }
    
    pRequest->Flags = SDREQ_FLAGS_RESP_SDIO_R5;
    pRequest->Command = CMD52;                                
}
               
/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Setup cmd52 requests
  
  @function name: SDLIB_SetupCMD52Request
  @prototype: void SDLIB_SetupCMD52Request(UINT8         FuncNo,
                                           UINT32        Address,
                                           BOOL          Write,
                                           UINT8         WriteData,                                    
                                           PSDREQUEST    pRequest)
  @category: PD_Reference
  
  @input:  FunctionNo - function number.
  @input:  Address - I/O address, 17-bit register address.
  @input:  Write  - TRUE if a write operation, FALSE for reads.
  @input:  WriteData - write data, byte to write if write operation.
  
  @output: pRequest - request is updated with cmd52 parameters
  
  @return: none
  
  @notes: This function does not perform any I/O. For register reads, the completion 
          routine can use the SD_R5_GET_READ_DATA() macro to extract the register value. 
          The routine should also extract the response flags using the SD_R5_GET_RESP_FLAGS()
          macro and check the flags with the SD_R5_ERRORS mask.
          
  @example: Getting the register value from the completion routine: 
          flags = SD_R5_GET_RESP_FLAGS(pRequest->Response);
          if (flags & SD_R5_ERRORS) {
             ... errors
          } else {          
             registerValue = SD_R5_GET_READ_DATA(pRequest->Response);
          }  
  
  @see also: SDLIB_IssueCMD52 
  @see also: SDDEVICE_CALL_REQUEST_FUNC
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void _SDLIB_SetupCMD52Request(UINT8         FuncNo,
                              UINT32        Address,
                              BOOL          Write,
                              UINT8         WriteData,                                    
                              PSDREQUEST    pRequest)
{
    _iSDLIB_SetupCMD52Request(FuncNo,Address,Write,WriteData,pRequest);      
}                

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Issue a CMD52 to read or write a register

  @function name: SDLIB_IssueCMD52
  @prototype: SDIO_STATUS SDLIB_IssueCMD52(PSDDEVICE     pDevice,
                                           UINT8         FuncNo,
                                           UINT32        Address,
                                           PUINT8        pData,
                                           INT           ByteCount,
                                           BOOL          Write)
  @category: PD_Reference
  @input: pDevice - the device that is the target of the command.
  @input: FunctionNo - function number of the target.
  @input: Address - 17-bit register address.
  @input: ByteCount - number of bytes to read or write,
  @input: Write - TRUE if a write operation, FALSE for reads.
  @input: pData - data buffer for writes.
  
  @output: pData - data buffer for writes.

  @return: SDIO Status
  
  @notes:  This function will allocate a request and issue multiple byte reads or writes
           to satisfy the ByteCount requested.  This function is fully synchronous and will block
           the caller.
  
  @see also: SDLIB_SetupCMD52Request
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDLIB_IssueCMD52(PSDDEVICE     pDevice,
                              UINT8         FuncNo,
                              UINT32        Address,
                              PUINT8        pData,
                              INT           ByteCount,
                              BOOL          Write)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    
    PSDREQUEST  pReq = NULL;

    pReq = SDDeviceAllocRequest(pDevice);
    
    if (NULL == pReq) {
        return SDIO_STATUS_NO_RESOURCES;    
    }
                             
    while (ByteCount) {
        _iSDLIB_SetupCMD52Request(FuncNo,Address,Write,*pData,pReq);
        status = SDDEVICE_CALL_REQUEST_FUNC(pDevice,pReq);
        if (!SDIO_SUCCESS(status)) {
            break;   
        }  
        
        status = ConvertCMD52ResponseToSDIOStatus(SD_R5_GET_RESP_FLAGS(pReq->Response));
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Library: CMD52 resp error: 0x%X \n", 
                                    SD_R5_GET_RESP_FLAGS(pReq->Response)));
            break; 
        }
        if (!Write) {
                /* store the byte */            
            *pData =  SD_R5_GET_READ_DATA(pReq->Response);
        }
        pData++;
        Address++;                          
        ByteCount--;   
    }  
  
    SDDeviceFreeRequest(pDevice,pReq);
    return status;  
}

     
                                                                          
/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Find a device's tuple.
  
  @function name: SDLIB_FindTuple
  @prototype: SDIO_STATUS SDLIB_FindTuple(PSDDEVICE  pDevice,
                                          UINT8      Tuple,
                                          UINT32     *pTupleScanAddress,
                                          PUINT8     pBuffer,
                                          UINT8      *pLength)

  @category: PD_Reference
  @input: pDevice - the device that is the target of the command.
  @input: Tuple - 8-bit ID of tuple to find          
  @input: pTupleScanAddress - On entry pTupleScanAddress is the adddress to start scanning
  @input: pLength - length of pBuffer 
  
  @output: pBuffer - storage for tuple
  @output: pTupleScanAddress - address of the next tuple 
  @output: pLength - length of tuple read

  @return: status
  
  @notes: It is possible to have the same tuple ID multiple times with different lengths. This function
          blocks and is fully synchronous.
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDLIB_FindTuple(PSDDEVICE  pDevice,
                             UINT8      Tuple,
                             UINT32     *pTupleScanAddress,
                             PUINT8     pBuffer,
                             UINT8      *pLength)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT32      scanStart = *pTupleScanAddress;
    UINT8       tupleCode;
    UINT8       tupleLink;
    
        /* sanity check */
    if (scanStart < SDIO_CIS_AREA_BEGIN) {
        return SDIO_STATUS_CIS_OUT_OF_RANGE; 
    }
   
    while (TRUE) {           
            /* check for end */
        if (scanStart > SDIO_CIS_AREA_END) {
            status = SDIO_STATUS_TUPLE_NOT_FOUND;
            break;   
        }          
            /* get the code */
        status = _Cmd52ReadByteCommon(pDevice, scanStart, &tupleCode);
        if (!SDIO_SUCCESS(status)) {
            break;   
        } 
        if (CISTPL_END == tupleCode) {
                /* found the end */
            status = SDIO_STATUS_TUPLE_NOT_FOUND;
            break; 
        }
            /* bump past tuple code */
        scanStart++;
            /* get the tuple link value */
        status = _Cmd52ReadByteCommon(pDevice, scanStart, &tupleLink);
        if (!SDIO_SUCCESS(status)) {
            break;   
        }
            /* bump past tuple link*/
        scanStart++;           
            /* check tuple we just found */
        if (tupleCode == Tuple) {
             DBG_PRINT(SDDBG_TRACE, ("SDIO Library: Tuple:0x%2.2X Found at Address:0x%X, TupleLink:0x%X \n",
                                     Tuple, (scanStart - 2), tupleLink));
            if (tupleLink != CISTPL_LINK_END) {
                    /* return the next scan address to the caller */
                *pTupleScanAddress = scanStart + tupleLink; 
            } else {
                    /* the tuple link is an end marker */ 
                *pTupleScanAddress = 0xFFFFFFFF;
            }
                /* go get the tuple */
            status = _Cmd52ReadMultipleCommon(pDevice, scanStart,pBuffer,min(*pLength,tupleLink));         
            if (SDIO_SUCCESS(status)) {
                    /* set the actual return length */
                *pLength = min(*pLength,tupleLink); 
            }
                /* break out of loop */
            break;
        }
            /*increment past this entire tuple */
        scanStart += tupleLink;
    }
    
    return status;
}

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
  
  @example: Command and data pairs:
            Type                               Data
            SDCONFIG_GET_WP             SDCONFIG_WP_VALUE 
            SDCONFIG_SEND_INIT_CLOCKS   none 
            SDCONFIG_SDIO_INT_CTRL      SDCONFIG_SDIO_INT_CTRL_DATA
            SDCONFIG_SDIO_REARM_INT     none 
            SDCONFIG_BUS_MODE_CTRL      SDCONFIG_BUS_MODE_DATA
            SDCONFIG_POWER_CTRL         SDCONFIG_POWER_CTRL_DATA
  
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
  @function: Set function block size

  @function name: SDLIB_SetFunctionBlockSize
  @prototype: SDIO_STATUS SDLIB_SetFunctionBlockSize(PSDDEVICE        pDevice,
                                                     UINT16           BlockSize)

  @category: PD_Reference
  @input:  pDevice - the device that is the target of the command.
  @input:  BlockSize - block size to set in function 

  @output: none
   
  @return: SDIO Status
  
  @notes:  Issues CMD52 to set the block size.  This function is fully synchronous and may
           block.
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDLIB_SetFunctionBlockSize(PSDDEVICE        pDevice,
                                        UINT16           BlockSize)
{
    UINT8   data[2];
        
      /* endian safe */
    data[0] = (UINT8)BlockSize;
    data[1] = (UINT8)(BlockSize >> 8);
        /* write the function blk size control register */
    return _SDLIB_IssueCMD52(pDevice,
                             0,    /* function 0 register space */
                             FBR_FUNC_BLK_SIZE_LOW_OFFSET(CalculateFBROffset(
                             SDDEVICE_GET_SDIO_FUNCNO(pDevice))),
                             data,
                             2,
                             TRUE);
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

/**++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get default operational current

  @function name: SDLIB_GetDefaultOpCurrent
  @prototype: SDIO_STATUS SDLIB_GetDefaultOpCurrent(PSDDEVICE  pDevice, SD_SLOT_CURRENT *pOpCurrent)
  @category: PD_Reference
  
  @input: pDevice - the device that is the target of the command.
  
  @output: pOpCurrent - operational current in mA.

  @return: SDIO_STATUS
  
  @notes:  This routine reads the function's CISTPL_FUNCE tuple for the default operational
           current. For SDIO 1.0 devices this value is read from the 8-bit TPLFE_OP_MAX_PWR
           field.  For SDIO 1.1 devices, the HP MAX power field is used only if the device is
           operating in HIPWR mode. Otherwise the 8-bit TPLFE_OP_MAX_PWR field is used. 
           Some systems may restrict high power/current mode and force cards to operate in a 
           legacy (< 200mA) mode.  This function is fully synchronous and will block the caller.
           
   @example: Getting the default operational current for this function:
            // get default operational current
       status = SDLIB_GetDefaultOpCurrent(pDevice, &slotCurrent);
       if (!SDIO_SUCCESS(status)) {
           .. failed
       }
  
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDLIB_GetDefaultOpCurrent(PSDDEVICE  pDevice, SD_SLOT_CURRENT *pOpCurrent) 
{
    UINT32              nextTpl;
    UINT8               tplLength;
    struct SDIO_FUNC_EXT_FUNCTION_TPL_1_1 funcTuple;
    SDIO_STATUS         status;
    
      /* get the FUNCE tuple */
    nextTpl = SDDEVICE_GET_SDIO_FUNC_CISPTR(pDevice);
    tplLength = sizeof(funcTuple); 
        /* go get the function Extension tuple */
    status = _SDLIB_FindTuple(pDevice,
                              CISTPL_FUNCE,
                              &nextTpl,
                              (PUINT8)&funcTuple,
                              &tplLength);
    
    if (!SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_ERROR, ("SDLIB_GetDefaultOpCurrent: Failed to get FuncE Tuple: %d \n", status));
        return status;  
    }    
       /* use the operational power (8-bit) value of current in mA as default*/
    *pOpCurrent = funcTuple.CommonInfo.OpMaxPwr;        
    if ((tplLength >= sizeof(funcTuple)) && (SDDEVICE_IS_SDIO_REV_GTEQ_1_10(pDevice))) {
            /* we have a 1.1 tuple */        
             /* check for HIPWR mode */
        if (SDDEVICE_GET_CARD_FLAGS(pDevice) & CARD_HIPWR) {        
                /* use the maximum operational power (16 bit ) from the tuple */
            *pOpCurrent = CT_LE16_TO_CPU_ENDIAN(funcTuple.HiPwrMaxPwr); 
        }
    } 
    return SDIO_STATUS_SUCCESS;  
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
  @prototype: PSDMESSAGE_QUEUE SDLIB_CreateMessageQueue(INT MaxMessages, UINT MaxMessageLength)
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
PSDMESSAGE_QUEUE _CreateMessageQueue(INT MaxMessages, UINT MaxMessageLength)
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
  @prototype: SDIO_STATUS SDLIB_PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, UINT MessageLength)
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
SDIO_STATUS _PostMessage(PSDMESSAGE_QUEUE pQueue, PVOID pMessage, UINT MessageLength)
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
  @prototype: SDIO_STATUS SDLIB_GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, UINT *pBufferLength) 
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
SDIO_STATUS _GetMessage(PSDMESSAGE_QUEUE pQueue, PVOID pData, UINT *pBufferLength) 
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


