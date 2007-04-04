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
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"

#define BTRFS_SUPER_MAGIC 0x9123682E

static struct inode_operations btrfs_dir_inode_operations;
static struct super_operations btrfs_super_ops;
static struct file_operations btrfs_dir_file_operations;
static struct inode_operations btrfs_file_inode_operations;
static struct address_space_operations btrfs_aops;
static struct file_operations btrfs_file_operations;

static int check_inode(struct inode *inode)
{
	struct btrfs_inode *ei = BTRFS_I(inode);
	WARN_ON(ei->magic != 0xDEADBEEF);
	WARN_ON(ei->magic2 != 0xDEADBEAF);
	return 0;
}

static void btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_path *path;
	struct btrfs_inode_item *inode_item;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	mutex_lock(&root->fs_info->fs_mutex);

	check_inode(inode);
	ret = btrfs_lookup_inode(NULL, root, path, inode->i_ino, 0);
	if (ret) {
		btrfs_release_path(root, path);
		btrfs_free_path(path);
		mutex_unlock(&root->fs_info->fs_mutex);
		make_bad_inode(inode);
		return;
	}
	check_inode(inode);
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

	btrfs_release_path(root, path);
	btrfs_free_path(path);
	inode_item = NULL;

	mutex_unlock(&root->fs_info->fs_mutex);
	check_inode(inode);
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
		inode->i_op = &btrfs_dir_inode_operations;
		inode->i_fop = &btrfs_dir_file_operations;
		break;
	case S_IFLNK:
		// inode->i_op = &page_symlink_inode_operations;
		break;
	}
	check_inode(inode);
	return;
}

static int btrfs_unlink_trans(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct inode *dir,
			      struct dentry *dentry)
{
	struct btrfs_path *path;
	const char *name = dentry->d_name.name;
	int name_len = dentry->d_name.len;
	int ret;
	u64 objectid;
	struct btrfs_dir_item *di;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	ret = btrfs_lookup_dir_item(trans, root, path, dir->i_ino,
				    name, name_len, -1);
	if (ret < 0)
		goto err;
	if (ret > 0) {
		ret = -ENOENT;
		goto err;
	}
	di = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			    struct btrfs_dir_item);
	objectid = btrfs_dir_objectid(di);

	ret = btrfs_del_item(trans, root, path);
	BUG_ON(ret);
	dentry->d_inode->i_ctime = dir->i_ctime;
err:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	if (ret == 0) {
		inode_dec_link_count(dentry->d_inode);
		dir->i_size -= name_len;
		mark_inode_dirty(dir);
	}
	return ret;
}

static int btrfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_root *root;
	struct btrfs_trans_handle *trans;
	int ret;

	root = btrfs_sb(dir->i_sb);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
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
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_trans_handle *trans;
	struct btrfs_disk_key *found_key;
	struct btrfs_leaf *leaf;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	key.objectid = inode->i_ino;
	key.offset = (u64)-1;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	BUG_ON(ret == 0);
	BUG_ON(path->slots[0] == 0);
	path->slots[0]--;
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	found_key = &leaf->items[path->slots[0]].key;
	if (btrfs_disk_key_objectid(found_key) != inode->i_ino) {
		err = -ENOENT;
		goto out;
	}
	if (btrfs_disk_key_type(found_key) != BTRFS_DIR_ITEM_KEY ||
	    btrfs_disk_key_offset(found_key) != 2) {
		err = -ENOTEMPTY;
		goto out;
	}
	ret = btrfs_del_item(trans, root, path);
	BUG_ON(ret);
	btrfs_release_path(root, path);
	key.offset = 1;
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	if (ret > 0) {
		err = -ENOTEMPTY;
		goto out;
	}
	ret = btrfs_del_item(trans, root, path);
	if (ret) {
		err = ret;
		goto out;
	}
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
	u64 objectid = inode->i_ino;
	struct btrfs_path *path;
	struct btrfs_inode_map_item *map;
	struct btrfs_key stat_data_key;
	int ret;

	clear_inode(inode);

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	ret = btrfs_lookup_inode_map(trans, root, path, objectid, -1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto error;
	}
	map = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			    struct btrfs_inode_map_item);
	btrfs_disk_key_to_cpu(&stat_data_key, &map->key);
	ret = btrfs_del_item(trans, root->fs_info->inode_root, path);
	BUG_ON(ret);
	btrfs_release_path(root, path);

	ret = btrfs_lookup_inode(trans, root, path, objectid, -1);
	BUG_ON(ret);
	ret = btrfs_del_item(trans, root, path);
	BUG_ON(ret);
error:
	btrfs_release_path(root, path);
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
		    btrfs_disk_key_type(found_key) != BTRFS_INLINE_DATA_KEY &&
		    btrfs_disk_key_type(found_key) != BTRFS_EXTENT_DATA_KEY)
			break;
		if (btrfs_disk_key_offset(found_key) < inode->i_size)
			break;
		if (btrfs_disk_key_type(found_key) == BTRFS_EXTENT_DATA_KEY) {
			fi = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
					    path->slots[0],
					    struct btrfs_file_extent_item);
			extent_start = btrfs_file_extent_disk_blocknr(fi);
			extent_num_blocks =
				btrfs_file_extent_disk_num_blocks(fi);
			inode->i_blocks -=
				btrfs_file_extent_num_blocks(fi) >> 9;
			found_extent = 1;
		} else {
			found_extent = 0;
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
	return ret;
}

static void btrfs_delete_inode(struct inode *inode)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	int ret;

	truncate_inode_pages(&inode->i_data, 0);
	if (is_bad_inode(inode)) {
		goto no_delete;
	}
	inode->i_size = 0;
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
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
			      ino_t *ino)
{
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	int ret;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	ret = btrfs_lookup_dir_item(NULL, root, path, dir->i_ino, name,
				    namelen, 0);
	if (ret || !btrfs_match_dir_item_name(root, path, name, namelen)) {
		*ino = 0;
		ret = 0;
		goto out;
	}
	di = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			    struct btrfs_dir_item);
	*ino = btrfs_dir_objectid(di);
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	check_inode(dir);
	return ret;
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode * inode;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	ino_t ino;
	int ret;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);
	mutex_lock(&root->fs_info->fs_mutex);
	ret = btrfs_inode_by_name(dir, dentry, &ino);
	mutex_unlock(&root->fs_info->fs_mutex);
	if (ret < 0)
		return ERR_PTR(ret);
	inode = NULL;
	if (ino) {
		inode = iget(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-EACCES);
		check_inode(inode);
	}
	check_inode(dir);
	return d_splice_alias(inode, dentry);
}

static int btrfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
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

	mutex_lock(&root->fs_info->fs_mutex);
	key.objectid = inode->i_ino;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	key.offset = filp->f_pos;
	path = btrfs_alloc_path();
	btrfs_init_path(path);
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto err;
	}
	advance = 0;
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
			} else {
				slot++;
				path->slots[0]++;
			}
		}
		advance = 1;
		item = leaf->items + slot;
		if (btrfs_disk_key_objectid(&item->key) != key.objectid)
			break;
		if (btrfs_disk_key_type(&item->key) != BTRFS_DIR_ITEM_KEY)
			continue;
		if (btrfs_disk_key_offset(&item->key) < filp->f_pos)
			continue;

		advance = 1;
		di = btrfs_item_ptr(leaf, slot, struct btrfs_dir_item);
		over = filldir(dirent, (const char *)(di + 1),
			       btrfs_dir_name_len(di),
			       btrfs_disk_key_offset(&item->key),
			       btrfs_dir_objectid(di), d_type);
		if (over) {
			filp->f_pos = btrfs_disk_key_offset(&item->key);
			break;
		}
		filp->f_pos = btrfs_disk_key_offset(&item->key) + 1;
	}
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
	struct btrfs_root *root;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_magic = BTRFS_SUPER_MAGIC;
	sb->s_op = &btrfs_super_ops;
	sb->s_time_gran = 1;

	root = open_ctree(sb);

	if (!root) {
		printk("btrfs: open_ctree failed\n");
		return -EIO;
	}
	sb->s_fs_info = root;
	disk_super = root->fs_info->disk_super;
	printk("read in super total blocks %Lu root %Lu\n",
	       btrfs_super_total_blocks(disk_super),
	       btrfs_super_root_dir(disk_super));

	inode = iget_locked(sb, btrfs_super_root_dir(disk_super));
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
	check_inode(inode);
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

	ret = btrfs_lookup_inode(trans, root, path, inode->i_ino, 1);
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
failed:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	check_inode(inode);
	return 0;
}

static int btrfs_write_inode(struct inode *inode, int wait)
{
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	struct btrfs_trans_handle *trans;
	int ret;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_update_inode(trans, root, inode);
	if (wait)
		btrfs_commit_transaction(trans, root);
	else
		btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	check_inode(inode);
	return ret;
}

static struct inode *btrfs_new_inode(struct btrfs_trans_handle *trans,
				     struct inode *dir, int mode)
{
	struct inode *inode;
	struct btrfs_inode_item inode_item;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	struct btrfs_key key;
	int ret;
	u64 objectid;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	check_inode(inode);
	ret = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	BUG_ON(ret);

	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_mode = mode;
	inode->i_ino = objectid;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	fill_inode_item(&inode_item, inode);

	key.objectid = objectid;
	key.flags = 0;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	ret = btrfs_insert_inode_map(trans, root, objectid, &key);
	BUG_ON(ret);

	ret = btrfs_insert_inode(trans, root, objectid, &inode_item);
	BUG_ON(ret);

	insert_inode_hash(inode);
	check_inode(inode);
	check_inode(dir);
	return inode;
}

static int btrfs_add_link(struct btrfs_trans_handle *trans,
			    struct dentry *dentry, struct inode *inode)
{
	int ret;
	ret = btrfs_insert_dir_item(trans, btrfs_sb(inode->i_sb),
				    dentry->d_name.name, dentry->d_name.len,
				    dentry->d_parent->d_inode->i_ino,
				    inode->i_ino, 0);
	if (ret == 0) {
		dentry->d_parent->d_inode->i_size += dentry->d_name.len;
		ret = btrfs_update_inode(trans, btrfs_sb(inode->i_sb),
					 dentry->d_parent->d_inode);
	}
	check_inode(inode);
	check_inode(dentry->d_parent->d_inode);
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
	check_inode(inode);
	return err;
}

static int btrfs_create(struct inode *dir, struct dentry *dentry,
			int mode, struct nameidata *nd)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	struct inode *inode;
	int err;
	int drop_inode = 0;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	inode = btrfs_new_inode(trans, dir, mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_unlock;
	// FIXME mark the inode dirty
	err = btrfs_add_nondir(trans, dentry, inode);
	if (err)
		drop_inode = 1;
	else {
		inode->i_mapping->a_ops = &btrfs_aops;
		inode->i_fop = &btrfs_file_operations;
		inode->i_op = &btrfs_file_inode_operations;
	}
	dir->i_sb->s_dirt = 1;
out_unlock:
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	check_inode(inode);
	check_inode(dir);

	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	return err;
}

static int btrfs_make_empty_dir(struct btrfs_trans_handle *trans,
				struct inode *inode, struct inode *dir)
{
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	int ret;
	char buf[2];
	buf[0] = '.';
	buf[1] = '.';

	ret = btrfs_insert_dir_item(trans, root, buf, 1, inode->i_ino,
				    inode->i_ino, 1);
	if (ret)
		goto error;
	ret = btrfs_insert_dir_item(trans, root, buf, 2, inode->i_ino,
				    dir->i_ino, 1);
	if (ret)
		goto error;
	inode->i_size = 3;
	ret = btrfs_update_inode(trans, root, inode);
error:
	return ret;
}

static int btrfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	int err = 0;
	int drop_on_err = 0;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_unlock;
	}
	inode = btrfs_new_inode(trans, dir, S_IFDIR | mode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_fail;
	}
	drop_on_err = 1;
	inode->i_op = &btrfs_dir_inode_operations;
	inode->i_fop = &btrfs_dir_file_operations;

	err = btrfs_make_empty_dir(trans, inode, dir);
	if (err)
		goto out_fail;
	err = btrfs_add_link(trans, dentry, inode);
	if (err)
		goto out_fail;
	d_instantiate(dentry, inode);
	drop_on_err = 0;

out_fail:
	btrfs_end_transaction(trans, root);
out_unlock:
	mutex_unlock(&root->fs_info->fs_mutex);
	if (drop_on_err)
		iput(inode);
	return err;
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
	filemap_write_and_wait(root->fs_info->btree_inode->i_mapping);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	sb->s_dirt = 0;
	BUG_ON(ret);
printk("btrfs sync_fs\n");
	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

static int btrfs_get_block_inline(struct inode *inode, sector_t iblock,
			   struct buffer_head *result, int create)
{
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_leaf *leaf;
	int num_bytes = result->b_size;
	int item_size;
	int ret;
	u64 pos;
	char *ptr;
	int copy_size;
	int err = 0;
	char *safe_ptr;
	char *data_ptr;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	WARN_ON(create);
	if (create) {
		return 0;
	}
	pos = iblock << inode->i_blkbits;
	key.objectid = inode->i_ino;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_INLINE_DATA_KEY);
	ptr = kmap(result->b_page);
	safe_ptr = ptr;
	ptr += (pos & (PAGE_CACHE_SIZE -1));
again:
	key.offset = pos;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret) {
		if (ret < 0)
			err = ret;
		else
			err = 0;
		goto out;
	}
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	item_size = btrfs_item_size(leaf->items + path->slots[0]);
	copy_size = min(num_bytes, item_size);
	data_ptr = btrfs_item_ptr(leaf, path->slots[0], char);
	WARN_ON(safe_ptr + PAGE_CACHE_SIZE < ptr + copy_size);
	memcpy(ptr, data_ptr, copy_size);
	pos += copy_size;
	num_bytes -= copy_size;
	WARN_ON(num_bytes < 0);
	ptr += copy_size;
	btrfs_release_path(root, path);
	if (num_bytes != 0) {
		if (pos >= i_size_read(inode))
			memset(ptr, 0, num_bytes);
		else
			goto again;
	}
	set_buffer_uptodate(result);
	map_bh(result, inode->i_sb, 0);
	err = 0;
out:
	btrfs_free_path(path);
	kunmap(result->b_page);
	return err;
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
	struct btrfs_path *path;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_file_extent_item *item;
	struct btrfs_leaf *leaf;
	struct btrfs_disk_key *found_key;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	if (create)
		trans = btrfs_start_transaction(root, 1);


	ret = btrfs_lookup_file_extent(trans, root, path,
				       inode->i_ino,
				       iblock << inode->i_blkbits, 0);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	if (ret != 0) {
		if (path->slots[0] == 0) {
			btrfs_release_path(root, path);
			goto allocate;
		}
		path->slots[0]--;
	}

	item = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]), path->slots[0],
			      struct btrfs_file_extent_item);
	leaf = btrfs_buffer_leaf(path->nodes[0]);
	blocknr = btrfs_file_extent_disk_blocknr(item);
	blocknr += btrfs_file_extent_offset(item);

	/* exact match found, use it */
	if (ret == 0) {
		err = 0;
		map_bh(result, inode->i_sb, blocknr);
		goto out;
	}

	/* are we inside the extent that was found? */
	found_key = &leaf->items[path->slots[0]].key;
	if (btrfs_disk_key_objectid(found_key) != objectid ||
	    btrfs_disk_key_type(found_key) != BTRFS_EXTENT_DATA_KEY) {
		extent_end = 0;
		extent_start = 0;
		btrfs_release_path(root, path);
		goto allocate;
	}

	extent_start = btrfs_disk_key_offset(&leaf->items[path->slots[0]].key);
	extent_start = extent_start >> inode->i_blkbits;
	extent_start += btrfs_file_extent_offset(item);
	extent_end = extent_start + btrfs_file_extent_num_blocks(item);
	if (iblock >= extent_start && iblock < extent_end) {
		err = 0;
		map_bh(result, inode->i_sb, blocknr + iblock - extent_start);
		goto out;
	}
allocate:
	/* ok, create a new extent */
	if (!create) {
		err = 0;
		goto out;
	}
	ret = btrfs_alloc_file_extent(trans, root, objectid,
				      iblock << inode->i_blkbits,
				      1, extent_end, &blocknr);
	if (ret) {
		err = ret;
		goto out;
	}
	inode->i_blocks += inode->i_sb->s_blocksize >> 9;
	set_buffer_new(result);
	map_bh(result, inode->i_sb, blocknr);

out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	if (trans)
		btrfs_end_transaction(trans, root);
	return err;
}

static int btrfs_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *result, int create)
{
	int err;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	mutex_lock(&root->fs_info->fs_mutex);
	// err = btrfs_get_block_lock(inode, iblock, result, create);
	err = btrfs_get_block_inline(inode, iblock, result, create);
	mutex_unlock(&root->fs_info->fs_mutex);
	return err;
}

static int btrfs_prepare_write(struct file *file, struct page *page,
			       unsigned from, unsigned to)
{
	WARN_ON(1);
	return nobh_prepare_write(page, from, to, btrfs_get_block);
}
static int btrfs_commit_write(struct file *file, struct page *page,
			       unsigned from, unsigned to)
{
	WARN_ON(1);
	return nobh_commit_write(file, page, from, to);
}

static void btrfs_write_super(struct super_block *sb)
{
	btrfs_sync_fs(sb, 1);
}

static int btrfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, btrfs_get_block);
}

static int btrfs_readpages(struct file *file, struct address_space *mapping,
			   struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, btrfs_get_block);
}

static int btrfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return nobh_writepage(page, btrfs_get_block, wbc);
}

static void btrfs_truncate(struct inode *inode)
{
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	int ret;
	struct btrfs_trans_handle *trans;

	if (!S_ISREG(inode->i_mode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	// nobh_truncate_page(inode->i_mapping, inode->i_size);

	/* FIXME, add redo link to tree so we don't leak on crash */
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_truncate_in_trans(trans, root, inode);
	BUG_ON(ret);
	ret = btrfs_end_transaction(trans, root);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->fs_mutex);
	mark_inode_dirty(inode);
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

	for (i = 0; i < num_pages; i++) {
		offset = pos & (PAGE_CACHE_SIZE -1);
		this_write = min(PAGE_CACHE_SIZE - offset, write_bytes);
		/* FIXME, one block at a time */

		mutex_lock(&root->fs_info->fs_mutex);
		trans = btrfs_start_transaction(root, 1);
		btrfs_csum_file_block(trans, root, inode->i_ino,
				      pages[i]->index << PAGE_CACHE_SHIFT,
				      kmap(pages[i]), PAGE_CACHE_SIZE);
		kunmap(pages[i]);
		SetPageChecked(pages[i]);
		ret = btrfs_end_transaction(trans, root);
		BUG_ON(ret);
		mutex_unlock(&root->fs_info->fs_mutex);

		ret = nobh_commit_write(file, pages[i], offset,
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

static int prepare_pages(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 struct file *file,
			 struct page **pages,
			 size_t num_pages,
			 loff_t pos,
			 size_t write_bytes)
{
	int i;
	unsigned long index = pos >> PAGE_CACHE_SHIFT;
	struct inode *inode = file->f_path.dentry->d_inode;
	int offset;
	int err = 0;
	int ret;
	int this_write;
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
		ret = nobh_prepare_write(pages[i], offset,
					 offset + this_write,
					 btrfs_get_block);
		pos += this_write;
		if (ret) {
			err = ret;
			goto failed_truncate;
		}
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
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	struct page *pages[1];

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
	mutex_lock(&inode->i_mutex);
	while(count > 0) {
		size_t offset = pos & (PAGE_CACHE_SIZE - 1);
		size_t write_bytes = min(count, PAGE_CACHE_SIZE - offset);
		size_t num_pages = (write_bytes + PAGE_CACHE_SIZE - 1) >>
					PAGE_CACHE_SHIFT;
		ret = prepare_pages(NULL, root, file, pages, num_pages,
				    pos, write_bytes);
		BUG_ON(ret);
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
	mutex_unlock(&inode->i_mutex);
out:
	*ppos = pos;
	current->backing_dev_info = NULL;
	return num_written ? num_written : err;
}

static ssize_t inline_one_page(struct btrfs_root *root, struct inode *inode,
			   struct page *page, loff_t pos,
			   size_t offset, size_t write_bytes)
{
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_leaf *leaf;
	struct btrfs_key found_key;
	int ret;
	size_t copy_size = 0;
	char *dst = NULL;
	int err = 0;
	size_t num_written = 0;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	key.objectid = inode->i_ino;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_INLINE_DATA_KEY);

again:
	key.offset = pos;
	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	if (ret == 0) {
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		btrfs_disk_key_to_cpu(&found_key,
				      &leaf->items[path->slots[0]].key);
		copy_size = btrfs_item_size(leaf->items + path->slots[0]);
		dst = btrfs_item_ptr(leaf, path->slots[0], char);
		copy_size = min(write_bytes, copy_size);
		goto copyit;
	} else {
		int slot = path->slots[0];
		if (slot > 0) {
			slot--;
		}
		// FIXME find max key
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		btrfs_disk_key_to_cpu(&found_key,
				      &leaf->items[slot].key);
		if (found_key.objectid != inode->i_ino)
			goto insert;
		if (btrfs_key_type(&found_key) != BTRFS_INLINE_DATA_KEY)
			goto insert;
		copy_size = btrfs_item_size(leaf->items + slot);
		if (found_key.offset + copy_size <= pos)
			goto insert;
		dst = btrfs_item_ptr(leaf, path->slots[0], char);
		dst += pos - found_key.offset;
		copy_size = copy_size - (pos - found_key.offset);
		BUG_ON(copy_size < 0);
		copy_size = min(write_bytes, copy_size);
		WARN_ON(copy_size == 0);
		goto copyit;
	}
insert:
	btrfs_release_path(root, path);
	copy_size = min(write_bytes,
			(size_t)BTRFS_LEAF_DATA_SIZE(root) -
			sizeof(struct btrfs_item) * 4);
	ret = btrfs_insert_empty_item(trans, root, path, &key, copy_size);
	BUG_ON(ret);
	dst = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
			     path->slots[0], char);
copyit:
	WARN_ON(copy_size == 0);
	WARN_ON(dst + copy_size >
		btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
						 path->slots[0], char) +
		btrfs_item_size(btrfs_buffer_leaf(path->nodes[0])->items +
						  path->slots[0]));
	btrfs_memcpy(root, path->nodes[0]->b_data, dst,
		     page_address(page) + offset, copy_size);
	mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(root, path);
	pos += copy_size;
	offset += copy_size;
	num_written += copy_size;
	write_bytes -= copy_size;
	if (write_bytes)
		goto again;
out:
	btrfs_free_path(path);
	ret = btrfs_end_transaction(trans, root);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->fs_mutex);
	return num_written ? num_written : err;
}

static ssize_t btrfs_file_inline_write(struct file *file,
				       const char __user *buf,
				       size_t count, loff_t *ppos)
{
	loff_t pos;
	size_t num_written = 0;
	int err = 0;
	int ret = 0;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	unsigned long page_index;

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
	mutex_lock(&inode->i_mutex);
	while(count > 0) {
		size_t offset = pos & (PAGE_CACHE_SIZE - 1);
		size_t write_bytes = min(count, PAGE_CACHE_SIZE - offset);
		struct page *page;

		page_index = pos >> PAGE_CACHE_SHIFT;
		page = grab_cache_page(inode->i_mapping, page_index);
		if (!PageUptodate(page)) {
			ret = mpage_readpage(page, btrfs_get_block);
			BUG_ON(ret);
			lock_page(page);
		}
		ret = btrfs_copy_from_user(pos, 1,
					   write_bytes, &page, buf);
		BUG_ON(ret);
		write_bytes = inline_one_page(root, inode, page, pos,
				      offset, write_bytes);
		SetPageUptodate(page);
		if (write_bytes > 0 && pos + write_bytes > inode->i_size) {
			i_size_write(inode, pos + write_bytes);
			mark_inode_dirty(inode);
		}
		page_cache_release(page);
		unlock_page(page);
		if (write_bytes < 0)
			goto out_unlock;
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
	*ppos = pos;
	current->backing_dev_info = NULL;
	return num_written ? num_written : err;
}

static int btrfs_read_actor(read_descriptor_t *desc, struct page *page,
			unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long left, count = desc->count;

	if (size > count)
		size = count;

	if (!PageChecked(page)) {
		/* FIXME, do it per block */
		struct btrfs_root *root = btrfs_sb(page->mapping->host->i_sb);
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
	ei->magic = 0xDEADBEEF;
	ei->magic2 = 0xDEADBEAF;
	return &ei->vfs_inode;
}

static void btrfs_destroy_inode(struct inode *inode)
{
	struct btrfs_inode *ei = BTRFS_I(inode);
	WARN_ON(ei->magic != 0xDEADBEEF);
	WARN_ON(ei->magic2 != 0xDEADBEAF);
	WARN_ON(!list_empty(&inode->i_dentry));
	WARN_ON(inode->i_data.nrpages);

	ei->magic = 0;
	ei->magic2 = 0;
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

static struct file_system_type btrfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "btrfs",
	.get_sb		= btrfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static struct super_operations btrfs_super_ops = {
	.statfs		= simple_statfs,
	.delete_inode	= btrfs_delete_inode,
	.put_super	= btrfs_put_super,
	.read_inode	= btrfs_read_locked_inode,
	.write_super	= btrfs_write_super,
	.sync_fs	= btrfs_sync_fs,
	.write_inode	= btrfs_write_inode,
	.alloc_inode	= btrfs_alloc_inode,
	.destroy_inode	= btrfs_destroy_inode,
};

static struct inode_operations btrfs_dir_inode_operations = {
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
	.unlink		= btrfs_unlink,
	.mkdir		= btrfs_mkdir,
	.rmdir		= btrfs_rmdir,
};

static struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= btrfs_readdir,
};

static struct address_space_operations btrfs_aops = {
	.readpage	= btrfs_readpage,
	// .readpages	= btrfs_readpages,
	.writepage	= btrfs_writepage,
	.sync_page	= block_sync_page,
	.prepare_write	= btrfs_prepare_write,
	.commit_write	= btrfs_commit_write,
};

static struct inode_operations btrfs_file_inode_operations = {
	.truncate	= btrfs_truncate,
};

static struct file_operations btrfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read       = generic_file_aio_read,
	.write		= btrfs_file_inline_write,
	.mmap		= generic_file_mmap,
	.open		= generic_file_open,
};

static int __init init_btrfs_fs(void)
{
	int err;
	printk("btrfs loaded!\n");
	err = init_inodecache();
	if (err)
		return err;
	return register_filesystem(&btrfs_fs_type);
}

static void __exit exit_btrfs_fs(void)
{
	destroy_inodecache();
	unregister_filesystem(&btrfs_fs_type);
	printk("btrfs unloaded\n");
}

module_init(init_btrfs_fs)
module_exit(exit_btrfs_fs)

MODULE_LICENSE("GPL");
