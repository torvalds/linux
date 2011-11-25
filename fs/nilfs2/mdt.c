/*
 * mdt.c - meta data file for NILFS
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
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 */

#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include "nilfs.h"
#include "btnode.h"
#include "segment.h"
#include "page.h"
#include "mdt.h"


#define NILFS_MDT_MAX_RA_BLOCKS		(16 - 1)


static int
nilfs_mdt_insert_new_block(struct inode *inode, unsigned long block,
			   struct buffer_head *bh,
			   void (*init_block)(struct inode *,
					      struct buffer_head *, void *))
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	void *kaddr;
	int ret;

	/* Caller exclude read accesses using page lock */

	/* set_buffer_new(bh); */
	bh->b_blocknr = 0;

	ret = nilfs_bmap_insert(ii->i_bmap, block, (unsigned long)bh);
	if (unlikely(ret))
		return ret;

	set_buffer_mapped(bh);

	kaddr = kmap_atomic(bh->b_page);
	memset(kaddr + bh_offset(bh), 0, 1 << inode->i_blkbits);
	if (init_block)
		init_block(inode, bh, kaddr);
	flush_dcache_page(bh->b_page);
	kunmap_atomic(kaddr);

	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	nilfs_mdt_mark_dirty(inode);
	return 0;
}

static int nilfs_mdt_create_block(struct inode *inode, unsigned long block,
				  struct buffer_head **out_bh,
				  void (*init_block)(struct inode *,
						     struct buffer_head *,
						     void *))
{
	struct super_block *sb = inode->i_sb;
	struct nilfs_transaction_info ti;
	struct buffer_head *bh;
	int err;

	nilfs_transaction_begin(sb, &ti, 0);

	err = -ENOMEM;
	bh = nilfs_grab_buffer(inode, inode->i_mapping, block, 0);
	if (unlikely(!bh))
		goto failed_unlock;

	err = -EEXIST;
	if (buffer_uptodate(bh))
		goto failed_bh;

	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		goto failed_bh;

	bh->b_bdev = sb->s_bdev;
	err = nilfs_mdt_insert_new_block(inode, block, bh, init_block);
	if (likely(!err)) {
		get_bh(bh);
		*out_bh = bh;
	}

 failed_bh:
	unlock_page(bh->b_page);
	page_cache_release(bh->b_page);
	brelse(bh);

 failed_unlock:
	if (likely(!err))
		err = nilfs_transaction_commit(sb);
	else
		nilfs_transaction_abort(sb);

	return err;
}

static int
nilfs_mdt_submit_block(struct inode *inode, unsigned long blkoff,
		       int mode, struct buffer_head **out_bh)
{
	struct buffer_head *bh;
	__u64 blknum = 0;
	int ret = -ENOMEM;

	bh = nilfs_grab_buffer(inode, inode->i_mapping, blkoff, 0);
	if (unlikely(!bh))
		goto failed;

	ret = -EEXIST; /* internal code */
	if (buffer_uptodate(bh))
		goto out;

	if (mode == READA) {
		if (!trylock_buffer(bh)) {
			ret = -EBUSY;
			goto failed_bh;
		}
	} else /* mode == READ */
		lock_buffer(bh);

	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		goto out;
	}

	ret = nilfs_bmap_lookup(NILFS_I(inode)->i_bmap, blkoff, &blknum);
	if (unlikely(ret)) {
		unlock_buffer(bh);
		goto failed_bh;
	}
	map_bh(bh, inode->i_sb, (sector_t)blknum);

	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(mode, bh);
	ret = 0;
 out:
	get_bh(bh);
	*out_bh = bh;

 failed_bh:
	unlock_page(bh->b_page);
	page_cache_release(bh->b_page);
	brelse(bh);
 failed:
	return ret;
}

static int nilfs_mdt_read_block(struct inode *inode, unsigned long block,
				int readahead, struct buffer_head **out_bh)
{
	struct buffer_head *first_bh, *bh;
	unsigned long blkoff;
	int i, nr_ra_blocks = NILFS_MDT_MAX_RA_BLOCKS;
	int err;

	err = nilfs_mdt_submit_block(inode, block, READ, &first_bh);
	if (err == -EEXIST) /* internal code */
		goto out;

	if (unlikely(err))
		goto failed;

	if (readahead) {
		blkoff = block + 1;
		for (i = 0; i < nr_ra_blocks; i++, blkoff++) {
			err = nilfs_mdt_submit_block(inode, blkoff, READA, &bh);
			if (likely(!err || err == -EEXIST))
				brelse(bh);
			else if (err != -EBUSY)
				break;
				/* abort readahead if bmap lookup failed */
			if (!buffer_locked(first_bh))
				goto out_no_wait;
		}
	}

	wait_on_buffer(first_bh);

 out_no_wait:
	err = -EIO;
	if (!buffer_uptodate(first_bh))
		goto failed_bh;
 out:
	*out_bh = first_bh;
	return 0;

 failed_bh:
	brelse(first_bh);
 failed:
	return err;
}

/**
 * nilfs_mdt_get_block - read or create a buffer on meta data file.
 * @inode: inode of the meta data file
 * @blkoff: block offset
 * @create: create flag
 * @init_block: initializer used for newly allocated block
 * @out_bh: output of a pointer to the buffer_head
 *
 * nilfs_mdt_get_block() looks up the specified buffer and tries to create
 * a new buffer if @create is not zero.  On success, the returned buffer is
 * assured to be either existing or formatted using a buffer lock on success.
 * @out_bh is substituted only when zero is returned.
 *
 * Return Value: On success, it returns 0. On error, the following negative
 * error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-ENOENT - the specified block does not exist (hole block)
 *
 * %-EROFS - Read only filesystem (for create mode)
 */
int nilfs_mdt_get_block(struct inode *inode, unsigned long blkoff, int create,
			void (*init_block)(struct inode *,
					   struct buffer_head *, void *),
			struct buffer_head **out_bh)
{
	int ret;

	/* Should be rewritten with merging nilfs_mdt_read_block() */
 retry:
	ret = nilfs_mdt_read_block(inode, blkoff, !create, out_bh);
	if (!create || ret != -ENOENT)
		return ret;

	ret = nilfs_mdt_create_block(inode, blkoff, out_bh, init_block);
	if (unlikely(ret == -EEXIST)) {
		/* create = 0; */  /* limit read-create loop retries */
		goto retry;
	}
	return ret;
}

/**
 * nilfs_mdt_delete_block - make a hole on the meta data file.
 * @inode: inode of the meta data file
 * @block: block offset
 *
 * Return Value: On success, zero is returned.
 * On error, one of the following negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 */
int nilfs_mdt_delete_block(struct inode *inode, unsigned long block)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);
	int err;

	err = nilfs_bmap_delete(ii->i_bmap, block);
	if (!err || err == -ENOENT) {
		nilfs_mdt_mark_dirty(inode);
		nilfs_mdt_forget_block(inode, block);
	}
	return err;
}

/**
 * nilfs_mdt_forget_block - discard dirty state and try to remove the page
 * @inode: inode of the meta data file
 * @block: block offset
 *
 * nilfs_mdt_forget_block() clears a dirty flag of the specified buffer, and
 * tries to release the page including the buffer from a page cache.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error code is returned.
 *
 * %-EBUSY - page has an active buffer.
 *
 * %-ENOENT - page cache has no page addressed by the offset.
 */
int nilfs_mdt_forget_block(struct inode *inode, unsigned long block)
{
	pgoff_t index = (pgoff_t)block >>
		(PAGE_CACHE_SHIFT - inode->i_blkbits);
	struct page *page;
	unsigned long first_block;
	int ret = 0;
	int still_dirty;

	page = find_lock_page(inode->i_mapping, index);
	if (!page)
		return -ENOENT;

	wait_on_page_writeback(page);

	first_block = (unsigned long)index <<
		(PAGE_CACHE_SHIFT - inode->i_blkbits);
	if (page_has_buffers(page)) {
		struct buffer_head *bh;

		bh = nilfs_page_get_nth_block(page, block - first_block);
		nilfs_forget_buffer(bh);
	}
	still_dirty = PageDirty(page);
	unlock_page(page);
	page_cache_release(page);

	if (still_dirty ||
	    invalidate_inode_pages2_range(inode->i_mapping, index, index) != 0)
		ret = -EBUSY;
	return ret;
}

/**
 * nilfs_mdt_mark_block_dirty - mark a block on the meta data file dirty.
 * @inode: inode of the meta data file
 * @block: block offset
 *
 * Return Value: On success, it returns 0. On error, the following negative
 * error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-ENOENT - the specified block does not exist (hole block)
 */
int nilfs_mdt_mark_block_dirty(struct inode *inode, unsigned long block)
{
	struct buffer_head *bh;
	int err;

	err = nilfs_mdt_read_block(inode, block, 0, &bh);
	if (unlikely(err))
		return err;
	mark_buffer_dirty(bh);
	nilfs_mdt_mark_dirty(inode);
	brelse(bh);
	return 0;
}

int nilfs_mdt_fetch_dirty(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);

	if (nilfs_bmap_test_and_clear_dirty(ii->i_bmap)) {
		set_bit(NILFS_I_DIRTY, &ii->i_state);
		return 1;
	}
	return test_bit(NILFS_I_DIRTY, &ii->i_state);
}

static int
nilfs_mdt_write_page(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode;
	struct super_block *sb;
	int err = 0;

	redirty_page_for_writepage(wbc, page);
	unlock_page(page);

	inode = page->mapping->host;
	if (!inode)
		return 0;

	sb = inode->i_sb;

	if (wbc->sync_mode == WB_SYNC_ALL)
		err = nilfs_construct_segment(sb);
	else if (wbc->for_reclaim)
		nilfs_flush_segment(sb, inode->i_ino);

	return err;
}


static const struct address_space_operations def_mdt_aops = {
	.writepage		= nilfs_mdt_write_page,
};

static const struct inode_operations def_mdt_iops;
static const struct file_operations def_mdt_fops;


int nilfs_mdt_init(struct inode *inode, gfp_t gfp_mask, size_t objsz)
{
	struct nilfs_mdt_info *mi;

	mi = kzalloc(max(sizeof(*mi), objsz), GFP_NOFS);
	if (!mi)
		return -ENOMEM;

	init_rwsem(&mi->mi_sem);
	inode->i_private = mi;

	inode->i_mode = S_IFREG;
	mapping_set_gfp_mask(inode->i_mapping, gfp_mask);
	inode->i_mapping->backing_dev_info = inode->i_sb->s_bdi;

	inode->i_op = &def_mdt_iops;
	inode->i_fop = &def_mdt_fops;
	inode->i_mapping->a_ops = &def_mdt_aops;

	return 0;
}

void nilfs_mdt_set_entry_size(struct inode *inode, unsigned entry_size,
			      unsigned header_size)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(inode);

	mi->mi_entry_size = entry_size;
	mi->mi_entries_per_block = (1 << inode->i_blkbits) / entry_size;
	mi->mi_first_entry_offset = DIV_ROUND_UP(header_size, entry_size);
}

/**
 * nilfs_mdt_setup_shadow_map - setup shadow map and bind it to metadata file
 * @inode: inode of the metadata file
 * @shadow: shadow mapping
 */
int nilfs_mdt_setup_shadow_map(struct inode *inode,
			       struct nilfs_shadow_map *shadow)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(inode);
	struct backing_dev_info *bdi = inode->i_sb->s_bdi;

	INIT_LIST_HEAD(&shadow->frozen_buffers);
	address_space_init_once(&shadow->frozen_data);
	nilfs_mapping_init(&shadow->frozen_data, inode, bdi);
	address_space_init_once(&shadow->frozen_btnodes);
	nilfs_mapping_init(&shadow->frozen_btnodes, inode, bdi);
	mi->mi_shadow = shadow;
	return 0;
}

/**
 * nilfs_mdt_save_to_shadow_map - copy bmap and dirty pages to shadow map
 * @inode: inode of the metadata file
 */
int nilfs_mdt_save_to_shadow_map(struct inode *inode)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(inode);
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;
	int ret;

	ret = nilfs_copy_dirty_pages(&shadow->frozen_data, inode->i_mapping);
	if (ret)
		goto out;

	ret = nilfs_copy_dirty_pages(&shadow->frozen_btnodes,
				     &ii->i_btnode_cache);
	if (ret)
		goto out;

	nilfs_bmap_save(ii->i_bmap, &shadow->bmap_store);
 out:
	return ret;
}

int nilfs_mdt_freeze_buffer(struct inode *inode, struct buffer_head *bh)
{
	struct nilfs_shadow_map *shadow = NILFS_MDT(inode)->mi_shadow;
	struct buffer_head *bh_frozen;
	struct page *page;
	int blkbits = inode->i_blkbits;

	page = grab_cache_page(&shadow->frozen_data, bh->b_page->index);
	if (!page)
		return -ENOMEM;

	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << blkbits, 0);

	bh_frozen = nilfs_page_get_nth_block(page, bh_offset(bh) >> blkbits);

	if (!buffer_uptodate(bh_frozen))
		nilfs_copy_buffer(bh_frozen, bh);
	if (list_empty(&bh_frozen->b_assoc_buffers)) {
		list_add_tail(&bh_frozen->b_assoc_buffers,
			      &shadow->frozen_buffers);
		set_buffer_nilfs_redirected(bh);
	} else {
		brelse(bh_frozen); /* already frozen */
	}

	unlock_page(page);
	page_cache_release(page);
	return 0;
}

struct buffer_head *
nilfs_mdt_get_frozen_buffer(struct inode *inode, struct buffer_head *bh)
{
	struct nilfs_shadow_map *shadow = NILFS_MDT(inode)->mi_shadow;
	struct buffer_head *bh_frozen = NULL;
	struct page *page;
	int n;

	page = find_lock_page(&shadow->frozen_data, bh->b_page->index);
	if (page) {
		if (page_has_buffers(page)) {
			n = bh_offset(bh) >> inode->i_blkbits;
			bh_frozen = nilfs_page_get_nth_block(page, n);
		}
		unlock_page(page);
		page_cache_release(page);
	}
	return bh_frozen;
}

static void nilfs_release_frozen_buffers(struct nilfs_shadow_map *shadow)
{
	struct list_head *head = &shadow->frozen_buffers;
	struct buffer_head *bh;

	while (!list_empty(head)) {
		bh = list_first_entry(head, struct buffer_head,
				      b_assoc_buffers);
		list_del_init(&bh->b_assoc_buffers);
		brelse(bh); /* drop ref-count to make it releasable */
	}
}

/**
 * nilfs_mdt_restore_from_shadow_map - restore dirty pages and bmap state
 * @inode: inode of the metadata file
 */
void nilfs_mdt_restore_from_shadow_map(struct inode *inode)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(inode);
	struct nilfs_inode_info *ii = NILFS_I(inode);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;

	down_write(&mi->mi_sem);

	if (mi->mi_palloc_cache)
		nilfs_palloc_clear_cache(inode);

	nilfs_clear_dirty_pages(inode->i_mapping);
	nilfs_copy_back_pages(inode->i_mapping, &shadow->frozen_data);

	nilfs_clear_dirty_pages(&ii->i_btnode_cache);
	nilfs_copy_back_pages(&ii->i_btnode_cache, &shadow->frozen_btnodes);

	nilfs_bmap_restore(ii->i_bmap, &shadow->bmap_store);

	up_write(&mi->mi_sem);
}

/**
 * nilfs_mdt_clear_shadow_map - truncate pages in shadow map caches
 * @inode: inode of the metadata file
 */
void nilfs_mdt_clear_shadow_map(struct inode *inode)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(inode);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;

	down_write(&mi->mi_sem);
	nilfs_release_frozen_buffers(shadow);
	truncate_inode_pages(&shadow->frozen_data, 0);
	truncate_inode_pages(&shadow->frozen_btnodes, 0);
	up_write(&mi->mi_sem);
}
