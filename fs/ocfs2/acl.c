// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
#include "dlmglue.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
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

	acl = posix_acl_alloc(count, GFP_NOFS);
	if (!acl)
		return ERR_PTR(-ENOMEM);
	for (n = 0; n < count; n++) {
		struct ocfs2_acl_entry *entry =
			(struct ocfs2_acl_entry *)value;

		acl->a_entries[n].e_tag  = le16_to_cpu(entry->e_tag);
		acl->a_entries[n].e_perm = le16_to_cpu(entry->e_perm);
		switch(acl->a_entries[n].e_tag) {
		case ACL_USER:
			acl->a_entries[n].e_uid =
				make_kuid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;
		case ACL_GROUP:
			acl->a_entries[n].e_gid =
				make_kgid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;
		default:
			break;
		}
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
		switch(acl->a_entries[n].e_tag) {
		case ACL_USER:
			entry->e_id = cpu_to_le32(
				from_kuid(&init_user_ns,
					  acl->a_entries[n].e_uid));
			break;
		case ACL_GROUP:
			entry->e_id = cpu_to_le32(
				from_kgid(&init_user_ns,
					  acl->a_entries[n].e_gid));
			break;
		default:
			entry->e_id = cpu_to_le32(ACL_UNDEFINED_ID);
			break;
		}
	}
	return ocfs2_acl;
}

static struct posix_acl *ocfs2_get_acl_nolock(struct inode *inode,
					      int type,
					      struct buffer_head *di_bh)
{
	int name_index;
	char *value = NULL;
	struct posix_acl *acl;
	int retval;

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
 * Helper function to set i_mode in memory and disk. Some call paths
 * will not have di_bh or a journal handle to pass, in which case it
 * will create it's own.
 */
static int ocfs2_acl_set_mode(struct inode *inode, struct buffer_head *di_bh,
			      handle_t *handle, umode_t new_mode)
{
	int ret, commit_handle = 0;
	struct ocfs2_dinode *di;

	if (di_bh == NULL) {
		ret = ocfs2_read_inode_block(inode, &di_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	} else
		get_bh(di_bh);

	if (handle == NULL) {
		handle = ocfs2_start_trans(OCFS2_SB(inode->i_sb),
					   OCFS2_INODE_UPDATE_CREDITS);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			mlog_errno(ret);
			goto out_brelse;
		}

		commit_handle = 1;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;
	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	inode->i_mode = new_mode;
	inode->i_ctime = current_time(inode);
	di->i_mode = cpu_to_le16(inode->i_mode);
	di->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	di->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
	ocfs2_update_inode_fsync_trans(handle, inode, 0);

	ocfs2_journal_dirty(handle, di_bh);

out_commit:
	if (commit_handle)
		ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out_brelse:
	brelse(di_bh);
out:
	return ret;
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
	if (!ret)
		set_cached_acl(inode, type, acl);

	return ret;
}

int ocfs2_iop_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	struct buffer_head *bh = NULL;
	int status, had_lock;
	struct ocfs2_lock_holder oh;

	had_lock = ocfs2_inode_lock_tracker(inode, &bh, 1, &oh);
	if (had_lock < 0)
		return had_lock;
	if (type == ACL_TYPE_ACCESS && acl) {
		umode_t mode;

		status = posix_acl_update_mode(inode, &mode, &acl);
		if (status)
			goto unlock;

		status = ocfs2_acl_set_mode(inode, bh, NULL, mode);
		if (status)
			goto unlock;
	}
	status = ocfs2_set_acl(NULL, inode, bh, type, acl, NULL, NULL);
unlock:
	ocfs2_inode_unlock_tracker(inode, 1, &oh, had_lock);
	brelse(bh);
	return status;
}

struct posix_acl *ocfs2_iop_get_acl(struct inode *inode, int type)
{
	struct ocfs2_super *osb;
	struct buffer_head *di_bh = NULL;
	struct posix_acl *acl;
	int had_lock;
	struct ocfs2_lock_holder oh;

	osb = OCFS2_SB(inode->i_sb);
	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return NULL;

	had_lock = ocfs2_inode_lock_tracker(inode, &di_bh, 0, &oh);
	if (had_lock < 0)
		return ERR_PTR(had_lock);

	down_read(&OCFS2_I(inode)->ip_xattr_sem);
	acl = ocfs2_get_acl_nolock(inode, type, di_bh);
	up_read(&OCFS2_I(inode)->ip_xattr_sem);

	ocfs2_inode_unlock_tracker(inode, 0, &oh, had_lock);
	brelse(di_bh);
	return acl;
}

int ocfs2_acl_chmod(struct inode *inode, struct buffer_head *bh)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct posix_acl *acl;
	int ret;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	if (!(osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL))
		return 0;

	down_read(&OCFS2_I(inode)->ip_xattr_sem);
	acl = ocfs2_get_acl_nolock(inode, ACL_TYPE_ACCESS, bh);
	up_read(&OCFS2_I(inode)->ip_xattr_sem);
	if (IS_ERR_OR_NULL(acl))
		return PTR_ERR_OR_ZERO(acl);
	ret = __posix_acl_chmod(&acl, GFP_KERNEL, inode->i_mode);
	if (ret)
		return ret;
	ret = ocfs2_set_acl(NULL, inode, NULL, ACL_TYPE_ACCESS,
			    acl, NULL, NULL);
	posix_acl_release(acl);
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
	int ret = 0, ret2;
	umode_t mode;

	if (!S_ISLNK(inode->i_mode)) {
		if (osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL) {
			down_read(&OCFS2_I(dir)->ip_xattr_sem);
			acl = ocfs2_get_acl_nolock(dir, ACL_TYPE_DEFAULT,
						   dir_bh);
			up_read(&OCFS2_I(dir)->ip_xattr_sem);
			if (IS_ERR(acl))
				return PTR_ERR(acl);
		}
		if (!acl) {
			mode = inode->i_mode & ~current_umask();
			ret = ocfs2_acl_set_mode(inode, di_bh, handle, mode);
			if (ret) {
				mlog_errno(ret);
				goto cleanup;
			}
		}
	}
	if ((osb->s_mount_opt & OCFS2_MOUNT_POSIX_ACL) && acl) {
		if (S_ISDIR(inode->i_mode)) {
			ret = ocfs2_set_acl(handle, inode, di_bh,
					    ACL_TYPE_DEFAULT, acl,
					    meta_ac, data_ac);
			if (ret)
				goto cleanup;
		}
		mode = inode->i_mode;
		ret = __posix_acl_create(&acl, GFP_NOFS, &mode);
		if (ret < 0)
			return ret;

		ret2 = ocfs2_acl_set_mode(inode, di_bh, handle, mode);
		if (ret2) {
			mlog_errno(ret2);
			ret = ret2;
			goto cleanup;
		}
		if (ret > 0) {
			ret = ocfs2_set_acl(handle, inode,
					    di_bh, ACL_TYPE_ACCESS,
					    acl, meta_ac, data_ac);
		}
	}
cleanup:
	posix_acl_release(acl);
	return ret;
}
