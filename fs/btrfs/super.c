#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"

void btrfs_fsinfo_release(struct kobject *obj)
{
	struct btrfs_fs_info *fsinfo = container_of(obj,
					    struct btrfs_fs_info, kobj);
	kfree(fsinfo);
}

struct kobj_type btrfs_fsinfo_ktype = {
	.release = btrfs_fsinfo_release,
};

struct btrfs_iget_args {
	u64 ino;
	struct btrfs_root *root;
};

decl_subsys(btrfs, &btrfs_fsinfo_ktype, NULL);

#define BTRFS_SUPER_MAGIC 0x9123682E

static struct inode_operations btrfs_dir_inode_operations;
static struct inode_operations btrfs_dir_ro_inode_operations;
static struct super_operations btrfs_super_ops;
static struct file_operations btrfs_dir_file_operations;
static struct inode_operations btrfs_file_inode_operations;
static struct address_space_operations btrfs_aops;
static struct file_operations btrfs_file_operations;

static void btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_path *path;
	struct btrfs_inode_item *inode_item;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key location;
	struct btrfs_block_group_cache *alloc_group;
	u64 alloc_group_block;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	mutex_lock(&root->fs_info->fs_mutex);

	memcpy(&location, &BTRFS_I(inode)->location, sizeof(location));
	ret = btrfs_lookup_inode(NULL, root, path, &location, 0);
	if (ret) {
		btrfs_free_path(path);
		goto make_bad;
	}
	inode_item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
				  path->slots[0],
				  struct btrfs_inode_item);

	inode->i_mode = btrfs_inode_mode(inode_item);
	inode->i_nlink = btrfs_inode_nlink(inode_item);
	inode->i_uid = btrfs_inode_uid(inode_item);
	inode->i_gid = btrfs_inode_gid(inode_item);
	inode->i_size = btrfs_inode_size(inode_item);
	inode->i_atime.tv_sec = btrfs_timespec_sec(&inode_item->atime);
	inode->i_atime.tv_nsec = btrfs_timespec_nsec(&inode_item->atime);
	inode->i_mtime.tv_sec = btrfs_timespec_sec(&inode_item->mtime);
	inode->i_mtime.tv_nsec = btrfs_timespec_nsec(&inode_item->mtime);
	inode->i_ctime.tv_sec = btrfs_timespec_sec(&inode_item->ctime);
	inode->i_ctime.tv_nsec = btrfs_timespec_nsec(&inode_item->ctime);
	inode->i_blocks = btrfs_inode_nblocks(inode_item);
	inode->i_generation = btrfs_inode_generation(inode_item);
	alloc_group_block = btrfs_inode_block_group(inode_item);
	ret = radix_tree_gang_lookup(&root->fs_info->block_group_radix,
				     (void **)&alloc_group,
				     alloc_group_block, 1);
	BUG_ON(!ret);
	BTRFS_I(inode)->block_group = alloc_group;

	btrfs_free_path(path);
	inode_item = NULL;

	mutex_unlock(&root->fs_info->fs_mutex);

	switch (inode->i_mode & S_IFMT) {
#if 0
	default:
		init_special_inode(inode, inode->i_mode,
				   btrfs_inode_rdev(inode_item));
		break;
#endif
	case S_IFREG:
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
		break;
	case S_IFDIR:
		inode->i_fop = &btrfs_dir_file_operations;
		if (root == root->fs_info->tree_root)
			inode->i_op = &btrfs_dir_ro_inode_operations;
		else
			inode->i_op = &btrfs_dir_inode_operations;
		break;
	case S_IFLNK:
		// inode->i_op = &page_symlink_inode_operations;
		break;
	}
	return;

make_bad:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->fs_mutex);
	make_bad_inode(inode);
}

static void fill_inode_item(struct btrfs_inode_item *item,
			    struct inode *inode)
{
	btrfs_set_inode_uid(item, inode->i_uid);
	btrfs_set_inode_gid(item, inode->i_gid);
	btrfs_set_inode_size(item, inode->i_size);
	btrfs_set_inode_mode(item, inode->i_mode);
	btrfs_set_inode_nlink(item, inode->i_nlink);
	btrfs_set_timespec_sec(&item->atime, inode->i_atime.tv_sec);
	btrfs_set_timespec_nsec(&item->atime, inode->i_atime.tv_nsec);
	btrfs_set_timespec_sec(&item->mtime, inode->i_mtime.tv_sec);
	btrfs_set_timespec_nsec(&item->mtime, inode->i_mtime.tv_nsec);
	btrfs_set_timespec_sec(&item->ctime, inode->i_ctime.tv_sec);
	btrfs_set_timespec_nsec(&item->ctime, inode->i_ctime.tv_nsec);
	btrfs_set_inode_nblocks(item, inode->i_blocks);
	btrfs_set_inode_generation(item, inode->i_generation);
	btrfs_set_inode_block_group(item,
				    BTRFS_I(inode)->block_group->key.objectid);
}


static int btrfs_update_inode(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct inode *inode)
{
	struct btrfs_inode_item *inode_item;
	struct btrfs_path *path;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	ret = btrfs_lookup_inode(trans, root, path,
				 &BTRFS_I(inode)->location, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

	inode_item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
				  path->slots[0],
				  struct btrfs_inode_item);

	fill_inode_item(inode_item, inode);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	ret = 0;
failed:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}


static int btrfs_unlink_trans(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct inode *dir,
			      struct dentry *dentry)
{
	struct btrfs_path *path;
	const char *name = dentry->d_name.name;
	int name_len = dentry->d_name.len;
	int ret = 0;
	u64 objectid;
	struct btrfs_dir_item *di;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	di = btrfs_lookup_dir_item(trans, root, path, dir->i_ino,
				    name, name_len, -1);
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto err;
	}
	if (!di) {
		ret = -ENOENT;
		goto err;
	}
	objectid = btrfs_disk_key_objectid(&di->location);
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	BUG_ON(ret);
	btrfs_release_path(root, path);

	di = btrfs_lookup_dir_index_item(trans, root, path, dir->i_ino,
					 objectid, name, name_len, -1);
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto err;
	}
	if (!di) {
		ret = -ENOENT;
		goto err;
	}
	ret = btrfs_delete_one_dir_name(trans, root, path, di);
	BUG_ON(ret);

	dentry->d_inode->i_ctime = dir->i_ctime;
err:
	btrfs_free_path(path);
	if (!ret) {
		dir->i_size -= name_len * 2;
		btrfs_update_inode(trans, root, dir);
		drop_nlink(dentry->d_inode);
		btrfs_update_inode(trans, root, dentry->d_inode);
		dir->i_sb->s_dirt = 1;
	}
	return ret;
}

static int btrfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root;
	struct btrfs_trans_handle *trans;
	int ret;

	root = BTRFS_I(dir)->root;
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);
	ret = btrfs_unlink_trans(trans, root, dir, dentry);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	return ret;
}

static int btrfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int err;
	int ret;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_trans_handle *trans;
	struct btrfs_key found_key;
	int found_type;
	struct btrfs_leaf *leaf;
	char *goodnames = "..";

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);
	key.objectid = inode->i_ino;
	key.offset = (u64)-1;
	key.flags = (u32)-1;
	while(1) {
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		BUG_ON(ret == 0);
		if (path->slots[0] == 0) {
			err = -ENOENT;
			goto out;
		}
		path->slots[0]--;
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		btrfs_disk_key_to_cpu(&found_key,
				      &leaf->items[path->slots[0]].key);
		found_type = btrfs_key_type(&found_key);
		if (found_key.objectid != inode->i_ino) {
			err = -ENOENT;
			goto out;
		}
		if ((found_type != BTRFS_DIR_ITEM_KEY &&
		     found_type != BTRFS_DIR_INDEX_KEY) ||
	            (!btrfs_match_dir_item_name(root, path, goodnames, 2) &&
	            !btrfs_match_dir_item_name(root, path, goodnames, 1))) {
			err = -ENOTEMPTY;
			goto out;
		}
		ret = btrfs_del_item(trans, root, path);
		BUG_ON(ret);

		if (found_type == BTRFS_DIR_ITEM_KEY && found_key.offset == 1)
			break;
		btrfs_release_path(root, path);
	}
	ret = 0;
	btrfs_release_path(root, path);

	/* now the directory is empty */
	err = btrfs_unlink_trans(trans, root, dir, dentry);
	if (!err) {
		inode->i_size = 0;
	}
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->fs_mutex);
	ret = btrfs_end_transaction(trans, root);
	if (ret && !err)
		err = ret;
	return err;
}

static int btrfs_free_inode(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct inode *inode)
{
	struct btrfs_path *path;
	int ret;

	clear_inode(inode);

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	ret = btrfs_lookup_inode(trans, root, path,
				 &BTRFS_I(inode)->location, -1);
	BUG_ON(ret);
	ret = btrfs_del_item(trans, root, path);
	BUG_ON(ret);
	btrfs_free_path(path);
	return ret;
}

static int btrfs_truncate_in_trans(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct inode *inode)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_disk_key *found_key;
	struct btrfs_leaf *leaf;
	struct btrfs_file_extent_item *fi = NULL;
	u64 extent_start = 0;
	u64 extent_num_blocks = 0;
	int found_extent;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	/* FIXME, add redo link to tree so we don't leak on crash */
	key.objectid = inode->i_ino;
	key.offset = (u64)-1;
	key.flags = 0;
	/*
	 * use BTRFS_CSUM_ITEM_KEY because it is larger than inline keys
	 * or extent data
	 */
	btrfs_set_key_type(&key, BTRFS_CSUM_ITEM_KEY);
	while(1) {
		btrfs_init_path(path);
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0) {
			goto error;
		}
		if (ret > 0) {
			BUG_ON(path->slots[0] == 0);
			path->slots[0]--;
		}
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		found_key = &leaf->items[path->slots[0]].key;
		if (btrfs_disk_key_objectid(found_key) != inode->i_ino)
			break;
		if (btrfs_disk_key_type(found_key) != BTRFS_CSUM_ITEM_KEY &&
		    btrfs_disk_key_type(found_key) != BTRFS_EXTENT_DATA_KEY)
			break;
		if (btrfs_disk_key_offset(found_key) < inode->i_size)
			break;
		found_extent = 0;
		if (btrfs_disk_key_type(found_key) == BTRFS_EXTENT_DATA_KEY) {
			fi = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
					    path->slots[0],
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(fi) !=
			    BTRFS_FILE_EXTENT_INLINE) {
				extent_start =
					btrfs_file_extent_disk_blocknr(fi);
				extent_num_blocks =
					btrfs_file_extent_disk_num_blocks(fi);
				/* FIXME blocksize != 4096 */
				inode->i_blocks -=
					btrfs_file_extent_num_blocks(fi) << 3;
				found_extent = 1;
			}
		}
		ret = btrfs_del_item(trans, root, path);
		BUG_ON(ret);
		btrfs_release_path(root, path);
		if (found_extent) {
			ret = btrfs_free_extent(trans, root, extent_start,
						extent_num_blocks, 0);
			BUG_ON(ret);
		}
	}
	ret = 0;
error:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	inode->i_sb->s_dirt = 1;
	return ret;
}

static void btrfs_delete_inode(struct inode *inode)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	truncate_inode_pages(&inode->i_data, 0);
	if (is_bad_inode(inode)) {
		goto no_delete;
	}
	inode->i_size = 0;
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	if (S_ISREG(inode->i_mode)) {
		ret = btrfs_truncate_in_trans(trans, root, inode);
		BUG_ON(ret);
	}
	btrfs_free_inode(trans, root, inode);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	return;
no_delete:
	clear_inode(inode);
}

static int btrfs_inode_by_name(struct inode *dir, struct dentry *dentry,
			       struct btrfs_key *location)
{
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	di = btrfs_lookup_dir_item(NULL, root, path, dir->i_ino, name,
				    namelen, 0);
	if (!di || IS_ERR(di)) {
		location->objectid = 0;
		ret = 0;
		goto out;
	}
	btrfs_disk_key_to_cpu(location, &di->location);
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}

int fixup_tree_root_location(struct btrfs_root *root,
			     struct btrfs_key *location,
			     struct btrfs_root **sub_root)
{
	struct btrfs_path *path;
	struct btrfs_root_item *ri;

	if (btrfs_key_type(location) != BTRFS_ROOT_ITEM_KEY)
		return 0;
	if (location->objectid == BTRFS_ROOT_TREE_OBJECTID)
		return 0;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	mutex_lock(&root->fs_info->fs_mutex);

	*sub_root = btrfs_read_fs_root(root->fs_info, location);
	if (IS_ERR(*sub_root))
		return PTR_ERR(*sub_root);

	ri = &(*sub_root)->root_item;
	location->objectid = btrfs_root_dirid(ri);
	location->flags = 0;
	btrfs_set_key_type(location, BTRFS_INODE_ITEM_KEY);
	location->offset = 0;

	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

int btrfs_init_locked_inode(struct inode *inode, void *p)
{
	struct btrfs_iget_args *args = p;
	inode->i_ino = args->ino;
	BTRFS_I(inode)->root = args->root;
	return 0;
}

int btrfs_find_actor(struct inode *inode, void *opaque)
{
	struct btrfs_iget_args *args = opaque;
	return (args->ino == inode->i_ino &&
		args->root == BTRFS_I(inode)->root);
}

struct inode *btrfs_iget_locked(struct super_block *s, u64 objectid,
				struct btrfs_root *root)
{
	struct inode *inode;
	struct btrfs_iget_args args;
	args.ino = objectid;
	args.root = root;

	inode = iget5_locked(s, objectid, btrfs_find_actor,
			     btrfs_init_locked_inode,
			     (void *)&args);
	return inode;
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode * inode;
	struct btrfs_inode *bi = BTRFS_I(dir);
	struct btrfs_root *root = bi->root;
	struct btrfs_root *sub_root = root;
	struct btrfs_key location;
	int ret;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);
	mutex_lock(&root->fs_info->fs_mutex);
	ret = btrfs_inode_by_name(dir, dentry, &location);
	mutex_unlock(&root->fs_info->fs_mutex);
	if (ret < 0)
		return ERR_PTR(ret);
	inode = NULL;
	if (location.objectid) {
		ret = fixup_tree_root_location(root, &location, &sub_root);
		if (ret < 0)
			return ERR_PTR(ret);
		if (ret > 0)
			return ERR_PTR(-ENOENT);
		inode = btrfs_iget_locked(dir->i_sb, location.objectid,
					  sub_root);
		if (!inode)
			return ERR_PTR(-EACCES);
		if (inode->i_state & I_NEW) {
			if (sub_root != root) {
printk("adding new root for inode %lu root %p (found %p)\n", inode->i_ino, sub_root, BTRFS_I(inode)->root);
				igrab(inode);
				sub_root->inode = inode;
			}
			BTRFS_I(inode)->root = sub_root;
			memcpy(&BTRFS_I(inode)->location, &location,
			       sizeof(location));
			btrfs_read_locked_inode(inode);
			unlock_new_inode(inode);
		}
	}
	return d_splice_alias(inode, dentry);
}

static void reada_leaves(struct btrfs_root *root, struct btrfs_path *path)
{
	struct btrfs_node *node;
	int i;
	int nritems;
	u64 objectid;
	u64 item_objectid;
	u64 blocknr;
	int slot;

	if (!path->nodes[1])
		return;
	node = btrfs_buffer_node(path->nodes[1]);
	slot = path->slots[1];
	objectid = btrfs_disk_key_objectid(&node->ptrs[slot].key);
	nritems = btrfs_header_nritems(&node->header);
	for (i = slot; i < nritems; i++) {
		item_objectid = btrfs_disk_key_objectid(&node->ptrs[i].key);
		if (item_objectid != objectid)
			break;
		blocknr = btrfs_node_blockptr(node, i);
		readahead_tree_block(root, blocknr);
	}
}

static int btrfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_path *path;
	int ret;
	u32 nritems;
	struct btrfs_leaf *leaf;
	int slot;
	int advance;
	unsigned char d_type = DT_UNKNOWN;
	int over = 0;
	u32 di_cur;
	u32 di_total;
	u32 di_len;
	int key_type = BTRFS_DIR_INDEX_KEY;

	/* FIXME, use a real flag for deciding about the key type */
	if (root->fs_info->tree_root == root)
		key_type = BTRFS_DIR_ITEM_KEY;
	mutex_lock(&root->fs_info->fs_mutex);
	key.objectid = inode->i_ino;
	key.flags = 0;
	btrfs_set_key_type(&key, key_type);
	key.offset = filp->f_pos;
	path = btrfs_alloc_path();
	btrfs_init_path(path);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	advance = 0;
	reada_leaves(root, path);
	while(1) {
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		nritems = btrfs_header_nritems(&leaf->header);
		slot = path->slots[0];
		if (advance || slot >= nritems) {
			if (slot >= nritems -1) {
				ret = btrfs_next_leaf(root, path);
				if (ret)
					break;
				leaf = btrfs_buffer_leaf(path->nodes[0]);
				nritems = btrfs_header_nritems(&leaf->header);
				slot = path->slots[0];
				if (path->slots[1] == 0)
					reada_leaves(root, path);
			} else {
				slot++;
				path->slots[0]++;
			}
		}
		advance = 1;
		item = leaf->items + slot;
		if (btrfs_disk_key_objectid(&item->key) != key.objectid)
			break;
		if (btrfs_disk_key_type(&item->key) != key_type)
			break;
		if (btrfs_disk_key_offset(&item->key) < filp->f_pos)
			continue;
		filp->f_pos = btrfs_disk_key_offset(&item->key);
		advance = 1;
		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		di_cur = 0;
		di_total = btrfs_item_size(leaf->items + slot);
		while(di_cur < di_total) {
			over = filldir(dirent, (const char *)(di + 1),
				       btrfs_dir_name_len(di),
				       btrfs_disk_key_offset(&item->key),
				       btrfs_disk_key_objectid(&di->location),
				       d_type);
			if (over)
				goto nopos;
			di_len = btrfs_dir_name_len(di) + sizeof(*di);
			di_cur += di_len;
			di = (struct btrfs_dir_item *)((char *)di + di_len);
		}
	}
	filp->f_pos++;
nopos:
	ret = 0;
err:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	mutex_unlock(&root->fs_info->fs_mutex);
	return ret;
}

static void btrfs_put_super (struct super_block * sb)
{
	struct btrfs_root *root = btrfs_sb(sb);
	int ret;

	ret = close_ctree(root);
	if (ret) {
		printk("close ctree returns %d\n", ret);
	}
	sb->s_fs_info = NULL;
}

static int btrfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root_dentry;
	struct btrfs_super_block *disk_super;
	struct btrfs_root *tree_root;
	struct btrfs_inode *bi;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_magic = BTRFS_SUPER_MAGIC;
	sb->s_op = &btrfs_super_ops;
	sb->s_time_gran = 1;

	tree_root = open_ctree(sb);

	if (!tree_root) {
		printk("btrfs: open_ctree failed\n");
		return -EIO;
	}
	sb->s_fs_info = tree_root;
	disk_super = tree_root->fs_info->disk_super;
	printk("read in super total blocks %Lu root %Lu\n",
	       btrfs_super_total_blocks(disk_super),
	       btrfs_super_root_dir(disk_super));

	inode = btrfs_iget_locked(sb, btrfs_super_root_dir(disk_super),
				  tree_root);
	bi = BTRFS_I(inode);
	bi->location.objectid = inode->i_ino;
	bi->location.offset = 0;
	bi->location.flags = 0;
	bi->root = tree_root;
	btrfs_set_key_type(&bi->location, BTRFS_INODE_ITEM_KEY);

	if (!inode)
		return -ENOMEM;
	if (inode->i_state & I_NEW) {
		btrfs_read_locked_inode(inode);
		unlock_new_inode(inode);
	}

	root_dentry = d_alloc_root(inode);
	if (!root_dentry) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root_dentry;

	return 0;
}

static int btrfs_write_inode(struct inode *inode, int wait)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	int ret = 0;

	if (wait) {
		mutex_lock(&root->fs_info->fs_mutex);
		trans = btrfs_start_transaction(root, 1);
		btrfs_set_trans_block_group(trans, inode);
		ret = btrfs_commit_transaction(trans, root);
		mutex_unlock(&root->fs_info->fs_mutex);
	}
	return ret;
}

static void btrfs_dirty_inode(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	btrfs_update_inode(trans, root, inode);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
}

static struct inode *btrfs_new_inode(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 objectid,
				     struct btrfs_block_group_cache *group,
				     int mode)
{
	struct inode *inode;
	struct btrfs_inode_item inode_item;
	struct btrfs_key *location;
	int ret;

	inode = new_inode(root->fs_info->sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	BTRFS_I(inode)->root = root;
	group = btrfs_find_block_group(root, group, 0);
	BTRFS_I(inode)->block_group = group;

	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_mode = mode;
	inode->i_ino = objectid;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	fill_inode_item(&inode_item, inode);
	location = &BTRFS_I(inode)->location;
	location->objectid = objectid;
	location->flags = 0;
	location->offset = 0;
	btrfs_set_key_type(location, BTRFS_INODE_ITEM_KEY);

	ret = btrfs_insert_inode(trans, root, objectid, &inode_item);
	BUG_ON(ret);

	insert_inode_hash(inode);
	return inode;
}

static int btrfs_add_link(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_root *root = BTRFS_I(dentry->d_parent->d_inode)->root;
	key.objectid = inode->i_ino;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	ret = btrfs_insert_dir_item(trans, root,
				    dentry->d_name.name, dentry->d_name.len,
				    dentry->d_parent->d_inode->i_ino,
				    &key, 0);
	if (ret == 0) {
		dentry->d_parent->d_inode->i_size += dentry->d_name.len * 2;
		ret = btrfs_update_inode(trans, root,
					 dentry->d_parent->d_inode);
	}
	return ret;
}

static int btrfs_add_nondir(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode)
{
	int err = btrfs_add_link(trans, dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	if (err > 0)
		err = -EEXIST;
	return err;
}

static int btrfs_create(struct inode *dir, struct dentry *dentry,
			int mode, struct nameidata *nd)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode;
	int err;
	int drop_inode = 0;
	u64 objectid;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, objectid,
				BTRFS_I(dir)->block_group, mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;

	btrfs_set_trans_block_group(trans, inode);
	err = btrfs_add_nondir(trans, dentry, inode);
	if (err)
		drop_inode = 1;
	else {
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
	}
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);
out_unlock:
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);

	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	return err;
}

static int btrfs_make_empty_dir(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 objectid, u64 dirid)
{
	int ret;
	char buf[2];
	struct btrfs_key key;

	buf[0] = '.';
	buf[1] = '.';

	key.objectid = objectid;
	key.offset = 0;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);

	ret = btrfs_insert_dir_item(trans, root, buf, 1, objectid,
				    &key, 1);
	if (ret)
		goto error;
	key.objectid = dirid;
	ret = btrfs_insert_dir_item(trans, root, buf, 2, objectid,
				    &key, 1);
	if (ret)
		goto error;
error:
	return ret;
}

static int btrfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	int err = 0;
	int drop_on_err = 0;
	u64 objectid;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_unlock;
	}

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, objectid,
				BTRFS_I(dir)->block_group, S_IFDIR | mode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_fail;
	}
	drop_on_err = 1;
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;
	btrfs_set_trans_block_group(trans, inode);

	err = btrfs_make_empty_dir(trans, root, inode->i_ino, dir->i_ino);
	if (err)
		goto out_fail;

	inode->i_size = 6;
	err = btrfs_update_inode(trans, root, inode);
	if (err)
		goto out_fail;
	err = btrfs_add_link(trans, dentry, inode);
	if (err)
		goto out_fail;
	d_instantiate(dentry, inode);
	drop_on_err = 0;
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, inode);
	btrfs_update_inode_block_group(trans, dir);

out_fail:
	btrfs_end_transaction(trans, root);
out_unlock:
	mutex_unlock(&root->fs_info->fs_mutex);
	if (drop_on_err)
		iput(inode);
	return err;
}

static int btrfs_sync_file(struct file *file,
			   struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;
	struct btrfs_trans_handle *trans;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		ret = -ENOMEM;
		goto out;
	}
	ret = btrfs_commit_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
out:
	return ret > 0 ? EIO : ret;
}

static int btrfs_sync_fs(struct super_block *sb, int wait)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root;
	int ret;
	root = btrfs_sb(sb);

	sb->s_dirt = 0;
	if (!wait) {
		filemap_flush(root->fs_info->btree_inode->i_mapping);
		return 0;
	}
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	sb->s_dirt = 0;
	BUG_ON(ret);
printk("btrfs sync_fs\n");
	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

static int btrfs_get_block_lock(struct inode *inode, sector_t iblock,
			   struct buffer_head *result, int create)
{
	int ret;
	int err = 0;
	u64 blocknr;
	u64 extent_start = 0;
	u64 extent_end = 0;
	u64 objectid = inode->i_ino;
	u32 found_type;
	struct btrfs_path *path;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_file_extent_item *item;
	struct btrfs_leaf *leaf;
	struct btrfs_disk_key *found_key;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	if (create) {
		WARN_ON(1);
	}

	ret = btrfs_lookup_file_extent(NULL, root, path,
				       inode->i_ino,
				       iblock << inode->i_blkbits, 0);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	if (ret != 0) {
		if (path->slots[0] == 0) {
			btrfs_release_path(root, path);
			goto out;
		}
		path->slots[0]--;
	}

	item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			      struct btrfs_file_extent_item);
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	blocknr = btrfs_file_extent_disk_blocknr(item);
	blocknr += btrfs_file_extent_offset(item);

	/* are we inside the extent that was found? */
	found_key = &leaf->items[path->slots[0]].key;
	found_type = btrfs_disk_key_type(found_key);
	if (btrfs_disk_key_objectid(found_key) != objectid ||
	    found_type != BTRFS_EXTENT_DATA_KEY) {
		extent_end = 0;
		extent_start = 0;
		btrfs_release_path(root, path);
		goto out;
	}
	found_type = btrfs_file_extent_type(item);
	extent_start = btrfs_disk_key_offset(&leaf->items[path->slots[0]].key);
	if (found_type == BTRFS_FILE_EXTENT_REG) {
		extent_start = extent_start >> inode->i_blkbits;
		extent_end = extent_start + btrfs_file_extent_num_blocks(item);
		if (iblock >= extent_start && iblock < extent_end) {
			err = 0;
			btrfs_map_bh_to_logical(root, result, blocknr +
						iblock - extent_start);
			goto out;
		}
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		char *ptr;
		char *map;
		u32 size;
		size = btrfs_file_extent_inline_len(leaf->items +
						    path->slots[0]);
		extent_end = (extent_start + size) >> inode->i_blkbits;
		extent_start >>= inode->i_blkbits;
		if (iblock < extent_start || iblock > extent_end) {
			goto out;
		}
		ptr = btrfs_file_extent_inline_start(item);
		map = kmap(result->b_page);
		memcpy(map, ptr, size);
		memset(map + size, 0, PAGE_CACHE_SIZE - size);
		flush_dcache_page(result->b_page);
		kunmap(result->b_page);
		set_buffer_uptodate(result);
		SetPageChecked(result->b_page);
		btrfs_map_bh_to_logical(root, result, 0);
	}
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return err;
}

static int btrfs_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *result, int create)
{
	int err;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	mutex_lock(&root->fs_info->fs_mutex);
	err = btrfs_get_block_lock(inode, iblock, result, create);
	mutex_unlock(&root->fs_info->fs_mutex);
	return err;
}

static int btrfs_prepare_write(struct file *file, struct page *page,
			       unsigned from, unsigned to)
{
	return nobh_prepare_write(page, from, to, btrfs_get_block);
}

static void btrfs_write_super(struct super_block *sb)
{
	btrfs_sync_fs(sb, 1);
}

static int btrfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, btrfs_get_block);
}

/*
 * While block_write_full_page is writing back the dirty buffers under
 * the page lock, whoever dirtied the buffers may decide to clean them
 * again at any time.  We handle that by only looking at the buffer
 * state inside lock_buffer().
 *
 * If block_write_full_page() is called for regular writeback
 * (wbc->sync_mode == WB_SYNC_NONE) then it will redirty a page which has a
 * locked buffer.   This only can happen if someone has written the buffer
 * directly, with submit_bh().  At the address_space level PageWriteback
 * prevents this contention from occurring.
 */
static int __btrfs_write_full_page(struct inode *inode, struct page *page,
				   struct writeback_control *wbc)
{
	int err;
	sector_t block;
	sector_t last_block;
	struct buffer_head *bh, *head;
	const unsigned blocksize = 1 << inode->i_blkbits;
	int nr_underway = 0;

	BUG_ON(!PageLocked(page));

	last_block = (i_size_read(inode) - 1) >> inode->i_blkbits;

	if (!page_has_buffers(page)) {
		create_empty_buffers(page, blocksize,
					(1 << BH_Dirty)|(1 << BH_Uptodate));
	}

	/*
	 * Be very careful.  We have no exclusion from __set_page_dirty_buffers
	 * here, and the (potentially unmapped) buffers may become dirty at
	 * any time.  If a buffer becomes dirty here after we've inspected it
	 * then we just miss that fact, and the page stays dirty.
	 *
	 * Buffers outside i_size may be dirtied by __set_page_dirty_buffers;
	 * handle that here by just cleaning them.
	 */

	block = (sector_t)page->index << (PAGE_CACHE_SHIFT - inode->i_blkbits);
	head = page_buffers(page);
	bh = head;

	/*
	 * Get all the dirty buffers mapped to disk addresses and
	 * handle any aliases from the underlying blockdev's mapping.
	 */
	do {
		if (block > last_block) {
			/*
			 * mapped buffers outside i_size will occur, because
			 * this page can be outside i_size when there is a
			 * truncate in progress.
			 */
			/*
			 * The buffer was zeroed by block_write_full_page()
			 */
			clear_buffer_dirty(bh);
			set_buffer_uptodate(bh);
		} else if (!buffer_mapped(bh) && buffer_dirty(bh)) {
			WARN_ON(bh->b_size != blocksize);
			err = btrfs_get_block(inode, block, bh, 0);
			if (err)
				goto recover;
			if (buffer_new(bh)) {
				/* blockdev mappings never come here */
				clear_buffer_new(bh);
				unmap_underlying_metadata(bh->b_bdev,
							bh->b_blocknr);
			}
		}
		bh = bh->b_this_page;
		block++;
	} while (bh != head);

	do {
		if (!buffer_mapped(bh))
			continue;
		/*
		 * If it's a fully non-blocking write attempt and we cannot
		 * lock the buffer then redirty the page.  Note that this can
		 * potentially cause a busy-wait loop from pdflush and kswapd
		 * activity, but those code paths have their own higher-level
		 * throttling.
		 */
		if (wbc->sync_mode != WB_SYNC_NONE || !wbc->nonblocking) {
			lock_buffer(bh);
		} else if (test_set_buffer_locked(bh)) {
			redirty_page_for_writepage(wbc, page);
			continue;
		}
		if (test_clear_buffer_dirty(bh) && bh->b_blocknr != 0) {
			mark_buffer_async_write(bh);
		} else {
			unlock_buffer(bh);
		}
	} while ((bh = bh->b_this_page) != head);

	/*
	 * The page and its buffers are protected by PageWriteback(), so we can
	 * drop the bh refcounts early.
	 */
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);

	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			submit_bh(WRITE, bh);
			nr_underway++;
		}
		bh = next;
	} while (bh != head);
	unlock_page(page);

	err = 0;
done:
	if (nr_underway == 0) {
		/*
		 * The page was marked dirty, but the buffers were
		 * clean.  Someone wrote them back by hand with
		 * ll_rw_block/submit_bh.  A rare case.
		 */
		int uptodate = 1;
		do {
			if (!buffer_uptodate(bh)) {
				uptodate = 0;
				break;
			}
			bh = bh->b_this_page;
		} while (bh != head);
		if (uptodate)
			SetPageUptodate(page);
		end_page_writeback(page);
		/*
		 * The page and buffer_heads can be released at any time from
		 * here on.
		 */
		wbc->pages_skipped++;	/* We didn't write this page */
	}
	return err;

recover:
	/*
	 * ENOSPC, or some other error.  We may already have added some
	 * blocks to the file, so we need to write these out to avoid
	 * exposing stale data.
	 * The page is currently locked and not marked for writeback
	 */
	bh = head;
	/* Recovery: lock and submit the mapped buffers */
	do {
		if (buffer_mapped(bh) && buffer_dirty(bh)) {
			lock_buffer(bh);
			mark_buffer_async_write(bh);
		} else {
			/*
			 * The buffer may have been set dirty during
			 * attachment to a dirty page.
			 */
			clear_buffer_dirty(bh);
		}
	} while ((bh = bh->b_this_page) != head);
	SetPageError(page);
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			clear_buffer_dirty(bh);
			submit_bh(WRITE, bh);
			nr_underway++;
		}
		bh = next;
	} while (bh != head);
	unlock_page(page);
	goto done;
}

/*
 * The generic ->writepage function for buffer-backed address_spaces
 */
static int btrfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode * const inode = page->mapping->host;
	loff_t i_size = i_size_read(inode);
	const pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset;
	void *kaddr;

	/* Is the page fully inside i_size? */
	if (page->index < end_index)
		return __btrfs_write_full_page(inode, page, wbc);

	/* Is the page fully outside i_size? (truncate in progress) */
	offset = i_size & (PAGE_CACHE_SIZE-1);
	if (page->index >= end_index+1 || !offset) {
		/*
		 * The page may have dirty, unmapped buffers.  For example,
		 * they may have been added in ext3_writepage().  Make them
		 * freeable here, so the page does not leak.
		 */
		block_invalidatepage(page, 0);
		unlock_page(page);
		return 0; /* don't care */
	}

	/*
	 * The page straddles i_size.  It must be zeroed out on each and every
	 * writepage invokation because it may be mmapped.  "A file is mapped
	 * in multiples of the page size.  For a file that is not a multiple of
	 * the  page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are not written out to the file."
	 */
	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr + offset, 0, PAGE_CACHE_SIZE - offset);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);
	return __btrfs_write_full_page(inode, page, wbc);
}

static void btrfs_truncate(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;
	struct btrfs_trans_handle *trans;

	if (!S_ISREG(inode->i_mode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	nobh_truncate_page(inode->i_mapping, inode->i_size);

	/* FIXME, add redo link to tree so we don't leak on crash */
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	ret = btrfs_truncate_in_trans(trans, root, inode);
	BUG_ON(ret);
	ret = btrfs_end_transaction(trans, root);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->fs_mutex);
	mark_inode_dirty(inode);
}

/*
 * Make sure any changes to nobh_commit_write() are reflected in
 * nobh_truncate_page(), since it doesn't call commit_write().
 */
static int btrfs_commit_write(struct file *file, struct page *page,
			      unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	struct buffer_head *bh;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;

	SetPageUptodate(page);
	bh = page_buffers(page);
	if (buffer_mapped(bh) && bh->b_blocknr != 0) {
		set_page_dirty(page);
	}
	if (pos > inode->i_size) {
		i_size_write(inode, pos);
		mark_inode_dirty(inode);
	}
	return 0;
}

static int btrfs_copy_from_user(loff_t pos, int num_pages, int write_bytes,
				struct page **prepared_pages,
				const char __user * buf)
{
	long page_fault = 0;
	int i;
	int offset = pos & (PAGE_CACHE_SIZE - 1);

	for (i = 0; i < num_pages && write_bytes > 0; i++, offset = 0) {
		size_t count = min_t(size_t,
				     PAGE_CACHE_SIZE - offset, write_bytes);
		struct page *page = prepared_pages[i];
		fault_in_pages_readable(buf, count);

		/* Copy data from userspace to the current page */
		kmap(page);
		page_fault = __copy_from_user(page_address(page) + offset,
					      buf, count);
		/* Flush processor's dcache for this page */
		flush_dcache_page(page);
		kunmap(page);
		buf += count;
		write_bytes -= count;

		if (page_fault)
			break;
	}
	return page_fault ? -EFAULT : 0;
}

static void btrfs_drop_pages(struct page **pages, size_t num_pages)
{
	size_t i;
	for (i = 0; i < num_pages; i++) {
		if (!pages[i])
			break;
		unlock_page(pages[i]);
		mark_page_accessed(pages[i]);
		page_cache_release(pages[i]);
	}
}
static int dirty_and_release_pages(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct file *file,
				   struct page **pages,
				   size_t num_pages,
				   loff_t pos,
				   size_t write_bytes)
{
	int i;
	int offset;
	int err = 0;
	int ret;
	int this_write;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct buffer_head *bh;
	struct btrfs_file_extent_item *ei;

	for (i = 0; i < num_pages; i++) {
		offset = pos & (PAGE_CACHE_SIZE -1);
		this_write = min(PAGE_CACHE_SIZE - offset, write_bytes);
		/* FIXME, one block at a time */

		mutex_lock(&root->fs_info->fs_mutex);
		trans = btrfs_start_transaction(root, 1);
		btrfs_set_trans_block_group(trans, inode);

		bh = page_buffers(pages[i]);
		if (buffer_mapped(bh) && bh->b_blocknr == 0) {
			struct btrfs_key key;
			struct btrfs_path *path;
			char *ptr;
			u32 datasize;

			path = btrfs_alloc_path();
			BUG_ON(!path);
			key.objectid = inode->i_ino;
			key.offset = pages[i]->index << PAGE_CACHE_SHIFT;
			key.flags = 0;
			btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
			BUG_ON(write_bytes >= PAGE_CACHE_SIZE);
			datasize = offset +
				btrfs_file_extent_calc_inline_size(write_bytes);
			ret = btrfs_insert_empty_item(trans, root, path, &key,
						      datasize);
			BUG_ON(ret);
			ei = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
			       path->slots[0], struct btrfs_file_extent_item);
			btrfs_set_file_extent_generation(ei, trans->transid);
			btrfs_set_file_extent_type(ei,
						   BTRFS_FILE_EXTENT_INLINE);
			ptr = btrfs_file_extent_inline_start(ei);
			memcpy(ptr, bh->b_data, offset + write_bytes);
			mark_buffer_dirty(path->nodes[0]);
			btrfs_free_path(path);
		} else {
			btrfs_csum_file_block(trans, root, inode->i_ino,
				      pages[i]->index << PAGE_CACHE_SHIFT,
				      kmap(pages[i]), PAGE_CACHE_SIZE);
			kunmap(pages[i]);
		}
		SetPageChecked(pages[i]);
		btrfs_update_inode_block_group(trans, inode);
		ret = btrfs_end_transaction(trans, root);
		BUG_ON(ret);
		mutex_unlock(&root->fs_info->fs_mutex);

		ret = btrfs_commit_write(file, pages[i], offset,
					 offset + this_write);
		pos += this_write;
		if (ret) {
			err = ret;
			goto failed;
		}
		WARN_ON(this_write > write_bytes);
		write_bytes -= this_write;
	}
failed:
	return err;
}

static int drop_extents(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct inode *inode,
			  u64 start, u64 end)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_leaf *leaf;
	int slot;
	struct btrfs_file_extent_item *extent;
	u64 extent_end = 0;
	int keep;
	struct btrfs_file_extent_item old;
	struct btrfs_path *path;
	u64 search_start = start;
	int bookend;
	int found_type;
	int found_extent;
	int found_inline;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	while(1) {
		btrfs_release_path(root, path);
		ret = btrfs_lookup_file_extent(trans, root, path, inode->i_ino,
					       search_start, -1);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			if (path->slots[0] == 0) {
				ret = 0;
				goto out;
			}
			path->slots[0]--;
		}
		keep = 0;
		bookend = 0;
		found_extent = 0;
		found_inline = 0;
		extent = NULL;
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		slot = path->slots[0];
		btrfs_disk_key_to_cpu(&key, &leaf->items[slot].key);
		if (key.offset >= end || key.objectid != inode->i_ino) {
			ret = 0;
			goto out;
		}
		if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY) {
			ret = 0;
			goto out;
		}
		extent = btrfs_item_ptr(leaf, slot,
					struct btrfs_file_extent_item);
		found_type = btrfs_file_extent_type(extent);
		if (found_type == BTRFS_FILE_EXTENT_REG) {
			extent_end = key.offset +
				(btrfs_file_extent_num_blocks(extent) <<
				 inode->i_blkbits);
			found_extent = 1;
		} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
			found_inline = 1;
			extent_end = key.offset +
			     btrfs_file_extent_inline_len(leaf->items + slot);
		}

		if (!found_extent && !found_inline) {
			ret = 0;
			goto out;
		}

		if (search_start >= extent_end) {
			ret = 0;
			goto out;
		}

		search_start = extent_end;

		if (end < extent_end && end >= key.offset) {
			if (found_extent) {
				memcpy(&old, extent, sizeof(old));
				ret = btrfs_inc_extent_ref(trans, root,
				      btrfs_file_extent_disk_blocknr(&old),
				      btrfs_file_extent_disk_num_blocks(&old));
				BUG_ON(ret);
			}
			WARN_ON(found_inline);
			bookend = 1;
		}

		if (start > key.offset) {
			u64 new_num;
			u64 old_num;
			/* truncate existing extent */
			keep = 1;
			WARN_ON(start & (root->blocksize - 1));
			if (found_extent) {
				new_num = (start - key.offset) >>
					inode->i_blkbits;
				old_num = btrfs_file_extent_num_blocks(extent);
				inode->i_blocks -= (old_num - new_num) << 3;
				btrfs_set_file_extent_num_blocks(extent,
								 new_num);
				mark_buffer_dirty(path->nodes[0]);
			} else {
				WARN_ON(1);
				/*
				ret = btrfs_truncate_item(trans, root, path,
							  start - key.offset);
				BUG_ON(ret);
				*/
			}
		}
		if (!keep) {
			u64 disk_blocknr = 0;
			u64 disk_num_blocks = 0;
			u64 extent_num_blocks = 0;
			if (found_extent) {
				disk_blocknr =
				      btrfs_file_extent_disk_blocknr(extent);
				disk_num_blocks =
				      btrfs_file_extent_disk_num_blocks(extent);
				extent_num_blocks =
				      btrfs_file_extent_num_blocks(extent);
			}
			ret = btrfs_del_item(trans, root, path);
			BUG_ON(ret);
			btrfs_release_path(root, path);
			if (found_extent) {
				inode->i_blocks -=
				btrfs_file_extent_num_blocks(extent) << 3;
				ret = btrfs_free_extent(trans, root,
							disk_blocknr,
							disk_num_blocks, 0);
			}

			BUG_ON(ret);
			if (!bookend && search_start >= end) {
				ret = 0;
				goto out;
			}
			if (!bookend)
				continue;
		}
		if (bookend && found_extent) {
			/* create bookend */
			struct btrfs_key ins;
			ins.objectid = inode->i_ino;
			ins.offset = end;
			ins.flags = 0;
			btrfs_set_key_type(&ins, BTRFS_EXTENT_DATA_KEY);

			btrfs_release_path(root, path);
			ret = btrfs_insert_empty_item(trans, root, path, &ins,
						      sizeof(*extent));
			BUG_ON(ret);
			extent = btrfs_item_ptr(
				    btrfs_buffer_leaf(path->nodes[0]),
				    path->slots[0],
				    struct btrfs_file_extent_item);
			btrfs_set_file_extent_disk_blocknr(extent,
				    btrfs_file_extent_disk_blocknr(&old));
			btrfs_set_file_extent_disk_num_blocks(extent,
				    btrfs_file_extent_disk_num_blocks(&old));

			btrfs_set_file_extent_offset(extent,
				    btrfs_file_extent_offset(&old) +
				    ((end - key.offset) >> inode->i_blkbits));
			WARN_ON(btrfs_file_extent_num_blocks(&old) <
				(end - key.offset) >> inode->i_blkbits);
			btrfs_set_file_extent_num_blocks(extent,
				    btrfs_file_extent_num_blocks(&old) -
				    ((end - key.offset) >> inode->i_blkbits));

			btrfs_set_file_extent_type(extent,
						   BTRFS_FILE_EXTENT_REG);
			btrfs_set_file_extent_generation(extent,
				    btrfs_file_extent_generation(&old));
			btrfs_mark_buffer_dirty(path->nodes[0]);
			inode->i_blocks +=
				btrfs_file_extent_num_blocks(extent) << 3;
			ret = 0;
			goto out;
		}
	}
out:
	btrfs_free_path(path);
	return ret;
}

static int prepare_pages(struct btrfs_root *root,
			 struct file *file,
			 struct page **pages,
			 size_t num_pages,
			 loff_t pos,
			 unsigned long first_index,
			 unsigned long last_index,
			 size_t write_bytes,
			 u64 alloc_extent_start)
{
	int i;
	unsigned long index = pos >> PAGE_CACHE_SHIFT;
	struct inode *inode = file->f_path.dentry->d_inode;
	int offset;
	int err = 0;
	int this_write;
	struct buffer_head *bh;
	struct buffer_head *head;
	loff_t isize = i_size_read(inode);

	memset(pages, 0, num_pages * sizeof(struct page *));

	for (i = 0; i < num_pages; i++) {
		pages[i] = grab_cache_page(inode->i_mapping, index + i);
		if (!pages[i]) {
			err = -ENOMEM;
			goto failed_release;
		}
		offset = pos & (PAGE_CACHE_SIZE -1);
		this_write = min(PAGE_CACHE_SIZE - offset, write_bytes);
		create_empty_buffers(pages[i], root->fs_info->sb->s_blocksize,
				     (1 << BH_Uptodate));
		head = page_buffers(pages[i]);
		bh = head;
		do {
			err = btrfs_map_bh_to_logical(root, bh,
						      alloc_extent_start);
			BUG_ON(err);
			if (err)
				goto failed_truncate;
			bh = bh->b_this_page;
			if (alloc_extent_start)
				alloc_extent_start++;
		} while (bh != head);
		pos += this_write;
		WARN_ON(this_write > write_bytes);
		write_bytes -= this_write;
	}
	return 0;

failed_release:
	btrfs_drop_pages(pages, num_pages);
	return err;

failed_truncate:
	btrfs_drop_pages(pages, num_pages);
	if (pos > isize)
		vmtruncate(inode, isize);
	return err;
}

static ssize_t btrfs_file_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	loff_t pos;
	size_t num_written = 0;
	int err = 0;
	int ret = 0;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct page *pages[8];
	struct page *pinned[2] = { NULL, NULL };
	unsigned long first_index;
	unsigned long last_index;
	u64 start_pos;
	u64 num_blocks;
	u64 alloc_extent_start;
	struct btrfs_trans_handle *trans;
	struct btrfs_key ins;

	if (file->f_flags & O_DIRECT)
		return -EINVAL;
	pos = *ppos;
	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);
	current->backing_dev_info = inode->i_mapping->backing_dev_info;
	err = generic_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out;
	if (count == 0)
		goto out;
	err = remove_suid(file->f_path.dentry);
	if (err)
		goto out;
	file_update_time(file);

	start_pos = pos & ~((u64)PAGE_CACHE_SIZE - 1);
	num_blocks = (count + pos - start_pos + root->blocksize - 1) >>
			inode->i_blkbits;

	mutex_lock(&inode->i_mutex);
	first_index = pos >> PAGE_CACHE_SHIFT;
	last_index = (pos + count) >> PAGE_CACHE_SHIFT;

	if ((first_index << PAGE_CACHE_SHIFT) < inode->i_size &&
	    (pos & (PAGE_CACHE_SIZE - 1))) {
		pinned[0] = grab_cache_page(inode->i_mapping, first_index);
		if (!PageUptodate(pinned[0])) {
			ret = mpage_readpage(pinned[0], btrfs_get_block);
			BUG_ON(ret);
		} else {
			unlock_page(pinned[0]);
		}
	}
	if (first_index != last_index &&
	    (last_index << PAGE_CACHE_SHIFT) < inode->i_size &&
	    (count & (PAGE_CACHE_SIZE - 1))) {
		pinned[1] = grab_cache_page(inode->i_mapping, last_index);
		if (!PageUptodate(pinned[1])) {
			ret = mpage_readpage(pinned[1], btrfs_get_block);
			BUG_ON(ret);
		} else {
			unlock_page(pinned[1]);
		}
	}

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		err = -ENOMEM;
		mutex_unlock(&root->fs_info->fs_mutex);
		goto out_unlock;
	}
	btrfs_set_trans_block_group(trans, inode);
	/* FIXME blocksize != 4096 */
	inode->i_blocks += num_blocks << 3;
	if (start_pos < inode->i_size) {
		/* FIXME blocksize != pagesize */
		ret = drop_extents(trans, root, inode,
				   start_pos,
				   (pos + count + root->blocksize -1) &
				   ~((u64)root->blocksize - 1));
		BUG_ON(ret);
	}
	if (inode->i_size >= PAGE_CACHE_SIZE || pos + count < inode->i_size ||
	    pos + count - start_pos > BTRFS_MAX_INLINE_DATA_SIZE(root)) {
		ret = btrfs_alloc_extent(trans, root, inode->i_ino,
					 num_blocks, 1, (u64)-1, &ins);
		BUG_ON(ret);
		ret = btrfs_insert_file_extent(trans, root, inode->i_ino,
				       start_pos, ins.objectid, ins.offset);
		BUG_ON(ret);
	} else {
		ins.offset = 0;
		ins.objectid = 0;
	}
	BUG_ON(ret);
	alloc_extent_start = ins.objectid;
	btrfs_update_inode_block_group(trans, inode);
	ret = btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);

	while(count > 0) {
		size_t offset = pos & (PAGE_CACHE_SIZE - 1);
		size_t write_bytes = min(count, PAGE_CACHE_SIZE - offset);
		size_t num_pages = (write_bytes + PAGE_CACHE_SIZE - 1) >>
					PAGE_CACHE_SHIFT;

		memset(pages, 0, sizeof(pages));
		ret = prepare_pages(root, file, pages, num_pages,
				    pos, first_index, last_index,
				    write_bytes, alloc_extent_start);
		BUG_ON(ret);

		/* FIXME blocks != pagesize */
		if (alloc_extent_start)
			alloc_extent_start += num_pages;
		ret = btrfs_copy_from_user(pos, num_pages,
					   write_bytes, pages, buf);
		BUG_ON(ret);

		ret = dirty_and_release_pages(NULL, root, file, pages,
					      num_pages, pos, write_bytes);
		BUG_ON(ret);
		btrfs_drop_pages(pages, num_pages);

		buf += write_bytes;
		count -= write_bytes;
		pos += write_bytes;
		num_written += write_bytes;

		balance_dirty_pages_ratelimited(inode->i_mapping);
		cond_resched();
	}
out_unlock:
	mutex_unlock(&inode->i_mutex);
out:
	if (pinned[0])
		page_cache_release(pinned[0]);
	if (pinned[1])
		page_cache_release(pinned[1]);
	*ppos = pos;
	current->backing_dev_info = NULL;
	mark_inode_dirty(inode);
	return num_written ? num_written : err;
}

static int btrfs_read_actor(read_descriptor_t *desc, struct page *page,
			unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long left, count = desc->count;
	struct inode *inode = page->mapping->host;

	if (size > count)
		size = count;

	if (!PageChecked(page)) {
		/* FIXME, do it per block */
		struct btrfs_root *root = BTRFS_I(inode)->root;

		int ret = btrfs_csum_verify_file_block(root,
				  page->mapping->host->i_ino,
				  page->index << PAGE_CACHE_SHIFT,
				  kmap(page), PAGE_CACHE_SIZE);
		if (ret) {
			printk("failed to verify ino %lu page %lu\n",
			       page->mapping->host->i_ino,
			       page->index);
			memset(page_address(page), 0, PAGE_CACHE_SIZE);
		}
		SetPageChecked(page);
		kunmap(page);
	}
	/*
	 * Faults on the destination of a read are common, so do it before
	 * taking the kmap.
	 */
	if (!fault_in_pages_writeable(desc->arg.buf, size)) {
		kaddr = kmap_atomic(page, KM_USER0);
		left = __copy_to_user_inatomic(desc->arg.buf,
						kaddr + offset, size);
		kunmap_atomic(kaddr, KM_USER0);
		if (left == 0)
			goto success;
	}

	/* Do it the slow way */
	kaddr = kmap(page);
	left = __copy_to_user(desc->arg.buf, kaddr + offset, size);
	kunmap(page);

	if (left) {
		size -= left;
		desc->error = -EFAULT;
	}
success:
	desc->count = count - size;
	desc->written += size;
	desc->arg.buf += size;
	return size;
}

/**
 * btrfs_file_aio_read - filesystem read routine
 * @iocb:	kernel I/O control block
 * @iov:	io vector request
 * @nr_segs:	number of segments in the iovec
 * @pos:	current file position
 */
static ssize_t btrfs_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
				   unsigned long nr_segs, loff_t pos)
{
	struct file *filp = iocb->ki_filp;
	ssize_t retval;
	unsigned long seg;
	size_t count;
	loff_t *ppos = &iocb->ki_pos;

	count = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		const struct iovec *iv = &iov[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		count += iv->iov_len;
		if (unlikely((ssize_t)(count|iv->iov_len) < 0))
			return -EINVAL;
		if (access_ok(VERIFY_WRITE, iv->iov_base, iv->iov_len))
			continue;
		if (seg == 0)
			return -EFAULT;
		nr_segs = seg;
		count -= iv->iov_len;	/* This segment is no good */
		break;
	}
	retval = 0;
	if (count) {
		for (seg = 0; seg < nr_segs; seg++) {
			read_descriptor_t desc;

			desc.written = 0;
			desc.arg.buf = iov[seg].iov_base;
			desc.count = iov[seg].iov_len;
			if (desc.count == 0)
				continue;
			desc.error = 0;
			do_generic_file_read(filp, ppos, &desc,
					     btrfs_read_actor);
			retval += desc.written;
			if (desc.error) {
				retval = retval ?: desc.error;
				break;
			}
		}
	}
	return retval;
}

static int create_subvol(struct btrfs_root *root, char *name, int namelen)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_root_item root_item;
	struct btrfs_inode_item *inode_item;
	struct buffer_head *subvol;
	struct btrfs_leaf *leaf;
	struct btrfs_root *new_root;
	struct inode *inode;
	struct inode *dir;
	int ret;
	u64 objectid;
	u64 new_dirid = BTRFS_FIRST_FREE_OBJECTID;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);

	subvol = btrfs_alloc_free_block(trans, root, 0);
	if (subvol == NULL)
		return -ENOSPC;
	leaf = btrfs_buffer_leaf(subvol);
	btrfs_set_header_nritems(&leaf->header, 0);
	btrfs_set_header_level(&leaf->header, 0);
	btrfs_set_header_blocknr(&leaf->header, bh_blocknr(subvol));
	btrfs_set_header_generation(&leaf->header, trans->transid);
	btrfs_set_header_owner(&leaf->header, root->root_key.objectid);
	memcpy(leaf->header.fsid, root->fs_info->disk_super->fsid,
	       sizeof(leaf->header.fsid));
	mark_buffer_dirty(subvol);

	inode_item = &root_item.inode;
	memset(inode_item, 0, sizeof(*inode_item));
	btrfs_set_inode_generation(inode_item, 1);
	btrfs_set_inode_size(inode_item, 3);
	btrfs_set_inode_nlink(inode_item, 1);
	btrfs_set_inode_nblocks(inode_item, 1);
	btrfs_set_inode_mode(inode_item, S_IFDIR | 0755);

	btrfs_set_root_blocknr(&root_item, bh_blocknr(subvol));
	btrfs_set_root_refs(&root_item, 1);
	brelse(subvol);
	subvol = NULL;

	ret = btrfs_find_free_objectid(trans, root->fs_info->tree_root,
				       0, &objectid);
	BUG_ON(ret);

	btrfs_set_root_dirid(&root_item, new_dirid);

	key.objectid = objectid;
	key.offset = 1;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
	ret = btrfs_insert_root(trans, root->fs_info->tree_root, &key,
				&root_item);
	BUG_ON(ret);

	/*
	 * insert the directory item
	 */
	key.offset = (u64)-1;
	dir = root->fs_info->sb->s_root->d_inode;
	ret = btrfs_insert_dir_item(trans, root->fs_info->tree_root,
				    name, namelen, dir->i_ino, &key, 0);
	BUG_ON(ret);

	ret = btrfs_commit_transaction(trans, root);
	BUG_ON(ret);

	new_root = btrfs_read_fs_root(root->fs_info, &key);
	BUG_ON(!new_root);

	trans = btrfs_start_transaction(new_root, 1);
	BUG_ON(!trans);

	inode = btrfs_new_inode(trans, new_root, new_dirid,
				BTRFS_I(dir)->block_group, S_IFDIR | 0700);
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;

	ret = btrfs_make_empty_dir(trans, new_root, new_dirid, new_dirid);
	BUG_ON(ret);

	inode->i_nlink = 1;
	inode->i_size = 6;
	ret = btrfs_update_inode(trans, new_root, inode);
	BUG_ON(ret);

	ret = btrfs_commit_transaction(trans, new_root);
	BUG_ON(ret);

	iput(inode);

	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

static int create_snapshot(struct btrfs_root *root, char *name, int namelen)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_root_item new_root_item;
	int ret;
	u64 objectid;

	if (!root->ref_cows)
		return -EINVAL;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);

	ret = btrfs_update_inode(trans, root, root->inode);
	BUG_ON(ret);

	ret = btrfs_find_free_objectid(trans, root->fs_info->tree_root,
				       0, &objectid);
	BUG_ON(ret);

	memcpy(&new_root_item, &root->root_item,
	       sizeof(new_root_item));

	key.objectid = objectid;
	key.offset = 1;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
	btrfs_set_root_blocknr(&new_root_item, bh_blocknr(root->node));

	ret = btrfs_insert_root(trans, root->fs_info->tree_root, &key,
				&new_root_item);
	BUG_ON(ret);

	/*
	 * insert the directory item
	 */
	key.offset = (u64)-1;
	ret = btrfs_insert_dir_item(trans, root->fs_info->tree_root,
				    name, namelen,
				    root->fs_info->sb->s_root->d_inode->i_ino,
				    &key, 0);

	BUG_ON(ret);

	ret = btrfs_inc_root_ref(trans, root);
	BUG_ON(ret);

	ret = btrfs_commit_transaction(trans, root);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

static int add_disk(struct btrfs_root *root, char *name, int namelen)
{
	struct block_device *bdev;
	struct btrfs_path *path;
	struct super_block *sb = root->fs_info->sb;
	struct btrfs_root *dev_root = root->fs_info->dev_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_device_item *dev_item;
	struct btrfs_key key;
	u16 item_size;
	u64 num_blocks;
	u64 new_blocks;
	u64 device_id;
	int ret;

printk("adding disk %s\n", name);
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	num_blocks = btrfs_super_total_blocks(root->fs_info->disk_super);
	bdev = open_bdev_excl(name, O_RDWR, sb);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
printk("open bdev excl failed ret %d\n", ret);
		goto out_nolock;
	}
	set_blocksize(bdev, sb->s_blocksize);
	new_blocks = bdev->bd_inode->i_size >> sb->s_blocksize_bits;
	key.objectid = num_blocks;
	key.offset = new_blocks;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DEV_ITEM_KEY);

	mutex_lock(&dev_root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(dev_root, 1);
	item_size = sizeof(*dev_item) + namelen;
printk("insert empty on %Lu %Lu %u size %d\n", num_blocks, new_blocks, key.flags, item_size);
	ret = btrfs_insert_empty_item(trans, dev_root, path, &key, item_size);
	if (ret) {
printk("insert failed %d\n", ret);
		close_bdev_excl(bdev);
		if (ret > 0)
			ret = -EEXIST;
		goto out;
	}
	dev_item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
				  path->slots[0], struct btrfs_device_item);
	btrfs_set_device_pathlen(dev_item, namelen);
	memcpy(dev_item + 1, name, namelen);

	device_id = btrfs_super_last_device_id(root->fs_info->disk_super) + 1;
	btrfs_set_super_last_device_id(root->fs_info->disk_super, device_id);
	btrfs_set_device_id(dev_item, device_id);
	mark_buffer_dirty(path->nodes[0]);

	ret = btrfs_insert_dev_radix(root, bdev, device_id, num_blocks,
				     new_blocks);

	if (!ret) {
		btrfs_set_super_total_blocks(root->fs_info->disk_super,
					     num_blocks + new_blocks);
		i_size_write(root->fs_info->btree_inode,
			     (num_blocks + new_blocks) <<
			     root->fs_info->btree_inode->i_blkbits);
	}

out:
	ret = btrfs_commit_transaction(trans, dev_root);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->fs_mutex);
out_nolock:
	btrfs_free_path(path);

	return ret;
}

static int btrfs_ioctl(struct inode *inode, struct file *filp, unsigned int
		       cmd, unsigned long arg)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_vol_args vol_args;
	int ret = 0;
	struct btrfs_dir_item *di;
	int namelen;
	struct btrfs_path *path;
	u64 root_dirid;

	switch (cmd) {
	case BTRFS_IOC_SNAP_CREATE:
		if (copy_from_user(&vol_args,
				   (struct btrfs_ioctl_vol_args __user *)arg,
				   sizeof(vol_args)))
			return -EFAULT;
		namelen = strlen(vol_args.name);
		if (namelen > BTRFS_VOL_NAME_MAX)
			return -EINVAL;
		path = btrfs_alloc_path();
		if (!path)
			return -ENOMEM;
		root_dirid = root->fs_info->sb->s_root->d_inode->i_ino,
		mutex_lock(&root->fs_info->fs_mutex);
		di = btrfs_lookup_dir_item(NULL, root->fs_info->tree_root,
				    path, root_dirid,
				    vol_args.name, namelen, 0);
		mutex_unlock(&root->fs_info->fs_mutex);
		btrfs_free_path(path);
		if (di && !IS_ERR(di))
			return -EEXIST;

		if (root == root->fs_info->tree_root)
			ret = create_subvol(root, vol_args.name, namelen);
		else
			ret = create_snapshot(root, vol_args.name, namelen);
		WARN_ON(ret);
		break;
	case BTRFS_IOC_ADD_DISK:
		if (copy_from_user(&vol_args,
				   (struct btrfs_ioctl_vol_args __user *)arg,
				   sizeof(vol_args)))
			return -EFAULT;
		namelen = strlen(vol_args.name);
		if (namelen > BTRFS_VOL_NAME_MAX)
			return -EINVAL;
		vol_args.name[namelen] = '\0';
		ret = add_disk(root, vol_args.name, namelen);
		break;
	default:
		return -ENOTTY;
	}
	return ret;
}

static struct kmem_cache *btrfs_inode_cachep;
struct kmem_cache *btrfs_trans_handle_cachep;
struct kmem_cache *btrfs_transaction_cachep;
struct kmem_cache *btrfs_bit_radix_cachep;
struct kmem_cache *btrfs_path_cachep;

/*
 * Called inside transaction, so use GFP_NOFS
 */
static struct inode *btrfs_alloc_inode(struct super_block *sb)
{
	struct btrfs_inode *ei;

	ei = kmem_cache_alloc(btrfs_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void btrfs_destroy_inode(struct inode *inode)
{
	WARN_ON(!list_empty(&inode->i_dentry));
	WARN_ON(inode->i_data.nrpages);

	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}

static void init_once(void * foo, struct kmem_cache * cachep,
		      unsigned long flags)
{
	struct btrfs_inode *ei = (struct btrfs_inode *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&ei->vfs_inode);
	}
}

static int init_inodecache(void)
{
	btrfs_inode_cachep = kmem_cache_create("btrfs_inode_cache",
					     sizeof(struct btrfs_inode),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	btrfs_trans_handle_cachep = kmem_cache_create("btrfs_trans_handle_cache",
					     sizeof(struct btrfs_trans_handle),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     NULL, NULL);
	btrfs_transaction_cachep = kmem_cache_create("btrfs_transaction_cache",
					     sizeof(struct btrfs_transaction),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     NULL, NULL);
	btrfs_path_cachep = kmem_cache_create("btrfs_path_cache",
					     sizeof(struct btrfs_transaction),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     NULL, NULL);
	btrfs_bit_radix_cachep = kmem_cache_create("btrfs_radix",
					     256,
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD |
						SLAB_DESTROY_BY_RCU),
					     NULL, NULL);
	if (btrfs_inode_cachep == NULL || btrfs_trans_handle_cachep == NULL ||
	    btrfs_transaction_cachep == NULL || btrfs_bit_radix_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(btrfs_inode_cachep);
	kmem_cache_destroy(btrfs_trans_handle_cachep);
	kmem_cache_destroy(btrfs_transaction_cachep);
	kmem_cache_destroy(btrfs_bit_radix_cachep);
	kmem_cache_destroy(btrfs_path_cachep);
}

static int btrfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data,
			   btrfs_fill_super, mnt);
}


static int btrfs_getattr(struct vfsmount *mnt,
			 struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blksize = 256 * 1024;
	return 0;
}

static int btrfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct btrfs_root *root = btrfs_sb(dentry->d_sb);
	struct btrfs_super_block *disk_super = root->fs_info->disk_super;

	buf->f_namelen = BTRFS_NAME_LEN;
	buf->f_blocks = btrfs_super_total_blocks(disk_super);
	buf->f_bfree = buf->f_blocks - btrfs_super_blocks_used(disk_super);
	buf->f_bavail = buf->f_bfree;
	buf->f_bsize = dentry->d_sb->s_blocksize;
	buf->f_type = BTRFS_SUPER_MAGIC;
	return 0;
}

static struct file_system_type btrfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "btrfs",
	.get_sb		= btrfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static struct super_operations btrfs_super_ops = {
	.delete_inode	= btrfs_delete_inode,
	.put_super	= btrfs_put_super,
	.read_inode	= btrfs_read_locked_inode,
	.write_super	= btrfs_write_super,
	.sync_fs	= btrfs_sync_fs,
	.write_inode	= btrfs_write_inode,
	.dirty_inode	= btrfs_dirty_inode,
	.alloc_inode	= btrfs_alloc_inode,
	.destroy_inode	= btrfs_destroy_inode,
	.statfs		= btrfs_statfs,
};

static struct inode_operations btrfs_dir_inode_operations = {
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
	.unlink		= btrfs_unlink,
	.mkdir		= btrfs_mkdir,
	.rmdir		= btrfs_rmdir,
};

static struct inode_operations btrfs_dir_ro_inode_operations = {
	.lookup		= btrfs_lookup,
};

static struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= btrfs_readdir,
	.ioctl		= btrfs_ioctl,
};

static struct address_space_operations btrfs_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.sync_page	= block_sync_page,
	.prepare_write	= btrfs_prepare_write,
	.commit_write	= btrfs_commit_write,
};

static struct inode_operations btrfs_file_inode_operations = {
	.truncate	= btrfs_truncate,
	.getattr	= btrfs_getattr,
};

static struct file_operations btrfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read       = btrfs_file_aio_read,
	.write		= btrfs_file_write,
	.mmap		= generic_file_mmap,
	.open		= generic_file_open,
	.ioctl		= btrfs_ioctl,
	.fsync		= btrfs_sync_file,
};

static int __init init_btrfs_fs(void)
{
	int err;
	printk("btrfs loaded!\n");
	err = init_inodecache();
	if (err)
		return err;
	kset_set_kset_s(&btrfs_subsys, fs_subsys);
	err = subsystem_register(&btrfs_subsys);
	if (err)
		goto out;
	return register_filesystem(&btrfs_fs_type);
out:
	destroy_inodecache();
	return err;
}

static void __exit exit_btrfs_fs(void)
{
	destroy_inodecache();
	unregister_filesystem(&btrfs_fs_type);
	subsystem_unregister(&btrfs_subsys);
	printk("btrfs unloaded\n");
}

module_init(init_btrfs_fs)
module_exit(exit_btrfs_fs)

MODULE_LICENSE("GPL");
