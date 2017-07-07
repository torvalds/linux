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
#include <linux/exportfs.h>
#include "overlayfs.h"
#include "ovl_entry.h"

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

static struct ovl_fh *ovl_encode_fh(struct dentry *lower, uuid_t *uuid)
{
	struct ovl_fh *fh;
	int fh_type, fh_len, dwords;
	void *buf;
	int buflen = MAX_HANDLE_SZ;

	buf = kmalloc(buflen, GFP_TEMPORARY);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	/*
	 * We encode a non-connectable file handle for non-dir, because we
	 * only need to find the lower inode number and we don't want to pay
	 * the price or reconnecting the dentry.
	 */
	dwords = buflen >> 2;
	fh_type = exportfs_encode_fh(lower, buf, &dwords, 0);
	buflen = (dwords << 2);

	fh = ERR_PTR(-EIO);
	if (WARN_ON(fh_type < 0) ||
	    WARN_ON(buflen > MAX_HANDLE_SZ) ||
	    WARN_ON(fh_type == FILEID_INVALID))
		goto out;

	BUILD_BUG_ON(MAX_HANDLE_SZ + offsetof(struct ovl_fh, fid) > 255);
	fh_len = offsetof(struct ovl_fh, fid) + buflen;
	fh = kmalloc(fh_len, GFP_KERNEL);
	if (!fh) {
		fh = ERR_PTR(-ENOMEM);
		goto out;
	}

	fh->version = OVL_FH_VERSION;
	fh->magic = OVL_FH_MAGIC;
	fh->type = fh_type;
	fh->flags = OVL_FH_FLAG_CPU_ENDIAN;
	fh->len = fh_len;
	fh->uuid = *uuid;
	memcpy(fh->fid, buf, buflen);

out:
	kfree(buf);
	return fh;
}

static int ovl_set_origin(struct dentry *dentry, struct dentry *lower,
			  struct dentry *upper)
{
	struct super_block *sb = lower->d_sb;
	const struct ovl_fh *fh = NULL;
	int err;

	/*
	 * When lower layer doesn't support export operations store a 'null' fh,
	 * so we can use the overlay.origin xattr to distignuish between a copy
	 * up and a pure upper inode.
	 */
	if (sb->s_export_op && sb->s_export_op->fh_to_dentry &&
	    !uuid_is_null(&sb->s_uuid)) {
		fh = ovl_encode_fh(lower, &sb->s_uuid);
		if (IS_ERR(fh))
			return PTR_ERR(fh);
	}

	/*
	 * Do not fail when upper doesn't support xattrs.
	 */
	err = ovl_check_setxattr(dentry, upper, OVL_XATTR_ORIGIN, fh,
				 fh ? fh->len : 0, 0);
	kfree(fh);

	return err;
}

static int ovl_copy_up_locked(struct dentry *workdir, struct dentry *upperdir,
			      struct dentry *dentry, struct path *lowerpath,
			      struct kstat *stat, const char *link,
			      struct kstat *pstat, bool tmpfile)
{
	struct inode *wdir = workdir->d_inode;
	struct inode *udir = upperdir->d_inode;
	struct dentry *newdentry = NULL;
	struct dentry *upper = NULL;
	struct dentry *temp = NULL;
	int err;
	const struct cred *old_creds = NULL;
	struct cred *new_creds = NULL;
	struct cattr cattr = {
		/* Can't properly set mode on creation because of the umask */
		.mode = stat->mode & S_IFMT,
		.rdev = stat->rdev,
		.link = link
	};

	err = security_inode_copy_up(dentry, &new_creds);
	if (err < 0)
		goto out;

	if (new_creds)
		old_creds = override_creds(new_creds);

	if (tmpfile)
		temp = ovl_do_tmpfile(upperdir, stat->mode);
	else
		temp = ovl_lookup_temp(workdir);
	err = 0;
	if (IS_ERR(temp)) {
		err = PTR_ERR(temp);
		temp = NULL;
	}

	if (!err && !tmpfile)
		err = ovl_create_real(wdir, temp, &cattr, NULL, true);

	if (new_creds) {
		revert_creds(old_creds);
		put_cred(new_creds);
	}

	if (err)
		goto out;

	if (S_ISREG(stat->mode)) {
		struct path upperpath;

		ovl_path_upper(dentry, &upperpath);
		BUG_ON(upperpath.dentry != NULL);
		upperpath.dentry = temp;

		if (tmpfile) {
			inode_unlock(udir);
			err = ovl_copy_up_data(lowerpath, &upperpath,
					       stat->size);
			inode_lock_nested(udir, I_MUTEX_PARENT);
		} else {
			err = ovl_copy_up_data(lowerpath, &upperpath,
					       stat->size);
		}

		if (err)
			goto out_cleanup;
	}

	err = ovl_copy_xattr(lowerpath->dentry, temp);
	if (err)
		goto out_cleanup;

	inode_lock(temp->d_inode);
	err = ovl_set_attr(temp, stat);
	inode_unlock(temp->d_inode);
	if (err)
		goto out_cleanup;

	/*
	 * Store identifier of lower inode in upper inode xattr to
	 * allow lookup of the copy up origin inode.
	 *
	 * Don't set origin when we are breaking the association with a lower
	 * hard link.
	 */
	if (S_ISDIR(stat->mode) || stat->nlink == 1) {
		err = ovl_set_origin(dentry, lowerpath->dentry, temp);
		if (err)
			goto out_cleanup;
	}

	upper = lookup_one_len(dentry->d_name.name, upperdir,
			       dentry->d_name.len);
	if (IS_ERR(upper)) {
		err = PTR_ERR(upper);
		upper = NULL;
		goto out_cleanup;
	}

	if (tmpfile)
		err = ovl_do_link(temp, udir, upper, true);
	else
		err = ovl_do_rename(wdir, temp, udir, upper, 0);
	if (err)
		goto out_cleanup;

	newdentry = dget(tmpfile ? upper : temp);
	ovl_dentry_update(dentry, newdentry);
	ovl_inode_update(d_inode(dentry), d_inode(newdentry));

	/* Restore timestamps on parent (best effort) */
	ovl_set_timestamps(upperdir, pstat);
out:
	dput(temp);
	dput(upper);
	return err;

out_cleanup:
	if (!tmpfile)
		ovl_cleanup(wdir, temp);
	goto out;
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
	struct ovl_fs *ofs = dentry->d_sb->s_fs_info;

	if (WARN_ON(!workdir))
		return -EROFS;

	ovl_do_check_copy_up(lowerdentry);

	ovl_path_upper(parent, &parentpath);
	upperdir = parentpath.dentry;

	/* Mark parent "impure" because it may now contain non-pure upper */
	err = ovl_set_impure(parent, upperdir);
	if (err)
		return err;

	err = vfs_getattr(&parentpath, &pstat,
			  STATX_ATIME | STATX_MTIME, AT_STATX_SYNC_AS_STAT);
	if (err)
		return err;

	if (S_ISLNK(stat->mode)) {
		link = vfs_get_link(lowerdentry, &done);
		if (IS_ERR(link))
			return PTR_ERR(link);
	}

	/* Should we copyup with O_TMPFILE or with workdir? */
	if (S_ISREG(stat->mode) && ofs->tmpfile) {
		err = ovl_copy_up_start(dentry);
		/* err < 0: interrupted, err > 0: raced with another copy-up */
		if (unlikely(err)) {
			pr_debug("ovl_copy_up_start(%pd2) = %i\n", dentry, err);
			if (err > 0)
				err = 0;
			goto out_done;
		}

		inode_lock_nested(upperdir->d_inode, I_MUTEX_PARENT);
		err = ovl_copy_up_locked(workdir, upperdir, dentry, lowerpath,
					 stat, link, &pstat, true);
		inode_unlock(upperdir->d_inode);
		ovl_copy_up_end(dentry);
		goto out_done;
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
				 stat, link, &pstat, false);
out_unlock:
	unlock_rename(workdir, upperdir);
out_done:
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
		err = vfs_getattr(&lowerpath, &stat,
				  STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
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
