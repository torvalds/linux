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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Buffer allocation support routines for bus_dmamem_alloc implementations.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/busdma_bufalloc.h>
#include <sys/domainset.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/uma.h>

/*
 * We manage buffer zones up to a page in size.  Buffers larger than a page can
 * be managed by one of the kernel's page-oriented memory allocation routines as
 * efficiently as what we can do here.  Also, a page is the largest size for
 * which we can g'tee contiguity when using uma, and contiguity is one of the
 * requirements we have to fulfill.
 */
#define	MIN_ZONE_BUFSIZE	32
#define	MAX_ZONE_BUFSIZE	PAGE_SIZE

/*
 * The static array of 12 bufzones is big enough to handle all the zones for the
 * smallest supported allocation size of 32 through the largest supported page
 * size of 64K.  If you up the biggest page size number, up the array size too.
 * Basically the size of the array needs to be log2(maxsize)-log2(minsize)+1,
 * but I don't know of an easy way to express that as a compile-time constant.
 */
#if PAGE_SIZE > 65536
#error Unsupported page size
#endif

struct busdma_bufalloc {
	bus_size_t		min_size;
	size_t			num_zones;
	struct busdma_bufzone	buf_zones[12];
};

busdma_bufalloc_t 
busdma_bufalloc_create(const char *name, bus_size_t minimum_alignment,
    uma_alloc alloc_func, uma_free free_func, u_int32_t zcreate_flags)
{
	struct busdma_bufalloc *ba;
	struct busdma_bufzone *bz;
	int i;
	bus_size_t cursize;

	ba = malloc(sizeof(struct busdma_bufalloc), M_DEVBUF, 
	    M_ZERO | M_WAITOK);

	ba->min_size = MAX(MIN_ZONE_BUFSIZE, minimum_alignment);

	/*
	 * Each uma zone is created with an alignment of size-1, meaning that
	 * the alignment is equal to the size (I.E., 64 byte buffers are aligned
	 * to 64 byte boundaries, etc).  This allows for a fast efficient test
	 * when deciding whether a pool buffer meets the constraints of a given
	 * tag used for allocation: the buffer is usable if tag->alignment <=
	 * bufzone->size.
	 */
	for (i = 0, bz = ba->buf_zones, cursize = ba->min_size;
	    i < nitems(ba->buf_zones) && cursize <= MAX_ZONE_BUFSIZE;
	    ++i, ++bz, cursize <<= 1) {
		snprintf(bz->name, sizeof(bz->name), "dma %.10s %ju",
		    name, (uintmax_t)cursize);
		bz->size = cursize;
		bz->umazone = uma_zcreate(bz->name, bz->size,
		    NULL, NULL, NULL, NULL, bz->size - 1, zcreate_flags);
		if (bz->umazone == NULL) {
			busdma_bufalloc_destroy(ba);
			return (NULL);
		}
		if (alloc_func != NULL)
			uma_zone_set_allocf(bz->umazone, alloc_func);
		if (free_func != NULL)
			uma_zone_set_freef(bz->umazone, free_func);
		++ba->num_zones;
	}

	return (ba);
}

void 
busdma_bufalloc_destroy(busdma_bufalloc_t ba)
{
	struct busdma_bufzone *bz;
	int i;

	if (ba == NULL)
		return;

	for (i = 0, bz = ba->buf_zones; i < ba->num_zones; ++i, ++bz) {
		uma_zdestroy(bz->umazone);
	}

	free(ba, M_DEVBUF);
}

struct busdma_bufzone * 
busdma_bufalloc_findzone(busdma_bufalloc_t ba, bus_size_t size)
{
	struct busdma_bufzone *bz;
	int i;

	if (size > MAX_ZONE_BUFSIZE)
		return (NULL);

	for (i = 0, bz = ba->buf_zones; i < ba->num_zones; ++i, ++bz) {
		if (bz->size >= size)
			return (bz);
	}

	panic("Didn't find a buffer zone of the right size");
}

void *
busdma_bufalloc_alloc_uncacheable(uma_zone_t zone, vm_size_t size, int domain,
    uint8_t *pflag, int wait)
{

#ifdef VM_MEMATTR_UNCACHEABLE
	/* Inform UMA that this allocator uses kernel_arena/object. */
	*pflag = UMA_SLAB_KERNEL;

	return ((void *)kmem_alloc_attr_domainset(DOMAINSET_FIXED(domain), size,
	    wait, 0, BUS_SPACE_MAXADDR, VM_MEMATTR_UNCACHEABLE));
#else
	panic("VM_MEMATTR_UNCACHEABLE unavailable");
#endif	/* VM_MEMATTR_UNCACHEABLE */
}

void 
busdma_bufalloc_free_uncacheable(void *item, vm_size_t size, uint8_t pflag)
{

	kmem_free((vm_offset_t)item, size);
}

