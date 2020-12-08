/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#ifndef __EROFS_FS_COMPRESS_H
#define __EROFS_FS_COMPRESS_H

#include "internal.h"

enum {
	Z_EROFS_COMPRESSION_SHIFTED = Z_EROFS_COMPRESSION_MAX,
	Z_EROFS_COMPRESSION_RUNTIME_MAX
};

struct z_erofs_decompress_req {
	struct super_block *sb;
	struct page **in, **out;

	unsigned short pageofs_out;
	unsigned int inputsize, outputsize;

	/* indicate the algorithm will be used for decompression */
	unsigned int alg;
	bool inplace_io, partial_decoding;
};

/* some special page->private (unsigned long, see below) */
#define Z_EROFS_SHORTLIVED_PAGE		(-1UL << 2)

/*
 * For all pages in a pcluster, page->private should be one of
 * Type                         Last 2bits      page->private
 * short-lived page             00              Z_EROFS_SHORTLIVED_PAGE
 * cached/managed page          00              pointer to z_erofs_pcluster
 * online page (file-backed,    01/10/11        sub-index << 2 | count
 *              some pages can be used for inplace I/O)
 *
 * page->mapping should be one of
 * Type                 page->mapping
 * short-lived page     NULL
 * cached/managed page  non-NULL or NULL (invalidated/truncated page)
 * online page          non-NULL
 *
 * For all managed pages, PG_private should be set with 1 extra refcount,
 * which is used for page reclaim / migration.
 */

/*
 * short-lived pages are pages directly from buddy system with specific
 * page->private (no need to set PagePrivate since these are non-LRU /
 * non-movable pages and bypass reclaim / migration code).
 */
static inline bool z_erofs_is_shortlived_page(struct page *page)
{
	if (page->private != Z_EROFS_SHORTLIVED_PAGE)
		return false;

	DBG_BUGON(page->mapping);
	return true;
}

static inline bool z_erofs_put_shortlivedpage(struct list_head *pagepool,
					      struct page *page)
{
	if (!z_erofs_is_shortlived_page(page))
		return false;

	/* short-lived pages should not be used by others at the same time */
	if (page_ref_count(page) > 1) {
		put_page(page);
	} else {
		/* follow the pcluster rule above. */
		set_page_private(page, 0);
		list_add(&page->lru, pagepool);
	}
	return true;
}

int z_erofs_decompress(struct z_erofs_decompress_req *rq,
		       struct list_head *pagepool);

#endif

