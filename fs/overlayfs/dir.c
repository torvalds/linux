/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include <linux/cred.h>
#include "overlayfs.h"

void ovl_cleanup(struct inode *wdir, struct dentry *wdentry)
{
	int err;

	dget(wdentry);
	if (d_is_dir(wdentry))
		err = ovl_do_rmdir(wdir, wdentry);
	else
		err = ovl_do_unlink(wdir, wdentry);
	dput(wdentry);

	if (err) {
		pr_err("overlayfs: cleanup of '%pd2' failed (%i)\n",
		       wdentry, err);
	}
}

struct dentry *ovl_lookup_temp(struct dentry *workdir, struct dentry *dentry)
{
	struct dentry *temp;
	char name[20];

	snprintf(name, sizeof(name), "#%lx", (unsigned long) dentry);

	temp = lookup_one_len(name, workdir, strlen(name));
	if (!IS_ERR(temp) && temp->d_inode) {
		pr_err("overlayfs: workdir/%s already exists\n", name);
		dput(temp);
		temp = ERR_PTR(-EIO);
	}

	return temp;
}

/* caller holds i_mutex on workdir */
static struct dentry *ovl_whiteout(struct dentry *workdir,
				   struct dentry *dentry)
{
	int err;
	struct dentry *whiteout;
	struct inode *wdir = workdir->d_inode;

	whiteout = ovl_lookup_temp(workdir, dentry);
	if (IS_ERR(whiteout))
		return whiteout;

	err = ovl_do_whiteout(wdir, whiteout);
	if (err) {
		dput(whiteout);
		whiteout = ERR_PTR(err);
	}

	return whiteout;
}

int ovl_create_real(struct inode *dir, struct dentry *newdentry,
		    struct kstat *stat, const char *link,
		    struct dentry *hardlink, bool debug)
{
	int err;

	if (newdentry->d_inode)
		return -ESTALE;

	if (hardlink) {
		err = ovl_do_link(hardlink, dir, newdentry, debug);
	} else {
		switch (stat->mode & S_IFMT) {
		case S_IFREG:
			err = ovl_do_create(dir, newdentry, stat->mode, debug);
			break;

		case S_IFDIR:
			err = ovl_do_mkdir(dir, newdentry, stat->mode, debug);
			break;

		case S_IFCHR:
		case S_IFBLK:
		case S_IFIFO:
		case S_IFSOCK:
			err = ovl_do_mknod(dir, newdentry,
					   stat->mode, stat->rdev, debug);
			break;

		case S_IFLNK:
			err = ovl_do_symlink(dir, newdentry, link, debug);
			break;

		default:
			err = -EPERM;
		}
	}
	if (!err && WARN_ON(!newdentry->d_inode)) {
		/*
		 * Not quite sure if non-instantiated dentry is legal or not.
		 * VFS doesn't seem to care so check and warn here.
		 */
		err = -ENOENT;
	}
	return err;
}

static int ovl_set_opaque(struct dentry *upperdentry)
{
	return ovl_do_setxattr(upperdentry, OVL_XATTR_OPAQUE, "y", 1, 0);
}

static void ovl_remove_opaque(struct dentry *upperdentry)
{
	int err;

	err = ovl_do_removexattr(upperdentry, OVL_XATTR_OPAQUE);
	if (err) {
		pr_warn("overlayfs: failed to remove opaque from '%s' (%i)\n",
			upperdentry->d_name.name, err);
	}
}

static int ovl_dir_getattr(struct vfsmount *mnt, struct dentry *dentry,
			 struct kstat *stat)
{
	int err;
	enum ovl_path_type type;
	struct path realpath;

	type = ovl_path_real(dentry, &realpath);
	err = vfs_getattr(&realpath, stat);
	if (err)
		return err;

	stat->dev = dentry->d_sb->s_dev;
	stat->ino = dentry->d_inode->i_ino;

	/*
	 * It's probably not worth it to count subdirs to get the
	 * correct link count.  nlink=1 seems to pacify 'find' and
	 * other utilities.
	 */
	if (OVL_TYPE_MERGE(type))
		stat->nlink = 1;

	return 0;
}

static int ovl_create_upper(struct dentry *dentry, struct inode *inode,
			    struct kstat *stat, const char *link,
			    struct dentry *hardlink)
{
	struct dentry *upperdir = ovl_dentry_upper(dentry->d_parent);
	struct inode *udir = upperdir->d_inode;
	struct dentry *newdentry;
	int err;

	inode_lock_nested(udir, I_MUTEX_PARENT);
	newdentry = lookup_one_len(dentry->d_name.name, upperdir,
				   dentry->d_name.len);
	err = PTR_ERR(newdentry);
	if (IS_ERR(newdentry))
		goto out_unlock;
	err = ovl_create_real(udir, newdentry, stat, link, hardlink, false);
	if (err)
		goto out_dput;

	ovl_dentry_version_inc(dentry->d_parent);
	ovl_dentry_update(dentry, newdentry);
	ovl_copyattr(newdentry->d_inode, inode);
	d_instantiate(dentry, inode);
	newdentry = NULL;
out_dput:
	dput(newdentry);
out_unlock:
	inode_unlock(udir);
	return err;
}

static int ovl_lock_rename_workdir(struct dentry *workdir,
				   struct dentry *upperdir)
{
	/* Workdir should not be the same as upperdir */
	if (workdir == upperdir)
		goto err;

	/* Workdir should not be subdir of upperdir and vice versa */
	if (lock_rename(workdir, upperdir) != NULL)
		goto err_unlock;

	return 0;

err_unlock:
	unlock_rename(workdir, upperdir);
err:
	pr_err("overlayfs: failed to lock workdir+upperdir\n");
	return -EIO;
}

static struct dentry *ovl_clear_empty(struct dentry *dentry,
				      struct list_head *list)
{
	struct dentry *workdir = ovl_workdir(dentry);
	struct inode *wdir = workdir->d_inode;
	struct dentry *upperdir = ovl_dentry_upper(dentry->d_parent);
	struct inode *udir = upperdir->d_inode;
	struct path upperpath;
	struct dentry *upper;
	struct dentry *opaquedir;
	struct kstat stat;
	int err;

	if (WARN_ON(!workdir))
		return ERR_PTR(-EROFS);

	err = ovl_lock_rename_workdir(workdir, upperdir);
	if (err)
		goto out;

	ovl_path_upper(dentry, &upperpath);
	err = vfs_getattr(&upperpath, &stat);
	if (err)
		goto out_unlock;

	err = -ESTALE;
	if (!S_ISDIR(stat.mode))
		goto out_unlock;
	upper = upperpath.dentry;
	if (upper->d_parent->d_inode != udir)
		goto out_unlock;

	opaquedir = ovl_lookup_temp(workdir, dentry);
	err = PTR_ERR(opaquedir);
	if (IS_ERR(opaquedir))
		goto out_unlock;

	err = ovl_create_real(wdir, opaquedir, &stat, NULL, NULL, true);
	if (err)
		goto out_dput;

	err = ovl_copy_xattr(upper, opaquedir);
	if (err)
		goto out_cleanup;

	err = ovl_set_opaque(opaquedir);
	if (err)
		goto out_cleanup;

	inode_lock(opaquedir->d_inode);
	err = ovl_set_attr(opaquedir, &stat);
	inode_unlock(opaquedir->d_inode);
	if (err)
		goto out_cleanup;

	err = ovl_do_rename(wdir, opaquedir, udir, upper, RENAME_EXCHANGE);
	if (err)
		goto out_cleanup;

	ovl_cleanup_whiteouts(upper, list);
	ovl_cleanup(wdir, upper);
	unlock_rename(workdir, upperdir);

	/* dentry's upper doesn't match now, get rid of it */
	d_drop(dentry);

	return opaquedir;

out_cleanup:
	ovl_cleanup(wdir, opaquedir);
out_dput:
	dput(opaquedir);
out_unlock:
	unlock_rename(workdir, upperdir);
out:
	return ERR_PTR(err);
}

static struct dentry *ovl_check_empty_and_clear(struct dentry *dentry)
{
	int err;
	struct dentry *ret = NULL;
	LIST_HEAD(list);

	err = ovl_check_empty_dir(dentry, &list);
	if (err)
		ret = ERR_PTR(err);
	else {
		/*
		 * If no upperdentry then skip clearing whiteouts.
		 *
		 * Can race with copy-up, since we don't hold the upperdir
		 * mutex.  Doesn't matter, since copy-up can't create a
		 * non-empty directory from an empty one.
		 */
		if (ovl_dentry_upper(dentry))
			ret = ovl_clear_empty(dentry, &list);
	}

	ovl_cache_free(&list);

	return ret;
}

static int ovl_create_over_whiteout(struct dentry *dentry, struct inode *inode,
				    struct kstat *stat, const char *link,
				    struct dentry *hardlink)
{
	struct dentry *workdir = ovl_workdir(dentry);
	struct inode *wdir = workdir->d_inode;
	struct dentry *upperdir = ovl_dentry_upper(dentry->d_parent);
	struct inode *udir = upperdir->d_inode;
	struct dentry *upper;
	struct dentry *newdentry;
	int err;

	if (WARN_ON(!workdir))
		return -EROFS;

	err = ovl_lock_rename_workdir(workdir, upperdir);
	if (err)
		goto out;

	newdentry = ovl_lookup_temp(workdir, dentry);
	err = PTR_ERR(newdentry);
	if (IS_ERR(newdentry))
		goto out_unlock;

	upper = lookup_one_len(dentry->d_name.name, upperdir,
			       dentry->d_name.len);
	err = PTR_ERR(upper);
	if (IS_ERR(upper))
		goto out_dput;

	err = ovl_create_real(wdir, newdentry, stat, link, hardlink, true);
	if (err)
		goto out_dput2;

	if (S_ISDIR(stat->mode)) {
		err = ovl_set_opaque(newdentry);
		if (err)
			goto out_cleanup;

		err = ovl_do_rename(wdir, newdentry, udir, upper,
				    RENAME_EXCHANGE);
		if (err)
			goto out_cleanup;

		ovl_cleanup(wdir, upper);
	} else {
		err = ovl_do_rename(wdir, newdentry, udir, upper, 0);
		if (err)
			goto out_cleanup;
	}
	ovl_dentry_version_inc(dentry->d_parent);
	ovl_dentry_update(dentry, newdentry);
	ovl_copyattr(newdentry->d_inode, inode);
	d_instantiate(dentry, inode);
	newdentry = NULL;
out_dput2:
	dput(upper);
out_dput:
	dput(newdentry);
out_unlock:
	unlock_rename(workdir, upperdir);
out:
	return err;

out_cleanup:
	ovl_cleanup(wdir, newdentry);
	goto out_dput2;
}

static int ovl_create_or_link(struct dentry *dentry, int mode, dev_t rdev,
			      const char *link, struct dentry *hardlink)
{
	int err;
	struct inode *inode;
	struct kstat stat = {
		.mode = mode,
		.rdev = rdev,
	};

	err = -ENOMEM;
	inode = ovl_new_inode(dentry->d_sb, mode, dentry->d_fsdata);
	if (!inode)
		goto out;

	err = ovl_copy_up(dentry->d_parent);
	if (err)
		goto out_iput;

	if (!ovl_dentry_is_opaque(dentry)) {
		err = ovl_create_upper(dentry, inode, &stat, link, hardlink);
	} else {
		const struct cred *old_cred;
		struct cred *override_cred;

		old_cred = ovl_override_creds(dentry->d_sb);

		err = -ENOMEM;
		override_cred = prepare_creds();
		if (override_cred) {
			override_cred->fsuid = old_cred->fsuid;
			override_cred->fsgid = old_cred->fsgid;
			put_cred(override_creds(override_cred));
			put_cred(override_cred);

			err = ovl_create_over_whiteout(dentry, inode, &stat,
						       link, hardlink);
		}
		revert_creds(old_cred);
	}

	if (!err)
		inode = NULL;
out_iput:
	iput(inode);
out:
	return err;
}

static int ovl_create_object(struct dentry *dentry, int mode, dev_t rdev,
			     const char *link)
{
	int err;

	err = ovl_want_write(dentry);
	if (!err) {
		err = ovl_create_or_link(dentry, mode, rdev, link, NULL);
		ovl_drop_write(dentry);
	}

	return err;
}

static int ovl_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		      bool excl)
{
	return ovl_create_object(dentry, (mode & 07777) | S_IFREG, 0, NULL);
}

static int ovl_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	return ovl_create_object(dentry, (mode & 07777) | S_IFDIR, 0, NULL);
}

static int ovl_mknod(struct inode *dir, struct dentry *dentry, umode_t mode,
		     dev_t rdev)
{
	/* Don't allow creation of "whiteout" on overlay */
	if (S_ISCHR(mode) && rdev == WHITEOUT_DEV)
		return -EPERM;

	return ovl_create_object(dentry, mode, rdev, NULL);
}

static int ovl_symlink(struct inode *dir, struct dentry *dentry,
		       const char *link)
{
	return ovl_create_object(dentry, S_IFLNK, 0, link);
}

static int ovl_link(struct dentry *old, struct inode *newdir,
		    struct dentry *new)
{
	int err;
	struct dentry *upper;

	err = ovl_want_write(old);
	if (err)
		goto out;

	err = ovl_copy_up(old);
	if (err)
		goto out_drop_write;

	upper = ovl_dentry_upper(old);
	err = ovl_create_or_link(new, upper->d_inode->i_mode, 0, NULL, upper);

out_drop_write:
	ovl_drop_write(old);
out:
	return err;
}

static int ovl_remove_and_whiteout(struct dentry *dentry, bool is_dir)
{
	struct dentry *workdir = ovl_workdir(dentry);
	struct inode *wdir = workdir->d_inode;
	struct dentry *upperdir = ovl_dentry_upper(dentry->d_parent);
	struct inode *udir = upperdir->d_inode;
	struct dentry *whiteout;
	struct dentry *upper;
	struct dentry *opaquedir = NULL;
	int err;
	int flags = 0;

	if (WARN_ON(!workdir))
		return -EROFS;

	if (is_dir) {
		if (OVL_TYPE_MERGE_OR_LOWER(ovl_path_type(dentry))) {
			opaquedir = ovl_check_empty_and_clear(dentry);
			err = PTR_ERR(opaquedir);
			if (IS_ERR(opaquedir))
				goto out;
		} else {
			LIST_HEAD(list);

			/*
			 * When removing an empty opaque directory, then it
			 * makes no sense to replace it with an exact replica of
			 * itself.  But emptiness still needs to be checked.
			 */
			err = ovl_check_empty_dir(dentry, &list);
			ovl_cache_free(&list);
			if (err)
				goto out;
		}
	}

	err = ovl_lock_rename_workdir(workdir, upperdir);
	if (err)
		goto out_dput;

	upper = lookup_one_len(dentry->d_name.name, upperdir,
			       dentry->d_name.len);
	err = PTR_ERR(upper);
	if (IS_ERR(upper))
		goto out_unlock;

	err = -ESTALE;
	if ((opaquedir && upper != opaquedir) ||
	    (!opaquedir && ovl_dentry_upper(dentry) &&
	     upper != ovl_dentry_upper(dentry))) {
		goto out_dput_upper;
	}

	whiteout = ovl_whiteout(workdir, dentry);
	err = PTR_ERR(whiteout);
	if (IS_ERR(whiteout))
		goto out_dput_upper;

	if (d_is_dir(upper))
		flags = RENAME_EXCHANGE;

	err = ovl_do_rename(wdir, whiteout, udir, upper, flags);
	if (err)
		goto kill_whiteout;
	if (flags)
		ovl_cleanup(wdir, upper);

	ovl_dentry_version_inc(dentry->d_parent);
out_d_drop:
	d_drop(dentry);
	dput(whiteout);
out_dput_upper:
	dput(upper);
out_unlock:
	unlock_rename(workdir, upperdir);
out_dput:
	dput(opaquedir);
out:
	return err;

kill_whiteout:
	ovl_cleanup(wdir, whiteout);
	goto out_d_drop;
}

static int ovl_remove_upper(struct dentry *dentry, bool is_dir)
{
	struct dentry *upperdir = ovl_dentry_upper(dentry->d_parent);
	struct inode *dir = upperdir->d_inode;
	struct dentry *upper;
	int err;

	inode_lock_nested(dir, I_MUTEX_PARENT);
	upper = lookup_one_len(dentry->d_name.name, upperdir,
			       dentry->d_name.len);
	err = PTR_ERR(upper);
	if (IS_ERR(upper))
		goto out_unlock;

	err = -ESTALE;
	if (upper == ovl_dentry_upper(dentry)) {
		if (is_dir)
			err = vfs_rmdir(dir, upper);
		else
			err = vfs_unlink(dir, upper, NULL);
		ovl_dentry_version_inc(dentry->d_parent);
	}
	dput(upper);

	/*
	 * Keeping this dentry hashed would mean having to release
	 * upperpath/lowerpath, which could only be done if we are the
	 * sole user of this dentry.  Too tricky...  Just unhash for
	 * now.
	 */
	if (!err)
		d_drop(dentry);
out_unlock:
	inode_unlock(dir);

	return err;
}

static inline int ovl_check_sticky(struct dentry *dentry)
{
	struct inode *dir = ovl_dentry_real(dentry->d_parent)->d_inode;
	struct inode *inode = ovl_dentry_real(dentry)->d_inode;

	if (check_sticky(dir, inode))
		return -EPERM;

	return 0;
}

static int ovl_do_remove(struct dentry *dentry, bool is_dir)
{
	enum ovl_path_type type;
	int err;

	err = ovl_check_sticky(dentry);
	if (err)
		goto out;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	err = ovl_copy_up(dentry->d_parent);
	if (err)
		goto out_drop_write;

	type = ovl_path_type(dentry);
	if (OVL_TYPE_PURE_UPPER(type)) {
		err = ovl_remove_upper(dentry, is_dir);
	} else {
		const struct cred *old_cred = ovl_override_creds(dentry->d_sb);

		err = ovl_remove_and_whiteout(dentry, is_dir);

		revert_creds(old_cred);
	}
out_drop_write:
	ovl_drop_write(dentry);
out:
	return err;
}

static int ovl_unlink(struct inode *dir, struct dentry *dentry)
{
	return ovl_do_remove(dentry, false);
}

static int ovl_rmdir(struct inode *dir, struct dentry *dentry)
{
	return ovl_do_remove(dentry, true);
}

static int ovl_rename2(struct inode *olddir, struct dentry *old,
		       struct inode *newdir, struct dentry *new,
		       unsigned int flags)
{
	int err;
	enum ovl_path_type old_type;
	enum ovl_path_type new_type;
	struct dentry *old_upperdir;
	struct dentry *new_upperdir;
	struct dentry *olddentry;
	struct dentry *newdentry;
	struct dentry *trap;
	bool old_opaque;
	bool new_opaque;
	bool cleanup_whiteout = false;
	bool overwrite = !(flags & RENAME_EXCHANGE);
	bool is_dir = d_is_dir(old);
	bool new_is_dir = false;
	struct dentry *opaquedir = NULL;
	const struct cred *old_cred = NULL;

	err = -EINVAL;
	if (flags & ~(RENAME_EXCHANGE | RENAME_NOREPLACE))
		goto out;

	flags &= ~RENAME_NOREPLACE;

	err = ovl_check_sticky(old);
	if (err)
		goto out;

	/* Don't copy up directory trees */
	old_type = ovl_path_type(old);
	err = -EXDEV;
	if (OVL_TYPE_MERGE_OR_LOWER(old_type) && is_dir)
		goto out;

	if (new->d_inode) {
		err = ovl_check_sticky(new);
		if (err)
			goto out;

		if (d_is_dir(new))
			new_is_dir = true;

		new_type = ovl_path_type(new);
		err = -EXDEV;
		if (!overwrite && OVL_TYPE_MERGE_OR_LOWER(new_type) && new_is_dir)
			goto out;

		err = 0;
		if (!OVL_TYPE_UPPER(new_type) && !OVL_TYPE_UPPER(old_type)) {
			if (ovl_dentry_lower(old)->d_inode ==
			    ovl_dentry_lower(new)->d_inode)
				goto out;
		}
		if (OVL_TYPE_UPPER(new_type) && OVL_TYPE_UPPER(old_type)) {
			if (ovl_dentry_upper(old)->d_inode ==
			    ovl_dentry_upper(new)->d_inode)
				goto out;
		}
	} else {
		if (ovl_dentry_is_opaque(new))
			new_type = __OVL_PATH_UPPER;
		else
			new_type = __OVL_PATH_UPPER | __OVL_PATH_PURE;
	}

	err = ovl_want_write(old);
	if (err)
		goto out;

	err = ovl_copy_up(old);
	if (err)
		goto out_drop_write;

	err = ovl_copy_up(new->d_parent);
	if (err)
		goto out_drop_write;
	if (!overwrite) {
		err = ovl_copy_up(new);
		if (err)
			goto out_drop_write;
	}

	old_opaque = !OVL_TYPE_PURE_UPPER(old_type);
	new_opaque = !OVL_TYPE_PURE_UPPER(new_type);

	if (old_opaque || new_opaque)
		old_cred = ovl_override_creds(old->d_sb);

	if (overwrite && OVL_TYPE_MERGE_OR_LOWER(new_type) && new_is_dir) {
		opaquedir = ovl_check_empty_and_clear(new);
		err = PTR_ERR(opaquedir);
		if (IS_ERR(opaquedir)) {
			opaquedir = NULL;
			goto out_revert_creds;
		}
	}

	if (overwrite) {
		if (old_opaque) {
			if (new->d_inode || !new_opaque) {
				/* Whiteout source */
				flags |= RENAME_WHITEOUT;
			} else {
				/* Switch whiteouts */
				flags |= RENAME_EXCHANGE;
			}
		} else if (is_dir && !new->d_inode && new_opaque) {
			flags |= RENAME_EXCHANGE;
			cleanup_whiteout = true;
		}
	}

	old_upperdir = ovl_dentry_upper(old->d_parent);
	new_upperdir = ovl_dentry_upper(new->d_parent);

	trap = lock_rename(new_upperdir, old_upperdir);


	olddentry = lookup_one_len(old->d_name.name, old_upperdir,
				   old->d_name.len);
	err = PTR_ERR(olddentry);
	if (IS_ERR(olddentry))
		goto out_unlock;

	err = -ESTALE;
	if (olddentry != ovl_dentry_upper(old))
		goto out_dput_old;

	newdentry = lookup_one_len(new->d_name.name, new_upperdir,
				   new->d_name.len);
	err = PTR_ERR(newdentry);
	if (IS_ERR(newdentry))
		goto out_dput_old;

	err = -ESTALE;
	if (ovl_dentry_upper(new)) {
		if (opaquedir) {
			if (newdentry != opaquedir)
				goto out_dput;
		} else {
			if (newdentry != ovl_dentry_upper(new))
				goto out_dput;
		}
	} else {
		if (!d_is_negative(newdentry) &&
		    (!new_opaque || !ovl_is_whiteout(newdentry)))
			goto out_dput;
	}

	if (olddentry == trap)
		goto out_dput;
	if (newdentry == trap)
		goto out_dput;

	if (is_dir && !old_opaque && new_opaque) {
		err = ovl_set_opaque(olddentry);
		if (err)
			goto out_dput;
	}
	if (!overwrite && new_is_dir && old_opaque && !new_opaque) {
		err = ovl_set_opaque(newdentry);
		if (err)
			goto out_dput;
	}

	if (old_opaque || new_opaque) {
		err = ovl_do_rename(old_upperdir->d_inode, olddentry,
				    new_upperdir->d_inode, newdentry,
				    flags);
	} else {
		/* No debug for the plain case */
		BUG_ON(flags & ~RENAME_EXCHANGE);
		err = vfs_rename(old_upperdir->d_inode, olddentry,
				 new_upperdir->d_inode, newdentry,
				 NULL, flags);
	}

	if (err) {
		if (is_dir && !old_opaque && new_opaque)
			ovl_remove_opaque(olddentry);
		if (!overwrite && new_is_dir && old_opaque && !new_opaque)
			ovl_remove_opaque(newdentry);
		goto out_dput;
	}

	if (is_dir && old_opaque && !new_opaque)
		ovl_remove_opaque(olddentry);
	if (!overwrite && new_is_dir && !old_opaque && new_opaque)
		ovl_remove_opaque(newdentry);

	/*
	 * Old dentry now lives in different location. Dentries in
	 * lowerstack are stale. We cannot drop them here because
	 * access to them is lockless. This could be only pure upper
	 * or opaque directory - numlower is zero. Or upper non-dir
	 * entry - its pureness is tracked by flag opaque.
	 */
	if (old_opaque != new_opaque) {
		ovl_dentry_set_opaque(old, new_opaque);
		if (!overwrite)
			ovl_dentry_set_opaque(new, old_opaque);
	}

	if (cleanup_whiteout)
		ovl_cleanup(old_upperdir->d_inode, newdentry);

	ovl_dentry_version_inc(old->d_parent);
	ovl_dentry_version_inc(new->d_parent);

out_dput:
	dput(newdentry);
out_dput_old:
	dput(olddentry);
out_unlock:
	unlock_rename(new_upperdir, old_upperdir);
out_revert_creds:
	if (old_opaque || new_opaque)
		revert_creds(old_cred);
out_drop_write:
	ovl_drop_write(old);
out:
	dput(opaquedir);
	return err;
}

const struct inode_operations ovl_dir_inode_operations = {
	.lookup		= ovl_lookup,
	.mkdir		= ovl_mkdir,
	.symlink	= ovl_symlink,
	.unlink		= ovl_unlink,
	.rmdir		= ovl_rmdir,
	.rename2	= ovl_rename2,
	.link		= ovl_link,
	.setattr	= ovl_setattr,
	.create		= ovl_create,
	.mknod		= ovl_mknod,
	.permission	= ovl_permission,
	.getattr	= ovl_dir_getattr,
	.setxattr	= ovl_setxattr,
	.getxattr	= ovl_getxattr,
	.listxattr	= ovl_listxattr,
	.removexattr	= ovl_removexattr,
};
