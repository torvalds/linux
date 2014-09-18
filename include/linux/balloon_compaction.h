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
 *      isolation, re-test the page->mapping flags and the page ref counter
 *      under the proper page lock, to ensure isolating a valid balloon page
 *      (not yet isolated, nor under release procedure)
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
	void *balloon_device;		/* balloon device descriptor */
	struct address_space *mapping;	/* balloon special page->mapping */
	unsigned long isolated_pages;	/* # of isolated pages for migration */
	spinlock_t pages_lock;		/* Protection to pages list */
	struct list_head pages;		/* Pages enqueued & handled to Host */
};

extern struct page *balloon_page_enqueue(struct balloon_dev_info *b_dev_info);
extern struct page *balloon_page_dequeue(struct balloon_dev_info *b_dev_info);
extern struct balloon_dev_info *balloon_devinfo_alloc(
						void *balloon_dev_descriptor);

static inline void balloon_devinfo_free(struct balloon_dev_info *b_dev_info)
{
	kfree(b_dev_info);
}

/*
 * balloon_page_free - release a balloon page back to the page free lists
 * @page: ballooned page to be set free
 *
 * This function must be used to properly set free an isolated/dequeued balloon
 * page at the end of a sucessful page migration, or at the balloon driver's
 * page release procedure.
 */
static inline void balloon_page_free(struct page *page)
{
	/*
	 * Balloon pages always get an extra refcount before being isolated
	 * and before being dequeued to help on sorting out fortuite colisions
	 * between a thread attempting to isolate and another thread attempting
	 * to release the very same balloon page.
	 *
	 * Before we handle the page back to Buddy, lets drop its extra refcnt.
	 */
	put_page(page);
	__free_page(page);
}

#ifdef CONFIG_BALLOON_COMPACTION
extern bool balloon_page_isolate(struct page *page);
extern void balloon_page_putback(struct page *page);
extern int balloon_page_migrate(struct page *newpage,
				struct page *page, enum migrate_mode mode);
extern struct address_space
*balloon_mapping_alloc(struct balloon_dev_info *b_dev_info,
			const struct address_space_operations *a_ops);

static inline void balloon_mapping_free(struct address_space *balloon_mapping)
{
	kfree(balloon_mapping);
}

/*
 * page_flags_cleared - helper to perform balloon @page ->flags tests.
 *
 * As balloon pages are obtained from buddy and we do not play with page->flags
 * at driver level (exception made when we get the page lock for compaction),
 * we can safely identify a ballooned page by checking if the
 * PAGE_FLAGS_CHECK_AT_PREP page->flags are all cleared.  This approach also
 * helps us skip ballooned pages that are locked for compaction or release, thus
 * mitigating their racy check at balloon_page_movable()
 */
static inline bool page_flags_cleared(struct page *page)
{
	return !(page->flags & PAGE_FLAGS_CHECK_AT_PREP);
}

/*
 * __is_movable_balloon_page - helper to perform @page mapping->flags tests
 */
static inline bool __is_movable_balloon_page(struct page *page)
{
	struct address_space *mapping = page->mapping;
	return mapping_balloon(mapping);
}

/*
 * balloon_page_movable - test page->mapping->flags to identify balloon pages
 *			  that can be moved by compaction/migration.
 *
 * This function is used at core compaction's page isolation scheme, therefore
 * most pages exposed to it are not enlisted as balloon pages and so, to avoid
 * undesired side effects like racing against __free_pages(), we cannot afford
 * holding the page locked while testing page->mapping->flags here.
 *
 * As we might return false positives in the case of a balloon page being just
 * released under us, the page->mapping->flags need to be re-tested later,
 * under the proper page lock, at the functions that will be coping with the
 * balloon page case.
 */
static inline bool balloon_page_movable(struct page *page)
{
	/*
	 * Before dereferencing and testing mapping->flags, let's make sure
	 * this is not a page that uses ->mapping in a different way
	 */
	if (page_flags_cleared(page) && !page_mapped(page) &&
	    page_count(page) == 1)
		return __is_movable_balloon_page(page);

	return false;
}

/*
 * isolated_balloon_page - identify an isolated balloon page on private
 *			   compaction/migration page lists.
 *
 * After a compaction thread isolates a balloon page for migration, it raises
 * the page refcount to prevent concurrent compaction threads from re-isolating
 * the same page. For that reason putback_movable_pages(), or other routines
 * that need to identify isolated balloon pages on private pagelists, cannot
 * rely on balloon_page_movable() to accomplish the task.
 */
static inline bool isolated_balloon_page(struct page *page)
{
	/* Already isolated balloon pages, by default, have a raised refcount */
	if (page_flags_cleared(page) && !page_mapped(page) &&
	    page_count(page) >= 2)
		return __is_movable_balloon_page(page);

	return false;
}

/*
 * balloon_page_insert - insert a page into the balloon's page list and make
 *		         the page->mapping assignment accordingly.
 * @page    : page to be assigned as a 'balloon page'
 * @mapping : allocated special 'balloon_mapping'
 * @head    : balloon's device page list head
 *
 * Caller must ensure the page is locked and the spin_lock protecting balloon
 * pages list is held before inserting a page into the balloon device.
 */
static inline void balloon_page_insert(struct page *page,
				       struct address_space *mapping,
				       struct list_head *head)
{
	page->mapping = mapping;
	list_add(&page->lru, head);
}

/*
 * balloon_page_delete - delete a page from balloon's page list and clear
 *			 the page->mapping assignement accordingly.
 * @page    : page to be released from balloon's page list
 *
 * Caller must ensure the page is locked and the spin_lock protecting balloon
 * pages list is held before deleting a page from the balloon device.
 */
static inline void balloon_page_delete(struct page *page)
{
	page->mapping = NULL;
	list_del(&page->lru);
}

/*
 * balloon_page_device - get the b_dev_info descriptor for the balloon device
 *			 that enqueues the given page.
 */
static inline struct balloon_dev_info *balloon_page_device(struct page *page)
{
	struct address_space *mapping = page->mapping;
	if (likely(mapping))
		return mapping->private_data;

	return NULL;
}

static inline gfp_t balloon_mapping_gfp_mask(void)
{
	return GFP_HIGHUSER_MOVABLE;
}

static inline bool balloon_compaction_check(void)
{
	return true;
}

#else /* !CONFIG_BALLOON_COMPACTION */

static inline void *balloon_mapping_alloc(void *balloon_device,
				const struct address_space_operations *a_ops)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void balloon_mapping_free(struct address_space *balloon_mapping)
{
	return;
}

static inline void balloon_page_insert(struct page *page,
				       struct address_space *mapping,
				       struct list_head *head)
{
	list_add(&page->lru, head);
}

static inline void balloon_page_delete(struct page *page)
{
	list_del(&page->lru);
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

static inline bool balloon_compaction_check(void)
{
	return false;
}
#endif /* CONFIG_BALLOON_COMPACTION */
#endif /* _LINUX_BALLOON_COMPACTION_H */
