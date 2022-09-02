// SPDX-License-Identifier: GPL-2.0
/*
 * Memory Migration functionality - linux/mm/migrate.c
 *
 * Copyright (C) 2006 Silicon Graphics, Inc., Christoph Lameter
 *
 * Page migration was first developed in the context of the memory hotplug
 * project. The main authors of the migration code are:
 *
 * IWAMOTO Toshihiro <iwamoto@valinux.co.jp>
 * Hirokazu Takahashi <taka@valinux.co.jp>
 * Dave Hansen <haveblue@us.ibm.com>
 * Christoph Lameter
 */

#include <linux/migrate.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/mm_inline.h>
#include <linux/nsproxy.h>
#include <linux/pagevec.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/writeback.h>
#include <linux/mempolicy.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/backing-dev.h>
#include <linux/compaction.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/hugetlb.h>
#include <linux/hugetlb_cgroup.h>
#include <linux/gfp.h>
#include <linux/pfn_t.h>
#include <linux/memremap.h>
#include <linux/userfaultfd_k.h>
#include <linux/balloon_compaction.h>
#include <linux/page_idle.h>
#include <linux/page_owner.h>
#include <linux/sched/mm.h>
#include <linux/ptrace.h>
#include <linux/oom.h>
#include <linux/memory.h>
#include <linux/random.h>
#include <linux/sched/sysctl.h>
#include <linux/memory-tiers.h>

#include <asm/tlbflush.h>

#include <trace/events/migrate.h>

#include "internal.h"

int isolate_movable_page(struct page *page, isolate_mode_t mode)
{
	const struct movable_operations *mops;

	/*
	 * Avoid burning cycles with pages that are yet under __free_pages(),
	 * or just got freed under us.
	 *
	 * In case we 'win' a race for a movable page being freed under us and
	 * raise its refcount preventing __free_pages() from doing its job
	 * the put_page() at the end of this block will take care of
	 * release this page, thus avoiding a nasty leakage.
	 */
	if (unlikely(!get_page_unless_zero(page)))
		goto out;

	/*
	 * Check PageMovable before holding a PG_lock because page's owner
	 * assumes anybody doesn't touch PG_lock of newly allocated page
	 * so unconditionally grabbing the lock ruins page's owner side.
	 */
	if (unlikely(!__PageMovable(page)))
		goto out_putpage;
	/*
	 * As movable pages are not isolated from LRU lists, concurrent
	 * compaction threads can race against page migration functions
	 * as well as race against the releasing a page.
	 *
	 * In order to avoid having an already isolated movable page
	 * being (wrongly) re-isolated while it is under migration,
	 * or to avoid attempting to isolate pages being released,
	 * lets be sure we have the page lock
	 * before proceeding with the movable page isolation steps.
	 */
	if (unlikely(!trylock_page(page)))
		goto out_putpage;

	if (!PageMovable(page) || PageIsolated(page))
		goto out_no_isolated;

	mops = page_movable_ops(page);
	VM_BUG_ON_PAGE(!mops, page);

	if (!mops->isolate_page(page, mode))
		goto out_no_isolated;

	/* Driver shouldn't use PG_isolated bit of page->flags */
	WARN_ON_ONCE(PageIsolated(page));
	SetPageIsolated(page);
	unlock_page(page);

	return 0;

out_no_isolated:
	unlock_page(page);
out_putpage:
	put_page(page);
out:
	return -EBUSY;
}

static void putback_movable_page(struct page *page)
{
	const struct movable_operations *mops = page_movable_ops(page);

	mops->putback_page(page);
	ClearPageIsolated(page);
}

/*
 * Put previously isolated pages back onto the appropriate lists
 * from where they were once taken off for compaction/migration.
 *
 * This function shall be used whenever the isolated pageset has been
 * built from lru, balloon, hugetlbfs page. See isolate_migratepages_range()
 * and isolate_hugetlb().
 */
void putback_movable_pages(struct list_head *l)
{
	struct page *page;
	struct page *page2;

	list_for_each_entry_safe(page, page2, l, lru) {
		if (unlikely(PageHuge(page))) {
			putback_active_hugepage(page);
			continue;
		}
		list_del(&page->lru);
		/*
		 * We isolated non-lru movable page so here we can use
		 * __PageMovable because LRU page's mapping cannot have
		 * PAGE_MAPPING_MOVABLE.
		 */
		if (unlikely(__PageMovable(page))) {
			VM_BUG_ON_PAGE(!PageIsolated(page), page);
			lock_page(page);
			if (PageMovable(page))
				putback_movable_page(page);
			else
				ClearPageIsolated(page);
			unlock_page(page);
			put_page(page);
		} else {
			mod_node_page_state(page_pgdat(page), NR_ISOLATED_ANON +
					page_is_file_lru(page), -thp_nr_pages(page));
			putback_lru_page(page);
		}
	}
}

/*
 * Restore a potential migration pte to a working pte entry
 */
static bool remove_migration_pte(struct folio *folio,
		struct vm_area_struct *vma, unsigned long addr, void *old)
{
	DEFINE_FOLIO_VMA_WALK(pvmw, old, vma, addr, PVMW_SYNC | PVMW_MIGRATION);

	while (page_vma_mapped_walk(&pvmw)) {
		rmap_t rmap_flags = RMAP_NONE;
		pte_t pte;
		swp_entry_t entry;
		struct page *new;
		unsigned long idx = 0;

		/* pgoff is invalid for ksm pages, but they are never large */
		if (folio_test_large(folio) && !folio_test_hugetlb(folio))
			idx = linear_page_index(vma, pvmw.address) - pvmw.pgoff;
		new = folio_page(folio, idx);

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
		/* PMD-mapped THP migration entry */
		if (!pvmw.pte) {
			VM_BUG_ON_FOLIO(folio_test_hugetlb(folio) ||
					!folio_test_pmd_mappable(folio), folio);
			remove_migration_pmd(&pvmw, new);
			continue;
		}
#endif

		folio_get(folio);
		pte = mk_pte(new, READ_ONCE(vma->vm_page_prot));
		if (pte_swp_soft_dirty(*pvmw.pte))
			pte = pte_mksoft_dirty(pte);

		/*
		 * Recheck VMA as permissions can change since migration started
		 */
		entry = pte_to_swp_entry(*pvmw.pte);
		if (!is_migration_entry_young(entry))
			pte = pte_mkold(pte);
		if (folio_test_dirty(folio) && is_migration_entry_dirty(entry))
			pte = pte_mkdirty(pte);
		if (is_writable_migration_entry(entry))
			pte = maybe_mkwrite(pte, vma);
		else if (pte_swp_uffd_wp(*pvmw.pte))
			pte = pte_mkuffd_wp(pte);

		if (folio_test_anon(folio) && !is_readable_migration_entry(entry))
			rmap_flags |= RMAP_EXCLUSIVE;

		if (unlikely(is_device_private_page(new))) {
			if (pte_write(pte))
				entry = make_writable_device_private_entry(
							page_to_pfn(new));
			else
				entry = make_readable_device_private_entry(
							page_to_pfn(new));
			pte = swp_entry_to_pte(entry);
			if (pte_swp_soft_dirty(*pvmw.pte))
				pte = pte_swp_mksoft_dirty(pte);
			if (pte_swp_uffd_wp(*pvmw.pte))
				pte = pte_swp_mkuffd_wp(pte);
		}

#ifdef CONFIG_HUGETLB_PAGE
		if (folio_test_hugetlb(folio)) {
			unsigned int shift = huge_page_shift(hstate_vma(vma));

			pte = pte_mkhuge(pte);
			pte = arch_make_huge_pte(pte, shift, vma->vm_flags);
			if (folio_test_anon(folio))
				hugepage_add_anon_rmap(new, vma, pvmw.address,
						       rmap_flags);
			else
				page_dup_file_rmap(new, true);
			set_huge_pte_at(vma->vm_mm, pvmw.address, pvmw.pte, pte);
		} else
#endif
		{
			if (folio_test_anon(folio))
				page_add_anon_rmap(new, vma, pvmw.address,
						   rmap_flags);
			else
				page_add_file_rmap(new, vma, false);
			set_pte_at(vma->vm_mm, pvmw.address, pvmw.pte, pte);
		}
		if (vma->vm_flags & VM_LOCKED)
			mlock_page_drain_local();

		trace_remove_migration_pte(pvmw.address, pte_val(pte),
					   compound_order(new));

		/* No need to invalidate - it was non-present before */
		update_mmu_cache(vma, pvmw.address, pvmw.pte);
	}

	return true;
}

/*
 * Get rid of all migration entries and replace them by
 * references to the indicated page.
 */
void remove_migration_ptes(struct folio *src, struct folio *dst, bool locked)
{
	struct rmap_walk_control rwc = {
		.rmap_one = remove_migration_pte,
		.arg = src,
	};

	if (locked)
		rmap_walk_locked(dst, &rwc);
	else
		rmap_walk(dst, &rwc);
}

/*
 * Something used the pte of a page under migration. We need to
 * get to the page and wait until migration is finished.
 * When we return from this function the fault will be retried.
 */
void __migration_entry_wait(struct mm_struct *mm, pte_t *ptep,
				spinlock_t *ptl)
{
	pte_t pte;
	swp_entry_t entry;

	spin_lock(ptl);
	pte = *ptep;
	if (!is_swap_pte(pte))
		goto out;

	entry = pte_to_swp_entry(pte);
	if (!is_migration_entry(entry))
		goto out;

	migration_entry_wait_on_locked(entry, ptep, ptl);
	return;
out:
	pte_unmap_unlock(ptep, ptl);
}

void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
				unsigned long address)
{
	spinlock_t *ptl = pte_lockptr(mm, pmd);
	pte_t *ptep = pte_offset_map(pmd, address);
	__migration_entry_wait(mm, ptep, ptl);
}

#ifdef CONFIG_HUGETLB_PAGE
void __migration_entry_wait_huge(pte_t *ptep, spinlock_t *ptl)
{
	pte_t pte;

	spin_lock(ptl);
	pte = huge_ptep_get(ptep);

	if (unlikely(!is_hugetlb_entry_migration(pte)))
		spin_unlock(ptl);
	else
		migration_entry_wait_on_locked(pte_to_swp_entry(pte), NULL, ptl);
}

void migration_entry_wait_huge(struct vm_area_struct *vma, pte_t *pte)
{
	spinlock_t *ptl = huge_pte_lockptr(hstate_vma(vma), vma->vm_mm, pte);

	__migration_entry_wait_huge(pte, ptl);
}
#endif

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
void pmd_migration_entry_wait(struct mm_struct *mm, pmd_t *pmd)
{
	spinlock_t *ptl;

	ptl = pmd_lock(mm, pmd);
	if (!is_pmd_migration_entry(*pmd))
		goto unlock;
	migration_entry_wait_on_locked(pmd_to_swp_entry(*pmd), NULL, ptl);
	return;
unlock:
	spin_unlock(ptl);
}
#endif

static int folio_expected_refs(struct address_space *mapping,
		struct folio *folio)
{
	int refs = 1;
	if (!mapping)
		return refs;

	refs += folio_nr_pages(folio);
	if (folio_test_private(folio))
		refs++;

	return refs;
}

/*
 * Replace the page in the mapping.
 *
 * The number of remaining references must be:
 * 1 for anonymous pages without a mapping
 * 2 for pages with a mapping
 * 3 for pages with a mapping and PagePrivate/PagePrivate2 set.
 */
int folio_migrate_mapping(struct address_space *mapping,
		struct folio *newfolio, struct folio *folio, int extra_count)
{
	XA_STATE(xas, &mapping->i_pages, folio_index(folio));
	struct zone *oldzone, *newzone;
	int dirty;
	int expected_count = folio_expected_refs(mapping, folio) + extra_count;
	long nr = folio_nr_pages(folio);

	if (!mapping) {
		/* Anonymous page without mapping */
		if (folio_ref_count(folio) != expected_count)
			return -EAGAIN;

		/* No turning back from here */
		newfolio->index = folio->index;
		newfolio->mapping = folio->mapping;
		if (folio_test_swapbacked(folio))
			__folio_set_swapbacked(newfolio);

		return MIGRATEPAGE_SUCCESS;
	}

	oldzone = folio_zone(folio);
	newzone = folio_zone(newfolio);

	xas_lock_irq(&xas);
	if (!folio_ref_freeze(folio, expected_count)) {
		xas_unlock_irq(&xas);
		return -EAGAIN;
	}

	/*
	 * Now we know that no one else is looking at the folio:
	 * no turning back from here.
	 */
	newfolio->index = folio->index;
	newfolio->mapping = folio->mapping;
	folio_ref_add(newfolio, nr); /* add cache reference */
	if (folio_test_swapbacked(folio)) {
		__folio_set_swapbacked(newfolio);
		if (folio_test_swapcache(folio)) {
			folio_set_swapcache(newfolio);
			newfolio->private = folio_get_private(folio);
		}
	} else {
		VM_BUG_ON_FOLIO(folio_test_swapcache(folio), folio);
	}

	/* Move dirty while page refs frozen and newpage not yet exposed */
	dirty = folio_test_dirty(folio);
	if (dirty) {
		folio_clear_dirty(folio);
		folio_set_dirty(newfolio);
	}

	xas_store(&xas, newfolio);

	/*
	 * Drop cache reference from old page by unfreezing
	 * to one less reference.
	 * We know this isn't the last reference.
	 */
	folio_ref_unfreeze(folio, expected_count - nr);

	xas_unlock(&xas);
	/* Leave irq disabled to prevent preemption while updating stats */

	/*
	 * If moved to a different zone then also account
	 * the page for that zone. Other VM counters will be
	 * taken care of when we establish references to the
	 * new page and drop references to the old page.
	 *
	 * Note that anonymous pages are accounted for
	 * via NR_FILE_PAGES and NR_ANON_MAPPED if they
	 * are mapped to swap space.
	 */
	if (newzone != oldzone) {
		struct lruvec *old_lruvec, *new_lruvec;
		struct mem_cgroup *memcg;

		memcg = folio_memcg(folio);
		old_lruvec = mem_cgroup_lruvec(memcg, oldzone->zone_pgdat);
		new_lruvec = mem_cgroup_lruvec(memcg, newzone->zone_pgdat);

		__mod_lruvec_state(old_lruvec, NR_FILE_PAGES, -nr);
		__mod_lruvec_state(new_lruvec, NR_FILE_PAGES, nr);
		if (folio_test_swapbacked(folio) && !folio_test_swapcache(folio)) {
			__mod_lruvec_state(old_lruvec, NR_SHMEM, -nr);
			__mod_lruvec_state(new_lruvec, NR_SHMEM, nr);
		}
#ifdef CONFIG_SWAP
		if (folio_test_swapcache(folio)) {
			__mod_lruvec_state(old_lruvec, NR_SWAPCACHE, -nr);
			__mod_lruvec_state(new_lruvec, NR_SWAPCACHE, nr);
		}
#endif
		if (dirty && mapping_can_writeback(mapping)) {
			__mod_lruvec_state(old_lruvec, NR_FILE_DIRTY, -nr);
			__mod_zone_page_state(oldzone, NR_ZONE_WRITE_PENDING, -nr);
			__mod_lruvec_state(new_lruvec, NR_FILE_DIRTY, nr);
			__mod_zone_page_state(newzone, NR_ZONE_WRITE_PENDING, nr);
		}
	}
	local_irq_enable();

	return MIGRATEPAGE_SUCCESS;
}
EXPORT_SYMBOL(folio_migrate_mapping);

/*
 * The expected number of remaining references is the same as that
 * of folio_migrate_mapping().
 */
int migrate_huge_page_move_mapping(struct address_space *mapping,
				   struct folio *dst, struct folio *src)
{
	XA_STATE(xas, &mapping->i_pages, folio_index(src));
	int expected_count;

	xas_lock_irq(&xas);
	expected_count = 2 + folio_has_private(src);
	if (!folio_ref_freeze(src, expected_count)) {
		xas_unlock_irq(&xas);
		return -EAGAIN;
	}

	dst->index = src->index;
	dst->mapping = src->mapping;

	folio_get(dst);

	xas_store(&xas, dst);

	folio_ref_unfreeze(src, expected_count - 1);

	xas_unlock_irq(&xas);

	return MIGRATEPAGE_SUCCESS;
}

/*
 * Copy the flags and some other ancillary information
 */
void folio_migrate_flags(struct folio *newfolio, struct folio *folio)
{
	int cpupid;

	if (folio_test_error(folio))
		folio_set_error(newfolio);
	if (folio_test_referenced(folio))
		folio_set_referenced(newfolio);
	if (folio_test_uptodate(folio))
		folio_mark_uptodate(newfolio);
	if (folio_test_clear_active(folio)) {
		VM_BUG_ON_FOLIO(folio_test_unevictable(folio), folio);
		folio_set_active(newfolio);
	} else if (folio_test_clear_unevictable(folio))
		folio_set_unevictable(newfolio);
	if (folio_test_workingset(folio))
		folio_set_workingset(newfolio);
	if (folio_test_checked(folio))
		folio_set_checked(newfolio);
	/*
	 * PG_anon_exclusive (-> PG_mappedtodisk) is always migrated via
	 * migration entries. We can still have PG_anon_exclusive set on an
	 * effectively unmapped and unreferenced first sub-pages of an
	 * anonymous THP: we can simply copy it here via PG_mappedtodisk.
	 */
	if (folio_test_mappedtodisk(folio))
		folio_set_mappedtodisk(newfolio);

	/* Move dirty on pages not done by folio_migrate_mapping() */
	if (folio_test_dirty(folio))
		folio_set_dirty(newfolio);

	if (folio_test_young(folio))
		folio_set_young(newfolio);
	if (folio_test_idle(folio))
		folio_set_idle(newfolio);

	/*
	 * Copy NUMA information to the new page, to prevent over-eager
	 * future migrations of this same page.
	 */
	cpupid = page_cpupid_xchg_last(&folio->page, -1);
	/*
	 * For memory tiering mode, when migrate between slow and fast
	 * memory node, reset cpupid, because that is used to record
	 * page access time in slow memory node.
	 */
	if (sysctl_numa_balancing_mode & NUMA_BALANCING_MEMORY_TIERING) {
		bool f_toptier = node_is_toptier(page_to_nid(&folio->page));
		bool t_toptier = node_is_toptier(page_to_nid(&newfolio->page));

		if (f_toptier != t_toptier)
			cpupid = -1;
	}
	page_cpupid_xchg_last(&newfolio->page, cpupid);

	folio_migrate_ksm(newfolio, folio);
	/*
	 * Please do not reorder this without considering how mm/ksm.c's
	 * get_ksm_page() depends upon ksm_migrate_page() and PageSwapCache().
	 */
	if (folio_test_swapcache(folio))
		folio_clear_swapcache(folio);
	folio_clear_private(folio);

	/* page->private contains hugetlb specific flags */
	if (!folio_test_hugetlb(folio))
		folio->private = NULL;

	/*
	 * If any waiters have accumulated on the new page then
	 * wake them up.
	 */
	if (folio_test_writeback(newfolio))
		folio_end_writeback(newfolio);

	/*
	 * PG_readahead shares the same bit with PG_reclaim.  The above
	 * end_page_writeback() may clear PG_readahead mistakenly, so set the
	 * bit after that.
	 */
	if (folio_test_readahead(folio))
		folio_set_readahead(newfolio);

	folio_copy_owner(newfolio, folio);

	if (!folio_test_hugetlb(folio))
		mem_cgroup_migrate(folio, newfolio);
}
EXPORT_SYMBOL(folio_migrate_flags);

void folio_migrate_copy(struct folio *newfolio, struct folio *folio)
{
	folio_copy(newfolio, folio);
	folio_migrate_flags(newfolio, folio);
}
EXPORT_SYMBOL(folio_migrate_copy);

/************************************************************
 *                    Migration functions
 ***********************************************************/

/**
 * migrate_folio() - Simple folio migration.
 * @mapping: The address_space containing the folio.
 * @dst: The folio to migrate the data to.
 * @src: The folio containing the current data.
 * @mode: How to migrate the page.
 *
 * Common logic to directly migrate a single LRU folio suitable for
 * folios that do not use PagePrivate/PagePrivate2.
 *
 * Folios are locked upon entry and exit.
 */
int migrate_folio(struct address_space *mapping, struct folio *dst,
		struct folio *src, enum migrate_mode mode)
{
	int rc;

	BUG_ON(folio_test_writeback(src));	/* Writeback must be complete */

	rc = folio_migrate_mapping(mapping, dst, src, 0);

	if (rc != MIGRATEPAGE_SUCCESS)
		return rc;

	if (mode != MIGRATE_SYNC_NO_COPY)
		folio_migrate_copy(dst, src);
	else
		folio_migrate_flags(dst, src);
	return MIGRATEPAGE_SUCCESS;
}
EXPORT_SYMBOL(migrate_folio);

#ifdef CONFIG_BLOCK
/* Returns true if all buffers are successfully locked */
static bool buffer_migrate_lock_buffers(struct buffer_head *head,
							enum migrate_mode mode)
{
	struct buffer_head *bh = head;

	/* Simple case, sync compaction */
	if (mode != MIGRATE_ASYNC) {
		do {
			lock_buffer(bh);
			bh = bh->b_this_page;

		} while (bh != head);

		return true;
	}

	/* async case, we cannot block on lock_buffer so use trylock_buffer */
	do {
		if (!trylock_buffer(bh)) {
			/*
			 * We failed to lock the buffer and cannot stall in
			 * async migration. Release the taken locks
			 */
			struct buffer_head *failed_bh = bh;
			bh = head;
			while (bh != failed_bh) {
				unlock_buffer(bh);
				bh = bh->b_this_page;
			}
			return false;
		}

		bh = bh->b_this_page;
	} while (bh != head);
	return true;
}

static int __buffer_migrate_folio(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode,
		bool check_refs)
{
	struct buffer_head *bh, *head;
	int rc;
	int expected_count;

	head = folio_buffers(src);
	if (!head)
		return migrate_folio(mapping, dst, src, mode);

	/* Check whether page does not have extra refs before we do more work */
	expected_count = folio_expected_refs(mapping, src);
	if (folio_ref_count(src) != expected_count)
		return -EAGAIN;

	if (!buffer_migrate_lock_buffers(head, mode))
		return -EAGAIN;

	if (check_refs) {
		bool busy;
		bool invalidated = false;

recheck_buffers:
		busy = false;
		spin_lock(&mapping->private_lock);
		bh = head;
		do {
			if (atomic_read(&bh->b_count)) {
				busy = true;
				break;
			}
			bh = bh->b_this_page;
		} while (bh != head);
		if (busy) {
			if (invalidated) {
				rc = -EAGAIN;
				goto unlock_buffers;
			}
			spin_unlock(&mapping->private_lock);
			invalidate_bh_lrus();
			invalidated = true;
			goto recheck_buffers;
		}
	}

	rc = folio_migrate_mapping(mapping, dst, src, 0);
	if (rc != MIGRATEPAGE_SUCCESS)
		goto unlock_buffers;

	folio_attach_private(dst, folio_detach_private(src));

	bh = head;
	do {
		set_bh_page(bh, &dst->page, bh_offset(bh));
		bh = bh->b_this_page;
	} while (bh != head);

	if (mode != MIGRATE_SYNC_NO_COPY)
		folio_migrate_copy(dst, src);
	else
		folio_migrate_flags(dst, src);

	rc = MIGRATEPAGE_SUCCESS;
unlock_buffers:
	if (check_refs)
		spin_unlock(&mapping->private_lock);
	bh = head;
	do {
		unlock_buffer(bh);
		bh = bh->b_this_page;
	} while (bh != head);

	return rc;
}

/**
 * buffer_migrate_folio() - Migration function for folios with buffers.
 * @mapping: The address space containing @src.
 * @dst: The folio to migrate to.
 * @src: The folio to migrate from.
 * @mode: How to migrate the folio.
 *
 * This function can only be used if the underlying filesystem guarantees
 * that no other references to @src exist. For example attached buffer
 * heads are accessed only under the folio lock.  If your filesystem cannot
 * provide this guarantee, buffer_migrate_folio_norefs() may be more
 * appropriate.
 *
 * Return: 0 on success or a negative errno on failure.
 */
int buffer_migrate_folio(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode)
{
	return __buffer_migrate_folio(mapping, dst, src, mode, false);
}
EXPORT_SYMBOL(buffer_migrate_folio);

/**
 * buffer_migrate_folio_norefs() - Migration function for folios with buffers.
 * @mapping: The address space containing @src.
 * @dst: The folio to migrate to.
 * @src: The folio to migrate from.
 * @mode: How to migrate the folio.
 *
 * Like buffer_migrate_folio() except that this variant is more careful
 * and checks that there are also no buffer head references. This function
 * is the right one for mappings where buffer heads are directly looked
 * up and referenced (such as block device mappings).
 *
 * Return: 0 on success or a negative errno on failure.
 */
int buffer_migrate_folio_norefs(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode)
{
	return __buffer_migrate_folio(mapping, dst, src, mode, true);
}
#endif

int filemap_migrate_folio(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode)
{
	int ret;

	ret = folio_migrate_mapping(mapping, dst, src, 0);
	if (ret != MIGRATEPAGE_SUCCESS)
		return ret;

	if (folio_get_private(src))
		folio_attach_private(dst, folio_detach_private(src));

	if (mode != MIGRATE_SYNC_NO_COPY)
		folio_migrate_copy(dst, src);
	else
		folio_migrate_flags(dst, src);
	return MIGRATEPAGE_SUCCESS;
}
EXPORT_SYMBOL_GPL(filemap_migrate_folio);

/*
 * Writeback a folio to clean the dirty state
 */
static int writeout(struct address_space *mapping, struct folio *folio)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = 1,
		.range_start = 0,
		.range_end = LLONG_MAX,
		.for_reclaim = 1
	};
	int rc;

	if (!mapping->a_ops->writepage)
		/* No write method for the address space */
		return -EINVAL;

	if (!folio_clear_dirty_for_io(folio))
		/* Someone else already triggered a write */
		return -EAGAIN;

	/*
	 * A dirty folio may imply that the underlying filesystem has
	 * the folio on some queue. So the folio must be clean for
	 * migration. Writeout may mean we lose the lock and the
	 * folio state is no longer what we checked for earlier.
	 * At this point we know that the migration attempt cannot
	 * be successful.
	 */
	remove_migration_ptes(folio, folio, false);

	rc = mapping->a_ops->writepage(&folio->page, &wbc);

	if (rc != AOP_WRITEPAGE_ACTIVATE)
		/* unlocked. Relock */
		folio_lock(folio);

	return (rc < 0) ? -EIO : -EAGAIN;
}

/*
 * Default handling if a filesystem does not provide a migration function.
 */
static int fallback_migrate_folio(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode)
{
	if (folio_test_dirty(src)) {
		/* Only writeback folios in full synchronous migration */
		switch (mode) {
		case MIGRATE_SYNC:
		case MIGRATE_SYNC_NO_COPY:
			break;
		default:
			return -EBUSY;
		}
		return writeout(mapping, src);
	}

	/*
	 * Buffers may be managed in a filesystem specific way.
	 * We must have no buffers or drop them.
	 */
	if (folio_test_private(src) &&
	    !filemap_release_folio(src, GFP_KERNEL))
		return mode == MIGRATE_SYNC ? -EAGAIN : -EBUSY;

	return migrate_folio(mapping, dst, src, mode);
}

/*
 * Move a page to a newly allocated page
 * The page is locked and all ptes have been successfully removed.
 *
 * The new page will have replaced the old page if this function
 * is successful.
 *
 * Return value:
 *   < 0 - error code
 *  MIGRATEPAGE_SUCCESS - success
 */
static int move_to_new_folio(struct folio *dst, struct folio *src,
				enum migrate_mode mode)
{
	int rc = -EAGAIN;
	bool is_lru = !__PageMovable(&src->page);

	VM_BUG_ON_FOLIO(!folio_test_locked(src), src);
	VM_BUG_ON_FOLIO(!folio_test_locked(dst), dst);

	if (likely(is_lru)) {
		struct address_space *mapping = folio_mapping(src);

		if (!mapping)
			rc = migrate_folio(mapping, dst, src, mode);
		else if (mapping->a_ops->migrate_folio)
			/*
			 * Most folios have a mapping and most filesystems
			 * provide a migrate_folio callback. Anonymous folios
			 * are part of swap space which also has its own
			 * migrate_folio callback. This is the most common path
			 * for page migration.
			 */
			rc = mapping->a_ops->migrate_folio(mapping, dst, src,
								mode);
		else
			rc = fallback_migrate_folio(mapping, dst, src, mode);
	} else {
		const struct movable_operations *mops;

		/*
		 * In case of non-lru page, it could be released after
		 * isolation step. In that case, we shouldn't try migration.
		 */
		VM_BUG_ON_FOLIO(!folio_test_isolated(src), src);
		if (!folio_test_movable(src)) {
			rc = MIGRATEPAGE_SUCCESS;
			folio_clear_isolated(src);
			goto out;
		}

		mops = page_movable_ops(&src->page);
		rc = mops->migrate_page(&dst->page, &src->page, mode);
		WARN_ON_ONCE(rc == MIGRATEPAGE_SUCCESS &&
				!folio_test_isolated(src));
	}

	/*
	 * When successful, old pagecache src->mapping must be cleared before
	 * src is freed; but stats require that PageAnon be left as PageAnon.
	 */
	if (rc == MIGRATEPAGE_SUCCESS) {
		if (__PageMovable(&src->page)) {
			VM_BUG_ON_FOLIO(!folio_test_isolated(src), src);

			/*
			 * We clear PG_movable under page_lock so any compactor
			 * cannot try to migrate this page.
			 */
			folio_clear_isolated(src);
		}

		/*
		 * Anonymous and movable src->mapping will be cleared by
		 * free_pages_prepare so don't reset it here for keeping
		 * the type to work PageAnon, for example.
		 */
		if (!folio_mapping_flags(src))
			src->mapping = NULL;

		if (likely(!folio_is_zone_device(dst)))
			flush_dcache_folio(dst);
	}
out:
	return rc;
}

static int __unmap_and_move(struct folio *src, struct folio *dst,
				int force, enum migrate_mode mode)
{
	int rc = -EAGAIN;
	bool page_was_mapped = false;
	struct anon_vma *anon_vma = NULL;
	bool is_lru = !__PageMovable(&src->page);

	if (!folio_trylock(src)) {
		if (!force || mode == MIGRATE_ASYNC)
			goto out;

		/*
		 * It's not safe for direct compaction to call lock_page.
		 * For example, during page readahead pages are added locked
		 * to the LRU. Later, when the IO completes the pages are
		 * marked uptodate and unlocked. However, the queueing
		 * could be merging multiple pages for one bio (e.g.
		 * mpage_readahead). If an allocation happens for the
		 * second or third page, the process can end up locking
		 * the same page twice and deadlocking. Rather than
		 * trying to be clever about what pages can be locked,
		 * avoid the use of lock_page for direct compaction
		 * altogether.
		 */
		if (current->flags & PF_MEMALLOC)
			goto out;

		folio_lock(src);
	}

	if (folio_test_writeback(src)) {
		/*
		 * Only in the case of a full synchronous migration is it
		 * necessary to wait for PageWriteback. In the async case,
		 * the retry loop is too short and in the sync-light case,
		 * the overhead of stalling is too much
		 */
		switch (mode) {
		case MIGRATE_SYNC:
		case MIGRATE_SYNC_NO_COPY:
			break;
		default:
			rc = -EBUSY;
			goto out_unlock;
		}
		if (!force)
			goto out_unlock;
		folio_wait_writeback(src);
	}

	/*
	 * By try_to_migrate(), src->mapcount goes down to 0 here. In this case,
	 * we cannot notice that anon_vma is freed while we migrate a page.
	 * This get_anon_vma() delays freeing anon_vma pointer until the end
	 * of migration. File cache pages are no problem because of page_lock()
	 * File Caches may use write_page() or lock_page() in migration, then,
	 * just care Anon page here.
	 *
	 * Only folio_get_anon_vma() understands the subtleties of
	 * getting a hold on an anon_vma from outside one of its mms.
	 * But if we cannot get anon_vma, then we won't need it anyway,
	 * because that implies that the anon page is no longer mapped
	 * (and cannot be remapped so long as we hold the page lock).
	 */
	if (folio_test_anon(src) && !folio_test_ksm(src))
		anon_vma = folio_get_anon_vma(src);

	/*
	 * Block others from accessing the new page when we get around to
	 * establishing additional references. We are usually the only one
	 * holding a reference to dst at this point. We used to have a BUG
	 * here if folio_trylock(dst) fails, but would like to allow for
	 * cases where there might be a race with the previous use of dst.
	 * This is much like races on refcount of oldpage: just don't BUG().
	 */
	if (unlikely(!folio_trylock(dst)))
		goto out_unlock;

	if (unlikely(!is_lru)) {
		rc = move_to_new_folio(dst, src, mode);
		goto out_unlock_both;
	}

	/*
	 * Corner case handling:
	 * 1. When a new swap-cache page is read into, it is added to the LRU
	 * and treated as swapcache but it has no rmap yet.
	 * Calling try_to_unmap() against a src->mapping==NULL page will
	 * trigger a BUG.  So handle it here.
	 * 2. An orphaned page (see truncate_cleanup_page) might have
	 * fs-private metadata. The page can be picked up due to memory
	 * offlining.  Everywhere else except page reclaim, the page is
	 * invisible to the vm, so the page can not be migrated.  So try to
	 * free the metadata, so the page can be freed.
	 */
	if (!src->mapping) {
		if (folio_test_private(src)) {
			try_to_free_buffers(src);
			goto out_unlock_both;
		}
	} else if (folio_mapped(src)) {
		/* Establish migration ptes */
		VM_BUG_ON_FOLIO(folio_test_anon(src) &&
			       !folio_test_ksm(src) && !anon_vma, src);
		try_to_migrate(src, 0);
		page_was_mapped = true;
	}

	if (!folio_mapped(src))
		rc = move_to_new_folio(dst, src, mode);

	/*
	 * When successful, push dst to LRU immediately: so that if it
	 * turns out to be an mlocked page, remove_migration_ptes() will
	 * automatically build up the correct dst->mlock_count for it.
	 *
	 * We would like to do something similar for the old page, when
	 * unsuccessful, and other cases when a page has been temporarily
	 * isolated from the unevictable LRU: but this case is the easiest.
	 */
	if (rc == MIGRATEPAGE_SUCCESS) {
		folio_add_lru(dst);
		if (page_was_mapped)
			lru_add_drain();
	}

	if (page_was_mapped)
		remove_migration_ptes(src,
			rc == MIGRATEPAGE_SUCCESS ? dst : src, false);

out_unlock_both:
	folio_unlock(dst);
out_unlock:
	/* Drop an anon_vma reference if we took one */
	if (anon_vma)
		put_anon_vma(anon_vma);
	folio_unlock(src);
out:
	/*
	 * If migration is successful, decrease refcount of dst,
	 * which will not free the page because new page owner increased
	 * refcounter.
	 */
	if (rc == MIGRATEPAGE_SUCCESS)
		folio_put(dst);

	return rc;
}

/*
 * Obtain the lock on page, remove all ptes and migrate the page
 * to the newly allocated page in newpage.
 */
static int unmap_and_move(new_page_t get_new_page,
				   free_page_t put_new_page,
				   unsigned long private, struct page *page,
				   int force, enum migrate_mode mode,
				   enum migrate_reason reason,
				   struct list_head *ret)
{
	struct folio *dst, *src = page_folio(page);
	int rc = MIGRATEPAGE_SUCCESS;
	struct page *newpage = NULL;

	if (!thp_migration_supported() && PageTransHuge(page))
		return -ENOSYS;

	if (page_count(page) == 1) {
		/* Page was freed from under us. So we are done. */
		ClearPageActive(page);
		ClearPageUnevictable(page);
		/* free_pages_prepare() will clear PG_isolated. */
		goto out;
	}

	newpage = get_new_page(page, private);
	if (!newpage)
		return -ENOMEM;
	dst = page_folio(newpage);

	newpage->private = 0;
	rc = __unmap_and_move(src, dst, force, mode);
	if (rc == MIGRATEPAGE_SUCCESS)
		set_page_owner_migrate_reason(newpage, reason);

out:
	if (rc != -EAGAIN) {
		/*
		 * A page that has been migrated has all references
		 * removed and will be freed. A page that has not been
		 * migrated will have kept its references and be restored.
		 */
		list_del(&page->lru);
	}

	/*
	 * If migration is successful, releases reference grabbed during
	 * isolation. Otherwise, restore the page to right list unless
	 * we want to retry.
	 */
	if (rc == MIGRATEPAGE_SUCCESS) {
		/*
		 * Compaction can migrate also non-LRU pages which are
		 * not accounted to NR_ISOLATED_*. They can be recognized
		 * as __PageMovable
		 */
		if (likely(!__PageMovable(page)))
			mod_node_page_state(page_pgdat(page), NR_ISOLATED_ANON +
					page_is_file_lru(page), -thp_nr_pages(page));

		if (reason != MR_MEMORY_FAILURE)
			/*
			 * We release the page in page_handle_poison.
			 */
			put_page(page);
	} else {
		if (rc != -EAGAIN)
			list_add_tail(&page->lru, ret);

		if (put_new_page)
			put_new_page(newpage, private);
		else
			put_page(newpage);
	}

	return rc;
}

/*
 * Counterpart of unmap_and_move_page() for hugepage migration.
 *
 * This function doesn't wait the completion of hugepage I/O
 * because there is no race between I/O and migration for hugepage.
 * Note that currently hugepage I/O occurs only in direct I/O
 * where no lock is held and PG_writeback is irrelevant,
 * and writeback status of all subpages are counted in the reference
 * count of the head page (i.e. if all subpages of a 2MB hugepage are
 * under direct I/O, the reference of the head page is 512 and a bit more.)
 * This means that when we try to migrate hugepage whose subpages are
 * doing direct I/O, some references remain after try_to_unmap() and
 * hugepage migration fails without data corruption.
 *
 * There is also no race when direct I/O is issued on the page under migration,
 * because then pte is replaced with migration swap entry and direct I/O code
 * will wait in the page fault for migration to complete.
 */
static int unmap_and_move_huge_page(new_page_t get_new_page,
				free_page_t put_new_page, unsigned long private,
				struct page *hpage, int force,
				enum migrate_mode mode, int reason,
				struct list_head *ret)
{
	struct folio *dst, *src = page_folio(hpage);
	int rc = -EAGAIN;
	int page_was_mapped = 0;
	struct page *new_hpage;
	struct anon_vma *anon_vma = NULL;
	struct address_space *mapping = NULL;

	/*
	 * Migratability of hugepages depends on architectures and their size.
	 * This check is necessary because some callers of hugepage migration
	 * like soft offline and memory hotremove don't walk through page
	 * tables or check whether the hugepage is pmd-based or not before
	 * kicking migration.
	 */
	if (!hugepage_migration_supported(page_hstate(hpage)))
		return -ENOSYS;

	if (folio_ref_count(src) == 1) {
		/* page was freed from under us. So we are done. */
		putback_active_hugepage(hpage);
		return MIGRATEPAGE_SUCCESS;
	}

	new_hpage = get_new_page(hpage, private);
	if (!new_hpage)
		return -ENOMEM;
	dst = page_folio(new_hpage);

	if (!folio_trylock(src)) {
		if (!force)
			goto out;
		switch (mode) {
		case MIGRATE_SYNC:
		case MIGRATE_SYNC_NO_COPY:
			break;
		default:
			goto out;
		}
		folio_lock(src);
	}

	/*
	 * Check for pages which are in the process of being freed.  Without
	 * folio_mapping() set, hugetlbfs specific move page routine will not
	 * be called and we could leak usage counts for subpools.
	 */
	if (hugetlb_page_subpool(hpage) && !folio_mapping(src)) {
		rc = -EBUSY;
		goto out_unlock;
	}

	if (folio_test_anon(src))
		anon_vma = folio_get_anon_vma(src);

	if (unlikely(!folio_trylock(dst)))
		goto put_anon;

	if (folio_mapped(src)) {
		enum ttu_flags ttu = 0;

		if (!folio_test_anon(src)) {
			/*
			 * In shared mappings, try_to_unmap could potentially
			 * call huge_pmd_unshare.  Because of this, take
			 * semaphore in write mode here and set TTU_RMAP_LOCKED
			 * to let lower levels know we have taken the lock.
			 */
			mapping = hugetlb_page_mapping_lock_write(hpage);
			if (unlikely(!mapping))
				goto unlock_put_anon;

			ttu = TTU_RMAP_LOCKED;
		}

		try_to_migrate(src, ttu);
		page_was_mapped = 1;

		if (ttu & TTU_RMAP_LOCKED)
			i_mmap_unlock_write(mapping);
	}

	if (!folio_mapped(src))
		rc = move_to_new_folio(dst, src, mode);

	if (page_was_mapped)
		remove_migration_ptes(src,
			rc == MIGRATEPAGE_SUCCESS ? dst : src, false);

unlock_put_anon:
	folio_unlock(dst);

put_anon:
	if (anon_vma)
		put_anon_vma(anon_vma);

	if (rc == MIGRATEPAGE_SUCCESS) {
		move_hugetlb_state(hpage, new_hpage, reason);
		put_new_page = NULL;
	}

out_unlock:
	folio_unlock(src);
out:
	if (rc == MIGRATEPAGE_SUCCESS)
		putback_active_hugepage(hpage);
	else if (rc != -EAGAIN)
		list_move_tail(&src->lru, ret);

	/*
	 * If migration was not successful and there's a freeing callback, use
	 * it.  Otherwise, put_page() will drop the reference grabbed during
	 * isolation.
	 */
	if (put_new_page)
		put_new_page(new_hpage, private);
	else
		putback_active_hugepage(new_hpage);

	return rc;
}

static inline int try_split_thp(struct page *page, struct list_head *split_pages)
{
	int rc;

	lock_page(page);
	rc = split_huge_page_to_list(page, split_pages);
	unlock_page(page);
	if (!rc)
		list_move_tail(&page->lru, split_pages);

	return rc;
}

/*
 * migrate_pages - migrate the pages specified in a list, to the free pages
 *		   supplied as the target for the page migration
 *
 * @from:		The list of pages to be migrated.
 * @get_new_page:	The function used to allocate free pages to be used
 *			as the target of the page migration.
 * @put_new_page:	The function used to free target pages if migration
 *			fails, or NULL if no special handling is necessary.
 * @private:		Private data to be passed on to get_new_page()
 * @mode:		The migration mode that specifies the constraints for
 *			page migration, if any.
 * @reason:		The reason for page migration.
 * @ret_succeeded:	Set to the number of normal pages migrated successfully if
 *			the caller passes a non-NULL pointer.
 *
 * The function returns after 10 attempts or if no pages are movable any more
 * because the list has become empty or no retryable pages exist any more.
 * It is caller's responsibility to call putback_movable_pages() to return pages
 * to the LRU or free list only if ret != 0.
 *
 * Returns the number of {normal page, THP, hugetlb} that were not migrated, or
 * an error code. The number of THP splits will be considered as the number of
 * non-migrated THP, no matter how many subpages of the THP are migrated successfully.
 */
int migrate_pages(struct list_head *from, new_page_t get_new_page,
		free_page_t put_new_page, unsigned long private,
		enum migrate_mode mode, int reason, unsigned int *ret_succeeded)
{
	int retry = 1;
	int thp_retry = 1;
	int nr_failed = 0;
	int nr_failed_pages = 0;
	int nr_retry_pages = 0;
	int nr_succeeded = 0;
	int nr_thp_succeeded = 0;
	int nr_thp_failed = 0;
	int nr_thp_split = 0;
	int pass = 0;
	bool is_thp = false;
	struct page *page;
	struct page *page2;
	int rc, nr_subpages;
	LIST_HEAD(ret_pages);
	LIST_HEAD(thp_split_pages);
	bool nosplit = (reason == MR_NUMA_MISPLACED);
	bool no_subpage_counting = false;

	trace_mm_migrate_pages_start(mode, reason);

thp_subpage_migration:
	for (pass = 0; pass < 10 && (retry || thp_retry); pass++) {
		retry = 0;
		thp_retry = 0;
		nr_retry_pages = 0;

		list_for_each_entry_safe(page, page2, from, lru) {
			/*
			 * THP statistics is based on the source huge page.
			 * Capture required information that might get lost
			 * during migration.
			 */
			is_thp = PageTransHuge(page) && !PageHuge(page);
			nr_subpages = compound_nr(page);
			cond_resched();

			if (PageHuge(page))
				rc = unmap_and_move_huge_page(get_new_page,
						put_new_page, private, page,
						pass > 2, mode, reason,
						&ret_pages);
			else
				rc = unmap_and_move(get_new_page, put_new_page,
						private, page, pass > 2, mode,
						reason, &ret_pages);
			/*
			 * The rules are:
			 *	Success: non hugetlb page will be freed, hugetlb
			 *		 page will be put back
			 *	-EAGAIN: stay on the from list
			 *	-ENOMEM: stay on the from list
			 *	-ENOSYS: stay on the from list
			 *	Other errno: put on ret_pages list then splice to
			 *		     from list
			 */
			switch(rc) {
			/*
			 * THP migration might be unsupported or the
			 * allocation could've failed so we should
			 * retry on the same page with the THP split
			 * to base pages.
			 *
			 * Sub-pages are put in thp_split_pages, and
			 * we will migrate them after the rest of the
			 * list is processed.
			 */
			case -ENOSYS:
				/* THP migration is unsupported */
				if (is_thp) {
					nr_thp_failed++;
					if (!try_split_thp(page, &thp_split_pages)) {
						nr_thp_split++;
						break;
					}
				/* Hugetlb migration is unsupported */
				} else if (!no_subpage_counting) {
					nr_failed++;
				}

				nr_failed_pages += nr_subpages;
				list_move_tail(&page->lru, &ret_pages);
				break;
			case -ENOMEM:
				/*
				 * When memory is low, don't bother to try to migrate
				 * other pages, just exit.
				 */
				if (is_thp) {
					nr_thp_failed++;
					/* THP NUMA faulting doesn't split THP to retry. */
					if (!nosplit && !try_split_thp(page, &thp_split_pages)) {
						nr_thp_split++;
						break;
					}
				} else if (!no_subpage_counting) {
					nr_failed++;
				}

				nr_failed_pages += nr_subpages + nr_retry_pages;
				/*
				 * There might be some subpages of fail-to-migrate THPs
				 * left in thp_split_pages list. Move them back to migration
				 * list so that they could be put back to the right list by
				 * the caller otherwise the page refcnt will be leaked.
				 */
				list_splice_init(&thp_split_pages, from);
				/* nr_failed isn't updated for not used */
				nr_thp_failed += thp_retry;
				goto out;
			case -EAGAIN:
				if (is_thp)
					thp_retry++;
				else if (!no_subpage_counting)
					retry++;
				nr_retry_pages += nr_subpages;
				break;
			case MIGRATEPAGE_SUCCESS:
				nr_succeeded += nr_subpages;
				if (is_thp)
					nr_thp_succeeded++;
				break;
			default:
				/*
				 * Permanent failure (-EBUSY, etc.):
				 * unlike -EAGAIN case, the failed page is
				 * removed from migration page list and not
				 * retried in the next outer loop.
				 */
				if (is_thp)
					nr_thp_failed++;
				else if (!no_subpage_counting)
					nr_failed++;

				nr_failed_pages += nr_subpages;
				break;
			}
		}
	}
	nr_failed += retry;
	nr_thp_failed += thp_retry;
	nr_failed_pages += nr_retry_pages;
	/*
	 * Try to migrate subpages of fail-to-migrate THPs, no nr_failed
	 * counting in this round, since all subpages of a THP is counted
	 * as 1 failure in the first round.
	 */
	if (!list_empty(&thp_split_pages)) {
		/*
		 * Move non-migrated pages (after 10 retries) to ret_pages
		 * to avoid migrating them again.
		 */
		list_splice_init(from, &ret_pages);
		list_splice_init(&thp_split_pages, from);
		no_subpage_counting = true;
		retry = 1;
		goto thp_subpage_migration;
	}

	rc = nr_failed + nr_thp_failed;
out:
	/*
	 * Put the permanent failure page back to migration list, they
	 * will be put back to the right list by the caller.
	 */
	list_splice(&ret_pages, from);

	count_vm_events(PGMIGRATE_SUCCESS, nr_succeeded);
	count_vm_events(PGMIGRATE_FAIL, nr_failed_pages);
	count_vm_events(THP_MIGRATION_SUCCESS, nr_thp_succeeded);
	count_vm_events(THP_MIGRATION_FAIL, nr_thp_failed);
	count_vm_events(THP_MIGRATION_SPLIT, nr_thp_split);
	trace_mm_migrate_pages(nr_succeeded, nr_failed_pages, nr_thp_succeeded,
			       nr_thp_failed, nr_thp_split, mode, reason);

	if (ret_succeeded)
		*ret_succeeded = nr_succeeded;

	return rc;
}

struct page *alloc_migration_target(struct page *page, unsigned long private)
{
	struct folio *folio = page_folio(page);
	struct migration_target_control *mtc;
	gfp_t gfp_mask;
	unsigned int order = 0;
	struct folio *new_folio = NULL;
	int nid;
	int zidx;

	mtc = (struct migration_target_control *)private;
	gfp_mask = mtc->gfp_mask;
	nid = mtc->nid;
	if (nid == NUMA_NO_NODE)
		nid = folio_nid(folio);

	if (folio_test_hugetlb(folio)) {
		struct hstate *h = page_hstate(&folio->page);

		gfp_mask = htlb_modify_alloc_mask(h, gfp_mask);
		return alloc_huge_page_nodemask(h, nid, mtc->nmask, gfp_mask);
	}

	if (folio_test_large(folio)) {
		/*
		 * clear __GFP_RECLAIM to make the migration callback
		 * consistent with regular THP allocations.
		 */
		gfp_mask &= ~__GFP_RECLAIM;
		gfp_mask |= GFP_TRANSHUGE;
		order = folio_order(folio);
	}
	zidx = zone_idx(folio_zone(folio));
	if (is_highmem_idx(zidx) || zidx == ZONE_MOVABLE)
		gfp_mask |= __GFP_HIGHMEM;

	new_folio = __folio_alloc(gfp_mask, order, nid, mtc->nmask);

	return &new_folio->page;
}

#ifdef CONFIG_NUMA

static int store_status(int __user *status, int start, int value, int nr)
{
	while (nr-- > 0) {
		if (put_user(value, status + start))
			return -EFAULT;
		start++;
	}

	return 0;
}

static int do_move_pages_to_node(struct mm_struct *mm,
		struct list_head *pagelist, int node)
{
	int err;
	struct migration_target_control mtc = {
		.nid = node,
		.gfp_mask = GFP_HIGHUSER_MOVABLE | __GFP_THISNODE,
	};

	err = migrate_pages(pagelist, alloc_migration_target, NULL,
		(unsigned long)&mtc, MIGRATE_SYNC, MR_SYSCALL, NULL);
	if (err)
		putback_movable_pages(pagelist);
	return err;
}

/*
 * Resolves the given address to a struct page, isolates it from the LRU and
 * puts it to the given pagelist.
 * Returns:
 *     errno - if the page cannot be found/isolated
 *     0 - when it doesn't have to be migrated because it is already on the
 *         target node
 *     1 - when it has been queued
 */
static int add_page_for_migration(struct mm_struct *mm, unsigned long addr,
		int node, struct list_head *pagelist, bool migrate_all)
{
	struct vm_area_struct *vma;
	struct page *page;
	int err;

	mmap_read_lock(mm);
	err = -EFAULT;
	vma = vma_lookup(mm, addr);
	if (!vma || !vma_migratable(vma))
		goto out;

	/* FOLL_DUMP to ignore special (like zero) pages */
	page = follow_page(vma, addr, FOLL_GET | FOLL_DUMP);

	err = PTR_ERR(page);
	if (IS_ERR(page))
		goto out;

	err = -ENOENT;
	if (!page)
		goto out;

	if (is_zone_device_page(page))
		goto out_putpage;

	err = 0;
	if (page_to_nid(page) == node)
		goto out_putpage;

	err = -EACCES;
	if (page_mapcount(page) > 1 && !migrate_all)
		goto out_putpage;

	if (PageHuge(page)) {
		if (PageHead(page)) {
			err = isolate_hugetlb(page, pagelist);
			if (!err)
				err = 1;
		}
	} else {
		struct page *head;

		head = compound_head(page);
		err = isolate_lru_page(head);
		if (err)
			goto out_putpage;

		err = 1;
		list_add_tail(&head->lru, pagelist);
		mod_node_page_state(page_pgdat(head),
			NR_ISOLATED_ANON + page_is_file_lru(head),
			thp_nr_pages(head));
	}
out_putpage:
	/*
	 * Either remove the duplicate refcount from
	 * isolate_lru_page() or drop the page ref if it was
	 * not isolated.
	 */
	put_page(page);
out:
	mmap_read_unlock(mm);
	return err;
}

static int move_pages_and_store_status(struct mm_struct *mm, int node,
		struct list_head *pagelist, int __user *status,
		int start, int i, unsigned long nr_pages)
{
	int err;

	if (list_empty(pagelist))
		return 0;

	err = do_move_pages_to_node(mm, pagelist, node);
	if (err) {
		/*
		 * Positive err means the number of failed
		 * pages to migrate.  Since we are going to
		 * abort and return the number of non-migrated
		 * pages, so need to include the rest of the
		 * nr_pages that have not been attempted as
		 * well.
		 */
		if (err > 0)
			err += nr_pages - i;
		return err;
	}
	return store_status(status, start, node, i - start);
}

/*
 * Migrate an array of page address onto an array of nodes and fill
 * the corresponding array of status.
 */
static int do_pages_move(struct mm_struct *mm, nodemask_t task_nodes,
			 unsigned long nr_pages,
			 const void __user * __user *pages,
			 const int __user *nodes,
			 int __user *status, int flags)
{
	int current_node = NUMA_NO_NODE;
	LIST_HEAD(pagelist);
	int start, i;
	int err = 0, err1;

	lru_cache_disable();

	for (i = start = 0; i < nr_pages; i++) {
		const void __user *p;
		unsigned long addr;
		int node;

		err = -EFAULT;
		if (get_user(p, pages + i))
			goto out_flush;
		if (get_user(node, nodes + i))
			goto out_flush;
		addr = (unsigned long)untagged_addr(p);

		err = -ENODEV;
		if (node < 0 || node >= MAX_NUMNODES)
			goto out_flush;
		if (!node_state(node, N_MEMORY))
			goto out_flush;

		err = -EACCES;
		if (!node_isset(node, task_nodes))
			goto out_flush;

		if (current_node == NUMA_NO_NODE) {
			current_node = node;
			start = i;
		} else if (node != current_node) {
			err = move_pages_and_store_status(mm, current_node,
					&pagelist, status, start, i, nr_pages);
			if (err)
				goto out;
			start = i;
			current_node = node;
		}

		/*
		 * Errors in the page lookup or isolation are not fatal and we simply
		 * report them via status
		 */
		err = add_page_for_migration(mm, addr, current_node,
				&pagelist, flags & MPOL_MF_MOVE_ALL);

		if (err > 0) {
			/* The page is successfully queued for migration */
			continue;
		}

		/*
		 * The move_pages() man page does not have an -EEXIST choice, so
		 * use -EFAULT instead.
		 */
		if (err == -EEXIST)
			err = -EFAULT;

		/*
		 * If the page is already on the target node (!err), store the
		 * node, otherwise, store the err.
		 */
		err = store_status(status, i, err ? : current_node, 1);
		if (err)
			goto out_flush;

		err = move_pages_and_store_status(mm, current_node, &pagelist,
				status, start, i, nr_pages);
		if (err) {
			/* We have accounted for page i */
			if (err > 0)
				err--;
			goto out;
		}
		current_node = NUMA_NO_NODE;
	}
out_flush:
	/* Make sure we do not overwrite the existing error */
	err1 = move_pages_and_store_status(mm, current_node, &pagelist,
				status, start, i, nr_pages);
	if (err >= 0)
		err = err1;
out:
	lru_cache_enable();
	return err;
}

/*
 * Determine the nodes of an array of pages and store it in an array of status.
 */
static void do_pages_stat_array(struct mm_struct *mm, unsigned long nr_pages,
				const void __user **pages, int *status)
{
	unsigned long i;

	mmap_read_lock(mm);

	for (i = 0; i < nr_pages; i++) {
		unsigned long addr = (unsigned long)(*pages);
		unsigned int foll_flags = FOLL_DUMP;
		struct vm_area_struct *vma;
		struct page *page;
		int err = -EFAULT;

		vma = vma_lookup(mm, addr);
		if (!vma)
			goto set_status;

		/* Not all huge page follow APIs support 'FOLL_GET' */
		if (!is_vm_hugetlb_page(vma))
			foll_flags |= FOLL_GET;

		/* FOLL_DUMP to ignore special (like zero) pages */
		page = follow_page(vma, addr, foll_flags);

		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto set_status;

		err = -ENOENT;
		if (!page)
			goto set_status;

		if (!is_zone_device_page(page))
			err = page_to_nid(page);

		if (foll_flags & FOLL_GET)
			put_page(page);
set_status:
		*status = err;

		pages++;
		status++;
	}

	mmap_read_unlock(mm);
}

static int get_compat_pages_array(const void __user *chunk_pages[],
				  const void __user * __user *pages,
				  unsigned long chunk_nr)
{
	compat_uptr_t __user *pages32 = (compat_uptr_t __user *)pages;
	compat_uptr_t p;
	int i;

	for (i = 0; i < chunk_nr; i++) {
		if (get_user(p, pages32 + i))
			return -EFAULT;
		chunk_pages[i] = compat_ptr(p);
	}

	return 0;
}

/*
 * Determine the nodes of a user array of pages and store it in
 * a user array of status.
 */
static int do_pages_stat(struct mm_struct *mm, unsigned long nr_pages,
			 const void __user * __user *pages,
			 int __user *status)
{
#define DO_PAGES_STAT_CHUNK_NR 16UL
	const void __user *chunk_pages[DO_PAGES_STAT_CHUNK_NR];
	int chunk_status[DO_PAGES_STAT_CHUNK_NR];

	while (nr_pages) {
		unsigned long chunk_nr = min(nr_pages, DO_PAGES_STAT_CHUNK_NR);

		if (in_compat_syscall()) {
			if (get_compat_pages_array(chunk_pages, pages,
						   chunk_nr))
				break;
		} else {
			if (copy_from_user(chunk_pages, pages,
				      chunk_nr * sizeof(*chunk_pages)))
				break;
		}

		do_pages_stat_array(mm, chunk_nr, chunk_pages, chunk_status);

		if (copy_to_user(status, chunk_status, chunk_nr * sizeof(*status)))
			break;

		pages += chunk_nr;
		status += chunk_nr;
		nr_pages -= chunk_nr;
	}
	return nr_pages ? -EFAULT : 0;
}

static struct mm_struct *find_mm_struct(pid_t pid, nodemask_t *mem_nodes)
{
	struct task_struct *task;
	struct mm_struct *mm;

	/*
	 * There is no need to check if current process has the right to modify
	 * the specified process when they are same.
	 */
	if (!pid) {
		mmget(current->mm);
		*mem_nodes = cpuset_mems_allowed(current);
		return current->mm;
	}

	/* Find the mm_struct */
	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		rcu_read_unlock();
		return ERR_PTR(-ESRCH);
	}
	get_task_struct(task);

	/*
	 * Check if this process has the right to modify the specified
	 * process. Use the regular "ptrace_may_access()" checks.
	 */
	if (!ptrace_may_access(task, PTRACE_MODE_READ_REALCREDS)) {
		rcu_read_unlock();
		mm = ERR_PTR(-EPERM);
		goto out;
	}
	rcu_read_unlock();

	mm = ERR_PTR(security_task_movememory(task));
	if (IS_ERR(mm))
		goto out;
	*mem_nodes = cpuset_mems_allowed(task);
	mm = get_task_mm(task);
out:
	put_task_struct(task);
	if (!mm)
		mm = ERR_PTR(-EINVAL);
	return mm;
}

/*
 * Move a list of pages in the address space of the currently executing
 * process.
 */
static int kernel_move_pages(pid_t pid, unsigned long nr_pages,
			     const void __user * __user *pages,
			     const int __user *nodes,
			     int __user *status, int flags)
{
	struct mm_struct *mm;
	int err;
	nodemask_t task_nodes;

	/* Check flags */
	if (flags & ~(MPOL_MF_MOVE|MPOL_MF_MOVE_ALL))
		return -EINVAL;

	if ((flags & MPOL_MF_MOVE_ALL) && !capable(CAP_SYS_NICE))
		return -EPERM;

	mm = find_mm_struct(pid, &task_nodes);
	if (IS_ERR(mm))
		return PTR_ERR(mm);

	if (nodes)
		err = do_pages_move(mm, task_nodes, nr_pages, pages,
				    nodes, status, flags);
	else
		err = do_pages_stat(mm, nr_pages, pages, status);

	mmput(mm);
	return err;
}

SYSCALL_DEFINE6(move_pages, pid_t, pid, unsigned long, nr_pages,
		const void __user * __user *, pages,
		const int __user *, nodes,
		int __user *, status, int, flags)
{
	return kernel_move_pages(pid, nr_pages, pages, nodes, status, flags);
}

#ifdef CONFIG_NUMA_BALANCING
/*
 * Returns true if this is a safe migration target node for misplaced NUMA
 * pages. Currently it only checks the watermarks which is crude.
 */
static bool migrate_balanced_pgdat(struct pglist_data *pgdat,
				   unsigned long nr_migrate_pages)
{
	int z;

	for (z = pgdat->nr_zones - 1; z >= 0; z--) {
		struct zone *zone = pgdat->node_zones + z;

		if (!managed_zone(zone))
			continue;

		/* Avoid waking kswapd by allocating pages_to_migrate pages. */
		if (!zone_watermark_ok(zone, 0,
				       high_wmark_pages(zone) +
				       nr_migrate_pages,
				       ZONE_MOVABLE, 0))
			continue;
		return true;
	}
	return false;
}

static struct page *alloc_misplaced_dst_page(struct page *page,
					   unsigned long data)
{
	int nid = (int) data;
	int order = compound_order(page);
	gfp_t gfp = __GFP_THISNODE;
	struct folio *new;

	if (order > 0)
		gfp |= GFP_TRANSHUGE_LIGHT;
	else {
		gfp |= GFP_HIGHUSER_MOVABLE | __GFP_NOMEMALLOC | __GFP_NORETRY |
			__GFP_NOWARN;
		gfp &= ~__GFP_RECLAIM;
	}
	new = __folio_alloc_node(gfp, order, nid);

	return &new->page;
}

static int numamigrate_isolate_page(pg_data_t *pgdat, struct page *page)
{
	int nr_pages = thp_nr_pages(page);
	int order = compound_order(page);

	VM_BUG_ON_PAGE(order && !PageTransHuge(page), page);

	/* Do not migrate THP mapped by multiple processes */
	if (PageTransHuge(page) && total_mapcount(page) > 1)
		return 0;

	/* Avoid migrating to a node that is nearly full */
	if (!migrate_balanced_pgdat(pgdat, nr_pages)) {
		int z;

		if (!(sysctl_numa_balancing_mode & NUMA_BALANCING_MEMORY_TIERING))
			return 0;
		for (z = pgdat->nr_zones - 1; z >= 0; z--) {
			if (managed_zone(pgdat->node_zones + z))
				break;
		}
		wakeup_kswapd(pgdat->node_zones + z, 0, order, ZONE_MOVABLE);
		return 0;
	}

	if (isolate_lru_page(page))
		return 0;

	mod_node_page_state(page_pgdat(page), NR_ISOLATED_ANON + page_is_file_lru(page),
			    nr_pages);

	/*
	 * Isolating the page has taken another reference, so the
	 * caller's reference can be safely dropped without the page
	 * disappearing underneath us during migration.
	 */
	put_page(page);
	return 1;
}

/*
 * Attempt to migrate a misplaced page to the specified destination
 * node. Caller is expected to have an elevated reference count on
 * the page that will be dropped by this function before returning.
 */
int migrate_misplaced_page(struct page *page, struct vm_area_struct *vma,
			   int node)
{
	pg_data_t *pgdat = NODE_DATA(node);
	int isolated;
	int nr_remaining;
	unsigned int nr_succeeded;
	LIST_HEAD(migratepages);
	int nr_pages = thp_nr_pages(page);

	/*
	 * Don't migrate file pages that are mapped in multiple processes
	 * with execute permissions as they are probably shared libraries.
	 */
	if (page_mapcount(page) != 1 && page_is_file_lru(page) &&
	    (vma->vm_flags & VM_EXEC))
		goto out;

	/*
	 * Also do not migrate dirty pages as not all filesystems can move
	 * dirty pages in MIGRATE_ASYNC mode which is a waste of cycles.
	 */
	if (page_is_file_lru(page) && PageDirty(page))
		goto out;

	isolated = numamigrate_isolate_page(pgdat, page);
	if (!isolated)
		goto out;

	list_add(&page->lru, &migratepages);
	nr_remaining = migrate_pages(&migratepages, alloc_misplaced_dst_page,
				     NULL, node, MIGRATE_ASYNC,
				     MR_NUMA_MISPLACED, &nr_succeeded);
	if (nr_remaining) {
		if (!list_empty(&migratepages)) {
			list_del(&page->lru);
			mod_node_page_state(page_pgdat(page), NR_ISOLATED_ANON +
					page_is_file_lru(page), -nr_pages);
			putback_lru_page(page);
		}
		isolated = 0;
	}
	if (nr_succeeded) {
		count_vm_numa_events(NUMA_PAGE_MIGRATE, nr_succeeded);
		if (!node_is_toptier(page_to_nid(page)) && node_is_toptier(node))
			mod_node_page_state(pgdat, PGPROMOTE_SUCCESS,
					    nr_succeeded);
	}
	BUG_ON(!list_empty(&migratepages));
	return isolated;

out:
	put_page(page);
	return 0;
}
#endif /* CONFIG_NUMA_BALANCING */
#endif /* CONFIG_NUMA */
