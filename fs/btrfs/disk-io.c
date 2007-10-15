/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/crc32c.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/radix-tree.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h> // for block_sync_page
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "print-tree.h"

#if 0
static int check_tree_block(struct btrfs_root *root, struct extent_buffer *buf)
{
	if (extent_buffer_blocknr(buf) != btrfs_header_blocknr(buf)) {
		printk(KERN_CRIT "buf blocknr(buf) is %llu, header is %llu\n",
		       (unsigned long long)extent_buffer_blocknr(buf),
		       (unsigned long long)btrfs_header_blocknr(buf));
		return 1;
	}
	return 0;
}
#endif

struct extent_buffer *btrfs_find_tree_block(struct btrfs_root *root,
					    u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;
	eb = find_extent_buffer(&BTRFS_I(btree_inode)->extent_tree,
				bytenr, blocksize, GFP_NOFS);
	if (eb)
		eb->alloc_addr = (unsigned long)__builtin_return_address(0);
	return eb;
}

struct extent_buffer *btrfs_find_create_tree_block(struct btrfs_root *root,
						 u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;

	eb = alloc_extent_buffer(&BTRFS_I(btree_inode)->extent_tree,
				 bytenr, blocksize, GFP_NOFS);
	eb->alloc_addr = (unsigned long)__builtin_return_address(0);
	return eb;
}

struct extent_map *btree_get_extent(struct inode *inode, struct page *page,
				    size_t page_offset, u64 start, u64 end,
				    int create)
{
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_map *em;
	int ret;

again:
	em = lookup_extent_mapping(em_tree, start, end);
	if (em) {
		goto out;
	}
	em = alloc_extent_map(GFP_NOFS);
	if (!em) {
		em = ERR_PTR(-ENOMEM);
		goto out;
	}
	em->start = 0;
	em->end = (i_size_read(inode) & ~((u64)PAGE_CACHE_SIZE -1)) - 1;
	em->block_start = 0;
	em->block_end = em->end;
	em->bdev = inode->i_sb->s_bdev;
	ret = add_extent_mapping(em_tree, em);
	if (ret == -EEXIST) {
		free_extent_map(em);
		em = NULL;
		goto again;
	} else if (ret) {
		em = ERR_PTR(ret);
	}
out:
	return em;
}

static int btree_writepage(struct page *page, struct writeback_control *wbc)
{
	struct extent_map_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->extent_tree;
	return extent_write_full_page(tree, page, btree_get_extent, wbc);
}
int btree_readpage(struct file *file, struct page *page)
{
	struct extent_map_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->extent_tree;
	return extent_read_full_page(tree, page, btree_get_extent);
}

static int btree_releasepage(struct page *page, gfp_t unused_gfp_flags)
{
	struct extent_map_tree *tree;
	int ret;

	BUG_ON(page->private != 1);
	tree = &BTRFS_I(page->mapping->host)->extent_tree;
	ret = try_release_extent_mapping(tree, page);
	if (ret == 1) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		page_cache_release(page);
	}
	return ret;
}

static void btree_invalidatepage(struct page *page, unsigned long offset)
{
	struct extent_map_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->extent_tree;
	extent_invalidatepage(tree, page, offset);
	btree_releasepage(page, GFP_NOFS);
}

int btrfs_csum_data(struct btrfs_root * root, char *data, size_t len,
		    char *result)
{
	return 0;
#if 0
	u32 crc;
	crc = crc32c(0, data, len);
	memcpy(result, &crc, BTRFS_CRC32_SIZE);
	return 0;
#endif
}

#if 0
static int csum_tree_block(struct btrfs_root *root, struct extent_buffer *buf,
			   int verify)
{
	return 0;
	char result[BTRFS_CRC32_SIZE];
	int ret;
	struct btrfs_node *node;

	ret = btrfs_csum_data(root, bh->b_data + BTRFS_CSUM_SIZE,
			      bh->b_size - BTRFS_CSUM_SIZE, result);
	if (ret)
		return ret;
	if (verify) {
		if (memcmp(bh->b_data, result, BTRFS_CRC32_SIZE)) {
			printk("btrfs: %s checksum verify failed on %llu\n",
			       root->fs_info->sb->s_id,
			       (unsigned long long)bh_blocknr(bh));
			return 1;
		}
	} else {
		node = btrfs_buffer_node(bh);
		memcpy(node->header.csum, result, BTRFS_CRC32_SIZE);
	}
	return 0;
}
#endif

#if 0
static int btree_writepage(struct page *page, struct writeback_control *wbc)
{
	struct buffer_head *bh;
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;
	struct buffer_head *head;
	if (!page_has_buffers(page)) {
		create_empty_buffers(page, root->fs_info->sb->s_blocksize,
					(1 << BH_Dirty)|(1 << BH_Uptodate));
	}
	head = page_buffers(page);
	bh = head;
	do {
		if (buffer_dirty(bh))
			csum_tree_block(root, bh, 0);
		bh = bh->b_this_page;
	} while (bh != head);
	return block_write_full_page(page, btree_get_block, wbc);
}
#endif

static struct address_space_operations btree_aops = {
	.readpage	= btree_readpage,
	.writepage	= btree_writepage,
	.releasepage	= btree_releasepage,
	.invalidatepage = btree_invalidatepage,
	.sync_page	= block_sync_page,
};

int readahead_tree_block(struct btrfs_root *root, u64 bytenr, u32 blocksize)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;
	int ret = 0;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return 0;
	read_extent_buffer_pages(&BTRFS_I(btree_inode)->extent_tree,
				 buf, 0);
	free_extent_buffer(buf);
	return ret;
}

struct extent_buffer *read_tree_block(struct btrfs_root *root, u64 bytenr,
				      u32 blocksize)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return NULL;
	read_extent_buffer_pages(&BTRFS_I(btree_inode)->extent_tree,
				 buf, 1);
	buf->alloc_addr = (unsigned long)__builtin_return_address(0);
	return buf;
}

int clean_tree_block(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		     struct extent_buffer *buf)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	clear_extent_buffer_dirty(&BTRFS_I(btree_inode)->extent_tree, buf);
	return 0;
}

int wait_on_tree_block_writeback(struct btrfs_root *root,
				 struct extent_buffer *buf)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	wait_on_extent_buffer_writeback(&BTRFS_I(btree_inode)->extent_tree,
					buf);
	return 0;
}

int set_tree_block_dirty(struct btrfs_root *root, struct extent_buffer *buf)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	set_extent_buffer_dirty(&BTRFS_I(btree_inode)->extent_tree, buf);
	return 0;
}

static int __setup_root(u32 nodesize, u32 leafsize, u32 sectorsize,
			struct btrfs_root *root,
			struct btrfs_fs_info *fs_info,
			u64 objectid)
{
	root->node = NULL;
	root->inode = NULL;
	root->commit_root = NULL;
	root->sectorsize = sectorsize;
	root->nodesize = nodesize;
	root->leafsize = leafsize;
	root->ref_cows = 0;
	root->fs_info = fs_info;
	root->objectid = objectid;
	root->last_trans = 0;
	root->highest_inode = 0;
	root->last_inode_alloc = 0;
	root->name = NULL;
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	memset(&root->defrag_progress, 0, sizeof(root->defrag_progress));
	memset(&root->root_kobj, 0, sizeof(root->root_kobj));
	init_completion(&root->kobj_unregister);
	init_rwsem(&root->snap_sem);
	root->defrag_running = 0;
	root->defrag_level = 0;
	root->root_key.objectid = objectid;
	return 0;
}

static int find_and_setup_root(struct btrfs_root *tree_root,
			       struct btrfs_fs_info *fs_info,
			       u64 objectid,
			       struct btrfs_root *root)
{
	int ret;
	u32 blocksize;

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, root, fs_info, objectid);
	ret = btrfs_find_last_root(tree_root, objectid,
				   &root->root_item, &root->root_key);
	BUG_ON(ret);

	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize);
	BUG_ON(!root->node);
	return 0;
}

struct btrfs_root *btrfs_read_fs_root_no_radix(struct btrfs_fs_info *fs_info,
					       struct btrfs_key *location)
{
	struct btrfs_root *root;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_path *path;
	struct extent_buffer *l;
	u64 highest_inode;
	u32 blocksize;
	int ret = 0;

	root = kzalloc(sizeof(*root), GFP_NOFS);
	if (!root)
		return ERR_PTR(-ENOMEM);
	if (location->offset == (u64)-1) {
		ret = find_and_setup_root(tree_root, fs_info,
					  location->objectid, root);
		if (ret) {
			kfree(root);
			return ERR_PTR(ret);
		}
		goto insert;
	}

	__setup_root(tree_root->nodesize, tree_root->leafsize,
		     tree_root->sectorsize, root, fs_info,
		     location->objectid);

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(NULL, tree_root, location, path, 0, 0);
	if (ret != 0) {
		if (ret > 0)
			ret = -ENOENT;
		goto out;
	}
	l = path->nodes[0];
	read_extent_buffer(l, &root->root_item,
	       btrfs_item_ptr_offset(l, path->slots[0]),
	       sizeof(root->root_item));
	ret = 0;
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	if (ret) {
		kfree(root);
		return ERR_PTR(ret);
	}
	blocksize = btrfs_level_size(root, btrfs_root_level(&root->root_item));
	root->node = read_tree_block(root, btrfs_root_bytenr(&root->root_item),
				     blocksize);
	BUG_ON(!root->node);
insert:
	root->ref_cows = 1;
	ret = btrfs_find_highest_inode(root, &highest_inode);
	if (ret == 0) {
		root->highest_inode = highest_inode;
		root->last_inode_alloc = highest_inode;
	}
	return root;
}

struct btrfs_root *btrfs_read_fs_root(struct btrfs_fs_info *fs_info,
				      struct btrfs_key *location,
				      const char *name, int namelen)
{
	struct btrfs_root *root;
	int ret;

	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)location->objectid);
	if (root)
		return root;

	root = btrfs_read_fs_root_no_radix(fs_info, location);
	if (IS_ERR(root))
		return root;
	ret = radix_tree_insert(&fs_info->fs_roots_radix,
				(unsigned long)root->root_key.objectid,
				root);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root);
		return ERR_PTR(ret);
	}

	ret = btrfs_set_root_name(root, name, namelen);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root);
		return ERR_PTR(ret);
	}

	ret = btrfs_sysfs_add_root(root);
	if (ret) {
		free_extent_buffer(root->node);
		kfree(root->name);
		kfree(root);
		return ERR_PTR(ret);
	}

	ret = btrfs_find_dead_roots(fs_info->tree_root,
				    root->root_key.objectid, root);
	BUG_ON(ret);

	return root;
}

struct btrfs_root *open_ctree(struct super_block *sb)
{
	u32 sectorsize;
	u32 nodesize;
	u32 leafsize;
	u32 blocksize;
	struct btrfs_root *extent_root = kmalloc(sizeof(struct btrfs_root),
						 GFP_NOFS);
	struct btrfs_root *tree_root = kmalloc(sizeof(struct btrfs_root),
					       GFP_NOFS);
	struct btrfs_fs_info *fs_info = kmalloc(sizeof(*fs_info),
						GFP_NOFS);
	int ret;
	int err = -EIO;
	struct btrfs_super_block *disk_super;

	if (!extent_root || !tree_root || !fs_info) {
		err = -ENOMEM;
		goto fail;
	}
	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_NOFS);
	INIT_LIST_HEAD(&fs_info->trans_list);
	INIT_LIST_HEAD(&fs_info->dead_roots);
	memset(&fs_info->super_kobj, 0, sizeof(fs_info->super_kobj));
	init_completion(&fs_info->kobj_unregister);
	sb_set_blocksize(sb, 4096);
	fs_info->running_transaction = NULL;
	fs_info->last_trans_committed = 0;
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->sb = sb;
	fs_info->btree_inode = new_inode(sb);
	fs_info->btree_inode->i_ino = 1;
	fs_info->btree_inode->i_nlink = 1;
	fs_info->btree_inode->i_size = sb->s_bdev->bd_inode->i_size;
	fs_info->btree_inode->i_mapping->a_ops = &btree_aops;
	extent_map_tree_init(&BTRFS_I(fs_info->btree_inode)->extent_tree,
			     fs_info->btree_inode->i_mapping,
			     GFP_NOFS);
	extent_map_tree_init(&fs_info->free_space_cache,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_map_tree_init(&fs_info->block_group_cache,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_map_tree_init(&fs_info->pinned_extents,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_map_tree_init(&fs_info->pending_del,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	extent_map_tree_init(&fs_info->extent_ins,
			     fs_info->btree_inode->i_mapping, GFP_NOFS);
	fs_info->do_barriers = 1;
	fs_info->closing = 0;

	INIT_DELAYED_WORK(&fs_info->trans_work, btrfs_transaction_cleaner);
	BTRFS_I(fs_info->btree_inode)->root = tree_root;
	memset(&BTRFS_I(fs_info->btree_inode)->location, 0,
	       sizeof(struct btrfs_key));
	insert_inode_hash(fs_info->btree_inode);
	mapping_set_gfp_mask(fs_info->btree_inode->i_mapping, GFP_NOFS);

	mutex_init(&fs_info->trans_mutex);
	mutex_init(&fs_info->fs_mutex);

	__setup_root(512, 512, 512, tree_root,
		     fs_info, BTRFS_ROOT_TREE_OBJECTID);

	fs_info->sb_buffer = read_tree_block(tree_root,
					     BTRFS_SUPER_INFO_OFFSET,
					     512);

	if (!fs_info->sb_buffer)
		goto fail_iput;

	read_extent_buffer(fs_info->sb_buffer, &fs_info->super_copy, 0,
			   sizeof(fs_info->super_copy));

	read_extent_buffer(fs_info->sb_buffer, fs_info->fsid,
			   (unsigned long)btrfs_super_fsid(fs_info->sb_buffer),
			   BTRFS_FSID_SIZE);
	disk_super = &fs_info->super_copy;
	if (!btrfs_super_root(disk_super))
		goto fail_sb_buffer;

	nodesize = btrfs_super_nodesize(disk_super);
	leafsize = btrfs_super_leafsize(disk_super);
	sectorsize = btrfs_super_sectorsize(disk_super);
	tree_root->nodesize = nodesize;
	tree_root->leafsize = leafsize;
	tree_root->sectorsize = sectorsize;

	i_size_write(fs_info->btree_inode,
		     btrfs_super_total_bytes(disk_super));


	if (strncmp((char *)(&disk_super->magic), BTRFS_MAGIC,
		    sizeof(disk_super->magic))) {
		printk("btrfs: valid FS not found on %s\n", sb->s_id);
		goto fail_sb_buffer;
	}
	blocksize = btrfs_level_size(tree_root,
				     btrfs_super_root_level(disk_super));
	tree_root->node = read_tree_block(tree_root,
					  btrfs_super_root(disk_super),
					  blocksize);
	if (!tree_root->node)
		goto fail_sb_buffer;

#if 0
	btrfs_print_leaf(tree_root, tree_root->node);
	err = -EIO;
	goto fail_tree_root;
#endif
	mutex_lock(&fs_info->fs_mutex);

	ret = find_and_setup_root(tree_root, fs_info,
				  BTRFS_EXTENT_TREE_OBJECTID, extent_root);
	if (ret) {
		mutex_unlock(&fs_info->fs_mutex);
		goto fail_tree_root;
	}

	btrfs_read_block_groups(extent_root);

	fs_info->generation = btrfs_super_generation(disk_super) + 1;
	mutex_unlock(&fs_info->fs_mutex);
	return tree_root;

fail_tree_root:
	free_extent_buffer(tree_root->node);
fail_sb_buffer:
	free_extent_buffer(fs_info->sb_buffer);
fail_iput:
	iput(fs_info->btree_inode);
fail:
	kfree(extent_root);
	kfree(tree_root);
	kfree(fs_info);
	return ERR_PTR(err);
}

int write_ctree_super(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root)
{
	int ret;
	struct extent_buffer *super = root->fs_info->sb_buffer;
	struct inode *btree_inode = root->fs_info->btree_inode;

	set_extent_buffer_dirty(&BTRFS_I(btree_inode)->extent_tree, super);
	ret = sync_page_range_nolock(btree_inode, btree_inode->i_mapping,
				     super->start, super->len);
	return ret;
}

int btrfs_free_fs_root(struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	radix_tree_delete(&fs_info->fs_roots_radix,
			  (unsigned long)root->root_key.objectid);
	btrfs_sysfs_del_root(root);
	if (root->inode)
		iput(root->inode);
	if (root->node)
		free_extent_buffer(root->node);
	if (root->commit_root)
		free_extent_buffer(root->commit_root);
	if (root->name)
		kfree(root->name);
	kfree(root);
	return 0;
}

static int del_fs_roots(struct btrfs_fs_info *fs_info)
{
	int ret;
	struct btrfs_root *gang[8];
	int i;

	while(1) {
		ret = radix_tree_gang_lookup(&fs_info->fs_roots_radix,
					     (void **)gang, 0,
					     ARRAY_SIZE(gang));
		if (!ret)
			break;
		for (i = 0; i < ret; i++)
			btrfs_free_fs_root(fs_info, gang[i]);
	}
	return 0;
}

int close_ctree(struct btrfs_root *root)
{
	int ret;
	struct btrfs_trans_handle *trans;
	struct btrfs_fs_info *fs_info = root->fs_info;

	fs_info->closing = 1;
	btrfs_transaction_flush_work(root);
	mutex_lock(&fs_info->fs_mutex);
	btrfs_defrag_dirty_roots(root->fs_info);
	trans = btrfs_start_transaction(root, 1);
	ret = btrfs_commit_transaction(trans, root);
	/* run commit again to  drop the original snapshot */
	trans = btrfs_start_transaction(root, 1);
	btrfs_commit_transaction(trans, root);
	ret = btrfs_write_and_wait_transaction(NULL, root);
	BUG_ON(ret);
	write_ctree_super(NULL, root);
	mutex_unlock(&fs_info->fs_mutex);

	if (fs_info->extent_root->node)
		free_extent_buffer(fs_info->extent_root->node);

	if (fs_info->tree_root->node)
		free_extent_buffer(fs_info->tree_root->node);

	free_extent_buffer(fs_info->sb_buffer);

	btrfs_free_block_groups(root->fs_info);
	del_fs_roots(fs_info);
	truncate_inode_pages(fs_info->btree_inode->i_mapping, 0);
	iput(fs_info->btree_inode);
	kfree(fs_info->extent_root);
	kfree(fs_info->tree_root);
	return 0;
}

int btrfs_buffer_uptodate(struct extent_buffer *buf)
{
	struct inode *btree_inode = buf->pages[0]->mapping->host;
	return extent_buffer_uptodate(&BTRFS_I(btree_inode)->extent_tree, buf);
}

int btrfs_set_buffer_uptodate(struct extent_buffer *buf)
{
	struct inode *btree_inode = buf->pages[0]->mapping->host;
	return set_extent_buffer_uptodate(&BTRFS_I(btree_inode)->extent_tree,
					  buf);
}

void btrfs_mark_buffer_dirty(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	u64 transid = btrfs_header_generation(buf);
	struct inode *btree_inode = root->fs_info->btree_inode;

	if (transid != root->fs_info->generation) {
		printk(KERN_CRIT "transid mismatch buffer %llu, found %Lu running %Lu\n",
			(unsigned long long)buf->start,
			transid, root->fs_info->generation);
		WARN_ON(1);
	}
	set_extent_buffer_dirty(&BTRFS_I(btree_inode)->extent_tree, buf);
}

void btrfs_btree_balance_dirty(struct btrfs_root *root, unsigned long nr)
{
	balance_dirty_pages_ratelimited_nr(
			root->fs_info->btree_inode->i_mapping, nr);
}

void btrfs_set_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	set_extent_bits(&BTRFS_I(btree_inode)->extent_tree, buf->start,
			buf->start + buf->len - 1, EXTENT_DEFRAG, GFP_NOFS);
}

void btrfs_set_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	set_extent_bits(&BTRFS_I(btree_inode)->extent_tree, buf->start,
			buf->start + buf->len - 1, EXTENT_DEFRAG_DONE,
			GFP_NOFS);
}

int btrfs_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return test_range_bit(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1, EXTENT_DEFRAG, 0);
}

int btrfs_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return test_range_bit(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG_DONE, 0);
}

int btrfs_clear_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return clear_extent_bits(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG_DONE, GFP_NOFS);
}

int btrfs_clear_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return clear_extent_bits(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG, GFP_NOFS);
}

int btrfs_read_buffer(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->pages[0]->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return read_extent_buffer_pages(&BTRFS_I(btree_inode)->extent_tree,
					buf, 1);
}
