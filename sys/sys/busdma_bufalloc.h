/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Ian Lepore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * $FreeBSD$
 */

/*
 * A buffer pool manager, for use by a platform's busdma implementation.
 */

#ifndef _MACHINE_BUSDMA_BUFALLOC_H_
#define _MACHINE_BUSDMA_BUFALLOC_H_

#include <machine/bus.h>
#include <vm/uma.h>

/*
 * Information about a buffer zone, returned by busdma_bufalloc_findzone().
 */
struct busdma_bufzone {
	bus_size_t	size;
	uma_zone_t	umazone;
	char		name[24];
};

/*
 * Opaque handle type returned by busdma_bufalloc_create().
 */
struct busdma_bufalloc;
typedef struct busdma_bufalloc *busdma_bufalloc_t;

/*
 * Create an allocator that manages a pool of DMA buffers.
 *
 * The allocator manages a collection of uma(9) zones of buffers in power-of-two
 * sized increments ranging from minimum_alignment to the platform's PAGE_SIZE.
 * The buffers within each zone are aligned on boundaries corresponding to the
 * buffer size, and thus by implication each buffer is contiguous within a page
 * and does not cross a power of two boundary larger than the buffer size.
 * These rules are intended to make it easy for a busdma implementation to
 * check whether a tag's constraints allow use of a buffer from the allocator.
 *
 * minimum_alignment is also the minimum buffer allocation size.  For platforms
 * with software-assisted cache coherency, this is typically the data cache line
 * size (and MUST not be smaller than the cache line size).
 *
 * name appears in zone stats as 'dma name nnnnn' where 'dma' is fixed and
 * 'nnnnn' is the size of buffers in that zone.
 *
 * If the alloc/free function pointers are NULL, the regular uma internal
 * allocators are used (I.E., you get "plain old kernel memory").  On a platform
 * with an exclusion zone that applies to all DMA operations, a custom allocator
 * could be used to ensure no buffer memory is ever allocated from that zone,
 * allowing the bus_dmamem_alloc() implementation to make the assumption that
 * buffers provided by the allocation could never lead to the need for a bounce.
 */
busdma_bufalloc_t busdma_bufalloc_create(const char *name,
    bus_size_t minimum_alignment,
    uma_alloc uma_alloc_func, uma_free uma_free_func,
    u_int32_t uma_zcreate_flags);

/*
 * Destroy an allocator created by busdma_bufalloc_create().
 * Safe to call with a NULL pointer.
 */
void busdma_bufalloc_destroy(busdma_bufalloc_t ba);

/*
 * Return a pointer to the busdma_bufzone that should be used to allocate or
 * free a buffer of the given size.  Returns NULL if the size is larger than the
 * largest zone handled by the allocator.
 */
struct busdma_bufzone * busdma_bufalloc_findzone(busdma_bufalloc_t ba,
    bus_size_t size);

/*
 * These built-in allocation routines are available for managing a pools of
 * uncacheable memory on platforms that support VM_MEMATTR_UNCACHEABLE.
 *
 * Allocation is done using kmem_alloc_attr() with these parameters:
 *   lowaddr  = 0
 *   highaddr = BUS_SPACE_MAXADDR
 *   memattr  = VM_MEMATTR_UNCACHEABLE.
 *
 * If your platform has no exclusion region (lowaddr/highaddr), and its pmap
 * routines support pmap_page_set_memattr() and the VM_MEMATTR_UNCACHEABLE flag
 * you can probably use these when you need uncacheable buffers.
 */
void * busdma_bufalloc_alloc_uncacheable(uma_zone_t zone, vm_size_t size,
    int domain, uint8_t *pflag, int wait);
void  busdma_bufalloc_free_uncacheable(void *item, vm_size_t size,
    uint8_t pflag);

#endif	/* _MACHINE_BUSDMA_BUFALLOC_H_ */
