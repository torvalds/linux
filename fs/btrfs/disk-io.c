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
#include "transaction.h"

static int allocated_blocks = 0;
int cache_max = 10000;

static int check_tree_block(struct btrfs_root *root, struct btrfs_buffer *buf)
{
	if (buf->blocknr != btrfs_header_blocknr(&buf->node.header))
		BUG();
	if (root->node && btrfs_header_parentid(&buf->node.header) !=
	    btrfs_header_parentid(&root->node->node.header))
		BUG();
	return 0;
}

static int free_some_buffers(struct btrfs_root *root)
{
	struct list_head *node, *next;
	struct btrfs_buffer *b;
	if (root->fs_info->cache_size < cache_max)
		return 0;
	list_for_each_safe(node, next, &root->fs_info->cache) {
		b = list_entry(node, struct btrfs_buffer, cache);
		if (b->count == 1) {
			BUG_ON(!list_empty(&b->dirty));
			list_del_init(&b->cache);
			btrfs_block_release(root, b);
			if (root->fs_info->cache_size < cache_max)
				break;
		}
	}
	return 0;
}

struct btrfs_buffer *alloc_tree_block(struct btrfs_root *root, u64 blocknr)
{
	struct btrfs_buffer *buf;
	int ret;

	buf = malloc(sizeof(struct btrfs_buffer) + root->blocksize);
	if (!buf)
		return buf;
	allocated_blocks++;
	buf->blocknr = blocknr;
	buf->count = 2;
	INIT_LIST_HEAD(&buf->dirty);
	free_some_buffers(root);
	radix_tree_preload(GFP_KERNEL);
	ret = radix_tree_insert(&root->fs_info->cache_radix, blocknr, buf);
	radix_tree_preload_end();
	list_add_tail(&buf->cache, &root->fs_info->cache);
	root->fs_info->cache_size++;
	if (ret) {
		free(buf);
		return NULL;
	}
	return buf;
}

struct btrfs_buffer *find_tree_block(struct btrfs_root *root, u64 blocknr)
{
	struct btrfs_buffer *buf;
	buf = radix_tree_lookup(&root->fs_info->cache_radix, blocknr);
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

struct btrfs_buffer *read_tree_block(struct btrfs_root *root, u64 blocknr)
{
	loff_t offset = blocknr * root->blocksize;
	struct btrfs_buffer *buf;
	int ret;

	buf = radix_tree_lookup(&root->fs_info->cache_radix, blocknr);
	if (buf) {
		buf->count++;
	} else {
		buf = alloc_tree_block(root, blocknr);
		if (!buf)
			return NULL;
		ret = pread(root->fs_info->fp, &buf->node, root->blocksize,
			    offset);
		if (ret != root->blocksize) {
			free(buf);
			return NULL;
		}
	}
	if (check_tree_block(root, buf))
		BUG();
	return buf;
}

int dirty_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct btrfs_buffer *buf)
{
	if (!list_empty(&buf->dirty))
		return 0;
	list_add_tail(&buf->dirty, &root->fs_info->trans);
	buf->count++;
	return 0;
}

int clean_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct btrfs_buffer *buf)
{
	if (!list_empty(&buf->dirty)) {
		list_del_init(&buf->dirty);
		btrfs_block_release(root, buf);
	}
	return 0;
}

int write_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct btrfs_buffer *buf)
{
	u64 blocknr = buf->blocknr;
	loff_t offset = blocknr * root->blocksize;
	int ret;

	if (buf->blocknr != btrfs_header_blocknr(&buf->node.header))
		BUG();
	ret = pwrite(root->fs_info->fp, &buf->node, root->blocksize, offset);
	if (ret != root->blocksize)
		return ret;
	return 0;
}

static int __commit_transaction(struct btrfs_trans_handle *trans, struct
				btrfs_root *root)
{
	struct btrfs_buffer *b;
	int ret = 0;
	int wret;
	while(!list_empty(&root->fs_info->trans)) {
		b = list_entry(root->fs_info->trans.next, struct btrfs_buffer,
			       dirty);
		list_del_init(&b->dirty);
		wret = write_tree_block(trans, root, b);
		if (wret)
			ret = wret;
		btrfs_block_release(root, b);
	}
	return ret;
}

static int commit_tree_roots(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info)
{
	int ret;
	u64 old_extent_block;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_root *inode_root = fs_info->inode_root;

	btrfs_set_root_blocknr(&inode_root->root_item,
			       inode_root->node->blocknr);
	ret = btrfs_update_root(trans, tree_root,
				&inode_root->root_key,
				&inode_root->root_item);
	BUG_ON(ret);
	while(1) {
		old_extent_block = btrfs_root_blocknr(&extent_root->root_item);
		if (old_extent_block == extent_root->node->blocknr)
			break;
		btrfs_set_root_blocknr(&extent_root->root_item,
				       extent_root->node->blocknr);
		ret = btrfs_update_root(trans, tree_root,
					&extent_root->root_key,
					&extent_root->root_item);
		BUG_ON(ret);
	}
	return 0;
}

int btrfs_commit_transaction(struct btrfs_trans_handle *trans, struct
			     btrfs_root *root, struct btrfs_super_block *s)
{
	int ret = 0;
	struct btrfs_buffer *snap = root->commit_root;
	struct btrfs_key snap_key;

	if (root->commit_root == root->node)
		return 0;

	memcpy(&snap_key, &root->root_key, sizeof(snap_key));
	root->root_key.offset++;

	btrfs_set_root_blocknr(&root->root_item, root->node->blocknr);
	ret = btrfs_insert_root(trans, root->fs_info->tree_root,
				&root->root_key, &root->root_item);
	BUG_ON(ret);

	ret = commit_tree_roots(trans, root->fs_info);
	BUG_ON(ret);

	ret = __commit_transaction(trans, root);
	BUG_ON(ret);

	write_ctree_super(trans, root, s);
	btrfs_finish_extent_commit(trans, root->fs_info->extent_root);
	btrfs_finish_extent_commit(trans, root->fs_info->tree_root);

	root->commit_root = root->node;
	root->node->count++;
	ret = btrfs_drop_snapshot(trans, root, snap);
	BUG_ON(ret);

	ret = btrfs_del_root(trans, root->fs_info->tree_root, &snap_key);
	BUG_ON(ret);

	return ret;
}

static int __setup_root(struct btrfs_super_block *super,
			struct btrfs_root *root,
			struct btrfs_fs_info *fs_info,
			u64 objectid, int fp)
{
	root->node = NULL;
	root->commit_root = NULL;
	root->blocksize = btrfs_super_blocksize(super);
	root->ref_cows = 0;
	root->fs_info = fs_info;
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	return 0;
}

static int find_and_setup_root(struct btrfs_super_block *super,
			       struct btrfs_root *tree_root,
			       struct btrfs_fs_info *fs_info,
			       u64 objectid,
			       struct btrfs_root *root, int fp)
{
	int ret;

	__setup_root(super, root, fs_info, objectid, fp);
	ret = btrfs_find_last_root(tree_root, objectid,
				   &root->root_item, &root->root_key);
	BUG_ON(ret);

	root->node = read_tree_block(root,
				     btrfs_root_blocknr(&root->root_item));
	BUG_ON(!root->node);
	return 0;
}

struct btrfs_root *open_ctree(char *filename, struct btrfs_super_block *super)
{
	struct btrfs_root *root = malloc(sizeof(struct btrfs_root));
	struct btrfs_root *extent_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_root *tree_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_root *inode_root = malloc(sizeof(struct btrfs_root));
	struct btrfs_fs_info *fs_info = malloc(sizeof(*fs_info));
	int fp;
	int ret;

	fp = open(filename, O_CREAT | O_RDWR, 0600);
	if (fp < 0) {
		free(root);
		return NULL;
	}
	INIT_RADIX_TREE(&fs_info->cache_radix, GFP_KERNEL);
	INIT_RADIX_TREE(&fs_info->pinned_radix, GFP_KERNEL);
	INIT_LIST_HEAD(&fs_info->trans);
	INIT_LIST_HEAD(&fs_info->cache);
	fs_info->cache_size = 0;
	fs_info->fp = fp;
	fs_info->running_transaction = NULL;
	fs_info->fs_root = root;
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->inode_root = inode_root;
	fs_info->last_inode_alloc = 0;
	fs_info->last_inode_alloc_dirid = 0;
	memset(&fs_info->current_insert, 0, sizeof(fs_info->current_insert));
	memset(&fs_info->last_insert, 0, sizeof(fs_info->last_insert));

	ret = pread(fp, super, sizeof(struct btrfs_super_block),
		     BTRFS_SUPER_INFO_OFFSET);
	if (ret == 0 || btrfs_super_root(super) == 0) {
		printf("making new FS!\n");
		ret = mkfs(fp, 0, 1024);
		if (ret)
			return NULL;
		ret = pread(fp, super, sizeof(struct btrfs_super_block),
			     BTRFS_SUPER_INFO_OFFSET);
		if (ret != sizeof(struct btrfs_super_block))
			return NULL;
	}
	BUG_ON(ret < 0);

	__setup_root(super, tree_root, fs_info, BTRFS_ROOT_TREE_OBJECTID, fp);
	tree_root->node = read_tree_block(tree_root, btrfs_super_root(super));
	BUG_ON(!tree_root->node);

	ret = find_and_setup_root(super, tree_root, fs_info,
				  BTRFS_EXTENT_TREE_OBJECTID, extent_root, fp);
	BUG_ON(ret);

	ret = find_and_setup_root(super, tree_root, fs_info,
				  BTRFS_INODE_MAP_OBJECTID, inode_root, fp);
	BUG_ON(ret);

	ret = find_and_setup_root(super, tree_root, fs_info,
				  BTRFS_FS_TREE_OBJECTID, root, fp);
	BUG_ON(ret);

	root->commit_root = root->node;
	root->node->count++;
	root->ref_cows = 1;
	return root;
}

int write_ctree_super(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_super_block *s)
{
	int ret;
	btrfs_set_super_root(s, root->fs_info->tree_root->node->blocknr);
	ret = pwrite(root->fs_info->fp, s, sizeof(*s),
		     BTRFS_SUPER_INFO_OFFSET);
	if (ret != sizeof(*s)) {
		fprintf(stderr, "failed to write new super block err %d\n", ret);
		return ret;
	}
	return 0;
}

static int drop_cache(struct btrfs_root *root)
{
	while(!list_empty(&root->fs_info->cache)) {
		struct btrfs_buffer *b = list_entry(root->fs_info->cache.next,
						    struct btrfs_buffer,
						    cache);
		list_del_init(&b->cache);
		btrfs_block_release(root, b);
	}
	return 0;
}
int close_ctree(struct btrfs_root *root, struct btrfs_super_block *s)
{
	int ret;
	struct btrfs_trans_handle *trans;

	trans = root->fs_info->running_transaction;
	btrfs_commit_transaction(trans, root, s);
	ret = commit_tree_roots(trans, root->fs_info);
	BUG_ON(ret);
	ret = __commit_transaction(trans, root);
	BUG_ON(ret);
	write_ctree_super(trans, root, s);
	drop_cache(root);
	BUG_ON(!list_empty(&root->fs_info->trans));

	close(root->fs_info->fp);
	if (root->node)
		btrfs_block_release(root, root->node);
	if (root->fs_info->extent_root->node)
		btrfs_block_release(root->fs_info->extent_root,
				    root->fs_info->extent_root->node);
	if (root->fs_info->inode_root->node)
		btrfs_block_release(root->fs_info->inode_root,
				    root->fs_info->inode_root->node);
	if (root->fs_info->tree_root->node)
		btrfs_block_release(root->fs_info->tree_root,
				    root->fs_info->tree_root->node);
	btrfs_block_release(root, root->commit_root);
	free(root);
	printf("on close %d blocks are allocated\n", allocated_blocks);
	return 0;
}

void btrfs_block_release(struct btrfs_root *root, struct btrfs_buffer *buf)
{
	buf->count--;
	if (buf->count < 0)
		BUG();
	if (buf->count == 0) {
		BUG_ON(!list_empty(&buf->cache));
		BUG_ON(!list_empty(&buf->dirty));
		if (!radix_tree_lookup(&root->fs_info->cache_radix,
				       buf->blocknr))
			BUG();
		radix_tree_delete(&root->fs_info->cache_radix, buf->blocknr);
		memset(buf, 0, sizeof(*buf));
		free(buf);
		BUG_ON(allocated_blocks == 0);
		allocated_blocks--;
		BUG_ON(root->fs_info->cache_size == 0);
		root->fs_info->cache_size--;
	}
}

