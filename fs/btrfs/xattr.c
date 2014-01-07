/*
 * Copyright (C) 2007 Red Hat.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/xattr.h>
#include <linux/security.h>
#include "ctree.h"
#include "btrfs_inode.h"
#include "transaction.h"
#include "xattr.h"
#include "disk-io.h"
#include "props.h"


ssize_t __btrfs_getxattr(struct inode *inode, const char *name,
				void *buffer, size_t size)
{
	struct btrfs_dir_item *di;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int ret = 0;
	unsigned long data_ptr;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/* lookup the xattr by name */
	di = btrfs_lookup_xattr(NULL, root, path, btrfs_ino(inode), name,
				strlen(name), 0);
	if (!di) {
		ret = -ENODATA;
		goto out;
	} else if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto out;
	}

	leaf = path->nodes[0];
	/* if size is 0, that means we want the size of the attr */
	if (!size) {
		ret = btrfs_dir_data_len(leaf, di);
		goto out;
	}

	/* now get the data out of our dir_item */
	if (btrfs_dir_data_len(leaf, di) > size) {
		ret = -ERANGE;
		goto out;
	}

	/*
	 * The way things are packed into the leaf is like this
	 * |struct btrfs_dir_item|name|data|
	 * where name is the xattr name, so security.foo, and data is the
	 * content of the xattr.  data_ptr points to the location in memory
	 * where the data starts in the in memory leaf
	 */
	data_ptr = (unsigned long)((char *)(di + 1) +
				   btrfs_dir_name_len(leaf, di));
	read_extent_buffer(leaf, buffer, data_ptr,
			   btrfs_dir_data_len(leaf, di));
	ret = btrfs_dir_data_len(leaf, di);

out:
	btrfs_free_path(path);
	return ret;
}

static int do_setxattr(struct btrfs_trans_handle *trans,
		       struct inode *inode, const char *name,
		       const void *value, size_t size, int flags)
{
	struct btrfs_dir_item *di;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path;
	size_t name_len = strlen(name);
	int ret = 0;

	if (name_len + size > BTRFS_MAX_XATTR_SIZE(root))
		return -ENOSPC;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (flags & XATTR_REPLACE) {
		di = btrfs_lookup_xattr(trans, root, path, btrfs_ino(inode), name,
					name_len, -1);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		} else if (!di) {
			ret = -ENODATA;
			goto out;
		}
		ret = btrfs_delete_one_dir_name(trans, root, path, di);
		if (ret)
			goto out;
		btrfs_release_path(path);

		/*
		 * remove the attribute
		 */
		if (!value)
			goto out;
	} else {
		di = btrfs_lookup_xattr(NULL, root, path, btrfs_ino(inode),
					name, name_len, 0);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		}
		if (!di && !value)
			goto out;
		btrfs_release_path(path);
	}

again:
	ret = btrfs_insert_xattr_item(trans, root, path, btrfs_ino(inode),
				      name, name_len, value, size);
	/*
	 * If we're setting an xattr to a new value but the new value is say
	 * exactly BTRFS_MAX_XATTR_SIZE, we could end up with EOVERFLOW getting
	 * back from split_leaf.  This is because it thinks we'll be extending
	 * the existing item size, but we're asking for enough space to add the
	 * item itself.  So if we get EOVERFLOW just set ret to EEXIST and let
	 * the rest of the function figure it out.
	 */
	if (ret == -EOVERFLOW)
		ret = -EEXIST;

	if (ret == -EEXIST) {
		if (flags & XATTR_CREATE)
			goto out;
		/*
		 * We can't use the path we already have since we won't have the
		 * proper locking for a delete, so release the path and
		 * re-lookup to delete the thing.
		 */
		btrfs_release_path(path);
		di = btrfs_lookup_xattr(trans, root, path, btrfs_ino(inode),
					name, name_len, -1);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		} else if (!di) {
			/* Shouldn't happen but just in case... */
			btrfs_release_path(path);
			goto again;
		}

		ret = btrfs_delete_one_dir_name(trans, root, path, di);
		if (ret)
			goto out;

		/*
		 * We have a value to set, so go back and try to insert it now.
		 */
		if (value) {
			btrfs_release_path(path);
			goto again;
		}
	}
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * @value: "" makes the attribute to empty, NULL removes it
 */
int __btrfs_setxattr(struct btrfs_trans_handle *trans,
		     struct inode *inode, const char *name,
		     const void *value, size_t size, int flags)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	if (trans)
		return do_setxattr(trans, inode, name, value, size, flags);

	trans = btrfs_start_transaction(root, 2);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	ret = do_setxattr(trans, inode, name, value, size, flags);
	if (ret)
		goto out;

	inode_inc_iversion(inode);
	inode->i_ctime = CURRENT_TIME;
	set_bit(BTRFS_INODE_COPY_EVERYTHING, &BTRFS_I(inode)->runtime_flags);
	ret = btrfs_update_inode(trans, root, inode);
	BUG_ON(ret);
out:
	btrfs_end_transaction(trans, root);
	return ret;
}

ssize_t btrfs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct btrfs_key key, found_key;
	struct inode *inode = dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	int ret = 0, slot;
	size_t total_size = 0, size_left = size;
	unsigned long name_ptr;
	size_t name_len;

	/*
	 * ok we want all objects associated with this id.
	 * NOTE: we set key.offset = 0; because we want to start with the
	 * first xattr that we find and walk forward
	 */
	key.objectid = btrfs_ino(inode);
	btrfs_set_key_type(&key, BTRFS_XATTR_ITEM_KEY);
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 2;

	/* search for our xattrs */
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;

	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];

		/* this is where we start walking through the path */
		if (slot >= btrfs_header_nritems(leaf)) {
			/*
			 * if we've reached the last slot in this leaf we need
			 * to go to the next leaf and reset everything
			 */
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto err;
			else if (ret > 0)
				break;
			continue;
		}

		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		/* check to make sure this item is what we want */
		if (found_key.objectid != key.objectid)
			break;
		if (btrfs_key_type(&found_key) != BTRFS_XATTR_ITEM_KEY)
			break;

		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		if (verify_dir_item(root, leaf, di))
			goto next;

		name_len = btrfs_dir_name_len(leaf, di);
		total_size += name_len + 1;

		/* we are just looking for how big our buffer needs to be */
		if (!size)
			goto next;

		if (!buffer || (name_len + 1) > size_left) {
			ret = -ERANGE;
			goto err;
		}

		name_ptr = (unsigned long)(di + 1);
		read_extent_buffer(leaf, buffer, name_ptr, name_len);
		buffer[name_len] = '\0';

		size_left -= name_len + 1;
		buffer += name_len + 1;
next:
		path->slots[0]++;
	}
	ret = total_size;

err:
	btrfs_free_path(path);

	return ret;
}

/*
 * List of handlers for synthetic system.* attributes.  All real ondisk
 * attributes are handled directly.
 */
const struct xattr_handler *btrfs_xattr_handlers[] = {
#ifdef CONFIG_BTRFS_FS_POSIX_ACL
	&btrfs_xattr_acl_access_handler,
	&btrfs_xattr_acl_default_handler,
#endif
	NULL,
};

/*
 * Check if the attribute is in a supported namespace.
 *
 * This applied after the check for the synthetic attributes in the system
 * namespace.
 */
static bool btrfs_is_valid_xattr(const char *name)
{
	return !strncmp(name, XATTR_SECURITY_PREFIX,
			XATTR_SECURITY_PREFIX_LEN) ||
	       !strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN) ||
	       !strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) ||
	       !strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN) ||
		!strncmp(name, XATTR_BTRFS_PREFIX, XATTR_BTRFS_PREFIX_LEN);
}

ssize_t btrfs_getxattr(struct dentry *dentry, const char *name,
		       void *buffer, size_t size)
{
	/*
	 * If this is a request for a synthetic attribute in the system.*
	 * namespace use the generic infrastructure to resolve a handler
	 * for it via sb->s_xattr.
	 */
	if (!strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return generic_getxattr(dentry, name, buffer, size);

	if (!btrfs_is_valid_xattr(name))
		return -EOPNOTSUPP;
	return __btrfs_getxattr(dentry->d_inode, name, buffer, size);
}

int btrfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		   size_t size, int flags)
{
	struct btrfs_root *root = BTRFS_I(dentry->d_inode)->root;

	/*
	 * The permission on security.* and system.* is not checked
	 * in permission().
	 */
	if (btrfs_root_readonly(root))
		return -EROFS;

	/*
	 * If this is a request for a synthetic attribute in the system.*
	 * namespace use the generic infrastructure to resolve a handler
	 * for it via sb->s_xattr.
	 */
	if (!strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return generic_setxattr(dentry, name, value, size, flags);

	if (!btrfs_is_valid_xattr(name))
		return -EOPNOTSUPP;

	if (!strncmp(name, XATTR_BTRFS_PREFIX, XATTR_BTRFS_PREFIX_LEN))
		return btrfs_set_prop(dentry->d_inode, name,
				      value, size, flags);

	if (size == 0)
		value = "";  /* empty EA, do not remove */

	return __btrfs_setxattr(NULL, dentry->d_inode, name, value, size,
				flags);
}

int btrfs_removexattr(struct dentry *dentry, const char *name)
{
	struct btrfs_root *root = BTRFS_I(dentry->d_inode)->root;

	/*
	 * The permission on security.* and system.* is not checked
	 * in permission().
	 */
	if (btrfs_root_readonly(root))
		return -EROFS;

	/*
	 * If this is a request for a synthetic attribute in the system.*
	 * namespace use the generic infrastructure to resolve a handler
	 * for it via sb->s_xattr.
	 */
	if (!strncmp(name, XATTR_SYSTEM_PREFIX, XATTR_SYSTEM_PREFIX_LEN))
		return generic_removexattr(dentry, name);

	if (!btrfs_is_valid_xattr(name))
		return -EOPNOTSUPP;

	if (!strncmp(name, XATTR_BTRFS_PREFIX, XATTR_BTRFS_PREFIX_LEN))
		return btrfs_set_prop(dentry->d_inode, name,
				      NULL, 0, XATTR_REPLACE);

	return __btrfs_setxattr(NULL, dentry->d_inode, name, NULL, 0,
				XATTR_REPLACE);
}

static int btrfs_initxattrs(struct inode *inode,
			    const struct xattr *xattr_array, void *fs_info)
{
	const struct xattr *xattr;
	struct btrfs_trans_handle *trans = fs_info;
	char *name;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		name = kmalloc(XATTR_SECURITY_PREFIX_LEN +
			       strlen(xattr->name) + 1, GFP_NOFS);
		if (!name) {
			err = -ENOMEM;
			break;
		}
		strcpy(name, XATTR_SECURITY_PREFIX);
		strcpy(name + XATTR_SECURITY_PREFIX_LEN, xattr->name);
		err = __btrfs_setxattr(trans, inode, name,
				       xattr->value, xattr->value_len, 0);
		kfree(name);
		if (err < 0)
			break;
	}
	return err;
}

int btrfs_xattr_security_init(struct btrfs_trans_handle *trans,
			      struct inode *inode, struct inode *dir,
			      const struct qstr *qstr)
{
	return security_inode_init_security(inode, dir, qstr,
					    &btrfs_initxattrs, trans);
}
