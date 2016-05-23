/*
 * btnode.c - NILFS B-tree node cache
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
 * This file was originally written by Seiji Kihara <kihara@osrg.net>
 * and fully revised by Ryusuke Konishi <ryusuke@osrg.net> for
 * stabilization and simplification.
 *
 */

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/gfp.h>
#include "nilfs.h"
#include "mdt.h"
#include "dat.h"
#include "page.h"
#include "btnode.h"

void nilfs_btnode_cache_clear(struct address_space *btnc)
{
	invalidate_mapping_pages(btnc, 0, -1);
	truncate_inode_pages(btnc, 0);
}

struct buffer_head *
nilfs_btnode_create_block(struct address_space *btnc, __u64 blocknr)
{
	struct inode *inode = NILFS_BTNC_I(btnc);
	struct buffer_head *bh;

	bh = nilfs_grab_buffer(inode, btnc, blocknr, 1 << BH_NILFS_Node);
	if (unlikely(!bh))
		return NULL;

	if (unlikely(buffer_mapped(bh) || buffer_uptodate(bh) ||
		     buffer_dirty(bh))) {
		brelse(bh);
		BUG();
	}
	memset(bh->b_data, 0, 1 << inode->i_blkbits);
	bh->b_bdev = inode->i_sb->s_bdev;
	bh->b_blocknr = blocknr;
	set_buffer_mapped(bh);
	set_buffer_uptodate(bh);

	unlock_page(bh->b_page);
	put_page(bh->b_page);
	return bh;
}

int nilfs_btnode_submit_block(struct address_space *btnc, __u64 blocknr,
			      sector_t pblocknr, int mode,
			      struct buffer_head **pbh, sector_t *submit_ptr)
{
	struct buffer_head *bh;
	struct inode *inode = NILFS_BTNC_I(btnc);
	struct page *page;
	int err;

	bh = nilfs_grab_buffer(inode, btnc, blocknr, 1 << BH_NILFS_Node);
	if (unlikely(!bh))
		return -ENOMEM;

	err = -EEXIST; /* internal code */
	page = bh->b_page;

	if (buffer_uptodate(bh) || buffer_dirty(bh))
		goto found;

	if (pblocknr == 0) {
		pblocknr = blocknr;
		if (inode->i_ino != NILFS_DAT_INO) {
			struct the_nilfs *nilfs = inode->i_sb->s_fs_info;

			/* blocknr is a virtual block number */
			err = nilfs_dat_translate(nilfs->ns_dat, blocknr,
						  &pblocknr);
			if (unlikely(err)) {
				brelse(bh);
				goto out_locked;
			}
		}
	}

	if (mode == READA) {
		if (pblocknr != *submit_ptr + 1 || !trylock_buffer(bh)) {
			err = -EBUSY; /* internal code */
			brelse(bh);
			goto out_locked;
		}
	} else { /* mode == READ */
		lock_buffer(bh);
	}
	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		err = -EEXIST; /* internal code */
		goto found;
	}
	set_buffer_mapped(bh);
	bh->b_bdev = inode->i_sb->s_bdev;
	bh->b_blocknr = pblocknr; /* set block address for read */
	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(mode, bh);
	bh->b_blocknr = blocknr; /* set back to the given block address */
	*submit_ptr = pblocknr;
	err = 0;
found:
	*pbh = bh;

out_locked:
	unlock_page(page);
	put_page(page);
	return err;
}

/**
 * nilfs_btnode_delete - delete B-tree node buffer
 * @bh: buffer to be deleted
 *
 * nilfs_btnode_delete() invalidates the specified buffer and delete the page
 * including the buffer if the page gets unbusy.
 */
void nilfs_btnode_delete(struct buffer_head *bh)
{
	struct address_space *mapping;
	struct page *page = bh->b_page;
	pgoff_t index = page_index(page);
	int still_dirty;

	get_page(page);
	lock_page(page);
	wait_on_page_writeback(page);

	nilfs_forget_buffer(bh);
	still_dirty = PageDirty(page);
	mapping = page->mapping;
	unlock_page(page);
	put_page(page);

	if (!still_dirty && mapping)
		invalidate_inode_pages2_range(mapping, index, index);
}

/**
 * nilfs_btnode_prepare_change_key
 *  prepare to move contents of the block for old key to one of new key.
 *  the old buffer will not be removed, but might be reused for new buffer.
 *  it might return -ENOMEM because of memory allocation errors,
 *  and might return -EIO because of disk read errors.
 */
int nilfs_btnode_prepare_change_key(struct address_space *btnc,
				    struct nilfs_btnode_chkey_ctxt *ctxt)
{
	struct buffer_head *obh, *nbh;
	struct inode *inode = NILFS_BTNC_I(btnc);
	__u64 oldkey = ctxt->oldkey, newkey = ctxt->newkey;
	int err;

	if (oldkey == newkey)
		return 0;

	obh = ctxt->bh;
	ctxt->newbh = NULL;

	if (inode->i_blkbits == PAGE_SHIFT) {
		lock_page(obh->b_page);
		/*
		 * We cannot call radix_tree_preload for the kernels older
		 * than 2.6.23, because it is not exported for modules.
		 */
retry:
		err = radix_tree_preload(GFP_NOFS & ~__GFP_HIGHMEM);
		if (err)
			goto failed_unlock;
		/* BUG_ON(oldkey != obh->b_page->index); */
		if (unlikely(oldkey != obh->b_page->index))
			NILFS_PAGE_BUG(obh->b_page,
				       "invalid oldkey %lld (newkey=%lld)",
				       (unsigned long long)oldkey,
				       (unsigned long long)newkey);

		spin_lock_irq(&btnc->tree_lock);
		err = radix_tree_insert(&btnc->page_tree, newkey, obh->b_page);
		spin_unlock_irq(&btnc->tree_lock);
		/*
		 * Note: page->index will not change to newkey until
		 * nilfs_btnode_commit_change_key() will be called.
		 * To protect the page in intermediate state, the page lock
		 * is held.
		 */
		radix_tree_preload_end();
		if (!err)
			return 0;
		else if (err != -EEXIST)
			goto failed_unlock;

		err = invalidate_inode_pages2_range(btnc, newkey, newkey);
		if (!err)
			goto retry;
		/* fallback to copy mode */
		unlock_page(obh->b_page);
	}

	nbh = nilfs_btnode_create_block(btnc, newkey);
	if (!nbh)
		return -ENOMEM;

	BUG_ON(nbh == obh);
	ctxt->newbh = nbh;
	return 0;

 failed_unlock:
	unlock_page(obh->b_page);
	return err;
}

/**
 * nilfs_btnode_commit_change_key
 *  commit the change_key operation prepared by prepare_change_key().
 */
void nilfs_btnode_commit_change_key(struct address_space *btnc,
				    struct nilfs_btnode_chkey_ctxt *ctxt)
{
	struct buffer_head *obh = ctxt->bh, *nbh = ctxt->newbh;
	__u64 oldkey = ctxt->oldkey, newkey = ctxt->newkey;
	struct page *opage;

	if (oldkey == newkey)
		return;

	if (nbh == NULL) {	/* blocksize == pagesize */
		opage = obh->b_page;
		if (unlikely(oldkey != opage->index))
			NILFS_PAGE_BUG(opage,
				       "invalid oldkey %lld (newkey=%lld)",
				       (unsigned long long)oldkey,
				       (unsigned long long)newkey);
		mark_buffer_dirty(obh);

		spin_lock_irq(&btnc->tree_lock);
		radix_tree_delete(&btnc->page_tree, oldkey);
		radix_tree_tag_set(&btnc->page_tree, newkey,
				   PAGECACHE_TAG_DIRTY);
		spin_unlock_irq(&btnc->tree_lock);

		opage->index = obh->b_blocknr = newkey;
		unlock_page(opage);
	} else {
		nilfs_copy_buffer(nbh, obh);
		mark_buffer_dirty(nbh);

		nbh->b_blocknr = newkey;
		ctxt->bh = nbh;
		nilfs_btnode_delete(obh); /* will decrement bh->b_count */
	}
}

/**
 * nilfs_btnode_abort_change_key
 *  abort the change_key operation prepared by prepare_change_key().
 */
void nilfs_btnode_abort_change_key(struct address_space *btnc,
				   struct nilfs_btnode_chkey_ctxt *ctxt)
{
	struct buffer_head *nbh = ctxt->newbh;
	__u64 oldkey = ctxt->oldkey, newkey = ctxt->newkey;

	if (oldkey == newkey)
		return;

	if (nbh == NULL) {	/* blocksize == pagesize */
		spin_lock_irq(&btnc->tree_lock);
		radix_tree_delete(&btnc->page_tree, newkey);
		spin_unlock_irq(&btnc->tree_lock);
		unlock_page(ctxt->bh->b_page);
	} else
		brelse(nbh);
}
