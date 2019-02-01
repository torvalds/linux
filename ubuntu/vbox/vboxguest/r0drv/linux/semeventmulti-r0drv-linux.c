/* $Id: semeventmulti-r0drv-linux.c $ */
/** @file
 * IPRT - Multiple Release Event Semaphores, Ring-0 Driver, Linux.
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
#define RTSEMEVENTMULTI_WITHOUT_REMAPPING
#include "the-linux-kernel.h"
#include "internal/iprt.h"
#include <iprt/semaphore.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/lockvalidator.h>

#include "waitqueue-r0drv-linux.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name fStateAndGen values
 * @{ */
/** The state bit number. */
#define RTSEMEVENTMULTILNX_STATE_BIT        0
/** The state mask. */
#define RTSEMEVENTMULTILNX_STATE_MASK       RT_BIT_32(RTSEMEVENTMULTILNX_STATE_BIT)
/** The generation mask. */
#define RTSEMEVENTMULTILNX_GEN_MASK         ~RTSEMEVENTMULTILNX_STATE_MASK
/** The generation shift. */
#define RTSEMEVENTMULTILNX_GEN_SHIFT        1
/** The initial variable value. */
#define RTSEMEVENTMULTILNX_STATE_GEN_INIT   UINT32_C(0xfffffffc)
/** @}  */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Linux event semaphore.
 */
typedef struct RTSEMEVENTMULTIINTERNAL
{
    /** Magic value (RTSEMEVENTMULTI_MAGIC). */
    uint32_t volatile   u32Magic;
    /** The object state bit and generation counter.
     * The generation counter is incremented every time the object is
     * signalled. */
    uint32_t volatile   fStateAndGen;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** The wait queue. */
    wait_queue_head_t   Head;
} RTSEMEVENTMULTIINTERNAL, *PRTSEMEVENTMULTIINTERNAL;





RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    PRTSEMEVENTMULTIINTERNAL pThis;
    IPRT_LINUX_SAVE_EFL_AC();
    RT_NOREF_PV(hClass); RT_NOREF_PV(pszNameFmt);

    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);
    pThis = (PRTSEMEVENTMULTIINTERNAL)RTMemAlloc(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic     = RTSEMEVENTMULTI_MAGIC;
        pThis->fStateAndGen = RTSEMEVENTMULTILNX_STATE_GEN_INIT;
        pThis->cRefs        = 1;
        init_waitqueue_head(&pThis->Head);

        *phEventMultiSem = pThis;
        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
    IPRT_LINUX_RESTORE_EFL_AC();
    return VERR_NO_MEMORY;
}
RT_EXPORT_SYMBOL(RTSemEventMultiCreate);


/**
 * Retain a reference to the semaphore.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventMultiLnxRetain(PRTSEMEVENTMULTIINTERNAL pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    NOREF(cRefs);
    Assert(cRefs && cRefs < 100000);
}


/**
 * Release a reference, destroy the thing if necessary.
 *
 * @param   pThis       The semaphore.
 */
DECLINLINE(void) rtR0SemEventMultiLnxRelease(PRTSEMEVENTMULTIINTERNAL pThis)
{
    if (RT_UNLIKELY(ASMAtomicDecU32(&pThis->cRefs) == 0))
    {
        Assert(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC);
        RTMemFree(pThis);
    }
}


RTDECL(int) RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (pThis == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    Assert(pThis->cRefs > 0);

    /*
     * Invalidate it and signal the object just in case.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTSEMEVENTMULTI_MAGIC);
    ASMAtomicAndU32(&pThis->fStateAndGen, RTSEMEVENTMULTILNX_GEN_MASK);
    Assert(!waitqueue_active(&pThis->Head));
    wake_up_all(&pThis->Head);
    rtR0SemEventMultiLnxRelease(pThis);

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemEventMultiDestroy);


RTDECL(int) RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    IPRT_LINUX_SAVE_EFL_AC();
    uint32_t fNew;
    uint32_t fOld;

    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiLnxRetain(pThis);

    /*
     * Signal the event object.  The cause of the paranoia here is racing to try
     * deal with racing RTSemEventMultiSignal calls (should probably be
     * forbidden, but it's relatively easy to handle).
     */
    do
    {
        fNew = fOld = ASMAtomicUoReadU32(&pThis->fStateAndGen);
        fNew += 1 << RTSEMEVENTMULTILNX_GEN_SHIFT;
        fNew |= RTSEMEVENTMULTILNX_STATE_MASK;
    }
    while (!ASMAtomicCmpXchgU32(&pThis->fStateAndGen, fNew, fOld));

    wake_up_all(&pThis->Head);

    rtR0SemEventMultiLnxRelease(pThis);
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemEventMultiSignal);


RTDECL(int) RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    PRTSEMEVENTMULTIINTERNAL pThis = (PRTSEMEVENTMULTIINTERNAL)hEventMultiSem;
    if (!pThis)
        return VERR_INVALID_PARAMETER;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiLnxRetain(pThis);

    /*
     * Reset it.
     */
    ASMAtomicAndU32(&pThis->fStateAndGen, ~RTSEMEVENTMULTILNX_STATE_MASK);

    rtR0SemEventMultiLnxRelease(pThis);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTSemEventMultiReset);


/**
 * Worker for RTSemEventMultiWaitEx and RTSemEventMultiWaitExDebug.
 *
 * @returns VBox status code.
 * @param   pThis           The event semaphore.
 * @param   fFlags          See RTSemEventMultiWaitEx.
 * @param   uTimeout        See RTSemEventMultiWaitEx.
 * @param   pSrcPos         The source code position of the wait.
 */
static int rtR0SemEventMultiLnxWait(PRTSEMEVENTMULTIINTERNAL pThis, uint32_t fFlags, uint64_t uTimeout,
                                    PCRTLOCKVALSRCPOS pSrcPos)
{
    uint32_t    fOrgStateAndGen;
    int         rc;
    RT_NOREF_PV(pSrcPos);

    /*
     * Validate the input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pThis->u32Magic == RTSEMEVENTMULTI_MAGIC, ("%p u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_PARAMETER);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);
    rtR0SemEventMultiLnxRetain(pThis);

    /*
     * Is the event already signalled or do we have to wait?
     */
    fOrgStateAndGen = ASMAtomicUoReadU32(&pThis->fStateAndGen);
    if (fOrgStateAndGen & RTSEMEVENTMULTILNX_STATE_MASK)
        rc = VINF_SUCCESS;
    else
    {
        /*
         * We have to wait.
         */
        RTR0SEMLNXWAIT Wait;
        IPRT_LINUX_SAVE_EFL_AC();
        rc = rtR0SemLnxWaitInit(&Wait, fFlags, uTimeout, &pThis->Head);
        if (RT_SUCCESS(rc))
        {
            IPRT_DEBUG_SEMS_STATE(pThis, 'E');
            for (;;)
            {
                /* The destruction test. */
                if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC))
                    rc = VERR_SEM_DESTROYED;
                else
                {
                    rtR0SemLnxWaitPrepare(&Wait);

                    /* Check the exit conditions. */
                    if (RT_UNLIKELY(pThis->u32Magic != RTSEMEVENTMULTI_MAGIC))
                        rc = VERR_SEM_DESTROYED;
                    else if (ASMAtomicUoReadU32(&pThis->fStateAndGen) != fOrgStateAndGen)
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

    rtR0SemEventMultiLnxRelease(pThis);
    return rc;
}


RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtR0SemEventMultiLnxWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtR0SemEventMultiLnxWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTSemEventMultiWaitEx);


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtR0SemEventMultiLnxWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}
RT_EXPORT_SYMBOL(RTSemEventMultiWaitExDebug);


RTDECL(uint32_t) RTSemEventMultiGetResolution(void)
{
    return rtR0SemLnxWaitGetResolution();
}
RT_EXPORT_SYMBOL(RTSemEventMultiGetResolution);

