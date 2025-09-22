/* $OpenBSD: tc_dma_3000_500.c,v 1.8 2025/06/28 16:04:10 miod Exp $ */
/* $NetBSD: tc_dma_3000_500.c,v 1.13 2001/07/19 06:40:03 thorpej Exp $ */

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
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/alpha_cpu.h>
#include <machine/bus.h>

#include <dev/tc/tcvar.h>
#include <alpha/tc/tc_conf.h>
#include <alpha/tc/tc_3000_500.h>
#include <alpha/tc/tc_sgmap.h>
#include <alpha/tc/tc_dma_3000_500.h>

#define	KV(x)	(ALPHA_PHYS_TO_K0SEG(x))

struct alpha_sgmap tc_dma_sgmap;	/* sgmap shared by all slots */

struct alpha_bus_dma_tag tc_dmat_sgmap = {
	NULL,				/* _cookie, not used */
	0,				/* _wbase */
	0x1000000,			/* _wsize (256MB: 32K entries) */
	NULL,				/* _next_window */
	0,				/* _boundary */
	&tc_dma_sgmap,			/* _sgmap */
	NULL,				/* _get_tag, not used */
	alpha_sgmap_dmamap_create,
	alpha_sgmap_dmamap_destroy,
	tc_bus_dmamap_load_sgmap,
	tc_bus_dmamap_load_mbuf_sgmap,
	tc_bus_dmamap_load_uio_sgmap,
	tc_bus_dmamap_load_raw_sgmap,
	tc_bus_dmamap_unload_sgmap,
	_bus_dmamap_sync,
	_bus_dmamem_alloc,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	_bus_dmamem_mmap,
};

void
tc_dma_init_3000_500(int nslots)
{
	int i;
	struct alpha_bus_dma_tag *t;

	for (i = 0; i < nslots; i++) {
		/*
		 * Note that the use of sgmap on a slot is global, so we
		 * can not afford reverting to direct dma (by making
		 * _next_window point to the direct tag).
		 * On the other hand, the sgmap allows for 256MB of
		 * DMA transfers being programmed at the same time for all
		 * the slots, which should not be a liability.
		 */
		tc_3000_500_ioslot(i, IOSLOT_S, !0);
	}
	/*
	 * The TURBOchannel sgmap is shared by all slots.
	 * We need a valid bus_dma_tag_t to pass alpha_sgmap_init() in order
	 * to allocate the sgmap fill page, so pick the first.
	 */
	t = &tc_dmat_sgmap;
	alpha_sgmap_init(t, t->_sgmap, "tc_sgmap",
	    t->_wbase, 0, t->_wsize,
	    SGMAP_PTE_SPACING * sizeof(SGMAP_PTE_TYPE),
	    (void *)TC_3000_500_SCMAP, 0);
}

/*
 * Return the DMA tag for the given slot.
 */
bus_dma_tag_t
tc_dma_get_tag_3000_500(int slot)
{
	return &tc_dmat_sgmap;
}

/*
 * Load a TURBOchannel SGMAP-mapped DMA map with a linear buffer.
 */
int
tc_bus_dmamap_load_sgmap(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	return (tc_sgmap_load(t, map, buf, buflen, p, flags, t->_sgmap));
}

/*
 * Load a TURBOchannel SGMAP-mapped DMA map with an mbuf chain.
 */
int
tc_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t t, bus_dmamap_t map, struct mbuf *m,
    int flags)
{
	return (tc_sgmap_load_mbuf(t, map, m, flags, t->_sgmap));
}

/*
 * Load a TURBOchannel SGMAP-mapped DMA map with a uio.
 */
int
tc_bus_dmamap_load_uio_sgmap(bus_dma_tag_t t, bus_dmamap_t map, struct uio *uio,
    int flags)
{
	return (tc_sgmap_load_uio(t, map, uio, flags, t->_sgmap));
}

/*
 * Load a TURBOchannel SGMAP-mapped DMA map with raw memory.
 */
int
tc_bus_dmamap_load_raw_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	return (tc_sgmap_load_raw(t, map, segs, nsegs, size, flags, t->_sgmap));
}

/*
 * Unload a TURBOchannel SGMAP-mapped DMA map.
 */
void
tc_bus_dmamap_unload_sgmap(bus_dma_tag_t t, bus_dmamap_t map)
{
	/*
	 * Invalidate any SGMAP page table entries used by this
	 * mapping.
	 */
	tc_sgmap_unload(t, map, t->_sgmap);

	/*
	 * Do the generic bits of the unload.
	 */
	_bus_dmamap_unload(t, map);
}
