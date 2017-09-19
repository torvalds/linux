/* $Id: waitqueue-r0drv-linux.h $ */
/** @file
 * IPRT - Linux Ring-0 Driver Helpers for Abstracting Wait Queues,
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
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


#ifndef ___r0drv_linux_waitqueue_r0drv_linux_h
#define ___r0drv_linux_waitqueue_r0drv_linux_h

#include "the-linux-kernel.h"

#include <iprt/asm-math.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/time.h>

/** The resolution (nanoseconds) specified when using
 *  schedule_hrtimeout_range. */
#define RTR0SEMLNXWAIT_RESOLUTION   50000


/**
 * Kernel mode Linux wait state structure.
 */
typedef struct RTR0SEMLNXWAIT
{
    /** The wait queue entry. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 14)  /* 4.13.0 and openSUSE */
    wait_queue_entry_t WaitQE;
#else
    wait_queue_t    WaitQE;
#endif
    /** The absolute timeout given as nano seconds since the start of the
     *  monotonic clock. */
    uint64_t        uNsAbsTimeout;
    /** The timeout in nano seconds relative to the start of the wait. */
    uint64_t        cNsRelTimeout;
    /** The native timeout value. */
    union
    {
#ifdef IPRT_LINUX_HAS_HRTIMER
        /** The timeout when fHighRes is true. Absolute, so no updating. */
        ktime_t     KtTimeout;
#endif
        /** The timeout when fHighRes is false.  Updated after waiting. */
        long        lTimeout;
    } u;
    /** Set if we use high resolution timeouts. */
    bool            fHighRes;
    /** Set if it's an indefinite wait. */
    bool            fIndefinite;
    /** Set if we've already timed out.
     * Set by rtR0SemLnxWaitDoIt and read by rtR0SemLnxWaitHasTimedOut. */
    bool            fTimedOut;
    /** TASK_INTERRUPTIBLE or TASK_UNINTERRUPTIBLE. */
    int             iWaitState;
    /** The wait queue. */
    wait_queue_head_t *pWaitQueue;
} RTR0SEMLNXWAIT;
/** Pointer to a linux wait state. */
typedef RTR0SEMLNXWAIT *PRTR0SEMLNXWAIT;


/**
 * Initializes a wait.
 *
 * The caller MUST check the wait condition BEFORE calling this function or the
 * timeout logic will be flawed.
 *
 * @returns VINF_SUCCESS or VERR_TIMEOUT.
 * @param   pWait               The wait structure.
 * @param   fFlags              The wait flags.
 * @param   uTimeout            The timeout.
 * @param   pWaitQueue          The wait queue head.
 */
DECLINLINE(int) rtR0SemLnxWaitInit(PRTR0SEMLNXWAIT pWait, uint32_t fFlags, uint64_t uTimeout,
                                   wait_queue_head_t *pWaitQueue)
{
    /*
     * Process the flags and timeout.
     */
    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
/** @todo optimize: millisecs -> nanosecs -> millisec -> jiffies */
        if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
            uTimeout = uTimeout < UINT64_MAX / RT_US_1SEC * RT_US_1SEC
                     ? uTimeout * RT_US_1SEC
                     : UINT64_MAX;
        if (uTimeout == UINT64_MAX)
            fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
        else
        {
            uint64_t u64Now;
            if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
            {
                if (uTimeout == 0)
                    return VERR_TIMEOUT;

                u64Now = RTTimeSystemNanoTS();
                pWait->cNsRelTimeout = uTimeout;
                pWait->uNsAbsTimeout = u64Now + uTimeout;
                if (pWait->uNsAbsTimeout < u64Now) /* overflow */
                    fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            }
            else
            {
                u64Now = RTTimeSystemNanoTS();
                if (u64Now >= uTimeout)
                    return VERR_TIMEOUT;

                pWait->cNsRelTimeout = uTimeout - u64Now;
                pWait->uNsAbsTimeout = uTimeout;
            }
        }
    }

    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
        pWait->fIndefinite      = false;
#ifdef IPRT_LINUX_HAS_HRTIMER
        if (   (fFlags & (RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE))
            || pWait->cNsRelTimeout < RT_NS_1SEC / HZ * 4)
        {
            pWait->fHighRes     = true;
# if BITS_PER_LONG < 64
            if (   KTIME_SEC_MAX <= LONG_MAX
                && pWait->uNsAbsTimeout >= KTIME_SEC_MAX * RT_NS_1SEC_64 + (RT_NS_1SEC - 1))
                fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            else
# endif
                pWait->u.KtTimeout  = ns_to_ktime(pWait->uNsAbsTimeout);
        }
        else
#endif
        {
            uint64_t cJiffies = ASMMultU64ByU32DivByU32(pWait->cNsRelTimeout, HZ, RT_NS_1SEC);
            if (cJiffies >= MAX_JIFFY_OFFSET)
                fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            else
            {
                pWait->u.lTimeout   = (long)cJiffies;
                pWait->fHighRes     = false;
            }
        }
    }

    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
    {
        pWait->fIndefinite      = true;
        pWait->fHighRes         = false;
        pWait->uNsAbsTimeout    = UINT64_MAX;
        pWait->cNsRelTimeout    = UINT64_MAX;
        pWait->u.lTimeout       = LONG_MAX;
    }

    pWait->fTimedOut   = false;

    /*
     * Initialize the wait queue related bits.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 39)
    init_wait((&pWait->WaitQE));
#else
    RT_ZERO(pWait->WaitQE);
    init_waitqueue_entry((&pWait->WaitQE), current);
#endif
    pWait->pWaitQueue = pWaitQueue;
    pWait->iWaitState = fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE
                      ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;

    return VINF_SUCCESS;
}


/**
 * Prepares the next wait.
 *
 * This must be called before rtR0SemLnxWaitDoIt, and the caller should check
 * the exit conditions in-between the two calls.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemLnxWaitPrepare(PRTR0SEMLNXWAIT pWait)
{
    /* Make everything thru schedule*() atomic scheduling wise. (Is this correct?) */
    prepare_to_wait(pWait->pWaitQueue, &pWait->WaitQE, pWait->iWaitState);
}


/**
 * Do the actual wait.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemLnxWaitDoIt(PRTR0SEMLNXWAIT pWait)
{
    if (pWait->fIndefinite)
        schedule();
#ifdef IPRT_LINUX_HAS_HRTIMER
    else if (pWait->fHighRes)
    {
        int rc = schedule_hrtimeout_range(&pWait->u.KtTimeout, HRTIMER_MODE_ABS, RTR0SEMLNXWAIT_RESOLUTION);
        if (!rc)
            pWait->fTimedOut = true;
    }
#endif
    else
    {
        pWait->u.lTimeout = schedule_timeout(pWait->u.lTimeout);
        if (pWait->u.lTimeout <= 0)
            pWait->fTimedOut = true;
    }
    after_wait((&pWait->WaitQE));
}


/**
 * Checks if a linux wait was interrupted.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 * @remarks This shall be called before the first rtR0SemLnxWaitDoIt().
 */
DECLINLINE(bool) rtR0SemLnxWaitWasInterrupted(PRTR0SEMLNXWAIT pWait)
{
    return pWait->iWaitState == TASK_INTERRUPTIBLE
        && signal_pending(current);
}


/**
 * Checks if a linux wait has timed out.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 */
DECLINLINE(bool) rtR0SemLnxWaitHasTimedOut(PRTR0SEMLNXWAIT pWait)
{
    return pWait->fTimedOut;
}


/**
 * Deletes a linux wait.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemLnxWaitDelete(PRTR0SEMLNXWAIT pWait)
{
    finish_wait(pWait->pWaitQueue, &pWait->WaitQE);
}


/**
 * Gets the max resolution of the timeout machinery.
 *
 * @returns Resolution specified in nanoseconds.
 */
DECLINLINE(uint32_t) rtR0SemLnxWaitGetResolution(void)
{
#ifdef IPRT_LINUX_HAS_HRTIMER
    return RTR0SEMLNXWAIT_RESOLUTION;
#else
    return RT_NS_1SEC / HZ; /* ns */
#endif
}

#endif

