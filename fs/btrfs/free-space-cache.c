/*
 * Copyright (C) 2008 Red Hat.  All rights reserved.
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

#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/math64.h>
#include "ctree.h"
#include "free-space-cache.h"
#include "transaction.h"
#include "disk-io.h"
#include "extent_io.h"
#include "inode-map.h"

#define BITS_PER_BITMAP		(PAGE_CACHE_SIZE * 8)
#define MAX_CACHE_BYTES_PER_GIG	(32 * 1024)

static int link_free_space(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info);

static struct inode *__lookup_free_space_inode(struct btrfs_root *root,
					       struct btrfs_path *path,
					       u64 offset)
{
	struct btrfs_key key;
	struct btrfs_key location;
	struct btrfs_disk_key disk_key;
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	struct inode *inode = NULL;
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

	inode = btrfs_iget(root->fs_info->sb, &location, root, NULL);
	if (!inode)
		return ERR_PTR(-ENOENT);
	if (IS_ERR(inode))
		return inode;
	if (is_bad_inode(inode)) {
		iput(inode);
		return ERR_PTR(-ENOENT);
	}

	inode->i_mapping->flags &= ~__GFP_FS;

	return inode;
}

struct inode *lookup_free_space_inode(struct btrfs_root *root,
				      struct btrfs_block_group_cache
				      *block_group, struct btrfs_path *path)
{
	struct inode *inode = NULL;

	spin_lock(&block_group->lock);
	if (block_group->inode)
		inode = igrab(block_group->inode);
	spin_unlock(&block_group->lock);
	if (inode)
		return inode;

	inode = __lookup_free_space_inode(root, path,
					  block_group->key.objectid);
	if (IS_ERR(inode))
		return inode;

	spin_lock(&block_group->lock);
	if (!btrfs_fs_closing(root->fs_info)) {
		block_group->inode = igrab(inode);
		block_group->iref = 1;
	}
	spin_unlock(&block_group->lock);

	return inode;
}

int __create_free_space_inode(struct btrfs_root *root,
			      struct btrfs_trans_handle *trans,
			      struct btrfs_path *path, u64 ino, u64 offset)
{
	struct btrfs_key key;
	struct btrfs_disk_key disk_key;
	struct btrfs_free_space_header *header;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	int ret;

	ret = btrfs_insert_empty_inode(trans, root, path, ino);
	if (ret)
		return ret;

	leaf = path->nodes[0];
	inode_item = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_inode_item);
	btrfs_item_key(leaf, &disk_key, path->slots[0]);
	memset_extent_buffer(leaf, 0, (unsigned long)inode_item,
			     sizeof(*inode_item));
	btrfs_set_inode_generation(leaf, inode_item, trans->transid);
	btrfs_set_inode_size(leaf, inode_item, 0);
	btrfs_set_inode_nbytes(leaf, inode_item, 0);
	btrfs_set_inode_uid(leaf, inode_item, 0);
	btrfs_set_inode_gid(leaf, inode_item, 0);
	btrfs_set_inode_mode(leaf, inode_item, S_IFREG | 0600);
	btrfs_set_inode_flags(leaf, inode_item, BTRFS_INODE_NOCOMPRESS |
			      BTRFS_INODE_PREALLOC | BTRFS_INODE_NODATASUM);
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
	memset_extent_buffer(leaf, 0, (unsigned long)header, sizeof(*header));
	btrfs_set_free_space_key(leaf, header, &disk_key);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	return 0;
}

int create_free_space_inode(struct btrfs_root *root,
			    struct btrfs_trans_handle *trans,
			    struct btrfs_block_group_cache *block_group,
			    struct btrfs_path *path)
{
	int ret;
	u64 ino;

	ret = btrfs_find_free_objectid(root, &ino);
	if (ret < 0)
		return ret;

	return __create_free_space_inode(root, trans, path, ino,
					 block_group->key.objectid);
}

int btrfs_truncate_free_space_cache(struct btrfs_root *root,
				    struct btrfs_trans_handle *trans,
				    struct btrfs_path *path,
				    struct inode *inode)
{
	loff_t oldsize;
	int ret = 0;

	trans->block_rsv = root->orphan_block_rsv;
	ret = btrfs_block_rsv_check(trans, root,
				    root->orphan_block_rsv,
				    0, 5);
	if (ret)
		return ret;

	oldsize = i_size_read(inode);
	btrfs_i_size_write(inode, 0);
	truncate_pagecache(inode, oldsize, 0);

	/*
	 * We don't need an orphan item because truncating the free space cache
	 * will never be split across transactions.
	 */
	ret = btrfs_truncate_inode_items(trans, root, inode,
					 0, BTRFS_EXTENT_DATA_KEY);
	if (ret) {
		WARN_ON(1);
		return ret;
	}

	ret = btrfs_update_inode(trans, root, inode);
	return ret;
}

static int readahead_cache(struct inode *inode)
{
	struct file_ra_state *ra;
	unsigned long last_index;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);
	if (!ra)
		return -ENOMEM;

	file_ra_state_init(ra, inode->i_mapping);
	last_index = (i_size_read(inode) - 1) >> PAGE_CACHE_SHIFT;

	page_cache_sync_readahead(inode->i_mapping, ra, NULL, 0, last_index);

	kfree(ra);

	return 0;
}

int __load_free_space_cache(struct btrfs_root *root, struct inode *inode,
			    struct btrfs_free_space_ctl *ctl,
			    struct btrfs_path *path, u64 offset)
{
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	struct page *page;
	u32 *checksums = NULL, *crc;
	char *disk_crcs = NULL;
	struct btrfs_key key;
	struct list_head bitmaps;
	u64 num_entries;
	u64 num_bitmaps;
	u64 generation;
	u32 cur_crc = ~(u32)0;
	pgoff_t index = 0;
	unsigned long first_page_offset;
	int num_checksums;
	int ret = 0;

	INIT_LIST_HEAD(&bitmaps);

	/* Nothing in the space cache, goodbye */
	if (!i_size_read(inode))
		goto out;

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	else if (ret > 0) {
		btrfs_release_path(path);
		ret = 0;
		goto out;
	}

	ret = -1;

	leaf = path->nodes[0];
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	num_entries = btrfs_free_space_entries(leaf, header);
	num_bitmaps = btrfs_free_space_bitmaps(leaf, header);
	generation = btrfs_free_space_generation(leaf, header);
	btrfs_release_path(path);

	if (BTRFS_I(inode)->generation != generation) {
		printk(KERN_ERR "btrfs: free space inode generation (%llu) did"
		       " not match free space cache generation (%llu)\n",
		       (unsigned long long)BTRFS_I(inode)->generation,
		       (unsigned long long)generation);
		goto out;
	}

	if (!num_entries)
		goto out;

	/* Setup everything for doing checksumming */
	num_checksums = i_size_read(inode) / PAGE_CACHE_SIZE;
	checksums = crc = kzalloc(sizeof(u32) * num_checksums, GFP_NOFS);
	if (!checksums)
		goto out;
	first_page_offset = (sizeof(u32) * num_checksums) + sizeof(u64);
	disk_crcs = kzalloc(first_page_offset, GFP_NOFS);
	if (!disk_crcs)
		goto out;

	ret = readahead_cache(inode);
	if (ret)
		goto out;

	while (1) {
		struct btrfs_free_space_entry *entry;
		struct btrfs_free_space *e;
		void *addr;
		unsigned long offset = 0;
		unsigned long start_offset = 0;
		int need_loop = 0;

		if (!num_entries && !num_bitmaps)
			break;

		if (index == 0) {
			start_offset = first_page_offset;
			offset = start_offset;
		}

		page = grab_cache_page(inode->i_mapping, index);
		if (!page)
			goto free_cache;

		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_cache_release(page);
				printk(KERN_ERR "btrfs: error reading free "
				       "space cache\n");
				goto free_cache;
			}
		}
		addr = kmap(page);

		if (index == 0) {
			u64 *gen;

			memcpy(disk_crcs, addr, first_page_offset);
			gen = addr + (sizeof(u32) * num_checksums);
			if (*gen != BTRFS_I(inode)->generation) {
				printk(KERN_ERR "btrfs: space cache generation"
				       " (%llu) does not match inode (%llu)\n",
				       (unsigned long long)*gen,
				       (unsigned long long)
				       BTRFS_I(inode)->generation);
				kunmap(page);
				unlock_page(page);
				page_cache_release(page);
				goto free_cache;
			}
			crc = (u32 *)disk_crcs;
		}
		entry = addr + start_offset;

		/* First lets check our crc before we do anything fun */
		cur_crc = ~(u32)0;
		cur_crc = btrfs_csum_data(root, addr + start_offset, cur_crc,
					  PAGE_CACHE_SIZE - start_offset);
		btrfs_csum_final(cur_crc, (char *)&cur_crc);
		if (cur_crc != *crc) {
			printk(KERN_ERR "btrfs: crc mismatch for page %lu\n",
			       index);
			kunmap(page);
			unlock_page(page);
			page_cache_release(page);
			goto free_cache;
		}
		crc++;

		while (1) {
			if (!num_entries)
				break;

			need_loop = 1;
			e = kmem_cache_zalloc(btrfs_free_space_cachep,
					      GFP_NOFS);
			if (!e) {
				kunmap(page);
				unlock_page(page);
				page_cache_release(page);
				goto free_cache;
			}

			e->offset = le64_to_cpu(entry->offset);
			e->bytes = le64_to_cpu(entry->bytes);
			if (!e->bytes) {
				kunmap(page);
				kmem_cache_free(btrfs_free_space_cachep, e);
				unlock_page(page);
				page_cache_release(page);
				goto free_cache;
			}

			if (entry->type == BTRFS_FREE_SPACE_EXTENT) {
				spin_lock(&ctl->tree_lock);
				ret = link_free_space(ctl, e);
				spin_unlock(&ctl->tree_lock);
				if (ret) {
					printk(KERN_ERR "Duplicate entries in "
					       "free space cache, dumping\n");
					kunmap(page);
					unlock_page(page);
					page_cache_release(page);
					goto free_cache;
				}
			} else {
				e->bitmap = kzalloc(PAGE_CACHE_SIZE, GFP_NOFS);
				if (!e->bitmap) {
					kunmap(page);
					kmem_cache_free(
						btrfs_free_space_cachep, e);
					unlock_page(page);
					page_cache_release(page);
					goto free_cache;
				}
				spin_lock(&ctl->tree_lock);
				ret = link_free_space(ctl, e);
				ctl->total_bitmaps++;
				ctl->op->recalc_thresholds(ctl);
				spin_unlock(&ctl->tree_lock);
				if (ret) {
					printk(KERN_ERR "Duplicate entries in "
					       "free space cache, dumping\n");
					kunmap(page);
					unlock_page(page);
					page_cache_release(page);
					goto free_cache;
				}
				list_add_tail(&e->list, &bitmaps);
			}

			num_entries--;
			offset += sizeof(struct btrfs_free_space_entry);
			if (offset + sizeof(struct btrfs_free_space_entry) >=
			    PAGE_CACHE_SIZE)
				break;
			entry++;
		}

		/*
		 * We read an entry out of this page, we need to move on to the
		 * next page.
		 */
		if (need_loop) {
			kunmap(page);
			goto next;
		}

		/*
		 * We add the bitmaps at the end of the entries in order that
		 * the bitmap entries are added to the cache.
		 */
		e = list_entry(bitmaps.next, struct btrfs_free_space, list);
		list_del_init(&e->list);
		memcpy(e->bitmap, addr, PAGE_CACHE_SIZE);
		kunmap(page);
		num_bitmaps--;
next:
		unlock_page(page);
		page_cache_release(page);
		index++;
	}

	ret = 1;
out:
	kfree(checksums);
	kfree(disk_crcs);
	return ret;
free_cache:
	__btrfs_remove_free_space_cache(ctl);
	goto out;
}

int load_free_space_cache(struct btrfs_fs_info *fs_info,
			  struct btrfs_block_group_cache *block_group)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_root *root = fs_info->tree_root;
	struct inode *inode;
	struct btrfs_path *path;
	int ret;
	bool matched;
	u64 used = btrfs_block_group_used(&block_group->item);

	/*
	 * If we're unmounting then just return, since this does a search on the
	 * normal root and not the commit root and we could deadlock.
	 */
	if (btrfs_fs_closing(fs_info))
		return 0;

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

	inode = lookup_free_space_inode(root, block_group, path);
	if (IS_ERR(inode)) {
		btrfs_free_path(path);
		return 0;
	}

	ret = __load_free_space_cache(fs_info->tree_root, inode, ctl,
				      path, block_group->key.objectid);
	btrfs_free_path(path);
	if (ret <= 0)
		goto out;

	spin_lock(&ctl->tree_lock);
	matched = (ctl->free_space == (block_group->key.offset - used -
				       block_group->bytes_super));
	spin_unlock(&ctl->tree_lock);

	if (!matched) {
		__btrfs_remove_free_space_cache(ctl);
		printk(KERN_ERR "block group %llu has an wrong amount of free "
		       "space\n", block_group->key.objectid);
		ret = -1;
	}
out:
	if (ret < 0) {
		/* This cache is bogus, make sure it gets cleared */
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_CLEAR;
		spin_unlock(&block_group->lock);
		ret = 0;

		printk(KERN_ERR "btrfs: failed to load free space cache "
		       "for block group %llu\n", block_group->key.objectid);
	}

	iput(inode);
	return ret;
}

int __btrfs_write_out_cache(struct btrfs_root *root, struct inode *inode,
			    struct btrfs_free_space_ctl *ctl,
			    struct btrfs_block_group_cache *block_group,
			    struct btrfs_trans_handle *trans,
			    struct btrfs_path *path, u64 offset)
{
	struct btrfs_free_space_header *header;
	struct extent_buffer *leaf;
	struct rb_node *node;
	struct list_head *pos, *n;
	struct page **pages;
	struct page *page;
	struct extent_state *cached_state = NULL;
	struct btrfs_free_cluster *cluster = NULL;
	struct extent_io_tree *unpin = NULL;
	struct list_head bitmap_list;
	struct btrfs_key key;
	u64 start, end, len;
	u64 bytes = 0;
	u32 *crc, *checksums;
	unsigned long first_page_offset;
	int index = 0, num_pages = 0;
	int entries = 0;
	int bitmaps = 0;
	int ret = -1;
	bool next_page = false;
	bool out_of_space = false;

	INIT_LIST_HEAD(&bitmap_list);

	node = rb_first(&ctl->free_space_offset);
	if (!node)
		return 0;

	if (!i_size_read(inode))
		return -1;

	num_pages = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >>
		PAGE_CACHE_SHIFT;

	/* Since the first page has all of our checksums and our generation we
	 * need to calculate the offset into the page that we can start writing
	 * our entries.
	 */
	first_page_offset = (sizeof(u32) * num_pages) + sizeof(u64);

	filemap_write_and_wait(inode->i_mapping);
	btrfs_wait_ordered_range(inode, inode->i_size &
				 ~(root->sectorsize - 1), (u64)-1);

	/* make sure we don't overflow that first page */
	if (first_page_offset + sizeof(struct btrfs_free_space_entry) >= PAGE_CACHE_SIZE) {
		/* this is really the same as running out of space, where we also return 0 */
		printk(KERN_CRIT "Btrfs: free space cache was too big for the crc page\n");
		ret = 0;
		goto out_update;
	}

	/* We need a checksum per page. */
	crc = checksums = kzalloc(sizeof(u32) * num_pages, GFP_NOFS);
	if (!crc)
		return -1;

	pages = kzalloc(sizeof(struct page *) * num_pages, GFP_NOFS);
	if (!pages) {
		kfree(crc);
		return -1;
	}

	/* Get the cluster for this block_group if it exists */
	if (block_group && !list_empty(&block_group->cluster_list))
		cluster = list_entry(block_group->cluster_list.next,
				     struct btrfs_free_cluster,
				     block_group_list);

	/*
	 * We shouldn't have switched the pinned extents yet so this is the
	 * right one
	 */
	unpin = root->fs_info->pinned_extents;

	/*
	 * Lock all pages first so we can lock the extent safely.
	 *
	 * NOTE: Because we hold the ref the entire time we're going to write to
	 * the page find_get_page should never fail, so we don't do a check
	 * after find_get_page at this point.  Just putting this here so people
	 * know and don't freak out.
	 */
	while (index < num_pages) {
		page = grab_cache_page(inode->i_mapping, index);
		if (!page) {
			int i;

			for (i = 0; i < num_pages; i++) {
				unlock_page(pages[i]);
				page_cache_release(pages[i]);
			}
			goto out_free;
		}
		pages[index] = page;
		index++;
	}

	index = 0;
	lock_extent_bits(&BTRFS_I(inode)->io_tree, 0, i_size_read(inode) - 1,
			 0, &cached_state, GFP_NOFS);

	/*
	 * When searching for pinned extents, we need to start at our start
	 * offset.
	 */
	if (block_group)
		start = block_group->key.objectid;

	/* Write out the extent entries */
	do {
		struct btrfs_free_space_entry *entry;
		void *addr;
		unsigned long offset = 0;
		unsigned long start_offset = 0;

		next_page = false;

		if (index == 0) {
			start_offset = first_page_offset;
			offset = start_offset;
		}

		if (index >= num_pages) {
			out_of_space = true;
			break;
		}

		page = pages[index];

		addr = kmap(page);
		entry = addr + start_offset;

		memset(addr, 0, PAGE_CACHE_SIZE);
		while (node && !next_page) {
			struct btrfs_free_space *e;

			e = rb_entry(node, struct btrfs_free_space, offset_index);
			entries++;

			entry->offset = cpu_to_le64(e->offset);
			entry->bytes = cpu_to_le64(e->bytes);
			if (e->bitmap) {
				entry->type = BTRFS_FREE_SPACE_BITMAP;
				list_add_tail(&e->list, &bitmap_list);
				bitmaps++;
			} else {
				entry->type = BTRFS_FREE_SPACE_EXTENT;
			}
			node = rb_next(node);
			if (!node && cluster) {
				node = rb_first(&cluster->root);
				cluster = NULL;
			}
			offset += sizeof(struct btrfs_free_space_entry);
			if (offset + sizeof(struct btrfs_free_space_entry) >=
			    PAGE_CACHE_SIZE)
				next_page = true;
			entry++;
		}

		/*
		 * We want to add any pinned extents to our free space cache
		 * so we don't leak the space
		 */
		while (block_group && !next_page &&
		       (start < block_group->key.objectid +
			block_group->key.offset)) {
			ret = find_first_extent_bit(unpin, start, &start, &end,
						    EXTENT_DIRTY);
			if (ret) {
				ret = 0;
				break;
			}

			/* This pinned extent is out of our range */
			if (start >= block_group->key.objectid +
			    block_group->key.offset)
				break;

			len = block_group->key.objectid +
				block_group->key.offset - start;
			len = min(len, end + 1 - start);

			entries++;
			entry->offset = cpu_to_le64(start);
			entry->bytes = cpu_to_le64(len);
			entry->type = BTRFS_FREE_SPACE_EXTENT;

			start = end + 1;
			offset += sizeof(struct btrfs_free_space_entry);
			if (offset + sizeof(struct btrfs_free_space_entry) >=
			    PAGE_CACHE_SIZE)
				next_page = true;
			entry++;
		}
		*crc = ~(u32)0;
		*crc = btrfs_csum_data(root, addr + start_offset, *crc,
				       PAGE_CACHE_SIZE - start_offset);
		kunmap(page);

		btrfs_csum_final(*crc, (char *)crc);
		crc++;

		bytes += PAGE_CACHE_SIZE;

		index++;
	} while (node || next_page);

	/* Write out the bitmaps */
	list_for_each_safe(pos, n, &bitmap_list) {
		void *addr;
		struct btrfs_free_space *entry =
			list_entry(pos, struct btrfs_free_space, list);

		if (index >= num_pages) {
			out_of_space = true;
			break;
		}
		page = pages[index];

		addr = kmap(page);
		memcpy(addr, entry->bitmap, PAGE_CACHE_SIZE);
		*crc = ~(u32)0;
		*crc = btrfs_csum_data(root, addr, *crc, PAGE_CACHE_SIZE);
		kunmap(page);
		btrfs_csum_final(*crc, (char *)crc);
		crc++;
		bytes += PAGE_CACHE_SIZE;

		list_del_init(&entry->list);
		index++;
	}

	if (out_of_space) {
		btrfs_drop_pages(pages, num_pages);
		unlock_extent_cached(&BTRFS_I(inode)->io_tree, 0,
				     i_size_read(inode) - 1, &cached_state,
				     GFP_NOFS);
		ret = 0;
		goto out_free;
	}

	/* Zero out the rest of the pages just to make sure */
	while (index < num_pages) {
		void *addr;

		page = pages[index];
		addr = kmap(page);
		memset(addr, 0, PAGE_CACHE_SIZE);
		kunmap(page);
		bytes += PAGE_CACHE_SIZE;
		index++;
	}

	/* Write the checksums and trans id to the first page */
	{
		void *addr;
		u64 *gen;

		page = pages[0];

		addr = kmap(page);
		memcpy(addr, checksums, sizeof(u32) * num_pages);
		gen = addr + (sizeof(u32) * num_pages);
		*gen = trans->transid;
		kunmap(page);
	}

	ret = btrfs_dirty_pages(root, inode, pages, num_pages, 0,
					    bytes, &cached_state);
	btrfs_drop_pages(pages, num_pages);
	unlock_extent_cached(&BTRFS_I(inode)->io_tree, 0,
			     i_size_read(inode) - 1, &cached_state, GFP_NOFS);

	if (ret) {
		ret = 0;
		goto out_free;
	}

	BTRFS_I(inode)->generation = trans->transid;

	filemap_write_and_wait(inode->i_mapping);

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = offset;
	key.type = 0;

	ret = btrfs_search_slot(trans, root, &key, path, 1, 1);
	if (ret < 0) {
		ret = -1;
		clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, bytes - 1,
				 EXTENT_DIRTY | EXTENT_DELALLOC |
				 EXTENT_DO_ACCOUNTING, 0, 0, NULL, GFP_NOFS);
		goto out_free;
	}
	leaf = path->nodes[0];
	if (ret > 0) {
		struct btrfs_key found_key;
		BUG_ON(!path->slots[0]);
		path->slots[0]--;
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != BTRFS_FREE_SPACE_OBJECTID ||
		    found_key.offset != offset) {
			ret = -1;
			clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, bytes - 1,
					 EXTENT_DIRTY | EXTENT_DELALLOC |
					 EXTENT_DO_ACCOUNTING, 0, 0, NULL,
					 GFP_NOFS);
			btrfs_release_path(path);
			goto out_free;
		}
	}
	header = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_free_space_header);
	btrfs_set_free_space_entries(leaf, header, entries);
	btrfs_set_free_space_bitmaps(leaf, header, bitmaps);
	btrfs_set_free_space_generation(leaf, header, trans->transid);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	ret = 1;

out_free:
	kfree(checksums);
	kfree(pages);

out_update:
	if (ret != 1) {
		invalidate_inode_pages2_range(inode->i_mapping, 0, index);
		BTRFS_I(inode)->generation = 0;
	}
	btrfs_update_inode(trans, root, inode);
	return ret;
}

int btrfs_write_out_cache(struct btrfs_root *root,
			  struct btrfs_trans_handle *trans,
			  struct btrfs_block_group_cache *block_group,
			  struct btrfs_path *path)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct inode *inode;
	int ret = 0;

	root = root->fs_info->tree_root;

	spin_lock(&block_group->lock);
	if (block_group->disk_cache_state < BTRFS_DC_SETUP) {
		spin_unlock(&block_group->lock);
		return 0;
	}
	spin_unlock(&block_group->lock);

	inode = lookup_free_space_inode(root, block_group, path);
	if (IS_ERR(inode))
		return 0;

	ret = __btrfs_write_out_cache(root, inode, ctl, block_group, trans,
				      path, block_group->key.objectid);
	if (ret < 0) {
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_ERROR;
		spin_unlock(&block_group->lock);
		ret = 0;

		printk(KERN_ERR "btrfs: failed to write free space cace "
		       "for block group %llu\n", block_group->key.objectid);
	}

	iput(inode);
	return ret;
}

static inline unsigned long offset_to_bit(u64 bitmap_start, u32 unit,
					  u64 offset)
{
	BUG_ON(offset < bitmap_start);
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
			n = &entry->offset_index;
			while (1) {
				n = rb_prev(n);
				if (!n)
					break;
				prev = rb_entry(n, struct btrfs_free_space,
						offset_index);
				if (!prev->bitmap) {
					if (prev->offset + prev->bytes > offset)
						entry = prev;
					break;
				}
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
			BUG_ON(entry->offset > offset);
		} else {
			if (fuzzy)
				return entry;
			else
				return NULL;
		}
	}

	if (entry->bitmap) {
		n = &entry->offset_index;
		while (1) {
			n = rb_prev(n);
			if (!n)
				break;
			prev = rb_entry(n, struct btrfs_free_space,
					offset_index);
			if (!prev->bitmap) {
				if (prev->offset + prev->bytes > offset)
					return prev;
				break;
			}
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

	BUG_ON(!info->bitmap && !info->bytes);
	ret = tree_insert_offset(&ctl->free_space_offset, info->offset,
				 &info->offset_index, (info->bitmap != NULL));
	if (ret)
		return ret;

	ctl->free_space += info->bytes;
	ctl->free_extents++;
	return ret;
}

static void recalculate_thresholds(struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_block_group_cache *block_group = ctl->private;
	u64 max_bytes;
	u64 bitmap_bytes;
	u64 extent_bytes;
	u64 size = block_group->key.offset;
	u64 bytes_per_bg = BITS_PER_BITMAP * block_group->sectorsize;
	int max_bitmaps = div64_u64(size + bytes_per_bg - 1, bytes_per_bg);

	BUG_ON(ctl->total_bitmaps > max_bitmaps);

	/*
	 * The goal is to keep the total amount of memory used per 1gb of space
	 * at or below 32k, so we need to adjust how much memory we allow to be
	 * used by extent based free space tracking
	 */
	if (size < 1024 * 1024 * 1024)
		max_bytes = MAX_CACHE_BYTES_PER_GIG;
	else
		max_bytes = MAX_CACHE_BYTES_PER_GIG *
			div64_u64(size, 1024 * 1024 * 1024);

	/*
	 * we want to account for 1 more bitmap than what we have so we can make
	 * sure we don't go over our overall goal of MAX_CACHE_BYTES_PER_GIG as
	 * we add more bitmaps.
	 */
	bitmap_bytes = (ctl->total_bitmaps + 1) * PAGE_CACHE_SIZE;

	if (bitmap_bytes >= max_bytes) {
		ctl->extents_thresh = 0;
		return;
	}

	/*
	 * we want the extent entry threshold to always be at most 1/2 the maxw
	 * bytes we can have, or whatever is less than that.
	 */
	extent_bytes = max_bytes - bitmap_bytes;
	extent_bytes = min_t(u64, extent_bytes, div64_u64(max_bytes, 2));

	ctl->extents_thresh =
		div64_u64(extent_bytes, (sizeof(struct btrfs_free_space)));
}

static void bitmap_clear_bits(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info, u64 offset,
			      u64 bytes)
{
	unsigned long start, count;

	start = offset_to_bit(info->offset, ctl->unit, offset);
	count = bytes_to_bits(bytes, ctl->unit);
	BUG_ON(start + count > BITS_PER_BITMAP);

	bitmap_clear(info->bitmap, start, count);

	info->bytes -= bytes;
	ctl->free_space -= bytes;
}

static void bitmap_set_bits(struct btrfs_free_space_ctl *ctl,
			    struct btrfs_free_space *info, u64 offset,
			    u64 bytes)
{
	unsigned long start, count;

	start = offset_to_bit(info->offset, ctl->unit, offset);
	count = bytes_to_bits(bytes, ctl->unit);
	BUG_ON(start + count > BITS_PER_BITMAP);

	bitmap_set(info->bitmap, start, count);

	info->bytes += bytes;
	ctl->free_space += bytes;
}

static int search_bitmap(struct btrfs_free_space_ctl *ctl,
			 struct btrfs_free_space *bitmap_info, u64 *offset,
			 u64 *bytes)
{
	unsigned long found_bits = 0;
	unsigned long bits, i;
	unsigned long next_zero;

	i = offset_to_bit(bitmap_info->offset, ctl->unit,
			  max_t(u64, *offset, bitmap_info->offset));
	bits = bytes_to_bits(*bytes, ctl->unit);

	for (i = find_next_bit(bitmap_info->bitmap, BITS_PER_BITMAP, i);
	     i < BITS_PER_BITMAP;
	     i = find_next_bit(bitmap_info->bitmap, BITS_PER_BITMAP, i + 1)) {
		next_zero = find_next_zero_bit(bitmap_info->bitmap,
					       BITS_PER_BITMAP, i);
		if ((next_zero - i) >= bits) {
			found_bits = next_zero - i;
			break;
		}
		i = next_zero;
	}

	if (found_bits) {
		*offset = (u64)(i * ctl->unit) + bitmap_info->offset;
		*bytes = (u64)(found_bits) * ctl->unit;
		return 0;
	}

	return -1;
}

static struct btrfs_free_space *
find_free_space(struct btrfs_free_space_ctl *ctl, u64 *offset, u64 *bytes)
{
	struct btrfs_free_space *entry;
	struct rb_node *node;
	int ret;

	if (!ctl->free_space_offset.rb_node)
		return NULL;

	entry = tree_search_offset(ctl, offset_to_bitmap(ctl, *offset), 0, 1);
	if (!entry)
		return NULL;

	for (node = &entry->offset_index; node; node = rb_next(node)) {
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		if (entry->bytes < *bytes)
			continue;

		if (entry->bitmap) {
			ret = search_bitmap(ctl, entry, offset, bytes);
			if (!ret)
				return entry;
			continue;
		}

		*offset = entry->offset;
		*bytes = entry->bytes;
		return entry;
	}

	return NULL;
}

static void add_new_bitmap(struct btrfs_free_space_ctl *ctl,
			   struct btrfs_free_space *info, u64 offset)
{
	info->offset = offset_to_bitmap(ctl, offset);
	info->bytes = 0;
	link_free_space(ctl, info);
	ctl->total_bitmaps++;

	ctl->op->recalc_thresholds(ctl);
}

static void free_bitmap(struct btrfs_free_space_ctl *ctl,
			struct btrfs_free_space *bitmap_info)
{
	unlink_free_space(ctl, bitmap_info);
	kfree(bitmap_info->bitmap);
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
	 * XXX - this can go away after a few releases.
	 *
	 * since the only user of btrfs_remove_free_space is the tree logging
	 * stuff, and the only way to test that is under crash conditions, we
	 * want to have this debug stuff here just in case somethings not
	 * working.  Search the bitmap for the space we are trying to use to
	 * make sure its actually there.  If its not there then we need to stop
	 * because something has gone wrong.
	 */
	search_start = *offset;
	search_bytes = *bytes;
	search_bytes = min(search_bytes, end - search_start + 1);
	ret = search_bitmap(ctl, bitmap_info, &search_start, &search_bytes);
	BUG_ON(ret < 0 || search_start != *offset);

	if (*offset > bitmap_info->offset && *offset + *bytes > end) {
		bitmap_clear_bits(ctl, bitmap_info, *offset, end - *offset + 1);
		*bytes -= end - *offset + 1;
		*offset = end + 1;
	} else if (*offset >= bitmap_info->offset && *offset + *bytes <= end) {
		bitmap_clear_bits(ctl, bitmap_info, *offset, *bytes);
		*bytes = 0;
	}

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
		search_bytes = *bytes;
		ret = search_bitmap(ctl, bitmap_info, &search_start,
				    &search_bytes);
		if (ret < 0 || search_start != *offset)
			return -EAGAIN;

		goto again;
	} else if (!bitmap_info->bytes)
		free_bitmap(ctl, bitmap_info);

	return 0;
}

static u64 add_bytes_to_bitmap(struct btrfs_free_space_ctl *ctl,
			       struct btrfs_free_space *info, u64 offset,
			       u64 bytes)
{
	u64 bytes_to_set = 0;
	u64 end;

	end = info->offset + (u64)(BITS_PER_BITMAP * ctl->unit);

	bytes_to_set = min(end - offset, bytes);

	bitmap_set_bits(ctl, info, offset, bytes_to_set);

	return bytes_to_set;

}

static bool use_bitmap(struct btrfs_free_space_ctl *ctl,
		      struct btrfs_free_space *info)
{
	struct btrfs_block_group_cache *block_group = ctl->private;

	/*
	 * If we are below the extents threshold then we can add this as an
	 * extent, and don't have to deal with the bitmap
	 */
	if (ctl->free_extents < ctl->extents_thresh) {
		/*
		 * If this block group has some small extents we don't want to
		 * use up all of our free slots in the cache with them, we want
		 * to reserve them to larger extents, however if we have plent
		 * of cache left then go ahead an dadd them, no sense in adding
		 * the overhead of a bitmap if we don't have to.
		 */
		if (info->bytes <= block_group->sectorsize * 4) {
			if (ctl->free_extents * 2 <= ctl->extents_thresh)
				return false;
		} else {
			return false;
		}
	}

	/*
	 * some block groups are so tiny they can't be enveloped by a bitmap, so
	 * don't even bother to create a bitmap for this
	 */
	if (BITS_PER_BITMAP * block_group->sectorsize >
	    block_group->key.offset)
		return false;

	return true;
}

static struct btrfs_free_space_op free_space_op = {
	.recalc_thresholds	= recalculate_thresholds,
	.use_bitmap		= use_bitmap,
};

static int insert_into_bitmap(struct btrfs_free_space_ctl *ctl,
			      struct btrfs_free_space *info)
{
	struct btrfs_free_space *bitmap_info;
	struct btrfs_block_group_cache *block_group = NULL;
	int added = 0;
	u64 bytes, offset, bytes_added;
	int ret;

	bytes = info->bytes;
	offset = info->offset;

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
			bytes_added = add_bytes_to_bitmap(ctl, entry,
							  offset, bytes);
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
		BUG_ON(added);
		goto new_bitmap;
	}

	bytes_added = add_bytes_to_bitmap(ctl, bitmap_info, offset, bytes);
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
		info->bitmap = kzalloc(PAGE_CACHE_SIZE, GFP_NOFS);
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
			kfree(info->bitmap);
		kmem_cache_free(btrfs_free_space_cachep, info);
	}

	return ret;
}

static bool try_merge_free_space(struct btrfs_free_space_ctl *ctl,
			  struct btrfs_free_space *info, bool update_stat)
{
	struct btrfs_free_space *left_info;
	struct btrfs_free_space *right_info;
	bool merged = false;
	u64 offset = info->offset;
	u64 bytes = info->bytes;

	/*
	 * first we want to see if there is free space adjacent to the range we
	 * are adding, if there is remove that struct and add a new one to
	 * cover the entire range
	 */
	right_info = tree_search_offset(ctl, offset + bytes, 0, 0);
	if (right_info && rb_prev(&right_info->offset_index))
		left_info = rb_entry(rb_prev(&right_info->offset_index),
				     struct btrfs_free_space, offset_index);
	else
		left_info = tree_search_offset(ctl, offset - 1, 0, 0);

	if (right_info && !right_info->bitmap) {
		if (update_stat)
			unlink_free_space(ctl, right_info);
		else
			__unlink_free_space(ctl, right_info);
		info->bytes += right_info->bytes;
		kmem_cache_free(btrfs_free_space_cachep, right_info);
		merged = true;
	}

	if (left_info && !left_info->bitmap &&
	    left_info->offset + left_info->bytes == offset) {
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

int __btrfs_add_free_space(struct btrfs_free_space_ctl *ctl,
			   u64 offset, u64 bytes)
{
	struct btrfs_free_space *info;
	int ret = 0;

	info = kmem_cache_zalloc(btrfs_free_space_cachep, GFP_NOFS);
	if (!info)
		return -ENOMEM;

	info->offset = offset;
	info->bytes = bytes;

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
	ret = link_free_space(ctl, info);
	if (ret)
		kmem_cache_free(btrfs_free_space_cachep, info);
out:
	spin_unlock(&ctl->tree_lock);

	if (ret) {
		printk(KERN_CRIT "btrfs: unable to add free space :%d\n", ret);
		BUG_ON(ret == -EEXIST);
	}

	return ret;
}

int btrfs_remove_free_space(struct btrfs_block_group_cache *block_group,
			    u64 offset, u64 bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *info;
	struct btrfs_free_space *next_info = NULL;
	int ret = 0;

	spin_lock(&ctl->tree_lock);

again:
	info = tree_search_offset(ctl, offset, 0, 0);
	if (!info) {
		/*
		 * oops didn't find an extent that matched the space we wanted
		 * to remove, look for a bitmap instead
		 */
		info = tree_search_offset(ctl, offset_to_bitmap(ctl, offset),
					  1, 0);
		if (!info) {
			WARN_ON(1);
			goto out_lock;
		}
	}

	if (info->bytes < bytes && rb_next(&info->offset_index)) {
		u64 end;
		next_info = rb_entry(rb_next(&info->offset_index),
					     struct btrfs_free_space,
					     offset_index);

		if (next_info->bitmap)
			end = next_info->offset +
			      BITS_PER_BITMAP * ctl->unit - 1;
		else
			end = next_info->offset + next_info->bytes;

		if (next_info->bytes < bytes ||
		    next_info->offset > offset || offset > end) {
			printk(KERN_CRIT "Found free space at %llu, size %llu,"
			      " trying to use %llu\n",
			      (unsigned long long)info->offset,
			      (unsigned long long)info->bytes,
			      (unsigned long long)bytes);
			WARN_ON(1);
			ret = -EINVAL;
			goto out_lock;
		}

		info = next_info;
	}

	if (info->bytes == bytes) {
		unlink_free_space(ctl, info);
		if (info->bitmap) {
			kfree(info->bitmap);
			ctl->total_bitmaps--;
		}
		kmem_cache_free(btrfs_free_space_cachep, info);
		goto out_lock;
	}

	if (!info->bitmap && info->offset == offset) {
		unlink_free_space(ctl, info);
		info->offset += bytes;
		info->bytes -= bytes;
		link_free_space(ctl, info);
		goto out_lock;
	}

	if (!info->bitmap && info->offset <= offset &&
	    info->offset + info->bytes >= offset + bytes) {
		u64 old_start = info->offset;
		/*
		 * we're freeing space in the middle of the info,
		 * this can happen during tree log replay
		 *
		 * first unlink the old info and then
		 * insert it again after the hole we're creating
		 */
		unlink_free_space(ctl, info);
		if (offset + bytes < info->offset + info->bytes) {
			u64 old_end = info->offset + info->bytes;

			info->offset = offset + bytes;
			info->bytes = old_end - info->offset;
			ret = link_free_space(ctl, info);
			WARN_ON(ret);
			if (ret)
				goto out_lock;
		} else {
			/* the hole we're creating ends at the end
			 * of the info struct, just free the info
			 */
			kmem_cache_free(btrfs_free_space_cachep, info);
		}
		spin_unlock(&ctl->tree_lock);

		/* step two, insert a new info struct to cover
		 * anything before the hole
		 */
		ret = btrfs_add_free_space(block_group, old_start,
					   offset - old_start);
		WARN_ON(ret);
		goto out;
	}

	ret = remove_from_bitmap(ctl, info, &offset, &bytes);
	if (ret == -EAGAIN)
		goto again;
	BUG_ON(ret);
out_lock:
	spin_unlock(&ctl->tree_lock);
out:
	return ret;
}

void btrfs_dump_free_space(struct btrfs_block_group_cache *block_group,
			   u64 bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *info;
	struct rb_node *n;
	int count = 0;

	for (n = rb_first(&ctl->free_space_offset); n; n = rb_next(n)) {
		info = rb_entry(n, struct btrfs_free_space, offset_index);
		if (info->bytes >= bytes)
			count++;
		printk(KERN_CRIT "entry offset %llu, bytes %llu, bitmap %s\n",
		       (unsigned long long)info->offset,
		       (unsigned long long)info->bytes,
		       (info->bitmap) ? "yes" : "no");
	}
	printk(KERN_INFO "block group has cluster?: %s\n",
	       list_empty(&block_group->cluster_list) ? "no" : "yes");
	printk(KERN_INFO "%d blocks of free space at or bigger than bytes is"
	       "\n", count);
}

void btrfs_init_free_space_ctl(struct btrfs_block_group_cache *block_group)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;

	spin_lock_init(&ctl->tree_lock);
	ctl->unit = block_group->sectorsize;
	ctl->start = block_group->key.objectid;
	ctl->private = block_group;
	ctl->op = &free_space_op;

	/*
	 * we only want to have 32k of ram per block group for keeping
	 * track of free space, and if we pass 1/2 of that we want to
	 * start converting things over to using bitmaps
	 */
	ctl->extents_thresh = ((1024 * 32) / 2) /
				sizeof(struct btrfs_free_space);
}

/*
 * for a given cluster, put all of its extents back into the free
 * space cache.  If the block group passed doesn't match the block group
 * pointed to by the cluster, someone else raced in and freed the
 * cluster already.  In that case, we just return without changing anything
 */
static int
__btrfs_return_cluster_to_free_space(
			     struct btrfs_block_group_cache *block_group,
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

		bitmap = (entry->bitmap != NULL);
		if (!bitmap)
			try_merge_free_space(ctl, entry, false);
		tree_insert_offset(&ctl->free_space_offset,
				   entry->offset, &entry->offset_index, bitmap);
	}
	cluster->root = RB_ROOT;

out:
	spin_unlock(&cluster->lock);
	btrfs_put_block_group(block_group);
	return 0;
}

void __btrfs_remove_free_space_cache_locked(struct btrfs_free_space_ctl *ctl)
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
		if (need_resched()) {
			spin_unlock(&ctl->tree_lock);
			cond_resched();
			spin_lock(&ctl->tree_lock);
		}
	}
}

void __btrfs_remove_free_space_cache(struct btrfs_free_space_ctl *ctl)
{
	spin_lock(&ctl->tree_lock);
	__btrfs_remove_free_space_cache_locked(ctl);
	spin_unlock(&ctl->tree_lock);
}

void btrfs_remove_free_space_cache(struct btrfs_block_group_cache *block_group)
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
		if (need_resched()) {
			spin_unlock(&ctl->tree_lock);
			cond_resched();
			spin_lock(&ctl->tree_lock);
		}
	}
	__btrfs_remove_free_space_cache_locked(ctl);
	spin_unlock(&ctl->tree_lock);

}

u64 btrfs_find_space_for_alloc(struct btrfs_block_group_cache *block_group,
			       u64 offset, u64 bytes, u64 empty_size)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry = NULL;
	u64 bytes_search = bytes + empty_size;
	u64 ret = 0;

	spin_lock(&ctl->tree_lock);
	entry = find_free_space(ctl, &offset, &bytes_search);
	if (!entry)
		goto out;

	ret = offset;
	if (entry->bitmap) {
		bitmap_clear_bits(ctl, entry, offset, bytes);
		if (!entry->bytes)
			free_bitmap(ctl, entry);
	} else {
		unlink_free_space(ctl, entry);
		entry->offset += bytes;
		entry->bytes -= bytes;
		if (!entry->bytes)
			kmem_cache_free(btrfs_free_space_cachep, entry);
		else
			link_free_space(ctl, entry);
	}

out:
	spin_unlock(&ctl->tree_lock);

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
int btrfs_return_cluster_to_free_space(
			       struct btrfs_block_group_cache *block_group,
			       struct btrfs_free_cluster *cluster)
{
	struct btrfs_free_space_ctl *ctl;
	int ret;

	/* first, get a safe pointer to the block group */
	spin_lock(&cluster->lock);
	if (!block_group) {
		block_group = cluster->block_group;
		if (!block_group) {
			spin_unlock(&cluster->lock);
			return 0;
		}
	} else if (cluster->block_group != block_group) {
		/* someone else has already freed it don't redo their work */
		spin_unlock(&cluster->lock);
		return 0;
	}
	atomic_inc(&block_group->count);
	spin_unlock(&cluster->lock);

	ctl = block_group->free_space_ctl;

	/* now return any extents the cluster had on it */
	spin_lock(&ctl->tree_lock);
	ret = __btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&ctl->tree_lock);

	/* finally drop our ref */
	btrfs_put_block_group(block_group);
	return ret;
}

static u64 btrfs_alloc_from_bitmap(struct btrfs_block_group_cache *block_group,
				   struct btrfs_free_cluster *cluster,
				   struct btrfs_free_space *entry,
				   u64 bytes, u64 min_start)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	int err;
	u64 search_start = cluster->window_start;
	u64 search_bytes = bytes;
	u64 ret = 0;

	search_start = min_start;
	search_bytes = bytes;

	err = search_bitmap(ctl, entry, &search_start, &search_bytes);
	if (err)
		return 0;

	ret = search_start;
	bitmap_clear_bits(ctl, entry, ret, bytes);

	return ret;
}

/*
 * given a cluster, try to allocate 'bytes' from it, returns 0
 * if it couldn't find anything suitably large, or a logical disk offset
 * if things worked out
 */
u64 btrfs_alloc_from_cluster(struct btrfs_block_group_cache *block_group,
			     struct btrfs_free_cluster *cluster, u64 bytes,
			     u64 min_start)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
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
	while(1) {
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
						      min_start);
			if (ret == 0) {
				node = rb_next(&entry->offset_index);
				if (!node)
					break;
				entry = rb_entry(node, struct btrfs_free_space,
						 offset_index);
				continue;
			}
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

	ctl->free_space -= bytes;
	if (entry->bytes == 0) {
		ctl->free_extents--;
		if (entry->bitmap) {
			kfree(entry->bitmap);
			ctl->total_bitmaps--;
			ctl->op->recalc_thresholds(ctl);
		}
		kmem_cache_free(btrfs_free_space_cachep, entry);
	}

	spin_unlock(&ctl->tree_lock);

	return ret;
}

static int btrfs_bitmap_cluster(struct btrfs_block_group_cache *block_group,
				struct btrfs_free_space *entry,
				struct btrfs_free_cluster *cluster,
				u64 offset, u64 bytes, u64 min_bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	unsigned long next_zero;
	unsigned long i;
	unsigned long search_bits;
	unsigned long total_bits;
	unsigned long found_bits;
	unsigned long start = 0;
	unsigned long total_found = 0;
	int ret;
	bool found = false;

	i = offset_to_bit(entry->offset, block_group->sectorsize,
			  max_t(u64, offset, entry->offset));
	search_bits = bytes_to_bits(bytes, block_group->sectorsize);
	total_bits = bytes_to_bits(min_bytes, block_group->sectorsize);

again:
	found_bits = 0;
	for (i = find_next_bit(entry->bitmap, BITS_PER_BITMAP, i);
	     i < BITS_PER_BITMAP;
	     i = find_next_bit(entry->bitmap, BITS_PER_BITMAP, i + 1)) {
		next_zero = find_next_zero_bit(entry->bitmap,
					       BITS_PER_BITMAP, i);
		if (next_zero - i >= search_bits) {
			found_bits = next_zero - i;
			break;
		}
		i = next_zero;
	}

	if (!found_bits)
		return -ENOSPC;

	if (!found) {
		start = i;
		found = true;
	}

	total_found += found_bits;

	if (cluster->max_size < found_bits * block_group->sectorsize)
		cluster->max_size = found_bits * block_group->sectorsize;

	if (total_found < total_bits) {
		i = find_next_bit(entry->bitmap, BITS_PER_BITMAP, next_zero);
		if (i - start > total_bits * 2) {
			total_found = 0;
			cluster->max_size = 0;
			found = false;
		}
		goto again;
	}

	cluster->window_start = start * block_group->sectorsize +
		entry->offset;
	rb_erase(&entry->offset_index, &ctl->free_space_offset);
	ret = tree_insert_offset(&cluster->root, entry->offset,
				 &entry->offset_index, 1);
	BUG_ON(ret);

	return 0;
}

/*
 * This searches the block group for just extents to fill the cluster with.
 */
static noinline int
setup_cluster_no_bitmap(struct btrfs_block_group_cache *block_group,
			struct btrfs_free_cluster *cluster,
			struct list_head *bitmaps, u64 offset, u64 bytes,
			u64 min_bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *first = NULL;
	struct btrfs_free_space *entry = NULL;
	struct btrfs_free_space *prev = NULL;
	struct btrfs_free_space *last;
	struct rb_node *node;
	u64 window_start;
	u64 window_free;
	u64 max_extent;
	u64 max_gap = 128 * 1024;

	entry = tree_search_offset(ctl, offset, 0, 1);
	if (!entry)
		return -ENOSPC;

	/*
	 * We don't want bitmaps, so just move along until we find a normal
	 * extent entry.
	 */
	while (entry->bitmap) {
		if (list_empty(&entry->list))
			list_add_tail(&entry->list, bitmaps);
		node = rb_next(&entry->offset_index);
		if (!node)
			return -ENOSPC;
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
	}

	window_start = entry->offset;
	window_free = entry->bytes;
	max_extent = entry->bytes;
	first = entry;
	last = entry;
	prev = entry;

	while (window_free <= min_bytes) {
		node = rb_next(&entry->offset_index);
		if (!node)
			return -ENOSPC;
		entry = rb_entry(node, struct btrfs_free_space, offset_index);

		if (entry->bitmap) {
			if (list_empty(&entry->list))
				list_add_tail(&entry->list, bitmaps);
			continue;
		}

		/*
		 * we haven't filled the empty size and the window is
		 * very large.  reset and try again
		 */
		if (entry->offset - (prev->offset + prev->bytes) > max_gap ||
		    entry->offset - window_start > (min_bytes * 2)) {
			first = entry;
			window_start = entry->offset;
			window_free = entry->bytes;
			last = entry;
			max_extent = entry->bytes;
		} else {
			last = entry;
			window_free += entry->bytes;
			if (entry->bytes > max_extent)
				max_extent = entry->bytes;
		}
		prev = entry;
	}

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
		if (entry->bitmap)
			continue;

		rb_erase(&entry->offset_index, &ctl->free_space_offset);
		ret = tree_insert_offset(&cluster->root, entry->offset,
					 &entry->offset_index, 0);
		BUG_ON(ret);
	} while (node && entry != last);

	cluster->max_size = max_extent;

	return 0;
}

/*
 * This specifically looks for bitmaps that may work in the cluster, we assume
 * that we have already failed to find extents that will work.
 */
static noinline int
setup_cluster_bitmap(struct btrfs_block_group_cache *block_group,
		     struct btrfs_free_cluster *cluster,
		     struct list_head *bitmaps, u64 offset, u64 bytes,
		     u64 min_bytes)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry;
	struct rb_node *node;
	int ret = -ENOSPC;

	if (ctl->total_bitmaps == 0)
		return -ENOSPC;

	/*
	 * First check our cached list of bitmaps and see if there is an entry
	 * here that will work.
	 */
	list_for_each_entry(entry, bitmaps, list) {
		if (entry->bytes < min_bytes)
			continue;
		ret = btrfs_bitmap_cluster(block_group, entry, cluster, offset,
					   bytes, min_bytes);
		if (!ret)
			return 0;
	}

	/*
	 * If we do have entries on our list and we are here then we didn't find
	 * anything, so go ahead and get the next entry after the last entry in
	 * this list and start the search from there.
	 */
	if (!list_empty(bitmaps)) {
		entry = list_entry(bitmaps->prev, struct btrfs_free_space,
				   list);
		node = rb_next(&entry->offset_index);
		if (!node)
			return -ENOSPC;
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		goto search;
	}

	entry = tree_search_offset(ctl, offset_to_bitmap(ctl, offset), 0, 1);
	if (!entry)
		return -ENOSPC;

search:
	node = &entry->offset_index;
	do {
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		node = rb_next(&entry->offset_index);
		if (!entry->bitmap)
			continue;
		if (entry->bytes < min_bytes)
			continue;
		ret = btrfs_bitmap_cluster(block_group, entry, cluster, offset,
					   bytes, min_bytes);
	} while (ret && node);

	return ret;
}

/*
 * here we try to find a cluster of blocks in a block group.  The goal
 * is to find at least bytes free and up to empty_size + bytes free.
 * We might not find them all in one contiguous area.
 *
 * returns zero and sets up cluster if things worked out, otherwise
 * it returns -enospc
 */
int btrfs_find_space_cluster(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_block_group_cache *block_group,
			     struct btrfs_free_cluster *cluster,
			     u64 offset, u64 bytes, u64 empty_size)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct list_head bitmaps;
	struct btrfs_free_space *entry, *tmp;
	u64 min_bytes;
	int ret;

	/* for metadata, allow allocates with more holes */
	if (btrfs_test_opt(root, SSD_SPREAD)) {
		min_bytes = bytes + empty_size;
	} else if (block_group->flags & BTRFS_BLOCK_GROUP_METADATA) {
		/*
		 * we want to do larger allocations when we are
		 * flushing out the delayed refs, it helps prevent
		 * making more work as we go along.
		 */
		if (trans->transaction->delayed_refs.flushing)
			min_bytes = max(bytes, (bytes + empty_size) >> 1);
		else
			min_bytes = max(bytes, (bytes + empty_size) >> 4);
	} else
		min_bytes = max(bytes, (bytes + empty_size) >> 2);

	spin_lock(&ctl->tree_lock);

	/*
	 * If we know we don't have enough space to make a cluster don't even
	 * bother doing all the work to try and find one.
	 */
	if (ctl->free_space < min_bytes) {
		spin_unlock(&ctl->tree_lock);
		return -ENOSPC;
	}

	spin_lock(&cluster->lock);

	/* someone already found a cluster, hooray */
	if (cluster->block_group) {
		ret = 0;
		goto out;
	}

	INIT_LIST_HEAD(&bitmaps);
	ret = setup_cluster_no_bitmap(block_group, cluster, &bitmaps, offset,
				      bytes, min_bytes);
	if (ret)
		ret = setup_cluster_bitmap(block_group, cluster, &bitmaps,
					   offset, bytes, min_bytes);

	/* Clear our temporary list */
	list_for_each_entry_safe(entry, tmp, &bitmaps, list)
		list_del_init(&entry->list);

	if (!ret) {
		atomic_inc(&block_group->count);
		list_add_tail(&cluster->block_group_list,
			      &block_group->cluster_list);
		cluster->block_group = block_group;
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
	INIT_LIST_HEAD(&cluster->block_group_list);
	cluster->block_group = NULL;
}

int btrfs_trim_block_group(struct btrfs_block_group_cache *block_group,
			   u64 *trimmed, u64 start, u64 end, u64 minlen)
{
	struct btrfs_free_space_ctl *ctl = block_group->free_space_ctl;
	struct btrfs_free_space *entry = NULL;
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	u64 bytes = 0;
	u64 actually_trimmed;
	int ret = 0;

	*trimmed = 0;

	while (start < end) {
		spin_lock(&ctl->tree_lock);

		if (ctl->free_space < minlen) {
			spin_unlock(&ctl->tree_lock);
			break;
		}

		entry = tree_search_offset(ctl, start, 0, 1);
		if (!entry)
			entry = tree_search_offset(ctl,
						   offset_to_bitmap(ctl, start),
						   1, 1);

		if (!entry || entry->offset >= end) {
			spin_unlock(&ctl->tree_lock);
			break;
		}

		if (entry->bitmap) {
			ret = search_bitmap(ctl, entry, &start, &bytes);
			if (!ret) {
				if (start >= end) {
					spin_unlock(&ctl->tree_lock);
					break;
				}
				bytes = min(bytes, end - start);
				bitmap_clear_bits(ctl, entry, start, bytes);
				if (entry->bytes == 0)
					free_bitmap(ctl, entry);
			} else {
				start = entry->offset + BITS_PER_BITMAP *
					block_group->sectorsize;
				spin_unlock(&ctl->tree_lock);
				ret = 0;
				continue;
			}
		} else {
			start = entry->offset;
			bytes = min(entry->bytes, end - start);
			unlink_free_space(ctl, entry);
			kmem_cache_free(btrfs_free_space_cachep, entry);
		}

		spin_unlock(&ctl->tree_lock);

		if (bytes >= minlen) {
			int update_ret;
			update_ret = btrfs_update_reserved_bytes(block_group,
								 bytes, 1, 1);

			ret = btrfs_error_discard_extent(fs_info->extent_root,
							 start,
							 bytes,
							 &actually_trimmed);

			btrfs_add_free_space(block_group, start, bytes);
			if (!update_ret)
				btrfs_update_reserved_bytes(block_group,
							    bytes, 0, 1);

			if (ret)
				break;
			*trimmed += actually_trimmed;
		}
		start += bytes;
		bytes = 0;

		if (fatal_signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		cond_resched();
	}

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

		ret = search_bitmap(ctl, entry, &offset, &count);
		BUG_ON(ret);

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

	spin_lock(&root->cache_lock);
	if (root->cache_inode)
		inode = igrab(root->cache_inode);
	spin_unlock(&root->cache_lock);
	if (inode)
		return inode;

	inode = __lookup_free_space_inode(root, path, 0);
	if (IS_ERR(inode))
		return inode;

	spin_lock(&root->cache_lock);
	if (!btrfs_fs_closing(root->fs_info))
		root->cache_inode = igrab(inode);
	spin_unlock(&root->cache_lock);

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

	if (!btrfs_test_opt(root, INODE_MAP_CACHE))
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
		printk(KERN_ERR "btrfs: failed to load free ino cache for "
		       "root %llu\n", root->root_key.objectid);
out_put:
	iput(inode);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_write_out_ino_cache(struct btrfs_root *root,
			      struct btrfs_trans_handle *trans,
			      struct btrfs_path *path)
{
	struct btrfs_free_space_ctl *ctl = root->free_ino_ctl;
	struct inode *inode;
	int ret;

	if (!btrfs_test_opt(root, INODE_MAP_CACHE))
		return 0;

	inode = lookup_free_ino_inode(root, path);
	if (IS_ERR(inode))
		return 0;

	ret = __btrfs_write_out_cache(root, inode, ctl, NULL, trans, path, 0);
	if (ret < 0)
		printk(KERN_ERR "btrfs: failed to write free ino cache "
		       "for root %llu\n", root->root_key.objectid);

	iput(inode);
	return ret;
}
