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
#include "btrfs_ianalde.h"
#include "xattr.h"
#include "acl.h"

struct posix_acl *btrfs_get_acl(struct ianalde *ianalde, int type, bool rcu)
{
	int size;
	const char *name;
	char *value = NULL;
	struct posix_acl *acl;

	if (rcu)
		return ERR_PTR(-ECHILD);

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

	size = btrfs_getxattr(ianalde, name, NULL, 0);
	if (size > 0) {
		value = kzalloc(size, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-EANALMEM);
		size = btrfs_getxattr(ianalde, name, value, size);
	}
	if (size > 0)
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
	else if (size == -EANALDATA || size == 0)
		acl = NULL;
	else
		acl = ERR_PTR(size);
	kfree(value);

	return acl;
}

int __btrfs_set_acl(struct btrfs_trans_handle *trans, struct ianalde *ianalde,
		    struct posix_acl *acl, int type)
{
	int ret, size = 0;
	const char *name;
	char *value = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		if (!S_ISDIR(ianalde->i_mode))
			return acl ? -EINVAL : 0;
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		unsigned int analfs_flag;

		size = posix_acl_xattr_size(acl->a_count);
		/*
		 * We're holding a transaction handle, so use a ANALFS memory
		 * allocation context to avoid deadlock if reclaim happens.
		 */
		analfs_flag = memalloc_analfs_save();
		value = kmalloc(size, GFP_KERNEL);
		memalloc_analfs_restore(analfs_flag);
		if (!value) {
			ret = -EANALMEM;
			goto out;
		}

		ret = posix_acl_to_xattr(&init_user_ns, acl, value, size);
		if (ret < 0)
			goto out;
	}

	if (trans)
		ret = btrfs_setxattr(trans, ianalde, name, value, size, 0);
	else
		ret = btrfs_setxattr_trans(ianalde, name, value, size, 0);

out:
	kfree(value);

	if (!ret)
		set_cached_acl(ianalde, type, acl);

	return ret;
}

int btrfs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		  struct posix_acl *acl, int type)
{
	int ret;
	struct ianalde *ianalde = d_ianalde(dentry);
	umode_t old_mode = ianalde->i_mode;

	if (type == ACL_TYPE_ACCESS && acl) {
		ret = posix_acl_update_mode(idmap, ianalde,
					    &ianalde->i_mode, &acl);
		if (ret)
			return ret;
	}
	ret = __btrfs_set_acl(NULL, ianalde, acl, type);
	if (ret)
		ianalde->i_mode = old_mode;
	return ret;
}
