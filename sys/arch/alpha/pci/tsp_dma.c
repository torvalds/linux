/* $OpenBSD: tsp_dma.c,v 1.13 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: tsp_dma.c,v 1.1 1999/06/29 06:46:47 ross Exp $ */

/*-
 * Copyright (c) 1999 by Ross Harvey.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Ross Harvey.
 * 4. The name of Ross Harvey may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ROSS HARVEY ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURP0SE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ROSS HARVEY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

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
#include <sys/device.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>

#include <machine/bus.h>
#include <machine/rpb.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/tsreg.h>
#include <alpha/pci/tsvar.h>

#define tsp_dma() { Generate ctags(1) key. }

#define	EDIFF(a, b) (((a) | WSBA_ENA | WSBA_SG)	!= ((b) | WSBA_ENA | WSBA_SG))

bus_dma_tag_t tsp_dma_get_tag(bus_dma_tag_t, alpha_bus_t);
int	tsp_bus_dmamap_load_sgmap(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);
int	tsp_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    struct mbuf *, int);
int	tsp_bus_dmamap_load_uio_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    struct uio *, int);
int	tsp_bus_dmamap_load_raw_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);
void	tsp_bus_dmamap_unload_sgmap(bus_dma_tag_t, bus_dmamap_t);
void	tsp_tlb_invalidate(struct tsp_config *);

void
tsp_dma_init(struct device *tsp, struct tsp_config *pcp)
{
	int i;
	bus_dma_tag_t t;
	struct ts_pchip *pccsr = pcp->pc_csr;
	bus_addr_t dwbase, dwlen, sgwbase, sgwlen, tbase;
	static const struct map_expected {
		u_int32_t base, mask, physbase, enables;
	} premap[4] = {
		/* 0-8MB at 8MB with S/G DMA (isa space) */
		{ 0x00800000, 0x00700000, 0x00000000, WSBA_ENA | WSBA_SG },
		/* 0-1GB at 2GB direct */
		{ 0x80000000, 0x3ff00000, 0x00000000, WSBA_ENA           },
		{ 0, 0, 0 },
		{ 0, 0, 0 }
	};

	alpha_mb();
	for(i = 0; i < 4; ++i) {
#ifdef DEBUG
		if (EDIFF(pccsr->tsp_wsba[i].tsg_r, premap[i].base) ||
		    EDIFF(pccsr->tsp_wsm[i].tsg_r, premap[i].mask) ||
		    ((premap[i].enables & WSBA_SG) == 0 &&
		      pccsr->tsp_tba[i].tsg_r != premap[i].physbase))
			printf("%s: window %d: %lx/base %lx/mask %lx"
			    " reinitialized\n",
			    tsp->dv_xname, i,
			    pccsr->tsp_wsba[i].tsg_r,
			    pccsr->tsp_wsm[i].tsg_r,
			    pccsr->tsp_tba[i].tsg_r);
#endif
		pccsr->tsp_wsba[i].tsg_r = premap[i].base | premap[i].enables;
		pccsr->tsp_wsm[i].tsg_r = premap[i].mask;
		if ((premap[i].enables & WSBA_SG) == 0)
			pccsr->tsp_tba[i].tsg_r = premap[i].physbase;
	}
	alpha_mb();

	/*
	 * Initialize the DMA tags used for direct-mapped DMA.
	 */
	t = &pcp->pc_dmat_direct;
	t->_cookie = pcp;
	t->_wbase = dwbase = WSBA_ADDR(pccsr->tsp_wsba[1].tsg_r);
	t->_wsize = dwlen = WSM_LEN(pccsr->tsp_wsm[1].tsg_r);
	t->_next_window = &pcp->pc_dmat_sgmap;
	t->_boundary = 0;
	t->_sgmap = NULL;
	t->_get_tag = tsp_dma_get_tag;
	/*
	 * Since we fall back to sgmap if the direct mapping fails,
	 * we need to set up for sgmap in any case.
	 */
	t->_dmamap_create = alpha_sgmap_dmamap_create;
	t->_dmamap_destroy = alpha_sgmap_dmamap_destroy;
	t->_dmamap_load = _bus_dmamap_load_direct;
	t->_dmamap_load_mbuf = _bus_dmamap_load_mbuf_direct;
	t->_dmamap_load_uio = _bus_dmamap_load_uio_direct;
	t->_dmamap_load_raw = _bus_dmamap_load_raw_direct;
	t->_dmamap_unload = _bus_dmamap_unload;
	t->_dmamap_sync = _bus_dmamap_sync;

	t->_dmamem_alloc = _bus_dmamem_alloc;
	t->_dmamem_free = _bus_dmamem_free;
	t->_dmamem_map = _bus_dmamem_map;
	t->_dmamem_unmap = _bus_dmamem_unmap;
	t->_dmamem_mmap = _bus_dmamem_mmap;

	/*
	 * Initialize the DMA tag used for sgmap-mapped DMA.
	 */
	t = &pcp->pc_dmat_sgmap;
	t->_cookie = pcp;
	t->_wbase = sgwbase = WSBA_ADDR(pccsr->tsp_wsba[0].tsg_r);
	t->_wsize = sgwlen = WSM_LEN(pccsr->tsp_wsm[0].tsg_r);
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = &pcp->pc_sgmap;
	t->_get_tag = tsp_dma_get_tag;
	t->_dmamap_create = alpha_sgmap_dmamap_create;
	t->_dmamap_destroy = alpha_sgmap_dmamap_destroy;
	t->_dmamap_load = tsp_bus_dmamap_load_sgmap;
	t->_dmamap_load_mbuf = tsp_bus_dmamap_load_mbuf_sgmap;
	t->_dmamap_load_uio = tsp_bus_dmamap_load_uio_sgmap;
	t->_dmamap_load_raw = tsp_bus_dmamap_load_raw_sgmap;
	t->_dmamap_unload = tsp_bus_dmamap_unload_sgmap;
	t->_dmamap_sync = _bus_dmamap_sync;

	t->_dmamem_alloc = _bus_dmamem_alloc;
	t->_dmamem_free = _bus_dmamem_free;
	t->_dmamem_map = _bus_dmamem_map;
	t->_dmamem_unmap = _bus_dmamem_unmap;
	t->_dmamem_mmap = _bus_dmamem_mmap;

	/*
	 * Initialize the SGMAP.  Align page table to 32k in case
	 * window is somewhat larger than expected.
	 */
	alpha_sgmap_init(t, &pcp->pc_sgmap, "tsp_sgmap",
	    sgwbase, 0, sgwlen, sizeof(u_int64_t), NULL, (32*1024));

	/*
	 * Check windows for sanity, especially if we later decide to
	 * use the firmware's initialization in some cases.
	 */
	if ((sgwbase <= dwbase && dwbase < sgwbase + sgwlen) ||
	    (dwbase <= sgwbase && sgwbase < dwbase + dwlen))
		panic("%s: overlap sg 0x%lx len 0x%lx d 0x%lx len 0x%lx",
		    __func__, sgwbase, sgwlen, dwbase, dwlen);

	tbase = pcp->pc_sgmap.aps_ptpa;
	if (tbase & ~0x7fffffc00UL)
		panic("tsp_dma_init: bad page table address");
	alpha_mb();
	pccsr->tsp_tba[0].tsg_r = tbase;
	alpha_mb();

	tsp_tlb_invalidate(pcp);
	alpha_mb();

	/* XXX XXX BEGIN XXX XXX */
	{							/* XXX */
		extern paddr_t alpha_XXX_dmamap_or;		/* XXX */
		alpha_XXX_dmamap_or = dwbase;			/* XXX */
	}							/* XXX */
	/* XXX XXX END XXX XXX */
}

/*
 * Return the bus dma tag to be used for the specified bus type.
 * INTERNAL USE ONLY!
 */
bus_dma_tag_t
tsp_dma_get_tag(bus_dma_tag_t t, alpha_bus_t bustype)
{
	struct tsp_config *pcp = t->_cookie;

	switch (bustype) {
	case ALPHA_BUS_PCI:
	case ALPHA_BUS_EISA:
		/*
		 * The direct mapped window will work for most systems,
		 * most of the time. When it doesn't, we chain to the sgmap
		 * window automatically.
		 */
		return (&pcp->pc_dmat_direct);

	case ALPHA_BUS_ISA:
		/*
		 * ISA doesn't have enough address bits to use
		 * the direct-mapped DMA window, so we must use
		 * SGMAPs.
		 */
		return (&pcp->pc_dmat_sgmap);

	default:
		panic("tsp_dma_get_tag: shouldn't be here, really...");
	}
}

/*
 * Load a TSP SGMAP-mapped DMA map with a linear buffer.
 */
int
tsp_bus_dmamap_load_sgmap(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	int error;

	error = pci_sgmap_pte64_load(t, map, buf, buflen, p, flags,
	    t->_sgmap);
	if (error == 0)
		tsp_tlb_invalidate(t->_cookie);

	return (error);
}

/*
 * Load a TSP SGMAP-mapped DMA map with an mbuf chain.
 */
int
tsp_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct mbuf *m, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_mbuf(t, map, m, flags, t->_sgmap);
	if (error == 0)
		tsp_tlb_invalidate(t->_cookie);

	return (error);
}

/*
 * Load a TSP SGMAP-mapped DMA map with a uio.
 */
int
tsp_bus_dmamap_load_uio_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct uio *uio, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_uio(t, map, uio, flags, t->_sgmap);
	if (error == 0)
		tsp_tlb_invalidate(t->_cookie);

	return (error);
}

/*
 * Load a TSP SGMAP-mapped DMA map with raw memory.
 */
int
tsp_bus_dmamap_load_raw_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_raw(t, map, segs, nsegs, size, flags,
	    t->_sgmap);
	if (error == 0)
		tsp_tlb_invalidate(t->_cookie);

	return (error);
}

/*
 * Unload a TSP DMA map.
 */
void
tsp_bus_dmamap_unload_sgmap(bus_dma_tag_t t, bus_dmamap_t map)
{

	/*
	 * Invalidate any SGMAP page table entries used by this
	 * mapping.
	 */
	pci_sgmap_pte64_unload(t, map, t->_sgmap);
	tsp_tlb_invalidate(t->_cookie);

	/*
	 * Do the generic bits of the unload.
	 */
	_bus_dmamap_unload(t, map);
}

/*
 * Flush the TSP scatter/gather TLB.
 */
void
tsp_tlb_invalidate(struct tsp_config *pcp)
{

	alpha_mb();
	*pcp->pc_tlbia = 0;
	alpha_mb();
}
