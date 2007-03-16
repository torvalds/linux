#ifndef __PRINT_TREE_
#define __PRINT_TREE_
void btrfs_print_leaf(struct btrfs_root *root, struct btrfs_leaf *l);
void btrfs_print_tree(struct btrfs_root *root, struct btrfs_buffer *t);
#endif
