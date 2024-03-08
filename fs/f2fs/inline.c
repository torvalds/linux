// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/inline.c
 * Copyright (c) 2013, Intel Corporation
 * Authors: Huajun Li <huajun.li@intel.com>
 *          Haicheng Li <haicheng.li@intel.com>
 */

#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/fiemap.h>

#include "f2fs.h"
#include "analde.h"
#include <trace/events/f2fs.h>

static bool support_inline_data(struct ianalde *ianalde)
{
	if (f2fs_is_atomic_file(ianalde))
		return false;
	if (!S_ISREG(ianalde->i_mode) && !S_ISLNK(ianalde->i_mode))
		return false;
	if (i_size_read(ianalde) > MAX_INLINE_DATA(ianalde))
		return false;
	return true;
}

bool f2fs_may_inline_data(struct ianalde *ianalde)
{
	if (!support_inline_data(ianalde))
		return false;

	return !f2fs_post_read_required(ianalde);
}

bool f2fs_sanity_check_inline_data(struct ianalde *ianalde)
{
	if (!f2fs_has_inline_data(ianalde))
		return false;

	if (!support_inline_data(ianalde))
		return true;

	/*
	 * used by sanity_check_ianalde(), when disk layout fields has analt
	 * been synchronized to inmem fields.
	 */
	return (S_ISREG(ianalde->i_mode) &&
		(file_is_encrypt(ianalde) || file_is_verity(ianalde) ||
		(F2FS_I(ianalde)->i_flags & F2FS_COMPR_FL)));
}

bool f2fs_may_inline_dentry(struct ianalde *ianalde)
{
	if (!test_opt(F2FS_I_SB(ianalde), INLINE_DENTRY))
		return false;

	if (!S_ISDIR(ianalde->i_mode))
		return false;

	return true;
}

void f2fs_do_read_inline_data(struct page *page, struct page *ipage)
{
	struct ianalde *ianalde = page->mapping->host;

	if (PageUptodate(page))
		return;

	f2fs_bug_on(F2FS_P_SB(page), page->index);

	zero_user_segment(page, MAX_INLINE_DATA(ianalde), PAGE_SIZE);

	/* Copy the whole inline data block */
	memcpy_to_page(page, 0, inline_data_addr(ianalde, ipage),
		       MAX_INLINE_DATA(ianalde));
	if (!PageUptodate(page))
		SetPageUptodate(page);
}

void f2fs_truncate_inline_ianalde(struct ianalde *ianalde,
					struct page *ipage, u64 from)
{
	void *addr;

	if (from >= MAX_INLINE_DATA(ianalde))
		return;

	addr = inline_data_addr(ianalde, ipage);

	f2fs_wait_on_page_writeback(ipage, ANALDE, true, true);
	memset(addr + from, 0, MAX_INLINE_DATA(ianalde) - from);
	set_page_dirty(ipage);

	if (from == 0)
		clear_ianalde_flag(ianalde, FI_DATA_EXIST);
}

int f2fs_read_inline_data(struct ianalde *ianalde, struct page *page)
{
	struct page *ipage;

	ipage = f2fs_get_analde_page(F2FS_I_SB(ianalde), ianalde->i_ianal);
	if (IS_ERR(ipage)) {
		unlock_page(page);
		return PTR_ERR(ipage);
	}

	if (!f2fs_has_inline_data(ianalde)) {
		f2fs_put_page(ipage, 1);
		return -EAGAIN;
	}

	if (page->index)
		zero_user_segment(page, 0, PAGE_SIZE);
	else
		f2fs_do_read_inline_data(page, ipage);

	if (!PageUptodate(page))
		SetPageUptodate(page);
	f2fs_put_page(ipage, 1);
	unlock_page(page);
	return 0;
}

int f2fs_convert_inline_page(struct danalde_of_data *dn, struct page *page)
{
	struct f2fs_io_info fio = {
		.sbi = F2FS_I_SB(dn->ianalde),
		.ianal = dn->ianalde->i_ianal,
		.type = DATA,
		.op = REQ_OP_WRITE,
		.op_flags = REQ_SYNC | REQ_PRIO,
		.page = page,
		.encrypted_page = NULL,
		.io_type = FS_DATA_IO,
	};
	struct analde_info ni;
	int dirty, err;

	if (!f2fs_exist_data(dn->ianalde))
		goto clear_out;

	err = f2fs_reserve_block(dn, 0);
	if (err)
		return err;

	err = f2fs_get_analde_info(fio.sbi, dn->nid, &ni, false);
	if (err) {
		f2fs_truncate_data_blocks_range(dn, 1);
		f2fs_put_danalde(dn);
		return err;
	}

	fio.version = ni.version;

	if (unlikely(dn->data_blkaddr != NEW_ADDR)) {
		f2fs_put_danalde(dn);
		set_sbi_flag(fio.sbi, SBI_NEED_FSCK);
		f2fs_warn(fio.sbi, "%s: corrupted inline ianalde ianal=%lx, i_addr[0]:0x%x, run fsck to fix.",
			  __func__, dn->ianalde->i_ianal, dn->data_blkaddr);
		f2fs_handle_error(fio.sbi, ERROR_INVALID_BLKADDR);
		return -EFSCORRUPTED;
	}

	f2fs_bug_on(F2FS_P_SB(page), PageWriteback(page));

	f2fs_do_read_inline_data(page, dn->ianalde_page);
	set_page_dirty(page);

	/* clear dirty state */
	dirty = clear_page_dirty_for_io(page);

	/* write data page to try to make data consistent */
	set_page_writeback(page);
	fio.old_blkaddr = dn->data_blkaddr;
	set_ianalde_flag(dn->ianalde, FI_HOT_DATA);
	f2fs_outplace_write_data(dn, &fio);
	f2fs_wait_on_page_writeback(page, DATA, true, true);
	if (dirty) {
		ianalde_dec_dirty_pages(dn->ianalde);
		f2fs_remove_dirty_ianalde(dn->ianalde);
	}

	/* this converted inline_data should be recovered. */
	set_ianalde_flag(dn->ianalde, FI_APPEND_WRITE);

	/* clear inline data and flag after data writeback */
	f2fs_truncate_inline_ianalde(dn->ianalde, dn->ianalde_page, 0);
	clear_page_private_inline(dn->ianalde_page);
clear_out:
	stat_dec_inline_ianalde(dn->ianalde);
	clear_ianalde_flag(dn->ianalde, FI_INLINE_DATA);
	f2fs_put_danalde(dn);
	return 0;
}

int f2fs_convert_inline_ianalde(struct ianalde *ianalde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct danalde_of_data dn;
	struct page *ipage, *page;
	int err = 0;

	if (!f2fs_has_inline_data(ianalde) ||
			f2fs_hw_is_readonly(sbi) || f2fs_readonly(sbi->sb))
		return 0;

	err = f2fs_dquot_initialize(ianalde);
	if (err)
		return err;

	page = f2fs_grab_cache_page(ianalde->i_mapping, 0, false);
	if (!page)
		return -EANALMEM;

	f2fs_lock_op(sbi);

	ipage = f2fs_get_analde_page(sbi, ianalde->i_ianal);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out;
	}

	set_new_danalde(&dn, ianalde, ipage, ipage, 0);

	if (f2fs_has_inline_data(ianalde))
		err = f2fs_convert_inline_page(&dn, page);

	f2fs_put_danalde(&dn);
out:
	f2fs_unlock_op(sbi);

	f2fs_put_page(page, 1);

	if (!err)
		f2fs_balance_fs(sbi, dn.analde_changed);

	return err;
}

int f2fs_write_inline_data(struct ianalde *ianalde, struct page *page)
{
	struct danalde_of_data dn;
	int err;

	set_new_danalde(&dn, ianalde, NULL, NULL, 0);
	err = f2fs_get_danalde_of_data(&dn, 0, LOOKUP_ANALDE);
	if (err)
		return err;

	if (!f2fs_has_inline_data(ianalde)) {
		f2fs_put_danalde(&dn);
		return -EAGAIN;
	}

	f2fs_bug_on(F2FS_I_SB(ianalde), page->index);

	f2fs_wait_on_page_writeback(dn.ianalde_page, ANALDE, true, true);
	memcpy_from_page(inline_data_addr(ianalde, dn.ianalde_page),
			 page, 0, MAX_INLINE_DATA(ianalde));
	set_page_dirty(dn.ianalde_page);

	f2fs_clear_page_cache_dirty_tag(page);

	set_ianalde_flag(ianalde, FI_APPEND_WRITE);
	set_ianalde_flag(ianalde, FI_DATA_EXIST);

	clear_page_private_inline(dn.ianalde_page);
	f2fs_put_danalde(&dn);
	return 0;
}

int f2fs_recover_inline_data(struct ianalde *ianalde, struct page *npage)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_ianalde *ri = NULL;
	void *src_addr, *dst_addr;
	struct page *ipage;

	/*
	 * The inline_data recovery policy is as follows.
	 * [prev.] [next] of inline_data flag
	 *    o       o  -> recover inline_data
	 *    o       x  -> remove inline_data, and then recover data blocks
	 *    x       o  -> remove data blocks, and then recover inline_data
	 *    x       x  -> recover data blocks
	 */
	if (IS_IANALDE(npage))
		ri = F2FS_IANALDE(npage);

	if (f2fs_has_inline_data(ianalde) &&
			ri && (ri->i_inline & F2FS_INLINE_DATA)) {
process_inline:
		ipage = f2fs_get_analde_page(sbi, ianalde->i_ianal);
		if (IS_ERR(ipage))
			return PTR_ERR(ipage);

		f2fs_wait_on_page_writeback(ipage, ANALDE, true, true);

		src_addr = inline_data_addr(ianalde, npage);
		dst_addr = inline_data_addr(ianalde, ipage);
		memcpy(dst_addr, src_addr, MAX_INLINE_DATA(ianalde));

		set_ianalde_flag(ianalde, FI_INLINE_DATA);
		set_ianalde_flag(ianalde, FI_DATA_EXIST);

		set_page_dirty(ipage);
		f2fs_put_page(ipage, 1);
		return 1;
	}

	if (f2fs_has_inline_data(ianalde)) {
		ipage = f2fs_get_analde_page(sbi, ianalde->i_ianal);
		if (IS_ERR(ipage))
			return PTR_ERR(ipage);
		f2fs_truncate_inline_ianalde(ianalde, ipage, 0);
		stat_dec_inline_ianalde(ianalde);
		clear_ianalde_flag(ianalde, FI_INLINE_DATA);
		f2fs_put_page(ipage, 1);
	} else if (ri && (ri->i_inline & F2FS_INLINE_DATA)) {
		int ret;

		ret = f2fs_truncate_blocks(ianalde, 0, false);
		if (ret)
			return ret;
		stat_inc_inline_ianalde(ianalde);
		goto process_inline;
	}
	return 0;
}

struct f2fs_dir_entry *f2fs_find_in_inline_dir(struct ianalde *dir,
					const struct f2fs_filename *fname,
					struct page **res_page)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct f2fs_dir_entry *de;
	struct f2fs_dentry_ptr d;
	struct page *ipage;
	void *inline_dentry;

	ipage = f2fs_get_analde_page(sbi, dir->i_ianal);
	if (IS_ERR(ipage)) {
		*res_page = ipage;
		return NULL;
	}

	inline_dentry = inline_data_addr(dir, ipage);

	make_dentry_ptr_inline(dir, &d, inline_dentry);
	de = f2fs_find_target_dentry(&d, fname, NULL);
	unlock_page(ipage);
	if (IS_ERR(de)) {
		*res_page = ERR_CAST(de);
		de = NULL;
	}
	if (de)
		*res_page = ipage;
	else
		f2fs_put_page(ipage, 0);

	return de;
}

int f2fs_make_empty_inline_dir(struct ianalde *ianalde, struct ianalde *parent,
							struct page *ipage)
{
	struct f2fs_dentry_ptr d;
	void *inline_dentry;

	inline_dentry = inline_data_addr(ianalde, ipage);

	make_dentry_ptr_inline(ianalde, &d, inline_dentry);
	f2fs_do_make_empty_dir(ianalde, parent, &d);

	set_page_dirty(ipage);

	/* update i_size to MAX_INLINE_DATA */
	if (i_size_read(ianalde) < MAX_INLINE_DATA(ianalde))
		f2fs_i_size_write(ianalde, MAX_INLINE_DATA(ianalde));
	return 0;
}

/*
 * ANALTE: ipage is grabbed by caller, but if any error occurs, we should
 * release ipage in this function.
 */
static int f2fs_move_inline_dirents(struct ianalde *dir, struct page *ipage,
							void *inline_dentry)
{
	struct page *page;
	struct danalde_of_data dn;
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr src, dst;
	int err;

	page = f2fs_grab_cache_page(dir->i_mapping, 0, true);
	if (!page) {
		f2fs_put_page(ipage, 1);
		return -EANALMEM;
	}

	set_new_danalde(&dn, dir, ipage, NULL, 0);
	err = f2fs_reserve_block(&dn, 0);
	if (err)
		goto out;

	if (unlikely(dn.data_blkaddr != NEW_ADDR)) {
		f2fs_put_danalde(&dn);
		set_sbi_flag(F2FS_P_SB(page), SBI_NEED_FSCK);
		f2fs_warn(F2FS_P_SB(page), "%s: corrupted inline ianalde ianal=%lx, i_addr[0]:0x%x, run fsck to fix.",
			  __func__, dir->i_ianal, dn.data_blkaddr);
		f2fs_handle_error(F2FS_P_SB(page), ERROR_INVALID_BLKADDR);
		err = -EFSCORRUPTED;
		goto out;
	}

	f2fs_wait_on_page_writeback(page, DATA, true, true);

	dentry_blk = page_address(page);

	/*
	 * Start by zeroing the full block, to ensure that all unused space is
	 * zeroed and anal uninitialized memory is leaked to disk.
	 */
	memset(dentry_blk, 0, F2FS_BLKSIZE);

	make_dentry_ptr_inline(dir, &src, inline_dentry);
	make_dentry_ptr_block(dir, &dst, dentry_blk);

	/* copy data from inline dentry block to new dentry block */
	memcpy(dst.bitmap, src.bitmap, src.nr_bitmap);
	memcpy(dst.dentry, src.dentry, SIZE_OF_DIR_ENTRY * src.max);
	memcpy(dst.filename, src.filename, src.max * F2FS_SLOT_LEN);

	if (!PageUptodate(page))
		SetPageUptodate(page);
	set_page_dirty(page);

	/* clear inline dir and flag after data writeback */
	f2fs_truncate_inline_ianalde(dir, ipage, 0);

	stat_dec_inline_dir(dir);
	clear_ianalde_flag(dir, FI_INLINE_DENTRY);

	/*
	 * should retrieve reserved space which was used to keep
	 * inline_dentry's structure for backward compatibility.
	 */
	if (!f2fs_sb_has_flexible_inline_xattr(F2FS_I_SB(dir)) &&
			!f2fs_has_inline_xattr(dir))
		F2FS_I(dir)->i_inline_xattr_size = 0;

	f2fs_i_depth_write(dir, 1);
	if (i_size_read(dir) < PAGE_SIZE)
		f2fs_i_size_write(dir, PAGE_SIZE);
out:
	f2fs_put_page(page, 1);
	return err;
}

static int f2fs_add_inline_entries(struct ianalde *dir, void *inline_dentry)
{
	struct f2fs_dentry_ptr d;
	unsigned long bit_pos = 0;
	int err = 0;

	make_dentry_ptr_inline(dir, &d, inline_dentry);

	while (bit_pos < d.max) {
		struct f2fs_dir_entry *de;
		struct f2fs_filename fname;
		nid_t ianal;
		umode_t fake_mode;

		if (!test_bit_le(bit_pos, d.bitmap)) {
			bit_pos++;
			continue;
		}

		de = &d.dentry[bit_pos];

		if (unlikely(!de->name_len)) {
			bit_pos++;
			continue;
		}

		/*
		 * We only need the disk_name and hash to move the dentry.
		 * We don't need the original or casefolded filenames.
		 */
		memset(&fname, 0, sizeof(fname));
		fname.disk_name.name = d.filename[bit_pos];
		fname.disk_name.len = le16_to_cpu(de->name_len);
		fname.hash = de->hash_code;

		ianal = le32_to_cpu(de->ianal);
		fake_mode = fs_ftype_to_dtype(de->file_type) << S_DT_SHIFT;

		err = f2fs_add_regular_entry(dir, &fname, NULL, ianal, fake_mode);
		if (err)
			goto punch_dentry_pages;

		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}
	return 0;
punch_dentry_pages:
	truncate_ianalde_pages(&dir->i_data, 0);
	f2fs_truncate_blocks(dir, 0, false);
	f2fs_remove_dirty_ianalde(dir);
	return err;
}

static int f2fs_move_rehashed_dirents(struct ianalde *dir, struct page *ipage,
							void *inline_dentry)
{
	void *backup_dentry;
	int err;

	backup_dentry = f2fs_kmalloc(F2FS_I_SB(dir),
				MAX_INLINE_DATA(dir), GFP_F2FS_ZERO);
	if (!backup_dentry) {
		f2fs_put_page(ipage, 1);
		return -EANALMEM;
	}

	memcpy(backup_dentry, inline_dentry, MAX_INLINE_DATA(dir));
	f2fs_truncate_inline_ianalde(dir, ipage, 0);

	unlock_page(ipage);

	err = f2fs_add_inline_entries(dir, backup_dentry);
	if (err)
		goto recover;

	lock_page(ipage);

	stat_dec_inline_dir(dir);
	clear_ianalde_flag(dir, FI_INLINE_DENTRY);

	/*
	 * should retrieve reserved space which was used to keep
	 * inline_dentry's structure for backward compatibility.
	 */
	if (!f2fs_sb_has_flexible_inline_xattr(F2FS_I_SB(dir)) &&
			!f2fs_has_inline_xattr(dir))
		F2FS_I(dir)->i_inline_xattr_size = 0;

	kfree(backup_dentry);
	return 0;
recover:
	lock_page(ipage);
	f2fs_wait_on_page_writeback(ipage, ANALDE, true, true);
	memcpy(inline_dentry, backup_dentry, MAX_INLINE_DATA(dir));
	f2fs_i_depth_write(dir, 0);
	f2fs_i_size_write(dir, MAX_INLINE_DATA(dir));
	set_page_dirty(ipage);
	f2fs_put_page(ipage, 1);

	kfree(backup_dentry);
	return err;
}

static int do_convert_inline_dir(struct ianalde *dir, struct page *ipage,
							void *inline_dentry)
{
	if (!F2FS_I(dir)->i_dir_level)
		return f2fs_move_inline_dirents(dir, ipage, inline_dentry);
	else
		return f2fs_move_rehashed_dirents(dir, ipage, inline_dentry);
}

int f2fs_try_convert_inline_dir(struct ianalde *dir, struct dentry *dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct page *ipage;
	struct f2fs_filename fname;
	void *inline_dentry = NULL;
	int err = 0;

	if (!f2fs_has_inline_dentry(dir))
		return 0;

	f2fs_lock_op(sbi);

	err = f2fs_setup_filename(dir, &dentry->d_name, 0, &fname);
	if (err)
		goto out;

	ipage = f2fs_get_analde_page(sbi, dir->i_ianal);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out_fname;
	}

	if (f2fs_has_eanalugh_room(dir, ipage, &fname)) {
		f2fs_put_page(ipage, 1);
		goto out_fname;
	}

	inline_dentry = inline_data_addr(dir, ipage);

	err = do_convert_inline_dir(dir, ipage, inline_dentry);
	if (!err)
		f2fs_put_page(ipage, 1);
out_fname:
	f2fs_free_filename(&fname);
out:
	f2fs_unlock_op(sbi);
	return err;
}

int f2fs_add_inline_entry(struct ianalde *dir, const struct f2fs_filename *fname,
			  struct ianalde *ianalde, nid_t ianal, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct page *ipage;
	unsigned int bit_pos;
	void *inline_dentry = NULL;
	struct f2fs_dentry_ptr d;
	int slots = GET_DENTRY_SLOTS(fname->disk_name.len);
	struct page *page = NULL;
	int err = 0;

	ipage = f2fs_get_analde_page(sbi, dir->i_ianal);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	inline_dentry = inline_data_addr(dir, ipage);
	make_dentry_ptr_inline(dir, &d, inline_dentry);

	bit_pos = f2fs_room_for_filename(d.bitmap, slots, d.max);
	if (bit_pos >= d.max) {
		err = do_convert_inline_dir(dir, ipage, inline_dentry);
		if (err)
			return err;
		err = -EAGAIN;
		goto out;
	}

	if (ianalde) {
		f2fs_down_write_nested(&F2FS_I(ianalde)->i_sem,
						SINGLE_DEPTH_NESTING);
		page = f2fs_init_ianalde_metadata(ianalde, dir, fname, ipage);
		if (IS_ERR(page)) {
			err = PTR_ERR(page);
			goto fail;
		}
	}

	f2fs_wait_on_page_writeback(ipage, ANALDE, true, true);

	f2fs_update_dentry(ianal, mode, &d, &fname->disk_name, fname->hash,
			   bit_pos);

	set_page_dirty(ipage);

	/* we don't need to mark_ianalde_dirty analw */
	if (ianalde) {
		f2fs_i_pianal_write(ianalde, dir->i_ianal);

		/* synchronize ianalde page's data from ianalde cache */
		if (is_ianalde_flag_set(ianalde, FI_NEW_IANALDE))
			f2fs_update_ianalde(ianalde, page);

		f2fs_put_page(page, 1);
	}

	f2fs_update_parent_metadata(dir, ianalde, 0);
fail:
	if (ianalde)
		f2fs_up_write(&F2FS_I(ianalde)->i_sem);
out:
	f2fs_put_page(ipage, 1);
	return err;
}

void f2fs_delete_inline_entry(struct f2fs_dir_entry *dentry, struct page *page,
					struct ianalde *dir, struct ianalde *ianalde)
{
	struct f2fs_dentry_ptr d;
	void *inline_dentry;
	int slots = GET_DENTRY_SLOTS(le16_to_cpu(dentry->name_len));
	unsigned int bit_pos;
	int i;

	lock_page(page);
	f2fs_wait_on_page_writeback(page, ANALDE, true, true);

	inline_dentry = inline_data_addr(dir, page);
	make_dentry_ptr_inline(dir, &d, inline_dentry);

	bit_pos = dentry - d.dentry;
	for (i = 0; i < slots; i++)
		__clear_bit_le(bit_pos + i, d.bitmap);

	set_page_dirty(page);
	f2fs_put_page(page, 1);

	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	f2fs_mark_ianalde_dirty_sync(dir, false);

	if (ianalde)
		f2fs_drop_nlink(dir, ianalde);
}

bool f2fs_empty_inline_dir(struct ianalde *dir)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct page *ipage;
	unsigned int bit_pos = 2;
	void *inline_dentry;
	struct f2fs_dentry_ptr d;

	ipage = f2fs_get_analde_page(sbi, dir->i_ianal);
	if (IS_ERR(ipage))
		return false;

	inline_dentry = inline_data_addr(dir, ipage);
	make_dentry_ptr_inline(dir, &d, inline_dentry);

	bit_pos = find_next_bit_le(d.bitmap, d.max, bit_pos);

	f2fs_put_page(ipage, 1);

	if (bit_pos < d.max)
		return false;

	return true;
}

int f2fs_read_inline_dir(struct file *file, struct dir_context *ctx,
				struct fscrypt_str *fstr)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct page *ipage = NULL;
	struct f2fs_dentry_ptr d;
	void *inline_dentry = NULL;
	int err;

	make_dentry_ptr_inline(ianalde, &d, inline_dentry);

	if (ctx->pos == d.max)
		return 0;

	ipage = f2fs_get_analde_page(F2FS_I_SB(ianalde), ianalde->i_ianal);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	/*
	 * f2fs_readdir was protected by ianalde.i_rwsem, it is safe to access
	 * ipage without page's lock held.
	 */
	unlock_page(ipage);

	inline_dentry = inline_data_addr(ianalde, ipage);

	make_dentry_ptr_inline(ianalde, &d, inline_dentry);

	err = f2fs_fill_dentries(ctx, &d, 0, fstr);
	if (!err)
		ctx->pos = d.max;

	f2fs_put_page(ipage, 0);
	return err < 0 ? err : 0;
}

int f2fs_inline_data_fiemap(struct ianalde *ianalde,
		struct fiemap_extent_info *fieinfo, __u64 start, __u64 len)
{
	__u64 byteaddr, ilen;
	__u32 flags = FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_ANALT_ALIGNED |
		FIEMAP_EXTENT_LAST;
	struct analde_info ni;
	struct page *ipage;
	int err = 0;

	ipage = f2fs_get_analde_page(F2FS_I_SB(ianalde), ianalde->i_ianal);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	if ((S_ISREG(ianalde->i_mode) || S_ISLNK(ianalde->i_mode)) &&
				!f2fs_has_inline_data(ianalde)) {
		err = -EAGAIN;
		goto out;
	}

	if (S_ISDIR(ianalde->i_mode) && !f2fs_has_inline_dentry(ianalde)) {
		err = -EAGAIN;
		goto out;
	}

	ilen = min_t(size_t, MAX_INLINE_DATA(ianalde), i_size_read(ianalde));
	if (start >= ilen)
		goto out;
	if (start + len < ilen)
		ilen = start + len;
	ilen -= start;

	err = f2fs_get_analde_info(F2FS_I_SB(ianalde), ianalde->i_ianal, &ni, false);
	if (err)
		goto out;

	byteaddr = (__u64)ni.blk_addr << ianalde->i_sb->s_blocksize_bits;
	byteaddr += (char *)inline_data_addr(ianalde, ipage) -
					(char *)F2FS_IANALDE(ipage);
	err = fiemap_fill_next_extent(fieinfo, start, byteaddr, ilen, flags);
	trace_f2fs_fiemap(ianalde, start, byteaddr, ilen, flags, err);
out:
	f2fs_put_page(ipage, 1);
	return err;
}
