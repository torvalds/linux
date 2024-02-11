// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 HUAWEI, Inc.
 *             https://www.huawei.com/
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

struct z_erofs_lz4_decompress_ctx {
	struct z_erofs_decompress_req *rq;
	/* # of encoded, decoded pages */
	unsigned int inpages, outpages;
	/* decoded block total length (used for in-place decompression) */
	unsigned int oend;
};

static int z_erofs_load_lz4_config(struct super_block *sb,
			    struct erofs_super_block *dsb, void *data, int size)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct z_erofs_lz4_cfgs *lz4 = data;
	u16 distance;

	if (lz4) {
		if (size < sizeof(struct z_erofs_lz4_cfgs)) {
			erofs_err(sb, "invalid lz4 cfgs, size=%u", size);
			return -EINVAL;
		}
		distance = le16_to_cpu(lz4->max_distance);

		sbi->lz4.max_pclusterblks = le16_to_cpu(lz4->max_pclusterblks);
		if (!sbi->lz4.max_pclusterblks) {
			sbi->lz4.max_pclusterblks = 1;	/* reserved case */
		} else if (sbi->lz4.max_pclusterblks >
			   erofs_blknr(sb, Z_EROFS_PCLUSTER_MAX_SIZE)) {
			erofs_err(sb, "too large lz4 pclusterblks %u",
				  sbi->lz4.max_pclusterblks);
			return -EINVAL;
		}
	} else {
		distance = le16_to_cpu(dsb->u1.lz4_max_distance);
		sbi->lz4.max_pclusterblks = 1;
	}

	sbi->lz4.max_distance_pages = distance ?
					DIV_ROUND_UP(distance, PAGE_SIZE) + 1 :
					LZ4_MAX_DISTANCE_PAGES;
	return erofs_pcpubuf_growsize(sbi->lz4.max_pclusterblks);
}

/*
 * Fill all gaps with bounce pages if it's a sparse page list. Also check if
 * all physical pages are consecutive, which can be seen for moderate CR.
 */
static int z_erofs_lz4_prepare_dstpages(struct z_erofs_lz4_decompress_ctx *ctx,
					struct page **pagepool)
{
	struct z_erofs_decompress_req *rq = ctx->rq;
	struct page *availables[LZ4_MAX_DISTANCE_PAGES] = { NULL };
	unsigned long bounced[DIV_ROUND_UP(LZ4_MAX_DISTANCE_PAGES,
					   BITS_PER_LONG)] = { 0 };
	unsigned int lz4_max_distance_pages =
				EROFS_SB(rq->sb)->lz4.max_distance_pages;
	void *kaddr = NULL;
	unsigned int i, j, top;

	top = 0;
	for (i = j = 0; i < ctx->outpages; ++i, ++j) {
		struct page *const page = rq->out[i];
		struct page *victim;

		if (j >= lz4_max_distance_pages)
			j = 0;

		/* 'valid' bounced can only be tested after a complete round */
		if (!rq->fillgaps && test_bit(j, bounced)) {
			DBG_BUGON(i < lz4_max_distance_pages);
			DBG_BUGON(top >= lz4_max_distance_pages);
			availables[top++] = rq->out[i - lz4_max_distance_pages];
		}

		if (page) {
			__clear_bit(j, bounced);
			if (!PageHighMem(page)) {
				if (!i) {
					kaddr = page_address(page);
					continue;
				}
				if (kaddr &&
				    kaddr + PAGE_SIZE == page_address(page)) {
					kaddr += PAGE_SIZE;
					continue;
				}
			}
			kaddr = NULL;
			continue;
		}
		kaddr = NULL;
		__set_bit(j, bounced);

		if (top) {
			victim = availables[--top];
			get_page(victim);
		} else {
			victim = erofs_allocpage(pagepool,
						 GFP_KERNEL | __GFP_NOFAIL);
			set_page_private(victim, Z_EROFS_SHORTLIVED_PAGE);
		}
		rq->out[i] = victim;
	}
	return kaddr ? 1 : 0;
}

static void *z_erofs_lz4_handle_overlap(struct z_erofs_lz4_decompress_ctx *ctx,
			void *inpage, unsigned int *inputmargin, int *maptype,
			bool may_inplace)
{
	struct z_erofs_decompress_req *rq = ctx->rq;
	unsigned int omargin, total, i, j;
	struct page **in;
	void *src, *tmp;

	if (rq->inplace_io) {
		omargin = PAGE_ALIGN(ctx->oend) - ctx->oend;
		if (rq->partial_decoding || !may_inplace ||
		    omargin < LZ4_DECOMPRESS_INPLACE_MARGIN(rq->inputsize))
			goto docopy;

		for (i = 0; i < ctx->inpages; ++i) {
			DBG_BUGON(rq->in[i] == NULL);
			for (j = 0; j < ctx->outpages - ctx->inpages + i; ++j)
				if (rq->out[j] == rq->in[i])
					goto docopy;
		}
	}

	if (ctx->inpages <= 1) {
		*maptype = 0;
		return inpage;
	}
	kunmap_local(inpage);
	might_sleep();
	src = erofs_vm_map_ram(rq->in, ctx->inpages);
	if (!src)
		return ERR_PTR(-ENOMEM);
	*maptype = 1;
	return src;

docopy:
	/* Or copy compressed data which can be overlapped to per-CPU buffer */
	in = rq->in;
	src = erofs_get_pcpubuf(ctx->inpages);
	if (!src) {
		DBG_BUGON(1);
		kunmap_local(inpage);
		return ERR_PTR(-EFAULT);
	}

	tmp = src;
	total = rq->inputsize;
	while (total) {
		unsigned int page_copycnt =
			min_t(unsigned int, total, PAGE_SIZE - *inputmargin);

		if (!inpage)
			inpage = kmap_local_page(*in);
		memcpy(tmp, inpage + *inputmargin, page_copycnt);
		kunmap_local(inpage);
		inpage = NULL;
		tmp += page_copycnt;
		total -= page_copycnt;
		++in;
		*inputmargin = 0;
	}
	*maptype = 2;
	return src;
}

/*
 * Get the exact inputsize with zero_padding feature.
 *  - For LZ4, it should work if zero_padding feature is on (5.3+);
 *  - For MicroLZMA, it'd be enabled all the time.
 */
int z_erofs_fixup_insize(struct z_erofs_decompress_req *rq, const char *padbuf,
			 unsigned int padbufsize)
{
	const char *padend;

	padend = memchr_inv(padbuf, 0, padbufsize);
	if (!padend)
		return -EFSCORRUPTED;
	rq->inputsize -= padend - padbuf;
	rq->pageofs_in += padend - padbuf;
	return 0;
}

static int z_erofs_lz4_decompress_mem(struct z_erofs_lz4_decompress_ctx *ctx,
				      u8 *out)
{
	struct z_erofs_decompress_req *rq = ctx->rq;
	bool support_0padding = false, may_inplace = false;
	unsigned int inputmargin;
	u8 *headpage, *src;
	int ret, maptype;

	DBG_BUGON(*rq->in == NULL);
	headpage = kmap_local_page(*rq->in);

	/* LZ4 decompression inplace is only safe if zero_padding is enabled */
	if (erofs_sb_has_zero_padding(EROFS_SB(rq->sb))) {
		support_0padding = true;
		ret = z_erofs_fixup_insize(rq, headpage + rq->pageofs_in,
				min_t(unsigned int, rq->inputsize,
				      rq->sb->s_blocksize - rq->pageofs_in));
		if (ret) {
			kunmap_local(headpage);
			return ret;
		}
		may_inplace = !((rq->pageofs_in + rq->inputsize) &
				(rq->sb->s_blocksize - 1));
	}

	inputmargin = rq->pageofs_in;
	src = z_erofs_lz4_handle_overlap(ctx, headpage, &inputmargin,
					 &maptype, may_inplace);
	if (IS_ERR(src))
		return PTR_ERR(src);

	/* legacy format could compress extra data in a pcluster. */
	if (rq->partial_decoding || !support_0padding)
		ret = LZ4_decompress_safe_partial(src + inputmargin, out,
				rq->inputsize, rq->outputsize, rq->outputsize);
	else
		ret = LZ4_decompress_safe(src + inputmargin, out,
					  rq->inputsize, rq->outputsize);

	if (ret != rq->outputsize) {
		erofs_err(rq->sb, "failed to decompress %d in[%u, %u] out[%u]",
			  ret, rq->inputsize, inputmargin, rq->outputsize);

		print_hex_dump(KERN_DEBUG, "[ in]: ", DUMP_PREFIX_OFFSET,
			       16, 1, src + inputmargin, rq->inputsize, true);
		print_hex_dump(KERN_DEBUG, "[out]: ", DUMP_PREFIX_OFFSET,
			       16, 1, out, rq->outputsize, true);

		if (ret >= 0)
			memset(out + ret, 0, rq->outputsize - ret);
		ret = -EIO;
	} else {
		ret = 0;
	}

	if (maptype == 0) {
		kunmap_local(headpage);
	} else if (maptype == 1) {
		vm_unmap_ram(src, ctx->inpages);
	} else if (maptype == 2) {
		erofs_put_pcpubuf(src);
	} else {
		DBG_BUGON(1);
		return -EFAULT;
	}
	return ret;
}

static int z_erofs_lz4_decompress(struct z_erofs_decompress_req *rq,
				  struct page **pagepool)
{
	struct z_erofs_lz4_decompress_ctx ctx;
	unsigned int dst_maptype;
	void *dst;
	int ret;

	ctx.rq = rq;
	ctx.oend = rq->pageofs_out + rq->outputsize;
	ctx.outpages = PAGE_ALIGN(ctx.oend) >> PAGE_SHIFT;
	ctx.inpages = PAGE_ALIGN(rq->inputsize) >> PAGE_SHIFT;

	/* one optimized fast path only for non bigpcluster cases yet */
	if (ctx.inpages == 1 && ctx.outpages == 1 && !rq->inplace_io) {
		DBG_BUGON(!*rq->out);
		dst = kmap_local_page(*rq->out);
		dst_maptype = 0;
		goto dstmap_out;
	}

	/* general decoding path which can be used for all cases */
	ret = z_erofs_lz4_prepare_dstpages(&ctx, pagepool);
	if (ret < 0) {
		return ret;
	} else if (ret > 0) {
		dst = page_address(*rq->out);
		dst_maptype = 1;
	} else {
		dst = erofs_vm_map_ram(rq->out, ctx.outpages);
		if (!dst)
			return -ENOMEM;
		dst_maptype = 2;
	}

dstmap_out:
	ret = z_erofs_lz4_decompress_mem(&ctx, dst + rq->pageofs_out);
	if (!dst_maptype)
		kunmap_local(dst);
	else if (dst_maptype == 2)
		vm_unmap_ram(dst, ctx.outpages);
	return ret;
}

static int z_erofs_transform_plain(struct z_erofs_decompress_req *rq,
				   struct page **pagepool)
{
	const unsigned int inpages = PAGE_ALIGN(rq->inputsize) >> PAGE_SHIFT;
	const unsigned int outpages =
		PAGE_ALIGN(rq->pageofs_out + rq->outputsize) >> PAGE_SHIFT;
	const unsigned int righthalf = min_t(unsigned int, rq->outputsize,
					     PAGE_SIZE - rq->pageofs_out);
	const unsigned int lefthalf = rq->outputsize - righthalf;
	const unsigned int interlaced_offset =
		rq->alg == Z_EROFS_COMPRESSION_SHIFTED ? 0 : rq->pageofs_out;
	u8 *src;

	if (outpages > 2 && rq->alg == Z_EROFS_COMPRESSION_SHIFTED) {
		DBG_BUGON(1);
		return -EFSCORRUPTED;
	}

	if (rq->out[0] == *rq->in) {
		DBG_BUGON(rq->pageofs_out);
		return 0;
	}

	src = kmap_local_page(rq->in[inpages - 1]) + rq->pageofs_in;
	if (rq->out[0])
		memcpy_to_page(rq->out[0], rq->pageofs_out,
			       src + interlaced_offset, righthalf);

	if (outpages > inpages) {
		DBG_BUGON(!rq->out[outpages - 1]);
		if (rq->out[outpages - 1] != rq->in[inpages - 1]) {
			memcpy_to_page(rq->out[outpages - 1], 0, src +
					(interlaced_offset ? 0 : righthalf),
				       lefthalf);
		} else if (!interlaced_offset) {
			memmove(src, src + righthalf, lefthalf);
			flush_dcache_page(rq->in[inpages - 1]);
		}
	}
	kunmap_local(src);
	return 0;
}

const struct z_erofs_decompressor erofs_decompressors[] = {
	[Z_EROFS_COMPRESSION_SHIFTED] = {
		.decompress = z_erofs_transform_plain,
		.name = "shifted"
	},
	[Z_EROFS_COMPRESSION_INTERLACED] = {
		.decompress = z_erofs_transform_plain,
		.name = "interlaced"
	},
	[Z_EROFS_COMPRESSION_LZ4] = {
		.config = z_erofs_load_lz4_config,
		.decompress = z_erofs_lz4_decompress,
		.name = "lz4"
	},
#ifdef CONFIG_EROFS_FS_ZIP_LZMA
	[Z_EROFS_COMPRESSION_LZMA] = {
		.config = z_erofs_load_lzma_config,
		.decompress = z_erofs_lzma_decompress,
		.name = "lzma"
	},
#endif
#ifdef CONFIG_EROFS_FS_ZIP_DEFLATE
	[Z_EROFS_COMPRESSION_DEFLATE] = {
		.config = z_erofs_load_deflate_config,
		.decompress = z_erofs_deflate_decompress,
		.name = "deflate"
	},
#endif
};

int z_erofs_parse_cfgs(struct super_block *sb, struct erofs_super_block *dsb)
{
	struct erofs_sb_info *sbi = EROFS_SB(sb);
	struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
	unsigned int algs, alg;
	erofs_off_t offset;
	int size, ret = 0;

	if (!erofs_sb_has_compr_cfgs(sbi)) {
		sbi->available_compr_algs = 1 << Z_EROFS_COMPRESSION_LZ4;
		return z_erofs_load_lz4_config(sb, dsb, NULL, 0);
	}

	sbi->available_compr_algs = le16_to_cpu(dsb->u1.available_compr_algs);
	if (sbi->available_compr_algs & ~Z_EROFS_ALL_COMPR_ALGS) {
		erofs_err(sb, "unidentified algorithms %x, please upgrade kernel",
			  sbi->available_compr_algs & ~Z_EROFS_ALL_COMPR_ALGS);
		return -EOPNOTSUPP;
	}

	erofs_init_metabuf(&buf, sb);
	offset = EROFS_SUPER_OFFSET + sbi->sb_size;
	alg = 0;
	for (algs = sbi->available_compr_algs; algs; algs >>= 1, ++alg) {
		void *data;

		if (!(algs & 1))
			continue;

		data = erofs_read_metadata(sb, &buf, &offset, &size);
		if (IS_ERR(data)) {
			ret = PTR_ERR(data);
			break;
		}

		if (alg >= ARRAY_SIZE(erofs_decompressors) ||
		    !erofs_decompressors[alg].config) {
			erofs_err(sb, "algorithm %d isn't enabled on this kernel",
				  alg);
			ret = -EOPNOTSUPP;
		} else {
			ret = erofs_decompressors[alg].config(sb,
					dsb, data, size);
		}

		kfree(data);
		if (ret)
			break;
	}
	erofs_put_metabuf(&buf);
	return ret;
}
