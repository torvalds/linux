// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common interface for implementing a memory balloon, including support
 * for migration of pages inflated in a memory balloon.
 *
 * Copyright (C) 2012, Red Hat, Inc.  Rafael Aquini <aquini@redhat.com>
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/balloon.h>

/*
 * Lock protecting the balloon_dev_info of all devices. We don't really
 * expect more than one device.
 */
static DEFINE_SPINLOCK(balloon_pages_lock);

/**
 * balloon_page_insert - insert a page into the balloon's page list and make
 *			 the page->private assignment accordingly.
 * @balloon : pointer to balloon device
 * @page    : page to be assigned as a 'balloon page'
 *
 * Caller must ensure the balloon_pages_lock is held.
 */
static void balloon_page_insert(struct balloon_dev_info *balloon,
				       struct page *page)
{
	lockdep_assert_held(&balloon_pages_lock);
	__SetPageOffline(page);
	if (IS_ENABLED(CONFIG_BALLOON_MIGRATION)) {
		SetPageMovableOps(page);
		set_page_private(page, (unsigned long)balloon);
	}
	list_add(&page->lru, &balloon->pages);
}

/**
 * balloon_page_finalize - prepare a balloon page that was removed from the
 *			   balloon list for release to the page allocator
 * @page: page to be released to the page allocator
 *
 * Caller must ensure the balloon_pages_lock is held.
 */
static void balloon_page_finalize(struct page *page)
{
	lockdep_assert_held(&balloon_pages_lock);
	if (IS_ENABLED(CONFIG_BALLOON_MIGRATION))
		set_page_private(page, 0);
	/* PageOffline is sticky until the page is freed to the buddy. */
}

static void balloon_page_enqueue_one(struct balloon_dev_info *b_dev_info,
				     struct page *page)
{
	balloon_page_insert(b_dev_info, page);
	if (b_dev_info->adjust_managed_page_count)
		adjust_managed_page_count(page, -1);
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

	spin_lock_irqsave(&balloon_pages_lock, flags);
	list_for_each_entry_safe(page, tmp, pages, lru) {
		list_del(&page->lru);
		balloon_page_enqueue_one(b_dev_info, page);
		n_pages++;
	}
	spin_unlock_irqrestore(&balloon_pages_lock, flags);
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

	spin_lock_irqsave(&balloon_pages_lock, flags);
	list_for_each_entry_safe(page, tmp, &b_dev_info->pages, lru) {
		if (n_pages == n_req_pages)
			break;
		list_del(&page->lru);
		if (b_dev_info->adjust_managed_page_count)
			adjust_managed_page_count(page, 1);
		balloon_page_finalize(page);
		__count_vm_event(BALLOON_DEFLATE);
		list_add(&page->lru, pages);
		dec_node_page_state(page, NR_BALLOON_PAGES);
		n_pages++;
	}
	spin_unlock_irqrestore(&balloon_pages_lock, flags);

	return n_pages;
}
EXPORT_SYMBOL_GPL(balloon_page_list_dequeue);

/**
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
	gfp_t gfp_flags = __GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN;

	if (IS_ENABLED(CONFIG_BALLOON_MIGRATION))
		gfp_flags |= GFP_HIGHUSER_MOVABLE;
	else
		gfp_flags |= GFP_HIGHUSER;

	return alloc_page(gfp_flags);
}
EXPORT_SYMBOL_GPL(balloon_page_alloc);

/**
 * balloon_page_enqueue - inserts a new page into the balloon page list.
 *
 * @b_dev_info: balloon device descriptor where we will insert a new page
 * @page: new page to enqueue - allocated using balloon_page_alloc.
 *
 * Drivers must call this function to properly enqueue a new allocated balloon
 * page before definitively removing the page from the guest system.
 *
 * Drivers must not enqueue pages while page->lru is still in
 * use, and must not use page->lru until a page was unqueued again.
 */
void balloon_page_enqueue(struct balloon_dev_info *b_dev_info,
			  struct page *page)
{
	unsigned long flags;

	spin_lock_irqsave(&balloon_pages_lock, flags);
	balloon_page_enqueue_one(b_dev_info, page);
	spin_unlock_irqrestore(&balloon_pages_lock, flags);
}
EXPORT_SYMBOL_GPL(balloon_page_enqueue);

/**
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
		spin_lock_irqsave(&balloon_pages_lock, flags);
		if (unlikely(list_empty(&b_dev_info->pages) &&
			     !b_dev_info->isolated_pages))
			BUG();
		spin_unlock_irqrestore(&balloon_pages_lock, flags);
		return NULL;
	}
	return list_first_entry(&pages, struct page, lru);
}
EXPORT_SYMBOL_GPL(balloon_page_dequeue);

#ifdef CONFIG_BALLOON_MIGRATION
static struct balloon_dev_info *balloon_page_device(struct page *page)
{
	return (struct balloon_dev_info *)page_private(page);
}

static bool balloon_page_isolate(struct page *page, isolate_mode_t mode)

{
	struct balloon_dev_info *b_dev_info;
	unsigned long flags;

	spin_lock_irqsave(&balloon_pages_lock, flags);
	b_dev_info = balloon_page_device(page);
	if (!b_dev_info) {
		/*
		 * The page already got deflated and removed from the
		 * balloon list.
		 */
		spin_unlock_irqrestore(&balloon_pages_lock, flags);
		return false;
	}
	list_del(&page->lru);
	b_dev_info->isolated_pages++;
	spin_unlock_irqrestore(&balloon_pages_lock, flags);

	return true;
}

static void balloon_page_putback(struct page *page)
{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;

	/*
	 * When we isolated the page, the page was still inflated in a balloon
	 * device. As isolated balloon pages cannot get deflated, we still have
	 * a balloon device here.
	 */
	if (WARN_ON_ONCE(!b_dev_info))
		return;

	spin_lock_irqsave(&balloon_pages_lock, flags);
	list_add(&page->lru, &b_dev_info->pages);
	b_dev_info->isolated_pages--;
	spin_unlock_irqrestore(&balloon_pages_lock, flags);
}

static int balloon_page_migrate(struct page *newpage, struct page *page,
		enum migrate_mode mode)
{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;
	int rc;

	/*
	 * When we isolated the page, the page was still inflated in a balloon
	 * device. As isolated balloon pages cannot get deflated, we still have
	 * a balloon device here.
	 */
	if (WARN_ON_ONCE(!b_dev_info))
		return -EAGAIN;

	rc = b_dev_info->migratepage(b_dev_info, newpage, page, mode);
	if (rc < 0 && rc != -ENOENT)
		return rc;

	spin_lock_irqsave(&balloon_pages_lock, flags);
	if (!rc) {
		/* Insert the new page into the balloon list. */
		get_page(newpage);
		balloon_page_insert(b_dev_info, newpage);
		__count_vm_event(BALLOON_MIGRATE);

		if (b_dev_info->adjust_managed_page_count &&
		    page_zone(page) != page_zone(newpage)) {
			/*
			 * When we migrate a page to a different zone we
			 * have to fixup the count of both involved zones.
			 */
			adjust_managed_page_count(page, 1);
			adjust_managed_page_count(newpage, -1);
		}
	} else {
		/* Old page was deflated but new page not inflated. */
		__count_vm_event(BALLOON_DEFLATE);

		if (b_dev_info->adjust_managed_page_count)
			adjust_managed_page_count(page, 1);
	}

	b_dev_info->isolated_pages--;

	/* Free the now-deflated page we isolated in balloon_page_isolate(). */
	balloon_page_finalize(page);
	spin_unlock_irqrestore(&balloon_pages_lock, flags);

	put_page(page);

	return 0;
}

static const struct movable_operations balloon_mops = {
	.migrate_page = balloon_page_migrate,
	.isolate_page = balloon_page_isolate,
	.putback_page = balloon_page_putback,
};

static int __init balloon_init(void)
{
	return set_movable_ops(&balloon_mops, PGTY_offline);
}
core_initcall(balloon_init);

#endif /* CONFIG_BALLOON_MIGRATION */
