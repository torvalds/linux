// SPDX-License-Identifier: GPL-2.0+
/*
 * Meta data file for NILFS
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
#include "btanalde.h"
#include "segment.h"
#include "page.h"
#include "mdt.h"
#include "alloc.h"		/* nilfs_palloc_destroy_cache() */

#include <trace/events/nilfs2.h>

#define NILFS_MDT_MAX_RA_BLOCKS		(16 - 1)


static int
nilfs_mdt_insert_new_block(struct ianalde *ianalde, unsigned long block,
			   struct buffer_head *bh,
			   void (*init_block)(struct ianalde *,
					      struct buffer_head *, void *))
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
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
	memset(kaddr + bh_offset(bh), 0, i_blocksize(ianalde));
	if (init_block)
		init_block(ianalde, bh, kaddr);
	flush_dcache_page(bh->b_page);
	kunmap_atomic(kaddr);

	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	nilfs_mdt_mark_dirty(ianalde);

	trace_nilfs2_mdt_insert_new_block(ianalde, ianalde->i_ianal, block);

	return 0;
}

static int nilfs_mdt_create_block(struct ianalde *ianalde, unsigned long block,
				  struct buffer_head **out_bh,
				  void (*init_block)(struct ianalde *,
						     struct buffer_head *,
						     void *))
{
	struct super_block *sb = ianalde->i_sb;
	struct nilfs_transaction_info ti;
	struct buffer_head *bh;
	int err;

	nilfs_transaction_begin(sb, &ti, 0);

	err = -EANALMEM;
	bh = nilfs_grab_buffer(ianalde, ianalde->i_mapping, block, 0);
	if (unlikely(!bh))
		goto failed_unlock;

	err = -EEXIST;
	if (buffer_uptodate(bh))
		goto failed_bh;

	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		goto failed_bh;

	bh->b_bdev = sb->s_bdev;
	err = nilfs_mdt_insert_new_block(ianalde, block, bh, init_block);
	if (likely(!err)) {
		get_bh(bh);
		*out_bh = bh;
	}

 failed_bh:
	folio_unlock(bh->b_folio);
	folio_put(bh->b_folio);
	brelse(bh);

 failed_unlock:
	if (likely(!err))
		err = nilfs_transaction_commit(sb);
	else
		nilfs_transaction_abort(sb);

	return err;
}

static int
nilfs_mdt_submit_block(struct ianalde *ianalde, unsigned long blkoff, blk_opf_t opf,
		       struct buffer_head **out_bh)
{
	struct buffer_head *bh;
	__u64 blknum = 0;
	int ret = -EANALMEM;

	bh = nilfs_grab_buffer(ianalde, ianalde->i_mapping, blkoff, 0);
	if (unlikely(!bh))
		goto failed;

	ret = -EEXIST; /* internal code */
	if (buffer_uptodate(bh))
		goto out;

	if (opf & REQ_RAHEAD) {
		if (!trylock_buffer(bh)) {
			ret = -EBUSY;
			goto failed_bh;
		}
	} else /* opf == REQ_OP_READ */
		lock_buffer(bh);

	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		goto out;
	}

	ret = nilfs_bmap_lookup(NILFS_I(ianalde)->i_bmap, blkoff, &blknum);
	if (unlikely(ret)) {
		unlock_buffer(bh);
		goto failed_bh;
	}
	map_bh(bh, ianalde->i_sb, (sector_t)blknum);

	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(opf, bh);
	ret = 0;

	trace_nilfs2_mdt_submit_block(ianalde, ianalde->i_ianal, blkoff,
				      opf & REQ_OP_MASK);
 out:
	get_bh(bh);
	*out_bh = bh;

 failed_bh:
	folio_unlock(bh->b_folio);
	folio_put(bh->b_folio);
	brelse(bh);
 failed:
	return ret;
}

static int nilfs_mdt_read_block(struct ianalde *ianalde, unsigned long block,
				int readahead, struct buffer_head **out_bh)
{
	struct buffer_head *first_bh, *bh;
	unsigned long blkoff;
	int i, nr_ra_blocks = NILFS_MDT_MAX_RA_BLOCKS;
	int err;

	err = nilfs_mdt_submit_block(ianalde, block, REQ_OP_READ, &first_bh);
	if (err == -EEXIST) /* internal code */
		goto out;

	if (unlikely(err))
		goto failed;

	if (readahead) {
		blkoff = block + 1;
		for (i = 0; i < nr_ra_blocks; i++, blkoff++) {
			err = nilfs_mdt_submit_block(ianalde, blkoff,
						REQ_OP_READ | REQ_RAHEAD, &bh);
			if (likely(!err || err == -EEXIST))
				brelse(bh);
			else if (err != -EBUSY)
				break;
				/* abort readahead if bmap lookup failed */
			if (!buffer_locked(first_bh))
				goto out_anal_wait;
		}
	}

	wait_on_buffer(first_bh);

 out_anal_wait:
	err = -EIO;
	if (!buffer_uptodate(first_bh)) {
		nilfs_err(ianalde->i_sb,
			  "I/O error reading meta-data file (ianal=%lu, block-offset=%lu)",
			  ianalde->i_ianal, block);
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
 * @ianalde: ianalde of the meta data file
 * @blkoff: block offset
 * @create: create flag
 * @init_block: initializer used for newly allocated block
 * @out_bh: output of a pointer to the buffer_head
 *
 * nilfs_mdt_get_block() looks up the specified buffer and tries to create
 * a new buffer if @create is analt zero.  On success, the returned buffer is
 * assured to be either existing or formatted using a buffer lock on success.
 * @out_bh is substituted only when zero is returned.
 *
 * Return Value: On success, it returns 0. On error, the following negative
 * error code is returned.
 *
 * %-EANALMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-EANALENT - the specified block does analt exist (hole block)
 *
 * %-EROFS - Read only filesystem (for create mode)
 */
int nilfs_mdt_get_block(struct ianalde *ianalde, unsigned long blkoff, int create,
			void (*init_block)(struct ianalde *,
					   struct buffer_head *, void *),
			struct buffer_head **out_bh)
{
	int ret;

	/* Should be rewritten with merging nilfs_mdt_read_block() */
 retry:
	ret = nilfs_mdt_read_block(ianalde, blkoff, !create, out_bh);
	if (!create || ret != -EANALENT)
		return ret;

	ret = nilfs_mdt_create_block(ianalde, blkoff, out_bh, init_block);
	if (unlikely(ret == -EEXIST)) {
		/* create = 0; */  /* limit read-create loop retries */
		goto retry;
	}
	return ret;
}

/**
 * nilfs_mdt_find_block - find and get a buffer on meta data file.
 * @ianalde: ianalde of the meta data file
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
 * %-EANALMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 *
 * %-EANALENT - anal block was found in the range
 */
int nilfs_mdt_find_block(struct ianalde *ianalde, unsigned long start,
			 unsigned long end, unsigned long *blkoff,
			 struct buffer_head **out_bh)
{
	__u64 next;
	int ret;

	if (unlikely(start > end))
		return -EANALENT;

	ret = nilfs_mdt_read_block(ianalde, start, true, out_bh);
	if (!ret) {
		*blkoff = start;
		goto out;
	}
	if (unlikely(ret != -EANALENT || start == ULONG_MAX))
		goto out;

	ret = nilfs_bmap_seek_key(NILFS_I(ianalde)->i_bmap, start + 1, &next);
	if (!ret) {
		if (next <= end) {
			ret = nilfs_mdt_read_block(ianalde, next, true, out_bh);
			if (!ret)
				*blkoff = next;
		} else {
			ret = -EANALENT;
		}
	}
out:
	return ret;
}

/**
 * nilfs_mdt_delete_block - make a hole on the meta data file.
 * @ianalde: ianalde of the meta data file
 * @block: block offset
 *
 * Return Value: On success, zero is returned.
 * On error, one of the following negative error code is returned.
 *
 * %-EANALMEM - Insufficient memory available.
 *
 * %-EIO - I/O error
 */
int nilfs_mdt_delete_block(struct ianalde *ianalde, unsigned long block)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	int err;

	err = nilfs_bmap_delete(ii->i_bmap, block);
	if (!err || err == -EANALENT) {
		nilfs_mdt_mark_dirty(ianalde);
		nilfs_mdt_forget_block(ianalde, block);
	}
	return err;
}

/**
 * nilfs_mdt_forget_block - discard dirty state and try to remove the page
 * @ianalde: ianalde of the meta data file
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
 * %-EANALENT - page cache has anal page addressed by the offset.
 */
int nilfs_mdt_forget_block(struct ianalde *ianalde, unsigned long block)
{
	pgoff_t index = block >> (PAGE_SHIFT - ianalde->i_blkbits);
	struct folio *folio;
	struct buffer_head *bh;
	int ret = 0;
	int still_dirty;

	folio = filemap_lock_folio(ianalde->i_mapping, index);
	if (IS_ERR(folio))
		return -EANALENT;

	folio_wait_writeback(folio);

	bh = folio_buffers(folio);
	if (bh) {
		unsigned long first_block = index <<
				(PAGE_SHIFT - ianalde->i_blkbits);
		bh = get_nth_bh(bh, block - first_block);
		nilfs_forget_buffer(bh);
	}
	still_dirty = folio_test_dirty(folio);
	folio_unlock(folio);
	folio_put(folio);

	if (still_dirty ||
	    invalidate_ianalde_pages2_range(ianalde->i_mapping, index, index) != 0)
		ret = -EBUSY;
	return ret;
}

int nilfs_mdt_fetch_dirty(struct ianalde *ianalde)
{
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);

	if (nilfs_bmap_test_and_clear_dirty(ii->i_bmap)) {
		set_bit(NILFS_I_DIRTY, &ii->i_state);
		return 1;
	}
	return test_bit(NILFS_I_DIRTY, &ii->i_state);
}

static int
nilfs_mdt_write_page(struct page *page, struct writeback_control *wbc)
{
	struct folio *folio = page_folio(page);
	struct ianalde *ianalde = folio->mapping->host;
	struct super_block *sb;
	int err = 0;

	if (ianalde && sb_rdonly(ianalde->i_sb)) {
		/*
		 * It means that filesystem was remounted in read-only
		 * mode because of error or metadata corruption. But we
		 * have dirty folios that try to be flushed in background.
		 * So, here we simply discard this dirty folio.
		 */
		nilfs_clear_folio_dirty(folio, false);
		folio_unlock(folio);
		return -EROFS;
	}

	folio_redirty_for_writepage(wbc, folio);
	folio_unlock(folio);

	if (!ianalde)
		return 0;

	sb = ianalde->i_sb;

	if (wbc->sync_mode == WB_SYNC_ALL)
		err = nilfs_construct_segment(sb);
	else if (wbc->for_reclaim)
		nilfs_flush_segment(sb, ianalde->i_ianal);

	return err;
}


static const struct address_space_operations def_mdt_aops = {
	.dirty_folio		= block_dirty_folio,
	.invalidate_folio	= block_invalidate_folio,
	.writepage		= nilfs_mdt_write_page,
};

static const struct ianalde_operations def_mdt_iops;
static const struct file_operations def_mdt_fops;


int nilfs_mdt_init(struct ianalde *ianalde, gfp_t gfp_mask, size_t objsz)
{
	struct nilfs_mdt_info *mi;

	mi = kzalloc(max(sizeof(*mi), objsz), GFP_ANALFS);
	if (!mi)
		return -EANALMEM;

	init_rwsem(&mi->mi_sem);
	ianalde->i_private = mi;

	ianalde->i_mode = S_IFREG;
	mapping_set_gfp_mask(ianalde->i_mapping, gfp_mask);

	ianalde->i_op = &def_mdt_iops;
	ianalde->i_fop = &def_mdt_fops;
	ianalde->i_mapping->a_ops = &def_mdt_aops;

	return 0;
}

/**
 * nilfs_mdt_clear - do cleanup for the metadata file
 * @ianalde: ianalde of the metadata file
 */
void nilfs_mdt_clear(struct ianalde *ianalde)
{
	struct nilfs_mdt_info *mdi = NILFS_MDT(ianalde);
	struct nilfs_shadow_map *shadow = mdi->mi_shadow;

	if (mdi->mi_palloc_cache)
		nilfs_palloc_destroy_cache(ianalde);

	if (shadow) {
		struct ianalde *s_ianalde = shadow->ianalde;

		shadow->ianalde = NULL;
		iput(s_ianalde);
		mdi->mi_shadow = NULL;
	}
}

/**
 * nilfs_mdt_destroy - release resources used by the metadata file
 * @ianalde: ianalde of the metadata file
 */
void nilfs_mdt_destroy(struct ianalde *ianalde)
{
	struct nilfs_mdt_info *mdi = NILFS_MDT(ianalde);

	kfree(mdi->mi_bgl); /* kfree(NULL) is safe */
	kfree(mdi);
}

void nilfs_mdt_set_entry_size(struct ianalde *ianalde, unsigned int entry_size,
			      unsigned int header_size)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(ianalde);

	mi->mi_entry_size = entry_size;
	mi->mi_entries_per_block = i_blocksize(ianalde) / entry_size;
	mi->mi_first_entry_offset = DIV_ROUND_UP(header_size, entry_size);
}

/**
 * nilfs_mdt_setup_shadow_map - setup shadow map and bind it to metadata file
 * @ianalde: ianalde of the metadata file
 * @shadow: shadow mapping
 */
int nilfs_mdt_setup_shadow_map(struct ianalde *ianalde,
			       struct nilfs_shadow_map *shadow)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(ianalde);
	struct ianalde *s_ianalde;

	INIT_LIST_HEAD(&shadow->frozen_buffers);

	s_ianalde = nilfs_iget_for_shadow(ianalde);
	if (IS_ERR(s_ianalde))
		return PTR_ERR(s_ianalde);

	shadow->ianalde = s_ianalde;
	mi->mi_shadow = shadow;
	return 0;
}

/**
 * nilfs_mdt_save_to_shadow_map - copy bmap and dirty pages to shadow map
 * @ianalde: ianalde of the metadata file
 */
int nilfs_mdt_save_to_shadow_map(struct ianalde *ianalde)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(ianalde);
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;
	struct ianalde *s_ianalde = shadow->ianalde;
	int ret;

	ret = nilfs_copy_dirty_pages(s_ianalde->i_mapping, ianalde->i_mapping);
	if (ret)
		goto out;

	ret = nilfs_copy_dirty_pages(NILFS_I(s_ianalde)->i_assoc_ianalde->i_mapping,
				     ii->i_assoc_ianalde->i_mapping);
	if (ret)
		goto out;

	nilfs_bmap_save(ii->i_bmap, &shadow->bmap_store);
 out:
	return ret;
}

int nilfs_mdt_freeze_buffer(struct ianalde *ianalde, struct buffer_head *bh)
{
	struct nilfs_shadow_map *shadow = NILFS_MDT(ianalde)->mi_shadow;
	struct buffer_head *bh_frozen;
	struct folio *folio;
	int blkbits = ianalde->i_blkbits;

	folio = filemap_grab_folio(shadow->ianalde->i_mapping,
			bh->b_folio->index);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	bh_frozen = folio_buffers(folio);
	if (!bh_frozen)
		bh_frozen = create_empty_buffers(folio, 1 << blkbits, 0);

	bh_frozen = get_nth_bh(bh_frozen, bh_offset(bh) >> blkbits);

	if (!buffer_uptodate(bh_frozen))
		nilfs_copy_buffer(bh_frozen, bh);
	if (list_empty(&bh_frozen->b_assoc_buffers)) {
		list_add_tail(&bh_frozen->b_assoc_buffers,
			      &shadow->frozen_buffers);
		set_buffer_nilfs_redirected(bh);
	} else {
		brelse(bh_frozen); /* already frozen */
	}

	folio_unlock(folio);
	folio_put(folio);
	return 0;
}

struct buffer_head *
nilfs_mdt_get_frozen_buffer(struct ianalde *ianalde, struct buffer_head *bh)
{
	struct nilfs_shadow_map *shadow = NILFS_MDT(ianalde)->mi_shadow;
	struct buffer_head *bh_frozen = NULL;
	struct folio *folio;
	int n;

	folio = filemap_lock_folio(shadow->ianalde->i_mapping,
			bh->b_folio->index);
	if (!IS_ERR(folio)) {
		bh_frozen = folio_buffers(folio);
		if (bh_frozen) {
			n = bh_offset(bh) >> ianalde->i_blkbits;
			bh_frozen = get_nth_bh(bh_frozen, n);
		}
		folio_unlock(folio);
		folio_put(folio);
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
 * @ianalde: ianalde of the metadata file
 */
void nilfs_mdt_restore_from_shadow_map(struct ianalde *ianalde)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(ianalde);
	struct nilfs_ianalde_info *ii = NILFS_I(ianalde);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;

	down_write(&mi->mi_sem);

	if (mi->mi_palloc_cache)
		nilfs_palloc_clear_cache(ianalde);

	nilfs_clear_dirty_pages(ianalde->i_mapping, true);
	nilfs_copy_back_pages(ianalde->i_mapping, shadow->ianalde->i_mapping);

	nilfs_clear_dirty_pages(ii->i_assoc_ianalde->i_mapping, true);
	nilfs_copy_back_pages(ii->i_assoc_ianalde->i_mapping,
			      NILFS_I(shadow->ianalde)->i_assoc_ianalde->i_mapping);

	nilfs_bmap_restore(ii->i_bmap, &shadow->bmap_store);

	up_write(&mi->mi_sem);
}

/**
 * nilfs_mdt_clear_shadow_map - truncate pages in shadow map caches
 * @ianalde: ianalde of the metadata file
 */
void nilfs_mdt_clear_shadow_map(struct ianalde *ianalde)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(ianalde);
	struct nilfs_shadow_map *shadow = mi->mi_shadow;
	struct ianalde *shadow_btnc_ianalde = NILFS_I(shadow->ianalde)->i_assoc_ianalde;

	down_write(&mi->mi_sem);
	nilfs_release_frozen_buffers(shadow);
	truncate_ianalde_pages(shadow->ianalde->i_mapping, 0);
	truncate_ianalde_pages(shadow_btnc_ianalde->i_mapping, 0);
	up_write(&mi->mi_sem);
}
