/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>

#include "common/efx.h"

#include "sfxge.h"

static void
sfxge_dma_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *addr;

	addr = arg;

	if (error != 0) {
		*addr = 0;
		return;
	}

	*addr = segs[0].ds_addr;
}

int
sfxge_dma_map_sg_collapse(bus_dma_tag_t tag, bus_dmamap_t map,
			  struct mbuf **mp, bus_dma_segment_t *segs,
			  int *nsegs, int maxsegs)
{
	bus_dma_segment_t *psegs;
	struct mbuf *m;
	int seg_count;
	int defragged;
	int err;

	m = *mp;
	defragged = err = seg_count = 0;

	KASSERT(m->m_pkthdr.len, ("packet has zero header length"));

retry:
	psegs = segs;
	seg_count = 0;
	if (m->m_next == NULL) {
		sfxge_map_mbuf_fast(tag, map, m, segs);
		*nsegs = 1;
		return (0);
	}
#if defined(__i386__) || defined(__amd64__)
	while (m != NULL && seg_count < maxsegs) {
		/*
		 * firmware doesn't like empty segments
		 */
		if (m->m_len != 0) {
			seg_count++;
			sfxge_map_mbuf_fast(tag, map, m, psegs);
			psegs++;
		}
		m = m->m_next;
	}
#else
	err = bus_dmamap_load_mbuf_sg(tag, map, *mp, segs, &seg_count, 0);
#endif
	if (seg_count == 0) {
		err = EFBIG;
		goto err_out;
	} else if (err == EFBIG || seg_count >= maxsegs) {
		if (!defragged) {
			m = m_defrag(*mp, M_NOWAIT);
			if (m == NULL) {
				err = ENOBUFS;
				goto err_out;
			}
			*mp = m;
			defragged = 1;
			goto retry;
		}
		err = EFBIG;
		goto err_out;
	}
	*nsegs = seg_count;

err_out:
	return (err);
}

void
sfxge_dma_free(efsys_mem_t *esmp)
{

	bus_dmamap_unload(esmp->esm_tag, esmp->esm_map);
	bus_dmamem_free(esmp->esm_tag, esmp->esm_base, esmp->esm_map);
	bus_dma_tag_destroy(esmp->esm_tag);

	esmp->esm_addr = 0;
	esmp->esm_base = NULL;
	esmp->esm_size = 0;
}

int
sfxge_dma_alloc(struct sfxge_softc *sc, bus_size_t len, efsys_mem_t *esmp)
{
	void *vaddr;

	/* Create the child DMA tag. */
	if (bus_dma_tag_create(sc->parent_dma_tag, PAGE_SIZE, 0,
	    MIN(0x3FFFFFFFFFFFUL, BUS_SPACE_MAXADDR), BUS_SPACE_MAXADDR, NULL,
	    NULL, len, 1, len, 0, NULL, NULL, &esmp->esm_tag) != 0) {
		device_printf(sc->dev, "Couldn't allocate txq DMA tag\n");
		goto fail_tag_create;
	}

	/* Allocate kernel memory. */
	if (bus_dmamem_alloc(esmp->esm_tag, (void **)&vaddr,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &esmp->esm_map) != 0) {
		device_printf(sc->dev, "Couldn't allocate DMA memory\n");
		goto fail_alloc;
	}

	/* Load map into device memory. */
	if (bus_dmamap_load(esmp->esm_tag, esmp->esm_map, vaddr, len,
	    sfxge_dma_cb, &esmp->esm_addr, 0) != 0) {
		device_printf(sc->dev, "Couldn't load DMA mapping\n");
		goto fail_load;
	}

	/*
	 * The callback gets error information about the mapping
	 * and will have set esm_addr to 0 if something went
	 * wrong.
	 */
	if (esmp->esm_addr == 0)
		goto fail_load_check;

	esmp->esm_base = vaddr;
	esmp->esm_size = len;

	return (0);

fail_load_check:
fail_load:
	bus_dmamem_free(esmp->esm_tag, vaddr, esmp->esm_map);
fail_alloc:
	bus_dma_tag_destroy(esmp->esm_tag);
fail_tag_create:
	return (ENOMEM);
}

void
sfxge_dma_fini(struct sfxge_softc *sc)
{

	bus_dma_tag_destroy(sc->parent_dma_tag);
}

int
sfxge_dma_init(struct sfxge_softc *sc)
{

	/* Create the parent dma tag. */
	if (bus_dma_tag_create(bus_get_dma_tag(sc->dev),	/* parent */
	    1, 0,			/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    BUS_SPACE_UNRESTRICTED,	/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lock, lockarg */
	    &sc->parent_dma_tag) != 0) {
		device_printf(sc->dev, "Cannot allocate parent DMA tag\n");
		return (ENOMEM);
	}

	return (0);
}
