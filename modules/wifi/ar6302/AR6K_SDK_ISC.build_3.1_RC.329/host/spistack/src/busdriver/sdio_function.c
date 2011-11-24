//------------------------------------------------------------------------------
// <copyright file="sdio_function.c" company="Atheros">
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
#define MODULE_NAME  SDBUSDRIVER
#include "../include/ctsystem.h"
#include "../include/sdio_busdriver.h"
#include "../include/sdio_lib.h"
#include "_busdriver.h"

static SDIO_STATUS ProbeForDevice(PSDFUNCTION pFunction);

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Register a function driver with the bus driver.

  @function name: SDIO_RegisterFunction
  @prototype: SDIO_STATUS SDIO_RegisterFunction(PSDFUNCTION pFunction) 
  @category: PD_Reference
  @input:  pFunction - the function definition structure.
 
  @output: none

  @return: SDIO_STATUS - SDIO_STATUS_SUCCESS when succesful.
 
  @notes: Each function driver must register with the bus driver once upon loading.
          The calling function must be prepared to receive a Probe callback before
          this function returns. This will occur when an perpheral device is already
          pluugged in that is supported by this function.
          The function driver should unregister itself when exiting.
          The bus driver checks for possible function drivers to support a device
          in reverse registration order.
 
  @example: Registering a function driver:
            //list of devices supported by this function driver
       static SD_PNP_INFO Ids[] = {
            {.SDIO_ManufacturerID = 0xaa55,  
             .SDIO_ManufacturerCode = 0x5555, 
             .SDIO_FunctionNo = 1},
            {}                      //list is null termintaed
        };
        static GENERIC_FUNCTION_CONTEXT FunctionContext = {
            .Function.pName    = "sdio_generic", //name of the device
            .Function.Version  = CT_SDIO_STACK_VERSION_CODE, // set stack version
            .Function.MaxDevices = 1,    //maximum number of devices supported by this driver
            .Function.NumDevices = 0,    //current number of devices, always zero to start
            .Function.pIds     = Ids,    //the list of devices supported by this device
            .Function.pProbe   = Probe,  //pointer to the function drivers Probe function
                                         //  that will be called when a possibly supported device
                                         //  is inserted.
            .Function.pRemove  = Remove, //pointer to the function drivers Remove function
                                         /  that will be called when a device is removed.
            .Function.pContext = &FunctionContext, //data value that will be passed into Probe and
                                         //  Remove callbacks. 
        }; 
        SDIO_STATUS status;
        status = SDIO_RegisterFunction(&FunctionContext.Function)
        if (!SDIO_SUCCESS(status)) {
            ...failed to register
        }
        
  @see also: SDIO_UnregisterFunction
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDIO_RegisterFunction(PSDFUNCTION pFunction) 
{
	SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    
#ifdef CT_MAN_CODE_CHECK
    DBG_PRINT(SDDBG_TRACE,
        ("SDIO Bus Driver: _SDIO_RegisterFunction: WARNING, this version is locked to Memory cards and SDIO cards with JEDEC IDs of: 0x%X\n",
            ManCodeCheck));
#else    
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: _SDIO_RegisterFunction\n"));
#endif 

	DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: Function Driver Stack Version: %d.%d \n",
        GET_SDIO_STACK_VERSION_MAJOR(pFunction),GET_SDIO_STACK_VERSION_MINOR(pFunction)));
        
    if (!CHECK_FUNCTION_DRIVER_VERSION(pFunction)) {
        DBG_PRINT(SDDBG_ERROR, 
           ("SDIO Bus Driver: Function Major Version Mismatch (hcd = %d, bus driver = %d)\n",
           GET_SDIO_STACK_VERSION_MAJOR(pFunction), CT_SDIO_STACK_VERSION_MAJOR(g_Version)));
        return SDIO_STATUS_INVALID_PARAMETER;       
    }
   

	/* sanity check the driver */
	if ((pFunction == NULL) ||
		(pFunction->pProbe == NULL) ||
		(pFunction->pIds == NULL)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_RegisterFunction, invalid registration data\n"));
	    return SDIO_STATUS_INVALID_PARAMETER;
	}
    /* protect the function list and add the function */
    if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->FunctionListSem)))) {
      goto cleanup;   /* wait interrupted */
    }
    SignalInitialize(&pFunction->CleanupReqSig);
    SDLIST_INIT(&pFunction->DeviceList);
    SDListAdd(&pBusContext->FunctionList, &pFunction->SDList);
    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->FunctionListSem)))) {
      goto cleanup;   /* wait interrupted */
    }
	
	/* see if we have devices for this new function driver */
	ProbeForDevice(pFunction);
	
	return status;
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_RegisterFunction, error exit 0x%X\n", status));
    return status;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Unregister a function driver with the bus driver.

  @function name: SDIO_UnregisterFunction
  @prototype: SDIO_STATUS SDIO_UnregisterFunction(PSDFUNCTION pFunction) 
  @category: PD_Reference
  
  @input:  pFunction - the function definition structure.
 
  @output: none

  @return: SDIO_STATUS - SDIO_STATUS_SUCCESS when succesful.
 
  @notes: Each function driver must unregister from the bus driver when the function driver
          exits.
          A function driver must disconnect from any interrupts before calling this function.
 
  @example: Unregistering a function driver:
        SDIO_UnregisterFunction(&FunctionContext.Function);
        
  @see also: SDIO_RegisterFunction
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDIO_UnregisterFunction(PSDFUNCTION pFunction) 
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDDEVICE pDevice;
    
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: _SDIO_UnregisterFunction\n"));

    /* protect the function list and synchronize with Probe() and Remove()*/
    if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->FunctionListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
        /* remove this function from the function list */
    SDListRemove(&pFunction->SDList);
        /* now remove this function as the handler for any of its devices */
    SDITERATE_OVER_LIST_ALLOW_REMOVE(&pFunction->DeviceList, pDevice, SDDEVICE,FuncListLink)  {
        if (pDevice->pFunction == pFunction) {
                /* notify removal */
            NotifyDeviceRemove(pDevice); 
        }
    }SDITERATE_END;
    
    SignalDelete(&pFunction->CleanupReqSig); 

    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->FunctionListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: _SDIO_UnregisterFunction\n"));
	return status;
	
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: _SDIO_UnregisterFunction, error exit 0x%X\n", status));
    return status;
}

/* documentation headers only for Probe and Remove */
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: This function is called by the Busdriver when a device is inserted that can be supported by this function driver.
  
  @function name: Probe
  @prototype: BOOL (*pProbe)(struct _SDFUNCTION *pFunction, struct _SDDEVICE *pDevice)
  @category: PD_Reference
  
  @input:  pFunction - the function definition structure that was passed to Busdriver
                       via the SDIO_RegisterFunction.
  @input:  pDevice   - the description of the newly inserted device.            
 
  @output: none

  @return: TRUE  - this function driver will suport this device
           FALSE - this function driver will not support this device
 
  @notes: The Busdriver calls the Probe function of a function driver to inform it that device is
          available for the function driver to control. The function driver should initialize the 
          device and be pepared to acceopt any interrupts from the device before returning.
  
  @example: Example of typical Probe function callback:
  static BOOL Probe(PSDFUNCTION pFunction, PSDDEVICE pDevice) { 
       ...get the our context info passed into the SDIO_RegisterFunction
    PSDXXX_DRIVER_CONTEXT pFunctionContext = 
                                (PSDXXX_DRIVER_CONTEXT)pFunction->pContext;
    SDIO_STATUS status;
       //test the identification of this device and ensure we want to support it
       // we can test based on class, or use more specific tests on SDIO_ManufacturerID, etc.
    if (pDevice->pId[0].SDIO_FunctionClass == XXX) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO XXX Function: Probe - card matched (0x%X/0x%X/0x%X)\n",
                                pDevice->pId[0].SDIO_ManufacturerID,
                                pDevice->pId[0].SDIO_ManufacturerCode,
                                pDevice->pId[0].SDIO_FunctionNo));
        ...
        
  @see also: SDIO_RegisterFunction
  @see also: Remove
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

BOOL FilterPnpInfo(PSDDEVICE pDevice) 
{
    return TRUE;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: This function is called by the Busdriver when a device controlled by this function
             function driver is removed.
  
  @function name: Remove
  @prototype: void (*pRemove)(struct _SDFUNCTION *pFunction, struct _SDDEVICE *pDevice)
  @category: PD_Reference
  
  @input:  pFunction - the function definition structure that was passed to Busdriver
                       via the SDIO_RegisterFunction.
  @input:  pDevice   - the description of the device being removed.
 
  @output: none

  @return: none
 
  @notes: The Busdriver calls the Remove function of a function driver to inform it that device it
          was supporting has been removed. The device has already been removed, so no further I/O
          to the device can be performed.
  
  @example: Example of typical Remove function callback:
    void Remove(PSDFUNCTION pFunction, PSDDEVICE pDevice) {
            // get the our context info passed into the SDIO_RegisterFunction
        PSDXXX_DRIVER_CONTEXT pFunctionContext = 
                             (PSDXXX_DRIVER_CONTEXT)pFunction->pContext;
           ...free any acquired resources.
                
  @see also: SDIO_RegisterFunction
  @see also: Probe
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/* 
 * ProbeForFunction - look for a function driver to handle this card
 * 
*/
SDIO_STATUS ProbeForFunction(PSDDEVICE pDevice, PSDHCD pHcd) {
    SDIO_STATUS status;
    PSDLIST pList;
    PSDFUNCTION pFunction;
	
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: ProbeForFunction\n"));
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: ProbeForFunction - Dump of Device PNP Data: \n"));
    DBG_PRINT(SDDBG_TRACE, (" Card Flags 0x%X \n", pDevice->pId[0].CardFlags));
   
    if (!FilterPnpInfo(pDevice)) {
        status = SDIO_STATUS_SUCCESS;
        goto cleanup;  
    }
    
    /* protect the function list */
    if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->FunctionListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
   
    /* protect against ProbeForDevice */
    if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->DeviceListSem)))) {
            /* release the function list semaphore we just took */
        SemaphorePost(&pBusContext->FunctionListSem);
        goto cleanup;
    }
    
    if (pDevice->pFunction != NULL) {
            /* device already has a function driver handling it */
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: ProbeForFunction, device already has function\n"));
            /* release function list */
        SemaphorePost(&pBusContext->DeviceListSem);    
            /* release function list */
        SemaphorePost(&pBusContext->FunctionListSem);
            /* just return success */
        status = SDIO_STATUS_SUCCESS;
        goto cleanup;
    } 
    
        /* release device list */
    SemaphorePost(&pBusContext->DeviceListSem); 
         
    /* walk functions looking for one that can handle this device */
    SDITERATE_OVER_LIST(&pBusContext->FunctionList, pList) {
        pFunction = CONTAINING_STRUCT(pList, SDFUNCTION, SDList);
        if (pFunction->NumDevices >=  pFunction->MaxDevices) {
            /* function can't support any more devices */
            continue;
        }
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: ProbeForFunction - checking: %s \n",
                                pFunction->pName));
            
        /* see if this function handles this device */
        if (IsPotentialIdMatch(pDevice->pId, pFunction->pIds)) {
            if (!FilterPnpInfo(pDevice)) {
                break;  
            } 
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: ProbeForFunction -Got Match, probing: %s \n",
                                    pFunction->pName));                                 
            /* we need to setup with the OS bus driver before the probe, so probe can
              do OS operations. */
            OS_InitializeDevice(pDevice, pFunction);  
            if (!SDIO_SUCCESS(OS_AddDevice(pDevice, pFunction))) {
                break;  
            }  
            /* close enough match, ask the function driver if it supports us */
            if (pFunction->pProbe(pFunction, pDevice)) {
                /* she accepted the device, add to list */
                pDevice->pFunction = pFunction;
                SDListAdd(&pFunction->DeviceList, &pDevice->FuncListLink);
                pFunction->NumDevices++;
                break;
            } else {
                DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: %s did not claim the device \n",
                  pFunction->pName));
                /* didn't take this device */
                OS_RemoveDevice(pDevice);
            }
            
        }
    }
    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->FunctionListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: ProbeForFunction\n"));
	return status; 	
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: ProbeForFunction, error exit 0x%X\n", status));
    return status;
}

/* 
 * ProbeForDevice - look for a device that this function driver supports
 * 
*/
static SDIO_STATUS ProbeForDevice(PSDFUNCTION pFunction) {
    SDIO_STATUS status;
    PSDLIST pList;
    PSDDEVICE pDevice;
	
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: ProbeForDevice\n"));
    if (pFunction->NumDevices >=  pFunction->MaxDevices) {
        /* function can't support any more devices */
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: ProbeForDevice, too many devices in function\n"));
        return SDIO_STATUS_SUCCESS;
    }
  
     /* protect the driver list */
    if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->DeviceListSem)))) {
      goto cleanup;   /* wait interrupted */
    }
    /* walk device list */
    SDITERATE_OVER_LIST(&pBusContext->DeviceList, pList) {
        pDevice = CONTAINING_STRUCT(pList, SDDEVICE, SDList);
        if (pDevice->pFunction != NULL) {
            /* device already has a function driver handling it */
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: ProbeForDevice, device already has function\n"));
            continue;
        }
        
        if (IsPotentialIdMatch(pDevice->pId, pFunction->pIds)) {
            if (!FilterPnpInfo(pDevice)) {
                break;  
            }
            /* we need to setup with the OS bus driver before the probe, so probe can
              do OS operations. */
            OS_InitializeDevice(pDevice, pFunction);  
            if (!SDIO_SUCCESS(OS_AddDevice(pDevice, pFunction))) {
                break;  
            }  
            /* close enough match, ask the function driver if it supports us */
            if (pFunction->pProbe(pFunction, pDevice)) {
                /* she accepted the device, add to list */
                pDevice->pFunction = pFunction;
                SDListAdd(&pFunction->DeviceList, &pDevice->FuncListLink);
                pFunction->NumDevices++;
                break;
            } else {
                DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: %s did not claim the device \n",
                  pFunction->pName));
                /* didn't take this device */
                OS_RemoveDevice(pDevice);
            }
        }
    }
    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->DeviceListSem)))) {
      goto cleanup;   /* wait interrupted */
    }

	return status;
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: ProbeForDevice, error exit 0x%X\n", status));
    return status;
}

     
/* 
 * IsPotentialIdMatch - test for potential device match
 * 
*/
BOOL IsPotentialIdMatch(PSD_PNP_INFO pIdsDev, PSD_PNP_INFO pIdsFuncList) 
{
    PSD_PNP_INFO pTFn;
	BOOL match = FALSE; 
    
	for (pTFn = pIdsFuncList; !IS_LAST_SDPNPINFO_ENTRY(pTFn); pTFn++ ) 
    {       
             /* check raw Card */
        if ((pIdsDev->CardFlags & CARD_RAW) &&
            (pTFn->CardFlags & CARD_RAW)) {
            match = TRUE;
            break;
        }
	}
    
    return match; 
}

/* 
 * NotifyDeviceRemove - tell function driver on this device that the device is being removed
 * 
*/
SDIO_STATUS NotifyDeviceRemove(PSDDEVICE pDevice) {
    SDIO_STATUS     status; 
    SDREQUESTQUEUE  cancelQueue;
    PSDREQUEST      pReq;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    InitializeRequestQueue(&cancelQueue);
    
	if ((pDevice->pFunction != NULL) && 
        (pDevice->pFunction->pRemove != NULL)){
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: removing device 0x%X\n", (INT)pDevice));
            /* fail any outstanding requests for this device */
            /* acquire lock for request queue */
        status = _AcquireHcdLock(pDevice->pHcd);
        if (!SDIO_SUCCESS(status)) {
            return status;  
        }
            /* mark the function to block any more requests comming down */
        pDevice->pFunction->Flags |= SDFUNCTION_FLAG_REMOVING;
            /* walk through HCD queue and remove this function's requests */
        SDITERATE_OVER_LIST_ALLOW_REMOVE(&pDevice->pHcd->RequestQueue.Queue, pReq, SDREQUEST, SDList) {
            if (pReq->pFunction == pDevice->pFunction) {
                /* cancel this request, as this device or function is being removed */
                /* note that these request are getting completed out of order */
                DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - NotifyDeviceRemove: canceling req 0x%X\n", (UINT)pReq));             
                pReq->Status = SDIO_STATUS_CANCELED;
                    /* remove it from the HCD queue */
                SDListRemove(&pReq->SDList);
                    /* add it to the cancel queue */
                QueueRequest(&cancelQueue, pReq);
            }
        }SDITERATE_END;
        
        status = _ReleaseHcdLock(pDevice->pHcd);
        
           /* now empty the cancel queue if anything is in there */
        while (TRUE) {
            pReq = DequeueRequest(&cancelQueue);
            if (NULL == pReq) {
                break;    
            }
                /* complete the request */
            DoRequestCompletion(pReq, pDevice->pHcd);
        }
            /* re-acquire the lock to deal with the current request */
        status = _AcquireHcdLock(pDevice->pHcd);
        if (!SDIO_SUCCESS(status)) {
            return status;  
        }        
            /* now deal with the current request */
        pReq = GET_CURRENT_REQUEST(pDevice->pHcd);
        if ((pReq !=NULL) && (pReq->pFunction == pDevice->pFunction) && (pReq->pFunction != NULL)) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - NotifyDeviceRemove: Outstanding Req 0x%X on HCD: 0x%X.. waiting...\n", 
                (UINT)pReq, (UINT)pDevice->pHcd));                         
                /* the outstanding request on this device is for the function being removed */
            pReq->Flags |= SDREQ_FLAGS_CANCELED; 
                /* wait for this request to get completed normally */
            status = _ReleaseHcdLock(pDevice->pHcd);
            SignalWait(&pDevice->pFunction->CleanupReqSig);
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - NotifyDeviceRemove: Outstanding HCD Req 0x%X completed \n", (UINT)pReq));                         
        }  else { 
                /* release lock */
            status = _ReleaseHcdLock(pDevice->pHcd);
        }
        
            /* synchronize with ISR SYNC Handlers */
	 	status = SemaphorePendInterruptable(&pBusContext->DeviceListSem);           
        if (!SDIO_SUCCESS(status)) {
            return status;
        }    
            /* call this devices Remove function */
        pDevice->pFunction->pRemove(pDevice->pFunction,pDevice);
        pDevice->pFunction->NumDevices--;
            /* make sure the sync handler is NULLed out */
        pDevice->pIrqFunction = NULL;  
        SemaphorePost(&pBusContext->DeviceListSem);
        
        OS_RemoveDevice(pDevice);
            /* detach this device from the function list it belongs to */
        SDListRemove(&pDevice->FuncListLink);
        pDevice->pFunction->Flags &= (SD_FUNCTION_FLAGS)~SDFUNCTION_FLAG_REMOVING;
		pDevice->pFunction = NULL;
	}
	return SDIO_STATUS_SUCCESS;
}

    
/* 
 * RemoveHcdFunctions - remove all functions attached to an HCD
 * 
*/
SDIO_STATUS RemoveHcdFunctions(PSDHCD pHcd) {
    SDIO_STATUS status;
    PSDLIST pList;
    PSDFUNCTION pFunction;
    PSDDEVICE pDevice;
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: RemoveHcdFunctions\n"));
    
    /* walk through the functions and remove the ones associated with this HCD */
    /* protect the driver list */
    if (!SDIO_SUCCESS((status = SemaphorePend(&pBusContext->FunctionListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
        /* mark that card is being removed */
    pHcd->CardProperties.CardState |= CARD_STATE_REMOVED;
    SDITERATE_OVER_LIST(&pBusContext->FunctionList, pList) {
        pFunction = CONTAINING_STRUCT(pList, SDFUNCTION, SDList);
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: scanning function 0x%X, %s\n", (INT)pFunction,
                                (pFunction == NULL)?"NULL":pFunction->pName));
        
        /* walk the devices on this function and look for a match */
        SDITERATE_OVER_LIST_ALLOW_REMOVE(&pFunction->DeviceList, pDevice, SDDEVICE,FuncListLink) {
            if (pDevice->pHcd == pHcd) {
                /* match, remove it */
                NotifyDeviceRemove(pDevice);
            }
        SDITERATE_END;
    SDITERATE_END;    
    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->FunctionListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: RemoveHcdFunctions\n"));
    return SDIO_STATUS_SUCCESS;
    
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: RemoveHcdFunctions, error exit 0x%X\n", status));
    return status;   
}

/* 
 * RemoveAllFunctions - remove all functions attached
 * 
*/
SDIO_STATUS RemoveAllFunctions() 
{
    SDIO_STATUS status;
    PSDLIST pList;
    PSDHCD pHcd;
    
    /* walk through the HCDs  */
    /* protect the driver list */
    if (!SDIO_SUCCESS((status = SemaphorePend(&pBusContext->HcdListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
    SDITERATE_OVER_LIST(&pBusContext->HcdList, pList) {
        pHcd = CONTAINING_STRUCT(pList, SDHCD, SDList);
            /* remove the functions */
        RemoveHcdFunctions(pHcd);
    }    
    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->HcdListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
    return SDIO_STATUS_SUCCESS;
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: RemoveAllFunctions, error exit 0x%X\n", status));
    return status;   
}

