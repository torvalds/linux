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

static struct extent_map_ops btree_extent_map_ops;

struct extent_buffer *btrfs_find_tree_block(struct btrfs_root *root,
					    u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;
	eb = find_extent_buffer(&BTRFS_I(btree_inode)->extent_tree,
				bytenr, blocksize, GFP_NOFS);
	return eb;
}

struct extent_buffer *btrfs_find_create_tree_block(struct btrfs_root *root,
						 u64 bytenr, u32 blocksize)
{
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_buffer *eb;

	eb = alloc_extent_buffer(&BTRFS_I(btree_inode)->extent_tree,
				 bytenr, blocksize, NULL, GFP_NOFS);
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

u32 btrfs_csum_data(struct btrfs_root *root, char *data, u32 seed, size_t len)
{
	return crc32c(seed, data, len);
}

void btrfs_csum_final(u32 crc, char *result)
{
	*(__le32 *)result = ~cpu_to_le32(crc);
}

static int csum_tree_block(struct btrfs_root *root, struct extent_buffer *buf,
			   int verify)
{
	char result[BTRFS_CRC32_SIZE];
	unsigned long len;
	unsigned long cur_len;
	unsigned long offset = BTRFS_CSUM_SIZE;
	char *map_token = NULL;
	char *kaddr;
	unsigned long map_start;
	unsigned long map_len;
	int err;
	u32 crc = ~(u32)0;

	len = buf->len - offset;
	while(len > 0) {
		err = map_private_extent_buffer(buf, offset, 32,
					&map_token, &kaddr,
					&map_start, &map_len, KM_USER0);
		if (err) {
			printk("failed to map extent buffer! %lu\n",
			       offset);
			return 1;
		}
		cur_len = min(len, map_len - (offset - map_start));
		crc = btrfs_csum_data(root, kaddr + offset - map_start,
				      crc, cur_len);
		len -= cur_len;
		offset += cur_len;
		unmap_extent_buffer(buf, map_token, KM_USER0);
	}
	btrfs_csum_final(crc, result);

	if (verify) {
		int from_this_trans = 0;

		if (root->fs_info->running_transaction &&
		    btrfs_header_generation(buf) ==
		    root->fs_info->running_transaction->transid)
			from_this_trans = 1;

		/* FIXME, this is not good */
		if (from_this_trans == 0 &&
		    memcmp_extent_buffer(buf, result, 0, BTRFS_CRC32_SIZE)) {
			u32 val;
			u32 found = 0;
			memcpy(&found, result, BTRFS_CRC32_SIZE);

			read_extent_buffer(buf, &val, 0, BTRFS_CRC32_SIZE);
			printk("btrfs: %s checksum verify failed on %llu "
			       "wanted %X found %X from_this_trans %d\n",
			       root->fs_info->sb->s_id,
			       buf->start, val, found, from_this_trans);
			return 1;
		}
	} else {
		write_extent_buffer(buf, result, 0, BTRFS_CRC32_SIZE);
	}
	return 0;
}


int csum_dirty_buffer(struct btrfs_root *root, struct page *page)
{
	struct extent_map_tree *tree;
	u64 start = (u64)page->index << PAGE_CACHE_SHIFT;
	u64 found_start;
	int found_level;
	unsigned long len;
	struct extent_buffer *eb;
	tree = &BTRFS_I(page->mapping->host)->extent_tree;

	if (page->private == EXTENT_PAGE_PRIVATE)
		goto out;
	if (!page->private)
		goto out;
	len = page->private >> 2;
	if (len == 0) {
		WARN_ON(1);
	}
	eb = alloc_extent_buffer(tree, start, len, page, GFP_NOFS);
	read_extent_buffer_pages(tree, eb, start + PAGE_CACHE_SIZE, 1);
	found_start = btrfs_header_bytenr(eb);
	if (found_start != start) {
		printk("warning: eb start incorrect %Lu buffer %Lu len %lu\n",
		       start, found_start, len);
	}
	found_level = btrfs_header_level(eb);
	csum_tree_block(root, eb, 0);
	free_extent_buffer(eb);
out:
	return 0;
}

static int btree_writepage_io_hook(struct page *page, u64 start, u64 end)
{
	struct btrfs_root *root = BTRFS_I(page->mapping->host)->root;

	csum_dirty_buffer(root, page);
	return 0;
}

static int btree_writepage(struct page *page, struct writeback_control *wbc)
{
	struct extent_map_tree *tree;
	tree = &BTRFS_I(page->mapping->host)->extent_tree;
	return extent_write_full_page(tree, page, btree_get_extent, wbc);
}

static int btree_writepages(struct address_space *mapping,
			    struct writeback_control *wbc)
{
	struct extent_map_tree *tree;
	tree = &BTRFS_I(mapping->host)->extent_tree;
	if (wbc->sync_mode == WB_SYNC_NONE) {
		u64 num_dirty;
		u64 start = 0;
		unsigned long thresh = 96 * 1024 * 1024;

		if (wbc->for_kupdate)
			return 0;

		if (current_is_pdflush()) {
			thresh = 96 * 1024 * 1024;
		} else {
			thresh = 8 * 1024 * 1024;
		}
		num_dirty = count_range_bits(tree, &start, (u64)-1,
					     thresh, EXTENT_DIRTY);
		if (num_dirty < thresh) {
			return 0;
		}
	}
	return extent_writepages(tree, mapping, btree_get_extent, wbc);
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
	.writepages	= btree_writepages,
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
				 buf, 0, 0);
	free_extent_buffer(buf);
	return ret;
}

struct extent_buffer *read_tree_block(struct btrfs_root *root, u64 bytenr,
				      u32 blocksize)
{
	struct extent_buffer *buf = NULL;
	struct inode *btree_inode = root->fs_info->btree_inode;
	struct extent_map_tree *extent_tree;
	u64 end;
	int ret;

	extent_tree = &BTRFS_I(btree_inode)->extent_tree;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return NULL;
	read_extent_buffer_pages(&BTRFS_I(btree_inode)->extent_tree,
				 buf, 0, 1);

	if (buf->flags & EXTENT_CSUM)
		return buf;

	end = buf->start + PAGE_CACHE_SIZE - 1;
	if (test_range_bit(extent_tree, buf->start, end, EXTENT_CSUM, 1)) {
		buf->flags |= EXTENT_CSUM;
		return buf;
	}

	lock_extent(extent_tree, buf->start, end, GFP_NOFS);

	if (test_range_bit(extent_tree, buf->start, end, EXTENT_CSUM, 1)) {
		buf->flags |= EXTENT_CSUM;
		goto out_unlock;
	}

	ret = csum_tree_block(root, buf, 1);
	set_extent_bits(extent_tree, buf->start, end, EXTENT_CSUM, GFP_NOFS);
	buf->flags |= EXTENT_CSUM;

out_unlock:
	unlock_extent(extent_tree, buf->start, end, GFP_NOFS);
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

static int __setup_root(u32 nodesize, u32 leafsize, u32 sectorsize,
			u32 stripesize, struct btrfs_root *root,
			struct btrfs_fs_info *fs_info,
			u64 objectid)
{
	root->node = NULL;
	root->inode = NULL;
	root->commit_root = NULL;
	root->sectorsize = sectorsize;
	root->nodesize = nodesize;
	root->leafsize = leafsize;
	root->stripesize = stripesize;
	root->ref_cows = 0;
	root->fs_info = fs_info;
	root->objectid = objectid;
	root->last_trans = 0;
	root->highest_inode = 0;
	root->last_inode_alloc = 0;
	root->name = NULL;
	root->in_sysfs = 0;
	memset(&root->root_key, 0, sizeof(root->root_key));
	memset(&root->root_item, 0, sizeof(root->root_item));
	memset(&root->defrag_progress, 0, sizeof(root->defrag_progress));
	memset(&root->root_kobj, 0, sizeof(root->root_kobj));
	init_completion(&root->kobj_unregister);
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
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, objectid);
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
		     tree_root->sectorsize, tree_root->stripesize,
		     root, fs_info, location->objectid);

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
	memcpy(&root->root_key, location, sizeof(*location));
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

struct btrfs_root *btrfs_lookup_fs_root(struct btrfs_fs_info *fs_info,
					u64 root_objectid)
{
	struct btrfs_root *root;

	if (root_objectid == BTRFS_ROOT_TREE_OBJECTID)
		return fs_info->tree_root;
	if (root_objectid == BTRFS_EXTENT_TREE_OBJECTID)
		return fs_info->extent_root;

	root = radix_tree_lookup(&fs_info->fs_roots_radix,
				 (unsigned long)root_objectid);
	return root;
}

struct btrfs_root *btrfs_read_fs_root_no_name(struct btrfs_fs_info *fs_info,
					      struct btrfs_key *location)
{
	struct btrfs_root *root;
	int ret;

	if (location->objectid == BTRFS_ROOT_TREE_OBJECTID)
		return fs_info->tree_root;
	if (location->objectid == BTRFS_EXTENT_TREE_OBJECTID)
		return fs_info->extent_root;

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
	ret = btrfs_find_dead_roots(fs_info->tree_root,
				    root->root_key.objectid, root);
	BUG_ON(ret);

	return root;
}

struct btrfs_root *btrfs_read_fs_root(struct btrfs_fs_info *fs_info,
				      struct btrfs_key *location,
				      const char *name, int namelen)
{
	struct btrfs_root *root;
	int ret;

	root = btrfs_read_fs_root_no_name(fs_info, location);
	if (!root)
		return NULL;

	if (root->in_sysfs)
		return root;

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
	root->in_sysfs = 1;
	return root;
}
#if 0
static int add_hasher(struct btrfs_fs_info *info, char *type) {
	struct btrfs_hasher *hasher;

	hasher = kmalloc(sizeof(*hasher), GFP_NOFS);
	if (!hasher)
		return -ENOMEM;
	hasher->hash_tfm = crypto_alloc_hash(type, 0, CRYPTO_ALG_ASYNC);
	if (!hasher->hash_tfm) {
		kfree(hasher);
		return -EINVAL;
	}
	spin_lock(&info->hash_lock);
	list_add(&hasher->list, &info->hashers);
	spin_unlock(&info->hash_lock);
	return 0;
}
#endif
struct btrfs_root *open_ctree(struct super_block *sb)
{
	u32 sectorsize;
	u32 nodesize;
	u32 leafsize;
	u32 blocksize;
	u32 stripesize;
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
	INIT_LIST_HEAD(&fs_info->hashers);
	spin_lock_init(&fs_info->hash_lock);
	spin_lock_init(&fs_info->delalloc_lock);

	memset(&fs_info->super_kobj, 0, sizeof(fs_info->super_kobj));
	init_completion(&fs_info->kobj_unregister);
	sb_set_blocksize(sb, 4096);
	fs_info->running_transaction = NULL;
	fs_info->last_trans_committed = 0;
	fs_info->tree_root = tree_root;
	fs_info->extent_root = extent_root;
	fs_info->sb = sb;
	fs_info->throttles = 0;
	fs_info->mount_opt = 0;
	fs_info->max_extent = (u64)-1;
	fs_info->delalloc_bytes = 0;
	fs_info->btree_inode = new_inode(sb);
	fs_info->btree_inode->i_ino = 1;
	fs_info->btree_inode->i_nlink = 1;
	fs_info->btree_inode->i_size = sb->s_bdev->bd_inode->i_size;
	fs_info->btree_inode->i_mapping->a_ops = &btree_aops;
	extent_map_tree_init(&BTRFS_I(fs_info->btree_inode)->extent_tree,
			     fs_info->btree_inode->i_mapping,
			     GFP_NOFS);
	BTRFS_I(fs_info->btree_inode)->extent_tree.ops = &btree_extent_map_ops;

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
	fs_info->total_pinned = 0;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,18)
	INIT_WORK(&fs_info->trans_work, btrfs_transaction_cleaner, fs_info);
#else
	INIT_DELAYED_WORK(&fs_info->trans_work, btrfs_transaction_cleaner);
#endif
	BTRFS_I(fs_info->btree_inode)->root = tree_root;
	memset(&BTRFS_I(fs_info->btree_inode)->location, 0,
	       sizeof(struct btrfs_key));
	insert_inode_hash(fs_info->btree_inode);
	mapping_set_gfp_mask(fs_info->btree_inode->i_mapping, GFP_NOFS);

	mutex_init(&fs_info->trans_mutex);
	mutex_init(&fs_info->fs_mutex);

#if 0
	ret = add_hasher(fs_info, "crc32c");
	if (ret) {
		printk("btrfs: failed hash setup, modprobe cryptomgr?\n");
		err = -ENOMEM;
		goto fail_iput;
	}
#endif
	__setup_root(512, 512, 512, 512, tree_root,
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
	stripesize = btrfs_super_stripesize(disk_super);
	tree_root->nodesize = nodesize;
	tree_root->leafsize = leafsize;
	tree_root->sectorsize = sectorsize;
	tree_root->stripesize = stripesize;
	sb_set_blocksize(sb, sectorsize);

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

	filemap_write_and_wait(fs_info->btree_inode->i_mapping);

	extent_map_tree_empty_lru(&fs_info->free_space_cache);
	extent_map_tree_empty_lru(&fs_info->block_group_cache);
	extent_map_tree_empty_lru(&fs_info->pinned_extents);
	extent_map_tree_empty_lru(&fs_info->pending_del);
	extent_map_tree_empty_lru(&fs_info->extent_ins);
	extent_map_tree_empty_lru(&BTRFS_I(fs_info->btree_inode)->extent_tree);

	truncate_inode_pages(fs_info->btree_inode->i_mapping, 0);

	iput(fs_info->btree_inode);
#if 0
	while(!list_empty(&fs_info->hashers)) {
		struct btrfs_hasher *hasher;
		hasher = list_entry(fs_info->hashers.next, struct btrfs_hasher,
				    hashers);
		list_del(&hasher->hashers);
		crypto_free_hash(&fs_info->hash_tfm);
		kfree(hasher);
	}
#endif
	kfree(fs_info->extent_root);
	kfree(fs_info->tree_root);
	return 0;
}

int btrfs_buffer_uptodate(struct extent_buffer *buf)
{
	struct inode *btree_inode = buf->first_page->mapping->host;
	return extent_buffer_uptodate(&BTRFS_I(btree_inode)->extent_tree, buf);
}

int btrfs_set_buffer_uptodate(struct extent_buffer *buf)
{
	struct inode *btree_inode = buf->first_page->mapping->host;
	return set_extent_buffer_uptodate(&BTRFS_I(btree_inode)->extent_tree,
					  buf);
}

void btrfs_mark_buffer_dirty(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
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

void btrfs_throttle(struct btrfs_root *root)
{
	if (root->fs_info->throttles)
		congestion_wait(WRITE, HZ/10);
}

void btrfs_btree_balance_dirty(struct btrfs_root *root, unsigned long nr)
{
	balance_dirty_pages_ratelimited_nr(
			root->fs_info->btree_inode->i_mapping, 1);
}

void btrfs_set_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	set_extent_bits(&BTRFS_I(btree_inode)->extent_tree, buf->start,
			buf->start + buf->len - 1, EXTENT_DEFRAG, GFP_NOFS);
}

void btrfs_set_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	set_extent_bits(&BTRFS_I(btree_inode)->extent_tree, buf->start,
			buf->start + buf->len - 1, EXTENT_DEFRAG_DONE,
			GFP_NOFS);
}

int btrfs_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return test_range_bit(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1, EXTENT_DEFRAG, 0);
}

int btrfs_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return test_range_bit(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG_DONE, 0);
}

int btrfs_clear_buffer_defrag_done(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return clear_extent_bits(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG_DONE, GFP_NOFS);
}

int btrfs_clear_buffer_defrag(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return clear_extent_bits(&BTRFS_I(btree_inode)->extent_tree,
		     buf->start, buf->start + buf->len - 1,
		     EXTENT_DEFRAG, GFP_NOFS);
}

int btrfs_read_buffer(struct extent_buffer *buf)
{
	struct btrfs_root *root = BTRFS_I(buf->first_page->mapping->host)->root;
	struct inode *btree_inode = root->fs_info->btree_inode;
	return read_extent_buffer_pages(&BTRFS_I(btree_inode)->extent_tree,
					buf, 0, 1);
}

static struct extent_map_ops btree_extent_map_ops = {
	.writepage_io_hook = btree_writepage_io_hook,
};
