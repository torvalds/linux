/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2016 Canonical Ltd. <seth.forshee@canonical.com>
 *
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
 */

#include "fuse_i.h"

#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>

static struct posix_acl *__fuse_get_acl(struct fuse_conn *fc,
					struct mnt_idmap *idmap,
					struct inode *inode, int type, bool rcu)
{
	int size;
	const char *name;
	void *value = NULL;
	struct posix_acl *acl;

	if (rcu)
		return ERR_PTR(-ECHILD);

	if (fuse_is_bad(inode))
		return ERR_PTR(-EIO);

	if (fc->no_getxattr)
		return NULL;

	if (type == ACL_TYPE_ACCESS)
		name = XATTR_NAME_POSIX_ACL_ACCESS;
	else if (type == ACL_TYPE_DEFAULT)
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
	else
		return ERR_PTR(-EOPNOTSUPP);

	value = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!value)
		return ERR_PTR(-ENOMEM);
	size = fuse_getxattr(inode, name, value, PAGE_SIZE);
	if (size > 0)
		acl = posix_acl_from_xattr(fc->user_ns, value, size);
	else if ((size == 0) || (size == -ENODATA) ||
		 (size == -EOPNOTSUPP && fc->no_getxattr))
		acl = NULL;
	else if (size == -ERANGE)
		acl = ERR_PTR(-E2BIG);
	else
		acl = ERR_PTR(size);

	kfree(value);
	return acl;
}

static inline bool fuse_no_acl(const struct fuse_conn *fc,
			       const struct inode *inode)
{
	/*
	 * Refuse interacting with POSIX ACLs for daemons that
	 * don't support FUSE_POSIX_ACL and are not mounted on
	 * the host to retain backwards compatibility.
	 */
	return !fc->posix_acl && (i_user_ns(inode) != &init_user_ns);
}

struct posix_acl *fuse_get_acl(struct mnt_idmap *idmap,
			       struct dentry *dentry, int type)
{
	struct inode *inode = d_inode(dentry);
	struct fuse_conn *fc = get_fuse_conn(inode);

	if (fuse_no_acl(fc, inode))
		return ERR_PTR(-EOPNOTSUPP);

	return __fuse_get_acl(fc, idmap, inode, type, false);
}

struct posix_acl *fuse_get_inode_acl(struct inode *inode, int type, bool rcu)
{
	struct fuse_conn *fc = get_fuse_conn(inode);

	/*
	 * FUSE daemons before FUSE_POSIX_ACL was introduced could get and set
	 * POSIX ACLs without them being used for permission checking by the
	 * vfs. Retain that behavior for backwards compatibility as there are
	 * filesystems that do all permission checking for acls in the daemon
	 * and not in the kernel.
	 */
	if (!fc->posix_acl)
		return NULL;

	return __fuse_get_acl(fc, &nop_mnt_idmap, inode, type, rcu);
}

int fuse_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct posix_acl *acl, int type)
{
	struct inode *inode = d_inode(dentry);
	struct fuse_conn *fc = get_fuse_conn(inode);
	const char *name;
	int ret;

	if (fuse_is_bad(inode))
		return -EIO;

	if (fc->no_setxattr || fuse_no_acl(fc, inode))
		return -EOPNOTSUPP;

	if (type == ACL_TYPE_ACCESS)
		name = XATTR_NAME_POSIX_ACL_ACCESS;
	else if (type == ACL_TYPE_DEFAULT)
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
	else
		return -EINVAL;

	if (acl) {
		unsigned int extra_flags = 0;
		/*
		 * Fuse userspace is responsible for updating access
		 * permissions in the inode, if needed. fuse_setxattr
		 * invalidates the inode attributes, which will force
		 * them to be refreshed the next time they are used,
		 * and it also updates i_ctime.
		 */
		size_t size = posix_acl_xattr_size(acl->a_count);
		void *value;

		if (size > PAGE_SIZE)
			return -E2BIG;

		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;

		ret = posix_acl_to_xattr(fc->user_ns, acl, value, size);
		if (ret < 0) {
			kfree(value);
			return ret;
		}

		/*
		 * Fuse daemons without FUSE_POSIX_ACL never changed the passed
		 * through POSIX ACLs. Such daemons don't expect setgid bits to
		 * be stripped.
		 */
		if (fc->posix_acl &&
		    !vfsgid_in_group_p(i_gid_into_vfsgid(&nop_mnt_idmap, inode)) &&
		    !capable_wrt_inode_uidgid(&nop_mnt_idmap, inode, CAP_FSETID))
			extra_flags |= FUSE_SETXATTR_ACL_KILL_SGID;

		ret = fuse_setxattr(inode, name, value, size, 0, extra_flags);
		kfree(value);
	} else {
		ret = fuse_removexattr(inode, name);
	}

	if (fc->posix_acl) {
		/*
		 * Fuse daemons without FUSE_POSIX_ACL never cached POSIX ACLs
		 * and didn't invalidate attributes. Retain that behavior.
		 */
		forget_all_cached_acls(inode);
		fuse_invalidate_attr(inode);
	}

	return ret;
}
