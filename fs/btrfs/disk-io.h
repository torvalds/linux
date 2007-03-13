#ifndef __DISKIO__
#define __DISKIO__
#include "list.h"

struct btrfs_buffer {
	u64 blocknr;
	int count;
	union {
		struct btrfs_node node;
		struct btrfs_leaf leaf;
	};
	struct list_head dirty;
	struct list_head cache;
};

struct btrfs_buffer *read_tree_block(struct btrfs_root *root, u64 blocknr);
struct btrfs_buffer *find_tree_block(struct btrfs_root *root, u64 blocknr);
int write_tree_block(struct btrfs_root *root, struct btrfs_buffer *buf);
int dirty_tree_block(struct btrfs_root *root, struct btrfs_buffer *buf);
int clean_tree_block(struct btrfs_root *root, struct btrfs_buffer *buf);
int btrfs_commit_transaction(struct btrfs_root *root,
			     struct btrfs_super_block *s);
struct btrfs_root *open_ctree(char *filename, struct btrfs_super_block *s);
int close_ctree(struct btrfs_root *root, struct btrfs_super_block *s);
void btrfs_block_release(struct btrfs_root *root, struct btrfs_buffer *buf);
int write_ctree_super(struct btrfs_root *root, struct btrfs_super_block *s);
int mkfs(int fd, u64 num_blocks, u16 blocksize);


#define BTRFS_SUPER_INFO_OFFSET(bs) (16 * (bs))

#endif
