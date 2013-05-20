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

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"
#include "acl.h"
#include <trace/events/f2fs.h>

static int f2fs_vm_page_mkwrite(struct vm_area_struct *vma,
						struct vm_fault *vmf)
{
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vma->vm_file);
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	block_t old_blk_addr;
	struct dnode_of_data dn;
	int err, ilock;

	f2fs_balance_fs(sbi);

	sb_start_pagefault(inode->i_sb);

	/* block allocation */
	ilock = mutex_lock_op(sbi);
	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, page->index, ALLOC_NODE);
	if (err) {
		mutex_unlock_op(sbi, ilock);
		goto out;
	}

	old_blk_addr = dn.data_blkaddr;

	if (old_blk_addr == NULL_ADDR) {
		err = reserve_new_block(&dn);
		if (err) {
			f2fs_put_dnode(&dn);
			mutex_unlock_op(sbi, ilock);
			goto out;
		}
	}
	f2fs_put_dnode(&dn);
	mutex_unlock_op(sbi, ilock);

	lock_page(page);
	if (page->mapping != inode->i_mapping ||
			page_offset(page) >= i_size_read(inode) ||
			!PageUptodate(page)) {
		unlock_page(page);
		err = -EFAULT;
		goto out;
	}

	/*
	 * check to see if the page is mapped already (no holes)
	 */
	if (PageMappedToDisk(page))
		goto out;

	/* fill the page */
	wait_on_page_writeback(page);

	/* page is wholly or partially inside EOF */
	if (((page->index + 1) << PAGE_CACHE_SHIFT) > i_size_read(inode)) {
		unsigned offset;
		offset = i_size_read(inode) & ~PAGE_CACHE_MASK;
		zero_user_segment(page, offset, PAGE_CACHE_SIZE);
	}
	set_page_dirty(page);
	SetPageUptodate(page);

	file_update_time(vma->vm_file);
out:
	sb_end_pagefault(inode->i_sb);
	return block_page_mkwrite_return(err);
}

static const struct vm_operations_struct f2fs_file_vm_ops = {
	.fault		= filemap_fault,
	.page_mkwrite	= f2fs_vm_page_mkwrite,
	.remap_pages	= generic_file_remap_pages,
};

int f2fs_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	int ret = 0;
	bool need_cp = false;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = LONG_MAX,
		.for_reclaim = 0,
	};

	if (inode->i_sb->s_flags & MS_RDONLY)
		return 0;

	trace_f2fs_sync_file_enter(inode);
	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret) {
		trace_f2fs_sync_file_exit(inode, need_cp, datasync, ret);
		return ret;
	}

	/* guarantee free sections for fsync */
	f2fs_balance_fs(sbi);

	mutex_lock(&inode->i_mutex);

	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		goto out;

	if (!S_ISREG(inode->i_mode) || inode->i_nlink != 1)
		need_cp = true;
	else if (is_cp_file(inode))
		need_cp = true;
	else if (!space_for_roll_forward(sbi))
		need_cp = true;
	else if (!is_checkpointed_node(sbi, F2FS_I(inode)->i_pino))
		need_cp = true;

	if (need_cp) {
		/* all the dirty node pages should be flushed for POR */
		ret = f2fs_sync_fs(inode->i_sb, 1);
	} else {
		/* if there is no written node page, write its inode page */
		while (!sync_node_pages(sbi, inode->i_ino, &wbc)) {
			ret = f2fs_write_inode(inode, NULL);
			if (ret)
				goto out;
		}
		filemap_fdatawait_range(sbi->node_inode->i_mapping,
							0, LONG_MAX);
		ret = blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);
	}
out:
	mutex_unlock(&inode->i_mutex);
	trace_f2fs_sync_file_exit(inode, need_cp, datasync, ret);
	return ret;
}

static int f2fs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
	vma->vm_ops = &f2fs_file_vm_ops;
	return 0;
}

static int truncate_data_blocks_range(struct dnode_of_data *dn, int count)
{
	int nr_free = 0, ofs = dn->ofs_in_node;
	struct f2fs_sb_info *sbi = F2FS_SB(dn->inode->i_sb);
	struct f2fs_node *raw_node;
	__le32 *addr;

	raw_node = page_address(dn->node_page);
	addr = blkaddr_in_node(raw_node) + ofs;

	for ( ; count > 0; count--, addr++, dn->ofs_in_node++) {
		block_t blkaddr = le32_to_cpu(*addr);
		if (blkaddr == NULL_ADDR)
			continue;

		update_extent_cache(NULL_ADDR, dn);
		invalidate_blocks(sbi, blkaddr);
		dec_valid_block_count(sbi, dn->inode, 1);
		nr_free++;
	}
	if (nr_free) {
		set_page_dirty(dn->node_page);
		sync_inode_page(dn);
	}
	dn->ofs_in_node = ofs;

	trace_f2fs_truncate_data_blocks_range(dn->inode, dn->nid,
					 dn->ofs_in_node, nr_free);
	return nr_free;
}

void truncate_data_blocks(struct dnode_of_data *dn)
{
	truncate_data_blocks_range(dn, ADDRS_PER_BLOCK);
}

static void truncate_partial_data_page(struct inode *inode, u64 from)
{
	unsigned offset = from & (PAGE_CACHE_SIZE - 1);
	struct page *page;

	if (!offset)
		return;

	page = find_data_page(inode, from >> PAGE_CACHE_SHIFT, false);
	if (IS_ERR(page))
		return;

	lock_page(page);
	if (page->mapping != inode->i_mapping) {
		f2fs_put_page(page, 1);
		return;
	}
	wait_on_page_writeback(page);
	zero_user(page, offset, PAGE_CACHE_SIZE - offset);
	set_page_dirty(page);
	f2fs_put_page(page, 1);
}

static int truncate_blocks(struct inode *inode, u64 from)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	unsigned int blocksize = inode->i_sb->s_blocksize;
	struct dnode_of_data dn;
	pgoff_t free_from;
	int count = 0, ilock = -1;
	int err;

	trace_f2fs_truncate_blocks_enter(inode, from);

	free_from = (pgoff_t)
			((from + blocksize - 1) >> (sbi->log_blocksize));

	ilock = mutex_lock_op(sbi);
	set_new_dnode(&dn, inode, NULL, NULL, 0);
	err = get_dnode_of_data(&dn, free_from, LOOKUP_NODE);
	if (err) {
		if (err == -ENOENT)
			goto free_next;
		mutex_unlock_op(sbi, ilock);
		trace_f2fs_truncate_blocks_exit(inode, err);
		return err;
	}

	if (IS_INODE(dn.node_page))
		count = ADDRS_PER_INODE;
	else
		count = ADDRS_PER_BLOCK;

	count -= dn.ofs_in_node;
	BUG_ON(count < 0);

	if (dn.ofs_in_node || IS_INODE(dn.node_page)) {
		truncate_data_blocks_range(&dn, count);
		free_from += count;
	}

	f2fs_put_dnode(&dn);
free_next:
	err = truncate_inode_blocks(inode, free_from);
	mutex_unlock_op(sbi, ilock);

	/* lastly zero out the first data page */
	truncate_partial_data_page(inode, from);

	trace_f2fs_truncate_blocks_exit(inode, err);
	return err;
}

void f2fs_truncate(struct inode *inode)
{
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
				S_ISLNK(inode->i_mode)))
		return;

	trace_f2fs_truncate(inode);

	if (!truncate_blocks(inode, i_size_read(inode))) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
}

static int f2fs_getattr(struct vfsmount *mnt,
			 struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blocks <<= 3;
	return 0;
}

#ifdef CONFIG_F2FS_FS_POSIX_ACL
static void __setattr_copy(struct inode *inode, const struct iattr *attr)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
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
		set_acl_inode(fi, mode);
	}
}
#else
#define __setattr_copy setattr_copy
#endif

int f2fs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct f2fs_inode_info *fi = F2FS_I(inode);
	int err;

	err = inode_change_ok(inode, attr);
	if (err)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) &&
			attr->ia_size != i_size_read(inode)) {
		truncate_setsize(inode, attr->ia_size);
		f2fs_truncate(inode);
		f2fs_balance_fs(F2FS_SB(inode->i_sb));
	}

	__setattr_copy(inode, attr);

	if (attr->ia_valid & ATTR_MODE) {
		err = f2fs_acl_chmod(inode);
		if (err || is_inode_flag_set(fi, FI_ACL_MODE)) {
			inode->i_mode = fi->i_acl_mode;
			clear_inode_flag(fi, FI_ACL_MODE);
		}
	}

	mark_inode_dirty(inode);
	return err;
}

const struct inode_operations f2fs_file_inode_operations = {
	.getattr	= f2fs_getattr,
	.setattr	= f2fs_setattr,
	.get_acl	= f2fs_get_acl,
#ifdef CONFIG_F2FS_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= f2fs_listxattr,
	.removexattr	= generic_removexattr,
#endif
};

static void fill_zero(struct inode *inode, pgoff_t index,
					loff_t start, loff_t len)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	struct page *page;
	int ilock;

	if (!len)
		return;

	f2fs_balance_fs(sbi);

	ilock = mutex_lock_op(sbi);
	page = get_new_data_page(inode, NULL, index, false);
	mutex_unlock_op(sbi, ilock);

	if (!IS_ERR(page)) {
		wait_on_page_writeback(page);
		zero_user(page, start, len);
		set_page_dirty(page);
		f2fs_put_page(page, 1);
	}
}

int truncate_hole(struct inode *inode, pgoff_t pg_start, pgoff_t pg_end)
{
	pgoff_t index;
	int err;

	for (index = pg_start; index < pg_end; index++) {
		struct dnode_of_data dn;

		set_new_dnode(&dn, inode, NULL, NULL, 0);
		err = get_dnode_of_data(&dn, index, LOOKUP_NODE);
		if (err) {
			if (err == -ENOENT)
				continue;
			return err;
		}

		if (dn.data_blkaddr != NULL_ADDR)
			truncate_data_blocks_range(&dn, 1);
		f2fs_put_dnode(&dn);
	}
	return 0;
}

static int punch_hole(struct inode *inode, loff_t offset, loff_t len, int mode)
{
	pgoff_t pg_start, pg_end;
	loff_t off_start, off_end;
	int ret = 0;

	pg_start = ((unsigned long long) offset) >> PAGE_CACHE_SHIFT;
	pg_end = ((unsigned long long) offset + len) >> PAGE_CACHE_SHIFT;

	off_start = offset & (PAGE_CACHE_SIZE - 1);
	off_end = (offset + len) & (PAGE_CACHE_SIZE - 1);

	if (pg_start == pg_end) {
		fill_zero(inode, pg_start, off_start,
						off_end - off_start);
	} else {
		if (off_start)
			fill_zero(inode, pg_start++, off_start,
					PAGE_CACHE_SIZE - off_start);
		if (off_end)
			fill_zero(inode, pg_end, 0, off_end);

		if (pg_start < pg_end) {
			struct address_space *mapping = inode->i_mapping;
			loff_t blk_start, blk_end;
			struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
			int ilock;

			f2fs_balance_fs(sbi);

			blk_start = pg_start << PAGE_CACHE_SHIFT;
			blk_end = pg_end << PAGE_CACHE_SHIFT;
			truncate_inode_pages_range(mapping, blk_start,
					blk_end - 1);

			ilock = mutex_lock_op(sbi);
			ret = truncate_hole(inode, pg_start, pg_end);
			mutex_unlock_op(sbi, ilock);
		}
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) &&
		i_size_read(inode) <= (offset + len)) {
		i_size_write(inode, offset);
		mark_inode_dirty(inode);
	}

	return ret;
}

static int expand_inode_data(struct inode *inode, loff_t offset,
					loff_t len, int mode)
{
	struct f2fs_sb_info *sbi = F2FS_SB(inode->i_sb);
	pgoff_t index, pg_start, pg_end;
	loff_t new_size = i_size_read(inode);
	loff_t off_start, off_end;
	int ret = 0;

	ret = inode_newsize_ok(inode, (len + offset));
	if (ret)
		return ret;

	pg_start = ((unsigned long long) offset) >> PAGE_CACHE_SHIFT;
	pg_end = ((unsigned long long) offset + len) >> PAGE_CACHE_SHIFT;

	off_start = offset & (PAGE_CACHE_SIZE - 1);
	off_end = (offset + len) & (PAGE_CACHE_SIZE - 1);

	for (index = pg_start; index <= pg_end; index++) {
		struct dnode_of_data dn;
		int ilock;

		ilock = mutex_lock_op(sbi);
		set_new_dnode(&dn, inode, NULL, NULL, 0);
		ret = get_dnode_of_data(&dn, index, ALLOC_NODE);
		if (ret) {
			mutex_unlock_op(sbi, ilock);
			break;
		}

		if (dn.data_blkaddr == NULL_ADDR) {
			ret = reserve_new_block(&dn);
			if (ret) {
				f2fs_put_dnode(&dn);
				mutex_unlock_op(sbi, ilock);
				break;
			}
		}
		f2fs_put_dnode(&dn);
		mutex_unlock_op(sbi, ilock);

		if (pg_start == pg_end)
			new_size = offset + len;
		else if (index == pg_start && off_start)
			new_size = (index + 1) << PAGE_CACHE_SHIFT;
		else if (index == pg_end)
			new_size = (index << PAGE_CACHE_SHIFT) + off_end;
		else
			new_size += PAGE_CACHE_SIZE;
	}

	if (!(mode & FALLOC_FL_KEEP_SIZE) &&
		i_size_read(inode) < new_size) {
		i_size_write(inode, new_size);
		mark_inode_dirty(inode);
	}

	return ret;
}

static long f2fs_fallocate(struct file *file, int mode,
				loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	long ret;

	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	if (mode & FALLOC_FL_PUNCH_HOLE)
		ret = punch_hole(inode, offset, len, mode);
	else
		ret = expand_inode_data(inode, offset, len, mode);

	if (!ret) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
	}
	trace_f2fs_fallocate(inode, mode, offset, len, ret);
	return ret;
}

#define F2FS_REG_FLMASK		(~(FS_DIRSYNC_FL | FS_TOPDIR_FL))
#define F2FS_OTHER_FLMASK	(FS_NODUMP_FL | FS_NOATIME_FL)

static inline __u32 f2fs_mask_flags(umode_t mode, __u32 flags)
{
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & F2FS_REG_FLMASK;
	else
		return flags & F2FS_OTHER_FLMASK;
}

long f2fs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct f2fs_inode_info *fi = F2FS_I(inode);
	unsigned int flags;
	int ret;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		flags = fi->i_flags & FS_FL_USER_VISIBLE;
		return put_user(flags, (int __user *) arg);
	case FS_IOC_SETFLAGS:
	{
		unsigned int oldflags;

		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;

		if (!inode_owner_or_capable(inode)) {
			ret = -EACCES;
			goto out;
		}

		if (get_user(flags, (int __user *) arg)) {
			ret = -EFAULT;
			goto out;
		}

		flags = f2fs_mask_flags(inode->i_mode, flags);

		mutex_lock(&inode->i_mutex);

		oldflags = fi->i_flags;

		if ((flags ^ oldflags) & (FS_APPEND_FL | FS_IMMUTABLE_FL)) {
			if (!capable(CAP_LINUX_IMMUTABLE)) {
				mutex_unlock(&inode->i_mutex);
				ret = -EPERM;
				goto out;
			}
		}

		flags = flags & FS_FL_USER_MODIFIABLE;
		flags |= oldflags & ~FS_FL_USER_MODIFIABLE;
		fi->i_flags = flags;
		mutex_unlock(&inode->i_mutex);

		f2fs_set_inode_flags(inode);
		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
out:
		mnt_drop_write_file(filp);
		return ret;
	}
	default:
		return -ENOTTY;
	}
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
	default:
		return -ENOIOCTLCMD;
	}
	return f2fs_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

const struct file_operations f2fs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.open		= generic_file_open,
	.mmap		= f2fs_file_mmap,
	.fsync		= f2fs_sync_file,
	.fallocate	= f2fs_fallocate,
	.unlocked_ioctl	= f2fs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= f2fs_compat_ioctl,
#endif
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
};
