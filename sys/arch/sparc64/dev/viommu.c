/*	$OpenBSD: viommu.c,v 1.22 2025/07/16 07:15:42 jsg Exp $	*/
/*	$NetBSD: iommu.c,v 1.47 2002/02/08 20:03:45 eeh Exp $	*/

/*
 * Copyright (c) 2008 Mark Kettenis
 * Copyright (c) 2003 Henric Jungheim
 * Copyright (c) 2001, 2002 Eduardo Horvath
 * Copyright (c) 1999, 2000 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * UltraSPARC Hypervisor IOMMU support.
 */

#include <sys/param.h>
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/mbuf.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <sparc64/sparc64/cache.h>
#include <sparc64/dev/iommuvar.h>
#include <sparc64/dev/viommuvar.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/hypervisor.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#endif

#ifdef DEBUG
#define IDB_BUSDMA	0x1
#define IDB_IOMMU	0x2
#define IDB_INFO	0x4
#define IDB_SYNC	0x8
#define IDB_XXX		0x10
#define IDB_PRINT_MAP	0x20
#define IDB_BREAK	0x40
extern int iommudebug;
#define DPRINTF(l, s)   do { if (iommudebug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

void viommu_enter(struct iommu_state *, struct strbuf_ctl *, bus_addr_t,
    paddr_t, int);
void viommu_remove(struct iommu_state *, struct strbuf_ctl *, bus_addr_t);
int viommu_dvmamap_load_seg(bus_dma_tag_t, struct iommu_state *,
    bus_dmamap_t, bus_dma_segment_t *, int, int, bus_size_t, bus_size_t);
int viommu_dvmamap_load_mlist(bus_dma_tag_t, struct iommu_state *,
    bus_dmamap_t, struct pglist *, int, bus_size_t, bus_size_t);
int viommu_dvmamap_append_range(bus_dma_tag_t, bus_dmamap_t, paddr_t,
    bus_size_t, int, bus_size_t);
int iommu_iomap_insert_page(struct iommu_map_state *, paddr_t);
bus_addr_t iommu_iomap_translate(struct iommu_map_state *, paddr_t);
void viommu_iomap_load_map(struct iommu_state *, struct iommu_map_state *,
    bus_addr_t, int);
void viommu_iomap_unload_map(struct iommu_state *, struct iommu_map_state *);
struct iommu_map_state *viommu_iomap_create(int);
void iommu_iomap_destroy(struct iommu_map_state *);
void iommu_iomap_clear_pages(struct iommu_map_state *);
void _viommu_dvmamap_sync(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
    bus_addr_t, bus_size_t, int);

/*
 * initialise the UltraSPARC IOMMU (Hypervisor):
 *	- allocate and setup the iotsb.
 *	- enable the IOMMU
 *	- create a private DVMA map.
 */
void
viommu_init(char *name, struct iommu_state *is, int tsbsize,
    u_int32_t iovabase)
{
	struct vm_page *m;
	struct pglist mlist;

	/*
	 * Setup the iommu.
	 *
	 * The sun4v iommu is accessed through the hypervisor so we will
	 * deal with it here..
	 */
	is->is_tsbsize = tsbsize;
	if (iovabase == (u_int32_t)-1) {
		is->is_dvmabase = IOTSB_VSTART(is->is_tsbsize);
		is->is_dvmaend = IOTSB_VEND;
	} else {
		is->is_dvmabase = iovabase;
		is->is_dvmaend = iovabase + IOTSB_VSIZE(tsbsize) - 1;
	}

	TAILQ_INIT(&mlist);
	if (uvm_pglistalloc(PAGE_SIZE, 0, -1, PAGE_SIZE, 0, &mlist, 1,
	    UVM_PLA_NOWAIT | UVM_PLA_ZERO) != 0)
		panic("%s: no memory", __func__);
	m = TAILQ_FIRST(&mlist);
	is->is_scratch = VM_PAGE_TO_PHYS(m);

	/*
	 * Allocate a dvma map.
	 */
	printf("dvma map %x-%x", is->is_dvmabase, is->is_dvmaend);
	is->is_dvmamap = extent_create(name,
	    is->is_dvmabase, (u_long)is->is_dvmaend + 1,
	    M_DEVBUF, NULL, 0, EX_NOCOALESCE);
	mtx_init(&is->is_mtx, IPL_HIGH);

	printf("\n");
}

/*
 * Add an entry to the IOMMU table.
 */
void
viommu_enter(struct iommu_state *is, struct strbuf_ctl *sb, bus_addr_t va,
    paddr_t pa, int flags)
{
	u_int64_t tsbid = IOTSBSLOT(va, is->is_tsbsize);
	paddr_t page_list[1], addr;
	u_int64_t attr, nmapped;
	int err;

	KASSERT(sb == NULL);

#ifdef DIAGNOSTIC
	if (va < is->is_dvmabase || (va + PAGE_MASK) > is->is_dvmaend)
		panic("viommu_enter: va %#lx not in DVMA space", va);
#endif

	attr = PCI_MAP_ATTR_READ | PCI_MAP_ATTR_WRITE;
	if (flags & BUS_DMA_READ)
		attr &= ~PCI_MAP_ATTR_READ;
	if (flags & BUS_DMA_WRITE)
		attr &= ~PCI_MAP_ATTR_WRITE;

	page_list[0] = trunc_page(pa);
	if (!pmap_extract(pmap_kernel(), (vaddr_t)page_list, &addr))
		panic("viommu_enter: pmap_extract failed");
	err = hv_pci_iommu_map(is->is_devhandle, tsbid, 1, attr,
	    addr, &nmapped);
	if (err != H_EOK || nmapped != 1)
		panic("hv_pci_iommu_map: err=%d", err);
}

/*
 * Remove an entry from the IOMMU table.
 */
void
viommu_remove(struct iommu_state *is, struct strbuf_ctl *sb, bus_addr_t va)
{
	u_int64_t tsbid = IOTSBSLOT(va, is->is_tsbsize);
	u_int64_t ndemapped;
	int err;

	KASSERT(sb == NULL);

#ifdef DIAGNOSTIC
	if (va < is->is_dvmabase || (va + PAGE_MASK) > is->is_dvmaend)
		panic("iommu_remove: va 0x%lx not in DVMA space", (u_long)va);
	if (va != trunc_page(va)) {
		printf("iommu_remove: unaligned va: %lx\n", va);
		va = trunc_page(va);
	}
#endif

	err = hv_pci_iommu_demap(is->is_devhandle, tsbid, 1, &ndemapped);
	if (err != H_EOK || ndemapped != 1)
		panic("hv_pci_iommu_unmap: err=%d", err);
}

/*
 * IOMMU DVMA operations, sun4v hypervisor version.
 */

#define BUS_DMA_FIND_PARENT(t, fn)                                      \
        if (t->_parent == NULL)                                         \
                panic("null bus_dma parent (" #fn ")");                 \
        for (t = t->_parent; t->fn == NULL; t = t->_parent)             \
                if (t->_parent == NULL)                                 \
                        panic("no bus_dma " #fn " located");

int
viommu_dvmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0,
    struct iommu_state *is, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamap)
{
	int ret;
	bus_dmamap_t map;
	struct iommu_map_state *ims;

	BUS_DMA_FIND_PARENT(t, _dmamap_create);
	ret = (*t->_dmamap_create)(t, t0, size, nsegments, maxsegsz, boundary,
	    flags, &map);

	if (ret)
		return (ret);

	ims = viommu_iomap_create(atop(round_page(size)));

	if (ims == NULL) {
		bus_dmamap_destroy(t0, map);
		return (ENOMEM);
	}

	ims->ims_iommu = is;
	map->_dm_cookie = ims;

	*dmamap = map;

	return (0);
}

void
viommu_dvmamap_destroy(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map)
{
	/*
	 * The specification (man page) requires a loaded
	 * map to be unloaded before it is destroyed.
	 */
	if (map->dm_nsegs)
		bus_dmamap_unload(t0, map);

        if (map->_dm_cookie)
                iommu_iomap_destroy(map->_dm_cookie);
	map->_dm_cookie = NULL;

	BUS_DMA_FIND_PARENT(t, _dmamap_destroy);
	(*t->_dmamap_destroy)(t, t0, map);
}

/*
 * Load a contiguous kva buffer into a dmamap.  The physical pages are
 * not assumed to be contiguous.  Two passes are made through the buffer
 * and both call pmap_extract() for the same va->pa translations.  It
 * is possible to run out of pa->dvma mappings; the code should be smart
 * enough to resize the iomap (when the "flags" permit allocation).  It
 * is trivial to compute the number of entries required (round the length
 * up to the page size and then divide by the page size)...
 */
int
viommu_dvmamap_load(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    void *buf, bus_size_t buflen, struct proc *p, int flags)
{
	int err = 0;
	bus_size_t sgsize;
	u_long dvmaddr, sgstart, sgend;
	bus_size_t align, boundary;
	struct iommu_state *is;
	struct iommu_map_state *ims = map->_dm_cookie;
	pmap_t pmap;

#ifdef DIAGNOSTIC
	if (ims == NULL)
		panic("viommu_dvmamap_load: null map state");
	if (ims->ims_iommu == NULL)
		panic("viommu_dvmamap_load: null iommu");
#endif
	is = ims->ims_iommu;

	if (map->dm_nsegs) {
		/*
		 * Is it still in use? _bus_dmamap_load should have taken care
		 * of this.
		 */
#ifdef DIAGNOSTIC
		panic("iommu_dvmamap_load: map still in use");
#endif
		bus_dmamap_unload(t0, map);
	}

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_nsegs = 0;

	if (buflen < 1 || buflen > map->_dm_size) {
		DPRINTF(IDB_BUSDMA,
		    ("iommu_dvmamap_load(): error %d > %d -- "
		     "map size exceeded!\n", (int)buflen, (int)map->_dm_size));
		return (EINVAL);
	}

	/*
	 * A boundary presented to bus_dmamem_alloc() takes precedence
	 * over boundary in the map.
	 */
	if ((boundary = (map->dm_segs[0]._ds_boundary)) == 0)
		boundary = map->_dm_boundary;
	align = MAX(map->dm_segs[0]._ds_align, PAGE_SIZE);

	pmap = p ? p->p_vmspace->vm_map.pmap : pmap_kernel();

	/* Count up the total number of pages we need */
	iommu_iomap_clear_pages(ims);
	{ /* Scope */
		bus_addr_t a, aend;
		bus_addr_t addr = (bus_addr_t)buf;
		int seg_len = buflen;

		aend = round_page(addr + seg_len);
		for (a = trunc_page(addr); a < aend; a += PAGE_SIZE) {
			paddr_t pa;

			if (pmap_extract(pmap, a, &pa) == FALSE)
				panic("iomap pmap error addr 0x%lx", a);

			err = iommu_iomap_insert_page(ims, pa);
			if (err) {
				printf("iomap insert error: %d for "
				    "va 0x%lx pa 0x%lx "
				    "(buf %p len %ld/%lx)\n",
				    err, a, pa, buf, buflen, buflen);
				iommu_iomap_clear_pages(ims);
				return (EFBIG);
			}
		}
	}
	if (flags & BUS_DMA_OVERRUN) {
		err = iommu_iomap_insert_page(ims, is->is_scratch);
		if (err) {
			iommu_iomap_clear_pages(ims);
			return (EFBIG);
		}
	}
	sgsize = ims->ims_map.ipm_pagecnt * PAGE_SIZE;

	mtx_enter(&is->is_mtx);
	if (flags & BUS_DMA_24BIT) {
		sgstart = MAX(is->is_dvmamap->ex_start, 0xff000000);
		sgend = MIN(is->is_dvmamap->ex_end, 0xffffffff);
	} else {
		sgstart = is->is_dvmamap->ex_start;
		sgend = is->is_dvmamap->ex_end;
	}

	/* 
	 * If our segment size is larger than the boundary we need to 
	 * split the transfer up into little pieces ourselves.
	 */
	err = extent_alloc_subregion_with_descr(is->is_dvmamap, sgstart, sgend,
	    sgsize, align, 0, (sgsize > boundary) ? 0 : boundary, 
	    EX_NOWAIT | EX_BOUNDZERO, &ims->ims_er, (u_long *)&dvmaddr);
	mtx_leave(&is->is_mtx);

#ifdef DEBUG
	if (err || (dvmaddr == (bus_addr_t)-1))	{ 
		printf("iommu_dvmamap_load(): extent_alloc(%d, %x) failed!\n",
		    (int)sgsize, flags);
#ifdef DDB
		if (iommudebug & IDB_BREAK)
			db_enter();
#endif
	}		
#endif	
	if (err != 0) {
		iommu_iomap_clear_pages(ims);
		return (err);
	}

	/* Set the active DVMA map */
	map->_dm_dvmastart = dvmaddr;
	map->_dm_dvmasize = sgsize;

	map->dm_mapsize = buflen;

	viommu_iomap_load_map(is, ims, dvmaddr, flags);

	{ /* Scope */
		bus_addr_t a, aend;
		bus_addr_t addr = (bus_addr_t)buf;
		int seg_len = buflen;

		aend = round_page(addr + seg_len);
		for (a = trunc_page(addr); a < aend; a += PAGE_SIZE) {
			bus_addr_t pgstart;
			bus_addr_t pgend;
			paddr_t pa;
			int pglen;

			/* Yuck... Redoing the same pmap_extract... */
			if (pmap_extract(pmap, a, &pa) == FALSE)
				panic("iomap pmap error addr 0x%lx", a);

			pgstart = pa | (MAX(a, addr) & PAGE_MASK);
			pgend = pa | (MIN(a + PAGE_SIZE - 1,
			    addr + seg_len - 1) & PAGE_MASK);
			pglen = pgend - pgstart + 1;

			if (pglen < 1)
				continue;

			err = viommu_dvmamap_append_range(t, map, pgstart,
			    pglen, flags, boundary);
			if (err == EFBIG)
				break;
			else if (err) {
				printf("iomap load seg page: %d for "
				    "va 0x%lx pa %lx (%lx - %lx) "
				    "for %d/0x%x\n",
				    err, a, pa, pgstart, pgend, pglen, pglen);
				break;
			}
		}
	}

	if (err)
		viommu_dvmamap_unload(t, t0, map);

	return (err);
}

/*
 * Load a dvmamap from an array of segs or an mlist (if the first
 * "segs" entry's mlist is non-null).  It calls iommu_dvmamap_load_segs()
 * or iommu_dvmamap_load_mlist() for part of the 2nd pass through the
 * mapping.  This is ugly.  A better solution would probably be to have
 * function pointers for implementing the traversal.  That way, there
 * could be one core load routine for each of the three required algorithms
 * (buffer, seg, and mlist).  That would also mean that the traversal
 * algorithm would then only need one implementation for each algorithm
 * instead of two (one for populating the iomap and one for populating
 * the dvma map).
 */
int
viommu_dvmamap_load_raw(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	int i;
	int left;
	int err = 0;
	bus_size_t sgsize;
	bus_size_t boundary, align;
	u_long dvmaddr, sgstart, sgend;
	struct iommu_state *is;
	struct iommu_map_state *ims = map->_dm_cookie;

#ifdef DIAGNOSTIC
	if (ims == NULL)
		panic("viommu_dvmamap_load_raw: null map state");
	if (ims->ims_iommu == NULL)
		panic("viommu_dvmamap_load_raw: null iommu");
#endif
	is = ims->ims_iommu;

	if (map->dm_nsegs) {
		/* Already in use?? */
#ifdef DIAGNOSTIC
		panic("iommu_dvmamap_load_raw: map still in use");
#endif
		bus_dmamap_unload(t0, map);
	}

	/*
	 * A boundary presented to bus_dmamem_alloc() takes precedence
	 * over boundary in the map.
	 */
	if ((boundary = segs[0]._ds_boundary) == 0)
		boundary = map->_dm_boundary;

	align = MAX(segs[0]._ds_align, PAGE_SIZE);

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_nsegs = 0;

	iommu_iomap_clear_pages(ims);
	if (segs[0]._ds_mlist) {
		struct pglist *mlist = segs[0]._ds_mlist;
		struct vm_page *m;
		for (m = TAILQ_FIRST(mlist); m != NULL;
		    m = TAILQ_NEXT(m,pageq)) {
			err = iommu_iomap_insert_page(ims, VM_PAGE_TO_PHYS(m));

			if(err) {
				printf("iomap insert error: %d for "
				    "pa 0x%lx\n", err, VM_PAGE_TO_PHYS(m));
				iommu_iomap_clear_pages(ims);
				return (EFBIG);
			}
		}
	} else {
		/* Count up the total number of pages we need */
		for (i = 0, left = size; left > 0 && i < nsegs; i++) {
			bus_addr_t a, aend;
			bus_size_t len = segs[i].ds_len;
			bus_addr_t addr = segs[i].ds_addr;
			int seg_len = MIN(left, len);

			if (len < 1)
				continue;

			aend = round_page(addr + seg_len);
			for (a = trunc_page(addr); a < aend; a += PAGE_SIZE) {

				err = iommu_iomap_insert_page(ims, a);
				if (err) {
					printf("iomap insert error: %d for "
					    "pa 0x%lx\n", err, a);
					iommu_iomap_clear_pages(ims);
					return (EFBIG);
				}
			}

			left -= seg_len;
		}
	}
	if (flags & BUS_DMA_OVERRUN) {
		err = iommu_iomap_insert_page(ims, is->is_scratch);
		if (err) {
			iommu_iomap_clear_pages(ims);
			return (EFBIG);
		}
	}
	sgsize = ims->ims_map.ipm_pagecnt * PAGE_SIZE;

	mtx_enter(&is->is_mtx);
	if (flags & BUS_DMA_24BIT) {
		sgstart = MAX(is->is_dvmamap->ex_start, 0xff000000);
		sgend = MIN(is->is_dvmamap->ex_end, 0xffffffff);
	} else {
		sgstart = is->is_dvmamap->ex_start;
		sgend = is->is_dvmamap->ex_end;
	}

	/* 
	 * If our segment size is larger than the boundary we need to 
	 * split the transfer up into little pieces ourselves.
	 */
	err = extent_alloc_subregion_with_descr(is->is_dvmamap, sgstart, sgend,
	    sgsize, align, 0, (sgsize > boundary) ? 0 : boundary, 
	    EX_NOWAIT | EX_BOUNDZERO, &ims->ims_er, (u_long *)&dvmaddr);
	mtx_leave(&is->is_mtx);

	if (err != 0) {
		iommu_iomap_clear_pages(ims);
		return (err);
	}

#ifdef DEBUG
	if (dvmaddr == (bus_addr_t)-1)	{ 
		printf("iommu_dvmamap_load_raw(): extent_alloc(%d, %x) "
		    "failed!\n", (int)sgsize, flags);
#ifdef DDB
		if (iommudebug & IDB_BREAK)
			db_enter();
#else
		panic("");
#endif
	}		
#endif	

	/* Set the active DVMA map */
	map->_dm_dvmastart = dvmaddr;
	map->_dm_dvmasize = sgsize;

	map->dm_mapsize = size;

	viommu_iomap_load_map(is, ims, dvmaddr, flags);

	if (segs[0]._ds_mlist)
		err = viommu_dvmamap_load_mlist(t, is, map, segs[0]._ds_mlist,
		    flags, size, boundary);
	else
		err = viommu_dvmamap_load_seg(t, is, map, segs, nsegs,
		    flags, size, boundary);

	if (err)
		viommu_dvmamap_unload(t, t0, map);

	return (err);
}

/*
 * Insert a range of addresses into a loaded map respecting the specified
 * boundary and alignment restrictions.  The range is specified by its 
 * physical address and length.  The range cannot cross a page boundary.
 * This code (along with most of the rest of the function in this file)
 * assumes that the IOMMU page size is equal to PAGE_SIZE.
 */
int
viommu_dvmamap_append_range(bus_dma_tag_t t, bus_dmamap_t map, paddr_t pa,
    bus_size_t length, int flags, bus_size_t boundary)
{
	struct iommu_map_state *ims = map->_dm_cookie;
	bus_addr_t sgstart, sgend, bd_mask;
	bus_dma_segment_t *seg = NULL;
	int i = map->dm_nsegs;

#ifdef DEBUG
	if (ims == NULL)
		panic("iommu_dvmamap_append_range: null map state");
#endif

	sgstart = iommu_iomap_translate(ims, pa);
	sgend = sgstart + length - 1;

#ifdef DIAGNOSTIC
	if (sgstart == 0 || sgstart > sgend) {
		printf("append range invalid mapping for %lx "
		    "(0x%lx - 0x%lx)\n", pa, sgstart, sgend);
		map->dm_nsegs = 0;
		return (EINVAL);
	}
#endif

#ifdef DEBUG
	if (trunc_page(sgstart) != trunc_page(sgend)) {
		printf("append range crossing page boundary! "
		    "pa %lx length %ld/0x%lx sgstart %lx sgend %lx\n",
		    pa, length, length, sgstart, sgend);
	}
#endif

	/*
	 * We will attempt to merge this range with the previous entry
	 * (if there is one).
	 */
	if (i > 0) {
		seg = &map->dm_segs[i - 1];
		if (sgstart == seg->ds_addr + seg->ds_len) {
			length += seg->ds_len;
			sgstart = seg->ds_addr;
			sgend = sgstart + length - 1;
		} else
			seg = NULL;
	}

	if (seg == NULL) {
		seg = &map->dm_segs[i];
		if (++i > map->_dm_segcnt) {
			map->dm_nsegs = 0;
			return (EFBIG);
		}
	}

	/*
	 * At this point, "i" is the index of the *next* bus_dma_segment_t
	 * (the segment count, aka map->dm_nsegs) and "seg" points to the
	 * *current* entry.  "length", "sgstart", and "sgend" reflect what
	 * we intend to put in "*seg".  No assumptions should be made about
	 * the contents of "*seg".  Only "boundary" issue can change this
	 * and "boundary" is often zero, so explicitly test for that case
	 * (the test is strictly an optimization).
	 */ 
	if (boundary != 0) {
		bd_mask = ~(boundary - 1);

		while ((sgstart & bd_mask) != (sgend & bd_mask)) {
			/*
			 * We are crossing a boundary so fill in the current
			 * segment with as much as possible, then grab a new
			 * one.
			 */

			seg->ds_addr = sgstart;
			seg->ds_len = boundary - (sgstart & ~bd_mask);

			sgstart += seg->ds_len; /* sgend stays the same */
			length -= seg->ds_len;

			seg = &map->dm_segs[i];
			if (++i > map->_dm_segcnt) {
				map->dm_nsegs = 0;
				return (EFBIG);
			}
		}
	}

	seg->ds_addr = sgstart;
	seg->ds_len = length;
	map->dm_nsegs = i;

	return (0);
}

/*
 * Populate the iomap from a bus_dma_segment_t array.  See note for
 * iommu_dvmamap_load() * regarding page entry exhaustion of the iomap.
 * This is less of a problem for load_seg, as the number of pages
 * is usually similar to the number of segments (nsegs).
 */
int
viommu_dvmamap_load_seg(bus_dma_tag_t t, struct iommu_state *is,
    bus_dmamap_t map, bus_dma_segment_t *segs, int nsegs, int flags,
    bus_size_t size, bus_size_t boundary)
{
	int i;
	int left;
	int seg;

	/*
	 * This segs is made up of individual physical
	 * segments, probably by _bus_dmamap_load_uio() or
	 * _bus_dmamap_load_mbuf().  Ignore the mlist and
	 * load each one individually.
	 */

	/*
	 * Keep in mind that each segment could span
	 * multiple pages and that these are not always
	 * adjacent. The code is no longer adding dvma
	 * aliases to the IOMMU.  The STC will not cross
	 * page boundaries anyway and a IOMMU table walk
	 * vs. what may be a streamed PCI DMA to a ring
	 * descriptor is probably a wash.  It eases TLB
	 * pressure and in the worst possible case, it is
	 * only as bad a non-IOMMUed architecture.  More
	 * importantly, the code is not quite as hairy.
	 * (It's bad enough as it is.)
	 */
	left = size;
	seg = 0;
	for (i = 0; left > 0 && i < nsegs; i++) {
		bus_addr_t a, aend;
		bus_size_t len = segs[i].ds_len;
		bus_addr_t addr = segs[i].ds_addr;
		int seg_len = MIN(left, len);

		if (len < 1)
			continue;

		aend = round_page(addr + seg_len);
		for (a = trunc_page(addr); a < aend; a += PAGE_SIZE) {
			bus_addr_t pgstart;
			bus_addr_t pgend;
			int pglen;
			int err;

			pgstart = MAX(a, addr);
			pgend = MIN(a + PAGE_SIZE - 1, addr + seg_len - 1);
			pglen = pgend - pgstart + 1;
			
			if (pglen < 1)
				continue;

			err = viommu_dvmamap_append_range(t, map, pgstart,
			    pglen, flags, boundary);
			if (err == EFBIG)
				return (err);
			if (err) {
				printf("iomap load seg page: %d for "
				    "pa 0x%lx (%lx - %lx for %d/%x\n",
				    err, a, pgstart, pgend, pglen, pglen);
				return (err);
			}

		}

		left -= seg_len;
	}
	return (0);
}

/*
 * Populate the iomap from an mlist.  See note for iommu_dvmamap_load()
 * regarding page entry exhaustion of the iomap.
 */
int
viommu_dvmamap_load_mlist(bus_dma_tag_t t, struct iommu_state *is,
    bus_dmamap_t map, struct pglist *mlist, int flags,
    bus_size_t size, bus_size_t boundary)
{
	struct vm_page *m;
	paddr_t pa;
	int err;

	/*
	 * This was allocated with bus_dmamem_alloc.
	 * The pages are on an `mlist'.
	 */
	for (m = TAILQ_FIRST(mlist); m != NULL; m = TAILQ_NEXT(m,pageq)) {
		pa = VM_PAGE_TO_PHYS(m);

		err = viommu_dvmamap_append_range(t, map, pa,
		    MIN(PAGE_SIZE, size), flags, boundary);
		if (err == EFBIG)
			return (err);
		if (err) {
			printf("iomap load seg page: %d for pa 0x%lx "
			    "(%lx - %lx for %d/%x\n", err, pa, pa,
			    pa + PAGE_SIZE, PAGE_SIZE, PAGE_SIZE);
			return (err);
		}
		if (size < PAGE_SIZE)
			break;
		size -= PAGE_SIZE;
	}

	return (0);
}

/*
 * Unload a dvmamap.
 */
void
viommu_dvmamap_unload(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map)
{
	struct iommu_state *is;
	struct iommu_map_state *ims = map->_dm_cookie;
	bus_addr_t dvmaddr = map->_dm_dvmastart;
	bus_size_t sgsize = map->_dm_dvmasize;
	int error;

#ifdef DEBUG
	if (ims == NULL)
		panic("viommu_dvmamap_unload: null map state");
	if (ims->ims_iommu == NULL)
		panic("viommu_dvmamap_unload: null iommu");
#endif /* DEBUG */

	is = ims->ims_iommu;

	/* Remove the IOMMU entries */
	viommu_iomap_unload_map(is, ims);

	/* Clear the iomap */
	iommu_iomap_clear_pages(ims);

	bus_dmamap_unload(t->_parent, map);

	/* Mark the mappings as invalid. */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	mtx_enter(&is->is_mtx);
	error = extent_free(is->is_dvmamap, dvmaddr, sgsize, EX_NOWAIT);
	map->_dm_dvmastart = 0;
	map->_dm_dvmasize = 0;
	mtx_leave(&is->is_mtx);
	if (error != 0)
		printf("warning: %ld of DVMA space lost\n", sgsize);
}

void
viommu_dvmamap_sync(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    bus_addr_t offset, bus_size_t len, int ops)
{
#ifdef DIAGNOSTIC
	struct iommu_map_state *ims = map->_dm_cookie;

	if (ims == NULL)
		panic("viommu_dvmamap_sync: null map state");
	if (ims->ims_iommu == NULL)
		panic("viommu_dvmamap_sync: null iommu");
#endif
	if (len == 0)
		return;

	if (ops & BUS_DMASYNC_PREWRITE)
		__membar("#MemIssue");

#if 0
	if (ops & (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_PREWRITE))
		_viommu_dvmamap_sync(t, t0, map, offset, len, ops);
#endif

	if (ops & BUS_DMASYNC_POSTREAD)
		__membar("#MemIssue");
}

int
viommu_dvmamem_alloc(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    bus_size_t alignment, bus_size_t boundary, bus_dma_segment_t *segs,
    int nsegs, int *rsegs, int flags)
{

	DPRINTF(IDB_BUSDMA, ("iommu_dvmamem_alloc: sz %llx align %llx "
	    "bound %llx segp %p flags %d\n", (unsigned long long)size,
	    (unsigned long long)alignment, (unsigned long long)boundary,
	    segs, flags));
	BUS_DMA_FIND_PARENT(t, _dmamem_alloc);
	return ((*t->_dmamem_alloc)(t, t0, size, alignment, boundary,
	    segs, nsegs, rsegs, flags | BUS_DMA_DVMA));
}

void
viommu_dvmamem_free(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dma_segment_t *segs,
    int nsegs)
{

	DPRINTF(IDB_BUSDMA, ("iommu_dvmamem_free: segp %p nsegs %d\n",
	    segs, nsegs));
	BUS_DMA_FIND_PARENT(t, _dmamem_free);
	(*t->_dmamem_free)(t, t0, segs, nsegs);
}

/*
 * Create a new iomap.
 */
struct iommu_map_state *
viommu_iomap_create(int n)
{
	struct iommu_map_state *ims;

	/* Safety for heavily fragmented data, such as mbufs */
	n += 4;
	if (n < 16)
		n = 16;

	ims = malloc(sizeof(*ims) + (n - 1) * sizeof(ims->ims_map.ipm_map[0]),
		M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ims == NULL)
		return (NULL);

	/* Initialize the map. */
	ims->ims_map.ipm_maxpage = n;
	SPLAY_INIT(&ims->ims_map.ipm_tree);

	return (ims);
}

/*
 * Locate the iomap by filling in the pa->va mapping and inserting it
 * into the IOMMU tables.
 */
void
viommu_iomap_load_map(struct iommu_state *is, struct iommu_map_state *ims,
    bus_addr_t vmaddr, int flags)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	int i;

	for (i = 0, e = ipm->ipm_map; i < ipm->ipm_pagecnt; ++i, ++e) {
		e->ipe_va = vmaddr;
		viommu_enter(is, NULL, e->ipe_va, e->ipe_pa, flags);
		vmaddr += PAGE_SIZE;
	}
}

/*
 * Remove the iomap from the IOMMU.
 */
void
viommu_iomap_unload_map(struct iommu_state *is, struct iommu_map_state *ims)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	int i;

	for (i = 0, e = ipm->ipm_map; i < ipm->ipm_pagecnt; ++i, ++e)
		viommu_remove(is, NULL, e->ipe_va);
}
