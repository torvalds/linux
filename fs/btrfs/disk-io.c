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

struct ctree_header {
	u64 root_block;
} __attribute__ ((__packed__));

static int get_free_block(struct ctree_root *root, u64 *block)
{
	struct stat st;
	int ret;

	st.st_size = 0;
	ret = fstat(root->fp, &st);
	if (st.st_size > sizeof(struct ctree_header)) {
		*block = (st.st_size -
			sizeof(struct ctree_header)) / CTREE_BLOCKSIZE;
	} else {
		*block = 0;
	}
	ret = ftruncate(root->fp, sizeof(struct ctree_header) + (*block + 1) *
			CTREE_BLOCKSIZE);
	return ret;
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

struct tree_buffer *alloc_free_block(struct ctree_root *root)
{
	u64 free_block;
	int ret;
	struct tree_buffer * buf;
	ret = get_free_block(root, &free_block);
	if (ret) {
		BUG();
		return NULL;
	}
	buf = alloc_tree_block(root, free_block);
	if (!buf)
		BUG();
	return buf;
}

struct tree_buffer *read_tree_block(struct ctree_root *root, u64 blocknr)
{
	loff_t offset = blocknr * CTREE_BLOCKSIZE + sizeof(struct ctree_header);
	struct tree_buffer *buf;
	int ret;

	buf = radix_tree_lookup(&root->cache_radix, blocknr);
	if (buf) {
		buf->count++;
		if (buf->blocknr != blocknr)
			BUG();
		if (buf->blocknr != buf->node.header.blocknr)
			BUG();
		return buf;
	}
	buf = alloc_tree_block(root, blocknr);
	if (!buf)
		return NULL;
	ret = pread(root->fp, &buf->node, CTREE_BLOCKSIZE, offset);
	if (ret != CTREE_BLOCKSIZE) {
		free(buf);
		return NULL;
	}
	if (buf->blocknr != buf->node.header.blocknr)
		BUG();
	return buf;
}

int write_tree_block(struct ctree_root *root, struct tree_buffer *buf)
{
	u64 blocknr = buf->blocknr;
	loff_t offset = blocknr * CTREE_BLOCKSIZE + sizeof(struct ctree_header);
	int ret;

	if (buf->blocknr != buf->node.header.blocknr)
		BUG();
	ret = pwrite(root->fp, &buf->node, CTREE_BLOCKSIZE, offset);
	if (ret != CTREE_BLOCKSIZE)
		return ret;
	if (buf == root->node)
		return update_root_block(root);
	return 0;
}

struct ctree_root *open_ctree(char *filename)
{
	struct ctree_root *root = malloc(sizeof(struct ctree_root));
	int fp;
	u64 root_block;
	int ret;

	fp = open(filename, O_CREAT | O_RDWR);
	if (fp < 0) {
		free(root);
		return NULL;
	}
	root->fp = fp;
	INIT_RADIX_TREE(&root->cache_radix, GFP_KERNEL);
	ret = pread(fp, &root_block, sizeof(u64), 0);
	if (ret == sizeof(u64)) {
		printf("reading root node at block %lu\n", root_block);
		root->node = read_tree_block(root, root_block);
	} else
		root->node = NULL;
	return root;
}

int close_ctree(struct ctree_root *root)
{
	close(root->fp);
	if (root->node)
		tree_block_release(root, root->node);
	free(root);
	printf("on close %d blocks are allocated\n", allocated_blocks);
	return 0;
}

int update_root_block(struct ctree_root *root)
{
	int ret;
	u64 root_block = root->node->blocknr;

	ret = pwrite(root->fp, &root_block, sizeof(u64), 0);
	if (ret != sizeof(u64))
		return ret;
	return 0;
}

void tree_block_release(struct ctree_root *root, struct tree_buffer *buf)
{
	buf->count--;
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

