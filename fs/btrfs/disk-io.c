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

static int get_free_block(struct ctree_root *root, u64 *block)
{
	struct stat st;
	int ret;

	if (root->alloc_extent->num_used >= root->alloc_extent->num_blocks)
		return -1;

	*block = root->alloc_extent->blocknr + root->alloc_extent->num_used;
	root->alloc_extent->num_used += 1;
	if (root->alloc_extent->num_used >= root->alloc_extent->num_blocks) {
		struct alloc_extent *ae = root->alloc_extent;
		root->alloc_extent = root->reserve_extent;
		root->reserve_extent = ae;
		ae->num_blocks = 0;
	}
	st.st_size = 0;
	ret = fstat(root->fp, &st);
	if (st.st_size < (*block + 1) * CTREE_BLOCKSIZE)
		ret = ftruncate(root->fp,
				(*block + 1) * CTREE_BLOCKSIZE);
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
	loff_t offset = blocknr * CTREE_BLOCKSIZE;
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
	loff_t offset = blocknr * CTREE_BLOCKSIZE;
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

struct ctree_super_block {
	struct ctree_root_info root_info;
	struct ctree_root_info extent_info;
} __attribute__ ((__packed__));

static int __setup_root(struct ctree_root *root, struct ctree_root *extent_root,
			struct ctree_root_info *info, int fp)
{
	root->fp = fp;
	root->node = read_tree_block(root, info->tree_root);
	root->extent_root = extent_root;
	memcpy(&root->ai1, &info->alloc_extent, sizeof(info->alloc_extent));
	memcpy(&root->ai2, &info->reserve_extent, sizeof(info->reserve_extent));
	root->alloc_extent = &root->ai1;
	root->reserve_extent = &root->ai2;
	INIT_RADIX_TREE(&root->cache_radix, GFP_KERNEL);
	printf("setup done reading root %p, used %lu\n", root, root->alloc_extent->num_used);
	return 0;
}

struct ctree_root *open_ctree(char *filename)
{
	struct ctree_root *root = malloc(sizeof(struct ctree_root));
	struct ctree_root *extent_root = malloc(sizeof(struct ctree_root));
	struct ctree_super_block super;
	int fp;
	int ret;

	fp = open(filename, O_CREAT | O_RDWR);
	if (fp < 0) {
		free(root);
		return NULL;
	}
	ret = pread(fp, &super, sizeof(struct ctree_super_block),
		     CTREE_SUPER_INFO_OFFSET(CTREE_BLOCKSIZE));
	if (ret == 0) {
		ret = mkfs(fp);
		if (ret)
			return NULL;
		ret = pread(fp, &super, sizeof(struct ctree_super_block),
			     CTREE_SUPER_INFO_OFFSET(CTREE_BLOCKSIZE));
		if (ret != sizeof(struct ctree_super_block))
			return NULL;
	}
	BUG_ON(ret < 0);
	__setup_root(root, extent_root, &super.root_info, fp);
	__setup_root(extent_root, extent_root, &super.extent_info, fp);
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
	return;
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

