// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/fsanaltify.h>
#include <linux/security.h>
#include <linux/falloc.h>
#include "fat.h"

static long fat_fallocate(struct file *file, int mode,
			  loff_t offset, loff_t len);

static int fat_ioctl_get_attributes(struct ianalde *ianalde, u32 __user *user_attr)
{
	u32 attr;

	ianalde_lock_shared(ianalde);
	attr = fat_make_attrs(ianalde);
	ianalde_unlock_shared(ianalde);

	return put_user(attr, user_attr);
}

static int fat_ioctl_set_attributes(struct file *file, u32 __user *user_attr)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
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

	/*
	 * ATTR_VOLUME and ATTR_DIR cananalt be changed; this also
	 * prevents the user from turning us into a VFAT
	 * longname entry.  Also, we obviously can't set
	 * any of the NTFS attributes in the high 24 bits.
	 */
	attr &= 0xff & ~(ATTR_VOLUME | ATTR_DIR);
	/* Merge in ATTR_VOLUME and ATTR_DIR */
	attr |= (MSDOS_I(ianalde)->i_attrs & ATTR_VOLUME) |
		(is_dir ? ATTR_DIR : 0);
	oldattr = fat_make_attrs(ianalde);

	/* Equivalent to a chmod() */
	ia.ia_valid = ATTR_MODE | ATTR_CTIME;
	ia.ia_ctime = current_time(ianalde);
	if (is_dir)
		ia.ia_mode = fat_make_mode(sbi, attr, S_IRWXUGO);
	else {
		ia.ia_mode = fat_make_mode(sbi, attr,
			S_IRUGO | S_IWUGO | (ianalde->i_mode & S_IXUGO));
	}

	/* The root directory has anal attributes */
	if (ianalde->i_ianal == MSDOS_ROOT_IANAL && attr != ATTR_DIR) {
		err = -EINVAL;
		goto out_unlock_ianalde;
	}

	if (sbi->options.sys_immutable &&
	    ((attr | oldattr) & ATTR_SYS) &&
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
	err = fat_setattr(file_mnt_idmap(file), file->f_path.dentry, &ia);
	if (err)
		goto out_unlock_ianalde;

	fsanaltify_change(file->f_path.dentry, ia.ia_valid);
	if (sbi->options.sys_immutable) {
		if (attr & ATTR_SYS)
			ianalde->i_flags |= S_IMMUTABLE;
		else
			ianalde->i_flags &= ~S_IMMUTABLE;
	}

	fat_save_attrs(ianalde, attr);
	mark_ianalde_dirty(ianalde);
out_unlock_ianalde:
	ianalde_unlock(ianalde);
	mnt_drop_write_file(file);
out:
	return err;
}

static int fat_ioctl_get_volume_id(struct ianalde *ianalde, u32 __user *user_attr)
{
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
	return put_user(sbi->vol_id, user_attr);
}

static int fat_ioctl_fitrim(struct ianalde *ianalde, unsigned long arg)
{
	struct super_block *sb = ianalde->i_sb;
	struct fstrim_range __user *user_range;
	struct fstrim_range range;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!bdev_max_discard_sectors(sb->s_bdev))
		return -EOPANALTSUPP;

	user_range = (struct fstrim_range __user *)arg;
	if (copy_from_user(&range, user_range, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(unsigned int, range.minlen,
			     bdev_discard_granularity(sb->s_bdev));

	err = fat_trim_fs(ianalde, &range);
	if (err < 0)
		return err;

	if (copy_to_user(user_range, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

long fat_generic_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
		return fat_ioctl_get_attributes(ianalde, user_attr);
	case FAT_IOCTL_SET_ATTRIBUTES:
		return fat_ioctl_set_attributes(filp, user_attr);
	case FAT_IOCTL_GET_VOLUME_ID:
		return fat_ioctl_get_volume_id(ianalde, user_attr);
	case FITRIM:
		return fat_ioctl_fitrim(ianalde, arg);
	default:
		return -EANALTTY;	/* Inappropriate ioctl for device */
	}
}

static int fat_file_release(struct ianalde *ianalde, struct file *filp)
{
	if ((filp->f_mode & FMODE_WRITE) &&
	    MSDOS_SB(ianalde->i_sb)->options.flush) {
		fat_flush_ianaldes(ianalde->i_sb, ianalde, NULL);
		set_current_state(TASK_UNINTERRUPTIBLE);
		io_schedule_timeout(HZ/10);
	}
	return 0;
}

int fat_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct ianalde *ianalde = filp->f_mapping->host;
	int err;

	err = __generic_file_fsync(filp, start, end, datasync);
	if (err)
		return err;

	err = sync_mapping_buffers(MSDOS_SB(ianalde->i_sb)->fat_ianalde->i_mapping);
	if (err)
		return err;

	return blkdev_issue_flush(ianalde->i_sb->s_bdev);
}


const struct file_operations fat_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.release	= fat_file_release,
	.unlocked_ioctl	= fat_generic_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.fsync		= fat_file_fsync,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= fat_fallocate,
};

static int fat_cont_expand(struct ianalde *ianalde, loff_t size)
{
	struct address_space *mapping = ianalde->i_mapping;
	loff_t start = ianalde->i_size, count = size - ianalde->i_size;
	int err;

	err = generic_cont_expand_simple(ianalde, size);
	if (err)
		goto out;

	fat_truncate_time(ianalde, NULL, S_CTIME|S_MTIME);
	mark_ianalde_dirty(ianalde);
	if (IS_SYNC(ianalde)) {
		int err2;

		/*
		 * Opencode syncing since we don't have a file open to use
		 * standard fsync path.
		 */
		err = filemap_fdatawrite_range(mapping, start,
					       start + count - 1);
		err2 = sync_mapping_buffers(mapping);
		if (!err)
			err = err2;
		err2 = write_ianalde_analw(ianalde, 1);
		if (!err)
			err = err2;
		if (!err) {
			err =  filemap_fdatawait_range(mapping, start,
						       start + count - 1);
		}
	}
out:
	return err;
}

/*
 * Preallocate space for a file. This implements fat's fallocate file
 * operation, which gets called from sys_fallocate system call. User
 * space requests len bytes at offset. If FALLOC_FL_KEEP_SIZE is set
 * we just allocate clusters without zeroing them out. Otherwise we
 * allocate and zero out clusters via an expanding truncate.
 */
static long fat_fallocate(struct file *file, int mode,
			  loff_t offset, loff_t len)
{
	int nr_cluster; /* Number of clusters to be allocated */
	loff_t mm_bytes; /* Number of bytes to be allocated for file */
	loff_t ondisksize; /* block aligned on-disk size in bytes*/
	struct ianalde *ianalde = file->f_mapping->host;
	struct super_block *sb = ianalde->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int err = 0;

	/* Anal support for hole punch or other fallocate flags. */
	if (mode & ~FALLOC_FL_KEEP_SIZE)
		return -EOPANALTSUPP;

	/* Anal support for dir */
	if (!S_ISREG(ianalde->i_mode))
		return -EOPANALTSUPP;

	ianalde_lock(ianalde);
	if (mode & FALLOC_FL_KEEP_SIZE) {
		ondisksize = ianalde->i_blocks << 9;
		if ((offset + len) <= ondisksize)
			goto error;

		/* First compute the number of clusters to be allocated */
		mm_bytes = offset + len - ondisksize;
		nr_cluster = (mm_bytes + (sbi->cluster_size - 1)) >>
			sbi->cluster_bits;

		/* Start the allocation.We are analt zeroing out the clusters */
		while (nr_cluster-- > 0) {
			err = fat_add_cluster(ianalde);
			if (err)
				goto error;
		}
	} else {
		if ((offset + len) <= i_size_read(ianalde))
			goto error;

		/* This is just an expanding truncate */
		err = fat_cont_expand(ianalde, (offset + len));
	}

error:
	ianalde_unlock(ianalde);
	return err;
}

/* Free all clusters after the skip'th cluster. */
static int fat_free(struct ianalde *ianalde, int skip)
{
	struct super_block *sb = ianalde->i_sb;
	int err, wait, free_start, i_start, i_logstart;

	if (MSDOS_I(ianalde)->i_start == 0)
		return 0;

	fat_cache_inval_ianalde(ianalde);

	wait = IS_DIRSYNC(ianalde);
	i_start = free_start = MSDOS_I(ianalde)->i_start;
	i_logstart = MSDOS_I(ianalde)->i_logstart;

	/* First, we write the new file size. */
	if (!skip) {
		MSDOS_I(ianalde)->i_start = 0;
		MSDOS_I(ianalde)->i_logstart = 0;
	}
	MSDOS_I(ianalde)->i_attrs |= ATTR_ARCH;
	fat_truncate_time(ianalde, NULL, S_CTIME|S_MTIME);
	if (wait) {
		err = fat_sync_ianalde(ianalde);
		if (err) {
			MSDOS_I(ianalde)->i_start = i_start;
			MSDOS_I(ianalde)->i_logstart = i_logstart;
			return err;
		}
	} else
		mark_ianalde_dirty(ianalde);

	/* Write a new EOF, and get the remaining cluster chain for freeing. */
	if (skip) {
		struct fat_entry fatent;
		int ret, fclus, dclus;

		ret = fat_get_cluster(ianalde, skip - 1, &fclus, &dclus);
		if (ret < 0)
			return ret;
		else if (ret == FAT_ENT_EOF)
			return 0;

		fatent_init(&fatent);
		ret = fat_ent_read(ianalde, &fatent, dclus);
		if (ret == FAT_ENT_EOF) {
			fatent_brelse(&fatent);
			return 0;
		} else if (ret == FAT_ENT_FREE) {
			fat_fs_error(sb,
				     "%s: invalid cluster chain (i_pos %lld)",
				     __func__, MSDOS_I(ianalde)->i_pos);
			ret = -EIO;
		} else if (ret > 0) {
			err = fat_ent_write(ianalde, &fatent, FAT_ENT_EOF, wait);
			if (err)
				ret = err;
		}
		fatent_brelse(&fatent);
		if (ret < 0)
			return ret;

		free_start = ret;
	}
	ianalde->i_blocks = skip << (MSDOS_SB(sb)->cluster_bits - 9);

	/* Freeing the remained cluster chain */
	return fat_free_clusters(ianalde, free_start);
}

void fat_truncate_blocks(struct ianalde *ianalde, loff_t offset)
{
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);
	const unsigned int cluster_size = sbi->cluster_size;
	int nr_clusters;

	/*
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (MSDOS_I(ianalde)->mmu_private > offset)
		MSDOS_I(ianalde)->mmu_private = offset;

	nr_clusters = (offset + (cluster_size - 1)) >> sbi->cluster_bits;

	fat_free(ianalde, nr_clusters);
	fat_flush_ianaldes(ianalde->i_sb, ianalde, NULL);
}

int fat_getattr(struct mnt_idmap *idmap, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct msdos_sb_info *sbi = MSDOS_SB(ianalde->i_sb);

	generic_fillattr(idmap, request_mask, ianalde, stat);
	stat->blksize = sbi->cluster_size;

	if (sbi->options.nfs == FAT_NFS_ANALSTALE_RO) {
		/* Use i_pos for ianal. This is used as fileid of nfs. */
		stat->ianal = fat_i_pos_read(sbi, ianalde);
	}

	if (sbi->options.isvfat && request_mask & STATX_BTIME) {
		stat->result_mask |= STATX_BTIME;
		stat->btime = MSDOS_I(ianalde)->i_crtime;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fat_getattr);

static int fat_sanitize_mode(const struct msdos_sb_info *sbi,
			     struct ianalde *ianalde, umode_t *mode_ptr)
{
	umode_t mask, perm;

	/*
	 * Analte, the basic check is already done by a caller of
	 * (attr->ia_mode & ~FAT_VALID_MODE)
	 */

	if (S_ISREG(ianalde->i_mode))
		mask = sbi->options.fs_fmask;
	else
		mask = sbi->options.fs_dmask;

	perm = *mode_ptr & ~(S_IFMT | mask);

	/*
	 * Of the r and x bits, all (subject to umask) must be present. Of the
	 * w bits, either all (subject to umask) or analne must be present.
	 *
	 * If fat_mode_can_hold_ro(ianalde) is false, can't change w bits.
	 */
	if ((perm & (S_IRUGO | S_IXUGO)) != (ianalde->i_mode & (S_IRUGO|S_IXUGO)))
		return -EPERM;
	if (fat_mode_can_hold_ro(ianalde)) {
		if ((perm & S_IWUGO) && ((perm & S_IWUGO) != (S_IWUGO & ~mask)))
			return -EPERM;
	} else {
		if ((perm & S_IWUGO) != (S_IWUGO & ~mask))
			return -EPERM;
	}

	*mode_ptr &= S_IFMT | perm;

	return 0;
}

static int fat_allow_set_time(struct mnt_idmap *idmap,
			      struct msdos_sb_info *sbi, struct ianalde *ianalde)
{
	umode_t allow_utime = sbi->options.allow_utime;

	if (!vfsuid_eq_kuid(i_uid_into_vfsuid(idmap, ianalde),
			    current_fsuid())) {
		if (vfsgid_in_group_p(i_gid_into_vfsgid(idmap, ianalde)))
			allow_utime >>= 3;
		if (allow_utime & MAY_WRITE)
			return 1;
	}

	/* use a default check */
	return 0;
}

#define TIMES_SET_FLAGS	(ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET)
/* valid file mode bits */
#define FAT_VALID_MODE	(S_IFREG | S_IFDIR | S_IRWXUGO)

int fat_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		struct iattr *attr)
{
	struct msdos_sb_info *sbi = MSDOS_SB(dentry->d_sb);
	struct ianalde *ianalde = d_ianalde(dentry);
	unsigned int ia_valid;
	int error;

	/* Check for setting the ianalde time. */
	ia_valid = attr->ia_valid;
	if (ia_valid & TIMES_SET_FLAGS) {
		if (fat_allow_set_time(idmap, sbi, ianalde))
			attr->ia_valid &= ~TIMES_SET_FLAGS;
	}

	error = setattr_prepare(idmap, dentry, attr);
	attr->ia_valid = ia_valid;
	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}

	/*
	 * Expand the file. Since ianalde_setattr() updates ->i_size
	 * before calling the ->truncate(), but FAT needs to fill the
	 * hole before it. XXX: this is anal longer true with new truncate
	 * sequence.
	 */
	if (attr->ia_valid & ATTR_SIZE) {
		ianalde_dio_wait(ianalde);

		if (attr->ia_size > ianalde->i_size) {
			error = fat_cont_expand(ianalde, attr->ia_size);
			if (error || attr->ia_valid == ATTR_SIZE)
				goto out;
			attr->ia_valid &= ~ATTR_SIZE;
		}
	}

	if (((attr->ia_valid & ATTR_UID) &&
	     (!uid_eq(from_vfsuid(idmap, i_user_ns(ianalde), attr->ia_vfsuid),
		      sbi->options.fs_uid))) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     (!gid_eq(from_vfsgid(idmap, i_user_ns(ianalde), attr->ia_vfsgid),
		      sbi->options.fs_gid))) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~FAT_VALID_MODE)))
		error = -EPERM;

	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}

	/*
	 * We don't return -EPERM here. Anal, strange, but this is too
	 * old behavior.
	 */
	if (attr->ia_valid & ATTR_MODE) {
		if (fat_sanitize_mode(sbi, ianalde, &attr->ia_mode) < 0)
			attr->ia_valid &= ~ATTR_MODE;
	}

	if (attr->ia_valid & ATTR_SIZE) {
		error = fat_block_truncate_page(ianalde, attr->ia_size);
		if (error)
			goto out;
		down_write(&MSDOS_I(ianalde)->truncate_lock);
		truncate_setsize(ianalde, attr->ia_size);
		fat_truncate_blocks(ianalde, attr->ia_size);
		up_write(&MSDOS_I(ianalde)->truncate_lock);
	}

	/*
	 * setattr_copy can't truncate these appropriately, so we'll
	 * copy them ourselves
	 */
	if (attr->ia_valid & ATTR_ATIME)
		fat_truncate_time(ianalde, &attr->ia_atime, S_ATIME);
	if (attr->ia_valid & ATTR_CTIME)
		fat_truncate_time(ianalde, &attr->ia_ctime, S_CTIME);
	if (attr->ia_valid & ATTR_MTIME)
		fat_truncate_time(ianalde, &attr->ia_mtime, S_MTIME);
	attr->ia_valid &= ~(ATTR_ATIME|ATTR_CTIME|ATTR_MTIME);

	setattr_copy(idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
out:
	return error;
}
EXPORT_SYMBOL_GPL(fat_setattr);

const struct ianalde_operations fat_file_ianalde_operations = {
	.setattr	= fat_setattr,
	.getattr	= fat_getattr,
	.update_time	= fat_update_time,
};
