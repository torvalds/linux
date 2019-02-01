/** @file
 * IPRT - Heap Implementations
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

#ifndef IPRT_INCLUDED_heap_h
#define IPRT_INCLUDED_heap_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_heap       RTHeap - Heap Implementations
 * @ingroup grp_rt
 * @{
 */


/** @defgroup grp_rt_heap_simple    RTHeapSimple - Simple Heap
 * @{
 */

/**
 * Initializes the heap.
 *
 * @returns IPRT status code.
 * @param   pHeap       Where to store the heap anchor block on success.
 * @param   pvMemory    Pointer to the heap memory.
 * @param   cbMemory    The size of the heap memory.
 */
RTDECL(int) RTHeapSimpleInit(PRTHEAPSIMPLE pHeap, void *pvMemory, size_t cbMemory);

/**
 * Merge two simple heaps into one.
 *
 * The requirement is of course that they next two each other memory wise.
 *
 * @returns IPRT status code.
 * @param   pHeap       Where to store the handle to the merged heap on success.
 * @param   Heap1       Handle to the first heap.
 * @param   Heap2       Handle to the second heap.
 * @remark  This API isn't implemented yet.
 */
RTDECL(int) RTHeapSimpleMerge(PRTHEAPSIMPLE pHeap, RTHEAPSIMPLE Heap1, RTHEAPSIMPLE Heap2);

/**
 * Relocater the heap internal structures after copying it to a new location.
 *
 * This can be used when loading a saved heap.
 *
 * @returns IPRT status code.
 * @param   hHeap       Heap handle that has already been adjusted by to the new
 *                      location.  That is to say, when calling
 *                      RTHeapSimpleInit, the caller must note the offset of the
 *                      returned heap handle into the heap memory.  This offset
 *                      must be used when calcuating the handle value for the
 *                      new location.  The offset may in some cases not be zero!
 * @param   offDelta    The delta between the new and old location, i.e. what
 *                      should be added to the internal pointers.
 */
RTDECL(int) RTHeapSimpleRelocate(RTHEAPSIMPLE hHeap, uintptr_t offDelta);

/**
 * Allocates memory from the specified simple heap.
 *
 * @returns Pointer to the allocated memory block on success.
 * @returns NULL if the request cannot be satisfied. (A VERR_NO_MEMORY condition.)
 *
 * @param   Heap        The heap to allocate the memory on.
 * @param   cb          The requested heap block size.
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default alignment.
 *                      Must be a power of 2.
 */
RTDECL(void *) RTHeapSimpleAlloc(RTHEAPSIMPLE Heap, size_t cb, size_t cbAlignment);

/**
 * Allocates zeroed memory from the specified simple heap.
 *
 * @returns Pointer to the allocated memory block on success.
 * @returns NULL if the request cannot be satisfied. (A VERR_NO_MEMORY condition.)
 *
 * @param   Heap        The heap to allocate the memory on.
 * @param   cb          The requested heap block size.
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default alignment.
 *                      Must be a power of 2.
 */
RTDECL(void *) RTHeapSimpleAllocZ(RTHEAPSIMPLE Heap, size_t cb, size_t cbAlignment);

/**
 * Reallocates / Allocates / Frees a heap block.
 *
 * @param   Heap        The heap. This is optional and will only be used for strict assertions.
 * @param   pv          The heap block returned by RTHeapSimple. If NULL it behaves like RTHeapSimpleAlloc().
 * @param   cbNew       The new size of the heap block. If NULL it behaves like RTHeapSimpleFree().
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default alignment.
 *                      Must be a power of 2.
 * @remark  This API isn't implemented yet.
 */
RTDECL(void *) RTHeapSimpleRealloc(RTHEAPSIMPLE Heap, void *pv, size_t cbNew, size_t cbAlignment);

/**
 * Reallocates / Allocates / Frees a heap block, zeroing any new bits.
 *
 * @param   Heap        The heap. This is optional and will only be used for strict assertions.
 * @param   pv          The heap block returned by RTHeapSimple. If NULL it behaves like RTHeapSimpleAllocZ().
 * @param   cbNew       The new size of the heap block. If NULL it behaves like RTHeapSimpleFree().
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default alignment.
 *                      Must be a power of 2.
 * @remark  This API isn't implemented yet.
 */
RTDECL(void *) RTHeapSimpleReallocZ(RTHEAPSIMPLE Heap, void *pv, size_t cbNew, size_t cbAlignment);

/**
 * Frees memory allocated from a simple heap.
 *
 * @param   Heap    The heap. This is optional and will only be used for strict assertions.
 * @param   pv      The heap block returned by RTHeapSimple
 */
RTDECL(void) RTHeapSimpleFree(RTHEAPSIMPLE Heap, void *pv);

/**
 * Gets the size of the specified heap block.
 *
 * @returns The actual size of the heap block.
 * @returns 0 if \a pv is NULL or it doesn't point to a valid heap block. An invalid \a pv
 *          can also cause traps or trigger assertions.
 * @param   Heap    The heap. This is optional and will only be used for strict assertions.
 * @param   pv      The heap block returned by RTHeapSimple
 */
RTDECL(size_t) RTHeapSimpleSize(RTHEAPSIMPLE Heap, void *pv);

/**
 * Gets the size of the heap.
 *
 * This size includes all the internal heap structures. So, even if the heap is
 * empty the RTHeapSimpleGetFreeSize() will never reach the heap size returned
 * by this function.
 *
 * @returns The heap size.
 * @returns 0 if heap was safely detected as being bad.
 * @param   Heap    The heap.
 */
RTDECL(size_t) RTHeapSimpleGetHeapSize(RTHEAPSIMPLE Heap);

/**
 * Returns the sum of all free heap blocks.
 *
 * This is the amount of memory you can theoretically allocate
 * if you do allocations exactly matching the free blocks.
 *
 * @returns The size of the free blocks.
 * @returns 0 if heap was safely detected as being bad.
 * @param   Heap    The heap.
 */
RTDECL(size_t) RTHeapSimpleGetFreeSize(RTHEAPSIMPLE Heap);

/**
 * Printf like callbaclk function for RTHeapSimpleDump.
 * @param   pszFormat   IPRT format string.
 * @param   ...         Format arguments.
 */
typedef DECLCALLBACK(void) FNRTHEAPSIMPLEPRINTF(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
/** Pointer to a FNRTHEAPSIMPLEPRINTF function. */
typedef FNRTHEAPSIMPLEPRINTF *PFNRTHEAPSIMPLEPRINTF;

/**
 * Dumps the hypervisor heap.
 *
 * @param   Heap        The heap handle.
 * @param   pfnPrintf   Printf like function that groks IPRT formatting.
 */
RTDECL(void) RTHeapSimpleDump(RTHEAPSIMPLE Heap, PFNRTHEAPSIMPLEPRINTF pfnPrintf);

/** @}  */



/** @defgroup grp_rt_heap_offset    RTHeapOffset - Offset Based Heap
 *
 * This is a variation on the simple heap that doesn't use pointers internally
 * and therefore can be saved and restored without any extra effort.
 *
 * @{
 */

/**
 * Initializes the heap.
 *
 * @returns IPRT status code.
 * @param   phHeap      Where to store the heap anchor block on success.
 * @param   pvMemory    Pointer to the heap memory.
 * @param   cbMemory    The size of the heap memory.
 */
RTDECL(int) RTHeapOffsetInit(PRTHEAPOFFSET phHeap, void *pvMemory, size_t cbMemory);

/**
 * Merge two simple heaps into one.
 *
 * The requirement is of course that they next two each other memory wise.
 *
 * @returns IPRT status code.
 * @param   phHeap      Where to store the handle to the merged heap on success.
 * @param   hHeap1      Handle to the first heap.
 * @param   hHeap2      Handle to the second heap.
 * @remark  This API isn't implemented yet.
 */
RTDECL(int) RTHeapOffsetMerge(PRTHEAPOFFSET phHeap, RTHEAPOFFSET hHeap1, RTHEAPOFFSET hHeap2);

/**
 * Allocates memory from the specified simple heap.
 *
 * @returns Pointer to the allocated memory block on success.
 * @returns NULL if the request cannot be satisfied. (A VERR_NO_MEMORY condition.)
 *
 * @param   hHeap       The heap to allocate the memory on.
 * @param   cb          The requested heap block size.
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default alignment.
 *                      Must be a power of 2.
 */
RTDECL(void *) RTHeapOffsetAlloc(RTHEAPOFFSET hHeap, size_t cb, size_t cbAlignment);

/**
 * Allocates zeroed memory from the specified simple heap.
 *
 * @returns Pointer to the allocated memory block on success.
 * @returns NULL if the request cannot be satisfied. (A VERR_NO_MEMORY condition.)
 *
 * @param   hHeap       The heap to allocate the memory on.
 * @param   cb          The requested heap block size.
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default
 *                      alignment. Must be a power of 2.
 */
RTDECL(void *) RTHeapOffsetAllocZ(RTHEAPOFFSET hHeap, size_t cb, size_t cbAlignment);

/**
 * Reallocates / Allocates / Frees a heap block.
 *
 * @param   hHeap       The heap handle. This is optional and will only be used
 *                      for strict assertions.
 * @param   pv          The heap block returned by RTHeapOffset. If NULL it
 *                      behaves like RTHeapOffsetAlloc().
 * @param   cbNew       The new size of the heap block. If NULL it behaves like
 *                      RTHeapOffsetFree().
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default
 *                      alignment. Must be a power of 2.
 * @remark  This API isn't implemented yet.
 */
RTDECL(void *) RTHeapOffsetRealloc(RTHEAPOFFSET hHeap, void *pv, size_t cbNew, size_t cbAlignment);

/**
 * Reallocates / Allocates / Frees a heap block, zeroing any new bits.
 *
 * @param   hHeap       The heap handle. This is optional and will only be used
 *                      for strict assertions.
 * @param   pv          The heap block returned by RTHeapOffset. If NULL it
 *                      behaves like RTHeapOffsetAllocZ().
 * @param   cbNew       The new size of the heap block. If NULL it behaves like
 *                      RTHeapOffsetFree().
 * @param   cbAlignment The requested heap block alignment. Pass 0 for default
 *                      alignment. Must be a power of 2.
 * @remark  This API isn't implemented yet.
 */
RTDECL(void *) RTHeapOffsetReallocZ(RTHEAPOFFSET hHeap, void *pv, size_t cbNew, size_t cbAlignment);

/**
 * Frees memory allocated from a simple heap.
 *
 * @param   hHeap       The heap handle. This is optional and will only be used
 *                      for strict assertions.
 * @param   pv          The heap block returned by RTHeapOffset
 */
RTDECL(void) RTHeapOffsetFree(RTHEAPOFFSET hHeap, void *pv);

/**
 * Gets the size of the specified heap block.
 *
 * @returns The actual size of the heap block.
 * @returns 0 if \a pv is NULL or it doesn't point to a valid heap block. An
 *          invalid \a pv can also cause traps or trigger assertions.
 *
 * @param   hHeap       The heap handle. This is optional and will only be used
 *                      for strict assertions.
 * @param   pv          The heap block returned by RTHeapOffset
 */
RTDECL(size_t) RTHeapOffsetSize(RTHEAPOFFSET hHeap, void *pv);

/**
 * Gets the size of the heap.
 *
 * This size includes all the internal heap structures. So, even if the heap is
 * empty the RTHeapOffsetGetFreeSize() will never reach the heap size returned
 * by this function.
 *
 * @returns The heap size.
 * @returns 0 if heap was safely detected as being bad.
 * @param   hHeap       The heap handle.
 */
RTDECL(size_t) RTHeapOffsetGetHeapSize(RTHEAPOFFSET hHeap);

/**
 * Returns the sum of all free heap blocks.
 *
 * This is the amount of memory you can theoretically allocate
 * if you do allocations exactly matching the free blocks.
 *
 * @returns The size of the free blocks.
 * @returns 0 if heap was safely detected as being bad.
 * @param   hHeap       The heap handle.
 */
RTDECL(size_t) RTHeapOffsetGetFreeSize(RTHEAPOFFSET hHeap);

/**
 * Printf like callbaclk function for RTHeapOffsetDump.
 * @param   pszFormat   IPRT format string.
 * @param   ...         Format arguments.
 */
typedef DECLCALLBACK(void) FNRTHEAPOFFSETPRINTF(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);
/** Pointer to a FNRTHEAPOFFSETPRINTF function. */
typedef FNRTHEAPOFFSETPRINTF *PFNRTHEAPOFFSETPRINTF;

/**
 * Dumps the hypervisor heap.
 *
 * @param   hHeap       The heap handle.
 * @param   pfnPrintf   Printf like function that groks IPRT formatting.
 */
RTDECL(void) RTHeapOffsetDump(RTHEAPOFFSET hHeap, PFNRTHEAPOFFSETPRINTF pfnPrintf);

/** @}  */

/** @}  */
RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_heap_h */

