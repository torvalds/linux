/* $Id: semspinmutex-r0drv-generic.c $ */
/** @file
 * IPRT - Spinning Mutex Semaphores, Ring-0 Driver, Generic.
 */

/*
 * Copyright (C) 2009-2016 Oracle Corporation
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
#ifdef RT_OS_WINDOWS
# include "../nt/the-nt-kernel.h"
#endif
#include "internal/iprt.h"

#include <iprt/semaphore.h>
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Saved state information.
 */
typedef struct RTSEMSPINMUTEXSTATE
{
    /** Saved flags register. */
    RTCCUINTREG             fSavedFlags;
    /** Preemption state.  */
    RTTHREADPREEMPTSTATE    PreemptState;
    /** Whether to spin or sleep. */
    bool                    fSpin;
    /** Whether the flags have been saved. */
    bool                    fValidFlags;
} RTSEMSPINMUTEXSTATE;

/**
 * Spinning mutex semaphore.
 */
typedef struct RTSEMSPINMUTEXINTERNAL
{
    /** Magic value (RTSEMSPINMUTEX_MAGIC)
     * RTCRITSECT_MAGIC is the value of an initialized & operational section. */
    uint32_t volatile       u32Magic;
    /** Flags. This is a combination of RTSEMSPINMUTEX_FLAGS_XXX and
     *  RTSEMSPINMUTEX_INT_FLAGS_XXX. */
    uint32_t volatile       fFlags;
    /** The owner thread.
     * This is NIL if the semaphore is not owned by anyone. */
    RTNATIVETHREAD volatile hOwner;
    /** Number of threads that are fighting for the lock. */
    int32_t volatile        cLockers;
    /** The semaphore to block on. */
    RTSEMEVENT              hEventSem;
    /** Saved state information of the owner.
     * This will be restored by RTSemSpinRelease. */
    RTSEMSPINMUTEXSTATE     SavedState;
} RTSEMSPINMUTEXINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*#define RTSEMSPINMUTEX_INT_FLAGS_MUST*/

/** Validates the handle, returning if invalid. */
#define RTSEMSPINMUTEX_VALIDATE_RETURN(pThis) \
    do \
    { \
        uint32_t u32Magic; \
        AssertPtr(pThis); \
        u32Magic = (pThis)->u32Magic; \
        if (u32Magic != RTSEMSPINMUTEX_MAGIC) \
        { \
            AssertMsgFailed(("u32Magic=%#x pThis=%p\n", u32Magic, pThis)); \
            return u32Magic == RTSEMSPINMUTEX_MAGIC_DEAD ? VERR_SEM_DESTROYED : VERR_INVALID_HANDLE; \
        } \
    } while (0)


RTDECL(int) RTSemSpinMutexCreate(PRTSEMSPINMUTEX phSpinMtx, uint32_t fFlags)
{
    RTSEMSPINMUTEXINTERNAL *pThis;
    int                     rc;

    AssertReturn(!(fFlags & ~RTSEMSPINMUTEX_FLAGS_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtr(phSpinMtx);

    /*
     * Allocate and initialize the structure.
     */
    pThis = (RTSEMSPINMUTEXINTERNAL *)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    pThis->u32Magic   = RTSEMSPINMUTEX_MAGIC;
    pThis->fFlags     = fFlags;
    pThis->hOwner     = NIL_RTNATIVETHREAD;
    pThis->cLockers   = 0;
    rc = RTSemEventCreateEx(&pThis->hEventSem, RTSEMEVENT_FLAGS_NO_LOCK_VAL, NIL_RTLOCKVALCLASS, NULL);
    if (RT_SUCCESS(rc))
    {
        *phSpinMtx = pThis;
        return VINF_SUCCESS;
    }

    RTMemFree(pThis);
    return rc;
}
RT_EXPORT_SYMBOL(RTSemSpinMutexCreate);


/**
 * Helper for RTSemSpinMutexTryRequest and RTSemSpinMutexRequest.
 *
 * This will check the current context and see if it's usui
 *
 * @returns VINF_SUCCESS or VERR_SEM_BAD_CONTEXT.
 * @param   pState      Output structure.
 */
static int rtSemSpinMutexEnter(RTSEMSPINMUTEXSTATE *pState, RTSEMSPINMUTEXINTERNAL *pThis)
{
#ifndef RT_OS_WINDOWS
    RTTHREADPREEMPTSTATE const StateInit = RTTHREADPREEMPTSTATE_INITIALIZER;
#endif
    int rc  = VINF_SUCCESS;

    /** @todo Later #1: When entering in interrupt context and we're not able to
     *        wake up threads from it, we could try switch the lock into pure
     *        spinlock mode. This would require that there are no other threads
     *        currently waiting on it and that the RTSEMSPINMUTEX_FLAGS_IRQ_SAFE
     *        flag is set.
     *
     *        Later #2: Similarly, it is possible to turn on the
     *        RTSEMSPINMUTEX_FLAGS_IRQ_SAFE at run time if we manage to grab the
     *        semaphore ownership at interrupt time. We might want to try delay the
     *        RTSEMSPINMUTEX_FLAGS_IRQ_SAFE even, since we're fine if we get it...
     */

#ifdef RT_OS_WINDOWS
    /*
     * NT: IRQL <= DISPATCH_LEVEL for waking up threads; IRQL < DISPATCH_LEVEL for sleeping.
     */
    pState->PreemptState.uchOldIrql = KeGetCurrentIrql();
    if (pState->PreemptState.uchOldIrql > DISPATCH_LEVEL)
        return VERR_SEM_BAD_CONTEXT;

    if (pState->PreemptState.uchOldIrql >= DISPATCH_LEVEL)
        pState->fSpin = true;
    else
    {
        pState->fSpin = false;
        KeRaiseIrql(DISPATCH_LEVEL, &pState->PreemptState.uchOldIrql);
        Assert(pState->PreemptState.uchOldIrql < DISPATCH_LEVEL);
    }

#elif defined(RT_OS_SOLARIS)
    /*
     * Solaris:  RTSemEventSignal will do bad stuff on S10 if interrupts are disabled.
     */
    if (!ASMIntAreEnabled())
        return VERR_SEM_BAD_CONTEXT;

    pState->fSpin = !RTThreadPreemptIsEnabled(NIL_RTTHREAD);
    if (RTThreadIsInInterrupt(NIL_RTTHREAD))
    {
        if (!(pThis->fFlags & RTSEMSPINMUTEX_FLAGS_IRQ_SAFE))
            rc = VINF_SEM_BAD_CONTEXT; /* Try, but owner might be interrupted. */
        pState->fSpin = true;
    }
    pState->PreemptState = StateInit;
    RTThreadPreemptDisable(&pState->PreemptState);

#elif defined(RT_OS_LINUX) || defined(RT_OS_OS2)
    /*
     * OSes on which RTSemEventSignal can be called from any context.
     */
    pState->fSpin = !RTThreadPreemptIsEnabled(NIL_RTTHREAD);
    if (RTThreadIsInInterrupt(NIL_RTTHREAD))
    {
        if (!(pThis->fFlags & RTSEMSPINMUTEX_FLAGS_IRQ_SAFE))
            rc = VINF_SEM_BAD_CONTEXT; /* Try, but owner might be interrupted. */
        pState->fSpin = true;
    }
    pState->PreemptState = StateInit;
    RTThreadPreemptDisable(&pState->PreemptState);

#else /* PORTME: Check for context where we cannot wake up threads. */
    /*
     * Default: ASSUME thread can be woken up if interrupts are enabled and
     *          we're not in an interrupt context.
     *          ASSUME that we can go to sleep if preemption is enabled.
     */
    if (    RTThreadIsInInterrupt(NIL_RTTHREAD)
        ||  !ASMIntAreEnabled())
        return VERR_SEM_BAD_CONTEXT;

    pState->fSpin = !RTThreadPreemptIsEnabled(NIL_RTTHREAD);
    pState->PreemptState = StateInit;
    RTThreadPreemptDisable(&pState->PreemptState);
#endif

    /*
     * Disable interrupts if necessary.
     */
    pState->fValidFlags = !!(pThis->fFlags & RTSEMSPINMUTEX_FLAGS_IRQ_SAFE);
    if (pState->fValidFlags)
        pState->fSavedFlags = ASMIntDisableFlags();
    else
        pState->fSavedFlags = 0;

    return rc;
}


/**
 * Helper for RTSemSpinMutexTryRequest, RTSemSpinMutexRequest and
 * RTSemSpinMutexRelease.
 *
 * @param  pState
 */
DECL_FORCE_INLINE(void) rtSemSpinMutexLeave(RTSEMSPINMUTEXSTATE *pState)
{
    /*
     * Restore the interrupt flag.
     */
    if (pState->fValidFlags)
        ASMSetFlags(pState->fSavedFlags);

#ifdef RT_OS_WINDOWS
    /*
     * NT: Lower the IRQL if we raised it.
     */
    if (pState->PreemptState.uchOldIrql < DISPATCH_LEVEL)
        KeLowerIrql(pState->PreemptState.uchOldIrql);
#else
    /*
     * Default: Restore preemption.
     */
    RTThreadPreemptRestore(&pState->PreemptState);
#endif
}


RTDECL(int) RTSemSpinMutexTryRequest(RTSEMSPINMUTEX hSpinMtx)
{
    RTSEMSPINMUTEXINTERNAL *pThis = hSpinMtx;
    RTNATIVETHREAD          hSelf = RTThreadNativeSelf();
    RTSEMSPINMUTEXSTATE     State;
    bool                    fRc;
    int                     rc;

    Assert(hSelf != NIL_RTNATIVETHREAD);
    RTSEMSPINMUTEX_VALIDATE_RETURN(pThis);

    /*
     * Check context, disable preemption and save flags if necessary.
     */
    rc = rtSemSpinMutexEnter(&State, pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Try take the ownership.
     */
    ASMAtomicCmpXchgHandle(&pThis->hOwner, hSelf, NIL_RTNATIVETHREAD, fRc);
    if (!fRc)
    {
        /* Busy, too bad. Check for attempts at nested access. */
        rc = VERR_SEM_BUSY;
        if (RT_UNLIKELY(pThis->hOwner == hSelf))
        {
            AssertMsgFailed(("%p attempt at nested access\n"));
            rc = VERR_SEM_NESTED;
        }

        rtSemSpinMutexLeave(&State);
        return rc;
    }

    /*
     * We're the semaphore owner.
     */
    ASMAtomicIncS32(&pThis->cLockers);
    pThis->SavedState = State;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemSpinMutexTryRequest);


RTDECL(int) RTSemSpinMutexRequest(RTSEMSPINMUTEX hSpinMtx)
{
    RTSEMSPINMUTEXINTERNAL *pThis = hSpinMtx;
    RTNATIVETHREAD          hSelf = RTThreadNativeSelf();
    RTSEMSPINMUTEXSTATE     State;
    bool                    fRc;
    int                     rc;

    Assert(hSelf != NIL_RTNATIVETHREAD);
    RTSEMSPINMUTEX_VALIDATE_RETURN(pThis);

    /*
     * Check context, disable preemption and save flags if necessary.
     */
    rc = rtSemSpinMutexEnter(&State, pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Try take the ownership.
     */
    ASMAtomicIncS32(&pThis->cLockers);
    ASMAtomicCmpXchgHandle(&pThis->hOwner, hSelf, NIL_RTNATIVETHREAD, fRc);
    if (!fRc)
    {
        uint32_t cSpins;

        /*
         * It's busy. Check if it's an attempt at nested access.
         */
        if (RT_UNLIKELY(pThis->hOwner == hSelf))
        {
            AssertMsgFailed(("%p attempt at nested access\n"));
            rtSemSpinMutexLeave(&State);
            return VERR_SEM_NESTED;
        }

        /*
         * Return if we're in interrupt context and the semaphore isn't
         * configure to be interrupt safe.
         */
        if (rc == VINF_SEM_BAD_CONTEXT)
        {
            rtSemSpinMutexLeave(&State);
            return VERR_SEM_BAD_CONTEXT;
        }

        /*
         * Ok, we have to wait.
         */
        if (State.fSpin)
        {
            for (cSpins = 0; ; cSpins++)
            {
                ASMAtomicCmpXchgHandle(&pThis->hOwner, hSelf, NIL_RTNATIVETHREAD, fRc);
                if (fRc)
                    break;
                ASMNopPause();
                if (RT_UNLIKELY(pThis->u32Magic != RTSEMSPINMUTEX_MAGIC))
                {
                    rtSemSpinMutexLeave(&State);
                    return VERR_SEM_DESTROYED;
                }

                /*
                 * "Yield" once in a while. This may lower our IRQL/PIL which
                 * may preempting us, and it will certainly stop the hammering
                 * of hOwner for a little while.
                 */
                if ((cSpins & 0x7f) == 0x1f)
                {
                    rtSemSpinMutexLeave(&State);
                    rtSemSpinMutexEnter(&State, pThis);
                    Assert(State.fSpin);
                }
            }
        }
        else
        {
            for (cSpins = 0;; cSpins++)
            {
                ASMAtomicCmpXchgHandle(&pThis->hOwner, hSelf, NIL_RTNATIVETHREAD, fRc);
                if (fRc)
                    break;
                ASMNopPause();
                if (RT_UNLIKELY(pThis->u32Magic != RTSEMSPINMUTEX_MAGIC))
                {
                    rtSemSpinMutexLeave(&State);
                    return VERR_SEM_DESTROYED;
                }

                if ((cSpins & 15) == 15) /* spin a bit before going sleep (again). */
                {
                    rtSemSpinMutexLeave(&State);

                    rc = RTSemEventWait(pThis->hEventSem, RT_INDEFINITE_WAIT);
                    ASMCompilerBarrier();
                    if (RT_SUCCESS(rc))
                        AssertReturn(pThis->u32Magic == RTSEMSPINMUTEX_MAGIC, VERR_SEM_DESTROYED);
                    else if (rc == VERR_INTERRUPTED)
                        AssertRC(rc);       /* shouldn't happen */
                    else
                    {
                        AssertRC(rc);
                        return rc;
                    }

                    rc = rtSemSpinMutexEnter(&State, pThis);
                    AssertRCReturn(rc, rc);
                    Assert(!State.fSpin);
                }
            }
        }
    }

    /*
     * We're the semaphore owner.
     */
    pThis->SavedState = State;
    Assert(pThis->hOwner == hSelf);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemSpinMutexRequest);


RTDECL(int) RTSemSpinMutexRelease(RTSEMSPINMUTEX hSpinMtx)
{
    RTSEMSPINMUTEXINTERNAL *pThis = hSpinMtx;
    RTNATIVETHREAD          hSelf = RTThreadNativeSelf();
    uint32_t                cLockers;
    RTSEMSPINMUTEXSTATE     State;
    bool                    fRc;

    Assert(hSelf != NIL_RTNATIVETHREAD);
    RTSEMSPINMUTEX_VALIDATE_RETURN(pThis);

    /*
     * Get the saved state and try release the semaphore.
     */
    State = pThis->SavedState;
    ASMCompilerBarrier();
    ASMAtomicCmpXchgHandle(&pThis->hOwner, NIL_RTNATIVETHREAD, hSelf, fRc);
    AssertMsgReturn(fRc,
                    ("hOwner=%p hSelf=%p cLockers=%d\n", pThis->hOwner, hSelf, pThis->cLockers),
                    VERR_NOT_OWNER);

    cLockers = ASMAtomicDecS32(&pThis->cLockers);
    rtSemSpinMutexLeave(&State);
    if (cLockers > 0)
    {
        int rc = RTSemEventSignal(pThis->hEventSem);
        AssertReleaseMsg(RT_SUCCESS(rc), ("RTSemEventSignal -> %Rrc\n", rc));
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemSpinMutexRelease);


RTDECL(int) RTSemSpinMutexDestroy(RTSEMSPINMUTEX hSpinMtx)
{
    RTSEMSPINMUTEXINTERNAL *pThis;
    RTSEMEVENT              hEventSem;
    int                     rc;

    if (hSpinMtx == NIL_RTSEMSPINMUTEX)
        return VINF_SUCCESS;
    pThis = hSpinMtx;
    RTSEMSPINMUTEX_VALIDATE_RETURN(pThis);

    /* No destruction races allowed! */
    AssertMsg(   pThis->cLockers  == 0
              && pThis->hOwner    == NIL_RTNATIVETHREAD,
              ("pThis=%p cLockers=%d hOwner=%p\n", pThis, pThis->cLockers, pThis->hOwner));

    /*
     * Invalidate the structure, free the mutex and free the structure.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, RTSEMSPINMUTEX_MAGIC_DEAD);
    hEventSem        = pThis->hEventSem;
    pThis->hEventSem = NIL_RTSEMEVENT;
    rc = RTSemEventDestroy(hEventSem); AssertRC(rc);

    RTMemFree(pThis);
    return rc;
}
RT_EXPORT_SYMBOL(RTSemSpinMutexDestroy);

