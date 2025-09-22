/*	$OpenBSD: msi.c,v 1.5 2020/06/23 01:21:29 jmatthew Exp $	*/
/*
 * Copyright (c) 2011 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <sparc64/dev/msivar.h>

struct msi_msg {
	uint64_t mm_data[8];
};

struct msi_eq *
msi_eq_alloc(bus_dma_tag_t t, int msi_eq_size, int num_eq)
{
	struct msi_eq *meq;
	bus_size_t eqsize, size;
	caddr_t va;
	int nsegs;

	meq = malloc(sizeof(struct msi_eq), M_DEVBUF, M_NOWAIT);
	if (meq == NULL)
		return NULL;

	eqsize = roundup(msi_eq_size * sizeof(struct msi_msg),
	    PAGE_SIZE);
	size = num_eq * eqsize;

	if (bus_dmamap_create(t, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &meq->meq_map) != 0)
		return (NULL);

	if (bus_dmamem_alloc(t, size, eqsize, 0, &meq->meq_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT) != 0)
		goto destroy;

	if (bus_dmamem_map(t, &meq->meq_seg, 1, size, &va,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(t, meq->meq_map, va, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	meq->meq_va = va;
	meq->meq_nentries = msi_eq_size;
	meq->meq_queuesize = eqsize;
	meq->meq_nqueues = num_eq;
	return (meq);

unmap:
	bus_dmamem_unmap(t, va, size);
free:
	bus_dmamem_free(t, &meq->meq_seg, 1);
destroy:
	bus_dmamap_destroy(t, meq->meq_map);

	return (NULL);
}

void
msi_eq_free(bus_dma_tag_t t, struct msi_eq *meq)
{
	bus_dmamap_unload(t, meq->meq_map);
	bus_dmamem_unmap(t, meq->meq_va, meq->meq_nqueues * meq->meq_queuesize);
	bus_dmamem_free(t, &meq->meq_seg, 1);
	bus_dmamap_destroy(t, meq->meq_map);
	free(meq, M_DEVBUF, sizeof *meq);
}

size_t
msi_eq_offset(struct msi_eq *meq, int eq)
{
	return (meq->meq_queuesize * eq);
}
