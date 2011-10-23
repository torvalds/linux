/*
 * Memory Migration functionality - linux/mm/migration.c
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
#include <linux/module.h>
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
#include <linux/memcontrol.h>
#include <linux/syscalls.h>
#include <linux/hugetlb.h>
#include <linux/gfp.h>

#include <asm/tlbflush.h>

#include "internal.h"

#define lru_to_page(_head) (list_entry((_head)->prev, struct page, lru))

/*
 * migrate_prep() needs to be called before we start compiling a list of pages
 * to be migrated using isolate_lru_page(). If scheduling work on other CPUs is
 * undesirable, use migrate_prep_local()
 */
int migrate_prep(void)
{
	/*
	 * Clear the LRU lists so pages can be isolated.
	 * Note that pages may be moved off the LRU after we have
	 * drained them. Those pages will fail to migrate like other
	 * pages that may be busy.
	 */
	lru_add_drain_all();

	return 0;
}

/* Do the necessary work of migrate_prep but not if it involves other CPUs */
int migrate_prep_local(void)
{
	lru_add_drain();

	return 0;
}

/*
 * Add isolated pages on the list back to the LRU under page lock
 * to avoid leaking evictable pages back onto unevictable list.
 */
void putback_lru_pages(struct list_head *l)
{
	struct page *page;
	struct page *page2;

	list_for_each_entry_safe(page, page2, l, lru) {
		list_del(&page->lru);
		dec_zone_page_state(page, NR_ISOLATED_ANON +
				page_is_file_cache(page));
		putback_lru_page(page);
	}
}

/*
 * Restore a potential migration pte to a working pte entry
 */
static int remove_migration_pte(struct page *new, struct vm_area_struct *vma,
				 unsigned long addr, void *old)
{
	struct mm_struct *mm = vma->vm_mm;
	swp_entry_t entry;
 	pgd_t *pgd;
 	pud_t *pud;
 	pmd_t *pmd;
	pte_t *ptep, pte;
 	spinlock_t *ptl;

	if (unlikely(PageHuge(new))) {
		ptep = huge_pte_offset(mm, addr);
		if (!ptep)
			goto out;
		ptl = &mm->page_table_lock;
	} else {
		pgd = pgd_offset(mm, addr);
		if (!pgd_present(*pgd))
			goto out;

		pud = pud_offset(pgd, addr);
		if (!pud_present(*pud))
			goto out;

		pmd = pmd_offset(pud, addr);
		if (pmd_trans_huge(*pmd))
			goto out;
		if (!pmd_present(*pmd))
			goto out;

		ptep = pte_offset_map(pmd, addr);

		/*
		 * Peek to check is_swap_pte() before taking ptlock?  No, we
		 * can race mremap's move_ptes(), which skips anon_vma lock.
		 */

		ptl = pte_lockptr(mm, pmd);
	}

 	spin_lock(ptl);
	pte = *ptep;
	if (!is_swap_pte(pte))
		goto unlock;

	entry = pte_to_swp_entry(pte);

	if (!is_migration_entry(entry) ||
	    migration_entry_to_page(entry) != old)
		goto unlock;

	get_page(new);
	pte = pte_mkold(mk_pte(new, vma->vm_page_prot));
	if (is_write_migration_entry(entry))
		pte = pte_mkwrite(pte);
#ifdef CONFIG_HUGETLB_PAGE
	if (PageHuge(new))
		pte = pte_mkhuge(pte);
#endif
	flush_cache_page(vma, addr, pte_pfn(pte));
	set_pte_at(mm, addr, ptep, pte);

	if (PageHuge(new)) {
		if (PageAnon(new))
			hugepage_add_anon_rmap(new, vma, addr);
		else
			page_dup_rmap(new);
	} else if (PageAnon(new))
		page_add_anon_rmap(new, vma, addr);
	else
		page_add_file_rmap(new);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, addr, ptep);
unlock:
	pte_unmap_unlock(ptep, ptl);
out:
	return SWAP_AGAIN;
}

/*
 * Get rid of all migration entries and replace them by
 * references to the indicated page.
 */
static void remove_migration_ptes(struct page *old, struct page *new)
{
	rmap_walk(new, remove_migration_pte, old);
}

/*
 * Something used the pte of a page under migration. We need to
 * get to the page and wait until migration is finished.
 * When we return from this function the fault will be retried.
 *
 * This function is called from do_swap_page().
 */
void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
				unsigned long address)
{
	pte_t *ptep, pte;
	spinlock_t *ptl;
	swp_entry_t entry;
	struct page *page;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!is_swap_pte(pte))
		goto out;

	entry = pte_to_swp_entry(pte);
	if (!is_migration_entry(entry))
		goto out;

	page = migration_entry_to_page(entry);

	/*
	 * Once radix-tree replacement of page migration started, page_count
	 * *must* be zero. And, we don't want to call wait_on_page_locked()
	 * against a page without get_page().
	 * So, we use get_page_unless_zero(), here. Even failed, page fault
	 * will occur again.
	 */
	if (!get_page_unless_zero(page))
		goto out;
	pte_unmap_unlock(ptep, ptl);
	wait_on_page_locked(page);
	put_page(page);
	return;
out:
	pte_unmap_unlock(ptep, ptl);
}

/*
 * Replace the page in the mapping.
 *
 * The number of remaining references must be:
 * 1 for anonymous pages without a mapping
 * 2 for pages with a mapping
 * 3 for pages with a mapping and PagePrivate/PagePrivate2 set.
 */
static int migrate_page_move_mapping(struct address_space *mapping,
		struct page *newpage, struct page *page)
{
	int expected_count;
	void **pslot;

	if (!mapping) {
		/* Anonymous page without mapping */
		if (page_count(page) != 1)
			return -EAGAIN;
		return 0;
	}

	spin_lock_irq(&mapping->tree_lock);

	pslot = radix_tree_lookup_slot(&mapping->page_tree,
 					page_index(page));

	expected_count = 2 + page_has_private(page);
	if (page_count(page) != expected_count ||
		radix_tree_deref_slot_protected(pslot, &mapping->tree_lock) != page) {
		spin_unlock_irq(&mapping->tree_lock);
		return -EAGAIN;
	}

	if (!page_freeze_refs(page, expected_count)) {
		spin_unlock_irq(&mapping->tree_lock);
		return -EAGAIN;
	}

	/*
	 * Now we know that no one else is looking at the page.
	 */
	get_page(newpage);	/* add cache reference */
	if (PageSwapCache(page)) {
		SetPageSwapCache(newpage);
		set_page_private(newpage, page_private(page));
	}

	radix_tree_replace_slot(pslot, newpage);

	page_unfreeze_refs(page, expected_count);
	/*
	 * Drop cache reference from old page.
	 * We know this isn't the last reference.
	 */
	__put_page(page);

	/*
	 * If moved to a different zone then also account
	 * the page for that zone. Other VM counters will be
	 * taken care of when we establish references to the
	 * new page and drop references to the old page.
	 *
	 * Note that anonymous pages are accounted for
	 * via NR_FILE_PAGES and NR_ANON_PAGES if they
	 * are mapped to swap space.
	 */
	__dec_zone_page_state(page, NR_FILE_PAGES);
	__inc_zone_page_state(newpage, NR_FILE_PAGES);
	if (!PageSwapCache(page) && PageSwapBacked(page)) {
		__dec_zone_page_state(page, NR_SHMEM);
		__inc_zone_page_state(newpage, NR_SHMEM);
	}
	spin_unlock_irq(&mapping->tree_lock);

	return 0;
}

/*
 * The expected number of remaining references is the same as that
 * of migrate_page_move_mapping().
 */
int migrate_huge_page_move_mapping(struct address_space *mapping,
				   struct page *newpage, struct page *page)
{
	int expected_count;
	void **pslot;

	if (!mapping) {
		if (page_count(page) != 1)
			return -EAGAIN;
		return 0;
	}

	spin_lock_irq(&mapping->tree_lock);

	pslot = radix_tree_lookup_slot(&mapping->page_tree,
					page_index(page));

	expected_count = 2 + page_has_private(page);
	if (page_count(page) != expected_count ||
		radix_tree_deref_slot_protected(pslot, &mapping->tree_lock) != page) {
		spin_unlock_irq(&mapping->tree_lock);
		return -EAGAIN;
	}

	if (!page_freeze_refs(page, expected_count)) {
		spin_unlock_irq(&mapping->tree_lock);
		return -EAGAIN;
	}

	get_page(newpage);

	radix_tree_replace_slot(pslot, newpage);

	page_unfreeze_refs(page, expected_count);

	__put_page(page);

	spin_unlock_irq(&mapping->tree_lock);
	return 0;
}

/*
 * Copy the page to its new location
 */
void migrate_page_copy(struct page *newpage, struct page *page)
{
	if (PageHuge(page))
		copy_huge_page(newpage, page);
	else
		copy_highpage(newpage, page);

	if (PageError(page))
		SetPageError(newpage);
	if (PageReferenced(page))
		SetPageReferenced(newpage);
	if (PageUptodate(page))
		SetPageUptodate(newpage);
	if (TestClearPageActive(page)) {
		VM_BUG_ON(PageUnevictable(page));
		SetPageActive(newpage);
	} else if (TestClearPageUnevictable(page))
		SetPageUnevictable(newpage);
	if (PageChecked(page))
		SetPageChecked(newpage);
	if (PageMappedToDisk(page))
		SetPageMappedToDisk(newpage);

	if (PageDirty(page)) {
		clear_page_dirty_for_io(page);
		/*
		 * Want to mark the page and the radix tree as dirty, and
		 * redo the accounting that clear_page_dirty_for_io undid,
		 * but we can't use set_page_dirty because that function
		 * is actually a signal that all of the page has become dirty.
		 * Whereas only part of our page may be dirty.
		 */
		__set_page_dirty_nobuffers(newpage);
 	}

	mlock_migrate_page(newpage, page);
	ksm_migrate_page(newpage, page);

	ClearPageSwapCache(page);
	ClearPagePrivate(page);
	set_page_private(page, 0);
	page->mapping = NULL;

	/*
	 * If any waiters have accumulated on the new page then
	 * wake them up.
	 */
	if (PageWriteback(newpage))
		end_page_writeback(newpage);
}

/************************************************************
 *                    Migration functions
 ***********************************************************/

/* Always fail migration. Used for mappings that are not movable */
int fail_migrate_page(struct address_space *mapping,
			struct page *newpage, struct page *page)
{
	return -EIO;
}
EXPORT_SYMBOL(fail_migrate_page);

/*
 * Common logic to directly migrate a single page suitable for
 * pages that do not use PagePrivate/PagePrivate2.
 *
 * Pages are locked upon entry and exit.
 */
int migrate_page(struct address_space *mapping,
		struct page *newpage, struct page *page)
{
	int rc;

	BUG_ON(PageWriteback(page));	/* Writeback must be complete */

	rc = migrate_page_move_mapping(mapping, newpage, page);

	if (rc)
		return rc;

	migrate_page_copy(newpage, page);
	return 0;
}
EXPORT_SYMBOL(migrate_page);

#ifdef CONFIG_BLOCK
/*
 * Migration function for pages with buffers. This function can only be used
 * if the underlying filesystem guarantees that no other references to "page"
 * exist.
 */
int buffer_migrate_page(struct address_space *mapping,
		struct page *newpage, struct page *page)
{
	struct buffer_head *bh, *head;
	int rc;

	if (!page_has_buffers(page))
		return migrate_page(mapping, newpage, page);

	head = page_buffers(page);

	rc = migrate_page_move_mapping(mapping, newpage, page);

	if (rc)
		return rc;

	bh = head;
	do {
		get_bh(bh);
		lock_buffer(bh);
		bh = bh->b_this_page;

	} while (bh != head);

	ClearPagePrivate(page);
	set_page_private(newpage, page_private(page));
	set_page_private(page, 0);
	put_page(page);
	get_page(newpage);

	bh = head;
	do {
		set_bh_page(bh, newpage, bh_offset(bh));
		bh = bh->b_this_page;

	} while (bh != head);

	SetPagePrivate(newpage);

	migrate_page_copy(newpage, page);

	bh = head;
	do {
		unlock_buffer(bh);
 		put_bh(bh);
		bh = bh->b_this_page;

	} while (bh != head);

	return 0;
}
EXPORT_SYMBOL(buffer_migrate_page);
#endif

/*
 * Writeback a page to clean the dirty state
 */
static int writeout(struct address_space *mapping, struct page *page)
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

	if (!clear_page_dirty_for_io(page))
		/* Someone else already triggered a write */
		return -EAGAIN;

	/*
	 * A dirty page may imply that the underlying filesystem has
	 * the page on some queue. So the page must be clean for
	 * migration. Writeout may mean we loose the lock and the
	 * page state is no longer what we checked for earlier.
	 * At this point we know that the migration attempt cannot
	 * be successful.
	 */
	remove_migration_ptes(page, page);

	rc = mapping->a_ops->writepage(page, &wbc);

	if (rc != AOP_WRITEPAGE_ACTIVATE)
		/* unlocked. Relock */
		lock_page(page);

	return (rc < 0) ? -EIO : -EAGAIN;
}

/*
 * Default handling if a filesystem does not provide a migration function.
 */
static int fallback_migrate_page(struct address_space *mapping,
	struct page *newpage, struct page *page)
{
	if (PageDirty(page))
		return writeout(mapping, page);

	/*
	 * Buffers may be managed in a filesystem specific way.
	 * We must have no buffers or drop them.
	 */
	if (page_has_private(page) &&
	    !try_to_release_page(page, GFP_KERNEL))
		return -EAGAIN;

	return migrate_page(mapping, newpage, page);
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
 *  == 0 - success
 */
static int move_to_new_page(struct page *newpage, struct page *page,
					int remap_swapcache, bool sync)
{
	struct address_space *mapping;
	int rc;

	/*
	 * Block others from accessing the page when we get around to
	 * establishing additional references. We are the only one
	 * holding a reference to the new page at this point.
	 */
	if (!trylock_page(newpage))
		BUG();

	/* Prepare mapping for the new page.*/
	newpage->index = page->index;
	newpage->mapping = page->mapping;
	if (PageSwapBacked(page))
		SetPageSwapBacked(newpage);

	mapping = page_mapping(page);
	if (!mapping)
		rc = migrate_page(mapping, newpage, page);
	else {
		/*
		 * Do not writeback pages if !sync and migratepage is
		 * not pointing to migrate_page() which is nonblocking
		 * (swapcache/tmpfs uses migratepage = migrate_page).
		 */
		if (PageDirty(page) && !sync &&
		    mapping->a_ops->migratepage != migrate_page)
			rc = -EBUSY;
		else if (mapping->a_ops->migratepage)
			/*
			 * Most pages have a mapping and most filesystems
			 * should provide a migration function. Anonymous
			 * pages are part of swap space which also has its
			 * own migration function. This is the most common
			 * path for page migration.
			 */
			rc = mapping->a_ops->migratepage(mapping,
							newpage, page);
		else
			rc = fallback_migrate_page(mapping, newpage, page);
	}

	if (rc) {
		newpage->mapping = NULL;
	} else {
		if (remap_swapcache)
			remove_migration_ptes(page, newpage);
	}

	unlock_page(newpage);

	return rc;
}

/*
 * Obtain the lock on page, remove all ptes and migrate the page
 * to the newly allocated page in newpage.
 */
static int unmap_and_move(new_page_t get_new_page, unsigned long private,
			struct page *page, int force, bool offlining, bool sync)
{
	int rc = 0;
	int *result = NULL;
	struct page *newpage = get_new_page(page, private, &result);
	int remap_swapcache = 1;
	int charge = 0;
	struct mem_cgroup *mem;
	struct anon_vma *anon_vma = NULL;

	if (!newpage)
		return -ENOMEM;

	if (page_count(page) == 1) {
		/* page was freed from under us. So we are done. */
		goto move_newpage;
	}
	if (unlikely(PageTransHuge(page)))
		if (unlikely(split_huge_page(page)))
			goto move_newpage;

	/* prepare cgroup just returns 0 or -ENOMEM */
	rc = -EAGAIN;

	if (!trylock_page(page)) {
		if (!force || !sync)
			goto move_newpage;

		/*
		 * It's not safe for direct compaction to call lock_page.
		 * For example, during page readahead pages are added locked
		 * to the LRU. Later, when the IO completes the pages are
		 * marked uptodate and unlocked. However, the queueing
		 * could be merging multiple pages for one bio (e.g.
		 * mpage_readpages). If an allocation happens for the
		 * second or third page, the process can end up locking
		 * the same page twice and deadlocking. Rather than
		 * trying to be clever about what pages can be locked,
		 * avoid the use of lock_page for direct compaction
		 * altogether.
		 */
		if (current->flags & PF_MEMALLOC)
			goto move_newpage;

		lock_page(page);
	}

	/*
	 * Only memory hotplug's offline_pages() caller has locked out KSM,
	 * and can safely migrate a KSM page.  The other cases have skipped
	 * PageKsm along with PageReserved - but it is only now when we have
	 * the page lock that we can be certain it will not go KSM beneath us
	 * (KSM will not upgrade a page from PageAnon to PageKsm when it sees
	 * its pagecount raised, but only here do we take the page lock which
	 * serializes that).
	 */
	if (PageKsm(page) && !offlining) {
		rc = -EBUSY;
		goto unlock;
	}

	/* charge against new page */
	charge = mem_cgroup_prepare_migration(page, newpage, &mem, GFP_KERNEL);
	if (charge == -ENOMEM) {
		rc = -ENOMEM;
		goto unlock;
	}
	BUG_ON(charge);

	if (PageWriteback(page)) {
		/*
		 * For !sync, there is no point retrying as the retry loop
		 * is expected to be too short for PageWriteback to be cleared
		 */
		if (!sync) {
			rc = -EBUSY;
			goto uncharge;
		}
		if (!force)
			goto uncharge;
		wait_on_page_writeback(page);
	}
	/*
	 * By try_to_unmap(), page->mapcount goes down to 0 here. In this case,
	 * we cannot notice that anon_vma is freed while we migrates a page.
	 * This get_anon_vma() delays freeing anon_vma pointer until the end
	 * of migration. File cache pages are no problem because of page_lock()
	 * File Caches may use write_page() or lock_page() in migration, then,
	 * just care Anon page here.
	 */
	if (PageAnon(page)) {
		/*
		 * Only page_lock_anon_vma() understands the subtleties of
		 * getting a hold on an anon_vma from outside one of its mms.
		 */
		anon_vma = page_get_anon_vma(page);
		if (anon_vma) {
			/*
			 * Anon page
			 */
		} else if (PageSwapCache(page)) {
			/*
			 * We cannot be sure that the anon_vma of an unmapped
			 * swapcache page is safe to use because we don't
			 * know in advance if the VMA that this page belonged
			 * to still exists. If the VMA and others sharing the
			 * data have been freed, then the anon_vma could
			 * already be invalid.
			 *
			 * To avoid this possibility, swapcache pages get
			 * migrated but are not remapped when migration
			 * completes
			 */
			remap_swapcache = 0;
		} else {
			goto uncharge;
		}
	}

	/*
	 * Corner case handling:
	 * 1. When a new swap-cache page is read into, it is added to the LRU
	 * and treated as swapcache but it has no rmap yet.
	 * Calling try_to_unmap() against a page->mapping==NULL page will
	 * trigger a BUG.  So handle it here.
	 * 2. An orphaned page (see truncate_complete_page) might have
	 * fs-private metadata. The page can be picked up due to memory
	 * offlining.  Everywhere else except page reclaim, the page is
	 * invisible to the vm, so the page can not be migrated.  So try to
	 * free the metadata, so the page can be freed.
	 */
	if (!page->mapping) {
		VM_BUG_ON(PageAnon(page));
		if (page_has_private(page)) {
			try_to_free_buffers(page);
			goto uncharge;
		}
		goto skip_unmap;
	}

	/* Establish migration ptes or remove ptes */
	try_to_unmap(page, TTU_MIGRATION|TTU_IGNORE_MLOCK|TTU_IGNORE_ACCESS);

skip_unmap:
	if (!page_mapped(page))
		rc = move_to_new_page(newpage, page, remap_swapcache, sync);

	if (rc && remap_swapcache)
		remove_migration_ptes(page, page);

	/* Drop an anon_vma reference if we took one */
	if (anon_vma)
		put_anon_vma(anon_vma);

uncharge:
	if (!charge)
		mem_cgroup_end_migration(mem, page, newpage, rc == 0);
unlock:
	unlock_page(page);

move_newpage:
	if (rc != -EAGAIN) {
 		/*
 		 * A page that has been migrated has all references
 		 * removed and will be freed. A page that has not been
 		 * migrated will have kepts its references and be
 		 * restored.
 		 */
 		list_del(&page->lru);
		dec_zone_page_state(page, NR_ISOLATED_ANON +
				page_is_file_cache(page));
		putback_lru_page(page);
	}

	/*
	 * Move the new page to the LRU. If migration was not successful
	 * then this will free the page.
	 */
	putback_lru_page(newpage);

	if (result) {
		if (rc)
			*result = rc;
		else
			*result = page_to_nid(newpage);
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
				unsigned long private, struct page *hpage,
				int force, bool offlining, bool sync)
{
	int rc = 0;
	int *result = NULL;
	struct page *new_hpage = get_new_page(hpage, private, &result);
	struct anon_vma *anon_vma = NULL;

	if (!new_hpage)
		return -ENOMEM;

	rc = -EAGAIN;

	if (!trylock_page(hpage)) {
		if (!force || !sync)
			goto out;
		lock_page(hpage);
	}

	if (PageAnon(hpage))
		anon_vma = page_get_anon_vma(hpage);

	try_to_unmap(hpage, TTU_MIGRATION|TTU_IGNORE_MLOCK|TTU_IGNORE_ACCESS);

	if (!page_mapped(hpage))
		rc = move_to_new_page(new_hpage, hpage, 1, sync);

	if (rc)
		remove_migration_ptes(hpage, hpage);

	if (anon_vma)
		put_anon_vma(anon_vma);
out:
	unlock_page(hpage);

	if (rc != -EAGAIN) {
		list_del(&hpage->lru);
		put_page(hpage);
	}

	put_page(new_hpage);

	if (result) {
		if (rc)
			*result = rc;
		else
			*result = page_to_nid(new_hpage);
	}
	return rc;
}

/*
 * migrate_pages
 *
 * The function takes one list of pages to migrate and a function
 * that determines from the page to be migrated and the private data
 * the target of the move and allocates the page.
 *
 * The function returns after 10 attempts or if no pages
 * are movable anymore because to has become empty
 * or no retryable pages exist anymore.
 * Caller should call putback_lru_pages to return pages to the LRU
 * or free list only if ret != 0.
 *
 * Return: Number of pages not migrated or error code.
 */
int migrate_pages(struct list_head *from,
		new_page_t get_new_page, unsigned long private, bool offlining,
		bool sync)
{
	int retry = 1;
	int nr_failed = 0;
	int pass = 0;
	struct page *page;
	struct page *page2;
	int swapwrite = current->flags & PF_SWAPWRITE;
	int rc;

	if (!swapwrite)
		current->flags |= PF_SWAPWRITE;

	for(pass = 0; pass < 10 && retry; pass++) {
		retry = 0;

		list_for_each_entry_safe(page, page2, from, lru) {
			cond_resched();

			rc = unmap_and_move(get_new_page, private,
						page, pass > 2, offlining,
						sync);

			switch(rc) {
			case -ENOMEM:
				goto out;
			case -EAGAIN:
				retry++;
				break;
			case 0:
				break;
			default:
				/* Permanent failure */
				nr_failed++;
				break;
			}
		}
	}
	rc = 0;
out:
	if (!swapwrite)
		current->flags &= ~PF_SWAPWRITE;

	if (rc)
		return rc;

	return nr_failed + retry;
}

int migrate_huge_pages(struct list_head *from,
		new_page_t get_new_page, unsigned long private, bool offlining,
		bool sync)
{
	int retry = 1;
	int nr_failed = 0;
	int pass = 0;
	struct page *page;
	struct page *page2;
	int rc;

	for (pass = 0; pass < 10 && retry; pass++) {
		retry = 0;

		list_for_each_entry_safe(page, page2, from, lru) {
			cond_resched();

			rc = unmap_and_move_huge_page(get_new_page,
					private, page, pass > 2, offlining,
					sync);

			switch(rc) {
			case -ENOMEM:
				goto out;
			case -EAGAIN:
				retry++;
				break;
			case 0:
				break;
			default:
				/* Permanent failure */
				nr_failed++;
				break;
			}
		}
	}
	rc = 0;
out:
	if (rc)
		return rc;

	return nr_failed + retry;
}

#ifdef CONFIG_NUMA
/*
 * Move a list of individual pages
 */
struct page_to_node {
	unsigned long addr;
	struct page *page;
	int node;
	int status;
};

static struct page *new_page_node(struct page *p, unsigned long private,
		int **result)
{
	struct page_to_node *pm = (struct page_to_node *)private;

	while (pm->node != MAX_NUMNODES && pm->page != p)
		pm++;

	if (pm->node == MAX_NUMNODES)
		return NULL;

	*result = &pm->status;

	return alloc_pages_exact_node(pm->node,
				GFP_HIGHUSER_MOVABLE | GFP_THISNODE, 0);
}

/*
 * Move a set of pages as indicated in the pm array. The addr
 * field must be set to the virtual address of the page to be moved
 * and the node number must contain a valid target node.
 * The pm array ends with node = MAX_NUMNODES.
 */
static int do_move_page_to_node_array(struct mm_struct *mm,
				      struct page_to_node *pm,
				      int migrate_all)
{
	int err;
	struct page_to_node *pp;
	LIST_HEAD(pagelist);

	down_read(&mm->mmap_sem);

	/*
	 * Build a list of pages to migrate
	 */
	for (pp = pm; pp->node != MAX_NUMNODES; pp++) {
		struct vm_area_struct *vma;
		struct page *page;

		err = -EFAULT;
		vma = find_vma(mm, pp->addr);
		if (!vma || pp->addr < vma->vm_start || !vma_migratable(vma))
			goto set_status;

		page = follow_page(vma, pp->addr, FOLL_GET|FOLL_SPLIT);

		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto set_status;

		err = -ENOENT;
		if (!page)
			goto set_status;

		/* Use PageReserved to check for zero page */
		if (PageReserved(page) || PageKsm(page))
			goto put_and_set;

		pp->page = page;
		err = page_to_nid(page);

		if (err == pp->node)
			/*
			 * Node already in the right place
			 */
			goto put_and_set;

		err = -EACCES;
		if (page_mapcount(page) > 1 &&
				!migrate_all)
			goto put_and_set;

		err = isolate_lru_page(page);
		if (!err) {
			list_add_tail(&page->lru, &pagelist);
			inc_zone_page_state(page, NR_ISOLATED_ANON +
					    page_is_file_cache(page));
		}
put_and_set:
		/*
		 * Either remove the duplicate refcount from
		 * isolate_lru_page() or drop the page ref if it was
		 * not isolated.
		 */
		put_page(page);
set_status:
		pp->status = err;
	}

	err = 0;
	if (!list_empty(&pagelist)) {
		err = migrate_pages(&pagelist, new_page_node,
				(unsigned long)pm, 0, true);
		if (err)
			putback_lru_pages(&pagelist);
	}

	up_read(&mm->mmap_sem);
	return err;
}

/*
 * Migrate an array of page address onto an array of nodes and fill
 * the corresponding array of status.
 */
static int do_pages_move(struct mm_struct *mm, struct task_struct *task,
			 unsigned long nr_pages,
			 const void __user * __user *pages,
			 const int __user *nodes,
			 int __user *status, int flags)
{
	struct page_to_node *pm;
	nodemask_t task_nodes;
	unsigned long chunk_nr_pages;
	unsigned long chunk_start;
	int err;

	task_nodes = cpuset_mems_allowed(task);

	err = -ENOMEM;
	pm = (struct page_to_node *)__get_free_page(GFP_KERNEL);
	if (!pm)
		goto out;

	migrate_prep();

	/*
	 * Store a chunk of page_to_node array in a page,
	 * but keep the last one as a marker
	 */
	chunk_nr_pages = (PAGE_SIZE / sizeof(struct page_to_node)) - 1;

	for (chunk_start = 0;
	     chunk_start < nr_pages;
	     chunk_start += chunk_nr_pages) {
		int j;

		if (chunk_start + chunk_nr_pages > nr_pages)
			chunk_nr_pages = nr_pages - chunk_start;

		/* fill the chunk pm with addrs and nodes from user-space */
		for (j = 0; j < chunk_nr_pages; j++) {
			const void __user *p;
			int node;

			err = -EFAULT;
			if (get_user(p, pages + j + chunk_start))
				goto out_pm;
			pm[j].addr = (unsigned long) p;

			if (get_user(node, nodes + j + chunk_start))
				goto out_pm;

			err = -ENODEV;
			if (node < 0 || node >= MAX_NUMNODES)
				goto out_pm;

			if (!node_state(node, N_HIGH_MEMORY))
				goto out_pm;

			err = -EACCES;
			if (!node_isset(node, task_nodes))
				goto out_pm;

			pm[j].node = node;
		}

		/* End marker for this chunk */
		pm[chunk_nr_pages].node = MAX_NUMNODES;

		/* Migrate this chunk */
		err = do_move_page_to_node_array(mm, pm,
						 flags & MPOL_MF_MOVE_ALL);
		if (err < 0)
			goto out_pm;

		/* Return status information */
		for (j = 0; j < chunk_nr_pages; j++)
			if (put_user(pm[j].status, status + j + chunk_start)) {
				err = -EFAULT;
				goto out_pm;
			}
	}
	err = 0;

out_pm:
	free_page((unsigned long)pm);
out:
	return err;
}

/*
 * Determine the nodes of an array of pages and store it in an array of status.
 */
static void do_pages_stat_array(struct mm_struct *mm, unsigned long nr_pages,
				const void __user **pages, int *status)
{
	unsigned long i;

	down_read(&mm->mmap_sem);

	for (i = 0; i < nr_pages; i++) {
		unsigned long addr = (unsigned long)(*pages);
		struct vm_area_struct *vma;
		struct page *page;
		int err = -EFAULT;

		vma = find_vma(mm, addr);
		if (!vma || addr < vma->vm_start)
			goto set_status;

		page = follow_page(vma, addr, 0);

		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto set_status;

		err = -ENOENT;
		/* Use PageReserved to check for zero page */
		if (!page || PageReserved(page) || PageKsm(page))
			goto set_status;

		err = page_to_nid(page);
set_status:
		*status = err;

		pages++;
		status++;
	}

	up_read(&mm->mmap_sem);
}

/*
 * Determine the nodes of a user array of pages and store it in
 * a user array of status.
 */
static int do_pages_stat(struct mm_struct *mm, unsigned long nr_pages,
			 const void __user * __user *pages,
			 int __user *status)
{
#define DO_PAGES_STAT_CHUNK_NR 16
	const void __user *chunk_pages[DO_PAGES_STAT_CHUNK_NR];
	int chunk_status[DO_PAGES_STAT_CHUNK_NR];

	while (nr_pages) {
		unsigned long chunk_nr;

		chunk_nr = nr_pages;
		if (chunk_nr > DO_PAGES_STAT_CHUNK_NR)
			chunk_nr = DO_PAGES_STAT_CHUNK_NR;

		if (copy_from_user(chunk_pages, pages, chunk_nr * sizeof(*chunk_pages)))
			break;

		do_pages_stat_array(mm, chunk_nr, chunk_pages, chunk_status);

		if (copy_to_user(status, chunk_status, chunk_nr * sizeof(*status)))
			break;

		pages += chunk_nr;
		status += chunk_nr;
		nr_pages -= chunk_nr;
	}
	return nr_pages ? -EFAULT : 0;
}

/*
 * Move a list of pages in the address space of the currently executing
 * process.
 */
SYSCALL_DEFINE6(move_pages, pid_t, pid, unsigned long, nr_pages,
		const void __user * __user *, pages,
		const int __user *, nodes,
		int __user *, status, int, flags)
{
	const struct cred *cred = current_cred(), *tcred;
	struct task_struct *task;
	struct mm_struct *mm;
	int err;

	/* Check flags */
	if (flags & ~(MPOL_MF_MOVE|MPOL_MF_MOVE_ALL))
		return -EINVAL;

	if ((flags & MPOL_MF_MOVE_ALL) && !capable(CAP_SYS_NICE))
		return -EPERM;

	/* Find the mm_struct */
	rcu_read_lock();
	task = pid ? find_task_by_vpid(pid) : current;
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	mm = get_task_mm(task);
	rcu_read_unlock();

	if (!mm)
		return -EINVAL;

	/*
	 * Check if this process has the right to modify the specified
	 * process. The right exists if the process has administrative
	 * capabilities, superuser privileges or the same
	 * userid as the target process.
	 */
	rcu_read_lock();
	tcred = __task_cred(task);
	if (cred->euid != tcred->suid && cred->euid != tcred->uid &&
	    cred->uid  != tcred->suid && cred->uid  != tcred->uid &&
	    !capable(CAP_SYS_NICE)) {
		rcu_read_unlock();
		err = -EPERM;
		goto out;
	}
	rcu_read_unlock();

 	err = security_task_movememory(task);
 	if (err)
		goto out;

	if (nodes) {
		err = do_pages_move(mm, task, nr_pages, pages, nodes, status,
				    flags);
	} else {
		err = do_pages_stat(mm, nr_pages, pages, status);
	}

out:
	mmput(mm);
	return err;
}

/*
 * Call migration functions in the vma_ops that may prepare
 * memory in a vm for migration. migration functions may perform
 * the migration for vmas that do not have an underlying page struct.
 */
int migrate_vmas(struct mm_struct *mm, const nodemask_t *to,
	const nodemask_t *from, unsigned long flags)
{
 	struct vm_area_struct *vma;
 	int err = 0;

	for (vma = mm->mmap; vma && !err; vma = vma->vm_next) {
 		if (vma->vm_ops && vma->vm_ops->migrate) {
 			err = vma->vm_ops->migrate(vma, to, from, flags);
 			if (err)
 				break;
 		}
 	}
 	return err;
}
#endif
