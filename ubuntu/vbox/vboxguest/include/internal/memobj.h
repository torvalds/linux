/* $Id: memobj.h $ */
/** @file
 * IPRT - Ring-0 Memory Objects.
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

#ifndef IPRT_INCLUDED_INTERNAL_memobj_h
#define IPRT_INCLUDED_INTERNAL_memobj_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/memobj.h>
#include <iprt/assert.h>
#include "internal/magics.h"

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_memobj_int Internals.
 * @ingroup grp_rt_memobj
 * @internal
 * @{
 */

/**
 * Ring-0 memory object type.
 */
typedef enum RTR0MEMOBJTYPE
{
    /** The traditional invalid value. */
    RTR0MEMOBJTYPE_INVALID = 0,

    /** @name Primary types (parents)
     * @{ */
    /** RTR0MemObjAllocPage.
     * This memory is page aligned and fixed. */
    RTR0MEMOBJTYPE_PAGE,
    /** RTR0MemObjAllocLow.
     * This memory is page aligned, fixed and is backed by physical memory below 4GB. */
    RTR0MEMOBJTYPE_LOW,
    /** RTR0MemObjAllocCont.
     * This memory is page aligned, fixed and is backed by contiguous physical memory below 4GB. */
    RTR0MEMOBJTYPE_CONT,
    /** RTR0MemObjLockKernel, RTR0MemObjLockUser.
     * This memory is page aligned and fixed. It was locked/pinned/wired down by the API call. */
    RTR0MEMOBJTYPE_LOCK,
    /** RTR0MemObjAllocPhys, RTR0MemObjEnterPhys.
     * This memory is physical memory, page aligned, contiguous and doesn't need to have a mapping. */
    RTR0MEMOBJTYPE_PHYS,
    /** RTR0MemObjAllocPhysNC.
     * This memory is physical memory, page aligned and doesn't need to have a mapping. */
    RTR0MEMOBJTYPE_PHYS_NC,
    /** RTR0MemObjReserveKernel, RTR0MemObjReserveUser.
     * This memory is page aligned and has no backing. */
    RTR0MEMOBJTYPE_RES_VIRT,
    /** @} */

    /** @name Secondary types (children)
     * @{
     */
    /** RTR0MemObjMapUser, RTR0MemObjMapKernel.
     * This is a user or kernel context mapping of another ring-0 memory object. */
    RTR0MEMOBJTYPE_MAPPING,
    /** @} */

    /** The end of the valid types. Used for sanity checking. */
    RTR0MEMOBJTYPE_END
} RTR0MEMOBJTYPE;


/** @name RTR0MEMOBJINTERNAL::fFlags
 * @{ */
/** Page level protection was changed. */
#define RTR0MEMOBJ_FLAGS_PROT_CHANGED       RT_BIT_32(0)
/** @} */


typedef struct RTR0MEMOBJINTERNAL *PRTR0MEMOBJINTERNAL;
typedef struct RTR0MEMOBJINTERNAL **PPRTR0MEMOBJINTERNAL;

/**
 * Ring-0 memory object.
 *
 * When using the PRTR0MEMOBJINTERNAL and PPRTR0MEMOBJINTERNAL types
 * we get pMem and ppMem variable names.
 *
 * When using the RTR0MEMOBJ and PRTR0MEMOBJ types we get MemObj and
 * pMemObj variable names. We never dereference variables of the RTR0MEMOBJ
 * type, we always convert it to a PRTR0MEMOBJECTINTERNAL variable first.
 */
typedef struct RTR0MEMOBJINTERNAL
{
    /** Magic number (RTR0MEMOBJ_MAGIC). */
    uint32_t        u32Magic;
    /** The size of this structure. */
    uint32_t        cbSelf;
    /** The type of allocation. */
    RTR0MEMOBJTYPE  enmType;
    /** Flags, RTR0MEMOBJ_FLAGS_*. */
    uint32_t        fFlags;
    /** The size of the memory allocated, pinned down, or mapped. */
    size_t          cb;
    /** The memory address.
     * What this really is varies with the type.
     * For PAGE, CONT, LOW, RES_VIRT/R0, LOCK/R0 and MAP/R0 it's the ring-0 mapping.
     * For LOCK/R3, RES_VIRT/R3 and MAP/R3 it is the ring-3 mapping.
     * For PHYS this might actually be NULL if there isn't any mapping.
     */
    void           *pv;

    /** Object relations. */
    union
    {
        /** This is for tracking child memory handles mapping the
         * memory described by the primary handle. */
        struct
        {
            /** Number of mappings. */
            uint32_t                cMappingsAllocated;
            /** Number of mappings in the array. */
            uint32_t                cMappings;
            /** Pointers to child handles mapping this memory. */
            PPRTR0MEMOBJINTERNAL    papMappings;
        } Parent;

        /** Pointer to the primary handle. */
        struct
        {
            /** Pointer to the parent. */
            PRTR0MEMOBJINTERNAL     pParent;
        } Child;
    } uRel;

    /** Type specific data for the memory types that requires that. */
    union
    {
        /** RTR0MEMTYPE_PAGE. */
        struct
        {
            unsigned iDummy;
        } Page;

        /** RTR0MEMTYPE_LOW. */
        struct
        {
            unsigned iDummy;
        } Low;

        /** RTR0MEMTYPE_CONT. */
        struct
        {
            /** The physical address of the first page. */
            RTHCPHYS    Phys;
        } Cont;

        /** RTR0MEMTYPE_LOCK_USER. */
        struct
        {
            /** The process that owns the locked memory.
             * This is NIL_RTR0PROCESS if it's kernel memory. */
            RTR0PROCESS R0Process;
        } Lock;

        /** RTR0MEMTYPE_PHYS. */
        struct
        {
            /** The base address of the physical memory. */
            RTHCPHYS    PhysBase;
            /** If set this object was created by RTR0MemPhysAlloc, otherwise it was
             * created by RTR0MemPhysEnter. */
            bool        fAllocated;
            /** See RTMEM_CACHE_POLICY_XXX constants */
            uint32_t    uCachePolicy;
        } Phys;

        /** RTR0MEMTYPE_PHYS_NC. */
        struct
        {
            unsigned iDummy;
        } PhysNC;

        /** RTR0MEMOBJTYPE_RES_VIRT */
        struct
        {
            /** The process that owns the reserved memory.
             * This is NIL_RTR0PROCESS if it's kernel memory. */
            RTR0PROCESS R0Process;
        } ResVirt;

        /** RTR0MEMOBJTYPE_MAPPING */
        struct
        {
            /** The process that owns the reserved memory.
             * This is NIL_RTR0PROCESS if it's kernel memory. */
            RTR0PROCESS R0Process;
        } Mapping;
    } u;

} RTR0MEMOBJINTERNAL;


/**
 * Checks if this is mapping or not.
 *
 * @returns true if it's a mapping, otherwise false.
 * @param   pMem        The ring-0 memory object handle.
 * @see RTR0MemObjIsMapping
 */
DECLINLINE(bool) rtR0MemObjIsMapping(PRTR0MEMOBJINTERNAL pMem)
{
    switch (pMem->enmType)
    {
        case RTR0MEMOBJTYPE_MAPPING:
            return true;

        default:
            return false;
    }
}


/**
 * Checks page level protection can be changed on this object.
 *
 * @returns true / false.
 * @param   pMem        The ring-0 memory object handle.
 */
DECLINLINE(bool) rtR0MemObjIsProtectable(PRTR0MEMOBJINTERNAL pMem)
{
    switch (pMem->enmType)
    {
        case RTR0MEMOBJTYPE_MAPPING:
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
            return true;

        default:
            return false;
    }
}


/**
 * Checks if RTR0MEMOBJ::pv is a ring-3 pointer or not.
 *
 * @returns true if it's a object with a ring-3 address, otherwise false.
 * @param   pMem        The ring-0 memory object handle.
 */
DECLINLINE(bool) rtR0MemObjIsRing3(PRTR0MEMOBJINTERNAL pMem)
{
    switch (pMem->enmType)
    {
        case RTR0MEMOBJTYPE_RES_VIRT:
            return pMem->u.ResVirt.R0Process != NIL_RTR0PROCESS;
        case RTR0MEMOBJTYPE_LOCK:
            return pMem->u.Lock.R0Process    != NIL_RTR0PROCESS;
        case RTR0MEMOBJTYPE_MAPPING:
            return pMem->u.Mapping.R0Process != NIL_RTR0PROCESS;
        default:
            return false;
    }
}


/**
 * Frees the memory object (but not the handle).
 * Any OS specific handle resources will be freed by this call.
 *
 * @returns IPRT status code. On failure it is assumed that the object remains valid.
 * @param   pMem        The ring-0 memory object handle to the memory which should be freed.
 */
DECLHIDDEN(int) rtR0MemObjNativeFree(PRTR0MEMOBJINTERNAL pMem);

/**
 * Allocates page aligned virtual kernel memory.
 *
 * The memory is taken from a non paged (= fixed physical memory backing) pool.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate, page aligned.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 */
DECLHIDDEN(int) rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable);

/**
 * Allocates page aligned virtual kernel memory with physical backing below 4GB.
 *
 * The physical memory backing the allocation is fixed.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate, page aligned.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 */
DECLHIDDEN(int) rtR0MemObjNativeAllocLow(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable);

/**
 * Allocates page aligned virtual kernel memory with contiguous physical backing below 4GB.
 *
 * The physical memory backing the allocation is fixed.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate, page aligned.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 */
DECLHIDDEN(int) rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable);

/**
 * Locks a range of user virtual memory.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   R3Ptr           User virtual address, page aligned.
 * @param   cb              Number of bytes to lock, page aligned.
 * @param   fAccess         The desired access, a combination of RTMEM_PROT_READ
 *                          and RTMEM_PROT_WRITE.
 * @param   R0Process       The process to lock pages in.
 */
DECLHIDDEN(int) rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess, RTR0PROCESS R0Process);

/**
 * Locks a range of kernel virtual memory.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   pv              Kernel virtual address, page aligned.
 * @param   cb              Number of bytes to lock, page aligned.
 * @param   fAccess         The desired access, a combination of RTMEM_PROT_READ
 *                          and RTMEM_PROT_WRITE.
 */
DECLHIDDEN(int) rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, uint32_t fAccess);

/**
 * Allocates contiguous page aligned physical memory without (necessarily) any
 * kernel mapping.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate, page aligned.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          NIL_RTHCPHYS if any address is acceptable.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are PAGE_SIZE, _2M, _4M and _1G.
 */
DECLHIDDEN(int) rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment);

/**
 * Allocates non-contiguous page aligned physical memory without (necessarily) any kernel mapping.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if it's not possible to allocated unmapped
 *          physical memory on this platform.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate, page aligned.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          NIL_RTHCPHYS if any address is acceptable.
 */
DECLHIDDEN(int) rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest);

/**
 * Creates a page aligned, contiguous, physical memory object.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   Phys            The physical address to start at, page aligned.
 * @param   cb              The size of the object in bytes, page aligned.
 * @param   uCachePolicy    One of the RTMEM_CACHE_XXX modes.
 */
DECLHIDDEN(int) rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy);

/**
 * Reserves kernel virtual address space.
 *
 * @returns IPRT status code.
 *          Return VERR_NOT_SUPPORTED to indicate that the user should employ fallback strategies.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This matches uAlignment if specified.
 * @param   cb              The number of bytes to reserve, page aligned.
 * @param   uAlignment      The alignment of the reserved memory; PAGE_SIZE, _2M or _4M.
 */
DECLHIDDEN(int) rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment);

/**
 * Reserves user virtual address space in the current process.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle.
 * @param   R3PtrFixed      Requested address. (RTR3PTR)-1 means any address. This matches uAlignment if specified.
 * @param   cb              The number of bytes to reserve, page aligned.
 * @param   uAlignment      The alignment of the reserved memory; PAGE_SIZE, _2M or _4M.
 * @param   R0Process       The process to reserve the memory in.
 */
DECLHIDDEN(int) rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment, RTR0PROCESS R0Process);

/**
 * Maps a memory object into user virtual address space in the current process.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED see RTR0MemObjMapKernelEx.
 *
 * @param   ppMem           Where to store the ring-0 memory object handle of the mapping object.
 * @param   pMemToMap       The object to be map.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This matches uAlignment if specified.
 * @param   uAlignment      The alignment of the reserved memory; PAGE_SIZE, _2M or _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 * @param   offSub          Where in the object to start mapping. If non-zero
 *                          the value must be page aligned and cbSub must be
 *                          non-zero as well.
 * @param   cbSub           The size of the part of the object to be mapped. If
 *                          zero the entire object is mapped. The value must be
 *                          page aligned.
 */
DECLHIDDEN(int) rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment,
                                          unsigned fProt, size_t offSub, size_t cbSub);

/**
 * Maps a memory object into user virtual address space in the current process.
 *
 * @returns IPRT status code.
 * @param   ppMem           Where to store the ring-0 memory object handle of the mapping object.
 * @param   pMemToMap       The object to be map.
 * @param   R3PtrFixed      Requested address. (RTR3PTR)-1 means any address. This matches uAlignment if specified.
 * @param   uAlignment      The alignment of the reserved memory; PAGE_SIZE, _2M or _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 * @param   R0Process       The process to map the memory into.
 */
DECLHIDDEN(int) rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, PRTR0MEMOBJINTERNAL pMemToMap, RTR3PTR R3PtrFixed, size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process);

/**
 * Change the page level protection of one or more pages in a memory object.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED see RTR0MemObjProtect.
 *
 * @param   pMem            The memory object.
 * @param   offSub          Offset into the memory object. Page aligned.
 * @param   cbSub           Number of bytes to change the protection of. Page
 *                          aligned.
 * @param   fProt           Combination of RTMEM_PROT_* flags.
 */
DECLHIDDEN(int) rtR0MemObjNativeProtect(PRTR0MEMOBJINTERNAL pMem, size_t offSub, size_t cbSub, uint32_t fProt);

/**
 * Get the physical address of an page in the memory object.
 *
 * @returns The physical address.
 * @returns NIL_RTHCPHYS if the object doesn't contain fixed physical pages.
 * @returns NIL_RTHCPHYS if the iPage is out of range.
 * @returns NIL_RTHCPHYS if the object handle isn't valid.
 * @param   pMem            The ring-0 memory object handle.
 * @param   iPage           The page number within the object (valid).
 */
DECLHIDDEN(RTHCPHYS) rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, size_t iPage);

DECLHIDDEN(PRTR0MEMOBJINTERNAL) rtR0MemObjNew(size_t cbSelf, RTR0MEMOBJTYPE enmType, void *pv, size_t cb);
DECLHIDDEN(void) rtR0MemObjDelete(PRTR0MEMOBJINTERNAL pMem);

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_memobj_h */

