// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * Based on jffs2 zlib code:
 * Copyright © 2001-2007 Red Hat, Inc.
 * Created by David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/zlib.h>
#include <linux/zutil.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/refcount.h>
#include "btrfs_inode.h"
#include "compression.h"
#include "fs.h"
#include "subpage.h"

/* workspace buffer size for s390 zlib hardware support */
#define ZLIB_DFLTCC_BUF_SIZE    (4 * PAGE_SIZE)

struct workspace {
	z_stream strm;
	char *buf;
	unsigned int buf_size;
	struct list_head list;
	int level;
};

static struct workspace_manager wsm;

struct list_head *zlib_get_workspace(unsigned int level)
{
	struct list_head *ws = btrfs_get_workspace(BTRFS_COMPRESS_ZLIB, level);
	struct workspace *workspace = list_entry(ws, struct workspace, list);

	workspace->level = level;

	return ws;
}

void zlib_free_workspace(struct list_head *ws)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);

	kvfree(workspace->strm.workspace);
	kfree(workspace->buf);
	kfree(workspace);
}

struct list_head *zlib_alloc_workspace(unsigned int level)
{
	struct workspace *workspace;
	int workspacesize;

	workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return ERR_PTR(-ENOMEM);

	workspacesize = max(zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL),
			zlib_inflate_workspacesize());
	workspace->strm.workspace = kvzalloc(workspacesize, GFP_KERNEL | __GFP_NOWARN);
	workspace->level = level;
	workspace->buf = NULL;
	/*
	 * In case of s390 zlib hardware support, allocate lager workspace
	 * buffer. If allocator fails, fall back to a single page buffer.
	 */
	if (zlib_deflate_dfltcc_enabled()) {
		workspace->buf = kmalloc(ZLIB_DFLTCC_BUF_SIZE,
					 __GFP_NOMEMALLOC | __GFP_NORETRY |
					 __GFP_NOWARN | GFP_NOIO);
		workspace->buf_size = ZLIB_DFLTCC_BUF_SIZE;
	}
	if (!workspace->buf) {
		workspace->buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		workspace->buf_size = PAGE_SIZE;
	}
	if (!workspace->strm.workspace || !workspace->buf)
		goto fail;

	INIT_LIST_HEAD(&workspace->list);

	return &workspace->list;
fail:
	zlib_free_workspace(&workspace->list);
	return ERR_PTR(-ENOMEM);
}

/*
 * Helper for S390x with hardware zlib compression support.
 *
 * That hardware acceleration requires a buffer size larger than a single page
 * to get ideal performance, thus we need to do the memory copy rather than
 * use the page cache directly as input buffer.
 */
static int copy_data_into_buffer(struct address_space *mapping,
				 struct workspace *workspace, u64 filepos,
				 unsigned long length)
{
	u64 cur = filepos;

	/* It's only for hardware accelerated zlib code. */
	ASSERT(zlib_deflate_dfltcc_enabled());

	while (cur < filepos + length) {
		struct folio *folio;
		void *data_in;
		unsigned int offset;
		unsigned long copy_length;
		int ret;

		ret = btrfs_compress_filemap_get_folio(mapping, cur, &folio);
		if (ret < 0)
			return ret;
		/* No large folio support yet. */
		ASSERT(!folio_test_large(folio));

		offset = offset_in_folio(folio, cur);
		copy_length = min(folio_size(folio) - offset,
				  filepos + length - cur);

		data_in = kmap_local_folio(folio, offset);
		memcpy(workspace->buf + cur - filepos, data_in, copy_length);
		kunmap_local(data_in);
		cur += copy_length;
	}
	return 0;
}

int zlib_compress_folios(struct list_head *ws, struct address_space *mapping,
			 u64 start, struct folio **folios, unsigned long *out_folios,
			 unsigned long *total_in, unsigned long *total_out)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	int ret;
	char *data_in = NULL;
	char *cfolio_out;
	int nr_folios = 0;
	struct folio *in_folio = NULL;
	struct folio *out_folio = NULL;
	unsigned long len = *total_out;
	unsigned long nr_dest_folios = *out_folios;
	const unsigned long max_out = nr_dest_folios * PAGE_SIZE;
	const u64 orig_end = start + len;

	*out_folios = 0;
	*total_out = 0;
	*total_in = 0;

	ret = zlib_deflateInit(&workspace->strm, workspace->level);
	if (unlikely(ret != Z_OK)) {
		struct btrfs_inode *inode = BTRFS_I(mapping->host);

		btrfs_err(inode->root->fs_info,
	"zlib compression init failed, error %d root %llu inode %llu offset %llu",
			  ret, btrfs_root_id(inode->root), btrfs_ino(inode), start);
		ret = -EIO;
		goto out;
	}

	workspace->strm.total_in = 0;
	workspace->strm.total_out = 0;

	out_folio = btrfs_alloc_compr_folio();
	if (out_folio == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	cfolio_out = folio_address(out_folio);
	folios[0] = out_folio;
	nr_folios = 1;

	workspace->strm.next_in = workspace->buf;
	workspace->strm.avail_in = 0;
	workspace->strm.next_out = cfolio_out;
	workspace->strm.avail_out = PAGE_SIZE;

	while (workspace->strm.total_in < len) {
		/*
		 * Get next input pages and copy the contents to
		 * the workspace buffer if required.
		 */
		if (workspace->strm.avail_in == 0) {
			unsigned long bytes_left = len - workspace->strm.total_in;
			unsigned int copy_length = min(bytes_left, workspace->buf_size);

			/*
			 * This can only happen when hardware zlib compression is
			 * enabled.
			 */
			if (copy_length > PAGE_SIZE) {
				ret = copy_data_into_buffer(mapping, workspace,
							    start, copy_length);
				if (ret < 0)
					goto out;
				start += copy_length;
				workspace->strm.next_in = workspace->buf;
				workspace->strm.avail_in = copy_length;
			} else {
				unsigned int pg_off;
				unsigned int cur_len;

				if (data_in) {
					kunmap_local(data_in);
					folio_put(in_folio);
					data_in = NULL;
				}
				ret = btrfs_compress_filemap_get_folio(mapping,
						start, &in_folio);
				if (ret < 0)
					goto out;
				pg_off = offset_in_page(start);
				cur_len = btrfs_calc_input_length(orig_end, start);
				data_in = kmap_local_folio(in_folio, pg_off);
				start += cur_len;
				workspace->strm.next_in = data_in;
				workspace->strm.avail_in = cur_len;
			}
		}

		ret = zlib_deflate(&workspace->strm, Z_SYNC_FLUSH);
		if (unlikely(ret != Z_OK)) {
			struct btrfs_inode *inode = BTRFS_I(mapping->host);

			btrfs_warn(inode->root->fs_info,
		"zlib compression failed, error %d root %llu inode %llu offset %llu",
				   ret, btrfs_root_id(inode->root), btrfs_ino(inode),
				   start);
			zlib_deflateEnd(&workspace->strm);
			ret = -EIO;
			goto out;
		}

		/* we're making it bigger, give up */
		if (workspace->strm.total_in > 8192 &&
		    workspace->strm.total_in <
		    workspace->strm.total_out) {
			ret = -E2BIG;
			goto out;
		}
		/* we need another page for writing out.  Test this
		 * before the total_in so we will pull in a new page for
		 * the stream end if required
		 */
		if (workspace->strm.avail_out == 0) {
			if (nr_folios == nr_dest_folios) {
				ret = -E2BIG;
				goto out;
			}
			out_folio = btrfs_alloc_compr_folio();
			if (out_folio == NULL) {
				ret = -ENOMEM;
				goto out;
			}
			cfolio_out = folio_address(out_folio);
			folios[nr_folios] = out_folio;
			nr_folios++;
			workspace->strm.avail_out = PAGE_SIZE;
			workspace->strm.next_out = cfolio_out;
		}
		/* we're all done */
		if (workspace->strm.total_in >= len)
			break;
		if (workspace->strm.total_out > max_out)
			break;
	}
	workspace->strm.avail_in = 0;
	/*
	 * Call deflate with Z_FINISH flush parameter providing more output
	 * space but no more input data, until it returns with Z_STREAM_END.
	 */
	while (ret != Z_STREAM_END) {
		ret = zlib_deflate(&workspace->strm, Z_FINISH);
		if (ret == Z_STREAM_END)
			break;
		if (ret != Z_OK && ret != Z_BUF_ERROR) {
			zlib_deflateEnd(&workspace->strm);
			ret = -EIO;
			goto out;
		} else if (workspace->strm.avail_out == 0) {
			/* Get another folio for the stream end. */
			if (nr_folios == nr_dest_folios) {
				ret = -E2BIG;
				goto out;
			}
			out_folio = btrfs_alloc_compr_folio();
			if (out_folio == NULL) {
				ret = -ENOMEM;
				goto out;
			}
			cfolio_out = folio_address(out_folio);
			folios[nr_folios] = out_folio;
			nr_folios++;
			workspace->strm.avail_out = PAGE_SIZE;
			workspace->strm.next_out = cfolio_out;
		}
	}
	zlib_deflateEnd(&workspace->strm);

	if (workspace->strm.total_out >= workspace->strm.total_in) {
		ret = -E2BIG;
		goto out;
	}

	ret = 0;
	*total_out = workspace->strm.total_out;
	*total_in = workspace->strm.total_in;
out:
	*out_folios = nr_folios;
	if (data_in) {
		kunmap_local(data_in);
		folio_put(in_folio);
	}

	return ret;
}

int zlib_decompress_bio(struct list_head *ws, struct compressed_bio *cb)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	int ret = 0, ret2;
	int wbits = MAX_WBITS;
	char *data_in;
	size_t total_out = 0;
	unsigned long folio_in_index = 0;
	size_t srclen = cb->compressed_len;
	unsigned long total_folios_in = DIV_ROUND_UP(srclen, PAGE_SIZE);
	unsigned long buf_start;
	struct folio **folios_in = cb->compressed_folios;

	data_in = kmap_local_folio(folios_in[folio_in_index], 0);
	workspace->strm.next_in = data_in;
	workspace->strm.avail_in = min_t(size_t, srclen, PAGE_SIZE);
	workspace->strm.total_in = 0;

	workspace->strm.total_out = 0;
	workspace->strm.next_out = workspace->buf;
	workspace->strm.avail_out = workspace->buf_size;

	/* If it's deflate, and it's got no preset dictionary, then
	   we can tell zlib to skip the adler32 check. */
	if (srclen > 2 && !(data_in[1] & PRESET_DICT) &&
	    ((data_in[0] & 0x0f) == Z_DEFLATED) &&
	    !(((data_in[0]<<8) + data_in[1]) % 31)) {

		wbits = -((data_in[0] >> 4) + 8);
		workspace->strm.next_in += 2;
		workspace->strm.avail_in -= 2;
	}

	ret = zlib_inflateInit2(&workspace->strm, wbits);
	if (unlikely(ret != Z_OK)) {
		struct btrfs_inode *inode = cb->bbio.inode;

		kunmap_local(data_in);
		btrfs_err(inode->root->fs_info,
	"zlib decompression init failed, error %d root %llu inode %llu offset %llu",
			  ret, btrfs_root_id(inode->root), btrfs_ino(inode), cb->start);
		return -EIO;
	}
	while (workspace->strm.total_in < srclen) {
		ret = zlib_inflate(&workspace->strm, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END)
			break;

		buf_start = total_out;
		total_out = workspace->strm.total_out;

		/* we didn't make progress in this inflate call, we're done */
		if (buf_start == total_out)
			break;

		ret2 = btrfs_decompress_buf2page(workspace->buf,
				total_out - buf_start, cb, buf_start);
		if (ret2 == 0) {
			ret = 0;
			goto done;
		}

		workspace->strm.next_out = workspace->buf;
		workspace->strm.avail_out = workspace->buf_size;

		if (workspace->strm.avail_in == 0) {
			unsigned long tmp;
			kunmap_local(data_in);
			folio_in_index++;
			if (folio_in_index >= total_folios_in) {
				data_in = NULL;
				break;
			}
			data_in = kmap_local_folio(folios_in[folio_in_index], 0);
			workspace->strm.next_in = data_in;
			tmp = srclen - workspace->strm.total_in;
			workspace->strm.avail_in = min(tmp, PAGE_SIZE);
		}
	}
	if (unlikely(ret != Z_STREAM_END)) {
		btrfs_err(cb->bbio.inode->root->fs_info,
		"zlib decompression failed, error %d root %llu inode %llu offset %llu",
			  ret, btrfs_root_id(cb->bbio.inode->root),
			  btrfs_ino(cb->bbio.inode), cb->start);
		ret = -EIO;
	} else {
		ret = 0;
	}
done:
	zlib_inflateEnd(&workspace->strm);
	if (data_in)
		kunmap_local(data_in);
	return ret;
}

int zlib_decompress(struct list_head *ws, const u8 *data_in,
		struct folio *dest_folio, unsigned long dest_pgoff, size_t srclen,
		size_t destlen)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	int ret = 0;
	int wbits = MAX_WBITS;
	unsigned long to_copy;

	workspace->strm.next_in = data_in;
	workspace->strm.avail_in = srclen;
	workspace->strm.total_in = 0;

	workspace->strm.next_out = workspace->buf;
	workspace->strm.avail_out = workspace->buf_size;
	workspace->strm.total_out = 0;
	/* If it's deflate, and it's got no preset dictionary, then
	   we can tell zlib to skip the adler32 check. */
	if (srclen > 2 && !(data_in[1] & PRESET_DICT) &&
	    ((data_in[0] & 0x0f) == Z_DEFLATED) &&
	    !(((data_in[0]<<8) + data_in[1]) % 31)) {

		wbits = -((data_in[0] >> 4) + 8);
		workspace->strm.next_in += 2;
		workspace->strm.avail_in -= 2;
	}

	ret = zlib_inflateInit2(&workspace->strm, wbits);
	if (unlikely(ret != Z_OK)) {
		struct btrfs_inode *inode = folio_to_inode(dest_folio);

		btrfs_err(inode->root->fs_info,
		"zlib decompression init failed, error %d root %llu inode %llu offset %llu",
			  ret, btrfs_root_id(inode->root), btrfs_ino(inode),
			  folio_pos(dest_folio));
		return -EIO;
	}

	/*
	 * Everything (in/out buf) should be at most one sector, there should
	 * be no need to switch any input/output buffer.
	 */
	ret = zlib_inflate(&workspace->strm, Z_FINISH);
	to_copy = min(workspace->strm.total_out, destlen);
	if (ret != Z_STREAM_END)
		goto out;

	memcpy_to_folio(dest_folio, dest_pgoff, workspace->buf, to_copy);

out:
	if (unlikely(to_copy != destlen)) {
		struct btrfs_inode *inode = folio_to_inode(dest_folio);

		btrfs_err(inode->root->fs_info,
"zlib decompression failed, error %d root %llu inode %llu offset %llu decompressed %lu expected %zu",
			  ret, btrfs_root_id(inode->root), btrfs_ino(inode),
			  folio_pos(dest_folio), to_copy, destlen);
		ret = -EIO;
	} else {
		ret = 0;
	}

	zlib_inflateEnd(&workspace->strm);

	if (unlikely(to_copy < destlen))
		folio_zero_range(dest_folio, dest_pgoff + to_copy, destlen - to_copy);
	return ret;
}

const struct btrfs_compress_op btrfs_zlib_compress = {
	.workspace_manager	= &wsm,
	.min_level		= 1,
	.max_level		= 9,
	.default_level		= BTRFS_ZLIB_DEFAULT_LEVEL,
};
