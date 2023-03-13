// SPDX-License-Identifier: GPL-2.0

#include <linux/fs.h>
#include <linux/types.h>
#include "ctree.h"
#include "disk-io.h"
#include "btrfs_inode.h"
#include "print-tree.h"
#include "export.h"

#define BTRFS_FID_SIZE_NON_CONNECTABLE (offsetof(struct btrfs_fid, \
						 parent_objectid) / 4)
#define BTRFS_FID_SIZE_CONNECTABLE (offsetof(struct btrfs_fid, \
					     parent_root_objectid) / 4)
#define BTRFS_FID_SIZE_CONNECTABLE_ROOT (sizeof(struct btrfs_fid) / 4)

static int btrfs_encode_fh(struct inode *inode, u32 *fh, int *max_len,
			   struct inode *parent)
{
	struct btrfs_fid *fid = (struct btrfs_fid *)fh;
	int len = *max_len;
	int type;

	if (parent && (len < BTRFS_FID_SIZE_CONNECTABLE)) {
		*max_len = BTRFS_FID_SIZE_CONNECTABLE;
		return FILEID_INVALID;
	} else if (len < BTRFS_FID_SIZE_NON_CONNECTABLE) {
		*max_len = BTRFS_FID_SIZE_NON_CONNECTABLE;
		return FILEID_INVALID;
	}

	len  = BTRFS_FID_SIZE_NON_CONNECTABLE;
	type = FILEID_BTRFS_WITHOUT_PARENT;

	fid->objectid = btrfs_ino(BTRFS_I(inode));
	fid->root_objectid = BTRFS_I(inode)->root->root_key.objectid;
	fid->gen = inode->i_generation;

	if (parent) {
		u64 parent_root_id;

		fid->parent_objectid = BTRFS_I(parent)->location.objectid;
		fid->parent_gen = parent->i_generation;
		parent_root_id = BTRFS_I(parent)->root->root_key.objectid;

		if (parent_root_id != fid->root_objectid) {
			fid->parent_root_objectid = parent_root_id;
			len = BTRFS_FID_SIZE_CONNECTABLE_ROOT;
			type = FILEID_BTRFS_WITH_PARENT_ROOT;
		} else {
			len = BTRFS_FID_SIZE_CONNECTABLE;
			type = FILEID_BTRFS_WITH_PARENT;
		}
	}

	*max_len = len;
	return type;
}

struct dentry *btrfs_get_dentry(struct super_block *sb, u64 objectid,
				u64 root_objectid, u64 generation,
				int check_generation)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_root *root;
	struct inode *inode;

	if (objectid < BTRFS_FIRST_FREE_OBJECTID)
		return ERR_PTR(-ESTALE);

	root = btrfs_get_fs_root(fs_info, root_objectid, true);
	if (IS_ERR(root))
		return ERR_CAST(root);

	inode = btrfs_iget(sb, objectid, root);
	btrfs_put_root(root);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	if (check_generation && generation != inode->i_generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	return d_obtain_alias(inode);
}

static struct dentry *btrfs_fh_to_parent(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct btrfs_fid *fid = (struct btrfs_fid *) fh;
	u64 objectid, root_objectid;
	u32 generation;

	if (fh_type == FILEID_BTRFS_WITH_PARENT) {
		if (fh_len <  BTRFS_FID_SIZE_CONNECTABLE)
			return NULL;
		root_objectid = fid->root_objectid;
	} else if (fh_type == FILEID_BTRFS_WITH_PARENT_ROOT) {
		if (fh_len < BTRFS_FID_SIZE_CONNECTABLE_ROOT)
			return NULL;
		root_objectid = fid->parent_root_objectid;
	} else
		return NULL;

	objectid = fid->parent_objectid;
	generation = fid->parent_gen;

	return btrfs_get_dentry(sb, objectid, root_objectid, generation, 1);
}

static struct dentry *btrfs_fh_to_dentry(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct btrfs_fid *fid = (struct btrfs_fid *) fh;
	u64 objectid, root_objectid;
	u32 generation;

	if ((fh_type != FILEID_BTRFS_WITH_PARENT ||
	     fh_len < BTRFS_FID_SIZE_CONNECTABLE) &&
	    (fh_type != FILEID_BTRFS_WITH_PARENT_ROOT ||
	     fh_len < BTRFS_FID_SIZE_CONNECTABLE_ROOT) &&
	    (fh_type != FILEID_BTRFS_WITHOUT_PARENT ||
	     fh_len < BTRFS_FID_SIZE_NON_CONNECTABLE))
		return NULL;

	objectid = fid->objectid;
	root_objectid = fid->root_objectid;
	generation = fid->gen;

	return btrfs_get_dentry(sb, objectid, root_objectid, generation, 1);
}

struct dentry *btrfs_get_parent(struct dentry *child)
{
	struct inode *dir = d_inode(child);
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_root_ref *ref;
	struct btrfs_key key;
	struct btrfs_key found_key;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return ERR_PTR(-ENOMEM);

	if (btrfs_ino(BTRFS_I(dir)) == BTRFS_FIRST_FREE_OBJECTID) {
		key.objectid = root->root_key.objectid;
		key.type = BTRFS_ROOT_BACKREF_KEY;
		key.offset = (u64)-1;
		root = fs_info->tree_root;
	} else {
		key.objectid = btrfs_ino(BTRFS_I(dir));
		key.type = BTRFS_INODE_REF_KEY;
		key.offset = (u64)-1;
	}

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto fail;

	BUG_ON(ret == 0); /* Key with offset of -1 found */
	if (path->slots[0] == 0) {
		ret = -ENOENT;
		goto fail;
	}

	path->slots[0]--;
	leaf = path->nodes[0];

	btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
	if (found_key.objectid != key.objectid || found_key.type != key.type) {
		ret = -ENOENT;
		goto fail;
	}

	if (found_key.type == BTRFS_ROOT_BACKREF_KEY) {
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_root_ref);
		key.objectid = btrfs_root_ref_dirid(leaf, ref);
	} else {
		key.objectid = found_key.offset;
	}
	btrfs_free_path(path);

	if (found_key.type == BTRFS_ROOT_BACKREF_KEY) {
		return btrfs_get_dentry(fs_info->sb, key.objectid,
					found_key.offset, 0, 0);
	}

	return d_obtain_alias(btrfs_iget(fs_info->sb, key.objectid, root));
fail:
	btrfs_free_path(path);
	return ERR_PTR(ret);
}

static int btrfs_get_name(struct dentry *parent, char *name,
			  struct dentry *child)
{
	struct inode *inode = d_inode(child);
	struct inode *dir = d_inode(parent);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_path *path;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_inode_ref *iref;
	struct btrfs_root_ref *rref;
	struct extent_buffer *leaf;
	unsigned long name_ptr;
	struct btrfs_key key;
	int name_len;
	int ret;
	u64 ino;

	if (!S_ISDIR(dir->i_mode))
		return -EINVAL;

	ino = btrfs_ino(BTRFS_I(inode));

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->leave_spinning = 1;

	if (ino == BTRFS_FIRST_FREE_OBJECTID) {
		key.objectid = BTRFS_I(inode)->root->root_key.objectid;
		key.type = BTRFS_ROOT_BACKREF_KEY;
		key.offset = (u64)-1;
		root = fs_info->tree_root;
	} else {
		key.objectid = ino;
		key.offset = btrfs_ino(BTRFS_I(dir));
		key.type = BTRFS_INODE_REF_KEY;
	}

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		btrfs_free_path(path);
		return ret;
	} else if (ret > 0) {
		if (ino == BTRFS_FIRST_FREE_OBJECTID) {
			path->slots[0]--;
		} else {
			btrfs_free_path(path);
			return -ENOENT;
		}
	}
	leaf = path->nodes[0];

	if (ino == BTRFS_FIRST_FREE_OBJECTID) {
		rref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_root_ref);
		name_ptr = (unsigned long)(rref + 1);
		name_len = btrfs_root_ref_name_len(leaf, rref);
	} else {
		iref = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_inode_ref);
		name_ptr = (unsigned long)(iref + 1);
		name_len = btrfs_inode_ref_name_len(leaf, iref);
	}

	read_extent_buffer(leaf, name, name_ptr, name_len);
	btrfs_free_path(path);

	/*
	 * have to add the null termination to make sure that reconnect_path
	 * gets the right len for strlen
	 */
	name[name_len] = '\0';

	return 0;
}

const struct export_operations btrfs_export_ops = {
	.encode_fh	= btrfs_encode_fh,
	.fh_to_dentry	= btrfs_fh_to_dentry,
	.fh_to_parent	= btrfs_fh_to_parent,
	.get_parent	= btrfs_get_parent,
	.get_name	= btrfs_get_name,
};
