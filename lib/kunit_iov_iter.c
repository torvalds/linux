// SPDX-License-Identifier: GPL-2.0-only
/* I/O iterator tests.  This can only test kernel-backed iterator types.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/uio.h>
#include <linux/bvec.h>
#include <linux/folio_queue.h>
#include <kunit/test.h>

MODULE_DESCRIPTION("iov_iter testing");
MODULE_AUTHOR("David Howells <dhowells@redhat.com>");
MODULE_LICENSE("GPL");

struct kvec_test_range {
	int	from, to;
};

static const struct kvec_test_range kvec_test_ranges[] = {
	{ 0x00002, 0x00002 },
	{ 0x00027, 0x03000 },
	{ 0x05193, 0x18794 },
	{ 0x20000, 0x20000 },
	{ 0x20000, 0x24000 },
	{ 0x24000, 0x27001 },
	{ 0x29000, 0xffffb },
	{ 0xffffd, 0xffffe },
	{ -1 }
};

static inline u8 pattern(unsigned long x)
{
	return x & 0xff;
}

static void iov_kunit_unmap(void *data)
{
	vunmap(data);
}

static void *__init iov_kunit_create_buffer(struct kunit *test,
					    struct page ***ppages,
					    size_t npages)
{
	struct page **pages;
	unsigned long got;
	void *buffer;

	pages = kunit_kcalloc(test, npages, sizeof(struct page *), GFP_KERNEL);
        KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pages);
	*ppages = pages;

	got = alloc_pages_bulk_array(GFP_KERNEL, npages, pages);
	if (got != npages) {
		release_pages(pages, got);
		KUNIT_ASSERT_EQ(test, got, npages);
	}

	for (int i = 0; i < npages; i++)
		pages[i]->index = i;

	buffer = vmap(pages, npages, VM_MAP | VM_MAP_PUT_PAGES, PAGE_KERNEL);
        KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buffer);

	kunit_add_action_or_reset(test, iov_kunit_unmap, buffer);
	return buffer;
}

static void __init iov_kunit_load_kvec(struct kunit *test,
				       struct iov_iter *iter, int dir,
				       struct kvec *kvec, unsigned int kvmax,
				       void *buffer, size_t bufsize,
				       const struct kvec_test_range *pr)
{
	size_t size = 0;
	int i;

	for (i = 0; i < kvmax; i++, pr++) {
		if (pr->from < 0)
			break;
		KUNIT_ASSERT_GE(test, pr->to, pr->from);
		KUNIT_ASSERT_LE(test, pr->to, bufsize);
		kvec[i].iov_base = buffer + pr->from;
		kvec[i].iov_len = pr->to - pr->from;
		size += pr->to - pr->from;
	}
	KUNIT_ASSERT_LE(test, size, bufsize);

	iov_iter_kvec(iter, dir, kvec, i, size);
}

/*
 * Test copying to a ITER_KVEC-type iterator.
 */
static void __init iov_kunit_copy_to_kvec(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct page **spages, **bpages;
	struct kvec kvec[8];
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, patt;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	for (i = 0; i < bufsize; i++)
		scratch[i] = pattern(i);

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	memset(buffer, 0, bufsize);

	iov_kunit_load_kvec(test, &iter, READ, kvec, ARRAY_SIZE(kvec),
			    buffer, bufsize, kvec_test_ranges);
	size = iter.count;

	copied = copy_to_iter(scratch, size, &iter);

	KUNIT_EXPECT_EQ(test, copied, size);
	KUNIT_EXPECT_EQ(test, iter.count, 0);
	KUNIT_EXPECT_EQ(test, iter.nr_segs, 0);

	/* Build the expected image in the scratch buffer. */
	patt = 0;
	memset(scratch, 0, bufsize);
	for (pr = kvec_test_ranges; pr->from >= 0; pr++)
		for (i = pr->from; i < pr->to; i++)
			scratch[i] = pattern(patt++);

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, buffer[i], scratch[i], "at i=%x", i);
		if (buffer[i] != scratch[i])
			return;
	}

	KUNIT_SUCCEED(test);
}

/*
 * Test copying from a ITER_KVEC-type iterator.
 */
static void __init iov_kunit_copy_from_kvec(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct page **spages, **bpages;
	struct kvec kvec[8];
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, j;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	for (i = 0; i < bufsize; i++)
		buffer[i] = pattern(i);

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	memset(scratch, 0, bufsize);

	iov_kunit_load_kvec(test, &iter, WRITE, kvec, ARRAY_SIZE(kvec),
			    buffer, bufsize, kvec_test_ranges);
	size = min(iter.count, bufsize);

	copied = copy_from_iter(scratch, size, &iter);

	KUNIT_EXPECT_EQ(test, copied, size);
	KUNIT_EXPECT_EQ(test, iter.count, 0);
	KUNIT_EXPECT_EQ(test, iter.nr_segs, 0);

	/* Build the expected image in the main buffer. */
	i = 0;
	memset(buffer, 0, bufsize);
	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		for (j = pr->from; j < pr->to; j++) {
			buffer[i++] = pattern(j);
			if (i >= bufsize)
				goto stop;
		}
	}
stop:

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, scratch[i], buffer[i], "at i=%x", i);
		if (scratch[i] != buffer[i])
			return;
	}

	KUNIT_SUCCEED(test);
}

struct bvec_test_range {
	int	page, from, to;
};

static const struct bvec_test_range bvec_test_ranges[] = {
	{ 0, 0x0002, 0x0002 },
	{ 1, 0x0027, 0x0893 },
	{ 2, 0x0193, 0x0794 },
	{ 3, 0x0000, 0x1000 },
	{ 4, 0x0000, 0x1000 },
	{ 5, 0x0000, 0x1000 },
	{ 6, 0x0000, 0x0ffb },
	{ 6, 0x0ffd, 0x0ffe },
	{ -1, -1, -1 }
};

static void __init iov_kunit_load_bvec(struct kunit *test,
				       struct iov_iter *iter, int dir,
				       struct bio_vec *bvec, unsigned int bvmax,
				       struct page **pages, size_t npages,
				       size_t bufsize,
				       const struct bvec_test_range *pr)
{
	struct page *can_merge = NULL, *page;
	size_t size = 0;
	int i;

	for (i = 0; i < bvmax; i++, pr++) {
		if (pr->from < 0)
			break;
		KUNIT_ASSERT_LT(test, pr->page, npages);
		KUNIT_ASSERT_LT(test, pr->page * PAGE_SIZE, bufsize);
		KUNIT_ASSERT_GE(test, pr->from, 0);
		KUNIT_ASSERT_GE(test, pr->to, pr->from);
		KUNIT_ASSERT_LE(test, pr->to, PAGE_SIZE);

		page = pages[pr->page];
		if (pr->from == 0 && pr->from != pr->to && page == can_merge) {
			i--;
			bvec[i].bv_len += pr->to;
		} else {
			bvec_set_page(&bvec[i], page, pr->to - pr->from, pr->from);
		}

		size += pr->to - pr->from;
		if ((pr->to & ~PAGE_MASK) == 0)
			can_merge = page + pr->to / PAGE_SIZE;
		else
			can_merge = NULL;
	}

	iov_iter_bvec(iter, dir, bvec, i, size);
}

/*
 * Test copying to a ITER_BVEC-type iterator.
 */
static void __init iov_kunit_copy_to_bvec(struct kunit *test)
{
	const struct bvec_test_range *pr;
	struct iov_iter iter;
	struct bio_vec bvec[8];
	struct page **spages, **bpages;
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, b, patt;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	for (i = 0; i < bufsize; i++)
		scratch[i] = pattern(i);

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	memset(buffer, 0, bufsize);

	iov_kunit_load_bvec(test, &iter, READ, bvec, ARRAY_SIZE(bvec),
			    bpages, npages, bufsize, bvec_test_ranges);
	size = iter.count;

	copied = copy_to_iter(scratch, size, &iter);

	KUNIT_EXPECT_EQ(test, copied, size);
	KUNIT_EXPECT_EQ(test, iter.count, 0);
	KUNIT_EXPECT_EQ(test, iter.nr_segs, 0);

	/* Build the expected image in the scratch buffer. */
	b = 0;
	patt = 0;
	memset(scratch, 0, bufsize);
	for (pr = bvec_test_ranges; pr->from >= 0; pr++, b++) {
		u8 *p = scratch + pr->page * PAGE_SIZE;

		for (i = pr->from; i < pr->to; i++)
			p[i] = pattern(patt++);
	}

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, buffer[i], scratch[i], "at i=%x", i);
		if (buffer[i] != scratch[i])
			return;
	}

	KUNIT_SUCCEED(test);
}

/*
 * Test copying from a ITER_BVEC-type iterator.
 */
static void __init iov_kunit_copy_from_bvec(struct kunit *test)
{
	const struct bvec_test_range *pr;
	struct iov_iter iter;
	struct bio_vec bvec[8];
	struct page **spages, **bpages;
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, j;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	for (i = 0; i < bufsize; i++)
		buffer[i] = pattern(i);

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	memset(scratch, 0, bufsize);

	iov_kunit_load_bvec(test, &iter, WRITE, bvec, ARRAY_SIZE(bvec),
			    bpages, npages, bufsize, bvec_test_ranges);
	size = iter.count;

	copied = copy_from_iter(scratch, size, &iter);

	KUNIT_EXPECT_EQ(test, copied, size);
	KUNIT_EXPECT_EQ(test, iter.count, 0);
	KUNIT_EXPECT_EQ(test, iter.nr_segs, 0);

	/* Build the expected image in the main buffer. */
	i = 0;
	memset(buffer, 0, bufsize);
	for (pr = bvec_test_ranges; pr->from >= 0; pr++) {
		size_t patt = pr->page * PAGE_SIZE;

		for (j = pr->from; j < pr->to; j++) {
			buffer[i++] = pattern(patt + j);
			if (i >= bufsize)
				goto stop;
		}
	}
stop:

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, scratch[i], buffer[i], "at i=%x", i);
		if (scratch[i] != buffer[i])
			return;
	}

	KUNIT_SUCCEED(test);
}

static void iov_kunit_destroy_folioq(void *data)
{
	struct folio_queue *folioq, *next;

	for (folioq = data; folioq; folioq = next) {
		next = folioq->next;
		for (int i = 0; i < folioq_nr_slots(folioq); i++)
			if (folioq_folio(folioq, i))
				folio_put(folioq_folio(folioq, i));
		kfree(folioq);
	}
}

static void __init iov_kunit_load_folioq(struct kunit *test,
					struct iov_iter *iter, int dir,
					struct folio_queue *folioq,
					struct page **pages, size_t npages)
{
	struct folio_queue *p = folioq;
	size_t size = 0;
	int i;

	for (i = 0; i < npages; i++) {
		if (folioq_full(p)) {
			p->next = kzalloc(sizeof(struct folio_queue), GFP_KERNEL);
			KUNIT_ASSERT_NOT_ERR_OR_NULL(test, p->next);
			folioq_init(p->next, 0);
			p->next->prev = p;
			p = p->next;
		}
		folioq_append(p, page_folio(pages[i]));
		size += PAGE_SIZE;
	}
	iov_iter_folio_queue(iter, dir, folioq, 0, 0, size);
}

static struct folio_queue *iov_kunit_create_folioq(struct kunit *test)
{
	struct folio_queue *folioq;

	folioq = kzalloc(sizeof(struct folio_queue), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, folioq);
	kunit_add_action_or_reset(test, iov_kunit_destroy_folioq, folioq);
	folioq_init(folioq, 0);
	return folioq;
}

/*
 * Test copying to a ITER_FOLIOQ-type iterator.
 */
static void __init iov_kunit_copy_to_folioq(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct folio_queue *folioq;
	struct page **spages, **bpages;
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, patt;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	folioq = iov_kunit_create_folioq(test);

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	for (i = 0; i < bufsize; i++)
		scratch[i] = pattern(i);

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	memset(buffer, 0, bufsize);

	iov_kunit_load_folioq(test, &iter, READ, folioq, bpages, npages);

	i = 0;
	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		size = pr->to - pr->from;
		KUNIT_ASSERT_LE(test, pr->to, bufsize);

		iov_iter_folio_queue(&iter, READ, folioq, 0, 0, pr->to);
		iov_iter_advance(&iter, pr->from);
		copied = copy_to_iter(scratch + i, size, &iter);

		KUNIT_EXPECT_EQ(test, copied, size);
		KUNIT_EXPECT_EQ(test, iter.count, 0);
		KUNIT_EXPECT_EQ(test, iter.iov_offset, pr->to % PAGE_SIZE);
		i += size;
		if (test->status == KUNIT_FAILURE)
			goto stop;
	}

	/* Build the expected image in the scratch buffer. */
	patt = 0;
	memset(scratch, 0, bufsize);
	for (pr = kvec_test_ranges; pr->from >= 0; pr++)
		for (i = pr->from; i < pr->to; i++)
			scratch[i] = pattern(patt++);

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, buffer[i], scratch[i], "at i=%x", i);
		if (buffer[i] != scratch[i])
			return;
	}

stop:
	KUNIT_SUCCEED(test);
}

/*
 * Test copying from a ITER_FOLIOQ-type iterator.
 */
static void __init iov_kunit_copy_from_folioq(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct folio_queue *folioq;
	struct page **spages, **bpages;
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, j;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	folioq = iov_kunit_create_folioq(test);

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	for (i = 0; i < bufsize; i++)
		buffer[i] = pattern(i);

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	memset(scratch, 0, bufsize);

	iov_kunit_load_folioq(test, &iter, READ, folioq, bpages, npages);

	i = 0;
	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		size = pr->to - pr->from;
		KUNIT_ASSERT_LE(test, pr->to, bufsize);

		iov_iter_folio_queue(&iter, WRITE, folioq, 0, 0, pr->to);
		iov_iter_advance(&iter, pr->from);
		copied = copy_from_iter(scratch + i, size, &iter);

		KUNIT_EXPECT_EQ(test, copied, size);
		KUNIT_EXPECT_EQ(test, iter.count, 0);
		KUNIT_EXPECT_EQ(test, iter.iov_offset, pr->to % PAGE_SIZE);
		i += size;
	}

	/* Build the expected image in the main buffer. */
	i = 0;
	memset(buffer, 0, bufsize);
	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		for (j = pr->from; j < pr->to; j++) {
			buffer[i++] = pattern(j);
			if (i >= bufsize)
				goto stop;
		}
	}
stop:

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, scratch[i], buffer[i], "at i=%x", i);
		if (scratch[i] != buffer[i])
			return;
	}

	KUNIT_SUCCEED(test);
}

static void iov_kunit_destroy_xarray(void *data)
{
	struct xarray *xarray = data;

	xa_destroy(xarray);
	kfree(xarray);
}

static void __init iov_kunit_load_xarray(struct kunit *test,
					 struct iov_iter *iter, int dir,
					 struct xarray *xarray,
					 struct page **pages, size_t npages)
{
	size_t size = 0;
	int i;

	for (i = 0; i < npages; i++) {
		void *x = xa_store(xarray, i, pages[i], GFP_KERNEL);

		KUNIT_ASSERT_FALSE(test, xa_is_err(x));
		size += PAGE_SIZE;
	}
	iov_iter_xarray(iter, dir, xarray, 0, size);
}

static struct xarray *iov_kunit_create_xarray(struct kunit *test)
{
	struct xarray *xarray;

	xarray = kzalloc(sizeof(struct xarray), GFP_KERNEL);
	xa_init(xarray);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xarray);
	kunit_add_action_or_reset(test, iov_kunit_destroy_xarray, xarray);
	return xarray;
}

/*
 * Test copying to a ITER_XARRAY-type iterator.
 */
static void __init iov_kunit_copy_to_xarray(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct xarray *xarray;
	struct page **spages, **bpages;
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, patt;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	xarray = iov_kunit_create_xarray(test);

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	for (i = 0; i < bufsize; i++)
		scratch[i] = pattern(i);

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	memset(buffer, 0, bufsize);

	iov_kunit_load_xarray(test, &iter, READ, xarray, bpages, npages);

	i = 0;
	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		size = pr->to - pr->from;
		KUNIT_ASSERT_LE(test, pr->to, bufsize);

		iov_iter_xarray(&iter, READ, xarray, pr->from, size);
		copied = copy_to_iter(scratch + i, size, &iter);

		KUNIT_EXPECT_EQ(test, copied, size);
		KUNIT_EXPECT_EQ(test, iter.count, 0);
		KUNIT_EXPECT_EQ(test, iter.iov_offset, size);
		i += size;
	}

	/* Build the expected image in the scratch buffer. */
	patt = 0;
	memset(scratch, 0, bufsize);
	for (pr = kvec_test_ranges; pr->from >= 0; pr++)
		for (i = pr->from; i < pr->to; i++)
			scratch[i] = pattern(patt++);

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, buffer[i], scratch[i], "at i=%x", i);
		if (buffer[i] != scratch[i])
			return;
	}

	KUNIT_SUCCEED(test);
}

/*
 * Test copying from a ITER_XARRAY-type iterator.
 */
static void __init iov_kunit_copy_from_xarray(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct xarray *xarray;
	struct page **spages, **bpages;
	u8 *scratch, *buffer;
	size_t bufsize, npages, size, copied;
	int i, j;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	xarray = iov_kunit_create_xarray(test);

	buffer = iov_kunit_create_buffer(test, &bpages, npages);
	for (i = 0; i < bufsize; i++)
		buffer[i] = pattern(i);

	scratch = iov_kunit_create_buffer(test, &spages, npages);
	memset(scratch, 0, bufsize);

	iov_kunit_load_xarray(test, &iter, READ, xarray, bpages, npages);

	i = 0;
	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		size = pr->to - pr->from;
		KUNIT_ASSERT_LE(test, pr->to, bufsize);

		iov_iter_xarray(&iter, WRITE, xarray, pr->from, size);
		copied = copy_from_iter(scratch + i, size, &iter);

		KUNIT_EXPECT_EQ(test, copied, size);
		KUNIT_EXPECT_EQ(test, iter.count, 0);
		KUNIT_EXPECT_EQ(test, iter.iov_offset, size);
		i += size;
	}

	/* Build the expected image in the main buffer. */
	i = 0;
	memset(buffer, 0, bufsize);
	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		for (j = pr->from; j < pr->to; j++) {
			buffer[i++] = pattern(j);
			if (i >= bufsize)
				goto stop;
		}
	}
stop:

	/* Compare the images */
	for (i = 0; i < bufsize; i++) {
		KUNIT_EXPECT_EQ_MSG(test, scratch[i], buffer[i], "at i=%x", i);
		if (scratch[i] != buffer[i])
			return;
	}

	KUNIT_SUCCEED(test);
}

/*
 * Test the extraction of ITER_KVEC-type iterators.
 */
static void __init iov_kunit_extract_pages_kvec(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct page **bpages, *pagelist[8], **pages = pagelist;
	struct kvec kvec[8];
	u8 *buffer;
	ssize_t len;
	size_t bufsize, size = 0, npages;
	int i, from;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	buffer = iov_kunit_create_buffer(test, &bpages, npages);

	iov_kunit_load_kvec(test, &iter, READ, kvec, ARRAY_SIZE(kvec),
			    buffer, bufsize, kvec_test_ranges);
	size = iter.count;

	pr = kvec_test_ranges;
	from = pr->from;
	do {
		size_t offset0 = LONG_MAX;

		for (i = 0; i < ARRAY_SIZE(pagelist); i++)
			pagelist[i] = (void *)(unsigned long)0xaa55aa55aa55aa55ULL;

		len = iov_iter_extract_pages(&iter, &pages, 100 * 1024,
					     ARRAY_SIZE(pagelist), 0, &offset0);
		KUNIT_EXPECT_GE(test, len, 0);
		if (len < 0)
			break;
		KUNIT_EXPECT_GE(test, (ssize_t)offset0, 0);
		KUNIT_EXPECT_LT(test, offset0, PAGE_SIZE);
		KUNIT_EXPECT_LE(test, len, size);
		KUNIT_EXPECT_EQ(test, iter.count, size - len);
		size -= len;

		if (len == 0)
			break;

		for (i = 0; i < ARRAY_SIZE(pagelist); i++) {
			struct page *p;
			ssize_t part = min_t(ssize_t, len, PAGE_SIZE - offset0);
			int ix;

			KUNIT_ASSERT_GE(test, part, 0);
			while (from == pr->to) {
				pr++;
				from = pr->from;
				if (from < 0)
					goto stop;
			}
			ix = from / PAGE_SIZE;
			KUNIT_ASSERT_LT(test, ix, npages);
			p = bpages[ix];
			KUNIT_EXPECT_PTR_EQ(test, pagelist[i], p);
			KUNIT_EXPECT_EQ(test, offset0, from % PAGE_SIZE);
			from += part;
			len -= part;
			KUNIT_ASSERT_GE(test, len, 0);
			if (len == 0)
				break;
			offset0 = 0;
		}

		if (test->status == KUNIT_FAILURE)
			break;
	} while (iov_iter_count(&iter) > 0);

stop:
	KUNIT_EXPECT_EQ(test, size, 0);
	KUNIT_EXPECT_EQ(test, iter.count, 0);
	KUNIT_SUCCEED(test);
}

/*
 * Test the extraction of ITER_BVEC-type iterators.
 */
static void __init iov_kunit_extract_pages_bvec(struct kunit *test)
{
	const struct bvec_test_range *pr;
	struct iov_iter iter;
	struct page **bpages, *pagelist[8], **pages = pagelist;
	struct bio_vec bvec[8];
	ssize_t len;
	size_t bufsize, size = 0, npages;
	int i, from;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	iov_kunit_create_buffer(test, &bpages, npages);
	iov_kunit_load_bvec(test, &iter, READ, bvec, ARRAY_SIZE(bvec),
			    bpages, npages, bufsize, bvec_test_ranges);
	size = iter.count;

	pr = bvec_test_ranges;
	from = pr->from;
	do {
		size_t offset0 = LONG_MAX;

		for (i = 0; i < ARRAY_SIZE(pagelist); i++)
			pagelist[i] = (void *)(unsigned long)0xaa55aa55aa55aa55ULL;

		len = iov_iter_extract_pages(&iter, &pages, 100 * 1024,
					     ARRAY_SIZE(pagelist), 0, &offset0);
		KUNIT_EXPECT_GE(test, len, 0);
		if (len < 0)
			break;
		KUNIT_EXPECT_GE(test, (ssize_t)offset0, 0);
		KUNIT_EXPECT_LT(test, offset0, PAGE_SIZE);
		KUNIT_EXPECT_LE(test, len, size);
		KUNIT_EXPECT_EQ(test, iter.count, size - len);
		size -= len;

		if (len == 0)
			break;

		for (i = 0; i < ARRAY_SIZE(pagelist); i++) {
			struct page *p;
			ssize_t part = min_t(ssize_t, len, PAGE_SIZE - offset0);
			int ix;

			KUNIT_ASSERT_GE(test, part, 0);
			while (from == pr->to) {
				pr++;
				from = pr->from;
				if (from < 0)
					goto stop;
			}
			ix = pr->page + from / PAGE_SIZE;
			KUNIT_ASSERT_LT(test, ix, npages);
			p = bpages[ix];
			KUNIT_EXPECT_PTR_EQ(test, pagelist[i], p);
			KUNIT_EXPECT_EQ(test, offset0, from % PAGE_SIZE);
			from += part;
			len -= part;
			KUNIT_ASSERT_GE(test, len, 0);
			if (len == 0)
				break;
			offset0 = 0;
		}

		if (test->status == KUNIT_FAILURE)
			break;
	} while (iov_iter_count(&iter) > 0);

stop:
	KUNIT_EXPECT_EQ(test, size, 0);
	KUNIT_EXPECT_EQ(test, iter.count, 0);
	KUNIT_SUCCEED(test);
}

/*
 * Test the extraction of ITER_FOLIOQ-type iterators.
 */
static void __init iov_kunit_extract_pages_folioq(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct folio_queue *folioq;
	struct iov_iter iter;
	struct page **bpages, *pagelist[8], **pages = pagelist;
	ssize_t len;
	size_t bufsize, size = 0, npages;
	int i, from;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	folioq = iov_kunit_create_folioq(test);

	iov_kunit_create_buffer(test, &bpages, npages);
	iov_kunit_load_folioq(test, &iter, READ, folioq, bpages, npages);

	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		from = pr->from;
		size = pr->to - from;
		KUNIT_ASSERT_LE(test, pr->to, bufsize);

		iov_iter_folio_queue(&iter, WRITE, folioq, 0, 0, pr->to);
		iov_iter_advance(&iter, from);

		do {
			size_t offset0 = LONG_MAX;

			for (i = 0; i < ARRAY_SIZE(pagelist); i++)
				pagelist[i] = (void *)(unsigned long)0xaa55aa55aa55aa55ULL;

			len = iov_iter_extract_pages(&iter, &pages, 100 * 1024,
						     ARRAY_SIZE(pagelist), 0, &offset0);
			KUNIT_EXPECT_GE(test, len, 0);
			if (len < 0)
				break;
			KUNIT_EXPECT_LE(test, len, size);
			KUNIT_EXPECT_EQ(test, iter.count, size - len);
			if (len == 0)
				break;
			size -= len;
			KUNIT_EXPECT_GE(test, (ssize_t)offset0, 0);
			KUNIT_EXPECT_LT(test, offset0, PAGE_SIZE);

			for (i = 0; i < ARRAY_SIZE(pagelist); i++) {
				struct page *p;
				ssize_t part = min_t(ssize_t, len, PAGE_SIZE - offset0);
				int ix;

				KUNIT_ASSERT_GE(test, part, 0);
				ix = from / PAGE_SIZE;
				KUNIT_ASSERT_LT(test, ix, npages);
				p = bpages[ix];
				KUNIT_EXPECT_PTR_EQ(test, pagelist[i], p);
				KUNIT_EXPECT_EQ(test, offset0, from % PAGE_SIZE);
				from += part;
				len -= part;
				KUNIT_ASSERT_GE(test, len, 0);
				if (len == 0)
					break;
				offset0 = 0;
			}

			if (test->status == KUNIT_FAILURE)
				goto stop;
		} while (iov_iter_count(&iter) > 0);

		KUNIT_EXPECT_EQ(test, size, 0);
		KUNIT_EXPECT_EQ(test, iter.count, 0);
	}

stop:
	KUNIT_SUCCEED(test);
}

/*
 * Test the extraction of ITER_XARRAY-type iterators.
 */
static void __init iov_kunit_extract_pages_xarray(struct kunit *test)
{
	const struct kvec_test_range *pr;
	struct iov_iter iter;
	struct xarray *xarray;
	struct page **bpages, *pagelist[8], **pages = pagelist;
	ssize_t len;
	size_t bufsize, size = 0, npages;
	int i, from;

	bufsize = 0x100000;
	npages = bufsize / PAGE_SIZE;

	xarray = iov_kunit_create_xarray(test);

	iov_kunit_create_buffer(test, &bpages, npages);
	iov_kunit_load_xarray(test, &iter, READ, xarray, bpages, npages);

	for (pr = kvec_test_ranges; pr->from >= 0; pr++) {
		from = pr->from;
		size = pr->to - from;
		KUNIT_ASSERT_LE(test, pr->to, bufsize);

		iov_iter_xarray(&iter, WRITE, xarray, from, size);

		do {
			size_t offset0 = LONG_MAX;

			for (i = 0; i < ARRAY_SIZE(pagelist); i++)
				pagelist[i] = (void *)(unsigned long)0xaa55aa55aa55aa55ULL;

			len = iov_iter_extract_pages(&iter, &pages, 100 * 1024,
						     ARRAY_SIZE(pagelist), 0, &offset0);
			KUNIT_EXPECT_GE(test, len, 0);
			if (len < 0)
				break;
			KUNIT_EXPECT_LE(test, len, size);
			KUNIT_EXPECT_EQ(test, iter.count, size - len);
			if (len == 0)
				break;
			size -= len;
			KUNIT_EXPECT_GE(test, (ssize_t)offset0, 0);
			KUNIT_EXPECT_LT(test, offset0, PAGE_SIZE);

			for (i = 0; i < ARRAY_SIZE(pagelist); i++) {
				struct page *p;
				ssize_t part = min_t(ssize_t, len, PAGE_SIZE - offset0);
				int ix;

				KUNIT_ASSERT_GE(test, part, 0);
				ix = from / PAGE_SIZE;
				KUNIT_ASSERT_LT(test, ix, npages);
				p = bpages[ix];
				KUNIT_EXPECT_PTR_EQ(test, pagelist[i], p);
				KUNIT_EXPECT_EQ(test, offset0, from % PAGE_SIZE);
				from += part;
				len -= part;
				KUNIT_ASSERT_GE(test, len, 0);
				if (len == 0)
					break;
				offset0 = 0;
			}

			if (test->status == KUNIT_FAILURE)
				goto stop;
		} while (iov_iter_count(&iter) > 0);

		KUNIT_EXPECT_EQ(test, size, 0);
		KUNIT_EXPECT_EQ(test, iter.count, 0);
		KUNIT_EXPECT_EQ(test, iter.iov_offset, pr->to - pr->from);
	}

stop:
	KUNIT_SUCCEED(test);
}

static struct kunit_case __refdata iov_kunit_cases[] = {
	KUNIT_CASE(iov_kunit_copy_to_kvec),
	KUNIT_CASE(iov_kunit_copy_from_kvec),
	KUNIT_CASE(iov_kunit_copy_to_bvec),
	KUNIT_CASE(iov_kunit_copy_from_bvec),
	KUNIT_CASE(iov_kunit_copy_to_folioq),
	KUNIT_CASE(iov_kunit_copy_from_folioq),
	KUNIT_CASE(iov_kunit_copy_to_xarray),
	KUNIT_CASE(iov_kunit_copy_from_xarray),
	KUNIT_CASE(iov_kunit_extract_pages_kvec),
	KUNIT_CASE(iov_kunit_extract_pages_bvec),
	KUNIT_CASE(iov_kunit_extract_pages_folioq),
	KUNIT_CASE(iov_kunit_extract_pages_xarray),
	{}
};

static struct kunit_suite iov_kunit_suite = {
	.name = "iov_iter",
	.test_cases = iov_kunit_cases,
};

kunit_test_suites(&iov_kunit_suite);
