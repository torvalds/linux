/* $OpenBSD: sgmap_typedep.c,v 1.15 2017/04/30 16:45:45 mpi Exp $ */
/* $NetBSD: sgmap_typedep.c,v 1.17 2001/07/19 04:27:37 thorpej Exp $ */

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
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

#ifdef SGMAP_DEBUG
int			__C(SGMAP_TYPE,_debug) = 0;
#endif

SGMAP_PTE_TYPE		__C(SGMAP_TYPE,_prefetch_spill_page_pte);

int			__C(SGMAP_TYPE,_load_buffer)(bus_dma_tag_t,
			    bus_dmamap_t, void *buf, size_t buflen,
			    struct proc *, int, int, struct alpha_sgmap *);

void
__C(SGMAP_TYPE,_init_spill_page_pte)(void)
{

	__C(SGMAP_TYPE,_prefetch_spill_page_pte) =
	    (alpha_sgmap_prefetch_spill_page_pa >>
	     SGPTE_PGADDR_SHIFT) | SGPTE_VALID;
}

int
__C(SGMAP_TYPE,_load_buffer)(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    size_t buflen, struct proc *p, int flags, int seg,
    struct alpha_sgmap *sgmap)
{
	vaddr_t endva, va = (vaddr_t)buf;
	paddr_t pa;
	pmap_t pmap;
	bus_addr_t dmaoffset, sgva;
	bus_size_t sgvalen, boundary, alignment;
	struct extent_region *regions = map->_dm_cookie;
	SGMAP_PTE_TYPE *pte, *page_table = sgmap->aps_pt;
	int pteidx, error, spill;

	if (seg >= map->_dm_segcnt)
		return (EFBIG);

	/* Initialize the spill page PTE if it hasn't been already. */
	if (__C(SGMAP_TYPE,_prefetch_spill_page_pte) == 0)
		__C(SGMAP_TYPE,_init_spill_page_pte)();

	/*
	 * Remember the offset into the first page and the total
	 * transfer length.
	 */
	dmaoffset = ((u_long)buf) & PGOFSET;

#ifdef SGMAP_DEBUG
	if (__C(SGMAP_TYPE,_debug)) {
		printf("sgmap_load: ----- buf = %p -----\n", buf);
		printf("sgmap_load: dmaoffset = 0x%lx, buflen = 0x%lx\n",
		    dmaoffset, buflen);
	}
#endif
	if (p != NULL)
		pmap = p->p_vmspace->vm_map.pmap;
	else
		pmap = pmap_kernel();

	/*
	 * Allocate the necessary virtual address space for the
	 * mapping.  Round the size, since we deal with whole pages.
	 */

	/*
	 * XXX Always allocate a spill page for now.  Note
	 * the spill page is not needed for an in-bound-only
	 * transfer.
	 */
	if ((flags & BUS_DMA_READ) == 0)
		spill = 1;
	else
		spill = 0;

	endva = round_page(va + buflen);
	va = trunc_page(va);

	boundary = map->_dm_boundary;
	alignment = PAGE_SIZE;

	sgvalen = (endva - va);
	if (spill) {
		sgvalen += PAGE_SIZE;

		/*
		 * ARGH!  If the addition of the spill page bumped us
		 * over our boundary, we have to 2x the boundary limit.
		 */
		if (boundary && boundary < sgvalen) {
			alignment = boundary;
			do {
				boundary <<= 1;
			} while (boundary < sgvalen);
		}
	}

#if 0
	printf("len 0x%lx -> 0x%lx, boundary 0x%lx -> 0x%lx -> ",
	    (endva - va), sgvalen, map->_dm_boundary, boundary);
#endif

	mtx_enter(&sgmap->aps_mtx);
	error = extent_alloc_with_descr(sgmap->aps_ex, sgvalen, alignment,
	    0, boundary, EX_NOWAIT, &regions[seg], &sgva);
	mtx_leave(&sgmap->aps_mtx);
	if (error)
		return (error);

#if 0
	printf("error %d sgva 0x%lx\n", error, sgva);
#endif

	pteidx = sgva >> SGMAP_ADDR_PTEIDX_SHIFT;
	pte = &page_table[pteidx * SGMAP_PTE_SPACING];

#ifdef SGMAP_DEBUG
	if (__C(SGMAP_TYPE,_debug))
		printf("sgmap_load: sgva = 0x%lx, pteidx = %d, "
		    "pte = %p (pt = %p)\n", sgva, pteidx, pte,
		    page_table);
#endif

	/* Generate the DMA address. */
	map->dm_segs[seg].ds_addr = sgmap->aps_wbase | sgva | dmaoffset;
	map->dm_segs[seg].ds_len = buflen;

#ifdef SGMAP_DEBUG
	if (__C(SGMAP_TYPE,_debug))
		printf("sgmap_load: wbase = 0x%lx, vpage = 0x%x, "
		    "dma addr = 0x%lx\n", sgmap->aps_wbase, sgva,
		    map->dm_segs[seg].ds_addr);
#endif

	for (; va < endva; va += PAGE_SIZE, pteidx++,
	     pte = &page_table[pteidx * SGMAP_PTE_SPACING]) {
		/* Get the physical address for this segment. */
		(void)pmap_extract(pmap, va, &pa);

		/* Load the current PTE with this page. */
		*pte = (pa >> SGPTE_PGADDR_SHIFT) | SGPTE_VALID;
#ifdef SGMAP_DEBUG
		if (__C(SGMAP_TYPE,_debug))
			printf("sgmap_load:     pa = 0x%lx, pte = %p, "
			    "*pte = 0x%lx\n", pa, pte, (u_long)(*pte));
#endif
	}

	if (spill) {
		/* ...and the prefetch-spill page. */
		*pte = __C(SGMAP_TYPE,_prefetch_spill_page_pte);
#ifdef SGMAP_DEBUG
		if (__C(SGMAP_TYPE,_debug)) {
			printf("sgmap_load:     spill page, pte = %p, "
			    "*pte = 0x%lx\n", pte, *pte);
			printf("sgmap_load:     pte count = %d\n",
			    map->_dm_ptecnt);
		}
#endif
	}

	return (0);
}

int
__C(SGMAP_TYPE,_load)(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags, struct alpha_sgmap *sgmap)
{
	int seg, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	if (buflen > map->_dm_size)
		return (EINVAL);

	KASSERT((map->_dm_flags & (BUS_DMA_READ|BUS_DMA_WRITE)) == 0);
	KASSERT((flags & (BUS_DMA_READ|BUS_DMA_WRITE)) !=
	    (BUS_DMA_READ|BUS_DMA_WRITE));

	map->_dm_flags |= flags & (BUS_DMA_READ|BUS_DMA_WRITE);

	seg = 0;
	error = __C(SGMAP_TYPE,_load_buffer)(t, map, buf, buflen, p,
	    flags, seg, sgmap);

	alpha_mb();

#if defined(SGMAP_DEBUG) && defined(DDB)
	if (__C(SGMAP_TYPE,_debug) > 1)
		db_enter();
#endif

	if (error == 0) {
		map->dm_mapsize = buflen;
		map->dm_nsegs = 1;
		map->_dm_window = t;
	} else {
		map->_dm_flags &= ~(BUS_DMA_READ|BUS_DMA_WRITE);
		if (t->_next_window != NULL) {
			/* Give the next window a chance. */
			error = bus_dmamap_load(t->_next_window, map, buf,
			    buflen, p, flags);
		}
	}
	return (error);
}

int
__C(SGMAP_TYPE,_load_mbuf)(bus_dma_tag_t t, bus_dmamap_t map,
    struct mbuf *m0, int flags, struct alpha_sgmap *sgmap)
{
	struct mbuf *m;
	int seg, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

#ifdef DIAGNOSTIC
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic(__S(__C(SGMAP_TYPE,_load_mbuf)) ": no packet header");
#endif

	if (m0->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

	KASSERT((map->_dm_flags & (BUS_DMA_READ|BUS_DMA_WRITE)) == 0);
	KASSERT((flags & (BUS_DMA_READ|BUS_DMA_WRITE)) !=
	    (BUS_DMA_READ|BUS_DMA_WRITE));

	map->_dm_flags |= flags & (BUS_DMA_READ|BUS_DMA_WRITE);

	seg = 0;
	error = 0;
	for (m = m0; m != NULL && error == 0; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		error = __C(SGMAP_TYPE,_load_buffer)(t, map,
		    m->m_data, m->m_len, NULL, flags, seg, sgmap);
		seg++;
	}

	alpha_mb();

#if defined(SGMAP_DEBUG) && defined(DDB)
	if (__C(SGMAP_TYPE,_debug) > 1)
		db_enter();
#endif

	if (error == 0) {
		map->dm_mapsize = m0->m_pkthdr.len;
		map->dm_nsegs = seg;
		map->_dm_window = t;
	} else {
		/* Need to back out what we've done so far. */
		map->dm_nsegs = seg - 1;
		__C(SGMAP_TYPE,_unload)(t, map, sgmap);
		map->_dm_flags &= ~(BUS_DMA_READ|BUS_DMA_WRITE);
		if (t->_next_window != NULL) {
			/* Give the next window a chance. */
			error = bus_dmamap_load_mbuf(t->_next_window, map,
			    m0, flags);
		}
	}

	return (error);
}

int
__C(SGMAP_TYPE,_load_uio)(bus_dma_tag_t t, bus_dmamap_t map, struct uio *uio,
    int flags, struct alpha_sgmap *sgmap)
{
	bus_size_t minlen, resid;
	struct proc *p = NULL;
	struct iovec *iov;
	caddr_t addr;
	int i, seg, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	KASSERT((map->_dm_flags & (BUS_DMA_READ|BUS_DMA_WRITE)) == 0);
	KASSERT((flags & (BUS_DMA_READ|BUS_DMA_WRITE)) !=
 	    (BUS_DMA_READ|BUS_DMA_WRITE));

	map->_dm_flags |= flags & (BUS_DMA_READ|BUS_DMA_WRITE);

	resid = uio->uio_resid;
	iov = uio->uio_iov;

	if (uio->uio_segflg == UIO_USERSPACE) {
		p = uio->uio_procp;
#ifdef DIAGNOSTIC
		if (p == NULL)
			panic(__S(__C(SGMAP_TYPE,_load_uio))
			    ": USERSPACE but no proc");
#endif
	}

	seg = 0;
	error = 0;
	for (i = 0; i < uio->uio_iovcnt && resid != 0 && error == 0;
	     i++, seg++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		minlen = resid < iov[i].iov_len ? resid : iov[i].iov_len;
		addr = (caddr_t)iov[i].iov_base;

		error = __C(SGMAP_TYPE,_load_buffer)(t, map,
		    addr, minlen, p, flags, seg, sgmap);

		resid -= minlen;
	}

	alpha_mb();

#if defined(SGMAP_DEBUG) && defined(DDB)
	if (__C(SGMAP_TYPE,_debug) > 1)
		db_enter();
#endif

	if (error == 0) {
		map->dm_mapsize = uio->uio_resid;
		map->dm_nsegs = seg;
	} else {
		/* Need to back out what we've done so far. */
		map->dm_nsegs = seg - 1;
		__C(SGMAP_TYPE,_unload)(t, map, sgmap);
		map->_dm_flags &= ~(BUS_DMA_READ|BUS_DMA_WRITE);
		if (t->_next_window != NULL) {
			/* Give the next window a chance. */
			error = bus_dmamap_load_uio(t->_next_window, map,
			    uio, flags);
		}
	}

	return (error);
}

int
__C(SGMAP_TYPE,_load_raw)(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags,
    struct alpha_sgmap *sgmap)
{
	KASSERT((map->_dm_flags & (BUS_DMA_READ|BUS_DMA_WRITE)) == 0);
	KASSERT((flags & (BUS_DMA_READ|BUS_DMA_WRITE)) !=
	    (BUS_DMA_READ|BUS_DMA_WRITE));

	panic(__S(__C(SGMAP_TYPE,_load_raw)) ": not implemented");
}

void
__C(SGMAP_TYPE,_unload)(bus_dma_tag_t t, bus_dmamap_t map,
    struct alpha_sgmap *sgmap)
{
	SGMAP_PTE_TYPE *pte, *page_table = sgmap->aps_pt;
	bus_addr_t osgva, sgva, esgva;
	int error, spill, seg, pteidx;

	for (seg = 0; seg < map->dm_nsegs; seg++) {
		/*
		 * XXX Always allocate a spill page for now.  Note
		 * the spill page is not needed for an in-bound-only
		 * transfer.
		 */
		if ((map->_dm_flags & BUS_DMA_READ) == 0)
			spill = 1;
		else
			spill = 0;

		sgva = map->dm_segs[seg].ds_addr & ~sgmap->aps_wbase;

		esgva = round_page(sgva + map->dm_segs[seg].ds_len);
		osgva = sgva = trunc_page(sgva);

		if (spill)
			esgva += PAGE_SIZE;

		/* Invalidate the PTEs for the mapping. */
		for (pteidx = sgva >> SGMAP_ADDR_PTEIDX_SHIFT;
		     sgva < esgva; sgva += PAGE_SIZE, pteidx++) {
			pte = &page_table[pteidx * SGMAP_PTE_SPACING];
#ifdef SGMAP_DEBUG
			if (__C(SGMAP_TYPE,_debug))
				printf("sgmap_unload:     pte = %p, "
				    "*pte = 0x%lx\n", pte, (u_long)(*pte));
#endif
			*pte = 0;
		}

		alpha_mb();

		/* Free the virtual address space used by the mapping. */
		mtx_enter(&sgmap->aps_mtx);
		error = extent_free(sgmap->aps_ex, osgva, (esgva - osgva),
		    EX_NOWAIT);
		mtx_leave(&sgmap->aps_mtx);
		if (error != 0)
			panic(__S(__C(SGMAP_TYPE,_unload)));
	}

	map->_dm_flags &= ~(BUS_DMA_READ|BUS_DMA_WRITE);

	/* Mark the mapping invalid. */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;
	map->_dm_window = NULL;
}
