// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Red Hat.  All rights reserved.
 */

#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include <linux/ratelimit.h>
#include <linux/error-injection.h>
#include <linux/sched/mm.h>
#include "ctree.h"
#include "free-space-cache.h"
#include "transaction.h"
#include "disk-io.h"
#include "extent_io.h"
#include "inode-map.h"
#include "volumes.h"
#include "space-info.h"
#include "delalloc-space.h"
#include "block-group.h"
#include "discard.h"

#define BITS_PER_BITMAP		(PAGE_SIZE * 8UL)
#define MAX_CACHE_BYTES_PER_GIG	SZ_64K
#define FORCE_EXTENT_THRESHOLD	SZ_1M

struct btrfs_trim_range {
	u64 start;
	u64 bytes;
	struct list_head list;
};

static int count_bitmap_extents(struct btrfs_free_space_ctl *ctl,
				struct btrfs_free_space *bitmap_info);
static int link_free_space(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info);
static void unlink_free_space(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info);
static int btrfs_wait_cache_io_root(struct btrfs_root *root,
			     struct btrfs_trans_handle *trans,
			     struct btrfs_io_ctl *io_ctl,
			     struct btrfs_path *path);

static struct inode *__lookup_free_space_inode(struct btrfs_root *root,
					       struct btrfs_path *path,
					       u64 offset)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key key;
	struct btrfs_key location;
	struct btrfs_disk_key disk_key;
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	struct inode *inode = NULL;
	unsigned nofs_flag;
	int ret;

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret > 0) {
		btrfs_release_path(path);
		return ERR_PTR(-ENOENT);
	}

	leaf = path->nodes[0];
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	btrfs_free_space_key(leaf, header, &disk_key);
	btrfs_disk_key_to_cpu(&location, &disk_key);
	btrfs_release_path(path);

	/*
	 * We are often under a trans handle at this point, so we need to make
	 * sure NOFS is set to keep us from deadlocking.
	 */
	nofs_flag = memalloc_nofs_save();
	inode = btrfs_iget_path(fs_info->sb, location.objectid, root, path);
	btrfs_release_path(path);
	memalloc_nofs_restore(nofs_flag);
	if (IS_ERR(inode))
		return inode;

	mapping_set_gfp_mask(inode->i_mapping,
			mapping_gfp_constraint(inode->i_mapping,
			~(__GFP_FS | __GFP_HIGHMEM)));

	return inode;
}

struct inode *lookup_free_space_inode(struct btrfs_block_group *block_group,
		struct btrfs_path *path)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct inode *inode = NULL;
	u32 flags = BTRFS_INODE_NODATASUM | BTRFS_INODE_NODATACOW;

	spin_lock(&block_group->lock);
	if (block_group->inode)
		inode = igrab(block_group->inode);
	spin_unlock(&block_group->lock);
	if (inode)
		return inode;

	inode = __lookup_free_space_inode(fs_info->tree_root, path,
					  block_group->start);
	if (IS_ERR(inode))
		return inode;

	spin_lock(&block_group->lock);
	if (!((BTRFS_I(inode)->flags & flags) == flags)) {
		btrfs_info(fs_info, "Old style space inode found, converting.");
		BTRFS_I(inode)->flags |= BTRFS_INODE_NODATASUM |
			BTRFS_INODE_NODATACOW;
		block_group->disk_cache_state = BTRFS_DC_CLEAR;
	}

	if (!block_group->iref) {
		block_group->inode = igrab(inode);
		block_group->iref = 1;
	}
	spin_unlock(&block_group->lock);

	return inode;
}

static int __create_free_space_inode(struct btrfs_root *root,
				     struct btrfs_trans_handle *trans,
				     struct btrfs_path *path,
				     u64 ino, u64 offset)
{
	struct btrfs_key key;
	struct btrfs_disk_key disk_key;
	struct btrfs_free_space_header *header;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	u64 flags = BTRFS_INODE_NOCOMPRESS | BTRFS_INODE_PREALLOC;
	int ret;

	ret = btrfs_insert_empty_inode(trans, root, path, ino);
	if (ret)
		return ret;

	/* We inline crc's for the free disk space cache */
	if (ino != BTRFS_FREE_INO_OBJECTID)
		flags |= BTRFS_INODE_NODATASUM | BTRFS_INODE_NODATACOW;

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);
	btrfs_item_key(leaf, &disk_key, path->slots[0]);
	memzero_extent_buffer(leaf, (unsigned long)inode_item,
			     sizeof(*inode_item));
	btrfs_set_inode_generation(leaf, inode_item, trans->transid);
	btrfs_set_inode_size(leaf, inode_item, 0);
	btrfs_set_inode_nbytes(leaf, inode_item, 0);
	btrfs_set_inode_uid(leaf, inode_item, 0);
	btrfs_set_inode_gid(leaf, inode_item, 0);
	btrfs_set_inode_mode(leaf, inode_item, S_IFREG | 0600);
	btrfs_set_inode_flags(leaf, inode_item, flags);
	btrfs_set_inode_nlink(leaf, inode_item, 1);
	btrfs_set_inode_transid(leaf, inode_item, trans->transid);
	btrfs_set_inode_block_group(leaf, inode_item, offset);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(struct btrfs_free_space_header));
	if (ret < 0) {
		btrfs_release_path(path);
		return ret;
	}

	leaf = path->nodes[0];
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	memzero_extent_buffer(leaf, (unsigned long)header, sizeof(*header));
	btrfs_set_free_space_key(leaf, header, &disk_key);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	return 0;
}

int create_free_space_inode(struct btrfs_trans_handle *trans,
			    struct btrfs_block_group *block_group,
			    struct btrfs_path *path)
{
	int ret;
	u64 ino;

	ret = btrfs_find_free_objectid(trans->fs_info->tree_root, &ino);
	if (ret < 0)
		return ret;

	return __create_free_space_inode(trans->fs_info->tree_root, trans, path,
					 ino, block_group->start);
}

int btrfs_check_trunc_cache_free_space(struct btrfs_fs_info *fs_info,
				       struct btrfs_block_rsv *rsv)
{
	u64 needed_bytes;
	int ret;

	/* 1 for slack space, 1 for updating the inode */
	needed_bytes = btrfs_calc_insert_metadata_size(fs_info, 1) +
		btrfs_calc_metadata_size(fs_info, 1);

	spin_lock(&rsv->lock);
	if (rsv->reserved < needed_bytes)
		ret = -ENOSPC;
	else
		ret = 0;
	spin_unlock(&rsv->lock);
	return ret;
}

int btrfs_truncate_free_space_cache(struct btrfs_trans_handle *trans,
				    struct btrfs_block_group *block_group,
				    struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;
	bool locked = false;

	if (block_group) {
		struct btrfs_path *path = btrfs_alloc_path();

		if (!path) {
			ret = -ENOMEM;
			goto fail;
		}
		locked = true;
		mutex_lock(&trans->transaction->cache_write_mutex);
		if (!list_empty(&block_group->io_list)) {
			list_del_init(&block_group->io_list);

			btrfs_wait_cache_io(trans, block_group, path);
			btrfs_put_block_group(block_group);
		}

		/*
		 * now that we've truncated the cache away, its no longer
		 * setup or written
		 */
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_CLEAR;
		spin_unlock(&block_group->lock);
		btrfs_free_path(path);
	}

	btrfs_i_size_write(BTRFS_I(inode), 0);
	truncate_pagecache(inode, 0);

	/*
	 * We skip the throttling logic for free space cache inodes, so we don't
	 * need to check for -EAGAIN.
	 */
	ret = btrfs_truncate_inode_items(trans, root, inode,
					 0, BTRFS_EXTENT_DATA_KEY);
	if (ret)
		goto fail;

	ret = btrfs_update_inode(trans, root, inode);

fail:
	if (locked)
		mutex_unlock(&trans->transaction->cache_write_mutex);
	if (ret)
		btrfs_abort_transaction(trans, ret);

	return ret;
}

static void readahead_cache(struct inode *inode)
{
	struct file_ra_state *ra;
	unsigned long last_index;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return;

	file_ra_state_init(ra, inode->i_mapping);
	last_index = (i_size_read(inode) - 1) >> PAGE_SHIFT;

	page_cache_sync_readahead(inode->i_mapping, ra, NULL, 0, last_index);

	kfree(ra);
}

static int io_ctl_init(struct btrfs_io_ctl *io_ctl, struct inode *inode,
		       int write)
{
	int num_pages;
	int check_crcs = 0;

	num_pages = DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE);

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FREE_INO_OBJECTID)
		check_crcs = 1;

	/* Make sure we can fit our crcs and generation into the first page */
	if (write && check_crcs &&
	    (num_pages * sizeof(u32) + sizeof(u64)) > PAGE_SIZE)
		return -ENOSPC;

	memset(io_ctl, 0, sizeof(struct btrfs_io_ctl));

	io_ctl->pages = kcalloc(num_pages, sizeof(struct page *), GFP_NOFS);
	if (!io_ctl->pages)
		return -ENOMEM;

	io_ctl->num_pages = num_pages;
	io_ctl->fs_info = btrfs_sb(inode->i_sb);
	io_ctl->check_crcs = check_crcs;
	io_ctl->inode = inode;

	return 0;
}
ALLOW_ERROR_INJECTION(io_ctl_init, ERRNO);

static void io_ctl_free(struct btrfs_io_ctl *io_ctl)
{
	kfree(io_ctl->pages);
	io_ctl->pages = NULL;
}

static void io_ctl_unmap_page(struct btrfs_io_ctl *io_ctl)
{
	if (io_ctl->cur) {
		io_ctl->cur = NULL;
		io_ctl->orig = NULL;
	}
}

static void io_ctl_map_page(struct btrfs_io_ctl *io_ctl, int clear)
{
	ASSERT(io_ctl->index < io_ctl->num_pages);
	io_ctl->page = io_ctl->pages[io_ctl->index++];
	io_ctl->cur = page_address(io_ctl->page);
	io_ctl->orig = io_ctl->cur;
	io_ctl->size = PAGE_SIZE;
	if (clear)
		clear_page(io_ctl->cur);
}

static void io_ctl_drop_pages(struct btrfs_io_ctl *io_ctl)
{
	int i;

	io_ctl_unmap_page(io_ctl);

	for (i = 0; i < io_ctl->num_pages; i++) {
		if (io_ctl->pages[i]) {
			ClearPageChecked(io_ctl->pages[i]);
			unlock_page(io_ctl->pages[i]);
			put_page(io_ctl->pages[i]);
		}
	}
}

static int io_ctl_prepare_pages(struct btrfs_io_ctl *io_ctl, bool uptodate)
{
	struct page *page;
	struct inode *inode = io_ctl->inode;
	gfp_t mask = btrfs_alloc_write_mask(inode->i_mapping);
	int i;

	for (i = 0; i < io_ctl->num_pages; i++) {
		page = find_or_create_page(inode->i_mapping, i, mask);
		if (!page) {
			io_ctl_drop_pages(io_ctl);
			return -ENOMEM;
		}
		io_ctl->pages[i] = page;
		if (uptodate && !PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (page->mapping != inode->i_mapping) {
				btrfs_err(BTRFS_I(inode)->root->fs_info,
					  "free space cache page truncated");
				io_ctl_drop_pages(io_ctl);
				return -EIO;
			}
			if (!PageUptodate(page)) {
				btrfs_err(BTRFS_I(inode)->root->fs_info,
					   "error reading free space cache");
				io_ctl_drop_pages(io_ctl);
				return -EIO;
			}
		}
	}

	for (i = 0; i < io_ctl->num_pages; i++) {
		clear_page_dirty_for_io(io_ctl->pages[i]);
		set_page_extent_mapped(io_ctl->pages[i]);
	}

	return 0;
}

static void io_ctl_set_generation(struct btrfs_io_ctl *io_ctl, u64 generation)
{
	__le64 *val;

	io_ctl_map_page(io_ctl, 1);

	/*
	 * Skip the csum areas.  If we don't check crcs then we just have a
	 * 64bit chunk at the front of the first page.
	 */
	if (io_ctl->check_crcs) {
		io_ctl->cur += (sizeof(u32) * io_ctl->num_pages);
		io_ctl->size -= sizeof(u64) + (sizeof(u32) * io_ctl->num_pages);
	} else {
		io_ctl->cur += sizeof(u64);
		io_ctl->size -= sizeof(u64) * 2;
	}

	val = io_ctl->cur;
	*val = cpu_to_le64(generation);
	io_ctl->cur += sizeof(u64);
}

static int io_ctl_check_generation(struct btrfs_io_ctl *io_ctl, u64 generation)
{
	__le64 *gen;

	/*
	 * Skip the crc area.  If we don't check crcs then we just have a 64bit
	 * chunk at the front of the first page.
	 */
	if (io_ctl->check_crcs) {
		io_ctl->cur += sizeof(u32) * io_ctl->num_pages;
		io_ctl->size -= sizeof(u64) +
			(sizeof(u32) * io_ctl->num_pages);
	} else {
		io_ctl->cur += sizeof(u64);
		io_ctl->size -= sizeof(u64) * 2;
	}

	gen = io_ctl->cur;
	if (le64_to_cpu(*gen) != generation) {
		btrfs_err_rl(io_ctl->fs_info,
			"space cache generation (%llu) does not match inode (%llu)",
				*gen, generation);
		io_ctl_unmap_page(io_ctl);
		return -EIO;
	}
	io_ctl->cur += sizeof(u64);
	return 0;
}

static void io_ctl_set_crc(struct btrfs_io_ctl *io_ctl, int index)
{
	u32 *tmp;
	u32 crc = ~(u32)0;
	unsigned offset = 0;

	if (!io_ctl->check_crcs) {
		io_ctl_unmap_page(io_ctl);
		return;
	}

	if (index == 0)
		offset = sizeof(u32) * io_ctl->num_pages;

	crc = btrfs_crc32c(crc, io_ctl->orig + offset, PAGE_SIZE - offset);
	btrfs_crc32c_final(crc, (u8 *)&crc);
	io_ctl_unmap_page(io_ctl);
	tmp = page_address(io_ctl->pages[0]);
	tmp += index;
	*tmp = crc;
}

static int io_ctl_check_crc(struct btrfs_io_ctl *io_ctl, int index)
{
	u32 *tmp, val;
	u32 crc = ~(u32)0;
	unsigned offset = 0;

	if (!io_ctl->check_crcs) {
		io_ctl_map_page(io_ctl, 0);
		return 0;
	}

	if (index == 0)
		offset = sizeof(u32) * io_ctl->num_pages;

	tmp = page_address(io_ctl->pages[0]);
	tmp += index;
	val = *tmp;

	io_ctl_map_page(io_ctl, 0);
	crc = btrfs_crc32c(crc, io_ctl->orig + offset, PAGE_SIZE - offset);
	btrfs_crc32c_final(crc, (u8 *)&crc);
	if (val != crc) {
		btrfs_err_rl(io_ctl->fs_info,
			"csum mismatch on free space cache");
		io_ctl_unmap_page(io_ctl);
		return -EIO;
	}

	return 0;
}

static int io_ctl_add_entry(struct btrfs_io_ctl *io_ctl, u64 offset, u64 bytes,
			    void *bitmap)
{
	struct btrfs_free_space_entry *entry;

	if (!io_ctl->cur)
		return -ENOSPC;

	entry = io_ctl->cur;
	entry->offset = cpu_to_le64(offset);
	entry->bytes = cpu_to_le64(bytes);
	entry->type = (bitmap) ? BTRFS_FREE_SPACE_BITMAP :
		BTRFS_FREE_SPACE_EXTENT;
	io_ctl->cur += sizeof(struct btrfs_free_space_entry);
	io_ctl->size -= sizeof(struct btrfs_free_space_entry);

	if (io_ctl->size >= sizeof(struct btrfs_free_space_entry))
		return 0;

	io_ctl_set_crc(io_ctl, io_ctl->index - 1);

	/* No more pages to map */
	if (io_ctl->index >= io_ctl->num_pages)
		return 0;

	/* map the next page */
	io_ctl_map_page(io_ctl, 1);
	return 0;
}

static int io_ctl_add_bitmap(struct btrfs_io_ctl *io_ctl, void *bitmap)
{
	if (!io_ctl->cur)
		return -ENOSPC;

	/*
	 * If we aren't at the start of the current page, unmap this one and
	 * map the next one if there is any left.
	 */
	if (io_ctl->cur != io_ctl->orig) {
		io_ctl_set_crc(io_ctl, io_ctl->index - 1);
		if (io_ctl->index >= io_ctl->num_pages)
			return -ENOSPC;
		io_ctl_map_page(io_ctl, 0);
	}

	copy_page(io_ctl->cur, bitmap);
	io_ctl_set_crc(io_ctl, io_ctl->index - 1);
	if (io_ctl->index < io_ctl->num_pages)
		io_ctl_map_page(io_ctl, 0);
	return 0;
}

static void io_ctl_zero_remaining_pages(struct btrfs_io_ctl *io_ctl)
{
	/*
	 * If we're not on the boundary we know we've modified the page and we
	 * need to crc the page.
	 */
	if (io_ctl->cur != io_ctl->orig)
		io_ctl_set_crc(io_ctl, io_ctl->index - 1);
	else
		io_ctl_unmap_page(io_ctl);

	while (io_ctl->index < io_ctl->num_pages) {
		io_ctl_map_page(io_ctl, 1);
		io_ctl_set_crc(io_ctl, io_ctl->index - 1);
	}
}

static int io_ctl_read_entry(struct btrfs_io_ctl *io_ctl,
			    struct btrfs_free_space *entry, u8 *type)
{
	struct btrfs_free_space_entry *e;
	int ret;

	if (!io_ctl->cur) {
		ret = io_ctl_check_crc(io_ctl, io_ctl->index);
		if (ret)
			return ret;
	}

	e = io_ctl->cur;
	entry->offset = le64_to_cpu(e->offset);
	entry->bytes = le64_to_cpu(e->bytes);
	*type = e->type;
	io_ctl->cur += sizeof(struct btrfs_free_space_entry);
	io_ctl->size -= sizeof(struct btrfs_free_space_entry);

	if (io_ctl->size >= sizeof(struct btrfs_free_space_entry))
		return 0;

	io_ctl_unmap_page(io_ctl);

	return 0;
}

static int io_ctl_read_bitmap(struct btrfs_io_ctl *io_ctl,
			      struct btrfs_free_space *entry)
{
	int ret;

	ret = io_ctl_check_crc(io_ctl, io_ctl->index);
	if (ret)
		return ret;

	copy_page(entry->bitmap, io_ctl->cur);
	io_ctl_unmap_page(io_ctl);

	return 0;
}

/*
 * Since we attach pinned extents after the fact we can have contiguous sections
 * of free space that are split up in entries.  This poses a problem with the
 * tree logging stuff since it could have allocated across what appears to be 2
 * entries since we would have merged the entries when adding the pinned extents
 * back to the free space cache.  So run through the space cache that we just
 * loaded and merge contiguous entries.  This will make the log replay stuff not
 * blow up and it will make for nicer allocator behavior.
 */
static void merge_space_tree(struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_free_space *e, *prev = NULL;
	struct rb_node *n;

again:
	spin_lock(&ctl->tree_lock);
	for (n = rb_first(&ctl->free_space_offset); n; n = rb_next(n)) {
		e = rb_entry(n, struct btrfs_free_space, offset_index);
		if (!prev)
			goto next;
		if (e->bitmap || prev->bitmap)
			goto next;
		if (prev->offset + prev->bytes == e->offset) {
			unlink_free_space(ctl, prev);
			unlink_free_space(ctl, e);
			prev->bytes += e->bytes;
			kmem_cache_free(btrfs_free_space_cachep, e);
			link_free_space(ctl, prev);
			prev = NULL;
			spin_unlock(&ctl->tree_lock);
			goto again;
		}
next:
		prev = e;
	}
	spin_unlock(&ctl->tree_lock);
}

static int __load_free_space_cache(struct btrfs_root *root, struct inode *inode,
				   struct btrfs_free_space_ctl *ctl,
				   struct btrfs_path *path, u64 offset)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	struct btrfs_io_ctl io_ctl;
	struct btrfs_key key;
	struct btrfs_free_space *e, *n;
	LIST_HEAD(bitmaps);
	u64 num_entries;
	u64 num_bitmaps;
	u64 generation;
	u8 type;
	int ret = 0;

	/* Nothing in the space cache, goodbye */
	if (!i_size_read(inode))
		return 0;

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		return 0;
	else if (ret > 0) {
		btrfs_release_path(path);
		return 0;
	}

	ret = -1;

	leaf = path->nodes[0];
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	num_entries = btrfs_free_space_entries(leaf, header);
	num_bitmaps = btrfs_free_space_bitmaps(leaf, header);
	generation = btrfs_free_space_generation(leaf, header);
	btrfs_release_path(path);

	if (!BTRFS_I(inode)->generation) {
		btrfs_info(fs_info,
			   "the free space cache file (%llu) is invalid, skip it",
			   offset);
		return 0;
	}

	if (BTRFS_I(inode)->generation != generation) {
		btrfs_err(fs_info,
			  "free space inode generation (%llu) did not match free space cache generation (%llu)",
			  BTRFS_I(inode)->generation, generation);
		return 0;
	}

	if (!num_entries)
		return 0;

	ret = io_ctl_init(&io_ctl, inode, 0);
	if (ret)
		return ret;

	readahead_cache(inode);

	ret = io_ctl_prepare_pages(&io_ctl, true);
	if (ret)
		goto out;

	ret = io_ctl_check_crc(&io_ctl, 0);
	if (ret)
		goto free_cache;

	ret = io_ctl_check_generation(&io_ctl, generation);
	if (ret)
		goto free_cache;

	while (num_entries) {
		e = kmem_cache_zalloc(btrfs_free_space_cachep,
				      GFP_NOFS);
		if (!e)
			goto free_cache;

		ret = io_ctl_read_entry(&io_ctl, e, &type);
		if (ret) {
			kmem_cache_free(btrfs_free_space_cachep, e);
			goto free_cache;
		}

		/*
		 * Sync discard ensures that the free space cache is always
		 * trimmed.  So when reading this in, the state should reflect
		 * that.  We also do this for async as a stop gap for lack of
		 * persistence.
		 */
		if (btrfs_test_opt(fs_info, DISCARD_SYNC) ||
		    btrfs_test_opt(fs_info, DISCARD_ASYNC))
			e->trim_state = BTRFS_TRIM_STATE_TRIMMED;

		if (!e->bytes) {
			kmem_cache_free(btrfs_free_space_cachep, e);
			goto free_cache;
		}

		if (type == BTRFS_FREE_SPACE_EXTENT) {
			spin_lock(&ctl->tree_lock);
			ret = link_free_space(ctl, e);
			spin_unlock(&ctl->tree_lock);
			if (ret) {
				btrfs_err(fs_info,
					"Duplicate entries in free space cache, dumping");
				kmem_cache_free(btrfs_free_space_cachep, e);
				goto free_cache;
			}
		} else {
			ASSERT(num_bitmaps);
			num_bitmaps--;
			e->bitmap = kmem_cache_zalloc(
					btrfs_free_space_bitmap_cachep, GFP_NOFS);
			if (!e->bitmap) {
				kmem_cache_free(
					btrfs_free_space_cachep, e);
				goto free_cache;
			}
			spin_lock(&ctl->tree_lock);
			ret = link_free_space(ctl, e);
			ctl->total_bitmaps++;
			ctl->op->recalc_thresholds(ctl);
			spin_unlock(&ctl->tree_lock);
			if (ret) {
				btrfs_err(fs_info,
					"Duplicate entries in free space cache, dumping");
				kmem_cache_free(btrfs_free_space_cachep, e);
				goto free_cache;
			}
			list_add_tail(&e->list, &bitmaps);
		}

		num_entries--;
	}

	io_ctl_unmap_page(&io_ctl);

	/*
	 * We add the bitmaps at the end of the entries in order that
	 * the bitmap entries are added to the cache.
	 */
	list_for_each_entry_safe(e, n, &bitmaps, list) {
		list_del_init(&e->list);
		ret = io_ctl_read_bitmap(&io_ctl, e);
		if (ret)
			goto free_cache;
		e->bitmap_extents = count_bitmap_extents(ctl, e);
		if (!btrfs_free_space_trimmed(e)) {
			ctl->discardable_extents[BTRFS_STAT_CURR] +=
				e->bitmap_extents;
			ctl->discardable_bytes[BTRFS_STAT_CURR] += e->bytes;
		}
	}

	io_ctl_drop_pages(&io_ctl);
	merge_space_tree(ctl);
	ret = 1;
out:
	btrfs_discard_update_discardable(ctl->private, ctl);
	io_ctl_free(&io_ctl);
	return ret;
free_cache:
	io_ctl_drop_pages(&io_ctl);
	__btrfs_remove_free_space_cache(ctl);
	goto out;
}

int load_free_space_cache(struct btrfs_block_group *block_group)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct inode *inode;
	struct btrfs_path *path;
	int ret = 0;
	bool matched;
	u64 used = block_group->used;

	/*
	 * If this block group has been marked to be cleared for one reason or
	 * another then we can't trust the on disk cache, so just return.
	 */
	spin_lock(&block_group->lock);
	if (block_group->disk_cache_state != BTRFS_DC_WRITTEN) {
		spin_unlock(&block_group->lock);
		return 0;
	}
	spin_unlock(&block_group->lock);

	path = btrfs_alloc_path();
	if (!path)
		return 0;
	path->search_commit_root = 1;
	path->skip_locking = 1;

	/*
	 * We must pass a path with search_commit_root set to btrfs_iget in
	 * order to avoid a deadlock when allocating extents for the tree root.
	 *
	 * When we are COWing an extent buffer from the tree root, when looking
	 * for a free extent, at extent-tree.c:find_free_extent(), we can find
	 * block group without its free space cache loaded. When we find one
	 * we must load its space cache which requires reading its free space
	 * cache's inode item from the root tree. If this inode item is located
	 * in the same leaf that we started COWing before, then we end up in
	 * deadlock on the extent buffer (trying to read lock it when we
	 * previously write locked it).
	 *
	 * It's safe to read the inode item using the commit root because
	 * block groups, once loaded, stay in memory forever (until they are
	 * removed) as well as their space caches once loaded. New block groups
	 * once created get their ->cached field set to BTRFS_CACHE_FINISHED so
	 * we will never try to read their inode item while the fs is mounted.
	 */
	inode = lookup_free_space_inode(block_group, path);
	if (IS_ERR(inode)) {
		btrfs_free_path(path);
		return 0;
	}

	/* We may have converted the inode and made the cache invalid. */
	spin_lock(&block_group->lock);
	if (block_group->disk_cache_state != BTRFS_DC_WRITTEN) {
		spin_unlock(&block_group->lock);
		btrfs_free_path(path);
		goto out;
	}
	spin_unlock(&block_group->lock);

	ret = __load_free_space_cache(fs_info->tree_root, inode, ctl,
				      path, block_group->start);
	btrfs_free_path(path);
	if (ret <= 0)
		goto out;

	spin_lock(&ctl->tree_lock);
	matched = (ctl->free_space == (block_group->length - used -
				       block_group->bytes_super));
	spin_unlock(&ctl->tree_lock);

	if (!matched) {
		__btrfs_remove_free_space_cache(ctl);
		btrfs_warn(fs_info,
			   "block group %llu has wrong amount of free space",
			   block_group->start);
		ret = -1;
	}
out:
	if (ret < 0) {
		/* This cache is bogus, make sure it gets cleared */
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_CLEAR;
		spin_unlock(&block_group->lock);
		ret = 0;

		btrfs_warn(fs_info,
			   "failed to load free space cache for block group %llu, rebuilding it now",
			   block_group->start);
	}

	iput(inode);
	return ret;
}

static noinline_for_stack
int write_cache_extent_entries(struct btrfs_io_ctl *io_ctl,
			      struct btrfs_free_space_ctl *ctl,
			      struct btrfs_block_group *block_group,
			      int *entries, int *bitmaps,
			      struct list_head *bitmap_list)
{
	int ret;
	struct btrfs_free_cluster *cluster = NULL;
	struct btrfs_free_cluster *cluster_locked = NULL;
	struct rb_node *node = rb_first(&ctl->free_space_offset);
	struct btrfs_trim_range *trim_entry;

	/* Get the cluster for this block_group if it exists */
	if (block_group && !list_empty(&block_group->cluster_list)) {
		cluster = list_entry(block_group->cluster_list.next,
				     struct btrfs_free_cluster,
				     block_group_list);
	}

	if (!node && cluster) {
		cluster_locked = cluster;
		spin_lock(&cluster_locked->lock);
		node = rb_first(&cluster->root);
		cluster = NULL;
	}

	/* Write out the extent entries */
	while (node) {
		struct btrfs_free_space *e;

		e = rb_entry(node, struct btrfs_free_space, offset_index);
		*entries += 1;

		ret = io_ctl_add_entry(io_ctl, e->offset, e->bytes,
				       e->bitmap);
		if (ret)
			goto fail;

		if (e->bitmap) {
			list_add_tail(&e->list, bitmap_list);
			*bitmaps += 1;
		}
		node = rb_next(node);
		if (!node && cluster) {
			node = rb_first(&cluster->root);
			cluster_locked = cluster;
			spin_lock(&cluster_locked->lock);
			cluster = NULL;
		}
	}
	if (cluster_locked) {
		spin_unlock(&cluster_locked->lock);
		cluster_locked = NULL;
	}

	/*
	 * Make sure we don't miss any range that was removed from our rbtree
	 * because trimming is running. Otherwise after a umount+mount (or crash
	 * after committing the transaction) we would leak free space and get
	 * an inconsistent free space cache report from fsck.
	 */
	list_for_each_entry(trim_entry, &ctl->trimming_ranges, list) {
		ret = io_ctl_add_entry(io_ctl, trim_entry->start,
				       trim_entry->bytes, NULL);
		if (ret)
			goto fail;
		*entries += 1;
	}

	return 0;
fail:
	if (cluster_locked)
		spin_unlock(&cluster_locked->lock);
	return -ENOSPC;
}

static noinline_for_stack int
update_cache_item(struct btrfs_trans_handle *trans,
		  struct btrfs_root *root,
		  struct inode *inode,
		  struct btrfs_path *path, u64 offset,
		  int entries, int bitmaps)
{
	struct btrfs_key key;
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	int ret;

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
	if (ret < 0) {
		clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, inode->i_size - 1,
				 EXTENT_DELALLOC, 0, 0, NULL);
		goto fail;
	}
	leaf = path->nodes[0];
	if (ret > 0) {
		struct btrfs_key found_key;
		ASSERT(path->slots[0]);
		path->slots[0]--;
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != BTRFS_FREE_SPACE_OBJECTID ||
		    found_key.offset != offset) {
			clear_extent_bit(&BTRFS_I(inode)->io_tree, 0,
					 inode->i_size - 1, EXTENT_DELALLOC, 0,
					 0, NULL);
			btrfs_release_path(path);
			goto fail;
		}
	}

	BTRFS_I(inode)->generation = trans->transid;
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	btrfs_set_free_space_entries(leaf, header, entries);
	btrfs_set_free_space_bitmaps(leaf, header, bitmaps);
	btrfs_set_free_space_generation(leaf, header, trans->transid);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	return 0;

fail:
	return -1;
}

static noinline_for_stack int write_pinned_extent_entries(
			    struct btrfs_trans_handle *trans,
			    struct btrfs_block_group *block_group,
			    struct btrfs_io_ctl *io_ctl,
			    int *entries)
{
	u64 start, extent_start, extent_end, len;
	struct extent_io_tree *unpin = NULL;
	int ret;

	if (!block_group)
		return 0;

	/*
	 * We want to add any pinned extents to our free space cache
	 * so we don't leak the space
	 *
	 * We shouldn't have switched the pinned extents yet so this is the
	 * right one
	 */
	unpin = &trans->transaction->pinned_extents;

	start = block_group->start;

	while (start < block_group->start + block_group->length) {
		ret = find_first_extent_bit(unpin, start,
					    &extent_start, &extent_end,
					    EXTENT_DIRTY, NULL);
		if (ret)
			return 0;

		/* This pinned extent is out of our range */
		if (extent_start >= block_group->start + block_group->length)
			return 0;

		extent_start = max(extent_start, start);
		extent_end = min(block_group->start + block_group->length,
				 extent_end + 1);
		len = extent_end - extent_start;

		*entries += 1;
		ret = io_ctl_add_entry(io_ctl, extent_start, len, NULL);
		if (ret)
			return -ENOSPC;

		start = extent_end;
	}

	return 0;
}

static noinline_for_stack int
write_bitmap_entries(struct btrfs_io_ctl *io_ctl, struct list_head *bitmap_list)
{
	struct btrfs_free_space *entry, *next;
	int ret;

	/* Write out the bitmaps */
	list_for_each_entry_safe(entry, next, bitmap_list, list) {
		ret = io_ctl_add_bitmap(io_ctl, entry->bitmap);
		if (ret)
			return -ENOSPC;
		list_del_init(&entry->list);
	}

	return 0;
}

static int flush_dirty_cache(struct inode *inode)
{
	int ret;

	ret = btrfs_wait_ordered_range(inode, 0, (u64)-1);
	if (ret)
		clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, inode->i_size - 1,
				 EXTENT_DELALLOC, 0, 0, NULL);

	return ret;
}

static void noinline_for_stack
cleanup_bitmap_list(struct list_head *bitmap_list)
{
	struct btrfs_free_space *entry, *next;

	list_for_each_entry_safe(entry, next, bitmap_list, list)
		list_del_init(&entry->list);
}

static void noinline_for_stack
cleanup_write_cache_enospc(struct inode *inode,
			   struct btrfs_io_ctl *io_ctl,
			   struct extent_state **cached_state)
{
	io_ctl_drop_pages(io_ctl);
	unlock_extent_cached(&BTRFS_I(inode)->io_tree, 0,
			     i_size_read(inode) - 1, cached_state);
}

static int __btrfs_wait_cache_io(struct btrfs_root *root,
				 struct btrfs_trans_handle *trans,
				 struct btrfs_block_group *block_group,
				 struct btrfs_io_ctl *io_ctl,
				 struct btrfs_path *path, u64 offset)
{
	int ret;
	struct inode *inode = io_ctl->inode;

	if (!inode)
		return 0;

	/* Flush the dirty pages in the cache file. */
	ret = flush_dirty_cache(inode);
	if (ret)
		goto out;

	/* Update the cache item to tell everyone this cache file is valid. */
	ret = update_cache_item(trans, root, inode, path, offset,
				io_ctl->entries, io_ctl->bitmaps);
out:
	if (ret) {
		invalidate_inode_pages2(inode->i_mapping);
		BTRFS_I(inode)->generation = 0;
		if (block_group)
			btrfs_debug(root->fs_info,
	  "failed to write free space cache for block group %llu error %d",
				  block_group->start, ret);
	}
	btrfs_update_inode(trans, root, inode);

	if (block_group) {
		/* the dirty list is protected by the dirty_bgs_lock */
		spin_lock(&trans->transaction->dirty_bgs_lock);

		/* the disk_cache_state is protected by the block group lock */
		spin_lock(&block_group->lock);

		/*
		 * only mark this as written if we didn't get put back on
		 * the dirty list while waiting for IO.   Otherwise our
		 * cache state won't be right, and we won't get written again
		 */
		if (!ret && list_empty(&block_group->dirty_list))
			block_group->disk_cache_state = BTRFS_DC_WRITTEN;
		else if (ret)
			block_group->disk_cache_state = BTRFS_DC_ERROR;

		spin_unlock(&block_group->lock);
		spin_unlock(&trans->transaction->dirty_bgs_lock);
		io_ctl->inode = NULL;
		iput(inode);
	}

	return ret;

}

static int btrfs_wait_cache_io_root(struct btrfs_root *root,
				    struct btrfs_trans_handle *trans,
				    struct btrfs_io_ctl *io_ctl,
				    struct btrfs_path *path)
{
	return __btrfs_wait_cache_io(root, trans, NULL, io_ctl, path, 0);
}

int btrfs_wait_cache_io(struct btrfs_trans_handle *trans,
			struct btrfs_block_group *block_group,
			struct btrfs_path *path)
{
	return __btrfs_wait_cache_io(block_group->fs_info->tree_root, trans,
				     block_group, &block_group->io_ctl,
				     path, block_group->start);
}

/**
 * __btrfs_write_out_cache - write out cached info to an inode
 * @root - the root the inode belongs to
 * @ctl - the free space cache we are going to write out
 * @block_group - the block_group for this cache if it belongs to a block_group
 * @trans - the trans handle
 *
 * This function writes out a free space cache struct to disk for quick recovery
 * on mount.  This will return 0 if it was successful in writing the cache out,
 * or an errno if it was not.
 */
static int __btrfs_write_out_cache(struct btrfs_root *root, struct inode *inode,
				   struct btrfs_free_space_ctl *ctl,
				   struct btrfs_block_group *block_group,
				   struct btrfs_io_ctl *io_ctl,
				   struct btrfs_trans_handle *trans)
{
	struct extent_state *cached_state = NULL;
	LIST_HEAD(bitmap_list);
	int entries = 0;
	int bitmaps = 0;
	int ret;
	int must_iput = 0;

	if (!i_size_read(inode))
		return -EIO;

	WARN_ON(io_ctl->pages);
	ret = io_ctl_init(io_ctl, inode, 1);
	if (ret)
		return ret;

	if (block_group && (block_group->flags & BTRFS_BLOCK_GROUP_DATA)) {
		down_write(&block_group->data_rwsem);
		spin_lock(&block_group->lock);
		if (block_group->delalloc_bytes) {
			block_group->disk_cache_state = BTRFS_DC_WRITTEN;
			spin_unlock(&block_group->lock);
			up_write(&block_group->data_rwsem);
			BTRFS_I(inode)->generation = 0;
			ret = 0;
			must_iput = 1;
			goto out;
		}
		spin_unlock(&block_group->lock);
	}

	/* Lock all pages first so we can lock the extent safely. */
	ret = io_ctl_prepare_pages(io_ctl, false);
	if (ret)
		goto out_unlock;

	lock_extent_bits(&BTRFS_I(inode)->io_tree, 0, i_size_read(inode) - 1,
			 &cached_state);

	io_ctl_set_generation(io_ctl, trans->transid);

	mutex_lock(&ctl->cache_writeout_mutex);
	/* Write out the extent entries in the free space cache */
	spin_lock(&ctl->tree_lock);
	ret = write_cache_extent_entries(io_ctl, ctl,
					 block_group, &entries, &bitmaps,
					 &bitmap_list);
	if (ret)
		goto out_nospc_locked;

	/*
	 * Some spaces that are freed in the current transaction are pinned,
	 * they will be added into free space cache after the transaction is
	 * committed, we shouldn't lose them.
	 *
	 * If this changes while we are working we'll get added back to
	 * the dirty list and redo it.  No locking needed
	 */
	ret = write_pinned_extent_entries(trans, block_group, io_ctl, &entries);
	if (ret)
		goto out_nospc_locked;

	/*
	 * At last, we write out all the bitmaps and keep cache_writeout_mutex
	 * locked while doing it because a concurrent trim can be manipulating
	 * or freeing the bitmap.
	 */
	ret = write_bitmap_entries(io_ctl, &bitmap_list);
	spin_unlock(&ctl->tree_lock);
	mutex_unlock(&ctl->cache_writeout_mutex);
	if (ret)
		goto out_nospc;

	/* Zero out the rest of the pages just to make sure */
	io_ctl_zero_remaining_pages(io_ctl);

	/* Everything is written out, now we dirty the pages in the file. */
	ret = btrfs_dirty_pages(BTRFS_I(inode), io_ctl->pages,
				io_ctl->num_pages, 0, i_size_read(inode),
				&cached_state);
	if (ret)
		goto out_nospc;

	if (block_group && (block_group->flags & BTRFS_BLOCK_GROUP_DATA))
		up_write(&block_group->data_rwsem);
	/*
	 * Release the pages and unlock the extent, we will flush
	 * them out later
	 */
	io_ctl_drop_pages(io_ctl);
	io_ctl_free(io_ctl);

	unlock_extent_cached(&BTRFS_I(inode)->io_tree, 0,
			     i_size_read(inode) - 1, &cached_state);

	/*
	 * at this point the pages are under IO and we're happy,
	 * The caller is responsible for waiting on them and updating
	 * the cache and the inode
	 */
	io_ctl->entries = entries;
	io_ctl->bitmaps = bitmaps;

	ret = btrfs_fdatawrite_range(inode, 0, (u64)-1);
	if (ret)
		goto out;

	return 0;

out_nospc_locked:
	cleanup_bitmap_list(&bitmap_list);
	spin_unlock(&ctl->tree_lock);
	mutex_unlock(&ctl->cache_writeout_mutex);

out_nospc:
	cleanup_write_cache_enospc(inode, io_ctl, &cached_state);

out_unlock:
	if (block_group && (block_group->flags & BTRFS_BLOCK_GROUP_DATA))
		up_write(&block_group->data_rwsem);

out:
	io_ctl->inode = NULL;
	io_ctl_free(io_ctl);
	if (ret) {
		invalidate_inode_pages2(inode->i_mapping);
		BTRFS_I(inode)->generation = 0;
	}
	btrfs_update_inode(trans, root, inode);
	if (must_iput)
		iput(inode);
	return ret;
}

int btrfs_write_out_cache(struct btrfs_trans_handle *trans,
			  struct btrfs_block_group *block_group,
			  struct btrfs_path *path)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct inode *inode;
	int ret = 0;

	spin_lock(&block_group->lock);
	if (block_group->disk_cache_state < BTRFS_DC_SETUP) {
		spin_unlock(&block_group->lock);
		return 0;
	}
	spin_unlock(&block_group->lock);

	inode = lookup_free_space_inode(block_group, path);
	if (IS_ERR(inode))
		return 0;

	ret = __btrfs_write_out_cache(fs_info->tree_root, inode, ctl,
				block_group, &block_group->io_ctl, trans);
	if (ret) {
		btrfs_debug(fs_info,
	  "failed to write free space cache for block group %llu error %d",
			  block_group->start, ret);
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_ERROR;
		spin_unlock(&block_group->lock);

		block_group->io_ctl.inode = NULL;
		iput(inode);
	}

	/*
	 * if ret == 0 the caller is expected to call btrfs_wait_cache_io
	 * to wait for IO and put the inode
	 */

	return ret;
}

static inline unsigned long offset_to_bit(u64 bitmap_start, u32 unit,
					  u64 offset)
{
	ASSERT(offset >= bitmap_start);
	offset -= bitmap_start;
	return (unsigned long)(div_u64(offset, unit));
}

static inline unsigned long bytes_to_bits(u64 bytes, u32 unit)
{
	return (unsigned long)(div_u64(bytes, unit));
}

static inline u64 offset_to_bitmap(struct btrfs_free_space_ctl *ctl,
				   u64 offset)
{
	u64 bitmap_start;
	u64 bytes_per_bitmap;

	bytes_per_bitmap = BITS_PER_BITMAP * ctl->unit;
	bitmap_start = offset - ctl->start;
	bitmap_start = div64_u64(bitmap_start, bytes_per_bitmap);
	bitmap_start *= bytes_per_bitmap;
	bitmap_start += ctl->start;

	return bitmap_start;
}

static int tree_insert_offset(struct rb_root *root, u64 offset,
			      struct rb_node *node, int bitmap)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_free_space *info;

	while (*p) {
		parent = *p;
		info = rb_entry(parent, struct btrfs_free_space, offset_index);

		if (offset < info->offset) {
			p = &(*p)->rb_left;
		} else if (offset > info->offset) {
			p = &(*p)->rb_right;
		} else {
			/*
			 * we could have a bitmap entry and an extent entry
			 * share the same offset.  If this is the case, we want
			 * the extent entry to always be found first if we do a
			 * linear search through the tree, since we want to have
			 * the quickest allocation time, and allocating from an
			 * extent is faster than allocating from a bitmap.  So
			 * if we're inserting a bitmap and we find an entry at
			 * this offset, we want to go right, or after this entry
			 * logically.  If we are inserting an extent and we've
			 * found a bitmap, we want to go left, or before
			 * logically.
			 */
			if (bitmap) {
				if (info->bitmap) {
					WARN_ON_ONCE(1);
					return -EEXIST;
				}
				p = &(*p)->rb_right;
			} else {
				if (!info->bitmap) {
					WARN_ON_ONCE(1);
					return -EEXIST;
				}
				p = &(*p)->rb_left;
			}
		}
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, root);

	return 0;
}

/*
 * searches the tree for the given offset.
 *
 * fuzzy - If this is set, then we are trying to make an allocation, and we just
 * want a section that has at least bytes size and comes at or after the given
 * offset.
 */
static struct btrfs_free_space *
tree_search_offset(struct btrfs_free_space_ctl *ctl,
		   u64 offset, int bitmap_only, int fuzzy)
{
	struct rb_node *n = ctl->free_space_offset.rb_node;
	struct btrfs_free_space *entry, *prev = NULL;

	/* find entry that is closest to the 'offset' */
	while (1) {
		if (!n) {
			entry = NULL;
			break;
		}

		entry = rb_entry(n, struct btrfs_free_space, offset_index);
		prev = entry;

		if (offset < entry->offset)
			n = n->rb_left;
		else if (offset > entry->offset)
			n = n->rb_right;
		else
			break;
	}

	if (bitmap_only) {
		if (!entry)
			return NULL;
		if (entry->bitmap)
			return entry;

		/*
		 * bitmap entry and extent entry may share same offset,
		 * in that case, bitmap entry comes after extent entry.
		 */
		n = rb_next(n);
		if (!n)
			return NULL;
		entry = rb_entry(n, struct btrfs_free_space, offset_index);
		if (entry->offset != offset)
			return NULL;

		WARN_ON(!entry->bitmap);
		return entry;
	} else if (entry) {
		if (entry->bitmap) {
			/*
			 * if previous extent entry covers the offset,
			 * we should return it instead of the bitmap entry
			 */
			n = rb_prev(&entry->offset_index);
			if (n) {
				prev = rb_entry(n, struct btrfs_free_space,
						offset_index);
				if (!prev->bitmap &&
				    prev->offset + prev->bytes > offset)
					entry = prev;
			}
		}
		return entry;
	}

	if (!prev)
		return NULL;

	/* find last entry before the 'offset' */
	entry = prev;
	if (entry->offset > offset) {
		n = rb_prev(&entry->offset_index);
		if (n) {
			entry = rb_entry(n, struct btrfs_free_space,
					offset_index);
			ASSERT(entry->offset <= offset);
		} else {
			if (fuzzy)
				return entry;
			else
				return NULL;
		}
	}

	if (entry->bitmap) {
		n = rb_prev(&entry->offset_index);
		if (n) {
			prev = rb_entry(n, struct btrfs_free_space,
					offset_index);
			if (!prev->bitmap &&
			    prev->offset + prev->bytes > offset)
				return prev;
		}
		if (entry->offset + BITS_PER_BITMAP * ctl->unit > offset)
			return entry;
	} else if (entry->offset + entry->bytes > offset)
		return entry;

	if (!fuzzy)
		return NULL;

	while (1) {
		if (entry->bitmap) {
			if (entry->offset + BITS_PER_BITMAP *
			    ctl->unit > offset)
				break;
		} else {
			if (entry->offset + entry->bytes > offset)
				break;
		}

		n = rb_next(&entry->offset_index);
		if (!n)
			return NULL;
		entry = rb_entry(n, struct btrfs_free_space, offset_index);
	}
	return entry;
}

static inline void
__unlink_free_space(struct btrfs_free_space_ctl *ctl,
		    struct btrfs_free_space *info)
{
	rb_erase(&info->offset_index, &ctl->free_space_offset);
	ctl->free_extents--;

	if (!info->bitmap && !btrfs_free_space_trimmed(info)) {
		ctl->discardable_extents[BTRFS_STAT_CURR]--;
		ctl->discardable_bytes[BTRFS_STAT_CURR] -= info->bytes;
	}
}

static void unlink_free_space(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info)
{
	__unlink_free_space(ctl, info);
	ctl->free_space -= info->bytes;
}

static int link_free_space(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info)
{
	int ret = 0;

	ASSERT(info->bytes || info->bitmap);
	ret = tree_insert_offset(&ctl->free_space_offset, info->offset,
				 &info->offset_index, (info->bitmap != NULL));
	if (ret)
		return ret;

	if (!info->bitmap && !btrfs_free_space_trimmed(info)) {
		ctl->discardable_extents[BTRFS_STAT_CURR]++;
		ctl->discardable_bytes[BTRFS_STAT_CURR] += info->bytes;
	}

	ctl->free_space += info->bytes;
	ctl->free_extents++;
	return ret;
}

static void recalculate_thresholds(struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_block_group *block_group = ctl->private;
	u64 max_bytes;
	u64 bitmap_bytes;
	u64 extent_bytes;
	u64 size = block_group->length;
	u64 bytes_per_bg = BITS_PER_BITMAP * ctl->unit;
	u64 max_bitmaps = div64_u64(size + bytes_per_bg - 1, bytes_per_bg);

	max_bitmaps = max_t(u64, max_bitmaps, 1);

	ASSERT(ctl->total_bitmaps <= max_bitmaps);

	/*
	 * We are trying to keep the total amount of memory used per 1GiB of
	 * space to be MAX_CACHE_BYTES_PER_GIG.  However, with a reclamation
	 * mechanism of pulling extents >= FORCE_EXTENT_THRESHOLD out of
	 * bitmaps, we may end up using more memory than this.
	 */
	if (size < SZ_1G)
		max_bytes = MAX_CACHE_BYTES_PER_GIG;
	else
		max_bytes = MAX_CACHE_BYTES_PER_GIG * div_u64(size, SZ_1G);

	bitmap_bytes = ctl->total_bitmaps * ctl->unit;

	/*
	 * we want the extent entry threshold to always be at most 1/2 the max
	 * bytes we can have, or whatever is less than that.
	 */
	extent_bytes = max_bytes - bitmap_bytes;
	extent_bytes = min_t(u64, extent_bytes, max_bytes >> 1);

	ctl->extents_thresh =
		div_u64(extent_bytes, sizeof(struct btrfs_free_space));
}

static inline void __bitmap_clear_bits(struct btrfs_free_space_ctl *ctl,
				       struct btrfs_free_space *info,
				       u64 offset, u64 bytes)
{
	unsigned long start, count, end;
	int extent_delta = -1;

	start = offset_to_bit(info->offset, ctl->unit, offset);
	count = bytes_to_bits(bytes, ctl->unit);
	end = start + count;
	ASSERT(end <= BITS_PER_BITMAP);

	bitmap_clear(info->bitmap, start, count);

	info->bytes -= bytes;
	if (info->max_extent_size > ctl->unit)
		info->max_extent_size = 0;

	if (start && test_bit(start - 1, info->bitmap))
		extent_delta++;

	if (end < BITS_PER_BITMAP && test_bit(end, info->bitmap))
		extent_delta++;

	info->bitmap_extents += extent_delta;
	if (!btrfs_free_space_trimmed(info)) {
		ctl->discardable_extents[BTRFS_STAT_CURR] += extent_delta;
		ctl->discardable_bytes[BTRFS_STAT_CURR] -= bytes;
	}
}

static void bitmap_clear_bits(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info, u64 offset,
			      u64 bytes)
{
	__bitmap_clear_bits(ctl, info, offset, bytes);
	ctl->free_space -= bytes;
}

static void bitmap_set_bits(struct btrfs_free_space_ctl *ctl,
			    struct btrfs_free_space *info, u64 offset,
			    u64 bytes)
{
	unsigned long start, count, end;
	int extent_delta = 1;

	start = offset_to_bit(info->offset, ctl->unit, offset);
	count = bytes_to_bits(bytes, ctl->unit);
	end = start + count;
	ASSERT(end <= BITS_PER_BITMAP);

	bitmap_set(info->bitmap, start, count);

	info->bytes += bytes;
	ctl->free_space += bytes;

	if (start && test_bit(start - 1, info->bitmap))
		extent_delta--;

	if (end < BITS_PER_BITMAP && test_bit(end, info->bitmap))
		extent_delta--;

	info->bitmap_extents += extent_delta;
	if (!btrfs_free_space_trimmed(info)) {
		ctl->discardable_extents[BTRFS_STAT_CURR] += extent_delta;
		ctl->discardable_bytes[BTRFS_STAT_CURR] += bytes;
	}
}

/*
 * If we can not find suitable extent, we will use bytes to record
 * the size of the max extent.
 */
static int search_bitmap(struct btrfs_free_space_ctl *ctl,
			 struct btrfs_free_space *bitmap_info, u64 *offset,
			 u64 *bytes, bool for_alloc)
{
	unsigned long found_bits = 0;
	unsigned long max_bits = 0;
	unsigned long bits, i;
	unsigned long next_zero;
	unsigned long extent_bits;

	/*
	 * Skip searching the bitmap if we don't have a contiguous section that
	 * is large enough for this allocation.
	 */
	if (for_alloc &&
	    bitmap_info->max_extent_size &&
	    bitmap_info->max_extent_size < *bytes) {
		*bytes = bitmap_info->max_extent_size;
		return -1;
	}

	i = offset_to_bit(bitmap_info->offset, ctl->unit,
			  max_t(u64, *offset, bitmap_info->offset));
	bits = bytes_to_bits(*bytes, ctl->unit);

	for_each_set_bit_from(i, bitmap_info->bitmap, BITS_PER_BITMAP) {
		if (for_alloc && bits == 1) {
			found_bits = 1;
			break;
		}
		next_zero = find_next_zero_bit(bitmap_info->bitmap,
					       BITS_PER_BITMAP, i);
		extent_bits = next_zero - i;
		if (extent_bits >= bits) {
			found_bits = extent_bits;
			break;
		} else if (extent_bits > max_bits) {
			max_bits = extent_bits;
		}
		i = next_zero;
	}

	if (found_bits) {
		*offset = (u64)(i * ctl->unit) + bitmap_info->offset;
		*bytes = (u64)(found_bits) * ctl->unit;
		return 0;
	}

	*bytes = (u64)(max_bits) * ctl->unit;
	bitmap_info->max_extent_size = *bytes;
	return -1;
}

static inline u64 get_max_extent_size(struct btrfs_free_space *entry)
{
	if (entry->bitmap)
		return entry->max_extent_size;
	return entry->bytes;
}

/* Cache the size of the max extent in bytes */
static struct btrfs_free_space *
find_free_space(struct btrfs_free_space_ctl *ctl, u64 *offset, u64 *bytes,
		unsigned long align, u64 *max_extent_size)
{
	struct btrfs_free_space *entry;
	struct rb_node *node;
	u64 tmp;
	u64 align_off;
	int ret;

	if (!ctl->free_space_offset.rb_node)
		goto out;

	entry = tree_search_offset(ctl, offset_to_bitmap(ctl, *offset), 0, 1);
	if (!entry)
		goto out;

	for (node = &entry->offset_index; node; node = rb_next(node)) {
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		if (entry->bytes < *bytes) {
			*max_extent_size = max(get_max_extent_size(entry),
					       *max_extent_size);
			continue;
		}

		/* make sure the space returned is big enough
		 * to match our requested alignment
		 */
		if (*bytes >= align) {
			tmp = entry->offset - ctl->start + align - 1;
			tmp = div64_u64(tmp, align);
			tmp = tmp * align + ctl->start;
			align_off = tmp - entry->offset;
		} else {
			align_off = 0;
			tmp = entry->offset;
		}

		if (entry->bytes < *bytes + align_off) {
			*max_extent_size = max(get_max_extent_size(entry),
					       *max_extent_size);
			continue;
		}

		if (entry->bitmap) {
			u64 size = *bytes;

			ret = search_bitmap(ctl, entry, &tmp, &size, true);
			if (!ret) {
				*offset = tmp;
				*bytes = size;
				return entry;
			} else {
				*max_extent_size =
					max(get_max_extent_size(entry),
					    *max_extent_size);
			}
			continue;
		}

		*offset = tmp;
		*bytes = entry->bytes - align_off;
		return entry;
	}
out:
	return NULL;
}

static int count_bitmap_extents(struct btrfs_free_space_ctl *ctl,
				struct btrfs_free_space *bitmap_info)
{
	struct btrfs_block_group *block_group = ctl->private;
	u64 bytes = bitmap_info->bytes;
	unsigned int rs, re;
	int count = 0;

	if (!block_group || !bytes)
		return count;

	bitmap_for_each_set_region(bitmap_info->bitmap, rs, re, 0,
				   BITS_PER_BITMAP) {
		bytes -= (rs - re) * ctl->unit;
		count++;

		if (!bytes)
			break;
	}

	return count;
}

static void add_new_bitmap(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info, u64 offset)
{
	info->offset = offset_to_bitmap(ctl, offset);
	info->bytes = 0;
	info->bitmap_extents = 0;
	INIT_LIST_HEAD(&info->list);
	link_free_space(ctl, info);
	ctl->total_bitmaps++;

	ctl->op->recalc_thresholds(ctl);
}

static void free_bitmap(struct btrfs_free_space_ctl *ctl,
			struct btrfs_free_space *bitmap_info)
{
	/*
	 * Normally when this is called, the bitmap is completely empty. However,
	 * if we are blowing up the free space cache for one reason or another
	 * via __btrfs_remove_free_space_cache(), then it may not be freed and
	 * we may leave stats on the table.
	 */
	if (bitmap_info->bytes && !btrfs_free_space_trimmed(bitmap_info)) {
		ctl->discardable_extents[BTRFS_STAT_CURR] -=
			bitmap_info->bitmap_extents;
		ctl->discardable_bytes[BTRFS_STAT_CURR] -= bitmap_info->bytes;

	}
	unlink_free_space(ctl, bitmap_info);
	kmem_cache_free(btrfs_free_space_bitmap_cachep, bitmap_info->bitmap);
	kmem_cache_free(btrfs_free_space_cachep, bitmap_info);
	ctl->total_bitmaps--;
	ctl->op->recalc_thresholds(ctl);
}

static noinline int remove_from_bitmap(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *bitmap_info,
			      u64 *offset, u64 *bytes)
{
	u64 end;
	u64 search_start, search_bytes;
	int ret;

again:
	end = bitmap_info->offset + (u64)(BITS_PER_BITMAP * ctl->unit) - 1;

	/*
	 * We need to search for bits in this bitmap.  We could only cover some
	 * of the extent in this bitmap thanks to how we add space, so we need
	 * to search for as much as it as we can and clear that amount, and then
	 * go searching for the next bit.
	 */
	search_start = *offset;
	search_bytes = ctl->unit;
	search_bytes = min(search_bytes, end - search_start + 1);
	ret = search_bitmap(ctl, bitmap_info, &search_start, &search_bytes,
			    false);
	if (ret < 0 || search_start != *offset)
		return -EINVAL;

	/* We may have found more bits than what we need */
	search_bytes = min(search_bytes, *bytes);

	/* Cannot clear past the end of the bitmap */
	search_bytes = min(search_bytes, end - search_start + 1);

	bitmap_clear_bits(ctl, bitmap_info, search_start, search_bytes);
	*offset += search_bytes;
	*bytes -= search_bytes;

	if (*bytes) {
		struct rb_node *next = rb_next(&bitmap_info->offset_index);
		if (!bitmap_info->bytes)
			free_bitmap(ctl, bitmap_info);

		/*
		 * no entry after this bitmap, but we still have bytes to
		 * remove, so something has gone wrong.
		 */
		if (!next)
			return -EINVAL;

		bitmap_info = rb_entry(next, struct btrfs_free_space,
				       offset_index);

		/*
		 * if the next entry isn't a bitmap we need to return to let the
		 * extent stuff do its work.
		 */
		if (!bitmap_info->bitmap)
			return -EAGAIN;

		/*
		 * Ok the next item is a bitmap, but it may not actually hold
		 * the information for the rest of this free space stuff, so
		 * look for it, and if we don't find it return so we can try
		 * everything over again.
		 */
		search_start = *offset;
		search_bytes = ctl->unit;
		ret = search_bitmap(ctl, bitmap_info, &search_start,
				    &search_bytes, false);
		if (ret < 0 || search_start != *offset)
			return -EAGAIN;

		goto again;
	} else if (!bitmap_info->bytes)
		free_bitmap(ctl, bitmap_info);

	return 0;
}

static u64 add_bytes_to_bitmap(struct btrfs_free_space_ctl *ctl,
			       struct btrfs_free_space *info, u64 offset,
			       u64 bytes, enum btrfs_trim_state trim_state)
{
	u64 bytes_to_set = 0;
	u64 end;

	/*
	 * This is a tradeoff to make bitmap trim state minimal.  We mark the
	 * whole bitmap untrimmed if at any point we add untrimmed regions.
	 */
	if (trim_state == BTRFS_TRIM_STATE_UNTRIMMED) {
		if (btrfs_free_space_trimmed(info)) {
			ctl->discardable_extents[BTRFS_STAT_CURR] +=
				info->bitmap_extents;
			ctl->discardable_bytes[BTRFS_STAT_CURR] += info->bytes;
		}
		info->trim_state = BTRFS_TRIM_STATE_UNTRIMMED;
	}

	end = info->offset + (u64)(BITS_PER_BITMAP * ctl->unit);

	bytes_to_set = min(end - offset, bytes);

	bitmap_set_bits(ctl, info, offset, bytes_to_set);

	/*
	 * We set some bytes, we have no idea what the max extent size is
	 * anymore.
	 */
	info->max_extent_size = 0;

	return bytes_to_set;

}

static bool use_bitmap(struct btrfs_free_space_ctl *ctl,
		      struct btrfs_free_space *info)
{
	struct btrfs_block_group *block_group = ctl->private;
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	bool forced = false;

#ifdef CONFIG_BTRFS_DEBUG
	if (btrfs_should_fragment_free_space(block_group))
		forced = true;
#endif

	/* This is a way to reclaim large regions from the bitmaps. */
	if (!forced && info->bytes >= FORCE_EXTENT_THRESHOLD)
		return false;

	/*
	 * If we are below the extents threshold then we can add this as an
	 * extent, and don't have to deal with the bitmap
	 */
	if (!forced && ctl->free_extents < ctl->extents_thresh) {
		/*
		 * If this block group has some small extents we don't want to
		 * use up all of our free slots in the cache with them, we want
		 * to reserve them to larger extents, however if we have plenty
		 * of cache left then go ahead an dadd them, no sense in adding
		 * the overhead of a bitmap if we don't have to.
		 */
		if (info->bytes <= fs_info->sectorsize * 8) {
			if (ctl->free_extents * 3 <= ctl->extents_thresh)
				return false;
		} else {
			return false;
		}
	}

	/*
	 * The original block groups from mkfs can be really small, like 8
	 * megabytes, so don't bother with a bitmap for those entries.  However
	 * some block groups can be smaller than what a bitmap would cover but
	 * are still large enough that they could overflow the 32k memory limit,
	 * so allow those block groups to still be allowed to have a bitmap
	 * entry.
	 */
	if (((BITS_PER_BITMAP * ctl->unit) >> 1) > block_group->length)
		return false;

	return true;
}

static const struct btrfs_free_space_op free_space_op = {
	.recalc_thresholds	= recalculate_thresholds,
	.use_bitmap		= use_bitmap,
};

static int insert_into_bitmap(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info)
{
	struct btrfs_free_space *bitmap_info;
	struct btrfs_block_group *block_group = NULL;
	int added = 0;
	u64 bytes, offset, bytes_added;
	enum btrfs_trim_state trim_state;
	int ret;

	bytes = info->bytes;
	offset = info->offset;
	trim_state = info->trim_state;

	if (!ctl->op->use_bitmap(ctl, info))
		return 0;

	if (ctl->op == &free_space_op)
		block_group = ctl->private;
again:
	/*
	 * Since we link bitmaps right into the cluster we need to see if we
	 * have a cluster here, and if so and it has our bitmap we need to add
	 * the free space to that bitmap.
	 */
	if (block_group && !list_empty(&block_group->cluster_list)) {
		struct btrfs_free_cluster *cluster;
		struct rb_node *node;
		struct btrfs_free_space *entry;

		cluster = list_entry(block_group->cluster_list.next,
				     struct btrfs_free_cluster,
				     block_group_list);
		spin_lock(&cluster->lock);
		node = rb_first(&cluster->root);
		if (!node) {
			spin_unlock(&cluster->lock);
			goto no_cluster_bitmap;
		}

		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		if (!entry->bitmap) {
			spin_unlock(&cluster->lock);
			goto no_cluster_bitmap;
		}

		if (entry->offset == offset_to_bitmap(ctl, offset)) {
			bytes_added = add_bytes_to_bitmap(ctl, entry, offset,
							  bytes, trim_state);
			bytes -= bytes_added;
			offset += bytes_added;
		}
		spin_unlock(&cluster->lock);
		if (!bytes) {
			ret = 1;
			goto out;
		}
	}

no_cluster_bitmap:
	bitmap_info = tree_search_offset(ctl, offset_to_bitmap(ctl, offset),
					 1, 0);
	if (!bitmap_info) {
		ASSERT(added == 0);
		goto new_bitmap;
	}

	bytes_added = add_bytes_to_bitmap(ctl, bitmap_info, offset, bytes,
					  trim_state);
	bytes -= bytes_added;
	offset += bytes_added;
	added = 0;

	if (!bytes) {
		ret = 1;
		goto out;
	} else
		goto again;

new_bitmap:
	if (info && info->bitmap) {
		add_new_bitmap(ctl, info, offset);
		added = 1;
		info = NULL;
		goto again;
	} else {
		spin_unlock(&ctl->tree_lock);

		/* no pre-allocated info, allocate a new one */
		if (!info) {
			info = kmem_cache_zalloc(btrfs_free_space_cachep,
						 GFP_NOFS);
			if (!info) {
				spin_lock(&ctl->tree_lock);
				ret = -ENOMEM;
				goto out;
			}
		}

		/* allocate the bitmap */
		info->bitmap = kmem_cache_zalloc(btrfs_free_space_bitmap_cachep,
						 GFP_NOFS);
		info->trim_state = BTRFS_TRIM_STATE_TRIMMED;
		spin_lock(&ctl->tree_lock);
		if (!info->bitmap) {
			ret = -ENOMEM;
			goto out;
		}
		goto again;
	}

out:
	if (info) {
		if (info->bitmap)
			kmem_cache_free(btrfs_free_space_bitmap_cachep,
					info->bitmap);
		kmem_cache_free(btrfs_free_space_cachep, info);
	}

	return ret;
}

/*
 * Free space merging rules:
 *  1) Merge trimmed areas together
 *  2) Let untrimmed areas coalesce with trimmed areas
 *  3) Always pull neighboring regions from bitmaps
 *
 * The above rules are for when we merge free space based on btrfs_trim_state.
 * Rules 2 and 3 are subtle because they are suboptimal, but are done for the
 * same reason: to promote larger extent regions which makes life easier for
 * find_free_extent().  Rule 2 enables coalescing based on the common path
 * being returning free space from btrfs_finish_extent_commit().  So when free
 * space is trimmed, it will prevent aggregating trimmed new region and
 * untrimmed regions in the rb_tree.  Rule 3 is purely to obtain larger extents
 * and provide find_free_extent() with the largest extents possible hoping for
 * the reuse path.
 */
static bool try_merge_free_space(struct btrfs_free_space_ctl *ctl,
			  struct btrfs_free_space *info, bool update_stat)
{
	struct btrfs_free_space *left_info = NULL;
	struct btrfs_free_space *right_info;
	bool merged = false;
	u64 offset = info->offset;
	u64 bytes = info->bytes;
	const bool is_trimmed = btrfs_free_space_trimmed(info);

	/*
	 * first we want to see if there is free space adjacent to the range we
	 * are adding, if there is remove that struct and add a new one to
	 * cover the entire range
	 */
	right_info = tree_search_offset(ctl, offset + bytes, 0, 0);
	if (right_info && rb_prev(&right_info->offset_index))
		left_info = rb_entry(rb_prev(&right_info->offset_index),
				     struct btrfs_free_space, offset_index);
	else if (!right_info)
		left_info = tree_search_offset(ctl, offset - 1, 0, 0);

	/* See try_merge_free_space() comment. */
	if (right_info && !right_info->bitmap &&
	    (!is_trimmed || btrfs_free_space_trimmed(right_info))) {
		if (update_stat)
			unlink_free_space(ctl, right_info);
		else
			__unlink_free_space(ctl, right_info);
		info->bytes += right_info->bytes;
		kmem_cache_free(btrfs_free_space_cachep, right_info);
		merged = true;
	}

	/* See try_merge_free_space() comment. */
	if (left_info && !left_info->bitmap &&
	    left_info->offset + left_info->bytes == offset &&
	    (!is_trimmed || btrfs_free_space_trimmed(left_info))) {
		if (update_stat)
			unlink_free_space(ctl, left_info);
		else
			__unlink_free_space(ctl, left_info);
		info->offset = left_info->offset;
		info->bytes += left_info->bytes;
		kmem_cache_free(btrfs_free_space_cachep, left_info);
		merged = true;
	}

	return merged;
}

static bool steal_from_bitmap_to_end(struct btrfs_free_space_ctl *ctl,
				     struct btrfs_free_space *info,
				     bool update_stat)
{
	struct btrfs_free_space *bitmap;
	unsigned long i;
	unsigned long j;
	const u64 end = info->offset + info->bytes;
	const u64 bitmap_offset = offset_to_bitmap(ctl, end);
	u64 bytes;

	bitmap = tree_search_offset(ctl, bitmap_offset, 1, 0);
	if (!bitmap)
		return false;

	i = offset_to_bit(bitmap->offset, ctl->unit, end);
	j = find_next_zero_bit(bitmap->bitmap, BITS_PER_BITMAP, i);
	if (j == i)
		return false;
	bytes = (j - i) * ctl->unit;
	info->bytes += bytes;

	/* See try_merge_free_space() comment. */
	if (!btrfs_free_space_trimmed(bitmap))
		info->trim_state = BTRFS_TRIM_STATE_UNTRIMMED;

	if (update_stat)
		bitmap_clear_bits(ctl, bitmap, end, bytes);
	else
		__bitmap_clear_bits(ctl, bitmap, end, bytes);

	if (!bitmap->bytes)
		free_bitmap(ctl, bitmap);

	return true;
}

static bool steal_from_bitmap_to_front(struct btrfs_free_space_ctl *ctl,
				       struct btrfs_free_space *info,
				       bool update_stat)
{
	struct btrfs_free_space *bitmap;
	u64 bitmap_offset;
	unsigned long i;
	unsigned long j;
	unsigned long prev_j;
	u64 bytes;

	bitmap_offset = offset_to_bitmap(ctl, info->offset);
	/* If we're on a boundary, try the previous logical bitmap. */
	if (bitmap_offset == info->offset) {
		if (info->offset == 0)
			return false;
		bitmap_offset = offset_to_bitmap(ctl, info->offset - 1);
	}

	bitmap = tree_search_offset(ctl, bitmap_offset, 1, 0);
	if (!bitmap)
		return false;

	i = offset_to_bit(bitmap->offset, ctl->unit, info->offset) - 1;
	j = 0;
	prev_j = (unsigned long)-1;
	for_each_clear_bit_from(j, bitmap->bitmap, BITS_PER_BITMAP) {
		if (j > i)
			break;
		prev_j = j;
	}
	if (prev_j == i)
		return false;

	if (prev_j == (unsigned long)-1)
		bytes = (i + 1) * ctl->unit;
	else
		bytes = (i - prev_j) * ctl->unit;

	info->offset -= bytes;
	info->bytes += bytes;

	/* See try_merge_free_space() comment. */
	if (!btrfs_free_space_trimmed(bitmap))
		info->trim_state = BTRFS_TRIM_STATE_UNTRIMMED;

	if (update_stat)
		bitmap_clear_bits(ctl, bitmap, info->offset, bytes);
	else
		__bitmap_clear_bits(ctl, bitmap, info->offset, bytes);

	if (!bitmap->bytes)
		free_bitmap(ctl, bitmap);

	return true;
}

/*
 * We prefer always to allocate from extent entries, both for clustered and
 * non-clustered allocation requests. So when attempting to add a new extent
 * entry, try to see if there's adjacent free space in bitmap entries, and if
 * there is, migrate that space from the bitmaps to the extent.
 * Like this we get better chances of satisfying space allocation requests
 * because we attempt to satisfy them based on a single cache entry, and never
 * on 2 or more entries - even if the entries represent a contiguous free space
 * region (e.g. 1 extent entry + 1 bitmap entry starting where the extent entry
 * ends).
 */
static void steal_from_bitmap(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info,
			      bool update_stat)
{
	/*
	 * Only work with disconnected entries, as we can change their offset,
	 * and must be extent entries.
	 */
	ASSERT(!info->bitmap);
	ASSERT(RB_EMPTY_NODE(&info->offset_index));

	if (ctl->total_bitmaps > 0) {
		bool stole_end;
		bool stole_front = false;

		stole_end = steal_from_bitmap_to_end(ctl, info, update_stat);
		if (ctl->total_bitmaps > 0)
			stole_front = steal_from_bitmap_to_front(ctl, info,
								 update_stat);

		if (stole_end || stole_front)
			try_merge_free_space(ctl, info, update_stat);
	}
}

int __btrfs_add_free_space(struct btrfs_fs_info *fs_info,
			   struct btrfs_free_space_ctl *ctl,
			   u64 offset, u64 bytes,
			   enum btrfs_trim_state trim_state)
{
	struct btrfs_block_group *block_group = ctl->private;
	struct btrfs_free_space *info;
	int ret = 0;
	u64 filter_bytes = bytes;

	info = kmem_cache_zalloc(btrfs_free_space_cachep, GFP_NOFS);
	if (!info)
		return -ENOMEM;

	info->offset = offset;
	info->bytes = bytes;
	info->trim_state = trim_state;
	RB_CLEAR_NODE(&info->offset_index);

	spin_lock(&ctl->tree_lock);

	if (try_merge_free_space(ctl, info, true))
		goto link;

	/*
	 * There was no extent directly to the left or right of this new
	 * extent then we know we're going to have to allocate a new extent, so
	 * before we do that see if we need to drop this into a bitmap
	 */
	ret = insert_into_bitmap(ctl, info);
	if (ret < 0) {
		goto out;
	} else if (ret) {
		ret = 0;
		goto out;
	}
link:
	/*
	 * Only steal free space from adjacent bitmaps if we're sure we're not
	 * going to add the new free space to existing bitmap entries - because
	 * that would mean unnecessary work that would be reverted. Therefore
	 * attempt to steal space from bitmaps if we're adding an extent entry.
	 */
	steal_from_bitmap(ctl, info, true);

	filter_bytes = max(filter_bytes, info->bytes);

	ret = link_free_space(ctl, info);
	if (ret)
		kmem_cache_free(btrfs_free_space_cachep, info);
out:
	btrfs_discard_update_discardable(block_group, ctl);
	spin_unlock(&ctl->tree_lock);

	if (ret) {
		btrfs_crit(fs_info, "unable to add free space :%d", ret);
		ASSERT(ret != -EEXIST);
	}

	if (trim_state != BTRFS_TRIM_STATE_TRIMMED) {
		btrfs_discard_check_filter(block_group, filter_bytes);
		btrfs_discard_queue_work(&fs_info->discard_ctl, block_group);
	}

	return ret;
}

int btrfs_add_free_space(struct btrfs_block_group *block_group,
			 u64 bytenr, u64 size)
{
	enum btrfs_trim_state trim_state = BTRFS_TRIM_STATE_UNTRIMMED;

	if (btrfs_test_opt(block_group->fs_info, DISCARD_SYNC))
		trim_state = BTRFS_TRIM_STATE_TRIMMED;

	return __btrfs_add_free_space(block_group->fs_info,
				      block_group->free_space_ctl,
				      bytenr, size, trim_state);
}

/*
 * This is a subtle distinction because when adding free space back in general,
 * we want it to be added as untrimmed for async. But in the case where we add
 * it on loading of a block group, we want to consider it trimmed.
 */
int btrfs_add_free_space_async_trimmed(struct btrfs_block_group *block_group,
				       u64 bytenr, u64 size)
{
	enum btrfs_trim_state trim_state = BTRFS_TRIM_STATE_UNTRIMMED;

	if (btrfs_test_opt(block_group->fs_info, DISCARD_SYNC) ||
	    btrfs_test_opt(block_group->fs_info, DISCARD_ASYNC))
		trim_state = BTRFS_TRIM_STATE_TRIMMED;

	return __btrfs_add_free_space(block_group->fs_info,
				      block_group->free_space_ctl,
				      bytenr, size, trim_state);
}

int btrfs_remove_free_space(struct btrfs_block_group *block_group,
			    u64 offset, u64 bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *info;
	int ret;
	bool re_search = false;

	spin_lock(&ctl->tree_lock);

again:
	ret = 0;
	if (!bytes)
		goto out_lock;

	info = tree_search_offset(ctl, offset, 0, 0);
	if (!info) {
		/*
		 * oops didn't find an extent that matched the space we wanted
		 * to remove, look for a bitmap instead
		 */
		info = tree_search_offset(ctl, offset_to_bitmap(ctl, offset),
					  1, 0);
		if (!info) {
			/*
			 * If we found a partial bit of our free space in a
			 * bitmap but then couldn't find the other part this may
			 * be a problem, so WARN about it.
			 */
			WARN_ON(re_search);
			goto out_lock;
		}
	}

	re_search = false;
	if (!info->bitmap) {
		unlink_free_space(ctl, info);
		if (offset == info->offset) {
			u64 to_free = min(bytes, info->bytes);

			info->bytes -= to_free;
			info->offset += to_free;
			if (info->bytes) {
				ret = link_free_space(ctl, info);
				WARN_ON(ret);
			} else {
				kmem_cache_free(btrfs_free_space_cachep, info);
			}

			offset += to_free;
			bytes -= to_free;
			goto again;
		} else {
			u64 old_end = info->bytes + info->offset;

			info->bytes = offset - info->offset;
			ret = link_free_space(ctl, info);
			WARN_ON(ret);
			if (ret)
				goto out_lock;

			/* Not enough bytes in this entry to satisfy us */
			if (old_end < offset + bytes) {
				bytes -= old_end - offset;
				offset = old_end;
				goto again;
			} else if (old_end == offset + bytes) {
				/* all done */
				goto out_lock;
			}
			spin_unlock(&ctl->tree_lock);

			ret = __btrfs_add_free_space(block_group->fs_info, ctl,
						     offset + bytes,
						     old_end - (offset + bytes),
						     info->trim_state);
			WARN_ON(ret);
			goto out;
		}
	}

	ret = remove_from_bitmap(ctl, info, &offset, &bytes);
	if (ret == -EAGAIN) {
		re_search = true;
		goto again;
	}
out_lock:
	btrfs_discard_update_discardable(block_group, ctl);
	spin_unlock(&ctl->tree_lock);
out:
	return ret;
}

void btrfs_dump_free_space(struct btrfs_block_group *block_group,
			   u64 bytes)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *info;
	struct rb_node *n;
	int count = 0;

	spin_lock(&ctl->tree_lock);
	for (n = rb_first(&ctl->free_space_offset); n; n = rb_next(n)) {
		info = rb_entry(n, struct btrfs_free_space, offset_index);
		if (info->bytes >= bytes && !block_group->ro)
			count++;
		btrfs_crit(fs_info, "entry offset %llu, bytes %llu, bitmap %s",
			   info->offset, info->bytes,
		       (info->bitmap) ? "yes" : "no");
	}
	spin_unlock(&ctl->tree_lock);
	btrfs_info(fs_info, "block group has cluster?: %s",
	       list_empty(&block_group->cluster_list) ? "no" : "yes");
	btrfs_info(fs_info,
		   "%d blocks of free space at or bigger than bytes is", count);
}

void btrfs_init_free_space_ctl(struct btrfs_block_group *block_group)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;

	spin_lock_init(&ctl->tree_lock);
	ctl->unit = fs_info->sectorsize;
	ctl->start = block_group->start;
	ctl->private = block_group;
	ctl->op = &free_space_op;
	INIT_LIST_HEAD(&ctl->trimming_ranges);
	mutex_init(&ctl->cache_writeout_mutex);

	/*
	 * we only want to have 32k of ram per block group for keeping
	 * track of free space, and if we pass 1/2 of that we want to
	 * start converting things over to using bitmaps
	 */
	ctl->extents_thresh = (SZ_32K / 2) / sizeof(struct btrfs_free_space);
}

/*
 * for a given cluster, put all of its extents back into the free
 * space cache.  If the block group passed doesn't match the block group
 * pointed to by the cluster, someone else raced in and freed the
 * cluster already.  In that case, we just return without changing anything
 */
static void __btrfs_return_cluster_to_free_space(
			     struct btrfs_block_group *block_group,
			     struct btrfs_free_cluster *cluster)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry;
	struct rb_node *node;

	spin_lock(&cluster->lock);
	if (cluster->block_group != block_group)
		goto out;

	cluster->block_group = NULL;
	cluster->window_start = 0;
	list_del_init(&cluster->block_group_list);

	node = rb_first(&cluster->root);
	while (node) {
		bool bitmap;

		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		node = rb_next(&entry->offset_index);
		rb_erase(&entry->offset_index, &cluster->root);
		RB_CLEAR_NODE(&entry->offset_index);

		bitmap = (entry->bitmap != NULL);
		if (!bitmap) {
			/* Merging treats extents as if they were new */
			if (!btrfs_free_space_trimmed(entry)) {
				ctl->discardable_extents[BTRFS_STAT_CURR]--;
				ctl->discardable_bytes[BTRFS_STAT_CURR] -=
					entry->bytes;
			}

			try_merge_free_space(ctl, entry, false);
			steal_from_bitmap(ctl, entry, false);

			/* As we insert directly, update these statistics */
			if (!btrfs_free_space_trimmed(entry)) {
				ctl->discardable_extents[BTRFS_STAT_CURR]++;
				ctl->discardable_bytes[BTRFS_STAT_CURR] +=
					entry->bytes;
			}
		}
		tree_insert_offset(&ctl->free_space_offset,
				   entry->offset, &entry->offset_index, bitmap);
	}
	cluster->root = RB_ROOT;

out:
	spin_unlock(&cluster->lock);
	btrfs_put_block_group(block_group);
}

static void __btrfs_remove_free_space_cache_locked(
				struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_free_space *info;
	struct rb_node *node;

	while ((node = rb_last(&ctl->free_space_offset)) != NULL) {
		info = rb_entry(node, struct btrfs_free_space, offset_index);
		if (!info->bitmap) {
			unlink_free_space(ctl, info);
			kmem_cache_free(btrfs_free_space_cachep, info);
		} else {
			free_bitmap(ctl, info);
		}

		cond_resched_lock(&ctl->tree_lock);
	}
}

void __btrfs_remove_free_space_cache(struct btrfs_free_space_ctl *ctl)
{
	spin_lock(&ctl->tree_lock);
	__btrfs_remove_free_space_cache_locked(ctl);
	if (ctl->private)
		btrfs_discard_update_discardable(ctl->private, ctl);
	spin_unlock(&ctl->tree_lock);
}

void btrfs_remove_free_space_cache(struct btrfs_block_group *block_group)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_cluster *cluster;
	struct list_head *head;

	spin_lock(&ctl->tree_lock);
	while ((head = block_group->cluster_list.next) !=
	       &block_group->cluster_list) {
		cluster = list_entry(head, struct btrfs_free_cluster,
				     block_group_list);

		WARN_ON(cluster->block_group != block_group);
		__btrfs_return_cluster_to_free_space(block_group, cluster);

		cond_resched_lock(&ctl->tree_lock);
	}
	__btrfs_remove_free_space_cache_locked(ctl);
	btrfs_discard_update_discardable(block_group, ctl);
	spin_unlock(&ctl->tree_lock);

}

/**
 * btrfs_is_free_space_trimmed - see if everything is trimmed
 * @block_group: block_group of interest
 *
 * Walk @block_group's free space rb_tree to determine if everything is trimmed.
 */
bool btrfs_is_free_space_trimmed(struct btrfs_block_group *block_group)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *info;
	struct rb_node *node;
	bool ret = true;

	spin_lock(&ctl->tree_lock);
	node = rb_first(&ctl->free_space_offset);

	while (node) {
		info = rb_entry(node, struct btrfs_free_space, offset_index);

		if (!btrfs_free_space_trimmed(info)) {
			ret = false;
			break;
		}

		node = rb_next(node);
	}

	spin_unlock(&ctl->tree_lock);
	return ret;
}

u64 btrfs_find_space_for_alloc(struct btrfs_block_group *block_group,
			       u64 offset, u64 bytes, u64 empty_size,
			       u64 *max_extent_size)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_discard_ctl *discard_ctl =
					&block_group->fs_info->discard_ctl;
	struct btrfs_free_space *entry = NULL;
	u64 bytes_search = bytes + empty_size;
	u64 ret = 0;
	u64 align_gap = 0;
	u64 align_gap_len = 0;
	enum btrfs_trim_state align_gap_trim_state = BTRFS_TRIM_STATE_UNTRIMMED;

	spin_lock(&ctl->tree_lock);
	entry = find_free_space(ctl, &offset, &bytes_search,
				block_group->full_stripe_len, max_extent_size);
	if (!entry)
		goto out;

	ret = offset;
	if (entry->bitmap) {
		bitmap_clear_bits(ctl, entry, offset, bytes);

		if (!btrfs_free_space_trimmed(entry))
			atomic64_add(bytes, &discard_ctl->discard_bytes_saved);

		if (!entry->bytes)
			free_bitmap(ctl, entry);
	} else {
		unlink_free_space(ctl, entry);
		align_gap_len = offset - entry->offset;
		align_gap = entry->offset;
		align_gap_trim_state = entry->trim_state;

		if (!btrfs_free_space_trimmed(entry))
			atomic64_add(bytes, &discard_ctl->discard_bytes_saved);

		entry->offset = offset + bytes;
		WARN_ON(entry->bytes < bytes + align_gap_len);

		entry->bytes -= bytes + align_gap_len;
		if (!entry->bytes)
			kmem_cache_free(btrfs_free_space_cachep, entry);
		else
			link_free_space(ctl, entry);
	}
out:
	btrfs_discard_update_discardable(block_group, ctl);
	spin_unlock(&ctl->tree_lock);

	if (align_gap_len)
		__btrfs_add_free_space(block_group->fs_info, ctl,
				       align_gap, align_gap_len,
				       align_gap_trim_state);
	return ret;
}

/*
 * given a cluster, put all of its extents back into the free space
 * cache.  If a block group is passed, this function will only free
 * a cluster that belongs to the passed block group.
 *
 * Otherwise, it'll get a reference on the block group pointed to by the
 * cluster and remove the cluster from it.
 */
void btrfs_return_cluster_to_free_space(
			       struct btrfs_block_group *block_group,
			       struct btrfs_free_cluster *cluster)
{
	struct btrfs_free_space_ctl *ctl;

	/* first, get a safe pointer to the block group */
	spin_lock(&cluster->lock);
	if (!block_group) {
		block_group = cluster->block_group;
		if (!block_group) {
			spin_unlock(&cluster->lock);
			return;
		}
	} else if (cluster->block_group != block_group) {
		/* someone else has already freed it don't redo their work */
		spin_unlock(&cluster->lock);
		return;
	}
	btrfs_get_block_group(block_group);
	spin_unlock(&cluster->lock);

	ctl = block_group->free_space_ctl;

	/* now return any extents the cluster had on it */
	spin_lock(&ctl->tree_lock);
	__btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&ctl->tree_lock);

	btrfs_discard_queue_work(&block_group->fs_info->discard_ctl, block_group);

	/* finally drop our ref */
	btrfs_put_block_group(block_group);
}

static u64 btrfs_alloc_from_bitmap(struct btrfs_block_group *block_group,
				   struct btrfs_free_cluster *cluster,
				   struct btrfs_free_space *entry,
				   u64 bytes, u64 min_start,
				   u64 *max_extent_size)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	int err;
	u64 search_start = cluster->window_start;
	u64 search_bytes = bytes;
	u64 ret = 0;

	search_start = min_start;
	search_bytes = bytes;

	err = search_bitmap(ctl, entry, &search_start, &search_bytes, true);
	if (err) {
		*max_extent_size = max(get_max_extent_size(entry),
				       *max_extent_size);
		return 0;
	}

	ret = search_start;
	__bitmap_clear_bits(ctl, entry, ret, bytes);

	return ret;
}

/*
 * given a cluster, try to allocate 'bytes' from it, returns 0
 * if it couldn't find anything suitably large, or a logical disk offset
 * if things worked out
 */
u64 btrfs_alloc_from_cluster(struct btrfs_block_group *block_group,
			     struct btrfs_free_cluster *cluster, u64 bytes,
			     u64 min_start, u64 *max_extent_size)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_discard_ctl *discard_ctl =
					&block_group->fs_info->discard_ctl;
	struct btrfs_free_space *entry = NULL;
	struct rb_node *node;
	u64 ret = 0;

	spin_lock(&cluster->lock);
	if (bytes > cluster->max_size)
		goto out;

	if (cluster->block_group != block_group)
		goto out;

	node = rb_first(&cluster->root);
	if (!node)
		goto out;

	entry = rb_entry(node, struct btrfs_free_space, offset_index);
	while (1) {
		if (entry->bytes < bytes)
			*max_extent_size = max(get_max_extent_size(entry),
					       *max_extent_size);

		if (entry->bytes < bytes ||
		    (!entry->bitmap && entry->offset < min_start)) {
			node = rb_next(&entry->offset_index);
			if (!node)
				break;
			entry = rb_entry(node, struct btrfs_free_space,
					 offset_index);
			continue;
		}

		if (entry->bitmap) {
			ret = btrfs_alloc_from_bitmap(block_group,
						      cluster, entry, bytes,
						      cluster->window_start,
						      max_extent_size);
			if (ret == 0) {
				node = rb_next(&entry->offset_index);
				if (!node)
					break;
				entry = rb_entry(node, struct btrfs_free_space,
						 offset_index);
				continue;
			}
			cluster->window_start += bytes;
		} else {
			ret = entry->offset;

			entry->offset += bytes;
			entry->bytes -= bytes;
		}

		if (entry->bytes == 0)
			rb_erase(&entry->offset_index, &cluster->root);
		break;
	}
out:
	spin_unlock(&cluster->lock);

	if (!ret)
		return 0;

	spin_lock(&ctl->tree_lock);

	if (!btrfs_free_space_trimmed(entry))
		atomic64_add(bytes, &discard_ctl->discard_bytes_saved);

	ctl->free_space -= bytes;
	if (!entry->bitmap && !btrfs_free_space_trimmed(entry))
		ctl->discardable_bytes[BTRFS_STAT_CURR] -= bytes;
	if (entry->bytes == 0) {
		ctl->free_extents--;
		if (entry->bitmap) {
			kmem_cache_free(btrfs_free_space_bitmap_cachep,
					entry->bitmap);
			ctl->total_bitmaps--;
			ctl->op->recalc_thresholds(ctl);
		} else if (!btrfs_free_space_trimmed(entry)) {
			ctl->discardable_extents[BTRFS_STAT_CURR]--;
		}
		kmem_cache_free(btrfs_free_space_cachep, entry);
	}

	spin_unlock(&ctl->tree_lock);

	return ret;
}

static int btrfs_bitmap_cluster(struct btrfs_block_group *block_group,
				struct btrfs_free_space *entry,
				struct btrfs_free_cluster *cluster,
				u64 offset, u64 bytes,
				u64 cont1_bytes, u64 min_bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	unsigned long next_zero;
	unsigned long i;
	unsigned long want_bits;
	unsigned long min_bits;
	unsigned long found_bits;
	unsigned long max_bits = 0;
	unsigned long start = 0;
	unsigned long total_found = 0;
	int ret;

	i = offset_to_bit(entry->offset, ctl->unit,
			  max_t(u64, offset, entry->offset));
	want_bits = bytes_to_bits(bytes, ctl->unit);
	min_bits = bytes_to_bits(min_bytes, ctl->unit);

	/*
	 * Don't bother looking for a cluster in this bitmap if it's heavily
	 * fragmented.
	 */
	if (entry->max_extent_size &&
	    entry->max_extent_size < cont1_bytes)
		return -ENOSPC;
again:
	found_bits = 0;
	for_each_set_bit_from(i, entry->bitmap, BITS_PER_BITMAP) {
		next_zero = find_next_zero_bit(entry->bitmap,
					       BITS_PER_BITMAP, i);
		if (next_zero - i >= min_bits) {
			found_bits = next_zero - i;
			if (found_bits > max_bits)
				max_bits = found_bits;
			break;
		}
		if (next_zero - i > max_bits)
			max_bits = next_zero - i;
		i = next_zero;
	}

	if (!found_bits) {
		entry->max_extent_size = (u64)max_bits * ctl->unit;
		return -ENOSPC;
	}

	if (!total_found) {
		start = i;
		cluster->max_size = 0;
	}

	total_found += found_bits;

	if (cluster->max_size < found_bits * ctl->unit)
		cluster->max_size = found_bits * ctl->unit;

	if (total_found < want_bits || cluster->max_size < cont1_bytes) {
		i = next_zero + 1;
		goto again;
	}

	cluster->window_start = start * ctl->unit + entry->offset;
	rb_erase(&entry->offset_index, &ctl->free_space_offset);
	ret = tree_insert_offset(&cluster->root, entry->offset,
				 &entry->offset_index, 1);
	ASSERT(!ret); /* -EEXIST; Logic error */

	trace_btrfs_setup_cluster(block_group, cluster,
				  total_found * ctl->unit, 1);
	return 0;
}

/*
 * This searches the block group for just extents to fill the cluster with.
 * Try to find a cluster with at least bytes total bytes, at least one
 * extent of cont1_bytes, and other clusters of at least min_bytes.
 */
static noinline int
setup_cluster_no_bitmap(struct btrfs_block_group *block_group,
			struct btrfs_free_cluster *cluster,
			struct list_head *bitmaps, u64 offset, u64 bytes,
			u64 cont1_bytes, u64 min_bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *first = NULL;
	struct btrfs_free_space *entry = NULL;
	struct btrfs_free_space *last;
	struct rb_node *node;
	u64 window_free;
	u64 max_extent;
	u64 total_size = 0;

	entry = tree_search_offset(ctl, offset, 0, 1);
	if (!entry)
		return -ENOSPC;

	/*
	 * We don't want bitmaps, so just move along until we find a normal
	 * extent entry.
	 */
	while (entry->bitmap || entry->bytes < min_bytes) {
		if (entry->bitmap && list_empty(&entry->list))
			list_add_tail(&entry->list, bitmaps);
		node = rb_next(&entry->offset_index);
		if (!node)
			return -ENOSPC;
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
	}

	window_free = entry->bytes;
	max_extent = entry->bytes;
	first = entry;
	last = entry;

	for (node = rb_next(&entry->offset_index); node;
	     node = rb_next(&entry->offset_index)) {
		entry = rb_entry(node, struct btrfs_free_space, offset_index);

		if (entry->bitmap) {
			if (list_empty(&entry->list))
				list_add_tail(&entry->list, bitmaps);
			continue;
		}

		if (entry->bytes < min_bytes)
			continue;

		last = entry;
		window_free += entry->bytes;
		if (entry->bytes > max_extent)
			max_extent = entry->bytes;
	}

	if (window_free < bytes || max_extent < cont1_bytes)
		return -ENOSPC;

	cluster->window_start = first->offset;

	node = &first->offset_index;

	/*
	 * now we've found our entries, pull them out of the free space
	 * cache and put them into the cluster rbtree
	 */
	do {
		int ret;

		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		node = rb_next(&entry->offset_index);
		if (entry->bitmap || entry->bytes < min_bytes)
			continue;

		rb_erase(&entry->offset_index, &ctl->free_space_offset);
		ret = tree_insert_offset(&cluster->root, entry->offset,
					 &entry->offset_index, 0);
		total_size += entry->bytes;
		ASSERT(!ret); /* -EEXIST; Logic error */
	} while (node && entry != last);

	cluster->max_size = max_extent;
	trace_btrfs_setup_cluster(block_group, cluster, total_size, 0);
	return 0;
}

/*
 * This specifically looks for bitmaps that may work in the cluster, we assume
 * that we have already failed to find extents that will work.
 */
static noinline int
setup_cluster_bitmap(struct btrfs_block_group *block_group,
		     struct btrfs_free_cluster *cluster,
		     struct list_head *bitmaps, u64 offset, u64 bytes,
		     u64 cont1_bytes, u64 min_bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry = NULL;
	int ret = -ENOSPC;
	u64 bitmap_offset = offset_to_bitmap(ctl, offset);

	if (ctl->total_bitmaps == 0)
		return -ENOSPC;

	/*
	 * The bitmap that covers offset won't be in the list unless offset
	 * is just its start offset.
	 */
	if (!list_empty(bitmaps))
		entry = list_first_entry(bitmaps, struct btrfs_free_space, list);

	if (!entry || entry->offset != bitmap_offset) {
		entry = tree_search_offset(ctl, bitmap_offset, 1, 0);
		if (entry && list_empty(&entry->list))
			list_add(&entry->list, bitmaps);
	}

	list_for_each_entry(entry, bitmaps, list) {
		if (entry->bytes < bytes)
			continue;
		ret = btrfs_bitmap_cluster(block_group, entry, cluster, offset,
					   bytes, cont1_bytes, min_bytes);
		if (!ret)
			return 0;
	}

	/*
	 * The bitmaps list has all the bitmaps that record free space
	 * starting after offset, so no more search is required.
	 */
	return -ENOSPC;
}

/*
 * here we try to find a cluster of blocks in a block group.  The goal
 * is to find at least bytes+empty_size.
 * We might not find them all in one contiguous area.
 *
 * returns zero and sets up cluster if things worked out, otherwise
 * it returns -enospc
 */
int btrfs_find_space_cluster(struct btrfs_block_group *block_group,
			     struct btrfs_free_cluster *cluster,
			     u64 offset, u64 bytes, u64 empty_size)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry, *tmp;
	LIST_HEAD(bitmaps);
	u64 min_bytes;
	u64 cont1_bytes;
	int ret;

	/*
	 * Choose the minimum extent size we'll require for this
	 * cluster.  For SSD_SPREAD, don't allow any fragmentation.
	 * For metadata, allow allocates with smaller extents.  For
	 * data, keep it dense.
	 */
	if (btrfs_test_opt(fs_info, SSD_SPREAD)) {
		cont1_bytes = min_bytes = bytes + empty_size;
	} else if (block_group->flags & BTRFS_BLOCK_GROUP_METADATA) {
		cont1_bytes = bytes;
		min_bytes = fs_info->sectorsize;
	} else {
		cont1_bytes = max(bytes, (bytes + empty_size) >> 2);
		min_bytes = fs_info->sectorsize;
	}

	spin_lock(&ctl->tree_lock);

	/*
	 * If we know we don't have enough space to make a cluster don't even
	 * bother doing all the work to try and find one.
	 */
	if (ctl->free_space < bytes) {
		spin_unlock(&ctl->tree_lock);
		return -ENOSPC;
	}

	spin_lock(&cluster->lock);

	/* someone already found a cluster, hooray */
	if (cluster->block_group) {
		ret = 0;
		goto out;
	}

	trace_btrfs_find_cluster(block_group, offset, bytes, empty_size,
				 min_bytes);

	ret = setup_cluster_no_bitmap(block_group, cluster, &bitmaps, offset,
				      bytes + empty_size,
				      cont1_bytes, min_bytes);
	if (ret)
		ret = setup_cluster_bitmap(block_group, cluster, &bitmaps,
					   offset, bytes + empty_size,
					   cont1_bytes, min_bytes);

	/* Clear our temporary list */
	list_for_each_entry_safe(entry, tmp, &bitmaps, list)
		list_del_init(&entry->list);

	if (!ret) {
		btrfs_get_block_group(block_group);
		list_add_tail(&cluster->block_group_list,
			      &block_group->cluster_list);
		cluster->block_group = block_group;
	} else {
		trace_btrfs_failed_cluster_setup(block_group);
	}
out:
	spin_unlock(&cluster->lock);
	spin_unlock(&ctl->tree_lock);

	return ret;
}

/*
 * simple code to zero out a cluster
 */
void btrfs_init_free_cluster(struct btrfs_free_cluster *cluster)
{
	spin_lock_init(&cluster->lock);
	spin_lock_init(&cluster->refill_lock);
	cluster->root = RB_ROOT;
	cluster->max_size = 0;
	cluster->fragmented = false;
	INIT_LIST_HEAD(&cluster->block_group_list);
	cluster->block_group = NULL;
}

static int do_trimming(struct btrfs_block_group *block_group,
		       u64 *total_trimmed, u64 start, u64 bytes,
		       u64 reserved_start, u64 reserved_bytes,
		       enum btrfs_trim_state reserved_trim_state,
		       struct btrfs_trim_range *trim_entry)
{
	struct btrfs_space_info *space_info = block_group->space_info;
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	int ret;
	int update = 0;
	const u64 end = start + bytes;
	const u64 reserved_end = reserved_start + reserved_bytes;
	enum btrfs_trim_state trim_state = BTRFS_TRIM_STATE_UNTRIMMED;
	u64 trimmed = 0;

	spin_lock(&space_info->lock);
	spin_lock(&block_group->lock);
	if (!block_group->ro) {
		block_group->reserved += reserved_bytes;
		space_info->bytes_reserved += reserved_bytes;
		update = 1;
	}
	spin_unlock(&block_group->lock);
	spin_unlock(&space_info->lock);

	ret = btrfs_discard_extent(fs_info, start, bytes, &trimmed);
	if (!ret) {
		*total_trimmed += trimmed;
		trim_state = BTRFS_TRIM_STATE_TRIMMED;
	}

	mutex_lock(&ctl->cache_writeout_mutex);
	if (reserved_start < start)
		__btrfs_add_free_space(fs_info, ctl, reserved_start,
				       start - reserved_start,
				       reserved_trim_state);
	if (start + bytes < reserved_start + reserved_bytes)
		__btrfs_add_free_space(fs_info, ctl, end, reserved_end - end,
				       reserved_trim_state);
	__btrfs_add_free_space(fs_info, ctl, start, bytes, trim_state);
	list_del(&trim_entry->list);
	mutex_unlock(&ctl->cache_writeout_mutex);

	if (update) {
		spin_lock(&space_info->lock);
		spin_lock(&block_group->lock);
		if (block_group->ro)
			space_info->bytes_readonly += reserved_bytes;
		block_group->reserved -= reserved_bytes;
		space_info->bytes_reserved -= reserved_bytes;
		spin_unlock(&block_group->lock);
		spin_unlock(&space_info->lock);
	}

	return ret;
}

/*
 * If @async is set, then we will trim 1 region and return.
 */
static int trim_no_bitmap(struct btrfs_block_group *block_group,
			  u64 *total_trimmed, u64 start, u64 end, u64 minlen,
			  bool async)
{
	struct btrfs_discard_ctl *discard_ctl =
					&block_group->fs_info->discard_ctl;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry;
	struct rb_node *node;
	int ret = 0;
	u64 extent_start;
	u64 extent_bytes;
	enum btrfs_trim_state extent_trim_state;
	u64 bytes;
	const u64 max_discard_size = READ_ONCE(discard_ctl->max_discard_size);

	while (start < end) {
		struct btrfs_trim_range trim_entry;

		mutex_lock(&ctl->cache_writeout_mutex);
		spin_lock(&ctl->tree_lock);

		if (ctl->free_space < minlen)
			goto out_unlock;

		entry = tree_search_offset(ctl, start, 0, 1);
		if (!entry)
			goto out_unlock;

		/* Skip bitmaps and if async, already trimmed entries */
		while (entry->bitmap ||
		       (async && btrfs_free_space_trimmed(entry))) {
			node = rb_next(&entry->offset_index);
			if (!node)
				goto out_unlock;
			entry = rb_entry(node, struct btrfs_free_space,
					 offset_index);
		}

		if (entry->offset >= end)
			goto out_unlock;

		extent_start = entry->offset;
		extent_bytes = entry->bytes;
		extent_trim_state = entry->trim_state;
		if (async) {
			start = entry->offset;
			bytes = entry->bytes;
			if (bytes < minlen) {
				spin_unlock(&ctl->tree_lock);
				mutex_unlock(&ctl->cache_writeout_mutex);
				goto next;
			}
			unlink_free_space(ctl, entry);
			/*
			 * Let bytes = BTRFS_MAX_DISCARD_SIZE + X.
			 * If X < BTRFS_ASYNC_DISCARD_MIN_FILTER, we won't trim
			 * X when we come back around.  So trim it now.
			 */
			if (max_discard_size &&
			    bytes >= (max_discard_size +
				      BTRFS_ASYNC_DISCARD_MIN_FILTER)) {
				bytes = max_discard_size;
				extent_bytes = max_discard_size;
				entry->offset += max_discard_size;
				entry->bytes -= max_discard_size;
				link_free_space(ctl, entry);
			} else {
				kmem_cache_free(btrfs_free_space_cachep, entry);
			}
		} else {
			start = max(start, extent_start);
			bytes = min(extent_start + extent_bytes, end) - start;
			if (bytes < minlen) {
				spin_unlock(&ctl->tree_lock);
				mutex_unlock(&ctl->cache_writeout_mutex);
				goto next;
			}

			unlink_free_space(ctl, entry);
			kmem_cache_free(btrfs_free_space_cachep, entry);
		}

		spin_unlock(&ctl->tree_lock);
		trim_entry.start = extent_start;
		trim_entry.bytes = extent_bytes;
		list_add_tail(&trim_entry.list, &ctl->trimming_ranges);
		mutex_unlock(&ctl->cache_writeout_mutex);

		ret = do_trimming(block_group, total_trimmed, start, bytes,
				  extent_start, extent_bytes, extent_trim_state,
				  &trim_entry);
		if (ret) {
			block_group->discard_cursor = start + bytes;
			break;
		}
next:
		start += bytes;
		block_group->discard_cursor = start;
		if (async && *total_trimmed)
			break;

		if (fatal_signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		cond_resched();
	}

	return ret;

out_unlock:
	block_group->discard_cursor = btrfs_block_group_end(block_group);
	spin_unlock(&ctl->tree_lock);
	mutex_unlock(&ctl->cache_writeout_mutex);

	return ret;
}

/*
 * If we break out of trimming a bitmap prematurely, we should reset the
 * trimming bit.  In a rather contrieved case, it's possible to race here so
 * reset the state to BTRFS_TRIM_STATE_UNTRIMMED.
 *
 * start = start of bitmap
 * end = near end of bitmap
 *
 * Thread 1:			Thread 2:
 * trim_bitmaps(start)
 *				trim_bitmaps(end)
 *				end_trimming_bitmap()
 * reset_trimming_bitmap()
 */
static void reset_trimming_bitmap(struct btrfs_free_space_ctl *ctl, u64 offset)
{
	struct btrfs_free_space *entry;

	spin_lock(&ctl->tree_lock);
	entry = tree_search_offset(ctl, offset, 1, 0);
	if (entry) {
		if (btrfs_free_space_trimmed(entry)) {
			ctl->discardable_extents[BTRFS_STAT_CURR] +=
				entry->bitmap_extents;
			ctl->discardable_bytes[BTRFS_STAT_CURR] += entry->bytes;
		}
		entry->trim_state = BTRFS_TRIM_STATE_UNTRIMMED;
	}

	spin_unlock(&ctl->tree_lock);
}

static void end_trimming_bitmap(struct btrfs_free_space_ctl *ctl,
				struct btrfs_free_space *entry)
{
	if (btrfs_free_space_trimming_bitmap(entry)) {
		entry->trim_state = BTRFS_TRIM_STATE_TRIMMED;
		ctl->discardable_extents[BTRFS_STAT_CURR] -=
			entry->bitmap_extents;
		ctl->discardable_bytes[BTRFS_STAT_CURR] -= entry->bytes;
	}
}

/*
 * If @async is set, then we will trim 1 region and return.
 */
static int trim_bitmaps(struct btrfs_block_group *block_group,
			u64 *total_trimmed, u64 start, u64 end, u64 minlen,
			u64 maxlen, bool async)
{
	struct btrfs_discard_ctl *discard_ctl =
					&block_group->fs_info->discard_ctl;
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry;
	int ret = 0;
	int ret2;
	u64 bytes;
	u64 offset = offset_to_bitmap(ctl, start);
	const u64 max_discard_size = READ_ONCE(discard_ctl->max_discard_size);

	while (offset < end) {
		bool next_bitmap = false;
		struct btrfs_trim_range trim_entry;

		mutex_lock(&ctl->cache_writeout_mutex);
		spin_lock(&ctl->tree_lock);

		if (ctl->free_space < minlen) {
			block_group->discard_cursor =
				btrfs_block_group_end(block_group);
			spin_unlock(&ctl->tree_lock);
			mutex_unlock(&ctl->cache_writeout_mutex);
			break;
		}

		entry = tree_search_offset(ctl, offset, 1, 0);
		/*
		 * Bitmaps are marked trimmed lossily now to prevent constant
		 * discarding of the same bitmap (the reason why we are bound
		 * by the filters).  So, retrim the block group bitmaps when we
		 * are preparing to punt to the unused_bgs list.  This uses
		 * @minlen to determine if we are in BTRFS_DISCARD_INDEX_UNUSED
		 * which is the only discard index which sets minlen to 0.
		 */
		if (!entry || (async && minlen && start == offset &&
			       btrfs_free_space_trimmed(entry))) {
			spin_unlock(&ctl->tree_lock);
			mutex_unlock(&ctl->cache_writeout_mutex);
			next_bitmap = true;
			goto next;
		}

		/*
		 * Async discard bitmap trimming begins at by setting the start
		 * to be key.objectid and the offset_to_bitmap() aligns to the
		 * start of the bitmap.  This lets us know we are fully
		 * scanning the bitmap rather than only some portion of it.
		 */
		if (start == offset)
			entry->trim_state = BTRFS_TRIM_STATE_TRIMMING;

		bytes = minlen;
		ret2 = search_bitmap(ctl, entry, &start, &bytes, false);
		if (ret2 || start >= end) {
			/*
			 * We lossily consider a bitmap trimmed if we only skip
			 * over regions <= BTRFS_ASYNC_DISCARD_MIN_FILTER.
			 */
			if (ret2 && minlen <= BTRFS_ASYNC_DISCARD_MIN_FILTER)
				end_trimming_bitmap(ctl, entry);
			else
				entry->trim_state = BTRFS_TRIM_STATE_UNTRIMMED;
			spin_unlock(&ctl->tree_lock);
			mutex_unlock(&ctl->cache_writeout_mutex);
			next_bitmap = true;
			goto next;
		}

		/*
		 * We already trimmed a region, but are using the locking above
		 * to reset the trim_state.
		 */
		if (async && *total_trimmed) {
			spin_unlock(&ctl->tree_lock);
			mutex_unlock(&ctl->cache_writeout_mutex);
			goto out;
		}

		bytes = min(bytes, end - start);
		if (bytes < minlen || (async && maxlen && bytes > maxlen)) {
			spin_unlock(&ctl->tree_lock);
			mutex_unlock(&ctl->cache_writeout_mutex);
			goto next;
		}

		/*
		 * Let bytes = BTRFS_MAX_DISCARD_SIZE + X.
		 * If X < @minlen, we won't trim X when we come back around.
		 * So trim it now.  We differ here from trimming extents as we
		 * don't keep individual state per bit.
		 */
		if (async &&
		    max_discard_size &&
		    bytes > (max_discard_size + minlen))
			bytes = max_discard_size;

		bitmap_clear_bits(ctl, entry, start, bytes);
		if (entry->bytes == 0)
			free_bitmap(ctl, entry);

		spin_unlock(&ctl->tree_lock);
		trim_entry.start = start;
		trim_entry.bytes = bytes;
		list_add_tail(&trim_entry.list, &ctl->trimming_ranges);
		mutex_unlock(&ctl->cache_writeout_mutex);

		ret = do_trimming(block_group, total_trimmed, start, bytes,
				  start, bytes, 0, &trim_entry);
		if (ret) {
			reset_trimming_bitmap(ctl, offset);
			block_group->discard_cursor =
				btrfs_block_group_end(block_group);
			break;
		}
next:
		if (next_bitmap) {
			offset += BITS_PER_BITMAP * ctl->unit;
			start = offset;
		} else {
			start += bytes;
		}
		block_group->discard_cursor = start;

		if (fatal_signal_pending(current)) {
			if (start != offset)
				reset_trimming_bitmap(ctl, offset);
			ret = -ERESTARTSYS;
			break;
		}

		cond_resched();
	}

	if (offset >= end)
		block_group->discard_cursor = end;

out:
	return ret;
}

int btrfs_trim_block_group(struct btrfs_block_group *block_group,
			   u64 *trimmed, u64 start, u64 end, u64 minlen)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	int ret;
	u64 rem = 0;

	*trimmed = 0;

	spin_lock(&block_group->lock);
	if (block_group->removed) {
		spin_unlock(&block_group->lock);
		return 0;
	}
	btrfs_freeze_block_group(block_group);
	spin_unlock(&block_group->lock);

	ret = trim_no_bitmap(block_group, trimmed, start, end, minlen, false);
	if (ret)
		goto out;

	ret = trim_bitmaps(block_group, trimmed, start, end, minlen, 0, false);
	div64_u64_rem(end, BITS_PER_BITMAP * ctl->unit, &rem);
	/* If we ended in the middle of a bitmap, reset the trimming flag */
	if (rem)
		reset_trimming_bitmap(ctl, offset_to_bitmap(ctl, end));
out:
	btrfs_unfreeze_block_group(block_group);
	return ret;
}

int btrfs_trim_block_group_extents(struct btrfs_block_group *block_group,
				   u64 *trimmed, u64 start, u64 end, u64 minlen,
				   bool async)
{
	int ret;

	*trimmed = 0;

	spin_lock(&block_group->lock);
	if (block_group->removed) {
		spin_unlock(&block_group->lock);
		return 0;
	}
	btrfs_freeze_block_group(block_group);
	spin_unlock(&block_group->lock);

	ret = trim_no_bitmap(block_group, trimmed, start, end, minlen, async);
	btrfs_unfreeze_block_group(block_group);

	return ret;
}

int btrfs_trim_block_group_bitmaps(struct btrfs_block_group *block_group,
				   u64 *trimmed, u64 start, u64 end, u64 minlen,
				   u64 maxlen, bool async)
{
	int ret;

	*trimmed = 0;

	spin_lock(&block_group->lock);
	if (block_group->removed) {
		spin_unlock(&block_group->lock);
		return 0;
	}
	btrfs_freeze_block_group(block_group);
	spin_unlock(&block_group->lock);

	ret = trim_bitmaps(block_group, trimmed, start, end, minlen, maxlen,
			   async);

	btrfs_unfreeze_block_group(block_group);

	return ret;
}

/*
 * Find the left-most item in the cache tree, and then return the
 * smallest inode number in the item.
 *
 * Note: the returned inode number may not be the smallest one in
 * the tree, if the left-most item is a bitmap.
 */
u64 btrfs_find_ino_for_alloc(struct btrfs_root *fs_root)
{
	struct btrfs_free_space_ctl *ctl = fs_root->free_ino_ctl;
	struct btrfs_free_space *entry = NULL;
	u64 ino = 0;

	spin_lock(&ctl->tree_lock);

	if (RB_EMPTY_ROOT(&ctl->free_space_offset))
		goto out;

	entry = rb_entry(rb_first(&ctl->free_space_offset),
			 struct btrfs_free_space, offset_index);

	if (!entry->bitmap) {
		ino = entry->offset;

		unlink_free_space(ctl, entry);
		entry->offset++;
		entry->bytes--;
		if (!entry->bytes)
			kmem_cache_free(btrfs_free_space_cachep, entry);
		else
			link_free_space(ctl, entry);
	} else {
		u64 offset = 0;
		u64 count = 1;
		int ret;

		ret = search_bitmap(ctl, entry, &offset, &count, true);
		/* Logic error; Should be empty if it can't find anything */
		ASSERT(!ret);

		ino = offset;
		bitmap_clear_bits(ctl, entry, offset, 1);
		if (entry->bytes == 0)
			free_bitmap(ctl, entry);
	}
out:
	spin_unlock(&ctl->tree_lock);

	return ino;
}

struct inode *lookup_free_ino_inode(struct btrfs_root *root,
				    struct btrfs_path *path)
{
	struct inode *inode = NULL;

	spin_lock(&root->ino_cache_lock);
	if (root->ino_cache_inode)
		inode = igrab(root->ino_cache_inode);
	spin_unlock(&root->ino_cache_lock);
	if (inode)
		return inode;

	inode = __lookup_free_space_inode(root, path, 0);
	if (IS_ERR(inode))
		return inode;

	spin_lock(&root->ino_cache_lock);
	if (!btrfs_fs_closing(root->fs_info))
		root->ino_cache_inode = igrab(inode);
	spin_unlock(&root->ino_cache_lock);

	return inode;
}

int create_free_ino_inode(struct btrfs_root *root,
			  struct btrfs_trans_handle *trans,
			  struct btrfs_path *path)
{
	return __create_free_space_inode(root, trans, path,
					 BTRFS_FREE_INO_OBJECTID, 0);
}

int load_free_ino_cache(struct btrfs_fs_info *fs_info, struct btrfs_root *root)
{
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	struct btrfs_path *path;
	struct inode *inode;
	int ret = 0;
	u64 root_gen = btrfs_root_generation(&root->root_item);

	if (!btrfs_test_opt(fs_info, INODE_MAP_CACHE))
		return 0;

	/*
	 * If we're unmounting then just return, since this does a search on the
	 * normal root and not the commit root and we could deadlock.
	 */
	if (btrfs_fs_closing(fs_info))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return 0;

	inode = lookup_free_ino_inode(root, path);
	if (IS_ERR(inode))
		goto out;

	if (root_gen != BTRFS_I(inode)->generation)
		goto out_put;

	ret = __load_free_space_cache(root, inode, ctl, path, 0);

	if (ret < 0)
		btrfs_err(fs_info,
			"failed to load free ino cache for root %llu",
			root->root_key.objectid);
out_put:
	iput(inode);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_write_out_ino_cache(struct btrfs_root *root,
			      struct btrfs_trans_handle *trans,
			      struct btrfs_path *path,
			      struct inode *inode)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	int ret;
	struct btrfs_io_ctl io_ctl;
	bool release_metadata = true;

	if (!btrfs_test_opt(fs_info, INODE_MAP_CACHE))
		return 0;

	memset(&io_ctl, 0, sizeof(io_ctl));
	ret = __btrfs_write_out_cache(root, inode, ctl, NULL, &io_ctl, trans);
	if (!ret) {
		/*
		 * At this point writepages() didn't error out, so our metadata
		 * reservation is released when the writeback finishes, at
		 * inode.c:btrfs_finish_ordered_io(), regardless of it finishing
		 * with or without an error.
		 */
		release_metadata = false;
		ret = btrfs_wait_cache_io_root(root, trans, &io_ctl, path);
	}

	if (ret) {
		if (release_metadata)
			btrfs_delalloc_release_metadata(BTRFS_I(inode),
					inode->i_size, true);
		btrfs_debug(fs_info,
			  "failed to write free ino cache for root %llu error %d",
			  root->root_key.objectid, ret);
	}

	return ret;
}

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
/*
 * Use this if you need to make a bitmap or extent entry specifically, it
 * doesn't do any of the merging that add_free_space does, this acts a lot like
 * how the free space cache loading stuff works, so you can get really weird
 * configurations.
 */
int test_add_free_space_entry(struct btrfs_block_group *cache,
			      u64 offset, u64 bytes, bool bitmap)
{
	struct btrfs_free_space_ctl *ctl = cache->free_space_ctl;
	struct btrfs_free_space *info = NULL, *bitmap_info;
	void *map = NULL;
	enum btrfs_trim_state trim_state = BTRFS_TRIM_STATE_TRIMMED;
	u64 bytes_added;
	int ret;

again:
	if (!info) {
		info = kmem_cache_zalloc(btrfs_free_space_cachep, GFP_NOFS);
		if (!info)
			return -ENOMEM;
	}

	if (!bitmap) {
		spin_lock(&ctl->tree_lock);
		info->offset = offset;
		info->bytes = bytes;
		info->max_extent_size = 0;
		ret = link_free_space(ctl, info);
		spin_unlock(&ctl->tree_lock);
		if (ret)
			kmem_cache_free(btrfs_free_space_cachep, info);
		return ret;
	}

	if (!map) {
		map = kmem_cache_zalloc(btrfs_free_space_bitmap_cachep, GFP_NOFS);
		if (!map) {
			kmem_cache_free(btrfs_free_space_cachep, info);
			return -ENOMEM;
		}
	}

	spin_lock(&ctl->tree_lock);
	bitmap_info = tree_search_offset(ctl, offset_to_bitmap(ctl, offset),
					 1, 0);
	if (!bitmap_info) {
		info->bitmap = map;
		map = NULL;
		add_new_bitmap(ctl, info, offset);
		bitmap_info = info;
		info = NULL;
	}

	bytes_added = add_bytes_to_bitmap(ctl, bitmap_info, offset, bytes,
					  trim_state);

	bytes -= bytes_added;
	offset += bytes_added;
	spin_unlock(&ctl->tree_lock);

	if (bytes)
		goto again;

	if (info)
		kmem_cache_free(btrfs_free_space_cachep, info);
	if (map)
		kmem_cache_free(btrfs_free_space_bitmap_cachep, map);
	return 0;
}

/*
 * Checks to see if the given range is in the free space cache.  This is really
 * just used to check the absence of space, so if there is free space in the
 * range at all we will return 1.
 */
int test_check_exists(struct btrfs_block_group *cache,
		      u64 offset, u64 bytes)
{
	struct btrfs_free_space_ctl *ctl = cache->free_space_ctl;
	struct btrfs_free_space *info;
	int ret = 0;

	spin_lock(&ctl->tree_lock);
	info = tree_search_offset(ctl, offset, 0, 0);
	if (!info) {
		info = tree_search_offset(ctl, offset_to_bitmap(ctl, offset),
					  1, 0);
		if (!info)
			goto out;
	}

have_info:
	if (info->bitmap) {
		u64 bit_off, bit_bytes;
		struct rb_node *n;
		struct btrfs_free_space *tmp;

		bit_off = offset;
		bit_bytes = ctl->unit;
		ret = search_bitmap(ctl, info, &bit_off, &bit_bytes, false);
		if (!ret) {
			if (bit_off == offset) {
				ret = 1;
				goto out;
			} else if (bit_off > offset &&
				   offset + bytes > bit_off) {
				ret = 1;
				goto out;
			}
		}

		n = rb_prev(&info->offset_index);
		while (n) {
			tmp = rb_entry(n, struct btrfs_free_space,
				       offset_index);
			if (tmp->offset + tmp->bytes < offset)
				break;
			if (offset + bytes < tmp->offset) {
				n = rb_prev(&tmp->offset_index);
				continue;
			}
			info = tmp;
			goto have_info;
		}

		n = rb_next(&info->offset_index);
		while (n) {
			tmp = rb_entry(n, struct btrfs_free_space,
				       offset_index);
			if (offset + bytes < tmp->offset)
				break;
			if (tmp->offset + tmp->bytes < offset) {
				n = rb_next(&tmp->offset_index);
				continue;
			}
			info = tmp;
			goto have_info;
		}

		ret = 0;
		goto out;
	}

	if (info->offset == offset) {
		ret = 1;
		goto out;
	}

	if (offset > info->offset && offset < info->offset + info->bytes)
		ret = 1;
out:
	spin_unlock(&ctl->tree_lock);
	return ret;
}
#endif /* CONFIG_BTRFS_FS_RUN_SANITY_TESTS */
