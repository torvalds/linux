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
@file: sdio_bus_events.c

@abstract: OS independent bus driver support

#notes: this file contains various event handlers and helpers
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define MODULE_NAME  SDBUSDRIVER
#include "../include/ctsystem.h"
#include "../include/sdio_busdriver.h"
#include "../include/sdio_lib.h"
#include "_busdriver.h"
#include "../include/_sdio_defs.h"
#include "../include/mmc_defs.h"

static SDIO_STATUS ScanSlotForCard(PSDHCD pHcd,
                                   PBOOL  pCardPresent);
static void GetPendingIrqComplete(PSDREQUEST pReq);
static void ProcessPendingIrqs(PSDHCD  pHcd, UINT8 IntPendingMsk);

/* 
 * DeviceDetach - tell core a device was removed from a slot
*/
SDIO_STATUS DeviceDetach(PSDHCD pHcd) 
{    
    SDCONFIG_SDIO_INT_CTRL_DATA irqData;
    
    ZERO_OBJECT(irqData);
     
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: DeviceDetach\n"));
        /* tell any function drivers we are gone */
    RemoveHcdFunctions(pHcd);
        /* delete the devices associated with this HCD */        
    DeleteDevices(pHcd);
        /* check and see if there are any IRQs that were left enabled */
    if (pHcd->IrqsEnabled) {
        irqData.SlotIRQEnable = FALSE;
            /* turn off IRQ detection in HCD */
        _IssueConfig(pHcd,SDCONFIG_SDIO_INT_CTRL,(PVOID)&irqData, sizeof(irqData));
    }
    
        /* reset hcd state */
    ResetHcdState(pHcd);
                                    
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: DeviceDetach\n"));
    return SDIO_STATUS_SUCCESS;
}
                 
/* 
 * DeviceAttach - tell core a device was inserted into a slot
*/
SDIO_STATUS DeviceAttach(PSDHCD pHcd) 
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    PSDDEVICE pDevice = NULL;
    UINT      ii;
    
    
    if (IS_CARD_PRESENT(pHcd)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: DeviceAttach called on occupied slot!\n"));
        return SDIO_STATUS_ERROR;   
    }
  
    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: DeviceAttach bdctxt:0x%X \n", (UINT32)pBusContext));
    
    if (IS_HCD_RAW(pHcd)) {
         DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: RAW HCD (%s) device attach \n",pHcd->pName));
            /* this is a raw HCD */
        memset(&pHcd->CardProperties,0,sizeof(pHcd->CardProperties));
        pHcd->CardProperties.Flags = CARD_RAW;
        pHcd->CardProperties.IOFnCount = 0;
          /* for raw HCD, set up minimum parameters
           * since we cannot determine these values using any standard, use values
           * reported by the HCD */            
            /* the operational rate is just the max clock rate reported */
        pHcd->CardProperties.OperBusClock =  pHcd->MaxClockRate;
            /* the max bytes per data transfer is just the max bytes per block */
        pHcd->CardProperties.OperBlockLenLimit = pHcd->MaxBytesPerBlock; 
            /* if the raw HCD uses blocks to transfer, report the operational size
             * from the HCD max value */
        pHcd->CardProperties.OperBlockCountLimit = pHcd->MaxBlocksPerTrans; 
            /* set the slot preferred voltage */
        pHcd->CardProperties.CardVoltage = pHcd->SlotVoltagePreferred;
    } else {
            /* initialize this card and get card properties  */
        if (!SDIO_SUCCESS((status = SDInitializeCard(pHcd)))) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: DeviceAttach, failed to initialize card, %d\n", 
                                   status));
            return status;
        }
    }
    
        /* check for SD or MMC, this must be done first as the query may involve
         * de-selecting the card */
    do {
        if (!(pHcd->CardProperties.Flags & (CARD_MMC | CARD_SD | CARD_RAW))) {
                /* none of these were discovered */
            break;
        }
        pDevice = AllocateDevice(pHcd);        
        if (NULL == pDevice) {
            break;
        }            
        if (pHcd->CardProperties.Flags & CARD_RAW) {
                /* set function number to 1 for IRQ processing */
            SDDEVICE_SET_SDIO_FUNCNO(pDevice,1);  
        } else {
                /* get the ID info for the SD/MMC Card */
            if (!SDIO_SUCCESS((status = SDQuerySDMMCInfo(pDevice)))) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: DeviceAttach, query SDMMC Info failed \n"));
                FreeDevice(pDevice);
                break;
            }
        }     
        AddDeviceToList(pDevice);
            /* look for a function driver to handle this card */
        ProbeForFunction(pDevice, pHcd);         
    } while (FALSE);
    
        /* create a device for each I/O function */
    for(ii= 1; ii <= pHcd->CardProperties.IOFnCount; ii++) {       
        pDevice = AllocateDevice(pHcd);        
        if (NULL == pDevice) {
            break;
        } 
            /* set the function number */
        SDDEVICE_SET_SDIO_FUNCNO(pDevice,ii);  
            /* get the ID info for each I/O function */
        if (!SDIO_SUCCESS((status = SDQuerySDIOInfo(pDevice)))) {
            DBG_PRINT(SDDBG_ERROR, 
                    ("SDIO Bus Driver: DeviceAttach, could not query SDIO Info, funcNo:%d status:%d \n",
                    ii, status));
            FreeDevice(pDevice);
                /* keep loading other functions */
            continue;
        }
        AddDeviceToList(pDevice);
            /* look for a function driver to handle this card */
        ProbeForFunction(pDevice, pHcd);
    }    
    
      
    DBG_PRINT(SDDBG_TRACE, ("-SDIO Bus Driver: DeviceAttach \n"));
    return status;
}   
  
static INLINE void CompleteRequestCheckCancel(PSDHCD pHcd, PSDREQUEST pReqToComplete) 
{
    BOOL cancel = FALSE;
    PSDFUNCTION pFunc = NULL;
    
        /* handle cancel of current request */
    if (pReqToComplete->Flags & SDREQ_FLAGS_CANCELED) {
        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - _SDIO_HandleHcdEvent: cancelling req 0X%X\n", (UINT)pReqToComplete)); 
        cancel = TRUE;
        pReqToComplete->Status = SDIO_STATUS_CANCELED;
        pFunc = pReqToComplete->pFunction;
        DBG_ASSERT(pFunc != NULL);
    } 
    
    DoRequestCompletion(pReqToComplete, pHcd);
    
    if (cancel) {
        SignalSet(&pFunc->CleanupReqSig);
    } 
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Indicate to the SDIO bus driver (core) of an event in the host controller
             driver.
             
  @function name: SDIO_HandleHcdEvent
  @prototype: SDIO_STATUS SDIO_HandleHcdEvent(PSDHCD pHcd, HCD_EVENT Event) 
  @category: HD_Reference
  
  @input:  pHcd - the host controller structure that was registered
           HCD_EVENT - event code
  
  @output: none

  @return: SDIO_STATUS
 
  @notes:  
          The host controller driver can indicate asynchronous events by calling this 
          function with an appropriate event code. Refer to the HDK help manual for
          more information on the event types
  
  @example: Example of indicating a card insertion event:
            SDIO_HandleHcdEvent(&Hcd, EVENT_HCD_ATTACH);
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS _SDIO_HandleHcdEvent(PSDHCD pHcd, HCD_EVENT Event) 
{
    PSDREQUEST       pReq;
    PSDREQUEST       pReqToComplete = NULL;
    PSDREQUEST       pNextReq = NULL;
    SDIO_STATUS      status;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    DBG_PRINT(SDIODBG_HCD_EVENTS, ("SDIO Bus Driver: _SDIO_HandleHcdEvent, event type 0x%X, HCD:0x%X\n", 
                         Event, (UINT)pHcd));
    
    if (Event == EVENT_HCD_TRANSFER_DONE) {
        pReq = GET_CURRENT_REQUEST(pHcd); 
        if (NULL == pReq) {
            DBG_ASSERT(FALSE);
            return SDIO_STATUS_ERROR;   
        }
        
        status = _AcquireHcdLock(pHcd);      
        if (SDIO_SUCCESS(status)) {
                /* null out the current request */
            SET_CURRENT_REQUEST(pHcd, NULL);
            status = _ReleaseHcdLock(pHcd);  
        } else {
            DBG_PRINT(SDDBG_ERROR,
              ("SDIO Bus Driver: SDIO_HandleHcdEvent Failed to acquire HCD lock \n"));
            return SDIO_STATUS_ERROR;
        }    
           
            /* note: the queue is still marked busy to prevent other threads/tasks from starting
             * new requests while we are handling completion , some completed requests are
             * marked as barrier requests which must be handled atomically */
             
        status = pReq->Status;
        DBG_PRINT(SDIODBG_REQUESTS,
            ("+SDIO Bus Driver: Handling Transfer Done (CMD:%d, Status:%d) from HCD:0x%08X \n",
                  pReq->Command, status, (INT)pHcd));    
            /* check SPI mode conversion */
        if (IS_HCD_BUS_MODE_SPI(pHcd) && SDIO_SUCCESS(status)) { 
            if (!(pReq->Flags & SDREQ_FLAGS_RESP_SKIP_SPI_FILT) && !(pReq->Flags & SDREQ_FLAGS_PSEUDO) &&
                (GET_SDREQ_RESP_TYPE(pReq->Flags) != SDREQ_FLAGS_NO_RESP)) {
                ConvertSPI_Response(pReq, NULL);     
            } 
        }        
                                   
        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Completing Request:0x%08X \n",(INT)pReq));

        if (!SDIO_SUCCESS(status) &&
            (status != SDIO_STATUS_CANCELED)  && 
            !(pReq->Flags & SDREQ_FLAGS_CANCELED) &&
            (pReq->RetryCount > 0)) {
                /* retry the request if it failed, was NOT cancelled and the retry count
                 * is greater than zero */
            pReq->RetryCount--;
            pReqToComplete = NULL;
                /* clear SPI converted flag */
            pReq->Flags &= ~SDREQ_FLAGS_RESP_SPI_CONVERTED;
            pNextReq = pReq;     
        } else {
                /* complete the request */
            if (pReq->Flags & SDREQ_FLAGS_BARRIER) {
                    /* a barrier request must be completed before the next bus request is
                     * started */
                CompleteRequestCheckCancel(pHcd, pReq);
                if (!ForceAllRequestsAsync()) {
                    if (CHECK_API_VERSION_COMPAT(pHcd,2,6)) {
                            /* the request was completed, decrement recursion count */
                        status = _AcquireHcdLock(pHcd);
                        if (!SDIO_SUCCESS(status)) {
                            return status;    
                        }
                        pHcd->Recursion--;
                        DBG_ASSERT(pHcd->Recursion >= 0);
                        status = _ReleaseHcdLock(pHcd);
                    } else {
                            /* reset bit */
                        AtomicTest_Clear(&pHcd->HcdFlags, HCD_REQUEST_CALL_BIT);
                    }
                }
                pReqToComplete = NULL;
            } else {
                    /* complete this after the next request has
                     * been started */
                pReqToComplete = pReq; 
            }
        }
        
            /* acquire the hcd lock to look at the queues */
        status = _AcquireHcdLock(pHcd);   
        if (SDIO_SUCCESS(status)) {
            if (pReqToComplete != NULL) {
                    /* queue the request that was completed */
                QueueRequest(&pHcd->CompletedRequestQueue, pReqToComplete);
            }  
            if (NULL == pNextReq) { 
                    /* check the queue for the next request */                    
                DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Checking queue.. \n"));       
                    /* check to see if the HCD was already working on one.  This occurs if
                     * the current request being completed was a barrier request and the
                     * barrier completion routine submitted a new request to the head of the
                     * queue */
                if (GET_CURRENT_REQUEST(pHcd) == NULL) {
                    pNextReq = DequeueRequest(&pHcd->RequestQueue);         
                    if (NULL == pNextReq) {
                            /* nothing in the queue, mark it not busy */
                        MarkQueueNotBusy(&pHcd->RequestQueue); 
                        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Queue idle \n")); 
                    } else {
                        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Next request in queue: 0x%X \n",
                            (INT)pNextReq));
                    }
                } else {
                    DBG_PRINT(SDIODBG_REQUESTS, 
                        ("SDIO Bus Driver: Busy Queue from barrier request \n"));
                }
            }
            
            if (pNextReq != NULL) {
                    /* a new request will be submitted to the HCD below, 
                     * check recursion while we have the lock */
                if (CHECK_API_VERSION_COMPAT(pHcd,2,6)) {
                    CHECK_HCD_RECURSE(pHcd,pNextReq);
                }   
            }                   
            status = _ReleaseHcdLock(pHcd);   
        } else {
            DBG_PRINT(SDDBG_ERROR,
              ("SDIO Bus Driver: SDIO_HandleHcdEvent Failed to acquire HCD lock \n"));
            return SDIO_STATUS_ERROR;
        }        
            /* check for the next request to issue */          
        if (pNextReq != NULL) {     
            DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Starting Next Request: 0x%X \n",
                        (INT)pNextReq));          
            SET_CURRENT_REQUEST(pHcd,pNextReq); 
            status = CallHcdRequest(pHcd);
                /* check and see if the HCD completed the request in the callback */
            if (status != SDIO_STATUS_PENDING) {              
                    /* recurse and process the request */
                _SDIO_HandleHcdEvent(pHcd, EVENT_HCD_TRANSFER_DONE);  
            } 
        }            
                
        /* now empty the completed request queue
         * - this guarantees in-order completion even during recursion */        
        status = _AcquireHcdLock(pHcd);   
        if (SDIO_SUCCESS(status)) {  
            while (1) {
                pReqToComplete = DequeueRequest(&pHcd->CompletedRequestQueue);  
                status = _ReleaseHcdLock(pHcd);  
                if (pReqToComplete != NULL) { 
                    CompleteRequestCheckCancel(pHcd, pReqToComplete);
                    if (!CHECK_API_VERSION_COMPAT(pHcd,2,6)) {
                        if (!ForceAllRequestsAsync()) {
                                /* reset bit */
                            AtomicTest_Clear(&pHcd->HcdFlags, HCD_REQUEST_CALL_BIT);                           
                        }
                    }
                        /* re-acquire lock */ 
                    status = _AcquireHcdLock(pHcd);              
                    if (!SDIO_SUCCESS(status)) {
                        return SDIO_STATUS_ERROR;  
                    }
                    if (CHECK_API_VERSION_COMPAT(pHcd,2,6)) {
                        if (!ForceAllRequestsAsync()) {
                            /* while we have the lock, decrement recursion count each time
                             * we complete a request */
                            pHcd->Recursion--;
                            DBG_ASSERT(pHcd->Recursion >= 0);
                        }
                    }
                }  else {
                        /* we're done */
                    break;  
                }
            }
        } else {
            DBG_PRINT(SDDBG_ERROR,
              ("SDIO Bus Driver: SDIO_HandleHcdEvent Failed to acquire HCD lock \n"));
            return SDIO_STATUS_ERROR;   
        }        
        DBG_PRINT(SDIODBG_REQUESTS, ("-SDIO Bus Driver: Transfer Done Handled \n"));   
        return SDIO_STATUS_SUCCESS;               
    } 
    
    switch(Event) {
        case EVENT_HCD_ATTACH:        
        case EVENT_HCD_DETACH:
                /* card detect helper does the actual attach detach */ 
            return PostCardDetectEvent(pBusContext,Event,pHcd);
        case EVENT_HCD_SDIO_IRQ_PENDING:
            return DeviceInterrupt(pHcd);
        default:
            DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: SDIO_HandleHcdEvent, invalid event type 0x%X, HCD:0x%X\n", 
                                    Event, (UINT)pHcd));
        return SDIO_STATUS_INVALID_PARAMETER;
    }
    
}

/* card detect helper function */
THREAD_RETURN CardDetectHelperFunction(POSKERNEL_HELPER pHelper)
{
    SDIO_STATUS       status;
    HCD_EVENT_MESSAGE message;
    UINT              length;
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - CardDetectHelperFunction starting up: 0x%X \n", (INT)pHelper));
    
    while (1) {
         
            /* wait for wake up event */
        status = SD_WAIT_FOR_WAKEUP(pHelper);    
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver - Card Detect Helper Semaphore Pend Error:%d \n",
                                    status));
            break;
        } 
        
        if (SD_IS_HELPER_SHUTTING_DOWN(pHelper)) {
                /* cleanup message queue on shutdown */
            while (1) {
                length = sizeof(message);
                    /* get a message */
                status = SDLIB_GetMessage(pBusContext->pCardDetectMsgQueue, 
                                          &message, &length);
                if (!SDIO_SUCCESS(status)) {
                    break;
                }
                if (message.pHcd != NULL) {
                        /* decrement HCD reference count */
                    OS_DecHcdReference(message.pHcd);    
                }
            }      
               
            break;   
        }
        
        while (1) {
            length = sizeof(message);
                /* get a message */
            status = SDLIB_GetMessage(pBusContext->pCardDetectMsgQueue, 
                                      &message, &length);
            if (!SDIO_SUCCESS(status)) {
                break;
            }
                          
            switch (message.Event) {
                case EVENT_HCD_ATTACH: 
                    DeviceAttach(message.pHcd);       
                    break;
                case EVENT_HCD_DETACH:
                    DeviceDetach(message.pHcd); 
                    break;
                case EVENT_HCD_CD_POLLING:
                        /* run detector */
                    RunCardDetect();
                    break;
                default:
                    DBG_ASSERT(FALSE);
                    break;
            }
            
            if (message.pHcd != NULL) {
                    /* message was processed, decrement reference count */
                OS_DecHcdReference(message.pHcd);    
            }
        }            
    }
 
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - Card Detect Helper Exiting.. \n"));
    return 0;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  RunCardDetect - run card detect on host controller slots that require polling
  Input:  
  Output: 
  Return:  
  Notes: This function is called from the card detect timer thread 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void RunCardDetect(void)
{
    BOOL       CDPollingRequired = FALSE;
    PSDLIST    pListItem;
    PSDHCD     pHcd;
    BOOL       cardPresent;
    
    DBG_PRINT(SDIODBG_CD_TIMER, ("+SDIO Bus Driver: RunCardDetect\n"));
    
       /* protect the HCD list */
    if (!SDIO_SUCCESS(SemaphorePendInterruptable(&pBusContext->HcdListSem))) {
        DBG_ASSERT(FALSE);
        return;  /* wait interrupted */
    }
        /* while we are running the detector we are blocking HCD removal*/
    SDITERATE_OVER_LIST(&pBusContext->HcdList, pListItem) {
        pHcd = CONTAINING_STRUCT(pListItem, SDHCD, SDList);
            /* does the HCD require polling ? */
        if (pHcd->Attributes & SDHCD_ATTRIB_SLOT_POLLING) {
            DBG_PRINT(SDIODBG_CD_TIMER, ("SDIO Bus Driver: Found HCD requiring polling \n"));
                /* set flag to queue the timer */
            CDPollingRequired = TRUE;              
            if (IS_CARD_PRESENT(pHcd)) {
                    /* there is a device in the slot */
                cardPresent = TRUE;   
                if (SDIO_SUCCESS(ScanSlotForCard(pHcd,&cardPresent))) {
                    if (!cardPresent) {
                        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver CD Polling.. Card Removal Detected\n"));
                        DeviceDetach(pHcd);
                    }    
                }
            } else {
                cardPresent = FALSE;
                if (SDIO_SUCCESS(ScanSlotForCard(pHcd,&cardPresent))) {
                    if (cardPresent) {
                        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver CD Polling.. Card Detected\n"));
                        DeviceAttach(pHcd);
                    }    
                }   
            }     
        } 
        
        DBG_PRINT(SDIODBG_CD_TIMER, ("SDIO Bus Driver: moving to next hcd:0x%X \n",
                                     (INT)pListItem->pNext));
    }
    
        /* check if we need to queue the timer */
    if (CDPollingRequired && !pBusContext->CDTimerQueued) {
        pBusContext->CDTimerQueued = TRUE;
        DBG_PRINT(SDIODBG_CD_TIMER, ("SDIO Bus Driver: Queuing Card detect timer \n"));
        if (!SDIO_SUCCESS(
            QueueTimer(SDIOBUS_CD_TIMER_ID, pBusContext->CDPollingInterval))) {
            DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: failed to queue CD timer \n"));
            pBusContext->CDTimerQueued = FALSE;
        }
    }
        /* release HCD list lock */    
    SemaphorePost(&pBusContext->HcdListSem);
    DBG_PRINT(SDIODBG_CD_TIMER, ("-SDIO Bus Driver: RunCardDetect\n"));
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  ScanSlotForCard - scan slot for a card
  Input:  pHcd - the hcd
  Output: pCardPresent - card present flag (set/cleared on return)
  Return: 
  Notes: 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static SDIO_STATUS ScanSlotForCard(PSDHCD pHcd,PBOOL pCardPresent)
{
    SDIO_STATUS         status = SDIO_STATUS_SUCCESS;
    UINT8               temp;
    
    DBG_PRINT(SDIODBG_CD_TIMER, ("+SDIO Bus Driver: ScanSlotForCard\n"));
    
    do {
        if (!IS_CARD_PRESENT(pHcd)) {
            INT   dbgLvl;
            dbgLvl = DBG_GET_DEBUG_LEVEL();
            DBG_SET_DEBUG_LEVEL(SDDBG_WARN);            
            status = CardInitSetup(pHcd);
            DBG_SET_DEBUG_LEVEL(dbgLvl);
            if (!SDIO_SUCCESS(status)) {
                break;   
            }  
                /* issue go-idle */
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_RESP_R1,NULL);  
            } else {
                _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_NO_RESP,NULL);
            }
                /* try SDIO */
            status = TestPresence(pHcd,CARD_SDIO,NULL);
            if (SDIO_SUCCESS(status)) {
                *pCardPresent = TRUE;
                break;    
            }
                /* issue go-idle */
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_RESP_R1,NULL);  
            } else {
                _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_NO_RESP,NULL);
            }        
                /* try SD */ 
            status = TestPresence(pHcd,CARD_SD,NULL);           
            if (SDIO_SUCCESS(status)) {
                *pCardPresent = TRUE;
                break;    
            }
                /* issue go-idle */
            if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_RESP_R1,NULL);  
            } else {
                _IssueSimpleBusRequest(pHcd,CMD0,0,SDREQ_FLAGS_NO_RESP,NULL);
            }
                /* try MMC */
            status = TestPresence(pHcd,CARD_MMC,NULL); 
            if (SDIO_SUCCESS(status)) {
                *pCardPresent = TRUE;
                break;    
            }
        } else { 
            if (pHcd->CardProperties.Flags & CARD_SDIO) {
#ifdef DUMP_INT_PENDING           
                temp = 0;
                    /* handy debug prints to check interrupt status and print pending register */                
                status = Cmd52ReadByteCommon(pHcd->pPseudoDev, SDIO_INT_ENABLE_REG, &temp); 
                if (SDIO_SUCCESS(status) && (temp != 0)) { 
                    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: INT Enable Reg: 0x%2.2X\n", temp));
                    status = Cmd52ReadByteCommon(pHcd->pPseudoDev, SDIO_INT_PENDING_REG, &temp); 
                    if (SDIO_SUCCESS(status)) { 
                        DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: INT Pend Reg: 0x%2.2X\n", temp));
                    }
                }
#endif
                    /* for SDIO cards, read the revision register */
                status = Cmd52ReadByteCommon(pHcd->pPseudoDev, CCCR_SDIO_REVISION_REG, &temp);      
            } else if (pHcd->CardProperties.Flags & (CARD_SD | CARD_MMC)) {                    
                    /* for SD/MMC cards, issue SEND_STATUS */
                if (IS_HCD_BUS_MODE_SPI(pHcd)) {
                        /* SPI uses the SPI R2 response */
                    status = _IssueSimpleBusRequest(pHcd,
                                                    CMD13,
                                                    0,
                                                    SDREQ_FLAGS_RESP_R2,
                                                    NULL);  
                } else {
                    status = _IssueSimpleBusRequest(pHcd,
                                                    CMD13,
                                                    (pHcd->CardProperties.RCA << 16),
                                                    SDREQ_FLAGS_RESP_R1,NULL);    
                }
            } else {
                DBG_ASSERT(FALSE);   
            }
            if (!SDIO_SUCCESS(status)) {
                    /* card is gone */   
                *pCardPresent = FALSE;  
            }
        }        
    } while (FALSE);
    
    if (status == SDIO_STATUS_BUS_RESP_TIMEOUT) {
        status = SDIO_STATUS_SUCCESS;  
    }
    
    DBG_PRINT(SDIODBG_CD_TIMER, ("-SDIO Bus Driver: ScanSlotForCard status:%d\n",
                                 status));
            
    return status;
}
    
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  DeviceInterrupt - handle device interrupt
  Input:  pHcd -  host controller
  Output: 
  Return: 
  Notes: 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS DeviceInterrupt(PSDHCD pHcd) 
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    SDIO_STATUS status2;
    PSDREQUEST pReq = NULL;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    DBG_PRINT(SDIODBG_FUNC_IRQ, ("+SDIO Bus Driver: DeviceInterrupt\n"));
    
    if (!IS_CARD_PRESENT(pHcd)) {
        DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: Device interrupt asserted on empty slot!\n"));
        return SDIO_STATUS_ERROR;  
    } 
   
    do {
            /* for RAW HCDs or HCDs flagged for single-function IRQ optimization */
        if (IS_HCD_RAW(pHcd) || (pHcd->HcdFlags & (1 << HCD_IRQ_NO_PEND_CHECK))) {
            status = _AcquireHcdLock(pHcd);
            if (!SDIO_SUCCESS(status)) {
                return status; 
            }
            if (pHcd->IrqProcState != SDHCD_IDLE) {
                DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: Already processing interrupts! (state = %d) \n",
                                    pHcd->IrqProcState));                                      
                status = SDIO_STATUS_ERROR; 
                status2 = _ReleaseHcdLock(pHcd); 
            } else {
                DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver :  Device Interrupt \n"));
                    /* mark that we are processing */
                pHcd->IrqProcState = SDHCD_IRQ_PENDING;
                status2 = _ReleaseHcdLock(pHcd); 
                    /* process Irqs for raw hcds or HCDs with the single function optimization */
                    /* force processing of function 1 interrupt */
                ProcessPendingIrqs(pHcd, (1 << 1));
            }
            DBG_PRINT(SDIODBG_FUNC_IRQ, ("-SDIO Bus Driver: DeviceInterrupt: %d\n", status));            
                /* done with RAW irqs */
            return status;  
        }
        
            /* pre-allocate a request to get the pending bits, we have to do this outside the
              * hcd lock acquisition */ 
        pReq = AllocateRequest();
        
        if (NULL == pReq) {
            status = SDIO_STATUS_NO_RESOURCES; 
            break;   
        }
            
        status = _AcquireHcdLock(pHcd);
        
        if (!SDIO_SUCCESS(status)) {
            break;
        }
    
        if (pHcd->IrqProcState != SDHCD_IDLE) {
            DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: Already processing interrupts! (state = %d) \n",
                                    pHcd->IrqProcState));                                      
            status = SDIO_STATUS_ERROR; 
        } else {           
                /* mark that we are processing */
            pHcd->IrqProcState = SDHCD_IRQ_PENDING;
                /* build argument to read IRQ pending register */
            SDIO_SET_CMD52_READ_ARG(pReq->Argument,0,SDIO_INT_PENDING_REG);    
            pReq->Command = CMD52;
            pReq->Flags = SDREQ_FLAGS_TRANS_ASYNC | SDREQ_FLAGS_RESP_SDIO_R5;
            pReq->pCompleteContext = (PVOID)pHcd;
            pReq->pCompletion = GetPendingIrqComplete;
            pReq->RetryCount = SDBUS_MAX_RETRY;
        }
        
        status2 = _ReleaseHcdLock(pHcd); 
        
        if (!SDIO_SUCCESS(status2)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: lock release error: %d\n", status2));
        }   
        
    } while (FALSE);
     
    if (SDIO_SUCCESS(status)) {
        DBG_ASSERT(pReq != NULL);
        IssueRequestToHCD(pHcd,pReq); 
        status = SDIO_STATUS_PENDING; 
    } else {
        if (pReq != NULL) {
            FreeRequest(pReq);       
        }       
    }
    
    DBG_PRINT(SDIODBG_FUNC_IRQ, ("-SDIO Bus Driver: DeviceInterrupt: %d\n", status));
    return status;  
}


/* SDIO IRQ helper */
THREAD_RETURN SDIOIrqHelperFunction(POSKERNEL_HELPER pHelper)
{
    PSDHCD            pHcd;
    SDIO_STATUS       status;
    PSDLIST           pListItem;
    PSDDEVICE         pDevice;
    UINT8             funcMask;
    PSDDEVICE         pDeviceIRQ[7];
    UINT              deviceIrqCount = 0;
    UINT              ii;
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - SDIOIrqHelperFunction starting up \n"));
    
    pHcd = (PSDHCD)pHelper->pContext;
    DBG_ASSERT(pHcd != NULL);
    
    while (1) {
        
            /* wait for wake up event */
        status = SD_WAIT_FOR_WAKEUP(pHelper);    
           
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver - SDIOIrqHelperFunction Pend Error:%d \n",
                                    status));
            break;
        }
                
        if (SD_IS_HELPER_SHUTTING_DOWN(pHelper)) {
            break;   
        }        
        
        DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver - Pending IRQs:0x%X \n", 
                                     pHcd->PendingHelperIrqs));
       
         /* take the device list lock as we iterate through the list, this blocks
             * device removals */
        status = SemaphorePendInterruptable(&pBusContext->DeviceListSem);           
        if (!SDIO_SUCCESS(status)) {
            break;
        }           
            /* walk through the device list matching HCD and interrupting function */
        SDITERATE_OVER_LIST(&pBusContext->DeviceList, pListItem) {
            pDevice = CONTAINING_STRUCT(pListItem, SDDEVICE, SDList);
                /* check if device belongs to the HCD */
            if (pDevice->pHcd != pHcd){
                    /* not on this hcd */
                continue;
            }
            funcMask = 1 << SDDEVICE_GET_SDIO_FUNCNO(pDevice); 
                /* check device function against the pending mask */
            if (!(funcMask & pHcd->PendingHelperIrqs)) {
                    /* this one is not scheduled for the helper */   
                continue;
            }
                /* clear bit */
            pHcd->PendingHelperIrqs &= ~funcMask;
                /* check for sync IRQ and call handler */          
            if (pDevice->pIrqFunction != NULL) {
                DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: Calling IRQ Handler. Fn:%d\n",
                                             SDDEVICE_GET_SDIO_FUNCNO(pDevice)));
                /* save the device so we can process it without holding any locks */
                pDeviceIRQ[deviceIrqCount++] = pDevice; 
            } else {  
                    /* this is actually okay if the device is removing, the callback
                     * is NULLed out */             
                DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: No IRQ handler Fn:%d\n",
                                             SDDEVICE_GET_SDIO_FUNCNO(pDevice)));
            }
        } 
            /* should have handled all these */
        DBG_ASSERT(pHcd->PendingHelperIrqs == 0);
        pHcd->PendingHelperIrqs = 0;
        SemaphorePost(&pBusContext->DeviceListSem);       
        for (ii = 0; ii < deviceIrqCount; ii++) {
            /* now call the function */
            SDDEVICE_CALL_IRQ_HANDLER(pDeviceIRQ[ii]); 
        }
        deviceIrqCount = 0;
    }

    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver - SDIOIrqHelperFunction Exiting.. \n"));
    return 0;  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  GetPendingIrqComplete - completion routine for getting pending IRQs
  Input:  pRequest -  completed request
  Output: 
  Return: 
  Notes: 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static void GetPendingIrqComplete(PSDREQUEST pReq) 
{
    UINT8       intPendingMsk;  
    PSDHCD      pHcd; 
  
    do {
        pHcd = (PSDHCD)pReq->pCompleteContext;
        DBG_ASSERT(pHcd != NULL);
        
        if (!SDIO_SUCCESS(pReq->Status)) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: Failed to get Interrupt pending register Err:%d\n",
                                    pReq->Status)); 
            break; 
        } 
               
        if (SD_R5_GET_RESP_FLAGS(pReq->Response) & SD_R5_ERRORS) {
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: CMD52 resp error: 0x%X \n", 
                                    SD_R5_GET_RESP_FLAGS(pReq->Response)));
            break; 
        }
            /* extract the pending mask */     
        intPendingMsk =  SD_R5_GET_READ_DATA(pReq->Response) & SDIO_INT_PEND_MASK;
            /* process them */
        ProcessPendingIrqs(pHcd, intPendingMsk);
        
    } while (FALSE);
    
    FreeRequest(pReq);
    
    DBG_PRINT(SDIODBG_FUNC_IRQ, ("-SDIO Bus Driver: GetPendingIrqComplete \n"));
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  ProcessPendingIrqs - processing pending Irqs
  Input:  pHcd - host controller
  Input:  IntPendingMsk -  pending irq bit mask
  Output: 
  Return: 
  Notes: 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
static void ProcessPendingIrqs(PSDHCD pHcd, UINT8 IntPendingMsk)
{
    PSDLIST     pListItem;
    PSDDEVICE   pDevice;
    UINT8       funcMask;
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    DBG_PRINT(SDIODBG_FUNC_IRQ, ("+SDIO Bus Driver: ProcessPendingIrqs \n"));
    do {
            /* acquire lock to protect configuration and irq enables */
        status = _AcquireHcdLock(pHcd);
        if (!SDIO_SUCCESS(status)) {
            break;    
        }
        
            /* sanity check */
        if ((IntPendingMsk & pHcd->IrqsEnabled) != IntPendingMsk) {
            DBG_PRINT(SDDBG_ERROR, 
                ("SDIO Bus Driver: IRQs asserting when not enabled : curr:0x%X , card reports: 0x%X\n",
                     pHcd->IrqsEnabled, IntPendingMsk));
                /* remove the pending IRQs that are not enabled */
            IntPendingMsk &= pHcd->IrqsEnabled;  
                /* fall through */
        }           
            
        if (!IntPendingMsk) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: No interrupts on HCD:0x%X \n", (INT)pHcd));
            pHcd->IrqProcState = SDHCD_IDLE;
            if (pHcd->IrqsEnabled) {
                    /* only re-arm if there are IRQs enabled */
                _IssueConfig(pHcd,SDCONFIG_SDIO_REARM_INT,NULL,0);   
            }   
            status = _ReleaseHcdLock(pHcd);
            break; 
        }
            /* reset helper IRQ bits */
        pHcd->PendingHelperIrqs = 0;
            /* save pending IRQ acks */
        pHcd->PendingIrqAcks = IntPendingMsk;
        status = _ReleaseHcdLock(pHcd);
        DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: INTs Pending - 0x%2.2X \n", IntPendingMsk));
            /* take the device list lock as we iterate through the list, this blocks
             * device removals */
        status = SemaphorePendInterruptable(&pBusContext->DeviceListSem);           
        if (!SDIO_SUCCESS(status)) {
            break;
        }  
            /* walk through the device list matching HCD and interrupting function */
        SDITERATE_OVER_LIST(&pBusContext->DeviceList, pListItem) {
            pDevice = CONTAINING_STRUCT(pListItem, SDDEVICE, SDList);
                /* check if device belongs to the HCD */
            if (pDevice->pHcd != pHcd){
                    /* not on this hcd */
                continue;
            }
            funcMask = 1 << SDDEVICE_GET_SDIO_FUNCNO(pDevice); 
                /* check device function against the pending mask */
            if (!(funcMask & IntPendingMsk)) {
                    /* this one is not interrupting */   
                continue;
            }
                /* check for async IRQ and call handler */          
            if (pDevice->pIrqAsyncFunction != NULL) {
                DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: Calling Async IRQ Handler. Fn:%d\n",
                                             SDDEVICE_GET_SDIO_FUNCNO(pDevice)));
                SDDEVICE_CALL_IRQ_ASYNC_HANDLER(pDevice);
            } else {
                    /* this one needs the helper */
                pHcd->PendingHelperIrqs |= funcMask;                
                DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: No Async IRQ, Pending Helper Fn:%d\n",
                                             SDDEVICE_GET_SDIO_FUNCNO(pDevice)));
            }
        }        
            /* release HCD list lock */    
        SemaphorePost(&pBusContext->DeviceListSem);
            /* check for helper IRQs */
        if (pHcd->PendingHelperIrqs) {
            pHcd->IrqProcState = SDHCD_IRQ_HELPER;
            DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: Waking IRQ Helper \n"));
            if (!SDIO_SUCCESS(SD_WAKE_OS_HELPER(&pHcd->SDIOIrqHelper))) {
                DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: failed to wake helper! \n"));
            }
        }
    } while (FALSE);
    
    DBG_PRINT(SDIODBG_FUNC_IRQ, ("-SDIO Bus Driver: ProcessPendingIrqs \n"));
}

SDIO_STATUS TryNoIrqPendingCheck(PSDDEVICE pDevice)
{
    if (pDevice->pHcd->CardProperties.IOFnCount > 1) {
            /* not supported on multi-function cards */
        DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: IRQ Pending Check cannot be bypassed, (Funcs:%d)\n",
            pDevice->pHcd->CardProperties.IOFnCount)); 
        return SDIO_STATUS_UNSUPPORTED;   
    }
    
    DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver: pending IRQ check bypassed \n"));
        /* set flag to optimize this */
    AtomicTest_Set(&pDevice->pHcd->HcdFlags, HCD_IRQ_NO_PEND_CHECK);
    return SDIO_STATUS_SUCCESS;
}


/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDIO_NotifyTimerTriggered - notification handler that a timer expired
  Input:  TimerID - ID of timer that expired
  Output: 
  Return: 
  Notes: 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
void SDIO_NotifyTimerTriggered(INT TimerID)
{
        
    switch (TimerID) {     
        case SDIOBUS_CD_TIMER_ID:
            pBusContext->CDTimerQueued = FALSE;
                /* post an HCD polling event to the helper thread */
            PostCardDetectEvent(pBusContext, EVENT_HCD_CD_POLLING, NULL);
            break;
        default:
            DBG_ASSERT(FALSE); 
    }
  
}
