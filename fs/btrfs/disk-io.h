#ifndef __DISKIO__
#define __DISKIO__

struct tree_buffer {
	u64 blocknr;
	int count;
	union {
		struct node node;
		struct leaf leaf;
	};
};

struct tree_buffer *read_tree_block(struct ctree_root *root, u64 blocknr);
int write_tree_block(struct ctree_root *root, struct tree_buffer *buf);
struct ctree_root *open_ctree(char *filename);
int close_ctree(struct ctree_root *root);
void tree_block_release(struct ctree_root *root, struct tree_buffer *buf);
struct tree_buffer *alloc_free_block(struct ctree_root *root);
int update_root_block(struct ctree_root *root);
int mkfs(int fd);

#define CTREE_SUPER_INFO_OFFSET(bs) (16 * (bs))

#endif
