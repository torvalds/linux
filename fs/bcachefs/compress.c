// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "checksum.h"
#include "compress.h"
#include "extents.h"
#include "super-io.h"

#include <linux/lz4.h>
#include <linux/zlib.h>
#include <linux/zstd.h>

/* Bounce buffer: */
struct bbuf {
	void		*b;
	enum {
		BB_NONE,
		BB_VMAP,
		BB_KMALLOC,
		BB_MEMPOOL,
	}		type;
	int		rw;
};

static struct bbuf __bounce_alloc(struct bch_fs *c, unsigned size, int rw)
{
	void *b;

	BUG_ON(size > c->opts.encoded_extent_max);

	b = kmalloc(size, GFP_NOFS|__GFP_NOWARN);
	if (b)
		return (struct bbuf) { .b = b, .type = BB_KMALLOC, .rw = rw };

	b = mempool_alloc(&c->compression_bounce[rw], GFP_NOFS);
	if (b)
		return (struct bbuf) { .b = b, .type = BB_MEMPOOL, .rw = rw };

	BUG();
}

static bool bio_phys_contig(struct bio *bio, struct bvec_iter start)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	void *expected_start = NULL;

	__bio_for_each_bvec(bv, bio, iter, start) {
		if (expected_start &&
		    expected_start != page_address(bv.bv_page) + bv.bv_offset)
			return false;

		expected_start = page_address(bv.bv_page) +
			bv.bv_offset + bv.bv_len;
	}

	return true;
}

static struct bbuf __bio_map_or_bounce(struct bch_fs *c, struct bio *bio,
				       struct bvec_iter start, int rw)
{
	struct bbuf ret;
	struct bio_vec bv;
	struct bvec_iter iter;
	unsigned nr_pages = 0;
	struct page *stack_pages[16];
	struct page **pages = NULL;
	void *data;

	BUG_ON(start.bi_size > c->opts.encoded_extent_max);

	if (!PageHighMem(bio_iter_page(bio, start)) &&
	    bio_phys_contig(bio, start))
		return (struct bbuf) {
			.b = page_address(bio_iter_page(bio, start)) +
				bio_iter_offset(bio, start),
			.type = BB_NONE, .rw = rw
		};

	/* check if we can map the pages contiguously: */
	__bio_for_each_segment(bv, bio, iter, start) {
		if (iter.bi_size != start.bi_size &&
		    bv.bv_offset)
			goto bounce;

		if (bv.bv_len < iter.bi_size &&
		    bv.bv_offset + bv.bv_len < PAGE_SIZE)
			goto bounce;

		nr_pages++;
	}

	BUG_ON(DIV_ROUND_UP(start.bi_size, PAGE_SIZE) > nr_pages);

	pages = nr_pages > ARRAY_SIZE(stack_pages)
		? kmalloc_array(nr_pages, sizeof(struct page *), GFP_NOFS)
		: stack_pages;
	if (!pages)
		goto bounce;

	nr_pages = 0;
	__bio_for_each_segment(bv, bio, iter, start)
		pages[nr_pages++] = bv.bv_page;

	data = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
	if (pages != stack_pages)
		kfree(pages);

	if (data)
		return (struct bbuf) {
			.b = data + bio_iter_offset(bio, start),
			.type = BB_VMAP, .rw = rw
		};
bounce:
	ret = __bounce_alloc(c, start.bi_size, rw);

	if (rw == READ)
		memcpy_from_bio(ret.b, bio, start);

	return ret;
}

static struct bbuf bio_map_or_bounce(struct bch_fs *c, struct bio *bio, int rw)
{
	return __bio_map_or_bounce(c, bio, bio->bi_iter, rw);
}

static void bio_unmap_or_unbounce(struct bch_fs *c, struct bbuf buf)
{
	switch (buf.type) {
	case BB_NONE:
		break;
	case BB_VMAP:
		vunmap((void *) ((unsigned long) buf.b & PAGE_MASK));
		break;
	case BB_KMALLOC:
		kfree(buf.b);
		break;
	case BB_MEMPOOL:
		mempool_free(buf.b, &c->compression_bounce[buf.rw]);
		break;
	}
}

static inline void zlib_set_workspace(z_stream *strm, void *workspace)
{
#ifdef __KERNEL__
	strm->workspace = workspace;
#endif
}

static int __bio_uncompress(struct bch_fs *c, struct bio *src,
			    void *dst_data, struct bch_extent_crc_unpacked crc)
{
	struct bbuf src_data = { NULL };
	size_t src_len = src->bi_iter.bi_size;
	size_t dst_len = crc.uncompressed_size << 9;
	void *workspace;
	int ret;

	src_data = bio_map_or_bounce(c, src, READ);

	switch (crc.compression_type) {
	case BCH_COMPRESSION_TYPE_lz4_old:
	case BCH_COMPRESSION_TYPE_lz4:
		ret = LZ4_decompress_safe_partial(src_data.b, dst_data,
						  src_len, dst_len, dst_len);
		if (ret != dst_len)
			goto err;
		break;
	case BCH_COMPRESSION_TYPE_gzip: {
		z_stream strm = {
			.next_in	= src_data.b,
			.avail_in	= src_len,
			.next_out	= dst_data,
			.avail_out	= dst_len,
		};

		workspace = mempool_alloc(&c->decompress_workspace, GFP_NOFS);

		zlib_set_workspace(&strm, workspace);
		zlib_inflateInit2(&strm, -MAX_WBITS);
		ret = zlib_inflate(&strm, Z_FINISH);

		mempool_free(workspace, &c->decompress_workspace);

		if (ret != Z_STREAM_END)
			goto err;
		break;
	}
	case BCH_COMPRESSION_TYPE_zstd: {
		ZSTD_DCtx *ctx;
		size_t real_src_len = le32_to_cpup(src_data.b);

		if (real_src_len > src_len - 4)
			goto err;

		workspace = mempool_alloc(&c->decompress_workspace, GFP_NOFS);
		ctx = zstd_init_dctx(workspace, zstd_dctx_workspace_bound());

		ret = zstd_decompress_dctx(ctx,
				dst_data,	dst_len,
				src_data.b + 4, real_src_len);

		mempool_free(workspace, &c->decompress_workspace);

		if (ret != dst_len)
			goto err;
		break;
	}
	default:
		BUG();
	}
	ret = 0;
out:
	bio_unmap_or_unbounce(c, src_data);
	return ret;
err:
	ret = -EIO;
	goto out;
}

int bch2_bio_uncompress_inplace(struct bch_fs *c, struct bio *bio,
				struct bch_extent_crc_unpacked *crc)
{
	struct bbuf data = { NULL };
	size_t dst_len = crc->uncompressed_size << 9;

	/* bio must own its pages: */
	BUG_ON(!bio->bi_vcnt);
	BUG_ON(DIV_ROUND_UP(crc->live_size, PAGE_SECTORS) > bio->bi_max_vecs);

	if (crc->uncompressed_size << 9	> c->opts.encoded_extent_max ||
	    crc->compressed_size << 9	> c->opts.encoded_extent_max) {
		bch_err(c, "error rewriting existing data: extent too big");
		return -EIO;
	}

	data = __bounce_alloc(c, dst_len, WRITE);

	if (__bio_uncompress(c, bio, data.b, *crc)) {
		if (!c->opts.no_data_io)
			bch_err(c, "error rewriting existing data: decompression error");
		bio_unmap_or_unbounce(c, data);
		return -EIO;
	}

	/*
	 * XXX: don't have a good way to assert that the bio was allocated with
	 * enough space, we depend on bch2_move_extent doing the right thing
	 */
	bio->bi_iter.bi_size = crc->live_size << 9;

	memcpy_to_bio(bio, bio->bi_iter, data.b + (crc->offset << 9));

	crc->csum_type		= 0;
	crc->compression_type	= 0;
	crc->compressed_size	= crc->live_size;
	crc->uncompressed_size	= crc->live_size;
	crc->offset		= 0;
	crc->csum		= (struct bch_csum) { 0, 0 };

	bio_unmap_or_unbounce(c, data);
	return 0;
}

int bch2_bio_uncompress(struct bch_fs *c, struct bio *src,
		       struct bio *dst, struct bvec_iter dst_iter,
		       struct bch_extent_crc_unpacked crc)
{
	struct bbuf dst_data = { NULL };
	size_t dst_len = crc.uncompressed_size << 9;
	int ret;

	if (crc.uncompressed_size << 9	> c->opts.encoded_extent_max ||
	    crc.compressed_size << 9	> c->opts.encoded_extent_max)
		return -EIO;

	dst_data = dst_len == dst_iter.bi_size
		? __bio_map_or_bounce(c, dst, dst_iter, WRITE)
		: __bounce_alloc(c, dst_len, WRITE);

	ret = __bio_uncompress(c, src, dst_data.b, crc);
	if (ret)
		goto err;

	if (dst_data.type != BB_NONE &&
	    dst_data.type != BB_VMAP)
		memcpy_to_bio(dst, dst_iter, dst_data.b + (crc.offset << 9));
err:
	bio_unmap_or_unbounce(c, dst_data);
	return ret;
}

static int attempt_compress(struct bch_fs *c,
			    void *workspace,
			    void *dst, size_t dst_len,
			    void *src, size_t src_len,
			    struct bch_compression_opt compression)
{
	enum bch_compression_type compression_type =
		__bch2_compression_opt_to_type[compression.type];

	switch (compression_type) {
	case BCH_COMPRESSION_TYPE_lz4:
		if (compression.level < LZ4HC_MIN_CLEVEL) {
			int len = src_len;
			int ret = LZ4_compress_destSize(
					src,		dst,
					&len,		dst_len,
					workspace);
			if (len < src_len)
				return -len;

			return ret;
		} else {
			int ret = LZ4_compress_HC(
					src,		dst,
					src_len,	dst_len,
					compression.level,
					workspace);

			return ret ?: -1;
		}
	case BCH_COMPRESSION_TYPE_gzip: {
		z_stream strm = {
			.next_in	= src,
			.avail_in	= src_len,
			.next_out	= dst,
			.avail_out	= dst_len,
		};

		zlib_set_workspace(&strm, workspace);
		zlib_deflateInit2(&strm,
				  compression.level
				  ? clamp_t(unsigned, compression.level,
					    Z_BEST_SPEED, Z_BEST_COMPRESSION)
				  : Z_DEFAULT_COMPRESSION,
				  Z_DEFLATED, -MAX_WBITS, DEF_MEM_LEVEL,
				  Z_DEFAULT_STRATEGY);

		if (zlib_deflate(&strm, Z_FINISH) != Z_STREAM_END)
			return 0;

		if (zlib_deflateEnd(&strm) != Z_OK)
			return 0;

		return strm.total_out;
	}
	case BCH_COMPRESSION_TYPE_zstd: {
		/*
		 * rescale:
		 * zstd max compression level is 22, our max level is 15
		 */
		unsigned level = min((compression.level * 3) / 2, zstd_max_clevel());
		ZSTD_parameters params = zstd_get_params(level, c->opts.encoded_extent_max);
		ZSTD_CCtx *ctx = zstd_init_cctx(workspace, c->zstd_workspace_size);

		/*
		 * ZSTD requires that when we decompress we pass in the exact
		 * compressed size - rounding it up to the nearest sector
		 * doesn't work, so we use the first 4 bytes of the buffer for
		 * that.
		 *
		 * Additionally, the ZSTD code seems to have a bug where it will
		 * write just past the end of the buffer - so subtract a fudge
		 * factor (7 bytes) from the dst buffer size to account for
		 * that.
		 */
		size_t len = zstd_compress_cctx(ctx,
				dst + 4,	dst_len - 4 - 7,
				src,		src_len,
				&params);
		if (zstd_is_error(len))
			return 0;

		*((__le32 *) dst) = cpu_to_le32(len);
		return len + 4;
	}
	default:
		BUG();
	}
}

static unsigned __bio_compress(struct bch_fs *c,
			       struct bio *dst, size_t *dst_len,
			       struct bio *src, size_t *src_len,
			       struct bch_compression_opt compression)
{
	struct bbuf src_data = { NULL }, dst_data = { NULL };
	void *workspace;
	enum bch_compression_type compression_type =
		__bch2_compression_opt_to_type[compression.type];
	unsigned pad;
	int ret = 0;

	BUG_ON(compression_type >= BCH_COMPRESSION_TYPE_NR);
	BUG_ON(!mempool_initialized(&c->compress_workspace[compression_type]));

	/* If it's only one block, don't bother trying to compress: */
	if (src->bi_iter.bi_size <= c->opts.block_size)
		return BCH_COMPRESSION_TYPE_incompressible;

	dst_data = bio_map_or_bounce(c, dst, WRITE);
	src_data = bio_map_or_bounce(c, src, READ);

	workspace = mempool_alloc(&c->compress_workspace[compression_type], GFP_NOFS);

	*src_len = src->bi_iter.bi_size;
	*dst_len = dst->bi_iter.bi_size;

	/*
	 * XXX: this algorithm sucks when the compression code doesn't tell us
	 * how much would fit, like LZ4 does:
	 */
	while (1) {
		if (*src_len <= block_bytes(c)) {
			ret = -1;
			break;
		}

		ret = attempt_compress(c, workspace,
				       dst_data.b,	*dst_len,
				       src_data.b,	*src_len,
				       compression);
		if (ret > 0) {
			*dst_len = ret;
			ret = 0;
			break;
		}

		/* Didn't fit: should we retry with a smaller amount?  */
		if (*src_len <= *dst_len) {
			ret = -1;
			break;
		}

		/*
		 * If ret is negative, it's a hint as to how much data would fit
		 */
		BUG_ON(-ret >= *src_len);

		if (ret < 0)
			*src_len = -ret;
		else
			*src_len -= (*src_len - *dst_len) / 2;
		*src_len = round_down(*src_len, block_bytes(c));
	}

	mempool_free(workspace, &c->compress_workspace[compression_type]);

	if (ret)
		goto err;

	/* Didn't get smaller: */
	if (round_up(*dst_len, block_bytes(c)) >= *src_len)
		goto err;

	pad = round_up(*dst_len, block_bytes(c)) - *dst_len;

	memset(dst_data.b + *dst_len, 0, pad);
	*dst_len += pad;

	if (dst_data.type != BB_NONE &&
	    dst_data.type != BB_VMAP)
		memcpy_to_bio(dst, dst->bi_iter, dst_data.b);

	BUG_ON(!*dst_len || *dst_len > dst->bi_iter.bi_size);
	BUG_ON(!*src_len || *src_len > src->bi_iter.bi_size);
	BUG_ON(*dst_len & (block_bytes(c) - 1));
	BUG_ON(*src_len & (block_bytes(c) - 1));
	ret = compression_type;
out:
	bio_unmap_or_unbounce(c, src_data);
	bio_unmap_or_unbounce(c, dst_data);
	return ret;
err:
	ret = BCH_COMPRESSION_TYPE_incompressible;
	goto out;
}

unsigned bch2_bio_compress(struct bch_fs *c,
			   struct bio *dst, size_t *dst_len,
			   struct bio *src, size_t *src_len,
			   unsigned compression_opt)
{
	unsigned orig_dst = dst->bi_iter.bi_size;
	unsigned orig_src = src->bi_iter.bi_size;
	unsigned compression_type;

	/* Don't consume more than BCH_ENCODED_EXTENT_MAX from @src: */
	src->bi_iter.bi_size = min_t(unsigned, src->bi_iter.bi_size,
				     c->opts.encoded_extent_max);
	/* Don't generate a bigger output than input: */
	dst->bi_iter.bi_size = min(dst->bi_iter.bi_size, src->bi_iter.bi_size);

	compression_type =
		__bio_compress(c, dst, dst_len, src, src_len,
			       bch2_compression_decode(compression_opt));

	dst->bi_iter.bi_size = orig_dst;
	src->bi_iter.bi_size = orig_src;
	return compression_type;
}

static int __bch2_fs_compress_init(struct bch_fs *, u64);

#define BCH_FEATURE_none	0

static const unsigned bch2_compression_opt_to_feature[] = {
#define x(t, n) [BCH_COMPRESSION_OPT_##t] = BCH_FEATURE_##t,
	BCH_COMPRESSION_OPTS()
#undef x
};

#undef BCH_FEATURE_none

static int __bch2_check_set_has_compressed_data(struct bch_fs *c, u64 f)
{
	int ret = 0;

	if ((c->sb.features & f) == f)
		return 0;

	mutex_lock(&c->sb_lock);

	if ((c->sb.features & f) == f) {
		mutex_unlock(&c->sb_lock);
		return 0;
	}

	ret = __bch2_fs_compress_init(c, c->sb.features|f);
	if (ret) {
		mutex_unlock(&c->sb_lock);
		return ret;
	}

	c->disk_sb.sb->features[0] |= cpu_to_le64(f);
	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return 0;
}

int bch2_check_set_has_compressed_data(struct bch_fs *c,
				       unsigned compression_opt)
{
	unsigned compression_type = bch2_compression_decode(compression_opt).type;

	BUG_ON(compression_type >= ARRAY_SIZE(bch2_compression_opt_to_feature));

	return compression_type
		? __bch2_check_set_has_compressed_data(c,
				1ULL << bch2_compression_opt_to_feature[compression_type])
		: 0;
}

void bch2_fs_compress_exit(struct bch_fs *c)
{
	unsigned i;

	mempool_exit(&c->decompress_workspace);
	for (i = 0; i < ARRAY_SIZE(c->compress_workspace); i++)
		mempool_exit(&c->compress_workspace[i]);
	mempool_exit(&c->compression_bounce[WRITE]);
	mempool_exit(&c->compression_bounce[READ]);
}

static int __bch2_fs_compress_init(struct bch_fs *c, u64 features)
{
	size_t decompress_workspace_size = 0;
	ZSTD_parameters params = zstd_get_params(zstd_max_clevel(),
						 c->opts.encoded_extent_max);

	/*
	 * ZSTD is lying: if we allocate the size of the workspace it says it
	 * requires, it returns memory allocation errors
	 */
	c->zstd_workspace_size = zstd_cctx_workspace_bound(&params.cParams);

	struct {
		unsigned			feature;
		enum bch_compression_type	type;
		size_t				compress_workspace;
		size_t				decompress_workspace;
	} compression_types[] = {
		{ BCH_FEATURE_lz4, BCH_COMPRESSION_TYPE_lz4,
			max_t(size_t, LZ4_MEM_COMPRESS, LZ4HC_MEM_COMPRESS),
			0 },
		{ BCH_FEATURE_gzip, BCH_COMPRESSION_TYPE_gzip,
			zlib_deflate_workspacesize(MAX_WBITS, DEF_MEM_LEVEL),
			zlib_inflate_workspacesize(), },
		{ BCH_FEATURE_zstd, BCH_COMPRESSION_TYPE_zstd,
			c->zstd_workspace_size,
			zstd_dctx_workspace_bound() },
	}, *i;
	bool have_compressed = false;

	for (i = compression_types;
	     i < compression_types + ARRAY_SIZE(compression_types);
	     i++)
		have_compressed |= (features & (1 << i->feature)) != 0;

	if (!have_compressed)
		return 0;

	if (!mempool_initialized(&c->compression_bounce[READ]) &&
	    mempool_init_kvpmalloc_pool(&c->compression_bounce[READ],
					1, c->opts.encoded_extent_max))
		return -BCH_ERR_ENOMEM_compression_bounce_read_init;

	if (!mempool_initialized(&c->compression_bounce[WRITE]) &&
	    mempool_init_kvpmalloc_pool(&c->compression_bounce[WRITE],
					1, c->opts.encoded_extent_max))
		return -BCH_ERR_ENOMEM_compression_bounce_write_init;

	for (i = compression_types;
	     i < compression_types + ARRAY_SIZE(compression_types);
	     i++) {
		decompress_workspace_size =
			max(decompress_workspace_size, i->decompress_workspace);

		if (!(features & (1 << i->feature)))
			continue;

		if (mempool_initialized(&c->compress_workspace[i->type]))
			continue;

		if (mempool_init_kvpmalloc_pool(
				&c->compress_workspace[i->type],
				1, i->compress_workspace))
			return -BCH_ERR_ENOMEM_compression_workspace_init;
	}

	if (!mempool_initialized(&c->decompress_workspace) &&
	    mempool_init_kvpmalloc_pool(&c->decompress_workspace,
					1, decompress_workspace_size))
		return -BCH_ERR_ENOMEM_decompression_workspace_init;

	return 0;
}

static u64 compression_opt_to_feature(unsigned v)
{
	unsigned type = bch2_compression_decode(v).type;

	return BIT_ULL(bch2_compression_opt_to_feature[type]);
}

int bch2_fs_compress_init(struct bch_fs *c)
{
	u64 f = c->sb.features;

	f |= compression_opt_to_feature(c->opts.compression);
	f |= compression_opt_to_feature(c->opts.background_compression);

	return __bch2_fs_compress_init(c, f);
}

int bch2_opt_compression_parse(struct bch_fs *c, const char *_val, u64 *res,
			       struct printbuf *err)
{
	char *val = kstrdup(_val, GFP_KERNEL);
	char *p = val, *type_str, *level_str;
	struct bch_compression_opt opt = { 0 };
	int ret;

	if (!val)
		return -ENOMEM;

	type_str = strsep(&p, ":");
	level_str = p;

	ret = match_string(bch2_compression_opts, -1, type_str);
	if (ret < 0 && err)
		prt_str(err, "invalid compression type");
	if (ret < 0)
		goto err;

	opt.type = ret;

	if (level_str) {
		unsigned level;

		ret = kstrtouint(level_str, 10, &level);
		if (!ret && !opt.type && level)
			ret = -EINVAL;
		if (!ret && level > 15)
			ret = -EINVAL;
		if (ret < 0 && err)
			prt_str(err, "invalid compression level");
		if (ret < 0)
			goto err;

		opt.level = level;
	}

	*res = bch2_compression_encode(opt);
err:
	kfree(val);
	return ret;
}

void bch2_compression_opt_to_text(struct printbuf *out, u64 v)
{
	struct bch_compression_opt opt = bch2_compression_decode(v);

	if (opt.type < BCH_COMPRESSION_OPT_NR)
		prt_str(out, bch2_compression_opts[opt.type]);
	else
		prt_printf(out, "(unknown compression opt %u)", opt.type);
	if (opt.level)
		prt_printf(out, ":%u", opt.level);
}

void bch2_opt_compression_to_text(struct printbuf *out,
				  struct bch_fs *c,
				  struct bch_sb *sb,
				  u64 v)
{
	return bch2_compression_opt_to_text(out, v);
}

int bch2_opt_compression_validate(u64 v, struct printbuf *err)
{
	if (!bch2_compression_opt_valid(v)) {
		prt_printf(err, "invalid compression opt %llu", v);
		return -BCH_ERR_invalid_sb_opt_compression;
	}

	return 0;
}
