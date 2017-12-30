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
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
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
xfs_acl_from_disk(
	const struct xfs_acl	*aclp,
	int			len,
	int			max_entries)
{
	struct posix_acl_entry *acl_e;
	struct posix_acl *acl;
	const struct xfs_acl_entry *ace;
	unsigned int count, i;

	if (len < sizeof(*aclp))
		return ERR_PTR(-EFSCORRUPTED);
	count = be32_to_cpu(aclp->acl_cnt);
	if (count > max_entries || XFS_ACL_SIZE(count) != len)
		return ERR_PTR(-EFSCORRUPTED);

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
			acl_e->e_uid = xfs_uid_to_kuid(be32_to_cpu(ace->ae_id));
			break;
		case ACL_GROUP:
			acl_e->e_gid = xfs_gid_to_kgid(be32_to_cpu(ace->ae_id));
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
			ace->ae_id = cpu_to_be32(xfs_kuid_to_uid(acl_e->e_uid));
			break;
		case ACL_GROUP:
			ace->ae_id = cpu_to_be32(xfs_kgid_to_gid(acl_e->e_gid));
			break;
		default:
			ace->ae_id = cpu_to_be32(ACL_UNDEFINED_ID);
			break;
		}

		ace->ae_perm = cpu_to_be16(acl_e->e_perm);
	}
}

struct posix_acl *
xfs_get_acl(struct inode *inode, int type)
{
	struct xfs_inode *ip = XFS_I(inode);
	struct posix_acl *acl = NULL;
	struct xfs_acl *xfs_acl;
	unsigned char *ea_name;
	int error;
	int len;

	trace_xfs_get_acl(ip);

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
	len = XFS_ACL_MAX_SIZE(ip->i_mount);
	xfs_acl = kmem_zalloc_large(len, KM_SLEEP);
	if (!xfs_acl)
		return ERR_PTR(-ENOMEM);

	error = xfs_attr_get(ip, ea_name, (unsigned char *)xfs_acl,
							&len, ATTR_ROOT);
	if (error) {
		/*
		 * If the attribute doesn't exist make sure we have a negative
		 * cache entry, for any other error assume it is transient.
		 */
		if (error != -ENOATTR)
			acl = ERR_PTR(error);
	} else  {
		acl = xfs_acl_from_disk(xfs_acl, len,
					XFS_ACL_MAX_ENTRIES(ip->i_mount));
	}
	kmem_free(xfs_acl);
	return acl;
}

int
__xfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	struct xfs_inode *ip = XFS_I(inode);
	unsigned char *ea_name;
	int error;

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
		int len = XFS_ACL_MAX_SIZE(ip->i_mount);

		xfs_acl = kmem_zalloc_large(len, KM_SLEEP);
		if (!xfs_acl)
			return -ENOMEM;

		xfs_acl_to_disk(xfs_acl, acl);

		/* subtract away the unused acl entries */
		len -= sizeof(struct xfs_acl_entry) *
			 (XFS_ACL_MAX_ENTRIES(ip->i_mount) - acl->a_count);

		error = xfs_attr_set(ip, ea_name, (unsigned char *)xfs_acl,
				len, ATTR_ROOT);

		kmem_free(xfs_acl);
	} else {
		/*
		 * A NULL ACL argument means we want to remove the ACL.
		 */
		error = xfs_attr_remove(ip, ea_name, ATTR_ROOT);

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

static int
xfs_set_mode(struct inode *inode, umode_t mode)
{
	int error = 0;

	if (mode != inode->i_mode) {
		struct iattr iattr;

		iattr.ia_valid = ATTR_MODE | ATTR_CTIME;
		iattr.ia_mode = mode;
		iattr.ia_ctime = current_time(inode);

		error = xfs_setattr_nonsize(XFS_I(inode), &iattr, XFS_ATTR_NOACL);
	}

	return error;
}

int
xfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	umode_t mode;
	bool set_mode = false;
	int error = 0;

	if (!acl)
		goto set_acl;

	error = -E2BIG;
	if (acl->a_count > XFS_ACL_MAX_ENTRIES(XFS_M(inode->i_sb)))
		return error;

	if (type == ACL_TYPE_ACCESS) {
		error = posix_acl_update_mode(inode, &mode, &acl);
		if (error)
			return error;
		set_mode = true;
	}

 set_acl:
	error =  __xfs_set_acl(inode, acl, type);
	if (error)
		return error;

	/*
	 * We set the mode after successfully updating the ACL xattr because the
	 * xattr update can fail at ENOSPC and we don't want to change the mode
	 * if the ACL update hasn't been applied.
	 */
	if (set_mode)
		error = xfs_set_mode(inode, mode);

	return error;
}
