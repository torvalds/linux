/*
 * fs/dax.c - Direct Access filesystem code
 * Copyright (c) 2013-2014 Intel Corporation
 * Author: Matthew Wilcox <matthew.r.wilcox@intel.com>
 * Author: Ross Zwisler <ross.zwisler@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/atomic.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/memcontrol.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pagevec.h>
#include <linux/pmem.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/vmstat.h>
#include <linux/pfn_t.h>
#include <linux/sizes.h>
#include <linux/iomap.h>
#include "internal.h"

/* We choose 4096 entries - same as per-zone page wait tables */
#define DAX_WAIT_TABLE_BITS 12
#define DAX_WAIT_TABLE_ENTRIES (1 << DAX_WAIT_TABLE_BITS)

static wait_queue_head_t wait_table[DAX_WAIT_TABLE_ENTRIES];

static int __init init_dax_wait_table(void)
{
	int i;

	for (i = 0; i < DAX_WAIT_TABLE_ENTRIES; i++)
		init_waitqueue_head(wait_table + i);
	return 0;
}
fs_initcall(init_dax_wait_table);

static long dax_map_atomic(struct block_device *bdev, struct blk_dax_ctl *dax)
{
	struct request_queue *q = bdev->bd_queue;
	long rc = -EIO;

	dax->addr = ERR_PTR(-EIO);
	if (blk_queue_enter(q, true) != 0)
		return rc;

	rc = bdev_direct_access(bdev, dax);
	if (rc < 0) {
		dax->addr = ERR_PTR(rc);
		blk_queue_exit(q);
		return rc;
	}
	return rc;
}

static void dax_unmap_atomic(struct block_device *bdev,
		const struct blk_dax_ctl *dax)
{
	if (IS_ERR(dax->addr))
		return;
	blk_queue_exit(bdev->bd_queue);
}

static int dax_is_pmd_entry(void *entry)
{
	return (unsigned long)entry & RADIX_DAX_PMD;
}

static int dax_is_pte_entry(void *entry)
{
	return !((unsigned long)entry & RADIX_DAX_PMD);
}

static int dax_is_zero_entry(void *entry)
{
	return (unsigned long)entry & RADIX_DAX_HZP;
}

static int dax_is_empty_entry(void *entry)
{
	return (unsigned long)entry & RADIX_DAX_EMPTY;
}

struct page *read_dax_sector(struct block_device *bdev, sector_t n)
{
	struct page *page = alloc_pages(GFP_KERNEL, 0);
	struct blk_dax_ctl dax = {
		.size = PAGE_SIZE,
		.sector = n & ~((((int) PAGE_SIZE) / 512) - 1),
	};
	long rc;

	if (!page)
		return ERR_PTR(-ENOMEM);

	rc = dax_map_atomic(bdev, &dax);
	if (rc < 0)
		return ERR_PTR(rc);
	memcpy_from_pmem(page_address(page), dax.addr, PAGE_SIZE);
	dax_unmap_atomic(bdev, &dax);
	return page;
}

static bool buffer_written(struct buffer_head *bh)
{
	return buffer_mapped(bh) && !buffer_unwritten(bh);
}

static sector_t to_sector(const struct buffer_head *bh,
		const struct inode *inode)
{
	sector_t sector = bh->b_blocknr << (inode->i_blkbits - 9);

	return sector;
}

static ssize_t dax_io(struct inode *inode, struct iov_iter *iter,
		      loff_t start, loff_t end, get_block_t get_block,
		      struct buffer_head *bh)
{
	loff_t pos = start, max = start, bh_max = start;
	bool hole = false;
	struct block_device *bdev = NULL;
	int rw = iov_iter_rw(iter), rc;
	long map_len = 0;
	struct blk_dax_ctl dax = {
		.addr = ERR_PTR(-EIO),
	};
	unsigned blkbits = inode->i_blkbits;
	sector_t file_blks = (i_size_read(inode) + (1 << blkbits) - 1)
								>> blkbits;

	if (rw == READ)
		end = min(end, i_size_read(inode));

	while (pos < end) {
		size_t len;
		if (pos == max) {
			long page = pos >> PAGE_SHIFT;
			sector_t block = page << (PAGE_SHIFT - blkbits);
			unsigned first = pos - (block << blkbits);
			long size;

			if (pos == bh_max) {
				bh->b_size = PAGE_ALIGN(end - pos);
				bh->b_state = 0;
				rc = get_block(inode, block, bh, rw == WRITE);
				if (rc)
					break;
				bh_max = pos - first + bh->b_size;
				bdev = bh->b_bdev;
				/*
				 * We allow uninitialized buffers for writes
				 * beyond EOF as those cannot race with faults
				 */
				WARN_ON_ONCE(
					(buffer_new(bh) && block < file_blks) ||
					(rw == WRITE && buffer_unwritten(bh)));
			} else {
				unsigned done = bh->b_size -
						(bh_max - (pos - first));
				bh->b_blocknr += done >> blkbits;
				bh->b_size -= done;
			}

			hole = rw == READ && !buffer_written(bh);
			if (hole) {
				size = bh->b_size - first;
			} else {
				dax_unmap_atomic(bdev, &dax);
				dax.sector = to_sector(bh, inode);
				dax.size = bh->b_size;
				map_len = dax_map_atomic(bdev, &dax);
				if (map_len < 0) {
					rc = map_len;
					break;
				}
				dax.addr += first;
				size = map_len - first;
			}
			/*
			 * pos + size is one past the last offset for IO,
			 * so pos + size can overflow loff_t at extreme offsets.
			 * Cast to u64 to catch this and get the true minimum.
			 */
			max = min_t(u64, pos + size, end);
		}

		if (iov_iter_rw(iter) == WRITE) {
			len = copy_from_iter_pmem(dax.addr, max - pos, iter);
		} else if (!hole)
			len = copy_to_iter((void __force *) dax.addr, max - pos,
					iter);
		else
			len = iov_iter_zero(max - pos, iter);

		if (!len) {
			rc = -EFAULT;
			break;
		}

		pos += len;
		if (!IS_ERR(dax.addr))
			dax.addr += len;
	}

	dax_unmap_atomic(bdev, &dax);

	return (pos == start) ? rc : pos - start;
}

/**
 * dax_do_io - Perform I/O to a DAX file
 * @iocb: The control block for this I/O
 * @inode: The file which the I/O is directed at
 * @iter: The addresses to do I/O from or to
 * @get_block: The filesystem method used to translate file offsets to blocks
 * @end_io: A filesystem callback for I/O completion
 * @flags: See below
 *
 * This function uses the same locking scheme as do_blockdev_direct_IO:
 * If @flags has DIO_LOCKING set, we assume that the i_mutex is held by the
 * caller for writes.  For reads, we take and release the i_mutex ourselves.
 * If DIO_LOCKING is not set, the filesystem takes care of its own locking.
 * As with do_blockdev_direct_IO(), we increment i_dio_count while the I/O
 * is in progress.
 */
ssize_t dax_do_io(struct kiocb *iocb, struct inode *inode,
		  struct iov_iter *iter, get_block_t get_block,
		  dio_iodone_t end_io, int flags)
{
	struct buffer_head bh;
	ssize_t retval = -EINVAL;
	loff_t pos = iocb->ki_pos;
	loff_t end = pos + iov_iter_count(iter);

	memset(&bh, 0, sizeof(bh));
	bh.b_bdev = inode->i_sb->s_bdev;

	if ((flags & DIO_LOCKING) && iov_iter_rw(iter) == READ)
		inode_lock(inode);

	/* Protects against truncate */
	if (!(flags & DIO_SKIP_DIO_COUNT))
		inode_dio_begin(inode);

	retval = dax_io(inode, iter, pos, end, get_block, &bh);

	if ((flags & DIO_LOCKING) && iov_iter_rw(iter) == READ)
		inode_unlock(inode);

	if (end_io) {
		int err;

		err = end_io(iocb, pos, retval, bh.b_private);
		if (err)
			retval = err;
	}

	if (!(flags & DIO_SKIP_DIO_COUNT))
		inode_dio_end(inode);
	return retval;
}
EXPORT_SYMBOL_GPL(dax_do_io);

/*
 * DAX radix tree locking
 */
struct exceptional_entry_key {
	struct address_space *mapping;
	pgoff_t entry_start;
};

struct wait_exceptional_entry_queue {
	wait_queue_t wait;
	struct exceptional_entry_key key;
};

static wait_queue_head_t *dax_entry_waitqueue(struct address_space *mapping,
		pgoff_t index, void *entry, struct exceptional_entry_key *key)
{
	unsigned long hash;

	/*
	 * If 'entry' is a PMD, align the 'index' that we use for the wait
	 * queue to the start of that PMD.  This ensures that all offsets in
	 * the range covered by the PMD map to the same bit lock.
	 */
	if (dax_is_pmd_entry(entry))
		index &= ~((1UL << (PMD_SHIFT - PAGE_SHIFT)) - 1);

	key->mapping = mapping;
	key->entry_start = index;

	hash = hash_long((unsigned long)mapping ^ index, DAX_WAIT_TABLE_BITS);
	return wait_table + hash;
}

static int wake_exceptional_entry_func(wait_queue_t *wait, unsigned int mode,
				       int sync, void *keyp)
{
	struct exceptional_entry_key *key = keyp;
	struct wait_exceptional_entry_queue *ewait =
		container_of(wait, struct wait_exceptional_entry_queue, wait);

	if (key->mapping != ewait->key.mapping ||
	    key->entry_start != ewait->key.entry_start)
		return 0;
	return autoremove_wake_function(wait, mode, sync, NULL);
}

/*
 * Check whether the given slot is locked. The function must be called with
 * mapping->tree_lock held
 */
static inline int slot_locked(struct address_space *mapping, void **slot)
{
	unsigned long entry = (unsigned long)
		radix_tree_deref_slot_protected(slot, &mapping->tree_lock);
	return entry & RADIX_DAX_ENTRY_LOCK;
}

/*
 * Mark the given slot is locked. The function must be called with
 * mapping->tree_lock held
 */
static inline void *lock_slot(struct address_space *mapping, void **slot)
{
	unsigned long entry = (unsigned long)
		radix_tree_deref_slot_protected(slot, &mapping->tree_lock);

	entry |= RADIX_DAX_ENTRY_LOCK;
	radix_tree_replace_slot(slot, (void *)entry);
	return (void *)entry;
}

/*
 * Mark the given slot is unlocked. The function must be called with
 * mapping->tree_lock held
 */
static inline void *unlock_slot(struct address_space *mapping, void **slot)
{
	unsigned long entry = (unsigned long)
		radix_tree_deref_slot_protected(slot, &mapping->tree_lock);

	entry &= ~(unsigned long)RADIX_DAX_ENTRY_LOCK;
	radix_tree_replace_slot(slot, (void *)entry);
	return (void *)entry;
}

/*
 * Lookup entry in radix tree, wait for it to become unlocked if it is
 * exceptional entry and return it. The caller must call
 * put_unlocked_mapping_entry() when he decided not to lock the entry or
 * put_locked_mapping_entry() when he locked the entry and now wants to
 * unlock it.
 *
 * The function must be called with mapping->tree_lock held.
 */
static void *get_unlocked_mapping_entry(struct address_space *mapping,
					pgoff_t index, void ***slotp)
{
	void *entry, **slot;
	struct wait_exceptional_entry_queue ewait;
	wait_queue_head_t *wq;

	init_wait(&ewait.wait);
	ewait.wait.func = wake_exceptional_entry_func;

	for (;;) {
		entry = __radix_tree_lookup(&mapping->page_tree, index, NULL,
					  &slot);
		if (!entry || !radix_tree_exceptional_entry(entry) ||
		    !slot_locked(mapping, slot)) {
			if (slotp)
				*slotp = slot;
			return entry;
		}

		wq = dax_entry_waitqueue(mapping, index, entry, &ewait.key);
		prepare_to_wait_exclusive(wq, &ewait.wait,
					  TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&mapping->tree_lock);
		schedule();
		finish_wait(wq, &ewait.wait);
		spin_lock_irq(&mapping->tree_lock);
	}
}

static void put_locked_mapping_entry(struct address_space *mapping,
				     pgoff_t index, void *entry)
{
	if (!radix_tree_exceptional_entry(entry)) {
		unlock_page(entry);
		put_page(entry);
	} else {
		dax_unlock_mapping_entry(mapping, index);
	}
}

/*
 * Called when we are done with radix tree entry we looked up via
 * get_unlocked_mapping_entry() and which we didn't lock in the end.
 */
static void put_unlocked_mapping_entry(struct address_space *mapping,
				       pgoff_t index, void *entry)
{
	if (!radix_tree_exceptional_entry(entry))
		return;

	/* We have to wake up next waiter for the radix tree entry lock */
	dax_wake_mapping_entry_waiter(mapping, index, entry, false);
}

/*
 * Find radix tree entry at given index. If it points to a page, return with
 * the page locked. If it points to the exceptional entry, return with the
 * radix tree entry locked. If the radix tree doesn't contain given index,
 * create empty exceptional entry for the index and return with it locked.
 *
 * When requesting an entry with size RADIX_DAX_PMD, grab_mapping_entry() will
 * either return that locked entry or will return an error.  This error will
 * happen if there are any 4k entries (either zero pages or DAX entries)
 * within the 2MiB range that we are requesting.
 *
 * We always favor 4k entries over 2MiB entries. There isn't a flow where we
 * evict 4k entries in order to 'upgrade' them to a 2MiB entry.  A 2MiB
 * insertion will fail if it finds any 4k entries already in the tree, and a
 * 4k insertion will cause an existing 2MiB entry to be unmapped and
 * downgraded to 4k entries.  This happens for both 2MiB huge zero pages as
 * well as 2MiB empty entries.
 *
 * The exception to this downgrade path is for 2MiB DAX PMD entries that have
 * real storage backing them.  We will leave these real 2MiB DAX entries in
 * the tree, and PTE writes will simply dirty the entire 2MiB DAX entry.
 *
 * Note: Unlike filemap_fault() we don't honor FAULT_FLAG_RETRY flags. For
 * persistent memory the benefit is doubtful. We can add that later if we can
 * show it helps.
 */
static void *grab_mapping_entry(struct address_space *mapping, pgoff_t index,
		unsigned long size_flag)
{
	bool pmd_downgrade = false; /* splitting 2MiB entry into 4k entries? */
	void *entry, **slot;

restart:
	spin_lock_irq(&mapping->tree_lock);
	entry = get_unlocked_mapping_entry(mapping, index, &slot);

	if (entry) {
		if (size_flag & RADIX_DAX_PMD) {
			if (!radix_tree_exceptional_entry(entry) ||
			    dax_is_pte_entry(entry)) {
				put_unlocked_mapping_entry(mapping, index,
						entry);
				entry = ERR_PTR(-EEXIST);
				goto out_unlock;
			}
		} else { /* trying to grab a PTE entry */
			if (radix_tree_exceptional_entry(entry) &&
			    dax_is_pmd_entry(entry) &&
			    (dax_is_zero_entry(entry) ||
			     dax_is_empty_entry(entry))) {
				pmd_downgrade = true;
			}
		}
	}

	/* No entry for given index? Make sure radix tree is big enough. */
	if (!entry || pmd_downgrade) {
		int err;

		if (pmd_downgrade) {
			/*
			 * Make sure 'entry' remains valid while we drop
			 * mapping->tree_lock.
			 */
			entry = lock_slot(mapping, slot);
		}

		spin_unlock_irq(&mapping->tree_lock);
		err = radix_tree_preload(
				mapping_gfp_mask(mapping) & ~__GFP_HIGHMEM);
		if (err) {
			if (pmd_downgrade)
				put_locked_mapping_entry(mapping, index, entry);
			return ERR_PTR(err);
		}

		/*
		 * Besides huge zero pages the only other thing that gets
		 * downgraded are empty entries which don't need to be
		 * unmapped.
		 */
		if (pmd_downgrade && dax_is_zero_entry(entry))
			unmap_mapping_range(mapping,
				(index << PAGE_SHIFT) & PMD_MASK, PMD_SIZE, 0);

		spin_lock_irq(&mapping->tree_lock);

		if (pmd_downgrade) {
			radix_tree_delete(&mapping->page_tree, index);
			mapping->nrexceptional--;
			dax_wake_mapping_entry_waiter(mapping, index, entry,
					true);
		}

		entry = dax_radix_locked_entry(0, size_flag | RADIX_DAX_EMPTY);

		err = __radix_tree_insert(&mapping->page_tree, index,
				dax_radix_order(entry), entry);
		radix_tree_preload_end();
		if (err) {
			spin_unlock_irq(&mapping->tree_lock);
			/*
			 * Someone already created the entry?  This is a
			 * normal failure when inserting PMDs in a range
			 * that already contains PTEs.  In that case we want
			 * to return -EEXIST immediately.
			 */
			if (err == -EEXIST && !(size_flag & RADIX_DAX_PMD))
				goto restart;
			/*
			 * Our insertion of a DAX PMD entry failed, most
			 * likely because it collided with a PTE sized entry
			 * at a different index in the PMD range.  We haven't
			 * inserted anything into the radix tree and have no
			 * waiters to wake.
			 */
			return ERR_PTR(err);
		}
		/* Good, we have inserted empty locked entry into the tree. */
		mapping->nrexceptional++;
		spin_unlock_irq(&mapping->tree_lock);
		return entry;
	}
	/* Normal page in radix tree? */
	if (!radix_tree_exceptional_entry(entry)) {
		struct page *page = entry;

		get_page(page);
		spin_unlock_irq(&mapping->tree_lock);
		lock_page(page);
		/* Page got truncated? Retry... */
		if (unlikely(page->mapping != mapping)) {
			unlock_page(page);
			put_page(page);
			goto restart;
		}
		return page;
	}
	entry = lock_slot(mapping, slot);
 out_unlock:
	spin_unlock_irq(&mapping->tree_lock);
	return entry;
}

/*
 * We do not necessarily hold the mapping->tree_lock when we call this
 * function so it is possible that 'entry' is no longer a valid item in the
 * radix tree.  This is okay because all we really need to do is to find the
 * correct waitqueue where tasks might be waiting for that old 'entry' and
 * wake them.
 */
void dax_wake_mapping_entry_waiter(struct address_space *mapping,
		pgoff_t index, void *entry, bool wake_all)
{
	struct exceptional_entry_key key;
	wait_queue_head_t *wq;

	wq = dax_entry_waitqueue(mapping, index, entry, &key);

	/*
	 * Checking for locked entry and prepare_to_wait_exclusive() happens
	 * under mapping->tree_lock, ditto for entry handling in our callers.
	 * So at this point all tasks that could have seen our entry locked
	 * must be in the waitqueue and the following check will see them.
	 */
	if (waitqueue_active(wq))
		__wake_up(wq, TASK_NORMAL, wake_all ? 0 : 1, &key);
}

void dax_unlock_mapping_entry(struct address_space *mapping, pgoff_t index)
{
	void *entry, **slot;

	spin_lock_irq(&mapping->tree_lock);
	entry = __radix_tree_lookup(&mapping->page_tree, index, NULL, &slot);
	if (WARN_ON_ONCE(!entry || !radix_tree_exceptional_entry(entry) ||
			 !slot_locked(mapping, slot))) {
		spin_unlock_irq(&mapping->tree_lock);
		return;
	}
	unlock_slot(mapping, slot);
	spin_unlock_irq(&mapping->tree_lock);
	dax_wake_mapping_entry_waiter(mapping, index, entry, false);
}

/*
 * Delete exceptional DAX entry at @index from @mapping. Wait for radix tree
 * entry to get unlocked before deleting it.
 */
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index)
{
	void *entry;

	spin_lock_irq(&mapping->tree_lock);
	entry = get_unlocked_mapping_entry(mapping, index, NULL);
	/*
	 * This gets called from truncate / punch_hole path. As such, the caller
	 * must hold locks protecting against concurrent modifications of the
	 * radix tree (usually fs-private i_mmap_sem for writing). Since the
	 * caller has seen exceptional entry for this index, we better find it
	 * at that index as well...
	 */
	if (WARN_ON_ONCE(!entry || !radix_tree_exceptional_entry(entry))) {
		spin_unlock_irq(&mapping->tree_lock);
		return 0;
	}
	radix_tree_delete(&mapping->page_tree, index);
	mapping->nrexceptional--;
	spin_unlock_irq(&mapping->tree_lock);
	dax_wake_mapping_entry_waiter(mapping, index, entry, true);

	return 1;
}

/*
 * The user has performed a load from a hole in the file.  Allocating
 * a new page in the file would cause excessive storage usage for
 * workloads with sparse files.  We allocate a page cache page instead.
 * We'll kick it out of the page cache if it's ever written to,
 * otherwise it will simply fall out of the page cache under memory
 * pressure without ever having been dirtied.
 */
static int dax_load_hole(struct address_space *mapping, void *entry,
			 struct vm_fault *vmf)
{
	struct page *page;

	/* Hole page already exists? Return it...  */
	if (!radix_tree_exceptional_entry(entry)) {
		vmf->page = entry;
		return VM_FAULT_LOCKED;
	}

	/* This will replace locked radix tree entry with a hole page */
	page = find_or_create_page(mapping, vmf->pgoff,
				   vmf->gfp_mask | __GFP_ZERO);
	if (!page) {
		put_locked_mapping_entry(mapping, vmf->pgoff, entry);
		return VM_FAULT_OOM;
	}
	vmf->page = page;
	return VM_FAULT_LOCKED;
}

static int copy_user_dax(struct block_device *bdev, sector_t sector, size_t size,
		struct page *to, unsigned long vaddr)
{
	struct blk_dax_ctl dax = {
		.sector = sector,
		.size = size,
	};
	void *vto;

	if (dax_map_atomic(bdev, &dax) < 0)
		return PTR_ERR(dax.addr);
	vto = kmap_atomic(to);
	copy_user_page(vto, (void __force *)dax.addr, vaddr, to);
	kunmap_atomic(vto);
	dax_unmap_atomic(bdev, &dax);
	return 0;
}

/*
 * By this point grab_mapping_entry() has ensured that we have a locked entry
 * of the appropriate size so we don't have to worry about downgrading PMDs to
 * PTEs.  If we happen to be trying to insert a PTE and there is a PMD
 * already in the tree, we will skip the insertion and just dirty the PMD as
 * appropriate.
 */
static void *dax_insert_mapping_entry(struct address_space *mapping,
				      struct vm_fault *vmf,
				      void *entry, sector_t sector,
				      unsigned long flags)
{
	struct radix_tree_root *page_tree = &mapping->page_tree;
	int error = 0;
	bool hole_fill = false;
	void *new_entry;
	pgoff_t index = vmf->pgoff;

	if (vmf->flags & FAULT_FLAG_WRITE)
		__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);

	/* Replacing hole page with block mapping? */
	if (!radix_tree_exceptional_entry(entry)) {
		hole_fill = true;
		/*
		 * Unmap the page now before we remove it from page cache below.
		 * The page is locked so it cannot be faulted in again.
		 */
		unmap_mapping_range(mapping, vmf->pgoff << PAGE_SHIFT,
				    PAGE_SIZE, 0);
		error = radix_tree_preload(vmf->gfp_mask & ~__GFP_HIGHMEM);
		if (error)
			return ERR_PTR(error);
	} else if (dax_is_zero_entry(entry) && !(flags & RADIX_DAX_HZP)) {
		/* replacing huge zero page with PMD block mapping */
		unmap_mapping_range(mapping,
			(vmf->pgoff << PAGE_SHIFT) & PMD_MASK, PMD_SIZE, 0);
	}

	spin_lock_irq(&mapping->tree_lock);
	new_entry = dax_radix_locked_entry(sector, flags);

	if (hole_fill) {
		__delete_from_page_cache(entry, NULL);
		/* Drop pagecache reference */
		put_page(entry);
		error = __radix_tree_insert(page_tree, index,
				dax_radix_order(new_entry), new_entry);
		if (error) {
			new_entry = ERR_PTR(error);
			goto unlock;
		}
		mapping->nrexceptional++;
	} else if (dax_is_zero_entry(entry) || dax_is_empty_entry(entry)) {
		/*
		 * Only swap our new entry into the radix tree if the current
		 * entry is a zero page or an empty entry.  If a normal PTE or
		 * PMD entry is already in the tree, we leave it alone.  This
		 * means that if we are trying to insert a PTE and the
		 * existing entry is a PMD, we will just leave the PMD in the
		 * tree and dirty it if necessary.
		 */
		void **slot;
		void *ret;

		ret = __radix_tree_lookup(page_tree, index, NULL, &slot);
		WARN_ON_ONCE(ret != entry);
		radix_tree_replace_slot(slot, new_entry);
	}
	if (vmf->flags & FAULT_FLAG_WRITE)
		radix_tree_tag_set(page_tree, index, PAGECACHE_TAG_DIRTY);
 unlock:
	spin_unlock_irq(&mapping->tree_lock);
	if (hole_fill) {
		radix_tree_preload_end();
		/*
		 * We don't need hole page anymore, it has been replaced with
		 * locked radix tree entry now.
		 */
		if (mapping->a_ops->freepage)
			mapping->a_ops->freepage(entry);
		unlock_page(entry);
		put_page(entry);
	}
	return new_entry;
}

static int dax_writeback_one(struct block_device *bdev,
		struct address_space *mapping, pgoff_t index, void *entry)
{
	struct radix_tree_root *page_tree = &mapping->page_tree;
	struct radix_tree_node *node;
	struct blk_dax_ctl dax;
	void **slot;
	int ret = 0;

	spin_lock_irq(&mapping->tree_lock);
	/*
	 * Regular page slots are stabilized by the page lock even
	 * without the tree itself locked.  These unlocked entries
	 * need verification under the tree lock.
	 */
	if (!__radix_tree_lookup(page_tree, index, &node, &slot))
		goto unlock;
	if (*slot != entry)
		goto unlock;

	/* another fsync thread may have already written back this entry */
	if (!radix_tree_tag_get(page_tree, index, PAGECACHE_TAG_TOWRITE))
		goto unlock;

	if (WARN_ON_ONCE(dax_is_empty_entry(entry) ||
				dax_is_zero_entry(entry))) {
		ret = -EIO;
		goto unlock;
	}

	/*
	 * Even if dax_writeback_mapping_range() was given a wbc->range_start
	 * in the middle of a PMD, the 'index' we are given will be aligned to
	 * the start index of the PMD, as will the sector we pull from
	 * 'entry'.  This allows us to flush for PMD_SIZE and not have to
	 * worry about partial PMD writebacks.
	 */
	dax.sector = dax_radix_sector(entry);
	dax.size = PAGE_SIZE << dax_radix_order(entry);
	spin_unlock_irq(&mapping->tree_lock);

	/*
	 * We cannot hold tree_lock while calling dax_map_atomic() because it
	 * eventually calls cond_resched().
	 */
	ret = dax_map_atomic(bdev, &dax);
	if (ret < 0)
		return ret;

	if (WARN_ON_ONCE(ret < dax.size)) {
		ret = -EIO;
		goto unmap;
	}

	wb_cache_pmem(dax.addr, dax.size);

	spin_lock_irq(&mapping->tree_lock);
	radix_tree_tag_clear(page_tree, index, PAGECACHE_TAG_TOWRITE);
	spin_unlock_irq(&mapping->tree_lock);
 unmap:
	dax_unmap_atomic(bdev, &dax);
	return ret;

 unlock:
	spin_unlock_irq(&mapping->tree_lock);
	return ret;
}

/*
 * Flush the mapping to the persistent domain within the byte range of [start,
 * end]. This is required by data integrity operations to ensure file data is
 * on persistent storage prior to completion of the operation.
 */
int dax_writeback_mapping_range(struct address_space *mapping,
		struct block_device *bdev, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	pgoff_t start_index, end_index;
	pgoff_t indices[PAGEVEC_SIZE];
	struct pagevec pvec;
	bool done = false;
	int i, ret = 0;

	if (WARN_ON_ONCE(inode->i_blkbits != PAGE_SHIFT))
		return -EIO;

	if (!mapping->nrexceptional || wbc->sync_mode != WB_SYNC_ALL)
		return 0;

	start_index = wbc->range_start >> PAGE_SHIFT;
	end_index = wbc->range_end >> PAGE_SHIFT;

	tag_pages_for_writeback(mapping, start_index, end_index);

	pagevec_init(&pvec, 0);
	while (!done) {
		pvec.nr = find_get_entries_tag(mapping, start_index,
				PAGECACHE_TAG_TOWRITE, PAGEVEC_SIZE,
				pvec.pages, indices);

		if (pvec.nr == 0)
			break;

		for (i = 0; i < pvec.nr; i++) {
			if (indices[i] > end_index) {
				done = true;
				break;
			}

			ret = dax_writeback_one(bdev, mapping, indices[i],
					pvec.pages[i]);
			if (ret < 0)
				return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(dax_writeback_mapping_range);

static int dax_insert_mapping(struct address_space *mapping,
		struct block_device *bdev, sector_t sector, size_t size,
		void **entryp, struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long vaddr = (unsigned long)vmf->virtual_address;
	struct blk_dax_ctl dax = {
		.sector = sector,
		.size = size,
	};
	void *ret;
	void *entry = *entryp;

	if (dax_map_atomic(bdev, &dax) < 0)
		return PTR_ERR(dax.addr);
	dax_unmap_atomic(bdev, &dax);

	ret = dax_insert_mapping_entry(mapping, vmf, entry, dax.sector, 0);
	if (IS_ERR(ret))
		return PTR_ERR(ret);
	*entryp = ret;

	return vm_insert_mixed(vma, vaddr, dax.pfn);
}

/**
 * dax_fault - handle a page fault on a DAX file
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 * @get_block: The filesystem method used to translate file offsets to blocks
 *
 * When a page fault occurs, filesystems may call this helper in their
 * fault handler for DAX files. dax_fault() assumes the caller has done all
 * the necessary locking for the page fault to proceed successfully.
 */
int dax_fault(struct vm_area_struct *vma, struct vm_fault *vmf,
			get_block_t get_block)
{
	struct file *file = vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	void *entry;
	struct buffer_head bh;
	unsigned long vaddr = (unsigned long)vmf->virtual_address;
	unsigned blkbits = inode->i_blkbits;
	sector_t block;
	pgoff_t size;
	int error;
	int major = 0;

	/*
	 * Check whether offset isn't beyond end of file now. Caller is supposed
	 * to hold locks serializing us with truncate / punch hole so this is
	 * a reliable test.
	 */
	size = (i_size_read(inode) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (vmf->pgoff >= size)
		return VM_FAULT_SIGBUS;

	memset(&bh, 0, sizeof(bh));
	block = (sector_t)vmf->pgoff << (PAGE_SHIFT - blkbits);
	bh.b_bdev = inode->i_sb->s_bdev;
	bh.b_size = PAGE_SIZE;

	entry = grab_mapping_entry(mapping, vmf->pgoff, 0);
	if (IS_ERR(entry)) {
		error = PTR_ERR(entry);
		goto out;
	}

	error = get_block(inode, block, &bh, 0);
	if (!error && (bh.b_size < PAGE_SIZE))
		error = -EIO;		/* fs corruption? */
	if (error)
		goto unlock_entry;

	if (vmf->cow_page) {
		struct page *new_page = vmf->cow_page;
		if (buffer_written(&bh))
			error = copy_user_dax(bh.b_bdev, to_sector(&bh, inode),
					bh.b_size, new_page, vaddr);
		else
			clear_user_highpage(new_page, vaddr);
		if (error)
			goto unlock_entry;
		if (!radix_tree_exceptional_entry(entry)) {
			vmf->page = entry;
			return VM_FAULT_LOCKED;
		}
		vmf->entry = entry;
		return VM_FAULT_DAX_LOCKED;
	}

	if (!buffer_mapped(&bh)) {
		if (vmf->flags & FAULT_FLAG_WRITE) {
			error = get_block(inode, block, &bh, 1);
			count_vm_event(PGMAJFAULT);
			mem_cgroup_count_vm_event(vma->vm_mm, PGMAJFAULT);
			major = VM_FAULT_MAJOR;
			if (!error && (bh.b_size < PAGE_SIZE))
				error = -EIO;
			if (error)
				goto unlock_entry;
		} else {
			return dax_load_hole(mapping, entry, vmf);
		}
	}

	/* Filesystem should not return unwritten buffers to us! */
	WARN_ON_ONCE(buffer_unwritten(&bh) || buffer_new(&bh));
	error = dax_insert_mapping(mapping, bh.b_bdev, to_sector(&bh, inode),
			bh.b_size, &entry, vma, vmf);
 unlock_entry:
	put_locked_mapping_entry(mapping, vmf->pgoff, entry);
 out:
	if (error == -ENOMEM)
		return VM_FAULT_OOM | major;
	/* -EBUSY is fine, somebody else faulted on the same PTE */
	if ((error < 0) && (error != -EBUSY))
		return VM_FAULT_SIGBUS | major;
	return VM_FAULT_NOPAGE | major;
}
EXPORT_SYMBOL_GPL(dax_fault);

/**
 * dax_pfn_mkwrite - handle first write to DAX page
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 */
int dax_pfn_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	void *entry;
	pgoff_t index = vmf->pgoff;

	spin_lock_irq(&mapping->tree_lock);
	entry = get_unlocked_mapping_entry(mapping, index, NULL);
	if (!entry || !radix_tree_exceptional_entry(entry))
		goto out;
	radix_tree_tag_set(&mapping->page_tree, index, PAGECACHE_TAG_DIRTY);
	put_unlocked_mapping_entry(mapping, index, entry);
out:
	spin_unlock_irq(&mapping->tree_lock);
	return VM_FAULT_NOPAGE;
}
EXPORT_SYMBOL_GPL(dax_pfn_mkwrite);

static bool dax_range_is_aligned(struct block_device *bdev,
				 unsigned int offset, unsigned int length)
{
	unsigned short sector_size = bdev_logical_block_size(bdev);

	if (!IS_ALIGNED(offset, sector_size))
		return false;
	if (!IS_ALIGNED(length, sector_size))
		return false;

	return true;
}

int __dax_zero_page_range(struct block_device *bdev, sector_t sector,
		unsigned int offset, unsigned int length)
{
	struct blk_dax_ctl dax = {
		.sector		= sector,
		.size		= PAGE_SIZE,
	};

	if (dax_range_is_aligned(bdev, offset, length)) {
		sector_t start_sector = dax.sector + (offset >> 9);

		return blkdev_issue_zeroout(bdev, start_sector,
				length >> 9, GFP_NOFS, true);
	} else {
		if (dax_map_atomic(bdev, &dax) < 0)
			return PTR_ERR(dax.addr);
		clear_pmem(dax.addr + offset, length);
		dax_unmap_atomic(bdev, &dax);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__dax_zero_page_range);

/**
 * dax_zero_page_range - zero a range within a page of a DAX file
 * @inode: The file being truncated
 * @from: The file offset that is being truncated to
 * @length: The number of bytes to zero
 * @get_block: The filesystem method used to translate file offsets to blocks
 *
 * This function can be called by a filesystem when it is zeroing part of a
 * page in a DAX file.  This is intended for hole-punch operations.  If
 * you are truncating a file, the helper function dax_truncate_page() may be
 * more convenient.
 */
int dax_zero_page_range(struct inode *inode, loff_t from, unsigned length,
							get_block_t get_block)
{
	struct buffer_head bh;
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned offset = from & (PAGE_SIZE-1);
	int err;

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;
	if (WARN_ON_ONCE((offset + length) > PAGE_SIZE))
		return -EINVAL;

	memset(&bh, 0, sizeof(bh));
	bh.b_bdev = inode->i_sb->s_bdev;
	bh.b_size = PAGE_SIZE;
	err = get_block(inode, index, &bh, 0);
	if (err < 0 || !buffer_written(&bh))
		return err;

	return __dax_zero_page_range(bh.b_bdev, to_sector(&bh, inode),
			offset, length);
}
EXPORT_SYMBOL_GPL(dax_zero_page_range);

/**
 * dax_truncate_page - handle a partial page being truncated in a DAX file
 * @inode: The file being truncated
 * @from: The file offset that is being truncated to
 * @get_block: The filesystem method used to translate file offsets to blocks
 *
 * Similar to block_truncate_page(), this function can be called by a
 * filesystem when it is truncating a DAX file to handle the partial page.
 */
int dax_truncate_page(struct inode *inode, loff_t from, get_block_t get_block)
{
	unsigned length = PAGE_ALIGN(from) - from;
	return dax_zero_page_range(inode, from, length, get_block);
}
EXPORT_SYMBOL_GPL(dax_truncate_page);

#ifdef CONFIG_FS_IOMAP
static sector_t dax_iomap_sector(struct iomap *iomap, loff_t pos)
{
	return iomap->blkno + (((pos & PAGE_MASK) - iomap->offset) >> 9);
}

static loff_t
dax_iomap_actor(struct inode *inode, loff_t pos, loff_t length, void *data,
		struct iomap *iomap)
{
	struct iov_iter *iter = data;
	loff_t end = pos + length, done = 0;
	ssize_t ret = 0;

	if (iov_iter_rw(iter) == READ) {
		end = min(end, i_size_read(inode));
		if (pos >= end)
			return 0;

		if (iomap->type == IOMAP_HOLE || iomap->type == IOMAP_UNWRITTEN)
			return iov_iter_zero(min(length, end - pos), iter);
	}

	if (WARN_ON_ONCE(iomap->type != IOMAP_MAPPED))
		return -EIO;

	while (pos < end) {
		unsigned offset = pos & (PAGE_SIZE - 1);
		struct blk_dax_ctl dax = { 0 };
		ssize_t map_len;

		dax.sector = dax_iomap_sector(iomap, pos);
		dax.size = (length + offset + PAGE_SIZE - 1) & PAGE_MASK;
		map_len = dax_map_atomic(iomap->bdev, &dax);
		if (map_len < 0) {
			ret = map_len;
			break;
		}

		dax.addr += offset;
		map_len -= offset;
		if (map_len > end - pos)
			map_len = end - pos;

		if (iov_iter_rw(iter) == WRITE)
			map_len = copy_from_iter_pmem(dax.addr, map_len, iter);
		else
			map_len = copy_to_iter(dax.addr, map_len, iter);
		dax_unmap_atomic(iomap->bdev, &dax);
		if (map_len <= 0) {
			ret = map_len ? map_len : -EFAULT;
			break;
		}

		pos += map_len;
		length -= map_len;
		done += map_len;
	}

	return done ? done : ret;
}

/**
 * dax_iomap_rw - Perform I/O to a DAX file
 * @iocb:	The control block for this I/O
 * @iter:	The addresses to do I/O from or to
 * @ops:	iomap ops passed from the file system
 *
 * This function performs read and write operations to directly mapped
 * persistent memory.  The callers needs to take care of read/write exclusion
 * and evicting any page cache pages in the region under I/O.
 */
ssize_t
dax_iomap_rw(struct kiocb *iocb, struct iov_iter *iter,
		struct iomap_ops *ops)
{
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	struct inode *inode = mapping->host;
	loff_t pos = iocb->ki_pos, ret = 0, done = 0;
	unsigned flags = 0;

	if (iov_iter_rw(iter) == WRITE)
		flags |= IOMAP_WRITE;

	/*
	 * Yes, even DAX files can have page cache attached to them:  A zeroed
	 * page is inserted into the pagecache when we have to serve a write
	 * fault on a hole.  It should never be dirtied and can simply be
	 * dropped from the pagecache once we get real data for the page.
	 *
	 * XXX: This is racy against mmap, and there's nothing we can do about
	 * it. We'll eventually need to shift this down even further so that
	 * we can check if we allocated blocks over a hole first.
	 */
	if (mapping->nrpages) {
		ret = invalidate_inode_pages2_range(mapping,
				pos >> PAGE_SHIFT,
				(pos + iov_iter_count(iter) - 1) >> PAGE_SHIFT);
		WARN_ON_ONCE(ret);
	}

	while (iov_iter_count(iter)) {
		ret = iomap_apply(inode, pos, iov_iter_count(iter), flags, ops,
				iter, dax_iomap_actor);
		if (ret <= 0)
			break;
		pos += ret;
		done += ret;
	}

	iocb->ki_pos += done;
	return done ? done : ret;
}
EXPORT_SYMBOL_GPL(dax_iomap_rw);

/**
 * dax_iomap_fault - handle a page fault on a DAX file
 * @vma: The virtual memory area where the fault occurred
 * @vmf: The description of the fault
 * @ops: iomap ops passed from the file system
 *
 * When a page fault occurs, filesystems may call this helper in their fault
 * or mkwrite handler for DAX files. Assumes the caller has done all the
 * necessary locking for the page fault to proceed successfully.
 */
int dax_iomap_fault(struct vm_area_struct *vma, struct vm_fault *vmf,
			struct iomap_ops *ops)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long vaddr = (unsigned long)vmf->virtual_address;
	loff_t pos = (loff_t)vmf->pgoff << PAGE_SHIFT;
	sector_t sector;
	struct iomap iomap = { 0 };
	unsigned flags = 0;
	int error, major = 0;
	int locked_status = 0;
	void *entry;

	/*
	 * Check whether offset isn't beyond end of file now. Caller is supposed
	 * to hold locks serializing us with truncate / punch hole so this is
	 * a reliable test.
	 */
	if (pos >= i_size_read(inode))
		return VM_FAULT_SIGBUS;

	entry = grab_mapping_entry(mapping, vmf->pgoff, 0);
	if (IS_ERR(entry)) {
		error = PTR_ERR(entry);
		goto out;
	}

	if ((vmf->flags & FAULT_FLAG_WRITE) && !vmf->cow_page)
		flags |= IOMAP_WRITE;

	/*
	 * Note that we don't bother to use iomap_apply here: DAX required
	 * the file system block size to be equal the page size, which means
	 * that we never have to deal with more than a single extent here.
	 */
	error = ops->iomap_begin(inode, pos, PAGE_SIZE, flags, &iomap);
	if (error)
		goto unlock_entry;
	if (WARN_ON_ONCE(iomap.offset + iomap.length < pos + PAGE_SIZE)) {
		error = -EIO;		/* fs corruption? */
		goto finish_iomap;
	}

	sector = dax_iomap_sector(&iomap, pos);

	if (vmf->cow_page) {
		switch (iomap.type) {
		case IOMAP_HOLE:
		case IOMAP_UNWRITTEN:
			clear_user_highpage(vmf->cow_page, vaddr);
			break;
		case IOMAP_MAPPED:
			error = copy_user_dax(iomap.bdev, sector, PAGE_SIZE,
					vmf->cow_page, vaddr);
			break;
		default:
			WARN_ON_ONCE(1);
			error = -EIO;
			break;
		}

		if (error)
			goto finish_iomap;
		if (!radix_tree_exceptional_entry(entry)) {
			vmf->page = entry;
			locked_status = VM_FAULT_LOCKED;
		} else {
			vmf->entry = entry;
			locked_status = VM_FAULT_DAX_LOCKED;
		}
		goto finish_iomap;
	}

	switch (iomap.type) {
	case IOMAP_MAPPED:
		if (iomap.flags & IOMAP_F_NEW) {
			count_vm_event(PGMAJFAULT);
			mem_cgroup_count_vm_event(vma->vm_mm, PGMAJFAULT);
			major = VM_FAULT_MAJOR;
		}
		error = dax_insert_mapping(mapping, iomap.bdev, sector,
				PAGE_SIZE, &entry, vma, vmf);
		break;
	case IOMAP_UNWRITTEN:
	case IOMAP_HOLE:
		if (!(vmf->flags & FAULT_FLAG_WRITE)) {
			locked_status = dax_load_hole(mapping, entry, vmf);
			break;
		}
		/*FALLTHRU*/
	default:
		WARN_ON_ONCE(1);
		error = -EIO;
		break;
	}

 finish_iomap:
	if (ops->iomap_end) {
		if (error) {
			/* keep previous error */
			ops->iomap_end(inode, pos, PAGE_SIZE, 0, flags,
					&iomap);
		} else {
			error = ops->iomap_end(inode, pos, PAGE_SIZE,
					PAGE_SIZE, flags, &iomap);
		}
	}
 unlock_entry:
	if (!locked_status || error)
		put_locked_mapping_entry(mapping, vmf->pgoff, entry);
 out:
	if (error == -ENOMEM)
		return VM_FAULT_OOM | major;
	/* -EBUSY is fine, somebody else faulted on the same PTE */
	if (error < 0 && error != -EBUSY)
		return VM_FAULT_SIGBUS | major;
	if (locked_status) {
		WARN_ON_ONCE(error); /* -EBUSY from ops->iomap_end? */
		return locked_status;
	}
	return VM_FAULT_NOPAGE | major;
}
EXPORT_SYMBOL_GPL(dax_iomap_fault);

#ifdef CONFIG_FS_DAX_PMD
/*
 * The 'colour' (ie low bits) within a PMD of a page offset.  This comes up
 * more often than one might expect in the below functions.
 */
#define PG_PMD_COLOUR	((PMD_SIZE >> PAGE_SHIFT) - 1)

static int dax_pmd_insert_mapping(struct vm_area_struct *vma, pmd_t *pmd,
		struct vm_fault *vmf, unsigned long address,
		struct iomap *iomap, loff_t pos, bool write, void **entryp)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	struct block_device *bdev = iomap->bdev;
	struct blk_dax_ctl dax = {
		.sector = dax_iomap_sector(iomap, pos),
		.size = PMD_SIZE,
	};
	long length = dax_map_atomic(bdev, &dax);
	void *ret;

	if (length < 0) /* dax_map_atomic() failed */
		return VM_FAULT_FALLBACK;
	if (length < PMD_SIZE)
		goto unmap_fallback;
	if (pfn_t_to_pfn(dax.pfn) & PG_PMD_COLOUR)
		goto unmap_fallback;
	if (!pfn_t_devmap(dax.pfn))
		goto unmap_fallback;

	dax_unmap_atomic(bdev, &dax);

	ret = dax_insert_mapping_entry(mapping, vmf, *entryp, dax.sector,
			RADIX_DAX_PMD);
	if (IS_ERR(ret))
		return VM_FAULT_FALLBACK;
	*entryp = ret;

	return vmf_insert_pfn_pmd(vma, address, pmd, dax.pfn, write);

 unmap_fallback:
	dax_unmap_atomic(bdev, &dax);
	return VM_FAULT_FALLBACK;
}

static int dax_pmd_load_hole(struct vm_area_struct *vma, pmd_t *pmd,
		struct vm_fault *vmf, unsigned long address,
		struct iomap *iomap, void **entryp)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	unsigned long pmd_addr = address & PMD_MASK;
	struct page *zero_page;
	spinlock_t *ptl;
	pmd_t pmd_entry;
	void *ret;

	zero_page = mm_get_huge_zero_page(vma->vm_mm);

	if (unlikely(!zero_page))
		return VM_FAULT_FALLBACK;

	ret = dax_insert_mapping_entry(mapping, vmf, *entryp, 0,
			RADIX_DAX_PMD | RADIX_DAX_HZP);
	if (IS_ERR(ret))
		return VM_FAULT_FALLBACK;
	*entryp = ret;

	ptl = pmd_lock(vma->vm_mm, pmd);
	if (!pmd_none(*pmd)) {
		spin_unlock(ptl);
		return VM_FAULT_FALLBACK;
	}

	pmd_entry = mk_pmd(zero_page, vma->vm_page_prot);
	pmd_entry = pmd_mkhuge(pmd_entry);
	set_pmd_at(vma->vm_mm, pmd_addr, pmd, pmd_entry);
	spin_unlock(ptl);
	return VM_FAULT_NOPAGE;
}

int dax_iomap_pmd_fault(struct vm_area_struct *vma, unsigned long address,
		pmd_t *pmd, unsigned int flags, struct iomap_ops *ops)
{
	struct address_space *mapping = vma->vm_file->f_mapping;
	unsigned long pmd_addr = address & PMD_MASK;
	bool write = flags & FAULT_FLAG_WRITE;
	unsigned int iomap_flags = write ? IOMAP_WRITE : 0;
	struct inode *inode = mapping->host;
	int result = VM_FAULT_FALLBACK;
	struct iomap iomap = { 0 };
	pgoff_t max_pgoff, pgoff;
	struct vm_fault vmf;
	void *entry;
	loff_t pos;
	int error;

	/* Fall back to PTEs if we're going to COW */
	if (write && !(vma->vm_flags & VM_SHARED))
		goto fallback;

	/* If the PMD would extend outside the VMA */
	if (pmd_addr < vma->vm_start)
		goto fallback;
	if ((pmd_addr + PMD_SIZE) > vma->vm_end)
		goto fallback;

	/*
	 * Check whether offset isn't beyond end of file now. Caller is
	 * supposed to hold locks serializing us with truncate / punch hole so
	 * this is a reliable test.
	 */
	pgoff = linear_page_index(vma, pmd_addr);
	max_pgoff = (i_size_read(inode) - 1) >> PAGE_SHIFT;

	if (pgoff > max_pgoff)
		return VM_FAULT_SIGBUS;

	/* If the PMD would extend beyond the file size */
	if ((pgoff | PG_PMD_COLOUR) > max_pgoff)
		goto fallback;

	/*
	 * grab_mapping_entry() will make sure we get a 2M empty entry, a DAX
	 * PMD or a HZP entry.  If it can't (because a 4k page is already in
	 * the tree, for instance), it will return -EEXIST and we just fall
	 * back to 4k entries.
	 */
	entry = grab_mapping_entry(mapping, pgoff, RADIX_DAX_PMD);
	if (IS_ERR(entry))
		goto fallback;

	/*
	 * Note that we don't use iomap_apply here.  We aren't doing I/O, only
	 * setting up a mapping, so really we're using iomap_begin() as a way
	 * to look up our filesystem block.
	 */
	pos = (loff_t)pgoff << PAGE_SHIFT;
	error = ops->iomap_begin(inode, pos, PMD_SIZE, iomap_flags, &iomap);
	if (error)
		goto unlock_entry;
	if (iomap.offset + iomap.length < pos + PMD_SIZE)
		goto finish_iomap;

	vmf.pgoff = pgoff;
	vmf.flags = flags;
	vmf.gfp_mask = mapping_gfp_mask(mapping) | __GFP_IO;

	switch (iomap.type) {
	case IOMAP_MAPPED:
		result = dax_pmd_insert_mapping(vma, pmd, &vmf, address,
				&iomap, pos, write, &entry);
		break;
	case IOMAP_UNWRITTEN:
	case IOMAP_HOLE:
		if (WARN_ON_ONCE(write))
			goto finish_iomap;
		result = dax_pmd_load_hole(vma, pmd, &vmf, address, &iomap,
				&entry);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

 finish_iomap:
	if (ops->iomap_end) {
		if (result == VM_FAULT_FALLBACK) {
			ops->iomap_end(inode, pos, PMD_SIZE, 0, iomap_flags,
					&iomap);
		} else {
			error = ops->iomap_end(inode, pos, PMD_SIZE, PMD_SIZE,
					iomap_flags, &iomap);
			if (error)
				result = VM_FAULT_FALLBACK;
		}
	}
 unlock_entry:
	put_locked_mapping_entry(mapping, pgoff, entry);
 fallback:
	if (result == VM_FAULT_FALLBACK) {
		split_huge_pmd(vma, pmd, address);
		count_vm_event(THP_FAULT_FALLBACK);
	}
	return result;
}
EXPORT_SYMBOL_GPL(dax_iomap_pmd_fault);
#endif /* CONFIG_FS_DAX_PMD */
#endif /* CONFIG_FS_IOMAP */
