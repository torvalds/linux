#ifndef __DISKIO__
#define __DISKIO__

#include <linux/buffer_head.h>

#define BTRFS_SUPER_INFO_OFFSET (16 * 1024)

enum btrfs_bh_state_bits {
	BH_Checked = BH_PrivateStart,
};
BUFFER_FNS(Checked, checked);

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
int readahead_tree_block(struct btrfs_root *root, u64 blocknr);
struct buffer_head *btrfs_find_create_tree_block(struct btrfs_root *root,
						 u64 blocknr);
int write_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf);
int dirty_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct buffer_head *buf);
int clean_tree_block(struct btrfs_trans_handle *trans,
		     struct btrfs_root *root, struct buffer_head *buf);
int btrfs_commit_transaction(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root);
struct btrfs_root *open_ctree(struct super_block *sb);
int close_ctree(struct btrfs_root *root);
void btrfs_block_release(struct btrfs_root *root, struct buffer_head *buf);
int write_ctree_super(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root);
struct buffer_head *btrfs_find_tree_block(struct btrfs_root *root, u64 blocknr);
int btrfs_csum_data(struct btrfs_root * root, char *data, size_t len,
		    char *result);
struct btrfs_root *btrfs_read_fs_root(struct btrfs_fs_info *fs_info,
				      struct btrfs_key *location);
u64 bh_blocknr(struct buffer_head *bh);
int btrfs_insert_dev_radix(struct btrfs_root *root,
			   struct block_device *bdev,
			   u64 device_id,
			   u64 block_start,
			   u64 num_blocks);
int btrfs_map_bh_to_logical(struct btrfs_root *root, struct buffer_head *bh,
			     u64 logical);
int btrfs_releasepage(struct page *page, gfp_t flags);
void btrfs_btree_balance_dirty(struct btrfs_root *root);
#endif
