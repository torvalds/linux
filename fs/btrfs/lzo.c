// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/lzo.h>
#include <linux/refcount.h>
#include "compression.h"
#include "ctree.h"

#define LZO_LEN	4

/*
 * Btrfs LZO compression format
 *
 * Regular and inlined LZO compressed data extents consist of:
 *
 * 1.  Header
 *     Fixed size. LZO_LEN (4) bytes long, LE32.
 *     Records the total size (including the header) of compressed data.
 *
 * 2.  Segment(s)
 *     Variable size. Each segment includes one segment header, followed by data
 *     payload.
 *     One regular LZO compressed extent can have one or more segments.
 *     For inlined LZO compressed extent, only one segment is allowed.
 *     One segment represents at most one page of uncompressed data.
 *
 * 2.1 Segment header
 *     Fixed size. LZO_LEN (4) bytes long, LE32.
 *     Records the total size of the segment (not including the header).
 *     Segment header never crosses page boundary, thus it's possible to
 *     have at most 3 padding zeros at the end of the page.
 *
 * 2.2 Data Payload
 *     Variable size. Size up limit should be lzo1x_worst_compress(PAGE_SIZE)
 *     which is 4419 for a 4KiB page.
 *
 * Example:
 * Page 1:
 *          0     0x2   0x4   0x6   0x8   0xa   0xc   0xe     0x10
 * 0x0000   |  Header   | SegHdr 01 | Data payload 01 ...     |
 * ...
 * 0x0ff0   | SegHdr  N | Data payload  N     ...          |00|
 *                                                          ^^ padding zeros
 * Page 2:
 * 0x1000   | SegHdr N+1| Data payload N+1 ...                |
 */

struct workspace {
	void *mem;
	void *buf;	/* where decompressed data goes */
	void *cbuf;	/* where compressed data goes */
	struct list_head list;
};

static struct workspace_manager wsm;

void lzo_free_workspace(struct list_head *ws)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);

	kvfree(workspace->buf);
	kvfree(workspace->cbuf);
	kvfree(workspace->mem);
	kfree(workspace);
}

struct list_head *lzo_alloc_workspace(unsigned int level)
{
	struct workspace *workspace;

	workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return ERR_PTR(-ENOMEM);

	workspace->mem = kvmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	workspace->buf = kvmalloc(lzo1x_worst_compress(PAGE_SIZE), GFP_KERNEL);
	workspace->cbuf = kvmalloc(lzo1x_worst_compress(PAGE_SIZE), GFP_KERNEL);
	if (!workspace->mem || !workspace->buf || !workspace->cbuf)
		goto fail;

	INIT_LIST_HEAD(&workspace->list);

	return &workspace->list;
fail:
	lzo_free_workspace(&workspace->list);
	return ERR_PTR(-ENOMEM);
}

static inline void write_compress_length(char *buf, size_t len)
{
	__le32 dlen;

	dlen = cpu_to_le32(len);
	memcpy(buf, &dlen, LZO_LEN);
}

static inline size_t read_compress_length(const char *buf)
{
	__le32 dlen;

	memcpy(&dlen, buf, LZO_LEN);
	return le32_to_cpu(dlen);
}

int lzo_compress_pages(struct list_head *ws, struct address_space *mapping,
		u64 start, struct page **pages, unsigned long *out_pages,
		unsigned long *total_in, unsigned long *total_out)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	int ret = 0;
	char *data_in;
	char *cpage_out, *sizes_ptr;
	int nr_pages = 0;
	struct page *in_page = NULL;
	struct page *out_page = NULL;
	unsigned long bytes_left;
	unsigned long len = *total_out;
	unsigned long nr_dest_pages = *out_pages;
	const unsigned long max_out = nr_dest_pages * PAGE_SIZE;
	size_t in_len;
	size_t out_len;
	char *buf;
	unsigned long tot_in = 0;
	unsigned long tot_out = 0;
	unsigned long pg_bytes_left;
	unsigned long out_offset;
	unsigned long bytes;

	*out_pages = 0;
	*total_out = 0;
	*total_in = 0;

	in_page = find_get_page(mapping, start >> PAGE_SHIFT);
	data_in = kmap(in_page);

	/*
	 * store the size of all chunks of compressed data in
	 * the first 4 bytes
	 */
	out_page = alloc_page(GFP_NOFS);
	if (out_page == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	cpage_out = kmap(out_page);
	out_offset = LZO_LEN;
	tot_out = LZO_LEN;
	pages[0] = out_page;
	nr_pages = 1;
	pg_bytes_left = PAGE_SIZE - LZO_LEN;

	/* compress at most one page of data each time */
	in_len = min(len, PAGE_SIZE);
	while (tot_in < len) {
		ret = lzo1x_1_compress(data_in, in_len, workspace->cbuf,
				       &out_len, workspace->mem);
		if (ret != LZO_E_OK) {
			pr_debug("BTRFS: lzo in loop returned %d\n",
			       ret);
			ret = -EIO;
			goto out;
		}

		/* store the size of this chunk of compressed data */
		write_compress_length(cpage_out + out_offset, out_len);
		tot_out += LZO_LEN;
		out_offset += LZO_LEN;
		pg_bytes_left -= LZO_LEN;

		tot_in += in_len;
		tot_out += out_len;

		/* copy bytes from the working buffer into the pages */
		buf = workspace->cbuf;
		while (out_len) {
			bytes = min_t(unsigned long, pg_bytes_left, out_len);

			memcpy(cpage_out + out_offset, buf, bytes);

			out_len -= bytes;
			pg_bytes_left -= bytes;
			buf += bytes;
			out_offset += bytes;

			/*
			 * we need another page for writing out.
			 *
			 * Note if there's less than 4 bytes left, we just
			 * skip to a new page.
			 */
			if ((out_len == 0 && pg_bytes_left < LZO_LEN) ||
			    pg_bytes_left == 0) {
				if (pg_bytes_left) {
					memset(cpage_out + out_offset, 0,
					       pg_bytes_left);
					tot_out += pg_bytes_left;
				}

				/* we're done, don't allocate new page */
				if (out_len == 0 && tot_in >= len)
					break;

				kunmap(out_page);
				if (nr_pages == nr_dest_pages) {
					out_page = NULL;
					ret = -E2BIG;
					goto out;
				}

				out_page = alloc_page(GFP_NOFS);
				if (out_page == NULL) {
					ret = -ENOMEM;
					goto out;
				}
				cpage_out = kmap(out_page);
				pages[nr_pages++] = out_page;

				pg_bytes_left = PAGE_SIZE;
				out_offset = 0;
			}
		}

		/* we're making it bigger, give up */
		if (tot_in > 8192 && tot_in < tot_out) {
			ret = -E2BIG;
			goto out;
		}

		/* we're all done */
		if (tot_in >= len)
			break;

		if (tot_out > max_out)
			break;

		bytes_left = len - tot_in;
		kunmap(in_page);
		put_page(in_page);

		start += PAGE_SIZE;
		in_page = find_get_page(mapping, start >> PAGE_SHIFT);
		data_in = kmap(in_page);
		in_len = min(bytes_left, PAGE_SIZE);
	}

	if (tot_out >= tot_in) {
		ret = -E2BIG;
		goto out;
	}

	/* store the size of all chunks of compressed data */
	sizes_ptr = kmap_local_page(pages[0]);
	write_compress_length(sizes_ptr, tot_out);
	kunmap_local(sizes_ptr);

	ret = 0;
	*total_out = tot_out;
	*total_in = tot_in;
out:
	*out_pages = nr_pages;
	if (out_page)
		kunmap(out_page);

	if (in_page) {
		kunmap(in_page);
		put_page(in_page);
	}

	return ret;
}

/*
 * Copy the compressed segment payload into @dest.
 *
 * For the payload there will be no padding, just need to do page switching.
 */
static void copy_compressed_segment(struct compressed_bio *cb,
				    char *dest, u32 len, u32 *cur_in)
{
	u32 orig_in = *cur_in;

	while (*cur_in < orig_in + len) {
		char *kaddr;
		struct page *cur_page;
		u32 copy_len = min_t(u32, PAGE_SIZE - offset_in_page(*cur_in),
					  orig_in + len - *cur_in);

		ASSERT(copy_len);
		cur_page = cb->compressed_pages[*cur_in / PAGE_SIZE];

		kaddr = kmap(cur_page);
		memcpy(dest + *cur_in - orig_in,
			kaddr + offset_in_page(*cur_in),
			copy_len);
		kunmap(cur_page);

		*cur_in += copy_len;
	}
}

int lzo_decompress_bio(struct list_head *ws, struct compressed_bio *cb)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	const struct btrfs_fs_info *fs_info = btrfs_sb(cb->inode->i_sb);
	const u32 sectorsize = fs_info->sectorsize;
	char *kaddr;
	int ret;
	/* Compressed data length, can be unaligned */
	u32 len_in;
	/* Offset inside the compressed data */
	u32 cur_in = 0;
	/* Bytes decompressed so far */
	u32 cur_out = 0;

	kaddr = kmap(cb->compressed_pages[0]);
	len_in = read_compress_length(kaddr);
	kunmap(cb->compressed_pages[0]);
	cur_in += LZO_LEN;

	/*
	 * LZO header length check
	 *
	 * The total length should not exceed the maximum extent length,
	 * and all sectors should be used.
	 * If this happens, it means the compressed extent is corrupted.
	 */
	if (len_in > min_t(size_t, BTRFS_MAX_COMPRESSED, cb->compressed_len) ||
	    round_up(len_in, sectorsize) < cb->compressed_len) {
		btrfs_err(fs_info,
			"invalid lzo header, lzo len %u compressed len %u",
			len_in, cb->compressed_len);
		return -EUCLEAN;
	}

	/* Go through each lzo segment */
	while (cur_in < len_in) {
		struct page *cur_page;
		/* Length of the compressed segment */
		u32 seg_len;
		u32 sector_bytes_left;
		size_t out_len = lzo1x_worst_compress(sectorsize);

		/*
		 * We should always have enough space for one segment header
		 * inside current sector.
		 */
		ASSERT(cur_in / sectorsize ==
		       (cur_in + LZO_LEN - 1) / sectorsize);
		cur_page = cb->compressed_pages[cur_in / PAGE_SIZE];
		kaddr = kmap(cur_page);
		ASSERT(cur_page);
		seg_len = read_compress_length(kaddr + offset_in_page(cur_in));
		cur_in += LZO_LEN;

		/* Copy the compressed segment payload into workspace */
		copy_compressed_segment(cb, workspace->cbuf, seg_len, &cur_in);

		/* Decompress the data */
		ret = lzo1x_decompress_safe(workspace->cbuf, seg_len,
					    workspace->buf, &out_len);
		if (ret != LZO_E_OK) {
			btrfs_err(fs_info, "failed to decompress");
			ret = -EIO;
			goto out;
		}

		/* Copy the data into inode pages */
		ret = btrfs_decompress_buf2page(workspace->buf, out_len, cb, cur_out);
		cur_out += out_len;

		/* All data read, exit */
		if (ret == 0)
			goto out;
		ret = 0;

		/* Check if the sector has enough space for a segment header */
		sector_bytes_left = sectorsize - (cur_in % sectorsize);
		if (sector_bytes_left >= LZO_LEN)
			continue;

		/* Skip the padding zeros */
		cur_in += sector_bytes_left;
	}
out:
	if (!ret)
		zero_fill_bio(cb->orig_bio);
	return ret;
}

int lzo_decompress(struct list_head *ws, unsigned char *data_in,
		struct page *dest_page, unsigned long start_byte, size_t srclen,
		size_t destlen)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	size_t in_len;
	size_t out_len;
	size_t max_segment_len = lzo1x_worst_compress(PAGE_SIZE);
	int ret = 0;
	char *kaddr;
	unsigned long bytes;

	if (srclen < LZO_LEN || srclen > max_segment_len + LZO_LEN * 2)
		return -EUCLEAN;

	in_len = read_compress_length(data_in);
	if (in_len != srclen)
		return -EUCLEAN;
	data_in += LZO_LEN;

	in_len = read_compress_length(data_in);
	if (in_len != srclen - LZO_LEN * 2) {
		ret = -EUCLEAN;
		goto out;
	}
	data_in += LZO_LEN;

	out_len = PAGE_SIZE;
	ret = lzo1x_decompress_safe(data_in, in_len, workspace->buf, &out_len);
	if (ret != LZO_E_OK) {
		pr_warn("BTRFS: decompress failed!\n");
		ret = -EIO;
		goto out;
	}

	if (out_len < start_byte) {
		ret = -EIO;
		goto out;
	}

	/*
	 * the caller is already checking against PAGE_SIZE, but lets
	 * move this check closer to the memcpy/memset
	 */
	destlen = min_t(unsigned long, destlen, PAGE_SIZE);
	bytes = min_t(unsigned long, destlen, out_len - start_byte);

	kaddr = kmap_local_page(dest_page);
	memcpy(kaddr, workspace->buf + start_byte, bytes);

	/*
	 * btrfs_getblock is doing a zero on the tail of the page too,
	 * but this will cover anything missing from the decompressed
	 * data.
	 */
	if (bytes < destlen)
		memset(kaddr+bytes, 0, destlen-bytes);
	kunmap_local(kaddr);
out:
	return ret;
}

const struct btrfs_compress_op btrfs_lzo_compress = {
	.workspace_manager	= &wsm,
	.max_level		= 1,
	.default_level		= 1,
};
