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

static inline unsigned int pe_order(enum page_entry_size pe_size)
{
	if (pe_size == PE_SIZE_PTE)
		return PAGE_SHIFT - PAGE_SHIFT;
	if (pe_size == PE_SIZE_PMD)
		return PMD_SHIFT - PAGE_SHIFT;
	if (pe_size == PE_SIZE_PUD)
		return PUD_SHIFT - PAGE_SHIFT;
	return ~0;
}

/* We choose 4096 entries - same as per-zone page wait tables */
#define DAX_WAIT_TABLE_BITS 12
#define DAX_WAIT_TABLE_ENTRIES (1 << DAX_WAIT_TABLE_BITS)

/* The 'colour' (ie low bits) within a PMD of a page offset.  */
#define PG_PMD_COLOUR	((PMD_SIZE >> PAGE_SHIFT) - 1)
#define PG_PMD_NR	(PMD_SIZE >> PAGE_SHIFT)

/* The order of a PMD entry */
#define PMD_ORDER	(PMD_SHIFT - PAGE_SHIFT)

static wait_queue_head_t wait_table[DAX_WAIT_TABLE_ENTRIES];

static int __init init_dax_wait_table(void)
{
	int i;

	for (i = 0; i < DAX_WAIT_TABLE_ENTRIES; i++)
		init_waitqueue_head(wait_table + i);
	return 0;
}
fs_initcall(init_dax_wait_table);

/*
 * DAX pagecache entries use XArray value entries so they can't be mistaken
 * for pages.  We use one bit for locking, one bit for the entry size (PMD)
 * and two more to tell us if the entry is a zero page or an empty entry that
 * is just used for locking.  In total four special bits.
 *
 * If the PMD bit isn't set the entry has size PAGE_SIZE, and if the ZERO_PAGE
 * and EMPTY bits aren't set the entry is a normal DAX entry with a filesystem
 * block allocation.
 */
#define DAX_SHIFT	(4)
#define DAX_LOCKED	(1UL << 0)
#define DAX_PMD		(1UL << 1)
#define DAX_ZERO_PAGE	(1UL << 2)
#define DAX_EMPTY	(1UL << 3)

static unsigned long dax_to_pfn(void *entry)
{
	return xa_to_value(entry) >> DAX_SHIFT;
}

static void *dax_make_entry(pfn_t pfn, unsigned long flags)
{
	return xa_mk_value(flags | (pfn_t_to_pfn(pfn) << DAX_SHIFT));
}

static bool dax_is_locked(void *entry)
{
	return xa_to_value(entry) & DAX_LOCKED;
}

static unsigned int dax_entry_order(void *entry)
{
	if (xa_to_value(entry) & DAX_PMD)
		return PMD_ORDER;
	return 0;
}

static unsigned long dax_is_pmd_entry(void *entry)
{
	return xa_to_value(entry) & DAX_PMD;
}

static bool dax_is_pte_entry(void *entry)
{
	return !(xa_to_value(entry) & DAX_PMD);
}

static int dax_is_zero_entry(void *entry)
{
	return xa_to_value(entry) & DAX_ZERO_PAGE;
}

static int dax_is_empty_entry(void *entry)
{
	return xa_to_value(entry) & DAX_EMPTY;
}

/*
 * DAX page cache entry locking
 */
struct exceptional_entry_key {
	struct xarray *xa;
	pgoff_t entry_start;
};

struct wait_exceptional_entry_queue {
	wait_queue_entry_t wait;
	struct exceptional_entry_key key;
};

static wait_queue_head_t *dax_entry_waitqueue(struct xa_state *xas,
		void *entry, struct exceptional_entry_key *key)
{
	unsigned long hash;
	unsigned long index = xas->xa_index;

	/*
	 * If 'entry' is a PMD, align the 'index' that we use for the wait
	 * queue to the start of that PMD.  This ensures that all offsets in
	 * the range covered by the PMD map to the same bit lock.
	 */
	if (dax_is_pmd_entry(entry))
		index &= ~PG_PMD_COLOUR;
	key->xa = xas->xa;
	key->entry_start = index;

	hash = hash_long((unsigned long)xas->xa ^ index, DAX_WAIT_TABLE_BITS);
	return wait_table + hash;
}

static int wake_exceptional_entry_func(wait_queue_entry_t *wait,
		unsigned int mode, int sync, void *keyp)
{
	struct exceptional_entry_key *key = keyp;
	struct wait_exceptional_entry_queue *ewait =
		container_of(wait, struct wait_exceptional_entry_queue, wait);

	if (key->xa != ewait->key.xa ||
	    key->entry_start != ewait->key.entry_start)
		return 0;
	return autoremove_wake_function(wait, mode, sync, NULL);
}

/*
 * @entry may no longer be the entry at the index in the mapping.
 * The important information it's conveying is whether the entry at
 * this index used to be a PMD entry.
 */
static void dax_wake_entry(struct xa_state *xas, void *entry, bool wake_all)
{
	struct exceptional_entry_key key;
	wait_queue_head_t *wq;

	wq = dax_entry_waitqueue(xas, entry, &key);

	/*
	 * Checking for locked entry and prepare_to_wait_exclusive() happens
	 * under the i_pages lock, ditto for entry handling in our callers.
	 * So at this point all tasks that could have seen our entry locked
	 * must be in the waitqueue and the following check will see them.
	 */
	if (waitqueue_active(wq))
		__wake_up(wq, TASK_NORMAL, wake_all ? 0 : 1, &key);
}

/*
 * Look up entry in page cache, wait for it to become unlocked if it
 * is a DAX entry and return it.  The caller must subsequently call
 * put_unlocked_entry() if it did not lock the entry or dax_unlock_entry()
 * if it did.
 *
 * Must be called with the i_pages lock held.
 */
static void *get_unlocked_entry(struct xa_state *xas)
{
	void *entry;
	struct wait_exceptional_entry_queue ewait;
	wait_queue_head_t *wq;

	init_wait(&ewait.wait);
	ewait.wait.func = wake_exceptional_entry_func;

	for (;;) {
		entry = xas_find_conflict(xas);
		if (!entry || WARN_ON_ONCE(!xa_is_value(entry)) ||
				!dax_is_locked(entry))
			return entry;

		wq = dax_entry_waitqueue(xas, entry, &ewait.key);
		prepare_to_wait_exclusive(wq, &ewait.wait,
					  TASK_UNINTERRUPTIBLE);
		xas_unlock_irq(xas);
		xas_reset(xas);
		schedule();
		finish_wait(wq, &ewait.wait);
		xas_lock_irq(xas);
	}
}

static void put_unlocked_entry(struct xa_state *xas, void *entry)
{
	/* If we were the only waiter woken, wake the next one */
	if (entry)
		dax_wake_entry(xas, entry, false);
}

/*
 * We used the xa_state to get the entry, but then we locked the entry and
 * dropped the xa_lock, so we know the xa_state is stale and must be reset
 * before use.
 */
static void dax_unlock_entry(struct xa_state *xas, void *entry)
{
	void *old;

	BUG_ON(dax_is_locked(entry));
	xas_reset(xas);
	xas_lock_irq(xas);
	old = xas_store(xas, entry);
	xas_unlock_irq(xas);
	BUG_ON(!dax_is_locked(old));
	dax_wake_entry(xas, entry, false);
}

/*
 * Return: The entry stored at this location before it was locked.
 */
static void *dax_lock_entry(struct xa_state *xas, void *entry)
{
	unsigned long v = xa_to_value(entry);
	return xas_store(xas, xa_mk_value(v | DAX_LOCKED));
}

static unsigned long dax_entry_size(void *entry)
{
	if (dax_is_zero_entry(entry))
		return 0;
	else if (dax_is_empty_entry(entry))
		return 0;
	else if (dax_is_pmd_entry(entry))
		return PMD_SIZE;
	else
		return PAGE_SIZE;
}

static unsigned long dax_end_pfn(void *entry)
{
	return dax_to_pfn(entry) + dax_entry_size(entry) / PAGE_SIZE;
}

/*
 * Iterate through all mapped pfns represented by an entry, i.e. skip
 * 'empty' and 'zero' entries.
 */
#define for_each_mapped_pfn(entry, pfn) \
	for (pfn = dax_to_pfn(entry); \
			pfn < dax_end_pfn(entry); pfn++)

/*
 * TODO: for reflink+dax we need a way to associate a single page with
 * multiple address_space instances at different linear_page_index()
 * offsets.
 */
static void dax_associate_entry(void *entry, struct address_space *mapping,
		struct vm_area_struct *vma, unsigned long address)
{
	unsigned long size = dax_entry_size(entry), pfn, index;
	int i = 0;

	if (IS_ENABLED(CONFIG_FS_DAX_LIMITED))
		return;

	index = linear_page_index(vma, address & ~(size - 1));
	for_each_mapped_pfn(entry, pfn) {
		struct page *page = pfn_to_page(pfn);

		WARN_ON_ONCE(page->mapping);
		page->mapping = mapping;
		page->index = index + i++;
	}
}

static void dax_disassociate_entry(void *entry, struct address_space *mapping,
		bool trunc)
{
	unsigned long pfn;

	if (IS_ENABLED(CONFIG_FS_DAX_LIMITED))
		return;

	for_each_mapped_pfn(entry, pfn) {
		struct page *page = pfn_to_page(pfn);

		WARN_ON_ONCE(trunc && page_ref_count(page) > 1);
		WARN_ON_ONCE(page->mapping && page->mapping != mapping);
		page->mapping = NULL;
		page->index = 0;
	}
}

static struct page *dax_busy_page(void *entry)
{
	unsigned long pfn;

	for_each_mapped_pfn(entry, pfn) {
		struct page *page = pfn_to_page(pfn);

		if (page_ref_count(page) > 1)
			return page;
	}
	return NULL;
}

/*
 * dax_lock_mapping_entry - Lock the DAX entry corresponding to a page
 * @page: The page whose entry we want to lock
 *
 * Context: Process context.
 * Return: %true if the entry was locked or does not need to be locked.
 */
bool dax_lock_mapping_entry(struct page *page)
{
	XA_STATE(xas, NULL, 0);
	void *entry;
	bool locked;

	/* Ensure page->mapping isn't freed while we look at it */
	rcu_read_lock();
	for (;;) {
		struct address_space *mapping = READ_ONCE(page->mapping);

		locked = false;
		if (!dax_mapping(mapping))
			break;

		/*
		 * In the device-dax case there's no need to lock, a
		 * struct dev_pagemap pin is sufficient to keep the
		 * inode alive, and we assume we have dev_pagemap pin
		 * otherwise we would not have a valid pfn_to_page()
		 * translation.
		 */
		locked = true;
		if (S_ISCHR(mapping->host->i_mode))
			break;

		xas.xa = &mapping->i_pages;
		xas_lock_irq(&xas);
		if (mapping != page->mapping) {
			xas_unlock_irq(&xas);
			continue;
		}
		xas_set(&xas, page->index);
		entry = xas_load(&xas);
		if (dax_is_locked(entry)) {
			rcu_read_unlock();
			entry = get_unlocked_entry(&xas);
			xas_unlock_irq(&xas);
			put_unlocked_entry(&xas, entry);
			rcu_read_lock();
			continue;
		}
		dax_lock_entry(&xas, entry);
		xas_unlock_irq(&xas);
		break;
	}
	rcu_read_unlock();
	return locked;
}

void dax_unlock_mapping_entry(struct page *page)
{
	struct address_space *mapping = page->mapping;
	XA_STATE(xas, &mapping->i_pages, page->index);
	void *entry;

	if (S_ISCHR(mapping->host->i_mode))
		return;

	rcu_read_lock();
	entry = xas_load(&xas);
	rcu_read_unlock();
	entry = dax_make_entry(page_to_pfn_t(page), dax_is_pmd_entry(entry));
	dax_unlock_entry(&xas, entry);
}

/*
 * Find page cache entry at given index. If it is a DAX entry, return it
 * with the entry locked. If the page cache doesn't contain an entry at
 * that index, add a locked empty entry.
 *
 * When requesting an entry with size DAX_PMD, grab_mapping_entry() will
 * either return that locked entry or will return VM_FAULT_FALLBACK.
 * This will happen if there are any PTE entries within the PMD range
 * that we are requesting.
 *
 * We always favor PTE entries over PMD entries. There isn't a flow where we
 * evict PTE entries in order to 'upgrade' them to a PMD entry.  A PMD
 * insertion will fail if it finds any PTE entries already in the tree, and a
 * PTE insertion will cause an existing PMD entry to be unmapped and
 * downgraded to PTE entries.  This happens for both PMD zero pages as
 * well as PMD empty entries.
 *
 * The exception to this downgrade path is for PMD entries that have
 * real storage backing them.  We will leave these real PMD entries in
 * the tree, and PTE writes will simply dirty the entire PMD entry.
 *
 * Note: Unlike filemap_fault() we don't honor FAULT_FLAG_RETRY flags. For
 * persistent memory the benefit is doubtful. We can add that later if we can
 * show it helps.
 *
 * On error, this function does not return an ERR_PTR.  Instead it returns
 * a VM_FAULT code, encoded as an xarray internal entry.  The ERR_PTR values
 * overlap with xarray value entries.
 */
static void *grab_mapping_entry(struct xa_state *xas,
		struct address_space *mapping, unsigned long size_flag)
{
	unsigned long index = xas->xa_index;
	bool pmd_downgrade = false; /* splitting PMD entry into PTE entries? */
	void *entry;

retry:
	xas_lock_irq(xas);
	entry = get_unlocked_entry(xas);

	if (entry) {
		if (!xa_is_value(entry)) {
			xas_set_err(xas, EIO);
			goto out_unlock;
		}

		if (size_flag & DAX_PMD) {
			if (dax_is_pte_entry(entry)) {
				put_unlocked_entry(xas, entry);
				goto fallback;
			}
		} else { /* trying to grab a PTE entry */
			if (dax_is_pmd_entry(entry) &&
			    (dax_is_zero_entry(entry) ||
			     dax_is_empty_entry(entry))) {
				pmd_downgrade = true;
			}
		}
	}

	if (pmd_downgrade) {
		/*
		 * Make sure 'entry' remains valid while we drop
		 * the i_pages lock.
		 */
		dax_lock_entry(xas, entry);

		/*
		 * Besides huge zero pages the only other thing that gets
		 * downgraded are empty entries which don't need to be
		 * unmapped.
		 */
		if (dax_is_zero_entry(entry)) {
			xas_unlock_irq(xas);
			unmap_mapping_pages(mapping,
					xas->xa_index & ~PG_PMD_COLOUR,
					PG_PMD_NR, false);
			xas_reset(xas);
			xas_lock_irq(xas);
		}

		dax_disassociate_entry(entry, mapping, false);
		xas_store(xas, NULL);	/* undo the PMD join */
		dax_wake_entry(xas, entry, true);
		mapping->nrexceptional--;
		entry = NULL;
		xas_set(xas, index);
	}

	if (entry) {
		dax_lock_entry(xas, entry);
	} else {
		entry = dax_make_entry(pfn_to_pfn_t(0), size_flag | DAX_EMPTY);
		dax_lock_entry(xas, entry);
		if (xas_error(xas))
			goto out_unlock;
		mapping->nrexceptional++;
	}

out_unlock:
	xas_unlock_irq(xas);
	if (xas_nomem(xas, mapping_gfp_mask(mapping) & ~__GFP_HIGHMEM))
		goto retry;
	if (xas->xa_node == XA_ERROR(-ENOMEM))
		return xa_mk_internal(VM_FAULT_OOM);
	if (xas_error(xas))
		return xa_mk_internal(VM_FAULT_SIGBUS);
	return entry;
fallback:
	xas_unlock_irq(xas);
	return xa_mk_internal(VM_FAULT_FALLBACK);
}

/**
 * dax_layout_busy_page - find first pinned page in @mapping
 * @mapping: address space to scan for a page with ref count > 1
 *
 * DAX requires ZONE_DEVICE mapped pages. These pages are never
 * 'onlined' to the page allocator so they are considered idle when
 * page->count == 1. A filesystem uses this interface to determine if
 * any page in the mapping is busy, i.e. for DMA, or other
 * get_user_pages() usages.
 *
 * It is expected that the filesystem is holding locks to block the
 * establishment of new mappings in this address_space. I.e. it expects
 * to be able to run unmap_mapping_range() and subsequently not race
 * mapping_mapped() becoming true.
 */
struct page *dax_layout_busy_page(struct address_space *mapping)
{
	XA_STATE(xas, &mapping->i_pages, 0);
	void *entry;
	unsigned int scanned = 0;
	struct page *page = NULL;

	/*
	 * In the 'limited' case get_user_pages() for dax is disabled.
	 */
	if (IS_ENABLED(CONFIG_FS_DAX_LIMITED))
		return NULL;

	if (!dax_mapping(mapping) || !mapping_mapped(mapping))
		return NULL;

	/*
	 * If we race get_user_pages_fast() here either we'll see the
	 * elevated page count in the iteration and wait, or
	 * get_user_pages_fast() will see that the page it took a reference
	 * against is no longer mapped in the page tables and bail to the
	 * get_user_pages() slow path.  The slow path is protected by
	 * pte_lock() and pmd_lock(). New references are not taken without
	 * holding those locks, and unmap_mapping_range() will not zero the
	 * pte or pmd without holding the respective lock, so we are
	 * guaranteed to either see new references or prevent new
	 * references from being established.
	 */
	unmap_mapping_range(mapping, 0, 0, 1);

	xas_lock_irq(&xas);
	xas_for_each(&xas, entry, ULONG_MAX) {
		if (WARN_ON_ONCE(!xa_is_value(entry)))
			continue;
		if (unlikely(dax_is_locked(entry)))
			entry = get_unlocked_entry(&xas);
		if (entry)
			page = dax_busy_page(entry);
		put_unlocked_entry(&xas, entry);
		if (page)
			break;
		if (++scanned % XA_CHECK_SCHED)
			continue;

		xas_pause(&xas);
		xas_unlock_irq(&xas);
		cond_resched();
		xas_lock_irq(&xas);
	}
	xas_unlock_irq(&xas);
	return page;
}
EXPORT_SYMBOL_GPL(dax_layout_busy_page);

static int __dax_invalidate_entry(struct address_space *mapping,
					  pgoff_t index, bool trunc)
{
	XA_STATE(xas, &mapping->i_pages, index);
	int ret = 0;
	void *entry;

	xas_lock_irq(&xas);
	entry = get_unlocked_entry(&xas);
	if (!entry || WARN_ON_ONCE(!xa_is_value(entry)))
		goto out;
	if (!trunc &&
	    (xas_get_mark(&xas, PAGECACHE_TAG_DIRTY) ||
	     xas_get_mark(&xas, PAGECACHE_TAG_TOWRITE)))
		goto out;
	dax_disassociate_entry(entry, mapping, trunc);
	xas_store(&xas, NULL);
	mapping->nrexceptional--;
	ret = 1;
out:
	put_unlocked_entry(&xas, entry);
	xas_unlock_irq(&xas);
	return ret;
}

/*
 * Delete DAX entry at @index from @mapping.  Wait for it
 * to be unlocked before deleting it.
 */
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index)
{
	int ret = __dax_invalidate_entry(mapping, index, true);

	/*
	 * This gets called from truncate / punch_hole path. As such, the caller
	 * must hold locks protecting against concurrent modifications of the
	 * page cache (usually fs-private i_mmap_sem for writing). Since the
	 * caller has seen a DAX entry for this index, we better find it
	 * at that index as well...
	 */
	WARN_ON_ONCE(!ret);
	return ret;
}

/*
 * Invalidate DAX entry if it is clean.
 */
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index)
{
	return __dax_invalidate_entry(mapping, index, false);
}

static int copy_user_dax(struct block_device *bdev, struct dax_device *dax_dev,
		sector_t sector, size_t size, struct page *to,
		unsigned long vaddr)
{
	void *vto, *kaddr;
	pgoff_t pgoff;
	long rc;
	int id;

	rc = bdev_dax_pgoff(bdev, sector, size, &pgoff);
	if (rc)
		return rc;

	id = dax_read_lock();
	rc = dax_direct_access(dax_dev, pgoff, PHYS_PFN(size), &kaddr, NULL);
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
static void *dax_insert_entry(struct xa_state *xas,
		struct address_space *mapping, struct vm_fault *vmf,
		void *entry, pfn_t pfn, unsigned long flags, bool dirty)
{
	void *new_entry = dax_make_entry(pfn, flags);

	if (dirty)
		__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);

	if (dax_is_zero_entry(entry) && !(flags & DAX_ZERO_PAGE)) {
		unsigned long index = xas->xa_index;
		/* we are replacing a zero page with block mapping */
		if (dax_is_pmd_entry(entry))
			unmap_mapping_pages(mapping, index & ~PG_PMD_COLOUR,
					PG_PMD_NR, false);
		else /* pte entry */
			unmap_mapping_pages(mapping, index, 1, false);
	}

	xas_reset(xas);
	xas_lock_irq(xas);
	if (dax_entry_size(entry) != dax_entry_size(new_entry)) {
		dax_disassociate_entry(entry, mapping, false);
		dax_associate_entry(new_entry, mapping, vmf->vma, vmf->address);
	}

	if (dax_is_zero_entry(entry) || dax_is_empty_entry(entry)) {
		/*
		 * Only swap our new entry into the page cache if the current
		 * entry is a zero page or an empty entry.  If a normal PTE or
		 * PMD entry is already in the cache, we leave it alone.  This
		 * means that if we are trying to insert a PTE and the
		 * existing entry is a PMD, we will just leave the PMD in the
		 * tree and dirty it if necessary.
		 */
		void *old = dax_lock_entry(xas, new_entry);
		WARN_ON_ONCE(old != xa_mk_value(xa_to_value(entry) |
					DAX_LOCKED));
		entry = new_entry;
	} else {
		xas_load(xas);	/* Walk the xa_state */
	}

	if (dirty)
		xas_set_mark(xas, PAGECACHE_TAG_DIRTY);

	xas_unlock_irq(xas);
	return entry;
}

static inline
unsigned long pgoff_address(pgoff_t pgoff, struct vm_area_struct *vma)
{
	unsigned long address;

	address = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT);
	VM_BUG_ON_VMA(address < vma->vm_start || address >= vma->vm_end, vma);
	return address;
}

/* Walk all mappings of a given index of a file and writeprotect them */
static void dax_entry_mkclean(struct address_space *mapping, pgoff_t index,
		unsigned long pfn)
{
	struct vm_area_struct *vma;
	pte_t pte, *ptep = NULL;
	pmd_t *pmdp = NULL;
	spinlock_t *ptl;

	i_mmap_lock_read(mapping);
	vma_interval_tree_foreach(vma, &mapping->i_mmap, index, index) {
		unsigned long address, start, end;

		cond_resched();

		if (!(vma->vm_flags & VM_SHARED))
			continue;

		address = pgoff_address(index, vma);

		/*
		 * Note because we provide start/end to follow_pte_pmd it will
		 * call mmu_notifier_invalidate_range_start() on our behalf
		 * before taking any lock.
		 */
		if (follow_pte_pmd(vma->vm_mm, address, &start, &end, &ptep, &pmdp, &ptl))
			continue;

		/*
		 * No need to call mmu_notifier_invalidate_range() as we are
		 * downgrading page table protection not changing it to point
		 * to a new page.
		 *
		 * See Documentation/vm/mmu_notifier.rst
		 */
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
unlock_pmd:
#endif
			spin_unlock(ptl);
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
unlock_pte:
			pte_unmap_unlock(ptep, ptl);
		}

		mmu_notifier_invalidate_range_end(vma->vm_mm, start, end);
	}
	i_mmap_unlock_read(mapping);
}

static int dax_writeback_one(struct xa_state *xas, struct dax_device *dax_dev,
		struct address_space *mapping, void *entry)
{
	unsigned long pfn;
	long ret = 0;
	size_t size;

	/*
	 * A page got tagged dirty in DAX mapping? Something is seriously
	 * wrong.
	 */
	if (WARN_ON(!xa_is_value(entry)))
		return -EIO;

	if (unlikely(dax_is_locked(entry))) {
		void *old_entry = entry;

		entry = get_unlocked_entry(xas);

		/* Entry got punched out / reallocated? */
		if (!entry || WARN_ON_ONCE(!xa_is_value(entry)))
			goto put_unlocked;
		/*
		 * Entry got reallocated elsewhere? No need to writeback.
		 * We have to compare pfns as we must not bail out due to
		 * difference in lockbit or entry type.
		 */
		if (dax_to_pfn(old_entry) != dax_to_pfn(entry))
			goto put_unlocked;
		if (WARN_ON_ONCE(dax_is_empty_entry(entry) ||
					dax_is_zero_entry(entry))) {
			ret = -EIO;
			goto put_unlocked;
		}

		/* Another fsync thread may have already done this entry */
		if (!xas_get_mark(xas, PAGECACHE_TAG_TOWRITE))
			goto put_unlocked;
	}

	/* Lock the entry to serialize with page faults */
	dax_lock_entry(xas, entry);

	/*
	 * We can clear the tag now but we have to be careful so that concurrent
	 * dax_writeback_one() calls for the same index cannot finish before we
	 * actually flush the caches. This is achieved as the calls will look
	 * at the entry only under the i_pages lock and once they do that
	 * they will see the entry locked and wait for it to unlock.
	 */
	xas_clear_mark(xas, PAGECACHE_TAG_TOWRITE);
	xas_unlock_irq(xas);

	/*
	 * Even if dax_writeback_mapping_range() was given a wbc->range_start
	 * in the middle of a PMD, the 'index' we are given will be aligned to
	 * the start index of the PMD, as will the pfn we pull from 'entry'.
	 * This allows us to flush for PMD_SIZE and not have to worry about
	 * partial PMD writebacks.
	 */
	pfn = dax_to_pfn(entry);
	size = PAGE_SIZE << dax_entry_order(entry);

	dax_entry_mkclean(mapping, xas->xa_index, pfn);
	dax_flush(dax_dev, page_address(pfn_to_page(pfn)), size);
	/*
	 * After we have flushed the cache, we can clear the dirty tag. There
	 * cannot be new dirty data in the pfn after the flush has completed as
	 * the pfn mappings are writeprotected and fault waits for mapping
	 * entry lock.
	 */
	xas_reset(xas);
	xas_lock_irq(xas);
	xas_store(xas, entry);
	xas_clear_mark(xas, PAGECACHE_TAG_DIRTY);
	dax_wake_entry(xas, entry, false);

	trace_dax_writeback_one(mapping->host, xas->xa_index,
			size >> PAGE_SHIFT);
	return ret;

 put_unlocked:
	put_unlocked_entry(xas, entry);
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
	XA_STATE(xas, &mapping->i_pages, wbc->range_start >> PAGE_SHIFT);
	struct inode *inode = mapping->host;
	pgoff_t end_index = wbc->range_end >> PAGE_SHIFT;
	struct dax_device *dax_dev;
	void *entry;
	int ret = 0;
	unsigned int scanned = 0;

	if (WARN_ON_ONCE(inode->i_blkbits != PAGE_SHIFT))
		return -EIO;

	if (!mapping->nrexceptional || wbc->sync_mode != WB_SYNC_ALL)
		return 0;

	dax_dev = dax_get_by_host(bdev->bd_disk->disk_name);
	if (!dax_dev)
		return -EIO;

	trace_dax_writeback_range(inode, xas.xa_index, end_index);

	tag_pages_for_writeback(mapping, xas.xa_index, end_index);

	xas_lock_irq(&xas);
	xas_for_each_marked(&xas, entry, end_index, PAGECACHE_TAG_TOWRITE) {
		ret = dax_writeback_one(&xas, dax_dev, mapping, entry);
		if (ret < 0) {
			mapping_set_error(mapping, ret);
			break;
		}
		if (++scanned % XA_CHECK_SCHED)
			continue;

		xas_pause(&xas);
		xas_unlock_irq(&xas);
		cond_resched();
		xas_lock_irq(&xas);
	}
	xas_unlock_irq(&xas);
	put_dax(dax_dev);
	trace_dax_writeback_range_done(inode, xas.xa_index, end_index);
	return ret;
}
EXPORT_SYMBOL_GPL(dax_writeback_mapping_range);

static sector_t dax_iomap_sector(struct iomap *iomap, loff_t pos)
{
	return (iomap->addr + (pos & PAGE_MASK) - iomap->offset) >> 9;
}

static int dax_iomap_pfn(struct iomap *iomap, loff_t pos, size_t size,
			 pfn_t *pfnp)
{
	const sector_t sector = dax_iomap_sector(iomap, pos);
	pgoff_t pgoff;
	int id, rc;
	long length;

	rc = bdev_dax_pgoff(iomap->bdev, sector, size, &pgoff);
	if (rc)
		return rc;
	id = dax_read_lock();
	length = dax_direct_access(iomap->dax_dev, pgoff, PHYS_PFN(size),
				   NULL, pfnp);
	if (length < 0) {
		rc = length;
		goto out;
	}
	rc = -EINVAL;
	if (PFN_PHYS(length) < size)
		goto out;
	if (pfn_t_to_pfn(*pfnp) & (PHYS_PFN(size)-1))
		goto out;
	/* For larger pages we need devmap */
	if (length > 1 && !pfn_t_devmap(*pfnp))
		goto out;
	rc = 0;
out:
	dax_read_unlock(id);
	return rc;
}

/*
 * The user has performed a load from a hole in the file.  Allocating a new
 * page in the file would cause excessive storage usage for workloads with
 * sparse files.  Instead we insert a read-only mapping of the 4k zero page.
 * If this page is ever written to we will re-fault and change the mapping to
 * point to real DAX storage instead.
 */
static vm_fault_t dax_load_hole(struct xa_state *xas,
		struct address_space *mapping, void **entry,
		struct vm_fault *vmf)
{
	struct inode *inode = mapping->host;
	unsigned long vaddr = vmf->address;
	pfn_t pfn = pfn_to_pfn_t(my_zero_pfn(vaddr));
	vm_fault_t ret;

	*entry = dax_insert_entry(xas, mapping, vmf, *entry, pfn,
			DAX_ZERO_PAGE, false);

	ret = vmf_insert_mixed(vmf->vma, vaddr, pfn);
	trace_dax_load_hole(inode, vmf, ret);
	return ret;
}

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

		rc = bdev_dax_pgoff(bdev, sector, PAGE_SIZE, &pgoff);
		if (rc)
			return rc;

		id = dax_read_lock();
		rc = dax_direct_access(dax_dev, pgoff, 1, &kaddr, NULL);
		if (rc < 0) {
			dax_read_unlock(id);
			return rc;
		}
		memset(kaddr + offset, 0, size);
		dax_flush(dax_dev, kaddr + offset, size);
		dax_read_unlock(id);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__dax_zero_page_range);

static loff_t
dax_iomap_actor(struct inode *inode, loff_t pos, loff_t length, void *data,
		struct iomap *iomap)
{
	struct block_device *bdev = iomap->bdev;
	struct dax_device *dax_dev = iomap->dax_dev;
	struct iov_iter *iter = data;
	loff_t end = pos + length, done = 0;
	ssize_t ret = 0;
	size_t xfer;
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

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		ret = bdev_dax_pgoff(bdev, sector, size, &pgoff);
		if (ret)
			break;

		map_len = dax_direct_access(dax_dev, pgoff, PHYS_PFN(size),
				&kaddr, NULL);
		if (map_len < 0) {
			ret = map_len;
			break;
		}

		map_len = PFN_PHYS(map_len);
		kaddr += offset;
		map_len -= offset;
		if (map_len > end - pos)
			map_len = end - pos;

		/*
		 * The userspace address for the memory copy has already been
		 * validated via access_ok() in either vfs_read() or
		 * vfs_write(), depending on which operation we are doing.
		 */
		if (iov_iter_rw(iter) == WRITE)
			xfer = dax_copy_from_iter(dax_dev, pgoff, kaddr,
					map_len, iter);
		else
			xfer = dax_copy_to_iter(dax_dev, pgoff, kaddr,
					map_len, iter);

		pos += xfer;
		length -= xfer;
		done += xfer;

		if (xfer == 0)
			ret = -EFAULT;
		if (xfer < map_len)
			break;
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

static vm_fault_t dax_fault_return(int error)
{
	if (error == 0)
		return VM_FAULT_NOPAGE;
	if (error == -ENOMEM)
		return VM_FAULT_OOM;
	return VM_FAULT_SIGBUS;
}

/*
 * MAP_SYNC on a dax mapping guarantees dirty metadata is
 * flushed on write-faults (non-cow), but not read-faults.
 */
static bool dax_fault_is_synchronous(unsigned long flags,
		struct vm_area_struct *vma, struct iomap *iomap)
{
	return (flags & IOMAP_WRITE) && (vma->vm_flags & VM_SYNC)
		&& (iomap->flags & IOMAP_F_DIRTY);
}

static vm_fault_t dax_iomap_pte_fault(struct vm_fault *vmf, pfn_t *pfnp,
			       int *iomap_errp, const struct iomap_ops *ops)
{
	struct vm_area_struct *vma = vmf->vma;
	struct address_space *mapping = vma->vm_file->f_mapping;
	XA_STATE(xas, &mapping->i_pages, vmf->pgoff);
	struct inode *inode = mapping->host;
	unsigned long vaddr = vmf->address;
	loff_t pos = (loff_t)vmf->pgoff << PAGE_SHIFT;
	struct iomap iomap = { 0 };
	unsigned flags = IOMAP_FAULT;
	int error, major = 0;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	bool sync;
	vm_fault_t ret = 0;
	void *entry;
	pfn_t pfn;

	trace_dax_pte_fault(inode, vmf, ret);
	/*
	 * Check whether offset isn't beyond end of file now. Caller is supposed
	 * to hold locks serializing us with truncate / punch hole so this is
	 * a reliable test.
	 */
	if (pos >= i_size_read(inode)) {
		ret = VM_FAULT_SIGBUS;
		goto out;
	}

	if (write && !vmf->cow_page)
		flags |= IOMAP_WRITE;

	entry = grab_mapping_entry(&xas, mapping, 0);
	if (xa_is_internal(entry)) {
		ret = xa_to_internal(entry);
		goto out;
	}

	/*
	 * It is possible, particularly with mixed reads & writes to private
	 * mappings, that we have raced with a PMD fault that overlaps with
	 * the PTE we need to set up.  If so just return and the fault will be
	 * retried.
	 */
	if (pmd_trans_huge(*vmf->pmd) || pmd_devmap(*vmf->pmd)) {
		ret = VM_FAULT_NOPAGE;
		goto unlock_entry;
	}

	/*
	 * Note that we don't bother to use iomap_apply here: DAX required
	 * the file system block size to be equal the page size, which means
	 * that we never have to deal with more than a single extent here.
	 */
	error = ops->iomap_begin(inode, pos, PAGE_SIZE, flags, &iomap);
	if (iomap_errp)
		*iomap_errp = error;
	if (error) {
		ret = dax_fault_return(error);
		goto unlock_entry;
	}
	if (WARN_ON_ONCE(iomap.offset + iomap.length < pos + PAGE_SIZE)) {
		error = -EIO;	/* fs corruption? */
		goto error_finish_iomap;
	}

	if (vmf->cow_page) {
		sector_t sector = dax_iomap_sector(&iomap, pos);

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
		ret = finish_fault(vmf);
		if (!ret)
			ret = VM_FAULT_DONE_COW;
		goto finish_iomap;
	}

	sync = dax_fault_is_synchronous(flags, vma, &iomap);

	switch (iomap.type) {
	case IOMAP_MAPPED:
		if (iomap.flags & IOMAP_F_NEW) {
			count_vm_event(PGMAJFAULT);
			count_memcg_event_mm(vma->vm_mm, PGMAJFAULT);
			major = VM_FAULT_MAJOR;
		}
		error = dax_iomap_pfn(&iomap, pos, PAGE_SIZE, &pfn);
		if (error < 0)
			goto error_finish_iomap;

		entry = dax_insert_entry(&xas, mapping, vmf, entry, pfn,
						 0, write && !sync);

		/*
		 * If we are doing synchronous page fault and inode needs fsync,
		 * we can insert PTE into page tables only after that happens.
		 * Skip insertion for now and return the pfn so that caller can
		 * insert it after fsync is done.
		 */
		if (sync) {
			if (WARN_ON_ONCE(!pfnp)) {
				error = -EIO;
				goto error_finish_iomap;
			}
			*pfnp = pfn;
			ret = VM_FAULT_NEEDDSYNC | major;
			goto finish_iomap;
		}
		trace_dax_insert_mapping(inode, vmf, entry);
		if (write)
			ret = vmf_insert_mixed_mkwrite(vma, vaddr, pfn);
		else
			ret = vmf_insert_mixed(vma, vaddr, pfn);

		goto finish_iomap;
	case IOMAP_UNWRITTEN:
	case IOMAP_HOLE:
		if (!write) {
			ret = dax_load_hole(&xas, mapping, &entry, vmf);
			goto finish_iomap;
		}
		/*FALLTHRU*/
	default:
		WARN_ON_ONCE(1);
		error = -EIO;
		break;
	}

 error_finish_iomap:
	ret = dax_fault_return(error);
 finish_iomap:
	if (ops->iomap_end) {
		int copied = PAGE_SIZE;

		if (ret & VM_FAULT_ERROR)
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
	dax_unlock_entry(&xas, entry);
 out:
	trace_dax_pte_fault_done(inode, vmf, ret);
	return ret | major;
}

#ifdef CONFIG_FS_DAX_PMD
static vm_fault_t dax_pmd_load_hole(struct xa_state *xas, struct vm_fault *vmf,
		struct iomap *iomap, void **entry)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	struct inode *inode = mapping->host;
	struct page *zero_page;
	spinlock_t *ptl;
	pmd_t pmd_entry;
	pfn_t pfn;

	zero_page = mm_get_huge_zero_page(vmf->vma->vm_mm);

	if (unlikely(!zero_page))
		goto fallback;

	pfn = page_to_pfn_t(zero_page);
	*entry = dax_insert_entry(xas, mapping, vmf, *entry, pfn,
			DAX_PMD | DAX_ZERO_PAGE, false);

	ptl = pmd_lock(vmf->vma->vm_mm, vmf->pmd);
	if (!pmd_none(*(vmf->pmd))) {
		spin_unlock(ptl);
		goto fallback;
	}

	pmd_entry = mk_pmd(zero_page, vmf->vma->vm_page_prot);
	pmd_entry = pmd_mkhuge(pmd_entry);
	set_pmd_at(vmf->vma->vm_mm, pmd_addr, vmf->pmd, pmd_entry);
	spin_unlock(ptl);
	trace_dax_pmd_load_hole(inode, vmf, zero_page, *entry);
	return VM_FAULT_NOPAGE;

fallback:
	trace_dax_pmd_load_hole_fallback(inode, vmf, zero_page, *entry);
	return VM_FAULT_FALLBACK;
}

static vm_fault_t dax_iomap_pmd_fault(struct vm_fault *vmf, pfn_t *pfnp,
			       const struct iomap_ops *ops)
{
	struct vm_area_struct *vma = vmf->vma;
	struct address_space *mapping = vma->vm_file->f_mapping;
	XA_STATE_ORDER(xas, &mapping->i_pages, vmf->pgoff, PMD_ORDER);
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	bool write = vmf->flags & FAULT_FLAG_WRITE;
	bool sync;
	unsigned int iomap_flags = (write ? IOMAP_WRITE : 0) | IOMAP_FAULT;
	struct inode *inode = mapping->host;
	vm_fault_t result = VM_FAULT_FALLBACK;
	struct iomap iomap = { 0 };
	pgoff_t max_pgoff;
	void *entry;
	loff_t pos;
	int error;
	pfn_t pfn;

	/*
	 * Check whether offset isn't beyond end of file now. Caller is
	 * supposed to hold locks serializing us with truncate / punch hole so
	 * this is a reliable test.
	 */
	max_pgoff = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);

	trace_dax_pmd_fault(inode, vmf, max_pgoff, 0);

	/*
	 * Make sure that the faulting address's PMD offset (color) matches
	 * the PMD offset from the start of the file.  This is necessary so
	 * that a PMD range in the page table overlaps exactly with a PMD
	 * range in the page cache.
	 */
	if ((vmf->pgoff & PG_PMD_COLOUR) !=
	    ((vmf->address >> PAGE_SHIFT) & PG_PMD_COLOUR))
		goto fallback;

	/* Fall back to PTEs if we're going to COW */
	if (write && !(vma->vm_flags & VM_SHARED))
		goto fallback;

	/* If the PMD would extend outside the VMA */
	if (pmd_addr < vma->vm_start)
		goto fallback;
	if ((pmd_addr + PMD_SIZE) > vma->vm_end)
		goto fallback;

	if (xas.xa_index >= max_pgoff) {
		result = VM_FAULT_SIGBUS;
		goto out;
	}

	/* If the PMD would extend beyond the file size */
	if ((xas.xa_index | PG_PMD_COLOUR) >= max_pgoff)
		goto fallback;

	/*
	 * grab_mapping_entry() will make sure we get an empty PMD entry,
	 * a zero PMD entry or a DAX PMD.  If it can't (because a PTE
	 * entry is already in the array, for instance), it will return
	 * VM_FAULT_FALLBACK.
	 */
	entry = grab_mapping_entry(&xas, mapping, DAX_PMD);
	if (xa_is_internal(entry)) {
		result = xa_to_internal(entry);
		goto fallback;
	}

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
	pos = (loff_t)xas.xa_index << PAGE_SHIFT;
	error = ops->iomap_begin(inode, pos, PMD_SIZE, iomap_flags, &iomap);
	if (error)
		goto unlock_entry;

	if (iomap.offset + iomap.length < pos + PMD_SIZE)
		goto finish_iomap;

	sync = dax_fault_is_synchronous(iomap_flags, vma, &iomap);

	switch (iomap.type) {
	case IOMAP_MAPPED:
		error = dax_iomap_pfn(&iomap, pos, PMD_SIZE, &pfn);
		if (error < 0)
			goto finish_iomap;

		entry = dax_insert_entry(&xas, mapping, vmf, entry, pfn,
						DAX_PMD, write && !sync);

		/*
		 * If we are doing synchronous page fault and inode needs fsync,
		 * we can insert PMD into page tables only after that happens.
		 * Skip insertion for now and return the pfn so that caller can
		 * insert it after fsync is done.
		 */
		if (sync) {
			if (WARN_ON_ONCE(!pfnp))
				goto finish_iomap;
			*pfnp = pfn;
			result = VM_FAULT_NEEDDSYNC;
			goto finish_iomap;
		}

		trace_dax_pmd_insert_mapping(inode, vmf, PMD_SIZE, pfn, entry);
		result = vmf_insert_pfn_pmd(vma, vmf->address, vmf->pmd, pfn,
					    write);
		break;
	case IOMAP_UNWRITTEN:
	case IOMAP_HOLE:
		if (WARN_ON_ONCE(write))
			break;
		result = dax_pmd_load_hole(&xas, vmf, &iomap, &entry);
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
	dax_unlock_entry(&xas, entry);
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
static vm_fault_t dax_iomap_pmd_fault(struct vm_fault *vmf, pfn_t *pfnp,
			       const struct iomap_ops *ops)
{
	return VM_FAULT_FALLBACK;
}
#endif /* CONFIG_FS_DAX_PMD */

/**
 * dax_iomap_fault - handle a page fault on a DAX file
 * @vmf: The description of the fault
 * @pe_size: Size of the page to fault in
 * @pfnp: PFN to insert for synchronous faults if fsync is required
 * @iomap_errp: Storage for detailed error code in case of error
 * @ops: Iomap ops passed from the file system
 *
 * When a page fault occurs, filesystems may call this helper in
 * their fault handler for DAX files. dax_iomap_fault() assumes the caller
 * has done all the necessary locking for page fault to proceed
 * successfully.
 */
vm_fault_t dax_iomap_fault(struct vm_fault *vmf, enum page_entry_size pe_size,
		    pfn_t *pfnp, int *iomap_errp, const struct iomap_ops *ops)
{
	switch (pe_size) {
	case PE_SIZE_PTE:
		return dax_iomap_pte_fault(vmf, pfnp, iomap_errp, ops);
	case PE_SIZE_PMD:
		return dax_iomap_pmd_fault(vmf, pfnp, ops);
	default:
		return VM_FAULT_FALLBACK;
	}
}
EXPORT_SYMBOL_GPL(dax_iomap_fault);

/*
 * dax_insert_pfn_mkwrite - insert PTE or PMD entry into page tables
 * @vmf: The description of the fault
 * @pfn: PFN to insert
 * @order: Order of entry to insert.
 *
 * This function inserts a writeable PTE or PMD entry into the page tables
 * for an mmaped DAX file.  It also marks the page cache entry as dirty.
 */
static vm_fault_t
dax_insert_pfn_mkwrite(struct vm_fault *vmf, pfn_t pfn, unsigned int order)
{
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	XA_STATE_ORDER(xas, &mapping->i_pages, vmf->pgoff, order);
	void *entry;
	vm_fault_t ret;

	xas_lock_irq(&xas);
	entry = get_unlocked_entry(&xas);
	/* Did we race with someone splitting entry or so? */
	if (!entry ||
	    (order == 0 && !dax_is_pte_entry(entry)) ||
	    (order == PMD_ORDER && !dax_is_pmd_entry(entry))) {
		put_unlocked_entry(&xas, entry);
		xas_unlock_irq(&xas);
		trace_dax_insert_pfn_mkwrite_no_entry(mapping->host, vmf,
						      VM_FAULT_NOPAGE);
		return VM_FAULT_NOPAGE;
	}
	xas_set_mark(&xas, PAGECACHE_TAG_DIRTY);
	dax_lock_entry(&xas, entry);
	xas_unlock_irq(&xas);
	if (order == 0)
		ret = vmf_insert_mixed_mkwrite(vmf->vma, vmf->address, pfn);
#ifdef CONFIG_FS_DAX_PMD
	else if (order == PMD_ORDER)
		ret = vmf_insert_pfn_pmd(vmf->vma, vmf->address, vmf->pmd,
			pfn, true);
#endif
	else
		ret = VM_FAULT_FALLBACK;
	dax_unlock_entry(&xas, entry);
	trace_dax_insert_pfn_mkwrite(mapping->host, vmf, ret);
	return ret;
}

/**
 * dax_finish_sync_fault - finish synchronous page fault
 * @vmf: The description of the fault
 * @pe_size: Size of entry to be inserted
 * @pfn: PFN to insert
 *
 * This function ensures that the file range touched by the page fault is
 * stored persistently on the media and handles inserting of appropriate page
 * table entry.
 */
vm_fault_t dax_finish_sync_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size, pfn_t pfn)
{
	int err;
	loff_t start = ((loff_t)vmf->pgoff) << PAGE_SHIFT;
	unsigned int order = pe_order(pe_size);
	size_t len = PAGE_SIZE << order;

	err = vfs_fsync_range(vmf->vma->vm_file, start, start + len - 1, 1);
	if (err)
		return VM_FAULT_SIGBUS;
	return dax_insert_pfn_mkwrite(vmf, pfn, order);
}
EXPORT_SYMBOL_GPL(dax_finish_sync_fault);
