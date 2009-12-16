/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * acl.c
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * CREDITS:
 * Lots of code in this file is copy from linux/fs/ext3/acl.c.
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#define MLOG_MASK_PREFIX ML_INODE
#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
#include "dlmglue.h"
#include "file.h"
#include "ocfs2_fs.h"

#include "xattr.h"
#include "acl.h"

/*
 * Convert from xattr value to acl struct.
 */
static struct posix_acl *ocfs2_acl_from_xattr(const void *value, size_t size)
{
	int n, count;
	struct posix_acl *acl;

	if (!value)
		return NULL;
	if (size < sizeof(struct posix_acl_entry))
		return ERR_PTR(-EINVAL);

	count = size / sizeof(struct posix_acl_entry);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;

	acl = posix_acl_alloc(count, GFP_NOFS);
	if (!acl)
		return ERR_PTR(-ENOMEM);
	for (n = 0; n < count; n++) {
		struct ocfs2_acl_entry *entry =
			(struct ocfs2_acl_entry *)value;

		acl->a_entries[n].e_tag  = le16_to_cpu(entry->e_tag);
		acl->a_entries[n].e_perm = le16_to_cpu(entry->e_perm);
		acl->a_entries[n].e_id   = le32_to_cpu(entry->e_id);
		value += sizeof(struct posix_acl_entry);

	}
	return acl;
}

/*
 * Convert acl struct to xattr value.
 */
static void *ocfs2_acl_to_xattr(const struct posix_acl *acl, size_t *size)
{
	struct ocfs2_acl_entry *entry = NULL;
	char *ocfs2_acl;
	size_t n;

	*size = acl->a_count * sizeof(struct posix_acl_entry);

	ocfs2_acl = kmalloc(*size, GFP_NOFS);
	if (!ocfs2_acl)
		return ERR_PTR(-ENOMEM);

	entry = (struct ocfs2_acl_entry *)ocfs2_acl;
	for (n = 0; n < acl->a_count; n++, entry++) {
		entry->e_tag  = cpu_to_le16(acl->a_entries[n].e_tag);
		entry->e_perm = cpu_to_le16(acl->a_entries[n].e_perm);
		entry->e_id   = cpu_to_le32(acl->a_entries[n].e_id);
	}
	return ocfs2_acl;
}

static struct posix_acl *ocfs2_get_acl_nolock(struct inode *inode,
					      int type,
					      struct buffer_head *di_bh)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	int name_index;
	char *value = NULL;
	struct posix_acl *acl;
	int retval;

	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name_index = OCFS2_XATTR_INDEX_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name_index = OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	retval = ocfs2_xattr_get_nolock(inode, di_bh, name_index, "", NULL, 0);
	if (retval > 0) {
		value = kmalloc(retval, GFP_NOFS);
		if (!value)
			return ERR_PTR(-ENOMEM);
		retval = ocfs2_xattr_get_nolock(inode, di_bh, name_index,
						"", value, retval);
	}

	if (retval > 0)
		acl = ocfs2_acl_from_xattr(value, retval);
	else if (retval == -ENODATA || retval == 0)
		acl = NULL;
	else
		acl = ERR_PTR(retval);

	kfree(value);

	return acl;
}


/*
 * Get posix acl.
 */
static struct posix_acl *ocfs2_get_acl(struct inode *inode, int type)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *di_bh = NULL;
	struct posix_acl *acl;
	int ret;

	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return NULL;

	ret = ocfs2_inode_lock(inode, &di_bh, 0);
	if (ret < 0) {
		mlog_errno(ret);
		acl = ERR_PTR(ret);
		return acl;
	}

	acl = ocfs2_get_acl_nolock(inode, type, di_bh);

	ocfs2_inode_unlock(inode, 0);

	brelse(di_bh);

	return acl;
}

/*
 * Set the access or default ACL of an inode.
 */
static int ocfs2_set_acl(handle_t *handle,
			 struct inode *inode,
			 struct buffer_head *di_bh,
			 int type,
			 struct posix_acl *acl,
			 struct ocfs2_alloc_context *meta_ac,
			 struct ocfs2_alloc_context *data_ac)
{
	int name_index;
	void *value = NULL;
	size_t size = 0;
	int ret;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name_index = OCFS2_XATTR_INDEX_POSIX_ACL_ACCESS;
		if (acl) {
			mode_t mode = inode->i_mode;
			ret = posix_acl_equiv_mode(acl, &mode);
			if (ret < 0)
				return ret;
			else {
				inode->i_mode = mode;
				if (ret == 0)
					acl = NULL;
			}
		}
		break;
	case ACL_TYPE_DEFAULT:
		name_index = OCFS2_XATTR_INDEX_POSIX_ACL_DEFAULT;
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		value = ocfs2_acl_to_xattr(acl, &size);
		if (IS_ERR(value))
			return (int)PTR_ERR(value);
	}

	if (handle)
		ret = ocfs2_xattr_set_handle(handle, inode, di_bh, name_index,
					     "", value, size, 0,
					     meta_ac, data_ac);
	else
		ret = ocfs2_xattr_set(inode, name_index, "", value, size, 0);

	kfree(value);

	return ret;
}

int ocfs2_check_acl(struct inode *inode, int mask)
{
	struct posix_acl *acl = ocfs2_get_acl(inode, ACL_TYPE_ACCESS);

	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
		int ret = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
		return ret;
	}

	return -EAGAIN;
}

int ocfs2_acl_chmod(struct inode *inode)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct posix_acl *acl, *clone;
	int ret;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return 0;

	acl = ocfs2_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return PTR_ERR(acl);
	clone = posix_acl_clone(acl, GFP_KERNEL);
	posix_acl_release(acl);
	if (!clone)
		return -ENOMEM;
	ret = posix_acl_chmod_masq(clone, inode->i_mode);
	if (!ret)
		ret = ocfs2_set_acl(NULL, inode, NULL, ACL_TYPE_ACCESS,
				    clone, NULL, NULL);
	posix_acl_release(clone);
	return ret;
}

/*
 * Initialize the ACLs of a new inode. If parent directory has default ACL,
 * then clone to new inode. Called from ocfs2_mknod.
 */
int ocfs2_init_acl(handle_t *handle,
		   struct inode *inode,
		   struct inode *dir,
		   struct buffer_head *di_bh,
		   struct buffer_head *dir_bh,
		   struct ocfs2_alloc_context *meta_ac,
		   struct ocfs2_alloc_context *data_ac)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct posix_acl *acl = NULL;
	int ret = 0;

	if (!S_ISLNK(inode->i_mode)) {
		if (osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL) {
			acl = ocfs2_get_acl_nolock(dir, ACL_TYPE_DEFAULT,
						   dir_bh);
			if (IS_ERR(acl))
				return PTR_ERR(acl);
		}
		if (!acl)
			inode->i_mode &= ~current_umask();
	}
	if ((osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL) && acl) {
		struct posix_acl *clone;
		mode_t mode;

		if (S_ISDIR(inode->i_mode)) {
			ret = ocfs2_set_acl(handle, inode, di_bh,
					    ACL_TYPE_DEFAULT, acl,
					    meta_ac, data_ac);
			if (ret)
				goto cleanup;
		}
		clone = posix_acl_clone(acl, GFP_NOFS);
		ret = -ENOMEM;
		if (!clone)
			goto cleanup;

		mode = inode->i_mode;
		ret = posix_acl_create_masq(clone, &mode);
		if (ret >= 0) {
			inode->i_mode = mode;
			if (ret > 0) {
				ret = ocfs2_set_acl(handle, inode,
						    di_bh, ACL_TYPE_ACCESS,
						    clone, meta_ac, data_ac);
			}
		}
		posix_acl_release(clone);
	}
cleanup:
	posix_acl_release(acl);
	return ret;
}

static size_t ocfs2_xattr_list_acl_access(struct dentry *dentry,
					  char *list,
					  size_t list_len,
					  const char *name,
					  size_t name_len,
					  int type)
{
	struct ocfs2_super *osb = OCFS2_SB(dentry->d_sb);
	const size_t size = sizeof(POSIX_ACL_XATTR_ACCESS);

	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return 0;

	if (list && size <= list_len)
		memcpy(list, POSIX_ACL_XATTR_ACCESS, size);
	return size;
}

static size_t ocfs2_xattr_list_acl_default(struct dentry *dentry,
					   char *list,
					   size_t list_len,
					   const char *name,
					   size_t name_len,
					   int type)
{
	struct ocfs2_super *osb = OCFS2_SB(dentry->d_sb);
	const size_t size = sizeof(POSIX_ACL_XATTR_DEFAULT);

	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return 0;

	if (list && size <= list_len)
		memcpy(list, POSIX_ACL_XATTR_DEFAULT, size);
	return size;
}

static int ocfs2_xattr_get_acl(struct dentry *dentry, const char *name,
		void *buffer, size_t size, int type)
{
	struct ocfs2_super *osb = OCFS2_SB(dentry->d_sb);
	struct posix_acl *acl;
	int ret;

	if (strcmp(name, "") != 0)
		return -EINVAL;
	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return -EOPNOTSUPP;

	acl = ocfs2_get_acl(dentry->d_inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	ret = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);

	return ret;
}

static int ocfs2_xattr_set_acl(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags, int type)
{
	struct inode *inode = dentry->d_inode;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct posix_acl *acl;
	int ret = 0;

	if (strcmp(name, "") != 0)
		return -EINVAL;
	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return -EOPNOTSUPP;

	if (!is_owner_or_cap(inode))
		return -EPERM;

	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		else if (acl) {
			ret = posix_acl_valid(acl);
			if (ret)
				goto cleanup;
		}
	} else
		acl = NULL;

	ret = ocfs2_set_acl(NULL, inode, NULL, type, acl, NULL, NULL);

cleanup:
	posix_acl_release(acl);
	return ret;
}

struct xattr_handler ocfs2_xattr_acl_access_handler = {
	.prefix	= POSIX_ACL_XATTR_ACCESS,
	.flags	= ACL_TYPE_ACCESS,
	.list	= ocfs2_xattr_list_acl_access,
	.get	= ocfs2_xattr_get_acl,
	.set	= ocfs2_xattr_set_acl,
};

struct xattr_handler ocfs2_xattr_acl_default_handler = {
	.prefix	= POSIX_ACL_XATTR_DEFAULT,
	.flags	= ACL_TYPE_DEFAULT,
	.list	= ocfs2_xattr_list_acl_default,
	.get	= ocfs2_xattr_get_acl,
	.set	= ocfs2_xattr_set_acl,
};
