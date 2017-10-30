/* $Id: alloc-r0drv.cpp $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define RTMEM_NO_WRAP_TO_EF_APIS
#include <iprt/mem.h>
#include "internal/iprt.h"

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#ifdef RT_MORE_STRICT
# include <iprt/mp.h>
#endif
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include "r0drv/alloc-r0drv.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_STRICT
# define RTR0MEM_STRICT
#endif

#ifdef RTR0MEM_STRICT
# define RTR0MEM_FENCE_EXTRA    16
#else
# define RTR0MEM_FENCE_EXTRA    0
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef RTR0MEM_STRICT
/** Fence data. */
static uint8_t const g_abFence[RTR0MEM_FENCE_EXTRA] =
{
    0x77, 0x88, 0x66, 0x99,  0x55, 0xaa, 0x44, 0xbb,
    0x33, 0xcc, 0x22, 0xdd,  0x11, 0xee, 0x00, 0xff
};
#endif


/**
 * Wrapper around rtR0MemAllocEx.
 *
 * @returns Pointer to the allocated memory block header.
 * @param   cb                  The number of bytes to allocate (sans header).
 * @param   fFlags              The allocation flags.
 */
DECLINLINE(PRTMEMHDR) rtR0MemAlloc(size_t cb, uint32_t fFlags)
{
    PRTMEMHDR pHdr;
    int rc = rtR0MemAllocEx(cb, fFlags, &pHdr);
    if (RT_FAILURE(rc))
        return NULL;
    return pHdr;
}


RTDECL(void *)  RTMemTmpAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return RTMemAllocTag(cb, pszTag);
}
RT_EXPORT_SYMBOL(RTMemTmpAllocTag);


RTDECL(void *)  RTMemTmpAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return RTMemAllocZTag(cb, pszTag);
}
RT_EXPORT_SYMBOL(RTMemTmpAllocZTag);


RTDECL(void)    RTMemTmpFree(void *pv) RT_NO_THROW_DEF
{
    return RTMemFree(pv);
}
RT_EXPORT_SYMBOL(RTMemTmpFree);





RTDECL(void *)  RTMemAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    PRTMEMHDR pHdr;
    RT_ASSERT_INTS_ON();
    RT_NOREF_PV(pszTag);

    pHdr = rtR0MemAlloc(cb + RTR0MEM_FENCE_EXTRA, 0);
    if (pHdr)
    {
#ifdef RTR0MEM_STRICT
        pHdr->cbReq = (uint32_t)cb; Assert(pHdr->cbReq == cb);
        memcpy((uint8_t *)(pHdr + 1) + cb, &g_abFence[0], RTR0MEM_FENCE_EXTRA);
#endif
        return pHdr + 1;
    }
    return NULL;
}
RT_EXPORT_SYMBOL(RTMemAllocTag);


RTDECL(void *)  RTMemAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    PRTMEMHDR pHdr;
    RT_ASSERT_INTS_ON();
    RT_NOREF_PV(pszTag);

    pHdr = rtR0MemAlloc(cb + RTR0MEM_FENCE_EXTRA, RTMEMHDR_FLAG_ZEROED);
    if (pHdr)
    {
#ifdef RTR0MEM_STRICT
        pHdr->cbReq = (uint32_t)cb; Assert(pHdr->cbReq == cb);
        memcpy((uint8_t *)(pHdr + 1) + cb, &g_abFence[0], RTR0MEM_FENCE_EXTRA);
        return memset(pHdr + 1, 0, cb);
#else
        return memset(pHdr + 1, 0, pHdr->cb);
#endif
    }
    return NULL;
}
RT_EXPORT_SYMBOL(RTMemAllocZTag);


RTDECL(void *) RTMemAllocVarTag(size_t cbUnaligned, const char *pszTag)
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return RTMemAllocTag(cbAligned, pszTag);
}
RT_EXPORT_SYMBOL(RTMemAllocVarTag);


RTDECL(void *) RTMemAllocZVarTag(size_t cbUnaligned, const char *pszTag)
{
    size_t cbAligned;
    if (cbUnaligned >= 16)
        cbAligned = RT_ALIGN_Z(cbUnaligned, 16);
    else
        cbAligned = RT_ALIGN_Z(cbUnaligned, sizeof(void *));
    return RTMemAllocZTag(cbAligned, pszTag);
}
RT_EXPORT_SYMBOL(RTMemAllocZVarTag);


RTDECL(void *) RTMemReallocTag(void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_DEF
{
    PRTMEMHDR pHdrOld;

    /* Free. */
    if (!cbNew && pvOld)
    {
        RTMemFree(pvOld);
        return NULL;
    }

    /* Alloc. */
    if (!pvOld)
        return RTMemAllocTag(cbNew, pszTag);

    /*
     * Realloc.
     */
    pHdrOld = (PRTMEMHDR)pvOld - 1;
    RT_ASSERT_PREEMPTIBLE();

    if (pHdrOld->u32Magic == RTMEMHDR_MAGIC)
    {
        PRTMEMHDR pHdrNew;

        /* If there is sufficient space in the old block and we don't cause
           substantial internal fragmentation, reuse the old block. */
        if (   pHdrOld->cb >= cbNew + RTR0MEM_FENCE_EXTRA
            && pHdrOld->cb - (cbNew + RTR0MEM_FENCE_EXTRA) <= 128)
        {
            pHdrOld->cbReq = (uint32_t)cbNew; Assert(pHdrOld->cbReq == cbNew);
#ifdef RTR0MEM_STRICT
            memcpy((uint8_t *)(pHdrOld + 1) + cbNew, &g_abFence[0], RTR0MEM_FENCE_EXTRA);
#endif
            return pvOld;
        }

        /* Allocate a new block and copy over the content. */
        pHdrNew = rtR0MemAlloc(cbNew + RTR0MEM_FENCE_EXTRA, 0);
        if (pHdrNew)
        {
            size_t cbCopy = RT_MIN(pHdrOld->cb, pHdrNew->cb);
            memcpy(pHdrNew + 1, pvOld, cbCopy);
#ifdef RTR0MEM_STRICT
            pHdrNew->cbReq = (uint32_t)cbNew; Assert(pHdrNew->cbReq == cbNew);
            memcpy((uint8_t *)(pHdrNew + 1) + cbNew, &g_abFence[0], RTR0MEM_FENCE_EXTRA);
            AssertReleaseMsg(!memcmp((uint8_t *)(pHdrOld + 1) + pHdrOld->cbReq, &g_abFence[0], RTR0MEM_FENCE_EXTRA),
                             ("pHdr=%p pvOld=%p cbReq=%u cb=%u cbNew=%zu fFlags=%#x\n"
                              "fence:    %.*Rhxs\n"
                              "expected: %.*Rhxs\n",
                              pHdrOld, pvOld, pHdrOld->cbReq, pHdrOld->cb, cbNew, pHdrOld->fFlags,
                              RTR0MEM_FENCE_EXTRA, (uint8_t *)(pHdrOld + 1) + pHdrOld->cbReq,
                              RTR0MEM_FENCE_EXTRA, &g_abFence[0]));
#endif
            rtR0MemFree(pHdrOld);
            return pHdrNew + 1;
        }
    }
    else
        AssertMsgFailed(("pHdrOld->u32Magic=%RX32 pvOld=%p cbNew=%#zx\n", pHdrOld->u32Magic, pvOld, cbNew));

    return NULL;
}
RT_EXPORT_SYMBOL(RTMemReallocTag);


RTDECL(void) RTMemFree(void *pv) RT_NO_THROW_DEF
{
    PRTMEMHDR pHdr;
    RT_ASSERT_INTS_ON();

    if (!pv)
        return;
    pHdr = (PRTMEMHDR)pv - 1;
    if (pHdr->u32Magic == RTMEMHDR_MAGIC)
    {
        Assert(!(pHdr->fFlags & RTMEMHDR_FLAG_ALLOC_EX));
        Assert(!(pHdr->fFlags & RTMEMHDR_FLAG_EXEC));
#ifdef RTR0MEM_STRICT
        AssertReleaseMsg(!memcmp((uint8_t *)(pHdr + 1) + pHdr->cbReq, &g_abFence[0], RTR0MEM_FENCE_EXTRA),
                         ("pHdr=%p pv=%p cbReq=%u cb=%u fFlags=%#x\n"
                          "fence:    %.*Rhxs\n"
                          "expected: %.*Rhxs\n",
                          pHdr, pv, pHdr->cbReq, pHdr->cb, pHdr->fFlags,
                          RTR0MEM_FENCE_EXTRA, (uint8_t *)(pHdr + 1) + pHdr->cbReq,
                          RTR0MEM_FENCE_EXTRA, &g_abFence[0]));
#endif
        rtR0MemFree(pHdr);
    }
    else
        AssertMsgFailed(("pHdr->u32Magic=%RX32 pv=%p\n", pHdr->u32Magic, pv));
}
RT_EXPORT_SYMBOL(RTMemFree);






RTDECL(void *)    RTMemExecAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    PRTMEMHDR pHdr;
#ifdef RT_OS_SOLARIS /** @todo figure out why */
    RT_ASSERT_INTS_ON();
#else
    RT_ASSERT_PREEMPTIBLE();
#endif
    RT_NOREF_PV(pszTag);


    pHdr = rtR0MemAlloc(cb + RTR0MEM_FENCE_EXTRA, RTMEMHDR_FLAG_EXEC);
    if (pHdr)
    {
#ifdef RTR0MEM_STRICT
        pHdr->cbReq = (uint32_t)cb; Assert(pHdr->cbReq == cb);
        memcpy((uint8_t *)(pHdr + 1) + cb, &g_abFence[0], RTR0MEM_FENCE_EXTRA);
#endif
        return pHdr + 1;
    }
    return NULL;
}
RT_EXPORT_SYMBOL(RTMemExecAllocTag);


RTDECL(void)      RTMemExecFree(void *pv, size_t cb) RT_NO_THROW_DEF
{
    PRTMEMHDR pHdr;
    RT_ASSERT_INTS_ON();
    RT_NOREF_PV(cb);

    if (!pv)
        return;
    pHdr = (PRTMEMHDR)pv - 1;
    if (pHdr->u32Magic == RTMEMHDR_MAGIC)
    {
        Assert(!(pHdr->fFlags & RTMEMHDR_FLAG_ALLOC_EX));
#ifdef RTR0MEM_STRICT
        AssertReleaseMsg(!memcmp((uint8_t *)(pHdr + 1) + pHdr->cbReq, &g_abFence[0], RTR0MEM_FENCE_EXTRA),
                         ("pHdr=%p pv=%p cbReq=%u cb=%u fFlags=%#x\n"
                          "fence:    %.*Rhxs\n"
                          "expected: %.*Rhxs\n",
                          pHdr, pv, pHdr->cbReq, pHdr->cb, pHdr->fFlags,
                          RTR0MEM_FENCE_EXTRA, (uint8_t *)(pHdr + 1) + pHdr->cbReq,
                          RTR0MEM_FENCE_EXTRA, &g_abFence[0]));
#endif
        rtR0MemFree(pHdr);
    }
    else
        AssertMsgFailed(("pHdr->u32Magic=%RX32 pv=%p\n", pHdr->u32Magic, pv));
}
RT_EXPORT_SYMBOL(RTMemExecFree);




RTDECL(int) RTMemAllocExTag(size_t cb, size_t cbAlignment, uint32_t fFlags, const char *pszTag, void **ppv) RT_NO_THROW_DEF
{
    uint32_t    fHdrFlags = RTMEMHDR_FLAG_ALLOC_EX;
    PRTMEMHDR   pHdr;
    int         rc;
    RT_NOREF_PV(pszTag);

    RT_ASSERT_PREEMPT_CPUID_VAR();
    if (!(fFlags & RTMEMALLOCEX_FLAGS_ANY_CTX_ALLOC))
        RT_ASSERT_INTS_ON();

    /*
     * Fake up some alignment support.
     */
    AssertMsgReturn(cbAlignment <= sizeof(void *), ("%zu (%#x)\n", cbAlignment, cbAlignment), VERR_UNSUPPORTED_ALIGNMENT);
    if (cb < cbAlignment)
        cb = cbAlignment;

    /*
     * Validate and convert flags.
     */
    AssertMsgReturn(!(fFlags & ~RTMEMALLOCEX_FLAGS_VALID_MASK_R0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    if (fFlags & RTMEMALLOCEX_FLAGS_ZEROED)
        fHdrFlags |= RTMEMHDR_FLAG_ZEROED;
    if (fFlags & RTMEMALLOCEX_FLAGS_EXEC)
        fHdrFlags |= RTMEMHDR_FLAG_EXEC;
    if (fFlags & RTMEMALLOCEX_FLAGS_ANY_CTX_ALLOC)
        fHdrFlags |= RTMEMHDR_FLAG_ANY_CTX_ALLOC;
    if (fFlags & RTMEMALLOCEX_FLAGS_ANY_CTX_FREE)
        fHdrFlags |= RTMEMHDR_FLAG_ANY_CTX_FREE;

    /*
     * Do the allocation.
     */
    rc = rtR0MemAllocEx(cb + RTR0MEM_FENCE_EXTRA, fHdrFlags, &pHdr);
    if (RT_SUCCESS(rc))
    {
        void *pv;

        Assert(pHdr->cbReq  == cb + RTR0MEM_FENCE_EXTRA);
        Assert((pHdr->fFlags & fFlags) == fFlags);

        /*
         * Calc user pointer, initialize the memory if requested, and if
         * memory strictness is enable set up the fence.
         */
        pv = pHdr + 1;
        *ppv = pv;
        if (fFlags & RTMEMHDR_FLAG_ZEROED)
            memset(pv, 0, pHdr->cb);

#ifdef RTR0MEM_STRICT
        pHdr->cbReq = (uint32_t)cb;
        memcpy((uint8_t *)pv + cb, &g_abFence[0], RTR0MEM_FENCE_EXTRA);
#endif
    }
    else if (rc == VERR_NO_MEMORY && (fFlags & RTMEMALLOCEX_FLAGS_EXEC))
        rc = VERR_NO_EXEC_MEMORY;

    RT_ASSERT_PREEMPT_CPUID();
    return rc;
}
RT_EXPORT_SYMBOL(RTMemAllocExTag);


RTDECL(void) RTMemFreeEx(void *pv, size_t cb) RT_NO_THROW_DEF
{
    PRTMEMHDR pHdr;
    RT_NOREF_PV(cb);

    if (!pv)
        return;

    AssertPtr(pv);
    pHdr = (PRTMEMHDR)pv - 1;
    if (pHdr->u32Magic == RTMEMHDR_MAGIC)
    {
        RT_ASSERT_PREEMPT_CPUID_VAR();

        Assert(pHdr->fFlags & RTMEMHDR_FLAG_ALLOC_EX);
        if (!(pHdr->fFlags & RTMEMHDR_FLAG_ANY_CTX_FREE))
            RT_ASSERT_INTS_ON();
        AssertMsg(pHdr->cbReq == cb, ("cbReq=%zu cb=%zu\n", pHdr->cb, cb));

#ifdef RTR0MEM_STRICT
        AssertReleaseMsg(!memcmp((uint8_t *)(pHdr + 1) + pHdr->cbReq, &g_abFence[0], RTR0MEM_FENCE_EXTRA),
                         ("pHdr=%p pv=%p cbReq=%u cb=%u fFlags=%#x\n"
                          "fence:    %.*Rhxs\n"
                          "expected: %.*Rhxs\n",
                          pHdr, pv, pHdr->cbReq, pHdr->cb, pHdr->fFlags,
                          RTR0MEM_FENCE_EXTRA, (uint8_t *)(pHdr + 1) + pHdr->cbReq,
                          RTR0MEM_FENCE_EXTRA, &g_abFence[0]));
#endif
        rtR0MemFree(pHdr);
        RT_ASSERT_PREEMPT_CPUID();
    }
    else
        AssertMsgFailed(("pHdr->u32Magic=%RX32 pv=%p\n", pHdr->u32Magic, pv));
}
RT_EXPORT_SYMBOL(RTMemFreeEx);

