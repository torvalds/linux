/*	$OpenBSD: bus_dma.c,v 1.60 2025/03/13 13:24:04 bluhm Exp $	*/
/*	$NetBSD: bus_dma.c,v 1.3 2003/05/07 21:33:58 fvdl Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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
 * The following is included because _bus_dma_uiomove is derived from
 * uiomove() in kern_subr.c.
 */

/*
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>

#include <machine/bus.h>

#include <uvm/uvm_extern.h>

/* #define FORCE_BOUNCE_BUFFER	1 */
#ifndef FORCE_BOUNCE_BUFFER
#define FORCE_BOUNCE_BUFFER	0
#endif

int _bus_dmamap_load_buffer(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t,
    struct proc *, int, paddr_t *, int *, int *, int);

/*
 * Common function for DMA map creation.  May be called by bus-specific
 * DMA map creation functions.
 */
int
_bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	struct bus_dmamap *map;
	struct pglist mlist;
	struct vm_page **pg, *pgnext;
	size_t mapsize, sz, ssize;
	vaddr_t va, sva;
	void *mapstore;
	int npages, error;
	const struct kmem_dyn_mode *kd;
	/* allocate and use bounce buffers when running as SEV guest */
	int use_bounce_buffer = cpu_sev_guestmode || FORCE_BOUNCE_BUFFER;

	/*
	 * Allocate and initialize the DMA map.  The end of the map
	 * is a variable-sized array of segments, so we allocate enough
	 * room for them in one shot.
	 *
	 * Note we don't preserve the WAITOK or NOWAIT flags.  Preservation
	 * of ALLOCNOW notifies others that we've reserved these resources,
	 * and they are not to be freed.
	 *
	 * The bus_dmamap_t includes one bus_dma_segment_t, hence
	 * the (nsegments - 1).
	 */
	mapsize = sizeof(struct bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (nsegments - 1));

	if (use_bounce_buffer) {
		/* this many pages plus one in case we get split */
		npages = round_page(size) / PAGE_SIZE + 1;
		if (npages < nsegments)
			npages = nsegments;
		mapsize += sizeof(struct vm_page *) * npages;
	}

	mapstore = malloc(mapsize, M_DEVBUF,
	    (flags & BUS_DMA_NOWAIT) ? (M_NOWAIT|M_ZERO) : (M_WAITOK|M_ZERO));
	if (mapstore == NULL)
		return (ENOMEM);

	map = (struct bus_dmamap *)mapstore;
	map->_dm_size = size;
	map->_dm_segcnt = nsegments;
	map->_dm_maxsegsz = maxsegsz;
	map->_dm_boundary = boundary;
	if (use_bounce_buffer) {
		map->_dm_pages = (void *)&map->dm_segs[nsegments];
		map->_dm_npages = npages;
	}
	map->_dm_flags = flags & ~(BUS_DMA_WAITOK|BUS_DMA_NOWAIT);

	if (!use_bounce_buffer) {
		*dmamp = map;
		return (0);
	}

	sz = npages << PGSHIFT;
	kd = flags & BUS_DMA_NOWAIT ? &kd_trylock : &kd_waitok;
	va = (vaddr_t)km_alloc(sz, &kv_any, &kp_none, kd);
	if (va == 0) {
		map->_dm_npages = 0;
		free(map, M_DEVBUF, mapsize);
		return (ENOMEM);
	}

	TAILQ_INIT(&mlist);
	error = uvm_pglistalloc(sz, 0, -1, PAGE_SIZE, 0, &mlist, nsegments,
	    (flags & BUS_DMA_NOWAIT) ? UVM_PLA_NOWAIT : UVM_PLA_WAITOK);
	if (error) {
		map->_dm_npages = 0;
		km_free((void *)va, sz, &kv_any, &kp_none);
		free(map, M_DEVBUF, mapsize);
		return (ENOMEM);
	}

	sva = va;
	ssize = sz;
	pgnext = TAILQ_FIRST(&mlist);
	for (pg = map->_dm_pages; npages--; va += PAGE_SIZE, pg++) {
		*pg = pgnext;
		error = pmap_enter(pmap_kernel(), va, VM_PAGE_TO_PHYS(*pg),
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED |
		    PMAP_CANFAIL | PMAP_NOCRYPT);
		if (error) {
			pmap_update(pmap_kernel());
			map->_dm_npages = 0;
			km_free((void *)sva, ssize, &kv_any, &kp_none);
			free(map, M_DEVBUF, mapsize);
			uvm_pglistfree(&mlist);
			return (ENOMEM);
		}
		pgnext = TAILQ_NEXT(*pg, pageq);
		bzero((void *)va, PAGE_SIZE);
	}
	pmap_update(pmap_kernel());
	map->_dm_pgva = sva;

	*dmamp = map;
	return (0);
}

/*
 * Common function for DMA map destruction.  May be called by bus-specific
 * DMA map destruction functions.
 */
void
_bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	size_t mapsize;
	struct vm_page **pg;
	struct pglist mlist;
	int use_bounce_buffer = cpu_sev_guestmode || FORCE_BOUNCE_BUFFER;

	if (map->_dm_pgva) {
		km_free((void *)map->_dm_pgva, map->_dm_npages << PGSHIFT,
		    &kv_any, &kp_none);
	}

	mapsize = sizeof(struct bus_dmamap) +
		(sizeof(bus_dma_segment_t) * (map->_dm_segcnt - 1));
	if (use_bounce_buffer)
		mapsize += sizeof(struct vm_page *) * map->_dm_npages;

	if (map->_dm_pages) {
		TAILQ_INIT(&mlist);
		for (pg = map->_dm_pages; map->_dm_npages--; pg++) {
			TAILQ_INSERT_TAIL(&mlist, *pg, pageq);
		}
		uvm_pglistfree(&mlist);
	}

	free(map, M_DEVBUF, mapsize);
}

/*
 * Common function for loading a DMA map with a linear buffer.  May
 * be called by bus-specific DMA map load functions.
 */
int
_bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	bus_addr_t lastaddr = 0;
	int seg, used, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	if (buflen > map->_dm_size)
		return (EINVAL);

	seg = 0;
	used = 0;
	error = _bus_dmamap_load_buffer(t, map, buf, buflen, p, flags,
	    &lastaddr, &seg, &used, 1);
	if (error == 0) {
		map->dm_mapsize = buflen;
		map->dm_nsegs = seg + 1;
		map->_dm_nused = used;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for mbufs.
 */
int
_bus_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t map, struct mbuf *m0,
    int flags)
{
	paddr_t lastaddr = 0;
	int seg, used, error, first;
	struct mbuf *m;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

#ifdef DIAGNOSTIC
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("_bus_dmamap_load_mbuf: no packet header");
#endif

	if (m0->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

	first = 1;
	seg = 0;
	used = 0;
	error = 0;
	for (m = m0; m != NULL && error == 0; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		error = _bus_dmamap_load_buffer(t, map, m->m_data, m->m_len,
		    NULL, flags, &lastaddr, &seg, &used, first);
		first = 0;
	}
	if (error == 0) {
		map->dm_mapsize = m0->m_pkthdr.len;
		map->dm_nsegs = seg + 1;
		map->_dm_nused = used;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for uios.
 */
int
_bus_dmamap_load_uio(bus_dma_tag_t t, bus_dmamap_t map, struct uio *uio,
    int flags)
{
	paddr_t lastaddr = 0;
	int seg, used, i, error, first;
	bus_size_t minlen, resid;
	struct proc *p = NULL;
	struct iovec *iov;
	caddr_t addr;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	resid = uio->uio_resid;
	iov = uio->uio_iov;

	if (uio->uio_segflg == UIO_USERSPACE) {
		p = uio->uio_procp;
#ifdef DIAGNOSTIC
		if (p == NULL)
			panic("_bus_dmamap_load_uio: USERSPACE but no proc");
#endif
	}

	first = 1;
	seg = 0;
	used = 0;
	error = 0;
	for (i = 0; i < uio->uio_iovcnt && resid != 0 && error == 0; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		minlen = resid < iov[i].iov_len ? resid : iov[i].iov_len;
		addr = (caddr_t)iov[i].iov_base;

		error = _bus_dmamap_load_buffer(t, map, addr, minlen,
		    p, flags, &lastaddr, &seg, &used, first);
		first = 0;

		resid -= minlen;
	}
	if (error == 0) {
		map->dm_mapsize = uio->uio_resid;
		map->dm_nsegs = seg + 1;
		map->_dm_nused = used;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for raw memory allocated with
 * bus_dmamem_alloc().
 */
int
_bus_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map, bus_dma_segment_t *segs,
    int nsegs, bus_size_t size, int flags)
{
	bus_addr_t paddr, baddr, bmask, lastaddr = 0;
	bus_size_t plen, sgsize, mapsize;
	int first = 1;
	int i, seg = 0;
	int page, off;
	vaddr_t pgva, vaddr;
	int use_bounce_buffer = cpu_sev_guestmode || FORCE_BOUNCE_BUFFER;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	if (nsegs > map->_dm_segcnt || size > map->_dm_size)
		return (EINVAL);

	page = 0;
	pgva = -1;
	vaddr = -1;

	mapsize = size;
	bmask  = ~(map->_dm_boundary - 1);

	for (i = 0; i < nsegs && size > 0; i++) {
		paddr = segs[i].ds_addr;
		plen = MIN(segs[i].ds_len, size);

		while (plen > 0) {
			if (use_bounce_buffer) {
				if (page >= map->_dm_npages)
					return (EFBIG);

				off = paddr & PAGE_MASK;
				vaddr = PMAP_DIRECT_MAP(paddr);
				pgva = map->_dm_pgva + (page << PGSHIFT) + off;
				page++;
			}

			/*
			 * Compute the segment size, and adjust counts.
			 */
			sgsize = PAGE_SIZE - ((u_long)paddr & PGOFSET);
			if (plen < sgsize)
				sgsize = plen;

			if (paddr > dma_constraint.ucr_high &&
			    (map->_dm_flags & BUS_DMA_64BIT) == 0)
				panic("Non dma-reachable buffer at "
				    "paddr %#lx(raw)", paddr);

			/*
			 * Make sure we don't cross any boundaries.
			 */
			if (map->_dm_boundary > 0) {
				baddr = (paddr + map->_dm_boundary) & bmask;
				if (sgsize > (baddr - paddr))
					sgsize = (baddr - paddr);
			}

			/*
			 * Insert chunk into a segment, coalescing with
			 * previous segment if possible.
			 */
			if (first) {
				map->dm_segs[seg].ds_addr = paddr;
				map->dm_segs[seg].ds_len = sgsize;
				map->dm_segs[seg]._ds_va = vaddr;
				map->dm_segs[seg]._ds_bounce_va = pgva;
				first = 0;
			} else {
				if (paddr == lastaddr &&
				    (map->dm_segs[seg].ds_len + sgsize) <=
				     map->_dm_maxsegsz &&
				    (map->_dm_boundary == 0 ||
				     (map->dm_segs[seg].ds_addr & bmask) ==
				     (paddr & bmask)) &&
				    (!use_bounce_buffer ||
				     (map->dm_segs[seg]._ds_va +
				     map->dm_segs[seg].ds_len) == vaddr)) {
					map->dm_segs[seg].ds_len += sgsize;
				} else {
					if (++seg >= map->_dm_segcnt)
						return (EINVAL);
					map->dm_segs[seg].ds_addr = paddr;
					map->dm_segs[seg].ds_len = sgsize;
					map->dm_segs[seg]._ds_va = vaddr;
					map->dm_segs[seg]._ds_bounce_va = pgva;
				}
			}

			paddr += sgsize;
			plen -= sgsize;
			size -= sgsize;

			lastaddr = paddr;
		}
	}

	map->dm_mapsize = mapsize;
	map->dm_nsegs = seg + 1;
	return (0);
}

/*
 * Common function for unloading a DMA map.  May be called by
 * bus-specific DMA map unload functions.
 */
void
_bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	/*
	 * No resources to free; just mark the mappings as
	 * invalid.
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;
	map->_dm_nused = 0;
}

/*
 * Common function for DMA map synchronization.  May be called
 * by bus-specific DMA map synchronization functions.
 */
void
_bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t addr,
    bus_size_t size, int op)
{
	bus_dma_segment_t *sg;
	int i, off = addr;
	bus_size_t l;
	int use_bounce_buffer = cpu_sev_guestmode || FORCE_BOUNCE_BUFFER;

	if (!use_bounce_buffer)
		return;

	for (i = map->_dm_segcnt, sg = map->dm_segs; size && i; i--, sg++) {
		if (off >= sg->ds_len) {
			off -= sg->ds_len;
			continue;
		}

		l = sg->ds_len - off;
		if (l > size)
			l = size;
		size -= l;

		/* PREREAD and POSTWRITE are no-ops. */

		/* READ: device -> memory */
		if (op & BUS_DMASYNC_POSTREAD) {
			bcopy((void *)(sg->_ds_bounce_va + off),
			    (void *)(sg->_ds_va + off), l);
		}

		/* WRITE: memory -> device */
		if (op & BUS_DMASYNC_PREWRITE) {
			bcopy((void *)(sg->_ds_va + off),
			    (void *)(sg->_ds_bounce_va + off), l);
		}

		off = 0;
	}
}

/*
 * Common function for DMA-safe memory allocation.  May be called
 * by bus-specific DMA memory allocation functions.
 */
int
_bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags)
{
	paddr_t low, high;

	if (flags & BUS_DMA_64BIT) {
		low = no_constraint.ucr_low;
		high = no_constraint.ucr_high;
	} else {
		low = dma_constraint.ucr_low;
		high = dma_constraint.ucr_high;
	}

	return _bus_dmamem_alloc_range(t, size, alignment, boundary,
	    segs, nsegs, rsegs, flags, low, high);
}

/*
 * Common function for freeing DMA-safe memory.  May be called by
 * bus-specific DMA memory free functions.
 */
void
_bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs)
{
	struct vm_page *m;
	bus_addr_t addr;
	struct pglist mlist;
	int curseg;

	/*
	 * Build a list of pages to free back to the VM system.
	 */
	TAILQ_INIT(&mlist);
	for (curseg = 0; curseg < nsegs; curseg++) {
		for (addr = segs[curseg].ds_addr;
		    addr < (segs[curseg].ds_addr + segs[curseg].ds_len);
		    addr += PAGE_SIZE) {
			m = PHYS_TO_VM_PAGE(addr);
			TAILQ_INSERT_TAIL(&mlist, m, pageq);
		}
	}

	uvm_pglistfree(&mlist);
}

/*
 * Common function for mapping DMA-safe memory.  May be called by
 * bus-specific DMA memory map functions.
 */
int
_bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    size_t size, caddr_t *kvap, int flags)
{
	vaddr_t va, sva;
	size_t ssize;
	bus_addr_t addr;
	int curseg, pmapflags = 0, error;
	const struct kmem_dyn_mode *kd;

	if (nsegs == 1 && (flags & BUS_DMA_NOCACHE) == 0) {
		*kvap = (caddr_t)PMAP_DIRECT_MAP(segs[0].ds_addr);
		return (0);
	}

	if (flags & BUS_DMA_NOCACHE)
		pmapflags |= PMAP_NOCACHE;

	size = round_page(size);
	kd = flags & BUS_DMA_NOWAIT ? &kd_trylock : &kd_waitok;
	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, kd);
	if (va == 0)
		return (ENOMEM);

	*kvap = (caddr_t)va;

	sva = va;
	ssize = size;
	for (curseg = 0; curseg < nsegs; curseg++) {
		for (addr = segs[curseg].ds_addr;
		    addr < (segs[curseg].ds_addr + segs[curseg].ds_len);
		    addr += PAGE_SIZE, va += PAGE_SIZE, size -= PAGE_SIZE) {
			if (size == 0)
				panic("_bus_dmamem_map: size botch");
			error = pmap_enter(pmap_kernel(), va, addr | pmapflags,
			    PROT_READ | PROT_WRITE,
			    PROT_READ | PROT_WRITE | PMAP_WIRED | PMAP_CANFAIL);
			if (error) {
				pmap_update(pmap_kernel());
				km_free((void *)sva, ssize, &kv_any, &kp_none);
				return (error);
			}
		}
	}
	pmap_update(pmap_kernel());

	return (0);
}

/*
 * Common function for unmapping DMA-safe memory.  May be called by
 * bus-specific DMA memory unmapping functions.
 */
void
_bus_dmamem_unmap(bus_dma_tag_t t, caddr_t kva, size_t size)
{

#ifdef DIAGNOSTIC
	if ((u_long)kva & PGOFSET)
		panic("_bus_dmamem_unmap");
#endif
	if (kva >= (caddr_t)PMAP_DIRECT_BASE && kva <= (caddr_t)PMAP_DIRECT_END)
		return;

	km_free(kva, round_page(size), &kv_any, &kp_none);
}

/*
 * Common function for mmap(2)'ing DMA-safe memory.  May be called by
 * bus-specific DMA mmap(2)'ing functions.
 */
paddr_t
_bus_dmamem_mmap(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs, off_t off,
    int prot, int flags)
{
	int i, pmapflags = 0;

	if (flags & BUS_DMA_NOCACHE)
		pmapflags |= PMAP_NOCACHE;

	for (i = 0; i < nsegs; i++) {
#ifdef DIAGNOSTIC
		if (off & PGOFSET)
			panic("_bus_dmamem_mmap: offset unaligned");
		if (segs[i].ds_addr & PGOFSET)
			panic("_bus_dmamem_mmap: segment unaligned");
		if (segs[i].ds_len & PGOFSET)
			panic("_bus_dmamem_mmap: segment size not multiple"
			    " of page size");
#endif
		if (off >= segs[i].ds_len) {
			off -= segs[i].ds_len;
			continue;
		}

		return ((segs[i].ds_addr + off) | pmapflags);
	}

	/* Page not found. */
	return (-1);
}

/**********************************************************************
 * DMA utility functions
 **********************************************************************/
/*
 * Utility function to load a linear buffer.  lastaddrp holds state
 * between invocations (for multiple-buffer loads).  segp contains
 * the starting segment on entrance, and the ending segment on exit.
 * first indicates if this is the first invocation of this function.
 */
int
_bus_dmamap_load_buffer(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags, paddr_t *lastaddrp,
    int *segp, int *usedp, int first)
{
	bus_size_t sgsize;
	bus_addr_t curaddr, lastaddr, baddr, bmask;
	vaddr_t pgva = -1, vaddr = (vaddr_t)buf;
	int seg, page, off;
	pmap_t pmap;
	struct vm_page *pg;
	int use_bounce_buffer = cpu_sev_guestmode || FORCE_BOUNCE_BUFFER;

	if (p != NULL)
		pmap = p->p_vmspace->vm_map.pmap;
	else
		pmap = pmap_kernel();

	page = *usedp;
	lastaddr = *lastaddrp;
	bmask  = ~(map->_dm_boundary - 1);

	for (seg = *segp; buflen > 0 ; ) {
		/*
		 * Get the physical address for this segment.
		 */
		pmap_extract(pmap, vaddr, (paddr_t *)&curaddr);

		if (curaddr > dma_constraint.ucr_high &&
		    (map->_dm_flags & BUS_DMA_64BIT) == 0)
			panic("Non dma-reachable buffer at curaddr %#lx(raw)",
			    curaddr);

		if (use_bounce_buffer) {
			if (page >= map->_dm_npages)
				return (EFBIG);

			off = vaddr & PAGE_MASK;
			pg = map->_dm_pages[page];
			curaddr = VM_PAGE_TO_PHYS(pg) + off;
			pgva = map->_dm_pgva + (page << PGSHIFT) + off;
			page++;
		}

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)vaddr & PGOFSET);
		if (buflen < sgsize)
			sgsize = buflen;

		/*
		 * Make sure we don't cross any boundaries.
		 */
		if (map->_dm_boundary > 0) {
			baddr = (curaddr + map->_dm_boundary) & bmask;
			if (sgsize > (baddr - curaddr))
				sgsize = (baddr - curaddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * previous segment if possible.
		 */
		if (first) {
			map->dm_segs[seg].ds_addr = curaddr;
			map->dm_segs[seg].ds_len = sgsize;
			map->dm_segs[seg]._ds_va = vaddr;
			map->dm_segs[seg]._ds_bounce_va = pgva;
			first = 0;
		} else {
			if (curaddr == lastaddr &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->_dm_maxsegsz &&
			    (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     (curaddr & bmask)) &&
			    (!use_bounce_buffer || (map->dm_segs[seg]._ds_va +
			     map->dm_segs[seg].ds_len) == vaddr)) {
				map->dm_segs[seg].ds_len += sgsize;
			} else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr = curaddr;
				map->dm_segs[seg].ds_len = sgsize;
				map->dm_segs[seg]._ds_va = vaddr;
				map->dm_segs[seg]._ds_bounce_va = pgva;
			}
		}

		lastaddr = curaddr + sgsize;
		vaddr += sgsize;
		buflen -= sgsize;
	}

	*segp = seg;
	*usedp = page;
	*lastaddrp = lastaddr;

	/*
	 * Did we fit?
	 */
	if (buflen != 0)
		return (EFBIG);		/* XXX better return value here? */
	return (0);
}

/*
 * Allocate physical memory from the given physical address range.
 * Called by DMA-safe memory allocation methods.
 */
int
_bus_dmamem_alloc_range(bus_dma_tag_t t, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags, bus_addr_t low, bus_addr_t high)
{
	paddr_t curaddr, lastaddr;
	struct vm_page *m;
	struct pglist mlist;
	int curseg, error, plaflag;

	/* Always round the size. */
	size = round_page(size);

	segs[0]._ds_boundary = boundary;
	segs[0]._ds_align = alignment;

	/*
	 * Allocate pages from the VM system.
	 */
	plaflag = flags & BUS_DMA_NOWAIT ? UVM_PLA_NOWAIT : UVM_PLA_WAITOK;
	if (flags & BUS_DMA_ZERO)
		plaflag |= UVM_PLA_ZERO;

	TAILQ_INIT(&mlist);
	error = uvm_pglistalloc(size, low, high, alignment, boundary,
	    &mlist, nsegs, plaflag);
	if (error)
		return (error);

	/*
	 * Compute the location, size, and number of segments actually
	 * returned by the VM code.
	 */
	m = TAILQ_FIRST(&mlist);
	curseg = 0;
	lastaddr = segs[curseg].ds_addr = VM_PAGE_TO_PHYS(m);
	segs[curseg].ds_len = PAGE_SIZE;

	for (m = TAILQ_NEXT(m, pageq); m != NULL; m = TAILQ_NEXT(m, pageq)) {
		curaddr = VM_PAGE_TO_PHYS(m);
#ifdef DIAGNOSTIC
		if (curseg == nsegs) {
			printf("uvm_pglistalloc returned too many\n");
			panic("_bus_dmamem_alloc_range");
		}
		if (curaddr < low || curaddr >= high) {
			printf("uvm_pglistalloc returned non-sensical"
			    " address 0x%lx\n", curaddr);
			panic("_bus_dmamem_alloc_range");
		}
#endif
		if (curaddr == (lastaddr + PAGE_SIZE))
			segs[curseg].ds_len += PAGE_SIZE;
		else {
			curseg++;
			segs[curseg].ds_addr = curaddr;
			segs[curseg].ds_len = PAGE_SIZE;
		}
		lastaddr = curaddr;
	}

	*rsegs = curseg + 1;

	return (0);
}
