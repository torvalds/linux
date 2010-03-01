/*
 * Copyright (C) 2008 Christoph Hellwig.
 * Portions Copyright (C) 2000-2008 Silicon Graphics, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "xfs.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_acl.h"
#include "xfs_vnodeops.h"

#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>


static int
xfs_xattr_get(struct dentry *dentry, const char *name,
		void *value, size_t size, int xflags)
{
	struct xfs_inode *ip = XFS_I(dentry->d_inode);
	int error, asize = size;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (!size) {
		xflags |= ATTR_KERNOVAL;
		value = NULL;
	}

	error = -xfs_attr_get(ip, name, value, &asize, xflags);
	if (error)
		return error;
	return asize;
}

static int
xfs_xattr_set(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags, int xflags)
{
	struct xfs_inode *ip = XFS_I(dentry->d_inode);

	if (strcmp(name, "") == 0)
		return -EINVAL;

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (flags & XATTR_CREATE)
		xflags |= ATTR_CREATE;
	if (flags & XATTR_REPLACE)
		xflags |= ATTR_REPLACE;

	if (!value)
		return -xfs_attr_remove(ip, name, xflags);
	return -xfs_attr_set(ip, name, (void *)value, size, xflags);
}

static struct xattr_handler xfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.flags	= 0, /* no flags implies user namespace */
	.get	= xfs_xattr_get,
	.set	= xfs_xattr_set,
};

static struct xattr_handler xfs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.flags	= ATTR_ROOT,
	.get	= xfs_xattr_get,
	.set	= xfs_xattr_set,
};

static struct xattr_handler xfs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.flags	= ATTR_SECURE,
	.get	= xfs_xattr_get,
	.set	= xfs_xattr_set,
};

struct xattr_handler *xfs_xattr_handlers[] = {
	&xfs_xattr_user_handler,
	&xfs_xattr_trusted_handler,
	&xfs_xattr_security_handler,
#ifdef CONFIG_XFS_POSIX_ACL
	&xfs_xattr_acl_access_handler,
	&xfs_xattr_acl_default_handler,
#endif
	NULL
};

static unsigned int xfs_xattr_prefix_len(int flags)
{
	if (flags & XFS_ATTR_SECURE)
		return sizeof("security");
	else if (flags & XFS_ATTR_ROOT)
		return sizeof("trusted");
	else
		return sizeof("user");
}

static const char *xfs_xattr_prefix(int flags)
{
	if (flags & XFS_ATTR_SECURE)
		return xfs_xattr_security_handler.prefix;
	else if (flags & XFS_ATTR_ROOT)
		return xfs_xattr_trusted_handler.prefix;
	else
		return xfs_xattr_user_handler.prefix;
}

static int
xfs_xattr_put_listent(struct xfs_attr_list_context *context, int flags,
		char *name, int namelen, int valuelen, char *value)
{
	unsigned int prefix_len = xfs_xattr_prefix_len(flags);
	char *offset;
	int arraytop;

	ASSERT(context->count >= 0);

	/*
	 * Only show root namespace entries if we are actually allowed to
	 * see them.
	 */
	if ((flags & XFS_ATTR_ROOT) && !capable(CAP_SYS_ADMIN))
		return 0;

	arraytop = context->count + prefix_len + namelen + 1;
	if (arraytop > context->firstu) {
		context->count = -1;	/* insufficient space */
		return 1;
	}
	offset = (char *)context->alist + context->count;
	strncpy(offset, xfs_xattr_prefix(flags), prefix_len);
	offset += prefix_len;
	strncpy(offset, name, namelen);			/* real name */
	offset += namelen;
	*offset = '\0';
	context->count += prefix_len + namelen + 1;
	return 0;
}

static int
xfs_xattr_put_listent_sizes(struct xfs_attr_list_context *context, int flags,
		char *name, int namelen, int valuelen, char *value)
{
	context->count += xfs_xattr_prefix_len(flags) + namelen + 1;
	return 0;
}

static int
list_one_attr(const char *name, const size_t len, void *data,
		size_t size, ssize_t *result)
{
	char *p = data + *result;

	*result += len;
	if (!size)
		return 0;
	if (*result > size)
		return -ERANGE;

	strcpy(p, name);
	return 0;
}

ssize_t
xfs_vn_listxattr(struct dentry *dentry, char *data, size_t size)
{
	struct xfs_attr_list_context context;
	struct attrlist_cursor_kern cursor = { 0 };
	struct inode		*inode = dentry->d_inode;
	int			error;

	/*
	 * First read the regular on-disk attributes.
	 */
	memset(&context, 0, sizeof(context));
	context.dp = XFS_I(inode);
	context.cursor = &cursor;
	context.resynch = 1;
	context.alist = data;
	context.bufsize = size;
	context.firstu = context.bufsize;

	if (size)
		context.put_listent = xfs_xattr_put_listent;
	else
		context.put_listent = xfs_xattr_put_listent_sizes;

	xfs_attr_list_int(&context);
	if (context.count < 0)
		return -ERANGE;

	/*
	 * Then add the two synthetic ACL attributes.
	 */
	if (posix_acl_access_exists(inode)) {
		error = list_one_attr(POSIX_ACL_XATTR_ACCESS,
				strlen(POSIX_ACL_XATTR_ACCESS) + 1,
				data, size, &context.count);
		if (error)
			return error;
	}

	if (posix_acl_default_exists(inode)) {
		error = list_one_attr(POSIX_ACL_XATTR_DEFAULT,
				strlen(POSIX_ACL_XATTR_DEFAULT) + 1,
				data, size, &context.count);
		if (error)
			return error;
	}

	return context.count;
}
