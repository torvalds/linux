/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_hcd_os.c

@abstract: Linux Tokyo Electron PCI Ellen SDIO Host Controller Driver

#notes: includes module load and unload functions

@notice: Copyright (c) 2004-2006 Atheros Communications Inc.


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

#include "../sdio_pciellen_hcd.h"
#include <linux/fs.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pci.h>

#define DESCRIPTION "SDIO Tokyo Electron PCI Ellen HCD"
#define AUTHOR "Atheros Communications, Inc."

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#define ATH_INIT_WORK(_t, _f, _c)	INIT_WORK((_t), (void (*)(void *))(_f), (_c));
#else
#define ATH_INIT_WORK(_t, _f, _c)	INIT_DELAYED_WORK((_t), (_f));
#ifndef pci_module_init 
#define pci_module_init pci_register_driver
#endif
#endif

static SYSTEM_STATUS Probe(struct pci_dev *pPCIdevice, const struct pci_device_id *pId);
static void Remove(struct pci_dev *pPCIdevice);
static int MapAddress(struct pci_dev *pPCIdevice, char *pName, UINT8 bar, PSDHCD_MEMORY pAddress);
static void UnmapAddress(PSDHCD_MEMORY pMap);
static void RemoveDevice(struct pci_dev *pPCIdevice, PSDHCD_DRIVER_CONTEXT pHcdContext);
static SDIO_STATUS InitEllen(PSDHCD_DEVICE pDeviceContext);
static void GetDefaults(PSDHCD_DEVICE pDeviceContext);
static irqreturn_t hcd_sdio_irq(int irq, void *context
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
, struct pt_regs * r
#endif
  );

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

/* debug print parameter */
module_param(debuglevel, int, 0644);
MODULE_PARM_DESC(debuglevel, "debuglevel 0-7, controls debug prints");
#define DEFAULT_ATTRIBUTES (SDHCD_ATTRIB_BUS_1BIT      | \
                            SDHCD_ATTRIB_BUS_4BIT      | \
                            SDHCD_ATTRIB_MULTI_BLK_IRQ | \
                            SDHCD_ATTRIB_AUTO_CMD12    | \
                            SDHCD_ATTRIB_POWER_SWITCH )
                            
static UINT32 hcdattributes = DEFAULT_ATTRIBUTES;
module_param(hcdattributes, int, 0644);
MODULE_PARM_DESC(hcdattributes, "PCIELLEN Attributes");
static INT BaseClock = 0;
module_param(BaseClock, int, 0444);
MODULE_PARM_DESC(BaseClock, "BaseClock Hz when not present in configuration");
static UINT32 timeout = HOST_REG_TIMEOUT_CONTROL_DEFAULT;
module_param(timeout, int, 0644);
MODULE_PARM_DESC(timeout, "PCIELLEN timeout flags");
static UINT32 ClockSpinLimit = HCD_COMMAND_MIN_POLLING_CLOCK;
module_param(ClockSpinLimit, int, 0644);
MODULE_PARM_DESC(ClockSpinLimit, "PCIELLEN command clock spin time");




/* the driver context data */
static SDHCD_DRIVER_CONTEXT HcdContext = {
   .pDescription  = DESCRIPTION,
   .DeviceCount   = 0,
};

#define PCI_CLASS_SYSTEM_SDIO    0x0805
/* PCI devices supported */
static const struct pci_device_id pci_ids [] = { 
  {
    .vendor = 0x1679, .device = 0x3000,
    .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID,  
    .driver_data =  (unsigned long) &HcdContext,
  },
  {
   PCI_DEVICE_CLASS(PCI_CLASS_SYSTEM_SDIO << 8, 0xFFFFFF00),
    .driver_data =  (unsigned long) &HcdContext,
  },
 { /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE (pci, pci_ids);

/* tell PCI bus driver about us */
static struct pci_driver sdio_pci_driver = {
    .name =     "sdio_pciellenhcd",
    .id_table = pci_ids,

    .probe =    Probe,
    .remove =   Remove,

#ifdef CONFIG_PM
    .suspend =  NULL,     
    .resume =  NULL,      
#endif
};
    


/*
 * Probe - probe to setup our device, if present
*/
static SYSTEM_STATUS Probe(struct pci_dev *pPCIdevice, const struct pci_device_id *pId)
{
    SYSTEM_STATUS err = 0;
    SDIO_STATUS   status = SDIO_STATUS_SUCCESS;
    PSDHCD_DRIVER_CONTEXT pHcdContext;
    PSDHCD_DEVICE pDeviceContext = NULL;
    PSDHCD_DEVICE pLastDeviceContext; 
    int ii;
    int count;
    int firstBar;
    UINT8 config;
    SDHCD_TYPE type = TYPE_CLASS;
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCIELLEN HCD: Probe - probing for new device\n"));
    if ((pId == NULL) || (pId->driver_data == 0)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCIELLEN HCD: Probe - no device\n"));
        return -EINVAL;
    } 
    pHcdContext = (PSDHCD_DRIVER_CONTEXT)pId->driver_data;
    
    if (pci_enable_device(pPCIdevice) < 0) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCIELLEN HCD: Probe  - failed to enable device\n"));
        return -ENODEV;
    }
    if ((pId->vendor == pci_ids[0].vendor) && (pId->device == pci_ids[0].device)) {
        type = TYPE_PCIELLEN;
        DBG_PRINT(SDDBG_TRACE, ("SDIO PCIELLEN HCD: Probe  - setting PCI Ellen type\n"));
    }
    /* get the number of slots supported and the initial BAR for it */
    pci_read_config_byte(pPCIdevice, PCI_CONFIG_SLOT, &config);
    count = GET_SLOT_COUNT(config);
    firstBar = GET_SLOT_FIRST(config);
    if (type == TYPE_PCIELLEN) {
        /* move the first bar to the right start place */
        firstBar = 2;
    }
    if (count > 0) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO PCI BD: Probe - slot count: %d, first BAR: %d\n", count, firstBar));
    } else {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCI BD: Probe - no slots defined, first BAR: %d\n", firstBar));
        pci_disable_device(pPCIdevice);
        return -ENODEV;
    }
    
    /* create a device for each slot that we have */
    for(ii = 0; ii < count; ii++, firstBar++) {
        pLastDeviceContext = pDeviceContext;
        /* allocate a device context for this new device */
        pDeviceContext =  (PSDHCD_DEVICE)KernelAlloc(sizeof(SDHCD_DEVICE));
        if (pDeviceContext == NULL) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCI BD: Probe - no memory for device context\n"));
            err = -ENOMEM;
            break;
        }
        ZERO_POBJECT(pDeviceContext);
        SDLIST_INIT(&pDeviceContext->List);
        pDeviceContext->Type = type;
        pDeviceContext->pBusDevice = pPCIdevice;
        spin_lock_init(&pDeviceContext->Lock);
        spin_lock_init(&pDeviceContext->AddressSpinlock);
        
        SET_SDIO_STACK_VERSION(&pDeviceContext->Hcd);
        pDeviceContext->Hcd.pName = (PTEXT)KernelAlloc(SDHCD_MAX_DEVICE_NAME+1);
        snprintf(pDeviceContext->Hcd.pName, SDHCD_MAX_DEVICE_NAME, SDIO_BD_BASE"%i:%i",
                 pHcdContext->DeviceCount++, ii);
        pDeviceContext->Hcd.Attributes = hcdattributes;
        pDeviceContext->Hcd.MaxBlocksPerTrans = SDIO_SD_MAX_BLOCKS;
        pDeviceContext->Hcd.pContext = pDeviceContext;
        pDeviceContext->Hcd.pRequest = HcdRequest;
        pDeviceContext->Hcd.pConfigure = HcdConfig;
        pDeviceContext->Hcd.pDevice = &pPCIdevice->dev;
        pDeviceContext->Hcd.pModule = THIS_MODULE;
        pDeviceContext->BaseClock = BaseClock;
        pDeviceContext->TimeOut = timeout;
        pDeviceContext->ClockSpinLimit = ClockSpinLimit;
        /* add device to our list of devices */
            /* protect the devicelist */
        if (!SDIO_SUCCESS(status = SemaphorePendInterruptable(&pHcdContext->DeviceListSem))) {
            break;   /* wait interrupted */
        }
        SDListInsertTail(&pHcdContext->DeviceList, &pDeviceContext->List); 
        SemaphorePost(&pHcdContext->DeviceListSem);
        
        /* map the slots memory BAR */
        status = MapAddress(pPCIdevice, pDeviceContext->DeviceName, 
                            (UINT8)firstBar, &pDeviceContext->Address);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, 
               ("SDIO PCIELLEN HCD: Probe - failed to map device memory address %s 0x%X, status %d\n",
                pDeviceContext->DeviceName, (UINT)pci_resource_start(pPCIdevice, firstBar),
                status));
               break;                  
        }
        pDeviceContext->InitStateMask |= SDIO_BAR_MAPPED;

        if (type == TYPE_PCIELLEN) {
            if (pLastDeviceContext == NULL) {
                /* map the slots control register BAR */
                status = MapAddress(pPCIdevice, pDeviceContext->DeviceName, 
                                    (UINT8)0, &pDeviceContext->ControlRegs);
                if (!SDIO_SUCCESS(status)) {
                    DBG_PRINT(SDDBG_ERROR, 
                       ("SDIO PCIELLEN HCD: Probe - failed to map device control address %s 0x%X, status %d\n",
                        pDeviceContext->DeviceName, (UINT)pci_resource_start(pPCIdevice, 0),
                        status));
                       break;                  
                }
            } else {
                /* copy the prior mapping */
                pDeviceContext->ControlRegs = pLastDeviceContext->ControlRegs;
            }
            if ((ii+1) == count) {
                /* mark last one */
                pDeviceContext->InitStateMask |= SDIO_LAST_CONTROL_BAR_MAPPED;
            } 
        }
        /* initialize work items */
        ATH_INIT_WORK(&(pDeviceContext->iocomplete_work), hcd_iocomplete_wqueue_handler, pDeviceContext);
        ATH_INIT_WORK(&(pDeviceContext->carddetect_work), hcd_carddetect_wqueue_handler, pDeviceContext);
        ATH_INIT_WORK(&(pDeviceContext->sdioirq_work), hcd_sdioirq_wqueue_handler, pDeviceContext);

        /* map the controller interrupt, we map it to each device. 
           Interrupts can be called from this point on */
#ifndef SA_SHIRQ
#define SA_SHIRQ           IRQF_SHARED
#endif

        err = request_irq(pPCIdevice->irq, hcd_sdio_irq, SA_SHIRQ,
                          pDeviceContext->DeviceName, pDeviceContext);
        if (err < 0) {
              DBG_PRINT(SDDBG_ERROR, ("SDIO PCIELLEN - probe, unable to map interrupt \n"));
              err = -ENODEV;
              break;
        }
        pDeviceContext->InitStateMask |= SDIO_IRQ_INTERRUPT_INIT;

        if (!SDIO_SUCCESS((status = HcdInitialize(pDeviceContext)))) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCIELLEN Probe - failed to init HW, status =%d\n",status));
            err = SDIOErrorToOSError(status);
            break;
        } 
        pDeviceContext->InitStateMask |= SDHC_HW_INIT;
        
           /* register with the SDIO bus driver */
        if (!SDIO_SUCCESS((status = SDIO_RegisterHostController(&pDeviceContext->Hcd)))) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO PCIELLEN Probe - failed to register with host, status =%d\n",status));
            err = SDIOErrorToOSError(status);
            break;
        }      
        pDeviceContext->InitStateMask |= SDHC_REGISTERED;
        
        /* queue a work item to check for a card present at start up
           this call will unmask the insert/remove interrupts */
        QueueEventResponse(pDeviceContext, WORK_ITEM_CARD_DETECT);
    }
        
    
    if ((err < 0) || (!SDIO_SUCCESS(status))){
        pHcdContext->DeviceCount--;
        RemoveDevice(pPCIdevice, pHcdContext);
    } else {
      
      if (type == TYPE_PCIELLEN) {
          InitEllen(pDeviceContext);
      }    
    
      DBG_PRINT(SDDBG_ERROR, ("SDIO PCIELLEN Probe - HCD ready! \n"));
    }
    return 0;  
}

/* Remove - remove  device
 * perform the undo of the Probe
*/
static void Remove(struct pci_dev *pPCIdevice) 
{
    PSDHCD_DRIVER_CONTEXT pHcdContext = &HcdContext;
    
    DBG_PRINT(SDDBG_TRACE, ("+SDIO PCIELLEN HCD: Remove - removing device\n"));

    RemoveDevice(pPCIdevice, pHcdContext);
    pHcdContext->DeviceCount--;
    
    DBG_PRINT(SDDBG_TRACE, ("-SDIO PCIELLEN HCD: Remove\n"));
    return;
}

/*
 * RemoveDevice - remove all devices associated with bus device
*/
static void RemoveDevice(struct pci_dev *pPCIdevice, PSDHCD_DRIVER_CONTEXT pHcdContext)
{
    PSDHCD_DEVICE pDeviceContext; 
    DBG_PRINT(SDDBG_TRACE, ("+SDIO PCIELLEN HCD: RemoveDevice\n"));
    
    /* protect the devicelist */
    if (!SDIO_SUCCESS(SemaphorePendInterruptable(&pHcdContext->DeviceListSem))) {
        return;   /* wait interrupted */
    }
    
    SDITERATE_OVER_LIST_ALLOW_REMOVE(&pHcdContext->DeviceList, pDeviceContext, SDHCD_DEVICE, List)
        if (pDeviceContext->pBusDevice == pPCIdevice) {
            if (pDeviceContext->InitStateMask & SDHC_HW_INIT) {
                HcdDeinitialize(pDeviceContext);
            }

            if (pDeviceContext->InitStateMask & SDHC_REGISTERED) {
                SDIO_UnregisterHostController(&pDeviceContext->Hcd);
            }
            
            /* wait for any of our work items to run */
            flush_scheduled_work();
            
            if (pDeviceContext->InitStateMask & SDIO_IRQ_INTERRUPT_INIT) {
                free_irq(pPCIdevice->irq, pDeviceContext);
            }
            
            if (pDeviceContext->InitStateMask & SDIO_BAR_MAPPED) {
                UnmapAddress(&pDeviceContext->Address);
            }
            
            if (pDeviceContext->InitStateMask & SDIO_LAST_CONTROL_BAR_MAPPED) {
                UnmapAddress(&pDeviceContext->ControlRegs);
            }
            if (pDeviceContext->Hcd.pName != NULL) {
                KernelFree(pDeviceContext->Hcd.pName);
                pDeviceContext->Hcd.pName = NULL;
            }
            KernelFree(pDeviceContext);
        }
    SDITERATE_END;
    SemaphorePost(&pHcdContext->DeviceListSem);
    DBG_PRINT(SDDBG_TRACE, ("-SDIO PCIELLEN HCD: RemoveDevice\n"));
}

/*
 * MapAddress - sets up the address for a given BAR
*/
static int MapAddress(struct pci_dev *pPCIdevice, char *pName, UINT8 bar, PSDHCD_MEMORY pAddress)
{
    if (pci_resource_flags(pPCIdevice, bar) & PCI_BASE_ADDRESS_SPACE  ) {
        DBG_PRINT(SDDBG_WARN, ("SDIO PCIELLEN HCD: MapAddress, port I/O not supported\n"));
        return -ENOMEM;
    } 
    pAddress->Raw = pci_resource_start(pPCIdevice, bar);
    pAddress->Length = pci_resource_len(pPCIdevice, bar);
    if (!request_mem_region (pAddress->Raw, pAddress->Length, pName)) {
        DBG_PRINT(SDDBG_WARN, ("SDIO PCIELLEN HCD: MapAddress - memory in use: 0x%X(0x%X)\n",
                               (UINT)pAddress->Raw, (UINT)pAddress->Length));
        return -EBUSY;
    }
    pAddress->pMapped = ioremap_nocache(pAddress->Raw, pAddress->Length);
    if (pAddress->pMapped == NULL) {
        DBG_PRINT(SDDBG_WARN, ("SDIO PCIELLEN HCD: MapAddress - unable to map memory\n"));
        /* cleanup region */
        release_mem_region (pAddress->Raw, pAddress->Length);
        return -EFAULT;
    }
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCIELLEN HCD: MapAddress - mapped memory: 0x%X(0x%X) to 0x%X\n",
                            (UINT)pAddress->Raw, (UINT)pAddress->Length, (UINT)pAddress->pMapped));
    return 0;
}

 

/*
 * UnmapAddress - unmaps the address 
*/
static void UnmapAddress(PSDHCD_MEMORY pAddress) {
    iounmap(pAddress->pMapped);
    release_mem_region(pAddress->Raw, pAddress->Length);
    pAddress->pMapped = NULL;
}

/*
 * InitEllen - initialize the Ellen card control registers
 * 
*/
static SDIO_STATUS InitEllen(PSDHCD_DEVICE pDeviceContext)
{
    UINT32 temp = READ_CONTROL_REG16(pDeviceContext, INTCSR);
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCIELLEN HCD: InitEllen INTCSR - 0x%X\n", (UINT)temp));
 
    WRITE_CONTROL_REG16(pDeviceContext, INTCSR,
        (UINT16)temp | INTCSR_LINTi1ENABLE | INTCSR_LINTi2ENABLE | INTCSR_PCIINTENABLE);

    temp = READ_CONTROL_REG32((pDeviceContext),GPIOCTRL);
        /* set GPIO 2,3 and 8 as output */
    temp &= ~(GPIO3_PIN_SELECT | GPIO2_PIN_SELECT | GPIO4_PIN_SELECT);  
    temp |= (GPIO8_PIN_DIRECTION | GPIO3_PIN_DIRECTION | GPIO2_PIN_DIRECTION | GPIO4_PIN_DIRECTION);               
    WRITE_CONTROL_REG32((pDeviceContext),GPIOCTRL, temp);
    DBG_PRINT(SDDBG_TRACE, ("SDIO PCIELLEN HCD: InitEllen GPIOCTRL - 0x%X\n", (UINT)temp));
    TRACE_SIGNAL_DATA_WRITE(pDeviceContext, FALSE);
    TRACE_SIGNAL_DATA_READ(pDeviceContext, FALSE);
    TRACE_SIGNAL_DATA_ISR(pDeviceContext, FALSE);
    TRACE_SIGNAL_DATA_IOCOMP(pDeviceContext, FALSE);

    return SDIO_STATUS_SUCCESS;    
}

/*
 * QueueEventResponse - queues an event in a process context back to the bus driver
 * 
*/
SDIO_STATUS QueueEventResponse(PSDHCD_DEVICE pDeviceContext, INT WorkItemID)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    struct work_struct  *work;
#else
    struct delayed_work *work;
#endif
    
    if (pDeviceContext->ShuttingDown) {
        return SDIO_STATUS_CANCELED;
    }

    switch (WorkItemID) {
        case WORK_ITEM_IO_COMPLETE:
            work = &pDeviceContext->iocomplete_work;
            break;
        case WORK_ITEM_CARD_DETECT:
            work = &pDeviceContext->carddetect_work;
            break;
        case WORK_ITEM_SDIO_IRQ:
            work = &pDeviceContext->sdioirq_work;
            break;
        default:
            DBG_ASSERT(FALSE);
            return SDIO_STATUS_ERROR;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
    if (schedule_work(work) > 0) {
#else
    if (schedule_delayed_work(work,0) > 0) { 
#endif
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
    PSDHCD_DEVICE pDeviceContext = (PSDHCD_DEVICE)context;
#else
struct work_struct *work)
{
    PSDHCD_DEVICE pDeviceContext =
      container_of( work, SDHCD_DEVICE, iocomplete_work.work );
#endif

    if (!pDeviceContext->ShuttingDown) {
        TRACE_SIGNAL_DATA_IOCOMP(pDeviceContext, TRUE);
        SDIO_HandleHcdEvent(&pDeviceContext->Hcd, EVENT_HCD_TRANSFER_DONE);
        TRACE_SIGNAL_DATA_IOCOMP(pDeviceContext, FALSE);
    }
}

/*
 * hcd_carddetect_handler - the work queue for card detect debouncing
*/
static void hcd_carddetect_wqueue_handler(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
void *context)
{
    PSDHCD_DEVICE pDeviceContext = (PSDHCD_DEVICE)context;
#else
struct work_struct *work)
{
    PSDHCD_DEVICE pDeviceContext =
      container_of( work, SDHCD_DEVICE, carddetect_work.work );
#endif

    HCD_EVENT event;
    volatile UINT32 temp;
    
    event = EVENT_HCD_NOP;
    
    DBG_PRINT(SDDBG_TRACE, ("+ SDIO PCIELLEN Card Detect Work Item \n"));
    if (pDeviceContext->ShuttingDown) {
        return;
    }

    DBG_PRINT(SDDBG_TRACE, ("SDIO PCIELLEN Card Detect Delaying to debounce card... \n"));
        /* sleep for slot debounce if there is no card */
    OSSleep(SD_SLOT_DEBOUNCE_MS);
    
    /* wait for stable */
    while(!((temp = READ_HOST_REG32(pDeviceContext, HOST_REG_PRESENT_STATE))& 
            HOST_REG_PRESENT_STATE_CARD_STATE_STABLE)) {
        ;
    }

    if (pDeviceContext->CardInserted) { 
        /* look for removal */
        if (!(temp & HOST_REG_PRESENT_STATE_CARD_INSERTED)) {
            /* card not present */
            event = EVENT_HCD_DETACH;
            pDeviceContext->CardInserted = FALSE; 
            pDeviceContext->KeepClockOn = FALSE;   
            /* turn the power off */
            SetPowerOn(pDeviceContext, FALSE); 
            MaskIrq(pDeviceContext, HOST_REG_INT_STATUS_ALL);
            DBG_PRINT(PXA_TRACE_CARD_INSERT, ("SDIO PCIELLEN Card Detect REMOVE\n"));
        }
    } else {
        /* look for insert */
        if (temp & HOST_REG_PRESENT_STATE_CARD_INSERTED) {
            /* card present */
            event = EVENT_HCD_ATTACH;
            pDeviceContext->CardInserted = TRUE; 
            GetDefaults(pDeviceContext);

            DBG_PRINT(PXA_TRACE_CARD_INSERT, ("SDIO PCIELLEN Card Detect INSERT\n"));
        }
    }
                /* clear interrupt */
    WRITE_HOST_REG16(pDeviceContext, 
                     HOST_REG_NORMAL_INT_STATUS,
                     HOST_REG_INT_STATUS_CARD_INSERT_ENABLE | 
                     HOST_REG_INT_STATUS_CARD_REMOVAL_ENABLE);
    UnmaskIrq(pDeviceContext, HOST_REG_INT_STATUS_ALLOW_INSERT_REMOVE_ONLY);

    if (event != EVENT_HCD_NOP) {
        SDIO_HandleHcdEvent(&pDeviceContext->Hcd, event);
    }
    
    DBG_PRINT(PXA_TRACE_CARD_INSERT, ("- SDIO PCIELLEN Card Detect Work Item \n"));
}

/*
 * hcd_sdioirq_handler - the work queue for handling SDIO IRQ
*/
static void hcd_sdioirq_wqueue_handler(
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
void *context)
{
    PSDHCD_DEVICE pDeviceContext = (PSDHCD_DEVICE)context;
#else
struct work_struct *work)
{
    PSDHCD_DEVICE pDeviceContext =
      container_of( work, SDHCD_DEVICE, sdioirq_work.work );
#endif

    DBG_PRINT(PXA_TRACE_SDIO_INT, ("SDIO PCIELLEN: hcd_sdioirq_wqueue_handler \n"));
    if (!pDeviceContext->ShuttingDown) {
        SDIO_HandleHcdEvent(&pDeviceContext->Hcd, EVENT_HCD_SDIO_IRQ_PENDING);
    }
}


/* SDIO interrupt request */
static irqreturn_t hcd_sdio_irq(int irq, void *context
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
, struct pt_regs * r  
#endif
  )
  {
    irqreturn_t retStat;
    UINT16 intStat;
    
    DBG_PRINT(PXA_TRACE_SDIO_INT, ("SDIO PCIELLEN SDIO IRQ \n"));
    
    if (((PSDHCD_DEVICE)context)->Type == TYPE_PCIELLEN) {
        /* see if we interrupted */
        intStat = READ_CONTROL_REG16((PSDHCD_DEVICE)context, INTCSR);
        DBG_PRINT(PXA_TRACE_SDIO_INT, ("intStat: 0x%X\n", (UINT)intStat));
        if (!(intStat & (INTCSR_LINTi1STATUS | INTCSR_LINTi2STATUS))) {
            return IRQ_NONE;
        }
    }
    
    TRACE_SIGNAL_DATA_ISR((PSDHCD_DEVICE)context, TRUE);
        /* call OS independent ISR */
    if (HcdSDInterrupt((PSDHCD_DEVICE)context)) {
        retStat = IRQ_HANDLED;
    } else {
        retStat = IRQ_NONE;
    }    
    TRACE_SIGNAL_DATA_ISR((PSDHCD_DEVICE)context, FALSE);
    return retStat;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  UnmaskIrq - Unmask SD interrupts
  Input:    pDevice - host controller
            Mask - mask value
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
UINT16 UnmaskIrq(PSDHCD_DEVICE pDevice, UINT32 Mask)
{
    UINT16 ints;
    /* protected read-modify-write */
    spin_lock_irq(&pDevice->AddressSpinlock);
    ints = READ_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE);
    ints |= Mask;
    WRITE_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE, ints);
    spin_unlock_irq(&pDevice->AddressSpinlock);
    return ints;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  MaskIrq - Mask SD interrupts
  Input:    pDevice - host controller
            Mask - mask value
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
UINT16 MaskIrq(PSDHCD_DEVICE pDevice, UINT32 Mask)
{
    UINT16 ints;
    /* protected read-modify-write */
    spin_lock_irq(&pDevice->AddressSpinlock);
    ints = READ_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE);
    ints &= ~Mask;
    WRITE_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE, ints);
    spin_unlock_irq(&pDevice->AddressSpinlock);
    return ints;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  MaskIrqFromIsr - Mask SD interrupts, called from ISR
  Input:    pDevice - host controller
            Mask - mask value
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
UINT16 MaskIrqFromIsr(PSDHCD_DEVICE pDevice, UINT32 Mask)
{
    UINT16 ints;
    /* protected read-modify-write */
    spin_lock(&pDevice->AddressSpinlock);
    ints = READ_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE);
    ints &= ~Mask;
    WRITE_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE, ints);
    spin_unlock(&pDevice->AddressSpinlock);
    return ints;
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  UnmaskIrqFromIsr - Unmask SD interrupts
  Input:    pDevice - host controller
            Mask - mask value
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
UINT16 UnmaskIrqFromIsr(PSDHCD_DEVICE pDevice, UINT32 Mask)
{
    UINT16 ints;
    /* protected read-modify-write */
    spin_lock(&pDevice->AddressSpinlock);
    ints = READ_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE);
    ints |= Mask;
    WRITE_HOST_REG16(pDevice, HOST_REG_INT_SIGNAL_ENABLE, ints);
    spin_unlock(&pDevice->AddressSpinlock);
    return ints;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GetDefaults - get the user modifiable data items
  Input:    pDeviceContext - host controller
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void GetDefaults(PSDHCD_DEVICE pDeviceContext)
{
    //can't change this dynanmically: pDeviceContext->BaseClock = BaseClock;
    pDeviceContext->TimeOut = timeout;
    pDeviceContext->ClockSpinLimit = ClockSpinLimit;
}
    
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  EnableDisableSDIOIRQ - enable SDIO interrupt detection
  Input:    pDevice - host controller
            Enable - enable SDIO IRQ detection
            FromIsr - called from ISR
  Output: 
  Return: 
  Notes: 
        
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void EnableDisableSDIOIRQ(PSDHCD_DEVICE pDevice, BOOL Enable, BOOL FromIsr)
{
    UINT16 intsEnables;
   
    if (FromIsr) {
        if (Enable) {
                // isr should never re-enable 
            DBG_ASSERT(FALSE);
        } else {
            MaskIrqFromIsr(pDevice, HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE);
        }
    } else {  
        if (Enable) { 
            UnmaskIrq(pDevice, HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE); 
        } else {             
            MaskIrq(pDevice, HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE);    
        }
    }           
     
    /* protected read-modify-write */   
    if (FromIsr) { 
        spin_lock(&pDevice->AddressSpinlock);
    } else {
        spin_lock_irq(&pDevice->AddressSpinlock);
    } 
        
    intsEnables = READ_HOST_REG16(pDevice, HOST_REG_INT_STATUS_ENABLE);
    if (Enable) {
        intsEnables |=  HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE;
    } else { 
        intsEnables &= ~HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE;
    }
        
    WRITE_HOST_REG16(pDevice, HOST_REG_INT_STATUS_ENABLE, intsEnables);   
    
    if (FromIsr) {
        spin_unlock(&pDevice->AddressSpinlock);
    } else {
        spin_unlock_irq(&pDevice->AddressSpinlock);    
    }
    
    
}

/*
 * module init
*/
static int __init sdio_pci_hcd_init(void) {
    SYSTEM_STATUS err;
    SDIO_STATUS status; 
    
    REL_PRINT(SDDBG_TRACE, ("+SDIO PCIELLEN HCD: loaded\n"));
    
    SDLIST_INIT(&HcdContext.DeviceList);
    status = SemaphoreInitialize(&HcdContext.DeviceListSem, 1);
    if (!SDIO_SUCCESS(status)) {
       return SDIOErrorToOSError(status);
    }       
    
    /* register with the PCI bus driver */
    err = pci_module_init(&sdio_pci_driver);
    if (err < 0) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO PCIELLEN HCD: failed to register with system PCI bus driver, %d\n",
                                err));
    }
    DBG_PRINT(SDDBG_TRACE, ("-SDIO PCIELLEN HCD: sdio_pci_hcd_init\n"));
    return err;
}

/*
 * module cleanup
*/
static void __exit sdio_pci_hcd_cleanup(void) {
    REL_PRINT(SDDBG_TRACE, ("+SDIO PCIELLEN HCD: unloaded\n"));
    pci_unregister_driver(&sdio_pci_driver);
    DBG_PRINT(SDDBG_TRACE, ("-SDIO PCIELLEN HCD: leave sdio_pci_hcd_cleanup\n"));
}

// 
//MODULE_LICENSE("Dual BSD/GPL");
//
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);

module_init(sdio_pci_hcd_init);
module_exit(sdio_pci_hcd_cleanup);

