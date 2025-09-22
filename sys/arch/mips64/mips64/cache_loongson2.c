/*	$OpenBSD: cache_loongson2.c,v 1.8 2021/03/11 11:16:59 jsg Exp $	*/

/*
 * Copyright (c) 2009, 2012 Miodrag Vallat.
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
 * Cache handling code for Loongson 2E and 2F processors.
 * This code could be made to work on 2C by not hardcoding the number of
 * cache ways.
 *
 * 2E and 2F caches are :
 * - L1 I$ is 4-way, VIPT, 32 bytes/line, 64KB total
 * - L1 D$ is 4-way, VIPT, write-back, 32 bytes/line, 64KB total
 * - L2 is 4-way, PIPT, write-back, 32 bytes/line, 512KB total
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <mips64/cache.h>
#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

/* L1 cache operations */
#define	IndexInvalidate_I	0x00
#define	IndexWBInvalidate_D	0x01
#define	IndexLoadTag_D		0x05
#define	IndexStoreTag_D		0x09
#define	HitInvalidate_D		0x11
#define	HitWBInvalidate_D	0x15
#define	IndexLoadData_D		0x19
#define	IndexStoreData_D	0x1d

/* L2 cache operations */
#define	IndexWBInvalidate_S	0x03
#define	IndexLoadTag_S		0x07
#define	IndexStoreTag_S		0x0b
#define	HitInvalidate_S		0x13
#define	HitWBInvalidate_S	0x17
#define	IndexLoadData_S		0x1b
#define	IndexStoreData_S	0x1f

#define	cache(op,set,addr) \
    __asm__ volatile \
      ("cache %0, %1(%2)" :: "i"(op), "i"(set), "r"(addr) : "memory")

static __inline__ void	ls2f_hitinv_primary(vaddr_t, vsize_t);
static __inline__ void	ls2f_hitinv_secondary(vaddr_t, vsize_t);
static __inline__ void	ls2f_hitwbinv_primary(vaddr_t, vsize_t);
static __inline__ void	ls2f_hitwbinv_secondary(vaddr_t, vsize_t);

#define	LS2F_CACHE_LINE	32UL
#define	LS2F_CACHE_WAYS	4UL
#define	LS2F_L1_SIZE		(64UL * 1024UL)
#define	LS2F_L2_SIZE		(512UL * 1024UL)

void
Loongson2_ConfigCache(struct cpu_info *ci)
{
	ci->ci_l1inst.size = LS2F_L1_SIZE;
	ci->ci_l1inst.linesize = LS2F_CACHE_LINE;
	ci->ci_l1inst.setsize = LS2F_L1_SIZE / LS2F_CACHE_WAYS;
	ci->ci_l1inst.sets = LS2F_CACHE_WAYS;

	ci->ci_l1data.size = LS2F_L1_SIZE;
	ci->ci_l1data.linesize = LS2F_CACHE_LINE;
	ci->ci_l1data.setsize = LS2F_L1_SIZE / LS2F_CACHE_WAYS;
	ci->ci_l1data.sets = LS2F_CACHE_WAYS;

	ci->ci_l2.size = LS2F_L2_SIZE;
	ci->ci_l2.linesize = LS2F_CACHE_LINE;
	ci->ci_l2.setsize = LS2F_L2_SIZE / LS2F_CACHE_WAYS;
	ci->ci_l2.sets = LS2F_CACHE_WAYS;

	memset(&ci->ci_l3, 0, sizeof(struct cache_info));

	cache_valias_mask = ci->ci_l1inst.setsize & ~PAGE_MASK;

	/* should not happen as we use 16KB pages */
	if (cache_valias_mask != 0) {
		cache_valias_mask |= PAGE_MASK;
		pmap_prefer_mask |= cache_valias_mask;
	}

	ci->ci_SyncCache = Loongson2_SyncCache;
	ci->ci_InvalidateICache = Loongson2_InvalidateICache;
	ci->ci_InvalidateICachePage = Loongson2_InvalidateICachePage;
	ci->ci_SyncICache = Loongson2_SyncICache;
	ci->ci_SyncDCachePage = Loongson2_SyncDCachePage;
	ci->ci_HitSyncDCachePage = Loongson2_SyncDCachePage;
	ci->ci_HitSyncDCache = Loongson2_HitSyncDCache;
	ci->ci_HitInvalidateDCache = Loongson2_HitInvalidateDCache;
	ci->ci_IOSyncDCache = Loongson2_IOSyncDCache;
}

/*
 * Writeback and invalidate all caches.
 */
void
Loongson2_SyncCache(struct cpu_info *ci)
{
	vaddr_t sva, eva;

	mips_sync();

	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	eva = sva + LS2F_L1_SIZE / LS2F_CACHE_WAYS;
	while (sva != eva) {
		cache(IndexInvalidate_I, 0, sva);
		sva += LS2F_CACHE_LINE;
	}

	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	eva = sva + LS2F_L1_SIZE / LS2F_CACHE_WAYS;
	while (sva != eva) {
		cache(IndexWBInvalidate_D, 0, sva);
		cache(IndexWBInvalidate_D, 1, sva);
		cache(IndexWBInvalidate_D, 2, sva);
		cache(IndexWBInvalidate_D, 3, sva);
		sva += LS2F_CACHE_LINE;
	}

	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	eva = sva + LS2F_L2_SIZE / LS2F_CACHE_WAYS;
	while (sva != eva) {
		cache(IndexWBInvalidate_S, 0, sva);
		cache(IndexWBInvalidate_S, 1, sva);
		cache(IndexWBInvalidate_S, 2, sva);
		cache(IndexWBInvalidate_S, 3, sva);
		sva += LS2F_CACHE_LINE;
	}
}

/*
 * Invalidate I$ for the given range.
 */
void
Loongson2_InvalidateICache(struct cpu_info *ci, vaddr_t _va, size_t _sz)
{
	vaddr_t va, sva, eva;
	vsize_t sz;

	/* extend the range to integral cache lines */
	va = _va & ~(LS2F_CACHE_LINE - 1);
	sz = ((_va + _sz + LS2F_CACHE_LINE - 1) & ~(LS2F_CACHE_LINE - 1)) - va;

	sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
	/* keep only the index bits */
	sva |= va & ((1UL << 14) - 1);
	eva = sva + sz;
	while (sva != eva) {
		cache(IndexInvalidate_I, 0, sva);
		sva += LS2F_CACHE_LINE;
	}
}

/*
 * Register a given page for I$ invalidation.
 */
void
Loongson2_InvalidateICachePage(struct cpu_info *ci, vaddr_t va)
{
	/*
	 * Since the page size matches the I$ set size, and I$ maintenance
	 * operations always operate on all the sets, all we need to do here
	 * is remember there are postponed flushes.
	 */
	ci->ci_cachepending_l1i = 1;
}

/*
 * Perform postponed I$ invalidation.
 */
void
Loongson2_SyncICache(struct cpu_info *ci)
{
	vaddr_t sva, eva;

	if (ci->ci_cachepending_l1i != 0) {
		/* inline Loongson2_InvalidateICache(ci, 0, PAGE_SIZE); */
		sva = PHYS_TO_XKPHYS(0, CCA_CACHED);
		eva = sva + PAGE_SIZE;
		while (sva != eva) {
			cache(IndexInvalidate_I, 0, sva);
			sva += LS2F_CACHE_LINE;
		}

		ci->ci_cachepending_l1i = 0;
	}
}

/*
 * Writeback D$ for the given page.
 *
 * The index for L1 is the low 14 bits of the virtual address. Since the
 * page size is 2**14 bytes, it is possible to access the page through
 * any valid address.
 */
void
Loongson2_SyncDCachePage(struct cpu_info *ci, vaddr_t va, paddr_t pa)
{
	vaddr_t sva, eva;

	mips_sync();

	sva = PHYS_TO_XKPHYS(pa, CCA_CACHED);
	eva = sva + PAGE_SIZE;
	for (va = sva; va != eva; va += LS2F_CACHE_LINE)
		cache(HitWBInvalidate_D, 0, va);
	for (va = sva; va != eva; va += LS2F_CACHE_LINE)
		cache(HitWBInvalidate_S, 0, va);
}

/*
 * Writeback D$ for the given range. Range is expected to be currently
 * mapped, allowing the use of `Hit' operations. This is less aggressive
 * than using `Index' operations.
 */

static __inline__ void
ls2f_hitwbinv_primary(vaddr_t va, vsize_t sz)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitWBInvalidate_D, 0, va);
		va += LS2F_CACHE_LINE;
	}
}

static __inline__ void
ls2f_hitwbinv_secondary(vaddr_t va, vsize_t sz)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitWBInvalidate_S, 0, va);
		va += LS2F_CACHE_LINE;
	}
}

void
Loongson2_HitSyncDCache(struct cpu_info *ci, vaddr_t _va, size_t _sz)
{
	vaddr_t va;
	vsize_t sz;

	mips_sync();

	/* extend the range to integral cache lines */
	va = _va & ~(LS2F_CACHE_LINE - 1);
	sz = ((_va + _sz + LS2F_CACHE_LINE - 1) & ~(LS2F_CACHE_LINE - 1)) - va;

	ls2f_hitwbinv_primary(va, sz);
	ls2f_hitwbinv_secondary(va, sz);
}

/*
 * Invalidate D$ for the given range. Range is expected to be currently
 * mapped, allowing the use of `Hit' operations. This is less aggressive
 * than using `Index' operations.
 */

static __inline__ void
ls2f_hitinv_primary(vaddr_t va, vsize_t sz)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitInvalidate_D, 0, va);
		va += LS2F_CACHE_LINE;
	}
}

static __inline__ void
ls2f_hitinv_secondary(vaddr_t va, vsize_t sz)
{
	vaddr_t eva;

	eva = va + sz;
	while (va != eva) {
		cache(HitInvalidate_S, 0, va);
		va += LS2F_CACHE_LINE;
	}
}

void
Loongson2_HitInvalidateDCache(struct cpu_info *ci, vaddr_t _va, size_t _sz)
{
	vaddr_t va;
	vsize_t sz;

	/* extend the range to integral cache lines */
	va = _va & ~(LS2F_CACHE_LINE - 1);
	sz = ((_va + _sz + LS2F_CACHE_LINE - 1) & ~(LS2F_CACHE_LINE - 1)) - va;

	ls2f_hitinv_primary(va, sz);
	ls2f_hitinv_secondary(va, sz);

	mips_sync();
}

/*
 * Backend for bus_dmamap_sync(). Enforce coherency of the given range
 * by performing the necessary cache writeback and/or invalidate
 * operations.
 */
void
Loongson2_IOSyncDCache(struct cpu_info *ci, vaddr_t _va, size_t _sz, int how)
{
	vaddr_t va;
	vsize_t sz;
	int partial_start, partial_end;

	/* extend the range to integral cache lines */
	va = _va & ~(LS2F_CACHE_LINE - 1);
	sz = ((_va + _sz + LS2F_CACHE_LINE - 1) & ~(LS2F_CACHE_LINE - 1)) - va;

	switch (how) {
	case CACHE_SYNC_R:
		/* writeback partial cachelines */
		if (((_va | _sz) & (LS2F_CACHE_LINE - 1)) != 0) {
			partial_start = va != _va;
			partial_end = va + sz != _va + _sz;
		} else {
			partial_start = partial_end = 0;
		}
		if (partial_start) {
			cache(HitWBInvalidate_D, 0, va);
			cache(HitWBInvalidate_S, 0, va);
			va += LS2F_CACHE_LINE;
			sz -= LS2F_CACHE_LINE;
		}
		if (sz != 0 && partial_end) {
			cache(HitWBInvalidate_D, 0, va + sz - LS2F_CACHE_LINE);
			cache(HitWBInvalidate_S, 0, va + sz - LS2F_CACHE_LINE);
			sz -= LS2F_CACHE_LINE;
		}
		ls2f_hitinv_primary(va, sz);
		ls2f_hitinv_secondary(va, sz);
		break;
	case CACHE_SYNC_X:
	case CACHE_SYNC_W:
		ls2f_hitwbinv_primary(va, sz);
		ls2f_hitwbinv_secondary(va, sz);
		break;
	}
}
