#include <linux/module.h>
#include <linux/fs.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

static int check_tree_block(struct btrfs_root *root, struct buffer_head *buf)
{
	struct btrfs_node *node = btrfs_buffer_node(buf);
	if (buf->b_blocknr != btrfs_header_blocknr(&node->header))
		BUG();
	if (root->node && btrfs_header_parentid(&node->header) !=
	    btrfs_header_parentid(btrfs_buffer_header(root->node))) {
		BUG();
	}
	return 0;
}

struct buffer_head *alloc_tree_block(struct btrfs_root *root, u64 blocknr)
{
	return sb_getblk(root->fs_info->sb, blocknr);
}

struct buffer_head *find_tree_block(struct btrfs_root *root, u64 blocknr)
{
	return sb_getblk(root->fs_info->sb, blocknr);
}

struct buffer_head *read_tree_block(struct btrfs_root *root, u64 blocknr)
{
	struct buffer_head *buf = sb_bread(root->fs_info->sb, blocknr);

	if (!buf)
		return buf;
	if (check_tree_block(root, buf))
		BUG();
	return buf;
}

int dirty_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf)
{
	mark_buffer_dirty(buf);
	return 0;
}

int clean_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf)
{
	clear_buffer_dirty(buf);
	return 0;
}

static int __setup_root(struct btrfs_super_block *super,
			struct btrfs_root *root,
			struct btrfs_fs_info *fs_info,
			u64 objectid)
{
	root->node = NULL;
	root->commit_root = NULL;
	root->blocksize = btrfs_super_blocksize(super);
	root->ref_cows = 0;
	root->fs_info = fs_info;
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	return 0;
}

static int find_and_setup_root(struct btrfs_super_block *super,
			       struct btrfs_root *tree_root,
			       struct btrfs_fs_info *fs_info,
			       u64 objectid,
			       struct btrfs_root *root)
{
	int ret;

	__setup_root(super, root, fs_info, objectid);
	ret = btrfs_find_last_root(tree_root, objectid,
				   &root->root_item, &root->root_key);
	BUG_ON(ret);

	root->node = read_tree_block(root,
				     btrfs_root_blocknr(&root->root_item));
	BUG_ON(!root->node);
	return 0;
}

struct btrfs_root *open_ctree(struct super_block *sb,
			      struct buffer_head *sb_buffer,
			      struct btrfs_super_block *disk_super)
{
	struct btrfs_root *root = kmalloc(sizeof(struct btrfs_root),
					  GFP_NOFS);
	struct btrfs_root *extent_root = kmalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *tree_root = kmalloc(sizeof(struct btrfs_root),
					       GFP_NOFS);
	struct btrfs_root *inode_root = kmalloc(sizeof(struct btrfs_root),
						GFP_NOFS);
	struct btrfs_fs_info *fs_info = kmalloc(sizeof(*fs_info),
						GFP_NOFS);
	int ret;

	/* FIXME: don't be stupid */
	if (!btrfs_super_root(disk_super))
		return NULL;
	init_bit_radix(&fs_info->pinned_radix);
	init_bit_radix(&fs_info->pending_del_radix);
	fs_info->running_transaction = NULL;
	fs_info->fs_root = root;
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->inode_root = inode_root;
	fs_info->last_inode_alloc = 0;
	fs_info->last_inode_alloc_dirid = 0;
	fs_info->disk_super = disk_super;
	fs_info->sb_buffer = sb_buffer;
	fs_info->sb = sb;
	mutex_init(&fs_info->trans_mutex);
	mutex_init(&fs_info->fs_mutex);
	memset(&fs_info->current_insert, 0, sizeof(fs_info->current_insert));
	memset(&fs_info->last_insert, 0, sizeof(fs_info->last_insert));

	__setup_root(disk_super, tree_root, fs_info, BTRFS_ROOT_TREE_OBJECTID);
	tree_root->node = read_tree_block(tree_root,
					  btrfs_super_root(disk_super));
	BUG_ON(!tree_root->node);

	ret = find_and_setup_root(disk_super, tree_root, fs_info,
				  BTRFS_EXTENT_TREE_OBJECTID, extent_root);
	BUG_ON(ret);

	ret = find_and_setup_root(disk_super, tree_root, fs_info,
				  BTRFS_INODE_MAP_OBJECTID, inode_root);
	BUG_ON(ret);

	ret = find_and_setup_root(disk_super, tree_root, fs_info,
				  BTRFS_FS_TREE_OBJECTID, root);
	BUG_ON(ret);

	root->commit_root = root->node;
	get_bh(root->node);
	root->ref_cows = 1;
	root->fs_info->generation = root->root_key.offset + 1;
	return root;
}

int write_ctree_super(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root)
{
	struct buffer_head *bh = root->fs_info->sb_buffer;
	btrfs_set_super_root(root->fs_info->disk_super,
			     root->fs_info->tree_root->node->b_blocknr);
	lock_buffer(bh);
	clear_buffer_dirty(bh);
	bh->b_end_io = end_buffer_write_sync;
	get_bh(bh);
	submit_bh(WRITE, bh);
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		WARN_ON(1);
		return -EIO;
	}
	return 0;
}

int close_ctree(struct btrfs_root *root)
{
	int ret;
	struct btrfs_trans_handle *trans;

	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	/* run commit again to  drop the original snapshot */
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	ret = btrfs_write_and_wait_transaction(NULL, root);
	BUG_ON(ret);
	write_ctree_super(NULL, root);

	if (root->node)
		btrfs_block_release(root, root->node);
	if (root->fs_info->extent_root->node)
		btrfs_block_release(root->fs_info->extent_root,
				    root->fs_info->extent_root->node);
	if (root->fs_info->inode_root->node)
		btrfs_block_release(root->fs_info->inode_root,
				    root->fs_info->inode_root->node);
	if (root->fs_info->tree_root->node)
		btrfs_block_release(root->fs_info->tree_root,
				    root->fs_info->tree_root->node);
	btrfs_block_release(root, root->commit_root);
	btrfs_block_release(root, root->fs_info->sb_buffer);
	kfree(root->fs_info->extent_root);
	kfree(root->fs_info->inode_root);
	kfree(root->fs_info->tree_root);
	kfree(root->fs_info);
	kfree(root);
	return 0;
}

void btrfs_block_release(struct btrfs_root *root, struct buffer_head *buf)
{
	brelse(buf);
}

