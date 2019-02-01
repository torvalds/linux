/* $Id: thread.cpp $ */
/** @file
 * IPRT - Threads, common routines.
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
#define LOG_GROUP RTLOGGROUP_THREAD
#include <iprt/thread.h>
#include "internal/iprt.h"

#include <iprt/log.h>
#include <iprt/avl.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/lockvalidator.h>
#include <iprt/semaphore.h>
#ifdef IN_RING0
# include <iprt/spinlock.h>
#endif
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include "internal/magics.h"
#include "internal/thread.h"
#include "internal/sched.h"
#include "internal/process.h"
#ifdef RT_WITH_ICONV_CACHE
# include "internal/string.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef IN_RING0
# define RT_THREAD_LOCK_RW()        RTSpinlockAcquire(g_ThreadSpinlock)
# define RT_THREAD_UNLOCK_RW()      RTSpinlockRelease(g_ThreadSpinlock)
# define RT_THREAD_LOCK_RD()        RTSpinlockAcquire(g_ThreadSpinlock)
# define RT_THREAD_UNLOCK_RD()      RTSpinlockRelease(g_ThreadSpinlock)
#else
# define RT_THREAD_LOCK_RW()        rtThreadLockRW()
# define RT_THREAD_UNLOCK_RW()      rtThreadUnLockRW()
# define RT_THREAD_LOCK_RD()        rtThreadLockRD()
# define RT_THREAD_UNLOCK_RD()      rtThreadUnLockRD()
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The AVL thread containing the threads. */
static PAVLPVNODECORE       g_ThreadTree;
/** The number of threads in the tree (for ring-0 termination kludge). */
static uint32_t volatile    g_cThreadInTree;
#ifdef IN_RING3
/** The RW lock protecting the tree. */
static RTSEMRW          g_ThreadRWSem = NIL_RTSEMRW;
#else
/** The spinlocks protecting the tree. */
static RTSPINLOCK       g_ThreadSpinlock = NIL_RTSPINLOCK;
#endif
/** Indicates whether we've been initialized or not. */
static bool             g_frtThreadInitialized;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtThreadDestroy(PRTTHREADINT pThread);
#ifdef IN_RING3
static int rtThreadAdopt(RTTHREADTYPE enmType, unsigned fFlags, uint32_t fIntFlags, const char *pszName);
#endif
static void rtThreadRemoveLocked(PRTTHREADINT pThread);
static PRTTHREADINT rtThreadAlloc(RTTHREADTYPE enmType, unsigned fFlags, uint32_t fIntFlags, const char *pszName);


/** @page pg_rt_thread  IPRT Thread Internals
 *
 * IPRT provides interface to whatever native threading that the host provides,
 * preferably using a CRT level interface to better integrate with other libraries.
 *
 * Internally IPRT keeps track of threads by means of the RTTHREADINT structure.
 * All the RTTHREADINT structures are kept in a AVL tree which is protected by a
 * read/write lock for efficient access. A thread is inserted into the tree in
 * three places in the code. The main thread is 'adopted' by IPRT on rtR3Init()
 * by rtThreadAdopt(). When creating a new thread there the child and the parent
 * race inserting the thread, this is rtThreadMain() and RTThreadCreate.
 *
 * RTTHREADINT objects are using reference counting as a mean of sticking around
 * till no-one needs them any longer. Waitable threads is created with one extra
 * reference so they won't go away until they are waited on. This introduces a
 * major problem if we use the host thread identifier as key in the AVL tree - the
 * host may reuse the thread identifier before the thread was waited on. So, on
 * most platforms we are using the RTTHREADINT pointer as key and not the
 * thread id. RTThreadSelf() then have to be implemented using a pointer stored
 * in thread local storage (TLS).
 *
 * In Ring-0 we only try keep track of kernel threads created by RTThreadCreate
 * at the moment. There we really only need the 'join' feature, but doing things
 * the same way allow us to name threads and similar stuff.
 */


/**
 * Initializes the thread database.
 *
 * @returns iprt status code.
 */
DECLHIDDEN(int) rtThreadInit(void)
{
#ifdef IN_RING3
    int rc = VINF_ALREADY_INITIALIZED;
    if (g_ThreadRWSem == NIL_RTSEMRW)
    {
        /*
         * We assume the caller is the 1st thread, which we'll call 'main'.
         * But first, we'll create the semaphore.
         */
        rc = RTSemRWCreateEx(&g_ThreadRWSem, RTSEMRW_FLAGS_NO_LOCK_VAL, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = rtThreadNativeInit();
            if (RT_SUCCESS(rc))
                rc = rtThreadAdopt(RTTHREADTYPE_DEFAULT, 0, RTTHREADINT_FLAGS_MAIN, "main");
            if (RT_SUCCESS(rc))
                rc = rtSchedNativeCalcDefaultPriority(RTTHREADTYPE_DEFAULT);
            if (RT_SUCCESS(rc))
            {
                g_frtThreadInitialized = true;
                return VINF_SUCCESS;
            }

            /* failed, clear out */
            RTSemRWDestroy(g_ThreadRWSem);
            g_ThreadRWSem = NIL_RTSEMRW;
        }
    }

#elif defined(IN_RING0)
    int rc;
    /*
     * Create the spinlock and to native init.
     */
    Assert(g_ThreadSpinlock == NIL_RTSPINLOCK);
    rc = RTSpinlockCreate(&g_ThreadSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "RTThread");
    if (RT_SUCCESS(rc))
    {
        rc = rtThreadNativeInit();
        if (RT_SUCCESS(rc))
        {
            g_frtThreadInitialized = true;
            return VINF_SUCCESS;
        }

        /* failed, clear out */
        RTSpinlockDestroy(g_ThreadSpinlock);
        g_ThreadSpinlock = NIL_RTSPINLOCK;
    }
#else
# error "!IN_RING0 && !IN_RING3"
#endif
    return rc;
}


#ifdef IN_RING3
/**
 * Called when IPRT was first initialized in unobtrusive mode and later changed
 * to obtrustive.
 *
 * This is only applicable in ring-3.
 */
DECLHIDDEN(void) rtThreadReInitObtrusive(void)
{
    rtThreadNativeReInitObtrusive();
}
#endif


/**
 * Terminates the thread database.
 */
DECLHIDDEN(void) rtThreadTerm(void)
{
#ifdef IN_RING3
    /* we don't cleanup here yet */

#elif defined(IN_RING0)
    /* just destroy the spinlock and assume the thread is fine... */
    RTSpinlockDestroy(g_ThreadSpinlock);
    g_ThreadSpinlock = NIL_RTSPINLOCK;
    if (g_ThreadTree != NULL)
        RTAssertMsg2Weak("WARNING: g_ThreadTree=%p\n", g_ThreadTree);
#endif
}


#ifdef IN_RING3

DECLINLINE(void) rtThreadLockRW(void)
{
    if (g_ThreadRWSem == NIL_RTSEMRW)
        rtThreadInit();
    int rc = RTSemRWRequestWrite(g_ThreadRWSem, RT_INDEFINITE_WAIT);
    AssertReleaseRC(rc);
}


DECLINLINE(void) rtThreadLockRD(void)
{
    if (g_ThreadRWSem == NIL_RTSEMRW)
        rtThreadInit();
    int rc = RTSemRWRequestRead(g_ThreadRWSem, RT_INDEFINITE_WAIT);
    AssertReleaseRC(rc);
}


DECLINLINE(void) rtThreadUnLockRW(void)
{
    int rc = RTSemRWReleaseWrite(g_ThreadRWSem);
    AssertReleaseRC(rc);
}


DECLINLINE(void) rtThreadUnLockRD(void)
{
    int rc = RTSemRWReleaseRead(g_ThreadRWSem);
    AssertReleaseRC(rc);
}


/**
 * Adopts the calling thread.
 * No locks are taken or released by this function.
 */
static int rtThreadAdopt(RTTHREADTYPE enmType, unsigned fFlags, uint32_t fIntFlags, const char *pszName)
{
    int rc;
    PRTTHREADINT pThread;
    Assert(!(fFlags & RTTHREADFLAGS_WAITABLE));
    fFlags &= ~RTTHREADFLAGS_WAITABLE;

    /*
     * Allocate and insert the thread.
     * (It is vital that rtThreadNativeAdopt updates the TLS before
     * we try inserting the thread because of locking.)
     */
    rc = VERR_NO_MEMORY;
    pThread = rtThreadAlloc(enmType, fFlags, RTTHREADINT_FLAGS_ALIEN | fIntFlags, pszName);
    if (pThread)
    {
        RTNATIVETHREAD NativeThread = RTThreadNativeSelf();
        rc = rtThreadNativeAdopt(pThread);
        if (RT_SUCCESS(rc))
        {
            rtThreadInsert(pThread, NativeThread);
            rtThreadSetState(pThread, RTTHREADSTATE_RUNNING);
            rtThreadRelease(pThread);
        }
        else
            rtThreadDestroy(pThread);
    }
    return rc;
}

/**
 * Adopts a non-IPRT thread.
 *
 * @returns IPRT status code.
 * @param   enmType         The thread type.
 * @param   fFlags          The thread flags. RTTHREADFLAGS_WAITABLE is not currently allowed.
 * @param   pszName         The thread name. Optional.
 * @param   pThread         Where to store the thread handle. Optional.
 */
RTDECL(int) RTThreadAdopt(RTTHREADTYPE enmType, unsigned fFlags, const char *pszName, PRTTHREAD pThread)
{
    int      rc;
    RTTHREAD Thread;

    AssertReturn(!(fFlags & RTTHREADFLAGS_WAITABLE), VERR_INVALID_PARAMETER);
    AssertReturn(!pszName || VALID_PTR(pszName), VERR_INVALID_POINTER);
    AssertReturn(!pThread || VALID_PTR(pThread), VERR_INVALID_POINTER);

    rc = VINF_SUCCESS;
    Thread = RTThreadSelf();
    if (Thread == NIL_RTTHREAD)
    {
        /* generate a name if none was given. */
        char szName[RTTHREAD_NAME_LEN];
        if (!pszName || !*pszName)
        {
            static uint32_t s_i32AlienId = 0;
            uint32_t i32Id = ASMAtomicIncU32(&s_i32AlienId);
            RTStrPrintf(szName, sizeof(szName), "ALIEN-%RX32", i32Id);
            pszName = szName;
        }

        /* try adopt it */
        rc = rtThreadAdopt(enmType, fFlags, 0, pszName);
        Thread = RTThreadSelf();
        Log(("RTThreadAdopt: %RTthrd %RTnthrd '%s' enmType=%d fFlags=%#x rc=%Rrc\n",
             Thread, RTThreadNativeSelf(), pszName, enmType, fFlags, rc));
    }
    else
        Log(("RTThreadAdopt: %RTthrd %RTnthrd '%s' enmType=%d fFlags=%#x - already adopted!\n",
             Thread, RTThreadNativeSelf(), pszName, enmType, fFlags));

    if (pThread)
        *pThread = Thread;
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadAdopt);


/**
 * Get the thread handle of the current thread, automatically adopting alien
 * threads.
 *
 * @returns Thread handle.
 */
RTDECL(RTTHREAD) RTThreadSelfAutoAdopt(void)
{
    RTTHREAD hSelf = RTThreadSelf();
    if (RT_UNLIKELY(hSelf == NIL_RTTHREAD))
        RTThreadAdopt(RTTHREADTYPE_DEFAULT, 0, NULL, &hSelf);
    return hSelf;
}
RT_EXPORT_SYMBOL(RTThreadSelfAutoAdopt);

#endif /* IN_RING3 */

/**
 * Allocates a per thread data structure and initializes the basic fields.
 *
 * @returns Pointer to per thread data structure.
 *          This is reference once.
 * @returns NULL on failure.
 * @param   enmType     The thread type.
 * @param   fFlags      The thread flags.
 * @param   fIntFlags   The internal thread flags.
 * @param   pszName     Pointer to the thread name.
 */
PRTTHREADINT rtThreadAlloc(RTTHREADTYPE enmType, unsigned fFlags, uint32_t fIntFlags, const char *pszName)
{
    PRTTHREADINT pThread = (PRTTHREADINT)RTMemAllocZ(sizeof(RTTHREADINT));
    if (pThread)
    {
        size_t cchName;
        int rc;

        pThread->Core.Key   = (void*)NIL_RTTHREAD;
        pThread->u32Magic   = RTTHREADINT_MAGIC;
        cchName = strlen(pszName);
        if (cchName >= RTTHREAD_NAME_LEN)
            cchName = RTTHREAD_NAME_LEN - 1;
        memcpy(pThread->szName, pszName, cchName);
        pThread->szName[cchName] = '\0';
        pThread->cRefs           = 2 + !!(fFlags & RTTHREADFLAGS_WAITABLE); /* And extra reference if waitable. */
        pThread->rc              = VERR_PROCESS_RUNNING; /** @todo get a better error code! */
        pThread->enmType         = enmType;
        pThread->fFlags          = fFlags;
        pThread->fIntFlags       = fIntFlags;
        pThread->enmState        = RTTHREADSTATE_INITIALIZING;
        pThread->fReallySleeping = false;
#ifdef IN_RING3
        rtLockValidatorInitPerThread(&pThread->LockValidator);
#endif
#ifdef RT_WITH_ICONV_CACHE
        rtStrIconvCacheInit(pThread);
#endif
        rc = RTSemEventMultiCreate(&pThread->EventUser);
        if (RT_SUCCESS(rc))
        {
            rc = RTSemEventMultiCreate(&pThread->EventTerminated);
            if (RT_SUCCESS(rc))
                return pThread;
            RTSemEventMultiDestroy(pThread->EventUser);
        }
        RTMemFree(pThread);
    }
    return NULL;
}


/**
 * Insert the per thread data structure into the tree.
 *
 * This can be called from both the thread it self and the parent,
 * thus it must handle insertion failures in a nice manner.
 *
 * @param   pThread         Pointer to thread structure allocated by rtThreadAlloc().
 * @param   NativeThread    The native thread id.
 */
DECLHIDDEN(void) rtThreadInsert(PRTTHREADINT pThread, RTNATIVETHREAD NativeThread)
{
    Assert(pThread);
    Assert(pThread->u32Magic == RTTHREADINT_MAGIC);

    {
        RT_THREAD_LOCK_RW();

        /*
         * Do not insert a terminated thread.
         *
         * This may happen if the thread finishes before the RTThreadCreate call
         * gets this far. Since the OS may quickly reuse the native thread ID
         * it should not be reinserted at this point.
         */
        if (rtThreadGetState(pThread) != RTTHREADSTATE_TERMINATED)
        {
            /*
             * Before inserting we must check if there is a thread with this id
             * in the tree already. We're racing parent and child on insert here
             * so that the handle is valid in both ends when they return / start.
             *
             * If it's not ourself we find, it's a dead alien thread and we will
             * unlink it from the tree. Alien threads will be released at this point.
             */
            PRTTHREADINT pThreadOther = (PRTTHREADINT)RTAvlPVGet(&g_ThreadTree, (void *)NativeThread);
            if (pThreadOther != pThread)
            {
                bool fRc;
                /* remove dead alien if any */
                if (pThreadOther)
                {
                    AssertMsg(pThreadOther->fIntFlags & RTTHREADINT_FLAGS_ALIEN, ("%p:%s; %p:%s\n", pThread, pThread->szName, pThreadOther, pThreadOther->szName));
                    ASMAtomicBitClear(&pThread->fIntFlags, RTTHREADINT_FLAG_IN_TREE_BIT);
                    rtThreadRemoveLocked(pThreadOther);
                    if (pThreadOther->fIntFlags & RTTHREADINT_FLAGS_ALIEN)
                        rtThreadRelease(pThreadOther);
                }

                /* insert the thread */
                ASMAtomicWritePtr(&pThread->Core.Key, (void *)NativeThread);
                fRc = RTAvlPVInsert(&g_ThreadTree, &pThread->Core);
                ASMAtomicOrU32(&pThread->fIntFlags, RTTHREADINT_FLAG_IN_TREE);
                if (fRc)
                    ASMAtomicIncU32(&g_cThreadInTree);

                AssertReleaseMsg(fRc, ("Lock problem? %p (%RTnthrd) %s\n", pThread, NativeThread, pThread->szName));
                NOREF(fRc);
            }
        }

        RT_THREAD_UNLOCK_RW();
    }
}


/**
 * Removes the thread from the AVL tree, call owns the tree lock
 * and has cleared the RTTHREADINT_FLAG_IN_TREE bit.
 *
 * @param   pThread     The thread to remove.
 */
static void rtThreadRemoveLocked(PRTTHREADINT pThread)
{
    PRTTHREADINT pThread2 = (PRTTHREADINT)RTAvlPVRemove(&g_ThreadTree, pThread->Core.Key);
#if !defined(RT_OS_OS2) /** @todo this asserts for threads created by NSPR */
    AssertMsg(pThread2 == pThread, ("%p(%s) != %p (%p/%s)\n", pThread2, pThread2  ? pThread2->szName : "<null>",
                                    pThread, pThread->Core.Key, pThread->szName));
#endif
    if (pThread2)
        ASMAtomicDecU32(&g_cThreadInTree);
}


/**
 * Removes the thread from the AVL tree.
 *
 * @param   pThread     The thread to remove.
 */
static void rtThreadRemove(PRTTHREADINT pThread)
{
    RT_THREAD_LOCK_RW();
    if (ASMAtomicBitTestAndClear(&pThread->fIntFlags, RTTHREADINT_FLAG_IN_TREE_BIT))
        rtThreadRemoveLocked(pThread);
    RT_THREAD_UNLOCK_RW();
}


/**
 * Checks if a thread is alive or not.
 *
 * @returns true if the thread is alive (or we don't really know).
 * @returns false if the thread has surely terminate.
 */
DECLINLINE(bool) rtThreadIsAlive(PRTTHREADINT pThread)
{
    return !(pThread->fIntFlags & RTTHREADINT_FLAGS_TERMINATED);
}


/**
 * Gets a thread by it's native ID.
 *
 * @returns pointer to the thread structure.
 * @returns NULL if not a thread IPRT knows.
 * @param   NativeThread    The native thread id.
 */
DECLHIDDEN(PRTTHREADINT) rtThreadGetByNative(RTNATIVETHREAD NativeThread)
{
    PRTTHREADINT pThread;
    /*
     * Simple tree lookup.
     */
    RT_THREAD_LOCK_RD();
    pThread = (PRTTHREADINT)RTAvlPVGet(&g_ThreadTree, (void *)NativeThread);
    RT_THREAD_UNLOCK_RD();
    return pThread;
}


/**
 * Gets the per thread data structure for a thread handle.
 *
 * @returns Pointer to the per thread data structure for Thread.
 *          The caller must release the thread using rtThreadRelease().
 * @returns NULL if Thread was not found.
 * @param   Thread      Thread id which structure is to be returned.
 */
DECLHIDDEN(PRTTHREADINT) rtThreadGet(RTTHREAD Thread)
{
    if (    Thread != NIL_RTTHREAD
        &&  VALID_PTR(Thread))
    {
        PRTTHREADINT pThread = (PRTTHREADINT)Thread;
        if (    pThread->u32Magic == RTTHREADINT_MAGIC
            &&  pThread->cRefs > 0)
        {
            ASMAtomicIncU32(&pThread->cRefs);
            return pThread;
        }
    }

    AssertMsgFailed(("Thread=%RTthrd\n", Thread));
    return NULL;
}

/**
 * Release a per thread data structure.
 *
 * @returns New reference count.
 * @param   pThread     The thread structure to release.
 */
DECLHIDDEN(uint32_t) rtThreadRelease(PRTTHREADINT pThread)
{
    uint32_t cRefs;

    Assert(pThread);
    if (pThread->cRefs >= 1)
    {
        cRefs = ASMAtomicDecU32(&pThread->cRefs);
        if (!cRefs)
            rtThreadDestroy(pThread);
    }
    else
    {
        cRefs = 0;
        AssertFailed();
    }
    return cRefs;
}


/**
 * Destroys the per thread data.
 *
 * @param   pThread     The thread to destroy.
 */
static void rtThreadDestroy(PRTTHREADINT pThread)
{
    RTSEMEVENTMULTI hEvt1, hEvt2;
    /*
     * Remove it from the tree and mark it as dead.
     *
     * Threads that has seen rtThreadTerminate and should already have been
     * removed from the tree. There is probably no thread that  should
     * require removing here. However, be careful making sure that cRefs
     * isn't 0 if we do or we'll blow up because the strict locking code
     * will be calling us back.
     */
    if (ASMBitTest(&pThread->fIntFlags, RTTHREADINT_FLAG_IN_TREE_BIT))
    {
        ASMAtomicIncU32(&pThread->cRefs);
        rtThreadRemove(pThread);
        ASMAtomicDecU32(&pThread->cRefs);
    }

    /*
     * Invalidate the thread structure.
     */
#ifdef IN_RING3
    rtLockValidatorSerializeDestructEnter();

    rtLockValidatorDeletePerThread(&pThread->LockValidator);
#endif
#ifdef RT_WITH_ICONV_CACHE
    rtStrIconvCacheDestroy(pThread);
#endif
    ASMAtomicXchgU32(&pThread->u32Magic, RTTHREADINT_MAGIC_DEAD);
    ASMAtomicWritePtr(&pThread->Core.Key, (void *)NIL_RTTHREAD);
    pThread->enmType         = RTTHREADTYPE_INVALID;
    hEvt1    = pThread->EventUser;
    pThread->EventUser       = NIL_RTSEMEVENTMULTI;
    hEvt2    = pThread->EventTerminated;
    pThread->EventTerminated = NIL_RTSEMEVENTMULTI;

#ifdef IN_RING3
    rtLockValidatorSerializeDestructLeave();
#endif

    /*
     * Destroy semaphore resources and free the bugger.
     */
    RTSemEventMultiDestroy(hEvt1);
    if (hEvt2 != NIL_RTSEMEVENTMULTI)
        RTSemEventMultiDestroy(hEvt2);

    rtThreadNativeDestroy(pThread);
    RTMemFree(pThread);
}


/**
 * Terminates the thread.
 * Called by the thread wrapper function when the thread terminates.
 *
 * @param   pThread     The thread structure.
 * @param   rc          The thread result code.
 */
DECLHIDDEN(void) rtThreadTerminate(PRTTHREADINT pThread, int rc)
{
    Assert(pThread->cRefs >= 1);

#ifdef IPRT_WITH_GENERIC_TLS
    /*
     * Destroy TLS entries.
     */
    rtThreadTlsDestruction(pThread);
#endif /* IPRT_WITH_GENERIC_TLS */

    /*
     * Set the rc, mark it terminated and signal anyone waiting.
     */
    pThread->rc = rc;
    rtThreadSetState(pThread, RTTHREADSTATE_TERMINATED);
    ASMAtomicOrU32(&pThread->fIntFlags, RTTHREADINT_FLAGS_TERMINATED);
    if (pThread->EventTerminated != NIL_RTSEMEVENTMULTI)
        RTSemEventMultiSignal(pThread->EventTerminated);

    /*
     * Remove the thread from the tree so that there will be no
     * key clashes in the AVL tree and release our reference to ourself.
     */
    rtThreadRemove(pThread);
    rtThreadRelease(pThread);
}


/**
 * The common thread main function.
 * This is called by rtThreadNativeMain().
 *
 * @returns The status code of the thread.
 *          pThread is dereference by the thread before returning!
 * @param   pThread         The thread structure.
 * @param   NativeThread    The native thread id.
 * @param   pszThreadName   The name of the thread (purely a dummy for backtrace).
 */
DECLCALLBACK(DECLHIDDEN(int)) rtThreadMain(PRTTHREADINT pThread, RTNATIVETHREAD NativeThread, const char *pszThreadName)
{
    int rc;
    NOREF(pszThreadName);
    rtThreadInsert(pThread, NativeThread);
    Log(("rtThreadMain: Starting: pThread=%p NativeThread=%RTnthrd Name=%s pfnThread=%p pvUser=%p\n",
         pThread, NativeThread, pThread->szName, pThread->pfnThread, pThread->pvUser));

    /*
     * Change the priority.
     */
    rc = rtThreadNativeSetPriority(pThread, pThread->enmType);
#ifdef IN_RING3
    AssertMsgRC(rc, ("Failed to set priority of thread %p (%RTnthrd / %s) to enmType=%d enmPriority=%d rc=%Rrc\n",
                     pThread, NativeThread, pThread->szName, pThread->enmType, g_enmProcessPriority, rc));
#else
    AssertMsgRC(rc, ("Failed to set priority of thread %p (%RTnthrd / %s) to enmType=%d rc=%Rrc\n",
                     pThread, NativeThread, pThread->szName, pThread->enmType, rc));
#endif

    /*
     * Call thread function and terminate when it returns.
     */
    rtThreadSetState(pThread, RTTHREADSTATE_RUNNING);
    rc = pThread->pfnThread(pThread, pThread->pvUser);

    /*
     * Paranoia checks for leftover resources.
     */
#ifdef RTSEMRW_STRICT
    int32_t cWrite = ASMAtomicReadS32(&pThread->cWriteLocks);
    Assert(!cWrite);
    int32_t cRead = ASMAtomicReadS32(&pThread->cReadLocks);
    Assert(!cRead);
#endif

    Log(("rtThreadMain: Terminating: rc=%d pThread=%p NativeThread=%RTnthrd Name=%s pfnThread=%p pvUser=%p\n",
         rc, pThread, NativeThread, pThread->szName, pThread->pfnThread, pThread->pvUser));
    rtThreadTerminate(pThread, rc);
    return rc;
}


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
 */
RTDECL(int) RTThreadCreate(PRTTHREAD pThread, PFNRTTHREAD pfnThread, void *pvUser, size_t cbStack,
                           RTTHREADTYPE enmType, unsigned fFlags, const char *pszName)
{
    int             rc;
    PRTTHREADINT    pThreadInt;

    LogFlow(("RTThreadCreate: pThread=%p pfnThread=%p pvUser=%p cbStack=%#x enmType=%d fFlags=%#x pszName=%p:{%s}\n",
             pThread, pfnThread, pvUser, cbStack, enmType, fFlags, pszName, pszName));

    /*
     * Validate input.
     */
    if (!VALID_PTR(pThread) && pThread)
    {
        Assert(VALID_PTR(pThread));
        return VERR_INVALID_PARAMETER;
    }
    if (!VALID_PTR(pfnThread))
    {
        Assert(VALID_PTR(pfnThread));
        return VERR_INVALID_PARAMETER;
    }
    if (!pszName || !*pszName || strlen(pszName) >= RTTHREAD_NAME_LEN)
    {
        AssertMsgFailed(("pszName=%s (max len is %d because of logging)\n", pszName, RTTHREAD_NAME_LEN - 1));
        return VERR_INVALID_PARAMETER;
    }
    if (fFlags & ~RTTHREADFLAGS_MASK)
    {
        AssertMsgFailed(("fFlags=%#x\n", fFlags));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Allocate thread argument.
     */
    pThreadInt = rtThreadAlloc(enmType, fFlags, 0, pszName);
    if (pThreadInt)
    {
        RTNATIVETHREAD NativeThread;

        pThreadInt->pfnThread = pfnThread;
        pThreadInt->pvUser    = pvUser;
        pThreadInt->cbStack   = cbStack;

        rc = rtThreadNativeCreate(pThreadInt, &NativeThread);
        if (RT_SUCCESS(rc))
        {
            rtThreadInsert(pThreadInt, NativeThread);
            rtThreadRelease(pThreadInt);
            Log(("RTThreadCreate: Created thread %p (%p) %s\n", pThreadInt, NativeThread, pszName));
            if (pThread)
                *pThread = pThreadInt;
            return VINF_SUCCESS;
        }

        pThreadInt->cRefs = 1;
        rtThreadRelease(pThreadInt);
    }
    else
        rc = VERR_NO_TMP_MEMORY;
    LogFlow(("RTThreadCreate: Failed to create thread, rc=%Rrc\n", rc));
    AssertReleaseRC(rc);
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadCreate);


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
 * @param   pszNameFmt  Thread name format.
 * @param   va          Format arguments.
 */
RTDECL(int) RTThreadCreateV(PRTTHREAD pThread, PFNRTTHREAD pfnThread, void *pvUser, size_t cbStack,
                            RTTHREADTYPE enmType, uint32_t fFlags, const char *pszNameFmt, va_list va)
{
    char szName[RTTHREAD_NAME_LEN * 2];
    RTStrPrintfV(szName, sizeof(szName), pszNameFmt, va);
    return RTThreadCreate(pThread, pfnThread, pvUser, cbStack, enmType, fFlags, szName);
}
RT_EXPORT_SYMBOL(RTThreadCreateV);


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
 * @param   pszNameFmt  Thread name format.
 * @param   ...         Format arguments.
 */
RTDECL(int) RTThreadCreateF(PRTTHREAD pThread, PFNRTTHREAD pfnThread, void *pvUser, size_t cbStack,
                            RTTHREADTYPE enmType, uint32_t fFlags, const char *pszNameFmt, ...)
{
    va_list va;
    int rc;
    va_start(va, pszNameFmt);
    rc = RTThreadCreateV(pThread, pfnThread, pvUser, cbStack, enmType, fFlags, pszNameFmt, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadCreateF);


/**
 * Gets the native thread id of a IPRT thread.
 *
 * @returns The native thread id.
 * @param   Thread      The IPRT thread.
 */
RTDECL(RTNATIVETHREAD) RTThreadGetNative(RTTHREAD Thread)
{
    PRTTHREADINT pThread = rtThreadGet(Thread);
    if (pThread)
    {
        RTNATIVETHREAD NativeThread = (RTNATIVETHREAD)pThread->Core.Key;
        rtThreadRelease(pThread);
        return NativeThread;
    }
    return NIL_RTNATIVETHREAD;
}
RT_EXPORT_SYMBOL(RTThreadGetNative);


/**
 * Gets the IPRT thread of a native thread.
 *
 * @returns The IPRT thread handle
 * @returns NIL_RTTHREAD if not a thread known to IPRT.
 * @param   NativeThread        The native thread handle/id.
 */
RTDECL(RTTHREAD) RTThreadFromNative(RTNATIVETHREAD NativeThread)
{
    PRTTHREADINT pThread = rtThreadGetByNative(NativeThread);
    if (pThread)
        return pThread;
    return NIL_RTTHREAD;
}
RT_EXPORT_SYMBOL(RTThreadFromNative);


/**
 * Gets the name of the current thread thread.
 *
 * @returns Pointer to readonly name string.
 * @returns NULL on failure.
 */
RTDECL(const char *) RTThreadSelfName(void)
{
    RTTHREAD Thread = RTThreadSelf();
    if (Thread != NIL_RTTHREAD)
    {
        PRTTHREADINT pThread = rtThreadGet(Thread);
        if (pThread)
        {
            const char *szName = pThread->szName;
            rtThreadRelease(pThread);
            return szName;
        }
    }
    return NULL;
}
RT_EXPORT_SYMBOL(RTThreadSelfName);


/**
 * Gets the name of a thread.
 *
 * @returns Pointer to readonly name string.
 * @returns NULL on failure.
 * @param   Thread      Thread handle of the thread to query the name of.
 */
RTDECL(const char *) RTThreadGetName(RTTHREAD Thread)
{
    PRTTHREADINT pThread;
    if (Thread == NIL_RTTHREAD)
        return NULL;
    pThread = rtThreadGet(Thread);
    if (pThread)
    {
        const char *szName = pThread->szName;
        rtThreadRelease(pThread);
        return szName;
    }
    return NULL;
}
RT_EXPORT_SYMBOL(RTThreadGetName);


/**
 * Sets the name of a thread.
 *
 * @returns iprt status code.
 * @param   Thread      Thread handle of the thread to query the name of.
 * @param   pszName     The thread name.
 */
RTDECL(int) RTThreadSetName(RTTHREAD Thread, const char *pszName)
{
    /*
     * Validate input.
     */
    PRTTHREADINT pThread;
    size_t cchName = strlen(pszName);
    if (cchName >= RTTHREAD_NAME_LEN)
    {
        AssertMsgFailed(("pszName=%s is too long, max is %d\n", pszName, RTTHREAD_NAME_LEN - 1));
        return VERR_INVALID_PARAMETER;
    }
    pThread = rtThreadGet(Thread);
    if (!pThread)
        return VERR_INVALID_HANDLE;

    /*
     * Update the name.
     */
    pThread->szName[cchName] = '\0';    /* paranoia */
    memcpy(pThread->szName, pszName, cchName);
    rtThreadRelease(pThread);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTThreadSetName);


/**
 * Checks if the specified thread is the main thread.
 *
 * @returns true if it is, false if it isn't.
 *
 * @param   hThread     The thread handle.
 *
 * @remarks This function may not return the correct value when rtR3Init was
 *          called on a thread of the than the main one.  This could for
 *          instance happen when the DLL/DYLIB/SO containing IPRT is dynamically
 *          loaded at run time by a different thread.
 */
RTDECL(bool) RTThreadIsMain(RTTHREAD hThread)
{
    PRTTHREADINT pThread = rtThreadGet(hThread);
    if (pThread)
    {
        bool fRc = !!(pThread->fIntFlags & RTTHREADINT_FLAGS_MAIN);
        rtThreadRelease(pThread);
        return fRc;
    }
    return false;
}
RT_EXPORT_SYMBOL(RTThreadIsMain);


RTDECL(bool) RTThreadIsSelfAlive(void)
{
    if (g_frtThreadInitialized)
    {
        RTTHREAD hSelf = RTThreadSelf();
        if (hSelf != NIL_RTTHREAD)
        {
            /*
             * Inspect the thread state.  ASSUMES thread state order.
             */
            RTTHREADSTATE enmState = rtThreadGetState(hSelf);
            if (   enmState >= RTTHREADSTATE_RUNNING
                && enmState <= RTTHREADSTATE_END)
                return true;
        }
    }
    return false;
}
RT_EXPORT_SYMBOL(RTThreadIsSelfAlive);


RTDECL(bool) RTThreadIsSelfKnown(void)
{
    if (g_frtThreadInitialized)
    {
        RTTHREAD hSelf = RTThreadSelf();
        if (hSelf != NIL_RTTHREAD)
            return true;
    }
    return false;
}
RT_EXPORT_SYMBOL(RTThreadIsSelfKnown);


RTDECL(bool) RTThreadIsInitialized(void)
{
    return g_frtThreadInitialized;
}
RT_EXPORT_SYMBOL(RTThreadIsInitialized);


/**
 * Signal the user event.
 *
 * @returns     iprt status code.
 */
RTDECL(int) RTThreadUserSignal(RTTHREAD Thread)
{
    int             rc;
    PRTTHREADINT    pThread = rtThreadGet(Thread);
    if (pThread)
    {
        rc = RTSemEventMultiSignal(pThread->EventUser);
        rtThreadRelease(pThread);
    }
    else
        rc = VERR_INVALID_HANDLE;
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadUserSignal);


/**
 * Wait for the user event, resume on interruption.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 */
RTDECL(int) RTThreadUserWait(RTTHREAD Thread, RTMSINTERVAL cMillies)
{
    int             rc;
    PRTTHREADINT    pThread = rtThreadGet(Thread);
    if (pThread)
    {
        rc = RTSemEventMultiWait(pThread->EventUser, cMillies);
        rtThreadRelease(pThread);
    }
    else
        rc = VERR_INVALID_HANDLE;
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadUserWait);


/**
 * Wait for the user event, return on interruption.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 */
RTDECL(int) RTThreadUserWaitNoResume(RTTHREAD Thread, RTMSINTERVAL cMillies)
{
    int             rc;
    PRTTHREADINT    pThread = rtThreadGet(Thread);
    if (pThread)
    {
        rc = RTSemEventMultiWaitNoResume(pThread->EventUser, cMillies);
        rtThreadRelease(pThread);
    }
    else
        rc = VERR_INVALID_HANDLE;
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadUserWaitNoResume);


/**
 * Reset the user event.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to reset.
 */
RTDECL(int) RTThreadUserReset(RTTHREAD Thread)
{
    int     rc;
    PRTTHREADINT  pThread = rtThreadGet(Thread);
    if (pThread)
    {
        rc = RTSemEventMultiReset(pThread->EventUser);
        rtThreadRelease(pThread);
    }
    else
        rc = VERR_INVALID_HANDLE;
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadUserReset);


/**
 * Wait for the thread to terminate.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 * @param       prc             Where to store the return code of the thread. Optional.
 * @param       fAutoResume     Whether or not to resume the wait on VERR_INTERRUPTED.
 */
static int rtThreadWait(RTTHREAD Thread, RTMSINTERVAL cMillies, int *prc, bool fAutoResume)
{
    int rc = VERR_INVALID_HANDLE;
    if (Thread != NIL_RTTHREAD)
    {
        PRTTHREADINT pThread = rtThreadGet(Thread);
        if (pThread)
        {
            if (pThread->fFlags & RTTHREADFLAGS_WAITABLE)
            {
#if defined(IN_RING3) && defined(RT_OS_WINDOWS)
                if (RT_LIKELY(rtThreadNativeIsAliveKludge(pThread)))
#endif
                {
                    if (fAutoResume)
                        rc = RTSemEventMultiWait(pThread->EventTerminated, cMillies);
                    else
                        rc = RTSemEventMultiWaitNoResume(pThread->EventTerminated, cMillies);
                }
#if defined(IN_RING3) && defined(RT_OS_WINDOWS)
                else
                {
                    rc = VINF_SUCCESS;
                    if (pThread->rc == VERR_PROCESS_RUNNING)
                        pThread->rc = VERR_THREAD_IS_DEAD;
                }
#endif
                if (RT_SUCCESS(rc))
                {
                    if (prc)
                        *prc = pThread->rc;

                    /*
                     * If the thread is marked as waitable, we'll do one additional
                     * release in order to free up the thread structure (see how we
                     * init cRef in rtThreadAlloc()).
                     */
                    if (ASMAtomicBitTestAndClear(&pThread->fFlags, RTTHREADFLAGS_WAITABLE_BIT))
                    {
                        rtThreadRelease(pThread);
#ifdef IN_RING0
                        /*
                         * IPRT termination kludge. Call native code to make sure
                         * the last thread is really out of IPRT to prevent it from
                         * crashing after we destroyed the spinlock in rtThreadTerm.
                         */
                        if (   ASMAtomicReadU32(&g_cThreadInTree) == 1
                            && ASMAtomicReadU32(&pThread->cRefs) > 1)
                            rtThreadNativeWaitKludge(pThread);
#endif
                    }
                }
            }
            else
            {
                rc = VERR_THREAD_NOT_WAITABLE;
                AssertRC(rc);
            }
            rtThreadRelease(pThread);
        }
    }
    return rc;
}


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
RTDECL(int) RTThreadWait(RTTHREAD Thread, RTMSINTERVAL cMillies, int *prc)
{
    int rc = rtThreadWait(Thread, cMillies, prc, true);
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadWait);


/**
 * Wait for the thread to terminate, return on interruption.
 *
 * @returns     iprt status code.
 * @param       Thread          The thread to wait for.
 * @param       cMillies        The number of milliseconds to wait. Use RT_INDEFINITE_WAIT for
 *                              an indefinite wait.
 * @param       prc             Where to store the return code of the thread. Optional.
 */
RTDECL(int) RTThreadWaitNoResume(RTTHREAD Thread, RTMSINTERVAL cMillies, int *prc)
{
    return rtThreadWait(Thread, cMillies, prc, false);
}
RT_EXPORT_SYMBOL(RTThreadWaitNoResume);


/**
 * Changes the type of the specified thread.
 *
 * @returns iprt status code.
 * @param   Thread      The thread which type should be changed.
 * @param   enmType     The new thread type.
 */
RTDECL(int) RTThreadSetType(RTTHREAD Thread, RTTHREADTYPE enmType)
{
    /*
     * Validate input.
     */
    int     rc;
    if (    enmType > RTTHREADTYPE_INVALID
        &&  enmType < RTTHREADTYPE_END)
    {
        PRTTHREADINT pThread = rtThreadGet(Thread);
        if (pThread)
        {
            if (rtThreadIsAlive(pThread))
            {
                /*
                 * Do the job.
                 */
                RT_THREAD_LOCK_RW();
                rc = rtThreadNativeSetPriority(pThread, enmType);
                if (RT_SUCCESS(rc))
                    ASMAtomicXchgSize(&pThread->enmType, enmType);
                RT_THREAD_UNLOCK_RW();
                if (RT_FAILURE(rc))
                    Log(("RTThreadSetType: failed on thread %p (%s), rc=%Rrc!!!\n", Thread, pThread->szName, rc));
            }
            else
                rc = VERR_THREAD_IS_DEAD;
            rtThreadRelease(pThread);
        }
        else
            rc = VERR_INVALID_HANDLE;
    }
    else
    {
        AssertMsgFailed(("enmType=%d\n", enmType));
        rc = VERR_INVALID_PARAMETER;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTThreadSetType);


/**
 * Gets the type of the specified thread.
 *
 * @returns The thread type.
 * @returns RTTHREADTYPE_INVALID if the thread handle is invalid.
 * @param   Thread      The thread in question.
 */
RTDECL(RTTHREADTYPE) RTThreadGetType(RTTHREAD Thread)
{
    RTTHREADTYPE enmType = RTTHREADTYPE_INVALID;
    PRTTHREADINT pThread = rtThreadGet(Thread);
    if (pThread)
    {
        enmType = pThread->enmType;
        rtThreadRelease(pThread);
    }
    return enmType;
}
RT_EXPORT_SYMBOL(RTThreadGetType);

#ifdef IN_RING3

/**
 * Recalculates scheduling attributes for the default process
 * priority using the specified priority type for the calling thread.
 *
 * The scheduling attributes are targeted at threads and they are protected
 * by the thread read-write semaphore, that's why RTProc is forwarding the
 * operation to RTThread.
 *
 * @returns iprt status code.
 * @remarks Will only work for strict builds.
 */
int rtThreadDoCalcDefaultPriority(RTTHREADTYPE enmType)
{
    RT_THREAD_LOCK_RW();
    int rc = rtSchedNativeCalcDefaultPriority(enmType);
    RT_THREAD_UNLOCK_RW();
    return rc;
}


/**
 * Thread enumerator - sets the priority of one thread.
 *
 * @returns 0 to continue.
 * @returns !0 to stop. In our case a VERR_ code.
 * @param   pNode   The thread node.
 * @param   pvUser  The new priority.
 */
static DECLCALLBACK(int) rtThreadSetPriorityOne(PAVLPVNODECORE pNode, void *pvUser)
{
    PRTTHREADINT pThread = (PRTTHREADINT)pNode;
    if (!rtThreadIsAlive(pThread))
        return VINF_SUCCESS;
    int rc = rtThreadNativeSetPriority(pThread, pThread->enmType);
    if (RT_SUCCESS(rc)) /* hide any warnings */
        return VINF_SUCCESS;
    NOREF(pvUser);
    return rc;
}


/**
 * Attempts to alter the priority of the current process.
 *
 * The scheduling attributes are targeted at threads and they are protected
 * by the thread read-write semaphore, that's why RTProc is forwarding the
 * operation to RTThread. This operation also involves updating all thread
 * which is much faster done from RTThread.
 *
 * @returns iprt status code.
 * @param   enmPriority     The new priority.
 */
DECLHIDDEN(int) rtThreadDoSetProcPriority(RTPROCPRIORITY enmPriority)
{
    LogFlow(("rtThreadDoSetProcPriority: enmPriority=%d\n", enmPriority));

    /*
     * First validate that we're allowed by the OS to use all the
     * scheduling attributes defined by the specified process priority.
     */
    RT_THREAD_LOCK_RW();
    int rc = rtProcNativeSetPriority(enmPriority);
    if (RT_SUCCESS(rc))
    {
        /*
         * Update the priority of existing thread.
         */
        rc = RTAvlPVDoWithAll(&g_ThreadTree, true, rtThreadSetPriorityOne, NULL);
        if (RT_SUCCESS(rc))
            ASMAtomicXchgSize(&g_enmProcessPriority, enmPriority);
        else
        {
            /*
             * Failed, restore the priority.
             */
            rtProcNativeSetPriority(g_enmProcessPriority);
            RTAvlPVDoWithAll(&g_ThreadTree, true, rtThreadSetPriorityOne, NULL);
        }
    }
    RT_THREAD_UNLOCK_RW();
    LogFlow(("rtThreadDoSetProcPriority: returns %Rrc\n", rc));
    return rc;
}


/**
 * Change the thread state to blocking.
 *
 * @param   hThread         The current thread.
 * @param   enmState        The sleep state.
 * @param   fReallySleeping Really going to sleep now.
 */
RTDECL(void) RTThreadBlocking(RTTHREAD hThread, RTTHREADSTATE enmState, bool fReallySleeping)
{
    Assert(RTTHREAD_IS_SLEEPING(enmState));
    PRTTHREADINT pThread = hThread;
    if (pThread != NIL_RTTHREAD)
    {
        Assert(pThread == RTThreadSelf());
        if (rtThreadGetState(pThread) == RTTHREADSTATE_RUNNING)
            rtThreadSetState(pThread, enmState);
        ASMAtomicWriteBool(&pThread->fReallySleeping, fReallySleeping);
    }
}
RT_EXPORT_SYMBOL(RTThreadBlocking);


/**
 * Unblocks a thread.
 *
 * This function is paired with rtThreadBlocking.
 *
 * @param   hThread     The current thread.
 * @param   enmCurState The current state, used to check for nested blocking.
 *                      The new state will be running.
 */
RTDECL(void) RTThreadUnblocked(RTTHREAD hThread, RTTHREADSTATE enmCurState)
{
    PRTTHREADINT pThread = hThread;
    if (pThread != NIL_RTTHREAD)
    {
        Assert(pThread == RTThreadSelf());
        ASMAtomicWriteBool(&pThread->fReallySleeping, false);

        RTTHREADSTATE enmActualState = rtThreadGetState(pThread);
        if (enmActualState == enmCurState)
        {
            rtThreadSetState(pThread, RTTHREADSTATE_RUNNING);
            if (   pThread->LockValidator.pRec
                && pThread->LockValidator.enmRecState == enmCurState)
                ASMAtomicWriteNullPtr(&pThread->LockValidator.pRec);
        }
        /* This is a bit ugly... :-/ */
        else if (   (   enmActualState == RTTHREADSTATE_TERMINATED
                     || enmActualState == RTTHREADSTATE_INITIALIZING)
                 && pThread->LockValidator.pRec)
            ASMAtomicWriteNullPtr(&pThread->LockValidator.pRec);
        Assert(   pThread->LockValidator.pRec == NULL
               || RTTHREAD_IS_SLEEPING(enmActualState));
    }
}
RT_EXPORT_SYMBOL(RTThreadUnblocked);


/**
 * Get the current thread state.
 *
 * @returns The thread state.
 * @param   hThread         The thread.
 */
RTDECL(RTTHREADSTATE) RTThreadGetState(RTTHREAD hThread)
{
    RTTHREADSTATE   enmState = RTTHREADSTATE_INVALID;
    PRTTHREADINT    pThread  = rtThreadGet(hThread);
    if (pThread)
    {
        enmState = rtThreadGetState(pThread);
        rtThreadRelease(pThread);
    }
    return enmState;
}
RT_EXPORT_SYMBOL(RTThreadGetState);


RTDECL(RTTHREADSTATE) RTThreadGetReallySleeping(RTTHREAD hThread)
{
    RTTHREADSTATE   enmState = RTTHREADSTATE_INVALID;
    PRTTHREADINT    pThread  = rtThreadGet(hThread);
    if (pThread)
    {
        enmState = rtThreadGetState(pThread);
        if (!ASMAtomicUoReadBool(&pThread->fReallySleeping))
            enmState = RTTHREADSTATE_RUNNING;
        rtThreadRelease(pThread);
    }
    return enmState;
}
RT_EXPORT_SYMBOL(RTThreadGetReallySleeping);


/**
 * Translate a thread state into a string.
 *
 * @returns Pointer to a read-only string containing the state name.
 * @param   enmState            The state.
 */
RTDECL(const char *) RTThreadStateName(RTTHREADSTATE enmState)
{
    switch (enmState)
    {
        case RTTHREADSTATE_INVALID:         return "INVALID";
        case RTTHREADSTATE_INITIALIZING:    return "INITIALIZING";
        case RTTHREADSTATE_TERMINATED:      return "TERMINATED";
        case RTTHREADSTATE_RUNNING:         return "RUNNING";
        case RTTHREADSTATE_CRITSECT:        return "CRITSECT";
        case RTTHREADSTATE_EVENT:           return "EVENT";
        case RTTHREADSTATE_EVENT_MULTI:     return "EVENT_MULTI";
        case RTTHREADSTATE_FAST_MUTEX:      return "FAST_MUTEX";
        case RTTHREADSTATE_MUTEX:           return "MUTEX";
        case RTTHREADSTATE_RW_READ:         return "RW_READ";
        case RTTHREADSTATE_RW_WRITE:        return "RW_WRITE";
        case RTTHREADSTATE_SLEEP:           return "SLEEP";
        case RTTHREADSTATE_SPIN_MUTEX:      return "SPIN_MUTEX";
        default:                            return "UnknownThreadState";
    }
}
RT_EXPORT_SYMBOL(RTThreadStateName);

#endif /* IN_RING3 */
#ifdef IPRT_WITH_GENERIC_TLS

/**
 * Thread enumerator - clears a TLS entry.
 *
 * @returns 0.
 * @param   pNode   The thread node.
 * @param   pvUser  The TLS index.
 */
static DECLCALLBACK(int) rtThreadClearTlsEntryCallback(PAVLPVNODECORE pNode, void *pvUser)
{
    PRTTHREADINT pThread = (PRTTHREADINT)pNode;
    RTTLS iTls = (RTTLS)(uintptr_t)pvUser;
    ASMAtomicWriteNullPtr(&pThread->apvTlsEntries[iTls]);
    return 0;
}


/**
 * Helper for the generic TLS implementation that clears a given TLS
 * entry on all threads.
 *
 * @param   iTls        The TLS entry. (valid)
 */
DECLHIDDEN(void) rtThreadClearTlsEntry(RTTLS iTls)
{
    RT_THREAD_LOCK_RD();
    RTAvlPVDoWithAll(&g_ThreadTree, true /* fFromLeft*/, rtThreadClearTlsEntryCallback, (void *)(uintptr_t)iTls);
    RT_THREAD_UNLOCK_RD();
}

#endif /* IPRT_WITH_GENERIC_TLS */


#if defined(RT_OS_WINDOWS) && defined(IN_RING3)

/**
 * Thread enumeration callback for RTThreadNameThreads
 */
static DECLCALLBACK(int) rtThreadNameThreadCallback(PAVLPVNODECORE pNode, void *pvUser)
{
    PRTTHREADINT pThread = (PRTTHREADINT)pNode;
    rtThreadNativeInformDebugger(pThread);
    RT_NOREF_PV(pvUser);
    return 0;
}

/**
 * A function that can be called from the windows debugger to get the names of
 * all threads when attaching to a process.
 *
 * Usage: .call VBoxRT!RTThreadNameThreads()
 *
 * @returns 0
 * @remarks Do not call from source code as it skips locks.
 */
extern "C" RTDECL(int) RTThreadNameThreads(void);
RTDECL(int) RTThreadNameThreads(void)
{
    return RTAvlPVDoWithAll(&g_ThreadTree, true /* fFromLeft*/, rtThreadNameThreadCallback, NULL);
}

#endif
