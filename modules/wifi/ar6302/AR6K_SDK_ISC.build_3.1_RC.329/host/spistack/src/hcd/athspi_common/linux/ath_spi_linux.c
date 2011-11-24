//------------------------------------------------------------------------------
// <copyright file="ath_spi_linux.c" company="Atheros">
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
#include "ath_spi_hcd.h"


/* map the request buffer to DMA
 * this function builds the scatter gather list which the hardware layer can use
 * to translate into DMA descriptor entries.  The hardware-specific layer is required to
 * perform cache/bus sync operations to make the buffer dma-coherent */
int HcdMapCurrentRequestBuffer(PSDHCD_DEVICE pDevice, struct scatterlist *pSGList, int MaxEntries)
{
    int validEntries = 0;
    int length;
    UINT32 vaddress = (UINT32)pDevice->pCurrentBuffer;
    int byteCount = pDevice->CurrentTransferLength;
    
    DBG_ASSERT(pDevice->Hcd.pDmaDescription != NULL);
            
    do {
                
        if (pDevice->HostDMABufferCopyMode != NO_BYTE_SWAP) {
                /* can't do direct if we have to byte swap in software, let the hardware layer 
                 * punt this to common buffer DMA */
            break;    
        }
                
        if (vaddress & pDevice->Hcd.pDmaDescription->AddressAlignment) {
            /* illegal address bits, hardware cannot handle the address range */
            break;                 
        }
                 
        if (byteCount & pDevice->Hcd.pDmaDescription->LengthAlignment) {
            /* illegal length alignment, hardware cannot handle the length multiple */
            break;    
        }
        
        DBG_PRINT(ATH_SPI_TRACE_DATA, ("ATH SPI, building scatter table (%s): pVaddr:0x%X length: %d \n",
            pDevice->CurrentTransferDirRx ? "RX":"TX",(UINT32)vaddress,pDevice->CurrentTransferLength));
            
            /* assemble scatter gather list */   
        for (validEntries = 0 ; (validEntries < MaxEntries) && (byteCount > 0); validEntries++) {
                /* set up page */
            pSGList[validEntries].page =  virt_to_page(vaddress);
                /* validate */
            if (!VALID_PAGE(pSGList[validEntries].page)) {
                validEntries = 0;
                break;    
            }   
            
                /* setup offset into page */
            pSGList[validEntries].offset = 
                            virt_to_phys((void *)vaddress) - page_to_phys(pSGList[validEntries].page);
                /* setup length for this descriptor */
                /* push length to the end of the page */
            length = PAGE_SIZE - pSGList[validEntries].offset;
                /* limit it to the host controller capability */
            length = min(length,(int)pDevice->Hcd.pDmaDescription->MaxBytesPerDescriptor);
                /* limit it to the current buffer count */
            length = min(length,byteCount);
                /* set the scatter entry length */ 
            pSGList[validEntries].length = length;               
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,20)
                /* under linux 2.4.20, the address field must be filled in for the HCD
                 * in order to call consistent_sync() to flush the caches, this API
                 * requires a virtual address range, in 2.6 the .address field was
                 * removed, however dma_map_sg/dma_unmap_sg were added to perform the cache sync */
            pSGList[validEntries].address = (void *)vaddress;
#endif
            DBG_PRINT(ATH_SPI_TRACE_DATA, ("  Entry:%d, Page Struct: 0x%X Offset :0x%X, Length : %d \n",
                validEntries,
                (UINT32)pSGList[validEntries].page,
                pSGList[validEntries].offset,
                pSGList[validEntries].length));
            
                /* advance address */
            vaddress += length;
            byteCount -= length;
        }
        
        if ((validEntries > 0) && (byteCount > 0)) {
            DBG_PRINT(SDDBG_WARN, 
                          ("  ATH SPI - request buffer 0x%X does not fit, remaining bytes:%d, valid entries:%d \n",
                          (UINT32)pDevice->pCurrentBuffer,byteCount, validEntries));    
            validEntries = 0;
        }
                   
    } while (FALSE);
 
    return validEntries;       
}

