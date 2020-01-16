// SPDX-License-Identifier: GPL-2.0+
/*
 * gciyesde.c - dummy iyesdes to buffer blocks for garbage collection
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Seiji Kihara, Amagai Yoshiji, and Ryusuke Konishi.
 * Revised by Ryusuke Konishi.
 *
 */
/*
 * This file adds the cache of on-disk blocks to be moved in garbage
 * collection.  The disk blocks are held with dummy iyesdes (called
 * gciyesdes), and this file provides lookup function of the dummy
 * iyesdes and their buffer read function.
 *
 * Buffers and pages held by the dummy iyesdes will be released each
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
#include "btyesde.h"
#include "page.h"
#include "mdt.h"
#include "dat.h"
#include "ifile.h"

/*
 * nilfs_gccache_submit_read_data() - add data buffer and submit read request
 * @iyesde - gc iyesde
 * @blkoff - dummy offset treated as the key for the page cache
 * @pbn - physical block number of the block
 * @vbn - virtual block number of the block, 0 for yesn-virtual block
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
 * %-ENOENT - The block specified with @pbn does yest exist.
 */
int nilfs_gccache_submit_read_data(struct iyesde *iyesde, sector_t blkoff,
				   sector_t pbn, __u64 vbn,
				   struct buffer_head **out_bh)
{
	struct buffer_head *bh;
	int err;

	bh = nilfs_grab_buffer(iyesde, iyesde->i_mapping, blkoff, 0);
	if (unlikely(!bh))
		return -ENOMEM;

	if (buffer_uptodate(bh))
		goto out;

	if (pbn == 0) {
		struct the_nilfs *nilfs = iyesde->i_sb->s_fs_info;

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
		bh->b_bdev = iyesde->i_sb->s_bdev;
		set_buffer_mapped(bh);
	}
	bh->b_blocknr = pbn;
	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(REQ_OP_READ, 0, bh);
	if (vbn)
		bh->b_blocknr = vbn;
 out:
	err = 0;
	*out_bh = bh;

 failed:
	unlock_page(bh->b_page);
	put_page(bh->b_page);
	return err;
}

/*
 * nilfs_gccache_submit_read_yesde() - add yesde buffer and submit read request
 * @iyesde - gc iyesde
 * @pbn - physical block number for the block
 * @vbn - virtual block number for the block
 * @out_bh - indirect pointer to a buffer_head struct to receive the results
 *
 * Description: nilfs_gccache_submit_read_yesde() registers the yesde buffer
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
int nilfs_gccache_submit_read_yesde(struct iyesde *iyesde, sector_t pbn,
				   __u64 vbn, struct buffer_head **out_bh)
{
	int ret;

	ret = nilfs_btyesde_submit_block(&NILFS_I(iyesde)->i_btyesde_cache,
					vbn ? : pbn, pbn, REQ_OP_READ, 0,
					out_bh, &pbn);
	if (ret == -EEXIST) /* internal code (cache hit) */
		ret = 0;
	return ret;
}

int nilfs_gccache_wait_and_mark_dirty(struct buffer_head *bh)
{
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		struct iyesde *iyesde = bh->b_page->mapping->host;

		nilfs_msg(iyesde->i_sb, KERN_ERR,
			  "I/O error reading %s block for GC (iyes=%lu, vblocknr=%llu)",
			  buffer_nilfs_yesde(bh) ? "yesde" : "data",
			  iyesde->i_iyes, (unsigned long long)bh->b_blocknr);
		return -EIO;
	}
	if (buffer_dirty(bh))
		return -EEXIST;

	if (buffer_nilfs_yesde(bh) && nilfs_btree_broken_yesde_block(bh)) {
		clear_buffer_uptodate(bh);
		return -EIO;
	}
	mark_buffer_dirty(bh);
	return 0;
}

int nilfs_init_gciyesde(struct iyesde *iyesde)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);

	iyesde->i_mode = S_IFREG;
	mapping_set_gfp_mask(iyesde->i_mapping, GFP_NOFS);
	iyesde->i_mapping->a_ops = &empty_aops;

	ii->i_flags = 0;
	nilfs_bmap_init_gc(ii->i_bmap);

	return 0;
}

/**
 * nilfs_remove_all_gciyesdes() - remove all unprocessed gc iyesdes
 */
void nilfs_remove_all_gciyesdes(struct the_nilfs *nilfs)
{
	struct list_head *head = &nilfs->ns_gc_iyesdes;
	struct nilfs_iyesde_info *ii;

	while (!list_empty(head)) {
		ii = list_first_entry(head, struct nilfs_iyesde_info, i_dirty);
		list_del_init(&ii->i_dirty);
		truncate_iyesde_pages(&ii->vfs_iyesde.i_data, 0);
		nilfs_btyesde_cache_clear(&ii->i_btyesde_cache);
		iput(&ii->vfs_iyesde);
	}
}
