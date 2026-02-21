// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 *  Regular file handling primitives for NTFS-based filesystems.
 *
 */

#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/compat.h>
#include <linux/falloc.h>
#include <linux/fiemap.h>
#include <linux/fileattr.h>
#include <linux/filelock.h>
#include <linux/iomap.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * cifx, btrfs, exfat, ext4, f2fs use this constant.
 * Hope this value will become common to all fs.
 */
#define NTFS3_IOC_SHUTDOWN _IOR('X', 125, __u32)

/*
 * Helper for ntfs_should_use_dio.
 */
static u32 ntfs_dio_alignment(struct inode *inode)
{
	struct ntfs_inode *ni = ntfs_i(inode);

	if (is_resident(ni)) {
		/* Check delalloc. */
		if (!ni->file.run_da.count)
			return 0;
	}

	/* In most cases this is bdev_logical_block_size(bdev). */
	return ni->mi.sbi->bdev_blocksize;
}

/*
 * Returns %true if the given DIO request should be attempted with DIO, or
 * %false if it should fall back to buffered I/O.
 */
static bool ntfs_should_use_dio(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	u32 dio_align = ntfs_dio_alignment(inode);

	if (!dio_align)
		return false;

	return IS_ALIGNED(iocb->ki_pos | iov_iter_alignment(iter), dio_align);
}

static int ntfs_ioctl_fitrim(struct ntfs_sb_info *sbi, unsigned long arg)
{
	struct fstrim_range __user *user_range;
	struct fstrim_range range;
	struct block_device *dev;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dev = sbi->sb->s_bdev;
	if (!bdev_max_discard_sectors(dev))
		return -EOPNOTSUPP;

	user_range = (struct fstrim_range __user *)arg;
	if (copy_from_user(&range, user_range, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(u32, range.minlen, bdev_discard_granularity(dev));

	err = ntfs_trim_fs(sbi, &range);
	if (err < 0)
		return err;

	if (copy_to_user(user_range, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

static int ntfs_ioctl_get_volume_label(struct ntfs_sb_info *sbi, u8 __user *buf)
{
	if (copy_to_user(buf, sbi->volume.label, FSLABEL_MAX))
		return -EFAULT;

	return 0;
}

static int ntfs_ioctl_set_volume_label(struct ntfs_sb_info *sbi, u8 __user *buf)
{
	u8 user[FSLABEL_MAX] = { 0 };
	int len;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(user, buf, FSLABEL_MAX))
		return -EFAULT;

	len = strnlen(user, FSLABEL_MAX);

	return ntfs_set_label(sbi, user, len);
}

/*
 * ntfs_force_shutdown - helper function. Called from ioctl
 */
static int ntfs_force_shutdown(struct super_block *sb, u32 flags)
{
	int err;
	struct ntfs_sb_info *sbi = sb->s_fs_info;

	if (unlikely(ntfs3_forced_shutdown(sb)))
		return 0;

	/* No additional options yet (flags). */
	err = bdev_freeze(sb->s_bdev);
	if (err)
		return err;
	set_bit(NTFS_FLAGS_SHUTDOWN_BIT, &sbi->flags);
	bdev_thaw(sb->s_bdev);
	return 0;
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

/*
 * ntfs_ioctl - file_operations::unlocked_ioctl
 */
long ntfs_ioctl(struct file *filp, u32 cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ntfs_i(inode))))
		return -EINVAL;

	switch (cmd) {
	case FITRIM:
		return ntfs_ioctl_fitrim(sbi, arg);
	case FS_IOC_GETFSLABEL:
		return ntfs_ioctl_get_volume_label(sbi, (u8 __user *)arg);
	case FS_IOC_SETFSLABEL:
		return ntfs_ioctl_set_volume_label(sbi, (u8 __user *)arg);
	case NTFS3_IOC_SHUTDOWN:
		return ntfs_ioctl_shutdown(sb, arg);
	}
	return -ENOTTY; /* Inappropriate ioctl for device. */
}

#ifdef CONFIG_COMPAT
long ntfs_compat_ioctl(struct file *filp, u32 cmd, unsigned long arg)

{
	return ntfs_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

/*
 * ntfs_getattr - inode_operations::getattr
 */
int ntfs_getattr(struct mnt_idmap *idmap, const struct path *path,
		 struct kstat *stat, u32 request_mask, u32 flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct ntfs_inode *ni = ntfs_i(inode);

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	stat->result_mask |= STATX_BTIME;
	stat->btime = ni->i_crtime;
	stat->blksize = ni->mi.sbi->cluster_size; /* 512, 1K, ..., 2M */

	if (inode->i_flags & S_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;

	if (inode->i_flags & S_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;

	if (is_compressed(ni))
		stat->attributes |= STATX_ATTR_COMPRESSED;

	if (is_encrypted(ni))
		stat->attributes |= STATX_ATTR_ENCRYPTED;

	stat->attributes_mask |= STATX_ATTR_COMPRESSED | STATX_ATTR_ENCRYPTED |
				 STATX_ATTR_IMMUTABLE | STATX_ATTR_APPEND;

	generic_fillattr(idmap, request_mask, inode, stat);

	return 0;
}

static int ntfs_extend_initialized_size(struct file *file,
					struct ntfs_inode *ni,
					const loff_t new_valid)
{
	struct inode *inode = &ni->vfs_inode;
	const loff_t valid = ni->i_valid;
	int err;

	if (valid >= new_valid)
		return 0;

	if (is_resident(ni)) {
		ni->i_valid = new_valid;
		return 0;
	}

	err = iomap_zero_range(inode, valid, new_valid - valid, NULL,
			       &ntfs_iomap_ops, &ntfs_iomap_folio_ops, NULL);
	if (err) {
		ni->i_valid = valid;
		ntfs_inode_warn(inode,
				"failed to extend initialized size to %llx.",
				new_valid);
		return err;
	}

	return 0;
}

static void ntfs_filemap_close(struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(vma->vm_file);
	struct ntfs_inode *ni = ntfs_i(inode);
	u64 from = (u64)vma->vm_pgoff << PAGE_SHIFT;
	u64 to = min_t(u64, i_size_read(inode),
		       from + vma->vm_end - vma->vm_start);

	if (ni->i_valid < to) {
		ni->i_valid = to;
		mark_inode_dirty(inode);
	}
}

/* Copy of generic_file_vm_ops. */
static const struct vm_operations_struct ntfs_file_vm_ops = {
	.close = ntfs_filemap_close,
	.fault = filemap_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = filemap_page_mkwrite,
};

/*
 * ntfs_file_mmap_prepare - file_operations::mmap_prepare
 */
static int ntfs_file_mmap_prepare(struct vm_area_desc *desc)
{
	struct file *file = desc->file;
	struct inode *inode = file_inode(file);
	struct ntfs_inode *ni = ntfs_i(inode);
	const bool rw = vma_desc_test_flags(desc, VMA_WRITE_BIT);
	int err;

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (unlikely(ntfs3_forced_shutdown(inode->i_sb)))
		return -EIO;

	if (is_encrypted(ni)) {
		ntfs_inode_warn(inode, "mmap encrypted not supported");
		return -EOPNOTSUPP;
	}

	if (is_dedup(ni)) {
		ntfs_inode_warn(inode, "mmap deduplicated not supported");
		return -EOPNOTSUPP;
	}

	if (is_compressed(ni)) {
		if (rw) {
			ntfs_inode_warn(inode,
					"mmap(write) compressed not supported");
			return -EOPNOTSUPP;
		}
		/* Turn off readahead for compressed files. */
		file->f_ra.ra_pages = 0;
	}

	if (rw) {
		u64 from = (u64)desc->pgoff << PAGE_SHIFT;
		u64 to = min_t(u64, i_size_read(inode),
			       from + vma_desc_size(desc));

		if (is_sparsed(ni)) {
			/* Allocate clusters for rw map. */
			struct ntfs_sb_info *sbi = inode->i_sb->s_fs_info;
			CLST lcn, len;
			CLST vcn = from >> sbi->cluster_bits;
			CLST end = bytes_to_cluster(sbi, to);
			bool new;

			for (; vcn < end; vcn += len) {
				err = attr_data_get_block(ni, vcn, 1, &lcn,
							  &len, &new, true,
							  NULL, false);
				if (err)
					goto out;
			}
		}

		if (ni->i_valid < to) {
			if (!inode_trylock(inode)) {
				err = -EAGAIN;
				goto out;
			}
			err = ntfs_extend_initialized_size(file, ni, to);
			inode_unlock(inode);
			if (err)
				goto out;
		}
	}

	err = generic_file_mmap_prepare(desc);
	if (!err && rw)
		desc->vm_ops = &ntfs_file_vm_ops;
out:
	return err;
}

static int ntfs_extend(struct inode *inode, loff_t pos, size_t count,
		       struct file *file)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	struct address_space *mapping = inode->i_mapping;
	loff_t end = pos + count;
	bool extend_init = file && pos > ni->i_valid;
	int err;

	if (end <= inode->i_size && !extend_init)
		return 0;

	/* Mark rw ntfs as dirty. It will be cleared at umount. */
	ntfs_set_state(ni->mi.sbi, NTFS_DIRTY_DIRTY);

	if (end > inode->i_size) {
		/*
		 * Normal files: increase file size, allocate space.
		 * Sparse/Compressed: increase file size. No space allocated.
		 */
		err = ntfs_set_size(inode, end);
		if (err)
			goto out;
	}

	if (extend_init && !is_compressed(ni)) {
		err = ntfs_extend_initialized_size(file, ni, pos);
		if (err)
			goto out;
	} else {
		err = 0;
	}

	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	mark_inode_dirty(inode);

	if (IS_SYNC(inode)) {
		int err2;

		err = filemap_fdatawrite_range(mapping, pos, end - 1);
		err2 = sync_mapping_buffers(mapping);
		if (!err)
			err = err2;
		err2 = write_inode_now(inode, 1);
		if (!err)
			err = err2;
		if (!err)
			err = filemap_fdatawait_range(mapping, pos, end - 1);
	}

out:
	return err;
}

static int ntfs_truncate(struct inode *inode, loff_t new_size)
{
	int err;
	struct ntfs_inode *ni = ntfs_i(inode);
	u64 new_valid = min_t(u64, ni->i_valid, new_size);

	truncate_setsize(inode, new_size);

	ni_lock(ni);

	down_write(&ni->file.run_lock);
	err = attr_set_size_ex(ni, ATTR_DATA, NULL, 0, &ni->file.run, new_size,
			       &new_valid, ni->mi.sbi->options->prealloc, NULL,
			       false);
	up_write(&ni->file.run_lock);

	ni->i_valid = new_valid;

	ni_unlock(ni);

	if (err)
		return err;

	ni->std_fa |= FILE_ATTRIBUTE_ARCHIVE;
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	if (!IS_DIRSYNC(inode)) {
		mark_inode_dirty(inode);
	} else {
		err = ntfs_sync_inode(inode);
		if (err)
			return err;
	}

	return 0;
}

/*
 * ntfs_fallocate - file_operations::ntfs_fallocate
 *
 * Preallocate space for a file. This implements ntfs's fallocate file
 * operation, which gets called from sys_fallocate system call. User
 * space requests 'len' bytes at 'vbo'. If FALLOC_FL_KEEP_SIZE is set
 * we just allocate clusters without zeroing them out. Otherwise we
 * allocate and zero out clusters via an expanding truncate.
 */
static long ntfs_fallocate(struct file *file, int mode, loff_t vbo, loff_t len)
{
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *ni = ntfs_i(inode);
	loff_t end = vbo + len;
	loff_t vbo_down = round_down(vbo, max_t(unsigned long,
						sbi->cluster_size, PAGE_SIZE));
	bool is_supported_holes = is_sparsed(ni) || is_compressed(ni);
	loff_t i_size, new_size;
	bool map_locked;
	int err;

	/* No support for dir. */
	if (!S_ISREG(inode->i_mode))
		return -EOPNOTSUPP;

	/*
	 * vfs_fallocate checks all possible combinations of mode.
	 * Do additional checks here before ntfs_set_state(dirty).
	 */
	if (mode & FALLOC_FL_PUNCH_HOLE) {
		if (!is_supported_holes)
			return -EOPNOTSUPP;
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		if (!is_supported_holes)
			return -EOPNOTSUPP;
	} else if (mode &
		   ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE |
		     FALLOC_FL_COLLAPSE_RANGE | FALLOC_FL_INSERT_RANGE)) {
		ntfs_inode_warn(inode, "fallocate(0x%x) is not supported",
				mode);
		return -EOPNOTSUPP;
	}

	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);

	inode_lock(inode);
	i_size = inode->i_size;
	new_size = max(end, i_size);
	map_locked = false;

	if (WARN_ON(ni->ni_flags & NI_FLAG_COMPRESSED_MASK)) {
		/* Should never be here, see ntfs_file_open. */
		err = -EOPNOTSUPP;
		goto out;
	}

	if (mode & (FALLOC_FL_PUNCH_HOLE | FALLOC_FL_COLLAPSE_RANGE |
		    FALLOC_FL_INSERT_RANGE)) {
		inode_dio_wait(inode);
		filemap_invalidate_lock(mapping);
		map_locked = true;
	}

	if (mode & FALLOC_FL_PUNCH_HOLE) {
		u32 frame_size;
		loff_t mask, vbo_a, end_a, tmp, from;

		err = filemap_write_and_wait_range(mapping, vbo_down,
						   LLONG_MAX);
		if (err)
			goto out;

		truncate_pagecache(inode, vbo_down);

		ni_lock(ni);
		err = attr_punch_hole(ni, vbo, len, &frame_size);
		ni_unlock(ni);
		if (!err)
			goto ok;

		if (err != E_NTFS_NOTALIGNED)
			goto out;

		/* Process not aligned punch. */
		err = 0;
		if (end > i_size)
			end = i_size;
		mask = frame_size - 1;
		vbo_a = (vbo + mask) & ~mask;
		end_a = end & ~mask;

		tmp = min(vbo_a, end);
		from = min_t(loff_t, ni->i_valid, vbo);
		/* Zero head of punch. */
		if (tmp > from) {
			err = iomap_zero_range(inode, from, tmp - from, NULL,
					       &ntfs_iomap_ops,
					       &ntfs_iomap_folio_ops, NULL);
			if (err)
				goto out;
		}

		/* Aligned punch_hole. Deallocate clusters. */
		if (end_a > vbo_a) {
			ni_lock(ni);
			err = attr_punch_hole(ni, vbo_a, end_a - vbo_a, NULL);
			ni_unlock(ni);
			if (err)
				goto out;
		}

		/* Zero tail of punch. */
		if (vbo < end_a && end_a < end) {
			err = iomap_zero_range(inode, end_a, end - end_a, NULL,
					       &ntfs_iomap_ops,
					       &ntfs_iomap_folio_ops, NULL);
			if (err)
				goto out;
		}
	} else if (mode & FALLOC_FL_COLLAPSE_RANGE) {
		/*
		 * Write tail of the last page before removed range since
		 * it will get removed from the page cache below.
		 */
		err = filemap_write_and_wait_range(mapping, vbo_down, vbo);
		if (err)
			goto out;

		/*
		 * Write data that will be shifted to preserve them
		 * when discarding page cache below.
		 */
		err = filemap_write_and_wait_range(mapping, end, LLONG_MAX);
		if (err)
			goto out;

		truncate_pagecache(inode, vbo_down);

		ni_lock(ni);
		err = attr_collapse_range(ni, vbo, len);
		ni_unlock(ni);
		if (err)
			goto out;
	} else if (mode & FALLOC_FL_INSERT_RANGE) {
		/* Check new size. */
		err = inode_newsize_ok(inode, new_size);
		if (err)
			goto out;

		/* Write out all dirty pages. */
		err = filemap_write_and_wait_range(mapping, vbo_down,
						   LLONG_MAX);
		if (err)
			goto out;
		truncate_pagecache(inode, vbo_down);

		ni_lock(ni);
		err = attr_insert_range(ni, vbo, len);
		ni_unlock(ni);
		if (err)
			goto out;
	} else {
		/* Check new size. */
		u8 cluster_bits = sbi->cluster_bits;

		/* Be sure file is non resident. */
		if (is_resident(ni)) {
			ni_lock(ni);
			err = attr_force_nonresident(ni);
			ni_unlock(ni);
			if (err)
				goto out;
		}

		/* generic/213: expected -ENOSPC instead of -EFBIG. */
		if (!is_supported_holes) {
			loff_t to_alloc = new_size - inode_get_bytes(inode);

			if (to_alloc > 0 &&
			    (to_alloc >> cluster_bits) >
				    wnd_zeroes(&sbi->used.bitmap)) {
				err = -ENOSPC;
				goto out;
			}
		}

		err = inode_newsize_ok(inode, new_size);
		if (err)
			goto out;

		if (new_size > i_size) {
			/*
			 * Allocate clusters, do not change 'valid' size.
			 */
			err = ntfs_set_size(inode, new_size);
			if (err)
				goto out;
		}

		if (is_supported_holes) {
			CLST vcn = vbo >> cluster_bits;
			CLST cend = bytes_to_cluster(sbi, end);
			CLST cend_v = bytes_to_cluster(sbi, ni->i_valid);
			CLST lcn, clen;
			bool new;

			if (cend_v > cend)
				cend_v = cend;

			/*
			 * Allocate and zero new clusters.
			 * Zeroing these clusters may be too long.
			 */
			for (; vcn < cend_v; vcn += clen) {
				err = attr_data_get_block(ni, vcn, cend_v - vcn,
							  &lcn, &clen, &new,
							  true, NULL, false);
				if (err)
					goto out;
			}

			/*
			 * Moving up 'valid size'.
			 */
			err = ntfs_extend_initialized_size(
				file, ni, (u64)cend_v << cluster_bits);
			if (err)
				goto out;

			/*
			 * Allocate but not zero new clusters.
			 */
			for (; vcn < cend; vcn += clen) {
				err = attr_data_get_block(ni, vcn, cend - vcn,
							  &lcn, &clen, &new,
							  false, NULL, false);
				if (err)
					goto out;
			}
		}

		if (mode & FALLOC_FL_KEEP_SIZE) {
			ni_lock(ni);
			/* True - Keep preallocated. */
			err = attr_set_size(ni, ATTR_DATA, NULL, 0,
					    &ni->file.run, i_size, &ni->i_valid,
					    true);
			ni_unlock(ni);
			if (err)
				goto out;
			i_size_write(inode, i_size);
		} else if (new_size > i_size) {
			i_size_write(inode, new_size);
		}
	}

ok:
	err = file_modified(file);
	if (err)
		goto out;

out:
	if (map_locked)
		filemap_invalidate_unlock(mapping);

	if (!err) {
		inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
		mark_inode_dirty(inode);
	}

	inode_unlock(inode);
	return err;
}

/*
 * ntfs_setattr - inode_operations::setattr
 */
int ntfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = ntfs_i(inode);
	u32 ia_valid = attr->ia_valid;
	umode_t mode = inode->i_mode;
	int err;

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (unlikely(ntfs3_forced_shutdown(inode->i_sb)))
		return -EIO;

	err = setattr_prepare(idmap, dentry, attr);
	if (err)
		goto out;

	if (ia_valid & ATTR_SIZE) {
		loff_t newsize, oldsize;

		if (WARN_ON(ni->ni_flags & NI_FLAG_COMPRESSED_MASK)) {
			/* Should never be here, see ntfs_file_open(). */
			err = -EOPNOTSUPP;
			goto out;
		}
		inode_dio_wait(inode);
		oldsize = i_size_read(inode);
		newsize = attr->ia_size;

		if (newsize <= oldsize)
			err = ntfs_truncate(inode, newsize);
		else
			err = ntfs_extend(inode, newsize, 0, NULL);

		if (err)
			goto out;

		ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
		i_size_write(inode, newsize);
	}

	setattr_copy(idmap, inode, attr);

	if (mode != inode->i_mode) {
		err = ntfs_acl_chmod(idmap, dentry);
		if (err)
			goto out;

		/* Linux 'w' -> Windows 'ro'. */
		if (0222 & inode->i_mode)
			ni->std_fa &= ~FILE_ATTRIBUTE_READONLY;
		else
			ni->std_fa |= FILE_ATTRIBUTE_READONLY;
	}

	if (ia_valid & (ATTR_UID | ATTR_GID | ATTR_MODE))
		ntfs_save_wsl_perm(inode, NULL);
	mark_inode_dirty(inode);
out:
	return err;
}

/*
 * check_read_restriction:
 * common code for ntfs_file_read_iter and ntfs_file_splice_read
 */
static int check_read_restriction(struct inode *inode)
{
	struct ntfs_inode *ni = ntfs_i(inode);

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (unlikely(ntfs3_forced_shutdown(inode->i_sb)))
		return -EIO;

	if (is_encrypted(ni)) {
		ntfs_inode_warn(inode, "encrypted i/o not supported");
		return -EOPNOTSUPP;
	}

#ifndef CONFIG_NTFS3_LZX_XPRESS
	if (ni->ni_flags & NI_FLAG_COMPRESSED_MASK) {
		ntfs_inode_warn(
			inode,
			"activate CONFIG_NTFS3_LZX_XPRESS to read external compressed files");
		return -EOPNOTSUPP;
	}
#endif

	if (is_dedup(ni)) {
		ntfs_inode_warn(inode, "read deduplicated not supported");
		return -EOPNOTSUPP;
	}

	return 0;
}

/*
 * ntfs_file_read_iter - file_operations::read_iter
 */
static ssize_t ntfs_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ntfs_inode *ni = ntfs_i(inode);
	size_t bytes = iov_iter_count(iter);
	loff_t valid, i_size, vbo, end;
	unsigned int dio_flags;
	ssize_t err;

	err = check_read_restriction(inode);
	if (err)
		return err;

	if (!bytes)
		return 0; /* skip atime */

	if (is_compressed(ni)) {
		if (iocb->ki_flags & IOCB_DIRECT) {
			ntfs_inode_warn(
				inode, "direct i/o + compressed not supported");
			return -EOPNOTSUPP;
		}
		/* Turn off readahead for compressed files. */
		file->f_ra.ra_pages = 0;
	}

	/* Fallback to buffered I/O if the inode does not support direct I/O. */
	if (!(iocb->ki_flags & IOCB_DIRECT) ||
	    !ntfs_should_use_dio(iocb, iter)) {
		iocb->ki_flags &= ~IOCB_DIRECT;
		return generic_file_read_iter(iocb, iter);
	}

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!inode_trylock_shared(inode))
			return -EAGAIN;
	} else {
		inode_lock_shared(inode);
	}

	vbo = iocb->ki_pos;
	end = vbo + bytes;
	dio_flags = 0;
	valid = ni->i_valid;
	i_size = inode->i_size;

	if (vbo < valid) {
		if (valid < end) {
			/* read cross 'valid' size. */
			dio_flags |= IOMAP_DIO_FORCE_WAIT;
		}

		if (ni->file.run_da.count) {
			/* Direct I/O is not compatible with delalloc. */
			err = ni_allocate_da_blocks(ni);
			if (err)
				goto out;
		}

		err = iomap_dio_rw(iocb, iter, &ntfs_iomap_ops, NULL, dio_flags,
				   NULL, 0);

		if (err <= 0)
			goto out;
		end = vbo + err;
		if (valid < end) {
			size_t to_zero = end - valid;
			/* Fix iter. */
			iov_iter_revert(iter, to_zero);
			iov_iter_zero(to_zero, iter);
		}
	} else if (vbo < i_size) {
		if (end > i_size)
			bytes = i_size - vbo;
		iov_iter_zero(bytes, iter);
		iocb->ki_pos += bytes;
		err = bytes;
	}

out:
	inode_unlock_shared(inode);
	file_accessed(iocb->ki_filp);
	return err;
}

/*
 * ntfs_file_splice_read - file_operations::splice_read
 */
static ssize_t ntfs_file_splice_read(struct file *in, loff_t *ppos,
				     struct pipe_inode_info *pipe, size_t len,
				     unsigned int flags)
{
	struct inode *inode = file_inode(in);
	ssize_t err;

	err = check_read_restriction(inode);
	if (err)
		return err;

	if (is_compressed(ntfs_i(inode))) {
		/* Turn off readahead for compressed files. */
		in->f_ra.ra_pages = 0;
	}

	return filemap_splice_read(in, ppos, pipe, len, flags);
}

/*
 * ntfs_get_frame_pages
 *
 * Return: Array of locked pages.
 */
static int ntfs_get_frame_pages(struct address_space *mapping, pgoff_t index,
				struct page **pages, u32 pages_per_frame,
				bool *frame_uptodate)
{
	gfp_t gfp_mask = mapping_gfp_mask(mapping);
	u32 npages;

	*frame_uptodate = true;

	for (npages = 0; npages < pages_per_frame; npages++, index++) {
		struct folio *folio;

		folio = __filemap_get_folio(mapping, index,
					    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
					    gfp_mask | __GFP_ZERO);
		if (IS_ERR(folio)) {
			while (npages--) {
				folio = page_folio(pages[npages]);
				folio_unlock(folio);
				folio_put(folio);
			}

			return -ENOMEM;
		}

		if (!folio_test_uptodate(folio))
			*frame_uptodate = false;

		pages[npages] = &folio->page;
	}

	return 0;
}

/*
 * ntfs_compress_write - Helper for ntfs_file_write_iter() (compressed files).
 */
static ssize_t ntfs_compress_write(struct kiocb *iocb, struct iov_iter *from)
{
	int err;
	struct file *file = iocb->ki_filp;
	size_t count = iov_iter_count(from);
	loff_t pos = iocb->ki_pos;
	struct inode *inode = file_inode(file);
	loff_t i_size = i_size_read(inode);
	struct address_space *mapping = inode->i_mapping;
	struct ntfs_inode *ni = ntfs_i(inode);
	u64 valid = ni->i_valid;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct page **pages = NULL;
	struct folio *folio;
	size_t written = 0;
	u8 frame_bits = NTFS_LZNT_CUNIT + sbi->cluster_bits;
	u32 frame_size = 1u << frame_bits;
	u32 pages_per_frame = frame_size >> PAGE_SHIFT;
	u32 ip, off;
	CLST frame;
	u64 frame_vbo;
	pgoff_t index;
	bool frame_uptodate;

	if (frame_size < PAGE_SIZE) {
		/*
		 * frame_size == 8K if cluster 512
		 * frame_size == 64K if cluster 4096
		 */
		ntfs_inode_warn(inode, "page size is bigger than frame size");
		return -EOPNOTSUPP;
	}

	pages = kmalloc_objs(struct page *, pages_per_frame, GFP_NOFS);
	if (!pages)
		return -ENOMEM;

	err = file_remove_privs(file);
	if (err)
		goto out;

	err = file_update_time(file);
	if (err)
		goto out;

	/* Zero range [valid : pos). */
	while (valid < pos) {
		CLST lcn, clen;

		frame = valid >> frame_bits;
		frame_vbo = valid & ~(frame_size - 1);
		off = valid & (frame_size - 1);

		err = attr_data_get_block(ni, frame << NTFS_LZNT_CUNIT, 1, &lcn,
					  &clen, NULL, false, NULL, false);
		if (err)
			goto out;

		if (lcn == SPARSE_LCN) {
			ni->i_valid = valid =
				frame_vbo + ((u64)clen << sbi->cluster_bits);
			continue;
		}

		/* Load full frame. */
		err = ntfs_get_frame_pages(mapping, frame_vbo >> PAGE_SHIFT,
					   pages, pages_per_frame,
					   &frame_uptodate);
		if (err)
			goto out;

		if (!frame_uptodate && off) {
			err = ni_read_frame(ni, frame_vbo, pages,
					    pages_per_frame, 0);
			if (err) {
				for (ip = 0; ip < pages_per_frame; ip++) {
					folio = page_folio(pages[ip]);
					folio_unlock(folio);
					folio_put(folio);
				}
				goto out;
			}
		}

		ip = off >> PAGE_SHIFT;
		off = offset_in_page(valid);
		for (; ip < pages_per_frame; ip++, off = 0) {
			folio = page_folio(pages[ip]);
			folio_zero_segment(folio, off, PAGE_SIZE);
			flush_dcache_folio(folio);
			folio_mark_uptodate(folio);
		}

		ni_lock(ni);
		err = ni_write_frame(ni, pages, pages_per_frame);
		ni_unlock(ni);

		for (ip = 0; ip < pages_per_frame; ip++) {
			folio = page_folio(pages[ip]);
			folio_mark_uptodate(folio);
			folio_unlock(folio);
			folio_put(folio);
		}

		if (err)
			goto out;

		ni->i_valid = valid = frame_vbo + frame_size;
	}

	/* Copy user data [pos : pos + count). */
	while (count) {
		size_t copied, bytes;

		off = pos & (frame_size - 1);
		bytes = frame_size - off;
		if (bytes > count)
			bytes = count;

		frame_vbo = pos & ~(frame_size - 1);
		index = frame_vbo >> PAGE_SHIFT;

		if (unlikely(fault_in_iov_iter_readable(from, bytes))) {
			err = -EFAULT;
			goto out;
		}

		/* Load full frame. */
		err = ntfs_get_frame_pages(mapping, index, pages,
					   pages_per_frame, &frame_uptodate);
		if (err)
			goto out;

		if (!frame_uptodate) {
			loff_t to = pos + bytes;

			if (off || (to < i_size && (to & (frame_size - 1)))) {
				err = ni_read_frame(ni, frame_vbo, pages,
						    pages_per_frame, 0);
				if (err) {
					for (ip = 0; ip < pages_per_frame;
					     ip++) {
						folio = page_folio(pages[ip]);
						folio_unlock(folio);
						folio_put(folio);
					}
					goto out;
				}
			}
		}

		WARN_ON(!bytes);
		copied = 0;
		ip = off >> PAGE_SHIFT;
		off = offset_in_page(pos);

		/* Copy user data to pages. */
		for (;;) {
			size_t cp, tail = PAGE_SIZE - off;

			folio = page_folio(pages[ip]);
			cp = copy_folio_from_iter_atomic(
				folio, off, min(tail, bytes), from);
			flush_dcache_folio(folio);

			copied += cp;
			bytes -= cp;
			if (!bytes || !cp)
				break;

			if (cp < tail) {
				off += cp;
			} else {
				ip++;
				off = 0;
			}
		}

		ni_lock(ni);
		err = ni_write_frame(ni, pages, pages_per_frame);
		ni_unlock(ni);

		for (ip = 0; ip < pages_per_frame; ip++) {
			folio = page_folio(pages[ip]);
			folio_clear_dirty(folio);
			folio_mark_uptodate(folio);
			folio_unlock(folio);
			folio_put(folio);
		}

		if (err)
			goto out;

		/*
		 * We can loop for a long time in here. Be nice and allow
		 * us to schedule out to avoid softlocking if preempt
		 * is disabled.
		 */
		cond_resched();

		pos += copied;
		written += copied;

		count = iov_iter_count(from);
	}

out:
	kfree(pages);

	if (err < 0)
		return err;

	iocb->ki_pos += written;
	if (iocb->ki_pos > ni->i_valid)
		ni->i_valid = iocb->ki_pos;
	if (iocb->ki_pos > i_size)
		i_size_write(inode, iocb->ki_pos);

	return written;
}

/*
 * check_write_restriction:
 * common code for ntfs_file_write_iter and ntfs_file_splice_write
 */
static int check_write_restriction(struct inode *inode)
{
	struct ntfs_inode *ni = ntfs_i(inode);

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (unlikely(ntfs3_forced_shutdown(inode->i_sb)))
		return -EIO;

	if (is_encrypted(ni)) {
		ntfs_inode_warn(inode, "encrypted i/o not supported");
		return -EOPNOTSUPP;
	}

	if (is_dedup(ni)) {
		ntfs_inode_warn(inode, "write into deduplicated not supported");
		return -EOPNOTSUPP;
	}

	if (unlikely(IS_IMMUTABLE(inode)))
		return -EPERM;

	return 0;
}

/*
 * ntfs_file_write_iter - file_operations::write_iter
 */
static ssize_t ntfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct ntfs_inode *ni = ntfs_i(inode);
	ssize_t ret, err;

	if (!inode_trylock(inode)) {
		if (iocb->ki_flags & IOCB_NOWAIT)
			return -EAGAIN;
		inode_lock(inode);
	}

	ret = check_write_restriction(inode);
	if (ret)
		goto out;

	if (is_compressed(ni) && (iocb->ki_flags & IOCB_DIRECT)) {
		ntfs_inode_warn(inode, "direct i/o + compressed not supported");
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	err = file_modified(iocb->ki_filp);
	if (err) {
		ret = err;
		goto out;
	}

	if (WARN_ON(ni->ni_flags & NI_FLAG_COMPRESSED_MASK)) {
		/* Should never be here, see ntfs_file_open(). */
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = ntfs_extend(inode, iocb->ki_pos, ret, file);
	if (ret)
		goto out;

	if (is_compressed(ni)) {
		ret = ntfs_compress_write(iocb, from);
		goto out;
	}

	/* Fallback to buffered I/O if the inode does not support direct I/O. */
	if (!(iocb->ki_flags & IOCB_DIRECT) ||
	    !ntfs_should_use_dio(iocb, from)) {
		iocb->ki_flags &= ~IOCB_DIRECT;

		ret = iomap_file_buffered_write(iocb, from, &ntfs_iomap_ops,
						&ntfs_iomap_folio_ops, NULL);
		inode_unlock(inode);

		if (likely(ret > 0))
			ret = generic_write_sync(iocb, ret);

		return ret;
	}

	if (ni->file.run_da.count) {
		/* Direct I/O is not compatible with delalloc. */
		ret = ni_allocate_da_blocks(ni);
		if (ret)
			goto out;
	}

	ret = iomap_dio_rw(iocb, from, &ntfs_iomap_ops, NULL, 0, NULL, 0);

	if (ret == -ENOTBLK) {
		/* Returns -ENOTBLK in case of a page invalidation failure for writes.*/
		/* The callers needs to fall back to buffered I/O in this case. */
		ret = 0;
	}

	if (ret >= 0 && iov_iter_count(from)) {
		loff_t offset = iocb->ki_pos, endbyte;

		iocb->ki_flags &= ~IOCB_DIRECT;
		err = iomap_file_buffered_write(iocb, from, &ntfs_iomap_ops,
						&ntfs_iomap_folio_ops, NULL);
		if (err < 0) {
			ret = err;
			goto out;
		}

		/*
		* We need to ensure that the pages within the page cache for
		* the range covered by this I/O are written to disk and
		* invalidated. This is in attempt to preserve the expected
		* direct I/O semantics in the case we fallback to buffered I/O
		* to complete off the I/O request.
		*/
		ret += err;
		endbyte = offset + err - 1;
		err = filemap_write_and_wait_range(inode->i_mapping, offset,
						   endbyte);
		if (err) {
			ret = err;
			goto out;
		}

		invalidate_mapping_pages(inode->i_mapping, offset >> PAGE_SHIFT,
					 endbyte >> PAGE_SHIFT);
	}

out:
	inode_unlock(inode);

	return ret;
}

/*
 * ntfs_file_open - file_operations::open
 */
int ntfs_file_open(struct inode *inode, struct file *file)
{
	struct ntfs_inode *ni = ntfs_i(inode);

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (unlikely(ntfs3_forced_shutdown(inode->i_sb)))
		return -EIO;

	if (unlikely((is_compressed(ni) || is_encrypted(ni)) &&
		     (file->f_flags & O_DIRECT))) {
		return -EOPNOTSUPP;
	}

	/* Decompress "external compressed" file if opened for rw. */
	if ((ni->ni_flags & NI_FLAG_COMPRESSED_MASK) &&
	    (file->f_flags & (O_WRONLY | O_RDWR | O_TRUNC))) {
#ifdef CONFIG_NTFS3_LZX_XPRESS
		int err = ni_decompress_file(ni);

		if (err)
			return err;
#else
		ntfs_inode_warn(
			inode,
			"activate CONFIG_NTFS3_LZX_XPRESS to write external compressed files");
		return -EOPNOTSUPP;
#endif
	}

	file->f_mode |= FMODE_NOWAIT | FMODE_CAN_ODIRECT;

	return generic_file_open(inode, file);
}

/*
 * ntfs_file_release - file_operations::release
 *
 * Called when an inode is released. Note that this is different
 * from ntfs_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ntfs_file_release(struct inode *inode, struct file *file)
{
	int err;
	struct ntfs_inode *ni;

	if (!(file->f_mode & FMODE_WRITE) ||
	    atomic_read(&inode->i_writecount) != 1 ||
	    inode->i_ino == MFT_REC_MFT) {
		return 0;
	}

	/* Close the last writer on the inode. */
	ni = ntfs_i(inode);

	/* Allocate delayed blocks (clusters). */
	err = ni_allocate_da_blocks(ni);
	if (err)
		goto out;

	if (ni->mi.sbi->options->prealloc) {
		ni_lock(ni);
		down_write(&ni->file.run_lock);

		/* Deallocate preallocated. */
		err = attr_set_size(ni, ATTR_DATA, NULL, 0, &ni->file.run,
				    inode->i_size, &ni->i_valid, false);

		up_write(&ni->file.run_lock);
		ni_unlock(ni);
	}
out:
	return err;
}

/*
 * ntfs_fiemap - inode_operations::fiemap
 */
int ntfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		__u64 start, __u64 len)
{
	int err;
	struct ntfs_inode *ni = ntfs_i(inode);

	/* Avoid any operation if inode is bad. */
	if (unlikely(is_bad_ni(ni)))
		return -EINVAL;

	if (is_compressed(ni)) {
		/* Unfortunately cp -r incorrectly treats compressed clusters. */
		ntfs_inode_warn(inode,
				"fiemap is not supported for compressed file");
		return -EOPNOTSUPP;
	}

	if (S_ISDIR(inode->i_mode)) {
		/* TODO: add support for dirs (ATTR_ALLOC). */
		ntfs_inode_warn(inode,
				"fiemap is not supported for directories");
		return -EOPNOTSUPP;
	}

	if (fieinfo->fi_flags & FIEMAP_FLAG_XATTR) {
		ntfs_inode_warn(inode, "fiemap(xattr) is not supported");
		return -EOPNOTSUPP;
	}

	inode_lock_shared(inode);

	err = iomap_fiemap(inode, fieinfo, start, len, &ntfs_iomap_ops);

	inode_unlock_shared(inode);
	return err;
}

/*
 * ntfs_file_splice_write - file_operations::splice_write
 */
static ssize_t ntfs_file_splice_write(struct pipe_inode_info *pipe,
				      struct file *file, loff_t *ppos,
				      size_t len, unsigned int flags)
{
	ssize_t err;
	struct inode *inode = file_inode(file);

	err = check_write_restriction(inode);
	if (err)
		return err;

	return iter_file_splice_write(pipe, file, ppos, len, flags);
}

/*
 * ntfs_file_fsync - file_operations::fsync
 */
int ntfs_file_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	int err, ret;

	if (unlikely(ntfs3_forced_shutdown(sb)))
		return -EIO;

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		return ret;

	ret = write_inode_now(inode, !datasync);

	if (!ret) {
		ret = ni_write_parents(ntfs_i(inode), !datasync);
	}

	if (!ret) {
		ntfs_set_state(sbi, NTFS_DIRTY_CLEAR);
		ntfs_update_mftmirr(sbi);
	}

	err = sync_blockdev(sb->s_bdev);
	if (unlikely(err && !ret))
		ret = err;
	if (!ret)
		blkdev_issue_flush(sb->s_bdev);
	return ret;
}

/*
 * ntfs_llseek - file_operations::llseek
 */
static loff_t ntfs_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;
	struct ntfs_inode *ni = ntfs_i(inode);
	loff_t maxbytes = ntfs_get_maxbytes(ni);
	loff_t ret;

	if (whence == SEEK_DATA || whence == SEEK_HOLE) {
		inode_lock_shared(inode);
		/* Scan file for hole or data. */
		ret = ni_seek_data_or_hole(ni, offset, whence == SEEK_DATA);
		inode_unlock_shared(inode);

		if (ret >= 0)
			ret = vfs_setpos(file, ret, maxbytes);
	} else {
		ret = generic_file_llseek_size(file, offset, whence, maxbytes,
					       i_size_read(inode));
	}
	return ret;
}

// clang-format off
const struct inode_operations ntfs_file_inode_operations = {
	.getattr	= ntfs_getattr,
	.setattr	= ntfs_setattr,
	.listxattr	= ntfs_listxattr,
	.get_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
	.fiemap		= ntfs_fiemap,
};

const struct file_operations ntfs_file_operations = {
	.llseek		= ntfs_llseek,
	.read_iter	= ntfs_file_read_iter,
	.write_iter	= ntfs_file_write_iter,
	.unlocked_ioctl = ntfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ntfs_compat_ioctl,
#endif
	.splice_read	= ntfs_file_splice_read,
	.splice_write	= ntfs_file_splice_write,
	.mmap_prepare	= ntfs_file_mmap_prepare,
	.open		= ntfs_file_open,
	.fsync		= ntfs_file_fsync,
	.fallocate	= ntfs_fallocate,
	.release	= ntfs_file_release,
	.setlease	= generic_setlease,
};

#if IS_ENABLED(CONFIG_NTFS_FS)
const struct file_operations ntfs_legacy_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= ntfs_file_read_iter,
	.splice_read	= ntfs_file_splice_read,
	.open		= ntfs_file_open,
	.release	= ntfs_file_release,
	.setlease	= generic_setlease,
};
#endif
// clang-format on
