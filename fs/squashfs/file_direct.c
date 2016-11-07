/*
 * Copyright (c) 2013
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
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

static void release_actor_pages(struct page **page, int pages, int error)
{
	int i;

	for (i = 0; i < pages; i++) {
		if (!page[i])
			continue;
		flush_dcache_page(page[i]);
		if (!error)
			SetPageUptodate(page[i]);
		else {
			SetPageError(page[i]);
			zero_user_segment(page[i], 0, PAGE_CACHE_SIZE);
		}
		unlock_page(page[i]);
		put_page(page[i]);
	}
	kfree(page);
}

/*
 * Create a "page actor" which will kmap and kunmap the
 * page cache pages appropriately within the decompressor
 */
static struct squashfs_page_actor *actor_from_page_cache(
	struct page *target_page, int start_index, int nr_pages)
{
	int i, n;
	struct page **page;
	struct squashfs_page_actor *actor;

	page = kmalloc_array(nr_pages, sizeof(void *), GFP_KERNEL);
	if (!page)
		return NULL;

	/* Try to grab all the pages covered by the SquashFS block */
	for (i = 0, n = start_index; i < nr_pages; i++, n++) {
		if (target_page->index == n) {
			page[i] = target_page;
		} else {
			page[i] = grab_cache_page_nowait(target_page->mapping,
							 n);
			if (page[i] == NULL)
				continue;
		}

		if (PageUptodate(page[i])) {
			unlock_page(page[i]);
			put_page(page[i]);
			page[i] = NULL;
		}
	}

	actor = squashfs_page_actor_init(page, nr_pages, 0,
			release_actor_pages);
	if (!actor) {
		release_actor_pages(page, nr_pages, -ENOMEM);
		kfree(page);
		return NULL;
	}
	return actor;
}

/* Read separately compressed datablock directly into page cache */
int squashfs_readpage_block(struct page *target_page, u64 block, int bsize)

{
	struct inode *inode = target_page->mapping->host;
	struct squashfs_sb_info *msblk = inode->i_sb->s_fs_info;

	int file_end = (i_size_read(inode) - 1) >> PAGE_CACHE_SHIFT;
	int mask = (1 << (msblk->block_log - PAGE_CACHE_SHIFT)) - 1;
	int start_index = target_page->index & ~mask;
	int end_index = start_index | mask;
	int pages, res = -ENOMEM;
	struct squashfs_page_actor *actor;

	if (end_index > file_end)
		end_index = file_end;
	pages = end_index - start_index + 1;

	actor = actor_from_page_cache(target_page, start_index, pages);
	if (!actor)
		return -ENOMEM;

	get_page(target_page);
	res = squashfs_read_data_async(inode->i_sb, block, bsize, NULL,
				       actor);
	return res < 0 ? res : 0;
}
