// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/kernfs/iyesde.c - kernfs iyesde implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 */

#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/erryes.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/security.h>

#include "kernfs-internal.h"

static const struct address_space_operations kernfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
};

static const struct iyesde_operations kernfs_iops = {
	.permission	= kernfs_iop_permission,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.listxattr	= kernfs_iop_listxattr,
};

static struct kernfs_iattrs *__kernfs_iattrs(struct kernfs_yesde *kn, int alloc)
{
	static DEFINE_MUTEX(iattr_mutex);
	struct kernfs_iattrs *ret;

	mutex_lock(&iattr_mutex);

	if (kn->iattr || !alloc)
		goto out_unlock;

	kn->iattr = kmem_cache_zalloc(kernfs_iattrs_cache, GFP_KERNEL);
	if (!kn->iattr)
		goto out_unlock;

	/* assign default attributes */
	kn->iattr->ia_uid = GLOBAL_ROOT_UID;
	kn->iattr->ia_gid = GLOBAL_ROOT_GID;

	ktime_get_real_ts64(&kn->iattr->ia_atime);
	kn->iattr->ia_mtime = kn->iattr->ia_atime;
	kn->iattr->ia_ctime = kn->iattr->ia_atime;

	simple_xattrs_init(&kn->iattr->xattrs);
out_unlock:
	ret = kn->iattr;
	mutex_unlock(&iattr_mutex);
	return ret;
}

static struct kernfs_iattrs *kernfs_iattrs(struct kernfs_yesde *kn)
{
	return __kernfs_iattrs(kn, 1);
}

static struct kernfs_iattrs *kernfs_iattrs_yesalloc(struct kernfs_yesde *kn)
{
	return __kernfs_iattrs(kn, 0);
}

int __kernfs_setattr(struct kernfs_yesde *kn, const struct iattr *iattr)
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
 * kernfs_setattr - set iattr on a yesde
 * @kn: target yesde
 * @iattr: iattr to set
 *
 * Returns 0 on success, -erryes on failure.
 */
int kernfs_setattr(struct kernfs_yesde *kn, const struct iattr *iattr)
{
	int ret;

	mutex_lock(&kernfs_mutex);
	ret = __kernfs_setattr(kn, iattr);
	mutex_unlock(&kernfs_mutex);
	return ret;
}

int kernfs_iop_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct kernfs_yesde *kn = iyesde->i_private;
	int error;

	if (!kn)
		return -EINVAL;

	mutex_lock(&kernfs_mutex);
	error = setattr_prepare(dentry, iattr);
	if (error)
		goto out;

	error = __kernfs_setattr(kn, iattr);
	if (error)
		goto out;

	/* this igyesres size changes */
	setattr_copy(iyesde, iattr);

out:
	mutex_unlock(&kernfs_mutex);
	return error;
}

ssize_t kernfs_iop_listxattr(struct dentry *dentry, char *buf, size_t size)
{
	struct kernfs_yesde *kn = kernfs_dentry_yesde(dentry);
	struct kernfs_iattrs *attrs;

	attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	return simple_xattr_list(d_iyesde(dentry), &attrs->xattrs, buf, size);
}

static inline void set_default_iyesde_attr(struct iyesde *iyesde, umode_t mode)
{
	iyesde->i_mode = mode;
	iyesde->i_atime = iyesde->i_mtime =
		iyesde->i_ctime = current_time(iyesde);
}

static inline void set_iyesde_attr(struct iyesde *iyesde,
				  struct kernfs_iattrs *attrs)
{
	iyesde->i_uid = attrs->ia_uid;
	iyesde->i_gid = attrs->ia_gid;
	iyesde->i_atime = timestamp_truncate(attrs->ia_atime, iyesde);
	iyesde->i_mtime = timestamp_truncate(attrs->ia_mtime, iyesde);
	iyesde->i_ctime = timestamp_truncate(attrs->ia_ctime, iyesde);
}

static void kernfs_refresh_iyesde(struct kernfs_yesde *kn, struct iyesde *iyesde)
{
	struct kernfs_iattrs *attrs = kn->iattr;

	iyesde->i_mode = kn->mode;
	if (attrs)
		/*
		 * kernfs_yesde has yesn-default attributes get them from
		 * persistent copy in kernfs_yesde.
		 */
		set_iyesde_attr(iyesde, attrs);

	if (kernfs_type(kn) == KERNFS_DIR)
		set_nlink(iyesde, kn->dir.subdirs + 2);
}

int kernfs_iop_getattr(const struct path *path, struct kstat *stat,
		       u32 request_mask, unsigned int query_flags)
{
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct kernfs_yesde *kn = iyesde->i_private;

	mutex_lock(&kernfs_mutex);
	kernfs_refresh_iyesde(kn, iyesde);
	mutex_unlock(&kernfs_mutex);

	generic_fillattr(iyesde, stat);
	return 0;
}

static void kernfs_init_iyesde(struct kernfs_yesde *kn, struct iyesde *iyesde)
{
	kernfs_get(kn);
	iyesde->i_private = kn;
	iyesde->i_mapping->a_ops = &kernfs_aops;
	iyesde->i_op = &kernfs_iops;
	iyesde->i_generation = kernfs_gen(kn);

	set_default_iyesde_attr(iyesde, kn->mode);
	kernfs_refresh_iyesde(kn, iyesde);

	/* initialize iyesde according to type */
	switch (kernfs_type(kn)) {
	case KERNFS_DIR:
		iyesde->i_op = &kernfs_dir_iops;
		iyesde->i_fop = &kernfs_dir_fops;
		if (kn->flags & KERNFS_EMPTY_DIR)
			make_empty_dir_iyesde(iyesde);
		break;
	case KERNFS_FILE:
		iyesde->i_size = kn->attr.size;
		iyesde->i_fop = &kernfs_file_fops;
		break;
	case KERNFS_LINK:
		iyesde->i_op = &kernfs_symlink_iops;
		break;
	default:
		BUG();
	}

	unlock_new_iyesde(iyesde);
}

/**
 *	kernfs_get_iyesde - get iyesde for kernfs_yesde
 *	@sb: super block
 *	@kn: kernfs_yesde to allocate iyesde for
 *
 *	Get iyesde for @kn.  If such iyesde doesn't exist, a new iyesde is
 *	allocated and basics are initialized.  New iyesde is returned
 *	locked.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	Pointer to allocated iyesde on success, NULL on failure.
 */
struct iyesde *kernfs_get_iyesde(struct super_block *sb, struct kernfs_yesde *kn)
{
	struct iyesde *iyesde;

	iyesde = iget_locked(sb, kernfs_iyes(kn));
	if (iyesde && (iyesde->i_state & I_NEW))
		kernfs_init_iyesde(kn, iyesde);

	return iyesde;
}

/*
 * The kernfs_yesde serves as both an iyesde and a directory entry for
 * kernfs.  To prevent the kernfs iyesde numbers from being freed
 * prematurely we take a reference to kernfs_yesde from the kernfs iyesde.  A
 * super_operations.evict_iyesde() implementation is needed to drop that
 * reference upon iyesde destruction.
 */
void kernfs_evict_iyesde(struct iyesde *iyesde)
{
	struct kernfs_yesde *kn = iyesde->i_private;

	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);
	kernfs_put(kn);
}

int kernfs_iop_permission(struct iyesde *iyesde, int mask)
{
	struct kernfs_yesde *kn;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	kn = iyesde->i_private;

	mutex_lock(&kernfs_mutex);
	kernfs_refresh_iyesde(kn, iyesde);
	mutex_unlock(&kernfs_mutex);

	return generic_permission(iyesde, mask);
}

int kernfs_xattr_get(struct kernfs_yesde *kn, const char *name,
		     void *value, size_t size)
{
	struct kernfs_iattrs *attrs = kernfs_iattrs_yesalloc(kn);
	if (!attrs)
		return -ENODATA;

	return simple_xattr_get(&attrs->xattrs, name, value, size);
}

int kernfs_xattr_set(struct kernfs_yesde *kn, const char *name,
		     const void *value, size_t size, int flags)
{
	struct kernfs_iattrs *attrs = kernfs_iattrs(kn);
	if (!attrs)
		return -ENOMEM;

	return simple_xattr_set(&attrs->xattrs, name, value, size, flags);
}

static int kernfs_vfs_xattr_get(const struct xattr_handler *handler,
				struct dentry *unused, struct iyesde *iyesde,
				const char *suffix, void *value, size_t size)
{
	const char *name = xattr_full_name(handler, suffix);
	struct kernfs_yesde *kn = iyesde->i_private;

	return kernfs_xattr_get(kn, name, value, size);
}

static int kernfs_vfs_xattr_set(const struct xattr_handler *handler,
				struct dentry *unused, struct iyesde *iyesde,
				const char *suffix, const void *value,
				size_t size, int flags)
{
	const char *name = xattr_full_name(handler, suffix);
	struct kernfs_yesde *kn = iyesde->i_private;

	return kernfs_xattr_set(kn, name, value, size, flags);
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

const struct xattr_handler *kernfs_xattr_handlers[] = {
	&kernfs_trusted_xattr_handler,
	&kernfs_security_xattr_handler,
	NULL
};
