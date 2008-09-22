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


/*
 * ACL handling.  Should eventually be moved into xfs_acl.c
 */

static int
xfs_decode_acl(const char *name)
{
	if (strcmp(name, "posix_acl_access") == 0)
		return _ACL_TYPE_ACCESS;
	else if (strcmp(name, "posix_acl_default") == 0)
		return _ACL_TYPE_DEFAULT;
	return -EINVAL;
}

/*
 * Get system extended attributes which at the moment only
 * includes Posix ACLs.
 */
static int
xfs_xattr_system_get(struct inode *inode, const char *name,
		void *buffer, size_t size)
{
	int acl;

	acl = xfs_decode_acl(name);
	if (acl < 0)
		return acl;

	return xfs_acl_vget(inode, buffer, size, acl);
}

static int
xfs_xattr_system_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	int acl;

	acl = xfs_decode_acl(name);
	if (acl < 0)
		return acl;
	if (flags & XATTR_CREATE)
		return -EINVAL;

	if (!value)
		return xfs_acl_vremove(inode, acl);

	return xfs_acl_vset(inode, (void *)value, size, acl);
}

static struct xattr_handler xfs_xattr_system_handler = {
	.prefix	= XATTR_SYSTEM_PREFIX,
	.get	= xfs_xattr_system_get,
	.set	= xfs_xattr_system_set,
};


/*
 * Real xattr handling.  The only difference between the namespaces is
 * a flag passed to the low-level attr code.
 */

static int
__xfs_xattr_get(struct inode *inode, const char *name,
		void *value, size_t size, int xflags)
{
	struct xfs_inode *ip = XFS_I(inode);
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
__xfs_xattr_set(struct inode *inode, const char *name, const void *value,
		size_t size, int flags, int xflags)
{
	struct xfs_inode *ip = XFS_I(inode);

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

static int
xfs_xattr_user_get(struct inode *inode, const char *name,
		void *value, size_t size)
{
	return __xfs_xattr_get(inode, name, value, size, 0);
}

static int
xfs_xattr_user_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	return __xfs_xattr_set(inode, name, value, size, flags, 0);
}

static struct xattr_handler xfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= xfs_xattr_user_get,
	.set	= xfs_xattr_user_set,
};


static int
xfs_xattr_trusted_get(struct inode *inode, const char *name,
		void *value, size_t size)
{
	return __xfs_xattr_get(inode, name, value, size, ATTR_ROOT);
}

static int
xfs_xattr_trusted_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	return __xfs_xattr_set(inode, name, value, size, flags, ATTR_ROOT);
}

static struct xattr_handler xfs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.get	= xfs_xattr_trusted_get,
	.set	= xfs_xattr_trusted_set,
};


static int
xfs_xattr_secure_get(struct inode *inode, const char *name,
		void *value, size_t size)
{
	return __xfs_xattr_get(inode, name, value, size, ATTR_SECURE);
}

static int
xfs_xattr_secure_set(struct inode *inode, const char *name,
		const void *value, size_t size, int flags)
{
	return __xfs_xattr_set(inode, name, value, size, flags, ATTR_SECURE);
}

static struct xattr_handler xfs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= xfs_xattr_secure_get,
	.set	= xfs_xattr_secure_set,
};


struct xattr_handler *xfs_xattr_handlers[] = {
	&xfs_xattr_user_handler,
	&xfs_xattr_trusted_handler,
	&xfs_xattr_security_handler,
	&xfs_xattr_system_handler,
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
	if (xfs_acl_vhasacl_access(inode)) {
		error = list_one_attr(POSIX_ACL_XATTR_ACCESS,
				strlen(POSIX_ACL_XATTR_ACCESS) + 1,
				data, size, &context.count);
		if (error)
			return error;
	}

	if (xfs_acl_vhasacl_default(inode)) {
		error = list_one_attr(POSIX_ACL_XATTR_DEFAULT,
				strlen(POSIX_ACL_XATTR_DEFAULT) + 1,
				data, size, &context.count);
		if (error)
			return error;
	}

	return context.count;
}
