/** @file
 * IPRT - Memory Management and Manipulation.
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

#ifndef ___iprt_mem_h
#define ___iprt_mem_h


#include <iprt/cdefs.h>
#include <iprt/types.h>


#ifdef IN_RC
# error "There are no RTMem APIs available Guest Context!"
#endif


/** @defgroup grp_rt_mem    RTMem - Memory Management and Manipulation
 * @ingroup grp_rt
 * @{
 */

RT_C_DECLS_BEGIN

/** @def RTMEM_ALIGNMENT
 * The alignment of the memory blocks returned by RTMemAlloc(), RTMemAllocZ(),
 * RTMemRealloc(), RTMemTmpAlloc() and RTMemTmpAllocZ() for allocations greater
 * than RTMEM_ALIGNMENT.
 *
 * @note This alignment is not forced if the electric fence is active!
 */
#if defined(RT_OS_OS2)
# define RTMEM_ALIGNMENT    4
#else
# define RTMEM_ALIGNMENT    8
#endif

/** @def RTMEM_TAG
 * The default allocation tag used by the RTMem allocation APIs.
 *
 * When not defined before the inclusion of iprt/mem.h or iprt/memobj.h, this
 * will default to the pointer to the current file name.  The memory API will
 * make of use of this as pointer to a volatile but read-only string.
 * The alternative tag includes the line number for a more-detailed analysis.
 */
#ifndef RTMEM_TAG
# if 0
#  define RTMEM_TAG   (__FILE__ ":" RT_XSTR(__LINE__))
# else
#  define RTMEM_TAG   (__FILE__)
# endif
#endif


/** @name Allocate temporary memory.
 * @{ */
/**
 * Allocates temporary memory with default tag.
 *
 * Temporary memory blocks are used for not too large memory blocks which
 * are believed not to stick around for too long. Using this API instead
 * of RTMemAlloc() not only gives the heap manager room for optimization
 * but makes the code easier to read.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure, assertion raised in strict builds.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
#define RTMemTmpAlloc(cb)               RTMemTmpAllocTag((cb), RTMEM_TAG)

/**
 * Allocates temporary memory with custom tag.
 *
 * Temporary memory blocks are used for not too large memory blocks which
 * are believed not to stick around for too long. Using this API instead
 * of RTMemAlloc() not only gives the heap manager room for optimization
 * but makes the code easier to read.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure, assertion raised in strict builds.
 * @param   cb      Size in bytes of the memory block to allocated.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *)  RTMemTmpAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Allocates zero'd temporary memory with default tag.
 *
 * Same as RTMemTmpAlloc() but the memory will be zero'd.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure, assertion raised in strict builds.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
#define RTMemTmpAllocZ(cb)              RTMemTmpAllocZTag((cb), RTMEM_TAG)

/**
 * Allocates zero'd temporary memory with custom tag.
 *
 * Same as RTMemTmpAlloc() but the memory will be zero'd.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure, assertion raised in strict builds.
 * @param   cb      Size in bytes of the memory block to allocated.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *)  RTMemTmpAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Free temporary memory.
 *
 * @param   pv      Pointer to memory block.
 */
RTDECL(void)    RTMemTmpFree(void *pv) RT_NO_THROW_PROTO;

/** @}  */


/**
 * Allocates memory with default tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure, assertion raised in strict builds.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
#define RTMemAlloc(cb)                  RTMemAllocTag((cb), RTMEM_TAG)

/**
 * Allocates memory with custom tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure, assertion raised in strict builds.
 * @param   cb      Size in bytes of the memory block to allocated.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *)  RTMemAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Allocates zero'd memory with default tag.
 *
 * Instead of memset(pv, 0, sizeof()) use this when you want zero'd
 * memory. This keeps the code smaller and the heap can skip the memset
 * in about 0.42% of calls :-).
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocated.
 */
#define RTMemAllocZ(cb)                 RTMemAllocZTag((cb), RTMEM_TAG)

/**
 * Allocates zero'd memory with custom tag.
 *
 * Instead of memset(pv, 0, sizeof()) use this when you want zero'd
 * memory. This keeps the code smaller and the heap can skip the memset
 * in about 0.42% of calls :-).
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocated.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *)  RTMemAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Wrapper around RTMemAlloc for automatically aligning variable sized
 * allocations so that the various electric fence heaps works correctly.
 *
 * @returns See RTMemAlloc.
 * @param   cbUnaligned         The unaligned size.
 */
#define RTMemAllocVar(cbUnaligned)      RTMemAllocVarTag((cbUnaligned), RTMEM_TAG)

/**
 * Wrapper around RTMemAllocTag for automatically aligning variable sized
 * allocations so that the various electric fence heaps works correctly.
 *
 * @returns See RTMemAlloc.
 * @param   cbUnaligned         The unaligned size.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(void *) RTMemAllocVarTag(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Wrapper around RTMemAllocZ for automatically aligning variable sized
 * allocations so that the various electric fence heaps works correctly.
 *
 * @returns See RTMemAllocZ.
 * @param   cbUnaligned         The unaligned size.
 */
#define RTMemAllocZVar(cbUnaligned)     RTMemAllocZVarTag((cbUnaligned), RTMEM_TAG)

/**
 * Wrapper around RTMemAllocZTag for automatically aligning variable sized
 * allocations so that the various electric fence heaps works correctly.
 *
 * @returns See RTMemAllocZ.
 * @param   cbUnaligned         The unaligned size.
 * @param   pszTag              Allocation tag used for statistics and such.
 */
RTDECL(void *) RTMemAllocZVarTag(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Duplicates a chunk of memory into a new heap block (default tag).
 *
 * @returns New heap block with the duplicate data.
 * @returns NULL if we're out of memory.
 * @param   pvSrc   The memory to duplicate.
 * @param   cb      The amount of memory to duplicate.
 */
#define RTMemDup(pvSrc, cb)             RTMemDupTag((pvSrc), (cb), RTMEM_TAG)

/**
 * Duplicates a chunk of memory into a new heap block (custom tag).
 *
 * @returns New heap block with the duplicate data.
 * @returns NULL if we're out of memory.
 * @param   pvSrc   The memory to duplicate.
 * @param   cb      The amount of memory to duplicate.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *) RTMemDupTag(const void *pvSrc, size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Duplicates a chunk of memory into a new heap block with some additional
 * zeroed memory (default tag).
 *
 * @returns New heap block with the duplicate data.
 * @returns NULL if we're out of memory.
 * @param   pvSrc   The memory to duplicate.
 * @param   cbSrc   The amount of memory to duplicate.
 * @param   cbExtra The amount of extra memory to allocate and zero.
 */
#define RTMemDupEx(pvSrc, cbSrc, cbExtra)   RTMemDupExTag((pvSrc), (cbSrc), (cbExtra), RTMEM_TAG)

/**
 * Duplicates a chunk of memory into a new heap block with some additional
 * zeroed memory (default tag).
 *
 * @returns New heap block with the duplicate data.
 * @returns NULL if we're out of memory.
 * @param   pvSrc   The memory to duplicate.
 * @param   cbSrc   The amount of memory to duplicate.
 * @param   cbExtra The amount of extra memory to allocate and zero.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *) RTMemDupExTag(const void *pvSrc, size_t cbSrc, size_t cbExtra, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Reallocates memory with default tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   pvOld   The memory block to reallocate.
 * @param   cbNew   The new block size (in bytes).
 */
#define RTMemRealloc(pvOld, cbNew)          RTMemReallocTag((pvOld), (cbNew), RTMEM_TAG)

/**
 * Reallocates memory with custom tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   pvOld   The memory block to reallocate.
 * @param   cbNew   The new block size (in bytes).
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *)  RTMemReallocTag(void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Frees memory.
 *
 * @param   pv      Pointer to memory block.
 */
RTDECL(void)    RTMemFree(void *pv) RT_NO_THROW_PROTO;



/** @name RTR0MemAllocEx and RTR0MemAllocExTag flags.
 * @{ */
/** The returned memory should be zeroed. */
#define RTMEMALLOCEX_FLAGS_ZEROED           RT_BIT(0)
/** It must be load code into the returned memory block and execute it. */
#define RTMEMALLOCEX_FLAGS_EXEC             RT_BIT(1)
/** Allocation from any context.
 * Will return VERR_NOT_SUPPORTED if not supported.  */
#define RTMEMALLOCEX_FLAGS_ANY_CTX_ALLOC    RT_BIT(2)
/** Allocate the memory such that it can be freed from any context.
 * Will return VERR_NOT_SUPPORTED if not supported. */
#define RTMEMALLOCEX_FLAGS_ANY_CTX_FREE     RT_BIT(3)
/** Allocate and free from any context.
 * Will return VERR_NOT_SUPPORTED if not supported. */
#define RTMEMALLOCEX_FLAGS_ANY_CTX          (RTMEMALLOCEX_FLAGS_ANY_CTX_ALLOC | RTMEMALLOCEX_FLAGS_ANY_CTX_FREE)
/** Reachable by 16-bit address.
 * Will return VERR_NOT_SUPPORTED if not supported.  */
#define RTMEMALLOCEX_FLAGS_16BIT_REACH      RT_BIT(4)
/** Reachable by 32-bit address.
 * Will return VERR_NOT_SUPPORTED if not supported.  */
#define RTMEMALLOCEX_FLAGS_32BIT_REACH      RT_BIT(5)
/** Mask of valid flags. */
#define RTMEMALLOCEX_FLAGS_VALID_MASK       UINT32_C(0x0000003f)
/** Mask of valid flags for ring-0. */
#define RTMEMALLOCEX_FLAGS_VALID_MASK_R0    UINT32_C(0x0000000f)
/** @}  */

/**
 * Extended heap allocation API, default tag.
 *
 * @returns IPRT status code.
 * @retval  VERR_NO_MEMORY if we're out of memory.
 * @retval  VERR_NO_EXEC_MEMORY if we're out of executable memory.
 * @retval  VERR_NOT_SUPPORTED if any of the specified flags are unsupported.
 *
 * @param   cb                  The amount of memory to allocate.
 * @param   cbAlignment         The alignment requirements.  Use 0 to indicate
 *                              default alignment.
 * @param   fFlags              A combination of the RTMEMALLOCEX_FLAGS_XXX
 *                              defines.
 * @param   ppv                 Where to return the memory.
 */
#define RTMemAllocEx(cb, cbAlignment, fFlags, ppv) RTMemAllocExTag((cb), (cbAlignment), (fFlags), RTMEM_TAG, (ppv))

/**
 * Extended heap allocation API, custom tag.
 *
 * Depending on the implementation, using this function may add extra overhead,
 * so use the simpler APIs where ever possible.
 *
 * @returns IPRT status code.
 * @retval  VERR_NO_MEMORY if we're out of memory.
 * @retval  VERR_NO_EXEC_MEMORY if we're out of executable memory.
 * @retval  VERR_NOT_SUPPORTED if any of the specified flags are unsupported.
 *
 * @param   cb                  The amount of memory to allocate.
 * @param   cbAlignment         The alignment requirements.  Use 0 to indicate
 *                              default alignment.
 * @param   fFlags              A combination of the RTMEMALLOCEX_FLAGS_XXX
 *                              defines.
 * @param   pszTag              The tag.
 * @param   ppv                 Where to return the memory.
 */
RTDECL(int) RTMemAllocExTag(size_t cb, size_t cbAlignment, uint32_t fFlags, const char *pszTag, void **ppv) RT_NO_THROW_PROTO;

/**
 * For freeing memory allocated by RTMemAllocEx or RTMemAllocExTag.
 *
 * @param   pv                  What to free, NULL is fine.
 * @param   cb                  The amount of allocated memory.
 */
RTDECL(void) RTMemFreeEx(void *pv, size_t cb) RT_NO_THROW_PROTO;



/**
 * Allocates memory which may contain code (default tag).
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocate.
 */
#define RTMemExecAlloc(cb)              RTMemExecAllocTag((cb), RTMEM_TAG)

/**
 * Allocates memory which may contain code (custom tag).
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocate.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *)  RTMemExecAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Free executable/read/write memory allocated by RTMemExecAlloc().
 *
 * @param   pv      Pointer to memory block.
 * @param   cb      The allocation size.
 */
RTDECL(void)    RTMemExecFree(void *pv, size_t cb) RT_NO_THROW_PROTO;

#if defined(IN_RING0) && defined(RT_ARCH_AMD64) && defined(RT_OS_LINUX)
/**
 * Donate read+write+execute memory to the exec heap.
 *
 * This API is specific to AMD64 and Linux/GNU. A kernel module that desires to
 * use RTMemExecAlloc on AMD64 Linux/GNU will have to donate some statically
 * allocated memory in the module if it wishes for GCC generated code to work.
 * GCC can only generate modules that work in the address range ~2GB to ~0
 * currently.
 *
 * The API only accept one single donation.
 *
 * @returns IPRT status code.
 * @param   pvMemory    Pointer to the memory block.
 * @param   cb          The size of the memory block.
 */
RTR0DECL(int) RTR0MemExecDonate(void *pvMemory, size_t cb) RT_NO_THROW_PROTO;
#endif /* R0+AMD64+LINUX */

/**
 * Allocate page aligned memory with default tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL if we're out of memory.
 * @param   cb  Size of the memory block. Will be rounded up to page size.
 */
#define RTMemPageAlloc(cb)              RTMemPageAllocTag((cb), RTMEM_TAG)

/**
 * Allocate page aligned memory with custom tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL if we're out of memory.
 * @param   cb  Size of the memory block. Will be rounded up to page size.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *) RTMemPageAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Allocate zero'd page aligned memory with default tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL if we're out of memory.
 * @param   cb  Size of the memory block. Will be rounded up to page size.
 */
#define RTMemPageAllocZ(cb)             RTMemPageAllocZTag((cb), RTMEM_TAG)

/**
 * Allocate zero'd page aligned memory with custom tag.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL if we're out of memory.
 * @param   cb  Size of the memory block. Will be rounded up to page size.
 * @param   pszTag  Allocation tag used for statistics and such.
 */
RTDECL(void *) RTMemPageAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Free a memory block allocated with RTMemPageAlloc() or RTMemPageAllocZ().
 *
 * @param   pv      Pointer to the block as it was returned by the allocation function.
 *                  NULL will be ignored.
 * @param   cb      The allocation size.  Will be rounded up to page size.
 *                  Ignored if @a pv is NULL.
 */
RTDECL(void) RTMemPageFree(void *pv, size_t cb) RT_NO_THROW_PROTO;

/** Page level protection flags for RTMemProtect().
 * @{
 */
/** No access at all. */
#define RTMEM_PROT_NONE   0
/** Read access. */
#define RTMEM_PROT_READ   1
/** Write access. */
#define RTMEM_PROT_WRITE  2
/** Execute access. */
#define RTMEM_PROT_EXEC   4
/** @} */

/**
 * Change the page level protection of a memory region.
 *
 * @returns iprt status code.
 * @param   pv          Start of the region. Will be rounded down to nearest page boundary.
 * @param   cb          Size of the region. Will be rounded up to the nearest page boundary.
 * @param   fProtect    The new protection, a combination of the RTMEM_PROT_* defines.
 */
RTDECL(int) RTMemProtect(void *pv, size_t cb, unsigned fProtect) RT_NO_THROW_PROTO;

/**
 * Goes thru some pains to make sure the specified memory block is thoroughly
 * scrambled.
 *
 * @param   pv          The start of the memory block.
 * @param   cb          The size of the memory block.
 * @param   cMinPasses  The minimum number of passes to make.
 */
RTDECL(void) RTMemWipeThoroughly(void *pv, size_t cb, size_t cMinPasses) RT_NO_THROW_PROTO;

#ifdef IN_RING0

/**
 * Allocates physical contiguous memory (below 4GB).
 * The allocation is page aligned and the content is undefined.
 *
 * @returns Pointer to the memory block. This is page aligned.
 * @param   pPhys   Where to store the physical address.
 * @param   cb      The allocation size in bytes. This is always
 *                  rounded up to PAGE_SIZE.
 */
RTR0DECL(void *) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb) RT_NO_THROW_PROTO;

/**
 * Frees memory allocated ysing RTMemContAlloc().
 *
 * @param   pv      Pointer to return from RTMemContAlloc().
 * @param   cb      The cb parameter passed to RTMemContAlloc().
 */
RTR0DECL(void) RTMemContFree(void *pv, size_t cb) RT_NO_THROW_PROTO;

/**
 * Copy memory from an user mode buffer into a kernel buffer.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_ACCESS_DENIED on error.
 *
 * @param   pvDst       The kernel mode destination address.
 * @param   R3PtrSrc    The user mode source address.
 * @param   cb          The number of bytes to copy.
 */
RTR0DECL(int) RTR0MemUserCopyFrom(void *pvDst, RTR3PTR R3PtrSrc, size_t cb);

/**
 * Copy memory from a kernel buffer into a user mode one.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_ACCESS_DENIED on error.
 *
 * @param   R3PtrDst    The user mode destination address.
 * @param   pvSrc       The kernel mode source address.
 * @param   cb          The number of bytes to copy.
 */
RTR0DECL(int) RTR0MemUserCopyTo(RTR3PTR R3PtrDst, void const *pvSrc, size_t cb);

/**
 * Tests if the specified address is in the user addressable range.
 *
 * This function does not check whether the memory at that address is accessible
 * or anything of that sort, only if the address it self is in the user mode
 * range.
 *
 * @returns true if it's in the user addressable range. false if not.
 * @param   R3Ptr       The user mode pointer to test.
 *
 * @remarks Some systems may have overlapping kernel and user address ranges.
 *          One prominent example of this is the x86 version of Mac OS X. Use
 *          RTR0MemAreKrnlAndUsrDifferent() to check.
 */
RTR0DECL(bool) RTR0MemUserIsValidAddr(RTR3PTR R3Ptr);

/**
 * Tests if the specified address is in the kernel mode range.
 *
 * This function does not check whether the memory at that address is accessible
 * or anything of that sort, only if the address it self is in the kernel mode
 * range.
 *
 * @returns true if it's in the kernel range. false if not.
 * @param   pv          The alleged kernel mode pointer.
 *
 * @remarks Some systems may have overlapping kernel and user address ranges.
 *          One prominent example of this is the x86 version of Mac OS X. Use
 *          RTR0MemAreKrnlAndUsrDifferent() to check.
 */
RTR0DECL(bool) RTR0MemKernelIsValidAddr(void *pv);

/**
 * Are user mode and kernel mode address ranges distinctly different.
 *
 * This determines whether RTR0MemKernelIsValidAddr and RTR0MemUserIsValidAddr
 * can be used for deciding whether some arbitrary address is a user mode or a
 * kernel mode one.
 *
 * @returns true if they are, false if not.
 */
RTR0DECL(bool) RTR0MemAreKrnlAndUsrDifferent(void);

/**
 * Copy memory from an potentially unsafe kernel mode location and into a safe
 * (kernel) buffer.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_ACCESS_DENIED on error.
 * @retval  VERR_NOT_SUPPORTED if not (yet) supported.
 *
 * @param   pvDst       The destination address (safe).
 * @param   pvSrc       The source address (potentially unsafe).
 * @param   cb          The number of bytes to copy.
 */
RTR0DECL(int) RTR0MemKernelCopyFrom(void *pvDst, void const *pvSrc, size_t cb);

/**
 * Copy from a safe (kernel) buffer and to a potentially unsafe kenrel mode
 * location.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_ACCESS_DENIED on error.
 * @retval  VERR_NOT_SUPPORTED if not (yet) supported.
 *
 * @param   pvDst       The destination address (potentially unsafe).
 * @param   pvSrc       The source address (safe).
 * @param   cb          The number of bytes to copy.
 */
RTR0DECL(int) RTR0MemKernelCopyTo(void *pvDst, void const *pvSrc, size_t cb);

#endif /* IN_RING0 */


/** @name Electrical Fence Version of some APIs.
 * @{
 */

/**
 * Same as RTMemTmpAllocTag() except that it's fenced.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocate.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.
 *                  Use RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *)  RTMemEfTmpAlloc(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemTmpAllocZTag() except that it's fenced.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocate.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *)  RTMemEfTmpAllocZ(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemTmpFree() except that it's for fenced memory.
 *
 * @param   pv      Pointer to memory block.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void)    RTMemEfTmpFree(void *pv, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemAllocTag() except that it's fenced.
 *
 * @returns Pointer to the allocated memory. Free with RTMemEfFree().
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocate.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *)  RTMemEfAlloc(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemAllocZTag() except that it's fenced.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cb      Size in bytes of the memory block to allocate.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *)  RTMemEfAllocZ(size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemAllocVarTag() except that it's fenced.
 *
 * @returns Pointer to the allocated memory. Free with RTMemEfFree().
 * @returns NULL on failure.
 * @param   cbUnaligned Size in bytes of the memory block to allocate.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *)  RTMemEfAllocVar(size_t cbUnaligned, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemAllocZVarTag() except that it's fenced.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   cbUnaligned Size in bytes of the memory block to allocate.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *)  RTMemEfAllocZVar(size_t cbUnaligned, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemReallocTag() except that it's fenced.
 *
 * @returns Pointer to the allocated memory.
 * @returns NULL on failure.
 * @param   pvOld   The memory block to reallocate.
 * @param   cbNew   The new block size (in bytes).
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *)  RTMemEfRealloc(void *pvOld, size_t cbNew, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Free memory allocated by any of the RTMemEf* allocators.
 *
 * @param   pv      Pointer to memory block.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void)    RTMemEfFree(void *pv, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemDupTag() except that it's fenced.
 *
 * @returns New heap block with the duplicate data.
 * @returns NULL if we're out of memory.
 * @param   pvSrc   The memory to duplicate.
 * @param   cb      The amount of memory to duplicate.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *) RTMemEfDup(const void *pvSrc, size_t cb, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/**
 * Same as RTMemEfDupExTag except that it's fenced.
 *
 * @returns New heap block with the duplicate data.
 * @returns NULL if we're out of memory.
 * @param   pvSrc   The memory to duplicate.
 * @param   cbSrc   The amount of memory to duplicate.
 * @param   cbExtra The amount of extra memory to allocate and zero.
 * @param   pszTag  Allocation tag used for statistics and such.
 * @param   SRC_POS The source position where call is being made from.  Use
 *                  RT_SRC_POS when possible.  Optional.
 */
RTDECL(void *) RTMemEfDupEx(const void *pvSrc, size_t cbSrc, size_t cbExtra, const char *pszTag, RT_SRC_POS_DECL) RT_NO_THROW_PROTO;

/** @def RTMEM_WRAP_SOME_NEW_AND_DELETE_TO_EF
 * Define RTMEM_WRAP_SOME_NEW_AND_DELETE_TO_EF to enable electric fence new and
 * delete operators for classes which uses the RTMEMEF_NEW_AND_DELETE_OPERATORS
 * macro.
 */
/** @def RTMEMEF_NEW_AND_DELETE_OPERATORS
 * Defines the electric fence new and delete operators for a class when
 * RTMEM_WRAP_SOME_NEW_AND_DELETE_TO_EF is define.
 */
/** @def RTR0MEMEF_NEW_AND_DELETE_OPERATORS_IOKIT
 * Defines the electric fence new and delete operators for an IOKit class when
 * RTMEM_WRAP_SOME_NEW_AND_DELETE_TO_EF is define.
 *
 * This differs from RTMEMEF_NEW_AND_DELETE_OPERATORS in that the memory we
 * allocate is initialized to zero.  It is also assuming we don't have nothrow
 * variants and exceptions, so fewer variations.
 */
#if defined(RTMEM_WRAP_SOME_NEW_AND_DELETE_TO_EF) && !defined(RTMEM_NO_WRAP_SOME_NEW_AND_DELETE_TO_EF)
# if defined(RT_EXCEPTIONS_ENABLED)
#  define RTMEMEF_NEW_AND_DELETE_OPERATORS() \
        void *operator new(size_t cb) RT_THROW(std::bad_alloc) \
        { \
            void *pv = RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
            if (RT_LIKELY(pv)) \
                return pv; \
            throw std::bad_alloc(); \
        } \
        void *operator new(size_t cb, const std::nothrow_t &nothrow_constant) RT_NO_THROW_DEF \
        { \
            NOREF(nothrow_constant); \
            return RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
        } \
        void *operator new[](size_t cb) RT_THROW(std::bad_alloc) \
        { \
            void *pv = RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
            if (RT_LIKELY(pv)) \
                return pv; \
            throw std::bad_alloc(); \
        } \
        void *operator new[](size_t cb, const std::nothrow_t &nothrow_constant) RT_NO_THROW_DEF \
        { \
            NOREF(nothrow_constant); \
            return RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
        } \
        \
        void operator delete(void *pv) RT_NO_THROW_DEF \
        { \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        void operator delete(void *pv, const std::nothrow_t &nothrow_constant) RT_NO_THROW_DEF \
        { \
            NOREF(nothrow_constant); \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        void operator delete[](void *pv) RT_NO_THROW_DEF \
        { \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        void operator delete[](void *pv, const std::nothrow_t &nothrow_constant) RT_NO_THROW_DEF \
        { \
            NOREF(nothrow_constant); \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        \
        typedef int UsingElectricNewAndDeleteOperators
# else
#  define RTMEMEF_NEW_AND_DELETE_OPERATORS() \
        void *operator new(size_t cb) \
        { \
            return RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
        } \
        void *operator new(size_t cb, const std::nothrow_t &nothrow_constant) \
        { \
            NOREF(nothrow_constant); \
            return RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
        } \
        void *operator new[](size_t cb) \
        { \
            return RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
        } \
        void *operator new[](size_t cb, const std::nothrow_t &nothrow_constant) \
        { \
            NOREF(nothrow_constant); \
            return RTMemEfAlloc(cb, RTMEM_TAG, RT_SRC_POS); \
        } \
        \
        void operator delete(void *pv) \
        { \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        void operator delete(void *pv, const std::nothrow_t &nothrow_constant) \
        { \
            NOREF(nothrow_constant); \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        void operator delete[](void *pv) \
        { \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        void operator delete[](void *pv, const std::nothrow_t &nothrow_constant) \
        { \
            NOREF(nothrow_constant); \
            RTMemEfFree(pv, RT_SRC_POS); \
        } \
        \
        typedef int UsingElectricNewAndDeleteOperators
# endif
# define RTR0MEMEF_NEW_AND_DELETE_OPERATORS_IOKIT() \
    void *operator new(size_t cb) \
    { \
        return RTMemEfAllocZ(cb, RTMEM_TAG, RT_SRC_POS); \
    } \
    void *operator new[](size_t cb) \
    { \
        return RTMemEfAllocZ(cb, RTMEM_TAG, RT_SRC_POS); \
    } \
    \
    void operator delete(void *pv) \
    { \
        RTMemEfFree(pv, RT_SRC_POS); \
    } \
    void operator delete[](void *pv) \
    { \
        RTMemEfFree(pv, RT_SRC_POS); \
    } \
    \
    typedef int UsingElectricNewAndDeleteOperators
#else
# define RTMEMEF_NEW_AND_DELETE_OPERATORS() \
        typedef int UsingDefaultNewAndDeleteOperators
# define RTR0MEMEF_NEW_AND_DELETE_OPERATORS_IOKIT() \
        typedef int UsingDefaultNewAndDeleteOperators
#endif
#ifdef DOXYGEN_RUNNING
# define RTMEM_WRAP_SOME_NEW_AND_DELETE_TO_EF
#endif

/** @def RTMEM_WRAP_TO_EF_APIS
 * Define RTMEM_WRAP_TO_EF_APIS to wrap RTMem APIs to RTMemEf APIs.
 */
#if defined(RTMEM_WRAP_TO_EF_APIS) && !defined(RTMEM_NO_WRAP_TO_EF_APIS) \
 && ( defined(IN_RING3) || ( defined(IN_RING0) && !defined(IN_RING0_AGNOSTIC) && (defined(RT_OS_DARWIN) || 0) ) )
# define RTMemTmpAllocTag(cb, pszTag)                   RTMemEfTmpAlloc((cb), (pszTag), RT_SRC_POS)
# define RTMemTmpAllocZTag(cb, pszTag)                  RTMemEfTmpAllocZ((cb), (pszTag), RT_SRC_POS)
# define RTMemTmpFree(pv)                               RTMemEfTmpFree((pv), RT_SRC_POS)
# define RTMemAllocTag(cb, pszTag)                      RTMemEfAlloc((cb), (pszTag), RT_SRC_POS)
# define RTMemAllocZTag(cb, pszTag)                     RTMemEfAllocZ((cb), (pszTag), RT_SRC_POS)
# define RTMemAllocVarTag(cbUnaligned, pszTag)          RTMemEfAllocVar((cbUnaligned), (pszTag), RT_SRC_POS)
# define RTMemAllocZVarTag(cbUnaligned, pszTag)         RTMemEfAllocZVar((cbUnaligned), (pszTag), RT_SRC_POS)
# define RTMemReallocTag(pvOld, cbNew, pszTag)          RTMemEfRealloc((pvOld), (cbNew), (pszTag), RT_SRC_POS)
# define RTMemFree(pv)                                  RTMemEfFree((pv), RT_SRC_POS)
# define RTMemDupTag(pvSrc, cb, pszTag)                 RTMemEfDup((pvSrc), (cb), (pszTag), RT_SRC_POS)
# define RTMemDupExTag(pvSrc, cbSrc, cbExtra, pszTag)   RTMemEfDupEx((pvSrc), (cbSrc), (cbExtra), (pszTag), RT_SRC_POS)
#endif
#ifdef DOXYGEN_RUNNING
# define RTMEM_WRAP_TO_EF_APIS
#endif

/**
 * Fenced drop-in replacement for RTMemTmpAllocTag.
 * @copydoc RTMemTmpAllocTag
 */
RTDECL(void *)  RTMemEfTmpAllocNP(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemTmpAllocZTag.
 * @copydoc RTMemTmpAllocZTag
 */
RTDECL(void *)  RTMemEfTmpAllocZNP(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemTmpFreeTag.
 * @copydoc RTMemTmpFree
 */
RTDECL(void)    RTMemEfTmpFreeNP(void *pv) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemAllocTag.
 * @copydoc RTMemAllocTag
 */
RTDECL(void *)  RTMemEfAllocNP(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemAllocZTag.
 * @copydoc RTMemAllocZTag
 */
RTDECL(void *)  RTMemEfAllocZNP(size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemAllocVarTag
 * @copydoc RTMemAllocVarTag
 */
RTDECL(void *)  RTMemEfAllocVarNP(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemAllocZVarTag.
 * @copydoc RTMemAllocZVarTag
 */
RTDECL(void *)  RTMemEfAllocZVarNP(size_t cbUnaligned, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemReallocTag.
 * @copydoc RTMemReallocTag
 */
RTDECL(void *)  RTMemEfReallocNP(void *pvOld, size_t cbNew, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemFree.
 * @copydoc RTMemFree
 */
RTDECL(void)    RTMemEfFreeNP(void *pv) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemDupExTag.
 * @copydoc RTMemDupTag
 */
RTDECL(void *) RTMemEfDupNP(const void *pvSrc, size_t cb, const char *pszTag) RT_NO_THROW_PROTO;

/**
 * Fenced drop-in replacement for RTMemDupExTag.
 * @copydoc RTMemDupExTag
 */
RTDECL(void *) RTMemEfDupExNP(const void *pvSrc, size_t cbSrc, size_t cbExtra, const char *pszTag) RT_NO_THROW_PROTO;

/** @} */

RT_C_DECLS_END

/** @} */


#endif

