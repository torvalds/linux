#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/ceph/pagelist.h>

static void ceph_pagelist_unmap_tail(struct ceph_pagelist *pl)
{
	if (pl->mapped_tail) {
		struct page *page = list_entry(pl->head.prev, struct page, lru);
		kunmap(page);
		pl->mapped_tail = NULL;
	}
}

void ceph_pagelist_release(struct ceph_pagelist *pl)
{
	if (!refcount_dec_and_test(&pl->refcnt))
		return;
	ceph_pagelist_unmap_tail(pl);
	while (!list_empty(&pl->head)) {
		struct page *page = list_first_entry(&pl->head, struct page,
						     lru);
		list_del(&page->lru);
		__free_page(page);
	}
	ceph_pagelist_free_reserve(pl);
	kfree(pl);
}
EXPORT_SYMBOL(ceph_pagelist_release);

static int ceph_pagelist_addpage(struct ceph_pagelist *pl)
{
	struct page *page;

	if (!pl->num_pages_free) {
		page = __page_cache_alloc(GFP_NOFS);
	} else {
		page = list_first_entry(&pl->free_list, struct page, lru);
		list_del(&page->lru);
		--pl->num_pages_free;
	}
	if (!page)
		return -ENOMEM;
	pl->room += PAGE_SIZE;
	ceph_pagelist_unmap_tail(pl);
	list_add_tail(&page->lru, &pl->head);
	pl->mapped_tail = kmap(page);
	return 0;
}

int ceph_pagelist_append(struct ceph_pagelist *pl, const void *buf, size_t len)
{
	while (pl->room < len) {
		size_t bit = pl->room;
		int ret;

		memcpy(pl->mapped_tail + (pl->length & ~PAGE_MASK),
		       buf, bit);
		pl->length += bit;
		pl->room -= bit;
		buf += bit;
		len -= bit;
		ret = ceph_pagelist_addpage(pl);
		if (ret)
			return ret;
	}

	memcpy(pl->mapped_tail + (pl->length & ~PAGE_MASK), buf, len);
	pl->length += len;
	pl->room -= len;
	return 0;
}
EXPORT_SYMBOL(ceph_pagelist_append);

/* Allocate enough pages for a pagelist to append the given amount
 * of data without without allocating.
 * Returns: 0 on success, -ENOMEM on error.
 */
int ceph_pagelist_reserve(struct ceph_pagelist *pl, size_t space)
{
	if (space <= pl->room)
		return 0;
	space -= pl->room;
	space = (space + PAGE_SIZE - 1) >> PAGE_SHIFT;   /* conv to num pages */

	while (space > pl->num_pages_free) {
		struct page *page = __page_cache_alloc(GFP_NOFS);
		if (!page)
			return -ENOMEM;
		list_add_tail(&page->lru, &pl->free_list);
		++pl->num_pages_free;
	}
	return 0;
}
EXPORT_SYMBOL(ceph_pagelist_reserve);

/* Free any pages that have been preallocated. */
int ceph_pagelist_free_reserve(struct ceph_pagelist *pl)
{
	while (!list_empty(&pl->free_list)) {
		struct page *page = list_first_entry(&pl->free_list,
						     struct page, lru);
		list_del(&page->lru);
		__free_page(page);
		--pl->num_pages_free;
	}
	BUG_ON(pl->num_pages_free);
	return 0;
}
EXPORT_SYMBOL(ceph_pagelist_free_reserve);

/* Create a truncation point. */
void ceph_pagelist_set_cursor(struct ceph_pagelist *pl,
			      struct ceph_pagelist_cursor *c)
{
	c->pl = pl;
	c->page_lru = pl->head.prev;
	c->room = pl->room;
}
EXPORT_SYMBOL(ceph_pagelist_set_cursor);

/* Truncate a pagelist to the given point. Move extra pages to reserve.
 * This won't sleep.
 * Returns: 0 on success,
 *          -EINVAL if the pagelist doesn't match the trunc point pagelist
 */
int ceph_pagelist_truncate(struct ceph_pagelist *pl,
			   struct ceph_pagelist_cursor *c)
{
	struct page *page;

	if (pl != c->pl)
		return -EINVAL;
	ceph_pagelist_unmap_tail(pl);
	while (pl->head.prev != c->page_lru) {
		page = list_entry(pl->head.prev, struct page, lru);
		/* move from pagelist to reserve */
		list_move_tail(&page->lru, &pl->free_list);
		++pl->num_pages_free;
	}
	pl->room = c->room;
	if (!list_empty(&pl->head)) {
		page = list_entry(pl->head.prev, struct page, lru);
		pl->mapped_tail = kmap(page);
	}
	return 0;
}
EXPORT_SYMBOL(ceph_pagelist_truncate);
