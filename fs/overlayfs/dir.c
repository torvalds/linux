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

static const char *ovl_whiteout_symlink = "(overlay-whiteout)";

static int ovl_whiteout(struct dentry *upperdir, struct dentry *dentry)
{
	int err;
	struct dentry *newdentry;
	const struct cred *old_cred;
	struct cred *override_cred;

	/* FIXME: recheck lower dentry to see if whiteout is really needed */

	err = -ENOMEM;
	override_cred = prepare_creds();
	if (!override_cred)
		goto out;

	/*
	 * CAP_SYS_ADMIN for setxattr
	 * CAP_DAC_OVERRIDE for symlink creation
	 * CAP_FOWNER for unlink in sticky directory
	 */
	cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
	cap_raise(override_cred->cap_effective, CAP_DAC_OVERRIDE);
	cap_raise(override_cred->cap_effective, CAP_FOWNER);
	override_cred->fsuid = GLOBAL_ROOT_UID;
	override_cred->fsgid = GLOBAL_ROOT_GID;
	old_cred = override_creds(override_cred);

	newdentry = lookup_one_len(dentry->d_name.name, upperdir,
				   dentry->d_name.len);
	err = PTR_ERR(newdentry);
	if (IS_ERR(newdentry))
		goto out_put_cred;

	/* Just been removed within the same locked region */
	WARN_ON(newdentry->d_inode);

	err = vfs_symlink(upperdir->d_inode, newdentry, ovl_whiteout_symlink);
	if (err)
		goto out_dput;

	ovl_dentry_version_inc(dentry->d_parent);

	err = vfs_setxattr(newdentry, ovl_whiteout_xattr, "y", 1, 0);
	if (err)
		vfs_unlink(upperdir->d_inode, newdentry);

out_dput:
	dput(newdentry);
out_put_cred:
	revert_creds(old_cred);
	put_cred(override_cred);
out:
	if (err) {
		/*
		 * There's no way to recover from failure to whiteout.
		 * What should we do?  Log a big fat error and... ?
		 */
		pr_err("overlayfs: ERROR - failed to whiteout '%s'\n",
		       dentry->d_name.name);
	}

	return err;
}

static struct dentry *ovl_lookup_create(struct dentry *upperdir,
					struct dentry *template)
{
	int err;
	struct dentry *newdentry;
	struct qstr *name = &template->d_name;

	newdentry = lookup_one_len(name->name, upperdir, name->len);
	if (IS_ERR(newdentry))
		return newdentry;

	if (newdentry->d_inode) {
		const struct cred *old_cred;
		struct cred *override_cred;

		/* No need to check whiteout if lower parent is non-existent */
		err = -EEXIST;
		if (!ovl_dentry_lower(template->d_parent))
			goto out_dput;

		if (!S_ISLNK(newdentry->d_inode->i_mode))
			goto out_dput;

		err = -ENOMEM;
		override_cred = prepare_creds();
		if (!override_cred)
			goto out_dput;

		/*
		 * CAP_SYS_ADMIN for getxattr
		 * CAP_FOWNER for unlink in sticky directory
		 */
		cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
		cap_raise(override_cred->cap_effective, CAP_FOWNER);
		old_cred = override_creds(override_cred);

		err = -EEXIST;
		if (ovl_is_whiteout(newdentry))
			err = vfs_unlink(upperdir->d_inode, newdentry);

		revert_creds(old_cred);
		put_cred(override_cred);
		if (err)
			goto out_dput;

		dput(newdentry);
		newdentry = lookup_one_len(name->name, upperdir, name->len);
		if (IS_ERR(newdentry)) {
			ovl_whiteout(upperdir, template);
			return newdentry;
		}

		/*
		 * Whiteout just been successfully removed, parent
		 * i_mutex is still held, there's no way the lookup
		 * could return positive.
		 */
		WARN_ON(newdentry->d_inode);
	}

	return newdentry;

out_dput:
	dput(newdentry);
	return ERR_PTR(err);
}

struct dentry *ovl_upper_create(struct dentry *upperdir, struct dentry *dentry,
				struct kstat *stat, const char *link)
{
	int err;
	struct dentry *newdentry;
	struct inode *dir = upperdir->d_inode;

	newdentry = ovl_lookup_create(upperdir, dentry);
	if (IS_ERR(newdentry))
		goto out;

	switch (stat->mode & S_IFMT) {
	case S_IFREG:
		err = vfs_create(dir, newdentry, stat->mode, NULL);
		break;

	case S_IFDIR:
		err = vfs_mkdir(dir, newdentry, stat->mode);
		break;

	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		err = vfs_mknod(dir, newdentry, stat->mode, stat->rdev);
		break;

	case S_IFLNK:
		err = vfs_symlink(dir, newdentry, link);
		break;

	default:
		err = -EPERM;
	}
	if (err) {
		if (ovl_dentry_is_opaque(dentry))
			ovl_whiteout(upperdir, dentry);
		dput(newdentry);
		newdentry = ERR_PTR(err);
	} else if (WARN_ON(!newdentry->d_inode)) {
		/*
		 * Not quite sure if non-instantiated dentry is legal or not.
		 * VFS doesn't seem to care so check and warn here.
		 */
		dput(newdentry);
		newdentry = ERR_PTR(-ENOENT);
	}

out:
	return newdentry;

}

static int ovl_set_opaque(struct dentry *upperdentry)
{
	int err;
	const struct cred *old_cred;
	struct cred *override_cred;

	override_cred = prepare_creds();
	if (!override_cred)
		return -ENOMEM;

	/* CAP_SYS_ADMIN for setxattr of "trusted" namespace */
	cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
	old_cred = override_creds(override_cred);
	err = vfs_setxattr(upperdentry, ovl_opaque_xattr, "y", 1, 0);
	revert_creds(old_cred);
	put_cred(override_cred);

	return err;
}

static int ovl_remove_opaque(struct dentry *upperdentry)
{
	int err;
	const struct cred *old_cred;
	struct cred *override_cred;

	override_cred = prepare_creds();
	if (!override_cred)
		return -ENOMEM;

	/* CAP_SYS_ADMIN for removexattr of "trusted" namespace */
	cap_raise(override_cred->cap_effective, CAP_SYS_ADMIN);
	old_cred = override_creds(override_cred);
	err = vfs_removexattr(upperdentry, ovl_opaque_xattr);
	revert_creds(old_cred);
	put_cred(override_cred);

	return err;
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
	if (type == OVL_PATH_MERGE)
		stat->nlink = 1;

	return 0;
}

static int ovl_create_object(struct dentry *dentry, int mode, dev_t rdev,
			     const char *link)
{
	int err;
	struct dentry *newdentry;
	struct dentry *upperdir;
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

	upperdir = ovl_dentry_upper(dentry->d_parent);
	mutex_lock_nested(&upperdir->d_inode->i_mutex, I_MUTEX_PARENT);

	newdentry = ovl_upper_create(upperdir, dentry, &stat, link);
	err = PTR_ERR(newdentry);
	if (IS_ERR(newdentry))
		goto out_unlock;

	ovl_dentry_version_inc(dentry->d_parent);
	if (ovl_dentry_is_opaque(dentry) && S_ISDIR(mode)) {
		err = ovl_set_opaque(newdentry);
		if (err) {
			vfs_rmdir(upperdir->d_inode, newdentry);
			ovl_whiteout(upperdir, dentry);
			goto out_dput;
		}
	}
	ovl_dentry_update(dentry, newdentry);
	ovl_copyattr(newdentry->d_inode, inode);
	d_instantiate(dentry, inode);
	inode = NULL;
	newdentry = NULL;
	err = 0;

out_dput:
	dput(newdentry);
out_unlock:
	mutex_unlock(&upperdir->d_inode->i_mutex);
out_iput:
	iput(inode);
out:
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
	return ovl_create_object(dentry, mode, rdev, NULL);
}

static int ovl_symlink(struct inode *dir, struct dentry *dentry,
			 const char *link)
{
	return ovl_create_object(dentry, S_IFLNK, 0, link);
}

static int ovl_do_remove(struct dentry *dentry, bool is_dir)
{
	int err;
	enum ovl_path_type type;
	struct path realpath;
	struct dentry *upperdir;

	err = ovl_copy_up(dentry->d_parent);
	if (err)
		return err;

	upperdir = ovl_dentry_upper(dentry->d_parent);
	mutex_lock_nested(&upperdir->d_inode->i_mutex, I_MUTEX_PARENT);
	type = ovl_path_real(dentry, &realpath);
	if (type != OVL_PATH_LOWER) {
		err = -ESTALE;
		if (realpath.dentry->d_parent != upperdir)
			goto out_d_drop;

		/* FIXME: create whiteout up front and rename to target */

		if (is_dir)
			err = vfs_rmdir(upperdir->d_inode, realpath.dentry);
		else
			err = vfs_unlink(upperdir->d_inode, realpath.dentry);
		if (err)
			goto out_d_drop;

		ovl_dentry_version_inc(dentry->d_parent);
	}

	if (type != OVL_PATH_UPPER || ovl_dentry_is_opaque(dentry))
		err = ovl_whiteout(upperdir, dentry);

	/*
	 * Keeping this dentry hashed would mean having to release
	 * upperpath/lowerpath, which could only be done if we are the
	 * sole user of this dentry.  Too tricky...  Just unhash for
	 * now.
	 */
out_d_drop:
	d_drop(dentry);
	mutex_unlock(&upperdir->d_inode->i_mutex);

	return err;
}

static int ovl_unlink(struct inode *dir, struct dentry *dentry)
{
	return ovl_do_remove(dentry, false);
}


static int ovl_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err;
	enum ovl_path_type type;

	type = ovl_path_type(dentry);
	if (type != OVL_PATH_UPPER) {
		err = ovl_check_empty_and_clear(dentry, type);
		if (err)
			return err;
	}

	return ovl_do_remove(dentry, true);
}

static int ovl_link(struct dentry *old, struct inode *newdir,
		    struct dentry *new)
{
	int err;
	struct dentry *olddentry;
	struct dentry *newdentry;
	struct dentry *upperdir;
	struct inode *newinode;

	err = ovl_copy_up(old);
	if (err)
		goto out;

	err = ovl_copy_up(new->d_parent);
	if (err)
		goto out;

	upperdir = ovl_dentry_upper(new->d_parent);
	mutex_lock_nested(&upperdir->d_inode->i_mutex, I_MUTEX_PARENT);
	newdentry = ovl_lookup_create(upperdir, new);
	err = PTR_ERR(newdentry);
	if (IS_ERR(newdentry))
		goto out_unlock;

	olddentry = ovl_dentry_upper(old);
	err = vfs_link(olddentry, upperdir->d_inode, newdentry);
	if (!err) {
		if (WARN_ON(!newdentry->d_inode)) {
			dput(newdentry);
			err = -ENOENT;
			goto out_unlock;
		}
		newinode = ovl_new_inode(old->d_sb, newdentry->d_inode->i_mode,
				new->d_fsdata);
		if (!newinode) {
			err = -ENOMEM;
			goto link_fail;
		}
		ovl_copyattr(upperdir->d_inode, newinode);

		ovl_dentry_version_inc(new->d_parent);
		ovl_dentry_update(new, newdentry);

		d_instantiate(new, newinode);
	} else {
link_fail:
		if (ovl_dentry_is_opaque(new))
			ovl_whiteout(upperdir, new);
		dput(newdentry);
	}
out_unlock:
	mutex_unlock(&upperdir->d_inode->i_mutex);
out:
	return err;
}

static int ovl_rename(struct inode *olddir, struct dentry *old,
			struct inode *newdir, struct dentry *new)
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
	bool new_create = false;
	bool is_dir = S_ISDIR(old->d_inode->i_mode);

	/* Don't copy up directory trees */
	old_type = ovl_path_type(old);
	if (old_type != OVL_PATH_UPPER && is_dir)
		return -EXDEV;

	if (new->d_inode) {
		new_type = ovl_path_type(new);

		if (new_type == OVL_PATH_LOWER && old_type == OVL_PATH_LOWER) {
			if (ovl_dentry_lower(old)->d_inode ==
			    ovl_dentry_lower(new)->d_inode)
				return 0;
		}
		if (new_type != OVL_PATH_LOWER && old_type != OVL_PATH_LOWER) {
			if (ovl_dentry_upper(old)->d_inode ==
			    ovl_dentry_upper(new)->d_inode)
				return 0;
		}

		if (new_type != OVL_PATH_UPPER &&
		    S_ISDIR(new->d_inode->i_mode)) {
			err = ovl_check_empty_and_clear(new, new_type);
			if (err)
				return err;
		}
	} else {
		new_type = OVL_PATH_UPPER;
	}

	err = ovl_copy_up(old);
	if (err)
		return err;

	err = ovl_copy_up(new->d_parent);
	if (err)
		return err;

	old_upperdir = ovl_dentry_upper(old->d_parent);
	new_upperdir = ovl_dentry_upper(new->d_parent);

	trap = lock_rename(new_upperdir, old_upperdir);

	olddentry = ovl_dentry_upper(old);
	newdentry = ovl_dentry_upper(new);
	if (newdentry) {
		dget(newdentry);
	} else {
		new_create = true;
		newdentry = ovl_lookup_create(new_upperdir, new);
		err = PTR_ERR(newdentry);
		if (IS_ERR(newdentry))
			goto out_unlock;
	}

	err = -ESTALE;
	if (olddentry->d_parent != old_upperdir)
		goto out_dput;
	if (newdentry->d_parent != new_upperdir)
		goto out_dput;
	if (olddentry == trap)
		goto out_dput;
	if (newdentry == trap)
		goto out_dput;

	old_opaque = ovl_dentry_is_opaque(old);
	new_opaque = ovl_dentry_is_opaque(new) || new_type != OVL_PATH_UPPER;

	if (is_dir && !old_opaque && new_opaque) {
		err = ovl_set_opaque(olddentry);
		if (err)
			goto out_dput;
	}

	err = vfs_rename(old_upperdir->d_inode, olddentry,
			 new_upperdir->d_inode, newdentry);

	if (err) {
		if (new_create && ovl_dentry_is_opaque(new))
			ovl_whiteout(new_upperdir, new);
		if (is_dir && !old_opaque && new_opaque)
			ovl_remove_opaque(olddentry);
		goto out_dput;
	}

	if (old_type != OVL_PATH_UPPER || old_opaque)
		err = ovl_whiteout(old_upperdir, old);
	if (is_dir && old_opaque && !new_opaque)
		ovl_remove_opaque(olddentry);

	if (old_opaque != new_opaque)
		ovl_dentry_set_opaque(old, new_opaque);

	ovl_dentry_version_inc(old->d_parent);
	ovl_dentry_version_inc(new->d_parent);

out_dput:
	dput(newdentry);
out_unlock:
	unlock_rename(new_upperdir, old_upperdir);
	return err;
}

const struct inode_operations ovl_dir_inode_operations = {
	.lookup		= ovl_lookup,
	.mkdir		= ovl_mkdir,
	.symlink	= ovl_symlink,
	.unlink		= ovl_unlink,
	.rmdir		= ovl_rmdir,
	.rename		= ovl_rename,
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
