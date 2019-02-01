/** @file
 * IPRT - Threads.
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

#ifndef IPRT_INCLUDED_thread_h
#define IPRT_INCLUDED_thread_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/stdarg.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_thread    RTThread - Thread Management
 * @ingroup grp_rt
 * @{
 */

/**
 * The thread state.
 */
typedef enum RTTHREADSTATE
{
    /** The usual invalid 0 value. */
    RTTHREADSTATE_INVALID = 0,
    /** The thread is being initialized. */
    RTTHREADSTATE_INITIALIZING,
    /** The thread has terminated */
    RTTHREADSTATE_TERMINATED,
    /** Probably running. */
    RTTHREADSTATE_RUNNING,

    /** Waiting on a critical section. */
    RTTHREADSTATE_CRITSECT,
    /** Waiting on a event semaphore. */
    RTTHREADSTATE_EVENT,
    /** Waiting on a event multiple wakeup semaphore. */
    RTTHREADSTATE_EVENT_MULTI,
    /** Waiting on a fast mutex. */
    RTTHREADSTATE_FAST_MUTEX,
    /** Waiting on a mutex. */
    RTTHREADSTATE_MUTEX,
    /** Waiting on a read write semaphore, read (shared) access. */
    RTTHREADSTATE_RW_READ,
    /** Waiting on a read write semaphore, write (exclusive) access. */
    RTTHREADSTATE_RW_WRITE,
    /** The thread is sleeping. */
    RTTHREADSTATE_SLEEP,
    /** Waiting on a spin mutex. */
    RTTHREADSTATE_SPIN_MUTEX,
    /** End of the thread states. */
    RTTHREADSTATE_END,

    /** The usual 32-bit size hack. */
    RTTHREADSTATE_32BIT_HACK = 0x7fffffff
} RTTHREADSTATE;

/** Checks if a thread state indicates that the thread is sleeping. */
#define RTTHREAD_IS_SLEEPING(enmState) ((enmState) >= RTTHREADSTATE_CRITSECT)

/**
 * Thread types.
 * Besides identifying the purpose of the thread, the thread type is
 * used to select the scheduling properties.
 *
 * The types in are placed in a rough order of ascending priority.
 */
typedef enum RTTHREADTYPE
{
    /** Invalid type. */
    RTTHREADTYPE_INVALID = 0,
    /** Infrequent poller thread.
     * This type of thread will sleep for the most of the time, and do
     * infrequent polls on resources at 0.5 sec or higher intervals.
     */
    RTTHREADTYPE_INFREQUENT_POLLER,
    /** Main heavy worker thread.
     * Thread of this type is driving asynchronous tasks in the Main
     * API which takes a long time and might involve a bit of CPU. Like
     * for instance creating a fixed sized VDI.
     */
    RTTHREADTYPE_MAIN_HEAVY_WORKER,
    /** The emulation thread type.
     * While being a thread with very high workload it still is vital
     * that it gets scheduled frequently. When possible all other thread
     * types except DEFAULT and GUI should interrupt this one ASAP when
     * they become ready.
     */
    RTTHREADTYPE_EMULATION,
    /** The default thread type.
     * Since it doesn't say much about the purpose of the thread
     * nothing special is normally done to the scheduling. This type
     * should be avoided.
     * The main thread is registered with default type during RTR3Init()
     * and that's what the default process priority is derived from.
     */
    RTTHREADTYPE_DEFAULT,
    /** The GUI thread type
     * The GUI normally have a low workload but is frequently scheduled
     * to handle events. When possible the scheduler should not leave
     * threads of this kind waiting for too long (~50ms).
     */
    RTTHREADTYPE_GUI,
    /** Main worker thread.
     * Thread of this type is driving asynchronous tasks in the Main API.
     * In most cases this means little work an a lot of waiting.
     */
    RTTHREADTYPE_MAIN_WORKER,
    /** VRDP I/O thread.
     * These threads are I/O threads in the RDP server will hang around
     * waiting for data, process it and pass it on.
     */
    RTTHREADTYPE_VRDP_IO,
    /** The debugger type.
     * Threads involved in servicing the debugger. It must remain
     * responsive even when things are running wild in.
     */
    RTTHREADTYPE_DEBUGGER,
    /** Message pump thread.
     * Thread pumping messages from one thread/process to another
     * thread/process. The workload is very small, most of the time
     * it's blocked waiting for messages to be produced or processed.
     * This type of thread will be favored after I/O threads.
     */
    RTTHREADTYPE_MSG_PUMP,
    /** The I/O thread type.
     * Doing I/O means shuffling data, waiting for request to arrive and
     * for them to complete. The thread should be favored when competing
     * with any other threads except timer threads.
     */
    RTTHREADTYPE_IO,
    /** The timer thread type.
     * A timer thread is mostly waiting for the timer to tick
     * and then perform a little bit of work. Accuracy is important here,
     * so the thread should be favoured over all threads. If premention can
     * be configured at thread level, it could be made very short.
     */
    RTTHREADTYPE_TIMER,
    /** Only used for validation. */
    RTTHREADTYPE_END
} RTTHREADTYPE;


#ifndef IN_RC

/**
 * Checks if the IPRT thread component has been initialized.
 *
 * This is used to avoid calling into RTThread before the runtime has been
 * initialized.
 *
 * @returns @c true if it's initialized, @c false if not.
 */
RTDECL(bool) RTThreadIsInitialized(void);

/**
 * Get the thread handle of the current thread.
 *
 * @returns Thread handle.
 */
RTDECL(RTTHREAD) RTThreadSelf(void);

/**
 * Get the native thread handle of the current thread.
 *
 * @returns Native thread handle.
 */
RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void);

/**
 * Millisecond granular sleep function.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_INTERRUPTED if a signal or other asynchronous stuff happened
 *          which interrupt the peaceful sleep.
 * @param   cMillies    Number of milliseconds to sleep.
 *                      0 milliseconds means yielding the timeslice - deprecated!
 * @remark  See RTThreadNanoSleep() for sleeping for smaller periods of time.
 */
RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies);

/**
 * Millisecond granular sleep function, no logger calls.
 *
 * Same as RTThreadSleep, except it will never call into the IPRT logger.  It
 * can therefore safely be used in places where the logger is off limits, like
 * at termination or init time.  The electric fence heap is one consumer of
 * this API.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_INTERRUPTED if a signal or other asynchronous stuff happened
 *          which interrupt the peaceful sleep.
 * @param   cMillies    Number of milliseconds to sleep.
 *                      0 milliseconds means yielding the timeslice - deprecated!
 */
RTDECL(int) RTThreadSleepNoLog(RTMSINTERVAL cMillies);

/**
 * Yields the CPU.
 *
 * @returns true if we yielded.
 * @returns false if it's probable that we didn't yield.
 */
RTDECL(bool) RTThreadYield(void);



/**
 * Thread function.
 *
 * @returns 0 on success.
 * @param   ThreadSelf  Thread handle to this thread.
 * @param   pvUser      User argument.
 */
typedef DECLCALLBACK(int) FNRTTHREAD(RTTHREAD ThreadSelf, void *pvUser);
/** Pointer to a FNRTTHREAD(). */
typedef FNRTTHREAD *PFNRTTHREAD;

/**
 * Thread creation flags.
 */
typedef enum RTTHREADFLAGS
{
    /** This flag is used to keep the thread structure around so it can
     * be waited on after termination.  @sa RTThreadWait and
     * RTThreadWaitNoResume.  Not required for RTThreadUserWait and friends!
     */
    RTTHREADFLAGS_WAITABLE = RT_BIT(0),
    /** The bit number corresponding to the RTTHREADFLAGS_WAITABLE mask. */
    RTTHREADFLAGS_WAITABLE_BIT = 0,

    /** Mask of valid flags, use for validation. */
    RTTHREADFLAGS_MASK = RT_BIT(0)
} RTTHREADFLAGS;


/**
 * Create a new thread.
 *
 * @returns iprt status code.
 * @param   pThread     Where to store the thread handle to the new thread. (optional)
 * @param   pfnThread   The thread function.
 * @param   pvUser      User argument.
 * @param   cbStack     The size of the stack for the new thread.
 *                      Use 0 for the default stack size.
 * @param   enmType     The thread type. Used for deciding scheduling attributes
 *                      of the thread.
 * @param   fFlags      Flags of the RTTHREADFLAGS type (ORed together).
 * @param   pszName     Thread name.
 *
 * @remark  When called in Ring-0, this API will create a new kernel thread and not a thread in
 *          the context of the calling process.
 */
RTDECL(int) RTThreadCreate(PRTTHREAD pThread, PFNRTTHREAD pfnThread, void *pvUser, size_t cbStack,
                           RTTHREADTYPE enmType, unsigned fFlags, const char *pszName);
#ifndef RT_OS_LINUX /* XXX crashes genksyms at least on 32-bit Linux hosts */
/** @copydoc RTThreadCreate */
typedef DECLCALLBACKPTR(int, PFNRTTHREADCREATE)(PRTTHREAD pThread, PFNRTTHREAD pfnThread, void *pvUser, size_t cbStack,
                                                RTTHREADTYPE enmType, unsigned fFlags, const char *pszName);
#endif


/**
 * Create a new thread.
 *
 * Same as RTThreadCreate except the name is given in the RTStrPrintfV form.
 *
 * @returns iprt status code.
 * @param   pThread     See RTThreadCreate.
 * @param   pfnThread   See RTThreadCreate.
 * @param   pvUser      See RTThreadCreate.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   fFlags      See RTThreadCreate.
 * @param   pszName     Thread name format.
 * @param   va          Format arguments.
 */
RTDECL(int) RTThreadCreateV(PRTTHREAD pThread, PFNRTTHREAD pfnThread, void *pvUser, size_t cbStack,
                            RTTHREADTYPE enmType, uint32_t fFlags, const char *pszNameFmt, va_list va) RT_IPRT_FORMAT_ATTR(7, 0);

/**
 * Create a new thread.
 *
 * Same as RTThreadCreate except the name is given in the RTStrPrintf form.
 *
 * @returns iprt status code.
 * @param   pThread     See RTThreadCreate.
 * @param   pfnThread   See RTThreadCreate.
 * @param   pvUser      See RTThreadCreate.
 * @param   cbStack     See RTThreadCreate.
 * @param   enmType     See RTThreadCreate.
 * @param   fFlags      See RTThreadCreate.
 * @param   pszName     Thread name format.
 * @param   ...         Format arguments.
 */
RTDECL(int) RTThreadCreateF(PRTTHREAD pThread, PFNRTTHREAD pfnThread, void *pvUser, size_t cbStack,
                            RTTHREADTYPE enmType, uint32_t fFlags, const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR(7, 8);

/**
 * Gets the native thread id of a IPRT thread.
 *
 * @returns The native thread id.
 * @param   Thread      The IPRT thread.
 */
RTDECL(RTNATIVETHREAD) RTThreadGetNative(RTTHREAD Thread);

/**
 * Gets the native thread handle for a IPRT thread.
 *
 * @returns The thread handle. INVALID_HANDLE_VALUE on failure.
 * @param   hThread     The IPRT thread handle.
 *
 * @note    Windows only.
 * @note    Only valid after parent returns from the thread creation call.
 */
RTDECL(uintptr_t) RTThreadGetNativeHandle(RTTHREAD hThread);

/**
 * Gets the IPRT thread of a native thread.
 *
 * @returns The IPRT thread handle
 * @returns NIL_RTTHREAD if not a thread known to IPRT.
 * @param   NativeThread        The native thread handle/id.
 */
RTDECL(RTTHREAD) RTThreadFromNative(RTNATIVETHREAD NativeThread);

/**
 * Changes the type of the specified thread.
 *
 * @returns iprt status code.
 * @param   Thread      The thread which type should be changed.
 * @param   enmType     The new thread type.
 * @remark  In Ring-0 it only works if Thread == RTThreadSelf().
 */
RTDECL(int) RTThreadSetType(RTTHREAD Thread, RTTHREADTYPE enmType);

/**
 * Wait for the thread to terminate, resume on interruption.
 *
 * @returns     iprt status code.
 *              Will not return VERR_INTERRUPTED.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 * @param       prc             Where to store the return code of the thread. Optional.
 */
RTDECL(int) RTThreadWait(RTTHREAD Thread, RTMSINTERVAL cMillies, int *prc);

/**
 * Wait for the thread to terminate, return on interruption.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 * @param       prc             Where to store the return code of the thread. Optional.
 */
RTDECL(int) RTThreadWaitNoResume(RTTHREAD Thread, RTMSINTERVAL cMillies, int *prc);

/**
 * Gets the name of the current thread thread.
 *
 * @returns Pointer to readonly name string.
 * @returns NULL on failure.
 */
RTDECL(const char *) RTThreadSelfName(void);

/**
 * Gets the name of a thread.
 *
 * @returns Pointer to readonly name string.
 * @returns NULL on failure.
 * @param   Thread      Thread handle of the thread to query the name of.
 */
RTDECL(const char *) RTThreadGetName(RTTHREAD Thread);

/**
 * Gets the type of the specified thread.
 *
 * @returns The thread type.
 * @returns RTTHREADTYPE_INVALID if the thread handle is invalid.
 * @param   Thread      The thread in question.
 */
RTDECL(RTTHREADTYPE) RTThreadGetType(RTTHREAD Thread);

/**
 * Sets the name of a thread.
 *
 * @returns iprt status code.
 * @param   Thread      Thread handle of the thread to query the name of.
 * @param   pszName     The thread name.
 */
RTDECL(int) RTThreadSetName(RTTHREAD Thread, const char *pszName);

/**
 * Checks if the specified thread is the main thread.
 *
 * @returns true if it is, false if it isn't.
 *
 * @param   hThread     The thread handle.
 */
RTDECL(bool) RTThreadIsMain(RTTHREAD hThread);

/**
 * Checks if the calling thread is known to IPRT.
 *
 * @returns @c true if it is, @c false if it isn't.
 */
RTDECL(bool) RTThreadIsSelfKnown(void);

/**
 * Checks if the calling thread is know to IPRT and is alive.
 *
 * @returns @c true if it is, @c false if it isn't.
 */
RTDECL(bool) RTThreadIsSelfAlive(void);

/**
 * Checks if the calling thread is known to IPRT.
 *
 * @returns @c true if it is, @c false if it isn't.
 */
RTDECL(bool) RTThreadIsOperational(void);

/**
 * Signal the user event.
 *
 * @returns     iprt status code.
 */
RTDECL(int) RTThreadUserSignal(RTTHREAD Thread);

/**
 * Wait for the user event.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 */
RTDECL(int) RTThreadUserWait(RTTHREAD Thread, RTMSINTERVAL cMillies);

/**
 * Wait for the user event, return on interruption.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 */
RTDECL(int) RTThreadUserWaitNoResume(RTTHREAD Thread, RTMSINTERVAL cMillies);

/**
 * Reset the user event.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to reset.
 */
RTDECL(int) RTThreadUserReset(RTTHREAD Thread);

/**
 * Pokes the thread.
 *
 * This will wake up or/and signal the thread, attempting to interrupt whatever
 * it's currently doing.
 *
 * The posixy version of this will send a signal to the thread, quite likely
 * waking it up from normal sleeps, waits, and I/O.  When IPRT is in
 * non-obtrusive mode, the posixy version will definitely return
 * VERR_NOT_IMPLEMENTED, and it may also do so if no usable signal was found.
 *
 * On Windows the thread will be alerted, waking it up from most sleeps and
 * waits, but not probably very little in the I/O area (needs testing).  On NT
 * 3.50 and 3.1 VERR_NOT_IMPLEMENTED will be returned.
 *
 * @returns IPRT status code.
 *
 * @param   hThread             The thread to poke.  This must not be the
 *                              calling thread.
 *
 * @note    This is *NOT* implemented on all platforms and may cause unresolved
 *          symbols during linking or VERR_NOT_IMPLEMENTED at runtime.
 *
 */
RTDECL(int) RTThreadPoke(RTTHREAD hThread);

# ifdef IN_RING0

/**
 * Check if preemption is currently enabled or not for the current thread.
 *
 * @note    This may return true even on systems where preemption isn't
 *          possible. In that case, it means no call to RTThreadPreemptDisable
 *          has been made and interrupts are still enabled.
 *
 * @returns true if preemption is enabled, false if preemetion is disabled.
 * @param   hThread             Must be NIL_RTTHREAD for now.
 */
RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread);

/**
 * Check if preemption is pending for the current thread.
 *
 * This function should be called regularly when executing larger portions of
 * code with preemption disabled.
 *
 * @returns true if pending, false if not.
 * @param   hThread         Must be NIL_RTTHREAD for now.
 *
 * @note    If called with interrupts disabled, the NT kernel may temporarily
 *          re-enable them while checking.
 */
RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread);

/**
 * Is RTThreadPreemptIsPending reliable?
 *
 * @returns true if reliable, false if not.
 */
RTDECL(bool) RTThreadPreemptIsPendingTrusty(void);

/**
 * Is preemption possible on this system.
 *
 * @returns true if possible, false if not.
 */
RTDECL(bool) RTThreadPreemptIsPossible(void);

/**
 * Preemption state saved by RTThreadPreemptDisable and used by
 * RTThreadPreemptRestore to restore the previous state.
 */
typedef struct RTTHREADPREEMPTSTATE
{
    /** In debug builds this will be used to check for cpu migration. */
    RTCPUID         idCpu;
#  ifdef RT_OS_WINDOWS
    /** The old IRQL. Don't touch! */
    unsigned char   uchOldIrql;
    /** Reserved, MBZ. */
    uint8_t         bReserved1;
    /** Reserved, MBZ. */
    uint8_t         bReserved2;
    /** Reserved, MBZ. */
    uint8_t         bReserved3;
#   define RTTHREADPREEMPTSTATE_INITIALIZER { NIL_RTCPUID, 255, 0, 0, 0 }
#  elif defined(RT_OS_HAIKU)
    /** The cpu_state. Don't touch! */
    uint32_t        uOldCpuState;
#   define RTTHREADPREEMPTSTATE_INITIALIZER { NIL_RTCPUID, 0 }
#  elif defined(RT_OS_SOLARIS)
    /** The Old PIL. Don't touch! */
    uint32_t        uOldPil;
#   define RTTHREADPREEMPTSTATE_INITIALIZER { NIL_RTCPUID, UINT32_MAX }
#  else
    /** Reserved, MBZ. */
    uint32_t        u32Reserved;
#   define RTTHREADPREEMPTSTATE_INITIALIZER { NIL_RTCPUID, 0 }
#  endif
} RTTHREADPREEMPTSTATE;
/** Pointer to a preemption state. */
typedef RTTHREADPREEMPTSTATE *PRTTHREADPREEMPTSTATE;

/**
 * Disable preemption.
 *
 * A call to this function must be matched by exactly one call to
 * RTThreadPreemptRestore().
 *
 * @param   pState              Where to store the preemption state.
 */
RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState);

/**
 * Restores the preemption state, undoing a previous call to
 * RTThreadPreemptDisable.
 *
 * A call to this function must be matching a previous call to
 * RTThreadPreemptDisable.
 *
 * @param  pState               The state return by RTThreadPreemptDisable.
 */
RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState);

/**
 * Check if the thread is executing in interrupt context.
 *
 * @returns true if in interrupt context, false if not.
 * @param       hThread         Must be NIL_RTTHREAD for now.
 */
RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread);


/**
 * Thread context swithcing events.
 */
typedef enum RTTHREADCTXEVENT
{
    /** This thread is being scheduled out on the current CPU (includes preemption,
     * waiting, sleep and whatever else may trigger scheduling). */
    RTTHREADCTXEVENT_OUT = 0,
    /** This thread is being scheduled in on the current CPU and will resume
     * execution. */
    RTTHREADCTXEVENT_IN,
    /** The usual 32-bit size hack. */
    RTTHREADCTXEVENT_32BIT_HACK = 0x7fffffff
} RTTHREADCTXEVENT;

/**
 * Thread context switching hook callback.
 *
 * This hook function is called when a thread is scheduled and preempted.  Check
 * @a enmEvent to see which it is.  Since the function is being called from
 * hooks inside the scheduler, it is limited what you can do from this function.
 * Do NOT acquire locks, sleep or yield the thread for instance.  IRQ safe
 * spinlocks are fine though.
 *
 * @returns IPRT status code.
 * @param   enmEvent    The thread-context event.  Please quitely ignore unknown
 *                      events, we may add more (thread exit, ++) later.
 * @param   pvUser      User argument.
 */
typedef DECLCALLBACK(void) FNRTTHREADCTXHOOK(RTTHREADCTXEVENT enmEvent, void *pvUser);
/** Pointer to a context switching hook. */
typedef FNRTTHREADCTXHOOK *PFNRTTHREADCTXHOOK;

/**
 * Initializes a thread context switching hook for the current thread.
 *
 * The hook is created as disabled, use RTThreadCtxHookEnable to enable it.
 *
 * @returns IPRT status code.
 * @param   phCtxHook       Where to store the hook handle.
 * @param   fFlags          Reserved for future extensions, must be zero.
 * @param   pfnCallback     Pointer to a the hook function (callback) that
 *                          should be called for all context switching events
 *                          involving the current thread.
 * @param   pvUser          User argument that will be passed to @a pfnCallback.
 * @remarks Preemption must be enabled.
 */
RTDECL(int) RTThreadCtxHookCreate(PRTTHREADCTXHOOK phCtxHook, uint32_t fFlags, PFNRTTHREADCTXHOOK pfnCallback, void *pvUser);

/**
 * Destroys a thread context switching hook.
 *
 * Caller must make sure the hook is disabled before the final reference is
 * released.  Recommended to call this on the owning thread, otherwise the
 * memory backing it may on some systems only be released when the thread
 * terminates.
 *
 * @returns IPRT status code.
 *
 * @param   hCtxHook        The context hook handle.  NIL_RTTHREADCTXHOOK is
 *                          ignored and the function will return VINF_SUCCESS.
 * @remarks Preemption must be enabled.
 * @remarks Do not call from FNRTTHREADCTXHOOK.
 */
RTDECL(int) RTThreadCtxHookDestroy(RTTHREADCTXHOOK hCtxHook);

/**
 * Enables the context switching hooks for the current thread.
 *
 * @returns IPRT status code.
 * @param   hCtxHook        The context hook handle.
 * @remarks Should be called with preemption disabled.
 */
RTDECL(int) RTThreadCtxHookEnable(RTTHREADCTXHOOK hCtxHook);

/**
 * Disables the thread context switching hook for the current thread.
 *
 * Will not assert or fail if called twice or with a NIL handle.
 *
 * @returns IPRT status code.
 * @param   hCtxHook        The context hook handle. NIL_RTTHREADCTXHOOK is
 *                          ignored and the function wil return VINF_SUCCESS.
 * @remarks Should be called with preemption disabled.
 * @remarks Do not call from FNRTTHREADCTXHOOK.
 */
RTDECL(int) RTThreadCtxHookDisable(RTTHREADCTXHOOK hCtxHook);

/**
 * Is the thread context switching hook enabled?
 *
 * @returns true if registered, false if not supported or not registered.
 * @param   hCtxHook        The context hook handle.   NIL_RTTHREADCTXHOOK is
 *                          ignored and the function will return false.
 *
 * @remarks Can be called from any thread, though is naturally subject to races
 *          when not called from the thread associated with the hook.
 */
RTDECL(bool) RTThreadCtxHookIsEnabled(RTTHREADCTXHOOK hCtxHook);

# endif /* IN_RING0 */


# ifdef IN_RING3

/**
 * Adopts a non-IPRT thread.
 *
 * @returns IPRT status code.
 * @param   enmType         The thread type.
 * @param   fFlags          The thread flags. RTTHREADFLAGS_WAITABLE is not currently allowed.
 * @param   pszName         The thread name. Optional
 * @param   pThread         Where to store the thread handle. Optional.
 */
RTDECL(int) RTThreadAdopt(RTTHREADTYPE enmType, unsigned fFlags, const char *pszName, PRTTHREAD pThread);

/**
 * Get the thread handle of the current thread, automatically adopting alien
 * threads.
 *
 * @returns Thread handle.
 */
RTDECL(RTTHREAD) RTThreadSelfAutoAdopt(void);

/**
 * Gets the affinity mask of the current thread.
 *
 * @returns IPRT status code.
 * @param   pCpuSet         Where to return the CPU affienty set of the calling
 *                          thread.
 */
RTR3DECL(int) RTThreadGetAffinity(PRTCPUSET pCpuSet);

/**
 * Sets the affinity mask of the current thread.
 *
 * @returns iprt status code.
 * @param   pCpuSet         The set of CPUs this thread can run on.  NULL means
 *                          all CPUs.
 */
RTR3DECL(int) RTThreadSetAffinity(PCRTCPUSET pCpuSet);

/**
 * Binds the thread to one specific CPU.
 *
 * @returns iprt status code.
 * @param   idCpu           The ID of the CPU to bind this thread to.  Use
 *                          NIL_RTCPUID to unbind it.
 */
RTR3DECL(int) RTThreadSetAffinityToCpu(RTCPUID idCpu);

/**
 * Unblocks a thread.
 *
 * This function is paired with RTThreadBlocking and RTThreadBlockingDebug.
 *
 * @param   hThread     The current thread.
 * @param   enmCurState The current state, used to check for nested blocking.
 *                      The new state will be running.
 */
RTDECL(void) RTThreadUnblocked(RTTHREAD hThread, RTTHREADSTATE enmCurState);

/**
 * Change the thread state to blocking.
 *
 * @param   hThread         The current thread.
 * @param   enmState        The sleep state.
 * @param   fReallySleeping Really going to sleep now.  Use false before calls
 *                          to other IPRT synchronization methods.
 */
RTDECL(void) RTThreadBlocking(RTTHREAD hThread, RTTHREADSTATE enmState, bool fReallySleeping);

/**
 * Get the current thread state.
 *
 * A thread that is reported as sleeping may actually still be running inside
 * the lock validator or/and in the code of some other IPRT synchronization
 * primitive.  Use RTThreadGetReallySleeping
 *
 * @returns The thread state.
 * @param   hThread         The thread.
 */
RTDECL(RTTHREADSTATE) RTThreadGetState(RTTHREAD hThread);

/**
 * Checks if the thread is really sleeping or not.
 *
 * @returns RTTHREADSTATE_RUNNING if not really sleeping, otherwise the state it
 *          is sleeping in.
 * @param   hThread         The thread.
 */
RTDECL(RTTHREADSTATE) RTThreadGetReallySleeping(RTTHREAD hThread);

/**
 * Translate a thread state into a string.
 *
 * @returns Pointer to a read-only string containing the state name.
 * @param   enmState            The state.
 */
RTDECL(const char *) RTThreadStateName(RTTHREADSTATE enmState);


/**
 * Native thread states returned by RTThreadNativeState.
 */
typedef enum RTTHREADNATIVESTATE
{
    /** Invalid thread handle. */
    RTTHREADNATIVESTATE_INVALID = 0,
    /** Unable to determine the thread state. */
    RTTHREADNATIVESTATE_UNKNOWN,
    /** The thread is running. */
    RTTHREADNATIVESTATE_RUNNING,
    /** The thread is blocked. */
    RTTHREADNATIVESTATE_BLOCKED,
    /** The thread is suspended / stopped. */
    RTTHREADNATIVESTATE_SUSPENDED,
    /** The thread has terminated. */
    RTTHREADNATIVESTATE_TERMINATED,
    /** Make sure it's a 32-bit type. */
    RTTHREADNATIVESTATE_32BIT_HACK = 0x7fffffff
} RTTHREADNATIVESTATE;


/**
 * Get the native state of a thread.
 *
 * @returns Native state.
 * @param   hThread             The thread handle.
 *
 * @remarks Not yet implemented on all systems, so have a backup plan for
 *          RTTHREADNATIVESTATE_UNKNOWN.
 */
RTDECL(RTTHREADNATIVESTATE) RTThreadGetNativeState(RTTHREAD hThread);


/**
 * Get the execution times of the specified thread
 *
 * @returns IPRT status code.
 * @param   pKernelTime         Kernel execution time in ms (out)
 * @param   pUserTime           User execution time in ms (out)
 *
 */
RTR3DECL(int) RTThreadGetExecutionTimeMilli(uint64_t *pKernelTime, uint64_t *pUserTime);

/** @name Thread Local Storage
 * @{
 */
/**
 * Thread termination callback for destroying a non-zero TLS entry.
 *
 * @remarks It is not permitable to use any RTTls APIs at this time. Doing so
 *          may lead to endless loops, crashes, and other bad stuff.
 *
 * @param   pvValue     The current value.
 */
typedef DECLCALLBACK(void) FNRTTLSDTOR(void *pvValue);
/** Pointer to a FNRTTLSDTOR. */
typedef FNRTTLSDTOR *PFNRTTLSDTOR;

/**
 * Allocates a TLS entry (index).
 *
 * Example code:
 * @code
    RTTLS g_iTls = NIL_RTTLS;

    ...

    // once for the process, allocate the TLS index
    if (g_iTls == NIL_RTTLS)
         g_iTls = RTTlsAlloc();

    // set the thread-local value.
    RTTlsSet(g_iTls, pMyData);

    ...

    // get the thread-local value
    PMYDATA pMyData = (PMYDATA)RTTlsGet(g_iTls);

   @endcode
 *
 * @returns the index of the allocated TLS entry.
 * @returns NIL_RTTLS on failure.
 */
RTR3DECL(RTTLS) RTTlsAlloc(void);

/**
 * Variant of RTTlsAlloc that returns a status code.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if pfnDestructor is non-NULL and the platform
 *          doesn't support this feature.
 *
 * @param   piTls           Where to store the index of the allocated TLS entry.
 *                          This is set to NIL_RTTLS on failure.
 * @param   pfnDestructor   Optional callback function for cleaning up on
 *                          thread termination. WARNING! This feature may not
 *                          be implemented everywhere.
 */
RTR3DECL(int) RTTlsAllocEx(PRTTLS piTls, PFNRTTLSDTOR pfnDestructor);

/**
 * Frees a TLS entry.
 *
 * @returns IPRT status code.
 * @param   iTls        The index of the TLS entry.
 */
RTR3DECL(int) RTTlsFree(RTTLS iTls);

/**
 * Get the (thread-local) value stored in a TLS entry.
 *
 * @returns value in given TLS entry.
 * @retval  NULL if RTTlsSet() has not yet been called on this thread, or if the
 *          TLS index is invalid.
 *
 * @param   iTls        The index of the TLS entry.
 */
RTR3DECL(void *) RTTlsGet(RTTLS iTls);

/**
 * Get the value stored in a TLS entry.
 *
 * @returns IPRT status code.
 * @param   iTls        The index of the TLS entry.
 * @param   ppvValue    Where to store the value.  The value will be NULL if
 *                      RTTlsSet has not yet been called on this thread.
 */
RTR3DECL(int) RTTlsGetEx(RTTLS iTls, void **ppvValue);

/**
 * Set the value stored in an allocated TLS entry.
 *
 * @returns IPRT status.
 * @param   iTls        The index of the TLS entry.
 * @param   pvValue     The value to store.
 *
 * @remarks Note that NULL is considered a special value.
 */
RTR3DECL(int) RTTlsSet(RTTLS iTls, void *pvValue);

/** @} */

# endif /* IN_RING3 */
# endif /* !IN_RC */

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_thread_h */

