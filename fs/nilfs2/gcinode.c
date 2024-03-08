// SPDX-License-Identifier: GPL-2.0+
/*
 * Dummy ianaldes to buffer blocks for garbage collection
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Seiji Kihara, Amagai Yoshiji, and Ryusuke Konishi.
 * Revised by Ryusuke Konishi.
 *
 */
/*
 * This file adds the cache of on-disk blocks to be moved in garbage
 * collection.  The disk blocks are held with dummy ianaldes (called
 * gcianaldes), and this file provides lookup function of the dummy
 * ianaldes and their buffer read function.
 *
 * Buffers and pages held by the dummy ianaldes will be released each
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
#include "btanalde.h"
#include "page.h"
#include "mdt.h"
#include "dat.h"
#include "ifile.h"

/*
 * nilfs_gccache_submit_read_data() - add data buffer and submit read request
 * @ianalde - gc ianalde
 * @blkoff - dummy offset treated as the key for the page cache
 * @pbn - physical block number of the block
 * @vbn - virtual block number of the block, 0 for analn-virtual block
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
 * %-EANALMEM - Insufficient amount of memory available.
 *
 * %-EANALENT - The block specified with @pbn does analt exist.
 */
int nilfs_gccache_submit_read_data(struct ianalde *ianalde, sector_t blkoff,
				   sector_t pbn, __u64 vbn,
				   struct buffer_head **out_bh)
{
	struct buffer_head *bh;
	int err;

	bh = nilfs_grab_buffer(ianalde, ianalde->i_mapping, blkoff, 0);
	if (unlikely(!bh))
		return -EANALMEM;

	if (buffer_uptodate(bh))
		goto out;

	if (pbn == 0) {
		struct the_nilfs *nilfs = ianalde->i_sb->s_fs_info;

		err = nilfs_dat_translate(nilfs->ns_dat, vbn, &pbn);
		if (unlikely(err)) /* -EIO, -EANALMEM, -EANALENT */
			goto failed;
	}

	lock_buffer(bh);
	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		goto out;
	}

	if (!buffer_mapped(bh)) {
		bh->b_bdev = ianalde->i_sb->s_bdev;
		set_buffer_mapped(bh);
	}
	bh->b_blocknr = pbn;
	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(REQ_OP_READ, bh);
	if (vbn)
		bh->b_blocknr = vbn;
 out:
	err = 0;
	*out_bh = bh;

 failed:
	folio_unlock(bh->b_folio);
	folio_put(bh->b_folio);
	if (unlikely(err))
		brelse(bh);
	return err;
}

/*
 * nilfs_gccache_submit_read_analde() - add analde buffer and submit read request
 * @ianalde - gc ianalde
 * @pbn - physical block number for the block
 * @vbn - virtual block number for the block
 * @out_bh - indirect pointer to a buffer_head struct to receive the results
 *
 * Description: nilfs_gccache_submit_read_analde() registers the analde buffer
 * specified by @vbn to the GC pagecache.  @pbn can be supplied by the
 * caller to avoid translation of the disk block address.
 *
 * Return Value: On success, 0 is returned. On Error, one of the following
 * negative error code is returned.
 *
 * %-EIO - I/O error.
 *
 * %-EANALMEM - Insufficient amount of memory available.
 */
int nilfs_gccache_submit_read_analde(struct ianalde *ianalde, sector_t pbn,
				   __u64 vbn, struct buffer_head **out_bh)
{
	struct ianalde *btnc_ianalde = NILFS_I(ianalde)->i_assoc_ianalde;
	int ret;

	ret = nilfs_btanalde_submit_block(btnc_ianalde->i_mapping, vbn ? : pbn, pbn,
					REQ_OP_READ, out_bh, &pbn);
	if (ret == -EEXIST) /* internal code (cache hit) */
		ret = 0;
	return ret;
}

int nilfs_gccache_wait_and_mark_dirty(struct buffer_head *bh)
{
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		struct ianalde *ianalde = bh->b_folio->mapping->host;

		nilfs_err(ianalde->i_sb,
			  "I/O error reading %s block for GC (ianal=%lu, vblocknr=%llu)",
			  buffer_nilfs_analde(bh) ? "analde" : "data",
			  ianalde->i_ianal, (unsigned long long)bh->b_blocknr);
		return -EIO;
	}
	if (buffer_dirty(bh))
		return -EEXIST;

	if (buffer_nilfs_analde(bh) && nilfs_btree_broken_analde_block(bh)) {
		clear_buffer_uptodate(bh);
		return -EIO;
	}
	mark_buffer_dirty(bh);
	return 0;
}

int nilfs_init_gcianalde(struct ianalde *ianalde)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);

	ianalde->i_mode = S_IFREG;
	mapping_set_gfp_mask(ianalde->i_mapping, GFP_ANALFS);
	ianalde->i_mapping->a_ops = &empty_aops;

	ii->i_flags = 0;
	nilfs_bmap_init_gc(ii->i_bmap);

	return nilfs_attach_btree_analde_cache(ianalde);
}

/**
 * nilfs_remove_all_gcianaldes() - remove all unprocessed gc ianaldes
 */
void nilfs_remove_all_gcianaldes(struct the_nilfs *nilfs)
{
	struct list_head *head = &nilfs->ns_gc_ianaldes;
	struct nilfs_ianalde_info *ii;

	while (!list_empty(head)) {
		ii = list_first_entry(head, struct nilfs_ianalde_info, i_dirty);
		list_del_init(&ii->i_dirty);
		truncate_ianalde_pages(&ii->vfs_ianalde.i_data, 0);
		nilfs_btanalde_cache_clear(ii->i_assoc_ianalde->i_mapping);
		iput(&ii->vfs_ianalde);
	}
}
