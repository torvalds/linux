/* $Id: thread.h $ */
/** @file
 * IPRT - Internal RTThread header.
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

#ifndef IPRT_INCLUDED_INTERNAL_thread_h
#define IPRT_INCLUDED_INTERNAL_thread_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/thread.h>
#include <iprt/avl.h>
#ifdef IN_RING3
# include <iprt/process.h>
# include <iprt/critsect.h>
#endif
#include "internal/lockvalidator.h"
#include "internal/magics.h"
#ifdef RT_WITH_ICONV_CACHE
# include "internal/string.h"
#endif

RT_C_DECLS_BEGIN


/** Max thread name length. */
#define RTTHREAD_NAME_LEN       16
#ifdef IPRT_WITH_GENERIC_TLS
/** The number of TLS entries for the generic implementation. */
# define RTTHREAD_TLS_ENTRIES   64
#endif

/**
 * Internal representation of a thread.
 */
typedef struct RTTHREADINT
{
    /** Avl node core - the key is the native thread id. */
    AVLPVNODECORE           Core;
    /** Magic value (RTTHREADINT_MAGIC). */
    uint32_t                u32Magic;
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** The current thread state. */
    RTTHREADSTATE volatile  enmState;
    /** Set when really sleeping. */
    bool volatile           fReallySleeping;
#if defined(RT_OS_WINDOWS) && defined(IN_RING3)
    /** The thread handle
     * This is not valid until the create function has returned! */
    uintptr_t               hThread;
#endif
#if defined(RT_OS_LINUX) && defined(IN_RING3)
    /** The thread ID.
     * This is not valid before rtThreadMain has been called by the new thread.  */
    pid_t                   tid;
#endif
#if defined(RT_OS_SOLARIS) && defined(IN_RING0)
    /** Debug thread ID needed for thread_join. */
    uint64_t                tid;
#endif
    /** The user event semaphore. */
    RTSEMEVENTMULTI         EventUser;
    /** The terminated event semaphore. */
    RTSEMEVENTMULTI         EventTerminated;
    /** The thread type. */
    RTTHREADTYPE            enmType;
    /** The thread creation flags. (RTTHREADFLAGS) */
    unsigned                fFlags;
    /** Internal flags. (RTTHREADINT_FLAGS_ *) */
    uint32_t                fIntFlags;
    /** The result code. */
    int                     rc;
    /** Thread function. */
    PFNRTTHREAD             pfnThread;
    /** Thread function argument. */
    void                   *pvUser;
    /** Actual stack size. */
    size_t                  cbStack;
#ifdef IN_RING3
    /** The lock validator data. */
    RTLOCKVALPERTHREAD      LockValidator;
#endif /* IN_RING3 */
#ifdef RT_WITH_ICONV_CACHE
    /** Handle cache for iconv.
     * @remarks ASSUMES sizeof(void *) >= sizeof(iconv_t). */
    void *ahIconvs[RTSTRICONV_END];
#endif
#ifdef IPRT_WITH_GENERIC_TLS
    /** The TLS entries for this thread. */
    void                   *apvTlsEntries[RTTHREAD_TLS_ENTRIES];
#endif
    /** Thread name. */
    char                    szName[RTTHREAD_NAME_LEN];
} RTTHREADINT;
/** Pointer to the internal representation of a thread. */
typedef RTTHREADINT *PRTTHREADINT;


/** @name RTTHREADINT::fIntFlags Masks and Bits.
 * @{ */
/** Set if the thread is an alien thread.
 * Clear if the thread was created by IPRT. */
#define RTTHREADINT_FLAGS_ALIEN      RT_BIT(0)
/** Set if the thread has terminated.
 * Clear if the thread is running. */
#define RTTHREADINT_FLAGS_TERMINATED RT_BIT(1)
/** This bit is set if the thread is in the AVL tree. */
#define RTTHREADINT_FLAG_IN_TREE_BIT 2
/** @copydoc RTTHREADINT_FLAG_IN_TREE_BIT */
#define RTTHREADINT_FLAG_IN_TREE     RT_BIT(RTTHREADINT_FLAG_IN_TREE_BIT)
/** Set if it's the main thread. */
#define RTTHREADINT_FLAGS_MAIN       RT_BIT(3)
/** @} */


/**
 * Initialize the native part of the thread management.
 *
 * Generally a TLS entry will be allocated at this point (Ring-3).
 *
 * @returns iprt status code.
 */
DECLHIDDEN(int) rtThreadNativeInit(void);

#ifdef IN_RING3
/**
 * Called when IPRT was first initialized in unobtrusive mode and later changed
 * to obtrustive.
 *
 * This is only applicable in ring-3.
 */
DECLHIDDEN(void) rtThreadNativeReInitObtrusive(void);
#endif

/**
 * Create a native thread.
 * This creates the thread as described in pThreadInt and stores the thread id in *pThread.
 *
 * @returns iprt status code.
 * @param   pThreadInt      The thread data structure for the thread.
 * @param   pNativeThread   Where to store the native thread identifier.
 */
DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThreadInt, PRTNATIVETHREAD pNativeThread);

/**
 * Adopts a thread, this is called immediately after allocating the
 * thread structure.
 *
 * @param   pThread     Pointer to the thread structure.
 */
DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread);

/**
 * Called from rtThreadDestroy so that the TLS entry and any native data in the
 * thread structure can be cleared.
 *
 * @param   pThread         The thread structure.
 */
DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread);

#ifdef IN_RING3
/**
 * Called to check whether the thread is still alive or not before we start
 * waiting.
 *
 * This is a kludge to deal with windows threads being killed wholesale in
 * certain process termination scenarios and we don't want to hang the last
 * thread because it's waiting on the semaphore of a dead thread.
 *
 * @returns true if alive, false if not.
 * @param   pThread         The thread structure.
 */
DECLHIDDEN(bool) rtThreadNativeIsAliveKludge(PRTTHREADINT pThread);
#endif

#ifdef IN_RING0
/**
 * Called from rtThreadWait when the last thread has completed in order to make
 * sure it's all the way out of IPRT before RTR0Term is called.
 *
 * @param   pThread     The thread structure.
 */
DECLHIDDEN(void) rtThreadNativeWaitKludge(PRTTHREADINT pThread);
#endif


/**
 * Sets the priority of the thread according to the thread type
 * and current process priority.
 *
 * The RTTHREADINT::enmType member has not yet been updated and will be updated by
 * the caller on a successful return.
 *
 * @returns iprt status code.
 * @param   pThread     The thread in question.
 * @param   enmType     The thread type.
 * @remark  Located in sched.
 */
DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType);

#ifdef IN_RING3
# ifdef RT_OS_WINDOWS
/**
 * Callback for when a native thread is detaching.
 *
 * It give the Win32/64 backend a chance to terminate alien
 * threads properly.
 */
DECLHIDDEN(void) rtThreadNativeDetach(void);

/**
 * Internal function for informing the debugger about a thread.
 * @param   pThread     The thread. May differ from the calling thread.
 */
DECLHIDDEN(void) rtThreadNativeInformDebugger(PRTTHREADINT pThread);
# endif
#endif /* IN_RING3 */


/* thread.cpp */
DECLCALLBACK(DECLHIDDEN(int)) rtThreadMain(PRTTHREADINT pThread, RTNATIVETHREAD NativeThread, const char *pszThreadName);
DECLHIDDEN(uint32_t)     rtThreadRelease(PRTTHREADINT pThread);
DECLHIDDEN(void)         rtThreadTerminate(PRTTHREADINT pThread, int rc);
DECLHIDDEN(PRTTHREADINT) rtThreadGetByNative(RTNATIVETHREAD NativeThread);
DECLHIDDEN(PRTTHREADINT) rtThreadGet(RTTHREAD Thread);
DECLHIDDEN(int)          rtThreadInit(void);
#ifdef IN_RING3
DECLHIDDEN(void)         rtThreadReInitObtrusive(void);
#endif
DECLHIDDEN(void)         rtThreadTerm(void);
DECLHIDDEN(void)         rtThreadInsert(PRTTHREADINT pThread, RTNATIVETHREAD NativeThread);
#ifdef IN_RING3
DECLHIDDEN(int)          rtThreadDoSetProcPriority(RTPROCPRIORITY enmPriority);
#endif /* !IN_RING0 */
#ifdef IPRT_WITH_GENERIC_TLS
DECLHIDDEN(void)         rtThreadClearTlsEntry(RTTLS iTls);
DECLHIDDEN(void)         rtThreadTlsDestruction(PRTTHREADINT pThread); /* in tls-generic.cpp */
#endif

#ifdef IPRT_INCLUDED_asm_h

/**
 * Gets the thread state.
 *
 * @returns The thread state.
 * @param   pThread             The thread.
 */
DECLINLINE(RTTHREADSTATE) rtThreadGetState(PRTTHREADINT pThread)
{
    return pThread->enmState;
}

/**
 * Sets the thread state.
 *
 * @param   pThread             The thread.
 * @param   enmNewState         The new thread state.
 */
DECLINLINE(void) rtThreadSetState(PRTTHREADINT pThread, RTTHREADSTATE enmNewState)
{
    AssertCompile(sizeof(pThread->enmState) == sizeof(uint32_t));
    ASMAtomicWriteU32((uint32_t volatile *)&pThread->enmState, enmNewState);
}

#endif

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_thread_h */
