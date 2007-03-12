#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"

static int allocated_blocks = 0;
int cache_max = 10000;

static int check_tree_block(struct ctree_root *root, struct tree_buffer *buf)
{
	if (buf->blocknr != btrfs_header_blocknr(&buf->node.header))
		BUG();
	if (root->node && btrfs_header_parentid(&buf->node.header) !=
	    btrfs_header_parentid(&root->node->node.header))
		BUG();
	return 0;
}

static int free_some_buffers(struct ctree_root *root)
{
	struct list_head *node, *next;
	struct tree_buffer *b;
	if (root->cache_size < cache_max)
		return 0;
	list_for_each_safe(node, next, &root->cache) {
		b = list_entry(node, struct tree_buffer, cache);
		if (b->count == 1) {
			BUG_ON(!list_empty(&b->dirty));
			list_del_init(&b->cache);
			tree_block_release(root, b);
			if (root->cache_size < cache_max)
				break;
		}
	}
	return 0;
}

struct tree_buffer *alloc_tree_block(struct ctree_root *root, u64 blocknr)
{
	struct tree_buffer *buf;
	int ret;
	buf = malloc(sizeof(struct tree_buffer));
	if (!buf)
		return buf;
	allocated_blocks++;
	buf->blocknr = blocknr;
	buf->count = 2;
	INIT_LIST_HEAD(&buf->dirty);
	free_some_buffers(root);
	radix_tree_preload(GFP_KERNEL);
	ret = radix_tree_insert(&root->cache_radix, blocknr, buf);
	radix_tree_preload_end();
	list_add_tail(&buf->cache, &root->cache);
	root->cache_size++;
	if (ret) {
		free(buf);
		return NULL;
	}
	return buf;
}

struct tree_buffer *find_tree_block(struct ctree_root *root, u64 blocknr)
{
	struct tree_buffer *buf;
	buf = radix_tree_lookup(&root->cache_radix, blocknr);
	if (buf) {
		buf->count++;
	} else {
		buf = alloc_tree_block(root, blocknr);
		if (!buf) {
			BUG();
			return NULL;
		}
	}
	return buf;
}

struct tree_buffer *read_tree_block(struct ctree_root *root, u64 blocknr)
{
	loff_t offset = blocknr * CTREE_BLOCKSIZE;
	struct tree_buffer *buf;
	int ret;

	buf = radix_tree_lookup(&root->cache_radix, blocknr);
	if (buf) {
		buf->count++;
	} else {
		buf = alloc_tree_block(root, blocknr);
		if (!buf)
			return NULL;
		ret = pread(root->fp, &buf->node, CTREE_BLOCKSIZE, offset);
		if (ret != CTREE_BLOCKSIZE) {
			free(buf);
			return NULL;
		}
	}
	if (check_tree_block(root, buf))
		BUG();
	return buf;
}

int dirty_tree_block(struct ctree_root *root, struct tree_buffer *buf)
{
	if (!list_empty(&buf->dirty))
		return 0;
	list_add_tail(&buf->dirty, &root->trans);
	buf->count++;
	return 0;
}

int clean_tree_block(struct ctree_root *root, struct tree_buffer *buf)
{
	if (!list_empty(&buf->dirty)) {
		list_del_init(&buf->dirty);
		tree_block_release(root, buf);
	}
	return 0;
}

int write_tree_block(struct ctree_root *root, struct tree_buffer *buf)
{
	u64 blocknr = buf->blocknr;
	loff_t offset = blocknr * CTREE_BLOCKSIZE;
	int ret;

	if (buf->blocknr != btrfs_header_blocknr(&buf->node.header))
		BUG();
	ret = pwrite(root->fp, &buf->node, CTREE_BLOCKSIZE, offset);
	if (ret != CTREE_BLOCKSIZE)
		return ret;
	return 0;
}

static int __commit_transaction(struct ctree_root *root)
{
	struct tree_buffer *b;
	int ret = 0;
	int wret;
	while(!list_empty(&root->trans)) {
		b = list_entry(root->trans.next, struct tree_buffer, dirty);
		list_del_init(&b->dirty);
		wret = write_tree_block(root, b);
		if (wret)
			ret = wret;
		tree_block_release(root, b);
	}
	return ret;
}

int commit_transaction(struct ctree_root *root, struct ctree_super_block *s)
{
	int ret = 0;

	ret = __commit_transaction(root);
	if (!ret && root != root->extent_root)
		ret = __commit_transaction(root->extent_root);
	BUG_ON(ret);
	if (root->commit_root != root->node) {
		struct tree_buffer *snap = root->commit_root;
		root->commit_root = root->node;
		root->node->count++;
		ret = btrfs_drop_snapshot(root, snap);
		BUG_ON(ret);
		// tree_block_release(root, snap);
	}
        write_ctree_super(root, s);
	btrfs_finish_extent_commit(root);
	return ret;
}

static int __setup_root(struct ctree_root *root, struct ctree_root *extent_root,
			struct ctree_root_info *info, int fp)
{
	INIT_LIST_HEAD(&root->trans);
	INIT_LIST_HEAD(&root->cache);
	root->cache_size = 0;
	root->fp = fp;
	root->node = NULL;
	root->extent_root = extent_root;
	root->commit_root = NULL;
	root->node = read_tree_block(root, info->tree_root);
	memset(&root->current_insert, 0, sizeof(root->current_insert));
	memset(&root->last_insert, 0, sizeof(root->last_insert));
	return 0;
}

struct ctree_root *open_ctree(char *filename, struct ctree_super_block *super)
{
	struct ctree_root *root = malloc(sizeof(struct ctree_root));
	struct ctree_root *extent_root = malloc(sizeof(struct ctree_root));
	int fp;
	int ret;

	fp = open(filename, O_CREAT | O_RDWR, 0600);
	if (fp < 0) {
		free(root);
		return NULL;
	}
	INIT_RADIX_TREE(&root->cache_radix, GFP_KERNEL);
	INIT_RADIX_TREE(&root->pinned_radix, GFP_KERNEL);
	INIT_RADIX_TREE(&extent_root->pinned_radix, GFP_KERNEL);
	INIT_RADIX_TREE(&extent_root->cache_radix, GFP_KERNEL);
	ret = pread(fp, super, sizeof(struct ctree_super_block),
		     CTREE_SUPER_INFO_OFFSET(CTREE_BLOCKSIZE));
	if (ret == 0 || super->root_info.tree_root == 0) {
		printf("making new FS!\n");
		ret = mkfs(fp);
		if (ret)
			return NULL;
		ret = pread(fp, super, sizeof(struct ctree_super_block),
			     CTREE_SUPER_INFO_OFFSET(CTREE_BLOCKSIZE));
		if (ret != sizeof(struct ctree_super_block))
			return NULL;
	}
	BUG_ON(ret < 0);
	__setup_root(root, extent_root, &super->root_info, fp);
	__setup_root(extent_root, extent_root, &super->extent_info, fp);
	root->commit_root = root->node;
	root->node->count++;
	return root;
}

static int __update_root(struct ctree_root *root, struct ctree_root_info *info)
{
	info->tree_root = root->node->blocknr;
	return 0;
}

int write_ctree_super(struct ctree_root *root, struct ctree_super_block *s)
{
	int ret;
	__update_root(root, &s->root_info);
	__update_root(root->extent_root, &s->extent_info);
	ret = pwrite(root->fp, s, sizeof(*s), CTREE_SUPER_INFO_OFFSET(CTREE_BLOCKSIZE));
	if (ret != sizeof(*s)) {
		fprintf(stderr, "failed to write new super block err %d\n", ret);
		return ret;
	}
	return 0;
}

static int drop_cache(struct ctree_root *root)
{
	while(!list_empty(&root->cache)) {
		struct tree_buffer *b = list_entry(root->cache.next,
						   struct tree_buffer, cache);
		list_del_init(&b->cache);
		tree_block_release(root, b);
	}
	return 0;
}
int close_ctree(struct ctree_root *root, struct ctree_super_block *s)
{
	commit_transaction(root, s);
	__commit_transaction(root->extent_root);
	write_ctree_super(root, s);
	drop_cache(root->extent_root);
	drop_cache(root);
	BUG_ON(!list_empty(&root->trans));
	BUG_ON(!list_empty(&root->extent_root->trans));

	close(root->fp);
	if (root->node)
		tree_block_release(root, root->node);
	if (root->extent_root->node)
		tree_block_release(root->extent_root, root->extent_root->node);
	tree_block_release(root, root->commit_root);
	free(root);
	printf("on close %d blocks are allocated\n", allocated_blocks);
	return 0;
}

void tree_block_release(struct ctree_root *root, struct tree_buffer *buf)
{
	buf->count--;
	if (buf->count < 0)
		BUG();
	if (buf->count == 0) {
		BUG_ON(!list_empty(&buf->cache));
		BUG_ON(!list_empty(&buf->dirty));
		if (!radix_tree_lookup(&root->cache_radix, buf->blocknr))
			BUG();
		radix_tree_delete(&root->cache_radix, buf->blocknr);
		memset(buf, 0, sizeof(*buf));
		free(buf);
		BUG_ON(allocated_blocks == 0);
		allocated_blocks--;
		BUG_ON(root->cache_size == 0);
		root->cache_size--;
	}
}

