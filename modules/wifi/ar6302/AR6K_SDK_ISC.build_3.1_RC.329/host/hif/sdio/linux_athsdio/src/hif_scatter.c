//------------------------------------------------------------------------------
// <copyright file="hif_scatter.c" company="Atheros">
//    Copyright (c) 2009 Atheros Corporation.  All rights reserved.
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
// HIF scatter request implementation
//
// Author(s): ="Atheros"
//==============================================================================
#include "hif_internal.h"

#include <asm/uaccess.h>
#include <asm/dma.h>
#include <asm/page.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#include <linux/scatterlist.h>
#endif

extern OS_CRITICALSECTION lock;

static HIF_SCATTER_REQ *AllocScatterReq(HIF_DEVICE *device) 
{
    DL_LIST *pItem; 
    
    CriticalSectionAcquire(&lock);
    
    pItem = DL_ListRemoveItemFromHead(&device->ScatterReqHead);
    
    CriticalSectionRelease(&lock);
    
    if (pItem != NULL) {
        return A_CONTAINING_STRUCT(pItem, HIF_SCATTER_REQ, ListLink);
    }
    
    return NULL;   
}

static void FreeScatterReq(HIF_DEVICE *device, HIF_SCATTER_REQ *pReq)
{
    CriticalSectionAcquire(&lock);
    
    DL_ListInsertTail(&device->ScatterReqHead, &pReq->ListLink);
    
    CriticalSectionRelease(&lock);
    
}

    /* ASYNC completion callback */
static void HifReadWriteScatterCompletion(SDREQUEST *sdrequest)
{
    HIF_SCATTER_REQ *pReq = sdrequest->pCompleteContext;
    HIF_DEVICE      *device;
    
    AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("+HIF Scatter Completion \n"));
                            
    if (!SDIO_SUCCESS(sdrequest->Status)) {
        pReq->CompletionStatus = A_ERROR;    
    } else {
        pReq->CompletionStatus = A_OK;         
    }
    
    device = GET_HIFDEVICE_SR(pReq);
    
        /* complete the request */
    pReq->CompletionRoutine(pReq);
    AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("-HIF Scatter Completion \n"));
        
}

static A_STATUS SetupBusRequestForDMA(HIF_DEVICE *device, SDREQUEST *sdrequest, HIF_SCATTER_REQ *pReq)
{
    A_STATUS                    status = A_OK;
    SDDMA_DESCRIPTOR            *pSg;
    HIF_SCATTER_DMA_BOUNCE_INFO *pBounceInfo;
    HIF_SCATTER_DMA_REAL_INFO   *pRealSGInfo;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)    
    int                         i;
#endif    
        
    switch (pReq->ScatterMethod) {
        case HIF_SCATTER_DMA_BOUNCE : 
            { 
                void *pBuffer;
                pBounceInfo = GET_DMA_BOUNCE_INFO_SR(pReq);          
                    /* setup the single SG entry for the bounce buffer */                
                pSg = &pBounceInfo->SGList[0];
                pBuffer = pBounceInfo->pBounceBuffer + pBounceInfo->AlignmentOffset;
                    /* setup SDIO bus request to use direct buffer DMA */
                sdrequest->pDataBuffer = pSg;   
                sdrequest->Flags |= SDREQ_FLAGS_DATA_DMA;
                sdrequest->DescriptorCount = 1;
                
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)                                  

                sg_init_table(pSg, 1);
                sg_set_buf(pSg, pBuffer, pReq->TotalLength);                                         

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
                    /* for older kernels, this is a manual setup */
                A_MEMZERO(pSg, sizeof(*pSg));
                pSg->page = virt_to_page(pBuffer);
                pSg->offset = offset_in_page(pBuffer);                                       
                pSg->length = pReq->TotalLength;
                                
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20)  
                /* TODO */
                status = A_ERROR;
#else
#error "SG support undefined "            
#endif            
            }
            break;
            
        case HIF_SCATTER_DMA_REAL :
            pRealSGInfo = GET_DMA_REAL_INFO_SR(pReq);
            pSg = &(pRealSGInfo->SDSGList[0]);
                /* setup SDIO request to use direct buffer DMA */
            sdrequest->pDataBuffer = pSg;
            sdrequest->Flags |= SDREQ_FLAGS_DATA_DMA;
            sdrequest->DescriptorCount = pReq->ValidScatterEntries;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)      
                            
                /* init table */
            sg_init_table(pSg, pReq->ValidScatterEntries);                 
                /* assemble SG list */   
            for (i = 0 ; i < pReq->ValidScatterEntries ; i++, pSg++) {
                    /* setup each sg entry */
                sg_set_buf(pSg, pReq->ScatterList[i].pBuffer, pReq->ScatterList[i].Length);
            }  

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
                /* for older kernels, this is a manual setup */
            for (i = 0 ; i < pReq->ValidScatterEntries ; i++, pSg++) {
                A_MEMZERO(pSg, sizeof(*pSg));
                pSg->page = virt_to_page(pReq->ScatterList[i].pBuffer);
                pSg->offset = offset_in_page(pReq->ScatterList[i].pBuffer); 
                pSg->length = pReq->ScatterList[i].Length;
            }
            
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20)  
                /* TODO */
            status = A_ERROR;
#else
#error "SG support undefined "            
#endif 
            break;
         default:
            A_ASSERT(FALSE);
            return A_EINVAL; 
    }
           
    return status;
}
        
static A_STATUS HifReadWriteScatter(HIF_DEVICE *device, HIF_SCATTER_REQ *pReq)
{
   
    A_STATUS        status = A_EINVAL;   
    SDREQUEST       *sdrequest;
    A_UINT8         rw;
    A_UINT8         mode;
    A_UINT8         funcNo;
    A_UINT8         opcode;
    A_UINT16        count;
    SDIO_STATUS     sdiostatus;
    A_UINT32        request = pReq->Request;
    
    do {
        
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("HIF Scatter : %d Scatter Entries: %d\n", 
                            pReq->TotalLength, pReq->ValidScatterEntries));
        
        if (pReq->TotalLength > MAX_SCATTER_REQ_TRANSFER_SIZE) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid length: %d \n", pReq->TotalLength));
            break;          
        }
        
        if (pReq->TotalLength == 0) {
            A_ASSERT(FALSE);
            break;    
        }
        
            /* get the sd bus request associated with this scatter request */         
        sdrequest = GET_SDREQUEST_SR(pReq);
        
        if (request & HIF_SYNCHRONOUS) {
            sdrequest->Flags = SDREQ_FLAGS_RESP_SDIO_R5 | SDREQ_FLAGS_DATA_TRANS;
            sdrequest->pCompleteContext = NULL;
            sdrequest->pCompletion = NULL;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,("  Synchronous \n"));
        } else if (request & HIF_ASYNCHRONOUS) {
            sdrequest->Flags = SDREQ_FLAGS_RESP_SDIO_R5 | SDREQ_FLAGS_DATA_TRANS |
                               SDREQ_FLAGS_TRANS_ASYNC;  
            sdrequest->pCompleteContext = pReq;
            sdrequest->pCompletion = HifReadWriteScatterCompletion;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,("  Asynchronous \n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid execution mode: 0x%08x\n", request));
            break;
        }

        if (!(request & HIF_EXTENDED_IO)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid command type: 0x%08x\n", request));
            break;
        }

        sdrequest->Command = CMD53;
            
        if (!(request & HIF_BLOCK_BASIS)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid data mode: 0x%08x\n", request));
            break;   
        }
            /* only block-mode commands */
        mode = CMD53_BLOCK_BASIS;
        sdrequest->BlockLen = HIF_MBOX_BLOCK_SIZE;
        sdrequest->BlockCount = pReq->TotalLength / HIF_MBOX_BLOCK_SIZE;
        count = sdrequest->BlockCount;
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                        ("  Block mode (BlockLen: %d, BlockCount: %d)\n",
                        sdrequest->BlockLen, sdrequest->BlockCount));
             
        if (request & HIF_WRITE) {
            rw = CMD53_WRITE;
            sdrequest->Flags |= SDREQ_FLAGS_DATA_WRITE;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("  Direction: Write\n"));
        } else if (request & HIF_READ) {
            rw = CMD53_READ;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("  Direction: Read\n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid direction: 0x%08x\n", request));
            break;
        }

        if (request & HIF_FIXED_ADDRESS) {
            opcode = CMD53_FIXED_ADDRESS;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("  Address mode: Fixed\n"));
        } else if (request & HIF_INCREMENTAL_ADDRESS) {
            opcode = CMD53_INCR_ADDRESS;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("  Address mode: Incremental\n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid address mode: 0x%08x\n", request));
            break;
        }
        
        
        funcNo = SDDEVICE_GET_SDIO_FUNCNO(device->handle);
       
        SDIO_SET_CMD53_ARG(sdrequest->Argument, rw, funcNo,
                           mode, opcode, pReq->Address, count);
        
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("HIF Scatter : SDIO CMD53 card address: 0x%X blocks: %d\n", 
                            pReq->Address, count));
                            
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("HIF Scatter : SDIO CMD53 , request flags:0x%X arg:0x%X\n", 
                            sdrequest->Flags, sdrequest->Argument));
                            
        status = SetupBusRequestForDMA(device, sdrequest, pReq);
        
        if (A_FAILED(status)){
            break;    
        }

        if (sdrequest->pDataBuffer == NULL) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            (" sdrequest->pDataBuffer is NULL!!!\n"));
            status = A_ERROR;
            // A_ASSERT(FALSE);
            break;    
        }
                    
            /* Send the command out */
        sdiostatus = SDDEVICE_CALL_REQUEST_FUNC(device->handle, sdrequest);
        
        if (!SDIO_SUCCESS(sdiostatus)) {
            status = A_ERROR;
            break;
        }
            
        status = A_OK;
       
    } while (FALSE);

    if (A_FAILED(status) && (request & HIF_ASYNCHRONOUS)) {
        pReq->CompletionStatus = status;
        pReq->CompletionRoutine(pReq);
        status = A_OK;
    }
        
    return status;  
}

static void DetermineScatterMethod(HIF_DEVICE *device)
{
    SDDMA_DESCRIPTION  *pDescription;
    A_UINT32            temp;
        
    pDescription = SDGET_DMA_DESCRIPTION(device->handle);
    
    do {       
            /* set default */
        device->ScatterMethod = HIF_SCATTER_NONE;
        
        if (pDescription == NULL) {
                /* no DMA at ALL */
            break;    
        }
                
        if (pDescription->MaxBytesPerDescriptor <= MAX_SCATTER_REQ_TRANSFER_SIZE) {
                /* scatter hardware cannot handle our largest single-descriptor transfer size */
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                ("HIF: ** scatter hardware allows only %d bytes per transfer descriptor ! \n", 
                    pDescription->MaxBytesPerDescriptor));
            break;    
        }              
                
            /* we meet the minimum requirements for using DMA bounce buffers */
        device->ScatterMethod = HIF_SCATTER_DMA_BOUNCE;
        
        if (pDescription->MaxDescriptors < MAX_SCATTER_ENTRIES_PER_REQ) {
                /* host controller driver does not provide enough scatter resources to fullfill
                 * the largest request we want to handle.  Punt this to using a bounce buffer */
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                ("HIF: ** scatter hardware allows only %d descriptors ! \n", 
                    pDescription->MaxDescriptors));
            break;    
        }

            /* for data buffers check for minimal 4-byte address alignment support */
        temp = ~0x3;                
            /* test for illegal bits */
        if (temp & pDescription->AddressAlignment) {
                /* there are illegal bits here, punt to common buffer */
            AR_DEBUG_PRINTF(ATH_DEBUG_INIT,
                ("HIF: ** scatter hardware forces Address Alignment : 0x%X  \n", 
                    pDescription->AddressAlignment));
            break;    
        }
        
            /* data transfer lengths are aligned to the block size */
        temp = ~(HIF_MBOX_BLOCK_SIZE - 1);
            /* illegal bits */       
        if (temp & pDescription->LengthAlignment) {
                /* illegal bits detected */
            AR_DEBUG_PRINTF(ATH_DEBUG_INIT,
                ("HIF: ** scatter hardware forces Length Alignment : 0x%X  \n", 
                    pDescription->LengthAlignment));    
            break;        
        }
        
            /* all checks pass, hardware can handle TRUE Scatter DMA */
        device->ScatterMethod = HIF_SCATTER_DMA_REAL;
        
    } while (FALSE);
    
    AR_DEBUG_PRINTF(ATH_DEBUG_INIT,("HIF: Scatter Method : %d \n",device->ScatterMethod));        
}

static void FreeBounceBuffer(HIF_DEVICE *device, HIF_SCATTER_DMA_BOUNCE_INFO *pBounceInfo)
{    
    if (pBounceInfo->pBounceBuffer == NULL) {
        return;    
    }    
    kfree(pBounceInfo->pBounceBuffer);
    pBounceInfo->pBounceBuffer = NULL;
    pBounceInfo->BufferSize = 0;
}
                                                     
static A_STATUS AllocateDMABounceBuffer(HIF_DEVICE *device, HIF_SCATTER_DMA_BOUNCE_INFO *pBounceInfo)
{
    A_STATUS            status = A_OK;
    A_UINT32            temp;
    SDDMA_DESCRIPTION  *pDescription;
        
    pDescription = SDGET_DMA_DESCRIPTION(device->handle);
    
    do {
        
        pBounceInfo->BufferSize = MAX_SCATTER_REQ_TRANSFER_SIZE + 2*(A_GET_CACHE_LINE_BYTES());
                
            /* allocate the bounce buffer */                 
        pBounceInfo->pBounceBuffer  = (A_UINT8 *)kmalloc(pBounceInfo->BufferSize,
                                                         GFP_KERNEL | GFP_DMA | GFP_ATOMIC);
                                         
        if (NULL == pBounceInfo->pBounceBuffer) {            
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,("HIF : *** unable to allocate bounce buffer \n"));      
            status = A_NO_MEMORY;
            break;  
        }   

        temp = (A_UINT32)A_ALIGN_TO_CACHE_LINE(pBounceInfo->pBounceBuffer);
            /* check bounce buffer address alignment */
        if ((temp & pDescription->AddressAlignment) == 0) {
                /* no illegal address bits */
            break;    
        }
                
            /* free the buffer we just allocated, we need to allocate a larger buffer */
        FreeBounceBuffer(device, pBounceInfo);
            /* increase buffer size by padding it by the required alignment */                  
        pBounceInfo->BufferSize = MAX_SCATTER_REQ_TRANSFER_SIZE + 2*(A_GET_CACHE_LINE_BYTES()) + 
                                        (pDescription->AddressAlignment + 1);   
                            
        pBounceInfo->pBounceBuffer  = (A_UINT8 *)kmalloc(pBounceInfo->BufferSize,
                                                         GFP_KERNEL | GFP_DMA | GFP_ATOMIC);
                                                         
        if (NULL == pBounceInfo->pBounceBuffer) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,("HIF : *** unable to allocate bounce buffer \n"));
            status = A_NO_MEMORY;
            break;  
        }   
        
            /* figure out required alignment */
        temp = (A_UINT32)A_ALIGN_TO_CACHE_LINE(pBounceInfo->pBounceBuffer);
        temp += (pDescription->AddressAlignment + 1);
        temp &= ~pDescription->AddressAlignment;
        
        pBounceInfo->AlignmentOffset = temp - (A_UINT32)pBounceInfo->pBounceBuffer;
        
        AR_DEBUG_PRINTF(ATH_DEBUG_WARN,("HIF : Bounce Buffer (0x%X) will apply alignment: %d \n",
                (A_UINT32)pBounceInfo->pBounceBuffer, pBounceInfo->AlignmentOffset));
                
    } while (FALSE);
    
    return status;
}

    /* setup scatter DMA resources for each scatter request */
static A_STATUS SetupScatterResource(HIF_DEVICE *device, HIF_SCATTER_REQ *pReq)
{
    A_STATUS status = A_OK;
    void     *pDMAInfo = NULL;
      
    switch (device->ScatterMethod) {
        case HIF_SCATTER_DMA_REAL :
                /* just allocate memory for the scatter list */
            pDMAInfo = A_MALLOC(sizeof(HIF_SCATTER_DMA_REAL_INFO));
            if (NULL == pDMAInfo) {
                status = A_NO_MEMORY;
                break;
            }           
            
            A_MEMZERO(pDMAInfo, sizeof(HIF_SCATTER_DMA_REAL_INFO));           
            pReq->ScatterMethod = HIF_SCATTER_DMA_REAL;
            
            break;
        case HIF_SCATTER_DMA_BOUNCE :               
            {
                HIF_SCATTER_DMA_BOUNCE_INFO *pBounceInfo;    
                
                    /* allocate the management structure */
                pBounceInfo = (HIF_SCATTER_DMA_BOUNCE_INFO *)A_MALLOC(sizeof(HIF_SCATTER_DMA_BOUNCE_INFO));
                if (NULL == pBounceInfo) {
                    status = A_NO_MEMORY;
                    break;    
                }
                
                A_MEMZERO(pBounceInfo, sizeof(HIF_SCATTER_DMA_BOUNCE_INFO));
               
                    /* allocate a bounce buffer for the request */ 
                status = AllocateDMABounceBuffer(device, pBounceInfo);                 
                if (A_FAILED(status)) {
                    A_FREE(pBounceInfo);
                    break;
                }    
                                 
                pDMAInfo = pBounceInfo;
                pReq->ScatterMethod = HIF_SCATTER_DMA_BOUNCE;
                pReq->pScatterBounceBuffer = pBounceInfo->pBounceBuffer + 
                                                        pBounceInfo->AlignmentOffset;     
            }
            break;
        default:
            break;  
    }
    
    if (pDMAInfo != NULL) {
        SET_DMA_INFO_SR(pReq,pDMAInfo);    
    }
    
    return status;
}

    /* cleanup any scatter DMA resources allocated for the request */
void CleanupScatterResource(HIF_DEVICE *device, HIF_SCATTER_REQ *pReq)
{
   
    if (GET_DMA_INFO_SR(pReq) == NULL) {
        return; 
    }
    
    switch (pReq->ScatterMethod) {
         case HIF_SCATTER_DMA_BOUNCE :   
            {
                FreeBounceBuffer(device, GET_DMA_BOUNCE_INFO_SR(pReq));
                A_FREE(GET_DMA_BOUNCE_INFO_SR(pReq));
            }
            break;
         case HIF_SCATTER_DMA_REAL :
                /* just free the the allocation */
            A_FREE(GET_DMA_INFO_SR(pReq));
            break;
         default:
            break;  
    }
    
    SET_DMA_INFO_SR(pReq,NULL);
    
}

A_STATUS SetupHIFScatterSupport(HIF_DEVICE *device, HIF_DEVICE_SCATTER_SUPPORT_INFO *pInfo)
{
    A_STATUS            status = A_ERROR;   
    int                 maxTransferSizePerScatter = MAX_SCATTER_REQ_TRANSFER_SIZE;
    int                 size, i;
    HIF_SCATTER_REQ     *pReq;
    SDREQUEST           *sdrequest;
        
    do {
        
        DetermineScatterMethod(device);
    
        if (device->ScatterMethod == HIF_SCATTER_NONE) {
                /* no scatter support */
            break;    
        }
        
        AR_DEBUG_PRINTF(ATH_DEBUG_INIT,("HIF : Cache Line Size: %d bytes \n",A_GET_CACHE_LINE_BYTES())); 
        
        size = sizeof(HIF_SCATTER_REQ) + 
                    (MAX_SCATTER_ENTRIES_PER_REQ - 1) * (sizeof(HIF_SCATTER_ITEM));
       
        for (i = 0; i < MAX_SCATTER_REQUESTS; i++) {    
            
            pReq = A_MALLOC(size);
            if (NULL == pReq) {
                break;    
            }
            A_MEMZERO(pReq, size);
            
                /* save the device instance */
            SET_DEVICE_INFO_SR(pReq, device);
            
                /* allocate a bus request for this scatter request */
            sdrequest = SDDeviceAllocRequest(device->handle);
            if (NULL == sdrequest) {
                A_FREE(pReq);
                break;    
            }
                /* store bus request into private area */
            SET_SDREQUEST_SR(pReq,sdrequest);
            
            status = SetupScatterResource(device,pReq);
            if (A_FAILED(status)) {
                SDDeviceFreeRequest(device->handle, sdrequest);
                A_FREE(pReq); 
                break;       
            }         
               
                /* add it to the free pool */
            FreeScatterReq(device, pReq);
        }
        
        if (i != MAX_SCATTER_REQUESTS) {
            status = A_NO_MEMORY;
            break;    
        }
        
            /* set function pointers */
        pInfo->pAllocateReqFunc = AllocScatterReq;
        pInfo->pFreeReqFunc = FreeScatterReq;
        pInfo->pReadWriteScatterFunc = HifReadWriteScatter;   
        pInfo->MaxScatterEntries = MAX_SCATTER_ENTRIES_PER_REQ;
        pInfo->MaxTransferSizePerScatterReq = maxTransferSizePerScatter;
     
        status = A_OK;
        
    } while (FALSE);
    
    if (A_FAILED(status)) {
        CleanupHIFScatterResources(device);   
    }
    
    return status;
}

void CleanupHIFScatterResources(HIF_DEVICE *device)
{
    HIF_SCATTER_REQ     *pReq;
    
    while (1) {
        
        pReq = AllocScatterReq(device);
                
        if (NULL == pReq) {
            break;    
        }   
        
        if (GET_SDREQUEST_SR(pReq) != NULL) {
                /* free bus request */
            SDDeviceFreeRequest(device->handle, GET_SDREQUEST_SR(pReq));
            SET_SDREQUEST_SR(pReq, NULL);     
        }
        
        CleanupScatterResource(device,pReq);
        
        A_FREE(pReq);
        
    }
}

