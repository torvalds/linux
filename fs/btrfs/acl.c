// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Red Hat.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/posix_acl.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>

#include "ctree.h"
#include "btrfs_inode.h"
#include "xattr.h"

struct posix_acl *btrfs_get_acl(struct inode *inode, int type)
{
	int size;
	const char *name;
	char *value = NULL;
	struct posix_acl *acl;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	size = btrfs_getxattr(inode, name, NULL, 0);
	if (size > 0) {
		value = kzalloc(size, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = btrfs_getxattr(inode, name, value, size);
	}
	if (size > 0)
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
	else if (size == -ENODATA || size == 0)
		acl = NULL;
	else
		acl = ERR_PTR(size);
	kfree(value);

	return acl;
}

static int __btrfs_set_acl(struct btrfs_trans_handle *trans,
			 struct inode *inode, struct posix_acl *acl, int type)
{
	int ret, size = 0;
	const char *name;
	char *value = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EINVAL : 0;
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		unsigned int nofs_flag;

		size = posix_acl_xattr_size(acl->a_count);
		/*
		 * We're holding a transaction handle, so use a NOFS memory
		 * allocation context to avoid deadlock if reclaim happens.
		 */
		nofs_flag = memalloc_nofs_save();
		value = kmalloc(size, GFP_KERNEL);
		memalloc_nofs_restore(nofs_flag);
		if (!value) {
			ret = -ENOMEM;
			goto out;
		}

		ret = posix_acl_to_xattr(&init_user_ns, acl, value, size);
		if (ret < 0)
			goto out;
	}

	if (trans)
		ret = btrfs_setxattr(trans, inode, name, value, size, 0);
	else
		ret = btrfs_setxattr_trans(inode, name, value, size, 0);

out:
	kfree(value);

	if (!ret)
		set_cached_acl(inode, type, acl);

	return ret;
}

int btrfs_set_acl(struct user_namespace *mnt_userns, struct inode *inode,
		  struct posix_acl *acl, int type)
{
	int ret;
	umode_t old_mode = inode->i_mode;

	if (type == ACL_TYPE_ACCESS && acl) {
		ret = posix_acl_update_mode(&init_user_ns, inode,
					    &inode->i_mode, &acl);
		if (ret)
			return ret;
	}
	ret = __btrfs_set_acl(NULL, inode, acl, type);
	if (ret)
		inode->i_mode = old_mode;
	return ret;
}

int btrfs_init_acl(struct btrfs_trans_handle *trans,
		   struct inode *inode, struct inode *dir)
{
	struct posix_acl *default_acl, *acl;
	int ret = 0;

	/* this happens with subvols */
	if (!dir)
		return 0;

	ret = posix_acl_create(dir, &inode->i_mode, &default_acl, &acl);
	if (ret)
		return ret;

	if (default_acl) {
		ret = __btrfs_set_acl(trans, inode, default_acl,
				      ACL_TYPE_DEFAULT);
		posix_acl_release(default_acl);
	}

	if (acl) {
		if (!ret)
			ret = __btrfs_set_acl(trans, inode, acl,
					      ACL_TYPE_ACCESS);
		posix_acl_release(acl);
	}

	if (!default_acl && !acl)
		cache_no_acl(inode);
	return ret;
}
