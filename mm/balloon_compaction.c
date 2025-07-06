// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/balloon_compaction.c
 *
 * Common interface for making balloon pages movable by compaction.
 *
 * Copyright (C) 2012, Red Hat, Inc.  Rafael Aquini <aquini@redhat.com>
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/balloon_compaction.h>

static void balloon_page_enqueue_one(struct balloon_dev_info *b_dev_info,
				     struct page *page)
{
	/*
	 * Block others from accessing the 'page' when we get around to
	 * establishing additional references. We should be the only one
	 * holding a reference to the 'page' at this point. If we are not, then
	 * memory corruption is possible and we should stop execution.
	 */
	BUG_ON(!trylock_page(page));
	balloon_page_insert(b_dev_info, page);
	unlock_page(page);
	__count_vm_event(BALLOON_INFLATE);
	inc_node_page_state(page, NR_BALLOON_PAGES);
}

/**
 * balloon_page_list_enqueue() - inserts a list of pages into the balloon page
 *				 list.
 * @b_dev_info: balloon device descriptor where we will insert a new page to
 * @pages: pages to enqueue - allocated using balloon_page_alloc.
 *
 * Driver must call this function to properly enqueue balloon pages before
 * definitively removing them from the guest system.
 *
 * Return: number of pages that were enqueued.
 */
size_t balloon_page_list_enqueue(struct balloon_dev_info *b_dev_info,
				 struct list_head *pages)
{
	struct page *page, *tmp;
	unsigned long flags;
	size_t n_pages = 0;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_for_each_entry_safe(page, tmp, pages, lru) {
		list_del(&page->lru);
		balloon_page_enqueue_one(b_dev_info, page);
		n_pages++;
	}
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
	return n_pages;
}
EXPORT_SYMBOL_GPL(balloon_page_list_enqueue);

/**
 * balloon_page_list_dequeue() - removes pages from balloon's page list and
 *				 returns a list of the pages.
 * @b_dev_info: balloon device descriptor where we will grab a page from.
 * @pages: pointer to the list of pages that would be returned to the caller.
 * @n_req_pages: number of requested pages.
 *
 * Driver must call this function to properly de-allocate a previous enlisted
 * balloon pages before definitively releasing it back to the guest system.
 * This function tries to remove @n_req_pages from the ballooned pages and
 * return them to the caller in the @pages list.
 *
 * Note that this function may fail to dequeue some pages even if the balloon
 * isn't empty - since the page list can be temporarily empty due to compaction
 * of isolated pages.
 *
 * Return: number of pages that were added to the @pages list.
 */
size_t balloon_page_list_dequeue(struct balloon_dev_info *b_dev_info,
				 struct list_head *pages, size_t n_req_pages)
{
	struct page *page, *tmp;
	unsigned long flags;
	size_t n_pages = 0;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_for_each_entry_safe(page, tmp, &b_dev_info->pages, lru) {
		if (n_pages == n_req_pages)
			break;

		/*
		 * Block others from accessing the 'page' while we get around to
		 * establishing additional references and preparing the 'page'
		 * to be released by the balloon driver.
		 */
		if (!trylock_page(page))
			continue;

		if (IS_ENABLED(CONFIG_BALLOON_COMPACTION) &&
		    PageIsolated(page)) {
			/* raced with isolation */
			unlock_page(page);
			continue;
		}
		balloon_page_delete(page);
		__count_vm_event(BALLOON_DEFLATE);
		list_add(&page->lru, pages);
		unlock_page(page);
		dec_node_page_state(page, NR_BALLOON_PAGES);
		n_pages++;
	}
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);

	return n_pages;
}
EXPORT_SYMBOL_GPL(balloon_page_list_dequeue);

/*
 * balloon_page_alloc - allocates a new page for insertion into the balloon
 *			page list.
 *
 * Driver must call this function to properly allocate a new balloon page.
 * Driver must call balloon_page_enqueue before definitively removing the page
 * from the guest system.
 *
 * Return: struct page for the allocated page or NULL on allocation failure.
 */
struct page *balloon_page_alloc(void)
{
	struct page *page = alloc_page(balloon_mapping_gfp_mask() |
				       __GFP_NOMEMALLOC | __GFP_NORETRY |
				       __GFP_NOWARN);
	return page;
}
EXPORT_SYMBOL_GPL(balloon_page_alloc);

/*
 * balloon_page_enqueue - inserts a new page into the balloon page list.
 *
 * @b_dev_info: balloon device descriptor where we will insert a new page
 * @page: new page to enqueue - allocated using balloon_page_alloc.
 *
 * Drivers must call this function to properly enqueue a new allocated balloon
 * page before definitively removing the page from the guest system.
 *
 * Drivers must not call balloon_page_enqueue on pages that have been pushed to
 * a list with balloon_page_push before removing them with balloon_page_pop. To
 * enqueue a list of pages, use balloon_page_list_enqueue instead.
 */
void balloon_page_enqueue(struct balloon_dev_info *b_dev_info,
			  struct page *page)
{
	unsigned long flags;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	balloon_page_enqueue_one(b_dev_info, page);
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
}
EXPORT_SYMBOL_GPL(balloon_page_enqueue);

/*
 * balloon_page_dequeue - removes a page from balloon's page list and returns
 *			  its address to allow the driver to release the page.
 * @b_dev_info: balloon device descriptor where we will grab a page from.
 *
 * Driver must call this function to properly dequeue a previously enqueued page
 * before definitively releasing it back to the guest system.
 *
 * Caller must perform its own accounting to ensure that this
 * function is called only if some pages are actually enqueued.
 *
 * Note that this function may fail to dequeue some pages even if there are
 * some enqueued pages - since the page list can be temporarily empty due to
 * the compaction of isolated pages.
 *
 * TODO: remove the caller accounting requirements, and allow caller to wait
 * until all pages can be dequeued.
 *
 * Return: struct page for the dequeued page, or NULL if no page was dequeued.
 */
struct page *balloon_page_dequeue(struct balloon_dev_info *b_dev_info)
{
	unsigned long flags;
	LIST_HEAD(pages);
	int n_pages;

	n_pages = balloon_page_list_dequeue(b_dev_info, &pages, 1);

	if (n_pages != 1) {
		/*
		 * If we are unable to dequeue a balloon page because the page
		 * list is empty and there are no isolated pages, then something
		 * went out of track and some balloon pages are lost.
		 * BUG() here, otherwise the balloon driver may get stuck in
		 * an infinite loop while attempting to release all its pages.
		 */
		spin_lock_irqsave(&b_dev_info->pages_lock, flags);
		if (unlikely(list_empty(&b_dev_info->pages) &&
			     !b_dev_info->isolated_pages))
			BUG();
		spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
		return NULL;
	}
	return list_first_entry(&pages, struct page, lru);
}
EXPORT_SYMBOL_GPL(balloon_page_dequeue);

#ifdef CONFIG_BALLOON_COMPACTION

static bool balloon_page_isolate(struct page *page, isolate_mode_t mode)

{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_del(&page->lru);
	b_dev_info->isolated_pages++;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);

	return true;
}

static void balloon_page_putback(struct page *page)
{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_add(&page->lru, &b_dev_info->pages);
	b_dev_info->isolated_pages--;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
}

/* move_to_new_page() counterpart for a ballooned page */
static int balloon_page_migrate(struct page *newpage, struct page *page,
		enum migrate_mode mode)
{
	struct balloon_dev_info *balloon = balloon_page_device(page);

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageLocked(newpage), newpage);

	return balloon->migratepage(balloon, newpage, page, mode);
}

const struct movable_operations balloon_mops = {
	.migrate_page = balloon_page_migrate,
	.isolate_page = balloon_page_isolate,
	.putback_page = balloon_page_putback,
};
EXPORT_SYMBOL_GPL(balloon_mops);

#endif /* CONFIG_BALLOON_COMPACTION */
