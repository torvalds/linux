/*
 * Copyright (c) 2012 Taobao.
 * Written by Tao Ma <boyu.mt@taobao.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "ext4_jbd2.h"
#include "ext4.h"
#include "xattr.h"

#define EXT4_XATTR_SYSTEM_DATA	"data"
#define EXT4_MIN_INLINE_DATA_SIZE	((sizeof(__le32) * EXT4_N_BLOCKS))

int ext4_get_inline_size(struct inode *inode)
{
	if (EXT4_I(inode)->i_inline_off)
		return EXT4_I(inode)->i_inline_size;

	return 0;
}

static int get_max_inline_xattr_value_size(struct inode *inode,
					   struct ext4_iloc *iloc)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_entry *entry;
	struct ext4_inode *raw_inode;
	int free, min_offs;

	min_offs = EXT4_SB(inode->i_sb)->s_inode_size -
			EXT4_GOOD_OLD_INODE_SIZE -
			EXT4_I(inode)->i_extra_isize -
			sizeof(struct ext4_xattr_ibody_header);

	/*
	 * We need to subtract another sizeof(__u32) since an in-inode xattr
	 * needs an empty 4 bytes to indicate the gap between the xattr entry
	 * and the name/value pair.
	 */
	if (!ext4_test_inode_state(inode, EXT4_STATE_XATTR))
		return EXT4_XATTR_SIZE(min_offs -
			EXT4_XATTR_LEN(strlen(EXT4_XATTR_SYSTEM_DATA)) -
			EXT4_XATTR_ROUND - sizeof(__u32));

	raw_inode = ext4_raw_inode(iloc);
	header = IHDR(inode, raw_inode);
	entry = IFIRST(header);

	/* Compute min_offs. */
	for (; !IS_LAST_ENTRY(entry); entry = EXT4_XATTR_NEXT(entry)) {
		if (!entry->e_value_block && entry->e_value_size) {
			size_t offs = le16_to_cpu(entry->e_value_offs);
			if (offs < min_offs)
				min_offs = offs;
		}
	}
	free = min_offs -
		((void *)entry - (void *)IFIRST(header)) - sizeof(__u32);

	if (EXT4_I(inode)->i_inline_off) {
		entry = (struct ext4_xattr_entry *)
			((void *)raw_inode + EXT4_I(inode)->i_inline_off);

		free += le32_to_cpu(entry->e_value_size);
		goto out;
	}

	free -= EXT4_XATTR_LEN(strlen(EXT4_XATTR_SYSTEM_DATA));

	if (free > EXT4_XATTR_ROUND)
		free = EXT4_XATTR_SIZE(free - EXT4_XATTR_ROUND);
	else
		free = 0;

out:
	return free;
}

/*
 * Get the maximum size we now can store in an inode.
 * If we can't find the space for a xattr entry, don't use the space
 * of the extents since we have no space to indicate the inline data.
 */
int ext4_get_max_inline_size(struct inode *inode)
{
	int error, max_inline_size;
	struct ext4_iloc iloc;

	if (EXT4_I(inode)->i_extra_isize == 0)
		return 0;

	error = ext4_get_inode_loc(inode, &iloc);
	if (error) {
		ext4_error_inode(inode, __func__, __LINE__, 0,
				 "can't get inode location %lu",
				 inode->i_ino);
		return 0;
	}

	down_read(&EXT4_I(inode)->xattr_sem);
	max_inline_size = get_max_inline_xattr_value_size(inode, &iloc);
	up_read(&EXT4_I(inode)->xattr_sem);

	brelse(iloc.bh);

	if (!max_inline_size)
		return 0;

	return max_inline_size + EXT4_MIN_INLINE_DATA_SIZE;
}

int ext4_has_inline_data(struct inode *inode)
{
	return ext4_test_inode_flag(inode, EXT4_INODE_INLINE_DATA) &&
	       EXT4_I(inode)->i_inline_off;
}

/*
 * this function does not take xattr_sem, which is OK because it is
 * currently only used in a code path coming form ext4_iget, before
 * the new inode has been unlocked
 */
int ext4_find_inline_data_nolock(struct inode *inode)
{
	struct ext4_xattr_ibody_find is = {
		.s = { .not_found = -ENODATA, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
	};
	int error;

	if (EXT4_I(inode)->i_extra_isize == 0)
		return 0;

	error = ext4_get_inode_loc(inode, &is.iloc);
	if (error)
		return error;

	error = ext4_xattr_ibody_find(inode, &i, &is);
	if (error)
		goto out;

	if (!is.s.not_found) {
		EXT4_I(inode)->i_inline_off = (u16)((void *)is.s.here -
					(void *)ext4_raw_inode(&is.iloc));
		EXT4_I(inode)->i_inline_size = EXT4_MIN_INLINE_DATA_SIZE +
				le32_to_cpu(is.s.here->e_value_size);
		ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	}
out:
	brelse(is.iloc.bh);
	return error;
}

static int ext4_read_inline_data(struct inode *inode, void *buffer,
				 unsigned int len,
				 struct ext4_iloc *iloc)
{
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_ibody_header *header;
	int cp_len = 0;
	struct ext4_inode *raw_inode;

	if (!len)
		return 0;

	BUG_ON(len > EXT4_I(inode)->i_inline_size);

	cp_len = len < EXT4_MIN_INLINE_DATA_SIZE ?
			len : EXT4_MIN_INLINE_DATA_SIZE;

	raw_inode = ext4_raw_inode(iloc);
	memcpy(buffer, (void *)(raw_inode->i_block), cp_len);

	len -= cp_len;
	buffer += cp_len;

	if (!len)
		goto out;

	header = IHDR(inode, raw_inode);
	entry = (struct ext4_xattr_entry *)((void *)raw_inode +
					    EXT4_I(inode)->i_inline_off);
	len = min_t(unsigned int, len,
		    (unsigned int)le32_to_cpu(entry->e_value_size));

	memcpy(buffer,
	       (void *)IFIRST(header) + le16_to_cpu(entry->e_value_offs), len);
	cp_len += len;

out:
	return cp_len;
}

/*
 * write the buffer to the inline inode.
 * If 'create' is set, we don't need to do the extra copy in the xattr
 * value since it is already handled by ext4_xattr_ibody_set. That saves
 * us one memcpy.
 */
void ext4_write_inline_data(struct inode *inode, struct ext4_iloc *iloc,
			    void *buffer, loff_t pos, unsigned int len)
{
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_ibody_header *header;
	struct ext4_inode *raw_inode;
	int cp_len = 0;

	BUG_ON(!EXT4_I(inode)->i_inline_off);
	BUG_ON(pos + len > EXT4_I(inode)->i_inline_size);

	raw_inode = ext4_raw_inode(iloc);
	buffer += pos;

	if (pos < EXT4_MIN_INLINE_DATA_SIZE) {
		cp_len = pos + len > EXT4_MIN_INLINE_DATA_SIZE ?
			 EXT4_MIN_INLINE_DATA_SIZE - pos : len;
		memcpy((void *)raw_inode->i_block + pos, buffer, cp_len);

		len -= cp_len;
		buffer += cp_len;
		pos += cp_len;
	}

	if (!len)
		return;

	pos -= EXT4_MIN_INLINE_DATA_SIZE;
	header = IHDR(inode, raw_inode);
	entry = (struct ext4_xattr_entry *)((void *)raw_inode +
					    EXT4_I(inode)->i_inline_off);

	memcpy((void *)IFIRST(header) + le16_to_cpu(entry->e_value_offs) + pos,
	       buffer, len);
}

static int ext4_create_inline_data(handle_t *handle,
				   struct inode *inode, unsigned len)
{
	int error;
	void *value = NULL;
	struct ext4_xattr_ibody_find is = {
		.s = { .not_found = -ENODATA, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
	};

	error = ext4_get_inode_loc(inode, &is.iloc);
	if (error)
		return error;

	error = ext4_journal_get_write_access(handle, is.iloc.bh);
	if (error)
		goto out;

	if (len > EXT4_MIN_INLINE_DATA_SIZE) {
		value = (void *)empty_zero_page;
		len -= EXT4_MIN_INLINE_DATA_SIZE;
	} else {
		value = "";
		len = 0;
	}

	/* Insert the the xttr entry. */
	i.value = value;
	i.value_len = len;

	error = ext4_xattr_ibody_find(inode, &i, &is);
	if (error)
		goto out;

	BUG_ON(!is.s.not_found);

	error = ext4_xattr_ibody_set(handle, inode, &i, &is);
	if (error) {
		if (error == -ENOSPC)
			ext4_clear_inode_state(inode,
					       EXT4_STATE_MAY_INLINE_DATA);
		goto out;
	}

	memset((void *)ext4_raw_inode(&is.iloc)->i_block,
		0, EXT4_MIN_INLINE_DATA_SIZE);

	EXT4_I(inode)->i_inline_off = (u16)((void *)is.s.here -
				      (void *)ext4_raw_inode(&is.iloc));
	EXT4_I(inode)->i_inline_size = len + EXT4_MIN_INLINE_DATA_SIZE;
	ext4_clear_inode_flag(inode, EXT4_INODE_EXTENTS);
	ext4_set_inode_flag(inode, EXT4_INODE_INLINE_DATA);
	get_bh(is.iloc.bh);
	error = ext4_mark_iloc_dirty(handle, inode, &is.iloc);

out:
	brelse(is.iloc.bh);
	return error;
}

static int ext4_update_inline_data(handle_t *handle, struct inode *inode,
				   unsigned int len)
{
	int error;
	void *value = NULL;
	struct ext4_xattr_ibody_find is = {
		.s = { .not_found = -ENODATA, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
	};

	/* If the old space is ok, write the data directly. */
	if (len <= EXT4_I(inode)->i_inline_size)
		return 0;

	error = ext4_get_inode_loc(inode, &is.iloc);
	if (error)
		return error;

	error = ext4_xattr_ibody_find(inode, &i, &is);
	if (error)
		goto out;

	BUG_ON(is.s.not_found);

	len -= EXT4_MIN_INLINE_DATA_SIZE;
	value = kzalloc(len, GFP_NOFS);
	if (!value)
		goto out;

	error = ext4_xattr_ibody_get(inode, i.name_index, i.name,
				     value, len);
	if (error == -ENODATA)
		goto out;

	error = ext4_journal_get_write_access(handle, is.iloc.bh);
	if (error)
		goto out;

	/* Update the xttr entry. */
	i.value = value;
	i.value_len = len;

	error = ext4_xattr_ibody_set(handle, inode, &i, &is);
	if (error)
		goto out;

	EXT4_I(inode)->i_inline_off = (u16)((void *)is.s.here -
				      (void *)ext4_raw_inode(&is.iloc));
	EXT4_I(inode)->i_inline_size = EXT4_MIN_INLINE_DATA_SIZE +
				le32_to_cpu(is.s.here->e_value_size);
	ext4_set_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
	get_bh(is.iloc.bh);
	error = ext4_mark_iloc_dirty(handle, inode, &is.iloc);

out:
	kfree(value);
	brelse(is.iloc.bh);
	return error;
}

int ext4_prepare_inline_data(handle_t *handle, struct inode *inode,
			     unsigned int len)
{
	int ret, size;
	struct ext4_inode_info *ei = EXT4_I(inode);

	if (!ext4_test_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA))
		return -ENOSPC;

	size = ext4_get_max_inline_size(inode);
	if (size < len)
		return -ENOSPC;

	down_write(&EXT4_I(inode)->xattr_sem);

	if (ei->i_inline_off)
		ret = ext4_update_inline_data(handle, inode, len);
	else
		ret = ext4_create_inline_data(handle, inode, len);

	up_write(&EXT4_I(inode)->xattr_sem);

	return ret;
}

static int ext4_destroy_inline_data_nolock(handle_t *handle,
					   struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct ext4_xattr_ibody_find is = {
		.s = { .not_found = 0, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
		.value = NULL,
		.value_len = 0,
	};
	int error;

	if (!ei->i_inline_off)
		return 0;

	error = ext4_get_inode_loc(inode, &is.iloc);
	if (error)
		return error;

	error = ext4_xattr_ibody_find(inode, &i, &is);
	if (error)
		goto out;

	error = ext4_journal_get_write_access(handle, is.iloc.bh);
	if (error)
		goto out;

	error = ext4_xattr_ibody_set(handle, inode, &i, &is);
	if (error)
		goto out;

	memset((void *)ext4_raw_inode(&is.iloc)->i_block,
		0, EXT4_MIN_INLINE_DATA_SIZE);

	if (EXT4_HAS_INCOMPAT_FEATURE(inode->i_sb,
				      EXT4_FEATURE_INCOMPAT_EXTENTS)) {
		if (S_ISDIR(inode->i_mode) ||
		    S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)) {
			ext4_set_inode_flag(inode, EXT4_INODE_EXTENTS);
			ext4_ext_tree_init(handle, inode);
		}
	}
	ext4_clear_inode_flag(inode, EXT4_INODE_INLINE_DATA);

	get_bh(is.iloc.bh);
	error = ext4_mark_iloc_dirty(handle, inode, &is.iloc);

	EXT4_I(inode)->i_inline_off = 0;
	EXT4_I(inode)->i_inline_size = 0;
	ext4_clear_inode_state(inode, EXT4_STATE_MAY_INLINE_DATA);
out:
	brelse(is.iloc.bh);
	if (error == -ENODATA)
		error = 0;
	return error;
}

static int ext4_read_inline_page(struct inode *inode, struct page *page)
{
	void *kaddr;
	int ret = 0;
	size_t len;
	struct ext4_iloc iloc;

	BUG_ON(!PageLocked(page));
	BUG_ON(!ext4_has_inline_data(inode));
	BUG_ON(page->index);

	if (!EXT4_I(inode)->i_inline_off) {
		ext4_warning(inode->i_sb, "inode %lu doesn't have inline data.",
			     inode->i_ino);
		goto out;
	}

	ret = ext4_get_inode_loc(inode, &iloc);
	if (ret)
		goto out;

	len = min_t(size_t, ext4_get_inline_size(inode), i_size_read(inode));
	kaddr = kmap_atomic(page);
	ret = ext4_read_inline_data(inode, kaddr, len, &iloc);
	flush_dcache_page(page);
	kunmap_atomic(kaddr);
	zero_user_segment(page, len, PAGE_CACHE_SIZE);
	SetPageUptodate(page);
	brelse(iloc.bh);

out:
	return ret;
}

int ext4_readpage_inline(struct inode *inode, struct page *page)
{
	int ret = 0;

	down_read(&EXT4_I(inode)->xattr_sem);
	if (!ext4_has_inline_data(inode)) {
		up_read(&EXT4_I(inode)->xattr_sem);
		return -EAGAIN;
	}

	/*
	 * Current inline data can only exist in the 1st page,
	 * So for all the other pages, just set them uptodate.
	 */
	if (!page->index)
		ret = ext4_read_inline_page(inode, page);
	else if (!PageUptodate(page)) {
		zero_user_segment(page, 0, PAGE_CACHE_SIZE);
		SetPageUptodate(page);
	}

	up_read(&EXT4_I(inode)->xattr_sem);

	unlock_page(page);
	return ret >= 0 ? 0 : ret;
}

int ext4_destroy_inline_data(handle_t *handle, struct inode *inode)
{
	int ret;

	down_write(&EXT4_I(inode)->xattr_sem);
	ret = ext4_destroy_inline_data_nolock(handle, inode);
	up_write(&EXT4_I(inode)->xattr_sem);

	return ret;
}
