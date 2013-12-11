/*
 * fs/kernfs/inode.c - kernfs inode implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
 */

#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/security.h>

#include "kernfs-internal.h"

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
	.removexattr	= sysfs_removexattr,
	.getxattr	= sysfs_getxattr,
	.listxattr	= sysfs_listxattr,
};

void __init sysfs_inode_init(void)
{
	if (bdi_init(&sysfs_backing_dev_info))
		panic("failed to init sysfs_backing_dev_info");
}

static struct kernfs_iattrs *kernfs_iattrs(struct kernfs_node *kn)
{
	struct iattr *iattrs;

	if (kn->iattr)
		return kn->iattr;

	kn->iattr = kzalloc(sizeof(struct kernfs_iattrs), GFP_KERNEL);
	if (!kn->iattr)
		return NULL;
	iattrs = &kn->iattr->ia_iattr;

	/* assign default attributes */
	iattrs->ia_mode = kn->mode;
	iattrs->ia_uid = GLOBAL_ROOT_UID;
	iattrs->ia_gid = GLOBAL_ROOT_GID;
	iattrs->ia_atime = iattrs->ia_mtime = iattrs->ia_ctime = CURRENT_TIME;

	simple_xattrs_init(&kn->iattr->xattrs);

	return kn->iattr;
}

static int __kernfs_setattr(struct kernfs_node *kn, const struct iattr *iattr)
{
	struct kernfs_iattrs *attrs;
	struct iattr *iattrs;
	unsigned int ia_valid = iattr->ia_valid;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	iattrs = &attrs->ia_iattr;

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
		iattrs->ia_mode = kn->mode = mode;
	}
	return 0;
}

/**
 * kernfs_setattr - set iattr on a node
 * @kn: target node
 * @iattr: iattr to set
 *
 * Returns 0 on success, -errno on failure.
 */
int kernfs_setattr(struct kernfs_node *kn, const struct iattr *iattr)
{
	int ret;

	mutex_lock(&sysfs_mutex);
	ret = __kernfs_setattr(kn, iattr);
	mutex_unlock(&sysfs_mutex);
	return ret;
}

int sysfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	struct kernfs_node *kn = dentry->d_fsdata;
	int error;

	if (!kn)
		return -EINVAL;

	mutex_lock(&sysfs_mutex);
	error = inode_change_ok(inode, iattr);
	if (error)
		goto out;

	error = __kernfs_setattr(kn, iattr);
	if (error)
		goto out;

	/* this ignores size changes */
	setattr_copy(inode, iattr);

out:
	mutex_unlock(&sysfs_mutex);
	return error;
}

static int sysfs_sd_setsecdata(struct kernfs_node *kn, void **secdata,
			       u32 *secdata_len)
{
	struct kernfs_iattrs *attrs;
	void *old_secdata;
	size_t old_secdata_len;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	old_secdata = attrs->ia_secdata;
	old_secdata_len = attrs->ia_secdata_len;

	attrs->ia_secdata = *secdata;
	attrs->ia_secdata_len = *secdata_len;

	*secdata = old_secdata;
	*secdata_len = old_secdata_len;
	return 0;
}

int sysfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	struct kernfs_iattrs *attrs;
	void *secdata;
	int error;
	u32 secdata_len = 0;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	if (!strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN)) {
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		error = security_inode_setsecurity(dentry->d_inode, suffix,
						value, size, flags);
		if (error)
			return error;
		error = security_inode_getsecctx(dentry->d_inode,
						&secdata, &secdata_len);
		if (error)
			return error;

		mutex_lock(&sysfs_mutex);
		error = sysfs_sd_setsecdata(kn, &secdata, &secdata_len);
		mutex_unlock(&sysfs_mutex);

		if (secdata)
			security_release_secctx(secdata, secdata_len);
		return error;
	} else if (!strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN)) {
		return simple_xattr_set(&attrs->xattrs, name, value, size,
					flags);
	}

	return -EINVAL;
}

int sysfs_removexattr(struct dentry *dentry, const char *name)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	struct kernfs_iattrs *attrs;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	return simple_xattr_remove(&attrs->xattrs, name);
}

ssize_t sysfs_getxattr(struct dentry *dentry, const char *name, void *buf,
		       size_t size)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	struct kernfs_iattrs *attrs;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	return simple_xattr_get(&attrs->xattrs, name, buf, size);
}

ssize_t sysfs_listxattr(struct dentry *dentry, char *buf, size_t size)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	struct kernfs_iattrs *attrs;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	return simple_xattr_list(&attrs->xattrs, buf, size);
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

static void sysfs_refresh_inode(struct kernfs_node *kn, struct inode *inode)
{
	struct kernfs_iattrs *attrs = kn->iattr;

	inode->i_mode = kn->mode;
	if (attrs) {
		/*
		 * kernfs_node has non-default attributes get them from
		 * persistent copy in kernfs_node.
		 */
		set_inode_attr(inode, &attrs->ia_iattr);
		security_inode_notifysecctx(inode, attrs->ia_secdata,
					    attrs->ia_secdata_len);
	}

	if (sysfs_type(kn) == SYSFS_DIR)
		set_nlink(inode, kn->dir.subdirs + 2);
}

int sysfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		  struct kstat *stat)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	struct inode *inode = dentry->d_inode;

	mutex_lock(&sysfs_mutex);
	sysfs_refresh_inode(kn, inode);
	mutex_unlock(&sysfs_mutex);

	generic_fillattr(inode, stat);
	return 0;
}

static void sysfs_init_inode(struct kernfs_node *kn, struct inode *inode)
{
	kernfs_get(kn);
	inode->i_private = kn;
	inode->i_mapping->a_ops = &sysfs_aops;
	inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
	inode->i_op = &sysfs_inode_operations;

	set_default_inode_attr(inode, kn->mode);
	sysfs_refresh_inode(kn, inode);

	/* initialize inode according to type */
	switch (sysfs_type(kn)) {
	case SYSFS_DIR:
		inode->i_op = &sysfs_dir_inode_operations;
		inode->i_fop = &sysfs_dir_operations;
		break;
	case SYSFS_KOBJ_ATTR:
		inode->i_size = kn->attr.size;
		inode->i_fop = &kernfs_file_operations;
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
 *	sysfs_get_inode - get inode for kernfs_node
 *	@sb: super block
 *	@kn: kernfs_node to allocate inode for
 *
 *	Get inode for @kn.  If such inode doesn't exist, a new inode is
 *	allocated and basics are initialized.  New inode is returned
 *	locked.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	Pointer to allocated inode on success, NULL on failure.
 */
struct inode *sysfs_get_inode(struct super_block *sb, struct kernfs_node *kn)
{
	struct inode *inode;

	inode = iget_locked(sb, kn->ino);
	if (inode && (inode->i_state & I_NEW))
		sysfs_init_inode(kn, inode);

	return inode;
}

/*
 * The kernfs_node serves as both an inode and a directory entry for sysfs.
 * To prevent the sysfs inode numbers from being freed prematurely we take
 * a reference to kernfs_node from the sysfs inode.  A
 * super_operations.evict_inode() implementation is needed to drop that
 * reference upon inode destruction.
 */
void sysfs_evict_inode(struct inode *inode)
{
	struct kernfs_node *kn = inode->i_private;

	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	kernfs_put(kn);
}

int sysfs_permission(struct inode *inode, int mask)
{
	struct kernfs_node *kn;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	kn = inode->i_private;

	mutex_lock(&sysfs_mutex);
	sysfs_refresh_inode(kn, inode);
	mutex_unlock(&sysfs_mutex);

	return generic_permission(inode, mask);
}
