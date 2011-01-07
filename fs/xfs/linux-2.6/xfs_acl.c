/*
 * Copyright (c) 2008, Christoph Hellwig
 * All Rights Reserved.
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
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_bmap_btree.h"
#include "xfs_inode.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>


/*
 * Locking scheme:
 *  - all ACL updates are protected by inode->i_mutex, which is taken before
 *    calling into this file.
 */

STATIC struct posix_acl *
xfs_acl_from_disk(struct xfs_acl *aclp)
{
	struct posix_acl_entry *acl_e;
	struct posix_acl *acl;
	struct xfs_acl_entry *ace;
	int count, i;

	count = be32_to_cpu(aclp->acl_cnt);

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
		case ACL_GROUP:
			acl_e->e_id = be32_to_cpu(ace->ae_id);
			break;
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			acl_e->e_id = ACL_UNDEFINED_ID;
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
		ace->ae_id = cpu_to_be32(acl_e->e_id);
		ace->ae_perm = cpu_to_be16(acl_e->e_perm);
	}
}

struct posix_acl *
xfs_get_acl(struct inode *inode, int type)
{
	struct xfs_inode *ip = XFS_I(inode);
	struct posix_acl *acl;
	struct xfs_acl *xfs_acl;
	int len = sizeof(struct xfs_acl);
	unsigned char *ea_name;
	int error;

	acl = get_cached_acl(inode, type);
	if (acl != ACL_NOT_CACHED)
		return acl;

	switch (type) {
	case ACL_TYPE_ACCESS:
		ea_name = SGI_ACL_FILE;
		break;
	case ACL_TYPE_DEFAULT:
		ea_name = SGI_ACL_DEFAULT;
		break;
	default:
		BUG();
	}

	/*
	 * If we have a cached ACLs value just return it, not need to
	 * go out to the disk.
	 */

	xfs_acl = kzalloc(sizeof(struct xfs_acl), GFP_KERNEL);
	if (!xfs_acl)
		return ERR_PTR(-ENOMEM);

	error = -xfs_attr_get(ip, ea_name, (unsigned char *)xfs_acl,
							&len, ATTR_ROOT);
	if (error) {
		/*
		 * If the attribute doesn't exist make sure we have a negative
		 * cache entry, for any other error assume it is transient and
		 * leave the cache entry as ACL_NOT_CACHED.
		 */
		if (error == -ENOATTR) {
			acl = NULL;
			goto out_update_cache;
		}
		goto out;
	}

	acl = xfs_acl_from_disk(xfs_acl);
	if (IS_ERR(acl))
		goto out;

 out_update_cache:
	set_cached_acl(inode, type, acl);
 out:
	kfree(xfs_acl);
	return acl;
}

STATIC int
xfs_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	struct xfs_inode *ip = XFS_I(inode);
	unsigned char *ea_name;
	int error;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	switch (type) {
	case ACL_TYPE_ACCESS:
		ea_name = SGI_ACL_FILE;
		break;
	case ACL_TYPE_DEFAULT:
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		ea_name = SGI_ACL_DEFAULT;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		struct xfs_acl *xfs_acl;
		int len;

		xfs_acl = kzalloc(sizeof(struct xfs_acl), GFP_KERNEL);
		if (!xfs_acl)
			return -ENOMEM;

		xfs_acl_to_disk(xfs_acl, acl);
		len = sizeof(struct xfs_acl) -
			(sizeof(struct xfs_acl_entry) *
			 (XFS_ACL_MAX_ENTRIES - acl->a_count));

		error = -xfs_attr_set(ip, ea_name, (unsigned char *)xfs_acl,
				len, ATTR_ROOT);

		kfree(xfs_acl);
	} else {
		/*
		 * A NULL ACL argument means we want to remove the ACL.
		 */
		error = -xfs_attr_remove(ip, ea_name, ATTR_ROOT);

		/*
		 * If the attribute didn't exist to start with that's fine.
		 */
		if (error == -ENOATTR)
			error = 0;
	}

	if (!error)
		set_cached_acl(inode, type, acl);
	return error;
}

int
xfs_check_acl(struct inode *inode, int mask, unsigned int flags)
{
	struct xfs_inode *ip;
	struct posix_acl *acl;
	int error = -EAGAIN;

	ip = XFS_I(inode);
	trace_xfs_check_acl(ip);

	/*
	 * If there is no attribute fork no ACL exists on this inode and
	 * we can skip the whole exercise.
	 */
	if (!XFS_IFORK_Q(ip))
		return -EAGAIN;

	if (flags & IPERM_FLAG_RCU) {
		if (!negative_cached_acl(inode, ACL_TYPE_ACCESS))
			return -ECHILD;
		return -EAGAIN;
	}

	acl = xfs_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
		error = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
	}

	return error;
}

static int
xfs_set_mode(struct inode *inode, mode_t mode)
{
	int error = 0;

	if (mode != inode->i_mode) {
		struct iattr iattr;

		iattr.ia_valid = ATTR_MODE | ATTR_CTIME;
		iattr.ia_mode = mode;
		iattr.ia_ctime = current_fs_time(inode->i_sb);

		error = -xfs_setattr(XFS_I(inode), &iattr, XFS_ATTR_NOACL);
	}

	return error;
}

static int
xfs_acl_exists(struct inode *inode, unsigned char *name)
{
	int len = sizeof(struct xfs_acl);

	return (xfs_attr_get(XFS_I(inode), name, NULL, &len,
			    ATTR_ROOT|ATTR_KERNOVAL) == 0);
}

int
posix_acl_access_exists(struct inode *inode)
{
	return xfs_acl_exists(inode, SGI_ACL_FILE);
}

int
posix_acl_default_exists(struct inode *inode)
{
	if (!S_ISDIR(inode->i_mode))
		return 0;
	return xfs_acl_exists(inode, SGI_ACL_DEFAULT);
}

/*
 * No need for i_mutex because the inode is not yet exposed to the VFS.
 */
int
xfs_inherit_acl(struct inode *inode, struct posix_acl *default_acl)
{
	struct posix_acl *clone;
	mode_t mode;
	int error = 0, inherit = 0;

	if (S_ISDIR(inode->i_mode)) {
		error = xfs_set_acl(inode, ACL_TYPE_DEFAULT, default_acl);
		if (error)
			return error;
	}

	clone = posix_acl_clone(default_acl, GFP_KERNEL);
	if (!clone)
		return -ENOMEM;

	mode = inode->i_mode;
	error = posix_acl_create_masq(clone, &mode);
	if (error < 0)
		goto out_release_clone;

	/*
	 * If posix_acl_create_masq returns a positive value we need to
	 * inherit a permission that can't be represented using the Unix
	 * mode bits and we actually need to set an ACL.
	 */
	if (error > 0)
		inherit = 1;

	error = xfs_set_mode(inode, mode);
	if (error)
		goto out_release_clone;

	if (inherit)
		error = xfs_set_acl(inode, ACL_TYPE_ACCESS, clone);

 out_release_clone:
	posix_acl_release(clone);
	return error;
}

int
xfs_acl_chmod(struct inode *inode)
{
	struct posix_acl *acl, *clone;
	int error;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	acl = xfs_get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return PTR_ERR(acl);

	clone = posix_acl_clone(acl, GFP_KERNEL);
	posix_acl_release(acl);
	if (!clone)
		return -ENOMEM;

	error = posix_acl_chmod_masq(clone, inode->i_mode);
	if (!error)
		error = xfs_set_acl(inode, ACL_TYPE_ACCESS, clone);

	posix_acl_release(clone);
	return error;
}

static int
xfs_xattr_acl_get(struct dentry *dentry, const char *name,
		void *value, size_t size, int type)
{
	struct posix_acl *acl;
	int error;

	acl = xfs_get_acl(dentry->d_inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;

	error = posix_acl_to_xattr(acl, value, size);
	posix_acl_release(acl);

	return error;
}

static int
xfs_xattr_acl_set(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags, int type)
{
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl = NULL;
	int error = 0;

	if (flags & XATTR_CREATE)
		return -EINVAL;
	if (type == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode))
		return value ? -EACCES : 0;
	if ((current_fsuid() != inode->i_uid) && !capable(CAP_FOWNER))
		return -EPERM;

	if (!value)
		goto set_acl;

	acl = posix_acl_from_xattr(value, size);
	if (!acl) {
		/*
		 * acl_set_file(3) may request that we set default ACLs with
		 * zero length -- defend (gracefully) against that here.
		 */
		goto out;
	}
	if (IS_ERR(acl)) {
		error = PTR_ERR(acl);
		goto out;
	}

	error = posix_acl_valid(acl);
	if (error)
		goto out_release;

	error = -EINVAL;
	if (acl->a_count > XFS_ACL_MAX_ENTRIES)
		goto out_release;

	if (type == ACL_TYPE_ACCESS) {
		mode_t mode = inode->i_mode;
		error = posix_acl_equiv_mode(acl, &mode);

		if (error <= 0) {
			posix_acl_release(acl);
			acl = NULL;

			if (error < 0)
				return error;
		}

		error = xfs_set_mode(inode, mode);
		if (error)
			goto out_release;
	}

 set_acl:
	error = xfs_set_acl(inode, type, acl);
 out_release:
	posix_acl_release(acl);
 out:
	return error;
}

const struct xattr_handler xfs_xattr_acl_access_handler = {
	.prefix	= POSIX_ACL_XATTR_ACCESS,
	.flags	= ACL_TYPE_ACCESS,
	.get	= xfs_xattr_acl_get,
	.set	= xfs_xattr_acl_set,
};

const struct xattr_handler xfs_xattr_acl_default_handler = {
	.prefix	= POSIX_ACL_XATTR_DEFAULT,
	.flags	= ACL_TYPE_DEFAULT,
	.get	= xfs_xattr_acl_get,
	.set	= xfs_xattr_acl_set,
};
