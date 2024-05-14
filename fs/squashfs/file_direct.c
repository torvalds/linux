// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/mutex.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"
#include "page_actor.h"

/* Read separately compressed datablock directly into page cache */
int squashfs_readpage_block(struct page *target_page, u64 block, int bsize,
	int expected)

{
	struct inode *inode = target_page->mapping->host;
	struct squashfs_sb_info *msblk = inode->i_sb->s_fs_info;

	int file_end = (i_size_read(inode) - 1) >> PAGE_SHIFT;
	int mask = (1 << (msblk->block_log - PAGE_SHIFT)) - 1;
	int start_index = target_page->index & ~mask;
	int end_index = start_index | mask;
	int i, n, pages, bytes, res = -ENOMEM;
	struct page **page;
	struct squashfs_page_actor *actor;
	void *pageaddr;

	if (end_index > file_end)
		end_index = file_end;

	pages = end_index - start_index + 1;

	page = kmalloc_array(pages, sizeof(void *), GFP_KERNEL);
	if (page == NULL)
		return res;

	/* Try to grab all the pages covered by the Squashfs block */
	for (i = 0, n = start_index; n <= end_index; n++) {
		page[i] = (n == target_page->index) ? target_page :
			grab_cache_page_nowait(target_page->mapping, n);

		if (page[i] == NULL)
			continue;

		if (PageUptodate(page[i])) {
			unlock_page(page[i]);
			put_page(page[i]);
			continue;
		}

		i++;
	}

	pages = i;

	/*
	 * Create a "page actor" which will kmap and kunmap the
	 * page cache pages appropriately within the decompressor
	 */
	actor = squashfs_page_actor_init_special(msblk, page, pages, expected);
	if (actor == NULL)
		goto out;

	/* Decompress directly into the page cache buffers */
	res = squashfs_read_data(inode->i_sb, block, bsize, NULL, actor);

	squashfs_page_actor_free(actor);

	if (res < 0)
		goto mark_errored;

	if (res != expected) {
		res = -EIO;
		goto mark_errored;
	}

	/* Last page (if present) may have trailing bytes not filled */
	bytes = res % PAGE_SIZE;
	if (page[pages - 1]->index == end_index && bytes) {
		pageaddr = kmap_local_page(page[pages - 1]);
		memset(pageaddr + bytes, 0, PAGE_SIZE - bytes);
		kunmap_local(pageaddr);
	}

	/* Mark pages as uptodate, unlock and release */
	for (i = 0; i < pages; i++) {
		flush_dcache_page(page[i]);
		SetPageUptodate(page[i]);
		unlock_page(page[i]);
		if (page[i] != target_page)
			put_page(page[i]);
	}

	kfree(page);

	return 0;

mark_errored:
	/* Decompression failed, mark pages as errored.  Target_page is
	 * dealt with by the caller
	 */
	for (i = 0; i < pages; i++) {
		if (page[i] == NULL || page[i] == target_page)
			continue;
		flush_dcache_page(page[i]);
		SetPageError(page[i]);
		unlock_page(page[i]);
		put_page(page[i]);
	}

out:
	kfree(page);
	return res;
}
