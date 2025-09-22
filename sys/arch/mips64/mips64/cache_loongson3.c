/*	$OpenBSD: cache_loongson3.c,v 1.2 2016/01/05 05:27:54 visa Exp $	*/

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
 * Cache handling code for Loongson 3A and compatible processors
 * (including Loongson 2Gq)
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <mips64/cache.h>
#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

void
Loongson3_ConfigCache(struct cpu_info *ci)
{
	mips64r2_ConfigCache(ci);

	ci->ci_SyncCache = Loongson3_SyncCache;
	ci->ci_InvalidateICache = Loongson3_InvalidateICache;
	ci->ci_InvalidateICachePage = Loongson3_InvalidateICachePage;
	ci->ci_SyncICache = Loongson3_SyncICache;
	ci->ci_SyncDCachePage = Loongson3_SyncDCachePage;
	ci->ci_HitSyncDCachePage = Loongson3_SyncDCachePage;
	ci->ci_HitSyncDCache = Loongson3_HitSyncDCache;
	ci->ci_HitInvalidateDCache = Loongson3_HitInvalidateDCache;
	ci->ci_IOSyncDCache = Loongson3_IOSyncDCache;
}

/*
 * Writeback and invalidate all caches.
 */
void
Loongson3_SyncCache(struct cpu_info *ci)
{
	mips_sync();
}

/*
 * Invalidate I$ for the given range.
 */
void
Loongson3_InvalidateICache(struct cpu_info *ci, vaddr_t va, size_t sz)
{
	/* nothing to do */
}

/*
 * Register a given page for I$ invalidation.
 */
void
Loongson3_InvalidateICachePage(struct cpu_info *ci, vaddr_t va)
{
	/* nothing to do */
}

/*
 * Perform postponed I$ invalidation.
 */
void
Loongson3_SyncICache(struct cpu_info *ci)
{
	/* nothing to do */
}

/*
 * Writeback D$ for the given page.
 */
void
Loongson3_SyncDCachePage(struct cpu_info *ci, vaddr_t va, paddr_t pa)
{
	/* nothing to do */
}

/*
 * Writeback D$ for the given range. Range is expected to be currently
 * mapped, allowing the use of `Hit' operations. This is less aggressive
 * than using `Index' operations.
 */

void
Loongson3_HitSyncDCache(struct cpu_info *ci, vaddr_t va, size_t sz)
{
	/* nothing to do */
}

/*
 * Invalidate D$ for the given range. Range is expected to be currently
 * mapped, allowing the use of `Hit' operations. This is less aggressive
 * than using `Index' operations.
 */

void
Loongson3_HitInvalidateDCache(struct cpu_info *ci, vaddr_t va, size_t sz)
{
	/* nothing to do */
}

/*
 * Backend for bus_dmamap_sync(). Enforce coherency of the given range
 * by performing the necessary cache writeback and/or invalidate
 * operations.
 */
void
Loongson3_IOSyncDCache(struct cpu_info *ci, vaddr_t va, size_t sz, int how)
{
	switch (how) {
	case CACHE_SYNC_R:
		break;
	case CACHE_SYNC_X:
	case CACHE_SYNC_W:
		mips_sync();	/* XXX necessary? */
		break;
	}
}
