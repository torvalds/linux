/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD AND BSD-3-Clause
 *
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * All rights reserved.
 * Copyright 2001 by Thomas Moestl <tmm@FreeBSD.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)machdep.c	8.6 (Berkeley) 1/14/94
 *	from: NetBSD: machdep.c,v 1.221 2008/04/28 20:23:37 martin Exp
 *	and
 *	from: FreeBSD: src/sys/i386/i386/busdma_machdep.c,v 1.24 2001/08/15
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_param.h>
#include <vm/vm_map.h>

#include <machine/asi.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/bus_private.h>
#include <machine/cache.h>
#include <machine/smp.h>
#include <machine/tlb.h>

/* ASIs for bus access */
const int bus_type_asi[] = {
	ASI_PHYS_BYPASS_EC_WITH_EBIT,		/* nexus */
	ASI_PHYS_BYPASS_EC_WITH_EBIT,		/* SBus */
	ASI_PHYS_BYPASS_EC_WITH_EBIT_L,		/* PCI configuration space */
	ASI_PHYS_BYPASS_EC_WITH_EBIT_L,		/* PCI memory space */
	ASI_PHYS_BYPASS_EC_WITH_EBIT_L,		/* PCI I/O space */
	0
};

const int bus_stream_asi[] = {
	ASI_PHYS_BYPASS_EC_WITH_EBIT,		/* nexus */
	ASI_PHYS_BYPASS_EC_WITH_EBIT,		/* SBus */
	ASI_PHYS_BYPASS_EC_WITH_EBIT,		/* PCI configuration space */
	ASI_PHYS_BYPASS_EC_WITH_EBIT,		/* PCI memory space */
	ASI_PHYS_BYPASS_EC_WITH_EBIT,		/* PCI I/O space */
	0
};

/*
 * Convenience function for manipulating driver locks from busdma (during
 * busdma_swi, for example).  Drivers that don't provide their own locks
 * should specify &Giant to dmat->lockfuncarg.  Drivers that use their own
 * non-mutex locking scheme don't have to use this at all.
 */
void
busdma_lock_mutex(void *arg, bus_dma_lock_op_t op)
{
	struct mtx *dmtx;

	dmtx = (struct mtx *)arg;
	switch (op) {
	case BUS_DMA_LOCK:
		mtx_lock(dmtx);
		break;
	case BUS_DMA_UNLOCK:
		mtx_unlock(dmtx);
		break;
	default:
		panic("Unknown operation 0x%x for busdma_lock_mutex!", op);
	}
}

/*
 * dflt_lock should never get called.  It gets put into the dma tag when
 * lockfunc == NULL, which is only valid if the maps that are associated
 * with the tag are meant to never be defered.
 * XXX Should have a way to identify which driver is responsible here.
 */
static void
dflt_lock(void *arg, bus_dma_lock_op_t op)
{

	panic("driver error: busdma dflt_lock called");
}

/*
 * Allocate a device specific dma_tag.
 */
int
bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
    bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filter, void *filterarg, bus_size_t maxsize,
    int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
    void *lockfuncarg, bus_dma_tag_t *dmat)
{
	bus_dma_tag_t newtag;

	/* Return a NULL tag on failure */
	*dmat = NULL;

	/* Enforce the usage of BUS_GET_DMA_TAG(). */
	if (parent == NULL)
		panic("%s: parent DMA tag NULL", __func__);

	newtag = (bus_dma_tag_t)malloc(sizeof(*newtag), M_DEVBUF, M_NOWAIT);
	if (newtag == NULL)
		return (ENOMEM);

	/*
	 * The method table pointer and the cookie need to be taken over from
	 * the parent.
	 */
	newtag->dt_cookie = parent->dt_cookie;
	newtag->dt_mt = parent->dt_mt;

	newtag->dt_parent = parent;
	newtag->dt_alignment = alignment;
	newtag->dt_boundary = boundary;
	newtag->dt_lowaddr = trunc_page((vm_offset_t)lowaddr) + (PAGE_SIZE - 1);
	newtag->dt_highaddr = trunc_page((vm_offset_t)highaddr) +
	    (PAGE_SIZE - 1);
	newtag->dt_filter = filter;
	newtag->dt_filterarg = filterarg;
	newtag->dt_maxsize = maxsize;
	newtag->dt_nsegments = nsegments;
	newtag->dt_maxsegsz = maxsegsz;
	newtag->dt_flags = flags;
	newtag->dt_ref_count = 1; /* Count ourselves */
	newtag->dt_map_count = 0;

	if (lockfunc != NULL) {
		newtag->dt_lockfunc = lockfunc;
		newtag->dt_lockfuncarg = lockfuncarg;
	} else {
		newtag->dt_lockfunc = dflt_lock;
		newtag->dt_lockfuncarg = NULL;
	}

	newtag->dt_segments = NULL;

	/* Take into account any restrictions imposed by our parent tag. */
	newtag->dt_lowaddr = ulmin(parent->dt_lowaddr, newtag->dt_lowaddr);
	newtag->dt_highaddr = ulmax(parent->dt_highaddr, newtag->dt_highaddr);
	if (newtag->dt_boundary == 0)
		newtag->dt_boundary = parent->dt_boundary;
	else if (parent->dt_boundary != 0)
		newtag->dt_boundary = ulmin(parent->dt_boundary,
		    newtag->dt_boundary);
	atomic_add_int(&parent->dt_ref_count, 1);

	if (newtag->dt_boundary > 0)
		newtag->dt_maxsegsz = ulmin(newtag->dt_maxsegsz,
		    newtag->dt_boundary);

	*dmat = newtag;
	return (0);
}

int
bus_dma_tag_destroy(bus_dma_tag_t dmat)
{
	bus_dma_tag_t parent;

	if (dmat != NULL) {
		if (dmat->dt_map_count != 0)
			return (EBUSY);
		while (dmat != NULL) {
			parent = dmat->dt_parent;
			atomic_subtract_int(&dmat->dt_ref_count, 1);
			if (dmat->dt_ref_count == 0) {
				if (dmat->dt_segments != NULL)
					free(dmat->dt_segments, M_DEVBUF);
				free(dmat, M_DEVBUF);
				/*
				 * Last reference count, so
				 * release our reference
				 * count on our parent.
				 */
				dmat = parent;
			} else
				dmat = NULL;
		}
	}
	return (0);
}

/* Allocate/free a tag, and do the necessary management work. */
int
sparc64_dma_alloc_map(bus_dma_tag_t dmat, bus_dmamap_t *mapp)
{

	if (dmat->dt_segments == NULL) {
		dmat->dt_segments = (bus_dma_segment_t *)malloc(
		    sizeof(bus_dma_segment_t) * dmat->dt_nsegments, M_DEVBUF,
		    M_NOWAIT);
		if (dmat->dt_segments == NULL)
			return (ENOMEM);
	}
	*mapp = malloc(sizeof(**mapp), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (*mapp == NULL)
		return (ENOMEM);

	SLIST_INIT(&(*mapp)->dm_reslist);
	dmat->dt_map_count++;
	return (0);
}

void
sparc64_dma_free_map(bus_dma_tag_t dmat, bus_dmamap_t map)
{

	free(map, M_DEVBUF);
	dmat->dt_map_count--;
}

static int
nexus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp)
{

	return (sparc64_dma_alloc_map(dmat, mapp));
}

static int
nexus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map)
{

	sparc64_dma_free_map(dmat, map);
	return (0);
}

/*
 * Add a single contiguous physical range to the segment list.
 */
static int
nexus_dmamap_addseg(bus_dma_tag_t dmat, bus_dmamap_t map, bus_addr_t curaddr,
    bus_size_t sgsize, bus_dma_segment_t *segs, int *segp)
{
	bus_addr_t baddr, bmask;
	int seg;

	/*
	 * Make sure we don't cross any boundaries.
	 */
	bmask  = ~(dmat->dt_boundary - 1);
	if (dmat->dt_boundary > 0) {
		baddr = (curaddr + dmat->dt_boundary) & bmask;
		if (sgsize > (baddr - curaddr))
			sgsize = (baddr - curaddr);
	}

	/*
	 * Insert chunk into a segment, coalescing with
	 * previous segment if possible.
	 */
	seg = *segp;
	if (seg == -1) {
		seg = 0;
		segs[seg].ds_addr = curaddr;
		segs[seg].ds_len = sgsize;
	} else {
		if (curaddr == segs[seg].ds_addr + segs[seg].ds_len &&
		    (segs[seg].ds_len + sgsize) <= dmat->dt_maxsegsz &&
		    (dmat->dt_boundary == 0 ||
		    (segs[seg].ds_addr & bmask) == (curaddr & bmask)))
			segs[seg].ds_len += sgsize;
		else {
			if (++seg >= dmat->dt_nsegments)
				return (0);
			segs[seg].ds_addr = curaddr;
			segs[seg].ds_len = sgsize;
		}
	}
	*segp = seg;
	return (sgsize);
}

/*
 * Utility function to load a physical buffer.  segp contains
 * the starting segment on entrace, and the ending segment on exit.
 */
static int
nexus_dmamap_load_phys(bus_dma_tag_t dmat, bus_dmamap_t map, vm_paddr_t buf,
    bus_size_t buflen, int flags, bus_dma_segment_t *segs, int *segp)
{
	bus_addr_t curaddr;
	bus_size_t sgsize;

	if (segs == NULL)
		segs = dmat->dt_segments;

	curaddr = buf;
	while (buflen > 0) {
		sgsize = MIN(buflen, dmat->dt_maxsegsz);
		sgsize = nexus_dmamap_addseg(dmat, map, curaddr, sgsize, segs,
		    segp);
		if (sgsize == 0)
			break;
		curaddr += sgsize;
		buflen -= sgsize;
	}

	/*
	 * Did we fit?
	 */
	return (buflen != 0 ? EFBIG : 0); /* XXX better return value here? */
}

/*
 * Utility function to load a linear buffer.  segp contains
 * the starting segment on entrace, and the ending segment on exit.
 */
static int
nexus_dmamap_load_buffer(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, pmap_t pmap, int flags, bus_dma_segment_t *segs,
    int *segp)
{
	bus_size_t sgsize;
	bus_addr_t curaddr;
	vm_offset_t vaddr = (vm_offset_t)buf;

	if (segs == NULL)
		segs = dmat->dt_segments;

	while (buflen > 0) {
		/*
		 * Get the physical address for this segment.
		 */
		if (pmap == kernel_pmap)
			curaddr = pmap_kextract(vaddr);
		else
			curaddr = pmap_extract(pmap, vaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)curaddr & PAGE_MASK);
		if (sgsize > dmat->dt_maxsegsz)
			sgsize = dmat->dt_maxsegsz;
		if (buflen < sgsize)
			sgsize = buflen;

		sgsize = nexus_dmamap_addseg(dmat, map, curaddr, sgsize, segs,
		    segp);
		if (sgsize == 0)
			break;

		vaddr += sgsize;
		buflen -= sgsize;
	}

	/*
	 * Did we fit?
	 */
	return (buflen != 0 ? EFBIG : 0); /* XXX better return value here? */
}

static void
nexus_dmamap_waitok(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct memdesc *mem, bus_dmamap_callback_t *callback, void *callback_arg)
{

}

static bus_dma_segment_t *
nexus_dmamap_complete(bus_dma_tag_t dmat, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, int error)
{

	if (segs == NULL)
		segs = dmat->dt_segments;
	return (segs);
}

/*
 * Common function for unloading a DMA map.  May be called by
 * bus-specific DMA map unload functions.
 */
static void
nexus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map)
{

	map->dm_flags &= ~DMF_LOADED;
}

/*
 * Common function for DMA map synchronization.  May be called
 * by bus-specific DMA map synchronization functions.
 */
static void
nexus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op)
{

	/*
	 * We sync out our caches, but the bus must do the same.
	 *
	 * Actually a #Sync is expensive.  We should optimize.
	 */
	if ((op & BUS_DMASYNC_PREREAD) || (op & BUS_DMASYNC_PREWRITE)) {
		/*
		 * Don't really need to do anything, but flush any pending
		 * writes anyway.
		 */
		membar(Sync);
	}
	if (op & BUS_DMASYNC_POSTWRITE) {
		/* Nothing to do.  Handled by the bus controller. */
	}
}

/*
 * Common function for DMA-safe memory allocation.  May be called
 * by bus-specific DMA memory allocation functions.
 */
static int
nexus_dmamem_alloc(bus_dma_tag_t dmat, void **vaddr, int flags,
    bus_dmamap_t *mapp)
{
	int mflags;

	if (flags & BUS_DMA_NOWAIT)
		mflags = M_NOWAIT;
	else
		mflags = M_WAITOK;
	if (flags & BUS_DMA_ZERO)
		mflags |= M_ZERO;

	/*
	 * XXX:
	 * (dmat->dt_alignment <= dmat->dt_maxsize) is just a quick hack; the
	 * exact alignment guarantees of malloc need to be nailed down, and
	 * the code below should be rewritten to take that into account.
	 *
	 * In the meantime, we'll warn the user if malloc gets it wrong.
	 */
	if (dmat->dt_maxsize <= PAGE_SIZE &&
	    dmat->dt_alignment <= dmat->dt_maxsize)
		*vaddr = malloc(dmat->dt_maxsize, M_DEVBUF, mflags);
	else {
		/*
		 * XXX use contigmalloc until it is merged into this
		 * facility and handles multi-seg allocations.  Nobody
		 * is doing multi-seg allocations yet though.
		 */
		*vaddr = contigmalloc(dmat->dt_maxsize, M_DEVBUF, mflags,
		    0ul, dmat->dt_lowaddr,
		    dmat->dt_alignment ? dmat->dt_alignment : 1UL,
		    dmat->dt_boundary);
	}
	if (*vaddr == NULL)
		return (ENOMEM);
	if (vtophys(*vaddr) % dmat->dt_alignment)
		printf("%s: failed to align memory properly.\n", __func__);
	return (0);
}

/*
 * Common function for freeing DMA-safe memory.  May be called by
 * bus-specific DMA memory free functions.
 */
static void
nexus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map)
{

	if (dmat->dt_maxsize <= PAGE_SIZE &&
	    dmat->dt_alignment < dmat->dt_maxsize)
		free(vaddr, M_DEVBUF);
	else
		contigfree(vaddr, dmat->dt_maxsize, M_DEVBUF);
}

static struct bus_dma_methods nexus_dma_methods = {
	nexus_dmamap_create,
	nexus_dmamap_destroy,
	nexus_dmamap_load_phys,
	nexus_dmamap_load_buffer,
	nexus_dmamap_waitok,
	nexus_dmamap_complete,
	nexus_dmamap_unload,
	nexus_dmamap_sync,
	nexus_dmamem_alloc,
	nexus_dmamem_free,
};

struct bus_dma_tag nexus_dmatag = {
	NULL,
	NULL,
	1,
	0,
	~0,
	~0,
	NULL,		/* XXX */
	NULL,
	~0,
	~0,
	~0,
	0,
	0,
	0,
	NULL,
	NULL,
	NULL,
	&nexus_dma_methods,
};

/*
 * Helpers to map/unmap bus memory
 */
int
bus_space_map(bus_space_tag_t tag, bus_addr_t address, bus_size_t size,
    int flags, bus_space_handle_t *handlep)
{

	return (sparc64_bus_mem_map(tag, address, size, flags, 0, handlep));
}

int
sparc64_bus_mem_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, vm_offset_t vaddr, bus_space_handle_t *hp)
{
	vm_offset_t sva;
	vm_offset_t va;
	vm_paddr_t pa;
	vm_size_t vsz;
	u_long pm_flags;

	/*
	 * Given that we use physical access for bus_space(9) there's no need
	 * need to map anything in unless BUS_SPACE_MAP_LINEAR is requested.
	 */
	if ((flags & BUS_SPACE_MAP_LINEAR) == 0) {
		*hp = addr;
		return (0);
	}

	if (tag->bst_cookie == NULL) {
		printf("%s: resource cookie not set\n", __func__);
		return (EINVAL);
	}

	size = round_page(size);
	if (size == 0) {
		printf("%s: zero size\n", __func__);
		return (EINVAL);
	}

	switch (tag->bst_type) {
	case PCI_CONFIG_BUS_SPACE:
	case PCI_IO_BUS_SPACE:
	case PCI_MEMORY_BUS_SPACE:
		pm_flags = TD_IE;
		break;
	default:
		pm_flags = 0;
		break;
	}

	if ((flags & BUS_SPACE_MAP_CACHEABLE) == 0)
		pm_flags |= TD_E;

	if (vaddr != 0L)
		sva = trunc_page(vaddr);
	else {
		if ((sva = kva_alloc(size)) == 0)
			panic("%s: cannot allocate virtual memory", __func__);
	}

	pa = trunc_page(addr);
	if ((flags & BUS_SPACE_MAP_READONLY) == 0)
		pm_flags |= TD_W;

	va = sva;
	vsz = size;
	do {
		pmap_kenter_flags(va, pa, pm_flags);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	} while ((vsz -= PAGE_SIZE) > 0);
	tlb_range_demap(kernel_pmap, sva, sva + size - 1);

	/* Note: we preserve the page offset. */
	rman_set_virtual(tag->bst_cookie, (void *)(sva | (addr & PAGE_MASK)));
	return (0);
}

void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t size)
{

	sparc64_bus_mem_unmap(tag, handle, size);
}

int
sparc64_bus_mem_unmap(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t size)
{
	vm_offset_t sva;
	vm_offset_t va;
	vm_offset_t endva;

	if (tag->bst_cookie == NULL ||
	    (sva = (vm_offset_t)rman_get_virtual(tag->bst_cookie)) == 0)
		return (0);
	sva = trunc_page(sva);
	endva = sva + round_page(size);
	for (va = sva; va < endva; va += PAGE_SIZE)
		pmap_kremove_flags(va);
	tlb_range_demap(kernel_pmap, sva, sva + size - 1);
	kva_free(sva, size);
	return (0);
}

/*
 * Fake up a bus tag, for use by console drivers in early boot when the
 * regular means to allocate resources are not yet available.
 * Addr is the physical address of the desired start of the handle.
 */
bus_space_handle_t
sparc64_fake_bustag(int space, bus_addr_t addr, struct bus_space_tag *ptag)
{

	ptag->bst_cookie = NULL;
	ptag->bst_type = space;
	return (addr);
}

/*
 * Allocate a bus tag
 */
bus_space_tag_t
sparc64_alloc_bus_tag(void *cookie, int type)
{
	bus_space_tag_t bt;

	bt = malloc(sizeof(struct bus_space_tag), M_DEVBUF, M_NOWAIT);
	if (bt == NULL)
		return (NULL);
	bt->bst_cookie = cookie;
	bt->bst_type = type;
	return (bt);
}

struct bus_space_tag nexus_bustag = {
	NULL,				/* cookie */
	NEXUS_BUS_SPACE			/* type */
};
