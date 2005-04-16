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
#include <linux/fs.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"

static struct posix_acl *jfs_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	char *ea_name;
	struct jfs_inode_info *ji = JFS_IP(inode);
	struct posix_acl **p_acl;
	int size;
	char *value = NULL;

	switch(type) {
		case ACL_TYPE_ACCESS:
			ea_name = XATTR_NAME_ACL_ACCESS;
			p_acl = &ji->i_acl;
			break;
		case ACL_TYPE_DEFAULT:
			ea_name = XATTR_NAME_ACL_DEFAULT;
			p_acl = &ji->i_default_acl;
			break;
		default:
			return ERR_PTR(-EINVAL);
	}

	if (*p_acl != JFS_ACL_NOT_CACHED)
		return posix_acl_dup(*p_acl);

	size = __jfs_getxattr(inode, ea_name, NULL, 0);

	if (size > 0) {
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		size = __jfs_getxattr(inode, ea_name, value, size);
	}

	if (size < 0) {
		if (size == -ENODATA) {
			*p_acl = NULL;
			acl = NULL;
		} else
			acl = ERR_PTR(size);
	} else {
		acl = posix_acl_from_xattr(value, size);
		if (!IS_ERR(acl))
			*p_acl = posix_acl_dup(acl);
	}
	if (value)
		kfree(value);
	return acl;
}

static int jfs_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	char *ea_name;
	struct jfs_inode_info *ji = JFS_IP(inode);
	struct posix_acl **p_acl;
	int rc;
	int size = 0;
	char *value = NULL;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	switch(type) {
		case ACL_TYPE_ACCESS:
			ea_name = XATTR_NAME_ACL_ACCESS;
			p_acl = &ji->i_acl;
			break;
		case ACL_TYPE_DEFAULT:
			ea_name = XATTR_NAME_ACL_DEFAULT;
			p_acl = &ji->i_default_acl;
			if (!S_ISDIR(inode->i_mode))
				return acl ? -EACCES : 0;
			break;
		default:
			return -EINVAL;
	}
	if (acl) {
		size = xattr_acl_size(acl->a_count);
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;
		rc = posix_acl_to_xattr(acl, value, size);
		if (rc < 0)
			goto out;
	}
	rc = __jfs_setxattr(inode, ea_name, value, size, 0);
out:
	if (value)
		kfree(value);

	if (!rc) {
		if (*p_acl && (*p_acl != JFS_ACL_NOT_CACHED))
			posix_acl_release(*p_acl);
		*p_acl = posix_acl_dup(acl);
	}
	return rc;
}

static int jfs_check_acl(struct inode *inode, int mask)
{
	struct jfs_inode_info *ji = JFS_IP(inode);

	if (ji->i_acl == JFS_ACL_NOT_CACHED) {
		struct posix_acl *acl = jfs_get_acl(inode, ACL_TYPE_ACCESS);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		posix_acl_release(acl);
	}

	if (ji->i_acl)
		return posix_acl_permission(inode, ji->i_acl, mask);
	return -EAGAIN;
}

int jfs_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	return generic_permission(inode, mask, jfs_check_acl);
}

int jfs_init_acl(struct inode *inode, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	struct posix_acl *clone;
	mode_t mode;
	int rc = 0;

	if (S_ISLNK(inode->i_mode))
		return 0;

	acl = jfs_get_acl(dir, ACL_TYPE_DEFAULT);
	if (IS_ERR(acl))
		return PTR_ERR(acl);

	if (acl) {
		if (S_ISDIR(inode->i_mode)) {
			rc = jfs_set_acl(inode, ACL_TYPE_DEFAULT, acl);
			if (rc)
				goto cleanup;
		}
		clone = posix_acl_clone(acl, GFP_KERNEL);
		if (!clone) {
			rc = -ENOMEM;
			goto cleanup;
		}
		mode = inode->i_mode;
		rc = posix_acl_create_masq(clone, &mode);
		if (rc >= 0) {
			inode->i_mode = mode;
			if (rc > 0)
				rc = jfs_set_acl(inode, ACL_TYPE_ACCESS, clone);
		}
		posix_acl_release(clone);
cleanup:
		posix_acl_release(acl);
	} else
		inode->i_mode &= ~current->fs->umask;

	return rc;
}

static int jfs_acl_chmod(struct inode *inode)
{
	struct posix_acl *acl, *clone;
	int rc;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	acl = jfs_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return PTR_ERR(acl);

	clone = posix_acl_clone(acl, GFP_KERNEL);
	posix_acl_release(acl);
	if (!clone)
		return -ENOMEM;

	rc = posix_acl_chmod_masq(clone, inode->i_mode);
	if (!rc)
		rc = jfs_set_acl(inode, ACL_TYPE_ACCESS, clone);

	posix_acl_release(clone);
	return rc;
}

int jfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int rc;

	rc = inode_change_ok(inode, iattr);
	if (rc)
		return rc;

	if ((iattr->ia_valid & ATTR_UID && iattr->ia_uid != inode->i_uid) ||
	    (iattr->ia_valid & ATTR_GID && iattr->ia_gid != inode->i_gid)) {
		if (DQUOT_TRANSFER(inode, iattr))
			return -EDQUOT;
	}

	rc = inode_setattr(inode, iattr);

	if (!rc && (iattr->ia_valid & ATTR_MODE))
		rc = jfs_acl_chmod(inode);

	return rc;
}
