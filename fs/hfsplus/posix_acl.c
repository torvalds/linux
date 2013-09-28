/*
 * linux/fs/hfsplus/posix_acl.c
 *
 * Vyacheslav Dubeyko <slava@dubeyko.com>
 *
 * Handler for Posix Access Control Lists (ACLs) support.
 */

#include "hfsplus_fs.h"
#include "xattr.h"
#include "acl.h"

struct posix_acl *hfsplus_get_posix_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	char *xattr_name;
	char *value = NULL;
	ssize_t size;

	acl = get_cached_acl(inode, type);
	if (acl != ACL_NOT_CACHED)
		return acl;

	switch (type) {
	case ACL_TYPE_ACCESS:
		xattr_name = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		xattr_name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	size = __hfsplus_getxattr(inode, xattr_name, NULL, 0);

	if (size > 0) {
		value = (char *)hfsplus_alloc_attr_entry();
		if (unlikely(!value))
			return ERR_PTR(-ENOMEM);
		size = __hfsplus_getxattr(inode, xattr_name, value, size);
	}

	if (size > 0)
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
	else if (size == -ENODATA)
		acl = NULL;
	else
		acl = ERR_PTR(size);

	hfsplus_destroy_attr_entry((hfsplus_attr_entry *)value);

	if (!IS_ERR(acl))
		set_cached_acl(inode, type, acl);

	return acl;
}

static int hfsplus_set_posix_acl(struct inode *inode,
					int type,
					struct posix_acl *acl)
{
	int err;
	char *xattr_name;
	size_t size = 0;
	char *value = NULL;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	switch (type) {
	case ACL_TYPE_ACCESS:
		xattr_name = POSIX_ACL_XATTR_ACCESS;
		if (acl) {
			err = posix_acl_equiv_mode(acl, &inode->i_mode);
			if (err < 0)
				return err;
		}
		err = 0;
		break;

	case ACL_TYPE_DEFAULT:
		xattr_name = POSIX_ACL_XATTR_DEFAULT;
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		break;

	default:
		return -EINVAL;
	}

	if (acl) {
		size = posix_acl_xattr_size(acl->a_count);
		if (unlikely(size > HFSPLUS_MAX_INLINE_DATA_SIZE))
			return -ENOMEM;
		value = (char *)hfsplus_alloc_attr_entry();
		if (unlikely(!value))
			return -ENOMEM;
		err = posix_acl_to_xattr(&init_user_ns, acl, value, size);
		if (unlikely(err < 0))
			goto end_set_acl;
	}

	err = __hfsplus_setxattr(inode, xattr_name, value, size, 0);

end_set_acl:
	hfsplus_destroy_attr_entry((hfsplus_attr_entry *)value);

	if (!err)
		set_cached_acl(inode, type, acl);

	return err;
}

int hfsplus_init_posix_acl(struct inode *inode, struct inode *dir)
{
	int err = 0;
	struct posix_acl *acl = NULL;

	hfs_dbg(ACL_MOD,
		"[%s]: ino %lu, dir->ino %lu\n",
		__func__, inode->i_ino, dir->i_ino);

	if (S_ISLNK(inode->i_mode))
		return 0;

	acl = hfsplus_get_posix_acl(dir, ACL_TYPE_DEFAULT);
	if (IS_ERR(acl))
		return PTR_ERR(acl);

	if (acl) {
		if (S_ISDIR(inode->i_mode)) {
			err = hfsplus_set_posix_acl(inode,
							ACL_TYPE_DEFAULT,
							acl);
			if (unlikely(err))
				goto init_acl_cleanup;
		}

		err = posix_acl_create(&acl, GFP_NOFS, &inode->i_mode);
		if (unlikely(err < 0))
			return err;

		if (err > 0)
			err = hfsplus_set_posix_acl(inode,
							ACL_TYPE_ACCESS,
							acl);
	} else
		inode->i_mode &= ~current_umask();

init_acl_cleanup:
	posix_acl_release(acl);
	return err;
}

int hfsplus_posix_acl_chmod(struct inode *inode)
{
	int err;
	struct posix_acl *acl;

	hfs_dbg(ACL_MOD, "[%s]: ino %lu\n", __func__, inode->i_ino);

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	acl = hfsplus_get_posix_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return PTR_ERR(acl);

	err = posix_acl_chmod(&acl, GFP_KERNEL, inode->i_mode);
	if (unlikely(err))
		return err;

	err = hfsplus_set_posix_acl(inode, ACL_TYPE_ACCESS, acl);
	posix_acl_release(acl);
	return err;
}

static int hfsplus_xattr_get_posix_acl(struct dentry *dentry,
					const char *name,
					void *buffer,
					size_t size,
					int type)
{
	int err = 0;
	struct posix_acl *acl;

	hfs_dbg(ACL_MOD,
		"[%s]: ino %lu, buffer %p, size %zu, type %#x\n",
		__func__, dentry->d_inode->i_ino, buffer, size, type);

	if (strcmp(name, "") != 0)
		return -EINVAL;

	acl = hfsplus_get_posix_acl(dentry->d_inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;

	err = posix_acl_to_xattr(&init_user_ns, acl, buffer, size);
	posix_acl_release(acl);

	return err;
}

static int hfsplus_xattr_set_posix_acl(struct dentry *dentry,
					const char *name,
					const void *value,
					size_t size,
					int flags,
					int type)
{
	int err = 0;
	struct inode *inode = dentry->d_inode;
	struct posix_acl *acl = NULL;

	hfs_dbg(ACL_MOD,
		"[%s]: ino %lu, value %p, size %zu, flags %#x, type %#x\n",
		__func__, inode->i_ino, value, size, flags, type);

	if (strcmp(name, "") != 0)
		return -EINVAL;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (value) {
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
		else if (acl) {
			err = posix_acl_valid(acl);
			if (err)
				goto end_xattr_set_acl;
		}
	}

	err = hfsplus_set_posix_acl(inode, type, acl);

end_xattr_set_acl:
	posix_acl_release(acl);
	return err;
}

static size_t hfsplus_xattr_list_posix_acl(struct dentry *dentry,
						char *list,
						size_t list_size,
						const char *name,
						size_t name_len,
						int type)
{
	/*
	 * This method is not used.
	 * It is used hfsplus_listxattr() instead of generic_listxattr().
	 */
	return -EOPNOTSUPP;
}

const struct xattr_handler hfsplus_xattr_acl_access_handler = {
	.prefix	= POSIX_ACL_XATTR_ACCESS,
	.flags	= ACL_TYPE_ACCESS,
	.list	= hfsplus_xattr_list_posix_acl,
	.get	= hfsplus_xattr_get_posix_acl,
	.set	= hfsplus_xattr_set_posix_acl,
};

const struct xattr_handler hfsplus_xattr_acl_default_handler = {
	.prefix	= POSIX_ACL_XATTR_DEFAULT,
	.flags	= ACL_TYPE_DEFAULT,
	.list	= hfsplus_xattr_list_posix_acl,
	.get	= hfsplus_xattr_get_posix_acl,
	.set	= hfsplus_xattr_set_posix_acl,
};
