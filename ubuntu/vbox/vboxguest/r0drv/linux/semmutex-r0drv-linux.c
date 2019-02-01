/* $Id: semmutex-r0drv-linux.c $ */
/** @file
 * IPRT - Mutex Semaphores, Ring-0 Driver, Linux.
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
#define RTSEMMUTEX_WITHOUT_REMAPPING
#include "the-linux-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/list.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTSEMMUTEXLNXWAITER
{
    /** The list entry. */
    RTLISTNODE                  ListEntry;
    /** The waiting task. */
    struct task_struct         *pTask;
    /** Why did we wake up? */
    enum
    {
        /** Wakeup to take the semaphore. */
        RTSEMMUTEXLNXWAITER_WAKEUP,
        /** Mutex is being destroyed. */
        RTSEMMUTEXLNXWAITER_DESTROYED,
        /** Some other reason. */
        RTSEMMUTEXLNXWAITER_OTHER
    } volatile                  enmReason;
} RTSEMMUTEXLNXWAITER, *PRTSEMMUTEXLNXWAITER;

/**
 * Wrapper for the linux semaphore structure.
 */
typedef struct RTSEMMUTEXINTERNAL
{
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t                    u32Magic;
    /** The number of recursions. */
    uint32_t                    cRecursions;
    /** The list of waiting threads. */
    RTLISTANCHOR                WaiterList;
    /** The current owner, NULL if none. */
    struct task_struct         *pOwnerTask;
    /** The number of references to this piece of memory.  This is used to
     *  prevent it from being kicked from underneath us while waiting. */
    uint32_t volatile           cRefs;
    /** The spinlock protecting the members and falling asleep. */
    spinlock_t                  Spinlock;
} RTSEMMUTEXINTERNAL, *PRTSEMMUTEXINTERNAL;


RTDECL(int) RTSemMutexCreate(PRTSEMMUTEX phMtx)
{
    int rc = VINF_SUCCESS;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Allocate.
     */
    PRTSEMMUTEXINTERNAL pThis;
    pThis = (PRTSEMMUTEXINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        /*
         * Initialize.
         */
        pThis->u32Magic     = RTSEMMUTEX_MAGIC;
        pThis->cRecursions  = 0;
        pThis->pOwnerTask   = NULL;
        pThis->cRefs        = 1;
        RTListInit(&pThis->WaiterList);
        spin_lock_init(&pThis->Spinlock);

        *phMtx = pThis;
    }
    else
        rc = VERR_NO_MEMORY;

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}
RT_EXPORT_SYMBOL(RTSemMutexCreate);


RTDECL(int) RTSemMutexDestroy(RTSEMMUTEX hMtx)
{
    PRTSEMMUTEXINTERNAL     pThis = hMtx;
    PRTSEMMUTEXLNXWAITER    pCur;
    unsigned long           fSavedIrq;

    /*
     * Validate.
     */
    if (pThis == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);

    /*
     * Kill it, kick waiters and release it.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD, RTSEMMUTEX_MAGIC), VERR_INVALID_HANDLE);

    IPRT_LINUX_SAVE_EFL_AC();

    spin_lock_irqsave(&pThis->Spinlock, fSavedIrq);
    RTListForEach(&pThis->WaiterList, pCur, RTSEMMUTEXLNXWAITER, ListEntry)
    {
        pCur->enmReason = RTSEMMUTEXLNXWAITER_DESTROYED;
        wake_up_process(pCur->pTask);
    }

    if (ASMAtomicDecU32(&pThis->cRefs) != 0)
        spin_unlock_irqrestore(&pThis->Spinlock, fSavedIrq);
    else
    {
        spin_unlock_irqrestore(&pThis->Spinlock, fSavedIrq);
        RTMemFree(pThis);
    }

    IPRT_LINUX_RESTORE_EFL_AC();

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemMutexDestroy);


/**
 * Worker for rtSemMutexLinuxRequest that handles the case where we go to sleep.
 *
 * @returns VINF_SUCCESS, VERR_INTERRUPTED, VERR_TIMEOUT or VERR_SEM_DESTROYED.
 *          Returns without owning the spinlock.
 * @param   pThis           The mutex instance.
 * @param   cMillies        The timeout.
 * @param   fInterruptible  The wait type.
 * @param   fSavedIrq       The saved IRQ flags.
 */
static int rtSemMutexLinuxRequestSleep(PRTSEMMUTEXINTERNAL pThis, RTMSINTERVAL cMillies,
                                       bool fInterruptible, unsigned long fSavedIrq)
{
    struct task_struct *pSelf    = current;
    int                 rc       = VERR_TIMEOUT;
    long                lTimeout = cMillies == RT_INDEFINITE_WAIT ? MAX_SCHEDULE_TIMEOUT : msecs_to_jiffies(cMillies);
    RTSEMMUTEXLNXWAITER Waiter;

    IPRT_DEBUG_SEMS_STATE(pThis, 'm');

    /*
     * Grab a reference to the mutex and add ourselves to the waiter list.
     */
    ASMAtomicIncU32(&pThis->cRefs);

    Waiter.pTask     = pSelf;
    Waiter.enmReason = RTSEMMUTEXLNXWAITER_OTHER;
    RTListAppend(&pThis->WaiterList, &Waiter.ListEntry);

    /*
     * Do the waiting.
     */
    for (;;)
    {
        /* Check signal and timeout conditions. */
        if (    fInterruptible
            &&  signal_pending(pSelf))
        {
            rc = VERR_INTERRUPTED;
            break;
        }

        if (!lTimeout)
            break;

        /* Go to sleep. */
        set_current_state(fInterruptible ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
        spin_unlock_irq(&pThis->Spinlock);

        lTimeout = schedule_timeout(lTimeout);

        spin_lock_irq(&pThis->Spinlock);
        set_current_state(TASK_RUNNING);

        /* Did someone wake us up? */
        if (Waiter.enmReason == RTSEMMUTEXLNXWAITER_WAKEUP)
        {
            Assert(pThis->cRecursions == 0);
            pThis->cRecursions = 1;
            pThis->pOwnerTask  = pSelf;
            rc = VINF_SUCCESS;
            break;
        }

        /* Is the mutex being destroyed? */
        if (RT_UNLIKELY(   Waiter.enmReason == RTSEMMUTEXLNXWAITER_DESTROYED
                        || pThis->u32Magic != RTSEMMUTEX_MAGIC))
        {
            rc = VERR_SEM_DESTROYED;
            break;
        }
    }

    /*
     * Unlink ourself from the waiter list, dereference the mutex and exit the
     * lock.  We might have to free the mutex if it was the destroyed.
     */
    RTListNodeRemove(&Waiter.ListEntry);
    IPRT_DEBUG_SEMS_STATE_RC(pThis, 'M', rc);

    if (RT_LIKELY(ASMAtomicDecU32(&pThis->cRefs) != 0))
        spin_unlock_irqrestore(&pThis->Spinlock, fSavedIrq);
    else
    {
        Assert(RT_FAILURE_NP(rc));
        spin_unlock_irqrestore(&pThis->Spinlock, fSavedIrq);
        RTMemFree(pThis);
    }
    return rc;
}


/**
 * Internal worker.
 */
DECLINLINE(int) rtSemMutexLinuxRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, bool fInterruptible)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    struct task_struct *pSelf = current;
    unsigned long       fSavedIrq;
    int                 rc;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate.
     */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs >= 1);

    /*
     * Lock it and check if it's a recursion.
     */
    spin_lock_irqsave(&pThis->Spinlock, fSavedIrq);
    if (pThis->pOwnerTask == pSelf)
    {
        pThis->cRecursions++;
        Assert(pThis->cRecursions > 1);
        Assert(pThis->cRecursions < 256);
        rc = VINF_SUCCESS;
    }
    /*
     * Not a recursion, maybe it's not owned by anyone then?
     */
    else if (   pThis->pOwnerTask == NULL
             && RTListIsEmpty(&pThis->WaiterList))
    {
        Assert(pThis->cRecursions == 0);
        pThis->cRecursions = 1;
        pThis->pOwnerTask  = pSelf;
        rc = VINF_SUCCESS;
    }
    /*
     * Was it a polling call?
     */
    else if (cMillies == 0)
        rc = VERR_TIMEOUT;
    /*
     * No, so go to sleep.
     */
    else
    {
        rc = rtSemMutexLinuxRequestSleep(pThis, cMillies, fInterruptible, fSavedIrq);
        IPRT_LINUX_RESTORE_EFL_ONLY_AC();
        return rc;
    }

    IPRT_DEBUG_SEMS_STATE_RC(pThis, 'M', rc);
    spin_unlock_irqrestore(&pThis->Spinlock, fSavedIrq);
    IPRT_LINUX_RESTORE_EFL_ONLY_AC();
    return rc;
}


RTDECL(int) RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtSemMutexLinuxRequest(hMutexSem, cMillies, false /*fInterruptible*/);
}
RT_EXPORT_SYMBOL(RTSemMutexRequest);


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RT_NOREF_PV(uId); RT_SRC_POS_NOREF();
    return RTSemMutexRequest(hMutexSem, cMillies);
}
RT_EXPORT_SYMBOL(RTSemMutexRequestDebug);


RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    return rtSemMutexLinuxRequest(hMutexSem, cMillies, true /*fInterruptible*/);
}
RT_EXPORT_SYMBOL(RTSemMutexRequestNoResume);


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RT_NOREF_PV(uId); RT_SRC_POS_NOREF();
    return RTSemMutexRequestNoResume(hMutexSem, cMillies);
}
RT_EXPORT_SYMBOL(RTSemMutexRequestNoResumeDebug);


RTDECL(int) RTSemMutexRelease(RTSEMMUTEX hMtx)
{
    PRTSEMMUTEXINTERNAL pThis = hMtx;
    struct task_struct *pSelf = current;
    unsigned long       fSavedIrq;
    int                 rc;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate.
     */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), VERR_INVALID_HANDLE);
    Assert(pThis->cRefs >= 1);

    /*
     * Take the lock and release one recursion.
     */
    spin_lock_irqsave(&pThis->Spinlock, fSavedIrq);
    if (pThis->pOwnerTask == pSelf)
    {
        Assert(pThis->cRecursions > 0);
        if (--pThis->cRecursions == 0)
        {
            pThis->pOwnerTask = NULL;

            /* anyone to wake up? */
            if (!RTListIsEmpty(&pThis->WaiterList))
            {
                PRTSEMMUTEXLNXWAITER pWaiter = RTListGetFirst(&pThis->WaiterList, RTSEMMUTEXLNXWAITER, ListEntry);
                pWaiter->enmReason = RTSEMMUTEXLNXWAITER_WAKEUP;
                wake_up_process(pWaiter->pTask);
            }
            IPRT_DEBUG_SEMS_STATE(pThis, 'u');
        }
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NOT_OWNER;
    spin_unlock_irqrestore(&pThis->Spinlock, fSavedIrq);

    AssertRC(rc);
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}
RT_EXPORT_SYMBOL(RTSemMutexRelease);


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    PRTSEMMUTEXINTERNAL pThis = hMutexSem;
    unsigned long       fSavedIrq;
    bool                fOwned;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate.
     */
    AssertPtrReturn(pThis, false);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, ("u32Magic=%RX32 pThis=%p\n", pThis->u32Magic, pThis), false);
    Assert(pThis->cRefs >= 1);

    /*
     * Take the lock and release one recursion.
     */
    spin_lock_irqsave(&pThis->Spinlock, fSavedIrq);
    fOwned = pThis->pOwnerTask != NULL;
    spin_unlock_irqrestore(&pThis->Spinlock, fSavedIrq);

    IPRT_LINUX_RESTORE_EFL_AC();
    return fOwned;

}
RT_EXPORT_SYMBOL(RTSemMutexIsOwned);

