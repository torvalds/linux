/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: FreeBSD: src/sys/i386/i386/busdma_machdep.c,v 1.25 2002/01/05
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_BUS_PRIVATE_H_
#define	_MACHINE_BUS_PRIVATE_H_

#include <sys/queue.h>

/*
 * Helpers
 */
int sparc64_bus_mem_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, vm_offset_t vaddr, bus_space_handle_t *hp);
int sparc64_bus_mem_unmap(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t size);
bus_space_tag_t sparc64_alloc_bus_tag(void *cookie, int type);
bus_space_handle_t sparc64_fake_bustag(int space, bus_addr_t addr,
    struct bus_space_tag *ptag);

struct bus_dmamap_res {
	struct resource		*dr_res;
	bus_size_t		dr_used;
	SLIST_ENTRY(bus_dmamap_res)	dr_link;
};

/*
 * Callers of the bus_dma interfaces must always protect their tags and maps
 * appropriately against concurrent access. However, when a map is on a LRU
 * queue, there is a second access path to it; for this case, the locking rules
 * are given in the parenthesized comments below:
 *	q - locked by the mutex protecting the queue.
 *	p - private to the owner of the map, no access through the queue.
 *	* - comment refers to pointer target.
 * Only the owner of the map is allowed to insert the map into a queue. Removal
 * and repositioning (i.e. temporal removal and reinsertion) is allowed to all
 * if the queue lock is held.
 */
struct bus_dmamap {
	TAILQ_ENTRY(bus_dmamap)	dm_maplruq;		/* (q) */
	SLIST_HEAD(, bus_dmamap_res)	dm_reslist;	/* (q, *q) */
	int			dm_onq;			/* (q) */
	int			dm_flags;		/* (p) */
};

/* Flag values */
#define	DMF_LOADED	(1 << 0)	/* Map is loaded. */
#define	DMF_COHERENT	(1 << 1)	/* Coherent mapping requested. */
#define	DMF_STREAMED	(1 << 2)	/* Streaming cache used. */

int sparc64_dma_alloc_map(bus_dma_tag_t dmat, bus_dmamap_t *mapp);
void sparc64_dma_free_map(bus_dma_tag_t dmat, bus_dmamap_t map);

#endif /* !_MACHINE_BUS_PRIVATE_H_ */
