//------------------------------------------------------------------------------
// <copyright file="sdio_bus_events.c" company="Atheros">
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

    if (IS_CARD_PRESENT(pHcd)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: DeviceAttach called on occupied slot!\n"));
        return SDIO_STATUS_ERROR;
    }

    DBG_PRINT(SDDBG_TRACE, ("+SDIO Bus Driver: DeviceAttach bdctxt:0x%X \n", (UINT32)pBusContext));

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

    do {

        pDevice = AllocateDevice(pHcd);
        if (NULL == pDevice) {
            break;
        }
            /* set function number to 1 for IRQ processing */
        SDDEVICE_SET_SDIO_FUNCNO(pDevice,1);

        AddDeviceToList(pDevice);

            /* look for a function driver to handle this card */
        ProbeForFunction(pDevice, pHcd);

    } while (FALSE);

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
            ("+SDIO Bus Driver: Handling Transfer Done (P[0]:0x%X, Status:%d) from HCD:0x%08X \n",
                  pReq->Parameters[0].As32bit, status, (INT)pHcd));

        DBG_PRINT(SDIODBG_REQUESTS, ("SDIO Bus Driver: Completing Request:0x%08X \n",(INT)pReq));

        if (!SDIO_SUCCESS(status) &&
            (status != SDIO_STATUS_CANCELED)  &&
            !(pReq->Flags & SDREQ_FLAGS_CANCELED) &&
            (pReq->RetryCount > 0)) {
                /* retry the request if it failed, was NOT cancelled and the retry count
                 * is greater than zero */
            pReq->RetryCount--;
            pReqToComplete = NULL;
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
    INT               length;

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
    CT_DECLARE_IRQ_SYNC_CONTEXT();

    DBG_PRINT(SDIODBG_FUNC_IRQ, ("+SDIO Bus Driver: DeviceInterrupt\n"));

    if (!IS_CARD_PRESENT(pHcd)) {
        DBG_PRINT(SDDBG_ERROR, ("-SDIO Bus Driver: Device interrupt asserted on empty slot!\n"));
        return SDIO_STATUS_ERROR;
    }

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

