/*	$OpenBSD: cache_mips64r2.c,v 1.4 2022/08/29 02:08:13 jsg Exp $	*/

/*
 * Copyright (c) 2014 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Cache handling code for mips64r2 compatible processors
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <mips64/cache.h>
#include <mips64/mips_cpu.h>
#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

#define	IndexInvalidate_I	0x00
#define	IndexWBInvalidate_D	0x01
#define	IndexWBInvalidate_T	0x02
#define	IndexWBInvalidate_S	0x03

#define	HitInvalidate_D		0x11
#define	HitInvalidate_T		0x12
#define	HitInvalidate_S		0x13

#define	HitWBInvalidate_D	0x15
#define	HitWBInvalidate_T	0x16
#define	HitWBInvalidate_S	0x17

#define	cache(op,addr) \
    __asm__ volatile \
      ("cache %0, 0(%1)" :: "i"(op), "r"(addr) : "memory")

static __inline__ void	mips64r2_hitinv_primary(vaddr_t, vsize_t, vsize_t);
static __inline__ void	mips64r2_hitinv_secondary(vaddr_t, vsize_t, vsize_t);
static __inline__ void	mips64r2_hitinv_ternary(vaddr_t, vsize_t, vsize_t);
static __inline__ void	mips64r2_hitwbinv_primary(vaddr_t, vsize_t, vsize_t);
static __inline__ void	mips64r2_hitwbinv_secondary(vaddr_t, vsize_t, vsize_t);
static __inline__ void	mips64r2_hitwbinv_ternary(vaddr_t, vsize_t, vsize_t);

void
mips64r2_ConfigCache(struct cpu_info *ci)
{
	uint32_t cfg, valias_mask;
	uint32_t s, l, a;

	cfg = cp0_get_config();
	if ((cfg & 0x80000000) == 0)
		panic("no M bit in cfg0.0");

	cfg = cp0_get_config_1();

	a = 1 + ((cfg & CONFIG1_DA) >> CONFIG1_DA_SHIFT);
	l = (cfg & CONFIG1_DL) >> CONFIG1_DL_SHIFT;
	s = (cfg & CONFIG1_DS) >> CONFIG1_DS_SHIFT;
	ci->ci_l1data.linesize = 2 << l;
	ci->ci_l1data.setsize = (64 << s) * ci->ci_l1data.linesize;
	ci->ci_l1data.sets = a;
	ci->ci_l1data.size = ci->ci_l1data.sets * ci->ci_l1data.setsize;

	a = 1 + ((cfg & CONFIG1_IA) >> CONFIG1_IA_SHIFT);
	l = (cfg & CONFIG1_IL) >> CONFIG1_IL_SHIFT;
	s = (cfg & CONFIG1_IS) >> CONFIG1_IS_SHIFT;
	ci->ci_l1inst.linesize = 2 << l;
	ci->ci_l1inst.setsize = (64 << s) * ci->ci_l1inst.linesize;
	ci->ci_l1inst.sets = a;
	ci->ci_l1inst.size = ci->ci_l1inst.sets * ci->ci_l1inst.setsize;

	memset(&ci->ci_l2, 0, sizeof(struct cache_info));
	memset(&ci->ci_l3, 0, sizeof(struct cache_info));

	if ((cfg & 0x80000000) != 0) {
		cfg = cp0_get_config_2();

		a = 1 + ((cfg >> 0) & 0x0f);
		l = (cfg >> 4) & 0x0f;
		s = (cfg >> 8) & 0x0f;
		if (l != 0) {
			ci->ci_l2.linesize = 2 << l;
			ci->ci_l2.setsize = (64 << s) * ci->ci_l2.linesize;
			ci->ci_l2.sets = a;
			ci->ci_l2.size = ci->ci_l2.sets * ci->ci_l2.setsize;
		}

		a = 1 + ((cfg >> 16) & 0x0f);
		l = (cfg >> 20) & 0x0f;
		s = (cfg >> 24) & 0x0f;
		if (l != 0) {
			ci->ci_l3.linesize = 2 << l;
			ci->ci_l3.setsize = (64 << s) * ci->ci_l3.linesize;
			ci->ci_l3.sets = a;
			ci->ci_l3.size = ci->ci_l3.sets * ci->ci_l3.setsize;
		}
	}

	valias_mask = (max(ci->ci_l1inst.setsize, ci->ci_l1data.setsize) - 1) &
	    ~PAGE_MASK;

	if (valias_mask != 0) {
		valias_mask |= PAGE_MASK;
#ifdef MULTIPROCESSOR
		if (valias_mask > cache_valias_mask) {
#endif
			cache_valias_mask = valias_mask;
			pmap_prefer_mask = valias_mask;
#ifdef MULTIPROCESSOR
		}
#endif
	}

	ci->ci_SyncCache = mips64r2_SyncCache;
	ci->ci_InvalidateICache = mips64r2_InvalidateICache;
	ci->ci_InvalidateICachePage = mips64r2_InvalidateICachePage;
	ci->ci_SyncICache = mips64r2_SyncICache;
	ci->ci_SyncDCachePage = mips64r2_SyncDCachePage;
	ci->ci_HitSyncDCachePage = mips64r2_HitSyncDCachePage;
	ci->ci_HitSyncDCache = mips64r2_HitSyncDCache;
	ci->ci_HitInvalidateDCache = mips64r2_HitInvalidateDCache;
	ci->ci_IOSyncDCache = mips64r2_IOSyncDCache;
}

static __inline__ void
mips64r2_hitwbinv_primary(vaddr_t va, vsize_t sz, vsize_t line)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitWBInvalidate_D, va);
		va += line;
	}
}

static __inline__ void
mips64r2_hitwbinv_secondary(vaddr_t va, vsize_t sz, vsize_t line)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitWBInvalidate_S, va);
		va += line;
	}
}

static __inline__ void
mips64r2_hitwbinv_ternary(vaddr_t va, vsize_t sz, vsize_t line)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitWBInvalidate_T, va);
		va += line;
	}
}

static __inline__ void
mips64r2_hitinv_primary(vaddr_t va, vsize_t sz, vsize_t line)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitInvalidate_D, va);
		va += line;
	}
}

static __inline__ void
mips64r2_hitinv_secondary(vaddr_t va, vsize_t sz, vsize_t line)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitInvalidate_S, va);
		va += line;
	}
}

static __inline__ void
mips64r2_hitinv_ternary(vaddr_t va, vsize_t sz, vsize_t line)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitInvalidate_T, va);
		va += line;
	}
}

/*
 * Writeback and invalidate all caches.
 */
void
mips64r2_SyncCache(struct cpu_info *ci)
{
	vaddr_t sva, eva;

	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	eva = sva + ci->ci_l1inst.linesize;
	while (sva != eva) {
		cache(IndexInvalidate_I, sva);
		sva += ci->ci_l1inst.linesize;
	}

	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	eva = sva + ci->ci_l1data.linesize;
	while (sva != eva) {
		cache(IndexWBInvalidate_D, sva);
		sva += ci->ci_l1data.linesize;
	}

	if (ci->ci_l2.size != 0) {
		sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
		eva = sva + ci->ci_l2.size;
		while (sva != eva) {
			cache(IndexWBInvalidate_S, sva);
			sva += ci->ci_l2.linesize;
		}
	}

	if (ci->ci_l3.size != 0) {
		sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
		eva = sva + ci->ci_l3.size;
		while (sva != eva) {
			cache(IndexWBInvalidate_T, sva);
			sva += ci->ci_l3.linesize;
		}
	}
}

/*
 * Invalidate I$ for the given range.
 */
void
mips64r2_InvalidateICache(struct cpu_info *ci, vaddr_t _va, size_t _sz)
{
	vaddr_t va, sva, eva, iva;
	vsize_t sz, offs;
	uint set, nsets;

	/* extend the range to integral cache lines */
	va = _va & ~(ci->ci_l1inst.linesize - 1);
	sz = ((_va + _sz + ci->ci_l1inst.linesize - 1) & ~(ci->ci_l1inst.linesize - 1)) - va;

	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	offs = ci->ci_l1inst.setsize;
	nsets = ci->ci_l1inst.sets;
	/* keep only the index bits */
	sva |= va & (offs - 1);
	eva = sva + sz;

	while (sva != eva) {
		for (set = nsets, iva = sva; set != 0; set--, iva += offs)
			cache(IndexInvalidate_I, iva);
		sva += ci->ci_l1inst.linesize;
	}
}

/*
 * Register a given page for I$ invalidation.
 */
void
mips64r2_InvalidateICachePage(struct cpu_info *ci, vaddr_t va)
{
	/* this code is too generic to allow for lazy I$ invalidates, yet */
	mips64r2_InvalidateICache(ci, va, PAGE_SIZE);
}

/*
 * Perform postponed I$ invalidation.
 */
void
mips64r2_SyncICache(struct cpu_info *ci)
{
}

/*
 * Writeback D$ for the given page.
 */
void
mips64r2_SyncDCachePage(struct cpu_info *ci, vaddr_t va, paddr_t pa)
{
	vaddr_t sva, eva, iva;
	vsize_t line, offs;
	uint set, nsets;

	line = ci->ci_l1data.linesize;
	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	offs = ci->ci_l1data.setsize;
	nsets = ci->ci_l1data.sets;
	/* keep only the index bits */
	sva += va & (offs - 1);
	eva = sva + PAGE_SIZE;
	while (sva != eva) {
		for (set = nsets, iva = sva; set != 0; set--, iva += offs)
			cache(IndexWBInvalidate_D, iva);
		sva += ci->ci_l1data.linesize;
	}
}

/*
 * Writeback D$ for the given page, which is expected to be currently
 * mapped, allowing the use of `Hit' operations. This is less aggressive
 * than using `Index' operations.
 */

void
mips64r2_HitSyncDCachePage(struct cpu_info *ci, vaddr_t va, paddr_t pa)
{
	mips64r2_hitwbinv_primary(va, PAGE_SIZE, ci->ci_l1data.linesize);
}

/*
 * Writeback D$ for the given range. Range is expected to be currently
 * mapped, allowing the use of `Hit' operations. This is less aggressive
 * than using `Index' operations.
 */

void
mips64r2_HitSyncDCache(struct cpu_info *ci, vaddr_t _va, size_t _sz)
{
	vaddr_t va;
	vsize_t sz;

	/* extend the range to integral cache lines */
	va = _va & ~(ci->ci_l1data.linesize - 1);
	sz = ((_va + _sz + ci->ci_l1data.linesize - 1) & ~(ci->ci_l1data.linesize - 1)) - va;
	mips64r2_hitwbinv_primary(va, sz, ci->ci_l1data.linesize);
}

/*
 * Invalidate D$ for the given range. Range is expected to be currently
 * mapped, allowing the use of `Hit' operations. This is less aggressive
 * than using `Index' operations.
 */

void
mips64r2_HitInvalidateDCache(struct cpu_info *ci, vaddr_t _va, size_t _sz)
{
	vaddr_t va;
	vsize_t sz;

	/* extend the range to integral cache lines */
	va = _va & ~(ci->ci_l1data.linesize - 1);
	sz = ((_va + _sz + ci->ci_l1data.linesize - 1) & ~(ci->ci_l1data.linesize - 1)) - va;
	mips64r2_hitinv_primary(va, sz, ci->ci_l1data.linesize);
}

/*
 * Backend for bus_dmamap_sync(). Enforce coherency of the given range
 * by performing the necessary cache writeback and/or invalidate
 * operations.
 */
void
mips64r2_IOSyncDCache(struct cpu_info *ci, vaddr_t _va, size_t _sz, int how)
{
	vaddr_t va;
	vsize_t sz;
	int partial_start, partial_end;

	/*
	 * L1
	 */

	/* extend the range to integral cache lines */
	va = _va & ~(ci->ci_l1data.linesize - 1);
	sz = ((_va + _sz + ci->ci_l1data.linesize - 1) & ~(ci->ci_l1data.linesize - 1)) - va;

	switch (how) {
	case CACHE_SYNC_R:
		/* writeback partial cachelines */
		if (((_va | _sz) & (ci->ci_l1data.linesize - 1)) != 0) {
			partial_start = va != _va;
			partial_end = va + sz != _va + _sz;
		} else {
			partial_start = partial_end = 0;
		}
		if (partial_start) {
			cache(HitWBInvalidate_D, va);
			va += ci->ci_l1data.linesize;
			sz -= ci->ci_l1data.linesize;
		}
		if (sz != 0 && partial_end) {
			sz -= ci->ci_l1data.linesize;
			cache(HitWBInvalidate_D, va + sz);
		}
		if (sz != 0)
			mips64r2_hitinv_primary(va, sz, ci->ci_l1data.linesize);
		break;
	case CACHE_SYNC_X:
	case CACHE_SYNC_W:
		mips64r2_hitwbinv_primary(va, sz, ci->ci_l1data.linesize);
		break;
	}

	/*
	 * L2
	 */

	if (ci->ci_l2.size != 0) {
		/* extend the range to integral cache lines */
		va = _va & ~(ci->ci_l2.linesize - 1);
		sz = ((_va + _sz + ci->ci_l2.linesize - 1) & ~(ci->ci_l2.linesize - 1)) - va;

		switch (how) {
		case CACHE_SYNC_R:
			/* writeback partial cachelines */
			if (((_va | _sz) & (ci->ci_l2.linesize - 1)) != 0) {
				partial_start = va != _va;
				partial_end = va + sz != _va + _sz;
			} else {
				partial_start = partial_end = 0;
			}
			if (partial_start) {
				cache(HitWBInvalidate_S, va);
				va += ci->ci_l2.linesize;
				sz -= ci->ci_l2.linesize;
			}
			if (sz != 0 && partial_end) {
				sz -= ci->ci_l2.linesize;
				cache(HitWBInvalidate_S, va + sz);
			}
			if (sz != 0)
				mips64r2_hitinv_secondary(va, sz, ci->ci_l2.linesize);
			break;
		case CACHE_SYNC_X:
		case CACHE_SYNC_W:
			mips64r2_hitwbinv_secondary(va, sz, ci->ci_l2.linesize);
			break;
		}
	}

	/*
	 * L3
	 */

	if (ci->ci_l3.size != 0) {
		/* extend the range to integral cache lines */
		va = _va & ~(ci->ci_l3.linesize - 1);
		sz = ((_va + _sz + ci->ci_l3.linesize - 1) & ~(ci->ci_l3.linesize - 1)) - va;

		switch (how) {
		case CACHE_SYNC_R:
			/* writeback partial cachelines */
			if (((_va | _sz) & (ci->ci_l3.linesize - 1)) != 0) {
				partial_start = va != _va;
				partial_end = va + sz != _va + _sz;
			} else {
				partial_start = partial_end = 0;
			}
			if (partial_start) {
				cache(HitWBInvalidate_S, va);
				va += ci->ci_l3.linesize;
				sz -= ci->ci_l3.linesize;
			}
			if (sz != 0 && partial_end) {
				sz -= ci->ci_l3.linesize;
				cache(HitWBInvalidate_S, va + sz);
			}
			if (sz != 0)
				mips64r2_hitinv_ternary(va, sz, ci->ci_l3.linesize);
			break;
		case CACHE_SYNC_X:
		case CACHE_SYNC_W:
			mips64r2_hitwbinv_ternary(va, sz, ci->ci_l3.linesize);
			break;
		}
	}
}
