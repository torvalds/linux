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
#include "xfs_inode.h"
#include "xfs_attr.h"
#include "xfs_acl.h"
#include "xfs_da_btree.h"

#include <linux/posix_acl_xattr.h>


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

	error = xfs_attr_get(&args);
	if (error)
		return error;
	return args.valuelen;
}

static int
xfs_xattr_set(const struct xattr_handler *handler,
	      struct user_namespace *mnt_userns, struct dentry *unused,
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

	error = xfs_attr_set(&args);
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

const struct xattr_handler *xfs_xattr_handlers[] = {
	&xfs_xattr_user_handler,
	&xfs_xattr_trusted_handler,
	&xfs_xattr_security_handler,
#ifdef CONFIG_XFS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
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
	strncpy(offset, prefix, prefix_len);
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
