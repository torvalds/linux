/*	$OpenBSD: irongate_dma.c,v 1.5 2008/06/26 05:42:08 ray Exp $	*/
/* $NetBSD: irongate_dma.c,v 1.3 2000/06/29 08:58:47 mrg Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * DMA support for the AMD 751 (``Irongate'') core logic chipset.
 *
 * The AMD 751 is really unlike all of the other Alpha PCI core logic
 * chipsets.  Instead, it looks like a normal PC chipset (not surprising,
 * since it is used for Athlon processors).
 *
 * Because of this, it lacks all of the SGMAP hardware normally present
 * on an Alpha system, and there are no DMA windows.  Instead, a memory
 * bus address is a PCI DMA address, and ISA DMA above 16M has to be
 * bounced (this is not unlike the Jensen, actually).
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

#include <alpha/pci/irongatereg.h>
#include <alpha/pci/irongatevar.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include "isadma.h"

bus_dma_tag_t irongate_dma_get_tag(bus_dma_tag_t, alpha_bus_t);

void
irongate_dma_init(struct irongate_config *icp)
{
	bus_dma_tag_t t;

	/*
	 * Initialize the DMA tag used for PCI DMA.
	 */
	t = &icp->ic_dmat_pci;
	t->_cookie = icp;
	t->_wbase = 0;
	t->_wsize = 0x100000000UL;
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = NULL;
	t->_get_tag = irongate_dma_get_tag;
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

#if NISADMA > 0
	/*
	 * Initialize the DMA tag used for ISA DMA.
	 */
	t = &icp->ic_dmat_isa;
	t->_cookie = icp;
	t->_wbase = 0;
	t->_wsize = 0x1000000;
	t->_next_window = NULL;
	t->_boundary = 0;
	t->_sgmap = NULL;
	t->_get_tag = irongate_dma_get_tag;
	t->_dmamap_create = isadma_bounce_dmamap_create;
	t->_dmamap_destroy = isadma_bounce_dmamap_destroy;
	t->_dmamap_load = isadma_bounce_dmamap_load;
	t->_dmamap_load_mbuf = isadma_bounce_dmamap_load_mbuf;
	t->_dmamap_load_uio = isadma_bounce_dmamap_load_uio;
	t->_dmamap_load_raw = isadma_bounce_dmamap_load_raw;
	t->_dmamap_unload = isadma_bounce_dmamap_unload;
	t->_dmamap_sync = isadma_bounce_dmamap_sync;

	t->_dmamem_alloc = isadma_bounce_dmamem_alloc;
	t->_dmamem_free = _bus_dmamem_free;
	t->_dmamem_map = _bus_dmamem_map;
	t->_dmamem_unmap = _bus_dmamem_unmap;
	t->_dmamem_mmap = _bus_dmamem_mmap;
#endif

	/* XXX XXX BEGIN XXX XXX */
	{							/* XXX */
		extern paddr_t alpha_XXX_dmamap_or;		/* XXX */
		alpha_XXX_dmamap_or = 0;			/* XXX */
	}							/* XXX */
	/* XXX XXX END XXX XXX */
}

/*
 * Return the bus dma tag to be used for the specified bus type.
 * INTERNAL USE ONLY!
 */
bus_dma_tag_t
irongate_dma_get_tag(bus_dma_tag_t t, alpha_bus_t bustype)
{
	struct irongate_config *icp = t->_cookie;

	switch (bustype) {
	case ALPHA_BUS_PCI:
	case ALPHA_BUS_EISA:
		/*
		 * Busses capable of 32-bit DMA get the PCI DMA tag.
		 */
		return (&icp->ic_dmat_pci);

	case ALPHA_BUS_ISA:
		/*
		 * Lame 24-bit busses have to bounce.
		 */
		return (&icp->ic_dmat_isa);

	default:
		panic("irongate_dma_get_tag: shouldn't be here, really...");
	}
}
