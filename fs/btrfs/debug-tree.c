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
	radix_tree_init();
	root = open_ctree("dbfile", &super);
	printf("fs tree\n");
	btrfs_print_tree(root, root->node);
	printf("map tree\n");
	btrfs_print_tree(root->extent_root, root->extent_root->node);
	printf("root tree\n");
	btrfs_print_tree(root->tree_root, root->tree_root->node);
	return 0;
}
