#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

int main() {
	struct ctree_super_block super;
	struct ctree_root *root;
	radix_tree_init();
	root = open_ctree("dbfile", &super);
	printf("root tree\n");
	print_tree(root, root->node);
	printf("map tree\n");
	print_tree(root->extent_root, root->extent_root->node);
	return 0;
}
