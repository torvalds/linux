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

bool isolate_movable_page(struct page *page, isolate_mode_t mode)
{
	struct folio *folio = folio_get_nontail_page(page);
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
	if (!folio)
		goto out;

	if (unlikely(folio_test_slab(folio)))
		goto out_putfolio;
	/* Pairs with smp_wmb() in slab freeing, e.g. SLUB's __free_slab() */
	smp_rmb();
	/*
	 * Check movable flag before taking the page lock because
	 * we use non-atomic bitops on newly allocated page flags so
	 * unconditionally grabbing the lock ruins page's owner side.
	 */
	if (unlikely(!__folio_test_movable(folio)))
		goto out_putfolio;
	/* Pairs with smp_wmb() in slab allocation, e.g. SLUB's alloc_slab_page() */
	smp_rmb();
	if (unlikely(folio_test_slab(folio)))
		goto out_putfolio;

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
	if (unlikely(!folio_trylock(folio)))
		goto out_putfolio;

	if (!folio_test_movable(folio) || folio_test_isolated(folio))
		goto out_no_isolated;

	mops = folio_movable_ops(folio);
	VM_BUG_ON_FOLIO(!mops, folio);

	if (!mops->isolate_page(&folio->page, mode))
		goto out_no_isolated;

	/* Driver shouldn't use the isolated flag */
	WARN_ON_ONCE(folio_test_isolated(folio));
	folio_set_isolated(folio);
	folio_unlock(folio);

	return true;

out_no_isolated:
	folio_unlock(folio);
out_putfolio:
	folio_put(folio);
out:
	return false;
}

static void putback_movable_folio(struct folio *folio)
{
	const struct movable_operations *mops = folio_movable_ops(folio);

	mops->putback_page(&folio->page);
	folio_clear_isolated(folio);
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
	struct folio *folio;
	struct folio *folio2;

	list_for_each_entry_safe(folio, folio2, l, lru) {
		if (unlikely(folio_test_hugetlb(folio))) {
			folio_putback_active_hugetlb(folio);
			continue;
		}
		list_del(&folio->lru);
		/*
		 * We isolated non-lru movable folio so here we can use
		 * __folio_test_movable because LRU folio's mapping cannot
		 * have PAGE_MAPPING_MOVABLE.
		 */
		if (unlikely(__folio_test_movable(folio))) {
			VM_BUG_ON_FOLIO(!folio_test_isolated(folio), folio);
			folio_lock(folio);
			if (folio_test_movable(folio))
				putback_movable_folio(folio);
			else
				folio_clear_isolated(folio);
			folio_unlock(folio);
			folio_put(folio);
		} else {
			node_stat_mod_folio(folio, NR_ISOLATED_ANON +
					folio_is_file_lru(folio), -folio_nr_pages(folio));
			folio_putback_lru(folio);
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
		pte_t old_pte;
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
		old_pte = ptep_get(pvmw.pte);

		entry = pte_to_swp_entry(old_pte);
		if (!is_migration_entry_young(entry))
			pte = pte_mkold(pte);
		if (folio_test_dirty(folio) && is_migration_entry_dirty(entry))
			pte = pte_mkdirty(pte);
		if (pte_swp_soft_dirty(old_pte))
			pte = pte_mksoft_dirty(pte);
		else
			pte = pte_clear_soft_dirty(pte);

		if (is_writable_migration_entry(entry))
			pte = pte_mkwrite(pte, vma);
		else if (pte_swp_uffd_wp(old_pte))
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
			if (pte_swp_soft_dirty(old_pte))
				pte = pte_swp_mksoft_dirty(pte);
			if (pte_swp_uffd_wp(old_pte))
				pte = pte_swp_mkuffd_wp(pte);
		}

#ifdef CONFIG_HUGETLB_PAGE
		if (folio_test_hugetlb(folio)) {
			struct hstate *h = hstate_vma(vma);
			unsigned int shift = huge_page_shift(h);
			unsigned long psize = huge_page_size(h);

			pte = arch_make_huge_pte(pte, shift, vma->vm_flags);
			if (folio_test_anon(folio))
				hugetlb_add_anon_rmap(folio, vma, pvmw.address,
						      rmap_flags);
			else
				hugetlb_add_file_rmap(folio);
			set_huge_pte_at(vma->vm_mm, pvmw.address, pvmw.pte, pte,
					psize);
		} else
#endif
		{
			if (folio_test_anon(folio))
				folio_add_anon_rmap_pte(folio, new, vma,
							pvmw.address, rmap_flags);
			else
				folio_add_file_rmap_pte(folio, new, vma);
			set_pte_at(vma->vm_mm, pvmw.address, pvmw.pte, pte);
		}
		if (vma->vm_flags & VM_LOCKED)
			mlock_drain_local();

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
void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
			  unsigned long address)
{
	spinlock_t *ptl;
	pte_t *ptep;
	pte_t pte;
	swp_entry_t entry;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!ptep)
		return;

	pte = ptep_get(ptep);
	pte_unmap(ptep);

	if (!is_swap_pte(pte))
		goto out;

	entry = pte_to_swp_entry(pte);
	if (!is_migration_entry(entry))
		goto out;

	migration_entry_wait_on_locked(entry, ptl);
	return;
out:
	spin_unlock(ptl);
}

#ifdef CONFIG_HUGETLB_PAGE
/*
 * The vma read lock must be held upon entry. Holding that lock prevents either
 * the pte or the ptl from being freed.
 *
 * This function will release the vma lock before returning.
 */
void migration_entry_wait_huge(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
	spinlock_t *ptl = huge_pte_lockptr(hstate_vma(vma), vma->vm_mm, ptep);
	pte_t pte;

	hugetlb_vma_assert_locked(vma);
	spin_lock(ptl);
	pte = huge_ptep_get(vma->vm_mm, addr, ptep);

	if (unlikely(!is_hugetlb_entry_migration(pte))) {
		spin_unlock(ptl);
		hugetlb_vma_unlock_read(vma);
	} else {
		/*
		 * If migration entry existed, safe to release vma lock
		 * here because the pgtable page won't be freed without the
		 * pgtable lock released.  See comment right above pgtable
		 * lock release in migration_entry_wait_on_locked().
		 */
		hugetlb_vma_unlock_read(vma);
		migration_entry_wait_on_locked(pte_to_swp_entry(pte), ptl);
	}
}
#endif

#ifdef CONFIG_ARCH_ENABLE_THP_MIGRATION
void pmd_migration_entry_wait(struct mm_struct *mm, pmd_t *pmd)
{
	spinlock_t *ptl;

	ptl = pmd_lock(mm, pmd);
	if (!is_pmd_migration_entry(*pmd))
		goto unlock;
	migration_entry_wait_on_locked(pmd_to_swp_entry(*pmd), ptl);
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
 * Replace the folio in the mapping.
 *
 * The number of remaining references must be:
 * 1 for anonymous folios without a mapping
 * 2 for folios with a mapping
 * 3 for folios with a mapping and PagePrivate/PagePrivate2 set.
 */
static int __folio_migrate_mapping(struct address_space *mapping,
		struct folio *newfolio, struct folio *folio, int expected_count)
{
	XA_STATE(xas, &mapping->i_pages, folio_index(folio));
	struct zone *oldzone, *newzone;
	int dirty;
	long nr = folio_nr_pages(folio);
	long entries, i;

	if (!mapping) {
		/* Take off deferred split queue while frozen and memcg set */
		if (folio_test_large(folio) &&
		    folio_test_large_rmappable(folio)) {
			if (!folio_ref_freeze(folio, expected_count))
				return -EAGAIN;
			folio_undo_large_rmappable(folio);
			folio_ref_unfreeze(folio, expected_count);
		}

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

	/* Take off deferred split queue while frozen and memcg set */
	folio_undo_large_rmappable(folio);

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
		entries = nr;
	} else {
		VM_BUG_ON_FOLIO(folio_test_swapcache(folio), folio);
		entries = 1;
	}

	/* Move dirty while folio refs frozen and newfolio not yet exposed */
	dirty = folio_test_dirty(folio);
	if (dirty) {
		folio_clear_dirty(folio);
		folio_set_dirty(newfolio);
	}

	/* Swap cache still stores N entries instead of a high-order entry */
	for (i = 0; i < entries; i++) {
		xas_store(&xas, newfolio);
		xas_next(&xas);
	}

	/*
	 * Drop cache reference from old folio by unfreezing
	 * to one less reference.
	 * We know this isn't the last reference.
	 */
	folio_ref_unfreeze(folio, expected_count - nr);

	xas_unlock(&xas);
	/* Leave irq disabled to prevent preemption while updating stats */

	/*
	 * If moved to a different zone then also account
	 * the folio for that zone. Other VM counters will be
	 * taken care of when we establish references to the
	 * new folio and drop references to the old folio.
	 *
	 * Note that anonymous folios are accounted for
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

			if (folio_test_pmd_mappable(folio)) {
				__mod_lruvec_state(old_lruvec, NR_SHMEM_THPS, -nr);
				__mod_lruvec_state(new_lruvec, NR_SHMEM_THPS, nr);
			}
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

int folio_migrate_mapping(struct address_space *mapping,
		struct folio *newfolio, struct folio *folio, int extra_count)
{
	int expected_count = folio_expected_refs(mapping, folio) + extra_count;

	if (folio_ref_count(folio) != expected_count)
		return -EAGAIN;

	return __folio_migrate_mapping(mapping, newfolio, folio, expected_count);
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
	int rc, expected_count = folio_expected_refs(mapping, src);

	if (folio_ref_count(src) != expected_count)
		return -EAGAIN;

	rc = folio_mc_copy(dst, src);
	if (unlikely(rc))
		return rc;

	xas_lock_irq(&xas);
	if (!folio_ref_freeze(src, expected_count)) {
		xas_unlock_irq(&xas);
		return -EAGAIN;
	}

	dst->index = src->index;
	dst->mapping = src->mapping;

	folio_ref_add(dst, folio_nr_pages(dst));

	xas_store(&xas, dst);

	folio_ref_unfreeze(src, expected_count - folio_nr_pages(src));

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
	cpupid = folio_xchg_last_cpupid(folio, -1);
	/*
	 * For memory tiering mode, when migrate between slow and fast
	 * memory node, reset cpupid, because that is used to record
	 * page access time in slow memory node.
	 */
	if (sysctl_numa_balancing_mode & NUMA_BALANCING_MEMORY_TIERING) {
		bool f_toptier = node_is_toptier(folio_nid(folio));
		bool t_toptier = node_is_toptier(folio_nid(newfolio));

		if (f_toptier != t_toptier)
			cpupid = -1;
	}
	folio_xchg_last_cpupid(newfolio, cpupid);

	folio_migrate_ksm(newfolio, folio);
	/*
	 * Please do not reorder this without considering how mm/ksm.c's
	 * ksm_get_folio() depends upon ksm_migrate_page() and PageSwapCache().
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

	mem_cgroup_migrate(folio, newfolio);
}
EXPORT_SYMBOL(folio_migrate_flags);

/************************************************************
 *                    Migration functions
 ***********************************************************/

static int __migrate_folio(struct address_space *mapping, struct folio *dst,
			   struct folio *src, void *src_private,
			   enum migrate_mode mode)
{
	int rc, expected_count = folio_expected_refs(mapping, src);

	/* Check whether src does not have extra refs before we do more work */
	if (folio_ref_count(src) != expected_count)
		return -EAGAIN;

	rc = folio_mc_copy(dst, src);
	if (unlikely(rc))
		return rc;

	rc = __folio_migrate_mapping(mapping, dst, src, expected_count);
	if (rc != MIGRATEPAGE_SUCCESS)
		return rc;

	if (src_private)
		folio_attach_private(dst, folio_detach_private(src));

	folio_migrate_flags(dst, src);
	return MIGRATEPAGE_SUCCESS;
}

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
	BUG_ON(folio_test_writeback(src));	/* Writeback must be complete */
	return __migrate_folio(mapping, dst, src, NULL, mode);
}
EXPORT_SYMBOL(migrate_folio);

#ifdef CONFIG_BUFFER_HEAD
/* Returns true if all buffers are successfully locked */
static bool buffer_migrate_lock_buffers(struct buffer_head *head,
							enum migrate_mode mode)
{
	struct buffer_head *bh = head;
	struct buffer_head *failed_bh;

	do {
		if (!trylock_buffer(bh)) {
			if (mode == MIGRATE_ASYNC)
				goto unlock;
			if (mode == MIGRATE_SYNC_LIGHT && !buffer_uptodate(bh))
				goto unlock;
			lock_buffer(bh);
		}

		bh = bh->b_this_page;
	} while (bh != head);

	return true;

unlock:
	/* We failed to lock the buffer and cannot stall. */
	failed_bh = bh;
	bh = head;
	while (bh != failed_bh) {
		unlock_buffer(bh);
		bh = bh->b_this_page;
	}

	return false;
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
		spin_lock(&mapping->i_private_lock);
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
			spin_unlock(&mapping->i_private_lock);
			invalidate_bh_lrus();
			invalidated = true;
			goto recheck_buffers;
		}
	}

	rc = filemap_migrate_folio(mapping, dst, src, mode);
	if (rc != MIGRATEPAGE_SUCCESS)
		goto unlock_buffers;

	bh = head;
	do {
		folio_set_bh(bh, dst, bh_offset(bh));
		bh = bh->b_this_page;
	} while (bh != head);

unlock_buffers:
	if (check_refs)
		spin_unlock(&mapping->i_private_lock);
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
EXPORT_SYMBOL_GPL(buffer_migrate_folio_norefs);
#endif /* CONFIG_BUFFER_HEAD */

int filemap_migrate_folio(struct address_space *mapping,
		struct folio *dst, struct folio *src, enum migrate_mode mode)
{
	return __migrate_folio(mapping, dst, src, folio_get_private(src), mode);
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
	if (!filemap_release_folio(src, GFP_KERNEL))
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
	bool is_lru = !__folio_test_movable(src);

	VM_BUG_ON_FOLIO(!folio_test_locked(src), src);
	VM_BUG_ON_FOLIO(!folio_test_locked(dst), dst);

	if (likely(is_lru)) {
		struct address_space *mapping = folio_mapping(src);

		if (!mapping)
			rc = migrate_folio(mapping, dst, src, mode);
		else if (mapping_inaccessible(mapping))
			rc = -EOPNOTSUPP;
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

		mops = folio_movable_ops(src);
		rc = mops->migrate_page(&dst->page, &src->page, mode);
		WARN_ON_ONCE(rc == MIGRATEPAGE_SUCCESS &&
				!folio_test_isolated(src));
	}

	/*
	 * When successful, old pagecache src->mapping must be cleared before
	 * src is freed; but stats require that PageAnon be left as PageAnon.
	 */
	if (rc == MIGRATEPAGE_SUCCESS) {
		if (__folio_test_movable(src)) {
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

/*
 * To record some information during migration, we use unused private
 * field of struct folio of the newly allocated destination folio.
 * This is safe because nobody is using it except us.
 */
enum {
	PAGE_WAS_MAPPED = BIT(0),
	PAGE_WAS_MLOCKED = BIT(1),
	PAGE_OLD_STATES = PAGE_WAS_MAPPED | PAGE_WAS_MLOCKED,
};

static void __migrate_folio_record(struct folio *dst,
				   int old_page_state,
				   struct anon_vma *anon_vma)
{
	dst->private = (void *)anon_vma + old_page_state;
}

static void __migrate_folio_extract(struct folio *dst,
				   int *old_page_state,
				   struct anon_vma **anon_vmap)
{
	unsigned long private = (unsigned long)dst->private;

	*anon_vmap = (struct anon_vma *)(private & ~PAGE_OLD_STATES);
	*old_page_state = private & PAGE_OLD_STATES;
	dst->private = NULL;
}

/* Restore the source folio to the original state upon failure */
static void migrate_folio_undo_src(struct folio *src,
				   int page_was_mapped,
				   struct anon_vma *anon_vma,
				   bool locked,
				   struct list_head *ret)
{
	if (page_was_mapped)
		remove_migration_ptes(src, src, false);
	/* Drop an anon_vma reference if we took one */
	if (anon_vma)
		put_anon_vma(anon_vma);
	if (locked)
		folio_unlock(src);
	if (ret)
		list_move_tail(&src->lru, ret);
}

/* Restore the destination folio to the original state upon failure */
static void migrate_folio_undo_dst(struct folio *dst, bool locked,
		free_folio_t put_new_folio, unsigned long private)
{
	if (locked)
		folio_unlock(dst);
	if (put_new_folio)
		put_new_folio(dst, private);
	else
		folio_put(dst);
}

/* Cleanup src folio upon migration success */
static void migrate_folio_done(struct folio *src,
			       enum migrate_reason reason)
{
	/*
	 * Compaction can migrate also non-LRU pages which are
	 * not accounted to NR_ISOLATED_*. They can be recognized
	 * as __folio_test_movable
	 */
	if (likely(!__folio_test_movable(src)))
		mod_node_page_state(folio_pgdat(src), NR_ISOLATED_ANON +
				    folio_is_file_lru(src), -folio_nr_pages(src));

	if (reason != MR_MEMORY_FAILURE)
		/* We release the page in page_handle_poison. */
		folio_put(src);
}

/* Obtain the lock on page, remove all ptes. */
static int migrate_folio_unmap(new_folio_t get_new_folio,
		free_folio_t put_new_folio, unsigned long private,
		struct folio *src, struct folio **dstp, enum migrate_mode mode,
		enum migrate_reason reason, struct list_head *ret)
{
	struct folio *dst;
	int rc = -EAGAIN;
	int old_page_state = 0;
	struct anon_vma *anon_vma = NULL;
	bool is_lru = !__folio_test_movable(src);
	bool locked = false;
	bool dst_locked = false;

	if (folio_ref_count(src) == 1) {
		/* Folio was freed from under us. So we are done. */
		folio_clear_active(src);
		folio_clear_unevictable(src);
		/* free_pages_prepare() will clear PG_isolated. */
		list_del(&src->lru);
		migrate_folio_done(src, reason);
		return MIGRATEPAGE_SUCCESS;
	}

	dst = get_new_folio(src, private);
	if (!dst)
		return -ENOMEM;
	*dstp = dst;

	dst->private = NULL;

	if (!folio_trylock(src)) {
		if (mode == MIGRATE_ASYNC)
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

		/*
		 * In "light" mode, we can wait for transient locks (eg
		 * inserting a page into the page table), but it's not
		 * worth waiting for I/O.
		 */
		if (mode == MIGRATE_SYNC_LIGHT && !folio_test_uptodate(src))
			goto out;

		folio_lock(src);
	}
	locked = true;
	if (folio_test_mlocked(src))
		old_page_state |= PAGE_WAS_MLOCKED;

	if (folio_test_writeback(src)) {
		/*
		 * Only in the case of a full synchronous migration is it
		 * necessary to wait for PageWriteback. In the async case,
		 * the retry loop is too short and in the sync-light case,
		 * the overhead of stalling is too much
		 */
		switch (mode) {
		case MIGRATE_SYNC:
			break;
		default:
			rc = -EBUSY;
			goto out;
		}
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
		goto out;
	dst_locked = true;

	if (unlikely(!is_lru)) {
		__migrate_folio_record(dst, old_page_state, anon_vma);
		return MIGRATEPAGE_UNMAP;
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
			goto out;
		}
	} else if (folio_mapped(src)) {
		/* Establish migration ptes */
		VM_BUG_ON_FOLIO(folio_test_anon(src) &&
			       !folio_test_ksm(src) && !anon_vma, src);
		try_to_migrate(src, mode == MIGRATE_ASYNC ? TTU_BATCH_FLUSH : 0);
		old_page_state |= PAGE_WAS_MAPPED;
	}

	if (!folio_mapped(src)) {
		__migrate_folio_record(dst, old_page_state, anon_vma);
		return MIGRATEPAGE_UNMAP;
	}

out:
	/*
	 * A folio that has not been unmapped will be restored to
	 * right list unless we want to retry.
	 */
	if (rc == -EAGAIN)
		ret = NULL;

	migrate_folio_undo_src(src, old_page_state & PAGE_WAS_MAPPED,
			       anon_vma, locked, ret);
	migrate_folio_undo_dst(dst, dst_locked, put_new_folio, private);

	return rc;
}

/* Migrate the folio to the newly allocated folio in dst. */
static int migrate_folio_move(free_folio_t put_new_folio, unsigned long private,
			      struct folio *src, struct folio *dst,
			      enum migrate_mode mode, enum migrate_reason reason,
			      struct list_head *ret)
{
	int rc;
	int old_page_state = 0;
	struct anon_vma *anon_vma = NULL;
	bool is_lru = !__folio_test_movable(src);
	struct list_head *prev;

	__migrate_folio_extract(dst, &old_page_state, &anon_vma);
	prev = dst->lru.prev;
	list_del(&dst->lru);

	rc = move_to_new_folio(dst, src, mode);
	if (rc)
		goto out;

	if (unlikely(!is_lru))
		goto out_unlock_both;

	/*
	 * When successful, push dst to LRU immediately: so that if it
	 * turns out to be an mlocked page, remove_migration_ptes() will
	 * automatically build up the correct dst->mlock_count for it.
	 *
	 * We would like to do something similar for the old page, when
	 * unsuccessful, and other cases when a page has been temporarily
	 * isolated from the unevictable LRU: but this case is the easiest.
	 */
	folio_add_lru(dst);
	if (old_page_state & PAGE_WAS_MLOCKED)
		lru_add_drain();

	if (old_page_state & PAGE_WAS_MAPPED)
		remove_migration_ptes(src, dst, false);

out_unlock_both:
	folio_unlock(dst);
	set_page_owner_migrate_reason(&dst->page, reason);
	/*
	 * If migration is successful, decrease refcount of dst,
	 * which will not free the page because new page owner increased
	 * refcounter.
	 */
	folio_put(dst);

	/*
	 * A folio that has been migrated has all references removed
	 * and will be freed.
	 */
	list_del(&src->lru);
	/* Drop an anon_vma reference if we took one */
	if (anon_vma)
		put_anon_vma(anon_vma);
	folio_unlock(src);
	migrate_folio_done(src, reason);

	return rc;
out:
	/*
	 * A folio that has not been migrated will be restored to
	 * right list unless we want to retry.
	 */
	if (rc == -EAGAIN) {
		list_add(&dst->lru, prev);
		__migrate_folio_record(dst, old_page_state, anon_vma);
		return rc;
	}

	migrate_folio_undo_src(src, old_page_state & PAGE_WAS_MAPPED,
			       anon_vma, true, ret);
	migrate_folio_undo_dst(dst, true, put_new_folio, private);

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
static int unmap_and_move_huge_page(new_folio_t get_new_folio,
		free_folio_t put_new_folio, unsigned long private,
		struct folio *src, int force, enum migrate_mode mode,
		int reason, struct list_head *ret)
{
	struct folio *dst;
	int rc = -EAGAIN;
	int page_was_mapped = 0;
	struct anon_vma *anon_vma = NULL;
	struct address_space *mapping = NULL;

	if (folio_ref_count(src) == 1) {
		/* page was freed from under us. So we are done. */
		folio_putback_active_hugetlb(src);
		return MIGRATEPAGE_SUCCESS;
	}

	dst = get_new_folio(src, private);
	if (!dst)
		return -ENOMEM;

	if (!folio_trylock(src)) {
		if (!force)
			goto out;
		switch (mode) {
		case MIGRATE_SYNC:
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
	if (hugetlb_folio_subpool(src) && !folio_mapping(src)) {
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
			mapping = hugetlb_folio_mapping_lock_write(src);
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
		move_hugetlb_state(src, dst, reason);
		put_new_folio = NULL;
	}

out_unlock:
	folio_unlock(src);
out:
	if (rc == MIGRATEPAGE_SUCCESS)
		folio_putback_active_hugetlb(src);
	else if (rc != -EAGAIN)
		list_move_tail(&src->lru, ret);

	/*
	 * If migration was not successful and there's a freeing callback, use
	 * it.  Otherwise, put_page() will drop the reference grabbed during
	 * isolation.
	 */
	if (put_new_folio)
		put_new_folio(dst, private);
	else
		folio_putback_active_hugetlb(dst);

	return rc;
}

static inline int try_split_folio(struct folio *folio, struct list_head *split_folios,
				  enum migrate_mode mode)
{
	int rc;

	if (mode == MIGRATE_ASYNC) {
		if (!folio_trylock(folio))
			return -EAGAIN;
	} else {
		folio_lock(folio);
	}
	rc = split_folio_to_list(folio, split_folios);
	folio_unlock(folio);
	if (!rc)
		list_move_tail(&folio->lru, split_folios);

	return rc;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define NR_MAX_BATCHED_MIGRATION	HPAGE_PMD_NR
#else
#define NR_MAX_BATCHED_MIGRATION	512
#endif
#define NR_MAX_MIGRATE_PAGES_RETRY	10
#define NR_MAX_MIGRATE_ASYNC_RETRY	3
#define NR_MAX_MIGRATE_SYNC_RETRY					\
	(NR_MAX_MIGRATE_PAGES_RETRY - NR_MAX_MIGRATE_ASYNC_RETRY)

struct migrate_pages_stats {
	int nr_succeeded;	/* Normal and large folios migrated successfully, in
				   units of base pages */
	int nr_failed_pages;	/* Normal and large folios failed to be migrated, in
				   units of base pages.  Untried folios aren't counted */
	int nr_thp_succeeded;	/* THP migrated successfully */
	int nr_thp_failed;	/* THP failed to be migrated */
	int nr_thp_split;	/* THP split before migrating */
	int nr_split;	/* Large folio (include THP) split before migrating */
};

/*
 * Returns the number of hugetlb folios that were not migrated, or an error code
 * after NR_MAX_MIGRATE_PAGES_RETRY attempts or if no hugetlb folios are movable
 * any more because the list has become empty or no retryable hugetlb folios
 * exist any more. It is caller's responsibility to call putback_movable_pages()
 * only if ret != 0.
 */
static int migrate_hugetlbs(struct list_head *from, new_folio_t get_new_folio,
			    free_folio_t put_new_folio, unsigned long private,
			    enum migrate_mode mode, int reason,
			    struct migrate_pages_stats *stats,
			    struct list_head *ret_folios)
{
	int retry = 1;
	int nr_failed = 0;
	int nr_retry_pages = 0;
	int pass = 0;
	struct folio *folio, *folio2;
	int rc, nr_pages;

	for (pass = 0; pass < NR_MAX_MIGRATE_PAGES_RETRY && retry; pass++) {
		retry = 0;
		nr_retry_pages = 0;

		list_for_each_entry_safe(folio, folio2, from, lru) {
			if (!folio_test_hugetlb(folio))
				continue;

			nr_pages = folio_nr_pages(folio);

			cond_resched();

			/*
			 * Migratability of hugepages depends on architectures and
			 * their size.  This check is necessary because some callers
			 * of hugepage migration like soft offline and memory
			 * hotremove don't walk through page tables or check whether
			 * the hugepage is pmd-based or not before kicking migration.
			 */
			if (!hugepage_migration_supported(folio_hstate(folio))) {
				nr_failed++;
				stats->nr_failed_pages += nr_pages;
				list_move_tail(&folio->lru, ret_folios);
				continue;
			}

			rc = unmap_and_move_huge_page(get_new_folio,
						      put_new_folio, private,
						      folio, pass > 2, mode,
						      reason, ret_folios);
			/*
			 * The rules are:
			 *	Success: hugetlb folio will be put back
			 *	-EAGAIN: stay on the from list
			 *	-ENOMEM: stay on the from list
			 *	Other errno: put on ret_folios list
			 */
			switch(rc) {
			case -ENOMEM:
				/*
				 * When memory is low, don't bother to try to migrate
				 * other folios, just exit.
				 */
				stats->nr_failed_pages += nr_pages + nr_retry_pages;
				return -ENOMEM;
			case -EAGAIN:
				retry++;
				nr_retry_pages += nr_pages;
				break;
			case MIGRATEPAGE_SUCCESS:
				stats->nr_succeeded += nr_pages;
				break;
			default:
				/*
				 * Permanent failure (-EBUSY, etc.):
				 * unlike -EAGAIN case, the failed folio is
				 * removed from migration folio list and not
				 * retried in the next outer loop.
				 */
				nr_failed++;
				stats->nr_failed_pages += nr_pages;
				break;
			}
		}
	}
	/*
	 * nr_failed is number of hugetlb folios failed to be migrated.  After
	 * NR_MAX_MIGRATE_PAGES_RETRY attempts, give up and count retried hugetlb
	 * folios as failed.
	 */
	nr_failed += retry;
	stats->nr_failed_pages += nr_retry_pages;

	return nr_failed;
}

/*
 * migrate_pages_batch() first unmaps folios in the from list as many as
 * possible, then move the unmapped folios.
 *
 * We only batch migration if mode == MIGRATE_ASYNC to avoid to wait a
 * lock or bit when we have locked more than one folio.  Which may cause
 * deadlock (e.g., for loop device).  So, if mode != MIGRATE_ASYNC, the
 * length of the from list must be <= 1.
 */
static int migrate_pages_batch(struct list_head *from,
		new_folio_t get_new_folio, free_folio_t put_new_folio,
		unsigned long private, enum migrate_mode mode, int reason,
		struct list_head *ret_folios, struct list_head *split_folios,
		struct migrate_pages_stats *stats, int nr_pass)
{
	int retry = 1;
	int thp_retry = 1;
	int nr_failed = 0;
	int nr_retry_pages = 0;
	int pass = 0;
	bool is_thp = false;
	bool is_large = false;
	struct folio *folio, *folio2, *dst = NULL, *dst2;
	int rc, rc_saved = 0, nr_pages;
	LIST_HEAD(unmap_folios);
	LIST_HEAD(dst_folios);
	bool nosplit = (reason == MR_NUMA_MISPLACED);

	VM_WARN_ON_ONCE(mode != MIGRATE_ASYNC &&
			!list_empty(from) && !list_is_singular(from));

	for (pass = 0; pass < nr_pass && retry; pass++) {
		retry = 0;
		thp_retry = 0;
		nr_retry_pages = 0;

		list_for_each_entry_safe(folio, folio2, from, lru) {
			is_large = folio_test_large(folio);
			is_thp = is_large && folio_test_pmd_mappable(folio);
			nr_pages = folio_nr_pages(folio);

			cond_resched();

			/*
			 * The rare folio on the deferred split list should
			 * be split now. It should not count as a failure:
			 * but increment nr_failed because, without doing so,
			 * migrate_pages() may report success with (split but
			 * unmigrated) pages still on its fromlist; whereas it
			 * always reports success when its fromlist is empty.
			 * stats->nr_thp_failed should be increased too,
			 * otherwise stats inconsistency will happen when
			 * migrate_pages_batch is called via migrate_pages()
			 * with MIGRATE_SYNC and MIGRATE_ASYNC.
			 *
			 * Only check it without removing it from the list.
			 * Since the folio can be on deferred_split_scan()
			 * local list and removing it can cause the local list
			 * corruption. Folio split process below can handle it
			 * with the help of folio_ref_freeze().
			 *
			 * nr_pages > 2 is needed to avoid checking order-1
			 * page cache folios. They exist, in contrast to
			 * non-existent order-1 anonymous folios, and do not
			 * use _deferred_list.
			 */
			if (nr_pages > 2 &&
			   !list_empty(&folio->_deferred_list)) {
				if (!try_split_folio(folio, split_folios, mode)) {
					nr_failed++;
					stats->nr_thp_failed += is_thp;
					stats->nr_thp_split += is_thp;
					stats->nr_split++;
					continue;
				}
			}

			/*
			 * Large folio migration might be unsupported or
			 * the allocation might be failed so we should retry
			 * on the same folio with the large folio split
			 * to normal folios.
			 *
			 * Split folios are put in split_folios, and
			 * we will migrate them after the rest of the
			 * list is processed.
			 */
			if (!thp_migration_supported() && is_thp) {
				nr_failed++;
				stats->nr_thp_failed++;
				if (!try_split_folio(folio, split_folios, mode)) {
					stats->nr_thp_split++;
					stats->nr_split++;
					continue;
				}
				stats->nr_failed_pages += nr_pages;
				list_move_tail(&folio->lru, ret_folios);
				continue;
			}

			rc = migrate_folio_unmap(get_new_folio, put_new_folio,
					private, folio, &dst, mode, reason,
					ret_folios);
			/*
			 * The rules are:
			 *	Success: folio will be freed
			 *	Unmap: folio will be put on unmap_folios list,
			 *	       dst folio put on dst_folios list
			 *	-EAGAIN: stay on the from list
			 *	-ENOMEM: stay on the from list
			 *	Other errno: put on ret_folios list
			 */
			switch(rc) {
			case -ENOMEM:
				/*
				 * When memory is low, don't bother to try to migrate
				 * other folios, move unmapped folios, then exit.
				 */
				nr_failed++;
				stats->nr_thp_failed += is_thp;
				/* Large folio NUMA faulting doesn't split to retry. */
				if (is_large && !nosplit) {
					int ret = try_split_folio(folio, split_folios, mode);

					if (!ret) {
						stats->nr_thp_split += is_thp;
						stats->nr_split++;
						break;
					} else if (reason == MR_LONGTERM_PIN &&
						   ret == -EAGAIN) {
						/*
						 * Try again to split large folio to
						 * mitigate the failure of longterm pinning.
						 */
						retry++;
						thp_retry += is_thp;
						nr_retry_pages += nr_pages;
						/* Undo duplicated failure counting. */
						nr_failed--;
						stats->nr_thp_failed -= is_thp;
						break;
					}
				}

				stats->nr_failed_pages += nr_pages + nr_retry_pages;
				/* nr_failed isn't updated for not used */
				stats->nr_thp_failed += thp_retry;
				rc_saved = rc;
				if (list_empty(&unmap_folios))
					goto out;
				else
					goto move;
			case -EAGAIN:
				retry++;
				thp_retry += is_thp;
				nr_retry_pages += nr_pages;
				break;
			case MIGRATEPAGE_SUCCESS:
				stats->nr_succeeded += nr_pages;
				stats->nr_thp_succeeded += is_thp;
				break;
			case MIGRATEPAGE_UNMAP:
				list_move_tail(&folio->lru, &unmap_folios);
				list_add_tail(&dst->lru, &dst_folios);
				break;
			default:
				/*
				 * Permanent failure (-EBUSY, etc.):
				 * unlike -EAGAIN case, the failed folio is
				 * removed from migration folio list and not
				 * retried in the next outer loop.
				 */
				nr_failed++;
				stats->nr_thp_failed += is_thp;
				stats->nr_failed_pages += nr_pages;
				break;
			}
		}
	}
	nr_failed += retry;
	stats->nr_thp_failed += thp_retry;
	stats->nr_failed_pages += nr_retry_pages;
move:
	/* Flush TLBs for all unmapped folios */
	try_to_unmap_flush();

	retry = 1;
	for (pass = 0; pass < nr_pass && retry; pass++) {
		retry = 0;
		thp_retry = 0;
		nr_retry_pages = 0;

		dst = list_first_entry(&dst_folios, struct folio, lru);
		dst2 = list_next_entry(dst, lru);
		list_for_each_entry_safe(folio, folio2, &unmap_folios, lru) {
			is_thp = folio_test_large(folio) && folio_test_pmd_mappable(folio);
			nr_pages = folio_nr_pages(folio);

			cond_resched();

			rc = migrate_folio_move(put_new_folio, private,
						folio, dst, mode,
						reason, ret_folios);
			/*
			 * The rules are:
			 *	Success: folio will be freed
			 *	-EAGAIN: stay on the unmap_folios list
			 *	Other errno: put on ret_folios list
			 */
			switch(rc) {
			case -EAGAIN:
				retry++;
				thp_retry += is_thp;
				nr_retry_pages += nr_pages;
				break;
			case MIGRATEPAGE_SUCCESS:
				stats->nr_succeeded += nr_pages;
				stats->nr_thp_succeeded += is_thp;
				break;
			default:
				nr_failed++;
				stats->nr_thp_failed += is_thp;
				stats->nr_failed_pages += nr_pages;
				break;
			}
			dst = dst2;
			dst2 = list_next_entry(dst, lru);
		}
	}
	nr_failed += retry;
	stats->nr_thp_failed += thp_retry;
	stats->nr_failed_pages += nr_retry_pages;

	rc = rc_saved ? : nr_failed;
out:
	/* Cleanup remaining folios */
	dst = list_first_entry(&dst_folios, struct folio, lru);
	dst2 = list_next_entry(dst, lru);
	list_for_each_entry_safe(folio, folio2, &unmap_folios, lru) {
		int old_page_state = 0;
		struct anon_vma *anon_vma = NULL;

		__migrate_folio_extract(dst, &old_page_state, &anon_vma);
		migrate_folio_undo_src(folio, old_page_state & PAGE_WAS_MAPPED,
				       anon_vma, true, ret_folios);
		list_del(&dst->lru);
		migrate_folio_undo_dst(dst, true, put_new_folio, private);
		dst = dst2;
		dst2 = list_next_entry(dst, lru);
	}

	return rc;
}

static int migrate_pages_sync(struct list_head *from, new_folio_t get_new_folio,
		free_folio_t put_new_folio, unsigned long private,
		enum migrate_mode mode, int reason,
		struct list_head *ret_folios, struct list_head *split_folios,
		struct migrate_pages_stats *stats)
{
	int rc, nr_failed = 0;
	LIST_HEAD(folios);
	struct migrate_pages_stats astats;

	memset(&astats, 0, sizeof(astats));
	/* Try to migrate in batch with MIGRATE_ASYNC mode firstly */
	rc = migrate_pages_batch(from, get_new_folio, put_new_folio, private, MIGRATE_ASYNC,
				 reason, &folios, split_folios, &astats,
				 NR_MAX_MIGRATE_ASYNC_RETRY);
	stats->nr_succeeded += astats.nr_succeeded;
	stats->nr_thp_succeeded += astats.nr_thp_succeeded;
	stats->nr_thp_split += astats.nr_thp_split;
	stats->nr_split += astats.nr_split;
	if (rc < 0) {
		stats->nr_failed_pages += astats.nr_failed_pages;
		stats->nr_thp_failed += astats.nr_thp_failed;
		list_splice_tail(&folios, ret_folios);
		return rc;
	}
	stats->nr_thp_failed += astats.nr_thp_split;
	/*
	 * Do not count rc, as pages will be retried below.
	 * Count nr_split only, since it includes nr_thp_split.
	 */
	nr_failed += astats.nr_split;
	/*
	 * Fall back to migrate all failed folios one by one synchronously. All
	 * failed folios except split THPs will be retried, so their failure
	 * isn't counted
	 */
	list_splice_tail_init(&folios, from);
	while (!list_empty(from)) {
		list_move(from->next, &folios);
		rc = migrate_pages_batch(&folios, get_new_folio, put_new_folio,
					 private, mode, reason, ret_folios,
					 split_folios, stats, NR_MAX_MIGRATE_SYNC_RETRY);
		list_splice_tail_init(&folios, ret_folios);
		if (rc < 0)
			return rc;
		nr_failed += rc;
	}

	return nr_failed;
}

/*
 * migrate_pages - migrate the folios specified in a list, to the free folios
 *		   supplied as the target for the page migration
 *
 * @from:		The list of folios to be migrated.
 * @get_new_folio:	The function used to allocate free folios to be used
 *			as the target of the folio migration.
 * @put_new_folio:	The function used to free target folios if migration
 *			fails, or NULL if no special handling is necessary.
 * @private:		Private data to be passed on to get_new_folio()
 * @mode:		The migration mode that specifies the constraints for
 *			folio migration, if any.
 * @reason:		The reason for folio migration.
 * @ret_succeeded:	Set to the number of folios migrated successfully if
 *			the caller passes a non-NULL pointer.
 *
 * The function returns after NR_MAX_MIGRATE_PAGES_RETRY attempts or if no folios
 * are movable any more because the list has become empty or no retryable folios
 * exist any more. It is caller's responsibility to call putback_movable_pages()
 * only if ret != 0.
 *
 * Returns the number of {normal folio, large folio, hugetlb} that were not
 * migrated, or an error code. The number of large folio splits will be
 * considered as the number of non-migrated large folio, no matter how many
 * split folios of the large folio are migrated successfully.
 */
int migrate_pages(struct list_head *from, new_folio_t get_new_folio,
		free_folio_t put_new_folio, unsigned long private,
		enum migrate_mode mode, int reason, unsigned int *ret_succeeded)
{
	int rc, rc_gather;
	int nr_pages;
	struct folio *folio, *folio2;
	LIST_HEAD(folios);
	LIST_HEAD(ret_folios);
	LIST_HEAD(split_folios);
	struct migrate_pages_stats stats;

	trace_mm_migrate_pages_start(mode, reason);

	memset(&stats, 0, sizeof(stats));

	rc_gather = migrate_hugetlbs(from, get_new_folio, put_new_folio, private,
				     mode, reason, &stats, &ret_folios);
	if (rc_gather < 0)
		goto out;

again:
	nr_pages = 0;
	list_for_each_entry_safe(folio, folio2, from, lru) {
		/* Retried hugetlb folios will be kept in list  */
		if (folio_test_hugetlb(folio)) {
			list_move_tail(&folio->lru, &ret_folios);
			continue;
		}

		nr_pages += folio_nr_pages(folio);
		if (nr_pages >= NR_MAX_BATCHED_MIGRATION)
			break;
	}
	if (nr_pages >= NR_MAX_BATCHED_MIGRATION)
		list_cut_before(&folios, from, &folio2->lru);
	else
		list_splice_init(from, &folios);
	if (mode == MIGRATE_ASYNC)
		rc = migrate_pages_batch(&folios, get_new_folio, put_new_folio,
				private, mode, reason, &ret_folios,
				&split_folios, &stats,
				NR_MAX_MIGRATE_PAGES_RETRY);
	else
		rc = migrate_pages_sync(&folios, get_new_folio, put_new_folio,
				private, mode, reason, &ret_folios,
				&split_folios, &stats);
	list_splice_tail_init(&folios, &ret_folios);
	if (rc < 0) {
		rc_gather = rc;
		list_splice_tail(&split_folios, &ret_folios);
		goto out;
	}
	if (!list_empty(&split_folios)) {
		/*
		 * Failure isn't counted since all split folios of a large folio
		 * is counted as 1 failure already.  And, we only try to migrate
		 * with minimal effort, force MIGRATE_ASYNC mode and retry once.
		 */
		migrate_pages_batch(&split_folios, get_new_folio,
				put_new_folio, private, MIGRATE_ASYNC, reason,
				&ret_folios, NULL, &stats, 1);
		list_splice_tail_init(&split_folios, &ret_folios);
	}
	rc_gather += rc;
	if (!list_empty(from))
		goto again;
out:
	/*
	 * Put the permanent failure folio back to migration list, they
	 * will be put back to the right list by the caller.
	 */
	list_splice(&ret_folios, from);

	/*
	 * Return 0 in case all split folios of fail-to-migrate large folios
	 * are migrated successfully.
	 */
	if (list_empty(from))
		rc_gather = 0;

	count_vm_events(PGMIGRATE_SUCCESS, stats.nr_succeeded);
	count_vm_events(PGMIGRATE_FAIL, stats.nr_failed_pages);
	count_vm_events(THP_MIGRATION_SUCCESS, stats.nr_thp_succeeded);
	count_vm_events(THP_MIGRATION_FAIL, stats.nr_thp_failed);
	count_vm_events(THP_MIGRATION_SPLIT, stats.nr_thp_split);
	trace_mm_migrate_pages(stats.nr_succeeded, stats.nr_failed_pages,
			       stats.nr_thp_succeeded, stats.nr_thp_failed,
			       stats.nr_thp_split, stats.nr_split, mode,
			       reason);

	if (ret_succeeded)
		*ret_succeeded = stats.nr_succeeded;

	return rc_gather;
}

struct folio *alloc_migration_target(struct folio *src, unsigned long private)
{
	struct migration_target_control *mtc;
	gfp_t gfp_mask;
	unsigned int order = 0;
	int nid;
	int zidx;

	mtc = (struct migration_target_control *)private;
	gfp_mask = mtc->gfp_mask;
	nid = mtc->nid;
	if (nid == NUMA_NO_NODE)
		nid = folio_nid(src);

	if (folio_test_hugetlb(src)) {
		struct hstate *h = folio_hstate(src);

		gfp_mask = htlb_modify_alloc_mask(h, gfp_mask);
		return alloc_hugetlb_folio_nodemask(h, nid,
						mtc->nmask, gfp_mask,
						htlb_allow_alloc_fallback(mtc->reason));
	}

	if (folio_test_large(src)) {
		/*
		 * clear __GFP_RECLAIM to make the migration callback
		 * consistent with regular THP allocations.
		 */
		gfp_mask &= ~__GFP_RECLAIM;
		gfp_mask |= GFP_TRANSHUGE;
		order = folio_order(src);
	}
	zidx = zone_idx(folio_zone(src));
	if (is_highmem_idx(zidx) || zidx == ZONE_MOVABLE)
		gfp_mask |= __GFP_HIGHMEM;

	return __folio_alloc(gfp_mask, order, nid, mtc->nmask);
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

static int do_move_pages_to_node(struct list_head *pagelist, int node)
{
	int err;
	struct migration_target_control mtc = {
		.nid = node,
		.gfp_mask = GFP_HIGHUSER_MOVABLE | __GFP_THISNODE,
		.reason = MR_SYSCALL,
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
static int add_page_for_migration(struct mm_struct *mm, const void __user *p,
		int node, struct list_head *pagelist, bool migrate_all)
{
	struct vm_area_struct *vma;
	unsigned long addr;
	struct page *page;
	struct folio *folio;
	int err;

	mmap_read_lock(mm);
	addr = (unsigned long)untagged_addr_remote(mm, p);

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

	folio = page_folio(page);
	if (folio_is_zone_device(folio))
		goto out_putfolio;

	err = 0;
	if (folio_nid(folio) == node)
		goto out_putfolio;

	err = -EACCES;
	if (folio_likely_mapped_shared(folio) && !migrate_all)
		goto out_putfolio;

	err = -EBUSY;
	if (folio_test_hugetlb(folio)) {
		if (isolate_hugetlb(folio, pagelist))
			err = 1;
	} else {
		if (!folio_isolate_lru(folio))
			goto out_putfolio;

		err = 1;
		list_add_tail(&folio->lru, pagelist);
		node_stat_mod_folio(folio,
			NR_ISOLATED_ANON + folio_is_file_lru(folio),
			folio_nr_pages(folio));
	}
out_putfolio:
	/*
	 * Either remove the duplicate refcount from folio_isolate_lru()
	 * or drop the folio ref if it was not isolated.
	 */
	folio_put(folio);
out:
	mmap_read_unlock(mm);
	return err;
}

static int move_pages_and_store_status(int node,
		struct list_head *pagelist, int __user *status,
		int start, int i, unsigned long nr_pages)
{
	int err;

	if (list_empty(pagelist))
		return 0;

	err = do_move_pages_to_node(pagelist, node);
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
	compat_uptr_t __user *compat_pages = (void __user *)pages;
	int current_node = NUMA_NO_NODE;
	LIST_HEAD(pagelist);
	int start, i;
	int err = 0, err1;

	lru_cache_disable();

	for (i = start = 0; i < nr_pages; i++) {
		const void __user *p;
		int node;

		err = -EFAULT;
		if (in_compat_syscall()) {
			compat_uptr_t cp;

			if (get_user(cp, compat_pages + i))
				goto out_flush;

			p = compat_ptr(cp);
		} else {
			if (get_user(p, pages + i))
				goto out_flush;
		}
		if (get_user(node, nodes + i))
			goto out_flush;

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
			err = move_pages_and_store_status(current_node,
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
		err = add_page_for_migration(mm, p, current_node, &pagelist,
					     flags & MPOL_MF_MOVE_ALL);

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

		err = move_pages_and_store_status(current_node, &pagelist,
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
	err1 = move_pages_and_store_status(current_node, &pagelist,
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
		struct vm_area_struct *vma;
		struct page *page;
		int err = -EFAULT;

		vma = vma_lookup(mm, addr);
		if (!vma)
			goto set_status;

		/* FOLL_DUMP to ignore special (like zero) pages */
		page = follow_page(vma, addr, FOLL_GET | FOLL_DUMP);

		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto set_status;

		err = -ENOENT;
		if (!page)
			goto set_status;

		if (!is_zone_device_page(page))
			err = page_to_nid(page);

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

static struct folio *alloc_misplaced_dst_folio(struct folio *src,
					   unsigned long data)
{
	int nid = (int) data;
	int order = folio_order(src);
	gfp_t gfp = __GFP_THISNODE;

	if (order > 0)
		gfp |= GFP_TRANSHUGE_LIGHT;
	else {
		gfp |= GFP_HIGHUSER_MOVABLE | __GFP_NOMEMALLOC | __GFP_NORETRY |
			__GFP_NOWARN;
		gfp &= ~__GFP_RECLAIM;
	}
	return __folio_alloc_node(gfp, order, nid);
}

/*
 * Prepare for calling migrate_misplaced_folio() by isolating the folio if
 * permitted. Must be called with the PTL still held.
 */
int migrate_misplaced_folio_prepare(struct folio *folio,
		struct vm_area_struct *vma, int node)
{
	int nr_pages = folio_nr_pages(folio);
	pg_data_t *pgdat = NODE_DATA(node);

	if (folio_is_file_lru(folio)) {
		/*
		 * Do not migrate file folios that are mapped in multiple
		 * processes with execute permissions as they are probably
		 * shared libraries.
		 *
		 * See folio_likely_mapped_shared() on possible imprecision
		 * when we cannot easily detect if a folio is shared.
		 */
		if ((vma->vm_flags & VM_EXEC) &&
		    folio_likely_mapped_shared(folio))
			return -EACCES;

		/*
		 * Do not migrate dirty folios as not all filesystems can move
		 * dirty folios in MIGRATE_ASYNC mode which is a waste of
		 * cycles.
		 */
		if (folio_test_dirty(folio))
			return -EAGAIN;
	}

	/* Avoid migrating to a node that is nearly full */
	if (!migrate_balanced_pgdat(pgdat, nr_pages)) {
		int z;

		if (!(sysctl_numa_balancing_mode & NUMA_BALANCING_MEMORY_TIERING))
			return -EAGAIN;
		for (z = pgdat->nr_zones - 1; z >= 0; z--) {
			if (managed_zone(pgdat->node_zones + z))
				break;
		}

		/*
		 * If there are no managed zones, it should not proceed
		 * further.
		 */
		if (z < 0)
			return -EAGAIN;

		wakeup_kswapd(pgdat->node_zones + z, 0,
			      folio_order(folio), ZONE_MOVABLE);
		return -EAGAIN;
	}

	if (!folio_isolate_lru(folio))
		return -EAGAIN;

	node_stat_mod_folio(folio, NR_ISOLATED_ANON + folio_is_file_lru(folio),
			    nr_pages);
	return 0;
}

/*
 * Attempt to migrate a misplaced folio to the specified destination
 * node. Caller is expected to have isolated the folio by calling
 * migrate_misplaced_folio_prepare(), which will result in an
 * elevated reference count on the folio. This function will un-isolate the
 * folio, dereferencing the folio before returning.
 */
int migrate_misplaced_folio(struct folio *folio, struct vm_area_struct *vma,
			    int node)
{
	pg_data_t *pgdat = NODE_DATA(node);
	int nr_remaining;
	unsigned int nr_succeeded;
	LIST_HEAD(migratepages);

	list_add(&folio->lru, &migratepages);
	nr_remaining = migrate_pages(&migratepages, alloc_misplaced_dst_folio,
				     NULL, node, MIGRATE_ASYNC,
				     MR_NUMA_MISPLACED, &nr_succeeded);
	if (nr_remaining && !list_empty(&migratepages))
		putback_movable_pages(&migratepages);
	if (nr_succeeded) {
		count_vm_numa_events(NUMA_PAGE_MIGRATE, nr_succeeded);
		if (!node_is_toptier(folio_nid(folio)) && node_is_toptier(node))
			mod_node_page_state(pgdat, PGPROMOTE_SUCCESS,
					    nr_succeeded);
	}
	BUG_ON(!list_empty(&migratepages));
	return nr_remaining ? -EAGAIN : 0;
}
#endif /* CONFIG_NUMA_BALANCING */
#endif /* CONFIG_NUMA */
