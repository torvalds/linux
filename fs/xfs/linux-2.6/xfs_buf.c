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
#include <linux/slab.h>
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

static kmem_zone_t *xfs_buf_zone;
STATIC int xfsbufd(void *);
STATIC int xfsbufd_wakeup(int, gfp_t);
STATIC void xfs_buf_delwri_queue(xfs_buf_t *, int);
static struct shrinker xfs_buf_shake = {
	.shrink = xfsbufd_wakeup,
	.seeks = DEFAULT_SEEKS,
};

static struct workqueue_struct *xfslogd_workqueue;
struct workqueue_struct *xfsdatad_workqueue;

#ifdef XFS_BUF_TRACE
void
xfs_buf_trace(
	xfs_buf_t	*bp,
	char		*id,
	void		*data,
	void		*ra)
{
	ktrace_enter(xfs_buf_trace_buf,
		bp, id,
		(void *)(unsigned long)bp->b_flags,
		(void *)(unsigned long)bp->b_hold.counter,
		(void *)(unsigned long)bp->b_sema.count.counter,
		(void *)current,
		data, ra,
		(void *)(unsigned long)((bp->b_file_offset>>32) & 0xffffffff),
		(void *)(unsigned long)(bp->b_file_offset & 0xffffffff),
		(void *)(unsigned long)bp->b_buffer_length,
		NULL, NULL, NULL, NULL, NULL);
}
ktrace_t *xfs_buf_trace_buf;
#define XFS_BUF_TRACE_SIZE	4096
#define XB_TRACE(bp, id, data)	\
	xfs_buf_trace(bp, id, (void *)data, (void *)__builtin_return_address(0))
#else
#define XB_TRACE(bp, id, data)	do { } while (0)
#endif

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

STATIC_INLINE void
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

STATIC_INLINE int
test_page_region(
	struct page	*page,
	size_t		offset,
	size_t		length)
{
	unsigned long	mask = page_region_mask(offset, length);

	return (mask && (page_private(page) & mask) == mask);
}

/*
 *	Mapping of multi-page buffers into contiguous virtual space
 */

typedef struct a_list {
	void		*vm_addr;
	struct a_list	*next;
} a_list_t;

static a_list_t		*as_free_head;
static int		as_list_len;
static DEFINE_SPINLOCK(as_lock);

/*
 *	Try to batch vunmaps because they are costly.
 */
STATIC void
free_address(
	void		*addr)
{
	a_list_t	*aentry;

#ifdef CONFIG_XEN
	/*
	 * Xen needs to be able to make sure it can get an exclusive
	 * RO mapping of pages it wants to turn into a pagetable.  If
	 * a newly allocated page is also still being vmap()ed by xfs,
	 * it will cause pagetable construction to fail.  This is a
	 * quick workaround to always eagerly unmap pages so that Xen
	 * is happy.
	 */
	vunmap(addr);
	return;
#endif

	aentry = kmalloc(sizeof(a_list_t), GFP_NOWAIT);
	if (likely(aentry)) {
		spin_lock(&as_lock);
		aentry->next = as_free_head;
		aentry->vm_addr = addr;
		as_free_head = aentry;
		as_list_len++;
		spin_unlock(&as_lock);
	} else {
		vunmap(addr);
	}
}

STATIC void
purge_addresses(void)
{
	a_list_t	*aentry, *old;

	if (as_free_head == NULL)
		return;

	spin_lock(&as_lock);
	aentry = as_free_head;
	as_free_head = NULL;
	as_list_len = 0;
	spin_unlock(&as_lock);

	while ((old = aentry) != NULL) {
		vunmap(aentry->vm_addr);
		aentry = aentry->next;
		kfree(old);
	}
}

/*
 *	Internal xfs_buf_t object manipulation
 */

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
	init_MUTEX_LOCKED(&bp->b_iodonesema);
	INIT_LIST_HEAD(&bp->b_list);
	INIT_LIST_HEAD(&bp->b_hash_list);
	init_MUTEX_LOCKED(&bp->b_sema); /* held, no waiters */
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
	XB_TRACE(bp, "initialize", target);
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
		kmem_free(bp->b_pages,
			  bp->b_page_count * sizeof(struct page *));
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
	XB_TRACE(bp, "free", 0);

	ASSERT(list_empty(&bp->b_hash_list));

	if (bp->b_flags & (_XBF_PAGE_CACHE|_XBF_PAGES)) {
		uint		i;

		if ((bp->b_flags & XBF_MAPPED) && (bp->b_page_count > 1))
			free_address(bp->b_addr - bp->b_offset);

		for (i = 0; i < bp->b_page_count; i++) {
			struct page	*page = bp->b_pages[i];

			if (bp->b_flags & _XBF_PAGE_CACHE)
				ASSERT(!PagePrivate(page));
			page_cache_release(page);
		}
		_xfs_buf_free_pages(bp);
	}

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
			xfsbufd_wakeup(0, gfp_mask);
			congestion_wait(WRITE, HZ/50);
			goto retry;
		}

		XFS_STATS_INC(xb_page_found);

		nbytes = min_t(size_t, size, PAGE_CACHE_SIZE - offset);
		size -= nbytes;

		ASSERT(!PagePrivate(page));
		if (!PageUptodate(page)) {
			page_count--;
			if (blocksize < PAGE_CACHE_SIZE && !PagePrivate(page)) {
				if (test_page_region(page, offset, nbytes))
					page_count++;
			}
		}

		unlock_page(page);
		bp->b_pages[i] = page;
		offset = 0;
	}

	if (page_count == bp->b_page_count)
		bp->b_flags |= XBF_DONE;

	XB_TRACE(bp, "lookup_pages", (long)page_count);
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
		if (as_list_len > 64)
			purge_addresses();
		bp->b_addr = vmap(bp->b_pages, bp->b_page_count,
					VM_MAP, PAGE_KERNEL);
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
	xfs_bufhash_t		*hash;
	xfs_buf_t		*bp, *n;

	range_base = (ioff << BBSHIFT);
	range_length = (isize << BBSHIFT);

	/* Check for IOs smaller than the sector size / not sector aligned */
	ASSERT(!(range_length < (1 << btp->bt_sshift)));
	ASSERT(!(range_base & (xfs_off_t)btp->bt_smask));

	hash = &btp->bt_hash[hash_long((unsigned long)ioff, btp->bt_hashshift)];

	spin_lock(&hash->bh_lock);

	list_for_each_entry_safe(bp, n, &hash->bh_list, b_hash_list) {
		ASSERT(btp == bp->b_target);
		if (bp->b_file_offset == range_base &&
		    bp->b_buffer_length == range_length) {
			/*
			 * If we look at something, bring it to the
			 * front of the list for next time.
			 */
			atomic_inc(&bp->b_hold);
			list_move(&bp->b_hash_list, &hash->bh_list);
			goto found;
		}
	}

	/* No match found */
	if (new_bp) {
		_xfs_buf_initialize(new_bp, btp, range_base,
				range_length, flags);
		new_bp->b_hash = hash;
		list_add(&new_bp->b_hash_list, &hash->bh_list);
	} else {
		XFS_STATS_INC(xb_miss_locked);
	}

	spin_unlock(&hash->bh_lock);
	return new_bp;

found:
	spin_unlock(&hash->bh_lock);

	/* Attempt to get the semaphore without sleeping,
	 * if this does not work then we need to drop the
	 * spinlock and do a hard attempt on the semaphore.
	 */
	if (down_trylock(&bp->b_sema)) {
		if (!(flags & XBF_TRYLOCK)) {
			/* wait for buffer ownership */
			XB_TRACE(bp, "get_lock", 0);
			xfs_buf_lock(bp);
			XFS_STATS_INC(xb_get_locked_waited);
		} else {
			/* We asked for a trylock and failed, no need
			 * to look at file offset and length here, we
			 * know that this buffer at least overlaps our
			 * buffer and is locked, therefore our buffer
			 * either does not exist, or is this buffer.
			 */
			xfs_buf_rele(bp);
			XFS_STATS_INC(xb_busy_locked);
			return NULL;
		}
	} else {
		/* trylock worked */
		XB_SET_OWNER(bp);
	}

	if (bp->b_flags & XBF_STALE) {
		ASSERT((bp->b_flags & _XBF_DELWRI_Q) == 0);
		bp->b_flags &= XBF_MAPPED;
	}
	XB_TRACE(bp, "got_lock", 0);
	XFS_STATS_INC(xb_get_locked);
	return bp;
}

/*
 *	Assembles a buffer covering the specified range.
 *	Storage in memory for all portions of the buffer will be allocated,
 *	although backing storage may not be.
 */
xfs_buf_t *
xfs_buf_get_flags(
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

	XB_TRACE(bp, "get", (unsigned long)flags);
	return bp;

 no_buffer:
	if (flags & (XBF_LOCK | XBF_TRYLOCK))
		xfs_buf_unlock(bp);
	xfs_buf_rele(bp);
	return NULL;
}

xfs_buf_t *
xfs_buf_read_flags(
	xfs_buftarg_t		*target,
	xfs_off_t		ioff,
	size_t			isize,
	xfs_buf_flags_t		flags)
{
	xfs_buf_t		*bp;

	flags |= XBF_READ;

	bp = xfs_buf_get_flags(target, ioff, isize, flags);
	if (bp) {
		if (!XFS_BUF_ISDONE(bp)) {
			XB_TRACE(bp, "read", (unsigned long)flags);
			XFS_STATS_INC(xb_get_read);
			xfs_buf_iostart(bp, flags);
		} else if (flags & XBF_ASYNC) {
			XB_TRACE(bp, "read_async", (unsigned long)flags);
			/*
			 * Read ahead call which is already satisfied,
			 * drop the buffer
			 */
			goto no_buffer;
		} else {
			XB_TRACE(bp, "read_done", (unsigned long)flags);
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
	size_t			isize,
	xfs_buf_flags_t		flags)
{
	struct backing_dev_info *bdi;

	bdi = target->bt_mapping->backing_dev_info;
	if (bdi_read_congested(bdi))
		return;

	flags |= (XBF_TRYLOCK|XBF_ASYNC|XBF_READ_AHEAD);
	xfs_buf_read_flags(target, ioff, isize, flags);
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

	rval = _xfs_buf_get_pages(bp, page_count, 0);
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

	return 0;
}

xfs_buf_t *
xfs_buf_get_noaddr(
	size_t			len,
	xfs_buftarg_t		*target)
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
		bp->b_pages[i] = alloc_page(GFP_KERNEL);
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

	XB_TRACE(bp, "no_daddr", len);
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
	atomic_inc(&bp->b_hold);
	XB_TRACE(bp, "hold", 0);
}

/*
 *	Releases a hold on the specified buffer.  If the
 *	the hold count is 1, calls xfs_buf_free.
 */
void
xfs_buf_rele(
	xfs_buf_t		*bp)
{
	xfs_bufhash_t		*hash = bp->b_hash;

	XB_TRACE(bp, "rele", bp->b_relse);

	if (unlikely(!hash)) {
		ASSERT(!bp->b_relse);
		if (atomic_dec_and_test(&bp->b_hold))
			xfs_buf_free(bp);
		return;
	}

	if (atomic_dec_and_lock(&bp->b_hold, &hash->bh_lock)) {
		if (bp->b_relse) {
			atomic_inc(&bp->b_hold);
			spin_unlock(&hash->bh_lock);
			(*(bp->b_relse)) (bp);
		} else if (bp->b_flags & XBF_FS_MANAGED) {
			spin_unlock(&hash->bh_lock);
		} else {
			ASSERT(!(bp->b_flags & (XBF_DELWRI|_XBF_DELWRI_Q)));
			list_del_init(&bp->b_hash_list);
			spin_unlock(&hash->bh_lock);
			xfs_buf_free(bp);
		}
	} else {
		/*
		 * Catch reference count leaks
		 */
		ASSERT(atomic_read(&bp->b_hold) >= 0);
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
 *	Locks a buffer object, if it is not already locked.
 *	Note that this in no way locks the underlying pages, so it is only
 *	useful for synchronizing concurrent use of buffer objects, not for
 *	synchronizing independent access to the underlying pages.
 */
int
xfs_buf_cond_lock(
	xfs_buf_t		*bp)
{
	int			locked;

	locked = down_trylock(&bp->b_sema) == 0;
	if (locked) {
		XB_SET_OWNER(bp);
	}
	XB_TRACE(bp, "cond_lock", (long)locked);
	return locked ? 0 : -EBUSY;
}

#if defined(DEBUG) || defined(XFS_BLI_TRACE)
int
xfs_buf_lock_value(
	xfs_buf_t		*bp)
{
	return bp->b_sema.count;
}
#endif

/*
 *	Locks a buffer object.
 *	Note that this in no way locks the underlying pages, so it is only
 *	useful for synchronizing concurrent use of buffer objects, not for
 *	synchronizing independent access to the underlying pages.
 */
void
xfs_buf_lock(
	xfs_buf_t		*bp)
{
	XB_TRACE(bp, "lock", 0);
	if (atomic_read(&bp->b_io_remaining))
		blk_run_address_space(bp->b_target->bt_mapping);
	down(&bp->b_sema);
	XB_SET_OWNER(bp);
	XB_TRACE(bp, "locked", 0);
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
	XB_TRACE(bp, "unlock", 0);
}


/*
 *	Pinning Buffer Storage in Memory
 *	Ensure that no attempt to force a buffer to disk will succeed.
 */
void
xfs_buf_pin(
	xfs_buf_t		*bp)
{
	atomic_inc(&bp->b_pin_count);
	XB_TRACE(bp, "pin", (long)bp->b_pin_count.counter);
}

void
xfs_buf_unpin(
	xfs_buf_t		*bp)
{
	if (atomic_dec_and_test(&bp->b_pin_count))
		wake_up_all(&bp->b_waiters);
	XB_TRACE(bp, "unpin", (long)bp->b_pin_count.counter);
}

int
xfs_buf_ispin(
	xfs_buf_t		*bp)
{
	return atomic_read(&bp->b_pin_count);
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

	/*
	 * We can get an EOPNOTSUPP to ordered writes.  Here we clear the
	 * ordered flag and reissue them.  Because we can't tell the higher
	 * layers directly that they should not issue ordered I/O anymore, they
	 * need to check if the ordered flag was cleared during I/O completion.
	 */
	if ((bp->b_error == EOPNOTSUPP) &&
	    (bp->b_flags & (XBF_ORDERED|XBF_ASYNC)) == (XBF_ORDERED|XBF_ASYNC)) {
		XB_TRACE(bp, "ordered_retry", bp->b_iodone);
		bp->b_flags &= ~XBF_ORDERED;
		xfs_buf_iorequest(bp);
	} else if (bp->b_iodone)
		(*(bp->b_iodone))(bp);
	else if (bp->b_flags & XBF_ASYNC)
		xfs_buf_relse(bp);
}

void
xfs_buf_ioend(
	xfs_buf_t		*bp,
	int			schedule)
{
	bp->b_flags &= ~(XBF_READ | XBF_WRITE | XBF_READ_AHEAD);
	if (bp->b_error == 0)
		bp->b_flags |= XBF_DONE;

	XB_TRACE(bp, "iodone", bp->b_iodone);

	if ((bp->b_iodone) || (bp->b_flags & XBF_ASYNC)) {
		if (schedule) {
			INIT_WORK(&bp->b_iodone_work, xfs_buf_iodone_work);
			queue_work(xfslogd_workqueue, &bp->b_iodone_work);
		} else {
			xfs_buf_iodone_work(&bp->b_iodone_work);
		}
	} else {
		up(&bp->b_iodonesema);
	}
}

void
xfs_buf_ioerror(
	xfs_buf_t		*bp,
	int			error)
{
	ASSERT(error >= 0 && error <= 0xffff);
	bp->b_error = (unsigned short)error;
	XB_TRACE(bp, "ioerror", (unsigned long)error);
}

/*
 *	Initiate I/O on a buffer, based on the flags supplied.
 *	The b_iodone routine in the buffer supplied will only be called
 *	when all of the subsidiary I/O requests, if any, have been completed.
 */
int
xfs_buf_iostart(
	xfs_buf_t		*bp,
	xfs_buf_flags_t		flags)
{
	int			status = 0;

	XB_TRACE(bp, "iostart", (unsigned long)flags);

	if (flags & XBF_DELWRI) {
		bp->b_flags &= ~(XBF_READ | XBF_WRITE | XBF_ASYNC);
		bp->b_flags |= flags & (XBF_DELWRI | XBF_ASYNC);
		xfs_buf_delwri_queue(bp, 1);
		return 0;
	}

	bp->b_flags &= ~(XBF_READ | XBF_WRITE | XBF_ASYNC | XBF_DELWRI | \
			XBF_READ_AHEAD | _XBF_RUN_QUEUES);
	bp->b_flags |= flags & (XBF_READ | XBF_WRITE | XBF_ASYNC | \
			XBF_READ_AHEAD | _XBF_RUN_QUEUES);

	BUG_ON(bp->b_bn == XFS_BUF_DADDR_NULL);

	/* For writes allow an alternate strategy routine to precede
	 * the actual I/O request (which may not be issued at all in
	 * a shutdown situation, for example).
	 */
	status = (flags & XBF_WRITE) ?
		xfs_buf_iostrategy(bp) : xfs_buf_iorequest(bp);

	/* Wait for I/O if we are not an async request.
	 * Note: async I/O request completion will release the buffer,
	 * and that can already be done by this point.  So using the
	 * buffer pointer from here on, after async I/O, is invalid.
	 */
	if (!status && !(flags & XBF_ASYNC))
		status = xfs_buf_iowait(bp);

	return status;
}

STATIC_INLINE void
_xfs_buf_ioend(
	xfs_buf_t		*bp,
	int			schedule)
{
	if (atomic_dec_and_test(&bp->b_io_remaining) == 1)
		xfs_buf_ioend(bp, schedule);
}

STATIC void
xfs_buf_bio_end_io(
	struct bio		*bio,
	int			error)
{
	xfs_buf_t		*bp = (xfs_buf_t *)bio->bi_private;
	unsigned int		blocksize = bp->b_target->bt_bsize;
	struct bio_vec		*bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		bp->b_error = EIO;

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
		rw = WRITE_BARRIER;
	} else if (bp->b_flags & _XBF_RUN_QUEUES) {
		ASSERT(!(bp->b_flags & XBF_READ_AHEAD));
		bp->b_flags &= ~_XBF_RUN_QUEUES;
		rw = (bp->b_flags & XBF_WRITE) ? WRITE_SYNC : READ_SYNC;
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
	    (bp->b_flags & XBF_READ) &&
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
		submit_bio(rw, bio);
		if (size)
			goto next_chunk;
	} else {
		bio_put(bio);
		xfs_buf_ioerror(bp, EIO);
	}
}

int
xfs_buf_iorequest(
	xfs_buf_t		*bp)
{
	XB_TRACE(bp, "iorequest", 0);

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
	XB_TRACE(bp, "iowait", 0);
	if (atomic_read(&bp->b_io_remaining))
		blk_run_address_space(bp->b_target->bt_mapping);
	down(&bp->b_iodonesema);
	XB_TRACE(bp, "iowaited", (long)bp->b_error);
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
	caddr_t			data,	/* data address			*/
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
 *	Wait for any bufs with callbacks that have been submitted but
 *	have not yet returned... walk the hash list for the target.
 */
void
xfs_wait_buftarg(
	xfs_buftarg_t	*btp)
{
	xfs_buf_t	*bp, *n;
	xfs_bufhash_t	*hash;
	uint		i;

	for (i = 0; i < (1 << btp->bt_hashshift); i++) {
		hash = &btp->bt_hash[i];
again:
		spin_lock(&hash->bh_lock);
		list_for_each_entry_safe(bp, n, &hash->bh_list, b_hash_list) {
			ASSERT(btp == bp->b_target);
			if (!(bp->b_flags & XBF_FS_MANAGED)) {
				spin_unlock(&hash->bh_lock);
				/*
				 * Catch superblock reference count leaks
				 * immediately
				 */
				BUG_ON(bp->b_bn == 0);
				delay(100);
				goto again;
			}
		}
		spin_unlock(&hash->bh_lock);
	}
}

/*
 *	Allocate buffer hash table for a given target.
 *	For devices containing metadata (i.e. not the log/realtime devices)
 *	we need to allocate a much larger hash table.
 */
STATIC void
xfs_alloc_bufhash(
	xfs_buftarg_t		*btp,
	int			external)
{
	unsigned int		i;

	btp->bt_hashshift = external ? 3 : 8;	/* 8 or 256 buckets */
	btp->bt_hashmask = (1 << btp->bt_hashshift) - 1;
	btp->bt_hash = kmem_zalloc((1 << btp->bt_hashshift) *
					sizeof(xfs_bufhash_t), KM_SLEEP | KM_LARGE);
	for (i = 0; i < (1 << btp->bt_hashshift); i++) {
		spin_lock_init(&btp->bt_hash[i].bh_lock);
		INIT_LIST_HEAD(&btp->bt_hash[i].bh_list);
	}
}

STATIC void
xfs_free_bufhash(
	xfs_buftarg_t		*btp)
{
	kmem_free(btp->bt_hash, (1<<btp->bt_hashshift) * sizeof(xfs_bufhash_t));
	btp->bt_hash = NULL;
}

/*
 *	buftarg list for delwrite queue processing
 */
static LIST_HEAD(xfs_buftarg_list);
static DEFINE_SPINLOCK(xfs_buftarg_lock);

STATIC void
xfs_register_buftarg(
	xfs_buftarg_t           *btp)
{
	spin_lock(&xfs_buftarg_lock);
	list_add(&btp->bt_list, &xfs_buftarg_list);
	spin_unlock(&xfs_buftarg_lock);
}

STATIC void
xfs_unregister_buftarg(
	xfs_buftarg_t           *btp)
{
	spin_lock(&xfs_buftarg_lock);
	list_del(&btp->bt_list);
	spin_unlock(&xfs_buftarg_lock);
}

void
xfs_free_buftarg(
	xfs_buftarg_t		*btp,
	int			external)
{
	xfs_flush_buftarg(btp, 1);
	xfs_blkdev_issue_flush(btp);
	if (external)
		xfs_blkdev_put(btp->bt_bdev);
	xfs_free_bufhash(btp);
	iput(btp->bt_mapping->host);

	/* Unregister the buftarg first so that we don't get a
	 * wakeup finding a non-existent task
	 */
	xfs_unregister_buftarg(btp);
	kthread_stop(btp->bt_task);

	kmem_free(btp, sizeof(*btp));
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
			PAGE_CACHE_SIZE, bdev_hardsect_size(bdev), 0);
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
	xfs_buftarg_t		*btp)
{
	int	error = 0;

	INIT_LIST_HEAD(&btp->bt_list);
	INIT_LIST_HEAD(&btp->bt_delwrite_queue);
	spin_lock_init(&btp->bt_delwrite_lock);
	btp->bt_flags = 0;
	btp->bt_task = kthread_run(xfsbufd, btp, "xfsbufd");
	if (IS_ERR(btp->bt_task)) {
		error = PTR_ERR(btp->bt_task);
		goto out_error;
	}
	xfs_register_buftarg(btp);
out_error:
	return error;
}

xfs_buftarg_t *
xfs_alloc_buftarg(
	struct block_device	*bdev,
	int			external)
{
	xfs_buftarg_t		*btp;

	btp = kmem_zalloc(sizeof(*btp), KM_SLEEP);

	btp->bt_dev =  bdev->bd_dev;
	btp->bt_bdev = bdev;
	if (xfs_setsize_buftarg_early(btp, bdev))
		goto error;
	if (xfs_mapping_buftarg(btp, bdev))
		goto error;
	if (xfs_alloc_delwrite_queue(btp))
		goto error;
	xfs_alloc_bufhash(btp, external);
	return btp;

error:
	kmem_free(btp, sizeof(*btp));
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

	XB_TRACE(bp, "delwri_q", (long)unlock);
	ASSERT((bp->b_flags&(XBF_DELWRI|XBF_ASYNC)) == (XBF_DELWRI|XBF_ASYNC));

	spin_lock(dwlk);
	/* If already in the queue, dequeue and place at tail */
	if (!list_empty(&bp->b_list)) {
		ASSERT(bp->b_flags & _XBF_DELWRI_Q);
		if (unlock)
			atomic_dec(&bp->b_hold);
		list_del(&bp->b_list);
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

	XB_TRACE(bp, "delwri_dq", (long)dequeued);
}

STATIC void
xfs_buf_runall_queues(
	struct workqueue_struct	*queue)
{
	flush_workqueue(queue);
}

STATIC int
xfsbufd_wakeup(
	int			priority,
	gfp_t			mask)
{
	xfs_buftarg_t		*btp;

	spin_lock(&xfs_buftarg_lock);
	list_for_each_entry(btp, &xfs_buftarg_list, bt_list) {
		if (test_bit(XBT_FORCE_SLEEP, &btp->bt_flags))
			continue;
		set_bit(XBT_FORCE_FLUSH, &btp->bt_flags);
		wake_up_process(btp->bt_task);
	}
	spin_unlock(&xfs_buftarg_lock);
	return 0;
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
		XB_TRACE(bp, "walkq1", (long)xfs_buf_ispin(bp));
		ASSERT(bp->b_flags & XBF_DELWRI);

		if (!xfs_buf_ispin(bp) && !xfs_buf_cond_lock(bp)) {
			if (!force &&
			    time_before(jiffies, bp->b_queuetime + age)) {
				xfs_buf_unlock(bp);
				break;
			}

			bp->b_flags &= ~(XBF_DELWRI|_XBF_DELWRI_Q|
					 _XBF_RUN_QUEUES);
			bp->b_flags |= XBF_WRITE;
			list_move_tail(&bp->b_list, list);
		} else
			skipped++;
	}
	spin_unlock(dwlk);

	return skipped;

}

STATIC int
xfsbufd(
	void		*data)
{
	struct list_head tmp;
	xfs_buftarg_t	*target = (xfs_buftarg_t *)data;
	int		count;
	xfs_buf_t	*bp;

	current->flags |= PF_MEMALLOC;

	set_freezable();

	do {
		if (unlikely(freezing(current))) {
			set_bit(XBT_FORCE_SLEEP, &target->bt_flags);
			refrigerator();
		} else {
			clear_bit(XBT_FORCE_SLEEP, &target->bt_flags);
		}

		schedule_timeout_interruptible(
			xfs_buf_timer_centisecs * msecs_to_jiffies(10));

		xfs_buf_delwri_split(target, &tmp,
				xfs_buf_age_centisecs * msecs_to_jiffies(10));

		count = 0;
		while (!list_empty(&tmp)) {
			bp = list_entry(tmp.next, xfs_buf_t, b_list);
			ASSERT(target == bp->b_target);

			list_del_init(&bp->b_list);
			xfs_buf_iostrategy(bp);
			count++;
		}

		if (as_list_len > 0)
			purge_addresses();
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
	struct list_head tmp;
	xfs_buf_t	*bp, *n;
	int		pincount = 0;

	xfs_buf_runall_queues(xfsdatad_workqueue);
	xfs_buf_runall_queues(xfslogd_workqueue);

	set_bit(XBT_FORCE_FLUSH, &target->bt_flags);
	pincount = xfs_buf_delwri_split(target, &tmp, 0);

	/*
	 * Dropped the delayed write list lock, now walk the temporary list
	 */
	list_for_each_entry_safe(bp, n, &tmp, b_list) {
		ASSERT(target == bp->b_target);
		if (wait)
			bp->b_flags &= ~XBF_ASYNC;
		else
			list_del_init(&bp->b_list);

		xfs_buf_iostrategy(bp);
	}

	if (wait)
		blk_run_address_space(target->bt_mapping);

	/*
	 * Remaining list items must be flushed before returning
	 */
	while (!list_empty(&tmp)) {
		bp = list_entry(tmp.next, xfs_buf_t, b_list);

		list_del_init(&bp->b_list);
		xfs_iowait(bp);
		xfs_buf_relse(bp);
	}

	return pincount;
}

int __init
xfs_buf_init(void)
{
#ifdef XFS_BUF_TRACE
	xfs_buf_trace_buf = ktrace_alloc(XFS_BUF_TRACE_SIZE, KM_SLEEP);
#endif

	xfs_buf_zone = kmem_zone_init_flags(sizeof(xfs_buf_t), "xfs_buf",
						KM_ZONE_HWALIGN, NULL);
	if (!xfs_buf_zone)
		goto out_free_trace_buf;

	xfslogd_workqueue = create_workqueue("xfslogd");
	if (!xfslogd_workqueue)
		goto out_free_buf_zone;

	xfsdatad_workqueue = create_workqueue("xfsdatad");
	if (!xfsdatad_workqueue)
		goto out_destroy_xfslogd_workqueue;

	register_shrinker(&xfs_buf_shake);
	return 0;

 out_destroy_xfslogd_workqueue:
	destroy_workqueue(xfslogd_workqueue);
 out_free_buf_zone:
	kmem_zone_destroy(xfs_buf_zone);
 out_free_trace_buf:
#ifdef XFS_BUF_TRACE
	ktrace_free(xfs_buf_trace_buf);
#endif
	return -ENOMEM;
}

void
xfs_buf_terminate(void)
{
	unregister_shrinker(&xfs_buf_shake);
	destroy_workqueue(xfsdatad_workqueue);
	destroy_workqueue(xfslogd_workqueue);
	kmem_zone_destroy(xfs_buf_zone);
#ifdef XFS_BUF_TRACE
	ktrace_free(xfs_buf_trace_buf);
#endif
}

#ifdef CONFIG_KDB_MODULES
struct list_head *
xfs_get_buftarg_list(void)
{
	return &xfs_buftarg_list;
}
#endif
