/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/time.h>
#include <linux/msdos_fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>

int fat_generic_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
	{
		u32 attr;

		if (inode->i_ino == MSDOS_ROOT_INO)
			attr = ATTR_DIR;
		else
			attr = fat_attr(inode);

		return put_user(attr, user_attr);
	}
	case FAT_IOCTL_SET_ATTRIBUTES:
	{
		u32 attr, oldattr;
		int err, is_dir = S_ISDIR(inode->i_mode);
		struct iattr ia;

		err = get_user(attr, user_attr);
		if (err)
			return err;

		mutex_lock(&inode->i_mutex);

		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			goto up_no_drop_write;

		/*
		 * ATTR_VOLUME and ATTR_DIR cannot be changed; this also
		 * prevents the user from turning us into a VFAT
		 * longname entry.  Also, we obviously can't set
		 * any of the NTFS attributes in the high 24 bits.
		 */
		attr &= 0xff & ~(ATTR_VOLUME | ATTR_DIR);
		/* Merge in ATTR_VOLUME and ATTR_DIR */
		attr |= (MSDOS_I(inode)->i_attrs & ATTR_VOLUME) |
			(is_dir ? ATTR_DIR : 0);
		oldattr = fat_attr(inode);

		/* Equivalent to a chmod() */
		ia.ia_valid = ATTR_MODE | ATTR_CTIME;
		if (is_dir) {
			ia.ia_mode = MSDOS_MKMODE(attr,
				S_IRWXUGO & ~sbi->options.fs_dmask)
				| S_IFDIR;
		} else {
			ia.ia_mode = MSDOS_MKMODE(attr,
				(S_IRUGO | S_IWUGO | (inode->i_mode & S_IXUGO))
				& ~sbi->options.fs_fmask)
				| S_IFREG;
		}

		/* The root directory has no attributes */
		if (inode->i_ino == MSDOS_ROOT_INO && attr != ATTR_DIR) {
			err = -EINVAL;
			goto up;
		}

		if (sbi->options.sys_immutable) {
			if ((attr | oldattr) & ATTR_SYS) {
				if (!capable(CAP_LINUX_IMMUTABLE)) {
					err = -EPERM;
					goto up;
				}
			}
		}

		/* This MUST be done before doing anything irreversible... */
		err = notify_change(filp->f_path.dentry, &ia);
		if (err)
			goto up;

		if (sbi->options.sys_immutable) {
			if (attr & ATTR_SYS)
				inode->i_flags |= S_IMMUTABLE;
			else
				inode->i_flags &= S_IMMUTABLE;
		}

		MSDOS_I(inode)->i_attrs = attr & ATTR_UNUSED;
		mark_inode_dirty(inode);
up:
		mnt_drop_write(filp->f_path.mnt);
up_no_drop_write:
		mutex_unlock(&inode->i_mutex);
		return err;
	}
	default:
		return -ENOTTY;	/* Inappropriate ioctl for device */
	}
}

static int fat_file_release(struct inode *inode, struct file *filp)
{
	if ((filp->f_mode & FMODE_WRITE) &&
	     MSDOS_SB(inode->i_sb)->options.flush) {
		fat_flush_inodes(inode->i_sb, inode, NULL);
		congestion_wait(WRITE, HZ/10);
	}
	return 0;
}

const struct file_operations fat_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.release	= fat_file_release,
	.ioctl		= fat_generic_ioctl,
	.fsync		= file_fsync,
	.splice_read	= generic_file_splice_read,
};

static int fat_cont_expand(struct inode *inode, loff_t size)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t start = inode->i_size, count = size - inode->i_size;
	int err;

	err = generic_cont_expand_simple(inode, size);
	if (err)
		goto out;

	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	if (IS_SYNC(inode))
		err = sync_page_range_nolock(inode, mapping, start, count);
out:
	return err;
}

/* Free all clusters after the skip'th cluster. */
static int fat_free(struct inode *inode, int skip)
{
	struct super_block *sb = inode->i_sb;
	int err, wait, free_start, i_start, i_logstart;

	if (MSDOS_I(inode)->i_start == 0)
		return 0;

	fat_cache_inval_inode(inode);

	wait = IS_DIRSYNC(inode);
	i_start = free_start = MSDOS_I(inode)->i_start;
	i_logstart = MSDOS_I(inode)->i_logstart;

	/* First, we write the new file size. */
	if (!skip) {
		MSDOS_I(inode)->i_start = 0;
		MSDOS_I(inode)->i_logstart = 0;
	}
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	if (wait) {
		err = fat_sync_inode(inode);
		if (err) {
			MSDOS_I(inode)->i_start = i_start;
			MSDOS_I(inode)->i_logstart = i_logstart;
			return err;
		}
	} else
		mark_inode_dirty(inode);

	/* Write a new EOF, and get the remaining cluster chain for freeing. */
	if (skip) {
		struct fat_entry fatent;
		int ret, fclus, dclus;

		ret = fat_get_cluster(inode, skip - 1, &fclus, &dclus);
		if (ret < 0)
			return ret;
		else if (ret == FAT_ENT_EOF)
			return 0;

		fatent_init(&fatent);
		ret = fat_ent_read(inode, &fatent, dclus);
		if (ret == FAT_ENT_EOF) {
			fatent_brelse(&fatent);
			return 0;
		} else if (ret == FAT_ENT_FREE) {
			fat_fs_panic(sb,
				     "%s: invalid cluster chain (i_pos %lld)",
				     __FUNCTION__, MSDOS_I(inode)->i_pos);
			ret = -EIO;
		} else if (ret > 0) {
			err = fat_ent_write(inode, &fatent, FAT_ENT_EOF, wait);
			if (err)
				ret = err;
		}
		fatent_brelse(&fatent);
		if (ret < 0)
			return ret;

		free_start = ret;
	}
	inode->i_blocks = skip << (MSDOS_SB(sb)->cluster_bits - 9);

	/* Freeing the remained cluster chain */
	return fat_free_clusters(inode, free_start);
}

void fat_truncate(struct inode *inode)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	const unsigned int cluster_size = sbi->cluster_size;
	int nr_clusters;

	/*
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (MSDOS_I(inode)->mmu_private > inode->i_size)
		MSDOS_I(inode)->mmu_private = inode->i_size;

	nr_clusters = (inode->i_size + (cluster_size - 1)) >> sbi->cluster_bits;

	lock_kernel();
	fat_free(inode, nr_clusters);
	unlock_kernel();
	fat_flush_inodes(inode->i_sb, inode, NULL);
}

int fat_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blksize = MSDOS_SB(inode->i_sb)->cluster_size;
	return 0;
}
EXPORT_SYMBOL_GPL(fat_getattr);

static int fat_check_mode(const struct msdos_sb_info *sbi, mode_t mode)
{
	mode_t mask, req = mode & ~S_IFMT;

	if (S_ISREG(mode))
		mask = sbi->options.fs_fmask;
	else
		mask = sbi->options.fs_dmask;

	/*
	 * Of the r and x bits, all (subject to umask) must be present. Of the
	 * w bits, either all (subject to umask) or none must be present.
	 */
	req &= ~mask;
	if ((req & (S_IRUGO | S_IXUGO)) != ((S_IRUGO | S_IXUGO) & ~mask))
		return -EPERM;
	if ((req & S_IWUGO) && ((req & S_IWUGO) != (S_IWUGO & ~mask)))
		return -EPERM;

	return 0;
}

int fat_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct msdos_sb_info *sbi = MSDOS_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	int mask, error = 0;

	lock_kernel();

	/*
	 * Expand the file. Since inode_setattr() updates ->i_size
	 * before calling the ->truncate(), but FAT needs to fill the
	 * hole before it.
	 */
	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size > inode->i_size) {
			error = fat_cont_expand(inode, attr->ia_size);
			if (error || attr->ia_valid == ATTR_SIZE)
				goto out;
			attr->ia_valid &= ~ATTR_SIZE;
		}
	}

	error = inode_change_ok(inode, attr);
	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}
	if (((attr->ia_valid & ATTR_UID) &&
	     (attr->ia_uid != sbi->options.fs_uid)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     (attr->ia_gid != sbi->options.fs_gid)))
		error = -EPERM;

	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}

	if (attr->ia_valid & ATTR_MODE) {
		error = fat_check_mode(sbi, attr->ia_mode);
		if (error != 0 && !sbi->options.quiet)
			goto out;
	}

	error = inode_setattr(inode, attr);
	if (error)
		goto out;

	if (S_ISDIR(inode->i_mode))
		mask = sbi->options.fs_dmask;
	else
		mask = sbi->options.fs_fmask;
	inode->i_mode &= S_IFMT | (S_IRWXUGO & ~mask);
out:
	unlock_kernel();
	return error;
}
EXPORT_SYMBOL_GPL(fat_setattr);

const struct inode_operations fat_file_inode_operations = {
	.truncate	= fat_truncate,
	.setattr	= fat_setattr,
	.getattr	= fat_getattr,
};
