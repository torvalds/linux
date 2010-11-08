/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/lzo.h>
#include "compression.h"

#define LZO_LEN	4

struct workspace {
	void *mem;
	void *buf;	/* where compressed data goes */
	void *cbuf;	/* where decompressed data goes */
	struct list_head list;
};

static void lzo_free_workspace(struct list_head *ws)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);

	vfree(workspace->buf);
	vfree(workspace->cbuf);
	vfree(workspace->mem);
	kfree(workspace);
}

static struct list_head *lzo_alloc_workspace(void)
{
	struct workspace *workspace;

	workspace = kzalloc(sizeof(*workspace), GFP_NOFS);
	if (!workspace)
		return ERR_PTR(-ENOMEM);

	workspace->mem = vmalloc(LZO1X_MEM_COMPRESS);
	workspace->buf = vmalloc(lzo1x_worst_compress(PAGE_CACHE_SIZE));
	workspace->cbuf = vmalloc(lzo1x_worst_compress(PAGE_CACHE_SIZE));
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

static inline size_t read_compress_length(char *buf)
{
	__le32 dlen;

	memcpy(&dlen, buf, LZO_LEN);
	return le32_to_cpu(dlen);
}

static int lzo_compress_pages(struct list_head *ws,
			      struct address_space *mapping,
			      u64 start, unsigned long len,
			      struct page **pages,
			      unsigned long nr_dest_pages,
			      unsigned long *out_pages,
			      unsigned long *total_in,
			      unsigned long *total_out,
			      unsigned long max_out)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	int ret = 0;
	char *data_in;
	char *cpage_out;
	int nr_pages = 0;
	struct page *in_page = NULL;
	struct page *out_page = NULL;
	unsigned long bytes_left;

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

	in_page = find_get_page(mapping, start >> PAGE_CACHE_SHIFT);
	data_in = kmap(in_page);

	/*
	 * store the size of all chunks of compressed data in
	 * the first 4 bytes
	 */
	out_page = alloc_page(GFP_NOFS | __GFP_HIGHMEM);
	if (out_page == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	cpage_out = kmap(out_page);
	out_offset = LZO_LEN;
	tot_out = LZO_LEN;
	pages[0] = out_page;
	nr_pages = 1;
	pg_bytes_left = PAGE_CACHE_SIZE - LZO_LEN;

	/* compress at most one page of data each time */
	in_len = min(len, PAGE_CACHE_SIZE);
	while (tot_in < len) {
		ret = lzo1x_1_compress(data_in, in_len, workspace->cbuf,
				       &out_len, workspace->mem);
		if (ret != LZO_E_OK) {
			printk(KERN_DEBUG "btrfs deflate in loop returned %d\n",
			       ret);
			ret = -1;
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
					ret = -1;
					goto out;
				}

				out_page = alloc_page(GFP_NOFS | __GFP_HIGHMEM);
				if (out_page == NULL) {
					ret = -ENOMEM;
					goto out;
				}
				cpage_out = kmap(out_page);
				pages[nr_pages++] = out_page;

				pg_bytes_left = PAGE_CACHE_SIZE;
				out_offset = 0;
			}
		}

		/* we're making it bigger, give up */
		if (tot_in > 8192 && tot_in < tot_out)
			goto out;

		/* we're all done */
		if (tot_in >= len)
			break;

		if (tot_out > max_out)
			break;

		bytes_left = len - tot_in;
		kunmap(in_page);
		page_cache_release(in_page);

		start += PAGE_CACHE_SIZE;
		in_page = find_get_page(mapping, start >> PAGE_CACHE_SHIFT);
		data_in = kmap(in_page);
		in_len = min(bytes_left, PAGE_CACHE_SIZE);
	}

	if (tot_out > tot_in)
		goto out;

	/* store the size of all chunks of compressed data */
	cpage_out = kmap(pages[0]);
	write_compress_length(cpage_out, tot_out);

	kunmap(pages[0]);

	ret = 0;
	*total_out = tot_out;
	*total_in = tot_in;
out:
	*out_pages = nr_pages;
	if (out_page)
		kunmap(out_page);

	if (in_page) {
		kunmap(in_page);
		page_cache_release(in_page);
	}

	return ret;
}

static int lzo_decompress_biovec(struct list_head *ws,
				 struct page **pages_in,
				 u64 disk_start,
				 struct bio_vec *bvec,
				 int vcnt,
				 size_t srclen)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	int ret = 0, ret2;
	char *data_in;
	unsigned long page_in_index = 0;
	unsigned long page_out_index = 0;
	unsigned long total_pages_in = (srclen + PAGE_CACHE_SIZE - 1) /
					PAGE_CACHE_SIZE;
	unsigned long buf_start;
	unsigned long buf_offset = 0;
	unsigned long bytes;
	unsigned long working_bytes;
	unsigned long pg_offset;

	size_t in_len;
	size_t out_len;
	unsigned long in_offset;
	unsigned long in_page_bytes_left;
	unsigned long tot_in;
	unsigned long tot_out;
	unsigned long tot_len;
	char *buf;

	data_in = kmap(pages_in[0]);
	tot_len = read_compress_length(data_in);

	tot_in = LZO_LEN;
	in_offset = LZO_LEN;
	tot_len = min_t(size_t, srclen, tot_len);
	in_page_bytes_left = PAGE_CACHE_SIZE - LZO_LEN;

	tot_out = 0;
	pg_offset = 0;

	while (tot_in < tot_len) {
		in_len = read_compress_length(data_in + in_offset);
		in_page_bytes_left -= LZO_LEN;
		in_offset += LZO_LEN;
		tot_in += LZO_LEN;

		tot_in += in_len;
		working_bytes = in_len;

		/* fast path: avoid using the working buffer */
		if (in_page_bytes_left >= in_len) {
			buf = data_in + in_offset;
			bytes = in_len;
			goto cont;
		}

		/* copy bytes from the pages into the working buffer */
		buf = workspace->cbuf;
		buf_offset = 0;
		while (working_bytes) {
			bytes = min(working_bytes, in_page_bytes_left);

			memcpy(buf + buf_offset, data_in + in_offset, bytes);
			buf_offset += bytes;
cont:
			working_bytes -= bytes;
			in_page_bytes_left -= bytes;
			in_offset += bytes;

			/* check if we need to pick another page */
			if ((working_bytes == 0 && in_page_bytes_left < LZO_LEN)
			    || in_page_bytes_left == 0) {
				tot_in += in_page_bytes_left;

				if (working_bytes == 0 && tot_in >= tot_len)
					break;

				kunmap(pages_in[page_in_index]);
				page_in_index++;
				if (page_in_index >= total_pages_in) {
					ret = -1;
					data_in = NULL;
					goto done;
				}
				data_in = kmap(pages_in[page_in_index]);

				in_page_bytes_left = PAGE_CACHE_SIZE;
				in_offset = 0;
			}
		}

		out_len = lzo1x_worst_compress(PAGE_CACHE_SIZE);
		ret = lzo1x_decompress_safe(buf, in_len, workspace->buf,
					    &out_len);
		if (ret != LZO_E_OK) {
			printk(KERN_WARNING "btrfs decompress failed\n");
			ret = -1;
			break;
		}

		buf_start = tot_out;
		tot_out += out_len;

		ret2 = btrfs_decompress_buf2page(workspace->buf, buf_start,
						 tot_out, disk_start,
						 bvec, vcnt,
						 &page_out_index, &pg_offset);
		if (ret2 == 0)
			break;
	}
done:
	if (data_in)
		kunmap(pages_in[page_in_index]);
	return ret;
}

static int lzo_decompress(struct list_head *ws, unsigned char *data_in,
			  struct page *dest_page,
			  unsigned long start_byte,
			  size_t srclen, size_t destlen)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	size_t in_len;
	size_t out_len;
	size_t tot_len;
	int ret = 0;
	char *kaddr;
	unsigned long bytes;

	BUG_ON(srclen < LZO_LEN);

	tot_len = read_compress_length(data_in);
	data_in += LZO_LEN;

	in_len = read_compress_length(data_in);
	data_in += LZO_LEN;

	out_len = PAGE_CACHE_SIZE;
	ret = lzo1x_decompress_safe(data_in, in_len, workspace->buf, &out_len);
	if (ret != LZO_E_OK) {
		printk(KERN_WARNING "btrfs decompress failed!\n");
		ret = -1;
		goto out;
	}

	if (out_len < start_byte) {
		ret = -1;
		goto out;
	}

	bytes = min_t(unsigned long, destlen, out_len - start_byte);

	kaddr = kmap_atomic(dest_page, KM_USER0);
	memcpy(kaddr, workspace->buf + start_byte, bytes);
	kunmap_atomic(kaddr, KM_USER0);
out:
	return ret;
}

struct btrfs_compress_op btrfs_lzo_compress = {
	.alloc_workspace	= lzo_alloc_workspace,
	.free_workspace		= lzo_free_workspace,
	.compress_pages		= lzo_compress_pages,
	.decompress_biovec	= lzo_decompress_biovec,
	.decompress		= lzo_decompress,
};
