/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2016 Canonical Ltd. <seth.forshee@canonical.com>
 *
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
 */

#include "fuse_i.h"

#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>

struct posix_acl *fuse_get_acl(struct inode *inode, int type)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	int size;
	const char *name;
	void *value = NULL;
	struct posix_acl *acl;

	if (fuse_is_bad(inode))
		return ERR_PTR(-EIO);

	if (!fc->posix_acl || fc->no_getxattr)
		return NULL;

	if (type == ACL_TYPE_ACCESS)
		name = XATTR_NAME_POSIX_ACL_ACCESS;
	else if (type == ACL_TYPE_DEFAULT)
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
	else
		return ERR_PTR(-EOPNOTSUPP);

	value = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!value)
		return ERR_PTR(-ENOMEM);
	size = fuse_getxattr(inode, name, value, PAGE_SIZE);
	if (size > 0)
		acl = posix_acl_from_xattr(fc->user_ns, value, size);
	else if ((size == 0) || (size == -ENODATA) ||
		 (size == -EOPNOTSUPP && fc->no_getxattr))
		acl = NULL;
	else if (size == -ERANGE)
		acl = ERR_PTR(-E2BIG);
	else
		acl = ERR_PTR(size);

	kfree(value);
	return acl;
}

int fuse_set_acl(struct user_namespace *mnt_userns, struct inode *inode,
		 struct posix_acl *acl, int type)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	const char *name;
	int ret;

	if (fuse_is_bad(inode))
		return -EIO;

	if (!fc->posix_acl || fc->no_setxattr)
		return -EOPNOTSUPP;

	if (type == ACL_TYPE_ACCESS)
		name = XATTR_NAME_POSIX_ACL_ACCESS;
	else if (type == ACL_TYPE_DEFAULT)
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
	else
		return -EINVAL;

	if (acl) {
		/*
		 * Fuse userspace is responsible for updating access
		 * permissions in the inode, if needed. fuse_setxattr
		 * invalidates the inode attributes, which will force
		 * them to be refreshed the next time they are used,
		 * and it also updates i_ctime.
		 */
		size_t size = posix_acl_xattr_size(acl->a_count);
		void *value;

		if (size > PAGE_SIZE)
			return -E2BIG;

		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;

		ret = posix_acl_to_xattr(fc->user_ns, acl, value, size);
		if (ret < 0) {
			kfree(value);
			return ret;
		}

		ret = fuse_setxattr(inode, name, value, size, 0);
		kfree(value);
	} else {
		ret = fuse_removexattr(inode, name);
	}
	forget_all_cached_acls(inode);
	fuse_invalidate_attr(inode);

	return ret;
}
