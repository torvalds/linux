/*
 * fs/f2fs/inline.c
 * Copyright (c) 2013, Intel Corporation
 * Authors: Huajun Li <huajun.li@intel.com>
 *          Haicheng Li <haicheng.li@intel.com>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"

bool f2fs_may_inline(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	block_t nr_blocks;
	loff_t i_size;

	if (!test_opt(sbi, INLINE_DATA))
		return false;

	nr_blocks = F2FS_I(inode)->i_xattr_nid ? 3 : 2;
	if (inode->i_blocks > nr_blocks)
		return false;

	i_size = i_size_read(inode);
	if (i_size > MAX_INLINE_DATA)
		return false;

	return true;
}

int f2fs_read_inline_data(struct inode *inode, struct page *page)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct page *ipage;
	void *src_addr, *dst_addr;

	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	zero_user_segment(page, INLINE_DATA_OFFSET,
				INLINE_DATA_OFFSET + MAX_INLINE_DATA);

	/* Copy the whole inline data block */
	src_addr = inline_data_addr(ipage);
	dst_addr = kmap(page);
	memcpy(dst_addr, src_addr, MAX_INLINE_DATA);
	kunmap(page);
	f2fs_put_page(ipage, 1);

	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}

static int __f2fs_convert_inline_data(struct inode *inode, struct page *page)
{
	int err;
	struct page *ipage;
	struct dnode_of_data dn;
	void *src_addr, *dst_addr;
	block_t new_blk_addr;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct f2fs_io_info fio = {
		.type = DATA,
		.rw = WRITE_SYNC | REQ_PRIO,
	};

	f2fs_lock_op(sbi);
	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	/*
	 * i_addr[0] is not used for inline data,
	 * so reserving new block will not destroy inline data
	 */
	set_new_dnode(&dn, inode, ipage, ipage, 0);
	err = f2fs_reserve_block(&dn, 0);
	if (err) {
		f2fs_put_page(ipage, 1);
		f2fs_unlock_op(sbi);
		return err;
	}

	zero_user_segment(page, 0, PAGE_CACHE_SIZE);

	/* Copy the whole inline data block */
	src_addr = inline_data_addr(ipage);
	dst_addr = kmap(page);
	memcpy(dst_addr, src_addr, MAX_INLINE_DATA);
	kunmap(page);
	SetPageUptodate(page);

	/* write data page to try to make data consistent */
	set_page_writeback(page);
	write_data_page(page, &dn, &new_blk_addr, &fio);
	update_extent_cache(new_blk_addr, &dn);
	f2fs_wait_on_page_writeback(page, DATA, true);

	/* clear inline data and flag after data writeback */
	zero_user_segment(ipage, INLINE_DATA_OFFSET,
				 INLINE_DATA_OFFSET + MAX_INLINE_DATA);
	clear_inode_flag(F2FS_I(inode), FI_INLINE_DATA);
	stat_dec_inline_inode(inode);

	sync_inode_page(&dn);
	f2fs_put_page(ipage, 1);
	f2fs_unlock_op(sbi);

	return err;
}

int f2fs_convert_inline_data(struct inode *inode, pgoff_t to_size)
{
	struct page *page;
	int err;

	if (!f2fs_has_inline_data(inode))
		return 0;
	else if (to_size <= MAX_INLINE_DATA)
		return 0;

	page = grab_cache_page_write_begin(inode->i_mapping, 0, AOP_FLAG_NOFS);
	if (!page)
		return -ENOMEM;

	err = __f2fs_convert_inline_data(inode, page);
	f2fs_put_page(page, 1);
	return err;
}

int f2fs_write_inline_data(struct inode *inode,
			   struct page *page, unsigned size)
{
	void *src_addr, *dst_addr;
	struct page *ipage;
	struct dnode_of_data dn;
	int err;

	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, 0, LOOKUP_NODE);
	if (err)
		return err;
	ipage = dn.inode_page;

	zero_user_segment(ipage, INLINE_DATA_OFFSET,
				 INLINE_DATA_OFFSET + MAX_INLINE_DATA);
	src_addr = kmap(page);
	dst_addr = inline_data_addr(ipage);
	memcpy(dst_addr, src_addr, size);
	kunmap(page);

	/* Release the first data block if it is allocated */
	if (!f2fs_has_inline_data(inode)) {
		truncate_data_blocks_range(&dn, 1);
		set_inode_flag(F2FS_I(inode), FI_INLINE_DATA);
		stat_inc_inline_inode(inode);
	}

	sync_inode_page(&dn);
	f2fs_put_dnode(&dn);

	return 0;
}
