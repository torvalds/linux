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
#include "nilfs.h"
#include "segment.h"
#include "page.h"
#include "mdt.h"


#define NILFS_MDT_MAX_RA_BLOCKS		(16 - 1)

#define INIT_UNUSED_INODE_FIELDS

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

	kaddr = kmap_atomic(bh->b_page, KM_USER0);
	memset(kaddr + bh_offset(bh), 0, 1 << inode->i_blkbits);
	if (init_block)
		init_block(inode, bh, kaddr);
	flush_dcache_page(bh->b_page);
	kunmap_atomic(kaddr, KM_USER0);

	set_buffer_uptodate(bh);
	nilfs_mark_buffer_dirty(bh);
	nilfs_mdt_mark_dirty(inode);
	return 0;
}

static int nilfs_mdt_create_block(struct inode *inode, unsigned long block,
				  struct buffer_head **out_bh,
				  void (*init_block)(struct inode *,
						     struct buffer_head *,
						     void *))
{
	struct the_nilfs *nilfs = NILFS_MDT(inode)->mi_nilfs;
	struct super_block *sb = inode->i_sb;
	struct nilfs_transaction_info ti;
	struct buffer_head *bh;
	int err;

	if (!sb) {
		/*
		 * Make sure this function is not called from any
		 * read-only context.
		 */
		if (!nilfs->ns_writer) {
			WARN_ON(1);
			err = -EROFS;
			goto out;
		}
		sb = nilfs->ns_writer->s_super;
	}

	nilfs_transaction_begin(sb, &ti, 0);

	err = -ENOMEM;
	bh = nilfs_grab_buffer(inode, inode->i_mapping, block, 0);
	if (unlikely(!bh))
		goto failed_unlock;

	err = -EEXIST;
	if (buffer_uptodate(bh) || buffer_mapped(bh))
		goto failed_bh;
#if 0
	/* The uptodate flag is not protected by the page lock, but
	   the mapped flag is.  Thus, we don't have to wait the buffer. */
	wait_on_buffer(bh);
	if (buffer_uptodate(bh))
		goto failed_bh;
#endif

	bh->b_bdev = nilfs->ns_bdev;
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
 out:
	return err;
}

static int
nilfs_mdt_submit_block(struct inode *inode, unsigned long blkoff,
		       int mode, struct buffer_head **out_bh)
{
	struct buffer_head *bh;
	unsigned long blknum = 0;
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
	if (!buffer_mapped(bh)) { /* unused buffer */
		ret = nilfs_bmap_lookup(NILFS_I(inode)->i_bmap, blkoff,
					&blknum);
		if (unlikely(ret)) {
			unlock_buffer(bh);
			goto failed_bh;
		}
		bh->b_bdev = NILFS_MDT(inode)->mi_nilfs->ns_bdev;
		bh->b_blocknr = blknum;
		set_buffer_mapped(bh);
	}

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
				struct buffer_head **out_bh)
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

	blkoff = block + 1;
	for (i = 0; i < nr_ra_blocks; i++, blkoff++) {
		err = nilfs_mdt_submit_block(inode, blkoff, READA, &bh);
		if (likely(!err || err == -EEXIST))
			brelse(bh);
		else if (err != -EBUSY)
			break; /* abort readahead if bmap lookup failed */

		if (!buffer_locked(first_bh))
			goto out_no_wait;
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
 * %-EINVAL - bmap is broken. (the caller should call nilfs_error())
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
	ret = nilfs_mdt_read_block(inode, blkoff, out_bh);
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
 *
 * %-EINVAL - bmap is broken. (the caller should call nilfs_error())
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
 *
 * %-EINVAL - bmap is broken. (the caller should call nilfs_error())
 */
int nilfs_mdt_mark_block_dirty(struct inode *inode, unsigned long block)
{
	struct buffer_head *bh;
	int err;

	err = nilfs_mdt_read_block(inode, block, &bh);
	if (unlikely(err))
		return err;
	nilfs_mark_buffer_dirty(bh);
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
	struct inode *inode = container_of(page->mapping,
					   struct inode, i_data);
	struct super_block *sb = inode->i_sb;
	struct nilfs_sb_info *writer = NULL;
	int err = 0;

	redirty_page_for_writepage(wbc, page);
	unlock_page(page);

	if (page->mapping->assoc_mapping)
		return 0; /* Do not request flush for shadow page cache */
	if (!sb) {
		writer = nilfs_get_writer(NILFS_MDT(inode)->mi_nilfs);
		if (!writer)
			return -EROFS;
		sb = writer->s_super;
	}

	if (wbc->sync_mode == WB_SYNC_ALL)
		err = nilfs_construct_segment(sb);
	else if (wbc->for_reclaim)
		nilfs_flush_segment(sb, inode->i_ino);

	if (writer)
		nilfs_put_writer(NILFS_MDT(inode)->mi_nilfs);
	return err;
}


static struct address_space_operations def_mdt_aops = {
	.writepage		= nilfs_mdt_write_page,
};

static struct inode_operations def_mdt_iops;
static struct file_operations def_mdt_fops;

/*
 * NILFS2 uses pseudo inodes for meta data files such as DAT, cpfile, sufile,
 * ifile, or gcinodes.  This allows the B-tree code and segment constructor
 * to treat them like regular files, and this helps to simplify the
 * implementation.
 *   On the other hand, some of the pseudo inodes have an irregular point:
 * They don't have valid inode->i_sb pointer because their lifetimes are
 * longer than those of the super block structs; they may continue for
 * several consecutive mounts/umounts.  This would need discussions.
 */
struct inode *
nilfs_mdt_new_common(struct the_nilfs *nilfs, struct super_block *sb,
		     ino_t ino, gfp_t gfp_mask)
{
	struct inode *inode = nilfs_alloc_inode(sb);

	if (!inode)
		return NULL;
	else {
		struct address_space * const mapping = &inode->i_data;
		struct nilfs_mdt_info *mi = kzalloc(sizeof(*mi), GFP_NOFS);

		if (!mi) {
			nilfs_destroy_inode(inode);
			return NULL;
		}
		mi->mi_nilfs = nilfs;
		init_rwsem(&mi->mi_sem);

		inode->i_sb = sb; /* sb may be NULL for some meta data files */
		inode->i_blkbits = nilfs->ns_blocksize_bits;
		inode->i_flags = 0;
		atomic_set(&inode->i_count, 1);
		inode->i_nlink = 1;
		inode->i_ino = ino;
		inode->i_mode = S_IFREG;
		inode->i_private = mi;

#ifdef INIT_UNUSED_INODE_FIELDS
		atomic_set(&inode->i_writecount, 0);
		inode->i_size = 0;
		inode->i_blocks = 0;
		inode->i_bytes = 0;
		inode->i_generation = 0;
#ifdef CONFIG_QUOTA
		memset(&inode->i_dquot, 0, sizeof(inode->i_dquot));
#endif
		inode->i_pipe = NULL;
		inode->i_bdev = NULL;
		inode->i_cdev = NULL;
		inode->i_rdev = 0;
#ifdef CONFIG_SECURITY
		inode->i_security = NULL;
#endif
		inode->dirtied_when = 0;

		INIT_LIST_HEAD(&inode->i_list);
		INIT_LIST_HEAD(&inode->i_sb_list);
		inode->i_state = 0;
#endif

		spin_lock_init(&inode->i_lock);
		mutex_init(&inode->i_mutex);
		init_rwsem(&inode->i_alloc_sem);

		mapping->host = NULL;  /* instead of inode */
		mapping->flags = 0;
		mapping_set_gfp_mask(mapping, gfp_mask);
		mapping->assoc_mapping = NULL;
		mapping->backing_dev_info = nilfs->ns_bdi;

		inode->i_mapping = mapping;
	}

	return inode;
}

struct inode *nilfs_mdt_new(struct the_nilfs *nilfs, struct super_block *sb,
			    ino_t ino, gfp_t gfp_mask)
{
	struct inode *inode = nilfs_mdt_new_common(nilfs, sb, ino, gfp_mask);

	if (!inode)
		return NULL;

	inode->i_op = &def_mdt_iops;
	inode->i_fop = &def_mdt_fops;
	inode->i_mapping->a_ops = &def_mdt_aops;
	return inode;
}

void nilfs_mdt_set_entry_size(struct inode *inode, unsigned entry_size,
			      unsigned header_size)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(inode);

	mi->mi_entry_size = entry_size;
	mi->mi_entries_per_block = (1 << inode->i_blkbits) / entry_size;
	mi->mi_first_entry_offset = DIV_ROUND_UP(header_size, entry_size);
}

void nilfs_mdt_set_shadow(struct inode *orig, struct inode *shadow)
{
	shadow->i_mapping->assoc_mapping = orig->i_mapping;
	NILFS_I(shadow)->i_btnode_cache.assoc_mapping =
		&NILFS_I(orig)->i_btnode_cache;
}

void nilfs_mdt_clear(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);

	invalidate_mapping_pages(inode->i_mapping, 0, -1);
	truncate_inode_pages(inode->i_mapping, 0);

	nilfs_bmap_clear(ii->i_bmap);
	nilfs_btnode_cache_clear(&ii->i_btnode_cache);
}

void nilfs_mdt_destroy(struct inode *inode)
{
	struct nilfs_mdt_info *mdi = NILFS_MDT(inode);

	kfree(mdi->mi_bgl); /* kfree(NULL) is safe */
	kfree(mdi);
	nilfs_destroy_inode(inode);
}
