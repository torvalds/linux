/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/splice.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include "overlayfs.h"

#define OVL_COPY_UP_CHUNK_SIZE (1 << 20)

static int ovl_copy_up_xattr(struct dentry *old, struct dentry *new)
{
	ssize_t list_size, size;
	char *buf, *name, *value;
	int error;

	if (!old->d_inode->i_op->getxattr ||
	    !new->d_inode->i_op->getxattr)
		return 0;

	list_size = vfs_listxattr(old, NULL, 0);
	if (list_size <= 0) {
		if (list_size == -EOPNOTSUPP)
			return 0;
		return list_size;
	}

	buf = kzalloc(list_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	error = -ENOMEM;
	value = kmalloc(XATTR_SIZE_MAX, GFP_KERNEL);
	if (!value)
		goto out;

	list_size = vfs_listxattr(old, buf, list_size);
	if (list_size <= 0) {
		error = list_size;
		goto out_free_value;
	}

	for (name = buf; name < (buf + list_size); name += strlen(name) + 1) {
		size = vfs_getxattr(old, name, value, XATTR_SIZE_MAX);
		if (size <= 0) {
			error = size;
			goto out_free_value;
		}
		error = vfs_setxattr(new, name, value, size, 0);
		if (error)
			goto out_free_value;
	}

out_free_value:
	kfree(value);
out:
	kfree(buf);
	return error;
}

static int ovl_copy_up_data(struct path *old, struct path *new, loff_t len)
{
	struct file *old_file;
	struct file *new_file;
	loff_t old_pos = 0;
	loff_t new_pos = 0;
	int error = 0;

	if (len == 0)
		return 0;

	old_file = ovl_path_open(old, O_RDONLY);
	if (IS_ERR(old_file))
		return PTR_ERR(old_file);

	new_file = ovl_path_open(new, O_WRONLY);
	if (IS_ERR(new_file)) {
		error = PTR_ERR(new_file);
		goto out_fput;
	}

	/* FIXME: copy up sparse files efficiently */
	while (len) {
		size_t this_len = OVL_COPY_UP_CHUNK_SIZE;
		long bytes;

		if (len < this_len)
			this_len = len;

		if (signal_pending_state(TASK_KILLABLE, current)) {
			error = -EINTR;
			break;
		}

		bytes = do_splice_direct(old_file, &old_pos,
					 new_file, &new_pos,
					 this_len, SPLICE_F_MOVE);
		if (bytes <= 0) {
			error = bytes;
			break;
		}

		len -= bytes;
	}

	fput(new_file);
out_fput:
	fput(old_file);
	return error;
}

static char *ovl_read_symlink(struct dentry *realdentry)
{
	int res;
	char *buf;
	struct inode *inode = realdentry->d_inode;
	mm_segment_t old_fs;

	res = -EINVAL;
	if (!inode->i_op->readlink)
		goto err;

	res = -ENOMEM;
	buf = (char *) __get_free_page(GFP_KERNEL);
	if (!buf)
		goto err;

	old_fs = get_fs();
	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	res = inode->i_op->readlink(realdentry,
				    (char __user *)buf, PAGE_SIZE - 1);
	set_fs(old_fs);
	if (res < 0) {
		free_page((unsigned long) buf);
		goto err;
	}
	buf[res] = '\0';

	return buf;

err:
	return ERR_PTR(res);
}

static int ovl_set_timestamps(struct dentry *upperdentry, struct kstat *stat)
{
	struct iattr attr = {
		.ia_valid =
		     ATTR_ATIME | ATTR_MTIME | ATTR_ATIME_SET | ATTR_MTIME_SET,
		.ia_atime = stat->atime,
		.ia_mtime = stat->mtime,
	};

	return notify_change(upperdentry, &attr);
}

static int ovl_set_mode(struct dentry *upperdentry, umode_t mode)
{
	struct iattr attr = {
		.ia_valid = ATTR_MODE,
		.ia_mode = mode,
	};

	return notify_change(upperdentry, &attr);
}

static int ovl_copy_up_locked(struct dentry *upperdir, struct dentry *dentry,
			      struct path *lowerpath, struct kstat *stat,
			      const char *link)
{
	int err;
	struct path newpath;
	umode_t mode = stat->mode;

	/* Can't properly set mode on creation because of the umask */
	stat->mode &= S_IFMT;

	ovl_path_upper(dentry, &newpath);
	WARN_ON(newpath.dentry);
	newpath.dentry = ovl_upper_create(upperdir, dentry, stat, link);
	if (IS_ERR(newpath.dentry))
		return PTR_ERR(newpath.dentry);

	if (S_ISREG(stat->mode)) {
		err = ovl_copy_up_data(lowerpath, &newpath, stat->size);
		if (err)
			goto err_remove;
	}

	err = ovl_copy_up_xattr(lowerpath->dentry, newpath.dentry);
	if (err)
		goto err_remove;

	mutex_lock(&newpath.dentry->d_inode->i_mutex);
	if (!S_ISLNK(stat->mode))
		err = ovl_set_mode(newpath.dentry, mode);
	if (!err)
		err = ovl_set_timestamps(newpath.dentry, stat);
	mutex_unlock(&newpath.dentry->d_inode->i_mutex);
	if (err)
		goto err_remove;

	ovl_dentry_update(dentry, newpath.dentry);

	/*
	 * Easiest way to get rid of the lower dentry reference is to
	 * drop this dentry.  This is neither needed nor possible for
	 * directories.
	 */
	if (!S_ISDIR(stat->mode))
		d_drop(dentry);

	return 0;

err_remove:
	if (S_ISDIR(stat->mode))
		vfs_rmdir(upperdir->d_inode, newpath.dentry);
	else
		vfs_unlink(upperdir->d_inode, newpath.dentry);

	dput(newpath.dentry);

	return err;
}

/*
 * Copy up a single dentry
 *
 * Directory renames only allowed on "pure upper" (already created on
 * upper filesystem, never copied up).  Directories which are on lower or
 * are merged may not be renamed.  For these -EXDEV is returned and
 * userspace has to deal with it.  This means, when copying up a
 * directory we can rely on it and ancestors being stable.
 *
 * Non-directory renames start with copy up of source if necessary.  The
 * actual rename will only proceed once the copy up was successful.  Copy
 * up uses upper parent i_mutex for exclusion.  Since rename can change
 * d_parent it is possible that the copy up will lock the old parent.  At
 * that point the file will have already been copied up anyway.
 */
static int ovl_copy_up_one(struct dentry *parent, struct dentry *dentry,
			   struct path *lowerpath, struct kstat *stat)
{
	int err;
	struct kstat pstat;
	struct path parentpath;
	struct dentry *upperdir;
	const struct cred *old_cred;
	struct cred *override_cred;
	char *link = NULL;

	ovl_path_upper(parent, &parentpath);
	upperdir = parentpath.dentry;

	err = vfs_getattr(&parentpath, &pstat);
	if (err)
		return err;

	if (S_ISLNK(stat->mode)) {
		link = ovl_read_symlink(lowerpath->dentry);
		if (IS_ERR(link))
			return PTR_ERR(link);
	}

	err = -ENOMEM;
	override_cred = prepare_creds();
	if (!override_cred)
		goto out_free_link;

	override_cred->fsuid = stat->uid;
	override_cred->fsgid = stat->gid;
	/*
	 * CAP_SYS_ADMIN for copying up extended attributes
	 * CAP_DAC_OVERRIDE for create
	 * CAP_FOWNER for chmod, timestamp update
	 * CAP_FSETID for chmod
	 * CAP_MKNOD for mknod
	 */
	cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
	cap_raise(override_cred->cap_effective, CAP_DAC_OVERRIDE);
	cap_raise(override_cred->cap_effective, CAP_FOWNER);
	cap_raise(override_cred->cap_effective, CAP_FSETID);
	cap_raise(override_cred->cap_effective, CAP_MKNOD);
	old_cred = override_creds(override_cred);

	mutex_lock_nested(&upperdir->d_inode->i_mutex, I_MUTEX_PARENT);
	if (ovl_path_type(dentry) != OVL_PATH_LOWER) {
		err = 0;
	} else {
		err = ovl_copy_up_locked(upperdir, dentry, lowerpath,
					 stat, link);
		if (!err) {
			/* Restore timestamps on parent (best effort) */
			ovl_set_timestamps(upperdir, &pstat);
		}
	}

	mutex_unlock(&upperdir->d_inode->i_mutex);

	revert_creds(old_cred);
	put_cred(override_cred);

out_free_link:
	if (link)
		free_page((unsigned long) link);

	return err;
}

int ovl_copy_up(struct dentry *dentry)
{
	int err;

	err = 0;
	while (!err) {
		struct dentry *next;
		struct dentry *parent;
		struct path lowerpath;
		struct kstat stat;
		enum ovl_path_type type = ovl_path_type(dentry);

		if (type != OVL_PATH_LOWER)
			break;

		next = dget(dentry);
		/* find the topmost dentry not yet copied up */
		for (;;) {
			parent = dget_parent(next);

			type = ovl_path_type(parent);
			if (type != OVL_PATH_LOWER)
				break;

			dput(next);
			next = parent;
		}

		ovl_path_lower(next, &lowerpath);
		err = vfs_getattr(&lowerpath, &stat);
		if (!err)
			err = ovl_copy_up_one(parent, next, &lowerpath, &stat);

		dput(parent);
		dput(next);
	}

	return err;
}

/* Optimize by not copying up the file first and truncating later */
int ovl_copy_up_truncate(struct dentry *dentry, loff_t size)
{
	int err;
	struct kstat stat;
	struct path lowerpath;
	struct dentry *parent = dget_parent(dentry);

	err = ovl_copy_up(parent);
	if (err)
		goto out_dput_parent;

	ovl_path_lower(dentry, &lowerpath);
	err = vfs_getattr(&lowerpath, &stat);
	if (err)
		goto out_dput_parent;

	if (size < stat.size)
		stat.size = size;

	err = ovl_copy_up_one(parent, dentry, &lowerpath, &stat);

out_dput_parent:
	dput(parent);
	return err;
}
