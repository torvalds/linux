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
#include "ctree.h"
#include "btrfs_inode.h"
#include "transaction.h"
#include "xattr.h"
#include "disk-io.h"

static struct xattr_handler *btrfs_xattr_handler_map[] = {
	[BTRFS_XATTR_INDEX_USER]		= &btrfs_xattr_user_handler,
	[BTRFS_XATTR_INDEX_POSIX_ACL_ACCESS]	= &btrfs_xattr_acl_access_handler,
	[BTRFS_XATTR_INDEX_POSIX_ACL_DEFAULT]	= &btrfs_xattr_acl_default_handler,
	[BTRFS_XATTR_INDEX_TRUSTED]		= &btrfs_xattr_trusted_handler,
	[BTRFS_XATTR_INDEX_SECURITY]		= &btrfs_xattr_security_handler,
	[BTRFS_XATTR_INDEX_SYSTEM]		= &btrfs_xattr_system_handler,
};

struct xattr_handler *btrfs_xattr_handlers[] = {
	&btrfs_xattr_user_handler,
	&btrfs_xattr_acl_access_handler,
	&btrfs_xattr_acl_default_handler,
	&btrfs_xattr_trusted_handler,
	&btrfs_xattr_security_handler,
	&btrfs_xattr_system_handler,
	NULL,
};

/*
 * @param name - the xattr name
 * @return - the xattr_handler for the xattr, NULL if its not found
 *
 * use this with listxattr where we don't already know the type of xattr we
 * have
 */
static struct xattr_handler *find_btrfs_xattr_handler(struct extent_buffer *l,
						      unsigned long name_ptr,
						      u16 name_len)
{
	struct xattr_handler *handler = NULL;
	int i = 0;

	for (handler = btrfs_xattr_handlers[i]; handler != NULL; i++,
	     handler = btrfs_xattr_handlers[i]) {
		u16 prefix_len = strlen(handler->prefix);

		if (name_len < prefix_len)
			continue;

		if (memcmp_extent_buffer(l, handler->prefix, name_ptr,
					 prefix_len) == 0)
			break;
	}

	return handler;
}

/*
 * @param name_index - the index for the xattr handler
 * @return the xattr_handler if we found it, NULL otherwise
 *
 * use this if we know the type of the xattr already
 */
static struct xattr_handler *btrfs_xattr_handler(int name_index)
{
	struct xattr_handler *handler = NULL;

	if (name_index >= 0 &&
	    name_index < ARRAY_SIZE(btrfs_xattr_handler_map))
		handler = btrfs_xattr_handler_map[name_index];

	return handler;
}

static inline char *get_name(const char *name, int name_index)
{
	char *ret = NULL;
	struct xattr_handler *handler = btrfs_xattr_handler(name_index);
	int prefix_len;

	if (!handler)
		return ret;

	prefix_len = strlen(handler->prefix);

	ret = kmalloc(strlen(name) + prefix_len + 1, GFP_KERNEL);
	if (!ret)
		return ret;

	memcpy(ret, handler->prefix, prefix_len);
	memcpy(ret+prefix_len, name, strlen(name));
	ret[prefix_len + strlen(name)] = '\0';

	return ret;
}

size_t btrfs_xattr_generic_list(struct inode *inode, char *list,
				size_t list_size, const char *name,
				size_t name_len)
{
	if (list && (name_len+1) <= list_size) {
		memcpy(list, name, name_len);
		list[name_len] = '\0';
	} else
		return -ERANGE;

	return name_len+1;
}

ssize_t btrfs_xattr_get(struct inode *inode, int name_index,
			const char *attr_name, void *buffer, size_t size)
{
	struct btrfs_dir_item *di;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct xattr_handler *handler = btrfs_xattr_handler(name_index);
	int ret = 0;
	unsigned long data_ptr;
	char *name;

	if (!handler)
		return -EOPNOTSUPP;
	name = get_name(attr_name, name_index);
	if (!name)
		return -ENOMEM;

	path = btrfs_alloc_path();
	if (!path) {
		kfree(name);
		return -ENOMEM;
	}

	mutex_lock(&root->fs_info->fs_mutex);
	/* lookup the xattr by name */
	di = btrfs_lookup_xattr(NULL, root, path, inode->i_ino, name,
				strlen(name), 0);
	if (!di || IS_ERR(di)) {
		ret = -ENODATA;
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
	data_ptr = (unsigned long)((char *)(di + 1) +
				   btrfs_dir_name_len(leaf, di));
	read_extent_buffer(leaf, buffer, data_ptr,
			   btrfs_dir_data_len(leaf, di));
	ret = btrfs_dir_data_len(leaf, di);

out:
	mutex_unlock(&root->fs_info->fs_mutex);
	kfree(name);
	btrfs_free_path(path);
	return ret;
}

int btrfs_xattr_set(struct inode *inode, int name_index,
		    const char *attr_name, const void *value, size_t size,
		    int flags)
{
	struct btrfs_dir_item *di;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct xattr_handler *handler = btrfs_xattr_handler(name_index);
	char *name;
	int ret = 0, mod = 0;
	if (!handler)
		return -EOPNOTSUPP;
	name = get_name(attr_name, name_index);
	if (!name)
		return -ENOMEM;

	path = btrfs_alloc_path();
	if (!path) {
		kfree(name);
		return -ENOMEM;
	}

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);

	/* first lets see if we already have this xattr */
	di = btrfs_lookup_xattr(trans, root, path, inode->i_ino, name,
				strlen(name), -1);
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto out;
	}

	/* ok we already have this xattr, lets remove it */
	if (di) {
		/* if we want create only exit */
		if (flags & XATTR_CREATE) {
			ret = -EEXIST;
			goto out;
		}

		ret = btrfs_delete_one_dir_name(trans, root, path, di);
		if (ret)
			goto out;
		btrfs_release_path(root, path);

		/* if we don't have a value then we are removing the xattr */
		if (!value) {
			mod = 1;
			goto out;
		}
	} else if (flags & XATTR_REPLACE) {
		/* we couldn't find the attr to replace, so error out */
		ret = -ENODATA;
		goto out;
	}

	/* ok we have to create a completely new xattr */
	ret = btrfs_insert_xattr_item(trans, root, name, strlen(name),
				      value, size, inode->i_ino);
	if (ret)
		goto out;
	mod = 1;

out:
	if (mod) {
		inode->i_ctime = CURRENT_TIME;
		ret = btrfs_update_inode(trans, root, inode);
	}

	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	kfree(name);
	btrfs_free_path(path);

	return ret;
}

ssize_t btrfs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct btrfs_key key, found_key;
	struct inode *inode = dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path;
	struct btrfs_item *item;
	struct extent_buffer *leaf;
	struct btrfs_dir_item *di;
	struct xattr_handler *handler;
	int ret = 0, slot, advance;
	size_t total_size = 0, size_left = size, written;
	unsigned long name_ptr;
	char *name;
	u32 nritems;

	/*
	 * ok we want all objects associated with this id.
	 * NOTE: we set key.offset = 0; because we want to start with the
	 * first xattr that we find and walk forward
	 */
	key.objectid = inode->i_ino;
	btrfs_set_key_type(&key, BTRFS_XATTR_ITEM_KEY);
	key.offset = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 2;

	mutex_lock(&root->fs_info->fs_mutex);

	/* search for our xattrs */
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	ret = 0;
	advance = 0;
	while (1) {
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		slot = path->slots[0];

		/* this is where we start walking through the path */
		if (advance || slot >= nritems) {
			/*
			 * if we've reached the last slot in this leaf we need
			 * to go to the next leaf and reset everything
			 */
			if (slot >= nritems-1) {
				ret = btrfs_next_leaf(root, path);
				if (ret)
					break;
				leaf = path->nodes[0];
				nritems = btrfs_header_nritems(leaf);
				slot = path->slots[0];
			} else {
				/*
				 * just walking through the slots on this leaf
				 */
				slot++;
				path->slots[0]++;
			}
		}
		advance = 1;

		item = btrfs_item_nr(leaf, slot);
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		/* check to make sure this item is what we want */
		if (found_key.objectid != key.objectid)
			break;
		if (btrfs_key_type(&found_key) != BTRFS_XATTR_ITEM_KEY)
			break;

		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);

		total_size += btrfs_dir_name_len(leaf, di)+1;

		/* we are just looking for how big our buffer needs to be */
		if (!size)
			continue;

		/* find our handler for this xattr */
		name_ptr = (unsigned long)(di + 1);
		handler = find_btrfs_xattr_handler(leaf, name_ptr,
						   btrfs_dir_name_len(leaf, di));
		if (!handler) {
			printk(KERN_ERR "btrfs: unsupported xattr found\n");
			continue;
		}

		name = kmalloc(btrfs_dir_name_len(leaf, di), GFP_KERNEL);
		read_extent_buffer(leaf, name, name_ptr,
				   btrfs_dir_name_len(leaf, di));

		/* call the list function associated with this xattr */
		written = handler->list(inode, buffer, size_left, name,
					btrfs_dir_name_len(leaf, di));
		kfree(name);

		if (written < 0) {
			ret = -ERANGE;
			break;
		}

		size_left -= written;
		buffer += written;
	}
	ret = total_size;

err:
	mutex_unlock(&root->fs_info->fs_mutex);
	btrfs_free_path(path);

	return ret;
}

/*
 * delete all the xattrs associated with the inode.  fs_mutex should be
 * held when we come into here
 */
int btrfs_delete_xattrs(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, struct inode *inode)
{
	struct btrfs_path *path;
	struct btrfs_key key, found_key;
	struct btrfs_item *item;
	struct extent_buffer *leaf;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = -1;
	key.objectid = inode->i_ino;
	btrfs_set_key_type(&key, BTRFS_XATTR_ITEM_KEY);
	key.offset = (u64)-1;

	while(1) {
		/* look for our next xattr */
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;
		BUG_ON(ret == 0);

		if (path->slots[0] == 0)
			break;

		path->slots[0]--;
		leaf = path->nodes[0];
		item = btrfs_item_nr(leaf, path->slots[0]);
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		if (found_key.objectid != key.objectid)
			break;
		if (btrfs_key_type(&found_key) != BTRFS_XATTR_ITEM_KEY)
			break;

		ret = btrfs_del_item(trans, root, path);
		BUG_ON(ret);
		btrfs_release_path(root, path);
	}
	ret = 0;
out:
	btrfs_free_path(path);

	return ret;
}

/*
 * Handler functions
 */
#define BTRFS_XATTR_SETGET_FUNCS(name, index)				\
static int btrfs_xattr_##name##_get(struct inode *inode,		\
				    const char *name, void *value,	\
				    size_t size)			\
{									\
	if (*name == '\0')						\
		return -EINVAL;						\
	return btrfs_xattr_get(inode, index, name, value, size);	\
}									\
static int btrfs_xattr_##name##_set(struct inode *inode,		\
				    const char *name, const void *value,\
				    size_t size, int flags)		\
{									\
	if (*name == '\0')						\
		return -EINVAL;						\
	return btrfs_xattr_set(inode, index, name, value, size, flags);	\
}									\
BTRFS_XATTR_SETGET_FUNCS(security, BTRFS_XATTR_INDEX_SECURITY);
BTRFS_XATTR_SETGET_FUNCS(system, BTRFS_XATTR_INDEX_SYSTEM);
BTRFS_XATTR_SETGET_FUNCS(user, BTRFS_XATTR_INDEX_USER);
BTRFS_XATTR_SETGET_FUNCS(trusted, BTRFS_XATTR_INDEX_TRUSTED);

struct xattr_handler btrfs_xattr_security_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.list	= btrfs_xattr_generic_list,
	.get	= btrfs_xattr_security_get,
	.set	= btrfs_xattr_security_set,
};

struct xattr_handler btrfs_xattr_system_handler = {
	.prefix = XATTR_SYSTEM_PREFIX,
	.list	= btrfs_xattr_generic_list,
	.get	= btrfs_xattr_system_get,
	.set	= btrfs_xattr_system_set,
};

struct xattr_handler btrfs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.list	= btrfs_xattr_generic_list,
	.get	= btrfs_xattr_user_get,
	.set	= btrfs_xattr_user_set,
};

struct xattr_handler btrfs_xattr_trusted_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.list	= btrfs_xattr_generic_list,
	.get	= btrfs_xattr_trusted_get,
	.set	= btrfs_xattr_trusted_set,
};
