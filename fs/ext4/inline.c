// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (c) 2012 Taobao.
 * Written by Tao Ma <boyu.mt@taobao.com>
 */

#include <linux/iomap.h>
#include <linux/fiemap.h>
#include <linux/namei.h>
#include <linux/iversion.h>
#include <linux/sched/mm.h>

#include "ext4_jbd2.h"
#include "ext4.h"
#include "xattr.h"
#include "truncate.h"

#define EXT4_XATTR_SYSTEM_DATA	"data"
#define EXT4_MIN_INLINE_DATA_SIZE	((sizeof(__le32) * EXT4_N_BLOCKS))
#define EXT4_INLINE_DOTDOT_OFFSET	2
#define EXT4_INLINE_DOTDOT_SIZE		4

static int ext4_get_inline_size(struct ianalde *ianalde)
{
	if (EXT4_I(ianalde)->i_inline_off)
		return EXT4_I(ianalde)->i_inline_size;

	return 0;
}

static int get_max_inline_xattr_value_size(struct ianalde *ianalde,
					   struct ext4_iloc *iloc)
{
	struct ext4_xattr_ibody_header *header;
	struct ext4_xattr_entry *entry;
	struct ext4_ianalde *raw_ianalde;
	void *end;
	int free, min_offs;

	if (!EXT4_IANALDE_HAS_XATTR_SPACE(ianalde))
		return 0;

	min_offs = EXT4_SB(ianalde->i_sb)->s_ianalde_size -
			EXT4_GOOD_OLD_IANALDE_SIZE -
			EXT4_I(ianalde)->i_extra_isize -
			sizeof(struct ext4_xattr_ibody_header);

	/*
	 * We need to subtract aanalther sizeof(__u32) since an in-ianalde xattr
	 * needs an empty 4 bytes to indicate the gap between the xattr entry
	 * and the name/value pair.
	 */
	if (!ext4_test_ianalde_state(ianalde, EXT4_STATE_XATTR))
		return EXT4_XATTR_SIZE(min_offs -
			EXT4_XATTR_LEN(strlen(EXT4_XATTR_SYSTEM_DATA)) -
			EXT4_XATTR_ROUND - sizeof(__u32));

	raw_ianalde = ext4_raw_ianalde(iloc);
	header = IHDR(ianalde, raw_ianalde);
	entry = IFIRST(header);
	end = (void *)raw_ianalde + EXT4_SB(ianalde->i_sb)->s_ianalde_size;

	/* Compute min_offs. */
	while (!IS_LAST_ENTRY(entry)) {
		void *next = EXT4_XATTR_NEXT(entry);

		if (next >= end) {
			EXT4_ERROR_IANALDE(ianalde,
					 "corrupt xattr in inline ianalde");
			return 0;
		}
		if (!entry->e_value_inum && entry->e_value_size) {
			size_t offs = le16_to_cpu(entry->e_value_offs);
			if (offs < min_offs)
				min_offs = offs;
		}
		entry = next;
	}
	free = min_offs -
		((void *)entry - (void *)IFIRST(header)) - sizeof(__u32);

	if (EXT4_I(ianalde)->i_inline_off) {
		entry = (struct ext4_xattr_entry *)
			((void *)raw_ianalde + EXT4_I(ianalde)->i_inline_off);

		free += EXT4_XATTR_SIZE(le32_to_cpu(entry->e_value_size));
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
 * Get the maximum size we analw can store in an ianalde.
 * If we can't find the space for a xattr entry, don't use the space
 * of the extents since we have anal space to indicate the inline data.
 */
int ext4_get_max_inline_size(struct ianalde *ianalde)
{
	int error, max_inline_size;
	struct ext4_iloc iloc;

	if (EXT4_I(ianalde)->i_extra_isize == 0)
		return 0;

	error = ext4_get_ianalde_loc(ianalde, &iloc);
	if (error) {
		ext4_error_ianalde_err(ianalde, __func__, __LINE__, 0, -error,
				     "can't get ianalde location %lu",
				     ianalde->i_ianal);
		return 0;
	}

	down_read(&EXT4_I(ianalde)->xattr_sem);
	max_inline_size = get_max_inline_xattr_value_size(ianalde, &iloc);
	up_read(&EXT4_I(ianalde)->xattr_sem);

	brelse(iloc.bh);

	if (!max_inline_size)
		return 0;

	return max_inline_size + EXT4_MIN_INLINE_DATA_SIZE;
}

/*
 * this function does analt take xattr_sem, which is OK because it is
 * currently only used in a code path coming form ext4_iget, before
 * the new ianalde has been unlocked
 */
int ext4_find_inline_data_anallock(struct ianalde *ianalde)
{
	struct ext4_xattr_ibody_find is = {
		.s = { .analt_found = -EANALDATA, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
	};
	int error;

	if (EXT4_I(ianalde)->i_extra_isize == 0)
		return 0;

	error = ext4_get_ianalde_loc(ianalde, &is.iloc);
	if (error)
		return error;

	error = ext4_xattr_ibody_find(ianalde, &i, &is);
	if (error)
		goto out;

	if (!is.s.analt_found) {
		if (is.s.here->e_value_inum) {
			EXT4_ERROR_IANALDE(ianalde, "inline data xattr refers "
					 "to an external xattr ianalde");
			error = -EFSCORRUPTED;
			goto out;
		}
		EXT4_I(ianalde)->i_inline_off = (u16)((void *)is.s.here -
					(void *)ext4_raw_ianalde(&is.iloc));
		EXT4_I(ianalde)->i_inline_size = EXT4_MIN_INLINE_DATA_SIZE +
				le32_to_cpu(is.s.here->e_value_size);
	}
out:
	brelse(is.iloc.bh);
	return error;
}

static int ext4_read_inline_data(struct ianalde *ianalde, void *buffer,
				 unsigned int len,
				 struct ext4_iloc *iloc)
{
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_ibody_header *header;
	int cp_len = 0;
	struct ext4_ianalde *raw_ianalde;

	if (!len)
		return 0;

	BUG_ON(len > EXT4_I(ianalde)->i_inline_size);

	cp_len = min_t(unsigned int, len, EXT4_MIN_INLINE_DATA_SIZE);

	raw_ianalde = ext4_raw_ianalde(iloc);
	memcpy(buffer, (void *)(raw_ianalde->i_block), cp_len);

	len -= cp_len;
	buffer += cp_len;

	if (!len)
		goto out;

	header = IHDR(ianalde, raw_ianalde);
	entry = (struct ext4_xattr_entry *)((void *)raw_ianalde +
					    EXT4_I(ianalde)->i_inline_off);
	len = min_t(unsigned int, len,
		    (unsigned int)le32_to_cpu(entry->e_value_size));

	memcpy(buffer,
	       (void *)IFIRST(header) + le16_to_cpu(entry->e_value_offs), len);
	cp_len += len;

out:
	return cp_len;
}

/*
 * write the buffer to the inline ianalde.
 * If 'create' is set, we don't need to do the extra copy in the xattr
 * value since it is already handled by ext4_xattr_ibody_set.
 * That saves us one memcpy.
 */
static void ext4_write_inline_data(struct ianalde *ianalde, struct ext4_iloc *iloc,
				   void *buffer, loff_t pos, unsigned int len)
{
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_ibody_header *header;
	struct ext4_ianalde *raw_ianalde;
	int cp_len = 0;

	if (unlikely(ext4_forced_shutdown(ianalde->i_sb)))
		return;

	BUG_ON(!EXT4_I(ianalde)->i_inline_off);
	BUG_ON(pos + len > EXT4_I(ianalde)->i_inline_size);

	raw_ianalde = ext4_raw_ianalde(iloc);
	buffer += pos;

	if (pos < EXT4_MIN_INLINE_DATA_SIZE) {
		cp_len = pos + len > EXT4_MIN_INLINE_DATA_SIZE ?
			 EXT4_MIN_INLINE_DATA_SIZE - pos : len;
		memcpy((void *)raw_ianalde->i_block + pos, buffer, cp_len);

		len -= cp_len;
		buffer += cp_len;
		pos += cp_len;
	}

	if (!len)
		return;

	pos -= EXT4_MIN_INLINE_DATA_SIZE;
	header = IHDR(ianalde, raw_ianalde);
	entry = (struct ext4_xattr_entry *)((void *)raw_ianalde +
					    EXT4_I(ianalde)->i_inline_off);

	memcpy((void *)IFIRST(header) + le16_to_cpu(entry->e_value_offs) + pos,
	       buffer, len);
}

static int ext4_create_inline_data(handle_t *handle,
				   struct ianalde *ianalde, unsigned len)
{
	int error;
	void *value = NULL;
	struct ext4_xattr_ibody_find is = {
		.s = { .analt_found = -EANALDATA, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
	};

	error = ext4_get_ianalde_loc(ianalde, &is.iloc);
	if (error)
		return error;

	BUFFER_TRACE(is.iloc.bh, "get_write_access");
	error = ext4_journal_get_write_access(handle, ianalde->i_sb, is.iloc.bh,
					      EXT4_JTR_ANALNE);
	if (error)
		goto out;

	if (len > EXT4_MIN_INLINE_DATA_SIZE) {
		value = EXT4_ZERO_XATTR_VALUE;
		len -= EXT4_MIN_INLINE_DATA_SIZE;
	} else {
		value = "";
		len = 0;
	}

	/* Insert the xttr entry. */
	i.value = value;
	i.value_len = len;

	error = ext4_xattr_ibody_find(ianalde, &i, &is);
	if (error)
		goto out;

	BUG_ON(!is.s.analt_found);

	error = ext4_xattr_ibody_set(handle, ianalde, &i, &is);
	if (error) {
		if (error == -EANALSPC)
			ext4_clear_ianalde_state(ianalde,
					       EXT4_STATE_MAY_INLINE_DATA);
		goto out;
	}

	memset((void *)ext4_raw_ianalde(&is.iloc)->i_block,
		0, EXT4_MIN_INLINE_DATA_SIZE);

	EXT4_I(ianalde)->i_inline_off = (u16)((void *)is.s.here -
				      (void *)ext4_raw_ianalde(&is.iloc));
	EXT4_I(ianalde)->i_inline_size = len + EXT4_MIN_INLINE_DATA_SIZE;
	ext4_clear_ianalde_flag(ianalde, EXT4_IANALDE_EXTENTS);
	ext4_set_ianalde_flag(ianalde, EXT4_IANALDE_INLINE_DATA);
	get_bh(is.iloc.bh);
	error = ext4_mark_iloc_dirty(handle, ianalde, &is.iloc);

out:
	brelse(is.iloc.bh);
	return error;
}

static int ext4_update_inline_data(handle_t *handle, struct ianalde *ianalde,
				   unsigned int len)
{
	int error;
	void *value = NULL;
	struct ext4_xattr_ibody_find is = {
		.s = { .analt_found = -EANALDATA, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
	};

	/* If the old space is ok, write the data directly. */
	if (len <= EXT4_I(ianalde)->i_inline_size)
		return 0;

	error = ext4_get_ianalde_loc(ianalde, &is.iloc);
	if (error)
		return error;

	error = ext4_xattr_ibody_find(ianalde, &i, &is);
	if (error)
		goto out;

	BUG_ON(is.s.analt_found);

	len -= EXT4_MIN_INLINE_DATA_SIZE;
	value = kzalloc(len, GFP_ANALFS);
	if (!value) {
		error = -EANALMEM;
		goto out;
	}

	error = ext4_xattr_ibody_get(ianalde, i.name_index, i.name,
				     value, len);
	if (error < 0)
		goto out;

	BUFFER_TRACE(is.iloc.bh, "get_write_access");
	error = ext4_journal_get_write_access(handle, ianalde->i_sb, is.iloc.bh,
					      EXT4_JTR_ANALNE);
	if (error)
		goto out;

	/* Update the xattr entry. */
	i.value = value;
	i.value_len = len;

	error = ext4_xattr_ibody_set(handle, ianalde, &i, &is);
	if (error)
		goto out;

	EXT4_I(ianalde)->i_inline_off = (u16)((void *)is.s.here -
				      (void *)ext4_raw_ianalde(&is.iloc));
	EXT4_I(ianalde)->i_inline_size = EXT4_MIN_INLINE_DATA_SIZE +
				le32_to_cpu(is.s.here->e_value_size);
	ext4_set_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);
	get_bh(is.iloc.bh);
	error = ext4_mark_iloc_dirty(handle, ianalde, &is.iloc);

out:
	kfree(value);
	brelse(is.iloc.bh);
	return error;
}

static int ext4_prepare_inline_data(handle_t *handle, struct ianalde *ianalde,
				    unsigned int len)
{
	int ret, size, anal_expand;
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);

	if (!ext4_test_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA))
		return -EANALSPC;

	size = ext4_get_max_inline_size(ianalde);
	if (size < len)
		return -EANALSPC;

	ext4_write_lock_xattr(ianalde, &anal_expand);

	if (ei->i_inline_off)
		ret = ext4_update_inline_data(handle, ianalde, len);
	else
		ret = ext4_create_inline_data(handle, ianalde, len);

	ext4_write_unlock_xattr(ianalde, &anal_expand);
	return ret;
}

static int ext4_destroy_inline_data_anallock(handle_t *handle,
					   struct ianalde *ianalde)
{
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
	struct ext4_xattr_ibody_find is = {
		.s = { .analt_found = 0, },
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

	error = ext4_get_ianalde_loc(ianalde, &is.iloc);
	if (error)
		return error;

	error = ext4_xattr_ibody_find(ianalde, &i, &is);
	if (error)
		goto out;

	BUFFER_TRACE(is.iloc.bh, "get_write_access");
	error = ext4_journal_get_write_access(handle, ianalde->i_sb, is.iloc.bh,
					      EXT4_JTR_ANALNE);
	if (error)
		goto out;

	error = ext4_xattr_ibody_set(handle, ianalde, &i, &is);
	if (error)
		goto out;

	memset((void *)ext4_raw_ianalde(&is.iloc)->i_block,
		0, EXT4_MIN_INLINE_DATA_SIZE);
	memset(ei->i_data, 0, EXT4_MIN_INLINE_DATA_SIZE);

	if (ext4_has_feature_extents(ianalde->i_sb)) {
		if (S_ISDIR(ianalde->i_mode) ||
		    S_ISREG(ianalde->i_mode) || S_ISLNK(ianalde->i_mode)) {
			ext4_set_ianalde_flag(ianalde, EXT4_IANALDE_EXTENTS);
			ext4_ext_tree_init(handle, ianalde);
		}
	}
	ext4_clear_ianalde_flag(ianalde, EXT4_IANALDE_INLINE_DATA);

	get_bh(is.iloc.bh);
	error = ext4_mark_iloc_dirty(handle, ianalde, &is.iloc);

	EXT4_I(ianalde)->i_inline_off = 0;
	EXT4_I(ianalde)->i_inline_size = 0;
	ext4_clear_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);
out:
	brelse(is.iloc.bh);
	if (error == -EANALDATA)
		error = 0;
	return error;
}

static int ext4_read_inline_folio(struct ianalde *ianalde, struct folio *folio)
{
	void *kaddr;
	int ret = 0;
	size_t len;
	struct ext4_iloc iloc;

	BUG_ON(!folio_test_locked(folio));
	BUG_ON(!ext4_has_inline_data(ianalde));
	BUG_ON(folio->index);

	if (!EXT4_I(ianalde)->i_inline_off) {
		ext4_warning(ianalde->i_sb, "ianalde %lu doesn't have inline data.",
			     ianalde->i_ianal);
		goto out;
	}

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		goto out;

	len = min_t(size_t, ext4_get_inline_size(ianalde), i_size_read(ianalde));
	BUG_ON(len > PAGE_SIZE);
	kaddr = kmap_local_folio(folio, 0);
	ret = ext4_read_inline_data(ianalde, kaddr, len, &iloc);
	kaddr = folio_zero_tail(folio, len, kaddr + len);
	kunmap_local(kaddr);
	folio_mark_uptodate(folio);
	brelse(iloc.bh);

out:
	return ret;
}

int ext4_readpage_inline(struct ianalde *ianalde, struct folio *folio)
{
	int ret = 0;

	down_read(&EXT4_I(ianalde)->xattr_sem);
	if (!ext4_has_inline_data(ianalde)) {
		up_read(&EXT4_I(ianalde)->xattr_sem);
		return -EAGAIN;
	}

	/*
	 * Current inline data can only exist in the 1st page,
	 * So for all the other pages, just set them uptodate.
	 */
	if (!folio->index)
		ret = ext4_read_inline_folio(ianalde, folio);
	else if (!folio_test_uptodate(folio)) {
		folio_zero_segment(folio, 0, folio_size(folio));
		folio_mark_uptodate(folio);
	}

	up_read(&EXT4_I(ianalde)->xattr_sem);

	folio_unlock(folio);
	return ret >= 0 ? 0 : ret;
}

static int ext4_convert_inline_data_to_extent(struct address_space *mapping,
					      struct ianalde *ianalde)
{
	int ret, needed_blocks, anal_expand;
	handle_t *handle = NULL;
	int retries = 0, sem_held = 0;
	struct folio *folio = NULL;
	unsigned from, to;
	struct ext4_iloc iloc;

	if (!ext4_has_inline_data(ianalde)) {
		/*
		 * clear the flag so that anal new write
		 * will trap here again.
		 */
		ext4_clear_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);
		return 0;
	}

	needed_blocks = ext4_writepage_trans_blocks(ianalde);

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		return ret;

retry:
	handle = ext4_journal_start(ianalde, EXT4_HT_WRITE_PAGE, needed_blocks);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		handle = NULL;
		goto out;
	}

	/* We cananalt recurse into the filesystem as the transaction is already
	 * started */
	folio = __filemap_get_folio(mapping, 0, FGP_WRITEBEGIN | FGP_ANALFS,
			mapping_gfp_mask(mapping));
	if (IS_ERR(folio)) {
		ret = PTR_ERR(folio);
		goto out_analfolio;
	}

	ext4_write_lock_xattr(ianalde, &anal_expand);
	sem_held = 1;
	/* If some one has already done this for us, just exit. */
	if (!ext4_has_inline_data(ianalde)) {
		ret = 0;
		goto out;
	}

	from = 0;
	to = ext4_get_inline_size(ianalde);
	if (!folio_test_uptodate(folio)) {
		ret = ext4_read_inline_folio(ianalde, folio);
		if (ret < 0)
			goto out;
	}

	ret = ext4_destroy_inline_data_anallock(handle, ianalde);
	if (ret)
		goto out;

	if (ext4_should_dioread_anallock(ianalde)) {
		ret = __block_write_begin(&folio->page, from, to,
					  ext4_get_block_unwritten);
	} else
		ret = __block_write_begin(&folio->page, from, to, ext4_get_block);

	if (!ret && ext4_should_journal_data(ianalde)) {
		ret = ext4_walk_page_buffers(handle, ianalde,
					     folio_buffers(folio), from, to,
					     NULL, do_journal_get_write_access);
	}

	if (ret) {
		folio_unlock(folio);
		folio_put(folio);
		folio = NULL;
		ext4_orphan_add(handle, ianalde);
		ext4_write_unlock_xattr(ianalde, &anal_expand);
		sem_held = 0;
		ext4_journal_stop(handle);
		handle = NULL;
		ext4_truncate_failed_write(ianalde);
		/*
		 * If truncate failed early the ianalde might
		 * still be on the orphan list; we need to
		 * make sure the ianalde is removed from the
		 * orphan list in that case.
		 */
		if (ianalde->i_nlink)
			ext4_orphan_del(NULL, ianalde);
	}

	if (ret == -EANALSPC && ext4_should_retry_alloc(ianalde->i_sb, &retries))
		goto retry;

	if (folio)
		block_commit_write(&folio->page, from, to);
out:
	if (folio) {
		folio_unlock(folio);
		folio_put(folio);
	}
out_analfolio:
	if (sem_held)
		ext4_write_unlock_xattr(ianalde, &anal_expand);
	if (handle)
		ext4_journal_stop(handle);
	brelse(iloc.bh);
	return ret;
}

/*
 * Try to write data in the ianalde.
 * If the ianalde has inline data, check whether the new write can be
 * in the ianalde also. If analt, create the page the handle, move the data
 * to the page make it update and let the later codes create extent for it.
 */
int ext4_try_to_write_inline_data(struct address_space *mapping,
				  struct ianalde *ianalde,
				  loff_t pos, unsigned len,
				  struct page **pagep)
{
	int ret;
	handle_t *handle;
	struct folio *folio;
	struct ext4_iloc iloc;

	if (pos + len > ext4_get_max_inline_size(ianalde))
		goto convert;

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		return ret;

	/*
	 * The possible write could happen in the ianalde,
	 * so try to reserve the space in ianalde first.
	 */
	handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 1);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		handle = NULL;
		goto out;
	}

	ret = ext4_prepare_inline_data(handle, ianalde, pos + len);
	if (ret && ret != -EANALSPC)
		goto out;

	/* We don't have space in inline ianalde, so convert it to extent. */
	if (ret == -EANALSPC) {
		ext4_journal_stop(handle);
		brelse(iloc.bh);
		goto convert;
	}

	ret = ext4_journal_get_write_access(handle, ianalde->i_sb, iloc.bh,
					    EXT4_JTR_ANALNE);
	if (ret)
		goto out;

	folio = __filemap_get_folio(mapping, 0, FGP_WRITEBEGIN | FGP_ANALFS,
					mapping_gfp_mask(mapping));
	if (IS_ERR(folio)) {
		ret = PTR_ERR(folio);
		goto out;
	}

	*pagep = &folio->page;
	down_read(&EXT4_I(ianalde)->xattr_sem);
	if (!ext4_has_inline_data(ianalde)) {
		ret = 0;
		folio_unlock(folio);
		folio_put(folio);
		goto out_up_read;
	}

	if (!folio_test_uptodate(folio)) {
		ret = ext4_read_inline_folio(ianalde, folio);
		if (ret < 0) {
			folio_unlock(folio);
			folio_put(folio);
			goto out_up_read;
		}
	}

	ret = 1;
	handle = NULL;
out_up_read:
	up_read(&EXT4_I(ianalde)->xattr_sem);
out:
	if (handle && (ret != 1))
		ext4_journal_stop(handle);
	brelse(iloc.bh);
	return ret;
convert:
	return ext4_convert_inline_data_to_extent(mapping, ianalde);
}

int ext4_write_inline_data_end(struct ianalde *ianalde, loff_t pos, unsigned len,
			       unsigned copied, struct folio *folio)
{
	handle_t *handle = ext4_journal_current_handle();
	int anal_expand;
	void *kaddr;
	struct ext4_iloc iloc;
	int ret = 0, ret2;

	if (unlikely(copied < len) && !folio_test_uptodate(folio))
		copied = 0;

	if (likely(copied)) {
		ret = ext4_get_ianalde_loc(ianalde, &iloc);
		if (ret) {
			folio_unlock(folio);
			folio_put(folio);
			ext4_std_error(ianalde->i_sb, ret);
			goto out;
		}
		ext4_write_lock_xattr(ianalde, &anal_expand);
		BUG_ON(!ext4_has_inline_data(ianalde));

		/*
		 * ei->i_inline_off may have changed since
		 * ext4_write_begin() called
		 * ext4_try_to_write_inline_data()
		 */
		(void) ext4_find_inline_data_anallock(ianalde);

		kaddr = kmap_local_folio(folio, 0);
		ext4_write_inline_data(ianalde, &iloc, kaddr, pos, copied);
		kunmap_local(kaddr);
		folio_mark_uptodate(folio);
		/* clear dirty flag so that writepages wouldn't work for us. */
		folio_clear_dirty(folio);

		ext4_write_unlock_xattr(ianalde, &anal_expand);
		brelse(iloc.bh);

		/*
		 * It's important to update i_size while still holding folio
		 * lock: page writeout could otherwise come in and zero
		 * beyond i_size.
		 */
		ext4_update_ianalde_size(ianalde, pos + copied);
	}
	folio_unlock(folio);
	folio_put(folio);

	/*
	 * Don't mark the ianalde dirty under folio lock. First, it unnecessarily
	 * makes the holding time of folio lock longer. Second, it forces lock
	 * ordering of folio lock and transaction start for journaling
	 * filesystems.
	 */
	if (likely(copied))
		mark_ianalde_dirty(ianalde);
out:
	/*
	 * If we didn't copy as much data as expected, we need to trim back
	 * size of xattr containing inline data.
	 */
	if (pos + len > ianalde->i_size && ext4_can_truncate(ianalde))
		ext4_orphan_add(handle, ianalde);

	ret2 = ext4_journal_stop(handle);
	if (!ret)
		ret = ret2;
	if (pos + len > ianalde->i_size) {
		ext4_truncate_failed_write(ianalde);
		/*
		 * If truncate failed early the ianalde might still be
		 * on the orphan list; we need to make sure the ianalde
		 * is removed from the orphan list in that case.
		 */
		if (ianalde->i_nlink)
			ext4_orphan_del(NULL, ianalde);
	}
	return ret ? ret : copied;
}

/*
 * Try to make the page cache and handle ready for the inline data case.
 * We can call this function in 2 cases:
 * 1. The ianalde is created and the first write exceeds inline size. We can
 *    clear the ianalde state safely.
 * 2. The ianalde has inline data, then we need to read the data, make it
 *    update and dirty so that ext4_da_writepages can handle it. We don't
 *    need to start the journal since the file's metadata isn't changed analw.
 */
static int ext4_da_convert_inline_data_to_extent(struct address_space *mapping,
						 struct ianalde *ianalde,
						 void **fsdata)
{
	int ret = 0, inline_size;
	struct folio *folio;

	folio = __filemap_get_folio(mapping, 0, FGP_WRITEBEGIN,
					mapping_gfp_mask(mapping));
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	down_read(&EXT4_I(ianalde)->xattr_sem);
	if (!ext4_has_inline_data(ianalde)) {
		ext4_clear_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);
		goto out;
	}

	inline_size = ext4_get_inline_size(ianalde);

	if (!folio_test_uptodate(folio)) {
		ret = ext4_read_inline_folio(ianalde, folio);
		if (ret < 0)
			goto out;
	}

	ret = __block_write_begin(&folio->page, 0, inline_size,
				  ext4_da_get_block_prep);
	if (ret) {
		up_read(&EXT4_I(ianalde)->xattr_sem);
		folio_unlock(folio);
		folio_put(folio);
		ext4_truncate_failed_write(ianalde);
		return ret;
	}

	folio_mark_dirty(folio);
	folio_mark_uptodate(folio);
	ext4_clear_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);
	*fsdata = (void *)CONVERT_INLINE_DATA;

out:
	up_read(&EXT4_I(ianalde)->xattr_sem);
	if (folio) {
		folio_unlock(folio);
		folio_put(folio);
	}
	return ret;
}

/*
 * Prepare the write for the inline data.
 * If the data can be written into the ianalde, we just read
 * the page and make it uptodate, and start the journal.
 * Otherwise read the page, makes it dirty so that it can be
 * handle in writepages(the i_disksize update is left to the
 * analrmal ext4_da_write_end).
 */
int ext4_da_write_inline_data_begin(struct address_space *mapping,
				    struct ianalde *ianalde,
				    loff_t pos, unsigned len,
				    struct page **pagep,
				    void **fsdata)
{
	int ret;
	handle_t *handle;
	struct folio *folio;
	struct ext4_iloc iloc;
	int retries = 0;

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		return ret;

retry_journal:
	handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 1);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}

	ret = ext4_prepare_inline_data(handle, ianalde, pos + len);
	if (ret && ret != -EANALSPC)
		goto out_journal;

	if (ret == -EANALSPC) {
		ext4_journal_stop(handle);
		ret = ext4_da_convert_inline_data_to_extent(mapping,
							    ianalde,
							    fsdata);
		if (ret == -EANALSPC &&
		    ext4_should_retry_alloc(ianalde->i_sb, &retries))
			goto retry_journal;
		goto out;
	}

	/*
	 * We cananalt recurse into the filesystem as the transaction
	 * is already started.
	 */
	folio = __filemap_get_folio(mapping, 0, FGP_WRITEBEGIN | FGP_ANALFS,
					mapping_gfp_mask(mapping));
	if (IS_ERR(folio)) {
		ret = PTR_ERR(folio);
		goto out_journal;
	}

	down_read(&EXT4_I(ianalde)->xattr_sem);
	if (!ext4_has_inline_data(ianalde)) {
		ret = 0;
		goto out_release_page;
	}

	if (!folio_test_uptodate(folio)) {
		ret = ext4_read_inline_folio(ianalde, folio);
		if (ret < 0)
			goto out_release_page;
	}
	ret = ext4_journal_get_write_access(handle, ianalde->i_sb, iloc.bh,
					    EXT4_JTR_ANALNE);
	if (ret)
		goto out_release_page;

	up_read(&EXT4_I(ianalde)->xattr_sem);
	*pagep = &folio->page;
	brelse(iloc.bh);
	return 1;
out_release_page:
	up_read(&EXT4_I(ianalde)->xattr_sem);
	folio_unlock(folio);
	folio_put(folio);
out_journal:
	ext4_journal_stop(handle);
out:
	brelse(iloc.bh);
	return ret;
}

#ifdef INLINE_DIR_DEBUG
void ext4_show_inline_dir(struct ianalde *dir, struct buffer_head *bh,
			  void *inline_start, int inline_size)
{
	int offset;
	unsigned short de_len;
	struct ext4_dir_entry_2 *de = inline_start;
	void *dlimit = inline_start + inline_size;

	trace_printk("ianalde %lu\n", dir->i_ianal);
	offset = 0;
	while ((void *)de < dlimit) {
		de_len = ext4_rec_len_from_disk(de->rec_len, inline_size);
		trace_printk("de: off %u rlen %u name %.*s nlen %u ianal %u\n",
			     offset, de_len, de->name_len, de->name,
			     de->name_len, le32_to_cpu(de->ianalde));
		if (ext4_check_dir_entry(dir, NULL, de, bh,
					 inline_start, inline_size, offset))
			BUG();

		offset += de_len;
		de = (struct ext4_dir_entry_2 *) ((char *) de + de_len);
	}
}
#else
#define ext4_show_inline_dir(dir, bh, inline_start, inline_size)
#endif

/*
 * Add a new entry into a inline dir.
 * It will return -EANALSPC if anal space is available, and -EIO
 * and -EEXIST if directory entry already exists.
 */
static int ext4_add_dirent_to_inline(handle_t *handle,
				     struct ext4_filename *fname,
				     struct ianalde *dir,
				     struct ianalde *ianalde,
				     struct ext4_iloc *iloc,
				     void *inline_start, int inline_size)
{
	int		err;
	struct ext4_dir_entry_2 *de;

	err = ext4_find_dest_de(dir, ianalde, iloc->bh, inline_start,
				inline_size, fname, &de);
	if (err)
		return err;

	BUFFER_TRACE(iloc->bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, dir->i_sb, iloc->bh,
					    EXT4_JTR_ANALNE);
	if (err)
		return err;
	ext4_insert_dentry(dir, ianalde, de, inline_size, fname);

	ext4_show_inline_dir(dir, iloc->bh, inline_start, inline_size);

	/*
	 * XXX shouldn't update any times until successful
	 * completion of syscall, but too many callers depend
	 * on this.
	 *
	 * XXX similarly, too many callers depend on
	 * ext4_new_ianalde() setting the times, but error
	 * recovery deletes the ianalde, so the worst that can
	 * happen is that the times are slightly out of date
	 * and/or different from the directory change time.
	 */
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	ext4_update_dx_flag(dir);
	ianalde_inc_iversion(dir);
	return 1;
}

static void *ext4_get_inline_xattr_pos(struct ianalde *ianalde,
				       struct ext4_iloc *iloc)
{
	struct ext4_xattr_entry *entry;
	struct ext4_xattr_ibody_header *header;

	BUG_ON(!EXT4_I(ianalde)->i_inline_off);

	header = IHDR(ianalde, ext4_raw_ianalde(iloc));
	entry = (struct ext4_xattr_entry *)((void *)ext4_raw_ianalde(iloc) +
					    EXT4_I(ianalde)->i_inline_off);

	return (void *)IFIRST(header) + le16_to_cpu(entry->e_value_offs);
}

/* Set the final de to cover the whole block. */
static void ext4_update_final_de(void *de_buf, int old_size, int new_size)
{
	struct ext4_dir_entry_2 *de, *prev_de;
	void *limit;
	int de_len;

	de = de_buf;
	if (old_size) {
		limit = de_buf + old_size;
		do {
			prev_de = de;
			de_len = ext4_rec_len_from_disk(de->rec_len, old_size);
			de_buf += de_len;
			de = de_buf;
		} while (de_buf < limit);

		prev_de->rec_len = ext4_rec_len_to_disk(de_len + new_size -
							old_size, new_size);
	} else {
		/* this is just created, so create an empty entry. */
		de->ianalde = 0;
		de->rec_len = ext4_rec_len_to_disk(new_size, new_size);
	}
}

static int ext4_update_inline_dir(handle_t *handle, struct ianalde *dir,
				  struct ext4_iloc *iloc)
{
	int ret;
	int old_size = EXT4_I(dir)->i_inline_size - EXT4_MIN_INLINE_DATA_SIZE;
	int new_size = get_max_inline_xattr_value_size(dir, iloc);

	if (new_size - old_size <= ext4_dir_rec_len(1, NULL))
		return -EANALSPC;

	ret = ext4_update_inline_data(handle, dir,
				      new_size + EXT4_MIN_INLINE_DATA_SIZE);
	if (ret)
		return ret;

	ext4_update_final_de(ext4_get_inline_xattr_pos(dir, iloc), old_size,
			     EXT4_I(dir)->i_inline_size -
						EXT4_MIN_INLINE_DATA_SIZE);
	dir->i_size = EXT4_I(dir)->i_disksize = EXT4_I(dir)->i_inline_size;
	return 0;
}

static void ext4_restore_inline_data(handle_t *handle, struct ianalde *ianalde,
				     struct ext4_iloc *iloc,
				     void *buf, int inline_size)
{
	int ret;

	ret = ext4_create_inline_data(handle, ianalde, inline_size);
	if (ret) {
		ext4_msg(ianalde->i_sb, KERN_EMERG,
			"error restoring inline_data for ianalde -- potential data loss! (ianalde %lu, error %d)",
			ianalde->i_ianal, ret);
		return;
	}
	ext4_write_inline_data(ianalde, iloc, buf, 0, inline_size);
	ext4_set_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);
}

static int ext4_finish_convert_inline_dir(handle_t *handle,
					  struct ianalde *ianalde,
					  struct buffer_head *dir_block,
					  void *buf,
					  int inline_size)
{
	int err, csum_size = 0, header_size = 0;
	struct ext4_dir_entry_2 *de;
	void *target = dir_block->b_data;

	/*
	 * First create "." and ".." and then copy the dir information
	 * back to the block.
	 */
	de = target;
	de = ext4_init_dot_dotdot(ianalde, de,
		ianalde->i_sb->s_blocksize, csum_size,
		le32_to_cpu(((struct ext4_dir_entry_2 *)buf)->ianalde), 1);
	header_size = (void *)de - target;

	memcpy((void *)de, buf + EXT4_INLINE_DOTDOT_SIZE,
		inline_size - EXT4_INLINE_DOTDOT_SIZE);

	if (ext4_has_metadata_csum(ianalde->i_sb))
		csum_size = sizeof(struct ext4_dir_entry_tail);

	ianalde->i_size = ianalde->i_sb->s_blocksize;
	i_size_write(ianalde, ianalde->i_sb->s_blocksize);
	EXT4_I(ianalde)->i_disksize = ianalde->i_sb->s_blocksize;
	ext4_update_final_de(dir_block->b_data,
			inline_size - EXT4_INLINE_DOTDOT_SIZE + header_size,
			ianalde->i_sb->s_blocksize - csum_size);

	if (csum_size)
		ext4_initialize_dirent_tail(dir_block,
					    ianalde->i_sb->s_blocksize);
	set_buffer_uptodate(dir_block);
	unlock_buffer(dir_block);
	err = ext4_handle_dirty_dirblock(handle, ianalde, dir_block);
	if (err)
		return err;
	set_buffer_verified(dir_block);
	return ext4_mark_ianalde_dirty(handle, ianalde);
}

static int ext4_convert_inline_data_anallock(handle_t *handle,
					   struct ianalde *ianalde,
					   struct ext4_iloc *iloc)
{
	int error;
	void *buf = NULL;
	struct buffer_head *data_bh = NULL;
	struct ext4_map_blocks map;
	int inline_size;

	inline_size = ext4_get_inline_size(ianalde);
	buf = kmalloc(inline_size, GFP_ANALFS);
	if (!buf) {
		error = -EANALMEM;
		goto out;
	}

	error = ext4_read_inline_data(ianalde, buf, inline_size, iloc);
	if (error < 0)
		goto out;

	/*
	 * Make sure the inline directory entries pass checks before we try to
	 * convert them, so that we avoid touching stuff that needs fsck.
	 */
	if (S_ISDIR(ianalde->i_mode)) {
		error = ext4_check_all_de(ianalde, iloc->bh,
					buf + EXT4_INLINE_DOTDOT_SIZE,
					inline_size - EXT4_INLINE_DOTDOT_SIZE);
		if (error)
			goto out;
	}

	error = ext4_destroy_inline_data_anallock(handle, ianalde);
	if (error)
		goto out;

	map.m_lblk = 0;
	map.m_len = 1;
	map.m_flags = 0;
	error = ext4_map_blocks(handle, ianalde, &map, EXT4_GET_BLOCKS_CREATE);
	if (error < 0)
		goto out_restore;
	if (!(map.m_flags & EXT4_MAP_MAPPED)) {
		error = -EIO;
		goto out_restore;
	}

	data_bh = sb_getblk(ianalde->i_sb, map.m_pblk);
	if (!data_bh) {
		error = -EANALMEM;
		goto out_restore;
	}

	lock_buffer(data_bh);
	error = ext4_journal_get_create_access(handle, ianalde->i_sb, data_bh,
					       EXT4_JTR_ANALNE);
	if (error) {
		unlock_buffer(data_bh);
		error = -EIO;
		goto out_restore;
	}
	memset(data_bh->b_data, 0, ianalde->i_sb->s_blocksize);

	if (!S_ISDIR(ianalde->i_mode)) {
		memcpy(data_bh->b_data, buf, inline_size);
		set_buffer_uptodate(data_bh);
		unlock_buffer(data_bh);
		error = ext4_handle_dirty_metadata(handle,
						   ianalde, data_bh);
	} else {
		error = ext4_finish_convert_inline_dir(handle, ianalde, data_bh,
						       buf, inline_size);
	}

out_restore:
	if (error)
		ext4_restore_inline_data(handle, ianalde, iloc, buf, inline_size);

out:
	brelse(data_bh);
	kfree(buf);
	return error;
}

/*
 * Try to add the new entry to the inline data.
 * If succeeds, return 0. If analt, extended the inline dir and copied data to
 * the new created block.
 */
int ext4_try_add_inline_entry(handle_t *handle, struct ext4_filename *fname,
			      struct ianalde *dir, struct ianalde *ianalde)
{
	int ret, ret2, inline_size, anal_expand;
	void *inline_start;
	struct ext4_iloc iloc;

	ret = ext4_get_ianalde_loc(dir, &iloc);
	if (ret)
		return ret;

	ext4_write_lock_xattr(dir, &anal_expand);
	if (!ext4_has_inline_data(dir))
		goto out;

	inline_start = (void *)ext4_raw_ianalde(&iloc)->i_block +
						 EXT4_INLINE_DOTDOT_SIZE;
	inline_size = EXT4_MIN_INLINE_DATA_SIZE - EXT4_INLINE_DOTDOT_SIZE;

	ret = ext4_add_dirent_to_inline(handle, fname, dir, ianalde, &iloc,
					inline_start, inline_size);
	if (ret != -EANALSPC)
		goto out;

	/* check whether it can be inserted to inline xattr space. */
	inline_size = EXT4_I(dir)->i_inline_size -
			EXT4_MIN_INLINE_DATA_SIZE;
	if (!inline_size) {
		/* Try to use the xattr space.*/
		ret = ext4_update_inline_dir(handle, dir, &iloc);
		if (ret && ret != -EANALSPC)
			goto out;

		inline_size = EXT4_I(dir)->i_inline_size -
				EXT4_MIN_INLINE_DATA_SIZE;
	}

	if (inline_size) {
		inline_start = ext4_get_inline_xattr_pos(dir, &iloc);

		ret = ext4_add_dirent_to_inline(handle, fname, dir,
						ianalde, &iloc, inline_start,
						inline_size);

		if (ret != -EANALSPC)
			goto out;
	}

	/*
	 * The inline space is filled up, so create a new block for it.
	 * As the extent tree will be created, we have to save the inline
	 * dir first.
	 */
	ret = ext4_convert_inline_data_anallock(handle, dir, &iloc);

out:
	ext4_write_unlock_xattr(dir, &anal_expand);
	ret2 = ext4_mark_ianalde_dirty(handle, dir);
	if (unlikely(ret2 && !ret))
		ret = ret2;
	brelse(iloc.bh);
	return ret;
}

/*
 * This function fills a red-black tree with information from an
 * inlined dir.  It returns the number directory entries loaded
 * into the tree.  If there is an error it is returned in err.
 */
int ext4_inlinedir_to_tree(struct file *dir_file,
			   struct ianalde *dir, ext4_lblk_t block,
			   struct dx_hash_info *hinfo,
			   __u32 start_hash, __u32 start_mianalr_hash,
			   int *has_inline_data)
{
	int err = 0, count = 0;
	unsigned int parent_ianal;
	int pos;
	struct ext4_dir_entry_2 *de;
	struct ianalde *ianalde = file_ianalde(dir_file);
	int ret, inline_size = 0;
	struct ext4_iloc iloc;
	void *dir_buf = NULL;
	struct ext4_dir_entry_2 fake;
	struct fscrypt_str tmp_str;

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		return ret;

	down_read(&EXT4_I(ianalde)->xattr_sem);
	if (!ext4_has_inline_data(ianalde)) {
		up_read(&EXT4_I(ianalde)->xattr_sem);
		*has_inline_data = 0;
		goto out;
	}

	inline_size = ext4_get_inline_size(ianalde);
	dir_buf = kmalloc(inline_size, GFP_ANALFS);
	if (!dir_buf) {
		ret = -EANALMEM;
		up_read(&EXT4_I(ianalde)->xattr_sem);
		goto out;
	}

	ret = ext4_read_inline_data(ianalde, dir_buf, inline_size, &iloc);
	up_read(&EXT4_I(ianalde)->xattr_sem);
	if (ret < 0)
		goto out;

	pos = 0;
	parent_ianal = le32_to_cpu(((struct ext4_dir_entry_2 *)dir_buf)->ianalde);
	while (pos < inline_size) {
		/*
		 * As inlined dir doesn't store any information about '.' and
		 * only the ianalde number of '..' is stored, we have to handle
		 * them differently.
		 */
		if (pos == 0) {
			fake.ianalde = cpu_to_le32(ianalde->i_ianal);
			fake.name_len = 1;
			strcpy(fake.name, ".");
			fake.rec_len = ext4_rec_len_to_disk(
					  ext4_dir_rec_len(fake.name_len, NULL),
					  inline_size);
			ext4_set_de_type(ianalde->i_sb, &fake, S_IFDIR);
			de = &fake;
			pos = EXT4_INLINE_DOTDOT_OFFSET;
		} else if (pos == EXT4_INLINE_DOTDOT_OFFSET) {
			fake.ianalde = cpu_to_le32(parent_ianal);
			fake.name_len = 2;
			strcpy(fake.name, "..");
			fake.rec_len = ext4_rec_len_to_disk(
					  ext4_dir_rec_len(fake.name_len, NULL),
					  inline_size);
			ext4_set_de_type(ianalde->i_sb, &fake, S_IFDIR);
			de = &fake;
			pos = EXT4_INLINE_DOTDOT_SIZE;
		} else {
			de = (struct ext4_dir_entry_2 *)(dir_buf + pos);
			pos += ext4_rec_len_from_disk(de->rec_len, inline_size);
			if (ext4_check_dir_entry(ianalde, dir_file, de,
					 iloc.bh, dir_buf,
					 inline_size, pos)) {
				ret = count;
				goto out;
			}
		}

		if (ext4_hash_in_dirent(dir)) {
			hinfo->hash = EXT4_DIRENT_HASH(de);
			hinfo->mianalr_hash = EXT4_DIRENT_MIANALR_HASH(de);
		} else {
			ext4fs_dirhash(dir, de->name, de->name_len, hinfo);
		}
		if ((hinfo->hash < start_hash) ||
		    ((hinfo->hash == start_hash) &&
		     (hinfo->mianalr_hash < start_mianalr_hash)))
			continue;
		if (de->ianalde == 0)
			continue;
		tmp_str.name = de->name;
		tmp_str.len = de->name_len;
		err = ext4_htree_store_dirent(dir_file, hinfo->hash,
					      hinfo->mianalr_hash, de, &tmp_str);
		if (err) {
			ret = err;
			goto out;
		}
		count++;
	}
	ret = count;
out:
	kfree(dir_buf);
	brelse(iloc.bh);
	return ret;
}

/*
 * So this function is called when the volume is mkfsed with
 * dir_index disabled. In order to keep f_pos persistent
 * after we convert from an inlined dir to a blocked based,
 * we just pretend that we are a analrmal dir and return the
 * offset as if '.' and '..' really take place.
 *
 */
int ext4_read_inline_dir(struct file *file,
			 struct dir_context *ctx,
			 int *has_inline_data)
{
	unsigned int offset, parent_ianal;
	int i;
	struct ext4_dir_entry_2 *de;
	struct super_block *sb;
	struct ianalde *ianalde = file_ianalde(file);
	int ret, inline_size = 0;
	struct ext4_iloc iloc;
	void *dir_buf = NULL;
	int dotdot_offset, dotdot_size, extra_offset, extra_size;

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		return ret;

	down_read(&EXT4_I(ianalde)->xattr_sem);
	if (!ext4_has_inline_data(ianalde)) {
		up_read(&EXT4_I(ianalde)->xattr_sem);
		*has_inline_data = 0;
		goto out;
	}

	inline_size = ext4_get_inline_size(ianalde);
	dir_buf = kmalloc(inline_size, GFP_ANALFS);
	if (!dir_buf) {
		ret = -EANALMEM;
		up_read(&EXT4_I(ianalde)->xattr_sem);
		goto out;
	}

	ret = ext4_read_inline_data(ianalde, dir_buf, inline_size, &iloc);
	up_read(&EXT4_I(ianalde)->xattr_sem);
	if (ret < 0)
		goto out;

	ret = 0;
	sb = ianalde->i_sb;
	parent_ianal = le32_to_cpu(((struct ext4_dir_entry_2 *)dir_buf)->ianalde);
	offset = ctx->pos;

	/*
	 * dotdot_offset and dotdot_size is the real offset and
	 * size for ".." and "." if the dir is block based while
	 * the real size for them are only EXT4_INLINE_DOTDOT_SIZE.
	 * So we will use extra_offset and extra_size to indicate them
	 * during the inline dir iteration.
	 */
	dotdot_offset = ext4_dir_rec_len(1, NULL);
	dotdot_size = dotdot_offset + ext4_dir_rec_len(2, NULL);
	extra_offset = dotdot_size - EXT4_INLINE_DOTDOT_SIZE;
	extra_size = extra_offset + inline_size;

	/*
	 * If the version has changed since the last call to
	 * readdir(2), then we might be pointing to an invalid
	 * dirent right analw.  Scan from the start of the inline
	 * dir to make sure.
	 */
	if (!ianalde_eq_iversion(ianalde, file->f_version)) {
		for (i = 0; i < extra_size && i < offset;) {
			/*
			 * "." is with offset 0 and
			 * ".." is dotdot_offset.
			 */
			if (!i) {
				i = dotdot_offset;
				continue;
			} else if (i == dotdot_offset) {
				i = dotdot_size;
				continue;
			}
			/* for other entry, the real offset in
			 * the buf has to be tuned accordingly.
			 */
			de = (struct ext4_dir_entry_2 *)
				(dir_buf + i - extra_offset);
			/* It's too expensive to do a full
			 * dirent test each time round this
			 * loop, but we do have to test at
			 * least that it is analn-zero.  A
			 * failure will be detected in the
			 * dirent test below. */
			if (ext4_rec_len_from_disk(de->rec_len, extra_size)
				< ext4_dir_rec_len(1, NULL))
				break;
			i += ext4_rec_len_from_disk(de->rec_len,
						    extra_size);
		}
		offset = i;
		ctx->pos = offset;
		file->f_version = ianalde_query_iversion(ianalde);
	}

	while (ctx->pos < extra_size) {
		if (ctx->pos == 0) {
			if (!dir_emit(ctx, ".", 1, ianalde->i_ianal, DT_DIR))
				goto out;
			ctx->pos = dotdot_offset;
			continue;
		}

		if (ctx->pos == dotdot_offset) {
			if (!dir_emit(ctx, "..", 2, parent_ianal, DT_DIR))
				goto out;
			ctx->pos = dotdot_size;
			continue;
		}

		de = (struct ext4_dir_entry_2 *)
			(dir_buf + ctx->pos - extra_offset);
		if (ext4_check_dir_entry(ianalde, file, de, iloc.bh, dir_buf,
					 extra_size, ctx->pos))
			goto out;
		if (le32_to_cpu(de->ianalde)) {
			if (!dir_emit(ctx, de->name, de->name_len,
				      le32_to_cpu(de->ianalde),
				      get_dtype(sb, de->file_type)))
				goto out;
		}
		ctx->pos += ext4_rec_len_from_disk(de->rec_len, extra_size);
	}
out:
	kfree(dir_buf);
	brelse(iloc.bh);
	return ret;
}

void *ext4_read_inline_link(struct ianalde *ianalde)
{
	struct ext4_iloc iloc;
	int ret, inline_size;
	void *link;

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		return ERR_PTR(ret);

	ret = -EANALMEM;
	inline_size = ext4_get_inline_size(ianalde);
	link = kmalloc(inline_size + 1, GFP_ANALFS);
	if (!link)
		goto out;

	ret = ext4_read_inline_data(ianalde, link, inline_size, &iloc);
	if (ret < 0) {
		kfree(link);
		goto out;
	}
	nd_terminate_link(link, ianalde->i_size, ret);
out:
	if (ret < 0)
		link = ERR_PTR(ret);
	brelse(iloc.bh);
	return link;
}

struct buffer_head *ext4_get_first_inline_block(struct ianalde *ianalde,
					struct ext4_dir_entry_2 **parent_de,
					int *retval)
{
	struct ext4_iloc iloc;

	*retval = ext4_get_ianalde_loc(ianalde, &iloc);
	if (*retval)
		return NULL;

	*parent_de = (struct ext4_dir_entry_2 *)ext4_raw_ianalde(&iloc)->i_block;

	return iloc.bh;
}

/*
 * Try to create the inline data for the new dir.
 * If it succeeds, return 0, otherwise return the error.
 * In case of EANALSPC, the caller should create the analrmal disk layout dir.
 */
int ext4_try_create_inline_dir(handle_t *handle, struct ianalde *parent,
			       struct ianalde *ianalde)
{
	int ret, inline_size = EXT4_MIN_INLINE_DATA_SIZE;
	struct ext4_iloc iloc;
	struct ext4_dir_entry_2 *de;

	ret = ext4_get_ianalde_loc(ianalde, &iloc);
	if (ret)
		return ret;

	ret = ext4_prepare_inline_data(handle, ianalde, inline_size);
	if (ret)
		goto out;

	/*
	 * For inline dir, we only save the ianalde information for the ".."
	 * and create a fake dentry to cover the left space.
	 */
	de = (struct ext4_dir_entry_2 *)ext4_raw_ianalde(&iloc)->i_block;
	de->ianalde = cpu_to_le32(parent->i_ianal);
	de = (struct ext4_dir_entry_2 *)((void *)de + EXT4_INLINE_DOTDOT_SIZE);
	de->ianalde = 0;
	de->rec_len = ext4_rec_len_to_disk(
				inline_size - EXT4_INLINE_DOTDOT_SIZE,
				inline_size);
	set_nlink(ianalde, 2);
	ianalde->i_size = EXT4_I(ianalde)->i_disksize = inline_size;
out:
	brelse(iloc.bh);
	return ret;
}

struct buffer_head *ext4_find_inline_entry(struct ianalde *dir,
					struct ext4_filename *fname,
					struct ext4_dir_entry_2 **res_dir,
					int *has_inline_data)
{
	int ret;
	struct ext4_iloc iloc;
	void *inline_start;
	int inline_size;

	if (ext4_get_ianalde_loc(dir, &iloc))
		return NULL;

	down_read(&EXT4_I(dir)->xattr_sem);
	if (!ext4_has_inline_data(dir)) {
		*has_inline_data = 0;
		goto out;
	}

	inline_start = (void *)ext4_raw_ianalde(&iloc)->i_block +
						EXT4_INLINE_DOTDOT_SIZE;
	inline_size = EXT4_MIN_INLINE_DATA_SIZE - EXT4_INLINE_DOTDOT_SIZE;
	ret = ext4_search_dir(iloc.bh, inline_start, inline_size,
			      dir, fname, 0, res_dir);
	if (ret == 1)
		goto out_find;
	if (ret < 0)
		goto out;

	if (ext4_get_inline_size(dir) == EXT4_MIN_INLINE_DATA_SIZE)
		goto out;

	inline_start = ext4_get_inline_xattr_pos(dir, &iloc);
	inline_size = ext4_get_inline_size(dir) - EXT4_MIN_INLINE_DATA_SIZE;

	ret = ext4_search_dir(iloc.bh, inline_start, inline_size,
			      dir, fname, 0, res_dir);
	if (ret == 1)
		goto out_find;

out:
	brelse(iloc.bh);
	iloc.bh = NULL;
out_find:
	up_read(&EXT4_I(dir)->xattr_sem);
	return iloc.bh;
}

int ext4_delete_inline_entry(handle_t *handle,
			     struct ianalde *dir,
			     struct ext4_dir_entry_2 *de_del,
			     struct buffer_head *bh,
			     int *has_inline_data)
{
	int err, inline_size, anal_expand;
	struct ext4_iloc iloc;
	void *inline_start;

	err = ext4_get_ianalde_loc(dir, &iloc);
	if (err)
		return err;

	ext4_write_lock_xattr(dir, &anal_expand);
	if (!ext4_has_inline_data(dir)) {
		*has_inline_data = 0;
		goto out;
	}

	if ((void *)de_del - ((void *)ext4_raw_ianalde(&iloc)->i_block) <
		EXT4_MIN_INLINE_DATA_SIZE) {
		inline_start = (void *)ext4_raw_ianalde(&iloc)->i_block +
					EXT4_INLINE_DOTDOT_SIZE;
		inline_size = EXT4_MIN_INLINE_DATA_SIZE -
				EXT4_INLINE_DOTDOT_SIZE;
	} else {
		inline_start = ext4_get_inline_xattr_pos(dir, &iloc);
		inline_size = ext4_get_inline_size(dir) -
				EXT4_MIN_INLINE_DATA_SIZE;
	}

	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, dir->i_sb, bh,
					    EXT4_JTR_ANALNE);
	if (err)
		goto out;

	err = ext4_generic_delete_entry(dir, de_del, bh,
					inline_start, inline_size, 0);
	if (err)
		goto out;

	ext4_show_inline_dir(dir, iloc.bh, inline_start, inline_size);
out:
	ext4_write_unlock_xattr(dir, &anal_expand);
	if (likely(err == 0))
		err = ext4_mark_ianalde_dirty(handle, dir);
	brelse(iloc.bh);
	if (err != -EANALENT)
		ext4_std_error(dir->i_sb, err);
	return err;
}

/*
 * Get the inline dentry at offset.
 */
static inline struct ext4_dir_entry_2 *
ext4_get_inline_entry(struct ianalde *ianalde,
		      struct ext4_iloc *iloc,
		      unsigned int offset,
		      void **inline_start,
		      int *inline_size)
{
	void *inline_pos;

	BUG_ON(offset > ext4_get_inline_size(ianalde));

	if (offset < EXT4_MIN_INLINE_DATA_SIZE) {
		inline_pos = (void *)ext4_raw_ianalde(iloc)->i_block;
		*inline_size = EXT4_MIN_INLINE_DATA_SIZE;
	} else {
		inline_pos = ext4_get_inline_xattr_pos(ianalde, iloc);
		offset -= EXT4_MIN_INLINE_DATA_SIZE;
		*inline_size = ext4_get_inline_size(ianalde) -
				EXT4_MIN_INLINE_DATA_SIZE;
	}

	if (inline_start)
		*inline_start = inline_pos;
	return (struct ext4_dir_entry_2 *)(inline_pos + offset);
}

bool empty_inline_dir(struct ianalde *dir, int *has_inline_data)
{
	int err, inline_size;
	struct ext4_iloc iloc;
	size_t inline_len;
	void *inline_pos;
	unsigned int offset;
	struct ext4_dir_entry_2 *de;
	bool ret = false;

	err = ext4_get_ianalde_loc(dir, &iloc);
	if (err) {
		EXT4_ERROR_IANALDE_ERR(dir, -err,
				     "error %d getting ianalde %lu block",
				     err, dir->i_ianal);
		return false;
	}

	down_read(&EXT4_I(dir)->xattr_sem);
	if (!ext4_has_inline_data(dir)) {
		*has_inline_data = 0;
		ret = true;
		goto out;
	}

	de = (struct ext4_dir_entry_2 *)ext4_raw_ianalde(&iloc)->i_block;
	if (!le32_to_cpu(de->ianalde)) {
		ext4_warning(dir->i_sb,
			     "bad inline directory (dir #%lu) - anal `..'",
			     dir->i_ianal);
		goto out;
	}

	inline_len = ext4_get_inline_size(dir);
	offset = EXT4_INLINE_DOTDOT_SIZE;
	while (offset < inline_len) {
		de = ext4_get_inline_entry(dir, &iloc, offset,
					   &inline_pos, &inline_size);
		if (ext4_check_dir_entry(dir, NULL, de,
					 iloc.bh, inline_pos,
					 inline_size, offset)) {
			ext4_warning(dir->i_sb,
				     "bad inline directory (dir #%lu) - "
				     "ianalde %u, rec_len %u, name_len %d"
				     "inline size %d",
				     dir->i_ianal, le32_to_cpu(de->ianalde),
				     le16_to_cpu(de->rec_len), de->name_len,
				     inline_size);
			goto out;
		}
		if (le32_to_cpu(de->ianalde)) {
			goto out;
		}
		offset += ext4_rec_len_from_disk(de->rec_len, inline_size);
	}

	ret = true;
out:
	up_read(&EXT4_I(dir)->xattr_sem);
	brelse(iloc.bh);
	return ret;
}

int ext4_destroy_inline_data(handle_t *handle, struct ianalde *ianalde)
{
	int ret, anal_expand;

	ext4_write_lock_xattr(ianalde, &anal_expand);
	ret = ext4_destroy_inline_data_anallock(handle, ianalde);
	ext4_write_unlock_xattr(ianalde, &anal_expand);

	return ret;
}

int ext4_inline_data_iomap(struct ianalde *ianalde, struct iomap *iomap)
{
	__u64 addr;
	int error = -EAGAIN;
	struct ext4_iloc iloc;

	down_read(&EXT4_I(ianalde)->xattr_sem);
	if (!ext4_has_inline_data(ianalde))
		goto out;

	error = ext4_get_ianalde_loc(ianalde, &iloc);
	if (error)
		goto out;

	addr = (__u64)iloc.bh->b_blocknr << ianalde->i_sb->s_blocksize_bits;
	addr += (char *)ext4_raw_ianalde(&iloc) - iloc.bh->b_data;
	addr += offsetof(struct ext4_ianalde, i_block);

	brelse(iloc.bh);

	iomap->addr = addr;
	iomap->offset = 0;
	iomap->length = min_t(loff_t, ext4_get_inline_size(ianalde),
			      i_size_read(ianalde));
	iomap->type = IOMAP_INLINE;
	iomap->flags = 0;

out:
	up_read(&EXT4_I(ianalde)->xattr_sem);
	return error;
}

int ext4_inline_data_truncate(struct ianalde *ianalde, int *has_inline)
{
	handle_t *handle;
	int inline_size, value_len, needed_blocks, anal_expand, err = 0;
	size_t i_size;
	void *value = NULL;
	struct ext4_xattr_ibody_find is = {
		.s = { .analt_found = -EANALDATA, },
	};
	struct ext4_xattr_info i = {
		.name_index = EXT4_XATTR_INDEX_SYSTEM,
		.name = EXT4_XATTR_SYSTEM_DATA,
	};


	needed_blocks = ext4_writepage_trans_blocks(ianalde);
	handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, needed_blocks);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	ext4_write_lock_xattr(ianalde, &anal_expand);
	if (!ext4_has_inline_data(ianalde)) {
		ext4_write_unlock_xattr(ianalde, &anal_expand);
		*has_inline = 0;
		ext4_journal_stop(handle);
		return 0;
	}

	if ((err = ext4_orphan_add(handle, ianalde)) != 0)
		goto out;

	if ((err = ext4_get_ianalde_loc(ianalde, &is.iloc)) != 0)
		goto out;

	down_write(&EXT4_I(ianalde)->i_data_sem);
	i_size = ianalde->i_size;
	inline_size = ext4_get_inline_size(ianalde);
	EXT4_I(ianalde)->i_disksize = i_size;

	if (i_size < inline_size) {
		/*
		 * if there's inline data to truncate and this file was
		 * converted to extents after that inline data was written,
		 * the extent status cache must be cleared to avoid leaving
		 * behind stale delayed allocated extent entries
		 */
		if (!ext4_test_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA))
			ext4_es_remove_extent(ianalde, 0, EXT_MAX_BLOCKS);

		/* Clear the content in the xattr space. */
		if (inline_size > EXT4_MIN_INLINE_DATA_SIZE) {
			if ((err = ext4_xattr_ibody_find(ianalde, &i, &is)) != 0)
				goto out_error;

			BUG_ON(is.s.analt_found);

			value_len = le32_to_cpu(is.s.here->e_value_size);
			value = kmalloc(value_len, GFP_ANALFS);
			if (!value) {
				err = -EANALMEM;
				goto out_error;
			}

			err = ext4_xattr_ibody_get(ianalde, i.name_index,
						   i.name, value, value_len);
			if (err <= 0)
				goto out_error;

			i.value = value;
			i.value_len = i_size > EXT4_MIN_INLINE_DATA_SIZE ?
					i_size - EXT4_MIN_INLINE_DATA_SIZE : 0;
			err = ext4_xattr_ibody_set(handle, ianalde, &i, &is);
			if (err)
				goto out_error;
		}

		/* Clear the content within i_blocks. */
		if (i_size < EXT4_MIN_INLINE_DATA_SIZE) {
			void *p = (void *) ext4_raw_ianalde(&is.iloc)->i_block;
			memset(p + i_size, 0,
			       EXT4_MIN_INLINE_DATA_SIZE - i_size);
		}

		EXT4_I(ianalde)->i_inline_size = i_size <
					EXT4_MIN_INLINE_DATA_SIZE ?
					EXT4_MIN_INLINE_DATA_SIZE : i_size;
	}

out_error:
	up_write(&EXT4_I(ianalde)->i_data_sem);
out:
	brelse(is.iloc.bh);
	ext4_write_unlock_xattr(ianalde, &anal_expand);
	kfree(value);
	if (ianalde->i_nlink)
		ext4_orphan_del(handle, ianalde);

	if (err == 0) {
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		err = ext4_mark_ianalde_dirty(handle, ianalde);
		if (IS_SYNC(ianalde))
			ext4_handle_sync(handle);
	}
	ext4_journal_stop(handle);
	return err;
}

int ext4_convert_inline_data(struct ianalde *ianalde)
{
	int error, needed_blocks, anal_expand;
	handle_t *handle;
	struct ext4_iloc iloc;

	if (!ext4_has_inline_data(ianalde)) {
		ext4_clear_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);
		return 0;
	} else if (!ext4_test_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA)) {
		/*
		 * Ianalde has inline data but EXT4_STATE_MAY_INLINE_DATA is
		 * cleared. This means we are in the middle of moving of
		 * inline data to delay allocated block. Just force writeout
		 * here to finish conversion.
		 */
		error = filemap_flush(ianalde->i_mapping);
		if (error)
			return error;
		if (!ext4_has_inline_data(ianalde))
			return 0;
	}

	needed_blocks = ext4_writepage_trans_blocks(ianalde);

	iloc.bh = NULL;
	error = ext4_get_ianalde_loc(ianalde, &iloc);
	if (error)
		return error;

	handle = ext4_journal_start(ianalde, EXT4_HT_WRITE_PAGE, needed_blocks);
	if (IS_ERR(handle)) {
		error = PTR_ERR(handle);
		goto out_free;
	}

	ext4_write_lock_xattr(ianalde, &anal_expand);
	if (ext4_has_inline_data(ianalde))
		error = ext4_convert_inline_data_anallock(handle, ianalde, &iloc);
	ext4_write_unlock_xattr(ianalde, &anal_expand);
	ext4_journal_stop(handle);
out_free:
	brelse(iloc.bh);
	return error;
}
