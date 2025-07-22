// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2024 Alibaba Cloud
 */
#include "compress.h"
#include <linux/lz4.h>

#define LZ4_MAX_DISTANCE_PAGES	(DIV_ROUND_UP(LZ4_DISTANCE_MAX, PAGE_SIZE) + 1)

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
	return z_erofs_gbuf_growsize(sbi->lz4.max_pclusterblks);
}

/*
 * Fill all gaps with bounce pages if it's a sparse page list. Also check if
 * all physical pages are consecutive, which can be seen for moderate CR.
 */
static int z_erofs_lz4_prepare_dstpages(struct z_erofs_decompress_req *rq,
					struct page **pagepool)
{
	struct page *availables[LZ4_MAX_DISTANCE_PAGES] = { NULL };
	unsigned long bounced[DIV_ROUND_UP(LZ4_MAX_DISTANCE_PAGES,
					   BITS_PER_LONG)] = { 0 };
	unsigned int lz4_max_distance_pages =
				EROFS_SB(rq->sb)->lz4.max_distance_pages;
	void *kaddr = NULL;
	unsigned int i, j, top;

	top = 0;
	for (i = j = 0; i < rq->outpages; ++i, ++j) {
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
		} else {
			victim = __erofs_allocpage(pagepool, rq->gfp, true);
			if (!victim)
				return -ENOMEM;
			set_page_private(victim, Z_EROFS_SHORTLIVED_PAGE);
		}
		rq->out[i] = victim;
	}
	return kaddr ? 1 : 0;
}

static void *z_erofs_lz4_handle_overlap(struct z_erofs_decompress_req *rq,
			void *inpage, void *out, unsigned int *inputmargin,
			int *maptype, bool may_inplace)
{
	unsigned int oend, omargin, total, i;
	struct page **in;
	void *src, *tmp;

	if (rq->inplace_io) {
		oend = rq->pageofs_out + rq->outputsize;
		omargin = PAGE_ALIGN(oend) - oend;
		if (rq->partial_decoding || !may_inplace ||
		    omargin < LZ4_DECOMPRESS_INPLACE_MARGIN(rq->inputsize))
			goto docopy;

		for (i = 0; i < rq->inpages; ++i)
			if (rq->out[rq->outpages - rq->inpages + i] !=
			    rq->in[i])
				goto docopy;
		kunmap_local(inpage);
		*maptype = 3;
		return out + ((rq->outpages - rq->inpages) << PAGE_SHIFT);
	}

	if (rq->inpages <= 1) {
		*maptype = 0;
		return inpage;
	}
	kunmap_local(inpage);
	src = erofs_vm_map_ram(rq->in, rq->inpages);
	if (!src)
		return ERR_PTR(-ENOMEM);
	*maptype = 1;
	return src;

docopy:
	/* Or copy compressed data which can be overlapped to per-CPU buffer */
	in = rq->in;
	src = z_erofs_get_gbuf(rq->inpages);
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

static int z_erofs_lz4_decompress_mem(struct z_erofs_decompress_req *rq, u8 *dst)
{
	bool support_0padding = false, may_inplace = false;
	unsigned int inputmargin;
	u8 *out, *headpage, *src;
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
	src = z_erofs_lz4_handle_overlap(rq, headpage, dst, &inputmargin,
					 &maptype, may_inplace);
	if (IS_ERR(src))
		return PTR_ERR(src);

	out = dst + rq->pageofs_out;
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
		if (ret >= 0)
			memset(out + ret, 0, rq->outputsize - ret);
		ret = -EFSCORRUPTED;
	} else {
		ret = 0;
	}

	if (maptype == 0) {
		kunmap_local(headpage);
	} else if (maptype == 1) {
		vm_unmap_ram(src, rq->inpages);
	} else if (maptype == 2) {
		z_erofs_put_gbuf(src);
	} else if (maptype != 3) {
		DBG_BUGON(1);
		return -EFAULT;
	}
	return ret;
}

static int z_erofs_lz4_decompress(struct z_erofs_decompress_req *rq,
				  struct page **pagepool)
{
	unsigned int dst_maptype;
	void *dst;
	int ret;

	/* one optimized fast path only for non bigpcluster cases yet */
	if (rq->inpages == 1 && rq->outpages == 1 && !rq->inplace_io) {
		DBG_BUGON(!*rq->out);
		dst = kmap_local_page(*rq->out);
		dst_maptype = 0;
	} else {
		/* general decoding path which can be used for all cases */
		ret = z_erofs_lz4_prepare_dstpages(rq, pagepool);
		if (ret < 0)
			return ret;
		if (ret > 0) {
			dst = page_address(*rq->out);
			dst_maptype = 1;
		} else {
			dst = erofs_vm_map_ram(rq->out, rq->outpages);
			if (!dst)
				return -ENOMEM;
			dst_maptype = 2;
		}
	}
	ret = z_erofs_lz4_decompress_mem(rq, dst);
	if (!dst_maptype)
		kunmap_local(dst);
	else if (dst_maptype == 2)
		vm_unmap_ram(dst, rq->outpages);
	return ret;
}

static int z_erofs_transform_plain(struct z_erofs_decompress_req *rq,
				   struct page **pagepool)
{
	const unsigned int nrpages_in = rq->inpages, nrpages_out = rq->outpages;
	const unsigned int bs = rq->sb->s_blocksize;
	unsigned int cur = 0, ni = 0, no, pi, po, insz, cnt;
	u8 *kin;

	if (rq->outputsize > rq->inputsize)
		return -EOPNOTSUPP;
	if (rq->alg == Z_EROFS_COMPRESSION_INTERLACED) {
		cur = bs - (rq->pageofs_out & (bs - 1));
		pi = (rq->pageofs_in + rq->inputsize - cur) & ~PAGE_MASK;
		cur = min(cur, rq->outputsize);
		if (cur && rq->out[0]) {
			kin = kmap_local_page(rq->in[nrpages_in - 1]);
			if (rq->out[0] == rq->in[nrpages_in - 1])
				memmove(kin + rq->pageofs_out, kin + pi, cur);
			else
				memcpy_to_page(rq->out[0], rq->pageofs_out,
					       kin + pi, cur);
			kunmap_local(kin);
		}
		rq->outputsize -= cur;
	}

	for (; rq->outputsize; rq->pageofs_in = 0, cur += insz, ni++) {
		insz = min(PAGE_SIZE - rq->pageofs_in, rq->outputsize);
		rq->outputsize -= insz;
		if (!rq->in[ni])
			continue;
		kin = kmap_local_page(rq->in[ni]);
		pi = 0;
		do {
			no = (rq->pageofs_out + cur + pi) >> PAGE_SHIFT;
			po = (rq->pageofs_out + cur + pi) & ~PAGE_MASK;
			DBG_BUGON(no >= nrpages_out);
			cnt = min(insz - pi, PAGE_SIZE - po);
			if (rq->out[no] == rq->in[ni])
				memmove(kin + po,
					kin + rq->pageofs_in + pi, cnt);
			else if (rq->out[no])
				memcpy_to_page(rq->out[no], po,
					       kin + rq->pageofs_in + pi, cnt);
			pi += cnt;
		} while (pi < insz);
		kunmap_local(kin);
	}
	DBG_BUGON(ni > nrpages_in);
	return 0;
}

int z_erofs_stream_switch_bufs(struct z_erofs_stream_dctx *dctx, void **dst,
			       void **src, struct page **pgpl)
{
	struct z_erofs_decompress_req *rq = dctx->rq;
	struct super_block *sb = rq->sb;
	struct page **pgo, *tmppage;
	unsigned int j;

	if (!dctx->avail_out) {
		if (++dctx->no >= rq->outpages || !rq->outputsize) {
			erofs_err(sb, "insufficient space for decompressed data");
			return -EFSCORRUPTED;
		}

		if (dctx->kout)
			kunmap_local(dctx->kout);
		dctx->avail_out = min(rq->outputsize, PAGE_SIZE - rq->pageofs_out);
		rq->outputsize -= dctx->avail_out;
		pgo = &rq->out[dctx->no];
		if (!*pgo && rq->fillgaps) {		/* deduped */
			*pgo = erofs_allocpage(pgpl, rq->gfp);
			if (!*pgo) {
				dctx->kout = NULL;
				return -ENOMEM;
			}
			set_page_private(*pgo, Z_EROFS_SHORTLIVED_PAGE);
		}
		if (*pgo) {
			dctx->kout = kmap_local_page(*pgo);
			*dst = dctx->kout + rq->pageofs_out;
		} else {
			*dst = dctx->kout = NULL;
		}
		rq->pageofs_out = 0;
	}

	if (dctx->inbuf_pos == dctx->inbuf_sz && rq->inputsize) {
		if (++dctx->ni >= rq->inpages) {
			erofs_err(sb, "invalid compressed data");
			return -EFSCORRUPTED;
		}
		if (dctx->kout) /* unlike kmap(), take care of the orders */
			kunmap_local(dctx->kout);
		kunmap_local(dctx->kin);

		dctx->inbuf_sz = min_t(u32, rq->inputsize, PAGE_SIZE);
		rq->inputsize -= dctx->inbuf_sz;
		dctx->kin = kmap_local_page(rq->in[dctx->ni]);
		*src = dctx->kin;
		dctx->bounced = false;
		if (dctx->kout) {
			j = (u8 *)*dst - dctx->kout;
			dctx->kout = kmap_local_page(rq->out[dctx->no]);
			*dst = dctx->kout + j;
		}
		dctx->inbuf_pos = 0;
	}

	/*
	 * Handle overlapping: Use the given bounce buffer if the input data is
	 * under processing; Or utilize short-lived pages from the on-stack page
	 * pool, where pages are shared among the same request.  Note that only
	 * a few inplace I/O pages need to be doubled.
	 */
	if (!dctx->bounced && rq->out[dctx->no] == rq->in[dctx->ni]) {
		memcpy(dctx->bounce, *src, dctx->inbuf_sz);
		*src = dctx->bounce;
		dctx->bounced = true;
	}

	for (j = dctx->ni + 1; j < rq->inpages; ++j) {
		if (rq->out[dctx->no] != rq->in[j])
			continue;
		tmppage = erofs_allocpage(pgpl, rq->gfp);
		if (!tmppage)
			return -ENOMEM;
		set_page_private(tmppage, Z_EROFS_SHORTLIVED_PAGE);
		copy_highpage(tmppage, rq->in[j]);
		rq->in[j] = tmppage;
	}
	return 0;
}

const struct z_erofs_decompressor *z_erofs_decomp[] = {
	[Z_EROFS_COMPRESSION_SHIFTED] = &(const struct z_erofs_decompressor) {
		.decompress = z_erofs_transform_plain,
		.name = "shifted"
	},
	[Z_EROFS_COMPRESSION_INTERLACED] = &(const struct z_erofs_decompressor) {
		.decompress = z_erofs_transform_plain,
		.name = "interlaced"
	},
	[Z_EROFS_COMPRESSION_LZ4] = &(const struct z_erofs_decompressor) {
		.config = z_erofs_load_lz4_config,
		.decompress = z_erofs_lz4_decompress,
		.init = z_erofs_gbuf_init,
		.exit = z_erofs_gbuf_exit,
		.name = "lz4"
	},
#ifdef CONFIG_EROFS_FS_ZIP_LZMA
	[Z_EROFS_COMPRESSION_LZMA] = &z_erofs_lzma_decomp,
#endif
#ifdef CONFIG_EROFS_FS_ZIP_DEFLATE
	[Z_EROFS_COMPRESSION_DEFLATE] = &z_erofs_deflate_decomp,
#endif
#ifdef CONFIG_EROFS_FS_ZIP_ZSTD
	[Z_EROFS_COMPRESSION_ZSTD] = &z_erofs_zstd_decomp,
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

	(void)erofs_init_metabuf(&buf, sb, false);
	offset = EROFS_SUPER_OFFSET + sbi->sb_size;
	alg = 0;
	for (algs = sbi->available_compr_algs; algs; algs >>= 1, ++alg) {
		const struct z_erofs_decompressor *dec = z_erofs_decomp[alg];
		void *data;

		if (!(algs & 1))
			continue;

		data = erofs_read_metadata(sb, &buf, &offset, &size);
		if (IS_ERR(data)) {
			ret = PTR_ERR(data);
			break;
		}

		if (alg < Z_EROFS_COMPRESSION_MAX && dec && dec->config) {
			ret = dec->config(sb, dsb, data, size);
		} else {
			erofs_err(sb, "algorithm %d isn't enabled on this kernel",
				  alg);
			ret = -EOPNOTSUPP;
		}
		kfree(data);
		if (ret)
			break;
	}
	erofs_put_metabuf(&buf);
	return ret;
}

int __init z_erofs_init_decompressor(void)
{
	int i, err;

	for (i = 0; i < Z_EROFS_COMPRESSION_MAX; ++i) {
		err = z_erofs_decomp[i] ? z_erofs_decomp[i]->init() : 0;
		if (err) {
			while (i--)
				if (z_erofs_decomp[i])
					z_erofs_decomp[i]->exit();
			return err;
		}
	}
	return 0;
}

void z_erofs_exit_decompressor(void)
{
	int i;

	for (i = 0; i < Z_EROFS_COMPRESSION_MAX; ++i)
		if (z_erofs_decomp[i])
			z_erofs_decomp[i]->exit();
}
