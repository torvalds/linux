/*
 *   Copyright (C) International Business Machines  Corp., 2002-2004
 *   Copyright (C) Andreas Gruenbacher, 2001
 *   Copyright (C) Linus Torvalds, 1991, 1992
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/posix_acl_xattr.h>
#include "jfs_incore.h"
#include "jfs_txnmgr.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"

struct posix_acl *jfs_get_acl(struct inode *inode, int type)
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

	size = __jfs_getxattr(inode, ea_name, NULL, 0);

	if (size > 0) {
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = __jfs_getxattr(inode, ea_name, value, size);
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

static int __jfs_set_acl(tid_t tid, struct inode *inode, int type,
		       struct posix_acl *acl)
{
	char *ea_name;
	int rc;
	int size = 0;
	char *value = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		ea_name = XATTR_NAME_POSIX_ACL_ACCESS;
		if (acl) {
			rc = posix_acl_equiv_mode(acl, &inode->i_mode);
			if (rc < 0)
				return rc;
			inode->i_ctime = CURRENT_TIME;
			mark_inode_dirty(inode);
			if (rc == 0)
				acl = NULL;
		}
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
	rc = __jfs_setxattr(tid, inode, ea_name, value, size, 0);
out:
	kfree(value);

	if (!rc)
		set_cached_acl(inode, type, acl);

	return rc;
}

int jfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int rc;
	tid_t tid;

	tid = txBegin(inode->i_sb, 0);
	mutex_lock(&JFS_IP(inode)->commit_mutex);
	rc = __jfs_set_acl(tid, inode, type, acl);
	if (!rc)
		rc = txCommit(tid, 1, &inode, 0);
	txEnd(tid);
	mutex_unlock(&JFS_IP(inode)->commit_mutex);
	return rc;
}

int jfs_init_acl(tid_t tid, struct inode *inode, struct inode *dir)
{
	struct posix_acl *default_acl, *acl;
	int rc = 0;

	rc = posix_acl_create(dir, &inode->i_mode, &default_acl, &acl);
	if (rc)
		return rc;

	if (default_acl) {
		rc = __jfs_set_acl(tid, inode, ACL_TYPE_DEFAULT, default_acl);
		posix_acl_release(default_acl);
	}

	if (acl) {
		if (!rc)
			rc = __jfs_set_acl(tid, inode, ACL_TYPE_ACCESS, acl);
		posix_acl_release(acl);
	}

	JFS_IP(inode)->mode2 = (JFS_IP(inode)->mode2 & 0xffff0000) |
			       inode->i_mode;

	return rc;
}
