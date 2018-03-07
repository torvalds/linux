// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"
#include <linux/posix_acl_xattr.h>
#include <linux/fs_struct.h>

struct posix_acl *orangefs_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	int ret;
	char *key = NULL, *value = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		key = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		key = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		gossip_err("orangefs_get_acl: bogus value of type %d\n", type);
		return ERR_PTR(-EINVAL);
	}
	/*
	 * Rather than incurring a network call just to determine the exact
	 * length of the attribute, I just allocate a max length to save on
	 * the network call. Conceivably, we could pass NULL to
	 * orangefs_inode_getxattr() to probe the length of the value, but
	 * I don't do that for now.
	 */
	value = kmalloc(ORANGEFS_MAX_XATTR_VALUELEN, GFP_KERNEL);
	if (!value)
		return ERR_PTR(-ENOMEM);

	gossip_debug(GOSSIP_ACL_DEBUG,
		     "inode %pU, key %s, type %d\n",
		     get_khandle_from_ino(inode),
		     key,
		     type);
	ret = orangefs_inode_getxattr(inode, key, value,
				      ORANGEFS_MAX_XATTR_VALUELEN);
	/* if the key exists, convert it to an in-memory rep */
	if (ret > 0) {
		acl = posix_acl_from_xattr(&init_user_ns, value, ret);
	} else if (ret == -ENODATA || ret == -ENOSYS) {
		acl = NULL;
	} else {
		gossip_err("inode %pU retrieving acl's failed with error %d\n",
			   get_khandle_from_ino(inode),
			   ret);
		acl = ERR_PTR(ret);
	}
	/* kfree(NULL) is safe, so don't worry if value ever got used */
	kfree(value);
	return acl;
}

static int __orangefs_set_acl(struct inode *inode, struct posix_acl *acl,
			      int type)
{
	int error = 0;
	void *value = NULL;
	size_t size = 0;
	const char *name = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		gossip_err("%s: invalid type %d!\n", __func__, type);
		return -EINVAL;
	}

	gossip_debug(GOSSIP_ACL_DEBUG,
		     "%s: inode %pU, key %s type %d\n",
		     __func__, get_khandle_from_ino(inode),
		     name,
		     type);

	if (acl) {
		size = posix_acl_xattr_size(acl->a_count);
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;

		error = posix_acl_to_xattr(&init_user_ns, acl, value, size);
		if (error < 0)
			goto out;
	}

	gossip_debug(GOSSIP_ACL_DEBUG,
		     "%s: name %s, value %p, size %zd, acl %p\n",
		     __func__, name, value, size, acl);
	/*
	 * Go ahead and set the extended attribute now. NOTE: Suppose acl
	 * was NULL, then value will be NULL and size will be 0 and that
	 * will xlate to a removexattr. However, we don't want removexattr
	 * complain if attributes does not exist.
	 */
	error = orangefs_inode_setxattr(inode, name, value, size, 0);

out:
	kfree(value);
	if (!error)
		set_cached_acl(inode, type, acl);
	return error;
}

int orangefs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int error;
	struct iattr iattr;
	int rc;

	if (type == ACL_TYPE_ACCESS && acl) {
		/*
		 * posix_acl_update_mode checks to see if the permissions
		 * described by the ACL can be encoded into the
		 * object's mode. If so, it sets "acl" to NULL
		 * and "mode" to the new desired value. It is up to
		 * us to propagate the new mode back to the server...
		 */
		error = posix_acl_update_mode(inode, &iattr.ia_mode, &acl);
		if (error) {
			gossip_err("%s: posix_acl_update_mode err: %d\n",
				   __func__,
				   error);
			return error;
		}

		if (acl) {
			rc = __orangefs_set_acl(inode, acl, type);
		} else {
			iattr.ia_valid = ATTR_MODE;
			rc = orangefs_inode_setattr(inode, &iattr);
		}

		return rc;

	} else {
		return -EINVAL;
	}
}

int orangefs_init_acl(struct inode *inode, struct inode *dir)
{
	struct posix_acl *default_acl, *acl;
	umode_t mode = inode->i_mode;
	struct iattr iattr;
	int error = 0;

	error = posix_acl_create(dir, &mode, &default_acl, &acl);
	if (error)
		return error;

	if (default_acl) {
		error = __orangefs_set_acl(inode, default_acl,
					   ACL_TYPE_DEFAULT);
		posix_acl_release(default_acl);
	}

	if (acl) {
		if (!error)
			error = __orangefs_set_acl(inode, acl, ACL_TYPE_ACCESS);
		posix_acl_release(acl);
	}

	/* If mode of the inode was changed, then do a forcible ->setattr */
	if (mode != inode->i_mode) {
		memset(&iattr, 0, sizeof iattr);
		inode->i_mode = mode;
		iattr.ia_mode = mode;
		iattr.ia_valid |= ATTR_MODE;
		orangefs_inode_setattr(inode, &iattr);
	}

	return error;
}
