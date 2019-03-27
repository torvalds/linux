/*-
 * Copyright (c) 2016 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <machine/bus.h>

#include <dev/hyperv/include/hyperv_busdma.h>

#define HYPERV_DMA_MASK	(BUS_DMA_WAITOK | BUS_DMA_NOWAIT | BUS_DMA_ZERO)

void
hyperv_dma_map_paddr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr = arg;

	if (error)
		return;

	KASSERT(nseg == 1, ("too many segments %d!", nseg));
	*paddr = segs->ds_addr;
}

void *
hyperv_dmamem_alloc(bus_dma_tag_t parent_dtag, bus_size_t alignment,
    bus_addr_t boundary, bus_size_t size, struct hyperv_dma *dma, int flags)
{
	void *ret;
	int error;

	error = bus_dma_tag_create(parent_dtag, /* parent */
	    alignment,		/* alignment */
	    boundary,		/* boundary */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    NULL, NULL,		/* filter, filterarg */
	    size,		/* maxsize */
	    1,			/* nsegments */
	    size,		/* maxsegsize */
	    0,			/* flags */
	    NULL,		/* lockfunc */
	    NULL,		/* lockfuncarg */
	    &dma->hv_dtag);
	if (error)
		return NULL;

	error = bus_dmamem_alloc(dma->hv_dtag, &ret,
	    (flags & HYPERV_DMA_MASK) | BUS_DMA_COHERENT, &dma->hv_dmap);
	if (error) {
		bus_dma_tag_destroy(dma->hv_dtag);
		return NULL;
	}

	error = bus_dmamap_load(dma->hv_dtag, dma->hv_dmap, ret, size,
	    hyperv_dma_map_paddr, &dma->hv_paddr, BUS_DMA_NOWAIT);
	if (error) {
		bus_dmamem_free(dma->hv_dtag, ret, dma->hv_dmap);
		bus_dma_tag_destroy(dma->hv_dtag);
		return NULL;
	}
	return ret;
}

void
hyperv_dmamem_free(struct hyperv_dma *dma, void *ptr)
{
	bus_dmamap_unload(dma->hv_dtag, dma->hv_dmap);
	bus_dmamem_free(dma->hv_dtag, ptr, dma->hv_dmap);
	bus_dma_tag_destroy(dma->hv_dtag);
}
