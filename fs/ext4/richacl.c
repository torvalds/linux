/*
 * Copyright IBM Corporation, 2010
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/richacl_xattr.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "richacl.h"

struct richacl *
ext4_get_richacl(struct inode *inode)
{
	const int name_index = EXT4_XATTR_INDEX_RICHACL;
	void *value = NULL;
	struct richacl *acl;
	int retval;

	if (!IS_RICHACL(inode))
		return ERR_PTR(-EOPNOTSUPP);
	acl = get_cached_richacl(inode);
	if (acl != ACL_NOT_CACHED)
		return acl;
	retval = ext4_xattr_get(inode, name_index, "", NULL, 0);
	if (retval > 0) {
		value = kmalloc(retval, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		retval = ext4_xattr_get(inode, name_index, "", value, retval);
	}
	if (retval > 0) {
		acl = richacl_from_xattr(value, retval);
		if (acl == ERR_PTR(-EINVAL))
			acl = ERR_PTR(-EIO);
	} else if (retval == -ENODATA || retval == -ENOSYS)
		acl = NULL;
	else
		acl = ERR_PTR(retval);
	kfree(value);

	if (!IS_ERR_OR_NULL(acl))
		set_cached_richacl(inode, acl);

	return acl;
}

static int
ext4_set_richacl(handle_t *handle, struct inode *inode, struct richacl *acl)
{
	const int name_index = EXT4_XATTR_INDEX_RICHACL;
	size_t size = 0;
	void *value = NULL;
	int retval;

	if (acl) {
		mode_t mode = inode->i_mode;
		if (richacl_equiv_mode(acl, &mode) == 0) {
			inode->i_mode = mode;
			ext4_mark_inode_dirty(handle, inode);
			acl = NULL;
		}
	}
	if (acl) {
		size = richacl_xattr_size(acl);
		value = kmalloc(size, GFP_KERNEL);
		if (!value)
			return -ENOMEM;
		richacl_to_xattr(acl, value);
	}
	if (handle)
		retval = ext4_xattr_set_handle(handle, inode, name_index, "",
					       value, size, 0);
	else
		retval = ext4_xattr_set(inode, name_index, "", value, size, 0);
	kfree(value);
	if (!retval)
		set_cached_richacl(inode, acl);

	return retval;
}

int
ext4_init_richacl(handle_t *handle, struct inode *inode, struct inode *dir)
{
	struct richacl *dir_acl = NULL;

	if (!S_ISLNK(inode->i_mode)) {
		dir_acl = ext4_get_richacl(dir);
		if (IS_ERR(dir_acl))
			return PTR_ERR(dir_acl);
	}
	if (dir_acl) {
		struct richacl *acl;
		int retval;

		acl = richacl_inherit_inode(dir_acl, inode);
		richacl_put(dir_acl);

		retval = PTR_ERR(acl);
		if (acl && !IS_ERR(acl)) {
			retval = ext4_set_richacl(handle, inode, acl);
			richacl_put(acl);
		}
		return retval;
	} else {
		inode->i_mode &= ~current_umask();
		return 0;
	}
}

int
ext4_richacl_chmod(struct inode *inode)
{
	struct richacl *acl;
	int retval;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	acl = ext4_get_richacl(inode);
	if (IS_ERR_OR_NULL(acl))
		return PTR_ERR(acl);
	acl = richacl_chmod(acl, inode->i_mode);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	retval = ext4_set_richacl(NULL, inode, acl);
	richacl_put(acl);

	return retval;
}

static size_t
ext4_xattr_list_richacl(struct dentry *dentry, char *list, size_t list_len,
			const char *name, size_t name_len, int type)
{
	const size_t size = sizeof(RICHACL_XATTR);
	if (!IS_RICHACL(dentry->d_inode))
		return 0;
	if (list && size <= list_len)
		memcpy(list, RICHACL_XATTR, size);
	return size;
}

static int
ext4_xattr_get_richacl(struct dentry *dentry, const char *name, void *buffer,
		size_t buffer_size, int type)
{
	struct richacl *acl;
	size_t size;

	if (strcmp(name, "") != 0)
		return -EINVAL;
	acl = ext4_get_richacl(dentry->d_inode);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	size = richacl_xattr_size(acl);
	if (buffer) {
		if (size > buffer_size)
			return -ERANGE;
		richacl_to_xattr(acl, buffer);
	}
	richacl_put(acl);

	return size;
}

static int
ext4_xattr_set_richacl(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags, int type)
{
	handle_t *handle;
	struct richacl *acl = NULL;
	int retval, retries = 0;
	struct inode *inode = dentry->d_inode;

	if (!IS_RICHACL(dentry->d_inode))
		return -EOPNOTSUPP;
	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
	if (strcmp(name, "") != 0)
		return -EINVAL;
	if (current_fsuid() != inode->i_uid &&
	    richacl_check_acl(inode, ACE4_WRITE_ACL) &&
	    !capable(CAP_FOWNER))
		return -EPERM;
	if (value) {
		acl = richacl_from_xattr(value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);

		inode->i_mode &= ~S_IRWXUGO;
		inode->i_mode |= richacl_masks_to_mode(acl);
	}

retry:
	handle = ext4_journal_start(inode, EXT4_DATA_TRANS_BLOCKS(inode->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	retval = ext4_set_richacl(handle, inode, acl);
	ext4_journal_stop(handle);
	if (retval == -ENOSPC && ext4_should_retry_alloc(inode->i_sb, &retries))
		goto retry;
	richacl_put(acl);
	return retval;
}

const struct xattr_handler ext4_richacl_xattr_handler = {
	.prefix	= RICHACL_XATTR,
	.list	= ext4_xattr_list_richacl,
	.get	= ext4_xattr_get_richacl,
	.set	= ext4_xattr_set_richacl,
};
