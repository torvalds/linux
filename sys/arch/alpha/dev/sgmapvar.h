/* $OpenBSD: sgmapvar.h,v 1.7 2014/06/14 23:11:20 jmatthew Exp $ */
/* $NetBSD: sgmapvar.h,v 1.10 1998/08/14 16:50:02 thorpej Exp $ */

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

#ifndef	_ALPHA_COMMON_SGMAPVAR_H
#define	_ALPHA_COMMON_SGMAPVAR_H

#include <sys/extent.h>
#include <sys/mutex.h>
#include <machine/bus.h>

/*
 * Bits n:13 of the DMA address are the index of the PTE into
 * the SGMAP page table.
 */
#define	SGMAP_ADDR_PTEIDX_SHIFT	13

/*
 * An Alpha SGMAP's state information.  Nothing in the sgmap requires
 * locking[*], with the exception of the extent map.  Locking of the
 * extent map is handled within the extent manager itself.
 *
 * [*] While the page table is a `global' resource, access to it is
 * controlled by the extent map; once a region has been allocated from
 * the map, that region is effectively `locked'.
 */
struct alpha_sgmap {
	struct extent *aps_ex;		/* extent map to manage sgva space */
	struct mutex aps_mtx;
	void	*aps_pt;		/* page table */
	bus_addr_t aps_ptpa;		/* page table physical address */
	bus_addr_t aps_sgvabase;	/* base of the sgva space */
	bus_size_t aps_sgvasize;	/* size of the sgva space */
	bus_addr_t aps_wbase;		/* base of the dma window */
};

extern	vaddr_t alpha_sgmap_prefetch_spill_page_va;
extern	bus_addr_t alpha_sgmap_prefetch_spill_page_pa;

void	alpha_sgmap_init(bus_dma_tag_t, struct alpha_sgmap *,
	    const char *, bus_addr_t, bus_addr_t, bus_size_t, size_t, void *,
	    bus_size_t);

int	alpha_sgmap_dmamap_setup(bus_dmamap_t, int, int);
void	alpha_sgmap_dmamap_teardown(bus_dmamap_t);
int	alpha_sgmap_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t,
	    bus_size_t, int, bus_dmamap_t *);
void	alpha_sgmap_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);

#endif	/* _ALPHA_COMMON_SGMAPVAR_H */
