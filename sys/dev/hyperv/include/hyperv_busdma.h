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
 *
 * $FreeBSD$
 */

#ifndef _HYPERV_BUSDMA_H_
#define _HYPERV_BUSDMA_H_

#include <sys/param.h>
#include <sys/bus.h>
#include <machine/bus.h>

struct hyperv_dma {
	bus_addr_t	hv_paddr;
	bus_dma_tag_t	hv_dtag;
	bus_dmamap_t	hv_dmap;
};

void		hyperv_dma_map_paddr(void *arg, bus_dma_segment_t *segs,
		    int nseg, int error);
void		*hyperv_dmamem_alloc(bus_dma_tag_t parent_dtag,
		    bus_size_t alignment, bus_addr_t boundary, bus_size_t size,
		    struct hyperv_dma *dma, int flags);
void		hyperv_dmamem_free(struct hyperv_dma *dma, void *ptr);

#endif	/* !_HYPERV_BUSDMA_H_ */
