// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#include "compress.h"
#include <linux/module.h>
#include <linux/lz4.h>

#ifndef LZ4_DISTANCE_MAX	/* history window size */
#define LZ4_DISTANCE_MAX 65535	/* set to maximum value by default */
#endif

#define LZ4_MAX_DISTANCE_PAGES	(DIV_ROUND_UP(LZ4_DISTANCE_MAX, PAGE_SIZE) + 1)
#ifndef LZ4_DECOMPRESS_INPLACE_MARGIN
#define LZ4_DECOMPRESS_INPLACE_MARGIN(srcsize)  (((srcsize) >> 8) + 32)
#endif

struct z_erofs_decompressor {
	/*
	 * if destpages have sparsed pages, fill them with bounce pages.
	 * it also check whether destpages indicate continuous physical memory.
	 */
	int (*prepare_destpages)(struct z_erofs_decompress_req *rq,
				 struct list_head *pagepool);
	int (*decompress)(struct z_erofs_decompress_req *rq, u8 *out);
	char *name;
};

static int z_erofs_lz4_prepare_destpages(struct z_erofs_decompress_req *rq,
					 struct list_head *pagepool)
{
	const unsigned int nr =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	struct page *availables[LZ4_MAX_DISTANCE_PAGES] = { NULL };
	unsigned long bounced[DIV_ROUND_UP(LZ4_MAX_DISTANCE_PAGES,
					   BITS_PER_LONG)] = { 0 };
	void *kaddr = NULL;
	unsigned int i, j, top;

	top = 0;
	for (i = j = 0; i < nr; ++i, ++j) {
		struct page *const page = rq->out[i];
		struct page *victim;

		if (j >= LZ4_MAX_DISTANCE_PAGES)
			j = 0;

		/* 'valid' bounced can only be tested after a complete round */
		if (test_bit(j, bounced)) {
			DBG_BUGON(i < LZ4_MAX_DISTANCE_PAGES);
			DBG_BUGON(top >= LZ4_MAX_DISTANCE_PAGES);
			availables[top++] = rq->out[i - LZ4_MAX_DISTANCE_PAGES];
		}

		if (page) {
			__clear_bit(j, bounced);
			if (kaddr) {
				if (kaddr + PAGE_SIZE == page_address(page))
					kaddr += PAGE_SIZE;
				else
					kaddr = NULL;
			} else if (!i) {
				kaddr = page_address(page);
			}
			continue;
		}
		kaddr = NULL;
		__set_bit(j, bounced);

		if (top) {
			victim = availables[--top];
			get_page(victim);
		} else {
			victim = erofs_allocpage(pagepool, GFP_KERNEL);
			if (!victim)
				return -ENOMEM;
			victim->mapping = Z_EROFS_MAPPING_STAGING;
		}
		rq->out[i] = victim;
	}
	return kaddr ? 1 : 0;
}

static void *generic_copy_inplace_data(struct z_erofs_decompress_req *rq,
				       u8 *src, unsigned int pageofs_in)
{
	/*
	 * if in-place decompression is ongoing, those decompressed
	 * pages should be copied in order to avoid being overlapped.
	 */
	struct page **in = rq->in;
	u8 *const tmp = erofs_get_pcpubuf(0);
	u8 *tmpp = tmp;
	unsigned int inlen = rq->inputsize - pageofs_in;
	unsigned int count = min_t(uint, inlen, PAGE_SIZE - pageofs_in);

	while (tmpp < tmp + inlen) {
		if (!src)
			src = kmap_atomic(*in);
		memcpy(tmpp, src + pageofs_in, count);
		kunmap_atomic(src);
		src = NULL;
		tmpp += count;
		pageofs_in = 0;
		count = PAGE_SIZE;
		++in;
	}
	return tmp;
}

static int z_erofs_lz4_decompress(struct z_erofs_decompress_req *rq, u8 *out)
{
	unsigned int inputmargin, inlen;
	u8 *src;
	bool copied, support_0padding;
	int ret;

	if (rq->inputsize > PAGE_SIZE)
		return -EOPNOTSUPP;

	src = kmap_atomic(*rq->in);
	inputmargin = 0;
	support_0padding = false;

	/* decompression inplace is only safe when 0padding is enabled */
	if (EROFS_SB(rq->sb)->feature_incompat &
	    EROFS_FEATURE_INCOMPAT_LZ4_0PADDING) {
		support_0padding = true;

		while (!src[inputmargin & ~PAGE_MASK])
			if (!(++inputmargin & ~PAGE_MASK))
				break;

		if (inputmargin >= rq->inputsize) {
			kunmap_atomic(src);
			return -EIO;
		}
	}

	copied = false;
	inlen = rq->inputsize - inputmargin;
	if (rq->inplace_io) {
		const uint oend = (rq->pageofs_out +
				   rq->outputsize) & ~PAGE_MASK;
		const uint nr = PAGE_ALIGN(rq->pageofs_out +
					   rq->outputsize) >> PAGE_SHIFT;

		if (rq->partial_decoding || !support_0padding ||
		    rq->out[nr - 1] != rq->in[0] ||
		    rq->inputsize - oend <
		      LZ4_DECOMPRESS_INPLACE_MARGIN(inlen)) {
			src = generic_copy_inplace_data(rq, src, inputmargin);
			inputmargin = 0;
			copied = true;
		}
	}

	ret = LZ4_decompress_safe_partial(src + inputmargin, out,
					  inlen, rq->outputsize,
					  rq->outputsize);
	if (ret < 0) {
		erofs_err(rq->sb, "failed to decompress, in[%u, %u] out[%u]",
			  inlen, inputmargin, rq->outputsize);
		WARN_ON(1);
		print_hex_dump(KERN_DEBUG, "[ in]: ", DUMP_PREFIX_OFFSET,
			       16, 1, src + inputmargin, inlen, true);
		print_hex_dump(KERN_DEBUG, "[out]: ", DUMP_PREFIX_OFFSET,
			       16, 1, out, rq->outputsize, true);
		ret = -EIO;
	}

	if (copied)
		erofs_put_pcpubuf(src);
	else
		kunmap_atomic(src);
	return ret;
}

static struct z_erofs_decompressor decompressors[] = {
	[Z_EROFS_COMPRESSION_SHIFTED] = {
		.name = "shifted"
	},
	[Z_EROFS_COMPRESSION_LZ4] = {
		.prepare_destpages = z_erofs_lz4_prepare_destpages,
		.decompress = z_erofs_lz4_decompress,
		.name = "lz4"
	},
};

static void copy_from_pcpubuf(struct page **out, const char *dst,
			      unsigned short pageofs_out,
			      unsigned int outputsize)
{
	const char *end = dst + outputsize;
	const unsigned int righthalf = PAGE_SIZE - pageofs_out;
	const char *cur = dst - pageofs_out;

	while (cur < end) {
		struct page *const page = *out++;

		if (page) {
			char *buf = kmap_atomic(page);

			if (cur >= dst) {
				memcpy(buf, cur, min_t(uint, PAGE_SIZE,
						       end - cur));
			} else {
				memcpy(buf + pageofs_out, cur + pageofs_out,
				       min_t(uint, righthalf, end - cur));
			}
			kunmap_atomic(buf);
		}
		cur += PAGE_SIZE;
	}
}

static int z_erofs_decompress_generic(struct z_erofs_decompress_req *rq,
				      struct list_head *pagepool)
{
	const unsigned int nrpages_out =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	const struct z_erofs_decompressor *alg = decompressors + rq->alg;
	unsigned int dst_maptype;
	void *dst;
	int ret, i;

	if (nrpages_out == 1 && !rq->inplace_io) {
		DBG_BUGON(!*rq->out);
		dst = kmap_atomic(*rq->out);
		dst_maptype = 0;
		goto dstmap_out;
	}

	/*
	 * For the case of small output size (especially much less
	 * than PAGE_SIZE), memcpy the decompressed data rather than
	 * compressed data is preferred.
	 */
	if (rq->outputsize <= PAGE_SIZE * 7 / 8) {
		dst = erofs_get_pcpubuf(0);
		if (IS_ERR(dst))
			return PTR_ERR(dst);

		rq->inplace_io = false;
		ret = alg->decompress(rq, dst);
		if (!ret)
			copy_from_pcpubuf(rq->out, dst, rq->pageofs_out,
					  rq->outputsize);

		erofs_put_pcpubuf(dst);
		return ret;
	}

	ret = alg->prepare_destpages(rq, pagepool);
	if (ret < 0) {
		return ret;
	} else if (ret) {
		dst = page_address(*rq->out);
		dst_maptype = 1;
		goto dstmap_out;
	}

	i = 0;
	while (1) {
		dst = vm_map_ram(rq->out, nrpages_out, -1, PAGE_KERNEL);

		/* retry two more times (totally 3 times) */
		if (dst || ++i >= 3)
			break;
		vm_unmap_aliases();
	}

	if (!dst)
		return -ENOMEM;

	dst_maptype = 2;

dstmap_out:
	ret = alg->decompress(rq, dst + rq->pageofs_out);

	if (!dst_maptype)
		kunmap_atomic(dst);
	else if (dst_maptype == 2)
		vm_unmap_ram(dst, nrpages_out);
	return ret;
}

static int z_erofs_shifted_transform(const struct z_erofs_decompress_req *rq,
				     struct list_head *pagepool)
{
	const unsigned int nrpages_out =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	const unsigned int righthalf = PAGE_SIZE - rq->pageofs_out;
	unsigned char *src, *dst;

	if (nrpages_out > 2) {
		DBG_BUGON(1);
		return -EIO;
	}

	if (rq->out[0] == *rq->in) {
		DBG_BUGON(nrpages_out != 1);
		return 0;
	}

	src = kmap_atomic(*rq->in);
	if (!rq->out[0]) {
		dst = NULL;
	} else {
		dst = kmap_atomic(rq->out[0]);
		memcpy(dst + rq->pageofs_out, src, righthalf);
	}

	if (rq->out[1] == *rq->in) {
		memmove(src, src + righthalf, rq->pageofs_out);
	} else if (nrpages_out == 2) {
		if (dst)
			kunmap_atomic(dst);
		DBG_BUGON(!rq->out[1]);
		dst = kmap_atomic(rq->out[1]);
		memcpy(dst, src + righthalf, rq->pageofs_out);
	}
	if (dst)
		kunmap_atomic(dst);
	kunmap_atomic(src);
	return 0;
}

int z_erofs_decompress(struct z_erofs_decompress_req *rq,
		       struct list_head *pagepool)
{
	if (rq->alg == Z_EROFS_COMPRESSION_SHIFTED)
		return z_erofs_shifted_transform(rq, pagepool);
	return z_erofs_decompress_generic(rq, pagepool);
}

