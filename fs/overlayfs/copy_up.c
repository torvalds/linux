/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/splice.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/cred.h>
#include <linux/namei.h>
#include <linux/fdtable.h>
#include <linux/ratelimit.h>
#include "overlayfs.h"

#define OVL_COPY_UP_CHUNK_SIZE (1 << 20)

static bool __read_mostly ovl_check_copy_up;
module_param_named(check_copy_up, ovl_check_copy_up, bool,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(ovl_check_copy_up,
		 "Warn on copy-up when causing process also has a R/O fd open");

static int ovl_check_fd(const void *data, struct file *f, unsigned int fd)
{
	const struct dentry *dentry = data;

	if (file_inode(f) == d_inode(dentry))
		pr_warn_ratelimited("overlayfs: Warning: Copying up %pD, but open R/O on fd %u which will cease to be coherent [pid=%d %s]\n",
				    f, fd, current->pid, current->comm);
	return 0;
}

/*
 * Check the fds open by this process and warn if something like the following
 * scenario is about to occur:
 *
 *	fd1 = open("foo", O_RDONLY);
 *	fd2 = open("foo", O_RDWR);
 */
static void ovl_do_check_copy_up(struct dentry *dentry)
{
	if (ovl_check_copy_up)
		iterate_fd(current->files, 0, ovl_check_fd, dentry);
}

int ovl_copy_xattr(struct dentry *old, struct dentry *new)
{
	ssize_t list_size, size, value_size = 0;
	char *buf, *name, *value = NULL;
	int uninitialized_var(error);
	size_t slen;

	if (!(old->d_inode->i_opflags & IOP_XATTR) ||
	    !(new->d_inode->i_opflags & IOP_XATTR))
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

	list_size = vfs_listxattr(old, buf, list_size);
	if (list_size <= 0) {
		error = list_size;
		goto out;
	}

	for (name = buf; list_size; name += slen) {
		slen = strnlen(name, list_size) + 1;

		/* underlying fs providing us with an broken xattr list? */
		if (WARN_ON(slen > list_size)) {
			error = -EIO;
			break;
		}
		list_size -= slen;

		if (ovl_is_private_xattr(name))
			continue;
retry:
		size = vfs_getxattr(old, name, value, value_size);
		if (size == -ERANGE)
			size = vfs_getxattr(old, name, NULL, 0);

		if (size < 0) {
			error = size;
			break;
		}

		if (size > value_size) {
			void *new;

			new = krealloc(value, size, GFP_KERNEL);
			if (!new) {
				error = -ENOMEM;
				break;
			}
			value = new;
			value_size = size;
			goto retry;
		}

		error = security_inode_copy_up_xattr(name);
		if (error < 0 && error != -EOPNOTSUPP)
			break;
		if (error == 1) {
			error = 0;
			continue; /* Discard */
		}
		error = vfs_setxattr(new, name, value, size, 0);
		if (error)
			break;
	}
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

	old_file = ovl_path_open(old, O_LARGEFILE | O_RDONLY);
	if (IS_ERR(old_file))
		return PTR_ERR(old_file);

	new_file = ovl_path_open(new, O_LARGEFILE | O_WRONLY);
	if (IS_ERR(new_file)) {
		error = PTR_ERR(new_file);
		goto out_fput;
	}

	/* Try to use clone_file_range to clone up within the same fs */
	error = vfs_clone_file_range(old_file, 0, new_file, 0, len);
	if (!error)
		goto out;
	/* Couldn't clone, so now we try to copy the data */
	error = 0;

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
		WARN_ON(old_pos != new_pos);

		len -= bytes;
	}
out:
	if (!error)
		error = vfs_fsync(new_file, 0);
	fput(new_file);
out_fput:
	fput(old_file);
	return error;
}

static int ovl_set_timestamps(struct dentry *upperdentry, struct kstat *stat)
{
	struct iattr attr = {
		.ia_valid =
		     ATTR_ATIME | ATTR_MTIME | ATTR_ATIME_SET | ATTR_MTIME_SET,
		.ia_atime = stat->atime,
		.ia_mtime = stat->mtime,
	};

	return notify_change(upperdentry, &attr, NULL);
}

int ovl_set_attr(struct dentry *upperdentry, struct kstat *stat)
{
	int err = 0;

	if (!S_ISLNK(stat->mode)) {
		struct iattr attr = {
			.ia_valid = ATTR_MODE,
			.ia_mode = stat->mode,
		};
		err = notify_change(upperdentry, &attr, NULL);
	}
	if (!err) {
		struct iattr attr = {
			.ia_valid = ATTR_UID | ATTR_GID,
			.ia_uid = stat->uid,
			.ia_gid = stat->gid,
		};
		err = notify_change(upperdentry, &attr, NULL);
	}
	if (!err)
		ovl_set_timestamps(upperdentry, stat);

	return err;
}

static int ovl_copy_up_locked(struct dentry *workdir, struct dentry *upperdir,
			      struct dentry *dentry, struct path *lowerpath,
			      struct kstat *stat, const char *link)
{
	struct inode *wdir = workdir->d_inode;
	struct inode *udir = upperdir->d_inode;
	struct dentry *newdentry = NULL;
	struct dentry *upper = NULL;
	int err;
	const struct cred *old_creds = NULL;
	struct cred *new_creds = NULL;
	struct cattr cattr = {
		/* Can't properly set mode on creation because of the umask */
		.mode = stat->mode & S_IFMT,
		.rdev = stat->rdev,
		.link = link
	};

	newdentry = ovl_lookup_temp(workdir, dentry);
	err = PTR_ERR(newdentry);
	if (IS_ERR(newdentry))
		goto out;

	upper = lookup_one_len(dentry->d_name.name, upperdir,
			       dentry->d_name.len);
	err = PTR_ERR(upper);
	if (IS_ERR(upper))
		goto out1;

	err = security_inode_copy_up(dentry, &new_creds);
	if (err < 0)
		goto out2;

	if (new_creds)
		old_creds = override_creds(new_creds);

	err = ovl_create_real(wdir, newdentry, &cattr, NULL, true);

	if (new_creds) {
		revert_creds(old_creds);
		put_cred(new_creds);
	}

	if (err)
		goto out2;

	if (S_ISREG(stat->mode)) {
		struct path upperpath;

		ovl_path_upper(dentry, &upperpath);
		BUG_ON(upperpath.dentry != NULL);
		upperpath.dentry = newdentry;

		err = ovl_copy_up_data(lowerpath, &upperpath, stat->size);
		if (err)
			goto out_cleanup;
	}

	err = ovl_copy_xattr(lowerpath->dentry, newdentry);
	if (err)
		goto out_cleanup;

	inode_lock(newdentry->d_inode);
	err = ovl_set_attr(newdentry, stat);
	inode_unlock(newdentry->d_inode);
	if (err)
		goto out_cleanup;

	err = ovl_do_rename(wdir, newdentry, udir, upper, 0);
	if (err)
		goto out_cleanup;

	ovl_dentry_update(dentry, newdentry);
	ovl_inode_update(d_inode(dentry), d_inode(newdentry));
	newdentry = NULL;
out2:
	dput(upper);
out1:
	dput(newdentry);
out:
	return err;

out_cleanup:
	ovl_cleanup(wdir, newdentry);
	goto out2;
}

/*
 * Copy up a single dentry
 *
 * All renames start with copy up of source if necessary.  The actual
 * rename will only proceed once the copy up was successful.  Copy up uses
 * upper parent i_mutex for exclusion.  Since rename can change d_parent it
 * is possible that the copy up will lock the old parent.  At that point
 * the file will have already been copied up anyway.
 */
static int ovl_copy_up_one(struct dentry *parent, struct dentry *dentry,
			   struct path *lowerpath, struct kstat *stat)
{
	DEFINE_DELAYED_CALL(done);
	struct dentry *workdir = ovl_workdir(dentry);
	int err;
	struct kstat pstat;
	struct path parentpath;
	struct dentry *lowerdentry = lowerpath->dentry;
	struct dentry *upperdir;
	const char *link = NULL;

	if (WARN_ON(!workdir))
		return -EROFS;

	ovl_do_check_copy_up(lowerdentry);

	ovl_path_upper(parent, &parentpath);
	upperdir = parentpath.dentry;

	err = vfs_getattr(&parentpath, &pstat);
	if (err)
		return err;

	if (S_ISLNK(stat->mode)) {
		link = vfs_get_link(lowerdentry, &done);
		if (IS_ERR(link))
			return PTR_ERR(link);
	}

	err = -EIO;
	if (lock_rename(workdir, upperdir) != NULL) {
		pr_err("overlayfs: failed to lock workdir+upperdir\n");
		goto out_unlock;
	}
	if (ovl_dentry_upper(dentry)) {
		/* Raced with another copy-up?  Nothing to do, then... */
		err = 0;
		goto out_unlock;
	}

	err = ovl_copy_up_locked(workdir, upperdir, dentry, lowerpath,
				 stat, link);
	if (!err) {
		/* Restore timestamps on parent (best effort) */
		ovl_set_timestamps(upperdir, &pstat);
	}
out_unlock:
	unlock_rename(workdir, upperdir);
	do_delayed_call(&done);

	return err;
}

int ovl_copy_up_flags(struct dentry *dentry, int flags)
{
	int err = 0;
	const struct cred *old_cred = ovl_override_creds(dentry->d_sb);

	while (!err) {
		struct dentry *next;
		struct dentry *parent;
		struct path lowerpath;
		struct kstat stat;
		enum ovl_path_type type = ovl_path_type(dentry);

		if (OVL_TYPE_UPPER(type))
			break;

		next = dget(dentry);
		/* find the topmost dentry not yet copied up */
		for (;;) {
			parent = dget_parent(next);

			type = ovl_path_type(parent);
			if (OVL_TYPE_UPPER(type))
				break;

			dput(next);
			next = parent;
		}

		ovl_path_lower(next, &lowerpath);
		err = vfs_getattr(&lowerpath, &stat);
		/* maybe truncate regular file. this has no effect on dirs */
		if (flags & O_TRUNC)
			stat.size = 0;
		if (!err)
			err = ovl_copy_up_one(parent, next, &lowerpath, &stat);

		dput(parent);
		dput(next);
	}
	revert_creds(old_cred);

	return err;
}

int ovl_copy_up(struct dentry *dentry)
{
	return ovl_copy_up_flags(dentry, 0);
}
