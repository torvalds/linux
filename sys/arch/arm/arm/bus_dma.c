/*	$OpenBSD: bus_dma.c,v 1.41 2021/03/25 04:12:00 jsg Exp $	*/
/*	$NetBSD: bus_dma.c,v 1.38 2003/10/30 08:44:13 scw Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

#define _ARM32_BUS_DMA_PRIVATE

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <arm/cpufunc.h>

/*
 * Common function for DMA map creation.  May be called by bus-specific
 * DMA map creation functions.
 */
int
_bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	struct arm32_bus_dmamap *map;
	void *mapstore;
	size_t mapsize;

#ifdef DEBUG_DMA
	printf("dmamap_create: t=%p size=%lx nseg=%x msegsz=%lx boundary=%lx flags=%x\n",
	    t, size, nsegments, maxsegsz, boundary, flags);
#endif	/* DEBUG_DMA */

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
	mapsize = sizeof(struct arm32_bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (nsegments - 1));
	if ((mapstore = malloc(mapsize, M_DEVBUF, (flags & BUS_DMA_NOWAIT) ?
	    (M_NOWAIT | M_ZERO) : (M_WAITOK | M_ZERO))) == NULL)
		return (ENOMEM);

	map = (struct arm32_bus_dmamap *)mapstore;
	map->_dm_size = size;
	map->_dm_segcnt = nsegments;
	map->_dm_maxsegsz = maxsegsz;
	map->_dm_boundary = boundary;
	map->_dm_flags = flags & ~(BUS_DMA_WAITOK|BUS_DMA_NOWAIT);
	map->_dm_origbuf = NULL;
	map->_dm_buftype = ARM32_BUFTYPE_INVALID;
	map->_dm_proc = NULL;
	map->dm_mapsize = 0;		/* no valid mappings */
	map->dm_nsegs = 0;

	*dmamp = map;
#ifdef DEBUG_DMA
	printf("dmamap_create:map=%p\n", map);
#endif	/* DEBUG_DMA */
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

#ifdef DEBUG_DMA
	printf("dmamap_destroy: t=%p map=%p\n", t, map);
#endif	/* DEBUG_DMA */

	mapsize = sizeof(struct arm32_bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (map->_dm_segcnt - 1));
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
	paddr_t lastaddr;
	int seg, error;

#ifdef DEBUG_DMA
	printf("dmamap_load: t=%p map=%p buf=%p len=%lx p=%p f=%d\n",
	    t, map, buf, buflen, p, flags);
#endif	/* DEBUG_DMA */

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	if (buflen > map->_dm_size)
		return (EINVAL);

	/* _bus_dmamap_load_buffer() clears this if we're not... */
	map->_dm_flags |= ARM32_DMAMAP_COHERENT;

	seg = 0;
	error = (*t->_dmamap_load_buffer)(t, map, buf, buflen, p, flags,
	    &lastaddr, &seg, 1);
	if (error == 0) {
		map->dm_mapsize = buflen;
		map->dm_nsegs = seg + 1;
		map->_dm_origbuf = buf;
		map->_dm_buftype = ARM32_BUFTYPE_LINEAR;
		map->_dm_proc = p;
	}
#ifdef DEBUG_DMA
	printf("dmamap_load: error=%d\n", error);
#endif	/* DEBUG_DMA */
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for mbufs.
 */
int
_bus_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t map, struct mbuf *m0,
    int flags)
{
	paddr_t lastaddr;
	int seg, error, first;
	struct mbuf *m;

#ifdef DEBUG_DMA
	printf("dmamap_load_mbuf: t=%p map=%p m0=%p f=%d\n",
	    t, map, m0, flags);
#endif	/* DEBUG_DMA */

	/*
	 * Make sure that on error condition we return "no valid mappings."
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

#ifdef DIAGNOSTIC
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("_bus_dmamap_load_mbuf: no packet header");
#endif	/* DIAGNOSTIC */

	if (m0->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

	/*
	 * Mbuf chains should almost never have coherent (i.e.
	 * un-cached) mappings, so clear that flag now.
	 */
	map->_dm_flags &= ~ARM32_DMAMAP_COHERENT;

	first = 1;
	seg = 0;
	error = 0;
	for (m = m0; m != NULL && error == 0; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		error = (*t->_dmamap_load_buffer)(t, map, m->m_data, m->m_len,
		    NULL, flags, &lastaddr, &seg, first);
		first = 0;
	}
	if (error == 0) {
		map->dm_mapsize = m0->m_pkthdr.len;
		map->dm_nsegs = seg + 1;
		map->_dm_origbuf = m0;
		map->_dm_buftype = ARM32_BUFTYPE_MBUF;
		map->_dm_proc = NULL;	/* always kernel */
	}
#ifdef DEBUG_DMA
	printf("dmamap_load_mbuf: error=%d\n", error);
#endif	/* DEBUG_DMA */
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for uios.
 */
int
_bus_dmamap_load_uio(bus_dma_tag_t t, bus_dmamap_t map, struct uio *uio,
    int flags)
{
	paddr_t lastaddr;
	int seg, i, error, first;
	bus_size_t minlen, resid;
	struct proc *p = NULL;
	struct iovec *iov;
	caddr_t addr;

	/*
	 * Make sure that on error condition we return "no valid mappings."
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

	/* _bus_dmamap_load_buffer() clears this if we're not... */
	map->_dm_flags |= ARM32_DMAMAP_COHERENT;

	first = 1;
	seg = 0;
	error = 0;
	for (i = 0; i < uio->uio_iovcnt && resid != 0 && error == 0; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		minlen = resid < iov[i].iov_len ? resid : iov[i].iov_len;
		addr = (caddr_t)iov[i].iov_base;

		error = (*t->_dmamap_load_buffer)(t, map, addr, minlen,
		    p, flags, &lastaddr, &seg, first);
		first = 0;

		resid -= minlen;
	}
	if (error == 0) {
		map->dm_mapsize = uio->uio_resid;
		map->dm_nsegs = seg + 1;
		map->_dm_origbuf = uio;
		map->_dm_buftype = ARM32_BUFTYPE_UIO;
		map->_dm_proc = p;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for raw memory allocated with
 * bus_dmamem_alloc().
 */
int
_bus_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	bus_addr_t paddr, baddr, bmask, lastaddr = 0;
	bus_size_t plen, sgsize, mapsize;
	vaddr_t vaddr;
	int first = 1;
	int i, seg = 0;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	if (nsegs > map->_dm_segcnt || size > map->_dm_size)
		return (EINVAL);

	mapsize = size;
	bmask = ~(map->_dm_boundary - 1);

	/*
	 * Assume to be coherent at first.  If one of the segments
	 * isn't, we clear the flag for the whole map.
	 */
	map->_dm_flags |= ARM32_DMAMAP_COHERENT;

	for (i = 0; i < nsegs && size > 0; i++) {
		paddr = segs[i].ds_addr;
		vaddr = segs[i]._ds_vaddr;
		plen = MIN(segs[i].ds_len, size);

		if (!segs[i]._ds_coherent)
			map->_dm_flags &= ~ARM32_DMAMAP_COHERENT;

		while (plen > 0) {
			/*
			 * Compute the segment size, and adjust counts.
			 */
			sgsize = PAGE_SIZE - ((u_long)paddr & PGOFSET);
			if (plen < sgsize)
				sgsize = plen;

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
				map->dm_segs[seg]._ds_vaddr = vaddr;
				first = 0;
			} else {
				if (paddr == lastaddr &&
				    (map->dm_segs[seg].ds_len + sgsize) <=
				     map->_dm_maxsegsz &&
				    (map->_dm_boundary == 0 ||
				     (map->dm_segs[seg].ds_addr & bmask) ==
				     (paddr & bmask)))
					map->dm_segs[seg].ds_len += sgsize;
				else {
					if (++seg >= map->_dm_segcnt)
						return (EINVAL);
					map->dm_segs[seg].ds_addr = paddr;
					map->dm_segs[seg].ds_len = sgsize;
					map->dm_segs[seg]._ds_vaddr = vaddr;
				}
			}

			paddr += sgsize;
			vaddr += sgsize;
			plen -= sgsize;
			size -= sgsize;

			lastaddr = paddr;
		}
	}

	map->dm_mapsize = mapsize;
	map->dm_nsegs = seg + 1;
	map->_dm_buftype = ARM32_BUFTYPE_RAW;
	map->_dm_origbuf = NULL;
	map->_dm_proc = NULL;
	return (0);
}

/*
 * Common function for unloading a DMA map.  May be called by
 * bus-specific DMA map unload functions.
 */
void
_bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{

#ifdef DEBUG_DMA
	printf("dmamap_unload: t=%p map=%p\n", t, map);
#endif	/* DEBUG_DMA */

	/*
	 * No resources to free; just mark the mappings as
	 * invalid.
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;
	map->_dm_origbuf = NULL;
	map->_dm_buftype = ARM32_BUFTYPE_INVALID;
	map->_dm_proc = NULL;
}

static void
_bus_dmamap_sync_segment(vaddr_t va, paddr_t pa, vsize_t len, int ops)
{
	KASSERT((va & PAGE_MASK) == (pa & PAGE_MASK));

#ifdef DEBUG_DMA
	printf("sync_segment: va=%#lx pa=%#lx len=%#lx ops=%#x\n",
	    va, pa, len, ops);
#endif

	switch (ops) {
	case BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE:
		cpu_dcache_wbinv_range(va, len);
		cpu_sdcache_wbinv_range(va, pa, len);
		break;

	case BUS_DMASYNC_PREREAD: {
		const size_t line_size = arm_dcache_align;
		const size_t line_mask = arm_dcache_align_mask;
		vsize_t misalignment = va & line_mask;
		if (misalignment) {
			va -= misalignment;
			pa -= misalignment;
			len += misalignment;
			cpu_dcache_wbinv_range(va, line_size);
			cpu_sdcache_wbinv_range(va, pa, line_size);
			if (len <= line_size)
				break;
			va += line_size;
			pa += line_size;
			len -= line_size;
		}
		misalignment = len & line_mask;
		len -= misalignment;
		if (len > 0) {
			cpu_dcache_inv_range(va, len);
			cpu_sdcache_inv_range(va, pa, len);
		}
		if (misalignment) {
			va += len;
			pa += len;
			cpu_dcache_wbinv_range(va, line_size);
			cpu_sdcache_wbinv_range(va, pa, line_size);
		}
		break;
	}

	case BUS_DMASYNC_PREWRITE:
		cpu_dcache_wb_range(va, len);
		cpu_sdcache_wb_range(va, pa, len);
		break;

	/*
	 * Cortex CPUs can do speculative loads so we need to clean the cache
	 * after a DMA read to deal with any speculatively loaded cache lines.
	 * Since these can't be dirty, we can just invalidate them and don't
	 * have to worry about having to write back their contents.
	 */
	case BUS_DMASYNC_POSTREAD:
	case BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE:
		membar_sync();
		cpu_dcache_inv_range(va, len);
		cpu_sdcache_inv_range(va, pa, len);
		break;
	}
}

static __inline void
_bus_dmamap_sync_linear(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops)
{
	bus_dma_segment_t *ds = map->dm_segs;
	vaddr_t va = (vaddr_t) map->_dm_origbuf;

	while (len > 0) {
		while (offset >= ds->ds_len) {
			offset -= ds->ds_len;
			va += ds->ds_len;
			ds++;
		}

		paddr_t pa = ds->ds_addr + offset;
		size_t seglen = min(len, ds->ds_len - offset);

		_bus_dmamap_sync_segment(va + offset, pa, seglen, ops);

		offset += seglen;
		len -= seglen;
	}
}

static __inline void
_bus_dmamap_sync_mbuf(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops)
{
	bus_dma_segment_t *ds = map->dm_segs;
	struct mbuf *m = map->_dm_origbuf;
	bus_size_t voff = offset;
	bus_size_t ds_off = offset;

	while (len > 0) {
		/* Find the current dma segment */
		while (ds_off >= ds->ds_len) {
			ds_off -= ds->ds_len;
			ds++;
		}
		/* Find the current mbuf. */
		while (voff >= m->m_len) {
			voff -= m->m_len;
			m = m->m_next;
		}

		/*
		 * Now at the first mbuf to sync; nail each one until
		 * we have exhausted the length.
		 */
		vsize_t seglen = min(len, min(m->m_len - voff, ds->ds_len - ds_off));
		vaddr_t va = mtod(m, vaddr_t) + voff;
		paddr_t pa = ds->ds_addr + ds_off;

		/*
		 * We can save a lot of work here if we know the mapping
		 * is read-only at the MMU:
		 *
		 * If a mapping is read-only, no dirty cache blocks will
		 * exist for it.  If a writable mapping was made read-only,
		 * we know any dirty cache lines for the range will have
		 * been cleaned for us already.  Therefore, if the upper
		 * layer can tell us we have a read-only mapping, we can
		 * skip all cache cleaning.
		 *
		 * NOTE: This only works if we know the pmap cleans pages
		 * before making a read-write -> read-only transition.  If
		 * this ever becomes non-true (e.g. Physically Indexed
		 * cache), this will have to be revisited.
		 */
		_bus_dmamap_sync_segment(va, pa, seglen, ops);

		voff += seglen;
		ds_off += seglen;
		len -= seglen;
	}
}

static __inline void
_bus_dmamap_sync_uio(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops)
{
	bus_dma_segment_t *ds = map->dm_segs;
	struct uio *uio = map->_dm_origbuf;
	struct iovec *iov = uio->uio_iov;
	bus_size_t voff = offset;
	bus_size_t ds_off = offset;

	while (len > 0) {
		/* Find the current dma segment */
		while (ds_off >= ds->ds_len) {
			ds_off -= ds->ds_len;
			ds++;
		}

		/* Find the current iovec. */
		while (voff >= iov->iov_len) {
			voff -= iov->iov_len;
			iov++;
		}

		/*
		 * Now at the first iovec to sync; nail each one until
		 * we have exhausted the length.
		 */
		vsize_t seglen = min(len, min(iov->iov_len - voff, ds->ds_len - ds_off));
		vaddr_t va = (vaddr_t) iov->iov_base + voff;
		paddr_t pa = ds->ds_addr + ds_off;

		_bus_dmamap_sync_segment(va, pa, seglen, ops);

		voff += seglen;
		ds_off += seglen;
		len -= seglen;
	}
}

static __inline void
_bus_dmamap_sync_raw(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops)
{
	bus_dma_segment_t *ds = map->dm_segs;

	while (len > 0) {
		while (offset >= ds->ds_len) {
			offset -= ds->ds_len;
			ds++;
		}

		vaddr_t va = ds->_ds_vaddr + offset;
		paddr_t pa = ds->ds_addr + offset;
		size_t seglen = min(len, ds->ds_len - offset);

		_bus_dmamap_sync_segment(va, pa, seglen, ops);

		offset += seglen;
		len -= seglen;
	}
}

/*
 * Common function for DMA map synchronization.  May be called
 * by bus-specific DMA map synchronization functions.
 *
 * This version works for the Virtually Indexed Virtually Tagged
 * cache found on 32-bit ARM processors.
 *
 * XXX Should have separate versions for write-through vs.
 * XXX write-back caches.  We currently assume write-back
 * XXX here, which is not as efficient as it could be for
 * XXX the write-through case.
 */
void
_bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops)
{

#ifdef DEBUG_DMA
	printf("dmamap_sync: t=%p map=%p offset=%lx len=%lx ops=%x\n",
	    t, map, offset, len, ops);
#endif	/* DEBUG_DMA */

	/*
	 * Mixing of PRE and POST operations is not allowed.
	 */
	if ((ops & (BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)) != 0 &&
	    (ops & (BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE)) != 0)
		panic("_bus_dmamap_sync: mix PRE and POST");

#ifdef DIAGNOSTIC
	if (offset >= map->dm_mapsize)
		panic("_bus_dmamap_sync: bad offset %lu (map size is %lu)",
		    offset, map->dm_mapsize);
	if ((offset + len) > map->dm_mapsize)
		panic("_bus_dmamap_sync: bad length");
#endif

	/*
	 * For a virtually-indexed write-back cache, we need
	 * to do the following things:
	 *
	 *	PREREAD -- Invalidate the D-cache.  We do this
	 *	here in case a write-back is required by the back-end.
	 *
	 *	PREWRITE -- Write-back the D-cache.  Note that if
	 *	we are doing a PREREAD|PREWRITE, we can collapse
	 *	the whole thing into a single Wb-Inv.
	 *
	 *	POSTREAD -- Invalidate the D-Cache. Contents of
	 *	the cache could be from before a device wrote
	 *	to the memory.
	 *
	 *	POSTWRITE -- Nothing.
	 */

	/* Skip cache frobbing if mapping was COHERENT. */
	if (map->_dm_flags & ARM32_DMAMAP_COHERENT) {
		/* Drain the write buffer. */
		cpu_drain_writebuf();
		cpu_sdcache_drain_writebuf();
		return;
	}

	/*
	 * If the mapping belongs to a non-kernel vmspace, and the
	 * vmspace has not been active since the last time a full
	 * cache flush was performed, we don't need to do anything.
	 */
	if (__predict_false(map->_dm_proc != NULL &&
	    map->_dm_proc->p_vmspace->vm_map.pmap->pm_cstate.cs_cache_d == 0))
		return;

	switch (map->_dm_buftype) {
	case ARM32_BUFTYPE_LINEAR:
		_bus_dmamap_sync_linear(t, map, offset, len, ops);
		break;

	case ARM32_BUFTYPE_MBUF:
		_bus_dmamap_sync_mbuf(t, map, offset, len, ops);
		break;

	case ARM32_BUFTYPE_UIO:
		_bus_dmamap_sync_uio(t, map, offset, len, ops);
		break;

	case ARM32_BUFTYPE_RAW:
		_bus_dmamap_sync_raw(t, map, offset, len, ops);
		break;

	case ARM32_BUFTYPE_INVALID:
		panic("_bus_dmamap_sync: ARM32_BUFTYPE_INVALID");
		break;

	default:
		printf("unknown buffer type %d\n", map->_dm_buftype);
		panic("_bus_dmamap_sync");
	}

	/* Drain the write buffer. */
	cpu_drain_writebuf();
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
	int error;

#ifdef DEBUG_DMA
	printf("dmamem_alloc t=%p size=%lx align=%lx boundary=%lx "
	    "segs=%p nsegs=%x rsegs=%p flags=%x\n", t, size, alignment,
	    boundary, segs, nsegs, rsegs, flags);
#endif

	error = _bus_dmamem_alloc_range(t, size, alignment, boundary,
	    segs, nsegs, rsegs, flags, 0, -1);

#ifdef DEBUG_DMA
	printf("dmamem_alloc: =%d\n", error);
#endif

	return(error);
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

#ifdef DEBUG_DMA
	printf("dmamem_free: t=%p segs=%p nsegs=%x\n", t, segs, nsegs);
#endif	/* DEBUG_DMA */

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
	int curseg;
	const struct kmem_dyn_mode *kd;
#ifdef DEBUG_DMA
	pt_entry_t *ptep;
#endif

#ifdef DEBUG_DMA
	printf("dmamem_map: t=%p segs=%p nsegs=%x size=%lx flags=%x\n", t,
	    segs, nsegs, (unsigned long)size, flags);
#endif	/* DEBUG_DMA */

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
#ifdef DEBUG_DMA
			printf("wiring p%lx to v%lx", addr, va);
#endif	/* DEBUG_DMA */
			if (size == 0)
				panic("_bus_dmamem_map: size botch");
			segs[curseg]._ds_coherent =
			    !!(flags & BUS_DMA_COHERENT);
			segs[curseg]._ds_vaddr = va;
			pmap_kenter_cache(va, addr,
			    PROT_READ | PROT_WRITE,
			    !(flags & BUS_DMA_COHERENT));

#ifdef DEBUG_DMA
			ptep = vtopte(va);
			printf(" pte=v%p *pte=%x\n", ptep, *ptep);
#endif	/* DEBUG_DMA */
		}
	}
	pmap_update(pmap_kernel());
#ifdef DEBUG_DMA
	printf("dmamem_map: =%p\n", *kvap);
#endif	/* DEBUG_DMA */
	return (0);
}

/*
 * Common function for unmapping DMA-safe memory.  May be called by
 * bus-specific DMA memory unmapping functions.
 */
void
_bus_dmamem_unmap(bus_dma_tag_t t, caddr_t kva, size_t size)
{

#ifdef DEBUG_DMA
	printf("dmamem_unmap: t=%p kva=%p size=%lx\n", t, kva,
	    (unsigned long)size);
#endif	/* DEBUG_DMA */
#ifdef DIAGNOSTIC
	if ((u_long)kva & PGOFSET)
		panic("_bus_dmamem_unmap");
#endif	/* DIAGNOSTIC */

	km_free(kva, round_page(size), &kv_any, &kp_none);
}

/*
 * Common function for mmap(2)'ing DMA-safe memory.  May be called by
 * bus-specific DMA mmap(2)'ing functions.
 */
paddr_t
_bus_dmamem_mmap(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    off_t off, int prot, int flags)
{
	int i;

	for (i = 0; i < nsegs; i++) {
#ifdef DIAGNOSTIC
		if (off & PGOFSET)
			panic("_bus_dmamem_mmap: offset unaligned");
		if (segs[i].ds_addr & PGOFSET)
			panic("_bus_dmamem_mmap: segment unaligned");
		if (segs[i].ds_len & PGOFSET)
			panic("_bus_dmamem_mmap: segment size not multiple"
			    " of page size");
#endif	/* DIAGNOSTIC */
		if (off >= segs[i].ds_len) {
			off -= segs[i].ds_len;
			continue;
		}

		return (segs[i].ds_addr + off);
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
    int *segp, int first)
{
	bus_size_t sgsize;
	bus_addr_t curaddr, lastaddr, baddr, bmask;
	vaddr_t vaddr = (vaddr_t)buf;
	pd_entry_t *pde;
	pt_entry_t pte;
	int seg;
	pmap_t pmap;
	pt_entry_t *ptep;

#ifdef DEBUG_DMA
	printf("_bus_dmamem_load_buffer(buf=%p, len=%lx, flags=%d, 1st=%d)\n",
	    buf, buflen, flags, first);
#endif	/* DEBUG_DMA */

	if (p != NULL)
		pmap = p->p_vmspace->vm_map.pmap;
	else
		pmap = pmap_kernel();

	lastaddr = *lastaddrp;
	bmask  = ~(map->_dm_boundary - 1);

	for (seg = *segp; buflen > 0; ) {
		/*
		 * Get the physical address for this segment.
		 *
		 * XXX Don't support checking for coherent mappings
		 * XXX in user address space.
		 */
		if (__predict_true(pmap == pmap_kernel())) {
			(void) pmap_get_pde_pte(pmap, vaddr, &pde, &ptep);
			if (__predict_false(pmap_pde_section(pde))) {
				curaddr = (*pde & L1_S_FRAME) |
				    (vaddr & L1_S_OFFSET);
				if (*pde & L1_S_COHERENT) {
					map->_dm_flags &=
					    ~ARM32_DMAMAP_COHERENT;
				}
			} else {
				pte = *ptep;
				KDASSERT((pte & L2_TYPE_MASK) != L2_TYPE_INV);
				if (__predict_false((pte & L2_TYPE_MASK)
						    == L2_TYPE_L)) {
					curaddr = (pte & L2_L_FRAME) |
					    (vaddr & L2_L_OFFSET);
					if (pte & L2_L_COHERENT) {
						map->_dm_flags &=
						    ~ARM32_DMAMAP_COHERENT;
					}
				} else {
					curaddr = (pte & L2_S_FRAME) |
					    (vaddr & L2_S_OFFSET);
					if (pte & L2_S_COHERENT) {
						map->_dm_flags &=
						    ~ARM32_DMAMAP_COHERENT;
					}
				}
			}
		} else {
			(void) pmap_extract(pmap, vaddr, &curaddr);
			map->_dm_flags &= ~ARM32_DMAMAP_COHERENT;
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
			first = 0;
		} else {
			if (curaddr == lastaddr &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->_dm_maxsegsz &&
			    (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     (curaddr & bmask)))
				map->dm_segs[seg].ds_len += sgsize;
			else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr = curaddr;
				map->dm_segs[seg].ds_len = sgsize;
			}
		}

		lastaddr = curaddr + sgsize;
		vaddr += sgsize;
		buflen -= sgsize;
	}

	*segp = seg;
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
    int flags, paddr_t low, paddr_t high)
{
	paddr_t curaddr, lastaddr;
	struct vm_page *m;
	struct pglist mlist;
	int curseg, error, plaflag;

#ifdef DEBUG_DMA
	printf("alloc_range: t=%p size=%lx align=%lx boundary=%lx segs=%p nsegs=%x rsegs=%p flags=%x lo=%lx hi=%lx\n",
	    t, size, alignment, boundary, segs, nsegs, rsegs, flags, low, high);
#endif	/* DEBUG_DMA */

	/* Always round the size. */
	size = round_page(size);

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
#ifdef DEBUG_DMA
		printf("alloc: page %lx\n", lastaddr);
#endif	/* DEBUG_DMA */
	m = TAILQ_NEXT(m, pageq);

	for (; m != NULL; m = TAILQ_NEXT(m, pageq)) {
		curaddr = VM_PAGE_TO_PHYS(m);
#ifdef DIAGNOSTIC
		if (curaddr < low || curaddr >= high) {
			printf("uvm_pglistalloc returned non-sensical"
			    " address 0x%lx\n", curaddr);
			panic("_bus_dmamem_alloc_range");
		}
#endif	/* DIAGNOSTIC */
#ifdef DEBUG_DMA
		printf("alloc: page %lx\n", curaddr);
#endif	/* DEBUG_DMA */
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

/*
 * probably should be ppc_space_copy
 */

#define _CONCAT(A,B) A ## B
#define __C(A,B)	_CONCAT(A,B)

#define BUS_SPACE_READ_RAW_MULTI_N(BYTES,SHIFT,TYPE)			\
void									\
__C(bus_space_read_raw_multi_,BYTES)(bus_space_tag_t bst,		\
    bus_space_handle_t h, bus_addr_t o, u_int8_t *dst, bus_size_t size)	\
{									\
	TYPE *rdst = (TYPE *)dst;					\
	int i;								\
	int count = size >> SHIFT;					\
									\
	for (i = 0; i < count; i++) {					\
		rdst[i] = __bs_rs(BYTES, bst, h, o);			\
	}								\
}
BUS_SPACE_READ_RAW_MULTI_N(2,1,u_int16_t)
BUS_SPACE_READ_RAW_MULTI_N(4,2,u_int32_t)

#define BUS_SPACE_WRITE_RAW_MULTI_N(BYTES,SHIFT,TYPE)			\
void									\
__C(bus_space_write_raw_multi_,BYTES)( bus_space_tag_t bst,		\
    bus_space_handle_t h, bus_addr_t o, const u_int8_t *src,		\
    bus_size_t size)							\
{									\
	int i;								\
	TYPE *rsrc = (TYPE *)src;					\
	int count = size >> SHIFT;					\
									\
	for (i = 0; i < count; i++) {					\
		__bs_ws(BYTES, bst, h, o, rsrc[i]);			\
	}								\
}

BUS_SPACE_WRITE_RAW_MULTI_N(2,1,u_int16_t)
BUS_SPACE_WRITE_RAW_MULTI_N(4,2,u_int32_t)
