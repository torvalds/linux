/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Jason A. Harmening.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BUS_DMA_INTERNAL_H_
#define _BUS_DMA_INTERNAL_H_

/*
 * The following functions define the interface between the MD and MI
 * busdma layers.  These are not intended for consumption by driver
 * software.
 */

bus_dma_segment_t	*_bus_dmamap_complete(bus_dma_tag_t dmat,
			    bus_dmamap_t map, bus_dma_segment_t *segs,
			    int nsegs, int error);

int	_bus_dmamap_load_buffer(bus_dma_tag_t dmat, bus_dmamap_t map,
	    void *buf, bus_size_t buflen, struct pmap *pmap,
	    int flags, bus_dma_segment_t *segs, int *segp);

int	_bus_dmamap_load_ma(bus_dma_tag_t dmat, bus_dmamap_t map,
	    struct vm_page **ma, bus_size_t tlen, int ma_offs,
	    int flags, bus_dma_segment_t *segs, int *segp);

int	_bus_dmamap_load_phys(bus_dma_tag_t dmat, bus_dmamap_t map,
	    vm_paddr_t paddr, bus_size_t buflen,
	    int flags, bus_dma_segment_t *segs, int *segp);

void	_bus_dmamap_waitok(bus_dma_tag_t dmat, bus_dmamap_t map,
	    struct memdesc *mem, bus_dmamap_callback_t *callback,
	    void *callback_arg);

#endif /* !_BUS_DMA_INTERNAL_H_ */

