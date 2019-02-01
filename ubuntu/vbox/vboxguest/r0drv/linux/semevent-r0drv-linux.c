/* $Id: semevent-r0drv-linux.c $ */
/** @file
 * IPRT - Single Release Event Semaphores, Ring-0 Driver, Linux.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define RTSEMEVENT_WITHOUT_REMAPPING
#include "the-linux-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>

#include "waitqueue-r0drv-linux.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Linux event semaphore.
 */
typedef struct RTSEMEVENTINTERNAL
{
    /** Magic value (RTSEMEVENT_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The object status - !0 when signaled and 0 when reset. */
    uint32_t volatile   fState;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** The wait queue. */
    wait_queue_head_t   Head;
} RTSEMEVENTINTERNAL, *PRTSEMEVENTINTERNAL;



RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    PRTSEMEVENTINTERNAL pThis;
    IPRT_LINUX_SAVE_EFL_AC();
    RT_NOREF_PV(hClass); RT_NOREF_PV(pszNameFmt);

    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));

    pThis = (PRTSEMEVENTINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic = RTSEMEVENT_MAGIC;
    pThis->fState   = 0;
    pThis->cRefs    = 1;
    init_waitqueue_head(&pThis->Head);

    *phEventSem = pThis;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemEventCreate);


/**
 * Retains a reference to the event semaphore.
 *
 * @param   pThis       The event semaphore.
 */
DECLINLINE(void) rtR0SemEventLnxRetain(PRTSEMEVENTINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < 100000); NOREF(cRefs);
}


/**
 * Releases a reference to the event semaphore.
 *
 * @param   pThis       The event semaphore.
 */
DECLINLINE(void) rtR0SemEventLnxRelease(PRTSEMEVENTINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
        RTMemFree(pThis);
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs > 0);

    /*
     * Invalidate it and signal the object just in case.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENT_MAGIC);
    ASMAtomicWriteU32(&pThis->fState, 0);
    Assert(!waitqueue_active(&pThis->Head));
    wake_up_all(&pThis->Head);
    rtR0SemEventLnxRelease(pThis);

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemEventDestroy);


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate input.
     */
    PRTSEMEVENTINTERNAL pThis = (PRTSEMEVENTINTERNAL)hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("pThis->u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    rtR0SemEventLnxRetain(pThis);

    /*
     * Signal the event object.
     */
    ASMAtomicWriteU32(&pThis->fState, 1);
    wake_up(&pThis->Head);

    rtR0SemEventLnxRelease(pThis);
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemEventSignal);


/**
 * Worker for RTSemEventWaitEx and RTSemEventWaitExDebug.
 *
 * @returns VBox status code.
 * @param   pThis           The event semaphore.
 * @param   fFlags          See RTSemEventWaitEx.
 * @param   uTimeout        See RTSemEventWaitEx.
 * @param   pSrcPos         The source code position of the wait.
 */
static int rtR0SemEventLnxWait(PRTSEMEVENTINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                               PCRTLOCKVALSRCPOS pSrcPos)
{
    int rc;
    RT_NOREF_PV(pSrcPos);

    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENT_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);
    rtR0SemEventLnxRetain(pThis);

    /*
     * Try grab the event without setting up the wait.
     */
    if (   1 /** @todo check if there are someone waiting already - waitqueue_active, but then what do we do below? */
        && ASMAtomicCmpXchgU32(&pThis->fState, 0, 1))
        rc = VINF_SUCCESS;
    else
    {
        /*
         * We have to wait.
         */
        IPRT_LINUX_SAVE_EFL_AC();
        RTR0SEMLNXWAIT Wait;
        rc = rtR0SemLnxWaitInit(&Wait, fFlags, uTimeout, &pThis->Head);
        if (RT_SUCCESS(rc))
        {
            IPRT_DEBUG_SEMS_STATE(pThis, 'E');
            for (;;)
            {
                /* The destruction test. */
                if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENT_MAGIC))
                    rc = VERR_SEM_DESTROYED;
                else
                {
                    rtR0SemLnxWaitPrepare(&Wait);

                    /* Check the exit conditions. */
                    if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENT_MAGIC))
                        rc = VERR_SEM_DESTROYED;
                    else if (ASMAtomicCmpXchgU32(&pThis->fState, 0, 1))
                        rc = VINF_SUCCESS;
                    else if (rtR0SemLnxWaitHasTimedOut(&Wait))
                        rc = VERR_TIMEOUT;
                    else if (rtR0SemLnxWaitWasInterrupted(&Wait))
                        rc = VERR_INTERRUPTED;
                    else
                    {
                        /* Do the wait and then recheck the conditions. */
                        rtR0SemLnxWaitDoIt(&Wait);
                        continue;
                    }
                }
                break;
            }

            rtR0SemLnxWaitDelete(&Wait);
            IPRT_DEBUG_SEMS_STATE_RC(pThis, 'E', rc);
        }
        IPRT_LINUX_RESTORE_EFL_AC();
    }

    rtR0SemEventLnxRelease(pThis);
    return rc;
}


RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventLnxWait(hEventSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventLnxWait(hEventSem, fFlags, uTimeout, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemEventWaitEx);


RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventLnxWait(hEventSem, fFlags, uTimeout, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemEventWaitExDebug);


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    return rtR0SemLnxWaitGetResolution();
}
RT_EXPORT_SYMBOL(RTSemEventGetResolution);

