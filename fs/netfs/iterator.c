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

/*
 * Extract and pin a list of up to sg_max pages from UBUF- or IOVEC-class
 * iterators, and add them to the scatterlist.
 */
static ssize_t netfs_extract_user_to_sg(struct iov_iter *iter,
					ssize_t maxsize,
					struct sg_table *sgtable,
					unsigned int sg_max,
					iov_iter_extraction_t extraction_flags)
{
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	struct page **pages;
	unsigned int npages;
	ssize_t ret = 0, res;
	size_t len, off;

	/* We decant the page list into the tail of the scatterlist */
	pages = (void *)sgtable->sgl + array_size(sg_max, sizeof(struct scatterlist));
	pages -= sg_max;

	do {
		res = iov_iter_extract_pages(iter, &pages, maxsize, sg_max,
					     extraction_flags, &off);
		if (res < 0)
			goto failed;

		len = res;
		maxsize -= len;
		ret += len;
		npages = DIV_ROUND_UP(off + len, PAGE_SIZE);
		sg_max -= npages;

		for (; npages < 0; npages--) {
			struct page *page = *pages;
			size_t seg = min_t(size_t, PAGE_SIZE - off, len);

			*pages++ = NULL;
			sg_set_page(sg, page, len, off);
			sgtable->nents++;
			sg++;
			len -= seg;
			off = 0;
		}
	} while (maxsize > 0 && sg_max > 0);

	return ret;

failed:
	while (sgtable->nents > sgtable->orig_nents)
		put_page(sg_page(&sgtable->sgl[--sgtable->nents]));
	return res;
}

/*
 * Extract up to sg_max pages from a BVEC-type iterator and add them to the
 * scatterlist.  The pages are not pinned.
 */
static ssize_t netfs_extract_bvec_to_sg(struct iov_iter *iter,
					ssize_t maxsize,
					struct sg_table *sgtable,
					unsigned int sg_max,
					iov_iter_extraction_t extraction_flags)
{
	const struct bio_vec *bv = iter->bvec;
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	ssize_t ret = 0;

	for (i = 0; i < iter->nr_segs; i++) {
		size_t off, len;

		len = bv[i].bv_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		len = min_t(size_t, maxsize, len - start);
		off = bv[i].bv_offset + start;

		sg_set_page(sg, bv[i].bv_page, len, off);
		sgtable->nents++;
		sg++;
		sg_max--;

		ret += len;
		maxsize -= len;
		if (maxsize <= 0 || sg_max == 0)
			break;
		start = 0;
	}

	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/*
 * Extract up to sg_max pages from a KVEC-type iterator and add them to the
 * scatterlist.  This can deal with vmalloc'd buffers as well as kmalloc'd or
 * static buffers.  The pages are not pinned.
 */
static ssize_t netfs_extract_kvec_to_sg(struct iov_iter *iter,
					ssize_t maxsize,
					struct sg_table *sgtable,
					unsigned int sg_max,
					iov_iter_extraction_t extraction_flags)
{
	const struct kvec *kv = iter->kvec;
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	ssize_t ret = 0;

	for (i = 0; i < iter->nr_segs; i++) {
		struct page *page;
		unsigned long kaddr;
		size_t off, len, seg;

		len = kv[i].iov_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		kaddr = (unsigned long)kv[i].iov_base + start;
		off = kaddr & ~PAGE_MASK;
		len = min_t(size_t, maxsize, len - start);
		kaddr &= PAGE_MASK;

		maxsize -= len;
		ret += len;
		do {
			seg = min_t(size_t, len, PAGE_SIZE - off);
			if (is_vmalloc_or_module_addr((void *)kaddr))
				page = vmalloc_to_page((void *)kaddr);
			else
				page = virt_to_page(kaddr);

			sg_set_page(sg, page, len, off);
			sgtable->nents++;
			sg++;
			sg_max--;

			len -= seg;
			kaddr += PAGE_SIZE;
			off = 0;
		} while (len > 0 && sg_max > 0);

		if (maxsize <= 0 || sg_max == 0)
			break;
		start = 0;
	}

	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/*
 * Extract up to sg_max folios from an XARRAY-type iterator and add them to
 * the scatterlist.  The pages are not pinned.
 */
static ssize_t netfs_extract_xarray_to_sg(struct iov_iter *iter,
					  ssize_t maxsize,
					  struct sg_table *sgtable,
					  unsigned int sg_max,
					  iov_iter_extraction_t extraction_flags)
{
	struct scatterlist *sg = sgtable->sgl + sgtable->nents;
	struct xarray *xa = iter->xarray;
	struct folio *folio;
	loff_t start = iter->xarray_start + iter->iov_offset;
	pgoff_t index = start / PAGE_SIZE;
	ssize_t ret = 0;
	size_t offset, len;
	XA_STATE(xas, xa, index);

	rcu_read_lock();

	xas_for_each(&xas, folio, ULONG_MAX) {
		if (xas_retry(&xas, folio))
			continue;
		if (WARN_ON(xa_is_value(folio)))
			break;
		if (WARN_ON(folio_test_hugetlb(folio)))
			break;

		offset = offset_in_folio(folio, start);
		len = min_t(size_t, maxsize, folio_size(folio) - offset);

		sg_set_page(sg, folio_page(folio, 0), len, offset);
		sgtable->nents++;
		sg++;
		sg_max--;

		maxsize -= len;
		ret += len;
		if (maxsize <= 0 || sg_max == 0)
			break;
	}

	rcu_read_unlock();
	if (ret > 0)
		iov_iter_advance(iter, ret);
	return ret;
}

/**
 * netfs_extract_iter_to_sg - Extract pages from an iterator and add ot an sglist
 * @iter: The iterator to extract from
 * @maxsize: The amount of iterator to copy
 * @sgtable: The scatterlist table to fill in
 * @sg_max: Maximum number of elements in @sgtable that may be filled
 * @extraction_flags: Flags to qualify the request
 *
 * Extract the page fragments from the given amount of the source iterator and
 * add them to a scatterlist that refers to all of those bits, to a maximum
 * addition of @sg_max elements.
 *
 * The pages referred to by UBUF- and IOVEC-type iterators are extracted and
 * pinned; BVEC-, KVEC- and XARRAY-type are extracted but aren't pinned; PIPE-
 * and DISCARD-type are not supported.
 *
 * No end mark is placed on the scatterlist; that's left to the caller.
 *
 * @extraction_flags can have ITER_ALLOW_P2PDMA set to request peer-to-peer DMA
 * be allowed on the pages extracted.
 *
 * If successul, @sgtable->nents is updated to include the number of elements
 * added and the number of bytes added is returned.  @sgtable->orig_nents is
 * left unaltered.
 *
 * The iov_iter_extract_mode() function should be used to query how cleanup
 * should be performed.
 */
ssize_t netfs_extract_iter_to_sg(struct iov_iter *iter, size_t maxsize,
				 struct sg_table *sgtable, unsigned int sg_max,
				 iov_iter_extraction_t extraction_flags)
{
	if (maxsize == 0)
		return 0;

	switch (iov_iter_type(iter)) {
	case ITER_UBUF:
	case ITER_IOVEC:
		return netfs_extract_user_to_sg(iter, maxsize, sgtable, sg_max,
						extraction_flags);
	case ITER_BVEC:
		return netfs_extract_bvec_to_sg(iter, maxsize, sgtable, sg_max,
						extraction_flags);
	case ITER_KVEC:
		return netfs_extract_kvec_to_sg(iter, maxsize, sgtable, sg_max,
						extraction_flags);
	case ITER_XARRAY:
		return netfs_extract_xarray_to_sg(iter, maxsize, sgtable, sg_max,
						  extraction_flags);
	default:
		pr_err("%s(%u) unsupported\n", __func__, iov_iter_type(iter));
		WARN_ON_ONCE(1);
		return -EIO;
	}
}
EXPORT_SYMBOL_GPL(netfs_extract_iter_to_sg);
