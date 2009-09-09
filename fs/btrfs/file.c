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
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/compat.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "tree-log.h"
#include "locking.h"
#include "compat.h"


/* simple helper to fault in pages and copy.  This should go away
 * and be replaced with calls into generic code.
 */
static noinline int btrfs_copy_from_user(loff_t pos, int num_pages,
					 int write_bytes,
					 struct page **prepared_pages,
					 const char __user *buf)
{
	long page_fault = 0;
	int i;
	int offset = pos & (PAGE_CACHE_SIZE - 1);

	for (i = 0; i < num_pages && write_bytes > 0; i++, offset = 0) {
		size_t count = min_t(size_t,
				     PAGE_CACHE_SIZE - offset, write_bytes);
		struct page *page = prepared_pages[i];
		fault_in_pages_readable(buf, count);

		/* Copy data from userspace to the current page */
		kmap(page);
		page_fault = __copy_from_user(page_address(page) + offset,
					      buf, count);
		/* Flush processor's dcache for this page */
		flush_dcache_page(page);
		kunmap(page);
		buf += count;
		write_bytes -= count;

		if (page_fault)
			break;
	}
	return page_fault ? -EFAULT : 0;
}

/*
 * unlocks pages after btrfs_file_write is done with them
 */
static noinline void btrfs_drop_pages(struct page **pages, size_t num_pages)
{
	size_t i;
	for (i = 0; i < num_pages; i++) {
		if (!pages[i])
			break;
		/* page checked is some magic around finding pages that
		 * have been modified without going through btrfs_set_page_dirty
		 * clear it here
		 */
		ClearPageChecked(pages[i]);
		unlock_page(pages[i]);
		mark_page_accessed(pages[i]);
		page_cache_release(pages[i]);
	}
}

/*
 * after copy_from_user, pages need to be dirtied and we need to make
 * sure holes are created between the current EOF and the start of
 * any next extents (if required).
 *
 * this also makes the decision about creating an inline extent vs
 * doing real data extents, marking pages dirty and delalloc as required.
 */
static noinline int dirty_and_release_pages(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct file *file,
				   struct page **pages,
				   size_t num_pages,
				   loff_t pos,
				   size_t write_bytes)
{
	int err = 0;
	int i;
	struct inode *inode = fdentry(file)->d_inode;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	u64 hint_byte;
	u64 num_bytes;
	u64 start_pos;
	u64 end_of_last_block;
	u64 end_pos = pos + write_bytes;
	loff_t isize = i_size_read(inode);

	start_pos = pos & ~((u64)root->sectorsize - 1);
	num_bytes = (write_bytes + pos - start_pos +
		    root->sectorsize - 1) & ~((u64)root->sectorsize - 1);

	end_of_last_block = start_pos + num_bytes - 1;

	lock_extent(io_tree, start_pos, end_of_last_block, GFP_NOFS);
	trans = btrfs_join_transaction(root, 1);
	if (!trans) {
		err = -ENOMEM;
		goto out_unlock;
	}
	btrfs_set_trans_block_group(trans, inode);
	hint_byte = 0;

	set_extent_uptodate(io_tree, start_pos, end_of_last_block, GFP_NOFS);

	/* check for reserved extents on each page, we don't want
	 * to reset the delalloc bit on things that already have
	 * extents reserved.
	 */
	btrfs_set_extent_delalloc(inode, start_pos, end_of_last_block);
	for (i = 0; i < num_pages; i++) {
		struct page *p = pages[i];
		SetPageUptodate(p);
		ClearPageChecked(p);
		set_page_dirty(p);
	}
	if (end_pos > isize) {
		i_size_write(inode, end_pos);
		btrfs_update_inode(trans, root, inode);
	}
	err = btrfs_end_transaction(trans, root);
out_unlock:
	unlock_extent(io_tree, start_pos, end_of_last_block, GFP_NOFS);
	return err;
}

/*
 * this drops all the extents in the cache that intersect the range
 * [start, end].  Existing extents are split as required.
 */
int btrfs_drop_extent_cache(struct inode *inode, u64 start, u64 end,
			    int skip_pinned)
{
	struct extent_map *em;
	struct extent_map *split = NULL;
	struct extent_map *split2 = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	u64 len = end - start + 1;
	int ret;
	int testend = 1;
	unsigned long flags;
	int compressed = 0;

	WARN_ON(end < start);
	if (end == (u64)-1) {
		len = (u64)-1;
		testend = 0;
	}
	while (1) {
		if (!split)
			split = alloc_extent_map(GFP_NOFS);
		if (!split2)
			split2 = alloc_extent_map(GFP_NOFS);

		spin_lock(&em_tree->lock);
		em = lookup_extent_mapping(em_tree, start, len);
		if (!em) {
			spin_unlock(&em_tree->lock);
			break;
		}
		flags = em->flags;
		if (skip_pinned && test_bit(EXTENT_FLAG_PINNED, &em->flags)) {
			spin_unlock(&em_tree->lock);
			if (em->start <= start &&
			    (!testend || em->start + em->len >= start + len)) {
				free_extent_map(em);
				break;
			}
			if (start < em->start) {
				len = em->start - start;
			} else {
				len = start + len - (em->start + em->len);
				start = em->start + em->len;
			}
			free_extent_map(em);
			continue;
		}
		compressed = test_bit(EXTENT_FLAG_COMPRESSED, &em->flags);
		clear_bit(EXTENT_FLAG_PINNED, &em->flags);
		remove_extent_mapping(em_tree, em);

		if (em->block_start < EXTENT_MAP_LAST_BYTE &&
		    em->start < start) {
			split->start = em->start;
			split->len = start - em->start;
			split->orig_start = em->orig_start;
			split->block_start = em->block_start;

			if (compressed)
				split->block_len = em->block_len;
			else
				split->block_len = split->len;

			split->bdev = em->bdev;
			split->flags = flags;
			ret = add_extent_mapping(em_tree, split);
			BUG_ON(ret);
			free_extent_map(split);
			split = split2;
			split2 = NULL;
		}
		if (em->block_start < EXTENT_MAP_LAST_BYTE &&
		    testend && em->start + em->len > start + len) {
			u64 diff = start + len - em->start;

			split->start = start + len;
			split->len = em->start + em->len - (start + len);
			split->bdev = em->bdev;
			split->flags = flags;

			if (compressed) {
				split->block_len = em->block_len;
				split->block_start = em->block_start;
				split->orig_start = em->orig_start;
			} else {
				split->block_len = split->len;
				split->block_start = em->block_start + diff;
				split->orig_start = split->start;
			}

			ret = add_extent_mapping(em_tree, split);
			BUG_ON(ret);
			free_extent_map(split);
			split = NULL;
		}
		spin_unlock(&em_tree->lock);

		/* once for us */
		free_extent_map(em);
		/* once for the tree*/
		free_extent_map(em);
	}
	if (split)
		free_extent_map(split);
	if (split2)
		free_extent_map(split2);
	return 0;
}

/*
 * this is very complex, but the basic idea is to drop all extents
 * in the range start - end.  hint_block is filled in with a block number
 * that would be a good hint to the block allocator for this file.
 *
 * If an extent intersects the range but is not entirely inside the range
 * it is either truncated or split.  Anything entirely inside the range
 * is deleted from the tree.
 *
 * inline_limit is used to tell this code which offsets in the file to keep
 * if they contain inline extents.
 */
noinline int btrfs_drop_extents(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root, struct inode *inode,
		       u64 start, u64 end, u64 locked_end,
		       u64 inline_limit, u64 *hint_byte)
{
	u64 extent_end = 0;
	u64 search_start = start;
	u64 ram_bytes = 0;
	u64 disk_bytenr = 0;
	u64 orig_locked_end = locked_end;
	u8 compression;
	u8 encryption;
	u16 other_encoding = 0;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *extent;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_file_extent_item old;
	int keep;
	int slot;
	int bookend;
	int found_type = 0;
	int found_extent;
	int found_inline;
	int recow;
	int ret;

	inline_limit = 0;
	btrfs_drop_extent_cache(inode, start, end - 1, 0);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	while (1) {
		recow = 0;
		btrfs_release_path(root, path);
		ret = btrfs_lookup_file_extent(trans, root, path, inode->i_ino,
					       search_start, -1);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			if (path->slots[0] == 0) {
				ret = 0;
				goto out;
			}
			path->slots[0]--;
		}
next_slot:
		keep = 0;
		bookend = 0;
		found_extent = 0;
		found_inline = 0;
		compression = 0;
		encryption = 0;
		extent = NULL;
		leaf = path->nodes[0];
		slot = path->slots[0];
		ret = 0;
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (btrfs_key_type(&key) == BTRFS_EXTENT_DATA_KEY &&
		    key.offset >= end) {
			goto out;
		}
		if (btrfs_key_type(&key) > BTRFS_EXTENT_DATA_KEY ||
		    key.objectid != inode->i_ino) {
			goto out;
		}
		if (recow) {
			search_start = max(key.offset, start);
			continue;
		}
		if (btrfs_key_type(&key) == BTRFS_EXTENT_DATA_KEY) {
			extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);
			found_type = btrfs_file_extent_type(leaf, extent);
			compression = btrfs_file_extent_compression(leaf,
								    extent);
			encryption = btrfs_file_extent_encryption(leaf,
								  extent);
			other_encoding = btrfs_file_extent_other_encoding(leaf,
								  extent);
			if (found_type == BTRFS_FILE_EXTENT_REG ||
			    found_type == BTRFS_FILE_EXTENT_PREALLOC) {
				extent_end =
				     btrfs_file_extent_disk_bytenr(leaf,
								   extent);
				if (extent_end)
					*hint_byte = extent_end;

				extent_end = key.offset +
				     btrfs_file_extent_num_bytes(leaf, extent);
				ram_bytes = btrfs_file_extent_ram_bytes(leaf,
								extent);
				found_extent = 1;
			} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
				found_inline = 1;
				extent_end = key.offset +
				     btrfs_file_extent_inline_len(leaf, extent);
			}
		} else {
			extent_end = search_start;
		}

		/* we found nothing we can drop */
		if ((!found_extent && !found_inline) ||
		    search_start >= extent_end) {
			int nextret;
			u32 nritems;
			nritems = btrfs_header_nritems(leaf);
			if (slot >= nritems - 1) {
				nextret = btrfs_next_leaf(root, path);
				if (nextret)
					goto out;
				recow = 1;
			} else {
				path->slots[0]++;
			}
			goto next_slot;
		}

		if (end <= extent_end && start >= key.offset && found_inline)
			*hint_byte = EXTENT_MAP_INLINE;

		if (found_extent) {
			read_extent_buffer(leaf, &old, (unsigned long)extent,
					   sizeof(old));
		}

		if (end < extent_end && end >= key.offset) {
			bookend = 1;
			if (found_inline && start <= key.offset)
				keep = 1;
		}

		if (bookend && found_extent) {
			if (locked_end < extent_end) {
				ret = try_lock_extent(&BTRFS_I(inode)->io_tree,
						locked_end, extent_end - 1,
						GFP_NOFS);
				if (!ret) {
					btrfs_release_path(root, path);
					lock_extent(&BTRFS_I(inode)->io_tree,
						locked_end, extent_end - 1,
						GFP_NOFS);
					locked_end = extent_end;
					continue;
				}
				locked_end = extent_end;
			}
			disk_bytenr = le64_to_cpu(old.disk_bytenr);
			if (disk_bytenr != 0) {
				ret = btrfs_inc_extent_ref(trans, root,
					   disk_bytenr,
					   le64_to_cpu(old.disk_num_bytes), 0,
					   root->root_key.objectid,
					   key.objectid, key.offset -
					   le64_to_cpu(old.offset));
				BUG_ON(ret);
			}
		}

		if (found_inline) {
			u64 mask = root->sectorsize - 1;
			search_start = (extent_end + mask) & ~mask;
		} else
			search_start = extent_end;

		/* truncate existing extent */
		if (start > key.offset) {
			u64 new_num;
			u64 old_num;
			keep = 1;
			WARN_ON(start & (root->sectorsize - 1));
			if (found_extent) {
				new_num = start - key.offset;
				old_num = btrfs_file_extent_num_bytes(leaf,
								      extent);
				*hint_byte =
					btrfs_file_extent_disk_bytenr(leaf,
								      extent);
				if (btrfs_file_extent_disk_bytenr(leaf,
								  extent)) {
					inode_sub_bytes(inode, old_num -
							new_num);
				}
				btrfs_set_file_extent_num_bytes(leaf,
							extent, new_num);
				btrfs_mark_buffer_dirty(leaf);
			} else if (key.offset < inline_limit &&
				   (end > extent_end) &&
				   (inline_limit < extent_end)) {
				u32 new_size;
				new_size = btrfs_file_extent_calc_inline_size(
						   inline_limit - key.offset);
				inode_sub_bytes(inode, extent_end -
						inline_limit);
				btrfs_set_file_extent_ram_bytes(leaf, extent,
							new_size);
				if (!compression && !encryption) {
					btrfs_truncate_item(trans, root, path,
							    new_size, 1);
				}
			}
		}
		/* delete the entire extent */
		if (!keep) {
			if (found_inline)
				inode_sub_bytes(inode, extent_end -
						key.offset);
			ret = btrfs_del_item(trans, root, path);
			/* TODO update progress marker and return */
			BUG_ON(ret);
			extent = NULL;
			btrfs_release_path(root, path);
			/* the extent will be freed later */
		}
		if (bookend && found_inline && start <= key.offset) {
			u32 new_size;
			new_size = btrfs_file_extent_calc_inline_size(
						   extent_end - end);
			inode_sub_bytes(inode, end - key.offset);
			btrfs_set_file_extent_ram_bytes(leaf, extent,
							new_size);
			if (!compression && !encryption)
				ret = btrfs_truncate_item(trans, root, path,
							  new_size, 0);
			BUG_ON(ret);
		}
		/* create bookend, splitting the extent in two */
		if (bookend && found_extent) {
			struct btrfs_key ins;
			ins.objectid = inode->i_ino;
			ins.offset = end;
			btrfs_set_key_type(&ins, BTRFS_EXTENT_DATA_KEY);

			btrfs_release_path(root, path);
			path->leave_spinning = 1;
			ret = btrfs_insert_empty_item(trans, root, path, &ins,
						      sizeof(*extent));
			BUG_ON(ret);

			leaf = path->nodes[0];
			extent = btrfs_item_ptr(leaf, path->slots[0],
						struct btrfs_file_extent_item);
			write_extent_buffer(leaf, &old,
					    (unsigned long)extent, sizeof(old));

			btrfs_set_file_extent_compression(leaf, extent,
							  compression);
			btrfs_set_file_extent_encryption(leaf, extent,
							 encryption);
			btrfs_set_file_extent_other_encoding(leaf, extent,
							     other_encoding);
			btrfs_set_file_extent_offset(leaf, extent,
				    le64_to_cpu(old.offset) + end - key.offset);
			WARN_ON(le64_to_cpu(old.num_bytes) <
				(extent_end - end));
			btrfs_set_file_extent_num_bytes(leaf, extent,
							extent_end - end);

			/*
			 * set the ram bytes to the size of the full extent
			 * before splitting.  This is a worst case flag,
			 * but its the best we can do because we don't know
			 * how splitting affects compression
			 */
			btrfs_set_file_extent_ram_bytes(leaf, extent,
							ram_bytes);
			btrfs_set_file_extent_type(leaf, extent, found_type);

			btrfs_unlock_up_safe(path, 1);
			btrfs_mark_buffer_dirty(path->nodes[0]);
			btrfs_set_lock_blocking(path->nodes[0]);

			path->leave_spinning = 0;
			btrfs_release_path(root, path);
			if (disk_bytenr != 0)
				inode_add_bytes(inode, extent_end - end);
		}

		if (found_extent && !keep) {
			u64 old_disk_bytenr = le64_to_cpu(old.disk_bytenr);

			if (old_disk_bytenr != 0) {
				inode_sub_bytes(inode,
						le64_to_cpu(old.num_bytes));
				ret = btrfs_free_extent(trans, root,
						old_disk_bytenr,
						le64_to_cpu(old.disk_num_bytes),
						0, root->root_key.objectid,
						key.objectid, key.offset -
						le64_to_cpu(old.offset));
				BUG_ON(ret);
				*hint_byte = old_disk_bytenr;
			}
		}

		if (search_start >= end) {
			ret = 0;
			goto out;
		}
	}
out:
	btrfs_free_path(path);
	if (locked_end > orig_locked_end) {
		unlock_extent(&BTRFS_I(inode)->io_tree, orig_locked_end,
			      locked_end - 1, GFP_NOFS);
	}
	return ret;
}

static int extent_mergeable(struct extent_buffer *leaf, int slot,
			    u64 objectid, u64 bytenr, u64 *start, u64 *end)
{
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	u64 extent_end;

	if (slot < 0 || slot >= btrfs_header_nritems(leaf))
		return 0;

	btrfs_item_key_to_cpu(leaf, &key, slot);
	if (key.objectid != objectid || key.type != BTRFS_EXTENT_DATA_KEY)
		return 0;

	fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
	if (btrfs_file_extent_type(leaf, fi) != BTRFS_FILE_EXTENT_REG ||
	    btrfs_file_extent_disk_bytenr(leaf, fi) != bytenr ||
	    btrfs_file_extent_compression(leaf, fi) ||
	    btrfs_file_extent_encryption(leaf, fi) ||
	    btrfs_file_extent_other_encoding(leaf, fi))
		return 0;

	extent_end = key.offset + btrfs_file_extent_num_bytes(leaf, fi);
	if ((*start && *start != key.offset) || (*end && *end != extent_end))
		return 0;

	*start = key.offset;
	*end = extent_end;
	return 1;
}

/*
 * Mark extent in the range start - end as written.
 *
 * This changes extent type from 'pre-allocated' to 'regular'. If only
 * part of extent is marked as written, the extent will be split into
 * two or three.
 */
int btrfs_mark_extent_written(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct inode *inode, u64 start, u64 end)
{
	struct extent_buffer *leaf;
	struct btrfs_path *path;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	u64 bytenr;
	u64 num_bytes;
	u64 extent_end;
	u64 orig_offset;
	u64 other_start;
	u64 other_end;
	u64 split = start;
	u64 locked_end = end;
	int extent_type;
	int split_end = 1;
	int ret;

	btrfs_drop_extent_cache(inode, start, end - 1, 0);

	path = btrfs_alloc_path();
	BUG_ON(!path);
again:
	key.objectid = inode->i_ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	if (split == start)
		key.offset = split;
	else
		key.offset = split - 1;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0 && path->slots[0] > 0)
		path->slots[0]--;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	BUG_ON(key.objectid != inode->i_ino ||
	       key.type != BTRFS_EXTENT_DATA_KEY);
	fi = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);
	extent_type = btrfs_file_extent_type(leaf, fi);
	BUG_ON(extent_type != BTRFS_FILE_EXTENT_PREALLOC);
	extent_end = key.offset + btrfs_file_extent_num_bytes(leaf, fi);
	BUG_ON(key.offset > start || extent_end < end);

	bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
	num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
	orig_offset = key.offset - btrfs_file_extent_offset(leaf, fi);

	if (key.offset == start)
		split = end;

	if (key.offset == start && extent_end == end) {
		int del_nr = 0;
		int del_slot = 0;
		other_start = end;
		other_end = 0;
		if (extent_mergeable(leaf, path->slots[0] + 1, inode->i_ino,
				     bytenr, &other_start, &other_end)) {
			extent_end = other_end;
			del_slot = path->slots[0] + 1;
			del_nr++;
			ret = btrfs_free_extent(trans, root, bytenr, num_bytes,
						0, root->root_key.objectid,
						inode->i_ino, orig_offset);
			BUG_ON(ret);
		}
		other_start = 0;
		other_end = start;
		if (extent_mergeable(leaf, path->slots[0] - 1, inode->i_ino,
				     bytenr, &other_start, &other_end)) {
			key.offset = other_start;
			del_slot = path->slots[0];
			del_nr++;
			ret = btrfs_free_extent(trans, root, bytenr, num_bytes,
						0, root->root_key.objectid,
						inode->i_ino, orig_offset);
			BUG_ON(ret);
		}
		split_end = 0;
		if (del_nr == 0) {
			btrfs_set_file_extent_type(leaf, fi,
						   BTRFS_FILE_EXTENT_REG);
			goto done;
		}

		fi = btrfs_item_ptr(leaf, del_slot - 1,
				    struct btrfs_file_extent_item);
		btrfs_set_file_extent_type(leaf, fi, BTRFS_FILE_EXTENT_REG);
		btrfs_set_file_extent_num_bytes(leaf, fi,
						extent_end - key.offset);
		btrfs_mark_buffer_dirty(leaf);

		ret = btrfs_del_items(trans, root, path, del_slot, del_nr);
		BUG_ON(ret);
		goto release;
	} else if (split == start) {
		if (locked_end < extent_end) {
			ret = try_lock_extent(&BTRFS_I(inode)->io_tree,
					locked_end, extent_end - 1, GFP_NOFS);
			if (!ret) {
				btrfs_release_path(root, path);
				lock_extent(&BTRFS_I(inode)->io_tree,
					locked_end, extent_end - 1, GFP_NOFS);
				locked_end = extent_end;
				goto again;
			}
			locked_end = extent_end;
		}
		btrfs_set_file_extent_num_bytes(leaf, fi, split - key.offset);
	} else  {
		BUG_ON(key.offset != start);
		key.offset = split;
		btrfs_set_file_extent_offset(leaf, fi, key.offset -
					     orig_offset);
		btrfs_set_file_extent_num_bytes(leaf, fi, extent_end - split);
		btrfs_set_item_key_safe(trans, root, path, &key);
		extent_end = split;
	}

	if (extent_end == end) {
		split_end = 0;
		extent_type = BTRFS_FILE_EXTENT_REG;
	}
	if (extent_end == end && split == start) {
		other_start = end;
		other_end = 0;
		if (extent_mergeable(leaf, path->slots[0] + 1, inode->i_ino,
				     bytenr, &other_start, &other_end)) {
			path->slots[0]++;
			fi = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_file_extent_item);
			key.offset = split;
			btrfs_set_item_key_safe(trans, root, path, &key);
			btrfs_set_file_extent_offset(leaf, fi, key.offset -
						     orig_offset);
			btrfs_set_file_extent_num_bytes(leaf, fi,
							other_end - split);
			goto done;
		}
	}
	if (extent_end == end && split == end) {
		other_start = 0;
		other_end = start;
		if (extent_mergeable(leaf, path->slots[0] - 1 , inode->i_ino,
				     bytenr, &other_start, &other_end)) {
			path->slots[0]--;
			fi = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_file_extent_item);
			btrfs_set_file_extent_num_bytes(leaf, fi, extent_end -
							other_start);
			goto done;
		}
	}

	btrfs_mark_buffer_dirty(leaf);

	ret = btrfs_inc_extent_ref(trans, root, bytenr, num_bytes, 0,
				   root->root_key.objectid,
				   inode->i_ino, orig_offset);
	BUG_ON(ret);
	btrfs_release_path(root, path);

	key.offset = start;
	ret = btrfs_insert_empty_item(trans, root, path, &key, sizeof(*fi));
	BUG_ON(ret);

	leaf = path->nodes[0];
	fi = btrfs_item_ptr(leaf, path->slots[0],
			    struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(leaf, fi, trans->transid);
	btrfs_set_file_extent_type(leaf, fi, extent_type);
	btrfs_set_file_extent_disk_bytenr(leaf, fi, bytenr);
	btrfs_set_file_extent_disk_num_bytes(leaf, fi, num_bytes);
	btrfs_set_file_extent_offset(leaf, fi, key.offset - orig_offset);
	btrfs_set_file_extent_num_bytes(leaf, fi, extent_end - key.offset);
	btrfs_set_file_extent_ram_bytes(leaf, fi, num_bytes);
	btrfs_set_file_extent_compression(leaf, fi, 0);
	btrfs_set_file_extent_encryption(leaf, fi, 0);
	btrfs_set_file_extent_other_encoding(leaf, fi, 0);
done:
	btrfs_mark_buffer_dirty(leaf);

release:
	btrfs_release_path(root, path);
	if (split_end && split == start) {
		split = end;
		goto again;
	}
	if (locked_end > end) {
		unlock_extent(&BTRFS_I(inode)->io_tree, end, locked_end - 1,
			      GFP_NOFS);
	}
	btrfs_free_path(path);
	return 0;
}

/*
 * this gets pages into the page cache and locks them down, it also properly
 * waits for data=ordered extents to finish before allowing the pages to be
 * modified.
 */
static noinline int prepare_pages(struct btrfs_root *root, struct file *file,
			 struct page **pages, size_t num_pages,
			 loff_t pos, unsigned long first_index,
			 unsigned long last_index, size_t write_bytes)
{
	int i;
	unsigned long index = pos >> PAGE_CACHE_SHIFT;
	struct inode *inode = fdentry(file)->d_inode;
	int err = 0;
	u64 start_pos;
	u64 last_pos;

	start_pos = pos & ~((u64)root->sectorsize - 1);
	last_pos = ((u64)index + num_pages) << PAGE_CACHE_SHIFT;

	if (start_pos > inode->i_size) {
		err = btrfs_cont_expand(inode, start_pos);
		if (err)
			return err;
	}

	memset(pages, 0, num_pages * sizeof(struct page *));
again:
	for (i = 0; i < num_pages; i++) {
		pages[i] = grab_cache_page(inode->i_mapping, index + i);
		if (!pages[i]) {
			err = -ENOMEM;
			BUG_ON(1);
		}
		wait_on_page_writeback(pages[i]);
	}
	if (start_pos < inode->i_size) {
		struct btrfs_ordered_extent *ordered;
		lock_extent(&BTRFS_I(inode)->io_tree,
			    start_pos, last_pos - 1, GFP_NOFS);
		ordered = btrfs_lookup_first_ordered_extent(inode,
							    last_pos - 1);
		if (ordered &&
		    ordered->file_offset + ordered->len > start_pos &&
		    ordered->file_offset < last_pos) {
			btrfs_put_ordered_extent(ordered);
			unlock_extent(&BTRFS_I(inode)->io_tree,
				      start_pos, last_pos - 1, GFP_NOFS);
			for (i = 0; i < num_pages; i++) {
				unlock_page(pages[i]);
				page_cache_release(pages[i]);
			}
			btrfs_wait_ordered_range(inode, start_pos,
						 last_pos - start_pos);
			goto again;
		}
		if (ordered)
			btrfs_put_ordered_extent(ordered);

		clear_extent_bits(&BTRFS_I(inode)->io_tree, start_pos,
				  last_pos - 1, EXTENT_DIRTY | EXTENT_DELALLOC,
				  GFP_NOFS);
		unlock_extent(&BTRFS_I(inode)->io_tree,
			      start_pos, last_pos - 1, GFP_NOFS);
	}
	for (i = 0; i < num_pages; i++) {
		clear_page_dirty_for_io(pages[i]);
		set_page_extent_mapped(pages[i]);
		WARN_ON(!PageLocked(pages[i]));
	}
	return 0;
}

static ssize_t btrfs_file_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	loff_t pos;
	loff_t start_pos;
	ssize_t num_written = 0;
	ssize_t err = 0;
	int ret = 0;
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct page **pages = NULL;
	int nrptrs;
	struct page *pinned[2];
	unsigned long first_index;
	unsigned long last_index;
	int will_write;

	will_write = ((file->f_flags & O_SYNC) || IS_SYNC(inode) ||
		      (file->f_flags & O_DIRECT));

	nrptrs = min((count + PAGE_CACHE_SIZE - 1) / PAGE_CACHE_SIZE,
		     PAGE_CACHE_SIZE / (sizeof(struct page *)));
	pinned[0] = NULL;
	pinned[1] = NULL;

	pos = *ppos;
	start_pos = pos;

	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);
	current->backing_dev_info = inode->i_mapping->backing_dev_info;
	err = generic_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out_nolock;
	if (count == 0)
		goto out_nolock;

	err = file_remove_suid(file);
	if (err)
		goto out_nolock;
	file_update_time(file);

	pages = kmalloc(nrptrs * sizeof(struct page *), GFP_KERNEL);

	mutex_lock(&inode->i_mutex);
	BTRFS_I(inode)->sequence++;
	first_index = pos >> PAGE_CACHE_SHIFT;
	last_index = (pos + count) >> PAGE_CACHE_SHIFT;

	/*
	 * there are lots of better ways to do this, but this code
	 * makes sure the first and last page in the file range are
	 * up to date and ready for cow
	 */
	if ((pos & (PAGE_CACHE_SIZE - 1))) {
		pinned[0] = grab_cache_page(inode->i_mapping, first_index);
		if (!PageUptodate(pinned[0])) {
			ret = btrfs_readpage(NULL, pinned[0]);
			BUG_ON(ret);
			wait_on_page_locked(pinned[0]);
		} else {
			unlock_page(pinned[0]);
		}
	}
	if ((pos + count) & (PAGE_CACHE_SIZE - 1)) {
		pinned[1] = grab_cache_page(inode->i_mapping, last_index);
		if (!PageUptodate(pinned[1])) {
			ret = btrfs_readpage(NULL, pinned[1]);
			BUG_ON(ret);
			wait_on_page_locked(pinned[1]);
		} else {
			unlock_page(pinned[1]);
		}
	}

	while (count > 0) {
		size_t offset = pos & (PAGE_CACHE_SIZE - 1);
		size_t write_bytes = min(count, nrptrs *
					(size_t)PAGE_CACHE_SIZE -
					 offset);
		size_t num_pages = (write_bytes + PAGE_CACHE_SIZE - 1) >>
					PAGE_CACHE_SHIFT;

		WARN_ON(num_pages > nrptrs);
		memset(pages, 0, sizeof(struct page *) * nrptrs);

		ret = btrfs_check_data_free_space(root, inode, write_bytes);
		if (ret)
			goto out;

		ret = prepare_pages(root, file, pages, num_pages,
				    pos, first_index, last_index,
				    write_bytes);
		if (ret) {
			btrfs_free_reserved_data_space(root, inode,
						       write_bytes);
			goto out;
		}

		ret = btrfs_copy_from_user(pos, num_pages,
					   write_bytes, pages, buf);
		if (ret) {
			btrfs_free_reserved_data_space(root, inode,
						       write_bytes);
			btrfs_drop_pages(pages, num_pages);
			goto out;
		}

		ret = dirty_and_release_pages(NULL, root, file, pages,
					      num_pages, pos, write_bytes);
		btrfs_drop_pages(pages, num_pages);
		if (ret) {
			btrfs_free_reserved_data_space(root, inode,
						       write_bytes);
			goto out;
		}

		if (will_write) {
			btrfs_fdatawrite_range(inode->i_mapping, pos,
					       pos + write_bytes - 1,
					       WB_SYNC_ALL);
		} else {
			balance_dirty_pages_ratelimited_nr(inode->i_mapping,
							   num_pages);
			if (num_pages <
			    (root->leafsize >> PAGE_CACHE_SHIFT) + 1)
				btrfs_btree_balance_dirty(root, 1);
			btrfs_throttle(root);
		}

		buf += write_bytes;
		count -= write_bytes;
		pos += write_bytes;
		num_written += write_bytes;

		cond_resched();
	}
out:
	mutex_unlock(&inode->i_mutex);
	if (ret)
		err = ret;

out_nolock:
	kfree(pages);
	if (pinned[0])
		page_cache_release(pinned[0]);
	if (pinned[1])
		page_cache_release(pinned[1]);
	*ppos = pos;

	/*
	 * we want to make sure fsync finds this change
	 * but we haven't joined a transaction running right now.
	 *
	 * Later on, someone is sure to update the inode and get the
	 * real transid recorded.
	 *
	 * We set last_trans now to the fs_info generation + 1,
	 * this will either be one more than the running transaction
	 * or the generation used for the next transaction if there isn't
	 * one running right now.
	 */
	BTRFS_I(inode)->last_trans = root->fs_info->generation + 1;

	if (num_written > 0 && will_write) {
		struct btrfs_trans_handle *trans;

		err = btrfs_wait_ordered_range(inode, start_pos, num_written);
		if (err)
			num_written = err;

		if ((file->f_flags & O_SYNC) || IS_SYNC(inode)) {
			trans = btrfs_start_transaction(root, 1);
			ret = btrfs_log_dentry_safe(trans, root,
						    file->f_dentry);
			if (ret == 0) {
				ret = btrfs_sync_log(trans, root);
				if (ret == 0)
					btrfs_end_transaction(trans, root);
				else
					btrfs_commit_transaction(trans, root);
			} else {
				btrfs_commit_transaction(trans, root);
			}
		}
		if (file->f_flags & O_DIRECT) {
			invalidate_mapping_pages(inode->i_mapping,
			      start_pos >> PAGE_CACHE_SHIFT,
			     (start_pos + num_written - 1) >> PAGE_CACHE_SHIFT);
		}
	}
	current->backing_dev_info = NULL;
	return num_written ? num_written : err;
}

int btrfs_release_file(struct inode *inode, struct file *filp)
{
	/*
	 * ordered_data_close is set by settattr when we are about to truncate
	 * a file from a non-zero size to a zero size.  This tries to
	 * flush down new bytes that may have been written if the
	 * application were using truncate to replace a file in place.
	 */
	if (BTRFS_I(inode)->ordered_data_close) {
		BTRFS_I(inode)->ordered_data_close = 0;
		btrfs_add_ordered_operation(NULL, BTRFS_I(inode)->root, inode);
		if (inode->i_size > BTRFS_ORDERED_OPERATIONS_FLUSH_LIMIT)
			filemap_flush(inode->i_mapping);
	}
	if (filp->private_data)
		btrfs_ioctl_trans_end(filp);
	return 0;
}

/*
 * fsync call for both files and directories.  This logs the inode into
 * the tree log instead of forcing full commits whenever possible.
 *
 * It needs to call filemap_fdatawait so that all ordered extent updates are
 * in the metadata btree are up to date for copying to the log.
 *
 * It drops the inode mutex before doing the tree log commit.  This is an
 * important optimization for directories because holding the mutex prevents
 * new operations on the dir while we write to disk.
 */
int btrfs_sync_file(struct file *file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;
	struct btrfs_trans_handle *trans;

	/*
	 * check the transaction that last modified this inode
	 * and see if its already been committed
	 */
	if (!BTRFS_I(inode)->last_trans)
		goto out;

	mutex_lock(&root->fs_info->trans_mutex);
	if (BTRFS_I(inode)->last_trans <=
	    root->fs_info->last_trans_committed) {
		BTRFS_I(inode)->last_trans = 0;
		mutex_unlock(&root->fs_info->trans_mutex);
		goto out;
	}
	mutex_unlock(&root->fs_info->trans_mutex);

	root->log_batch++;
	filemap_fdatawrite(inode->i_mapping);
	btrfs_wait_ordered_range(inode, 0, (u64)-1);
	root->log_batch++;

	if (datasync && !(inode->i_state & I_DIRTY_PAGES))
		goto out;
	/*
	 * ok we haven't committed the transaction yet, lets do a commit
	 */
	if (file && file->private_data)
		btrfs_ioctl_trans_end(file);

	trans = btrfs_start_transaction(root, 1);
	if (!trans) {
		ret = -ENOMEM;
		goto out;
	}

	ret = btrfs_log_dentry_safe(trans, root, dentry);
	if (ret < 0)
		goto out;

	/* we've logged all the items and now have a consistent
	 * version of the file in the log.  It is possible that
	 * someone will come in and modify the file, but that's
	 * fine because the log is consistent on disk, and we
	 * have references to all of the file's extents
	 *
	 * It is possible that someone will come in and log the
	 * file again, but that will end up using the synchronization
	 * inside btrfs_sync_log to keep things safe.
	 */
	mutex_unlock(&dentry->d_inode->i_mutex);

	if (ret > 0) {
		ret = btrfs_commit_transaction(trans, root);
	} else {
		ret = btrfs_sync_log(trans, root);
		if (ret == 0)
			ret = btrfs_end_transaction(trans, root);
		else
			ret = btrfs_commit_transaction(trans, root);
	}
	mutex_lock(&dentry->d_inode->i_mutex);
out:
	return ret > 0 ? EIO : ret;
}

static struct vm_operations_struct btrfs_file_vm_ops = {
	.fault		= filemap_fault,
	.page_mkwrite	= btrfs_page_mkwrite,
};

static int btrfs_file_mmap(struct file	*filp, struct vm_area_struct *vma)
{
	vma->vm_ops = &btrfs_file_vm_ops;
	file_accessed(filp);
	return 0;
}

struct file_operations btrfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read       = generic_file_aio_read,
	.splice_read	= generic_file_splice_read,
	.write		= btrfs_file_write,
	.mmap		= btrfs_file_mmap,
	.open		= generic_file_open,
	.release	= btrfs_release_file,
	.fsync		= btrfs_sync_file,
	.unlocked_ioctl	= btrfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= btrfs_ioctl,
#endif
};
