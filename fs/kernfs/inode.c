// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/kernfs/inode.c - kernfs inode implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 */

#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/security.h>

#include "kernfs-internal.h"

static const struct inode_operations kernfs_iops = {
	.permission	= kernfs_iop_permission,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.listxattr	= kernfs_iop_listxattr,
};

static struct kernfs_iattrs *__kernfs_iattrs(struct kernfs_node *kn, bool alloc)
{
	struct kernfs_iattrs *ret __free(kfree) = NULL;
	struct kernfs_iattrs *attr;

	attr = READ_ONCE(kn->iattr);
	if (attr || !alloc)
		return attr;

	ret = kmem_cache_zalloc(kernfs_iattrs_cache, GFP_KERNEL);
	if (!ret)
		return NULL;

	/* assign default attributes */
	ret->ia_uid = GLOBAL_ROOT_UID;
	ret->ia_gid = GLOBAL_ROOT_GID;

	ktime_get_real_ts64(&ret->ia_atime);
	ret->ia_mtime = ret->ia_atime;
	ret->ia_ctime = ret->ia_atime;

	simple_xattr_limits_init(&ret->xattr_limits);

	/* If someone raced us, recognize it. */
	if (!try_cmpxchg(&kn->iattr, &attr, ret))
		return READ_ONCE(kn->iattr);

	return no_free_ptr(ret);
}

static struct kernfs_iattrs *kernfs_iattrs(struct kernfs_node *kn)
{
	return __kernfs_iattrs(kn, true);
}

static struct kernfs_iattrs *kernfs_iattrs_noalloc(struct kernfs_node *kn)
{
	return __kernfs_iattrs(kn, false);
}

int __kernfs_setattr(struct kernfs_node *kn, const struct iattr *iattr)
{
	struct kernfs_iattrs *attrs;
	unsigned int ia_valid = iattr->ia_valid;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	if (ia_valid & ATTR_UID)
		attrs->ia_uid = iattr->ia_uid;
	if (ia_valid & ATTR_GID)
		attrs->ia_gid = iattr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		attrs->ia_atime = iattr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		attrs->ia_mtime = iattr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		attrs->ia_ctime = iattr->ia_ctime;
	if (ia_valid & ATTR_MODE)
		kn->mode = iattr->ia_mode;
	return 0;
}

/**
 * kernfs_setattr - set iattr on a node
 * @kn: target node
 * @iattr: iattr to set
 *
 * Return: %0 on success, -errno on failure.
 */
int kernfs_setattr(struct kernfs_node *kn, const struct iattr *iattr)
{
	int ret;
	struct kernfs_root *root = kernfs_root(kn);

	down_write(&root->kernfs_iattr_rwsem);
	ret = __kernfs_setattr(kn, iattr);
	up_write(&root->kernfs_iattr_rwsem);
	return ret;
}

int kernfs_iop_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		       struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	struct kernfs_node *kn = inode->i_private;
	struct kernfs_root *root;
	int error;

	if (!kn)
		return -EINVAL;

	root = kernfs_root(kn);
	down_write(&root->kernfs_iattr_rwsem);
	error = setattr_prepare(&nop_mnt_idmap, dentry, iattr);
	if (error)
		goto out;

	error = __kernfs_setattr(kn, iattr);
	if (error)
		goto out;

	/* this ignores size changes */
	setattr_copy(&nop_mnt_idmap, inode, iattr);

out:
	up_write(&root->kernfs_iattr_rwsem);
	return error;
}

ssize_t kernfs_iop_listxattr(struct dentry *dentry, char *buf, size_t size)
{
	struct kernfs_node *kn = kernfs_dentry_node(dentry);
	struct kernfs_iattrs *attrs;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	return simple_xattr_list(d_inode(dentry), READ_ONCE(attrs->xattrs),
				 buf, size);
}

static inline void set_default_inode_attr(struct inode *inode, umode_t mode)
{
	inode->i_mode = mode;
	simple_inode_init_ts(inode);
}

static inline void set_inode_attr(struct inode *inode,
				  struct kernfs_iattrs *attrs)
{
	inode->i_uid = attrs->ia_uid;
	inode->i_gid = attrs->ia_gid;
	inode_set_atime_to_ts(inode, attrs->ia_atime);
	inode_set_mtime_to_ts(inode, attrs->ia_mtime);
	inode_set_ctime_to_ts(inode, attrs->ia_ctime);
}

static void kernfs_refresh_inode(struct kernfs_node *kn, struct inode *inode)
{
	struct kernfs_iattrs *attrs;

	inode->i_mode = kn->mode;
	attrs = kernfs_iattrs_noalloc(kn);
	if (attrs)
		/*
		 * kernfs_node has non-default attributes get them from
		 * persistent copy in kernfs_node.
		 */
		set_inode_attr(inode, attrs);

	if (kernfs_type(kn) == KERNFS_DIR)
		set_nlink(inode, kn->dir.subdirs + 2);
}

int kernfs_iop_getattr(struct mnt_idmap *idmap,
		       const struct path *path, struct kstat *stat,
		       u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct kernfs_node *kn = inode->i_private;
	struct kernfs_root *root = kernfs_root(kn);

	down_read(&root->kernfs_iattr_rwsem);
	kernfs_refresh_inode(kn, inode);
	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
	up_read(&root->kernfs_iattr_rwsem);

	return 0;
}

static void kernfs_init_inode(struct kernfs_node *kn, struct inode *inode)
{
	kernfs_get(kn);
	inode->i_private = kn;
	inode->i_mapping->a_ops = &ram_aops;
	inode->i_op = &kernfs_iops;
	inode->i_generation = kernfs_gen(kn);

	set_default_inode_attr(inode, kn->mode);
	kernfs_refresh_inode(kn, inode);

	/* initialize inode according to type */
	switch (kernfs_type(kn)) {
	case KERNFS_DIR:
		inode->i_op = &kernfs_dir_iops;
		inode->i_fop = &kernfs_dir_fops;
		if (kn->flags & KERNFS_EMPTY_DIR)
			make_empty_dir_inode(inode);
		break;
	case KERNFS_FILE:
		inode->i_size = kn->attr.size;
		inode->i_fop = &kernfs_file_fops;
		break;
	case KERNFS_LINK:
		inode->i_op = &kernfs_symlink_iops;
		break;
	default:
		BUG();
	}

	unlock_new_inode(inode);
}

/**
 *	kernfs_get_inode - get inode for kernfs_node
 *	@sb: super block
 *	@kn: kernfs_node to allocate inode for
 *
 *	Get inode for @kn.  If such inode doesn't exist, a new inode is
 *	allocated and basics are initialized.  New inode is returned
 *	locked.
 *
 *	Locking:
 *	Kernel thread context (may sleep).
 *
 *	Return:
 *	Pointer to allocated inode on success, %NULL on failure.
 */
struct inode *kernfs_get_inode(struct super_block *sb, struct kernfs_node *kn)
{
	struct inode *inode;

	inode = iget_locked(sb, kernfs_ino(kn));
	if (inode && (inode_state_read_once(inode) & I_NEW))
		kernfs_init_inode(kn, inode);

	return inode;
}

/*
 * The kernfs_node serves as both an inode and a directory entry for
 * kernfs.  To prevent the kernfs inode numbers from being freed
 * prematurely we take a reference to kernfs_node from the kernfs inode.  A
 * super_operations.evict_inode() implementation is needed to drop that
 * reference upon inode destruction.
 */
void kernfs_evict_inode(struct inode *inode)
{
	struct kernfs_node *kn = inode->i_private;

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	kernfs_put(kn);
}

int kernfs_iop_permission(struct mnt_idmap *idmap,
			  struct inode *inode, int mask)
{
	struct kernfs_node *kn;
	struct kernfs_root *root;
	int ret;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	kn = inode->i_private;
	root = kernfs_root(kn);

	down_read(&root->kernfs_iattr_rwsem);
	kernfs_refresh_inode(kn, inode);
	ret = generic_permission(&nop_mnt_idmap, inode, mask);
	up_read(&root->kernfs_iattr_rwsem);

	return ret;
}

int kernfs_xattr_get(struct kernfs_node *kn, const char *name,
		     void *value, size_t size)
{
	struct kernfs_iattrs *attrs = kernfs_iattrs_noalloc(kn);
	struct simple_xattrs *xattrs;

	if (!attrs)
		return -ENODATA;

	xattrs = READ_ONCE(attrs->xattrs);
	if (!xattrs)
		return -ENODATA;

	return simple_xattr_get(xattrs, name, value, size);
}

int kernfs_xattr_set(struct kernfs_node *kn, const char *name,
		     const void *value, size_t size, int flags)
{
	struct simple_xattr *old_xattr;
	struct simple_xattrs *xattrs;
	struct kernfs_iattrs *attrs;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	xattrs = simple_xattrs_lazy_alloc(&attrs->xattrs, value, flags);
	if (IS_ERR_OR_NULL(xattrs))
		return PTR_ERR(xattrs);

	old_xattr = simple_xattr_set(xattrs, name, value, size, flags);
	if (IS_ERR(old_xattr))
		return PTR_ERR(old_xattr);

	simple_xattr_free_rcu(old_xattr);
	return 0;
}

static int kernfs_vfs_xattr_get(const struct xattr_handler *handler,
				struct dentry *unused, struct inode *inode,
				const char *suffix, void *value, size_t size)
{
	const char *name = xattr_full_name(handler, suffix);
	struct kernfs_node *kn = inode->i_private;

	return kernfs_xattr_get(kn, name, value, size);
}

static int kernfs_vfs_xattr_set(const struct xattr_handler *handler,
				struct mnt_idmap *idmap,
				struct dentry *unused, struct inode *inode,
				const char *suffix, const void *value,
				size_t size, int flags)
{
	const char *name = xattr_full_name(handler, suffix);
	struct kernfs_node *kn = inode->i_private;

	return kernfs_xattr_set(kn, name, value, size, flags);
}

static int kernfs_vfs_user_xattr_set(const struct xattr_handler *handler,
				     struct mnt_idmap *idmap,
				     struct dentry *unused, struct inode *inode,
				     const char *suffix, const void *value,
				     size_t size, int flags)
{
	const char *full_name = xattr_full_name(handler, suffix);
	struct kernfs_node *kn = inode->i_private;
	struct simple_xattrs *xattrs;
	struct kernfs_iattrs *attrs;

	if (!(kernfs_root(kn)->flags & KERNFS_ROOT_SUPPORT_USER_XATTR))
		return -EOPNOTSUPP;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	xattrs = simple_xattrs_lazy_alloc(&attrs->xattrs, value, flags);
	if (IS_ERR_OR_NULL(xattrs))
		return PTR_ERR(xattrs);

	return simple_xattr_set_limited(xattrs, &attrs->xattr_limits,
					full_name, value, size, flags);
}

static const struct xattr_handler kernfs_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get = kernfs_vfs_xattr_get,
	.set = kernfs_vfs_xattr_set,
};

static const struct xattr_handler kernfs_security_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = kernfs_vfs_xattr_get,
	.set = kernfs_vfs_xattr_set,
};

static const struct xattr_handler kernfs_user_xattr_handler = {
	.prefix = XATTR_USER_PREFIX,
	.get = kernfs_vfs_xattr_get,
	.set = kernfs_vfs_user_xattr_set,
};

const struct xattr_handler * const kernfs_xattr_handlers[] = {
	&kernfs_trusted_xattr_handler,
	&kernfs_security_xattr_handler,
	&kernfs_user_xattr_handler,
	NULL
};
