/*	$OpenBSD: bus_dma.c,v 1.14 2018/01/11 15:49:34 visa Exp $	*/
/*	$NetBSD: bus_dma.c,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

/*
 * Copyright (c) 2005 NONAKA Kimihiro
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <uvm/uvm_extern.h>

#include <sh/cache.h>

#include <machine/autoconf.h>
#define	_LANDISK_BUS_DMA_PRIVATE
#include <machine/bus.h>

#if defined(DEBUG) && defined(BUSDMA_DEBUG)
#define	DPRINTF(a)	printf a
#else
#define	DPRINTF(a)
#endif

struct _bus_dma_tag landisk_bus_dma = {
	._cookie = NULL,

	._dmamap_create = _bus_dmamap_create,
	._dmamap_destroy = _bus_dmamap_destroy,
	._dmamap_load = _bus_dmamap_load,
	._dmamap_load_mbuf = _bus_dmamap_load_mbuf,
	._dmamap_load_uio = _bus_dmamap_load_uio,
	._dmamap_load_raw = _bus_dmamap_load_raw,
	._dmamap_unload = _bus_dmamap_unload,
	._dmamap_sync = _bus_dmamap_sync,

	._dmamem_alloc = _bus_dmamem_alloc,
	._dmamem_free = _bus_dmamem_free,
	._dmamem_map = _bus_dmamem_map,
	._dmamem_unmap = _bus_dmamem_unmap,
	._dmamem_mmap = _bus_dmamem_mmap,
};

#define DMAMAP_RESET(_m) do { \
	(_m)->dm_mapsize = 0; \
	(_m)->dm_nsegs = 0; \
} while (0)

int	_bus_dmamap_load_vaddr(bus_dma_tag_t, bus_dmamap_t,
	    void *, bus_size_t, pmap_t);
int	_bus_dmamap_load_paddr(bus_dma_tag_t, bus_dmamap_t,
	    paddr_t, vaddr_t, bus_size_t);

/*
 * Create a DMA map.
 */
int
_bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	bus_dmamap_t map;
	void *mapstore;
	size_t mapsize;
	int error;

	DPRINTF(("bus_dmamap_create: t = %p, size = %ld, nsegments = %d, maxsegsz = %ld, boundary = %ld, flags = %x\n", t, size, nsegments, maxsegsz, boundary, flags));

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
	error = 0;
	mapsize = sizeof(struct _bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (nsegments - 1));
	if ((mapstore = malloc(mapsize, M_DEVBUF, (flags & BUS_DMA_NOWAIT) ?
	    (M_NOWAIT | M_ZERO) : (M_WAITOK | M_ZERO))) == NULL)
		return (ENOMEM);

	DPRINTF(("bus_dmamap_create: dmamp = %p\n", mapstore));

	map = (bus_dmamap_t)mapstore;
	map->_dm_size = size;
	map->_dm_segcnt = nsegments;
	map->_dm_maxsegsz = maxsegsz;
	map->_dm_boundary = boundary;
	map->_dm_flags = flags & ~(BUS_DMA_WAITOK|BUS_DMA_NOWAIT);

	DMAMAP_RESET(map); /* no valid mappings */

	*dmamp = map;

	return (0);
}

/*
 * Destroy a DMA map.
 */
void
_bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	size_t mapsize;

	DPRINTF(("bus_dmamap_destroy: t = %p, map = %p\n", t, map));

	mapsize = sizeof(struct _bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (map->_dm_segcnt - 1));
	free(map, M_DEVBUF, mapsize);
}

int
_bus_dmamap_load_paddr(bus_dma_tag_t t, bus_dmamap_t map,
    paddr_t paddr, vaddr_t vaddr, bus_size_t size)
{
	bus_dma_segment_t * const segs = map->dm_segs;
	bus_addr_t bmask = ~(map->_dm_boundary - 1);

	int first = map->dm_mapsize == 0;
	int nseg = map->dm_nsegs;
	paddr_t lastaddr = SH3_P2SEG_TO_PHYS(segs[nseg].ds_addr);

	map->dm_mapsize += size;

	do {
		bus_size_t sgsize = size;

		/* Make sure we don't cross any boundaries. */
		if (map->_dm_boundary > 0) {
			bus_addr_t baddr; /* next boundary address */

			baddr = (paddr + map->_dm_boundary) & bmask;
			if (sgsize > (baddr - paddr))
				sgsize = (baddr - paddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * previous segment if possible.
		 */
		if (first) {
			/* first segment */
			segs[nseg].ds_addr = SH3_PHYS_TO_P2SEG(paddr);
			segs[nseg].ds_len = sgsize;
			segs[nseg]._ds_vaddr = vaddr;
			first = 0;
		} else if ((paddr == lastaddr)
		 && (segs[nseg].ds_len + sgsize <= map->_dm_maxsegsz)
		 && (map->_dm_boundary == 0 ||
		     (segs[nseg].ds_addr & bmask) == (paddr & bmask))) {
			/* coalesce */
			segs[nseg].ds_len += sgsize;
		} else {
			if (++nseg >= map->_dm_segcnt)
				return (EFBIG);

			/* new segment */
			segs[nseg].ds_addr = SH3_PHYS_TO_P2SEG(paddr);
			segs[nseg].ds_len = sgsize;
			segs[nseg]._ds_vaddr = vaddr;
		}

		lastaddr = paddr + sgsize;
		paddr += sgsize;
		vaddr += sgsize;
		size -= sgsize;
	} while (size > 0);

	map->dm_nsegs = nseg;

	return (0);
}

int
_bus_dmamap_load_vaddr(bus_dma_tag_t t, bus_dmamap_t map,
    void *buf, bus_size_t size, pmap_t pmap)
{
	vaddr_t vaddr;
	paddr_t paddr;
	vaddr_t next, end;
	int error;

	vaddr = (vaddr_t)buf;
	end = vaddr + size;

	if (pmap == pmap_kernel() &&
	    vaddr >= SH3_P1SEG_BASE && end <= SH3_P2SEG_END)
		paddr = SH3_P1SEG_TO_PHYS(vaddr);
	else {
		for (next = (vaddr + PAGE_SIZE) & ~PAGE_MASK;
		    next < end; next += PAGE_SIZE) {
			pmap_extract(pmap, vaddr, &paddr);
			error = _bus_dmamap_load_paddr(t, map,
			    paddr, vaddr, next - vaddr);
			if (error != 0)
				return (error);

			vaddr = next;
		}

		pmap_extract(pmap, vaddr, &paddr);
		size = end - vaddr;
	}

	return (_bus_dmamap_load_paddr(t, map, paddr, vaddr, size));
}

/*
 * Load a DMA map with a linear buffer.
 */
int
_bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	int error;

	DPRINTF(("bus_dmamap_load: t = %p, map = %p, buf = %p, buflen = %ld, p = %p, flags = %x\n", t, map, buf, buflen, p, flags));

	DMAMAP_RESET(map);

	if (buflen > map->_dm_size)
		return (EINVAL);

	error = _bus_dmamap_load_vaddr(t, map, buf, buflen,
	    p == NULL ? pmap_kernel() : p->p_vmspace->vm_map.pmap);
	if (error != 0) {
		DMAMAP_RESET(map); /* no valid mappings */
		return (error);
	}

	map->dm_nsegs++;

	return (0);
}

/*
 * Like _bus_dmamap_load(), but for mbufs.
 */
int
_bus_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t map, struct mbuf *m0,
    int flags)
{
	struct mbuf *m;
	int error;

	DMAMAP_RESET(map);

#ifdef DIAGNOSTIC
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("_bus_dmamap_load_mbuf: no packet header");
#endif

	if (m0->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

	for (m = m0; m != NULL; m = m->m_next) {
		if (m->m_len == 0)
			continue;

		error = _bus_dmamap_load_vaddr(t, map, m->m_data, m->m_len,
		    pmap_kernel());
		if (error != 0) {
			DMAMAP_RESET(map);
			return (error);
		}
	}

	map->dm_nsegs++;

	return (0);
}

/*
 * Like _bus_dmamap_load(), but for uios.
 */
int
_bus_dmamap_load_uio(bus_dma_tag_t t, bus_dmamap_t map, struct uio *uio,
    int flags)
{

	panic("_bus_dmamap_load_uio: not implemented");
}

/*
 * Like _bus_dmamap_load(), but for raw memory allocated with
 * bus_dmamem_alloc().
 */
int
_bus_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{

	panic("_bus_dmamap_load_raw: not implemented");
}

/*
 * Unload a DMA map.
 */
void
_bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{

	DPRINTF(("bus_dmamap_unload: t = %p, map = %p\n", t, map));

	map->dm_nsegs = 0;
	map->dm_mapsize = 0;
}

/*
 * Synchronize a DMA map.
 */
void
_bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t len, int ops)
{
	bus_size_t minlen;
	bus_addr_t addr, naddr;
	int i;

	DPRINTF(("bus_dmamap_sync: t = %p, map = %p, offset = %ld, len = %ld, ops = %x\n", t, map, offset, len, ops));

#ifdef DIAGNOSTIC
	/*
	 * Mixing PRE and POST operations is not allowed.
	 */
	if ((ops & (BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE)) != 0 &&
	    (ops & (BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE)) != 0)
		panic("_bus_dmamap_sync: mix PRE and POST");

	if (offset >= map->dm_mapsize)
		panic("_bus_dmamap_sync: bad offset");
	if ((offset + len) > map->dm_mapsize)
		panic("_bus_dmamap_sync: bad length");
#endif

	if (!sh_cache_enable_dcache) {
		/* Nothing to do */
		DPRINTF(("bus_dmamap_sync: disabled D-Cache\n"));
		return;
	}

	for (i = 0; i < map->dm_nsegs && len != 0; i++) {
		/* Find the beginning segment. */
		if (offset >= map->dm_segs[i].ds_len) {
			offset -= map->dm_segs[i].ds_len;
			continue;
		}

		/*
		 * Now at the first segment to sync; nail
		 * each segment until we have exhausted the
		 * length.
		 */
		minlen = len < map->dm_segs[i].ds_len - offset ?
		    len : map->dm_segs[i].ds_len - offset;

		addr = map->dm_segs[i]._ds_vaddr;
		naddr = addr + offset;

		if ((naddr >= SH3_P2SEG_BASE)
		 && (naddr + minlen <= SH3_P2SEG_END)) {
			DPRINTF(("bus_dmamap_sync: P2SEG (0x%08lx)\n", naddr));
			offset = 0;
			len -= minlen;
			continue;
		}

		DPRINTF(("bus_dmamap_sync: flushing segment %d "
		    "(0x%lx+%lx, 0x%lx+0x%lx) (remain = %ld)\n",
		    i, addr, offset, addr, offset + minlen - 1, len));

		switch (ops) {
		case BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE:
			if (SH_HAS_WRITEBACK_CACHE)
				sh_dcache_wbinv_range(naddr, minlen);
			else
				sh_dcache_inv_range(naddr, minlen);
			break;

		case BUS_DMASYNC_PREREAD:
			if (SH_HAS_WRITEBACK_CACHE &&
			    ((naddr | minlen) & (sh_cache_line_size - 1)) != 0)
				sh_dcache_wbinv_range(naddr, minlen);
			else
				sh_dcache_inv_range(naddr, minlen);
			break;

		case BUS_DMASYNC_PREWRITE:
			if (SH_HAS_WRITEBACK_CACHE)
				sh_dcache_wb_range(naddr, minlen);
			break;

		case BUS_DMASYNC_POSTREAD:
		case BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE:
			sh_dcache_inv_range(naddr, minlen);
			break;
		}
		offset = 0;
		len -= minlen;
	}
}

/*
 * Allocate memory safe for DMA.
 */
int
_bus_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags)
{
	struct pglist mlist;
	paddr_t curaddr, lastaddr;
	struct vm_page *m;
	int curseg, error, plaflag;

	DPRINTF(("bus_dmamem_alloc: t = %p, size = %ld, alignment = %ld, boundary = %ld, segs = %p, nsegs = %d, rsegs = %p, flags = %x\n", t, size, alignment, boundary, segs, nsegs, rsegs, flags));

	/* Always round the size. */
	size = round_page(size);

	/*
	 * Allocate the pages from the VM system.
	 */
	plaflag = flags & BUS_DMA_NOWAIT ? UVM_PLA_NOWAIT : UVM_PLA_WAITOK;
	if (flags & BUS_DMA_ZERO)
		plaflag |= UVM_PLA_ZERO;

	TAILQ_INIT(&mlist);
	error = uvm_pglistalloc(size, 0, -1, alignment, boundary,
	    &mlist, nsegs, plaflag);
	if (error)
		return (error);

	/*
	 * Compute the location, size, and number of segments actually
	 * returned by the VM code.
	 */
	m = mlist.tqh_first;
	curseg = 0;
	lastaddr = segs[curseg].ds_addr = VM_PAGE_TO_PHYS(m);
	segs[curseg].ds_len = PAGE_SIZE;

	DPRINTF(("bus_dmamem_alloc: m = %p, lastaddr = 0x%08lx\n",m,lastaddr));

	while ((m = TAILQ_NEXT(m, pageq)) != NULL) {
		curaddr = VM_PAGE_TO_PHYS(m);
		DPRINTF(("bus_dmamem_alloc: m = %p, curaddr = 0x%08lx, lastaddr = 0x%08lx\n", m, curaddr, lastaddr));
		if (curaddr == (lastaddr + PAGE_SIZE)) {
			segs[curseg].ds_len += PAGE_SIZE;
		} else {
			DPRINTF(("bus_dmamem_alloc: new segment\n"));
			curseg++;
			segs[curseg].ds_addr = curaddr;
			segs[curseg].ds_len = PAGE_SIZE;
		}
		lastaddr = curaddr;
	}

	*rsegs = curseg + 1;

	DPRINTF(("bus_dmamem_alloc: curseg = %d, *rsegs = %d\n",curseg,*rsegs));

	return (0);
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

	DPRINTF(("bus_dmamem_free: t = %p, segs = %p, nsegs = %d\n", t, segs, nsegs));

	/*
	 * Build a list of pages to free back to the VM system.
	 */
	TAILQ_INIT(&mlist);
	for (curseg = 0; curseg < nsegs; curseg++) {
		DPRINTF(("bus_dmamem_free: segs[%d]: ds_addr = 0x%08lx, ds_len = %ld\n", curseg, segs[curseg].ds_addr, segs[curseg].ds_len));
		for (addr = segs[curseg].ds_addr;
		    addr < (segs[curseg].ds_addr + segs[curseg].ds_len);
		    addr += PAGE_SIZE) {
			m = PHYS_TO_VM_PAGE(addr);
			DPRINTF(("bus_dmamem_free: m = %p\n", m));
			TAILQ_INSERT_TAIL(&mlist, m, pageq);
		}
	}

	uvm_pglistfree(&mlist);
}


int
_bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    size_t size, caddr_t *kvap, int flags)
{
	vaddr_t va;
	bus_addr_t addr;
	int curseg;
	const struct kmem_dyn_mode *kd;

	DPRINTF(("bus_dmamem_map: t = %p, segs = %p, nsegs = %d, size = %d, kvap = %p, flags = %x\n", t, segs, nsegs, size, kvap, flags));

        /*
	 * If we're only mapping 1 segment, use P2SEG, to avoid
	 * TLB thrashing.
	 */
	if (nsegs == 1) {
		if (flags & BUS_DMA_COHERENT) {
			*kvap = (caddr_t)SH3_PHYS_TO_P2SEG(segs[0].ds_addr);
		} else {
			*kvap = (caddr_t)SH3_PHYS_TO_P1SEG(segs[0].ds_addr);
		}
		DPRINTF(("bus_dmamem_map: addr = 0x%08lx, kva = %p\n", segs[0].ds_addr, *kvap));
		return 0;
	}

	/* Always round the size. */
	size = round_page(size);
	kd = flags & BUS_DMA_NOWAIT ? &kd_trylock : &kd_waitok;
	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, kd);
	if (va == 0)
		return (ENOMEM);

	*kvap = (caddr_t)va;
	for (curseg = 0; curseg < nsegs; curseg++) {
		DPRINTF(("bus_dmamem_map: segs[%d]: ds_addr = 0x%08lx, ds_len = %ld\n", curseg, segs[curseg].ds_addr, segs[curseg].ds_len));
		for (addr = segs[curseg].ds_addr;
		     addr < segs[curseg].ds_addr + segs[curseg].ds_len;
		     addr += PAGE_SIZE, va += PAGE_SIZE, size -= PAGE_SIZE) {
			if (size == 0)
				panic("_bus_dmamem_map: size botch");
			pmap_kenter_pa(va, addr,
			    PROT_READ | PROT_WRITE);
		}
	}
	pmap_update(pmap_kernel());

	return (0);
}

void
_bus_dmamem_unmap(bus_dma_tag_t t, caddr_t kva, size_t size)
{

	DPRINTF(("bus_dmamem_unmap: t = %p, kva = %p, size = %d\n", t, kva, size));

#ifdef DIAGNOSTIC
	if ((u_long)kva & PAGE_MASK)
		panic("_bus_dmamem_unmap");
#endif

        /*
	 * Nothing to do if we mapped it with P[12]SEG.
	 */
	if ((kva >= (caddr_t)SH3_P1SEG_BASE)
	 && (kva <= (caddr_t)SH3_P2SEG_END)) {
		return;
	}

	size = round_page(size);
	pmap_kremove((vaddr_t)kva, size);
	pmap_update(pmap_kernel());
	km_free(kva, size, &kv_any, &kp_none);
}

paddr_t
_bus_dmamem_mmap(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    off_t off, int prot, int flags)
{

	/* Not implemented. */
	return (paddr_t)(-1);
}
