// SPDX-License-Identifier: GPL-2.0
/*
 * Device Memory Migration functionality.
 *
 * Originally written by Jérôme Glisse.
 */
#include <linux/export.h>
#include <linux/memremap.h>
#include <linux/migrate.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/mmu_notifier.h>
#include <linux/oom.h>
#include <linux/pagewalk.h>
#include <linux/rmap.h>
#include <linux/swapops.h>
#include <asm/tlbflush.h>
#include "internal.h"

static int migrate_vma_collect_skip(unsigned long start,
				    unsigned long end,
				    struct mm_walk *walk)
{
	struct migrate_vma *migrate = walk->private;
	unsigned long addr;

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		migrate->dst[migrate->npages] = 0;
		migrate->src[migrate->npages++] = 0;
	}

	return 0;
}

static int migrate_vma_collect_hole(unsigned long start,
				    unsigned long end,
				    __always_unused int depth,
				    struct mm_walk *walk)
{
	struct migrate_vma *migrate = walk->private;
	unsigned long addr;

	/* Only allow populating anonymous memory. */
	if (!vma_is_anonymous(walk->vma))
		return migrate_vma_collect_skip(start, end, walk);

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		migrate->src[migrate->npages] = MIGRATE_PFN_MIGRATE;
		migrate->dst[migrate->npages] = 0;
		migrate->npages++;
		migrate->cpages++;
	}

	return 0;
}

static int migrate_vma_collect_pmd(pmd_t *pmdp,
				   unsigned long start,
				   unsigned long end,
				   struct mm_walk *walk)
{
	struct migrate_vma *migrate = walk->private;
	struct folio *fault_folio = migrate->fault_page ?
		page_folio(migrate->fault_page) : NULL;
	struct vm_area_struct *vma = walk->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr = start, unmapped = 0;
	spinlock_t *ptl;
	pte_t *ptep;

again:
	if (pmd_none(*pmdp))
		return migrate_vma_collect_hole(start, end, -1, walk);

	if (pmd_trans_huge(*pmdp)) {
		struct folio *folio;

		ptl = pmd_lock(mm, pmdp);
		if (unlikely(!pmd_trans_huge(*pmdp))) {
			spin_unlock(ptl);
			goto again;
		}

		folio = pmd_folio(*pmdp);
		if (is_huge_zero_folio(folio)) {
			spin_unlock(ptl);
			split_huge_pmd(vma, pmdp, addr);
		} else {
			int ret;

			folio_get(folio);
			spin_unlock(ptl);
			/* FIXME: we don't expect THP for fault_folio */
			if (WARN_ON_ONCE(fault_folio == folio))
				return migrate_vma_collect_skip(start, end,
								walk);
			if (unlikely(!folio_trylock(folio)))
				return migrate_vma_collect_skip(start, end,
								walk);
			ret = split_folio(folio);
			if (fault_folio != folio)
				folio_unlock(folio);
			folio_put(folio);
			if (ret)
				return migrate_vma_collect_skip(start, end,
								walk);
		}
	}

	ptep = pte_offset_map_lock(mm, pmdp, addr, &ptl);
	if (!ptep)
		goto again;
	arch_enter_lazy_mmu_mode();

	for (; addr < end; addr += PAGE_SIZE, ptep++) {
		unsigned long mpfn = 0, pfn;
		struct folio *folio;
		struct page *page;
		swp_entry_t entry;
		pte_t pte;

		pte = ptep_get(ptep);

		if (pte_none(pte)) {
			if (vma_is_anonymous(vma)) {
				mpfn = MIGRATE_PFN_MIGRATE;
				migrate->cpages++;
			}
			goto next;
		}

		if (!pte_present(pte)) {
			/*
			 * Only care about unaddressable device page special
			 * page table entry. Other special swap entries are not
			 * migratable, and we ignore regular swapped page.
			 */
			entry = pte_to_swp_entry(pte);
			if (!is_device_private_entry(entry))
				goto next;

			page = pfn_swap_entry_to_page(entry);
			if (!(migrate->flags &
				MIGRATE_VMA_SELECT_DEVICE_PRIVATE) ||
			    page->pgmap->owner != migrate->pgmap_owner)
				goto next;

			mpfn = migrate_pfn(page_to_pfn(page)) |
					MIGRATE_PFN_MIGRATE;
			if (is_writable_device_private_entry(entry))
				mpfn |= MIGRATE_PFN_WRITE;
		} else {
			pfn = pte_pfn(pte);
			if (is_zero_pfn(pfn) &&
			    (migrate->flags & MIGRATE_VMA_SELECT_SYSTEM)) {
				mpfn = MIGRATE_PFN_MIGRATE;
				migrate->cpages++;
				goto next;
			}
			page = vm_normal_page(migrate->vma, addr, pte);
			if (page && !is_zone_device_page(page) &&
			    !(migrate->flags & MIGRATE_VMA_SELECT_SYSTEM))
				goto next;
			else if (page && is_device_coherent_page(page) &&
			    (!(migrate->flags & MIGRATE_VMA_SELECT_DEVICE_COHERENT) ||
			     page->pgmap->owner != migrate->pgmap_owner))
				goto next;
			mpfn = migrate_pfn(pfn) | MIGRATE_PFN_MIGRATE;
			mpfn |= pte_write(pte) ? MIGRATE_PFN_WRITE : 0;
		}

		/* FIXME support THP */
		if (!page || !page->mapping || PageTransCompound(page)) {
			mpfn = 0;
			goto next;
		}

		/*
		 * By getting a reference on the folio we pin it and that blocks
		 * any kind of migration. Side effect is that it "freezes" the
		 * pte.
		 *
		 * We drop this reference after isolating the folio from the lru
		 * for non device folio (device folio are not on the lru and thus
		 * can't be dropped from it).
		 */
		folio = page_folio(page);
		folio_get(folio);

		/*
		 * We rely on folio_trylock() to avoid deadlock between
		 * concurrent migrations where each is waiting on the others
		 * folio lock. If we can't immediately lock the folio we fail this
		 * migration as it is only best effort anyway.
		 *
		 * If we can lock the folio it's safe to set up a migration entry
		 * now. In the common case where the folio is mapped once in a
		 * single process setting up the migration entry now is an
		 * optimisation to avoid walking the rmap later with
		 * try_to_migrate().
		 */
		if (fault_folio == folio || folio_trylock(folio)) {
			bool anon_exclusive;
			pte_t swp_pte;

			flush_cache_page(vma, addr, pte_pfn(pte));
			anon_exclusive = folio_test_anon(folio) &&
					  PageAnonExclusive(page);
			if (anon_exclusive) {
				pte = ptep_clear_flush(vma, addr, ptep);

				if (folio_try_share_anon_rmap_pte(folio, page)) {
					set_pte_at(mm, addr, ptep, pte);
					if (fault_folio != folio)
						folio_unlock(folio);
					folio_put(folio);
					mpfn = 0;
					goto next;
				}
			} else {
				pte = ptep_get_and_clear(mm, addr, ptep);
			}

			migrate->cpages++;

			/* Set the dirty flag on the folio now the pte is gone. */
			if (pte_dirty(pte))
				folio_mark_dirty(folio);

			/* Setup special migration page table entry */
			if (mpfn & MIGRATE_PFN_WRITE)
				entry = make_writable_migration_entry(
							page_to_pfn(page));
			else if (anon_exclusive)
				entry = make_readable_exclusive_migration_entry(
							page_to_pfn(page));
			else
				entry = make_readable_migration_entry(
							page_to_pfn(page));
			if (pte_present(pte)) {
				if (pte_young(pte))
					entry = make_migration_entry_young(entry);
				if (pte_dirty(pte))
					entry = make_migration_entry_dirty(entry);
			}
			swp_pte = swp_entry_to_pte(entry);
			if (pte_present(pte)) {
				if (pte_soft_dirty(pte))
					swp_pte = pte_swp_mksoft_dirty(swp_pte);
				if (pte_uffd_wp(pte))
					swp_pte = pte_swp_mkuffd_wp(swp_pte);
			} else {
				if (pte_swp_soft_dirty(pte))
					swp_pte = pte_swp_mksoft_dirty(swp_pte);
				if (pte_swp_uffd_wp(pte))
					swp_pte = pte_swp_mkuffd_wp(swp_pte);
			}
			set_pte_at(mm, addr, ptep, swp_pte);

			/*
			 * This is like regular unmap: we remove the rmap and
			 * drop the folio refcount. The folio won't be freed, as
			 * we took a reference just above.
			 */
			folio_remove_rmap_pte(folio, page, vma);
			folio_put(folio);

			if (pte_present(pte))
				unmapped++;
		} else {
			folio_put(folio);
			mpfn = 0;
		}

next:
		migrate->dst[migrate->npages] = 0;
		migrate->src[migrate->npages++] = mpfn;
	}

	/* Only flush the TLB if we actually modified any entries */
	if (unmapped)
		flush_tlb_range(walk->vma, start, end);

	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(ptep - 1, ptl);

	return 0;
}

static const struct mm_walk_ops migrate_vma_walk_ops = {
	.pmd_entry		= migrate_vma_collect_pmd,
	.pte_hole		= migrate_vma_collect_hole,
	.walk_lock		= PGWALK_RDLOCK,
};

/*
 * migrate_vma_collect() - collect pages over a range of virtual addresses
 * @migrate: migrate struct containing all migration information
 *
 * This will walk the CPU page table. For each virtual address backed by a
 * valid page, it updates the src array and takes a reference on the page, in
 * order to pin the page until we lock it and unmap it.
 */
static void migrate_vma_collect(struct migrate_vma *migrate)
{
	struct mmu_notifier_range range;

	/*
	 * Note that the pgmap_owner is passed to the mmu notifier callback so
	 * that the registered device driver can skip invalidating device
	 * private page mappings that won't be migrated.
	 */
	mmu_notifier_range_init_owner(&range, MMU_NOTIFY_MIGRATE, 0,
		migrate->vma->vm_mm, migrate->start, migrate->end,
		migrate->pgmap_owner);
	mmu_notifier_invalidate_range_start(&range);

	walk_page_range(migrate->vma->vm_mm, migrate->start, migrate->end,
			&migrate_vma_walk_ops, migrate);

	mmu_notifier_invalidate_range_end(&range);
	migrate->end = migrate->start + (migrate->npages << PAGE_SHIFT);
}

/*
 * migrate_vma_check_page() - check if page is pinned or not
 * @page: struct page to check
 *
 * Pinned pages cannot be migrated. This is the same test as in
 * folio_migrate_mapping(), except that here we allow migration of a
 * ZONE_DEVICE page.
 */
static bool migrate_vma_check_page(struct page *page, struct page *fault_page)
{
	struct folio *folio = page_folio(page);

	/*
	 * One extra ref because caller holds an extra reference, either from
	 * folio_isolate_lru() for a regular folio, or migrate_vma_collect() for
	 * a device folio.
	 */
	int extra = 1 + (page == fault_page);

	/*
	 * FIXME support THP (transparent huge page), it is bit more complex to
	 * check them than regular pages, because they can be mapped with a pmd
	 * or with a pte (split pte mapping).
	 */
	if (folio_test_large(folio))
		return false;

	/* Page from ZONE_DEVICE have one extra reference */
	if (folio_is_zone_device(folio))
		extra++;

	/* For file back page */
	if (folio_mapping(folio))
		extra += 1 + folio_has_private(folio);

	if ((folio_ref_count(folio) - extra) > folio_mapcount(folio))
		return false;

	return true;
}

/*
 * Unmaps pages for migration. Returns number of source pfns marked as
 * migrating.
 */
static unsigned long migrate_device_unmap(unsigned long *src_pfns,
					  unsigned long npages,
					  struct page *fault_page)
{
	struct folio *fault_folio = fault_page ?
		page_folio(fault_page) : NULL;
	unsigned long i, restore = 0;
	bool allow_drain = true;
	unsigned long unmapped = 0;

	lru_add_drain();

	for (i = 0; i < npages; i++) {
		struct page *page = migrate_pfn_to_page(src_pfns[i]);
		struct folio *folio;

		if (!page) {
			if (src_pfns[i] & MIGRATE_PFN_MIGRATE)
				unmapped++;
			continue;
		}

		folio =	page_folio(page);
		/* ZONE_DEVICE folios are not on LRU */
		if (!folio_is_zone_device(folio)) {
			if (!folio_test_lru(folio) && allow_drain) {
				/* Drain CPU's lru cache */
				lru_add_drain_all();
				allow_drain = false;
			}

			if (!folio_isolate_lru(folio)) {
				src_pfns[i] &= ~MIGRATE_PFN_MIGRATE;
				restore++;
				continue;
			}

			/* Drop the reference we took in collect */
			folio_put(folio);
		}

		if (folio_mapped(folio))
			try_to_migrate(folio, 0);

		if (folio_mapped(folio) ||
		    !migrate_vma_check_page(page, fault_page)) {
			if (!folio_is_zone_device(folio)) {
				folio_get(folio);
				folio_putback_lru(folio);
			}

			src_pfns[i] &= ~MIGRATE_PFN_MIGRATE;
			restore++;
			continue;
		}

		unmapped++;
	}

	for (i = 0; i < npages && restore; i++) {
		struct page *page = migrate_pfn_to_page(src_pfns[i]);
		struct folio *folio;

		if (!page || (src_pfns[i] & MIGRATE_PFN_MIGRATE))
			continue;

		folio = page_folio(page);
		remove_migration_ptes(folio, folio, 0);

		src_pfns[i] = 0;
		if (fault_folio != folio)
			folio_unlock(folio);
		folio_put(folio);
		restore--;
	}

	return unmapped;
}

/*
 * migrate_vma_unmap() - replace page mapping with special migration pte entry
 * @migrate: migrate struct containing all migration information
 *
 * Isolate pages from the LRU and replace mappings (CPU page table pte) with a
 * special migration pte entry and check if it has been pinned. Pinned pages are
 * restored because we cannot migrate them.
 *
 * This is the last step before we call the device driver callback to allocate
 * destination memory and copy contents of original page over to new page.
 */
static void migrate_vma_unmap(struct migrate_vma *migrate)
{
	migrate->cpages = migrate_device_unmap(migrate->src, migrate->npages,
					migrate->fault_page);
}

/**
 * migrate_vma_setup() - prepare to migrate a range of memory
 * @args: contains the vma, start, and pfns arrays for the migration
 *
 * Returns: negative errno on failures, 0 when 0 or more pages were migrated
 * without an error.
 *
 * Prepare to migrate a range of memory virtual address range by collecting all
 * the pages backing each virtual address in the range, saving them inside the
 * src array.  Then lock those pages and unmap them. Once the pages are locked
 * and unmapped, check whether each page is pinned or not.  Pages that aren't
 * pinned have the MIGRATE_PFN_MIGRATE flag set (by this function) in the
 * corresponding src array entry.  Then restores any pages that are pinned, by
 * remapping and unlocking those pages.
 *
 * The caller should then allocate destination memory and copy source memory to
 * it for all those entries (ie with MIGRATE_PFN_VALID and MIGRATE_PFN_MIGRATE
 * flag set).  Once these are allocated and copied, the caller must update each
 * corresponding entry in the dst array with the pfn value of the destination
 * page and with MIGRATE_PFN_VALID. Destination pages must be locked via
 * lock_page().
 *
 * Note that the caller does not have to migrate all the pages that are marked
 * with MIGRATE_PFN_MIGRATE flag in src array unless this is a migration from
 * device memory to system memory.  If the caller cannot migrate a device page
 * back to system memory, then it must return VM_FAULT_SIGBUS, which has severe
 * consequences for the userspace process, so it must be avoided if at all
 * possible.
 *
 * For empty entries inside CPU page table (pte_none() or pmd_none() is true) we
 * do set MIGRATE_PFN_MIGRATE flag inside the corresponding source array thus
 * allowing the caller to allocate device memory for those unbacked virtual
 * addresses.  For this the caller simply has to allocate device memory and
 * properly set the destination entry like for regular migration.  Note that
 * this can still fail, and thus inside the device driver you must check if the
 * migration was successful for those entries after calling migrate_vma_pages(),
 * just like for regular migration.
 *
 * After that, the callers must call migrate_vma_pages() to go over each entry
 * in the src array that has the MIGRATE_PFN_VALID and MIGRATE_PFN_MIGRATE flag
 * set. If the corresponding entry in dst array has MIGRATE_PFN_VALID flag set,
 * then migrate_vma_pages() to migrate struct page information from the source
 * struct page to the destination struct page.  If it fails to migrate the
 * struct page information, then it clears the MIGRATE_PFN_MIGRATE flag in the
 * src array.
 *
 * At this point all successfully migrated pages have an entry in the src
 * array with MIGRATE_PFN_VALID and MIGRATE_PFN_MIGRATE flag set and the dst
 * array entry with MIGRATE_PFN_VALID flag set.
 *
 * Once migrate_vma_pages() returns the caller may inspect which pages were
 * successfully migrated, and which were not.  Successfully migrated pages will
 * have the MIGRATE_PFN_MIGRATE flag set for their src array entry.
 *
 * It is safe to update device page table after migrate_vma_pages() because
 * both destination and source page are still locked, and the mmap_lock is held
 * in read mode (hence no one can unmap the range being migrated).
 *
 * Once the caller is done cleaning up things and updating its page table (if it
 * chose to do so, this is not an obligation) it finally calls
 * migrate_vma_finalize() to update the CPU page table to point to new pages
 * for successfully migrated pages or otherwise restore the CPU page table to
 * point to the original source pages.
 */
int migrate_vma_setup(struct migrate_vma *args)
{
	long nr_pages = (args->end - args->start) >> PAGE_SHIFT;

	args->start &= PAGE_MASK;
	args->end &= PAGE_MASK;
	if (!args->vma || is_vm_hugetlb_page(args->vma) ||
	    (args->vma->vm_flags & VM_SPECIAL) || vma_is_dax(args->vma))
		return -EINVAL;
	if (nr_pages <= 0)
		return -EINVAL;
	if (args->start < args->vma->vm_start ||
	    args->start >= args->vma->vm_end)
		return -EINVAL;
	if (args->end <= args->vma->vm_start || args->end > args->vma->vm_end)
		return -EINVAL;
	if (!args->src || !args->dst)
		return -EINVAL;
	if (args->fault_page && !is_device_private_page(args->fault_page))
		return -EINVAL;
	if (args->fault_page && !PageLocked(args->fault_page))
		return -EINVAL;

	memset(args->src, 0, sizeof(*args->src) * nr_pages);
	args->cpages = 0;
	args->npages = 0;

	migrate_vma_collect(args);

	if (args->cpages)
		migrate_vma_unmap(args);

	/*
	 * At this point pages are locked and unmapped, and thus they have
	 * stable content and can safely be copied to destination memory that
	 * is allocated by the drivers.
	 */
	return 0;

}
EXPORT_SYMBOL(migrate_vma_setup);

/*
 * This code closely matches the code in:
 *   __handle_mm_fault()
 *     handle_pte_fault()
 *       do_anonymous_page()
 * to map in an anonymous zero page but the struct page will be a ZONE_DEVICE
 * private or coherent page.
 */
static void migrate_vma_insert_page(struct migrate_vma *migrate,
				    unsigned long addr,
				    struct page *page,
				    unsigned long *src)
{
	struct folio *folio = page_folio(page);
	struct vm_area_struct *vma = migrate->vma;
	struct mm_struct *mm = vma->vm_mm;
	bool flush = false;
	spinlock_t *ptl;
	pte_t entry;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	pte_t orig_pte;

	/* Only allow populating anonymous memory */
	if (!vma_is_anonymous(vma))
		goto abort;

	pgdp = pgd_offset(mm, addr);
	p4dp = p4d_alloc(mm, pgdp, addr);
	if (!p4dp)
		goto abort;
	pudp = pud_alloc(mm, p4dp, addr);
	if (!pudp)
		goto abort;
	pmdp = pmd_alloc(mm, pudp, addr);
	if (!pmdp)
		goto abort;
	if (pmd_trans_huge(*pmdp) || pmd_devmap(*pmdp))
		goto abort;
	if (pte_alloc(mm, pmdp))
		goto abort;
	if (unlikely(anon_vma_prepare(vma)))
		goto abort;
	if (mem_cgroup_charge(folio, vma->vm_mm, GFP_KERNEL))
		goto abort;

	/*
	 * The memory barrier inside __folio_mark_uptodate makes sure that
	 * preceding stores to the folio contents become visible before
	 * the set_pte_at() write.
	 */
	__folio_mark_uptodate(folio);

	if (folio_is_device_private(folio)) {
		swp_entry_t swp_entry;

		if (vma->vm_flags & VM_WRITE)
			swp_entry = make_writable_device_private_entry(
						page_to_pfn(page));
		else
			swp_entry = make_readable_device_private_entry(
						page_to_pfn(page));
		entry = swp_entry_to_pte(swp_entry);
	} else {
		if (folio_is_zone_device(folio) &&
		    !folio_is_device_coherent(folio)) {
			pr_warn_once("Unsupported ZONE_DEVICE page type.\n");
			goto abort;
		}
		entry = mk_pte(page, vma->vm_page_prot);
		if (vma->vm_flags & VM_WRITE)
			entry = pte_mkwrite(pte_mkdirty(entry), vma);
	}

	ptep = pte_offset_map_lock(mm, pmdp, addr, &ptl);
	if (!ptep)
		goto abort;
	orig_pte = ptep_get(ptep);

	if (check_stable_address_space(mm))
		goto unlock_abort;

	if (pte_present(orig_pte)) {
		unsigned long pfn = pte_pfn(orig_pte);

		if (!is_zero_pfn(pfn))
			goto unlock_abort;
		flush = true;
	} else if (!pte_none(orig_pte))
		goto unlock_abort;

	/*
	 * Check for userfaultfd but do not deliver the fault. Instead,
	 * just back off.
	 */
	if (userfaultfd_missing(vma))
		goto unlock_abort;

	inc_mm_counter(mm, MM_ANONPAGES);
	folio_add_new_anon_rmap(folio, vma, addr, RMAP_EXCLUSIVE);
	if (!folio_is_zone_device(folio))
		folio_add_lru_vma(folio, vma);
	folio_get(folio);

	if (flush) {
		flush_cache_page(vma, addr, pte_pfn(orig_pte));
		ptep_clear_flush(vma, addr, ptep);
	}
	set_pte_at(mm, addr, ptep, entry);
	update_mmu_cache(vma, addr, ptep);

	pte_unmap_unlock(ptep, ptl);
	*src = MIGRATE_PFN_MIGRATE;
	return;

unlock_abort:
	pte_unmap_unlock(ptep, ptl);
abort:
	*src &= ~MIGRATE_PFN_MIGRATE;
}

static void __migrate_device_pages(unsigned long *src_pfns,
				unsigned long *dst_pfns, unsigned long npages,
				struct migrate_vma *migrate)
{
	struct mmu_notifier_range range;
	unsigned long i;
	bool notified = false;

	for (i = 0; i < npages; i++) {
		struct page *newpage = migrate_pfn_to_page(dst_pfns[i]);
		struct page *page = migrate_pfn_to_page(src_pfns[i]);
		struct address_space *mapping;
		struct folio *newfolio, *folio;
		int r, extra_cnt = 0;

		if (!newpage) {
			src_pfns[i] &= ~MIGRATE_PFN_MIGRATE;
			continue;
		}

		if (!page) {
			unsigned long addr;

			if (!(src_pfns[i] & MIGRATE_PFN_MIGRATE))
				continue;

			/*
			 * The only time there is no vma is when called from
			 * migrate_device_coherent_folio(). However this isn't
			 * called if the page could not be unmapped.
			 */
			VM_BUG_ON(!migrate);
			addr = migrate->start + i*PAGE_SIZE;
			if (!notified) {
				notified = true;

				mmu_notifier_range_init_owner(&range,
					MMU_NOTIFY_MIGRATE, 0,
					migrate->vma->vm_mm, addr, migrate->end,
					migrate->pgmap_owner);
				mmu_notifier_invalidate_range_start(&range);
			}
			migrate_vma_insert_page(migrate, addr, newpage,
						&src_pfns[i]);
			continue;
		}

		newfolio = page_folio(newpage);
		folio = page_folio(page);
		mapping = folio_mapping(folio);

		if (folio_is_device_private(newfolio) ||
		    folio_is_device_coherent(newfolio)) {
			if (mapping) {
				/*
				 * For now only support anonymous memory migrating to
				 * device private or coherent memory.
				 *
				 * Try to get rid of swap cache if possible.
				 */
				if (!folio_test_anon(folio) ||
				    !folio_free_swap(folio)) {
					src_pfns[i] &= ~MIGRATE_PFN_MIGRATE;
					continue;
				}
			}
		} else if (folio_is_zone_device(newfolio)) {
			/*
			 * Other types of ZONE_DEVICE page are not supported.
			 */
			src_pfns[i] &= ~MIGRATE_PFN_MIGRATE;
			continue;
		}

		BUG_ON(folio_test_writeback(folio));

		if (migrate && migrate->fault_page == page)
			extra_cnt = 1;
		r = folio_migrate_mapping(mapping, newfolio, folio, extra_cnt);
		if (r != MIGRATEPAGE_SUCCESS)
			src_pfns[i] &= ~MIGRATE_PFN_MIGRATE;
		else
			folio_migrate_flags(newfolio, folio);
	}

	if (notified)
		mmu_notifier_invalidate_range_end(&range);
}

/**
 * migrate_device_pages() - migrate meta-data from src page to dst page
 * @src_pfns: src_pfns returned from migrate_device_range()
 * @dst_pfns: array of pfns allocated by the driver to migrate memory to
 * @npages: number of pages in the range
 *
 * Equivalent to migrate_vma_pages(). This is called to migrate struct page
 * meta-data from source struct page to destination.
 */
void migrate_device_pages(unsigned long *src_pfns, unsigned long *dst_pfns,
			unsigned long npages)
{
	__migrate_device_pages(src_pfns, dst_pfns, npages, NULL);
}
EXPORT_SYMBOL(migrate_device_pages);

/**
 * migrate_vma_pages() - migrate meta-data from src page to dst page
 * @migrate: migrate struct containing all migration information
 *
 * This migrates struct page meta-data from source struct page to destination
 * struct page. This effectively finishes the migration from source page to the
 * destination page.
 */
void migrate_vma_pages(struct migrate_vma *migrate)
{
	__migrate_device_pages(migrate->src, migrate->dst, migrate->npages, migrate);
}
EXPORT_SYMBOL(migrate_vma_pages);

static void __migrate_device_finalize(unsigned long *src_pfns,
				      unsigned long *dst_pfns,
				      unsigned long npages,
				      struct page *fault_page)
{
	struct folio *fault_folio = fault_page ?
		page_folio(fault_page) : NULL;
	unsigned long i;

	for (i = 0; i < npages; i++) {
		struct folio *dst = NULL, *src = NULL;
		struct page *newpage = migrate_pfn_to_page(dst_pfns[i]);
		struct page *page = migrate_pfn_to_page(src_pfns[i]);

		if (newpage)
			dst = page_folio(newpage);

		if (!page) {
			if (dst) {
				WARN_ON_ONCE(fault_folio == dst);
				folio_unlock(dst);
				folio_put(dst);
			}
			continue;
		}

		src = page_folio(page);

		if (!(src_pfns[i] & MIGRATE_PFN_MIGRATE) || !dst) {
			if (dst) {
				WARN_ON_ONCE(fault_folio == dst);
				folio_unlock(dst);
				folio_put(dst);
			}
			dst = src;
		}

		if (!folio_is_zone_device(dst))
			folio_add_lru(dst);
		remove_migration_ptes(src, dst, 0);
		if (fault_folio != src)
			folio_unlock(src);
		folio_put(src);

		if (dst != src) {
			WARN_ON_ONCE(fault_folio == dst);
			folio_unlock(dst);
			folio_put(dst);
		}
	}
}

/*
 * migrate_device_finalize() - complete page migration
 * @src_pfns: src_pfns returned from migrate_device_range()
 * @dst_pfns: array of pfns allocated by the driver to migrate memory to
 * @npages: number of pages in the range
 *
 * Completes migration of the page by removing special migration entries.
 * Drivers must ensure copying of page data is complete and visible to the CPU
 * before calling this.
 */
void migrate_device_finalize(unsigned long *src_pfns,
			     unsigned long *dst_pfns, unsigned long npages)
{
	return __migrate_device_finalize(src_pfns, dst_pfns, npages, NULL);
}
EXPORT_SYMBOL(migrate_device_finalize);

/**
 * migrate_vma_finalize() - restore CPU page table entry
 * @migrate: migrate struct containing all migration information
 *
 * This replaces the special migration pte entry with either a mapping to the
 * new page if migration was successful for that page, or to the original page
 * otherwise.
 *
 * This also unlocks the pages and puts them back on the lru, or drops the extra
 * refcount, for device pages.
 */
void migrate_vma_finalize(struct migrate_vma *migrate)
{
	__migrate_device_finalize(migrate->src, migrate->dst, migrate->npages,
				  migrate->fault_page);
}
EXPORT_SYMBOL(migrate_vma_finalize);

static unsigned long migrate_device_pfn_lock(unsigned long pfn)
{
	struct folio *folio;

	folio = folio_get_nontail_page(pfn_to_page(pfn));
	if (!folio)
		return 0;

	if (!folio_trylock(folio)) {
		folio_put(folio);
		return 0;
	}

	return migrate_pfn(pfn) | MIGRATE_PFN_MIGRATE;
}

/**
 * migrate_device_range() - migrate device private pfns to normal memory.
 * @src_pfns: array large enough to hold migrating source device private pfns.
 * @start: starting pfn in the range to migrate.
 * @npages: number of pages to migrate.
 *
 * migrate_vma_setup() is similar in concept to migrate_vma_setup() except that
 * instead of looking up pages based on virtual address mappings a range of
 * device pfns that should be migrated to system memory is used instead.
 *
 * This is useful when a driver needs to free device memory but doesn't know the
 * virtual mappings of every page that may be in device memory. For example this
 * is often the case when a driver is being unloaded or unbound from a device.
 *
 * Like migrate_vma_setup() this function will take a reference and lock any
 * migrating pages that aren't free before unmapping them. Drivers may then
 * allocate destination pages and start copying data from the device to CPU
 * memory before calling migrate_device_pages().
 */
int migrate_device_range(unsigned long *src_pfns, unsigned long start,
			unsigned long npages)
{
	unsigned long i, pfn;

	for (pfn = start, i = 0; i < npages; pfn++, i++)
		src_pfns[i] = migrate_device_pfn_lock(pfn);

	migrate_device_unmap(src_pfns, npages, NULL);

	return 0;
}
EXPORT_SYMBOL(migrate_device_range);

/**
 * migrate_device_pfns() - migrate device private pfns to normal memory.
 * @src_pfns: pre-popluated array of source device private pfns to migrate.
 * @npages: number of pages to migrate.
 *
 * Similar to migrate_device_range() but supports non-contiguous pre-popluated
 * array of device pages to migrate.
 */
int migrate_device_pfns(unsigned long *src_pfns, unsigned long npages)
{
	unsigned long i;

	for (i = 0; i < npages; i++)
		src_pfns[i] = migrate_device_pfn_lock(src_pfns[i]);

	migrate_device_unmap(src_pfns, npages, NULL);

	return 0;
}
EXPORT_SYMBOL(migrate_device_pfns);

/*
 * Migrate a device coherent folio back to normal memory. The caller should have
 * a reference on folio which will be copied to the new folio if migration is
 * successful or dropped on failure.
 */
int migrate_device_coherent_folio(struct folio *folio)
{
	unsigned long src_pfn, dst_pfn = 0;
	struct folio *dfolio;

	WARN_ON_ONCE(folio_test_large(folio));

	folio_lock(folio);
	src_pfn = migrate_pfn(folio_pfn(folio)) | MIGRATE_PFN_MIGRATE;

	/*
	 * We don't have a VMA and don't need to walk the page tables to find
	 * the source folio. So call migrate_vma_unmap() directly to unmap the
	 * folio as migrate_vma_setup() will fail if args.vma == NULL.
	 */
	migrate_device_unmap(&src_pfn, 1, NULL);
	if (!(src_pfn & MIGRATE_PFN_MIGRATE))
		return -EBUSY;

	dfolio = folio_alloc(GFP_USER | __GFP_NOWARN, 0);
	if (dfolio) {
		folio_lock(dfolio);
		dst_pfn = migrate_pfn(folio_pfn(dfolio));
	}

	migrate_device_pages(&src_pfn, &dst_pfn, 1);
	if (src_pfn & MIGRATE_PFN_MIGRATE)
		folio_copy(dfolio, folio);
	migrate_device_finalize(&src_pfn, &dst_pfn, 1);

	if (src_pfn & MIGRATE_PFN_MIGRATE)
		return 0;
	return -EBUSY;
}
