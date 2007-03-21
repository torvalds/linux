#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"

int main(int ac, char **av) {
	struct btrfs_super_block super;
	struct btrfs_root *root;

	if (ac != 2) {
		fprintf(stderr, "usage: %s device\n", av[0]);
		exit(1);
	}
	radix_tree_init();
	root = open_ctree(av[1], &super);
	if (!root) {
		fprintf(stderr, "unable to open %s\n", av[1]);
		exit(1);
	}
	printf("fs tree\n");
	btrfs_print_tree(root, root->node);
	printf("map tree\n");
	btrfs_print_tree(root->fs_info->extent_root,
			 root->fs_info->extent_root->node);
	printf("inode tree\n");
	btrfs_print_tree(root->fs_info->inode_root,
			 root->fs_info->inode_root->node);
	printf("root tree\n");
	btrfs_print_tree(root->fs_info->tree_root,
			 root->fs_info->tree_root->node);
	printf("total blocks %Lu\n", btrfs_super_total_blocks(&super));
	printf("blocks used %Lu\n", btrfs_super_blocks_used(&super));
	return 0;
}
