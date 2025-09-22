/*	$OpenBSD: viommuvar.h,v 1.2 2009/01/02 20:01:45 kettenis Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis
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

#ifndef _SPARC64_DEV_VIOMMUVAR_H_
#define _SPARC64_DEV_VIOMMUVAR_H_

#include <sparc64/dev/iommuvar.h>

/* interfaces for PCI code */
void	viommu_init(char *, struct iommu_state *, int, u_int32_t);

/* bus_dma_tag_t implementation functions */
int	viommu_dvmamap_create(bus_dma_tag_t, bus_dma_tag_t,
	    struct iommu_state *, bus_size_t, int, bus_size_t, bus_size_t,
	    int, bus_dmamap_t *);
void	viommu_dvmamap_destroy(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
int	viommu_dvmamap_load(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);
void	viommu_dvmamap_unload(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
int	viommu_dvmamap_load_raw(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);
void	viommu_dvmamap_sync(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
	    bus_addr_t, bus_size_t, int);
int	viommu_dvmamem_alloc(bus_dma_tag_t, bus_dma_tag_t, bus_size_t,
	    bus_size_t, bus_size_t, bus_dma_segment_t *, int, int *, int);
void	viommu_dvmamem_free(bus_dma_tag_t, bus_dma_tag_t, bus_dma_segment_t *,
	    int);

#endif
