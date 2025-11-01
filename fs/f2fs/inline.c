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
#include "node.h"
#include <trace/events/f2fs.h>

static bool support_inline_data(struct inode *inode)
{
	if (f2fs_used_in_atomic_write(inode))
		return false;
	if (!S_ISREG(inode->i_mode) && !S_ISLNK(inode->i_mode))
		return false;
	if (i_size_read(inode) > MAX_INLINE_DATA(inode))
		return false;
	return true;
}

bool f2fs_may_inline_data(struct inode *inode)
{
	if (!support_inline_data(inode))
		return false;

	return !f2fs_post_read_required(inode);
}

static bool inode_has_blocks(struct inode *inode, struct folio *ifolio)
{
	struct f2fs_inode *ri = F2FS_INODE(ifolio);
	int i;

	if (F2FS_HAS_BLOCKS(inode))
		return true;

	for (i = 0; i < DEF_NIDS_PER_INODE; i++) {
		if (ri->i_nid[i])
			return true;
	}
	return false;
}

bool f2fs_sanity_check_inline_data(struct inode *inode, struct folio *ifolio)
{
	if (!f2fs_has_inline_data(inode))
		return false;

	if (inode_has_blocks(inode, ifolio))
		return false;

	if (!support_inline_data(inode))
		return true;

	/*
	 * used by sanity_check_inode(), when disk layout fields has not
	 * been synchronized to inmem fields.
	 */
	return (S_ISREG(inode->i_mode) &&
		(file_is_encrypt(inode) || file_is_verity(inode) ||
		(F2FS_I(inode)->i_flags & F2FS_COMPR_FL)));
}

bool f2fs_may_inline_dentry(struct inode *inode)
{
	if (!test_opt(F2FS_I_SB(inode), INLINE_DENTRY))
		return false;

	if (!S_ISDIR(inode->i_mode))
		return false;

	return true;
}

void f2fs_do_read_inline_data(struct folio *folio, struct folio *ifolio)
{
	struct inode *inode = folio->mapping->host;

	if (folio_test_uptodate(folio))
		return;

	f2fs_bug_on(F2FS_I_SB(inode), folio->index);

	folio_zero_segment(folio, MAX_INLINE_DATA(inode), folio_size(folio));

	/* Copy the whole inline data block */
	memcpy_to_folio(folio, 0, inline_data_addr(inode, ifolio),
		       MAX_INLINE_DATA(inode));
	if (!folio_test_uptodate(folio))
		folio_mark_uptodate(folio);
}

void f2fs_truncate_inline_inode(struct inode *inode, struct folio *ifolio,
		u64 from)
{
	void *addr;

	if (from >= MAX_INLINE_DATA(inode))
		return;

	addr = inline_data_addr(inode, ifolio);

	f2fs_folio_wait_writeback(ifolio, NODE, true, true);
	memset(addr + from, 0, MAX_INLINE_DATA(inode) - from);
	folio_mark_dirty(ifolio);

	if (from == 0)
		clear_inode_flag(inode, FI_DATA_EXIST);
}

int f2fs_read_inline_data(struct inode *inode, struct folio *folio)
{
	struct folio *ifolio;

	ifolio = f2fs_get_inode_folio(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ifolio)) {
		folio_unlock(folio);
		return PTR_ERR(ifolio);
	}

	if (!f2fs_has_inline_data(inode)) {
		f2fs_folio_put(ifolio, true);
		return -EAGAIN;
	}

	if (folio->index)
		folio_zero_segment(folio, 0, folio_size(folio));
	else
		f2fs_do_read_inline_data(folio, ifolio);

	if (!folio_test_uptodate(folio))
		folio_mark_uptodate(folio);
	f2fs_folio_put(ifolio, true);
	folio_unlock(folio);
	return 0;
}

int f2fs_convert_inline_folio(struct dnode_of_data *dn, struct folio *folio)
{
	struct f2fs_io_info fio = {
		.sbi = F2FS_I_SB(dn->inode),
		.ino = dn->inode->i_ino,
		.type = DATA,
		.op = REQ_OP_WRITE,
		.op_flags = REQ_SYNC | REQ_PRIO,
		.folio = folio,
		.encrypted_page = NULL,
		.io_type = FS_DATA_IO,
	};
	struct node_info ni;
	int dirty, err;

	if (!f2fs_exist_data(dn->inode))
		goto clear_out;

	err = f2fs_reserve_block(dn, 0);
	if (err)
		return err;

	err = f2fs_get_node_info(fio.sbi, dn->nid, &ni, false);
	if (err) {
		f2fs_truncate_data_blocks_range(dn, 1);
		f2fs_put_dnode(dn);
		return err;
	}

	fio.version = ni.version;

	if (unlikely(dn->data_blkaddr != NEW_ADDR)) {
		f2fs_put_dnode(dn);
		set_sbi_flag(fio.sbi, SBI_NEED_FSCK);
		f2fs_warn(fio.sbi, "%s: corrupted inline inode ino=%lx, i_addr[0]:0x%x, run fsck to fix.",
			  __func__, dn->inode->i_ino, dn->data_blkaddr);
		f2fs_handle_error(fio.sbi, ERROR_INVALID_BLKADDR);
		return -EFSCORRUPTED;
	}

	f2fs_bug_on(F2FS_F_SB(folio), folio_test_writeback(folio));

	f2fs_do_read_inline_data(folio, dn->inode_folio);
	folio_mark_dirty(folio);

	/* clear dirty state */
	dirty = folio_clear_dirty_for_io(folio);

	/* write data page to try to make data consistent */
	folio_start_writeback(folio);
	fio.old_blkaddr = dn->data_blkaddr;
	set_inode_flag(dn->inode, FI_HOT_DATA);
	f2fs_outplace_write_data(dn, &fio);
	f2fs_folio_wait_writeback(folio, DATA, true, true);
	if (dirty) {
		inode_dec_dirty_pages(dn->inode);
		f2fs_remove_dirty_inode(dn->inode);
	}

	/* this converted inline_data should be recovered. */
	set_inode_flag(dn->inode, FI_APPEND_WRITE);

	/* clear inline data and flag after data writeback */
	f2fs_truncate_inline_inode(dn->inode, dn->inode_folio, 0);
	folio_clear_f2fs_inline(dn->inode_folio);
clear_out:
	stat_dec_inline_inode(dn->inode);
	clear_inode_flag(dn->inode, FI_INLINE_DATA);
	f2fs_put_dnode(dn);
	return 0;
}

int f2fs_convert_inline_inode(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct dnode_of_data dn;
	struct folio *ifolio, *folio;
	int err = 0;

	if (f2fs_hw_is_readonly(sbi) || f2fs_readonly(sbi->sb))
		return -EROFS;

	if (!f2fs_has_inline_data(inode))
		return 0;

	err = f2fs_dquot_initialize(inode);
	if (err)
		return err;

	folio = f2fs_grab_cache_folio(inode->i_mapping, 0, false);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	f2fs_lock_op(sbi);

	ifolio = f2fs_get_inode_folio(sbi, inode->i_ino);
	if (IS_ERR(ifolio)) {
		err = PTR_ERR(ifolio);
		goto out;
	}

	set_new_dnode(&dn, inode, ifolio, ifolio, 0);

	if (f2fs_has_inline_data(inode))
		err = f2fs_convert_inline_folio(&dn, folio);

	f2fs_put_dnode(&dn);
out:
	f2fs_unlock_op(sbi);

	f2fs_folio_put(folio, true);

	if (!err)
		f2fs_balance_fs(sbi, dn.node_changed);

	return err;
}

int f2fs_write_inline_data(struct inode *inode, struct folio *folio)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct folio *ifolio;

	ifolio = f2fs_get_inode_folio(sbi, inode->i_ino);
	if (IS_ERR(ifolio))
		return PTR_ERR(ifolio);

	if (!f2fs_has_inline_data(inode)) {
		f2fs_folio_put(ifolio, true);
		return -EAGAIN;
	}

	f2fs_bug_on(F2FS_I_SB(inode), folio->index);

	f2fs_folio_wait_writeback(ifolio, NODE, true, true);
	memcpy_from_folio(inline_data_addr(inode, ifolio),
			 folio, 0, MAX_INLINE_DATA(inode));
	folio_mark_dirty(ifolio);

	f2fs_clear_page_cache_dirty_tag(folio);

	set_inode_flag(inode, FI_APPEND_WRITE);
	set_inode_flag(inode, FI_DATA_EXIST);

	folio_clear_f2fs_inline(ifolio);
	f2fs_folio_put(ifolio, 1);
	return 0;
}

int f2fs_recover_inline_data(struct inode *inode, struct folio *nfolio)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_inode *ri = NULL;
	void *src_addr, *dst_addr;

	/*
	 * The inline_data recovery policy is as follows.
	 * [prev.] [next] of inline_data flag
	 *    o       o  -> recover inline_data
	 *    o       x  -> remove inline_data, and then recover data blocks
	 *    x       o  -> remove data blocks, and then recover inline_data
	 *    x       x  -> recover data blocks
	 */
	if (IS_INODE(nfolio))
		ri = F2FS_INODE(nfolio);

	if (f2fs_has_inline_data(inode) &&
			ri && (ri->i_inline & F2FS_INLINE_DATA)) {
		struct folio *ifolio;
process_inline:
		ifolio = f2fs_get_inode_folio(sbi, inode->i_ino);
		if (IS_ERR(ifolio))
			return PTR_ERR(ifolio);

		f2fs_folio_wait_writeback(ifolio, NODE, true, true);

		src_addr = inline_data_addr(inode, nfolio);
		dst_addr = inline_data_addr(inode, ifolio);
		memcpy(dst_addr, src_addr, MAX_INLINE_DATA(inode));

		set_inode_flag(inode, FI_INLINE_DATA);
		set_inode_flag(inode, FI_DATA_EXIST);

		folio_mark_dirty(ifolio);
		f2fs_folio_put(ifolio, true);
		return 1;
	}

	if (f2fs_has_inline_data(inode)) {
		struct folio *ifolio = f2fs_get_inode_folio(sbi, inode->i_ino);
		if (IS_ERR(ifolio))
			return PTR_ERR(ifolio);
		f2fs_truncate_inline_inode(inode, ifolio, 0);
		stat_dec_inline_inode(inode);
		clear_inode_flag(inode, FI_INLINE_DATA);
		f2fs_folio_put(ifolio, true);
	} else if (ri && (ri->i_inline & F2FS_INLINE_DATA)) {
		int ret;

		ret = f2fs_truncate_blocks(inode, 0, false);
		if (ret)
			return ret;
		stat_inc_inline_inode(inode);
		goto process_inline;
	}
	return 0;
}

struct f2fs_dir_entry *f2fs_find_in_inline_dir(struct inode *dir,
					const struct f2fs_filename *fname,
					struct folio **res_folio,
					bool use_hash)
{
	struct f2fs_sb_info *sbi = F2FS_SB(dir->i_sb);
	struct f2fs_dir_entry *de;
	struct f2fs_dentry_ptr d;
	struct folio *ifolio;
	void *inline_dentry;

	ifolio = f2fs_get_inode_folio(sbi, dir->i_ino);
	if (IS_ERR(ifolio)) {
		*res_folio = ifolio;
		return NULL;
	}

	inline_dentry = inline_data_addr(dir, ifolio);

	make_dentry_ptr_inline(dir, &d, inline_dentry);
	de = f2fs_find_target_dentry(&d, fname, NULL, use_hash);
	folio_unlock(ifolio);
	if (IS_ERR(de)) {
		*res_folio = ERR_CAST(de);
		de = NULL;
	}
	if (de)
		*res_folio = ifolio;
	else
		f2fs_folio_put(ifolio, false);

	return de;
}

int f2fs_make_empty_inline_dir(struct inode *inode, struct inode *parent,
							struct folio *ifolio)
{
	struct f2fs_dentry_ptr d;
	void *inline_dentry;

	inline_dentry = inline_data_addr(inode, ifolio);

	make_dentry_ptr_inline(inode, &d, inline_dentry);
	f2fs_do_make_empty_dir(inode, parent, &d);

	folio_mark_dirty(ifolio);

	/* update i_size to MAX_INLINE_DATA */
	if (i_size_read(inode) < MAX_INLINE_DATA(inode))
		f2fs_i_size_write(inode, MAX_INLINE_DATA(inode));
	return 0;
}

/*
 * NOTE: ipage is grabbed by caller, but if any error occurs, we should
 * release ipage in this function.
 */
static int f2fs_move_inline_dirents(struct inode *dir, struct folio *ifolio,
							void *inline_dentry)
{
	struct folio *folio;
	struct dnode_of_data dn;
	struct f2fs_dentry_block *dentry_blk;
	struct f2fs_dentry_ptr src, dst;
	int err;

	folio = f2fs_grab_cache_folio(dir->i_mapping, 0, true);
	if (IS_ERR(folio)) {
		f2fs_folio_put(ifolio, true);
		return PTR_ERR(folio);
	}

	set_new_dnode(&dn, dir, ifolio, NULL, 0);
	err = f2fs_reserve_block(&dn, 0);
	if (err)
		goto out;

	if (unlikely(dn.data_blkaddr != NEW_ADDR)) {
		f2fs_put_dnode(&dn);
		set_sbi_flag(F2FS_F_SB(folio), SBI_NEED_FSCK);
		f2fs_warn(F2FS_F_SB(folio), "%s: corrupted inline inode ino=%lx, i_addr[0]:0x%x, run fsck to fix.",
			  __func__, dir->i_ino, dn.data_blkaddr);
		f2fs_handle_error(F2FS_F_SB(folio), ERROR_INVALID_BLKADDR);
		err = -EFSCORRUPTED;
		goto out;
	}

	f2fs_folio_wait_writeback(folio, DATA, true, true);

	dentry_blk = folio_address(folio);

	/*
	 * Start by zeroing the full block, to ensure that all unused space is
	 * zeroed and no uninitialized memory is leaked to disk.
	 */
	memset(dentry_blk, 0, F2FS_BLKSIZE);

	make_dentry_ptr_inline(dir, &src, inline_dentry);
	make_dentry_ptr_block(dir, &dst, dentry_blk);

	/* copy data from inline dentry block to new dentry block */
	memcpy(dst.bitmap, src.bitmap, src.nr_bitmap);
	memcpy(dst.dentry, src.dentry, SIZE_OF_DIR_ENTRY * src.max);
	memcpy(dst.filename, src.filename, src.max * F2FS_SLOT_LEN);

	if (!folio_test_uptodate(folio))
		folio_mark_uptodate(folio);
	folio_mark_dirty(folio);

	/* clear inline dir and flag after data writeback */
	f2fs_truncate_inline_inode(dir, ifolio, 0);

	stat_dec_inline_dir(dir);
	clear_inode_flag(dir, FI_INLINE_DENTRY);

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
	f2fs_folio_put(folio, true);
	return err;
}

static int f2fs_add_inline_entries(struct inode *dir, void *inline_dentry)
{
	struct f2fs_dentry_ptr d;
	unsigned long bit_pos = 0;
	int err = 0;

	make_dentry_ptr_inline(dir, &d, inline_dentry);

	while (bit_pos < d.max) {
		struct f2fs_dir_entry *de;
		struct f2fs_filename fname;
		nid_t ino;
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

		ino = le32_to_cpu(de->ino);
		fake_mode = fs_ftype_to_dtype(de->file_type) << S_DT_SHIFT;

		err = f2fs_add_regular_entry(dir, &fname, NULL, ino, fake_mode);
		if (err)
			goto punch_dentry_pages;

		bit_pos += GET_DENTRY_SLOTS(le16_to_cpu(de->name_len));
	}
	return 0;
punch_dentry_pages:
	truncate_inode_pages(&dir->i_data, 0);
	f2fs_truncate_blocks(dir, 0, false);
	f2fs_remove_dirty_inode(dir);
	return err;
}

static int f2fs_move_rehashed_dirents(struct inode *dir, struct folio *ifolio,
							void *inline_dentry)
{
	void *backup_dentry;
	int err;

	backup_dentry = f2fs_kmalloc(F2FS_I_SB(dir),
				MAX_INLINE_DATA(dir), GFP_F2FS_ZERO);
	if (!backup_dentry) {
		f2fs_folio_put(ifolio, true);
		return -ENOMEM;
	}

	memcpy(backup_dentry, inline_dentry, MAX_INLINE_DATA(dir));
	f2fs_truncate_inline_inode(dir, ifolio, 0);

	folio_unlock(ifolio);

	err = f2fs_add_inline_entries(dir, backup_dentry);
	if (err)
		goto recover;

	folio_lock(ifolio);

	stat_dec_inline_dir(dir);
	clear_inode_flag(dir, FI_INLINE_DENTRY);

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
	folio_lock(ifolio);
	f2fs_folio_wait_writeback(ifolio, NODE, true, true);
	memcpy(inline_dentry, backup_dentry, MAX_INLINE_DATA(dir));
	f2fs_i_depth_write(dir, 0);
	f2fs_i_size_write(dir, MAX_INLINE_DATA(dir));
	folio_mark_dirty(ifolio);
	f2fs_folio_put(ifolio, 1);

	kfree(backup_dentry);
	return err;
}

static int do_convert_inline_dir(struct inode *dir, struct folio *ifolio,
							void *inline_dentry)
{
	if (!F2FS_I(dir)->i_dir_level)
		return f2fs_move_inline_dirents(dir, ifolio, inline_dentry);
	else
		return f2fs_move_rehashed_dirents(dir, ifolio, inline_dentry);
}

int f2fs_try_convert_inline_dir(struct inode *dir, struct dentry *dentry)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct folio *ifolio;
	struct f2fs_filename fname;
	void *inline_dentry = NULL;
	int err = 0;

	if (!f2fs_has_inline_dentry(dir))
		return 0;

	f2fs_lock_op(sbi);

	err = f2fs_setup_filename(dir, &dentry->d_name, 0, &fname);
	if (err)
		goto out;

	ifolio = f2fs_get_inode_folio(sbi, dir->i_ino);
	if (IS_ERR(ifolio)) {
		err = PTR_ERR(ifolio);
		goto out_fname;
	}

	if (f2fs_has_enough_room(dir, ifolio, &fname)) {
		f2fs_folio_put(ifolio, true);
		goto out_fname;
	}

	inline_dentry = inline_data_addr(dir, ifolio);

	err = do_convert_inline_dir(dir, ifolio, inline_dentry);
	if (!err)
		f2fs_folio_put(ifolio, true);
out_fname:
	f2fs_free_filename(&fname);
out:
	f2fs_unlock_op(sbi);
	return err;
}

int f2fs_add_inline_entry(struct inode *dir, const struct f2fs_filename *fname,
			  struct inode *inode, nid_t ino, umode_t mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct folio *ifolio;
	unsigned int bit_pos;
	void *inline_dentry = NULL;
	struct f2fs_dentry_ptr d;
	int slots = GET_DENTRY_SLOTS(fname->disk_name.len);
	struct folio *folio = NULL;
	int err = 0;

	ifolio = f2fs_get_inode_folio(sbi, dir->i_ino);
	if (IS_ERR(ifolio))
		return PTR_ERR(ifolio);

	inline_dentry = inline_data_addr(dir, ifolio);
	make_dentry_ptr_inline(dir, &d, inline_dentry);

	bit_pos = f2fs_room_for_filename(d.bitmap, slots, d.max);
	if (bit_pos >= d.max) {
		err = do_convert_inline_dir(dir, ifolio, inline_dentry);
		if (err)
			return err;
		err = -EAGAIN;
		goto out;
	}

	if (inode) {
		f2fs_down_write_nested(&F2FS_I(inode)->i_sem,
						SINGLE_DEPTH_NESTING);
		folio = f2fs_init_inode_metadata(inode, dir, fname, ifolio);
		if (IS_ERR(folio)) {
			err = PTR_ERR(folio);
			goto fail;
		}
	}

	f2fs_folio_wait_writeback(ifolio, NODE, true, true);

	f2fs_update_dentry(ino, mode, &d, &fname->disk_name, fname->hash,
			   bit_pos);

	folio_mark_dirty(ifolio);

	/* we don't need to mark_inode_dirty now */
	if (inode) {
		f2fs_i_pino_write(inode, dir->i_ino);

		/* synchronize inode page's data from inode cache */
		if (is_inode_flag_set(inode, FI_NEW_INODE))
			f2fs_update_inode(inode, folio);

		f2fs_folio_put(folio, true);
	}

	f2fs_update_parent_metadata(dir, inode, 0);
fail:
	if (inode)
		f2fs_up_write(&F2FS_I(inode)->i_sem);
out:
	f2fs_folio_put(ifolio, true);
	return err;
}

void f2fs_delete_inline_entry(struct f2fs_dir_entry *dentry,
		struct folio *folio, struct inode *dir, struct inode *inode)
{
	struct f2fs_dentry_ptr d;
	void *inline_dentry;
	int slots = GET_DENTRY_SLOTS(le16_to_cpu(dentry->name_len));
	unsigned int bit_pos;
	int i;

	folio_lock(folio);
	f2fs_folio_wait_writeback(folio, NODE, true, true);

	inline_dentry = inline_data_addr(dir, folio);
	make_dentry_ptr_inline(dir, &d, inline_dentry);

	bit_pos = dentry - d.dentry;
	for (i = 0; i < slots; i++)
		__clear_bit_le(bit_pos + i, d.bitmap);

	folio_mark_dirty(folio);
	f2fs_folio_put(folio, true);

	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
	f2fs_mark_inode_dirty_sync(dir, false);

	if (inode)
		f2fs_drop_nlink(dir, inode);
}

bool f2fs_empty_inline_dir(struct inode *dir)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dir);
	struct folio *ifolio;
	unsigned int bit_pos = 2;
	void *inline_dentry;
	struct f2fs_dentry_ptr d;

	ifolio = f2fs_get_inode_folio(sbi, dir->i_ino);
	if (IS_ERR(ifolio))
		return false;

	inline_dentry = inline_data_addr(dir, ifolio);
	make_dentry_ptr_inline(dir, &d, inline_dentry);

	bit_pos = find_next_bit_le(d.bitmap, d.max, bit_pos);

	f2fs_folio_put(ifolio, true);

	if (bit_pos < d.max)
		return false;

	return true;
}

int f2fs_read_inline_dir(struct file *file, struct dir_context *ctx,
				struct fscrypt_str *fstr)
{
	struct inode *inode = file_inode(file);
	struct folio *ifolio = NULL;
	struct f2fs_dentry_ptr d;
	void *inline_dentry = NULL;
	int err;

	make_dentry_ptr_inline(inode, &d, inline_dentry);

	if (ctx->pos == d.max)
		return 0;

	ifolio = f2fs_get_inode_folio(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ifolio))
		return PTR_ERR(ifolio);

	/*
	 * f2fs_readdir was protected by inode.i_rwsem, it is safe to access
	 * ipage without page's lock held.
	 */
	folio_unlock(ifolio);

	inline_dentry = inline_data_addr(inode, ifolio);

	make_dentry_ptr_inline(inode, &d, inline_dentry);

	err = f2fs_fill_dentries(ctx, &d, 0, fstr);
	if (!err)
		ctx->pos = d.max;

	f2fs_folio_put(ifolio, false);
	return err < 0 ? err : 0;
}

int f2fs_inline_data_fiemap(struct inode *inode,
		struct fiemap_extent_info *fieinfo, __u64 start, __u64 len)
{
	__u64 byteaddr, ilen;
	__u32 flags = FIEMAP_EXTENT_DATA_INLINE | FIEMAP_EXTENT_NOT_ALIGNED |
		FIEMAP_EXTENT_LAST;
	struct node_info ni;
	struct folio *ifolio;
	int err = 0;

	ifolio = f2fs_get_inode_folio(F2FS_I_SB(inode), inode->i_ino);
	if (IS_ERR(ifolio))
		return PTR_ERR(ifolio);

	if ((S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)) &&
				!f2fs_has_inline_data(inode)) {
		err = -EAGAIN;
		goto out;
	}

	if (S_ISDIR(inode->i_mode) && !f2fs_has_inline_dentry(inode)) {
		err = -EAGAIN;
		goto out;
	}

	ilen = min_t(size_t, MAX_INLINE_DATA(inode), i_size_read(inode));
	if (start >= ilen)
		goto out;
	if (start + len < ilen)
		ilen = start + len;
	ilen -= start;

	err = f2fs_get_node_info(F2FS_I_SB(inode), inode->i_ino, &ni, false);
	if (err)
		goto out;

	byteaddr = (__u64)ni.blk_addr << inode->i_sb->s_blocksize_bits;
	byteaddr += (char *)inline_data_addr(inode, ifolio) -
					(char *)F2FS_INODE(ifolio);
	err = fiemap_fill_next_extent(fieinfo, start, byteaddr, ilen, flags);
	trace_f2fs_fiemap(inode, start, byteaddr, ilen, flags, err);
out:
	f2fs_folio_put(ifolio, true);
	return err;
}
