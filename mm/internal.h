/* internal.h: mm/ internal definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef __MM_INTERNAL_H
#define __MM_INTERNAL_H

#include <linux/mm.h>

static inline void set_page_count(struct page *page, int v)
{
	atomic_set(&page->_count, v);
}

/*
 * Turn a non-refcounted page (->_count == 0) into refcounted with
 * a count of one.
 */
static inline void set_page_refcounted(struct page *page)
{
	VM_BUG_ON(PageTail(page));
	VM_BUG_ON(atomic_read(&page->_count));
	set_page_count(page, 1);
}

static inline void __put_page(struct page *page)
{
	atomic_dec(&page->_count);
}

extern void __init __free_pages_bootmem(struct page *page,
						unsigned int order);

/*
 * function for dealing with page's order in buddy system.
 * zone->lock is already acquired when we use these.
 * So, we don't need atomic page->flags operations here.
 */
static inline unsigned long page_order(struct page *page)
{
	VM_BUG_ON(!PageBuddy(page));
	return page_private(page);
}
#endif
