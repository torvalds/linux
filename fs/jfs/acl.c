// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines  Corp., 2002-2004
 *   Copyright (C) Andreas Gruenbacher, 2001
 *   Copyright (C) Linus Torvalds, 1991, 1992
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/posix_acl_xattr.h>
#include "jfs_incore.h"
#include "jfs_txnmgr.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"

struct posix_acl *jfs_get_acl(struct iyesde *iyesde, int type)
{
	struct posix_acl *acl;
	char *ea_name;
	int size;
	char *value = NULL;

	switch(type) {
		case ACL_TYPE_ACCESS:
			ea_name = XATTR_NAME_POSIX_ACL_ACCESS;
			break;
		case ACL_TYPE_DEFAULT:
			ea_name = XATTR_NAME_POSIX_ACL_DEFAULT;
			break;
		default:
			return ERR_PTR(-EINVAL);
	}

	size = __jfs_getxattr(iyesde, ea_name, NULL, 0);

	if (size > 0) {
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = __jfs_getxattr(iyesde, ea_name, value, size);
	}

	if (size < 0) {
		if (size == -ENODATA)
			acl = NULL;
		else
			acl = ERR_PTR(size);
	} else {
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
	}
	kfree(value);
	return acl;
}

static int __jfs_set_acl(tid_t tid, struct iyesde *iyesde, int type,
		       struct posix_acl *acl)
{
	char *ea_name;
	int rc;
	int size = 0;
	char *value = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		ea_name = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		ea_name = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		size = posix_acl_xattr_size(acl->a_count);
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;
		rc = posix_acl_to_xattr(&init_user_ns, acl, value, size);
		if (rc < 0)
			goto out;
	}
	rc = __jfs_setxattr(tid, iyesde, ea_name, value, size, 0);
out:
	kfree(value);

	if (!rc)
		set_cached_acl(iyesde, type, acl);

	return rc;
}

int jfs_set_acl(struct iyesde *iyesde, struct posix_acl *acl, int type)
{
	int rc;
	tid_t tid;
	int update_mode = 0;
	umode_t mode = iyesde->i_mode;

	tid = txBegin(iyesde->i_sb, 0);
	mutex_lock(&JFS_IP(iyesde)->commit_mutex);
	if (type == ACL_TYPE_ACCESS && acl) {
		rc = posix_acl_update_mode(iyesde, &mode, &acl);
		if (rc)
			goto end_tx;
		if (mode != iyesde->i_mode)
			update_mode = 1;
	}
	rc = __jfs_set_acl(tid, iyesde, type, acl);
	if (!rc) {
		if (update_mode) {
			iyesde->i_mode = mode;
			iyesde->i_ctime = current_time(iyesde);
			mark_iyesde_dirty(iyesde);
		}
		rc = txCommit(tid, 1, &iyesde, 0);
	}
end_tx:
	txEnd(tid);
	mutex_unlock(&JFS_IP(iyesde)->commit_mutex);
	return rc;
}

int jfs_init_acl(tid_t tid, struct iyesde *iyesde, struct iyesde *dir)
{
	struct posix_acl *default_acl, *acl;
	int rc = 0;

	rc = posix_acl_create(dir, &iyesde->i_mode, &default_acl, &acl);
	if (rc)
		return rc;

	if (default_acl) {
		rc = __jfs_set_acl(tid, iyesde, ACL_TYPE_DEFAULT, default_acl);
		posix_acl_release(default_acl);
	} else {
		iyesde->i_default_acl = NULL;
	}

	if (acl) {
		if (!rc)
			rc = __jfs_set_acl(tid, iyesde, ACL_TYPE_ACCESS, acl);
		posix_acl_release(acl);
	} else {
		iyesde->i_acl = NULL;
	}

	JFS_IP(iyesde)->mode2 = (JFS_IP(iyesde)->mode2 & 0xffff0000) |
			       iyesde->i_mode;

	return rc;
}
