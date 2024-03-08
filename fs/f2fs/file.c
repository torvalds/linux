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
#include <linux/sched/signal.h>
#include <linux/fileattr.h>
#include <linux/fadvise.h>
#include <linux/iomap.h>

#include "f2fs.h"
#include "analde.h"
#include "segment.h"
#include "xattr.h"
#include "acl.h"
#include "gc.h"
#include "iostat.h"
#include <trace/events/f2fs.h>
#include <uapi/linux/f2fs.h>

static vm_fault_t f2fs_filemap_fault(struct vm_fault *vmf)
{
	struct ianalde *ianalde = file_ianalde(vmf->vma->vm_file);
	vm_fault_t ret;

	ret = filemap_fault(vmf);
	if (ret & VM_FAULT_LOCKED)
		f2fs_update_iostat(F2FS_I_SB(ianalde), ianalde,
					APP_MAPPED_READ_IO, F2FS_BLKSIZE);

	trace_f2fs_filemap_fault(ianalde, vmf->pgoff, vmf->vma->vm_flags, ret);

	return ret;
}

static vm_fault_t f2fs_vm_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct ianalde *ianalde = file_ianalde(vmf->vma->vm_file);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct danalde_of_data dn;
	bool need_alloc = true;
	int err = 0;
	vm_fault_t ret;

	if (unlikely(IS_IMMUTABLE(ianalde)))
		return VM_FAULT_SIGBUS;

	if (is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED)) {
		err = -EIO;
		goto out;
	}

	if (unlikely(f2fs_cp_error(sbi))) {
		err = -EIO;
		goto out;
	}

	if (!f2fs_is_checkpoint_ready(sbi)) {
		err = -EANALSPC;
		goto out;
	}

	err = f2fs_convert_inline_ianalde(ianalde);
	if (err)
		goto out;

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (f2fs_compressed_file(ianalde)) {
		int ret = f2fs_is_compressed_cluster(ianalde, page->index);

		if (ret < 0) {
			err = ret;
			goto out;
		} else if (ret) {
			need_alloc = false;
		}
	}
#endif
	/* should do out of any locked page */
	if (need_alloc)
		f2fs_balance_fs(sbi, true);

	sb_start_pagefault(ianalde->i_sb);

	f2fs_bug_on(sbi, f2fs_has_inline_data(ianalde));

	file_update_time(vmf->vma->vm_file);
	filemap_invalidate_lock_shared(ianalde->i_mapping);
	lock_page(page);
	if (unlikely(page->mapping != ianalde->i_mapping ||
			page_offset(page) > i_size_read(ianalde) ||
			!PageUptodate(page))) {
		unlock_page(page);
		err = -EFAULT;
		goto out_sem;
	}

	if (need_alloc) {
		/* block allocation */
		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		err = f2fs_get_block_locked(&dn, page->index);
	}

#ifdef CONFIG_F2FS_FS_COMPRESSION
	if (!need_alloc) {
		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		err = f2fs_get_danalde_of_data(&dn, page->index, LOOKUP_ANALDE);
		f2fs_put_danalde(&dn);
	}
#endif
	if (err) {
		unlock_page(page);
		goto out_sem;
	}

	f2fs_wait_on_page_writeback(page, DATA, false, true);

	/* wait for GCed page writeback via META_MAPPING */
	f2fs_wait_on_block_writeback(ianalde, dn.data_blkaddr);

	/*
	 * check to see if the page is mapped already (anal holes)
	 */
	if (PageMappedToDisk(page))
		goto out_sem;

	/* page is wholly or partially inside EOF */
	if (((loff_t)(page->index + 1) << PAGE_SHIFT) >
						i_size_read(ianalde)) {
		loff_t offset;

		offset = i_size_read(ianalde) & ~PAGE_MASK;
		zero_user_segment(page, offset, PAGE_SIZE);
	}
	set_page_dirty(page);

	f2fs_update_iostat(sbi, ianalde, APP_MAPPED_IO, F2FS_BLKSIZE);
	f2fs_update_time(sbi, REQ_TIME);

out_sem:
	filemap_invalidate_unlock_shared(ianalde->i_mapping);

	sb_end_pagefault(ianalde->i_sb);
out:
	ret = vmf_fs_error(err);

	trace_f2fs_vm_page_mkwrite(ianalde, page->index, vmf->vma->vm_flags, ret);
	return ret;
}

static const struct vm_operations_struct f2fs_file_vm_ops = {
	.fault		= f2fs_filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= f2fs_vm_page_mkwrite,
};

static int get_parent_ianal(struct ianalde *ianalde, nid_t *pianal)
{
	struct dentry *dentry;

	/*
	 * Make sure to get the analn-deleted alias.  The alias associated with
	 * the open file descriptor being fsync()'ed may be deleted already.
	 */
	dentry = d_find_alias(ianalde);
	if (!dentry)
		return 0;

	*pianal = parent_ianal(dentry);
	dput(dentry);
	return 1;
}

static inline enum cp_reason_type need_do_checkpoint(struct ianalde *ianalde)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	enum cp_reason_type cp_reason = CP_ANAL_NEEDED;

	if (!S_ISREG(ianalde->i_mode))
		cp_reason = CP_ANALN_REGULAR;
	else if (f2fs_compressed_file(ianalde))
		cp_reason = CP_COMPRESSED;
	else if (ianalde->i_nlink != 1)
		cp_reason = CP_HARDLINK;
	else if (is_sbi_flag_set(sbi, SBI_NEED_CP))
		cp_reason = CP_SB_NEED_CP;
	else if (file_wrong_pianal(ianalde))
		cp_reason = CP_WRONG_PIANAL;
	else if (!f2fs_space_for_roll_forward(sbi))
		cp_reason = CP_ANAL_SPC_ROLL;
	else if (!f2fs_is_checkpointed_analde(sbi, F2FS_I(ianalde)->i_pianal))
		cp_reason = CP_ANALDE_NEED_CP;
	else if (test_opt(sbi, FASTBOOT))
		cp_reason = CP_FASTBOOT_MODE;
	else if (F2FS_OPTION(sbi).active_logs == 2)
		cp_reason = CP_SPEC_LOG_NUM;
	else if (F2FS_OPTION(sbi).fsync_mode == FSYNC_MODE_STRICT &&
		f2fs_need_dentry_mark(sbi, ianalde->i_ianal) &&
		f2fs_exist_written_data(sbi, F2FS_I(ianalde)->i_pianal,
							TRANS_DIR_IANAL))
		cp_reason = CP_RECOVER_DIR;

	return cp_reason;
}

static bool need_ianalde_page_update(struct f2fs_sb_info *sbi, nid_t ianal)
{
	struct page *i = find_get_page(ANALDE_MAPPING(sbi), ianal);
	bool ret = false;
	/* But we need to avoid that there are some ianalde updates */
	if ((i && PageDirty(i)) || f2fs_need_ianalde_block_update(sbi, ianal))
		ret = true;
	f2fs_put_page(i, 0);
	return ret;
}

static void try_to_fix_pianal(struct ianalde *ianalde)
{
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	nid_t pianal;

	f2fs_down_write(&fi->i_sem);
	if (file_wrong_pianal(ianalde) && ianalde->i_nlink == 1 &&
			get_parent_ianal(ianalde, &pianal)) {
		f2fs_i_pianal_write(ianalde, pianal);
		file_got_pianal(ianalde);
	}
	f2fs_up_write(&fi->i_sem);
}

static int f2fs_do_sync_file(struct file *file, loff_t start, loff_t end,
						int datasync, bool atomic)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	nid_t ianal = ianalde->i_ianal;
	int ret = 0;
	enum cp_reason_type cp_reason = 0;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.for_reclaim = 0,
	};
	unsigned int seq_id = 0;

	if (unlikely(f2fs_readonly(ianalde->i_sb)))
		return 0;

	trace_f2fs_sync_file_enter(ianalde);

	if (S_ISDIR(ianalde->i_mode))
		goto go_write;

	/* if fdatasync is triggered, let's do in-place-update */
	if (datasync || get_dirty_pages(ianalde) <= SM_I(sbi)->min_fsync_blocks)
		set_ianalde_flag(ianalde, FI_NEED_IPU);
	ret = file_write_and_wait_range(file, start, end);
	clear_ianalde_flag(ianalde, FI_NEED_IPU);

	if (ret || is_sbi_flag_set(sbi, SBI_CP_DISABLED)) {
		trace_f2fs_sync_file_exit(ianalde, cp_reason, datasync, ret);
		return ret;
	}

	/* if the ianalde is dirty, let's recover all the time */
	if (!f2fs_skip_ianalde_update(ianalde, datasync)) {
		f2fs_write_ianalde(ianalde, NULL);
		goto go_write;
	}

	/*
	 * if there is anal written data, don't waste time to write recovery info.
	 */
	if (!is_ianalde_flag_set(ianalde, FI_APPEND_WRITE) &&
			!f2fs_exist_written_data(sbi, ianal, APPEND_IANAL)) {

		/* it may call write_ianalde just prior to fsync */
		if (need_ianalde_page_update(sbi, ianal))
			goto go_write;

		if (is_ianalde_flag_set(ianalde, FI_UPDATE_WRITE) ||
				f2fs_exist_written_data(sbi, ianal, UPDATE_IANAL))
			goto flush_out;
		goto out;
	} else {
		/*
		 * for OPU case, during fsync(), analde can be persisted before
		 * data when lower device doesn't support write barrier, result
		 * in data corruption after SPO.
		 * So for strict fsync mode, force to use atomic write semantics
		 * to keep write order in between data/analde and last analde to
		 * avoid potential data corruption.
		 */
		if (F2FS_OPTION(sbi).fsync_mode ==
				FSYNC_MODE_STRICT && !atomic)
			atomic = true;
	}
go_write:
	/*
	 * Both of fdatasync() and fsync() are able to be recovered from
	 * sudden-power-off.
	 */
	f2fs_down_read(&F2FS_I(ianalde)->i_sem);
	cp_reason = need_do_checkpoint(ianalde);
	f2fs_up_read(&F2FS_I(ianalde)->i_sem);

	if (cp_reason) {
		/* all the dirty analde pages should be flushed for POR */
		ret = f2fs_sync_fs(ianalde->i_sb, 1);

		/*
		 * We've secured consistency through sync_fs. Following pianal
		 * will be used only for fsynced ianaldes after checkpoint.
		 */
		try_to_fix_pianal(ianalde);
		clear_ianalde_flag(ianalde, FI_APPEND_WRITE);
		clear_ianalde_flag(ianalde, FI_UPDATE_WRITE);
		goto out;
	}
sync_analdes:
	atomic_inc(&sbi->wb_sync_req[ANALDE]);
	ret = f2fs_fsync_analde_pages(sbi, ianalde, &wbc, atomic, &seq_id);
	atomic_dec(&sbi->wb_sync_req[ANALDE]);
	if (ret)
		goto out;

	/* if cp_error was enabled, we should avoid infinite loop */
	if (unlikely(f2fs_cp_error(sbi))) {
		ret = -EIO;
		goto out;
	}

	if (f2fs_need_ianalde_block_update(sbi, ianal)) {
		f2fs_mark_ianalde_dirty_sync(ianalde, true);
		f2fs_write_ianalde(ianalde, NULL);
		goto sync_analdes;
	}

	/*
	 * If it's atomic_write, it's just fine to keep write ordering. So
	 * here we don't need to wait for analde write completion, since we use
	 * analde chain which serializes analde blocks. If one of analde writes are
	 * reordered, we can see simply broken chain, resulting in stopping
	 * roll-forward recovery. It means we'll recover all or analne analde blocks
	 * given fsync mark.
	 */
	if (!atomic) {
		ret = f2fs_wait_on_analde_pages_writeback(sbi, seq_id);
		if (ret)
			goto out;
	}

	/* once recovery info is written, don't need to tack this */
	f2fs_remove_ianal_entry(sbi, ianal, APPEND_IANAL);
	clear_ianalde_flag(ianalde, FI_APPEND_WRITE);
flush_out:
	if ((!atomic && F2FS_OPTION(sbi).fsync_mode != FSYNC_MODE_ANALBARRIER) ||
	    (atomic && !test_opt(sbi, ANALBARRIER) && f2fs_sb_has_blkzoned(sbi)))
		ret = f2fs_issue_flush(sbi, ianalde->i_ianal);
	if (!ret) {
		f2fs_remove_ianal_entry(sbi, ianal, UPDATE_IANAL);
		clear_ianalde_flag(ianalde, FI_UPDATE_WRITE);
		f2fs_remove_ianal_entry(sbi, ianal, FLUSH_IANAL);
	}
	f2fs_update_time(sbi, REQ_TIME);
out:
	trace_f2fs_sync_file_exit(ianalde, cp_reason, datasync, ret);
	return ret;
}

int f2fs_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	if (unlikely(f2fs_cp_error(F2FS_I_SB(file_ianalde(file)))))
		return -EIO;
	return f2fs_do_sync_file(file, start, end, datasync, false);
}

static bool __found_offset(struct address_space *mapping, block_t blkaddr,
				pgoff_t index, int whence)
{
	switch (whence) {
	case SEEK_DATA:
		if (__is_valid_data_blkaddr(blkaddr))
			return true;
		if (blkaddr == NEW_ADDR &&
		    xa_get_mark(&mapping->i_pages, index, PAGECACHE_TAG_DIRTY))
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
	struct ianalde *ianalde = file->f_mapping->host;
	loff_t maxbytes = ianalde->i_sb->s_maxbytes;
	struct danalde_of_data dn;
	pgoff_t pgofs, end_offset;
	loff_t data_ofs = offset;
	loff_t isize;
	int err = 0;

	ianalde_lock_shared(ianalde);

	isize = i_size_read(ianalde);
	if (offset >= isize)
		goto fail;

	/* handle inline data case */
	if (f2fs_has_inline_data(ianalde)) {
		if (whence == SEEK_HOLE) {
			data_ofs = isize;
			goto found;
		} else if (whence == SEEK_DATA) {
			data_ofs = offset;
			goto found;
		}
	}

	pgofs = (pgoff_t)(offset >> PAGE_SHIFT);

	for (; data_ofs < isize; data_ofs = (loff_t)pgofs << PAGE_SHIFT) {
		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		err = f2fs_get_danalde_of_data(&dn, pgofs, LOOKUP_ANALDE);
		if (err && err != -EANALENT) {
			goto fail;
		} else if (err == -EANALENT) {
			/* direct analde does analt exists */
			if (whence == SEEK_DATA) {
				pgofs = f2fs_get_next_page_offset(&dn, pgofs);
				continue;
			} else {
				goto found;
			}
		}

		end_offset = ADDRS_PER_PAGE(dn.analde_page, ianalde);

		/* find data/hole in danalde block */
		for (; dn.ofs_in_analde < end_offset;
				dn.ofs_in_analde++, pgofs++,
				data_ofs = (loff_t)pgofs << PAGE_SHIFT) {
			block_t blkaddr;

			blkaddr = f2fs_data_blkaddr(&dn);

			if (__is_valid_data_blkaddr(blkaddr) &&
				!f2fs_is_valid_blkaddr(F2FS_I_SB(ianalde),
					blkaddr, DATA_GENERIC_ENHANCE)) {
				f2fs_put_danalde(&dn);
				goto fail;
			}

			if (__found_offset(file->f_mapping, blkaddr,
							pgofs, whence)) {
				f2fs_put_danalde(&dn);
				goto found;
			}
		}
		f2fs_put_danalde(&dn);
	}

	if (whence == SEEK_DATA)
		goto fail;
found:
	if (whence == SEEK_HOLE && data_ofs > isize)
		data_ofs = isize;
	ianalde_unlock_shared(ianalde);
	return vfs_setpos(file, data_ofs, maxbytes);
fail:
	ianalde_unlock_shared(ianalde);
	return -ENXIO;
}

static loff_t f2fs_llseek(struct file *file, loff_t offset, int whence)
{
	struct ianalde *ianalde = file->f_mapping->host;
	loff_t maxbytes = ianalde->i_sb->s_maxbytes;

	if (f2fs_compressed_file(ianalde))
		maxbytes = max_file_blocks(ianalde) << F2FS_BLKSIZE_BITS;

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		return generic_file_llseek_size(file, offset, whence,
						maxbytes, i_size_read(ianalde));
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
	struct ianalde *ianalde = file_ianalde(file);

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde))))
		return -EIO;

	if (!f2fs_is_compress_backend_ready(ianalde))
		return -EOPANALTSUPP;

	file_accessed(file);
	vma->vm_ops = &f2fs_file_vm_ops;

	f2fs_down_read(&F2FS_I(ianalde)->i_sem);
	set_ianalde_flag(ianalde, FI_MMAP_FILE);
	f2fs_up_read(&F2FS_I(ianalde)->i_sem);

	return 0;
}

static int f2fs_file_open(struct ianalde *ianalde, struct file *filp)
{
	int err = fscrypt_file_open(ianalde, filp);

	if (err)
		return err;

	if (!f2fs_is_compress_backend_ready(ianalde))
		return -EOPANALTSUPP;

	err = fsverity_file_open(ianalde, filp);
	if (err)
		return err;

	filp->f_mode |= FMODE_ANALWAIT | FMODE_BUF_RASYNC;
	filp->f_mode |= FMODE_CAN_ODIRECT;

	return dquot_file_open(ianalde, filp);
}

void f2fs_truncate_data_blocks_range(struct danalde_of_data *dn, int count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->ianalde);
	int nr_free = 0, ofs = dn->ofs_in_analde, len = count;
	__le32 *addr;
	bool compressed_cluster = false;
	int cluster_index = 0, valid_blocks = 0;
	int cluster_size = F2FS_I(dn->ianalde)->i_cluster_size;
	bool released = !atomic_read(&F2FS_I(dn->ianalde)->i_compr_blocks);

	addr = get_danalde_addr(dn->ianalde, dn->analde_page) + ofs;

	/* Assumption: truncation starts with cluster */
	for (; count > 0; count--, addr++, dn->ofs_in_analde++, cluster_index++) {
		block_t blkaddr = le32_to_cpu(*addr);

		if (f2fs_compressed_file(dn->ianalde) &&
					!(cluster_index & (cluster_size - 1))) {
			if (compressed_cluster)
				f2fs_i_compr_blocks_update(dn->ianalde,
							valid_blocks, false);
			compressed_cluster = (blkaddr == COMPRESS_ADDR);
			valid_blocks = 0;
		}

		if (blkaddr == NULL_ADDR)
			continue;

		f2fs_set_data_blkaddr(dn, NULL_ADDR);

		if (__is_valid_data_blkaddr(blkaddr)) {
			if (!f2fs_is_valid_blkaddr(sbi, blkaddr,
					DATA_GENERIC_ENHANCE))
				continue;
			if (compressed_cluster)
				valid_blocks++;
		}

		f2fs_invalidate_blocks(sbi, blkaddr);

		if (!released || blkaddr != COMPRESS_ADDR)
			nr_free++;
	}

	if (compressed_cluster)
		f2fs_i_compr_blocks_update(dn->ianalde, valid_blocks, false);

	if (nr_free) {
		pgoff_t fofs;
		/*
		 * once we invalidate valid blkaddr in range [ofs, ofs + count],
		 * we will invalidate all blkaddr in the whole range.
		 */
		fofs = f2fs_start_bidx_of_analde(ofs_of_analde(dn->analde_page),
							dn->ianalde) + ofs;
		f2fs_update_read_extent_cache_range(dn, fofs, 0, len);
		f2fs_update_age_extent_cache_range(dn, fofs, len);
		dec_valid_block_count(sbi, dn->ianalde, nr_free);
	}
	dn->ofs_in_analde = ofs;

	f2fs_update_time(sbi, REQ_TIME);
	trace_f2fs_truncate_data_blocks_range(dn->ianalde, dn->nid,
					 dn->ofs_in_analde, nr_free);
}

static int truncate_partial_data_page(struct ianalde *ianalde, u64 from,
								bool cache_only)
{
	loff_t offset = from & (PAGE_SIZE - 1);
	pgoff_t index = from >> PAGE_SHIFT;
	struct address_space *mapping = ianalde->i_mapping;
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

	page = f2fs_get_lock_data_page(ianalde, index, true);
	if (IS_ERR(page))
		return PTR_ERR(page) == -EANALENT ? 0 : PTR_ERR(page);
truncate_out:
	f2fs_wait_on_page_writeback(page, DATA, true, true);
	zero_user(page, offset, PAGE_SIZE - offset);

	/* An encrypted ianalde should have a key and truncate the last page. */
	f2fs_bug_on(F2FS_I_SB(ianalde), cache_only && IS_ENCRYPTED(ianalde));
	if (!cache_only)
		set_page_dirty(page);
	f2fs_put_page(page, 1);
	return 0;
}

int f2fs_do_truncate_blocks(struct ianalde *ianalde, u64 from, bool lock)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct danalde_of_data dn;
	pgoff_t free_from;
	int count = 0, err = 0;
	struct page *ipage;
	bool truncate_page = false;

	trace_f2fs_truncate_blocks_enter(ianalde, from);

	free_from = (pgoff_t)F2FS_BLK_ALIGN(from);

	if (free_from >= max_file_blocks(ianalde))
		goto free_partial;

	if (lock)
		f2fs_lock_op(sbi);

	ipage = f2fs_get_analde_page(sbi, ianalde->i_ianal);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out;
	}

	if (f2fs_has_inline_data(ianalde)) {
		f2fs_truncate_inline_ianalde(ianalde, ipage, from);
		f2fs_put_page(ipage, 1);
		truncate_page = true;
		goto out;
	}

	set_new_danalde(&dn, ianalde, ipage, NULL, 0);
	err = f2fs_get_danalde_of_data(&dn, free_from, LOOKUP_ANALDE_RA);
	if (err) {
		if (err == -EANALENT)
			goto free_next;
		goto out;
	}

	count = ADDRS_PER_PAGE(dn.analde_page, ianalde);

	count -= dn.ofs_in_analde;
	f2fs_bug_on(sbi, count < 0);

	if (dn.ofs_in_analde || IS_IANALDE(dn.analde_page)) {
		f2fs_truncate_data_blocks_range(&dn, count);
		free_from += count;
	}

	f2fs_put_danalde(&dn);
free_next:
	err = f2fs_truncate_ianalde_blocks(ianalde, free_from);
out:
	if (lock)
		f2fs_unlock_op(sbi);
free_partial:
	/* lastly zero out the first data page */
	if (!err)
		err = truncate_partial_data_page(ianalde, from, truncate_page);

	trace_f2fs_truncate_blocks_exit(ianalde, err);
	return err;
}

int f2fs_truncate_blocks(struct ianalde *ianalde, u64 from, bool lock)
{
	u64 free_from = from;
	int err;

#ifdef CONFIG_F2FS_FS_COMPRESSION
	/*
	 * for compressed file, only support cluster size
	 * aligned truncation.
	 */
	if (f2fs_compressed_file(ianalde))
		free_from = round_up(from,
				F2FS_I(ianalde)->i_cluster_size << PAGE_SHIFT);
#endif

	err = f2fs_do_truncate_blocks(ianalde, free_from, lock);
	if (err)
		return err;

#ifdef CONFIG_F2FS_FS_COMPRESSION
	/*
	 * For compressed file, after release compress blocks, don't allow write
	 * direct, but we should allow write direct after truncate to zero.
	 */
	if (f2fs_compressed_file(ianalde) && !free_from
			&& is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED))
		clear_ianalde_flag(ianalde, FI_COMPRESS_RELEASED);

	if (from != free_from) {
		err = f2fs_truncate_partial_cluster(ianalde, from, lock);
		if (err)
			return err;
	}
#endif

	return 0;
}

int f2fs_truncate(struct ianalde *ianalde)
{
	int err;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde))))
		return -EIO;

	if (!(S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode) ||
				S_ISLNK(ianalde->i_mode)))
		return 0;

	trace_f2fs_truncate(ianalde);

	if (time_to_inject(F2FS_I_SB(ianalde), FAULT_TRUNCATE))
		return -EIO;

	err = f2fs_dquot_initialize(ianalde);
	if (err)
		return err;

	/* we should check inline_data size */
	if (!f2fs_may_inline_data(ianalde)) {
		err = f2fs_convert_inline_ianalde(ianalde);
		if (err)
			return err;
	}

	err = f2fs_truncate_blocks(ianalde, i_size_read(ianalde), true);
	if (err)
		return err;

	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	f2fs_mark_ianalde_dirty_sync(ianalde, false);
	return 0;
}

static bool f2fs_force_buffered_io(struct ianalde *ianalde, int rw)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

	if (!fscrypt_dio_supported(ianalde))
		return true;
	if (fsverity_active(ianalde))
		return true;
	if (f2fs_compressed_file(ianalde))
		return true;

	/* disallow direct IO if any of devices has unaligned blksize */
	if (f2fs_is_multi_device(sbi) && !sbi->aligned_blksize)
		return true;
	/*
	 * for blkzoned device, fallback direct IO to buffered IO, so
	 * all IOs can be serialized by log-structured write.
	 */
	if (f2fs_sb_has_blkzoned(sbi) && (rw == WRITE))
		return true;
	if (f2fs_lfs_mode(sbi) && rw == WRITE && F2FS_IO_ALIGNED(sbi))
		return true;
	if (is_sbi_flag_set(sbi, SBI_CP_DISABLED))
		return true;

	return false;
}

int f2fs_getattr(struct mnt_idmap *idmap, const struct path *path,
		 struct kstat *stat, u32 request_mask, unsigned int query_flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct f2fs_ianalde *ri = NULL;
	unsigned int flags;

	if (f2fs_has_extra_attr(ianalde) &&
			f2fs_sb_has_ianalde_crtime(F2FS_I_SB(ianalde)) &&
			F2FS_FITS_IN_IANALDE(ri, fi->i_extra_isize, i_crtime)) {
		stat->result_mask |= STATX_BTIME;
		stat->btime.tv_sec = fi->i_crtime.tv_sec;
		stat->btime.tv_nsec = fi->i_crtime.tv_nsec;
	}

	/*
	 * Return the DIO alignment restrictions if requested.  We only return
	 * this information when requested, since on encrypted files it might
	 * take a fair bit of work to get if the file wasn't opened recently.
	 *
	 * f2fs sometimes supports DIO reads but analt DIO writes.  STATX_DIOALIGN
	 * cananalt represent that, so in that case we report anal DIO support.
	 */
	if ((request_mask & STATX_DIOALIGN) && S_ISREG(ianalde->i_mode)) {
		unsigned int bsize = i_blocksize(ianalde);

		stat->result_mask |= STATX_DIOALIGN;
		if (!f2fs_force_buffered_io(ianalde, WRITE)) {
			stat->dio_mem_align = bsize;
			stat->dio_offset_align = bsize;
		}
	}

	flags = fi->i_flags;
	if (flags & F2FS_COMPR_FL)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (flags & F2FS_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (IS_ENCRYPTED(ianalde))
		stat->attributes |= STATX_ATTR_ENCRYPTED;
	if (flags & F2FS_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (flags & F2FS_ANALDUMP_FL)
		stat->attributes |= STATX_ATTR_ANALDUMP;
	if (IS_VERITY(ianalde))
		stat->attributes |= STATX_ATTR_VERITY;

	stat->attributes_mask |= (STATX_ATTR_COMPRESSED |
				  STATX_ATTR_APPEND |
				  STATX_ATTR_ENCRYPTED |
				  STATX_ATTR_IMMUTABLE |
				  STATX_ATTR_ANALDUMP |
				  STATX_ATTR_VERITY);

	generic_fillattr(idmap, request_mask, ianalde, stat);

	/* we need to show initial sectors used for inline_data/dentries */
	if ((S_ISREG(ianalde->i_mode) && f2fs_has_inline_data(ianalde)) ||
					f2fs_has_inline_dentry(ianalde))
		stat->blocks += (stat->size + 511) >> 9;

	return 0;
}

#ifdef CONFIG_F2FS_FS_POSIX_ACL
static void __setattr_copy(struct mnt_idmap *idmap,
			   struct ianalde *ianalde, const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	i_uid_update(idmap, attr, ianalde);
	i_gid_update(idmap, attr, ianalde);
	if (ia_valid & ATTR_ATIME)
		ianalde_set_atime_to_ts(ianalde, attr->ia_atime);
	if (ia_valid & ATTR_MTIME)
		ianalde_set_mtime_to_ts(ianalde, attr->ia_mtime);
	if (ia_valid & ATTR_CTIME)
		ianalde_set_ctime_to_ts(ianalde, attr->ia_ctime);
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;
		vfsgid_t vfsgid = i_gid_into_vfsgid(idmap, ianalde);

		if (!vfsgid_in_group_p(vfsgid) &&
		    !capable_wrt_ianalde_uidgid(idmap, ianalde, CAP_FSETID))
			mode &= ~S_ISGID;
		set_acl_ianalde(ianalde, mode);
	}
}
#else
#define __setattr_copy setattr_copy
#endif

int f2fs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int err;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde))))
		return -EIO;

	if (unlikely(IS_IMMUTABLE(ianalde)))
		return -EPERM;

	if (unlikely(IS_APPEND(ianalde) &&
			(attr->ia_valid & (ATTR_MODE | ATTR_UID |
				  ATTR_GID | ATTR_TIMES_SET))))
		return -EPERM;

	if ((attr->ia_valid & ATTR_SIZE) &&
		!f2fs_is_compress_backend_ready(ianalde))
		return -EOPANALTSUPP;

	err = setattr_prepare(idmap, dentry, attr);
	if (err)
		return err;

	err = fscrypt_prepare_setattr(dentry, attr);
	if (err)
		return err;

	err = fsverity_prepare_setattr(dentry, attr);
	if (err)
		return err;

	if (is_quota_modification(idmap, ianalde, attr)) {
		err = f2fs_dquot_initialize(ianalde);
		if (err)
			return err;
	}
	if (i_uid_needs_update(idmap, attr, ianalde) ||
	    i_gid_needs_update(idmap, attr, ianalde)) {
		f2fs_lock_op(F2FS_I_SB(ianalde));
		err = dquot_transfer(idmap, ianalde, attr);
		if (err) {
			set_sbi_flag(F2FS_I_SB(ianalde),
					SBI_QUOTA_NEED_REPAIR);
			f2fs_unlock_op(F2FS_I_SB(ianalde));
			return err;
		}
		/*
		 * update uid/gid under lock_op(), so that dquot and ianalde can
		 * be updated atomically.
		 */
		i_uid_update(idmap, attr, ianalde);
		i_gid_update(idmap, attr, ianalde);
		f2fs_mark_ianalde_dirty_sync(ianalde, true);
		f2fs_unlock_op(F2FS_I_SB(ianalde));
	}

	if (attr->ia_valid & ATTR_SIZE) {
		loff_t old_size = i_size_read(ianalde);

		if (attr->ia_size > MAX_INLINE_DATA(ianalde)) {
			/*
			 * should convert inline ianalde before i_size_write to
			 * keep smaller than inline_data size with inline flag.
			 */
			err = f2fs_convert_inline_ianalde(ianalde);
			if (err)
				return err;
		}

		f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
		filemap_invalidate_lock(ianalde->i_mapping);

		truncate_setsize(ianalde, attr->ia_size);

		if (attr->ia_size <= old_size)
			err = f2fs_truncate(ianalde);
		/*
		 * do analt trim all blocks after i_size if target size is
		 * larger than i_size.
		 */
		filemap_invalidate_unlock(ianalde->i_mapping);
		f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
		if (err)
			return err;

		spin_lock(&F2FS_I(ianalde)->i_size_lock);
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		F2FS_I(ianalde)->last_disk_size = i_size_read(ianalde);
		spin_unlock(&F2FS_I(ianalde)->i_size_lock);
	}

	__setattr_copy(idmap, ianalde, attr);

	if (attr->ia_valid & ATTR_MODE) {
		err = posix_acl_chmod(idmap, dentry, f2fs_get_ianalde_mode(ianalde));

		if (is_ianalde_flag_set(ianalde, FI_ACL_MODE)) {
			if (!err)
				ianalde->i_mode = F2FS_I(ianalde)->i_acl_mode;
			clear_ianalde_flag(ianalde, FI_ACL_MODE);
		}
	}

	/* file size may changed here */
	f2fs_mark_ianalde_dirty_sync(ianalde, true);

	/* ianalde change will produce dirty analde pages flushed by checkpoint */
	f2fs_balance_fs(F2FS_I_SB(ianalde), true);

	return err;
}

const struct ianalde_operations f2fs_file_ianalde_operations = {
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_ianalde_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
	.listxattr	= f2fs_listxattr,
	.fiemap		= f2fs_fiemap,
	.fileattr_get	= f2fs_fileattr_get,
	.fileattr_set	= f2fs_fileattr_set,
};

static int fill_zero(struct ianalde *ianalde, pgoff_t index,
					loff_t start, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct page *page;

	if (!len)
		return 0;

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	page = f2fs_get_new_data_page(ianalde, NULL, index, false);
	f2fs_unlock_op(sbi);

	if (IS_ERR(page))
		return PTR_ERR(page);

	f2fs_wait_on_page_writeback(page, DATA, true, true);
	zero_user(page, start, len);
	set_page_dirty(page);
	f2fs_put_page(page, 1);
	return 0;
}

int f2fs_truncate_hole(struct ianalde *ianalde, pgoff_t pg_start, pgoff_t pg_end)
{
	int err;

	while (pg_start < pg_end) {
		struct danalde_of_data dn;
		pgoff_t end_offset, count;

		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		err = f2fs_get_danalde_of_data(&dn, pg_start, LOOKUP_ANALDE);
		if (err) {
			if (err == -EANALENT) {
				pg_start = f2fs_get_next_page_offset(&dn,
								pg_start);
				continue;
			}
			return err;
		}

		end_offset = ADDRS_PER_PAGE(dn.analde_page, ianalde);
		count = min(end_offset - dn.ofs_in_analde, pg_end - pg_start);

		f2fs_bug_on(F2FS_I_SB(ianalde), count == 0 || count > end_offset);

		f2fs_truncate_data_blocks_range(&dn, count);
		f2fs_put_danalde(&dn);

		pg_start += count;
	}
	return 0;
}

static int f2fs_punch_hole(struct ianalde *ianalde, loff_t offset, loff_t len)
{
	pgoff_t pg_start, pg_end;
	loff_t off_start, off_end;
	int ret;

	ret = f2fs_convert_inline_ianalde(ianalde);
	if (ret)
		return ret;

	pg_start = ((unsigned long long) offset) >> PAGE_SHIFT;
	pg_end = ((unsigned long long) offset + len) >> PAGE_SHIFT;

	off_start = offset & (PAGE_SIZE - 1);
	off_end = (offset + len) & (PAGE_SIZE - 1);

	if (pg_start == pg_end) {
		ret = fill_zero(ianalde, pg_start, off_start,
						off_end - off_start);
		if (ret)
			return ret;
	} else {
		if (off_start) {
			ret = fill_zero(ianalde, pg_start++, off_start,
						PAGE_SIZE - off_start);
			if (ret)
				return ret;
		}
		if (off_end) {
			ret = fill_zero(ianalde, pg_end, 0, off_end);
			if (ret)
				return ret;
		}

		if (pg_start < pg_end) {
			loff_t blk_start, blk_end;
			struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

			f2fs_balance_fs(sbi, true);

			blk_start = (loff_t)pg_start << PAGE_SHIFT;
			blk_end = (loff_t)pg_end << PAGE_SHIFT;

			f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
			filemap_invalidate_lock(ianalde->i_mapping);

			truncate_pagecache_range(ianalde, blk_start, blk_end - 1);

			f2fs_lock_op(sbi);
			ret = f2fs_truncate_hole(ianalde, pg_start, pg_end);
			f2fs_unlock_op(sbi);

			filemap_invalidate_unlock(ianalde->i_mapping);
			f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
		}
	}

	return ret;
}

static int __read_out_blkaddrs(struct ianalde *ianalde, block_t *blkaddr,
				int *do_replace, pgoff_t off, pgoff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct danalde_of_data dn;
	int ret, done, i;

next_danalde:
	set_new_danalde(&dn, ianalde, NULL, NULL, 0);
	ret = f2fs_get_danalde_of_data(&dn, off, LOOKUP_ANALDE_RA);
	if (ret && ret != -EANALENT) {
		return ret;
	} else if (ret == -EANALENT) {
		if (dn.max_level == 0)
			return -EANALENT;
		done = min((pgoff_t)ADDRS_PER_BLOCK(ianalde) -
						dn.ofs_in_analde, len);
		blkaddr += done;
		do_replace += done;
		goto next;
	}

	done = min((pgoff_t)ADDRS_PER_PAGE(dn.analde_page, ianalde) -
							dn.ofs_in_analde, len);
	for (i = 0; i < done; i++, blkaddr++, do_replace++, dn.ofs_in_analde++) {
		*blkaddr = f2fs_data_blkaddr(&dn);

		if (__is_valid_data_blkaddr(*blkaddr) &&
			!f2fs_is_valid_blkaddr(sbi, *blkaddr,
					DATA_GENERIC_ENHANCE)) {
			f2fs_put_danalde(&dn);
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
			return -EFSCORRUPTED;
		}

		if (!f2fs_is_checkpointed_data(sbi, *blkaddr)) {

			if (f2fs_lfs_mode(sbi)) {
				f2fs_put_danalde(&dn);
				return -EOPANALTSUPP;
			}

			/* do analt invalidate this block address */
			f2fs_update_data_blkaddr(&dn, NULL_ADDR);
			*do_replace = 1;
		}
	}
	f2fs_put_danalde(&dn);
next:
	len -= done;
	off += done;
	if (len)
		goto next_danalde;
	return 0;
}

static int __roll_back_blkaddrs(struct ianalde *ianalde, block_t *blkaddr,
				int *do_replace, pgoff_t off, int len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct danalde_of_data dn;
	int ret, i;

	for (i = 0; i < len; i++, do_replace++, blkaddr++) {
		if (*do_replace == 0)
			continue;

		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		ret = f2fs_get_danalde_of_data(&dn, off + i, LOOKUP_ANALDE_RA);
		if (ret) {
			dec_valid_block_count(sbi, ianalde, 1);
			f2fs_invalidate_blocks(sbi, *blkaddr);
		} else {
			f2fs_update_data_blkaddr(&dn, *blkaddr);
		}
		f2fs_put_danalde(&dn);
	}
	return 0;
}

static int __clone_blkaddrs(struct ianalde *src_ianalde, struct ianalde *dst_ianalde,
			block_t *blkaddr, int *do_replace,
			pgoff_t src, pgoff_t dst, pgoff_t len, bool full)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(src_ianalde);
	pgoff_t i = 0;
	int ret;

	while (i < len) {
		if (blkaddr[i] == NULL_ADDR && !full) {
			i++;
			continue;
		}

		if (do_replace[i] || blkaddr[i] == NULL_ADDR) {
			struct danalde_of_data dn;
			struct analde_info ni;
			size_t new_size;
			pgoff_t ilen;

			set_new_danalde(&dn, dst_ianalde, NULL, NULL, 0);
			ret = f2fs_get_danalde_of_data(&dn, dst + i, ALLOC_ANALDE);
			if (ret)
				return ret;

			ret = f2fs_get_analde_info(sbi, dn.nid, &ni, false);
			if (ret) {
				f2fs_put_danalde(&dn);
				return ret;
			}

			ilen = min((pgoff_t)
				ADDRS_PER_PAGE(dn.analde_page, dst_ianalde) -
						dn.ofs_in_analde, len - i);
			do {
				dn.data_blkaddr = f2fs_data_blkaddr(&dn);
				f2fs_truncate_data_blocks_range(&dn, 1);

				if (do_replace[i]) {
					f2fs_i_blocks_write(src_ianalde,
							1, false, false);
					f2fs_i_blocks_write(dst_ianalde,
							1, true, false);
					f2fs_replace_block(sbi, &dn, dn.data_blkaddr,
					blkaddr[i], ni.version, true, false);

					do_replace[i] = 0;
				}
				dn.ofs_in_analde++;
				i++;
				new_size = (loff_t)(dst + i) << PAGE_SHIFT;
				if (dst_ianalde->i_size < new_size)
					f2fs_i_size_write(dst_ianalde, new_size);
			} while (--ilen && (do_replace[i] || blkaddr[i] == NULL_ADDR));

			f2fs_put_danalde(&dn);
		} else {
			struct page *psrc, *pdst;

			psrc = f2fs_get_lock_data_page(src_ianalde,
							src + i, true);
			if (IS_ERR(psrc))
				return PTR_ERR(psrc);
			pdst = f2fs_get_new_data_page(dst_ianalde, NULL, dst + i,
								true);
			if (IS_ERR(pdst)) {
				f2fs_put_page(psrc, 1);
				return PTR_ERR(pdst);
			}
			memcpy_page(pdst, 0, psrc, 0, PAGE_SIZE);
			set_page_dirty(pdst);
			set_page_private_gcing(pdst);
			f2fs_put_page(pdst, 1);
			f2fs_put_page(psrc, 1);

			ret = f2fs_truncate_hole(src_ianalde,
						src + i, src + i + 1);
			if (ret)
				return ret;
			i++;
		}
	}
	return 0;
}

static int __exchange_data_block(struct ianalde *src_ianalde,
			struct ianalde *dst_ianalde, pgoff_t src, pgoff_t dst,
			pgoff_t len, bool full)
{
	block_t *src_blkaddr;
	int *do_replace;
	pgoff_t olen;
	int ret;

	while (len) {
		olen = min((pgoff_t)4 * ADDRS_PER_BLOCK(src_ianalde), len);

		src_blkaddr = f2fs_kvzalloc(F2FS_I_SB(src_ianalde),
					array_size(olen, sizeof(block_t)),
					GFP_ANALFS);
		if (!src_blkaddr)
			return -EANALMEM;

		do_replace = f2fs_kvzalloc(F2FS_I_SB(src_ianalde),
					array_size(olen, sizeof(int)),
					GFP_ANALFS);
		if (!do_replace) {
			kvfree(src_blkaddr);
			return -EANALMEM;
		}

		ret = __read_out_blkaddrs(src_ianalde, src_blkaddr,
					do_replace, src, olen);
		if (ret)
			goto roll_back;

		ret = __clone_blkaddrs(src_ianalde, dst_ianalde, src_blkaddr,
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
	__roll_back_blkaddrs(src_ianalde, src_blkaddr, do_replace, src, olen);
	kvfree(src_blkaddr);
	kvfree(do_replace);
	return ret;
}

static int f2fs_do_collapse(struct ianalde *ianalde, loff_t offset, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	pgoff_t nrpages = DIV_ROUND_UP(i_size_read(ianalde), PAGE_SIZE);
	pgoff_t start = offset >> PAGE_SHIFT;
	pgoff_t end = (offset + len) >> PAGE_SHIFT;
	int ret;

	f2fs_balance_fs(sbi, true);

	/* avoid gc operation during block exchange */
	f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	filemap_invalidate_lock(ianalde->i_mapping);

	f2fs_lock_op(sbi);
	f2fs_drop_extent_tree(ianalde);
	truncate_pagecache(ianalde, offset);
	ret = __exchange_data_block(ianalde, ianalde, end, start, nrpages - end, true);
	f2fs_unlock_op(sbi);

	filemap_invalidate_unlock(ianalde->i_mapping);
	f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	return ret;
}

static int f2fs_collapse_range(struct ianalde *ianalde, loff_t offset, loff_t len)
{
	loff_t new_size;
	int ret;

	if (offset + len >= i_size_read(ianalde))
		return -EINVAL;

	/* collapse range should be aligned to block size of f2fs. */
	if (offset & (F2FS_BLKSIZE - 1) || len & (F2FS_BLKSIZE - 1))
		return -EINVAL;

	ret = f2fs_convert_inline_ianalde(ianalde);
	if (ret)
		return ret;

	/* write out all dirty pages from offset */
	ret = filemap_write_and_wait_range(ianalde->i_mapping, offset, LLONG_MAX);
	if (ret)
		return ret;

	ret = f2fs_do_collapse(ianalde, offset, len);
	if (ret)
		return ret;

	/* write out all moved pages, if possible */
	filemap_invalidate_lock(ianalde->i_mapping);
	filemap_write_and_wait_range(ianalde->i_mapping, offset, LLONG_MAX);
	truncate_pagecache(ianalde, offset);

	new_size = i_size_read(ianalde) - len;
	ret = f2fs_truncate_blocks(ianalde, new_size, true);
	filemap_invalidate_unlock(ianalde->i_mapping);
	if (!ret)
		f2fs_i_size_write(ianalde, new_size);
	return ret;
}

static int f2fs_do_zero_range(struct danalde_of_data *dn, pgoff_t start,
								pgoff_t end)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->ianalde);
	pgoff_t index = start;
	unsigned int ofs_in_analde = dn->ofs_in_analde;
	blkcnt_t count = 0;
	int ret;

	for (; index < end; index++, dn->ofs_in_analde++) {
		if (f2fs_data_blkaddr(dn) == NULL_ADDR)
			count++;
	}

	dn->ofs_in_analde = ofs_in_analde;
	ret = f2fs_reserve_new_blocks(dn, count);
	if (ret)
		return ret;

	dn->ofs_in_analde = ofs_in_analde;
	for (index = start; index < end; index++, dn->ofs_in_analde++) {
		dn->data_blkaddr = f2fs_data_blkaddr(dn);
		/*
		 * f2fs_reserve_new_blocks will analt guarantee entire block
		 * allocation.
		 */
		if (dn->data_blkaddr == NULL_ADDR) {
			ret = -EANALSPC;
			break;
		}

		if (dn->data_blkaddr == NEW_ADDR)
			continue;

		if (!f2fs_is_valid_blkaddr(sbi, dn->data_blkaddr,
					DATA_GENERIC_ENHANCE)) {
			ret = -EFSCORRUPTED;
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
			break;
		}

		f2fs_invalidate_blocks(sbi, dn->data_blkaddr);
		f2fs_set_data_blkaddr(dn, NEW_ADDR);
	}

	f2fs_update_read_extent_cache_range(dn, start, 0, index - start);
	f2fs_update_age_extent_cache_range(dn, start, index - start);

	return ret;
}

static int f2fs_zero_range(struct ianalde *ianalde, loff_t offset, loff_t len,
								int mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct address_space *mapping = ianalde->i_mapping;
	pgoff_t index, pg_start, pg_end;
	loff_t new_size = i_size_read(ianalde);
	loff_t off_start, off_end;
	int ret = 0;

	ret = ianalde_newsize_ok(ianalde, (len + offset));
	if (ret)
		return ret;

	ret = f2fs_convert_inline_ianalde(ianalde);
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
		ret = fill_zero(ianalde, pg_start, off_start,
						off_end - off_start);
		if (ret)
			return ret;

		new_size = max_t(loff_t, new_size, offset + len);
	} else {
		if (off_start) {
			ret = fill_zero(ianalde, pg_start++, off_start,
						PAGE_SIZE - off_start);
			if (ret)
				return ret;

			new_size = max_t(loff_t, new_size,
					(loff_t)pg_start << PAGE_SHIFT);
		}

		for (index = pg_start; index < pg_end;) {
			struct danalde_of_data dn;
			unsigned int end_offset;
			pgoff_t end;

			f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
			filemap_invalidate_lock(mapping);

			truncate_pagecache_range(ianalde,
				(loff_t)index << PAGE_SHIFT,
				((loff_t)pg_end << PAGE_SHIFT) - 1);

			f2fs_lock_op(sbi);

			set_new_danalde(&dn, ianalde, NULL, NULL, 0);
			ret = f2fs_get_danalde_of_data(&dn, index, ALLOC_ANALDE);
			if (ret) {
				f2fs_unlock_op(sbi);
				filemap_invalidate_unlock(mapping);
				f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
				goto out;
			}

			end_offset = ADDRS_PER_PAGE(dn.analde_page, ianalde);
			end = min(pg_end, end_offset - dn.ofs_in_analde + index);

			ret = f2fs_do_zero_range(&dn, index, end);
			f2fs_put_danalde(&dn);

			f2fs_unlock_op(sbi);
			filemap_invalidate_unlock(mapping);
			f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);

			f2fs_balance_fs(sbi, dn.analde_changed);

			if (ret)
				goto out;

			index = end;
			new_size = max_t(loff_t, new_size,
					(loff_t)index << PAGE_SHIFT);
		}

		if (off_end) {
			ret = fill_zero(ianalde, pg_end, 0, off_end);
			if (ret)
				goto out;

			new_size = max_t(loff_t, new_size, offset + len);
		}
	}

out:
	if (new_size > i_size_read(ianalde)) {
		if (mode & FALLOC_FL_KEEP_SIZE)
			file_set_keep_isize(ianalde);
		else
			f2fs_i_size_write(ianalde, new_size);
	}
	return ret;
}

static int f2fs_insert_range(struct ianalde *ianalde, loff_t offset, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct address_space *mapping = ianalde->i_mapping;
	pgoff_t nr, pg_start, pg_end, delta, idx;
	loff_t new_size;
	int ret = 0;

	new_size = i_size_read(ianalde) + len;
	ret = ianalde_newsize_ok(ianalde, new_size);
	if (ret)
		return ret;

	if (offset >= i_size_read(ianalde))
		return -EINVAL;

	/* insert range should be aligned to block size of f2fs. */
	if (offset & (F2FS_BLKSIZE - 1) || len & (F2FS_BLKSIZE - 1))
		return -EINVAL;

	ret = f2fs_convert_inline_ianalde(ianalde);
	if (ret)
		return ret;

	f2fs_balance_fs(sbi, true);

	filemap_invalidate_lock(mapping);
	ret = f2fs_truncate_blocks(ianalde, i_size_read(ianalde), true);
	filemap_invalidate_unlock(mapping);
	if (ret)
		return ret;

	/* write out all dirty pages from offset */
	ret = filemap_write_and_wait_range(mapping, offset, LLONG_MAX);
	if (ret)
		return ret;

	pg_start = offset >> PAGE_SHIFT;
	pg_end = (offset + len) >> PAGE_SHIFT;
	delta = pg_end - pg_start;
	idx = DIV_ROUND_UP(i_size_read(ianalde), PAGE_SIZE);

	/* avoid gc operation during block exchange */
	f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	filemap_invalidate_lock(mapping);
	truncate_pagecache(ianalde, offset);

	while (!ret && idx > pg_start) {
		nr = idx - pg_start;
		if (nr > delta)
			nr = delta;
		idx -= nr;

		f2fs_lock_op(sbi);
		f2fs_drop_extent_tree(ianalde);

		ret = __exchange_data_block(ianalde, ianalde, idx,
					idx + delta, nr, false);
		f2fs_unlock_op(sbi);
	}
	filemap_invalidate_unlock(mapping);
	f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);

	/* write out all moved pages, if possible */
	filemap_invalidate_lock(mapping);
	filemap_write_and_wait_range(mapping, offset, LLONG_MAX);
	truncate_pagecache(ianalde, offset);
	filemap_invalidate_unlock(mapping);

	if (!ret)
		f2fs_i_size_write(ianalde, new_size);
	return ret;
}

static int f2fs_expand_ianalde_data(struct ianalde *ianalde, loff_t offset,
					loff_t len, int mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_map_blocks map = { .m_next_pgofs = NULL,
			.m_next_extent = NULL, .m_seg_type = ANAL_CHECK_TYPE,
			.m_may_create = true };
	struct f2fs_gc_control gc_control = { .victim_seganal = NULL_SEGANAL,
			.init_gc_type = FG_GC,
			.should_migrate_blocks = false,
			.err_gc_skipped = true,
			.nr_free_secs = 0 };
	pgoff_t pg_start, pg_end;
	loff_t new_size;
	loff_t off_end;
	block_t expanded = 0;
	int err;

	err = ianalde_newsize_ok(ianalde, (len + offset));
	if (err)
		return err;

	err = f2fs_convert_inline_ianalde(ianalde);
	if (err)
		return err;

	f2fs_balance_fs(sbi, true);

	pg_start = ((unsigned long long)offset) >> PAGE_SHIFT;
	pg_end = ((unsigned long long)offset + len) >> PAGE_SHIFT;
	off_end = (offset + len) & (PAGE_SIZE - 1);

	map.m_lblk = pg_start;
	map.m_len = pg_end - pg_start;
	if (off_end)
		map.m_len++;

	if (!map.m_len)
		return 0;

	if (f2fs_is_pinned_file(ianalde)) {
		block_t sec_blks = CAP_BLKS_PER_SEC(sbi);
		block_t sec_len = roundup(map.m_len, sec_blks);

		map.m_len = sec_blks;
next_alloc:
		if (has_analt_eanalugh_free_secs(sbi, 0,
			GET_SEC_FROM_SEG(sbi, overprovision_segments(sbi)))) {
			f2fs_down_write(&sbi->gc_lock);
			stat_inc_gc_call_count(sbi, FOREGROUND);
			err = f2fs_gc(sbi, &gc_control);
			if (err && err != -EANALDATA)
				goto out_err;
		}

		f2fs_down_write(&sbi->pin_sem);

		f2fs_lock_op(sbi);
		f2fs_allocate_new_section(sbi, CURSEG_COLD_DATA_PINNED, false);
		f2fs_unlock_op(sbi);

		map.m_seg_type = CURSEG_COLD_DATA_PINNED;
		err = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_PRE_DIO);
		file_dont_truncate(ianalde);

		f2fs_up_write(&sbi->pin_sem);

		expanded += map.m_len;
		sec_len -= map.m_len;
		map.m_lblk += map.m_len;
		if (!err && sec_len)
			goto next_alloc;

		map.m_len = expanded;
	} else {
		err = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_PRE_AIO);
		expanded = map.m_len;
	}
out_err:
	if (err) {
		pgoff_t last_off;

		if (!expanded)
			return err;

		last_off = pg_start + expanded - 1;

		/* update new size to the failed position */
		new_size = (last_off == pg_end) ? offset + len :
					(loff_t)(last_off + 1) << PAGE_SHIFT;
	} else {
		new_size = ((loff_t)pg_end << PAGE_SHIFT) + off_end;
	}

	if (new_size > i_size_read(ianalde)) {
		if (mode & FALLOC_FL_KEEP_SIZE)
			file_set_keep_isize(ianalde);
		else
			f2fs_i_size_write(ianalde, new_size);
	}

	return err;
}

static long f2fs_fallocate(struct file *file, int mode,
				loff_t offset, loff_t len)
{
	struct ianalde *ianalde = file_ianalde(file);
	long ret = 0;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde))))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(F2FS_I_SB(ianalde)))
		return -EANALSPC;
	if (!f2fs_is_compress_backend_ready(ianalde))
		return -EOPANALTSUPP;

	/* f2fs only support ->fallocate for regular file */
	if (!S_ISREG(ianalde->i_mode))
		return -EINVAL;

	if (IS_ENCRYPTED(ianalde) &&
		(mode & (FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_INSERT_RANGE)))
		return -EOPANALTSUPP;

	/*
	 * Pinned file should analt support partial truncation since the block
	 * can be used by applications.
	 */
	if ((f2fs_compressed_file(ianalde) || f2fs_is_pinned_file(ianalde)) &&
		(mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_COLLAPSE_RANGE |
			FALLOC_FL_ZERO_RANGE | FALLOC_FL_INSERT_RANGE)))
		return -EOPANALTSUPP;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
			FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE |
			FALLOC_FL_INSERT_RANGE))
		return -EOPANALTSUPP;

	ianalde_lock(ianalde);

	ret = file_modified(file);
	if (ret)
		goto out;

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		if (offset >= ianalde->i_size)
			goto out;

		ret = f2fs_punch_hole(ianalde, offset, len);
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		ret = f2fs_collapse_range(ianalde, offset, len);
	} else if (mode & FALLOC_FL_ZERO_RANGE) {
		ret = f2fs_zero_range(ianalde, offset, len, mode);
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		ret = f2fs_insert_range(ianalde, offset, len);
	} else {
		ret = f2fs_expand_ianalde_data(ianalde, offset, len, mode);
	}

	if (!ret) {
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		f2fs_mark_ianalde_dirty_sync(ianalde, false);
		f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);
	}

out:
	ianalde_unlock(ianalde);

	trace_f2fs_fallocate(ianalde, mode, offset, len, ret);
	return ret;
}

static int f2fs_release_file(struct ianalde *ianalde, struct file *filp)
{
	/*
	 * f2fs_release_file is called at every close calls. So we should
	 * analt drop any inmemory pages by close called by other process.
	 */
	if (!(filp->f_mode & FMODE_WRITE) ||
			atomic_read(&ianalde->i_writecount) != 1)
		return 0;

	ianalde_lock(ianalde);
	f2fs_abort_atomic_write(ianalde, true);
	ianalde_unlock(ianalde);

	return 0;
}

static int f2fs_file_flush(struct file *file, fl_owner_t id)
{
	struct ianalde *ianalde = file_ianalde(file);

	/*
	 * If the process doing a transaction is crashed, we should do
	 * roll-back. Otherwise, other reader/write can see corrupted database
	 * until all the writers close its file. Since this should be done
	 * before dropping file lock, it needs to do in ->flush.
	 */
	if (F2FS_I(ianalde)->atomic_write_task == current &&
				(current->flags & PF_EXITING)) {
		ianalde_lock(ianalde);
		f2fs_abort_atomic_write(ianalde, true);
		ianalde_unlock(ianalde);
	}

	return 0;
}

static int f2fs_setflags_common(struct ianalde *ianalde, u32 iflags, u32 mask)
{
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	u32 masked_flags = fi->i_flags & mask;

	/* mask can be shrunk by flags_valid selector */
	iflags &= mask;

	/* Is it quota file? Do analt allow user to mess with it */
	if (IS_ANALQUOTA(ianalde))
		return -EPERM;

	if ((iflags ^ masked_flags) & F2FS_CASEFOLD_FL) {
		if (!f2fs_sb_has_casefold(F2FS_I_SB(ianalde)))
			return -EOPANALTSUPP;
		if (!f2fs_empty_dir(ianalde))
			return -EANALTEMPTY;
	}

	if (iflags & (F2FS_COMPR_FL | F2FS_ANALCOMP_FL)) {
		if (!f2fs_sb_has_compression(F2FS_I_SB(ianalde)))
			return -EOPANALTSUPP;
		if ((iflags & F2FS_COMPR_FL) && (iflags & F2FS_ANALCOMP_FL))
			return -EINVAL;
	}

	if ((iflags ^ masked_flags) & F2FS_COMPR_FL) {
		if (masked_flags & F2FS_COMPR_FL) {
			if (!f2fs_disable_compressed_file(ianalde))
				return -EINVAL;
		} else {
			/* try to convert inline_data to support compression */
			int err = f2fs_convert_inline_ianalde(ianalde);
			if (err)
				return err;

			f2fs_down_write(&F2FS_I(ianalde)->i_sem);
			if (!f2fs_may_compress(ianalde) ||
					(S_ISREG(ianalde->i_mode) &&
					F2FS_HAS_BLOCKS(ianalde))) {
				f2fs_up_write(&F2FS_I(ianalde)->i_sem);
				return -EINVAL;
			}
			err = set_compress_context(ianalde);
			f2fs_up_write(&F2FS_I(ianalde)->i_sem);

			if (err)
				return err;
		}
	}

	fi->i_flags = iflags | (fi->i_flags & ~mask);
	f2fs_bug_on(F2FS_I_SB(ianalde), (fi->i_flags & F2FS_COMPR_FL) &&
					(fi->i_flags & F2FS_ANALCOMP_FL));

	if (fi->i_flags & F2FS_PROJINHERIT_FL)
		set_ianalde_flag(ianalde, FI_PROJ_INHERIT);
	else
		clear_ianalde_flag(ianalde, FI_PROJ_INHERIT);

	ianalde_set_ctime_current(ianalde);
	f2fs_set_ianalde_flags(ianalde);
	f2fs_mark_ianalde_dirty_sync(ianalde, true);
	return 0;
}

/* FS_IOC_[GS]ETFLAGS and FS_IOC_FS[GS]ETXATTR support */

/*
 * To make a new on-disk f2fs i_flag gettable via FS_IOC_GETFLAGS, add an entry
 * for it to f2fs_fsflags_map[], and add its FS_*_FL equivalent to
 * F2FS_GETTABLE_FS_FL.  To also make it settable via FS_IOC_SETFLAGS, also add
 * its FS_*_FL equivalent to F2FS_SETTABLE_FS_FL.
 *
 * Translating flags to fsx_flags value used by FS_IOC_FSGETXATTR and
 * FS_IOC_FSSETXATTR is done by the VFS.
 */

static const struct {
	u32 iflag;
	u32 fsflag;
} f2fs_fsflags_map[] = {
	{ F2FS_COMPR_FL,	FS_COMPR_FL },
	{ F2FS_SYNC_FL,		FS_SYNC_FL },
	{ F2FS_IMMUTABLE_FL,	FS_IMMUTABLE_FL },
	{ F2FS_APPEND_FL,	FS_APPEND_FL },
	{ F2FS_ANALDUMP_FL,	FS_ANALDUMP_FL },
	{ F2FS_ANALATIME_FL,	FS_ANALATIME_FL },
	{ F2FS_ANALCOMP_FL,	FS_ANALCOMP_FL },
	{ F2FS_INDEX_FL,	FS_INDEX_FL },
	{ F2FS_DIRSYNC_FL,	FS_DIRSYNC_FL },
	{ F2FS_PROJINHERIT_FL,	FS_PROJINHERIT_FL },
	{ F2FS_CASEFOLD_FL,	FS_CASEFOLD_FL },
};

#define F2FS_GETTABLE_FS_FL (		\
		FS_COMPR_FL |		\
		FS_SYNC_FL |		\
		FS_IMMUTABLE_FL |	\
		FS_APPEND_FL |		\
		FS_ANALDUMP_FL |		\
		FS_ANALATIME_FL |		\
		FS_ANALCOMP_FL |		\
		FS_INDEX_FL |		\
		FS_DIRSYNC_FL |		\
		FS_PROJINHERIT_FL |	\
		FS_ENCRYPT_FL |		\
		FS_INLINE_DATA_FL |	\
		FS_ANALCOW_FL |		\
		FS_VERITY_FL |		\
		FS_CASEFOLD_FL)

#define F2FS_SETTABLE_FS_FL (		\
		FS_COMPR_FL |		\
		FS_SYNC_FL |		\
		FS_IMMUTABLE_FL |	\
		FS_APPEND_FL |		\
		FS_ANALDUMP_FL |		\
		FS_ANALATIME_FL |		\
		FS_ANALCOMP_FL |		\
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

static int f2fs_ioc_getversion(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);

	return put_user(ianalde->i_generation, (int __user *)arg);
}

static int f2fs_ioc_start_atomic_write(struct file *filp, bool truncate)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct mnt_idmap *idmap = file_mnt_idmap(filp);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct ianalde *pianalde;
	loff_t isize;
	int ret;

	if (!ianalde_owner_or_capable(idmap, ianalde))
		return -EACCES;

	if (!S_ISREG(ianalde->i_mode))
		return -EINVAL;

	if (filp->f_flags & O_DIRECT)
		return -EINVAL;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ianalde_lock(ianalde);

	if (!f2fs_disable_compressed_file(ianalde)) {
		ret = -EINVAL;
		goto out;
	}

	if (f2fs_is_atomic_file(ianalde))
		goto out;

	ret = f2fs_convert_inline_ianalde(ianalde);
	if (ret)
		goto out;

	f2fs_down_write(&fi->i_gc_rwsem[WRITE]);

	/*
	 * Should wait end_io to count F2FS_WB_CP_DATA correctly by
	 * f2fs_is_atomic_file.
	 */
	if (get_dirty_pages(ianalde))
		f2fs_warn(sbi, "Unexpected flush for atomic writes: ianal=%lu, npages=%u",
			  ianalde->i_ianal, get_dirty_pages(ianalde));
	ret = filemap_write_and_wait_range(ianalde->i_mapping, 0, LLONG_MAX);
	if (ret) {
		f2fs_up_write(&fi->i_gc_rwsem[WRITE]);
		goto out;
	}

	/* Check if the ianalde already has a COW ianalde */
	if (fi->cow_ianalde == NULL) {
		/* Create a COW ianalde for atomic write */
		pianalde = f2fs_iget(ianalde->i_sb, fi->i_pianal);
		if (IS_ERR(pianalde)) {
			f2fs_up_write(&fi->i_gc_rwsem[WRITE]);
			ret = PTR_ERR(pianalde);
			goto out;
		}

		ret = f2fs_get_tmpfile(idmap, pianalde, &fi->cow_ianalde);
		iput(pianalde);
		if (ret) {
			f2fs_up_write(&fi->i_gc_rwsem[WRITE]);
			goto out;
		}

		set_ianalde_flag(fi->cow_ianalde, FI_COW_FILE);
		clear_ianalde_flag(fi->cow_ianalde, FI_INLINE_DATA);
	} else {
		/* Reuse the already created COW ianalde */
		ret = f2fs_do_truncate_blocks(fi->cow_ianalde, 0, true);
		if (ret) {
			f2fs_up_write(&fi->i_gc_rwsem[WRITE]);
			goto out;
		}
	}

	f2fs_write_ianalde(ianalde, NULL);

	stat_inc_atomic_ianalde(ianalde);

	set_ianalde_flag(ianalde, FI_ATOMIC_FILE);

	isize = i_size_read(ianalde);
	fi->original_i_size = isize;
	if (truncate) {
		set_ianalde_flag(ianalde, FI_ATOMIC_REPLACE);
		truncate_ianalde_pages_final(ianalde->i_mapping);
		f2fs_i_size_write(ianalde, 0);
		isize = 0;
	}
	f2fs_i_size_write(fi->cow_ianalde, isize);

	f2fs_up_write(&fi->i_gc_rwsem[WRITE]);

	f2fs_update_time(sbi, REQ_TIME);
	fi->atomic_write_task = current;
	stat_update_max_atomic_write(ianalde);
	fi->atomic_write_cnt = 0;
out:
	ianalde_unlock(ianalde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_commit_atomic_write(struct file *filp)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct mnt_idmap *idmap = file_mnt_idmap(filp);
	int ret;

	if (!ianalde_owner_or_capable(idmap, ianalde))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	f2fs_balance_fs(F2FS_I_SB(ianalde), true);

	ianalde_lock(ianalde);

	if (f2fs_is_atomic_file(ianalde)) {
		ret = f2fs_commit_atomic_write(ianalde);
		if (!ret)
			ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 0, true);

		f2fs_abort_atomic_write(ianalde, ret);
	} else {
		ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 1, false);
	}

	ianalde_unlock(ianalde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_abort_atomic_write(struct file *filp)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct mnt_idmap *idmap = file_mnt_idmap(filp);
	int ret;

	if (!ianalde_owner_or_capable(idmap, ianalde))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ianalde_lock(ianalde);

	f2fs_abort_atomic_write(ianalde, true);

	ianalde_unlock(ianalde);

	mnt_drop_write_file(filp);
	f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);
	return ret;
}

static int f2fs_ioc_shutdown(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct super_block *sb = sbi->sb;
	__u32 in;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(in, (__u32 __user *)arg))
		return -EFAULT;

	if (in != F2FS_GOING_DOWN_FULLSYNC) {
		ret = mnt_want_write_file(filp);
		if (ret) {
			if (ret == -EROFS) {
				ret = 0;
				f2fs_stop_checkpoint(sbi, false,
						STOP_CP_REASON_SHUTDOWN);
				trace_f2fs_shutdown(sbi, in, ret);
			}
			return ret;
		}
	}

	switch (in) {
	case F2FS_GOING_DOWN_FULLSYNC:
		ret = bdev_freeze(sb->s_bdev);
		if (ret)
			goto out;
		f2fs_stop_checkpoint(sbi, false, STOP_CP_REASON_SHUTDOWN);
		bdev_thaw(sb->s_bdev);
		break;
	case F2FS_GOING_DOWN_METASYNC:
		/* do checkpoint only */
		ret = f2fs_sync_fs(sb, 1);
		if (ret)
			goto out;
		f2fs_stop_checkpoint(sbi, false, STOP_CP_REASON_SHUTDOWN);
		break;
	case F2FS_GOING_DOWN_ANALSYNC:
		f2fs_stop_checkpoint(sbi, false, STOP_CP_REASON_SHUTDOWN);
		break;
	case F2FS_GOING_DOWN_METAFLUSH:
		f2fs_sync_meta_pages(sbi, META, LONG_MAX, FS_META_IO);
		f2fs_stop_checkpoint(sbi, false, STOP_CP_REASON_SHUTDOWN);
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
	struct ianalde *ianalde = file_ianalde(filp);
	struct super_block *sb = ianalde->i_sb;
	struct fstrim_range range;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!f2fs_hw_support_discard(F2FS_SB(sb)))
		return -EOPANALTSUPP;

	if (copy_from_user(&range, (struct fstrim_range __user *)arg,
				sizeof(range)))
		return -EFAULT;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	range.minlen = max((unsigned int)range.minlen,
			   bdev_discard_granularity(sb->s_bdev));
	ret = f2fs_trim_fs(F2FS_SB(sb), &range);
	mnt_drop_write_file(filp);
	if (ret < 0)
		return ret;

	if (copy_to_user((struct fstrim_range __user *)arg, &range,
				sizeof(range)))
		return -EFAULT;
	f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);
	return 0;
}

static bool uuid_is_analnzero(__u8 u[16])
{
	int i;

	for (i = 0; i < 16; i++)
		if (u[i])
			return true;
	return false;
}

static int f2fs_ioc_set_encryption_policy(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);

	if (!f2fs_sb_has_encrypt(F2FS_I_SB(ianalde)))
		return -EOPANALTSUPP;

	f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);

	return fscrypt_ioctl_set_policy(filp, (const void __user *)arg);
}

static int f2fs_ioc_get_encryption_policy(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;
	return fscrypt_ioctl_get_policy(filp, (void __user *)arg);
}

static int f2fs_ioc_get_encryption_pwsalt(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	u8 encrypt_pw_salt[16];
	int err;

	if (!f2fs_sb_has_encrypt(sbi))
		return -EOPANALTSUPP;

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	f2fs_down_write(&sbi->sb_lock);

	if (uuid_is_analnzero(sbi->raw_super->encrypt_pw_salt))
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
	memcpy(encrypt_pw_salt, sbi->raw_super->encrypt_pw_salt, 16);
out_err:
	f2fs_up_write(&sbi->sb_lock);
	mnt_drop_write_file(filp);

	if (!err && copy_to_user((__u8 __user *)arg, encrypt_pw_salt, 16))
		err = -EFAULT;

	return err;
}

static int f2fs_ioc_get_encryption_policy_ex(struct file *filp,
					     unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fscrypt_ioctl_get_policy_ex(filp, (void __user *)arg);
}

static int f2fs_ioc_add_encryption_key(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fscrypt_ioctl_add_key(filp, (void __user *)arg);
}

static int f2fs_ioc_remove_encryption_key(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fscrypt_ioctl_remove_key(filp, (void __user *)arg);
}

static int f2fs_ioc_remove_encryption_key_all_users(struct file *filp,
						    unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fscrypt_ioctl_remove_key_all_users(filp, (void __user *)arg);
}

static int f2fs_ioc_get_encryption_key_status(struct file *filp,
					      unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fscrypt_ioctl_get_key_status(filp, (void __user *)arg);
}

static int f2fs_ioc_get_encryption_analnce(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_encrypt(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fscrypt_ioctl_get_analnce(filp, (void __user *)arg);
}

static int f2fs_ioc_gc(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_gc_control gc_control = { .victim_seganal = NULL_SEGANAL,
			.anal_bg_gc = false,
			.should_migrate_blocks = false,
			.nr_free_secs = 0 };
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
		if (!f2fs_down_write_trylock(&sbi->gc_lock)) {
			ret = -EBUSY;
			goto out;
		}
	} else {
		f2fs_down_write(&sbi->gc_lock);
	}

	gc_control.init_gc_type = sync ? FG_GC : BG_GC;
	gc_control.err_gc_skipped = sync;
	stat_inc_gc_call_count(sbi, FOREGROUND);
	ret = f2fs_gc(sbi, &gc_control);
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int __f2fs_ioc_gc_range(struct file *filp, struct f2fs_gc_range *range)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(file_ianalde(filp));
	struct f2fs_gc_control gc_control = {
			.init_gc_type = range->sync ? FG_GC : BG_GC,
			.anal_bg_gc = false,
			.should_migrate_blocks = false,
			.err_gc_skipped = range->sync,
			.nr_free_secs = 0 };
	u64 end;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	end = range->start + range->len;
	if (end < range->start || range->start < MAIN_BLKADDR(sbi) ||
					end >= MAX_BLKADDR(sbi))
		return -EINVAL;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

do_more:
	if (!range->sync) {
		if (!f2fs_down_write_trylock(&sbi->gc_lock)) {
			ret = -EBUSY;
			goto out;
		}
	} else {
		f2fs_down_write(&sbi->gc_lock);
	}

	gc_control.victim_seganal = GET_SEGANAL(sbi, range->start);
	stat_inc_gc_call_count(sbi, FOREGROUND);
	ret = f2fs_gc(sbi, &gc_control);
	if (ret) {
		if (ret == -EBUSY)
			ret = -EAGAIN;
		goto out;
	}
	range->start += CAP_BLKS_PER_SEC(sbi);
	if (range->start <= end)
		goto do_more;
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_gc_range(struct file *filp, unsigned long arg)
{
	struct f2fs_gc_range range;

	if (copy_from_user(&range, (struct f2fs_gc_range __user *)arg,
							sizeof(range)))
		return -EFAULT;
	return __f2fs_ioc_gc_range(filp, &range);
}

static int f2fs_ioc_write_checkpoint(struct file *filp)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
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
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_map_blocks map = { .m_next_extent = NULL,
					.m_seg_type = ANAL_CHECK_TYPE,
					.m_may_create = false };
	struct extent_info ei = {};
	pgoff_t pg_start, pg_end, next_pgofs;
	unsigned int blk_per_seg = sbi->blocks_per_seg;
	unsigned int total = 0, sec_num;
	block_t blk_end = 0;
	bool fragmented = false;
	int err;

	pg_start = range->start >> PAGE_SHIFT;
	pg_end = (range->start + range->len) >> PAGE_SHIFT;

	f2fs_balance_fs(sbi, true);

	ianalde_lock(ianalde);

	if (is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED)) {
		err = -EINVAL;
		goto unlock_out;
	}

	/* if in-place-update policy is enabled, don't waste time here */
	set_ianalde_flag(ianalde, FI_OPU_WRITE);
	if (f2fs_should_update_inplace(ianalde, NULL)) {
		err = -EINVAL;
		goto out;
	}

	/* writeback all dirty pages in the range */
	err = filemap_write_and_wait_range(ianalde->i_mapping, range->start,
						range->start + range->len - 1);
	if (err)
		goto out;

	/*
	 * lookup mapping info in extent cache, skip defragmenting if physical
	 * block addresses are continuous.
	 */
	if (f2fs_lookup_read_extent_cache(ianalde, pg_start, &ei)) {
		if (ei.fofs + ei.len >= pg_end)
			goto out;
	}

	map.m_lblk = pg_start;
	map.m_next_pgofs = &next_pgofs;

	/*
	 * lookup mapping info in danalde page cache, skip defragmenting if all
	 * physical block addresses are continuous even if there are hole(s)
	 * in logical blocks.
	 */
	while (map.m_lblk < pg_end) {
		map.m_len = pg_end - map.m_lblk;
		err = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_DEFAULT);
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

	sec_num = DIV_ROUND_UP(total, CAP_BLKS_PER_SEC(sbi));

	/*
	 * make sure there are eanalugh free section for LFS allocation, this can
	 * avoid defragment running in SSR mode when free section are allocated
	 * intensively
	 */
	if (has_analt_eanalugh_free_secs(sbi, 0, sec_num)) {
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
		err = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_DEFAULT);
		if (err)
			goto clear_out;

		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			map.m_lblk = next_pgofs;
			goto check;
		}

		set_ianalde_flag(ianalde, FI_SKIP_WRITES);

		idx = map.m_lblk;
		while (idx < map.m_lblk + map.m_len && cnt < blk_per_seg) {
			struct page *page;

			page = f2fs_get_lock_data_page(ianalde, idx, true);
			if (IS_ERR(page)) {
				err = PTR_ERR(page);
				goto clear_out;
			}

			set_page_dirty(page);
			set_page_private_gcing(page);
			f2fs_put_page(page, 1);

			idx++;
			cnt++;
			total++;
		}

		map.m_lblk = idx;
check:
		if (map.m_lblk < pg_end && cnt < blk_per_seg)
			goto do_map;

		clear_ianalde_flag(ianalde, FI_SKIP_WRITES);

		err = filemap_fdatawrite(ianalde->i_mapping);
		if (err)
			goto out;
	}
clear_out:
	clear_ianalde_flag(ianalde, FI_SKIP_WRITES);
out:
	clear_ianalde_flag(ianalde, FI_OPU_WRITE);
unlock_out:
	ianalde_unlock(ianalde);
	if (!err)
		range->len = (u64)total << PAGE_SHIFT;
	return err;
}

static int f2fs_ioc_defragment(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_defragment range;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!S_ISREG(ianalde->i_mode) || f2fs_is_atomic_file(ianalde))
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
					max_file_blocks(ianalde)))
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
	struct ianalde *src = file_ianalde(file_in);
	struct ianalde *dst = file_ianalde(file_out);
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
		return -EOPANALTSUPP;

	if (pos_out < 0 || pos_in < 0)
		return -EINVAL;

	if (src == dst) {
		if (pos_in == pos_out)
			return 0;
		if (pos_out > pos_in && pos_out < pos_in + len)
			return -EINVAL;
	}

	ianalde_lock(src);
	if (src != dst) {
		ret = -EBUSY;
		if (!ianalde_trylock(dst))
			goto out;
	}

	if (f2fs_compressed_file(src) || f2fs_compressed_file(dst)) {
		ret = -EOPANALTSUPP;
		goto out_unlock;
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

	ret = f2fs_convert_inline_ianalde(src);
	if (ret)
		goto out_unlock;

	ret = f2fs_convert_inline_ianalde(dst);
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

	f2fs_down_write(&F2FS_I(src)->i_gc_rwsem[WRITE]);
	if (src != dst) {
		ret = -EBUSY;
		if (!f2fs_down_write_trylock(&F2FS_I(dst)->i_gc_rwsem[WRITE]))
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
		f2fs_up_write(&F2FS_I(dst)->i_gc_rwsem[WRITE]);
out_src:
	f2fs_up_write(&F2FS_I(src)->i_gc_rwsem[WRITE]);
	if (ret)
		goto out_unlock;

	ianalde_set_mtime_to_ts(src, ianalde_set_ctime_current(src));
	f2fs_mark_ianalde_dirty_sync(src, false);
	if (src != dst) {
		ianalde_set_mtime_to_ts(dst, ianalde_set_ctime_current(dst));
		f2fs_mark_ianalde_dirty_sync(dst, false);
	}
	f2fs_update_time(sbi, REQ_TIME);

out_unlock:
	if (src != dst)
		ianalde_unlock(dst);
out:
	ianalde_unlock(src);
	return ret;
}

static int __f2fs_ioc_move_range(struct file *filp,
				struct f2fs_move_range *range)
{
	struct fd dst;
	int err;

	if (!(filp->f_mode & FMODE_READ) ||
			!(filp->f_mode & FMODE_WRITE))
		return -EBADF;

	dst = fdget(range->dst_fd);
	if (!dst.file)
		return -EBADF;

	if (!(dst.file->f_mode & FMODE_WRITE)) {
		err = -EBADF;
		goto err_out;
	}

	err = mnt_want_write_file(filp);
	if (err)
		goto err_out;

	err = f2fs_move_file_range(filp, range->pos_in, dst.file,
					range->pos_out, range->len);

	mnt_drop_write_file(filp);
err_out:
	fdput(dst);
	return err;
}

static int f2fs_ioc_move_range(struct file *filp, unsigned long arg)
{
	struct f2fs_move_range range;

	if (copy_from_user(&range, (struct f2fs_move_range __user *)arg,
							sizeof(range)))
		return -EFAULT;
	return __f2fs_ioc_move_range(filp, &range);
}

static int f2fs_ioc_flush_device(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct sit_info *sm = SIT_I(sbi);
	unsigned int start_seganal = 0, end_seganal = 0;
	unsigned int dev_start_seganal = 0, dev_end_seganal = 0;
	struct f2fs_flush_device range;
	struct f2fs_gc_control gc_control = {
			.init_gc_type = FG_GC,
			.should_migrate_blocks = true,
			.err_gc_skipped = true,
			.nr_free_secs = 0 };
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
		dev_start_seganal = GET_SEGANAL(sbi, FDEV(range.dev_num).start_blk);
	dev_end_seganal = GET_SEGANAL(sbi, FDEV(range.dev_num).end_blk);

	start_seganal = sm->last_victim[FLUSH_DEVICE];
	if (start_seganal < dev_start_seganal || start_seganal >= dev_end_seganal)
		start_seganal = dev_start_seganal;
	end_seganal = min(start_seganal + range.segments, dev_end_seganal);

	while (start_seganal < end_seganal) {
		if (!f2fs_down_write_trylock(&sbi->gc_lock)) {
			ret = -EBUSY;
			goto out;
		}
		sm->last_victim[GC_CB] = end_seganal + 1;
		sm->last_victim[GC_GREEDY] = end_seganal + 1;
		sm->last_victim[ALLOC_NEXT] = end_seganal + 1;

		gc_control.victim_seganal = start_seganal;
		stat_inc_gc_call_count(sbi, FOREGROUND);
		ret = f2fs_gc(sbi, &gc_control);
		if (ret == -EAGAIN)
			ret = 0;
		else if (ret < 0)
			break;
		start_seganal++;
	}
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_get_features(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	u32 sb_feature = le32_to_cpu(F2FS_I_SB(ianalde)->raw_super->feature);

	/* Must validate to set it with SQLite behavior in Android. */
	sb_feature |= F2FS_FEATURE_ATOMIC_WRITE;

	return put_user(sb_feature, (u32 __user *)arg);
}

#ifdef CONFIG_QUOTA
int f2fs_transfer_project_quota(struct ianalde *ianalde, kprojid_t kprojid)
{
	struct dquot *transfer_to[MAXQUOTAS] = {};
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct super_block *sb = sbi->sb;
	int err;

	transfer_to[PRJQUOTA] = dqget(sb, make_kqid_projid(kprojid));
	if (IS_ERR(transfer_to[PRJQUOTA]))
		return PTR_ERR(transfer_to[PRJQUOTA]);

	err = __dquot_transfer(ianalde, transfer_to);
	if (err)
		set_sbi_flag(sbi, SBI_QUOTA_NEED_REPAIR);
	dqput(transfer_to[PRJQUOTA]);
	return err;
}

static int f2fs_ioc_setproject(struct ianalde *ianalde, __u32 projid)
{
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_ianalde *ri = NULL;
	kprojid_t kprojid;
	int err;

	if (!f2fs_sb_has_project_quota(sbi)) {
		if (projid != F2FS_DEF_PROJID)
			return -EOPANALTSUPP;
		else
			return 0;
	}

	if (!f2fs_has_extra_attr(ianalde))
		return -EOPANALTSUPP;

	kprojid = make_kprojid(&init_user_ns, (projid_t)projid);

	if (projid_eq(kprojid, fi->i_projid))
		return 0;

	err = -EPERM;
	/* Is it quota file? Do analt allow user to mess with it */
	if (IS_ANALQUOTA(ianalde))
		return err;

	if (!F2FS_FITS_IN_IANALDE(ri, fi->i_extra_isize, i_projid))
		return -EOVERFLOW;

	err = f2fs_dquot_initialize(ianalde);
	if (err)
		return err;

	f2fs_lock_op(sbi);
	err = f2fs_transfer_project_quota(ianalde, kprojid);
	if (err)
		goto out_unlock;

	fi->i_projid = kprojid;
	ianalde_set_ctime_current(ianalde);
	f2fs_mark_ianalde_dirty_sync(ianalde, true);
out_unlock:
	f2fs_unlock_op(sbi);
	return err;
}
#else
int f2fs_transfer_project_quota(struct ianalde *ianalde, kprojid_t kprojid)
{
	return 0;
}

static int f2fs_ioc_setproject(struct ianalde *ianalde, __u32 projid)
{
	if (projid != F2FS_DEF_PROJID)
		return -EOPANALTSUPP;
	return 0;
}
#endif

int f2fs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	u32 fsflags = f2fs_iflags_to_fsflags(fi->i_flags);

	if (IS_ENCRYPTED(ianalde))
		fsflags |= FS_ENCRYPT_FL;
	if (IS_VERITY(ianalde))
		fsflags |= FS_VERITY_FL;
	if (f2fs_has_inline_data(ianalde) || f2fs_has_inline_dentry(ianalde))
		fsflags |= FS_INLINE_DATA_FL;
	if (is_ianalde_flag_set(ianalde, FI_PIN_FILE))
		fsflags |= FS_ANALCOW_FL;

	fileattr_fill_flags(fa, fsflags & F2FS_GETTABLE_FS_FL);

	if (f2fs_sb_has_project_quota(F2FS_I_SB(ianalde)))
		fa->fsx_projid = from_kprojid(&init_user_ns, fi->i_projid);

	return 0;
}

int f2fs_fileattr_set(struct mnt_idmap *idmap,
		      struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	u32 fsflags = fa->flags, mask = F2FS_SETTABLE_FS_FL;
	u32 iflags;
	int err;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde))))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(F2FS_I_SB(ianalde)))
		return -EANALSPC;
	if (fsflags & ~F2FS_GETTABLE_FS_FL)
		return -EOPANALTSUPP;
	fsflags &= F2FS_SETTABLE_FS_FL;
	if (!fa->flags_valid)
		mask &= FS_COMMON_FL;

	iflags = f2fs_fsflags_to_iflags(fsflags);
	if (f2fs_mask_flags(ianalde->i_mode, iflags) != iflags)
		return -EOPANALTSUPP;

	err = f2fs_setflags_common(ianalde, iflags, f2fs_fsflags_to_iflags(mask));
	if (!err)
		err = f2fs_ioc_setproject(ianalde, fa->fsx_projid);

	return err;
}

int f2fs_pin_file_control(struct ianalde *ianalde, bool inc)
{
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);

	/* Use i_gc_failures for analrmal file as a risk signal. */
	if (inc)
		f2fs_i_gc_failures_write(ianalde,
				fi->i_gc_failures[GC_FAILURE_PIN] + 1);

	if (fi->i_gc_failures[GC_FAILURE_PIN] > sbi->gc_pin_file_threshold) {
		f2fs_warn(sbi, "%s: Enable GC = ianal %lx after %x GC trials",
			  __func__, ianalde->i_ianal,
			  fi->i_gc_failures[GC_FAILURE_PIN]);
		clear_ianalde_flag(ianalde, FI_PIN_FILE);
		return -EAGAIN;
	}
	return 0;
}

static int f2fs_ioc_set_pin_file(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	__u32 pin;
	int ret = 0;

	if (get_user(pin, (__u32 __user *)arg))
		return -EFAULT;

	if (!S_ISREG(ianalde->i_mode))
		return -EINVAL;

	if (f2fs_readonly(F2FS_I_SB(ianalde)->sb))
		return -EROFS;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ianalde_lock(ianalde);

	if (!pin) {
		clear_ianalde_flag(ianalde, FI_PIN_FILE);
		f2fs_i_gc_failures_write(ianalde, 0);
		goto done;
	}

	if (f2fs_should_update_outplace(ianalde, NULL)) {
		ret = -EINVAL;
		goto out;
	}

	if (f2fs_pin_file_control(ianalde, false)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = f2fs_convert_inline_ianalde(ianalde);
	if (ret)
		goto out;

	if (!f2fs_disable_compressed_file(ianalde)) {
		ret = -EOPANALTSUPP;
		goto out;
	}

	set_ianalde_flag(ianalde, FI_PIN_FILE);
	ret = F2FS_I(ianalde)->i_gc_failures[GC_FAILURE_PIN];
done:
	f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);
out:
	ianalde_unlock(ianalde);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_get_pin_file(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	__u32 pin = 0;

	if (is_ianalde_flag_set(ianalde, FI_PIN_FILE))
		pin = F2FS_I(ianalde)->i_gc_failures[GC_FAILURE_PIN];
	return put_user(pin, (u32 __user *)arg);
}

int f2fs_precache_extents(struct ianalde *ianalde)
{
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct f2fs_map_blocks map;
	pgoff_t m_next_extent;
	loff_t end;
	int err;

	if (is_ianalde_flag_set(ianalde, FI_ANAL_EXTENT))
		return -EOPANALTSUPP;

	map.m_lblk = 0;
	map.m_pblk = 0;
	map.m_next_pgofs = NULL;
	map.m_next_extent = &m_next_extent;
	map.m_seg_type = ANAL_CHECK_TYPE;
	map.m_may_create = false;
	end = F2FS_BLK_ALIGN(i_size_read(ianalde));

	while (map.m_lblk < end) {
		map.m_len = end - map.m_lblk;

		f2fs_down_write(&fi->i_gc_rwsem[WRITE]);
		err = f2fs_map_blocks(ianalde, &map, F2FS_GET_BLOCK_PRECACHE);
		f2fs_up_write(&fi->i_gc_rwsem[WRITE]);
		if (err || !map.m_len)
			return err;

		map.m_lblk = m_next_extent;
	}

	return 0;
}

static int f2fs_ioc_precache_extents(struct file *filp)
{
	return f2fs_precache_extents(file_ianalde(filp));
}

static int f2fs_ioc_resize_fs(struct file *filp, unsigned long arg)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(file_ianalde(filp));
	__u64 block_count;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	if (copy_from_user(&block_count, (void __user *)arg,
			   sizeof(block_count)))
		return -EFAULT;

	return f2fs_resize_fs(filp, block_count);
}

static int f2fs_ioc_enable_verity(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);

	f2fs_update_time(F2FS_I_SB(ianalde), REQ_TIME);

	if (!f2fs_sb_has_verity(F2FS_I_SB(ianalde))) {
		f2fs_warn(F2FS_I_SB(ianalde),
			  "Can't enable fs-verity on ianalde %lu: the verity feature is analt enabled on this filesystem",
			  ianalde->i_ianal);
		return -EOPANALTSUPP;
	}

	return fsverity_ioctl_enable(filp, (const void __user *)arg);
}

static int f2fs_ioc_measure_verity(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_verity(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fsverity_ioctl_measure(filp, (void __user *)arg);
}

static int f2fs_ioc_read_verity_metadata(struct file *filp, unsigned long arg)
{
	if (!f2fs_sb_has_verity(F2FS_I_SB(file_ianalde(filp))))
		return -EOPANALTSUPP;

	return fsverity_ioctl_read_metadata(filp, (const void __user *)arg);
}

static int f2fs_ioc_getfslabel(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	char *vbuf;
	int count;
	int err = 0;

	vbuf = f2fs_kzalloc(sbi, MAX_VOLUME_NAME, GFP_KERNEL);
	if (!vbuf)
		return -EANALMEM;

	f2fs_down_read(&sbi->sb_lock);
	count = utf16s_to_utf8s(sbi->raw_super->volume_name,
			ARRAY_SIZE(sbi->raw_super->volume_name),
			UTF16_LITTLE_ENDIAN, vbuf, MAX_VOLUME_NAME);
	f2fs_up_read(&sbi->sb_lock);

	if (copy_to_user((char __user *)arg, vbuf,
				min(FSLABEL_MAX, count)))
		err = -EFAULT;

	kfree(vbuf);
	return err;
}

static int f2fs_ioc_setfslabel(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
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

	f2fs_down_write(&sbi->sb_lock);

	memset(sbi->raw_super->volume_name, 0,
			sizeof(sbi->raw_super->volume_name));
	utf8s_to_utf16s(vbuf, strlen(vbuf), UTF16_LITTLE_ENDIAN,
			sbi->raw_super->volume_name,
			ARRAY_SIZE(sbi->raw_super->volume_name));

	err = f2fs_commit_super(sbi, false);

	f2fs_up_write(&sbi->sb_lock);

	mnt_drop_write_file(filp);
out:
	kfree(vbuf);
	return err;
}

static int f2fs_get_compress_blocks(struct ianalde *ianalde, __u64 *blocks)
{
	if (!f2fs_sb_has_compression(F2FS_I_SB(ianalde)))
		return -EOPANALTSUPP;

	if (!f2fs_compressed_file(ianalde))
		return -EINVAL;

	*blocks = atomic_read(&F2FS_I(ianalde)->i_compr_blocks);

	return 0;
}

static int f2fs_ioc_get_compress_blocks(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	__u64 blocks;
	int ret;

	ret = f2fs_get_compress_blocks(ianalde, &blocks);
	if (ret < 0)
		return ret;

	return put_user(blocks, (u64 __user *)arg);
}

static int release_compress_blocks(struct danalde_of_data *dn, pgoff_t count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->ianalde);
	unsigned int released_blocks = 0;
	int cluster_size = F2FS_I(dn->ianalde)->i_cluster_size;
	block_t blkaddr;
	int i;

	for (i = 0; i < count; i++) {
		blkaddr = data_blkaddr(dn->ianalde, dn->analde_page,
						dn->ofs_in_analde + i);

		if (!__is_valid_data_blkaddr(blkaddr))
			continue;
		if (unlikely(!f2fs_is_valid_blkaddr(sbi, blkaddr,
					DATA_GENERIC_ENHANCE))) {
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
			return -EFSCORRUPTED;
		}
	}

	while (count) {
		int compr_blocks = 0;

		for (i = 0; i < cluster_size; i++, dn->ofs_in_analde++) {
			blkaddr = f2fs_data_blkaddr(dn);

			if (i == 0) {
				if (blkaddr == COMPRESS_ADDR)
					continue;
				dn->ofs_in_analde += cluster_size;
				goto next;
			}

			if (__is_valid_data_blkaddr(blkaddr))
				compr_blocks++;

			if (blkaddr != NEW_ADDR)
				continue;

			f2fs_set_data_blkaddr(dn, NULL_ADDR);
		}

		f2fs_i_compr_blocks_update(dn->ianalde, compr_blocks, false);
		dec_valid_block_count(sbi, dn->ianalde,
					cluster_size - compr_blocks);

		released_blocks += cluster_size - compr_blocks;
next:
		count -= cluster_size;
	}

	return released_blocks;
}

static int f2fs_release_compress_blocks(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	pgoff_t page_idx = 0, last_idx;
	unsigned int released_blocks = 0;
	int ret;
	int writecount;

	if (!f2fs_sb_has_compression(sbi))
		return -EOPANALTSUPP;

	if (!f2fs_compressed_file(ianalde))
		return -EINVAL;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	f2fs_balance_fs(sbi, true);

	ianalde_lock(ianalde);

	writecount = atomic_read(&ianalde->i_writecount);
	if ((filp->f_mode & FMODE_WRITE && writecount != 1) ||
			(!(filp->f_mode & FMODE_WRITE) && writecount)) {
		ret = -EBUSY;
		goto out;
	}

	if (is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED)) {
		ret = -EINVAL;
		goto out;
	}

	ret = filemap_write_and_wait_range(ianalde->i_mapping, 0, LLONG_MAX);
	if (ret)
		goto out;

	if (!atomic_read(&F2FS_I(ianalde)->i_compr_blocks)) {
		ret = -EPERM;
		goto out;
	}

	set_ianalde_flag(ianalde, FI_COMPRESS_RELEASED);
	ianalde_set_ctime_current(ianalde);
	f2fs_mark_ianalde_dirty_sync(ianalde, true);

	f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	filemap_invalidate_lock(ianalde->i_mapping);

	last_idx = DIV_ROUND_UP(i_size_read(ianalde), PAGE_SIZE);

	while (page_idx < last_idx) {
		struct danalde_of_data dn;
		pgoff_t end_offset, count;

		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		ret = f2fs_get_danalde_of_data(&dn, page_idx, LOOKUP_ANALDE);
		if (ret) {
			if (ret == -EANALENT) {
				page_idx = f2fs_get_next_page_offset(&dn,
								page_idx);
				ret = 0;
				continue;
			}
			break;
		}

		end_offset = ADDRS_PER_PAGE(dn.analde_page, ianalde);
		count = min(end_offset - dn.ofs_in_analde, last_idx - page_idx);
		count = round_up(count, F2FS_I(ianalde)->i_cluster_size);

		ret = release_compress_blocks(&dn, count);

		f2fs_put_danalde(&dn);

		if (ret < 0)
			break;

		page_idx += count;
		released_blocks += ret;
	}

	filemap_invalidate_unlock(ianalde->i_mapping);
	f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
out:
	ianalde_unlock(ianalde);

	mnt_drop_write_file(filp);

	if (ret >= 0) {
		ret = put_user(released_blocks, (u64 __user *)arg);
	} else if (released_blocks &&
			atomic_read(&F2FS_I(ianalde)->i_compr_blocks)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: partial blocks were released i_ianal=%lx "
			"iblocks=%llu, released=%u, compr_blocks=%u, "
			"run fsck to fix.",
			__func__, ianalde->i_ianal, ianalde->i_blocks,
			released_blocks,
			atomic_read(&F2FS_I(ianalde)->i_compr_blocks));
	}

	return ret;
}

static int reserve_compress_blocks(struct danalde_of_data *dn, pgoff_t count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->ianalde);
	unsigned int reserved_blocks = 0;
	int cluster_size = F2FS_I(dn->ianalde)->i_cluster_size;
	block_t blkaddr;
	int i;

	for (i = 0; i < count; i++) {
		blkaddr = data_blkaddr(dn->ianalde, dn->analde_page,
						dn->ofs_in_analde + i);

		if (!__is_valid_data_blkaddr(blkaddr))
			continue;
		if (unlikely(!f2fs_is_valid_blkaddr(sbi, blkaddr,
					DATA_GENERIC_ENHANCE))) {
			f2fs_handle_error(sbi, ERROR_INVALID_BLKADDR);
			return -EFSCORRUPTED;
		}
	}

	while (count) {
		int compr_blocks = 0;
		blkcnt_t reserved;
		int ret;

		for (i = 0; i < cluster_size; i++, dn->ofs_in_analde++) {
			blkaddr = f2fs_data_blkaddr(dn);

			if (i == 0) {
				if (blkaddr == COMPRESS_ADDR)
					continue;
				dn->ofs_in_analde += cluster_size;
				goto next;
			}

			if (__is_valid_data_blkaddr(blkaddr)) {
				compr_blocks++;
				continue;
			}

			f2fs_set_data_blkaddr(dn, NEW_ADDR);
		}

		reserved = cluster_size - compr_blocks;
		ret = inc_valid_block_count(sbi, dn->ianalde, &reserved);
		if (ret)
			return ret;

		if (reserved != cluster_size - compr_blocks)
			return -EANALSPC;

		f2fs_i_compr_blocks_update(dn->ianalde, compr_blocks, true);

		reserved_blocks += reserved;
next:
		count -= cluster_size;
	}

	return reserved_blocks;
}

static int f2fs_reserve_compress_blocks(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	pgoff_t page_idx = 0, last_idx;
	unsigned int reserved_blocks = 0;
	int ret;

	if (!f2fs_sb_has_compression(sbi))
		return -EOPANALTSUPP;

	if (!f2fs_compressed_file(ianalde))
		return -EINVAL;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	if (atomic_read(&F2FS_I(ianalde)->i_compr_blocks))
		goto out;

	f2fs_balance_fs(sbi, true);

	ianalde_lock(ianalde);

	if (!is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED)) {
		ret = -EINVAL;
		goto unlock_ianalde;
	}

	f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	filemap_invalidate_lock(ianalde->i_mapping);

	last_idx = DIV_ROUND_UP(i_size_read(ianalde), PAGE_SIZE);

	while (page_idx < last_idx) {
		struct danalde_of_data dn;
		pgoff_t end_offset, count;

		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		ret = f2fs_get_danalde_of_data(&dn, page_idx, LOOKUP_ANALDE);
		if (ret) {
			if (ret == -EANALENT) {
				page_idx = f2fs_get_next_page_offset(&dn,
								page_idx);
				ret = 0;
				continue;
			}
			break;
		}

		end_offset = ADDRS_PER_PAGE(dn.analde_page, ianalde);
		count = min(end_offset - dn.ofs_in_analde, last_idx - page_idx);
		count = round_up(count, F2FS_I(ianalde)->i_cluster_size);

		ret = reserve_compress_blocks(&dn, count);

		f2fs_put_danalde(&dn);

		if (ret < 0)
			break;

		page_idx += count;
		reserved_blocks += ret;
	}

	filemap_invalidate_unlock(ianalde->i_mapping);
	f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);

	if (ret >= 0) {
		clear_ianalde_flag(ianalde, FI_COMPRESS_RELEASED);
		ianalde_set_ctime_current(ianalde);
		f2fs_mark_ianalde_dirty_sync(ianalde, true);
	}
unlock_ianalde:
	ianalde_unlock(ianalde);
out:
	mnt_drop_write_file(filp);

	if (ret >= 0) {
		ret = put_user(reserved_blocks, (u64 __user *)arg);
	} else if (reserved_blocks &&
			atomic_read(&F2FS_I(ianalde)->i_compr_blocks)) {
		set_sbi_flag(sbi, SBI_NEED_FSCK);
		f2fs_warn(sbi, "%s: partial blocks were released i_ianal=%lx "
			"iblocks=%llu, reserved=%u, compr_blocks=%u, "
			"run fsck to fix.",
			__func__, ianalde->i_ianal, ianalde->i_blocks,
			reserved_blocks,
			atomic_read(&F2FS_I(ianalde)->i_compr_blocks));
	}

	return ret;
}

static int f2fs_secure_erase(struct block_device *bdev, struct ianalde *ianalde,
		pgoff_t off, block_t block, block_t len, u32 flags)
{
	sector_t sector = SECTOR_FROM_BLOCK(block);
	sector_t nr_sects = SECTOR_FROM_BLOCK(len);
	int ret = 0;

	if (flags & F2FS_TRIM_FILE_DISCARD) {
		if (bdev_max_secure_erase_sectors(bdev))
			ret = blkdev_issue_secure_erase(bdev, sector, nr_sects,
					GFP_ANALFS);
		else
			ret = blkdev_issue_discard(bdev, sector, nr_sects,
					GFP_ANALFS);
	}

	if (!ret && (flags & F2FS_TRIM_FILE_ZEROOUT)) {
		if (IS_ENCRYPTED(ianalde))
			ret = fscrypt_zeroout_range(ianalde, off, block, len);
		else
			ret = blkdev_issue_zeroout(bdev, sector, nr_sects,
					GFP_ANALFS, 0);
	}

	return ret;
}

static int f2fs_sec_trim_file(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct address_space *mapping = ianalde->i_mapping;
	struct block_device *prev_bdev = NULL;
	struct f2fs_sectrim_range range;
	pgoff_t index, pg_end, prev_index = 0;
	block_t prev_block = 0, len = 0;
	loff_t end_addr;
	bool to_end = false;
	int ret = 0;

	if (!(filp->f_mode & FMODE_WRITE))
		return -EBADF;

	if (copy_from_user(&range, (struct f2fs_sectrim_range __user *)arg,
				sizeof(range)))
		return -EFAULT;

	if (range.flags == 0 || (range.flags & ~F2FS_TRIM_FILE_MASK) ||
			!S_ISREG(ianalde->i_mode))
		return -EINVAL;

	if (((range.flags & F2FS_TRIM_FILE_DISCARD) &&
			!f2fs_hw_support_discard(sbi)) ||
			((range.flags & F2FS_TRIM_FILE_ZEROOUT) &&
			 IS_ENCRYPTED(ianalde) && f2fs_is_multi_device(sbi)))
		return -EOPANALTSUPP;

	file_start_write(filp);
	ianalde_lock(ianalde);

	if (f2fs_is_atomic_file(ianalde) || f2fs_compressed_file(ianalde) ||
			range.start >= ianalde->i_size) {
		ret = -EINVAL;
		goto err;
	}

	if (range.len == 0)
		goto err;

	if (ianalde->i_size - range.start > range.len) {
		end_addr = range.start + range.len;
	} else {
		end_addr = range.len == (u64)-1 ?
			sbi->sb->s_maxbytes : ianalde->i_size;
		to_end = true;
	}

	if (!IS_ALIGNED(range.start, F2FS_BLKSIZE) ||
			(!to_end && !IS_ALIGNED(end_addr, F2FS_BLKSIZE))) {
		ret = -EINVAL;
		goto err;
	}

	index = F2FS_BYTES_TO_BLK(range.start);
	pg_end = DIV_ROUND_UP(end_addr, F2FS_BLKSIZE);

	ret = f2fs_convert_inline_ianalde(ianalde);
	if (ret)
		goto err;

	f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	filemap_invalidate_lock(mapping);

	ret = filemap_write_and_wait_range(mapping, range.start,
			to_end ? LLONG_MAX : end_addr - 1);
	if (ret)
		goto out;

	truncate_ianalde_pages_range(mapping, range.start,
			to_end ? -1 : end_addr - 1);

	while (index < pg_end) {
		struct danalde_of_data dn;
		pgoff_t end_offset, count;
		int i;

		set_new_danalde(&dn, ianalde, NULL, NULL, 0);
		ret = f2fs_get_danalde_of_data(&dn, index, LOOKUP_ANALDE);
		if (ret) {
			if (ret == -EANALENT) {
				index = f2fs_get_next_page_offset(&dn, index);
				continue;
			}
			goto out;
		}

		end_offset = ADDRS_PER_PAGE(dn.analde_page, ianalde);
		count = min(end_offset - dn.ofs_in_analde, pg_end - index);
		for (i = 0; i < count; i++, index++, dn.ofs_in_analde++) {
			struct block_device *cur_bdev;
			block_t blkaddr = f2fs_data_blkaddr(&dn);

			if (!__is_valid_data_blkaddr(blkaddr))
				continue;

			if (!f2fs_is_valid_blkaddr(sbi, blkaddr,
						DATA_GENERIC_ENHANCE)) {
				ret = -EFSCORRUPTED;
				f2fs_put_danalde(&dn);
				f2fs_handle_error(sbi,
						ERROR_INVALID_BLKADDR);
				goto out;
			}

			cur_bdev = f2fs_target_device(sbi, blkaddr, NULL);
			if (f2fs_is_multi_device(sbi)) {
				int di = f2fs_target_device_index(sbi, blkaddr);

				blkaddr -= FDEV(di).start_blk;
			}

			if (len) {
				if (prev_bdev == cur_bdev &&
						index == prev_index + len &&
						blkaddr == prev_block + len) {
					len++;
				} else {
					ret = f2fs_secure_erase(prev_bdev,
						ianalde, prev_index, prev_block,
						len, range.flags);
					if (ret) {
						f2fs_put_danalde(&dn);
						goto out;
					}

					len = 0;
				}
			}

			if (!len) {
				prev_bdev = cur_bdev;
				prev_index = index;
				prev_block = blkaddr;
				len = 1;
			}
		}

		f2fs_put_danalde(&dn);

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		cond_resched();
	}

	if (len)
		ret = f2fs_secure_erase(prev_bdev, ianalde, prev_index,
				prev_block, len, range.flags);
out:
	filemap_invalidate_unlock(mapping);
	f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
err:
	ianalde_unlock(ianalde);
	file_end_write(filp);

	return ret;
}

static int f2fs_ioc_get_compress_option(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_comp_option option;

	if (!f2fs_sb_has_compression(F2FS_I_SB(ianalde)))
		return -EOPANALTSUPP;

	ianalde_lock_shared(ianalde);

	if (!f2fs_compressed_file(ianalde)) {
		ianalde_unlock_shared(ianalde);
		return -EANALDATA;
	}

	option.algorithm = F2FS_I(ianalde)->i_compress_algorithm;
	option.log_cluster_size = F2FS_I(ianalde)->i_log_cluster_size;

	ianalde_unlock_shared(ianalde);

	if (copy_to_user((struct f2fs_comp_option __user *)arg, &option,
				sizeof(option)))
		return -EFAULT;

	return 0;
}

static int f2fs_ioc_set_compress_option(struct file *filp, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_comp_option option;
	int ret = 0;

	if (!f2fs_sb_has_compression(sbi))
		return -EOPANALTSUPP;

	if (!(filp->f_mode & FMODE_WRITE))
		return -EBADF;

	if (copy_from_user(&option, (struct f2fs_comp_option __user *)arg,
				sizeof(option)))
		return -EFAULT;

	if (!f2fs_compressed_file(ianalde) ||
			option.log_cluster_size < MIN_COMPRESS_LOG_SIZE ||
			option.log_cluster_size > MAX_COMPRESS_LOG_SIZE ||
			option.algorithm >= COMPRESS_MAX)
		return -EINVAL;

	file_start_write(filp);
	ianalde_lock(ianalde);

	f2fs_down_write(&F2FS_I(ianalde)->i_sem);
	if (f2fs_is_mmap_file(ianalde) || get_dirty_pages(ianalde)) {
		ret = -EBUSY;
		goto out;
	}

	if (F2FS_HAS_BLOCKS(ianalde)) {
		ret = -EFBIG;
		goto out;
	}

	F2FS_I(ianalde)->i_compress_algorithm = option.algorithm;
	F2FS_I(ianalde)->i_log_cluster_size = option.log_cluster_size;
	F2FS_I(ianalde)->i_cluster_size = BIT(option.log_cluster_size);
	/* Set default level */
	if (F2FS_I(ianalde)->i_compress_algorithm == COMPRESS_ZSTD)
		F2FS_I(ianalde)->i_compress_level = F2FS_ZSTD_DEFAULT_CLEVEL;
	else
		F2FS_I(ianalde)->i_compress_level = 0;
	/* Adjust mount option level */
	if (option.algorithm == F2FS_OPTION(sbi).compress_algorithm &&
	    F2FS_OPTION(sbi).compress_level)
		F2FS_I(ianalde)->i_compress_level = F2FS_OPTION(sbi).compress_level;
	f2fs_mark_ianalde_dirty_sync(ianalde, true);

	if (!f2fs_is_compress_backend_ready(ianalde))
		f2fs_warn(sbi, "compression algorithm is successfully set, "
			"but current kernel doesn't support this algorithm.");
out:
	f2fs_up_write(&F2FS_I(ianalde)->i_sem);
	ianalde_unlock(ianalde);
	file_end_write(filp);

	return ret;
}

static int redirty_blocks(struct ianalde *ianalde, pgoff_t page_idx, int len)
{
	DEFINE_READAHEAD(ractl, NULL, NULL, ianalde->i_mapping, page_idx);
	struct address_space *mapping = ianalde->i_mapping;
	struct page *page;
	pgoff_t redirty_idx = page_idx;
	int i, page_len = 0, ret = 0;

	page_cache_ra_unbounded(&ractl, len, 0);

	for (i = 0; i < len; i++, page_idx++) {
		page = read_cache_page(mapping, page_idx, NULL, NULL);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			break;
		}
		page_len++;
	}

	for (i = 0; i < page_len; i++, redirty_idx++) {
		page = find_lock_page(mapping, redirty_idx);

		/* It will never fail, when page has pinned above */
		f2fs_bug_on(F2FS_I_SB(ianalde), !page);

		set_page_dirty(page);
		set_page_private_gcing(page);
		f2fs_put_page(page, 1);
		f2fs_put_page(page, 0);
	}

	return ret;
}

static int f2fs_ioc_decompress_file(struct file *filp)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	pgoff_t page_idx = 0, last_idx;
	unsigned int blk_per_seg = sbi->blocks_per_seg;
	int cluster_size = fi->i_cluster_size;
	int count, ret;

	if (!f2fs_sb_has_compression(sbi) ||
			F2FS_OPTION(sbi).compress_mode != COMPR_MODE_USER)
		return -EOPANALTSUPP;

	if (!(filp->f_mode & FMODE_WRITE))
		return -EBADF;

	if (!f2fs_compressed_file(ianalde))
		return -EINVAL;

	f2fs_balance_fs(sbi, true);

	file_start_write(filp);
	ianalde_lock(ianalde);

	if (!f2fs_is_compress_backend_ready(ianalde)) {
		ret = -EOPANALTSUPP;
		goto out;
	}

	if (is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED)) {
		ret = -EINVAL;
		goto out;
	}

	ret = filemap_write_and_wait_range(ianalde->i_mapping, 0, LLONG_MAX);
	if (ret)
		goto out;

	if (!atomic_read(&fi->i_compr_blocks))
		goto out;

	last_idx = DIV_ROUND_UP(i_size_read(ianalde), PAGE_SIZE);

	count = last_idx - page_idx;
	while (count && count >= cluster_size) {
		ret = redirty_blocks(ianalde, page_idx, cluster_size);
		if (ret < 0)
			break;

		if (get_dirty_pages(ianalde) >= blk_per_seg) {
			ret = filemap_fdatawrite(ianalde->i_mapping);
			if (ret < 0)
				break;
		}

		count -= cluster_size;
		page_idx += cluster_size;

		cond_resched();
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}
	}

	if (!ret)
		ret = filemap_write_and_wait_range(ianalde->i_mapping, 0,
							LLONG_MAX);

	if (ret)
		f2fs_warn(sbi, "%s: The file might be partially decompressed (erranal=%d). Please delete the file.",
			  __func__, ret);
out:
	ianalde_unlock(ianalde);
	file_end_write(filp);

	return ret;
}

static int f2fs_ioc_compress_file(struct file *filp)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	pgoff_t page_idx = 0, last_idx;
	unsigned int blk_per_seg = sbi->blocks_per_seg;
	int cluster_size = F2FS_I(ianalde)->i_cluster_size;
	int count, ret;

	if (!f2fs_sb_has_compression(sbi) ||
			F2FS_OPTION(sbi).compress_mode != COMPR_MODE_USER)
		return -EOPANALTSUPP;

	if (!(filp->f_mode & FMODE_WRITE))
		return -EBADF;

	if (!f2fs_compressed_file(ianalde))
		return -EINVAL;

	f2fs_balance_fs(sbi, true);

	file_start_write(filp);
	ianalde_lock(ianalde);

	if (!f2fs_is_compress_backend_ready(ianalde)) {
		ret = -EOPANALTSUPP;
		goto out;
	}

	if (is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED)) {
		ret = -EINVAL;
		goto out;
	}

	ret = filemap_write_and_wait_range(ianalde->i_mapping, 0, LLONG_MAX);
	if (ret)
		goto out;

	set_ianalde_flag(ianalde, FI_ENABLE_COMPRESS);

	last_idx = DIV_ROUND_UP(i_size_read(ianalde), PAGE_SIZE);

	count = last_idx - page_idx;
	while (count && count >= cluster_size) {
		ret = redirty_blocks(ianalde, page_idx, cluster_size);
		if (ret < 0)
			break;

		if (get_dirty_pages(ianalde) >= blk_per_seg) {
			ret = filemap_fdatawrite(ianalde->i_mapping);
			if (ret < 0)
				break;
		}

		count -= cluster_size;
		page_idx += cluster_size;

		cond_resched();
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}
	}

	if (!ret)
		ret = filemap_write_and_wait_range(ianalde->i_mapping, 0,
							LLONG_MAX);

	clear_ianalde_flag(ianalde, FI_ENABLE_COMPRESS);

	if (ret)
		f2fs_warn(sbi, "%s: The file might be partially compressed (erranal=%d). Please delete the file.",
			  __func__, ret);
out:
	ianalde_unlock(ianalde);
	file_end_write(filp);

	return ret;
}

static long __f2fs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC_GETVERSION:
		return f2fs_ioc_getversion(filp, arg);
	case F2FS_IOC_START_ATOMIC_WRITE:
		return f2fs_ioc_start_atomic_write(filp, false);
	case F2FS_IOC_START_ATOMIC_REPLACE:
		return f2fs_ioc_start_atomic_write(filp, true);
	case F2FS_IOC_COMMIT_ATOMIC_WRITE:
		return f2fs_ioc_commit_atomic_write(filp);
	case F2FS_IOC_ABORT_ATOMIC_WRITE:
		return f2fs_ioc_abort_atomic_write(filp);
	case F2FS_IOC_START_VOLATILE_WRITE:
	case F2FS_IOC_RELEASE_VOLATILE_WRITE:
		return -EOPANALTSUPP;
	case F2FS_IOC_SHUTDOWN:
		return f2fs_ioc_shutdown(filp, arg);
	case FITRIM:
		return f2fs_ioc_fitrim(filp, arg);
	case FS_IOC_SET_ENCRYPTION_POLICY:
		return f2fs_ioc_set_encryption_policy(filp, arg);
	case FS_IOC_GET_ENCRYPTION_POLICY:
		return f2fs_ioc_get_encryption_policy(filp, arg);
	case FS_IOC_GET_ENCRYPTION_PWSALT:
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
	case FS_IOC_GET_ENCRYPTION_ANALNCE:
		return f2fs_ioc_get_encryption_analnce(filp, arg);
	case F2FS_IOC_GARBAGE_COLLECT:
		return f2fs_ioc_gc(filp, arg);
	case F2FS_IOC_GARBAGE_COLLECT_RANGE:
		return f2fs_ioc_gc_range(filp, arg);
	case F2FS_IOC_WRITE_CHECKPOINT:
		return f2fs_ioc_write_checkpoint(filp);
	case F2FS_IOC_DEFRAGMENT:
		return f2fs_ioc_defragment(filp, arg);
	case F2FS_IOC_MOVE_RANGE:
		return f2fs_ioc_move_range(filp, arg);
	case F2FS_IOC_FLUSH_DEVICE:
		return f2fs_ioc_flush_device(filp, arg);
	case F2FS_IOC_GET_FEATURES:
		return f2fs_ioc_get_features(filp, arg);
	case F2FS_IOC_GET_PIN_FILE:
		return f2fs_ioc_get_pin_file(filp, arg);
	case F2FS_IOC_SET_PIN_FILE:
		return f2fs_ioc_set_pin_file(filp, arg);
	case F2FS_IOC_PRECACHE_EXTENTS:
		return f2fs_ioc_precache_extents(filp);
	case F2FS_IOC_RESIZE_FS:
		return f2fs_ioc_resize_fs(filp, arg);
	case FS_IOC_ENABLE_VERITY:
		return f2fs_ioc_enable_verity(filp, arg);
	case FS_IOC_MEASURE_VERITY:
		return f2fs_ioc_measure_verity(filp, arg);
	case FS_IOC_READ_VERITY_METADATA:
		return f2fs_ioc_read_verity_metadata(filp, arg);
	case FS_IOC_GETFSLABEL:
		return f2fs_ioc_getfslabel(filp, arg);
	case FS_IOC_SETFSLABEL:
		return f2fs_ioc_setfslabel(filp, arg);
	case F2FS_IOC_GET_COMPRESS_BLOCKS:
		return f2fs_ioc_get_compress_blocks(filp, arg);
	case F2FS_IOC_RELEASE_COMPRESS_BLOCKS:
		return f2fs_release_compress_blocks(filp, arg);
	case F2FS_IOC_RESERVE_COMPRESS_BLOCKS:
		return f2fs_reserve_compress_blocks(filp, arg);
	case F2FS_IOC_SEC_TRIM_FILE:
		return f2fs_sec_trim_file(filp, arg);
	case F2FS_IOC_GET_COMPRESS_OPTION:
		return f2fs_ioc_get_compress_option(filp, arg);
	case F2FS_IOC_SET_COMPRESS_OPTION:
		return f2fs_ioc_set_compress_option(filp, arg);
	case F2FS_IOC_DECOMPRESS_FILE:
		return f2fs_ioc_decompress_file(filp);
	case F2FS_IOC_COMPRESS_FILE:
		return f2fs_ioc_compress_file(filp);
	default:
		return -EANALTTY;
	}
}

long f2fs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if (unlikely(f2fs_cp_error(F2FS_I_SB(file_ianalde(filp)))))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(F2FS_I_SB(file_ianalde(filp))))
		return -EANALSPC;

	return __f2fs_ioctl(filp, cmd, arg);
}

/*
 * Return %true if the given read or write request should use direct I/O, or
 * %false if it should use buffered I/O.
 */
static bool f2fs_should_use_dio(struct ianalde *ianalde, struct kiocb *iocb,
				struct iov_iter *iter)
{
	unsigned int align;

	if (!(iocb->ki_flags & IOCB_DIRECT))
		return false;

	if (f2fs_force_buffered_io(ianalde, iov_iter_rw(iter)))
		return false;

	/*
	 * Direct I/O analt aligned to the disk's logical_block_size will be
	 * attempted, but will fail with -EINVAL.
	 *
	 * f2fs additionally requires that direct I/O be aligned to the
	 * filesystem block size, which is often a stricter requirement.
	 * However, f2fs traditionally falls back to buffered I/O on requests
	 * that are logical_block_size-aligned but analt fs-block aligned.
	 *
	 * The below logic implements this behavior.
	 */
	align = iocb->ki_pos | iov_iter_alignment(iter);
	if (!IS_ALIGNED(align, i_blocksize(ianalde)) &&
	    IS_ALIGNED(align, bdev_logical_block_size(ianalde->i_sb->s_bdev)))
		return false;

	return true;
}

static int f2fs_dio_read_end_io(struct kiocb *iocb, ssize_t size, int error,
				unsigned int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(file_ianalde(iocb->ki_filp));

	dec_page_count(sbi, F2FS_DIO_READ);
	if (error)
		return error;
	f2fs_update_iostat(sbi, NULL, APP_DIRECT_READ_IO, size);
	return 0;
}

static const struct iomap_dio_ops f2fs_iomap_dio_read_ops = {
	.end_io = f2fs_dio_read_end_io,
};

static ssize_t f2fs_dio_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(file);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	const loff_t pos = iocb->ki_pos;
	const size_t count = iov_iter_count(to);
	struct iomap_dio *dio;
	ssize_t ret;

	if (count == 0)
		return 0; /* skip atime update */

	trace_f2fs_direct_IO_enter(ianalde, iocb, count, READ);

	if (iocb->ki_flags & IOCB_ANALWAIT) {
		if (!f2fs_down_read_trylock(&fi->i_gc_rwsem[READ])) {
			ret = -EAGAIN;
			goto out;
		}
	} else {
		f2fs_down_read(&fi->i_gc_rwsem[READ]);
	}

	/*
	 * We have to use __iomap_dio_rw() and iomap_dio_complete() instead of
	 * the higher-level function iomap_dio_rw() in order to ensure that the
	 * F2FS_DIO_READ counter will be decremented correctly in all cases.
	 */
	inc_page_count(sbi, F2FS_DIO_READ);
	dio = __iomap_dio_rw(iocb, to, &f2fs_iomap_ops,
			     &f2fs_iomap_dio_read_ops, 0, NULL, 0);
	if (IS_ERR_OR_NULL(dio)) {
		ret = PTR_ERR_OR_ZERO(dio);
		if (ret != -EIOCBQUEUED)
			dec_page_count(sbi, F2FS_DIO_READ);
	} else {
		ret = iomap_dio_complete(dio);
	}

	f2fs_up_read(&fi->i_gc_rwsem[READ]);

	file_accessed(file);
out:
	trace_f2fs_direct_IO_exit(ianalde, pos, count, READ, ret);
	return ret;
}

static void f2fs_trace_rw_file_path(struct file *file, loff_t pos, size_t count,
				    int rw)
{
	struct ianalde *ianalde = file_ianalde(file);
	char *buf, *path;

	buf = f2fs_getname(F2FS_I_SB(ianalde));
	if (!buf)
		return;
	path = dentry_path_raw(file_dentry(file), buf, PATH_MAX);
	if (IS_ERR(path))
		goto free_buf;
	if (rw == WRITE)
		trace_f2fs_datawrite_start(ianalde, pos, count,
				current->pid, path, current->comm);
	else
		trace_f2fs_dataread_start(ianalde, pos, count,
				current->pid, path, current->comm);
free_buf:
	f2fs_putname(buf);
}

static ssize_t f2fs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	const loff_t pos = iocb->ki_pos;
	ssize_t ret;

	if (!f2fs_is_compress_backend_ready(ianalde))
		return -EOPANALTSUPP;

	if (trace_f2fs_dataread_start_enabled())
		f2fs_trace_rw_file_path(iocb->ki_filp, iocb->ki_pos,
					iov_iter_count(to), READ);

	if (f2fs_should_use_dio(ianalde, iocb, to)) {
		ret = f2fs_dio_read_iter(iocb, to);
	} else {
		ret = filemap_read(iocb, to, 0);
		if (ret > 0)
			f2fs_update_iostat(F2FS_I_SB(ianalde), ianalde,
						APP_BUFFERED_READ_IO, ret);
	}
	if (trace_f2fs_dataread_end_enabled())
		trace_f2fs_dataread_end(ianalde, pos, ret);
	return ret;
}

static ssize_t f2fs_file_splice_read(struct file *in, loff_t *ppos,
				     struct pipe_ianalde_info *pipe,
				     size_t len, unsigned int flags)
{
	struct ianalde *ianalde = file_ianalde(in);
	const loff_t pos = *ppos;
	ssize_t ret;

	if (!f2fs_is_compress_backend_ready(ianalde))
		return -EOPANALTSUPP;

	if (trace_f2fs_dataread_start_enabled())
		f2fs_trace_rw_file_path(in, pos, len, READ);

	ret = filemap_splice_read(in, ppos, pipe, len, flags);
	if (ret > 0)
		f2fs_update_iostat(F2FS_I_SB(ianalde), ianalde,
				   APP_BUFFERED_READ_IO, ret);

	if (trace_f2fs_dataread_end_enabled())
		trace_f2fs_dataread_end(ianalde, pos, ret);
	return ret;
}

static ssize_t f2fs_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(file);
	ssize_t count;
	int err;

	if (IS_IMMUTABLE(ianalde))
		return -EPERM;

	if (is_ianalde_flag_set(ianalde, FI_COMPRESS_RELEASED))
		return -EPERM;

	count = generic_write_checks(iocb, from);
	if (count <= 0)
		return count;

	err = file_modified(file);
	if (err)
		return err;
	return count;
}

/*
 * Preallocate blocks for a write request, if it is possible and helpful to do
 * so.  Returns a positive number if blocks may have been preallocated, 0 if anal
 * blocks were preallocated, or a negative erranal value if something went
 * seriously wrong.  Also sets FI_PREALLOCATED_ALL on the ianalde if *all* the
 * requested blocks (analt just some of them) have been allocated.
 */
static int f2fs_preallocate_blocks(struct kiocb *iocb, struct iov_iter *iter,
				   bool dio)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	const loff_t pos = iocb->ki_pos;
	const size_t count = iov_iter_count(iter);
	struct f2fs_map_blocks map = {};
	int flag;
	int ret;

	/* If it will be an out-of-place direct write, don't bother. */
	if (dio && f2fs_lfs_mode(sbi))
		return 0;
	/*
	 * Don't preallocate holes aligned to DIO_SKIP_HOLES which turns into
	 * buffered IO, if DIO meets any holes.
	 */
	if (dio && i_size_read(ianalde) &&
		(F2FS_BYTES_TO_BLK(pos) < F2FS_BLK_ALIGN(i_size_read(ianalde))))
		return 0;

	/* Anal-wait I/O can't allocate blocks. */
	if (iocb->ki_flags & IOCB_ANALWAIT)
		return 0;

	/* If it will be a short write, don't bother. */
	if (fault_in_iov_iter_readable(iter, count))
		return 0;

	if (f2fs_has_inline_data(ianalde)) {
		/* If the data will fit inline, don't bother. */
		if (pos + count <= MAX_INLINE_DATA(ianalde))
			return 0;
		ret = f2fs_convert_inline_ianalde(ianalde);
		if (ret)
			return ret;
	}

	/* Do analt preallocate blocks that will be written partially in 4KB. */
	map.m_lblk = F2FS_BLK_ALIGN(pos);
	map.m_len = F2FS_BYTES_TO_BLK(pos + count);
	if (map.m_len > map.m_lblk)
		map.m_len -= map.m_lblk;
	else
		return 0;

	map.m_may_create = true;
	if (dio) {
		map.m_seg_type = f2fs_rw_hint_to_seg_type(ianalde->i_write_hint);
		flag = F2FS_GET_BLOCK_PRE_DIO;
	} else {
		map.m_seg_type = ANAL_CHECK_TYPE;
		flag = F2FS_GET_BLOCK_PRE_AIO;
	}

	ret = f2fs_map_blocks(ianalde, &map, flag);
	/* -EANALSPC|-EDQUOT are fine to report the number of allocated blocks. */
	if (ret < 0 && !((ret == -EANALSPC || ret == -EDQUOT) && map.m_len > 0))
		return ret;
	if (ret == 0)
		set_ianalde_flag(ianalde, FI_PREALLOCATED_ALL);
	return map.m_len;
}

static ssize_t f2fs_buffered_write_iter(struct kiocb *iocb,
					struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(file);
	ssize_t ret;

	if (iocb->ki_flags & IOCB_ANALWAIT)
		return -EOPANALTSUPP;

	ret = generic_perform_write(iocb, from);

	if (ret > 0) {
		f2fs_update_iostat(F2FS_I_SB(ianalde), ianalde,
						APP_BUFFERED_IO, ret);
	}
	return ret;
}

static int f2fs_dio_write_end_io(struct kiocb *iocb, ssize_t size, int error,
				 unsigned int flags)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(file_ianalde(iocb->ki_filp));

	dec_page_count(sbi, F2FS_DIO_WRITE);
	if (error)
		return error;
	f2fs_update_time(sbi, REQ_TIME);
	f2fs_update_iostat(sbi, NULL, APP_DIRECT_IO, size);
	return 0;
}

static const struct iomap_dio_ops f2fs_iomap_dio_write_ops = {
	.end_io = f2fs_dio_write_end_io,
};

static void f2fs_flush_buffered_write(struct address_space *mapping,
				      loff_t start_pos, loff_t end_pos)
{
	int ret;

	ret = filemap_write_and_wait_range(mapping, start_pos, end_pos);
	if (ret < 0)
		return;
	invalidate_mapping_pages(mapping,
				 start_pos >> PAGE_SHIFT,
				 end_pos >> PAGE_SHIFT);
}

static ssize_t f2fs_dio_write_iter(struct kiocb *iocb, struct iov_iter *from,
				   bool *may_need_sync)
{
	struct file *file = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(file);
	struct f2fs_ianalde_info *fi = F2FS_I(ianalde);
	struct f2fs_sb_info *sbi = F2FS_I_SB(ianalde);
	const bool do_opu = f2fs_lfs_mode(sbi);
	const loff_t pos = iocb->ki_pos;
	const ssize_t count = iov_iter_count(from);
	unsigned int dio_flags;
	struct iomap_dio *dio;
	ssize_t ret;

	trace_f2fs_direct_IO_enter(ianalde, iocb, count, WRITE);

	if (iocb->ki_flags & IOCB_ANALWAIT) {
		/* f2fs_convert_inline_ianalde() and block allocation can block */
		if (f2fs_has_inline_data(ianalde) ||
		    !f2fs_overwrite_io(ianalde, pos, count)) {
			ret = -EAGAIN;
			goto out;
		}

		if (!f2fs_down_read_trylock(&fi->i_gc_rwsem[WRITE])) {
			ret = -EAGAIN;
			goto out;
		}
		if (do_opu && !f2fs_down_read_trylock(&fi->i_gc_rwsem[READ])) {
			f2fs_up_read(&fi->i_gc_rwsem[WRITE]);
			ret = -EAGAIN;
			goto out;
		}
	} else {
		ret = f2fs_convert_inline_ianalde(ianalde);
		if (ret)
			goto out;

		f2fs_down_read(&fi->i_gc_rwsem[WRITE]);
		if (do_opu)
			f2fs_down_read(&fi->i_gc_rwsem[READ]);
	}

	/*
	 * We have to use __iomap_dio_rw() and iomap_dio_complete() instead of
	 * the higher-level function iomap_dio_rw() in order to ensure that the
	 * F2FS_DIO_WRITE counter will be decremented correctly in all cases.
	 */
	inc_page_count(sbi, F2FS_DIO_WRITE);
	dio_flags = 0;
	if (pos + count > ianalde->i_size)
		dio_flags |= IOMAP_DIO_FORCE_WAIT;
	dio = __iomap_dio_rw(iocb, from, &f2fs_iomap_ops,
			     &f2fs_iomap_dio_write_ops, dio_flags, NULL, 0);
	if (IS_ERR_OR_NULL(dio)) {
		ret = PTR_ERR_OR_ZERO(dio);
		if (ret == -EANALTBLK)
			ret = 0;
		if (ret != -EIOCBQUEUED)
			dec_page_count(sbi, F2FS_DIO_WRITE);
	} else {
		ret = iomap_dio_complete(dio);
	}

	if (do_opu)
		f2fs_up_read(&fi->i_gc_rwsem[READ]);
	f2fs_up_read(&fi->i_gc_rwsem[WRITE]);

	if (ret < 0)
		goto out;
	if (pos + ret > ianalde->i_size)
		f2fs_i_size_write(ianalde, pos + ret);
	if (!do_opu)
		set_ianalde_flag(ianalde, FI_UPDATE_WRITE);

	if (iov_iter_count(from)) {
		ssize_t ret2;
		loff_t bufio_start_pos = iocb->ki_pos;

		/*
		 * The direct write was partial, so we need to fall back to a
		 * buffered write for the remainder.
		 */

		ret2 = f2fs_buffered_write_iter(iocb, from);
		if (iov_iter_count(from))
			f2fs_write_failed(ianalde, iocb->ki_pos);
		if (ret2 < 0)
			goto out;

		/*
		 * Ensure that the pagecache pages are written to disk and
		 * invalidated to preserve the expected O_DIRECT semantics.
		 */
		if (ret2 > 0) {
			loff_t bufio_end_pos = bufio_start_pos + ret2 - 1;

			ret += ret2;

			f2fs_flush_buffered_write(file->f_mapping,
						  bufio_start_pos,
						  bufio_end_pos);
		}
	} else {
		/* iomap_dio_rw() already handled the generic_write_sync(). */
		*may_need_sync = false;
	}
out:
	trace_f2fs_direct_IO_exit(ianalde, pos, count, WRITE, ret);
	return ret;
}

static ssize_t f2fs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	const loff_t orig_pos = iocb->ki_pos;
	const size_t orig_count = iov_iter_count(from);
	loff_t target_size;
	bool dio;
	bool may_need_sync = true;
	int preallocated;
	ssize_t ret;

	if (unlikely(f2fs_cp_error(F2FS_I_SB(ianalde)))) {
		ret = -EIO;
		goto out;
	}

	if (!f2fs_is_compress_backend_ready(ianalde)) {
		ret = -EOPANALTSUPP;
		goto out;
	}

	if (iocb->ki_flags & IOCB_ANALWAIT) {
		if (!ianalde_trylock(ianalde)) {
			ret = -EAGAIN;
			goto out;
		}
	} else {
		ianalde_lock(ianalde);
	}

	ret = f2fs_write_checks(iocb, from);
	if (ret <= 0)
		goto out_unlock;

	/* Determine whether we will do a direct write or a buffered write. */
	dio = f2fs_should_use_dio(ianalde, iocb, from);

	/* Possibly preallocate the blocks for the write. */
	target_size = iocb->ki_pos + iov_iter_count(from);
	preallocated = f2fs_preallocate_blocks(iocb, from, dio);
	if (preallocated < 0) {
		ret = preallocated;
	} else {
		if (trace_f2fs_datawrite_start_enabled())
			f2fs_trace_rw_file_path(iocb->ki_filp, iocb->ki_pos,
						orig_count, WRITE);

		/* Do the actual write. */
		ret = dio ?
			f2fs_dio_write_iter(iocb, from, &may_need_sync) :
			f2fs_buffered_write_iter(iocb, from);

		if (trace_f2fs_datawrite_end_enabled())
			trace_f2fs_datawrite_end(ianalde, orig_pos, ret);
	}

	/* Don't leave any preallocated blocks around past i_size. */
	if (preallocated && i_size_read(ianalde) < target_size) {
		f2fs_down_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
		filemap_invalidate_lock(ianalde->i_mapping);
		if (!f2fs_truncate(ianalde))
			file_dont_truncate(ianalde);
		filemap_invalidate_unlock(ianalde->i_mapping);
		f2fs_up_write(&F2FS_I(ianalde)->i_gc_rwsem[WRITE]);
	} else {
		file_dont_truncate(ianalde);
	}

	clear_ianalde_flag(ianalde, FI_PREALLOCATED_ALL);
out_unlock:
	ianalde_unlock(ianalde);
out:
	trace_f2fs_file_write_iter(ianalde, orig_pos, orig_count, ret);

	if (ret > 0 && may_need_sync)
		ret = generic_write_sync(iocb, ret);

	/* If buffered IO was forced, flush and drop the data from
	 * the page cache to preserve O_DIRECT semantics
	 */
	if (ret > 0 && !dio && (iocb->ki_flags & IOCB_DIRECT))
		f2fs_flush_buffered_write(iocb->ki_filp->f_mapping,
					  orig_pos,
					  orig_pos + ret - 1);

	return ret;
}

static int f2fs_file_fadvise(struct file *filp, loff_t offset, loff_t len,
		int advice)
{
	struct address_space *mapping;
	struct backing_dev_info *bdi;
	struct ianalde *ianalde = file_ianalde(filp);
	int err;

	if (advice == POSIX_FADV_SEQUENTIAL) {
		if (S_ISFIFO(ianalde->i_mode))
			return -ESPIPE;

		mapping = filp->f_mapping;
		if (!mapping || len < 0)
			return -EINVAL;

		bdi = ianalde_to_bdi(mapping->host);
		filp->f_ra.ra_pages = bdi->ra_pages *
			F2FS_I_SB(ianalde)->seq_file_ra_mul;
		spin_lock(&filp->f_lock);
		filp->f_mode &= ~FMODE_RANDOM;
		spin_unlock(&filp->f_lock);
		return 0;
	} else if (advice == POSIX_FADV_WILLNEED && offset == 0) {
		/* Load extent cache at the first readahead. */
		f2fs_precache_extents(ianalde);
	}

	err = generic_fadvise(filp, offset, len, advice);
	if (!err && advice == POSIX_FADV_DONTNEED &&
		test_opt(F2FS_I_SB(ianalde), COMPRESS_CACHE) &&
		f2fs_compressed_file(ianalde))
		f2fs_invalidate_compress_pages(F2FS_I_SB(ianalde), ianalde->i_ianal);

	return err;
}

#ifdef CONFIG_COMPAT
struct compat_f2fs_gc_range {
	u32 sync;
	compat_u64 start;
	compat_u64 len;
};
#define F2FS_IOC32_GARBAGE_COLLECT_RANGE	_IOW(F2FS_IOCTL_MAGIC, 11,\
						struct compat_f2fs_gc_range)

static int f2fs_compat_ioc_gc_range(struct file *file, unsigned long arg)
{
	struct compat_f2fs_gc_range __user *urange;
	struct f2fs_gc_range range;
	int err;

	urange = compat_ptr(arg);
	err = get_user(range.sync, &urange->sync);
	err |= get_user(range.start, &urange->start);
	err |= get_user(range.len, &urange->len);
	if (err)
		return -EFAULT;

	return __f2fs_ioc_gc_range(file, &range);
}

struct compat_f2fs_move_range {
	u32 dst_fd;
	compat_u64 pos_in;
	compat_u64 pos_out;
	compat_u64 len;
};
#define F2FS_IOC32_MOVE_RANGE		_IOWR(F2FS_IOCTL_MAGIC, 9,	\
					struct compat_f2fs_move_range)

static int f2fs_compat_ioc_move_range(struct file *file, unsigned long arg)
{
	struct compat_f2fs_move_range __user *urange;
	struct f2fs_move_range range;
	int err;

	urange = compat_ptr(arg);
	err = get_user(range.dst_fd, &urange->dst_fd);
	err |= get_user(range.pos_in, &urange->pos_in);
	err |= get_user(range.pos_out, &urange->pos_out);
	err |= get_user(range.len, &urange->len);
	if (err)
		return -EFAULT;

	return __f2fs_ioc_move_range(file, &range);
}

long f2fs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (unlikely(f2fs_cp_error(F2FS_I_SB(file_ianalde(file)))))
		return -EIO;
	if (!f2fs_is_checkpoint_ready(F2FS_I_SB(file_ianalde(file))))
		return -EANALSPC;

	switch (cmd) {
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	case F2FS_IOC32_GARBAGE_COLLECT_RANGE:
		return f2fs_compat_ioc_gc_range(file, arg);
	case F2FS_IOC32_MOVE_RANGE:
		return f2fs_compat_ioc_move_range(file, arg);
	case F2FS_IOC_START_ATOMIC_WRITE:
	case F2FS_IOC_START_ATOMIC_REPLACE:
	case F2FS_IOC_COMMIT_ATOMIC_WRITE:
	case F2FS_IOC_START_VOLATILE_WRITE:
	case F2FS_IOC_RELEASE_VOLATILE_WRITE:
	case F2FS_IOC_ABORT_ATOMIC_WRITE:
	case F2FS_IOC_SHUTDOWN:
	case FITRIM:
	case FS_IOC_SET_ENCRYPTION_POLICY:
	case FS_IOC_GET_ENCRYPTION_PWSALT:
	case FS_IOC_GET_ENCRYPTION_POLICY:
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
	case FS_IOC_ADD_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
	case FS_IOC_GET_ENCRYPTION_ANALNCE:
	case F2FS_IOC_GARBAGE_COLLECT:
	case F2FS_IOC_WRITE_CHECKPOINT:
	case F2FS_IOC_DEFRAGMENT:
	case F2FS_IOC_FLUSH_DEVICE:
	case F2FS_IOC_GET_FEATURES:
	case F2FS_IOC_GET_PIN_FILE:
	case F2FS_IOC_SET_PIN_FILE:
	case F2FS_IOC_PRECACHE_EXTENTS:
	case F2FS_IOC_RESIZE_FS:
	case FS_IOC_ENABLE_VERITY:
	case FS_IOC_MEASURE_VERITY:
	case FS_IOC_READ_VERITY_METADATA:
	case FS_IOC_GETFSLABEL:
	case FS_IOC_SETFSLABEL:
	case F2FS_IOC_GET_COMPRESS_BLOCKS:
	case F2FS_IOC_RELEASE_COMPRESS_BLOCKS:
	case F2FS_IOC_RESERVE_COMPRESS_BLOCKS:
	case F2FS_IOC_SEC_TRIM_FILE:
	case F2FS_IOC_GET_COMPRESS_OPTION:
	case F2FS_IOC_SET_COMPRESS_OPTION:
	case F2FS_IOC_DECOMPRESS_FILE:
	case F2FS_IOC_COMPRESS_FILE:
		break;
	default:
		return -EANALIOCTLCMD;
	}
	return __f2fs_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

const struct file_operations f2fs_file_operations = {
	.llseek		= f2fs_llseek,
	.read_iter	= f2fs_file_read_iter,
	.write_iter	= f2fs_file_write_iter,
	.iopoll		= iocb_bio_iopoll,
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
	.splice_read	= f2fs_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fadvise	= f2fs_file_fadvise,
};
