// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include <linux/backing-dev.h>
#include <linux/dax.h>

#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_log_recover.h"
#include "xfs_log_priv.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_ag.h"
#include "xfs_buf_mem.h"
#include "xfs_notify_failure.h"

struct kmem_cache *xfs_buf_cache;

/*
 * Locking orders
 *
 * xfs_buf_stale:
 *	b_sema (caller holds)
 *	  b_lock
 *	    lru_lock
 *
 * xfs_buf_rele:
 *	b_lock
 *	  lru_lock
 *
 * xfs_buftarg_drain_rele
 *	lru_lock
 *	  b_lock (trylock due to inversion)
 *
 * xfs_buftarg_isolate
 *	lru_lock
 *	  b_lock (trylock due to inversion)
 */

static void xfs_buf_submit(struct xfs_buf *bp);
static int xfs_buf_iowait(struct xfs_buf *bp);

static inline bool xfs_buf_is_uncached(struct xfs_buf *bp)
{
	return bp->b_rhash_key == XFS_BUF_DADDR_NULL;
}

static inline int
xfs_buf_is_vmapped(
	struct xfs_buf	*bp)
{
	/*
	 * Return true if the buffer is vmapped.
	 *
	 * b_addr is null if the buffer is not mapped, but the code is clever
	 * enough to know it doesn't have to map a single page, so the check has
	 * to be both for b_addr and bp->b_page_count > 1.
	 */
	return bp->b_addr && bp->b_page_count > 1;
}

static inline int
xfs_buf_vmap_len(
	struct xfs_buf	*bp)
{
	return (bp->b_page_count * PAGE_SIZE);
}

/*
 * When we mark a buffer stale, we remove the buffer from the LRU and clear the
 * b_lru_ref count so that the buffer is freed immediately when the buffer
 * reference count falls to zero. If the buffer is already on the LRU, we need
 * to remove the reference that LRU holds on the buffer.
 *
 * This prevents build-up of stale buffers on the LRU.
 */
void
xfs_buf_stale(
	struct xfs_buf	*bp)
{
	ASSERT(xfs_buf_islocked(bp));

	bp->b_flags |= XBF_STALE;

	/*
	 * Clear the delwri status so that a delwri queue walker will not
	 * flush this buffer to disk now that it is stale. The delwri queue has
	 * a reference to the buffer, so this is safe to do.
	 */
	bp->b_flags &= ~_XBF_DELWRI_Q;

	spin_lock(&bp->b_lock);
	atomic_set(&bp->b_lru_ref, 0);
	if (!(bp->b_state & XFS_BSTATE_DISPOSE) &&
	    (list_lru_del_obj(&bp->b_target->bt_lru, &bp->b_lru)))
		bp->b_hold--;

	ASSERT(bp->b_hold >= 1);
	spin_unlock(&bp->b_lock);
}

static int
xfs_buf_get_maps(
	struct xfs_buf		*bp,
	int			map_count)
{
	ASSERT(bp->b_maps == NULL);
	bp->b_map_count = map_count;

	if (map_count == 1) {
		bp->b_maps = &bp->__b_map;
		return 0;
	}

	bp->b_maps = kzalloc(map_count * sizeof(struct xfs_buf_map),
			GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL);
	if (!bp->b_maps)
		return -ENOMEM;
	return 0;
}

static void
xfs_buf_free_maps(
	struct xfs_buf	*bp)
{
	if (bp->b_maps != &bp->__b_map) {
		kfree(bp->b_maps);
		bp->b_maps = NULL;
	}
}

static int
_xfs_buf_alloc(
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp)
{
	struct xfs_buf		*bp;
	int			error;
	int			i;

	*bpp = NULL;
	bp = kmem_cache_zalloc(xfs_buf_cache,
			GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL);

	/*
	 * We don't want certain flags to appear in b_flags unless they are
	 * specifically set by later operations on the buffer.
	 */
	flags &= ~(XBF_UNMAPPED | XBF_TRYLOCK | XBF_ASYNC | XBF_READ_AHEAD);

	/*
	 * A new buffer is held and locked by the owner.  This ensures that the
	 * buffer is owned by the caller and racing RCU lookups right after
	 * inserting into the hash table are safe (and will have to wait for
	 * the unlock to do anything non-trivial).
	 */
	bp->b_hold = 1;
	sema_init(&bp->b_sema, 0); /* held, no waiters */

	spin_lock_init(&bp->b_lock);
	atomic_set(&bp->b_lru_ref, 1);
	init_completion(&bp->b_iowait);
	INIT_LIST_HEAD(&bp->b_lru);
	INIT_LIST_HEAD(&bp->b_list);
	INIT_LIST_HEAD(&bp->b_li_list);
	bp->b_target = target;
	bp->b_mount = target->bt_mount;
	bp->b_flags = flags;

	error = xfs_buf_get_maps(bp, nmaps);
	if (error)  {
		kmem_cache_free(xfs_buf_cache, bp);
		return error;
	}

	bp->b_rhash_key = map[0].bm_bn;
	bp->b_length = 0;
	for (i = 0; i < nmaps; i++) {
		bp->b_maps[i].bm_bn = map[i].bm_bn;
		bp->b_maps[i].bm_len = map[i].bm_len;
		bp->b_length += map[i].bm_len;
	}

	atomic_set(&bp->b_pin_count, 0);
	init_waitqueue_head(&bp->b_waiters);

	XFS_STATS_INC(bp->b_mount, xb_create);
	trace_xfs_buf_init(bp, _RET_IP_);

	*bpp = bp;
	return 0;
}

static void
xfs_buf_free_pages(
	struct xfs_buf	*bp)
{
	uint		i;

	ASSERT(bp->b_flags & _XBF_PAGES);

	if (xfs_buf_is_vmapped(bp))
		vm_unmap_ram(bp->b_addr, bp->b_page_count);

	for (i = 0; i < bp->b_page_count; i++) {
		if (bp->b_pages[i])
			__free_page(bp->b_pages[i]);
	}
	mm_account_reclaimed_pages(bp->b_page_count);

	if (bp->b_pages != bp->b_page_array)
		kfree(bp->b_pages);
	bp->b_pages = NULL;
	bp->b_flags &= ~_XBF_PAGES;
}

static void
xfs_buf_free_callback(
	struct callback_head	*cb)
{
	struct xfs_buf		*bp = container_of(cb, struct xfs_buf, b_rcu);

	xfs_buf_free_maps(bp);
	kmem_cache_free(xfs_buf_cache, bp);
}

static void
xfs_buf_free(
	struct xfs_buf		*bp)
{
	trace_xfs_buf_free(bp, _RET_IP_);

	ASSERT(list_empty(&bp->b_lru));

	if (xfs_buftarg_is_mem(bp->b_target))
		xmbuf_unmap_page(bp);
	else if (bp->b_flags & _XBF_PAGES)
		xfs_buf_free_pages(bp);
	else if (bp->b_flags & _XBF_KMEM)
		kfree(bp->b_addr);

	call_rcu(&bp->b_rcu, xfs_buf_free_callback);
}

static int
xfs_buf_alloc_kmem(
	struct xfs_buf	*bp,
	xfs_buf_flags_t	flags)
{
	gfp_t		gfp_mask = GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOFAIL;
	size_t		size = BBTOB(bp->b_length);

	/* Assure zeroed buffer for non-read cases. */
	if (!(flags & XBF_READ))
		gfp_mask |= __GFP_ZERO;

	bp->b_addr = kmalloc(size, gfp_mask);
	if (!bp->b_addr)
		return -ENOMEM;

	if (((unsigned long)(bp->b_addr + size - 1) & PAGE_MASK) !=
	    ((unsigned long)bp->b_addr & PAGE_MASK)) {
		/* b_addr spans two pages - use alloc_page instead */
		kfree(bp->b_addr);
		bp->b_addr = NULL;
		return -ENOMEM;
	}
	bp->b_offset = offset_in_page(bp->b_addr);
	bp->b_pages = bp->b_page_array;
	bp->b_pages[0] = kmem_to_page(bp->b_addr);
	bp->b_page_count = 1;
	bp->b_flags |= _XBF_KMEM;
	return 0;
}

static int
xfs_buf_alloc_pages(
	struct xfs_buf	*bp,
	xfs_buf_flags_t	flags)
{
	gfp_t		gfp_mask = GFP_KERNEL | __GFP_NOLOCKDEP | __GFP_NOWARN;
	long		filled = 0;

	if (flags & XBF_READ_AHEAD)
		gfp_mask |= __GFP_NORETRY;

	/* Make sure that we have a page list */
	bp->b_page_count = DIV_ROUND_UP(BBTOB(bp->b_length), PAGE_SIZE);
	if (bp->b_page_count <= XB_PAGES) {
		bp->b_pages = bp->b_page_array;
	} else {
		bp->b_pages = kzalloc(sizeof(struct page *) * bp->b_page_count,
					gfp_mask);
		if (!bp->b_pages)
			return -ENOMEM;
	}
	bp->b_flags |= _XBF_PAGES;

	/* Assure zeroed buffer for non-read cases. */
	if (!(flags & XBF_READ))
		gfp_mask |= __GFP_ZERO;

	/*
	 * Bulk filling of pages can take multiple calls. Not filling the entire
	 * array is not an allocation failure, so don't back off if we get at
	 * least one extra page.
	 */
	for (;;) {
		long	last = filled;

		filled = alloc_pages_bulk(gfp_mask, bp->b_page_count,
					  bp->b_pages);
		if (filled == bp->b_page_count) {
			XFS_STATS_INC(bp->b_mount, xb_page_found);
			break;
		}

		if (filled != last)
			continue;

		if (flags & XBF_READ_AHEAD) {
			xfs_buf_free_pages(bp);
			return -ENOMEM;
		}

		XFS_STATS_INC(bp->b_mount, xb_page_retries);
		memalloc_retry_wait(gfp_mask);
	}
	return 0;
}

/*
 *	Map buffer into kernel address-space if necessary.
 */
STATIC int
_xfs_buf_map_pages(
	struct xfs_buf		*bp,
	xfs_buf_flags_t		flags)
{
	ASSERT(bp->b_flags & _XBF_PAGES);
	if (bp->b_page_count == 1) {
		/* A single page buffer is always mappable */
		bp->b_addr = page_address(bp->b_pages[0]);
	} else if (flags & XBF_UNMAPPED) {
		bp->b_addr = NULL;
	} else {
		int retried = 0;
		unsigned nofs_flag;

		/*
		 * vm_map_ram() will allocate auxiliary structures (e.g.
		 * pagetables) with GFP_KERNEL, yet we often under a scoped nofs
		 * context here. Mixing GFP_KERNEL with GFP_NOFS allocations
		 * from the same call site that can be run from both above and
		 * below memory reclaim causes lockdep false positives. Hence we
		 * always need to force this allocation to nofs context because
		 * we can't pass __GFP_NOLOCKDEP down to auxillary structures to
		 * prevent false positive lockdep reports.
		 *
		 * XXX(dgc): I think dquot reclaim is the only place we can get
		 * to this function from memory reclaim context now. If we fix
		 * that like we've fixed inode reclaim to avoid writeback from
		 * reclaim, this nofs wrapping can go away.
		 */
		nofs_flag = memalloc_nofs_save();
		do {
			bp->b_addr = vm_map_ram(bp->b_pages, bp->b_page_count,
						-1);
			if (bp->b_addr)
				break;
			vm_unmap_aliases();
		} while (retried++ <= 1);
		memalloc_nofs_restore(nofs_flag);

		if (!bp->b_addr)
			return -ENOMEM;
	}

	return 0;
}

/*
 *	Finding and Reading Buffers
 */
static int
_xfs_buf_obj_cmp(
	struct rhashtable_compare_arg	*arg,
	const void			*obj)
{
	const struct xfs_buf_map	*map = arg->key;
	const struct xfs_buf		*bp = obj;

	/*
	 * The key hashing in the lookup path depends on the key being the
	 * first element of the compare_arg, make sure to assert this.
	 */
	BUILD_BUG_ON(offsetof(struct xfs_buf_map, bm_bn) != 0);

	if (bp->b_rhash_key != map->bm_bn)
		return 1;

	if (unlikely(bp->b_length != map->bm_len)) {
		/*
		 * found a block number match. If the range doesn't
		 * match, the only way this is allowed is if the buffer
		 * in the cache is stale and the transaction that made
		 * it stale has not yet committed. i.e. we are
		 * reallocating a busy extent. Skip this buffer and
		 * continue searching for an exact match.
		 *
		 * Note: If we're scanning for incore buffers to stale, don't
		 * complain if we find non-stale buffers.
		 */
		if (!(map->bm_flags & XBM_LIVESCAN))
			ASSERT(bp->b_flags & XBF_STALE);
		return 1;
	}
	return 0;
}

static const struct rhashtable_params xfs_buf_hash_params = {
	.min_size		= 32,	/* empty AGs have minimal footprint */
	.nelem_hint		= 16,
	.key_len		= sizeof(xfs_daddr_t),
	.key_offset		= offsetof(struct xfs_buf, b_rhash_key),
	.head_offset		= offsetof(struct xfs_buf, b_rhash_head),
	.automatic_shrinking	= true,
	.obj_cmpfn		= _xfs_buf_obj_cmp,
};

int
xfs_buf_cache_init(
	struct xfs_buf_cache	*bch)
{
	return rhashtable_init(&bch->bc_hash, &xfs_buf_hash_params);
}

void
xfs_buf_cache_destroy(
	struct xfs_buf_cache	*bch)
{
	rhashtable_destroy(&bch->bc_hash);
}

static int
xfs_buf_map_verify(
	struct xfs_buftarg	*btp,
	struct xfs_buf_map	*map)
{
	xfs_daddr_t		eofs;

	/* Check for IOs smaller than the sector size / not sector aligned */
	ASSERT(!(BBTOB(map->bm_len) < btp->bt_meta_sectorsize));
	ASSERT(!(BBTOB(map->bm_bn) & (xfs_off_t)btp->bt_meta_sectormask));

	/*
	 * Corrupted block numbers can get through to here, unfortunately, so we
	 * have to check that the buffer falls within the filesystem bounds.
	 */
	eofs = XFS_FSB_TO_BB(btp->bt_mount, btp->bt_mount->m_sb.sb_dblocks);
	if (map->bm_bn < 0 || map->bm_bn >= eofs) {
		xfs_alert(btp->bt_mount,
			  "%s: daddr 0x%llx out of range, EOFS 0x%llx",
			  __func__, map->bm_bn, eofs);
		WARN_ON(1);
		return -EFSCORRUPTED;
	}
	return 0;
}

static int
xfs_buf_find_lock(
	struct xfs_buf          *bp,
	xfs_buf_flags_t		flags)
{
	if (flags & XBF_TRYLOCK) {
		if (!xfs_buf_trylock(bp)) {
			XFS_STATS_INC(bp->b_mount, xb_busy_locked);
			return -EAGAIN;
		}
	} else {
		xfs_buf_lock(bp);
		XFS_STATS_INC(bp->b_mount, xb_get_locked_waited);
	}

	/*
	 * if the buffer is stale, clear all the external state associated with
	 * it. We need to keep flags such as how we allocated the buffer memory
	 * intact here.
	 */
	if (bp->b_flags & XBF_STALE) {
		if (flags & XBF_LIVESCAN) {
			xfs_buf_unlock(bp);
			return -ENOENT;
		}
		ASSERT((bp->b_flags & _XBF_DELWRI_Q) == 0);
		bp->b_flags &= _XBF_KMEM | _XBF_PAGES;
		bp->b_ops = NULL;
	}
	return 0;
}

static bool
xfs_buf_try_hold(
	struct xfs_buf		*bp)
{
	spin_lock(&bp->b_lock);
	if (bp->b_hold == 0) {
		spin_unlock(&bp->b_lock);
		return false;
	}
	bp->b_hold++;
	spin_unlock(&bp->b_lock);
	return true;
}

static inline int
xfs_buf_lookup(
	struct xfs_buf_cache	*bch,
	struct xfs_buf_map	*map,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp)
{
	struct xfs_buf          *bp;
	int			error;

	rcu_read_lock();
	bp = rhashtable_lookup(&bch->bc_hash, map, xfs_buf_hash_params);
	if (!bp || !xfs_buf_try_hold(bp)) {
		rcu_read_unlock();
		return -ENOENT;
	}
	rcu_read_unlock();

	error = xfs_buf_find_lock(bp, flags);
	if (error) {
		xfs_buf_rele(bp);
		return error;
	}

	trace_xfs_buf_find(bp, flags, _RET_IP_);
	*bpp = bp;
	return 0;
}

/*
 * Insert the new_bp into the hash table. This consumes the perag reference
 * taken for the lookup regardless of the result of the insert.
 */
static int
xfs_buf_find_insert(
	struct xfs_buftarg	*btp,
	struct xfs_buf_cache	*bch,
	struct xfs_perag	*pag,
	struct xfs_buf_map	*cmap,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp)
{
	struct xfs_buf		*new_bp;
	struct xfs_buf		*bp;
	int			error;

	error = _xfs_buf_alloc(btp, map, nmaps, flags, &new_bp);
	if (error)
		goto out_drop_pag;

	if (xfs_buftarg_is_mem(new_bp->b_target)) {
		error = xmbuf_map_page(new_bp);
	} else if (BBTOB(new_bp->b_length) >= PAGE_SIZE ||
		   xfs_buf_alloc_kmem(new_bp, flags) < 0) {
		/*
		 * For buffers that fit entirely within a single page, first
		 * attempt to allocate the memory from the heap to minimise
		 * memory usage. If we can't get heap memory for these small
		 * buffers, we fall back to using the page allocator.
		 */
		error = xfs_buf_alloc_pages(new_bp, flags);
	}
	if (error)
		goto out_free_buf;

	/* The new buffer keeps the perag reference until it is freed. */
	new_bp->b_pag = pag;

	rcu_read_lock();
	bp = rhashtable_lookup_get_insert_fast(&bch->bc_hash,
			&new_bp->b_rhash_head, xfs_buf_hash_params);
	if (IS_ERR(bp)) {
		rcu_read_unlock();
		error = PTR_ERR(bp);
		goto out_free_buf;
	}
	if (bp && xfs_buf_try_hold(bp)) {
		/* found an existing buffer */
		rcu_read_unlock();
		error = xfs_buf_find_lock(bp, flags);
		if (error)
			xfs_buf_rele(bp);
		else
			*bpp = bp;
		goto out_free_buf;
	}
	rcu_read_unlock();

	*bpp = new_bp;
	return 0;

out_free_buf:
	xfs_buf_free(new_bp);
out_drop_pag:
	if (pag)
		xfs_perag_put(pag);
	return error;
}

static inline struct xfs_perag *
xfs_buftarg_get_pag(
	struct xfs_buftarg		*btp,
	const struct xfs_buf_map	*map)
{
	struct xfs_mount		*mp = btp->bt_mount;

	if (xfs_buftarg_is_mem(btp))
		return NULL;
	return xfs_perag_get(mp, xfs_daddr_to_agno(mp, map->bm_bn));
}

static inline struct xfs_buf_cache *
xfs_buftarg_buf_cache(
	struct xfs_buftarg		*btp,
	struct xfs_perag		*pag)
{
	if (pag)
		return &pag->pag_bcache;
	return btp->bt_cache;
}

/*
 * Assembles a buffer covering the specified range. The code is optimised for
 * cache hits, as metadata intensive workloads will see 3 orders of magnitude
 * more hits than misses.
 */
int
xfs_buf_get_map(
	struct xfs_buftarg	*btp,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp)
{
	struct xfs_buf_cache	*bch;
	struct xfs_perag	*pag;
	struct xfs_buf		*bp = NULL;
	struct xfs_buf_map	cmap = { .bm_bn = map[0].bm_bn };
	int			error;
	int			i;

	if (flags & XBF_LIVESCAN)
		cmap.bm_flags |= XBM_LIVESCAN;
	for (i = 0; i < nmaps; i++)
		cmap.bm_len += map[i].bm_len;

	error = xfs_buf_map_verify(btp, &cmap);
	if (error)
		return error;

	pag = xfs_buftarg_get_pag(btp, &cmap);
	bch = xfs_buftarg_buf_cache(btp, pag);

	error = xfs_buf_lookup(bch, &cmap, flags, &bp);
	if (error && error != -ENOENT)
		goto out_put_perag;

	/* cache hits always outnumber misses by at least 10:1 */
	if (unlikely(!bp)) {
		XFS_STATS_INC(btp->bt_mount, xb_miss_locked);

		if (flags & XBF_INCORE)
			goto out_put_perag;

		/* xfs_buf_find_insert() consumes the perag reference. */
		error = xfs_buf_find_insert(btp, bch, pag, &cmap, map, nmaps,
				flags, &bp);
		if (error)
			return error;
	} else {
		XFS_STATS_INC(btp->bt_mount, xb_get_locked);
		if (pag)
			xfs_perag_put(pag);
	}

	/* We do not hold a perag reference anymore. */
	if (!bp->b_addr) {
		error = _xfs_buf_map_pages(bp, flags);
		if (unlikely(error)) {
			xfs_warn_ratelimited(btp->bt_mount,
				"%s: failed to map %u pages", __func__,
				bp->b_page_count);
			xfs_buf_relse(bp);
			return error;
		}
	}

	/*
	 * Clear b_error if this is a lookup from a caller that doesn't expect
	 * valid data to be found in the buffer.
	 */
	if (!(flags & XBF_READ))
		xfs_buf_ioerror(bp, 0);

	XFS_STATS_INC(btp->bt_mount, xb_get);
	trace_xfs_buf_get(bp, flags, _RET_IP_);
	*bpp = bp;
	return 0;

out_put_perag:
	if (pag)
		xfs_perag_put(pag);
	return error;
}

int
_xfs_buf_read(
	struct xfs_buf		*bp)
{
	ASSERT(bp->b_maps[0].bm_bn != XFS_BUF_DADDR_NULL);

	bp->b_flags &= ~(XBF_WRITE | XBF_ASYNC | XBF_READ_AHEAD | XBF_DONE);
	bp->b_flags |= XBF_READ;
	xfs_buf_submit(bp);
	return xfs_buf_iowait(bp);
}

/*
 * Reverify a buffer found in cache without an attached ->b_ops.
 *
 * If the caller passed an ops structure and the buffer doesn't have ops
 * assigned, set the ops and use it to verify the contents. If verification
 * fails, clear XBF_DONE. We assume the buffer has no recorded errors and is
 * already in XBF_DONE state on entry.
 *
 * Under normal operations, every in-core buffer is verified on read I/O
 * completion. There are two scenarios that can lead to in-core buffers without
 * an assigned ->b_ops. The first is during log recovery of buffers on a V4
 * filesystem, though these buffers are purged at the end of recovery. The
 * other is online repair, which intentionally reads with a NULL buffer ops to
 * run several verifiers across an in-core buffer in order to establish buffer
 * type.  If repair can't establish that, the buffer will be left in memory
 * with NULL buffer ops.
 */
int
xfs_buf_reverify(
	struct xfs_buf		*bp,
	const struct xfs_buf_ops *ops)
{
	ASSERT(bp->b_flags & XBF_DONE);
	ASSERT(bp->b_error == 0);

	if (!ops || bp->b_ops)
		return 0;

	bp->b_ops = ops;
	bp->b_ops->verify_read(bp);
	if (bp->b_error)
		bp->b_flags &= ~XBF_DONE;
	return bp->b_error;
}

int
xfs_buf_read_map(
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops,
	xfs_failaddr_t		fa)
{
	struct xfs_buf		*bp;
	int			error;

	ASSERT(!(flags & (XBF_WRITE | XBF_ASYNC | XBF_READ_AHEAD)));

	flags |= XBF_READ;
	*bpp = NULL;

	error = xfs_buf_get_map(target, map, nmaps, flags, &bp);
	if (error)
		return error;

	trace_xfs_buf_read(bp, flags, _RET_IP_);

	if (!(bp->b_flags & XBF_DONE)) {
		/* Initiate the buffer read and wait. */
		XFS_STATS_INC(target->bt_mount, xb_get_read);
		bp->b_ops = ops;
		error = _xfs_buf_read(bp);
	} else {
		/* Buffer already read; all we need to do is check it. */
		error = xfs_buf_reverify(bp, ops);

		/* We do not want read in the flags */
		bp->b_flags &= ~XBF_READ;
		ASSERT(bp->b_ops != NULL || ops == NULL);
	}

	/*
	 * If we've had a read error, then the contents of the buffer are
	 * invalid and should not be used. To ensure that a followup read tries
	 * to pull the buffer from disk again, we clear the XBF_DONE flag and
	 * mark the buffer stale. This ensures that anyone who has a current
	 * reference to the buffer will interpret it's contents correctly and
	 * future cache lookups will also treat it as an empty, uninitialised
	 * buffer.
	 */
	if (error) {
		/*
		 * Check against log shutdown for error reporting because
		 * metadata writeback may require a read first and we need to
		 * report errors in metadata writeback until the log is shut
		 * down. High level transaction read functions already check
		 * against mount shutdown, anyway, so we only need to be
		 * concerned about low level IO interactions here.
		 */
		if (!xlog_is_shutdown(target->bt_mount->m_log))
			xfs_buf_ioerror_alert(bp, fa);

		bp->b_flags &= ~XBF_DONE;
		xfs_buf_stale(bp);
		xfs_buf_relse(bp);

		/* bad CRC means corrupted metadata */
		if (error == -EFSBADCRC)
			error = -EFSCORRUPTED;
		return error;
	}

	*bpp = bp;
	return 0;
}

/*
 *	If we are not low on memory then do the readahead in a deadlock
 *	safe manner.
 */
void
xfs_buf_readahead_map(
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	const struct xfs_buf_ops *ops)
{
	const xfs_buf_flags_t	flags = XBF_READ | XBF_ASYNC | XBF_READ_AHEAD;
	struct xfs_buf		*bp;

	/*
	 * Currently we don't have a good means or justification for performing
	 * xmbuf_map_page asynchronously, so we don't do readahead.
	 */
	if (xfs_buftarg_is_mem(target))
		return;

	if (xfs_buf_get_map(target, map, nmaps, flags | XBF_TRYLOCK, &bp))
		return;
	trace_xfs_buf_readahead(bp, 0, _RET_IP_);

	if (bp->b_flags & XBF_DONE) {
		xfs_buf_reverify(bp, ops);
		xfs_buf_relse(bp);
		return;
	}
	XFS_STATS_INC(target->bt_mount, xb_get_read);
	bp->b_ops = ops;
	bp->b_flags &= ~(XBF_WRITE | XBF_DONE);
	bp->b_flags |= flags;
	percpu_counter_inc(&target->bt_readahead_count);
	xfs_buf_submit(bp);
}

/*
 * Read an uncached buffer from disk. Allocates and returns a locked
 * buffer containing the disk contents or nothing. Uncached buffers always have
 * a cache index of XFS_BUF_DADDR_NULL so we can easily determine if the buffer
 * is cached or uncached during fault diagnosis.
 */
int
xfs_buf_read_uncached(
	struct xfs_buftarg	*target,
	xfs_daddr_t		daddr,
	size_t			numblks,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp;
	int			error;

	*bpp = NULL;

	error = xfs_buf_get_uncached(target, numblks, flags, &bp);
	if (error)
		return error;

	/* set up the buffer for a read IO */
	ASSERT(bp->b_map_count == 1);
	bp->b_rhash_key = XFS_BUF_DADDR_NULL;
	bp->b_maps[0].bm_bn = daddr;
	bp->b_flags |= XBF_READ;
	bp->b_ops = ops;

	xfs_buf_submit(bp);
	error = xfs_buf_iowait(bp);
	if (error) {
		xfs_buf_relse(bp);
		return error;
	}

	*bpp = bp;
	return 0;
}

int
xfs_buf_get_uncached(
	struct xfs_buftarg	*target,
	size_t			numblks,
	xfs_buf_flags_t		flags,
	struct xfs_buf		**bpp)
{
	int			error;
	struct xfs_buf		*bp;
	DEFINE_SINGLE_BUF_MAP(map, XFS_BUF_DADDR_NULL, numblks);

	/* there are currently no valid flags for xfs_buf_get_uncached */
	ASSERT(flags == 0);

	*bpp = NULL;

	error = _xfs_buf_alloc(target, &map, 1, flags, &bp);
	if (error)
		return error;

	if (xfs_buftarg_is_mem(bp->b_target))
		error = xmbuf_map_page(bp);
	else
		error = xfs_buf_alloc_pages(bp, flags);
	if (error)
		goto fail_free_buf;

	error = _xfs_buf_map_pages(bp, 0);
	if (unlikely(error)) {
		xfs_warn(target->bt_mount,
			"%s: failed to map pages", __func__);
		goto fail_free_buf;
	}

	trace_xfs_buf_get_uncached(bp, _RET_IP_);
	*bpp = bp;
	return 0;

fail_free_buf:
	xfs_buf_free(bp);
	return error;
}

/*
 *	Increment reference count on buffer, to hold the buffer concurrently
 *	with another thread which may release (free) the buffer asynchronously.
 *	Must hold the buffer already to call this function.
 */
void
xfs_buf_hold(
	struct xfs_buf		*bp)
{
	trace_xfs_buf_hold(bp, _RET_IP_);

	spin_lock(&bp->b_lock);
	bp->b_hold++;
	spin_unlock(&bp->b_lock);
}

static void
xfs_buf_rele_uncached(
	struct xfs_buf		*bp)
{
	ASSERT(list_empty(&bp->b_lru));

	spin_lock(&bp->b_lock);
	if (--bp->b_hold) {
		spin_unlock(&bp->b_lock);
		return;
	}
	spin_unlock(&bp->b_lock);
	xfs_buf_free(bp);
}

static void
xfs_buf_rele_cached(
	struct xfs_buf		*bp)
{
	struct xfs_buftarg	*btp = bp->b_target;
	struct xfs_perag	*pag = bp->b_pag;
	struct xfs_buf_cache	*bch = xfs_buftarg_buf_cache(btp, pag);
	bool			freebuf = false;

	trace_xfs_buf_rele(bp, _RET_IP_);

	spin_lock(&bp->b_lock);
	ASSERT(bp->b_hold >= 1);
	if (bp->b_hold > 1) {
		bp->b_hold--;
		goto out_unlock;
	}

	/* we are asked to drop the last reference */
	if (atomic_read(&bp->b_lru_ref)) {
		/*
		 * If the buffer is added to the LRU, keep the reference to the
		 * buffer for the LRU and clear the (now stale) dispose list
		 * state flag, else drop the reference.
		 */
		if (list_lru_add_obj(&btp->bt_lru, &bp->b_lru))
			bp->b_state &= ~XFS_BSTATE_DISPOSE;
		else
			bp->b_hold--;
	} else {
		bp->b_hold--;
		/*
		 * most of the time buffers will already be removed from the
		 * LRU, so optimise that case by checking for the
		 * XFS_BSTATE_DISPOSE flag indicating the last list the buffer
		 * was on was the disposal list
		 */
		if (!(bp->b_state & XFS_BSTATE_DISPOSE)) {
			list_lru_del_obj(&btp->bt_lru, &bp->b_lru);
		} else {
			ASSERT(list_empty(&bp->b_lru));
		}

		ASSERT(!(bp->b_flags & _XBF_DELWRI_Q));
		rhashtable_remove_fast(&bch->bc_hash, &bp->b_rhash_head,
				xfs_buf_hash_params);
		if (pag)
			xfs_perag_put(pag);
		freebuf = true;
	}

out_unlock:
	spin_unlock(&bp->b_lock);

	if (freebuf)
		xfs_buf_free(bp);
}

/*
 * Release a hold on the specified buffer.
 */
void
xfs_buf_rele(
	struct xfs_buf		*bp)
{
	trace_xfs_buf_rele(bp, _RET_IP_);
	if (xfs_buf_is_uncached(bp))
		xfs_buf_rele_uncached(bp);
	else
		xfs_buf_rele_cached(bp);
}

/*
 *	Lock a buffer object, if it is not already locked.
 *
 *	If we come across a stale, pinned, locked buffer, we know that we are
 *	being asked to lock a buffer that has been reallocated. Because it is
 *	pinned, we know that the log has not been pushed to disk and hence it
 *	will still be locked.  Rather than continuing to have trylock attempts
 *	fail until someone else pushes the log, push it ourselves before
 *	returning.  This means that the xfsaild will not get stuck trying
 *	to push on stale inode buffers.
 */
int
xfs_buf_trylock(
	struct xfs_buf		*bp)
{
	int			locked;

	locked = down_trylock(&bp->b_sema) == 0;
	if (locked)
		trace_xfs_buf_trylock(bp, _RET_IP_);
	else
		trace_xfs_buf_trylock_fail(bp, _RET_IP_);
	return locked;
}

/*
 *	Lock a buffer object.
 *
 *	If we come across a stale, pinned, locked buffer, we know that we
 *	are being asked to lock a buffer that has been reallocated. Because
 *	it is pinned, we know that the log has not been pushed to disk and
 *	hence it will still be locked. Rather than sleeping until someone
 *	else pushes the log, push it ourselves before trying to get the lock.
 */
void
xfs_buf_lock(
	struct xfs_buf		*bp)
{
	trace_xfs_buf_lock(bp, _RET_IP_);

	if (atomic_read(&bp->b_pin_count) && (bp->b_flags & XBF_STALE))
		xfs_log_force(bp->b_mount, 0);
	down(&bp->b_sema);

	trace_xfs_buf_lock_done(bp, _RET_IP_);
}

void
xfs_buf_unlock(
	struct xfs_buf		*bp)
{
	ASSERT(xfs_buf_islocked(bp));

	up(&bp->b_sema);
	trace_xfs_buf_unlock(bp, _RET_IP_);
}

STATIC void
xfs_buf_wait_unpin(
	struct xfs_buf		*bp)
{
	DECLARE_WAITQUEUE	(wait, current);

	if (atomic_read(&bp->b_pin_count) == 0)
		return;

	add_wait_queue(&bp->b_waiters, &wait);
	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (atomic_read(&bp->b_pin_count) == 0)
			break;
		io_schedule();
	}
	remove_wait_queue(&bp->b_waiters, &wait);
	set_current_state(TASK_RUNNING);
}

static void
xfs_buf_ioerror_alert_ratelimited(
	struct xfs_buf		*bp)
{
	static unsigned long	lasttime;
	static struct xfs_buftarg *lasttarg;

	if (bp->b_target != lasttarg ||
	    time_after(jiffies, (lasttime + 5*HZ))) {
		lasttime = jiffies;
		xfs_buf_ioerror_alert(bp, __this_address);
	}
	lasttarg = bp->b_target;
}

/*
 * Account for this latest trip around the retry handler, and decide if
 * we've failed enough times to constitute a permanent failure.
 */
static bool
xfs_buf_ioerror_permanent(
	struct xfs_buf		*bp,
	struct xfs_error_cfg	*cfg)
{
	struct xfs_mount	*mp = bp->b_mount;

	if (cfg->max_retries != XFS_ERR_RETRY_FOREVER &&
	    ++bp->b_retries > cfg->max_retries)
		return true;
	if (cfg->retry_timeout != XFS_ERR_RETRY_FOREVER &&
	    time_after(jiffies, cfg->retry_timeout + bp->b_first_retry_time))
		return true;

	/* At unmount we may treat errors differently */
	if (xfs_is_unmounting(mp) && mp->m_fail_unmount)
		return true;

	return false;
}

/*
 * On a sync write or shutdown we just want to stale the buffer and let the
 * caller handle the error in bp->b_error appropriately.
 *
 * If the write was asynchronous then no one will be looking for the error.  If
 * this is the first failure of this type, clear the error state and write the
 * buffer out again. This means we always retry an async write failure at least
 * once, but we also need to set the buffer up to behave correctly now for
 * repeated failures.
 *
 * If we get repeated async write failures, then we take action according to the
 * error configuration we have been set up to use.
 *
 * Returns true if this function took care of error handling and the caller must
 * not touch the buffer again.  Return false if the caller should proceed with
 * normal I/O completion handling.
 */
static bool
xfs_buf_ioend_handle_error(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_error_cfg	*cfg;
	struct xfs_log_item	*lip;

	/*
	 * If we've already shutdown the journal because of I/O errors, there's
	 * no point in giving this a retry.
	 */
	if (xlog_is_shutdown(mp->m_log))
		goto out_stale;

	xfs_buf_ioerror_alert_ratelimited(bp);

	/*
	 * We're not going to bother about retrying this during recovery.
	 * One strike!
	 */
	if (bp->b_flags & _XBF_LOGRECOVERY) {
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		return false;
	}

	/*
	 * Synchronous writes will have callers process the error.
	 */
	if (!(bp->b_flags & XBF_ASYNC))
		goto out_stale;

	trace_xfs_buf_iodone_async(bp, _RET_IP_);

	cfg = xfs_error_get_cfg(mp, XFS_ERR_METADATA, bp->b_error);
	if (bp->b_last_error != bp->b_error ||
	    !(bp->b_flags & (XBF_STALE | XBF_WRITE_FAIL))) {
		bp->b_last_error = bp->b_error;
		if (cfg->retry_timeout != XFS_ERR_RETRY_FOREVER &&
		    !bp->b_first_retry_time)
			bp->b_first_retry_time = jiffies;
		goto resubmit;
	}

	/*
	 * Permanent error - we need to trigger a shutdown if we haven't already
	 * to indicate that inconsistency will result from this action.
	 */
	if (xfs_buf_ioerror_permanent(bp, cfg)) {
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		goto out_stale;
	}

	/* Still considered a transient error. Caller will schedule retries. */
	list_for_each_entry(lip, &bp->b_li_list, li_bio_list) {
		set_bit(XFS_LI_FAILED, &lip->li_flags);
		clear_bit(XFS_LI_FLUSHING, &lip->li_flags);
	}

	xfs_buf_ioerror(bp, 0);
	xfs_buf_relse(bp);
	return true;

resubmit:
	xfs_buf_ioerror(bp, 0);
	bp->b_flags |= (XBF_DONE | XBF_WRITE_FAIL);
	reinit_completion(&bp->b_iowait);
	xfs_buf_submit(bp);
	return true;
out_stale:
	xfs_buf_stale(bp);
	bp->b_flags |= XBF_DONE;
	bp->b_flags &= ~XBF_WRITE;
	trace_xfs_buf_error_relse(bp, _RET_IP_);
	return false;
}

/* returns false if the caller needs to resubmit the I/O, else true */
static bool
__xfs_buf_ioend(
	struct xfs_buf	*bp)
{
	trace_xfs_buf_iodone(bp, _RET_IP_);

	if (bp->b_flags & XBF_READ) {
		if (!bp->b_error && xfs_buf_is_vmapped(bp))
			invalidate_kernel_vmap_range(bp->b_addr,
					xfs_buf_vmap_len(bp));
		if (!bp->b_error && bp->b_ops)
			bp->b_ops->verify_read(bp);
		if (!bp->b_error)
			bp->b_flags |= XBF_DONE;
		if (bp->b_flags & XBF_READ_AHEAD)
			percpu_counter_dec(&bp->b_target->bt_readahead_count);
	} else {
		if (!bp->b_error) {
			bp->b_flags &= ~XBF_WRITE_FAIL;
			bp->b_flags |= XBF_DONE;
		}

		if (unlikely(bp->b_error) && xfs_buf_ioend_handle_error(bp))
			return false;

		/* clear the retry state */
		bp->b_last_error = 0;
		bp->b_retries = 0;
		bp->b_first_retry_time = 0;

		/*
		 * Note that for things like remote attribute buffers, there may
		 * not be a buffer log item here, so processing the buffer log
		 * item must remain optional.
		 */
		if (bp->b_log_item)
			xfs_buf_item_done(bp);

		if (bp->b_iodone)
			bp->b_iodone(bp);
	}

	bp->b_flags &= ~(XBF_READ | XBF_WRITE | XBF_READ_AHEAD |
			 _XBF_LOGRECOVERY);
	return true;
}

static void
xfs_buf_ioend(
	struct xfs_buf	*bp)
{
	if (!__xfs_buf_ioend(bp))
		return;
	if (bp->b_flags & XBF_ASYNC)
		xfs_buf_relse(bp);
	else
		complete(&bp->b_iowait);
}

static void
xfs_buf_ioend_work(
	struct work_struct	*work)
{
	struct xfs_buf		*bp =
		container_of(work, struct xfs_buf, b_ioend_work);

	if (__xfs_buf_ioend(bp))
		xfs_buf_relse(bp);
}

void
__xfs_buf_ioerror(
	struct xfs_buf		*bp,
	int			error,
	xfs_failaddr_t		failaddr)
{
	ASSERT(error <= 0 && error >= -1000);
	bp->b_error = error;
	trace_xfs_buf_ioerror(bp, error, failaddr);
}

void
xfs_buf_ioerror_alert(
	struct xfs_buf		*bp,
	xfs_failaddr_t		func)
{
	xfs_buf_alert_ratelimited(bp, "XFS: metadata IO error",
		"metadata I/O error in \"%pS\" at daddr 0x%llx len %d error %d",
				  func, (uint64_t)xfs_buf_daddr(bp),
				  bp->b_length, -bp->b_error);
}

/*
 * To simulate an I/O failure, the buffer must be locked and held with at least
 * three references. The LRU reference is dropped by the stale call. The buf
 * item reference is dropped via ioend processing. The third reference is owned
 * by the caller and is dropped on I/O completion if the buffer is XBF_ASYNC.
 */
void
xfs_buf_ioend_fail(
	struct xfs_buf	*bp)
{
	bp->b_flags &= ~XBF_DONE;
	xfs_buf_stale(bp);
	xfs_buf_ioerror(bp, -EIO);
	xfs_buf_ioend(bp);
}

int
xfs_bwrite(
	struct xfs_buf		*bp)
{
	int			error;

	ASSERT(xfs_buf_islocked(bp));

	bp->b_flags |= XBF_WRITE;
	bp->b_flags &= ~(XBF_ASYNC | XBF_READ | _XBF_DELWRI_Q |
			 XBF_DONE);

	xfs_buf_submit(bp);
	error = xfs_buf_iowait(bp);
	if (error)
		xfs_force_shutdown(bp->b_mount, SHUTDOWN_META_IO_ERROR);
	return error;
}

static void
xfs_buf_bio_end_io(
	struct bio		*bio)
{
	struct xfs_buf		*bp = bio->bi_private;

	if (bio->bi_status)
		xfs_buf_ioerror(bp, blk_status_to_errno(bio->bi_status));
	else if ((bp->b_flags & XBF_WRITE) && (bp->b_flags & XBF_ASYNC) &&
		 XFS_TEST_ERROR(false, bp->b_mount, XFS_ERRTAG_BUF_IOERROR))
		xfs_buf_ioerror(bp, -EIO);

	if (bp->b_flags & XBF_ASYNC) {
		INIT_WORK(&bp->b_ioend_work, xfs_buf_ioend_work);
		queue_work(bp->b_mount->m_buf_workqueue, &bp->b_ioend_work);
	} else {
		complete(&bp->b_iowait);
	}

	bio_put(bio);
}

static inline blk_opf_t
xfs_buf_bio_op(
	struct xfs_buf		*bp)
{
	blk_opf_t		op;

	if (bp->b_flags & XBF_WRITE) {
		op = REQ_OP_WRITE;
	} else {
		op = REQ_OP_READ;
		if (bp->b_flags & XBF_READ_AHEAD)
			op |= REQ_RAHEAD;
	}

	return op | REQ_META;
}

static void
xfs_buf_submit_bio(
	struct xfs_buf		*bp)
{
	unsigned int		size = BBTOB(bp->b_length);
	unsigned int		map = 0, p;
	struct blk_plug		plug;
	struct bio		*bio;

	bio = bio_alloc(bp->b_target->bt_bdev, bp->b_page_count,
			xfs_buf_bio_op(bp), GFP_NOIO);
	bio->bi_private = bp;
	bio->bi_end_io = xfs_buf_bio_end_io;

	if (bp->b_flags & _XBF_KMEM) {
		__bio_add_page(bio, virt_to_page(bp->b_addr), size,
				bp->b_offset);
	} else {
		for (p = 0; p < bp->b_page_count; p++)
			__bio_add_page(bio, bp->b_pages[p], PAGE_SIZE, 0);
		bio->bi_iter.bi_size = size; /* limit to the actual size used */

		if (xfs_buf_is_vmapped(bp))
			flush_kernel_vmap_range(bp->b_addr,
					xfs_buf_vmap_len(bp));
	}

	/*
	 * If there is more than one map segment, split out a new bio for each
	 * map except of the last one.  The last map is handled by the
	 * remainder of the original bio outside the loop.
	 */
	blk_start_plug(&plug);
	for (map = 0; map < bp->b_map_count - 1; map++) {
		struct bio	*split;

		split = bio_split(bio, bp->b_maps[map].bm_len, GFP_NOFS,
				&fs_bio_set);
		split->bi_iter.bi_sector = bp->b_maps[map].bm_bn;
		bio_chain(split, bio);
		submit_bio(split);
	}
	bio->bi_iter.bi_sector = bp->b_maps[map].bm_bn;
	submit_bio(bio);
	blk_finish_plug(&plug);
}

/*
 * Wait for I/O completion of a sync buffer and return the I/O error code.
 */
static int
xfs_buf_iowait(
	struct xfs_buf	*bp)
{
	ASSERT(!(bp->b_flags & XBF_ASYNC));

	do {
		trace_xfs_buf_iowait(bp, _RET_IP_);
		wait_for_completion(&bp->b_iowait);
		trace_xfs_buf_iowait_done(bp, _RET_IP_);
	} while (!__xfs_buf_ioend(bp));

	return bp->b_error;
}

/*
 * Run the write verifier callback function if it exists. If this fails, mark
 * the buffer with an error and do not dispatch the I/O.
 */
static bool
xfs_buf_verify_write(
	struct xfs_buf		*bp)
{
	if (bp->b_ops) {
		bp->b_ops->verify_write(bp);
		if (bp->b_error)
			return false;
	} else if (bp->b_rhash_key != XFS_BUF_DADDR_NULL) {
		/*
		 * Non-crc filesystems don't attach verifiers during log
		 * recovery, so don't warn for such filesystems.
		 */
		if (xfs_has_crc(bp->b_mount)) {
			xfs_warn(bp->b_mount,
				"%s: no buf ops on daddr 0x%llx len %d",
				__func__, xfs_buf_daddr(bp),
				bp->b_length);
			xfs_hex_dump(bp->b_addr, XFS_CORRUPTION_DUMP_LEN);
			dump_stack();
		}
	}

	return true;
}

/*
 * Buffer I/O submission path, read or write. Asynchronous submission transfers
 * the buffer lock ownership and the current reference to the IO. It is not
 * safe to reference the buffer after a call to this function unless the caller
 * holds an additional reference itself.
 */
static void
xfs_buf_submit(
	struct xfs_buf	*bp)
{
	trace_xfs_buf_submit(bp, _RET_IP_);

	ASSERT(!(bp->b_flags & _XBF_DELWRI_Q));

	/*
	 * On log shutdown we stale and complete the buffer immediately. We can
	 * be called to read the superblock before the log has been set up, so
	 * be careful checking the log state.
	 *
	 * Checking the mount shutdown state here can result in the log tail
	 * moving inappropriately on disk as the log may not yet be shut down.
	 * i.e. failing this buffer on mount shutdown can remove it from the AIL
	 * and move the tail of the log forwards without having written this
	 * buffer to disk. This corrupts the log tail state in memory, and
	 * because the log may not be shut down yet, it can then be propagated
	 * to disk before the log is shutdown. Hence we check log shutdown
	 * state here rather than mount state to avoid corrupting the log tail
	 * on shutdown.
	 */
	if (bp->b_mount->m_log && xlog_is_shutdown(bp->b_mount->m_log)) {
		xfs_buf_ioend_fail(bp);
		return;
	}

	if (bp->b_flags & XBF_WRITE)
		xfs_buf_wait_unpin(bp);

	/*
	 * Make sure we capture only current IO errors rather than stale errors
	 * left over from previous use of the buffer (e.g. failed readahead).
	 */
	bp->b_error = 0;

	if ((bp->b_flags & XBF_WRITE) && !xfs_buf_verify_write(bp)) {
		xfs_force_shutdown(bp->b_mount, SHUTDOWN_CORRUPT_INCORE);
		xfs_buf_ioend(bp);
		return;
	}

	/* In-memory targets are directly mapped, no I/O required. */
	if (xfs_buftarg_is_mem(bp->b_target)) {
		xfs_buf_ioend(bp);
		return;
	}

	xfs_buf_submit_bio(bp);
}

void *
xfs_buf_offset(
	struct xfs_buf		*bp,
	size_t			offset)
{
	struct page		*page;

	if (bp->b_addr)
		return bp->b_addr + offset;

	page = bp->b_pages[offset >> PAGE_SHIFT];
	return page_address(page) + (offset & (PAGE_SIZE-1));
}

void
xfs_buf_zero(
	struct xfs_buf		*bp,
	size_t			boff,
	size_t			bsize)
{
	size_t			bend;

	bend = boff + bsize;
	while (boff < bend) {
		struct page	*page;
		int		page_index, page_offset, csize;

		page_index = (boff + bp->b_offset) >> PAGE_SHIFT;
		page_offset = (boff + bp->b_offset) & ~PAGE_MASK;
		page = bp->b_pages[page_index];
		csize = min_t(size_t, PAGE_SIZE - page_offset,
				      BBTOB(bp->b_length) - boff);

		ASSERT((csize + page_offset) <= PAGE_SIZE);

		memset(page_address(page) + page_offset, 0, csize);

		boff += csize;
	}
}

/*
 * Log a message about and stale a buffer that a caller has decided is corrupt.
 *
 * This function should be called for the kinds of metadata corruption that
 * cannot be detect from a verifier, such as incorrect inter-block relationship
 * data.  Do /not/ call this function from a verifier function.
 *
 * The buffer must be XBF_DONE prior to the call.  Afterwards, the buffer will
 * be marked stale, but b_error will not be set.  The caller is responsible for
 * releasing the buffer or fixing it.
 */
void
__xfs_buf_mark_corrupt(
	struct xfs_buf		*bp,
	xfs_failaddr_t		fa)
{
	ASSERT(bp->b_flags & XBF_DONE);

	xfs_buf_corruption_error(bp, fa);
	xfs_buf_stale(bp);
}

/*
 *	Handling of buffer targets (buftargs).
 */

/*
 * Wait for any bufs with callbacks that have been submitted but have not yet
 * returned. These buffers will have an elevated hold count, so wait on those
 * while freeing all the buffers only held by the LRU.
 */
static enum lru_status
xfs_buftarg_drain_rele(
	struct list_head	*item,
	struct list_lru_one	*lru,
	void			*arg)

{
	struct xfs_buf		*bp = container_of(item, struct xfs_buf, b_lru);
	struct list_head	*dispose = arg;

	if (!spin_trylock(&bp->b_lock))
		return LRU_SKIP;
	if (bp->b_hold > 1) {
		/* need to wait, so skip it this pass */
		spin_unlock(&bp->b_lock);
		trace_xfs_buf_drain_buftarg(bp, _RET_IP_);
		return LRU_SKIP;
	}

	/*
	 * clear the LRU reference count so the buffer doesn't get
	 * ignored in xfs_buf_rele().
	 */
	atomic_set(&bp->b_lru_ref, 0);
	bp->b_state |= XFS_BSTATE_DISPOSE;
	list_lru_isolate_move(lru, item, dispose);
	spin_unlock(&bp->b_lock);
	return LRU_REMOVED;
}

/*
 * Wait for outstanding I/O on the buftarg to complete.
 */
void
xfs_buftarg_wait(
	struct xfs_buftarg	*btp)
{
	/*
	 * First wait for all in-flight readahead buffers to be released.  This is
	 * critical as new buffers do not make the LRU until they are released.
	 *
	 * Next, flush the buffer workqueue to ensure all completion processing
	 * has finished. Just waiting on buffer locks is not sufficient for
	 * async IO as the reference count held over IO is not released until
	 * after the buffer lock is dropped. Hence we need to ensure here that
	 * all reference counts have been dropped before we start walking the
	 * LRU list.
	 */
	while (percpu_counter_sum(&btp->bt_readahead_count))
		delay(100);
	flush_workqueue(btp->bt_mount->m_buf_workqueue);
}

void
xfs_buftarg_drain(
	struct xfs_buftarg	*btp)
{
	LIST_HEAD(dispose);
	int			loop = 0;
	bool			write_fail = false;

	xfs_buftarg_wait(btp);

	/* loop until there is nothing left on the lru list. */
	while (list_lru_count(&btp->bt_lru)) {
		list_lru_walk(&btp->bt_lru, xfs_buftarg_drain_rele,
			      &dispose, LONG_MAX);

		while (!list_empty(&dispose)) {
			struct xfs_buf *bp;
			bp = list_first_entry(&dispose, struct xfs_buf, b_lru);
			list_del_init(&bp->b_lru);
			if (bp->b_flags & XBF_WRITE_FAIL) {
				write_fail = true;
				xfs_buf_alert_ratelimited(bp,
					"XFS: Corruption Alert",
"Corruption Alert: Buffer at daddr 0x%llx had permanent write failures!",
					(long long)xfs_buf_daddr(bp));
			}
			xfs_buf_rele(bp);
		}
		if (loop++ != 0)
			delay(100);
	}

	/*
	 * If one or more failed buffers were freed, that means dirty metadata
	 * was thrown away. This should only ever happen after I/O completion
	 * handling has elevated I/O error(s) to permanent failures and shuts
	 * down the journal.
	 */
	if (write_fail) {
		ASSERT(xlog_is_shutdown(btp->bt_mount->m_log));
		xfs_alert(btp->bt_mount,
	      "Please run xfs_repair to determine the extent of the problem.");
	}
}

static enum lru_status
xfs_buftarg_isolate(
	struct list_head	*item,
	struct list_lru_one	*lru,
	void			*arg)
{
	struct xfs_buf		*bp = container_of(item, struct xfs_buf, b_lru);
	struct list_head	*dispose = arg;

	/*
	 * we are inverting the lru lock/bp->b_lock here, so use a trylock.
	 * If we fail to get the lock, just skip it.
	 */
	if (!spin_trylock(&bp->b_lock))
		return LRU_SKIP;
	/*
	 * Decrement the b_lru_ref count unless the value is already
	 * zero. If the value is already zero, we need to reclaim the
	 * buffer, otherwise it gets another trip through the LRU.
	 */
	if (atomic_add_unless(&bp->b_lru_ref, -1, 0)) {
		spin_unlock(&bp->b_lock);
		return LRU_ROTATE;
	}

	bp->b_state |= XFS_BSTATE_DISPOSE;
	list_lru_isolate_move(lru, item, dispose);
	spin_unlock(&bp->b_lock);
	return LRU_REMOVED;
}

static unsigned long
xfs_buftarg_shrink_scan(
	struct shrinker		*shrink,
	struct shrink_control	*sc)
{
	struct xfs_buftarg	*btp = shrink->private_data;
	LIST_HEAD(dispose);
	unsigned long		freed;

	freed = list_lru_shrink_walk(&btp->bt_lru, sc,
				     xfs_buftarg_isolate, &dispose);

	while (!list_empty(&dispose)) {
		struct xfs_buf *bp;
		bp = list_first_entry(&dispose, struct xfs_buf, b_lru);
		list_del_init(&bp->b_lru);
		xfs_buf_rele(bp);
	}

	return freed;
}

static unsigned long
xfs_buftarg_shrink_count(
	struct shrinker		*shrink,
	struct shrink_control	*sc)
{
	struct xfs_buftarg	*btp = shrink->private_data;
	return list_lru_shrink_count(&btp->bt_lru, sc);
}

void
xfs_destroy_buftarg(
	struct xfs_buftarg	*btp)
{
	shrinker_free(btp->bt_shrinker);
	ASSERT(percpu_counter_sum(&btp->bt_readahead_count) == 0);
	percpu_counter_destroy(&btp->bt_readahead_count);
	list_lru_destroy(&btp->bt_lru);
}

void
xfs_free_buftarg(
	struct xfs_buftarg	*btp)
{
	xfs_destroy_buftarg(btp);
	fs_put_dax(btp->bt_daxdev, btp->bt_mount);
	/* the main block device is closed by kill_block_super */
	if (btp->bt_bdev != btp->bt_mount->m_super->s_bdev)
		bdev_fput(btp->bt_bdev_file);
	kfree(btp);
}

int
xfs_setsize_buftarg(
	struct xfs_buftarg	*btp,
	unsigned int		sectorsize)
{
	/* Set up metadata sector size info */
	btp->bt_meta_sectorsize = sectorsize;
	btp->bt_meta_sectormask = sectorsize - 1;

	if (set_blocksize(btp->bt_bdev_file, sectorsize)) {
		xfs_warn(btp->bt_mount,
			"Cannot set_blocksize to %u on device %pg",
			sectorsize, btp->bt_bdev);
		return -EINVAL;
	}

	return 0;
}

int
xfs_init_buftarg(
	struct xfs_buftarg		*btp,
	size_t				logical_sectorsize,
	const char			*descr)
{
	/* Set up device logical sector size mask */
	btp->bt_logical_sectorsize = logical_sectorsize;
	btp->bt_logical_sectormask = logical_sectorsize - 1;

	/*
	 * Buffer IO error rate limiting. Limit it to no more than 10 messages
	 * per 30 seconds so as to not spam logs too much on repeated errors.
	 */
	ratelimit_state_init(&btp->bt_ioerror_rl, 30 * HZ,
			     DEFAULT_RATELIMIT_BURST);

	if (list_lru_init(&btp->bt_lru))
		return -ENOMEM;
	if (percpu_counter_init(&btp->bt_readahead_count, 0, GFP_KERNEL))
		goto out_destroy_lru;

	btp->bt_shrinker =
		shrinker_alloc(SHRINKER_NUMA_AWARE, "xfs-buf:%s", descr);
	if (!btp->bt_shrinker)
		goto out_destroy_io_count;
	btp->bt_shrinker->count_objects = xfs_buftarg_shrink_count;
	btp->bt_shrinker->scan_objects = xfs_buftarg_shrink_scan;
	btp->bt_shrinker->private_data = btp;
	shrinker_register(btp->bt_shrinker);
	return 0;

out_destroy_io_count:
	percpu_counter_destroy(&btp->bt_readahead_count);
out_destroy_lru:
	list_lru_destroy(&btp->bt_lru);
	return -ENOMEM;
}

struct xfs_buftarg *
xfs_alloc_buftarg(
	struct xfs_mount	*mp,
	struct file		*bdev_file)
{
	struct xfs_buftarg	*btp;
	const struct dax_holder_operations *ops = NULL;

#if defined(CONFIG_FS_DAX) && defined(CONFIG_MEMORY_FAILURE)
	ops = &xfs_dax_holder_operations;
#endif
	btp = kzalloc(sizeof(*btp), GFP_KERNEL | __GFP_NOFAIL);

	btp->bt_mount = mp;
	btp->bt_bdev_file = bdev_file;
	btp->bt_bdev = file_bdev(bdev_file);
	btp->bt_dev = btp->bt_bdev->bd_dev;
	btp->bt_daxdev = fs_dax_get_by_bdev(btp->bt_bdev, &btp->bt_dax_part_off,
					    mp, ops);

	if (bdev_can_atomic_write(btp->bt_bdev)) {
		btp->bt_bdev_awu_min = bdev_atomic_write_unit_min_bytes(
						btp->bt_bdev);
		btp->bt_bdev_awu_max = bdev_atomic_write_unit_max_bytes(
						btp->bt_bdev);
	}

	/*
	 * When allocating the buftargs we have not yet read the super block and
	 * thus don't know the file system sector size yet.
	 */
	if (xfs_setsize_buftarg(btp, bdev_logical_block_size(btp->bt_bdev)))
		goto error_free;
	if (xfs_init_buftarg(btp, bdev_logical_block_size(btp->bt_bdev),
			mp->m_super->s_id))
		goto error_free;

	return btp;

error_free:
	kfree(btp);
	return NULL;
}

static inline void
xfs_buf_list_del(
	struct xfs_buf		*bp)
{
	list_del_init(&bp->b_list);
	wake_up_var(&bp->b_list);
}

/*
 * Cancel a delayed write list.
 *
 * Remove each buffer from the list, clear the delwri queue flag and drop the
 * associated buffer reference.
 */
void
xfs_buf_delwri_cancel(
	struct list_head	*list)
{
	struct xfs_buf		*bp;

	while (!list_empty(list)) {
		bp = list_first_entry(list, struct xfs_buf, b_list);

		xfs_buf_lock(bp);
		bp->b_flags &= ~_XBF_DELWRI_Q;
		xfs_buf_list_del(bp);
		xfs_buf_relse(bp);
	}
}

/*
 * Add a buffer to the delayed write list.
 *
 * This queues a buffer for writeout if it hasn't already been.  Note that
 * neither this routine nor the buffer list submission functions perform
 * any internal synchronization.  It is expected that the lists are thread-local
 * to the callers.
 *
 * Returns true if we queued up the buffer, or false if it already had
 * been on the buffer list.
 */
bool
xfs_buf_delwri_queue(
	struct xfs_buf		*bp,
	struct list_head	*list)
{
	ASSERT(xfs_buf_islocked(bp));
	ASSERT(!(bp->b_flags & XBF_READ));

	/*
	 * If the buffer is already marked delwri it already is queued up
	 * by someone else for imediate writeout.  Just ignore it in that
	 * case.
	 */
	if (bp->b_flags & _XBF_DELWRI_Q) {
		trace_xfs_buf_delwri_queued(bp, _RET_IP_);
		return false;
	}

	trace_xfs_buf_delwri_queue(bp, _RET_IP_);

	/*
	 * If a buffer gets written out synchronously or marked stale while it
	 * is on a delwri list we lazily remove it. To do this, the other party
	 * clears the  _XBF_DELWRI_Q flag but otherwise leaves the buffer alone.
	 * It remains referenced and on the list.  In a rare corner case it
	 * might get readded to a delwri list after the synchronous writeout, in
	 * which case we need just need to re-add the flag here.
	 */
	bp->b_flags |= _XBF_DELWRI_Q;
	if (list_empty(&bp->b_list)) {
		xfs_buf_hold(bp);
		list_add_tail(&bp->b_list, list);
	}

	return true;
}

/*
 * Queue a buffer to this delwri list as part of a data integrity operation.
 * If the buffer is on any other delwri list, we'll wait for that to clear
 * so that the caller can submit the buffer for IO and wait for the result.
 * Callers must ensure the buffer is not already on the list.
 */
void
xfs_buf_delwri_queue_here(
	struct xfs_buf		*bp,
	struct list_head	*buffer_list)
{
	/*
	 * We need this buffer to end up on the /caller's/ delwri list, not any
	 * old list.  This can happen if the buffer is marked stale (which
	 * clears DELWRI_Q) after the AIL queues the buffer to its list but
	 * before the AIL has a chance to submit the list.
	 */
	while (!list_empty(&bp->b_list)) {
		xfs_buf_unlock(bp);
		wait_var_event(&bp->b_list, list_empty(&bp->b_list));
		xfs_buf_lock(bp);
	}

	ASSERT(!(bp->b_flags & _XBF_DELWRI_Q));

	xfs_buf_delwri_queue(bp, buffer_list);
}

/*
 * Compare function is more complex than it needs to be because
 * the return value is only 32 bits and we are doing comparisons
 * on 64 bit values
 */
static int
xfs_buf_cmp(
	void			*priv,
	const struct list_head	*a,
	const struct list_head	*b)
{
	struct xfs_buf	*ap = container_of(a, struct xfs_buf, b_list);
	struct xfs_buf	*bp = container_of(b, struct xfs_buf, b_list);
	xfs_daddr_t		diff;

	diff = ap->b_maps[0].bm_bn - bp->b_maps[0].bm_bn;
	if (diff < 0)
		return -1;
	if (diff > 0)
		return 1;
	return 0;
}

static bool
xfs_buf_delwri_submit_prep(
	struct xfs_buf		*bp)
{
	/*
	 * Someone else might have written the buffer synchronously or marked it
	 * stale in the meantime.  In that case only the _XBF_DELWRI_Q flag got
	 * cleared, and we have to drop the reference and remove it from the
	 * list here.
	 */
	if (!(bp->b_flags & _XBF_DELWRI_Q)) {
		xfs_buf_list_del(bp);
		xfs_buf_relse(bp);
		return false;
	}

	trace_xfs_buf_delwri_split(bp, _RET_IP_);
	bp->b_flags &= ~_XBF_DELWRI_Q;
	bp->b_flags |= XBF_WRITE;
	return true;
}

/*
 * Write out a buffer list asynchronously.
 *
 * This will take the @buffer_list, write all non-locked and non-pinned buffers
 * out and not wait for I/O completion on any of the buffers.  This interface
 * is only safely useable for callers that can track I/O completion by higher
 * level means, e.g. AIL pushing as the @buffer_list is consumed in this
 * function.
 *
 * Note: this function will skip buffers it would block on, and in doing so
 * leaves them on @buffer_list so they can be retried on a later pass. As such,
 * it is up to the caller to ensure that the buffer list is fully submitted or
 * cancelled appropriately when they are finished with the list. Failure to
 * cancel or resubmit the list until it is empty will result in leaked buffers
 * at unmount time.
 */
int
xfs_buf_delwri_submit_nowait(
	struct list_head	*buffer_list)
{
	struct xfs_buf		*bp, *n;
	int			pinned = 0;
	struct blk_plug		plug;

	list_sort(NULL, buffer_list, xfs_buf_cmp);

	blk_start_plug(&plug);
	list_for_each_entry_safe(bp, n, buffer_list, b_list) {
		if (!xfs_buf_trylock(bp))
			continue;
		if (xfs_buf_ispinned(bp)) {
			xfs_buf_unlock(bp);
			pinned++;
			continue;
		}
		if (!xfs_buf_delwri_submit_prep(bp))
			continue;
		bp->b_flags |= XBF_ASYNC;
		xfs_buf_list_del(bp);
		xfs_buf_submit(bp);
	}
	blk_finish_plug(&plug);

	return pinned;
}

/*
 * Write out a buffer list synchronously.
 *
 * This will take the @buffer_list, write all buffers out and wait for I/O
 * completion on all of the buffers. @buffer_list is consumed by the function,
 * so callers must have some other way of tracking buffers if they require such
 * functionality.
 */
int
xfs_buf_delwri_submit(
	struct list_head	*buffer_list)
{
	LIST_HEAD		(wait_list);
	int			error = 0, error2;
	struct xfs_buf		*bp, *n;
	struct blk_plug		plug;

	list_sort(NULL, buffer_list, xfs_buf_cmp);

	blk_start_plug(&plug);
	list_for_each_entry_safe(bp, n, buffer_list, b_list) {
		xfs_buf_lock(bp);
		if (!xfs_buf_delwri_submit_prep(bp))
			continue;
		bp->b_flags &= ~XBF_ASYNC;
		list_move_tail(&bp->b_list, &wait_list);
		xfs_buf_submit(bp);
	}
	blk_finish_plug(&plug);

	/* Wait for IO to complete. */
	while (!list_empty(&wait_list)) {
		bp = list_first_entry(&wait_list, struct xfs_buf, b_list);

		xfs_buf_list_del(bp);

		/*
		 * Wait on the locked buffer, check for errors and unlock and
		 * release the delwri queue reference.
		 */
		error2 = xfs_buf_iowait(bp);
		xfs_buf_relse(bp);
		if (!error)
			error = error2;
	}

	return error;
}

/*
 * Push a single buffer on a delwri queue.
 *
 * The purpose of this function is to submit a single buffer of a delwri queue
 * and return with the buffer still on the original queue.
 *
 * The buffer locking and queue management logic between _delwri_pushbuf() and
 * _delwri_queue() guarantee that the buffer cannot be queued to another list
 * before returning.
 */
int
xfs_buf_delwri_pushbuf(
	struct xfs_buf		*bp,
	struct list_head	*buffer_list)
{
	int			error;

	ASSERT(bp->b_flags & _XBF_DELWRI_Q);

	trace_xfs_buf_delwri_pushbuf(bp, _RET_IP_);

	xfs_buf_lock(bp);
	bp->b_flags &= ~(_XBF_DELWRI_Q | XBF_ASYNC);
	bp->b_flags |= XBF_WRITE;
	xfs_buf_submit(bp);

	/*
	 * The buffer is now locked, under I/O but still on the original delwri
	 * queue. Wait for I/O completion, restore the DELWRI_Q flag and
	 * return with the buffer unlocked and still on the original queue.
	 */
	error = xfs_buf_iowait(bp);
	bp->b_flags |= _XBF_DELWRI_Q;
	xfs_buf_unlock(bp);

	return error;
}

void xfs_buf_set_ref(struct xfs_buf *bp, int lru_ref)
{
	/*
	 * Set the lru reference count to 0 based on the error injection tag.
	 * This allows userspace to disrupt buffer caching for debug/testing
	 * purposes.
	 */
	if (XFS_TEST_ERROR(false, bp->b_mount, XFS_ERRTAG_BUF_LRU_REF))
		lru_ref = 0;

	atomic_set(&bp->b_lru_ref, lru_ref);
}

/*
 * Verify an on-disk magic value against the magic value specified in the
 * verifier structure. The verifier magic is in disk byte order so the caller is
 * expected to pass the value directly from disk.
 */
bool
xfs_verify_magic(
	struct xfs_buf		*bp,
	__be32			dmagic)
{
	struct xfs_mount	*mp = bp->b_mount;
	int			idx;

	idx = xfs_has_crc(mp);
	if (WARN_ON(!bp->b_ops || !bp->b_ops->magic[idx]))
		return false;
	return dmagic == bp->b_ops->magic[idx];
}
/*
 * Verify an on-disk magic value against the magic value specified in the
 * verifier structure. The verifier magic is in disk byte order so the caller is
 * expected to pass the value directly from disk.
 */
bool
xfs_verify_magic16(
	struct xfs_buf		*bp,
	__be16			dmagic)
{
	struct xfs_mount	*mp = bp->b_mount;
	int			idx;

	idx = xfs_has_crc(mp);
	if (WARN_ON(!bp->b_ops || !bp->b_ops->magic16[idx]))
		return false;
	return dmagic == bp->b_ops->magic16[idx];
}
