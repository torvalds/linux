/* $Id: powernotification-r0drv.c $ */
/** @file
 * IPRT - Power Management, Ring-0 Driver, Event Notifications.
 */

/*
 * Copyright (C) 2008-2016 Oracle Corporation
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
#include <iprt/power.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include "r0drv/mp-r0drv.h"
#include "r0drv/power-r0drv.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Notification registration record tracking
 * RTPowerRegisterNotification() calls.
 */
typedef struct RTPOWERNOTIFYREG
{
    /** Pointer to the next record. */
    struct RTPOWERNOTIFYREG * volatile pNext;
    /** The callback. */
    PFNRTPOWERNOTIFICATION pfnCallback;
    /** The user argument. */
    void *pvUser;
    /** Bit mask indicating whether we've done this callback or not. */
    uint8_t bmDone[sizeof(void *)];
} RTPOWERNOTIFYREG;
/** Pointer to a registration record. */
typedef RTPOWERNOTIFYREG *PRTPOWERNOTIFYREG;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The spinlock protecting the list. */
static RTSPINLOCK volatile g_hRTPowerNotifySpinLock = NIL_RTSPINLOCK;
/** List of callbacks, in registration order. */
static PRTPOWERNOTIFYREG volatile g_pRTPowerCallbackHead = NULL;
/** The current done bit. */
static uint32_t volatile g_iRTPowerDoneBit;
/** The list generation.
 * This is increased whenever the list has been modified. The callback routine
 * make use of this to avoid having restart at the list head after each callback. */
static uint32_t volatile g_iRTPowerGeneration;




RTDECL(int) RTPowerSignalEvent(RTPOWEREVENT enmEvent)
{
    PRTPOWERNOTIFYREG pCur;
    RTSPINLOCK        hSpinlock;

    /*
     * This is a little bit tricky as we cannot be holding the spinlock
     * while calling the callback. This means that the list might change
     * while we're walking it, and that multiple events might be running
     * concurrently (depending on the OS).
     *
     * So, the first measure is to employ a 32-bitmask for each
     * record where we'll use a bit that rotates for each call to
     * this function to indicate which records that has been
     * processed. This will take care of both changes to the list
     * and a reasonable amount of concurrent events.
     *
     * In order to avoid having to restart the list walks for every
     * callback we make, we'll make use a list generation number that is
     * incremented everytime the list is changed. So, if it remains
     * unchanged over a callback we can safely continue the iteration.
     */
    uint32_t iDone = ASMAtomicIncU32(&g_iRTPowerDoneBit);
    iDone %= RT_SIZEOFMEMB(RTPOWERNOTIFYREG, bmDone) * 8;

    hSpinlock = g_hRTPowerNotifySpinLock;
    if (hSpinlock == NIL_RTSPINLOCK)
        return VERR_ACCESS_DENIED;
    RTSpinlockAcquire(hSpinlock);

    /* Clear the bit. */
    for (pCur = g_pRTPowerCallbackHead; pCur; pCur = pCur->pNext)
        ASMAtomicBitClear(&pCur->bmDone[0], iDone);

    /* Iterate the records and perform the callbacks. */
    do
    {
        uint32_t const iGeneration = ASMAtomicUoReadU32(&g_iRTPowerGeneration);

        pCur = g_pRTPowerCallbackHead;
        while (pCur)
        {
            if (!ASMAtomicBitTestAndSet(&pCur->bmDone[0], iDone))
            {
                PFNRTPOWERNOTIFICATION pfnCallback = pCur->pfnCallback;
                void *pvUser = pCur->pvUser;
                pCur = pCur->pNext;
                RTSpinlockRelease(g_hRTPowerNotifySpinLock);

                pfnCallback(enmEvent, pvUser);

                /* carefully require the lock here, see RTR0MpNotificationTerm(). */
                hSpinlock = g_hRTPowerNotifySpinLock;
                if (hSpinlock == NIL_RTSPINLOCK)
                    return VERR_ACCESS_DENIED;
                RTSpinlockAcquire(hSpinlock);
                if (ASMAtomicUoReadU32(&g_iRTPowerGeneration) != iGeneration)
                    break;
            }
            else
                pCur = pCur->pNext;
        }
    } while (pCur);

    RTSpinlockRelease(hSpinlock);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTPowerSignalEvent);


RTDECL(int) RTPowerNotificationRegister(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser)
{
    PRTPOWERNOTIFYREG   pCur;
    PRTPOWERNOTIFYREG   pNew;

    /*
     * Validation.
     */
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertReturn(g_hRTPowerNotifySpinLock != NIL_RTSPINLOCK, VERR_WRONG_ORDER);
    RT_ASSERT_PREEMPTIBLE();

    RTSpinlockAcquire(g_hRTPowerNotifySpinLock);
    for (pCur = g_pRTPowerCallbackHead; pCur; pCur = pCur->pNext)
        if (    pCur->pvUser == pvUser
            &&  pCur->pfnCallback == pfnCallback)
            break;
    RTSpinlockRelease(g_hRTPowerNotifySpinLock);
    AssertMsgReturn(!pCur, ("pCur=%p pfnCallback=%p pvUser=%p\n", pCur, pfnCallback, pvUser), VERR_ALREADY_EXISTS);

    /*
     * Allocate a new record and attempt to insert it.
     */
    pNew = (PRTPOWERNOTIFYREG)RTMemAlloc(sizeof(*pNew));
    if (!pNew)
        return VERR_NO_MEMORY;

    pNew->pNext = NULL;
    pNew->pfnCallback = pfnCallback;
    pNew->pvUser = pvUser;
    memset(&pNew->bmDone[0], 0xff, sizeof(pNew->bmDone));

    RTSpinlockAcquire(g_hRTPowerNotifySpinLock);

    pCur = g_pRTPowerCallbackHead;
    if (!pCur)
        g_pRTPowerCallbackHead = pNew;
    else
    {
        for (pCur = g_pRTPowerCallbackHead; ; pCur = pCur->pNext)
            if (    pCur->pvUser == pvUser
                &&  pCur->pfnCallback == pfnCallback)
                break;
            else if (!pCur->pNext)
            {
                pCur->pNext = pNew;
                pCur = NULL;
                break;
            }
    }

    ASMAtomicIncU32(&g_iRTPowerGeneration);

    RTSpinlockRelease(g_hRTPowerNotifySpinLock);

    /* duplicate? */
    if (pCur)
    {
        RTMemFree(pCur);
        AssertMsgFailedReturn(("pCur=%p pfnCallback=%p pvUser=%p\n", pCur, pfnCallback, pvUser), VERR_ALREADY_EXISTS);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTPowerNotificationRegister);


RTDECL(int) RTPowerNotificationDeregister(PFNRTPOWERNOTIFICATION pfnCallback, void *pvUser)
{
    PRTPOWERNOTIFYREG   pPrev;
    PRTPOWERNOTIFYREG   pCur;

    /*
     * Validation.
     */
    AssertPtrReturn(pfnCallback, VERR_INVALID_POINTER);
    AssertReturn(g_hRTPowerNotifySpinLock != NIL_RTSPINLOCK, VERR_WRONG_ORDER);
    RT_ASSERT_INTS_ON();

    /*
     * Find and unlink the record from the list.
     */
    RTSpinlockAcquire(g_hRTPowerNotifySpinLock);
    pPrev = NULL;
    for (pCur = g_pRTPowerCallbackHead; pCur; pCur = pCur->pNext)
    {
        if (    pCur->pvUser == pvUser
            &&  pCur->pfnCallback == pfnCallback)
            break;
        pPrev = pCur;
    }
    if (pCur)
    {
        if (pPrev)
            pPrev->pNext = pCur->pNext;
        else
            g_pRTPowerCallbackHead = pCur->pNext;
        ASMAtomicIncU32(&g_iRTPowerGeneration);
    }
    RTSpinlockRelease(g_hRTPowerNotifySpinLock);

    if (!pCur)
        return VERR_NOT_FOUND;

    /*
     * Invalidate and free the record.
     */
    pCur->pNext = NULL;
    pCur->pfnCallback = NULL;
    RTMemFree(pCur);

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTPowerNotificationDeregister);


DECLHIDDEN(int) rtR0PowerNotificationInit(void)
{
    int rc = RTSpinlockCreate((PRTSPINLOCK)&g_hRTPowerNotifySpinLock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "RTR0Power");
    if (RT_SUCCESS(rc))
    {
        /** @todo OS specific init here */
        return rc;
#if 0
        RTSpinlockDestroy(g_hRTPowerNotifySpinLock);
        g_hRTPowerNotifySpinLock = NIL_RTSPINLOCK;
#endif
    }
    return rc;
}


DECLHIDDEN(void) rtR0PowerNotificationTerm(void)
{
    PRTPOWERNOTIFYREG   pHead;
    RTSPINLOCK          hSpinlock = g_hRTPowerNotifySpinLock;
    AssertReturnVoid(hSpinlock != NIL_RTSPINLOCK);

    /** @todo OS specific term here */

    /* pick up the list and the spinlock. */
    RTSpinlockAcquire(hSpinlock);
    ASMAtomicWriteHandle(&g_hRTPowerNotifySpinLock, NIL_RTSPINLOCK);
    pHead = g_pRTPowerCallbackHead;
    g_pRTPowerCallbackHead = NULL;
    ASMAtomicIncU32(&g_iRTPowerGeneration);
    RTSpinlockRelease(hSpinlock);

    /* free the list. */
    while (pHead)
    {
        PRTPOWERNOTIFYREG pFree = pHead;
        pHead = pHead->pNext;

        pFree->pNext = NULL;
        pFree->pfnCallback = NULL;
        RTMemFree(pFree);
    }

    RTSpinlockDestroy(hSpinlock);
}

