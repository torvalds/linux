// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/cred.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/msdos_fs.h>
#include <linux/writeback.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

static int exfat_cont_expand(struct inode *inode, loff_t size)
{
	int ret;
	unsigned int num_clusters, new_num_clusters, last_clu;
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_chain clu;

	ret = inode_newsize_ok(inode, size);
	if (ret)
		return ret;

	num_clusters = EXFAT_B_TO_CLU(exfat_ondisk_size(inode), sbi);
	new_num_clusters = EXFAT_B_TO_CLU_ROUND_UP(size, sbi);

	if (new_num_clusters == num_clusters)
		goto out;

	if (num_clusters) {
		exfat_chain_set(&clu, ei->start_clu, num_clusters, ei->flags);
		ret = exfat_find_last_cluster(sb, &clu, &last_clu);
		if (ret)
			return ret;

		clu.dir = last_clu + 1;
	} else {
		last_clu = EXFAT_EOF_CLUSTER;
		clu.dir = EXFAT_EOF_CLUSTER;
	}

	clu.size = 0;
	clu.flags = ei->flags;

	ret = exfat_alloc_cluster(inode, new_num_clusters - num_clusters,
			&clu, inode_needs_sync(inode));
	if (ret)
		return ret;

	/* Append new clusters to chain */
	if (num_clusters) {
		if (clu.flags != ei->flags)
			if (exfat_chain_cont_cluster(sb, ei->start_clu, num_clusters))
				goto free_clu;

		if (clu.flags == ALLOC_FAT_CHAIN)
			if (exfat_ent_set(sb, last_clu, clu.dir))
				goto free_clu;
	} else
		ei->start_clu = clu.dir;

	ei->flags = clu.flags;

out:
	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	/* Expanded range not zeroed, do not update valid_size */
	i_size_write(inode, size);

	inode->i_blocks = round_up(size, sbi->cluster_size) >> 9;
	mark_inode_dirty(inode);

	if (IS_SYNC(inode))
		return write_inode_now(inode, 1);

	return 0;

free_clu:
	exfat_free_cluster(inode, &clu);
	return -EIO;
}

static bool exfat_allow_set_time(struct mnt_idmap *idmap,
				 struct exfat_sb_info *sbi, struct inode *inode)
{
	mode_t allow_utime = sbi->options.allow_utime;

	if (!vfsuid_eq_kuid(i_uid_into_vfsuid(idmap, inode),
			    current_fsuid())) {
		if (vfsgid_in_group_p(i_gid_into_vfsgid(idmap, inode)))
			allow_utime >>= 3;
		if (allow_utime & MAY_WRITE)
			return true;
	}

	/* use a default check */
	return false;
}

static int exfat_sanitize_mode(const struct exfat_sb_info *sbi,
		struct inode *inode, umode_t *mode_ptr)
{
	mode_t i_mode, mask, perm;

	i_mode = inode->i_mode;

	mask = (S_ISREG(i_mode) || S_ISLNK(i_mode)) ?
		sbi->options.fs_fmask : sbi->options.fs_dmask;
	perm = *mode_ptr & ~(S_IFMT | mask);

	/* Of the r and x bits, all (subject to umask) must be present.*/
	if ((perm & 0555) != (i_mode & 0555))
		return -EPERM;

	if (exfat_mode_can_hold_ro(inode)) {
		/*
		 * Of the w bits, either all (subject to umask) or none must
		 * be present.
		 */
		if ((perm & 0222) && ((perm & 0222) != (0222 & ~mask)))
			return -EPERM;
	} else {
		/*
		 * If exfat_mode_can_hold_ro(inode) is false, can't change
		 * w bits.
		 */
		if ((perm & 0222) != (0222 & ~mask))
			return -EPERM;
	}

	*mode_ptr &= S_IFMT | perm;

	return 0;
}

/* resize the file length */
int __exfat_truncate(struct inode *inode)
{
	unsigned int num_clusters_new, num_clusters_phys;
	unsigned int last_clu = EXFAT_FREE_CLUSTER;
	struct exfat_chain clu;
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);

	/* check if the given file ID is opened */
	if (ei->type != TYPE_FILE && ei->type != TYPE_DIR)
		return -EPERM;

	exfat_set_volume_dirty(sb);

	num_clusters_new = EXFAT_B_TO_CLU_ROUND_UP(i_size_read(inode), sbi);
	num_clusters_phys = EXFAT_B_TO_CLU(exfat_ondisk_size(inode), sbi);

	exfat_chain_set(&clu, ei->start_clu, num_clusters_phys, ei->flags);

	if (i_size_read(inode) > 0) {
		/*
		 * Truncate FAT chain num_clusters after the first cluster
		 * num_clusters = min(new, phys);
		 */
		unsigned int num_clusters =
			min(num_clusters_new, num_clusters_phys);

		/*
		 * Follow FAT chain
		 * (defensive coding - works fine even with corrupted FAT table
		 */
		if (clu.flags == ALLOC_NO_FAT_CHAIN) {
			clu.dir += num_clusters;
			clu.size -= num_clusters;
		} else {
			while (num_clusters > 0) {
				last_clu = clu.dir;
				if (exfat_get_next_cluster(sb, &(clu.dir)))
					return -EIO;

				num_clusters--;
				clu.size--;
			}
		}
	} else {
		ei->flags = ALLOC_NO_FAT_CHAIN;
		ei->start_clu = EXFAT_EOF_CLUSTER;
	}

	if (i_size_read(inode) < ei->valid_size)
		ei->valid_size = i_size_read(inode);

	if (ei->type == TYPE_FILE)
		ei->attr |= EXFAT_ATTR_ARCHIVE;

	/*
	 * update the directory entry
	 *
	 * If the directory entry is updated by mark_inode_dirty(), the
	 * directory entry will be written after a writeback cycle of
	 * updating the bitmap/FAT, which may result in clusters being
	 * freed but referenced by the directory entry in the event of a
	 * sudden power failure.
	 * __exfat_write_inode() is called for directory entry, bitmap
	 * and FAT to be written in a same writeback.
	 */
	if (__exfat_write_inode(inode, inode_needs_sync(inode)))
		return -EIO;

	/* cut off from the FAT chain */
	if (ei->flags == ALLOC_FAT_CHAIN && last_clu != EXFAT_FREE_CLUSTER &&
			last_clu != EXFAT_EOF_CLUSTER) {
		if (exfat_ent_set(sb, last_clu, EXFAT_EOF_CLUSTER))
			return -EIO;
	}

	/* invalidate cache and free the clusters */
	/* clear exfat cache */
	exfat_cache_inval_inode(inode);

	/* hint information */
	ei->hint_bmap.off = EXFAT_EOF_CLUSTER;
	ei->hint_bmap.clu = EXFAT_EOF_CLUSTER;

	/* hint_stat will be used if this is directory. */
	ei->hint_stat.eidx = 0;
	ei->hint_stat.clu = ei->start_clu;
	ei->hint_femp.eidx = EXFAT_HINT_NONE;

	/* free the clusters */
	if (exfat_free_cluster(inode, &clu))
		return -EIO;

	return 0;
}

void exfat_truncate(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	int err;

	mutex_lock(&sbi->s_lock);
	if (ei->start_clu == 0) {
		/*
		 * Empty start_clu != ~0 (not allocated)
		 */
		exfat_fs_error(sb, "tried to truncate zeroed cluster.");
		goto write_size;
	}

	err = __exfat_truncate(inode);
	if (err)
		goto write_size;

	inode->i_blocks = round_up(i_size_read(inode), sbi->cluster_size) >> 9;
write_size:
	mutex_unlock(&sbi->s_lock);
}

int exfat_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, unsigned int request_mask,
		  unsigned int query_flags)
{
	struct inode *inode = d_backing_inode(path->dentry);
	struct exfat_inode_info *ei = EXFAT_I(inode);

	generic_fillattr(idmap, request_mask, inode, stat);
	exfat_truncate_atime(&stat->atime);
	stat->result_mask |= STATX_BTIME;
	stat->btime.tv_sec = ei->i_crtime.tv_sec;
	stat->btime.tv_nsec = ei->i_crtime.tv_nsec;
	stat->blksize = EXFAT_SB(inode->i_sb)->cluster_size;
	return 0;
}

int exfat_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *attr)
{
	struct exfat_sb_info *sbi = EXFAT_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	unsigned int ia_valid;
	int error;

	if (unlikely(exfat_forced_shutdown(inode->i_sb)))
		return -EIO;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size > i_size_read(inode)) {
		error = exfat_cont_expand(inode, attr->ia_size);
		if (error || attr->ia_valid == ATTR_SIZE)
			return error;
		attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Check for setting the inode time. */
	ia_valid = attr->ia_valid;
	if ((ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) &&
	    exfat_allow_set_time(idmap, sbi, inode)) {
		attr->ia_valid &= ~(ATTR_MTIME_SET | ATTR_ATIME_SET |
				ATTR_TIMES_SET);
	}

	error = setattr_prepare(idmap, dentry, attr);
	attr->ia_valid = ia_valid;
	if (error)
		goto out;

	if (((attr->ia_valid & ATTR_UID) &&
	      (!uid_eq(from_vfsuid(idmap, i_user_ns(inode), attr->ia_vfsuid),
	       sbi->options.fs_uid))) ||
	    ((attr->ia_valid & ATTR_GID) &&
	      (!gid_eq(from_vfsgid(idmap, i_user_ns(inode), attr->ia_vfsgid),
	       sbi->options.fs_gid))) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~(S_IFREG | S_IFLNK | S_IFDIR | 0777)))) {
		error = -EPERM;
		goto out;
	}

	/*
	 * We don't return -EPERM here. Yes, strange, but this is too
	 * old behavior.
	 */
	if (attr->ia_valid & ATTR_MODE) {
		if (exfat_sanitize_mode(sbi, inode, &attr->ia_mode) < 0)
			attr->ia_valid &= ~ATTR_MODE;
	}

	if (attr->ia_valid & ATTR_SIZE)
		inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));

	setattr_copy(idmap, inode, attr);
	exfat_truncate_inode_atime(inode);

	if (attr->ia_valid & ATTR_SIZE) {
		error = exfat_block_truncate_page(inode, attr->ia_size);
		if (error)
			goto out;

		down_write(&EXFAT_I(inode)->truncate_lock);
		truncate_setsize(inode, attr->ia_size);

		/*
		 * __exfat_write_inode() is called from exfat_truncate(), inode
		 * is already written by it, so mark_inode_dirty() is unneeded.
		 */
		exfat_truncate(inode);
		up_write(&EXFAT_I(inode)->truncate_lock);
	} else
		mark_inode_dirty(inode);

out:
	return error;
}

/*
 * modified ioctls from fat/file.c by Welmer Almesberger
 */
static int exfat_ioctl_get_attributes(struct inode *inode, u32 __user *user_attr)
{
	u32 attr;

	inode_lock_shared(inode);
	attr = exfat_make_attr(inode);
	inode_unlock_shared(inode);

	return put_user(attr, user_attr);
}

static int exfat_ioctl_set_attributes(struct file *file, u32 __user *user_attr)
{
	struct inode *inode = file_inode(file);
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);
	int is_dir = S_ISDIR(inode->i_mode);
	u32 attr, oldattr;
	struct iattr ia;
	int err;

	err = get_user(attr, user_attr);
	if (err)
		goto out;

	err = mnt_want_write_file(file);
	if (err)
		goto out;
	inode_lock(inode);

	oldattr = exfat_make_attr(inode);

	/*
	 * Mask attributes so we don't set reserved fields.
	 */
	attr &= (EXFAT_ATTR_READONLY | EXFAT_ATTR_HIDDEN | EXFAT_ATTR_SYSTEM |
		 EXFAT_ATTR_ARCHIVE);
	attr |= (is_dir ? EXFAT_ATTR_SUBDIR : 0);

	/* Equivalent to a chmod() */
	ia.ia_valid = ATTR_MODE | ATTR_CTIME;
	ia.ia_ctime = current_time(inode);
	if (is_dir)
		ia.ia_mode = exfat_make_mode(sbi, attr, 0777);
	else
		ia.ia_mode = exfat_make_mode(sbi, attr, 0666 | (inode->i_mode & 0111));

	/* The root directory has no attributes */
	if (inode->i_ino == EXFAT_ROOT_INO && attr != EXFAT_ATTR_SUBDIR) {
		err = -EINVAL;
		goto out_unlock_inode;
	}

	if (((attr | oldattr) & EXFAT_ATTR_SYSTEM) &&
	    !capable(CAP_LINUX_IMMUTABLE)) {
		err = -EPERM;
		goto out_unlock_inode;
	}

	/*
	 * The security check is questionable...  We single
	 * out the RO attribute for checking by the security
	 * module, just because it maps to a file mode.
	 */
	err = security_inode_setattr(file_mnt_idmap(file),
				     file->f_path.dentry, &ia);
	if (err)
		goto out_unlock_inode;

	/* This MUST be done before doing anything irreversible... */
	err = exfat_setattr(file_mnt_idmap(file), file->f_path.dentry, &ia);
	if (err)
		goto out_unlock_inode;

	fsnotify_change(file->f_path.dentry, ia.ia_valid);

	exfat_save_attr(inode, attr);
	mark_inode_dirty(inode);
out_unlock_inode:
	inode_unlock(inode);
	mnt_drop_write_file(file);
out:
	return err;
}

static int exfat_ioctl_fitrim(struct inode *inode, unsigned long arg)
{
	struct fstrim_range range;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!bdev_max_discard_sectors(inode->i_sb->s_bdev))
		return -EOPNOTSUPP;

	if (copy_from_user(&range, (struct fstrim_range __user *)arg, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(unsigned int, range.minlen,
				bdev_discard_granularity(inode->i_sb->s_bdev));

	ret = exfat_trim_fs(inode, &range);
	if (ret < 0)
		return ret;

	if (copy_to_user((struct fstrim_range __user *)arg, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

static int exfat_ioctl_shutdown(struct super_block *sb, unsigned long arg)
{
	u32 flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(flags, (__u32 __user *)arg))
		return -EFAULT;

	return exfat_force_shutdown(sb, flags);
}

long exfat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
		return exfat_ioctl_get_attributes(inode, user_attr);
	case FAT_IOCTL_SET_ATTRIBUTES:
		return exfat_ioctl_set_attributes(filp, user_attr);
	case EXFAT_IOC_SHUTDOWN:
		return exfat_ioctl_shutdown(inode->i_sb, arg);
	case FITRIM:
		return exfat_ioctl_fitrim(inode, arg);
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long exfat_compat_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	return exfat_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

int exfat_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	int err;

	if (unlikely(exfat_forced_shutdown(inode->i_sb)))
		return -EIO;

	err = __generic_file_fsync(filp, start, end, datasync);
	if (err)
		return err;

	err = sync_blockdev(inode->i_sb->s_bdev);
	if (err)
		return err;

	return blkdev_issue_flush(inode->i_sb->s_bdev);
}

static int exfat_extend_valid_size(struct file *file, loff_t new_valid_size)
{
	int err;
	loff_t pos;
	struct inode *inode = file_inode(file);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	struct address_space *mapping = inode->i_mapping;
	const struct address_space_operations *ops = mapping->a_ops;

	pos = ei->valid_size;
	while (pos < new_valid_size) {
		u32 len;
		struct folio *folio;
		unsigned long off;

		len = PAGE_SIZE - (pos & (PAGE_SIZE - 1));
		if (pos + len > new_valid_size)
			len = new_valid_size - pos;

		err = ops->write_begin(file, mapping, pos, len, &folio, NULL);
		if (err)
			goto out;

		off = offset_in_folio(folio, pos);
		folio_zero_new_buffers(folio, off, off + len);

		err = ops->write_end(file, mapping, pos, len, len, folio, NULL);
		if (err < 0)
			goto out;
		pos += len;

		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
	}

	return 0;

out:
	return err;
}

static ssize_t exfat_file_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t ret;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	loff_t pos = iocb->ki_pos;
	loff_t valid_size;

	if (unlikely(exfat_forced_shutdown(inode->i_sb)))
		return -EIO;

	inode_lock(inode);

	valid_size = ei->valid_size;

	ret = generic_write_checks(iocb, iter);
	if (ret <= 0)
		goto unlock;

	if (iocb->ki_flags & IOCB_DIRECT) {
		unsigned long align = pos | iov_iter_alignment(iter);

		if (!IS_ALIGNED(align, i_blocksize(inode)) &&
		    !IS_ALIGNED(align, bdev_logical_block_size(inode->i_sb->s_bdev))) {
			ret = -EINVAL;
			goto unlock;
		}
	}

	if (pos > valid_size) {
		ret = exfat_extend_valid_size(file, pos);
		if (ret < 0 && ret != -ENOSPC) {
			exfat_err(inode->i_sb,
				"write: fail to zero from %llu to %llu(%zd)",
				valid_size, pos, ret);
		}
		if (ret < 0)
			goto unlock;
	}

	ret = __generic_file_write_iter(iocb, iter);
	if (ret < 0)
		goto unlock;

	inode_unlock(inode);

	if (pos > valid_size)
		pos = valid_size;

	if (iocb->ki_pos > pos) {
		ssize_t err = generic_write_sync(iocb, iocb->ki_pos - pos);
		if (err < 0)
			return err;
	}

	return ret;

unlock:
	inode_unlock(inode);

	return ret;
}

static ssize_t exfat_file_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	if (unlikely(exfat_forced_shutdown(inode->i_sb)))
		return -EIO;

	return generic_file_read_iter(iocb, iter);
}

static vm_fault_t exfat_page_mkwrite(struct vm_fault *vmf)
{
	int err;
	struct vm_area_struct *vma = vmf->vma;
	struct file *file = vma->vm_file;
	struct inode *inode = file_inode(file);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	loff_t start, end;

	if (!inode_trylock(inode))
		return VM_FAULT_RETRY;

	start = ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
	end = min_t(loff_t, i_size_read(inode),
			start + vma->vm_end - vma->vm_start);

	if (ei->valid_size < end) {
		err = exfat_extend_valid_size(file, end);
		if (err < 0) {
			inode_unlock(inode);
			return vmf_fs_error(err);
		}
	}

	inode_unlock(inode);

	return filemap_page_mkwrite(vmf);
}

static const struct vm_operations_struct exfat_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= exfat_page_mkwrite,
};

static int exfat_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (unlikely(exfat_forced_shutdown(file_inode(file)->i_sb)))
		return -EIO;

	file_accessed(file);
	vma->vm_ops = &exfat_file_vm_ops;
	return 0;
}

static ssize_t exfat_splice_read(struct file *in, loff_t *ppos,
		struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
	if (unlikely(exfat_forced_shutdown(file_inode(in)->i_sb)))
		return -EIO;

	return filemap_splice_read(in, ppos, pipe, len, flags);
}

const struct file_operations exfat_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= exfat_file_read_iter,
	.write_iter	= exfat_file_write_iter,
	.unlocked_ioctl = exfat_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = exfat_compat_ioctl,
#endif
	.mmap		= exfat_file_mmap,
	.fsync		= exfat_file_fsync,
	.splice_read	= exfat_splice_read,
	.splice_write	= iter_file_splice_write,
};

const struct inode_operations exfat_file_inode_operations = {
	.setattr     = exfat_setattr,
	.getattr     = exfat_getattr,
};
