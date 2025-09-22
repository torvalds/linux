/* $OpenBSD: sgmap_common.c,v 1.16 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: sgmap_common.c,v 1.13 2000/06/29 09:02:57 mrg Exp $ */

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
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

#define _ALPHA_BUS_DMA_PRIVATE

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <alpha/dev/sgmapvar.h>

/*
 * Some systems will prefetch the next page during a memory -> device DMA.
 * This can cause machine checks if there is not a spill page after the
 * last page of the DMA (thus avoiding hitting an invalid SGMAP PTE).
 */
vaddr_t		alpha_sgmap_prefetch_spill_page_va;
bus_addr_t	alpha_sgmap_prefetch_spill_page_pa;

void
alpha_sgmap_init(bus_dma_tag_t t, struct alpha_sgmap *sgmap, const char *name,
    bus_addr_t wbase, bus_addr_t sgvabase, bus_size_t sgvasize, size_t ptesize,
    void *ptva, bus_size_t minptalign)
{
	bus_dma_segment_t seg;
	size_t ptsize;
	int rseg;

	if (sgvasize & PGOFSET) {
		printf("size botch for sgmap `%s'\n", name);
		goto die;
	}

	sgmap->aps_wbase = wbase;
	sgmap->aps_sgvabase = sgvabase;
	sgmap->aps_sgvasize = sgvasize;

	if (ptva != NULL) {
		/*
		 * We already have a page table; this may be a system
		 * where the page table resides in bridge-resident SRAM.
		 */
		sgmap->aps_pt = ptva;
		sgmap->aps_ptpa = 0;
	} else {
		/*
		 * Compute the page table size and allocate it.  At minimum,
		 * this must be aligned to the page table size.  However,
		 * some platforms have more strict alignment requirements.
		 */
		ptsize = (sgvasize / PAGE_SIZE) * ptesize;
		if (minptalign != 0) {
			if (minptalign < ptsize)
				minptalign = ptsize;
		} else
			minptalign = ptsize;
		if (bus_dmamem_alloc(t, ptsize, minptalign, 0, &seg, 1, &rseg,
		    BUS_DMA_NOWAIT)) {
			panic("unable to allocate page table for sgmap `%s'",
			    name);
			goto die;
		}
		sgmap->aps_ptpa = seg.ds_addr;
		sgmap->aps_pt = (caddr_t)ALPHA_PHYS_TO_K0SEG(sgmap->aps_ptpa);
	}

	/*
	 * Create the extent map used to manage the virtual address
	 * space.
	 */
	sgmap->aps_ex = extent_create((char *)name, sgvabase, sgvasize - 1,
	    M_DEVBUF, NULL, 0, EX_NOWAIT|EX_NOCOALESCE);
	if (sgmap->aps_ex == NULL) {
		printf("unable to create extent map for sgmap `%s'\n",
		    name);
		goto die;
	}
	mtx_init(&sgmap->aps_mtx, IPL_HIGH);

	/*
	 * Allocate a spill page if that hasn't already been done.
	 */
	if (alpha_sgmap_prefetch_spill_page_va == 0) {
		if (bus_dmamem_alloc(t, PAGE_SIZE, 0, 0, &seg, 1, &rseg,
		    BUS_DMA_NOWAIT)) {
			printf("unable to allocate spill page for sgmap `%s'\n",
			    name);
			goto die;
		}
		alpha_sgmap_prefetch_spill_page_pa = seg.ds_addr;
		alpha_sgmap_prefetch_spill_page_va =
		    ALPHA_PHYS_TO_K0SEG(alpha_sgmap_prefetch_spill_page_pa);
		bzero((caddr_t)alpha_sgmap_prefetch_spill_page_va, PAGE_SIZE);
	}
	
	return;
 die:
	panic("alpha_sgmap_init");
}

int
alpha_sgmap_dmamap_setup(bus_dmamap_t map, int nsegments, int flags)
{
	map->_dm_cookie = mallocarray(nsegments, sizeof(struct extent_region),
	    M_DEVBUF, (flags & BUS_DMA_NOWAIT) ? M_NOWAIT : M_WAITOK);
	if (map->_dm_cookie != NULL)
		map->_dm_cookiesize = nsegments * sizeof(struct extent_region);
	return (map->_dm_cookie == NULL);
}

int
alpha_sgmap_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	bus_dmamap_t map;
	int error;

	error = _bus_dmamap_create(t, size, nsegments, maxsegsz,
	    boundary, flags, dmamp);
	if (error)
		return (error);

	map = *dmamp;
	if (alpha_sgmap_dmamap_setup(map, nsegments, flags)) {
		_bus_dmamap_destroy(t, map);
		return (ENOMEM);
	}

	/* XXX BUS_DMA_ALLOCNOW */

	return (0);
}

void
alpha_sgmap_dmamap_teardown(bus_dmamap_t map)
{
	free(map->_dm_cookie, M_DEVBUF, map->_dm_cookiesize);
}

void
alpha_sgmap_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	KASSERT(map->dm_mapsize == 0);

	alpha_sgmap_dmamap_teardown(map);
	_bus_dmamap_destroy(t, map);
}
