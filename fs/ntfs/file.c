// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel file operations.
 *
 * Copyright (c) 2001-2015 Anton Altaparmakov and Tuxera Inc.
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/uio.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/compat.h>
#include <linux/falloc.h>

#include "lcnalloc.h"
#include "ntfs.h"
#include "reparse.h"
#include "ea.h"
#include "iomap.h"
#include "bitmap.h"

#include <linux/filelock.h>

/*
 * ntfs_file_open - called when an inode is about to be opened
 * @vi:		inode to be opened
 * @filp:	file structure describing the inode
 *
 * Limit file size to the page cache limit on architectures where unsigned long
 * is 32-bits. This is the most we can do for now without overflowing the page
 * cache page index. Doing it this way means we don't run into problems because
 * of existing too large files. It would be better to allow the user to read
 * the beginning of the file but I doubt very much anyone is going to hit this
 * check on a 32-bit architecture, so there is no point in adding the extra
 * complexity required to support this.
 *
 * On 64-bit architectures, the check is hopefully optimized away by the
 * compiler.
 *
 * After the check passes, just call generic_file_open() to do its work.
 */
static int ntfs_file_open(struct inode *vi, struct file *filp)
{
	struct ntfs_inode *ni = NTFS_I(vi);

	if (NVolShutdown(ni->vol))
		return -EIO;

	if (sizeof(unsigned long) < 8) {
		if (i_size_read(vi) > MAX_LFS_FILESIZE)
			return -EOVERFLOW;
	}

	filp->f_mode |= FMODE_NOWAIT | FMODE_CAN_ODIRECT;

	return generic_file_open(vi, filp);
}

/*
 * Trim preallocated space on file release.
 *
 * When the preallo_size mount option is set (default 64KB), writes extend
 * allocated_size and runlist in units of preallocated size to reduce
 * runlist merge overhead for small writes. This can leave
 * allocated_size > data_size if not all preallocated space is used.
 *
 * We perform the trim here because ->release() is called only when
 * the file is no longer open. At this point, no further writes can occur,
 * so it is safe to reclaim the unused preallocated space.
 *
 * Returns 0 on success, or negative error on failure.
 */
static int ntfs_trim_prealloc(struct inode *vi)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_volume *vol = ni->vol;
	struct runlist_element *rl;
	s64 aligned_data_size;
	s64 vcn_ds, vcn_tr;
	ssize_t rc;
	int err = 0;

	inode_lock(vi);
	mutex_lock(&ni->mrec_lock);
	down_write(&ni->runlist.lock);

	aligned_data_size = round_up(ni->data_size, vol->cluster_size);
	if (aligned_data_size >= ni->allocated_size)
		goto out_unlock;

	vcn_ds = ntfs_bytes_to_cluster(vol, aligned_data_size);
	vcn_tr = -1;
	rc = ni->runlist.count - 2;
	rl = ni->runlist.rl;

	while (rc >= 0 && rl[rc].lcn == LCN_HOLE && vcn_ds <= rl[rc].vcn) {
		vcn_tr = rl[rc].vcn;
		rc--;
	}

	if (vcn_tr >= 0) {
		err = ntfs_rl_truncate_nolock(vol, &ni->runlist, vcn_tr);
		if (err) {
			kvfree(ni->runlist.rl);
			ni->runlist.rl = NULL;
			ntfs_error(vol->sb, "Preallocated block rollback failed");
		} else {
			ni->allocated_size = ntfs_cluster_to_bytes(vol, vcn_tr);
			err = ntfs_attr_update_mapping_pairs(ni, 0);
			if (err)
				ntfs_error(vol->sb,
					   "Failed to rollback mapping pairs for prealloc");
		}
	}

out_unlock:
	up_write(&ni->runlist.lock);
	mutex_unlock(&ni->mrec_lock);
	inode_unlock(vi);

	return err;
}

static int ntfs_file_release(struct inode *vi, struct file *filp)
{
	if (!NInoCompressed(NTFS_I(vi)))
		return ntfs_trim_prealloc(vi);

	return 0;
}

/*
 * ntfs_file_fsync - sync a file to disk
 * @filp:	file to be synced
 * @start:	start offset to be synced
 * @end:	end offset to be synced
 * @datasync:	if non-zero only flush user data and not metadata
 *
 * Data integrity sync of a file to disk.  Used for fsync, fdatasync, and msync
 * system calls.  This function is inspired by fs/buffer.c::file_fsync().
 *
 * If @datasync is false, write the mft record and all associated extent mft
 * records as well as the $DATA attribute and then sync the block device.
 *
 * If @datasync is true and the attribute is non-resident, we skip the writing
 * of the mft record and all associated extent mft records (this might still
 * happen due to the write_inode_now() call).
 *
 * Also, if @datasync is true, we do not wait on the inode to be written out
 * but we always wait on the page cache pages to be written out.
 */
static int ntfs_file_fsync(struct file *filp, loff_t start, loff_t end,
			   int datasync)
{
	struct inode *vi = filp->f_mapping->host;
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_volume *vol = ni->vol;
	int err, ret = 0;
	struct inode *parent_vi, *ia_vi;
	struct ntfs_attr_search_ctx *ctx;

	ntfs_debug("Entering for inode 0x%llx.", ni->mft_no);

	if (NVolShutdown(vol))
		return -EIO;

	err = file_write_and_wait_range(filp, start, end);
	if (err)
		return err;

	if (!datasync || !NInoNonResident(NTFS_I(vi)))
		ret = __ntfs_write_inode(vi, 1);
	write_inode_now(vi, !datasync);

	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock_nested(&ni->mrec_lock, NTFS_INODE_MUTEX_NORMAL_CHILD);
	while (!(err = ntfs_attr_lookup(AT_UNUSED, NULL, 0, 0, 0, NULL, 0, ctx))) {
		if (ctx->attr->type == AT_FILE_NAME) {
			struct file_name_attr *fn = (struct file_name_attr *)((u8 *)ctx->attr +
					le16_to_cpu(ctx->attr->data.resident.value_offset));

			parent_vi = ntfs_iget(vi->i_sb, MREF_LE(fn->parent_directory));
			if (IS_ERR(parent_vi))
				continue;
			mutex_lock_nested(&NTFS_I(parent_vi)->mrec_lock, NTFS_INODE_MUTEX_NORMAL);
			ia_vi = ntfs_index_iget(parent_vi, I30, 4);
			mutex_unlock(&NTFS_I(parent_vi)->mrec_lock);
			if (IS_ERR(ia_vi)) {
				iput(parent_vi);
				continue;
			}
			write_inode_now(ia_vi, 1);
			iput(ia_vi);
			write_inode_now(parent_vi, 1);
			iput(parent_vi);
		} else if (ctx->attr->non_resident) {
			struct inode *attr_vi;
			__le16 *name;

			name = (__le16 *)((u8 *)ctx->attr + le16_to_cpu(ctx->attr->name_offset));
			if (ctx->attr->type == AT_DATA && ctx->attr->name_length == 0)
				continue;

			attr_vi = ntfs_attr_iget(vi, ctx->attr->type,
						 name, ctx->attr->name_length);
			if (IS_ERR(attr_vi))
				continue;
			spin_lock(&attr_vi->i_lock);
			if (inode_state_read_once(attr_vi) & I_DIRTY_PAGES) {
				spin_unlock(&attr_vi->i_lock);
				filemap_write_and_wait(attr_vi->i_mapping);
			} else
				spin_unlock(&attr_vi->i_lock);
			iput(attr_vi);
		}
	}
	mutex_unlock(&ni->mrec_lock);
	ntfs_attr_put_search_ctx(ctx);

	write_inode_now(vol->mftbmp_ino, 1);
	down_write(&vol->lcnbmp_lock);
	write_inode_now(vol->lcnbmp_ino, 1);
	up_write(&vol->lcnbmp_lock);
	write_inode_now(vol->mft_ino, 1);

	/*
	 * NOTE: If we were to use mapping->private_list (see ext2 and
	 * fs/buffer.c) for dirty blocks then we could optimize the below to be
	 * sync_mapping_buffers(vi->i_mapping).
	 */
	err = sync_blockdev(vi->i_sb->s_bdev);
	if (unlikely(err && !ret))
		ret = err;
	if (likely(!ret))
		ntfs_debug("Done.");
	else
		ntfs_warning(vi->i_sb,
				"Failed to f%ssync inode 0x%llx.  Error %u.",
				datasync ? "data" : "", ni->mft_no, -ret);
	if (!ret)
		blkdev_issue_flush(vi->i_sb->s_bdev);
	return ret;
}

static int ntfs_setattr_size(struct inode *vi, struct iattr *attr)
{
	struct ntfs_inode *ni = NTFS_I(vi);
	int err;
	loff_t old_size = vi->i_size;

	if (NInoCompressed(ni) || NInoEncrypted(ni)) {
		ntfs_warning(vi->i_sb,
			"Changes in inode size are not supported yet for %s files, ignoring.",
			NInoCompressed(ni) ? "compressed" : "encrypted");
		return -EOPNOTSUPP;
	}

	err = inode_newsize_ok(vi, attr->ia_size);
	if (err)
		return err;

	inode_dio_wait(vi);
	/* Serialize against page faults */
	if (NInoNonResident(NTFS_I(vi)) && attr->ia_size < old_size) {
		err = iomap_truncate_page(vi, attr->ia_size, NULL,
				&ntfs_read_iomap_ops,
				&ntfs_iomap_folio_ops, NULL);
		if (err)
			return err;
	}

	truncate_setsize(vi, attr->ia_size);
	err = ntfs_truncate_vfs(vi, attr->ia_size, old_size);
	if (err) {
		i_size_write(vi, old_size);
		return err;
	}

	if (NInoNonResident(ni) && attr->ia_size > old_size &&
	    old_size % PAGE_SIZE != 0) {
		loff_t len = min_t(loff_t,
				round_up(old_size, PAGE_SIZE) - old_size,
				attr->ia_size - old_size);
		err = iomap_zero_range(vi, old_size, len,
				NULL, &ntfs_seek_iomap_ops,
				&ntfs_iomap_folio_ops, NULL);
	}

	return err;
}

/*
 * ntfs_setattr
 *
 * Called from notify_change() when an attribute is being changed.
 *
 * NOTE: Changes in inode size are not supported yet for compressed or
 * encrypted files.
 */
int ntfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct iattr *attr)
{
	struct inode *vi = d_inode(dentry);
	int err;
	unsigned int ia_valid = attr->ia_valid;
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_volume *vol = ni->vol;

	if (NVolShutdown(vol))
		return -EIO;

	err = setattr_prepare(idmap, dentry, attr);
	if (err)
		goto out;

	if (!(vol->vol_flags & VOLUME_IS_DIRTY))
		ntfs_set_volume_flags(vol, VOLUME_IS_DIRTY);

	if (ia_valid & ATTR_SIZE) {
		err = ntfs_setattr_size(vi, attr);
		if (err)
			goto out;

		ia_valid |= ATTR_MTIME | ATTR_CTIME;
	}

	setattr_copy(idmap, vi, attr);

	if (vol->sb->s_flags & SB_POSIXACL && !S_ISLNK(vi->i_mode)) {
		err = posix_acl_chmod(idmap, dentry, vi->i_mode);
		if (err)
			goto out;
	}

	if (0222 & vi->i_mode)
		ni->flags &= ~FILE_ATTR_READONLY;
	else
		ni->flags |= FILE_ATTR_READONLY;

	if (ia_valid & (ATTR_UID | ATTR_GID | ATTR_MODE)) {
		unsigned int flags = 0;

		if (ia_valid & ATTR_UID)
			flags |= NTFS_EA_UID;
		if (ia_valid & ATTR_GID)
			flags |= NTFS_EA_GID;
		if (ia_valid & ATTR_MODE)
			flags |= NTFS_EA_MODE;

		if (S_ISDIR(vi->i_mode))
			vi->i_mode &= ~vol->dmask;
		else
			vi->i_mode &= ~vol->fmask;

		mutex_lock(&ni->mrec_lock);
		ntfs_ea_set_wsl_inode(vi, 0, NULL, flags);
		mutex_unlock(&ni->mrec_lock);
	}

	mark_inode_dirty(vi);
out:
	return err;
}

int ntfs_getattr(struct mnt_idmap *idmap, const struct path *path,
		struct kstat *stat, unsigned int request_mask,
		unsigned int query_flags)
{
	struct inode *inode = d_backing_inode(path->dentry);
	struct ntfs_inode *ni = NTFS_I(inode);

	generic_fillattr(idmap, request_mask, inode, stat);

	stat->blksize = NTFS_SB(inode->i_sb)->cluster_size;
	stat->blocks = (((u64)NTFS_I(inode)->i_dealloc_clusters <<
			NTFS_SB(inode->i_sb)->cluster_size_bits) >> 9) + inode->i_blocks;
	stat->result_mask |= STATX_BTIME;
	stat->btime = NTFS_I(inode)->i_crtime;

	if (NInoCompressed(ni))
		stat->attributes |= STATX_ATTR_COMPRESSED;

	if (NInoEncrypted(ni))
		stat->attributes |= STATX_ATTR_ENCRYPTED;

	if (inode->i_flags & S_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;

	if (inode->i_flags & S_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;

	stat->attributes_mask |= STATX_ATTR_COMPRESSED | STATX_ATTR_ENCRYPTED |
				 STATX_ATTR_IMMUTABLE | STATX_ATTR_APPEND;

	/*
	 * If it's a compressed or encrypted file, NTFS currently
	 * does not support DIO. For normal files, we report the bdev
	 * logical block size.
	 */
	if (request_mask & STATX_DIOALIGN && S_ISREG(inode->i_mode)) {
		unsigned int align =
			bdev_logical_block_size(inode->i_sb->s_bdev);

		stat->result_mask |= STATX_DIOALIGN;
		if (!NInoCompressed(ni) && !NInoEncrypted(ni)) {
			stat->dio_mem_align = align;
			stat->dio_offset_align = align;
		}
	}

	return 0;
}

static loff_t ntfs_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;

	switch (whence) {
	case SEEK_HOLE:
		inode_lock_shared(inode);
		offset = iomap_seek_hole(inode, offset, &ntfs_seek_iomap_ops);
		inode_unlock_shared(inode);
		break;
	case SEEK_DATA:
		inode_lock_shared(inode);
		offset = iomap_seek_data(inode, offset, &ntfs_seek_iomap_ops);
		inode_unlock_shared(inode);
		break;
	default:
		return generic_file_llseek_size(file, offset, whence,
						inode->i_sb->s_maxbytes,
						i_size_read(inode));
	}
	if (offset < 0)
		return offset;
	return vfs_setpos(file, offset, inode->i_sb->s_maxbytes);
}

static ssize_t ntfs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *vi = file_inode(iocb->ki_filp);
	struct super_block *sb = vi->i_sb;
	ssize_t ret;

	if (NVolShutdown(NTFS_SB(sb)))
		return -EIO;

	if (NInoCompressed(NTFS_I(vi)) && iocb->ki_flags & IOCB_DIRECT)
		return -EOPNOTSUPP;

	inode_lock_shared(vi);

	if (iocb->ki_flags & IOCB_DIRECT) {
		size_t count = iov_iter_count(to);

		if ((iocb->ki_pos | count) & (sb->s_blocksize - 1)) {
			ret = -EINVAL;
			goto inode_unlock;
		}

		file_accessed(iocb->ki_filp);
		ret = iomap_dio_rw(iocb, to, &ntfs_read_iomap_ops, NULL, 0,
				NULL, 0);
	} else {
		ret = generic_file_read_iter(iocb, to);
	}

inode_unlock:
	inode_unlock_shared(vi);

	return ret;
}

static int ntfs_file_write_dio_end_io(struct kiocb *iocb, ssize_t size,
		int error, unsigned int flags)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	if (error)
		return error;

	if (size) {
		if (i_size_read(inode) < iocb->ki_pos + size) {
			i_size_write(inode, iocb->ki_pos + size);
			mark_inode_dirty(inode);
		}
	}

	return 0;
}

static const struct iomap_dio_ops ntfs_write_dio_ops = {
	.end_io			= ntfs_file_write_dio_end_io,
};

static ssize_t ntfs_dio_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t ret;

	ret = iomap_dio_rw(iocb, from, &ntfs_dio_iomap_ops,
			&ntfs_write_dio_ops, 0, NULL, 0);
	if (ret == -ENOTBLK)
		ret = 0;
	else if (ret < 0)
		goto out;

	if (iov_iter_count(from)) {
		loff_t offset, end;
		ssize_t written;
		int ret2;

		offset = iocb->ki_pos;
		iocb->ki_flags &= ~IOCB_DIRECT;
		written = iomap_file_buffered_write(iocb, from,
				&ntfs_write_iomap_ops, &ntfs_iomap_folio_ops,
				NULL);
		if (written < 0) {
			ret = written;
			goto out;
		}

		ret += written;
		end = iocb->ki_pos + written - 1;
		ret2 = filemap_write_and_wait_range(iocb->ki_filp->f_mapping,
				offset, end);
		if (ret2) {
			ret = -EIO;
			goto out;
		}
		if (!ret2)
			invalidate_mapping_pages(iocb->ki_filp->f_mapping,
						 offset >> PAGE_SHIFT,
						 end >> PAGE_SHIFT);
	}

out:
	return ret;
}

static ssize_t ntfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *vi = file->f_mapping->host;
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_volume *vol = ni->vol;
	ssize_t ret;
	ssize_t count;
	loff_t pos;
	int err;
	loff_t old_data_size, old_init_size;

	if (NVolShutdown(vol))
		return -EIO;

	if (NInoEncrypted(ni)) {
		ntfs_error(vi->i_sb, "Writing for %s files is not supported yet",
			   NInoCompressed(ni) ? "Compressed" : "Encrypted");
		return -EOPNOTSUPP;
	}

	if (NInoCompressed(ni) && iocb->ki_flags & IOCB_DIRECT)
		return -EOPNOTSUPP;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!inode_trylock(vi))
			return -EAGAIN;
	} else
		inode_lock(vi);

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out_lock;

	err = file_modified(iocb->ki_filp);
	if (err) {
		ret = err;
		goto out_lock;
	}

	if (!(vol->vol_flags & VOLUME_IS_DIRTY))
		ntfs_set_volume_flags(vol, VOLUME_IS_DIRTY);

	pos = iocb->ki_pos;
	count = ret;

	old_data_size = ni->data_size;
	old_init_size = ni->initialized_size;

	if (NInoNonResident(ni) && NInoCompressed(ni)) {
		ret = ntfs_compress_write(ni, pos, count, from);
		if (ret > 0)
			iocb->ki_pos += ret;
		goto out;
	}

	if (NInoNonResident(ni) && iocb->ki_flags & IOCB_DIRECT)
		ret = ntfs_dio_write_iter(iocb, from);
	else
		ret = iomap_file_buffered_write(iocb, from, &ntfs_write_iomap_ops,
				&ntfs_iomap_folio_ops, NULL);
out:
	if (ret < 0 && ret != -EIOCBQUEUED) {
		if (ni->initialized_size != old_init_size) {
			mutex_lock(&ni->mrec_lock);
			ntfs_attr_set_initialized_size(ni, old_init_size);
			mutex_unlock(&ni->mrec_lock);
		}
		if (ni->data_size != old_data_size) {
			truncate_setsize(vi, old_data_size);
			ntfs_attr_truncate(ni, old_data_size);
		}
	}
out_lock:
	inode_unlock(vi);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}

static vm_fault_t ntfs_filemap_page_mkwrite(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	vm_fault_t ret;

	sb_start_pagefault(inode->i_sb);
	file_update_time(vmf->vma->vm_file);

	ret = iomap_page_mkwrite(vmf, &ntfs_page_mkwrite_iomap_ops, NULL);
	sb_end_pagefault(inode->i_sb);
	return ret;
}

static const struct vm_operations_struct ntfs_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= ntfs_filemap_page_mkwrite,
};

static int ntfs_file_mmap_prepare(struct vm_area_desc *desc)
{
	struct file *file = desc->file;
	struct inode *inode = file_inode(file);

	if (NVolShutdown(NTFS_SB(file->f_mapping->host->i_sb)))
		return -EIO;

	if (NInoCompressed(NTFS_I(inode)))
		return -EOPNOTSUPP;

	if (vma_desc_test_flags(desc, VMA_WRITE_BIT)) {
		struct inode *inode = file_inode(file);
		loff_t from, to;
		int err;

		from = ((loff_t)desc->pgoff << PAGE_SHIFT);
		to = min_t(loff_t, i_size_read(inode),
			   from + desc->end - desc->start);

		if (NTFS_I(inode)->initialized_size < to) {
			err = ntfs_extend_initialized_size(inode, to, to, false);
			if (err)
				return err;
		}
	}


	file_accessed(file);
	desc->vm_ops = &ntfs_file_vm_ops;
	return 0;
}

static int ntfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len)
{
	return iomap_fiemap(inode, fieinfo, start, len, &ntfs_read_iomap_ops);
}

static const char *ntfs_get_link(struct dentry *dentry, struct inode *inode,
		struct delayed_call *done)
{
	if (!NTFS_I(inode)->target)
		return ERR_PTR(-EINVAL);

	return NTFS_I(inode)->target;
}

static ssize_t ntfs_file_splice_read(struct file *in, loff_t *ppos,
		struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
	if (NVolShutdown(NTFS_SB(in->f_mapping->host->i_sb)))
		return -EIO;

	return filemap_splice_read(in, ppos, pipe, len, flags);
}

static int ntfs_ioctl_shutdown(struct super_block *sb, unsigned long arg)
{
	u32 flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(flags, (__u32 __user *)arg))
		return -EFAULT;

	return ntfs_force_shutdown(sb, flags);
}

static int ntfs_ioctl_get_volume_label(struct file *filp, unsigned long arg)
{
	struct ntfs_volume *vol = NTFS_SB(file_inode(filp)->i_sb);
	char __user *buf = (char __user *)arg;

	if (!vol->volume_label) {
		if (copy_to_user(buf, "", 1))
			return -EFAULT;
	} else if (copy_to_user(buf, vol->volume_label,
				MIN(FSLABEL_MAX, strlen(vol->volume_label) + 1)))
		return -EFAULT;
	return 0;
}

static int ntfs_ioctl_set_volume_label(struct file *filp, unsigned long arg)
{
	struct ntfs_volume *vol = NTFS_SB(file_inode(filp)->i_sb);
	char *label;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	label = strndup_user((const char __user *)arg, FSLABEL_MAX);
	if (IS_ERR(label))
		return PTR_ERR(label);

	ret = mnt_want_write_file(filp);
	if (ret)
		goto out;

	ret = ntfs_write_volume_label(vol, label);
	mnt_drop_write_file(filp);
out:
	kfree(label);
	return ret;
}

static int ntfs_ioctl_fitrim(struct ntfs_volume *vol, unsigned long arg)
{
	struct fstrim_range __user *user_range;
	struct fstrim_range range;
	struct block_device *dev;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dev = vol->sb->s_bdev;
	if (!bdev_max_discard_sectors(dev))
		return -EOPNOTSUPP;

	user_range = (struct fstrim_range __user *)arg;
	if (copy_from_user(&range, user_range, sizeof(range)))
		return -EFAULT;

	if (range.len == 0)
		return -EINVAL;

	if (range.len < vol->cluster_size)
		return -EINVAL;

	range.minlen = max_t(u32, range.minlen, bdev_discard_granularity(dev));

	err = ntfs_trim_fs(vol, &range);
	if (err < 0)
		return err;

	if (copy_to_user(user_range, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

long ntfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC_SHUTDOWN:
		return ntfs_ioctl_shutdown(file_inode(filp)->i_sb, arg);
	case FS_IOC_GETFSLABEL:
		return ntfs_ioctl_get_volume_label(filp, arg);
	case FS_IOC_SETFSLABEL:
		return ntfs_ioctl_set_volume_label(filp, arg);
	case FITRIM:
		return ntfs_ioctl_fitrim(NTFS_SB(file_inode(filp)->i_sb), arg);
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long ntfs_compat_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	return ntfs_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static int ntfs_allocate_range(struct ntfs_inode *ni, int mode, loff_t offset,
		loff_t len)
{
	struct inode *vi = VFS_I(ni);
	struct ntfs_volume *vol = ni->vol;
	s64 need_space;
	loff_t old_size, new_size;
	s64 start_vcn, end_vcn;
	int err;

	old_size = i_size_read(vi);
	new_size = max_t(loff_t, old_size, offset + len);
	start_vcn = ntfs_bytes_to_cluster(vol, offset);
	end_vcn = ntfs_bytes_to_cluster(vol, offset + len - 1) + 1;

	err = inode_newsize_ok(vi, new_size);
	if (err)
		goto out;

	need_space = ntfs_bytes_to_cluster(vol, ni->allocated_size);
	if (need_space > start_vcn)
		need_space = end_vcn - need_space;
	else
		need_space = end_vcn - start_vcn;
	if (need_space > 0 &&
	    need_space > (atomic64_read(&vol->free_clusters) -
			  atomic64_read(&vol->dirty_clusters))) {
		err = -ENOSPC;
		goto out;
	}

	err = ntfs_attr_fallocate(ni, offset, len,
			mode & FALLOC_FL_KEEP_SIZE ? true : false);

	if (!(mode & FALLOC_FL_KEEP_SIZE) && new_size != old_size)
		i_size_write(vi, ni->data_size);
out:
	return err;
}

static int ntfs_punch_hole(struct ntfs_inode *ni, int mode, loff_t offset,
		loff_t len)
{
	struct ntfs_volume *vol = ni->vol;
	struct inode *vi = VFS_I(ni);
	loff_t end_offset;
	s64 start_vcn, end_vcn;
	int err = 0;

	loff_t offset_down = round_down(offset, max_t(unsigned int,
				vol->cluster_size, PAGE_SIZE));

	if (NVolDisableSparse(vol)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (offset >= ni->data_size)
		goto out;

	if (offset + len > ni->data_size)
		end_offset = ni->data_size;
	else
		end_offset = offset + len;

	err = filemap_write_and_wait_range(vi->i_mapping, offset_down, LLONG_MAX);
	if (err)
		goto out;
	truncate_pagecache(vi, offset_down);

	start_vcn = ntfs_bytes_to_cluster(vol, offset);
	end_vcn = ntfs_bytes_to_cluster(vol, end_offset - 1) + 1;

	if (offset & vol->cluster_size_mask) {
		loff_t to;

		to = min_t(loff_t, ntfs_cluster_to_bytes(vol, start_vcn + 1),
				end_offset);
		err = iomap_zero_range(vi, offset, to - offset, NULL,
				&ntfs_seek_iomap_ops,
				&ntfs_iomap_folio_ops, NULL);
		if (err < 0 || (end_vcn - start_vcn) == 1)
			goto out;
		start_vcn++;
	}

	if (end_offset & vol->cluster_size_mask) {
		loff_t from;

		from = ntfs_cluster_to_bytes(vol, end_vcn - 1);
		err = iomap_zero_range(vi, from, end_offset - from, NULL,
				&ntfs_seek_iomap_ops,
				&ntfs_iomap_folio_ops, NULL);
		if (err < 0 || (end_vcn - start_vcn) == 1)
			goto out;
		end_vcn--;
	}

	mutex_lock_nested(&ni->mrec_lock, NTFS_INODE_MUTEX_NORMAL);
	err = ntfs_non_resident_attr_punch_hole(ni, start_vcn,
			end_vcn - start_vcn);
	mutex_unlock(&ni->mrec_lock);
out:
	return err;
}

static int ntfs_collapse_range(struct ntfs_inode *ni, loff_t offset, loff_t len)
{
	struct ntfs_volume *vol = ni->vol;
	struct inode *vi = VFS_I(ni);
	loff_t old_size, new_size;
	s64 start_vcn, end_vcn;
	int err;

	loff_t offset_down = round_down(offset,
			max_t(unsigned long, vol->cluster_size, PAGE_SIZE));

	if ((offset & vol->cluster_size_mask) ||
	    (len & vol->cluster_size_mask) ||
	    offset >= ni->allocated_size) {
		err = -EINVAL;
		goto out;
	}

	old_size = i_size_read(vi);
	start_vcn = ntfs_bytes_to_cluster(vol, offset);
	end_vcn = ntfs_bytes_to_cluster(vol, offset + len - 1) + 1;

	if (ntfs_cluster_to_bytes(vol, end_vcn) > ni->allocated_size)
		end_vcn = (round_up(ni->allocated_size - 1,
			   vol->cluster_size) >> vol->cluster_size_bits) + 1;
	new_size = old_size - ntfs_cluster_to_bytes(vol, end_vcn - start_vcn);
	if (new_size < 0)
		new_size = 0;
	err = filemap_write_and_wait_range(vi->i_mapping,
			offset_down, LLONG_MAX);
	if (err)
		goto out;

	truncate_pagecache(vi, offset_down);

	mutex_lock_nested(&ni->mrec_lock, NTFS_INODE_MUTEX_NORMAL);
	err = ntfs_non_resident_attr_collapse_range(ni, start_vcn,
			end_vcn - start_vcn);
	mutex_unlock(&ni->mrec_lock);

	if (new_size != old_size)
		i_size_write(vi, ni->data_size);
out:
	return err;
}

static int ntfs_insert_range(struct ntfs_inode *ni, loff_t offset, loff_t len)
{
	struct ntfs_volume *vol = ni->vol;
	struct inode *vi = VFS_I(ni);
	loff_t offset_down = round_down(offset,
			max_t(unsigned long, vol->cluster_size, PAGE_SIZE));
	loff_t alloc_size, end_offset = offset + len;
	loff_t old_size, new_size;
	s64 start_vcn, end_vcn;
	int err;

	if (NVolDisableSparse(vol)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if ((offset & vol->cluster_size_mask) ||
	    (len & vol->cluster_size_mask) ||
	     offset >= ni->allocated_size) {
		err = -EINVAL;
		goto out;
	}

	old_size = i_size_read(vi);
	start_vcn = ntfs_bytes_to_cluster(vol, offset);
	end_vcn = ntfs_bytes_to_cluster(vol, end_offset - 1) + 1;

	new_size = old_size + ntfs_cluster_to_bytes(vol, end_vcn - start_vcn);
	alloc_size = ni->allocated_size +
		ntfs_cluster_to_bytes(vol, end_vcn - start_vcn);
	if (alloc_size < 0) {
		err = -EFBIG;
		goto out;
	}
	err = inode_newsize_ok(vi, alloc_size);
	if (err)
		goto out;

	err = filemap_write_and_wait_range(vi->i_mapping,
			offset_down, LLONG_MAX);
	if (err)
		goto out;

	truncate_pagecache(vi, offset_down);

	mutex_lock_nested(&ni->mrec_lock, NTFS_INODE_MUTEX_NORMAL);
	err = ntfs_non_resident_attr_insert_range(ni, start_vcn,
			end_vcn - start_vcn);
	mutex_unlock(&ni->mrec_lock);

	if (new_size != old_size)
		i_size_write(vi, ni->data_size);
out:
	return err;
}

#define NTFS_FALLOC_FL_SUPPORTED					\
		(FALLOC_FL_ALLOCATE_RANGE | FALLOC_FL_KEEP_SIZE |	\
		 FALLOC_FL_INSERT_RANGE | FALLOC_FL_PUNCH_HOLE |	\
		 FALLOC_FL_COLLAPSE_RANGE)

static long ntfs_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct inode *vi = file_inode(file);
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_volume *vol = ni->vol;
	int err = 0;
	loff_t old_size;
	bool map_locked = false;

	if (mode & ~(NTFS_FALLOC_FL_SUPPORTED))
		return -EOPNOTSUPP;

	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));

	if ((ni->vol->mft_zone_end - ni->vol->mft_zone_start) == 0)
		return -ENOSPC;

	if (NInoNonResident(ni) && !NInoFullyMapped(ni)) {
		down_write(&ni->runlist.lock);
		err = ntfs_attr_map_whole_runlist(ni);
		up_write(&ni->runlist.lock);
		if (err)
			return err;
	}

	if (!(vol->vol_flags & VOLUME_IS_DIRTY)) {
		err = ntfs_set_volume_flags(vol, VOLUME_IS_DIRTY);
		if (err)
			return err;
	}

	old_size = i_size_read(vi);

	inode_lock(vi);
	if (NInoCompressed(ni) || NInoEncrypted(ni)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	inode_dio_wait(vi);
	if (mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_COLLAPSE_RANGE |
		    FALLOC_FL_INSERT_RANGE)) {
		filemap_invalidate_lock(vi->i_mapping);
		map_locked = true;
	}

	switch (mode & FALLOC_FL_MODE_MASK) {
	case FALLOC_FL_ALLOCATE_RANGE:
	case FALLOC_FL_KEEP_SIZE:
		err = ntfs_allocate_range(ni, mode, offset, len);
		break;
	case FALLOC_FL_PUNCH_HOLE:
		err = ntfs_punch_hole(ni, mode, offset, len);
		break;
	case FALLOC_FL_COLLAPSE_RANGE:
		err = ntfs_collapse_range(ni, offset, len);
		break;
	case FALLOC_FL_INSERT_RANGE:
		err = ntfs_insert_range(ni, offset, len);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	if (err)
		goto out;

	err = file_modified(file);
out:
	if (map_locked)
		filemap_invalidate_unlock(vi->i_mapping);
	if (!err) {
		if (mode == 0 && NInoNonResident(ni) &&
		    offset > old_size && old_size % PAGE_SIZE != 0) {
			loff_t len = min_t(loff_t,
					   round_up(old_size, PAGE_SIZE) - old_size,
					   offset - old_size);
			err = iomap_zero_range(vi, old_size, len, NULL,
					       &ntfs_seek_iomap_ops,
					       &ntfs_iomap_folio_ops, NULL);
		}
		NInoSetFileNameDirty(ni);
		inode_set_mtime_to_ts(vi, inode_set_ctime_current(vi));
		mark_inode_dirty(vi);
	}

	inode_unlock(vi);
	return err;
}

const struct file_operations ntfs_file_ops = {
	.llseek		= ntfs_file_llseek,
	.read_iter	= ntfs_file_read_iter,
	.write_iter	= ntfs_file_write_iter,
	.fsync		= ntfs_file_fsync,
	.mmap_prepare	= ntfs_file_mmap_prepare,
	.open		= ntfs_file_open,
	.release	= ntfs_file_release,
	.splice_read	= ntfs_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.unlocked_ioctl	= ntfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ntfs_compat_ioctl,
#endif
	.fallocate	= ntfs_fallocate,
	.setlease	= generic_setlease,
};

const struct inode_operations ntfs_file_inode_ops = {
	.setattr	= ntfs_setattr,
	.getattr	= ntfs_getattr,
	.listxattr	= ntfs_listxattr,
	.get_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
	.fiemap		= ntfs_fiemap,
};

const struct inode_operations ntfs_symlink_inode_operations = {
	.get_link	= ntfs_get_link,
	.setattr	= ntfs_setattr,
	.listxattr	= ntfs_listxattr,
};

const struct inode_operations ntfs_special_inode_operations = {
	.setattr	= ntfs_setattr,
	.getattr	= ntfs_getattr,
	.listxattr	= ntfs_listxattr,
	.get_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
};

const struct file_operations ntfs_empty_file_ops = {};

const struct inode_operations ntfs_empty_inode_ops = {};
