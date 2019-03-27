/*-
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _MACHINE_BUS_DMA_IMPL_H_
#define	_MACHINE_BUS_DMA_IMPL_H_

struct bus_dma_tag_common {
	struct bus_dma_impl *impl;
	struct bus_dma_tag_common *parent;
	bus_size_t	  alignment;
	bus_addr_t	  boundary;
	bus_addr_t	  lowaddr;
	bus_addr_t	  highaddr;
	bus_dma_filter_t *filter;
	void		 *filterarg;
	bus_size_t	  maxsize;
	u_int		  nsegments;
	bus_size_t	  maxsegsz;
	int		  flags;
	bus_dma_lock_t	 *lockfunc;
	void		 *lockfuncarg;
	int		  ref_count;
};

struct bus_dma_impl {
	int (*tag_create)(bus_dma_tag_t parent,
	    bus_size_t alignment, bus_addr_t boundary, bus_addr_t lowaddr,
	    bus_addr_t highaddr, bus_dma_filter_t *filter,
	    void *filterarg, bus_size_t maxsize, int nsegments,
	    bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
	    void *lockfuncarg, bus_dma_tag_t *dmat);
	int (*tag_destroy)(bus_dma_tag_t dmat);
	int (*map_create)(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp);
	int (*map_destroy)(bus_dma_tag_t dmat, bus_dmamap_t map);
	int (*mem_alloc)(bus_dma_tag_t dmat, void** vaddr, int flags,
	    bus_dmamap_t *mapp);
	void (*mem_free)(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map);
	int (*load_ma)(bus_dma_tag_t dmat, bus_dmamap_t map,
	    struct vm_page **ma, bus_size_t tlen, int ma_offs, int flags,
	    bus_dma_segment_t *segs, int *segp);
	int (*load_phys)(bus_dma_tag_t dmat, bus_dmamap_t map,
	    vm_paddr_t buf, bus_size_t buflen, int flags,
	    bus_dma_segment_t *segs, int *segp);
	int (*load_buffer)(bus_dma_tag_t dmat, bus_dmamap_t map,
	    void *buf, bus_size_t buflen, struct pmap *pmap, int flags,
	    bus_dma_segment_t *segs, int *segp);
	void (*map_waitok)(bus_dma_tag_t dmat, bus_dmamap_t map,
	    struct memdesc *mem, bus_dmamap_callback_t *callback,
	    void *callback_arg);
	bus_dma_segment_t *(*map_complete)(bus_dma_tag_t dmat, bus_dmamap_t map,
	    bus_dma_segment_t *segs, int nsegs, int error);
	void (*map_unload)(bus_dma_tag_t dmat, bus_dmamap_t map);
	void (*map_sync)(bus_dma_tag_t dmat, bus_dmamap_t map,
	    bus_dmasync_op_t op);
};

void bus_dma_dflt_lock(void *arg, bus_dma_lock_op_t op);
int bus_dma_run_filter(struct bus_dma_tag_common *dmat, bus_addr_t paddr);
int common_bus_dma_tag_create(struct bus_dma_tag_common *parent,
    bus_size_t alignment,
    bus_addr_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filter, void *filterarg, bus_size_t maxsize,
    int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
    void *lockfuncarg, size_t sz, void **dmat);

extern struct bus_dma_impl bus_dma_bounce_impl;

#endif
