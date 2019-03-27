/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/






/**
 * @file
 *
 * This file provides prototypes for the memory management library functions.
 * Two different allocators are provided: an arena based allocator that is derived from a
 * modified version of ptmalloc2 (used in glibc), and a zone allocator for allocating fixed
 * size memory blocks.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_MALLOC_H__
#define __CVMX_MALLOC_H__

#include "cvmx-spinlock.h"
#ifdef __cplusplus
extern "C" {
#endif


struct malloc_state; /* forward declaration */
typedef struct malloc_state *cvmx_arena_list_t;


#ifndef CVMX_BUILD_FOR_LINUX_USER
/**
 * Creates an arena from the memory region specified and adds it
 * to the supplied arena list.
 *
 * @param arena_list Pointer to an arena list to add new arena to.
 *                   If NULL, new list is created.
 * @param ptr        pointer to memory region to create arena from
 *
 * @param size       Size of memory region available at ptr in bytes.
 *
 * @return -1 on Failure
 *         0 on success
 */
int cvmx_add_arena(cvmx_arena_list_t *arena_list, void *ptr, size_t size);

/**
 * allocate buffer from an arena list
 *
 * @param arena_list arena list to allocate buffer from
 * @param size       size of buffer to allocate (in bytes)
 *
 * @return pointer to buffer or NULL if allocation failed
 */
void *cvmx_malloc(cvmx_arena_list_t arena_list, size_t size);
/**
 * Allocate zero initialized buffer
 *
 * @param arena_list arena list to allocate from
 * @param n          number of elements
 * @param elem_size  size of elementes
 *
 * @return pointer to (n*elem_size) byte zero initialized buffer or NULL
 *         on allocation failure
 */
void *cvmx_calloc(cvmx_arena_list_t arena_list, size_t n, size_t elem_size);
/**
 * attempt to increase the size of an already allocated buffer
 * This function may allocate a new buffer and copy
 * the data if current buffer can't be extended.
 *
 * @param arena_list arena list to allocate from
 * @param ptr        pointer to buffer to extend
 * @param size       new buffer size
 *
 * @return pointer to expanded buffer (may differ from ptr)
 *         or NULL on failure
 */
void *cvmx_realloc(cvmx_arena_list_t arena_list, void *ptr, size_t size);
/**
 * allocate a buffer with a specified alignment
 *
 * @param arena_list arena list to allocate from
 * @param alignment  alignment of buffer.  Must be a power of 2
 * @param bytes      size of buffer in bytes
 *
 * @return pointer to buffer on success
 *         NULL on failure
 */
void *cvmx_memalign(cvmx_arena_list_t arena_list, size_t alignment, size_t bytes);
/**
 * free a previously allocated buffer
 *
 * @param ptr    pointer of buffer to deallocate
 */
void cvmx_free(void *ptr);
#endif




#define CVMX_ZONE_OVERHEAD  (64)
/** Zone allocator definitions
 *
 */
struct cvmx_zone
{
	cvmx_spinlock_t lock;
	char *baseptr;
	char *name;
	void *freelist;
	uint32_t num_elem;
	uint32_t elem_size;
	uint32_t align;
};
typedef struct cvmx_zone * cvmx_zone_t;

static inline uint32_t cvmx_zone_size(cvmx_zone_t zone)
{
    return(zone->elem_size);
}
static inline char *cvmx_zone_name(cvmx_zone_t zone)
{
    return(zone->name);
}


#ifndef CVMX_BUILD_FOR_LINUX_USER
/**
 * Creates a memory zone for efficient allocation/deallocation of
 * fixed size memory blocks from a specified memory region.
 *
 * @param name      name of zone.
 * @param elem_size size of blocks that will be requested from zone
 * @param num_elem  number of elements to allocate
 * @param mem_ptr   pointer to memory to allocate zone from
 * @param mem_size  size of memory region available
 *                  (must be at least elem_size * num_elem + CVMX_ZONE_OVERHEAD bytes)
 * @param flags     flags for zone.  Currently unused.
 *
 * @return pointer to zone on success or
 *         NULL on failure
 */
cvmx_zone_t cvmx_zone_create_from_addr(char *name, uint32_t elem_size, uint32_t num_elem,
                             void* mem_ptr, uint64_t mem_size, uint32_t flags);
/**
 * Creates a memory zone for efficient allocation/deallocation of
 * fixed size memory blocks from a previously initialized arena list.
 *
 * @param name       name of zone.
 * @param elem_size  size of blocks that will be requested from zone
 * @param num_elem   number of elements to allocate
 * @param align      alignment of buffers (must be power of 2)
 *                   Elements are allocated contiguously, so the buffer size
 *                   must be a multiple of the requested alignment for all
 *                   buffers to have the requested alignment.
 * @param arena_list arena list to allocate memory from
 * @param flags      flags for zone.  Currently unused.
 *
 * @return pointer to zone on success or
 *         NULL on failure
 */
cvmx_zone_t cvmx_zone_create_from_arena(char *name, uint32_t elem_size, uint32_t num_elem, uint32_t align,
                             cvmx_arena_list_t arena_list, uint32_t flags);
#endif
/**
 * Allocate a buffer from a memory zone
 *
 * @param zone   zone to allocate buffer from
 * @param flags  flags (currently unused)
 *
 * @return pointer to buffer or NULL on failure
 */
void * cvmx_zone_alloc(cvmx_zone_t zone, uint32_t flags);
/**
 * Free a previously allocated buffer
 *
 * @param zone   zone that buffer was allocated from
 * @param ptr    pointer to buffer to be freed
 */
void cvmx_zone_free(cvmx_zone_t zone, void *ptr);

#ifdef __cplusplus
}
#endif

#endif // __CVMX_MALLOC_H__
