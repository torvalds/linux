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
@file: sdio_hcd.c

@abstract: Tokyo Electron PCI Ellen SDIO Host Controller Driver

#notes: OS independent code
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#include "sdio_pciellen_hcd.h"

#define CLOCK_ON  TRUE
#define CLOCK_OFF FALSE

void Dbg_DumpBuffer(PUCHAR pBuffer, INT Length);
SDIO_STATUS SetPowerLevel(PSDHCD_DEVICE pDeviceContext, BOOL On, SLOT_VOLTAGE_MASK Level); 


SD_CLOCK_TBL_ENTRY SDClockDivisorTable[SD_CLOCK_MAX_ENTRIES] =
{   /* clock rate divisor, divisor setting */
    {1, 0x0000},
    {2, 0x0100},
    {4, 0x0200},
    {8, 0x0400},
    {16,0x0800},
    {32,0x1000},
    {64,0x2000},
    {128,0x4000},
    {256,0x8000}, 
};


#define WAIT_REGISTER32_CHANGE(pDevice, pStatus, reg,mask,cmp,timout) \
    {\
        if (!WaitRegisterBitsChange((pDevice),    \
                                    (pStatus),    \
                                    (reg),        \
                                    (mask),       \
                                    (cmp),        \
                                    (timout))) {  \
           DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - Reg Change Timeout : 0x%X src:%s, line:%d \n",\
           (reg),__FILE__, __LINE__));        \
        }                                     \
    }
 
#define WAIT_FOR_DAT_CMD_DAT_READY(pDevice, pStatus) \
        WAIT_REGISTER32_CHANGE(pDevice,            \
                             pStatus,            \
                             HOST_REG_PRESENT_STATE,(HOST_REG_PRESENT_STATE_BUFFER_COMMAND_INHIBIT_DAT | \
                             HOST_REG_PRESENT_STATE_BUFFER_COMMAND_INHIBIT_CMD), \
                             0, 30000)
                              
   
static INLINE BOOL WaitRegisterBitsChange(PSDHCD_DEVICE pDevice, 
                                         SDIO_STATUS   *pStatus,
                                         UINT32         Reg, 
                                         UINT32         Mask, 
                                         UINT32         CompareMask, 
                                         UINT32         Count)
      {
    while (Count) {
                
        if ((READ_HOST_REG32(pDevice, Reg) & Mask) == CompareMask) {
            break;
        }
                
        Count--;    
    }    
    
    if (0 == Count) {
        if (pStatus != NULL) {
            *pStatus = SDIO_STATUS_ERROR;
        }
        return FALSE;  
    }
    
    if (pStatus != NULL) {
        *pStatus = SDIO_STATUS_SUCCESS;
    }
    
    return TRUE;
}


/* reset command data line state machines - xx*/
void ResetCmdDatLine(PSDHCD_DEVICE pDevice)
{
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen Issuing CMD DAT Reset \n"));  
        // issue reset
    WRITE_HOST_REG32(pDevice, HOST_REG_SW_RESET, 
            (HOST_REG_SW_RST_CMD_LINE | HOST_REG_SW_RST_DAT_LINE));
        // wait for bits to clear
    WAIT_REGISTER32_CHANGE(pDevice, NULL,
                         HOST_REG_SW_RESET,
                         HOST_REG_SW_RST_CMD_LINE | HOST_REG_SW_RST_DAT_LINE,
                         0, 
                         30000);        
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen CMD DAT Reset Done \n"));  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GetResponseData - get the response data 
  Input:    pDevice - device context
            pReq - the request
  Output: 
  Return: returns status
  Notes: This function returns SDIO_STATUS_SUCCESS for SD mode.  
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS GetResponseData(PSDHCD_DEVICE pDevice, PSDREQUEST pReq)
{
    UINT    dwordCount;
    UINT    byteCount;
    UINT32  readBuffer[4];
    UINT    ii;
    
    if (GET_SDREQ_RESP_TYPE(pReq->Flags) == SDREQ_FLAGS_NO_RESP) {
        return SDIO_STATUS_SUCCESS;    
    }
    
       
    byteCount = SD_DEFAULT_RESPONSE_BYTES;        
    if (GET_SDREQ_RESP_TYPE(pReq->Flags) == SDREQ_FLAGS_RESP_R2) {
        byteCount = SD_R2_RESPONSE_BYTES;         
    } 
    dwordCount = (byteCount + 3) / 4;      

    /* move data into read buffer */
    for (ii = 0; ii < dwordCount; ii++) {
        readBuffer[ii] = READ_HOST_REG32(pDevice, HOST_REG_RESPONSE+(ii*4));
    }

    /* handle normal SD/MMC responses */        
  
    /* the standard host strips the CRC for all responses and puts them in 
     * a nice linear order */
    memcpy(&pReq->Response[1],readBuffer,byteCount);
   
    if (DBG_GET_DEBUG_LEVEL() >= PXA_TRACE_REQUESTS) { 
        if (GET_SDREQ_RESP_TYPE(pReq->Flags) == SDREQ_FLAGS_RESP_R2) {
            byteCount = 17; 
        }
        SDLIB_PrintBuffer(pReq->Response,byteCount,"SDIO PCI Ellen - Response Dump");
    }
    
    return SDIO_STATUS_SUCCESS;  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  DumpCurrentRequestInfo - debug dump  
  Input:    pDevice - device context
  Output: 
  Return: 
  Notes: This function debug prints the current request  
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void DumpCurrentRequestInfo(PSDHCD_DEVICE pDevice)
{
    if (pDevice->Hcd.pCurrentRequest != NULL) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - Current Request Command:%d, ARG:0x%8.8X\n",
                  pDevice->Hcd.pCurrentRequest->Command, pDevice->Hcd.pCurrentRequest->Argument));
        if (IS_SDREQ_DATA_TRANS(pDevice->Hcd.pCurrentRequest->Flags)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - Data %s, Blocks: %d, BlockLen:%d Remaining: %d \n",
                      IS_SDREQ_WRITE_DATA(pDevice->Hcd.pCurrentRequest->Flags) ? "WRITE":"READ",
                      pDevice->Hcd.pCurrentRequest->BlockCount,
                      pDevice->Hcd.pCurrentRequest->BlockLen,
                      pDevice->Hcd.pCurrentRequest->DataRemaining));
        }
    }
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  TranslateSDError - check for an SD error 
  Input:    pDevice - device context
            Status -  error interrupt status register value
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS TranslateSDError(PSDHCD_DEVICE pDevice, UINT16 Status)
{
    if (Status & HOST_REG_ERROR_INT_STATUS_CRCERR) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - RESP CRC ERROR \n"));
        return SDIO_STATUS_BUS_RESP_CRC_ERR;
    } else if (Status & HOST_REG_ERROR_INT_STATUS_DATATIMEOUTERR) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - DATA TIMEOUT ERROR \n"));
        return SDIO_STATUS_BUS_READ_TIMEOUT;
    } else if (Status & HOST_REG_ERROR_INT_STATUS_DATACRCERR) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - READDATA CRC ERROR \n"));
        DumpCurrentRequestInfo(pDevice);
        return SDIO_STATUS_BUS_READ_CRC_ERR;
    } else if (Status & HOST_REG_ERROR_INT_STATUS_CMDTIMEOUTERR) {
        if (pDevice->CardInserted) {
                /* hide error if we are polling an empty slot */
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - RESPONSE TIMEOUT \n"));
        }
        return SDIO_STATUS_BUS_RESP_TIMEOUT;
    }
    DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen - untranslated error 0x%X\n", (UINT)Status));
    
    return SDIO_STATUS_DEVICE_ERROR;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  ClockStartStop - SD clock control
  Input:  pDevice - device object
          On - turn on or off (TRUE/FALSE)
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void ClockStartStop(PSDHCD_DEVICE pDevice, BOOL On) 
{
    /* beware, an unprotected read-modify-write */
    UINT16 state;

    DBG_PRINT(PXA_TRACE_CLOCK, ("SDIO PCI Ellen - ClockStartStop, %d\n", (UINT)On));
    
    state = READ_HOST_REG16(pDevice, HOST_REG_CLOCK_CONTROL);

    if (On) {
        state |= HOST_REG_CLOCK_CONTROL_SD_ENABLE;
        WRITE_HOST_REG16(pDevice, HOST_REG_CLOCK_CONTROL, state);
    } else {
        state &= ~HOST_REG_CLOCK_CONTROL_SD_ENABLE;
        WRITE_HOST_REG16(pDevice, HOST_REG_CLOCK_CONTROL, state);
    }  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SetBusMode - Set Bus mode
  Input:  pDevice - device object
          pMode - mode
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void SetBusMode(PSDHCD_DEVICE pDevice, PSDCONFIG_BUS_MODE_DATA pMode) 
{
    int ii;
    int clockIndex;
    UINT16 state;
    UINT32 rate;
    
    DBG_PRINT(PXA_TRACE_CONFIG , ("SDIO PCI Ellen - SetMode\n"));
    
        /* set clock index to the end, the table is sorted this way */
    clockIndex = SD_CLOCK_MAX_ENTRIES - 1;
    pMode->ActualClockRate = (pDevice->BaseClock) / SDClockDivisorTable[clockIndex].ClockRateDivisor;
    for (ii = 0; ii < SD_CLOCK_MAX_ENTRIES; ii++) {
        rate = pDevice->BaseClock / SDClockDivisorTable[ii].ClockRateDivisor;
        if (pMode->ClockRate >= rate) {
            pMode->ActualClockRate = rate;
            clockIndex = ii;
            break; 
        }   
    }
                                        
    switch (SDCONFIG_GET_BUSWIDTH(pMode->BusModeFlags)) {
        case SDCONFIG_BUS_WIDTH_1_BIT:
            WRITE_HOST_REG8(pDevice, HOST_REG_CONTROL, HOST_REG_CONTROL_1BIT_WIDTH);
            //WRITE_HOST_REG16(pDevice, HOST_REG_CONTROL, HOST_REG_CONTROL_1BIT_WIDTH);        
            break;        
        case SDCONFIG_BUS_WIDTH_4_BIT:
            WRITE_HOST_REG8(pDevice, HOST_REG_CONTROL, HOST_REG_CONTROL_4BIT_WIDTH);
            //WRITE_HOST_REG16(pDevice, HOST_REG_CONTROL, HOST_REG_CONTROL_4BIT_WIDTH);
            break;
        default:
            break;
    }
   
        /* set the clock divisor, unprotected read modify write */
    state = SDClockDivisorTable[clockIndex].RegisterValue | HOST_REG_CLOCK_CONTROL_CLOCK_ENABLE;
    WRITE_HOST_REG16(pDevice, HOST_REG_CLOCK_CONTROL, state);
    
        /* wait for stable */
    while(!(READ_HOST_REG16(pDevice, HOST_REG_CLOCK_CONTROL) & HOST_REG_CLOCK_CONTROL_CLOCK_STABLE)) {
      ;
    }
    WRITE_HOST_REG16(pDevice, HOST_REG_CLOCK_CONTROL, state | HOST_REG_CLOCK_CONTROL_SD_ENABLE);
    
    state = READ_HOST_REG16(pDevice, HOST_REG_CLOCK_CONTROL);
    DBG_PRINT(PXA_TRACE_CONFIG , ("SDIO PCI Ellen - Clock: %d Khz, ClockRate %d (%d) state:0x%X\n", 
                                   pMode->ActualClockRate, pMode->ClockRate, clockIndex, (UINT)state));
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdTransferTxData - data transmit transfer
  Input:  pDevice - device object
          pReq    - transfer request
  Output: 
  Return: 
  Notes: writes request data
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
BOOL HcdTransferTxData(PSDHCD_DEVICE pDevice, PSDREQUEST pReq)
{
    INT     dataCopy;
    PUINT8  pBuf;
    
    dataCopy = min(pReq->DataRemaining, (UINT)pReq->BlockLen);   
    pBuf = (PUINT8)pReq->pHcdContext;
   
    /* update remaining count */
    pReq->DataRemaining -= dataCopy;
    /* set the block data */
    while(dataCopy) { 
        UINT32 outData = 0;
        UINT   count = 0;
        if (dataCopy > 4) {
            outData = ((UINT32)(*(pBuf+0))) | 
                      (((UINT32)(*(pBuf+1))) << 8) | 
                      (((UINT32)(*(pBuf+2))) << 16) | 
                      (((UINT32)(*(pBuf+3))) << 24);
            WRITE_HOST_REG32(pDevice, HOST_REG_BUFFER_DATA_PORT, outData);
            dataCopy -= 4; 
            pBuf += 4;
        } else {
            for(count = 0; (dataCopy > 0) && (count < 4); count++) {
               outData |= (*pBuf) << (count*8); 
               pBuf++;
               dataCopy--;
            }
            WRITE_HOST_REG32(pDevice, HOST_REG_BUFFER_DATA_PORT, outData);
        }
    }
    
        /* update pointer position */
    pReq->pHcdContext = (PVOID)pBuf;
    if (pReq->DataRemaining) {
        return FALSE; 
    }
    return TRUE;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdTransferRxData - data receive transfer
  Input:  pDevice - device object
          pReq    - transfer request
  Output: 
  Return: 
  Notes: reads request data
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void HcdTransferRxData(PSDHCD_DEVICE pDevice, PSDREQUEST pReq)
{
    INT     dataCopy;
    PUINT8  pBuf;
    
    dataCopy = min(pReq->DataRemaining, (UINT)pReq->BlockLen);   
    pBuf = (PUINT8)pReq->pHcdContext;
   
    /* update remaining count */
    pReq->DataRemaining -= dataCopy;
    /* set the block data */
    while(dataCopy) { 
        UINT32 inData;
        UINT   count = 0;
        inData = READ_HOST_REG32(pDevice, HOST_REG_BUFFER_DATA_PORT);
        for(count = 0; (dataCopy > 0) && (count < 4); count++) {
            *pBuf = (inData >> (count*8)) & 0xFF;
            dataCopy--;
            pBuf++;
        }
    }
    
        /* update pointer position */
    pReq->pHcdContext = (PVOID)pBuf;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdRequest - SD request handler
  Input:  pHcd - HCD object
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS HcdRequest(PSDHCD pHcd) 
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDHCD_DEVICE pDevice = (PSDHCD_DEVICE)pHcd->pContext;
    UINT16                temp;
    UINT16                ints;
    PSDREQUEST            pReq;
    
    pReq = GET_CURRENT_REQUEST(pHcd);
    DBG_ASSERT(pReq != NULL);
        /* make sure clock is off */
    ClockStartStop(pDevice, CLOCK_OFF);
    
    if(pDevice->ShuttingDown) {
        DBG_PRINT(PXA_TRACE_REQUESTS, ("SDIO PCI Ellen HcdRequest returning canceled\n"));
        return SDIO_STATUS_CANCELED;
    }
    /* make sure error ints are disabled */
    WRITE_HOST_REG16(pDevice, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                (UINT16)(~HOST_REG_ERROR_INT_STATUS_ALL_ERR));

    switch (GET_SDREQ_RESP_TYPE(pReq->Flags)) {    
        default:
        case SDREQ_FLAGS_NO_RESP:
            temp = 0x00;
            break;
        case SDREQ_FLAGS_RESP_R2:
            temp = 0x01 |
                    HOST_REG_COMMAND_REGISTER_CRC_CHECK_ENABLE;
            break;
        case SDREQ_FLAGS_RESP_R3:
        case SDREQ_FLAGS_RESP_SDIO_R4:
            temp = 0x02;
            break;
        case SDREQ_FLAGS_RESP_R1:
        case SDREQ_FLAGS_RESP_SDIO_R5:
        case SDREQ_FLAGS_RESP_R6:
            temp = 0x02 | HOST_REG_COMMAND_REGISTER_CRC_CHECK_ENABLE 
                        | HOST_REG_COMMAND_REGISTER_CMD_INDEX_CHECK_ENABLE;
            break;
        case SDREQ_FLAGS_RESP_R1B:   
            temp = 0x03 | HOST_REG_COMMAND_REGISTER_CRC_CHECK_ENABLE
                        | HOST_REG_COMMAND_REGISTER_CMD_INDEX_CHECK_ENABLE;
            break;
    }   

        /* start the clock */
    ClockStartStop(pDevice, CLOCK_ON);
    /* mask the remove while we are spinning on the CMD ready bits */
    MaskIrq(pDevice, HOST_REG_INT_STATUS_ALLOW_INSERT_REMOVE_ONLY);
    WAIT_FOR_DAT_CMD_DAT_READY(pDevice, &status);

    if (!SDIO_SUCCESS(status)) {
        ResetCmdDatLine(pDevice); 
        goto processComplete;   
    }
        /* clear any error statuses */
    WRITE_HOST_REG16(pDevice, HOST_REG_ERROR_INT_STATUS, HOST_REG_ERROR_INT_STATUS_ALL_ERR);
    WRITE_HOST_REG16(pDevice, HOST_REG_NORMAL_INT_STATUS, HOST_REG_NORMAL_INT_STATUS_ALL_ERR);

    if (pReq->Flags & SDREQ_FLAGS_DATA_TRANS){
        /* set the block size register */
        WRITE_HOST_REG16(pDevice, HOST_REG_BLOCK_SIZE, pReq->BlockLen);
        /* set block count register */
        WRITE_HOST_REG16(pDevice, HOST_REG_BLOCK_COUNT, pReq->BlockCount);
        pReq->DataRemaining = pReq->BlockLen * pReq->BlockCount;
        DBG_PRINT(PXA_TRACE_DATA, ("SDIO PCI Ellen %s Data Transfer, Blocks:%d, BlockLen:%d, Total:%d \n",
                                   IS_SDREQ_WRITE_DATA(pReq->Flags) ? "TX":"RX",
                                   pReq->BlockCount, pReq->BlockLen, pReq->DataRemaining));
            /* use the context to hold where we are in the buffer */
        pReq->pHcdContext = pReq->pDataBuffer;
        temp |= HOST_REG_COMMAND_REGISTER_DATA_PRESENT; 
    }  
   
    /* set the argument register */
    WRITE_HOST_REG32(pDevice, HOST_REG_ARGUMENT, pReq->Argument);
    /* set transfer mode register */
    WRITE_HOST_REG16(pDevice, HOST_REG_TRANSFER_MODE, 
            ((pReq->BlockCount > 1) ? HOST_REG_TRANSFER_MODE_MULTI_BLOCK:0) |
            ((pReq->BlockCount > 1) ? HOST_REG_TRANSFER_MODE_BLOCKCOUNT_ENABLE:0) |
            ((pReq->Flags & SDREQ_FLAGS_AUTO_CMD12) ? HOST_REG_TRANSFER_MODE_AUTOCMD12 : 0) |
            ((IS_SDREQ_WRITE_DATA(pReq->Flags))?0 : HOST_REG_TRANSFER_MODE_READ));
   
    /* block cmd timeout errors */
    WRITE_HOST_REG16(pDevice, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                HOST_REG_ERROR_INT_STATUS_ALL_ERR & ~HOST_REG_ERROR_INT_STATUS_CMDTIMEOUTERR);

    /* set command register, make sure it is clear to write */
    temp |= (pReq->Command << HOST_REG_COMMAND_REGISTER_CMD_SHIFT);
    DBG_PRINT(PXA_TRACE_REQUESTS, ("SDIO PCI Ellen CMDDAT:0x%X (RespType:%d, Command:0x%X , Arg:0x%X) \n",
              temp, GET_SDREQ_RESP_TYPE(pReq->Flags), pReq->Command, pReq->Argument));
    
    
    if (SDHCD_GET_OPER_CLOCK(pHcd) < pDevice->ClockSpinLimit) {
            /* clock rate is very low, need to use interrupts here */
            /* enable error interrupts */
        WRITE_HOST_REG16(pDevice, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                HOST_REG_ERROR_INT_STATUS_ALL_ERR);
        UnmaskIrq(pDevice, HOST_REG_INT_STATUS_CMD_COMPLETE_ENABLE);
        WRITE_HOST_REG16(pDevice, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                            HOST_REG_ERROR_INT_STATUS_ALL_ERR);
                            
        if (pReq->Flags & SDREQ_FLAGS_DATA_TRANS) {
             if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
                 TRACE_SIGNAL_DATA_WRITE(pDevice, TRUE); 
             } else {
                 TRACE_SIGNAL_DATA_READ(pDevice, TRUE);
             }
        }
                
        WRITE_HOST_REG16(pDevice, HOST_REG_COMMAND_REGISTER, temp);
        
        status = SDIO_STATUS_PENDING;
        if (pReq->Flags & SDREQ_FLAGS_DATA_TRANS) {
            DBG_PRINT(PXA_TRACE_REQUESTS, ("SDIO PCI Ellen using interrupt for command done.*** with data. (clock:%d, ref:%d)\n",
                SDHCD_GET_OPER_CLOCK(pHcd),pDevice->ClockSpinLimit));
        } else {
            DBG_PRINT(PXA_TRACE_REQUESTS, ("SDIO PCI Ellen using interrupt for command done. (clock:%d, ref:%d) \n",
                SDHCD_GET_OPER_CLOCK(pHcd),pDevice->ClockSpinLimit));
        }
        return status;
    } else {
        if (pReq->Flags & SDREQ_FLAGS_DATA_TRANS) {
             if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
                 TRACE_SIGNAL_DATA_WRITE(pDevice, TRUE); 
             } else {
                 TRACE_SIGNAL_DATA_READ(pDevice, TRUE);
             }
        }
        WRITE_HOST_REG16(pDevice, HOST_REG_COMMAND_REGISTER, temp);
        if (pReq->Flags & SDREQ_FLAGS_DATA_TRANS) {
            WAIT_REGISTER32_CHANGE(pDevice,            
                                   &status,            
                                   HOST_REG_PRESENT_STATE,
                                   HOST_REG_PRESENT_STATE_BUFFER_COMMAND_INHIBIT_CMD,
                                   0, 30000);
        } else  {
            WAIT_FOR_DAT_CMD_DAT_READY(pDevice, &status);
        }
        
        if (!SDIO_SUCCESS(status)) {
            ResetCmdDatLine(pDevice); 
            goto processComplete;   
        }
    }

    /* check for errors */
    temp = READ_HOST_REG16(pDevice, HOST_REG_ERROR_INT_STATUS);
    ints = READ_HOST_REG16(pDevice, HOST_REG_NORMAL_INT_STATUS);
    if (ints & HOST_REG_NORMAL_INT_STATUS_TRANSFER_COMPLETE) {
        DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdRequest clearing possible data timeout errors: 0x%X, ints: 0x%X \n",
                                      temp, ints));
        temp &= ~HOST_REG_ERROR_INT_STATUS_DATATIMEOUTERR;
    }
    WRITE_HOST_REG16(pDevice, HOST_REG_NORMAL_INT_STATUS,
                                HOST_REG_NORMAL_INT_STATUS_CMD_COMPLETE);

    UnmaskIrq(pDevice, HOST_REG_INT_STATUS_ALLOW_INSERT_REMOVE_ONLY);

    if (temp != 0) {
        if (temp & HOST_REG_ERROR_INT_STATUS_CMDTIMEOUTERR) {
            /* toggle timeout gpio */
            TRACE_SIGNAL_DATA_TIMEOUT(pDevice, TRUE);
            TRACE_SIGNAL_DATA_TIMEOUT(pDevice, FALSE);
        }
        status = TranslateSDError(pDevice, temp);
        /* clear any existing errors - non-synchronized clear */
        WRITE_HOST_REG16(pDevice, HOST_REG_ERROR_INT_STATUS, HOST_REG_ERROR_INT_STATUS_ALL_ERR);        
            /* reset statemachine, just in case */
        ResetCmdDatLine(pDevice);            
    } else if (pDevice->Cancel) {
        status = SDIO_STATUS_CANCELED;   
    } else {
            /* get the response data for the command */
        status = GetResponseData(pDevice, pReq);
    }
    

        /* check for data */
    if (SDIO_SUCCESS(status) && (pReq->Flags & SDREQ_FLAGS_DATA_TRANS)){
       
            /* check with the bus driver if it is okay to continue with data */
        status = SDIO_CheckResponse(pHcd, pReq, SDHCD_CHECK_DATA_TRANS_OK);
        
        if (SDIO_SUCCESS(status)) {
            /* re-enable the cmd timeout error */
            WRITE_HOST_REG16(pDevice, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                    HOST_REG_ERROR_INT_STATUS_ALL_ERR);
            if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
                /* see if the buffer is ready, it should be */
                ints = READ_HOST_REG16(pDevice, HOST_REG_NORMAL_INT_STATUS);
                if (ints & HOST_REG_INT_STATUS_BUFFER_WRITE_RDY_ENABLE) {
                    WRITE_HOST_REG16(pDevice, 
                                    HOST_REG_NORMAL_INT_STATUS, 
                                    HOST_REG_NORMAL_INT_STATUS_BUFFER_WRITE_RDY);
                    
                    /* send the initial buffer */
                    /* transfer data */
                    HcdTransferTxData(pDevice, pReq);
                }
                    /* expecting interrupt */  
                UnmaskIrq(pDevice, HOST_REG_INT_STATUS_TRANSFER_COMPLETE_ENABLE 
                                   | HOST_REG_INT_STATUS_BUFFER_WRITE_RDY_ENABLE);    
            } else {
                UnmaskIrq(pDevice, HOST_REG_INT_STATUS_TRANSFER_COMPLETE_ENABLE 
                                   | HOST_REG_INT_STATUS_BUFFER_READ_RDY_ENABLE);    
            }
            DBG_PRINT(PXA_TRACE_DATA, ("SDIO PCI Ellen Pending %s transfer \n",
                                       IS_SDREQ_WRITE_DATA(pReq->Flags) ? "TX":"RX"));
                                       
                /* return pending */  
            status = SDIO_STATUS_PENDING; 
        } else {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen : Response for Data transfer error :%d n",status));
            ResetCmdDatLine(pDevice);  	
        }
    }

processComplete: 
   
    if (status != SDIO_STATUS_PENDING) {  
        if (!pDevice->KeepClockOn) {   
            ClockStartStop(pDevice, CLOCK_OFF);
        }
        pReq->Status = status;
        
        if (IS_SDREQ_FORCE_DEFERRED_COMPLETE(pReq->Flags)) {
            DBG_PRINT(PXA_TRACE_REQUESTS, ("SDIO PCI Ellen deferring completion to work item \n"));
                /* the HCD must do the indication in a separate context and return status pending */
            QueueEventResponse(pDevice, WORK_ITEM_IO_COMPLETE); 
            return SDIO_STATUS_PENDING;
        } else {        
                /* complete the request */
            DBG_PRINT(PXA_TRACE_REQUESTS, ("SDIO PCI Ellen Command Done, status:%d \n", status));
        }
        pDevice->Cancel = FALSE;  
    }     

    return status;
} 

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdConfig - HCD configuration handler
  Input:  pHcd - HCD object
          pConfig - configuration setting
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS HcdConfig(PSDHCD pHcd, PSDCONFIG pConfig) 
{
    PSDHCD_DEVICE pDevice = (PSDHCD_DEVICE)pHcd->pContext; 
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT16      command;
    UINT32 temp;
    
    if(pDevice->ShuttingDown) {
        DBG_PRINT(PXA_TRACE_REQUESTS, ("SDIO PCI Ellen HcdConfig returning canceled\n"));
        return SDIO_STATUS_CANCELED;
    }

    command = GET_SDCONFIG_CMD(pConfig);
        
    switch (command){
        case SDCONFIG_GET_WP:
            /* get write protect */
            temp = READ_HOST_REG32(pDevice, HOST_REG_PRESENT_STATE);
            /* if write enabled, set WP value to zero */
            *((SDCONFIG_WP_VALUE *)pConfig->pData) = 
                    (temp & HOST_REG_PRESENT_STATE_WRITE_ENABLED )? 0 : 1;
            break;
        case SDCONFIG_SEND_INIT_CLOCKS:
            ClockStartStop(pDevice,CLOCK_ON);
                /* should be at least 80 clocks at our lowest clock setting */
            status = OSSleep(100);
            ClockStartStop(pDevice,CLOCK_OFF);          
            break;
        case SDCONFIG_SDIO_INT_CTRL:
            if (GET_SDCONFIG_CMD_DATA(PSDCONFIG_SDIO_INT_CTRL_DATA,pConfig)->SlotIRQEnable) {
                {
                    SDIO_IRQ_MODE_FLAGS irqModeFlags;
                    UINT8               blockGapControl;
                    
                    irqModeFlags = GET_SDCONFIG_CMD_DATA(PSDCONFIG_SDIO_INT_CTRL_DATA,pConfig)->IRQDetectMode;
                    if (irqModeFlags & IRQ_DETECT_4_BIT) {
                        DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen: 4 Bit IRQ mode \r\n")); 
                            /* in 4 bit mode, the clock needs to be left on */
                        pDevice->KeepClockOn = TRUE;
                        blockGapControl = READ_HOST_REG8(pDevice,HOST_REG_BLOCK_GAP);
                        if (irqModeFlags & IRQ_DETECT_MULTI_BLK) {
                            blockGapControl |= HOST_REG_INT_DETECT_AT_BLOCK_GAP;
                            DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen: 4 Bit Multi-block IRQ detection enabled \r\n")); 
                        } else {
                                // no interrupts between blocks
                            blockGapControl &= ~HOST_REG_INT_DETECT_AT_BLOCK_GAP;   
                        }  
                        WRITE_HOST_REG8(pDevice,HOST_REG_BLOCK_GAP,blockGapControl);                         
                    } else {
                            /* in 1 bit mode, the clock can be left off */
                        pDevice->KeepClockOn = FALSE;   
                    }                   
                }
                    /* enable detection */
                EnableDisableSDIOIRQ(pDevice,TRUE,FALSE); 
            } else {
                pDevice->KeepClockOn = FALSE; 
                EnableDisableSDIOIRQ(pDevice,FALSE,FALSE);
            }
            break;
        case SDCONFIG_SDIO_REARM_INT:
                /* re-enable IRQ detection */
            EnableDisableSDIOIRQ(pDevice,TRUE,FALSE);
            break;
        case SDCONFIG_BUS_MODE_CTRL:
            SetBusMode(pDevice, (PSDCONFIG_BUS_MODE_DATA)(pConfig->pData));
            break;
        case SDCONFIG_POWER_CTRL:
            DBG_PRINT(PXA_TRACE_CONFIG, ("SDIO PCI Ellen PwrControl: En:%d, VCC:0x%X \n",
                      GET_SDCONFIG_CMD_DATA(PSDCONFIG_POWER_CTRL_DATA,pConfig)->SlotPowerEnable,
                      GET_SDCONFIG_CMD_DATA(PSDCONFIG_POWER_CTRL_DATA,pConfig)->SlotPowerVoltageMask));
            status = SetPowerLevel(pDevice, 
                     GET_SDCONFIG_CMD_DATA(PSDCONFIG_POWER_CTRL_DATA,pConfig)->SlotPowerEnable,
                     GET_SDCONFIG_CMD_DATA(PSDCONFIG_POWER_CTRL_DATA,pConfig)->SlotPowerVoltageMask);
            break;
        default:
            /* invalid request */
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen Local HCD: HcdConfig - bad command: 0x%X\n",
                                    command));
            status = SDIO_STATUS_INVALID_PARAMETER;
    }
    
    return status;
} 


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SetPowerLevel - Set power level of board
  Input:  pDeviceContext - device context
          On - if true turns power on, else off
          Level - SLOT_VOLTAGE_MASK level
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SetPowerLevel(PSDHCD_DEVICE pDeviceContext, BOOL On, SLOT_VOLTAGE_MASK Level) 
{
    UINT8 out;
    UINT32 capCurrent;
    
    capCurrent = READ_HOST_REG32(pDeviceContext, HOST_REG_MAX_CURRENT_CAPABILITIES);
       
    switch (Level) {
      case SLOT_POWER_3_3V:
        out = HOST_REG_POWER_CONTROL_VOLT_3_3;
            /* extract */
        capCurrent = (capCurrent & HOST_REG_MAX_CURRENT_CAPABILITIES_3_3_MASK) >>
                        HOST_REG_MAX_CURRENT_CAPABILITIES_3_3_SHIFT;
        break;
      case SLOT_POWER_3_0V:
        out = HOST_REG_POWER_CONTROL_VOLT_3_0;            
            /* extract */
        capCurrent = (capCurrent & HOST_REG_MAX_CURRENT_CAPABILITIES_3_0_MASK) >>
                        HOST_REG_MAX_CURRENT_CAPABILITIES_3_0_SHIFT;
        break;
      case SLOT_POWER_1_8V:
        out = HOST_REG_POWER_CONTROL_VOLT_1_8;            
            /* extract */
        capCurrent = (capCurrent & HOST_REG_MAX_CURRENT_CAPABILITIES_1_8_MASK) >>
                        HOST_REG_MAX_CURRENT_CAPABILITIES_1_8_SHIFT;       
        break;
      default:
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen SetPowerLevel - illegal power level %d\n",
                                (UINT)Level));
        return SDIO_STATUS_INVALID_PARAMETER;                        
    }
    
    if (capCurrent != 0) {
            /* convert to mA and set max current */
        pDeviceContext->Hcd.MaxSlotCurrent = capCurrent * HOST_REG_MAX_CURRENT_CAPABILITIES_SCALER;
    } else {
        DBG_PRINT(SDDBG_WARN, ("SDIO PCI Ellen No Current Caps value for VMask:0x%X, using 200mA \n",
                  Level));  
            /* set a value */          
        pDeviceContext->Hcd.MaxSlotCurrent = 200; 
    }
        
    if (On) {
        out |= HOST_REG_POWER_CONTROL_ON;
    }
        
    WRITE_HOST_REG8(pDeviceContext, HOST_REG_POWER_CONTROL, out);
    return SDIO_STATUS_SUCCESS;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SetPowerOn - Set power on or off for card
  Input:  pDeviceContext - device context
          On - if true turns power on, else off
  Output: 
  Return: 
  Notes: leavse the level alone
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void SetPowerOn(PSDHCD_DEVICE pDeviceContext, BOOL On) 
{
    /* non-synchronized read modify write */
    UINT8 out = READ_HOST_REG8(pDeviceContext, HOST_REG_POWER_CONTROL);
    if (On) {
        out |= HOST_REG_POWER_CONTROL_ON;
    } else {
        out &= ~HOST_REG_POWER_CONTROL_ON;
    }
    WRITE_HOST_REG8(pDeviceContext, HOST_REG_POWER_CONTROL, out);
    return;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdInitialize - Initialize MMC controller
  Input:  pDeviceContext - device context
  Output: 
  Return: 
  Notes: I/O resources must be mapped before calling this function
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS HcdInitialize(PSDHCD_DEVICE pDeviceContext) 
{
    UINT32 caps;
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT32 clockValue;
    DBG_PRINT(SDDBG_TRACE, ("+SDIO PCI Ellen HcdInitialize\n"));
    
        /* reset the device */
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen HcdInitialize, resetting\n"));
    WRITE_HOST_REG8(pDeviceContext, HOST_REG_SW_RESET, HOST_REG_SW_RESET_ALL);
        /* wait for done */
    while(READ_HOST_REG8(pDeviceContext, HOST_REG_SW_RESET) &  HOST_REG_SW_RESET_ALL)
        ;
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen HcdInitialize, reset\n"));
            
        /* turn off clock */
    ClockStartStop(pDeviceContext, CLOCK_OFF);
        /* display version info */
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen HcdInitialize: Spec verison: %s, Vendor version: %d\n",
       (((READ_HOST_REG16(pDeviceContext, HOST_REG_VERSION) & HOST_REG_VERSION_SPEC_VERSION_MASK )== 0)?
        "SD Host Spec. 1.0": "SD Host Spec. **UNKNOWN**"),
        (READ_HOST_REG16(pDeviceContext, HOST_REG_VERSION) >> HOST_REG_VERSION_VENDOR_VERSION_SHIFT) &&
        HOST_REG_VERSION_VENDOR_VERSION_MASK));
        
        /* get capabilities */
    caps = READ_HOST_REG32(pDeviceContext, HOST_REG_CAPABILITIES);
    pDeviceContext->HighSpeed = (caps & HOST_REG_CAPABILITIES_HIGH_SPEED);
    switch((caps & HOST_REG_CAPABILITIES_MAX_BLOCK_LEN_MASK) >> HOST_REG_CAPABILITIES_MAX_BLOCK_LEN_SHIFT) {
        case 0x00:
            pDeviceContext->Hcd.MaxBytesPerBlock = 512;
            break;
        case 0x01:
            pDeviceContext->Hcd.MaxBytesPerBlock = 1024;
            break;
        case 0x02:
            pDeviceContext->Hcd.MaxBytesPerBlock = 2048;
            break;
        case 0x03:
            pDeviceContext->Hcd.MaxBytesPerBlock = 512;
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen invalid buffer length\n"));
            status = SDIO_STATUS_DEVICE_ERROR;
            break;
    }
    
    clockValue = (caps & HOST_REG_CAPABILITIES_CLOCK_MASK) >> HOST_REG_CAPABILITIES_CLOCK_SHIFT;
    if (clockValue != 0) {
            /* convert to Hz */
        pDeviceContext->BaseClock = clockValue*1000*1000;   
    } else {
        DBG_PRINT(SDDBG_WARN, ("SDIO PCI Ellen base clock is zero! (caps:0x%X) \n",caps));
            /* fall through and see if a default was setup */   
    }                                               
    if (pDeviceContext->BaseClock == 0) {
         DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen invalid base clock setting\n"));
         status = SDIO_STATUS_DEVICE_ERROR;
         return status;
    }
            
    pDeviceContext->Hcd.MaxClockRate =  pDeviceContext->BaseClock;
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen Using clock %dHz, max. block %d, high speed %s\n",
                            pDeviceContext->BaseClock, pDeviceContext->Hcd.MaxBytesPerBlock, 
                            (pDeviceContext->HighSpeed)? "supported" : "not supported"));
    /* setup the supported voltages and max current */
    pDeviceContext->Hcd.SlotVoltageCaps = 0;
    /* max current is dynamically set based on the desired voltage, see SetPowerLevel() */
    pDeviceContext->Hcd.MaxSlotCurrent = 0;

    if (caps & HOST_REG_CAPABILITIES_VOLT_1_8) {
        pDeviceContext->Hcd.SlotVoltageCaps |= SLOT_POWER_1_8V;
        pDeviceContext->Hcd.SlotVoltagePreferred = SLOT_POWER_1_8V;
    } 
    if(caps & HOST_REG_CAPABILITIES_VOLT_3_0) {
        pDeviceContext->Hcd.SlotVoltageCaps |= SLOT_POWER_3_0V;
        pDeviceContext->Hcd.SlotVoltagePreferred = SLOT_POWER_3_0V;
    }
    if(caps & HOST_REG_CAPABILITIES_VOLT_3_3) {
        pDeviceContext->Hcd.SlotVoltageCaps |= SLOT_POWER_3_3V;
        pDeviceContext->Hcd.SlotVoltagePreferred = SLOT_POWER_3_3V;
    }
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen HcdInitialize: caps: 0x%X, SlotVoltageCaps: 0x%X, MaxSlotCurrent: 0x%X\n",
                        (UINT)caps, (UINT)pDeviceContext->Hcd.SlotVoltageCaps, (UINT)pDeviceContext->Hcd.MaxSlotCurrent));
              
        /* set the default timeout */
    WRITE_HOST_REG8(pDeviceContext, HOST_REG_TIMEOUT_CONTROL, pDeviceContext->TimeOut);

    /* clear any existing errors */
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_NORMAL_INT_STATUS, HOST_REG_NORMAL_INT_STATUS_ALL_ERR);
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_ERROR_INT_STATUS, HOST_REG_ERROR_INT_STATUS_ALL_ERR);
    /* enable error interrupts */
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_ERR_STATUS_ENABLE, HOST_REG_ERROR_INT_STATUS_ALL_ERR);
//??    WRITE_HOST_REG16(pDeviceContext, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
//??                HOST_REG_ERROR_INT_STATUS_ALL_ERR & ~HOST_REG_ERROR_INT_STATUS_CMDTIMEOUTERR);
    /* leave disabled for now */
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                (UINT16)~HOST_REG_ERROR_INT_STATUS_ALL_ERR);
    /* enble statuses */
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_INT_STATUS_ENABLE, HOST_REG_INT_STATUS_ALL);
    
    
    /* interrupts will get enabled by the caller after all of the OS dependent work is done */
    /*UnmaskIrq(pDeviceContext, HOST_REG_INT_STATUS_ALLOW_INSERT_REMOVE_ONLY);*/
    DBG_PRINT(SDDBG_TRACE, ("-SDIO PCI Ellen HcdInitialize\n"));
    return status;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdDeinitialize - deactivate controller
  Input:  pDeviceContext - context
  Output: 
  Return: 
  Notes:
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void HcdDeinitialize(PSDHCD_DEVICE pDeviceContext)
{
    DBG_PRINT(SDDBG_TRACE, ("+SDIO PCI Ellen HcdDeinitialize\n"));
    pDeviceContext->KeepClockOn = FALSE;
    MaskIrq(pDeviceContext, HOST_REG_INT_STATUS_ALL);
    pDeviceContext->ShuttingDown = TRUE;
    /* disable error interrupts */
    /* clear any existing errors */
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_ERROR_INT_STATUS, HOST_REG_ERROR_INT_STATUS_ALL_ERR);
    /* disable error interrupts */
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                                     (UINT16)~HOST_REG_ERROR_INT_STATUS_ALL_ERR);
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_ERR_STATUS_ENABLE, 
                                     (UINT16)~HOST_REG_ERROR_INT_STATUS_ALL_ERR);
    ClockStartStop(pDeviceContext, CLOCK_OFF);
    SetPowerOn(pDeviceContext, FALSE);
    DBG_PRINT(SDDBG_TRACE, ("-SDIO PCI Ellen HcdDeinitialize\n"));
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdSDInterrupt - process controller interrupt
  Input:  pDeviceContext - context
  Output: 
  Return: TRUE if interrupt was handled
  Notes:
               
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
BOOL HcdSDInterrupt(PSDHCD_DEVICE pDeviceContext) 
{
    UINT16      ints;
    UINT16      errors;
    UINT16 enables;
    UINT16 statenables;
    PSDREQUEST  pReq;
    SDIO_STATUS status = SDIO_STATUS_PENDING;
    
    DBG_PRINT(PXA_TRACE_MMC_INT, ("+SDIO PCI Ellen HcdSDInterrupt Int handler \n"));
    

    ints = READ_HOST_REG16(pDeviceContext, HOST_REG_NORMAL_INT_STATUS);
    errors = READ_HOST_REG16(pDeviceContext, HOST_REG_ERROR_INT_STATUS);
    
    if ((ints == 0) && (errors == 0)) {
        DBG_PRINT(SDDBG_ERROR, ("-SDIO PCI Ellen HcdSDInterrupt False Interrupt! \n"));
        return FALSE;   
    }
    enables = READ_HOST_REG16(pDeviceContext, HOST_REG_INT_SIGNAL_ENABLE);
    statenables = READ_HOST_REG16(pDeviceContext, HOST_REG_INT_STATUS_ENABLE);
    DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt, ints: 0x%X errors: 0x%x, sigenables: 0x%X, statenable: 0x%X\n", 
            (UINT)ints, (UINT)errors, (UINT)enables, (UINT)statenables));
                /* clear any error statuses */
    WRITE_HOST_REG16(pDeviceContext, HOST_REG_ERROR_INT_STATUS, errors);
    
    pReq = GET_CURRENT_REQUEST(&pDeviceContext->Hcd);
    
    if (ints & HOST_REG_NORMAL_INT_STATUS_TRANSFER_COMPLETE) {
        DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt clearing possible data timeout errors: 0x%X \n",
                                      errors));
        errors &= ~HOST_REG_ERROR_INT_STATUS_DATATIMEOUTERR;
    }
    /* handle the error cases first */
    if (errors != 0) {
        if (errors & HOST_REG_ERROR_INT_STATUS_VENDOR_MASK) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt vendor error 0x%X: \n", 
                        (UINT)((errors & HOST_REG_ERROR_INT_STATUS_VENDOR_MASK) >> 
                                HOST_REG_ERROR_INT_STATUS_VENDOR_SHIFT)));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_AUTOCMD12ERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt auto cmd12 error\n"));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_CURRENTLIMITERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt current limit error\n"));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_DATAENDBITERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt data end bit error\n"));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_DATACRCERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt data CRC error\n"));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_DATATIMEOUTERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt data timeout error\n"));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_CMDINDEXERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt CMD index error\n"));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_CMDENDBITERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt CMD end bit error\n")); 
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_CRCERR) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt CRC error\n"));
        }
        if (errors & HOST_REG_ERROR_INT_STATUS_CMDTIMEOUTERR) {
            /* toggle timeout gpio */
            TRACE_SIGNAL_DATA_TIMEOUT(pDeviceContext, TRUE);
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen HcdSDInterrupt CMD timeout error\n"));
            TRACE_SIGNAL_DATA_TIMEOUT(pDeviceContext, FALSE);
        }
        if (ints & HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE) {
              /* disable SDIO interrupt */
            EnableDisableSDIOIRQ(pDeviceContext,FALSE,TRUE);
        }
        /* process insert/remove even on error conditions */
        if (ints & 
            (HOST_REG_INT_STATUS_CARD_INSERT_ENABLE | HOST_REG_INT_STATUS_CARD_REMOVAL_ENABLE)){
            /* card was inserted or removed, clear interrupt */
            WRITE_HOST_REG16(pDeviceContext, 
                             HOST_REG_NORMAL_INT_STATUS, 
                             HOST_REG_INT_STATUS_CARD_INSERT_ENABLE | 
                             HOST_REG_INT_STATUS_CARD_REMOVAL_ENABLE);
            enables = MaskIrqFromIsr(pDeviceContext, HOST_REG_INT_STATUS_ALL);
            QueueEventResponse(pDeviceContext, WORK_ITEM_CARD_DETECT);
        } 
        
    } else {
        /* only look at ints that are enabled */
        ints &= enables;
        
        //DBG_PRINT(SDDBG_TRACE, ("SDIO PCI Ellen ints: 0x%X errors: 0x%x, sigenables: 0x%X, statenable: 0x%X\n", 
        //    (UINT)ints, (UINT)errors, (UINT)enables, (UINT)statenables));         
        if ((pDeviceContext->CardInserted) && 
            (ints & HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE)) {               
              /* SD card interrupt*/
              /* disable the interrupt, the user must clear the interrupt */
            EnableDisableSDIOIRQ(pDeviceContext,FALSE,TRUE);            
            QueueEventResponse(pDeviceContext, WORK_ITEM_SDIO_IRQ);
            /* continue looking for other interrupt causes */
        } else if (ints & HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE) {
              /* disable bogus interrupt */
            EnableDisableSDIOIRQ(pDeviceContext,FALSE,TRUE);
        }
        
        if (ints & 
            (HOST_REG_INT_STATUS_CARD_INSERT_ENABLE | HOST_REG_INT_STATUS_CARD_REMOVAL_ENABLE)){
            /* card was inserted or removed, clear interrupt */
            WRITE_HOST_REG16(pDeviceContext, 
                             HOST_REG_NORMAL_INT_STATUS, 
                             HOST_REG_INT_STATUS_CARD_INSERT_ENABLE | 
                             HOST_REG_INT_STATUS_CARD_REMOVAL_ENABLE);
            enables = MaskIrqFromIsr(pDeviceContext, HOST_REG_INT_STATUS_ALL);
            QueueEventResponse(pDeviceContext, WORK_ITEM_CARD_DETECT);
            return TRUE;
        } 
        
        if (pDeviceContext->CardInserted && (pReq != NULL)) {
            if (ints & HOST_REG_NORMAL_INT_STATUS_CMD_COMPLETE) {
                WRITE_HOST_REG16(pDeviceContext, HOST_REG_NORMAL_INT_STATUS,
                                HOST_REG_NORMAL_INT_STATUS_CMD_COMPLETE);
                MaskIrqFromIsr(pDeviceContext, HOST_REG_INT_STATUS_CMD_COMPLETE_ENABLE);
                    /* get the response data for the command */
                status = GetResponseData(pDeviceContext, pReq);
                DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt command complete, status: %d\n",status));

                if (SDIO_SUCCESS(status) && (pReq->Flags & SDREQ_FLAGS_DATA_TRANS)){
           
                        /* check with the bus driver if it is okay to continue with data */
                    status = SDIO_CheckResponse(&pDeviceContext->Hcd, pReq, SDHCD_CHECK_DATA_TRANS_OK);

                    /* re-enable the cmd timeout error */
                    WRITE_HOST_REG16(pDeviceContext, HOST_REG_INT_ERR_SIGNAL_ENABLE, 
                            HOST_REG_ERROR_INT_STATUS_ALL_ERR);
                    
                    DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt status %d\n", status));
                    if (SDIO_SUCCESS(status)) {
                        if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
                                /* expecting interrupt */  
                            DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt unmasking write\n"));
                            UnmaskIrqFromIsr(pDeviceContext, HOST_REG_INT_STATUS_TRANSFER_COMPLETE_ENABLE 
                                            | HOST_REG_INT_STATUS_BUFFER_WRITE_RDY_ENABLE);    
                        } else {
                            DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt unmasking read\n"));
                            UnmaskIrqFromIsr(pDeviceContext, HOST_REG_INT_STATUS_TRANSFER_COMPLETE_ENABLE 
                                            | HOST_REG_INT_STATUS_BUFFER_READ_RDY_ENABLE);    
                        }
                        DBG_PRINT(PXA_TRACE_DATA, ("SDIO PCI Ellen Pending from ISR %s transfer \n",
                                                IS_SDREQ_WRITE_DATA(pReq->Flags) ? "TX":"RX"));               	
                    	status = SDIO_STATUS_PENDING; 
                    } else {
                        DBG_PRINT(SDDBG_ERROR, ("SDIO PCI Ellen : Response for Data transfer error :%d n",status));
                        ResetCmdDatLine(pDeviceContext);  		
                    }
                } else {
                    status = SDIO_STATUS_SUCCESS; 
                }
            } else {
                if (IS_SDREQ_DATA_TRANS(pReq->Flags)) {
                    if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
                        /* TX processing */
                        if (ints & 
                                (HOST_REG_NORMAL_INT_STATUS_BUFFER_WRITE_RDY)) {
                            /* clear interrupt */
                            WRITE_HOST_REG16(pDeviceContext, 
                                            HOST_REG_NORMAL_INT_STATUS, 
                                            HOST_REG_NORMAL_INT_STATUS_BUFFER_WRITE_RDY);
                            if (pReq->DataRemaining > 0) {
                                /* transfer data */
                                HcdTransferTxData(pDeviceContext, pReq);
                                return TRUE;
                            } else {
                                /* re-read the interrupt status message to allow us to catch the
                                   transfer complete in one interrupt */
                                ints = READ_HOST_REG16(pDeviceContext, HOST_REG_NORMAL_INT_STATUS);
                                ints &= enables;
                                DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt re-enable \n"));
                            }
                        }
                    } else {
                        /* RX processing */
                        if (ints & 
                            (HOST_REG_NORMAL_INT_STATUS_BUFFER_READ_RDY)) {
                            /* clear interrupt */
                            WRITE_HOST_REG16(pDeviceContext, 
                                            HOST_REG_NORMAL_INT_STATUS, 
                                            HOST_REG_NORMAL_INT_STATUS_BUFFER_READ_RDY );
                                /* unload fifo */
                            HcdTransferRxData(pDeviceContext, pReq);
                            if (pReq->DataRemaining > 0) {
                                return TRUE;
                            }
                        }
                    }
                }
            }

            if (ints & HOST_REG_NORMAL_INT_STATUS_TRANSFER_COMPLETE) {
                if (IS_SDREQ_DATA_TRANS(pReq->Flags)) {
                    DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt Transfer done \n"));
                    /* clear interrupt */
                    WRITE_HOST_REG16(pDeviceContext, 
                                     HOST_REG_NORMAL_INT_STATUS, 
                                     HOST_REG_NORMAL_INT_STATUS_TRANSFER_COMPLETE);
                        /* if we get here without an error, we are done with the read
                         * data operation */
                    status = SDIO_STATUS_SUCCESS;
                }   
            }
        }
    }
    if (errors) {
            /* alter status based on error */
        status = TranslateSDError(pDeviceContext, errors);   
    } 
    
    if (status != SDIO_STATUS_PENDING) {
        if (IS_SDREQ_DATA_TRANS(pReq->Flags)) {
            if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
                TRACE_SIGNAL_DATA_WRITE(pDeviceContext, FALSE);
            } else {
                TRACE_SIGNAL_DATA_READ(pDeviceContext, FALSE);
            } 
        } 
            /* turn off interrupts and clock */
        MaskIrqFromIsr(pDeviceContext, 
                    ~(HOST_REG_INT_STATUS_ALLOW_INSERT_REMOVE_ONLY | 
                      HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE) );
                      
        if (errors) {
                /* reset statemachine */
            ResetCmdDatLine(pDeviceContext);          
        }
        
        if (!pDeviceContext->KeepClockOn) {
            ClockStartStop(pDeviceContext, CLOCK_OFF);
        }
        if (pDeviceContext->CardInserted && (pReq != NULL)) {
                /* set the status */
            pReq->Status = status;
                /* queue work item to notify bus driver of I/O completion */
            QueueEventResponse(pDeviceContext, WORK_ITEM_IO_COMPLETE);
        } else {
            DBG_PRINT(PXA_TRACE_MMC_INT, ("SDIO PCI Ellen HcdSDInterrupt, no request to report: status %d \n",
                                           status));
        }
    }
    
    DBG_PRINT(PXA_TRACE_MMC_INT, ("-SDIO PCI Ellen HcdSDInterrupt Int handler \n"));
    return TRUE;
}



