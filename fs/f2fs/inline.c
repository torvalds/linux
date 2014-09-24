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
	block_t nr_blocks;
	loff_t i_size;

	if (!test_opt(F2FS_I_SB(inode), INLINE_DATA))
		return false;

	if (f2fs_is_atomic_file(inode))
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
	struct page *ipage;
	void *src_addr, *dst_addr;

	if (page->index) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		goto out;
	}

	ipage = get_node_page(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ipage)) {
		unlock_page(page);
		return PTR_ERR(ipage);
	}

	zero_user_segment(page, MAX_INLINE_DATA, PAGE_CACHE_SIZE);

	/* Copy the whole inline data block */
	src_addr = inline_data_addr(ipage);
	dst_addr = kmap(page);
	memcpy(dst_addr, src_addr, MAX_INLINE_DATA);
	kunmap(page);
	f2fs_put_page(ipage, 1);

out:
	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}

static int __f2fs_convert_inline_data(struct inode *inode, struct page *page)
{
	int err = 0;
	struct page *ipage;
	struct dnode_of_data dn;
	void *src_addr, *dst_addr;
	block_t new_blk_addr;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_io_info fio = {
		.type = DATA,
		.rw = WRITE_SYNC | REQ_PRIO,
	};

	f2fs_lock_op(sbi);
	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out;
	}

	/* someone else converted inline_data already */
	if (!f2fs_has_inline_data(inode))
		goto out;

	/*
	 * i_addr[0] is not used for inline data,
	 * so reserving new block will not destroy inline data
	 */
	set_new_dnode(&dn, inode, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, 0);
	if (err)
		goto out;

	f2fs_wait_on_page_writeback(page, DATA);
	zero_user_segment(page, MAX_INLINE_DATA, PAGE_CACHE_SIZE);

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
	f2fs_wait_on_page_writeback(page, DATA);

	/* clear inline data and flag after data writeback */
	zero_user_segment(ipage, INLINE_DATA_OFFSET,
				 INLINE_DATA_OFFSET + MAX_INLINE_DATA);
	clear_inode_flag(F2FS_I(inode), FI_INLINE_DATA);
	stat_dec_inline_inode(inode);

	sync_inode_page(&dn);
	f2fs_put_dnode(&dn);
out:
	f2fs_unlock_op(sbi);
	return err;
}

int f2fs_convert_inline_data(struct inode *inode, pgoff_t to_size,
						struct page *page)
{
	struct page *new_page = page;
	int err;

	if (!f2fs_has_inline_data(inode))
		return 0;
	else if (to_size <= MAX_INLINE_DATA)
		return 0;

	if (!page || page->index != 0) {
		new_page = grab_cache_page(inode->i_mapping, 0);
		if (!new_page)
			return -ENOMEM;
	}

	err = __f2fs_convert_inline_data(inode, new_page);
	if (!page || page->index != 0)
		f2fs_put_page(new_page, 1);
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

	/* Release any data block if it is allocated */
	if (!f2fs_has_inline_data(inode)) {
		int count = ADDRS_PER_PAGE(dn.node_page, F2FS_I(inode));
		truncate_data_blocks_range(&dn, count);
		set_inode_flag(F2FS_I(inode), FI_INLINE_DATA);
		stat_inc_inline_inode(inode);
	}

	f2fs_wait_on_page_writeback(ipage, NODE);
	zero_user_segment(ipage, INLINE_DATA_OFFSET,
				 INLINE_DATA_OFFSET + MAX_INLINE_DATA);
	src_addr = kmap(page);
	dst_addr = inline_data_addr(ipage);
	memcpy(dst_addr, src_addr, size);
	kunmap(page);

	set_inode_flag(F2FS_I(inode), FI_APPEND_WRITE);
	sync_inode_page(&dn);
	f2fs_put_dnode(&dn);

	return 0;
}

void truncate_inline_data(struct inode *inode, u64 from)
{
	struct page *ipage;

	if (from >= MAX_INLINE_DATA)
		return;

	ipage = get_node_page(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ipage))
		return;

	f2fs_wait_on_page_writeback(ipage, NODE);

	zero_user_segment(ipage, INLINE_DATA_OFFSET + from,
				INLINE_DATA_OFFSET + MAX_INLINE_DATA);
	set_page_dirty(ipage);
	f2fs_put_page(ipage, 1);
}

bool recover_inline_data(struct inode *inode, struct page *npage)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode *ri = NULL;
	void *src_addr, *dst_addr;
	struct page *ipage;

	/*
	 * The inline_data recovery policy is as follows.
	 * [prev.] [next] of inline_data flag
	 *    o       o  -> recover inline_data
	 *    o       x  -> remove inline_data, and then recover data blocks
	 *    x       o  -> remove inline_data, and then recover inline_data
	 *    x       x  -> recover data blocks
	 */
	if (IS_INODE(npage))
		ri = F2FS_INODE(npage);

	if (f2fs_has_inline_data(inode) &&
			ri && (ri->i_inline & F2FS_INLINE_DATA)) {
process_inline:
		ipage = get_node_page(sbi, inode->i_ino);
		f2fs_bug_on(sbi, IS_ERR(ipage));

		f2fs_wait_on_page_writeback(ipage, NODE);

		src_addr = inline_data_addr(npage);
		dst_addr = inline_data_addr(ipage);
		memcpy(dst_addr, src_addr, MAX_INLINE_DATA);
		update_inode(inode, ipage);
		f2fs_put_page(ipage, 1);
		return true;
	}

	if (f2fs_has_inline_data(inode)) {
		ipage = get_node_page(sbi, inode->i_ino);
		f2fs_bug_on(sbi, IS_ERR(ipage));
		f2fs_wait_on_page_writeback(ipage, NODE);
		zero_user_segment(ipage, INLINE_DATA_OFFSET,
				 INLINE_DATA_OFFSET + MAX_INLINE_DATA);
		clear_inode_flag(F2FS_I(inode), FI_INLINE_DATA);
		update_inode(inode, ipage);
		f2fs_put_page(ipage, 1);
	} else if (ri && (ri->i_inline & F2FS_INLINE_DATA)) {
		truncate_blocks(inode, 0, false);
		set_inode_flag(F2FS_I(inode), FI_INLINE_DATA);
		goto process_inline;
	}
	return false;
}

struct f2fs_dir_entry *find_in_inline_dir(struct inode *dir,
				struct qstr *name, struct page **res_page)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct page *ipage;
	struct f2fs_dir_entry *de;
	f2fs_hash_t namehash;
	unsigned long bit_pos = 0;
	struct f2fs_inline_dentry *dentry_blk;
	const void *dentry_bits;

	ipage = get_node_page(sbi, dir->i_ino);
	if (IS_ERR(ipage))
		return NULL;

	namehash = f2fs_dentry_hash(name);

	dentry_blk = inline_data_addr(ipage);
	dentry_bits = &dentry_blk->dentry_bitmap;

	while (bit_pos < NR_INLINE_DENTRY) {
		if (!test_bit_le(bit_pos, dentry_bits)) {
			bit_pos++;
			continue;
		}
		de = &dentry_blk->dentry[bit_pos];
		if (early_match_name(name->len, namehash, de)) {
			if (!memcmp(dentry_blk->filename[bit_pos],
							name->name,
							name->len)) {
				*res_page = ipage;
				goto found;
			}
		}

		/*
		 * For the most part, it should be a bug when name_len is zero.
		 * We stop here for figuring out where the bugs are occurred.
		 */
		f2fs_bug_on(F2FS_P_SB(ipage), !de->name_len);

		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}

	de = NULL;
found:
	unlock_page(ipage);
	return de;
}

struct f2fs_dir_entry *f2fs_parent_inline_dir(struct inode *dir,
							struct page **p)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct page *ipage;
	struct f2fs_dir_entry *de;
	struct f2fs_inline_dentry *dentry_blk;

	ipage = get_node_page(sbi, dir->i_ino);
	if (IS_ERR(ipage))
		return NULL;

	dentry_blk = inline_data_addr(ipage);
	de = &dentry_blk->dentry[1];
	*p = ipage;
	unlock_page(ipage);
	return de;
}

int make_empty_inline_dir(struct inode *inode, struct inode *parent,
							struct page *ipage)
{
	struct f2fs_inline_dentry *dentry_blk;
	struct f2fs_dir_entry *de;

	dentry_blk = inline_data_addr(ipage);

	de = &dentry_blk->dentry[0];
	de->name_len = cpu_to_le16(1);
	de->hash_code = 0;
	de->ino = cpu_to_le32(inode->i_ino);
	memcpy(dentry_blk->filename[0], ".", 1);
	set_de_type(de, inode);

	de = &dentry_blk->dentry[1];
	de->hash_code = 0;
	de->name_len = cpu_to_le16(2);
	de->ino = cpu_to_le32(parent->i_ino);
	memcpy(dentry_blk->filename[1], "..", 2);
	set_de_type(de, inode);

	test_and_set_bit_le(0, &dentry_blk->dentry_bitmap);
	test_and_set_bit_le(1, &dentry_blk->dentry_bitmap);

	set_page_dirty(ipage);

	/* update i_size to MAX_INLINE_DATA */
	if (i_size_read(inode) < MAX_INLINE_DATA) {
		i_size_write(inode, MAX_INLINE_DATA);
		set_inode_flag(F2FS_I(inode), FI_UPDATE_DIR);
	}
	return 0;
}

int room_in_inline_dir(struct f2fs_inline_dentry *dentry_blk, int slots)
{
	int bit_start = 0;
	int zero_start, zero_end;
next:
	zero_start = find_next_zero_bit_le(&dentry_blk->dentry_bitmap,
						NR_INLINE_DENTRY,
						bit_start);
	if (zero_start >= NR_INLINE_DENTRY)
		return NR_INLINE_DENTRY;

	zero_end = find_next_bit_le(&dentry_blk->dentry_bitmap,
						NR_INLINE_DENTRY,
						zero_start);
	if (zero_end - zero_start >= slots)
		return zero_start;

	bit_start = zero_end + 1;

	if (zero_end + 1 >= NR_INLINE_DENTRY)
		return NR_INLINE_DENTRY;
	goto next;
}

int f2fs_convert_inline_dir(struct inode *dir, struct page *ipage,
				struct f2fs_inline_dentry *inline_dentry)
{
	struct page *page;
	struct dnode_of_data dn;
	struct f2fs_dentry_block *dentry_blk;
	int err;

	page = grab_cache_page(dir->i_mapping, 0);
	if (!page)
		return -ENOMEM;

	set_new_dnode(&dn, dir, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, 0);
	if (err)
		goto out;

	f2fs_wait_on_page_writeback(page, DATA);
	zero_user_segment(page, 0, PAGE_CACHE_SIZE);

	dentry_blk = kmap(page);

	/* copy data from inline dentry block to new dentry block */
	memcpy(dentry_blk->dentry_bitmap, inline_dentry->dentry_bitmap,
					INLINE_DENTRY_BITMAP_SIZE);
	memcpy(dentry_blk->dentry, inline_dentry->dentry,
			sizeof(struct f2fs_dir_entry) * NR_INLINE_DENTRY);
	memcpy(dentry_blk->filename, inline_dentry->filename,
					NR_INLINE_DENTRY * F2FS_SLOT_LEN);

	kunmap(page);
	SetPageUptodate(page);
	set_page_dirty(page);

	/* clear inline dir and flag after data writeback */
	zero_user_segment(ipage, INLINE_DATA_OFFSET,
				 INLINE_DATA_OFFSET + MAX_INLINE_DATA);

	stat_dec_inline_inode(dir);

	if (i_size_read(dir) < PAGE_CACHE_SIZE) {
		i_size_write(dir, PAGE_CACHE_SIZE);
		set_inode_flag(F2FS_I(dir), FI_UPDATE_DIR);
	}

	sync_inode_page(&dn);
out:
	f2fs_put_page(page, 1);
	return err;
}

int f2fs_add_inline_entry(struct inode *dir, const struct qstr *name,
						struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct page *ipage;
	unsigned int bit_pos;
	f2fs_hash_t name_hash;
	struct f2fs_dir_entry *de;
	size_t namelen = name->len;
	struct f2fs_inline_dentry *dentry_blk = NULL;
	int slots = GET_DENTRY_SLOTS(namelen);
	struct page *page;
	int err = 0;
	int i;

	name_hash = f2fs_dentry_hash(name);

	ipage = get_node_page(sbi, dir->i_ino);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	dentry_blk = inline_data_addr(ipage);
	bit_pos = room_in_inline_dir(dentry_blk, slots);
	if (bit_pos >= NR_INLINE_DENTRY) {
		err = f2fs_convert_inline_dir(dir, ipage, dentry_blk);
		if (!err)
			err = -EAGAIN;
		goto out;
	}

	f2fs_wait_on_page_writeback(ipage, DATA);

	down_write(&F2FS_I(inode)->i_sem);
	page = init_inode_metadata(inode, dir, name);
	if (IS_ERR(page)) {
		err = PTR_ERR(page);
		goto fail;
	}
	de = &dentry_blk->dentry[bit_pos];
	de->hash_code = name_hash;
	de->name_len = cpu_to_le16(namelen);
	memcpy(dentry_blk->filename[bit_pos], name->name, name->len);
	de->ino = cpu_to_le32(inode->i_ino);
	set_de_type(de, inode);
	for (i = 0; i < slots; i++)
		test_and_set_bit_le(bit_pos + i, &dentry_blk->dentry_bitmap);
	set_page_dirty(ipage);

	/* we don't need to mark_inode_dirty now */
	F2FS_I(inode)->i_pino = dir->i_ino;
	update_inode(inode, page);
	f2fs_put_page(page, 1);

	update_parent_metadata(dir, inode, 0);
fail:
	up_write(&F2FS_I(inode)->i_sem);

	if (is_inode_flag_set(F2FS_I(dir), FI_UPDATE_DIR)) {
		update_inode(dir, ipage);
		clear_inode_flag(F2FS_I(dir), FI_UPDATE_DIR);
	}
out:
	f2fs_put_page(ipage, 1);
	return err;
}

void f2fs_delete_inline_entry(struct f2fs_dir_entry *dentry, struct page *page,
					struct inode *dir, struct inode *inode)
{
	struct f2fs_inline_dentry *inline_dentry;
	int slots = GET_DENTRY_SLOTS(le16_to_cpu(dentry->name_len));
	unsigned int bit_pos;
	int i;

	lock_page(page);
	f2fs_wait_on_page_writeback(page, DATA);

	inline_dentry = inline_data_addr(page);
	bit_pos = dentry - inline_dentry->dentry;
	for (i = 0; i < slots; i++)
		test_and_clear_bit_le(bit_pos + i,
				&inline_dentry->dentry_bitmap);

	set_page_dirty(page);

	dir->i_ctime = dir->i_mtime = CURRENT_TIME;

	if (inode)
		f2fs_drop_nlink(dir, inode, page);

	f2fs_put_page(page, 1);
}

bool f2fs_empty_inline_dir(struct inode *dir)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct page *ipage;
	unsigned int bit_pos = 2;
	struct f2fs_inline_dentry *dentry_blk;

	ipage = get_node_page(sbi, dir->i_ino);
	if (IS_ERR(ipage))
		return false;

	dentry_blk = inline_data_addr(ipage);
	bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap,
					NR_INLINE_DENTRY,
					bit_pos);

	f2fs_put_page(ipage, 1);

	if (bit_pos < NR_INLINE_DENTRY)
		return false;

	return true;
}

int f2fs_read_inline_dir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned int bit_pos = 0;
	struct f2fs_inline_dentry *inline_dentry = NULL;
	struct f2fs_dir_entry *de = NULL;
	struct page *ipage = NULL;
	unsigned char d_type = DT_UNKNOWN;

	if (ctx->pos == NR_INLINE_DENTRY)
		return 0;

	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	bit_pos = ((unsigned long)ctx->pos % NR_INLINE_DENTRY);

	inline_dentry = inline_data_addr(ipage);
	while (bit_pos < NR_INLINE_DENTRY) {
		bit_pos = find_next_bit_le(&inline_dentry->dentry_bitmap,
						NR_INLINE_DENTRY,
						bit_pos);
		if (bit_pos >= NR_INLINE_DENTRY)
			break;

		de = &inline_dentry->dentry[bit_pos];
		if (de->file_type < F2FS_FT_MAX)
			d_type = f2fs_filetype_table[de->file_type];
		else
			d_type = DT_UNKNOWN;

		if (!dir_emit(ctx,
				inline_dentry->filename[bit_pos],
				le16_to_cpu(de->name_len),
				le32_to_cpu(de->ino), d_type))
			goto out;

		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
		ctx->pos = bit_pos;
	}

	ctx->pos = NR_INLINE_DENTRY;
out:
	f2fs_put_page(ipage, 1);

	return 0;
}
