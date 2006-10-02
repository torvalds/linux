/*
 * fs/generic_acl.c
 *
 * (C) 2005 Andreas Gruenbacher <agruen@suse.de>
 *
 * This file is released under the GPL.
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/generic_acl.h>

/**
 * generic_acl_list  -  Generic xattr_handler->list() operation
 * @ops:	Filesystem specific getacl and setacl callbacks
 */
size_t
generic_acl_list(struct inode *inode, struct generic_acl_operations *ops,
		 int type, char *list, size_t list_size)
{
	struct posix_acl *acl;
	const char *name;
	size_t size;

	acl = ops->getacl(inode, type);
	if (!acl)
		return 0;
	posix_acl_release(acl);

	switch(type) {
		case ACL_TYPE_ACCESS:
			name = POSIX_ACL_XATTR_ACCESS;
			break;

		case ACL_TYPE_DEFAULT:
			name = POSIX_ACL_XATTR_DEFAULT;
			break;

		default:
			return 0;
	}
	size = strlen(name) + 1;
	if (list && size <= list_size)
		memcpy(list, name, size);
	return size;
}

/**
 * generic_acl_get  -  Generic xattr_handler->get() operation
 * @ops:	Filesystem specific getacl and setacl callbacks
 */
int
generic_acl_get(struct inode *inode, struct generic_acl_operations *ops,
		int type, void *buffer, size_t size)
{
	struct posix_acl *acl;
	int error;

	acl = ops->getacl(inode, type);
	if (!acl)
		return -ENODATA;
	error = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);

	return error;
}

/**
 * generic_acl_set  -  Generic xattr_handler->set() operation
 * @ops:	Filesystem specific getacl and setacl callbacks
 */
int
generic_acl_set(struct inode *inode, struct generic_acl_operations *ops,
		int type, const void *value, size_t size)
{
	struct posix_acl *acl = NULL;
	int error;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	if (current->fsuid != inode->i_uid && !capable(CAP_FOWNER))
		return -EPERM;
	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}
	if (acl) {
		mode_t mode;

		error = posix_acl_valid(acl);
		if (error)
			goto failed;
		switch(type) {
			case ACL_TYPE_ACCESS:
				mode = inode->i_mode;
				error = posix_acl_equiv_mode(acl, &mode);
				if (error < 0)
					goto failed;
				inode->i_mode = mode;
				if (error == 0) {
					posix_acl_release(acl);
					acl = NULL;
				}
				break;

			case ACL_TYPE_DEFAULT:
				if (!S_ISDIR(inode->i_mode)) {
					error = -EINVAL;
					goto failed;
				}
				break;
		}
	}
	ops->setacl(inode, type, acl);
	error = 0;
failed:
	posix_acl_release(acl);
	return error;
}

/**
 * generic_acl_init  -  Take care of acl inheritance at @inode create time
 * @ops:	Filesystem specific getacl and setacl callbacks
 *
 * Files created inside a directory with a default ACL inherit the
 * directory's default ACL.
 */
int
generic_acl_init(struct inode *inode, struct inode *dir,
		 struct generic_acl_operations *ops)
{
	struct posix_acl *acl = NULL;
	mode_t mode = inode->i_mode;
	int error;

	inode->i_mode = mode & ~current->fs->umask;
	if (!S_ISLNK(inode->i_mode))
		acl = ops->getacl(dir, ACL_TYPE_DEFAULT);
	if (acl) {
		struct posix_acl *clone;

		if (S_ISDIR(inode->i_mode)) {
			clone = posix_acl_clone(acl, GFP_KERNEL);
			error = -ENOMEM;
			if (!clone)
				goto cleanup;
			ops->setacl(inode, ACL_TYPE_DEFAULT, clone);
			posix_acl_release(clone);
		}
		clone = posix_acl_clone(acl, GFP_KERNEL);
		error = -ENOMEM;
		if (!clone)
			goto cleanup;
		error = posix_acl_create_masq(clone, &mode);
		if (error >= 0) {
			inode->i_mode = mode;
			if (error > 0)
				ops->setacl(inode, ACL_TYPE_ACCESS, clone);
		}
		posix_acl_release(clone);
	}
	error = 0;

cleanup:
	posix_acl_release(acl);
	return error;
}

/**
 * generic_acl_chmod  -  change the access acl of @inode upon chmod()
 * @ops:	FIlesystem specific getacl and setacl callbacks
 *
 * A chmod also changes the permissions of the owner, group/mask, and
 * other ACL entries.
 */
int
generic_acl_chmod(struct inode *inode, struct generic_acl_operations *ops)
{
	struct posix_acl *acl, *clone;
	int error = 0;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	acl = ops->getacl(inode, ACL_TYPE_ACCESS);
	if (acl) {
		clone = posix_acl_clone(acl, GFP_KERNEL);
		posix_acl_release(acl);
		if (!clone)
			return -ENOMEM;
		error = posix_acl_chmod_masq(clone, inode->i_mode);
		if (!error)
			ops->setacl(inode, ACL_TYPE_ACCESS, clone);
		posix_acl_release(clone);
	}
	return error;
}
