// SPDX-License-Identifier: GPL-2.0-or-later
/* Iterator helpers.
 *
 * Copyright (C) 2022 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/uio.h>
#include <linux/scatterlist.h>
#include <linux/netfs.h>
#include "internal.h"

/**
 * netfs_extract_user_iter - Extract the pages from a user iterator into a bvec
 * @orig: The original iterator
 * @orig_len: The amount of iterator to copy
 * @new: The iterator to be set up
 * @extraction_flags: Flags to qualify the request
 *
 * Extract the page fragments from the given amount of the source iterator and
 * build up a second iterator that refers to all of those bits.  This allows
 * the original iterator to disposed of.
 *
 * @extraction_flags can have ITER_ALLOW_P2PDMA set to request peer-to-peer DMA be
 * allowed on the pages extracted.
 *
 * On success, the number of elements in the bvec is returned, the original
 * iterator will have been advanced by the amount extracted.
 *
 * The iov_iter_extract_mode() function should be used to query how cleanup
 * should be performed.
 */
ssize_t netfs_extract_user_iter(struct iov_iter *orig, size_t orig_len,
				struct iov_iter *new,
				iov_iter_extraction_t extraction_flags)
{
	struct bio_vec *bv = NULL;
	struct page **pages;
	unsigned int cur_npages;
	unsigned int max_pages;
	unsigned int npages = 0;
	unsigned int i;
	ssize_t ret;
	size_t count = orig_len, offset, len;
	size_t bv_size, pg_size;

	if (WARN_ON_ONCE(!iter_is_ubuf(orig) && !iter_is_iovec(orig)))
		return -EIO;

	max_pages = iov_iter_npages(orig, INT_MAX);
	bv_size = array_size(max_pages, sizeof(*bv));
	bv = kvmalloc(bv_size, GFP_KERNEL);
	if (!bv)
		return -ENOMEM;

	/* Put the page list at the end of the bvec list storage.  bvec
	 * elements are larger than page pointers, so as long as we work
	 * 0->last, we should be fine.
	 */
	pg_size = array_size(max_pages, sizeof(*pages));
	pages = (void *)bv + bv_size - pg_size;

	while (count && npages < max_pages) {
		ret = iov_iter_extract_pages(orig, &pages, count,
					     max_pages - npages, extraction_flags,
					     &offset);
		if (ret < 0) {
			pr_err("Couldn't get user pages (rc=%zd)\n", ret);
			break;
		}

		if (ret > count) {
			pr_err("get_pages rc=%zd more than %zu\n", ret, count);
			break;
		}

		count -= ret;
		ret += offset;
		cur_npages = DIV_ROUND_UP(ret, PAGE_SIZE);

		if (npages + cur_npages > max_pages) {
			pr_err("Out of bvec array capacity (%u vs %u)\n",
			       npages + cur_npages, max_pages);
			break;
		}

		for (i = 0; i < cur_npages; i++) {
			len = ret > PAGE_SIZE ? PAGE_SIZE : ret;
			bvec_set_page(bv + npages + i, *pages++, len - offset, offset);
			ret -= len;
			offset = 0;
		}

		npages += cur_npages;
	}

	iov_iter_bvec(new, orig->data_source, bv, npages, orig_len - count);
	return npages;
}
EXPORT_SYMBOL_GPL(netfs_extract_user_iter);
