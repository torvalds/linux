/** @file
 * IPRT - Semaphore.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
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

#ifndef ___iprt_semaphore_h
#define ___iprt_semaphore_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#if defined(RT_LOCK_STRICT_ORDER) && defined(IN_RING3)
# include <iprt/lockvalidator.h>
#endif


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_sems    RTSem - Semaphores
 *
 * This module implements all kinds of event and mutex semaphores; in addition
 * to these, IPRT implements "critical sections", which are fast recursive
 * mutexes (see @ref grp_rt_critsect ).  C++ users may find @ref grp_rt_cpp_lock
 * interesting.
 *
 * @ingroup grp_rt
 * @{
 */


/** @name Generic Semaphore Wait Flags.
 *
 * @remarks Exactly one of RTSEMWAIT_FLAGS_RELATIVE and
 *          RTSEMWAIT_FLAGS_ABSOLUTE must be set, unless
 *          RTSEMWAIT_FLAGS_INDEFINITE is used.
 *
 *          Exactly one of RTSEMWAIT_FLAGS_NANOSECS and
 *          RTSEMWAIT_FLAGS_MILLISECS must be set, unless
 *          RTSEMWAIT_FLAGS_INDEFINITE is used.
 *
 *          Exactly one of RTSEMWAIT_FLAGS_RESUME and RTSEMWAIT_FLAGS_NORESUME
 *          must be set.
 *
 *          The interruptible vs resume stuff is ring-0 vs ring-3 semantics.
 *
 * @{ */
/** The timeout is relative. */
#define RTSEMWAIT_FLAGS_RELATIVE            RT_BIT_32(0)
/** The timeout is absolute. */
#define RTSEMWAIT_FLAGS_ABSOLUTE            RT_BIT_32(1)
/** The timeout is specified in nanoseconds. */
#define RTSEMWAIT_FLAGS_NANOSECS            RT_BIT_32(2)
/** The timeout is specified in milliseconds. */
#define RTSEMWAIT_FLAGS_MILLISECS           RT_BIT_32(3)
/** Indefinite wait.
 * The relative/absolute and nano-/millisecond flags are ignored. */
#define RTSEMWAIT_FLAGS_INDEFINITE          RT_BIT_32(4)
/** Mask covering the time related bits. */
#define RTSEMWAIT_FLAGS_TIME_MASK           UINT32_C(0x0000001f)

/** Interruptible wait. */
#define RTSEMWAIT_FLAGS_INTERRUPTIBLE       RT_BIT_32(5)
/** No automatic resume, same as interruptible. */
#define RTSEMWAIT_FLAGS_NORESUME            RTSEMWAIT_FLAGS_INTERRUPTIBLE
/** Uninterruptible wait. */
#define RTSEMWAIT_FLAGS_UNINTERRUPTIBLE     RT_BIT_32(6)
/** Resume on interrupt, same as uninterruptible. */
#define RTSEMWAIT_FLAGS_RESUME              RTSEMWAIT_FLAGS_UNINTERRUPTIBLE

/** Macro for validate the flags. */
#define RTSEMWAIT_FLAGS_ARE_VALID(fFlags) \
    (   !((fFlags) & UINT32_C(0xffffff80)) \
     &&  (  ((fFlags) & RTSEMWAIT_FLAGS_INDEFINITE) \
          ? ( (((fFlags) & UINT32_C(0x20))) ^ (((fFlags) >> 1) & UINT32_C(0x20)) ) == UINT32_C(0x20) \
          : ( (((fFlags) & UINT32_C(0x25))) ^ (((fFlags) >> 1) & UINT32_C(0x25)) ) == UINT32_C(0x25) ))
/** @}  */



/** @defgroup grp_rt_sems_event    RTSemEvent - Single Release Event Semaphores
 *
 * Event semaphores can be used for inter-thread communication when one thread
 * wants to notify another thread that something happened.  A thread can block
 * ("wait") on an event semaphore until it is signalled by another thread; see
 * RTSemEventCreate, RTSemEventSignal and RTSemEventWait.
 *
 * @{ */

/**
 * Create an event semaphore.
 *
 * @returns iprt status code.
 * @param   phEventSem          Where to store the handle to the newly created
 *                              event semaphore.
 */
RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem);

/**
 * Create an event semaphore.
 *
 * @returns iprt status code.
 * @param   phEventSem          Where to store the handle to the newly created
 *                              event semaphore.
 * @param   fFlags              Flags, any combination of the
 *                              RTSEMEVENT_FLAGS_XXX \#defines.
 * @param   hClass              The class (no reference consumed).  Since we
 *                              don't do order checks on event semaphores, the
 *                              use of the class is limited to controlling the
 *                              timeout threshold for deadlock detection.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL).  Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(4, 5);

/** @name RTSemMutexCreateEx flags
 * @{ */
/** Disables lock validation. */
#define RTSEMEVENT_FLAGS_NO_LOCK_VAL    UINT32_C(0x00000001)
/** Bootstrap hack for use with certain memory allocator locks only! */
#define RTSEMEVENT_FLAGS_BOOTSTRAP_HACK UINT32_C(0x00000004)
/** @} */

/**
 * Destroy an event semaphore.
 *
 * @returns iprt status code.
 * @param   hEventSem           Handle of the event semaphore.  NIL_RTSEMEVENT
 *                              is quietly ignored (VINF_SUCCESS).
 */
RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem);

/**
 * Signal an event semaphore.
 *
 * The event semaphore will be signaled and automatically reset after exactly
 * one thread have successfully returned from RTSemEventWait() after
 * waiting/polling on that semaphore.
 *
 * @returns iprt status code.
 * @param   hEventSem           The event semaphore to signal.
 *
 * @remarks ring-0: This works when preemption is disabled.  However it is
 *          system specific whether it works in interrupt context or with
 *          interrupts disabled.
 */
RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem);

/**
 * Wait for the event semaphore to be signaled, resume on interruption.
 *
 * This function will resume if the wait is interrupted by an async system event
 * (like a unix signal) or similar.
 *
 * @returns iprt status code.
 *          Will not return VERR_INTERRUPTED.
 * @param   hEventSem           The event semaphore to wait on.
 * @param   cMillies            Number of milliseconds to wait.
 */
RTDECL(int)  RTSemEventWait(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies);

/**
 * Wait for the event semaphore to be signaled, return on interruption.
 *
 * This function will not resume the wait if interrupted.
 *
 * @returns iprt status code.
 * @param   hEventSem           The event semaphore to wait on.
 * @param   cMillies            Number of milliseconds to wait.
 */
RTDECL(int)  RTSemEventWaitNoResume(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies);

/**
 * Extended API for waiting on an event semaphore to be signaled.
 *
 * @returns IPRT status code.
 * @param   hEventSem           The event semaphore to wait on.
 * @param   fFlags              Combination of RTSEMWAIT_FLAGS_XXX.
 * @param   uTimeout            The timeout, ignored if
 *                              RTSEMWAIT_FLAGS_INDEFINITE is set in @a flags.
 *                              Whether this is absolute or relative,
 *                              milliseconds or nanoseconds depends on the @a
 *                              fFlags value.  Do not pass RT_INDEFINITE_WAIT
 *                              here, use RTSEMWAIT_FLAGS_INDEFINITE instead.
 */
RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout);

/**
 * Debug version of RTSemEventWaitEx that tracks the location.
 *
 * @returns IPRT status code, see RTSemEventWaitEx.
 * @param   hEventSem           The event semaphore to wait on.
 * @param   fFlags              See RTSemEventWaitEx.
 * @param   uTimeout            See RTSemEventWaitEx.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Gets the best timeout resolution that RTSemEventWaitEx can do.
 *
 * @returns The resolution in nanoseconds.
 */
RTDECL(uint32_t) RTSemEventGetResolution(void);

/**
 * Sets the signaller thread to one specific thread.
 *
 * This is only used for validating usage and deadlock detection.  When used
 * after calls to RTSemEventAddSignaller, the specified thread will be the only
 * signalling thread.
 *
 * @param   hEventSem           The event semaphore.
 * @param   hThread             The thread that will signal it.  Pass
 *                              NIL_RTTHREAD to indicate that there is no
 *                              special signalling thread.
 */
RTDECL(void) RTSemEventSetSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread);

/**
 * To add more signalling threads.
 *
 * First call RTSemEventSetSignaller then add further threads with this.
 *
 * @param   hEventSem           The event semaphore.
 * @param   hThread             The thread that will signal it. NIL_RTTHREAD is
 *                              not accepted.
 */
RTDECL(void) RTSemEventAddSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread);

/**
 * To remove a signalling thread.
 *
 * Reverts work done by RTSemEventAddSignaller and RTSemEventSetSignaller.
 *
 * @param   hEventSem           The event semaphore.
 * @param   hThread             A previously added thread.
 */
RTDECL(void) RTSemEventRemoveSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread);

/** @} */


/** @defgroup grp_rt_sems_event_multi   RTSemEventMulti - Multiple Release Event Semaphores
 *
 * A variant of @ref  grp_rt_sems_event where all threads will be unblocked when
 * signalling the semaphore.
 *
 * @{ */

/**
 * Creates a multiple release event semaphore.
 *
 * @returns iprt status code.
 * @param   phEventMultiSem     Where to store the handle to the newly created
 *                              multiple release event semaphore.
 */
RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem);

/**
 * Creates a multiple release event semaphore.
 *
 * @returns iprt status code.
 * @param   phEventMultiSem     Where to store the handle to the newly created
 *                              multiple release event semaphore.
 * @param   fFlags              Flags, any combination of the
 *                              RTSEMEVENTMULTI_FLAGS_XXX \#defines.
 * @param   hClass              The class (no reference consumed).  Since we
 *                              don't do order checks on event semaphores, the
 *                              use of the class is limited to controlling the
 *                              timeout threshold for deadlock detection.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL).  Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(4, 5);

/** @name RTSemMutexCreateEx flags
 * @{ */
/** Disables lock validation. */
#define RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL   UINT32_C(0x00000001)
/** @} */

/**
 * Destroy an event multi semaphore.
 *
 * @returns iprt status code.
 * @param   hEventMultiSem      The multiple release event semaphore.  NIL is
 *                              quietly ignored (VINF_SUCCESS).
 */
RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem);

/**
 * Signal an event multi semaphore.
 *
 * @returns iprt status code.
 * @param   hEventMultiSem      The multiple release event semaphore.
 *
 * @remarks ring-0: This works when preemption is disabled.  However it is
 *          system specific whether it works in interrupt context or with
 *          interrupts disabled.
 */
RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem);

/**
 * Resets an event multi semaphore to non-signaled state.
 *
 * @returns iprt status code.
 * @param   hEventMultiSem      The multiple release event semaphore.
 */
RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem);

/**
 * Wait for the event multi semaphore to be signaled, resume on interruption.
 *
 * This function will resume if the wait is interrupted by an async
 * system event (like a unix signal) or similar.
 *
 * @returns iprt status code.
 *          Will not return VERR_INTERRUPTED.
 * @param   hEventMultiSem      The multiple release event semaphore.
 * @param   cMillies            Number of milliseconds to wait.
 */
RTDECL(int)  RTSemEventMultiWait(RTSEMEVENTMULTI hEventMultiSem, RTMSINTERVAL cMillies);

/**
 * Wait for the event multi semaphore to be signaled, return on interruption.
 *
 * This function will not resume the wait if interrupted.
 *
 * @returns iprt status code.
 * @param   hEventMultiSem      The multiple release event semaphore.
 * @param   cMillies            Number of milliseconds to wait.
 * @todo    Rename to RTSemEventMultiWaitIntr since it is mainly for
 *          ring-0 consumption.
 */
RTDECL(int)  RTSemEventMultiWaitNoResume(RTSEMEVENTMULTI hEventMultiSem, RTMSINTERVAL cMillies);

/**
 * Extended API for waiting on an event semaphore to be signaled.
 *
 * @returns IPRT status code.
 * @param   hEventMultiSem      The multiple release event semaphore to wait
 *                              on.
 * @param   fFlags              Combination of the RTSEMWAIT_FLAGS_XXX.
 * @param   uTimeout            The timeout, ignored if
 *                              RTSEMWAIT_FLAGS_INDEFINITE is set in @a flags.
 *                              Whether this is absolute or relative,
 *                              milliseconds or nanoseconds depends on the @a
 *                              fFlags value.  Do not pass RT_INDEFINITE_WAIT
 *                              here, use RTSEMWAIT_FLAGS_INDEFINITE instead.
 */
RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout);

/**
 * Debug version of RTSemEventMultiWaitEx that tracks the location.

 * @returns IPRT status code, see RTSemEventMultiWaitEx.
 * @param   hEventMultiSem      The multiple release event semaphore handle.
 * @param   fFlags              See RTSemEventMultiWaitEx.
 * @param   uTimeout            See RTSemEventMultiWaitEx.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Gets the best timeout resolution that RTSemEventMultiWaitEx can do.
 *
 * @returns The resolution in nanoseconds.
 */
RTDECL(uint32_t) RTSemEventMultiGetResolution(void);

/**
 * Sets the signaller thread to one specific thread.
 *
 * This is only used for validating usage and deadlock detection.  When used
 * after calls to RTSemEventAddSignaller, the specified thread will be the only
 * signalling thread.
 *
 * @param   hEventMultiSem      The multiple release event semaphore.
 * @param   hThread             The thread that will signal it.  Pass
 *                              NIL_RTTHREAD to indicate that there is no
 *                              special signalling thread.
 */
RTDECL(void) RTSemEventMultiSetSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread);

/**
 * To add more signalling threads.
 *
 * First call RTSemEventSetSignaller then add further threads with this.
 *
 * @param   hEventMultiSem      The multiple release event semaphore.
 * @param   hThread             The thread that will signal it. NIL_RTTHREAD is
 *                              not accepted.
 */
RTDECL(void) RTSemEventMultiAddSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread);

/**
 * To remove a signalling thread.
 *
 * Reverts work done by RTSemEventAddSignaller and RTSemEventSetSignaller.
 *
 * @param   hEventMultiSem      The multiple release event semaphore.
 * @param   hThread             A previously added thread.
 */
RTDECL(void) RTSemEventMultiRemoveSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread);

/** @} */


/** @defgroup grp_rt_sems_mutex     RTSemMutex - Mutex semaphores.
 *
 * Mutex semaphores protect a section of code or data to which access must be
 * exclusive.  Only one thread can hold access to a critical section at one
 * time.  See RTSemMutexCreate, RTSemMutexRequest and RTSemMutexRelease.
 *
 * @remarks These are less efficient than "fast mutexes" and "critical
 *          sections", which IPRT implements as well; see @ref
 *          grp_rt_sems_fast_mutex and @ref grp_rt_critsect .
 *
 * @{ */

/**
 * Create a mutex semaphore.
 *
 * @returns iprt status code.
 * @param   phMutexSem      Where to store the mutex semaphore handle.
 */
RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem);

/**
 * Creates a read/write semaphore.
 *
 * @returns iprt status code.
 * @param   phMutexSem          Where to store the handle to the newly created
 *                              mutex semaphore.
 * @param   fFlags              Flags, any combination of the
 *                              RTSEMMUTEX_FLAGS_XXX \#defines.
 * @param   hClass              The class (no reference consumed).  If NIL, no
 *                              lock order validation will be performed on this
 *                              lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order within a class.  RTLOCKVAL_SUB_CLASS_NONE
 *                              is the recommended value here.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL).  Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                               const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(5, 6);

/** @name RTSemMutexCreateEx flags
 * @{ */
/** Disables lock validation. */
#define RTSEMMUTEX_FLAGS_NO_LOCK_VAL    UINT32_C(0x00000001)
/** @} */


/**
 * Destroy a mutex semaphore.
 *
 * @returns iprt status code.
 * @param   hMutexSem           The mutex semaphore to destroy.  NIL is quietly
 *                              ignored (VINF_SUCCESS).
 */
RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem);

/**
 * Changes the lock validator sub-class of the mutex semaphore.
 *
 * It is recommended to try make sure that nobody is using this semaphore while
 * changing the value.
 *
 * @returns The old sub-class.  RTLOCKVAL_SUB_CLASS_INVALID is returns if the
 *          lock validator isn't compiled in or either of the parameters are
 *          invalid.
 * @param   hMutexSem           The handle to the mutex semaphore.
 * @param   uSubClass           The new sub-class value.
 */
RTDECL(uint32_t) RTSemMutexSetSubClass(RTSEMMUTEX hMutexSem, uint32_t uSubClass);

/**
 * Request ownership of a mutex semaphore, resume on interruption.
 *
 * This function will resume if the wait is interrupted by an async
 * system event (like a unix signal) or similar.
 *
 * The same thread may request a mutex semaphore multiple times,
 * a nested counter is kept to make sure it's released on the right
 * RTSemMutexRelease() call.
 *
 * @returns iprt status code.
 *          Will not return VERR_INTERRUPTED.
 * @param   hMutexSem           The mutex semaphore to request ownership over.
 * @param   cMillies            The number of milliseconds to wait.
 */
RTDECL(int)  RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies);

/**
 * Request ownership of a mutex semaphore, return on interruption.
 *
 * This function will not resume the wait if interrupted.
 *
 * The same thread may request a mutex semaphore multiple times,
 * a nested counter is kept to make sure it's released on the right
 * RTSemMutexRelease() call.
 *
 * @returns iprt status code.
 * @param   hMutexSem           The mutex semaphore to request ownership over.
 * @param   cMillies            The number of milliseconds to wait.
 */
RTDECL(int)  RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies);

/**
 * Debug version of RTSemMutexRequest that tracks the location.
 *
 * @returns iprt status code.
 *          Will not return VERR_INTERRUPTED.
 * @param   hMutexSem           The mutex semaphore to request ownership over.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Debug version of RTSemMutexRequestNoResume that tracks the location.
 *
 * @returns iprt status code.
 * @param   hMutexSem           The mutex semaphore to request ownership over.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Request ownership of a mutex semaphore, extended edition.
 *
 * The same thread may request a mutex semaphore multiple times,
 * a nested counter is kept to make sure it's released on the right
 * RTSemMutexRelease() call.
 *
 * @returns iprt status code.
 * @param   hMutexSem           The mutex semaphore to request ownership over.
 * @param   fFlags              Combination of the RTSEMWAIT_FLAGS_XXX.
 * @param   uTimeout            The timeout, ignored if
 *                              RTSEMWAIT_FLAGS_INDEFINITE is set in @a flags.
 *                              Whether this is absolute or relative,
 *                              milliseconds or nanoseconds depends on the @a
 *                              fFlags value.  Do not pass RT_INDEFINITE_WAIT
 *                              here, use RTSEMWAIT_FLAGS_INDEFINITE instead.
 */
RTDECL(int)  RTSemMutexRequestEx(RTSEMMUTEX hMutexSem, uint32_t fFlags, uint64_t uTimeout);

/**
 * Debug version of RTSemMutexRequestEx that tracks the location.
 *
 * @returns iprt status code.
 * @param   hMutexSem           The mutex semaphore to request ownership over.
 * @param   fFlags              See RTSemMutexRequestEx.
 * @param   uTimeout            See RTSemMutexRequestEx.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemMutexRequestExDebug(RTSEMMUTEX hMutexSem, uint32_t fFlags, uint64_t uTimeout,
                                      RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Release the ownership of a mutex semaphore.
 *
 * @returns iprt status code.
 * @param   hMutexSem           The mutex to release the ownership of.  It goes
 *                              without saying the the calling thread must own
 *                              it.
 */
RTDECL(int)  RTSemMutexRelease(RTSEMMUTEX hMutexSem);

/**
 * Checks if the mutex semaphore is owned or not.
 *
 * @returns true if owned, false if not.
 * @param   hMutexSem           The mutex semaphore.
 */
RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem);

/* Strict build: Remap the two request calls to the debug versions. */
#if   defined(RT_STRICT) && !defined(RTSEMMUTEX_WITHOUT_REMAPPING) && !defined(RT_WITH_MANGLING)
# ifdef ___iprt_asm_h
#  define RTSemMutexRequest(hMutexSem, cMillies)            RTSemMutexRequestDebug((hMutexSem), (cMillies), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
#  define RTSemMutexRequestNoResume(hMutexSem, cMillies)    RTSemMutexRequestNoResumeDebug((hMutexSem), (cMillies), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
#  define RTSemMutexRequestEx(hMutexSem, fFlags, uTimeout)  RTSemMutexRequestExDebug((hMutexSem), (fFlags), (uTimeout), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
# else
#  define RTSemMutexRequest(hMutexSem, cMillies)            RTSemMutexRequestDebug((hMutexSem), (cMillies), 0, RT_SRC_POS)
#  define RTSemMutexRequestNoResume(hMutexSem, cMillies)    RTSemMutexRequestNoResumeDebug((hMutexSem), (cMillies), 0, RT_SRC_POS)
#  define RTSemMutexRequestEx(hMutexSem, fFlags, uTimeout)  RTSemMutexRequestExDebug((hMutexSem), (fFlags), (uTimeout), 0, RT_SRC_POS)
# endif
#endif

/* Strict lock order: Automatically classify locks by init location. */
#if   defined(RT_LOCK_STRICT_ORDER) && defined(IN_RING3) && !defined(RTSEMMUTEX_WITHOUT_REMAPPING) && !defined(RT_WITH_MANGLING)
# define RTSemMutexCreate(phMutexSem) \
    RTSemMutexCreateEx((phMutexSem), 0 /*fFlags*/, \
                       RTLockValidatorClassForSrcPos(RT_SRC_POS, NULL), \
                       RTLOCKVAL_SUB_CLASS_NONE, NULL)
#endif

/** @} */


/** @defgroup grp_rt_sems_fast_mutex    RTSemFastMutex - Fast Mutex Semaphores
 *
 * Fast mutexes work like regular mutexes in that they allow only a single
 * thread access to a critical piece of code or data.  As opposed to mutexes,
 * they require no syscall if the fast mutex is not held (like critical
 * sections).  Unlike critical sections however, they are *not* recursive.
 *
 * @remarks The fast mutexes has sideeffects on IRQL on Windows hosts.  So use
 *          with care and test on windows with driver verifier.
 *
 * @{ */

/**
 * Create a fast mutex semaphore.
 *
 * @returns iprt status code.
 * @param   phFastMtx           Where to store the handle to the newly created
 *                              fast mutex semaphore.
 *
 * @remarks Fast mutex semaphores are not recursive.
 */
RTDECL(int)  RTSemFastMutexCreate(PRTSEMFASTMUTEX phFastMtx);

/**
 * Destroy a fast mutex semaphore.
 *
 * @returns iprt status code.
 * @param   hFastMtx            Handle to the fast mutex semaphore.  NIL is
 *                              quietly ignored (VINF_SUCCESS).
 */
RTDECL(int)  RTSemFastMutexDestroy(RTSEMFASTMUTEX hFastMtx);

/**
 * Request ownership of a fast mutex semaphore.
 *
 * The same thread may request a mutex semaphore multiple times,
 * a nested counter is kept to make sure it's released on the right
 * RTSemMutexRelease() call.
 *
 * @returns iprt status code.
 * @param   hFastMtx            Handle to the fast mutex semaphore.
 */
RTDECL(int)  RTSemFastMutexRequest(RTSEMFASTMUTEX hFastMtx);

/**
 * Release the ownership of a fast mutex semaphore.
 *
 * @returns iprt status code.
 * @param   hFastMtx            Handle to the fast mutex semaphore.  It goes
 *                              without saying the the calling thread must own
 *                              it.
 */
RTDECL(int)  RTSemFastMutexRelease(RTSEMFASTMUTEX hFastMtx);

/** @} */


/** @defgroup grp_rt_sems_spin_mutex RTSemSpinMutex - Spinning Mutex Semaphores
 *
 * A very adaptive variant of mutex semaphore that is tailored for the ring-0
 * logger.
 *
 * @{ */

/**
 * Creates a spinning mutex semaphore.
 *
 * @returns iprt status code.
 * @retval  VERR_INVALID_PARAMETER on invalid flags.
 * @retval  VERR_NO_MEMORY if out of memory for the semaphore structure and
 *          handle.
 *
 * @param   phSpinMtx   Where to return the handle to the create semaphore.
 * @param   fFlags      Flags, see RTSEMSPINMUTEX_FLAGS_XXX.
 */
RTDECL(int) RTSemSpinMutexCreate(PRTSEMSPINMUTEX phSpinMtx, uint32_t fFlags);

/** @name RTSemSpinMutexCreate flags.
 * @{ */
/** Always take the semaphore in a IRQ safe way.
 * (In plain words: always disable interrupts.) */
#define RTSEMSPINMUTEX_FLAGS_IRQ_SAFE       RT_BIT_32(0)
/** Mask of valid flags. */
#define RTSEMSPINMUTEX_FLAGS_VALID_MASK     UINT32_C(0x00000001)
/** @} */

/**
 * Destroys a spinning mutex semaphore.
 *
 * @returns iprt status code.
 * @retval  VERR_INVALID_HANDLE (or crash) if the handle is invalid. (NIL will
 *          not cause this status.)
 *
 * @param   hSpinMtx    The semaphore handle. NIL_RTSEMSPINMUTEX is ignored
 *                      quietly (VINF_SUCCESS).
 */
RTDECL(int) RTSemSpinMutexDestroy(RTSEMSPINMUTEX hSpinMtx);

/**
 * Request the spinning mutex semaphore.
 *
 * This may block if the context we're called in allows this. If not it will
 * spin. If called in an interrupt context, we will only spin if the current
 * owner isn't interrupted. Also, on some systems it is not always possible to
 * wake up blocking threads in all contexts, so, which will either be indicated
 * by returning VERR_SEM_BAD_CONTEXT or by temporarily switching the semaphore
 * into pure spinlock state.
 *
 * Preemption will be disabled upon return. IRQs may also be disabled.
 *
 * @returns iprt status code.
 * @retval  VERR_SEM_BAD_CONTEXT if the context it's called in isn't suitable
 *          for releasing it if someone is sleeping on it.
 * @retval  VERR_SEM_DESTROYED if destroyed.
 * @retval  VERR_SEM_NESTED if held by the caller. Asserted.
 * @retval  VERR_INVALID_HANDLE if the handle is invalid. Asserted
 *
 * @param   hSpinMtx    The semaphore handle.
 */
RTDECL(int) RTSemSpinMutexRequest(RTSEMSPINMUTEX hSpinMtx);

/**
 * Like RTSemSpinMutexRequest but it won't block or spin if the semaphore is
 * held by someone else.
 *
 * @returns iprt status code.
 * @retval  VERR_SEM_BUSY if held by someone else.
 * @retval  VERR_SEM_DESTROYED if destroyed.
 * @retval  VERR_SEM_NESTED if held by the caller. Asserted.
 * @retval  VERR_INVALID_HANDLE if the handle is invalid. Asserted
 *
 * @param   hSpinMtx    The semaphore handle.
 */
RTDECL(int) RTSemSpinMutexTryRequest(RTSEMSPINMUTEX hSpinMtx);

/**
 * Releases the semaphore previously acquired by RTSemSpinMutexRequest or
 * RTSemSpinMutexTryRequest.
 *
 * @returns iprt status code.
 * @retval  VERR_SEM_DESTROYED if destroyed.
 * @retval  VERR_NOT_OWNER if not owner. Asserted.
 * @retval  VERR_INVALID_HANDLE if the handle is invalid. Asserted.
 *
 * @param   hSpinMtx    The semaphore handle.
 */
RTDECL(int) RTSemSpinMutexRelease(RTSEMSPINMUTEX hSpinMtx);

/** @} */


/** @defgroup grp_rt_sem_rw             RTSemRW - Read / Write Semaphores
 *
 * Read/write semaphores are a fancier version of mutexes in that they grant
 * read access to the protected data to several threads at the same time but
 * allow only one writer at a time.  This can make code scale better at the
 * expense of slightly more overhead in mutex management.
 *
 * @{ */

/**
 * Creates a read/write semaphore.
 *
 * @returns iprt status code.
 * @param   phRWSem             Where to store the handle to the newly created
 *                              RW semaphore.
 */
RTDECL(int)   RTSemRWCreate(PRTSEMRW phRWSem);

/**
 * Creates a read/write semaphore.
 *
 * @returns iprt status code.
 * @param   phRWSem             Where to store the handle to the newly created
 *                              RW semaphore.
 * @param   fFlags              Flags, any combination of the RTSEMRW_FLAGS_XXX
 *                              \#defines.
 * @param   hClass              The class (no reference consumed).  If NIL, no
 *                              lock order validation will be performed on this
 *                              lock.
 * @param   uSubClass           The sub-class.  This is used to define lock
 *                              order within a class.  RTLOCKVAL_SUB_CLASS_NONE
 *                              is the recommended value here.
 * @param   pszNameFmt          Name format string for the lock validator,
 *                              optional (NULL).  Max length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(int)   RTSemRWCreateEx(PRTSEMRW phRWSem, uint32_t fFlags, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                              const char *pszNameFmt, ...) RT_IPRT_FORMAT_ATTR_MAYBE_NULL(5, 6);

/** @name RTSemRWCreateEx flags
 * @{ */
/** Disables lock validation. */
#define RTSEMRW_FLAGS_NO_LOCK_VAL   UINT32_C(0x00000001)
/** @} */

/**
 * Destroys a read/write semaphore.
 *
 * @returns iprt status code.
 * @param   hRWSem              Handle to the read/write semaphore.  NIL is
 *                              quietly ignored (VINF_SUCCESS).
 */
RTDECL(int)   RTSemRWDestroy(RTSEMRW hRWSem);

/**
 * Changes the lock validator sub-class of the read/write semaphore.
 *
 * It is recommended to try make sure that nobody is using this semaphore while
 * changing the value.
 *
 * @returns The old sub-class.  RTLOCKVAL_SUB_CLASS_INVALID is returns if the
 *          lock validator isn't compiled in or either of the parameters are
 *          invalid.
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   uSubClass           The new sub-class value.
 */
RTDECL(uint32_t) RTSemRWSetSubClass(RTSEMRW hRWSem, uint32_t uSubClass);

/**
 * Request read access to a read/write semaphore, resume on interruption
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPT if the wait was interrupted.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 */
RTDECL(int)   RTSemRWRequestRead(RTSEMRW hRWSem, RTMSINTERVAL cMillies);

/**
 * Request read access to a read/write semaphore, return on interruption
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPT if the wait was interrupted.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 */
RTDECL(int)   RTSemRWRequestReadNoResume(RTSEMRW hRWSem, RTMSINTERVAL cMillies);

/**
 * Debug version of RTSemRWRequestRead that tracks the location.
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPT if the wait was interrupted.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)   RTSemRWRequestReadDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Debug version of RTSemRWRequestWriteNoResume that tracks the location.
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPT if the wait was interrupted.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)   RTSemRWRequestReadNoResumeDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Request read access to a read/write semaphore, extended edition.
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPT if the wait was interrupted.
 * @retval  VERR_TIMEOUT if the wait timed out.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   fFlags              Combination of the RTSEMWAIT_FLAGS_XXX.
 * @param   uTimeout            The timeout, ignored if
 *                              RTSEMWAIT_FLAGS_INDEFINITE is set in @a flags.
 *                              Whether this is absolute or relative,
 *                              milliseconds or nanoseconds depends on the @a
 *                              fFlags value.  Do not pass RT_INDEFINITE_WAIT
 *                              here, use RTSEMWAIT_FLAGS_INDEFINITE instead.
 */
RTDECL(int)   RTSemRWRequestReadEx(RTSEMRW hRWSem, uint32_t fFlags, uint64_t uTimeout);


/**
 * Debug version of RTSemRWRequestReadEx that tracks the location.
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPT if the wait was interrupted.
 * @retval  VERR_TIMEOUT if the wait timed out.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   fFlags              See RTSemRWRequestReadEx.
 * @param   uTimeout            See RTSemRWRequestReadEx.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)   RTSemRWRequestReadExDebug(RTSEMRW hRWSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Release read access to a read/write semaphore.
 *
 * @returns iprt status code.
 * @param   hRWSem              Handle to the read/write semaphore.  It goes
 *                              without saying that caller must own read
 *                              privileges to the semaphore.
 */
RTDECL(int)   RTSemRWReleaseRead(RTSEMRW hRWSem);

/**
 * Request write access to a read/write semaphore, resume on interruption.
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_DEADLOCK if the caller owned the read lock.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 */
RTDECL(int)   RTSemRWRequestWrite(RTSEMRW hRWSem, RTMSINTERVAL cMillies);

/**
 * Request write access to a read/write semaphore, return on interruption.
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPT if the wait was interrupted.
 * @retval  VERR_DEADLOCK if the caller owned the read lock.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 */
RTDECL(int)   RTSemRWRequestWriteNoResume(RTSEMRW hRWSem, RTMSINTERVAL cMillies);

/**
 * Debug version of RTSemRWRequestWrite that tracks the location.
 *
 * @returns IPRT status code, see RTSemRWRequestWrite.
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemRWRequestWriteDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Debug version of RTSemRWRequestWriteNoResume that tracks the location.
 *
 * @returns IPRT status code, see RTSemRWRequestWriteNoResume.
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   cMillies            The number of milliseconds to wait.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemRWRequestWriteNoResumeDebug(RTSEMRW hRWSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Request write access to a read/write semaphore, extended edition.
 *
 * @returns iprt status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INTERRUPTED if the wait was interrupted.
 * @retval  VERR_TIMEOUT if the wait timed out.
 * @retval  VERR_DEADLOCK if the caller owned the read lock.  Do not depend on
 *          this as it is implementation specific.
 * @retval  VERR_INVALID_HANDLE if hRWSem is invalid.
 *
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   fFlags              Combination of the RTSEMWAIT_FLAGS_XXX.
 * @param   uTimeout            The timeout, ignored if
 *                              RTSEMWAIT_FLAGS_INDEFINITE is set in @a flags.
 *                              Whether this is absolute or relative,
 *                              milliseconds or nanoseconds depends on the @a
 *                              fFlags value.  Do not pass RT_INDEFINITE_WAIT
 *                              here, use RTSEMWAIT_FLAGS_INDEFINITE instead.
 */
RTDECL(int)   RTSemRWRequestWriteEx(RTSEMRW hRWSem, uint32_t fFlags, uint64_t uTimeout);

/**
 * Debug version of RTSemRWRequestWriteEx that tracks the location.
 *
 * @returns IPRT status code, see RTSemRWRequestWriteEx.
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   fFlags              See RTSemRWRequestWriteEx.
 * @param   uTimeout            See RTSemRWRequestWriteEx.
 * @param   uId                 Some kind of locking location ID.  Typically a
 *                              return address up the stack.  Optional (0).
 * @param   SRC_POS             The source position where call is being made
 *                              from.  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(int)  RTSemRWRequestWriteExDebug(RTSEMRW hRWSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL);

/**
 * Release write access to a read/write semaphore.
 *
 * @returns iprt status code.
 * @param   hRWSem              Handle to the read/write semaphore.  Goes
 *                              without saying that caller must have write
 *                              access to the semaphore.
 */
RTDECL(int)   RTSemRWReleaseWrite(RTSEMRW hRWSem);

/**
 * Checks if the caller is the exclusive semaphore owner.
 *
 * @returns true / false accoringly.
 * @param   hRWSem              Handle to the read/write semaphore.
 */
RTDECL(bool)  RTSemRWIsWriteOwner(RTSEMRW hRWSem);

/**
 * Checks if the caller is one of the read owners of the semaphore.
 *
 * @note    !CAUTION!  This API doesn't work reliably if lock validation isn't
 *          enabled. Meaning, the answer is not trustworhty unless
 *          RT_LOCK_STRICT or RTSEMRW_STRICT was defined at build time.  Also,
 *          make sure you do not use RTSEMRW_FLAGS_NO_LOCK_VAL when creating
 *          the semaphore.  And finally, if you used a locking class, don't
 *          disable deadlock detection by setting cMsMinDeadlock to
 *          RT_INDEFINITE_WAIT.
 *
 *          In short, only use this for assertions.
 *
 * @returns true if reader, false if not.
 * @param   hRWSem              Handle to the read/write semaphore.
 * @param   fWannaHear          What you'd like to hear when lock validation is
 *                              not available.  (For avoiding asserting all over
 *                              the place.)
 */
RTDECL(bool)  RTSemRWIsReadOwner(RTSEMRW hRWSem, bool fWannaHear);

/**
 * Gets the write recursion count.
 *
 * @returns The write recursion count (0 if bad semaphore handle).
 * @param   hRWSem              Handle to the read/write semaphore.
 */
RTDECL(uint32_t) RTSemRWGetWriteRecursion(RTSEMRW hRWSem);

/**
 * Gets the read recursion count of the current writer.
 *
 * @returns The read recursion count (0 if bad semaphore handle).
 * @param   hRWSem              Handle to the read/write semaphore.
 */
RTDECL(uint32_t) RTSemRWGetWriterReadRecursion(RTSEMRW hRWSem);

/**
 * Gets the current number of reads.
 *
 * This includes all read recursions, so it might be higher than the number of
 * read owners.  It does not include reads done by the current writer.
 *
 * @returns The read count (0 if bad semaphore handle).
 * @param   hRWSem              Handle to the read/write semaphore.
 */
RTDECL(uint32_t) RTSemRWGetReadCount(RTSEMRW hRWSem);

/* Strict build: Remap the four request calls to the debug versions. */
#if defined(RT_STRICT) && !defined(RTSEMRW_WITHOUT_REMAPPING) && !defined(RT_WITH_MANGLING)
# ifdef ___iprt_asm_h
#  define RTSemRWRequestRead(hRWSem, cMillies)              RTSemRWRequestReadDebug((hRWSem), (cMillies), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
#  define RTSemRWRequestReadNoResume(hRWSem, cMillies)      RTSemRWRequestReadNoResumeDebug((hRWSem), (cMillies), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
#  define RTSemRWRequestWrite(hRWSem, cMillies)             RTSemRWRequestWriteDebug((hRWSem), (cMillies), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
#  define RTSemRWRequestWriteNoResume(hRWSem, cMillies)     RTSemRWRequestWriteNoResumeDebug((hRWSem), (cMillies), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
#  define RTSemRWRequestWriteEx(hRWSem, fFlags, uTimeout)   RTSemRWRequestWriteExDebug((hRWSem), (fFlags), (uTimeout), (uintptr_t)ASMReturnAddress(), RT_SRC_POS)
# else
#  define RTSemRWRequestRead(hRWSem, cMillies)              RTSemRWRequestReadDebug((hRWSem), (cMillies), 0, RT_SRC_POS)
#  define RTSemRWRequestReadNoResume(hRWSem, cMillies)      RTSemRWRequestReadNoResumeDebug((hRWSem), (cMillies), 0, RT_SRC_POS)
#  define RTSemRWRequestWrite(hRWSem, cMillies)             RTSemRWRequestWriteDebug((hRWSem), (cMillies), 0, RT_SRC_POS)
#  define RTSemRWRequestWriteNoResume(hRWSem, cMillies)     RTSemRWRequestWriteNoResumeDebug((hRWSem), (cMillies), 0, RT_SRC_POS)
#  define RTSemRWRequestWriteEx(hRWSem, fFlags, uTimeout)   RTSemRWRequestWriteExDebug((hRWSem), (fFlags), (uTimeout), 0, RT_SRC_POS)
# endif
#endif

/* Strict lock order: Automatically classify locks by init location. */
#if defined(RT_LOCK_STRICT_ORDER) && defined(IN_RING3) && !defined(RTSEMRW_WITHOUT_REMAPPING) && !defined(RT_WITH_MANGLING)
# define RTSemRWCreate(phSemRW) \
    RTSemRWCreateEx((phSemRW), 0 /*fFlags*/, \
                    RTLockValidatorClassForSrcPos(RT_SRC_POS, NULL), \
                    RTLOCKVAL_SUB_CLASS_NONE, NULL)
#endif

/** @} */


/** @defgroup grp_rt_sems_pingpong      RTSemPingPong - Ping-Pong Construct
 *
 * Serialization of a two way communication.
 *
 * @{ */

/**
 * Ping-pong speaker
 */
typedef enum RTPINGPONGSPEAKER
{
    /** Not initialized. */
    RTPINGPONGSPEAKER_UNINITIALIZE = 0,
    /** Ping is speaking, Pong is waiting. */
    RTPINGPONGSPEAKER_PING,
    /** Pong is signaled, Ping is waiting. */
    RTPINGPONGSPEAKER_PONG_SIGNALED,
    /** Pong is speaking, Ping is waiting. */
    RTPINGPONGSPEAKER_PONG,
    /** Ping is signaled, Pong is waiting. */
    RTPINGPONGSPEAKER_PING_SIGNALED,
    /** Hack to ensure that it's at least 32-bits wide. */
    RTPINGPONGSPEAKER_HACK = 0x7fffffff
} RTPINGPONGSPEAKER;

/**
 * Ping-Pong construct.
 *
 * Two threads, one saying Ping and the other saying Pong. The construct
 * makes sure they don't speak out of turn and that they can wait and poll
 * on the conversation.
 */
typedef struct RTPINGPONG
{
    /** The semaphore the Ping thread waits on. */
    RTSEMEVENT                  Ping;
    /** The semaphore the Pong thread waits on. */
    RTSEMEVENT                  Pong;
    /** The current speaker. */
    volatile RTPINGPONGSPEAKER  enmSpeaker;
#if HC_ARCH_BITS == 64
    /** Padding the structure to become a multiple of sizeof(RTHCPTR). */
    uint32_t                    u32Padding;
#endif
} RTPINGPONG;
/** Pointer to Ping-Pong construct. */
typedef RTPINGPONG *PRTPINGPONG;

/**
 * Init a Ping-Pong construct.
 *
 * @returns iprt status code.
 * @param   pPP         Pointer to the ping-pong structure which needs initialization.
 */
RTDECL(int) RTSemPingPongInit(PRTPINGPONG pPP);

/**
 * Deletes a Ping-Pong construct.
 *
 * @returns iprt status code.
 * @param   pPP         Pointer to the ping-pong structure which is to be destroyed.
 *                      (I.e. put into uninitialized state.)
 */
RTDECL(int) RTSemPingPongDelete(PRTPINGPONG pPP);

/**
 * Signals the pong thread in a ping-pong construct. (I.e. sends ping.)
 * This is called by the ping thread.
 *
 * @returns iprt status code.
 * @param   pPP         Pointer to the ping-pong structure to ping.
 */
RTDECL(int) RTSemPing(PRTPINGPONG pPP);

/**
 * Signals the ping thread in a ping-pong construct. (I.e. sends pong.)
 * This is called by the pong thread.
 *
 * @returns iprt status code.
 * @param   pPP         Pointer to the ping-pong structure to pong.
 */
RTDECL(int) RTSemPong(PRTPINGPONG pPP);

/**
 * Wait function for the ping thread.
 *
 * @returns iprt status code.
 *          Will not return VERR_INTERRUPTED.
 * @param   pPP         Pointer to the ping-pong structure to wait on.
 * @param   cMillies    Number of milliseconds to wait.
 */
RTDECL(int) RTSemPingWait(PRTPINGPONG pPP, RTMSINTERVAL cMillies);

/**
 * Wait function for the pong thread.
 *
 * @returns iprt status code.
 *          Will not return VERR_INTERRUPTED.
 * @param   pPP         Pointer to the ping-pong structure to wait on.
 * @param   cMillies    Number of milliseconds to wait.
 */
RTDECL(int) RTSemPongWait(PRTPINGPONG pPP, RTMSINTERVAL cMillies);


/**
 * Checks if the pong thread is speaking.
 *
 * @returns true / false.
 * @param   pPP         Pointer to the ping-pong structure.
 * @remark  This is NOT the same as !RTSemPongIsSpeaker().
 */
DECLINLINE(bool) RTSemPingIsSpeaker(PRTPINGPONG pPP)
{
    RTPINGPONGSPEAKER enmSpeaker = pPP->enmSpeaker;
    return enmSpeaker == RTPINGPONGSPEAKER_PING;
}


/**
 * Checks if the pong thread is speaking.
 *
 * @returns true / false.
 * @param   pPP         Pointer to the ping-pong structure.
 * @remark  This is NOT the same as !RTSemPingIsSpeaker().
 */
DECLINLINE(bool) RTSemPongIsSpeaker(PRTPINGPONG pPP)
{
    RTPINGPONGSPEAKER enmSpeaker = pPP->enmSpeaker;
    return enmSpeaker == RTPINGPONGSPEAKER_PONG;
}


/**
 * Checks whether the ping thread should wait.
 *
 * @returns true / false.
 * @param   pPP         Pointer to the ping-pong structure.
 * @remark  This is NOT the same as !RTSemPongShouldWait().
 */
DECLINLINE(bool) RTSemPingShouldWait(PRTPINGPONG pPP)
{
    RTPINGPONGSPEAKER enmSpeaker = pPP->enmSpeaker;
    return enmSpeaker == RTPINGPONGSPEAKER_PONG
        || enmSpeaker == RTPINGPONGSPEAKER_PONG_SIGNALED
        || enmSpeaker == RTPINGPONGSPEAKER_PING_SIGNALED;
}


/**
 * Checks whether the pong thread should wait.
 *
 * @returns true / false.
 * @param   pPP         Pointer to the ping-pong structure.
 * @remark  This is NOT the same as !RTSemPingShouldWait().
 */
DECLINLINE(bool) RTSemPongShouldWait(PRTPINGPONG pPP)
{
    RTPINGPONGSPEAKER enmSpeaker = pPP->enmSpeaker;
    return enmSpeaker == RTPINGPONGSPEAKER_PING
        || enmSpeaker == RTPINGPONGSPEAKER_PING_SIGNALED
        || enmSpeaker == RTPINGPONGSPEAKER_PONG_SIGNALED;
}

/** @} */


/** @defgroup grp_rt_sems_xroads    RTSemXRoads - Crossroads
 *
 * The crossroads semaphore is intended to prevent two classes of incompatible
 * events from occurring simultaneously, like south/north bound traffic and
 * west/east bound traffic at a 4-way junction.
 *
 * @remarks In order to simplify the implementation, the current flow is always
 *          given priority.  So, it won't work at all well when busy!
 *
 * @remarks "XRoads" is used as a name because it is briefer than "crossroads"
 *          and it slightly stresses that is a 4 way crossing to the users of
 *          American English.
 * @{
 */

/**
 * Creates a crossroads semaphore.
 *
 * @returns IPRT status code.
 *
 * @param   phXRoads            Where to return the handle to the newly created
 *                              crossroads semaphore.
 */
RTDECL(int) RTSemXRoadsCreate(PRTSEMXROADS phXRoads);

/**
 * Destroys a crossroads semaphore.
 *
 * @returns IPRT status code.
 *
 * @param   hXRoads             Handle to the crossroads semaphore that is to be
 *                              destroyed.  NIL_RTSEMXROADS is quitetly ignored
 *                              (VINF_SUCCESS).
 */
RTDECL(int) RTSemXRoadsDestroy(RTSEMXROADS hXRoads);

/**
 * Enter the crossroads from the south or north.
 *
 * (Coupled with RTSemXRoadsNSLeave.)
 *
 * @returns IPRT status code.
 * @param   hXRoads             Handle to the crossroads semaphore.
 */
RTDECL(int) RTSemXRoadsNSEnter(RTSEMXROADS hXRoads);

/**
 * Leave the crossroads to the north or south.
 *
 * (Coupled with RTSemXRoadsNSEnter.)
 *
 * @returns IPRT status code.
 * @param   hXRoads             Handle to the crossroads semaphore.
 */
RTDECL(int) RTSemXRoadsNSLeave(RTSEMXROADS hXRoads);

/**
 * Leave the crossroads from the east or west.
 *
 * (Coupled with RTSemXRoadsEWLeave.)
 *
 * @returns IPRT status code.
 * @param   hXRoads             Handle to the crossroads semaphore.
 */
RTDECL(int) RTSemXRoadsEWEnter(RTSEMXROADS hXRoads);

/**
 * Leave the crossroads to the west or east.
 *
 * (Coupled with RTSemXRoadsEWEnter.)
 *
 * @returns IPRT status code.
 * @param   hXRoads             Handle to the crossroads semaphore.
 */
RTDECL(int) RTSemXRoadsEWLeave(RTSEMXROADS hXRoads);

/** @} */

/** @} */

RT_C_DECLS_END

#endif

