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
#include <linux/compat.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"

struct btrfs_iget_args {
	u64 ino;
	struct btrfs_root *root;
};

static struct inode_operations btrfs_dir_inode_operations;
static struct inode_operations btrfs_symlink_inode_operations;
static struct inode_operations btrfs_dir_ro_inode_operations;
static struct inode_operations btrfs_file_inode_operations;
static struct address_space_operations btrfs_aops;
static struct address_space_operations btrfs_symlink_aops;
static struct file_operations btrfs_dir_file_operations;

static struct kmem_cache *btrfs_inode_cachep;
struct kmem_cache *btrfs_trans_handle_cachep;
struct kmem_cache *btrfs_transaction_cachep;
struct kmem_cache *btrfs_bit_radix_cachep;
struct kmem_cache *btrfs_path_cachep;

#define S_SHIFT 12
static unsigned char btrfs_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]	= BTRFS_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]	= BTRFS_FT_DIR,
	[S_IFCHR >> S_SHIFT]	= BTRFS_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]	= BTRFS_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]	= BTRFS_FT_FIFO,
	[S_IFSOCK >> S_SHIFT]	= BTRFS_FT_SOCK,
	[S_IFLNK >> S_SHIFT]	= BTRFS_FT_SYMLINK,
};

void btrfs_read_locked_inode(struct inode *inode)
{
	struct btrfs_path *path;
	struct btrfs_inode_item *inode_item;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_key location;
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
	BTRFS_I(inode)->block_group = btrfs_lookup_block_group(root->fs_info,
						       alloc_group_block);

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
		inode->i_op = &btrfs_symlink_inode_operations;
		inode->i_mapping->a_ops = &btrfs_symlink_aops;
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
	btrfs_btree_balance_dirty(root);
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
	btrfs_btree_balance_dirty(root);
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

/*
 * truncates go from a high offset to a low offset.  So, walk
 * from hi to lo in the node and issue readas.  Stop when you find
 * keys from a different objectid
 */
static void reada_truncate(struct btrfs_root *root, struct btrfs_path *path,
			   u64 objectid)
{
	struct btrfs_node *node;
	int i;
	int nritems;
	u64 item_objectid;
	u64 blocknr;
	int slot;
	int ret;

	if (!path->nodes[1])
		return;
	node = btrfs_buffer_node(path->nodes[1]);
	slot = path->slots[1];
	if (slot == 0)
		return;
	nritems = btrfs_header_nritems(&node->header);
	for (i = slot - 1; i >= 0; i--) {
		item_objectid = btrfs_disk_key_objectid(&node->ptrs[i].key);
		if (item_objectid != objectid)
			break;
		blocknr = btrfs_node_blockptr(node, i);
		ret = readahead_tree_block(root, blocknr);
		if (ret)
			break;
	}
}

/*
 * this can truncate away extent items, csum items and directory items.
 * It starts at a high offset and removes keys until it can't find
 * any higher than i_size.
 *
 * csum items that cross the new i_size are truncated to the new size
 * as well.
 */
static int btrfs_truncate_in_trans(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct inode *inode)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_disk_key *found_key;
	u32 found_type;
	struct btrfs_leaf *leaf;
	struct btrfs_file_extent_item *fi;
	u64 extent_start = 0;
	u64 extent_num_blocks = 0;
	u64 item_end = 0;
	int found_extent;
	int del_item;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	/* FIXME, add redo link to tree so we don't leak on crash */
	key.objectid = inode->i_ino;
	key.offset = (u64)-1;
	key.flags = (u32)-1;
	while(1) {
		btrfs_init_path(path);
		fi = NULL;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0) {
			goto error;
		}
		if (ret > 0) {
			BUG_ON(path->slots[0] == 0);
			path->slots[0]--;
		}
		reada_truncate(root, path, inode->i_ino);
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		found_key = &leaf->items[path->slots[0]].key;
		found_type = btrfs_disk_key_type(found_key);

		if (btrfs_disk_key_objectid(found_key) != inode->i_ino)
			break;
		if (found_type != BTRFS_CSUM_ITEM_KEY &&
		    found_type != BTRFS_DIR_ITEM_KEY &&
		    found_type != BTRFS_DIR_INDEX_KEY &&
		    found_type != BTRFS_EXTENT_DATA_KEY)
			break;

		item_end = btrfs_disk_key_offset(found_key);
		if (found_type == BTRFS_EXTENT_DATA_KEY) {
			fi = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
					    path->slots[0],
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(fi) !=
			    BTRFS_FILE_EXTENT_INLINE) {
				item_end += btrfs_file_extent_num_blocks(fi) <<
						inode->i_blkbits;
			}
		}
		if (found_type == BTRFS_CSUM_ITEM_KEY) {
			ret = btrfs_csum_truncate(trans, root, path,
						  inode->i_size);
			BUG_ON(ret);
		}
		if (item_end < inode->i_size) {
			if (found_type) {
				btrfs_set_key_type(&key, found_type - 1);
				continue;
			}
			break;
		}
		if (btrfs_disk_key_offset(found_key) >= inode->i_size)
			del_item = 1;
		else
			del_item = 0;
		found_extent = 0;

		/* FIXME, shrink the extent if the ref count is only 1 */
		if (found_type == BTRFS_EXTENT_DATA_KEY &&
			   btrfs_file_extent_type(fi) !=
			   BTRFS_FILE_EXTENT_INLINE) {
			u64 num_dec;
			if (!del_item) {
				u64 orig_num_blocks =
					btrfs_file_extent_num_blocks(fi);
				extent_num_blocks = inode->i_size -
					btrfs_disk_key_offset(found_key) +
					root->blocksize - 1;
				extent_num_blocks >>= inode->i_blkbits;
				btrfs_set_file_extent_num_blocks(fi,
							 extent_num_blocks);
				inode->i_blocks -= (orig_num_blocks -
					extent_num_blocks) << 3;
				mark_buffer_dirty(path->nodes[0]);
			} else {
				extent_start =
					btrfs_file_extent_disk_blocknr(fi);
				extent_num_blocks =
					btrfs_file_extent_disk_num_blocks(fi);
				/* FIXME blocksize != 4096 */
				num_dec = btrfs_file_extent_num_blocks(fi) << 3;
				if (extent_start != 0) {
					found_extent = 1;
					inode->i_blocks -= num_dec;
				}
			}
		}
		if (del_item) {
			ret = btrfs_del_item(trans, root, path);
			BUG_ON(ret);
		} else {
			break;
		}
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

/*
 * taken from block_truncate_page, but does cow as it zeros out
 * any bytes left in the last page in the file.
 */
static int btrfs_truncate_page(struct address_space *mapping, loff_t from)
{
	struct inode *inode = mapping->host;
	unsigned blocksize = 1 << inode->i_blkbits;
	pgoff_t index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	struct page *page;
	char *kaddr;
	int ret = 0;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 alloc_hint = 0;
	struct btrfs_key ins;
	struct btrfs_trans_handle *trans;

	if ((offset & (blocksize - 1)) == 0)
		goto out;

	ret = -ENOMEM;
	page = grab_cache_page(mapping, index);
	if (!page)
		goto out;

	if (!PageUptodate(page)) {
		ret = mpage_readpage(page, btrfs_get_block);
		lock_page(page);
		if (!PageUptodate(page)) {
			ret = -EIO;
			goto out;
		}
	}
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);

	ret = btrfs_drop_extents(trans, root, inode,
				 page->index << PAGE_CACHE_SHIFT,
				 (page->index + 1) << PAGE_CACHE_SHIFT,
				 &alloc_hint);
	BUG_ON(ret);
	ret = btrfs_alloc_extent(trans, root, inode->i_ino, 1,
				 alloc_hint, (u64)-1, &ins, 1);
	BUG_ON(ret);
	ret = btrfs_insert_file_extent(trans, root, inode->i_ino,
				       page->index << PAGE_CACHE_SHIFT,
				       ins.objectid, 1, 1);
	BUG_ON(ret);
	SetPageChecked(page);
	kaddr = kmap(page);
	memset(kaddr + offset, 0, PAGE_CACHE_SIZE - offset);
	flush_dcache_page(page);
	btrfs_csum_file_block(trans, root, inode->i_ino,
			      page->index << PAGE_CACHE_SHIFT,
			      kaddr, PAGE_CACHE_SIZE);
	kunmap(page);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);

	set_page_dirty(page);
	unlock_page(page);
	page_cache_release(page);
out:
	return ret;
}

static int btrfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int err;

	err = inode_change_ok(inode, attr);
	if (err)
		return err;

	if (S_ISREG(inode->i_mode) &&
	    attr->ia_valid & ATTR_SIZE && attr->ia_size > inode->i_size) {
		struct btrfs_trans_handle *trans;
		struct btrfs_root *root = BTRFS_I(inode)->root;
		u64 mask = root->blocksize - 1;
		u64 pos = (inode->i_size + mask) & ~mask;
		u64 hole_size;

		if (attr->ia_size <= pos)
			goto out;

		btrfs_truncate_page(inode->i_mapping, inode->i_size);

		hole_size = (attr->ia_size - pos + mask) & ~mask;
		hole_size >>= inode->i_blkbits;

		mutex_lock(&root->fs_info->fs_mutex);
		trans = btrfs_start_transaction(root, 1);
		btrfs_set_trans_block_group(trans, inode);
		err = btrfs_insert_file_extent(trans, root, inode->i_ino,
					       pos, 0, 0, hole_size);
		BUG_ON(err);
		btrfs_end_transaction(trans, root);
		mutex_unlock(&root->fs_info->fs_mutex);
	}
out:
	err = inode_setattr(inode, attr);

	return err;
}
void btrfs_delete_inode(struct inode *inode)
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
	ret = btrfs_truncate_in_trans(trans, root, inode);
	BUG_ON(ret);
	btrfs_free_inode(trans, root, inode);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	btrfs_btree_balance_dirty(root);
	return;
no_delete:
	clear_inode(inode);
}

/*
 * this returns the key found in the dir entry in the location pointer.
 * If no dir entries were found, location->objectid is 0.
 */
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

/*
 * when we hit a tree root in a directory, the btrfs part of the inode
 * needs to be changed to reflect the root directory of the tree root.  This
 * is kind of like crossing a mount point.
 */
static int fixup_tree_root_location(struct btrfs_root *root,
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

static int btrfs_init_locked_inode(struct inode *inode, void *p)
{
	struct btrfs_iget_args *args = p;
	inode->i_ino = args->ino;
	BTRFS_I(inode)->root = args->root;
	return 0;
}

static int btrfs_find_actor(struct inode *inode, void *opaque)
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
			/* the inode and parent dir are two different roots */
			if (sub_root != root) {
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

/*
 * readahead one full node of leaves as long as their keys include
 * the objectid supplied
 */
static void reada_leaves(struct btrfs_root *root, struct btrfs_path *path,
			 u64 objectid)
{
	struct btrfs_node *node;
	int i;
	u32 nritems;
	u64 item_objectid;
	u64 blocknr;
	int slot;
	int ret;

	if (!path->nodes[1])
		return;
	node = btrfs_buffer_node(path->nodes[1]);
	slot = path->slots[1];
	nritems = btrfs_header_nritems(&node->header);
	for (i = slot + 1; i < nritems; i++) {
		item_objectid = btrfs_disk_key_objectid(&node->ptrs[i].key);
		if (item_objectid != objectid)
			break;
		blocknr = btrfs_node_blockptr(node, i);
		ret = readahead_tree_block(root, blocknr);
		if (ret)
			break;
	}
}
static unsigned char btrfs_filetype_table[] = {
	DT_UNKNOWN, DT_REG, DT_DIR, DT_CHR, DT_BLK, DT_FIFO, DT_SOCK, DT_LNK
};

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
	unsigned char d_type;
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
	reada_leaves(root, path, inode->i_ino);
	while(1) {
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		nritems = btrfs_header_nritems(&leaf->header);
		slot = path->slots[0];
		if (advance || slot >= nritems) {
			if (slot >= nritems -1) {
				reada_leaves(root, path, inode->i_ino);
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
			d_type = btrfs_filetype_table[btrfs_dir_type(di)];
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

int btrfs_write_inode(struct inode *inode, int wait)
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

/*
 * This is somewhat expense, updating the tree every time the
 * inode changes.  But, it is most likely to find the inode in cache.
 * FIXME, needs more benchmarking...there are no reasons other than performance
 * to keep or drop this code.
 */
void btrfs_dirty_inode(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);
	btrfs_update_inode(trans, root, inode);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	btrfs_btree_balance_dirty(root);
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
	int owner;

	inode = new_inode(root->fs_info->sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	BTRFS_I(inode)->root = root;
	if (mode & S_IFDIR)
		owner = 0;
	else
		owner = 1;
	group = btrfs_find_block_group(root, group, 0, 0, owner);
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

static inline u8 btrfs_inode_type(struct inode *inode)
{
	return btrfs_type_by_mode[(inode->i_mode & S_IFMT) >> S_SHIFT];
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
				    &key, btrfs_inode_type(inode));
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
	btrfs_btree_balance_dirty(root);
	return err;
}

static int btrfs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct inode *inode = old_dentry->d_inode;
	int err;
	int drop_inode = 0;

	if (inode->i_nlink == 0)
		return -ENOENT;

	inc_nlink(inode);
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);
	atomic_inc(&inode->i_count);
	err = btrfs_add_nondir(trans, dentry, inode);
	if (err)
		drop_inode = 1;
	dir->i_sb->s_dirt = 1;
	btrfs_update_inode_block_group(trans, dir);
	btrfs_update_inode(trans, root, inode);

	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);

	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root);
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
				    &key, BTRFS_FT_DIR);
	if (ret)
		goto error;
	key.objectid = dirid;
	ret = btrfs_insert_dir_item(trans, root, buf, 2, objectid,
				    &key, BTRFS_FT_DIR);
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
	btrfs_btree_balance_dirty(root);
	return err;
}

/*
 * FIBMAP and others want to pass in a fake buffer head.  They need to
 * use BTRFS_GET_BLOCK_NO_DIRECT to make sure we don't try to memcpy
 * any packed file data into the fake bh
 */
#define BTRFS_GET_BLOCK_NO_CREATE 0
#define BTRFS_GET_BLOCK_CREATE 1
#define BTRFS_GET_BLOCK_NO_DIRECT 2

/*
 * FIXME create==1 doe not work.
 */
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
	u64 alloc_hint = 0;
	struct btrfs_path *path;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_file_extent_item *item;
	struct btrfs_leaf *leaf;
	struct btrfs_disk_key *found_key;
	struct btrfs_trans_handle *trans = NULL;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	if (create & BTRFS_GET_BLOCK_CREATE) {
		WARN_ON(1);
		/* this almost but not quite works */
		trans = btrfs_start_transaction(root, 1);
		if (!trans) {
			err = -ENOMEM;
			goto out;
		}
		ret = btrfs_drop_extents(trans, root, inode,
					 iblock << inode->i_blkbits,
					 (iblock + 1) << inode->i_blkbits,
					 &alloc_hint);
		BUG_ON(ret);
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
			goto not_found;
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
		goto not_found;
	}
	found_type = btrfs_file_extent_type(item);
	extent_start = btrfs_disk_key_offset(&leaf->items[path->slots[0]].key);
	if (found_type == BTRFS_FILE_EXTENT_REG) {
		extent_start = extent_start >> inode->i_blkbits;
		extent_end = extent_start + btrfs_file_extent_num_blocks(item);
		err = 0;
		if (btrfs_file_extent_disk_blocknr(item) == 0)
			goto out;
		if (iblock >= extent_start && iblock < extent_end) {
			btrfs_map_bh_to_logical(root, result, blocknr +
						iblock - extent_start);
			goto out;
		}
	} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
		char *ptr;
		char *map;
		u32 size;

		if (create & BTRFS_GET_BLOCK_NO_DIRECT) {
			err = -EINVAL;
			goto out;
		}
		size = btrfs_file_extent_inline_len(leaf->items +
						    path->slots[0]);
		extent_end = (extent_start + size) >> inode->i_blkbits;
		extent_start >>= inode->i_blkbits;
		if (iblock < extent_start || iblock > extent_end) {
			goto not_found;
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
not_found:
	if (create & BTRFS_GET_BLOCK_CREATE) {
		struct btrfs_key ins;
		ret = btrfs_alloc_extent(trans, root, inode->i_ino,
					 1, alloc_hint, (u64)-1,
					 &ins, 1);
		BUG_ON(ret);
		ret = btrfs_insert_file_extent(trans, root, inode->i_ino,
					       iblock << inode->i_blkbits,
					       ins.objectid, ins.offset,
					       ins.offset);
		BUG_ON(ret);
		SetPageChecked(result->b_page);
		btrfs_map_bh_to_logical(root, result, ins.objectid);
	}
out:
	if (trans)
		err = btrfs_end_transaction(trans, root);
	btrfs_free_path(path);
	return err;
}

int btrfs_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *result, int create)
{
	int err;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	mutex_lock(&root->fs_info->fs_mutex);
	err = btrfs_get_block_lock(inode, iblock, result, create);
	mutex_unlock(&root->fs_info->fs_mutex);
	return err;
}

static int btrfs_get_block_bmap(struct inode *inode, sector_t iblock,
			   struct buffer_head *result, int create)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	mutex_lock(&root->fs_info->fs_mutex);
	btrfs_get_block_lock(inode, iblock, result, BTRFS_GET_BLOCK_NO_DIRECT);
	mutex_unlock(&root->fs_info->fs_mutex);
	return 0;
}

static sector_t btrfs_bmap(struct address_space *as, sector_t block)
{
	return generic_block_bmap(as, block, btrfs_get_block_bmap);
}

static int btrfs_prepare_write(struct file *file, struct page *page,
			       unsigned from, unsigned to)
{
	return block_prepare_write(page, from, to, btrfs_get_block);
}

static int btrfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, btrfs_get_block);
}

/*
 * Aside from a tiny bit of packed file data handling, this is the
 * same as the generic code.
 *
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
			if (err) {
				goto recover;
			}
			if (buffer_new(bh)) {
				/* blockdev mappings never come here */
				clear_buffer_new(bh);
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

	btrfs_truncate_page(inode->i_mapping, inode->i_size);

	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, inode);

	/* FIXME, add redo link to tree so we don't leak on crash */
	ret = btrfs_truncate_in_trans(trans, root, inode);
	BUG_ON(ret);
	btrfs_update_inode(trans, root, inode);
	ret = btrfs_end_transaction(trans, root);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->fs_mutex);
	btrfs_btree_balance_dirty(root);
}

int btrfs_commit_write(struct file *file, struct page *page,
		       unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	struct buffer_head *bh;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;

	SetPageUptodate(page);
	bh = page_buffers(page);
	set_buffer_uptodate(bh);
	if (buffer_mapped(bh) && bh->b_blocknr != 0) {
		set_page_dirty(page);
	}
	if (pos > inode->i_size) {
		i_size_write(inode, pos);
		mark_inode_dirty(inode);
	}
	return 0;
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
				    name, namelen, dir->i_ino, &key,
				    BTRFS_FT_DIR);
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
	btrfs_btree_balance_dirty(root);
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
				    &key, BTRFS_FT_DIR);

	BUG_ON(ret);

	ret = btrfs_inc_root_ref(trans, root);
	BUG_ON(ret);

	ret = btrfs_commit_transaction(trans, root);
	BUG_ON(ret);
	mutex_unlock(&root->fs_info->fs_mutex);
	btrfs_btree_balance_dirty(root);
	return 0;
}

int btrfs_ioctl(struct inode *inode, struct file *filp, unsigned int
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
	default:
		return -ENOTTY;
	}
	return ret;
}

#ifdef CONFIG_COMPAT
long btrfs_compat_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int ret;
	lock_kernel();
	ret = btrfs_ioctl(inode, file, cmd, (unsigned long) compat_ptr(arg));
	unlock_kernel();
	return ret;

}
#endif

/*
 * Called inside transaction, so use GFP_NOFS
 */
struct inode *btrfs_alloc_inode(struct super_block *sb)
{
	struct btrfs_inode *ei;

	ei = kmem_cache_alloc(btrfs_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

void btrfs_destroy_inode(struct inode *inode)
{
	WARN_ON(!list_empty(&inode->i_dentry));
	WARN_ON(inode->i_data.nrpages);

	kmem_cache_free(btrfs_inode_cachep, BTRFS_I(inode));
}

static void init_once(void * foo, struct kmem_cache * cachep,
		      unsigned long flags)
{
	struct btrfs_inode *ei = (struct btrfs_inode *) foo;

	inode_init_once(&ei->vfs_inode);
}

void btrfs_destroy_cachep(void)
{
	if (btrfs_inode_cachep)
		kmem_cache_destroy(btrfs_inode_cachep);
	if (btrfs_trans_handle_cachep)
		kmem_cache_destroy(btrfs_trans_handle_cachep);
	if (btrfs_transaction_cachep)
		kmem_cache_destroy(btrfs_transaction_cachep);
	if (btrfs_bit_radix_cachep)
		kmem_cache_destroy(btrfs_bit_radix_cachep);
	if (btrfs_path_cachep)
		kmem_cache_destroy(btrfs_path_cachep);
}

int btrfs_init_cachep(void)
{
	btrfs_inode_cachep = kmem_cache_create("btrfs_inode_cache",
					     sizeof(struct btrfs_inode),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	if (!btrfs_inode_cachep)
		goto fail;
	btrfs_trans_handle_cachep = kmem_cache_create("btrfs_trans_handle_cache",
					     sizeof(struct btrfs_trans_handle),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     NULL, NULL);
	if (!btrfs_trans_handle_cachep)
		goto fail;
	btrfs_transaction_cachep = kmem_cache_create("btrfs_transaction_cache",
					     sizeof(struct btrfs_transaction),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     NULL, NULL);
	if (!btrfs_transaction_cachep)
		goto fail;
	btrfs_path_cachep = kmem_cache_create("btrfs_path_cache",
					     sizeof(struct btrfs_transaction),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     NULL, NULL);
	if (!btrfs_path_cachep)
		goto fail;
	btrfs_bit_radix_cachep = kmem_cache_create("btrfs_radix",
					     256,
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD |
						SLAB_DESTROY_BY_RCU),
					     NULL, NULL);
	if (!btrfs_bit_radix_cachep)
		goto fail;
	return 0;
fail:
	btrfs_destroy_cachep();
	return -ENOMEM;
}

static int btrfs_getattr(struct vfsmount *mnt,
			 struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blksize = 256 * 1024;
	return 0;
}

static int btrfs_rename(struct inode * old_dir, struct dentry *old_dentry,
			   struct inode * new_dir,struct dentry *new_dentry)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(old_dir)->root;
	struct inode *new_inode = new_dentry->d_inode;
	struct inode *old_inode = old_dentry->d_inode;
	struct timespec ctime = CURRENT_TIME;
	struct btrfs_path *path;
	struct btrfs_dir_item *di;
	int ret;

	if (S_ISDIR(old_inode->i_mode) && new_inode &&
	    new_inode->i_size > BTRFS_EMPTY_DIR_SIZE) {
		return -ENOTEMPTY;
	}
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, new_dir);
	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out_fail;
	}

	old_dentry->d_inode->i_nlink++;
	old_dir->i_ctime = old_dir->i_mtime = ctime;
	new_dir->i_ctime = new_dir->i_mtime = ctime;
	old_inode->i_ctime = ctime;
	if (S_ISDIR(old_inode->i_mode) && old_dir != new_dir) {
		struct btrfs_key *location = &BTRFS_I(new_dir)->location;
		u64 old_parent_oid;
		di = btrfs_lookup_dir_item(trans, root, path, old_inode->i_ino,
					   "..", 2, -1);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out_fail;
		}
		if (!di) {
			ret = -ENOENT;
			goto out_fail;
		}
		old_parent_oid = btrfs_disk_key_objectid(&di->location);
		ret = btrfs_del_item(trans, root, path);
		if (ret) {
			ret = -EIO;
			goto out_fail;
		}
		btrfs_release_path(root, path);

		di = btrfs_lookup_dir_index_item(trans, root, path,
						 old_inode->i_ino,
						 old_parent_oid,
						 "..", 2, -1);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out_fail;
		}
		if (!di) {
			ret = -ENOENT;
			goto out_fail;
		}
		ret = btrfs_del_item(trans, root, path);
		if (ret) {
			ret = -EIO;
			goto out_fail;
		}
		btrfs_release_path(root, path);

		ret = btrfs_insert_dir_item(trans, root, "..", 2,
					    old_inode->i_ino, location,
					    BTRFS_FT_DIR);
		if (ret)
			goto out_fail;
	}


	ret = btrfs_unlink_trans(trans, root, old_dir, old_dentry);
	if (ret)
		goto out_fail;

	if (new_inode) {
		new_inode->i_ctime = CURRENT_TIME;
		ret = btrfs_unlink_trans(trans, root, new_dir, new_dentry);
		if (ret)
			goto out_fail;
		if (S_ISDIR(new_inode->i_mode))
			clear_nlink(new_inode);
		else
			drop_nlink(new_inode);
		btrfs_update_inode(trans, root, new_inode);
	}
	ret = btrfs_add_link(trans, new_dentry, old_inode);
	if (ret)
		goto out_fail;

out_fail:
	btrfs_free_path(path);
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);
	return ret;
}

static int btrfs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct inode *inode;
	int err;
	int drop_inode = 0;
	u64 objectid;
	int name_len;
	int datasize;
	char *ptr;
	struct btrfs_file_extent_item *ei;

	name_len = strlen(symname) + 1;
	if (name_len > BTRFS_MAX_INLINE_DATA_SIZE(root))
		return -ENAMETOOLONG;
	mutex_lock(&root->fs_info->fs_mutex);
	trans = btrfs_start_transaction(root, 1);
	btrfs_set_trans_block_group(trans, dir);

	err = btrfs_find_free_objectid(trans, root, dir->i_ino, &objectid);
	if (err) {
		err = -ENOSPC;
		goto out_unlock;
	}

	inode = btrfs_new_inode(trans, root, objectid,
				BTRFS_I(dir)->block_group, S_IFLNK|S_IRWXUGO);
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
	if (drop_inode)
		goto out_unlock;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	key.objectid = inode->i_ino;
	key.offset = 0;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_EXTENT_DATA_KEY);
	datasize = btrfs_file_extent_calc_inline_size(name_len);
	err = btrfs_insert_empty_item(trans, root, path, &key,
				      datasize);
	BUG_ON(err);
	ei = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
	       path->slots[0], struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(ei, trans->transid);
	btrfs_set_file_extent_type(ei,
				   BTRFS_FILE_EXTENT_INLINE);
	ptr = btrfs_file_extent_inline_start(ei);
	btrfs_memcpy(root, path->nodes[0]->b_data,
		     ptr, symname, name_len);
	mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);
	inode->i_op = &btrfs_symlink_inode_operations;
	inode->i_mapping->a_ops = &btrfs_symlink_aops;
	inode->i_size = name_len - 1;
	btrfs_update_inode(trans, root, inode);
	err = 0;

out_unlock:
	btrfs_end_transaction(trans, root);
	mutex_unlock(&root->fs_info->fs_mutex);

	if (drop_inode) {
		inode_dec_link_count(inode);
		iput(inode);
	}
	btrfs_btree_balance_dirty(root);
	return err;
}

static struct inode_operations btrfs_dir_inode_operations = {
	.lookup		= btrfs_lookup,
	.create		= btrfs_create,
	.unlink		= btrfs_unlink,
	.link		= btrfs_link,
	.mkdir		= btrfs_mkdir,
	.rmdir		= btrfs_rmdir,
	.rename		= btrfs_rename,
	.symlink	= btrfs_symlink,
	.setattr	= btrfs_setattr,
};

static struct inode_operations btrfs_dir_ro_inode_operations = {
	.lookup		= btrfs_lookup,
};

static struct file_operations btrfs_dir_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= btrfs_readdir,
	.ioctl		= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_compat_ioctl,
#endif
};

static struct address_space_operations btrfs_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
	.sync_page	= block_sync_page,
	.prepare_write	= btrfs_prepare_write,
	.commit_write	= btrfs_commit_write,
	.bmap		= btrfs_bmap,
};

static struct address_space_operations btrfs_symlink_aops = {
	.readpage	= btrfs_readpage,
	.writepage	= btrfs_writepage,
};

static struct inode_operations btrfs_file_inode_operations = {
	.truncate	= btrfs_truncate,
	.getattr	= btrfs_getattr,
	.setattr	= btrfs_setattr,
};

static struct inode_operations btrfs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
};
