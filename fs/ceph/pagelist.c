
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>

#include "pagelist.h"

static void ceph_pagelist_unmap_tail(struct ceph_pagelist *pl)
{
	struct page *page = list_entry(pl->head.prev, struct page,
				       lru);
	kunmap(page);
}

int ceph_pagelist_release(struct ceph_pagelist *pl)
{
	if (pl->mapped_tail)
		ceph_pagelist_unmap_tail(pl);

	while (!list_empty(&pl->head)) {
		struct page *page = list_first_entry(&pl->head, struct page,
						     lru);
		list_del(&page->lru);
		__free_page(page);
	}
	return 0;
}

static int ceph_pagelist_addpage(struct ceph_pagelist *pl)
{
	struct page *page = __page_cache_alloc(GFP_NOFS);
	if (!page)
		return -ENOMEM;
	pl->room += PAGE_SIZE;
	list_add_tail(&page->lru, &pl->head);
	if (pl->mapped_tail)
		ceph_pagelist_unmap_tail(pl);
	pl->mapped_tail = kmap(page);
	return 0;
}

int ceph_pagelist_append(struct ceph_pagelist *pl, void *buf, size_t len)
{
	while (pl->room < len) {
		size_t bit = pl->room;
		int ret;

		memcpy(pl->mapped_tail + (pl->length & ~PAGE_CACHE_MASK),
		       buf, bit);
		pl->length += bit;
		pl->room -= bit;
		buf += bit;
		len -= bit;
		ret = ceph_pagelist_addpage(pl);
		if (ret)
			return ret;
	}

	memcpy(pl->mapped_tail + (pl->length & ~PAGE_CACHE_MASK), buf, len);
	pl->length += len;
	pl->room -= len;
	return 0;
}
