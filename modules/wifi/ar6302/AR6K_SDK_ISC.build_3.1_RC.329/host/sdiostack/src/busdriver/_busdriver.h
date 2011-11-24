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
@file: _busdriver.h

@abstract: internal include file for busdriver
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef ___BUSDRIVER_H___
#define ___BUSDRIVER_H___
#include "../include/sdio_lib.h"

#define SDIODBG_FUNC_IRQ  (SDDBG_TRACE + 1)
#define SDIODBG_REQUESTS  (SDDBG_TRACE + 2)
#define SDIODBG_CD_TIMER  (SDDBG_TRACE + 3)
#define SDIODBG_HCD_EVENTS  (SDDBG_TRACE + 4)

#define SDIOBUS_CD_TIMER_ID 0

#define SDBUS_MAX_RETRY   3

/* Notes on list linkages:
 *  list heads are held in BDCONTEXT
 *  HcdList - SDHCD
 *          one per registered host controller
 *          Next - links of all HCDs
 *  DeviceList SDDEVICE  
 *          one per inserted device
 *          Next - links of all devices
 *          DeviceListNext - links of all devices on a function
 *          pFunction - ptr to Function supportting this device
 *          pHcd - ptr to HCD with supporting this device
 *  FunctionList SDFUNCTION
 *          one per register function driver
 *          Next - links of all functions
 *          DeviceList - list of devices being support by this function
 *                       uses DeviceListNext in SDDEVICE to link
 * 
 * 
*/

#define SDMMC_DEFAULT_CMD_RETRIES           1
#define SDMMC_DEFAULT_CARD_READY_RETRIES    200 
#define OCR_READY_CHECK_DELAY_MS            10 
#define SDMMC_POWER_SETTLE_DELAY            400  /* in milliseconds */
#define SDBUS_DEFAULT_REQ_LIST_SIZE         16
#define SDBUS_DEFAULT_REQ_SIG_SIZE          8
#define CARD_DETECT_PAUSE                   100
#define SDBUS_DEFAULT_CD_POLLING_INTERVAL   1000  /* in milliseconds */
#define MAX_CARD_DETECT_MSGS                16   
#define SDMMC_DEFAULT_BYTES_PER_BLOCK       2048
#define SDMMC_DEFAULT_BLOCKS_PER_TRANS      512
#define SDMMC_CMD13_POLLING_MULTIPLIER      1000 /* per block multiplier */
#define MAX_HCD_REQ_RECURSION               5 
#define MAX_HCD_RECURSION_RUNAWAY           100

    /* internal signalling item */
typedef struct _SIGNAL_ITEM{
    SDLIST       SDList;        /* list link*/
    OS_SIGNAL    Signal;        /* signal */
}SIGNAL_ITEM, *PSIGNAL_ITEM;

typedef struct _HCD_EVENT_MESSAGE {
    HCD_EVENT Event;    /* the event */
    PSDHCD    pHcd;     /* hcd that generated the event */
}HCD_EVENT_MESSAGE, *PHCD_EVENT_MESSAGE;

/* internal data for bus driver */
typedef struct _BDCONTEXT {
   
    /* list of SD requests and signalling semaphores and a semaphore to protect it */
    SDLIST  RequestList;
    SDLIST  SignalList;
    OS_CRITICALSECTION RequestListCritSection;
    /* list of host controller bus drivers, sempahore to protect it */
    SDLIST HcdList;
    OS_SEMAPHORE HcdListSem;
    /* list of inserted devices, semaphore to protect it */
    SDLIST DeviceList;
    OS_SEMAPHORE DeviceListSem;
    /* list of function drivers, semaphore to protect it */
    SDLIST FunctionList;
    OS_SEMAPHORE FunctionListSem;
    INT              RequestListSize;        /* default request list */
    INT              SignalSemListSize;      /* default signalling semaphore size */
    INT              CurrentRequestAllocations; /*current count of allocated requests */
    INT              CurrentSignalAllocations;   /* current count of signal allocations */
    INT              MaxRequestAllocations;  /* max number of allocated requests to keep around*/
    INT              MaxSignalAllocations;   /* max number of signal allocations to keep around*/
    INT              RequestRetries;         /* cmd retries */
    INT              CardReadyPollingRetry;  /* card ready polling retry count */
    INT              PowerSettleDelay;       /* power settle delay */
    INT              CMD13PollingMultiplier; /* CMD13 (GET STATUS) multiplier */
    SD_BUSCLOCK_RATE DefaultOperClock;       /* default operation clock */
    SD_BUSMODE_FLAGS DefaultBusMode;         /* default bus mode */ 
    UINT16           DefaultOperBlockLen;    /* default operational block length per block */
    UINT16           DefaultOperBlockCount;  /* default operational block count per transaction */
    UINT32           CDPollingInterval;      /* card insert/removal polling interval */ 
    UINT8            InitMask;               /* bus driver init mask */
#define BD_TIMER_INIT    0x01
#define HELPER_INIT      0x02
#define RESOURCE_INIT    0x04
    BOOL             CDTimerQueued;          /* card detect timer queued */ 
    OSKERNEL_HELPER  CardDetectHelper;       /* card detect helper */ 
    PSDMESSAGE_QUEUE pCardDetectMsgQueue;    /* card detect message queue */
    ULONG            HcdInUseField;          /* bit field of in use HCD numbers*/
    UINT32           ConfigFlags;            /* bus driver configuration flags */
#define BD_CONFIG_SDREQ_FORCE_ALL_ASYNC 0x00000001
#define BD_CONFIG_SDIO_HS_NO_CLK_LMT    0x00000002   /* allow SDIO HS mode with no lower limit on clock */
#define BD_CONFIG_SDIO_HS_NO_HOST_CHNG  0x00000004
    INT              MaxHcdRecursion;        /* max HCD recurion level */
}BDCONTEXT, *PBDCONTEXT;

#define BD_DEFAULT_CONFIG_FLAGS 0x00000000
#define IsQueueBusy(pRequestQueue)      (pRequestQueue)->Busy 
#define MarkQueueBusy(pRequestQueue)    (pRequestQueue)->Busy = TRUE   
#define MarkQueueNotBusy(pRequestQueue) (pRequestQueue)->Busy = FALSE 

#define CLEAR_INTERNAL_REQ_FLAGS(pReq) (pReq)->Flags &= ~(UINT)((SDREQ_FLAGS_RESP_SPI_CONVERTED | \
                                                          SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE))
                                                          
/* macros to insert request into the queue */
#define QueueRequest(pReqQ,pReq) SDListInsertTail(&(pReqQ)->Queue,&(pReq)->SDList)
#define QueueRequestToFront(pReqQ,pReq) SDListInsertHead(&(pReqQ)->Queue,&(pReq)->SDList)

/* macros to remove an item from the head of the queue */
static INLINE PSDREQUEST DequeueRequest(PSDREQUESTQUEUE pRequestQueue) {
    PSDLIST pItem;
    pItem = SDListRemoveItemFromHead(&pRequestQueue->Queue);
    if (pItem != NULL) {
        return CONTAINING_STRUCT(pItem, SDREQUEST, SDList);        
    }
    return NULL;
};

static INLINE SDIO_STATUS InitializeRequestQueue(PSDREQUESTQUEUE pRequestQueue) {
    SDLIST_INIT(&pRequestQueue->Queue);  
    MarkQueueNotBusy(pRequestQueue);
    return SDIO_STATUS_SUCCESS;
}

static INLINE void CleanupRequestQueue(PSDREQUESTQUEUE pRequestQueue) {
    
}

/* for bus driver internal use only */
SDIO_STATUS _SDIO_BusDriverInitialize(void);
SDIO_STATUS _SDIO_BusGetDefaultSettings(PBDCONTEXT pBdc);
void _SDIO_BusDriverCleanup(void);
SDIO_STATUS RemoveAllFunctions(void);
SDIO_STATUS RemoveHcdFunctions(PSDHCD pHcd);
PSDDEVICE AllocateDevice(PSDHCD pHcd);
BOOL AddDeviceToList(PSDDEVICE pDevice);
SDIO_STATUS DeleteDevices(PSDHCD pHcd);
SDIO_STATUS NotifyDeviceRemove(PSDDEVICE pDevice);
extern PBDCONTEXT pBusContext;
extern const CT_VERSION_CODE g_Version;
SDIO_STATUS _SDIO_RegisterHostController(PSDHCD pHcd);
SDIO_STATUS _SDIO_UnregisterHostController(PSDHCD pHcd);
SDIO_STATUS _SDIO_HandleHcdEvent(PSDHCD pHcd, HCD_EVENT Event);
SDIO_STATUS _SDIO_RegisterFunction(PSDFUNCTION pFunction);
SDIO_STATUS _SDIO_UnregisterFunction(PSDFUNCTION pFunction);
SDIO_STATUS _SDIO_CheckResponse(PSDHCD pHcd, PSDREQUEST pReq, SDHCD_RESPONSE_CHECK_MODE CheckMode);
SDIO_STATUS ProbeForFunction(PSDDEVICE pDevice, PSDHCD pHcd);
SDIO_STATUS SDInitializeCard(PSDHCD pHcd);
SDIO_STATUS SDQuerySDMMCInfo(PSDDEVICE pDevice);
SDIO_STATUS SDQuerySDIOInfo(PSDDEVICE pDevice);
SDIO_STATUS SDEnableFunction(PSDDEVICE pDevice, PSDCONFIG_FUNC_ENABLE_DISABLE_DATA pEnData);
SDIO_STATUS SDAllocFreeSlotCurrent(PSDDEVICE pDevice, BOOL Allocate, PSDCONFIG_FUNC_SLOT_CURRENT_DATA pData);
SDIO_STATUS SDMaskUnmaskFunctionIRQ(PSDDEVICE pDevice, BOOL Mask);
SDIO_STATUS SDFunctionAckInterrupt(PSDDEVICE pDevice);
SDIO_STATUS SDSPIModeEnableDisableCRC(PSDDEVICE pDevice,BOOL Enable);
SDIO_STATUS IssueBusConfig(PSDDEVICE pDev, PSDCONFIG pConfig);
SDIO_STATUS IssueBusRequest(PSDDEVICE pDev, PSDREQUEST pReq);
PSDREQUEST IssueAllocRequest(PSDDEVICE pDev);
void IssueFreeRequest(PSDDEVICE pDev, PSDREQUEST pReq);
PSDREQUEST AllocateRequest(void);
void FreeRequest(PSDREQUEST pReq);
PSIGNAL_ITEM AllocateSignal(void);
void FreeSignal(PSIGNAL_ITEM pSignal);
SDIO_STATUS InitializeTimers(void);
SDIO_STATUS CleanupTimers(void);
SDIO_STATUS QueueTimer(INT TimerID, UINT32 TimeOut);
SDIO_STATUS DeviceAttach(PSDHCD pHcd); 
SDIO_STATUS DeviceDetach(PSDHCD pHcd);
SDIO_STATUS DeviceInterrupt(PSDHCD pHcd);
SDIO_STATUS CardInitSetup(PSDHCD pHcd);
void RunCardDetect(void);
void SDIO_NotifyTimerTriggered(INT TimerID);
SDIO_STATUS TestPresence(PSDHCD          pHcd, 
                         CARD_INFO_FLAGS TestType,
                         PSDREQUEST      pReq);              
#define _IssueSimpleBusRequest(pHcd,Cmd,Arg,Flags,pReqToUse) \
          _IssueBusRequestBd((pHcd),(Cmd),(Arg),(Flags),(pReqToUse),NULL,0) 

SDIO_STATUS Do_OS_IncHcdReference(PSDHCD pHcd);
SDIO_STATUS Do_OS_DecHcdReference(PSDHCD pHcd);
SDIO_STATUS TryNoIrqPendingCheck(PSDDEVICE pDev);

    /* check API version compatibility of an HCD or function driver to a stack major/minor version
     if the driver version is greater than the major number, we are compatible
     if the driver version is equal, then we check if the minor is greater than or equal
     we don't have to check for the less than major, because the bus driver never loads
     drivers with different major numbers ...
     if the busdriver compiled version major is greater than the major version being checked this
     macro will resolved to ALWAYS true thus optimizing the code to not check the HCD since
     as a rule we never load an HCD with a lower major number */
#define CHECK_API_VERSION_COMPAT(p,major,minor)       \
     ((CT_SDIO_STACK_VERSION_MAJOR(CT_SDIO_STACK_VERSION_CODE) > (major)) || \
      (GET_SDIO_STACK_VERSION_MINOR((p)) >= (minor)))

static INLINE SDIO_STATUS OS_IncHcdReference(PSDHCD pHcd) {
        /* this API was added in version 2.3 which requires access to a field in the HCD structure */
    if (CHECK_API_VERSION_COMPAT(pHcd,2,3)) {
            /* we can safely call the OS-dependent function */
        return Do_OS_IncHcdReference(pHcd);
    }
    return SDIO_STATUS_SUCCESS;
}

static INLINE SDIO_STATUS OS_DecHcdReference(PSDHCD pHcd) {
            /* this API was added in version 2.3 which requires access to a field in the HCD structure */
    if (CHECK_API_VERSION_COMPAT(pHcd,2,3)) {
            /* we can safely call the OS-dependent function */
        return Do_OS_DecHcdReference(pHcd);
    }
    return SDIO_STATUS_SUCCESS;
}

SDIO_STATUS _IssueBusRequestBd(PSDHCD           pHcd,
                               UINT8            Cmd,
                               UINT32           Argument,
                               SDREQUEST_FLAGS  Flags,
                               PSDREQUEST       pReqToUse,
                               PVOID            pData,
                               INT              Length);
                                           
SDIO_STATUS IssueRequestToHCD(PSDHCD pHcd,PSDREQUEST pReq);
                
#define CALL_HCD_CONFIG(pHcd,pCfg) (pHcd)->pConfigure((pHcd),(pCfg))
    /* macro to force all requests to be asynchronous in the HCD */
static INLINE BOOL ForceAllRequestsAsync(void) {
    return (pBusContext->ConfigFlags & BD_CONFIG_SDREQ_FORCE_ALL_ASYNC);
}

static INLINE SDIO_STATUS CallHcdRequest(PSDHCD pHcd) {
    
    if (pHcd->pCurrentRequest->Flags & SDREQ_FLAGS_PSEUDO) {
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: PSEUDO Request 0x%X \n",
                    (INT)pHcd->pCurrentRequest));   
            /* return successful completion so that processing can finish */
        return SDIO_STATUS_SUCCESS;
    }
    
    if (ForceAllRequestsAsync()) {
            /* all requests must be completed(indicated) in a separate context */
        pHcd->pCurrentRequest->Flags |= SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE;    
    } else {
            /* otherwise perform a test on flags in the HCD */
        if (!CHECK_API_VERSION_COMPAT(pHcd,2,6) && 
            AtomicTest_Set(&pHcd->HcdFlags, HCD_REQUEST_CALL_BIT)) {

            /* bit was already set, this is a recursive call, 
             * we need to tell the HCD to complete the 
             * request in a separate context */
            DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Recursive CallHcdRequest \n"));
            pHcd->pCurrentRequest->Flags |= SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE;
        }
    }
#ifdef DEBUG
    {       
        SDIO_STATUS status;
        BOOL forceDeferred;
        forceDeferred = pHcd->pCurrentRequest->Flags & SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE;
        status = pHcd->pRequest(pHcd);    
        if (forceDeferred) {
                /* status better be pending... */
            DBG_ASSERT(status == SDIO_STATUS_PENDING);
        }
        return status;  
    }
#else 
    return pHcd->pRequest(pHcd);
#endif
    
}

/* note the caller of this macro must take the HCD lock to protect the count */
#define CHECK_HCD_RECURSE(pHcd,pReq)   \
{                                      \
    (pHcd)->Recursion++;               \
    DBG_ASSERT((pHcd)->Recursion < MAX_HCD_RECURSION_RUNAWAY); \
    if ((pHcd)->Recursion > pBusContext->MaxHcdRecursion) {    \
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Recursive Request Count Exceeded (%d) \n",(pHcd)->Recursion)); \
        (pReq)->Flags |= SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE; \
    }                                                         \
}

/* InternalFlags bit number settings */
#define SDBD_INIT        1
#define SDBD_PENDING    15
#define SDBD_ALLOC_IRQ_SAFE     2

#define SDBD_ALLOC_IRQ_SAFE_MASK (1 << SDBD_ALLOC_IRQ_SAFE)

static void INLINE DoRequestCompletion(PSDREQUEST pReq, PSDHCD pHcd) {
    CLEAR_INTERNAL_REQ_FLAGS(pReq);
    if (pReq->pCompletion != NULL) {
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Calling completion on request:0x%X, CMD:%d \n",
           (INT)pReq, pReq->Command));  
            /* call completion routine, mark request reusable */
        AtomicTest_Clear(&pReq->InternalFlags, SDBD_PENDING);
        pReq->pCompletion(pReq);  
    } else {
            /* mark request reusable */
        AtomicTest_Clear(&pReq->InternalFlags, SDBD_PENDING);
    }
}

THREAD_RETURN CardDetectHelperFunction(POSKERNEL_HELPER pHelper);
THREAD_RETURN SDIOIrqHelperFunction(POSKERNEL_HELPER pHelper);

void ConvertSPI_Response(PSDREQUEST pReq, UINT8 *pRespBuffer);

static INLINE SDIO_STATUS PostCardDetectEvent(PBDCONTEXT pSDB, HCD_EVENT Event, PSDHCD pHcd) {
    HCD_EVENT_MESSAGE message;
    SDIO_STATUS       status;
    message.Event = Event;
    message.pHcd = pHcd; 
    
    if (pHcd != NULL) {
            /* increment HCD reference count to process this HCD message */
        status = OS_IncHcdReference(pHcd);        
        if (!SDIO_SUCCESS(status)) {
            return status;    
        }
    }
        /* post card detect message */
    status = SDLIB_PostMessage(pSDB->pCardDetectMsgQueue, &message, sizeof(message));
    if (!SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: PostCardDetectEvent error status %d\n",status));
        if (pHcd != NULL) {
                /* decrement count */
            OS_DecHcdReference(pHcd);
        }
        return status;   
    }
        /* wake card detect helper */
    DBG_PRINT(SDIODBG_HCD_EVENTS, ("SDIO Bus Driver: PostCardDetectEvent waking\n"));
    return SD_WAKE_OS_HELPER(&pSDB->CardDetectHelper); 
};

/* initialize device fields */
static INLINE void InitDeviceData(PSDHCD pHcd, PSDDEVICE pDevice) {
    ZERO_POBJECT(pDevice);
    SDLIST_INIT(&pDevice->SDList);
    SDLIST_INIT(&pDevice->FuncListLink);
    pDevice->pRequest = IssueBusRequest;
    pDevice->pConfigure = IssueBusConfig;
    pDevice->AllocRequest = IssueAllocRequest;
    pDevice->FreeRequest = IssueFreeRequest;
        /* set card flags in the ID */
    pDevice->pId[0].CardFlags = pHcd->CardProperties.Flags;
    pDevice->pFunction = NULL;
    pDevice->pHcd = pHcd;   
    SET_SDIO_STACK_VERSION(pDevice);
}

/* de-initialize device fields */
static INLINE void DeinitDeviceData(PSDDEVICE pDevice) {
}

/* reset hcd state */
static INLINE void ResetHcdState(PSDHCD pHcd) {
    ZERO_POBJECT(&pHcd->CardProperties);  
    pHcd->PendingHelperIrqs = 0;  
    pHcd->PendingIrqAcks = 0;     
    pHcd->IrqsEnabled = 0; 
    pHcd->pCurrentRequest = NULL;
    pHcd->IrqProcState = SDHCD_IDLE;
        /* mark this device as special */
    pHcd->pPseudoDev->pId[0].CardFlags = CARD_PSEUDO;
    pHcd->SlotCurrentAllocated = 0;
}

static INLINE SDIO_STATUS _IssueConfig(PSDHCD           pHcd,
                                       SDCONFIG_COMMAND Command,
                                       PVOID            pData,
                                       INT              Length){
    SDCONFIG  configHdr; 
    SET_SDCONFIG_CMD_INFO(&configHdr,Command,pData,Length);
    return CALL_HCD_CONFIG(pHcd,&configHdr);
}

/* prototypes */
#define _AcquireHcdLock(pHcd)CriticalSectionAcquireSyncIrq(&(pHcd)->HcdCritSection)
#define _ReleaseHcdLock(pHcd)CriticalSectionReleaseSyncIrq(&(pHcd)->HcdCritSection)

#define AcquireHcdLock(pDev) CriticalSectionAcquireSyncIrq(&(pDev)->pHcd->HcdCritSection)
#define ReleaseHcdLock(pDev) CriticalSectionReleaseSyncIrq(&(pDev)->pHcd->HcdCritSection)

SDIO_STATUS OS_AddDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction);
void OS_RemoveDevice(PSDDEVICE pDevice);
SDIO_STATUS OS_InitializeDevice(PSDDEVICE pDevice, PSDFUNCTION pFunction);
SDIO_STATUS SetOperationalBusMode(PSDDEVICE               pDevice, 
                                  PSDCONFIG_BUS_MODE_DATA pBusMode); 
void FreeDevice(PSDDEVICE pDevice);
BOOL IsPotentialIdMatch(PSD_PNP_INFO pIdsDev, PSD_PNP_INFO pIdsFuncList);


#define CHECK_FUNCTION_DRIVER_VERSION(pF) \
    (GET_SDIO_STACK_VERSION_MAJOR((pF)) == CT_SDIO_STACK_VERSION_MAJOR(g_Version))   
#define CHECK_HCD_DRIVER_VERSION(pH) \
    (GET_SDIO_STACK_VERSION_MAJOR((pH)) == CT_SDIO_STACK_VERSION_MAJOR(g_Version))                                                       

/* CLARIFICATION on SDREQ_FLAGS_PSEUDO and SDREQ_FLAGS_BARRIER flags :
 * 
 * A request marked as PSEUDO is synchronized with bus requests and is not a true request
 * that is issued to an HCD.
 * 
 * A request marked with a BARRIER flag requires that the completion routine be called
 * before the next bus request starts.  This is required for HCD requests that can change 
 * bus or clock modes.  Changing the clock or bus mode while a bus request is pending 
 * can cause problems.
 * 
 * 
 * 
 * */
#define SD_PSEUDO_REQ_FLAGS \
      (SDREQ_FLAGS_PSEUDO | SDREQ_FLAGS_BARRIER | SDREQ_FLAGS_TRANS_ASYNC)      
                                 
#endif /*___BUSDRIVER_H___*/
