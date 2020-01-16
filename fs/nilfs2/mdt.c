// SPDX-License-Identifier: GPL-2.0+
/*
 * mdt.c - meta data file for NILFS
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Ryusuke Konishi.
 */

#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include "nilfs.h"
#include "btyesde.h"
#include "segment.h"
#include "page.h"
#include "mdt.h"
#include "alloc.h"		/* nilfs_palloc_destroy_cache() */

#include <trace/events/nilfs2.h>

#define NILFS_MDT_MAX_RA_BLOCKS		(16 - 1)


static int
nilfs_mdt_insert_new_block(struct iyesde *iyesde, unsigned long block,
			   struct buffer_head *bh,
			   void (*init_block)(struct iyesde *,
					      struct buffer_head *, void *))
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
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
	memset(kaddr + bh_offset(bh), 0, i_blocksize(iyesde));
	if (init_block)
		init_block(iyesde, bh, kaddr);
	flush_dcache_page(bh->b_page);
	kunmap_atomic(kaddr);

	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	nilfs_mdt_mark_dirty(iyesde);

	trace_nilfs2_mdt_insert_new_block(iyesde, iyesde->i_iyes, block);

	return 0;
}

static int nilfs_mdt_create_block(struct iyesde *iyesde, unsigned long block,
				  struct buffer_head **out_bh,
				  void (*init_block)(struct iyesde *,
						     struct buffer_head *,
						     void *))
{
	struct super_block *sb = iyesde->i_sb;
	struct nilfs_transaction_info ti;
	struct buffer_head *bh;
	int err;

	nilfs_transaction_begin(sb, &ti, 0);

	err = -ENOMEM;
	bh = nilfs_grab_buffer(iyesde, iyesde->i_mapping, block, 0);
	if (unlikely(!bh))
		goto failed_unlock;

	err = -EEXIST;
	if (buffer_uptodate(bh))
		goto failed_bh;

	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		goto failed_bh;

	bh->b_bdev = sb->s_bdev;
	err = nilfs_mdt_insert_new_block(iyesde, block, bh, init_block);
	if (likely(!err)) {
		get_bh(bh);
		*out_bh = bh;
	}

 failed_bh:
	unlock_page(bh->b_page);
	put_page(bh->b_page);
	brelse(bh);

 failed_unlock:
	if (likely(!err))
		err = nilfs_transaction_commit(sb);
	else
		nilfs_transaction_abort(sb);

	return err;
}

static int
nilfs_mdt_submit_block(struct iyesde *iyesde, unsigned long blkoff,
		       int mode, int mode_flags, struct buffer_head **out_bh)
{
	struct buffer_head *bh;
	__u64 blknum = 0;
	int ret = -ENOMEM;

	bh = nilfs_grab_buffer(iyesde, iyesde->i_mapping, blkoff, 0);
	if (unlikely(!bh))
		goto failed;

	ret = -EEXIST; /* internal code */
	if (buffer_uptodate(bh))
		goto out;

	if (mode_flags & REQ_RAHEAD) {
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

	ret = nilfs_bmap_lookup(NILFS_I(iyesde)->i_bmap, blkoff, &blknum);
	if (unlikely(ret)) {
		unlock_buffer(bh);
		goto failed_bh;
	}
	map_bh(bh, iyesde->i_sb, (sector_t)blknum);

	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(mode, mode_flags, bh);
	ret = 0;

	trace_nilfs2_mdt_submit_block(iyesde, iyesde->i_iyes, blkoff, mode);
 out:
	get_bh(bh);
	*out_bh = bh;

 failed_bh:
	unlock_page(bh->b_page);
	put_page(bh->b_page);
	brelse(bh);
 failed:
	return ret;
}

static int nilfs_mdt_read_block(struct iyesde *iyesde, unsigned long block,
				int readahead, struct buffer_head **out_bh)
{
	struct buffer_head *first_bh, *bh;
	unsigned long blkoff;
	int i, nr_ra_blocks = NILFS_MDT_MAX_RA_BLOCKS;
	int err;

	err = nilfs_mdt_submit_block(iyesde, block, REQ_OP_READ, 0, &first_bh);
	if (err == -EEXIST) /* internal code */
		goto out;

	if (unlikely(err))
		goto failed;

	if (readahead) {
		blkoff = block + 1;
		for (i = 0; i < nr_ra_blocks; i++, blkoff++) {
			err = nilfs_mdt_submit_block(iyesde, blkoff, REQ_OP_READ,
						     REQ_RAHEAD, &bh);
			if (likely(!err || err == -EEXIST))
				brelse(bh);
			else if (err != -EBUSY)
				break;
				/* abort readahead if bmap lookup failed */
			if (!buffer_locked(first_bh))
				goto out_yes_wait;
		}
	}

	wait_on_buffer(first_bh);

 out_yes_wait:
	err = -EIO;
	if (!buffer_uptodate(first_bh)) {
		nilfs_msg(iyesde->i_sb, KERN_ERR,
			  "I/O error reading meta-data file (iyes=%lu, block-offset=%lu)",
			  iyesde->i_iyes, block);
		goto failed_bh;
	}
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
 * @iyesde: iyesde of the meta data file
 * @blkoff: block offset
 * @create: create flag
 * @init_block: initializer used for newly allocated block
 * @out_bh: output of a pointer to the buffer_head
 *
 * nilfs_mdt_get_block() looks up the specified buffer and tries to create
 * a new buffer if @create is yest zero.  On success, the returned buffer is
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
 * %-ENOENT - the specified block does yest exist (hole block)
 *
 * %-EROFS - Read only filesystem (for create mode)
 */
int nilfs_mdt_get_block(struct iyesde *iyesde, unsigned long blkoff, int create,
			void (*init_block)(struct iyesde *,
					   struct buffer_head *, void *),
			struct buffer_head **out_bh)
{
	int ret;

	/* Should be rewritten with merging nilfs_mdt_read_block() */
 retry:
	ret = nilfs_mdt_read_block(iyesde, blkoff, !create, out_bh);
	if (!create || ret != -ENOENT)
		return ret;

	ret = nilfs_mdt_create_block(iyesde, blkoff, out_bh, init_block);
	if (unlikely(ret == -EEXIST)) {
		/* create = 0; */  /* limit read-create loop retries */
		goto retry;
	}
	return ret;
}

/**
 * nilfs_mdt_find_block - find and get a buffer on meta data file.
 * @iyesde: iyesde of the meta data file
 * @start: start block offset (inclusive)
 * @end: end block offset (inclusive)
 * @blkoff: block offset
 * @out_bh: place to store a pointer to buffer_head struct
 *
 * nilfs_mdt_find_block() looks up an existing block in range of
 * [@start, @end] and stores pointer to a buffer head of the block to
 * @out_bh, and block offset to @blkoff, respectively.  @out_bh and
 * @blkoff are substituted only when zero is returned.
 *
 * Return Value: On success, it returns 0. On error, the following negative
 * error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-ENOENT - yes block was found in the range
 */
int nilfs_mdt_find_block(struct iyesde *iyesde, unsigned long start,
			 unsigned long end, unsigned long *blkoff,
			 struct buffer_head **out_bh)
{
	__u64 next;
	int ret;

	if (unlikely(start > end))
		return -ENOENT;

	ret = nilfs_mdt_read_block(iyesde, start, true, out_bh);
	if (!ret) {
		*blkoff = start;
		goto out;
	}
	if (unlikely(ret != -ENOENT || start == ULONG_MAX))
		goto out;

	ret = nilfs_bmap_seek_key(NILFS_I(iyesde)->i_bmap, start + 1, &next);
	if (!ret) {
		if (next <= end) {
			ret = nilfs_mdt_read_block(iyesde, next, true, out_bh);
			if (!ret)
				*blkoff = next;
		} else {
			ret = -ENOENT;
		}
	}
out:
	return ret;
}

/**
 * nilfs_mdt_delete_block - make a hole on the meta data file.
 * @iyesde: iyesde of the meta data file
 * @block: block offset
 *
 * Return Value: On success, zero is returned.
 * On error, one of the following negative error code is returned.
 *
 * %-ENOMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 */
int nilfs_mdt_delete_block(struct iyesde *iyesde, unsigned long block)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	int err;

	err = nilfs_bmap_delete(ii->i_bmap, block);
	if (!err || err == -ENOENT) {
		nilfs_mdt_mark_dirty(iyesde);
		nilfs_mdt_forget_block(iyesde, block);
	}
	return err;
}

/**
 * nilfs_mdt_forget_block - discard dirty state and try to remove the page
 * @iyesde: iyesde of the meta data file
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
 * %-ENOENT - page cache has yes page addressed by the offset.
 */
int nilfs_mdt_forget_block(struct iyesde *iyesde, unsigned long block)
{
	pgoff_t index = (pgoff_t)block >>
		(PAGE_SHIFT - iyesde->i_blkbits);
	struct page *page;
	unsigned long first_block;
	int ret = 0;
	int still_dirty;

	page = find_lock_page(iyesde->i_mapping, index);
	if (!page)
		return -ENOENT;

	wait_on_page_writeback(page);

	first_block = (unsigned long)index <<
		(PAGE_SHIFT - iyesde->i_blkbits);
	if (page_has_buffers(page)) {
		struct buffer_head *bh;

		bh = nilfs_page_get_nth_block(page, block - first_block);
		nilfs_forget_buffer(bh);
	}
	still_dirty = PageDirty(page);
	unlock_page(page);
	put_page(page);

	if (still_dirty ||
	    invalidate_iyesde_pages2_range(iyesde->i_mapping, index, index) != 0)
		ret = -EBUSY;
	return ret;
}

int nilfs_mdt_fetch_dirty(struct iyesde *iyesde)
{
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);

	if (nilfs_bmap_test_and_clear_dirty(ii->i_bmap)) {
		set_bit(NILFS_I_DIRTY, &ii->i_state);
		return 1;
	}
	return test_bit(NILFS_I_DIRTY, &ii->i_state);
}

static int
nilfs_mdt_write_page(struct page *page, struct writeback_control *wbc)
{
	struct iyesde *iyesde = page->mapping->host;
	struct super_block *sb;
	int err = 0;

	if (iyesde && sb_rdonly(iyesde->i_sb)) {
		/*
		 * It means that filesystem was remounted in read-only
		 * mode because of error or metadata corruption. But we
		 * have dirty pages that try to be flushed in background.
		 * So, here we simply discard this dirty page.
		 */
		nilfs_clear_dirty_page(page, false);
		unlock_page(page);
		return -EROFS;
	}

	redirty_page_for_writepage(wbc, page);
	unlock_page(page);

	if (!iyesde)
		return 0;

	sb = iyesde->i_sb;

	if (wbc->sync_mode == WB_SYNC_ALL)
		err = nilfs_construct_segment(sb);
	else if (wbc->for_reclaim)
		nilfs_flush_segment(sb, iyesde->i_iyes);

	return err;
}


static const struct address_space_operations def_mdt_aops = {
	.writepage		= nilfs_mdt_write_page,
};

static const struct iyesde_operations def_mdt_iops;
static const struct file_operations def_mdt_fops;


int nilfs_mdt_init(struct iyesde *iyesde, gfp_t gfp_mask, size_t objsz)
{
	struct nilfs_mdt_info *mi;

	mi = kzalloc(max(sizeof(*mi), objsz), GFP_NOFS);
	if (!mi)
		return -ENOMEM;

	init_rwsem(&mi->mi_sem);
	iyesde->i_private = mi;

	iyesde->i_mode = S_IFREG;
	mapping_set_gfp_mask(iyesde->i_mapping, gfp_mask);

	iyesde->i_op = &def_mdt_iops;
	iyesde->i_fop = &def_mdt_fops;
	iyesde->i_mapping->a_ops = &def_mdt_aops;

	return 0;
}

/**
 * nilfs_mdt_clear - do cleanup for the metadata file
 * @iyesde: iyesde of the metadata file
 */
void nilfs_mdt_clear(struct iyesde *iyesde)
{
	struct nilfs_mdt_info *mdi = NILFS_MDT(iyesde);

	if (mdi->mi_palloc_cache)
		nilfs_palloc_destroy_cache(iyesde);
}

/**
 * nilfs_mdt_destroy - release resources used by the metadata file
 * @iyesde: iyesde of the metadata file
 */
void nilfs_mdt_destroy(struct iyesde *iyesde)
{
	struct nilfs_mdt_info *mdi = NILFS_MDT(iyesde);

	kfree(mdi->mi_bgl); /* kfree(NULL) is safe */
	kfree(mdi);
}

void nilfs_mdt_set_entry_size(struct iyesde *iyesde, unsigned int entry_size,
			      unsigned int header_size)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(iyesde);

	mi->mi_entry_size = entry_size;
	mi->mi_entries_per_block = i_blocksize(iyesde) / entry_size;
	mi->mi_first_entry_offset = DIV_ROUND_UP(header_size, entry_size);
}

/**
 * nilfs_mdt_setup_shadow_map - setup shadow map and bind it to metadata file
 * @iyesde: iyesde of the metadata file
 * @shadow: shadow mapping
 */
int nilfs_mdt_setup_shadow_map(struct iyesde *iyesde,
			       struct nilfs_shadow_map *shadow)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(iyesde);

	INIT_LIST_HEAD(&shadow->frozen_buffers);
	address_space_init_once(&shadow->frozen_data);
	nilfs_mapping_init(&shadow->frozen_data, iyesde);
	address_space_init_once(&shadow->frozen_btyesdes);
	nilfs_mapping_init(&shadow->frozen_btyesdes, iyesde);
	mi->mi_shadow = shadow;
	return 0;
}

/**
 * nilfs_mdt_save_to_shadow_map - copy bmap and dirty pages to shadow map
 * @iyesde: iyesde of the metadata file
 */
int nilfs_mdt_save_to_shadow_map(struct iyesde *iyesde)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(iyesde);
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;
	int ret;

	ret = nilfs_copy_dirty_pages(&shadow->frozen_data, iyesde->i_mapping);
	if (ret)
		goto out;

	ret = nilfs_copy_dirty_pages(&shadow->frozen_btyesdes,
				     &ii->i_btyesde_cache);
	if (ret)
		goto out;

	nilfs_bmap_save(ii->i_bmap, &shadow->bmap_store);
 out:
	return ret;
}

int nilfs_mdt_freeze_buffer(struct iyesde *iyesde, struct buffer_head *bh)
{
	struct nilfs_shadow_map *shadow = NILFS_MDT(iyesde)->mi_shadow;
	struct buffer_head *bh_frozen;
	struct page *page;
	int blkbits = iyesde->i_blkbits;

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
	put_page(page);
	return 0;
}

struct buffer_head *
nilfs_mdt_get_frozen_buffer(struct iyesde *iyesde, struct buffer_head *bh)
{
	struct nilfs_shadow_map *shadow = NILFS_MDT(iyesde)->mi_shadow;
	struct buffer_head *bh_frozen = NULL;
	struct page *page;
	int n;

	page = find_lock_page(&shadow->frozen_data, bh->b_page->index);
	if (page) {
		if (page_has_buffers(page)) {
			n = bh_offset(bh) >> iyesde->i_blkbits;
			bh_frozen = nilfs_page_get_nth_block(page, n);
		}
		unlock_page(page);
		put_page(page);
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
 * @iyesde: iyesde of the metadata file
 */
void nilfs_mdt_restore_from_shadow_map(struct iyesde *iyesde)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(iyesde);
	struct nilfs_iyesde_info *ii = NILFS_I(iyesde);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;

	down_write(&mi->mi_sem);

	if (mi->mi_palloc_cache)
		nilfs_palloc_clear_cache(iyesde);

	nilfs_clear_dirty_pages(iyesde->i_mapping, true);
	nilfs_copy_back_pages(iyesde->i_mapping, &shadow->frozen_data);

	nilfs_clear_dirty_pages(&ii->i_btyesde_cache, true);
	nilfs_copy_back_pages(&ii->i_btyesde_cache, &shadow->frozen_btyesdes);

	nilfs_bmap_restore(ii->i_bmap, &shadow->bmap_store);

	up_write(&mi->mi_sem);
}

/**
 * nilfs_mdt_clear_shadow_map - truncate pages in shadow map caches
 * @iyesde: iyesde of the metadata file
 */
void nilfs_mdt_clear_shadow_map(struct iyesde *iyesde)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(iyesde);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;

	down_write(&mi->mi_sem);
	nilfs_release_frozen_buffers(shadow);
	truncate_iyesde_pages(&shadow->frozen_data, 0);
	truncate_iyesde_pages(&shadow->frozen_btyesdes, 0);
	up_write(&mi->mi_sem);
}
