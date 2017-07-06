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
#include <linux/sched/signal.h>
#include <linux/uio.h>
#include <linux/vmstat.h>
#include <linux/pfn_t.h>
#include <linux/sizes.h>
#include <linux/mmu_notifier.h>
#include <linux/iomap.h>
#include "internal.h"

#define CREATE_TRACE_POINTS
#include <trace/events/fs_dax.h>

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

/*
 * DAX radix tree locking
 */
struct exceptional_entry_key {
	struct address_space *mapping;
	pgoff_t entry_start;
};

struct wait_exceptional_entry_queue {
	wait_queue_entry_t wait;
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

static int wake_exceptional_entry_func(wait_queue_entry_t *wait, unsigned int mode,
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
	radix_tree_replace_slot(&mapping->page_tree, slot, (void *)entry);
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
	radix_tree_replace_slot(&mapping->page_tree, slot, (void *)entry);
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

static void dax_unlock_mapping_entry(struct address_space *mapping,
				     pgoff_t index)
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
		/*
		 * Besides huge zero pages the only other thing that gets
		 * downgraded are empty entries which don't need to be
		 * unmapped.
		 */
		if (pmd_downgrade && dax_is_zero_entry(entry))
			unmap_mapping_range(mapping,
				(index << PAGE_SHIFT) & PMD_MASK, PMD_SIZE, 0);

		err = radix_tree_preload(
				mapping_gfp_mask(mapping) & ~__GFP_HIGHMEM);
		if (err) {
			if (pmd_downgrade)
				put_locked_mapping_entry(mapping, index, entry);
			return ERR_PTR(err);
		}
		spin_lock_irq(&mapping->tree_lock);

		if (!entry) {
			/*
			 * We needed to drop the page_tree lock while calling
			 * radix_tree_preload() and we didn't have an entry to
			 * lock.  See if another thread inserted an entry at
			 * our index during this time.
			 */
			entry = __radix_tree_lookup(&mapping->page_tree, index,
					NULL, &slot);
			if (entry) {
				radix_tree_preload_end();
				spin_unlock_irq(&mapping->tree_lock);
				goto restart;
			}
		}

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
			 * Our insertion of a DAX entry failed, most likely
			 * because we were inserting a PMD entry and it
			 * collided with a PTE sized entry at a different
			 * index in the PMD range.  We haven't inserted
			 * anything into the radix tree and have no waiters to
			 * wake.
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

static int __dax_invalidate_mapping_entry(struct address_space *mapping,
					  pgoff_t index, bool trunc)
{
	int ret = 0;
	void *entry;
	struct radix_tree_root *page_tree = &mapping->page_tree;

	spin_lock_irq(&mapping->tree_lock);
	entry = get_unlocked_mapping_entry(mapping, index, NULL);
	if (!entry || !radix_tree_exceptional_entry(entry))
		goto out;
	if (!trunc &&
	    (radix_tree_tag_get(page_tree, index, PAGECACHE_TAG_DIRTY) ||
	     radix_tree_tag_get(page_tree, index, PAGECACHE_TAG_TOWRITE)))
		goto out;
	radix_tree_delete(page_tree, index);
	mapping->nrexceptional--;
	ret = 1;
out:
	put_unlocked_mapping_entry(mapping, index, entry);
	spin_unlock_irq(&mapping->tree_lock);
	return ret;
}
/*
 * Delete exceptional DAX entry at @index from @mapping. Wait for radix tree
 * entry to get unlocked before deleting it.
 */
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index)
{
	int ret = __dax_invalidate_mapping_entry(mapping, index, true);

	/*
	 * This gets called from truncate / punch_hole path. As such, the caller
	 * must hold locks protecting against concurrent modifications of the
	 * radix tree (usually fs-private i_mmap_sem for writing). Since the
	 * caller has seen exceptional entry for this index, we better find it
	 * at that index as well...
	 */
	WARN_ON_ONCE(!ret);
	return ret;
}

/*
 * Invalidate exceptional DAX entry if it is clean.
 */
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index)
{
	return __dax_invalidate_mapping_entry(mapping, index, false);
}

/*
 * The user has performed a load from a hole in the file.  Allocating
 * a new page in the file would cause excessive storage usage for
 * workloads with sparse files.  We allocate a page cache page instead.
 * We'll kick it out of the page cache if it's ever written to,
 * otherwise it will simply fall out of the page cache under memory
 * pressure without ever having been dirtied.
 */
static int dax_load_hole(struct address_space *mapping, void **entry,
			 struct vm_fault *vmf)
{
	struct inode *inode = mapping->host;
	struct page *page;
	int ret;

	/* Hole page already exists? Return it...  */
	if (!radix_tree_exceptional_entry(*entry)) {
		page = *entry;
		goto finish_fault;
	}

	/* This will replace locked radix tree entry with a hole page */
	page = find_or_create_page(mapping, vmf->pgoff,
				   vmf->gfp_mask | __GFP_ZERO);
	if (!page) {
		ret = VM_FAULT_OOM;
		goto out;
	}

finish_fault:
	vmf->page = page;
	ret = finish_fault(vmf);
	vmf->page = NULL;
	*entry = page;
	if (!ret) {
		/* Grab reference for PTE that is now referencing the page */
		get_page(page);
		ret = VM_FAULT_NOPAGE;
	}
out:
	trace_dax_load_hole(inode, vmf, ret);
	return ret;
}

static int copy_user_dax(struct block_device *bdev, struct dax_device *dax_dev,
		sector_t sector, size_t size, struct page *to,
		unsigned long vaddr)
{
	void *vto, *kaddr;
	pgoff_t pgoff;
	pfn_t pfn;
	long rc;
	int id;

	rc = bdev_dax_pgoff(bdev, sector, size, &pgoff);
	if (rc)
		return rc;

	id = dax_read_lock();
	rc = dax_direct_access(dax_dev, pgoff, PHYS_PFN(size), &kaddr, &pfn);
	if (rc < 0) {
		dax_read_unlock(id);
		return rc;
	}
	vto = kmap_atomic(to);
	copy_user_page(vto, (void __force *)kaddr, vaddr, to);
	kunmap_atomic(vto);
	dax_read_unlock(id);
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
		struct radix_tree_node *node;
		void **slot;
		void *ret;

		ret = __radix_tree_lookup(page_tree, index, &node, &slot);
		WARN_ON_ONCE(ret != entry);
		__radix_tree_replace(page_tree, node, slot,
				     new_entry, NULL, NULL);
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

static inline unsigned long
pgoff_address(pgoff_t pgoff, struct vm_area_struct *vma)
{
	unsigned long address;

	address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	VM_BUG_ON_VMA(address < vma->vm_start || address >= vma->vm_end, vma);
	return address;
}

/* Walk all mappings of a given index of a file and writeprotect them */
static void dax_mapping_entry_mkclean(struct address_space *mapping,
				      pgoff_t index, unsigned long pfn)
{
	struct vm_area_struct *vma;
	pte_t pte, *ptep = NULL;
	pmd_t *pmdp = NULL;
	spinlock_t *ptl;
	bool changed;

	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, index, index) {
		unsigned long address;

		cond_resched();

		if (!(vma->vm_flags & VM_SHARED))
			continue;

		address = pgoff_address(index, vma);
		changed = false;
		if (follow_pte_pmd(vma->vm_mm, address, &ptep, &pmdp, &ptl))
			continue;

		if (pmdp) {
#ifdef CONFIG_FS_DAX_PMD
			pmd_t pmd;

			if (pfn != pmd_pfn(*pmdp))
				goto unlock_pmd;
			if (!pmd_dirty(*pmdp) && !pmd_write(*pmdp))
				goto unlock_pmd;

			flush_cache_page(vma, address, pfn);
			pmd = pmdp_huge_clear_flush(vma, address, pmdp);
			pmd = pmd_wrprotect(pmd);
			pmd = pmd_mkclean(pmd);
			set_pmd_at(vma->vm_mm, address, pmdp, pmd);
			changed = true;
unlock_pmd:
			spin_unlock(ptl);
#endif
		} else {
			if (pfn != pte_pfn(*ptep))
				goto unlock_pte;
			if (!pte_dirty(*ptep) && !pte_write(*ptep))
				goto unlock_pte;

			flush_cache_page(vma, address, pfn);
			pte = ptep_clear_flush(vma, address, ptep);
			pte = pte_wrprotect(pte);
			pte = pte_mkclean(pte);
			set_pte_at(vma->vm_mm, address, ptep, pte);
			changed = true;
unlock_pte:
			pte_unmap_unlock(ptep, ptl);
		}

		if (changed)
			mmu_notifier_invalidate_page(vma->vm_mm, address);
	}
	i_mmap_unlock_read(mapping);
}

static int dax_writeback_one(struct block_device *bdev,
		struct dax_device *dax_dev, struct address_space *mapping,
		pgoff_t index, void *entry)
{
	struct radix_tree_root *page_tree = &mapping->page_tree;
	void *entry2, **slot, *kaddr;
	long ret = 0, id;
	sector_t sector;
	pgoff_t pgoff;
	size_t size;
	pfn_t pfn;

	/*
	 * A page got tagged dirty in DAX mapping? Something is seriously
	 * wrong.
	 */
	if (WARN_ON(!radix_tree_exceptional_entry(entry)))
		return -EIO;

	spin_lock_irq(&mapping->tree_lock);
	entry2 = get_unlocked_mapping_entry(mapping, index, &slot);
	/* Entry got punched out / reallocated? */
	if (!entry2 || !radix_tree_exceptional_entry(entry2))
		goto put_unlocked;
	/*
	 * Entry got reallocated elsewhere? No need to writeback. We have to
	 * compare sectors as we must not bail out due to difference in lockbit
	 * or entry type.
	 */
	if (dax_radix_sector(entry2) != dax_radix_sector(entry))
		goto put_unlocked;
	if (WARN_ON_ONCE(dax_is_empty_entry(entry) ||
				dax_is_zero_entry(entry))) {
		ret = -EIO;
		goto put_unlocked;
	}

	/* Another fsync thread may have already written back this entry */
	if (!radix_tree_tag_get(page_tree, index, PAGECACHE_TAG_TOWRITE))
		goto put_unlocked;
	/* Lock the entry to serialize with page faults */
	entry = lock_slot(mapping, slot);
	/*
	 * We can clear the tag now but we have to be careful so that concurrent
	 * dax_writeback_one() calls for the same index cannot finish before we
	 * actually flush the caches. This is achieved as the calls will look
	 * at the entry only under tree_lock and once they do that they will
	 * see the entry locked and wait for it to unlock.
	 */
	radix_tree_tag_clear(page_tree, index, PAGECACHE_TAG_TOWRITE);
	spin_unlock_irq(&mapping->tree_lock);

	/*
	 * Even if dax_writeback_mapping_range() was given a wbc->range_start
	 * in the middle of a PMD, the 'index' we are given will be aligned to
	 * the start index of the PMD, as will the sector we pull from
	 * 'entry'.  This allows us to flush for PMD_SIZE and not have to
	 * worry about partial PMD writebacks.
	 */
	sector = dax_radix_sector(entry);
	size = PAGE_SIZE << dax_radix_order(entry);

	id = dax_read_lock();
	ret = bdev_dax_pgoff(bdev, sector, size, &pgoff);
	if (ret)
		goto dax_unlock;

	/*
	 * dax_direct_access() may sleep, so cannot hold tree_lock over
	 * its invocation.
	 */
	ret = dax_direct_access(dax_dev, pgoff, size / PAGE_SIZE, &kaddr, &pfn);
	if (ret < 0)
		goto dax_unlock;

	if (WARN_ON_ONCE(ret < size / PAGE_SIZE)) {
		ret = -EIO;
		goto dax_unlock;
	}

	dax_mapping_entry_mkclean(mapping, index, pfn_t_to_pfn(pfn));
	wb_cache_pmem(kaddr, size);
	/*
	 * After we have flushed the cache, we can clear the dirty tag. There
	 * cannot be new dirty data in the pfn after the flush has completed as
	 * the pfn mappings are writeprotected and fault waits for mapping
	 * entry lock.
	 */
	spin_lock_irq(&mapping->tree_lock);
	radix_tree_tag_clear(page_tree, index, PAGECACHE_TAG_DIRTY);
	spin_unlock_irq(&mapping->tree_lock);
	trace_dax_writeback_one(mapping->host, index, size >> PAGE_SHIFT);
 dax_unlock:
	dax_read_unlock(id);
	put_locked_mapping_entry(mapping, index, entry);
	return ret;

 put_unlocked:
	put_unlocked_mapping_entry(mapping, index, entry2);
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
	struct dax_device *dax_dev;
	struct pagevec pvec;
	bool done = false;
	int i, ret = 0;

	if (WARN_ON_ONCE(inode->i_blkbits != PAGE_SHIFT))
		return -EIO;

	if (!mapping->nrexceptional || wbc->sync_mode != WB_SYNC_ALL)
		return 0;

	dax_dev = dax_get_by_host(bdev->bd_disk->disk_name);
	if (!dax_dev)
		return -EIO;

	start_index = wbc->range_start >> PAGE_SHIFT;
	end_index = wbc->range_end >> PAGE_SHIFT;

	trace_dax_writeback_range(inode, start_index, end_index);

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

			ret = dax_writeback_one(bdev, dax_dev, mapping,
					indices[i], pvec.pages[i]);
			if (ret < 0)
				goto out;
		}
		start_index = indices[pvec.nr - 1] + 1;
	}
out:
	put_dax(dax_dev);
	trace_dax_writeback_range_done(inode, start_index, end_index);
	return (ret < 0 ? ret : 0);
}
EXPORT_SYMBOL_GPL(dax_writeback_mapping_range);

static int dax_insert_mapping(struct address_space *mapping,
		struct block_device *bdev, struct dax_device *dax_dev,
		sector_t sector, size_t size, void **entryp,
		struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long vaddr = vmf->address;
	void *entry = *entryp;
	void *ret, *kaddr;
	pgoff_t pgoff;
	int id, rc;
	pfn_t pfn;

	rc = bdev_dax_pgoff(bdev, sector, size, &pgoff);
	if (rc)
		return rc;

	id = dax_read_lock();
	rc = dax_direct_access(dax_dev, pgoff, PHYS_PFN(size), &kaddr, &pfn);
	if (rc < 0) {
		dax_read_unlock(id);
		return rc;
	}
	dax_read_unlock(id);

	ret = dax_insert_mapping_entry(mapping, vmf, entry, sector, 0);
	if (IS_ERR(ret))
		return PTR_ERR(ret);
	*entryp = ret;

	trace_dax_insert_mapping(mapping->host, vmf, ret);
	return vm_insert_mixed(vma, vaddr, pfn);
}

/**
 * dax_pfn_mkwrite - handle first write to DAX page
 * @vmf: The description of the fault
 */
int dax_pfn_mkwrite(struct vm_fault *vmf)
{
	struct file *file = vmf->vma->vm_file;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	void *entry, **slot;
	pgoff_t index = vmf->pgoff;

	spin_lock_irq(&mapping->tree_lock);
	entry = get_unlocked_mapping_entry(mapping, index, &slot);
	if (!entry || !radix_tree_exceptional_entry(entry)) {
		if (entry)
			put_unlocked_mapping_entry(mapping, index, entry);
		spin_unlock_irq(&mapping->tree_lock);
		trace_dax_pfn_mkwrite_no_entry(inode, vmf, VM_FAULT_NOPAGE);
		return VM_FAULT_NOPAGE;
	}
	radix_tree_tag_set(&mapping->page_tree, index, PAGECACHE_TAG_DIRTY);
	entry = lock_slot(mapping, slot);
	spin_unlock_irq(&mapping->tree_lock);
	/*
	 * If we race with somebody updating the PTE and finish_mkwrite_fault()
	 * fails, we don't care. We need to return VM_FAULT_NOPAGE and retry
	 * the fault in either case.
	 */
	finish_mkwrite_fault(vmf);
	put_locked_mapping_entry(mapping, index, entry);
	trace_dax_pfn_mkwrite(inode, vmf, VM_FAULT_NOPAGE);
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

int __dax_zero_page_range(struct block_device *bdev,
		struct dax_device *dax_dev, sector_t sector,
		unsigned int offset, unsigned int size)
{
	if (dax_range_is_aligned(bdev, offset, size)) {
		sector_t start_sector = sector + (offset >> 9);

		return blkdev_issue_zeroout(bdev, start_sector,
				size >> 9, GFP_NOFS, 0);
	} else {
		pgoff_t pgoff;
		long rc, id;
		void *kaddr;
		pfn_t pfn;

		rc = bdev_dax_pgoff(bdev, sector, PAGE_SIZE, &pgoff);
		if (rc)
			return rc;

		id = dax_read_lock();
		rc = dax_direct_access(dax_dev, pgoff, 1, &kaddr,
				&pfn);
		if (rc < 0) {
			dax_read_unlock(id);
			return rc;
		}
		clear_pmem(kaddr + offset, size);
		dax_read_unlock(id);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__dax_zero_page_range);

static sector_t dax_iomap_sector(struct iomap *iomap, loff_t pos)
{
	return iomap->blkno + (((pos & PAGE_MASK) - iomap->offset) >> 9);
}

static loff_t
dax_iomap_actor(struct inode *inode, loff_t pos, loff_t length, void *data,
		struct iomap *iomap)
{
	struct block_device *bdev = iomap->bdev;
	struct dax_device *dax_dev = iomap->dax_dev;
	struct iov_iter *iter = data;
	loff_t end = pos + length, done = 0;
	ssize_t ret = 0;
	int id;

	if (iov_iter_rw(iter) == READ) {
		end = min(end, i_size_read(inode));
		if (pos >= end)
			return 0;

		if (iomap->type == IOMAP_HOLE || iomap->type == IOMAP_UNWRITTEN)
			return iov_iter_zero(min(length, end - pos), iter);
	}

	if (WARN_ON_ONCE(iomap->type != IOMAP_MAPPED))
		return -EIO;

	/*
	 * Write can allocate block for an area which has a hole page mapped
	 * into page tables. We have to tear down these mappings so that data
	 * written by write(2) is visible in mmap.
	 */
	if (iomap->flags & IOMAP_F_NEW) {
		invalidate_inode_pages2_range(inode->i_mapping,
					      pos >> PAGE_SHIFT,
					      (end - 1) >> PAGE_SHIFT);
	}

	id = dax_read_lock();
	while (pos < end) {
		unsigned offset = pos & (PAGE_SIZE - 1);
		const size_t size = ALIGN(length + offset, PAGE_SIZE);
		const sector_t sector = dax_iomap_sector(iomap, pos);
		ssize_t map_len;
		pgoff_t pgoff;
		void *kaddr;
		pfn_t pfn;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		ret = bdev_dax_pgoff(bdev, sector, size, &pgoff);
		if (ret)
			break;

		map_len = dax_direct_access(dax_dev, pgoff, PHYS_PFN(size),
				&kaddr, &pfn);
		if (map_len < 0) {
			ret = map_len;
			break;
		}

		map_len = PFN_PHYS(map_len);
		kaddr += offset;
		map_len -= offset;
		if (map_len > end - pos)
			map_len = end - pos;

		if (iov_iter_rw(iter) == WRITE)
			map_len = copy_from_iter_pmem(kaddr, map_len, iter);
		else
			map_len = copy_to_iter(kaddr, map_len, iter);
		if (map_len <= 0) {
			ret = map_len ? map_len : -EFAULT;
			break;
		}

		pos += map_len;
		length -= map_len;
		done += map_len;
	}
	dax_read_unlock(id);

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
		const struct iomap_ops *ops)
{
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	struct inode *inode = mapping->host;
	loff_t pos = iocb->ki_pos, ret = 0, done = 0;
	unsigned flags = 0;

	if (iov_iter_rw(iter) == WRITE) {
		lockdep_assert_held_exclusive(&inode->i_rwsem);
		flags |= IOMAP_WRITE;
	} else {
		lockdep_assert_held(&inode->i_rwsem);
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

static int dax_fault_return(int error)
{
	if (error == 0)
		return VM_FAULT_NOPAGE;
	if (error == -ENOMEM)
		return VM_FAULT_OOM;
	return VM_FAULT_SIGBUS;
}

static int dax_iomap_pte_fault(struct vm_fault *vmf,
			       const struct iomap_ops *ops)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	struct inode *inode = mapping->host;
	unsigned long vaddr = vmf->address;
	loff_t pos = (loff_t)vmf->pgoff << PAGE_SHIFT;
	sector_t sector;
	struct iomap iomap = { 0 };
	unsigned flags = IOMAP_FAULT;
	int error, major = 0;
	int vmf_ret = 0;
	void *entry;

	trace_dax_pte_fault(inode, vmf, vmf_ret);
	/*
	 * Check whether offset isn't beyond end of file now. Caller is supposed
	 * to hold locks serializing us with truncate / punch hole so this is
	 * a reliable test.
	 */
	if (pos >= i_size_read(inode)) {
		vmf_ret = VM_FAULT_SIGBUS;
		goto out;
	}

	if ((vmf->flags & FAULT_FLAG_WRITE) && !vmf->cow_page)
		flags |= IOMAP_WRITE;

	entry = grab_mapping_entry(mapping, vmf->pgoff, 0);
	if (IS_ERR(entry)) {
		vmf_ret = dax_fault_return(PTR_ERR(entry));
		goto out;
	}

	/*
	 * It is possible, particularly with mixed reads & writes to private
	 * mappings, that we have raced with a PMD fault that overlaps with
	 * the PTE we need to set up.  If so just return and the fault will be
	 * retried.
	 */
	if (pmd_trans_huge(*vmf->pmd) || pmd_devmap(*vmf->pmd)) {
		vmf_ret = VM_FAULT_NOPAGE;
		goto unlock_entry;
	}

	/*
	 * Note that we don't bother to use iomap_apply here: DAX required
	 * the file system block size to be equal the page size, which means
	 * that we never have to deal with more than a single extent here.
	 */
	error = ops->iomap_begin(inode, pos, PAGE_SIZE, flags, &iomap);
	if (error) {
		vmf_ret = dax_fault_return(error);
		goto unlock_entry;
	}
	if (WARN_ON_ONCE(iomap.offset + iomap.length < pos + PAGE_SIZE)) {
		error = -EIO;	/* fs corruption? */
		goto error_finish_iomap;
	}

	sector = dax_iomap_sector(&iomap, pos);

	if (vmf->cow_page) {
		switch (iomap.type) {
		case IOMAP_HOLE:
		case IOMAP_UNWRITTEN:
			clear_user_highpage(vmf->cow_page, vaddr);
			break;
		case IOMAP_MAPPED:
			error = copy_user_dax(iomap.bdev, iomap.dax_dev,
					sector, PAGE_SIZE, vmf->cow_page, vaddr);
			break;
		default:
			WARN_ON_ONCE(1);
			error = -EIO;
			break;
		}

		if (error)
			goto error_finish_iomap;

		__SetPageUptodate(vmf->cow_page);
		vmf_ret = finish_fault(vmf);
		if (!vmf_ret)
			vmf_ret = VM_FAULT_DONE_COW;
		goto finish_iomap;
	}

	switch (iomap.type) {
	case IOMAP_MAPPED:
		if (iomap.flags & IOMAP_F_NEW) {
			count_vm_event(PGMAJFAULT);
			count_memcg_event_mm(vmf->vma->vm_mm, PGMAJFAULT);
			major = VM_FAULT_MAJOR;
		}
		error = dax_insert_mapping(mapping, iomap.bdev, iomap.dax_dev,
				sector, PAGE_SIZE, &entry, vmf->vma, vmf);
		/* -EBUSY is fine, somebody else faulted on the same PTE */
		if (error == -EBUSY)
			error = 0;
		break;
	case IOMAP_UNWRITTEN:
	case IOMAP_HOLE:
		if (!(vmf->flags & FAULT_FLAG_WRITE)) {
			vmf_ret = dax_load_hole(mapping, &entry, vmf);
			goto finish_iomap;
		}
		/*FALLTHRU*/
	default:
		WARN_ON_ONCE(1);
		error = -EIO;
		break;
	}

 error_finish_iomap:
	vmf_ret = dax_fault_return(error) | major;
 finish_iomap:
	if (ops->iomap_end) {
		int copied = PAGE_SIZE;

		if (vmf_ret & VM_FAULT_ERROR)
			copied = 0;
		/*
		 * The fault is done by now and there's no way back (other
		 * thread may be already happily using PTE we have installed).
		 * Just ignore error from ->iomap_end since we cannot do much
		 * with it.
		 */
		ops->iomap_end(inode, pos, PAGE_SIZE, copied, flags, &iomap);
	}
 unlock_entry:
	put_locked_mapping_entry(mapping, vmf->pgoff, entry);
 out:
	trace_dax_pte_fault_done(inode, vmf, vmf_ret);
	return vmf_ret;
}

#ifdef CONFIG_FS_DAX_PMD
/*
 * The 'colour' (ie low bits) within a PMD of a page offset.  This comes up
 * more often than one might expect in the below functions.
 */
#define PG_PMD_COLOUR	((PMD_SIZE >> PAGE_SHIFT) - 1)

static int dax_pmd_insert_mapping(struct vm_fault *vmf, struct iomap *iomap,
		loff_t pos, void **entryp)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	const sector_t sector = dax_iomap_sector(iomap, pos);
	struct dax_device *dax_dev = iomap->dax_dev;
	struct block_device *bdev = iomap->bdev;
	struct inode *inode = mapping->host;
	const size_t size = PMD_SIZE;
	void *ret = NULL, *kaddr;
	long length = 0;
	pgoff_t pgoff;
	pfn_t pfn;
	int id;

	if (bdev_dax_pgoff(bdev, sector, size, &pgoff) != 0)
		goto fallback;

	id = dax_read_lock();
	length = dax_direct_access(dax_dev, pgoff, PHYS_PFN(size), &kaddr, &pfn);
	if (length < 0)
		goto unlock_fallback;
	length = PFN_PHYS(length);

	if (length < size)
		goto unlock_fallback;
	if (pfn_t_to_pfn(pfn) & PG_PMD_COLOUR)
		goto unlock_fallback;
	if (!pfn_t_devmap(pfn))
		goto unlock_fallback;
	dax_read_unlock(id);

	ret = dax_insert_mapping_entry(mapping, vmf, *entryp, sector,
			RADIX_DAX_PMD);
	if (IS_ERR(ret))
		goto fallback;
	*entryp = ret;

	trace_dax_pmd_insert_mapping(inode, vmf, length, pfn, ret);
	return vmf_insert_pfn_pmd(vmf->vma, vmf->address, vmf->pmd,
			pfn, vmf->flags & FAULT_FLAG_WRITE);

unlock_fallback:
	dax_read_unlock(id);
fallback:
	trace_dax_pmd_insert_mapping_fallback(inode, vmf, length, pfn, ret);
	return VM_FAULT_FALLBACK;
}

static int dax_pmd_load_hole(struct vm_fault *vmf, struct iomap *iomap,
		void **entryp)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	struct inode *inode = mapping->host;
	struct page *zero_page;
	void *ret = NULL;
	spinlock_t *ptl;
	pmd_t pmd_entry;

	zero_page = mm_get_huge_zero_page(vmf->vma->vm_mm);

	if (unlikely(!zero_page))
		goto fallback;

	ret = dax_insert_mapping_entry(mapping, vmf, *entryp, 0,
			RADIX_DAX_PMD | RADIX_DAX_HZP);
	if (IS_ERR(ret))
		goto fallback;
	*entryp = ret;

	ptl = pmd_lock(vmf->vma->vm_mm, vmf->pmd);
	if (!pmd_none(*(vmf->pmd))) {
		spin_unlock(ptl);
		goto fallback;
	}

	pmd_entry = mk_pmd(zero_page, vmf->vma->vm_page_prot);
	pmd_entry = pmd_mkhuge(pmd_entry);
	set_pmd_at(vmf->vma->vm_mm, pmd_addr, vmf->pmd, pmd_entry);
	spin_unlock(ptl);
	trace_dax_pmd_load_hole(inode, vmf, zero_page, ret);
	return VM_FAULT_NOPAGE;

fallback:
	trace_dax_pmd_load_hole_fallback(inode, vmf, zero_page, ret);
	return VM_FAULT_FALLBACK;
}

static int dax_iomap_pmd_fault(struct vm_fault *vmf,
			       const struct iomap_ops *ops)
{
	struct vm_area_struct *vma = vmf->vma;
	struct address_space *mapping = vma->vm_file->f_mapping;
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	unsigned int iomap_flags = (write ? IOMAP_WRITE : 0) | IOMAP_FAULT;
	struct inode *inode = mapping->host;
	int result = VM_FAULT_FALLBACK;
	struct iomap iomap = { 0 };
	pgoff_t max_pgoff, pgoff;
	void *entry;
	loff_t pos;
	int error;

	/*
	 * Check whether offset isn't beyond end of file now. Caller is
	 * supposed to hold locks serializing us with truncate / punch hole so
	 * this is a reliable test.
	 */
	pgoff = linear_page_index(vma, pmd_addr);
	max_pgoff = (i_size_read(inode) - 1) >> PAGE_SHIFT;

	trace_dax_pmd_fault(inode, vmf, max_pgoff, 0);

	/* Fall back to PTEs if we're going to COW */
	if (write && !(vma->vm_flags & VM_SHARED))
		goto fallback;

	/* If the PMD would extend outside the VMA */
	if (pmd_addr < vma->vm_start)
		goto fallback;
	if ((pmd_addr + PMD_SIZE) > vma->vm_end)
		goto fallback;

	if (pgoff > max_pgoff) {
		result = VM_FAULT_SIGBUS;
		goto out;
	}

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
	 * It is possible, particularly with mixed reads & writes to private
	 * mappings, that we have raced with a PTE fault that overlaps with
	 * the PMD we need to set up.  If so just return and the fault will be
	 * retried.
	 */
	if (!pmd_none(*vmf->pmd) && !pmd_trans_huge(*vmf->pmd) &&
			!pmd_devmap(*vmf->pmd)) {
		result = 0;
		goto unlock_entry;
	}

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

	switch (iomap.type) {
	case IOMAP_MAPPED:
		result = dax_pmd_insert_mapping(vmf, &iomap, pos, &entry);
		break;
	case IOMAP_UNWRITTEN:
	case IOMAP_HOLE:
		if (WARN_ON_ONCE(write))
			break;
		result = dax_pmd_load_hole(vmf, &iomap, &entry);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

 finish_iomap:
	if (ops->iomap_end) {
		int copied = PMD_SIZE;

		if (result == VM_FAULT_FALLBACK)
			copied = 0;
		/*
		 * The fault is done by now and there's no way back (other
		 * thread may be already happily using PMD we have installed).
		 * Just ignore error from ->iomap_end since we cannot do much
		 * with it.
		 */
		ops->iomap_end(inode, pos, PMD_SIZE, copied, iomap_flags,
				&iomap);
	}
 unlock_entry:
	put_locked_mapping_entry(mapping, pgoff, entry);
 fallback:
	if (result == VM_FAULT_FALLBACK) {
		split_huge_pmd(vma, vmf->pmd, vmf->address);
		count_vm_event(THP_FAULT_FALLBACK);
	}
out:
	trace_dax_pmd_fault_done(inode, vmf, max_pgoff, result);
	return result;
}
#else
static int dax_iomap_pmd_fault(struct vm_fault *vmf,
			       const struct iomap_ops *ops)
{
	return VM_FAULT_FALLBACK;
}
#endif /* CONFIG_FS_DAX_PMD */

/**
 * dax_iomap_fault - handle a page fault on a DAX file
 * @vmf: The description of the fault
 * @ops: iomap ops passed from the file system
 *
 * When a page fault occurs, filesystems may call this helper in
 * their fault handler for DAX files. dax_iomap_fault() assumes the caller
 * has done all the necessary locking for page fault to proceed
 * successfully.
 */
int dax_iomap_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
		    const struct iomap_ops *ops)
{
	switch (pe_size) {
	case PE_SIZE_PTE:
		return dax_iomap_pte_fault(vmf, ops);
	case PE_SIZE_PMD:
		return dax_iomap_pmd_fault(vmf, ops);
	default:
		return VM_FAULT_FALLBACK;
	}
}
EXPORT_SYMBOL_GPL(dax_iomap_fault);
