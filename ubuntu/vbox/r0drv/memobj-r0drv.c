/* $Id: memobj-r0drv.cpp $ */
/** @file
 * IPRT - Ring-0 Memory Objects, Common Code.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DEFAULT /// @todo RTLOGGROUP_MEM
#define RTMEM_NO_WRAP_TO_EF_APIS /* circular dependency otherwise. */
#include <iprt/memobj.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mp.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <iprt/thread.h>

#include "internal/memobj.h"


/**
 * Internal function for allocating a new memory object.
 *
 * @returns The allocated and initialized handle.
 * @param   cbSelf      The size of the memory object handle. 0 mean default size.
 * @param   enmType     The memory object type.
 * @param   pv          The memory object mapping.
 * @param   cb          The size of the memory object.
 */
DECLHIDDEN(PRTR0MEMOBJINTERNAL) rtR0MemObjNew(size_t cbSelf, RTR0MEMOBJTYPE enmType, void *pv, size_t cb)
{
    PRTR0MEMOBJINTERNAL pNew;

    /* validate the size */
    if (!cbSelf)
        cbSelf = sizeof(*pNew);
    Assert(cbSelf >= sizeof(*pNew));
    Assert(cbSelf == (uint32_t)cbSelf);
    AssertMsg(RT_ALIGN_Z(cb, PAGE_SIZE) == cb, ("%#zx\n", cb));

    /*
     * Allocate and initialize the object.
     */
    pNew = (PRTR0MEMOBJINTERNAL)RTMemAllocZ(cbSelf);
    if (pNew)
    {
        pNew->u32Magic  = RTR0MEMOBJ_MAGIC;
        pNew->cbSelf    = (uint32_t)cbSelf;
        pNew->enmType   = enmType;
        pNew->fFlags    = 0;
        pNew->cb        = cb;
        pNew->pv        = pv;
    }
    return pNew;
}


/**
 * Deletes an incomplete memory object.
 *
 * This is for cleaning up after failures during object creation.
 *
 * @param   pMem    The incomplete memory object to delete.
 */
DECLHIDDEN(void) rtR0MemObjDelete(PRTR0MEMOBJINTERNAL pMem)
{
    if (pMem)
    {
        ASMAtomicUoWriteU32(&pMem->u32Magic, ~RTR0MEMOBJ_MAGIC);
        pMem->enmType = RTR0MEMOBJTYPE_END;
        RTMemFree(pMem);
    }
}


/**
 * Links a mapping object to a primary object.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_NO_MEMORY if we couldn't expand the mapping array of the parent.
 * @param   pParent     The parent (primary) memory object.
 * @param   pChild      The child (mapping) memory object.
 */
static int rtR0MemObjLink(PRTR0MEMOBJINTERNAL pParent, PRTR0MEMOBJINTERNAL pChild)
{
    uint32_t i;

    /* sanity */
    Assert(rtR0MemObjIsMapping(pChild));
    Assert(!rtR0MemObjIsMapping(pParent));

    /* expand the array? */
    i = pParent->uRel.Parent.cMappings;
    if (i >= pParent->uRel.Parent.cMappingsAllocated)
    {
        void *pv = RTMemRealloc(pParent->uRel.Parent.papMappings,
                                (i + 32) * sizeof(pParent->uRel.Parent.papMappings[0]));
        if (!pv)
            return VERR_NO_MEMORY;
        pParent->uRel.Parent.papMappings = (PPRTR0MEMOBJINTERNAL)pv;
        pParent->uRel.Parent.cMappingsAllocated = i + 32;
        Assert(i == pParent->uRel.Parent.cMappings);
    }

    /* do the linking. */
    pParent->uRel.Parent.papMappings[i] = pChild;
    pParent->uRel.Parent.cMappings++;
    pChild->uRel.Child.pParent = pParent;

    return VINF_SUCCESS;
}


/**
 * Checks if this is mapping or not.
 *
 * @returns true if it's a mapping, otherwise false.
 * @param   MemObj      The ring-0 memory object handle.
 */
RTR0DECL(bool) RTR0MemObjIsMapping(RTR0MEMOBJ MemObj)
{
    /* Validate the object handle. */
    PRTR0MEMOBJINTERNAL pMem;
    AssertPtrReturn(MemObj, false);
    pMem = (PRTR0MEMOBJINTERNAL)MemObj;
    AssertMsgReturn(pMem->u32Magic == RTR0MEMOBJ_MAGIC, ("%p: %#x\n", pMem, pMem->u32Magic), false);
    AssertMsgReturn(pMem->enmType > RTR0MEMOBJTYPE_INVALID && pMem->enmType < RTR0MEMOBJTYPE_END, ("%p: %d\n", pMem, pMem->enmType), false);

    /* hand it on to the inlined worker. */
    return rtR0MemObjIsMapping(pMem);
}
RT_EXPORT_SYMBOL(RTR0MemObjIsMapping);


/**
 * Gets the address of a ring-0 memory object.
 *
 * @returns The address of the memory object.
 * @returns NULL if the handle is invalid (asserts in strict builds) or if there isn't any mapping.
 * @param   MemObj  The ring-0 memory object handle.
 */
RTR0DECL(void *) RTR0MemObjAddress(RTR0MEMOBJ MemObj)
{
    /* Validate the object handle. */
    PRTR0MEMOBJINTERNAL pMem;
    if (RT_UNLIKELY(MemObj == NIL_RTR0MEMOBJ))
        return NULL;
    AssertPtrReturn(MemObj, NULL);
    pMem = (PRTR0MEMOBJINTERNAL)MemObj;
    AssertMsgReturn(pMem->u32Magic == RTR0MEMOBJ_MAGIC, ("%p: %#x\n", pMem, pMem->u32Magic), NULL);
    AssertMsgReturn(pMem->enmType > RTR0MEMOBJTYPE_INVALID && pMem->enmType < RTR0MEMOBJTYPE_END, ("%p: %d\n", pMem, pMem->enmType), NULL);

    /* return the mapping address. */
    return pMem->pv;
}
RT_EXPORT_SYMBOL(RTR0MemObjAddress);


/**
 * Gets the ring-3 address of a ring-0 memory object.
 *
 * This only applies to ring-0 memory object with ring-3 mappings of some kind, i.e.
 * locked user memory, reserved user address space and user mappings. This API should
 * not be used on any other objects.
 *
 * @returns The address of the memory object.
 * @returns NIL_RTR3PTR if the handle is invalid or if it's not an object with a ring-3 mapping.
 *          Strict builds will assert in both cases.
 * @param   MemObj  The ring-0 memory object handle.
 */
RTR0DECL(RTR3PTR) RTR0MemObjAddressR3(RTR0MEMOBJ MemObj)
{
    PRTR0MEMOBJINTERNAL pMem;

    /* Validate the object handle. */
    if (RT_UNLIKELY(MemObj == NIL_RTR0MEMOBJ))
        return NIL_RTR3PTR;
    AssertPtrReturn(MemObj, NIL_RTR3PTR);
    pMem = (PRTR0MEMOBJINTERNAL)MemObj;
    AssertMsgReturn(pMem->u32Magic == RTR0MEMOBJ_MAGIC, ("%p: %#x\n", pMem, pMem->u32Magic), NIL_RTR3PTR);
    AssertMsgReturn(pMem->enmType > RTR0MEMOBJTYPE_INVALID && pMem->enmType < RTR0MEMOBJTYPE_END, ("%p: %d\n", pMem, pMem->enmType), NIL_RTR3PTR);
    if (RT_UNLIKELY(    (   pMem->enmType != RTR0MEMOBJTYPE_MAPPING
                         || pMem->u.Mapping.R0Process == NIL_RTR0PROCESS)
                    &&  (   pMem->enmType != RTR0MEMOBJTYPE_LOCK
                         || pMem->u.Lock.R0Process == NIL_RTR0PROCESS)
                    &&  (   pMem->enmType != RTR0MEMOBJTYPE_PHYS_NC
                         || pMem->u.Lock.R0Process == NIL_RTR0PROCESS)
                    &&  (   pMem->enmType != RTR0MEMOBJTYPE_RES_VIRT
                         || pMem->u.ResVirt.R0Process == NIL_RTR0PROCESS)))
        return NIL_RTR3PTR;

    /* return the mapping address. */
    return (RTR3PTR)pMem->pv;
}
RT_EXPORT_SYMBOL(RTR0MemObjAddressR3);


/**
 * Gets the size of a ring-0 memory object.
 *
 * The returned value may differ from the one specified to the API creating the
 * object because of alignment adjustments.  The minimal alignment currently
 * employed by any API is PAGE_SIZE, so the result can safely be shifted by
 * PAGE_SHIFT to calculate a page count.
 *
 * @returns The object size.
 * @returns 0 if the handle is invalid (asserts in strict builds) or if there isn't any mapping.
 * @param   MemObj  The ring-0 memory object handle.
 */
RTR0DECL(size_t) RTR0MemObjSize(RTR0MEMOBJ MemObj)
{
    PRTR0MEMOBJINTERNAL pMem;

    /* Validate the object handle. */
    if (RT_UNLIKELY(MemObj == NIL_RTR0MEMOBJ))
        return 0;
    AssertPtrReturn(MemObj, 0);
    pMem = (PRTR0MEMOBJINTERNAL)MemObj;
    AssertMsgReturn(pMem->u32Magic == RTR0MEMOBJ_MAGIC, ("%p: %#x\n", pMem, pMem->u32Magic), 0);
    AssertMsgReturn(pMem->enmType > RTR0MEMOBJTYPE_INVALID && pMem->enmType < RTR0MEMOBJTYPE_END, ("%p: %d\n", pMem, pMem->enmType), 0);
    AssertMsg(RT_ALIGN_Z(pMem->cb, PAGE_SIZE) == pMem->cb, ("%#zx\n", pMem->cb));

    /* return the size. */
    return pMem->cb;
}
RT_EXPORT_SYMBOL(RTR0MemObjSize);


/**
 * Get the physical address of an page in the memory object.
 *
 * @returns The physical address.
 * @returns NIL_RTHCPHYS if the object doesn't contain fixed physical pages.
 * @returns NIL_RTHCPHYS if the iPage is out of range.
 * @returns NIL_RTHCPHYS if the object handle isn't valid.
 * @param   MemObj  The ring-0 memory object handle.
 * @param   iPage   The page number within the object.
 */
/* Work around gcc bug 55940 */
#if defined(__GNUC__) && defined(RT_ARCH_X86) && (__GNUC__ * 100 + __GNUC_MINOR__) == 407
 __attribute__((__optimize__ ("no-shrink-wrap")))
#endif
RTR0DECL(RTHCPHYS) RTR0MemObjGetPagePhysAddr(RTR0MEMOBJ MemObj, size_t iPage)
{
    /* Validate the object handle. */
    PRTR0MEMOBJINTERNAL pMem;
    size_t cPages;
    AssertPtrReturn(MemObj, NIL_RTHCPHYS);
    pMem = (PRTR0MEMOBJINTERNAL)MemObj;
    AssertReturn(pMem->u32Magic == RTR0MEMOBJ_MAGIC, NIL_RTHCPHYS);
    AssertReturn(pMem->enmType > RTR0MEMOBJTYPE_INVALID && pMem->enmType < RTR0MEMOBJTYPE_END, NIL_RTHCPHYS);
    AssertMsgReturn(pMem->u32Magic == RTR0MEMOBJ_MAGIC, ("%p: %#x\n", pMem, pMem->u32Magic), NIL_RTHCPHYS);
    AssertMsgReturn(pMem->enmType > RTR0MEMOBJTYPE_INVALID && pMem->enmType < RTR0MEMOBJTYPE_END, ("%p: %d\n", pMem, pMem->enmType), NIL_RTHCPHYS);
    cPages = (pMem->cb >> PAGE_SHIFT);
    if (iPage >= cPages)
    {
        /* permit: while (RTR0MemObjGetPagePhysAddr(pMem, iPage++) != NIL_RTHCPHYS) {} */
        if (iPage == cPages)
            return NIL_RTHCPHYS;
        AssertReturn(iPage < (pMem->cb >> PAGE_SHIFT), NIL_RTHCPHYS);
    }

    /*
     * We know the address of physically contiguous allocations and mappings.
     */
    if (pMem->enmType == RTR0MEMOBJTYPE_CONT)
        return pMem->u.Cont.Phys + iPage * PAGE_SIZE;
    if (pMem->enmType == RTR0MEMOBJTYPE_PHYS)
        return pMem->u.Phys.PhysBase + iPage * PAGE_SIZE;

    /*
     * Do the job.
     */
    return rtR0MemObjNativeGetPagePhysAddr(pMem, iPage);
}
RT_EXPORT_SYMBOL(RTR0MemObjGetPagePhysAddr);


/**
 * Frees a ring-0 memory object.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_HANDLE if
 * @param   MemObj          The ring-0 memory object to be freed. NULL is accepted.
 * @param   fFreeMappings   Whether or not to free mappings of the object.
 */
RTR0DECL(int) RTR0MemObjFree(RTR0MEMOBJ MemObj, bool fFreeMappings)
{
    /*
     * Validate the object handle.
     */
    PRTR0MEMOBJINTERNAL pMem;
    int rc;

    if (MemObj == NIL_RTR0MEMOBJ)
        return VINF_SUCCESS;
    AssertPtrReturn(MemObj, VERR_INVALID_HANDLE);
    pMem = (PRTR0MEMOBJINTERNAL)MemObj;
    AssertReturn(pMem->u32Magic == RTR0MEMOBJ_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pMem->enmType > RTR0MEMOBJTYPE_INVALID && pMem->enmType < RTR0MEMOBJTYPE_END, VERR_INVALID_HANDLE);
    RT_ASSERT_PREEMPTIBLE();

    /*
     * Deal with mappings according to fFreeMappings.
     */
    if (    !rtR0MemObjIsMapping(pMem)
        &&  pMem->uRel.Parent.cMappings > 0)
    {
        /* fail if not requested to free mappings. */
        if (!fFreeMappings)
            return VERR_MEMORY_BUSY;

        while (pMem->uRel.Parent.cMappings > 0)
        {
            PRTR0MEMOBJINTERNAL pChild = pMem->uRel.Parent.papMappings[--pMem->uRel.Parent.cMappings];
            pMem->uRel.Parent.papMappings[pMem->uRel.Parent.cMappings] = NULL;

            /* sanity checks. */
            AssertPtr(pChild);
            AssertFatal(pChild->u32Magic == RTR0MEMOBJ_MAGIC);
            AssertFatal(pChild->enmType > RTR0MEMOBJTYPE_INVALID && pChild->enmType < RTR0MEMOBJTYPE_END);
            AssertFatal(rtR0MemObjIsMapping(pChild));

            /* free the mapping. */
            rc = rtR0MemObjNativeFree(pChild);
            if (RT_FAILURE(rc))
            {
                Log(("RTR0MemObjFree: failed to free mapping %p: %p %#zx; rc=%Rrc\n", pChild, pChild->pv, pChild->cb, rc));
                pMem->uRel.Parent.papMappings[pMem->uRel.Parent.cMappings++] = pChild;
                return rc;
            }
        }
    }

    /*
     * Free this object.
     */
    rc = rtR0MemObjNativeFree(pMem);
    if (RT_SUCCESS(rc))
    {
        /*
         * Ok, it was freed just fine. Now, if it's a mapping we'll have to remove it from the parent.
         */
        if (rtR0MemObjIsMapping(pMem))
        {
            PRTR0MEMOBJINTERNAL pParent = pMem->uRel.Child.pParent;
            uint32_t i;

            /* sanity checks */
            AssertPtr(pParent);
            AssertFatal(pParent->u32Magic == RTR0MEMOBJ_MAGIC);
            AssertFatal(pParent->enmType > RTR0MEMOBJTYPE_INVALID && pParent->enmType < RTR0MEMOBJTYPE_END);
            AssertFatal(!rtR0MemObjIsMapping(pParent));
            AssertFatal(pParent->uRel.Parent.cMappings > 0);
            AssertPtr(pParent->uRel.Parent.papMappings);

            /* locate and remove from the array of mappings. */
            i = pParent->uRel.Parent.cMappings;
            while (i-- > 0)
            {
                if (pParent->uRel.Parent.papMappings[i] == pMem)
                {
                    pParent->uRel.Parent.papMappings[i] = pParent->uRel.Parent.papMappings[--pParent->uRel.Parent.cMappings];
                    break;
                }
            }
            Assert(i != UINT32_MAX);
        }
        else
            Assert(pMem->uRel.Parent.cMappings == 0);

        /*
         * Finally, destroy the handle.
         */
        pMem->u32Magic++;
        pMem->enmType = RTR0MEMOBJTYPE_END;
        if (!rtR0MemObjIsMapping(pMem))
            RTMemFree(pMem->uRel.Parent.papMappings);
        RTMemFree(pMem);
    }
    else
        Log(("RTR0MemObjFree: failed to free %p: %d %p %#zx; rc=%Rrc\n",
             pMem, pMem->enmType, pMem->pv, pMem->cb, rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTR0MemObjFree);



RTR0DECL(int) RTR0MemObjAllocPageTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeAllocPage(pMemObj, cbAligned, fExecutable);
}
RT_EXPORT_SYMBOL(RTR0MemObjAllocPageTag);


RTR0DECL(int) RTR0MemObjAllocLowTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeAllocLow(pMemObj, cbAligned, fExecutable);
}
RT_EXPORT_SYMBOL(RTR0MemObjAllocLowTag);


RTR0DECL(int) RTR0MemObjAllocContTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeAllocCont(pMemObj, cbAligned, fExecutable);
}
RT_EXPORT_SYMBOL(RTR0MemObjAllocContTag);


RTR0DECL(int) RTR0MemObjLockUserTag(PRTR0MEMOBJ pMemObj, RTR3PTR R3Ptr, size_t cb,
                                    uint32_t fAccess, RTR0PROCESS R0Process, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb + (R3Ptr & PAGE_OFFSET_MASK), PAGE_SIZE);
    RTR3PTR const R3PtrAligned = (R3Ptr & ~(RTR3PTR)PAGE_OFFSET_MASK);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    if (R0Process == NIL_RTR0PROCESS)
        R0Process = RTR0ProcHandleSelf();
    AssertReturn(!(fAccess & ~(RTMEM_PROT_READ | RTMEM_PROT_WRITE)), VERR_INVALID_PARAMETER);
    AssertReturn(fAccess, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the locking. */
    return rtR0MemObjNativeLockUser(pMemObj, R3PtrAligned, cbAligned, fAccess, R0Process);
}
RT_EXPORT_SYMBOL(RTR0MemObjLockUserTag);


RTR0DECL(int) RTR0MemObjLockKernelTag(PRTR0MEMOBJ pMemObj, void *pv, size_t cb, uint32_t fAccess, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb + ((uintptr_t)pv & PAGE_OFFSET_MASK), PAGE_SIZE);
    void * const pvAligned = (void *)((uintptr_t)pv & ~(uintptr_t)PAGE_OFFSET_MASK);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvAligned, VERR_INVALID_POINTER);
    AssertReturn(!(fAccess & ~(RTMEM_PROT_READ | RTMEM_PROT_WRITE)), VERR_INVALID_PARAMETER);
    AssertReturn(fAccess, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeLockKernel(pMemObj, pvAligned, cbAligned, fAccess);
}
RT_EXPORT_SYMBOL(RTR0MemObjLockKernelTag);


RTR0DECL(int) RTR0MemObjAllocPhysTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    AssertReturn(PhysHighest >= cb, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeAllocPhys(pMemObj, cbAligned, PhysHighest, PAGE_SIZE /* page aligned */);
}
RT_EXPORT_SYMBOL(RTR0MemObjAllocPhysTag);


RTR0DECL(int) RTR0MemObjAllocPhysExTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    AssertReturn(PhysHighest >= cb, VERR_INVALID_PARAMETER);
    if (uAlignment == 0)
        uAlignment = PAGE_SIZE;
    AssertReturn(    uAlignment == PAGE_SIZE
                 ||  uAlignment == _2M
                 ||  uAlignment == _4M
                 ||  uAlignment == _1G,
                 VERR_INVALID_PARAMETER);
#if HC_ARCH_BITS == 32
    /* Memory allocated in this way is typically mapped into kernel space as well; simply
       don't allow this on 32 bits hosts as the kernel space is too crowded already. */
    if (uAlignment != PAGE_SIZE)
        return VERR_NOT_SUPPORTED;
#endif
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeAllocPhys(pMemObj, cbAligned, PhysHighest, uAlignment);
}
RT_EXPORT_SYMBOL(RTR0MemObjAllocPhysExTag);


RTR0DECL(int) RTR0MemObjAllocPhysNCTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    AssertReturn(PhysHighest >= cb, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeAllocPhysNC(pMemObj, cbAligned, PhysHighest);
}
RT_EXPORT_SYMBOL(RTR0MemObjAllocPhysNCTag);


RTR0DECL(int) RTR0MemObjEnterPhysTag(PRTR0MEMOBJ pMemObj, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb + (Phys & PAGE_OFFSET_MASK), PAGE_SIZE);
    const RTHCPHYS PhysAligned = Phys & ~(RTHCPHYS)PAGE_OFFSET_MASK;
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    AssertReturn(Phys != NIL_RTHCPHYS, VERR_INVALID_PARAMETER);
    AssertReturn(   uCachePolicy == RTMEM_CACHE_POLICY_DONT_CARE
                 || uCachePolicy == RTMEM_CACHE_POLICY_MMIO,
                 VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the allocation. */
    return rtR0MemObjNativeEnterPhys(pMemObj, PhysAligned, cbAligned, uCachePolicy);
}
RT_EXPORT_SYMBOL(RTR0MemObjEnterPhysTag);


RTR0DECL(int) RTR0MemObjReserveKernelTag(PRTR0MEMOBJ pMemObj, void *pvFixed, size_t cb, size_t uAlignment, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    if (uAlignment == 0)
        uAlignment = PAGE_SIZE;
    AssertReturn(uAlignment == PAGE_SIZE || uAlignment == _2M || uAlignment == _4M, VERR_INVALID_PARAMETER);
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    if (pvFixed != (void *)-1)
        AssertReturn(!((uintptr_t)pvFixed & (uAlignment - 1)), VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the reservation. */
    return rtR0MemObjNativeReserveKernel(pMemObj, pvFixed, cbAligned, uAlignment);
}
RT_EXPORT_SYMBOL(RTR0MemObjReserveKernelTag);


RTR0DECL(int) RTR0MemObjReserveUserTag(PRTR0MEMOBJ pMemObj, RTR3PTR R3PtrFixed, size_t cb,
                                       size_t uAlignment, RTR0PROCESS R0Process, const char *pszTag)
{
    /* sanity checks. */
    const size_t cbAligned = RT_ALIGN_Z(cb, PAGE_SIZE);
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    if (uAlignment == 0)
        uAlignment = PAGE_SIZE;
    AssertReturn(uAlignment == PAGE_SIZE || uAlignment == _2M || uAlignment == _4M, VERR_INVALID_PARAMETER);
    AssertReturn(cb > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cb <= cbAligned, VERR_INVALID_PARAMETER);
    if (R3PtrFixed != (RTR3PTR)-1)
        AssertReturn(!(R3PtrFixed & (uAlignment - 1)), VERR_INVALID_PARAMETER);
    if (R0Process == NIL_RTR0PROCESS)
        R0Process = RTR0ProcHandleSelf();
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the reservation. */
    return rtR0MemObjNativeReserveUser(pMemObj, R3PtrFixed, cbAligned, uAlignment, R0Process);
}
RT_EXPORT_SYMBOL(RTR0MemObjReserveUserTag);


RTR0DECL(int) RTR0MemObjMapKernelTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed,
                                     size_t uAlignment, unsigned fProt, const char *pszTag)
{
    return RTR0MemObjMapKernelExTag(pMemObj, MemObjToMap, pvFixed, uAlignment, fProt, 0, 0, pszTag);
}
RT_EXPORT_SYMBOL(RTR0MemObjMapKernelTag);


RTR0DECL(int) RTR0MemObjMapKernelExTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed, size_t uAlignment,
                                       unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag)
{
    PRTR0MEMOBJINTERNAL pMemToMap;
    PRTR0MEMOBJINTERNAL pNew;
    int                 rc;

    /* sanity checks. */
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertPtrReturn(MemObjToMap, VERR_INVALID_HANDLE);
    pMemToMap = (PRTR0MEMOBJINTERNAL)MemObjToMap;
    AssertReturn(pMemToMap->u32Magic == RTR0MEMOBJ_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pMemToMap->enmType > RTR0MEMOBJTYPE_INVALID && pMemToMap->enmType < RTR0MEMOBJTYPE_END, VERR_INVALID_HANDLE);
    AssertReturn(!rtR0MemObjIsMapping(pMemToMap), VERR_INVALID_PARAMETER);
    AssertReturn(pMemToMap->enmType != RTR0MEMOBJTYPE_RES_VIRT, VERR_INVALID_PARAMETER);
    if (uAlignment == 0)
        uAlignment = PAGE_SIZE;
    AssertReturn(uAlignment == PAGE_SIZE || uAlignment == _2M || uAlignment == _4M, VERR_INVALID_PARAMETER);
    if (pvFixed != (void *)-1)
        AssertReturn(!((uintptr_t)pvFixed & (uAlignment - 1)), VERR_INVALID_PARAMETER);
    AssertReturn(fProt != RTMEM_PROT_NONE, VERR_INVALID_PARAMETER);
    AssertReturn(!(fProt & ~(RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC)), VERR_INVALID_PARAMETER);
    AssertReturn(!(offSub & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(offSub < pMemToMap->cb, VERR_INVALID_PARAMETER);
    AssertReturn(!(cbSub & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(cbSub <= pMemToMap->cb, VERR_INVALID_PARAMETER);
    AssertReturn((!offSub && !cbSub) || (offSub + cbSub) <= pMemToMap->cb, VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* adjust the request to simplify the native code. */
    if (offSub == 0 && cbSub == pMemToMap->cb)
        cbSub = 0;

    /* do the mapping. */
    rc = rtR0MemObjNativeMapKernel(&pNew, pMemToMap, pvFixed, uAlignment, fProt, offSub, cbSub);
    if (RT_SUCCESS(rc))
    {
        /* link it. */
        rc = rtR0MemObjLink(pMemToMap, pNew);
        if (RT_SUCCESS(rc))
            *pMemObj = pNew;
        else
        {
            /* damn, out of memory. bail out. */
            int rc2 = rtR0MemObjNativeFree(pNew);
            AssertRC(rc2);
            pNew->u32Magic++;
            pNew->enmType = RTR0MEMOBJTYPE_END;
            RTMemFree(pNew);
        }
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTR0MemObjMapKernelExTag);


RTR0DECL(int) RTR0MemObjMapUserTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, RTR3PTR R3PtrFixed,
                                   size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process, const char *pszTag)
{
    /* sanity checks. */
    PRTR0MEMOBJINTERNAL pMemToMap;
    PRTR0MEMOBJINTERNAL pNew;
    int rc;
    AssertPtrReturn(pMemObj, VERR_INVALID_POINTER);
    pMemToMap = (PRTR0MEMOBJINTERNAL)MemObjToMap;
    *pMemObj = NIL_RTR0MEMOBJ;
    AssertPtrReturn(MemObjToMap, VERR_INVALID_HANDLE);
    AssertReturn(pMemToMap->u32Magic == RTR0MEMOBJ_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pMemToMap->enmType > RTR0MEMOBJTYPE_INVALID && pMemToMap->enmType < RTR0MEMOBJTYPE_END, VERR_INVALID_HANDLE);
    AssertReturn(!rtR0MemObjIsMapping(pMemToMap), VERR_INVALID_PARAMETER);
    AssertReturn(pMemToMap->enmType != RTR0MEMOBJTYPE_RES_VIRT, VERR_INVALID_PARAMETER);
    if (uAlignment == 0)
        uAlignment = PAGE_SIZE;
    AssertReturn(uAlignment == PAGE_SIZE || uAlignment == _2M || uAlignment == _4M, VERR_INVALID_PARAMETER);
    if (R3PtrFixed != (RTR3PTR)-1)
        AssertReturn(!(R3PtrFixed & (uAlignment - 1)), VERR_INVALID_PARAMETER);
    AssertReturn(fProt != RTMEM_PROT_NONE, VERR_INVALID_PARAMETER);
    AssertReturn(!(fProt & ~(RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC)), VERR_INVALID_PARAMETER);
    if (R0Process == NIL_RTR0PROCESS)
        R0Process = RTR0ProcHandleSelf();
    RT_ASSERT_PREEMPTIBLE();

    RT_NOREF_PV(pszTag);

    /* do the mapping. */
    rc = rtR0MemObjNativeMapUser(&pNew, pMemToMap, R3PtrFixed, uAlignment, fProt, R0Process);
    if (RT_SUCCESS(rc))
    {
        /* link it. */
        rc = rtR0MemObjLink(pMemToMap, pNew);
        if (RT_SUCCESS(rc))
            *pMemObj = pNew;
        else
        {
            /* damn, out of memory. bail out. */
            int rc2 = rtR0MemObjNativeFree(pNew);
            AssertRC(rc2);
            pNew->u32Magic++;
            pNew->enmType = RTR0MEMOBJTYPE_END;
            RTMemFree(pNew);
        }
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTR0MemObjMapUserTag);


RTR0DECL(int) RTR0MemObjProtect(RTR0MEMOBJ hMemObj, size_t offSub, size_t cbSub, uint32_t fProt)
{
    PRTR0MEMOBJINTERNAL pMemObj;
    int                 rc;

    /* sanity checks. */
    pMemObj = (PRTR0MEMOBJINTERNAL)hMemObj;
    AssertPtrReturn(pMemObj, VERR_INVALID_HANDLE);
    AssertReturn(pMemObj->u32Magic == RTR0MEMOBJ_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pMemObj->enmType > RTR0MEMOBJTYPE_INVALID && pMemObj->enmType < RTR0MEMOBJTYPE_END, VERR_INVALID_HANDLE);
    AssertReturn(rtR0MemObjIsProtectable(pMemObj), VERR_INVALID_PARAMETER);
    AssertReturn(!(offSub & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(offSub < pMemObj->cb, VERR_INVALID_PARAMETER);
    AssertReturn(!(cbSub  & PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(cbSub <= pMemObj->cb, VERR_INVALID_PARAMETER);
    AssertReturn(offSub + cbSub <= pMemObj->cb, VERR_INVALID_PARAMETER);
    AssertReturn(!(fProt & ~(RTMEM_PROT_NONE | RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC)), VERR_INVALID_PARAMETER);
    RT_ASSERT_PREEMPTIBLE();

    /* do the job */
    rc = rtR0MemObjNativeProtect(pMemObj, offSub, cbSub, fProt);
    if (RT_SUCCESS(rc))
        pMemObj->fFlags |= RTR0MEMOBJ_FLAGS_PROT_CHANGED; /* record it */

    return rc;
}
RT_EXPORT_SYMBOL(RTR0MemObjProtect);

