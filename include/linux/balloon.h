/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common interface for implementing a memory balloon, including support
 * for migration of pages inflated in a memory balloon.
 *
 * Balloon page migration makes use of the general "movable_ops page migration"
 * feature.
 *
 * page->private is used to reference the responsible balloon device.
 * That these pages have movable_ops, and which movable_ops apply,
 * is derived from the page type (PageOffline()) combined with the
 * PG_movable_ops flag (PageMovableOps()).
 *
 * Once the page type and the PG_movable_ops are set, migration code
 * can initiate page isolation by invoking the
 * movable_operations()->isolate_page() callback
 *
 * As long as page->private is set, the page is either on the balloon list
 * or isolated for migration. If page->private is not set, the page is
 * either still getting inflated, or was deflated to be freed by the balloon
 * driver soon. Isolation is impossible in both cases.
 *
 * As the page isolation scanning step a compaction thread does is a lockless
 * procedure (from a page standpoint), it might bring some racy situations while
 * performing balloon page migration. In order to sort out these racy scenarios
 * and safely perform balloon's page migration we must, always, ensure following
 * these simple rules:
 *
 *   i. Inflation/deflation must set/clear page->private under the
 *      balloon_pages_lock
 *
 *  ii. isolation or dequeueing procedure must remove the page from balloon
 *      device page list under balloon_pages_lock
 *
 * Copyright (C) 2012, Red Hat, Inc.  Rafael Aquini <aquini@redhat.com>
 */
#ifndef _LINUX_BALLOON_H
#define _LINUX_BALLOON_H
#include <linux/pagemap.h>
#include <linux/page-flags.h>
#include <linux/migrate.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/list.h>

/*
 * Balloon device information descriptor.
 * This struct is used to allow the common balloon page migration interface
 * procedures to find the proper balloon device holding memory pages they'll
 * have to cope for page migration, as well as it serves the balloon driver as
 * a page book-keeper for its registered balloon devices.
 */
struct balloon_dev_info {
	unsigned long isolated_pages;	/* # of isolated pages for migration */
	struct list_head pages;		/* Pages enqueued & handled to Host */
	int (*migratepage)(struct balloon_dev_info *, struct page *newpage,
			struct page *page, enum migrate_mode mode);
	bool adjust_managed_page_count;
};

struct page *balloon_page_alloc(void);
void balloon_page_enqueue(struct balloon_dev_info *b_dev_info,
		struct page *page);
struct page *balloon_page_dequeue(struct balloon_dev_info *b_dev_info);
size_t balloon_page_list_enqueue(struct balloon_dev_info *b_dev_info,
		struct list_head *pages);
size_t balloon_page_list_dequeue(struct balloon_dev_info *b_dev_info,
		struct list_head *pages, size_t n_req_pages);

static inline void balloon_devinfo_init(struct balloon_dev_info *balloon)
{
	balloon->isolated_pages = 0;
	INIT_LIST_HEAD(&balloon->pages);
	balloon->migratepage = NULL;
	balloon->adjust_managed_page_count = false;
}
#endif /* _LINUX_BALLOON_H */
