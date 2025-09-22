/* $OpenBSD: mcpcia_dma.c,v 1.6 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: mcpcia_dma.c,v 1.15 2001/07/19 18:55:40 thorpej Exp $ */

/*-
 * Copyright (c) 1997, 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe and Matthew Jacob of the Numerical Aerospace Simulation
 * Facility, NASA Ames Research Center.
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

#include <machine/bus.h>
#include <machine/rpb.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/mcpciareg.h>
#include <alpha/pci/mcpciavar.h>
#include <alpha/pci/pci_kn300.h>

bus_dma_tag_t mcpcia_dma_get_tag (bus_dma_tag_t, alpha_bus_t);

int	mcpcia_bus_dmamap_load_sgmap (bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);

int	mcpcia_bus_dmamap_load_mbuf_sgmap (bus_dma_tag_t, bus_dmamap_t,
	    struct mbuf *, int);

int	mcpcia_bus_dmamap_load_uio_sgmap (bus_dma_tag_t, bus_dmamap_t,
	    struct uio *, int);

int	mcpcia_bus_dmamap_load_raw_sgmap (bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);

void	mcpcia_bus_dmamap_unload_sgmap (bus_dma_tag_t, bus_dmamap_t);

/*
 * Direct-mapped window: 2G at 2G
 */
#define	MCPCIA_DIRECT_MAPPED_BASE	(2UL*1024UL*1024UL*1024UL)
#define	MCPCIA_DIRECT_MAPPED_SIZE	(2UL*1024UL*1024UL*1024UL)

/*
 * SGMAP window for PCI: 1G at 1G
 */
#define	MCPCIA_PCI_SG_MAPPED_BASE	(1UL*1024UL*1024UL*1024UL)
#define	MCPCIA_PCI_SG_MAPPED_SIZE	(1UL*1024UL*1024UL*1024UL)

/*
 * SGMAP window for ISA: 8M at 8M
 */
#define	MCPCIA_ISA_SG_MAPPED_BASE	(8*1024*1024)
#define	MCPCIA_ISA_SG_MAPPED_SIZE	(8*1024*1024)

#define	MCPCIA_SGTLB_INVALIDATE(ccp)					\
do {									\
	alpha_mb();							\
	REGVAL(MCPCIA_SG_TBIA(ccp)) = 0xdeadbeef;			\
	alpha_mb();							\
} while (0)

void
mcpcia_dma_init(struct mcpcia_config *ccp)
{
	bus_dma_tag_t t;

	/*
	 * Initialize the DMA tag used for direct-mapped DMA.
	 */
	t = &ccp->cc_dmat_direct;
	t->_cookie = ccp;
	t->_wbase = MCPCIA_DIRECT_MAPPED_BASE;
	t->_wsize = MCPCIA_DIRECT_MAPPED_SIZE;
	t->_next_window = &ccp->cc_dmat_pci_sgmap;
	t->_boundary = 0;
	t->_sgmap = NULL;
	t->_get_tag = mcpcia_dma_get_tag;
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
	 * Initialize the DMA tag used for sgmap-mapped PCI DMA.
	 */
	t = &ccp->cc_dmat_pci_sgmap;
	t->_cookie = ccp;
	t->_wbase = MCPCIA_PCI_SG_MAPPED_BASE;
	t->_wsize = MCPCIA_PCI_SG_MAPPED_SIZE;
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = &ccp->cc_pci_sgmap;
	t->_get_tag = mcpcia_dma_get_tag;
	t->_dmamap_create = alpha_sgmap_dmamap_create;
	t->_dmamap_destroy = alpha_sgmap_dmamap_destroy;
	t->_dmamap_load = mcpcia_bus_dmamap_load_sgmap;
	t->_dmamap_load_mbuf = mcpcia_bus_dmamap_load_mbuf_sgmap;
	t->_dmamap_load_uio = mcpcia_bus_dmamap_load_uio_sgmap;
	t->_dmamap_load_raw = mcpcia_bus_dmamap_load_raw_sgmap;
	t->_dmamap_unload = mcpcia_bus_dmamap_unload_sgmap;
	t->_dmamap_sync = _bus_dmamap_sync;

	t->_dmamem_alloc = _bus_dmamem_alloc;
	t->_dmamem_free = _bus_dmamem_free;
	t->_dmamem_map = _bus_dmamem_map;
	t->_dmamem_unmap = _bus_dmamem_unmap;
	t->_dmamem_mmap = _bus_dmamem_mmap;

	/*
	 * Initialize the DMA tag used for sgmap-mapped ISA DMA.
	 */
	t = &ccp->cc_dmat_isa_sgmap;
	t->_cookie = ccp;
	t->_wbase = MCPCIA_ISA_SG_MAPPED_BASE;
	t->_wsize = MCPCIA_ISA_SG_MAPPED_SIZE;
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = &ccp->cc_isa_sgmap;
	t->_get_tag = mcpcia_dma_get_tag;
	t->_dmamap_create = alpha_sgmap_dmamap_create;
	t->_dmamap_destroy = alpha_sgmap_dmamap_destroy;
	t->_dmamap_load = mcpcia_bus_dmamap_load_sgmap;
	t->_dmamap_load_mbuf = mcpcia_bus_dmamap_load_mbuf_sgmap;
	t->_dmamap_load_uio = mcpcia_bus_dmamap_load_uio_sgmap;
	t->_dmamap_load_raw = mcpcia_bus_dmamap_load_raw_sgmap;
	t->_dmamap_unload = mcpcia_bus_dmamap_unload_sgmap;
	t->_dmamap_sync = _bus_dmamap_sync;

	t->_dmamem_alloc = _bus_dmamem_alloc;
	t->_dmamem_free = _bus_dmamem_free;
	t->_dmamem_map = _bus_dmamem_map;
	t->_dmamem_unmap = _bus_dmamem_unmap;
	t->_dmamem_mmap = _bus_dmamem_mmap;

	/*
	 * Initialize the SGMAPs.
	 */
	alpha_sgmap_init(&ccp->cc_dmat_pci_sgmap, &ccp->cc_pci_sgmap,
	    "mcpcia pci sgmap",
	    MCPCIA_PCI_SG_MAPPED_BASE, 0, MCPCIA_PCI_SG_MAPPED_SIZE,
	    sizeof(u_int64_t), NULL, 0);

	alpha_sgmap_init(&ccp->cc_dmat_isa_sgmap, &ccp->cc_isa_sgmap,
	    "mcpcia isa sgmap",
	    MCPCIA_ISA_SG_MAPPED_BASE, 0, MCPCIA_ISA_SG_MAPPED_SIZE,
	    sizeof(u_int64_t), NULL, 0);

	/*
	 * Disable windows first.
	 */
	REGVAL(MCPCIA_W0_BASE(ccp)) = 0;
	REGVAL(MCPCIA_W1_BASE(ccp)) = 0;
	REGVAL(MCPCIA_W2_BASE(ccp)) = 0;
	REGVAL(MCPCIA_W3_BASE(ccp)) = 0;
	REGVAL(MCPCIA_T0_BASE(ccp)) = 0;
	REGVAL(MCPCIA_T1_BASE(ccp)) = 0;
	REGVAL(MCPCIA_T2_BASE(ccp)) = 0;
	REGVAL(MCPCIA_T3_BASE(ccp)) = 0;
	alpha_mb();

	/*
	 * Set up window 0 as an 8MB SGMAP-mapped window starting at 8MB.
	 */
	REGVAL(MCPCIA_W0_MASK(ccp)) = MCPCIA_WMASK_8M;
	REGVAL(MCPCIA_T0_BASE(ccp)) =
		ccp->cc_isa_sgmap.aps_ptpa >> MCPCIA_TBASEX_SHIFT;
	alpha_mb();
	REGVAL(MCPCIA_W0_BASE(ccp)) =
		MCPCIA_WBASE_EN | MCPCIA_WBASE_SG | MCPCIA_ISA_SG_MAPPED_BASE;
	alpha_mb();

	MCPCIA_SGTLB_INVALIDATE(ccp);

	/*
	 * Set up window 1 as a 2 GB Direct-mapped window starting at 2GB.
	 */
	REGVAL(MCPCIA_W1_MASK(ccp)) = MCPCIA_WMASK_2G;
	REGVAL(MCPCIA_T1_BASE(ccp)) = 0;
	alpha_mb();
	REGVAL(MCPCIA_W1_BASE(ccp)) =
		MCPCIA_DIRECT_MAPPED_BASE | MCPCIA_WBASE_EN;
	alpha_mb();

	/*
	 * Set up window 2 as a 1G SGMAP-mapped window starting at 1G.
	 */
	REGVAL(MCPCIA_W2_MASK(ccp)) = MCPCIA_WMASK_1G;
	REGVAL(MCPCIA_T2_BASE(ccp)) =
		ccp->cc_pci_sgmap.aps_ptpa >> MCPCIA_TBASEX_SHIFT;
	alpha_mb();
	REGVAL(MCPCIA_W2_BASE(ccp)) =
		MCPCIA_WBASE_EN | MCPCIA_WBASE_SG | MCPCIA_PCI_SG_MAPPED_BASE;
	alpha_mb();

	/* XXX XXX BEGIN XXX XXX */
	{							/* XXX */
		extern paddr_t alpha_XXX_dmamap_or;		/* XXX */
		alpha_XXX_dmamap_or = MCPCIA_DIRECT_MAPPED_BASE;/* XXX */
	}							/* XXX */
	/* XXX XXX END XXX XXX */
}

/*
 * Return the bus dma tag to be used for the specified bus type.
 * INTERNAL USE ONLY!
 */
bus_dma_tag_t
mcpcia_dma_get_tag(bus_dma_tag_t t, alpha_bus_t bustype)
{
	struct mcpcia_config *ccp = t->_cookie;

	switch (bustype) {
	case ALPHA_BUS_PCI:
	case ALPHA_BUS_EISA:
		/*
		 * Start off using the direct-mapped window.  We will
		 * automatically fall backed onto the chained PCI SGMAP
		 * window if necessary.
		 */
		return (&ccp->cc_dmat_direct);

	case ALPHA_BUS_ISA:
		/*
		 * ISA doesn't have enough address bits to use
		 * the direct-mapped DMA window, so we must use
		 * SGMAPs.
		 */
		return (&ccp->cc_dmat_isa_sgmap);

	default:
		panic("mcpcia_dma_get_tag: shouldn't be here, really...");
	}
}

/*
 * Load a MCPCIA SGMAP-mapped DMA map with a linear buffer.
 */
int
mcpcia_bus_dmamap_load_sgmap(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	int error;
	struct mcpcia_config *ccp = t->_cookie;

	error = pci_sgmap_pte64_load(t, map, buf, buflen, p, flags,
	    t->_sgmap);
	if (error == 0)
		MCPCIA_SGTLB_INVALIDATE(ccp);
	return (error);
}

/*
 * Load a MCPCIA SGMAP-mapped DMA map with an mbuf chain.
 */
int
mcpcia_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct mbuf *m, int flags)
{
	int error;
	struct mcpcia_config *ccp = t->_cookie;

	error = pci_sgmap_pte64_load_mbuf(t, map, m, flags, t->_sgmap);
	if (error == 0)
		MCPCIA_SGTLB_INVALIDATE(ccp);
	return (error);
}

/*
 * Load a MCPCIA SGMAP-mapped DMA map with a uio.
 */
int
mcpcia_bus_dmamap_load_uio_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct uio *uio, int flags)
{
	int error;
	struct mcpcia_config *ccp = t->_cookie;

	error = pci_sgmap_pte64_load_uio(t, map, uio, flags, t->_sgmap);
	if (error == 0)
		MCPCIA_SGTLB_INVALIDATE(ccp);
	return (error);
}

/*
 * Load a MCPCIA SGMAP-mapped DMA map with raw memory.
 */
int
mcpcia_bus_dmamap_load_raw_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	int error;
	struct mcpcia_config *ccp = t->_cookie;

	error = pci_sgmap_pte64_load_raw(t, map, segs, nsegs, size, flags,
	    t->_sgmap);
	if (error == 0)
		MCPCIA_SGTLB_INVALIDATE(ccp);
	return (error);
}

/*
 * Unload a MCPCIA DMA map.
 */
void
mcpcia_bus_dmamap_unload_sgmap(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct mcpcia_config *ccp = t->_cookie;

	/*
	 * Invalidate any SGMAP page table entries used by this mapping.
	 */
	pci_sgmap_pte64_unload(t, map, t->_sgmap);
	MCPCIA_SGTLB_INVALIDATE(ccp);

	/*
	 * Do the generic bits of the unload.
	 */
	_bus_dmamap_unload(t, map);
}
