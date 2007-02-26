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

static int check_tree_block(struct ctree_root *root, struct tree_buffer *buf)
{
	if (buf->blocknr != buf->node.header.blocknr)
		BUG();
	if (root->node && buf->node.header.parentid != root->node->node.header.parentid)
		BUG();
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
	buf->count = 1;
	radix_tree_preload(GFP_KERNEL);
	ret = radix_tree_insert(&root->cache_radix, blocknr, buf);
	radix_tree_preload_end();
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

int write_tree_block(struct ctree_root *root, struct tree_buffer *buf)
{
	u64 blocknr = buf->blocknr;
	loff_t offset = blocknr * CTREE_BLOCKSIZE;
	int ret;

	if (buf->blocknr != buf->node.header.blocknr)
		BUG();
	ret = pwrite(root->fp, &buf->node, CTREE_BLOCKSIZE, offset);
	if (ret != CTREE_BLOCKSIZE)
		return ret;
	return 0;
}

static int __setup_root(struct ctree_root *root, struct ctree_root *extent_root,
			struct ctree_root_info *info, int fp)
{
	root->fp = fp;
	root->node = NULL;
	root->node = read_tree_block(root, info->tree_root);
	root->extent_root = extent_root;
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

int close_ctree(struct ctree_root *root)
{
	close(root->fp);
	if (root->node)
		tree_block_release(root, root->node);
	if (root->extent_root->node)
		tree_block_release(root->extent_root, root->extent_root->node);
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
		if (!radix_tree_lookup(&root->cache_radix, buf->blocknr))
			BUG();
		radix_tree_delete(&root->cache_radix, buf->blocknr);
		memset(buf, 0, sizeof(*buf));
		free(buf);
		BUG_ON(allocated_blocks == 0);
		allocated_blocks--;
	}
}

