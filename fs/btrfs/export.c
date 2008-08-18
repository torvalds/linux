#include <linux/fs.h>
#include <linux/types.h>
#include "ctree.h"
#include "disk-io.h"
#include "btrfs_inode.h"
#include "print-tree.h"
#include "export.h"
#include "compat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
#define FILEID_BTRFS_WITHOUT_PARENT		0x4d
#define FILEID_BTRFS_WITH_PARENT 		0x4e
#define FILEID_BTRFS_WITH_PARENT_ROOT 		0x4f
#endif

#define BTRFS_FID_SIZE_NON_CONNECTABLE		(offsetof(struct btrfs_fid, parent_objectid)/4)
#define BTRFS_FID_SIZE_CONNECTABLE		(offsetof(struct btrfs_fid, parent_root_objectid)/4)
#define BTRFS_FID_SIZE_CONNECTABLE_ROOT		(sizeof(struct btrfs_fid)/4)

static int btrfs_encode_fh(struct dentry *dentry, u32 *fh, int *max_len,
			   int connectable)
{
	struct btrfs_fid *fid = (struct btrfs_fid *)fh;
	struct inode *inode = dentry->d_inode;
	int len = *max_len;
	int type;

	if ((len < BTRFS_FID_SIZE_NON_CONNECTABLE) ||
	    (connectable && len < BTRFS_FID_SIZE_CONNECTABLE))
		return 255;

	len  = BTRFS_FID_SIZE_NON_CONNECTABLE;
	type = FILEID_BTRFS_WITHOUT_PARENT;

	fid->objectid = BTRFS_I(inode)->location.objectid;
	fid->root_objectid = BTRFS_I(inode)->root->objectid;
	fid->gen = inode->i_generation;

	if (connectable && !S_ISDIR(inode->i_mode)) {
		struct inode *parent;
		u64 parent_root_id;

		spin_lock(&dentry->d_lock);

		parent = dentry->d_parent->d_inode;
		fid->parent_objectid = BTRFS_I(parent)->location.objectid;
		fid->parent_gen = parent->i_generation;
		parent_root_id = BTRFS_I(parent)->root->objectid;

		spin_unlock(&dentry->d_lock);

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

static struct dentry *btrfs_get_dentry(struct super_block *sb, u64 objectid,
				       u64 root_objectid, u32 generation)
{
	struct btrfs_root *root;
	struct inode *inode;
	struct dentry *result;
	struct btrfs_key key;

	key.objectid = objectid;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	root = btrfs_lookup_fs_root(btrfs_sb(sb)->fs_info, root_objectid);
	inode = btrfs_iget(sb, &key, root, NULL);
	if (IS_ERR(inode))
		return (void *)inode;

	if (generation != inode->i_generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	result = d_obtain_alias(inode);
	if (!result)
		return ERR_PTR(-ENOMEM);

	return result;
}

static struct dentry *btrfs_fh_to_parent(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct btrfs_fid *fid = (struct btrfs_fid *) fh;
	u64 objectid, root_objectid;
	u32 generation;

	if (fh_type == FILEID_BTRFS_WITH_PARENT) {
		if (fh_len !=  BTRFS_FID_SIZE_CONNECTABLE)
			return NULL;
		root_objectid = fid->root_objectid;
	} else if (fh_type == FILEID_BTRFS_WITH_PARENT_ROOT) {
		if (fh_len != BTRFS_FID_SIZE_CONNECTABLE_ROOT)
			return NULL;
		root_objectid = fid->parent_root_objectid;
	} else
		return NULL;

	objectid = fid->parent_objectid;
	generation = fid->parent_gen;

	return btrfs_get_dentry(sb, objectid, root_objectid, generation);
}

static struct dentry *btrfs_fh_to_dentry(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct btrfs_fid *fid = (struct btrfs_fid *) fh;
	u64 objectid, root_objectid;
	u32 generation;

	if ((fh_type != FILEID_BTRFS_WITH_PARENT ||
	     fh_len != BTRFS_FID_SIZE_CONNECTABLE) &&
	    (fh_type != FILEID_BTRFS_WITH_PARENT_ROOT ||
	     fh_len != BTRFS_FID_SIZE_CONNECTABLE_ROOT) &&
	    (fh_type != FILEID_BTRFS_WITHOUT_PARENT ||
	     fh_len != BTRFS_FID_SIZE_NON_CONNECTABLE))
		return NULL;

	objectid = fid->objectid;
	root_objectid = fid->root_objectid;
	generation = fid->gen;

	return btrfs_get_dentry(sb, objectid, root_objectid, generation);
}

static struct dentry *btrfs_get_parent(struct dentry *child)
{
	struct inode *dir = child->d_inode;
	struct inode *inode;
	struct dentry *parent;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_key key;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int slot;
	u64 objectid;
	int ret;

	path = btrfs_alloc_path();

	key.objectid = dir->i_ino;
	btrfs_set_key_type(&key, BTRFS_INODE_REF_KEY);
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	leaf = path->nodes[0];
	slot = path->slots[0];
	if (ret < 0 || slot == 0) {
		btrfs_free_path(path);
		goto out;
	}
	/* btrfs_search_slot() returns the slot where we'd want to insert
	   an INODE_REF_KEY for parent inode #0xFFFFFFFFFFFFFFFF. The _real_
	   one, telling us what the parent inode _actually_ is, will be in
	   the slot _before_ the one that btrfs_search_slot() returns. */
	slot--;

	btrfs_item_key_to_cpu(leaf, &key, slot);
	btrfs_free_path(path);

	if (key.objectid != dir->i_ino || key.type != BTRFS_INODE_REF_KEY)
		goto out;

	objectid = key.offset;

	/* Build a new key for the inode item */
	key.objectid = objectid;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	inode = btrfs_iget(root->fs_info->sb, &key, root, NULL);

	parent = d_obtain_alias(inode);
	if (!parent)
		parent = ERR_PTR(-ENOMEM);

	return parent;

out:
	btrfs_free_path(path);
	return ERR_PTR(-EINVAL);
}

const struct export_operations btrfs_export_ops = {
	.encode_fh	= btrfs_encode_fh,
	.fh_to_dentry	= btrfs_fh_to_dentry,
	.fh_to_parent	= btrfs_fh_to_parent,
	.get_parent	= btrfs_get_parent,
};
