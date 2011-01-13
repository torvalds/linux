/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include <linux/list_sort.h>

#include "xfs_sb.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_trace.h"

static kmem_zone_t *xfs_buf_zone;
STATIC int xfsbufd(void *);
STATIC void xfs_buf_delwri_queue(xfs_buf_t *, int);

static struct workqueue_struct *xfslogd_workqueue;
struct workqueue_struct *xfsdatad_workqueue;
struct workqueue_struct *xfsconvertd_workqueue;

#ifdef XFS_BUF_LOCK_TRACKING
# define XB_SET_OWNER(bp)	((bp)->b_last_holder = current->pid)
# define XB_CLEAR_OWNER(bp)	((bp)->b_last_holder = -1)
# define XB_GET_OWNER(bp)	((bp)->b_last_holder)
#else
# define XB_SET_OWNER(bp)	do { } while (0)
# define XB_CLEAR_OWNER(bp)	do { } while (0)
# define XB_GET_OWNER(bp)	do { } while (0)
#endif

#define xb_to_gfp(flags) \
	((((flags) & XBF_READ_AHEAD) ? __GFP_NORETRY : \
	  ((flags) & XBF_DONT_BLOCK) ? GFP_NOFS : GFP_KERNEL) | __GFP_NOWARN)

#define xb_to_km(flags) \
	 (((flags) & XBF_DONT_BLOCK) ? KM_NOFS : KM_SLEEP)

#define xfs_buf_allocate(flags) \
	kmem_zone_alloc(xfs_buf_zone, xb_to_km(flags))
#define xfs_buf_deallocate(bp) \
	kmem_zone_free(xfs_buf_zone, (bp));

static inline int
xfs_buf_is_vmapped(
	struct xfs_buf	*bp)
{
	/*
	 * Return true if the buffer is vmapped.
	 *
	 * The XBF_MAPPED flag is set if the buffer should be mapped, but the
	 * code is clever enough to know it doesn't have to map a single page,
	 * so the check has to be both for XBF_MAPPED and bp->b_page_count > 1.
	 */
	return (bp->b_flags & XBF_MAPPED) && bp->b_page_count > 1;
}

static inline int
xfs_buf_vmap_len(
	struct xfs_buf	*bp)
{
	return (bp->b_page_count * PAGE_SIZE) - bp->b_offset;
}

/*
 *	Page Region interfaces.
 *
 *	For pages in filesystems where the blocksize is smaller than the
 *	pagesize, we use the page->private field (long) to hold a bitmap
 * 	of uptodate regions within the page.
 *
 *	Each such region is "bytes per page / bits per long" bytes long.
 *
 *	NBPPR == number-of-bytes-per-page-region
 *	BTOPR == bytes-to-page-region (rounded up)
 *	BTOPRT == bytes-to-page-region-truncated (rounded down)
 */
#if (BITS_PER_LONG == 32)
#define PRSHIFT		(PAGE_CACHE_SHIFT - 5)	/* (32 == 1<<5) */
#elif (BITS_PER_LONG == 64)
#define PRSHIFT		(PAGE_CACHE_SHIFT - 6)	/* (64 == 1<<6) */
#else
#error BITS_PER_LONG must be 32 or 64
#endif
#define NBPPR		(PAGE_CACHE_SIZE/BITS_PER_LONG)
#define BTOPR(b)	(((unsigned int)(b) + (NBPPR - 1)) >> PRSHIFT)
#define BTOPRT(b)	(((unsigned int)(b) >> PRSHIFT))

STATIC unsigned long
page_region_mask(
	size_t		offset,
	size_t		length)
{
	unsigned long	mask;
	int		first, final;

	first = BTOPR(offset);
	final = BTOPRT(offset + length - 1);
	first = min(first, final);

	mask = ~0UL;
	mask <<= BITS_PER_LONG - (final - first);
	mask >>= BITS_PER_LONG - (final);

	ASSERT(offset + length <= PAGE_CACHE_SIZE);
	ASSERT((final - first) < BITS_PER_LONG && (final - first) >= 0);

	return mask;
}

STATIC void
set_page_region(
	struct page	*page,
	size_t		offset,
	size_t		length)
{
	set_page_private(page,
		page_private(page) | page_region_mask(offset, length));
	if (page_private(page) == ~0UL)
		SetPageUptodate(page);
}

STATIC int
test_page_region(
	struct page	*page,
	size_t		offset,
	size_t		length)
{
	unsigned long	mask = page_region_mask(offset, length);

	return (mask && (page_private(page) & mask) == mask);
}

/*
 * xfs_buf_lru_add - add a buffer to the LRU.
 *
 * The LRU takes a new reference to the buffer so that it will only be freed
 * once the shrinker takes the buffer off the LRU.
 */
STATIC void
xfs_buf_lru_add(
	struct xfs_buf	*bp)
{
	struct xfs_buftarg *btp = bp->b_target;

	spin_lock(&btp->bt_lru_lock);
	if (list_empty(&bp->b_lru)) {
		atomic_inc(&bp->b_hold);
		list_add_tail(&bp->b_lru, &btp->bt_lru);
		btp->bt_lru_nr++;
	}
	spin_unlock(&btp->bt_lru_lock);
}

/*
 * xfs_buf_lru_del - remove a buffer from the LRU
 *
 * The unlocked check is safe here because it only occurs when there are not
 * b_lru_ref counts left on the inode under the pag->pag_buf_lock. it is there
 * to optimise the shrinker removing the buffer from the LRU and calling
 * xfs_buf_free(). i.e. it removes an unneccessary round trip on the
 * bt_lru_lock.
 */
STATIC void
xfs_buf_lru_del(
	struct xfs_buf	*bp)
{
	struct xfs_buftarg *btp = bp->b_target;

	if (list_empty(&bp->b_lru))
		return;

	spin_lock(&btp->bt_lru_lock);
	if (!list_empty(&bp->b_lru)) {
		list_del_init(&bp->b_lru);
		btp->bt_lru_nr--;
	}
	spin_unlock(&btp->bt_lru_lock);
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
	bp->b_flags |= XBF_STALE;
	atomic_set(&(bp)->b_lru_ref, 0);
	if (!list_empty(&bp->b_lru)) {
		struct xfs_buftarg *btp = bp->b_target;

		spin_lock(&btp->bt_lru_lock);
		if (!list_empty(&bp->b_lru)) {
			list_del_init(&bp->b_lru);
			btp->bt_lru_nr--;
			atomic_dec(&bp->b_hold);
		}
		spin_unlock(&btp->bt_lru_lock);
	}
	ASSERT(atomic_read(&bp->b_hold) >= 1);
}

STATIC void
_xfs_buf_initialize(
	xfs_buf_t		*bp,
	xfs_buftarg_t		*target,
	xfs_off_t		range_base,
	size_t			range_length,
	xfs_buf_flags_t		flags)
{
	/*
	 * We don't want certain flags to appear in b_flags.
	 */
	flags &= ~(XBF_LOCK|XBF_MAPPED|XBF_DONT_BLOCK|XBF_READ_AHEAD);

	memset(bp, 0, sizeof(xfs_buf_t));
	atomic_set(&bp->b_hold, 1);
	atomic_set(&bp->b_lru_ref, 1);
	init_completion(&bp->b_iowait);
	INIT_LIST_HEAD(&bp->b_lru);
	INIT_LIST_HEAD(&bp->b_list);
	RB_CLEAR_NODE(&bp->b_rbnode);
	sema_init(&bp->b_sema, 0); /* held, no waiters */
	XB_SET_OWNER(bp);
	bp->b_target = target;
	bp->b_file_offset = range_base;
	/*
	 * Set buffer_length and count_desired to the same value initially.
	 * I/O routines should use count_desired, which will be the same in
	 * most cases but may be reset (e.g. XFS recovery).
	 */
	bp->b_buffer_length = bp->b_count_desired = range_length;
	bp->b_flags = flags;
	bp->b_bn = XFS_BUF_DADDR_NULL;
	atomic_set(&bp->b_pin_count, 0);
	init_waitqueue_head(&bp->b_waiters);

	XFS_STATS_INC(xb_create);

	trace_xfs_buf_init(bp, _RET_IP_);
}

/*
 *	Allocate a page array capable of holding a specified number
 *	of pages, and point the page buf at it.
 */
STATIC int
_xfs_buf_get_pages(
	xfs_buf_t		*bp,
	int			page_count,
	xfs_buf_flags_t		flags)
{
	/* Make sure that we have a page list */
	if (bp->b_pages == NULL) {
		bp->b_offset = xfs_buf_poff(bp->b_file_offset);
		bp->b_page_count = page_count;
		if (page_count <= XB_PAGES) {
			bp->b_pages = bp->b_page_array;
		} else {
			bp->b_pages = kmem_alloc(sizeof(struct page *) *
					page_count, xb_to_km(flags));
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
 * 	The buffer most not be on any hash - use xfs_buf_rele instead for
 * 	hashed and refcounted buffers
 */
void
xfs_buf_free(
	xfs_buf_t		*bp)
{
	trace_xfs_buf_free(bp, _RET_IP_);

	ASSERT(list_empty(&bp->b_lru));

	if (bp->b_flags & (_XBF_PAGE_CACHE|_XBF_PAGES)) {
		uint		i;

		if (xfs_buf_is_vmapped(bp))
			vm_unmap_ram(bp->b_addr - bp->b_offset,
					bp->b_page_count);

		for (i = 0; i < bp->b_page_count; i++) {
			struct page	*page = bp->b_pages[i];

			if (bp->b_flags & _XBF_PAGE_CACHE)
				ASSERT(!PagePrivate(page));
			page_cache_release(page);
		}
	}
	_xfs_buf_free_pages(bp);
	xfs_buf_deallocate(bp);
}

/*
 *	Finds all pages for buffer in question and builds it's page list.
 */
STATIC int
_xfs_buf_lookup_pages(
	xfs_buf_t		*bp,
	uint			flags)
{
	struct address_space	*mapping = bp->b_target->bt_mapping;
	size_t			blocksize = bp->b_target->bt_bsize;
	size_t			size = bp->b_count_desired;
	size_t			nbytes, offset;
	gfp_t			gfp_mask = xb_to_gfp(flags);
	unsigned short		page_count, i;
	pgoff_t			first;
	xfs_off_t		end;
	int			error;

	end = bp->b_file_offset + bp->b_buffer_length;
	page_count = xfs_buf_btoc(end) - xfs_buf_btoct(bp->b_file_offset);

	error = _xfs_buf_get_pages(bp, page_count, flags);
	if (unlikely(error))
		return error;
	bp->b_flags |= _XBF_PAGE_CACHE;

	offset = bp->b_offset;
	first = bp->b_file_offset >> PAGE_CACHE_SHIFT;

	for (i = 0; i < bp->b_page_count; i++) {
		struct page	*page;
		uint		retries = 0;

	      retry:
		page = find_or_create_page(mapping, first + i, gfp_mask);
		if (unlikely(page == NULL)) {
			if (flags & XBF_READ_AHEAD) {
				bp->b_page_count = i;
				for (i = 0; i < bp->b_page_count; i++)
					unlock_page(bp->b_pages[i]);
				return -ENOMEM;
			}

			/*
			 * This could deadlock.
			 *
			 * But until all the XFS lowlevel code is revamped to
			 * handle buffer allocation failures we can't do much.
			 */
			if (!(++retries % 100))
				printk(KERN_ERR
					"XFS: possible memory allocation "
					"deadlock in %s (mode:0x%x)\n",
					__func__, gfp_mask);

			XFS_STATS_INC(xb_page_retries);
			congestion_wait(BLK_RW_ASYNC, HZ/50);
			goto retry;
		}

		XFS_STATS_INC(xb_page_found);

		nbytes = min_t(size_t, size, PAGE_CACHE_SIZE - offset);
		size -= nbytes;

		ASSERT(!PagePrivate(page));
		if (!PageUptodate(page)) {
			page_count--;
			if (blocksize >= PAGE_CACHE_SIZE) {
				if (flags & XBF_READ)
					bp->b_flags |= _XBF_PAGE_LOCKED;
			} else if (!PagePrivate(page)) {
				if (test_page_region(page, offset, nbytes))
					page_count++;
			}
		}

		bp->b_pages[i] = page;
		offset = 0;
	}

	if (!(bp->b_flags & _XBF_PAGE_LOCKED)) {
		for (i = 0; i < bp->b_page_count; i++)
			unlock_page(bp->b_pages[i]);
	}

	if (page_count == bp->b_page_count)
		bp->b_flags |= XBF_DONE;

	return error;
}

/*
 *	Map buffer into kernel address-space if nessecary.
 */
STATIC int
_xfs_buf_map_pages(
	xfs_buf_t		*bp,
	uint			flags)
{
	/* A single page buffer is always mappable */
	if (bp->b_page_count == 1) {
		bp->b_addr = page_address(bp->b_pages[0]) + bp->b_offset;
		bp->b_flags |= XBF_MAPPED;
	} else if (flags & XBF_MAPPED) {
		bp->b_addr = vm_map_ram(bp->b_pages, bp->b_page_count,
					-1, PAGE_KERNEL);
		if (unlikely(bp->b_addr == NULL))
			return -ENOMEM;
		bp->b_addr += bp->b_offset;
		bp->b_flags |= XBF_MAPPED;
	}

	return 0;
}

/*
 *	Finding and Reading Buffers
 */

/*
 *	Look up, and creates if absent, a lockable buffer for
 *	a given range of an inode.  The buffer is returned
 *	locked.	 If other overlapping buffers exist, they are
 *	released before the new buffer is created and locked,
 *	which may imply that this call will block until those buffers
 *	are unlocked.  No I/O is implied by this call.
 */
xfs_buf_t *
_xfs_buf_find(
	xfs_buftarg_t		*btp,	/* block device target		*/
	xfs_off_t		ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	xfs_buf_flags_t		flags,
	xfs_buf_t		*new_bp)
{
	xfs_off_t		range_base;
	size_t			range_length;
	struct xfs_perag	*pag;
	struct rb_node		**rbp;
	struct rb_node		*parent;
	xfs_buf_t		*bp;

	range_base = (ioff << BBSHIFT);
	range_length = (isize << BBSHIFT);

	/* Check for IOs smaller than the sector size / not sector aligned */
	ASSERT(!(range_length < (1 << btp->bt_sshift)));
	ASSERT(!(range_base & (xfs_off_t)btp->bt_smask));

	/* get tree root */
	pag = xfs_perag_get(btp->bt_mount,
				xfs_daddr_to_agno(btp->bt_mount, ioff));

	/* walk tree */
	spin_lock(&pag->pag_buf_lock);
	rbp = &pag->pag_buf_tree.rb_node;
	parent = NULL;
	bp = NULL;
	while (*rbp) {
		parent = *rbp;
		bp = rb_entry(parent, struct xfs_buf, b_rbnode);

		if (range_base < bp->b_file_offset)
			rbp = &(*rbp)->rb_left;
		else if (range_base > bp->b_file_offset)
			rbp = &(*rbp)->rb_right;
		else {
			/*
			 * found a block offset match. If the range doesn't
			 * match, the only way this is allowed is if the buffer
			 * in the cache is stale and the transaction that made
			 * it stale has not yet committed. i.e. we are
			 * reallocating a busy extent. Skip this buffer and
			 * continue searching to the right for an exact match.
			 */
			if (bp->b_buffer_length != range_length) {
				ASSERT(bp->b_flags & XBF_STALE);
				rbp = &(*rbp)->rb_right;
				continue;
			}
			atomic_inc(&bp->b_hold);
			goto found;
		}
	}

	/* No match found */
	if (new_bp) {
		_xfs_buf_initialize(new_bp, btp, range_base,
				range_length, flags);
		rb_link_node(&new_bp->b_rbnode, parent, rbp);
		rb_insert_color(&new_bp->b_rbnode, &pag->pag_buf_tree);
		/* the buffer keeps the perag reference until it is freed */
		new_bp->b_pag = pag;
		spin_unlock(&pag->pag_buf_lock);
	} else {
		XFS_STATS_INC(xb_miss_locked);
		spin_unlock(&pag->pag_buf_lock);
		xfs_perag_put(pag);
	}
	return new_bp;

found:
	spin_unlock(&pag->pag_buf_lock);
	xfs_perag_put(pag);

	if (xfs_buf_cond_lock(bp)) {
		/* failed, so wait for the lock if requested. */
		if (!(flags & XBF_TRYLOCK)) {
			xfs_buf_lock(bp);
			XFS_STATS_INC(xb_get_locked_waited);
		} else {
			xfs_buf_rele(bp);
			XFS_STATS_INC(xb_busy_locked);
			return NULL;
		}
	}

	if (bp->b_flags & XBF_STALE) {
		ASSERT((bp->b_flags & _XBF_DELWRI_Q) == 0);
		bp->b_flags &= XBF_MAPPED;
	}

	trace_xfs_buf_find(bp, flags, _RET_IP_);
	XFS_STATS_INC(xb_get_locked);
	return bp;
}

/*
 *	Assembles a buffer covering the specified range.
 *	Storage in memory for all portions of the buffer will be allocated,
 *	although backing storage may not be.
 */
xfs_buf_t *
xfs_buf_get(
	xfs_buftarg_t		*target,/* target for buffer		*/
	xfs_off_t		ioff,	/* starting offset of range	*/
	size_t			isize,	/* length of range		*/
	xfs_buf_flags_t		flags)
{
	xfs_buf_t		*bp, *new_bp;
	int			error = 0, i;

	new_bp = xfs_buf_allocate(flags);
	if (unlikely(!new_bp))
		return NULL;

	bp = _xfs_buf_find(target, ioff, isize, flags, new_bp);
	if (bp == new_bp) {
		error = _xfs_buf_lookup_pages(bp, flags);
		if (error)
			goto no_buffer;
	} else {
		xfs_buf_deallocate(new_bp);
		if (unlikely(bp == NULL))
			return NULL;
	}

	for (i = 0; i < bp->b_page_count; i++)
		mark_page_accessed(bp->b_pages[i]);

	if (!(bp->b_flags & XBF_MAPPED)) {
		error = _xfs_buf_map_pages(bp, flags);
		if (unlikely(error)) {
			printk(KERN_WARNING "%s: failed to map pages\n",
					__func__);
			goto no_buffer;
		}
	}

	XFS_STATS_INC(xb_get);

	/*
	 * Always fill in the block number now, the mapped cases can do
	 * their own overlay of this later.
	 */
	bp->b_bn = ioff;
	bp->b_count_desired = bp->b_buffer_length;

	trace_xfs_buf_get(bp, flags, _RET_IP_);
	return bp;

 no_buffer:
	if (flags & (XBF_LOCK | XBF_TRYLOCK))
		xfs_buf_unlock(bp);
	xfs_buf_rele(bp);
	return NULL;
}

STATIC int
_xfs_buf_read(
	xfs_buf_t		*bp,
	xfs_buf_flags_t		flags)
{
	int			status;

	ASSERT(!(flags & (XBF_DELWRI|XBF_WRITE)));
	ASSERT(bp->b_bn != XFS_BUF_DADDR_NULL);

	bp->b_flags &= ~(XBF_WRITE | XBF_ASYNC | XBF_DELWRI | \
			XBF_READ_AHEAD | _XBF_RUN_QUEUES);
	bp->b_flags |= flags & (XBF_READ | XBF_ASYNC | \
			XBF_READ_AHEAD | _XBF_RUN_QUEUES);

	status = xfs_buf_iorequest(bp);
	if (status || XFS_BUF_ISERROR(bp) || (flags & XBF_ASYNC))
		return status;
	return xfs_buf_iowait(bp);
}

xfs_buf_t *
xfs_buf_read(
	xfs_buftarg_t		*target,
	xfs_off_t		ioff,
	size_t			isize,
	xfs_buf_flags_t		flags)
{
	xfs_buf_t		*bp;

	flags |= XBF_READ;

	bp = xfs_buf_get(target, ioff, isize, flags);
	if (bp) {
		trace_xfs_buf_read(bp, flags, _RET_IP_);

		if (!XFS_BUF_ISDONE(bp)) {
			XFS_STATS_INC(xb_get_read);
			_xfs_buf_read(bp, flags);
		} else if (flags & XBF_ASYNC) {
			/*
			 * Read ahead call which is already satisfied,
			 * drop the buffer
			 */
			goto no_buffer;
		} else {
			/* We do not want read in the flags */
			bp->b_flags &= ~XBF_READ;
		}
	}

	return bp;

 no_buffer:
	if (flags & (XBF_LOCK | XBF_TRYLOCK))
		xfs_buf_unlock(bp);
	xfs_buf_rele(bp);
	return NULL;
}

/*
 *	If we are not low on memory then do the readahead in a deadlock
 *	safe manner.
 */
void
xfs_buf_readahead(
	xfs_buftarg_t		*target,
	xfs_off_t		ioff,
	size_t			isize)
{
	struct backing_dev_info *bdi;

	bdi = target->bt_mapping->backing_dev_info;
	if (bdi_read_congested(bdi))
		return;

	xfs_buf_read(target, ioff, isize,
		     XBF_TRYLOCK|XBF_ASYNC|XBF_READ_AHEAD|XBF_DONT_BLOCK);
}

/*
 * Read an uncached buffer from disk. Allocates and returns a locked
 * buffer containing the disk contents or nothing.
 */
struct xfs_buf *
xfs_buf_read_uncached(
	struct xfs_mount	*mp,
	struct xfs_buftarg	*target,
	xfs_daddr_t		daddr,
	size_t			length,
	int			flags)
{
	xfs_buf_t		*bp;
	int			error;

	bp = xfs_buf_get_uncached(target, length, flags);
	if (!bp)
		return NULL;

	/* set up the buffer for a read IO */
	xfs_buf_lock(bp);
	XFS_BUF_SET_ADDR(bp, daddr);
	XFS_BUF_READ(bp);
	XFS_BUF_BUSY(bp);

	xfsbdstrat(mp, bp);
	error = xfs_buf_iowait(bp);
	if (error || bp->b_error) {
		xfs_buf_relse(bp);
		return NULL;
	}
	return bp;
}

xfs_buf_t *
xfs_buf_get_empty(
	size_t			len,
	xfs_buftarg_t		*target)
{
	xfs_buf_t		*bp;

	bp = xfs_buf_allocate(0);
	if (bp)
		_xfs_buf_initialize(bp, target, 0, len, 0);
	return bp;
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

	pageaddr = (unsigned long)mem & PAGE_CACHE_MASK;
	offset = (unsigned long)mem - pageaddr;
	buflen = PAGE_CACHE_ALIGN(len + offset);
	page_count = buflen >> PAGE_CACHE_SHIFT;

	/* Free any previous set of page pointers */
	if (bp->b_pages)
		_xfs_buf_free_pages(bp);

	bp->b_pages = NULL;
	bp->b_addr = mem;

	rval = _xfs_buf_get_pages(bp, page_count, XBF_DONT_BLOCK);
	if (rval)
		return rval;

	bp->b_offset = offset;

	for (i = 0; i < bp->b_page_count; i++) {
		bp->b_pages[i] = mem_to_page((void *)pageaddr);
		pageaddr += PAGE_CACHE_SIZE;
	}

	bp->b_count_desired = len;
	bp->b_buffer_length = buflen;
	bp->b_flags |= XBF_MAPPED;
	bp->b_flags &= ~_XBF_PAGE_LOCKED;

	return 0;
}

xfs_buf_t *
xfs_buf_get_uncached(
	struct xfs_buftarg	*target,
	size_t			len,
	int			flags)
{
	unsigned long		page_count = PAGE_ALIGN(len) >> PAGE_SHIFT;
	int			error, i;
	xfs_buf_t		*bp;

	bp = xfs_buf_allocate(0);
	if (unlikely(bp == NULL))
		goto fail;
	_xfs_buf_initialize(bp, target, 0, len, 0);

	error = _xfs_buf_get_pages(bp, page_count, 0);
	if (error)
		goto fail_free_buf;

	for (i = 0; i < page_count; i++) {
		bp->b_pages[i] = alloc_page(xb_to_gfp(flags));
		if (!bp->b_pages[i])
			goto fail_free_mem;
	}
	bp->b_flags |= _XBF_PAGES;

	error = _xfs_buf_map_pages(bp, XBF_MAPPED);
	if (unlikely(error)) {
		printk(KERN_WARNING "%s: failed to map pages\n",
				__func__);
		goto fail_free_mem;
	}

	xfs_buf_unlock(bp);

	trace_xfs_buf_get_uncached(bp, _RET_IP_);
	return bp;

 fail_free_mem:
	while (--i >= 0)
		__free_page(bp->b_pages[i]);
	_xfs_buf_free_pages(bp);
 fail_free_buf:
	xfs_buf_deallocate(bp);
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
 *	Releases a hold on the specified buffer.  If the
 *	the hold count is 1, calls xfs_buf_free.
 */
void
xfs_buf_rele(
	xfs_buf_t		*bp)
{
	struct xfs_perag	*pag = bp->b_pag;

	trace_xfs_buf_rele(bp, _RET_IP_);

	if (!pag) {
		ASSERT(!bp->b_relse);
		ASSERT(list_empty(&bp->b_lru));
		ASSERT(RB_EMPTY_NODE(&bp->b_rbnode));
		if (atomic_dec_and_test(&bp->b_hold))
			xfs_buf_free(bp);
		return;
	}

	ASSERT(!RB_EMPTY_NODE(&bp->b_rbnode));

	ASSERT(atomic_read(&bp->b_hold) > 0);
	if (atomic_dec_and_lock(&bp->b_hold, &pag->pag_buf_lock)) {
		if (bp->b_relse) {
			atomic_inc(&bp->b_hold);
			spin_unlock(&pag->pag_buf_lock);
			bp->b_relse(bp);
		} else if (!(bp->b_flags & XBF_STALE) &&
			   atomic_read(&bp->b_lru_ref)) {
			xfs_buf_lru_add(bp);
			spin_unlock(&pag->pag_buf_lock);
		} else {
			xfs_buf_lru_del(bp);
			ASSERT(!(bp->b_flags & (XBF_DELWRI|_XBF_DELWRI_Q)));
			rb_erase(&bp->b_rbnode, &pag->pag_buf_tree);
			spin_unlock(&pag->pag_buf_lock);
			xfs_perag_put(pag);
			xfs_buf_free(bp);
		}
	}
}


/*
 *	Mutual exclusion on buffers.  Locking model:
 *
 *	Buffers associated with inodes for which buffer locking
 *	is not enabled are not protected by semaphores, and are
 *	assumed to be exclusively owned by the caller.  There is a
 *	spinlock in the buffer, used by the caller when concurrent
 *	access is possible.
 */

/*
 *	Locks a buffer object, if it is not already locked.  Note that this in
 *	no way locks the underlying pages, so it is only useful for
 *	synchronizing concurrent use of buffer objects, not for synchronizing
 *	independent access to the underlying pages.
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
xfs_buf_cond_lock(
	xfs_buf_t		*bp)
{
	int			locked;

	locked = down_trylock(&bp->b_sema) == 0;
	if (locked)
		XB_SET_OWNER(bp);
	else if (atomic_read(&bp->b_pin_count) && (bp->b_flags & XBF_STALE))
		xfs_log_force(bp->b_target->bt_mount, 0);

	trace_xfs_buf_cond_lock(bp, _RET_IP_);
	return locked ? 0 : -EBUSY;
}

int
xfs_buf_lock_value(
	xfs_buf_t		*bp)
{
	return bp->b_sema.count;
}

/*
 *	Locks a buffer object.
 *	Note that this in no way locks the underlying pages, so it is only
 *	useful for synchronizing concurrent use of buffer objects, not for
 *	synchronizing independent access to the underlying pages.
 *
 *	If we come across a stale, pinned, locked buffer, we know that we
 *	are being asked to lock a buffer that has been reallocated. Because
 *	it is pinned, we know that the log has not been pushed to disk and
 *	hence it will still be locked. Rather than sleeping until someone
 *	else pushes the log, push it ourselves before trying to get the lock.
 */
void
xfs_buf_lock(
	xfs_buf_t		*bp)
{
	trace_xfs_buf_lock(bp, _RET_IP_);

	if (atomic_read(&bp->b_pin_count) && (bp->b_flags & XBF_STALE))
		xfs_log_force(bp->b_target->bt_mount, 0);
	if (atomic_read(&bp->b_io_remaining))
		blk_run_address_space(bp->b_target->bt_mapping);
	down(&bp->b_sema);
	XB_SET_OWNER(bp);

	trace_xfs_buf_lock_done(bp, _RET_IP_);
}

/*
 *	Releases the lock on the buffer object.
 *	If the buffer is marked delwri but is not queued, do so before we
 *	unlock the buffer as we need to set flags correctly.  We also need to
 *	take a reference for the delwri queue because the unlocker is going to
 *	drop their's and they don't know we just queued it.
 */
void
xfs_buf_unlock(
	xfs_buf_t		*bp)
{
	if ((bp->b_flags & (XBF_DELWRI|_XBF_DELWRI_Q)) == XBF_DELWRI) {
		atomic_inc(&bp->b_hold);
		bp->b_flags |= XBF_ASYNC;
		xfs_buf_delwri_queue(bp, 0);
	}

	XB_CLEAR_OWNER(bp);
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
		if (atomic_read(&bp->b_io_remaining))
			blk_run_address_space(bp->b_target->bt_mapping);
		schedule();
	}
	remove_wait_queue(&bp->b_waiters, &wait);
	set_current_state(TASK_RUNNING);
}

/*
 *	Buffer Utility Routines
 */

STATIC void
xfs_buf_iodone_work(
	struct work_struct	*work)
{
	xfs_buf_t		*bp =
		container_of(work, xfs_buf_t, b_iodone_work);

	if (bp->b_iodone)
		(*(bp->b_iodone))(bp);
	else if (bp->b_flags & XBF_ASYNC)
		xfs_buf_relse(bp);
}

void
xfs_buf_ioend(
	xfs_buf_t		*bp,
	int			schedule)
{
	trace_xfs_buf_iodone(bp, _RET_IP_);

	bp->b_flags &= ~(XBF_READ | XBF_WRITE | XBF_READ_AHEAD);
	if (bp->b_error == 0)
		bp->b_flags |= XBF_DONE;

	if ((bp->b_iodone) || (bp->b_flags & XBF_ASYNC)) {
		if (schedule) {
			INIT_WORK(&bp->b_iodone_work, xfs_buf_iodone_work);
			queue_work(xfslogd_workqueue, &bp->b_iodone_work);
		} else {
			xfs_buf_iodone_work(&bp->b_iodone_work);
		}
	} else {
		complete(&bp->b_iowait);
	}
}

void
xfs_buf_ioerror(
	xfs_buf_t		*bp,
	int			error)
{
	ASSERT(error >= 0 && error <= 0xffff);
	bp->b_error = (unsigned short)error;
	trace_xfs_buf_ioerror(bp, error, _RET_IP_);
}

int
xfs_bwrite(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp)
{
	int			error;

	bp->b_flags |= XBF_WRITE;
	bp->b_flags &= ~(XBF_ASYNC | XBF_READ);

	xfs_buf_delwri_dequeue(bp);
	xfs_bdstrat_cb(bp);

	error = xfs_buf_iowait(bp);
	if (error)
		xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
	xfs_buf_relse(bp);
	return error;
}

void
xfs_bdwrite(
	void			*mp,
	struct xfs_buf		*bp)
{
	trace_xfs_buf_bdwrite(bp, _RET_IP_);

	bp->b_flags &= ~XBF_READ;
	bp->b_flags |= (XBF_DELWRI | XBF_ASYNC);

	xfs_buf_delwri_queue(bp, 1);
}

/*
 * Called when we want to stop a buffer from getting written or read.
 * We attach the EIO error, muck with its flags, and call xfs_buf_ioend
 * so that the proper iodone callbacks get called.
 */
STATIC int
xfs_bioerror(
	xfs_buf_t *bp)
{
#ifdef XFSERRORDEBUG
	ASSERT(XFS_BUF_ISREAD(bp) || bp->b_iodone);
#endif

	/*
	 * No need to wait until the buffer is unpinned, we aren't flushing it.
	 */
	XFS_BUF_ERROR(bp, EIO);

	/*
	 * We're calling xfs_buf_ioend, so delete XBF_DONE flag.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_UNDONE(bp);
	XFS_BUF_STALE(bp);

	xfs_buf_ioend(bp, 0);

	return EIO;
}

/*
 * Same as xfs_bioerror, except that we are releasing the buffer
 * here ourselves, and avoiding the xfs_buf_ioend call.
 * This is meant for userdata errors; metadata bufs come with
 * iodone functions attached, so that we can track down errors.
 */
STATIC int
xfs_bioerror_relse(
	struct xfs_buf	*bp)
{
	int64_t		fl = XFS_BUF_BFLAGS(bp);
	/*
	 * No need to wait until the buffer is unpinned.
	 * We aren't flushing it.
	 *
	 * chunkhold expects B_DONE to be set, whether
	 * we actually finish the I/O or not. We don't want to
	 * change that interface.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_DONE(bp);
	XFS_BUF_STALE(bp);
	XFS_BUF_CLR_IODONE_FUNC(bp);
	if (!(fl & XBF_ASYNC)) {
		/*
		 * Mark b_error and B_ERROR _both_.
		 * Lot's of chunkcache code assumes that.
		 * There's no reason to mark error for
		 * ASYNC buffers.
		 */
		XFS_BUF_ERROR(bp, EIO);
		XFS_BUF_FINISH_IOWAIT(bp);
	} else {
		xfs_buf_relse(bp);
	}

	return EIO;
}


/*
 * All xfs metadata buffers except log state machine buffers
 * get this attached as their b_bdstrat callback function.
 * This is so that we can catch a buffer
 * after prematurely unpinning it to forcibly shutdown the filesystem.
 */
int
xfs_bdstrat_cb(
	struct xfs_buf	*bp)
{
	if (XFS_FORCED_SHUTDOWN(bp->b_target->bt_mount)) {
		trace_xfs_bdstrat_shut(bp, _RET_IP_);
		/*
		 * Metadata write that didn't get logged but
		 * written delayed anyway. These aren't associated
		 * with a transaction, and can be ignored.
		 */
		if (!bp->b_iodone && !XFS_BUF_ISREAD(bp))
			return xfs_bioerror_relse(bp);
		else
			return xfs_bioerror(bp);
	}

	xfs_buf_iorequest(bp);
	return 0;
}

/*
 * Wrapper around bdstrat so that we can stop data from going to disk in case
 * we are shutting down the filesystem.  Typically user data goes thru this
 * path; one of the exceptions is the superblock.
 */
void
xfsbdstrat(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp)
{
	if (XFS_FORCED_SHUTDOWN(mp)) {
		trace_xfs_bdstrat_shut(bp, _RET_IP_);
		xfs_bioerror_relse(bp);
		return;
	}

	xfs_buf_iorequest(bp);
}

STATIC void
_xfs_buf_ioend(
	xfs_buf_t		*bp,
	int			schedule)
{
	if (atomic_dec_and_test(&bp->b_io_remaining) == 1) {
		bp->b_flags &= ~_XBF_PAGE_LOCKED;
		xfs_buf_ioend(bp, schedule);
	}
}

STATIC void
xfs_buf_bio_end_io(
	struct bio		*bio,
	int			error)
{
	xfs_buf_t		*bp = (xfs_buf_t *)bio->bi_private;
	unsigned int		blocksize = bp->b_target->bt_bsize;
	struct bio_vec		*bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	xfs_buf_ioerror(bp, -error);

	if (!error && xfs_buf_is_vmapped(bp) && (bp->b_flags & XBF_READ))
		invalidate_kernel_vmap_range(bp->b_addr, xfs_buf_vmap_len(bp));

	do {
		struct page	*page = bvec->bv_page;

		ASSERT(!PagePrivate(page));
		if (unlikely(bp->b_error)) {
			if (bp->b_flags & XBF_READ)
				ClearPageUptodate(page);
		} else if (blocksize >= PAGE_CACHE_SIZE) {
			SetPageUptodate(page);
		} else if (!PagePrivate(page) &&
				(bp->b_flags & _XBF_PAGE_CACHE)) {
			set_page_region(page, bvec->bv_offset, bvec->bv_len);
		}

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (bp->b_flags & _XBF_PAGE_LOCKED)
			unlock_page(page);
	} while (bvec >= bio->bi_io_vec);

	_xfs_buf_ioend(bp, 1);
	bio_put(bio);
}

STATIC void
_xfs_buf_ioapply(
	xfs_buf_t		*bp)
{
	int			rw, map_i, total_nr_pages, nr_pages;
	struct bio		*bio;
	int			offset = bp->b_offset;
	int			size = bp->b_count_desired;
	sector_t		sector = bp->b_bn;
	unsigned int		blocksize = bp->b_target->bt_bsize;

	total_nr_pages = bp->b_page_count;
	map_i = 0;

	if (bp->b_flags & XBF_ORDERED) {
		ASSERT(!(bp->b_flags & XBF_READ));
		rw = WRITE_FLUSH_FUA;
	} else if (bp->b_flags & XBF_LOG_BUFFER) {
		ASSERT(!(bp->b_flags & XBF_READ_AHEAD));
		bp->b_flags &= ~_XBF_RUN_QUEUES;
		rw = (bp->b_flags & XBF_WRITE) ? WRITE_SYNC : READ_SYNC;
	} else if (bp->b_flags & _XBF_RUN_QUEUES) {
		ASSERT(!(bp->b_flags & XBF_READ_AHEAD));
		bp->b_flags &= ~_XBF_RUN_QUEUES;
		rw = (bp->b_flags & XBF_WRITE) ? WRITE_META : READ_META;
	} else {
		rw = (bp->b_flags & XBF_WRITE) ? WRITE :
		     (bp->b_flags & XBF_READ_AHEAD) ? READA : READ;
	}

	/* Special code path for reading a sub page size buffer in --
	 * we populate up the whole page, and hence the other metadata
	 * in the same page.  This optimization is only valid when the
	 * filesystem block size is not smaller than the page size.
	 */
	if ((bp->b_buffer_length < PAGE_CACHE_SIZE) &&
	    ((bp->b_flags & (XBF_READ|_XBF_PAGE_LOCKED)) ==
	      (XBF_READ|_XBF_PAGE_LOCKED)) &&
	    (blocksize >= PAGE_CACHE_SIZE)) {
		bio = bio_alloc(GFP_NOIO, 1);

		bio->bi_bdev = bp->b_target->bt_bdev;
		bio->bi_sector = sector - (offset >> BBSHIFT);
		bio->bi_end_io = xfs_buf_bio_end_io;
		bio->bi_private = bp;

		bio_add_page(bio, bp->b_pages[0], PAGE_CACHE_SIZE, 0);
		size = 0;

		atomic_inc(&bp->b_io_remaining);

		goto submit_io;
	}

next_chunk:
	atomic_inc(&bp->b_io_remaining);
	nr_pages = BIO_MAX_SECTORS >> (PAGE_SHIFT - BBSHIFT);
	if (nr_pages > total_nr_pages)
		nr_pages = total_nr_pages;

	bio = bio_alloc(GFP_NOIO, nr_pages);
	bio->bi_bdev = bp->b_target->bt_bdev;
	bio->bi_sector = sector;
	bio->bi_end_io = xfs_buf_bio_end_io;
	bio->bi_private = bp;

	for (; size && nr_pages; nr_pages--, map_i++) {
		int	rbytes, nbytes = PAGE_CACHE_SIZE - offset;

		if (nbytes > size)
			nbytes = size;

		rbytes = bio_add_page(bio, bp->b_pages[map_i], nbytes, offset);
		if (rbytes < nbytes)
			break;

		offset = 0;
		sector += nbytes >> BBSHIFT;
		size -= nbytes;
		total_nr_pages--;
	}

submit_io:
	if (likely(bio->bi_size)) {
		if (xfs_buf_is_vmapped(bp)) {
			flush_kernel_vmap_range(bp->b_addr,
						xfs_buf_vmap_len(bp));
		}
		submit_bio(rw, bio);
		if (size)
			goto next_chunk;
	} else {
		/*
		 * if we get here, no pages were added to the bio. However,
		 * we can't just error out here - if the pages are locked then
		 * we have to unlock them otherwise we can hang on a later
		 * access to the page.
		 */
		xfs_buf_ioerror(bp, EIO);
		if (bp->b_flags & _XBF_PAGE_LOCKED) {
			int i;
			for (i = 0; i < bp->b_page_count; i++)
				unlock_page(bp->b_pages[i]);
		}
		bio_put(bio);
	}
}

int
xfs_buf_iorequest(
	xfs_buf_t		*bp)
{
	trace_xfs_buf_iorequest(bp, _RET_IP_);

	if (bp->b_flags & XBF_DELWRI) {
		xfs_buf_delwri_queue(bp, 1);
		return 0;
	}

	if (bp->b_flags & XBF_WRITE) {
		xfs_buf_wait_unpin(bp);
	}

	xfs_buf_hold(bp);

	/* Set the count to 1 initially, this will stop an I/O
	 * completion callout which happens before we have started
	 * all the I/O from calling xfs_buf_ioend too early.
	 */
	atomic_set(&bp->b_io_remaining, 1);
	_xfs_buf_ioapply(bp);
	_xfs_buf_ioend(bp, 0);

	xfs_buf_rele(bp);
	return 0;
}

/*
 *	Waits for I/O to complete on the buffer supplied.
 *	It returns immediately if no I/O is pending.
 *	It returns the I/O error code, if any, or 0 if there was no error.
 */
int
xfs_buf_iowait(
	xfs_buf_t		*bp)
{
	trace_xfs_buf_iowait(bp, _RET_IP_);

	if (atomic_read(&bp->b_io_remaining))
		blk_run_address_space(bp->b_target->bt_mapping);
	wait_for_completion(&bp->b_iowait);

	trace_xfs_buf_iowait_done(bp, _RET_IP_);
	return bp->b_error;
}

xfs_caddr_t
xfs_buf_offset(
	xfs_buf_t		*bp,
	size_t			offset)
{
	struct page		*page;

	if (bp->b_flags & XBF_MAPPED)
		return XFS_BUF_PTR(bp) + offset;

	offset += bp->b_offset;
	page = bp->b_pages[offset >> PAGE_CACHE_SHIFT];
	return (xfs_caddr_t)page_address(page) + (offset & (PAGE_CACHE_SIZE-1));
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
	size_t			bend, cpoff, csize;
	struct page		*page;

	bend = boff + bsize;
	while (boff < bend) {
		page = bp->b_pages[xfs_buf_btoct(boff + bp->b_offset)];
		cpoff = xfs_buf_poff(boff + bp->b_offset);
		csize = min_t(size_t,
			      PAGE_CACHE_SIZE-cpoff, bp->b_count_desired-boff);

		ASSERT(((csize + cpoff) <= PAGE_CACHE_SIZE));

		switch (mode) {
		case XBRW_ZERO:
			memset(page_address(page) + cpoff, 0, csize);
			break;
		case XBRW_READ:
			memcpy(data, page_address(page) + cpoff, csize);
			break;
		case XBRW_WRITE:
			memcpy(page_address(page) + cpoff, data, csize);
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
void
xfs_wait_buftarg(
	struct xfs_buftarg	*btp)
{
	struct xfs_buf		*bp;

restart:
	spin_lock(&btp->bt_lru_lock);
	while (!list_empty(&btp->bt_lru)) {
		bp = list_first_entry(&btp->bt_lru, struct xfs_buf, b_lru);
		if (atomic_read(&bp->b_hold) > 1) {
			spin_unlock(&btp->bt_lru_lock);
			delay(100);
			goto restart;
		}
		/*
		 * clear the LRU reference count so the bufer doesn't get
		 * ignored in xfs_buf_rele().
		 */
		atomic_set(&bp->b_lru_ref, 0);
		spin_unlock(&btp->bt_lru_lock);
		xfs_buf_rele(bp);
		spin_lock(&btp->bt_lru_lock);
	}
	spin_unlock(&btp->bt_lru_lock);
}

int
xfs_buftarg_shrink(
	struct shrinker		*shrink,
	int			nr_to_scan,
	gfp_t			mask)
{
	struct xfs_buftarg	*btp = container_of(shrink,
					struct xfs_buftarg, bt_shrinker);
	struct xfs_buf		*bp;
	LIST_HEAD(dispose);

	if (!nr_to_scan)
		return btp->bt_lru_nr;

	spin_lock(&btp->bt_lru_lock);
	while (!list_empty(&btp->bt_lru)) {
		if (nr_to_scan-- <= 0)
			break;

		bp = list_first_entry(&btp->bt_lru, struct xfs_buf, b_lru);

		/*
		 * Decrement the b_lru_ref count unless the value is already
		 * zero. If the value is already zero, we need to reclaim the
		 * buffer, otherwise it gets another trip through the LRU.
		 */
		if (!atomic_add_unless(&bp->b_lru_ref, -1, 0)) {
			list_move_tail(&bp->b_lru, &btp->bt_lru);
			continue;
		}

		/*
		 * remove the buffer from the LRU now to avoid needing another
		 * lock round trip inside xfs_buf_rele().
		 */
		list_move(&bp->b_lru, &dispose);
		btp->bt_lru_nr--;
	}
	spin_unlock(&btp->bt_lru_lock);

	while (!list_empty(&dispose)) {
		bp = list_first_entry(&dispose, struct xfs_buf, b_lru);
		list_del_init(&bp->b_lru);
		xfs_buf_rele(bp);
	}

	return btp->bt_lru_nr;
}

void
xfs_free_buftarg(
	struct xfs_mount	*mp,
	struct xfs_buftarg	*btp)
{
	unregister_shrinker(&btp->bt_shrinker);

	xfs_flush_buftarg(btp, 1);
	if (mp->m_flags & XFS_MOUNT_BARRIER)
		xfs_blkdev_issue_flush(btp);
	iput(btp->bt_mapping->host);

	kthread_stop(btp->bt_task);
	kmem_free(btp);
}

STATIC int
xfs_setsize_buftarg_flags(
	xfs_buftarg_t		*btp,
	unsigned int		blocksize,
	unsigned int		sectorsize,
	int			verbose)
{
	btp->bt_bsize = blocksize;
	btp->bt_sshift = ffs(sectorsize) - 1;
	btp->bt_smask = sectorsize - 1;

	if (set_blocksize(btp->bt_bdev, sectorsize)) {
		printk(KERN_WARNING
			"XFS: Cannot set_blocksize to %u on device %s\n",
			sectorsize, XFS_BUFTARG_NAME(btp));
		return EINVAL;
	}

	if (verbose &&
	    (PAGE_CACHE_SIZE / BITS_PER_LONG) > sectorsize) {
		printk(KERN_WARNING
			"XFS: %u byte sectors in use on device %s.  "
			"This is suboptimal; %u or greater is ideal.\n",
			sectorsize, XFS_BUFTARG_NAME(btp),
			(unsigned int)PAGE_CACHE_SIZE / BITS_PER_LONG);
	}

	return 0;
}

/*
 *	When allocating the initial buffer target we have not yet
 *	read in the superblock, so don't know what sized sectors
 *	are being used is at this early stage.  Play safe.
 */
STATIC int
xfs_setsize_buftarg_early(
	xfs_buftarg_t		*btp,
	struct block_device	*bdev)
{
	return xfs_setsize_buftarg_flags(btp,
			PAGE_CACHE_SIZE, bdev_logical_block_size(bdev), 0);
}

int
xfs_setsize_buftarg(
	xfs_buftarg_t		*btp,
	unsigned int		blocksize,
	unsigned int		sectorsize)
{
	return xfs_setsize_buftarg_flags(btp, blocksize, sectorsize, 1);
}

STATIC int
xfs_mapping_buftarg(
	xfs_buftarg_t		*btp,
	struct block_device	*bdev)
{
	struct backing_dev_info	*bdi;
	struct inode		*inode;
	struct address_space	*mapping;
	static const struct address_space_operations mapping_aops = {
		.sync_page = block_sync_page,
		.migratepage = fail_migrate_page,
	};

	inode = new_inode(bdev->bd_inode->i_sb);
	if (!inode) {
		printk(KERN_WARNING
			"XFS: Cannot allocate mapping inode for device %s\n",
			XFS_BUFTARG_NAME(btp));
		return ENOMEM;
	}
	inode->i_ino = get_next_ino();
	inode->i_mode = S_IFBLK;
	inode->i_bdev = bdev;
	inode->i_rdev = bdev->bd_dev;
	bdi = blk_get_backing_dev_info(bdev);
	if (!bdi)
		bdi = &default_backing_dev_info;
	mapping = &inode->i_data;
	mapping->a_ops = &mapping_aops;
	mapping->backing_dev_info = bdi;
	mapping_set_gfp_mask(mapping, GFP_NOFS);
	btp->bt_mapping = mapping;
	return 0;
}

STATIC int
xfs_alloc_delwrite_queue(
	xfs_buftarg_t		*btp,
	const char		*fsname)
{
	INIT_LIST_HEAD(&btp->bt_delwrite_queue);
	spin_lock_init(&btp->bt_delwrite_lock);
	btp->bt_flags = 0;
	btp->bt_task = kthread_run(xfsbufd, btp, "xfsbufd/%s", fsname);
	if (IS_ERR(btp->bt_task))
		return PTR_ERR(btp->bt_task);
	return 0;
}

xfs_buftarg_t *
xfs_alloc_buftarg(
	struct xfs_mount	*mp,
	struct block_device	*bdev,
	int			external,
	const char		*fsname)
{
	xfs_buftarg_t		*btp;

	btp = kmem_zalloc(sizeof(*btp), KM_SLEEP);

	btp->bt_mount = mp;
	btp->bt_dev =  bdev->bd_dev;
	btp->bt_bdev = bdev;
	INIT_LIST_HEAD(&btp->bt_lru);
	spin_lock_init(&btp->bt_lru_lock);
	if (xfs_setsize_buftarg_early(btp, bdev))
		goto error;
	if (xfs_mapping_buftarg(btp, bdev))
		goto error;
	if (xfs_alloc_delwrite_queue(btp, fsname))
		goto error;
	btp->bt_shrinker.shrink = xfs_buftarg_shrink;
	btp->bt_shrinker.seeks = DEFAULT_SEEKS;
	register_shrinker(&btp->bt_shrinker);
	return btp;

error:
	kmem_free(btp);
	return NULL;
}


/*
 *	Delayed write buffer handling
 */
STATIC void
xfs_buf_delwri_queue(
	xfs_buf_t		*bp,
	int			unlock)
{
	struct list_head	*dwq = &bp->b_target->bt_delwrite_queue;
	spinlock_t		*dwlk = &bp->b_target->bt_delwrite_lock;

	trace_xfs_buf_delwri_queue(bp, _RET_IP_);

	ASSERT((bp->b_flags&(XBF_DELWRI|XBF_ASYNC)) == (XBF_DELWRI|XBF_ASYNC));

	spin_lock(dwlk);
	/* If already in the queue, dequeue and place at tail */
	if (!list_empty(&bp->b_list)) {
		ASSERT(bp->b_flags & _XBF_DELWRI_Q);
		if (unlock)
			atomic_dec(&bp->b_hold);
		list_del(&bp->b_list);
	}

	if (list_empty(dwq)) {
		/* start xfsbufd as it is about to have something to do */
		wake_up_process(bp->b_target->bt_task);
	}

	bp->b_flags |= _XBF_DELWRI_Q;
	list_add_tail(&bp->b_list, dwq);
	bp->b_queuetime = jiffies;
	spin_unlock(dwlk);

	if (unlock)
		xfs_buf_unlock(bp);
}

void
xfs_buf_delwri_dequeue(
	xfs_buf_t		*bp)
{
	spinlock_t		*dwlk = &bp->b_target->bt_delwrite_lock;
	int			dequeued = 0;

	spin_lock(dwlk);
	if ((bp->b_flags & XBF_DELWRI) && !list_empty(&bp->b_list)) {
		ASSERT(bp->b_flags & _XBF_DELWRI_Q);
		list_del_init(&bp->b_list);
		dequeued = 1;
	}
	bp->b_flags &= ~(XBF_DELWRI|_XBF_DELWRI_Q);
	spin_unlock(dwlk);

	if (dequeued)
		xfs_buf_rele(bp);

	trace_xfs_buf_delwri_dequeue(bp, _RET_IP_);
}

/*
 * If a delwri buffer needs to be pushed before it has aged out, then promote
 * it to the head of the delwri queue so that it will be flushed on the next
 * xfsbufd run. We do this by resetting the queuetime of the buffer to be older
 * than the age currently needed to flush the buffer. Hence the next time the
 * xfsbufd sees it is guaranteed to be considered old enough to flush.
 */
void
xfs_buf_delwri_promote(
	struct xfs_buf	*bp)
{
	struct xfs_buftarg *btp = bp->b_target;
	long		age = xfs_buf_age_centisecs * msecs_to_jiffies(10) + 1;

	ASSERT(bp->b_flags & XBF_DELWRI);
	ASSERT(bp->b_flags & _XBF_DELWRI_Q);

	/*
	 * Check the buffer age before locking the delayed write queue as we
	 * don't need to promote buffers that are already past the flush age.
	 */
	if (bp->b_queuetime < jiffies - age)
		return;
	bp->b_queuetime = jiffies - age;
	spin_lock(&btp->bt_delwrite_lock);
	list_move(&bp->b_list, &btp->bt_delwrite_queue);
	spin_unlock(&btp->bt_delwrite_lock);
}

STATIC void
xfs_buf_runall_queues(
	struct workqueue_struct	*queue)
{
	flush_workqueue(queue);
}

/*
 * Move as many buffers as specified to the supplied list
 * idicating if we skipped any buffers to prevent deadlocks.
 */
STATIC int
xfs_buf_delwri_split(
	xfs_buftarg_t	*target,
	struct list_head *list,
	unsigned long	age)
{
	xfs_buf_t	*bp, *n;
	struct list_head *dwq = &target->bt_delwrite_queue;
	spinlock_t	*dwlk = &target->bt_delwrite_lock;
	int		skipped = 0;
	int		force;

	force = test_and_clear_bit(XBT_FORCE_FLUSH, &target->bt_flags);
	INIT_LIST_HEAD(list);
	spin_lock(dwlk);
	list_for_each_entry_safe(bp, n, dwq, b_list) {
		ASSERT(bp->b_flags & XBF_DELWRI);

		if (!XFS_BUF_ISPINNED(bp) && !xfs_buf_cond_lock(bp)) {
			if (!force &&
			    time_before(jiffies, bp->b_queuetime + age)) {
				xfs_buf_unlock(bp);
				break;
			}

			bp->b_flags &= ~(XBF_DELWRI|_XBF_DELWRI_Q|
					 _XBF_RUN_QUEUES);
			bp->b_flags |= XBF_WRITE;
			list_move_tail(&bp->b_list, list);
			trace_xfs_buf_delwri_split(bp, _RET_IP_);
		} else
			skipped++;
	}
	spin_unlock(dwlk);

	return skipped;

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

	diff = ap->b_bn - bp->b_bn;
	if (diff < 0)
		return -1;
	if (diff > 0)
		return 1;
	return 0;
}

void
xfs_buf_delwri_sort(
	xfs_buftarg_t	*target,
	struct list_head *list)
{
	list_sort(NULL, list, xfs_buf_cmp);
}

STATIC int
xfsbufd(
	void		*data)
{
	xfs_buftarg_t   *target = (xfs_buftarg_t *)data;

	current->flags |= PF_MEMALLOC;

	set_freezable();

	do {
		long	age = xfs_buf_age_centisecs * msecs_to_jiffies(10);
		long	tout = xfs_buf_timer_centisecs * msecs_to_jiffies(10);
		int	count = 0;
		struct list_head tmp;

		if (unlikely(freezing(current))) {
			set_bit(XBT_FORCE_SLEEP, &target->bt_flags);
			refrigerator();
		} else {
			clear_bit(XBT_FORCE_SLEEP, &target->bt_flags);
		}

		/* sleep for a long time if there is nothing to do. */
		if (list_empty(&target->bt_delwrite_queue))
			tout = MAX_SCHEDULE_TIMEOUT;
		schedule_timeout_interruptible(tout);

		xfs_buf_delwri_split(target, &tmp, age);
		list_sort(NULL, &tmp, xfs_buf_cmp);
		while (!list_empty(&tmp)) {
			struct xfs_buf *bp;
			bp = list_first_entry(&tmp, struct xfs_buf, b_list);
			list_del_init(&bp->b_list);
			xfs_bdstrat_cb(bp);
			count++;
		}
		if (count)
			blk_run_address_space(target->bt_mapping);

	} while (!kthread_should_stop());

	return 0;
}

/*
 *	Go through all incore buffers, and release buffers if they belong to
 *	the given device. This is used in filesystem error handling to
 *	preserve the consistency of its metadata.
 */
int
xfs_flush_buftarg(
	xfs_buftarg_t	*target,
	int		wait)
{
	xfs_buf_t	*bp;
	int		pincount = 0;
	LIST_HEAD(tmp_list);
	LIST_HEAD(wait_list);

	xfs_buf_runall_queues(xfsconvertd_workqueue);
	xfs_buf_runall_queues(xfsdatad_workqueue);
	xfs_buf_runall_queues(xfslogd_workqueue);

	set_bit(XBT_FORCE_FLUSH, &target->bt_flags);
	pincount = xfs_buf_delwri_split(target, &tmp_list, 0);

	/*
	 * Dropped the delayed write list lock, now walk the temporary list.
	 * All I/O is issued async and then if we need to wait for completion
	 * we do that after issuing all the IO.
	 */
	list_sort(NULL, &tmp_list, xfs_buf_cmp);
	while (!list_empty(&tmp_list)) {
		bp = list_first_entry(&tmp_list, struct xfs_buf, b_list);
		ASSERT(target == bp->b_target);
		list_del_init(&bp->b_list);
		if (wait) {
			bp->b_flags &= ~XBF_ASYNC;
			list_add(&bp->b_list, &wait_list);
		}
		xfs_bdstrat_cb(bp);
	}

	if (wait) {
		/* Expedite and wait for IO to complete. */
		blk_run_address_space(target->bt_mapping);
		while (!list_empty(&wait_list)) {
			bp = list_first_entry(&wait_list, struct xfs_buf, b_list);

			list_del_init(&bp->b_list);
			xfs_buf_iowait(bp);
			xfs_buf_relse(bp);
		}
	}

	return pincount;
}

int __init
xfs_buf_init(void)
{
	xfs_buf_zone = kmem_zone_init_flags(sizeof(xfs_buf_t), "xfs_buf",
						KM_ZONE_HWALIGN, NULL);
	if (!xfs_buf_zone)
		goto out;

	xfslogd_workqueue = alloc_workqueue("xfslogd",
					WQ_MEM_RECLAIM | WQ_HIGHPRI, 1);
	if (!xfslogd_workqueue)
		goto out_free_buf_zone;

	xfsdatad_workqueue = create_workqueue("xfsdatad");
	if (!xfsdatad_workqueue)
		goto out_destroy_xfslogd_workqueue;

	xfsconvertd_workqueue = create_workqueue("xfsconvertd");
	if (!xfsconvertd_workqueue)
		goto out_destroy_xfsdatad_workqueue;

	return 0;

 out_destroy_xfsdatad_workqueue:
	destroy_workqueue(xfsdatad_workqueue);
 out_destroy_xfslogd_workqueue:
	destroy_workqueue(xfslogd_workqueue);
 out_free_buf_zone:
	kmem_zone_destroy(xfs_buf_zone);
 out:
	return -ENOMEM;
}

void
xfs_buf_terminate(void)
{
	destroy_workqueue(xfsconvertd_workqueue);
	destroy_workqueue(xfsdatad_workqueue);
	destroy_workqueue(xfslogd_workqueue);
	kmem_zone_destroy(xfs_buf_zone);
}

#ifdef CONFIG_KDB_MODULES
struct list_head *
xfs_get_buftarg_list(void)
{
	return &xfs_buftarg_list;
}
#endif
