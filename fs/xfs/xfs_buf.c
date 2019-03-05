// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/bio.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/percpu.h>
#include <linux/blkdev.h>
#include <linux/hash.h>
#include <linux/kthread.h>
#include <linux/migrate.h>
#include <linux/backing-dev.h>
#include <linux/freezer.h>

#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_errortag.h"
#include "xfs_error.h"

static kmem_zone_t *xfs_buf_zone;

#define xb_to_gfp(flags) \
	((((flags) & XBF_READ_AHEAD) ? __GFP_NORETRY : GFP_NOFS) | __GFP_NOWARN)

/*
 * Locking orders
 *
 * xfs_buf_ioacct_inc:
 * xfs_buf_ioacct_dec:
 *	b_sema (caller holds)
 *	  b_lock
 *
 * xfs_buf_stale:
 *	b_sema (caller holds)
 *	  b_lock
 *	    lru_lock
 *
 * xfs_buf_rele:
 *	b_lock
 *	  pag_buf_lock
 *	    lru_lock
 *
 * xfs_buftarg_wait_rele
 *	lru_lock
 *	  b_lock (trylock due to inversion)
 *
 * xfs_buftarg_isolate
 *	lru_lock
 *	  b_lock (trylock due to inversion)
 */

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
	return (bp->b_page_count * PAGE_SIZE) - bp->b_offset;
}

/*
 * Bump the I/O in flight count on the buftarg if we haven't yet done so for
 * this buffer. The count is incremented once per buffer (per hold cycle)
 * because the corresponding decrement is deferred to buffer release. Buffers
 * can undergo I/O multiple times in a hold-release cycle and per buffer I/O
 * tracking adds unnecessary overhead. This is used for sychronization purposes
 * with unmount (see xfs_wait_buftarg()), so all we really need is a count of
 * in-flight buffers.
 *
 * Buffers that are never released (e.g., superblock, iclog buffers) must set
 * the XBF_NO_IOACCT flag before I/O submission. Otherwise, the buftarg count
 * never reaches zero and unmount hangs indefinitely.
 */
static inline void
xfs_buf_ioacct_inc(
	struct xfs_buf	*bp)
{
	if (bp->b_flags & XBF_NO_IOACCT)
		return;

	ASSERT(bp->b_flags & XBF_ASYNC);
	spin_lock(&bp->b_lock);
	if (!(bp->b_state & XFS_BSTATE_IN_FLIGHT)) {
		bp->b_state |= XFS_BSTATE_IN_FLIGHT;
		percpu_counter_inc(&bp->b_target->bt_io_count);
	}
	spin_unlock(&bp->b_lock);
}

/*
 * Clear the in-flight state on a buffer about to be released to the LRU or
 * freed and unaccount from the buftarg.
 */
static inline void
__xfs_buf_ioacct_dec(
	struct xfs_buf	*bp)
{
	lockdep_assert_held(&bp->b_lock);

	if (bp->b_state & XFS_BSTATE_IN_FLIGHT) {
		bp->b_state &= ~XFS_BSTATE_IN_FLIGHT;
		percpu_counter_dec(&bp->b_target->bt_io_count);
	}
}

static inline void
xfs_buf_ioacct_dec(
	struct xfs_buf	*bp)
{
	spin_lock(&bp->b_lock);
	__xfs_buf_ioacct_dec(bp);
	spin_unlock(&bp->b_lock);
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

	/*
	 * Once the buffer is marked stale and unlocked, a subsequent lookup
	 * could reset b_flags. There is no guarantee that the buffer is
	 * unaccounted (released to LRU) before that occurs. Drop in-flight
	 * status now to preserve accounting consistency.
	 */
	spin_lock(&bp->b_lock);
	__xfs_buf_ioacct_dec(bp);

	atomic_set(&bp->b_lru_ref, 0);
	if (!(bp->b_state & XFS_BSTATE_DISPOSE) &&
	    (list_lru_del(&bp->b_target->bt_lru, &bp->b_lru)))
		atomic_dec(&bp->b_hold);

	ASSERT(atomic_read(&bp->b_hold) >= 1);
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

	bp->b_maps = kmem_zalloc(map_count * sizeof(struct xfs_buf_map),
				KM_NOFS);
	if (!bp->b_maps)
		return -ENOMEM;
	return 0;
}

/*
 *	Frees b_pages if it was allocated.
 */
static void
xfs_buf_free_maps(
	struct xfs_buf	*bp)
{
	if (bp->b_maps != &bp->__b_map) {
		kmem_free(bp->b_maps);
		bp->b_maps = NULL;
	}
}

struct xfs_buf *
_xfs_buf_alloc(
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags)
{
	struct xfs_buf		*bp;
	int			error;
	int			i;

	bp = kmem_zone_zalloc(xfs_buf_zone, KM_NOFS);
	if (unlikely(!bp))
		return NULL;

	/*
	 * We don't want certain flags to appear in b_flags unless they are
	 * specifically set by later operations on the buffer.
	 */
	flags &= ~(XBF_UNMAPPED | XBF_TRYLOCK | XBF_ASYNC | XBF_READ_AHEAD);

	atomic_set(&bp->b_hold, 1);
	atomic_set(&bp->b_lru_ref, 1);
	init_completion(&bp->b_iowait);
	INIT_LIST_HEAD(&bp->b_lru);
	INIT_LIST_HEAD(&bp->b_list);
	INIT_LIST_HEAD(&bp->b_li_list);
	sema_init(&bp->b_sema, 0); /* held, no waiters */
	spin_lock_init(&bp->b_lock);
	bp->b_target = target;
	bp->b_flags = flags;

	/*
	 * Set length and io_length to the same value initially.
	 * I/O routines should use io_length, which will be the same in
	 * most cases but may be reset (e.g. XFS recovery).
	 */
	error = xfs_buf_get_maps(bp, nmaps);
	if (error)  {
		kmem_zone_free(xfs_buf_zone, bp);
		return NULL;
	}

	bp->b_bn = map[0].bm_bn;
	bp->b_length = 0;
	for (i = 0; i < nmaps; i++) {
		bp->b_maps[i].bm_bn = map[i].bm_bn;
		bp->b_maps[i].bm_len = map[i].bm_len;
		bp->b_length += map[i].bm_len;
	}
	bp->b_io_length = bp->b_length;

	atomic_set(&bp->b_pin_count, 0);
	init_waitqueue_head(&bp->b_waiters);

	XFS_STATS_INC(target->bt_mount, xb_create);
	trace_xfs_buf_init(bp, _RET_IP_);

	return bp;
}

/*
 *	Allocate a page array capable of holding a specified number
 *	of pages, and point the page buf at it.
 */
STATIC int
_xfs_buf_get_pages(
	xfs_buf_t		*bp,
	int			page_count)
{
	/* Make sure that we have a page list */
	if (bp->b_pages == NULL) {
		bp->b_page_count = page_count;
		if (page_count <= XB_PAGES) {
			bp->b_pages = bp->b_page_array;
		} else {
			bp->b_pages = kmem_alloc(sizeof(struct page *) *
						 page_count, KM_NOFS);
			if (bp->b_pages == NULL)
				return -ENOMEM;
		}
		memset(bp->b_pages, 0, sizeof(struct page *) * page_count);
	}
	return 0;
}

/*
 *	Frees b_pages if it was allocated.
 */
STATIC void
_xfs_buf_free_pages(
	xfs_buf_t	*bp)
{
	if (bp->b_pages != bp->b_page_array) {
		kmem_free(bp->b_pages);
		bp->b_pages = NULL;
	}
}

/*
 *	Releases the specified buffer.
 *
 * 	The modification state of any associated pages is left unchanged.
 * 	The buffer must not be on any hash - use xfs_buf_rele instead for
 * 	hashed and refcounted buffers
 */
void
xfs_buf_free(
	xfs_buf_t		*bp)
{
	trace_xfs_buf_free(bp, _RET_IP_);

	ASSERT(list_empty(&bp->b_lru));

	if (bp->b_flags & _XBF_PAGES) {
		uint		i;

		if (xfs_buf_is_vmapped(bp))
			vm_unmap_ram(bp->b_addr - bp->b_offset,
					bp->b_page_count);

		for (i = 0; i < bp->b_page_count; i++) {
			struct page	*page = bp->b_pages[i];

			__free_page(page);
		}
	} else if (bp->b_flags & _XBF_KMEM)
		kmem_free(bp->b_addr);
	_xfs_buf_free_pages(bp);
	xfs_buf_free_maps(bp);
	kmem_zone_free(xfs_buf_zone, bp);
}

/*
 * Allocates all the pages for buffer in question and builds it's page list.
 */
STATIC int
xfs_buf_allocate_memory(
	xfs_buf_t		*bp,
	uint			flags)
{
	size_t			size;
	size_t			nbytes, offset;
	gfp_t			gfp_mask = xb_to_gfp(flags);
	unsigned short		page_count, i;
	xfs_off_t		start, end;
	int			error;

	/*
	 * for buffers that are contained within a single page, just allocate
	 * the memory from the heap - there's no need for the complexity of
	 * page arrays to keep allocation down to order 0.
	 */
	size = BBTOB(bp->b_length);
	if (size < PAGE_SIZE) {
		bp->b_addr = kmem_alloc(size, KM_NOFS);
		if (!bp->b_addr) {
			/* low memory - use alloc_page loop instead */
			goto use_alloc_page;
		}

		if (((unsigned long)(bp->b_addr + size - 1) & PAGE_MASK) !=
		    ((unsigned long)bp->b_addr & PAGE_MASK)) {
			/* b_addr spans two pages - use alloc_page instead */
			kmem_free(bp->b_addr);
			bp->b_addr = NULL;
			goto use_alloc_page;
		}
		bp->b_offset = offset_in_page(bp->b_addr);
		bp->b_pages = bp->b_page_array;
		bp->b_pages[0] = virt_to_page(bp->b_addr);
		bp->b_page_count = 1;
		bp->b_flags |= _XBF_KMEM;
		return 0;
	}

use_alloc_page:
	start = BBTOB(bp->b_maps[0].bm_bn) >> PAGE_SHIFT;
	end = (BBTOB(bp->b_maps[0].bm_bn + bp->b_length) + PAGE_SIZE - 1)
								>> PAGE_SHIFT;
	page_count = end - start;
	error = _xfs_buf_get_pages(bp, page_count);
	if (unlikely(error))
		return error;

	offset = bp->b_offset;
	bp->b_flags |= _XBF_PAGES;

	for (i = 0; i < bp->b_page_count; i++) {
		struct page	*page;
		uint		retries = 0;
retry:
		page = alloc_page(gfp_mask);
		if (unlikely(page == NULL)) {
			if (flags & XBF_READ_AHEAD) {
				bp->b_page_count = i;
				error = -ENOMEM;
				goto out_free_pages;
			}

			/*
			 * This could deadlock.
			 *
			 * But until all the XFS lowlevel code is revamped to
			 * handle buffer allocation failures we can't do much.
			 */
			if (!(++retries % 100))
				xfs_err(NULL,
		"%s(%u) possible memory allocation deadlock in %s (mode:0x%x)",
					current->comm, current->pid,
					__func__, gfp_mask);

			XFS_STATS_INC(bp->b_target->bt_mount, xb_page_retries);
			congestion_wait(BLK_RW_ASYNC, HZ/50);
			goto retry;
		}

		XFS_STATS_INC(bp->b_target->bt_mount, xb_page_found);

		nbytes = min_t(size_t, size, PAGE_SIZE - offset);
		size -= nbytes;
		bp->b_pages[i] = page;
		offset = 0;
	}
	return 0;

out_free_pages:
	for (i = 0; i < bp->b_page_count; i++)
		__free_page(bp->b_pages[i]);
	bp->b_flags &= ~_XBF_PAGES;
	return error;
}

/*
 *	Map buffer into kernel address-space if necessary.
 */
STATIC int
_xfs_buf_map_pages(
	xfs_buf_t		*bp,
	uint			flags)
{
	ASSERT(bp->b_flags & _XBF_PAGES);
	if (bp->b_page_count == 1) {
		/* A single page buffer is always mappable */
		bp->b_addr = page_address(bp->b_pages[0]) + bp->b_offset;
	} else if (flags & XBF_UNMAPPED) {
		bp->b_addr = NULL;
	} else {
		int retried = 0;
		unsigned nofs_flag;

		/*
		 * vm_map_ram() will allocate auxillary structures (e.g.
		 * pagetables) with GFP_KERNEL, yet we are likely to be under
		 * GFP_NOFS context here. Hence we need to tell memory reclaim
		 * that we are in such a context via PF_MEMALLOC_NOFS to prevent
		 * memory reclaim re-entering the filesystem here and
		 * potentially deadlocking.
		 */
		nofs_flag = memalloc_nofs_save();
		do {
			bp->b_addr = vm_map_ram(bp->b_pages, bp->b_page_count,
						-1, PAGE_KERNEL);
			if (bp->b_addr)
				break;
			vm_unmap_aliases();
		} while (retried++ <= 1);
		memalloc_nofs_restore(nofs_flag);

		if (!bp->b_addr)
			return -ENOMEM;
		bp->b_addr += bp->b_offset;
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

	if (bp->b_bn != map->bm_bn)
		return 1;

	if (unlikely(bp->b_length != map->bm_len)) {
		/*
		 * found a block number match. If the range doesn't
		 * match, the only way this is allowed is if the buffer
		 * in the cache is stale and the transaction that made
		 * it stale has not yet committed. i.e. we are
		 * reallocating a busy extent. Skip this buffer and
		 * continue searching for an exact match.
		 */
		ASSERT(bp->b_flags & XBF_STALE);
		return 1;
	}
	return 0;
}

static const struct rhashtable_params xfs_buf_hash_params = {
	.min_size		= 32,	/* empty AGs have minimal footprint */
	.nelem_hint		= 16,
	.key_len		= sizeof(xfs_daddr_t),
	.key_offset		= offsetof(struct xfs_buf, b_bn),
	.head_offset		= offsetof(struct xfs_buf, b_rhash_head),
	.automatic_shrinking	= true,
	.obj_cmpfn		= _xfs_buf_obj_cmp,
};

int
xfs_buf_hash_init(
	struct xfs_perag	*pag)
{
	spin_lock_init(&pag->pag_buf_lock);
	return rhashtable_init(&pag->pag_buf_hash, &xfs_buf_hash_params);
}

void
xfs_buf_hash_destroy(
	struct xfs_perag	*pag)
{
	rhashtable_destroy(&pag->pag_buf_hash);
}

/*
 * Look up a buffer in the buffer cache and return it referenced and locked
 * in @found_bp.
 *
 * If @new_bp is supplied and we have a lookup miss, insert @new_bp into the
 * cache.
 *
 * If XBF_TRYLOCK is set in @flags, only try to lock the buffer and return
 * -EAGAIN if we fail to lock it.
 *
 * Return values are:
 *	-EFSCORRUPTED if have been supplied with an invalid address
 *	-EAGAIN on trylock failure
 *	-ENOENT if we fail to find a match and @new_bp was NULL
 *	0, with @found_bp:
 *		- @new_bp if we inserted it into the cache
 *		- the buffer we found and locked.
 */
static int
xfs_buf_find(
	struct xfs_buftarg	*btp,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags,
	struct xfs_buf		*new_bp,
	struct xfs_buf		**found_bp)
{
	struct xfs_perag	*pag;
	xfs_buf_t		*bp;
	struct xfs_buf_map	cmap = { .bm_bn = map[0].bm_bn };
	xfs_daddr_t		eofs;
	int			i;

	*found_bp = NULL;

	for (i = 0; i < nmaps; i++)
		cmap.bm_len += map[i].bm_len;

	/* Check for IOs smaller than the sector size / not sector aligned */
	ASSERT(!(BBTOB(cmap.bm_len) < btp->bt_meta_sectorsize));
	ASSERT(!(BBTOB(cmap.bm_bn) & (xfs_off_t)btp->bt_meta_sectormask));

	/*
	 * Corrupted block numbers can get through to here, unfortunately, so we
	 * have to check that the buffer falls within the filesystem bounds.
	 */
	eofs = XFS_FSB_TO_BB(btp->bt_mount, btp->bt_mount->m_sb.sb_dblocks);
	if (cmap.bm_bn < 0 || cmap.bm_bn >= eofs) {
		xfs_alert(btp->bt_mount,
			  "%s: daddr 0x%llx out of range, EOFS 0x%llx",
			  __func__, cmap.bm_bn, eofs);
		WARN_ON(1);
		return -EFSCORRUPTED;
	}

	pag = xfs_perag_get(btp->bt_mount,
			    xfs_daddr_to_agno(btp->bt_mount, cmap.bm_bn));

	spin_lock(&pag->pag_buf_lock);
	bp = rhashtable_lookup_fast(&pag->pag_buf_hash, &cmap,
				    xfs_buf_hash_params);
	if (bp) {
		atomic_inc(&bp->b_hold);
		goto found;
	}

	/* No match found */
	if (!new_bp) {
		XFS_STATS_INC(btp->bt_mount, xb_miss_locked);
		spin_unlock(&pag->pag_buf_lock);
		xfs_perag_put(pag);
		return -ENOENT;
	}

	/* the buffer keeps the perag reference until it is freed */
	new_bp->b_pag = pag;
	rhashtable_insert_fast(&pag->pag_buf_hash, &new_bp->b_rhash_head,
			       xfs_buf_hash_params);
	spin_unlock(&pag->pag_buf_lock);
	*found_bp = new_bp;
	return 0;

found:
	spin_unlock(&pag->pag_buf_lock);
	xfs_perag_put(pag);

	if (!xfs_buf_trylock(bp)) {
		if (flags & XBF_TRYLOCK) {
			xfs_buf_rele(bp);
			XFS_STATS_INC(btp->bt_mount, xb_busy_locked);
			return -EAGAIN;
		}
		xfs_buf_lock(bp);
		XFS_STATS_INC(btp->bt_mount, xb_get_locked_waited);
	}

	/*
	 * if the buffer is stale, clear all the external state associated with
	 * it. We need to keep flags such as how we allocated the buffer memory
	 * intact here.
	 */
	if (bp->b_flags & XBF_STALE) {
		ASSERT((bp->b_flags & _XBF_DELWRI_Q) == 0);
		ASSERT(bp->b_iodone == NULL);
		bp->b_flags &= _XBF_KMEM | _XBF_PAGES;
		bp->b_ops = NULL;
	}

	trace_xfs_buf_find(bp, flags, _RET_IP_);
	XFS_STATS_INC(btp->bt_mount, xb_get_locked);
	*found_bp = bp;
	return 0;
}

struct xfs_buf *
xfs_buf_incore(
	struct xfs_buftarg	*target,
	xfs_daddr_t		blkno,
	size_t			numblks,
	xfs_buf_flags_t		flags)
{
	struct xfs_buf		*bp;
	int			error;
	DEFINE_SINGLE_BUF_MAP(map, blkno, numblks);

	error = xfs_buf_find(target, &map, 1, flags, NULL, &bp);
	if (error)
		return NULL;
	return bp;
}

/*
 * Assembles a buffer covering the specified range. The code is optimised for
 * cache hits, as metadata intensive workloads will see 3 orders of magnitude
 * more hits than misses.
 */
struct xfs_buf *
xfs_buf_get_map(
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags)
{
	struct xfs_buf		*bp;
	struct xfs_buf		*new_bp;
	int			error = 0;

	error = xfs_buf_find(target, map, nmaps, flags, NULL, &bp);

	switch (error) {
	case 0:
		/* cache hit */
		goto found;
	case -EAGAIN:
		/* cache hit, trylock failure, caller handles failure */
		ASSERT(flags & XBF_TRYLOCK);
		return NULL;
	case -ENOENT:
		/* cache miss, go for insert */
		break;
	case -EFSCORRUPTED:
	default:
		/*
		 * None of the higher layers understand failure types
		 * yet, so return NULL to signal a fatal lookup error.
		 */
		return NULL;
	}

	new_bp = _xfs_buf_alloc(target, map, nmaps, flags);
	if (unlikely(!new_bp))
		return NULL;

	error = xfs_buf_allocate_memory(new_bp, flags);
	if (error) {
		xfs_buf_free(new_bp);
		return NULL;
	}

	error = xfs_buf_find(target, map, nmaps, flags, new_bp, &bp);
	if (error) {
		xfs_buf_free(new_bp);
		return NULL;
	}

	if (bp != new_bp)
		xfs_buf_free(new_bp);

found:
	if (!bp->b_addr) {
		error = _xfs_buf_map_pages(bp, flags);
		if (unlikely(error)) {
			xfs_warn(target->bt_mount,
				"%s: failed to map pagesn", __func__);
			xfs_buf_relse(bp);
			return NULL;
		}
	}

	/*
	 * Clear b_error if this is a lookup from a caller that doesn't expect
	 * valid data to be found in the buffer.
	 */
	if (!(flags & XBF_READ))
		xfs_buf_ioerror(bp, 0);

	XFS_STATS_INC(target->bt_mount, xb_get);
	trace_xfs_buf_get(bp, flags, _RET_IP_);
	return bp;
}

STATIC int
_xfs_buf_read(
	xfs_buf_t		*bp,
	xfs_buf_flags_t		flags)
{
	ASSERT(!(flags & XBF_WRITE));
	ASSERT(bp->b_maps[0].bm_bn != XFS_BUF_DADDR_NULL);

	bp->b_flags &= ~(XBF_WRITE | XBF_ASYNC | XBF_READ_AHEAD);
	bp->b_flags |= flags & (XBF_READ | XBF_ASYNC | XBF_READ_AHEAD);

	return xfs_buf_submit(bp);
}

/*
 * Set buffer ops on an unchecked buffer and validate it, if possible.
 *
 * If the caller passed in an ops structure and the buffer doesn't have ops
 * assigned, set the ops and use them to verify the contents.  If the contents
 * cannot be verified, we'll clear XBF_DONE.  We assume the buffer has no
 * recorded errors and is already in XBF_DONE state.
 *
 * Under normal operations, every in-core buffer must have buffer ops assigned
 * to them when the buffer is read in from disk so that we can validate the
 * metadata.
 *
 * However, there are two scenarios where one can encounter in-core buffers
 * that don't have buffer ops.  The first is during log recovery of buffers on
 * a V4 filesystem, though these buffers are purged at the end of recovery.
 *
 * The other is online repair, which tries to match arbitrary metadata blocks
 * with btree types in order to find the root.  If online repair doesn't match
 * the buffer with /any/ btree type, the buffer remains in memory in DONE state
 * with no ops, and a subsequent read_buf call from elsewhere will not set the
 * ops.  This function helps us fix this situation.
 */
int
xfs_buf_ensure_ops(
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

xfs_buf_t *
xfs_buf_read_map(
	struct xfs_buftarg	*target,
	struct xfs_buf_map	*map,
	int			nmaps,
	xfs_buf_flags_t		flags,
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp;

	flags |= XBF_READ;

	bp = xfs_buf_get_map(target, map, nmaps, flags);
	if (!bp)
		return NULL;

	trace_xfs_buf_read(bp, flags, _RET_IP_);

	if (!(bp->b_flags & XBF_DONE)) {
		XFS_STATS_INC(target->bt_mount, xb_get_read);
		bp->b_ops = ops;
		_xfs_buf_read(bp, flags);
		return bp;
	}

	xfs_buf_ensure_ops(bp, ops);

	if (flags & XBF_ASYNC) {
		/*
		 * Read ahead call which is already satisfied,
		 * drop the buffer
		 */
		xfs_buf_relse(bp);
		return NULL;
	}

	/* We do not want read in the flags */
	bp->b_flags &= ~XBF_READ;
	ASSERT(bp->b_ops != NULL || ops == NULL);
	return bp;
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
	if (bdi_read_congested(target->bt_bdev->bd_bdi))
		return;

	xfs_buf_read_map(target, map, nmaps,
		     XBF_TRYLOCK|XBF_ASYNC|XBF_READ_AHEAD, ops);
}

/*
 * Read an uncached buffer from disk. Allocates and returns a locked
 * buffer containing the disk contents or nothing.
 */
int
xfs_buf_read_uncached(
	struct xfs_buftarg	*target,
	xfs_daddr_t		daddr,
	size_t			numblks,
	int			flags,
	struct xfs_buf		**bpp,
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp;

	*bpp = NULL;

	bp = xfs_buf_get_uncached(target, numblks, flags);
	if (!bp)
		return -ENOMEM;

	/* set up the buffer for a read IO */
	ASSERT(bp->b_map_count == 1);
	bp->b_bn = XFS_BUF_DADDR_NULL;  /* always null for uncached buffers */
	bp->b_maps[0].bm_bn = daddr;
	bp->b_flags |= XBF_READ;
	bp->b_ops = ops;

	xfs_buf_submit(bp);
	if (bp->b_error) {
		int	error = bp->b_error;
		xfs_buf_relse(bp);
		return error;
	}

	*bpp = bp;
	return 0;
}

/*
 * Return a buffer allocated as an empty buffer and associated to external
 * memory via xfs_buf_associate_memory() back to it's empty state.
 */
void
xfs_buf_set_empty(
	struct xfs_buf		*bp,
	size_t			numblks)
{
	if (bp->b_pages)
		_xfs_buf_free_pages(bp);

	bp->b_pages = NULL;
	bp->b_page_count = 0;
	bp->b_addr = NULL;
	bp->b_length = numblks;
	bp->b_io_length = numblks;

	ASSERT(bp->b_map_count == 1);
	bp->b_bn = XFS_BUF_DADDR_NULL;
	bp->b_maps[0].bm_bn = XFS_BUF_DADDR_NULL;
	bp->b_maps[0].bm_len = bp->b_length;
}

static inline struct page *
mem_to_page(
	void			*addr)
{
	if ((!is_vmalloc_addr(addr))) {
		return virt_to_page(addr);
	} else {
		return vmalloc_to_page(addr);
	}
}

int
xfs_buf_associate_memory(
	xfs_buf_t		*bp,
	void			*mem,
	size_t			len)
{
	int			rval;
	int			i = 0;
	unsigned long		pageaddr;
	unsigned long		offset;
	size_t			buflen;
	int			page_count;

	pageaddr = (unsigned long)mem & PAGE_MASK;
	offset = (unsigned long)mem - pageaddr;
	buflen = PAGE_ALIGN(len + offset);
	page_count = buflen >> PAGE_SHIFT;

	/* Free any previous set of page pointers */
	if (bp->b_pages)
		_xfs_buf_free_pages(bp);

	bp->b_pages = NULL;
	bp->b_addr = mem;

	rval = _xfs_buf_get_pages(bp, page_count);
	if (rval)
		return rval;

	bp->b_offset = offset;

	for (i = 0; i < bp->b_page_count; i++) {
		bp->b_pages[i] = mem_to_page((void *)pageaddr);
		pageaddr += PAGE_SIZE;
	}

	bp->b_io_length = BTOBB(len);
	bp->b_length = BTOBB(buflen);

	return 0;
}

xfs_buf_t *
xfs_buf_get_uncached(
	struct xfs_buftarg	*target,
	size_t			numblks,
	int			flags)
{
	unsigned long		page_count;
	int			error, i;
	struct xfs_buf		*bp;
	DEFINE_SINGLE_BUF_MAP(map, XFS_BUF_DADDR_NULL, numblks);

	/* flags might contain irrelevant bits, pass only what we care about */
	bp = _xfs_buf_alloc(target, &map, 1, flags & XBF_NO_IOACCT);
	if (unlikely(bp == NULL))
		goto fail;

	page_count = PAGE_ALIGN(numblks << BBSHIFT) >> PAGE_SHIFT;
	error = _xfs_buf_get_pages(bp, page_count);
	if (error)
		goto fail_free_buf;

	for (i = 0; i < page_count; i++) {
		bp->b_pages[i] = alloc_page(xb_to_gfp(flags));
		if (!bp->b_pages[i])
			goto fail_free_mem;
	}
	bp->b_flags |= _XBF_PAGES;

	error = _xfs_buf_map_pages(bp, 0);
	if (unlikely(error)) {
		xfs_warn(target->bt_mount,
			"%s: failed to map pages", __func__);
		goto fail_free_mem;
	}

	trace_xfs_buf_get_uncached(bp, _RET_IP_);
	return bp;

 fail_free_mem:
	while (--i >= 0)
		__free_page(bp->b_pages[i]);
	_xfs_buf_free_pages(bp);
 fail_free_buf:
	xfs_buf_free_maps(bp);
	kmem_zone_free(xfs_buf_zone, bp);
 fail:
	return NULL;
}

/*
 *	Increment reference count on buffer, to hold the buffer concurrently
 *	with another thread which may release (free) the buffer asynchronously.
 *	Must hold the buffer already to call this function.
 */
void
xfs_buf_hold(
	xfs_buf_t		*bp)
{
	trace_xfs_buf_hold(bp, _RET_IP_);
	atomic_inc(&bp->b_hold);
}

/*
 * Release a hold on the specified buffer. If the hold count is 1, the buffer is
 * placed on LRU or freed (depending on b_lru_ref).
 */
void
xfs_buf_rele(
	xfs_buf_t		*bp)
{
	struct xfs_perag	*pag = bp->b_pag;
	bool			release;
	bool			freebuf = false;

	trace_xfs_buf_rele(bp, _RET_IP_);

	if (!pag) {
		ASSERT(list_empty(&bp->b_lru));
		if (atomic_dec_and_test(&bp->b_hold)) {
			xfs_buf_ioacct_dec(bp);
			xfs_buf_free(bp);
		}
		return;
	}

	ASSERT(atomic_read(&bp->b_hold) > 0);

	/*
	 * We grab the b_lock here first to serialise racing xfs_buf_rele()
	 * calls. The pag_buf_lock being taken on the last reference only
	 * serialises against racing lookups in xfs_buf_find(). IOWs, the second
	 * to last reference we drop here is not serialised against the last
	 * reference until we take bp->b_lock. Hence if we don't grab b_lock
	 * first, the last "release" reference can win the race to the lock and
	 * free the buffer before the second-to-last reference is processed,
	 * leading to a use-after-free scenario.
	 */
	spin_lock(&bp->b_lock);
	release = atomic_dec_and_lock(&bp->b_hold, &pag->pag_buf_lock);
	if (!release) {
		/*
		 * Drop the in-flight state if the buffer is already on the LRU
		 * and it holds the only reference. This is racy because we
		 * haven't acquired the pag lock, but the use of _XBF_IN_FLIGHT
		 * ensures the decrement occurs only once per-buf.
		 */
		if ((atomic_read(&bp->b_hold) == 1) && !list_empty(&bp->b_lru))
			__xfs_buf_ioacct_dec(bp);
		goto out_unlock;
	}

	/* the last reference has been dropped ... */
	__xfs_buf_ioacct_dec(bp);
	if (!(bp->b_flags & XBF_STALE) && atomic_read(&bp->b_lru_ref)) {
		/*
		 * If the buffer is added to the LRU take a new reference to the
		 * buffer for the LRU and clear the (now stale) dispose list
		 * state flag
		 */
		if (list_lru_add(&bp->b_target->bt_lru, &bp->b_lru)) {
			bp->b_state &= ~XFS_BSTATE_DISPOSE;
			atomic_inc(&bp->b_hold);
		}
		spin_unlock(&pag->pag_buf_lock);
	} else {
		/*
		 * most of the time buffers will already be removed from the
		 * LRU, so optimise that case by checking for the
		 * XFS_BSTATE_DISPOSE flag indicating the last list the buffer
		 * was on was the disposal list
		 */
		if (!(bp->b_state & XFS_BSTATE_DISPOSE)) {
			list_lru_del(&bp->b_target->bt_lru, &bp->b_lru);
		} else {
			ASSERT(list_empty(&bp->b_lru));
		}

		ASSERT(!(bp->b_flags & _XBF_DELWRI_Q));
		rhashtable_remove_fast(&pag->pag_buf_hash, &bp->b_rhash_head,
				       xfs_buf_hash_params);
		spin_unlock(&pag->pag_buf_lock);
		xfs_perag_put(pag);
		freebuf = true;
	}

out_unlock:
	spin_unlock(&bp->b_lock);

	if (freebuf)
		xfs_buf_free(bp);
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
		xfs_log_force(bp->b_target->bt_mount, 0);
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
	xfs_buf_t		*bp)
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

/*
 *	Buffer Utility Routines
 */

void
xfs_buf_ioend(
	struct xfs_buf	*bp)
{
	bool		read = bp->b_flags & XBF_READ;

	trace_xfs_buf_iodone(bp, _RET_IP_);

	bp->b_flags &= ~(XBF_READ | XBF_WRITE | XBF_READ_AHEAD);

	/*
	 * Pull in IO completion errors now. We are guaranteed to be running
	 * single threaded, so we don't need the lock to read b_io_error.
	 */
	if (!bp->b_error && bp->b_io_error)
		xfs_buf_ioerror(bp, bp->b_io_error);

	/* Only validate buffers that were read without errors */
	if (read && !bp->b_error && bp->b_ops) {
		ASSERT(!bp->b_iodone);
		bp->b_ops->verify_read(bp);
	}

	if (!bp->b_error)
		bp->b_flags |= XBF_DONE;

	if (bp->b_iodone)
		(*(bp->b_iodone))(bp);
	else if (bp->b_flags & XBF_ASYNC)
		xfs_buf_relse(bp);
	else
		complete(&bp->b_iowait);
}

static void
xfs_buf_ioend_work(
	struct work_struct	*work)
{
	struct xfs_buf		*bp =
		container_of(work, xfs_buf_t, b_ioend_work);

	xfs_buf_ioend(bp);
}

static void
xfs_buf_ioend_async(
	struct xfs_buf	*bp)
{
	INIT_WORK(&bp->b_ioend_work, xfs_buf_ioend_work);
	queue_work(bp->b_ioend_wq, &bp->b_ioend_work);
}

void
__xfs_buf_ioerror(
	xfs_buf_t		*bp,
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
	const char		*func)
{
	xfs_alert(bp->b_target->bt_mount,
"metadata I/O error in \"%s\" at daddr 0x%llx len %d error %d",
			func, (uint64_t)XFS_BUF_ADDR(bp), bp->b_length,
			-bp->b_error);
}

int
xfs_bwrite(
	struct xfs_buf		*bp)
{
	int			error;

	ASSERT(xfs_buf_islocked(bp));

	bp->b_flags |= XBF_WRITE;
	bp->b_flags &= ~(XBF_ASYNC | XBF_READ | _XBF_DELWRI_Q |
			 XBF_WRITE_FAIL | XBF_DONE);

	error = xfs_buf_submit(bp);
	if (error) {
		xfs_force_shutdown(bp->b_target->bt_mount,
				   SHUTDOWN_META_IO_ERROR);
	}
	return error;
}

static void
xfs_buf_bio_end_io(
	struct bio		*bio)
{
	struct xfs_buf		*bp = (struct xfs_buf *)bio->bi_private;

	/*
	 * don't overwrite existing errors - otherwise we can lose errors on
	 * buffers that require multiple bios to complete.
	 */
	if (bio->bi_status) {
		int error = blk_status_to_errno(bio->bi_status);

		cmpxchg(&bp->b_io_error, 0, error);
	}

	if (!bp->b_error && xfs_buf_is_vmapped(bp) && (bp->b_flags & XBF_READ))
		invalidate_kernel_vmap_range(bp->b_addr, xfs_buf_vmap_len(bp));

	if (atomic_dec_and_test(&bp->b_io_remaining) == 1)
		xfs_buf_ioend_async(bp);
	bio_put(bio);
}

static void
xfs_buf_ioapply_map(
	struct xfs_buf	*bp,
	int		map,
	int		*buf_offset,
	int		*count,
	int		op,
	int		op_flags)
{
	int		page_index;
	int		total_nr_pages = bp->b_page_count;
	int		nr_pages;
	struct bio	*bio;
	sector_t	sector =  bp->b_maps[map].bm_bn;
	int		size;
	int		offset;

	/* skip the pages in the buffer before the start offset */
	page_index = 0;
	offset = *buf_offset;
	while (offset >= PAGE_SIZE) {
		page_index++;
		offset -= PAGE_SIZE;
	}

	/*
	 * Limit the IO size to the length of the current vector, and update the
	 * remaining IO count for the next time around.
	 */
	size = min_t(int, BBTOB(bp->b_maps[map].bm_len), *count);
	*count -= size;
	*buf_offset += size;

next_chunk:
	atomic_inc(&bp->b_io_remaining);
	nr_pages = min(total_nr_pages, BIO_MAX_PAGES);

	bio = bio_alloc(GFP_NOIO, nr_pages);
	bio_set_dev(bio, bp->b_target->bt_bdev);
	bio->bi_iter.bi_sector = sector;
	bio->bi_end_io = xfs_buf_bio_end_io;
	bio->bi_private = bp;
	bio_set_op_attrs(bio, op, op_flags);

	for (; size && nr_pages; nr_pages--, page_index++) {
		int	rbytes, nbytes = PAGE_SIZE - offset;

		if (nbytes > size)
			nbytes = size;

		rbytes = bio_add_page(bio, bp->b_pages[page_index], nbytes,
				      offset);
		if (rbytes < nbytes)
			break;

		offset = 0;
		sector += BTOBB(nbytes);
		size -= nbytes;
		total_nr_pages--;
	}

	if (likely(bio->bi_iter.bi_size)) {
		if (xfs_buf_is_vmapped(bp)) {
			flush_kernel_vmap_range(bp->b_addr,
						xfs_buf_vmap_len(bp));
		}
		submit_bio(bio);
		if (size)
			goto next_chunk;
	} else {
		/*
		 * This is guaranteed not to be the last io reference count
		 * because the caller (xfs_buf_submit) holds a count itself.
		 */
		atomic_dec(&bp->b_io_remaining);
		xfs_buf_ioerror(bp, -EIO);
		bio_put(bio);
	}

}

STATIC void
_xfs_buf_ioapply(
	struct xfs_buf	*bp)
{
	struct blk_plug	plug;
	int		op;
	int		op_flags = 0;
	int		offset;
	int		size;
	int		i;

	/*
	 * Make sure we capture only current IO errors rather than stale errors
	 * left over from previous use of the buffer (e.g. failed readahead).
	 */
	bp->b_error = 0;

	/*
	 * Initialize the I/O completion workqueue if we haven't yet or the
	 * submitter has not opted to specify a custom one.
	 */
	if (!bp->b_ioend_wq)
		bp->b_ioend_wq = bp->b_target->bt_mount->m_buf_workqueue;

	if (bp->b_flags & XBF_WRITE) {
		op = REQ_OP_WRITE;
		if (bp->b_flags & XBF_SYNCIO)
			op_flags = REQ_SYNC;
		if (bp->b_flags & XBF_FUA)
			op_flags |= REQ_FUA;
		if (bp->b_flags & XBF_FLUSH)
			op_flags |= REQ_PREFLUSH;

		/*
		 * Run the write verifier callback function if it exists. If
		 * this function fails it will mark the buffer with an error and
		 * the IO should not be dispatched.
		 */
		if (bp->b_ops) {
			bp->b_ops->verify_write(bp);
			if (bp->b_error) {
				xfs_force_shutdown(bp->b_target->bt_mount,
						   SHUTDOWN_CORRUPT_INCORE);
				return;
			}
		} else if (bp->b_bn != XFS_BUF_DADDR_NULL) {
			struct xfs_mount *mp = bp->b_target->bt_mount;

			/*
			 * non-crc filesystems don't attach verifiers during
			 * log recovery, so don't warn for such filesystems.
			 */
			if (xfs_sb_version_hascrc(&mp->m_sb)) {
				xfs_warn(mp,
					"%s: no buf ops on daddr 0x%llx len %d",
					__func__, bp->b_bn, bp->b_length);
				xfs_hex_dump(bp->b_addr,
						XFS_CORRUPTION_DUMP_LEN);
				dump_stack();
			}
		}
	} else if (bp->b_flags & XBF_READ_AHEAD) {
		op = REQ_OP_READ;
		op_flags = REQ_RAHEAD;
	} else {
		op = REQ_OP_READ;
	}

	/* we only use the buffer cache for meta-data */
	op_flags |= REQ_META;

	/*
	 * Walk all the vectors issuing IO on them. Set up the initial offset
	 * into the buffer and the desired IO size before we start -
	 * _xfs_buf_ioapply_vec() will modify them appropriately for each
	 * subsequent call.
	 */
	offset = bp->b_offset;
	size = BBTOB(bp->b_io_length);
	blk_start_plug(&plug);
	for (i = 0; i < bp->b_map_count; i++) {
		xfs_buf_ioapply_map(bp, i, &offset, &size, op, op_flags);
		if (bp->b_error)
			break;
		if (size <= 0)
			break;	/* all done */
	}
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

	trace_xfs_buf_iowait(bp, _RET_IP_);
	wait_for_completion(&bp->b_iowait);
	trace_xfs_buf_iowait_done(bp, _RET_IP_);

	return bp->b_error;
}

/*
 * Buffer I/O submission path, read or write. Asynchronous submission transfers
 * the buffer lock ownership and the current reference to the IO. It is not
 * safe to reference the buffer after a call to this function unless the caller
 * holds an additional reference itself.
 */
int
__xfs_buf_submit(
	struct xfs_buf	*bp,
	bool		wait)
{
	int		error = 0;

	trace_xfs_buf_submit(bp, _RET_IP_);

	ASSERT(!(bp->b_flags & _XBF_DELWRI_Q));

	/* on shutdown we stale and complete the buffer immediately */
	if (XFS_FORCED_SHUTDOWN(bp->b_target->bt_mount)) {
		xfs_buf_ioerror(bp, -EIO);
		bp->b_flags &= ~XBF_DONE;
		xfs_buf_stale(bp);
		xfs_buf_ioend(bp);
		return -EIO;
	}

	/*
	 * Grab a reference so the buffer does not go away underneath us. For
	 * async buffers, I/O completion drops the callers reference, which
	 * could occur before submission returns.
	 */
	xfs_buf_hold(bp);

	if (bp->b_flags & XBF_WRITE)
		xfs_buf_wait_unpin(bp);

	/* clear the internal error state to avoid spurious errors */
	bp->b_io_error = 0;

	/*
	 * Set the count to 1 initially, this will stop an I/O completion
	 * callout which happens before we have started all the I/O from calling
	 * xfs_buf_ioend too early.
	 */
	atomic_set(&bp->b_io_remaining, 1);
	if (bp->b_flags & XBF_ASYNC)
		xfs_buf_ioacct_inc(bp);
	_xfs_buf_ioapply(bp);

	/*
	 * If _xfs_buf_ioapply failed, we can get back here with only the IO
	 * reference we took above. If we drop it to zero, run completion so
	 * that we don't return to the caller with completion still pending.
	 */
	if (atomic_dec_and_test(&bp->b_io_remaining) == 1) {
		if (bp->b_error || !(bp->b_flags & XBF_ASYNC))
			xfs_buf_ioend(bp);
		else
			xfs_buf_ioend_async(bp);
	}

	if (wait)
		error = xfs_buf_iowait(bp);

	/*
	 * Release the hold that keeps the buffer referenced for the entire
	 * I/O. Note that if the buffer is async, it is not safe to reference
	 * after this release.
	 */
	xfs_buf_rele(bp);
	return error;
}

void *
xfs_buf_offset(
	struct xfs_buf		*bp,
	size_t			offset)
{
	struct page		*page;

	if (bp->b_addr)
		return bp->b_addr + offset;

	offset += bp->b_offset;
	page = bp->b_pages[offset >> PAGE_SHIFT];
	return page_address(page) + (offset & (PAGE_SIZE-1));
}

/*
 *	Move data into or out of a buffer.
 */
void
xfs_buf_iomove(
	xfs_buf_t		*bp,	/* buffer to process		*/
	size_t			boff,	/* starting buffer offset	*/
	size_t			bsize,	/* length to copy		*/
	void			*data,	/* data address			*/
	xfs_buf_rw_t		mode)	/* read/write/zero flag		*/
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
				      BBTOB(bp->b_io_length) - boff);

		ASSERT((csize + page_offset) <= PAGE_SIZE);

		switch (mode) {
		case XBRW_ZERO:
			memset(page_address(page) + page_offset, 0, csize);
			break;
		case XBRW_READ:
			memcpy(data, page_address(page) + page_offset, csize);
			break;
		case XBRW_WRITE:
			memcpy(page_address(page) + page_offset, data, csize);
		}

		boff += csize;
		data += csize;
	}
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
xfs_buftarg_wait_rele(
	struct list_head	*item,
	struct list_lru_one	*lru,
	spinlock_t		*lru_lock,
	void			*arg)

{
	struct xfs_buf		*bp = container_of(item, struct xfs_buf, b_lru);
	struct list_head	*dispose = arg;

	if (atomic_read(&bp->b_hold) > 1) {
		/* need to wait, so skip it this pass */
		trace_xfs_buf_wait_buftarg(bp, _RET_IP_);
		return LRU_SKIP;
	}
	if (!spin_trylock(&bp->b_lock))
		return LRU_SKIP;

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

void
xfs_wait_buftarg(
	struct xfs_buftarg	*btp)
{
	LIST_HEAD(dispose);
	int loop = 0;

	/*
	 * First wait on the buftarg I/O count for all in-flight buffers to be
	 * released. This is critical as new buffers do not make the LRU until
	 * they are released.
	 *
	 * Next, flush the buffer workqueue to ensure all completion processing
	 * has finished. Just waiting on buffer locks is not sufficient for
	 * async IO as the reference count held over IO is not released until
	 * after the buffer lock is dropped. Hence we need to ensure here that
	 * all reference counts have been dropped before we start walking the
	 * LRU list.
	 */
	while (percpu_counter_sum(&btp->bt_io_count))
		delay(100);
	flush_workqueue(btp->bt_mount->m_buf_workqueue);

	/* loop until there is nothing left on the lru list. */
	while (list_lru_count(&btp->bt_lru)) {
		list_lru_walk(&btp->bt_lru, xfs_buftarg_wait_rele,
			      &dispose, LONG_MAX);

		while (!list_empty(&dispose)) {
			struct xfs_buf *bp;
			bp = list_first_entry(&dispose, struct xfs_buf, b_lru);
			list_del_init(&bp->b_lru);
			if (bp->b_flags & XBF_WRITE_FAIL) {
				xfs_alert(btp->bt_mount,
"Corruption Alert: Buffer at daddr 0x%llx had permanent write failures!",
					(long long)bp->b_bn);
				xfs_alert(btp->bt_mount,
"Please run xfs_repair to determine the extent of the problem.");
			}
			xfs_buf_rele(bp);
		}
		if (loop++ != 0)
			delay(100);
	}
}

static enum lru_status
xfs_buftarg_isolate(
	struct list_head	*item,
	struct list_lru_one	*lru,
	spinlock_t		*lru_lock,
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
	struct xfs_buftarg	*btp = container_of(shrink,
					struct xfs_buftarg, bt_shrinker);
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
	struct xfs_buftarg	*btp = container_of(shrink,
					struct xfs_buftarg, bt_shrinker);
	return list_lru_shrink_count(&btp->bt_lru, sc);
}

void
xfs_free_buftarg(
	struct xfs_buftarg	*btp)
{
	unregister_shrinker(&btp->bt_shrinker);
	ASSERT(percpu_counter_sum(&btp->bt_io_count) == 0);
	percpu_counter_destroy(&btp->bt_io_count);
	list_lru_destroy(&btp->bt_lru);

	xfs_blkdev_issue_flush(btp);

	kmem_free(btp);
}

int
xfs_setsize_buftarg(
	xfs_buftarg_t		*btp,
	unsigned int		sectorsize)
{
	/* Set up metadata sector size info */
	btp->bt_meta_sectorsize = sectorsize;
	btp->bt_meta_sectormask = sectorsize - 1;

	if (set_blocksize(btp->bt_bdev, sectorsize)) {
		xfs_warn(btp->bt_mount,
			"Cannot set_blocksize to %u on device %pg",
			sectorsize, btp->bt_bdev);
		return -EINVAL;
	}

	/* Set up device logical sector size mask */
	btp->bt_logical_sectorsize = bdev_logical_block_size(btp->bt_bdev);
	btp->bt_logical_sectormask = bdev_logical_block_size(btp->bt_bdev) - 1;

	return 0;
}

/*
 * When allocating the initial buffer target we have not yet
 * read in the superblock, so don't know what sized sectors
 * are being used at this early stage.  Play safe.
 */
STATIC int
xfs_setsize_buftarg_early(
	xfs_buftarg_t		*btp,
	struct block_device	*bdev)
{
	return xfs_setsize_buftarg(btp, bdev_logical_block_size(bdev));
}

xfs_buftarg_t *
xfs_alloc_buftarg(
	struct xfs_mount	*mp,
	struct block_device	*bdev,
	struct dax_device	*dax_dev)
{
	xfs_buftarg_t		*btp;

	btp = kmem_zalloc(sizeof(*btp), KM_SLEEP | KM_NOFS);

	btp->bt_mount = mp;
	btp->bt_dev =  bdev->bd_dev;
	btp->bt_bdev = bdev;
	btp->bt_daxdev = dax_dev;

	if (xfs_setsize_buftarg_early(btp, bdev))
		goto error_free;

	if (list_lru_init(&btp->bt_lru))
		goto error_free;

	if (percpu_counter_init(&btp->bt_io_count, 0, GFP_KERNEL))
		goto error_lru;

	btp->bt_shrinker.count_objects = xfs_buftarg_shrink_count;
	btp->bt_shrinker.scan_objects = xfs_buftarg_shrink_scan;
	btp->bt_shrinker.seeks = DEFAULT_SEEKS;
	btp->bt_shrinker.flags = SHRINKER_NUMA_AWARE;
	if (register_shrinker(&btp->bt_shrinker))
		goto error_pcpu;
	return btp;

error_pcpu:
	percpu_counter_destroy(&btp->bt_io_count);
error_lru:
	list_lru_destroy(&btp->bt_lru);
error_free:
	kmem_free(btp);
	return NULL;
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
		list_del_init(&bp->b_list);
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
		atomic_inc(&bp->b_hold);
		list_add_tail(&bp->b_list, list);
	}

	return true;
}

/*
 * Compare function is more complex than it needs to be because
 * the return value is only 32 bits and we are doing comparisons
 * on 64 bit values
 */
static int
xfs_buf_cmp(
	void		*priv,
	struct list_head *a,
	struct list_head *b)
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

/*
 * Submit buffers for write. If wait_list is specified, the buffers are
 * submitted using sync I/O and placed on the wait list such that the caller can
 * iowait each buffer. Otherwise async I/O is used and the buffers are released
 * at I/O completion time. In either case, buffers remain locked until I/O
 * completes and the buffer is released from the queue.
 */
static int
xfs_buf_delwri_submit_buffers(
	struct list_head	*buffer_list,
	struct list_head	*wait_list)
{
	struct xfs_buf		*bp, *n;
	int			pinned = 0;
	struct blk_plug		plug;

	list_sort(NULL, buffer_list, xfs_buf_cmp);

	blk_start_plug(&plug);
	list_for_each_entry_safe(bp, n, buffer_list, b_list) {
		if (!wait_list) {
			if (xfs_buf_ispinned(bp)) {
				pinned++;
				continue;
			}
			if (!xfs_buf_trylock(bp))
				continue;
		} else {
			xfs_buf_lock(bp);
		}

		/*
		 * Someone else might have written the buffer synchronously or
		 * marked it stale in the meantime.  In that case only the
		 * _XBF_DELWRI_Q flag got cleared, and we have to drop the
		 * reference and remove it from the list here.
		 */
		if (!(bp->b_flags & _XBF_DELWRI_Q)) {
			list_del_init(&bp->b_list);
			xfs_buf_relse(bp);
			continue;
		}

		trace_xfs_buf_delwri_split(bp, _RET_IP_);

		/*
		 * If we have a wait list, each buffer (and associated delwri
		 * queue reference) transfers to it and is submitted
		 * synchronously. Otherwise, drop the buffer from the delwri
		 * queue and submit async.
		 */
		bp->b_flags &= ~(_XBF_DELWRI_Q | XBF_WRITE_FAIL);
		bp->b_flags |= XBF_WRITE;
		if (wait_list) {
			bp->b_flags &= ~XBF_ASYNC;
			list_move_tail(&bp->b_list, wait_list);
		} else {
			bp->b_flags |= XBF_ASYNC;
			list_del_init(&bp->b_list);
		}
		__xfs_buf_submit(bp, false);
	}
	blk_finish_plug(&plug);

	return pinned;
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
	return xfs_buf_delwri_submit_buffers(buffer_list, NULL);
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
	struct xfs_buf		*bp;

	xfs_buf_delwri_submit_buffers(buffer_list, &wait_list);

	/* Wait for IO to complete. */
	while (!list_empty(&wait_list)) {
		bp = list_first_entry(&wait_list, struct xfs_buf, b_list);

		list_del_init(&bp->b_list);

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
 * and return with the buffer still on the original queue. The waiting delwri
 * buffer submission infrastructure guarantees transfer of the delwri queue
 * buffer reference to a temporary wait list. We reuse this infrastructure to
 * transfer the buffer back to the original queue.
 *
 * Note the buffer transitions from the queued state, to the submitted and wait
 * listed state and back to the queued state during this call. The buffer
 * locking and queue management logic between _delwri_pushbuf() and
 * _delwri_queue() guarantee that the buffer cannot be queued to another list
 * before returning.
 */
int
xfs_buf_delwri_pushbuf(
	struct xfs_buf		*bp,
	struct list_head	*buffer_list)
{
	LIST_HEAD		(submit_list);
	int			error;

	ASSERT(bp->b_flags & _XBF_DELWRI_Q);

	trace_xfs_buf_delwri_pushbuf(bp, _RET_IP_);

	/*
	 * Isolate the buffer to a new local list so we can submit it for I/O
	 * independently from the rest of the original list.
	 */
	xfs_buf_lock(bp);
	list_move(&bp->b_list, &submit_list);
	xfs_buf_unlock(bp);

	/*
	 * Delwri submission clears the DELWRI_Q buffer flag and returns with
	 * the buffer on the wait list with the original reference. Rather than
	 * bounce the buffer from a local wait list back to the original list
	 * after I/O completion, reuse the original list as the wait list.
	 */
	xfs_buf_delwri_submit_buffers(&submit_list, buffer_list);

	/*
	 * The buffer is now locked, under I/O and wait listed on the original
	 * delwri queue. Wait for I/O completion, restore the DELWRI_Q flag and
	 * return with the buffer unlocked and on the original queue.
	 */
	error = xfs_buf_iowait(bp);
	bp->b_flags |= _XBF_DELWRI_Q;
	xfs_buf_unlock(bp);

	return error;
}

int __init
xfs_buf_init(void)
{
	xfs_buf_zone = kmem_zone_init_flags(sizeof(xfs_buf_t), "xfs_buf",
						KM_ZONE_HWALIGN, NULL);
	if (!xfs_buf_zone)
		goto out;

	return 0;

 out:
	return -ENOMEM;
}

void
xfs_buf_terminate(void)
{
	kmem_zone_destroy(xfs_buf_zone);
}

void xfs_buf_set_ref(struct xfs_buf *bp, int lru_ref)
{
	/*
	 * Set the lru reference count to 0 based on the error injection tag.
	 * This allows userspace to disrupt buffer caching for debug/testing
	 * purposes.
	 */
	if (XFS_TEST_ERROR(false, bp->b_target->bt_mount,
			   XFS_ERRTAG_BUF_LRU_REF))
		lru_ref = 0;

	atomic_set(&bp->b_lru_ref, lru_ref);
}
