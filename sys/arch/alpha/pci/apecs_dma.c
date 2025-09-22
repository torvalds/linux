/* $OpenBSD: apecs_dma.c,v 1.8 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: apecs_dma.c,v 1.13 2000/06/29 08:58:45 mrg Exp $ */

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

/*
 * XXX - We should define this before including bus.h, but since other stuff
 *       pulls in bus.h we must do this here.
 */
#define _ALPHA_BUS_DMA_PRIVATE

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/apecsreg.h>
#include <alpha/pci/apecsvar.h>

bus_dma_tag_t apecs_dma_get_tag(bus_dma_tag_t, alpha_bus_t);

int	apecs_bus_dmamap_load_sgmap(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);

int	apecs_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    struct mbuf *, int);

int	apecs_bus_dmamap_load_uio_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    struct uio *, int);

int	apecs_bus_dmamap_load_raw_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);

void	apecs_bus_dmamap_unload_sgmap(bus_dma_tag_t, bus_dmamap_t);

/*
 * Direct-mapped window: 1G at 1G
 */
#define	APECS_DIRECT_MAPPED_BASE (1*1024*1024*1024)
#define	APECS_DIRECT_MAPPED_SIZE (1*1024*1024*1024)

/*
 * SGMAP window: 8M at 8M
 */
#define	APECS_SGMAP_MAPPED_BASE	(8*1024*1024)
#define	APECS_SGMAP_MAPPED_SIZE	(8*1024*1024)

/*
 * Macro to flush APECS scatter/gather TLB.
 */
#define	APECS_TLB_INVALIDATE() \
do { \
	alpha_mb(); \
	REGVAL(EPIC_TBIA) = 0; \
	alpha_mb(); \
} while (0)

void
apecs_dma_init(struct apecs_config *acp)
{
	bus_addr_t tbase;
	bus_dma_tag_t t;

	/*
	 * Initialize the DMA tag used for direct-mapped DMA.
	 */
	t = &acp->ac_dmat_direct;
	t->_cookie = acp;
	t->_wbase = APECS_DIRECT_MAPPED_BASE;
	t->_wsize = APECS_DIRECT_MAPPED_SIZE;
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = NULL;
	t->_get_tag = apecs_dma_get_tag;
	t->_dmamap_create = _bus_dmamap_create;
	t->_dmamap_destroy = _bus_dmamap_destroy;
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
	t = &acp->ac_dmat_sgmap;
	t->_cookie = acp;
	t->_wbase = APECS_SGMAP_MAPPED_BASE;
	t->_wsize = APECS_SGMAP_MAPPED_SIZE;
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = &acp->ac_sgmap;
	t->_get_tag = apecs_dma_get_tag;
	t->_dmamap_create = alpha_sgmap_dmamap_create;
	t->_dmamap_destroy = alpha_sgmap_dmamap_destroy;
	t->_dmamap_load = apecs_bus_dmamap_load_sgmap;
	t->_dmamap_load_mbuf = apecs_bus_dmamap_load_mbuf_sgmap;
	t->_dmamap_load_uio = apecs_bus_dmamap_load_uio_sgmap;
	t->_dmamap_load_raw = apecs_bus_dmamap_load_raw_sgmap;
	t->_dmamap_unload = apecs_bus_dmamap_unload_sgmap;
	t->_dmamap_sync = _bus_dmamap_sync;

	t->_dmamem_alloc = _bus_dmamem_alloc;
	t->_dmamem_free = _bus_dmamem_free;
	t->_dmamem_map = _bus_dmamem_map;
	t->_dmamem_unmap = _bus_dmamem_unmap;
	t->_dmamem_mmap = _bus_dmamem_mmap;

	/*
	 * The firmware has set up window 2 as a 1G direct-mapped DMA
	 * window beginning at 1G.  We leave it alone.  Disable
	 * window 1.
	 */
	REGVAL(EPIC_PCI_BASE_1) = 0;
	alpha_mb();

	/*
	 * Initialize the SGMAP.
	 */
	alpha_sgmap_init(t, &acp->ac_sgmap, "apecs_sgmap",
	    APECS_SGMAP_MAPPED_BASE, 0, APECS_SGMAP_MAPPED_SIZE,
	    sizeof(u_int64_t), NULL, 0);

	/*
	 * Set up window 1 as an 8MB SGMAP-mapped window
	 * starting at 8MB.
	 */
	REGVAL(EPIC_PCI_BASE_1) = APECS_SGMAP_MAPPED_BASE |
	    EPIC_PCI_BASE_SGEN | EPIC_PCI_BASE_WENB;
	alpha_mb();

	REGVAL(EPIC_PCI_MASK_1) = EPIC_PCI_MASK_8M;
	alpha_mb();

	tbase = acp->ac_sgmap.aps_ptpa >> EPIC_TBASE_SHIFT;
	if ((tbase & EPIC_TBASE_T_BASE) != tbase)
		panic("apecs_dma_init: bad page table address");
	REGVAL(EPIC_TBASE_1) = tbase;
	alpha_mb();

	APECS_TLB_INVALIDATE();

	/* XXX XXX BEGIN XXX XXX */
	{							/* XXX */
		extern paddr_t alpha_XXX_dmamap_or;		/* XXX */
		alpha_XXX_dmamap_or = APECS_DIRECT_MAPPED_BASE;	/* XXX */
	}							/* XXX */
	/* XXX XXX END XXX XXX */
}

/*
 * Return the bus dma tag to be used for the specified bus type.
 * INTERNAL USE ONLY!
 */
bus_dma_tag_t
apecs_dma_get_tag(bus_dma_tag_t t, alpha_bus_t bustype)
{
	struct apecs_config *acp = t->_cookie;

	switch (bustype) {
	case ALPHA_BUS_PCI:
	case ALPHA_BUS_EISA:
		/*
		 * Systems with an APECS can only support 1G
		 * of memory, so we use the direct-mapped window
		 * on busses that have 32-bit DMA.
		 */
		return (&acp->ac_dmat_direct);

	case ALPHA_BUS_ISA:
		/*
		 * ISA doesn't have enough address bits to use
		 * the direct-mapped DMA window, so we must use
		 * SGMAPs.
		 */
		return (&acp->ac_dmat_sgmap);

	default:
		panic("apecs_dma_get_tag: shouldn't be here, really...");
	}
}

/*
 * Load an APECS SGMAP-mapped DMA map with a linear buffer.
 */
int
apecs_bus_dmamap_load_sgmap(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	int error;

	error = pci_sgmap_pte64_load(t, map, buf, buflen, p, flags,
	    t->_sgmap);
	if (error == 0)
		APECS_TLB_INVALIDATE();

	return (error);
}

/*
 * Load an APECS SGMAP-mapped DMA map with an mbuf chain.
 */
int
apecs_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct mbuf *m, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_mbuf(t, map, m, flags, t->_sgmap);
	if (error == 0)
		APECS_TLB_INVALIDATE();

	return (error);
}

/*
 * Load an APECS SGMAP-mapped DMA map with a uio.
 */
int
apecs_bus_dmamap_load_uio_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct uio *uio, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_uio(t, map, uio, flags, t->_sgmap);
	if (error == 0)
		APECS_TLB_INVALIDATE();

	return (error);
}

/*
 * Load an APECS SGMAP-mapped DMA map with raw memory.
 */
int
apecs_bus_dmamap_load_raw_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
   bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_raw(t, map, segs, nsegs, size, flags,
	    t->_sgmap);
	if (error == 0)
		APECS_TLB_INVALIDATE();

	return (error);
}

/*
 * Unload an APECS DMA map.
 */
void
apecs_bus_dmamap_unload_sgmap(bus_dma_tag_t t, bus_dmamap_t map)
{

	/*
	 * Invalidate any SGMAP page table entries used by this
	 * mapping.
	 */
	pci_sgmap_pte64_unload(t, map, t->_sgmap);
	APECS_TLB_INVALIDATE();

	/*
	 * Do the generic bits of the unload.
	 */
	_bus_dmamap_unload(t, map);
}
