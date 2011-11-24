//------------------------------------------------------------------------------
// <copyright file="sdio_bus_os.c" company="Atheros">
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

/* debug level for this module*/
#define DBG_DECLARE 3;

#include "../../include/ctsystem.h"
#include <linux/kernel.h> 
#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/kthread.h>
#include <linux/pnp.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
void pnp_remove_device(struct pnp_dev *dev);
#endif
#endif
#include "../../include/sdio_busdriver.h"
#include "../../include/sdio_lib.h"
#include "../_busdriver.h"

#define DESCRIPTION "SPI Device Bus Driver"
#define AUTHOR "Atheros Communications, Inc."
        
/* debug print parameter */
module_param(debuglevel, int, 0644);
MODULE_PARM_DESC(debuglevel, "debuglevel 0-7, controls debug prints");
/* configuration and default parameters */
static int RequestRetries = 0;
module_param(RequestRetries, int, 0644);
MODULE_PARM_DESC(RequestRetries, "number of command retries");
static int RequestListSize = SDBUS_DEFAULT_REQ_LIST_SIZE;
module_param(RequestListSize, int, 0644);
MODULE_PARM_DESC(RequestListSize, "");
static int SignalSemListSize = SDBUS_DEFAULT_REQ_SIG_SIZE;
module_param(SignalSemListSize, int, 0644);
MODULE_PARM_DESC(SignalSemListSize, "");

static int HcdRCount = MAX_HCD_REQ_RECURSION;
module_param(HcdRCount, int, 0644);
MODULE_PARM_DESC(HcdRCount, "HCD request recursion count");
static int RegisterDriver(PSDFUNCTION pFunction);
static int UnregisterDriver(PSDFUNCTION pFunction);

#define SDDEVICE_FROM_OSDEVICE(pOSDevice)  container_of(pOSDevice, SDDEVICE, Device)
#define SDFUNCTION_FROM_OSDRIVER(pOSDriver)  container_of(pOSDriver, SDFUNCTION, Driver)


/* 
 * SDIO_RegisterHostController - register a host controller bus driver
*/
SDIO_STATUS SDIO_RegisterHostController(PSDHCD pHcd) {
    /* we are the exported verison, call the internal verison */
    return _SDIO_RegisterHostController(pHcd);
}

/* 
 * SDIO_UnregisterHostController - unregister a host controller bus driver
*/
SDIO_STATUS SDIO_UnregisterHostController(PSDHCD pHcd) {
    /* we are the exported verison, call the internal verison */
    return _SDIO_UnregisterHostController(pHcd);
}

/* 
 * SDIO_RegisterFunction - register a function driver
*/
SDIO_STATUS SDIO_RegisterFunction(PSDFUNCTION pFunction) {
    int error;
    SDIO_STATUS status;

    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - SDIO_RegisterFunction\n"));
    
        /* since we do PnP registration first, we need to check the version */
    if (!CHECK_FUNCTION_DRIVER_VERSION(pFunction)) {
        DBG_PRINT(SDDBG_ERROR, 
           ("SDIO Bus Driver: Function Major Version Mismatch (hcd = %d, bus driver = %d)\n",
           GET_SDIO_STACK_VERSION_MAJOR(pFunction), CT_SDIO_STACK_VERSION_MAJOR(g_Version)));
        return SDIO_STATUS_INVALID_PARAMETER;       
    }
    
    /* we are the exported verison, call the internal verison after registering with the bus
       we handle probes internally to the bus driver */
    if ((error = RegisterDriver(pFunction)) < 0) {
        DBG_PRINT(SDDBG_ERROR, 
            ("SDIO BusDriver - SDIO_RegisterFunction, failed to register with system bus driver: %d\n",
            error)); 
        status = OSErrorToSDIOError(error);       
    } else {
        status = _SDIO_RegisterFunction(pFunction);
        if (!SDIO_SUCCESS(status)) {
            UnregisterDriver(pFunction);   
        }
    }
    
    return status;
}

/* 
 * SDIO_UnregisterFunction - unregister a function driver
*/
SDIO_STATUS SDIO_UnregisterFunction(PSDFUNCTION pFunction) {
    SDIO_STATUS status;
    /* we are the exported verison, call the internal verison */
    status = _SDIO_UnregisterFunction(pFunction);
    UnregisterDriver(pFunction);
    return  status;
}

/* 
 * SDIO_HandleHcdEvent - tell core an event occurred
*/
SDIO_STATUS SDIO_HandleHcdEvent(PSDHCD pHcd, HCD_EVENT Event) {
    /* we are the exported verison, call the internal verison */
    DBG_PRINT(SDIODBG_HCD_EVENTS, ("SDIO Bus Driver: SDIO_HandleHcdEvent, event type 0x%X, HCD:0x%X\n", 
                         Event, (UINT)pHcd));
    return _SDIO_HandleHcdEvent(pHcd, Event);
}	
        
/* get default settings */
SDIO_STATUS _SDIO_BusGetDefaultSettings(PBDCONTEXT pBdc)
{
    /* these defaults are module params */  
    pBdc->RequestRetries = RequestRetries;
    pBdc->RequestListSize = RequestListSize;
    pBdc->SignalSemListSize = SignalSemListSize;
    pBdc->MaxHcdRecursion = HcdRCount;
    return SDIO_STATUS_SUCCESS;  
}

/*
 * OS_IncHcdReference - increment host controller driver reference count
*/
SDIO_STATUS Do_OS_IncHcdReference(PSDHCD pHcd)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    
    do {
        if (NULL == pHcd->pModule) {
                /* hcds that are 2.3 or higher should set this */
            DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: HCD:%s should set module ptr!\n",
                (pHcd->pName != NULL) ? pHcd->pName : "Unknown")); 
            break;    
        }        

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)       
        if (!try_module_get(pHcd->pModule)) {
            status = SDIO_STATUS_ERROR;   
        }
#else 
        if (!try_inc_mod_count(pHcd->pModule)) {
            status = SDIO_STATUS_ERROR;     
        }
#endif
        
    } while (FALSE);
    
    if (!SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: HCD:%s failed to get module\n",
            (pHcd->pName != NULL) ? pHcd->pName : "Unknown")); 
    }
                
    return status;
}

/*
 * OS_DecHcdReference - decrement host controller driver reference count
*/
SDIO_STATUS Do_OS_DecHcdReference(PSDHCD pHcd)
{
    if (pHcd->pModule != NULL) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        module_put(pHcd->pModule);   
#else
            /* 2.4 or lower */
        __MOD_DEC_USE_COUNT(pHcd->pModule);
#endif
    }
    return SDIO_STATUS_SUCCESS;
}

/****************************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/pnp.h>

#if !defined(CONFIG_PNP)
#error "CONFIG_PNP not defined"
#endif

static ULONG InUseDevices = 0;
static spinlock_t InUseDevicesLock = SPIN_LOCK_UNLOCKED;

static const struct pnp_device_id pnp_idtable[] = {
    {"SD_XXXX",  0}
};
static int sdio_get_resources(struct pnp_dev * pDev, struct pnp_resource_table * res)
{
    DBG_PRINT(SDDBG_TRACE, 
        ("SDIO BusDriver - sdio_get_resources: %s\n",
        pDev->dev.bus_id));
    return 0;
}
static int sdio_set_resources(struct pnp_dev * pDev, struct pnp_resource_table * res)
{
    DBG_PRINT(SDDBG_TRACE, 
        ("SDIO BusDriver - sdio_set_resources: %s\n",
        pDev->dev.bus_id));
    return 0;
}

static int sdio_disable_resources(struct pnp_dev *pDev)
{
    DBG_PRINT(SDDBG_TRACE, 
        ("SDIO BusDriver - sdio_disable_resources: %s\n",
        pDev->dev.bus_id));
    if (pDev != NULL) {
        pDev->active = 0;
    }
    return 0;
}
void    release(struct device * pDev) {
    DBG_PRINT(SDDBG_TRACE, 
        ("SDIO BusDriver - release: %s\n",
        pDev->bus_id));
    return;
}
struct pnp_protocol sdio_protocol = {
    .name   = "SDIO",
    .get    = sdio_get_resources,
    .set    = sdio_set_resources,
    .disable = sdio_disable_resources,
    .dev.release = release,
};

/* 
 * driver_probe - probe for OS based driver
*/
static int driver_probe(struct pnp_dev* pOSDevice, const struct pnp_device_id *pId)
{
    PSDDEVICE pDevice = SDDEVICE_FROM_OSDEVICE(pOSDevice);
    PSDFUNCTION pFunction = pDevice->Device.dev.driver_data;

    if (pFunction == NULL) {
        return -1;
    }
    
    if (strcmp(pFunction->pName, pOSDevice->dev.driver->name) == 0) {
        DBG_PRINT(SDDBG_TRACE, 
            ("SDIO BusDriver - driver_probe, match: %s/%s driver: %s\n",
            pOSDevice->dev.bus_id, pFunction->pName, pOSDevice->dev.driver->name));
        return 1;
    } else {
        DBG_PRINT(SDDBG_TRACE, 
            ("SDIO BusDriver - driver_probe, no match: %s/%s driver: %s\n",
            pOSDevice->dev.bus_id, pFunction->pName, pOSDevice->dev.driver->name));
        return -1;
    }        
/*    if (pOSDevice->id != NULL) {
        if (strcmp(pOSDevice->id->id, pId->id) == 0) {
            DBG_PRINT(SDDBG_TRACE, 
                ("SDIO BusDriver - driver_probe, match: %s/%s\n",
                pOSDevice->dev.bus_id, pId->id));
            return 1;
        }
        DBG_PRINT(SDDBG_TRACE, 
            ("SDIO BusDriver - driver_probe, did not match: %s/%s/%s\n",
            pOSDevice->dev.bus_id, pId->id, pOSDevice->id->id));
    } else {
        DBG_PRINT(SDDBG_TRACE, 
            ("SDIO BusDriver - driver_probe, did not match: %s/%s\n",
            pOSDevice->dev.bus_id, pId->id));
    }
    return -1;
*/    
//??    if (pDevice->Device.dev.driver_data != NULL) {
//??        if (pDevice->Device.dev.driver_data == pFunction) {
//??    if (pDevice->Device.data != NULL) {
//??        if (pDevice->Device.data == pFunction) {
//??            DBG_PRINT(SDDBG_TRACE, 
//??                ("SDIO BusDriver - driver_probe, match: %s\n",
//??                pOSDevice->dev.bus_id));
//??            return 1;
//??        }
//??    }
   DBG_PRINT(SDDBG_TRACE, 
        ("SDIO BusDriver - driver_probe,  match: %s\n",
        pOSDevice->dev.bus_id));
    return 1;
}

static int RegisterDriver(PSDFUNCTION pFunction)
{
    memset(&pFunction->Driver, 0, sizeof(pFunction->Driver));
    pFunction->Driver.name = pFunction->pName;
    pFunction->Driver.probe = driver_probe;
    pFunction->Driver.id_table = pnp_idtable;
    pFunction->Driver.flags = PNP_DRIVER_RES_DO_NOT_CHANGE;
    
    DBG_PRINT(SDDBG_TRACE, 
            ("SDIO BusDriver - SDIO_RegisterFunction, registering driver: %s\n",
            pFunction->Driver.name));
    return pnp_register_driver(&pFunction->Driver);
}

static int UnregisterDriver(PSDFUNCTION pFunction)
{
    DBG_PRINT(SDDBG_TRACE, 
            ("+SDIO BusDriver - UnregisterDriver, driver: %s\n",
            pFunction->Driver.name));
    pnp_unregister_driver(&pFunction->Driver);
    DBG_PRINT(SDDBG_TRACE, 
            ("-SDIO BusDriver - UnregisterDriver\n"));
   return 0;
}

/*
 * OS_InitializeDevice - initialize device that will be registered
*/
SDIO_STATUS OS_InitializeDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction) 
{
    struct pnp_id *pFdname;
    memset(&pDevice->Device, 0, sizeof(pDevice->Device));
    pDevice->Device.dev.driver_data = (PVOID)pFunction;
//??    pDevice->Device.data = (PVOID)pFunction;
//??    pDevice->Device.dev.driver = &pFunction->Driver.driver;
//??    pDevice->Device.driver = &pFunction->Driver;
//??    pDevice->Device.dev.release = release;
    /* get a unique device number, must be done with locks held */
    spin_lock(&InUseDevicesLock);
    pDevice->Device.number = FirstClearBit(&InUseDevices);
    SetBit(&InUseDevices, pDevice->Device.number);
    spin_unlock(&InUseDevicesLock);
    pDevice->Device.capabilities = PNP_REMOVABLE | PNP_DISABLE;
    pDevice->Device.protocol = &sdio_protocol;
    pDevice->Device.active = 1;
    
    pnp_init_resource_table(&pDevice->Device.res);
    
    pFdname = KernelAlloc(sizeof(struct pnp_id));
    
    if (NULL == pFdname) {
        return SDIO_STATUS_NO_RESOURCES; 
    }
    /* set the id as slot number/function number */
    snprintf(pFdname->id, sizeof(pFdname->id), "SD_%02X%02X", 
             pDevice->pHcd->SlotNumber, (UINT)SDDEVICE_GET_SDIO_FUNCNO(pDevice));
    pFdname->next = NULL;   
    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - OS_InitializeDevice adding id: %s\n",
                             pFdname->id));
    pnp_add_id(pFdname, &pDevice->Device);
    
        /* deal with DMA settings */
    if (pDevice->pHcd->pDmaDescription != NULL) {
        pDevice->Device.dev.dma_mask = &pDevice->pHcd->pDmaDescription->Mask;  
        pDevice->Device.dev.coherent_dma_mask = pDevice->pHcd->pDmaDescription->Mask; 
    }
    
    return SDIO_STATUS_SUCCESS;
}

/*
 * OS_AddDevice - must be pre-initialized with OS_InitializeDevice
*/
SDIO_STATUS OS_AddDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction) 
{
    int error; 
    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - OS_AddDevice adding function: %s\n",
                               pFunction->pName));
    error = pnp_add_device(&pDevice->Device);
    if (error < 0) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO BusDriver - OS_AddDevice failed pnp_add_device: %d\n",
                               error));
    }
        /* replace the buggy pnp's release */
    pDevice->Device.dev.release = release;
    
    return OSErrorToSDIOError(error);
}

/*
 * OS_RemoveDevice - unregister device with driver and bus
*/
void OS_RemoveDevice(PSDDEVICE pDevice) 
{
    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - OS_RemoveDevice \n"));
    pnp_remove_device(&pDevice->Device);
    spin_lock(&InUseDevicesLock);
    ClearBit(&InUseDevices, pDevice->Device.number);
    spin_unlock(&InUseDevicesLock);
    
    if (pDevice->Device.id != NULL) {
        KernelFree(pDevice->Device.id);
        pDevice->Device.id = NULL;
    }
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Add OS device to bus driver.

  @function name: SDIO_BusAddOSDevice 
  @category: HD_Reference
  
  @output: pDma    - descrip[tion of support DMA or NULL
  @output: pDriver - assigned driver object
  @output: pDevice - assigned device object
 
  @return: SDIO_STATUS - SDIO_STATUS_SUCCESS when successful.
 
  @notes: If the HCD does not register with the driver sub-system directly (like in the PCI case), 
          then it should register with the bus driver to obtain OS dependent device objects.
          All input structures should be maintained throughout the life of the driver.
 
  @example: getting device objects: 
    typedef struct _SDHCD_DRIVER {
        OS_PNPDEVICE   HcdDevice;     / * the OS device for this HCD * /
        OS_PNPDRIVER   HcdDriver;     / * the OS driver for this HCD * / 
        SDDMA_DESCRIPTION Dma;        / * driver DMA description * /
    }SDHCD_DRIVER, *PSDHCD_DRIVER;
    
    typedef struct _SDHCD_DRIVER_CONTEXT {
        PTEXT        pDescription;       / * human readable device decsription * /
        SDLIST       DeviceList;         / * the list of current devices handled by this driver * /
        OS_SEMAPHORE DeviceListSem;      / * protection for the DeviceList * /
        UINT         DeviceCount;        / * number of devices currently installed * /     
        SDHCD_DRIVER Driver;             / * OS dependent driver specific info * /  
    }SDHCD_DRIVER_CONTEXT, *PSDHCD_DRIVER_CONTEXT;
   
    static SDHCD_DRIVER_CONTEXT HcdContext = {
        .pDescription  = DESCRIPTION,
        .DeviceCount   = 0,
        .Driver.HcdDevice.name = "sdio_xxx_hcd",
        .Driver.HcdDriver.name = "sdio_xxx_hcd",
    }
    .....
    status = SDIO_BusAddOSDevice(NULL, &HcdContext.Driver, &HcdContext.Device);
    if (SDIO_SUCCESS(status) {
        return Probe(&HcdContext.Device);
    }
    return SDIOErrorToOSError(status);
        
  @see also: SDIO_BusRemoveOSDevice
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDIO_BusAddOSDevice(PSDDMA_DESCRIPTION pDma, POS_PNPDRIVER pDriver, POS_PNPDEVICE pDevice) 
{
    int err;
    struct pnp_id *pFdname;
    struct pnp_device_id *pFdid;
    static int slotNumber = 0; /* we just use an increasing count for the slots number */
    
    if (pDma != NULL) {
        pDevice->dev.dma_mask = &pDma->Mask;
        pDevice->dev.coherent_dma_mask = pDma->Mask;
    }        
    DBG_PRINT(SDDBG_ERROR, 
            ("SDIO BusDriver - SDIO_GetBusOSDevice, registering driver: %s DMAmask: 0x%x\n",
            pDriver->name, (UINT)*pDevice->dev.dma_mask));
    pFdid = KernelAlloc(sizeof(struct pnp_device_id)*2);
    /* set the id as slot number/function number */
    snprintf(pFdid[0].id, sizeof(pFdid[0].id), "SD_%02X08", 
             slotNumber++); 
    pFdid[0].driver_data = 0;    
    pFdid[1].id[0] = '\0';
    pFdid[1].driver_data = 0;    
             
    pDriver->id_table = pFdid;
    pDriver->flags = PNP_DRIVER_RES_DO_NOT_CHANGE;
    err = pnp_register_driver(pDriver);
    if (err < 0) {
        DBG_PRINT(SDDBG_ERROR, 
            ("SDIO BusDriver - SDIO_GetBusOSDevice, failed registering driver: %s, err: %d\n",
            pDriver->name, err));
        return OSErrorToSDIOError(err);    
    }
    
    pDevice->protocol = &sdio_protocol;
    pDevice->capabilities = PNP_REMOVABLE | PNP_DISABLE;
    pDevice->active = 1;
    
    pFdname = KernelAlloc(sizeof(struct pnp_id));
    /* set the id as slot number/function number */
    snprintf(pFdname->id, sizeof(pFdname->id), "SD_%02X08", 
             0); //??pDevice->pHcd->SlotNumber);//?????fix this, slotnumber isn't vaialble yet
    pFdname->next = NULL;    
    pnp_add_id(pFdname, pDevice);
    
    /* get a unique device number */
    spin_lock(&InUseDevicesLock);
    pDevice->number = FirstClearBit(&InUseDevices);
    SetBit(&InUseDevices, pDevice->number);
    spin_unlock(&InUseDevicesLock);
    pnp_init_resource_table(&pDevice->res);
    err = pnp_add_device(pDevice);
    if (err < 0) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO BusDriver - SDIO_GetBusOSDevice failed pnp_device_add: %d\n",
                               err));
        pnp_unregister_driver(pDriver);
    }
    /* replace the buggy pnp's release */
    pDevice->dev.release = release;
    return OSErrorToSDIOError(err);
}

/**+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Return OS device from bus driver.

  @function name: SDIO_BusRemoveOSDevice 
  @category: HD_Reference
  
  @input: pDriver - setup PNP driver object
  @input: pDevice - setup PNP device object
 
  @return: none
 
 
  @example: returning device objects:  
        SDIO_BusRemoveOSDevice(&HcdContext.Driver, &HcdContext.Device);

        
  @see also: SDIO_BusAddOSDevice
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void SDIO_BusRemoveOSDevice(POS_PNPDRIVER pDriver, POS_PNPDEVICE pDevice) 
{
    DBG_PRINT(SDDBG_ERROR, 
            ("SDIO BusDriver - SDIO_PutBusOSDevice, unregistering driver: %s\n",
            pDriver->name));
          
    pnp_remove_device(pDevice);
    if (pDevice->id != NULL) {
        KernelFree(pDevice->id);
        pDevice->id = NULL;
    }
    
    spin_lock(&InUseDevicesLock);
    ClearBit(&InUseDevices, pDevice->number);
    spin_unlock(&InUseDevicesLock);
    
    pnp_unregister_driver(pDriver);
    if (pDriver->id_table != NULL) {
        KernelFree((void *)pDriver->id_table);
        pDriver->id_table = NULL;
    }
    
}


/*
 * module init
*/
static int __init sdio_busdriver_init(void) {
    SDIO_STATUS status; 
    int error;
    REL_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: loaded\n"));
    if (!SDIO_SUCCESS((status = _SDIO_BusDriverInitialize()))) {
        return SDIOErrorToOSError(status);
    }
    /* register the sdio bus */
    error = pnp_register_protocol(&sdio_protocol);
    if (error < 0) {
        REL_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: failed to register bus device, %d\n", error));
        _SDIO_BusDriverCleanup();
        return error;
    }
    return 0;
}

/*
 * module cleanup
*/
static void __exit sdio_busdriver_cleanup(void) {
    REL_PRINT(SDDBG_TRACE, ("SDIO unloaded\n"));
    _SDIO_BusDriverCleanup();
    pnp_unregister_protocol(&sdio_protocol);
DBG_PRINT(SDDBG_TRACE, 
            ("SDIO BusDriver - unloaded 1\n"));
}
EXPORT_SYMBOL(SDIO_BusAddOSDevice);
EXPORT_SYMBOL(SDIO_BusRemoveOSDevice);

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    /* 2.4 */
static int RegisterDriver(PSDFUNCTION pFunction)
{
    return 0;
}

static int UnregisterDriver(PSDFUNCTION pFunction)
{
    DBG_PRINT(SDDBG_TRACE, 
            ("+-SDIO BusDriver - UnregisterDriver, driver: \n"));
   return 0;
}

/*
 * OS_InitializeDevice - initialize device that will be registered
*/
SDIO_STATUS OS_InitializeDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction) 
{
    return SDIO_STATUS_SUCCESS;
}

/*
 * OS_AddDevice - must be pre-initialized with OS_InitializeDevice
*/
SDIO_STATUS OS_AddDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction) 
{
    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - OS_AddDevice adding function: %s\n",
                               pFunction->pName));
    return SDIO_STATUS_SUCCESS;

}

/*
 * OS_RemoveDevice - unregister device with driver and bus
*/
void OS_RemoveDevice(PSDDEVICE pDevice) 
{
    DBG_PRINT(SDDBG_TRACE, ("SDIO BusDriver - OS_RemoveDevice \n"));
}

/*
 * module init
*/
static int __init sdio_busdriver_init(void) {
    SDIO_STATUS status; 
    REL_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: loaded\n"));
    if (!SDIO_SUCCESS((status = _SDIO_BusDriverInitialize()))) {
        return SDIOErrorToOSError(status);
    }
    return 0;
}

/*
 * module cleanup
*/
static void __exit sdio_busdriver_cleanup(void) {
    REL_PRINT(SDDBG_TRACE, ("SDIO unloaded\n"));
    _SDIO_BusDriverCleanup();
}
#else  ////KERNEL_VERSION
#error "unsupported kernel version: "UTS_RELEASE
#endif //KERNEL_VERSION

MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);

module_init(sdio_busdriver_init);
module_exit(sdio_busdriver_cleanup);
EXPORT_SYMBOL(SDIO_RegisterHostController);
EXPORT_SYMBOL(SDIO_UnregisterHostController);
EXPORT_SYMBOL(SDIO_HandleHcdEvent);
EXPORT_SYMBOL(SDIO_RegisterFunction);
EXPORT_SYMBOL(SDIO_UnregisterFunction);
