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

/* Read separately compressed datablock and memcopy into page cache */
int squashfs_readpage_block(struct page *page, u64 block, int bsize, int expected)
{
	struct inode *i = page->mapping->host;
	struct squashfs_cache_entry *buffer = squashfs_get_datablock(i->i_sb,
		block, bsize);
	int res = buffer->error;

	if (res)
		ERROR("Unable to read page, block %llx, size %x\n", block,
			bsize);
	else
		squashfs_copy_cache(page, buffer, expected, 0);

	squashfs_cache_put(buffer);
	return res;
}
