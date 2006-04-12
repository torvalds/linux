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
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/mm_inline.h>
#include <linux/pagevec.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/swapops.h>

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
	/* Must have swap device for migration */
	if (nr_swap_pages <= 0)
		return -ENODEV;

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
	list_del(&page->lru);
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
		move_to_lru(page);
		count++;
	}
	return count;
}

/*
 * Non migratable page
 */
int fail_migrate_page(struct page *newpage, struct page *page)
{
	return -EIO;
}
EXPORT_SYMBOL(fail_migrate_page);

/*
 * swapout a single page
 * page is locked upon entry, unlocked on exit
 */
static int swap_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (page_mapped(page) && mapping)
		if (try_to_unmap(page, 1) != SWAP_SUCCESS)
			goto unlock_retry;

	if (PageDirty(page)) {
		/* Page is dirty, try to write it out here */
		switch(pageout(page, mapping)) {
		case PAGE_KEEP:
		case PAGE_ACTIVATE:
			goto unlock_retry;

		case PAGE_SUCCESS:
			goto retry;

		case PAGE_CLEAN:
			; /* try to free the page below */
		}
	}

	if (PagePrivate(page)) {
		if (!try_to_release_page(page, GFP_KERNEL) ||
		    (!mapping && page_count(page) == 1))
			goto unlock_retry;
	}

	if (remove_mapping(mapping, page)) {
		/* Success */
		unlock_page(page);
		return 0;
	}

unlock_retry:
	unlock_page(page);

retry:
	return -EAGAIN;
}

/*
 * Remove references for a page and establish the new page with the correct
 * basic settings to be able to stop accesses to the page.
 */
int migrate_page_remove_references(struct page *newpage,
				struct page *page, int nr_refs)
{
	struct address_space *mapping = page_mapping(page);
	struct page **radix_pointer;

	/*
	 * Avoid doing any of the following work if the page count
	 * indicates that the page is in use or truncate has removed
	 * the page.
	 */
	if (!mapping || page_mapcount(page) + nr_refs != page_count(page))
		return -EAGAIN;

	/*
	 * Establish swap ptes for anonymous pages or destroy pte
	 * maps for files.
	 *
	 * In order to reestablish file backed mappings the fault handlers
	 * will take the radix tree_lock which may then be used to stop
  	 * processses from accessing this page until the new page is ready.
	 *
	 * A process accessing via a swap pte (an anonymous page) will take a
	 * page_lock on the old page which will block the process until the
	 * migration attempt is complete. At that time the PageSwapCache bit
	 * will be examined. If the page was migrated then the PageSwapCache
	 * bit will be clear and the operation to retrieve the page will be
	 * retried which will find the new page in the radix tree. Then a new
	 * direct mapping may be generated based on the radix tree contents.
	 *
	 * If the page was not migrated then the PageSwapCache bit
	 * is still set and the operation may continue.
	 */
	if (try_to_unmap(page, 1) == SWAP_FAIL)
		/* A vma has VM_LOCKED set -> permanent failure */
		return -EPERM;

	/*
	 * Give up if we were unable to remove all mappings.
	 */
	if (page_mapcount(page))
		return -EAGAIN;

	write_lock_irq(&mapping->tree_lock);

	radix_pointer = (struct page **)radix_tree_lookup_slot(
						&mapping->page_tree,
						page_index(page));

	if (!page_mapping(page) || page_count(page) != nr_refs ||
			*radix_pointer != page) {
		write_unlock_irq(&mapping->tree_lock);
		return -EAGAIN;
	}

	/*
	 * Now we know that no one else is looking at the page.
	 *
	 * Certain minimal information about a page must be available
	 * in order for other subsystems to properly handle the page if they
	 * find it through the radix tree update before we are finished
	 * copying the page.
	 */
	get_page(newpage);
	newpage->index = page->index;
	newpage->mapping = page->mapping;
	if (PageSwapCache(page)) {
		SetPageSwapCache(newpage);
		set_page_private(newpage, page_private(page));
	}

	*radix_pointer = newpage;
	__put_page(page);
	write_unlock_irq(&mapping->tree_lock);

	return 0;
}
EXPORT_SYMBOL(migrate_page_remove_references);

/*
 * Copy the page to its new location
 */
void migrate_page_copy(struct page *newpage, struct page *page)
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

	ClearPageSwapCache(page);
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
EXPORT_SYMBOL(migrate_page_copy);

/*
 * Common logic to directly migrate a single page suitable for
 * pages that do not use PagePrivate.
 *
 * Pages are locked upon entry and exit.
 */
int migrate_page(struct page *newpage, struct page *page)
{
	int rc;

	BUG_ON(PageWriteback(page));	/* Writeback must be complete */

	rc = migrate_page_remove_references(newpage, page, 2);

	if (rc)
		return rc;

	migrate_page_copy(newpage, page);

	/*
	 * Remove auxiliary swap entries and replace
	 * them with real ptes.
	 *
	 * Note that a real pte entry will allow processes that are not
	 * waiting on the page lock to use the new page via the page tables
	 * before the new page is unlocked.
	 */
	remove_from_swap(newpage);
	return 0;
}
EXPORT_SYMBOL(migrate_page);

/*
 * migrate_pages
 *
 * Two lists are passed to this function. The first list
 * contains the pages isolated from the LRU to be migrated.
 * The second list contains new pages that the pages isolated
 * can be moved to. If the second list is NULL then all
 * pages are swapped out.
 *
 * The function returns after 10 attempts or if no pages
 * are movable anymore because to has become empty
 * or no retryable pages exist anymore.
 *
 * Return: Number of pages not migrated when "to" ran empty.
 */
int migrate_pages(struct list_head *from, struct list_head *to,
		  struct list_head *moved, struct list_head *failed)
{
	int retry;
	int nr_failed = 0;
	int pass = 0;
	struct page *page;
	struct page *page2;
	int swapwrite = current->flags & PF_SWAPWRITE;
	int rc;

	if (!swapwrite)
		current->flags |= PF_SWAPWRITE;

redo:
	retry = 0;

	list_for_each_entry_safe(page, page2, from, lru) {
		struct page *newpage = NULL;
		struct address_space *mapping;

		cond_resched();

		rc = 0;
		if (page_count(page) == 1)
			/* page was freed from under us. So we are done. */
			goto next;

		if (to && list_empty(to))
			break;

		/*
		 * Skip locked pages during the first two passes to give the
		 * functions holding the lock time to release the page. Later we
		 * use lock_page() to have a higher chance of acquiring the
		 * lock.
		 */
		rc = -EAGAIN;
		if (pass > 2)
			lock_page(page);
		else
			if (TestSetPageLocked(page))
				goto next;

		/*
		 * Only wait on writeback if we have already done a pass where
		 * we we may have triggered writeouts for lots of pages.
		 */
		if (pass > 0) {
			wait_on_page_writeback(page);
		} else {
			if (PageWriteback(page))
				goto unlock_page;
		}

		/*
		 * Anonymous pages must have swap cache references otherwise
		 * the information contained in the page maps cannot be
		 * preserved.
		 */
		if (PageAnon(page) && !PageSwapCache(page)) {
			if (!add_to_swap(page, GFP_KERNEL)) {
				rc = -ENOMEM;
				goto unlock_page;
			}
		}

		if (!to) {
			rc = swap_page(page);
			goto next;
		}

		newpage = lru_to_page(to);
		lock_page(newpage);

		/*
		 * Pages are properly locked and writeback is complete.
		 * Try to migrate the page.
		 */
		mapping = page_mapping(page);
		if (!mapping)
			goto unlock_both;

		if (mapping->a_ops->migratepage) {
			/*
			 * Most pages have a mapping and most filesystems
			 * should provide a migration function. Anonymous
			 * pages are part of swap space which also has its
			 * own migration function. This is the most common
			 * path for page migration.
			 */
			rc = mapping->a_ops->migratepage(newpage, page);
			goto unlock_both;
                }

		/*
		 * Default handling if a filesystem does not provide
		 * a migration function. We can only migrate clean
		 * pages so try to write out any dirty pages first.
		 */
		if (PageDirty(page)) {
			switch (pageout(page, mapping)) {
			case PAGE_KEEP:
			case PAGE_ACTIVATE:
				goto unlock_both;

			case PAGE_SUCCESS:
				unlock_page(newpage);
				goto next;

			case PAGE_CLEAN:
				; /* try to migrate the page below */
			}
                }

		/*
		 * Buffers are managed in a filesystem specific way.
		 * We must have no buffers or drop them.
		 */
		if (!page_has_buffers(page) ||
		    try_to_release_page(page, GFP_KERNEL)) {
			rc = migrate_page(newpage, page);
			goto unlock_both;
		}

		/*
		 * On early passes with mapped pages simply
		 * retry. There may be a lock held for some
		 * buffers that may go away. Later
		 * swap them out.
		 */
		if (pass > 4) {
			/*
			 * Persistently unable to drop buffers..... As a
			 * measure of last resort we fall back to
			 * swap_page().
			 */
			unlock_page(newpage);
			newpage = NULL;
			rc = swap_page(page);
			goto next;
		}

unlock_both:
		unlock_page(newpage);

unlock_page:
		unlock_page(page);

next:
		if (rc == -EAGAIN) {
			retry++;
		} else if (rc) {
			/* Permanent failure */
			list_move(&page->lru, failed);
			nr_failed++;
		} else {
			if (newpage) {
				/* Successful migration. Return page to LRU */
				move_to_lru(newpage);
			}
			list_move(&page->lru, moved);
		}
	}
	if (retry && pass++ < 10)
		goto redo;

	if (!swapwrite)
		current->flags &= ~PF_SWAPWRITE;

	return nr_failed + retry;
}

/*
 * Migration function for pages with buffers. This function can only be used
 * if the underlying filesystem guarantees that no other references to "page"
 * exist.
 */
int buffer_migrate_page(struct page *newpage, struct page *page)
{
	struct address_space *mapping = page->mapping;
	struct buffer_head *bh, *head;
	int rc;

	if (!mapping)
		return -EAGAIN;

	if (!page_has_buffers(page))
		return migrate_page(newpage, page);

	head = page_buffers(page);

	rc = migrate_page_remove_references(newpage, page, 3);

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
 * Migrate the list 'pagelist' of pages to a certain destination.
 *
 * Specify destination with either non-NULL vma or dest_node >= 0
 * Return the number of pages not migrated or error code
 */
int migrate_pages_to(struct list_head *pagelist,
			struct vm_area_struct *vma, int dest)
{
	LIST_HEAD(newlist);
	LIST_HEAD(moved);
	LIST_HEAD(failed);
	int err = 0;
	unsigned long offset = 0;
	int nr_pages;
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
	err = migrate_pages(pagelist, &newlist, &moved, &failed);

	putback_lru_pages(&moved);	/* Call release pages instead ?? */

	if (err >= 0 && list_empty(&newlist) && !list_empty(pagelist))
		goto redo;
out:
	/* Return leftover allocated pages */
	while (!list_empty(&newlist)) {
		page = list_entry(newlist.next, struct page, lru);
		list_del(&page->lru);
		__free_page(page);
	}
	list_splice(&failed, pagelist);
	if (err < 0)
		return err;

	/* Calculate number of leftover pages */
	nr_pages = 0;
	list_for_each(p, pagelist)
		nr_pages++;
	return nr_pages;
}
