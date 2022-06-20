// SPDX-License-Identifier: GPL-2.0
/*
 * Device Memory Migration functionality.
 *
 * Originally written by Jérôme Glisse.
 */
#include <linux/export.h>
#include <linux/memremap.h>
#include <linux/migrate.h>
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
	struct vm_area_struct *vma = walk->vma;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr = start, unmapped = 0;
	spinlock_t *ptl;
	pte_t *ptep;

again:
	if (pmd_none(*pmdp))
		return migrate_vma_collect_hole(start, end, -1, walk);

	if (pmd_trans_huge(*pmdp)) {
		struct page *page;

		ptl = pmd_lock(mm, pmdp);
		if (unlikely(!pmd_trans_huge(*pmdp))) {
			spin_unlock(ptl);
			goto again;
		}

		page = pmd_page(*pmdp);
		if (is_huge_zero_page(page)) {
			spin_unlock(ptl);
			split_huge_pmd(vma, pmdp, addr);
			if (pmd_trans_unstable(pmdp))
				return migrate_vma_collect_skip(start, end,
								walk);
		} else {
			int ret;

			get_page(page);
			spin_unlock(ptl);
			if (unlikely(!trylock_page(page)))
				return migrate_vma_collect_skip(start, end,
								walk);
			ret = split_huge_page(page);
			unlock_page(page);
			put_page(page);
			if (ret)
				return migrate_vma_collect_skip(start, end,
								walk);
			if (pmd_none(*pmdp))
				return migrate_vma_collect_hole(start, end, -1,
								walk);
		}
	}

	if (unlikely(pmd_bad(*pmdp)))
		return migrate_vma_collect_skip(start, end, walk);

	ptep = pte_offset_map_lock(mm, pmdp, addr, &ptl);
	arch_enter_lazy_mmu_mode();

	for (; addr < end; addr += PAGE_SIZE, ptep++) {
		unsigned long mpfn = 0, pfn;
		struct page *page;
		swp_entry_t entry;
		pte_t pte;

		pte = *ptep;

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
			if (!(migrate->flags & MIGRATE_VMA_SELECT_SYSTEM))
				goto next;
			pfn = pte_pfn(pte);
			if (is_zero_pfn(pfn)) {
				mpfn = MIGRATE_PFN_MIGRATE;
				migrate->cpages++;
				goto next;
			}
			page = vm_normal_page(migrate->vma, addr, pte);
			mpfn = migrate_pfn(pfn) | MIGRATE_PFN_MIGRATE;
			mpfn |= pte_write(pte) ? MIGRATE_PFN_WRITE : 0;
		}

		/* FIXME support THP */
		if (!page || !page->mapping || PageTransCompound(page)) {
			mpfn = 0;
			goto next;
		}

		/*
		 * By getting a reference on the page we pin it and that blocks
		 * any kind of migration. Side effect is that it "freezes" the
		 * pte.
		 *
		 * We drop this reference after isolating the page from the lru
		 * for non device page (device page are not on the lru and thus
		 * can't be dropped from it).
		 */
		get_page(page);

		/*
		 * Optimize for the common case where page is only mapped once
		 * in one process. If we can lock the page, then we can safely
		 * set up a special migration page table entry now.
		 */
		if (trylock_page(page)) {
			bool anon_exclusive;
			pte_t swp_pte;

			anon_exclusive = PageAnon(page) && PageAnonExclusive(page);
			if (anon_exclusive) {
				flush_cache_page(vma, addr, pte_pfn(*ptep));
				ptep_clear_flush(vma, addr, ptep);

				if (page_try_share_anon_rmap(page)) {
					set_pte_at(mm, addr, ptep, pte);
					unlock_page(page);
					put_page(page);
					mpfn = 0;
					goto next;
				}
			} else {
				ptep_get_and_clear(mm, addr, ptep);
			}

			migrate->cpages++;

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
			 * drop page refcount. Page won't be freed, as we took
			 * a reference just above.
			 */
			page_remove_rmap(page, vma, false);
			put_page(page);

			if (pte_present(pte))
				unmapped++;
		} else {
			put_page(page);
			mpfn = 0;
		}

next:
		migrate->dst[migrate->npages] = 0;
		migrate->src[migrate->npages++] = mpfn;
	}
	arch_leave_lazy_mmu_mode();
	pte_unmap_unlock(ptep - 1, ptl);

	/* Only flush the TLB if we actually modified any entries */
	if (unmapped)
		flush_tlb_range(walk->vma, start, end);

	return 0;
}

static const struct mm_walk_ops migrate_vma_walk_ops = {
	.pmd_entry		= migrate_vma_collect_pmd,
	.pte_hole		= migrate_vma_collect_hole,
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
		migrate->vma, migrate->vma->vm_mm, migrate->start, migrate->end,
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
static bool migrate_vma_check_page(struct page *page)
{
	/*
	 * One extra ref because caller holds an extra reference, either from
	 * isolate_lru_page() for a regular page, or migrate_vma_collect() for
	 * a device page.
	 */
	int extra = 1;

	/*
	 * FIXME support THP (transparent huge page), it is bit more complex to
	 * check them than regular pages, because they can be mapped with a pmd
	 * or with a pte (split pte mapping).
	 */
	if (PageCompound(page))
		return false;

	/* Page from ZONE_DEVICE have one extra reference */
	if (is_zone_device_page(page))
		extra++;

	/* For file back page */
	if (page_mapping(page))
		extra += 1 + page_has_private(page);

	if ((page_count(page) - extra) > page_mapcount(page))
		return false;

	return true;
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
	const unsigned long npages = migrate->npages;
	unsigned long i, restore = 0;
	bool allow_drain = true;

	lru_add_drain();

	for (i = 0; i < npages; i++) {
		struct page *page = migrate_pfn_to_page(migrate->src[i]);
		struct folio *folio;

		if (!page)
			continue;

		/* ZONE_DEVICE pages are not on LRU */
		if (!is_zone_device_page(page)) {
			if (!PageLRU(page) && allow_drain) {
				/* Drain CPU's pagevec */
				lru_add_drain_all();
				allow_drain = false;
			}

			if (isolate_lru_page(page)) {
				migrate->src[i] &= ~MIGRATE_PFN_MIGRATE;
				migrate->cpages--;
				restore++;
				continue;
			}

			/* Drop the reference we took in collect */
			put_page(page);
		}

		folio = page_folio(page);
		if (folio_mapped(folio))
			try_to_migrate(folio, 0);

		if (page_mapped(page) || !migrate_vma_check_page(page)) {
			if (!is_zone_device_page(page)) {
				get_page(page);
				putback_lru_page(page);
			}

			migrate->src[i] &= ~MIGRATE_PFN_MIGRATE;
			migrate->cpages--;
			restore++;
			continue;
		}
	}

	for (i = 0; i < npages && restore; i++) {
		struct page *page = migrate_pfn_to_page(migrate->src[i]);
		struct folio *folio;

		if (!page || (migrate->src[i] & MIGRATE_PFN_MIGRATE))
			continue;

		folio = page_folio(page);
		remove_migration_ptes(folio, folio, false);

		migrate->src[i] = 0;
		folio_unlock(folio);
		folio_put(folio);
		restore--;
	}
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
 * private page.
 */
static void migrate_vma_insert_page(struct migrate_vma *migrate,
				    unsigned long addr,
				    struct page *page,
				    unsigned long *src)
{
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

	/*
	 * Use pte_alloc() instead of pte_alloc_map().  We can't run
	 * pte_offset_map() on pmds where a huge pmd might be created
	 * from a different thread.
	 *
	 * pte_alloc_map() is safe to use under mmap_write_lock(mm) or when
	 * parallel threads are excluded by other means.
	 *
	 * Here we only have mmap_read_lock(mm).
	 */
	if (pte_alloc(mm, pmdp))
		goto abort;

	/* See the comment in pte_alloc_one_map() */
	if (unlikely(pmd_trans_unstable(pmdp)))
		goto abort;

	if (unlikely(anon_vma_prepare(vma)))
		goto abort;
	if (mem_cgroup_charge(page_folio(page), vma->vm_mm, GFP_KERNEL))
		goto abort;

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);

	if (is_device_private_page(page)) {
		swp_entry_t swp_entry;

		if (vma->vm_flags & VM_WRITE)
			swp_entry = make_writable_device_private_entry(
						page_to_pfn(page));
		else
			swp_entry = make_readable_device_private_entry(
						page_to_pfn(page));
		entry = swp_entry_to_pte(swp_entry);
	} else {
		/*
		 * For now we only support migrating to un-addressable device
		 * memory.
		 */
		if (is_zone_device_page(page)) {
			pr_warn_once("Unsupported ZONE_DEVICE page type.\n");
			goto abort;
		}
		entry = mk_pte(page, vma->vm_page_prot);
		if (vma->vm_flags & VM_WRITE)
			entry = pte_mkwrite(pte_mkdirty(entry));
	}

	ptep = pte_offset_map_lock(mm, pmdp, addr, &ptl);

	if (check_stable_address_space(mm))
		goto unlock_abort;

	if (pte_present(*ptep)) {
		unsigned long pfn = pte_pfn(*ptep);

		if (!is_zero_pfn(pfn))
			goto unlock_abort;
		flush = true;
	} else if (!pte_none(*ptep))
		goto unlock_abort;

	/*
	 * Check for userfaultfd but do not deliver the fault. Instead,
	 * just back off.
	 */
	if (userfaultfd_missing(vma))
		goto unlock_abort;

	inc_mm_counter(mm, MM_ANONPAGES);
	page_add_new_anon_rmap(page, vma, addr);
	if (!is_zone_device_page(page))
		lru_cache_add_inactive_or_unevictable(page, vma);
	get_page(page);

	if (flush) {
		flush_cache_page(vma, addr, pte_pfn(*ptep));
		ptep_clear_flush_notify(vma, addr, ptep);
		set_pte_at_notify(mm, addr, ptep, entry);
		update_mmu_cache(vma, addr, ptep);
	} else {
		/* No need to invalidate - it was non-present before */
		set_pte_at(mm, addr, ptep, entry);
		update_mmu_cache(vma, addr, ptep);
	}

	pte_unmap_unlock(ptep, ptl);
	*src = MIGRATE_PFN_MIGRATE;
	return;

unlock_abort:
	pte_unmap_unlock(ptep, ptl);
abort:
	*src &= ~MIGRATE_PFN_MIGRATE;
}

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
	const unsigned long npages = migrate->npages;
	const unsigned long start = migrate->start;
	struct mmu_notifier_range range;
	unsigned long addr, i;
	bool notified = false;

	for (i = 0, addr = start; i < npages; addr += PAGE_SIZE, i++) {
		struct page *newpage = migrate_pfn_to_page(migrate->dst[i]);
		struct page *page = migrate_pfn_to_page(migrate->src[i]);
		struct address_space *mapping;
		int r;

		if (!newpage) {
			migrate->src[i] &= ~MIGRATE_PFN_MIGRATE;
			continue;
		}

		if (!page) {
			if (!(migrate->src[i] & MIGRATE_PFN_MIGRATE))
				continue;
			if (!notified) {
				notified = true;

				mmu_notifier_range_init_owner(&range,
					MMU_NOTIFY_MIGRATE, 0, migrate->vma,
					migrate->vma->vm_mm, addr, migrate->end,
					migrate->pgmap_owner);
				mmu_notifier_invalidate_range_start(&range);
			}
			migrate_vma_insert_page(migrate, addr, newpage,
						&migrate->src[i]);
			continue;
		}

		mapping = page_mapping(page);

		if (is_device_private_page(newpage)) {
			/*
			 * For now only support private anonymous when migrating
			 * to un-addressable device memory.
			 */
			if (mapping) {
				migrate->src[i] &= ~MIGRATE_PFN_MIGRATE;
				continue;
			}
		} else if (is_zone_device_page(newpage)) {
			/*
			 * Other types of ZONE_DEVICE page are not supported.
			 */
			migrate->src[i] &= ~MIGRATE_PFN_MIGRATE;
			continue;
		}

		r = migrate_page(mapping, newpage, page, MIGRATE_SYNC_NO_COPY);
		if (r != MIGRATEPAGE_SUCCESS)
			migrate->src[i] &= ~MIGRATE_PFN_MIGRATE;
	}

	/*
	 * No need to double call mmu_notifier->invalidate_range() callback as
	 * the above ptep_clear_flush_notify() inside migrate_vma_insert_page()
	 * did already call it.
	 */
	if (notified)
		mmu_notifier_invalidate_range_only_end(&range);
}
EXPORT_SYMBOL(migrate_vma_pages);

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
	const unsigned long npages = migrate->npages;
	unsigned long i;

	for (i = 0; i < npages; i++) {
		struct folio *dst, *src;
		struct page *newpage = migrate_pfn_to_page(migrate->dst[i]);
		struct page *page = migrate_pfn_to_page(migrate->src[i]);

		if (!page) {
			if (newpage) {
				unlock_page(newpage);
				put_page(newpage);
			}
			continue;
		}

		if (!(migrate->src[i] & MIGRATE_PFN_MIGRATE) || !newpage) {
			if (newpage) {
				unlock_page(newpage);
				put_page(newpage);
			}
			newpage = page;
		}

		src = page_folio(page);
		dst = page_folio(newpage);
		remove_migration_ptes(src, dst, false);
		folio_unlock(src);

		if (is_zone_device_page(page))
			put_page(page);
		else
			putback_lru_page(page);

		if (newpage != page) {
			unlock_page(newpage);
			if (is_zone_device_page(newpage))
				put_page(newpage);
			else
				putback_lru_page(newpage);
		}
	}
}
EXPORT_SYMBOL(migrate_vma_finalize);
