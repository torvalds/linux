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
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

#define BTRFS_SUPER_MAGIC 0x9123682E

static struct inode_operations btrfs_dir_inode_operations;
static struct super_operations btrfs_super_ops;
static struct file_operations btrfs_dir_file_operations;
static struct inode_operations btrfs_file_inode_operations;
static struct address_space_operations btrfs_aops;
static struct file_operations btrfs_file_operations;

static void btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_path path;
	struct btrfs_inode_item *inode_item;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	int ret;

	btrfs_init_path(&path);
	mutex_lock(&root->fs_info->fs_mutex);

	ret = btrfs_lookup_inode(NULL, root, &path, inode->i_ino, 0);
	if (ret) {
		btrfs_release_path(root, &path);
		mutex_unlock(&root->fs_info->fs_mutex);
		make_bad_inode(inode);
		return;
	}
	inode_item = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]),
				  path.slots[0],
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
	btrfs_release_path(root, &path);
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
		inode->i_op = &btrfs_dir_inode_operations;
		inode->i_fop = &btrfs_dir_file_operations;
		break;
	case S_IFLNK:
		// inode->i_op = &page_symlink_inode_operations;
		break;
	}
	return;
}

static int btrfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct btrfs_path path;
	struct btrfs_root *root;
	struct btrfs_trans_handle *trans;
	const char *name = dentry->d_name.name;
	int name_len = dentry->d_name.len;
	int ret;
	u64 objectid;
	struct btrfs_dir_item *di;

	btrfs_init_path(&path);
	root = btrfs_sb(dir->i_sb);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);

	ret = btrfs_lookup_dir_item(trans, root, &path, dir->i_ino,
				    name, name_len, -1);
	if (ret < 0)
		goto err;
	if (ret > 0) {
		ret = -ENOENT;
		goto err;
	}
	di = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			    struct btrfs_dir_item);
	objectid = btrfs_dir_objectid(di);

	ret = btrfs_del_item(trans, root, &path);
	BUG_ON(ret);
	dentry->d_inode->i_ctime = dir->i_ctime;
err:
	btrfs_release_path(root, &path);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	if (ret == 0)
		inode_dec_link_count(dentry->d_inode);
	return ret;
}

static int btrfs_free_inode(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root,
			    struct inode *inode)
{
	u64 objectid = inode->i_ino;
	struct btrfs_path path;
	struct btrfs_inode_map_item *map;
	struct btrfs_key stat_data_key;
	int ret;
	clear_inode(inode);
	btrfs_init_path(&path);
	ret = btrfs_lookup_inode_map(trans, root, &path, objectid, -1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		btrfs_release_path(root, &path);
		goto error;
	}
	map = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			    struct btrfs_inode_map_item);
	btrfs_disk_key_to_cpu(&stat_data_key, &map->key);
	ret = btrfs_del_item(trans, root->fs_info->inode_root, &path);
	BUG_ON(ret);
	btrfs_release_path(root, &path);
	btrfs_init_path(&path);

	ret = btrfs_lookup_inode(trans, root, &path, objectid, -1);
	BUG_ON(ret);
	ret = btrfs_del_item(trans, root, &path);
	BUG_ON(ret);
	btrfs_release_path(root, &path);
error:
	return ret;
}

static int btrfs_truncate_in_trans(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct inode *inode)
{
	int ret;
	struct btrfs_path path;
	struct btrfs_key key;
	struct btrfs_disk_key *found_key;
	struct btrfs_leaf *leaf;
	struct btrfs_file_extent_item *fi;
	u64 extent_start;
	u64 extent_num_blocks;

	/* FIXME, add redo link to tree so we don't leak on crash */
	key.objectid = inode->i_ino;
	key.offset = (u64)-1;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
	while(1) {
		btrfs_init_path(&path);
		ret = btrfs_search_slot(trans, root, &key, &path, -1, 1);
		if (ret < 0) {
			btrfs_release_path(root, &path);
			goto error;
		}
		if (ret > 0) {
			BUG_ON(path.slots[0] == 0);
			path.slots[0]--;
		}
		leaf = btrfs_buffer_leaf(path.nodes[0]);
		found_key = &leaf->items[path.slots[0]].key;
		if (btrfs_disk_key_objectid(found_key) != inode->i_ino)
			break;
		if (btrfs_disk_key_type(found_key) != BTRFS_EXTENT_DATA_KEY)
			break;
		if (btrfs_disk_key_offset(found_key) < inode->i_size)
			break;
		/* FIXME: add extent truncation */
		if (btrfs_disk_key_offset(found_key) < inode->i_size)
			break;
		fi = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]),
				    path.slots[0],
				    struct btrfs_file_extent_item);
		extent_start = btrfs_file_extent_disk_blocknr(fi);
		extent_num_blocks = btrfs_file_extent_disk_num_blocks(fi);
		key.offset = btrfs_disk_key_offset(found_key) - 1;
		ret = btrfs_del_item(trans, root, &path);
		BUG_ON(ret);
		inode->i_blocks -= btrfs_file_extent_num_blocks(fi) >> 9;
		btrfs_release_path(root, &path);
		ret = btrfs_free_extent(trans, root, extent_start,
					extent_num_blocks, 0);
		BUG_ON(ret);
		if (btrfs_disk_key_offset(found_key) == 0)
			break;
	}
	btrfs_release_path(root, &path);
	ret = 0;
error:
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
	struct btrfs_path path;
	struct btrfs_root *root = btrfs_sb(dir->i_sb);
	int ret;

	btrfs_init_path(&path);
	ret = btrfs_lookup_dir_item(NULL, root, &path, dir->i_ino, name,
				    namelen, 0);
	if (ret || !btrfs_match_dir_item_name(root, &path, name, namelen)) {
		*ino = 0;
		goto out;
	}
	di = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			    struct btrfs_dir_item);
	*ino = btrfs_dir_objectid(di);
out:
	btrfs_release_path(root, &path);
	return ret;
}

static struct dentry *btrfs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode * inode;
	ino_t ino;
	int ret;

	if (dentry->d_name.len > BTRFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ret = btrfs_inode_by_name(dir, dentry, &ino);
	if (ret < 0)
		return ERR_PTR(ret);
	inode = NULL;
	if (ino) {
		inode = iget(dir->i_sb, ino);
		if (!inode)
			return ERR_PTR(-EACCES);
	}
	return d_splice_alias(inode, dentry);
}

static int btrfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_path path;
	int ret;
	u32 nritems;
	struct btrfs_leaf *leaf;
	int slot;
	int advance;
	unsigned char d_type = DT_UNKNOWN;
	int over = 0;

	key.objectid = inode->i_ino;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	key.offset = filp->f_pos;
	btrfs_init_path(&path);
	ret = btrfs_search_slot(NULL, root, &key, &path, 0, 0);
	if (ret < 0) {
		goto err;
	}
	advance = 0;
	while(1) {
		leaf = btrfs_buffer_leaf(path.nodes[0]);
		nritems = btrfs_header_nritems(&leaf->header);
		slot = path.slots[0];
		if (advance || slot >= nritems) {
			if (slot >= nritems -1) {
				ret = btrfs_next_leaf(root, &path);
				if (ret)
					break;
				leaf = btrfs_buffer_leaf(path.nodes[0]);
				nritems = btrfs_header_nritems(&leaf->header);
				slot = path.slots[0];
			} else {
				slot++;
				path.slots[0]++;
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
	btrfs_release_path(root, &path);
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
	struct buffer_head *bh;
	struct btrfs_root *root;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = BTRFS_SUPER_MAGIC;
	sb->s_op = &btrfs_super_ops;
	sb->s_time_gran = 1;

	bh = sb_bread(sb, BTRFS_SUPER_INFO_OFFSET / sb->s_blocksize);
	if (!bh) {
		printk("btrfs: unable to read on disk super\n");
		return -EIO;
	}
	disk_super = (struct btrfs_super_block *)bh->b_data;
	root = open_ctree(sb, bh, disk_super);
	sb->s_fs_info = root;
	if (!root) {
		printk("btrfs: open_ctree failed\n");
		return -EIO;
	}
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
}

static int btrfs_update_inode(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct inode *inode)
{
	struct btrfs_inode_item *inode_item;
	struct btrfs_path path;
	int ret;

	btrfs_init_path(&path);

	ret = btrfs_lookup_inode(trans, root, &path, inode->i_ino, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto failed;
	}

	inode_item = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]),
				  path.slots[0],
				  struct btrfs_inode_item);

	fill_inode_item(inode_item, inode);
	mark_buffer_dirty(path.nodes[0]);
failed:
	btrfs_release_path(root, &path);
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
	btrfs_end_transaction(trans, root);
out_unlock:
	mutex_unlock(&root->fs_info->fs_mutex);
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

	sb->s_dirt = 0;
	if (!wait) {
		filemap_flush(sb->s_bdev->bd_inode->i_mapping);
		return 0;
	}
	filemap_write_and_wait(sb->s_bdev->bd_inode->i_mapping);

	root = btrfs_sb(sb);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	sb->s_dirt = 0;
	BUG_ON(ret);
printk("btrfs sync_fs\n");
	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

static int btrfs_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *result, int create)
{
	int ret;
	int err = 0;
	u64 blocknr;
	u64 extent_start = 0;
	u64 extent_end = 0;
	u64 objectid = inode->i_ino;
	struct btrfs_path path;
	struct btrfs_root *root = btrfs_sb(inode->i_sb);
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_file_extent_item *item;
	struct btrfs_leaf *leaf;
	struct btrfs_disk_key *found_key;

	btrfs_init_path(&path);
	mutex_lock(&root->fs_info->fs_mutex);
	if (create)
		trans = btrfs_start_transaction(root, 1);


	ret = btrfs_lookup_file_extent(trans, root, &path,
				       inode->i_ino,
				       iblock << inode->i_blkbits, 0);
	if (ret < 0) {
		btrfs_release_path(root, &path);
		err = ret;
		goto out;
	}

	if (ret != 0) {
		if (path.slots[0] == 0) {
			btrfs_release_path(root, &path);
			goto allocate;
		}
		path.slots[0]--;
	}

	item = btrfs_item_ptr(btrfs_buffer_leaf(path.nodes[0]), path.slots[0],
			      struct btrfs_file_extent_item);
	leaf = btrfs_buffer_leaf(path.nodes[0]);
	blocknr = btrfs_file_extent_disk_blocknr(item);
	blocknr += btrfs_file_extent_offset(item);

	/* exact match found, use it */
	if (ret == 0) {
		err = 0;
		map_bh(result, inode->i_sb, blocknr);
		btrfs_release_path(root, &path);
		goto out;
	}

	/* are we inside the extent that was found? */
	found_key = &leaf->items[path.slots[0]].key;
	if (btrfs_disk_key_objectid(found_key) != objectid ||
	    btrfs_disk_key_type(found_key) != BTRFS_EXTENT_DATA_KEY) {
		extent_end = 0;
		extent_start = 0;
		btrfs_release_path(root, &path);
		goto allocate;
	}

	extent_start = btrfs_disk_key_offset(&leaf->items[path.slots[0]].key);
	extent_start = extent_start >> inode->i_blkbits;
	extent_start += btrfs_file_extent_offset(item);
	extent_end = extent_start + btrfs_file_extent_num_blocks(item);
	btrfs_release_path(root, &path);
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
	if (trans)
		btrfs_end_transaction(trans, root);
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

	nobh_truncate_page(inode->i_mapping, inode->i_size);

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
};

static struct inode_operations btrfs_dir_inode_operations = {
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
	.unlink		= btrfs_unlink,
	.mkdir		= btrfs_mkdir,
};

static struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= btrfs_readdir,
};

static struct address_space_operations btrfs_aops = {
	.readpage	= btrfs_readpage,
	.readpages	= btrfs_readpages,
	.writepage	= btrfs_writepage,
	.sync_page	= block_sync_page,
	.prepare_write	= btrfs_prepare_write,
	.commit_write	= nobh_commit_write,
};

static struct inode_operations btrfs_file_inode_operations = {
	.truncate	= btrfs_truncate,
};

static struct file_operations btrfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.open		= generic_file_open,
	.sendfile	= generic_file_sendfile,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
};

static int __init init_btrfs_fs(void)
{
	printk("btrfs loaded!\n");
	return register_filesystem(&btrfs_fs_type);
}

static void __exit exit_btrfs_fs(void)
{
	unregister_filesystem(&btrfs_fs_type);
	printk("btrfs unloaded\n");
}

module_init(init_btrfs_fs)
module_exit(exit_btrfs_fs)

MODULE_LICENSE("GPL");
