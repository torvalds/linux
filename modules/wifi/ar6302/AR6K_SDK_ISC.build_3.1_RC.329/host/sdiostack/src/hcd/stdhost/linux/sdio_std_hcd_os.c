/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_std_hcd_os.c

@abstract: Generic Linux implementation for the Standard SDIO Host Controller Driver

#notes: 
 
@notice: Copyright (c), 2006 Atheros Communications, Inc.


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

/* debug level for this module*/
#define DBG_DECLARE 4;
#include "../../../include/ctsystem.h"
#include "../sdio_std_hcd.h"
#include "sdio_std_hcd_linux_lib.h"
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#define ATH_INIT_WORK(_t, _f, _c)      INIT_WORK((_t), (void (*)(void *))(_f), (_c));
#else
#define ATH_INIT_WORK(_t, _f, _c)      INIT_DELAYED_WORK((_t), (_f));
#endif

static void hcd_iocomplete_wqueue_handler(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
void *context); 
#else
struct work_struct *work);
#endif

static void hcd_carddetect_wqueue_handler(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
void *context); 
#else
struct work_struct *work);   
#endif

static void hcd_sdioirq_wqueue_handler(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
void *context); 
#else
struct work_struct *work);   
#endif

static SDIO_STATUS SetupDmaBuffers(PSDHCD_INSTANCE pHcInstance);
static SDIO_STATUS SetupDmaCommonBuffer(PSDHCD_INSTANCE pHcInstance);
static void DeinitializeStdHcdInstance(PSDHCD_INSTANCE pHcInstance);

#ifdef TEST_MISSED_IRQ
static void IRQTimeout(unsigned long Context);
static void SetIRQTimer(PSDHCD_INSTANCE pHcInstance, UINT32 TimeOut);
static void CancelIRQTimer(PSDHCD_INSTANCE pHcInstance);
#define IRQ_TIMEOUT_MS  4000
#endif

/* debug print parameter */
module_param(debuglevel, int, 0644);
MODULE_PARM_DESC(debuglevel, "debuglevel 0-7, controls debug prints");

    /* defaults for all std hosts, various attributes will be cleared based
     * on values from the capabilities register */
#define DEFAULT_ATTRIBUTES (SDHCD_ATTRIB_BUS_1BIT      | \
                            SDHCD_ATTRIB_BUS_4BIT      | \
                            SDHCD_ATTRIB_MULTI_BLK_IRQ | \
                            SDHCD_ATTRIB_AUTO_CMD12    | \
                            SDHCD_ATTRIB_POWER_SWITCH  | \
                            SDHCD_ATTRIB_BUS_MMC8BIT   | \
                            SDHCD_ATTRIB_SD_HIGH_SPEED | \
                            SDHCD_ATTRIB_MMC_HIGH_SPEED)
                            
static UINT32 hcdattributes = DEFAULT_ATTRIBUTES;
module_param(hcdattributes, int, 0644);
MODULE_PARM_DESC(hcdattributes, "STD Host Attributes");
static INT BaseClock = 0;
module_param(BaseClock, int, 0444);
MODULE_PARM_DESC(BaseClock, "BaseClock Hz when not present in configuration");
static UINT32 timeout = HOST_REG_TIMEOUT_CONTROL_DEFAULT;
module_param(timeout, int, 0644);
MODULE_PARM_DESC(timeout, "STD Host data timeout control");
static UINT32 ClockSpinLimit = HCD_COMMAND_MIN_POLLING_CLOCK;
module_param(ClockSpinLimit, int, 0644);
MODULE_PARM_DESC(ClockSpinLimit, "STD Host command clock spin time");

static UINT32 IdleBusClockRate = 0;  /* currently disabled by default */
module_param(IdleBusClockRate, int, 0644);
MODULE_PARM_DESC(IdleBusClockRate, "STD Host idle clock rate when in 4-bit mode");

static UINT32 CardDetectDebounce = SD_SLOT_DEBOUNCE_MS;  /* default to 1 second */
module_param(CardDetectDebounce, int, 0644);
MODULE_PARM_DESC(CardDetectDebounce, "STD Host card detect debounce interval (MS)");

static UINT32 Async4bitIRQ = 0; 
module_param(Async4bitIRQ, int, 0644);
MODULE_PARM_DESC(Async4bitIRQ, "Allow detection of interrupts in 4-bit mode without a clock");

typedef struct _STDHCD_DEV {
    SDLIST       CoreList;           /* the list of core contexts */
    spinlock_t   CoreListLock;       /* protection for the list */  
}STDHCD_DEV, *PSTDHCD_DEV;

STDHCD_DEV StdDevices;

void  InitStdHostLib()
{
    ZERO_POBJECT(&StdDevices);
    SDLIST_INIT(&StdDevices.CoreList);
    spin_lock_init(&StdDevices.CoreListLock); 
}

void  DeinitStdHostLib()
{
    
    
}

PSDHCD_CORE_CONTEXT CreateStdHostCore(PVOID pBusContext)
{
    PSDHCD_CORE_CONTEXT pStdCore = NULL;
    unsigned long        flags;
    
    do {
        pStdCore = KernelAlloc(sizeof(SDHCD_CORE_CONTEXT));  
        if (NULL == pStdCore) {
            break;    
        }
        ZERO_POBJECT(pStdCore);
        SDLIST_INIT(&pStdCore->SlotList);
        spin_lock_init(&pStdCore->SlotListLock); 
        pStdCore->pBusContext = pBusContext;
        
            /* add it */
        spin_lock_irqsave(&StdDevices.CoreListLock,flags);               
        SDListInsertHead(&StdDevices.CoreList, &pStdCore->List); 
        spin_unlock_irqrestore(&StdDevices.CoreListLock,flags);     
    } while (FALSE);
    
    return pStdCore;
}

void DeleteStdHostCore(PSDHCD_CORE_CONTEXT pStdCore)
{   
    unsigned long flags;
        
    spin_lock_irqsave(&StdDevices.CoreListLock,flags);   
        /* remove */            
    SDListRemove(&pStdCore->List);
    spin_unlock_irqrestore(&StdDevices.CoreListLock,flags);     
    
    KernelFree(pStdCore);
}

/* find the std core associated with this bus context */
PSDHCD_CORE_CONTEXT GetStdHostCore(PVOID pBusContext)
{
    PSDLIST             pListItem;
    PSDHCD_CORE_CONTEXT pStdCore = NULL;
    unsigned long       flags;
        
    spin_lock_irqsave(&StdDevices.CoreListLock,flags);    
    
    do {
        if (SDLIST_IS_EMPTY(&StdDevices.CoreList)) {
            break;    
        }        
          
        SDITERATE_OVER_LIST(&StdDevices.CoreList, pListItem) {
            pStdCore = CONTAINING_STRUCT(pListItem, SDHCD_CORE_CONTEXT, List);
            if (pStdCore->pBusContext == pBusContext) {
                    /* found it */
                break;   
            } 
            pStdCore = NULL; 
        }
        
    } while (FALSE);
    
    spin_unlock_irqrestore(&StdDevices.CoreListLock,flags);    
    return pStdCore;
}

/* create a standard host memory instance */
PSDHCD_INSTANCE CreateStdHcdInstance(POS_DEVICE pOSDevice, 
                                     UINT       SlotNumber, 
                                     PTEXT      pName)
{
    PSDHCD_INSTANCE pHcInstance = NULL;
    BOOL            success = FALSE;
    
    do {
            /* allocate an instance for this new device */
        pHcInstance =  (PSDHCD_INSTANCE)KernelAlloc(sizeof(SDHCD_INSTANCE));
        
        if (pHcInstance == NULL) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host: CreateStdHcdInstance - no memory for instance\n"));
            break;
        }
        
        ZERO_POBJECT(pHcInstance);
        SET_SDIO_STACK_VERSION(&pHcInstance->Hcd);
        
        pHcInstance->OsSpecific.SlotNumber = SlotNumber;
        spin_lock_init(&pHcInstance->OsSpecific.Lock);
        spin_lock_init(&pHcInstance->OsSpecific.RegAccessLock);
            /* initialize work items */
        ATH_INIT_WORK(&(pHcInstance->OsSpecific.iocomplete_work), hcd_iocomplete_wqueue_handler, pHcInstance);
        ATH_INIT_WORK(&(pHcInstance->OsSpecific.carddetect_work), hcd_carddetect_wqueue_handler, pHcInstance);
        ATH_INIT_WORK(&(pHcInstance->OsSpecific.sdioirq_work), hcd_sdioirq_wqueue_handler, pHcInstance);
            /* allocate space for the name */ 
        pHcInstance->Hcd.pName = (PTEXT)KernelAlloc(strlen(pName)+1);
        if (NULL == pHcInstance->Hcd.pName) {
            break;    
        }        
        strcpy(pHcInstance->Hcd.pName,pName);
            /* set OS device for DMA allocations and mapping */
        pHcInstance->Hcd.pDevice = pOSDevice;
        pHcInstance->Hcd.Attributes = hcdattributes;
        pHcInstance->Hcd.MaxBlocksPerTrans = SDIO_SD_MAX_BLOCKS;
        pHcInstance->Hcd.pContext = pHcInstance;
        pHcInstance->Hcd.pRequest = HcdRequest;
        pHcInstance->Hcd.pConfigure = HcdConfig;
        pHcInstance->Hcd.pModule = THIS_MODULE;
        pHcInstance->BaseClock = BaseClock;
        pHcInstance->TimeOut = timeout;
        pHcInstance->ClockSpinLimit = ClockSpinLimit;
        pHcInstance->IdleBusClockRate = IdleBusClockRate;
        pHcInstance->CardDetectDebounceMS = CardDetectDebounce;
        if (Async4bitIRQ) {
                /* set special bus clock rate for async 4-bit mode */
            pHcInstance->IdleBusClockRate = ASYNC_4_BIT_IRQ_CLOCK_RATE;    
                /* enable 1bit IRQ detection mode */
            pHcInstance->Idle1BitIRQ = TRUE;
        }
        success = TRUE;
    } while (FALSE);
    
    if (!success && (pHcInstance != NULL)) {
        DeleteStdHcdInstance(pHcInstance);
    }
    
    return pHcInstance;
}

/*
 * AddStdHcdInstance - add the std host controller instance
*/
SDIO_STATUS AddStdHcdInstance(PSDHCD_CORE_CONTEXT pStdCore, 
                              PSDHCD_INSTANCE pHcInstance, 
                              UINT Flags,
                              PPLAT_OVERRIDE_CALLBACK pCallBack,                                
                              SDDMA_DESCRIPTION       *pSDMADescrip,
                              SDDMA_DESCRIPTION       *pADMADescrip)
{
    
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    unsigned long flags;
        
    do { 
                
        if (!SDIO_SUCCESS((status = HcdInitialize(pHcInstance)))) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO STD HOST: StartStdHcdInstance - failed to init HW, status =%d\n",status));  
            break;
        }    
            /* mark that the hardware was initialized */
        pHcInstance->InitStateMask |= SDHC_HW_INIT;
                  
        pHcInstance->Hcd.pDmaDescription = NULL;
        
        if (!(Flags & START_HCD_FLAGS_FORCE_NO_DMA)) {
                /* check DMA parameters discovered by HcdInitialize */
            if (!(Flags & START_HCD_FLAGS_FORCE_SDMA) && 
                  (pHcInstance->Caps & HOST_REG_CAPABILITIES_ADMA) &&
                  (pADMADescrip != NULL)) {
                DBG_PRINT(SDDBG_TRACE, ("SDIO STD HOST: StartStdHcdInstance - using Advanced DMA\n"));
                    /* copy the DMA description for advanced DMA */
                memcpy(&pHcInstance->DmaDescription, pADMADescrip, sizeof(SDDMA_DESCRIPTION));
                    /* set DMA description */
                pHcInstance->Hcd.pDmaDescription = &pHcInstance->DmaDescription;
            } else if ((pHcInstance->Caps & HOST_REG_CAPABILITIES_DMA) &&
                       (pSDMADescrip != NULL)) {
                DBG_PRINT(SDDBG_TRACE, ("SDIO STD HOST: StartStdHcdInstance - using Simple DMA\n"));
                    /* copy the DMA description for advanced DMA */
                memcpy(&pHcInstance->DmaDescription, pSDMADescrip, sizeof(SDDMA_DESCRIPTION));
                    /* set DMA description */
                pHcInstance->Hcd.pDmaDescription = &pHcInstance->DmaDescription;
            }
        }
       
        if (IS_HCD_ADMA(pHcInstance)) {
                /* setup DMA buffers for scatter-gather descriptor tables used in advanced DMA */
            status = SetupDmaBuffers(pHcInstance);
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host : StartStdHcdInstance - failed to setup DMA buffer\n"));
                break;
            }
        } 
                
        if ((IS_HCD_ADMA(pHcInstance) || IS_HCD_SDMA(pHcInstance)) && (Flags & START_HCD_FLAGS_ALLOW_CBDMA)) {            
            status = SetupDmaCommonBuffer(pHcInstance);          
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host : StartStdHcdInstance - failed to setup common buffer DMA \n"));
                break;
            }            
        }  
        
        if (pCallBack != NULL) {
                /* allow the platform to override any settings */
            status = pCallBack(pHcInstance);
            if (!SDIO_SUCCESS(status)) {
                break;    
            }
        }             
            /* add this instance to our list, we will start the HCDs later */
            /* protect the devicelist */
        spin_lock_irqsave(&pStdCore->SlotListLock,flags);               
        SDListInsertHead(&pStdCore->SlotList, &pHcInstance->List); 
        pStdCore->SlotCount++;
        spin_unlock_irqrestore(&pStdCore->SlotListLock,flags);     
        
    } while (FALSE);
        
    
    if (SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host - ready! \n"));
    } else {
            /* undo everything */
        DeinitializeStdHcdInstance(pHcInstance);
    }
    
    return status;  
}

INT GetCurrentHcdInstanceCount(PSDHCD_CORE_CONTEXT pStdCore)
{
    return pStdCore->SlotCount;   
}

static void DeinitializeStdHcdInstance(PSDHCD_INSTANCE pHcInstance)
{
        /* wait for any of our work items to run */
    flush_scheduled_work();  

#ifdef TEST_MISSED_IRQ    
    CancelIRQTimer(pHcInstance);
#endif
    
    if (pHcInstance->InitStateMask & SDHC_REGISTERED) {
        SDIO_UnregisterHostController(&pHcInstance->Hcd);
        pHcInstance->InitStateMask &= ~SDHC_REGISTERED;
    }      
    
    if (pHcInstance->InitStateMask & SDHC_HW_INIT) {
        HcdDeinitialize(pHcInstance);
        pHcInstance->InitStateMask &= ~SDHC_HW_INIT;
    }        
        /* free any DMA resources */
    if (pHcInstance->OsSpecific.hDmaBuffer != (DMA_ADDRESS)NULL) {
        dma_free_coherent(pHcInstance->Hcd.pDevice, 
                          SDHCD_ADMA_DESCRIPTOR_SIZE,
                          pHcInstance->OsSpecific.pDmaBuffer,
                          pHcInstance->OsSpecific.hDmaBuffer);
        pHcInstance->OsSpecific.hDmaBuffer = (DMA_ADDRESS)NULL;
        pHcInstance->OsSpecific.pDmaBuffer = NULL;
    }
    
    if (pHcInstance->OsSpecific.hDmaCommonBuffer != (DMA_ADDRESS)NULL) {
        dma_free_coherent(pHcInstance->Hcd.pDevice, 
                          pHcInstance->OsSpecific.DmaCommonAllocSize,
                          pHcInstance->OsSpecific.pDmaCommonBuffer,
                          pHcInstance->OsSpecific.hDmaCommonBuffer);
        pHcInstance->OsSpecific.hDmaCommonBuffer = (DMA_ADDRESS)NULL;
        pHcInstance->OsSpecific.pDmaCommonBuffer = NULL;
    }          
    
}
void DeleteStdHcdInstance(PSDHCD_INSTANCE pHcInstance)
{
    if (pHcInstance->Hcd.pName != NULL) {
        KernelFree(pHcInstance->Hcd.pName);
        pHcInstance->Hcd.pName = NULL;
    }            
        
    KernelFree(pHcInstance);    
}


/*
 * RemoveStdHcdInstance - remove the hcd instance
*/
PSDHCD_INSTANCE RemoveStdHcdInstance(PSDHCD_CORE_CONTEXT pStdCore)
{
    PSDHCD_INSTANCE pHcInstanceToRemove = NULL;
    PSDLIST pListItem;
    unsigned long flags;
        
    DBG_PRINT(SDDBG_TRACE, ("+SDIO STD HCD: RemoveStdHcdInstance\n"));
    
        /* protect the devicelist */
    spin_lock_irqsave(&pStdCore->SlotListLock,flags);
    
    do {        
        pListItem = SDListRemoveItemFromHead(&pStdCore->SlotList);
        
        if (NULL == pListItem) {
            break;    
        }
    
        pHcInstanceToRemove = CONTAINING_STRUCT(pListItem,SDHCD_INSTANCE,List);
        
        pStdCore->SlotCount--;
    
    } while (FALSE);
    
    spin_unlock_irqrestore(&pStdCore->SlotListLock,flags);
    
    if (pHcInstanceToRemove != NULL) {
        DBG_PRINT(SDDBG_TRACE, (" SDIO STD HCD: Deinitializing 0x%X \n",(UINT)pHcInstanceToRemove));
        DeinitializeStdHcdInstance(pHcInstanceToRemove);   
    }           
           
    DBG_PRINT(SDDBG_TRACE, ("-SDIO STD HCD: RemoveStdHcdInstance\n"));
    
        /* return the instance we found */
    return pHcInstanceToRemove;
}

/*
 * SetupDmaBuffers - allocate required DMA buffers
 * 
*/
static SDIO_STATUS SetupDmaBuffers(PSDHCD_INSTANCE pHcInstance)
{
    if (pHcInstance->Hcd.pDmaDescription == NULL) {
        DBG_ASSERT(FALSE);
        return SDIO_STATUS_NO_RESOURCES;
    }    
    if (pHcInstance->Hcd.pDmaDescription->Flags & SDDMA_DESCRIPTION_FLAG_SGDMA) {
        /* we are only supporting scatter-gather DMA in this driver */
        /* allocate a DMA buffer large enough for the command buffers and the data buffers */
        pHcInstance->OsSpecific.pDmaBuffer =  dma_alloc_coherent(pHcInstance->Hcd.pDevice, 
                                                  SDHCD_ADMA_DESCRIPTOR_SIZE, 
                                                  &pHcInstance->OsSpecific.hDmaBuffer, 
                                                  GFP_DMA);
        DBG_PRINT(SDDBG_TRACE, ("SDIO STD Host : SetupDmaBuffers - pDmaBuffer: 0x%X, hDmaBuffer: 0x%X\n",
                                (UINT)pHcInstance->OsSpecific.pDmaBuffer , (UINT)pHcInstance->OsSpecific.hDmaBuffer ));
        if (pHcInstance->OsSpecific.pDmaBuffer == NULL) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host : SetupDmaBuffers - unable to get DMA buffer\n"));
            return SDIO_STATUS_NO_RESOURCES;
        }        
        return SDIO_STATUS_SUCCESS;
    } else {
        DBG_PRINT(SDDBG_TRACE, ("SDIO STD Host : SetupDmaBuffers - invalid DMA type\n"));
        return SDIO_STATUS_INVALID_PARAMETER;
    }
    
}

static SDIO_STATUS SetupDmaCommonBuffer(PSDHCD_INSTANCE pHcInstance)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT32      alignPage = 0;
    UINT32      temp;
    UINT32      distance;
   
    DBG_PRINT(SDDBG_TRACE, ("SDIO STD Host : SetupDmaCommonBuffer - requested length : %d bytes \n",
         pHcInstance->CommonBufferLength));  

    do {
        
        if (IS_HCD_ADMA(pHcInstance)) {
            if (pHcInstance->CommonBufferLength > SDHCD_MAX_ADMA_LENGTH) {
                status = SDIO_STATUS_INVALID_PARAMETER;
                break;  
            }
            alignPage = SDHCD_ADMA_BUFFER_PAGE_ALIGN;
                /* allocate a buffer size + 1 page to align the buffer + 1 page to provide space for
                 * ADMA descriptors (must be page aligned) */
            pHcInstance->OsSpecific.DmaCommonAllocSize = pHcInstance->CommonBufferLength + 2 * alignPage;      
                /* part of the common buffer (1 page) is used for SG descriptors which offsets the user-data area
                 * by a ADMA page size */
            pHcInstance->CommonBufferUserDataOffset = alignPage;    
        } else {
            if (pHcInstance->CommonBufferLength > SDHCD_MAX_SDMA_LENGTH) {
                status = SDIO_STATUS_INVALID_PARAMETER;
                break;  
            }
            pHcInstance->OsSpecific.DmaCommonAllocSize = pHcInstance->CommonBufferLength;       
            pHcInstance->CommonBufferUserDataOffset = 0;      
        }
           
        pHcInstance->OsSpecific.pDmaCommonBuffer = dma_alloc_coherent(pHcInstance->Hcd.pDevice, 
                                                                      pHcInstance->OsSpecific.DmaCommonAllocSize, 
                                                                      &pHcInstance->OsSpecific.hDmaCommonBuffer, 
                                                                      GFP_DMA);    
        if (pHcInstance->OsSpecific.pDmaCommonBuffer == NULL) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host : SetupDmaCommonBuffer - unable to get DMA buffer\n"));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }        
        
        DBG_PRINT(SDDBG_TRACE, ("SDIO STD Host : SetupDmaCommonBuffer - pDmaCommonBuffer: 0x%X, hDmaCommonBuffer: 0x%X\n",
                                (UINT)pHcInstance->OsSpecific.pDmaCommonBuffer , 
                                (UINT)pHcInstance->OsSpecific.hDmaCommonBuffer));
                                
        pHcInstance->pCommonBuffer = pHcInstance->OsSpecific.pDmaCommonBuffer;
        pHcInstance->CommonBufferPhys = (UINT32)pHcInstance->OsSpecific.hDmaCommonBuffer;
        
        if (IS_HCD_ADMA(pHcInstance)) {
                /* setup common buffer to be page aligned */
            pHcInstance->pCommonBuffer += (alignPage - 1);    
            pHcInstance->pCommonBuffer = (PUINT8)((UINT32)pHcInstance->pCommonBuffer & ~(alignPage - 1));
            pHcInstance->CommonBufferPhys += (alignPage - 1);
            pHcInstance->CommonBufferPhys &= ~(alignPage - 1);
                /* done */
            break;    
        }
     
        /* complete SDMA common buffer setup */
        
        temp = pHcInstance->CommonBufferPhys;
            /* round up buffer address to nearest 512K boundary */
        temp += SDHC_SDMA_512K_BOUNDARY_LENGTH - 1;
        temp &= ~(SDHC_SDMA_512K_BOUNDARY_LENGTH - 1);
            /* calc distance to next 512K boundary, this could be zero */
        distance = (temp - pHcInstance->CommonBufferPhys);
       
        DBG_PRINT(SDDBG_TRACE, 
            ("SDIO STD Host : SetupDmaCommonBuffer (SDMA) - Next:0x%X  distance: %d\n", temp, distance));
        
        /* the SDMA (as per spec) will stop at an address boundary specified in the HOST_REG_BLOCK_SIZE
         * register.  This behavior cannot be turned off.  For common buffer DMA, the common layer will
         * set this boundary to 512K.  In order to avoid taking an interrupt to "continue" the DMA operation,
         * we can adjust the buffer address so that it cannot cross a 512K boundary.  In order to do this
         * we need to allocate twice the buffer size to give us room to adjust.
         */  
                       
        if ((distance == 0) || (distance >= pHcInstance->CommonBufferLength)) {
            /* if the buffer is already at a 512K boundary OR the distance to the next 512K is greater
             * than or equal the buffer length, we have nothing to worry about */     
             break;   
        }
        
        /* the distance to the next 512K boundary is less than our common buffer length */ 
        
            /* free the buffer we just allocated, we need to allocate a larger buffer */
        dma_free_coherent(pHcInstance->Hcd.pDevice, 
                          pHcInstance->OsSpecific.DmaCommonAllocSize,
                          pHcInstance->OsSpecific.pDmaCommonBuffer,
                          pHcInstance->OsSpecific.hDmaCommonBuffer);
                          
        pHcInstance->OsSpecific.hDmaCommonBuffer = (DMA_ADDRESS)NULL;
        pHcInstance->OsSpecific.pDmaCommonBuffer = NULL;    
            /* allocate twice as much now to provide room to adjust the address */             
        pHcInstance->OsSpecific.DmaCommonAllocSize =  pHcInstance->OsSpecific.DmaCommonAllocSize * 2;   
        pHcInstance->OsSpecific.pDmaCommonBuffer = 
                                         dma_alloc_coherent(pHcInstance->Hcd.pDevice, 
                                                            pHcInstance->OsSpecific.DmaCommonAllocSize, 
                                                            &pHcInstance->OsSpecific.hDmaCommonBuffer, 
                                                            GFP_DMA);  
                                                                
        if (pHcInstance->OsSpecific.pDmaCommonBuffer == NULL) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host : SetupDmaCommonBuffer - unable to get DMA buffer\n"));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }   
        
        pHcInstance->pCommonBuffer = pHcInstance->OsSpecific.pDmaCommonBuffer;
        pHcInstance->CommonBufferPhys = (UINT32)pHcInstance->OsSpecific.hDmaCommonBuffer;

            /* re-calc boundaries */
        temp = pHcInstance->CommonBufferPhys;
        temp += SDHC_SDMA_512K_BOUNDARY_LENGTH - 1;
        temp &= ~(SDHC_SDMA_512K_BOUNDARY_LENGTH - 1);
        distance = (temp - pHcInstance->CommonBufferPhys);
        
        if ((distance == 0) || (distance >= pHcInstance->CommonBufferLength)) {
                /* newly allocated buffer is okay */
            break;    
        }
            /* push the buffer past the 512K boundary */
        pHcInstance->pCommonBuffer += distance;
        pHcInstance->CommonBufferPhys += distance;
        DBG_PRINT(SDDBG_TRACE, 
            ("SDIO STD Host : SetupDmaCommonBuffer (SDMA) - adjusting %d bytes \n",distance));
        
    } while (FALSE);                        

    if (SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO STD Host : SetupDmaCommonBuffer - pCommonBuffer: 0x%X, CommonBufferPhys: 0x%X Offset:%d\n",
                                (UINT32)pHcInstance->pCommonBuffer , 
                                pHcInstance->CommonBufferPhys,
                                pHcInstance->CommonBufferUserDataOffset));
    }
                            
    return status;
}

/*
 * QueueEventResponse - queues an event in a process context back to the bus driver
 * 
*/
SDIO_STATUS QueueEventResponse(PSDHCD_INSTANCE pHcInstance, INT WorkItemID)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
     struct work_struct *work;
#else
    struct delayed_work *work;
#endif

    if (pHcInstance->ShuttingDown) {
        return SDIO_STATUS_CANCELED;
    }

    switch (WorkItemID) {
        case WORK_ITEM_IO_COMPLETE:
            work = &pHcInstance->OsSpecific.iocomplete_work;
            break;
        case WORK_ITEM_CARD_DETECT:
            work = &pHcInstance->OsSpecific.carddetect_work;
            break;
        case WORK_ITEM_SDIO_IRQ:
            work = &pHcInstance->OsSpecific.sdioirq_work;
            break;
        default:
            DBG_ASSERT(FALSE);
            return SDIO_STATUS_ERROR;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    if (schedule_work(work) > 0)
#else
    if (schedule_delayed_work(work,0) > 0) 
#endif
    {
        if (WORK_ITEM_IO_COMPLETE == WorkItemID) {            
            pHcInstance->RequestCompleteQueued = TRUE;
        }
        return SDIO_STATUS_SUCCESS;
    } else {
        return SDIO_STATUS_PENDING;
    }
}

/*
 * hcd_iocomplete_wqueue_handler - the work queue for io completion
*/
static void hcd_iocomplete_wqueue_handler(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
void *context)
 {
     PSDHCD_INSTANCE pHcInstance = (PSDHCD_INSTANCE)context;
#else
struct work_struct *work)
{
    PSDHCD_INSTANCE pHcInstance =
      container_of( work, SDHCD_INSTANCE, OsSpecific.iocomplete_work.work );
#endif
 
    pHcInstance->RequestCompleteQueued = FALSE;
    
    if (!pHcInstance->ShuttingDown) {
        SDIO_HandleHcdEvent(&pHcInstance->Hcd, EVENT_HCD_TRANSFER_DONE);
    }
}

/*
 * hcd_carddetect_handler - the work queue for card detect debouncing
*/
static void hcd_carddetect_wqueue_handler(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
struct work_struct *work)
{
    PSDHCD_INSTANCE context =
      container_of( work, SDHCD_INSTANCE, OsSpecific.carddetect_work.work );
#else
void *context)
 {
#endif

    ProcessDeferredCardDetect((PSDHCD_INSTANCE)context);
}

/*
 * hcd_sdioirq_handler - the work queue for handling SDIO IRQ
*/
static void hcd_sdioirq_wqueue_handler(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
void *context)
 {
PSDHCD_INSTANCE pHcInstance = (PSDHCD_INSTANCE)context;
#else
struct work_struct *work)
{
    PSDHCD_INSTANCE pHcInstance  =
      container_of( work, SDHCD_INSTANCE, OsSpecific.sdioirq_work.work );
#endif

    DBG_PRINT(STD_HOST_TRACE_SDIO_INT, ("SDIO STD HOST: hcd_sdioirq_wqueue_handler \n"));
    if (!pHcInstance->ShuttingDown) {
        SDIO_HandleHcdEvent(&pHcInstance->Hcd, EVENT_HCD_SDIO_IRQ_PENDING);
    }
}




/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  UnmaskIrq - Unmask SD interrupts
  Input:    pHcInstance - host controller
            Mask - mask value
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
UINT16 UnmaskIrq(PSDHCD_INSTANCE pHcInstance, UINT32 Mask, BOOL FromIsr)
{
    UINT16 ints;
    unsigned long flags;

        /* protected read-modify-write */
    spin_lock_irqsave(&pHcInstance->OsSpecific.RegAccessLock,flags);
   
    ints = READ_HOST_REG16(pHcInstance, HOST_REG_INT_SIGNAL_ENABLE);
    ints |= Mask;
    WRITE_HOST_REG16(pHcInstance, HOST_REG_INT_SIGNAL_ENABLE, ints);

    spin_unlock_irqrestore(&pHcInstance->OsSpecific.RegAccessLock,flags);
    
    return ints;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  MaskIrq - Mask SD interrupts
  Input:    pHcInstance - host controller
            Mask - mask value
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
UINT16 MaskIrq(PSDHCD_INSTANCE pHcInstance, UINT32 Mask, BOOL FromIsr)
{
    UINT16 ints;
    unsigned long flags;
        
    /* protected read-modify-write */
    spin_lock_irqsave(&pHcInstance->OsSpecific.RegAccessLock,flags);
    
    ints = READ_HOST_REG16(pHcInstance, HOST_REG_INT_SIGNAL_ENABLE);
    ints &= ~Mask;
    WRITE_HOST_REG16(pHcInstance, HOST_REG_INT_SIGNAL_ENABLE, ints);

    spin_unlock_irqrestore(&pHcInstance->OsSpecific.RegAccessLock,flags);
    
    return ints;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  EnableDisableSDIOIRQ - enable SDIO interrupt detection
  Input:    pHcInstance - host controller
            Enable - enable SDIO IRQ detection
            FromIsr - called from ISR
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void EnableDisableSDIOIRQ(PSDHCD_INSTANCE pHcInstance, BOOL Enable, BOOL FromIsr)
{
    UINT16 intsEnables;
    unsigned long flags;
       
    if (FromIsr) {
        if (Enable) {
                // isr should never re-enable 
            DBG_ASSERT(FALSE);
        } else {
            MaskIrq(pHcInstance, HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE,TRUE);
        }
    } else {  
        if (Enable) { 
            UnmaskIrq(pHcInstance, HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE, FALSE); 
        } else {             
            MaskIrq(pHcInstance, HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE, FALSE);    
        }
    }           
     
    /* protected read-modify-write */   
    spin_lock_irqsave(&pHcInstance->OsSpecific.RegAccessLock,flags);
    
        
    intsEnables = READ_HOST_REG16(pHcInstance, HOST_REG_INT_STATUS_ENABLE);
    if (Enable) {
        intsEnables |=  HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE;
    } else { 
        intsEnables &= ~HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE;
    }
        
    WRITE_HOST_REG16(pHcInstance, HOST_REG_INT_STATUS_ENABLE, intsEnables);   

#ifdef TEST_MISSED_IRQ    
    if (Enable) {
        SetIRQTimer(pHcInstance, IRQ_TIMEOUT_MS);
    } else {
        CancelIRQTimer(pHcInstance);
    }
#endif
    
    spin_unlock_irqrestore(&pHcInstance->OsSpecific.RegAccessLock,flags);    
    
    

}


SDIO_STATUS SetUpHCDDMA(PSDHCD_INSTANCE pHcInstance, PSDREQUEST pReq)
{
    
    PSDDMA_DESCRIPTOR pReqDescriptor     = (PSDDMA_DESCRIPTOR)pReq->pDataBuffer;  
    UINT32 totalLength = 0;
    
    DBG_PRINT(STD_HOST_TRACE_DATA, ("SDIO STD HOST SetUpHCDDMA (%s) DescCount:%d Blocks:%d, BlockLen:%d\n",
        IS_SDREQ_WRITE_DATA(pReq->Flags) ?  "TX" : "RX",
        pReq->DescriptorCount, pReq->BlockCount, pReq->BlockLen));
    
    if (IS_HCD_ADMA(pHcInstance) && (pReq->DescriptorCount > SDHCD_MAX_ADMA_DESCRIPTOR)) {
        DBG_ASSERT(FALSE);
        return SDIO_STATUS_INVALID_PARAMETER;  
    } else if (IS_HCD_SDMA(pHcInstance) && (pReq->DescriptorCount > SDHCD_MAX_SDMA_DESCRIPTOR)) {
        DBG_ASSERT(FALSE);
        return SDIO_STATUS_INVALID_PARAMETER;  
    }  
    
         /* map this scatter gather entries to address and save for unmap */
    if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {        
        dma_map_sg(pHcInstance->Hcd.pDevice, pReqDescriptor, pReq->DescriptorCount, DMA_TO_DEVICE);
    } else {
        dma_map_sg(pHcInstance->Hcd.pDevice, pReqDescriptor, pReq->DescriptorCount, DMA_FROM_DEVICE);
    }
     
    pHcInstance->OsSpecific.pDmaList = pReqDescriptor;   
    pHcInstance->OsSpecific.SGcount = pReq->DescriptorCount; 
    
    if (IS_HCD_ADMA(pHcInstance)) {
        int ii;
        PSDHCD_SGDMA_DESCRIPTOR  pDescriptor = 
            (PSDHCD_SGDMA_DESCRIPTOR)pHcInstance->OsSpecific.pDmaBuffer;

        DBG_ASSERT(pDescriptor != NULL);
            /* for ADMA build the in memory descriptor table */
        memset(pDescriptor, 0, pReq->DescriptorCount*(sizeof(SDHCD_SGDMA_DESCRIPTOR)));
        
        for (ii = 0; ii < pReq->DescriptorCount; ii++,pDescriptor++) {
            DBG_PRINT(STD_HOST_TRACE_DATA, ("SDIO STD HOST SetUpHCDDMA ADMA Descrp: 0x%X, ReqDescrip: 0x%X, len: %d bytes, addr: 0x%X\n",
             (UINT)pDescriptor, (UINT)&pReqDescriptor[ii], (UINT)sg_dma_len(&pReqDescriptor[ii]), (UINT)sg_dma_address(&pReqDescriptor[ii])));
            SET_DMA_LENGTH(pDescriptor, sg_dma_len(&pReqDescriptor[ii]));
            SET_DMA_ADDRESS(pDescriptor, sg_dma_address(&pReqDescriptor[ii]));
            totalLength += sg_dma_len(&pReqDescriptor[ii]);
            if (ii == (pReq->DescriptorCount-1)) {
                /* last entry, set END, 
                 ****note: we do NOT want an interrupt generated for this last descriptor,
                           the controller will generate interrupts indicating:
                           write CRC acknowledgement and program done -or-
                           read CRC okay */
                SET_DMA_END_OF_TRANSFER(pDescriptor);
            }
        }
    } else {
        DBG_PRINT(STD_HOST_TRACE_DATA, ("SDIO STD HOST SetUpHCDDMA SDMA, ReqDescrip: 0x%X, len: %d bytes, addr: 0x%X\n",
             (UINT)pReqDescriptor, (UINT)sg_dma_len(pReqDescriptor), (UINT)sg_dma_address(pReqDescriptor)));
            /* for simple DMA, setup DMA address */            
        WRITE_HOST_REG32(pHcInstance, HOST_REG_SYSTEM_ADDRESS, sg_dma_address(pReqDescriptor));
            /* since we only support 1 descriptor of up to 512KB size, we set the boundary up to 512KB 
               to prevent the DMA from stopping early , we let block count and length stop the DMA*/
        WRITE_HOST_REG16(pHcInstance, 
                         HOST_REG_BLOCK_SIZE, 
                         READ_HOST_REG16(pHcInstance,HOST_REG_BLOCK_SIZE) | HOST_REG_BLOCK_SIZE_DMA_512K_BOUNDARY);   
        totalLength = sg_dma_len(pReqDescriptor);
    }
    
    if (totalLength != (pReq->BlockCount * pReq->BlockLen)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO STD HOST DMA Block Length and Count Mismatch SGList Reports:%d bytes, Bus request : Blocks:%d,Bytes Per Block:%d \n",
        totalLength,pReq->BlockCount,pReq->BlockLen));
        return SDIO_STATUS_INVALID_PARAMETER;
    }
    
    if (IS_HCD_ADMA(pHcInstance)) {
            /* program the controller to execute the descriptor list */ 
        WRITE_HOST_REG32(pHcInstance, HOST_REG_ADMA_ADDRESS, (UINT32)pHcInstance->OsSpecific.hDmaBuffer);
            /* unprotect read-modify-write, set 32-bit DMA mode */
        WRITE_HOST_REG8(pHcInstance, HOST_REG_CONTROL,
            (READ_HOST_REG8(pHcInstance, HOST_REG_CONTROL) & ~HOST_REG_CONTROL_DMA_MASK) |
            HOST_REG_CONTROL_DMA_32BIT);
    }
            
    return SDIO_STATUS_PENDING;
}

/*
 * HcdTransferDataDMAEnd - cleanup bus master scatter-gather DMA read/write
*/
void HcdTransferDataDMAEnd(PSDHCD_INSTANCE pHcInstance, PSDREQUEST pReq)
{
    if (pHcInstance->OsSpecific.SGcount > 0) { 
        if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
            dma_unmap_sg(pHcInstance->Hcd.pDevice, 
                         pHcInstance->OsSpecific.pDmaList, 
                         pHcInstance->OsSpecific.SGcount, 
                         DMA_TO_DEVICE);
        } else {
            dma_unmap_sg(pHcInstance->Hcd.pDevice, 
                         pHcInstance->OsSpecific.pDmaList, 
                         pHcInstance->OsSpecific.SGcount, 
                         DMA_FROM_DEVICE);
        }
        pHcInstance->OsSpecific.SGcount = 0;
    }
}

void DumpDMADescriptorsInfo(PSDHCD_INSTANCE pHcInstance)
{
    if (IS_HCD_ADMA(pHcInstance)) {
        if (pHcInstance->DMAMode == STD_HCD_DMA_SG) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO STD HOST, ADMA Descriptor Start (PHYS):0x%X \n",
                     (UINT32)pHcInstance->OsSpecific.hDmaBuffer));    
            SDLIB_PrintBuffer((PUCHAR)pHcInstance->OsSpecific.pDmaBuffer, 
                               SDHCD_ADMA_DESCRIPTOR_SIZE, 
                               "SDIO STD HOST: ALL DMA Descriptors");                           
        } else if (pHcInstance->DMAMode == STD_HCD_DMA_COMMON) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO STD HOST, ADMA Common Buffer Descriptor Start (PHYS):0x%X \n",
                     (UINT32)pHcInstance->CommonBufferPhys));    
            SDLIB_PrintBuffer((PUCHAR)pHcInstance->pCommonBuffer, 
                              SDHCD_ADMA_DESCRIPTOR_SIZE, 
                              "SDIO STD HOST: Common Buffer DMA Descriptors");                           
                
        }
    }
}

/* handle an interrupting standard host
 * this route checks the slot interrupting register and calls the interrupt routine
 * of the interrupting slot, the caller must the std core structure containing a 
 * list of hcd instances.  This function will use the first hcd instance to read the
 * slot interrupting register.
 * */
BOOL HandleSharedStdHostInterrupt(PSDHCD_CORE_CONTEXT pStdCore)
{
    PSDLIST         pListItem;
    UINT16          interruptingSlots;
    BOOL            handled = FALSE;
    UINT            slotIndex;
    PSDHCD_INSTANCE  pHcInstance;
    
    
        /* this is called at ISR priority, we do not need to protect the list */
    if (SDLIST_IS_EMPTY(&pStdCore->SlotList)) {
        return FALSE; 
    }        
        /* the first controller will do, the interrupt status register
         * is mapped to each slot controller */
    pListItem = SDLIST_GET_ITEM_AT_HEAD(&pStdCore->SlotList);

    pHcInstance = CONTAINING_STRUCT(pListItem, SDHCD_INSTANCE, List);
        
    interruptingSlots = READ_HOST_REG16(pHcInstance, HOST_REG_SLOT_INT_STATUS);
    interruptingSlots &= HOST_REG_SLOT_INT_MASK;
    
    if (0 == interruptingSlots) {
            /* not our interrupt */
        return FALSE;    
    }
    
    DBG_PRINT(STD_HOST_TRACE_INT, ("SDIO STD HOST HandleSharedStdHostInterrupt : slot ints:0x%X \n",interruptingSlots));
    
    slotIndex = 0;
     
    SDITERATE_OVER_LIST(&pStdCore->SlotList, pListItem) {
        pHcInstance = CONTAINING_STRUCT(pListItem, SDHCD_INSTANCE, List);
            /* is it interrupting ? */        
        if ((1 << pHcInstance->OsSpecific.SlotNumber) & interruptingSlots) {
            DBG_PRINT(STD_HOST_TRACE_INT, ("SDIO STD HOST HandleSharedStdHostInterrupt pHcInstance: 0x%X, slot:%d is interrupting \n",
                    (UINT)pHcInstance,pHcInstance->OsSpecific.SlotNumber));
   
                /* this one is interrupting.. */
            TRACE_SIGNAL_DATA_ISR(pHcInstance, TRUE);  
            if (HcdSDInterrupt(pHcInstance)) {
                    /* at least one handled it */
                handled = TRUE;    
            }
            TRACE_SIGNAL_DATA_ISR(pHcInstance, FALSE); 
        }   
    }
        
    return handled;
}

/* start the standard host instances 
 * this function registers the standard host drivers and queues an event to check the slots */
SDIO_STATUS StartStdHostCore(PSDHCD_CORE_CONTEXT pStdCore)
{
    SDIO_STATUS         status = SDIO_STATUS_SUCCESS;
    PSDHCD_INSTANCE     pHcInstance;
    PSDLIST             pListItem;
    INT                 coreStarts = 0;
    unsigned long       flags;
        
    spin_lock_irqsave(&pStdCore->SlotListLock,flags);
     
    do {
        
        if (SDLIST_IS_EMPTY(&pStdCore->SlotList)) {
            break;    
        }
         
        SDITERATE_OVER_LIST(&pStdCore->SlotList, pListItem) {
            
            pHcInstance = CONTAINING_STRUCT(pListItem, SDHCD_INSTANCE, List);
            
            spin_unlock_irqrestore(&pStdCore->SlotListLock,flags);
            
                /* register with the SDIO bus driver */
            status = SDIO_RegisterHostController(&pHcInstance->Hcd);
            
            spin_lock_irqsave(&pStdCore->SlotListLock,flags);
            
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO STD Host - failed to register with host, status =%d\n",status));
                break;    
            }
            
            coreStarts++;
            
                /* mark that it has been registered */
            pHcInstance->InitStateMask |= SDHC_REGISTERED; 
    
                /* queue a work item to check for a card present at start up
                  this call will unmask the insert/remove interrupts */
            QueueEventResponse(pHcInstance, WORK_ITEM_CARD_DETECT);
        }
    
    } while (FALSE);
    
    spin_unlock_irqrestore(&pStdCore->SlotListLock,flags);
    
    if (0 == coreStarts) {
        return SDIO_STATUS_ERROR;    
    }
    
    return SDIO_STATUS_SUCCESS;
        
}

#ifdef TEST_MISSED_IRQ
struct timer_list   IRQTimer;         
static BOOL         IRQTimerQueued = FALSE;
static BOOL         IRQTimerCancelled = FALSE;
static BOOL         IRQTimerInit = FALSE;

static void DumpRegisters(PSDHCD_INSTANCE pHcInstance)
{
    DBG_PRINT(SDDBG_ERROR, ("----------------\n"));  
    DBG_PRINT(SDDBG_ERROR,("NORMAL INT STATUS : 0x%X \n",
                READ_HOST_REG16(pHcInstance, HOST_REG_NORMAL_INT_STATUS)));
    DBG_PRINT(SDDBG_ERROR,("INT SIGNAL ENABLE : 0x%X \n",
                READ_HOST_REG16(pHcInstance, HOST_REG_INT_SIGNAL_ENABLE))); 
    DBG_PRINT(SDDBG_ERROR,("STATUS ENABLES    : 0x%X \n",
                READ_HOST_REG16(pHcInstance, HOST_REG_INT_STATUS_ENABLE)));             
    DBG_PRINT(SDDBG_ERROR, ("HOST PRESENT_STATE: 0x%X \n",
                READ_HOST_REG32(pHcInstance, HOST_REG_PRESENT_STATE)));               
    DBG_PRINT(SDDBG_ERROR, ("---------------\n"));
}

static void IRQTimeout(unsigned long Context)
{
    PSDHCD_INSTANCE pHcInstance = (PSDHCD_INSTANCE)Context;
    unsigned long   flags;
        
    spin_lock_irqsave(&pHcInstance->OsSpecific.RegAccessLock,flags);
    
    IRQTimerQueued = FALSE;  
    
    if (!IRQTimerCancelled) {
        DumpRegisters(pHcInstance);
        SetIRQTimer(pHcInstance, IRQ_TIMEOUT_MS);
    }
    
    spin_unlock_irqrestore(&pHcInstance->OsSpecific.RegAccessLock,flags);
}

static void SetIRQTimer(PSDHCD_INSTANCE pHcInstance, UINT32 TimeOutMS)
{
     UINT32 delta;
    
    if (!IRQTimerInit) {
        IRQTimerInit = TRUE;
        init_timer(&IRQTimer);       
        IRQTimer.function = IRQTimeout;
        IRQTimer.data = (unsigned long)pHcInstance;     
    }
    
    if (!IRQTimerQueued) {
            /* convert timeout to ticks */
        delta = (TimeOutMS * HZ)/1000;
        if (delta == 0) {
            delta = 1;  
        }
        IRQTimer.expires = jiffies + delta;
        IRQTimerQueued = TRUE;
        IRQTimerCancelled = FALSE;
        add_timer(&IRQTimer);
    } else {
        DBG_ASSERT(FALSE);    
    }
    
}

static void CancelIRQTimer(PSDHCD_INSTANCE pHcInstance)
{
    if (IRQTimerQueued) {
        IRQTimerQueued = FALSE;   
        IRQTimerCancelled = TRUE;  
        del_timer(&IRQTimer);
    }
    
}
#endif


