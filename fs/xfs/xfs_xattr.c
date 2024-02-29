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

#include <linux/posix_acl_xattr.h>

/*
 * Get permission to use log-assisted atomic exchange of file extents.
 *
 * Callers must not be running any transactions or hold any inode locks, and
 * they must release the permission by calling xlog_drop_incompat_feat
 * when they're done.
 */
static inline int
xfs_attr_grab_log_assist(
	struct xfs_mount	*mp)
{
	int			error = 0;

	/*
	 * Protect ourselves from an idle log clearing the logged xattrs log
	 * incompat feature bit.
	 */
	xlog_use_incompat_feat(mp->m_log);

	/*
	 * If log-assisted xattrs are already enabled, the caller can use the
	 * log assisted swap functions with the log-incompat reference we got.
	 */
	if (xfs_sb_version_haslogxattrs(&mp->m_sb))
		return 0;

	/*
	 * Check if the filesystem featureset is new enough to set this log
	 * incompat feature bit.  Strictly speaking, the minimum requirement is
	 * a V5 filesystem for the superblock field, but we'll require rmap
	 * or reflink to avoid having to deal with really old kernels.
	 */
	if (!xfs_has_reflink(mp) && !xfs_has_rmapbt(mp)) {
		error = -EOPNOTSUPP;
		goto drop_incompat;
	}

	/* Enable log-assisted xattrs. */
	error = xfs_add_incompat_log_feature(mp,
			XFS_SB_FEAT_INCOMPAT_LOG_XATTRS);
	if (error)
		goto drop_incompat;

	xfs_warn_mount(mp, XFS_OPSTATE_WARNED_LARP,
 "EXPERIMENTAL logged extended attributes feature in use. Use at your own risk!");

	return 0;
drop_incompat:
	xlog_drop_incompat_feat(mp->m_log);
	return error;
}

static inline void
xfs_attr_rele_log_assist(
	struct xfs_mount	*mp)
{
	xlog_drop_incompat_feat(mp->m_log);
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
 * prior to calling libxfs.
 */
int
xfs_attr_change(
	struct xfs_da_args	*args)
{
	struct xfs_mount	*mp = args->dp->i_mount;
	bool			use_logging = false;
	int			error;

	ASSERT(!(args->op_flags & XFS_DA_OP_LOGGED));

	if (xfs_attr_want_log_assist(mp)) {
		error = xfs_attr_grab_log_assist(mp);
		if (error)
			return error;

		args->op_flags |= XFS_DA_OP_LOGGED;
		use_logging = true;
	}

	error = xfs_attr_set(args);

	if (use_logging)
		xfs_attr_rele_log_assist(mp);
	return error;
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

static int
xfs_xattr_set(const struct xattr_handler *handler,
	      struct mnt_idmap *idmap, struct dentry *unused,
	      struct inode *inode, const char *name, const void *value,
	      size_t size, int flags)
{
	struct xfs_da_args	args = {
		.dp		= XFS_I(inode),
		.attr_filter	= handler->flags,
		.attr_flags	= flags,
		.name		= name,
		.namelen	= strlen(name),
		.value		= (void *)value,
		.valuelen	= size,
	};
	int			error;

	error = xfs_attr_change(&args);
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
	int		valuelen)
{
	char *prefix;
	int prefix_len;

	ASSERT(context->count >= 0);

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
