//------------------------------------------------------------------------------
// <copyright file="sdio_bus.c" company="Atheros">
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

/* list of host controller bus drivers */
PBDCONTEXT pBusContext = NULL;
static void CleanUpBusResources(void);
static SDIO_STATUS AllocateBusResources(void);
static PSIGNAL_ITEM BuildSignal(void);
static void DestroySignal(PSIGNAL_ITEM pSignal);

const CT_VERSION_CODE g_Version = CT_SDIO_STACK_VERSION_CODE;
/* 
 * _SDIO_BusDriverInitialize - call once on driver loading
 * 
*/
SDIO_STATUS _SDIO_BusDriverInitialize(void) 
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO (SPI Device I/O) Bus Driver : Version: %d.%d\n",
       CT_SDIO_STACK_VERSION_MAJOR(g_Version),CT_SDIO_STACK_VERSION_MINOR(g_Version)));
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: enter _SDIO_BusDriverInitialize\n"));
    
    do {
        /* allocate our internal data initialize it */
        pBusContext = KernelAlloc(sizeof(BDCONTEXT));
        if (pBusContext == NULL) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't allocate memory.\n"));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        } 
        memset(pBusContext,0,sizeof(BDCONTEXT));
        SDLIST_INIT(&pBusContext->RequestList);
        SDLIST_INIT(&pBusContext->HcdList);
        SDLIST_INIT(&pBusContext->DeviceList);
        SDLIST_INIT(&pBusContext->FunctionList);
        SDLIST_INIT(&pBusContext->SignalList);
        
            /* setup defaults */
        pBusContext->RequestRetries = 0;
        pBusContext->RequestListSize = SDBUS_DEFAULT_REQ_LIST_SIZE;
        pBusContext->SignalSemListSize = SDBUS_DEFAULT_REQ_SIG_SIZE;
        pBusContext->ConfigFlags = BD_DEFAULT_CONFIG_FLAGS;
        pBusContext->MaxHcdRecursion = MAX_HCD_REQ_RECURSION;
        
            /* get overrides for the defaults */
        status = _SDIO_BusGetDefaultSettings(pBusContext);
        if (!SDIO_SUCCESS(status)) {
            break;
        }  
        
        pBusContext->MaxRequestAllocations = pBusContext->RequestListSize << 1;
        pBusContext->MaxSignalAllocations = pBusContext->SignalSemListSize << 1;
              
        status = CriticalSectionInit(&pBusContext->RequestListCritSection);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't CriticalSectionInit.\n"));
            break;
        }        
        status = SemaphoreInitialize(&pBusContext->HcdListSem, 1);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't SemaphoreInitialize HcdListSem.\n"));
            break; 
        }       
        status = SemaphoreInitialize(&pBusContext->DeviceListSem, 1);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't SemaphoreInitialize DeviceListSem.\n"));
            break;
        }        
        status = SemaphoreInitialize(&pBusContext->FunctionListSem, 1);
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't SemaphoreInitialize FunctionListSem.\n"));
            break;
        } 
        status = AllocateBusResources();
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't AllocateBusResources.\n"));
            break;   
        }
                
        pBusContext->InitMask |= RESOURCE_INIT;
        
        pBusContext->pCardDetectMsgQueue = SDLIB_CreateMessageQueue(MAX_CARD_DETECT_MSGS, 
                                                                   sizeof(HCD_EVENT_MESSAGE));
        
        if (NULL == pBusContext->pCardDetectMsgQueue) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't CreateMessageQueue.\n"));
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }
        
        status = SDLIB_OSCreateHelper(&pBusContext->CardDetectHelper,
                                      CardDetectHelperFunction, 
                                      NULL); 
                                
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't OSCreateHelper.\n"));
            break;   
        }
        
        pBusContext->InitMask |= HELPER_INIT;
                     
    } while(FALSE);

    if (!SDIO_SUCCESS(status)) {
        _SDIO_BusDriverCleanup();
    }
    
    return status;    
}


/* 
 * _SDIO_BusDriverBusDriverCleanup - call once on driver unloading
 * 
*/
void _SDIO_BusDriverCleanup(void) {
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: _SDIO_BusDriverCleanup\n"));
     
    if (pBusContext->InitMask & HELPER_INIT) {
        SDLIB_OSDeleteHelper(&pBusContext->CardDetectHelper);
    }
    
    if (pBusContext->pCardDetectMsgQueue != NULL) {
        SDLIB_DeleteMessageQueue(pBusContext->pCardDetectMsgQueue);
        pBusContext->pCardDetectMsgQueue = NULL;    
    }
        /* remove functions */
    RemoveAllFunctions();
        /* cleanup all devices */
    DeleteDevices(NULL);
    CleanUpBusResources(); 
    CriticalSectionDelete(&pBusContext->RequestListCritSection);
    SemaphoreDelete(&pBusContext->HcdListSem);
    SemaphoreDelete(&pBusContext->DeviceListSem);
    SemaphoreDelete(&pBusContext->FunctionListSem);
    KernelFree(pBusContext);
    pBusContext = NULL;
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: _SDIO_BusDriverCleanup\n"));
}


/* cleanup hcd */
static void CleanupHcd(PSDHCD pHcd) 
{
    SDLIB_OSDeleteHelper(&pHcd->SDIOIrqHelper);    
    CleanupRequestQueue(&pHcd->CompletedRequestQueue);
    CleanupRequestQueue(&pHcd->RequestQueue);
    CriticalSectionDelete(&pHcd->HcdCritSection);
    SemaphoreDelete(&pHcd->ConfigureOpsSem);
    pHcd->pCurrentRequest = NULL;
    if (pHcd->pPseudoDev != NULL) {
        FreeDevice(pHcd->pPseudoDev);
        pHcd->pPseudoDev = NULL;   
    }
}

/* set up the hcd */
static SDIO_STATUS SetupHcd(PSDHCD pHcd)
{
    SDIO_STATUS status;
   
    ZERO_POBJECT(&pHcd->SDIOIrqHelper);
    ZERO_POBJECT(&pHcd->ConfigureOpsSem);
    ZERO_POBJECT(&pHcd->HcdCritSection);
    ZERO_POBJECT(&pHcd->RequestQueue);
    ZERO_POBJECT(&pHcd->CompletedRequestQueue);
    pHcd->pPseudoDev = NULL;
    pHcd->Recursion = 0;
    
    do {
       
        pHcd->pPseudoDev = AllocateDevice(pHcd);
       
        if (NULL == pHcd->pPseudoDev) {
            status = SDIO_STATUS_NO_RESOURCES;  
            break;  
        }
        
        ResetHcdState(pHcd); 
        
        status = SemaphoreInitialize(&pHcd->ConfigureOpsSem,1);
        if (!SDIO_SUCCESS(status)) {
            break;  
        }
        status = CriticalSectionInit(&pHcd->HcdCritSection);
        if (!SDIO_SUCCESS(status)) {
            break;  
        }
        status = InitializeRequestQueue(&pHcd->RequestQueue);
        if (!SDIO_SUCCESS(status)) {            
            break;  
        } 
        status = InitializeRequestQueue(&pHcd->CompletedRequestQueue);
        if (!SDIO_SUCCESS(status)) {            
            break;  
        } 
            /* create Irq helper */       
        status = SDLIB_OSCreateHelper(&pHcd->SDIOIrqHelper,
                                      SDIOIrqHelperFunction, 
                                     (PVOID)pHcd);
    } while(FALSE);
                            
    if (!SDIO_SUCCESS(status)) {
            /* undo what we did */
        CleanupHcd(pHcd);     
    }
    return status;
}


/* 
 * _SDIO_RegisterHostController - register a host controller bus driver
 * 
*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Register a host controller driver with the bus driver.

  @function name: SDIO_RegisterHostController 
  @prototype: SDIO_STATUS SDIO_RegisterHostController (PSDHCD pHcd) 
  @category: HD_Reference
  
  @input:  pHcd - the host controller definition structure.
 
  @output: none

  @return: SDIO_STATUS - SDIO_STATUS_SUCCESS when successful.
 
  @notes: Each host controller driver must register with the bus driver when loaded. 
          The driver registers an SDHCD structure initialized with hardware properties 
          and callback functions for bus requests and configuration.  On multi-slot 
          hardware ,each slot should be registered with a separate SDHCD structure.
          The bus driver views each slot as a seperate host controller object.
          The driver should be prepared to receive configuration requests before 
          this call returns. The host controller driver must unregister itself when 
          shutting down.
 
  @example: Registering a host controller driver:  
    static SDHCD Hcd = {
       .pName = "sdio_custom_hcd",
       .Version = CT_SDIO_STACK_VERSION_CODE,  // set stack version code
       .SlotNumber = 0,                        // bus driver internal use
       .Attributes = SDHCD_ATTRIB_BUS_1BIT | SDHCD_ATTRIB_BUS_4BIT | SDHCD_ATTRIB_MULTI_BLK_IRQ
                     SDHCD_ATTRIB_AUTO_CMD12 ,
       .MaxBytesPerBlock = 2048     // each data block can be up to 2048 bytes
       .MaxBlocksPerTrans = 1024,   // each data transaction can consist of 1024 blocks
       .MaxSlotCurrent = 500,       // max FET switch current rating
       .SlotVoltageCaps = SLOT_POWER_3_3V,      // only 3.3V operation
       .SlotVoltagePreferred = SLOT_POWER_3_3V,  
       .MaxClockRate = 24000000,   // 24 Mhz max operation
       .pContext = &HcdContext,    // set our driver context
       .pRequest = HcdRequest,     // set SDIO bus request callback
       .pConfigure = HcdConfig,    // set SDIO bus configuration callback
    };    
    if (!SDIO_SUCCESS((status = SDIO_RegisterHostController(&Hcd)))) {
         DBG_PRINT(SDDBG_ERROR, ("SDIO HCD - failed to register with host, status =%d\n",
                                    status));
    } 
        
  @see also: SDIO_UnregisterHostController
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDIO_RegisterHostController(PSDHCD pHcd) {
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;

    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: _SDIO_RegisterHostController - %s\n",pHcd->pName));
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: Host Controller Stack Version: %d.%d \n",
        GET_SDIO_STACK_VERSION_MAJOR(pHcd),GET_SDIO_STACK_VERSION_MINOR(pHcd)));
        
    if (!CHECK_HCD_DRIVER_VERSION(pHcd)) {
        DBG_PRINT(SDDBG_ERROR, 
           ("SDIO Bus Driver: HCD Major Version Mismatch (hcd = %d, bus driver = %d)\n",
           GET_SDIO_STACK_VERSION_MAJOR(pHcd), CT_SDIO_STACK_VERSION_MAJOR(g_Version)));
        return SDIO_STATUS_INVALID_PARAMETER;       
    }
        /* setup hcd */
    status = SetupHcd(pHcd);
    if (!SDIO_SUCCESS(status)) {
        return status;
    } 
        
    do {        
        INT slotNumber;            
        
            /* protect the HCD list */
        if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->HcdListSem)))) {
            break;  /* wait interrupted */
        }
            /* find a unique number for this HCD, must be done under semaphore protection */
        slotNumber = FirstClearBit(&pBusContext->HcdInUseField);
        if (slotNumber < 0) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_RegisterHostController, error, slotNumber exceeded\n"));
            /* fake something */
            slotNumber = 31;
        }
        SetBit(&pBusContext->HcdInUseField, slotNumber);
        pHcd->SlotNumber = slotNumber;
            /* add HCD to the end of the internal list */
        SDListAdd(&pBusContext->HcdList , &pHcd->SDList);
        if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->HcdListSem)))) {
            break;   /* wait interrupted */
        }
        
    } while (FALSE);
    
    if (!SDIO_SUCCESS(status)) {
       CleanupHcd(pHcd);
       DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_RegisterHostController, error 0x%X.\n", status));
    }
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: _SDIO_RegisterHostController\n"));
    return status;
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Unregister a host controller driver with the bus driver.

  @function name: SDIO_UnregisterHostController
  @prototype: SDIO_STATUS SDIO_UnregisterHostController (PSDHCD pHcd) 
  @category: HD_Reference
 
  @input:  pHcd - the host controller definition structure that was registered.
 
  @output: none

  @return: SDIO_STATUS - SDIO_STATUS_SUCCESS when successful.
 
  @notes: Each host controller driver must unregister with the bus driver when 
          unloading. The driver is responsible for halting any outstanding I/O 
          operations.  The bus driver will automatically unload function drivers
          that may be attached assigned to cards inserted into slots.
           
  @example: Unregistering a host controller driver: 
    if (!SDIO_SUCCESS((status = SDIO_UnregisterHostController(&Hcd)))) {
         DBG_PRINT(SDDBG_ERROR, ("SDIO HCD - failed to unregister with host, status =%d\n",
                                    status));
    } 
        
  @see also: SDIO_RegisterHostController
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDIO_UnregisterHostController(PSDHCD pHcd) {
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: _SDIO_UnregisterHostController\n"));
    
        /* remove functions associated with the HCD */
    RemoveHcdFunctions(pHcd);
        /* remove any devices associated with the HCD */    
    DeleteDevices(pHcd);
    /* wait for the message queue to be empty, so we don't have any delayed requests going
       to this device */
    while(!SDLIB_IsQueueEmpty(pBusContext->pCardDetectMsgQueue)) {
        /* wait for the messages to be handled */
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: _SDIO_UnregisterHostController, waiting on messages\n"));
        OSSleep(250);
    }

    /* protect the HCD list */
    if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->HcdListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
    ClearBit(&pBusContext->HcdInUseField, pHcd->SlotNumber);
    /* delete HCD from list  */
    SDListRemove(&pHcd->SDList);
    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->HcdListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
        /* cleanup anything we allocated */
    CleanupHcd(pHcd);
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: _SDIO_UnregisterHostController\n"));
    return status;
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_UnregisterHostController, error 0x%X.\n", status));
    return status;
}

/* documentation headers only for Request and Configure */
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: The bus driver calls the request callback to start an SDIO bus transaction. 
  @function name: Request
  @prototype: SDIO_STATUS (*pRequest) (struct _SDHCD *pHcd)
  @category: HD_Reference
  
  @input:  pHcd - the host controller structure that was registered
  
  @output: none

  @return: SDIO_STATUS
 
  @notes:  
          The bus driver maintains an internal queue of SDREQUEST structures submited by function
          drivers. The driver should use request macros to obtain a pointer to the current SDREQUEST 
          at the head of the queue.  The driver can access the fields of the current request in order
          to program hardware appropriately.   Once the request completes, the driver should update
          the current request information (final status, response bytes and/or data) and call
          SDIO_HandleHcdEvent() with the event type of EVENT_HCD_TRANSFER_DONE.
          The bus driver will remove the current request from the head of the queue and start the next
          request.
  
  @example: Example of a typical Request callback:
  SDIO_STATUS HcdRequest(PSDHCD pHcd) 
  {
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDHCD_DRIVER_CONTEXT pHct = (PSDHCD_DRIVER_CONTEXT)pHcd->pContext;
    UINT32                temp = 0;
    PSDREQUEST            pReq;
       // get the current request
    pReq = GET_CURRENT_REQUEST(pHcd);
    DBG_ASSERT(pReq != NULL);   
       // get controller settings based on response type
    switch (GET_SDREQ_RESP_TYPE(pReq->Flags)) {    
        case SDREQ_FLAGS_NO_RESP:
            break;
        case SDREQ_FLAGS_RESP_R1:
        case SDREQ_FLAGS_RESP_MMC_R4:        
        case SDREQ_FLAGS_RESP_MMC_R5:
        case SDREQ_FLAGS_RESP_R6:     
        case SDREQ_FLAGS_RESP_SDIO_R5:  
            temp |= CMDDAT_RES_R1_R4_R5;
            break;
        case SDREQ_FLAGS_RESP_R1B:
            temp |= (CMDDAT_RES_R1_R4_R5 | CMDAT_RES_BUSY);
            break;
        case SDREQ_FLAGS_RESP_R2:
            temp |= CMDDAT_RES_R2;
            break;
        case SDREQ_FLAGS_RESP_R3:
        case SDREQ_FLAGS_RESP_SDIO_R4:
            temp |= CMDDAT_RES_R3;
            break;
    }   
        // check for data    
    if (pReq->Flags & SDREQ_FLAGS_DATA_TRANS){
        temp |= CMDDAT_DATA_EN; 
        // set data remaining count
        pReq->DataRemaining = pReq->BlockLen * pReq->BlockCount;
        DBG_PRINT(TRACE_DATA, ("SDIO %s Data Transfer, Blocks:%d, BlockLen:%d, Total:%d \n",
                    IS_SDREQ_WRITE_DATA(pReq->Flags) ? "TX":"RX",
                    pReq->BlockCount, pReq->BlockLen, pReq->DataRemaining));
        if (IS_SDREQ_WRITE_DATA(pReq->Flags)) {
                // write operation
        }
    }
    // .... program hardware, interrupt handler will complete request
    return SDIO_STATUS_PENDING;
  }
        
  @see also: SDIO_HandleHcdEvent
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: The bus driver calls the configure callback to set various options
             and modes in the host controller hardware. 
             
  @function name: Configure
  @prototype: SDIO_STATUS (*pConfigure) (struct _SDHCD *pHcd, PSDCONFIG pConfig)
  @category: HD_Reference
  
  @input:  pHcd - the host controller structure that was registered
  @input:  pConfig - configuration request structure
  
  @output: none

  @return: SDIO_STATUS
 
  @notes:  
          The host controller driver recieves configuration requests for options
          such as slot voltage, bus width, clock rates and interrupt detection.
          The bus driver guarantees that only one configuration option request 
          can be issued at a time.  
  
  @example: Example of a typical configure callback:
  SDIO_STATUS HcdConfig(PSDHCD pHcd, PSDCONFIG pConfig) 
  {
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDHCD_DRIVER_CONTEXT pHct = (PSDHCD_DRIVER_CONTEXT)pHcd->pContext;
    UINT16      command;
        // get command
    command = GET_SDCONFIG_CMD(pConfig);
        // decode command
    switch (command){
        case SDCONFIG_GET_WP:
            if (GetGpioPinLevel(pHct,SDIO_CARD_WP_GPIO) == WP_POLARITY) {
                *((SDCONFIG_WP_VALUE *)pConfig->pData) = 1;
            } else {
                *((SDCONFIG_WP_VALUE *)pConfig->pData) = 0;  
            }            
            break;
        case SDCONFIG_SEND_INIT_CLOCKS:
            ClockStartStop(pHct,CLOCK_ON);
                // sleep a little, should be at least 80 clocks at our lowest clock setting
            status = OSSleep(100);
            ClockStartStop(pHct,CLOCK_OFF);          
            break;
        case SDCONFIG_SDIO_INT_CTRL:
            if (GET_SDCONFIG_CMD_DATA(PSDCONFIG_SDIO_INT_CTRL_DATA,pConfig)->SlotIRQEnable) {
                // request to enable IRQ detection
            } else {
                // request to disable IRQ detectioon
            }
            break;
        case SDCONFIG_SDIO_REARM_INT:
                // request to re-arm the card IRQ detection logic
            break;
        case SDCONFIG_BUS_MODE_CTRL:
                // request to set bus mode
            {
                // get bus mode data structure
               PSDCONFIG_BUS_MODE_DATA pBusMode = 
                      GET_SDCONFIG_CMD_DATA(PSDCONFIG_SDIO_INT_CTRL_DATA,pConfig);
                // set bus mode based on settings in bus mode structure
                // bus mode   :  pBusMode->BusModeFlags
                // clock rate :  pBusMode->ClockRate 
            }
            break;
        case SDCONFIG_POWER_CTRL:
                // request to set power/voltage 
            {
                PSDCONFIG_POWER_CTRL_DATA pPowerSetting = 
                       GET_SDCONFIG_CMD_DATA(PSDCONFIG_POWER_CTRL_DATA,pConfig);                       
                if (pPowerSetting->SlotPowerEnable) {
                    // turn on slot power
                    //
                } else {
                    // turn off slot power
                }       
                DBG_PRINT(PXA_TRACE_CONFIG, ("SDIO PXA255 PwrControl: En:%d, VCC:0x%X \n",
                      pPowerSetting->SlotPowerEnable,
                      pPowerSetting->SlotPowerVoltageMask));
            }
            break;
        default:
            // unsupported
            status = SDIO_STATUS_INVALID_PARAMETER;
    }
    return status;
 } 
    
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/


/*
 * Allocate a Device instance
 */
PSDDEVICE AllocateDevice(PSDHCD pHcd)
{
    PSDDEVICE pDevice;
     
    pDevice = KernelAlloc(sizeof(SDDEVICE));
    if (pDevice != NULL) {
        InitDeviceData(pHcd,pDevice);    
    } 
    return pDevice;
}


/*
 * Free a Device instance
 */
void FreeDevice(PSDDEVICE pDevice)
{
    DeinitDeviceData(pDevice);
    KernelFree(pDevice);
}
/* 
 * add this device to the list
 */
BOOL AddDeviceToList(PSDDEVICE pDevice)
{
    BOOL success = FALSE;
    
    do {
            /* protect the driver list */
        if (!SDIO_SUCCESS(SemaphorePendInterruptable(&pBusContext->DeviceListSem))) {
            break;   /* wait interrupted */
        }
        
            /* add new device to the internal list */
        SDListAdd(&pBusContext->DeviceList , &pDevice->SDList);
        
        if (!SDIO_SUCCESS(SemaphorePost(&pBusContext->DeviceListSem))) {
            break;
        }
        
        success = TRUE;
    } while (FALSE);
    
    return success;
}      

/*
 *  Delete device associated with the HCD
 *  if pHCD is NULL this function cleans up all devices, the caller
 *  better have cleaned up functions first! 
 */
SDIO_STATUS DeleteDevices(PSDHCD pHcd)
{
    SDIO_STATUS status;
    PSDDEVICE   pDevice;
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: DeleteDevices hcd:0x%X \n", (INT)pHcd));
      /* protect the device list */
    if (!SDIO_SUCCESS((status = SemaphorePendInterruptable(&pBusContext->DeviceListSem)))) {
        goto cleanup;   /* wait interrupted */
    }      
    SDITERATE_OVER_LIST_ALLOW_REMOVE(&pBusContext->DeviceList,pDevice,SDDEVICE,SDList) {
            /* only remove devices for the hcd or if we are cleaning up all */
        if ((NULL == pHcd) || (pDevice->pHcd == pHcd)) {
            SDListRemove(&pDevice->SDList); 
            DeinitDeviceData(pDevice);
            FreeDevice(pDevice);
        }
    }SDITERATE_END;
    if (!SDIO_SUCCESS((status = SemaphorePost(&pBusContext->DeviceListSem)))) {
        goto cleanup;   /* wait interrupted */
    }
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: DeleteDevices \n"));
    return status;
cleanup:
    DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: DeleteDevice, error exit 0x%X\n", status));
    return status;
}


static SDIO_STATUS AllocateBusResources(void)
{
    INT                 ii;
    PSDREQUEST          pReq;
    PSIGNAL_ITEM        pSignal;
    
    DBG_PRINT(SDDBG_TRACE, 
    ("+SDIO Bus Driver: AllocateBusResources (R:%d,S:%d) (CR:%d,MR:%d)(CS:%d,MS:%d) \n",
       pBusContext->RequestListSize,
       pBusContext->SignalSemListSize,
       pBusContext->CurrentRequestAllocations,pBusContext->MaxRequestAllocations,
       pBusContext->CurrentSignalAllocations,pBusContext->MaxSignalAllocations));
       
        /* allocate some initial requests */
    for (ii = 0; ii < pBusContext->RequestListSize; ii++) {
        pReq = AllocateRequest();
        if (pReq == NULL) {         
            break;
        } 
            /* free requests adds the request to the list */
        FreeRequest(pReq); 
    }  

    for (ii = 0; ii < pBusContext->SignalSemListSize; ii++) {
        pSignal = AllocateSignal();
        if (pSignal == NULL) {
            break; 
        } 
            /* freeing it adds it to the list */
        FreeSignal(pSignal);       
    }
    
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: AllocateBusResources\n"));
    return SDIO_STATUS_SUCCESS;
}


/* cleanup bus resources */
static void CleanUpBusResources(void)
{
    PSDLIST      pItem;
    PSDREQUEST   pReq;
    PSIGNAL_ITEM pSignal;
    
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: CleanUpBusResources (CR:%d,MR:%d)(CS:%d,MS:%d) \n",
       pBusContext->CurrentRequestAllocations,pBusContext->MaxRequestAllocations,
       pBusContext->CurrentSignalAllocations,pBusContext->MaxSignalAllocations));
      
    while(1) {
        pItem = SDListRemoveItemFromHead(&pBusContext->RequestList);
        if (NULL == pItem) {
            break;   
        }
            /* free the request */
        pReq = CONTAINING_STRUCT(pItem, SDREQUEST, SDList);        
        if (pReq->InternalFlags & SDBD_ALLOC_IRQ_SAFE_MASK) {
            KernelFreeIrqSafe(pReq);     
        } else {
            KernelFree(pReq);     
        }
        pBusContext->CurrentRequestAllocations--;
    }  
    
    if (pBusContext->CurrentRequestAllocations != 0) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Request allocations are not ZERO! (CR:%d)\n",
             pBusContext->CurrentRequestAllocations)); 
    }
    
    while(1) {
        pItem = SDListRemoveItemFromHead(&pBusContext->SignalList);
        if (NULL == pItem) {
            break;   
        }
        pSignal = CONTAINING_STRUCT(pItem, SIGNAL_ITEM, SDList);  
        DestroySignal(pSignal);
        pBusContext->CurrentSignalAllocations--;
    }  
    
    if (pBusContext->CurrentSignalAllocations != 0) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Signal allocations are not ZERO! (CR:%d)\n",
             pBusContext->CurrentRequestAllocations)); 
    }
    
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: CleanUpBusResources\n"));
}


/* free a request to the lookaside list */
void FreeRequest(PSDREQUEST pReq)
{    
    SDIO_STATUS status;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
   
    status = CriticalSectionAcquireSyncIrq(&pBusContext->RequestListCritSection);
        /* protect request list */
    if (!SDIO_SUCCESS(status)) {
        return;
    }
    
    if ((pBusContext->CurrentRequestAllocations <= pBusContext->MaxRequestAllocations) ||
         !(pReq->InternalFlags & SDBD_ALLOC_IRQ_SAFE_MASK)) {
            /* add it to the list */
        SDListAdd(&pBusContext->RequestList, &pReq->SDList);  
            /* we will hold onto this one */
        pReq = NULL;
    } else {
            /* decrement count */
        pBusContext->CurrentRequestAllocations--;  
    }
             
    status = CriticalSectionReleaseSyncIrq(&pBusContext->RequestListCritSection);
    
    if (pReq != NULL) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Free Request allocation (CR:%d,MR:%d)\n",
        pBusContext->CurrentRequestAllocations,pBusContext->MaxRequestAllocations));
        if (pReq->InternalFlags & SDBD_ALLOC_IRQ_SAFE_MASK) {
            KernelFreeIrqSafe(pReq);     
        } else {
                /* we should never free the ones that were normally allocated */
            DBG_ASSERT(FALSE);
        }
    }
}

/* allocate a request from the lookaside list */
PSDREQUEST AllocateRequest(void)
{
    PSDLIST  pItem;
    SDIO_STATUS status;
    PSDREQUEST pReq = NULL;
    ATOMIC_FLAGS internalflags;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
        
    
    status = CriticalSectionAcquireSyncIrq(&pBusContext->RequestListCritSection);
    
    if (!SDIO_SUCCESS(status)) {
        return NULL;
    }
    
    if (pBusContext->InitMask & RESOURCE_INIT) {
            /* check the list, we are now running... */
        pItem = SDListRemoveItemFromHead(&pBusContext->RequestList);
    } else {
            /* we are loading the list with requests at initialization */
        pItem = NULL;    
    }
    status = CriticalSectionReleaseSyncIrq(&pBusContext->RequestListCritSection);
   
    if (pItem != NULL) {
        pReq = CONTAINING_STRUCT(pItem, SDREQUEST, SDList);
    } else {
        if (pBusContext->InitMask & RESOURCE_INIT) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Request List empty..allocating new one (irq-safe) (CR:%d,MR:%d)\n",
            pBusContext->CurrentRequestAllocations,pBusContext->MaxRequestAllocations));
                /* the resource list was already allocated, we must be running now.
                 * at run-time, we allocate using the safe IRQ */    
            pReq = (PSDREQUEST)KernelAllocIrqSafe(sizeof(SDREQUEST));             
                /* mark that this one was created using IRQ safe allocation */ 
            internalflags = SDBD_ALLOC_IRQ_SAFE_MASK;    
        } else {
                /* use the normal allocation since we are called at initialization */
            pReq = (PSDREQUEST)KernelAlloc(sizeof(SDREQUEST));   
            internalflags = 0;    
        }
          
        if (pReq != NULL) {
            pReq->InternalFlags = internalflags;
                /* keep track of allocations */
            status = CriticalSectionAcquireSyncIrq(&pBusContext->RequestListCritSection);
            pBusContext->CurrentRequestAllocations++;     
            status = CriticalSectionReleaseSyncIrq(&pBusContext->RequestListCritSection);
        }  
    }  
    
    
    if (pReq != NULL) {
            /* preserve internal flags */
        internalflags = pReq->InternalFlags;
        ZERO_POBJECT(pReq);
        pReq->InternalFlags = internalflags;
    }
    
    return pReq;
}

static void DestroySignal(PSIGNAL_ITEM pSignal)
{
   SignalDelete(&pSignal->Signal);      
   KernelFree(pSignal);  
}

static PSIGNAL_ITEM BuildSignal(void)
{
    PSIGNAL_ITEM pSignal; 
    
    pSignal = (PSIGNAL_ITEM)KernelAlloc(sizeof(SIGNAL_ITEM));        
    if (pSignal != NULL) {
            /* initialize signal */
        if (!SDIO_SUCCESS(SignalInitialize(&pSignal->Signal))) {
            KernelFree(pSignal);   
            pSignal = NULL;
        } 
    }  
    return pSignal;
}
/* free a signal*/
void FreeSignal(PSIGNAL_ITEM pSignal)
{    
    SDIO_STATUS status;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
         
    status = CriticalSectionAcquireSyncIrq(&pBusContext->RequestListCritSection);
    
    if (!SDIO_SUCCESS(status)) {
        return;
    }
    
    if (pBusContext->CurrentSignalAllocations <= pBusContext->MaxSignalAllocations) {
            /* add it to the list */
        SDListAdd(&pBusContext->SignalList, &pSignal->SDList); 
            /* flag that we are holding onto it */
        pSignal = NULL;   
    } else {
            /* decrement count */
        pBusContext->CurrentSignalAllocations--;
    }
       
    status = CriticalSectionReleaseSyncIrq(&pBusContext->RequestListCritSection);
    
    if (pSignal != NULL) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Free signal allocation (CS:%d,MS:%d)\n",
        pBusContext->CurrentSignalAllocations,pBusContext->MaxSignalAllocations));
        DestroySignal(pSignal); 
    }
}

/* allocate a signal from the list */
PSIGNAL_ITEM AllocateSignal(void)
{ 
    PSDLIST         pItem;  
    PSIGNAL_ITEM    pSignal;
    SDIO_STATUS status;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
         
    status = CriticalSectionAcquireSyncIrq(&pBusContext->RequestListCritSection);
    
    if (!SDIO_SUCCESS(status)) {
        return NULL;
    }
    
    if (pBusContext->InitMask & RESOURCE_INIT) {
            /* check the list */
        pItem = SDListRemoveItemFromHead(&pBusContext->SignalList);
    } else {
            /* we are loading the list */
        pItem = NULL;
    }
    
    status = CriticalSectionReleaseSyncIrq(&pBusContext->RequestListCritSection);
    if (pItem != NULL) { 
            /* return the one from the list */
        pSignal = CONTAINING_STRUCT(pItem, SIGNAL_ITEM, SDList);
    } else {
        if (pBusContext->InitMask & RESOURCE_INIT) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Signal List empty..allocating new one (CS:%d,MS:%d)\n",
            pBusContext->CurrentSignalAllocations,pBusContext->MaxSignalAllocations));
        }
            /* just allocate one */    
        pSignal = BuildSignal();
        status = CriticalSectionAcquireSyncIrq(&pBusContext->RequestListCritSection);
        if (pSignal != NULL) {
            pBusContext->CurrentSignalAllocations++;   
        }    
        status = CriticalSectionReleaseSyncIrq(&pBusContext->RequestListCritSection);
    }
    
    return pSignal;
}

/*
 * Issus Bus Request (exposed to function drivers)
*/
PSDREQUEST IssueAllocRequest(PSDDEVICE pDev)
{
    return AllocateRequest();     
} 

/*
 * Free Request (exposed to function drivers)
*/
void IssueFreeRequest(PSDDEVICE pDev, PSDREQUEST pReq)
{
    FreeRequest(pReq);      
} 

/*
 * Issus Bus Request (exposed to function drivers)
*/
SDIO_STATUS IssueBusRequest(PSDDEVICE pDev, PSDREQUEST pReq)
{
    pReq->pFunction = pDev->pFunction;
    return IssueRequestToHCD(pDev->pHcd,pReq);      
} 


    /* completion routine for HCD configs, this is synchronized with normal bus requests */
static void HcdConfigComplete(PSDREQUEST pReq)
{
    
    pReq->Status = CALL_HCD_CONFIG((PSDHCD)pReq->pDataBuffer, (PSDCONFIG)pReq->pCompleteContext);
    
    SignalSet(&((PSIGNAL_ITEM)pReq->pHcdContext)->Signal);                
}

SDIO_STATUS SendSyncedHcdBusConfig(PSDDEVICE pDevice, PSDCONFIG pConfig)
{
    SDIO_STATUS     status = SDIO_STATUS_SUCCESS;
    PSDREQUEST      pReq = NULL;
    PSIGNAL_ITEM    pSignal = NULL;
    
    do { 
        
        pSignal = AllocateSignal();
        if (NULL == pSignal) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
                
        pReq = AllocateRequest();
        if (NULL == pReq) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }

            /* issue pseudo request to sync this with bus requests */
        pReq->pCompletion = HcdConfigComplete;
        pReq->pCompleteContext = pConfig;
            /* re-use hcd context to store the signal since this request 
             * never actually goes to an HCD */  
        pReq->pHcdContext = pSignal;
        pReq->pDataBuffer = pDevice->pHcd;
            /* flag this as barrier in case it may change the bus mode of the HCD */
        pReq->Flags = SDREQ_FLAGS_PSEUDO | SDREQ_FLAGS_BARRIER | SDREQ_FLAGS_TRANS_ASYNC;
        pReq->Status = SDIO_STATUS_SUCCESS;
        
            /* issue request */
        status = IssueRequestToHCD(pDevice->pHcd,pReq);       

    } while (FALSE); 
    
    if (SDIO_SUCCESS(status)) {
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Config Request Sync-Op waiting....\n"));
        status = SignalWait(&pSignal->Signal);
                
        if (SDIO_SUCCESS(status)) {
                /* return the result of the configuration request */
            status = pReq->Status;    
        }    
    }    
    
        /* cleanup */
    if (pReq != NULL) {
        FreeRequest(pReq);     
    }  
    
    if (pSignal != NULL) {
        FreeSignal(pSignal);    
    }
    
    return status;  
}

/*
 * Issus bus Configuration  (exposed to function drivers)
*/
SDIO_STATUS IssueBusConfig(PSDDEVICE pDev, PSDCONFIG pConfig)
{
    SDIO_STATUS status;
    INT         cmdLength;
    UINT8       debugLevel = SDDBG_ERROR;
    
    cmdLength = GET_SDCONFIG_CMD_LEN(pConfig);
    status = SDIO_STATUS_INVALID_PARAMETER;
     
    do {
            /* check buffers and length */
        if (IS_SDCONFIG_CMD_GET(pConfig) || IS_SDCONFIG_CMD_PUT(pConfig)) {
            if ((GET_SDCONFIG_CMD_DATA(PVOID,pConfig) == NULL) || (0 == cmdLength)) {
                break;
            }    
        }
                        
        switch (GET_SDCONFIG_CMD(pConfig)) {
            case SDCONFIG_FUNC_ACK_IRQ:
                status = SDFunctionAckInterrupt(pDev);
                break;
            case SDCONFIG_FUNC_UNMASK_IRQ:
                status = SDMaskUnmaskFunctionIRQ(pDev,FALSE);
                break;
            case SDCONFIG_FUNC_MASK_IRQ:
                status = SDMaskUnmaskFunctionIRQ(pDev,TRUE);
                break;
            default:
                
                if (GET_SDCONFIG_CMD(pConfig) & SDCONFIG_FLAGS_HC_CONFIG) {    
                        /* synchronize config requests with busrequests */
                    status = SendSyncedHcdBusConfig(pDev,pConfig);
                } else {
                    DBG_PRINT(SDDBG_ERROR, 
                        ("SDIO Bus Driver: IssueBusConfig - unknown command:0x%X \n",
                        GET_SDCONFIG_CMD(pConfig)));   
                    status = SDIO_STATUS_INVALID_PARAMETER; 
                }              
                break;  
        }
    } while(FALSE);
    
    if (!SDIO_SUCCESS(status)) {
         DBG_PRINT(debugLevel, 
                ("SDIO Bus Driver: IssueBusConfig - Error in command:0x%X, Buffer:0x%X, Length:%d Err:%d\n",
                GET_SDCONFIG_CMD(pConfig),
                GET_SDCONFIG_CMD_DATA(INT,pConfig),
                cmdLength, status));
    }
    return status;
} 

/* start a request */ 
static INLINE SDIO_STATUS StartHcdRequest(PSDHCD pHcd, PSDREQUEST pReq) 
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    CT_DECLARE_IRQ_SYNC_CONTEXT();

    if ((pReq->pFunction != NULL) && (pReq->pFunction->Flags & SDFUNCTION_FLAG_REMOVING)) {
        /* this device or function is going away, fail any new requests */
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: StartHcdRequest, fail request 0x%X, device is removing\n", (UINT)pReq));
        pReq->Status = SDIO_STATUS_CANCELED;
        return SDIO_STATUS_SDREQ_QUEUE_FAILED; 
    }
    
    status = _AcquireHcdLock(pHcd);    
    
    if (!SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to acquire HCD request lock: Err:%d\n", status));
        pReq->Status = SDIO_STATUS_SDREQ_QUEUE_FAILED;
        return SDIO_STATUS_SDREQ_QUEUE_FAILED;
    }
    
    if (pReq->Flags & SDREQ_FLAGS_QUEUE_HEAD) {
            /* caller wants this request queued to the head */
                    
            /* a completion routine for a barrier request is called
             * while the queue is busy.  A barrier request can
             * insert a new request at the head of the queue */
        DBG_ASSERT(IsQueueBusy(&pHcd->RequestQueue));    
        QueueRequestToFront(&pHcd->RequestQueue,pReq);
    } else {
            /* insert in queue at tail */ 
        QueueRequest(&pHcd->RequestQueue,pReq);
      
            /* is queue busy ? */
        if (IsQueueBusy(&pHcd->RequestQueue)) {
                /* release lock */
            status = _ReleaseHcdLock(pHcd);
                /* controller is busy already, no need to call the hcd */
            return SDIO_STATUS_PENDING; 
        } 
            /* mark it as busy */
        MarkQueueBusy(&pHcd->RequestQueue);
    }
        
        /* remove item from head and set current request */
    SET_CURRENT_REQUEST(pHcd, DequeueRequest(&pHcd->RequestQueue));
    if (CHECK_API_VERSION_COMPAT(pHcd,2,6)) { 
        CHECK_HCD_RECURSE(pHcd, pHcd->pCurrentRequest);
    }
        /* release lock */
    status = _ReleaseHcdLock(pHcd);
        /* controller was not busy, call into HCD to process current request */
    status = CallHcdRequest(pHcd); 
    return status;
}

/* synch completion routine */
static void SynchCompletion(PSDREQUEST pRequest)
{
    PSIGNAL_ITEM pSignal;    
    
    pSignal = (PSIGNAL_ITEM)pRequest->pCompleteContext;
    DBG_ASSERT(pSignal != NULL);
    if (!SDIO_SUCCESS(SignalSet(&pSignal->Signal))) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: SynchCompletion - signal failed \n"));
    }
    
}

/*
 * Issue a request to the host controller
 * 
 */  
SDIO_STATUS IssueRequestToHCD(PSDHCD pHcd, PSDREQUEST pReq)
{  
    SDIO_STATUS     status = SDIO_STATUS_SUCCESS;
    PSIGNAL_ITEM    pSignal = NULL;
    BOOL            handleFailedReqSubmit = FALSE;
    
    CLEAR_INTERNAL_REQ_FLAGS(pReq); 
    
    do {
            /* mark request in-use */
        ATOMIC_FLAGS internal = AtomicTest_Set(&pReq->InternalFlags, SDBD_PENDING);
        if (internal & ((ATOMIC_FLAGS)1 << SDBD_PENDING)) {
            DBG_ASSERT_WITH_MSG(FALSE,
                            "SDIO Bus Driver: IssueRequestToHCD - request already in use \n");
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Request already in use: 0x%X",(INT)pReq)); 
        }
        
        if (!(pReq->Flags & SDREQ_FLAGS_TRANS_ASYNC)) { 
                /* caller wants synchronous operation, insert our completion routine */
            pReq->pCompletion = SynchCompletion;
            pSignal = AllocateSignal();
            if (NULL == pSignal) {
                status = SDIO_STATUS_NO_RESOURCES;
                pReq->Status = SDIO_STATUS_NO_RESOURCES;
                handleFailedReqSubmit = TRUE;
                    /* no need to continue */
                break;    
            }  
            pReq->pCompleteContext = (PVOID)pSignal;      
        }
        
            /* start the normal request */
        status = StartHcdRequest(pHcd,pReq);                
                 
        if (SDIO_STATUS_SDREQ_QUEUE_FAILED == status) {
            handleFailedReqSubmit = TRUE;
                /* no need to continue, clean up at the end */
            break; 
        }
        
            /* at this point, the request was either queued or was processed by the
             * HCD */
        
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: HCD returned status:%d on request: 0x%X, (P[0]:)x%X) \n",
                  status, (INT)pReq, pReq->Parameters[0].As32bit));   
                         
        if (status != SDIO_STATUS_PENDING) {                  
            /* the HCD completed the request within the HCD request callback, 
             * check and see if this is a synchronous request */            
            if (pSignal != NULL) {
                    /* it was synchronous */      
                DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Sync-Op signal wait bypassed \n"));
                    /* NULL out completion info, there's no need to
                     * signal the semaphore */
                pReq->pCompletion = NULL;
                
            } else {
                DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Async operation completed in-line \n"));
                    /* this was an async call, always return pending */
                status = SDIO_STATUS_PENDING;   
            }
                /* process this completed transfer on behalf of the HCD */
            _SDIO_HandleHcdEvent(pHcd, EVENT_HCD_TRANSFER_DONE);
            
                /* done processing */
            break;            
        }
                /* I/O is now pending, could be sync or async */
                /* check for synch op */
        if (pSignal != NULL) {  
                /* wait for completion */
            DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Sync-Op signal waiting....\n"));
                /* this is not interruptable, as the HCD must complete it. */
            status = SignalWait(&pSignal->Signal);
                /* don't need the signal anymore */
            FreeSignal(pSignal);    
            pSignal = NULL;
            
            /* note: it is safe to touch pReq since we own
             * the completion routine for synch transfers */
             
                /* check signal wait status */
            if (!SDIO_SUCCESS(status)) {
                DBG_PRINT(SDDBG_TRACE, 
                ("SDIO Bus Driver - IssueRequestToHCD: Synch transfer - signal wait failed, cancelling req 0X%X\n", 
                (UINT)pReq)); 
                pReq->Status = SDIO_STATUS_CANCELED;
                status = SDIO_STATUS_CANCELED;
                break;   
            }
            DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Sync-Op woke up\n")); 
                /* return the completion status of the request */
            status = pReq->Status;  
        } else {
            DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Async operation Pending \n"));   
        }
            
    } while (FALSE);
   
        /* see if we need to clean up failed submissions */     
    if (handleFailedReqSubmit) {
            /* make sure this is cleared */
        AtomicTest_Clear(&pReq->InternalFlags, SDBD_PENDING);
            /* the  request processing failed before it was submitted to the HCD */
            /* note: since it never made it to the queue we can touch pReq */
        if (pReq->Flags & SDREQ_FLAGS_TRANS_ASYNC) {
            /* for ASYNC requests, we need to call the completion routine */
            DoRequestCompletion(pReq, pHcd);
                /* return pending for all ASYNC requests */ 
            status = SDIO_STATUS_PENDING;
        }         
    }

        /* check if we need to clean up the signal */
    if (pSignal != NULL) {
            /* make sure this is freed */
        FreeSignal(pSignal);    
    }
        /* return status */
    return status;
}


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Unmask the function's IRQ
  
  @function name: SDCONFIG_FUNC_UNMASK_IRQ
  @prototype: SDCONFIG_FUNC_UNMASK_IRQ 
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          unmasks the IRQ for the I/O function. This request sets the function's
          interrupt enable bit in the INTENABLE register in the
          common register space.
          
  @example: Example of unmasking interrupt :
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_UNMASK_IRQ,
                                   NULL,
                                   0);
                                   
  @see also: SDCONFIG_FUNC_MASK_IRQ  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Mask the function's IRQ
  
  @function name: SDCONFIG_FUNC_MASK_IRQ
  @prototype: SDCONFIG_FUNC_MASK_IRQ 
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          masks the IRQ for the I/O function.  
          
  @example: Example of unmasking interrupt :
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_MASK_IRQ,
                                   NULL,
                                   0);
                                   
  @see also: SDCONFIG_FUNC_UNMASK_IRQ  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Acknowledge that the function's IRQ has been handled
  
  @function name: SDCONFIG_FUNC_ACK_IRQ
  @prototype: SDCONFIG_FUNC_ACK_IRQ 
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          indicates to the bus driver that the function driver has handled the 
          interrupt.  The bus driver will notify the host controller to unmask the
          interrupt source.  SDIO interrupts are level triggered and are masked at the
          host controller level until all function drivers have indicated that they 
          have handled their respective interrupt. This command can be issued in either
          the IRQ handler or asynchronous IRQ handler.
          
  @example: Example of acknowledging an interrupt :
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_ACK_IRQ,
                                   NULL,
                                   0);
                                   
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the debug level of the underlying host controller driver.
  
  @function name: SDCONFIG_GET_HCD_DEBUG
  @prototype: SDCONFIG_GET_HCD_DEBUG
  @category: PD_Reference
  
  @input:  none
 
  @output: CT_DEBUG_LEVEL

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          requests the current debug level of the HCD driver.  This API is useful for
          saving the current debug level of the HCD prior to issuing SDCONFIG_SET_HCD_DEBUG
          in order to increase the verbosity of the HCD. This API should be used only for
          debugging purposes.  If multiple functions attempt to save and set the HCD debug
          level simultanously, the final debug level will be unknown. Not all HCDs support
          this command.
          
  @example: Example of saving the debug level:
        CT_DEBUG_LEVEL savedDebug;
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_GET_HCD_DEBUG,
                                   &savedDebug,
                                   sizeof(savedDebug));
                                        
  @see also: SDCONFIG_SET_HCD_DEBUG                                  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/ 

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Set the debug level of the underlying host controller driver.
  
  @function name: SDCONFIG_SET_HCD_DEBUG
  @prototype: SDCONFIG_SET_HCD_DEBUG
  @category: PD_Reference
  
  @input:  CT_DEBUG_LEVEL
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          sets the current debug level of the HCD driver.  This API is useful for
          setting the debug level of the HCD programatically for debugging purposes. 
          If multiple functions attempt to save and set the HCD debug
          level simultanously, the final debug level will be unknown. Not all HCDs support
          this request.
          
  @example: Example of setting the debug level:
        CT_DEBUG_LEVEL setDebug = 15;
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_GET_HCD_DEBUG,
                                   &setDebug,
                                   sizeof(setDebug));
                                        
  @see also: SDCONFIG_GET_HCD_DEBUG                                  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/ 


