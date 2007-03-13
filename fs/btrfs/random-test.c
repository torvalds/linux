#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"

int keep_running = 1;
struct ctree_super_block super;

static int setup_key(struct radix_tree_root *root, struct btrfs_key *key,
		     int exists)
{
	int num = rand();
	unsigned long res[2];
	int ret;

	key->flags = 0;
	key->offset = 0;
again:
	ret = radix_tree_gang_lookup(root, (void **)res, num, 2);
	if (exists) {
		if (ret == 0)
			return -1;
		num = res[0];
	} else if (ret != 0 && num == res[0]) {
		num++;
		if (ret > 1 && num == res[1]) {
			num++;
			goto again;
		}
	}
	key->objectid = num;
	return 0;
}

static int ins_one(struct ctree_root *root, struct radix_tree_root *radix)
{
	struct ctree_path path;
	struct btrfs_key key;
	int ret;
	char buf[128];
	unsigned long oid;
	init_path(&path);
	ret = setup_key(radix, &key, 0);
	sprintf(buf, "str-%Lu\n", key.objectid);
	ret = insert_item(root, &key, buf, strlen(buf));
	if (ret)
		goto error;
	oid = (unsigned long)key.objectid;
	radix_tree_preload(GFP_KERNEL);
	ret = radix_tree_insert(radix, oid, (void *)oid);
	radix_tree_preload_end();
	if (ret)
		goto error;
	return ret;
error:
	printf("failed to insert %Lu\n", key.objectid);
	return -1;
}

static int insert_dup(struct ctree_root *root, struct radix_tree_root *radix)
{
	struct ctree_path path;
	struct btrfs_key key;
	int ret;
	char buf[128];
	init_path(&path);
	ret = setup_key(radix, &key, 1);
	if (ret < 0)
		return 0;
	sprintf(buf, "str-%Lu\n", key.objectid);
	ret = insert_item(root, &key, buf, strlen(buf));
	if (ret != -EEXIST) {
		printf("insert on %Lu gave us %d\n", key.objectid, ret);
		return 1;
	}
	return 0;
}

static int del_one(struct ctree_root *root, struct radix_tree_root *radix)
{
	struct ctree_path path;
	struct btrfs_key key;
	int ret;
	unsigned long *ptr;
	init_path(&path);
	ret = setup_key(radix, &key, 1);
	if (ret < 0)
		return 0;
	ret = search_slot(root, &key, &path, -1, 1);
	if (ret)
		goto error;
	ret = del_item(root, &path);
	release_path(root, &path);
	if (ret != 0)
		goto error;
	ptr = radix_tree_delete(radix, key.objectid);
	if (!ptr)
		goto error;
	return 0;
error:
	printf("failed to delete %Lu\n", key.objectid);
	return -1;
}

static int lookup_item(struct ctree_root *root, struct radix_tree_root *radix)
{
	struct ctree_path path;
	struct btrfs_key key;
	int ret;
	init_path(&path);
	ret = setup_key(radix, &key, 1);
	if (ret < 0)
		return 0;
	ret = search_slot(root, &key, &path, 0, 1);
	release_path(root, &path);
	if (ret)
		goto error;
	return 0;
error:
	printf("unable to find key %Lu\n", key.objectid);
	return -1;
}

static int lookup_enoent(struct ctree_root *root, struct radix_tree_root *radix)
{
	struct ctree_path path;
	struct btrfs_key key;
	int ret;
	init_path(&path);
	ret = setup_key(radix, &key, 0);
	if (ret < 0)
		return ret;
	ret = search_slot(root, &key, &path, 0, 0);
	release_path(root, &path);
	if (ret <= 0)
		goto error;
	return 0;
error:
	printf("able to find key that should not exist %Lu\n", key.objectid);
	return -1;
}

static int empty_tree(struct ctree_root *root, struct radix_tree_root *radix,
		      int nr)
{
	struct ctree_path path;
	struct btrfs_key key;
	unsigned long found = 0;
	int ret;
	int slot;
	int *ptr;
	int count = 0;

	key.offset = 0;
	key.flags = 0;
	key.objectid = (unsigned long)-1;
	while(nr-- >= 0) {
		init_path(&path);
		ret = search_slot(root, &key, &path, -1, 1);
		if (ret < 0) {
			release_path(root, &path);
			return ret;
		}
		if (ret != 0) {
			if (path.slots[0] == 0) {
				release_path(root, &path);
				break;
			}
			path.slots[0] -= 1;
		}
		slot = path.slots[0];
		found=btrfs_key_objectid(&path.nodes[0]->leaf.items[slot].key);
		ret = del_item(root, &path);
		count++;
		if (ret) {
			fprintf(stderr,
				"failed to remove %lu from tree\n",
				found);
			return -1;
		}
		release_path(root, &path);
		ptr = radix_tree_delete(radix, found);
		if (!ptr)
			goto error;
		if (!keep_running)
			break;
	}
	return 0;
error:
	fprintf(stderr, "failed to delete from the radix %lu\n", found);
	return -1;
}

static int fill_tree(struct ctree_root *root, struct radix_tree_root *radix,
		     int count)
{
	int i;
	int ret = 0;
	for (i = 0; i < count; i++) {
		ret = ins_one(root, radix);
		if (ret) {
			fprintf(stderr, "fill failed\n");
			goto out;
		}
		if (i % 1000 == 0) {
			ret = commit_transaction(root, &super);
			if (ret) {
				fprintf(stderr, "fill commit failed\n");
				return ret;
			}
		}
		if (i && i % 10000 == 0) {
			printf("bigfill %d\n", i);
		}
		if (!keep_running)
			break;
	}
out:
	return ret;
}

static int bulk_op(struct ctree_root *root, struct radix_tree_root *radix)
{
	int ret;
	int nr = rand() % 5000;
	static int run_nr = 0;

	/* do the bulk op much less frequently */
	if (run_nr++ % 100)
		return 0;
	ret = empty_tree(root, radix, nr);
	if (ret)
		return ret;
	ret = fill_tree(root, radix, nr);
	if (ret)
		return ret;
	return 0;
}


int (*ops[])(struct ctree_root *root, struct radix_tree_root *radix) =
	{ ins_one, insert_dup, del_one, lookup_item,
	  lookup_enoent, bulk_op };

static int fill_radix(struct ctree_root *root, struct radix_tree_root *radix)
{
	struct ctree_path path;
	struct btrfs_key key;
	unsigned long found;
	int ret;
	int slot;
	int i;

	key.offset = 0;
	key.flags = 0;
	key.objectid = (unsigned long)-1;
	while(1) {
		init_path(&path);
		ret = search_slot(root, &key, &path, 0, 0);
		if (ret < 0) {
			release_path(root, &path);
			return ret;
		}
		slot = path.slots[0];
		if (ret != 0) {
			if (slot == 0) {
				release_path(root, &path);
				break;
			}
			slot -= 1;
		}
		for (i = slot; i >= 0; i--) {
			found = btrfs_key_objectid(&path.nodes[0]->
						   leaf.items[i].key);
			radix_tree_preload(GFP_KERNEL);
			ret = radix_tree_insert(radix, found, (void *)found);
			if (ret) {
				fprintf(stderr,
					"failed to insert %lu into radix\n",
					found);
				exit(1);
			}

			radix_tree_preload_end();
		}
		release_path(root, &path);
		key.objectid = found - 1;
		if (key.objectid > found)
			break;
	}
	return 0;
}
void sigstopper(int ignored)
{
	keep_running = 0;
	fprintf(stderr, "caught exit signal, stopping\n");
}

int print_usage(void)
{
	printf("usage: tester [-ih] [-c count] [-f count]\n");
	printf("\t -c count -- iteration count after filling\n");
	printf("\t -f count -- run this many random inserts before starting\n");
	printf("\t -i       -- only do initial fill\n");
	printf("\t -h       -- this help text\n");
	exit(1);
}
int main(int ac, char **av)
{
	RADIX_TREE(radix, GFP_KERNEL);
	struct ctree_root *root;
	int i;
	int ret;
	int count;
	int op;
	int iterations = 20000;
	int init_fill_count = 800000;
	int err = 0;
	int initial_only = 0;
	radix_tree_init();
	root = open_ctree("dbfile", &super);
	fill_radix(root, &radix);

	signal(SIGTERM, sigstopper);
	signal(SIGINT, sigstopper);

	for (i = 1 ; i < ac ; i++) {
		if (strcmp(av[i], "-i") == 0) {
			initial_only = 1;
		} else if (strcmp(av[i], "-c") == 0) {
			iterations = atoi(av[i+1]);
			i++;
		} else if (strcmp(av[i], "-f") == 0) {
			init_fill_count = atoi(av[i+1]);
			i++;
		} else {
			print_usage();
		}
	}
	printf("initial fill\n");
	ret = fill_tree(root, &radix, init_fill_count);
	printf("starting run\n");
	if (ret) {
		err = ret;
		goto out;
	}
	if (initial_only == 1) {
		goto out;
	}
	for (i = 0; i < iterations; i++) {
		op = rand() % ARRAY_SIZE(ops);
		count = rand() % 128;
		if (i % 2000 == 0) {
			printf("%d\n", i);
			fflush(stdout);
		}
		if (i && i % 5000 == 0) {
			printf("open & close, root level %d nritems %d\n",
				btrfs_header_level(&root->node->node.header),
				btrfs_header_nritems(&root->node->node.header));
			close_ctree(root, &super);
			root = open_ctree("dbfile", &super);
		}
		while(count--) {
			ret = ops[op](root, &radix);
			if (ret) {
				fprintf(stderr, "op %d failed %d:%d\n",
					op, i, iterations);
				print_tree(root, root->node);
				fprintf(stderr, "op %d failed %d:%d\n",
					op, i, iterations);
				err = ret;
				goto out;
			}
			if (ops[op] == bulk_op)
				break;
			if (keep_running == 0) {
				err = 0;
				goto out;
			}
		}
	}
out:
	close_ctree(root, &super);
	return err;
}

