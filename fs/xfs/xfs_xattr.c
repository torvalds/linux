// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Christoph Hellwig.
 * Portions Copyright (C) 2000-2008 Silicon Graphics, Inc.
 */

#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_da_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_acl.h"
#include "xfs_log.h"
#include "xfs_xattr.h"
#include "xfs_quota.h"

#include <linux/posix_acl_xattr.h>

/*
 * Get permission to use log-assisted atomic exchange of file extents.
 * Callers must not be running any transactions or hold any ILOCKs.
 */
static inline int
xfs_attr_grab_log_assist(
	struct xfs_mount	*mp)
{
	int			error = 0;

	/* xattr update log intent items are already enabled */
	if (xfs_is_using_logged_xattrs(mp))
		return 0;

	/*
	 * Check if the filesystem featureset is new enough to set this log
	 * incompat feature bit.  Strictly speaking, the minimum requirement is
	 * a V5 filesystem for the superblock field, but we'll require rmap
	 * or reflink to avoid having to deal with really old kernels.
	 */
	if (!xfs_has_reflink(mp) && !xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;

	/* Enable log-assisted xattrs. */
	error = xfs_add_incompat_log_feature(mp,
			XFS_SB_FEAT_INCOMPAT_LOG_XATTRS);
	if (error)
		return error;
	xfs_set_using_logged_xattrs(mp);

	xfs_warn_experimental(mp, XFS_EXPERIMENTAL_LARP);

	return 0;
}

static inline bool
xfs_attr_want_log_assist(
	struct xfs_mount	*mp)
{
#ifdef DEBUG
	/* Logged xattrs require a V5 super for log_incompat */
	return xfs_has_crc(mp) && xfs_globals.larp;
#else
	return false;
#endif
}

/*
 * Set or remove an xattr, having grabbed the appropriate logging resources
 * prior to calling libxfs.  Callers of this function are only required to
 * initialize the inode, attr_filter, name, namelen, value, and valuelen fields
 * of @args.
 */
int
xfs_attr_change(
	struct xfs_da_args	*args,
	enum xfs_attr_update	op)
{
	struct xfs_mount	*mp = args->dp->i_mount;
	int			error;

	if (xfs_is_shutdown(mp))
		return -EIO;

	error = xfs_qm_dqattach(args->dp);
	if (error)
		return error;

	/*
	 * We have no control over the attribute names that userspace passes us
	 * to remove, so we have to allow the name lookup prior to attribute
	 * removal to fail as well.
	 */
	args->op_flags = XFS_DA_OP_OKNOENT;

	if (xfs_attr_want_log_assist(mp)) {
		error = xfs_attr_grab_log_assist(mp);
		if (error)
			return error;

		args->op_flags |= XFS_DA_OP_LOGGED;
	}

	args->owner = args->dp->i_ino;
	args->geo = mp->m_attr_geo;
	args->whichfork = XFS_ATTR_FORK;
	xfs_attr_sethash(args);

	/*
	 * Some xattrs must be resistant to allocation failure at ENOSPC, e.g.
	 * creating an inode with ACLs or security attributes requires the
	 * allocation of the xattr holding that information to succeed. Hence
	 * we allow xattrs in the VFS TRUSTED, SYSTEM, POSIX_ACL and SECURITY
	 * (LSM xattr) namespaces to dip into the reserve block pool to allow
	 * manipulation of these xattrs when at ENOSPC. These VFS xattr
	 * namespaces translate to the XFS_ATTR_ROOT and XFS_ATTR_SECURE on-disk
	 * namespaces.
	 *
	 * For most of these cases, these special xattrs will fit in the inode
	 * itself and so consume no extra space or only require temporary extra
	 * space while an overwrite is being made. Hence the use of the reserved
	 * pool is largely to avoid the worst case reservation from preventing
	 * the xattr from being created at ENOSPC.
	 */
	return xfs_attr_set(args, op,
			args->attr_filter & (XFS_ATTR_ROOT | XFS_ATTR_SECURE));
}


static int
xfs_xattr_get(const struct xattr_handler *handler, struct dentry *unused,
		struct inode *inode, const char *name, void *value, size_t size)
{
	struct xfs_da_args	args = {
		.dp		= XFS_I(inode),
		.attr_filter	= handler->flags,
		.name		= name,
		.namelen	= strlen(name),
		.value		= value,
		.valuelen	= size,
	};
	int			error;

	if (xfs_ifork_zapped(XFS_I(inode), XFS_ATTR_FORK))
		return -EIO;

	error = xfs_attr_get(&args);
	if (error)
		return error;
	return args.valuelen;
}

static inline enum xfs_attr_update
xfs_xattr_flags_to_op(
	int		flags,
	const void	*value)
{
	if (!value)
		return XFS_ATTRUPDATE_REMOVE;
	if (flags & XATTR_CREATE)
		return XFS_ATTRUPDATE_CREATE;
	if (flags & XATTR_REPLACE)
		return XFS_ATTRUPDATE_REPLACE;
	return XFS_ATTRUPDATE_UPSERT;
}

static int
xfs_xattr_set(const struct xattr_handler *handler,
	      struct mnt_idmap *idmap, struct dentry *unused,
	      struct inode *inode, const char *name, const void *value,
	      size_t size, int flags)
{
	struct xfs_da_args	args = {
		.dp		= XFS_I(inode),
		.attr_filter	= handler->flags,
		.name		= name,
		.namelen	= strlen(name),
		.value		= (void *)value,
		.valuelen	= size,
	};
	int			error;

	error = xfs_attr_change(&args, xfs_xattr_flags_to_op(flags, value));
	if (!error && (handler->flags & XFS_ATTR_ROOT))
		xfs_forget_acl(inode, name);
	return error;
}

static const struct xattr_handler xfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.flags	= 0, /* no flags implies user namespace */
	.get	= xfs_xattr_get,
	.set	= xfs_xattr_set,
};

static const struct xattr_handler xfs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.flags	= XFS_ATTR_ROOT,
	.get	= xfs_xattr_get,
	.set	= xfs_xattr_set,
};

static const struct xattr_handler xfs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.flags	= XFS_ATTR_SECURE,
	.get	= xfs_xattr_get,
	.set	= xfs_xattr_set,
};

const struct xattr_handler * const xfs_xattr_handlers[] = {
	&xfs_xattr_user_handler,
	&xfs_xattr_trusted_handler,
	&xfs_xattr_security_handler,
	NULL
};

static void
__xfs_xattr_put_listent(
	struct xfs_attr_list_context *context,
	char *prefix,
	int prefix_len,
	unsigned char *name,
	int namelen)
{
	char *offset;
	int arraytop;

	if (context->count < 0 || context->seen_enough)
		return;

	if (!context->buffer)
		goto compute_size;

	arraytop = context->count + prefix_len + namelen + 1;
	if (arraytop > context->firstu) {
		context->count = -1;	/* insufficient space */
		context->seen_enough = 1;
		return;
	}
	offset = context->buffer + context->count;
	memcpy(offset, prefix, prefix_len);
	offset += prefix_len;
	strncpy(offset, (char *)name, namelen);			/* real name */
	offset += namelen;
	*offset = '\0';

compute_size:
	context->count += prefix_len + namelen + 1;
	return;
}

static void
xfs_xattr_put_listent(
	struct xfs_attr_list_context *context,
	int		flags,
	unsigned char	*name,
	int		namelen,
	void		*value,
	int		valuelen)
{
	char *prefix;
	int prefix_len;

	ASSERT(context->count >= 0);

	/* Don't expose private xattr namespaces. */
	if (flags & XFS_ATTR_PRIVATE_NSP_MASK)
		return;

	if (flags & XFS_ATTR_ROOT) {
#ifdef CONFIG_XFS_POSIX_ACL
		if (namelen == SGI_ACL_FILE_SIZE &&
		    strncmp(name, SGI_ACL_FILE,
			    SGI_ACL_FILE_SIZE) == 0) {
			__xfs_xattr_put_listent(
					context, XATTR_SYSTEM_PREFIX,
					XATTR_SYSTEM_PREFIX_LEN,
					XATTR_POSIX_ACL_ACCESS,
					strlen(XATTR_POSIX_ACL_ACCESS));
		} else if (namelen == SGI_ACL_DEFAULT_SIZE &&
			 strncmp(name, SGI_ACL_DEFAULT,
				 SGI_ACL_DEFAULT_SIZE) == 0) {
			__xfs_xattr_put_listent(
					context, XATTR_SYSTEM_PREFIX,
					XATTR_SYSTEM_PREFIX_LEN,
					XATTR_POSIX_ACL_DEFAULT,
					strlen(XATTR_POSIX_ACL_DEFAULT));
		}
#endif

		/*
		 * Only show root namespace entries if we are actually allowed to
		 * see them.
		 */
		if (!capable(CAP_SYS_ADMIN))
			return;

		prefix = XATTR_TRUSTED_PREFIX;
		prefix_len = XATTR_TRUSTED_PREFIX_LEN;
	} else if (flags & XFS_ATTR_SECURE) {
		prefix = XATTR_SECURITY_PREFIX;
		prefix_len = XATTR_SECURITY_PREFIX_LEN;
	} else {
		prefix = XATTR_USER_PREFIX;
		prefix_len = XATTR_USER_PREFIX_LEN;
	}

	__xfs_xattr_put_listent(context, prefix, prefix_len, name,
				namelen);
	return;
}

ssize_t
xfs_vn_listxattr(
	struct dentry	*dentry,
	char		*data,
	size_t		size)
{
	struct xfs_attr_list_context context;
	struct inode	*inode = d_inode(dentry);
	int		error;

	if (xfs_ifork_zapped(XFS_I(inode), XFS_ATTR_FORK))
		return -EIO;

	/*
	 * First read the regular on-disk attributes.
	 */
	memset(&context, 0, sizeof(context));
	context.dp = XFS_I(inode);
	context.resynch = 1;
	context.buffer = size ? data : NULL;
	context.bufsize = size;
	context.firstu = context.bufsize;
	context.put_listent = xfs_xattr_put_listent;

	error = xfs_attr_list(&context);
	if (error)
		return error;
	if (context.count < 0)
		return -ERANGE;

	return context.count;
}
