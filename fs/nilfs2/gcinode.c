/*
 * gcinode.c - dummy inodes to buffer blocks for garbage collection
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Seiji Kihara <kihara@osrg.net>, Amagai Yoshiji <amagai@osrg.net>,
 *            and Ryusuke Konishi <ryusuke@osrg.net>.
 * Revised by Ryusuke Konishi <ryusuke@osrg.net>.
 *
 */
/*
 * This file adds the cache of on-disk blocks to be moved in garbage
 * collection.  The disk blocks are held with dummy inodes (called
 * gcinodes), and this file provides lookup function of the dummy
 * inodes and their buffer read function.
 *
 * Buffers and pages held by the dummy inodes will be released each
 * time after they are copied to a new log.  Dirty blocks made on the
 * current generation and the blocks to be moved by GC never overlap
 * because the dirty blocks make a new generation; they rather must be
 * written individually.
 */

#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include "nilfs.h"
#include "btree.h"
#include "btnode.h"
#include "page.h"
#include "mdt.h"
#include "dat.h"
#include "ifile.h"

/*
 * nilfs_gccache_submit_read_data() - add data buffer and submit read request
 * @inode - gc inode
 * @blkoff - dummy offset treated as the key for the page cache
 * @pbn - physical block number of the block
 * @vbn - virtual block number of the block, 0 for non-virtual block
 * @out_bh - indirect pointer to a buffer_head struct to receive the results
 *
 * Description: nilfs_gccache_submit_read_data() registers the data buffer
 * specified by @pbn to the GC pagecache with the key @blkoff.
 * This function sets @vbn (@pbn if @vbn is zero) in b_blocknr of the buffer.
 *
 * Return Value: On success, 0 is returned. On Error, one of the following
 * negative error code is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - The block specified with @pbn does not exist.
 */
int nilfs_gccache_submit_read_data(struct inode *inode, sector_t blkoff,
				   sector_t pbn, __u64 vbn,
				   struct buffer_head **out_bh)
{
	struct buffer_head *bh;
	int err;

	bh = nilfs_grab_buffer(inode, inode->i_mapping, blkoff, 0);
	if (unlikely(!bh))
		return -ENOMEM;

	if (buffer_uptodate(bh))
		goto out;

	if (pbn == 0) {
		struct the_nilfs *nilfs = inode->i_sb->s_fs_info;

		err = nilfs_dat_translate(nilfs->ns_dat, vbn, &pbn);
		if (unlikely(err)) { /* -EIO, -ENOMEM, -ENOENT */
			brelse(bh);
			goto failed;
		}
	}

	lock_buffer(bh);
	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		goto out;
	}

	if (!buffer_mapped(bh)) {
		bh->b_bdev = inode->i_sb->s_bdev;
		set_buffer_mapped(bh);
	}
	bh->b_blocknr = pbn;
	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(READ, bh);
	if (vbn)
		bh->b_blocknr = vbn;
 out:
	err = 0;
	*out_bh = bh;

 failed:
	unlock_page(bh->b_page);
	page_cache_release(bh->b_page);
	return err;
}

/*
 * nilfs_gccache_submit_read_node() - add node buffer and submit read request
 * @inode - gc inode
 * @pbn - physical block number for the block
 * @vbn - virtual block number for the block
 * @out_bh - indirect pointer to a buffer_head struct to receive the results
 *
 * Description: nilfs_gccache_submit_read_node() registers the node buffer
 * specified by @vbn to the GC pagecache.  @pbn can be supplied by the
 * caller to avoid translation of the disk block address.
 *
 * Return Value: On success, 0 is returned. On Error, one of the following
 * negative error code is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_gccache_submit_read_node(struct inode *inode, sector_t pbn,
				   __u64 vbn, struct buffer_head **out_bh)
{
	int ret;

	ret = nilfs_btnode_submit_block(&NILFS_I(inode)->i_btnode_cache,
					vbn ? : pbn, pbn, READ, out_bh, &pbn);
	if (ret == -EEXIST) /* internal code (cache hit) */
		ret = 0;
	return ret;
}

int nilfs_gccache_wait_and_mark_dirty(struct buffer_head *bh)
{
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh))
		return -EIO;
	if (buffer_dirty(bh))
		return -EEXIST;

	if (buffer_nilfs_node(bh) && nilfs_btree_broken_node_block(bh)) {
		clear_buffer_uptodate(bh);
		return -EIO;
	}
	mark_buffer_dirty(bh);
	return 0;
}

int nilfs_init_gcinode(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);

	inode->i_mode = S_IFREG;
	mapping_set_gfp_mask(inode->i_mapping, GFP_NOFS);
	inode->i_mapping->a_ops = &empty_aops;
	inode->i_mapping->backing_dev_info = inode->i_sb->s_bdi;

	ii->i_flags = 0;
	nilfs_bmap_init_gc(ii->i_bmap);

	return 0;
}

/**
 * nilfs_remove_all_gcinodes() - remove all unprocessed gc inodes
 */
void nilfs_remove_all_gcinodes(struct the_nilfs *nilfs)
{
	struct list_head *head = &nilfs->ns_gc_inodes;
	struct nilfs_inode_info *ii;

	while (!list_empty(head)) {
		ii = list_first_entry(head, struct nilfs_inode_info, i_dirty);
		list_del_init(&ii->i_dirty);
		iput(&ii->vfs_inode);
	}
}
