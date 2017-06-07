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
#include <linux/cred.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include "overlayfs.h"

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

	err = ovl_copy_up(dentry);
	if (!err) {
		upperdentry = ovl_dentry_upper(dentry);

		if (attr->ia_valid & (ATTR_KILL_SUID|ATTR_KILL_SGID))
			attr->ia_valid &= ~ATTR_MODE;

		inode_lock(upperdentry->d_inode);
		old_cred = ovl_override_creds(dentry->d_sb);
		err = notify_change(upperdentry, attr, NULL);
		revert_creds(old_cred);
		if (!err)
			ovl_copyattr(upperdentry->d_inode, dentry->d_inode);
		inode_unlock(upperdentry->d_inode);
	}
	ovl_drop_write(dentry);
out:
	return err;
}

int ovl_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	enum ovl_path_type type;
	struct path realpath;
	const struct cred *old_cred;
	bool is_dir = S_ISDIR(dentry->d_inode->i_mode);
	int err;

	type = ovl_path_real(dentry, &realpath);
	old_cred = ovl_override_creds(dentry->d_sb);
	err = vfs_getattr(&realpath, stat, request_mask, flags);
	if (err)
		goto out;

	/*
	 * When all layers are on the same fs, all real inode number are
	 * unique, so we use the overlay st_dev, which is friendly to du -x.
	 *
	 * We also use st_ino of the copy up origin, if we know it.
	 * This guaranties constant st_dev/st_ino across copy up.
	 *
	 * If filesystem supports NFS export ops, this also guaranties
	 * persistent st_ino across mount cycle.
	 */
	if (ovl_same_sb(dentry->d_sb)) {
		if (OVL_TYPE_ORIGIN(type)) {
			struct kstat lowerstat;
			u32 lowermask = STATX_INO | (!is_dir ? STATX_NLINK : 0);

			ovl_path_lower(dentry, &realpath);
			err = vfs_getattr(&realpath, &lowerstat,
					  lowermask, flags);
			if (err)
				goto out;

			WARN_ON_ONCE(stat->dev != lowerstat.dev);
			/*
			 * Lower hardlinks are broken on copy up to different
			 * upper files, so we cannot use the lower origin st_ino
			 * for those different files, even for the same fs case.
			 */
			if (is_dir || lowerstat.nlink == 1)
				stat->ino = lowerstat.ino;
		}
		stat->dev = dentry->d_sb->s_dev;
	} else if (is_dir) {
		/*
		 * If not all layers are on the same fs the pair {real st_ino;
		 * overlay st_dev} is not unique, so use the non persistent
		 * overlay st_ino.
		 *
		 * Always use the overlay st_dev for directories, so 'find
		 * -xdev' will scan the entire overlay mount and won't cross the
		 * overlay mount boundaries.
		 */
		stat->dev = dentry->d_sb->s_dev;
		stat->ino = dentry->d_inode->i_ino;
	}

	/*
	 * It's probably not worth it to count subdirs to get the
	 * correct link count.  nlink=1 seems to pacify 'find' and
	 * other utilities.
	 */
	if (is_dir && OVL_TYPE_MERGE(type))
		stat->nlink = 1;

out:
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

static bool ovl_can_list(const char *s)
{
	/* List all non-trusted xatts */
	if (strncmp(s, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) != 0)
		return true;

	/* Never list trusted.overlay, list other trusted for superuser only */
	return !ovl_is_private_xattr(s) && capable(CAP_SYS_ADMIN);
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
		if (!ovl_can_list(s)) {
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
			err = ovl_copy_up_flags(dentry, file_flags);
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
	.getattr	= ovl_getattr,
	.listxattr	= ovl_listxattr,
	.update_time	= ovl_update_time,
};

/*
 * It is possible to stack overlayfs instance on top of another
 * overlayfs instance as lower layer. We need to annonate the
 * stackable i_mutex locks according to stack level of the super
 * block instance. An overlayfs instance can never be in stack
 * depth 0 (there is always a real fs below it).  An overlayfs
 * inode lock will use the lockdep annotaion ovl_i_mutex_key[depth].
 *
 * For example, here is a snip from /proc/lockdep_chains after
 * dir_iterate of nested overlayfs:
 *
 * [...] &ovl_i_mutex_dir_key[depth]   (stack_depth=2)
 * [...] &ovl_i_mutex_dir_key[depth]#2 (stack_depth=1)
 * [...] &type->i_mutex_dir_key        (stack_depth=0)
 */
#define OVL_MAX_NESTING FILESYSTEM_MAX_STACK_DEPTH

static inline void ovl_lockdep_annotate_inode_mutex_key(struct inode *inode)
{
#ifdef CONFIG_LOCKDEP
	static struct lock_class_key ovl_i_mutex_key[OVL_MAX_NESTING];
	static struct lock_class_key ovl_i_mutex_dir_key[OVL_MAX_NESTING];

	int depth = inode->i_sb->s_stack_depth - 1;

	if (WARN_ON_ONCE(depth < 0 || depth >= OVL_MAX_NESTING))
		depth = 0;

	if (S_ISDIR(inode->i_mode))
		lockdep_set_class(&inode->i_rwsem, &ovl_i_mutex_dir_key[depth]);
	else
		lockdep_set_class(&inode->i_rwsem, &ovl_i_mutex_key[depth]);
#endif
}

static void ovl_fill_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_flags |= S_NOCMTIME;
#ifdef CONFIG_FS_POSIX_ACL
	inode->i_acl = inode->i_default_acl = ACL_DONT_CACHE;
#endif

	ovl_lockdep_annotate_inode_mutex_key(inode);

	switch (mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &ovl_file_inode_operations;
		break;

	case S_IFDIR:
		inode->i_op = &ovl_dir_inode_operations;
		inode->i_fop = &ovl_dir_operations;
		break;

	case S_IFLNK:
		inode->i_op = &ovl_symlink_inode_operations;
		break;

	default:
		inode->i_op = &ovl_file_inode_operations;
		init_special_inode(inode, mode, rdev);
		break;
	}
}

struct inode *ovl_new_inode(struct super_block *sb, umode_t mode, dev_t rdev)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode)
		ovl_fill_inode(inode, mode, rdev);

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
		ovl_fill_inode(inode, realinode->i_mode, realinode->i_rdev);
		set_nlink(inode, realinode->i_nlink);
		unlock_new_inode(inode);
	}

	return inode;
}
