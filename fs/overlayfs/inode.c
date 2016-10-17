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
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include "overlayfs.h"

static int ovl_copy_up_truncate(struct dentry *dentry)
{
	int err;
	struct dentry *parent;
	struct kstat stat;
	struct path lowerpath;
	const struct cred *old_cred;

	parent = dget_parent(dentry);
	err = ovl_copy_up(parent);
	if (err)
		goto out_dput_parent;

	ovl_path_lower(dentry, &lowerpath);

	old_cred = ovl_override_creds(dentry->d_sb);
	err = vfs_getattr(&lowerpath, &stat);
	if (!err) {
		stat.size = 0;
		err = ovl_copy_up_one(parent, dentry, &lowerpath, &stat);
	}
	revert_creds(old_cred);

out_dput_parent:
	dput(parent);
	return err;
}

int ovl_setattr(struct dentry *dentry, struct iattr *attr)
{
	int err;
	struct dentry *upperdentry;
	const struct cred *old_cred;

	/*
	 * Check for permissions before trying to copy-up.  This is redundant
	 * since it will be rechecked later by ->setattr() on upper dentry.  But
	 * without this, copy-up can be triggered by just about anybody.
	 *
	 * We don't initialize inode->size, which just means that
	 * inode_newsize_ok() will always check against MAX_LFS_FILESIZE and not
	 * check for a swapfile (which this won't be anyway).
	 */
	err = setattr_prepare(dentry, attr);
	if (err)
		return err;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	if (attr->ia_valid & ATTR_SIZE) {
		struct inode *realinode = d_inode(ovl_dentry_real(dentry));

		err = -ETXTBSY;
		if (atomic_read(&realinode->i_writecount) < 0)
			goto out_drop_write;
	}

	err = ovl_copy_up(dentry);
	if (!err) {
		struct inode *winode = NULL;

		upperdentry = ovl_dentry_upper(dentry);

		if (attr->ia_valid & ATTR_SIZE) {
			winode = d_inode(upperdentry);
			err = get_write_access(winode);
			if (err)
				goto out_drop_write;
		}

		if (attr->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
			attr->ia_valid &= ~ATTR_MODE;

		inode_lock(upperdentry->d_inode);
		old_cred = ovl_override_creds(dentry->d_sb);
		err = notify_change(upperdentry, attr, NULL);
		revert_creds(old_cred);
		if (!err)
			ovl_copyattr(upperdentry->d_inode, dentry->d_inode);
		inode_unlock(upperdentry->d_inode);

		if (winode)
			put_write_access(winode);
	}
out_drop_write:
	ovl_drop_write(dentry);
out:
	return err;
}

static int ovl_getattr(struct vfsmount *mnt, struct dentry *dentry,
			 struct kstat *stat)
{
	struct path realpath;
	const struct cred *old_cred;
	int err;

	ovl_path_real(dentry, &realpath);
	old_cred = ovl_override_creds(dentry->d_sb);
	err = vfs_getattr(&realpath, stat);
	revert_creds(old_cred);
	return err;
}

int ovl_permission(struct inode *inode, int mask)
{
	bool is_upper;
	struct inode *realinode = ovl_inode_real(inode, &is_upper);
	const struct cred *old_cred;
	int err;

	/* Careful in RCU walk mode */
	if (!realinode) {
		WARN_ON(!(mask & MAY_NOT_BLOCK));
		return -ECHILD;
	}

	/*
	 * Check overlay inode with the creds of task and underlying inode
	 * with creds of mounter
	 */
	err = generic_permission(inode, mask);
	if (err)
		return err;

	old_cred = ovl_override_creds(inode->i_sb);
	if (!is_upper && !special_file(realinode->i_mode) && mask & MAY_WRITE) {
		mask &= ~(MAY_WRITE | MAY_APPEND);
		/* Make sure mounter can read file for copy up later */
		mask |= MAY_READ;
	}
	err = inode_permission(realinode, mask);
	revert_creds(old_cred);

	return err;
}

static const char *ovl_get_link(struct dentry *dentry,
				struct inode *inode,
				struct delayed_call *done)
{
	const struct cred *old_cred;
	const char *p;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	old_cred = ovl_override_creds(dentry->d_sb);
	p = vfs_get_link(ovl_dentry_real(dentry), done);
	revert_creds(old_cred);
	return p;
}

bool ovl_is_private_xattr(const char *name)
{
	return strncmp(name, OVL_XATTR_PREFIX,
		       sizeof(OVL_XATTR_PREFIX) - 1) == 0;
}

int ovl_xattr_set(struct dentry *dentry, const char *name, const void *value,
		  size_t size, int flags)
{
	int err;
	struct path realpath;
	enum ovl_path_type type = ovl_path_real(dentry, &realpath);
	const struct cred *old_cred;

	err = ovl_want_write(dentry);
	if (err)
		goto out;

	if (!value && !OVL_TYPE_UPPER(type)) {
		err = vfs_getxattr(realpath.dentry, name, NULL, 0);
		if (err < 0)
			goto out_drop_write;
	}

	err = ovl_copy_up(dentry);
	if (err)
		goto out_drop_write;

	if (!OVL_TYPE_UPPER(type))
		ovl_path_upper(dentry, &realpath);

	old_cred = ovl_override_creds(dentry->d_sb);
	if (value)
		err = vfs_setxattr(realpath.dentry, name, value, size, flags);
	else {
		WARN_ON(flags != XATTR_REPLACE);
		err = vfs_removexattr(realpath.dentry, name);
	}
	revert_creds(old_cred);

out_drop_write:
	ovl_drop_write(dentry);
out:
	return err;
}

int ovl_xattr_get(struct dentry *dentry, const char *name,
		  void *value, size_t size)
{
	struct dentry *realdentry = ovl_dentry_real(dentry);
	ssize_t res;
	const struct cred *old_cred;

	old_cred = ovl_override_creds(dentry->d_sb);
	res = vfs_getxattr(realdentry, name, value, size);
	revert_creds(old_cred);
	return res;
}

ssize_t ovl_listxattr(struct dentry *dentry, char *list, size_t size)
{
	struct dentry *realdentry = ovl_dentry_real(dentry);
	ssize_t res;
	size_t len;
	char *s;
	const struct cred *old_cred;

	old_cred = ovl_override_creds(dentry->d_sb);
	res = vfs_listxattr(realdentry, list, size);
	revert_creds(old_cred);
	if (res <= 0 || size == 0)
		return res;

	/* filter out private xattrs */
	for (s = list, len = res; len;) {
		size_t slen = strnlen(s, len) + 1;

		/* underlying fs providing us with an broken xattr list? */
		if (WARN_ON(slen > len))
			return -EIO;

		len -= slen;
		if (ovl_is_private_xattr(s)) {
			res -= slen;
			memmove(s, s + slen, len);
		} else {
			s += slen;
		}
	}

	return res;
}

struct posix_acl *ovl_get_acl(struct inode *inode, int type)
{
	struct inode *realinode = ovl_inode_real(inode, NULL);
	const struct cred *old_cred;
	struct posix_acl *acl;

	if (!IS_ENABLED(CONFIG_FS_POSIX_ACL) || !IS_POSIXACL(realinode))
		return NULL;

	if (!realinode->i_op->get_acl)
		return NULL;

	old_cred = ovl_override_creds(inode->i_sb);
	acl = get_acl(realinode, type);
	revert_creds(old_cred);

	return acl;
}

static bool ovl_open_need_copy_up(int flags, enum ovl_path_type type,
				  struct dentry *realdentry)
{
	if (OVL_TYPE_UPPER(type))
		return false;

	if (special_file(realdentry->d_inode->i_mode))
		return false;

	if (!(OPEN_FMODE(flags) & FMODE_WRITE) && !(flags & O_TRUNC))
		return false;

	return true;
}

int ovl_open_maybe_copy_up(struct dentry *dentry, unsigned int file_flags)
{
	int err = 0;
	struct path realpath;
	enum ovl_path_type type;

	type = ovl_path_real(dentry, &realpath);
	if (ovl_open_need_copy_up(file_flags, type, realpath.dentry)) {
		err = ovl_want_write(dentry);
		if (!err) {
			if (file_flags & O_TRUNC)
				err = ovl_copy_up_truncate(dentry);
			else
				err = ovl_copy_up(dentry);
			ovl_drop_write(dentry);
		}
	}

	return err;
}

int ovl_update_time(struct inode *inode, struct timespec *ts, int flags)
{
	struct dentry *alias;
	struct path upperpath;

	if (!(flags & S_ATIME))
		return 0;

	alias = d_find_any_alias(inode);
	if (!alias)
		return 0;

	ovl_path_upper(alias, &upperpath);
	if (upperpath.dentry) {
		touch_atime(&upperpath);
		inode->i_atime = d_inode(upperpath.dentry)->i_atime;
	}

	dput(alias);

	return 0;
}

static const struct inode_operations ovl_file_inode_operations = {
	.setattr	= ovl_setattr,
	.permission	= ovl_permission,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.get_acl	= ovl_get_acl,
	.update_time	= ovl_update_time,
};

static const struct inode_operations ovl_symlink_inode_operations = {
	.setattr	= ovl_setattr,
	.get_link	= ovl_get_link,
	.readlink	= generic_readlink,
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.update_time	= ovl_update_time,
};

static void ovl_fill_inode(struct inode *inode, umode_t mode)
{
	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_flags |= S_NOCMTIME;
#ifdef CONFIG_FS_POSIX_ACL
	inode->i_acl = inode->i_default_acl = ACL_DONT_CACHE;
#endif

	mode &= S_IFMT;
	switch (mode) {
	case S_IFDIR:
		inode->i_op = &ovl_dir_inode_operations;
		inode->i_fop = &ovl_dir_operations;
		break;

	case S_IFLNK:
		inode->i_op = &ovl_symlink_inode_operations;
		break;

	default:
		WARN(1, "illegal file type: %i\n", mode);
		/* Fall through */

	case S_IFREG:
	case S_IFSOCK:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFIFO:
		inode->i_op = &ovl_file_inode_operations;
		break;
	}
}

struct inode *ovl_new_inode(struct super_block *sb, umode_t mode)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode)
		ovl_fill_inode(inode, mode);

	return inode;
}

static int ovl_inode_test(struct inode *inode, void *data)
{
	return ovl_inode_real(inode, NULL) == data;
}

static int ovl_inode_set(struct inode *inode, void *data)
{
	inode->i_private = (void *) (((unsigned long) data) | OVL_ISUPPER_MASK);
	return 0;
}

struct inode *ovl_get_inode(struct super_block *sb, struct inode *realinode)

{
	struct inode *inode;

	inode = iget5_locked(sb, (unsigned long) realinode,
			     ovl_inode_test, ovl_inode_set, realinode);
	if (inode && inode->i_state & I_NEW) {
		ovl_fill_inode(inode, realinode->i_mode);
		set_nlink(inode, realinode->i_nlink);
		unlock_new_inode(inode);
	}

	return inode;
}
