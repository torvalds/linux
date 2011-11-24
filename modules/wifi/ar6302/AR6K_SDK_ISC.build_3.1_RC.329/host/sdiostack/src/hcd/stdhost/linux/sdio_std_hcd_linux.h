/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_std_hcd_linux.h

@abstract: include file for linux dependent code
 
@notice: Copyright (c), 2005 Atheros Communications, Inc.


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
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef __SDIO_STD_HCD_LINUX_H___
#define __SDIO_STD_HCD_LINUX_H___

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <asm/irq.h>

#define SDHCD_MAX_DEVICE_NAME        64

/* Advance DMA parameters */
#define SDHCD_MAX_ADMA_DESCRIPTOR    32
#define SDHCD_ADMA_DESCRIPTOR_SIZE   (SDHCD_MAX_ADMA_DESCRIPTOR * sizeof(SDHCD_SGDMA_DESCRIPTOR))
#define SDHCD_MAX_ADMA_LENGTH        0x8000      /* up to 32KB per descriptor */    
#define SDHCD_ADMA_ADDRESS_MASK      0xFFFFE000  /* 4KB boundaries */
#define SDHCD_ADMA_ALIGNMENT         0xFFF       /* illegal alignment bits*/
#define SDHCD_ADMA_LENGTH_ALIGNMENT  0x0         /* any length up to the max */

/* simple DMA */
#define SDHCD_MAX_SDMA_DESCRIPTOR    1
#define SDHCD_MAX_SDMA_LENGTH        0x80000     /* up to 512KB for a single descriptor*/    
#define SDHCD_SDMA_ADDRESS_MASK      0xFFFFFFFF  /* any 32 bit address */
#define SDHCD_SDMA_ALIGNMENT         0x0         /* any 32 bit address */
#define SDHCD_SDMA_LENGTH_ALIGNMENT  0x0         /* any length up to the max */

#define HCD_COMMAND_MIN_POLLING_CLOCK 5000000

/* debounce delay for slot */
#define SD_SLOT_DEBOUNCE_MS  1000

/* mapped memory address */
typedef struct _SDHCD_MEMORY {
    ULONG Raw;      /* start of address range */
    ULONG Length;   /* length of range */
    PVOID pMapped;  /* the mapped address */
}SDHCD_MEMORY, *PSDHCD_MEMORY;

typedef struct _SDHCD_OS_SPECIFIC {
    SDHCD_MEMORY Address;               /* memory address of this device */ 
    spinlock_t   RegAccessLock;         /* use to protect registers when needed */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    struct work_struct iocomplete_work; /* work item definitions */
    struct work_struct carddetect_work; /* work item definintions */
    struct work_struct sdioirq_work;    /* work item definintions */
#else
    struct delayed_work iocomplete_work; /* work item definitions */ 
    struct delayed_work carddetect_work; /* work item definintions */
    struct delayed_work sdioirq_work;    /* work item definintions */
#endif
    spinlock_t   Lock;                  /* general purpose lock against the ISR */
    DMA_ADDRESS  hDmaBuffer;            /* handle for data buffer */
    PUINT8       pDmaBuffer;            /* virtual address of DMA command buffer */
    PSDDMA_DESCRIPTOR pDmaList;         /* in use scatter-gather list */
    UINT         SGcount;               /* count of in-use scatter gather list */
    UINT         SlotNumber;            /* the STD-host defined slot number assigned to this instance */
    DMA_ADDRESS  hDmaCommonBuffer;      /* handle for DMA common buffer */
    PUINT8       pDmaCommonBuffer;      /* virtual address of DMA common buffer */
    UINT32       DmaCommonAllocSize;    /* size of the allocated common buffer */
/* everything below this line is used by the implementation that uses this STD core */
    UINT16        InitMask;             /* implementation specific portion init mask */
    UINT32        ImpSpecific0;         /* implementation specific storage */           
    UINT32        ImpSpecific1;         /* implementation specific storage */ 
} SDHCD_OS_SPECIFIC, *PSDHCD_OS_SPECIFIC;


#define WORK_ITEM_IO_COMPLETE  0
#define WORK_ITEM_CARD_DETECT  1
#define WORK_ITEM_SDIO_IRQ     2
 
#define READ_HOST_REG32(pHcInstance, OFFSET)  \
    _READ_DWORD_REG((((UINT32)((pHcInstance)->pRegs))) + (OFFSET))
#define WRITE_HOST_REG32(pHcInstance, OFFSET, VALUE) \
    _WRITE_DWORD_REG((((UINT32)((pHcInstance)->pRegs))) + (OFFSET),(VALUE))
#define READ_HOST_REG16(pHcInstance, OFFSET)  \
    _READ_WORD_REG((((UINT32)((pHcInstance)->pRegs))) + (OFFSET))
#define WRITE_HOST_REG16(pHcInstance, OFFSET, VALUE) \
    _WRITE_WORD_REG((((UINT32)((pHcInstance)->pRegs))) + (OFFSET),(VALUE))
#define READ_HOST_REG8(pHcInstance, OFFSET)  \
    _READ_BYTE_REG((((UINT32)((pHcInstance)->pRegs))) + (OFFSET))
#define WRITE_HOST_REG8(pHcInstance, OFFSET, VALUE) \
    _WRITE_BYTE_REG((((UINT32)((pHcInstance)->pRegs))) + (OFFSET),(VALUE))

#define TRACE_SIGNAL_DATA_WRITE(pHcInstance, on) 
#define TRACE_SIGNAL_DATA_READ(pHcInstance, on) 
#define TRACE_SIGNAL_DATA_ISR(pHcInstance, on) 
#define TRACE_SIGNAL_DATA_IOCOMP(pHcInstance, on) 
#define TRACE_SIGNAL_DATA_TIMEOUT(pHcInstance, on) 



#define IS_HCD_ADMA(pHc) (((pHc)->Hcd.pDmaDescription != NULL) && \
                           ((pHc)->Hcd.pDmaDescription->Flags & SDDMA_DESCRIPTION_FLAG_SGDMA))

#define IS_HCD_SDMA(pHc) (((pHc)->Hcd.pDmaDescription != NULL) &&   \
                           (((pHc)->Hcd.pDmaDescription->Flags &   \
                             (SDDMA_DESCRIPTION_FLAG_SGDMA | SDDMA_DESCRIPTION_FLAG_DMA)) == \
                             SDDMA_DESCRIPTION_FLAG_DMA))

#endif 
