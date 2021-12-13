// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs compress support
 *
 * Copyright (c) 2019 Chao Yu <chao@kernel.org>
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/moduleparam.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/lzo.h>
#include <linux/lz4.h>
#include <linux/zstd.h>
#include <linux/pagevec.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include <trace/events/f2fs.h>

static struct kmem_cache *cic_entry_slab;
static struct kmem_cache *dic_entry_slab;

static void *page_array_alloc(struct inode *inode, int nr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned int size = sizeof(struct page *) * nr;

	if (likely(size <= sbi->page_array_slab_size))
		return f2fs_kmem_cache_alloc(sbi->page_array_slab,
					GFP_F2FS_ZERO, false, F2FS_I_SB(inode));
	return f2fs_kzalloc(sbi, size, GFP_NOFS);
}

static void page_array_free(struct inode *inode, void *pages, int nr)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned int size = sizeof(struct page *) * nr;

	if (!pages)
		return;

	if (likely(size <= sbi->page_array_slab_size))
		kmem_cache_free(sbi->page_array_slab, pages);
	else
		kfree(pages);
}

struct f2fs_compress_ops {
	int (*init_compress_ctx)(struct compress_ctx *cc);
	void (*destroy_compress_ctx)(struct compress_ctx *cc);
	int (*compress_pages)(struct compress_ctx *cc);
	int (*init_decompress_ctx)(struct decompress_io_ctx *dic);
	void (*destroy_decompress_ctx)(struct decompress_io_ctx *dic);
	int (*decompress_pages)(struct decompress_io_ctx *dic);
};

static unsigned int offset_in_cluster(struct compress_ctx *cc, pgoff_t index)
{
	return index & (cc->cluster_size - 1);
}

static pgoff_t cluster_idx(struct compress_ctx *cc, pgoff_t index)
{
	return index >> cc->log_cluster_size;
}

static pgoff_t start_idx_of_cluster(struct compress_ctx *cc)
{
	return cc->cluster_idx << cc->log_cluster_size;
}

bool f2fs_is_compressed_page(struct page *page)
{
	if (!PagePrivate(page))
		return false;
	if (!page_private(page))
		return false;
	if (page_private_nonpointer(page))
		return false;

	f2fs_bug_on(F2FS_M_SB(page->mapping),
		*((u32 *)page_private(page)) != F2FS_COMPRESSED_PAGE_MAGIC);
	return true;
}

static void f2fs_set_compressed_page(struct page *page,
		struct inode *inode, pgoff_t index, void *data)
{
	attach_page_private(page, (void *)data);

	/* i_crypto_info and iv index */
	page->index = index;
	page->mapping = inode->i_mapping;
}

static void f2fs_drop_rpages(struct compress_ctx *cc, int len, bool unlock)
{
	int i;

	for (i = 0; i < len; i++) {
		if (!cc->rpages[i])
			continue;
		if (unlock)
			unlock_page(cc->rpages[i]);
		else
			put_page(cc->rpages[i]);
	}
}

static void f2fs_put_rpages(struct compress_ctx *cc)
{
	f2fs_drop_rpages(cc, cc->cluster_size, false);
}

static void f2fs_unlock_rpages(struct compress_ctx *cc, int len)
{
	f2fs_drop_rpages(cc, len, true);
}

static void f2fs_put_rpages_wbc(struct compress_ctx *cc,
		struct writeback_control *wbc, bool redirty, int unlock)
{
	unsigned int i;

	for (i = 0; i < cc->cluster_size; i++) {
		if (!cc->rpages[i])
			continue;
		if (redirty)
			redirty_page_for_writepage(wbc, cc->rpages[i]);
		f2fs_put_page(cc->rpages[i], unlock);
	}
}

struct page *f2fs_compress_control_page(struct page *page)
{
	return ((struct compress_io_ctx *)page_private(page))->rpages[0];
}

int f2fs_init_compress_ctx(struct compress_ctx *cc)
{
	if (cc->rpages)
		return 0;

	cc->rpages = page_array_alloc(cc->inode, cc->cluster_size);
	return cc->rpages ? 0 : -ENOMEM;
}

void f2fs_destroy_compress_ctx(struct compress_ctx *cc, bool reuse)
{
	page_array_free(cc->inode, cc->rpages, cc->cluster_size);
	cc->rpages = NULL;
	cc->nr_rpages = 0;
	cc->nr_cpages = 0;
	cc->valid_nr_cpages = 0;
	if (!reuse)
		cc->cluster_idx = NULL_CLUSTER;
}

void f2fs_compress_ctx_add_page(struct compress_ctx *cc, struct page *page)
{
	unsigned int cluster_ofs;

	if (!f2fs_cluster_can_merge_page(cc, page->index))
		f2fs_bug_on(F2FS_I_SB(cc->inode), 1);

	cluster_ofs = offset_in_cluster(cc, page->index);
	cc->rpages[cluster_ofs] = page;
	cc->nr_rpages++;
	cc->cluster_idx = cluster_idx(cc, page->index);
}

#ifdef CONFIG_F2FS_FS_LZO
static int lzo_init_compress_ctx(struct compress_ctx *cc)
{
	cc->private = f2fs_kvmalloc(F2FS_I_SB(cc->inode),
				LZO1X_MEM_COMPRESS, GFP_NOFS);
	if (!cc->private)
		return -ENOMEM;

	cc->clen = lzo1x_worst_compress(PAGE_SIZE << cc->log_cluster_size);
	return 0;
}

static void lzo_destroy_compress_ctx(struct compress_ctx *cc)
{
	kvfree(cc->private);
	cc->private = NULL;
}

static int lzo_compress_pages(struct compress_ctx *cc)
{
	int ret;

	ret = lzo1x_1_compress(cc->rbuf, cc->rlen, cc->cbuf->cdata,
					&cc->clen, cc->private);
	if (ret != LZO_E_OK) {
		printk_ratelimited("%sF2FS-fs (%s): lzo compress failed, ret:%d\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id, ret);
		return -EIO;
	}
	return 0;
}

static int lzo_decompress_pages(struct decompress_io_ctx *dic)
{
	int ret;

	ret = lzo1x_decompress_safe(dic->cbuf->cdata, dic->clen,
						dic->rbuf, &dic->rlen);
	if (ret != LZO_E_OK) {
		printk_ratelimited("%sF2FS-fs (%s): lzo decompress failed, ret:%d\n",
				KERN_ERR, F2FS_I_SB(dic->inode)->sb->s_id, ret);
		return -EIO;
	}

	if (dic->rlen != PAGE_SIZE << dic->log_cluster_size) {
		printk_ratelimited("%sF2FS-fs (%s): lzo invalid rlen:%zu, "
					"expected:%lu\n", KERN_ERR,
					F2FS_I_SB(dic->inode)->sb->s_id,
					dic->rlen,
					PAGE_SIZE << dic->log_cluster_size);
		return -EIO;
	}
	return 0;
}

static const struct f2fs_compress_ops f2fs_lzo_ops = {
	.init_compress_ctx	= lzo_init_compress_ctx,
	.destroy_compress_ctx	= lzo_destroy_compress_ctx,
	.compress_pages		= lzo_compress_pages,
	.decompress_pages	= lzo_decompress_pages,
};
#endif

#ifdef CONFIG_F2FS_FS_LZ4
static int lz4_init_compress_ctx(struct compress_ctx *cc)
{
	unsigned int size = LZ4_MEM_COMPRESS;

#ifdef CONFIG_F2FS_FS_LZ4HC
	if (F2FS_I(cc->inode)->i_compress_flag >> COMPRESS_LEVEL_OFFSET)
		size = LZ4HC_MEM_COMPRESS;
#endif

	cc->private = f2fs_kvmalloc(F2FS_I_SB(cc->inode), size, GFP_NOFS);
	if (!cc->private)
		return -ENOMEM;

	/*
	 * we do not change cc->clen to LZ4_compressBound(inputsize) to
	 * adapt worst compress case, because lz4 compressor can handle
	 * output budget properly.
	 */
	cc->clen = cc->rlen - PAGE_SIZE - COMPRESS_HEADER_SIZE;
	return 0;
}

static void lz4_destroy_compress_ctx(struct compress_ctx *cc)
{
	kvfree(cc->private);
	cc->private = NULL;
}

#ifdef CONFIG_F2FS_FS_LZ4HC
static int lz4hc_compress_pages(struct compress_ctx *cc)
{
	unsigned char level = F2FS_I(cc->inode)->i_compress_flag >>
						COMPRESS_LEVEL_OFFSET;
	int len;

	if (level)
		len = LZ4_compress_HC(cc->rbuf, cc->cbuf->cdata, cc->rlen,
					cc->clen, level, cc->private);
	else
		len = LZ4_compress_default(cc->rbuf, cc->cbuf->cdata, cc->rlen,
						cc->clen, cc->private);
	if (!len)
		return -EAGAIN;

	cc->clen = len;
	return 0;
}
#endif

static int lz4_compress_pages(struct compress_ctx *cc)
{
	int len;

#ifdef CONFIG_F2FS_FS_LZ4HC
	return lz4hc_compress_pages(cc);
#endif
	len = LZ4_compress_default(cc->rbuf, cc->cbuf->cdata, cc->rlen,
						cc->clen, cc->private);
	if (!len)
		return -EAGAIN;

	cc->clen = len;
	return 0;
}

static int lz4_decompress_pages(struct decompress_io_ctx *dic)
{
	int ret;

	ret = LZ4_decompress_safe(dic->cbuf->cdata, dic->rbuf,
						dic->clen, dic->rlen);
	if (ret < 0) {
		printk_ratelimited("%sF2FS-fs (%s): lz4 decompress failed, ret:%d\n",
				KERN_ERR, F2FS_I_SB(dic->inode)->sb->s_id, ret);
		return -EIO;
	}

	if (ret != PAGE_SIZE << dic->log_cluster_size) {
		printk_ratelimited("%sF2FS-fs (%s): lz4 invalid rlen:%zu, "
					"expected:%lu\n", KERN_ERR,
					F2FS_I_SB(dic->inode)->sb->s_id,
					dic->rlen,
					PAGE_SIZE << dic->log_cluster_size);
		return -EIO;
	}
	return 0;
}

static const struct f2fs_compress_ops f2fs_lz4_ops = {
	.init_compress_ctx	= lz4_init_compress_ctx,
	.destroy_compress_ctx	= lz4_destroy_compress_ctx,
	.compress_pages		= lz4_compress_pages,
	.decompress_pages	= lz4_decompress_pages,
};
#endif

#ifdef CONFIG_F2FS_FS_ZSTD
#define F2FS_ZSTD_DEFAULT_CLEVEL	1

static int zstd_init_compress_ctx(struct compress_ctx *cc)
{
	zstd_parameters params;
	zstd_cstream *stream;
	void *workspace;
	unsigned int workspace_size;
	unsigned char level = F2FS_I(cc->inode)->i_compress_flag >>
						COMPRESS_LEVEL_OFFSET;

	if (!level)
		level = F2FS_ZSTD_DEFAULT_CLEVEL;

	params = zstd_get_params(F2FS_ZSTD_DEFAULT_CLEVEL, cc->rlen);
	workspace_size = zstd_cstream_workspace_bound(&params.cParams);

	workspace = f2fs_kvmalloc(F2FS_I_SB(cc->inode),
					workspace_size, GFP_NOFS);
	if (!workspace)
		return -ENOMEM;

	stream = zstd_init_cstream(&params, 0, workspace, workspace_size);
	if (!stream) {
		printk_ratelimited("%sF2FS-fs (%s): %s zstd_init_cstream failed\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id,
				__func__);
		kvfree(workspace);
		return -EIO;
	}

	cc->private = workspace;
	cc->private2 = stream;

	cc->clen = cc->rlen - PAGE_SIZE - COMPRESS_HEADER_SIZE;
	return 0;
}

static void zstd_destroy_compress_ctx(struct compress_ctx *cc)
{
	kvfree(cc->private);
	cc->private = NULL;
	cc->private2 = NULL;
}

static int zstd_compress_pages(struct compress_ctx *cc)
{
	zstd_cstream *stream = cc->private2;
	zstd_in_buffer inbuf;
	zstd_out_buffer outbuf;
	int src_size = cc->rlen;
	int dst_size = src_size - PAGE_SIZE - COMPRESS_HEADER_SIZE;
	int ret;

	inbuf.pos = 0;
	inbuf.src = cc->rbuf;
	inbuf.size = src_size;

	outbuf.pos = 0;
	outbuf.dst = cc->cbuf->cdata;
	outbuf.size = dst_size;

	ret = zstd_compress_stream(stream, &outbuf, &inbuf);
	if (zstd_is_error(ret)) {
		printk_ratelimited("%sF2FS-fs (%s): %s zstd_compress_stream failed, ret: %d\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id,
				__func__, zstd_get_error_code(ret));
		return -EIO;
	}

	ret = zstd_end_stream(stream, &outbuf);
	if (zstd_is_error(ret)) {
		printk_ratelimited("%sF2FS-fs (%s): %s zstd_end_stream returned %d\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id,
				__func__, zstd_get_error_code(ret));
		return -EIO;
	}

	/*
	 * there is compressed data remained in intermediate buffer due to
	 * no more space in cbuf.cdata
	 */
	if (ret)
		return -EAGAIN;

	cc->clen = outbuf.pos;
	return 0;
}

static int zstd_init_decompress_ctx(struct decompress_io_ctx *dic)
{
	zstd_dstream *stream;
	void *workspace;
	unsigned int workspace_size;
	unsigned int max_window_size =
			MAX_COMPRESS_WINDOW_SIZE(dic->log_cluster_size);

	workspace_size = zstd_dstream_workspace_bound(max_window_size);

	workspace = f2fs_kvmalloc(F2FS_I_SB(dic->inode),
					workspace_size, GFP_NOFS);
	if (!workspace)
		return -ENOMEM;

	stream = zstd_init_dstream(max_window_size, workspace, workspace_size);
	if (!stream) {
		printk_ratelimited("%sF2FS-fs (%s): %s zstd_init_dstream failed\n",
				KERN_ERR, F2FS_I_SB(dic->inode)->sb->s_id,
				__func__);
		kvfree(workspace);
		return -EIO;
	}

	dic->private = workspace;
	dic->private2 = stream;

	return 0;
}

static void zstd_destroy_decompress_ctx(struct decompress_io_ctx *dic)
{
	kvfree(dic->private);
	dic->private = NULL;
	dic->private2 = NULL;
}

static int zstd_decompress_pages(struct decompress_io_ctx *dic)
{
	zstd_dstream *stream = dic->private2;
	zstd_in_buffer inbuf;
	zstd_out_buffer outbuf;
	int ret;

	inbuf.pos = 0;
	inbuf.src = dic->cbuf->cdata;
	inbuf.size = dic->clen;

	outbuf.pos = 0;
	outbuf.dst = dic->rbuf;
	outbuf.size = dic->rlen;

	ret = zstd_decompress_stream(stream, &outbuf, &inbuf);
	if (zstd_is_error(ret)) {
		printk_ratelimited("%sF2FS-fs (%s): %s zstd_decompress_stream failed, ret: %d\n",
				KERN_ERR, F2FS_I_SB(dic->inode)->sb->s_id,
				__func__, zstd_get_error_code(ret));
		return -EIO;
	}

	if (dic->rlen != outbuf.pos) {
		printk_ratelimited("%sF2FS-fs (%s): %s ZSTD invalid rlen:%zu, "
				"expected:%lu\n", KERN_ERR,
				F2FS_I_SB(dic->inode)->sb->s_id,
				__func__, dic->rlen,
				PAGE_SIZE << dic->log_cluster_size);
		return -EIO;
	}

	return 0;
}

static const struct f2fs_compress_ops f2fs_zstd_ops = {
	.init_compress_ctx	= zstd_init_compress_ctx,
	.destroy_compress_ctx	= zstd_destroy_compress_ctx,
	.compress_pages		= zstd_compress_pages,
	.init_decompress_ctx	= zstd_init_decompress_ctx,
	.destroy_decompress_ctx	= zstd_destroy_decompress_ctx,
	.decompress_pages	= zstd_decompress_pages,
};
#endif

#ifdef CONFIG_F2FS_FS_LZO
#ifdef CONFIG_F2FS_FS_LZORLE
static int lzorle_compress_pages(struct compress_ctx *cc)
{
	int ret;

	ret = lzorle1x_1_compress(cc->rbuf, cc->rlen, cc->cbuf->cdata,
					&cc->clen, cc->private);
	if (ret != LZO_E_OK) {
		printk_ratelimited("%sF2FS-fs (%s): lzo-rle compress failed, ret:%d\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id, ret);
		return -EIO;
	}
	return 0;
}

static const struct f2fs_compress_ops f2fs_lzorle_ops = {
	.init_compress_ctx	= lzo_init_compress_ctx,
	.destroy_compress_ctx	= lzo_destroy_compress_ctx,
	.compress_pages		= lzorle_compress_pages,
	.decompress_pages	= lzo_decompress_pages,
};
#endif
#endif

static const struct f2fs_compress_ops *f2fs_cops[COMPRESS_MAX] = {
#ifdef CONFIG_F2FS_FS_LZO
	&f2fs_lzo_ops,
#else
	NULL,
#endif
#ifdef CONFIG_F2FS_FS_LZ4
	&f2fs_lz4_ops,
#else
	NULL,
#endif
#ifdef CONFIG_F2FS_FS_ZSTD
	&f2fs_zstd_ops,
#else
	NULL,
#endif
#if defined(CONFIG_F2FS_FS_LZO) && defined(CONFIG_F2FS_FS_LZORLE)
	&f2fs_lzorle_ops,
#else
	NULL,
#endif
};

bool f2fs_is_compress_backend_ready(struct inode *inode)
{
	if (!f2fs_compressed_file(inode))
		return true;
	return f2fs_cops[F2FS_I(inode)->i_compress_algorithm];
}

static mempool_t *compress_page_pool;
static int num_compress_pages = 512;
module_param(num_compress_pages, uint, 0444);
MODULE_PARM_DESC(num_compress_pages,
		"Number of intermediate compress pages to preallocate");

int f2fs_init_compress_mempool(void)
{
	compress_page_pool = mempool_create_page_pool(num_compress_pages, 0);
	if (!compress_page_pool)
		return -ENOMEM;

	return 0;
}

void f2fs_destroy_compress_mempool(void)
{
	mempool_destroy(compress_page_pool);
}

static struct page *f2fs_compress_alloc_page(void)
{
	struct page *page;

	page = mempool_alloc(compress_page_pool, GFP_NOFS);
	lock_page(page);

	return page;
}

static void f2fs_compress_free_page(struct page *page)
{
	if (!page)
		return;
	detach_page_private(page);
	page->mapping = NULL;
	unlock_page(page);
	mempool_free(page, compress_page_pool);
}

#define MAX_VMAP_RETRIES	3

static void *f2fs_vmap(struct page **pages, unsigned int count)
{
	int i;
	void *buf = NULL;

	for (i = 0; i < MAX_VMAP_RETRIES; i++) {
		buf = vm_map_ram(pages, count, -1);
		if (buf)
			break;
		vm_unmap_aliases();
	}
	return buf;
}

static int f2fs_compress_pages(struct compress_ctx *cc)
{
	struct f2fs_inode_info *fi = F2FS_I(cc->inode);
	const struct f2fs_compress_ops *cops =
				f2fs_cops[fi->i_compress_algorithm];
	unsigned int max_len, new_nr_cpages;
	u32 chksum = 0;
	int i, ret;

	trace_f2fs_compress_pages_start(cc->inode, cc->cluster_idx,
				cc->cluster_size, fi->i_compress_algorithm);

	if (cops->init_compress_ctx) {
		ret = cops->init_compress_ctx(cc);
		if (ret)
			goto out;
	}

	max_len = COMPRESS_HEADER_SIZE + cc->clen;
	cc->nr_cpages = DIV_ROUND_UP(max_len, PAGE_SIZE);
	cc->valid_nr_cpages = cc->nr_cpages;

	cc->cpages = page_array_alloc(cc->inode, cc->nr_cpages);
	if (!cc->cpages) {
		ret = -ENOMEM;
		goto destroy_compress_ctx;
	}

	for (i = 0; i < cc->nr_cpages; i++) {
		cc->cpages[i] = f2fs_compress_alloc_page();
		if (!cc->cpages[i]) {
			ret = -ENOMEM;
			goto out_free_cpages;
		}
	}

	cc->rbuf = f2fs_vmap(cc->rpages, cc->cluster_size);
	if (!cc->rbuf) {
		ret = -ENOMEM;
		goto out_free_cpages;
	}

	cc->cbuf = f2fs_vmap(cc->cpages, cc->nr_cpages);
	if (!cc->cbuf) {
		ret = -ENOMEM;
		goto out_vunmap_rbuf;
	}

	ret = cops->compress_pages(cc);
	if (ret)
		goto out_vunmap_cbuf;

	max_len = PAGE_SIZE * (cc->cluster_size - 1) - COMPRESS_HEADER_SIZE;

	if (cc->clen > max_len) {
		ret = -EAGAIN;
		goto out_vunmap_cbuf;
	}

	cc->cbuf->clen = cpu_to_le32(cc->clen);

	if (fi->i_compress_flag & 1 << COMPRESS_CHKSUM)
		chksum = f2fs_crc32(F2FS_I_SB(cc->inode),
					cc->cbuf->cdata, cc->clen);
	cc->cbuf->chksum = cpu_to_le32(chksum);

	for (i = 0; i < COMPRESS_DATA_RESERVED_SIZE; i++)
		cc->cbuf->reserved[i] = cpu_to_le32(0);

	new_nr_cpages = DIV_ROUND_UP(cc->clen + COMPRESS_HEADER_SIZE, PAGE_SIZE);

	/* zero out any unused part of the last page */
	memset(&cc->cbuf->cdata[cc->clen], 0,
			(new_nr_cpages * PAGE_SIZE) -
			(cc->clen + COMPRESS_HEADER_SIZE));

	vm_unmap_ram(cc->cbuf, cc->nr_cpages);
	vm_unmap_ram(cc->rbuf, cc->cluster_size);

	for (i = 0; i < cc->nr_cpages; i++) {
		if (i < new_nr_cpages)
			continue;
		f2fs_compress_free_page(cc->cpages[i]);
		cc->cpages[i] = NULL;
	}

	if (cops->destroy_compress_ctx)
		cops->destroy_compress_ctx(cc);

	cc->valid_nr_cpages = new_nr_cpages;

	trace_f2fs_compress_pages_end(cc->inode, cc->cluster_idx,
							cc->clen, ret);
	return 0;

out_vunmap_cbuf:
	vm_unmap_ram(cc->cbuf, cc->nr_cpages);
out_vunmap_rbuf:
	vm_unmap_ram(cc->rbuf, cc->cluster_size);
out_free_cpages:
	for (i = 0; i < cc->nr_cpages; i++) {
		if (cc->cpages[i])
			f2fs_compress_free_page(cc->cpages[i]);
	}
	page_array_free(cc->inode, cc->cpages, cc->nr_cpages);
	cc->cpages = NULL;
destroy_compress_ctx:
	if (cops->destroy_compress_ctx)
		cops->destroy_compress_ctx(cc);
out:
	trace_f2fs_compress_pages_end(cc->inode, cc->cluster_idx,
							cc->clen, ret);
	return ret;
}

void f2fs_decompress_cluster(struct decompress_io_ctx *dic)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dic->inode);
	struct f2fs_inode_info *fi = F2FS_I(dic->inode);
	const struct f2fs_compress_ops *cops =
			f2fs_cops[fi->i_compress_algorithm];
	int ret;
	int i;

	trace_f2fs_decompress_pages_start(dic->inode, dic->cluster_idx,
				dic->cluster_size, fi->i_compress_algorithm);

	if (dic->failed) {
		ret = -EIO;
		goto out_end_io;
	}

	dic->tpages = page_array_alloc(dic->inode, dic->cluster_size);
	if (!dic->tpages) {
		ret = -ENOMEM;
		goto out_end_io;
	}

	for (i = 0; i < dic->cluster_size; i++) {
		if (dic->rpages[i]) {
			dic->tpages[i] = dic->rpages[i];
			continue;
		}

		dic->tpages[i] = f2fs_compress_alloc_page();
		if (!dic->tpages[i]) {
			ret = -ENOMEM;
			goto out_end_io;
		}
	}

	if (cops->init_decompress_ctx) {
		ret = cops->init_decompress_ctx(dic);
		if (ret)
			goto out_end_io;
	}

	dic->rbuf = f2fs_vmap(dic->tpages, dic->cluster_size);
	if (!dic->rbuf) {
		ret = -ENOMEM;
		goto out_destroy_decompress_ctx;
	}

	dic->cbuf = f2fs_vmap(dic->cpages, dic->nr_cpages);
	if (!dic->cbuf) {
		ret = -ENOMEM;
		goto out_vunmap_rbuf;
	}

	dic->clen = le32_to_cpu(dic->cbuf->clen);
	dic->rlen = PAGE_SIZE << dic->log_cluster_size;

	if (dic->clen > PAGE_SIZE * dic->nr_cpages - COMPRESS_HEADER_SIZE) {
		ret = -EFSCORRUPTED;
		goto out_vunmap_cbuf;
	}

	ret = cops->decompress_pages(dic);

	if (!ret && (fi->i_compress_flag & 1 << COMPRESS_CHKSUM)) {
		u32 provided = le32_to_cpu(dic->cbuf->chksum);
		u32 calculated = f2fs_crc32(sbi, dic->cbuf->cdata, dic->clen);

		if (provided != calculated) {
			if (!is_inode_flag_set(dic->inode, FI_COMPRESS_CORRUPT)) {
				set_inode_flag(dic->inode, FI_COMPRESS_CORRUPT);
				printk_ratelimited(
					"%sF2FS-fs (%s): checksum invalid, nid = %lu, %x vs %x",
					KERN_INFO, sbi->sb->s_id, dic->inode->i_ino,
					provided, calculated);
			}
			set_sbi_flag(sbi, SBI_NEED_FSCK);
		}
	}

out_vunmap_cbuf:
	vm_unmap_ram(dic->cbuf, dic->nr_cpages);
out_vunmap_rbuf:
	vm_unmap_ram(dic->rbuf, dic->cluster_size);
out_destroy_decompress_ctx:
	if (cops->destroy_decompress_ctx)
		cops->destroy_decompress_ctx(dic);
out_end_io:
	trace_f2fs_decompress_pages_end(dic->inode, dic->cluster_idx,
							dic->clen, ret);
	f2fs_decompress_end_io(dic, ret);
}

/*
 * This is called when a page of a compressed cluster has been read from disk
 * (or failed to be read from disk).  It checks whether this page was the last
 * page being waited on in the cluster, and if so, it decompresses the cluster
 * (or in the case of a failure, cleans up without actually decompressing).
 */
void f2fs_end_read_compressed_page(struct page *page, bool failed,
						block_t blkaddr)
{
	struct decompress_io_ctx *dic =
			(struct decompress_io_ctx *)page_private(page);
	struct f2fs_sb_info *sbi = F2FS_I_SB(dic->inode);

	dec_page_count(sbi, F2FS_RD_DATA);

	if (failed)
		WRITE_ONCE(dic->failed, true);
	else if (blkaddr)
		f2fs_cache_compressed_page(sbi, page,
					dic->inode->i_ino, blkaddr);

	if (atomic_dec_and_test(&dic->remaining_pages))
		f2fs_decompress_cluster(dic);
}

static bool is_page_in_cluster(struct compress_ctx *cc, pgoff_t index)
{
	if (cc->cluster_idx == NULL_CLUSTER)
		return true;
	return cc->cluster_idx == cluster_idx(cc, index);
}

bool f2fs_cluster_is_empty(struct compress_ctx *cc)
{
	return cc->nr_rpages == 0;
}

static bool f2fs_cluster_is_full(struct compress_ctx *cc)
{
	return cc->cluster_size == cc->nr_rpages;
}

bool f2fs_cluster_can_merge_page(struct compress_ctx *cc, pgoff_t index)
{
	if (f2fs_cluster_is_empty(cc))
		return true;
	return is_page_in_cluster(cc, index);
}

bool f2fs_all_cluster_page_loaded(struct compress_ctx *cc, struct pagevec *pvec,
				int index, int nr_pages)
{
	unsigned long pgidx;
	int i;

	if (nr_pages - index < cc->cluster_size)
		return false;

	pgidx = pvec->pages[index]->index;

	for (i = 1; i < cc->cluster_size; i++) {
		if (pvec->pages[index + i]->index != pgidx + i)
			return false;
	}

	return true;
}

static bool cluster_has_invalid_data(struct compress_ctx *cc)
{
	loff_t i_size = i_size_read(cc->inode);
	unsigned nr_pages = DIV_ROUND_UP(i_size, PAGE_SIZE);
	int i;

	for (i = 0; i < cc->cluster_size; i++) {
		struct page *page = cc->rpages[i];

		f2fs_bug_on(F2FS_I_SB(cc->inode), !page);

		/* beyond EOF */
		if (page->index >= nr_pages)
			return true;
	}
	return false;
}

bool f2fs_sanity_check_cluster(struct dnode_of_data *dn)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	unsigned int cluster_size = F2FS_I(dn->inode)->i_cluster_size;
	bool compressed = dn->data_blkaddr == COMPRESS_ADDR;
	int cluster_end = 0;
	int i;
	char *reason = "";

	if (!compressed)
		return false;

	/* [..., COMPR_ADDR, ...] */
	if (dn->ofs_in_node % cluster_size) {
		reason = "[*|C|*|*]";
		goto out;
	}

	for (i = 1; i < cluster_size; i++) {
		block_t blkaddr = data_blkaddr(dn->inode, dn->node_page,
							dn->ofs_in_node + i);

		/* [COMPR_ADDR, ..., COMPR_ADDR] */
		if (blkaddr == COMPRESS_ADDR) {
			reason = "[C|*|C|*]";
			goto out;
		}
		if (compressed) {
			if (!__is_valid_data_blkaddr(blkaddr)) {
				if (!cluster_end)
					cluster_end = i;
				continue;
			}
			/* [COMPR_ADDR, NULL_ADDR or NEW_ADDR, valid_blkaddr] */
			if (cluster_end) {
				reason = "[C|N|N|V]";
				goto out;
			}
		}
	}
	return false;
out:
	f2fs_warn(sbi, "access invalid cluster, ino:%lu, nid:%u, ofs_in_node:%u, reason:%s",
			dn->inode->i_ino, dn->nid, dn->ofs_in_node, reason);
	set_sbi_flag(sbi, SBI_NEED_FSCK);
	return true;
}

static int __f2fs_cluster_blocks(struct inode *inode,
				unsigned int cluster_idx, bool compr)
{
	struct dnode_of_data dn;
	unsigned int cluster_size = F2FS_I(inode)->i_cluster_size;
	unsigned int start_idx = cluster_idx <<
				F2FS_I(inode)->i_log_cluster_size;
	int ret;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	ret = f2fs_get_dnode_of_data(&dn, start_idx, LOOKUP_NODE);
	if (ret) {
		if (ret == -ENOENT)
			ret = 0;
		goto fail;
	}

	if (f2fs_sanity_check_cluster(&dn)) {
		ret = -EFSCORRUPTED;
		goto fail;
	}

	if (dn.data_blkaddr == COMPRESS_ADDR) {
		int i;

		ret = 1;
		for (i = 1; i < cluster_size; i++) {
			block_t blkaddr;

			blkaddr = data_blkaddr(dn.inode,
					dn.node_page, dn.ofs_in_node + i);
			if (compr) {
				if (__is_valid_data_blkaddr(blkaddr))
					ret++;
			} else {
				if (blkaddr != NULL_ADDR)
					ret++;
			}
		}

		f2fs_bug_on(F2FS_I_SB(inode),
			!compr && ret != cluster_size &&
			!is_inode_flag_set(inode, FI_COMPRESS_RELEASED));
	}
fail:
	f2fs_put_dnode(&dn);
	return ret;
}

/* return # of compressed blocks in compressed cluster */
static int f2fs_compressed_blocks(struct compress_ctx *cc)
{
	return __f2fs_cluster_blocks(cc->inode, cc->cluster_idx, true);
}

/* return # of valid blocks in compressed cluster */
int f2fs_is_compressed_cluster(struct inode *inode, pgoff_t index)
{
	return __f2fs_cluster_blocks(inode,
		index >> F2FS_I(inode)->i_log_cluster_size,
		false);
}

static bool cluster_may_compress(struct compress_ctx *cc)
{
	if (!f2fs_need_compress_data(cc->inode))
		return false;
	if (f2fs_is_atomic_file(cc->inode))
		return false;
	if (!f2fs_cluster_is_full(cc))
		return false;
	if (unlikely(f2fs_cp_error(F2FS_I_SB(cc->inode))))
		return false;
	return !cluster_has_invalid_data(cc);
}

static void set_cluster_writeback(struct compress_ctx *cc)
{
	int i;

	for (i = 0; i < cc->cluster_size; i++) {
		if (cc->rpages[i])
			set_page_writeback(cc->rpages[i]);
	}
}

static void set_cluster_dirty(struct compress_ctx *cc)
{
	int i;

	for (i = 0; i < cc->cluster_size; i++)
		if (cc->rpages[i])
			set_page_dirty(cc->rpages[i]);
}

static int prepare_compress_overwrite(struct compress_ctx *cc,
		struct page **pagep, pgoff_t index, void **fsdata)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(cc->inode);
	struct address_space *mapping = cc->inode->i_mapping;
	struct page *page;
	sector_t last_block_in_bio;
	unsigned fgp_flag = FGP_LOCK | FGP_WRITE | FGP_CREAT;
	pgoff_t start_idx = start_idx_of_cluster(cc);
	int i, ret;

retry:
	ret = f2fs_is_compressed_cluster(cc->inode, start_idx);
	if (ret <= 0)
		return ret;

	ret = f2fs_init_compress_ctx(cc);
	if (ret)
		return ret;

	/* keep page reference to avoid page reclaim */
	for (i = 0; i < cc->cluster_size; i++) {
		page = f2fs_pagecache_get_page(mapping, start_idx + i,
							fgp_flag, GFP_NOFS);
		if (!page) {
			ret = -ENOMEM;
			goto unlock_pages;
		}

		if (PageUptodate(page))
			f2fs_put_page(page, 1);
		else
			f2fs_compress_ctx_add_page(cc, page);
	}

	if (!f2fs_cluster_is_empty(cc)) {
		struct bio *bio = NULL;

		ret = f2fs_read_multi_pages(cc, &bio, cc->cluster_size,
					&last_block_in_bio, false, true);
		f2fs_put_rpages(cc);
		f2fs_destroy_compress_ctx(cc, true);
		if (ret)
			goto out;
		if (bio)
			f2fs_submit_bio(sbi, bio, DATA);

		ret = f2fs_init_compress_ctx(cc);
		if (ret)
			goto out;
	}

	for (i = 0; i < cc->cluster_size; i++) {
		f2fs_bug_on(sbi, cc->rpages[i]);

		page = find_lock_page(mapping, start_idx + i);
		if (!page) {
			/* page can be truncated */
			goto release_and_retry;
		}

		f2fs_wait_on_page_writeback(page, DATA, true, true);
		f2fs_compress_ctx_add_page(cc, page);

		if (!PageUptodate(page)) {
release_and_retry:
			f2fs_put_rpages(cc);
			f2fs_unlock_rpages(cc, i + 1);
			f2fs_destroy_compress_ctx(cc, true);
			goto retry;
		}
	}

	if (likely(!ret)) {
		*fsdata = cc->rpages;
		*pagep = cc->rpages[offset_in_cluster(cc, index)];
		return cc->cluster_size;
	}

unlock_pages:
	f2fs_put_rpages(cc);
	f2fs_unlock_rpages(cc, i);
	f2fs_destroy_compress_ctx(cc, true);
out:
	return ret;
}

int f2fs_prepare_compress_overwrite(struct inode *inode,
		struct page **pagep, pgoff_t index, void **fsdata)
{
	struct compress_ctx cc = {
		.inode = inode,
		.log_cluster_size = F2FS_I(inode)->i_log_cluster_size,
		.cluster_size = F2FS_I(inode)->i_cluster_size,
		.cluster_idx = index >> F2FS_I(inode)->i_log_cluster_size,
		.rpages = NULL,
		.nr_rpages = 0,
	};

	return prepare_compress_overwrite(&cc, pagep, index, fsdata);
}

bool f2fs_compress_write_end(struct inode *inode, void *fsdata,
					pgoff_t index, unsigned copied)

{
	struct compress_ctx cc = {
		.inode = inode,
		.log_cluster_size = F2FS_I(inode)->i_log_cluster_size,
		.cluster_size = F2FS_I(inode)->i_cluster_size,
		.rpages = fsdata,
	};
	bool first_index = (index == cc.rpages[0]->index);

	if (copied)
		set_cluster_dirty(&cc);

	f2fs_put_rpages_wbc(&cc, NULL, false, 1);
	f2fs_destroy_compress_ctx(&cc, false);

	return first_index;
}

int f2fs_truncate_partial_cluster(struct inode *inode, u64 from, bool lock)
{
	void *fsdata = NULL;
	struct page *pagep;
	int log_cluster_size = F2FS_I(inode)->i_log_cluster_size;
	pgoff_t start_idx = from >> (PAGE_SHIFT + log_cluster_size) <<
							log_cluster_size;
	int err;

	err = f2fs_is_compressed_cluster(inode, start_idx);
	if (err < 0)
		return err;

	/* truncate normal cluster */
	if (!err)
		return f2fs_do_truncate_blocks(inode, from, lock);

	/* truncate compressed cluster */
	err = f2fs_prepare_compress_overwrite(inode, &pagep,
						start_idx, &fsdata);

	/* should not be a normal cluster */
	f2fs_bug_on(F2FS_I_SB(inode), err == 0);

	if (err <= 0)
		return err;

	if (err > 0) {
		struct page **rpages = fsdata;
		int cluster_size = F2FS_I(inode)->i_cluster_size;
		int i;

		for (i = cluster_size - 1; i >= 0; i--) {
			loff_t start = rpages[i]->index << PAGE_SHIFT;

			if (from <= start) {
				zero_user_segment(rpages[i], 0, PAGE_SIZE);
			} else {
				zero_user_segment(rpages[i], from - start,
								PAGE_SIZE);
				break;
			}
		}

		f2fs_compress_write_end(inode, fsdata, start_idx, true);
	}
	return 0;
}

static int f2fs_write_compressed_pages(struct compress_ctx *cc,
					int *submitted,
					struct writeback_control *wbc,
					enum iostat_type io_type)
{
	struct inode *inode = cc->inode;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_io_info fio = {
		.sbi = sbi,
		.ino = cc->inode->i_ino,
		.type = DATA,
		.op = REQ_OP_WRITE,
		.op_flags = wbc_to_write_flags(wbc),
		.old_blkaddr = NEW_ADDR,
		.page = NULL,
		.encrypted_page = NULL,
		.compressed_page = NULL,
		.submitted = false,
		.io_type = io_type,
		.io_wbc = wbc,
		.encrypted = fscrypt_inode_uses_fs_layer_crypto(cc->inode),
	};
	struct dnode_of_data dn;
	struct node_info ni;
	struct compress_io_ctx *cic;
	pgoff_t start_idx = start_idx_of_cluster(cc);
	unsigned int last_index = cc->cluster_size - 1;
	loff_t psize;
	int i, err;

	/* we should bypass data pages to proceed the kworkder jobs */
	if (unlikely(f2fs_cp_error(sbi))) {
		mapping_set_error(cc->rpages[0]->mapping, -EIO);
		goto out_free;
	}

	if (IS_NOQUOTA(inode)) {
		/*
		 * We need to wait for node_write to avoid block allocation during
		 * checkpoint. This can only happen to quota writes which can cause
		 * the below discard race condition.
		 */
		down_read(&sbi->node_write);
	} else if (!f2fs_trylock_op(sbi)) {
		goto out_free;
	}

	set_new_dnode(&dn, cc->inode, NULL, NULL, 0);

	err = f2fs_get_dnode_of_data(&dn, start_idx, LOOKUP_NODE);
	if (err)
		goto out_unlock_op;

	for (i = 0; i < cc->cluster_size; i++) {
		if (data_blkaddr(dn.inode, dn.node_page,
					dn.ofs_in_node + i) == NULL_ADDR)
			goto out_put_dnode;
	}

	psize = (loff_t)(cc->rpages[last_index]->index + 1) << PAGE_SHIFT;

	err = f2fs_get_node_info(fio.sbi, dn.nid, &ni, false);
	if (err)
		goto out_put_dnode;

	fio.version = ni.version;

	cic = f2fs_kmem_cache_alloc(cic_entry_slab, GFP_F2FS_ZERO, false, sbi);
	if (!cic)
		goto out_put_dnode;

	cic->magic = F2FS_COMPRESSED_PAGE_MAGIC;
	cic->inode = inode;
	atomic_set(&cic->pending_pages, cc->valid_nr_cpages);
	cic->rpages = page_array_alloc(cc->inode, cc->cluster_size);
	if (!cic->rpages)
		goto out_put_cic;

	cic->nr_rpages = cc->cluster_size;

	for (i = 0; i < cc->valid_nr_cpages; i++) {
		f2fs_set_compressed_page(cc->cpages[i], inode,
					cc->rpages[i + 1]->index, cic);
		fio.compressed_page = cc->cpages[i];

		fio.old_blkaddr = data_blkaddr(dn.inode, dn.node_page,
						dn.ofs_in_node + i + 1);

		/* wait for GCed page writeback via META_MAPPING */
		f2fs_wait_on_block_writeback(inode, fio.old_blkaddr);

		if (fio.encrypted) {
			fio.page = cc->rpages[i + 1];
			err = f2fs_encrypt_one_page(&fio);
			if (err)
				goto out_destroy_crypt;
			cc->cpages[i] = fio.encrypted_page;
		}
	}

	set_cluster_writeback(cc);

	for (i = 0; i < cc->cluster_size; i++)
		cic->rpages[i] = cc->rpages[i];

	for (i = 0; i < cc->cluster_size; i++, dn.ofs_in_node++) {
		block_t blkaddr;

		blkaddr = f2fs_data_blkaddr(&dn);
		fio.page = cc->rpages[i];
		fio.old_blkaddr = blkaddr;

		/* cluster header */
		if (i == 0) {
			if (blkaddr == COMPRESS_ADDR)
				fio.compr_blocks++;
			if (__is_valid_data_blkaddr(blkaddr))
				f2fs_invalidate_blocks(sbi, blkaddr);
			f2fs_update_data_blkaddr(&dn, COMPRESS_ADDR);
			goto unlock_continue;
		}

		if (fio.compr_blocks && __is_valid_data_blkaddr(blkaddr))
			fio.compr_blocks++;

		if (i > cc->valid_nr_cpages) {
			if (__is_valid_data_blkaddr(blkaddr)) {
				f2fs_invalidate_blocks(sbi, blkaddr);
				f2fs_update_data_blkaddr(&dn, NEW_ADDR);
			}
			goto unlock_continue;
		}

		f2fs_bug_on(fio.sbi, blkaddr == NULL_ADDR);

		if (fio.encrypted)
			fio.encrypted_page = cc->cpages[i - 1];
		else
			fio.compressed_page = cc->cpages[i - 1];

		cc->cpages[i - 1] = NULL;
		f2fs_outplace_write_data(&dn, &fio);
		(*submitted)++;
unlock_continue:
		inode_dec_dirty_pages(cc->inode);
		unlock_page(fio.page);
	}

	if (fio.compr_blocks)
		f2fs_i_compr_blocks_update(inode, fio.compr_blocks - 1, false);
	f2fs_i_compr_blocks_update(inode, cc->valid_nr_cpages, true);
	add_compr_block_stat(inode, cc->valid_nr_cpages);

	set_inode_flag(cc->inode, FI_APPEND_WRITE);
	if (cc->cluster_idx == 0)
		set_inode_flag(inode, FI_FIRST_BLOCK_WRITTEN);

	f2fs_put_dnode(&dn);
	if (IS_NOQUOTA(inode))
		up_read(&sbi->node_write);
	else
		f2fs_unlock_op(sbi);

	spin_lock(&fi->i_size_lock);
	if (fi->last_disk_size < psize)
		fi->last_disk_size = psize;
	spin_unlock(&fi->i_size_lock);

	f2fs_put_rpages(cc);
	page_array_free(cc->inode, cc->cpages, cc->nr_cpages);
	cc->cpages = NULL;
	f2fs_destroy_compress_ctx(cc, false);
	return 0;

out_destroy_crypt:
	page_array_free(cc->inode, cic->rpages, cc->cluster_size);

	for (--i; i >= 0; i--)
		fscrypt_finalize_bounce_page(&cc->cpages[i]);
out_put_cic:
	kmem_cache_free(cic_entry_slab, cic);
out_put_dnode:
	f2fs_put_dnode(&dn);
out_unlock_op:
	if (IS_NOQUOTA(inode))
		up_read(&sbi->node_write);
	else
		f2fs_unlock_op(sbi);
out_free:
	for (i = 0; i < cc->valid_nr_cpages; i++) {
		f2fs_compress_free_page(cc->cpages[i]);
		cc->cpages[i] = NULL;
	}
	page_array_free(cc->inode, cc->cpages, cc->nr_cpages);
	cc->cpages = NULL;
	return -EAGAIN;
}

void f2fs_compress_write_end_io(struct bio *bio, struct page *page)
{
	struct f2fs_sb_info *sbi = bio->bi_private;
	struct compress_io_ctx *cic =
			(struct compress_io_ctx *)page_private(page);
	int i;

	if (unlikely(bio->bi_status))
		mapping_set_error(cic->inode->i_mapping, -EIO);

	f2fs_compress_free_page(page);

	dec_page_count(sbi, F2FS_WB_DATA);

	if (atomic_dec_return(&cic->pending_pages))
		return;

	for (i = 0; i < cic->nr_rpages; i++) {
		WARN_ON(!cic->rpages[i]);
		clear_page_private_gcing(cic->rpages[i]);
		end_page_writeback(cic->rpages[i]);
	}

	page_array_free(cic->inode, cic->rpages, cic->nr_rpages);
	kmem_cache_free(cic_entry_slab, cic);
}

static int f2fs_write_raw_pages(struct compress_ctx *cc,
					int *submitted,
					struct writeback_control *wbc,
					enum iostat_type io_type)
{
	struct address_space *mapping = cc->inode->i_mapping;
	int _submitted, compr_blocks, ret, i;

	compr_blocks = f2fs_compressed_blocks(cc);

	for (i = 0; i < cc->cluster_size; i++) {
		if (!cc->rpages[i])
			continue;

		redirty_page_for_writepage(wbc, cc->rpages[i]);
		unlock_page(cc->rpages[i]);
	}

	if (compr_blocks < 0)
		return compr_blocks;

	for (i = 0; i < cc->cluster_size; i++) {
		if (!cc->rpages[i])
			continue;
retry_write:
		lock_page(cc->rpages[i]);

		if (cc->rpages[i]->mapping != mapping) {
continue_unlock:
			unlock_page(cc->rpages[i]);
			continue;
		}

		if (!PageDirty(cc->rpages[i]))
			goto continue_unlock;

		if (!clear_page_dirty_for_io(cc->rpages[i]))
			goto continue_unlock;

		ret = f2fs_write_single_data_page(cc->rpages[i], &_submitted,
						NULL, NULL, wbc, io_type,
						compr_blocks, false);
		if (ret) {
			if (ret == AOP_WRITEPAGE_ACTIVATE) {
				unlock_page(cc->rpages[i]);
				ret = 0;
			} else if (ret == -EAGAIN) {
				/*
				 * for quota file, just redirty left pages to
				 * avoid deadlock caused by cluster update race
				 * from foreground operation.
				 */
				if (IS_NOQUOTA(cc->inode))
					return 0;
				ret = 0;
				cond_resched();
				congestion_wait(BLK_RW_ASYNC,
						DEFAULT_IO_TIMEOUT);
				goto retry_write;
			}
			return ret;
		}

		*submitted += _submitted;
	}

	f2fs_balance_fs(F2FS_M_SB(mapping), true);

	return 0;
}

int f2fs_write_multi_pages(struct compress_ctx *cc,
					int *submitted,
					struct writeback_control *wbc,
					enum iostat_type io_type)
{
	int err;

	*submitted = 0;
	if (cluster_may_compress(cc)) {
		err = f2fs_compress_pages(cc);
		if (err == -EAGAIN) {
			add_compr_block_stat(cc->inode, cc->cluster_size);
			goto write;
		} else if (err) {
			f2fs_put_rpages_wbc(cc, wbc, true, 1);
			goto destroy_out;
		}

		err = f2fs_write_compressed_pages(cc, submitted,
							wbc, io_type);
		if (!err)
			return 0;
		f2fs_bug_on(F2FS_I_SB(cc->inode), err != -EAGAIN);
	}
write:
	f2fs_bug_on(F2FS_I_SB(cc->inode), *submitted);

	err = f2fs_write_raw_pages(cc, submitted, wbc, io_type);
	f2fs_put_rpages_wbc(cc, wbc, false, 0);
destroy_out:
	f2fs_destroy_compress_ctx(cc, false);
	return err;
}

static void f2fs_free_dic(struct decompress_io_ctx *dic);

struct decompress_io_ctx *f2fs_alloc_dic(struct compress_ctx *cc)
{
	struct decompress_io_ctx *dic;
	pgoff_t start_idx = start_idx_of_cluster(cc);
	int i;

	dic = f2fs_kmem_cache_alloc(dic_entry_slab, GFP_F2FS_ZERO,
					false, F2FS_I_SB(cc->inode));
	if (!dic)
		return ERR_PTR(-ENOMEM);

	dic->rpages = page_array_alloc(cc->inode, cc->cluster_size);
	if (!dic->rpages) {
		kmem_cache_free(dic_entry_slab, dic);
		return ERR_PTR(-ENOMEM);
	}

	dic->magic = F2FS_COMPRESSED_PAGE_MAGIC;
	dic->inode = cc->inode;
	atomic_set(&dic->remaining_pages, cc->nr_cpages);
	dic->cluster_idx = cc->cluster_idx;
	dic->cluster_size = cc->cluster_size;
	dic->log_cluster_size = cc->log_cluster_size;
	dic->nr_cpages = cc->nr_cpages;
	refcount_set(&dic->refcnt, 1);
	dic->failed = false;
	dic->need_verity = f2fs_need_verity(cc->inode, start_idx);

	for (i = 0; i < dic->cluster_size; i++)
		dic->rpages[i] = cc->rpages[i];
	dic->nr_rpages = cc->cluster_size;

	dic->cpages = page_array_alloc(dic->inode, dic->nr_cpages);
	if (!dic->cpages)
		goto out_free;

	for (i = 0; i < dic->nr_cpages; i++) {
		struct page *page;

		page = f2fs_compress_alloc_page();
		if (!page)
			goto out_free;

		f2fs_set_compressed_page(page, cc->inode,
					start_idx + i + 1, dic);
		dic->cpages[i] = page;
	}

	return dic;

out_free:
	f2fs_free_dic(dic);
	return ERR_PTR(-ENOMEM);
}

static void f2fs_free_dic(struct decompress_io_ctx *dic)
{
	int i;

	if (dic->tpages) {
		for (i = 0; i < dic->cluster_size; i++) {
			if (dic->rpages[i])
				continue;
			if (!dic->tpages[i])
				continue;
			f2fs_compress_free_page(dic->tpages[i]);
		}
		page_array_free(dic->inode, dic->tpages, dic->cluster_size);
	}

	if (dic->cpages) {
		for (i = 0; i < dic->nr_cpages; i++) {
			if (!dic->cpages[i])
				continue;
			f2fs_compress_free_page(dic->cpages[i]);
		}
		page_array_free(dic->inode, dic->cpages, dic->nr_cpages);
	}

	page_array_free(dic->inode, dic->rpages, dic->nr_rpages);
	kmem_cache_free(dic_entry_slab, dic);
}

static void f2fs_put_dic(struct decompress_io_ctx *dic)
{
	if (refcount_dec_and_test(&dic->refcnt))
		f2fs_free_dic(dic);
}

/*
 * Update and unlock the cluster's pagecache pages, and release the reference to
 * the decompress_io_ctx that was being held for I/O completion.
 */
static void __f2fs_decompress_end_io(struct decompress_io_ctx *dic, bool failed)
{
	int i;

	for (i = 0; i < dic->cluster_size; i++) {
		struct page *rpage = dic->rpages[i];

		if (!rpage)
			continue;

		/* PG_error was set if verity failed. */
		if (failed || PageError(rpage)) {
			ClearPageUptodate(rpage);
			/* will re-read again later */
			ClearPageError(rpage);
		} else {
			SetPageUptodate(rpage);
		}
		unlock_page(rpage);
	}

	f2fs_put_dic(dic);
}

static void f2fs_verify_cluster(struct work_struct *work)
{
	struct decompress_io_ctx *dic =
		container_of(work, struct decompress_io_ctx, verity_work);
	int i;

	/* Verify the cluster's decompressed pages with fs-verity. */
	for (i = 0; i < dic->cluster_size; i++) {
		struct page *rpage = dic->rpages[i];

		if (rpage && !fsverity_verify_page(rpage))
			SetPageError(rpage);
	}

	__f2fs_decompress_end_io(dic, false);
}

/*
 * This is called when a compressed cluster has been decompressed
 * (or failed to be read and/or decompressed).
 */
void f2fs_decompress_end_io(struct decompress_io_ctx *dic, bool failed)
{
	if (!failed && dic->need_verity) {
		/*
		 * Note that to avoid deadlocks, the verity work can't be done
		 * on the decompression workqueue.  This is because verifying
		 * the data pages can involve reading metadata pages from the
		 * file, and these metadata pages may be compressed.
		 */
		INIT_WORK(&dic->verity_work, f2fs_verify_cluster);
		fsverity_enqueue_verify_work(&dic->verity_work);
	} else {
		__f2fs_decompress_end_io(dic, failed);
	}
}

/*
 * Put a reference to a compressed page's decompress_io_ctx.
 *
 * This is called when the page is no longer needed and can be freed.
 */
void f2fs_put_page_dic(struct page *page)
{
	struct decompress_io_ctx *dic =
			(struct decompress_io_ctx *)page_private(page);

	f2fs_put_dic(dic);
}

/*
 * check whether cluster blocks are contiguous, and add extent cache entry
 * only if cluster blocks are logically and physically contiguous.
 */
unsigned int f2fs_cluster_blocks_are_contiguous(struct dnode_of_data *dn)
{
	bool compressed = f2fs_data_blkaddr(dn) == COMPRESS_ADDR;
	int i = compressed ? 1 : 0;
	block_t first_blkaddr = data_blkaddr(dn->inode, dn->node_page,
						dn->ofs_in_node + i);

	for (i += 1; i < F2FS_I(dn->inode)->i_cluster_size; i++) {
		block_t blkaddr = data_blkaddr(dn->inode, dn->node_page,
						dn->ofs_in_node + i);

		if (!__is_valid_data_blkaddr(blkaddr))
			break;
		if (first_blkaddr + i - (compressed ? 1 : 0) != blkaddr)
			return 0;
	}

	return compressed ? i - 1 : i;
}

const struct address_space_operations f2fs_compress_aops = {
	.releasepage = f2fs_release_page,
	.invalidatepage = f2fs_invalidate_page,
};

struct address_space *COMPRESS_MAPPING(struct f2fs_sb_info *sbi)
{
	return sbi->compress_inode->i_mapping;
}

void f2fs_invalidate_compress_page(struct f2fs_sb_info *sbi, block_t blkaddr)
{
	if (!sbi->compress_inode)
		return;
	invalidate_mapping_pages(COMPRESS_MAPPING(sbi), blkaddr, blkaddr);
}

void f2fs_cache_compressed_page(struct f2fs_sb_info *sbi, struct page *page,
						nid_t ino, block_t blkaddr)
{
	struct page *cpage;
	int ret;

	if (!test_opt(sbi, COMPRESS_CACHE))
		return;

	if (!f2fs_is_valid_blkaddr(sbi, blkaddr, DATA_GENERIC_ENHANCE_READ))
		return;

	if (!f2fs_available_free_memory(sbi, COMPRESS_PAGE))
		return;

	cpage = find_get_page(COMPRESS_MAPPING(sbi), blkaddr);
	if (cpage) {
		f2fs_put_page(cpage, 0);
		return;
	}

	cpage = alloc_page(__GFP_NOWARN | __GFP_IO);
	if (!cpage)
		return;

	ret = add_to_page_cache_lru(cpage, COMPRESS_MAPPING(sbi),
						blkaddr, GFP_NOFS);
	if (ret) {
		f2fs_put_page(cpage, 0);
		return;
	}

	set_page_private_data(cpage, ino);

	if (!f2fs_is_valid_blkaddr(sbi, blkaddr, DATA_GENERIC_ENHANCE_READ))
		goto out;

	memcpy(page_address(cpage), page_address(page), PAGE_SIZE);
	SetPageUptodate(cpage);
out:
	f2fs_put_page(cpage, 1);
}

bool f2fs_load_compressed_page(struct f2fs_sb_info *sbi, struct page *page,
								block_t blkaddr)
{
	struct page *cpage;
	bool hitted = false;

	if (!test_opt(sbi, COMPRESS_CACHE))
		return false;

	cpage = f2fs_pagecache_get_page(COMPRESS_MAPPING(sbi),
				blkaddr, FGP_LOCK | FGP_NOWAIT, GFP_NOFS);
	if (cpage) {
		if (PageUptodate(cpage)) {
			atomic_inc(&sbi->compress_page_hit);
			memcpy(page_address(page),
				page_address(cpage), PAGE_SIZE);
			hitted = true;
		}
		f2fs_put_page(cpage, 1);
	}

	return hitted;
}

void f2fs_invalidate_compress_pages(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct address_space *mapping = sbi->compress_inode->i_mapping;
	struct pagevec pvec;
	pgoff_t index = 0;
	pgoff_t end = MAX_BLKADDR(sbi);

	if (!mapping->nrpages)
		return;

	pagevec_init(&pvec);

	do {
		unsigned int nr_pages;
		int i;

		nr_pages = pagevec_lookup_range(&pvec, mapping,
						&index, end - 1);
		if (!nr_pages)
			break;

		for (i = 0; i < nr_pages; i++) {
			struct page *page = pvec.pages[i];

			if (page->index > end)
				break;

			lock_page(page);
			if (page->mapping != mapping) {
				unlock_page(page);
				continue;
			}

			if (ino != get_page_private_data(page)) {
				unlock_page(page);
				continue;
			}

			generic_error_remove_page(mapping, page);
			unlock_page(page);
		}
		pagevec_release(&pvec);
		cond_resched();
	} while (index < end);
}

int f2fs_init_compress_inode(struct f2fs_sb_info *sbi)
{
	struct inode *inode;

	if (!test_opt(sbi, COMPRESS_CACHE))
		return 0;

	inode = f2fs_iget(sbi->sb, F2FS_COMPRESS_INO(sbi));
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	sbi->compress_inode = inode;

	sbi->compress_percent = COMPRESS_PERCENT;
	sbi->compress_watermark = COMPRESS_WATERMARK;

	atomic_set(&sbi->compress_page_hit, 0);

	return 0;
}

void f2fs_destroy_compress_inode(struct f2fs_sb_info *sbi)
{
	if (!sbi->compress_inode)
		return;
	iput(sbi->compress_inode);
	sbi->compress_inode = NULL;
}

int f2fs_init_page_array_cache(struct f2fs_sb_info *sbi)
{
	dev_t dev = sbi->sb->s_bdev->bd_dev;
	char slab_name[32];

	sprintf(slab_name, "f2fs_page_array_entry-%u:%u", MAJOR(dev), MINOR(dev));

	sbi->page_array_slab_size = sizeof(struct page *) <<
					F2FS_OPTION(sbi).compress_log_size;

	sbi->page_array_slab = f2fs_kmem_cache_create(slab_name,
					sbi->page_array_slab_size);
	if (!sbi->page_array_slab)
		return -ENOMEM;
	return 0;
}

void f2fs_destroy_page_array_cache(struct f2fs_sb_info *sbi)
{
	kmem_cache_destroy(sbi->page_array_slab);
}

static int __init f2fs_init_cic_cache(void)
{
	cic_entry_slab = f2fs_kmem_cache_create("f2fs_cic_entry",
					sizeof(struct compress_io_ctx));
	if (!cic_entry_slab)
		return -ENOMEM;
	return 0;
}

static void f2fs_destroy_cic_cache(void)
{
	kmem_cache_destroy(cic_entry_slab);
}

static int __init f2fs_init_dic_cache(void)
{
	dic_entry_slab = f2fs_kmem_cache_create("f2fs_dic_entry",
					sizeof(struct decompress_io_ctx));
	if (!dic_entry_slab)
		return -ENOMEM;
	return 0;
}

static void f2fs_destroy_dic_cache(void)
{
	kmem_cache_destroy(dic_entry_slab);
}

int __init f2fs_init_compress_cache(void)
{
	int err;

	err = f2fs_init_cic_cache();
	if (err)
		goto out;
	err = f2fs_init_dic_cache();
	if (err)
		goto free_cic;
	return 0;
free_cic:
	f2fs_destroy_cic_cache();
out:
	return -ENOMEM;
}

void f2fs_destroy_compress_cache(void)
{
	f2fs_destroy_dic_cache();
	f2fs_destroy_cic_cache();
}
