/*
 * fs/sysfs/inode.c - basic sysfs inode and dentry operations
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#undef DEBUG

#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include "sysfs.h"

static const struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
};

static struct backing_dev_info sysfs_backing_dev_info = {
	.name		= "sysfs",
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

static const struct inode_operations sysfs_inode_operations = {
	.permission	= sysfs_permission,
	.setattr	= sysfs_setattr,
	.getattr	= sysfs_getattr,
	.setxattr	= sysfs_setxattr,
};

int __init sysfs_inode_init(void)
{
	return bdi_init(&sysfs_backing_dev_info);
}

static struct sysfs_inode_attrs *sysfs_init_inode_attrs(struct sysfs_dirent *sd)
{
	struct sysfs_inode_attrs *attrs;
	struct iattr *iattrs;

	attrs = kzalloc(sizeof(struct sysfs_inode_attrs), GFP_KERNEL);
	if (!attrs)
		return NULL;
	iattrs = &attrs->ia_iattr;

	/* assign default attributes */
	iattrs->ia_mode = sd->s_mode;
	iattrs->ia_uid = GLOBAL_ROOT_UID;
	iattrs->ia_gid = GLOBAL_ROOT_GID;
	iattrs->ia_atime = iattrs->ia_mtime = iattrs->ia_ctime = CURRENT_TIME;

	return attrs;
}

static int __kernfs_setattr(struct sysfs_dirent *sd, const struct iattr *iattr)
{
	struct sysfs_inode_attrs *sd_attrs;
	struct iattr *iattrs;
	unsigned int ia_valid = iattr->ia_valid;

	sd_attrs = sd->s_iattr;

	if (!sd_attrs) {
		/* setting attributes for the first time, allocate now */
		sd_attrs = sysfs_init_inode_attrs(sd);
		if (!sd_attrs)
			return -ENOMEM;
		sd->s_iattr = sd_attrs;
	}
	/* attributes were changed at least once in past */
	iattrs = &sd_attrs->ia_iattr;

	if (ia_valid & ATTR_UID)
		iattrs->ia_uid = iattr->ia_uid;
	if (ia_valid & ATTR_GID)
		iattrs->ia_gid = iattr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		iattrs->ia_atime = iattr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		iattrs->ia_mtime = iattr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		iattrs->ia_ctime = iattr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		umode_t mode = iattr->ia_mode;
		iattrs->ia_mode = sd->s_mode = mode;
	}
	return 0;
}

/**
 * kernfs_setattr - set iattr on a node
 * @sd: target node
 * @iattr: iattr to set
 *
 * Returns 0 on success, -errno on failure.
 */
int kernfs_setattr(struct sysfs_dirent *sd, const struct iattr *iattr)
{
	int ret;

	mutex_lock(&sysfs_mutex);
	ret = __kernfs_setattr(sd, iattr);
	mutex_unlock(&sysfs_mutex);
	return ret;
}

int sysfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	struct sysfs_dirent *sd = dentry->d_fsdata;
	int error;

	if (!sd)
		return -EINVAL;

	mutex_lock(&sysfs_mutex);
	error = inode_change_ok(inode, iattr);
	if (error)
		goto out;

	error = __kernfs_setattr(sd, iattr);
	if (error)
		goto out;

	/* this ignores size changes */
	setattr_copy(inode, iattr);

out:
	mutex_unlock(&sysfs_mutex);
	return error;
}

static int sysfs_sd_setsecdata(struct sysfs_dirent *sd, void **secdata,
			       u32 *secdata_len)
{
	struct sysfs_inode_attrs *iattrs;
	void *old_secdata;
	size_t old_secdata_len;

	if (!sd->s_iattr) {
		sd->s_iattr = sysfs_init_inode_attrs(sd);
		if (!sd->s_iattr)
			return -ENOMEM;
	}

	iattrs = sd->s_iattr;
	old_secdata = iattrs->ia_secdata;
	old_secdata_len = iattrs->ia_secdata_len;

	iattrs->ia_secdata = *secdata;
	iattrs->ia_secdata_len = *secdata_len;

	*secdata = old_secdata;
	*secdata_len = old_secdata_len;
	return 0;
}

int sysfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	struct sysfs_dirent *sd = dentry->d_fsdata;
	void *secdata;
	int error;
	u32 secdata_len = 0;

	if (!sd)
		return -EINVAL;

	if (!strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN)) {
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		error = security_inode_setsecurity(dentry->d_inode, suffix,
						value, size, flags);
		if (error)
			goto out;
		error = security_inode_getsecctx(dentry->d_inode,
						&secdata, &secdata_len);
		if (error)
			goto out;

		mutex_lock(&sysfs_mutex);
		error = sysfs_sd_setsecdata(sd, &secdata, &secdata_len);
		mutex_unlock(&sysfs_mutex);

		if (secdata)
			security_release_secctx(secdata, secdata_len);
	} else
		return -EINVAL;
out:
	return error;
}

static inline void set_default_inode_attr(struct inode *inode, umode_t mode)
{
	inode->i_mode = mode;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}

static inline void set_inode_attr(struct inode *inode, struct iattr *iattr)
{
	inode->i_uid = iattr->ia_uid;
	inode->i_gid = iattr->ia_gid;
	inode->i_atime = iattr->ia_atime;
	inode->i_mtime = iattr->ia_mtime;
	inode->i_ctime = iattr->ia_ctime;
}

static void sysfs_refresh_inode(struct sysfs_dirent *sd, struct inode *inode)
{
	struct sysfs_inode_attrs *iattrs = sd->s_iattr;

	inode->i_mode = sd->s_mode;
	if (iattrs) {
		/* sysfs_dirent has non-default attributes
		 * get them from persistent copy in sysfs_dirent
		 */
		set_inode_attr(inode, &iattrs->ia_iattr);
		security_inode_notifysecctx(inode,
					    iattrs->ia_secdata,
					    iattrs->ia_secdata_len);
	}

	if (sysfs_type(sd) == SYSFS_DIR)
		set_nlink(inode, sd->s_dir.subdirs + 2);
}

int sysfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		  struct kstat *stat)
{
	struct sysfs_dirent *sd = dentry->d_fsdata;
	struct inode *inode = dentry->d_inode;

	mutex_lock(&sysfs_mutex);
	sysfs_refresh_inode(sd, inode);
	mutex_unlock(&sysfs_mutex);

	generic_fillattr(inode, stat);
	return 0;
}

static void sysfs_init_inode(struct sysfs_dirent *sd, struct inode *inode)
{
	struct bin_attribute *bin_attr;

	inode->i_private = sysfs_get(sd);
	inode->i_mapping->a_ops = &sysfs_aops;
	inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
	inode->i_op = &sysfs_inode_operations;

	set_default_inode_attr(inode, sd->s_mode);
	sysfs_refresh_inode(sd, inode);

	/* initialize inode according to type */
	switch (sysfs_type(sd)) {
	case SYSFS_DIR:
		inode->i_op = &sysfs_dir_inode_operations;
		inode->i_fop = &sysfs_dir_operations;
		break;
	case SYSFS_KOBJ_ATTR:
		inode->i_size = PAGE_SIZE;
		inode->i_fop = &sysfs_file_operations;
		break;
	case SYSFS_KOBJ_BIN_ATTR:
		bin_attr = sd->priv;
		inode->i_size = bin_attr->size;
		inode->i_fop = &sysfs_bin_operations;
		break;
	case SYSFS_KOBJ_LINK:
		inode->i_op = &sysfs_symlink_inode_operations;
		break;
	default:
		BUG();
	}

	unlock_new_inode(inode);
}

/**
 *	sysfs_get_inode - get inode for sysfs_dirent
 *	@sb: super block
 *	@sd: sysfs_dirent to allocate inode for
 *
 *	Get inode for @sd.  If such inode doesn't exist, a new inode
 *	is allocated and basics are initialized.  New inode is
 *	returned locked.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	Pointer to allocated inode on success, NULL on failure.
 */
struct inode *sysfs_get_inode(struct super_block *sb, struct sysfs_dirent *sd)
{
	struct inode *inode;

	inode = iget_locked(sb, sd->s_ino);
	if (inode && (inode->i_state & I_NEW))
		sysfs_init_inode(sd, inode);

	return inode;
}

/*
 * The sysfs_dirent serves as both an inode and a directory entry for sysfs.
 * To prevent the sysfs inode numbers from being freed prematurely we take a
 * reference to sysfs_dirent from the sysfs inode.  A
 * super_operations.evict_inode() implementation is needed to drop that
 * reference upon inode destruction.
 */
void sysfs_evict_inode(struct inode *inode)
{
	struct sysfs_dirent *sd  = inode->i_private;

	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	sysfs_put(sd);
}

int sysfs_permission(struct inode *inode, int mask)
{
	struct sysfs_dirent *sd;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	sd = inode->i_private;

	mutex_lock(&sysfs_mutex);
	sysfs_refresh_inode(sd, inode);
	mutex_unlock(&sysfs_mutex);

	return generic_permission(inode, mask);
}
