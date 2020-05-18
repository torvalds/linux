// SPDX-License-Identifier: GPL-2.0
/*
 * f2fs compress support
 *
 * Copyright (c) 2019 Chao Yu <chao@kernel.org>
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/lzo.h>
#include <linux/lz4.h>
#include <linux/zstd.h>

#include "f2fs.h"
#include "node.h"
#include <trace/events/f2fs.h>

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
	if (IS_ATOMIC_WRITTEN_PAGE(page) || IS_DUMMY_WRITTEN_PAGE(page))
		return false;
	f2fs_bug_on(F2FS_M_SB(page->mapping),
		*((u32 *)page_private(page)) != F2FS_COMPRESSED_PAGE_MAGIC);
	return true;
}

static void f2fs_set_compressed_page(struct page *page,
		struct inode *inode, pgoff_t index, void *data)
{
	SetPagePrivate(page);
	set_page_private(page, (unsigned long)data);

	/* i_crypto_info and iv index */
	page->index = index;
	page->mapping = inode->i_mapping;
}

static void f2fs_put_compressed_page(struct page *page)
{
	set_page_private(page, (unsigned long)NULL);
	ClearPagePrivate(page);
	page->mapping = NULL;
	unlock_page(page);
	put_page(page);
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

static void f2fs_put_rpages_mapping(struct compress_ctx *cc,
				struct address_space *mapping,
				pgoff_t start, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		struct page *page = find_get_page(mapping, start + i);

		put_page(page);
		put_page(page);
	}
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
	struct f2fs_sb_info *sbi = F2FS_I_SB(cc->inode);

	if (cc->nr_rpages)
		return 0;

	cc->rpages = f2fs_kzalloc(sbi, sizeof(struct page *) <<
					cc->log_cluster_size, GFP_NOFS);
	return cc->rpages ? 0 : -ENOMEM;
}

void f2fs_destroy_compress_ctx(struct compress_ctx *cc)
{
	kfree(cc->rpages);
	cc->rpages = NULL;
	cc->nr_rpages = 0;
	cc->nr_cpages = 0;
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
	cc->private = f2fs_kvmalloc(F2FS_I_SB(cc->inode),
				LZ4_MEM_COMPRESS, GFP_NOFS);
	if (!cc->private)
		return -ENOMEM;

	cc->clen = LZ4_compressBound(PAGE_SIZE << cc->log_cluster_size);
	return 0;
}

static void lz4_destroy_compress_ctx(struct compress_ctx *cc)
{
	kvfree(cc->private);
	cc->private = NULL;
}

static int lz4_compress_pages(struct compress_ctx *cc)
{
	int len;

	len = LZ4_compress_default(cc->rbuf, cc->cbuf->cdata, cc->rlen,
						cc->clen, cc->private);
	if (!len) {
		printk_ratelimited("%sF2FS-fs (%s): lz4 compress failed\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id);
		return -EIO;
	}
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
	ZSTD_parameters params;
	ZSTD_CStream *stream;
	void *workspace;
	unsigned int workspace_size;

	params = ZSTD_getParams(F2FS_ZSTD_DEFAULT_CLEVEL, cc->rlen, 0);
	workspace_size = ZSTD_CStreamWorkspaceBound(params.cParams);

	workspace = f2fs_kvmalloc(F2FS_I_SB(cc->inode),
					workspace_size, GFP_NOFS);
	if (!workspace)
		return -ENOMEM;

	stream = ZSTD_initCStream(params, 0, workspace, workspace_size);
	if (!stream) {
		printk_ratelimited("%sF2FS-fs (%s): %s ZSTD_initCStream failed\n",
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
	ZSTD_CStream *stream = cc->private2;
	ZSTD_inBuffer inbuf;
	ZSTD_outBuffer outbuf;
	int src_size = cc->rlen;
	int dst_size = src_size - PAGE_SIZE - COMPRESS_HEADER_SIZE;
	int ret;

	inbuf.pos = 0;
	inbuf.src = cc->rbuf;
	inbuf.size = src_size;

	outbuf.pos = 0;
	outbuf.dst = cc->cbuf->cdata;
	outbuf.size = dst_size;

	ret = ZSTD_compressStream(stream, &outbuf, &inbuf);
	if (ZSTD_isError(ret)) {
		printk_ratelimited("%sF2FS-fs (%s): %s ZSTD_compressStream failed, ret: %d\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id,
				__func__, ZSTD_getErrorCode(ret));
		return -EIO;
	}

	ret = ZSTD_endStream(stream, &outbuf);
	if (ZSTD_isError(ret)) {
		printk_ratelimited("%sF2FS-fs (%s): %s ZSTD_endStream returned %d\n",
				KERN_ERR, F2FS_I_SB(cc->inode)->sb->s_id,
				__func__, ZSTD_getErrorCode(ret));
		return -EIO;
	}

	cc->clen = outbuf.pos;
	return 0;
}

static int zstd_init_decompress_ctx(struct decompress_io_ctx *dic)
{
	ZSTD_DStream *stream;
	void *workspace;
	unsigned int workspace_size;

	workspace_size = ZSTD_DStreamWorkspaceBound(MAX_COMPRESS_WINDOW_SIZE);

	workspace = f2fs_kvmalloc(F2FS_I_SB(dic->inode),
					workspace_size, GFP_NOFS);
	if (!workspace)
		return -ENOMEM;

	stream = ZSTD_initDStream(MAX_COMPRESS_WINDOW_SIZE,
					workspace, workspace_size);
	if (!stream) {
		printk_ratelimited("%sF2FS-fs (%s): %s ZSTD_initDStream failed\n",
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
	ZSTD_DStream *stream = dic->private2;
	ZSTD_inBuffer inbuf;
	ZSTD_outBuffer outbuf;
	int ret;

	inbuf.pos = 0;
	inbuf.src = dic->cbuf->cdata;
	inbuf.size = dic->clen;

	outbuf.pos = 0;
	outbuf.dst = dic->rbuf;
	outbuf.size = dic->rlen;

	ret = ZSTD_decompressStream(stream, &outbuf, &inbuf);
	if (ZSTD_isError(ret)) {
		printk_ratelimited("%sF2FS-fs (%s): %s ZSTD_compressStream failed, ret: %d\n",
				KERN_ERR, F2FS_I_SB(dic->inode)->sb->s_id,
				__func__, ZSTD_getErrorCode(ret));
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
};

bool f2fs_is_compress_backend_ready(struct inode *inode)
{
	if (!f2fs_compressed_file(inode))
		return true;
	return f2fs_cops[F2FS_I(inode)->i_compress_algorithm];
}

static struct page *f2fs_grab_page(void)
{
	struct page *page;

	page = alloc_page(GFP_NOFS);
	if (!page)
		return NULL;
	lock_page(page);
	return page;
}

static int f2fs_compress_pages(struct compress_ctx *cc)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(cc->inode);
	struct f2fs_inode_info *fi = F2FS_I(cc->inode);
	const struct f2fs_compress_ops *cops =
				f2fs_cops[fi->i_compress_algorithm];
	unsigned int max_len, nr_cpages;
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

	cc->cpages = f2fs_kzalloc(sbi, sizeof(struct page *) *
					cc->nr_cpages, GFP_NOFS);
	if (!cc->cpages) {
		ret = -ENOMEM;
		goto destroy_compress_ctx;
	}

	for (i = 0; i < cc->nr_cpages; i++) {
		cc->cpages[i] = f2fs_grab_page();
		if (!cc->cpages[i]) {
			ret = -ENOMEM;
			goto out_free_cpages;
		}
	}

	cc->rbuf = vmap(cc->rpages, cc->cluster_size, VM_MAP, PAGE_KERNEL_RO);
	if (!cc->rbuf) {
		ret = -ENOMEM;
		goto out_free_cpages;
	}

	cc->cbuf = vmap(cc->cpages, cc->nr_cpages, VM_MAP, PAGE_KERNEL);
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

	for (i = 0; i < COMPRESS_DATA_RESERVED_SIZE; i++)
		cc->cbuf->reserved[i] = cpu_to_le32(0);

	nr_cpages = DIV_ROUND_UP(cc->clen + COMPRESS_HEADER_SIZE, PAGE_SIZE);

	/* zero out any unused part of the last page */
	memset(&cc->cbuf->cdata[cc->clen], 0,
	       (nr_cpages * PAGE_SIZE) - (cc->clen + COMPRESS_HEADER_SIZE));

	vunmap(cc->cbuf);
	vunmap(cc->rbuf);

	for (i = nr_cpages; i < cc->nr_cpages; i++) {
		f2fs_put_compressed_page(cc->cpages[i]);
		cc->cpages[i] = NULL;
	}

	if (cops->destroy_compress_ctx)
		cops->destroy_compress_ctx(cc);

	cc->nr_cpages = nr_cpages;

	trace_f2fs_compress_pages_end(cc->inode, cc->cluster_idx,
							cc->clen, ret);
	return 0;

out_vunmap_cbuf:
	vunmap(cc->cbuf);
out_vunmap_rbuf:
	vunmap(cc->rbuf);
out_free_cpages:
	for (i = 0; i < cc->nr_cpages; i++) {
		if (cc->cpages[i])
			f2fs_put_compressed_page(cc->cpages[i]);
	}
	kfree(cc->cpages);
	cc->cpages = NULL;
destroy_compress_ctx:
	if (cops->destroy_compress_ctx)
		cops->destroy_compress_ctx(cc);
out:
	trace_f2fs_compress_pages_end(cc->inode, cc->cluster_idx,
							cc->clen, ret);
	return ret;
}

void f2fs_decompress_pages(struct bio *bio, struct page *page, bool verity)
{
	struct decompress_io_ctx *dic =
			(struct decompress_io_ctx *)page_private(page);
	struct f2fs_sb_info *sbi = F2FS_I_SB(dic->inode);
	struct f2fs_inode_info *fi= F2FS_I(dic->inode);
	const struct f2fs_compress_ops *cops =
			f2fs_cops[fi->i_compress_algorithm];
	int ret;

	dec_page_count(sbi, F2FS_RD_DATA);

	if (bio->bi_status || PageError(page))
		dic->failed = true;

	if (refcount_dec_not_one(&dic->ref))
		return;

	trace_f2fs_decompress_pages_start(dic->inode, dic->cluster_idx,
				dic->cluster_size, fi->i_compress_algorithm);

	/* submit partial compressed pages */
	if (dic->failed) {
		ret = -EIO;
		goto out_free_dic;
	}

	if (cops->init_decompress_ctx) {
		ret = cops->init_decompress_ctx(dic);
		if (ret)
			goto out_free_dic;
	}

	dic->rbuf = vmap(dic->tpages, dic->cluster_size, VM_MAP, PAGE_KERNEL);
	if (!dic->rbuf) {
		ret = -ENOMEM;
		goto destroy_decompress_ctx;
	}

	dic->cbuf = vmap(dic->cpages, dic->nr_cpages, VM_MAP, PAGE_KERNEL_RO);
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

out_vunmap_cbuf:
	vunmap(dic->cbuf);
out_vunmap_rbuf:
	vunmap(dic->rbuf);
destroy_decompress_ctx:
	if (cops->destroy_decompress_ctx)
		cops->destroy_decompress_ctx(dic);
out_free_dic:
	if (verity)
		refcount_set(&dic->ref, dic->nr_cpages);
	if (!verity)
		f2fs_decompress_end_io(dic->rpages, dic->cluster_size,
								ret, false);

	trace_f2fs_decompress_pages_end(dic->inode, dic->cluster_idx,
							dic->clen, ret);
	if (!verity)
		f2fs_free_dic(dic);
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

static bool __cluster_may_compress(struct compress_ctx *cc)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(cc->inode);
	loff_t i_size = i_size_read(cc->inode);
	unsigned nr_pages = DIV_ROUND_UP(i_size, PAGE_SIZE);
	int i;

	for (i = 0; i < cc->cluster_size; i++) {
		struct page *page = cc->rpages[i];

		f2fs_bug_on(sbi, !page);

		if (unlikely(f2fs_cp_error(sbi)))
			return false;
		if (unlikely(is_sbi_flag_set(sbi, SBI_POR_DOING)))
			return false;

		/* beyond EOF */
		if (page->index >= nr_pages)
			return false;
	}
	return true;
}

static int __f2fs_cluster_blocks(struct compress_ctx *cc, bool compr)
{
	struct dnode_of_data dn;
	int ret;

	set_new_dnode(&dn, cc->inode, NULL, NULL, 0);
	ret = f2fs_get_dnode_of_data(&dn, start_idx_of_cluster(cc),
							LOOKUP_NODE);
	if (ret) {
		if (ret == -ENOENT)
			ret = 0;
		goto fail;
	}

	if (dn.data_blkaddr == COMPRESS_ADDR) {
		int i;

		ret = 1;
		for (i = 1; i < cc->cluster_size; i++) {
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
	}
fail:
	f2fs_put_dnode(&dn);
	return ret;
}

/* return # of compressed blocks in compressed cluster */
static int f2fs_compressed_blocks(struct compress_ctx *cc)
{
	return __f2fs_cluster_blocks(cc, true);
}

/* return # of valid blocks in compressed cluster */
static int f2fs_cluster_blocks(struct compress_ctx *cc, bool compr)
{
	return __f2fs_cluster_blocks(cc, false);
}

int f2fs_is_compressed_cluster(struct inode *inode, pgoff_t index)
{
	struct compress_ctx cc = {
		.inode = inode,
		.log_cluster_size = F2FS_I(inode)->i_log_cluster_size,
		.cluster_size = F2FS_I(inode)->i_cluster_size,
		.cluster_idx = index >> F2FS_I(inode)->i_log_cluster_size,
	};

	return f2fs_cluster_blocks(&cc, false);
}

static bool cluster_may_compress(struct compress_ctx *cc)
{
	if (!f2fs_compressed_file(cc->inode))
		return false;
	if (f2fs_is_atomic_file(cc->inode))
		return false;
	if (f2fs_is_mmap_file(cc->inode))
		return false;
	if (!f2fs_cluster_is_full(cc))
		return false;
	return __cluster_may_compress(cc);
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
	struct dnode_of_data dn;
	sector_t last_block_in_bio;
	unsigned fgp_flag = FGP_LOCK | FGP_WRITE | FGP_CREAT;
	pgoff_t start_idx = start_idx_of_cluster(cc);
	int i, ret;
	bool prealloc;

retry:
	ret = f2fs_cluster_blocks(cc, false);
	if (ret <= 0)
		return ret;

	/* compressed case */
	prealloc = (ret < cc->cluster_size);

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
			unlock_page(page);
		else
			f2fs_compress_ctx_add_page(cc, page);
	}

	if (!f2fs_cluster_is_empty(cc)) {
		struct bio *bio = NULL;

		ret = f2fs_read_multi_pages(cc, &bio, cc->cluster_size,
					&last_block_in_bio, false, true);
		f2fs_destroy_compress_ctx(cc);
		if (ret)
			goto release_pages;
		if (bio)
			f2fs_submit_bio(sbi, bio, DATA);

		ret = f2fs_init_compress_ctx(cc);
		if (ret)
			goto release_pages;
	}

	for (i = 0; i < cc->cluster_size; i++) {
		f2fs_bug_on(sbi, cc->rpages[i]);

		page = find_lock_page(mapping, start_idx + i);
		f2fs_bug_on(sbi, !page);

		f2fs_wait_on_page_writeback(page, DATA, true, true);

		f2fs_compress_ctx_add_page(cc, page);
		f2fs_put_page(page, 0);

		if (!PageUptodate(page)) {
			f2fs_unlock_rpages(cc, i + 1);
			f2fs_put_rpages_mapping(cc, mapping, start_idx,
					cc->cluster_size);
			f2fs_destroy_compress_ctx(cc);
			goto retry;
		}
	}

	if (prealloc) {
		__do_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO, true);

		set_new_dnode(&dn, cc->inode, NULL, NULL, 0);

		for (i = cc->cluster_size - 1; i > 0; i--) {
			ret = f2fs_get_block(&dn, start_idx + i);
			if (ret) {
				i = cc->cluster_size;
				break;
			}

			if (dn.data_blkaddr != NEW_ADDR)
				break;
		}

		__do_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO, false);
	}

	if (likely(!ret)) {
		*fsdata = cc->rpages;
		*pagep = cc->rpages[offset_in_cluster(cc, index)];
		return cc->cluster_size;
	}

unlock_pages:
	f2fs_unlock_rpages(cc, i);
release_pages:
	f2fs_put_rpages_mapping(cc, mapping, start_idx, i);
	f2fs_destroy_compress_ctx(cc);
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
		.log_cluster_size = F2FS_I(inode)->i_log_cluster_size,
		.cluster_size = F2FS_I(inode)->i_cluster_size,
		.rpages = fsdata,
	};
	bool first_index = (index == cc.rpages[0]->index);

	if (copied)
		set_cluster_dirty(&cc);

	f2fs_put_rpages_wbc(&cc, NULL, false, 1);
	f2fs_destroy_compress_ctx(&cc);

	return first_index;
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
		.encrypted = f2fs_encrypted_file(cc->inode),
	};
	struct dnode_of_data dn;
	struct node_info ni;
	struct compress_io_ctx *cic;
	pgoff_t start_idx = start_idx_of_cluster(cc);
	unsigned int last_index = cc->cluster_size - 1;
	loff_t psize;
	int i, err;

	if (!IS_NOQUOTA(inode) && !f2fs_trylock_op(sbi))
		return -EAGAIN;

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

	err = f2fs_get_node_info(fio.sbi, dn.nid, &ni);
	if (err)
		goto out_put_dnode;

	fio.version = ni.version;

	cic = f2fs_kzalloc(sbi, sizeof(struct compress_io_ctx), GFP_NOFS);
	if (!cic)
		goto out_put_dnode;

	cic->magic = F2FS_COMPRESSED_PAGE_MAGIC;
	cic->inode = inode;
	refcount_set(&cic->ref, cc->nr_cpages);
	cic->rpages = f2fs_kzalloc(sbi, sizeof(struct page *) <<
			cc->log_cluster_size, GFP_NOFS);
	if (!cic->rpages)
		goto out_put_cic;

	cic->nr_rpages = cc->cluster_size;

	for (i = 0; i < cc->nr_cpages; i++) {
		f2fs_set_compressed_page(cc->cpages[i], inode,
					cc->rpages[i + 1]->index, cic);
		fio.compressed_page = cc->cpages[i];
		if (fio.encrypted) {
			fio.page = cc->rpages[i + 1];
			err = f2fs_encrypt_one_page(&fio);
			if (err)
				goto out_destroy_crypt;
			if (fscrypt_inode_uses_fs_layer_crypto(inode))
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

		if (i > cc->nr_cpages) {
			if (__is_valid_data_blkaddr(blkaddr)) {
				f2fs_invalidate_blocks(sbi, blkaddr);
				f2fs_update_data_blkaddr(&dn, NEW_ADDR);
			}
			goto unlock_continue;
		}

		f2fs_bug_on(fio.sbi, blkaddr == NULL_ADDR);

		if (fio.encrypted && fscrypt_inode_uses_fs_layer_crypto(inode))
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
	f2fs_i_compr_blocks_update(inode, cc->nr_cpages, true);

	set_inode_flag(cc->inode, FI_APPEND_WRITE);
	if (cc->cluster_idx == 0)
		set_inode_flag(inode, FI_FIRST_BLOCK_WRITTEN);

	f2fs_put_dnode(&dn);
	if (!IS_NOQUOTA(inode))
		f2fs_unlock_op(sbi);

	spin_lock(&fi->i_size_lock);
	if (fi->last_disk_size < psize)
		fi->last_disk_size = psize;
	spin_unlock(&fi->i_size_lock);

	f2fs_put_rpages(cc);
	f2fs_destroy_compress_ctx(cc);
	return 0;

out_destroy_crypt:
	kfree(cic->rpages);

	for (--i; i >= 0; i--)
		fscrypt_finalize_bounce_page(&cc->cpages[i]);
	for (i = 0; i < cc->nr_cpages; i++) {
		if (!cc->cpages[i])
			continue;
		f2fs_put_page(cc->cpages[i], 1);
	}
out_put_cic:
	kfree(cic);
out_put_dnode:
	f2fs_put_dnode(&dn);
out_unlock_op:
	if (!IS_NOQUOTA(inode))
		f2fs_unlock_op(sbi);
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

	f2fs_put_compressed_page(page);

	dec_page_count(sbi, F2FS_WB_DATA);

	if (refcount_dec_not_one(&cic->ref))
		return;

	for (i = 0; i < cic->nr_rpages; i++) {
		WARN_ON(!cic->rpages[i]);
		clear_cold_data(cic->rpages[i]);
		end_page_writeback(cic->rpages[i]);
	}

	kfree(cic->rpages);
	kfree(cic);
}

static int f2fs_write_raw_pages(struct compress_ctx *cc,
					int *submitted,
					struct writeback_control *wbc,
					enum iostat_type io_type)
{
	struct address_space *mapping = cc->inode->i_mapping;
	int _submitted, compr_blocks, ret;
	int i = -1, err = 0;

	compr_blocks = f2fs_compressed_blocks(cc);
	if (compr_blocks < 0) {
		err = compr_blocks;
		goto out_err;
	}

	for (i = 0; i < cc->cluster_size; i++) {
		if (!cc->rpages[i])
			continue;
retry_write:
		if (cc->rpages[i]->mapping != mapping) {
			unlock_page(cc->rpages[i]);
			continue;
		}

		BUG_ON(!PageLocked(cc->rpages[i]));

		ret = f2fs_write_single_data_page(cc->rpages[i], &_submitted,
						NULL, NULL, wbc, io_type,
						compr_blocks);
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
				if (IS_NOQUOTA(cc->inode)) {
					err = 0;
					goto out_err;
				}
				ret = 0;
				cond_resched();
				congestion_wait(BLK_RW_ASYNC,
						DEFAULT_IO_TIMEOUT);
				lock_page(cc->rpages[i]);
				clear_page_dirty_for_io(cc->rpages[i]);
				goto retry_write;
			}
			err = ret;
			goto out_err;
		}

		*submitted += _submitted;
	}
	return 0;
out_err:
	for (++i; i < cc->cluster_size; i++) {
		if (!cc->rpages[i])
			continue;
		redirty_page_for_writepage(wbc, cc->rpages[i]);
		unlock_page(cc->rpages[i]);
	}
	return err;
}

int f2fs_write_multi_pages(struct compress_ctx *cc,
					int *submitted,
					struct writeback_control *wbc,
					enum iostat_type io_type)
{
	struct f2fs_inode_info *fi = F2FS_I(cc->inode);
	const struct f2fs_compress_ops *cops =
			f2fs_cops[fi->i_compress_algorithm];
	int err;

	*submitted = 0;
	if (cluster_may_compress(cc)) {
		err = f2fs_compress_pages(cc);
		if (err == -EAGAIN) {
			goto write;
		} else if (err) {
			f2fs_put_rpages_wbc(cc, wbc, true, 1);
			goto destroy_out;
		}

		err = f2fs_write_compressed_pages(cc, submitted,
							wbc, io_type);
		cops->destroy_compress_ctx(cc);
		if (!err)
			return 0;
		f2fs_bug_on(F2FS_I_SB(cc->inode), err != -EAGAIN);
	}
write:
	f2fs_bug_on(F2FS_I_SB(cc->inode), *submitted);

	err = f2fs_write_raw_pages(cc, submitted, wbc, io_type);
	f2fs_put_rpages_wbc(cc, wbc, false, 0);
destroy_out:
	f2fs_destroy_compress_ctx(cc);
	return err;
}

struct decompress_io_ctx *f2fs_alloc_dic(struct compress_ctx *cc)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(cc->inode);
	struct decompress_io_ctx *dic;
	pgoff_t start_idx = start_idx_of_cluster(cc);
	int i;

	dic = f2fs_kzalloc(sbi, sizeof(struct decompress_io_ctx), GFP_NOFS);
	if (!dic)
		return ERR_PTR(-ENOMEM);

	dic->rpages = f2fs_kzalloc(sbi, sizeof(struct page *) <<
			cc->log_cluster_size, GFP_NOFS);
	if (!dic->rpages) {
		kfree(dic);
		return ERR_PTR(-ENOMEM);
	}

	dic->magic = F2FS_COMPRESSED_PAGE_MAGIC;
	dic->inode = cc->inode;
	refcount_set(&dic->ref, cc->nr_cpages);
	dic->cluster_idx = cc->cluster_idx;
	dic->cluster_size = cc->cluster_size;
	dic->log_cluster_size = cc->log_cluster_size;
	dic->nr_cpages = cc->nr_cpages;
	dic->failed = false;

	for (i = 0; i < dic->cluster_size; i++)
		dic->rpages[i] = cc->rpages[i];
	dic->nr_rpages = cc->cluster_size;

	dic->cpages = f2fs_kzalloc(sbi, sizeof(struct page *) *
					dic->nr_cpages, GFP_NOFS);
	if (!dic->cpages)
		goto out_free;

	for (i = 0; i < dic->nr_cpages; i++) {
		struct page *page;

		page = f2fs_grab_page();
		if (!page)
			goto out_free;

		f2fs_set_compressed_page(page, cc->inode,
					start_idx + i + 1, dic);
		dic->cpages[i] = page;
	}

	dic->tpages = f2fs_kzalloc(sbi, sizeof(struct page *) *
					dic->cluster_size, GFP_NOFS);
	if (!dic->tpages)
		goto out_free;

	for (i = 0; i < dic->cluster_size; i++) {
		if (cc->rpages[i]) {
			dic->tpages[i] = cc->rpages[i];
			continue;
		}

		dic->tpages[i] = f2fs_grab_page();
		if (!dic->tpages[i])
			goto out_free;
	}

	return dic;

out_free:
	f2fs_free_dic(dic);
	return ERR_PTR(-ENOMEM);
}

void f2fs_free_dic(struct decompress_io_ctx *dic)
{
	int i;

	if (dic->tpages) {
		for (i = 0; i < dic->cluster_size; i++) {
			if (dic->rpages[i])
				continue;
			if (!dic->tpages[i])
				continue;
			unlock_page(dic->tpages[i]);
			put_page(dic->tpages[i]);
		}
		kfree(dic->tpages);
	}

	if (dic->cpages) {
		for (i = 0; i < dic->nr_cpages; i++) {
			if (!dic->cpages[i])
				continue;
			f2fs_put_compressed_page(dic->cpages[i]);
		}
		kfree(dic->cpages);
	}

	kfree(dic->rpages);
	kfree(dic);
}

void f2fs_decompress_end_io(struct page **rpages,
			unsigned int cluster_size, bool err, bool verity)
{
	int i;

	for (i = 0; i < cluster_size; i++) {
		struct page *rpage = rpages[i];

		if (!rpage)
			continue;

		if (err || PageError(rpage))
			goto clear_uptodate;

		if (!verity || fsverity_verify_page(rpage)) {
			SetPageUptodate(rpage);
			goto unlock;
		}
clear_uptodate:
		ClearPageUptodate(rpage);
		ClearPageError(rpage);
unlock:
		unlock_page(rpage);
	}
}
