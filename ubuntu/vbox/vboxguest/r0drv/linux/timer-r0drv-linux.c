/* $Id: timer-r0drv-linux.c $ */
/** @file
 * IPRT - Timers, Ring-0 Driver, Linux.
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
#include "the-linux-kernel.h"
#include "internal/iprt.h"

#include <iprt/timer.h>
#include <iprt/time.h>
#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/spinlock.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>

#include "internal/magics.h"

/** @def RTTIMER_LINUX_WITH_HRTIMER
 * Whether to use high resolution timers.  */
#if !defined(RTTIMER_LINUX_WITH_HRTIMER) \
    && defined(IPRT_LINUX_HAS_HRTIMER)
# define RTTIMER_LINUX_WITH_HRTIMER
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
# define mod_timer_pinned               mod_timer
# define HRTIMER_MODE_ABS_PINNED        HRTIMER_MODE_ABS
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Timer state machine.
 *
 * This is used to try handle the issues with MP events and
 * timers that runs on all CPUs. It's relatively nasty :-/
 */
typedef enum RTTIMERLNXSTATE
{
    /** Stopped. */
    RTTIMERLNXSTATE_STOPPED = 0,
    /** Transient state; next ACTIVE. */
    RTTIMERLNXSTATE_STARTING,
    /** Transient state; next ACTIVE. (not really necessary) */
    RTTIMERLNXSTATE_MP_STARTING,
    /** Active. */
    RTTIMERLNXSTATE_ACTIVE,
    /** Active and in callback; next ACTIVE, STOPPED or CALLBACK_DESTROYING. */
    RTTIMERLNXSTATE_CALLBACK,
    /** Stopped while in the callback; next STOPPED. */
    RTTIMERLNXSTATE_CB_STOPPING,
    /** Restarted while in the callback; next ACTIVE, STOPPED, DESTROYING. */
    RTTIMERLNXSTATE_CB_RESTARTING,
    /** The callback shall destroy the timer; next STOPPED. */
    RTTIMERLNXSTATE_CB_DESTROYING,
    /** Transient state; next STOPPED. */
    RTTIMERLNXSTATE_STOPPING,
    /** Transient state; next STOPPED. */
    RTTIMERLNXSTATE_MP_STOPPING,
    /** The usual 32-bit hack. */
    RTTIMERLNXSTATE_32BIT_HACK = 0x7fffffff
} RTTIMERLNXSTATE;


/**
 * A Linux sub-timer.
 */
typedef struct RTTIMERLNXSUBTIMER
{
    /** Timer specific data.  */
    union
    {
#if defined(RTTIMER_LINUX_WITH_HRTIMER)
        /** High resolution timer. */
        struct
        {
            /** The linux timer structure. */
            struct hrtimer          LnxTimer;
        } Hr;
#endif
        /** Standard timer. */
        struct
        {
            /** The linux timer structure. */
            struct timer_list       LnxTimer;
            /** The start of the current run (ns).
             * This is used to calculate when the timer ought to fire the next time. */
            uint64_t                u64NextTS;
            /** The u64NextTS in jiffies. */
            unsigned long           ulNextJiffies;
            /** Set when starting or changing the timer so that u64StartTs
             *  and u64NextTS gets reinitialized (eliminating some jitter). */
            bool volatile           fFirstAfterChg;
        } Std;
    } u;
    /** The current tick number. */
    uint64_t                iTick;
    /** Restart the single shot timer at this specific time.
     * Used when a single shot timer is restarted from the callback. */
    uint64_t volatile       uNsRestartAt;
    /** Pointer to the parent timer. */
    PRTTIMER                pParent;
    /** The current sub-timer state. */
    RTTIMERLNXSTATE volatile enmState;
} RTTIMERLNXSUBTIMER;
/** Pointer to a linux sub-timer. */
typedef RTTIMERLNXSUBTIMER *PRTTIMERLNXSUBTIMER;


/**
 * The internal representation of an Linux timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Spinlock synchronizing the fSuspended and MP event handling.
     * This is NIL_RTSPINLOCK if cCpus == 1. */
    RTSPINLOCK              hSpinlock;
    /** Flag indicating that the timer is suspended. */
    bool volatile           fSuspended;
    /** Whether the timer must run on one specific CPU or not. */
    bool                    fSpecificCpu;
#ifdef CONFIG_SMP
    /** Whether the timer must run on all CPUs or not. */
    bool                    fAllCpus;
#endif /* else: All -> specific on non-SMP kernels */
    /** Whether it is a high resolution timer or a standard one. */
    bool                    fHighRes;
    /** The id of the CPU it must run on if fSpecificCpu is set. */
    RTCPUID                 idCpu;
    /** The number of CPUs this timer should run on. */
    RTCPUID                 cCpus;
    /** Callback. */
    PFNRTTIMER              pfnTimer;
    /** User argument. */
    void                   *pvUser;
    /** The timer interval. 0 if one-shot. */
    uint64_t volatile       u64NanoInterval;
    /** This is set to the number of jiffies between ticks if the interval is
     * an exact number of jiffies. (Standard timers only.) */
    unsigned long volatile  cJiffies;
    /** The change interval spinlock for standard timers only. */
    spinlock_t              ChgIntLock;
    /** Workqueue item for delayed destruction. */
    RTR0LNXWORKQUEUEITEM    DtorWorkqueueItem;
    /** Sub-timers.
     * Normally there is just one, but for RTTIMER_FLAGS_CPU_ALL this will contain
     * an entry for all possible cpus. In that case the index will be the same as
     * for the RTCpuSet. */
    RTTIMERLNXSUBTIMER      aSubTimers[1];
} RTTIMER;


/**
 * A rtTimerLinuxStartOnCpu and rtTimerLinuxStartOnCpu argument package.
 */
typedef struct RTTIMERLINUXSTARTONCPUARGS
{
    /** The current time (RTTimeSystemNanoTS). */
    uint64_t                u64Now;
    /** When to start firing (delta). */
    uint64_t                u64First;
} RTTIMERLINUXSTARTONCPUARGS;
/** Pointer to a rtTimerLinuxStartOnCpu argument package. */
typedef RTTIMERLINUXSTARTONCPUARGS *PRTTIMERLINUXSTARTONCPUARGS;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef CONFIG_SMP
static DECLCALLBACK(void) rtTimerLinuxMpEvent(RTMPEVENT enmEvent, RTCPUID idCpu, void *pvUser);
#endif

#if 0
#define DEBUG_HACKING
#include <iprt/string.h>
#include <iprt/asm-amd64-x86.h>
static void myLogBackdoorPrintf(const char *pszFormat, ...)
{
    char        szTmp[256];
    va_list     args;
    size_t      cb;

    cb = RTStrPrintf(szTmp, sizeof(szTmp) - 10, "%d: ", RTMpCpuId());
    va_start(args, pszFormat);
    cb += RTStrPrintfV(&szTmp[cb], sizeof(szTmp) - cb, pszFormat, args);
    va_end(args);

    ASMOutStrU8(0x504, (uint8_t *)&szTmp[0], cb);
}
# define RTAssertMsg1Weak(pszExpr, uLine, pszFile, pszFunction) \
    myLogBackdoorPrintf("\n!!Guest Assertion failed!!\n%s(%d) %s\n%s\n", uLine, pszFile, pszFunction, (pszExpr))
# define RTAssertMsg2Weak myLogBackdoorPrintf
# define RTTIMERLNX_LOG(a)          myLogBackdoorPrintf a
#else
# define RTTIMERLNX_LOG(a)          do { } while (0)
#endif

/**
 * Sets the state.
 */
DECLINLINE(void) rtTimerLnxSetState(RTTIMERLNXSTATE volatile *penmState, RTTIMERLNXSTATE enmNewState)
{
#ifdef DEBUG_HACKING
    RTTIMERLNX_LOG(("set %d -> %d\n", *penmState, enmNewState));
#endif
    ASMAtomicWriteU32((uint32_t volatile *)penmState, enmNewState);
}


/**
 * Sets the state if it has a certain value.
 *
 * @return true if xchg was done.
 * @return false if xchg wasn't done.
 */
#ifdef DEBUG_HACKING
#define rtTimerLnxCmpXchgState(penmState, enmNewState, enmCurState) rtTimerLnxCmpXchgStateDebug(penmState, enmNewState, enmCurState, __LINE__)
static bool rtTimerLnxCmpXchgStateDebug(RTTIMERLNXSTATE volatile *penmState, RTTIMERLNXSTATE enmNewState,
                                        RTTIMERLNXSTATE enmCurState, uint32_t uLine)
{
    RTTIMERLNXSTATE enmOldState = enmCurState;
    bool fRc = ASMAtomicCmpXchgExU32((uint32_t volatile *)penmState, enmNewState, enmCurState, (uint32_t *)&enmOldState);
    RTTIMERLNX_LOG(("cxg %d -> %d - %d at %u\n", enmOldState, enmNewState, fRc, uLine));
    return fRc;
}
#else
DECLINLINE(bool) rtTimerLnxCmpXchgState(RTTIMERLNXSTATE volatile *penmState, RTTIMERLNXSTATE enmNewState,
                                        RTTIMERLNXSTATE enmCurState)
{
    return ASMAtomicCmpXchgU32((uint32_t volatile *)penmState, enmNewState, enmCurState);
}
#endif


/**
 * Gets the state.
 */
DECLINLINE(RTTIMERLNXSTATE) rtTimerLnxGetState(RTTIMERLNXSTATE volatile *penmState)
{
    return (RTTIMERLNXSTATE)ASMAtomicUoReadU32((uint32_t volatile *)penmState);
}

#ifdef RTTIMER_LINUX_WITH_HRTIMER

/**
 * Converts a nano second time stamp to ktime_t.
 *
 * ASSUMES RTTimeSystemNanoTS() is implemented using ktime_get_ts().
 *
 * @returns ktime_t.
 * @param   cNanoSecs   Nanoseconds.
 */
DECLINLINE(ktime_t) rtTimerLnxNanoToKt(uint64_t cNanoSecs)
{
    /* With some luck the compiler optimizes the division out of this... (Bet it doesn't.) */
    return ktime_set(cNanoSecs / 1000000000, cNanoSecs % 1000000000);
}

/**
 * Converts ktime_t to a nano second time stamp.
 *
 * ASSUMES RTTimeSystemNanoTS() is implemented using ktime_get_ts().
 *
 * @returns nano second time stamp.
 * @param   Kt          ktime_t.
 */
DECLINLINE(uint64_t) rtTimerLnxKtToNano(ktime_t Kt)
{
    return ktime_to_ns(Kt);
}

#endif /* RTTIMER_LINUX_WITH_HRTIMER */

/**
 * Converts a nano second interval to jiffies.
 *
 * @returns Jiffies.
 * @param   cNanoSecs   Nanoseconds.
 */
DECLINLINE(unsigned long) rtTimerLnxNanoToJiffies(uint64_t cNanoSecs)
{
    /* this can be made even better... */
    if (cNanoSecs > (uint64_t)TICK_NSEC * MAX_JIFFY_OFFSET)
        return MAX_JIFFY_OFFSET;
# if ARCH_BITS == 32
    if (RT_LIKELY(cNanoSecs <= UINT32_MAX))
        return ((uint32_t)cNanoSecs + (TICK_NSEC-1)) / TICK_NSEC;
# endif
    return (cNanoSecs + (TICK_NSEC-1)) / TICK_NSEC;
}


/**
 * Starts a sub-timer (RTTimerStart).
 *
 * @param   pSubTimer   The sub-timer to start.
 * @param   u64Now      The current timestamp (RTTimeSystemNanoTS()).
 * @param   u64First    The interval from u64Now to the first time the timer should fire.
 * @param   fPinned     true = timer pinned to a specific CPU,
 *                      false = timer can migrate between CPUs
 * @param   fHighRes    Whether the user requested a high resolution timer or not.
 * @param   enmOldState The old timer state.
 */
static void rtTimerLnxStartSubTimer(PRTTIMERLNXSUBTIMER pSubTimer, uint64_t u64Now, uint64_t u64First,
                                    bool fPinned, bool fHighRes)
{
    /*
     * Calc when it should start firing.
     */
    uint64_t u64NextTS = u64Now + u64First;
    if (!fHighRes)
        pSubTimer->u.Std.u64NextTS = u64NextTS;
    RTTIMERLNX_LOG(("startsubtimer %p\n", pSubTimer->pParent));

    pSubTimer->iTick = 0;

#ifdef RTTIMER_LINUX_WITH_HRTIMER
    if (fHighRes)
        hrtimer_start(&pSubTimer->u.Hr.LnxTimer, rtTimerLnxNanoToKt(u64NextTS),
                      fPinned ? HRTIMER_MODE_ABS_PINNED : HRTIMER_MODE_ABS);
    else
#endif
    {
        unsigned long cJiffies = !u64First ? 0 : rtTimerLnxNanoToJiffies(u64First);
        pSubTimer->u.Std.ulNextJiffies  = jiffies + cJiffies;
        pSubTimer->u.Std.fFirstAfterChg = true;
#ifdef CONFIG_SMP
        if (fPinned)
        {
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
            mod_timer(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
# else
            mod_timer_pinned(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
# endif
        }
        else
#endif
            mod_timer(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
    }

    /* Be a bit careful here since we could be racing the callback. */
    if (!rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_ACTIVE, RTTIMERLNXSTATE_STARTING))
        rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_ACTIVE, RTTIMERLNXSTATE_MP_STARTING);
}


/**
 * Stops a sub-timer (RTTimerStart and rtTimerLinuxMpEvent()).
 *
 * The caller has already changed the state, so we will not be in a callback
 * situation wrt to the calling thread.
 *
 * @param   pSubTimer   The sub-timer.
 * @param   fHighRes    Whether the user requested a high resolution timer or not.
 */
static void rtTimerLnxStopSubTimer(PRTTIMERLNXSUBTIMER pSubTimer, bool fHighRes)
{
    RTTIMERLNX_LOG(("stopsubtimer %p %d\n", pSubTimer->pParent, fHighRes));
#ifdef RTTIMER_LINUX_WITH_HRTIMER
    if (fHighRes)
    {
        /* There is no equivalent to del_timer in the hrtimer API,
           hrtimer_cancel() == del_timer_sync().  Just like the WARN_ON in
           del_timer_sync() asserts, waiting for a timer callback to complete
           is deadlock prone, so don't do it.  */
        int rc = hrtimer_try_to_cancel(&pSubTimer->u.Hr.LnxTimer);
        if (rc < 0)
        {
            hrtimer_start(&pSubTimer->u.Hr.LnxTimer, ktime_set(KTIME_SEC_MAX, 0), HRTIMER_MODE_ABS);
            hrtimer_try_to_cancel(&pSubTimer->u.Hr.LnxTimer);
        }
    }
    else
#endif
        del_timer(&pSubTimer->u.Std.LnxTimer);

    rtTimerLnxSetState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED);
}


/**
 * Used by RTTimerDestroy and rtTimerLnxCallbackDestroy to do the actual work.
 *
 * @param   pTimer  The timer in question.
 */
static void rtTimerLnxDestroyIt(PRTTIMER pTimer)
{
    RTSPINLOCK  hSpinlock = pTimer->hSpinlock;
    RTCPUID     iCpu;
    Assert(pTimer->fSuspended);
    RTTIMERLNX_LOG(("destroyit %p\n", pTimer));

    /*
     * Remove the MP notifications first because it'll reduce the risk of
     * us overtaking any MP event that might theoretically be racing us here.
     */
#ifdef CONFIG_SMP
    if (    pTimer->cCpus > 1
        &&  hSpinlock != NIL_RTSPINLOCK)
    {
        int rc = RTMpNotificationDeregister(rtTimerLinuxMpEvent, pTimer);
        AssertRC(rc);
    }
#endif /* CONFIG_SMP */

    /*
     * Invalidate the handle.
     */
    ASMAtomicWriteU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);

    /*
     * Make sure all timers have stopped executing since we're stopping them in
     * an asynchronous manner up in rtTimerLnxStopSubTimer.
     */
    iCpu = pTimer->cCpus;
    while (iCpu-- > 0)
    {
#ifdef RTTIMER_LINUX_WITH_HRTIMER
        if (pTimer->fHighRes)
            hrtimer_cancel(&pTimer->aSubTimers[iCpu].u.Hr.LnxTimer);
        else
#endif
            del_timer_sync(&pTimer->aSubTimers[iCpu].u.Std.LnxTimer);
    }

    /*
     * Finally, free the resources.
     */
    RTMemFreeEx(pTimer, RT_UOFFSETOF_DYN(RTTIMER, aSubTimers[pTimer->cCpus]));
    if (hSpinlock != NIL_RTSPINLOCK)
        RTSpinlockDestroy(hSpinlock);
}


/**
 * Workqueue callback (no DECLCALLBACK!) for deferred destruction.
 *
 * @param   pWork        Pointer to the DtorWorkqueueItem member of our timer
 *                       structure.
 */
static void rtTimerLnxDestroyDeferred(RTR0LNXWORKQUEUEITEM *pWork)
{
    PRTTIMER pTimer = RT_FROM_MEMBER(pWork, RTTIMER, DtorWorkqueueItem);
    rtTimerLnxDestroyIt(pTimer);
}


/**
 * Called when the timer was destroyed by the callback function.
 *
 * @param   pTimer      The timer.
 * @param   pSubTimer   The sub-timer which we're handling, the state of this
 *                      will be RTTIMERLNXSTATE_CALLBACK_DESTROYING.
 */
static void rtTimerLnxCallbackDestroy(PRTTIMER pTimer, PRTTIMERLNXSUBTIMER pSubTimer)
{
    /*
     * If it's an omni timer, the last dude does the destroying.
     */
    if (pTimer->cCpus > 1)
    {
        uint32_t        iCpu        = pTimer->cCpus;
        RTSpinlockAcquire(pTimer->hSpinlock);

        Assert(pSubTimer->enmState == RTTIMERLNXSTATE_CB_DESTROYING);
        rtTimerLnxSetState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED);

        while (iCpu-- > 0)
            if (rtTimerLnxGetState(&pTimer->aSubTimers[iCpu].enmState) != RTTIMERLNXSTATE_STOPPED)
            {
                RTSpinlockRelease(pTimer->hSpinlock);
                return;
            }

        RTSpinlockRelease(pTimer->hSpinlock);
    }

    /*
     * Destroying a timer from the callback is unsafe since the callout code
     * might be touching the timer structure upon return (hrtimer does!).  So,
     * we have to defer the actual destruction to the IRPT workqueue.
     */
    rtR0LnxWorkqueuePush(&pTimer->DtorWorkqueueItem, rtTimerLnxDestroyDeferred);
}


#ifdef CONFIG_SMP
/**
 * Deal with a sub-timer that has migrated.
 *
 * @param   pTimer          The timer.
 * @param   pSubTimer       The sub-timer.
 */
static void rtTimerLnxCallbackHandleMigration(PRTTIMER pTimer, PRTTIMERLNXSUBTIMER pSubTimer)
{
    RTTIMERLNXSTATE enmState;
    if (pTimer->cCpus > 1)
        RTSpinlockAcquire(pTimer->hSpinlock);

    do
    {
        enmState = rtTimerLnxGetState(&pSubTimer->enmState);
        switch (enmState)
        {
            case RTTIMERLNXSTATE_STOPPING:
            case RTTIMERLNXSTATE_MP_STOPPING:
                enmState = RTTIMERLNXSTATE_STOPPED;
            case RTTIMERLNXSTATE_STOPPED:
                break;

            default:
                AssertMsgFailed(("%d\n", enmState)); RT_FALL_THRU();
            case RTTIMERLNXSTATE_STARTING:
            case RTTIMERLNXSTATE_MP_STARTING:
            case RTTIMERLNXSTATE_ACTIVE:
            case RTTIMERLNXSTATE_CALLBACK:
            case RTTIMERLNXSTATE_CB_STOPPING:
            case RTTIMERLNXSTATE_CB_RESTARTING:
                if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED, enmState))
                    enmState = RTTIMERLNXSTATE_STOPPED;
                break;

            case RTTIMERLNXSTATE_CB_DESTROYING:
            {
                if (pTimer->cCpus > 1)
                    RTSpinlockRelease(pTimer->hSpinlock);

                rtTimerLnxCallbackDestroy(pTimer, pSubTimer);
                return;
            }
        }
    } while (enmState != RTTIMERLNXSTATE_STOPPED);

    if (pTimer->cCpus > 1)
        RTSpinlockRelease(pTimer->hSpinlock);
}
#endif /* CONFIG_SMP */


/**
 * The slow path of rtTimerLnxChangeToCallbackState.
 *
 * @returns true if changed successfully, false if not.
 * @param   pSubTimer       The sub-timer.
 */
static bool rtTimerLnxChangeToCallbackStateSlow(PRTTIMERLNXSUBTIMER pSubTimer)
{
    for (;;)
    {
        RTTIMERLNXSTATE enmState = rtTimerLnxGetState(&pSubTimer->enmState);
        switch (enmState)
        {
            case RTTIMERLNXSTATE_ACTIVE:
            case RTTIMERLNXSTATE_STARTING:
            case RTTIMERLNXSTATE_MP_STARTING:
                if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_CALLBACK, enmState))
                    return true;
                break;

            case RTTIMERLNXSTATE_CALLBACK:
            case RTTIMERLNXSTATE_CB_STOPPING:
            case RTTIMERLNXSTATE_CB_RESTARTING:
            case RTTIMERLNXSTATE_CB_DESTROYING:
                AssertMsgFailed(("%d\n", enmState)); RT_FALL_THRU();
            default:
                return false;
        }
        ASMNopPause();
    }
}


/**
 * Tries to change the sub-timer state to 'callback'.
 *
 * @returns true if changed successfully, false if not.
 * @param   pSubTimer       The sub-timer.
 */
DECLINLINE(bool) rtTimerLnxChangeToCallbackState(PRTTIMERLNXSUBTIMER pSubTimer)
{
    if (RT_LIKELY(rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_CALLBACK, RTTIMERLNXSTATE_ACTIVE)))
        return true;
    return rtTimerLnxChangeToCallbackStateSlow(pSubTimer);
}


#ifdef RTTIMER_LINUX_WITH_HRTIMER
/**
 * Timer callback function for high resolution timers.
 *
 * @returns HRTIMER_NORESTART or HRTIMER_RESTART depending on whether it's a
 *          one-shot or interval timer.
 * @param   pHrTimer    Pointer to the sub-timer structure.
 */
static enum hrtimer_restart rtTimerLinuxHrCallback(struct hrtimer *pHrTimer)
{
    PRTTIMERLNXSUBTIMER     pSubTimer = RT_FROM_MEMBER(pHrTimer, RTTIMERLNXSUBTIMER, u.Hr.LnxTimer);
    PRTTIMER                pTimer    = pSubTimer->pParent;


    RTTIMERLNX_LOG(("hrcallback %p\n", pTimer));
    if (RT_UNLIKELY(!rtTimerLnxChangeToCallbackState(pSubTimer)))
        return HRTIMER_NORESTART;

#ifdef CONFIG_SMP
    /*
     * Check for unwanted migration.
     */
    if (pTimer->fAllCpus || pTimer->fSpecificCpu)
    {
        RTCPUID idCpu = RTMpCpuId();
        if (RT_UNLIKELY(  pTimer->fAllCpus
                        ? (RTCPUID)(pSubTimer - &pTimer->aSubTimers[0]) != idCpu
                        : pTimer->idCpu != idCpu))
        {
            rtTimerLnxCallbackHandleMigration(pTimer, pSubTimer);
            return HRTIMER_NORESTART;
        }
    }
#endif

    if (pTimer->u64NanoInterval)
    {
        /*
         * Periodic timer, run it and update the native timer afterwards so
         * we can handle RTTimerStop and RTTimerChangeInterval from the
         * callback as well as a racing control thread.
         */
        pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pSubTimer->iTick);
        hrtimer_add_expires_ns(&pSubTimer->u.Hr.LnxTimer, ASMAtomicReadU64(&pTimer->u64NanoInterval));
        if (RT_LIKELY(rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_ACTIVE, RTTIMERLNXSTATE_CALLBACK)))
            return HRTIMER_RESTART;
    }
    else
    {
        /*
         * One shot timer (no omni), stop it before dispatching it.
         * Allow RTTimerStart as well as RTTimerDestroy to be called from
         * the callback.
         */
        ASMAtomicWriteBool(&pTimer->fSuspended, true);
        pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pSubTimer->iTick);
        if (RT_LIKELY(rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED, RTTIMERLNXSTATE_CALLBACK)))
            return HRTIMER_NORESTART;
    }

    /*
     * Some state change occurred while we were in the callback routine.
     */
    for (;;)
    {
        RTTIMERLNXSTATE enmState = rtTimerLnxGetState(&pSubTimer->enmState);
        switch (enmState)
        {
            case RTTIMERLNXSTATE_CB_DESTROYING:
                rtTimerLnxCallbackDestroy(pTimer, pSubTimer);
                return HRTIMER_NORESTART;

            case RTTIMERLNXSTATE_CB_STOPPING:
                if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED, RTTIMERLNXSTATE_CB_STOPPING))
                    return HRTIMER_NORESTART;
                break;

            case RTTIMERLNXSTATE_CB_RESTARTING:
                if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_ACTIVE, RTTIMERLNXSTATE_CB_RESTARTING))
                {
                    pSubTimer->iTick = 0;
                    hrtimer_set_expires(&pSubTimer->u.Hr.LnxTimer, rtTimerLnxNanoToKt(pSubTimer->uNsRestartAt));
                    return HRTIMER_RESTART;
                }
                break;

            default:
                AssertMsgFailed(("%d\n", enmState));
                return HRTIMER_NORESTART;
        }
        ASMNopPause();
    }
}
#endif /* RTTIMER_LINUX_WITH_HRTIMER */


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
/**
 * Timer callback function for standard timers.
 *
 * @param   pLnxTimer   Pointer to the Linux timer structure.
 */
static void rtTimerLinuxStdCallback(struct timer_list *pLnxTimer)
{
    PRTTIMERLNXSUBTIMER pSubTimer = from_timer(pSubTimer, pLnxTimer, u.Std.LnxTimer);
#else
/**
 * Timer callback function for standard timers.
 *
 * @param   ulUser      Address of the sub-timer structure.
 */
static void rtTimerLinuxStdCallback(unsigned long ulUser)
{
    PRTTIMERLNXSUBTIMER pSubTimer = (PRTTIMERLNXSUBTIMER)ulUser;
#endif
    PRTTIMER            pTimer    = pSubTimer->pParent;

    RTTIMERLNX_LOG(("stdcallback %p\n", pTimer));
    if (RT_UNLIKELY(!rtTimerLnxChangeToCallbackState(pSubTimer)))
        return;

#ifdef CONFIG_SMP
    /*
     * Check for unwanted migration.
     */
    if (pTimer->fAllCpus || pTimer->fSpecificCpu)
    {
        RTCPUID idCpu = RTMpCpuId();
        if (RT_UNLIKELY(  pTimer->fAllCpus
                        ? (RTCPUID)(pSubTimer - &pTimer->aSubTimers[0]) != idCpu
                        : pTimer->idCpu != idCpu))
        {
            rtTimerLnxCallbackHandleMigration(pTimer, pSubTimer);
            return;
        }
    }
#endif

    if (pTimer->u64NanoInterval)
    {
        /*
         * Interval timer, calculate the next timeout.
         *
         * The first time around, we'll re-adjust the u.Std.u64NextTS to
         * try prevent some jittering if we were started at a bad time.
         */
        const uint64_t  iTick = ++pSubTimer->iTick;
        uint64_t        u64NanoInterval;
        unsigned long   cJiffies;
        unsigned long   flFlags;

        spin_lock_irqsave(&pTimer->ChgIntLock, flFlags);
        u64NanoInterval = pTimer->u64NanoInterval;
        cJiffies        = pTimer->cJiffies;
        if (RT_UNLIKELY(pSubTimer->u.Std.fFirstAfterChg))
        {
            pSubTimer->u.Std.fFirstAfterChg = false;
            pSubTimer->u.Std.u64NextTS      = RTTimeSystemNanoTS();
            pSubTimer->u.Std.ulNextJiffies  = jiffies;
        }
        spin_unlock_irqrestore(&pTimer->ChgIntLock, flFlags);

        pSubTimer->u.Std.u64NextTS += u64NanoInterval;
        if (cJiffies)
        {
            pSubTimer->u.Std.ulNextJiffies += cJiffies;
            /* Prevent overflows when the jiffies counter wraps around.
             * Special thanks to Ken Preslan for helping debugging! */
            while (time_before(pSubTimer->u.Std.ulNextJiffies, jiffies))
            {
                pSubTimer->u.Std.ulNextJiffies += cJiffies;
                pSubTimer->u.Std.u64NextTS     += u64NanoInterval;
            }
        }
        else
        {
            const uint64_t u64NanoTS = RTTimeSystemNanoTS();
            while (pSubTimer->u.Std.u64NextTS < u64NanoTS)
                pSubTimer->u.Std.u64NextTS += u64NanoInterval;
            pSubTimer->u.Std.ulNextJiffies = jiffies + rtTimerLnxNanoToJiffies(pSubTimer->u.Std.u64NextTS - u64NanoTS);
        }

        /*
         * Run the timer and re-arm it unless the state changed                                                                                                        .
         *                                                                                                                                                             .
         * We must re-arm it afterwards as we're not in a position to undo this                                                                                        .
         * operation if for instance someone stopped or destroyed us while we                                                                                          .
         * were in the callback.  (Linux takes care of any races here.)
         */
        pTimer->pfnTimer(pTimer, pTimer->pvUser, iTick);
        if (RT_LIKELY(rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_ACTIVE, RTTIMERLNXSTATE_CALLBACK)))
        {
#ifdef CONFIG_SMP
            if (pTimer->fSpecificCpu || pTimer->fAllCpus)
            {
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
                mod_timer(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
# else
                mod_timer_pinned(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
# endif
            }
            else
#endif
                mod_timer(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
            return;
        }
    }
    else
    {
        /*
         * One shot timer, stop it before dispatching it.
         * Allow RTTimerStart as well as RTTimerDestroy to be called from
         * the callback.
         */
        ASMAtomicWriteBool(&pTimer->fSuspended, true);
        pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pSubTimer->iTick);
        if (RT_LIKELY(rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED, RTTIMERLNXSTATE_CALLBACK)))
            return;
    }

    /*
     * Some state change occurred while we were in the callback routine.
     */
    for (;;)
    {
        RTTIMERLNXSTATE enmState = rtTimerLnxGetState(&pSubTimer->enmState);
        switch (enmState)
        {
            case RTTIMERLNXSTATE_CB_DESTROYING:
                rtTimerLnxCallbackDestroy(pTimer, pSubTimer);
                return;

            case RTTIMERLNXSTATE_CB_STOPPING:
                if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED, RTTIMERLNXSTATE_CB_STOPPING))
                    return;
                break;

            case RTTIMERLNXSTATE_CB_RESTARTING:
                if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_ACTIVE, RTTIMERLNXSTATE_CB_RESTARTING))
                {
                    uint64_t        u64NanoTS;
                    uint64_t        u64NextTS;
                    unsigned long   flFlags;

                    spin_lock_irqsave(&pTimer->ChgIntLock, flFlags);
                    u64NextTS = pSubTimer->uNsRestartAt;
                    u64NanoTS = RTTimeSystemNanoTS();
                    pSubTimer->iTick                = 0;
                    pSubTimer->u.Std.u64NextTS      = u64NextTS;
                    pSubTimer->u.Std.fFirstAfterChg = true;
                    pSubTimer->u.Std.ulNextJiffies  = u64NextTS > u64NanoTS
                                                    ? jiffies + rtTimerLnxNanoToJiffies(u64NextTS - u64NanoTS)
                                                    : jiffies;
                    spin_unlock_irqrestore(&pTimer->ChgIntLock, flFlags);

#ifdef CONFIG_SMP
                    if (pTimer->fSpecificCpu || pTimer->fAllCpus)
                    {
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
                        mod_timer(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
# else
                        mod_timer_pinned(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
# endif
                    }
                    else
#endif
                        mod_timer(&pSubTimer->u.Std.LnxTimer, pSubTimer->u.Std.ulNextJiffies);
                    return;
                }
                break;

            default:
                AssertMsgFailed(("%d\n", enmState));
                return;
        }
        ASMNopPause();
    }
}


#ifdef CONFIG_SMP

/**
 * Per-cpu callback function (RTMpOnAll/RTMpOnSpecific).
 *
 * @param   idCpu       The current CPU.
 * @param   pvUser1     Pointer to the timer.
 * @param   pvUser2     Pointer to the argument structure.
 */
static DECLCALLBACK(void) rtTimerLnxStartAllOnCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PRTTIMERLINUXSTARTONCPUARGS pArgs = (PRTTIMERLINUXSTARTONCPUARGS)pvUser2;
    PRTTIMER pTimer = (PRTTIMER)pvUser1;
    Assert(idCpu < pTimer->cCpus);
    rtTimerLnxStartSubTimer(&pTimer->aSubTimers[idCpu], pArgs->u64Now, pArgs->u64First, true /*fPinned*/, pTimer->fHighRes);
}


/**
 * Worker for RTTimerStart() that takes care of the ugly bits.
 *
 * @returns RTTimerStart() return value.
 * @param   pTimer      The timer.
 * @param   pArgs       The argument structure.
 */
static int rtTimerLnxOmniStart(PRTTIMER pTimer, PRTTIMERLINUXSTARTONCPUARGS pArgs)
{
    RTCPUID         iCpu;
    RTCPUSET        OnlineSet;
    RTCPUSET        OnlineSet2;
    int             rc2;

    /*
     * Prepare all the sub-timers for the startup and then flag the timer
     * as a whole as non-suspended, make sure we get them all before
     * clearing fSuspended as the MP handler will be waiting on this
     * should something happen while we're looping.
     */
    RTSpinlockAcquire(pTimer->hSpinlock);

    /* Just make it a omni timer restriction that no stop/start races are allowed. */
    for (iCpu = 0; iCpu < pTimer->cCpus; iCpu++)
        if (rtTimerLnxGetState(&pTimer->aSubTimers[iCpu].enmState) != RTTIMERLNXSTATE_STOPPED)
        {
            RTSpinlockRelease(pTimer->hSpinlock);
            return VERR_TIMER_BUSY;
        }

    do
    {
        RTMpGetOnlineSet(&OnlineSet);
        for (iCpu = 0; iCpu < pTimer->cCpus; iCpu++)
        {
            Assert(pTimer->aSubTimers[iCpu].enmState != RTTIMERLNXSTATE_MP_STOPPING);
            rtTimerLnxSetState(&pTimer->aSubTimers[iCpu].enmState,
                               RTCpuSetIsMember(&OnlineSet, iCpu)
                               ? RTTIMERLNXSTATE_STARTING
                               : RTTIMERLNXSTATE_STOPPED);
        }
    } while (!RTCpuSetIsEqual(&OnlineSet, RTMpGetOnlineSet(&OnlineSet2)));

    ASMAtomicWriteBool(&pTimer->fSuspended, false);

    RTSpinlockRelease(pTimer->hSpinlock);

    /*
     * Start them (can't find any exported function that allows me to
     * do this without the cross calls).
     */
    pArgs->u64Now = RTTimeSystemNanoTS();
    rc2 = RTMpOnAll(rtTimerLnxStartAllOnCpu, pTimer, pArgs);
    AssertRC(rc2); /* screw this if it fails. */

    /*
     * Reset the sub-timers who didn't start up (ALL CPUs case).
     */
    RTSpinlockAcquire(pTimer->hSpinlock);

    for (iCpu = 0; iCpu < pTimer->cCpus; iCpu++)
        if (rtTimerLnxCmpXchgState(&pTimer->aSubTimers[iCpu].enmState, RTTIMERLNXSTATE_STOPPED, RTTIMERLNXSTATE_STARTING))
        {
            /** @todo very odd case for a rainy day. Cpus that temporarily went offline while
             * we were between calls needs to nudged as the MP handler will ignore events for
             * them because of the STARTING state. This is an extremely unlikely case - not that
             * that means anything in my experience... ;-) */
            RTTIMERLNX_LOG(("what!? iCpu=%u -> didn't start\n", iCpu));
        }

    RTSpinlockRelease(pTimer->hSpinlock);

    return VINF_SUCCESS;
}


/**
 * Worker for RTTimerStop() that takes care of the ugly SMP bits.
 *
 * @returns true if there was any active callbacks, false if not.
 * @param   pTimer      The timer (valid).
 * @param   fForDestroy Whether this is for RTTimerDestroy or not.
 */
static bool rtTimerLnxOmniStop(PRTTIMER pTimer, bool fForDestroy)
{
    bool            fActiveCallbacks = false;
    RTCPUID         iCpu;
    RTTIMERLNXSTATE enmState;


    /*
     * Mark the timer as suspended and flag all timers as stopping, except
     * for those being stopped by an MP event.
     */
    RTSpinlockAcquire(pTimer->hSpinlock);

    ASMAtomicWriteBool(&pTimer->fSuspended, true);
    for (iCpu = 0; iCpu < pTimer->cCpus; iCpu++)
    {
        for (;;)
        {
            enmState = rtTimerLnxGetState(&pTimer->aSubTimers[iCpu].enmState);
            if (    enmState == RTTIMERLNXSTATE_STOPPED
                ||  enmState == RTTIMERLNXSTATE_MP_STOPPING)
                break;
            if (   enmState == RTTIMERLNXSTATE_CALLBACK
                || enmState == RTTIMERLNXSTATE_CB_STOPPING
                || enmState == RTTIMERLNXSTATE_CB_RESTARTING)
            {
                Assert(enmState != RTTIMERLNXSTATE_CB_STOPPING || fForDestroy);
                if (rtTimerLnxCmpXchgState(&pTimer->aSubTimers[iCpu].enmState,
                                           !fForDestroy ? RTTIMERLNXSTATE_CB_STOPPING : RTTIMERLNXSTATE_CB_DESTROYING,
                                           enmState))
                {
                    fActiveCallbacks = true;
                    break;
                }
            }
            else
            {
                Assert(enmState == RTTIMERLNXSTATE_ACTIVE);
                if (rtTimerLnxCmpXchgState(&pTimer->aSubTimers[iCpu].enmState, RTTIMERLNXSTATE_STOPPING, enmState))
                    break;
            }
            ASMNopPause();
        }
    }

    RTSpinlockRelease(pTimer->hSpinlock);

    /*
     * Do the actual stopping. Fortunately, this doesn't require any IPIs.
     * Unfortunately it cannot be done synchronously.
     */
    for (iCpu = 0; iCpu < pTimer->cCpus; iCpu++)
        if (rtTimerLnxGetState(&pTimer->aSubTimers[iCpu].enmState) == RTTIMERLNXSTATE_STOPPING)
            rtTimerLnxStopSubTimer(&pTimer->aSubTimers[iCpu], pTimer->fHighRes);

    return fActiveCallbacks;
}


/**
 * Per-cpu callback function (RTMpOnSpecific) used by rtTimerLinuxMpEvent()
 * to start a sub-timer on a cpu that just have come online.
 *
 * @param   idCpu       The current CPU.
 * @param   pvUser1     Pointer to the timer.
 * @param   pvUser2     Pointer to the argument structure.
 */
static DECLCALLBACK(void) rtTimerLinuxMpStartOnCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PRTTIMERLINUXSTARTONCPUARGS pArgs = (PRTTIMERLINUXSTARTONCPUARGS)pvUser2;
    PRTTIMER pTimer = (PRTTIMER)pvUser1;
    RTSPINLOCK hSpinlock;
    Assert(idCpu < pTimer->cCpus);

    /*
     * We have to be kind of careful here as we might be racing RTTimerStop
     * (and/or RTTimerDestroy, thus the paranoia.
     */
    hSpinlock = pTimer->hSpinlock;
    if (    hSpinlock != NIL_RTSPINLOCK
        &&  pTimer->u32Magic == RTTIMER_MAGIC)
    {
        RTSpinlockAcquire(hSpinlock);

        if (    !ASMAtomicUoReadBool(&pTimer->fSuspended)
            &&  pTimer->u32Magic == RTTIMER_MAGIC)
        {
            /* We're sane and the timer is not suspended yet. */
            PRTTIMERLNXSUBTIMER pSubTimer = &pTimer->aSubTimers[idCpu];
            if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_MP_STARTING, RTTIMERLNXSTATE_STOPPED))
                rtTimerLnxStartSubTimer(pSubTimer, pArgs->u64Now, pArgs->u64First, true /*fPinned*/, pTimer->fHighRes);
        }

        RTSpinlockRelease(hSpinlock);
    }
}


/**
 * MP event notification callback.
 *
 * @param   enmEvent    The event.
 * @param   idCpu       The cpu it applies to.
 * @param   pvUser      The timer.
 */
static DECLCALLBACK(void) rtTimerLinuxMpEvent(RTMPEVENT enmEvent, RTCPUID idCpu, void *pvUser)
{
    PRTTIMER            pTimer    = (PRTTIMER)pvUser;
    PRTTIMERLNXSUBTIMER pSubTimer = &pTimer->aSubTimers[idCpu];
    RTSPINLOCK          hSpinlock;

    Assert(idCpu < pTimer->cCpus);

    /*
     * Some initial paranoia.
     */
    if (pTimer->u32Magic != RTTIMER_MAGIC)
        return;
    hSpinlock = pTimer->hSpinlock;
    if (hSpinlock == NIL_RTSPINLOCK)
        return;

    RTSpinlockAcquire(hSpinlock);

    /* Is it active? */
    if (    !ASMAtomicUoReadBool(&pTimer->fSuspended)
        &&  pTimer->u32Magic == RTTIMER_MAGIC)
    {
        switch (enmEvent)
        {
            /*
             * Try do it without leaving the spin lock, but if we have to, retake it
             * when we're on the right cpu.
             */
            case RTMPEVENT_ONLINE:
                if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_MP_STARTING, RTTIMERLNXSTATE_STOPPED))
                {
                    RTTIMERLINUXSTARTONCPUARGS Args;
                    Args.u64Now = RTTimeSystemNanoTS();
                    Args.u64First = 0;

                    if (RTMpCpuId() == idCpu)
                        rtTimerLnxStartSubTimer(pSubTimer, Args.u64Now, Args.u64First, true /*fPinned*/, pTimer->fHighRes);
                    else
                    {
                        rtTimerLnxSetState(&pSubTimer->enmState, RTTIMERLNXSTATE_STOPPED); /* we'll recheck it. */
                        RTSpinlockRelease(hSpinlock);

                        RTMpOnSpecific(idCpu, rtTimerLinuxMpStartOnCpu, pTimer, &Args);
                        return; /* we've left the spinlock */
                    }
                }
                break;

            /*
             * The CPU is (going) offline, make sure the sub-timer is stopped.
             *
             * Linux will migrate it to a different CPU, but we don't want this. The
             * timer function is checking for this.
             */
            case RTMPEVENT_OFFLINE:
            {
                RTTIMERLNXSTATE enmState;
                while (   (enmState = rtTimerLnxGetState(&pSubTimer->enmState)) == RTTIMERLNXSTATE_ACTIVE
                       || enmState == RTTIMERLNXSTATE_CALLBACK
                       || enmState == RTTIMERLNXSTATE_CB_RESTARTING)
                {
                    if (enmState == RTTIMERLNXSTATE_ACTIVE)
                    {
                        if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_MP_STOPPING, RTTIMERLNXSTATE_ACTIVE))
                        {
                            RTSpinlockRelease(hSpinlock);

                            rtTimerLnxStopSubTimer(pSubTimer, pTimer->fHighRes);
                            return; /* we've left the spinlock */
                        }
                    }
                    else if (rtTimerLnxCmpXchgState(&pSubTimer->enmState, RTTIMERLNXSTATE_CB_STOPPING, enmState))
                        break;

                    /* State not stable, try again. */
                    ASMNopPause();
                }
                break;
            }
        }
    }

    RTSpinlockRelease(hSpinlock);
}

#endif /* CONFIG_SMP */


/**
 * Callback function use by RTTimerStart via RTMpOnSpecific to start a timer
 * running on a specific CPU.
 *
 * @param   idCpu       The current CPU.
 * @param   pvUser1     Pointer to the timer.
 * @param   pvUser2     Pointer to the argument structure.
 */
static DECLCALLBACK(void) rtTimerLnxStartOnSpecificCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PRTTIMERLINUXSTARTONCPUARGS pArgs = (PRTTIMERLINUXSTARTONCPUARGS)pvUser2;
    PRTTIMER pTimer = (PRTTIMER)pvUser1;
    RT_NOREF_PV(idCpu);
    rtTimerLnxStartSubTimer(&pTimer->aSubTimers[0], pArgs->u64Now, pArgs->u64First, true /*fPinned*/, pTimer->fHighRes);
}


RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First)
{
    RTTIMERLINUXSTARTONCPUARGS Args;
    int rc2;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate.
     */
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);

    if (!ASMAtomicUoReadBool(&pTimer->fSuspended))
        return VERR_TIMER_ACTIVE;
    RTTIMERLNX_LOG(("start %p cCpus=%d\n", pTimer, pTimer->cCpus));

    Args.u64First = u64First;
#ifdef CONFIG_SMP
    /*
     * Omni timer?
     */
    if (pTimer->fAllCpus)
    {
        rc2 = rtTimerLnxOmniStart(pTimer, &Args);
        IPRT_LINUX_RESTORE_EFL_AC();
        return rc2;
    }
#endif

    /*
     * Simple timer - Pretty straight forward if it wasn't for restarting.
     */
    Args.u64Now = RTTimeSystemNanoTS();
    ASMAtomicWriteU64(&pTimer->aSubTimers[0].uNsRestartAt, Args.u64Now + u64First);
    for (;;)
    {
        RTTIMERLNXSTATE enmState = rtTimerLnxGetState(&pTimer->aSubTimers[0].enmState);
        switch (enmState)
        {
            case RTTIMERLNXSTATE_STOPPED:
                if (rtTimerLnxCmpXchgState(&pTimer->aSubTimers[0].enmState, RTTIMERLNXSTATE_STARTING, RTTIMERLNXSTATE_STOPPED))
                {
                    ASMAtomicWriteBool(&pTimer->fSuspended, false);
                    if (!pTimer->fSpecificCpu)
                        rtTimerLnxStartSubTimer(&pTimer->aSubTimers[0], Args.u64Now, Args.u64First,
                                                false /*fPinned*/, pTimer->fHighRes);
                    else
                    {
                        rc2 = RTMpOnSpecific(pTimer->idCpu, rtTimerLnxStartOnSpecificCpu, pTimer, &Args);
                        if (RT_FAILURE(rc2))
                        {
                            /* Suspend it, the cpu id is probably invalid or offline. */
                            ASMAtomicWriteBool(&pTimer->fSuspended, true);
                            rtTimerLnxSetState(&pTimer->aSubTimers[0].enmState, RTTIMERLNXSTATE_STOPPED);
                            return rc2;
                        }
                    }
                    IPRT_LINUX_RESTORE_EFL_AC();
                    return VINF_SUCCESS;
                }
                break;

            case RTTIMERLNXSTATE_CALLBACK:
            case RTTIMERLNXSTATE_CB_STOPPING:
                if (rtTimerLnxCmpXchgState(&pTimer->aSubTimers[0].enmState, RTTIMERLNXSTATE_CB_RESTARTING, enmState))
                {
                    ASMAtomicWriteBool(&pTimer->fSuspended, false);
                    IPRT_LINUX_RESTORE_EFL_AC();
                    return VINF_SUCCESS;
                }
                break;

            default:
                AssertMsgFailed(("%d\n", enmState));
                IPRT_LINUX_RESTORE_EFL_AC();
                return VERR_INTERNAL_ERROR_4;
        }
        ASMNopPause();
    }
}
RT_EXPORT_SYMBOL(RTTimerStart);


/**
 * Common worker for RTTimerStop and RTTimerDestroy.
 *
 * @returns true if there was any active callbacks, false if not.
 * @param   pTimer              The timer to stop.
 * @param   fForDestroy         Whether it's RTTimerDestroy calling or not.
 */
static bool rtTimerLnxStop(PRTTIMER pTimer, bool fForDestroy)
{
    RTTIMERLNX_LOG(("lnxstop %p %d\n", pTimer, fForDestroy));
#ifdef CONFIG_SMP
    /*
     * Omni timer?
     */
    if (pTimer->fAllCpus)
        return rtTimerLnxOmniStop(pTimer, fForDestroy);
#endif

    /*
     * Simple timer.
     */
    ASMAtomicWriteBool(&pTimer->fSuspended, true);
    for (;;)
    {
        RTTIMERLNXSTATE enmState = rtTimerLnxGetState(&pTimer->aSubTimers[0].enmState);
        switch (enmState)
        {
            case RTTIMERLNXSTATE_ACTIVE:
                if (rtTimerLnxCmpXchgState(&pTimer->aSubTimers[0].enmState, RTTIMERLNXSTATE_STOPPING, RTTIMERLNXSTATE_ACTIVE))
                {
                    rtTimerLnxStopSubTimer(&pTimer->aSubTimers[0], pTimer->fHighRes);
                    return false;
                }
                break;

            case RTTIMERLNXSTATE_CALLBACK:
            case RTTIMERLNXSTATE_CB_RESTARTING:
            case RTTIMERLNXSTATE_CB_STOPPING:
                Assert(enmState != RTTIMERLNXSTATE_CB_STOPPING || fForDestroy);
                if (rtTimerLnxCmpXchgState(&pTimer->aSubTimers[0].enmState,
                                           !fForDestroy ? RTTIMERLNXSTATE_CB_STOPPING : RTTIMERLNXSTATE_CB_DESTROYING,
                                           enmState))
                    return true;
                break;

            case RTTIMERLNXSTATE_STOPPED:
                return VINF_SUCCESS;

            case RTTIMERLNXSTATE_CB_DESTROYING:
                AssertMsgFailed(("enmState=%d pTimer=%p\n", enmState, pTimer));
                return true;

            default:
            case RTTIMERLNXSTATE_STARTING:
            case RTTIMERLNXSTATE_MP_STARTING:
            case RTTIMERLNXSTATE_STOPPING:
            case RTTIMERLNXSTATE_MP_STOPPING:
                AssertMsgFailed(("enmState=%d pTimer=%p\n", enmState, pTimer));
                return false;
        }

        /* State not stable, try again. */
        ASMNopPause();
    }
}


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    /*
     * Validate.
     */
    IPRT_LINUX_SAVE_EFL_AC();
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);
    RTTIMERLNX_LOG(("stop %p\n", pTimer));

    if (ASMAtomicUoReadBool(&pTimer->fSuspended))
        return VERR_TIMER_SUSPENDED;

    rtTimerLnxStop(pTimer, false /*fForDestroy*/);

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTTimerStop);


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    unsigned long cJiffies;
    unsigned long flFlags;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate.
     */
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(u64NanoInterval, VERR_INVALID_PARAMETER);
    AssertReturn(u64NanoInterval < UINT64_MAX / 8, VERR_INVALID_PARAMETER);
    AssertReturn(pTimer->u64NanoInterval, VERR_INVALID_STATE);
    RTTIMERLNX_LOG(("change %p %llu\n", pTimer, u64NanoInterval));

#ifdef RTTIMER_LINUX_WITH_HRTIMER
    /*
     * For the high resolution timers it is easy since we don't care so much
     * about when it is applied to the sub-timers.
     */
    if (pTimer->fHighRes)
    {
        ASMAtomicWriteU64(&pTimer->u64NanoInterval, u64NanoInterval);
        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
#endif

    /*
     * Standard timers have a bit more complicated way of calculating
     * their interval and such. So, forget omni timers for now.
     */
    if (pTimer->cCpus > 1)
        return VERR_NOT_SUPPORTED;

    cJiffies = u64NanoInterval / RTTimerGetSystemGranularity();
    if (cJiffies * RTTimerGetSystemGranularity() != u64NanoInterval)
        cJiffies = 0;

    spin_lock_irqsave(&pTimer->ChgIntLock, flFlags);
    pTimer->aSubTimers[0].u.Std.fFirstAfterChg = true;
    pTimer->cJiffies = cJiffies;
    ASMAtomicWriteU64(&pTimer->u64NanoInterval, u64NanoInterval);
    spin_unlock_irqrestore(&pTimer->ChgIntLock, flFlags);
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTTimerChangeInterval);


RTDECL(int) RTTimerDestroy(PRTTIMER pTimer)
{
    bool fCanDestroy;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate. It's ok to pass NULL pointer.
     */
    if (pTimer == /*NIL_RTTIMER*/ NULL)
        return VINF_SUCCESS;
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);
    RTTIMERLNX_LOG(("destroy %p\n", pTimer));
/** @todo We should invalidate the magic here! */

    /*
     * Stop the timer if it's still active, then destroy it if we can.
     */
    if (!ASMAtomicUoReadBool(&pTimer->fSuspended))
        fCanDestroy = rtTimerLnxStop(pTimer, true /*fForDestroy*/);
    else
    {
        uint32_t        iCpu = pTimer->cCpus;
        if (pTimer->cCpus > 1)
            RTSpinlockAcquire(pTimer->hSpinlock);

        fCanDestroy = true;
        while (iCpu-- > 0)
        {
            for (;;)
            {
                RTTIMERLNXSTATE enmState = rtTimerLnxGetState(&pTimer->aSubTimers[iCpu].enmState);
                switch (enmState)
                {
                    case RTTIMERLNXSTATE_CALLBACK:
                    case RTTIMERLNXSTATE_CB_RESTARTING:
                    case RTTIMERLNXSTATE_CB_STOPPING:
                        if (!rtTimerLnxCmpXchgState(&pTimer->aSubTimers[iCpu].enmState, RTTIMERLNXSTATE_CB_DESTROYING, enmState))
                            continue;
                        fCanDestroy = false;
                        break;

                    case RTTIMERLNXSTATE_CB_DESTROYING:
                        AssertMsgFailed(("%d\n", enmState));
                        fCanDestroy = false;
                        break;
                    default:
                        break;
                }
                break;
            }
        }

        if (pTimer->cCpus > 1)
            RTSpinlockRelease(pTimer->hSpinlock);
    }

    if (fCanDestroy)
    {
        /* For paranoid reasons, defer actually destroying the semaphore when
           in atomic or interrupt context. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 32)
        if (in_atomic() || in_interrupt())
#else
        if (in_interrupt())
#endif
            rtR0LnxWorkqueuePush(&pTimer->DtorWorkqueueItem, rtTimerLnxDestroyDeferred);
        else
            rtTimerLnxDestroyIt(pTimer);
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTTimerDestroy);


RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    PRTTIMER    pTimer;
    RTCPUID     iCpu;
    unsigned    cCpus;
    int         rc;
    IPRT_LINUX_SAVE_EFL_AC();

    rtR0LnxWorkqueueFlush();                /* for 2.4 */
    *ppTimer = NULL;

    /*
     * Validate flags.
     */
    if (!RTTIMER_FLAGS_ARE_VALID(fFlags))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_INVALID_PARAMETER;
    }
    if (    (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        &&  (fFlags & RTTIMER_FLAGS_CPU_ALL) != RTTIMER_FLAGS_CPU_ALL
        &&  !RTMpIsCpuPossible(RTMpCpuIdFromSetIndex(fFlags & RTTIMER_FLAGS_CPU_MASK)))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_CPU_NOT_FOUND;
    }

    /*
     * Allocate the timer handler.
     */
    cCpus = 1;
#ifdef CONFIG_SMP
    if ((fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL)
    {
        cCpus = RTMpGetMaxCpuId() + 1;
        Assert(cCpus <= RTCPUSET_MAX_CPUS); /* On linux we have a 1:1 relationship between cpuid and set index. */
        AssertReturnStmt(u64NanoInterval, IPRT_LINUX_RESTORE_EFL_AC(), VERR_NOT_IMPLEMENTED); /* We don't implement single shot on all cpus, sorry. */
    }
#endif

    rc = RTMemAllocEx(RT_UOFFSETOF_DYN(RTTIMER, aSubTimers[cCpus]), 0,
                      RTMEMALLOCEX_FLAGS_ZEROED | RTMEMALLOCEX_FLAGS_ANY_CTX_FREE, (void **)&pTimer);
    if (RT_FAILURE(rc))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return rc;
    }

    /*
     * Initialize it.
     */
    pTimer->u32Magic        = RTTIMER_MAGIC;
    pTimer->hSpinlock       = NIL_RTSPINLOCK;
    pTimer->fSuspended      = true;
    pTimer->fHighRes        = !!(fFlags & RTTIMER_FLAGS_HIGH_RES);
#ifdef CONFIG_SMP
    pTimer->fSpecificCpu    = (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC) && (fFlags & RTTIMER_FLAGS_CPU_ALL) != RTTIMER_FLAGS_CPU_ALL;
    pTimer->fAllCpus        = (fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL;
    pTimer->idCpu           = pTimer->fSpecificCpu
                            ? RTMpCpuIdFromSetIndex(fFlags & RTTIMER_FLAGS_CPU_MASK)
                            : NIL_RTCPUID;
#else
    pTimer->fSpecificCpu    = !!(fFlags & RTTIMER_FLAGS_CPU_SPECIFIC);
    pTimer->idCpu           = RTMpCpuId();
#endif
    pTimer->cCpus           = cCpus;
    pTimer->pfnTimer        = pfnTimer;
    pTimer->pvUser          = pvUser;
    pTimer->u64NanoInterval = u64NanoInterval;
    pTimer->cJiffies        = u64NanoInterval / RTTimerGetSystemGranularity();
    if (pTimer->cJiffies * RTTimerGetSystemGranularity() != u64NanoInterval)
        pTimer->cJiffies    = 0;
    spin_lock_init(&pTimer->ChgIntLock);

    for (iCpu = 0; iCpu < cCpus; iCpu++)
    {
#ifdef RTTIMER_LINUX_WITH_HRTIMER
        if (pTimer->fHighRes)
        {
            hrtimer_init(&pTimer->aSubTimers[iCpu].u.Hr.LnxTimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
            pTimer->aSubTimers[iCpu].u.Hr.LnxTimer.function     = rtTimerLinuxHrCallback;
        }
        else
#endif
        {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
            timer_setup(&pTimer->aSubTimers[iCpu].u.Std.LnxTimer, rtTimerLinuxStdCallback, TIMER_PINNED);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
            init_timer_pinned(&pTimer->aSubTimers[iCpu].u.Std.LnxTimer);
#else
            init_timer(&pTimer->aSubTimers[iCpu].u.Std.LnxTimer);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
            pTimer->aSubTimers[iCpu].u.Std.LnxTimer.data        = (unsigned long)&pTimer->aSubTimers[iCpu];
            pTimer->aSubTimers[iCpu].u.Std.LnxTimer.function    = rtTimerLinuxStdCallback;
#endif
            pTimer->aSubTimers[iCpu].u.Std.LnxTimer.expires     = jiffies;
            pTimer->aSubTimers[iCpu].u.Std.u64NextTS            = 0;
        }
        pTimer->aSubTimers[iCpu].iTick      = 0;
        pTimer->aSubTimers[iCpu].pParent    = pTimer;
        pTimer->aSubTimers[iCpu].enmState   = RTTIMERLNXSTATE_STOPPED;
    }

#ifdef CONFIG_SMP
    /*
     * If this is running on ALL cpus, we'll have to register a callback
     * for MP events (so timers can be started/stopped on cpus going
     * online/offline). We also create the spinlock for synchronizing
     * stop/start/mp-event.
     */
    if (cCpus > 1)
    {
        int rc = RTSpinlockCreate(&pTimer->hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "RTTimerLnx");
        if (RT_SUCCESS(rc))
            rc = RTMpNotificationRegister(rtTimerLinuxMpEvent, pTimer);
        else
            pTimer->hSpinlock = NIL_RTSPINLOCK;
        if (RT_FAILURE(rc))
        {
            RTTimerDestroy(pTimer);
            IPRT_LINUX_RESTORE_EFL_AC();
            return rc;
        }
    }
#endif /* CONFIG_SMP */

    RTTIMERLNX_LOG(("create %p hires=%d fFlags=%#x cCpus=%u\n", pTimer, pTimer->fHighRes, fFlags, cCpus));
    *ppTimer = pTimer;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTTimerCreateEx);


RTDECL(uint32_t) RTTimerGetSystemGranularity(void)
{
#if 0 /** @todo Not sure if this is what we want or not... Add new API for
       *        querying the resolution of the high res timers? */
    struct timespec Ts;
    int rc;
    IPRT_LINUX_SAVE_EFL_AC();
    rc = hrtimer_get_res(CLOCK_MONOTONIC, &Ts);
    IPRT_LINUX_RESTORE_EFL_AC();
    if (!rc)
    {
        Assert(!Ts.tv_sec);
        return Ts.tv_nsec;
    }
#endif
    return RT_NS_1SEC / HZ; /* ns */
}
RT_EXPORT_SYMBOL(RTTimerGetSystemGranularity);


RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted)
{
    RT_NOREF_PV(u32Request); RT_NOREF_PV(*pu32Granted);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTTimerRequestSystemGranularity);


RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted)
{
    RT_NOREF_PV(u32Granted);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTTimerReleaseSystemGranularity);


RTDECL(bool) RTTimerCanDoHighResolution(void)
{
#ifdef RTTIMER_LINUX_WITH_HRTIMER
    return true;
#else
    return false;
#endif
}
RT_EXPORT_SYMBOL(RTTimerCanDoHighResolution);

