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
 * Christoph Lameter <clameter@sgi.com>
 */

#include <linux/migrate.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/mm_inline.h>
#include <linux/pagevec.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/writeback.h>

#include "internal.h"

/* The maximum number of pages to take off the LRU for migration */
#define MIGRATE_CHUNK_SIZE 256

#define lru_to_page(_head) (list_entry((_head)->prev, struct page, lru))

/*
 * Isolate one page from the LRU lists. If successful put it onto
 * the indicated list with elevated page count.
 *
 * Result:
 *  -EBUSY: page not on LRU list
 *  0: page removed from LRU list and added to the specified list.
 */
int isolate_lru_page(struct page *page, struct list_head *pagelist)
{
	int ret = -EBUSY;

	if (PageLRU(page)) {
		struct zone *zone = page_zone(page);

		spin_lock_irq(&zone->lru_lock);
		if (PageLRU(page)) {
			ret = 0;
			get_page(page);
			ClearPageLRU(page);
			if (PageActive(page))
				del_page_from_active_list(zone, page);
			else
				del_page_from_inactive_list(zone, page);
			list_add_tail(&page->lru, pagelist);
		}
		spin_unlock_irq(&zone->lru_lock);
	}
	return ret;
}

/*
 * migrate_prep() needs to be called after we have compiled the list of pages
 * to be migrated using isolate_lru_page() but before we begin a series of calls
 * to migrate_pages().
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

static inline void move_to_lru(struct page *page)
{
	if (PageActive(page)) {
		/*
		 * lru_cache_add_active checks that
		 * the PG_active bit is off.
		 */
		ClearPageActive(page);
		lru_cache_add_active(page);
	} else {
		lru_cache_add(page);
	}
	put_page(page);
}

/*
 * Add isolated pages on the list back to the LRU.
 *
 * returns the number of pages put back.
 */
int putback_lru_pages(struct list_head *l)
{
	struct page *page;
	struct page *page2;
	int count = 0;

	list_for_each_entry_safe(page, page2, l, lru) {
		list_del(&page->lru);
		move_to_lru(page);
		count++;
	}
	return count;
}

static inline int is_swap_pte(pte_t pte)
{
	return !pte_none(pte) && !pte_present(pte) && !pte_file(pte);
}

/*
 * Restore a potential migration pte to a working pte entry
 */
static void remove_migration_pte(struct vm_area_struct *vma,
		struct page *old, struct page *new)
{
	struct mm_struct *mm = vma->vm_mm;
	swp_entry_t entry;
 	pgd_t *pgd;
 	pud_t *pud;
 	pmd_t *pmd;
	pte_t *ptep, pte;
 	spinlock_t *ptl;
	unsigned long addr = page_address_in_vma(new, vma);

	if (addr == -EFAULT)
		return;

 	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
                return;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
                return;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return;

	ptep = pte_offset_map(pmd, addr);

	if (!is_swap_pte(*ptep)) {
		pte_unmap(ptep);
 		return;
 	}

 	ptl = pte_lockptr(mm, pmd);
 	spin_lock(ptl);
	pte = *ptep;
	if (!is_swap_pte(pte))
		goto out;

	entry = pte_to_swp_entry(pte);

	if (!is_migration_entry(entry) || migration_entry_to_page(entry) != old)
		goto out;

	get_page(new);
	pte = pte_mkold(mk_pte(new, vma->vm_page_prot));
	if (is_write_migration_entry(entry))
		pte = pte_mkwrite(pte);
	set_pte_at(mm, addr, ptep, pte);

	if (PageAnon(new))
		page_add_anon_rmap(new, vma, addr);
	else
		page_add_file_rmap(new);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, addr, pte);
	lazy_mmu_prot_update(pte);

out:
	pte_unmap_unlock(ptep, ptl);
}

/*
 * Note that remove_file_migration_ptes will only work on regular mappings,
 * Nonlinear mappings do not use migration entries.
 */
static void remove_file_migration_ptes(struct page *old, struct page *new)
{
	struct vm_area_struct *vma;
	struct address_space *mapping = page_mapping(new);
	struct prio_tree_iter iter;
	pgoff_t pgoff = new->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

	if (!mapping)
		return;

	spin_lock(&mapping->i_mmap_lock);

	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff)
		remove_migration_pte(vma, old, new);

	spin_unlock(&mapping->i_mmap_lock);
}

/*
 * Must hold mmap_sem lock on at least one of the vmas containing
 * the page so that the anon_vma cannot vanish.
 */
static void remove_anon_migration_ptes(struct page *old, struct page *new)
{
	struct anon_vma *anon_vma;
	struct vm_area_struct *vma;
	unsigned long mapping;

	mapping = (unsigned long)new->mapping;

	if (!mapping || (mapping & PAGE_MAPPING_ANON) == 0)
		return;

	/*
	 * We hold the mmap_sem lock. So no need to call page_lock_anon_vma.
	 */
	anon_vma = (struct anon_vma *) (mapping - PAGE_MAPPING_ANON);
	spin_lock(&anon_vma->lock);

	list_for_each_entry(vma, &anon_vma->head, anon_vma_node)
		remove_migration_pte(vma, old, new);

	spin_unlock(&anon_vma->lock);
}

/*
 * Get rid of all migration entries and replace them by
 * references to the indicated page.
 */
static void remove_migration_ptes(struct page *old, struct page *new)
{
	if (PageAnon(new))
		remove_anon_migration_ptes(old, new);
	else
		remove_file_migration_ptes(old, new);
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

	get_page(page);
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
 * 3 for pages with a mapping and PagePrivate set.
 */
static int migrate_page_move_mapping(struct address_space *mapping,
		struct page *newpage, struct page *page)
{
	struct page **radix_pointer;

	if (!mapping) {
		/* Anonymous page */
		if (page_count(page) != 1)
			return -EAGAIN;
		return 0;
	}

	write_lock_irq(&mapping->tree_lock);

	radix_pointer = (struct page **)radix_tree_lookup_slot(
						&mapping->page_tree,
						page_index(page));

	if (page_count(page) != 2 + !!PagePrivate(page) ||
			*radix_pointer != page) {
		write_unlock_irq(&mapping->tree_lock);
		return -EAGAIN;
	}

	/*
	 * Now we know that no one else is looking at the page.
	 */
	get_page(newpage);
#ifdef CONFIG_SWAP
	if (PageSwapCache(page)) {
		SetPageSwapCache(newpage);
		set_page_private(newpage, page_private(page));
	}
#endif

	*radix_pointer = newpage;
	__put_page(page);
	write_unlock_irq(&mapping->tree_lock);

	return 0;
}

/*
 * Copy the page to its new location
 */
static void migrate_page_copy(struct page *newpage, struct page *page)
{
	copy_highpage(newpage, page);

	if (PageError(page))
		SetPageError(newpage);
	if (PageReferenced(page))
		SetPageReferenced(newpage);
	if (PageUptodate(page))
		SetPageUptodate(newpage);
	if (PageActive(page))
		SetPageActive(newpage);
	if (PageChecked(page))
		SetPageChecked(newpage);
	if (PageMappedToDisk(page))
		SetPageMappedToDisk(newpage);

	if (PageDirty(page)) {
		clear_page_dirty_for_io(page);
		set_page_dirty(newpage);
 	}

#ifdef CONFIG_SWAP
	ClearPageSwapCache(page);
#endif
	ClearPageActive(page);
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
 * pages that do not use PagePrivate.
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
		.nonblocking = 1,
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
	if (rc < 0)
		/* I/O Error writing */
		return -EIO;

	if (rc != AOP_WRITEPAGE_ACTIVATE)
		/* unlocked. Relock */
		lock_page(page);

	return -EAGAIN;
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
	if (page_has_buffers(page) &&
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
 */
static int move_to_new_page(struct page *newpage, struct page *page)
{
	struct address_space *mapping;
	int rc;

	/*
	 * Block others from accessing the page when we get around to
	 * establishing additional references. We are the only one
	 * holding a reference to the new page at this point.
	 */
	if (TestSetPageLocked(newpage))
		BUG();

	/* Prepare mapping for the new page.*/
	newpage->index = page->index;
	newpage->mapping = page->mapping;

	mapping = page_mapping(page);
	if (!mapping)
		rc = migrate_page(mapping, newpage, page);
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

	if (!rc)
		remove_migration_ptes(page, newpage);
	else
		newpage->mapping = NULL;

	unlock_page(newpage);

	return rc;
}

/*
 * Obtain the lock on page, remove all ptes and migrate the page
 * to the newly allocated page in newpage.
 */
static int unmap_and_move(struct page *newpage, struct page *page, int force)
{
	int rc = 0;

	if (page_count(page) == 1)
		/* page was freed from under us. So we are done. */
		goto ret;

	rc = -EAGAIN;
	if (TestSetPageLocked(page)) {
		if (!force)
			goto ret;
		lock_page(page);
	}

	if (PageWriteback(page)) {
		if (!force)
			goto unlock;
		wait_on_page_writeback(page);
	}

	/*
	 * Establish migration ptes or remove ptes
	 */
	if (try_to_unmap(page, 1) != SWAP_FAIL) {
		if (!page_mapped(page))
			rc = move_to_new_page(newpage, page);
	} else
		/* A vma has VM_LOCKED set -> permanent failure */
		rc = -EPERM;

	if (rc)
		remove_migration_ptes(page, page);
unlock:
	unlock_page(page);
ret:
	if (rc != -EAGAIN) {
 		/*
 		 * A page that has been migrated has all references
 		 * removed and will be freed. A page that has not been
 		 * migrated will have kepts its references and be
 		 * restored.
 		 */
 		list_del(&page->lru);
 		move_to_lru(page);

		list_del(&newpage->lru);
		move_to_lru(newpage);
	}
	return rc;
}

/*
 * migrate_pages
 *
 * Two lists are passed to this function. The first list
 * contains the pages isolated from the LRU to be migrated.
 * The second list contains new pages that the isolated pages
 * can be moved to.
 *
 * The function returns after 10 attempts or if no pages
 * are movable anymore because to has become empty
 * or no retryable pages exist anymore. All pages will be
 * retruned to the LRU or freed.
 *
 * Return: Number of pages not migrated.
 */
int migrate_pages(struct list_head *from, struct list_head *to)
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

			if (list_empty(to))
				break;

			cond_resched();

			rc = unmap_and_move(lru_to_page(to), page, pass > 2);

			switch(rc) {
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

	if (!swapwrite)
		current->flags &= ~PF_SWAPWRITE;

	putback_lru_pages(from);
	return nr_failed + retry;
}

/*
 * Migrate the list 'pagelist' of pages to a certain destination.
 *
 * Specify destination with either non-NULL vma or dest_node >= 0
 * Return the number of pages not migrated or error code
 */
int migrate_pages_to(struct list_head *pagelist,
			struct vm_area_struct *vma, int dest)
{
	LIST_HEAD(newlist);
	int err = 0;
	unsigned long offset = 0;
	int nr_pages;
	int nr_failed = 0;
	struct page *page;
	struct list_head *p;

redo:
	nr_pages = 0;
	list_for_each(p, pagelist) {
		if (vma) {
			/*
			 * The address passed to alloc_page_vma is used to
			 * generate the proper interleave behavior. We fake
			 * the address here by an increasing offset in order
			 * to get the proper distribution of pages.
			 *
			 * No decision has been made as to which page
			 * a certain old page is moved to so we cannot
			 * specify the correct address.
			 */
			page = alloc_page_vma(GFP_HIGHUSER, vma,
					offset + vma->vm_start);
			offset += PAGE_SIZE;
		}
		else
			page = alloc_pages_node(dest, GFP_HIGHUSER, 0);

		if (!page) {
			err = -ENOMEM;
			goto out;
		}
		list_add_tail(&page->lru, &newlist);
		nr_pages++;
		if (nr_pages > MIGRATE_CHUNK_SIZE)
			break;
	}
	err = migrate_pages(pagelist, &newlist);

	if (err >= 0) {
		nr_failed += err;
		if (list_empty(&newlist) && !list_empty(pagelist))
			goto redo;
	}
out:

	/* Calculate number of leftover pages */
	list_for_each(p, pagelist)
		nr_failed++;
	return nr_failed;
}
