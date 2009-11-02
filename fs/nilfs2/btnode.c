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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include "nilfs.h"
#include "mdt.h"
#include "dat.h"
#include "page.h"
#include "btnode.h"


void nilfs_btnode_cache_init_once(struct address_space *btnc)
{
	memset(btnc, 0, sizeof(*btnc));
	INIT_RADIX_TREE(&btnc->page_tree, GFP_ATOMIC);
	spin_lock_init(&btnc->tree_lock);
	INIT_LIST_HEAD(&btnc->private_list);
	spin_lock_init(&btnc->private_lock);

	spin_lock_init(&btnc->i_mmap_lock);
	INIT_RAW_PRIO_TREE_ROOT(&btnc->i_mmap);
	INIT_LIST_HEAD(&btnc->i_mmap_nonlinear);
}

static const struct address_space_operations def_btnode_aops = {
	.sync_page		= block_sync_page,
};

void nilfs_btnode_cache_init(struct address_space *btnc,
			     struct backing_dev_info *bdi)
{
	btnc->host = NULL;  /* can safely set to host inode ? */
	btnc->flags = 0;
	mapping_set_gfp_mask(btnc, GFP_NOFS);
	btnc->assoc_mapping = NULL;
	btnc->backing_dev_info = bdi;
	btnc->a_ops = &def_btnode_aops;
}

void nilfs_btnode_cache_clear(struct address_space *btnc)
{
	invalidate_mapping_pages(btnc, 0, -1);
	truncate_inode_pages(btnc, 0);
}

int nilfs_btnode_submit_block(struct address_space *btnc, __u64 blocknr,
			      sector_t pblocknr, struct buffer_head **pbh,
			      int newblk)
{
	struct buffer_head *bh;
	struct inode *inode = NILFS_BTNC_I(btnc);
	int err;

	bh = nilfs_grab_buffer(inode, btnc, blocknr, 1 << BH_NILFS_Node);
	if (unlikely(!bh))
		return -ENOMEM;

	err = -EEXIST; /* internal code */
	if (newblk) {
		if (unlikely(buffer_mapped(bh) || buffer_uptodate(bh) ||
			     buffer_dirty(bh))) {
			brelse(bh);
			BUG();
		}
		bh->b_bdev = NILFS_I_NILFS(inode)->ns_bdev;
		bh->b_blocknr = blocknr;
		set_buffer_mapped(bh);
		set_buffer_uptodate(bh);
		goto found;
	}

	if (buffer_uptodate(bh) || buffer_dirty(bh))
		goto found;

	if (pblocknr == 0) {
		pblocknr = blocknr;
		if (inode->i_ino != NILFS_DAT_INO) {
			struct inode *dat =
				nilfs_dat_inode(NILFS_I_NILFS(inode));

			/* blocknr is a virtual block number */
			err = nilfs_dat_translate(dat, blocknr, &pblocknr);
			if (unlikely(err)) {
				brelse(bh);
				goto out_locked;
			}
		}
	}
	lock_buffer(bh);
	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		err = -EEXIST; /* internal code */
		goto found;
	}
	set_buffer_mapped(bh);
	bh->b_bdev = NILFS_I_NILFS(inode)->ns_bdev;
	bh->b_blocknr = pblocknr; /* set block address for read */
	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	submit_bh(READ, bh);
	bh->b_blocknr = blocknr; /* set back to the given block address */
	err = 0;
found:
	*pbh = bh;

out_locked:
	unlock_page(bh->b_page);
	page_cache_release(bh->b_page);
	return err;
}

int nilfs_btnode_get(struct address_space *btnc, __u64 blocknr,
		     sector_t pblocknr, struct buffer_head **pbh, int newblk)
{
	struct buffer_head *bh;
	int err;

	err = nilfs_btnode_submit_block(btnc, blocknr, pblocknr, pbh, newblk);
	if (err == -EEXIST) /* internal code (cache hit) */
		return 0;
	if (unlikely(err))
		return err;

	bh = *pbh;
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		brelse(bh);
		return -EIO;
	}
	return 0;
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

	page_cache_get(page);
	lock_page(page);
	wait_on_page_writeback(page);

	nilfs_forget_buffer(bh);
	still_dirty = PageDirty(page);
	mapping = page->mapping;
	unlock_page(page);
	page_cache_release(page);

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

	if (inode->i_blkbits == PAGE_CACHE_SHIFT) {
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

	err = nilfs_btnode_get(btnc, newkey, 0, &nbh, 1);
	if (likely(!err)) {
		BUG_ON(nbh == obh);
		ctxt->newbh = nbh;
	}
	return err;

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
		nilfs_btnode_mark_dirty(obh);

		spin_lock_irq(&btnc->tree_lock);
		radix_tree_delete(&btnc->page_tree, oldkey);
		radix_tree_tag_set(&btnc->page_tree, newkey,
				   PAGECACHE_TAG_DIRTY);
		spin_unlock_irq(&btnc->tree_lock);

		opage->index = obh->b_blocknr = newkey;
		unlock_page(opage);
	} else {
		nilfs_copy_buffer(nbh, obh);
		nilfs_btnode_mark_dirty(nbh);

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
