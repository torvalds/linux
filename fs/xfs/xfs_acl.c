// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2008, Christoph Hellwig
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_trace.h"
#include "xfs_error.h"
#include "xfs_acl.h"
#include "xfs_trans.h"
#include "xfs_xattr.h"

#include <linux/posix_acl_xattr.h>

/*
 * Locking scheme:
 *  - all ACL updates are protected by inode->i_mutex, which is taken before
 *    calling into this file.
 */

STATIC struct posix_acl *
xfs_acl_from_disk(
	struct xfs_mount	*mp,
	const struct xfs_acl	*aclp,
	int			len,
	int			max_entries)
{
	struct posix_acl_entry *acl_e;
	struct posix_acl *acl;
	const struct xfs_acl_entry *ace;
	unsigned int count, i;

	if (len < sizeof(*aclp)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, aclp,
				len);
		return ERR_PTR(-EFSCORRUPTED);
	}

	count = be32_to_cpu(aclp->acl_cnt);
	if (count > max_entries || XFS_ACL_SIZE(count) != len) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, aclp,
				len);
		return ERR_PTR(-EFSCORRUPTED);
	}

	acl = posix_acl_alloc(count, GFP_KERNEL);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < count; i++) {
		acl_e = &acl->a_entries[i];
		ace = &aclp->acl_entry[i];

		/*
		 * The tag is 32 bits on disk and 16 bits in core.
		 *
		 * Because every access to it goes through the core
		 * format first this is not a problem.
		 */
		acl_e->e_tag = be32_to_cpu(ace->ae_tag);
		acl_e->e_perm = be16_to_cpu(ace->ae_perm);

		switch (acl_e->e_tag) {
		case ACL_USER:
			acl_e->e_uid = make_kuid(&init_user_ns,
						 be32_to_cpu(ace->ae_id));
			break;
		case ACL_GROUP:
			acl_e->e_gid = make_kgid(&init_user_ns,
						 be32_to_cpu(ace->ae_id));
			break;
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			break;
		default:
			goto fail;
		}
	}
	return acl;

fail:
	posix_acl_release(acl);
	return ERR_PTR(-EINVAL);
}

STATIC void
xfs_acl_to_disk(struct xfs_acl *aclp, const struct posix_acl *acl)
{
	const struct posix_acl_entry *acl_e;
	struct xfs_acl_entry *ace;
	int i;

	aclp->acl_cnt = cpu_to_be32(acl->a_count);
	for (i = 0; i < acl->a_count; i++) {
		ace = &aclp->acl_entry[i];
		acl_e = &acl->a_entries[i];

		ace->ae_tag = cpu_to_be32(acl_e->e_tag);
		switch (acl_e->e_tag) {
		case ACL_USER:
			ace->ae_id = cpu_to_be32(
					from_kuid(&init_user_ns, acl_e->e_uid));
			break;
		case ACL_GROUP:
			ace->ae_id = cpu_to_be32(
					from_kgid(&init_user_ns, acl_e->e_gid));
			break;
		default:
			ace->ae_id = cpu_to_be32(ACL_UNDEFINED_ID);
			break;
		}

		ace->ae_perm = cpu_to_be16(acl_e->e_perm);
	}
}

struct posix_acl *
xfs_get_acl(struct inode *inode, int type, bool rcu)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct posix_acl	*acl = NULL;
	struct xfs_da_args	args = {
		.dp		= ip,
		.attr_filter	= XFS_ATTR_ROOT,
		.valuelen	= XFS_ACL_MAX_SIZE(mp),
	};
	int			error;

	if (rcu)
		return ERR_PTR(-ECHILD);

	trace_xfs_get_acl(ip);

	switch (type) {
	case ACL_TYPE_ACCESS:
		args.name = SGI_ACL_FILE;
		break;
	case ACL_TYPE_DEFAULT:
		args.name = SGI_ACL_DEFAULT;
		break;
	default:
		BUG();
	}
	args.namelen = strlen(args.name);

	/*
	 * If the attribute doesn't exist make sure we have a negative cache
	 * entry, for any other error assume it is transient.
	 */
	error = xfs_attr_get(&args);
	if (!error) {
		acl = xfs_acl_from_disk(mp, args.value, args.valuelen,
					XFS_ACL_MAX_ENTRIES(mp));
	} else if (error != -ENOATTR) {
		acl = ERR_PTR(error);
	}

	kmem_free(args.value);
	return acl;
}

int
__xfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_da_args	args = {
		.dp		= ip,
		.attr_filter	= XFS_ATTR_ROOT,
	};
	int			error;

	switch (type) {
	case ACL_TYPE_ACCESS:
		args.name = SGI_ACL_FILE;
		break;
	case ACL_TYPE_DEFAULT:
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		args.name = SGI_ACL_DEFAULT;
		break;
	default:
		return -EINVAL;
	}
	args.namelen = strlen(args.name);

	if (acl) {
		args.valuelen = XFS_ACL_SIZE(acl->a_count);
		args.value = kvzalloc(args.valuelen, GFP_KERNEL);
		if (!args.value)
			return -ENOMEM;
		xfs_acl_to_disk(args.value, acl);
	}

	error = xfs_attr_change(&args);
	kmem_free(args.value);

	/*
	 * If the attribute didn't exist to start with that's fine.
	 */
	if (!acl && error == -ENOATTR)
		error = 0;
	if (!error)
		set_cached_acl(inode, type, acl);
	return error;
}

static int
xfs_acl_set_mode(
	struct inode		*inode,
	umode_t			mode)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ichange, 0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	inode->i_mode = mode;
	inode->i_ctime = current_time(inode);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	if (xfs_has_wsync(mp))
		xfs_trans_set_sync(tp);
	return xfs_trans_commit(tp);
}

int
xfs_set_acl(struct user_namespace *mnt_userns, struct dentry *dentry,
	    struct posix_acl *acl, int type)
{
	umode_t mode;
	bool set_mode = false;
	int error = 0;
	struct inode *inode = d_inode(dentry);

	if (!acl)
		goto set_acl;

	error = -E2BIG;
	if (acl->a_count > XFS_ACL_MAX_ENTRIES(XFS_M(inode->i_sb)))
		return error;

	if (type == ACL_TYPE_ACCESS) {
		error = posix_acl_update_mode(mnt_userns, inode, &mode, &acl);
		if (error)
			return error;
		set_mode = true;
	}

 set_acl:
	/*
	 * We set the mode after successfully updating the ACL xattr because the
	 * xattr update can fail at ENOSPC and we don't want to change the mode
	 * if the ACL update hasn't been applied.
	 */
	error =  __xfs_set_acl(inode, acl, type);
	if (!error && set_mode && mode != inode->i_mode)
		error = xfs_acl_set_mode(inode, mode);
	return error;
}

/*
 * Invalidate any cached ACLs if the user has bypassed the ACL interface.
 * We don't validate the content whatsoever so it is caller responsibility to
 * provide data in valid format and ensure i_mode is consistent.
 */
void
xfs_forget_acl(
	struct inode		*inode,
	const char		*name)
{
	if (!strcmp(name, SGI_ACL_FILE))
		forget_cached_acl(inode, ACL_TYPE_ACCESS);
	else if (!strcmp(name, SGI_ACL_DEFAULT))
		forget_cached_acl(inode, ACL_TYPE_DEFAULT);
}
