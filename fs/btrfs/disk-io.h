#ifndef __DISKIO__
#define __DISKIO__

#include <linux/buffer_head.h>

#define BTRFS_SUPER_INFO_OFFSET (16 * 1024)

static inline struct btrfs_node *btrfs_buffer_node(struct buffer_head *bh)
{
	return (struct btrfs_node *)bh->b_data;
}

static inline struct btrfs_leaf *btrfs_buffer_leaf(struct buffer_head *bh)
{
	return (struct btrfs_leaf *)bh->b_data;
}

static inline struct btrfs_header *btrfs_buffer_header(struct buffer_head *bh)
{
	return &((struct btrfs_node *)bh->b_data)->header;
}

struct buffer_head *read_tree_block(struct btrfs_root *root, u64 blocknr);
struct buffer_head *find_tree_block(struct btrfs_root *root, u64 blocknr);
int write_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf);
int dirty_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf);
int clean_tree_block(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root, struct buffer_head *buf);
int btrfs_commit_transaction(struct btrfs_trans_handle *trans, struct btrfs_root
			     *root, struct btrfs_super_block *s);
struct btrfs_root *open_ctree(struct super_block *sb,
			      struct buffer_head *sb_buffer,
			      struct btrfs_super_block *disk_super);
int close_ctree(struct btrfs_root *root);
void btrfs_block_release(struct btrfs_root *root, struct buffer_head *buf);
int write_ctree_super(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      struct btrfs_super_block *s);
int mkfs(int fd, u64 num_blocks, u32 blocksize);
#endif
