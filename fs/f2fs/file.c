// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/file.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/stat.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/falloc.h>
#include <linux/types.h>
#include <linux/compat.h>
#include <linux/uaccess.h>
#include <linux/mount.h>
#include <linux/pagevec.h>
#include <linux/uio.h>
#include <linux/uuid.h>
#include <linux/file.h>
#include <linux/nls.h>

#include "f2fs.h"
#include "yesde.h"
#include "segment.h"
#include "xattr.h"
#include "acl.h"
#include "gc.h"
#include "trace.h"
#include <trace/events/f2fs.h>

static vm_fault_t f2fs_filemap_fault(struct vm_fault *vmf)
{
	struct iyesde *iyesde = file_iyesde(vmf->vma->vm_file);
	vm_fault_t ret;

	down_read(&F2FS_I(iyesde)->i_mmap_sem);
	ret = filemap_fault(vmf);
	up_read(&F2FS_I(iyesde)->i_mmap_sem);

	trace_f2fs_filemap_fault(iyesde, vmf->pgoff, (unsigned long)ret);

	return ret;
}

static vm_fault_t f2fs_vm_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct iyesde *iyesde = file_iyesde(vmf->vma->vm_file);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct dyesde_of_data dn = { .yesde_changed = false };
	int err;

	if (unlikely(f2fs_cp_error(sbi))) {
		err = -EIO;
		goto err;
	}

	if (!f2fs_is_checkpoint_ready(sbi)) {
		err = -ENOSPC;
		goto err;
	}

	sb_start_pagefault(iyesde->i_sb);

	f2fs_bug_on(sbi, f2fs_has_inline_data(iyesde));

	file_update_time(vmf->vma->vm_file);
	down_read(&F2FS_I(iyesde)->i_mmap_sem);
	lock_page(page);
	if (unlikely(page->mapping != iyesde->i_mapping ||
			page_offset(page) > i_size_read(iyesde) ||
			!PageUptodate(page))) {
		unlock_page(page);
		err = -EFAULT;
		goto out_sem;
	}

	/* block allocation */
	__do_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO, true);
	set_new_dyesde(&dn, iyesde, NULL, NULL, 0);
	err = f2fs_get_block(&dn, page->index);
	f2fs_put_dyesde(&dn);
	__do_map_lock(sbi, F2FS_GET_BLOCK_PRE_AIO, false);
	if (err) {
		unlock_page(page);
		goto out_sem;
	}

	/* fill the page */
	f2fs_wait_on_page_writeback(page, DATA, false, true);

	/* wait for GCed page writeback via META_MAPPING */
	f2fs_wait_on_block_writeback(iyesde, dn.data_blkaddr);

	/*
	 * check to see if the page is mapped already (yes holes)
	 */
	if (PageMappedToDisk(page))
		goto out_sem;

	/* page is wholly or partially inside EOF */
	if (((loff_t)(page->index + 1) << PAGE_SHIFT) >
						i_size_read(iyesde)) {
		loff_t offset;

		offset = i_size_read(iyesde) & ~PAGE_MASK;
		zero_user_segment(page, offset, PAGE_SIZE);
	}
	set_page_dirty(page);
	if (!PageUptodate(page))
		SetPageUptodate(page);

	f2fs_update_iostat(sbi, APP_MAPPED_IO, F2FS_BLKSIZE);
	f2fs_update_time(sbi, REQ_TIME);

	trace_f2fs_vm_page_mkwrite(page, DATA);
out_sem:
	up_read(&F2FS_I(iyesde)->i_mmap_sem);

	f2fs_balance_fs(sbi, dn.yesde_changed);

	sb_end_pagefault(iyesde->i_sb);
err:
	return block_page_mkwrite_return(err);
}

static const struct vm_operations_struct f2fs_file_vm_ops = {
	.fault		= f2fs_filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= f2fs_vm_page_mkwrite,
};

static int get_parent_iyes(struct iyesde *iyesde, nid_t *piyes)
{
	struct dentry *dentry;

	iyesde = igrab(iyesde);
	dentry = d_find_any_alias(iyesde);
	iput(iyesde);
	if (!dentry)
		return 0;

	*piyes = parent_iyes(dentry);
	dput(dentry);
	return 1;
}

static inline enum cp_reason_type need_do_checkpoint(struct iyesde *iyesde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	enum cp_reason_type cp_reason = CP_NO_NEEDED;

	if (!S_ISREG(iyesde->i_mode))
		cp_reason = CP_NON_REGULAR;
	else if (iyesde->i_nlink != 1)
		cp_reason = CP_HARDLINK;
	else if (is_sbi_flag_set(sbi, SBI_NEED_CP))
		cp_reason = CP_SB_NEED_CP;
	else if (file_wrong_piyes(iyesde))
		cp_reason = CP_WRONG_PINO;
	else if (!f2fs_space_for_roll_forward(sbi))
		cp_reason = CP_NO_SPC_ROLL;
	else if (!f2fs_is_checkpointed_yesde(sbi, F2FS_I(iyesde)->i_piyes))
		cp_reason = CP_NODE_NEED_CP;
	else if (test_opt(sbi, FASTBOOT))
		cp_reason = CP_FASTBOOT_MODE;
	else if (F2FS_OPTION(sbi).active_logs == 2)
		cp_reason = CP_SPEC_LOG_NUM;
	else if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT &&
		f2fs_need_dentry_mark(sbi, iyesde->i_iyes) &&
		f2fs_exist_written_data(sbi, F2FS_I(iyesde)->i_piyes,
							TRANS_DIR_INO))
		cp_reason = CP_RECOVER_DIR;

	return cp_reason;
}

static bool need_iyesde_page_update(struct f2fs_sb_info *sbi, nid_t iyes)
{
	struct page *i = find_get_page(NODE_MAPPING(sbi), iyes);
	bool ret = false;
	/* But we need to avoid that there are some iyesde updates */
	if ((i && PageDirty(i)) || f2fs_need_iyesde_block_update(sbi, iyes))
		ret = true;
	f2fs_put_page(i, 0);
	return ret;
}

static void try_to_fix_piyes(struct iyesde *iyesde)
{
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	nid_t piyes;

	down_write(&fi->i_sem);
	if (file_wrong_piyes(iyesde) && iyesde->i_nlink == 1 &&
			get_parent_iyes(iyesde, &piyes)) {
		f2fs_i_piyes_write(iyesde, piyes);
		file_got_piyes(iyesde);
	}
	up_write(&fi->i_sem);
}

static int f2fs_do_sync_file(struct file *file, loff_t start, loff_t end,
						int datasync, bool atomic)
{
	struct iyesde *iyesde = file->f_mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	nid_t iyes = iyesde->i_iyes;
	int ret = 0;
	enum cp_reason_type cp_reason = 0;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.for_reclaim = 0,
	};
	unsigned int seq_id = 0;

	if (unlikely(f2fs_readonly(iyesde->i_sb) ||
				is_sbi_flag_set(sbi, SBI_CP_DISABLED)))
		return 0;

	trace_f2fs_sync_file_enter(iyesde);

	if (S_ISDIR(iyesde->i_mode))
		goto go_write;

	/* if fdatasync is triggered, let's do in-place-update */
	if (datasync || get_dirty_pages(iyesde) <= SM_I(sbi)->min_fsync_blocks)
		set_iyesde_flag(iyesde, FI_NEED_IPU);
	ret = file_write_and_wait_range(file, start, end);
	clear_iyesde_flag(iyesde, FI_NEED_IPU);

	if (ret) {
		trace_f2fs_sync_file_exit(iyesde, cp_reason, datasync, ret);
		return ret;
	}

	/* if the iyesde is dirty, let's recover all the time */
	if (!f2fs_skip_iyesde_update(iyesde, datasync)) {
		f2fs_write_iyesde(iyesde, NULL);
		goto go_write;
	}

	/*
	 * if there is yes written data, don't waste time to write recovery info.
	 */
	if (!is_iyesde_flag_set(iyesde, FI_APPEND_WRITE) &&
			!f2fs_exist_written_data(sbi, iyes, APPEND_INO)) {

		/* it may call write_iyesde just prior to fsync */
		if (need_iyesde_page_update(sbi, iyes))
			goto go_write;

		if (is_iyesde_flag_set(iyesde, FI_UPDATE_WRITE) ||
				f2fs_exist_written_data(sbi, iyes, UPDATE_INO))
			goto flush_out;
		goto out;
	}
go_write:
	/*
	 * Both of fdatasync() and fsync() are able to be recovered from
	 * sudden-power-off.
	 */
	down_read(&F2FS_I(iyesde)->i_sem);
	cp_reason = need_do_checkpoint(iyesde);
	up_read(&F2FS_I(iyesde)->i_sem);

	if (cp_reason) {
		/* all the dirty yesde pages should be flushed for POR */
		ret = f2fs_sync_fs(iyesde->i_sb, 1);

		/*
		 * We've secured consistency through sync_fs. Following piyes
		 * will be used only for fsynced iyesdes after checkpoint.
		 */
		try_to_fix_piyes(iyesde);
		clear_iyesde_flag(iyesde, FI_APPEND_WRITE);
		clear_iyesde_flag(iyesde, FI_UPDATE_WRITE);
		goto out;
	}
sync_yesdes:
	atomic_inc(&sbi->wb_sync_req[NODE]);
	ret = f2fs_fsync_yesde_pages(sbi, iyesde, &wbc, atomic, &seq_id);
	atomic_dec(&sbi->wb_sync_req[NODE]);
	if (ret)
		goto out;

	/* if cp_error was enabled, we should avoid infinite loop */
	if (unlikely(f2fs_cp_error(sbi))) {
		ret = -EIO;
		goto out;
	}

	if (f2fs_need_iyesde_block_update(sbi, iyes)) {
		f2fs_mark_iyesde_dirty_sync(iyesde, true);
		f2fs_write_iyesde(iyesde, NULL);
		goto sync_yesdes;
	}

	/*
	 * If it's atomic_write, it's just fine to keep write ordering. So
	 * here we don't need to wait for yesde write completion, since we use
	 * yesde chain which serializes yesde blocks. If one of yesde writes are
	 * reordered, we can see simply broken chain, resulting in stopping
	 * roll-forward recovery. It means we'll recover all or yesne yesde blocks
	 * given fsync mark.
	 */
	if (!atomic) {
		ret = f2fs_wait_on_yesde_pages_writeback(sbi, seq_id);
		if (ret)
			goto out;
	}

	/* once recovery info is written, don't need to tack this */
	f2fs_remove_iyes_entry(sbi, iyes, APPEND_INO);
	clear_iyesde_flag(iyesde, FI_APPEND_WRITE);
flush_out:
	if (!atomic && F2FS_OPTION(sbi).fsync_mode != FSYNC_MODE_NOBARRIER)
		ret = f2fs_issue_flush(sbi, iyesde->i_iyes);
	if (!ret) {
		f2fs_remove_iyes_entry(sbi, iyes, UPDATE_INO);
		clear_iyesde_flag(iyesde, FI_UPDATE_WRITE);
		f2fs_remove_iyes_entry(sbi, iyes, FLUSH_INO);
	}
	f2fs_update_time(sbi, REQ_TIME);
out:
	trace_f2fs_sync_file_exit(iyesde, cp_reason, datasync, ret);
	f2fs_trace_ios(NULL, 1);
	return ret;
}

int f2fs_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	if (unlikely(f2fs_cp_error(F2FS_I_SB(file_iyesde(file)))))
		return -EIO;
	return f2fs_do_sync_file(file, start, end, datasync, false);
}

static pgoff_t __get_first_dirty_index(struct address_space *mapping,
						pgoff_t pgofs, int whence)
{
	struct page *page;
	int nr_pages;

	if (whence != SEEK_DATA)
		return 0;

	/* find first dirty page index */
	nr_pages = find_get_pages_tag(mapping, &pgofs, PAGECACHE_TAG_DIRTY,
				      1, &page);
	if (!nr_pages)
		return ULONG_MAX;
	pgofs = page->index;
	put_page(page);
	return pgofs;
}

static bool __found_offset(struct f2fs_sb_info *sbi, block_t blkaddr,
				pgoff_t dirty, pgoff_t pgofs, int whence)
{
	switch (whence) {
	case SEEK_DATA:
		if ((blkaddr == NEW_ADDR && dirty == pgofs) ||
			__is_valid_data_blkaddr(blkaddr))
			return true;
		break;
	case SEEK_HOLE:
		if (blkaddr == NULL_ADDR)
			return true;
		break;
	}
	return false;
}

static loff_t f2fs_seek_block(struct file *file, loff_t offset, int whence)
{
	struct iyesde *iyesde = file->f_mapping->host;
	loff_t maxbytes = iyesde->i_sb->s_maxbytes;
	struct dyesde_of_data dn;
	pgoff_t pgofs, end_offset, dirty;
	loff_t data_ofs = offset;
	loff_t isize;
	int err = 0;

	iyesde_lock(iyesde);

	isize = i_size_read(iyesde);
	if (offset >= isize)
		goto fail;

	/* handle inline data case */
	if (f2fs_has_inline_data(iyesde) || f2fs_has_inline_dentry(iyesde)) {
		if (whence == SEEK_HOLE)
			data_ofs = isize;
		goto found;
	}

	pgofs = (pgoff_t)(offset >> PAGE_SHIFT);

	dirty = __get_first_dirty_index(iyesde->i_mapping, pgofs, whence);

	for (; data_ofs < isize; data_ofs = (loff_t)pgofs << PAGE_SHIFT) {
		set_new_dyesde(&dn, iyesde, NULL, NULL, 0);
		err = f2fs_get_dyesde_of_data(&dn, pgofs, LOOKUP_NODE);
		if (err && err != -ENOENT) {
			goto fail;
		} else if (err == -ENOENT) {
			/* direct yesde does yest exists */
			if (whence == SEEK_DATA) {
				pgofs = f2fs_get_next_page_offset(&dn, pgofs);
				continue;
			} else {
				goto found;
			}
		}

		end_offset = ADDRS_PER_PAGE(dn.yesde_page, iyesde);

		/* find data/hole in dyesde block */
		for (; dn.ofs_in_yesde < end_offset;
				dn.ofs_in_yesde++, pgofs++,
				data_ofs = (loff_t)pgofs << PAGE_SHIFT) {
			block_t blkaddr;

			blkaddr = datablock_addr(dn.iyesde,
					dn.yesde_page, dn.ofs_in_yesde);

			if (__is_valid_data_blkaddr(blkaddr) &&
				!f2fs_is_valid_blkaddr(F2FS_I_SB(iyesde),
					blkaddr, DATA_GENERIC_ENHANCE)) {
				f2fs_put_dyesde(&dn);
				goto fail;
			}

			if (__found_offset(F2FS_I_SB(iyesde), blkaddr, dirty,
							pgofs, whence)) {
				f2fs_put_dyesde(&dn);
				goto found;
			}
		}
		f2fs_put_dyesde(&dn);
	}

	if (whence == SEEK_DATA)
		goto fail;
found:
	if (whence == SEEK_HOLE && data_ofs > isize)
		data_ofs = isize;
	iyesde_unlock(iyesde);
	return vfs_setpos(file, data_ofs, maxbytes);
fail:
	iyesde_unlock(iyesde);
	return -ENXIO;
}

static loff_t f2fs_llseek(struct file *file, loff_t offset, int whence)
{
	struct iyesde *iyesde = file->f_mapping->host;
	loff_t maxbytes = iyesde->i_sb->s_maxbytes;

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		return generic_file_llseek_size(file, offset, whence,
						maxbytes, i_size_read(iyesde));
	case SEEK_DATA:
	case SEEK_HOLE:
		if (offset < 0)
			return -ENXIO;
		return f2fs_seek_block(file, offset, whence);
	}

	return -EINVAL;
}

static int f2fs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct iyesde *iyesde = file_iyesde(file);
	int err;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(iyesde))))
		return -EIO;

	/* we don't need to use inline_data strictly */
	err = f2fs_convert_inline_iyesde(iyesde);
	if (err)
		return err;

	file_accessed(file);
	vma->vm_ops = &f2fs_file_vm_ops;
	return 0;
}

static int f2fs_file_open(struct iyesde *iyesde, struct file *filp)
{
	int err = fscrypt_file_open(iyesde, filp);

	if (err)
		return err;

	err = fsverity_file_open(iyesde, filp);
	if (err)
		return err;

	filp->f_mode |= FMODE_NOWAIT;

	return dquot_file_open(iyesde, filp);
}

void f2fs_truncate_data_blocks_range(struct dyesde_of_data *dn, int count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->iyesde);
	struct f2fs_yesde *raw_yesde;
	int nr_free = 0, ofs = dn->ofs_in_yesde, len = count;
	__le32 *addr;
	int base = 0;

	if (IS_INODE(dn->yesde_page) && f2fs_has_extra_attr(dn->iyesde))
		base = get_extra_isize(dn->iyesde);

	raw_yesde = F2FS_NODE(dn->yesde_page);
	addr = blkaddr_in_yesde(raw_yesde) + base + ofs;

	for (; count > 0; count--, addr++, dn->ofs_in_yesde++) {
		block_t blkaddr = le32_to_cpu(*addr);

		if (blkaddr == NULL_ADDR)
			continue;

		dn->data_blkaddr = NULL_ADDR;
		f2fs_set_data_blkaddr(dn);

		if (__is_valid_data_blkaddr(blkaddr) &&
			!f2fs_is_valid_blkaddr(sbi, blkaddr,
					DATA_GENERIC_ENHANCE))
			continue;

		f2fs_invalidate_blocks(sbi, blkaddr);
		if (dn->ofs_in_yesde == 0 && IS_INODE(dn->yesde_page))
			clear_iyesde_flag(dn->iyesde, FI_FIRST_BLOCK_WRITTEN);
		nr_free++;
	}

	if (nr_free) {
		pgoff_t fofs;
		/*
		 * once we invalidate valid blkaddr in range [ofs, ofs + count],
		 * we will invalidate all blkaddr in the whole range.
		 */
		fofs = f2fs_start_bidx_of_yesde(ofs_of_yesde(dn->yesde_page),
							dn->iyesde) + ofs;
		f2fs_update_extent_cache_range(dn, fofs, 0, len);
		dec_valid_block_count(sbi, dn->iyesde, nr_free);
	}
	dn->ofs_in_yesde = ofs;

	f2fs_update_time(sbi, REQ_TIME);
	trace_f2fs_truncate_data_blocks_range(dn->iyesde, dn->nid,
					 dn->ofs_in_yesde, nr_free);
}

void f2fs_truncate_data_blocks(struct dyesde_of_data *dn)
{
	f2fs_truncate_data_blocks_range(dn, ADDRS_PER_BLOCK(dn->iyesde));
}

static int truncate_partial_data_page(struct iyesde *iyesde, u64 from,
								bool cache_only)
{
	loff_t offset = from & (PAGE_SIZE - 1);
	pgoff_t index = from >> PAGE_SHIFT;
	struct address_space *mapping = iyesde->i_mapping;
	struct page *page;

	if (!offset && !cache_only)
		return 0;

	if (cache_only) {
		page = find_lock_page(mapping, index);
		if (page && PageUptodate(page))
			goto truncate_out;
		f2fs_put_page(page, 1);
		return 0;
	}

	page = f2fs_get_lock_data_page(iyesde, index, true);
	if (IS_ERR(page))
		return PTR_ERR(page) == -ENOENT ? 0 : PTR_ERR(page);
truncate_out:
	f2fs_wait_on_page_writeback(page, DATA, true, true);
	zero_user(page, offset, PAGE_SIZE - offset);

	/* An encrypted iyesde should have a key and truncate the last page. */
	f2fs_bug_on(F2FS_I_SB(iyesde), cache_only && IS_ENCRYPTED(iyesde));
	if (!cache_only)
		set_page_dirty(page);
	f2fs_put_page(page, 1);
	return 0;
}

int f2fs_truncate_blocks(struct iyesde *iyesde, u64 from, bool lock)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct dyesde_of_data dn;
	pgoff_t free_from;
	int count = 0, err = 0;
	struct page *ipage;
	bool truncate_page = false;

	trace_f2fs_truncate_blocks_enter(iyesde, from);

	free_from = (pgoff_t)F2FS_BLK_ALIGN(from);

	if (free_from >= sbi->max_file_blocks)
		goto free_partial;

	if (lock)
		f2fs_lock_op(sbi);

	ipage = f2fs_get_yesde_page(sbi, iyesde->i_iyes);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out;
	}

	if (f2fs_has_inline_data(iyesde)) {
		f2fs_truncate_inline_iyesde(iyesde, ipage, from);
		f2fs_put_page(ipage, 1);
		truncate_page = true;
		goto out;
	}

	set_new_dyesde(&dn, iyesde, ipage, NULL, 0);
	err = f2fs_get_dyesde_of_data(&dn, free_from, LOOKUP_NODE_RA);
	if (err) {
		if (err == -ENOENT)
			goto free_next;
		goto out;
	}

	count = ADDRS_PER_PAGE(dn.yesde_page, iyesde);

	count -= dn.ofs_in_yesde;
	f2fs_bug_on(sbi, count < 0);

	if (dn.ofs_in_yesde || IS_INODE(dn.yesde_page)) {
		f2fs_truncate_data_blocks_range(&dn, count);
		free_from += count;
	}

	f2fs_put_dyesde(&dn);
free_next:
	err = f2fs_truncate_iyesde_blocks(iyesde, free_from);
out:
	if (lock)
		f2fs_unlock_op(sbi);
free_partial:
	/* lastly zero out the first data page */
	if (!err)
		err = truncate_partial_data_page(iyesde, from, truncate_page);

	trace_f2fs_truncate_blocks_exit(iyesde, err);
	return err;
}

int f2fs_truncate(struct iyesde *iyesde)
{
	int err;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(iyesde))))
		return -EIO;

	if (!(S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode) ||
				S_ISLNK(iyesde->i_mode)))
		return 0;

	trace_f2fs_truncate(iyesde);

	if (time_to_inject(F2FS_I_SB(iyesde), FAULT_TRUNCATE)) {
		f2fs_show_injection_info(F2FS_I_SB(iyesde), FAULT_TRUNCATE);
		return -EIO;
	}

	/* we should check inline_data size */
	if (!f2fs_may_inline_data(iyesde)) {
		err = f2fs_convert_inline_iyesde(iyesde);
		if (err)
			return err;
	}

	err = f2fs_truncate_blocks(iyesde, i_size_read(iyesde), true);
	if (err)
		return err;

	iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	f2fs_mark_iyesde_dirty_sync(iyesde, false);
	return 0;
}

int f2fs_getattr(const struct path *path, struct kstat *stat,
		 u32 request_mask, unsigned int query_flags)
{
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	struct f2fs_iyesde *ri;
	unsigned int flags;

	if (f2fs_has_extra_attr(iyesde) &&
			f2fs_sb_has_iyesde_crtime(F2FS_I_SB(iyesde)) &&
			F2FS_FITS_IN_INODE(ri, fi->i_extra_isize, i_crtime)) {
		stat->result_mask |= STATX_BTIME;
		stat->btime.tv_sec = fi->i_crtime.tv_sec;
		stat->btime.tv_nsec = fi->i_crtime.tv_nsec;
	}

	flags = fi->i_flags;
	if (flags & F2FS_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (IS_ENCRYPTED(iyesde))
		stat->attributes |= STATX_ATTR_ENCRYPTED;
	if (flags & F2FS_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (flags & F2FS_NODUMP_FL)
		stat->attributes |= STATX_ATTR_NODUMP;
	if (IS_VERITY(iyesde))
		stat->attributes |= STATX_ATTR_VERITY;

	stat->attributes_mask |= (STATX_ATTR_APPEND |
				  STATX_ATTR_ENCRYPTED |
				  STATX_ATTR_IMMUTABLE |
				  STATX_ATTR_NODUMP |
				  STATX_ATTR_VERITY);

	generic_fillattr(iyesde, stat);

	/* we need to show initial sectors used for inline_data/dentries */
	if ((S_ISREG(iyesde->i_mode) && f2fs_has_inline_data(iyesde)) ||
					f2fs_has_inline_dentry(iyesde))
		stat->blocks += (stat->size + 511) >> 9;

	return 0;
}

#ifdef CONFIG_F2FS_FS_POSIX_ACL
static void __setattr_copy(struct iyesde *iyesde, const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_UID)
		iyesde->i_uid = attr->ia_uid;
	if (ia_valid & ATTR_GID)
		iyesde->i_gid = attr->ia_gid;
	if (ia_valid & ATTR_ATIME) {
		iyesde->i_atime = timestamp_truncate(attr->ia_atime,
						  iyesde);
	}
	if (ia_valid & ATTR_MTIME) {
		iyesde->i_mtime = timestamp_truncate(attr->ia_mtime,
						  iyesde);
	}
	if (ia_valid & ATTR_CTIME) {
		iyesde->i_ctime = timestamp_truncate(attr->ia_ctime,
						  iyesde);
	}
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;

		if (!in_group_p(iyesde->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		set_acl_iyesde(iyesde, mode);
	}
}
#else
#define __setattr_copy setattr_copy
#endif

int f2fs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int err;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(iyesde))))
		return -EIO;

	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	err = fscrypt_prepare_setattr(dentry, attr);
	if (err)
		return err;

	err = fsverity_prepare_setattr(dentry, attr);
	if (err)
		return err;

	if (is_quota_modification(iyesde, attr)) {
		err = dquot_initialize(iyesde);
		if (err)
			return err;
	}
	if ((attr->ia_valid & ATTR_UID &&
		!uid_eq(attr->ia_uid, iyesde->i_uid)) ||
		(attr->ia_valid & ATTR_GID &&
		!gid_eq(attr->ia_gid, iyesde->i_gid))) {
		f2fs_lock_op(F2FS_I_SB(iyesde));
		err = dquot_transfer(iyesde, attr);
		if (err) {
			set_sbi_flag(F2FS_I_SB(iyesde),
					SBI_QUOTA_NEED_REPAIR);
			f2fs_unlock_op(F2FS_I_SB(iyesde));
			return err;
		}
		/*
		 * update uid/gid under lock_op(), so that dquot and iyesde can
		 * be updated atomically.
		 */
		if (attr->ia_valid & ATTR_UID)
			iyesde->i_uid = attr->ia_uid;
		if (attr->ia_valid & ATTR_GID)
			iyesde->i_gid = attr->ia_gid;
		f2fs_mark_iyesde_dirty_sync(iyesde, true);
		f2fs_unlock_op(F2FS_I_SB(iyesde));
	}

	if (attr->ia_valid & ATTR_SIZE) {
		loff_t old_size = i_size_read(iyesde);

		if (attr->ia_size > MAX_INLINE_DATA(iyesde)) {
			/*
			 * should convert inline iyesde before i_size_write to
			 * keep smaller than inline_data size with inline flag.
			 */
			err = f2fs_convert_inline_iyesde(iyesde);
			if (err)
				return err;
		}

		down_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
		down_write(&F2FS_I(iyesde)->i_mmap_sem);

		truncate_setsize(iyesde, attr->ia_size);

		if (attr->ia_size <= old_size)
			err = f2fs_truncate(iyesde);
		/*
		 * do yest trim all blocks after i_size if target size is
		 * larger than i_size.
		 */
		up_write(&F2FS_I(iyesde)->i_mmap_sem);
		up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
		if (err)
			return err;

		down_write(&F2FS_I(iyesde)->i_sem);
		iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
		F2FS_I(iyesde)->last_disk_size = i_size_read(iyesde);
		up_write(&F2FS_I(iyesde)->i_sem);
	}

	__setattr_copy(iyesde, attr);

	if (attr->ia_valid & ATTR_MODE) {
		err = posix_acl_chmod(iyesde, f2fs_get_iyesde_mode(iyesde));
		if (err || is_iyesde_flag_set(iyesde, FI_ACL_MODE)) {
			iyesde->i_mode = F2FS_I(iyesde)->i_acl_mode;
			clear_iyesde_flag(iyesde, FI_ACL_MODE);
		}
	}

	/* file size may changed here */
	f2fs_mark_iyesde_dirty_sync(iyesde, true);

	/* iyesde change will produce dirty yesde pages flushed by checkpoint */
	f2fs_balance_fs(F2FS_I_SB(iyesde), true);

	return err;
}

const struct iyesde_operations f2fs_file_iyesde_operations = {
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
#ifdef CONFIG_F2FS_FS_XATTR
	.listxattr	= f2fs_listxattr,
#endif
	.fiemap		= f2fs_fiemap,
};

static int fill_zero(struct iyesde *iyesde, pgoff_t index,
					loff_t start, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct page *page;

	if (!len)
		return 0;

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	page = f2fs_get_new_data_page(iyesde, NULL, index, false);
	f2fs_unlock_op(sbi);

	if (IS_ERR(page))
		return PTR_ERR(page);

	f2fs_wait_on_page_writeback(page, DATA, true, true);
	zero_user(page, start, len);
	set_page_dirty(page);
	f2fs_put_page(page, 1);
	return 0;
}

int f2fs_truncate_hole(struct iyesde *iyesde, pgoff_t pg_start, pgoff_t pg_end)
{
	int err;

	while (pg_start < pg_end) {
		struct dyesde_of_data dn;
		pgoff_t end_offset, count;

		set_new_dyesde(&dn, iyesde, NULL, NULL, 0);
		err = f2fs_get_dyesde_of_data(&dn, pg_start, LOOKUP_NODE);
		if (err) {
			if (err == -ENOENT) {
				pg_start = f2fs_get_next_page_offset(&dn,
								pg_start);
				continue;
			}
			return err;
		}

		end_offset = ADDRS_PER_PAGE(dn.yesde_page, iyesde);
		count = min(end_offset - dn.ofs_in_yesde, pg_end - pg_start);

		f2fs_bug_on(F2FS_I_SB(iyesde), count == 0 || count > end_offset);

		f2fs_truncate_data_blocks_range(&dn, count);
		f2fs_put_dyesde(&dn);

		pg_start += count;
	}
	return 0;
}

static int punch_hole(struct iyesde *iyesde, loff_t offset, loff_t len)
{
	pgoff_t pg_start, pg_end;
	loff_t off_start, off_end;
	int ret;

	ret = f2fs_convert_inline_iyesde(iyesde);
	if (ret)
		return ret;

	pg_start = ((unsigned long long) offset) >> PAGE_SHIFT;
	pg_end = ((unsigned long long) offset + len) >> PAGE_SHIFT;

	off_start = offset & (PAGE_SIZE - 1);
	off_end = (offset + len) & (PAGE_SIZE - 1);

	if (pg_start == pg_end) {
		ret = fill_zero(iyesde, pg_start, off_start,
						off_end - off_start);
		if (ret)
			return ret;
	} else {
		if (off_start) {
			ret = fill_zero(iyesde, pg_start++, off_start,
						PAGE_SIZE - off_start);
			if (ret)
				return ret;
		}
		if (off_end) {
			ret = fill_zero(iyesde, pg_end, 0, off_end);
			if (ret)
				return ret;
		}

		if (pg_start < pg_end) {
			struct address_space *mapping = iyesde->i_mapping;
			loff_t blk_start, blk_end;
			struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);

			f2fs_balance_fs(sbi, true);

			blk_start = (loff_t)pg_start << PAGE_SHIFT;
			blk_end = (loff_t)pg_end << PAGE_SHIFT;

			down_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
			down_write(&F2FS_I(iyesde)->i_mmap_sem);

			truncate_iyesde_pages_range(mapping, blk_start,
					blk_end - 1);

			f2fs_lock_op(sbi);
			ret = f2fs_truncate_hole(iyesde, pg_start, pg_end);
			f2fs_unlock_op(sbi);

			up_write(&F2FS_I(iyesde)->i_mmap_sem);
			up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
		}
	}

	return ret;
}

static int __read_out_blkaddrs(struct iyesde *iyesde, block_t *blkaddr,
				int *do_replace, pgoff_t off, pgoff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct dyesde_of_data dn;
	int ret, done, i;

next_dyesde:
	set_new_dyesde(&dn, iyesde, NULL, NULL, 0);
	ret = f2fs_get_dyesde_of_data(&dn, off, LOOKUP_NODE_RA);
	if (ret && ret != -ENOENT) {
		return ret;
	} else if (ret == -ENOENT) {
		if (dn.max_level == 0)
			return -ENOENT;
		done = min((pgoff_t)ADDRS_PER_BLOCK(iyesde) - dn.ofs_in_yesde,
									len);
		blkaddr += done;
		do_replace += done;
		goto next;
	}

	done = min((pgoff_t)ADDRS_PER_PAGE(dn.yesde_page, iyesde) -
							dn.ofs_in_yesde, len);
	for (i = 0; i < done; i++, blkaddr++, do_replace++, dn.ofs_in_yesde++) {
		*blkaddr = datablock_addr(dn.iyesde,
					dn.yesde_page, dn.ofs_in_yesde);

		if (__is_valid_data_blkaddr(*blkaddr) &&
			!f2fs_is_valid_blkaddr(sbi, *blkaddr,
					DATA_GENERIC_ENHANCE)) {
			f2fs_put_dyesde(&dn);
			return -EFSCORRUPTED;
		}

		if (!f2fs_is_checkpointed_data(sbi, *blkaddr)) {

			if (test_opt(sbi, LFS)) {
				f2fs_put_dyesde(&dn);
				return -EOPNOTSUPP;
			}

			/* do yest invalidate this block address */
			f2fs_update_data_blkaddr(&dn, NULL_ADDR);
			*do_replace = 1;
		}
	}
	f2fs_put_dyesde(&dn);
next:
	len -= done;
	off += done;
	if (len)
		goto next_dyesde;
	return 0;
}

static int __roll_back_blkaddrs(struct iyesde *iyesde, block_t *blkaddr,
				int *do_replace, pgoff_t off, int len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct dyesde_of_data dn;
	int ret, i;

	for (i = 0; i < len; i++, do_replace++, blkaddr++) {
		if (*do_replace == 0)
			continue;

		set_new_dyesde(&dn, iyesde, NULL, NULL, 0);
		ret = f2fs_get_dyesde_of_data(&dn, off + i, LOOKUP_NODE_RA);
		if (ret) {
			dec_valid_block_count(sbi, iyesde, 1);
			f2fs_invalidate_blocks(sbi, *blkaddr);
		} else {
			f2fs_update_data_blkaddr(&dn, *blkaddr);
		}
		f2fs_put_dyesde(&dn);
	}
	return 0;
}

static int __clone_blkaddrs(struct iyesde *src_iyesde, struct iyesde *dst_iyesde,
			block_t *blkaddr, int *do_replace,
			pgoff_t src, pgoff_t dst, pgoff_t len, bool full)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(src_iyesde);
	pgoff_t i = 0;
	int ret;

	while (i < len) {
		if (blkaddr[i] == NULL_ADDR && !full) {
			i++;
			continue;
		}

		if (do_replace[i] || blkaddr[i] == NULL_ADDR) {
			struct dyesde_of_data dn;
			struct yesde_info ni;
			size_t new_size;
			pgoff_t ilen;

			set_new_dyesde(&dn, dst_iyesde, NULL, NULL, 0);
			ret = f2fs_get_dyesde_of_data(&dn, dst + i, ALLOC_NODE);
			if (ret)
				return ret;

			ret = f2fs_get_yesde_info(sbi, dn.nid, &ni);
			if (ret) {
				f2fs_put_dyesde(&dn);
				return ret;
			}

			ilen = min((pgoff_t)
				ADDRS_PER_PAGE(dn.yesde_page, dst_iyesde) -
						dn.ofs_in_yesde, len - i);
			do {
				dn.data_blkaddr = datablock_addr(dn.iyesde,
						dn.yesde_page, dn.ofs_in_yesde);
				f2fs_truncate_data_blocks_range(&dn, 1);

				if (do_replace[i]) {
					f2fs_i_blocks_write(src_iyesde,
							1, false, false);
					f2fs_i_blocks_write(dst_iyesde,
							1, true, false);
					f2fs_replace_block(sbi, &dn, dn.data_blkaddr,
					blkaddr[i], ni.version, true, false);

					do_replace[i] = 0;
				}
				dn.ofs_in_yesde++;
				i++;
				new_size = (loff_t)(dst + i) << PAGE_SHIFT;
				if (dst_iyesde->i_size < new_size)
					f2fs_i_size_write(dst_iyesde, new_size);
			} while (--ilen && (do_replace[i] || blkaddr[i] == NULL_ADDR));

			f2fs_put_dyesde(&dn);
		} else {
			struct page *psrc, *pdst;

			psrc = f2fs_get_lock_data_page(src_iyesde,
							src + i, true);
			if (IS_ERR(psrc))
				return PTR_ERR(psrc);
			pdst = f2fs_get_new_data_page(dst_iyesde, NULL, dst + i,
								true);
			if (IS_ERR(pdst)) {
				f2fs_put_page(psrc, 1);
				return PTR_ERR(pdst);
			}
			f2fs_copy_page(psrc, pdst);
			set_page_dirty(pdst);
			f2fs_put_page(pdst, 1);
			f2fs_put_page(psrc, 1);

			ret = f2fs_truncate_hole(src_iyesde,
						src + i, src + i + 1);
			if (ret)
				return ret;
			i++;
		}
	}
	return 0;
}

static int __exchange_data_block(struct iyesde *src_iyesde,
			struct iyesde *dst_iyesde, pgoff_t src, pgoff_t dst,
			pgoff_t len, bool full)
{
	block_t *src_blkaddr;
	int *do_replace;
	pgoff_t olen;
	int ret;

	while (len) {
		olen = min((pgoff_t)4 * ADDRS_PER_BLOCK(src_iyesde), len);

		src_blkaddr = f2fs_kvzalloc(F2FS_I_SB(src_iyesde),
					array_size(olen, sizeof(block_t)),
					GFP_KERNEL);
		if (!src_blkaddr)
			return -ENOMEM;

		do_replace = f2fs_kvzalloc(F2FS_I_SB(src_iyesde),
					array_size(olen, sizeof(int)),
					GFP_KERNEL);
		if (!do_replace) {
			kvfree(src_blkaddr);
			return -ENOMEM;
		}

		ret = __read_out_blkaddrs(src_iyesde, src_blkaddr,
					do_replace, src, olen);
		if (ret)
			goto roll_back;

		ret = __clone_blkaddrs(src_iyesde, dst_iyesde, src_blkaddr,
					do_replace, src, dst, olen, full);
		if (ret)
			goto roll_back;

		src += olen;
		dst += olen;
		len -= olen;

		kvfree(src_blkaddr);
		kvfree(do_replace);
	}
	return 0;

roll_back:
	__roll_back_blkaddrs(src_iyesde, src_blkaddr, do_replace, src, olen);
	kvfree(src_blkaddr);
	kvfree(do_replace);
	return ret;
}

static int f2fs_do_collapse(struct iyesde *iyesde, loff_t offset, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	pgoff_t nrpages = DIV_ROUND_UP(i_size_read(iyesde), PAGE_SIZE);
	pgoff_t start = offset >> PAGE_SHIFT;
	pgoff_t end = (offset + len) >> PAGE_SHIFT;
	int ret;

	f2fs_balance_fs(sbi, true);

	/* avoid gc operation during block exchange */
	down_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
	down_write(&F2FS_I(iyesde)->i_mmap_sem);

	f2fs_lock_op(sbi);
	f2fs_drop_extent_tree(iyesde);
	truncate_pagecache(iyesde, offset);
	ret = __exchange_data_block(iyesde, iyesde, end, start, nrpages - end, true);
	f2fs_unlock_op(sbi);

	up_write(&F2FS_I(iyesde)->i_mmap_sem);
	up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
	return ret;
}

static int f2fs_collapse_range(struct iyesde *iyesde, loff_t offset, loff_t len)
{
	loff_t new_size;
	int ret;

	if (offset + len >= i_size_read(iyesde))
		return -EINVAL;

	/* collapse range should be aligned to block size of f2fs. */
	if (offset & (F2FS_BLKSIZE - 1) || len & (F2FS_BLKSIZE - 1))
		return -EINVAL;

	ret = f2fs_convert_inline_iyesde(iyesde);
	if (ret)
		return ret;

	/* write out all dirty pages from offset */
	ret = filemap_write_and_wait_range(iyesde->i_mapping, offset, LLONG_MAX);
	if (ret)
		return ret;

	ret = f2fs_do_collapse(iyesde, offset, len);
	if (ret)
		return ret;

	/* write out all moved pages, if possible */
	down_write(&F2FS_I(iyesde)->i_mmap_sem);
	filemap_write_and_wait_range(iyesde->i_mapping, offset, LLONG_MAX);
	truncate_pagecache(iyesde, offset);

	new_size = i_size_read(iyesde) - len;
	truncate_pagecache(iyesde, new_size);

	ret = f2fs_truncate_blocks(iyesde, new_size, true);
	up_write(&F2FS_I(iyesde)->i_mmap_sem);
	if (!ret)
		f2fs_i_size_write(iyesde, new_size);
	return ret;
}

static int f2fs_do_zero_range(struct dyesde_of_data *dn, pgoff_t start,
								pgoff_t end)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->iyesde);
	pgoff_t index = start;
	unsigned int ofs_in_yesde = dn->ofs_in_yesde;
	blkcnt_t count = 0;
	int ret;

	for (; index < end; index++, dn->ofs_in_yesde++) {
		if (datablock_addr(dn->iyesde, dn->yesde_page,
					dn->ofs_in_yesde) == NULL_ADDR)
			count++;
	}

	dn->ofs_in_yesde = ofs_in_yesde;
	ret = f2fs_reserve_new_blocks(dn, count);
	if (ret)
		return ret;

	dn->ofs_in_yesde = ofs_in_yesde;
	for (index = start; index < end; index++, dn->ofs_in_yesde++) {
		dn->data_blkaddr = datablock_addr(dn->iyesde,
					dn->yesde_page, dn->ofs_in_yesde);
		/*
		 * f2fs_reserve_new_blocks will yest guarantee entire block
		 * allocation.
		 */
		if (dn->data_blkaddr == NULL_ADDR) {
			ret = -ENOSPC;
			break;
		}
		if (dn->data_blkaddr != NEW_ADDR) {
			f2fs_invalidate_blocks(sbi, dn->data_blkaddr);
			dn->data_blkaddr = NEW_ADDR;
			f2fs_set_data_blkaddr(dn);
		}
	}

	f2fs_update_extent_cache_range(dn, start, 0, index - start);

	return ret;
}

static int f2fs_zero_range(struct iyesde *iyesde, loff_t offset, loff_t len,
								int mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct address_space *mapping = iyesde->i_mapping;
	pgoff_t index, pg_start, pg_end;
	loff_t new_size = i_size_read(iyesde);
	loff_t off_start, off_end;
	int ret = 0;

	ret = iyesde_newsize_ok(iyesde, (len + offset));
	if (ret)
		return ret;

	ret = f2fs_convert_inline_iyesde(iyesde);
	if (ret)
		return ret;

	ret = filemap_write_and_wait_range(mapping, offset, offset + len - 1);
	if (ret)
		return ret;

	pg_start = ((unsigned long long) offset) >> PAGE_SHIFT;
	pg_end = ((unsigned long long) offset + len) >> PAGE_SHIFT;

	off_start = offset & (PAGE_SIZE - 1);
	off_end = (offset + len) & (PAGE_SIZE - 1);

	if (pg_start == pg_end) {
		ret = fill_zero(iyesde, pg_start, off_start,
						off_end - off_start);
		if (ret)
			return ret;

		new_size = max_t(loff_t, new_size, offset + len);
	} else {
		if (off_start) {
			ret = fill_zero(iyesde, pg_start++, off_start,
						PAGE_SIZE - off_start);
			if (ret)
				return ret;

			new_size = max_t(loff_t, new_size,
					(loff_t)pg_start << PAGE_SHIFT);
		}

		for (index = pg_start; index < pg_end;) {
			struct dyesde_of_data dn;
			unsigned int end_offset;
			pgoff_t end;

			down_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
			down_write(&F2FS_I(iyesde)->i_mmap_sem);

			truncate_pagecache_range(iyesde,
				(loff_t)index << PAGE_SHIFT,
				((loff_t)pg_end << PAGE_SHIFT) - 1);

			f2fs_lock_op(sbi);

			set_new_dyesde(&dn, iyesde, NULL, NULL, 0);
			ret = f2fs_get_dyesde_of_data(&dn, index, ALLOC_NODE);
			if (ret) {
				f2fs_unlock_op(sbi);
				up_write(&F2FS_I(iyesde)->i_mmap_sem);
				up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
				goto out;
			}

			end_offset = ADDRS_PER_PAGE(dn.yesde_page, iyesde);
			end = min(pg_end, end_offset - dn.ofs_in_yesde + index);

			ret = f2fs_do_zero_range(&dn, index, end);
			f2fs_put_dyesde(&dn);

			f2fs_unlock_op(sbi);
			up_write(&F2FS_I(iyesde)->i_mmap_sem);
			up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);

			f2fs_balance_fs(sbi, dn.yesde_changed);

			if (ret)
				goto out;

			index = end;
			new_size = max_t(loff_t, new_size,
					(loff_t)index << PAGE_SHIFT);
		}

		if (off_end) {
			ret = fill_zero(iyesde, pg_end, 0, off_end);
			if (ret)
				goto out;

			new_size = max_t(loff_t, new_size, offset + len);
		}
	}

out:
	if (new_size > i_size_read(iyesde)) {
		if (mode & FALLOC_FL_KEEP_SIZE)
			file_set_keep_isize(iyesde);
		else
			f2fs_i_size_write(iyesde, new_size);
	}
	return ret;
}

static int f2fs_insert_range(struct iyesde *iyesde, loff_t offset, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	pgoff_t nr, pg_start, pg_end, delta, idx;
	loff_t new_size;
	int ret = 0;

	new_size = i_size_read(iyesde) + len;
	ret = iyesde_newsize_ok(iyesde, new_size);
	if (ret)
		return ret;

	if (offset >= i_size_read(iyesde))
		return -EINVAL;

	/* insert range should be aligned to block size of f2fs. */
	if (offset & (F2FS_BLKSIZE - 1) || len & (F2FS_BLKSIZE - 1))
		return -EINVAL;

	ret = f2fs_convert_inline_iyesde(iyesde);
	if (ret)
		return ret;

	f2fs_balance_fs(sbi, true);

	down_write(&F2FS_I(iyesde)->i_mmap_sem);
	ret = f2fs_truncate_blocks(iyesde, i_size_read(iyesde), true);
	up_write(&F2FS_I(iyesde)->i_mmap_sem);
	if (ret)
		return ret;

	/* write out all dirty pages from offset */
	ret = filemap_write_and_wait_range(iyesde->i_mapping, offset, LLONG_MAX);
	if (ret)
		return ret;

	pg_start = offset >> PAGE_SHIFT;
	pg_end = (offset + len) >> PAGE_SHIFT;
	delta = pg_end - pg_start;
	idx = DIV_ROUND_UP(i_size_read(iyesde), PAGE_SIZE);

	/* avoid gc operation during block exchange */
	down_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
	down_write(&F2FS_I(iyesde)->i_mmap_sem);
	truncate_pagecache(iyesde, offset);

	while (!ret && idx > pg_start) {
		nr = idx - pg_start;
		if (nr > delta)
			nr = delta;
		idx -= nr;

		f2fs_lock_op(sbi);
		f2fs_drop_extent_tree(iyesde);

		ret = __exchange_data_block(iyesde, iyesde, idx,
					idx + delta, nr, false);
		f2fs_unlock_op(sbi);
	}
	up_write(&F2FS_I(iyesde)->i_mmap_sem);
	up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);

	/* write out all moved pages, if possible */
	down_write(&F2FS_I(iyesde)->i_mmap_sem);
	filemap_write_and_wait_range(iyesde->i_mapping, offset, LLONG_MAX);
	truncate_pagecache(iyesde, offset);
	up_write(&F2FS_I(iyesde)->i_mmap_sem);

	if (!ret)
		f2fs_i_size_write(iyesde, new_size);
	return ret;
}

static int expand_iyesde_data(struct iyesde *iyesde, loff_t offset,
					loff_t len, int mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct f2fs_map_blocks map = { .m_next_pgofs = NULL,
			.m_next_extent = NULL, .m_seg_type = NO_CHECK_TYPE,
			.m_may_create = true };
	pgoff_t pg_end;
	loff_t new_size = i_size_read(iyesde);
	loff_t off_end;
	int err;

	err = iyesde_newsize_ok(iyesde, (len + offset));
	if (err)
		return err;

	err = f2fs_convert_inline_iyesde(iyesde);
	if (err)
		return err;

	f2fs_balance_fs(sbi, true);

	pg_end = ((unsigned long long)offset + len) >> PAGE_SHIFT;
	off_end = (offset + len) & (PAGE_SIZE - 1);

	map.m_lblk = ((unsigned long long)offset) >> PAGE_SHIFT;
	map.m_len = pg_end - map.m_lblk;
	if (off_end)
		map.m_len++;

	if (!map.m_len)
		return 0;

	if (f2fs_is_pinned_file(iyesde)) {
		block_t len = (map.m_len >> sbi->log_blocks_per_seg) <<
					sbi->log_blocks_per_seg;
		block_t done = 0;

		if (map.m_len % sbi->blocks_per_seg)
			len += sbi->blocks_per_seg;

		map.m_len = sbi->blocks_per_seg;
next_alloc:
		if (has_yest_eyesugh_free_secs(sbi, 0,
			GET_SEC_FROM_SEG(sbi, overprovision_segments(sbi)))) {
			mutex_lock(&sbi->gc_mutex);
			err = f2fs_gc(sbi, true, false, NULL_SEGNO);
			if (err && err != -ENODATA && err != -EAGAIN)
				goto out_err;
		}

		down_write(&sbi->pin_sem);
		map.m_seg_type = CURSEG_COLD_DATA_PINNED;
		f2fs_allocate_new_segments(sbi, CURSEG_COLD_DATA);
		err = f2fs_map_blocks(iyesde, &map, 1, F2FS_GET_BLOCK_PRE_DIO);
		up_write(&sbi->pin_sem);

		done += map.m_len;
		len -= map.m_len;
		map.m_lblk += map.m_len;
		if (!err && len)
			goto next_alloc;

		map.m_len = done;
	} else {
		err = f2fs_map_blocks(iyesde, &map, 1, F2FS_GET_BLOCK_PRE_AIO);
	}
out_err:
	if (err) {
		pgoff_t last_off;

		if (!map.m_len)
			return err;

		last_off = map.m_lblk + map.m_len - 1;

		/* update new size to the failed position */
		new_size = (last_off == pg_end) ? offset + len :
					(loff_t)(last_off + 1) << PAGE_SHIFT;
	} else {
		new_size = ((loff_t)pg_end << PAGE_SHIFT) + off_end;
	}

	if (new_size > i_size_read(iyesde)) {
		if (mode & FALLOC_FL_KEEP_SIZE)
			file_set_keep_isize(iyesde);
		else
			f2fs_i_size_write(iyesde, new_size);
	}

	return err;
}

static long f2fs_fallocate(struct file *file, int mode,
				loff_t offset, loff_t len)
{
	struct iyesde *iyesde = file_iyesde(file);
	long ret = 0;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(iyesde))))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(F2FS_I_SB(iyesde)))
		return -ENOSPC;

	/* f2fs only support ->fallocate for regular file */
	if (!S_ISREG(iyesde->i_mode))
		return -EINVAL;

	if (IS_ENCRYPTED(iyesde) &&
		(mode & (FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_INSERT_RANGE)))
		return -EOPNOTSUPP;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
			FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE |
			FALLOC_FL_INSERT_RANGE))
		return -EOPNOTSUPP;

	iyesde_lock(iyesde);

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		if (offset >= iyesde->i_size)
			goto out;

		ret = punch_hole(iyesde, offset, len);
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		ret = f2fs_collapse_range(iyesde, offset, len);
	} else if (mode & FALLOC_FL_ZERO_RANGE) {
		ret = f2fs_zero_range(iyesde, offset, len, mode);
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		ret = f2fs_insert_range(iyesde, offset, len);
	} else {
		ret = expand_iyesde_data(iyesde, offset, len, mode);
	}

	if (!ret) {
		iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
		f2fs_mark_iyesde_dirty_sync(iyesde, false);
		f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);
	}

out:
	iyesde_unlock(iyesde);

	trace_f2fs_fallocate(iyesde, mode, offset, len, ret);
	return ret;
}

static int f2fs_release_file(struct iyesde *iyesde, struct file *filp)
{
	/*
	 * f2fs_relase_file is called at every close calls. So we should
	 * yest drop any inmemory pages by close called by other process.
	 */
	if (!(filp->f_mode & FMODE_WRITE) ||
			atomic_read(&iyesde->i_writecount) != 1)
		return 0;

	/* some remained atomic pages should discarded */
	if (f2fs_is_atomic_file(iyesde))
		f2fs_drop_inmem_pages(iyesde);
	if (f2fs_is_volatile_file(iyesde)) {
		set_iyesde_flag(iyesde, FI_DROP_CACHE);
		filemap_fdatawrite(iyesde->i_mapping);
		clear_iyesde_flag(iyesde, FI_DROP_CACHE);
		clear_iyesde_flag(iyesde, FI_VOLATILE_FILE);
		stat_dec_volatile_write(iyesde);
	}
	return 0;
}

static int f2fs_file_flush(struct file *file, fl_owner_t id)
{
	struct iyesde *iyesde = file_iyesde(file);

	/*
	 * If the process doing a transaction is crashed, we should do
	 * roll-back. Otherwise, other reader/write can see corrupted database
	 * until all the writers close its file. Since this should be done
	 * before dropping file lock, it needs to do in ->flush.
	 */
	if (f2fs_is_atomic_file(iyesde) &&
			F2FS_I(iyesde)->inmem_task == current)
		f2fs_drop_inmem_pages(iyesde);
	return 0;
}

static int f2fs_setflags_common(struct iyesde *iyesde, u32 iflags, u32 mask)
{
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);

	/* Is it quota file? Do yest allow user to mess with it */
	if (IS_NOQUOTA(iyesde))
		return -EPERM;

	if ((iflags ^ fi->i_flags) & F2FS_CASEFOLD_FL) {
		if (!f2fs_sb_has_casefold(F2FS_I_SB(iyesde)))
			return -EOPNOTSUPP;
		if (!f2fs_empty_dir(iyesde))
			return -ENOTEMPTY;
	}

	fi->i_flags = iflags | (fi->i_flags & ~mask);

	if (fi->i_flags & F2FS_PROJINHERIT_FL)
		set_iyesde_flag(iyesde, FI_PROJ_INHERIT);
	else
		clear_iyesde_flag(iyesde, FI_PROJ_INHERIT);

	iyesde->i_ctime = current_time(iyesde);
	f2fs_set_iyesde_flags(iyesde);
	f2fs_mark_iyesde_dirty_sync(iyesde, true);
	return 0;
}

/* FS_IOC_GETFLAGS and FS_IOC_SETFLAGS support */

/*
 * To make a new on-disk f2fs i_flag gettable via FS_IOC_GETFLAGS, add an entry
 * for it to f2fs_fsflags_map[], and add its FS_*_FL equivalent to
 * F2FS_GETTABLE_FS_FL.  To also make it settable via FS_IOC_SETFLAGS, also add
 * its FS_*_FL equivalent to F2FS_SETTABLE_FS_FL.
 */

static const struct {
	u32 iflag;
	u32 fsflag;
} f2fs_fsflags_map[] = {
	{ F2FS_SYNC_FL,		FS_SYNC_FL },
	{ F2FS_IMMUTABLE_FL,	FS_IMMUTABLE_FL },
	{ F2FS_APPEND_FL,	FS_APPEND_FL },
	{ F2FS_NODUMP_FL,	FS_NODUMP_FL },
	{ F2FS_NOATIME_FL,	FS_NOATIME_FL },
	{ F2FS_INDEX_FL,	FS_INDEX_FL },
	{ F2FS_DIRSYNC_FL,	FS_DIRSYNC_FL },
	{ F2FS_PROJINHERIT_FL,	FS_PROJINHERIT_FL },
	{ F2FS_CASEFOLD_FL,	FS_CASEFOLD_FL },
};

#define F2FS_GETTABLE_FS_FL (		\
		FS_SYNC_FL |		\
		FS_IMMUTABLE_FL |	\
		FS_APPEND_FL |		\
		FS_NODUMP_FL |		\
		FS_NOATIME_FL |		\
		FS_INDEX_FL |		\
		FS_DIRSYNC_FL |		\
		FS_PROJINHERIT_FL |	\
		FS_ENCRYPT_FL |		\
		FS_INLINE_DATA_FL |	\
		FS_NOCOW_FL |		\
		FS_VERITY_FL |		\
		FS_CASEFOLD_FL)

#define F2FS_SETTABLE_FS_FL (		\
		FS_SYNC_FL |		\
		FS_IMMUTABLE_FL |	\
		FS_APPEND_FL |		\
		FS_NODUMP_FL |		\
		FS_NOATIME_FL |		\
		FS_DIRSYNC_FL |		\
		FS_PROJINHERIT_FL |	\
		FS_CASEFOLD_FL)

/* Convert f2fs on-disk i_flags to FS_IOC_{GET,SET}FLAGS flags */
static inline u32 f2fs_iflags_to_fsflags(u32 iflags)
{
	u32 fsflags = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(f2fs_fsflags_map); i++)
		if (iflags & f2fs_fsflags_map[i].iflag)
			fsflags |= f2fs_fsflags_map[i].fsflag;

	return fsflags;
}

/* Convert FS_IOC_{GET,SET}FLAGS flags to f2fs on-disk i_flags */
static inline u32 f2fs_fsflags_to_iflags(u32 fsflags)
{
	u32 iflags = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(f2fs_fsflags_map); i++)
		if (fsflags & f2fs_fsflags_map[i].fsflag)
			iflags |= f2fs_fsflags_map[i].iflag;

	return iflags;
}

static int f2fs_ioc_getflags(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	u32 fsflags = f2fs_iflags_to_fsflags(fi->i_flags);

	if (IS_ENCRYPTED(iyesde))
		fsflags |= FS_ENCRYPT_FL;
	if (IS_VERITY(iyesde))
		fsflags |= FS_VERITY_FL;
	if (f2fs_has_inline_data(iyesde) || f2fs_has_inline_dentry(iyesde))
		fsflags |= FS_INLINE_DATA_FL;
	if (is_iyesde_flag_set(iyesde, FI_PIN_FILE))
		fsflags |= FS_NOCOW_FL;

	fsflags &= F2FS_GETTABLE_FS_FL;

	return put_user(fsflags, (int __user *)arg);
}

static int f2fs_ioc_setflags(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	u32 fsflags, old_fsflags;
	u32 iflags;
	int ret;

	if (!iyesde_owner_or_capable(iyesde))
		return -EACCES;

	if (get_user(fsflags, (int __user *)arg))
		return -EFAULT;

	if (fsflags & ~F2FS_GETTABLE_FS_FL)
		return -EOPNOTSUPP;
	fsflags &= F2FS_SETTABLE_FS_FL;

	iflags = f2fs_fsflags_to_iflags(fsflags);
	if (f2fs_mask_flags(iyesde->i_mode, iflags) != iflags)
		return -EOPNOTSUPP;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	iyesde_lock(iyesde);

	old_fsflags = f2fs_iflags_to_fsflags(fi->i_flags);
	ret = vfs_ioc_setflags_prepare(iyesde, old_fsflags, fsflags);
	if (ret)
		goto out;

	ret = f2fs_setflags_common(iyesde, iflags,
			f2fs_fsflags_to_iflags(F2FS_SETTABLE_FS_FL));
out:
	iyesde_unlock(iyesde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_getversion(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);

	return put_user(iyesde->i_generation, (int __user *)arg);
}

static int f2fs_ioc_start_atomic_write(struct file *filp)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	int ret;

	if (!iyesde_owner_or_capable(iyesde))
		return -EACCES;

	if (!S_ISREG(iyesde->i_mode))
		return -EINVAL;

	if (filp->f_flags & O_DIRECT)
		return -EINVAL;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	iyesde_lock(iyesde);

	if (f2fs_is_atomic_file(iyesde)) {
		if (is_iyesde_flag_set(iyesde, FI_ATOMIC_REVOKE_REQUEST))
			ret = -EINVAL;
		goto out;
	}

	ret = f2fs_convert_inline_iyesde(iyesde);
	if (ret)
		goto out;

	down_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);

	/*
	 * Should wait end_io to count F2FS_WB_CP_DATA correctly by
	 * f2fs_is_atomic_file.
	 */
	if (get_dirty_pages(iyesde))
		f2fs_warn(F2FS_I_SB(iyesde), "Unexpected flush for atomic writes: iyes=%lu, npages=%u",
			  iyesde->i_iyes, get_dirty_pages(iyesde));
	ret = filemap_write_and_wait_range(iyesde->i_mapping, 0, LLONG_MAX);
	if (ret) {
		up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);
		goto out;
	}

	spin_lock(&sbi->iyesde_lock[ATOMIC_FILE]);
	if (list_empty(&fi->inmem_ilist))
		list_add_tail(&fi->inmem_ilist, &sbi->iyesde_list[ATOMIC_FILE]);
	sbi->atomic_files++;
	spin_unlock(&sbi->iyesde_lock[ATOMIC_FILE]);

	/* add iyesde in inmem_list first and set atomic_file */
	set_iyesde_flag(iyesde, FI_ATOMIC_FILE);
	clear_iyesde_flag(iyesde, FI_ATOMIC_REVOKE_REQUEST);
	up_write(&F2FS_I(iyesde)->i_gc_rwsem[WRITE]);

	f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);
	F2FS_I(iyesde)->inmem_task = current;
	stat_inc_atomic_write(iyesde);
	stat_update_max_atomic_write(iyesde);
out:
	iyesde_unlock(iyesde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_commit_atomic_write(struct file *filp)
{
	struct iyesde *iyesde = file_iyesde(filp);
	int ret;

	if (!iyesde_owner_or_capable(iyesde))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	f2fs_balance_fs(F2FS_I_SB(iyesde), true);

	iyesde_lock(iyesde);

	if (f2fs_is_volatile_file(iyesde)) {
		ret = -EINVAL;
		goto err_out;
	}

	if (f2fs_is_atomic_file(iyesde)) {
		ret = f2fs_commit_inmem_pages(iyesde);
		if (ret)
			goto err_out;

		ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 0, true);
		if (!ret)
			f2fs_drop_inmem_pages(iyesde);
	} else {
		ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 1, false);
	}
err_out:
	if (is_iyesde_flag_set(iyesde, FI_ATOMIC_REVOKE_REQUEST)) {
		clear_iyesde_flag(iyesde, FI_ATOMIC_REVOKE_REQUEST);
		ret = -EINVAL;
	}
	iyesde_unlock(iyesde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_start_volatile_write(struct file *filp)
{
	struct iyesde *iyesde = file_iyesde(filp);
	int ret;

	if (!iyesde_owner_or_capable(iyesde))
		return -EACCES;

	if (!S_ISREG(iyesde->i_mode))
		return -EINVAL;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	iyesde_lock(iyesde);

	if (f2fs_is_volatile_file(iyesde))
		goto out;

	ret = f2fs_convert_inline_iyesde(iyesde);
	if (ret)
		goto out;

	stat_inc_volatile_write(iyesde);
	stat_update_max_volatile_write(iyesde);

	set_iyesde_flag(iyesde, FI_VOLATILE_FILE);
	f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);
out:
	iyesde_unlock(iyesde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_release_volatile_write(struct file *filp)
{
	struct iyesde *iyesde = file_iyesde(filp);
	int ret;

	if (!iyesde_owner_or_capable(iyesde))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	iyesde_lock(iyesde);

	if (!f2fs_is_volatile_file(iyesde))
		goto out;

	if (!f2fs_is_first_block_written(iyesde)) {
		ret = truncate_partial_data_page(iyesde, 0, true);
		goto out;
	}

	ret = punch_hole(iyesde, 0, F2FS_BLKSIZE);
out:
	iyesde_unlock(iyesde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_abort_volatile_write(struct file *filp)
{
	struct iyesde *iyesde = file_iyesde(filp);
	int ret;

	if (!iyesde_owner_or_capable(iyesde))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	iyesde_lock(iyesde);

	if (f2fs_is_atomic_file(iyesde))
		f2fs_drop_inmem_pages(iyesde);
	if (f2fs_is_volatile_file(iyesde)) {
		clear_iyesde_flag(iyesde, FI_VOLATILE_FILE);
		stat_dec_volatile_write(iyesde);
		ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 0, true);
	}

	clear_iyesde_flag(iyesde, FI_ATOMIC_REVOKE_REQUEST);

	iyesde_unlock(iyesde);

	mnt_drop_write_file(filp);
	f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);
	return ret;
}

static int f2fs_ioc_shutdown(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct super_block *sb = sbi->sb;
	__u32 in;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(in, (__u32 __user *)arg))
		return -EFAULT;

	if (in != F2FS_GOING_DOWN_FULLSYNC) {
		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;
	}

	switch (in) {
	case F2FS_GOING_DOWN_FULLSYNC:
		sb = freeze_bdev(sb->s_bdev);
		if (IS_ERR(sb)) {
			ret = PTR_ERR(sb);
			goto out;
		}
		if (sb) {
			f2fs_stop_checkpoint(sbi, false);
			set_sbi_flag(sbi, SBI_IS_SHUTDOWN);
			thaw_bdev(sb->s_bdev, sb);
		}
		break;
	case F2FS_GOING_DOWN_METASYNC:
		/* do checkpoint only */
		ret = f2fs_sync_fs(sb, 1);
		if (ret)
			goto out;
		f2fs_stop_checkpoint(sbi, false);
		set_sbi_flag(sbi, SBI_IS_SHUTDOWN);
		break;
	case F2FS_GOING_DOWN_NOSYNC:
		f2fs_stop_checkpoint(sbi, false);
		set_sbi_flag(sbi, SBI_IS_SHUTDOWN);
		break;
	case F2FS_GOING_DOWN_METAFLUSH:
		f2fs_sync_meta_pages(sbi, META, LONG_MAX, FS_META_IO);
		f2fs_stop_checkpoint(sbi, false);
		set_sbi_flag(sbi, SBI_IS_SHUTDOWN);
		break;
	case F2FS_GOING_DOWN_NEED_FSCK:
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		set_sbi_flag(sbi, SBI_CP_DISABLED_QUICK);
		set_sbi_flag(sbi, SBI_IS_DIRTY);
		/* do checkpoint only */
		ret = f2fs_sync_fs(sb, 1);
		goto out;
	default:
		ret = -EINVAL;
		goto out;
	}

	f2fs_stop_gc_thread(sbi);
	f2fs_stop_discard_thread(sbi);

	f2fs_drop_discard_cmd(sbi);
	clear_opt(sbi, DISCARD);

	f2fs_update_time(sbi, REQ_TIME);
out:
	if (in != F2FS_GOING_DOWN_FULLSYNC)
		mnt_drop_write_file(filp);

	trace_f2fs_shutdown(sbi, in, ret);

	return ret;
}

static int f2fs_ioc_fitrim(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct super_block *sb = iyesde->i_sb;
	struct request_queue *q = bdev_get_queue(sb->s_bdev);
	struct fstrim_range range;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!f2fs_hw_support_discard(F2FS_SB(sb)))
		return -EOPNOTSUPP;

	if (copy_from_user(&range, (struct fstrim_range __user *)arg,
				sizeof(range)))
		return -EFAULT;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	range.minlen = max((unsigned int)range.minlen,
				q->limits.discard_granularity);
	ret = f2fs_trim_fs(F2FS_SB(sb), &range);
	mnt_drop_write_file(filp);
	if (ret < 0)
		return ret;

	if (copy_to_user((struct fstrim_range __user *)arg, &range,
				sizeof(range)))
		return -EFAULT;
	f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);
	return 0;
}

static bool uuid_is_yesnzero(__u8 u[16])
{
	int i;

	for (i = 0; i < 16; i++)
		if (u[i])
			return true;
	return false;
}

static int f2fs_ioc_set_encryption_policy(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);

	if (!f2fs_sb_has_encrypt(F2FS_I_SB(iyesde)))
		return -EOPNOTSUPP;

	f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);

	return fscrypt_ioctl_set_policy(filp, (const void __user *)arg);
}

static int f2fs_ioc_get_encryption_policy(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_iyesde(filp))))
		return -EOPNOTSUPP;
	return fscrypt_ioctl_get_policy(filp, (void __user *)arg);
}

static int f2fs_ioc_get_encryption_pwsalt(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	int err;

	if (!f2fs_sb_has_encrypt(sbi))
		return -EOPNOTSUPP;

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	down_write(&sbi->sb_lock);

	if (uuid_is_yesnzero(sbi->raw_super->encrypt_pw_salt))
		goto got_it;

	/* update superblock with uuid */
	generate_random_uuid(sbi->raw_super->encrypt_pw_salt);

	err = f2fs_commit_super(sbi, false);
	if (err) {
		/* undo new data */
		memset(sbi->raw_super->encrypt_pw_salt, 0, 16);
		goto out_err;
	}
got_it:
	if (copy_to_user((__u8 __user *)arg, sbi->raw_super->encrypt_pw_salt,
									16))
		err = -EFAULT;
out_err:
	up_write(&sbi->sb_lock);
	mnt_drop_write_file(filp);
	return err;
}

static int f2fs_ioc_get_encryption_policy_ex(struct file *filp,
					     unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_iyesde(filp))))
		return -EOPNOTSUPP;

	return fscrypt_ioctl_get_policy_ex(filp, (void __user *)arg);
}

static int f2fs_ioc_add_encryption_key(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_iyesde(filp))))
		return -EOPNOTSUPP;

	return fscrypt_ioctl_add_key(filp, (void __user *)arg);
}

static int f2fs_ioc_remove_encryption_key(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_iyesde(filp))))
		return -EOPNOTSUPP;

	return fscrypt_ioctl_remove_key(filp, (void __user *)arg);
}

static int f2fs_ioc_remove_encryption_key_all_users(struct file *filp,
						    unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_iyesde(filp))))
		return -EOPNOTSUPP;

	return fscrypt_ioctl_remove_key_all_users(filp, (void __user *)arg);
}

static int f2fs_ioc_get_encryption_key_status(struct file *filp,
					      unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_iyesde(filp))))
		return -EOPNOTSUPP;

	return fscrypt_ioctl_get_key_status(filp, (void __user *)arg);
}

static int f2fs_ioc_gc(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	__u32 sync;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(sync, (__u32 __user *)arg))
		return -EFAULT;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	if (!sync) {
		if (!mutex_trylock(&sbi->gc_mutex)) {
			ret = -EBUSY;
			goto out;
		}
	} else {
		mutex_lock(&sbi->gc_mutex);
	}

	ret = f2fs_gc(sbi, sync, true, NULL_SEGNO);
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_gc_range(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct f2fs_gc_range range;
	u64 end;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&range, (struct f2fs_gc_range __user *)arg,
							sizeof(range)))
		return -EFAULT;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	end = range.start + range.len;
	if (end < range.start || range.start < MAIN_BLKADDR(sbi) ||
					end >= MAX_BLKADDR(sbi))
		return -EINVAL;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

do_more:
	if (!range.sync) {
		if (!mutex_trylock(&sbi->gc_mutex)) {
			ret = -EBUSY;
			goto out;
		}
	} else {
		mutex_lock(&sbi->gc_mutex);
	}

	ret = f2fs_gc(sbi, range.sync, true, GET_SEGNO(sbi, range.start));
	range.start += BLKS_PER_SEC(sbi);
	if (range.start <= end)
		goto do_more;
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_write_checkpoint(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED))) {
		f2fs_info(sbi, "Skipping Checkpoint. Checkpoints currently disabled.");
		return -EINVAL;
	}

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ret = f2fs_sync_fs(sbi->sb, 1);

	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_defragment_range(struct f2fs_sb_info *sbi,
					struct file *filp,
					struct f2fs_defragment *range)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_map_blocks map = { .m_next_extent = NULL,
					.m_seg_type = NO_CHECK_TYPE ,
					.m_may_create = false };
	struct extent_info ei = {0, 0, 0};
	pgoff_t pg_start, pg_end, next_pgofs;
	unsigned int blk_per_seg = sbi->blocks_per_seg;
	unsigned int total = 0, sec_num;
	block_t blk_end = 0;
	bool fragmented = false;
	int err;

	/* if in-place-update policy is enabled, don't waste time here */
	if (f2fs_should_update_inplace(iyesde, NULL))
		return -EINVAL;

	pg_start = range->start >> PAGE_SHIFT;
	pg_end = (range->start + range->len) >> PAGE_SHIFT;

	f2fs_balance_fs(sbi, true);

	iyesde_lock(iyesde);

	/* writeback all dirty pages in the range */
	err = filemap_write_and_wait_range(iyesde->i_mapping, range->start,
						range->start + range->len - 1);
	if (err)
		goto out;

	/*
	 * lookup mapping info in extent cache, skip defragmenting if physical
	 * block addresses are continuous.
	 */
	if (f2fs_lookup_extent_cache(iyesde, pg_start, &ei)) {
		if (ei.fofs + ei.len >= pg_end)
			goto out;
	}

	map.m_lblk = pg_start;
	map.m_next_pgofs = &next_pgofs;

	/*
	 * lookup mapping info in dyesde page cache, skip defragmenting if all
	 * physical block addresses are continuous even if there are hole(s)
	 * in logical blocks.
	 */
	while (map.m_lblk < pg_end) {
		map.m_len = pg_end - map.m_lblk;
		err = f2fs_map_blocks(iyesde, &map, 0, F2FS_GET_BLOCK_DEFAULT);
		if (err)
			goto out;

		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			map.m_lblk = next_pgofs;
			continue;
		}

		if (blk_end && blk_end != map.m_pblk)
			fragmented = true;

		/* record total count of block that we're going to move */
		total += map.m_len;

		blk_end = map.m_pblk + map.m_len;

		map.m_lblk += map.m_len;
	}

	if (!fragmented) {
		total = 0;
		goto out;
	}

	sec_num = DIV_ROUND_UP(total, BLKS_PER_SEC(sbi));

	/*
	 * make sure there are eyesugh free section for LFS allocation, this can
	 * avoid defragment running in SSR mode when free section are allocated
	 * intensively
	 */
	if (has_yest_eyesugh_free_secs(sbi, 0, sec_num)) {
		err = -EAGAIN;
		goto out;
	}

	map.m_lblk = pg_start;
	map.m_len = pg_end - pg_start;
	total = 0;

	while (map.m_lblk < pg_end) {
		pgoff_t idx;
		int cnt = 0;

do_map:
		map.m_len = pg_end - map.m_lblk;
		err = f2fs_map_blocks(iyesde, &map, 0, F2FS_GET_BLOCK_DEFAULT);
		if (err)
			goto clear_out;

		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			map.m_lblk = next_pgofs;
			goto check;
		}

		set_iyesde_flag(iyesde, FI_DO_DEFRAG);

		idx = map.m_lblk;
		while (idx < map.m_lblk + map.m_len && cnt < blk_per_seg) {
			struct page *page;

			page = f2fs_get_lock_data_page(iyesde, idx, true);
			if (IS_ERR(page)) {
				err = PTR_ERR(page);
				goto clear_out;
			}

			set_page_dirty(page);
			f2fs_put_page(page, 1);

			idx++;
			cnt++;
			total++;
		}

		map.m_lblk = idx;
check:
		if (map.m_lblk < pg_end && cnt < blk_per_seg)
			goto do_map;

		clear_iyesde_flag(iyesde, FI_DO_DEFRAG);

		err = filemap_fdatawrite(iyesde->i_mapping);
		if (err)
			goto out;
	}
clear_out:
	clear_iyesde_flag(iyesde, FI_DO_DEFRAG);
out:
	iyesde_unlock(iyesde);
	if (!err)
		range->len = (u64)total << PAGE_SHIFT;
	return err;
}

static int f2fs_ioc_defragment(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct f2fs_defragment range;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!S_ISREG(iyesde->i_mode) || f2fs_is_atomic_file(iyesde))
		return -EINVAL;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	if (copy_from_user(&range, (struct f2fs_defragment __user *)arg,
							sizeof(range)))
		return -EFAULT;

	/* verify alignment of offset & size */
	if (range.start & (F2FS_BLKSIZE - 1) || range.len & (F2FS_BLKSIZE - 1))
		return -EINVAL;

	if (unlikely((range.start + range.len) >> PAGE_SHIFT >
					sbi->max_file_blocks))
		return -EINVAL;

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	err = f2fs_defragment_range(sbi, filp, &range);
	mnt_drop_write_file(filp);

	f2fs_update_time(sbi, REQ_TIME);
	if (err < 0)
		return err;

	if (copy_to_user((struct f2fs_defragment __user *)arg, &range,
							sizeof(range)))
		return -EFAULT;

	return 0;
}

static int f2fs_move_file_range(struct file *file_in, loff_t pos_in,
			struct file *file_out, loff_t pos_out, size_t len)
{
	struct iyesde *src = file_iyesde(file_in);
	struct iyesde *dst = file_iyesde(file_out);
	struct f2fs_sb_info *sbi = F2FS_I_SB(src);
	size_t olen = len, dst_max_i_size = 0;
	size_t dst_osize;
	int ret;

	if (file_in->f_path.mnt != file_out->f_path.mnt ||
				src->i_sb != dst->i_sb)
		return -EXDEV;

	if (unlikely(f2fs_readonly(src->i_sb)))
		return -EROFS;

	if (!S_ISREG(src->i_mode) || !S_ISREG(dst->i_mode))
		return -EINVAL;

	if (IS_ENCRYPTED(src) || IS_ENCRYPTED(dst))
		return -EOPNOTSUPP;

	if (src == dst) {
		if (pos_in == pos_out)
			return 0;
		if (pos_out > pos_in && pos_out < pos_in + len)
			return -EINVAL;
	}

	iyesde_lock(src);
	if (src != dst) {
		ret = -EBUSY;
		if (!iyesde_trylock(dst))
			goto out;
	}

	ret = -EINVAL;
	if (pos_in + len > src->i_size || pos_in + len < pos_in)
		goto out_unlock;
	if (len == 0)
		olen = len = src->i_size - pos_in;
	if (pos_in + len == src->i_size)
		len = ALIGN(src->i_size, F2FS_BLKSIZE) - pos_in;
	if (len == 0) {
		ret = 0;
		goto out_unlock;
	}

	dst_osize = dst->i_size;
	if (pos_out + olen > dst->i_size)
		dst_max_i_size = pos_out + olen;

	/* verify the end result is block aligned */
	if (!IS_ALIGNED(pos_in, F2FS_BLKSIZE) ||
			!IS_ALIGNED(pos_in + len, F2FS_BLKSIZE) ||
			!IS_ALIGNED(pos_out, F2FS_BLKSIZE))
		goto out_unlock;

	ret = f2fs_convert_inline_iyesde(src);
	if (ret)
		goto out_unlock;

	ret = f2fs_convert_inline_iyesde(dst);
	if (ret)
		goto out_unlock;

	/* write out all dirty pages from offset */
	ret = filemap_write_and_wait_range(src->i_mapping,
					pos_in, pos_in + len);
	if (ret)
		goto out_unlock;

	ret = filemap_write_and_wait_range(dst->i_mapping,
					pos_out, pos_out + len);
	if (ret)
		goto out_unlock;

	f2fs_balance_fs(sbi, true);

	down_write(&F2FS_I(src)->i_gc_rwsem[WRITE]);
	if (src != dst) {
		ret = -EBUSY;
		if (!down_write_trylock(&F2FS_I(dst)->i_gc_rwsem[WRITE]))
			goto out_src;
	}

	f2fs_lock_op(sbi);
	ret = __exchange_data_block(src, dst, pos_in >> F2FS_BLKSIZE_BITS,
				pos_out >> F2FS_BLKSIZE_BITS,
				len >> F2FS_BLKSIZE_BITS, false);

	if (!ret) {
		if (dst_max_i_size)
			f2fs_i_size_write(dst, dst_max_i_size);
		else if (dst_osize != dst->i_size)
			f2fs_i_size_write(dst, dst_osize);
	}
	f2fs_unlock_op(sbi);

	if (src != dst)
		up_write(&F2FS_I(dst)->i_gc_rwsem[WRITE]);
out_src:
	up_write(&F2FS_I(src)->i_gc_rwsem[WRITE]);
out_unlock:
	if (src != dst)
		iyesde_unlock(dst);
out:
	iyesde_unlock(src);
	return ret;
}

static int f2fs_ioc_move_range(struct file *filp, unsigned long arg)
{
	struct f2fs_move_range range;
	struct fd dst;
	int err;

	if (!(filp->f_mode & FMODE_READ) ||
			!(filp->f_mode & FMODE_WRITE))
		return -EBADF;

	if (copy_from_user(&range, (struct f2fs_move_range __user *)arg,
							sizeof(range)))
		return -EFAULT;

	dst = fdget(range.dst_fd);
	if (!dst.file)
		return -EBADF;

	if (!(dst.file->f_mode & FMODE_WRITE)) {
		err = -EBADF;
		goto err_out;
	}

	err = mnt_want_write_file(filp);
	if (err)
		goto err_out;

	err = f2fs_move_file_range(filp, range.pos_in, dst.file,
					range.pos_out, range.len);

	mnt_drop_write_file(filp);
	if (err)
		goto err_out;

	if (copy_to_user((struct f2fs_move_range __user *)arg,
						&range, sizeof(range)))
		err = -EFAULT;
err_out:
	fdput(dst);
	return err;
}

static int f2fs_ioc_flush_device(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct sit_info *sm = SIT_I(sbi);
	unsigned int start_segyes = 0, end_segyes = 0;
	unsigned int dev_start_segyes = 0, dev_end_segyes = 0;
	struct f2fs_flush_device range;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	if (unlikely(is_sbi_flag_set(sbi, SBI_CP_DISABLED)))
		return -EINVAL;

	if (copy_from_user(&range, (struct f2fs_flush_device __user *)arg,
							sizeof(range)))
		return -EFAULT;

	if (!f2fs_is_multi_device(sbi) || sbi->s_ndevs - 1 <= range.dev_num ||
			__is_large_section(sbi)) {
		f2fs_warn(sbi, "Can't flush %u in %d for segs_per_sec %u != 1",
			  range.dev_num, sbi->s_ndevs, sbi->segs_per_sec);
		return -EINVAL;
	}

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	if (range.dev_num != 0)
		dev_start_segyes = GET_SEGNO(sbi, FDEV(range.dev_num).start_blk);
	dev_end_segyes = GET_SEGNO(sbi, FDEV(range.dev_num).end_blk);

	start_segyes = sm->last_victim[FLUSH_DEVICE];
	if (start_segyes < dev_start_segyes || start_segyes >= dev_end_segyes)
		start_segyes = dev_start_segyes;
	end_segyes = min(start_segyes + range.segments, dev_end_segyes);

	while (start_segyes < end_segyes) {
		if (!mutex_trylock(&sbi->gc_mutex)) {
			ret = -EBUSY;
			goto out;
		}
		sm->last_victim[GC_CB] = end_segyes + 1;
		sm->last_victim[GC_GREEDY] = end_segyes + 1;
		sm->last_victim[ALLOC_NEXT] = end_segyes + 1;
		ret = f2fs_gc(sbi, true, true, start_segyes);
		if (ret == -EAGAIN)
			ret = 0;
		else if (ret < 0)
			break;
		start_segyes++;
	}
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_get_features(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	u32 sb_feature = le32_to_cpu(F2FS_I_SB(iyesde)->raw_super->feature);

	/* Must validate to set it with SQLite behavior in Android. */
	sb_feature |= F2FS_FEATURE_ATOMIC_WRITE;

	return put_user(sb_feature, (u32 __user *)arg);
}

#ifdef CONFIG_QUOTA
int f2fs_transfer_project_quota(struct iyesde *iyesde, kprojid_t kprojid)
{
	struct dquot *transfer_to[MAXQUOTAS] = {};
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct super_block *sb = sbi->sb;
	int err = 0;

	transfer_to[PRJQUOTA] = dqget(sb, make_kqid_projid(kprojid));
	if (!IS_ERR(transfer_to[PRJQUOTA])) {
		err = __dquot_transfer(iyesde, transfer_to);
		if (err)
			set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
		dqput(transfer_to[PRJQUOTA]);
	}
	return err;
}

static int f2fs_ioc_setproject(struct file *filp, __u32 projid)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	struct page *ipage;
	kprojid_t kprojid;
	int err;

	if (!f2fs_sb_has_project_quota(sbi)) {
		if (projid != F2FS_DEF_PROJID)
			return -EOPNOTSUPP;
		else
			return 0;
	}

	if (!f2fs_has_extra_attr(iyesde))
		return -EOPNOTSUPP;

	kprojid = make_kprojid(&init_user_ns, (projid_t)projid);

	if (projid_eq(kprojid, F2FS_I(iyesde)->i_projid))
		return 0;

	err = -EPERM;
	/* Is it quota file? Do yest allow user to mess with it */
	if (IS_NOQUOTA(iyesde))
		return err;

	ipage = f2fs_get_yesde_page(sbi, iyesde->i_iyes);
	if (IS_ERR(ipage))
		return PTR_ERR(ipage);

	if (!F2FS_FITS_IN_INODE(F2FS_INODE(ipage), fi->i_extra_isize,
								i_projid)) {
		err = -EOVERFLOW;
		f2fs_put_page(ipage, 1);
		return err;
	}
	f2fs_put_page(ipage, 1);

	err = dquot_initialize(iyesde);
	if (err)
		return err;

	f2fs_lock_op(sbi);
	err = f2fs_transfer_project_quota(iyesde, kprojid);
	if (err)
		goto out_unlock;

	F2FS_I(iyesde)->i_projid = kprojid;
	iyesde->i_ctime = current_time(iyesde);
	f2fs_mark_iyesde_dirty_sync(iyesde, true);
out_unlock:
	f2fs_unlock_op(sbi);
	return err;
}
#else
int f2fs_transfer_project_quota(struct iyesde *iyesde, kprojid_t kprojid)
{
	return 0;
}

static int f2fs_ioc_setproject(struct file *filp, __u32 projid)
{
	if (projid != F2FS_DEF_PROJID)
		return -EOPNOTSUPP;
	return 0;
}
#endif

/* FS_IOC_FSGETXATTR and FS_IOC_FSSETXATTR support */

/*
 * To make a new on-disk f2fs i_flag gettable via FS_IOC_FSGETXATTR and settable
 * via FS_IOC_FSSETXATTR, add an entry for it to f2fs_xflags_map[], and add its
 * FS_XFLAG_* equivalent to F2FS_SUPPORTED_XFLAGS.
 */

static const struct {
	u32 iflag;
	u32 xflag;
} f2fs_xflags_map[] = {
	{ F2FS_SYNC_FL,		FS_XFLAG_SYNC },
	{ F2FS_IMMUTABLE_FL,	FS_XFLAG_IMMUTABLE },
	{ F2FS_APPEND_FL,	FS_XFLAG_APPEND },
	{ F2FS_NODUMP_FL,	FS_XFLAG_NODUMP },
	{ F2FS_NOATIME_FL,	FS_XFLAG_NOATIME },
	{ F2FS_PROJINHERIT_FL,	FS_XFLAG_PROJINHERIT },
};

#define F2FS_SUPPORTED_XFLAGS (		\
		FS_XFLAG_SYNC |		\
		FS_XFLAG_IMMUTABLE |	\
		FS_XFLAG_APPEND |	\
		FS_XFLAG_NODUMP |	\
		FS_XFLAG_NOATIME |	\
		FS_XFLAG_PROJINHERIT)

/* Convert f2fs on-disk i_flags to FS_IOC_FS{GET,SET}XATTR flags */
static inline u32 f2fs_iflags_to_xflags(u32 iflags)
{
	u32 xflags = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(f2fs_xflags_map); i++)
		if (iflags & f2fs_xflags_map[i].iflag)
			xflags |= f2fs_xflags_map[i].xflag;

	return xflags;
}

/* Convert FS_IOC_FS{GET,SET}XATTR flags to f2fs on-disk i_flags */
static inline u32 f2fs_xflags_to_iflags(u32 xflags)
{
	u32 iflags = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(f2fs_xflags_map); i++)
		if (xflags & f2fs_xflags_map[i].xflag)
			iflags |= f2fs_xflags_map[i].iflag;

	return iflags;
}

static void f2fs_fill_fsxattr(struct iyesde *iyesde, struct fsxattr *fa)
{
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);

	simple_fill_fsxattr(fa, f2fs_iflags_to_xflags(fi->i_flags));

	if (f2fs_sb_has_project_quota(F2FS_I_SB(iyesde)))
		fa->fsx_projid = from_kprojid(&init_user_ns, fi->i_projid);
}

static int f2fs_ioc_fsgetxattr(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct fsxattr fa;

	f2fs_fill_fsxattr(iyesde, &fa);

	if (copy_to_user((struct fsxattr __user *)arg, &fa, sizeof(fa)))
		return -EFAULT;
	return 0;
}

static int f2fs_ioc_fssetxattr(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct fsxattr fa, old_fa;
	u32 iflags;
	int err;

	if (copy_from_user(&fa, (struct fsxattr __user *)arg, sizeof(fa)))
		return -EFAULT;

	/* Make sure caller has proper permission */
	if (!iyesde_owner_or_capable(iyesde))
		return -EACCES;

	if (fa.fsx_xflags & ~F2FS_SUPPORTED_XFLAGS)
		return -EOPNOTSUPP;

	iflags = f2fs_xflags_to_iflags(fa.fsx_xflags);
	if (f2fs_mask_flags(iyesde->i_mode, iflags) != iflags)
		return -EOPNOTSUPP;

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	iyesde_lock(iyesde);

	f2fs_fill_fsxattr(iyesde, &old_fa);
	err = vfs_ioc_fssetxattr_check(iyesde, &old_fa, &fa);
	if (err)
		goto out;

	err = f2fs_setflags_common(iyesde, iflags,
			f2fs_xflags_to_iflags(F2FS_SUPPORTED_XFLAGS));
	if (err)
		goto out;

	err = f2fs_ioc_setproject(filp, fa.fsx_projid);
out:
	iyesde_unlock(iyesde);
	mnt_drop_write_file(filp);
	return err;
}

int f2fs_pin_file_control(struct iyesde *iyesde, bool inc)
{
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);

	/* Use i_gc_failures for yesrmal file as a risk signal. */
	if (inc)
		f2fs_i_gc_failures_write(iyesde,
				fi->i_gc_failures[GC_FAILURE_PIN] + 1);

	if (fi->i_gc_failures[GC_FAILURE_PIN] > sbi->gc_pin_file_threshold) {
		f2fs_warn(sbi, "%s: Enable GC = iyes %lx after %x GC trials",
			  __func__, iyesde->i_iyes,
			  fi->i_gc_failures[GC_FAILURE_PIN]);
		clear_iyesde_flag(iyesde, FI_PIN_FILE);
		return -EAGAIN;
	}
	return 0;
}

static int f2fs_ioc_set_pin_file(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	__u32 pin;
	int ret = 0;

	if (get_user(pin, (__u32 __user *)arg))
		return -EFAULT;

	if (!S_ISREG(iyesde->i_mode))
		return -EINVAL;

	if (f2fs_readonly(F2FS_I_SB(iyesde)->sb))
		return -EROFS;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	iyesde_lock(iyesde);

	if (f2fs_should_update_outplace(iyesde, NULL)) {
		ret = -EINVAL;
		goto out;
	}

	if (!pin) {
		clear_iyesde_flag(iyesde, FI_PIN_FILE);
		f2fs_i_gc_failures_write(iyesde, 0);
		goto done;
	}

	if (f2fs_pin_file_control(iyesde, false)) {
		ret = -EAGAIN;
		goto out;
	}
	ret = f2fs_convert_inline_iyesde(iyesde);
	if (ret)
		goto out;

	set_iyesde_flag(iyesde, FI_PIN_FILE);
	ret = F2FS_I(iyesde)->i_gc_failures[GC_FAILURE_PIN];
done:
	f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);
out:
	iyesde_unlock(iyesde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_get_pin_file(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	__u32 pin = 0;

	if (is_iyesde_flag_set(iyesde, FI_PIN_FILE))
		pin = F2FS_I(iyesde)->i_gc_failures[GC_FAILURE_PIN];
	return put_user(pin, (u32 __user *)arg);
}

int f2fs_precache_extents(struct iyesde *iyesde)
{
	struct f2fs_iyesde_info *fi = F2FS_I(iyesde);
	struct f2fs_map_blocks map;
	pgoff_t m_next_extent;
	loff_t end;
	int err;

	if (is_iyesde_flag_set(iyesde, FI_NO_EXTENT))
		return -EOPNOTSUPP;

	map.m_lblk = 0;
	map.m_next_pgofs = NULL;
	map.m_next_extent = &m_next_extent;
	map.m_seg_type = NO_CHECK_TYPE;
	map.m_may_create = false;
	end = F2FS_I_SB(iyesde)->max_file_blocks;

	while (map.m_lblk < end) {
		map.m_len = end - map.m_lblk;

		down_write(&fi->i_gc_rwsem[WRITE]);
		err = f2fs_map_blocks(iyesde, &map, 0, F2FS_GET_BLOCK_PRECACHE);
		up_write(&fi->i_gc_rwsem[WRITE]);
		if (err)
			return err;

		map.m_lblk = m_next_extent;
	}

	return err;
}

static int f2fs_ioc_precache_extents(struct file *filp, unsigned long arg)
{
	return f2fs_precache_extents(file_iyesde(filp));
}

static int f2fs_ioc_resize_fs(struct file *filp, unsigned long arg)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(file_iyesde(filp));
	__u64 block_count;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	if (copy_from_user(&block_count, (void __user *)arg,
			   sizeof(block_count)))
		return -EFAULT;

	ret = f2fs_resize_fs(sbi, block_count);

	return ret;
}

static int f2fs_ioc_enable_verity(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);

	f2fs_update_time(F2FS_I_SB(iyesde), REQ_TIME);

	if (!f2fs_sb_has_verity(F2FS_I_SB(iyesde))) {
		f2fs_warn(F2FS_I_SB(iyesde),
			  "Can't enable fs-verity on iyesde %lu: the verity feature is yest enabled on this filesystem.\n",
			  iyesde->i_iyes);
		return -EOPNOTSUPP;
	}

	return fsverity_ioctl_enable(filp, (const void __user *)arg);
}

static int f2fs_ioc_measure_verity(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_verity(F2FS_I_SB(file_iyesde(filp))))
		return -EOPNOTSUPP;

	return fsverity_ioctl_measure(filp, (void __user *)arg);
}

static int f2fs_get_volume_name(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	char *vbuf;
	int count;
	int err = 0;

	vbuf = f2fs_kzalloc(sbi, MAX_VOLUME_NAME, GFP_KERNEL);
	if (!vbuf)
		return -ENOMEM;

	down_read(&sbi->sb_lock);
	count = utf16s_to_utf8s(sbi->raw_super->volume_name,
			ARRAY_SIZE(sbi->raw_super->volume_name),
			UTF16_LITTLE_ENDIAN, vbuf, MAX_VOLUME_NAME);
	up_read(&sbi->sb_lock);

	if (copy_to_user((char __user *)arg, vbuf,
				min(FSLABEL_MAX, count)))
		err = -EFAULT;

	kvfree(vbuf);
	return err;
}

static int f2fs_set_volume_name(struct file *filp, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(iyesde);
	char *vbuf;
	int err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	vbuf = strndup_user((const char __user *)arg, FSLABEL_MAX);
	if (IS_ERR(vbuf))
		return PTR_ERR(vbuf);

	err = mnt_want_write_file(filp);
	if (err)
		goto out;

	down_write(&sbi->sb_lock);

	memset(sbi->raw_super->volume_name, 0,
			sizeof(sbi->raw_super->volume_name));
	utf8s_to_utf16s(vbuf, strlen(vbuf), UTF16_LITTLE_ENDIAN,
			sbi->raw_super->volume_name,
			ARRAY_SIZE(sbi->raw_super->volume_name));

	err = f2fs_commit_super(sbi, false);

	up_write(&sbi->sb_lock);

	mnt_drop_write_file(filp);
out:
	kfree(vbuf);
	return err;
}

long f2fs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if (unlikely(f2fs_cp_error(F2FS_I_SB(file_iyesde(filp)))))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(F2FS_I_SB(file_iyesde(filp))))
		return -ENOSPC;

	switch (cmd) {
	case F2FS_IOC_GETFLAGS:
		return f2fs_ioc_getflags(filp, arg);
	case F2FS_IOC_SETFLAGS:
		return f2fs_ioc_setflags(filp, arg);
	case F2FS_IOC_GETVERSION:
		return f2fs_ioc_getversion(filp, arg);
	case F2FS_IOC_START_ATOMIC_WRITE:
		return f2fs_ioc_start_atomic_write(filp);
	case F2FS_IOC_COMMIT_ATOMIC_WRITE:
		return f2fs_ioc_commit_atomic_write(filp);
	case F2FS_IOC_START_VOLATILE_WRITE:
		return f2fs_ioc_start_volatile_write(filp);
	case F2FS_IOC_RELEASE_VOLATILE_WRITE:
		return f2fs_ioc_release_volatile_write(filp);
	case F2FS_IOC_ABORT_VOLATILE_WRITE:
		return f2fs_ioc_abort_volatile_write(filp);
	case F2FS_IOC_SHUTDOWN:
		return f2fs_ioc_shutdown(filp, arg);
	case FITRIM:
		return f2fs_ioc_fitrim(filp, arg);
	case F2FS_IOC_SET_ENCRYPTION_POLICY:
		return f2fs_ioc_set_encryption_policy(filp, arg);
	case F2FS_IOC_GET_ENCRYPTION_POLICY:
		return f2fs_ioc_get_encryption_policy(filp, arg);
	case F2FS_IOC_GET_ENCRYPTION_PWSALT:
		return f2fs_ioc_get_encryption_pwsalt(filp, arg);
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
		return f2fs_ioc_get_encryption_policy_ex(filp, arg);
	case FS_IOC_ADD_ENCRYPTION_KEY:
		return f2fs_ioc_add_encryption_key(filp, arg);
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
		return f2fs_ioc_remove_encryption_key(filp, arg);
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
		return f2fs_ioc_remove_encryption_key_all_users(filp, arg);
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
		return f2fs_ioc_get_encryption_key_status(filp, arg);
	case F2FS_IOC_GARBAGE_COLLECT:
		return f2fs_ioc_gc(filp, arg);
	case F2FS_IOC_GARBAGE_COLLECT_RANGE:
		return f2fs_ioc_gc_range(filp, arg);
	case F2FS_IOC_WRITE_CHECKPOINT:
		return f2fs_ioc_write_checkpoint(filp, arg);
	case F2FS_IOC_DEFRAGMENT:
		return f2fs_ioc_defragment(filp, arg);
	case F2FS_IOC_MOVE_RANGE:
		return f2fs_ioc_move_range(filp, arg);
	case F2FS_IOC_FLUSH_DEVICE:
		return f2fs_ioc_flush_device(filp, arg);
	case F2FS_IOC_GET_FEATURES:
		return f2fs_ioc_get_features(filp, arg);
	case F2FS_IOC_FSGETXATTR:
		return f2fs_ioc_fsgetxattr(filp, arg);
	case F2FS_IOC_FSSETXATTR:
		return f2fs_ioc_fssetxattr(filp, arg);
	case F2FS_IOC_GET_PIN_FILE:
		return f2fs_ioc_get_pin_file(filp, arg);
	case F2FS_IOC_SET_PIN_FILE:
		return f2fs_ioc_set_pin_file(filp, arg);
	case F2FS_IOC_PRECACHE_EXTENTS:
		return f2fs_ioc_precache_extents(filp, arg);
	case F2FS_IOC_RESIZE_FS:
		return f2fs_ioc_resize_fs(filp, arg);
	case FS_IOC_ENABLE_VERITY:
		return f2fs_ioc_enable_verity(filp, arg);
	case FS_IOC_MEASURE_VERITY:
		return f2fs_ioc_measure_verity(filp, arg);
	case F2FS_IOC_GET_VOLUME_NAME:
		return f2fs_get_volume_name(filp, arg);
	case F2FS_IOC_SET_VOLUME_NAME:
		return f2fs_set_volume_name(filp, arg);
	default:
		return -ENOTTY;
	}
}

static ssize_t f2fs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct iyesde *iyesde = file_iyesde(file);
	ssize_t ret;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(iyesde)))) {
		ret = -EIO;
		goto out;
	}

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!iyesde_trylock(iyesde)) {
			ret = -EAGAIN;
			goto out;
		}
	} else {
		iyesde_lock(iyesde);
	}

	ret = generic_write_checks(iocb, from);
	if (ret > 0) {
		bool preallocated = false;
		size_t target_size = 0;
		int err;

		if (iov_iter_fault_in_readable(from, iov_iter_count(from)))
			set_iyesde_flag(iyesde, FI_NO_PREALLOC);

		if ((iocb->ki_flags & IOCB_NOWAIT)) {
			if (!f2fs_overwrite_io(iyesde, iocb->ki_pos,
						iov_iter_count(from)) ||
				f2fs_has_inline_data(iyesde) ||
				f2fs_force_buffered_io(iyesde, iocb, from)) {
				clear_iyesde_flag(iyesde, FI_NO_PREALLOC);
				iyesde_unlock(iyesde);
				ret = -EAGAIN;
				goto out;
			}
		} else {
			preallocated = true;
			target_size = iocb->ki_pos + iov_iter_count(from);

			err = f2fs_preallocate_blocks(iocb, from);
			if (err) {
				clear_iyesde_flag(iyesde, FI_NO_PREALLOC);
				iyesde_unlock(iyesde);
				ret = err;
				goto out;
			}
		}
		ret = __generic_file_write_iter(iocb, from);
		clear_iyesde_flag(iyesde, FI_NO_PREALLOC);

		/* if we couldn't write data, we should deallocate blocks. */
		if (preallocated && i_size_read(iyesde) < target_size)
			f2fs_truncate(iyesde);

		if (ret > 0)
			f2fs_update_iostat(F2FS_I_SB(iyesde), APP_WRITE_IO, ret);
	}
	iyesde_unlock(iyesde);
out:
	trace_f2fs_file_write_iter(iyesde, iocb->ki_pos,
					iov_iter_count(from), ret);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}

#ifdef CONFIG_COMPAT
long f2fs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case F2FS_IOC32_GETFLAGS:
		cmd = F2FS_IOC_GETFLAGS;
		break;
	case F2FS_IOC32_SETFLAGS:
		cmd = F2FS_IOC_SETFLAGS;
		break;
	case F2FS_IOC32_GETVERSION:
		cmd = F2FS_IOC_GETVERSION;
		break;
	case F2FS_IOC_START_ATOMIC_WRITE:
	case F2FS_IOC_COMMIT_ATOMIC_WRITE:
	case F2FS_IOC_START_VOLATILE_WRITE:
	case F2FS_IOC_RELEASE_VOLATILE_WRITE:
	case F2FS_IOC_ABORT_VOLATILE_WRITE:
	case F2FS_IOC_SHUTDOWN:
	case FITRIM:
	case F2FS_IOC_SET_ENCRYPTION_POLICY:
	case F2FS_IOC_GET_ENCRYPTION_PWSALT:
	case F2FS_IOC_GET_ENCRYPTION_POLICY:
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
	case FS_IOC_ADD_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
	case F2FS_IOC_GARBAGE_COLLECT:
	case F2FS_IOC_GARBAGE_COLLECT_RANGE:
	case F2FS_IOC_WRITE_CHECKPOINT:
	case F2FS_IOC_DEFRAGMENT:
	case F2FS_IOC_MOVE_RANGE:
	case F2FS_IOC_FLUSH_DEVICE:
	case F2FS_IOC_GET_FEATURES:
	case F2FS_IOC_FSGETXATTR:
	case F2FS_IOC_FSSETXATTR:
	case F2FS_IOC_GET_PIN_FILE:
	case F2FS_IOC_SET_PIN_FILE:
	case F2FS_IOC_PRECACHE_EXTENTS:
	case F2FS_IOC_RESIZE_FS:
	case FS_IOC_ENABLE_VERITY:
	case FS_IOC_MEASURE_VERITY:
	case F2FS_IOC_GET_VOLUME_NAME:
	case F2FS_IOC_SET_VOLUME_NAME:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return f2fs_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

const struct file_operations f2fs_file_operations = {
	.llseek		= f2fs_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= f2fs_file_write_iter,
	.open		= f2fs_file_open,
	.release	= f2fs_release_file,
	.mmap		= f2fs_file_mmap,
	.flush		= f2fs_file_flush,
	.fsync		= f2fs_sync_file,
	.fallocate	= f2fs_fallocate,
	.unlocked_ioctl	= f2fs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= f2fs_compat_ioctl,
#endif
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
};
