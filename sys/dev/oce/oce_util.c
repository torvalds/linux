/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2013 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/* $FreeBSD$ */

#include "oce_if.h"

static void oce_dma_map_ring(void *arg,
			     bus_dma_segment_t *segs,
			     int nseg,
			     int error);

/**
 * @brief		Allocate DMA memory
 * @param sc		software handle to the device
 * @param size		bus size
 * @param dma		dma memory area
 * @param flags		creation flags
 * @returns		0 on success, error otherwize
 */
int
oce_dma_alloc(POCE_SOFTC sc, bus_size_t size, POCE_DMA_MEM dma, int flags)
{
	int rc;


	memset(dma, 0, sizeof(OCE_DMA_MEM));

	rc = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
				8, 0,
				BUS_SPACE_MAXADDR,
				BUS_SPACE_MAXADDR,
				NULL, NULL,
				size, 1, size, 0, NULL, NULL, &dma->tag);

	if (rc == 0) {
		rc = bus_dmamem_alloc(dma->tag,
				      &dma->ptr,
				      BUS_DMA_NOWAIT | BUS_DMA_COHERENT |
					BUS_DMA_ZERO,
				      &dma->map);
	}

	dma->paddr = 0;
	if (rc == 0) {
		rc = bus_dmamap_load(dma->tag,
				     dma->map,
				     dma->ptr,
				     size,
				     oce_dma_map_addr,
				     &dma->paddr, flags | BUS_DMA_NOWAIT);
		if (dma->paddr == 0)
			rc = ENXIO;
	}

	if (rc != 0)
		oce_dma_free(sc, dma);

	return rc;
}

/**
 * @brief		Free DMA memory
 * @param sc		software handle to the device
 * @param dma		dma area to free
 */
void
oce_dma_free(POCE_SOFTC sc, POCE_DMA_MEM dma)
{
	if (dma->tag == NULL)
		return;

	if (dma->paddr != 0) {
		bus_dmamap_sync(dma->tag, dma->map,
				BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->tag, dma->map);
		dma->paddr = 0;
	}

	if (dma->ptr != NULL) {
		bus_dmamem_free(dma->tag, dma->ptr, dma->map);
		dma->ptr = NULL;
	}

	bus_dma_tag_destroy(dma->tag);
	dma->tag = NULL;
	
	return;
}



/**
 * @brief		Map DMA memory segment addresses
 * @param arg		physical address pointer
 * @param segs		dma memory segments
 * @param nseg		number of dma memory segments
 * @param error		if error, zeroes the physical address
 */
void
oce_dma_map_addr(void *arg, bus_dma_segment_t * segs, int nseg, int error)
{
	bus_addr_t *paddr = arg;

	if (error)
		*paddr = 0;
	else
		*paddr = segs->ds_addr;
}



/**
 * @brief		Destroy a ring buffer
 * @param sc		software handle to the device
 * @param ring		ring buffer
 */

void
oce_destroy_ring_buffer(POCE_SOFTC sc, oce_ring_buffer_t *ring)
{
	oce_dma_free(sc, &ring->dma);
	free(ring, M_DEVBUF);
}



oce_ring_buffer_t *
oce_create_ring_buffer(POCE_SOFTC sc,
		uint32_t q_len, uint32_t item_size)
{
	uint32_t size = q_len * item_size;
	int rc;
	oce_ring_buffer_t *ring;


	ring = malloc(sizeof(oce_ring_buffer_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ring == NULL) 
		return NULL;

	ring->item_size = item_size;
	ring->num_items = q_len;

	rc = bus_dma_tag_create(bus_get_dma_tag(sc->dev),
				4096, 0,
				BUS_SPACE_MAXADDR,
				BUS_SPACE_MAXADDR,
				NULL, NULL,
				size, 8, 4096, 0, NULL, NULL, &ring->dma.tag);
	if (rc)
		goto fail;


	rc = bus_dmamem_alloc(ring->dma.tag,
				&ring->dma.ptr,
				BUS_DMA_NOWAIT | BUS_DMA_COHERENT,
				&ring->dma.map);
	if (rc)
		goto fail;

	bzero(ring->dma.ptr, size);
	bus_dmamap_sync(ring->dma.tag, ring->dma.map,
			BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	ring->dma.paddr = 0;
	
	return ring;
	
fail:
	oce_dma_free(sc, &ring->dma);
	free(ring, M_DEVBUF);
	ring = NULL;
	return NULL;
}



struct _oce_dmamap_paddr_table {
	uint32_t max_entries;
	uint32_t num_entries;
	struct phys_addr *paddrs;
};



/**
 * @brief		Map ring buffer
 * @param arg		dma map phyical address table pointer
 * @param segs		dma memory segments
 * @param nseg		number of dma memory segments
 * @param error		maps only if error is 0
 */
static void
oce_dma_map_ring(void *arg, bus_dma_segment_t * segs, int nseg, int error)
{
	int i;
	struct _oce_dmamap_paddr_table *dpt =
	    (struct _oce_dmamap_paddr_table *)arg;

	if (error == 0) {
		if (nseg <= dpt->max_entries) {
			for (i = 0; i < nseg; i++) {
				dpt->paddrs[i].lo = ADDR_LO(segs[i].ds_addr);
				dpt->paddrs[i].hi = ADDR_HI(segs[i].ds_addr);
			}
			dpt->num_entries = nseg;
		}
	}
}



/**
 * @brief		Load bus dma map for a ring buffer
 * @param ring		ring buffer pointer
 * @param pa_list	physical address list
 * @returns		number entries
 */
uint32_t
oce_page_list(oce_ring_buffer_t *ring, struct phys_addr *pa_list)
{
	struct _oce_dmamap_paddr_table dpt;

	dpt.max_entries = 8;
	dpt.num_entries = 0;
	dpt.paddrs = pa_list;

	bus_dmamap_load(ring->dma.tag,
			ring->dma.map,
			ring->dma.ptr,
			ring->item_size * ring->num_items,
			oce_dma_map_ring, &dpt, BUS_DMA_NOWAIT);

	return dpt.num_entries;
}
