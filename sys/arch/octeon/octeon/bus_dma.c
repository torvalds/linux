/*	$OpenBSD: bus_dma.c,v 1.17 2019/12/20 13:34:41 visa Exp $ */

/*
 * Copyright (c) 2003-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#include <uvm/uvm_extern.h>

#include <mips64/cache.h>
#include <machine/cpu.h>
#include <machine/autoconf.h>

#include <machine/bus.h>

/*
 * Common function for DMA map creation.  May be called by bus-specific
 * DMA map creation functions.
 */
int
_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	struct machine_bus_dmamap *map;
	void *mapstore;
	size_t mapsize;

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
	mapsize = sizeof(struct machine_bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (nsegments - 1));
	if ((mapstore = malloc(mapsize, M_DEVBUF, (flags & BUS_DMA_NOWAIT) ?
	    (M_NOWAIT | M_ZERO) : (M_WAITOK | M_ZERO))) == NULL)
		return (ENOMEM);

	map = (struct machine_bus_dmamap *)mapstore;
	map->_dm_size = size;
	map->_dm_segcnt = nsegments;
	map->_dm_maxsegsz = maxsegsz;
	map->_dm_boundary = boundary;
	map->_dm_flags = flags & ~(BUS_DMA_WAITOK|BUS_DMA_NOWAIT);

	*dmamp = map;
	return (0);
}

/*
 * Common function for DMA map destruction.  May be called by bus-specific
 * DMA map destruction functions.
 */
void
_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	size_t mapsize;

	mapsize = sizeof(struct machine_bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (map->_dm_segcnt - 1));
	free(map, M_DEVBUF, mapsize);
}

/*
 * Common function for loading a DMA map with a linear buffer.  May
 * be called by bus-specific DMA map load functions.
 */
int
_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *buf, bus_size_t buflen,
    struct proc *p, int flags)
{
	paddr_t lastaddr;
	int seg, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_nsegs = 0;
	map->dm_mapsize = 0;

	if (buflen > map->_dm_size)
		return (EINVAL);

	seg = 0;
	error = (*t->_dmamap_load_buffer)(t, map, buf, buflen, p, flags,
	    &lastaddr, &seg, 1);
	if (error == 0) {
		map->dm_nsegs = seg + 1;
		map->dm_mapsize = buflen;
	}

	return (error);
}

/*
 * Like _bus_dmamap_load(), but for mbufs.
 */
int
_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t map, struct mbuf *m0, int flags)
{
	paddr_t lastaddr;
	int seg, error, first;
	struct mbuf *m;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_nsegs = 0;
	map->dm_mapsize = 0;

#ifdef DIAGNOSTIC
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("_dmamap_load_mbuf: no packet header");
#endif

	if (m0->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

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
		map->dm_nsegs = seg + 1;
		map->dm_mapsize = m0->m_pkthdr.len;
	}

	return (error);
}

/*
 * Like _dmamap_load(), but for uios.
 */
int
_dmamap_load_uio(bus_dma_tag_t t, bus_dmamap_t map, struct uio *uio, int flags)
{
	paddr_t lastaddr;
	int seg, i, error, first;
	bus_size_t minlen, resid;
	struct proc *p = NULL;
	struct iovec *iov;
	void *addr;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_nsegs = 0;
	map->dm_mapsize = 0;

	resid = uio->uio_resid;
	iov = uio->uio_iov;

	if (uio->uio_segflg == UIO_USERSPACE) {
		p = uio->uio_procp;
#ifdef DIAGNOSTIC
		if (p == NULL)
			panic("_dmamap_load_uio: USERSPACE but no proc");
#endif
	}

	first = 1;
	seg = 0;
	error = 0;
	for (i = 0; i < uio->uio_iovcnt && resid != 0 && error == 0; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		minlen = resid < iov[i].iov_len ? resid : iov[i].iov_len;
		addr = (void *)iov[i].iov_base;

		error = (*t->_dmamap_load_buffer)(t, map, addr, minlen,
		    p, flags, &lastaddr, &seg, first);
		first = 0;

		resid -= minlen;
	}
	if (error == 0) {
		map->dm_nsegs = seg + 1;
		map->dm_mapsize = uio->uio_resid;
	}

	return (error);
}

/*
 * Like _dmamap_load(), but for raw memory allocated with
 * bus_dmamem_alloc().
 */
int
_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map, bus_dma_segment_t *segs,
    int nsegs, bus_size_t size, int flags)
{
	bus_addr_t paddr, baddr, bmask, lastaddr = 0;
	bus_size_t plen, sgsize, mapsize;
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

	for (i = 0; i < nsegs && size > 0; i++) {
		paddr = segs[i].ds_addr;
		plen = MIN(segs[i].ds_len, size);

		while (plen > 0) {
			/*
			 * Compute the segment size, and adjust counts.
			 */
			sgsize = PAGE_SIZE - ((u_long)paddr & PGOFSET);
			if (plen < sgsize)
				sgsize = plen;

			if (paddr > dma_constraint.ucr_high)
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
_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	/*
	 * No resources to free; just mark the mappings as
	 * invalid.
	 */
	map->dm_nsegs = 0;
	map->dm_mapsize = 0;
}

/*
 * Common function for DMA map synchronization.  May be called
 * by bus-specific DMA map synchronization functions.
 */
void
_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t addr,
    bus_size_t size, int op)
{
	int nsegs;
	int curseg;
	struct cpu_info *ci = curcpu();

	nsegs = map->dm_nsegs;
	curseg = 0;

	while (size && nsegs) {
		paddr_t paddr;
		vaddr_t vaddr;
		bus_size_t ssize;

		ssize = map->dm_segs[curseg].ds_len;
		paddr = map->dm_segs[curseg]._ds_paddr;
		vaddr = map->dm_segs[curseg]._ds_vaddr;

		if (addr != 0) {
			if (addr >= ssize) {
				addr -= ssize;
				ssize = 0;
			} else {
				vaddr += addr;
				paddr += addr;
				ssize -= addr;
				addr = 0;
			}
		}
		if (ssize > size)
			ssize = size;

		if (IS_XKPHYS(vaddr) && XKPHYS_TO_CCA(vaddr) == CCA_NC) {
			size -= ssize;
			ssize = 0;
		}

		if (ssize != 0) {
			/*
			 * If only PREWRITE is requested, writeback.
			 * PREWRITE with PREREAD writebacks
			 * and invalidates (if noncoherent) *all* cache levels.
			 * Otherwise, just invalidate (if noncoherent).
			 */
			if (op & BUS_DMASYNC_PREWRITE) {
				if (op & BUS_DMASYNC_PREREAD)
					Mips_IOSyncDCache(ci, vaddr,
					    ssize, CACHE_SYNC_X);
				else
					Mips_IOSyncDCache(ci, vaddr,
					    ssize, CACHE_SYNC_W);
			} else
			if (op & (BUS_DMASYNC_PREREAD | BUS_DMASYNC_POSTREAD)) {
				Mips_IOSyncDCache(ci, vaddr,
				    ssize, CACHE_SYNC_R);
			}
			size -= ssize;
		}
		curseg++;
		nsegs--;
	}

	if (size != 0) {
		panic("_dmamap_sync: ran off map!");
	}
}

/*
 * Common function for DMA-safe memory allocation.  May be called
 * by bus-specific DMA memory allocation functions.
 */
int
_dmamem_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags)
{
	return _dmamem_alloc_range(t, size, alignment, boundary,
	    segs, nsegs, rsegs, flags, (paddr_t)0, (paddr_t)-1);
}

/*
 * Common function for freeing DMA-safe memory.  May be called by
 * bus-specific DMA memory free functions.
 */
void
_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs)
{
	vm_page_t m;
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
			m = PHYS_TO_VM_PAGE((*t->_device_to_pa)(addr));
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
_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs, size_t size,
    caddr_t *kvap, int flags)
{
	vaddr_t va, sva;
	size_t ssize;
	paddr_t pa;
	bus_addr_t addr;
	int curseg, error, pmap_flags;
	const struct kmem_dyn_mode *kd;

	if (nsegs == 1) {
		pa = (*t->_device_to_pa)(segs[0].ds_addr);
		if (flags & (BUS_DMA_COHERENT | BUS_DMA_NOCACHE))
			*kvap = (caddr_t)PHYS_TO_XKPHYS(pa, CCA_NC);
		else
			*kvap = (caddr_t)PHYS_TO_XKPHYS(pa, CCA_CACHED);
		return (0);
	}

	size = round_page(size);
	kd = flags & BUS_DMA_NOWAIT ? &kd_trylock : &kd_waitok;
	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, kd);
	if (va == 0)
		return (ENOMEM);

	*kvap = (caddr_t)va;

	sva = va;
	ssize = size;
	pmap_flags = PMAP_WIRED | PMAP_CANFAIL;
	if (flags & (BUS_DMA_COHERENT | BUS_DMA_NOCACHE))
		pmap_flags |= PMAP_NOCACHE;
	for (curseg = 0; curseg < nsegs; curseg++) {
		for (addr = segs[curseg].ds_addr;
		    addr < (segs[curseg].ds_addr + segs[curseg].ds_len);
		    addr += NBPG, va += NBPG, size -= NBPG) {
			if (size == 0)
				panic("_dmamem_map: size botch");
			pa = (*t->_device_to_pa)(addr);
			error = pmap_enter(pmap_kernel(), va, pa,
			    PROT_READ | PROT_WRITE,
			    PROT_READ | PROT_WRITE | pmap_flags);
			if (error) {
				pmap_update(pmap_kernel());
				km_free((void *)sva, ssize, &kv_any, &kp_none);
				return (error);
			}

			/*
			 * This is redundant with what pmap_enter() did
			 * above, but will take care of forcing other
			 * mappings of the same page (if any) to be
			 * uncached.
			 * If there are no multiple mappings of that
			 * page, this amounts to a noop.
			 */
			if (flags & (BUS_DMA_COHERENT | BUS_DMA_NOCACHE))
				pmap_page_cache(PHYS_TO_VM_PAGE(pa),
				    PGF_UNCACHED);
		}
		pmap_update(pmap_kernel());
	}

	return (0);
}

/*
 * Common function for unmapping DMA-safe memory.  May be called by
 * bus-specific DMA memory unmapping functions.
 */
void
_dmamem_unmap(bus_dma_tag_t t, caddr_t kva, size_t size)
{
	if (IS_XKPHYS((vaddr_t)kva))
		return;

	km_free(kva, round_page(size), &kv_any, &kp_none);
}

/*
 * Common function for mmap(2)'ing DMA-safe memory.  May be called by
 * bus-specific DMA mmap(2)'ing functions.
 */
paddr_t
_dmamem_mmap(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs, off_t off,
    int prot, int flags)
{
	int i;

	for (i = 0; i < nsegs; i++) {
#ifdef DIAGNOSTIC
		if (off & PGOFSET)
			panic("_dmamem_mmap: offset unaligned");
		if (segs[i].ds_addr & PGOFSET)
			panic("_dmamem_mmap: segment unaligned");
		if (segs[i].ds_len & PGOFSET)
			panic("_dmamem_mmap: segment size not multiple"
			    " of page size");
#endif
		if (off >= segs[i].ds_len) {
			off -= segs[i].ds_len;
			continue;
		}

		return ((*t->_device_to_pa)(segs[i].ds_addr) + off);
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
_dmamap_load_buffer(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags, paddr_t *lastaddrp,
    int *segp, int first)
{
	bus_size_t sgsize;
	bus_addr_t lastaddr, baddr, bmask;
	paddr_t curaddr;
	vaddr_t vaddr = (vaddr_t)buf;
	int seg;
	pmap_t pmap;

	if (p != NULL)
		pmap = p->p_vmspace->vm_map.pmap;
	else
		pmap = pmap_kernel();

	lastaddr = *lastaddrp;
	bmask  = ~(map->_dm_boundary - 1);
	if (t->_dma_mask != 0)
		bmask &= t->_dma_mask;

	for (seg = *segp; buflen > 0; ) {
		/*
		 * Get the physical address for this segment.
		 */
		if (pmap_extract(pmap, vaddr, &curaddr) == 0)
			panic("_dmapmap_load_buffer: pmap_extract(%p, %lx) failed!",
			    pmap, vaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = NBPG - ((u_long)vaddr & PGOFSET);
		if (buflen < sgsize)
			sgsize = buflen;

		/*
		 * Make sure we don't cross any boundaries.
		 */
		if (map->_dm_boundary > 0) {
			baddr = ((bus_addr_t)curaddr + map->_dm_boundary) &
			    bmask;
			if (sgsize > (baddr - (bus_addr_t)curaddr))
				sgsize = (baddr - (bus_addr_t)curaddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * previous segment if possible.
		 */
		if (first) {
			map->dm_segs[seg].ds_addr =
			    (*t->_pa_to_device)(curaddr);
			map->dm_segs[seg].ds_len = sgsize;
			map->dm_segs[seg]._ds_paddr = curaddr;
			map->dm_segs[seg]._ds_vaddr = vaddr;
			first = 0;
		} else {
			if ((bus_addr_t)curaddr == lastaddr &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->_dm_maxsegsz &&
			     (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     ((bus_addr_t)curaddr & bmask)))
				map->dm_segs[seg].ds_len += sgsize;
			else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr =
				    (*t->_pa_to_device)(curaddr);
				map->dm_segs[seg].ds_len = sgsize;
				map->dm_segs[seg]._ds_paddr = curaddr;
				map->dm_segs[seg]._ds_vaddr = vaddr;
			}
		}

		lastaddr = (bus_addr_t)curaddr + sgsize;
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
_dmamem_alloc_range(bus_dma_tag_t t, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags, paddr_t low, paddr_t high)
{
	paddr_t curaddr, lastaddr;
	vm_page_t m;
	struct pglist mlist;
	int curseg, error, plaflag;

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
	lastaddr = segs[curseg].ds_addr =
	    (*t->_pa_to_device)(VM_PAGE_TO_PHYS(m));
	segs[curseg].ds_len = PAGE_SIZE;
	m = TAILQ_NEXT(m, pageq);

	for (; m != NULL; m = TAILQ_NEXT(m, pageq)) {
		curaddr = VM_PAGE_TO_PHYS(m);
#ifdef DIAGNOSTIC
		if (curaddr < low || curaddr >= high) {
			printf("vm_page_alloc_memory returned non-sensical"
			    " address 0x%lx\n", curaddr);
			panic("_dmamem_alloc_range");
		}
#endif
		curaddr = (*t->_pa_to_device)(curaddr);
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
