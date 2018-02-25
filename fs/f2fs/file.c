/*
 * fs/f2fs/file.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"
#include "acl.h"
#include "gc.h"
#include "trace.h"
#include <trace/events/f2fs.h>

static int f2fs_filemap_fault(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	int err;

	down_read(&F2FS_I(inode)->i_mmap_sem);
	err = filemap_fault(vmf);
	up_read(&F2FS_I(inode)->i_mmap_sem);

	return err;
}

static int f2fs_vm_page_mkwrite(struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct dnode_of_data dn;
	int err;

	sb_start_pagefault(inode->i_sb);

	f2fs_bug_on(sbi, f2fs_has_inline_data(inode));

	/* block allocation */
	f2fs_lock_op(sbi);
	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = f2fs_reserve_block(&dn, page->index);
	if (err) {
		f2fs_unlock_op(sbi);
		goto out;
	}
	f2fs_put_dnode(&dn);
	f2fs_unlock_op(sbi);

	f2fs_balance_fs(sbi, dn.node_changed);

	file_update_time(vmf->vma->vm_file);
	down_read(&F2FS_I(inode)->i_mmap_sem);
	lock_page(page);
	if (unlikely(page->mapping != inode->i_mapping ||
			page_offset(page) > i_size_read(inode) ||
			!PageUptodate(page))) {
		unlock_page(page);
		err = -EFAULT;
		goto out_sem;
	}

	/*
	 * check to see if the page is mapped already (no holes)
	 */
	if (PageMappedToDisk(page))
		goto mapped;

	/* page is wholly or partially inside EOF */
	if (((loff_t)(page->index + 1) << PAGE_SHIFT) >
						i_size_read(inode)) {
		unsigned offset;
		offset = i_size_read(inode) & ~PAGE_MASK;
		zero_user_segment(page, offset, PAGE_SIZE);
	}
	set_page_dirty(page);
	if (!PageUptodate(page))
		SetPageUptodate(page);

	f2fs_update_iostat(sbi, APP_MAPPED_IO, F2FS_BLKSIZE);

	trace_f2fs_vm_page_mkwrite(page, DATA);
mapped:
	/* fill the page */
	f2fs_wait_on_page_writeback(page, DATA, false);

	/* wait for GCed encrypted page writeback */
	if (f2fs_encrypted_file(inode))
		f2fs_wait_on_block_writeback(sbi, dn.data_blkaddr);

out_sem:
	up_read(&F2FS_I(inode)->i_mmap_sem);
out:
	sb_end_pagefault(inode->i_sb);
	f2fs_update_time(sbi, REQ_TIME);
	return block_page_mkwrite_return(err);
}

static const struct vm_operations_struct f2fs_file_vm_ops = {
	.fault		= f2fs_filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= f2fs_vm_page_mkwrite,
};

static int get_parent_ino(struct inode *inode, nid_t *pino)
{
	struct dentry *dentry;

	inode = igrab(inode);
	dentry = d_find_any_alias(inode);
	iput(inode);
	if (!dentry)
		return 0;

	*pino = parent_ino(dentry);
	dput(dentry);
	return 1;
}

static inline bool need_do_checkpoint(struct inode *inode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	bool need_cp = false;

	if (!S_ISREG(inode->i_mode) || inode->i_nlink != 1)
		need_cp = true;
	else if (is_sbi_flag_set(sbi, SBI_NEED_CP))
		need_cp = true;
	else if (file_wrong_pino(inode))
		need_cp = true;
	else if (!space_for_roll_forward(sbi))
		need_cp = true;
	else if (!is_checkpointed_node(sbi, F2FS_I(inode)->i_pino))
		need_cp = true;
	else if (test_opt(sbi, FASTBOOT))
		need_cp = true;
	else if (sbi->active_logs == 2)
		need_cp = true;

	return need_cp;
}

static bool need_inode_page_update(struct f2fs_sb_info *sbi, nid_t ino)
{
	struct page *i = find_get_page(NODE_MAPPING(sbi), ino);
	bool ret = false;
	/* But we need to avoid that there are some inode updates */
	if ((i && PageDirty(i)) || need_inode_block_update(sbi, ino))
		ret = true;
	f2fs_put_page(i, 0);
	return ret;
}

static void try_to_fix_pino(struct inode *inode)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	nid_t pino;

	down_write(&fi->i_sem);
	if (file_wrong_pino(inode) && inode->i_nlink == 1 &&
			get_parent_ino(inode, &pino)) {
		f2fs_i_pino_write(inode, pino);
		file_got_pino(inode);
	}
	up_write(&fi->i_sem);
}

static int f2fs_do_sync_file(struct file *file, loff_t start, loff_t end,
						int datasync, bool atomic)
{
	struct inode *inode = file->f_mapping->host;
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	nid_t ino = inode->i_ino;
	int ret = 0;
	bool need_cp = false;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.for_reclaim = 0,
	};

	if (unlikely(f2fs_readonly(inode->i_sb)))
		return 0;

	trace_f2fs_sync_file_enter(inode);

	/* if fdatasync is triggered, let's do in-place-update */
	if (datasync || get_dirty_pages(inode) <= SM_I(sbi)->min_fsync_blocks)
		set_inode_flag(inode, FI_NEED_IPU);
	ret = file_write_and_wait_range(file, start, end);
	clear_inode_flag(inode, FI_NEED_IPU);

	if (ret) {
		trace_f2fs_sync_file_exit(inode, need_cp, datasync, ret);
		return ret;
	}

	/* if the inode is dirty, let's recover all the time */
	if (!f2fs_skip_inode_update(inode, datasync)) {
		f2fs_write_inode(inode, NULL);
		goto go_write;
	}

	/*
	 * if there is no written data, don't waste time to write recovery info.
	 */
	if (!is_inode_flag_set(inode, FI_APPEND_WRITE) &&
			!exist_written_data(sbi, ino, APPEND_INO)) {

		/* it may call write_inode just prior to fsync */
		if (need_inode_page_update(sbi, ino))
			goto go_write;

		if (is_inode_flag_set(inode, FI_UPDATE_WRITE) ||
				exist_written_data(sbi, ino, UPDATE_INO))
			goto flush_out;
		goto out;
	}
go_write:
	/*
	 * Both of fdatasync() and fsync() are able to be recovered from
	 * sudden-power-off.
	 */
	down_read(&F2FS_I(inode)->i_sem);
	need_cp = need_do_checkpoint(inode);
	up_read(&F2FS_I(inode)->i_sem);

	if (need_cp) {
		/* all the dirty node pages should be flushed for POR */
		ret = f2fs_sync_fs(inode->i_sb, 1);

		/*
		 * We've secured consistency through sync_fs. Following pino
		 * will be used only for fsynced inodes after checkpoint.
		 */
		try_to_fix_pino(inode);
		clear_inode_flag(inode, FI_APPEND_WRITE);
		clear_inode_flag(inode, FI_UPDATE_WRITE);
		goto out;
	}
sync_nodes:
	ret = fsync_node_pages(sbi, inode, &wbc, atomic);
	if (ret)
		goto out;

	/* if cp_error was enabled, we should avoid infinite loop */
	if (unlikely(f2fs_cp_error(sbi))) {
		ret = -EIO;
		goto out;
	}

	if (need_inode_block_update(sbi, ino)) {
		f2fs_mark_inode_dirty_sync(inode, true);
		f2fs_write_inode(inode, NULL);
		goto sync_nodes;
	}

	/*
	 * If it's atomic_write, it's just fine to keep write ordering. So
	 * here we don't need to wait for node write completion, since we use
	 * node chain which serializes node blocks. If one of node writes are
	 * reordered, we can see simply broken chain, resulting in stopping
	 * roll-forward recovery. It means we'll recover all or none node blocks
	 * given fsync mark.
	 */
	if (!atomic) {
		ret = wait_on_node_pages_writeback(sbi, ino);
		if (ret)
			goto out;
	}

	/* once recovery info is written, don't need to tack this */
	remove_ino_entry(sbi, ino, APPEND_INO);
	clear_inode_flag(inode, FI_APPEND_WRITE);
flush_out:
	remove_ino_entry(sbi, ino, UPDATE_INO);
	clear_inode_flag(inode, FI_UPDATE_WRITE);
	if (!atomic)
		ret = f2fs_issue_flush(sbi);
	f2fs_update_time(sbi, REQ_TIME);
out:
	trace_f2fs_sync_file_exit(inode, need_cp, datasync, ret);
	f2fs_trace_ios(NULL, 1);
	return ret;
}

int f2fs_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	return f2fs_do_sync_file(file, start, end, datasync, false);
}

static pgoff_t __get_first_dirty_index(struct address_space *mapping,
						pgoff_t pgofs, int whence)
{
	struct pagevec pvec;
	int nr_pages;

	if (whence != SEEK_DATA)
		return 0;

	/* find first dirty page index */
	pagevec_init(&pvec, 0);
	nr_pages = pagevec_lookup_tag(&pvec, mapping, &pgofs,
					PAGECACHE_TAG_DIRTY, 1);
	pgofs = nr_pages ? pvec.pages[0]->index : ULONG_MAX;
	pagevec_release(&pvec);
	return pgofs;
}

static bool __found_offset(block_t blkaddr, pgoff_t dirty, pgoff_t pgofs,
							int whence)
{
	switch (whence) {
	case SEEK_DATA:
		if ((blkaddr == NEW_ADDR && dirty == pgofs) ||
			(blkaddr != NEW_ADDR && blkaddr != NULL_ADDR))
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
	struct inode *inode = file->f_mapping->host;
	loff_t maxbytes = inode->i_sb->s_maxbytes;
	struct dnode_of_data dn;
	pgoff_t pgofs, end_offset, dirty;
	loff_t data_ofs = offset;
	loff_t isize;
	int err = 0;

	inode_lock(inode);

	isize = i_size_read(inode);
	if (offset >= isize)
		goto fail;

	/* handle inline data case */
	if (f2fs_has_inline_data(inode) || f2fs_has_inline_dentry(inode)) {
		if (whence == SEEK_HOLE)
			data_ofs = isize;
		goto found;
	}

	pgofs = (pgoff_t)(offset >> PAGE_SHIFT);

	dirty = __get_first_dirty_index(inode->i_mapping, pgofs, whence);

	for (; data_ofs < isize; data_ofs = (loff_t)pgofs << PAGE_SHIFT) {
		set_new_dnode(&dn, inode, NULL, NULL, 0);
		err = get_dnode_of_data(&dn, pgofs, LOOKUP_NODE);
		if (err && err != -ENOENT) {
			goto fail;
		} else if (err == -ENOENT) {
			/* direct node does not exists */
			if (whence == SEEK_DATA) {
				pgofs = get_next_page_offset(&dn, pgofs);
				continue;
			} else {
				goto found;
			}
		}

		end_offset = ADDRS_PER_PAGE(dn.node_page, inode);

		/* find data/hole in dnode block */
		for (; dn.ofs_in_node < end_offset;
				dn.ofs_in_node++, pgofs++,
				data_ofs = (loff_t)pgofs << PAGE_SHIFT) {
			block_t blkaddr;
			blkaddr = datablock_addr(dn.inode,
					dn.node_page, dn.ofs_in_node);

			if (__found_offset(blkaddr, dirty, pgofs, whence)) {
				f2fs_put_dnode(&dn);
				goto found;
			}
		}
		f2fs_put_dnode(&dn);
	}

	if (whence == SEEK_DATA)
		goto fail;
found:
	if (whence == SEEK_HOLE && data_ofs > isize)
		data_ofs = isize;
	inode_unlock(inode);
	return vfs_setpos(file, data_ofs, maxbytes);
fail:
	inode_unlock(inode);
	return -ENXIO;
}

static loff_t f2fs_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	loff_t maxbytes = inode->i_sb->s_maxbytes;

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		return generic_file_llseek_size(file, offset, whence,
						maxbytes, i_size_read(inode));
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
	struct inode *inode = file_inode(file);
	int err;

	/* we don't need to use inline_data strictly */
	err = f2fs_convert_inline_inode(inode);
	if (err)
		return err;

	file_accessed(file);
	vma->vm_ops = &f2fs_file_vm_ops;
	return 0;
}

static int f2fs_file_open(struct inode *inode, struct file *filp)
{
	struct dentry *dir;

	if (f2fs_encrypted_inode(inode)) {
		int ret = fscrypt_get_encryption_info(inode);
		if (ret)
			return -EACCES;
		if (!fscrypt_has_encryption_key(inode))
			return -ENOKEY;
	}
	dir = dget_parent(file_dentry(filp));
	if (f2fs_encrypted_inode(d_inode(dir)) &&
			!fscrypt_has_permitted_context(d_inode(dir), inode)) {
		dput(dir);
		return -EPERM;
	}
	dput(dir);
	return dquot_file_open(inode, filp);
}

int truncate_data_blocks_range(struct dnode_of_data *dn, int count)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	struct f2fs_node *raw_node;
	int nr_free = 0, ofs = dn->ofs_in_node, len = count;
	__le32 *addr;
	int base = 0;

	if (IS_INODE(dn->node_page) && f2fs_has_extra_attr(dn->inode))
		base = get_extra_isize(dn->inode);

	raw_node = F2FS_NODE(dn->node_page);
	addr = blkaddr_in_node(raw_node) + base + ofs;

	for (; count > 0; count--, addr++, dn->ofs_in_node++) {
		block_t blkaddr = le32_to_cpu(*addr);
		if (blkaddr == NULL_ADDR)
			continue;

		dn->data_blkaddr = NULL_ADDR;
		set_data_blkaddr(dn);
		invalidate_blocks(sbi, blkaddr);
		if (dn->ofs_in_node == 0 && IS_INODE(dn->node_page))
			clear_inode_flag(dn->inode, FI_FIRST_BLOCK_WRITTEN);
		nr_free++;
	}

	if (nr_free) {
		pgoff_t fofs;
		/*
		 * once we invalidate valid blkaddr in range [ofs, ofs + count],
		 * we will invalidate all blkaddr in the whole range.
		 */
		fofs = start_bidx_of_node(ofs_of_node(dn->node_page),
							dn->inode) + ofs;
		f2fs_update_extent_cache_range(dn, fofs, 0, len);
		dec_valid_block_count(sbi, dn->inode, nr_free);
	}
	dn->ofs_in_node = ofs;

	f2fs_update_time(sbi, REQ_TIME);
	trace_f2fs_truncate_data_blocks_range(dn->inode, dn->nid,
					 dn->ofs_in_node, nr_free);
	return nr_free;
}

void truncate_data_blocks(struct dnode_of_data *dn)
{
	truncate_data_blocks_range(dn, ADDRS_PER_BLOCK);
}

static int truncate_partial_data_page(struct inode *inode, u64 from,
								bool cache_only)
{
	unsigned offset = from & (PAGE_SIZE - 1);
	pgoff_t index = from >> PAGE_SHIFT;
	struct address_space *mapping = inode->i_mapping;
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

	page = get_lock_data_page(inode, index, true);
	if (IS_ERR(page))
		return PTR_ERR(page) == -ENOENT ? 0 : PTR_ERR(page);
truncate_out:
	f2fs_wait_on_page_writeback(page, DATA, true);
	zero_user(page, offset, PAGE_SIZE - offset);

	/* An encrypted inode should have a key and truncate the last page. */
	f2fs_bug_on(F2FS_I_SB(inode), cache_only && f2fs_encrypted_inode(inode));
	if (!cache_only)
		set_page_dirty(page);
	f2fs_put_page(page, 1);
	return 0;
}

int truncate_blocks(struct inode *inode, u64 from, bool lock)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	unsigned int blocksize = inode->i_sb->s_blocksize;
	struct dnode_of_data dn;
	pgoff_t free_from;
	int count = 0, err = 0;
	struct page *ipage;
	bool truncate_page = false;

	trace_f2fs_truncate_blocks_enter(inode, from);

	free_from = (pgoff_t)F2FS_BYTES_TO_BLK(from + blocksize - 1);

	if (free_from >= sbi->max_file_blocks)
		goto free_partial;

	if (lock)
		f2fs_lock_op(sbi);

	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out;
	}

	if (f2fs_has_inline_data(inode)) {
		truncate_inline_inode(inode, ipage, from);
		f2fs_put_page(ipage, 1);
		truncate_page = true;
		goto out;
	}

	set_new_dnode(&dn, inode, ipage, NULL, 0);
	err = get_dnode_of_data(&dn, free_from, LOOKUP_NODE_RA);
	if (err) {
		if (err == -ENOENT)
			goto free_next;
		goto out;
	}

	count = ADDRS_PER_PAGE(dn.node_page, inode);

	count -= dn.ofs_in_node;
	f2fs_bug_on(sbi, count < 0);

	if (dn.ofs_in_node || IS_INODE(dn.node_page)) {
		truncate_data_blocks_range(&dn, count);
		free_from += count;
	}

	f2fs_put_dnode(&dn);
free_next:
	err = truncate_inode_blocks(inode, free_from);
out:
	if (lock)
		f2fs_unlock_op(sbi);
free_partial:
	/* lastly zero out the first data page */
	if (!err)
		err = truncate_partial_data_page(inode, from, truncate_page);

	trace_f2fs_truncate_blocks_exit(inode, err);
	return err;
}

int f2fs_truncate(struct inode *inode)
{
	int err;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
				S_ISLNK(inode->i_mode)))
		return 0;

	trace_f2fs_truncate(inode);

#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (time_to_inject(F2FS_I_SB(inode), FAULT_TRUNCATE)) {
		f2fs_show_injection_info(FAULT_TRUNCATE);
		return -EIO;
	}
#endif
	/* we should check inline_data size */
	if (!f2fs_may_inline_data(inode)) {
		err = f2fs_convert_inline_inode(inode);
		if (err)
			return err;
	}

	err = truncate_blocks(inode, i_size_read(inode), true);
	if (err)
		return err;

	inode->i_mtime = inode->i_ctime = current_time(inode);
	f2fs_mark_inode_dirty_sync(inode, false);
	return 0;
}

int f2fs_getattr(const struct path *path, struct kstat *stat,
		 u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	unsigned int flags;

	flags = fi->i_flags & (FS_FL_USER_VISIBLE | FS_PROJINHERIT_FL);
	if (flags & FS_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (flags & FS_COMPR_FL)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (f2fs_encrypted_inode(inode))
		stat->attributes |= STATX_ATTR_ENCRYPTED;
	if (flags & FS_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (flags & FS_NODUMP_FL)
		stat->attributes |= STATX_ATTR_NODUMP;

	stat->attributes_mask |= (STATX_ATTR_APPEND |
				  STATX_ATTR_COMPRESSED |
				  STATX_ATTR_ENCRYPTED |
				  STATX_ATTR_IMMUTABLE |
				  STATX_ATTR_NODUMP);

	generic_fillattr(inode, stat);

	/* we need to show initial sectors used for inline_data/dentries */
	if ((S_ISREG(inode->i_mode) && f2fs_has_inline_data(inode)) ||
					f2fs_has_inline_dentry(inode))
		stat->blocks += (stat->size + 511) >> 9;

	return 0;
}

#ifdef CONFIG_F2FS_FS_POSIX_ACL
static void __setattr_copy(struct inode *inode, const struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;

	if (ia_valid & ATTR_UID)
		inode->i_uid = attr->ia_uid;
	if (ia_valid & ATTR_GID)
		inode->i_gid = attr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		inode->i_atime = timespec_trunc(attr->ia_atime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MTIME)
		inode->i_mtime = timespec_trunc(attr->ia_mtime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_CTIME)
		inode->i_ctime = timespec_trunc(attr->ia_ctime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MODE) {
		umode_t mode = attr->ia_mode;

		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		set_acl_inode(inode, mode);
	}
}
#else
#define __setattr_copy setattr_copy
#endif

int f2fs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int err;
	bool size_changed = false;

	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	if (is_quota_modification(inode, attr)) {
		err = dquot_initialize(inode);
		if (err)
			return err;
	}
	if ((attr->ia_valid & ATTR_UID &&
		!uid_eq(attr->ia_uid, inode->i_uid)) ||
		(attr->ia_valid & ATTR_GID &&
		!gid_eq(attr->ia_gid, inode->i_gid))) {
		err = dquot_transfer(inode, attr);
		if (err)
			return err;
	}

	if (attr->ia_valid & ATTR_SIZE) {
		if (f2fs_encrypted_inode(inode)) {
			err = fscrypt_get_encryption_info(inode);
			if (err)
				return err;
			if (!fscrypt_has_encryption_key(inode))
				return -ENOKEY;
		}

		if (attr->ia_size <= i_size_read(inode)) {
			down_write(&F2FS_I(inode)->i_mmap_sem);
			truncate_setsize(inode, attr->ia_size);
			err = f2fs_truncate(inode);
			up_write(&F2FS_I(inode)->i_mmap_sem);
			if (err)
				return err;
		} else {
			/*
			 * do not trim all blocks after i_size if target size is
			 * larger than i_size.
			 */
			down_write(&F2FS_I(inode)->i_mmap_sem);
			truncate_setsize(inode, attr->ia_size);
			up_write(&F2FS_I(inode)->i_mmap_sem);

			/* should convert inline inode here */
			if (!f2fs_may_inline_data(inode)) {
				err = f2fs_convert_inline_inode(inode);
				if (err)
					return err;
			}
			inode->i_mtime = inode->i_ctime = current_time(inode);
		}

		size_changed = true;
	}

	__setattr_copy(inode, attr);

	if (attr->ia_valid & ATTR_MODE) {
		err = posix_acl_chmod(inode, get_inode_mode(inode));
		if (err || is_inode_flag_set(inode, FI_ACL_MODE)) {
			inode->i_mode = F2FS_I(inode)->i_acl_mode;
			clear_inode_flag(inode, FI_ACL_MODE);
		}
	}

	/* file size may changed here */
	f2fs_mark_inode_dirty_sync(inode, size_changed);

	/* inode change will produce dirty node pages flushed by checkpoint */
	f2fs_balance_fs(F2FS_I_SB(inode), true);

	return err;
}

const struct inode_operations f2fs_file_inode_operations = {
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_acl	= f2fs_get_acl,
	.set_acl	= f2fs_set_acl,
#ifdef CONFIG_F2FS_FS_XATTR
	.listxattr	= f2fs_listxattr,
#endif
	.fiemap		= f2fs_fiemap,
};

static int fill_zero(struct inode *inode, pgoff_t index,
					loff_t start, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct page *page;

	if (!len)
		return 0;

	f2fs_balance_fs(sbi, true);

	f2fs_lock_op(sbi);
	page = get_new_data_page(inode, NULL, index, false);
	f2fs_unlock_op(sbi);

	if (IS_ERR(page))
		return PTR_ERR(page);

	f2fs_wait_on_page_writeback(page, DATA, true);
	zero_user(page, start, len);
	set_page_dirty(page);
	f2fs_put_page(page, 1);
	return 0;
}

int truncate_hole(struct inode *inode, pgoff_t pg_start, pgoff_t pg_end)
{
	int err;

	while (pg_start < pg_end) {
		struct dnode_of_data dn;
		pgoff_t end_offset, count;

		set_new_dnode(&dn, inode, NULL, NULL, 0);
		err = get_dnode_of_data(&dn, pg_start, LOOKUP_NODE);
		if (err) {
			if (err == -ENOENT) {
				pg_start++;
				continue;
			}
			return err;
		}

		end_offset = ADDRS_PER_PAGE(dn.node_page, inode);
		count = min(end_offset - dn.ofs_in_node, pg_end - pg_start);

		f2fs_bug_on(F2FS_I_SB(inode), count == 0 || count > end_offset);

		truncate_data_blocks_range(&dn, count);
		f2fs_put_dnode(&dn);

		pg_start += count;
	}
	return 0;
}

static int punch_hole(struct inode *inode, loff_t offset, loff_t len)
{
	pgoff_t pg_start, pg_end;
	loff_t off_start, off_end;
	int ret;

	ret = f2fs_convert_inline_inode(inode);
	if (ret)
		return ret;

	pg_start = ((unsigned long long) offset) >> PAGE_SHIFT;
	pg_end = ((unsigned long long) offset + len) >> PAGE_SHIFT;

	off_start = offset & (PAGE_SIZE - 1);
	off_end = (offset + len) & (PAGE_SIZE - 1);

	if (pg_start == pg_end) {
		ret = fill_zero(inode, pg_start, off_start,
						off_end - off_start);
		if (ret)
			return ret;
	} else {
		if (off_start) {
			ret = fill_zero(inode, pg_start++, off_start,
						PAGE_SIZE - off_start);
			if (ret)
				return ret;
		}
		if (off_end) {
			ret = fill_zero(inode, pg_end, 0, off_end);
			if (ret)
				return ret;
		}

		if (pg_start < pg_end) {
			struct address_space *mapping = inode->i_mapping;
			loff_t blk_start, blk_end;
			struct f2fs_sb_info *sbi = F2FS_I_SB(inode);

			f2fs_balance_fs(sbi, true);

			blk_start = (loff_t)pg_start << PAGE_SHIFT;
			blk_end = (loff_t)pg_end << PAGE_SHIFT;
			down_write(&F2FS_I(inode)->i_mmap_sem);
			truncate_inode_pages_range(mapping, blk_start,
					blk_end - 1);

			f2fs_lock_op(sbi);
			ret = truncate_hole(inode, pg_start, pg_end);
			f2fs_unlock_op(sbi);
			up_write(&F2FS_I(inode)->i_mmap_sem);
		}
	}

	return ret;
}

static int __read_out_blkaddrs(struct inode *inode, block_t *blkaddr,
				int *do_replace, pgoff_t off, pgoff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct dnode_of_data dn;
	int ret, done, i;

next_dnode:
	set_new_dnode(&dn, inode, NULL, NULL, 0);
	ret = get_dnode_of_data(&dn, off, LOOKUP_NODE_RA);
	if (ret && ret != -ENOENT) {
		return ret;
	} else if (ret == -ENOENT) {
		if (dn.max_level == 0)
			return -ENOENT;
		done = min((pgoff_t)ADDRS_PER_BLOCK - dn.ofs_in_node, len);
		blkaddr += done;
		do_replace += done;
		goto next;
	}

	done = min((pgoff_t)ADDRS_PER_PAGE(dn.node_page, inode) -
							dn.ofs_in_node, len);
	for (i = 0; i < done; i++, blkaddr++, do_replace++, dn.ofs_in_node++) {
		*blkaddr = datablock_addr(dn.inode,
					dn.node_page, dn.ofs_in_node);
		if (!is_checkpointed_data(sbi, *blkaddr)) {

			if (test_opt(sbi, LFS)) {
				f2fs_put_dnode(&dn);
				return -ENOTSUPP;
			}

			/* do not invalidate this block address */
			f2fs_update_data_blkaddr(&dn, NULL_ADDR);
			*do_replace = 1;
		}
	}
	f2fs_put_dnode(&dn);
next:
	len -= done;
	off += done;
	if (len)
		goto next_dnode;
	return 0;
}

static int __roll_back_blkaddrs(struct inode *inode, block_t *blkaddr,
				int *do_replace, pgoff_t off, int len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct dnode_of_data dn;
	int ret, i;

	for (i = 0; i < len; i++, do_replace++, blkaddr++) {
		if (*do_replace == 0)
			continue;

		set_new_dnode(&dn, inode, NULL, NULL, 0);
		ret = get_dnode_of_data(&dn, off + i, LOOKUP_NODE_RA);
		if (ret) {
			dec_valid_block_count(sbi, inode, 1);
			invalidate_blocks(sbi, *blkaddr);
		} else {
			f2fs_update_data_blkaddr(&dn, *blkaddr);
		}
		f2fs_put_dnode(&dn);
	}
	return 0;
}

static int __clone_blkaddrs(struct inode *src_inode, struct inode *dst_inode,
			block_t *blkaddr, int *do_replace,
			pgoff_t src, pgoff_t dst, pgoff_t len, bool full)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(src_inode);
	pgoff_t i = 0;
	int ret;

	while (i < len) {
		if (blkaddr[i] == NULL_ADDR && !full) {
			i++;
			continue;
		}

		if (do_replace[i] || blkaddr[i] == NULL_ADDR) {
			struct dnode_of_data dn;
			struct node_info ni;
			size_t new_size;
			pgoff_t ilen;

			set_new_dnode(&dn, dst_inode, NULL, NULL, 0);
			ret = get_dnode_of_data(&dn, dst + i, ALLOC_NODE);
			if (ret)
				return ret;

			get_node_info(sbi, dn.nid, &ni);
			ilen = min((pgoff_t)
				ADDRS_PER_PAGE(dn.node_page, dst_inode) -
						dn.ofs_in_node, len - i);
			do {
				dn.data_blkaddr = datablock_addr(dn.inode,
						dn.node_page, dn.ofs_in_node);
				truncate_data_blocks_range(&dn, 1);

				if (do_replace[i]) {
					f2fs_i_blocks_write(src_inode,
							1, false, false);
					f2fs_i_blocks_write(dst_inode,
							1, true, false);
					f2fs_replace_block(sbi, &dn, dn.data_blkaddr,
					blkaddr[i], ni.version, true, false);

					do_replace[i] = 0;
				}
				dn.ofs_in_node++;
				i++;
				new_size = (dst + i) << PAGE_SHIFT;
				if (dst_inode->i_size < new_size)
					f2fs_i_size_write(dst_inode, new_size);
			} while (--ilen && (do_replace[i] || blkaddr[i] == NULL_ADDR));

			f2fs_put_dnode(&dn);
		} else {
			struct page *psrc, *pdst;

			psrc = get_lock_data_page(src_inode, src + i, true);
			if (IS_ERR(psrc))
				return PTR_ERR(psrc);
			pdst = get_new_data_page(dst_inode, NULL, dst + i,
								true);
			if (IS_ERR(pdst)) {
				f2fs_put_page(psrc, 1);
				return PTR_ERR(pdst);
			}
			f2fs_copy_page(psrc, pdst);
			set_page_dirty(pdst);
			f2fs_put_page(pdst, 1);
			f2fs_put_page(psrc, 1);

			ret = truncate_hole(src_inode, src + i, src + i + 1);
			if (ret)
				return ret;
			i++;
		}
	}
	return 0;
}

static int __exchange_data_block(struct inode *src_inode,
			struct inode *dst_inode, pgoff_t src, pgoff_t dst,
			pgoff_t len, bool full)
{
	block_t *src_blkaddr;
	int *do_replace;
	pgoff_t olen;
	int ret;

	while (len) {
		olen = min((pgoff_t)4 * ADDRS_PER_BLOCK, len);

		src_blkaddr = kvzalloc(sizeof(block_t) * olen, GFP_KERNEL);
		if (!src_blkaddr)
			return -ENOMEM;

		do_replace = kvzalloc(sizeof(int) * olen, GFP_KERNEL);
		if (!do_replace) {
			kvfree(src_blkaddr);
			return -ENOMEM;
		}

		ret = __read_out_blkaddrs(src_inode, src_blkaddr,
					do_replace, src, olen);
		if (ret)
			goto roll_back;

		ret = __clone_blkaddrs(src_inode, dst_inode, src_blkaddr,
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
	__roll_back_blkaddrs(src_inode, src_blkaddr, do_replace, src, len);
	kvfree(src_blkaddr);
	kvfree(do_replace);
	return ret;
}

static int f2fs_do_collapse(struct inode *inode, pgoff_t start, pgoff_t end)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	pgoff_t nrpages = (i_size_read(inode) + PAGE_SIZE - 1) / PAGE_SIZE;
	int ret;

	f2fs_balance_fs(sbi, true);
	f2fs_lock_op(sbi);

	f2fs_drop_extent_tree(inode);

	ret = __exchange_data_block(inode, inode, end, start, nrpages - end, true);
	f2fs_unlock_op(sbi);
	return ret;
}

static int f2fs_collapse_range(struct inode *inode, loff_t offset, loff_t len)
{
	pgoff_t pg_start, pg_end;
	loff_t new_size;
	int ret;

	if (offset + len >= i_size_read(inode))
		return -EINVAL;

	/* collapse range should be aligned to block size of f2fs. */
	if (offset & (F2FS_BLKSIZE - 1) || len & (F2FS_BLKSIZE - 1))
		return -EINVAL;

	ret = f2fs_convert_inline_inode(inode);
	if (ret)
		return ret;

	pg_start = offset >> PAGE_SHIFT;
	pg_end = (offset + len) >> PAGE_SHIFT;

	down_write(&F2FS_I(inode)->i_mmap_sem);
	/* write out all dirty pages from offset */
	ret = filemap_write_and_wait_range(inode->i_mapping, offset, LLONG_MAX);
	if (ret)
		goto out;

	truncate_pagecache(inode, offset);

	ret = f2fs_do_collapse(inode, pg_start, pg_end);
	if (ret)
		goto out;

	/* write out all moved pages, if possible */
	filemap_write_and_wait_range(inode->i_mapping, offset, LLONG_MAX);
	truncate_pagecache(inode, offset);

	new_size = i_size_read(inode) - len;
	truncate_pagecache(inode, new_size);

	ret = truncate_blocks(inode, new_size, true);
	if (!ret)
		f2fs_i_size_write(inode, new_size);

out:
	up_write(&F2FS_I(inode)->i_mmap_sem);
	return ret;
}

static int f2fs_do_zero_range(struct dnode_of_data *dn, pgoff_t start,
								pgoff_t end)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(dn->inode);
	pgoff_t index = start;
	unsigned int ofs_in_node = dn->ofs_in_node;
	blkcnt_t count = 0;
	int ret;

	for (; index < end; index++, dn->ofs_in_node++) {
		if (datablock_addr(dn->inode, dn->node_page,
					dn->ofs_in_node) == NULL_ADDR)
			count++;
	}

	dn->ofs_in_node = ofs_in_node;
	ret = reserve_new_blocks(dn, count);
	if (ret)
		return ret;

	dn->ofs_in_node = ofs_in_node;
	for (index = start; index < end; index++, dn->ofs_in_node++) {
		dn->data_blkaddr = datablock_addr(dn->inode,
					dn->node_page, dn->ofs_in_node);
		/*
		 * reserve_new_blocks will not guarantee entire block
		 * allocation.
		 */
		if (dn->data_blkaddr == NULL_ADDR) {
			ret = -ENOSPC;
			break;
		}
		if (dn->data_blkaddr != NEW_ADDR) {
			invalidate_blocks(sbi, dn->data_blkaddr);
			dn->data_blkaddr = NEW_ADDR;
			set_data_blkaddr(dn);
		}
	}

	f2fs_update_extent_cache_range(dn, start, 0, index - start);

	return ret;
}

static int f2fs_zero_range(struct inode *inode, loff_t offset, loff_t len,
								int mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct address_space *mapping = inode->i_mapping;
	pgoff_t index, pg_start, pg_end;
	loff_t new_size = i_size_read(inode);
	loff_t off_start, off_end;
	int ret = 0;

	ret = inode_newsize_ok(inode, (len + offset));
	if (ret)
		return ret;

	ret = f2fs_convert_inline_inode(inode);
	if (ret)
		return ret;

	down_write(&F2FS_I(inode)->i_mmap_sem);
	ret = filemap_write_and_wait_range(mapping, offset, offset + len - 1);
	if (ret)
		goto out_sem;

	truncate_pagecache_range(inode, offset, offset + len - 1);

	pg_start = ((unsigned long long) offset) >> PAGE_SHIFT;
	pg_end = ((unsigned long long) offset + len) >> PAGE_SHIFT;

	off_start = offset & (PAGE_SIZE - 1);
	off_end = (offset + len) & (PAGE_SIZE - 1);

	if (pg_start == pg_end) {
		ret = fill_zero(inode, pg_start, off_start,
						off_end - off_start);
		if (ret)
			goto out_sem;

		new_size = max_t(loff_t, new_size, offset + len);
	} else {
		if (off_start) {
			ret = fill_zero(inode, pg_start++, off_start,
						PAGE_SIZE - off_start);
			if (ret)
				goto out_sem;

			new_size = max_t(loff_t, new_size,
					(loff_t)pg_start << PAGE_SHIFT);
		}

		for (index = pg_start; index < pg_end;) {
			struct dnode_of_data dn;
			unsigned int end_offset;
			pgoff_t end;

			f2fs_lock_op(sbi);

			set_new_dnode(&dn, inode, NULL, NULL, 0);
			ret = get_dnode_of_data(&dn, index, ALLOC_NODE);
			if (ret) {
				f2fs_unlock_op(sbi);
				goto out;
			}

			end_offset = ADDRS_PER_PAGE(dn.node_page, inode);
			end = min(pg_end, end_offset - dn.ofs_in_node + index);

			ret = f2fs_do_zero_range(&dn, index, end);
			f2fs_put_dnode(&dn);
			f2fs_unlock_op(sbi);

			f2fs_balance_fs(sbi, dn.node_changed);

			if (ret)
				goto out;

			index = end;
			new_size = max_t(loff_t, new_size,
					(loff_t)index << PAGE_SHIFT);
		}

		if (off_end) {
			ret = fill_zero(inode, pg_end, 0, off_end);
			if (ret)
				goto out;

			new_size = max_t(loff_t, new_size, offset + len);
		}
	}

out:
	if (new_size > i_size_read(inode)) {
		if (mode & FALLOC_FL_KEEP_SIZE)
			file_set_keep_isize(inode);
		else
			f2fs_i_size_write(inode, new_size);
	}
out_sem:
	up_write(&F2FS_I(inode)->i_mmap_sem);

	return ret;
}

static int f2fs_insert_range(struct inode *inode, loff_t offset, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	pgoff_t nr, pg_start, pg_end, delta, idx;
	loff_t new_size;
	int ret = 0;

	new_size = i_size_read(inode) + len;
	ret = inode_newsize_ok(inode, new_size);
	if (ret)
		return ret;

	if (offset >= i_size_read(inode))
		return -EINVAL;

	/* insert range should be aligned to block size of f2fs. */
	if (offset & (F2FS_BLKSIZE - 1) || len & (F2FS_BLKSIZE - 1))
		return -EINVAL;

	ret = f2fs_convert_inline_inode(inode);
	if (ret)
		return ret;

	f2fs_balance_fs(sbi, true);

	down_write(&F2FS_I(inode)->i_mmap_sem);
	ret = truncate_blocks(inode, i_size_read(inode), true);
	if (ret)
		goto out;

	/* write out all dirty pages from offset */
	ret = filemap_write_and_wait_range(inode->i_mapping, offset, LLONG_MAX);
	if (ret)
		goto out;

	truncate_pagecache(inode, offset);

	pg_start = offset >> PAGE_SHIFT;
	pg_end = (offset + len) >> PAGE_SHIFT;
	delta = pg_end - pg_start;
	idx = (i_size_read(inode) + PAGE_SIZE - 1) / PAGE_SIZE;

	while (!ret && idx > pg_start) {
		nr = idx - pg_start;
		if (nr > delta)
			nr = delta;
		idx -= nr;

		f2fs_lock_op(sbi);
		f2fs_drop_extent_tree(inode);

		ret = __exchange_data_block(inode, inode, idx,
					idx + delta, nr, false);
		f2fs_unlock_op(sbi);
	}

	/* write out all moved pages, if possible */
	filemap_write_and_wait_range(inode->i_mapping, offset, LLONG_MAX);
	truncate_pagecache(inode, offset);

	if (!ret)
		f2fs_i_size_write(inode, new_size);
out:
	up_write(&F2FS_I(inode)->i_mmap_sem);
	return ret;
}

static int expand_inode_data(struct inode *inode, loff_t offset,
					loff_t len, int mode)
{
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_map_blocks map = { .m_next_pgofs = NULL };
	pgoff_t pg_end;
	loff_t new_size = i_size_read(inode);
	loff_t off_end;
	int err;

	err = inode_newsize_ok(inode, (len + offset));
	if (err)
		return err;

	err = f2fs_convert_inline_inode(inode);
	if (err)
		return err;

	f2fs_balance_fs(sbi, true);

	pg_end = ((unsigned long long)offset + len) >> PAGE_SHIFT;
	off_end = (offset + len) & (PAGE_SIZE - 1);

	map.m_lblk = ((unsigned long long)offset) >> PAGE_SHIFT;
	map.m_len = pg_end - map.m_lblk;
	if (off_end)
		map.m_len++;

	err = f2fs_map_blocks(inode, &map, 1, F2FS_GET_BLOCK_PRE_AIO);
	if (err) {
		pgoff_t last_off;

		if (!map.m_len)
			return err;

		last_off = map.m_lblk + map.m_len - 1;

		/* update new size to the failed position */
		new_size = (last_off == pg_end) ? offset + len:
					(loff_t)(last_off + 1) << PAGE_SHIFT;
	} else {
		new_size = ((loff_t)pg_end << PAGE_SHIFT) + off_end;
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) && i_size_read(inode) < new_size)
		f2fs_i_size_write(inode, new_size);

	return err;
}

static long f2fs_fallocate(struct file *file, int mode,
				loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	long ret = 0;

	/* f2fs only support ->fallocate for regular file */
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	if (f2fs_encrypted_inode(inode) &&
		(mode & (FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_INSERT_RANGE)))
		return -EOPNOTSUPP;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
			FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_ZERO_RANGE |
			FALLOC_FL_INSERT_RANGE))
		return -EOPNOTSUPP;

	inode_lock(inode);

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		if (offset >= inode->i_size)
			goto out;

		ret = punch_hole(inode, offset, len);
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		ret = f2fs_collapse_range(inode, offset, len);
	} else if (mode & FALLOC_FL_ZERO_RANGE) {
		ret = f2fs_zero_range(inode, offset, len, mode);
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		ret = f2fs_insert_range(inode, offset, len);
	} else {
		ret = expand_inode_data(inode, offset, len, mode);
	}

	if (!ret) {
		inode->i_mtime = inode->i_ctime = current_time(inode);
		f2fs_mark_inode_dirty_sync(inode, false);
		if (mode & FALLOC_FL_KEEP_SIZE)
			file_set_keep_isize(inode);
		f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
	}

out:
	inode_unlock(inode);

	trace_f2fs_fallocate(inode, mode, offset, len, ret);
	return ret;
}

static int f2fs_release_file(struct inode *inode, struct file *filp)
{
	/*
	 * f2fs_relase_file is called at every close calls. So we should
	 * not drop any inmemory pages by close called by other process.
	 */
	if (!(filp->f_mode & FMODE_WRITE) ||
			atomic_read(&inode->i_writecount) != 1)
		return 0;

	/* some remained atomic pages should discarded */
	if (f2fs_is_atomic_file(inode))
		drop_inmem_pages(inode);
	if (f2fs_is_volatile_file(inode)) {
		clear_inode_flag(inode, FI_VOLATILE_FILE);
		stat_dec_volatile_write(inode);
		set_inode_flag(inode, FI_DROP_CACHE);
		filemap_fdatawrite(inode->i_mapping);
		clear_inode_flag(inode, FI_DROP_CACHE);
	}
	return 0;
}

static int f2fs_file_flush(struct file *file, fl_owner_t id)
{
	struct inode *inode = file_inode(file);

	/*
	 * If the process doing a transaction is crashed, we should do
	 * roll-back. Otherwise, other reader/write can see corrupted database
	 * until all the writers close its file. Since this should be done
	 * before dropping file lock, it needs to do in ->flush.
	 */
	if (f2fs_is_atomic_file(inode) &&
			F2FS_I(inode)->inmem_task == current)
		drop_inmem_pages(inode);
	return 0;
}

static int f2fs_ioc_getflags(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	unsigned int flags = fi->i_flags &
			(FS_FL_USER_VISIBLE | FS_PROJINHERIT_FL);
	return put_user(flags, (int __user *)arg);
}

static int __f2fs_ioc_setflags(struct inode *inode, unsigned int flags)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	unsigned int oldflags;

	/* Is it quota file? Do not allow user to mess with it */
	if (IS_NOQUOTA(inode))
		return -EPERM;

	flags = f2fs_mask_flags(inode->i_mode, flags);

	oldflags = fi->i_flags;

	if ((flags ^ oldflags) & (FS_APPEND_FL | FS_IMMUTABLE_FL))
		if (!capable(CAP_LINUX_IMMUTABLE))
			return -EPERM;

	flags = flags & (FS_FL_USER_MODIFIABLE | FS_PROJINHERIT_FL);
	flags |= oldflags & ~(FS_FL_USER_MODIFIABLE | FS_PROJINHERIT_FL);
	fi->i_flags = flags;

	if (fi->i_flags & FS_PROJINHERIT_FL)
		set_inode_flag(inode, FI_PROJ_INHERIT);
	else
		clear_inode_flag(inode, FI_PROJ_INHERIT);

	inode->i_ctime = current_time(inode);
	f2fs_set_inode_flags(inode);
	f2fs_mark_inode_dirty_sync(inode, false);
	return 0;
}

static int f2fs_ioc_setflags(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	unsigned int flags;
	int ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	if (get_user(flags, (int __user *)arg))
		return -EFAULT;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	inode_lock(inode);

	ret = __f2fs_ioc_setflags(inode, flags);

	inode_unlock(inode);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_getversion(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);

	return put_user(inode->i_generation, (int __user *)arg);
}

static int f2fs_ioc_start_atomic_write(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	inode_lock(inode);

	if (f2fs_is_atomic_file(inode))
		goto out;

	ret = f2fs_convert_inline_inode(inode);
	if (ret)
		goto out;

	set_inode_flag(inode, FI_ATOMIC_FILE);
	set_inode_flag(inode, FI_HOT_DATA);
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);

	if (!get_dirty_pages(inode))
		goto inc_stat;

	f2fs_msg(F2FS_I_SB(inode)->sb, KERN_WARNING,
		"Unexpected flush for atomic writes: ino=%lu, npages=%u",
					inode->i_ino, get_dirty_pages(inode));
	ret = filemap_write_and_wait_range(inode->i_mapping, 0, LLONG_MAX);
	if (ret) {
		clear_inode_flag(inode, FI_ATOMIC_FILE);
		clear_inode_flag(inode, FI_HOT_DATA);
		goto out;
	}

inc_stat:
	F2FS_I(inode)->inmem_task = current;
	stat_inc_atomic_write(inode);
	stat_update_max_atomic_write(inode);
out:
	inode_unlock(inode);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_commit_atomic_write(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	inode_lock(inode);

	if (f2fs_is_volatile_file(inode))
		goto err_out;

	if (f2fs_is_atomic_file(inode)) {
		ret = commit_inmem_pages(inode);
		if (ret)
			goto err_out;

		ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 0, true);
		if (!ret) {
			clear_inode_flag(inode, FI_ATOMIC_FILE);
			clear_inode_flag(inode, FI_HOT_DATA);
			stat_dec_atomic_write(inode);
		}
	} else {
		ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 1, false);
	}
err_out:
	inode_unlock(inode);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_start_volatile_write(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	inode_lock(inode);

	if (f2fs_is_volatile_file(inode))
		goto out;

	ret = f2fs_convert_inline_inode(inode);
	if (ret)
		goto out;

	stat_inc_volatile_write(inode);
	stat_update_max_volatile_write(inode);

	set_inode_flag(inode, FI_VOLATILE_FILE);
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
out:
	inode_unlock(inode);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_release_volatile_write(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	inode_lock(inode);

	if (!f2fs_is_volatile_file(inode))
		goto out;

	if (!f2fs_is_first_block_written(inode)) {
		ret = truncate_partial_data_page(inode, 0, true);
		goto out;
	}

	ret = punch_hole(inode, 0, F2FS_BLKSIZE);
out:
	inode_unlock(inode);
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_abort_volatile_write(struct file *filp)
{
	struct inode *inode = file_inode(filp);
	int ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	inode_lock(inode);

	if (f2fs_is_atomic_file(inode))
		drop_inmem_pages(inode);
	if (f2fs_is_volatile_file(inode)) {
		clear_inode_flag(inode, FI_VOLATILE_FILE);
		stat_dec_volatile_write(inode);
		ret = f2fs_do_sync_file(filp, 0, LLONG_MAX, 0, true);
	}

	inode_unlock(inode);

	mnt_drop_write_file(filp);
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
	return ret;
}

static int f2fs_ioc_shutdown(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct super_block *sb = sbi->sb;
	__u32 in;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(in, (__u32 __user *)arg))
		return -EFAULT;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	switch (in) {
	case F2FS_GOING_DOWN_FULLSYNC:
		sb = freeze_bdev(sb->s_bdev);
		if (sb && !IS_ERR(sb)) {
			f2fs_stop_checkpoint(sbi, false);
			thaw_bdev(sb->s_bdev, sb);
		}
		break;
	case F2FS_GOING_DOWN_METASYNC:
		/* do checkpoint only */
		f2fs_sync_fs(sb, 1);
		f2fs_stop_checkpoint(sbi, false);
		break;
	case F2FS_GOING_DOWN_NOSYNC:
		f2fs_stop_checkpoint(sbi, false);
		break;
	case F2FS_GOING_DOWN_METAFLUSH:
		sync_meta_pages(sbi, META, LONG_MAX, FS_META_IO);
		f2fs_stop_checkpoint(sbi, false);
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	f2fs_update_time(sbi, REQ_TIME);
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_fitrim(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct request_queue *q = bdev_get_queue(sb->s_bdev);
	struct fstrim_range range;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!blk_queue_discard(q))
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
	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);
	return 0;
}

static bool uuid_is_nonzero(__u8 u[16])
{
	int i;

	for (i = 0; i < 16; i++)
		if (u[i])
			return true;
	return false;
}

static int f2fs_ioc_set_encryption_policy(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);

	f2fs_update_time(F2FS_I_SB(inode), REQ_TIME);

	return fscrypt_ioctl_set_policy(filp, (const void __user *)arg);
}

static int f2fs_ioc_get_encryption_policy(struct file *filp, unsigned long arg)
{
	return fscrypt_ioctl_get_policy(filp, (void __user *)arg);
}

static int f2fs_ioc_get_encryption_pwsalt(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int err;

	if (!f2fs_sb_has_crypto(inode->i_sb))
		return -EOPNOTSUPP;

	if (uuid_is_nonzero(sbi->raw_super->encrypt_pw_salt))
		goto got_it;

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	/* update superblock with uuid */
	generate_random_uuid(sbi->raw_super->encrypt_pw_salt);

	err = f2fs_commit_super(sbi, false);
	if (err) {
		/* undo new data */
		memset(sbi->raw_super->encrypt_pw_salt, 0, 16);
		mnt_drop_write_file(filp);
		return err;
	}
	mnt_drop_write_file(filp);
got_it:
	if (copy_to_user((__u8 __user *)arg, sbi->raw_super->encrypt_pw_salt,
									16))
		return -EFAULT;
	return 0;
}

static int f2fs_ioc_gc(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
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
	struct inode *inode = file_inode(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
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

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	end = range.start + range.len;
	if (range.start < MAIN_BLKADDR(sbi) || end >= MAX_BLKADDR(sbi))
		return -EINVAL;
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
	range.start += sbi->blocks_per_seg;
	if (range.start <= end)
		goto do_more;
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_write_checkpoint(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

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
	struct inode *inode = file_inode(filp);
	struct f2fs_map_blocks map = { .m_next_pgofs = NULL };
	struct extent_info ei = {0,0,0};
	pgoff_t pg_start, pg_end;
	unsigned int blk_per_seg = sbi->blocks_per_seg;
	unsigned int total = 0, sec_num;
	block_t blk_end = 0;
	bool fragmented = false;
	int err;

	/* if in-place-update policy is enabled, don't waste time here */
	if (need_inplace_update_policy(inode, NULL))
		return -EINVAL;

	pg_start = range->start >> PAGE_SHIFT;
	pg_end = (range->start + range->len) >> PAGE_SHIFT;

	f2fs_balance_fs(sbi, true);

	inode_lock(inode);

	/* writeback all dirty pages in the range */
	err = filemap_write_and_wait_range(inode->i_mapping, range->start,
						range->start + range->len - 1);
	if (err)
		goto out;

	/*
	 * lookup mapping info in extent cache, skip defragmenting if physical
	 * block addresses are continuous.
	 */
	if (f2fs_lookup_extent_cache(inode, pg_start, &ei)) {
		if (ei.fofs + ei.len >= pg_end)
			goto out;
	}

	map.m_lblk = pg_start;

	/*
	 * lookup mapping info in dnode page cache, skip defragmenting if all
	 * physical block addresses are continuous even if there are hole(s)
	 * in logical blocks.
	 */
	while (map.m_lblk < pg_end) {
		map.m_len = pg_end - map.m_lblk;
		err = f2fs_map_blocks(inode, &map, 0, F2FS_GET_BLOCK_DEFAULT);
		if (err)
			goto out;

		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			map.m_lblk++;
			continue;
		}

		if (blk_end && blk_end != map.m_pblk) {
			fragmented = true;
			break;
		}
		blk_end = map.m_pblk + map.m_len;

		map.m_lblk += map.m_len;
	}

	if (!fragmented)
		goto out;

	map.m_lblk = pg_start;
	map.m_len = pg_end - pg_start;

	sec_num = (map.m_len + BLKS_PER_SEC(sbi) - 1) / BLKS_PER_SEC(sbi);

	/*
	 * make sure there are enough free section for LFS allocation, this can
	 * avoid defragment running in SSR mode when free section are allocated
	 * intensively
	 */
	if (has_not_enough_free_secs(sbi, 0, sec_num)) {
		err = -EAGAIN;
		goto out;
	}

	while (map.m_lblk < pg_end) {
		pgoff_t idx;
		int cnt = 0;

do_map:
		map.m_len = pg_end - map.m_lblk;
		err = f2fs_map_blocks(inode, &map, 0, F2FS_GET_BLOCK_DEFAULT);
		if (err)
			goto clear_out;

		if (!(map.m_flags & F2FS_MAP_FLAGS)) {
			map.m_lblk++;
			continue;
		}

		set_inode_flag(inode, FI_DO_DEFRAG);

		idx = map.m_lblk;
		while (idx < map.m_lblk + map.m_len && cnt < blk_per_seg) {
			struct page *page;

			page = get_lock_data_page(inode, idx, true);
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

		if (idx < pg_end && cnt < blk_per_seg)
			goto do_map;

		clear_inode_flag(inode, FI_DO_DEFRAG);

		err = filemap_fdatawrite(inode->i_mapping);
		if (err)
			goto out;
	}
clear_out:
	clear_inode_flag(inode, FI_DO_DEFRAG);
out:
	inode_unlock(inode);
	if (!err)
		range->len = (u64)total << PAGE_SHIFT;
	return err;
}

static int f2fs_ioc_defragment(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct f2fs_defragment range;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!S_ISREG(inode->i_mode) || f2fs_is_atomic_file(inode))
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
	struct inode *src = file_inode(file_in);
	struct inode *dst = file_inode(file_out);
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

	if (f2fs_encrypted_inode(src) || f2fs_encrypted_inode(dst))
		return -EOPNOTSUPP;

	if (src == dst) {
		if (pos_in == pos_out)
			return 0;
		if (pos_out > pos_in && pos_out < pos_in + len)
			return -EINVAL;
	}

	inode_lock(src);
	if (src != dst) {
		if (!inode_trylock(dst)) {
			ret = -EBUSY;
			goto out;
		}
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

	ret = f2fs_convert_inline_inode(src);
	if (ret)
		goto out_unlock;

	ret = f2fs_convert_inline_inode(dst);
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
out_unlock:
	if (src != dst)
		inode_unlock(dst);
out:
	inode_unlock(src);
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
	struct inode *inode = file_inode(filp);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct sit_info *sm = SIT_I(sbi);
	unsigned int start_segno = 0, end_segno = 0;
	unsigned int dev_start_segno = 0, dev_end_segno = 0;
	struct f2fs_flush_device range;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (f2fs_readonly(sbi->sb))
		return -EROFS;

	if (copy_from_user(&range, (struct f2fs_flush_device __user *)arg,
							sizeof(range)))
		return -EFAULT;

	if (sbi->s_ndevs <= 1 || sbi->s_ndevs - 1 <= range.dev_num ||
			sbi->segs_per_sec != 1) {
		f2fs_msg(sbi->sb, KERN_WARNING,
			"Can't flush %u in %d for segs_per_sec %u != 1\n",
				range.dev_num, sbi->s_ndevs,
				sbi->segs_per_sec);
		return -EINVAL;
	}

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	if (range.dev_num != 0)
		dev_start_segno = GET_SEGNO(sbi, FDEV(range.dev_num).start_blk);
	dev_end_segno = GET_SEGNO(sbi, FDEV(range.dev_num).end_blk);

	start_segno = sm->last_victim[FLUSH_DEVICE];
	if (start_segno < dev_start_segno || start_segno >= dev_end_segno)
		start_segno = dev_start_segno;
	end_segno = min(start_segno + range.segments, dev_end_segno);

	while (start_segno < end_segno) {
		if (!mutex_trylock(&sbi->gc_mutex)) {
			ret = -EBUSY;
			goto out;
		}
		sm->last_victim[GC_CB] = end_segno + 1;
		sm->last_victim[GC_GREEDY] = end_segno + 1;
		sm->last_victim[ALLOC_NEXT] = end_segno + 1;
		ret = f2fs_gc(sbi, true, true, start_segno);
		if (ret == -EAGAIN)
			ret = 0;
		else if (ret < 0)
			break;
		start_segno++;
	}
out:
	mnt_drop_write_file(filp);
	return ret;
}

static int f2fs_ioc_get_features(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	u32 sb_feature = le32_to_cpu(F2FS_I_SB(inode)->raw_super->feature);

	/* Must validate to set it with SQLite behavior in Android. */
	sb_feature |= F2FS_FEATURE_ATOMIC_WRITE;

	return put_user(sb_feature, (u32 __user *)arg);
}

#ifdef CONFIG_QUOTA
static int f2fs_ioc_setproject(struct file *filp, __u32 projid)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
	struct super_block *sb = sbi->sb;
	struct dquot *transfer_to[MAXQUOTAS] = {};
	struct page *ipage;
	kprojid_t kprojid;
	int err;

	if (!f2fs_sb_has_project_quota(sb)) {
		if (projid != F2FS_DEF_PROJID)
			return -EOPNOTSUPP;
		else
			return 0;
	}

	if (!f2fs_has_extra_attr(inode))
		return -EOPNOTSUPP;

	kprojid = make_kprojid(&init_user_ns, (projid_t)projid);

	if (projid_eq(kprojid, F2FS_I(inode)->i_projid))
		return 0;

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	err = -EPERM;
	inode_lock(inode);

	/* Is it quota file? Do not allow user to mess with it */
	if (IS_NOQUOTA(inode))
		goto out_unlock;

	ipage = get_node_page(sbi, inode->i_ino);
	if (IS_ERR(ipage)) {
		err = PTR_ERR(ipage);
		goto out_unlock;
	}

	if (!F2FS_FITS_IN_INODE(F2FS_INODE(ipage), fi->i_extra_isize,
								i_projid)) {
		err = -EOVERFLOW;
		f2fs_put_page(ipage, 1);
		goto out_unlock;
	}
	f2fs_put_page(ipage, 1);

	dquot_initialize(inode);

	transfer_to[PRJQUOTA] = dqget(sb, make_kqid_projid(kprojid));
	if (!IS_ERR(transfer_to[PRJQUOTA])) {
		err = __dquot_transfer(inode, transfer_to);
		dqput(transfer_to[PRJQUOTA]);
		if (err)
			goto out_dirty;
	}

	F2FS_I(inode)->i_projid = kprojid;
	inode->i_ctime = current_time(inode);
out_dirty:
	f2fs_mark_inode_dirty_sync(inode, true);
out_unlock:
	inode_unlock(inode);
	mnt_drop_write_file(filp);
	return err;
}
#else
static int f2fs_ioc_setproject(struct file *filp, __u32 projid)
{
	if (projid != F2FS_DEF_PROJID)
		return -EOPNOTSUPP;
	return 0;
}
#endif

/* Transfer internal flags to xflags */
static inline __u32 f2fs_iflags_to_xflags(unsigned long iflags)
{
	__u32 xflags = 0;

	if (iflags & FS_SYNC_FL)
		xflags |= FS_XFLAG_SYNC;
	if (iflags & FS_IMMUTABLE_FL)
		xflags |= FS_XFLAG_IMMUTABLE;
	if (iflags & FS_APPEND_FL)
		xflags |= FS_XFLAG_APPEND;
	if (iflags & FS_NODUMP_FL)
		xflags |= FS_XFLAG_NODUMP;
	if (iflags & FS_NOATIME_FL)
		xflags |= FS_XFLAG_NOATIME;
	if (iflags & FS_PROJINHERIT_FL)
		xflags |= FS_XFLAG_PROJINHERIT;
	return xflags;
}

#define F2FS_SUPPORTED_FS_XFLAGS (FS_XFLAG_SYNC | FS_XFLAG_IMMUTABLE | \
				  FS_XFLAG_APPEND | FS_XFLAG_NODUMP | \
				  FS_XFLAG_NOATIME | FS_XFLAG_PROJINHERIT)

/* Flags we can manipulate with through EXT4_IOC_FSSETXATTR */
#define F2FS_FL_XFLAG_VISIBLE		(FS_SYNC_FL | \
					 FS_IMMUTABLE_FL | \
					 FS_APPEND_FL | \
					 FS_NODUMP_FL | \
					 FS_NOATIME_FL | \
					 FS_PROJINHERIT_FL)

/* Transfer xflags flags to internal */
static inline unsigned long f2fs_xflags_to_iflags(__u32 xflags)
{
	unsigned long iflags = 0;

	if (xflags & FS_XFLAG_SYNC)
		iflags |= FS_SYNC_FL;
	if (xflags & FS_XFLAG_IMMUTABLE)
		iflags |= FS_IMMUTABLE_FL;
	if (xflags & FS_XFLAG_APPEND)
		iflags |= FS_APPEND_FL;
	if (xflags & FS_XFLAG_NODUMP)
		iflags |= FS_NODUMP_FL;
	if (xflags & FS_XFLAG_NOATIME)
		iflags |= FS_NOATIME_FL;
	if (xflags & FS_XFLAG_PROJINHERIT)
		iflags |= FS_PROJINHERIT_FL;

	return iflags;
}

static int f2fs_ioc_fsgetxattr(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct fsxattr fa;

	memset(&fa, 0, sizeof(struct fsxattr));
	fa.fsx_xflags = f2fs_iflags_to_xflags(fi->i_flags &
				(FS_FL_USER_VISIBLE | FS_PROJINHERIT_FL));

	if (f2fs_sb_has_project_quota(inode->i_sb))
		fa.fsx_projid = (__u32)from_kprojid(&init_user_ns,
							fi->i_projid);

	if (copy_to_user((struct fsxattr __user *)arg, &fa, sizeof(fa)))
		return -EFAULT;
	return 0;
}

static int f2fs_ioc_fssetxattr(struct file *filp, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct fsxattr fa;
	unsigned int flags;
	int err;

	if (copy_from_user(&fa, (struct fsxattr __user *)arg, sizeof(fa)))
		return -EFAULT;

	/* Make sure caller has proper permission */
	if (!inode_owner_or_capable(inode))
		return -EACCES;

	if (fa.fsx_xflags & ~F2FS_SUPPORTED_FS_XFLAGS)
		return -EOPNOTSUPP;

	flags = f2fs_xflags_to_iflags(fa.fsx_xflags);
	if (f2fs_mask_flags(inode->i_mode, flags) != flags)
		return -EOPNOTSUPP;

	err = mnt_want_write_file(filp);
	if (err)
		return err;

	inode_lock(inode);
	flags = (fi->i_flags & ~F2FS_FL_XFLAG_VISIBLE) |
				(flags & F2FS_FL_XFLAG_VISIBLE);
	err = __f2fs_ioc_setflags(inode, flags);
	inode_unlock(inode);
	mnt_drop_write_file(filp);
	if (err)
		return err;

	err = f2fs_ioc_setproject(filp, fa.fsx_projid);
	if (err)
		return err;

	return 0;
}

long f2fs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
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
	default:
		return -ENOTTY;
	}
}

static ssize_t f2fs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct blk_plug plug;
	ssize_t ret;

	inode_lock(inode);
	ret = generic_write_checks(iocb, from);
	if (ret > 0) {
		int err;

		if (iov_iter_fault_in_readable(from, iov_iter_count(from)))
			set_inode_flag(inode, FI_NO_PREALLOC);

		err = f2fs_preallocate_blocks(iocb, from);
		if (err) {
			clear_inode_flag(inode, FI_NO_PREALLOC);
			inode_unlock(inode);
			return err;
		}
		blk_start_plug(&plug);
		ret = __generic_file_write_iter(iocb, from);
		blk_finish_plug(&plug);
		clear_inode_flag(inode, FI_NO_PREALLOC);

		if (ret > 0)
			f2fs_update_iostat(F2FS_I_SB(inode), APP_WRITE_IO, ret);
	}
	inode_unlock(inode);

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
	case F2FS_IOC_SET_ENCRYPTION_POLICY:
	case F2FS_IOC_GET_ENCRYPTION_PWSALT:
	case F2FS_IOC_GET_ENCRYPTION_POLICY:
	case F2FS_IOC_GARBAGE_COLLECT:
	case F2FS_IOC_GARBAGE_COLLECT_RANGE:
	case F2FS_IOC_WRITE_CHECKPOINT:
	case F2FS_IOC_DEFRAGMENT:
	case F2FS_IOC_MOVE_RANGE:
	case F2FS_IOC_FLUSH_DEVICE:
	case F2FS_IOC_GET_FEATURES:
	case F2FS_IOC_FSGETXATTR:
	case F2FS_IOC_FSSETXATTR:
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
