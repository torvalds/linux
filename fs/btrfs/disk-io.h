#ifndef __DISKIO__
#define __DISKIO__
#include "list.h"

struct tree_buffer {
	u64 blocknr;
	int count;
	union {
		struct node node;
		struct leaf leaf;
	};
	struct list_head dirty;
	struct list_head cache;
};

struct tree_buffer *read_tree_block(struct ctree_root *root, u64 blocknr);
struct tree_buffer *find_tree_block(struct ctree_root *root, u64 blocknr);
int write_tree_block(struct ctree_root *root, struct tree_buffer *buf);
int dirty_tree_block(struct ctree_root *root, struct tree_buffer *buf);
int clean_tree_block(struct ctree_root *root, struct tree_buffer *buf);
int commit_transaction(struct ctree_root *root);
struct ctree_root *open_ctree(char *filename, struct ctree_super_block *s);
int close_ctree(struct ctree_root *root);
void tree_block_release(struct ctree_root *root, struct tree_buffer *buf);
int write_ctree_super(struct ctree_root *root, struct ctree_super_block *s);
int mkfs(int fd);

#define CTREE_SUPER_INFO_OFFSET(bs) (16 * (bs))

#endif
