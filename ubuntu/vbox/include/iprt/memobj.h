/** @file
 * IPRT - Memory Objects (Ring-0).
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

#ifndef ___iprt_memobj_h
#define ___iprt_memobj_h

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_memobj     RTMemObj - Memory Object Manipulation (Ring-0)
 * @ingroup grp_rt
 * @{
 */

/** @def RTMEM_TAG
 * The default allocation tag used by the RTMem allocation APIs.
 *
 * When not defined before the inclusion of iprt/memobj.h or iprt/mem.h, this
 * will default to the pointer to the current file name.  The memory API will
 * make of use of this as pointer to a volatile but read-only string.
 */
#ifndef RTMEM_TAG
# define RTMEM_TAG   (__FILE__)
#endif

#ifdef IN_RING0

/**
 * Checks if this is mapping or not.
 *
 * @returns true if it's a mapping, otherwise false.
 * @param   MemObj  The ring-0 memory object handle.
 */
RTR0DECL(bool) RTR0MemObjIsMapping(RTR0MEMOBJ MemObj);

/**
 * Gets the address of a ring-0 memory object.
 *
 * @returns The address of the memory object.
 * @returns NULL if the handle is invalid (asserts in strict builds) or if there isn't any mapping.
 * @param   MemObj  The ring-0 memory object handle.
 */
RTR0DECL(void *) RTR0MemObjAddress(RTR0MEMOBJ MemObj);

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
RTR0DECL(RTR3PTR) RTR0MemObjAddressR3(RTR0MEMOBJ MemObj);

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
RTR0DECL(size_t) RTR0MemObjSize(RTR0MEMOBJ MemObj);

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
RTR0DECL(RTHCPHYS) RTR0MemObjGetPagePhysAddr(RTR0MEMOBJ MemObj, size_t iPage);

/**
 * Frees a ring-0 memory object.
 *
 * @returns IPRT status code.
 * @retval  VERR_INVALID_HANDLE if
 * @param   MemObj          The ring-0 memory object to be freed. NULL is accepted.
 * @param   fFreeMappings   Whether or not to free mappings of the object.
 */
RTR0DECL(int) RTR0MemObjFree(RTR0MEMOBJ MemObj, bool fFreeMappings);

/**
 * Allocates page aligned virtual kernel memory (default tag).
 *
 * The memory is taken from a non paged (= fixed physical memory backing) pool.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 */
#define RTR0MemObjAllocPage(pMemObj, cb, fExecutable) \
    RTR0MemObjAllocPageTag((pMemObj), (cb), (fExecutable), RTMEM_TAG)

/**
 * Allocates page aligned virtual kernel memory (custom tag).
 *
 * The memory is taken from a non paged (= fixed physical memory backing) pool.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjAllocPageTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag);

/**
 * Allocates page aligned virtual kernel memory with physical backing below 4GB
 * (default tag).
 *
 * The physical memory backing the allocation is fixed.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 */
#define RTR0MemObjAllocLow(pMemObj, cb, fExecutable) \
    RTR0MemObjAllocLowTag((pMemObj), (cb), (fExecutable), RTMEM_TAG)

/**
 * Allocates page aligned virtual kernel memory with physical backing below 4GB
 * (custom tag).
 *
 * The physical memory backing the allocation is fixed.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjAllocLowTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag);

/**
 * Allocates page aligned virtual kernel memory with contiguous physical backing
 * below 4GB (default tag).
 *
 * The physical memory backing the allocation is fixed.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 */
#define RTR0MemObjAllocCont(pMemObj, cb, fExecutable) \
    RTR0MemObjAllocContTag((pMemObj), (cb), (fExecutable), RTMEM_TAG)

/**
 * Allocates page aligned virtual kernel memory with contiguous physical backing
 * below 4GB (custom tag).
 *
 * The physical memory backing the allocation is fixed.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   fExecutable     Flag indicating whether it should be permitted to executed code in the memory object.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjAllocContTag(PRTR0MEMOBJ pMemObj, size_t cb, bool fExecutable, const char *pszTag);

/**
 * Locks a range of user virtual memory (default tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   R3Ptr           User virtual address. This is rounded down to a page
 *                          boundary.
 * @param   cb              Number of bytes to lock. This is rounded up to
 *                          nearest page boundary.
 * @param   fAccess         The desired access, a combination of RTMEM_PROT_READ
 *                          and RTMEM_PROT_WRITE.
 * @param   R0Process       The process to lock pages in. NIL_R0PROCESS is an
 *                          alias for the current one.
 *
 * @remarks RTR0MemGetAddressR3() and RTR0MemGetAddress() will return therounded
 *          down address.
 *
 * @remarks Linux: This API requires that the memory begin locked is in a memory
 *          mapping that is not required in any forked off child process. This
 *          is not intented as permanent restriction, feel free to help out
 *          lifting it.
 */
#define RTR0MemObjLockUser(pMemObj, R3Ptr, cb, fAccess, R0Process) \
    RTR0MemObjLockUserTag((pMemObj), (R3Ptr), (cb), (fAccess), (R0Process), RTMEM_TAG)

/**
 * Locks a range of user virtual memory (custom tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   R3Ptr           User virtual address. This is rounded down to a page
 *                          boundary.
 * @param   cb              Number of bytes to lock. This is rounded up to
 *                          nearest page boundary.
 * @param   fAccess         The desired access, a combination of RTMEM_PROT_READ
 *                          and RTMEM_PROT_WRITE.
 * @param   R0Process       The process to lock pages in. NIL_R0PROCESS is an
 *                          alias for the current one.
 * @param   pszTag          Allocation tag used for statistics and such.
 *
 * @remarks RTR0MemGetAddressR3() and RTR0MemGetAddress() will return therounded
 *          down address.
 *
 * @remarks Linux: This API requires that the memory begin locked is in a memory
 *          mapping that is not required in any forked off child process. This
 *          is not intented as permanent restriction, feel free to help out
 *          lifting it.
 */
RTR0DECL(int) RTR0MemObjLockUserTag(PRTR0MEMOBJ pMemObj, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess,
                                    RTR0PROCESS R0Process, const char *pszTag);

/**
 * Locks a range of kernel virtual memory (default tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   pv              Kernel virtual address. This is rounded down to a page boundary.
 * @param   cb              Number of bytes to lock. This is rounded up to nearest page boundary.
 * @param   fAccess         The desired access, a combination of RTMEM_PROT_READ
 *                          and RTMEM_PROT_WRITE.
 *
 * @remark  RTR0MemGetAddress() will return the rounded down address.
 */
#define RTR0MemObjLockKernel(pMemObj, pv, cb, fAccess) \
    RTR0MemObjLockKernelTag((pMemObj), (pv), (cb), (fAccess), RTMEM_TAG)

/**
 * Locks a range of kernel virtual memory (custom tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   pv              Kernel virtual address. This is rounded down to a page boundary.
 * @param   cb              Number of bytes to lock. This is rounded up to nearest page boundary.
 * @param   fAccess         The desired access, a combination of RTMEM_PROT_READ
 *                          and RTMEM_PROT_WRITE.
 * @param   pszTag          Allocation tag used for statistics and such.
 *
 * @remark  RTR0MemGetAddress() will return the rounded down address.
 */
RTR0DECL(int) RTR0MemObjLockKernelTag(PRTR0MEMOBJ pMemObj, void *pv, size_t cb, uint32_t fAccess, const char *pszTag);

/**
 * Allocates contiguous page aligned physical memory without (necessarily) any
 * kernel mapping (default tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          Pass NIL_RTHCPHYS if any address is acceptable.
 */
#define RTR0MemObjAllocPhys(pMemObj, cb, PhysHighest) \
    RTR0MemObjAllocPhysTag((pMemObj), (cb), (PhysHighest), RTMEM_TAG)

/**
 * Allocates contiguous page aligned physical memory without (necessarily) any
 * kernel mapping (custom tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          Pass NIL_RTHCPHYS if any address is acceptable.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjAllocPhysTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag);

/**
 * Allocates contiguous physical memory without (necessarily) any kernel mapping
 * (default tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          Pass NIL_RTHCPHYS if any address is acceptable.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M, _4M and _1G.
 */
#define RTR0MemObjAllocPhysEx(pMemObj, cb, PhysHighest, uAlignment) \
    RTR0MemObjAllocPhysExTag((pMemObj), (cb), (PhysHighest), (uAlignment), RTMEM_TAG)

/**
 * Allocates contiguous physical memory without (necessarily) any kernel mapping
 * (custom tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          Pass NIL_RTHCPHYS if any address is acceptable.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M, _4M and _1G.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjAllocPhysExTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment, const char *pszTag);

/**
 * Allocates non-contiguous page aligned physical memory without (necessarily)
 * any kernel mapping (default tag).
 *
 * This API is for allocating huge amounts of pages and will return
 * VERR_NOT_SUPPORTED if this cannot be implemented in a satisfactory
 * manner.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if it's not possible to allocated unmapped
 *          physical memory on this platform. The caller should expect
 *          this error and have a fallback strategy for it.
 *
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          Pass NIL_RTHCPHYS if any address is acceptable.
 */
#define RTR0MemObjAllocPhysNC(pMemObj, cb, PhysHighest) \
    RTR0MemObjAllocPhysNCTag((pMemObj), (cb), (PhysHighest), RTMEM_TAG)

/**
 * Allocates non-contiguous page aligned physical memory without (necessarily)
 * any kernel mapping (custom tag).
 *
 * This API is for allocating huge amounts of pages and will return
 * VERR_NOT_SUPPORTED if this cannot be implemented in a satisfactory
 * manner.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if it's not possible to allocated unmapped
 *          physical memory on this platform. The caller should expect
 *          this error and have a fallback strategy for it.
 *
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   cb              Number of bytes to allocate. This is rounded up to nearest page.
 * @param   PhysHighest     The highest permitable address (inclusive).
 *                          Pass NIL_RTHCPHYS if any address is acceptable.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjAllocPhysNCTag(PRTR0MEMOBJ pMemObj, size_t cb, RTHCPHYS PhysHighest, const char *pszTag);

/** Memory cache policy for RTR0MemObjEnterPhys.
 * @{
 */
/** Default caching policy -- don't care. */
#define RTMEM_CACHE_POLICY_DONT_CARE    UINT32_C(0)
/** MMIO caching policy -- uncachable. */
#define RTMEM_CACHE_POLICY_MMIO         UINT32_C(1)
/** @} */

/**
 * Creates a page aligned, contiguous, physical memory object (default tag).
 *
 * No physical memory is allocated, we trust you do know what you're doing.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   Phys            The physical address to start at. This is rounded down to the
 *                          nearest page boundary.
 * @param   cb              The size of the object in bytes. This is rounded up to nearest page boundary.
 * @param   uCachePolicy    One of the RTMEM_CACHE_XXX modes.
 */
#define RTR0MemObjEnterPhys(pMemObj, Phys, cb, uCachePolicy) \
    RTR0MemObjEnterPhysTag((pMemObj), (Phys), (cb), (uCachePolicy), RTMEM_TAG)

/**
 * Creates a page aligned, contiguous, physical memory object (custom tag).
 *
 * No physical memory is allocated, we trust you do know what you're doing.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   Phys            The physical address to start at. This is rounded down to the
 *                          nearest page boundary.
 * @param   cb              The size of the object in bytes. This is rounded up to nearest page boundary.
 * @param   uCachePolicy    One of the RTMEM_CACHE_XXX modes.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjEnterPhysTag(PRTR0MEMOBJ pMemObj, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy, const char *pszTag);

/**
 * Reserves kernel virtual address space (default tag).
 *
 * If this function fails with VERR_NOT_SUPPORTED, the idea is that you
 * can use RTR0MemObjEnterPhys() + RTR0MemObjMapKernel() as a fallback if
 * you have a safe physical address range to make use of...
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This must match the alignment.
 * @param   cb              The number of bytes to reserve. This is rounded up to nearest page.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 */
#define RTR0MemObjReserveKernel(pMemObj, pvFixed, cb, uAlignment) \
    RTR0MemObjReserveKernelTag((pMemObj), (pvFixed), (cb), (uAlignment), RTMEM_TAG)

/**
 * Reserves kernel virtual address space (custom tag).
 *
 * If this function fails with VERR_NOT_SUPPORTED, the idea is that you
 * can use RTR0MemObjEnterPhys() + RTR0MemObjMapKernel() as a fallback if
 * you have a safe physical address range to make use of...
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This must match the alignment.
 * @param   cb              The number of bytes to reserve. This is rounded up to nearest page.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjReserveKernelTag(PRTR0MEMOBJ pMemObj, void *pvFixed, size_t cb, size_t uAlignment, const char *pszTag);

/**
 * Reserves user virtual address space in the current process (default tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   R3PtrFixed      Requested address. (RTR3PTR)-1 means any address. This must match the alignment.
 * @param   cb              The number of bytes to reserve. This is rounded up to nearest PAGE_SIZE.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   R0Process       The process to reserve the memory in. NIL_R0PROCESS is an alias for the current one.
 */
#define RTR0MemObjReserveUser(pMemObj, R3PtrFixed, cb, uAlignment, R0Process) \
    RTR0MemObjReserveUserTag((pMemObj), (R3PtrFixed), (cb), (uAlignment), (R0Process), RTMEM_TAG)

/**
 * Reserves user virtual address space in the current process (custom tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle.
 * @param   R3PtrFixed      Requested address. (RTR3PTR)-1 means any address. This must match the alignment.
 * @param   cb              The number of bytes to reserve. This is rounded up to nearest PAGE_SIZE.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   R0Process       The process to reserve the memory in. NIL_R0PROCESS is an alias for the current one.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjReserveUserTag(PRTR0MEMOBJ pMemObj, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment,
                                       RTR0PROCESS R0Process, const char *pszTag);

/**
 * Maps a memory object into kernel virtual address space (default tag).
 *
 * This is the same as calling RTR0MemObjMapKernelEx with cbSub and offSub set
 * to zero.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle of the mapping object.
 * @param   MemObjToMap     The object to be map.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This must match the alignment.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 */
#define RTR0MemObjMapKernel(pMemObj, MemObjToMap, pvFixed, uAlignment, fProt) \
    RTR0MemObjMapKernelTag((pMemObj), (MemObjToMap), (pvFixed), (uAlignment), (fProt), RTMEM_TAG)

/**
 * Maps a memory object into kernel virtual address space (custom tag).
 *
 * This is the same as calling RTR0MemObjMapKernelEx with cbSub and offSub set
 * to zero.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle of the mapping object.
 * @param   MemObjToMap     The object to be map.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This must match the alignment.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjMapKernelTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed,
                                     size_t uAlignment, unsigned fProt, const char *pszTag);

/**
 * Maps a memory object into kernel virtual address space (default tag).
 *
 * The ability to map subsections of the object into kernel space is currently
 * not implemented on all platforms. All/Most of platforms supports mapping the
 * whole object into  kernel space.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if it's not possible to map a subsection of a
 *          memory object on this platform. When you hit this, try implement it.
 *
 * @param   pMemObj         Where to store the ring-0 memory object handle of the mapping object.
 * @param   MemObjToMap     The object to be map.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This must match the alignment.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 * @param   offSub          Where in the object to start mapping. If non-zero
 *                          the value must be page aligned and cbSub must be
 *                          non-zero as well.
 * @param   cbSub           The size of the part of the object to be mapped. If
 *                          zero the entire object is mapped. The value must be
 *                          page aligned.
 */
#define RTR0MemObjMapKernelEx(pMemObj, MemObjToMap, pvFixed, uAlignment, fProt, offSub, cbSub) \
    RTR0MemObjMapKernelExTag((pMemObj), (MemObjToMap), (pvFixed), (uAlignment), (fProt), (offSub), (cbSub), RTMEM_TAG)

/**
 * Maps a memory object into kernel virtual address space (custom tag).
 *
 * The ability to map subsections of the object into kernel space is currently
 * not implemented on all platforms. All/Most of platforms supports mapping the
 * whole object into  kernel space.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if it's not possible to map a subsection of a
 *          memory object on this platform. When you hit this, try implement it.
 *
 * @param   pMemObj         Where to store the ring-0 memory object handle of the mapping object.
 * @param   MemObjToMap     The object to be map.
 * @param   pvFixed         Requested address. (void *)-1 means any address. This must match the alignment.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 * @param   offSub          Where in the object to start mapping. If non-zero
 *                          the value must be page aligned and cbSub must be
 *                          non-zero as well.
 * @param   cbSub           The size of the part of the object to be mapped. If
 *                          zero the entire object is mapped. The value must be
 *                          page aligned.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjMapKernelExTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, void *pvFixed, size_t uAlignment,
                                       unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag);

/**
 * Maps a memory object into user virtual address space in the current process
 * (default tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle of the mapping object.
 * @param   MemObjToMap     The object to be map.
 * @param   R3PtrFixed      Requested address. (RTR3PTR)-1 means any address. This must match the alignment.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 * @param   R0Process       The process to map the memory into. NIL_R0PROCESS is an alias for the current one.
 */
#define RTR0MemObjMapUser(pMemObj, MemObjToMap, R3PtrFixed, uAlignment, fProt, R0Process) \
    RTR0MemObjMapUserTag((pMemObj), (MemObjToMap), (R3PtrFixed), (uAlignment), (fProt), (R0Process), RTMEM_TAG)

/**
 * Maps a memory object into user virtual address space in the current process
 * (custom tag).
 *
 * @returns IPRT status code.
 * @param   pMemObj         Where to store the ring-0 memory object handle of the mapping object.
 * @param   MemObjToMap     The object to be map.
 * @param   R3PtrFixed      Requested address. (RTR3PTR)-1 means any address. This must match the alignment.
 * @param   uAlignment      The alignment of the reserved memory.
 *                          Supported values are 0 (alias for PAGE_SIZE), PAGE_SIZE, _2M and _4M.
 * @param   fProt           Combination of RTMEM_PROT_* flags (except RTMEM_PROT_NONE).
 * @param   R0Process       The process to map the memory into. NIL_R0PROCESS is an alias for the current one.
 * @param   pszTag          Allocation tag used for statistics and such.
 */
RTR0DECL(int) RTR0MemObjMapUserTag(PRTR0MEMOBJ pMemObj, RTR0MEMOBJ MemObjToMap, RTR3PTR R3PtrFixed,
                                   size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process, const char *pszTag);

/**
 * Change the page level protection of one or more pages in a memory object.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if the OS doesn't provide any way to manipulate
 *          page level protection. The caller must handle this status code
 *          gracefully. (Note that it may also occur if the implementation is
 *          missing, in which case just go ahead and implement it.)
 *
 * @param   hMemObj         Memory object handle.
 * @param   offSub          Offset into the memory object. Must be page aligned.
 * @param   cbSub           Number of bytes to change the protection of. Must be
 *                          page aligned.
 * @param   fProt           Combination of RTMEM_PROT_* flags.
 */
RTR0DECL(int) RTR0MemObjProtect(RTR0MEMOBJ hMemObj, size_t offSub, size_t cbSub, uint32_t fProt);

#endif /* IN_RING0 */

/** @} */

RT_C_DECLS_END

#endif

