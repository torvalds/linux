#include <stdio.h>
#include <stdlib.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

/* for testing only */
int next_key(int i, int max_key) {
	return rand() % max_key;
	//return i;
}

int main(int ac, char **av) {
	struct key ins;
	struct key last = { (u64)-1, 0, 0};
	char *buf;
	int i;
	int num;
	int ret;
	int run_size = 100000;
	int max_key =  100000000;
	int tree_size = 0;
	struct ctree_path path;
	struct ctree_super_block super;
	struct ctree_root *root;

	radix_tree_init();

	root = open_ctree("dbfile", &super);
	srand(55);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		// num = i;
		sprintf(buf, "string-%d", num);
		if (i % 10000 == 0)
			fprintf(stderr, "insert %d:%d\n", num, i);
		ins.objectid = num;
		ins.offset = 0;
		ins.flags = 0;
		ret = insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
		free(buf);
	}
	write_ctree_super(root, &super);
	close_ctree(root);

	root = open_ctree("dbfile", &super);
	printf("starting search\n");
	srand(55);
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		if (i % 10000 == 0)
			fprintf(stderr, "search %d:%d\n", num, i);
		ret = search_slot(root, &ins, &path, 0);
		if (ret) {
			print_tree(root, root->node);
			printf("unable to find %d\n", num);
			exit(1);
		}
		release_path(root, &path);
	}
	write_ctree_super(root, &super);
	close_ctree(root);
	root = open_ctree("dbfile", &super);
	printf("node %p level %d total ptrs %d free spc %lu\n", root->node,
	        node_level(root->node->node.header.flags),
		root->node->node.header.nritems,
		NODEPTRS_PER_BLOCK - root->node->node.header.nritems);
	printf("all searches good, deleting some items\n");
	i = 0;
	srand(55);
	for (i = 0 ; i < run_size/4; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		ret = search_slot(root, &ins, &path, -1);
		if (!ret) {
			if (i % 10000 == 0)
				fprintf(stderr, "del %d:%d\n", num, i);
			ret = del_item(root, &path);
			if (ret != 0)
				BUG();
			tree_size--;
		}
		release_path(root, &path);
	}
	write_ctree_super(root, &super);
	close_ctree(root);
	root = open_ctree("dbfile", &super);
	srand(128);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		sprintf(buf, "string-%d", num);
		ins.objectid = num;
		if (i % 10000 == 0)
			fprintf(stderr, "insert %d:%d\n", num, i);
		ret = insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
		free(buf);
	}
	write_ctree_super(root, &super);
	close_ctree(root);
	root = open_ctree("dbfile", &super);
	srand(128);
	printf("starting search2\n");
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		init_path(&path);
		if (i % 10000 == 0)
			fprintf(stderr, "search %d:%d\n", num, i);
		ret = search_slot(root, &ins, &path, 0);
		if (ret) {
			print_tree(root, root->node);
			printf("unable to find %d\n", num);
			exit(1);
		}
		release_path(root, &path);
	}
	printf("starting big long delete run\n");
	while(root->node && root->node->node.header.nritems > 0) {
		struct leaf *leaf;
		int slot;
		ins.objectid = (u64)-1;
		init_path(&path);
		ret = search_slot(root, &ins, &path, -1);
		if (ret == 0)
			BUG();

		leaf = &path.nodes[0]->leaf;
		slot = path.slots[0];
		if (slot != leaf->header.nritems)
			BUG();
		while(path.slots[0] > 0) {
			path.slots[0] -= 1;
			slot = path.slots[0];
			leaf = &path.nodes[0]->leaf;

			memcpy(&last, &leaf->items[slot].key, sizeof(last));
			if (tree_size % 10000 == 0)
				printf("big del %d:%d\n", tree_size, i);
			ret = del_item(root, &path);
			if (ret != 0) {
				printf("del_item returned %d\n", ret);
				BUG();
			}
			tree_size--;
		}
		release_path(root, &path);
	}
	printf("tree size is now %d\n", tree_size);
	printf("map tree\n");
	print_tree(root->extent_root, root->extent_root->node);
	write_ctree_super(root, &super);
	close_ctree(root);
	return 0;
}
