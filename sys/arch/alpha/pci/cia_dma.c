/* $OpenBSD: cia_dma.c,v 1.12 2025/06/28 16:04:09 miod Exp $ */
/* $NetBSD: cia_dma.c,v 1.16 2000/06/29 08:58:46 mrg Exp $ */

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
#include <alpha/pci/ciareg.h>
#include <alpha/pci/ciavar.h>

bus_dma_tag_t cia_dma_get_tag(bus_dma_tag_t, alpha_bus_t);

int	cia_bus_dmamap_create_direct(bus_dma_tag_t, bus_size_t, int,
	    bus_size_t, bus_size_t, int, bus_dmamap_t *);
void	cia_bus_dmamap_destroy_direct(bus_dma_tag_t, bus_dmamap_t);

int	cia_bus_dmamap_load_sgmap(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);

int	cia_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    struct mbuf *, int);

int	cia_bus_dmamap_load_uio_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    struct uio *, int);

int	cia_bus_dmamap_load_raw_sgmap(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);

void	cia_bus_dmamap_unload_sgmap(bus_dma_tag_t, bus_dmamap_t);

/*
 * Direct-mapped window: 1G at 1G
 */
#define	CIA_DIRECT_MAPPED_BASE	(1*1024*1024*1024)
#define	CIA_DIRECT_MAPPED_SIZE	(1*1024*1024*1024)

/*
 * SGMAP window: 8M at 8M
 */
#define	CIA_SGMAP_MAPPED_BASE	(8*1024*1024)
#define	CIA_SGMAP_MAPPED_SIZE	(8*1024*1024)

void	cia_tlb_invalidate(void);
void	cia_broken_pyxis_tlb_invalidate(void);

void	(*cia_tlb_invalidate_fn)(void);

#define	CIA_TLB_INVALIDATE()	(*cia_tlb_invalidate_fn)()

struct alpha_sgmap cia_pyxis_bug_sgmap;
#define	CIA_PYXIS_BUG_BASE	(128*1024*1024)
#define	CIA_PYXIS_BUG_SIZE	(2*1024*1024)

void
cia_dma_init(struct cia_config *ccp)
{
	bus_addr_t tbase;
	bus_dma_tag_t t;

	/*
	 * Initialize the DMA tag used for direct-mapped DMA.
	 */
	t = &ccp->cc_dmat_direct;
	t->_cookie = ccp;
	t->_wbase = CIA_DIRECT_MAPPED_BASE;
	t->_wsize = CIA_DIRECT_MAPPED_SIZE;
	t->_next_window = &ccp->cc_dmat_sgmap;
	t->_boundary = 0;
	t->_sgmap = NULL;
	t->_get_tag = cia_dma_get_tag;
	t->_dmamap_create = cia_bus_dmamap_create_direct;
	t->_dmamap_destroy = cia_bus_dmamap_destroy_direct;
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
	t = &ccp->cc_dmat_sgmap;
	t->_cookie = ccp;
	t->_wbase = CIA_SGMAP_MAPPED_BASE;
	t->_wsize = CIA_SGMAP_MAPPED_SIZE;
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = &ccp->cc_sgmap;
	t->_get_tag = cia_dma_get_tag;
	t->_dmamap_create = alpha_sgmap_dmamap_create;
	t->_dmamap_destroy = alpha_sgmap_dmamap_destroy;
	t->_dmamap_load = cia_bus_dmamap_load_sgmap;
	t->_dmamap_load_mbuf = cia_bus_dmamap_load_mbuf_sgmap;
	t->_dmamap_load_uio = cia_bus_dmamap_load_uio_sgmap;
	t->_dmamap_load_raw = cia_bus_dmamap_load_raw_sgmap;
	t->_dmamap_unload = cia_bus_dmamap_unload_sgmap;
	t->_dmamap_sync = _bus_dmamap_sync;

	t->_dmamem_alloc = _bus_dmamem_alloc;
	t->_dmamem_free = _bus_dmamem_free;
	t->_dmamem_map = _bus_dmamem_map;
	t->_dmamem_unmap = _bus_dmamem_unmap;
	t->_dmamem_mmap = _bus_dmamem_mmap;

	/*
	 * The firmware has set up window 1 as a 1G direct-mapped DMA
	 * window beginning at 1G.  We leave it alone.  Leave window
	 * 0 alone until we reconfigure it for SGMAP-mapped DMA.
	 * Windows 2 and 3 are already disabled.
	 */

	/*
	 * Initialize the SGMAP.  Must align page table to 32k
	 * (hardware bug?).
	 */
	alpha_sgmap_init(t, &ccp->cc_sgmap, "cia_sgmap",
	    CIA_SGMAP_MAPPED_BASE, 0, CIA_SGMAP_MAPPED_SIZE,
	    sizeof(u_int64_t), NULL, (32*1024));

	/*
	 * Set up window 0 as an 8MB SGMAP-mapped window
	 * starting at 8MB.
	 */
	REGVAL(CIA_PCI_W0BASE) = CIA_SGMAP_MAPPED_BASE |
	    CIA_PCI_WnBASE_SG_EN | CIA_PCI_WnBASE_W_EN;
	alpha_mb();

	REGVAL(CIA_PCI_W0MASK) = CIA_PCI_WnMASK_8M;
	alpha_mb();

	tbase = ccp->cc_sgmap.aps_ptpa >> CIA_PCI_TnBASE_SHIFT;
	if ((tbase & CIA_PCI_TnBASE_MASK) != tbase)
		panic("cia_dma_init: bad page table address");
	REGVAL(CIA_PCI_T0BASE) = tbase;
	alpha_mb();

	/*
	 * Pass 1 and 2 (i.e. revision <= 1) of the Pyxis have a
	 * broken scatter/gather TLB; it cannot be invalidated.  To
	 * work around this problem, we configure window 2 as an SG
	 * 2M window at 128M, which we use in DMA loopback mode to
	 * read a spill page.  This works by causing TLB misses,
	 * causing the old entries to be purged to make room for
	 * the new entries coming in for the spill page.
	 */
	if ((ccp->cc_flags & CCF_ISPYXIS) != 0 && ccp->cc_rev <= 1) {
		u_int64_t *page_table;
		int i;

		cia_tlb_invalidate_fn =
		    cia_broken_pyxis_tlb_invalidate;

		alpha_sgmap_init(t, &cia_pyxis_bug_sgmap,
		    "pyxis_bug_sgmap", CIA_PYXIS_BUG_BASE, 0,
		    CIA_PYXIS_BUG_SIZE, sizeof(u_int64_t), NULL,
		    (32*1024));

		REGVAL(CIA_PCI_W2BASE) = CIA_PYXIS_BUG_BASE |
		    CIA_PCI_WnBASE_SG_EN | CIA_PCI_WnBASE_W_EN;
		alpha_mb();

		REGVAL(CIA_PCI_W2MASK) = CIA_PCI_WnMASK_2M;
		alpha_mb();

		tbase = cia_pyxis_bug_sgmap.aps_ptpa >>
		    CIA_PCI_TnBASE_SHIFT;
		if ((tbase & CIA_PCI_TnBASE_MASK) != tbase)
			panic("cia_dma_init: bad page table address");
		REGVAL(CIA_PCI_T2BASE) = tbase;
		alpha_mb();

		/*
		 * Initialize the page table to point at the spill
		 * page.  Leave the last entry invalid.
		 */
		pci_sgmap_pte64_init_spill_page_pte();
		for (i = 0, page_table = cia_pyxis_bug_sgmap.aps_pt;
		     i < (CIA_PYXIS_BUG_SIZE / PAGE_SIZE) - 1; i++) {
			page_table[i] =
			    pci_sgmap_pte64_prefetch_spill_page_pte;
		}
		alpha_mb();
	} else
		cia_tlb_invalidate_fn = cia_tlb_invalidate;

	CIA_TLB_INVALIDATE();

	/* XXX XXX BEGIN XXX XXX */
	{							/* XXX */
		extern paddr_t alpha_XXX_dmamap_or;		/* XXX */
		alpha_XXX_dmamap_or = CIA_DIRECT_MAPPED_BASE;	/* XXX */
	}							/* XXX */
	/* XXX XXX END XXX XXX */
}

/*
 * Return the bus dma tag to be used for the specified bus type.
 * INTERNAL USE ONLY!
 */
bus_dma_tag_t
cia_dma_get_tag(bus_dma_tag_t t, alpha_bus_t bustype)
{
	struct cia_config *ccp = t->_cookie;

	switch (bustype) {
	case ALPHA_BUS_PCI:
	case ALPHA_BUS_EISA:
		/*
		 * Systems with a CIA can only support 1G
		 * of memory, so we use the direct-mapped window
		 * on busses that have 32-bit DMA.
		 *
		 * Ahem:  I have a PWS 500au with 1.5G of memory, and it
		 * had problems doing DMA because it was not falling back
		 * to using SGMAPs.  I've fixed that and my PWS now works with
		 * 1.5G.  There have been other reports about failures with
		 * more than 1.0G of memory.  Michael Hitch
		 */
		return (&ccp->cc_dmat_direct);

	case ALPHA_BUS_ISA:
		/*
		 * ISA doesn't have enough address bits to use
		 * the direct-mapped DMA window, so we must use
		 * SGMAPs.
		 */
		return (&ccp->cc_dmat_sgmap);

	default:
		panic("cia_dma_get_tag: shouldn't be here, really...");
	}
}

/*
 * Create a CIA direct-mapped DMA map.
 */
int
cia_bus_dmamap_create_direct(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	struct cia_config *ccp = t->_cookie;
	bus_dmamap_t map;
	int error;

	error = _bus_dmamap_create(t, size, nsegments, maxsegsz,
	    boundary, flags, dmamp);
	if (error)
		return (error);

	/*
	 * Since we fall back to sgmap if the direct mapping fails,
	 * we need to set up for sgmap in any case.
	 */
	map = *dmamp;
	if (alpha_sgmap_dmamap_setup(map, nsegments, flags)) {
		_bus_dmamap_destroy(t, map);
		return (ENOMEM);
	}

	if ((ccp->cc_flags & CCF_PYXISBUG) != 0 &&
	    map->_dm_segcnt > 1) {
		/*
		 * We have a Pyxis with the DMA page crossing bug, make
		 * sure we don't coalesce adjacent DMA segments.
		 *
		 * NOTE: We can only do this if the max segment count
		 * is greater than 1.  This is because many network
		 * drivers allocate large contiguous blocks of memory
		 * for control data structures, even though they won't
		 * do any single DMA that crosses a page boundary.
		 *	-- thorpej@netbsd.org, 2/5/2000
		 */
		map->_dm_flags |= DMAMAP_NO_COALESCE;
	}

	return (0);
}

/*
 * Destroy a CIA direct-mapped DMA map.
 */
void
cia_bus_dmamap_destroy_direct(bus_dma_tag_t t, bus_dmamap_t map)
{
	alpha_sgmap_dmamap_teardown(map);
	_bus_dmamap_destroy(t, map);
}

/*
 * Load a CIA SGMAP-mapped DMA map with a linear buffer.
 */
int
cia_bus_dmamap_load_sgmap(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	int error;

	error = pci_sgmap_pte64_load(t, map, buf, buflen, p, flags,
	    t->_sgmap);
	if (error == 0)
		CIA_TLB_INVALIDATE();

	return (error);
}

/*
 * Load a CIA SGMAP-mapped DMA map with an mbuf chain.
 */
int
cia_bus_dmamap_load_mbuf_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct mbuf *m, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_mbuf(t, map, m, flags, t->_sgmap);
	if (error == 0)
		CIA_TLB_INVALIDATE();

	return (error);
}

/*
 * Load a CIA SGMAP-mapped DMA map with a uio.
 */
int
cia_bus_dmamap_load_uio_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    struct uio *uio, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_uio(t, map, uio, flags, t->_sgmap);
	if (error == 0)
		CIA_TLB_INVALIDATE();

	return (error);
}

/*
 * Load a CIA SGMAP-mapped DMA map with raw memory.
 */
int
cia_bus_dmamap_load_raw_sgmap(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	int error;

	error = pci_sgmap_pte64_load_raw(t, map, segs, nsegs, size, flags,
	    t->_sgmap);
	if (error == 0)
		CIA_TLB_INVALIDATE();

	return (error);
}

/*
 * Unload a CIA DMA map.
 */
void
cia_bus_dmamap_unload_sgmap(bus_dma_tag_t t, bus_dmamap_t map)
{

	/*
	 * Invalidate any SGMAP page table entries used by this
	 * mapping.
	 */
	pci_sgmap_pte64_unload(t, map, t->_sgmap);
	CIA_TLB_INVALIDATE();

	/*
	 * Do the generic bits of the unload.
	 */
	_bus_dmamap_unload(t, map);
}

/*
 * Flush the CIA scatter/gather TLB.
 */
void
cia_tlb_invalidate(void)
{

	alpha_mb();
	REGVAL(CIA_PCI_TBIA) = CIA_PCI_TBIA_ALL;
	alpha_mb();
}

/*
 * Flush the scatter/gather TLB on broken Pyxis chips.
 */
void
cia_broken_pyxis_tlb_invalidate(void)
{
	volatile u_int64_t dummy;
	u_int32_t ctrl;
	int i, s;

	s = splhigh();

	/*
	 * Put the Pyxis into PCI loopback mode.
	 */
	alpha_mb();
	ctrl = REGVAL(CIA_CSR_CTRL);
	REGVAL(CIA_CSR_CTRL) = ctrl | CTRL_PCI_LOOP_EN;
	alpha_mb();

	/*
	 * Now, read from PCI dense memory space at offset 128M (our
	 * target window base), skipping 64k on each read.  This forces
	 * S/G TLB misses.
	 *
	 * XXX Looks like the TLB entries are `not quite LRU'.  We need
	 * XXX to read more times than there are actual tags!
	 */
	for (i = 0; i < CIA_TLB_NTAGS + 4; i++) {
		dummy = *((volatile u_int64_t *)
		    ALPHA_PHYS_TO_K0SEG(CIA_PCI_DENSE + CIA_PYXIS_BUG_BASE +
		    (i * 65536)));
	}

	/*
	 * Restore normal PCI operation.
	 */
	alpha_mb();
	REGVAL(CIA_CSR_CTRL) = ctrl;
	alpha_mb();

	splx(s);
}
