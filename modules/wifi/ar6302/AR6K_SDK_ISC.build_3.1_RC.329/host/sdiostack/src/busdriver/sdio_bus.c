// Copyright (c) 2004-2006 Atheros Communications Inc.
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
@file: sdio_bus.c

@abstract: OS independent bus driver support
@category abstract: HD_Reference Host Controller Driver Interfaces.
@category abstract: PD_Reference
    Peripheral Driver Interfaces.

#notes: this file supports the HCD's and generic functions 

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define MODULE_NAME  SDBUSDRIVER
#include "../include/ctsystem.h"
#include "../include/sdio_busdriver.h"
#include "../include/_sdio_defs.h"
#include "../include/sdio_lib.h"
#include "../include/mmc_defs.h"
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
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: Version: %d.%d\n",
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
        pBusContext->RequestRetries = SDMMC_DEFAULT_CMD_RETRIES;
        pBusContext->CardReadyPollingRetry = SDMMC_DEFAULT_CARD_READY_RETRIES;
        pBusContext->PowerSettleDelay = SDMMC_POWER_SETTLE_DELAY;
        pBusContext->DefaultOperClock = MMC_HS_MAX_BUS_CLOCK;
        pBusContext->DefaultBusMode = SDCONFIG_BUS_WIDTH_4_BIT;
        pBusContext->RequestListSize = SDBUS_DEFAULT_REQ_LIST_SIZE;
        pBusContext->SignalSemListSize = SDBUS_DEFAULT_REQ_SIG_SIZE;
        pBusContext->CDPollingInterval = SDBUS_DEFAULT_CD_POLLING_INTERVAL;
        pBusContext->DefaultOperBlockLen = SDMMC_DEFAULT_BYTES_PER_BLOCK;
        pBusContext->DefaultOperBlockCount = SDMMC_DEFAULT_BLOCKS_PER_TRANS;
        pBusContext->ConfigFlags = BD_DEFAULT_CONFIG_FLAGS;
        pBusContext->CMD13PollingMultiplier = SDMMC_CMD13_POLLING_MULTIPLIER;      
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
          
        status = InitializeTimers();        
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: _SDIO_BusDriverInitialize can't InitializeTimers.\n"));
            break;        
        }
        pBusContext->InitMask |= BD_TIMER_INIT;                 
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
    
    if (pBusContext->InitMask & BD_TIMER_INIT) {
        CleanupTimers();   
    }
    
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
            /* create SDIO Irq helper */       
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
        if (pHcd->Attributes & SDHCD_ATTRIB_SLOT_POLLING) {
                /* post message to card detect helper to do polling */
            PostCardDetectEvent(pBusContext, EVENT_HCD_CD_POLLING, NULL);      
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

void DestroySignal(PSIGNAL_ITEM pSignal)
{
   SignalDelete(&pSignal->Signal);      
   KernelFree(pSignal);  
}

PSIGNAL_ITEM BuildSignal(void)
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
            case SDCONFIG_FUNC_ENABLE_DISABLE:
                if (cmdLength < sizeof(SDCONFIG_FUNC_ENABLE_DISABLE_DATA)) {
                    break;   
                }
                status = SDEnableFunction(pDev, 
                           GET_SDCONFIG_CMD_DATA(PSDCONFIG_FUNC_ENABLE_DISABLE_DATA,pConfig));            
                break;
            case SDCONFIG_FUNC_UNMASK_IRQ:
                status = SDMaskUnmaskFunctionIRQ(pDev,FALSE);
                break;
            case SDCONFIG_FUNC_MASK_IRQ:
                status = SDMaskUnmaskFunctionIRQ(pDev,TRUE);
                break;
            case SDCONFIG_FUNC_SPI_MODE_DISABLE_CRC:
                status = SDSPIModeEnableDisableCRC(pDev,FALSE);
                break;
            case SDCONFIG_FUNC_SPI_MODE_ENABLE_CRC:
                status = SDSPIModeEnableDisableCRC(pDev,TRUE);
                break;
            case SDCONFIG_FUNC_ALLOC_SLOT_CURRENT:
                status = SDAllocFreeSlotCurrent(pDev,
                                                TRUE,
                                   GET_SDCONFIG_CMD_DATA(PSDCONFIG_FUNC_SLOT_CURRENT_DATA,pConfig));
                break;
            case SDCONFIG_FUNC_FREE_SLOT_CURRENT:
                status = SDAllocFreeSlotCurrent(pDev, FALSE, NULL);
                break;
            case SDCONFIG_FUNC_CHANGE_BUS_MODE:
            
                status = SetOperationalBusMode(pDev,
                                               GET_SDCONFIG_CMD_DATA(PSDCONFIG_BUS_MODE_DATA,
                                               pConfig));
                break;
            case SDCONFIG_FUNC_NO_IRQ_PEND_CHECK:
                status = TryNoIrqPendingCheck(pDev);
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

         if (status == SDIO_STATUS_FUNC_ENABLE_TIMEOUT ) {
                debugLevel = SDDBG_TRACE; /* reduce debug level to avoid timeout error messages */
         }
         
                              
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


/* used by CMD12,CMD13 to save the original completion routine */
#define GET_BD_RSV_REQUEST_COMPLETION(pR)   ((PSDEQUEST_COMPLETION)((pR)->pBdRsv1))
#define SET_BD_RSV_REQUEST_COMPLETION(pR,c) (pR)->pBdRsv1 = (PVOID)(c)

/* used by CMD12 processing to save/restore the original data transfer status */
#define GET_BD_RSV_ORIG_STATUS(pR)          ((SDIO_STATUS)((pR)->pBdRsv2))
#define SET_BD_RSV_ORIG_STATUS(pR,s)        (pR)->pBdRsv2 = (PVOID)(s)

/* used by CMD13 processing to get/set polling count */
#define GET_BD_RSV_STATUS_POLL_COUNT(pR)     (INT)(pR)->pBdRsv2
#define SET_BD_RSV_STATUS_POLL_COUNT(pR,s)   (pR)->pBdRsv2 = (PVOID)(s)

/* used by CMD55 processing to save the second part of the request */
#define GET_BD_RSV_ORIG_REQ(pR)             (PSDREQUEST)(pR)->pBdRsv1
#define SET_BD_RSV_ORIG_REQ(pR,r)           (pR)->pBdRsv1 = (PVOID)(r)

/* used by all to save HCD */
#define GET_BD_RSV_HCD(pR)                  (PSDHCD)(pR)->pBdRsv3
#define SET_BD_RSV_HCD(pR,h)                (pR)->pBdRsv3 = (PVOID)(h)

static void CMD13CompletionBarrier(PSDREQUEST pReq);

static INLINE void SetupCMD13(PSDHCD pHcd, PSDREQUEST pReq) 
{
    pReq->Command = CMD13;
        /* sequence must be atomic, queue it to the head and flag as a barrier */
    pReq->Flags = SDREQ_FLAGS_QUEUE_HEAD | SDREQ_FLAGS_BARRIER | SDREQ_FLAGS_TRANS_ASYNC;
    if (IS_HCD_BUS_MODE_SPI(pHcd)) {
        pReq->Argument = 0;
        pReq->Flags |= SDREQ_FLAGS_RESP_R2;
    } else {
        pReq->Flags |= SDREQ_FLAGS_RESP_R1;
        pReq->Argument |= pHcd->CardProperties.RCA << 16;  
    }
        /* insert completion */
    pReq->pCompletion = CMD13CompletionBarrier; 
}

/* CMD13 (GET STATUS) completion */
static void CMD13CompletionBarrier(PSDREQUEST pReq) 
{
    PSDEQUEST_COMPLETION pOrigCompletion = GET_BD_RSV_REQUEST_COMPLETION(pReq); 
    PSDHCD               pHcd = GET_BD_RSV_HCD(pReq);
    INT                  pollingCount = GET_BD_RSV_STATUS_POLL_COUNT(pReq);
    BOOL                 doCompletion = TRUE;
    UINT32               cardStatus;
    
    DBG_ASSERT(pOrigCompletion != NULL);
    DBG_ASSERT(pHcd != NULL);
    DBG_PRINT(SDIODBG_REQUESTS, ("+SDIO Bus Driver: CMD13CompletionBarrier (cnt:%d) \n",pollingCount));
    
    do {
        if (!SDIO_SUCCESS(pReq->Status)) {
            break;    
        }
        
        cardStatus = SD_R1_GET_CARD_STATUS(pReq->Response);
        
        if (cardStatus & SD_CS_TRANSFER_ERRORS) {
            DBG_PRINT(SDIODBG_REQUESTS,("SDIO Bus Driver: Card transfer errors : 0x%X \n",cardStatus));
            pReq->Status = SDIO_STATUS_PROGRAM_STATUS_ERROR;
            break;        
        }
        
        if (SD_CS_GET_STATE(cardStatus) != SD_CS_STATE_PRG) {
            DBG_PRINT(SDIODBG_REQUESTS,("SDIO Bus Driver: Card programming done \n"));
            break;    
        }
        
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Card still programming.. \n"));
        pollingCount--;
        
        if (pollingCount < 0) {
            pReq->Status = SDIO_STATUS_PROGRAM_TIMEOUT;
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: card programming timeout!\n"));
            break;  
        }
        
        doCompletion = FALSE;
            /* keep trying */
        SET_BD_RSV_STATUS_POLL_COUNT(pReq, pollingCount);
        SetupCMD13(pHcd,pReq);
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: re-issuing CMD13 \n"));
            /* re-issue */
        IssueRequestToHCD(pHcd, pReq);
        
    } while (FALSE);
    
    
    if (doCompletion) {
            /* restore original completion routine */
        pReq->pCompletion = pOrigCompletion; 
            /* call original completion routine */      
        pOrigCompletion(pReq);
    }
    
    DBG_PRINT(SDIODBG_REQUESTS, ("-SDIO Bus Driver: CMD13CompletionBarrier \n"));
}

/* command 13 (GET STATUS) preparation */
static void PrepCMD13Barrier(PSDREQUEST pReq)
{
    SDIO_STATUS status = pReq->Status;
    PSDHCD      pHcd = GET_BD_RSV_HCD(pReq);
    INT         pollingCount;
    PSDEQUEST_COMPLETION pOrigCompletion = GET_BD_RSV_REQUEST_COMPLETION(pReq); 
    
    DBG_ASSERT(pHcd != NULL);
    DBG_ASSERT(pOrigCompletion != NULL);
    
    DBG_PRINT(SDIODBG_REQUESTS, ("+SDIO Bus Driver: PrepCMD13Barrier \n")); 
    
    if (SDIO_SUCCESS(status)) {
            /* re-use the request for CMD13 */
        SetupCMD13(pHcd,pReq);
            /* set polling count to a multiple of the Block count, if the BlockCount was
             * zeroed by the HCD, then set it to 1X multiplier */
        pollingCount = max(pBusContext->CMD13PollingMultiplier, 
                           pBusContext->CMD13PollingMultiplier * (INT)pReq->BlockCount);
            /* initialize count */
        SET_BD_RSV_STATUS_POLL_COUNT(pReq, pollingCount);
            /* re-issue it, we can call IssueRequest here since we are re-using the request */
        IssueRequestToHCD(pHcd, pReq);   
    } else {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Request Failure (%d) , CMD13 bypassed.\n",status));
            /* call the original completion routine */
        pOrigCompletion(pReq);      
    }
    
    DBG_PRINT(SDIODBG_REQUESTS, ("-SDIO Bus Driver: PrepCMD13Barrier (%d) \n",status));
}

/* CMD12 completion */
static void CMD12Completion(PSDREQUEST pReq) 
{
    PSDEQUEST_COMPLETION pOrigCompletion = GET_BD_RSV_REQUEST_COMPLETION(pReq); 

    DBG_ASSERT(pOrigCompletion != NULL);
    
    DBG_PRINT(SDIODBG_REQUESTS, ("+SDIO Bus Driver: CMD12Completion \n"));
    
        /* restore original completion routine */
    pReq->pCompletion = pOrigCompletion;
    
    if (SDIO_SUCCESS(pReq->Status)) {
            /* if CMD12 succeeds, we want to return the result of the original
             * request */
        pReq->Status = GET_BD_RSV_ORIG_STATUS(pReq); 
        DBG_PRINT(SDIODBG_REQUESTS, 
                ("SDIO Bus Driver: PrepCMD12Completion original status %d \n",pReq->Status));
    }
        /* call original completion routine */      
    pOrigCompletion(pReq);
    
    DBG_PRINT(SDIODBG_REQUESTS, ("-SDIO Bus Driver: CMD12Completion \n"));
}

/* CMD12 preparation */
static void PrepCMD12Barrier(PSDREQUEST pReq) 
{
    
    SDIO_STATUS status = pReq->Status;
    PSDHCD               pHcd = GET_BD_RSV_HCD(pReq);
    PSDEQUEST_COMPLETION pOrigCompletion = GET_BD_RSV_REQUEST_COMPLETION(pReq); 
    
    DBG_ASSERT(pHcd != NULL);
    DBG_ASSERT(pOrigCompletion != NULL);
    
    DBG_PRINT(SDIODBG_REQUESTS, ("+SDIO Bus Driver: PrepCMD12Barrier \n")); 
    
    if (SDIO_SUCCESS(status) ||    /* only issue CMD12 on success or specific bus errors */
        (SDIO_STATUS_BUS_READ_TIMEOUT == status) ||
        (SDIO_STATUS_BUS_READ_CRC_ERR == status) || 
        (SDIO_STATUS_BUS_WRITE_ERROR == status)) {
        if (!CHECK_API_VERSION_COMPAT(pHcd,2,6)) {
            if (!ForceAllRequestsAsync()) {
                /* clear the call bit as an optimization, note clearing it wholesale here will 
                 * allow request processing to recurse one more level */
                AtomicTest_Clear(&pHcd->HcdFlags, HCD_REQUEST_CALL_BIT);
            }
        }
            /* re-use the request for CMD12 */
        pReq->Command = CMD12;
        pReq->Argument = 0;
        
            /* if the data transfer was successful, check for transfer check */
        if (SDIO_SUCCESS(status) &&
            (pReq->Flags & SDREQ_FLAGS_AUTO_TRANSFER_STATUS)) {
                /* original data request requires a transfer status check, which is another
                 * barrier request */
            pReq->Flags = SDREQ_FLAGS_RESP_R1B | SDREQ_FLAGS_QUEUE_HEAD | SDREQ_FLAGS_BARRIER |
                          SDREQ_FLAGS_TRANS_ASYNC;
            DBG_PRINT(SDIODBG_REQUESTS, ("-SDIO Bus Driver: PrepCMD12Barrier , chaining CMD13 \n"));
                /* switch out completion to send the CMD13 next */
            pReq->pCompletion = PrepCMD13Barrier;  
        } else {
            pReq->Flags = SDREQ_FLAGS_RESP_R1B | SDREQ_FLAGS_QUEUE_HEAD | SDREQ_FLAGS_TRANS_ASYNC;
            pReq->pCompletion = CMD12Completion;
        }
        
            /* save the original data transfer request status */
        SET_BD_RSV_ORIG_STATUS(pReq,status); 
            /* re-issue it, we can call IssueRequest here since we are re-using the request */
        IssueRequestToHCD(pHcd, pReq);   
    } else {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Request Failure (%d) , CMD12 bypassed.\n",status));
            /* call the original completion routine */
        pOrigCompletion(pReq);      
    }
    
    DBG_PRINT(SDIODBG_REQUESTS, ("-SDIO Bus Driver: PrepCMD12Barrier (%d) \n",status));
}


/* CMD55 barrier - this is a special barrier completion routine, we have to submit the second 
 * part of the command command sequence atomically */
static void CMD55CompletionBarrier(PSDREQUEST pReq)
{
    SDIO_STATUS status = pReq->Status;
    PSDREQUEST  pOrigReq = GET_BD_RSV_ORIG_REQ(pReq);
    PSDHCD      pHcd = GET_BD_RSV_HCD(pReq);
    BOOL        doCompletion = FALSE;
    
    DBG_ASSERT(pOrigReq != NULL);
    DBG_ASSERT(pHcd != NULL);
    
    DBG_PRINT(SDIODBG_REQUESTS, ("+SDIO Bus Driver: CMD55Completion \n")); 
    
    do {
        
        if (!SDIO_SUCCESS(status)) {
                /* command 55 failed */
            pOrigReq->Status = status;
            doCompletion = TRUE;
            break;    
        }
        
        if (!(SD_R1_GET_CARD_STATUS(pReq->Response) & SD_CS_APP_CMD)) {                  
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Card is not accepting CMD55, status:0x%X \n",
                    SD_R1_GET_CARD_STATUS(pReq->Response))); 
            pOrigReq->Status = SDIO_STATUS_INVALID_COMMAND;
            doCompletion = TRUE;
            break;
        }
        
        if (!CHECK_API_VERSION_COMPAT(pHcd,2,6)) {
            if (!ForceAllRequestsAsync()) {
                AtomicTest_Clear(&pHcd->HcdFlags, HCD_REQUEST_CALL_BIT);
            }
        }
        
            /* flag the original request to queue to the head */
        pOrigReq->Flags |= SDREQ_FLAGS_QUEUE_HEAD;
            /* submit original request, we cannot call IssueRequestHCD() here because the
             * original request has already gone through IssueRequestHCD() already */
        status = StartHcdRequest(pHcd, pOrigReq);
        
        if (SDIO_STATUS_PENDING == status) {
            break;    
        }
        
        pOrigReq->Status = status;
       
        if (SDIO_STATUS_SDREQ_QUEUE_FAILED == status) {
                /* never made it to the queue */
            doCompletion = TRUE;
            break;
        } 
        
            /* request completed in-line */
        _SDIO_HandleHcdEvent(pHcd, EVENT_HCD_TRANSFER_DONE);   
         
    } while (FALSE);
    
    if (doCompletion) {
        DoRequestCompletion(pOrigReq, pHcd);        
    }
    
        /* free the CMD55 request */
    FreeRequest(pReq);  
    
    DBG_PRINT(SDIODBG_REQUESTS, ("-SDIO Bus Driver: CMD55Completion \n"));
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
 * 
 * The following flags are handled internally by the bus driver to guarantee atomicity.
 * 
 *    SDREQ_FLAGS_APP_CMD - SD Extended commands requiring CMD55 to precede the actual command
 *    SDREQ_FLAGS_AUTO_CMD12 - Memory Card Data transfer needs CMD12 to stop transfer 
 *                             (multi-block reads/writes)
 *    SDREQ_FLAGS_AUTO_TRANSFER_STATUS - Memory card data transfer needs transfer status polling 
 *                                       using CMD13
 * 
 *    These request flags require additional commands prepended or appended to the original command
 *    
 *    The order of command execution :
 * 
 *    Order  Condition                 Command Issued
 *    ------------------------------------------------------------- 
 *      1.   If APP_CMD                CMD55 issued.
 *      2.   Always                    Caller command issued.
 *      3.   If AUTO_CMD12             CMD12 issued.
 *      4.   If AUTO_TRANSFER_STATUS   CMD13 issued until card programming is complete
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
        if (internal & (1<<SDBD_PENDING)) {
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
        
        if ((pReq->Flags & SDREQ_FLAGS_AUTO_CMD12) &&        
            !(pHcd->Attributes & SDHCD_ATTRIB_AUTO_CMD12) &&
            !(IS_HCD_BUS_MODE_SPI(pHcd) && IS_SDREQ_WRITE_DATA(pReq->Flags))) {
            DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Auto CMD12 on Request:0x%08X \n",(INT)pReq));
                /* caller wants CMD12 auto-issued and the HCD does not support it */
                /* setup caller's request as a barrier and replace their completion routine */
            pReq->Flags |= SDREQ_FLAGS_BARRIER;
                /* take off the flag, since the BD will be issuing it */
            pReq->Flags &= ~SDREQ_FLAGS_AUTO_CMD12;
                /* save original completion */
            SET_BD_RSV_REQUEST_COMPLETION(pReq,pReq->pCompletion);
                /* save the HCD we are on */
            SET_BD_RSV_HCD(pReq,pHcd);
                /* use completion for preping CMD12 */
            pReq->pCompletion = PrepCMD12Barrier;               
        }
        
        if (pReq->Flags & SDREQ_FLAGS_AUTO_TRANSFER_STATUS) {
            /* caller wants transfer status checked. If a CMD12
             * barrier request has been setup we let the CMD12 completion take care
             * of setting up the transfer check */    
            if (pReq->pCompletion != PrepCMD12Barrier) {
                    /* make CMD13 prep a barrier */
                pReq->Flags |= SDREQ_FLAGS_BARRIER;
                    /* save original completion */
                SET_BD_RSV_REQUEST_COMPLETION(pReq,pReq->pCompletion);
                    /* save the HCD we are on */
                SET_BD_RSV_HCD(pReq,pHcd);
                    /* use completion for preping CMD13 */
                pReq->pCompletion = PrepCMD13Barrier;            
            }
        }
            
            /* check app command, the two command sequence must be handled atomically */
        if (pReq->Flags & SDREQ_FLAGS_APP_CMD) {            
            PSDREQUEST      pCmd55;
                /* allocate request to handle initial CMD55 command */
            pCmd55 = AllocateRequest();
            if (NULL == pCmd55) {
                status = SDIO_STATUS_NO_RESOURCES;
                pReq->Status = SDIO_STATUS_NO_RESOURCES;
                    /* complete the caller's request with error */
                handleFailedReqSubmit = TRUE;
                    /* no need to continue */
                break;
            }
                /* first submit CMD55 */
                /* set RCA */
            pCmd55->Argument = pHcd->CardProperties.RCA << 16;
                /* mark as a barrier request */
            pCmd55->Flags = SDREQ_FLAGS_RESP_R1 | SDREQ_FLAGS_BARRIER | SDREQ_FLAGS_TRANS_ASYNC; 
            pCmd55->Command = CMD55;
                /* call our barrier completion routine when done */
            pCmd55->pCompletion = CMD55CompletionBarrier;
                /* save request and target HCD */
            SET_BD_RSV_ORIG_REQ(pCmd55,pReq);
            SET_BD_RSV_HCD(pCmd55,pHcd);
                /* recursively start the CMD55 request, since the CMD55 is a barrier
                 * request, it's completion routine will submit the actual request 
                 * atomically */                           
            status = IssueRequestToHCD(pHcd, pCmd55);
              
        } else {
                /* start the normal request */
            status = StartHcdRequest(pHcd,pReq);                
        } 
        
         
        if (SDIO_STATUS_SDREQ_QUEUE_FAILED == status) {
            handleFailedReqSubmit = TRUE;
                /* no need to continue, clean up at the end */
            break; 
        }
        
            /* at this point, the request was either queued or was processed by the
             * HCD */
        
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: HCD returned status:%d on request: 0x%X, (CMD:%d) \n",
                  status, (INT)pReq, pReq->Command));   
                         
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

/* documentation for configuration requests */    
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Enable or Disable the SDIO Function
  
  @function name: SDCONFIG_FUNC_ENABLE_DISABLE
  @prototype: SDCONFIG_FUNC_ENABLE_DISABLE 
  @category: PD_Reference
  
  @input:  SDCONFIG_FUNC_ENABLE_DISABLE_DATA - Enable Data structure
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          uses the SDCONFIG_FUNC_ENABLE_DISABLE_DATA structure.  The caller must set the
          EnableFlags and specify the TimeOut value in milliseconds.   The TimeOut
          value is used for polling the I/O ready bit.  This command returns a status
          of SDIO_STATUS_FUNC_ENABLE_TIMEOUT if the ready bit was not set/cleared 
          by the card within the timeout period.
          
  @example: Example of enabling an I/O function:
        fData.EnableFlags = SDCONFIG_ENABLE_FUNC;
        fData.TimeOut = 500; 
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_ENABLE_DISABLE,
                                   &fData,
                                   sizeof(fData));
                                    
  @see also: SDLIB_IssueConfig
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/


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
  @function: Disable SD/MMC/SDIO card CRC checking.
  
  @function name: SDCONFIG_FUNC_SPI_MODE_DISABLE_CRC
  @prototype: SDCONFIG_FUNC_SPI_MODE_DISABLE_CRC 
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          issues CMD59 to disable SPI-CRC checking and requests the host controller 
          driver to stop checking the CRC. This is typically used in systems where 
          CRC checking is not required and performance is improved if the CRC checking
          is ommitted (i.e. SPI implementations without hardware CRC support).
          
  @example: Example of disabling SPI CRC checking:
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_SPI_MODE_DISABLE_CRC,
                                   NULL,
                                   0);
                                   
  @see also: SDCONFIG_FUNC_SPI_MODE_ENABLE_CRC                                  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Enable SD/MMC/SDIO card CRC checking.
  
  @function name: SDCONFIG_FUNC_SPI_MODE_ENABLE_CRC
  @prototype: SDCONFIG_FUNC_SPI_MODE_ENABLE_CRC 
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          issues CMD59 to enable SPI-CRC checking and requests the host controller 
          driver to generate valid CRCs for commands and data as well as
          check the CRC in responses and incomming data blocks. 
          
  @example: Example of enabling SPI CRC checking:
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_SPI_MODE_ENABLE_CRC,
                                   NULL,
                                   0);
                                   
  @see also: SDCONFIG_FUNC_SPI_MODE_DISABLE_CRC                                  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Allocate slot current for a card function.
  
  @function name: SDCONFIG_FUNC_ALLOC_SLOT_CURRENT
  @prototype: SDCONFIG_FUNC_ALLOC_SLOT_CURRENT
  @category: PD_Reference
  
  @input:  SDCONFIG_FUNC_SLOT_CURRENT_DATA
 
  @output: SDCONFIG_FUNC_SLOT_CURRENT_DATA

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          requests an allocation of slot current to satisfy the power requirements
          of the function.  The command uses the SDCONFIG_FUNC_SLOT_CURRENT_DATA 
          data structure to pass the required current in mA. Slot current allocation
          is not cummulative and this command should only be issued once by each function
          driver with the worse case slot current usage.
          The command returns SDIO_STATUS_NO_RESOURCES if the
          requirement cannot be met by the host hardware.  The SlotCurrent field will 
          contain the remaining current available to the slot.  The slot current should 
          be allocated before the function is enabled using SDCONFIG_FUNC_ENABLE_DISABLE.
          When a function driver is unloaded it should free the slot current allocation
          by using the SDCONFIG_FUNC_FREE_SLOT_CURRENT command.
          
  @example: Example of allocating slot current:
        slotCurrent.SlotCurrent = 150;  // 150 mA
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_ALLOC_SLOT_CURRENT,
                                   &slotCurrent,
                                   sizeof(slotCurrent));
                                   
                                   
  @see also: SDCONFIG_FUNC_FREE_SLOT_CURRENT                                  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Free slot current for a card function.
  
  @function name: SDCONFIG_FUNC_FREE_SLOT_CURRENT
  @prototype: SDCONFIG_FUNC_FREE_SLOT_CURRENT
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command
          frees the allocated current for a card function.  This command should be 
          issued only once (per function) and only after an allocation was successfully made.
          
  @example: Example of freeing slot current:
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_FREE_SLOT_CURRENT,
                                   NULL,
                                   0);
                                        
  @see also: SDCONFIG_FUNC_ALLOC_SLOT_CURRENT                                  
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/ 

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Set the bus mode for the SD/SDIO card.
  
  @function name: SDCONFIG_FUNC_CHANGE_BUS_MODE
  @prototype: SDCONFIG_FUNC_CHANGE_BUS_MODE
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes:     This command code is used in the SDLIB_IssueConfig() API.  The command
          alters the card's bus mode (width and clock rate) to a driver specified
          value.  The driver must read the current bus mode flags, modify if necessary
          and pass the value in the SDCONFIG_BUS_MODE_DATA structure. 
              If the bus width is changed (1 or 4 bit) the caller must adjust the mode flags 
          for the new width. Cards cannot be switched between 1/4 bit and SPI mode.  
          Switching to or from SPI mode requires a power cycle. Adjustments to the clock 
          rate is immediate on the next bus transaction.  The actual clock rate value is
          limited by the host controller and is reported in the ClockRate field when the
          command completes successfully.          
              The bus mode change is card wide and may affect other SDIO functions on 
          multi-function cards. Use this feature with caution. This feature should NOT be
          used to dynamically control clock rates during runtime and should only be used
          at card initialization. Changing the bus mode must be done with SDIO function 
          interrupts masked.  
              This request can block and must only be called from a schedulable context.
          
  @example: Example of changing the clock rate:
    SDCONFIG_BUS_MODE_DATA  busSettings;
    ZERO_OBJECT(busSettings);
       // get current bus flags and keep the same bus width
    busSettings.BusModeFlags = SDDEVICE_GET_BUSMODE_FLAGS(pInstance->pDevice);
    busSettings.ClockRate = 8000000;  // adjust clock to 8 Mhz
       // issue config request to override clock rate
    status = SDLIB_IssueConfig(pInstance->pDevice,
                               SDCONFIG_FUNC_CHANGE_BUS_MODE,
                               &busSettings,
                               sizeof(SDCONFIG_BUS_MODE_DATA)); 
                                        
  @see also: SDDEVICE_GET_BUSMODE_FLAGS                                  
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

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Instruct the bus driver to not check the SDIO card interrupt pending
             register on card interrupts, if possible.
  
  @function name: SDCONFIG_FUNC_NO_IRQ_PEND_CHECK
  @prototype: SDCONFIG_FUNC_NO_IRQ_PEND_CHECK
  @category: PD_Reference
  
  @input:  none
 
  @output: none

  @return: SDIO Status
 
  @notes: This command code is used in the SDLIB_IssueConfig() API.  The command instructs the
          bus driver to skip checking the card interrupt pending register on each card
          interrupt.  The bus driver will assume the function is interrupting and immediately start
          the interrupt processing stage. This option is only valid for single function cards.  
          The bus driver will reject the command for a card with more than 1 function. 
          For single function cards, this can improve interrupt response time.
          
  @example: Example of skipping IRQ pending checks:
       
        status = SDLIB_IssueConfig(pInstance->pDevice,
                                   SDCONFIG_FUNC_NO_IRQ_PEND_CHECK,
                                   NULL,
                                   0);
                                                                      
  @see also: SDLIB_IssueConfig 
      
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/ 
