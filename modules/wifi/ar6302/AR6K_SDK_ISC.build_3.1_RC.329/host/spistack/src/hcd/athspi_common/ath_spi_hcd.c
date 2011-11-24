//------------------------------------------------------------------------------
// <copyright file="ath_spi_hcd.c" company="Atheros">
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
#define MODULE_NAME  ATHSPI
#include "ath_spi_hcd.h"

#define ATH_SHORT_DATA_LIMIT max(ATH_SPI_HOST_CTRL_MAX_BYTES,48) 
#define TODO_FIX_FOR_BMI 1
#define WAR_MAX_PKTS_IN_SPI_WRITE_BUFFER    2

#define CMD_ADDR_READ        (1 << 15)    
#define CMD_ADDRESS_INTERNAL (1 << 14)
  
#define EXTERNAL_ACCESS_TIMEOUT_MS   2000 
#define EXTERNAL_ACCESS_TIMER        0x1
          
#define ADJUST_WRBUF_SPACE(p,b) \
{                               \
     (p)->WriteBufferSpace -= (b);  \
     (p)->WriteBufferSpace -= ATH_SPI_WRBUF_RSVD_BYTES; \
} 
      
#define IS_SLEEP_WAR_ENABLED(p) ((p)->MiscFlags & MISC_FLAG_SPI_SLEEP_WAR)

/* macro to issue command/address phase */                    
#define OUT_TOKEN_CMD_ADDR_DMA_WRITE(pdev,addr) \
        HW_InOut_Token((pdev),(addr),ATH_TRANS_DS_16,NULL)

#define OUT_TOKEN_CMD_ADDR_DMA_READ(pdev,addr)  \
        OUT_TOKEN_CMD_ADDR_DMA_WRITE(pdev,((addr) | CMD_ADDR_READ))
                    
#define OUT_TOKEN_CMD_ADDR_INTERNAL_READ(pdev,addr) \
        OUT_TOKEN_CMD_ADDR_DMA_READ(pdev, ((addr) | CMD_ADDRESS_INTERNAL))
        
#define OUT_TOKEN_CMD_ADDR_INTERNAL_WRITE(pdev,addr) \
        OUT_TOKEN_CMD_ADDR_DMA_WRITE(pdev, ((addr) | CMD_ADDRESS_INTERNAL))             

static SDIO_STATUS DoPioWriteInternal(PSDHCD_DEVICE pDev,
                                      UINT16        Addr,
                                      UINT32        Value);
static SDIO_STATUS DoPioReadInternal(PSDHCD_DEVICE pDev,
                                     UINT16        Addr, 
                                     PUINT32       pValue);
static SDIO_STATUS HcdTransferData(PSDHCD_DEVICE pDevice, PSDREQUEST pReq);

#ifdef DEBUG
static void DumpSpiInternalRegisters(PSDHCD_DEVICE pDevice);
static void DumpInternalRegister(PSDHCD_DEVICE pDevice, UINT16 Reg);
#else
#define DumpSpiInternalRegisters(p)
#define DumpInternalRegister(p,r)
#endif
static void EnableDisableSPIIRQHwDetect(PSDHCD_DEVICE pDevice, BOOL Enable);

#ifdef DEBUG

#define DUMP_REQUEST(z,pReq) \
    if (DBG_GET_DEBUG_LEVEL() >= (z)) DumpRequest((pReq))
    
#define DBG_DUMP_DATA_BUFFER(cond, pReq)  \
{                               \
    if ((DBG_GET_DEBUG_LEVEL() >= ATH_SPI_TRACE_DATA_DUMP) && (cond)) DumpDataBuffer(pReq); \
}

#define TODO_TOGGLE_DBG_SIG(p,pin)  HW_ToggleDebugSignal((p), (pin))
#define TODO_SET_DBG_SIG(p,pin,on) HW_SetDebugSignal((p), (pin), (on))

#else
#define DBG_DUMP_DATA_BUFFER(cond, pReq) { }
#define DUMP_REQUEST(z,pReq)
#define TODO_TOGGLE_DBG_SIG(p,pin)
#define TODO_SET_DBG_SIG(p,pin,on)
#endif
       
#define SET8_PART(p,i,v) \
          ((PUINT8)(p))[(i)] = (UINT8)(v);
          
#define ASSEMBLE16_PART(p,i)  ((UINT16)((PUINT8)(p))[(i)])
#define ASSEMBLE16_VALUE(p) (ASSEMBLE16_PART(p,0) | (ASSEMBLE16_PART(p,1) << 8))
           
#define SET16_VALUE(p,v)       \
{                              \
    SET8_PART(p,0,(v));        \
    SET8_PART(p,1,(v) >> 8);   \
}

#define ASSEMBLE32_PART(p,i)  ((UINT32)((PUINT8)(p))[(i)])
#define ASSEMBLE32_VALUE(p) (ASSEMBLE32_PART(p,0) | (ASSEMBLE32_PART(p,1) << 8) | \
                              (ASSEMBLE32_PART(p,2) << 16) | (ASSEMBLE32_PART(p,3) << 24))

#define ASSEMBLE32_VALUE_INLINE(v,pS)   \
{                                       \
    register UINT32 _t16;               \
    _t16 = ((PUINT16)(pS))[0];          \
    (v)  = (_t16 & 0x00FF) << 24;   \
    (v) |= (_t16 & 0xFF00) << 8;    \
    _t16 = ((PUINT16)(pS))[1];      \
    (v) |= (_t16 & 0x00FF) << 8;    \
    (v) |= (_t16 & 0xFF00) >> 8;    \
}

#define SET32_VALUE_INLINE(pD,v)        \
{                                       \
    register UINT32 _t16;               \
    _t16  = (v & 0x00FF0000) >> 8;      \
    _t16 |= (v & 0xFF000000) >> 24;     \
    ((PUINT16)(pD))[0] = (UINT16)_t16;  \
    _t16 = (v & 0x0000FF00) >> 8;      \
    _t16 |= (v & 0x000000FF) << 8;     \
    ((PUINT16)(pD))[1] = (UINT16)_t16;  \
}
           
#define SET32_VALUE(p,v)       \
{                              \
    SET8_PART(p,0,(v));       \
    SET8_PART(p,1,(v) >> 8);  \
    SET8_PART(p,2,(v) >> 16); \
    SET8_PART(p,3,(v) >> 24); \
}


/* dump data buffer in the request */
static void DumpDataBuffer(PSDREQUEST pReq)
{
    if (ATH_IS_TRANS_DMA(pReq)) {
        if (ATH_IS_TRANS_WRITE(pReq)) {
            SDLIB_PrintBuffer(pReq->pDataBuffer, ATH_GET_DMA_TRANSFER_BYTES(pReq), "ATH SPI - DMA Data Write");    
        } else {
            SDLIB_PrintBuffer(pReq->pDataBuffer, ATH_GET_DMA_TRANSFER_BYTES(pReq), "ATH SPI - DMA Data Read");         
        }
    } else {
            /* host access dump */
        if (ATH_IS_TRANS_WRITE(pReq)) {
            SDLIB_PrintBuffer(pReq->pDataBuffer, ATH_GET_EXT_TRANSFER_BYTES(pReq), "ATH SPI - Host Access Data Write");    
        } else {
            SDLIB_PrintBuffer(pReq->pDataBuffer, ATH_GET_EXT_TRANSFER_BYTES(pReq), "ATH SPI - Host Access Data Read");         
        }        
    }
}    

static void DumpRequest(PSDREQUEST pReq) 
{

    REL_PRINT(SDDBG_ERROR, ("************************REQUEST DUMP*********************** \n"));
             
    do {
        
        if (ATH_IS_TRANS_PIO(pReq)) {
            
            REL_PRINT(SDDBG_ERROR, ("PIO Request %s , %s , Address : 0x%4.4X\n",
                    ATH_IS_TRANS_PIO_INTERNAL(pReq) ? "INTERNAL" : "EXTERNAL",
                    ATH_IS_TRANS_READ(pReq) ? "READ" : "WRITE",
                    ATH_GET_IO_ADDRESS(pReq)));
             
            if (ATH_IS_TRANS_PIO_INTERNAL(pReq)) {
                if (ATH_IS_TRANS_WRITE(pReq)) { 
                    REL_PRINT(SDDBG_ERROR, ("INTERNAL WRITE data: 0x%4.4X   \n",ATH_GET_PIO_WRITE_VALUE(pReq)));
                }
                break;
            }
            
            REL_PRINT(SDDBG_ERROR, ("EXTERNAL Access : %d  bytes  buffer:0x%X \n",
                    ATH_GET_EXT_TRANSFER_BYTES(pReq), (UINT32)pReq->pDataBuffer));
             
            break;
        }
         
        /* DMA requests */
            
        REL_PRINT(SDDBG_ERROR, ("DMA Request %s , Address : 0x%4.4X\n",
                    ATH_IS_TRANS_READ(pReq) ? "READ" : "WRITE",
                    ATH_GET_IO_ADDRESS(pReq)));
                    
        REL_PRINT(SDDBG_ERROR, ("   %d  bytes  buffer:0x%X \n",
                    ATH_GET_DMA_TRANSFER_BYTES(pReq), (UINT32)pReq->pDataBuffer));
                          
    } while (FALSE);   
    
    REL_PRINT(SDDBG_ERROR, ("************************************************************ \n")); 
}
 
/* copy to/from a common buffer, this function assumes that at least one of the buffer pointers, is
 * aligned.  This is typically the common buffer allocated in the hardware later */                            
void HcdCommonBufferCopy(UINT8         DataSize, 
                         PVOID         pDest, 
                         PVOID         pSrc, 
                         INT           Bytes,
                         BOOL          ByteSwap)
{
    INT     i;
    UINT32  alignflags = 0;
    #define SRC_MIS_ALIGNED  0x1
    #define DEST_MIS_ALIGNED 0x2
    #define ALIGN_GOOD       0x0
    #define BOTH_ALIGN_BAD   0x3
    
    DBG_PRINT(ATH_SPI_TRACE_INFO, ("ATH SPI - CommonBufferCopy: DataCopy: %d (swap mode:%s)\n", 
              Bytes, ByteSwap ? "Swap":"No-Swap"));

    if (!ByteSwap || (DataSize == ATH_TRANS_DS_8)) {
            /* for 8-bit copies or no-conversion */
        memcpy(pDest,pSrc,Bytes);
        return;
    }
    
    do {
            /* for 16 or 32 bit , do the byte swap */
        if (DataSize == ATH_TRANS_DS_32) {
            PUINT32 pSrc32; 
            PUINT32 pDest32; 
            register UINT32  temp;
            
            pSrc32 = (PUINT32)pSrc;
            pDest32 = (PUINT32)pDest;
            
            alignflags = ((UINT32)pSrc & 3) ? SRC_MIS_ALIGNED : 0;
            alignflags |= ((UINT32)pDest & 3) ? DEST_MIS_ALIGNED : 0;
            
            if (ALIGN_GOOD == alignflags) {
                for (i = 0 ; i < (Bytes >> 2); i++,pDest32++,pSrc32++) {
                    *pDest32 = CT_BE32_TO_CPU_ENDIAN(*pSrc32);    
                }
                
                break;
            }
        
            DBG_PRINT(ATH_SPI_TRACE_INFO, ("ATH SPI - 32-bit misalign pSrc:0x%X, pDest:0x%X\n",
                    (UINT32)pSrc,(UINT32)pDest));  
                            
            for (i = 0 ; i < (Bytes >> 2); i++,pDest32++,pSrc32++) {
                switch (alignflags) {
                    case SRC_MIS_ALIGNED:
#if  1
                        ASSEMBLE32_VALUE_INLINE(temp,pSrc32);
#else
                        temp = ASSEMBLE32_VALUE(pSrc32);
                        temp = CT_BE32_TO_CPU_ENDIAN(temp);
#endif                            
                        *pDest32 = temp;
                        break;
                    case DEST_MIS_ALIGNED:
#if 1   
                        temp = *pSrc32;
                        SET32_VALUE_INLINE(pDest32,temp);
#else                         
                        temp = CT_BE32_TO_CPU_ENDIAN(*pSrc32);   
                        SET32_VALUE(pDest32,temp);  
#endif 
                
                        break;
                    default:
                            /* this should never happen in this driver, one of the buffers MUST
                             * be aligned */
                        DBG_ASSERT(FALSE);
                        break;
                }
            
                break;   
            }
        }
        
            /* 16 bit case */    
        if (DataSize == ATH_TRANS_DS_16) { 
            PUINT16 pSrc16; 
            PUINT16 pDest16; 
            register UINT16 temp;
            
            pSrc16 = (PUINT16)pSrc;
            pDest16 = (PUINT16)pDest;
            
            alignflags = ((UINT32)pSrc & 1) ? SRC_MIS_ALIGNED : 0;
            alignflags |= ((UINT32)pDest & 1) ? DEST_MIS_ALIGNED : 0;
            
            if (ALIGN_GOOD == alignflags) {
                for (i = 0 ; i < (Bytes >> 1); i++,pSrc16++,pDest16++) {
                    *pDest16 = CT_BE16_TO_CPU_ENDIAN(*pSrc16);
                }    
                break;
            }
                
            DBG_PRINT(ATH_SPI_TRACE_INFO, ("ATH SPI - 16-bit misalign pSrc:0x%X, pDest:0x%X\n",
                    (UINT32)pSrc,(UINT32)pDest));  
                        
            for (i = 0 ; i < (Bytes >> 1); i++,pSrc16++,pDest16++) {
                switch (alignflags) {
                    case SRC_MIS_ALIGNED:
                        temp = ASSEMBLE16_VALUE(pSrc16);
                        temp = CT_BE16_TO_CPU_ENDIAN(temp);
                        *pDest16 = temp;
                        break;
                    case DEST_MIS_ALIGNED:
                        temp = CT_BE16_TO_CPU_ENDIAN(*pSrc16);
                        SET16_VALUE(pDest16,temp);        
                        break;
                    default:
                            /* this should never happen in this driver, one of the buffers MUST be aligned */
                        DBG_ASSERT(FALSE);
                        break;
                }      
            } 
        }
            
    } while (FALSE);
    
}

/*
 * DMA completion routine, called by the HW layer in a normal scheduling context
*/
void HcdDmaCompletion(PSDHCD_DEVICE pDevice, SDIO_STATUS Status) 
{
    PSDREQUEST      pReq = GET_CURRENT_REQUEST(&pDevice->Hcd);
    BOOL            doIOComplete = FALSE;
    
    if (NULL == pReq) {
        DBG_ASSERT(FALSE);
        return;    
    }   
    
    if (!SDIO_SUCCESS(CriticalSectionAcquire(&pDevice->CritSection))) {
        DBG_ASSERT(FALSE);
        return;    
    }
     
    if (!SDIO_SUCCESS(Status)) {
        DBG_PRINT(SDDBG_ERROR, 
            ("ATH SPI - DMA Completion: %s transfer completion failed, status: %d\n", 
            ATH_IS_TRANS_READ(pReq) ? "RX":"TX", Status));
        DUMP_REQUEST(SDDBG_ERROR,pReq);  
    } else {
            /* check and see if need to move more data, this also covers the case of
             * early termination where we need to move the last few bytes manually */          
        if (pReq->DataRemaining > 0) {
                /* call into transfer state machine again */
            Status = HcdTransferData(pDevice, pReq);
        }
        
    }
    
    if (Status != SDIO_STATUS_PENDING) {
            /* were done */
        DBG_PRINT(ATH_SPI_TRACE_REQUESTS, 
            ("ATH SPI - HcdDmaCompletion - %s (%d bytes) transfer complete  (status=%d) \n", 
                 ATH_IS_TRANS_READ(pReq) ? "RX":"TX", ATH_GET_DMA_TRANSFER_BYTES(pReq), Status));       
        pDevice->DMAHWTransferInProgress = FALSE;        
        pReq->Status = Status;       
            /* re-enable spi interrupt processing */
        EnableDisableSPIIRQHwDetect(pDevice,TRUE);
            /* do completion outside the lock */
        doIOComplete = TRUE;       
        DBG_DUMP_DATA_BUFFER(SDIO_SUCCESS(Status) && ATH_IS_TRANS_READ(pReq), pReq);
    } else {
            /* start DMA again */
        HW_StartDMA(pDevice);     
    }
    
    CriticalSectionRelease(&pDevice->CritSection);
    
    if (doIOComplete) {       
            /* indicate completion */
        SDIO_HandleHcdEvent(&pDevice->Hcd, EVENT_HCD_TRANSFER_DONE);    
    }
    
}

/* this function outputs/inputs data frames on the SPI bus, the caller has to setup control
 * tokens ahead of this operation to setup for the transfer.  There are two request types that
 * take advantage of this, DMA transfers and External I/O access through read/write ports.
 * 
 * For DMA transfers:
 *    If the data count is small, there is less overhead to use manual dataframes instead of
 *    activating the SPI DMA hardware and taking a completion interrupt
 *    If the data transfer requires early termination, the remaining frame is sent using this function
 * 
 * Extern I/O access:
 *     All external I/O access through the RD/WR ports uses this function to generate the data
 *     frames.  External I/O accesses are short (< 32 bytes).
 */

static SDIO_STATUS DoDataFrames(PSDHCD_DEVICE pDevice, 
                                PVOID         pBuffer, 
                                INT           Bytes, 
                                ATH_TRANS_CMD Cmd,
                                BOOL          ByteSwap)
{  
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;  
    PUINT8  pBuf;
    UINT32  incr;
    UINT32  data;
    
    pBuf = pBuffer;    
    
    DBG_PRINT(ATH_SPI_TRACE_REQUESTS, 
       ("ATH SPI - DoDataFrames : Cmd:0x%X , %d bytes\n",Cmd,Bytes));
    
    if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_16) {
        incr = 2;   
    } else if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_32) {
        incr = 4;
    } else if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_24) {
        incr = 3;
    } else {
        incr = 1;
    }
      
    while (Bytes) {
        if (ATH_TRANS_IS_DIR_READ(Cmd)) {
            
            status = HW_InOut_Token(pDevice,0xFFFFFFFF,ATH_GET_TRANS_DS(Cmd),&data); 
            
            if (!SDIO_SUCCESS(status)) {
                break;   
            }
                                
            if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_16) {
                if (ByteSwap) {
                    pBuf[0] = (UINT8)(data >> 8);
                    pBuf[1] = (UINT8)(data);
                } else {
                    pBuf[1] = (UINT8)(data >> 8);
                    pBuf[0] = (UINT8)(data);   
                }
            } else if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_32) {
                if (ByteSwap) {
                    pBuf[0] = (UINT8)(data >> 24);
                    pBuf[1] = (UINT8)(data >> 16);
                    pBuf[2] = (UINT8)(data >> 8);
                    pBuf[3] = (UINT8)(data); 
                } else {
                    pBuf[3] = (UINT8)(data >> 24);
                    pBuf[2] = (UINT8)(data >> 16);
                    pBuf[1] = (UINT8)(data >> 8);
                    pBuf[0] = (UINT8)(data);    
                }
            } else if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_24) {
                 if (ByteSwap) {
                     pBuf[0] = (UINT8)(data >> 16);
                     pBuf[1] = (UINT8)(data >> 8);
                     pBuf[2] = (UINT8)(data);
                 } else {
                     pBuf[2] = (UINT8)(data >> 16);
                     pBuf[1] = (UINT8)(data >> 8);
                     pBuf[0] = (UINT8)(data);   
                 }
            } else {
                *pBuf = (UINT8)data;   
            }
        } else {
            if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_16) {
                if (ByteSwap) {
                    data  = ((UINT16)pBuf[0]) << 8;
                    data |= ((UINT16)pBuf[1]);
                } else {
                    data  = ((UINT16)pBuf[1]) << 8;
                    data |= ((UINT16)pBuf[0]);    
                }               
            } else if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_32) {
               if (ByteSwap) {
                    data =  ((UINT32)pBuf[0]) << 24;
                    data |= ((UINT32)pBuf[1]) << 16;
                    data |= ((UINT32)pBuf[2]) << 8;
                    data |= ((UINT32)pBuf[3]);   
               } else {
                    data =  ((UINT32)pBuf[3]) << 24;
                    data |= ((UINT32)pBuf[2]) << 16;
                    data |= ((UINT32)pBuf[1]) << 8;
                    data |= ((UINT32)pBuf[0]);        
               }
            } else if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_24) {
                if (ByteSwap) {
                    data  = ((UINT32)pBuf[0]) << 16;
                    data |= ((UINT32)pBuf[1]) << 8;
                    data |= (UINT32)pBuf[2];   
                } else {
                    data  = ((UINT32)pBuf[2]) << 16;
                    data |= ((UINT32)pBuf[1]) << 8;
                    data |= (UINT32)pBuf[0];       
                }
            } else {
                data = (UINT32)(*pBuf); 
            }
            
            status = HW_InOut_Token(pDevice,data,ATH_GET_TRANS_DS(Cmd),NULL);   
            
            if (!SDIO_SUCCESS(status)) {
                break;   
            }
        }
        
        Bytes -= incr;  
        pBuf += incr; 
    }
        
    return status;        
}

/* This function checks DMA or host access request for early termination. Early termination issues
 * a data frame that is less than the initial transfer word size.  For example
 * if the number of bytes in the host access was 7 and the word size was 32-bits (4-bytes per frame) the
 * transaction would be issued as:
 *   
 *  [32 bit frame] [24-bit frame]
 *    (4 bytes)      (3 bytes)
 * */
static SDIO_STATUS CheckEarlyTermination(PSDHCD_DEVICE pDevice, 
                                         PVOID         pBuffer, 
                                         INT           Length, 
                                         ATH_TRANS_CMD Cmd)
{
    SDIO_STATUS   status = SDIO_STATUS_PENDING;
    INT           bytes = 0;
    ATH_TRANS_CMD newCmd;
    
    newCmd = Cmd & (~ATH_TRANS_DS_MASK);
    
    do {  
        /* check for early termination on 16 and 32 bit transfers */
        if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_16) {
            if (Length == 1) {
                DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - 16 bit early termination : %d bytes \n", 
                        Length));
                bytes = 1;
                newCmd |= ATH_TRANS_DS_8;
            }
            break;
        }
        
        if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_32) {
            if (Length > 4) {
                break;
            }
                /* for 4 bytes or less don't use DMA */
            DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - 32 bit early termination : %d bytes \n", 
                    Length)); 
                    
            bytes = Length; 
            switch (bytes) { 
                case 4 :
                        /* early terminate 32 bit */
                    newCmd |= ATH_TRANS_DS_32;   
                    break;               
                case 3:
                    if (pDevice->SpiHWCapabilitiesFlags & HW_SPI_FRAME_WIDTH_24) {
                        newCmd |= ATH_TRANS_DS_24;   
                    } else {
                            /* send them out as 8-bit data frames */
                        newCmd |= ATH_TRANS_DS_8;  
                    }
                    break; 
                case 2:
                        /* early terminate using 16 bit transfer */
                    newCmd |= ATH_TRANS_DS_16; 
                    break;
                case 1:
                        /* early terminate using 8 bit transfer */
                    newCmd |= ATH_TRANS_DS_8; 
                    break;
                default:
                    DBG_ASSERT(FALSE);
                    status = SDIO_STATUS_INVALID_PARAMETER;
                    /* done */
                    break;
            }
        }  
        
    } while (FALSE);
    
    if (bytes != 0) {
            /* do the early termination, early termination requires us to byteswap the
             * data buffer */
        status = DoDataFrames(pDevice, 
                              pBuffer, 
                              bytes,
                              newCmd,
                              pDevice->HostAccessCopyMode);      
    }  
    return status;  
}

/* this function transfers data frames without using SPI Host controller DMA hardware */
static SDIO_STATUS ManualDataFrameTransfer(PSDHCD_DEVICE pDevice, 
                                           PVOID         pBuffer, 
                                           INT           Bytes, 
                                           ATH_TRANS_CMD Cmd)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    INT         bytesTransfer;
  
        /* break this up into whole WORDS/DWORDS */
    if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_16) {
        bytesTransfer = Bytes & ~0x1;
    } else if (ATH_GET_TRANS_DS(Cmd) == ATH_TRANS_DS_32) {
        bytesTransfer = Bytes & ~0x3;
    } else {
        bytesTransfer = Bytes;    
    }
    
    do {
                
        if (bytesTransfer) {
                /* do the WORD/DWORD aligned portion of the data transfer
                 * CheckEarlyTermination will issue the remainder 
                 * note: if the transfer is part of a DMA request then the target module
                 * may do byte swapping for us otherwise for external I/O, we have
                 * to manually convert ourselves */
            status = DoDataFrames(pDevice, 
                                  pBuffer, 
                                  bytesTransfer, 
                                  Cmd,
                                  (Cmd & ATH_TRANS_DMA) ? pDevice->HostDMABufferCopyMode : pDevice->HostAccessCopyMode);
                                  
            if (!SDIO_SUCCESS(status)) {
                break;   
            }
        }
        
        Bytes -= bytesTransfer; 
        if (0 == Bytes) {
            break;  
        }
        
        status = CheckEarlyTermination(pDevice, 
                                       ((PUINT8)pBuffer + bytesTransfer),
                                       Bytes,Cmd);
    } while (FALSE);
       
    return status;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdTransferData - transfer data state machine
  Input:  pDevice - device object
          pReq    - transfer request
  Output: 
  Return: 
  Notes: This state machine handles the data phases of a DMA request or an external I/O access through
         the read and write ports.
         This state machine handles early termination of bytes and transfers bytes manually if
         the remaining bytes is small.
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static SDIO_STATUS HcdTransferData(PSDHCD_DEVICE pDevice, PSDREQUEST pReq)
{
    INT            dataCopy;
    SDIO_STATUS    status = SDIO_STATUS_SUCCESS;
    ATH_TRANS_CMD  cmd;
        
    DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - HcdTransferData: data remaining: %d, pBuf: 0x%X\n",
                             pReq->DataRemaining, (UINT)pReq->pHcdContext));
    
    cmd = (ATH_TRANS_CMD)ATH_GET_IO_CMD(pReq);
    
        /* ignore the data transfer width, in the old protocol this could be specified by
         * the function driver. Now the data frame width is controlled by the host driver based
         * on the spi module and spi controller capability */
    cmd &= ~ATH_TRANS_DS_MASK;
    
    if (cmd & ATH_TRANS_DMA) {
            /* for DMA transfers, use this data width for data phases */
        cmd |= pDevice->DMADataWidth;
    } else {
            /* for external access transfers, use this data width */
        cmd |= pDevice->HostAccessDataWidth;
    }
    
    do { 
        
            /* check for short transfers or external I/O transactions */
        if ((pReq->DataRemaining < ATH_SHORT_DATA_LIMIT) || (cmd & ATH_TRANS_EXT_TRANS) ||
            (pDevice->SpiHWCapabilitiesFlags & HW_SPI_NO_DMA)) {
                /* perform data frames manually */
            status = ManualDataFrameTransfer(pDevice, 
                                             pReq->pHcdContext, 
                                             pReq->DataRemaining,
                                             cmd); 
            if (SDIO_SUCCESS(status)) {  
                pReq->DataRemaining = 0;
            }           
            break;
        }
        
        if (cmd & ATH_TRANS_EXT_TRANS) {
                /* if this is an external I/O request there is nothing more to do */
            break;    
        }                            
                   
            /* we will use the SPI Host DMA controller for the data frames, common buffer is currently supported */
        dataCopy = min(pReq->DataRemaining, (UINT32)pDevice->MaxBytesPerDMARequest);   
    
            /* align amount to transfer width */
        if (ATH_GET_TRANS_DS(cmd) == ATH_TRANS_DS_16) {
            dataCopy &= ~0x1;  
        } else if (ATH_GET_TRANS_DS(cmd) == ATH_TRANS_DS_32) {
            dataCopy &= ~0x3;     
        }
                       
            /* save starting position for HW DMA*/
        pDevice->pCurrentBuffer =  (PUINT8)pReq->pHcdContext;   
            /* save current transfer size for HW DMA*/
        pDevice->CurrentTransferLength = dataCopy;
            /* save direction for HW DMA */
        pDevice->CurrentTransferDirRx = ATH_TRANS_IS_DIR_WRITE(cmd) ? FALSE : TRUE;
            /* save transfer width for HW DMA */
        pDevice->CurrentDmaWidth = ATH_GET_TRANS_DS(cmd);
                 
            /* update remaining count */
        pReq->DataRemaining -= dataCopy;  
            /* advance pointer position by what we could do for the next entry into this
             * state machine */
        pReq->pHcdContext = &((PUINT8)pReq->pHcdContext)[dataCopy];
        
        DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - using DMA %s (width:%d) \n",
                        pDevice->CurrentTransferDirRx ? "RX":"TX",
                        pDevice->CurrentDmaWidth));
                                              
            /* set up DMA HW  */ 
        status =  HW_SpiSetUpDMA(pDevice);
                                    
        if (!SDIO_SUCCESS(status)) {
            break;   
        } 
                           
    } while (FALSE);
    
    return status;
}

/* enable/disable SPI interrupt detection, can only be called from normal scheduling context */
static void EnableDisableSPIIRQHwDetect(PSDHCD_DEVICE pDevice, BOOL Enable)
{    
    if ((pDevice->SpiHWCapabilitiesFlags & HW_SPI_INT_EDGE_DETECT) && Enable) {
        /* the SPI interrupt is edge triggered and has requirements for a good edge 
         * when we enable interrupt detection we disable all interrupts on the module and
           then re-enable them again to generate a good edge */
        
        if (!SDIO_SUCCESS(DoPioWriteInternal(pDevice,ATH_SPI_INTR_ENABLE_REG,0))) {
            DBG_ASSERT(FALSE);    
        }    
            /* re-enable SPI interrupt detection at the host controller */ 
        HW_EnableDisableSPIIRQ(pDevice,TRUE,HW_FROM_NORMAL_CONTEXT);   
        
            /* re-enable interrupt sources, if there are pending interrupts, this should
             * generate a nice clean edge */
        if (!SDIO_SUCCESS(DoPioWriteInternal(pDevice,ATH_SPI_INTR_ENABLE_REG,pDevice->SpiIntEnableShadow))) {
            DBG_ASSERT(FALSE);    
        }     
        
    } else {
            /* the SPI interrupt logic is level triggered (or we are simply disabling) so we can simply tell
             * the hardware layer to enable/disable it */
        HW_EnableDisableSPIIRQ(pDevice,Enable,HW_FROM_NORMAL_CONTEXT); 
    }
}

/* PIO internal writes (16-bit) */
static SDIO_STATUS DoPioWriteInternal(PSDHCD_DEVICE pDev, 
                                      UINT16        Addr,
                                      UINT32        Value) 
{   
    SDIO_STATUS status;
    
    Addr |= CMD_ADDRESS_INTERNAL;  
          
        /* issue CMD/ADDR token */                                                 
    status = OUT_TOKEN_CMD_ADDR_INTERNAL_WRITE(pDev,Addr);
    
    if (!SDIO_SUCCESS(status)) {                     
        return status;                                    
    }  
        /* send out data */
    return HW_InOut_Token(pDev,Value,ATH_TRANS_DS_16,NULL);           
}   

/* PIO internal reads (16-bit) */        
static SDIO_STATUS DoPioReadInternal(PSDHCD_DEVICE pDev, 
                              UINT16        Addr, 
                              PUINT32       pValue) 
{   
    SDIO_STATUS status;  
    
    do {
               
        Addr |= CMD_ADDRESS_INTERNAL;  
                   
        status = OUT_TOKEN_CMD_ADDR_INTERNAL_READ(pDev,Addr); 
        
        if (!SDIO_SUCCESS(status)) {                      
            break;                                      
        } 
              
        status = HW_InOut_Token(pDev,0xFFFFFFFF,ATH_TRANS_DS_16,pValue);
        
    } while (FALSE);
    
    return status;                                   
}   

/* mask SPI interrupts (module interrupt sources), caller needs to acquire lock to protect */
static SDIO_STATUS UnmaskSPIInterrupts(PSDHCD_DEVICE pDevice, UINT16 Mask)
{
    pDevice->SpiIntEnableShadow |= Mask;
    
    return DoPioWriteInternal(pDevice, ATH_SPI_INTR_ENABLE_REG, (UINT32)pDevice->SpiIntEnableShadow);
}

/* mask SPI interrupts (module interrupt sources) caller needs to acquire lock to protect*/
static SDIO_STATUS MaskSPIInterrupts(PSDHCD_DEVICE pDevice, UINT16 Mask)
{
    pDevice->SpiIntEnableShadow &= ~Mask;   

    return DoPioWriteInternal(pDevice, ATH_SPI_INTR_ENABLE_REG, (UINT32)pDevice->SpiIntEnableShadow);
}

/* handle the completion of an external read done */
static INLINE SDIO_STATUS HandleExternalReadDone(PSDHCD_DEVICE pDevice, 
                                                 PSDREQUEST    pReq)
{
    SDIO_STATUS status;
    
        /* send out cmd/addr token */    
    status = OUT_TOKEN_CMD_ADDR_INTERNAL_READ(pDevice, ATH_SPI_HOST_CTRL_RD_PORT_REG);

    if (!SDIO_SUCCESS(status)) {                   
        return status;                                  
    } 
        /* do the data frames to get the data */
    status = HcdTransferData(pDevice,pReq);
        /* this should never return pending, external I/O accesses do not invoke the DMA hardware */
    DBG_ASSERT(status != SDIO_STATUS_PENDING);
    
    DBG_DUMP_DATA_BUFFER(SDIO_SUCCESS(status),pReq);
    
    return status;
}

/* PIO external access, reads/writes
 * accessing external registers requires the use of the SPI slave controller's internal proxy state machine
 * in the event that the chip is asleep.  The internal proxy performs the operation 
 * and will signal us via an interrupt or polling operation */
static SDIO_STATUS DoPioExternalAccess(PSDHCD_DEVICE pDevice,
                                       PSDREQUEST    pReq)
{
    SDIO_STATUS status;
    UINT32      regValue;
    int         retry;
    
    do {
                 
        pReq->pHcdContext = pReq->pDataBuffer;
        pReq->DataRemaining = ATH_GET_EXT_TRANSFER_BYTES(pReq);
            
        DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - External Access %s : Addr:0x%X, Bytes:%d Address Mode:%s\n",
                  ATH_IS_TRANS_READ(pReq) ? "Read":"Write",
                  ATH_GET_IO_ADDRESS(pReq),
                  ATH_GET_EXT_TRANSFER_BYTES(pReq),
                  ATH_IS_TRANS_EXT_ADDR_FIXED(pReq) ? "Fixed":"Increment"));
                        
            /* set incrementing or fixed addressing */
        regValue = ATH_IS_TRANS_EXT_ADDR_FIXED(pReq) ? ATH_SPI_HOST_CTRL_NO_ADDR_INC : 0;
            /* set the length */
        regValue |= pReq->DataRemaining;        
        
        DBG_PRINT(ATH_SPI_TRACE_INFO, ("ATH SPI - External Access BYTE_SIZE_REG:0x%X \n",regValue));
                  
            /* write control reg */
        status = DoPioWriteInternal(pDevice, ATH_SPI_HOST_CTRL_BYTE_SIZE_REG, regValue);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }                              
        
        if (ATH_IS_TRANS_WRITE(pReq)) {
            DBG_DUMP_DATA_BUFFER(TRUE,pReq);
                /* write to the data port  */
                /* send out cmd/addr token */    
            status = OUT_TOKEN_CMD_ADDR_INTERNAL_WRITE(pDevice, ATH_SPI_HOST_CTRL_WR_PORT_REG);
    
            if (!SDIO_SUCCESS(status)) {                     
                break;                                   
            } 
                /* do the data frames */
            status = HcdTransferData(pDevice,pReq);
                /* these frames should go out without using interrupts */
            DBG_ASSERT(status != SDIO_STATUS_PENDING);
            if (!SDIO_SUCCESS(status)) {                     
                break;                                   
            } 
        }
        
            /* enable, set direction and set address range to do the operation on */
        regValue = ATH_SPI_HOST_CTRL_CONFIG_ENABLE;
        regValue |= ATH_IS_TRANS_READ(pReq) ? 0 : ATH_SPI_HOST_CTRL_CONFIG_DIR_WRITE;
        regValue |= ATH_GET_IO_ADDRESS(pReq);
        
        DBG_PRINT(ATH_SPI_TRACE_INFO, ("ATH SPI - External Access CTRL_CONFIG_REG:0x%X \n",regValue));
         
            /* write config to start the operation */
        status = DoPioWriteInternal(pDevice, ATH_SPI_HOST_CTRL_CONFIG_REG, regValue);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        } 
        
            /* poll the host_access_done bit in SPI_STATUS register, if the access takes longer
             * than the retry count, the core was probably asleep, we need to wait for an interrupt
             * for the operation to complete */
        retry = EXTERNAL_ACCESS_DONE_RETRY_COUNT;
                 
        while (retry) {
            
            status = DoPioReadInternal(pDevice, ATH_SPI_STATUS_REG, &regValue);
            
            if (!SDIO_SUCCESS(status)) {
                break;    
            }
            
            DBG_PRINT(ATH_SPI_TRACE_INFO, 
                    ("ATH SPI - External Access SPI_STATUS_REG Poll:0x%X \n",regValue));
            
            if (regValue & ATH_SPI_STATUS_HOST_ACCESS_DONE) {
                DBG_PRINT(ATH_SPI_TRACE_REQUESTS,("ATH SPI External Access - Host access done in-line \n"));
                break;    
            }
            
            retry--;   
        }
        
        if (0 == retry) {
            DBG_PRINT(ATH_SPI_TRACE_REQUESTS,
                ("ATH SPI External Access - Host done timeout, enable and wait on interrupt \n"));
            pDevice->ExternalIOPending = TRUE;
                /* start timer */
            HW_StartTimer(pDevice, EXTERNAL_ACCESS_TIMEOUT_MS, EXTERNAL_ACCESS_TIMER);
                /* unmask interrupt and wait for completion */
            if (ATH_IS_TRANS_READ(pReq)) {
                UnmaskSPIInterrupts(pDevice, ATH_SPI_INTR_HOST_CTRL_RD_DONE);
            } else {
                UnmaskSPIInterrupts(pDevice, ATH_SPI_INTR_HOST_CTRL_WR_DONE);    
            }
            status = SDIO_STATUS_PENDING;
            break;
        } 
        
        /* if we get here the chip was awake and the access finished within the polling interval */
        
        if (ATH_IS_TRANS_READ(pReq)) {
            /* for reads, empty the read port */
            status = HandleExternalReadDone(pDevice,pReq);
        }       
        
            /* clear the interrupt cause, each host access operation will set the RD or WR
             * cause bit */
        status = DoPioWriteInternal(pDevice,
                                    ATH_SPI_INTR_CAUSE_REG,
                                    ATH_SPI_INTR_HOST_CTRL_RD_DONE | ATH_SPI_INTR_HOST_CTRL_WR_DONE);
                                         
        if (!SDIO_SUCCESS(status)) {
            break;    
        }         
            
    } while (FALSE);
    
    return status;
}
    
static INLINE SDIO_STATUS ConfigureByteSwap(PSDHCD_DEVICE pDevice, UINT8 TransferType)
{
    UINT16 swapSettings;
    
    swapSettings = pDevice->SpiConfigShadow;
    
    /* based on the transfer type, figure out what mode settings we need */
                     
    if (TransferType == ATH_TRANS_DS_32) {
            /* make sure swap is turned on */
        swapSettings |= ATH_SPI_CONFIG_BYTE_SWAP;
            /* turn off 16 bit mode - which actually turns ON 32 bit swapping */
        swapSettings &= ~ATH_SPI_CONFIG_SWAP_16BIT; 
    } else if (TransferType == ATH_TRANS_DS_16) {
            /* make sure swap is turned on */
        swapSettings |= ATH_SPI_CONFIG_BYTE_SWAP;
            /* turn on 16 bit swapping mode */
        swapSettings |= ATH_SPI_CONFIG_SWAP_16BIT;   
    } else if (TransferType == ATH_TRANS_DS_8) {
            /* disable byte swapping entirely */
        swapSettings &= ~ATH_SPI_CONFIG_BYTE_SWAP;
        swapSettings &= ~ATH_SPI_CONFIG_SWAP_16BIT;    
    } else {
        DBG_ASSERT(FALSE);    
    }       
    
        /* did any bits change? */
    if (swapSettings ^ pDevice->SpiConfigShadow) {
        DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - data mode:%d requires byte swap config change: Old:0x%X, New:0x%X\n",
                        TransferType,pDevice->SpiConfigShadow,swapSettings));
            /* save new value */
        pDevice->SpiConfigShadow = swapSettings;
            /* write it out */
        return DoPioWriteInternal(pDevice, ATH_SPI_CONFIG_REG, swapSettings);   
    }
    
    return SDIO_STATUS_SUCCESS;
}

/* reset write buffer water mark level */
static SDIO_STATUS ResetWriteBufferWaterMark(PSDHCD_DEVICE pDevice)
{
    SDIO_STATUS status;
 
    status = DoPioWriteInternal(pDevice,ATH_SPI_WRBUF_WATERMARK_REG,0);

    if (!SDIO_SUCCESS(status)) {
        return status;  
    }
    
       /* the watermark interrupt status constantly latches, so after we
        * set to zero, we need to clear the cause register */
    return DoPioWriteInternal(pDevice,
                              ATH_SPI_INTR_CAUSE_REG,
                              ATH_SPI_INTR_WRBUF_BELOW_WMARK);            
}

/* do DMA operation */
static SDIO_STATUS DoDMAOp(PSDHCD_DEVICE pDevice, PSDREQUEST pReq)
{
    SDIO_STATUS status;
    
    do {
            /* setup DMA size register */
        status = DoPioWriteInternal(pDevice, 
                                    ATH_SPI_DMA_SIZE_REG,
                                    pReq->DataRemaining);  
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        } 
        
        if (ATH_IS_TRANS_READ(pReq)) {
            
#ifdef TODO_FIX_FOR_BMI
                /* the following code can be removed once the host-side BMI
                 * has been modified to check for bytes available in it's polling loop 
                 * without this check BMI can blast through after the credit counter check and start
                 * reading the mailbox data */
            UINT32 bytesAvail = 0;
            int pollLimit = ATH_SPI_BYTES_AVAIL_POLL_RETRY_LIMIT;
            
            while (TRUE) {
                status = DoPioReadInternal(pDevice,
                                           ATH_SPI_RDBUF_BYTE_AVA_REG,
                                           &bytesAvail);
                                           
                if (!SDIO_SUCCESS(status)) {
                    break;    
                }         
                
                if (bytesAvail >= pReq->DataRemaining) {
                        /* we're good here */
                    break;    
                }
                
                pollLimit--;
                
                if (0 == pollLimit) {
                    status = SDIO_STATUS_ERROR;
                    DBG_PRINT(SDDBG_WARN, 
                        ("ATH SPI - POLL Limit Expired!! buffer has: %d bytes, host wants: %d bytes !!!\n",
                            bytesAvail,pReq->DataRemaining));
                    DBG_ASSERT(FALSE);
                    break;    
                }
            }
            
            if (!SDIO_SUCCESS(status)) {
                break;    
            }
            
#endif                                                                     
                /* send out the read DMA token */
            status = OUT_TOKEN_CMD_ADDR_DMA_READ(pDevice,ATH_GET_IO_ADDRESS(pReq));
        } else {
                /* send out the write DMA token */
            status = OUT_TOKEN_CMD_ADDR_DMA_WRITE(pDevice,ATH_GET_IO_ADDRESS(pReq));
        }
        
        if (!SDIO_SUCCESS(status)) {                     
           break;                                 
        }  
        
        if (IS_SLEEP_WAR_ENABLED(pDevice)) {
            if (ATH_IS_TRANS_WRITE(pReq)) {
                    /* keep track of packets written to the SPI buffer */
                pDevice->PktsInSPIWriteBuffer++;
                if (pDevice->PktsInSPIWriteBuffer > WAR_MAX_PKTS_IN_SPI_WRITE_BUFFER) {
                    DBG_PRINT(SDDBG_ERROR, ("*** ATH SPI - too many packets: %d ***\n",
                                            pDevice->PktsInSPIWriteBuffer));
                }
            }   
        }
                              
            /* call transfer state machine to handle data frames */                 
        status = HcdTransferData(pDevice, pReq);  
                
        if (SDIO_STATUS_PENDING == status) {
            DBG_ASSERT(ATH_IS_TRANS_DMA(pReq));
                /* transfer routine requires DMA to start */    
            pDevice->DMAHWTransferInProgress = TRUE;
                /* we are transfering control to the SPI hardware's DMA engine, while it is
                 * running we cannot process SPI interrupts.  SPI interrupt processing requires
                 * CMD/Data frames on the SPI bus and requires ownership of the SPI hardware.
                 * this function is called with the driver lock held to hold off the SPI interrupt
                 * handler HcdSpiInterrupt() , the handler should wake up and check for a pending
                 * DMA hardware transfer and exit.  On completion of the DMA transfer, the SPI
                 * interrupt detection will be enabled */
                 /* disable SPI interrupt detection at the hardware layer */
            EnableDisableSPIIRQHwDetect(pDevice,FALSE);
            HW_StartDMA(pDevice);         
        }
        
    } while (FALSE);
    
    return status;    
}

/* timer callback, called from a timer ISR context */
void HcdTimerCallback(PSDHCD_DEVICE pDevice, int Context)
{
    switch (Context) {
        case EXTERNAL_ACCESS_TIMER:
            {
                PSDREQUEST pReq = GET_CURRENT_REQUEST(&pDevice->Hcd);
                DBG_PRINT(SDDBG_ERROR, ("*** ATH SPI - External Host IO access timeout!***\n"));
                /* external access timmed out, complete the stalled request with error */
                if (pReq != NULL) {
                    DumpSpiInternalRegisters(pDevice);
                    DumpRequest(pReq);
                    pReq->Status = SDIO_STATUS_IO_TIMEOUT;
                    HW_QueueDeferredCompletion(pDevice);
                } 
            }
            break;
        default:
            DBG_ASSERT(FALSE);
    }    
}

/* program the write buffer watermark level to trigger an interrupt when 
 * space becomes available */
static SDIO_STATUS ProgramWriteBufferWaterMark(PSDHCD_DEVICE pDevice, PSDREQUEST pReq)
{
    UINT32      waterMarkLevel,regValue;
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    BOOL        waitInterrupt = TRUE;
    
    do {                                    
        TODO_TOGGLE_DBG_SIG(pDevice,1);
                
            /* calculate the water mark based on the number of bytes currently in the buffer 
             * and the number of bytes we need to fullfill the request, this is the level 
             * the buffer needs to drop below to trigger an interrupt */          

        if (IS_SLEEP_WAR_ENABLED(pDevice)) { 
            /* we wait for the buffer to completely drain, can't set this to zero so set it to 1 */          
            waterMarkLevel = 1;
        } else {
            waterMarkLevel = pDevice->MaxWriteBufferSpace - pReq->DataRemaining;
        }
         
        if (waterMarkLevel < pDevice->MaxWriteBufferSpace) {
                /* also... watermark level cannot be zero */
            waterMarkLevel = waterMarkLevel ? waterMarkLevel : 1;
        } else {
            DBG_ASSERT(FALSE);   
            waterMarkLevel = 1; 
        }
          
            /* update watermark trigger */                  
        status = DoPioWriteInternal(pDevice,
                                    ATH_SPI_WRBUF_WATERMARK_REG,
                                    waterMarkLevel);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }          
        
            /* re-sample SPI_STATUS and space available, the operation to update the watermark could
             * miss the window when the core is still awake, if this happens the
             * watermark level will be reached but it will not generate an interrupt
             * as INTR_CAUSE is only updated while the core clock is active, so any updates to
             * WR_BUF_WATERMARK_REG are only valid while the core is awake */  
                               
        status = DoPioReadInternal(pDevice, ATH_SPI_STATUS_REG, &regValue);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }
                      
        if ((regValue & ATH_SPI_STATUS_RTC_STATE_MASK) == ATH_SPI_STATUS_RTC_STATE_SLEEP) {
            
                /* it can take up to 60 microseconds for the sleep state to change */
            HW_UsecDelay(pDevice,60);
            
                /* re-read status and re-check the sleep state */
            status = DoPioReadInternal(pDevice, ATH_SPI_STATUS_REG, &regValue);
        
            if (!SDIO_SUCCESS(status)) {
                break;    
            } 
            
            if ((regValue & ATH_SPI_STATUS_RTC_STATE_MASK) != ATH_SPI_STATUS_RTC_STATE_SLEEP) {
                    /* core is awake or is just about to wakeup.  We can wait for an interrupt */
                break;    
            }
          
                /* re-read space available, the core went to sleep and we may never get an interrupt */
            status = DoPioReadInternal(pDevice,
                                       ATH_SPI_WRBUF_SPC_AVA_REG,
                                       &pDevice->WriteBufferSpace);
        
            if (!SDIO_SUCCESS(status)) {
                break;    
            } 

            if (IS_SLEEP_WAR_ENABLED(pDevice)) { 
            
                if (pDevice->WriteBufferSpace < pDevice->MaxWriteBufferSpace) {
                        /* need to wait for watermark interrupt */
                    break;
                }
            
                /* buffer was completely drained again */
                pDevice->PktsInSPIWriteBuffer = 0;
                
            } else { 
            
                if (pDevice->WriteBufferSpace < pReq->DataRemaining) {
                    if (pDevice->ChipType <= ATH_SPI_AR6002) {                        
                        UINT32 bytesAvail = 0;
                        UINT32 spistatus = 0;
                        
                        /* this should not happen, the core cannot go to sleep while there
                         * are bytes in the write buffer */
                        DoPioReadInternal(pDevice,
                                          ATH_SPI_WRBUF_SPC_AVA_REG,
                                          &bytesAvail);
                        DoPioReadInternal(pDevice,
                                          ATH_SPI_STATUS_REG,
                                          &spistatus);
                        DumpSpiInternalRegisters(pDevice);
                        DBG_PRINT(SDDBG_ERROR, ("*** ATH SPI (6002) - Error: WR_SPC:0x%4.4X STAT:0x%4.4X ***\n",
                                bytesAvail,spistatus));
                    }                   
                        /* go and wait for an interrupt on watermark trigger */
                    break;
                } 
            }
            
                /* no need to wait, we have buffer space to send this packet */
            waitInterrupt = FALSE;
            
                /* the core drained the write buffer after we sampled 
                 * SPC_AVAIL, we now have room and the core went to sleep.
                 * reset watermark since we won't be using it */
            status = ResetWriteBufferWaterMark(pDevice);
        
            if (!SDIO_SUCCESS(status)) {
                break;    
            }
            
            /* fall through and return SUCCESS */                                    
        } 
    
    } while (FALSE);
    
    if (SDIO_SUCCESS(status) && waitInterrupt) {        
            /*  no space, need to wait for interrupt  */
        pDevice->DMAWriteWaitingForBuffer = TRUE;
            /* wait for the buffer to empty below the water mark */
        UnmaskSPIInterrupts(pDevice, ATH_SPI_INTR_WRBUF_BELOW_WMARK);                   
        status = SDIO_STATUS_PENDING;
    }
    
    return status;
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
    PSDREQUEST            pReq;
            
    pReq = GET_CURRENT_REQUEST(pHcd);
    DBG_ASSERT(pReq != NULL);
          
        /* we must take the lock to protect against the SPI IRQ processing task/thread
         * we need to guarantee exclusive access to the SPI hw layer */
    status = CriticalSectionAcquire(&pDevice->CritSection);
    
    if (!SDIO_SUCCESS(status)) {    
        return status;    
    }
        
    do {   
        
        if (pDevice->FatalError) {
            status = SDIO_STATUS_DEVICE_ERROR;
            break;
        }
        
        if (pDevice->ShuttingDown) {
            DBG_PRINT(SDDBG_WARN, ("ATH SPI - HcdRequest returning canceled\n"));
            status = SDIO_STATUS_CANCELED;
            break;
        } 
        
#ifdef ATH_HCD_REQ_PARAM_CHECK         
        if (!(pReq->Flags & SDREQ_FLAGS_RAW)) {
                /* only raw requests */ 
            DBG_ASSERT(FALSE);
            status = SDIO_STATUS_INVALID_PARAMETER;
            break;   
        } 
#endif
        
           
#ifdef ATH_HCD_REQ_PARAM_CHECK    

        if (ATH_GET_IO_ADDRESS(pReq) > ATH_TRANS_ADDR_MASK) {
            status = SDIO_STATUS_INVALID_PARAMETER;
            DBG_ASSERT(FALSE); 
            break;         
        }
        
        if (ATH_IS_TRANS_PIO(pReq) && ATH_IS_TRANS_PIO_EXTERNAL(pReq)) {
            if (NULL == pReq->pDataBuffer) {
                status = SDIO_STATUS_INVALID_PARAMETER;
                DBG_ASSERT(FALSE); 
                break;     
            } 
            if (ATH_GET_EXT_TRANSFER_BYTES(pReq) > ATH_TRANS_EXT_MAX_PIO_BYTES) {
                status = SDIO_STATUS_INVALID_PARAMETER;
                DBG_ASSERT(FALSE);
                break;      
            }
        }
            
        if (ATH_IS_TRANS_DMA(pReq)) {
            if (NULL == pReq->pDataBuffer) {
                status = SDIO_STATUS_INVALID_PARAMETER;
                DBG_ASSERT(FALSE);
                break;   
            }
    
            if (ATH_GET_DMA_TRANSFER_BYTES(pReq) > pDevice->MaxBytesPerDMARequest) {
                DBG_ASSERT(FALSE);
                status = SDIO_STATUS_INVALID_PARAMETER;
                break;
            }
            
            if (ATH_GET_DMA_TRANSFER_BYTES(pReq) > ATH_SPI_DMA_SIZE_MAX) {
                DBG_ASSERT(FALSE);
                status = SDIO_STATUS_INVALID_PARAMETER;
                break;
            }
        }
                
#endif        
        DUMP_REQUEST(ATH_SPI_TRACE_REQUESTS,pReq);          
       
        if (ATH_IS_TRANS_PIO(pReq)) {
            
            /* handle all PIO ops */
            
            if (ATH_IS_TRANS_PIO_INTERNAL(pReq)) {
                /* internal accesses are easy */
                if (ATH_IS_TRANS_READ(pReq)) {
                    UINT32 temp;
                        /* its a read op */    
                    status =  DoPioReadInternal(pDevice,
                                                ATH_GET_IO_ADDRESS(pReq),
                                                &temp);
                    if (SDIO_SUCCESS(status)) {
                            /* return read result */
                        ATH_SET_PIO_INTERNAL_READ_RESULT(pReq, temp);
                    }                 
                } else {
                        /* its a write op */
                    status = DoPioWriteInternal(pDevice, 
                                                ATH_GET_IO_ADDRESS(pReq),
                                                ATH_GET_PIO_WRITE_VALUE(pReq));  
                }  
            } else {
                    /* external reads are more complicated */
                status = DoPioExternalAccess(pDevice,pReq);    
            }
            
                /* we're done */
            break;
        }
  
        /* if we get here, we are doing DMA transfers */
            
        DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - DMA Address:0x%X, DMA Bytes:%d\n",
                    ATH_GET_IO_ADDRESS(pReq),ATH_GET_DMA_TRANSFER_BYTES(pReq)));
               
            /* setup tracking variables */
        pReq->DataRemaining = ATH_GET_DMA_TRANSFER_BYTES(pReq); 
        pReq->pHcdContext = pReq->pDataBuffer; 
              
        DBG_DUMP_DATA_BUFFER(ATH_IS_TRANS_WRITE(pReq), pReq);
                
            /* check if we need to change byte swap logic
             * TODO : normally DMADataWidth does not change, however for testing
             * purposes we allow the width to change dynamically through a config request */
        status = ConfigureByteSwap(pDevice, pDevice->DMADataWidth);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        } 
        
        if (ATH_IS_TRANS_READ(pReq)) {           
            status = DoDMAOp(pDevice,pReq);
        } else {   
            BOOL useWriteWaterMark = FALSE;
            
                /* read buffer space register */ 
            status = DoPioReadInternal(pDevice,ATH_SPI_WRBUF_SPC_AVA_REG,&pDevice->WriteBufferSpace);
                            
            if (!SDIO_SUCCESS(status)) {
                break;    
            } 

            if (IS_SLEEP_WAR_ENABLED(pDevice)) { 
                /* the SPI controller has a sleep issue where many small packets within the SPI
                 * write buffer increases the chance of a corrupted packet when the core goes to 
                 * sleep. To mitigate this we only allow a limited number of packets (of any size) 
                 * to occupy the SPI write buffer. */
    
                if (pDevice->WriteBufferSpace >= pDevice->MaxWriteBufferSpace) {
                        /* reset packet count because SPI buffer has completely drained */
                    pDevice->PktsInSPIWriteBuffer = 0;      
                }
                
                if ((pDevice->PktsInSPIWriteBuffer >= WAR_MAX_PKTS_IN_SPI_WRITE_BUFFER) ||
                    (pDevice->WriteBufferSpace < pReq->DataRemaining)) {
                        
                    DBG_PRINT(ATH_SPI_TRACE_REQUESTS, 
                        ("ATH SPI - Not enough write buffer Space: %d bytes, need %d -or- too many packets : %d \n", 
                        pDevice->WriteBufferSpace, pReq->DataRemaining, pDevice->PktsInSPIWriteBuffer)); 
                        
                    useWriteWaterMark = TRUE;    
                }
               
            } else {
                if (pDevice->WriteBufferSpace < pReq->DataRemaining) {
                    
                    DBG_PRINT(ATH_SPI_TRACE_REQUESTS, 
                        ("ATH SPI - Not enough write buffer Space: %d bytes, need %d \n", 
                        pDevice->WriteBufferSpace, pReq->DataRemaining));
                        
                    useWriteWaterMark = TRUE;
                }  
            }
            
            if (useWriteWaterMark) {
                status = ProgramWriteBufferWaterMark(pDevice, pReq);
                                    
                if (SDIO_STATUS_PENDING == status) {
                        /* watermark was set, waiting for an interrupt */
                    break;    
                } else if (SDIO_STATUS_SUCCESS == status) {
                        /* fall through and start DMA */    
                } else {
                        /* an error occured */
                    break;    
                }
            }
               
            DBG_PRINT(ATH_SPI_TRACE_REQUESTS, 
                    ("ATH SPI - Write Buffer Space : %d bytes , issuing %d byte transfer \n", 
                    pDevice->WriteBufferSpace, pReq->DataRemaining));
            
            DBG_ASSERT(pDevice->WriteBufferSpace >= pReq->DataRemaining);
            ADJUST_WRBUF_SPACE(pDevice,pReq->DataRemaining);
            
                /* fire off the write operation */       
            status = DoDMAOp(pDevice,pReq);
                
        }
                     
    } while (FALSE);    
    
    CriticalSectionRelease(&pDevice->CritSection);
        
    if (status != SDIO_STATUS_PENDING) {  
               
        pReq->Status = status;
        
        if (IS_SDREQ_FORCE_DEFERRED_COMPLETE(pReq->Flags)) {
            DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - HcdRequest deferring completion to work item \n"));
                /* the HCD must do the indication in a separate context and return status pending */
            HW_QueueDeferredCompletion(pDevice); 
            return SDIO_STATUS_PENDING;
        } else {        
                /* complete the request */
            DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - HcdRequest Command Done, status:%d \n", status));
        }        
    }
    
    return status;
} 

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdConfig - HCD configuration handler
  Input:  pHcd - HCD object
          pConfig - configuration setting
  Output: 
  Return: 
  Notes: The bus driver guarantees that HCD config requests are serialized with HCD bus requests
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS HcdConfig(PSDHCD pHcd, PSDCONFIG pConfig) 
{
    PSDHCD_DEVICE pDevice = (PSDHCD_DEVICE)pHcd->pContext; 
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT16      command = GET_SDCONFIG_CMD(pConfig);
    
    if (command==ATH_SPI_CONFIG_SET_POWER) { 
        /* 
           special case for set power.
           It is not necessary to protect against the SPI IRQ for this case.
         */
        UINT32 temp;
        if (GET_SDCONFIG_CMD_LEN(pConfig) < sizeof(UINT32)) {
            DBG_ASSERT(FALSE);
            return SDIO_STATUS_INVALID_PARAMETER;                   
        }
        temp = *GET_SDCONFIG_CMD_DATA(PUINT32,pConfig);
        HW_PowerUpDown(pDevice->pHWDevice, temp);
        return SDIO_STATUS_SUCCESS; 
    }

    if(pDevice->ShuttingDown) {
        DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("ATH SPI - HcdConfig returning canceled\n"));
        return SDIO_STATUS_CANCELED;
    }

        /* we must take the lock to protect against the SPI IRQ processing task/thread 
         * some config requests generate internal SPI transactions which must be protected */
    status = CriticalSectionAcquire(&pDevice->CritSection);
    
    if (!SDIO_SUCCESS(status)) {    
        return status;    
    }
        
    switch (command){
        case SDCONFIG_SDIO_INT_CTRL:
            if (GET_SDCONFIG_CMD_DATA(PSDCONFIG_SDIO_INT_CTRL_DATA,pConfig)->SlotIRQEnable) {
                DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("ATH SPI HcdConfig: enable SPI IRQ\n")); 
                    /* unmask IRQ sources that the function driver is interested in */    
                UnmaskSPIInterrupts(pDevice,ATH_SPI_FUNC_DRIVER_IRQ_SOURCES);
                 
            } else { 
                DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("ATH SPI HcdConfig: disable SPI IRQ\n"));
                    /* mask all the function driver IRQ sources */
                MaskSPIInterrupts(pDevice,ATH_SPI_FUNC_DRIVER_IRQ_SOURCES);    
            }
            break;
        case SDCONFIG_SDIO_REARM_INT:
            DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("ATH SPI HcdConfig - SPI IRQ re-armed\n"));
                /* unmask IRQ sources that the function driver is interested in */    
            UnmaskSPIInterrupts(pDevice,ATH_SPI_FUNC_DRIVER_IRQ_SOURCES);
            break;
        case ATH_SPI_CONFIG_DUMP_SPI_INTERNAL_REGISTERS:
            DumpSpiInternalRegisters(pDevice);
            break;
#ifdef DEBUG        
        case SDCONFIG_GET_HCD_DEBUG:
            *GET_SDCONFIG_CMD_DATA(CT_DEBUG_LEVEL *,pConfig) = DBG_GET_DEBUG_LEVEL();                                    
            break;
        case SDCONFIG_SET_HCD_DEBUG:
            DBG_SET_DEBUG_LEVEL(*GET_SDCONFIG_CMD_DATA(CT_DEBUG_LEVEL *,pConfig));
            DBG_PRINT(SDDBG_TRACE, ("ATH SPI - HCD: HcdConfig - new debug level : %d \n",
                                    DBG_GET_DEBUG_LEVEL()));
            break;
#endif                   
        case ATH_SPI_CONFIG_SET_CLOCK:
            {
                UINT32 temp;
                if (GET_SDCONFIG_CMD_LEN(pConfig) < sizeof(UINT32)) {
                    DBG_ASSERT(FALSE);
                    status = SDIO_STATUS_INVALID_PARAMETER;
                    break;                          
                }
                temp = *GET_SDCONFIG_CMD_DATA(PUINT32,pConfig);
                HW_SetClock(pDevice, &temp); 
                pDevice->OperationalClock = temp;
            }
            break;
            
        case ATH_SPI_CONFIG_GET_CLOCK:
            if (GET_SDCONFIG_CMD_LEN(pConfig) < sizeof(UINT32)) {
                DBG_ASSERT(FALSE);
                status = SDIO_STATUS_INVALID_PARAMETER;
                break;                          
            }
            *GET_SDCONFIG_CMD_DATA(PUINT32,pConfig) = pDevice->OperationalClock;        
            break;
        
        case ATH_SPI_CONFIG_SET_DMA_DATA_WIDTH:
            if (GET_SDCONFIG_CMD_LEN(pConfig) < sizeof(UINT8)) {
                DBG_ASSERT(FALSE);
                status = SDIO_STATUS_INVALID_PARAMETER;
                break;                          
            }
            
            pDevice->DMADataWidth = *GET_SDCONFIG_CMD_DATA(PUINT8,pConfig);
            break;
        
        case ATH_SPI_CONFIG_SET_HOST_ACCESS_DATA_WIDTH:
            if (GET_SDCONFIG_CMD_LEN(pConfig) < sizeof(UINT8)) {
                DBG_ASSERT(FALSE);
                status = SDIO_STATUS_INVALID_PARAMETER;
                break;                          
            }
            
            pDevice->HostAccessDataWidth = *GET_SDCONFIG_CMD_DATA(PUINT8,pConfig);
            break;
        default:
            /* invalid request */
            DBG_PRINT(SDDBG_ERROR, ("ATH SPI - HCD: HcdConfig - bad command: 0x%X\n",
                                    command));
            status = SDIO_STATUS_INVALID_PARAMETER;
    }
    
    CriticalSectionRelease(&pDevice->CritSection);
    
    return status;
} 

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdInitialize - Initialize SPI module controller
  Input:  pDevice - device context
  Output: 
  Return: 
  Notes: I/O resources must be mapped before calling this function
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS HcdInitialize(PSDHCD_DEVICE pDevice) 
{
    SDIO_STATUS status;
    UINT32      startUpClock;
    
    DBG_PRINT(SDDBG_TRACE, ("+ATH SPI - HcdInitialize\n"));

    if (IS_SLEEP_WAR_ENABLED(pDevice)) { 
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI - SPI Sleep WAR enabled...\n"));
    } else {
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI - SPI NORMAL OPERATION \n"));    
    }
               
    do {
        status = CriticalSectionInit(&pDevice->CritSection);
    
        if (!SDIO_SUCCESS(status)) {
            break;    
        }     
        
        if (!(pDevice->SpiHWCapabilitiesFlags & (HW_SPI_FRAME_WIDTH_8 | HW_SPI_FRAME_WIDTH_16))) {
                /* We minimally require 8 and 16 bit frame widths
                 * 16-bits - Command/Addr frames
                 * 8-bits - data frames including early termination */
            status = SDIO_STATUS_UNSUPPORTED;
            DBG_ASSERT(FALSE);
            break;    
        }
        
            /* the SPI module has byte swapping capability for DMA requests and we will be turning it on
             * set the DMA buffer copy mode to not swap  */
        pDevice->HostDMABufferCopyMode = NO_BYTE_SWAP;
            /* for host access data frames, byte swaping is required */
        pDevice->HostAccessCopyMode = BYTE_SWAP;
        
        if (pDevice->SpiHWCapabilitiesFlags & HW_SPI_FRAME_WIDTH_32) {
            pDevice->HostAccessDataWidth = ATH_TRANS_DS_32;
            pDevice->DMADataWidth = ATH_TRANS_DS_32;
        } else {
            pDevice->HostAccessDataWidth = ATH_TRANS_DS_16;
            pDevice->DMADataWidth = ATH_TRANS_DS_16;
        }
        
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI HostAccessWidth:%d DMADataWidth:%d \n", 
                    pDevice->HostAccessDataWidth,  pDevice->DMADataWidth));
        
        pDevice->SpiConfigShadow = 0;
        
        startUpClock = AR6002_NORMAL_CLOCK_RATE;
        
        if (startUpClock > pDevice->OperationalClock) {
                /* the hardware layer wants a clock rate that is less than the normal rate */
            startUpClock = pDevice->OperationalClock;        
        }
                            
        if (pDevice->OperationalClock > AR6002_NORMAL_CLOCK_RATE) {
                /* when operating above the normal clock rate, we need to enable some
                 * logic to improve timing margins.
                 * *** NOTE : setting the config register must be done at our startup clock rate.
                 *     We can switch to the higher clock rate once the SPI config register is written
                 * */
            pDevice->SpiConfigShadow |= 0x1 << ATH_SPI_CONFIG_MISO_MUXSEL_MASK_SHIFT; 
        }
        
            /* set our startup clock mode, the config register must be written with the startup clock
             * rate */
        HW_SetClock(pDevice, &startUpClock); 
        
            /* start off with the I/O enable bit and byte swapping turned off */
        pDevice->SpiConfigShadow |= ATH_SPI_CONFIG_IO_ENABLE;
        
        if (ATH_TRANS_DS_32 == pDevice->DMADataWidth) {
                /* enable byte swapping logic, default is 32 bit mode */
            pDevice->SpiConfigShadow |= ATH_SPI_CONFIG_BYTE_SWAP;
            
        } else if (ATH_TRANS_DS_16 == pDevice->DMADataWidth) {
                /* enable byte swapping logic for 16 bit mode */
            pDevice->SpiConfigShadow |= ATH_SPI_CONFIG_SWAP_16BIT | ATH_SPI_CONFIG_BYTE_SWAP;      
            
        } else {
             /* for 8 bit mode, do not turn on byte swapping */      
        }
               
            /* delay a bit before our first SPI operation */       
        OSSleep(pDevice->PowerUpDelay);
                    
        status = DoPioWriteInternal(pDevice,ATH_SPI_CONFIG_REG,pDevice->SpiConfigShadow);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }

            /* SPI configuration has been written, we can safely switch to the desired operating clock rate */        
        HW_SetClock(pDevice, &pDevice->OperationalClock); 
        
            /* read the buffer space available for this chip, at reset we should have an empty buffer */    
        status = DoPioReadInternal(pDevice,ATH_SPI_WRBUF_SPC_AVA_REG,&pDevice->WriteBufferSpace);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }
        
            /* save this off for watermark level calculations */
        pDevice->MaxWriteBufferSpace = pDevice->WriteBufferSpace;
        
        if (pDevice->MaxWriteBufferSpace >= AR6003_WRITE_BUFFER_SIZE) {        
            pDevice->ChipType = ATH_SPI_AR6003;
                /* note : default is 6002 */
        }
        
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI Shadow:0x%X Write Buffer Space:%d bytes\n", 
            pDevice->SpiConfigShadow, pDevice->WriteBufferSpace));
        
        {
            UINT32 temp = 0;
            
            status = DoPioReadInternal(pDevice,ATH_SPI_CONFIG_REG,&temp);
            
            if (!SDIO_SUCCESS(status)) {
                break;    
            }
            
            if (temp == 0xFFFF) {
                DBG_PRINT(SDDBG_TRACE, ("ATH SPI Config reads back 0xFFFF! Bus is floating... \n"));    
                status = SDIO_STATUS_DEVICE_ERROR;
                break;
            }
        }
         
            /* set write buffer watermark to zero, we only set it when we need an interrupt */       
        status = ResetWriteBufferWaterMark(pDevice);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }
                               
            /* turn on error IRQ sources */
        UnmaskSPIInterrupts(pDevice, ATH_SPI_HCD_ERROR_IRQS);
        
            /* enable interrupts at the HW layer */
        EnableDisableSPIIRQHwDetect(pDevice,TRUE);
        
    } while (FALSE);
    
     
    DBG_PRINT(SDDBG_TRACE, ("-ATH SPI - HcdInitialize\n"));
    return status;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdDeinitialize - deactivate controller
  Input:  pDevice - context
  Output: 
  Return: 
  Notes:
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void HcdDeinitialize(PSDHCD_DEVICE pDevice)
{
    PSDREQUEST pReq;
    
    pDevice->ShuttingDown = TRUE;
    
    DBG_PRINT(SDDBG_TRACE, ("+ATH SPI - HcdDeinitialize\n"));
    
        /* stop any timers that could be running */
    HW_StopTimer(pDevice);
    
    pReq = GET_CURRENT_REQUEST(&pDevice->Hcd);    
        
    if (pReq != NULL) {
        DUMP_REQUEST(SDDBG_ERROR,pReq);
        pReq->Status = SDIO_STATUS_CANCELED;
        DBG_PRINT(SDDBG_WARN, ("ATH SPI - HcdDeinitialize - cancelling request.\n"));
        SDIO_HandleHcdEvent(&pDevice->Hcd, EVENT_HCD_TRANSFER_DONE);  
    }     
  
    if (pDevice->MiscFlags & MISC_FLAG_RESET_SPI_IF_SHUTDOWN) {
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI - Reseting SPI I/F..\n"));         
            /* reset spi */
        DoPioWriteInternal(pDevice,
                           ATH_SPI_CONFIG_REG,
                           ATH_SPI_CONFIG_RESET);

    } else {
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI - Host does not want SPI I/F reset\n"));     
    }
        
    if (pDevice->MiscFlags & MISC_FLAG_DUMP_STATE_ON_SHUTDOWN) {           
        DumpSpiInternalRegisters(pDevice);
    }
                     
    CriticalSectionDelete(&pDevice->CritSection);
        
    DBG_PRINT(SDDBG_TRACE, ("-ATH SPI - HcdDeinitialize\n"));
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  HcdSpiInterrupt - process interrupt from SPI interface
  Input:  pDevice - context
  Output: 
  Return: TRUE if interrupt was handled
  Notes: this is called from a normal schedulable context, on entry the SPI interrupt is disabled
         at the GPIO controller
               
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
BOOL HcdSpiInterrupt(PSDHCD_DEVICE pDevice)
{
    SDIO_STATUS status;
    BOOL   notifyFunctionIRQs = FALSE;
    BOOL   doIoCompletion = FALSE;
    UINT32 interrupts;
    UINT32 hostIrqs;
    
    DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("+ATH SPI - Int handler \n"));
   
        /* take the lock to protect against HcdConfig and HcdRequest */
    status = CriticalSectionAcquire(&pDevice->CritSection);
    
    if (!SDIO_SUCCESS(status)) {
        DBG_ASSERT(FALSE);
        return FALSE;    
    }
    
        /* check for pending DMA HW transfers */
    if (pDevice->DMAHWTransferInProgress) {
         /* The DMA engine owns the hardware. We may not have been able to stop this task/thread
         * from running, so we need to abort processing interrupts immediately.
         * The SPI interrupts would have been disabled at this point so we can pick up the pending
         * interrupts later when the DMA completion routine turns interrupts back on */  
        DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("-ATH SPI - Int handler aborting, DMAHWtransfer in progress \n"));
        /* exit immediately, we do not want to turn the interrupts back on */
        CriticalSectionRelease(&pDevice->CritSection);
        return TRUE;
    }
        
    do {
        
        status = DoPioReadInternal(pDevice,ATH_SPI_INTR_CAUSE_REG,&interrupts);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }
        
        DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("ATH SPI - INTR_CAUSE: 0x%X Enables: 0x%X Pending IRQs: 0x%X \n",
            interrupts, pDevice->SpiIntEnableShadow, interrupts & pDevice->SpiIntEnableShadow));
        
            /* only process interrupts that are enabled , the INTR_CAUSE register state is
             * always valid, whether or not an interrupt is issued is determined by the
             * INTR_ENABLE register */
        interrupts &= pDevice->SpiIntEnableShadow;
        
            /* get the HCD-owned IRQs */
        hostIrqs = interrupts & (ATH_SPI_HCD_IRQ_SOURCES | ATH_SPI_INTR_HOST_CTRL_ACCESS_DONE);
        
            /* before processing HCD-owned IRQ sources we need to Ack them, some processing 
             * may cause additional bus requests which can generate more interrupts , we want to
             * avoid clearing an interrupt that might be valid */
             
        if (hostIrqs) {
            DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("ATH SPI - Host IRQs of interest: 0x%X \n",hostIrqs));
    
                /* ack interrupts that are HCD owned */
            status = DoPioWriteInternal(pDevice,
                                        ATH_SPI_INTR_CAUSE_REG,
                                        hostIrqs);
                                         
            if (!SDIO_SUCCESS(status)) {
                break;    
            }            
        }
        
            /* process errors */ 
        if (interrupts & ATH_SPI_HCD_ERROR_IRQS) {
            PSDREQUEST pReq = GET_CURRENT_REQUEST(&pDevice->Hcd);
            REL_PRINT(SDDBG_ERROR, ("ATH SPI - ERRORs detected in INTR_CAUSE : 0x%X \n", interrupts));
                                
            if (interrupts & ATH_SPI_INTR_ADDRESS_ERROR) {
                REL_PRINT(SDDBG_ERROR, ("ATH SPI - ADDRESS ERROR \n")); 
            }
            
            if (interrupts &  ATH_SPI_INTR_WRBUF_ERROR) {
                REL_PRINT(SDDBG_ERROR, ("ATH SPI - WRBUF ERROR \n"));     
            }
            
            if (interrupts & ATH_SPI_INTR_RDBUF_ERROR) {
                REL_PRINT(SDDBG_ERROR, ("ATH SPI - RDBUF ERROR \n"));     
            }
            
            DumpSpiInternalRegisters(pDevice);
             
            if (pReq) {     
                    /* dump as much as we can to the debugger */
                DumpRequest(pReq);
                    /* complete the request */
                if (!pDevice->DMAHWTransferInProgress) {
                        /* complete the current request if it is not DMA */
                    doIoCompletion = TRUE;
                    pReq->Status = SDIO_STATUS_DEVICE_ERROR;
                }
            } else {
                DBG_PRINT(SDDBG_ERROR, ("*** NO Pending Request ****\n"));      
            }
            
                /* ack error interrupts */
            DoPioWriteInternal(pDevice,
                               ATH_SPI_INTR_CAUSE_REG,
                               interrupts & ATH_SPI_HCD_ERROR_IRQS);
                      
            interrupts &= ~ATH_SPI_HCD_ERROR_IRQS;
            pDevice->FatalError = TRUE;
                /* mask all interrupts */
            DoPioWriteInternal(pDevice,ATH_SPI_INTR_ENABLE_REG, 0);
            break;
            
        }
                       
        if (interrupts & ATH_SPI_INTR_HOST_CTRL_ACCESS_DONE) {
            
            PSDREQUEST pReq = GET_CURRENT_REQUEST(&pDevice->Hcd);
            
            DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("ATH SPI - Host Access Done \n"));
                            
            if ((pReq != NULL) && (pDevice->ExternalIOPending)) {
                    /* stop external access timer */
                HW_StopTimer(pDevice);  
                pDevice->ExternalIOPending = FALSE;                
                if (ATH_IS_TRANS_READ(pReq)) {
                    DBG_ASSERT((interrupts & ATH_SPI_INTR_HOST_CTRL_RD_DONE) != 0);
                } else {
                    DBG_ASSERT((interrupts & ATH_SPI_INTR_HOST_CTRL_WR_DONE) != 0);    
                }
                
                if (ATH_IS_TRANS_READ(pReq)) {
                        /* complete the read */
                    status = HandleExternalReadDone(pDevice,pReq);    
                } else {
                        /* write success */
                    status = SDIO_STATUS_SUCCESS;    
                }
                    /* complete the request, it completed or failed */
                pReq->Status = status;
                    /* set flag, the completion has to be done outside the lock */
                doIoCompletion = TRUE;
                                
            } else {
                DBG_ASSERT(FALSE);
            }
                        
            interrupts &= ~ATH_SPI_INTR_HOST_CTRL_ACCESS_DONE;
            
                /* mask host access interrupts */
            MaskSPIInterrupts(pDevice,(ATH_SPI_INTR_HOST_CTRL_RD_DONE | ATH_SPI_INTR_HOST_CTRL_WR_DONE));            
        }
        
        if (interrupts & ATH_SPI_INTR_WRBUF_BELOW_WMARK) {
                        
            interrupts &= ~ATH_SPI_INTR_WRBUF_BELOW_WMARK;
                
                /* disable interrupt */
            MaskSPIInterrupts(pDevice, ATH_SPI_INTR_WRBUF_BELOW_WMARK);
            
                /* check to see if we need to start any pending DMA waiting on more buffer space */      
            if (pDevice->DMAWriteWaitingForBuffer) {
                PSDREQUEST pReq = GET_CURRENT_REQUEST(&pDevice->Hcd);
                               
                    /* re-cache the buffer space available register */    
                status = DoPioReadInternal(pDevice,ATH_SPI_WRBUF_SPC_AVA_REG,&pDevice->WriteBufferSpace);
                
                if (!SDIO_SUCCESS(status)) {
                    break;    
                }
                        
                DBG_PRINT(ATH_SPI_TRACE_SPI_INT,
                        ("ATH SPI INT : DMA write pending .... got space: %d req:0x%X \n", 
                        pDevice->WriteBufferSpace, (UINT32)pReq));  
                                                           
                if (pReq != NULL) {  
                    BOOL gotSpace = FALSE; 

                    if (IS_SLEEP_WAR_ENABLED(pDevice)) { 

                        if (pDevice->WriteBufferSpace >= pDevice->MaxWriteBufferSpace) {
                            pDevice->PktsInSPIWriteBuffer = 0;
                            gotSpace = TRUE;  
                        }
                        
                    } else { 
                                   
                        if (pDevice->WriteBufferSpace >= pReq->DataRemaining) {                        
                            gotSpace = TRUE;    
                        }
                        
                    }
                    
                    if (gotSpace) {   
                        
                        pDevice->DMAWriteWaitingForBuffer = FALSE;                          
                            /* reset water mark */
                        status = ResetWriteBufferWaterMark(pDevice);
                    
                        if (!SDIO_SUCCESS(status)) {
                            break;    
                        }
                      
                        ADJUST_WRBUF_SPACE(pDevice,pReq->DataRemaining);
                            /* start the pending DMA write operation */
                        status = DoDMAOp(pDevice, pReq);
                        
                        if (status != SDIO_STATUS_PENDING) {
                                /* complete the request, it completed or failed */
                            pReq->Status = status;
                                /* set flag, the completion has to be done outside the lock */
                            doIoCompletion = TRUE;
                        } else {
                                /* DMA operation is in progress */
                                /* do not process any more interrupts because the DMA controller
                                 * now owns the SPI hardware */
                            break;    
                        }
                    } else {
                        UINT32 regValue = 0;
                        
                        DoPioReadInternal(pDevice,ATH_SPI_WRBUF_WATERMARK_REG,&regValue);
                        
                        if (IS_SLEEP_WAR_ENABLED(pDevice)) { 
                            DBG_PRINT(SDDBG_TRACE, 
                                ("ERROR - ATH SPI WRBUF_BELOW_INTERRUPT : Got WR buffer space:%d Waiting for:%d (watermark:%d) \n", 
                                        pDevice->WriteBufferSpace, pDevice->MaxWriteBufferSpace,
                                        regValue));
                        } else {
                            DBG_PRINT(SDDBG_TRACE, 
                                ("ERROR - ATH SPI WRBUF_BELOW_INTERRUPT : Got WR buffer space:%d Need:%d (watermark:%d)  \n", 
                                        pDevice->WriteBufferSpace, 
                                        pReq->DataRemaining,
                                        regValue));
                        }                                  
                        //DBG_ASSERT(FALSE);
                        //DumpSpiInternalRegisters(pDevice);
                        //DumpRequest(pReq);
                        UnmaskSPIInterrupts(pDevice, ATH_SPI_INTR_WRBUF_BELOW_WMARK);
                    }
                } else {
                    DBG_ASSERT(FALSE);    
                }
            }
        }
               
            /* check to see if any other sources remain, these belong to the function driver */
        if (interrupts & ATH_SPI_FUNC_DRIVER_IRQ_SOURCES) {
                /* If there are pending interrupts of interest to the function driver 
                 * we disable all function-driver IRQ sources while the function driver processes
                 * the interrupt event. This allows the host driver to continue processing
                 * host access done and write buffer water mark interrupts.
                 * 
                 * The function driver will re-enable sources when it is done */
            DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("ATH SPI - Function IRQs of interest: 0x%X \n",
                interrupts & ATH_SPI_FUNC_DRIVER_IRQ_SOURCES));
            MaskSPIInterrupts(pDevice,ATH_SPI_FUNC_DRIVER_IRQ_SOURCES);    
            notifyFunctionIRQs = TRUE;    
        }
        
    } while (FALSE);


    if (!pDevice->DMAHWTransferInProgress) {
        /* re-enable SPI interrupt detection at the hardware layer, note we always enable the
         * SPI interrupt so that the HCD can get host-access-done interrupts.
         * Note also that we do not enable interrupts if a DMA transfer is in progress */
        EnableDisableSPIIRQHwDetect(pDevice,TRUE);
    }
    
    CriticalSectionRelease(&pDevice->CritSection);
    
    if (doIoCompletion) {
        SDIO_HandleHcdEvent(&pDevice->Hcd, EVENT_HCD_TRANSFER_DONE); 
    }     
                    
    if (notifyFunctionIRQs) {
        SDIO_HandleHcdEvent(&pDevice->Hcd, EVENT_HCD_SDIO_IRQ_PENDING);
    } 
        
    DBG_PRINT(ATH_SPI_TRACE_SPI_INT, ("-ATH SPI - Int handler\n"));
    
    if (!SDIO_SUCCESS(status)) {
        DBG_ASSERT(FALSE);
        return FALSE;        
    }
    
    return TRUE;
}

#ifdef DEBUG
typedef struct _SPI_REG_DESCRIPTION_LOOKUP {
    UINT16  Address;       
    PTEXT   pDescription;    
}SPI_REG_DESCRIPTION_LOOKUP, *PSPI_REG_DESCRIPTION_LOOKUP;

SPI_REG_DESCRIPTION_LOOKUP SpiRegTable[] = {
    {ATH_SPI_DMA_SIZE_REG,      "DMA_SIZE"},
    {ATH_SPI_CONFIG_REG,        "SPI_CONFIG"},
    {ATH_SPI_WRBUF_SPC_AVA_REG, "WRBUF_SPC_AVA"},
    {ATH_SPI_WRBUF_WRPTR_REG ,  "WRBUF_WRPTR"},
    {ATH_SPI_WRBUF_RDPTR_REG,   "WRBUF_RDPTR"},
    {ATH_SPI_INTR_CAUSE_REG,    "INTR_CAUSE"},
    {ATH_SPI_STATUS_REG,        "SPI_STATUS"},
    {ATH_SPI_RDBUF_BYTE_AVA_REG,"RDBUF_BYTES_AVA"},
    {ATH_SPI_RDBUF_WRPTR_REG,   "RDBUF_WRPTR"},
    {ATH_SPI_RDBUF_RDPTR_REG,   "RDBUF_RDPTR"},
    {ATH_SPI_HOST_CTRL_BYTE_SIZE_REG, "HOST_CTRL_BYTE_SIZE"},
    {ATH_SPI_HOST_CTRL_CONFIG_REG,  "HOST_CTRL_CONFIG"},
    {ATH_SPI_RDBUF_WATERMARK_REG, "RDBUF_WATERMARK"},
    {ATH_SPI_WRBUF_WATERMARK_REG,"WR_BUF_WATERMARK"},   
    {ATH_SPI_INTR_ENABLE_REG,"INTR_ENABLE"},   
   
        /* zero terminate list */
    {0,  NULL},
};

static void DumpInternalRegister(PSDHCD_DEVICE pDevice, UINT16 Reg)
{
    UINT32 regValue;
    PSPI_REG_DESCRIPTION_LOOKUP pLookup = SpiRegTable;
    
    while (pLookup->pDescription != NULL) {
        if (pLookup->Address == Reg) {
            break;    
        }
        pLookup++;    
    }
    
    if (SDIO_SUCCESS(DoPioReadInternal(pDevice, 
                                       Reg,
                                       &regValue))) {
                                        
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI - Register Dump : 0x%4.4X (%s) = 0x%4.4X \n", 
            Reg,(pLookup->pDescription == NULL) ? "UNKNOWN" : pLookup->pDescription, regValue)); 
                                              
    } else {
    
        DBG_PRINT(SDDBG_TRACE, ("ATH SPI - Failed to read REG:0x%X (%s) \n", Reg, 
            (pLookup->pDescription == NULL) ? "UNKNOWN" : pLookup->pDescription));         
    }    
    
}

/* dump some SPI internal registers */
static void DumpSpiInternalRegisters(PSDHCD_DEVICE pDevice)
{   
    DumpInternalRegister(pDevice, ATH_SPI_STATUS_REG);
    DumpInternalRegister(pDevice, ATH_SPI_CONFIG_REG);  
    DumpInternalRegister(pDevice, ATH_SPI_WRBUF_SPC_AVA_REG);
    DumpInternalRegister(pDevice, ATH_SPI_WRBUF_WRPTR_REG);
    DumpInternalRegister(pDevice, ATH_SPI_WRBUF_RDPTR_REG);
    DumpInternalRegister(pDevice, ATH_SPI_INTR_CAUSE_REG);
    DumpInternalRegister(pDevice, ATH_SPI_INTR_ENABLE_REG);
    DumpInternalRegister(pDevice, ATH_SPI_RDBUF_BYTE_AVA_REG);
    DumpInternalRegister(pDevice, ATH_SPI_RDBUF_WRPTR_REG);
    DumpInternalRegister(pDevice, ATH_SPI_RDBUF_RDPTR_REG);
    DumpInternalRegister(pDevice, ATH_SPI_HOST_CTRL_BYTE_SIZE_REG);
    DumpInternalRegister(pDevice, ATH_SPI_HOST_CTRL_CONFIG_REG);
    DumpInternalRegister(pDevice, ATH_SPI_WRBUF_WATERMARK_REG);   
    DumpInternalRegister(pDevice, ATH_SPI_RDBUF_WATERMARK_REG);
}
#endif
