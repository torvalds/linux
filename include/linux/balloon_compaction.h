/*
 * include/linux/balloon_compaction.h
 *
 * Common interface definitions for making balloon pages movable by compaction.
 *
 * Despite being perfectly possible to perform ballooned pages migration, they
 * make a special corner case to compaction scans because balloon pages are not
 * enlisted at any LRU list like the other pages we do compact / migrate.
 *
 * As the page isolation scanning step a compaction thread does is a lockless
 * procedure (from a page standpoint), it might bring some racy situations while
 * performing balloon page compaction. In order to sort out these racy scenarios
 * and safely perform balloon's page compaction and migration we must, always,
 * ensure following these three simple rules:
 *
 *   i. when updating a balloon's page ->mapping element, strictly do it under
 *      the following lock order, independently of the far superior
 *      locking scheme (lru_lock, balloon_lock):
 *	    +-page_lock(page);
 *	      +--spin_lock_irq(&b_dev_info->pages_lock);
 *	            ... page->mapping updates here ...
 *
 *  ii. before isolating or dequeueing a balloon page from the balloon device
 *      pages list, the page reference counter must be raised by one and the
 *      extra refcount must be dropped when the page is enqueued back into
 *      the balloon device page list, thus a balloon page keeps its reference
 *      counter raised only while it is under our special handling;
 *
 * iii. after the lockless scan step have selected a potential balloon page for
 *      isolation, re-test the PageBalloon mark and the PagePrivate flag
 *      under the proper page lock, to ensure isolating a valid balloon page
 *      (not yet isolated, nor under release procedure)
 *
 *  iv. isolation or dequeueing procedure must clear PagePrivate flag under
 *      page lock together with removing page from balloon device page list.
 *
 * The functions provided by this interface are placed to help on coping with
 * the aforementioned balloon page corner case, as well as to ensure the simple
 * set of exposed rules are satisfied while we are dealing with balloon pages
 * compaction / migration.
 *
 * Copyright (C) 2012, Red Hat, Inc.  Rafael Aquini <aquini@redhat.com>
 */
#ifndef _LINUX_BALLOON_COMPACTION_H
#define _LINUX_BALLOON_COMPACTION_H
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/migrate.h>
#include <linux/gfp.h>
#include <linux/err.h>

/*
 * Balloon device information descriptor.
 * This struct is used to allow the common balloon compaction interface
 * procedures to find the proper balloon device holding memory pages they'll
 * have to cope for page compaction / migration, as well as it serves the
 * balloon driver as a page book-keeper for its registered balloon devices.
 */
struct balloon_dev_info {
	unsigned long isolated_pages;	/* # of isolated pages for migration */
	spinlock_t pages_lock;		/* Protection to pages list */
	struct list_head pages;		/* Pages enqueued & handled to Host */
	int (*migratepage)(struct balloon_dev_info *, struct page *newpage,
			struct page *page, enum migrate_mode mode);
};

extern struct page *balloon_page_enqueue(struct balloon_dev_info *b_dev_info);
extern struct page *balloon_page_dequeue(struct balloon_dev_info *b_dev_info);

static inline void balloon_devinfo_init(struct balloon_dev_info *balloon)
{
	balloon->isolated_pages = 0;
	spin_lock_init(&balloon->pages_lock);
	INIT_LIST_HEAD(&balloon->pages);
	balloon->migratepage = NULL;
}

#ifdef CONFIG_BALLOON_COMPACTION
extern bool balloon_page_isolate(struct page *page);
extern void balloon_page_putback(struct page *page);
extern int balloon_page_migrate(struct page *newpage,
				struct page *page, enum migrate_mode mode);

/*
 * __is_movable_balloon_page - helper to perform @page PageBalloon tests
 */
static inline bool __is_movable_balloon_page(struct page *page)
{
	return PageBalloon(page);
}

/*
 * balloon_page_movable - test PageBalloon to identify balloon pages
 *			  and PagePrivate to check that the page is not
 *			  isolated and can be moved by compaction/migration.
 *
 * As we might return false positives in the case of a balloon page being just
 * released under us, this need to be re-tested later, under the page lock.
 */
static inline bool balloon_page_movable(struct page *page)
{
	return PageBalloon(page) && PagePrivate(page);
}

/*
 * isolated_balloon_page - identify an isolated balloon page on private
 *			   compaction/migration page lists.
 */
static inline bool isolated_balloon_page(struct page *page)
{
	return PageBalloon(page);
}

/*
 * balloon_page_insert - insert a page into the balloon's page list and make
 *			 the page->private assignment accordingly.
 * @balloon : pointer to balloon device
 * @page    : page to be assigned as a 'balloon page'
 *
 * Caller must ensure the page is locked and the spin_lock protecting balloon
 * pages list is held before inserting a page into the balloon device.
 */
static inline void balloon_page_insert(struct balloon_dev_info *balloon,
				       struct page *page)
{
	__SetPageBalloon(page);
	SetPagePrivate(page);
	set_page_private(page, (unsigned long)balloon);
	list_add(&page->lru, &balloon->pages);
}

/*
 * balloon_page_delete - delete a page from balloon's page list and clear
 *			 the page->private assignement accordingly.
 * @page    : page to be released from balloon's page list
 *
 * Caller must ensure the page is locked and the spin_lock protecting balloon
 * pages list is held before deleting a page from the balloon device.
 */
static inline void balloon_page_delete(struct page *page)
{
	__ClearPageBalloon(page);
	set_page_private(page, 0);
	if (PagePrivate(page)) {
		ClearPagePrivate(page);
		list_del(&page->lru);
	}
}

/*
 * balloon_page_device - get the b_dev_info descriptor for the balloon device
 *			 that enqueues the given page.
 */
static inline struct balloon_dev_info *balloon_page_device(struct page *page)
{
	return (struct balloon_dev_info *)page_private(page);
}

static inline gfp_t balloon_mapping_gfp_mask(void)
{
	return GFP_HIGHUSER_MOVABLE;
}

#else /* !CONFIG_BALLOON_COMPACTION */

static inline void balloon_page_insert(struct balloon_dev_info *balloon,
				       struct page *page)
{
	list_add(&page->lru, &balloon->pages);
}

static inline void balloon_page_delete(struct page *page)
{
	list_del(&page->lru);
}

static inline bool __is_movable_balloon_page(struct page *page)
{
	return false;
}

static inline bool balloon_page_movable(struct page *page)
{
	return false;
}

static inline bool isolated_balloon_page(struct page *page)
{
	return false;
}

static inline bool balloon_page_isolate(struct page *page)
{
	return false;
}

static inline void balloon_page_putback(struct page *page)
{
	return;
}

static inline int balloon_page_migrate(struct page *newpage,
				struct page *page, enum migrate_mode mode)
{
	return 0;
}

static inline gfp_t balloon_mapping_gfp_mask(void)
{
	return GFP_HIGHUSER;
}

#endif /* CONFIG_BALLOON_COMPACTION */
#endif /* _LINUX_BALLOON_COMPACTION_H */
