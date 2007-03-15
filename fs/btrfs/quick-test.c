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
	// return i;
}

int main(int ac, char **av) {
	struct btrfs_key ins;
	struct btrfs_key last = { (u64)-1, 0, 0};
	char *buf;
	int i;
	int num;
	int ret;
	int run_size = 100000;
	int max_key =  100000000;
	int tree_size = 0;
	struct btrfs_path path;
	struct btrfs_super_block super;
	struct btrfs_root *root;

	radix_tree_init();

	root = open_ctree("dbfile", &super);
	srand(55);
	ins.flags = 0;
	btrfs_set_key_type(&ins, BTRFS_STRING_ITEM_KEY);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		// num = i;
		sprintf(buf, "string-%d", num);
		if (i % 10000 == 0)
			fprintf(stderr, "insert %d:%d\n", num, i);
		ins.objectid = num;
		ins.offset = 0;
		ret = btrfs_insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
		free(buf);
		if (i == run_size - 5) {
			btrfs_commit_transaction(root, &super);
		}

	}
	close_ctree(root, &super);

	root = open_ctree("dbfile", &super);
	printf("starting search\n");
	srand(55);
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		btrfs_init_path(&path);
		if (i % 10000 == 0)
			fprintf(stderr, "search %d:%d\n", num, i);
		ret = btrfs_search_slot(root, &ins, &path, 0, 0);
		if (ret) {
			btrfs_print_tree(root, root->node);
			printf("unable to find %d\n", num);
			exit(1);
		}
		btrfs_release_path(root, &path);
	}
	close_ctree(root, &super);
	root = open_ctree("dbfile", &super);
	printf("node %p level %d total ptrs %d free spc %lu\n", root->node,
	        btrfs_header_level(&root->node->node.header),
		btrfs_header_nritems(&root->node->node.header),
		BTRFS_NODEPTRS_PER_BLOCK(root) -
		btrfs_header_nritems(&root->node->node.header));
	printf("all searches good, deleting some items\n");
	i = 0;
	srand(55);
	for (i = 0 ; i < run_size/4; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		btrfs_init_path(&path);
		ret = btrfs_search_slot(root, &ins, &path, -1, 1);
		if (!ret) {
			if (i % 10000 == 0)
				fprintf(stderr, "del %d:%d\n", num, i);
			ret = btrfs_del_item(root, &path);
			if (ret != 0)
				BUG();
			tree_size--;
		}
		btrfs_release_path(root, &path);
	}
	close_ctree(root, &super);
	root = open_ctree("dbfile", &super);
	srand(128);
	for (i = 0; i < run_size; i++) {
		buf = malloc(64);
		num = next_key(i, max_key);
		sprintf(buf, "string-%d", num);
		ins.objectid = num;
		if (i % 10000 == 0)
			fprintf(stderr, "insert %d:%d\n", num, i);
		ret = btrfs_insert_item(root, &ins, buf, strlen(buf));
		if (!ret)
			tree_size++;
		free(buf);
	}
	close_ctree(root, &super);
	root = open_ctree("dbfile", &super);
	srand(128);
	printf("starting search2\n");
	for (i = 0; i < run_size; i++) {
		num = next_key(i, max_key);
		ins.objectid = num;
		btrfs_init_path(&path);
		if (i % 10000 == 0)
			fprintf(stderr, "search %d:%d\n", num, i);
		ret = btrfs_search_slot(root, &ins, &path, 0, 0);
		if (ret) {
			btrfs_print_tree(root, root->node);
			printf("unable to find %d\n", num);
			exit(1);
		}
		btrfs_release_path(root, &path);
	}
	printf("starting big long delete run\n");
	while(root->node &&
	      btrfs_header_nritems(&root->node->node.header) > 0) {
		struct btrfs_leaf *leaf;
		int slot;
		ins.objectid = (u64)-1;
		btrfs_init_path(&path);
		ret = btrfs_search_slot(root, &ins, &path, -1, 1);
		if (ret == 0)
			BUG();

		leaf = &path.nodes[0]->leaf;
		slot = path.slots[0];
		if (slot != btrfs_header_nritems(&leaf->header))
			BUG();
		while(path.slots[0] > 0) {
			path.slots[0] -= 1;
			slot = path.slots[0];
			leaf = &path.nodes[0]->leaf;

			btrfs_disk_key_to_cpu(&last, &leaf->items[slot].key);
			if (tree_size % 10000 == 0)
				printf("big del %d:%d\n", tree_size, i);
			ret = btrfs_del_item(root, &path);
			if (ret != 0) {
				printf("del_item returned %d\n", ret);
				BUG();
			}
			tree_size--;
		}
		btrfs_release_path(root, &path);
	}
	/*
	printf("previous tree:\n");
	btrfs_print_tree(root, root->commit_root);
	printf("map before commit\n");
	btrfs_print_tree(root->extent_root, root->extent_root->node);
	*/
	btrfs_commit_transaction(root, &super);
	printf("tree size is now %d\n", tree_size);
	printf("root %p commit root %p\n", root->node, root->commit_root);
	printf("map tree\n");
	btrfs_print_tree(root->extent_root, root->extent_root->node);
	close_ctree(root, &super);
	return 0;
}
