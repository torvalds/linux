/*	$OpenBSD: iommu.c,v 1.83 2023/10/18 14:24:29 jan Exp $	*/
/*	$NetBSD: iommu.c,v 1.47 2002/02/08 20:03:45 eeh Exp $	*/

/*
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
 * UltraSPARC IOMMU support; used by both the sbus and pci code.
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
#include <sparc64/dev/iommureg.h>
#include <sparc64/dev/iommuvar.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>

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
int iommudebug = IDB_INFO;
#define DPRINTF(l, s)   do { if (iommudebug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

void iommu_enter(struct iommu_state *, struct strbuf_ctl *, bus_addr_t,
    paddr_t, int);
void iommu_remove(struct iommu_state *, struct strbuf_ctl *, bus_addr_t);
int iommu_dvmamap_sync_range(struct strbuf_ctl*, bus_addr_t, bus_size_t);
int iommu_strbuf_flush_done(struct iommu_map_state *);
int iommu_dvmamap_load_seg(bus_dma_tag_t, struct iommu_state *,
    bus_dmamap_t, bus_dma_segment_t *, int, int, bus_size_t, bus_size_t);
int iommu_dvmamap_load_mlist(bus_dma_tag_t, struct iommu_state *,
    bus_dmamap_t, struct pglist *, int, bus_size_t, bus_size_t);
int iommu_dvmamap_validate_map(bus_dma_tag_t, struct iommu_state *,
    bus_dmamap_t);
void iommu_dvmamap_print_map(bus_dma_tag_t, struct iommu_state *,
    bus_dmamap_t);
int iommu_dvmamap_append_range(bus_dma_tag_t, bus_dmamap_t, paddr_t,
    bus_size_t, int, bus_size_t);
int iommu_dvmamap_insert(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
    bus_size_t, int, bus_size_t);
int64_t iommu_tsb_entry(struct iommu_state *, bus_addr_t);
void strbuf_reset(struct strbuf_ctl *);
int iommu_iomap_insert_page(struct iommu_map_state *, paddr_t);
bus_addr_t iommu_iomap_translate(struct iommu_map_state *, paddr_t);
void iommu_iomap_load_map(struct iommu_state *, struct iommu_map_state *,
    bus_addr_t, int);
void iommu_iomap_unload_map(struct iommu_state *, struct iommu_map_state *);
struct iommu_map_state *iommu_iomap_create(int);
void iommu_iomap_destroy(struct iommu_map_state *);
void iommu_iomap_clear_pages(struct iommu_map_state *);
void _iommu_dvmamap_sync(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
    bus_addr_t, bus_size_t, int);

void iommu_hw_enable(struct iommu_state *);

const struct iommu_hw iommu_hw_default = {
	.ihw_enable	= iommu_hw_enable,

	.ihw_dvma_pa	= IOTTE_PAMASK,

	.ihw_bypass	= 0x3fffUL << 50,
	.ihw_bypass_nc	= 0,
	.ihw_bypass_ro	= 0,
};

void
iommu_hw_enable(struct iommu_state *is)
{
	IOMMUREG_WRITE(is, iommu_tsb, is->is_ptsb);
	IOMMUREG_WRITE(is, iommu_cr, IOMMUCR_EN | (is->is_tsbsize << 16));
}

/*
 * Initiate an STC entry flush.
 */
static inline void
iommu_strbuf_flush(struct strbuf_ctl *sb, bus_addr_t va)
{
#ifdef DEBUG
	if (sb->sb_flush == NULL) {
		printf("iommu_strbuf_flush: attempting to flush w/o STC\n");
		return;
	}
#endif

	bus_space_write_8(sb->sb_bustag, sb->sb_sb,
	    STRBUFREG(strbuf_pgflush), va);
}

/*
 * initialise the UltraSPARC IOMMU (SBus or PCI):
 *	- allocate and setup the iotsb.
 *	- enable the IOMMU
 *	- initialise the streaming buffers (if they exist)
 *	- create a private DVMA map.
 */
void
iommu_init(char *name, const struct iommu_hw *ihw, struct iommu_state *is,
    int tsbsize, u_int32_t iovabase)
{
	psize_t size;
	vaddr_t va;
	paddr_t pa;
	struct vm_page *m;
	struct pglist mlist;

	/*
	 * Setup the iommu.
	 *
	 * The sun4u iommu is part of the SBus or PCI controller so we will
	 * deal with it here..
	 *
	 * For sysio and psycho/psycho+ the IOMMU address space always ends at
	 * 0xffffe000, but the starting address depends on the size of the
	 * map.  The map size is 1024 * 2 ^ is->is_tsbsize entries, where each
	 * entry is 8 bytes.  The start of the map can be calculated by
	 * (0xffffe000 << (8 + is->is_tsbsize)).
	 *
	 * But sabre and hummingbird use a different scheme that seems to
	 * be hard-wired, so we read the start and size from the PROM and
	 * just use those values.
	 */

	is->is_hw = ihw;

	is->is_tsbsize = tsbsize;
	if (iovabase == (u_int32_t)-1) {
		is->is_dvmabase = IOTSB_VSTART(is->is_tsbsize);
		is->is_dvmaend = IOTSB_VEND;
	} else {
		is->is_dvmabase = iovabase;
		is->is_dvmaend = iovabase + IOTSB_VSIZE(tsbsize) - 1;
	}

	/*
	 * Allocate memory for I/O pagetables.  They need to be physically
	 * contiguous.
	 */

	size = PAGE_SIZE << is->is_tsbsize;
	TAILQ_INIT(&mlist);
	if (uvm_pglistalloc((psize_t)size, (paddr_t)0, (paddr_t)-1,
	    (paddr_t)PAGE_SIZE, (paddr_t)0, &mlist, 1, UVM_PLA_NOWAIT) != 0)
		panic("iommu_init: no memory");

	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		panic("iommu_init: no memory");
	is->is_tsb = (int64_t *)va;

	m = TAILQ_FIRST(&mlist);
	is->is_ptsb = VM_PAGE_TO_PHYS(m);

	/* Map the pages */
	for (; m != NULL; m = TAILQ_NEXT(m,pageq)) {
		pa = VM_PAGE_TO_PHYS(m);
		pmap_enter(pmap_kernel(), va, pa | PMAP_NVC,
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED);
		va += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
	memset(is->is_tsb, 0, size);

	TAILQ_INIT(&mlist);
	if (uvm_pglistalloc(PAGE_SIZE, 0, -1, PAGE_SIZE, 0, &mlist, 1,
	    UVM_PLA_NOWAIT | UVM_PLA_ZERO) != 0)
		panic("%s: no memory", __func__);
	m = TAILQ_FIRST(&mlist);
	is->is_scratch = VM_PAGE_TO_PHYS(m);

#ifdef DEBUG
	if (iommudebug & IDB_INFO) {
		/* Probe the iommu */
		/* The address or contents of the regs...? */
		printf("iommu regs at: cr=%lx tsb=%lx flush=%lx\n",
		    (u_long)bus_space_vaddr(is->is_bustag, is->is_iommu) +
			IOMMUREG(iommu_cr),
		    (u_long)bus_space_vaddr(is->is_bustag, is->is_iommu) +
			IOMMUREG(iommu_tsb),
		    (u_long)bus_space_vaddr(is->is_bustag, is->is_iommu) +
			IOMMUREG(iommu_flush));
		printf("iommu cr=%llx tsb=%llx\n",
		    IOMMUREG_READ(is, iommu_cr),
		    IOMMUREG_READ(is, iommu_tsb));
		printf("TSB base %p phys %llx\n",
		    (void *)is->is_tsb, (unsigned long long)is->is_ptsb);
		delay(1000000); /* 1 s */
	}
#endif

	/*
	 * Now all the hardware's working we need to allocate a dvma map.
	 */
	printf("dvma map %x-%x", is->is_dvmabase, is->is_dvmaend);
#ifdef DEBUG
	printf(", iotdb %llx-%llx",
	    (unsigned long long)is->is_ptsb,
	    (unsigned long long)(is->is_ptsb + size));
#endif
	is->is_dvmamap = extent_create(name,
	    is->is_dvmabase, (u_long)is->is_dvmaend + 1,
	    M_DEVBUF, NULL, 0, EX_NOCOALESCE);
	mtx_init(&is->is_mtx, IPL_HIGH);

	/*
	 * Now actually start up the IOMMU.
	 */
	iommu_reset(is);
	printf("\n");
}

/*
 * Streaming buffers don't exist on the UltraSPARC IIi/e; we should have
 * detected that already and disabled them.  If not, we will notice that
 * they aren't there when the STRBUF_EN bit does not remain.
 */
void
iommu_reset(struct iommu_state *is)
{
	int i;

	(*is->is_hw->ihw_enable)(is);

	for (i = 0; i < 2; ++i) {
		struct strbuf_ctl *sb = is->is_sb[i];

		if (sb == NULL)
			continue;

		sb->sb_iommu = is;
		strbuf_reset(sb);

		if (sb->sb_flush)
			printf(", STC%d enabled", i);
	}

	if (ISSET(is->is_hw->ihw_flags, IOMMU_HW_FLUSH_CACHE))
		IOMMUREG_WRITE(is, iommu_cache_invalidate, -1ULL);
}

/*
 * Initialize one STC.
 */
void
strbuf_reset(struct strbuf_ctl *sb)
{
	if(sb->sb_flush == NULL)
		return;

	bus_space_write_8(sb->sb_bustag, sb->sb_sb,
	    STRBUFREG(strbuf_ctl), STRBUF_EN);

	__membar("#Lookaside");

	/* No streaming buffers? Disable them */
	if (bus_space_read_8(sb->sb_bustag, sb->sb_sb,
	    STRBUFREG(strbuf_ctl)) == 0) {
		sb->sb_flush = NULL;
	} else {
		/*
		 * locate the pa of the flush buffer
		 */
		if (pmap_extract(pmap_kernel(),
		    (vaddr_t)sb->sb_flush, &sb->sb_flushpa) == FALSE)
			sb->sb_flush = NULL;
		mtx_init(&sb->sb_mtx, IPL_HIGH);
	}
}

/*
 * Add an entry to the IOMMU table.
 *
 * The entry is marked streaming if an STC was detected and 
 * the BUS_DMA_STREAMING flag is set.
 */
void
iommu_enter(struct iommu_state *is, struct strbuf_ctl *sb, bus_addr_t va,
    paddr_t pa, int flags)
{
	int64_t tte;
	volatile int64_t *tte_ptr = &is->is_tsb[IOTSBSLOT(va,is->is_tsbsize)];

#ifdef DIAGNOSTIC
	if (va < is->is_dvmabase || (va + PAGE_MASK) > is->is_dvmaend)
		panic("iommu_enter: va %#lx not in DVMA space", va);

	tte = *tte_ptr;

	if (tte & IOTTE_V) {
		printf("Overwriting valid tte entry (dva %lx pa %lx "
		    "&tte %p tte %llx)\n", va, pa, tte_ptr, tte);
		extent_print(is->is_dvmamap);
		panic("IOMMU overwrite");
	}
#endif

	tte = MAKEIOTTE(pa, !(flags & BUS_DMA_NOWRITE),
	    !(flags & BUS_DMA_NOCACHE), (flags & BUS_DMA_STREAMING));

	DPRINTF(IDB_IOMMU, ("Clearing TSB slot %d for va %p\n", 
	    (int)IOTSBSLOT(va,is->is_tsbsize), (void *)(u_long)va));

	*tte_ptr = tte;

	/*
	 * Why bother to flush this va?  It should only be relevant for
	 * V ==> V or V ==> non-V transitions.  The former is illegal and
	 * the latter is never done here.  It is true that this provides
	 * some protection against a misbehaving master using an address
	 * after it should.  The IOMMU documentations specifically warns
	 * that the consequences of a simultaneous IOMMU flush and DVMA
	 * access to the same address are undefined.  (By that argument,
	 * the STC should probably be flushed as well.)   Note that if
	 * a bus master keeps using a memory region after it has been
	 * unmapped, the specific behavior of the IOMMU is likely to
	 * be the least of our worries.
	 */
	IOMMUREG_WRITE(is, iommu_flush, va);

	DPRINTF(IDB_IOMMU, ("iommu_enter: va %lx pa %lx TSB[%lx]@%p=%lx\n",
	    va, (long)pa, (u_long)IOTSBSLOT(va,is->is_tsbsize), 
	    (void *)(u_long)&is->is_tsb[IOTSBSLOT(va,is->is_tsbsize)],
	    (u_long)tte));
}

/*
 * Remove an entry from the IOMMU table.
 *
 * The entry is flushed from the STC if an STC is detected and the TSB
 * entry has the IOTTE_STREAM flags set.  It should be impossible for
 * the TSB entry to have this flag set without the BUS_DMA_STREAMING
 * flag, but better to be safe.  (The IOMMU will be ignored as long
 * as an STC entry exists.)
 */
void
iommu_remove(struct iommu_state *is, struct strbuf_ctl *sb, bus_addr_t va)
{
	int64_t *tte_ptr = &is->is_tsb[IOTSBSLOT(va, is->is_tsbsize)];
	int64_t tte;

#ifdef DIAGNOSTIC
	if (va < is->is_dvmabase || (va + PAGE_MASK) > is->is_dvmaend)
		panic("iommu_remove: va 0x%lx not in DVMA space", (u_long)va);
	if (va != trunc_page(va)) {
		printf("iommu_remove: unaligned va: %lx\n", va);
		va = trunc_page(va);
	}
#endif
	tte = *tte_ptr;

	DPRINTF(IDB_IOMMU, ("iommu_remove: va %lx TSB[%llx]@%p\n",
	    va, tte, tte_ptr));

#ifdef DIAGNOSTIC
	if ((tte & IOTTE_V) == 0) {
		printf("Removing invalid tte entry (dva %lx &tte %p "
		    "tte %llx)\n", va, tte_ptr, tte);
		extent_print(is->is_dvmamap);
		panic("IOMMU remove overwrite");
	}
#endif

	*tte_ptr = tte & ~IOTTE_V;

	/*
	 * IO operations are strongly ordered WRT each other.  It is
	 * unclear how they relate to normal memory accesses.
	 */
	__membar("#StoreStore");

	IOMMUREG_WRITE(is, iommu_flush, va);

	if (sb && (tte & IOTTE_STREAM))
		iommu_strbuf_flush(sb, va);

	/* Should we sync the iommu and stc here? */
}

/*
 * Find the physical address of a DVMA address (debug routine).
 */
paddr_t
iommu_extract(struct iommu_state *is, bus_addr_t dva)
{
	int64_t tte = 0;
	
	if (dva >= is->is_dvmabase && dva <= is->is_dvmaend)
		tte = is->is_tsb[IOTSBSLOT(dva, is->is_tsbsize)];

	return (tte & is->is_hw->ihw_dvma_pa);
}

/*
 * Lookup a TSB entry for a given DVMA (debug routine).
 */
int64_t
iommu_lookup_tte(struct iommu_state *is, bus_addr_t dva)
{
	int64_t tte = 0;
	
	if (dva >= is->is_dvmabase && dva <= is->is_dvmaend)
		tte = is->is_tsb[IOTSBSLOT(dva, is->is_tsbsize)];

	return (tte);
}

/*
 * Lookup a TSB entry at a given physical address (debug routine).
 */
int64_t
iommu_fetch_tte(struct iommu_state *is, paddr_t pa)
{
	int64_t tte = 0;
	
	if (pa >= is->is_ptsb && pa < is->is_ptsb +
	    (PAGE_SIZE << is->is_tsbsize)) 
		tte = ldxa(pa, ASI_PHYS_CACHED);

	return (tte);
}

/*
 * Fetch a TSB entry with some sanity checking.
 */
int64_t
iommu_tsb_entry(struct iommu_state *is, bus_addr_t dva)
{
	int64_t tte;

	if (dva < is->is_dvmabase || dva > is->is_dvmaend)
		panic("invalid dva: %llx", (long long)dva);

	tte = is->is_tsb[IOTSBSLOT(dva,is->is_tsbsize)];

	if ((tte & IOTTE_V) == 0)
		panic("iommu_tsb_entry: invalid entry %lx", dva);

	return (tte);
}

/*
 * Initiate and then block until an STC flush synchronization has completed.
 */
int 
iommu_strbuf_flush_done(struct iommu_map_state *ims)
{
	struct strbuf_ctl *sb = ims->ims_sb;
	struct strbuf_flush *sf = &ims->ims_flush;
	struct timeval cur, flushtimeout;
	struct timeval to = { 0, 500000 };
	u_int64_t flush;
	int timeout_started = 0;

#ifdef DIAGNOSTIC
	if (sb == NULL) {
		panic("iommu_strbuf_flush_done: invalid flush buffer");
	}
#endif

	mtx_enter(&sb->sb_mtx);

	/*
	 * Streaming buffer flushes:
	 * 
	 *   1 Tell strbuf to flush by storing va to strbuf_pgflush.
	 *   2 Store 0 in flag
	 *   3 Store pointer to flag in flushsync
	 *   4 wait till flushsync becomes 0x1
	 *
	 * If it takes more than .5 sec, something went very, very wrong.
	 */

	/*
	 * If we're reading from ASI_PHYS_CACHED, then we'll write to
	 * it too.  No need to tempt fate or learn about Si bugs or such.
	 * FreeBSD just uses normal "volatile" reads/writes...
	 */

	stxa(sf->sbf_flushpa, ASI_PHYS_CACHED, 0);

	/*
	 * Insure any previous strbuf operations are complete and that 
	 * memory is initialized before the IOMMU uses it.
	 * Is this Needed?  How are IO and memory operations ordered? 
	 */
	__membar("#StoreStore");

	bus_space_write_8(sb->sb_bustag, sb->sb_sb,
		    STRBUFREG(strbuf_flushsync), sf->sbf_flushpa);

	DPRINTF(IDB_IOMMU,
	    ("iommu_strbuf_flush_done: flush = %llx pa = %lx\n", 
		ldxa(sf->sbf_flushpa, ASI_PHYS_CACHED), sf->sbf_flushpa));

	__membar("#StoreLoad | #Lookaside");

	for(;;) {
		int i;

		/*
		 * Try to shave a few instruction cycles off the average
		 * latency by only checking the elapsed time every few
		 * fetches.
		 */
		for (i = 0; i < 1000; ++i) {
			__membar("#LoadLoad");
			/* Bypass non-coherent D$ */
			/* non-coherent...?   Huh? */
			flush = ldxa(sf->sbf_flushpa, ASI_PHYS_CACHED);

			if (flush) {
				DPRINTF(IDB_IOMMU,
				    ("iommu_strbuf_flush_done: flushed\n"));
				mtx_leave(&sb->sb_mtx);
				return (0);
			}
		}

		microtime(&cur);

		if (timeout_started) {
			if (timercmp(&cur, &flushtimeout, >))
				panic("STC timeout at %lx (%lld)",
				    sf->sbf_flushpa, flush);
		} else {
			timeradd(&cur, &to, &flushtimeout);
			
			timeout_started = 1;
	
			DPRINTF(IDB_IOMMU,
			    ("iommu_strbuf_flush_done: flush = %llx pa = %lx "
				"now=%llx:%lx until = %llx:%lx\n", 
				ldxa(sf->sbf_flushpa, ASI_PHYS_CACHED),
				sf->sbf_flushpa, cur.tv_sec, cur.tv_usec, 
				flushtimeout.tv_sec, flushtimeout.tv_usec));
		}
	}
}

/*
 * IOMMU DVMA operations, common to SBus and PCI.
 */

#define BUS_DMA_FIND_PARENT(t, fn)                                      \
        if (t->_parent == NULL)                                         \
                panic("null bus_dma parent (" #fn ")");                 \
        for (t = t->_parent; t->fn == NULL; t = t->_parent)             \
                if (t->_parent == NULL)                                 \
                        panic("no bus_dma " #fn " located");

int
iommu_dvmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0, struct strbuf_ctl *sb,
    bus_size_t size, int nsegments, bus_size_t maxsegsz, bus_size_t boundary,
    int flags, bus_dmamap_t *dmamap)
{
	int ret;
	bus_dmamap_t map;
	struct iommu_state *is = sb->sb_iommu;
	struct iommu_map_state *ims;

	BUS_DMA_FIND_PARENT(t, _dmamap_create);
	ret = (*t->_dmamap_create)(t, t0, size, nsegments, maxsegsz, boundary,
	    flags, &map);

	if (ret)
		return (ret);

	if (flags & BUS_DMA_64BIT) {
		map->_dm_cookie = is;
		*dmamap = map;
		return (0);
	}

	ims = iommu_iomap_create(atop(round_page(size)));

	if (ims == NULL) {
		bus_dmamap_destroy(t0, map);
		return (ENOMEM);
	}

	ims->ims_sb = sb;
	map->_dm_cookie = ims;

#ifdef DIAGNOSTIC
	if (ims->ims_sb == NULL)
		panic("iommu_dvmamap_create: null sb");
	if (ims->ims_sb->sb_iommu == NULL)
		panic("iommu_dvmamap_create: null iommu");
#endif
	*dmamap = map;

	return (0);
}

void
iommu_dvmamap_destroy(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map)
{
	/*
	 * The specification (man page) requires a loaded
	 * map to be unloaded before it is destroyed.
	 */
	if (map->dm_nsegs)
		bus_dmamap_unload(t0, map);

	if (!ISSET(map->_dm_flags, BUS_DMA_64BIT)) {
	        if (map->_dm_cookie)
			iommu_iomap_destroy(map->_dm_cookie);
	}
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
iommu_dvmamap_load(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    void *buf, bus_size_t buflen, struct proc *p, int flags)
{
	int err = 0;
	bus_size_t sgsize;
	u_long dvmaddr, sgstart, sgend;
	bus_size_t align, boundary;
	struct iommu_state *is;
	struct iommu_map_state *ims;
	pmap_t pmap;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	KASSERTMSG(map->dm_nsegs == 0, "map still in use");

	if (ISSET(map->_dm_flags, BUS_DMA_64BIT)) {
		unsigned long bypass;
		int i;

		is = map->_dm_cookie;
		bypass = is->is_hw->ihw_bypass;

		/* Bypass translation by the IOMMU. */

		BUS_DMA_FIND_PARENT(t, _dmamap_load);
		err = (*t->_dmamap_load)(t, t0, map, buf, buflen, p, flags);
		if (err != 0)
			return (err);

		for (i = 0; i < map->dm_nsegs; i++)
			map->dm_segs[i].ds_addr |= bypass;

		return (0);
	}

	ims = map->_dm_cookie;
	is = ims->ims_sb->sb_iommu;

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
				iommu_dvmamap_print_map(t, is, map);
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

#ifdef DEBUG
	iommu_dvmamap_validate_map(t, is, map);
#endif

	iommu_iomap_load_map(is, ims, dvmaddr, flags);

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

			err = iommu_dvmamap_append_range(t, map, pgstart,
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
#ifdef DEBUG
	iommu_dvmamap_validate_map(t, is, map);

	if (err)
		printf("**** iommu_dvmamap_load failed with error %d\n",
		    err);
	
	if (err || (iommudebug & IDB_PRINT_MAP)) {
		iommu_dvmamap_print_map(t, is, map);
#ifdef DDB
		if (iommudebug & IDB_BREAK)
			db_enter();
#endif
	}
#endif
	if (err)
		iommu_dvmamap_unload(t, t0, map);

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
iommu_dvmamap_load_raw(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	int i;
	int left;
	int err = 0;
	bus_size_t sgsize;
	bus_size_t boundary, align;
	u_long dvmaddr, sgstart, sgend;
	struct iommu_state *is;
	struct iommu_map_state *ims;

	KASSERTMSG(map->dm_nsegs == 0, "map still in use");

	/*
	 * A boundary presented to bus_dmamem_alloc() takes precedence
	 * over boundary in the map.
	 */
	if ((boundary = segs[0]._ds_boundary) == 0)
		boundary = map->_dm_boundary;

	if (ISSET(map->_dm_flags, BUS_DMA_64BIT)) {
		unsigned long bypass;

		is = map->_dm_cookie;
		bypass = is->is_hw->ihw_bypass;

		/* Bypass translation by the IOMMU. */
		for (i = 0; i < nsegs; i++) {
			err = iommu_dvmamap_insert(t, map,
			    bypass | segs[i].ds_addr, segs[i].ds_len,
			    0, boundary);
			if (err != 0) {
				map->dm_nsegs = 0;
				return (err);
			}
		}

		map->dm_mapsize = size;

		return (0);
	}

	ims = map->_dm_cookie;
	is = ims->ims_sb->sb_iommu;

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
				iommu_dvmamap_print_map(t, is, map);
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
					iommu_dvmamap_print_map(t, is, map);
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

#ifdef DEBUG
	iommu_dvmamap_validate_map(t, is, map);
#endif

	iommu_iomap_load_map(is, ims, dvmaddr, flags);

	if (segs[0]._ds_mlist)
		err = iommu_dvmamap_load_mlist(t, is, map, segs[0]._ds_mlist,
		    flags, size, boundary);
	else
		err = iommu_dvmamap_load_seg(t, is, map, segs, nsegs,
		    flags, size, boundary);

#ifdef DEBUG
	/* The map should be valid even if the load failed */
	if (iommu_dvmamap_validate_map(t, is, map)) {
		printf("load size %ld/0x%lx\n", size, size);
		if (segs[0]._ds_mlist)
			printf("mlist %p\n", segs[0]._ds_mlist);
		else  {
			long tot_len = 0;
			long clip_len = 0;
			printf("segs %p nsegs %d\n", segs, nsegs);

			left = size;
			for(i = 0; i < nsegs; i++) {
				bus_size_t len = segs[i].ds_len;
				bus_addr_t addr = segs[i].ds_addr;
				int seg_len = MIN(left, len);

				printf("addr %lx len %ld/0x%lx seg_len "
				    "%d/0x%x left %d/0x%x\n", addr,
				    len, len, seg_len, seg_len, left, left);

				left -= seg_len;
				
				clip_len += seg_len;
				tot_len += segs[i].ds_len;
			}
			printf("total length %ld/0x%lx total seg. "
			    "length %ld/0x%lx\n", tot_len, tot_len, clip_len,
			    clip_len);
		}

		if (err == 0)
			err = 1;
	}

	if (err)
		printf("**** iommu_dvmamap_load_raw failed with error %d\n",
		    err);
	
	if (err || (iommudebug & IDB_PRINT_MAP)) {
		iommu_dvmamap_print_map(t, is, map);
#ifdef DDB
		if (iommudebug & IDB_BREAK)
			db_enter();
#endif
	}
#endif
	if (err)
		iommu_dvmamap_unload(t, t0, map);

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
iommu_dvmamap_append_range(bus_dma_tag_t t, bus_dmamap_t map, paddr_t pa,
    bus_size_t length, int flags, bus_size_t boundary)
{
	struct iommu_map_state *ims = map->_dm_cookie;
	bus_addr_t sgstart = iommu_iomap_translate(ims, pa);

	return (iommu_dvmamap_insert(t, map, sgstart, length, flags, boundary));
}

int
iommu_dvmamap_insert(bus_dma_tag_t t, bus_dmamap_t map,
    bus_addr_t sgstart, bus_size_t length, int flags, bus_size_t boundary)
{
	bus_addr_t sgend = sgstart + length - 1;
	bus_addr_t bd_mask;
	bus_dma_segment_t *seg = NULL;
	int i = map->dm_nsegs;

#ifdef DIAGNOSTIC
	if (sgstart == 0 || sgstart > sgend) {
		printf("append range invalid mapping for "
		    "0x%lx - 0x%lx\n", sgstart, sgend);
		map->dm_nsegs = 0;
		return (EINVAL);
	}
#endif

#ifdef DEBUG
	if (trunc_page(sgstart) != trunc_page(sgend)) {
		printf("append range crossing page boundary! "
		    "length %ld/0x%lx sgstart %lx sgend %lx\n",
		    length, length, sgstart, sgend);
	}
#endif

	/*
	 * We will attempt to merge this range with the previous entry
	 * (if there is one).
	 */
	if (i > 0) {
		seg = &map->dm_segs[i - 1];
		if (sgstart == seg->ds_addr + seg->ds_len &&
		    length + seg->ds_len <= map->_dm_maxsegsz) {
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
iommu_dvmamap_load_seg(bus_dma_tag_t t, struct iommu_state *is,
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

			err = iommu_dvmamap_append_range(t, map, pgstart,
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
iommu_dvmamap_load_mlist(bus_dma_tag_t t, struct iommu_state *is,
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

		err = iommu_dvmamap_append_range(t, map, pa,
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
iommu_dvmamap_unload(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map)
{
	struct iommu_state *is;
	struct iommu_map_state *ims;
	bus_addr_t dvmaddr = map->_dm_dvmastart;
	bus_size_t sgsize = map->_dm_dvmasize;
	int error;

	if (ISSET(map->_dm_flags, BUS_DMA_64BIT)) {
		bus_dmamap_unload(t->_parent, map);
		return;
	}

	ims = map->_dm_cookie;
	is = ims->ims_sb->sb_iommu;

	/* Flush the iommu */
#ifdef DEBUG
	if (dvmaddr == 0) {
		printf("iommu_dvmamap_unload: No dvmastart\n");
#ifdef DDB
		if (iommudebug & IDB_BREAK)
			db_enter();
#endif
		return;
	}

	iommu_dvmamap_validate_map(t, is, map);

	if (iommudebug & IDB_PRINT_MAP)
		iommu_dvmamap_print_map(t, is, map);
#endif /* DEBUG */

	/* Remove the IOMMU entries */
	iommu_iomap_unload_map(is, ims);

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

#ifdef DEBUG
/*
 * Perform internal consistency checking on a dvmamap.
 */
int
iommu_dvmamap_validate_map(bus_dma_tag_t t, struct iommu_state *is,
    bus_dmamap_t map)
{
	int err = 0;
	int seg;

	if (trunc_page(map->_dm_dvmastart) != map->_dm_dvmastart) {
		printf("**** dvmastart address not page aligned: %lx",
			map->_dm_dvmastart);
		err = 1;
	}
	if (trunc_page(map->_dm_dvmasize) != map->_dm_dvmasize) {
		printf("**** dvmasize not a multiple of page size: %lx",
			map->_dm_dvmasize);
		err = 1;
	}
	if (map->_dm_dvmastart < is->is_dvmabase ||
	    (round_page(map->_dm_dvmastart + map->_dm_dvmasize) - 1) >
	    is->is_dvmaend) {
		printf("dvmaddr %lx len %lx out of range %x - %x\n",
			    map->_dm_dvmastart, map->_dm_dvmasize,
			    is->is_dvmabase, is->is_dvmaend);
		err = 1;
	}
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		if (map->dm_segs[seg].ds_addr == 0 ||
		    map->dm_segs[seg].ds_len == 0) {
			printf("seg %d null segment dvmaddr %lx len %lx for "
			    "range %lx len %lx\n",
			    seg,
			    map->dm_segs[seg].ds_addr,
			    map->dm_segs[seg].ds_len,
			    map->_dm_dvmastart, map->_dm_dvmasize);
			err = 1;
		} else if (map->dm_segs[seg].ds_addr < map->_dm_dvmastart ||
		    round_page(map->dm_segs[seg].ds_addr +
			map->dm_segs[seg].ds_len) >
		    map->_dm_dvmastart + map->_dm_dvmasize) {
			printf("seg %d dvmaddr %lx len %lx out of "
			    "range %lx len %lx\n",
			    seg,
			    map->dm_segs[seg].ds_addr,
			    map->dm_segs[seg].ds_len,
			    map->_dm_dvmastart, map->_dm_dvmasize);
			err = 1;
		}
	}

	if (err) {
		iommu_dvmamap_print_map(t, is, map);
#if defined(DDB) && defined(DEBUG)
		if (iommudebug & IDB_BREAK)
			db_enter();
#endif
	}

	return (err);
}
#endif /* DEBUG */

void
iommu_dvmamap_print_map(bus_dma_tag_t t, struct iommu_state *is,
    bus_dmamap_t map)
{
	int seg, i;
	long full_len, source_len;
	struct mbuf *m;

	printf("DVMA %x for %x, mapping %p: dvstart %lx dvsize %lx "
	    "size %ld/%lx maxsegsz %lx boundary %lx segcnt %d "
	    "flags %x type %d source %p "
	    "cookie %p mapsize %lx nsegs %d\n",
	    is ? is->is_dvmabase : 0, is ? is->is_dvmaend : 0, map,
	    map->_dm_dvmastart, map->_dm_dvmasize,
	    map->_dm_size, map->_dm_size, map->_dm_maxsegsz, map->_dm_boundary,
	    map->_dm_segcnt, map->_dm_flags, map->_dm_type,
	    map->_dm_source, map->_dm_cookie, map->dm_mapsize,
	    map->dm_nsegs);

	full_len = 0;
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		printf("seg %d dvmaddr %lx pa %lx len %lx (tte %llx)\n",
		    seg, map->dm_segs[seg].ds_addr,
		    is ? iommu_extract(is, map->dm_segs[seg].ds_addr) : 0,
		    map->dm_segs[seg].ds_len,
		    is ? iommu_lookup_tte(is, map->dm_segs[seg].ds_addr) : 0);
		full_len += map->dm_segs[seg].ds_len;
	}
	printf("total length = %ld/0x%lx\n", full_len, full_len);

	if (map->_dm_source) switch (map->_dm_type) {
	case _DM_TYPE_MBUF:
		m = map->_dm_source;
		if (m->m_flags & M_PKTHDR)
			printf("source PKTHDR mbuf (%p) hdr len = %d/0x%x:\n",
			    m, m->m_pkthdr.len, m->m_pkthdr.len);
		else
			printf("source mbuf (%p):\n", m);

		source_len = 0;
		for ( ; m; m = m->m_next) {
			vaddr_t vaddr = mtod(m, vaddr_t);
			long len = m->m_len;
			paddr_t pa;

			if (pmap_extract(pmap_kernel(), vaddr, &pa))
				printf("kva %lx pa %lx len %ld/0x%lx\n",
				    vaddr, pa, len, len);
			else
				printf("kva %lx pa <invalid> len %ld/0x%lx\n",
				    vaddr, len, len);

			source_len += len;
		}

		if (full_len != source_len)
			printf("mbuf length %ld/0x%lx is %s than mapping "
			    "length %ld/0x%lx\n", source_len, source_len,
			    (source_len > full_len) ? "greater" : "less",
			    full_len, full_len);
		else
			printf("mbuf length %ld/0x%lx\n", source_len,
			    source_len);
		break;
	case _DM_TYPE_LOAD:
	case _DM_TYPE_SEGS:
	case _DM_TYPE_UIO:
	default:
		break;
	}

	if (!ISSET(map->_dm_flags, BUS_DMA_64BIT) && map->_dm_cookie != NULL) {
		struct iommu_map_state *ims = map->_dm_cookie;
		struct iommu_page_map *ipm = &ims->ims_map;

		printf("page map (%p) of size %d with %d entries\n",
		    ipm, ipm->ipm_maxpage, ipm->ipm_pagecnt);
		for (i = 0; i < ipm->ipm_pagecnt; ++i) {
			struct iommu_page_entry *e = &ipm->ipm_map[i];
			printf("%d: vmaddr 0x%lx pa 0x%lx\n", i,
			    e->ipe_va, e->ipe_pa);
		}
	} else
		printf("iommu map state (cookie) is NULL\n");
}

void
_iommu_dvmamap_sync(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
	bus_addr_t offset, bus_size_t len, int ops)
{
	struct iommu_state *is;
	struct iommu_map_state *ims = map->_dm_cookie;
	struct strbuf_ctl *sb;
	bus_size_t count;
	int i, needsflush = 0;

	sb = ims->ims_sb;
	is = sb->sb_iommu;

	for (i = 0; i < map->dm_nsegs; i++) {
		if (offset < map->dm_segs[i].ds_len)
			break;
		offset -= map->dm_segs[i].ds_len;
	}

	if (i == map->dm_nsegs)
		panic("iommu_dvmamap_sync: too short %lu", offset);

	for (; len > 0 && i < map->dm_nsegs; i++) {
		count = MIN(map->dm_segs[i].ds_len - offset, len);
		if (count > 0 && iommu_dvmamap_sync_range(sb,
		    map->dm_segs[i].ds_addr + offset, count))
			needsflush = 1;
		len -= count;
	}

#ifdef DIAGNOSTIC
	if (i == map->dm_nsegs && len > 0)
		panic("iommu_dvmamap_sync: leftover %lu", len);
#endif

	if (needsflush)
		iommu_strbuf_flush_done(ims);
}

void
iommu_dvmamap_sync(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    bus_addr_t offset, bus_size_t len, int ops)
{
	struct iommu_map_state *ims;

	if (len == 0)
		return;

	if (map->_dm_flags & BUS_DMA_64BIT) {
		if (ops & (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_POSTREAD))
			__membar("#MemIssue");
		return;
	}

	ims = map->_dm_cookie;

	if (ops & BUS_DMASYNC_PREWRITE)
		__membar("#MemIssue");

	if ((ims->ims_flags & IOMMU_MAP_STREAM) &&
	    (ops & (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_PREWRITE)))
		_iommu_dvmamap_sync(t, t0, map, offset, len, ops);

	if (ops & BUS_DMASYNC_POSTREAD)
		__membar("#MemIssue");
}

/*
 * Flush an individual dma segment, returns non-zero if the streaming buffers
 * need flushing afterwards.
 */
int
iommu_dvmamap_sync_range(struct strbuf_ctl *sb, bus_addr_t va, bus_size_t len)
{
	bus_addr_t vaend;
#ifdef DIAGNOSTIC
	struct iommu_state *is = sb->sb_iommu;

	if (va < is->is_dvmabase || va > is->is_dvmaend)
		panic("invalid va: %llx", (long long)va);

	if ((is->is_tsb[IOTSBSLOT(va, is->is_tsbsize)] & IOTTE_STREAM) == 0) {
		printf("iommu_dvmamap_sync_range: attempting to flush "
		    "non-streaming entry\n");
		return (0);
	}
#endif

	vaend = (va + len + PAGE_MASK) & ~PAGE_MASK;
	va &= ~PAGE_MASK;

#ifdef DIAGNOSTIC
	if (va < is->is_dvmabase || (vaend - 1) > is->is_dvmaend)
		panic("invalid va range: %llx to %llx (%x to %x)",
		    (long long)va, (long long)vaend,
		    is->is_dvmabase,
		    is->is_dvmaend);
#endif

	for ( ; va <= vaend; va += PAGE_SIZE) {
		DPRINTF(IDB_BUSDMA,
		    ("iommu_dvmamap_sync_range: flushing va %p\n",
		    (void *)(u_long)va));
		iommu_strbuf_flush(sb, va);
	}

	return (1);
}

int
iommu_dvmamem_alloc(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    bus_size_t alignment, bus_size_t boundary, bus_dma_segment_t *segs,
    int nsegs, int *rsegs, int flags)
{

	DPRINTF(IDB_BUSDMA, ("iommu_dvmamem_alloc: sz %llx align %llx "
	    "bound %llx segp %p flags %d\n", (unsigned long long)size,
	    (unsigned long long)alignment, (unsigned long long)boundary,
	    segs, flags));

	if ((flags & BUS_DMA_64BIT) == 0)
		flags |= BUS_DMA_DVMA;

	BUS_DMA_FIND_PARENT(t, _dmamem_alloc);
	return ((*t->_dmamem_alloc)(t, t0, size, alignment, boundary,
	    segs, nsegs, rsegs, flags));
}

void
iommu_dvmamem_free(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dma_segment_t *segs,
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
iommu_iomap_create(int n)
{
	struct iommu_map_state *ims;
	struct strbuf_flush *sbf;
	vaddr_t va;

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

	/* Initialize the flush area. */
	sbf = &ims->ims_flush;
	va = (vaddr_t)&sbf->sbf_area[0x40];
	va &= ~0x3f;
	pmap_extract(pmap_kernel(), va, &sbf->sbf_flushpa);
	sbf->sbf_flush = (void *)va;

	return (ims);
}

/*
 * Destroy an iomap.
 */
void
iommu_iomap_destroy(struct iommu_map_state *ims)
{
#ifdef DIAGNOSTIC
	if (ims->ims_map.ipm_pagecnt > 0)
		printf("iommu_iomap_destroy: %d page entries in use\n",
		    ims->ims_map.ipm_pagecnt);
#endif

	free(ims, M_DEVBUF, 0);
}

/*
 * Utility function used by splay tree to order page entries by pa.
 */
static inline int
iomap_compare(struct iommu_page_entry *a, struct iommu_page_entry *b)
{
	return ((a->ipe_pa > b->ipe_pa) ? 1 :
		(a->ipe_pa < b->ipe_pa) ? -1 : 0);
}

SPLAY_PROTOTYPE(iommu_page_tree, iommu_page_entry, ipe_node, iomap_compare);

SPLAY_GENERATE(iommu_page_tree, iommu_page_entry, ipe_node, iomap_compare);

/*
 * Insert a pa entry in the iomap.
 */
int
iommu_iomap_insert_page(struct iommu_map_state *ims, paddr_t pa)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;

	if (ipm->ipm_pagecnt >= ipm->ipm_maxpage) {
		struct iommu_page_entry ipe;

		ipe.ipe_pa = pa;
		if (SPLAY_FIND(iommu_page_tree, &ipm->ipm_tree, &ipe))
			return (0);

		return (ENOMEM);
	}

	e = &ipm->ipm_map[ipm->ipm_pagecnt];

	e->ipe_pa = pa;
	e->ipe_va = 0;

	e = SPLAY_INSERT(iommu_page_tree, &ipm->ipm_tree, e);

	/* Duplicates are okay, but only count them once. */
	if (e)
		return (0);

	++ipm->ipm_pagecnt;

	return (0);
}

/*
 * Locate the iomap by filling in the pa->va mapping and inserting it
 * into the IOMMU tables.
 */
void
iommu_iomap_load_map(struct iommu_state *is, struct iommu_map_state *ims,
    bus_addr_t vmaddr, int flags)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	struct strbuf_ctl *sb = ims->ims_sb;
	int i, slot;

	if (sb->sb_flush == NULL)
		flags &= ~BUS_DMA_STREAMING;

	if (flags & BUS_DMA_STREAMING)
		ims->ims_flags |= IOMMU_MAP_STREAM;
	else
		ims->ims_flags &= ~IOMMU_MAP_STREAM;

	for (i = 0, e = ipm->ipm_map; i < ipm->ipm_pagecnt; ++i, ++e) {
		e->ipe_va = vmaddr;
		iommu_enter(is, sb, e->ipe_va, e->ipe_pa, flags);

		/* Flush cache if necessary. */
		slot = IOTSBSLOT(e->ipe_va, is->is_tsbsize);
		if (ISSET(is->is_hw->ihw_flags, IOMMU_HW_FLUSH_CACHE) &&
		    (i == (ipm->ipm_pagecnt - 1) || (slot % 8) == 7))
			IOMMUREG_WRITE(is, iommu_cache_flush,
			    is->is_ptsb + slot * 8);

		vmaddr += PAGE_SIZE;
	}
}

/*
 * Remove the iomap from the IOMMU.
 */
void
iommu_iomap_unload_map(struct iommu_state *is, struct iommu_map_state *ims)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	struct strbuf_ctl *sb = ims->ims_sb;
	int i, slot;

	for (i = 0, e = ipm->ipm_map; i < ipm->ipm_pagecnt; ++i, ++e) {
		iommu_remove(is, sb, e->ipe_va);

		/* Flush cache if necessary. */
		slot = IOTSBSLOT(e->ipe_va, is->is_tsbsize);
		if (ISSET(is->is_hw->ihw_flags, IOMMU_HW_FLUSH_CACHE) &&
		    (i == (ipm->ipm_pagecnt - 1) || (slot % 8) == 7))
			IOMMUREG_WRITE(is, iommu_cache_flush,
			    is->is_ptsb + slot * 8);
	}
}

/*
 * Translate a physical address (pa) into a DVMA address.
 */
bus_addr_t
iommu_iomap_translate(struct iommu_map_state *ims, paddr_t pa)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	struct iommu_page_entry pe;
	paddr_t offset = pa & PAGE_MASK;

	pe.ipe_pa = trunc_page(pa);

	e = SPLAY_FIND(iommu_page_tree, &ipm->ipm_tree, &pe);

	if (e == NULL)
		return (0);

	return (e->ipe_va | offset);
}

/*
 * Clear the iomap table and tree.
 */
void
iommu_iomap_clear_pages(struct iommu_map_state *ims)
{
	ims->ims_map.ipm_pagecnt = 0;
	SPLAY_INIT(&ims->ims_map.ipm_tree);
}

