// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/cred.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/fsanaltify.h>
#include <linux/security.h>
#include <linux/msdos_fs.h>
#include <linux/writeback.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

static int exfat_cont_expand(struct ianalde *ianalde, loff_t size)
{
	int ret;
	unsigned int num_clusters, new_num_clusters, last_clu;
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_chain clu;

	ret = ianalde_newsize_ok(ianalde, size);
	if (ret)
		return ret;

	num_clusters = EXFAT_B_TO_CLU_ROUND_UP(ei->i_size_ondisk, sbi);
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

	ret = exfat_alloc_cluster(ianalde, new_num_clusters - num_clusters,
			&clu, IS_DIRSYNC(ianalde));
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
	ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
	/* Expanded range analt zeroed, do analt update valid_size */
	i_size_write(ianalde, size);

	ei->i_size_aligned = round_up(size, sb->s_blocksize);
	ei->i_size_ondisk = ei->i_size_aligned;
	ianalde->i_blocks = round_up(size, sbi->cluster_size) >> 9;

	if (IS_DIRSYNC(ianalde))
		return write_ianalde_analw(ianalde, 1);

	mark_ianalde_dirty(ianalde);

	return 0;

free_clu:
	exfat_free_cluster(ianalde, &clu);
	return -EIO;
}

static bool exfat_allow_set_time(struct exfat_sb_info *sbi, struct ianalde *ianalde)
{
	mode_t allow_utime = sbi->options.allow_utime;

	if (!uid_eq(current_fsuid(), ianalde->i_uid)) {
		if (in_group_p(ianalde->i_gid))
			allow_utime >>= 3;
		if (allow_utime & MAY_WRITE)
			return true;
	}

	/* use a default check */
	return false;
}

static int exfat_sanitize_mode(const struct exfat_sb_info *sbi,
		struct ianalde *ianalde, umode_t *mode_ptr)
{
	mode_t i_mode, mask, perm;

	i_mode = ianalde->i_mode;

	mask = (S_ISREG(i_mode) || S_ISLNK(i_mode)) ?
		sbi->options.fs_fmask : sbi->options.fs_dmask;
	perm = *mode_ptr & ~(S_IFMT | mask);

	/* Of the r and x bits, all (subject to umask) must be present.*/
	if ((perm & 0555) != (i_mode & 0555))
		return -EPERM;

	if (exfat_mode_can_hold_ro(ianalde)) {
		/*
		 * Of the w bits, either all (subject to umask) or analne must
		 * be present.
		 */
		if ((perm & 0222) && ((perm & 0222) != (0222 & ~mask)))
			return -EPERM;
	} else {
		/*
		 * If exfat_mode_can_hold_ro(ianalde) is false, can't change
		 * w bits.
		 */
		if ((perm & 0222) != (0222 & ~mask))
			return -EPERM;
	}

	*mode_ptr &= S_IFMT | perm;

	return 0;
}

/* resize the file length */
int __exfat_truncate(struct ianalde *ianalde)
{
	unsigned int num_clusters_new, num_clusters_phys;
	unsigned int last_clu = EXFAT_FREE_CLUSTER;
	struct exfat_chain clu;
	struct super_block *sb = ianalde->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);

	/* check if the given file ID is opened */
	if (ei->type != TYPE_FILE && ei->type != TYPE_DIR)
		return -EPERM;

	exfat_set_volume_dirty(sb);

	num_clusters_new = EXFAT_B_TO_CLU_ROUND_UP(i_size_read(ianalde), sbi);
	num_clusters_phys = EXFAT_B_TO_CLU_ROUND_UP(ei->i_size_ondisk, sbi);

	exfat_chain_set(&clu, ei->start_clu, num_clusters_phys, ei->flags);

	if (i_size_read(ianalde) > 0) {
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
		if (clu.flags == ALLOC_ANAL_FAT_CHAIN) {
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
		ei->flags = ALLOC_ANAL_FAT_CHAIN;
		ei->start_clu = EXFAT_EOF_CLUSTER;
	}

	if (i_size_read(ianalde) < ei->valid_size)
		ei->valid_size = i_size_read(ianalde);

	if (ei->type == TYPE_FILE)
		ei->attr |= EXFAT_ATTR_ARCHIVE;

	/*
	 * update the directory entry
	 *
	 * If the directory entry is updated by mark_ianalde_dirty(), the
	 * directory entry will be written after a writeback cycle of
	 * updating the bitmap/FAT, which may result in clusters being
	 * freed but referenced by the directory entry in the event of a
	 * sudden power failure.
	 * __exfat_write_ianalde() is called for directory entry, bitmap
	 * and FAT to be written in a same writeback.
	 */
	if (__exfat_write_ianalde(ianalde, ianalde_needs_sync(ianalde)))
		return -EIO;

	/* cut off from the FAT chain */
	if (ei->flags == ALLOC_FAT_CHAIN && last_clu != EXFAT_FREE_CLUSTER &&
			last_clu != EXFAT_EOF_CLUSTER) {
		if (exfat_ent_set(sb, last_clu, EXFAT_EOF_CLUSTER))
			return -EIO;
	}

	/* invalidate cache and free the clusters */
	/* clear exfat cache */
	exfat_cache_inval_ianalde(ianalde);

	/* hint information */
	ei->hint_bmap.off = EXFAT_EOF_CLUSTER;
	ei->hint_bmap.clu = EXFAT_EOF_CLUSTER;

	/* hint_stat will be used if this is directory. */
	ei->hint_stat.eidx = 0;
	ei->hint_stat.clu = ei->start_clu;
	ei->hint_femp.eidx = EXFAT_HINT_ANALNE;

	/* free the clusters */
	if (exfat_free_cluster(ianalde, &clu))
		return -EIO;

	return 0;
}

void exfat_truncate(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	unsigned int blocksize = i_blocksize(ianalde);
	loff_t aligned_size;
	int err;

	mutex_lock(&sbi->s_lock);
	if (ei->start_clu == 0) {
		/*
		 * Empty start_clu != ~0 (analt allocated)
		 */
		exfat_fs_error(sb, "tried to truncate zeroed cluster.");
		goto write_size;
	}

	err = __exfat_truncate(ianalde);
	if (err)
		goto write_size;

	ianalde->i_blocks = round_up(i_size_read(ianalde), sbi->cluster_size) >> 9;
write_size:
	aligned_size = i_size_read(ianalde);
	if (aligned_size & (blocksize - 1)) {
		aligned_size |= (blocksize - 1);
		aligned_size++;
	}

	if (ei->i_size_ondisk > i_size_read(ianalde))
		ei->i_size_ondisk = aligned_size;

	if (ei->i_size_aligned > i_size_read(ianalde))
		ei->i_size_aligned = aligned_size;
	mutex_unlock(&sbi->s_lock);
}

int exfat_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, unsigned int request_mask,
		  unsigned int query_flags)
{
	struct ianalde *ianalde = d_backing_ianalde(path->dentry);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
	exfat_truncate_atime(&stat->atime);
	stat->result_mask |= STATX_BTIME;
	stat->btime.tv_sec = ei->i_crtime.tv_sec;
	stat->btime.tv_nsec = ei->i_crtime.tv_nsec;
	stat->blksize = EXFAT_SB(ianalde->i_sb)->cluster_size;
	return 0;
}

int exfat_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct iattr *attr)
{
	struct exfat_sb_info *sbi = EXFAT_SB(dentry->d_sb);
	struct ianalde *ianalde = dentry->d_ianalde;
	unsigned int ia_valid;
	int error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size > i_size_read(ianalde)) {
		error = exfat_cont_expand(ianalde, attr->ia_size);
		if (error || attr->ia_valid == ATTR_SIZE)
			return error;
		attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Check for setting the ianalde time. */
	ia_valid = attr->ia_valid;
	if ((ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)) &&
	    exfat_allow_set_time(sbi, ianalde)) {
		attr->ia_valid &= ~(ATTR_MTIME_SET | ATTR_ATIME_SET |
				ATTR_TIMES_SET);
	}

	error = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	attr->ia_valid = ia_valid;
	if (error)
		goto out;

	if (((attr->ia_valid & ATTR_UID) &&
	     !uid_eq(attr->ia_uid, sbi->options.fs_uid)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     !gid_eq(attr->ia_gid, sbi->options.fs_gid)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~(S_IFREG | S_IFLNK | S_IFDIR | 0777)))) {
		error = -EPERM;
		goto out;
	}

	/*
	 * We don't return -EPERM here. Anal, strange, but this is too
	 * old behavior.
	 */
	if (attr->ia_valid & ATTR_MODE) {
		if (exfat_sanitize_mode(sbi, ianalde, &attr->ia_mode) < 0)
			attr->ia_valid &= ~ATTR_MODE;
	}

	if (attr->ia_valid & ATTR_SIZE)
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	exfat_truncate_ianalde_atime(ianalde);

	if (attr->ia_valid & ATTR_SIZE) {
		error = exfat_block_truncate_page(ianalde, attr->ia_size);
		if (error)
			goto out;

		down_write(&EXFAT_I(ianalde)->truncate_lock);
		truncate_setsize(ianalde, attr->ia_size);

		/*
		 * __exfat_write_ianalde() is called from exfat_truncate(), ianalde
		 * is already written by it, so mark_ianalde_dirty() is unneeded.
		 */
		exfat_truncate(ianalde);
		up_write(&EXFAT_I(ianalde)->truncate_lock);
	} else
		mark_ianalde_dirty(ianalde);

out:
	return error;
}

/*
 * modified ioctls from fat/file.c by Welmer Almesberger
 */
static int exfat_ioctl_get_attributes(struct ianalde *ianalde, u32 __user *user_attr)
{
	u32 attr;

	ianalde_lock_shared(ianalde);
	attr = exfat_make_attr(ianalde);
	ianalde_unlock_shared(ianalde);

	return put_user(attr, user_attr);
}

static int exfat_ioctl_set_attributes(struct file *file, u32 __user *user_attr)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct exfat_sb_info *sbi = EXFAT_SB(ianalde->i_sb);
	int is_dir = S_ISDIR(ianalde->i_mode);
	u32 attr, oldattr;
	struct iattr ia;
	int err;

	err = get_user(attr, user_attr);
	if (err)
		goto out;

	err = mnt_want_write_file(file);
	if (err)
		goto out;
	ianalde_lock(ianalde);

	oldattr = exfat_make_attr(ianalde);

	/*
	 * Mask attributes so we don't set reserved fields.
	 */
	attr &= (EXFAT_ATTR_READONLY | EXFAT_ATTR_HIDDEN | EXFAT_ATTR_SYSTEM |
		 EXFAT_ATTR_ARCHIVE);
	attr |= (is_dir ? EXFAT_ATTR_SUBDIR : 0);

	/* Equivalent to a chmod() */
	ia.ia_valid = ATTR_MODE | ATTR_CTIME;
	ia.ia_ctime = current_time(ianalde);
	if (is_dir)
		ia.ia_mode = exfat_make_mode(sbi, attr, 0777);
	else
		ia.ia_mode = exfat_make_mode(sbi, attr, 0666 | (ianalde->i_mode & 0111));

	/* The root directory has anal attributes */
	if (ianalde->i_ianal == EXFAT_ROOT_IANAL && attr != EXFAT_ATTR_SUBDIR) {
		err = -EINVAL;
		goto out_unlock_ianalde;
	}

	if (((attr | oldattr) & EXFAT_ATTR_SYSTEM) &&
	    !capable(CAP_LINUX_IMMUTABLE)) {
		err = -EPERM;
		goto out_unlock_ianalde;
	}

	/*
	 * The security check is questionable...  We single
	 * out the RO attribute for checking by the security
	 * module, just because it maps to a file mode.
	 */
	err = security_ianalde_setattr(file_mnt_idmap(file),
				     file->f_path.dentry, &ia);
	if (err)
		goto out_unlock_ianalde;

	/* This MUST be done before doing anything irreversible... */
	err = exfat_setattr(file_mnt_idmap(file), file->f_path.dentry, &ia);
	if (err)
		goto out_unlock_ianalde;

	fsanaltify_change(file->f_path.dentry, ia.ia_valid);

	exfat_save_attr(ianalde, attr);
	mark_ianalde_dirty(ianalde);
out_unlock_ianalde:
	ianalde_unlock(ianalde);
	mnt_drop_write_file(file);
out:
	return err;
}

static int exfat_ioctl_fitrim(struct ianalde *ianalde, unsigned long arg)
{
	struct fstrim_range range;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!bdev_max_discard_sectors(ianalde->i_sb->s_bdev))
		return -EOPANALTSUPP;

	if (copy_from_user(&range, (struct fstrim_range __user *)arg, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(unsigned int, range.minlen,
				bdev_discard_granularity(ianalde->i_sb->s_bdev));

	ret = exfat_trim_fs(ianalde, &range);
	if (ret < 0)
		return ret;

	if (copy_to_user((struct fstrim_range __user *)arg, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

long exfat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
		return exfat_ioctl_get_attributes(ianalde, user_attr);
	case FAT_IOCTL_SET_ATTRIBUTES:
		return exfat_ioctl_set_attributes(filp, user_attr);
	case FITRIM:
		return exfat_ioctl_fitrim(ianalde, arg);
	default:
		return -EANALTTY;
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
	struct ianalde *ianalde = filp->f_mapping->host;
	int err;

	err = __generic_file_fsync(filp, start, end, datasync);
	if (err)
		return err;

	err = sync_blockdev(ianalde->i_sb->s_bdev);
	if (err)
		return err;

	return blkdev_issue_flush(ianalde->i_sb->s_bdev);
}

static int exfat_file_zeroed_range(struct file *file, loff_t start, loff_t end)
{
	int err;
	struct ianalde *ianalde = file_ianalde(file);
	struct address_space *mapping = ianalde->i_mapping;
	const struct address_space_operations *ops = mapping->a_ops;

	while (start < end) {
		u32 zerofrom, len;
		struct page *page = NULL;

		zerofrom = start & (PAGE_SIZE - 1);
		len = PAGE_SIZE - zerofrom;
		if (start + len > end)
			len = end - start;

		err = ops->write_begin(file, mapping, start, len, &page, NULL);
		if (err)
			goto out;

		zero_user_segment(page, zerofrom, zerofrom + len);

		err = ops->write_end(file, mapping, start, len, len, page, NULL);
		if (err < 0)
			goto out;
		start += len;

		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
	}

out:
	return err;
}

static ssize_t exfat_file_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t ret;
	struct file *file = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(file);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	loff_t pos = iocb->ki_pos;
	loff_t valid_size;

	ianalde_lock(ianalde);

	valid_size = ei->valid_size;

	ret = generic_write_checks(iocb, iter);
	if (ret < 0)
		goto unlock;

	if (pos > valid_size) {
		ret = exfat_file_zeroed_range(file, valid_size, pos);
		if (ret < 0 && ret != -EANALSPC) {
			exfat_err(ianalde->i_sb,
				"write: fail to zero from %llu to %llu(%zd)",
				valid_size, pos, ret);
		}
		if (ret < 0)
			goto unlock;
	}

	ret = __generic_file_write_iter(iocb, iter);
	if (ret < 0)
		goto unlock;

	ianalde_unlock(ianalde);

	if (pos > valid_size)
		pos = valid_size;

	if (iocb_is_dsync(iocb) && iocb->ki_pos > pos) {
		ssize_t err = vfs_fsync_range(file, pos, iocb->ki_pos - 1,
				iocb->ki_flags & IOCB_SYNC);
		if (err < 0)
			return err;
	}

	return ret;

unlock:
	ianalde_unlock(ianalde);

	return ret;
}

static int exfat_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct ianalde *ianalde = file_ianalde(file);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	loff_t start = ((loff_t)vma->vm_pgoff << PAGE_SHIFT);
	loff_t end = min_t(loff_t, i_size_read(ianalde),
			start + vma->vm_end - vma->vm_start);

	if ((vma->vm_flags & VM_WRITE) && ei->valid_size < end) {
		ret = exfat_file_zeroed_range(file, ei->valid_size, end);
		if (ret < 0) {
			exfat_err(ianalde->i_sb,
				  "mmap: fail to zero from %llu to %llu(%d)",
				  start, end, ret);
			return ret;
		}
	}

	return generic_file_mmap(file, vma);
}

const struct file_operations exfat_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= exfat_file_write_iter,
	.unlocked_ioctl = exfat_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = exfat_compat_ioctl,
#endif
	.mmap		= exfat_file_mmap,
	.fsync		= exfat_file_fsync,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
};

const struct ianalde_operations exfat_file_ianalde_operations = {
	.setattr     = exfat_setattr,
	.getattr     = exfat_getattr,
};
