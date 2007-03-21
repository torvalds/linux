#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "hash.h"
#include "transaction.h"

int keep_running = 1;
struct btrfs_super_block super;
static u64 dir_oid = 44556;
static u64 file_oid = 33778;

static int find_num(struct radix_tree_root *root, unsigned long *num_ret,
		     int exists)
{
	unsigned long num = rand();
	unsigned long res[2];
	int ret;

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
	*num_ret = num;
	return 0;
}

static void initial_inode_init(struct btrfs_root *root,
			       struct btrfs_inode_item *inode_item)
{
	memset(inode_item, 0, sizeof(*inode_item));
	btrfs_set_inode_generation(inode_item, root->fs_info->generation);
}

static int ins_one(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct radix_tree_root *radix)
{
	int ret;
	char buf[128];
	unsigned long oid;
	u64 objectid;
	struct btrfs_path path;
	struct btrfs_key inode_map;
	struct btrfs_inode_item inode_item;

	find_num(radix, &oid, 0);
	sprintf(buf, "str-%lu", oid);

	ret = btrfs_find_free_objectid(trans, root, dir_oid + 1, &objectid);
	if (ret)
		goto error;

	inode_map.objectid = objectid;
	inode_map.flags = 0;
	inode_map.offset = 0;

	ret = btrfs_insert_inode_map(trans, root, objectid, &inode_map);
	if (ret)
		goto error;

	initial_inode_init(root, &inode_item);
	ret = btrfs_insert_inode(trans, root, objectid, &inode_item);
	if (ret)
		goto error;
	ret = btrfs_insert_dir_item(trans, root, buf, strlen(buf), dir_oid,
				    objectid, 1);
	if (ret)
		goto error;

	radix_tree_preload(GFP_KERNEL);
	ret = radix_tree_insert(radix, oid, (void *)oid);
	radix_tree_preload_end();
	if (ret)
		goto error;
	return ret;
error:
	if (ret != -EEXIST)
		goto fatal;

	/*
	 * if we got an EEXIST, it may be due to hash collision, double
	 * check
	 */
	btrfs_init_path(&path);
	ret = btrfs_lookup_dir_item(trans, root, &path, dir_oid, buf,
				    strlen(buf), 0);
	if (ret)
		goto fatal_release;
	if (!btrfs_match_dir_item_name(root, &path, buf, strlen(buf))) {
		struct btrfs_dir_item *di;
		char *found;
		u32 found_len;
		u64 myhash;
		u64 foundhash;

		di = btrfs_item_ptr(&path.nodes[0]->leaf, path.slots[0],
				    struct btrfs_dir_item);
		found = (char *)(di + 1);
		found_len = btrfs_dir_name_len(di);
		btrfs_name_hash(buf, strlen(buf), &myhash);
		btrfs_name_hash(found, found_len, &foundhash);
		if (myhash != foundhash)
			goto fatal_release;
		btrfs_release_path(root, &path);
		return 0;
	}
fatal_release:
	btrfs_release_path(root, &path);
fatal:
	printf("failed to insert %lu ret %d\n", oid, ret);
	return -1;
}

static int insert_dup(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct radix_tree_root *radix)
{
	int ret;
	char buf[128];
	unsigned long oid;

	ret = find_num(radix, &oid, 1);
	if (ret < 0)
		return 0;
	sprintf(buf, "str-%lu", oid);

	ret = btrfs_insert_dir_item(trans, root, buf, strlen(buf), dir_oid,
				    file_oid, 1);
	if (ret != -EEXIST) {
		printf("insert on %s gave us %d\n", buf, ret);
		return 1;
	}
	return 0;
}

static int del_dir_item(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct radix_tree_root *radix,
			unsigned long radix_index,
			struct btrfs_path *path)
{
	int ret;
	unsigned long *ptr;
	u64 file_objectid;
	struct btrfs_dir_item *di;

	/* find the inode number of the file */
	di = btrfs_item_ptr(&path->nodes[0]->leaf, path->slots[0],
			    struct btrfs_dir_item);
	file_objectid = btrfs_dir_objectid(di);

	/* delete the directory item */
	ret = btrfs_del_item(trans, root, path);
	if (ret)
		goto out_release;
	btrfs_release_path(root, path);

	/* delete the inode */
	btrfs_init_path(path);
	ret = btrfs_lookup_inode(trans, root, path, file_objectid, -1);
	if (ret)
		goto out_release;
	ret = btrfs_del_item(trans, root, path);
	if (ret)
		goto out_release;
	btrfs_release_path(root, path);

	/* delete the inode mapping */
	btrfs_init_path(path);
	ret = btrfs_lookup_inode_map(trans, root, path, file_objectid, -1);
	if (ret)
		goto out_release;
	ret = btrfs_del_item(trans, root->fs_info->inode_root, path);
	if (ret)
		goto out_release;

	if (root->fs_info->last_inode_alloc > file_objectid)
		root->fs_info->last_inode_alloc = file_objectid;
	btrfs_release_path(root, path);
	ptr = radix_tree_delete(radix, radix_index);
	if (!ptr) {
		ret = -5555;
		goto out;
	}
	return 0;
out_release:
	btrfs_release_path(root, path);
out:
	printf("failed to delete %lu %d\n", radix_index, ret);
	return -1;
}

static int del_one(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct radix_tree_root *radix)
{
	int ret;
	char buf[128];
	unsigned long oid;
	struct btrfs_path path;

	ret = find_num(radix, &oid, 1);
	if (ret < 0)
		return 0;
	sprintf(buf, "str-%lu", oid);
	btrfs_init_path(&path);
	ret = btrfs_lookup_dir_item(trans, root, &path, dir_oid, buf,
				    strlen(buf), -1);
	if (ret)
		goto out_release;

	ret = del_dir_item(trans, root, radix, oid, &path);
	if (ret)
		goto out_release;
	return ret;
out_release:
	btrfs_release_path(root, &path);
	printf("failed to delete %lu %d\n", oid, ret);
	return -1;
}

static int lookup_item(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, struct radix_tree_root *radix)
{
	struct btrfs_path path;
	char buf[128];
	int ret;
	unsigned long oid;
	u64 objectid;
	struct btrfs_dir_item *di;

	ret = find_num(radix, &oid, 1);
	if (ret < 0)
		return 0;
	sprintf(buf, "str-%lu", oid);
	btrfs_init_path(&path);
	ret = btrfs_lookup_dir_item(trans, root, &path, dir_oid, buf,
				    strlen(buf), 0);
	if (!ret) {
		di = btrfs_item_ptr(&path.nodes[0]->leaf, path.slots[0],
				    struct btrfs_dir_item);
		objectid = btrfs_dir_objectid(di);
		btrfs_release_path(root, &path);
		btrfs_init_path(&path);
		ret = btrfs_lookup_inode_map(trans, root, &path, objectid, 0);
	}
	btrfs_release_path(root, &path);
	if (ret) {
		printf("unable to find key %lu\n", oid);
		return -1;
	}
	return 0;
}

static int lookup_enoent(struct btrfs_trans_handle *trans, struct btrfs_root
			 *root, struct radix_tree_root *radix)
{
	struct btrfs_path path;
	char buf[128];
	int ret;
	unsigned long oid;

	ret = find_num(radix, &oid, 0);
	if (ret < 0)
		return 0;
	sprintf(buf, "str-%lu", oid);
	btrfs_init_path(&path);
	ret = btrfs_lookup_dir_item(trans, root, &path, dir_oid, buf,
				    strlen(buf), 0);
	btrfs_release_path(root, &path);
	if (!ret) {
		printf("able to find key that should not exist %lu\n", oid);
		return -1;
	}
	return 0;
}

static int empty_tree(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct radix_tree_root *radix, int nr)
{
	struct btrfs_path path;
	struct btrfs_key key;
	unsigned long found = 0;
	u32 found_len;
	int ret;
	int slot;
	int count = 0;
	char buf[128];
	struct btrfs_dir_item *di;

	key.offset = (u64)-1;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_DIR_ITEM_KEY);
	key.objectid = dir_oid;
	while(nr-- >= 0) {
		btrfs_init_path(&path);
		ret = btrfs_search_slot(trans, root, &key, &path, -1, 1);
		if (ret < 0) {
			btrfs_release_path(root, &path);
			return ret;
		}
		if (ret != 0) {
			if (path.slots[0] == 0) {
				btrfs_release_path(root, &path);
				break;
			}
			path.slots[0] -= 1;
		}
		slot = path.slots[0];
		di = btrfs_item_ptr(&path.nodes[0]->leaf, slot,
				    struct btrfs_dir_item);
		found_len = btrfs_dir_name_len(di);
		memcpy(buf, (char *)(di + 1), found_len);
		BUG_ON(found_len > 128);
		buf[found_len] = '\0';
		found = atoi(buf + 4);
		ret = del_dir_item(trans, root, radix, found, &path);
		count++;
		if (ret) {
			fprintf(stderr,
				"failed to remove %lu from tree\n",
				found);
			return -1;
		}
		if (!keep_running)
			break;
	}
	return 0;
	fprintf(stderr, "failed to delete from the radix %lu\n", found);
	return -1;
}

static int fill_tree(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct radix_tree_root *radix, int count)
{
	int i;
	int ret = 0;
	for (i = 0; i < count; i++) {
		ret = ins_one(trans, root, radix);
		if (ret) {
			fprintf(stderr, "fill failed\n");
			goto out;
		}
		if (i % 1000 == 0) {
			ret = btrfs_commit_transaction(trans, root, &super);
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

static int bulk_op(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct radix_tree_root *radix)
{
	int ret;
	int nr = rand() % 5000;
	static int run_nr = 0;

	/* do the bulk op much less frequently */
	if (run_nr++ % 100)
		return 0;
	ret = empty_tree(trans, root, radix, nr);
	if (ret)
		return ret;
	ret = fill_tree(trans, root, radix, nr);
	if (ret)
		return ret;
	return 0;
}


int (*ops[])(struct btrfs_trans_handle *trans, struct btrfs_root *root, struct
	     radix_tree_root *radix) =
	{ ins_one, insert_dup, del_one, lookup_item,
	  lookup_enoent, bulk_op };

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
	struct btrfs_root *root;
	int i;
	int ret;
	int count;
	int op;
	int iterations = 20000;
	int init_fill_count = 800000;
	int err = 0;
	int initial_only = 0;
	struct btrfs_trans_handle *trans;
	radix_tree_init();

	root = open_ctree("dbfile", &super);
	trans = btrfs_start_transaction(root, 1);

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
	ret = fill_tree(trans, root, &radix, init_fill_count);
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
			ret = ops[op](trans, root, &radix);
			if (ret) {
				fprintf(stderr, "op %d failed %d:%d\n",
					op, i, iterations);
				btrfs_print_tree(root, root->node);
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

