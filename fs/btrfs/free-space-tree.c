/*
 * Copyright (C) 2015 Facebook.  All rights reserved.
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

#include <linux/kernel.h>
#include <linux/sched/mm.h>
#include "ctree.h"
#include "disk-io.h"
#include "locking.h"
#include "free-space-tree.h"
#include "transaction.h"

static int __add_block_group_free_space(struct btrfs_trans_handle *trans,
					struct btrfs_fs_info *fs_info,
					struct btrfs_block_group_cache *block_group,
					struct btrfs_path *path);

void set_free_space_tree_thresholds(struct btrfs_block_group_cache *cache)
{
	u32 bitmap_range;
	size_t bitmap_size;
	u64 num_bitmaps, total_bitmap_size;

	/*
	 * We convert to bitmaps when the disk space required for using extents
	 * exceeds that required for using bitmaps.
	 */
	bitmap_range = cache->fs_info->sectorsize * BTRFS_FREE_SPACE_BITMAP_BITS;
	num_bitmaps = div_u64(cache->key.offset + bitmap_range - 1,
			      bitmap_range);
	bitmap_size = sizeof(struct btrfs_item) + BTRFS_FREE_SPACE_BITMAP_SIZE;
	total_bitmap_size = num_bitmaps * bitmap_size;
	cache->bitmap_high_thresh = div_u64(total_bitmap_size,
					    sizeof(struct btrfs_item));

	/*
	 * We allow for a small buffer between the high threshold and low
	 * threshold to avoid thrashing back and forth between the two formats.
	 */
	if (cache->bitmap_high_thresh > 100)
		cache->bitmap_low_thresh = cache->bitmap_high_thresh - 100;
	else
		cache->bitmap_low_thresh = 0;
}

static int add_new_free_space_info(struct btrfs_trans_handle *trans,
				   struct btrfs_fs_info *fs_info,
				   struct btrfs_block_group_cache *block_group,
				   struct btrfs_path *path)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_free_space_info *info;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	int ret;

	key.objectid = block_group->key.objectid;
	key.type = BTRFS_FREE_SPACE_INFO_KEY;
	key.offset = block_group->key.offset;

	ret = btrfs_insert_empty_item(trans, root, path, &key, sizeof(*info));
	if (ret)
		goto out;

	leaf = path->nodes[0];
	info = btrfs_item_ptr(leaf, path->slots[0],
			      struct btrfs_free_space_info);
	btrfs_set_free_space_extent_count(leaf, info, 0);
	btrfs_set_free_space_flags(leaf, info, 0);
	btrfs_mark_buffer_dirty(leaf);

	ret = 0;
out:
	btrfs_release_path(path);
	return ret;
}

struct btrfs_free_space_info *
search_free_space_info(struct btrfs_trans_handle *trans,
		       struct btrfs_fs_info *fs_info,
		       struct btrfs_block_group_cache *block_group,
		       struct btrfs_path *path, int cow)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_key key;
	int ret;

	key.objectid = block_group->key.objectid;
	key.type = BTRFS_FREE_SPACE_INFO_KEY;
	key.offset = block_group->key.offset;

	ret = btrfs_search_slot(trans, root, &key, path, 0, cow);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret != 0) {
		btrfs_warn(fs_info, "missing free space info for %llu",
			   block_group->key.objectid);
		ASSERT(0);
		return ERR_PTR(-ENOENT);
	}

	return btrfs_item_ptr(path->nodes[0], path->slots[0],
			      struct btrfs_free_space_info);
}

/*
 * btrfs_search_slot() but we're looking for the greatest key less than the
 * passed key.
 */
static int btrfs_search_prev_slot(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_key *key, struct btrfs_path *p,
				  int ins_len, int cow)
{
	int ret;

	ret = btrfs_search_slot(trans, root, key, p, ins_len, cow);
	if (ret < 0)
		return ret;

	if (ret == 0) {
		ASSERT(0);
		return -EIO;
	}

	if (p->slots[0] == 0) {
		ASSERT(0);
		return -EIO;
	}
	p->slots[0]--;

	return 0;
}

static inline u32 free_space_bitmap_size(u64 size, u32 sectorsize)
{
	return DIV_ROUND_UP((u32)div_u64(size, sectorsize), BITS_PER_BYTE);
}

static u8 *alloc_bitmap(u32 bitmap_size)
{
	u8 *ret;
	unsigned int nofs_flag;

	/*
	 * GFP_NOFS doesn't work with kvmalloc(), but we really can't recurse
	 * into the filesystem as the free space bitmap can be modified in the
	 * critical section of a transaction commit.
	 *
	 * TODO: push the memalloc_nofs_{save,restore}() to the caller where we
	 * know that recursion is unsafe.
	 */
	nofs_flag = memalloc_nofs_save();
	ret = kvzalloc(bitmap_size, GFP_KERNEL);
	memalloc_nofs_restore(nofs_flag);
	return ret;
}

int convert_free_space_to_bitmaps(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group,
				  struct btrfs_path *path)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_free_space_info *info;
	struct btrfs_key key, found_key;
	struct extent_buffer *leaf;
	u8 *bitmap, *bitmap_cursor;
	u64 start, end;
	u64 bitmap_range, i;
	u32 bitmap_size, flags, expected_extent_count;
	u32 extent_count = 0;
	int done = 0, nr;
	int ret;

	bitmap_size = free_space_bitmap_size(block_group->key.offset,
					     fs_info->sectorsize);
	bitmap = alloc_bitmap(bitmap_size);
	if (!bitmap) {
		ret = -ENOMEM;
		goto out;
	}

	start = block_group->key.objectid;
	end = block_group->key.objectid + block_group->key.offset;

	key.objectid = end - 1;
	key.type = (u8)-1;
	key.offset = (u64)-1;

	while (!done) {
		ret = btrfs_search_prev_slot(trans, root, &key, path, -1, 1);
		if (ret)
			goto out;

		leaf = path->nodes[0];
		nr = 0;
		path->slots[0]++;
		while (path->slots[0] > 0) {
			btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0] - 1);

			if (found_key.type == BTRFS_FREE_SPACE_INFO_KEY) {
				ASSERT(found_key.objectid == block_group->key.objectid);
				ASSERT(found_key.offset == block_group->key.offset);
				done = 1;
				break;
			} else if (found_key.type == BTRFS_FREE_SPACE_EXTENT_KEY) {
				u64 first, last;

				ASSERT(found_key.objectid >= start);
				ASSERT(found_key.objectid < end);
				ASSERT(found_key.objectid + found_key.offset <= end);

				first = div_u64(found_key.objectid - start,
						fs_info->sectorsize);
				last = div_u64(found_key.objectid + found_key.offset - start,
					       fs_info->sectorsize);
				le_bitmap_set(bitmap, first, last - first);

				extent_count++;
				nr++;
				path->slots[0]--;
			} else {
				ASSERT(0);
			}
		}

		ret = btrfs_del_items(trans, root, path, path->slots[0], nr);
		if (ret)
			goto out;
		btrfs_release_path(path);
	}

	info = search_free_space_info(trans, fs_info, block_group, path, 1);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out;
	}
	leaf = path->nodes[0];
	flags = btrfs_free_space_flags(leaf, info);
	flags |= BTRFS_FREE_SPACE_USING_BITMAPS;
	btrfs_set_free_space_flags(leaf, info, flags);
	expected_extent_count = btrfs_free_space_extent_count(leaf, info);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	if (extent_count != expected_extent_count) {
		btrfs_err(fs_info,
			  "incorrect extent count for %llu; counted %u, expected %u",
			  block_group->key.objectid, extent_count,
			  expected_extent_count);
		ASSERT(0);
		ret = -EIO;
		goto out;
	}

	bitmap_cursor = bitmap;
	bitmap_range = fs_info->sectorsize * BTRFS_FREE_SPACE_BITMAP_BITS;
	i = start;
	while (i < end) {
		unsigned long ptr;
		u64 extent_size;
		u32 data_size;

		extent_size = min(end - i, bitmap_range);
		data_size = free_space_bitmap_size(extent_size,
						   fs_info->sectorsize);

		key.objectid = i;
		key.type = BTRFS_FREE_SPACE_BITMAP_KEY;
		key.offset = extent_size;

		ret = btrfs_insert_empty_item(trans, root, path, &key,
					      data_size);
		if (ret)
			goto out;

		leaf = path->nodes[0];
		ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
		write_extent_buffer(leaf, bitmap_cursor, ptr,
				    data_size);
		btrfs_mark_buffer_dirty(leaf);
		btrfs_release_path(path);

		i += extent_size;
		bitmap_cursor += data_size;
	}

	ret = 0;
out:
	kvfree(bitmap);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;
}

int convert_free_space_to_extents(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group,
				  struct btrfs_path *path)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_free_space_info *info;
	struct btrfs_key key, found_key;
	struct extent_buffer *leaf;
	u8 *bitmap;
	u64 start, end;
	/* Initialize to silence GCC. */
	u64 extent_start = 0;
	u64 offset;
	u32 bitmap_size, flags, expected_extent_count;
	int prev_bit = 0, bit, bitnr;
	u32 extent_count = 0;
	int done = 0, nr;
	int ret;

	bitmap_size = free_space_bitmap_size(block_group->key.offset,
					     fs_info->sectorsize);
	bitmap = alloc_bitmap(bitmap_size);
	if (!bitmap) {
		ret = -ENOMEM;
		goto out;
	}

	start = block_group->key.objectid;
	end = block_group->key.objectid + block_group->key.offset;

	key.objectid = end - 1;
	key.type = (u8)-1;
	key.offset = (u64)-1;

	while (!done) {
		ret = btrfs_search_prev_slot(trans, root, &key, path, -1, 1);
		if (ret)
			goto out;

		leaf = path->nodes[0];
		nr = 0;
		path->slots[0]++;
		while (path->slots[0] > 0) {
			btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0] - 1);

			if (found_key.type == BTRFS_FREE_SPACE_INFO_KEY) {
				ASSERT(found_key.objectid == block_group->key.objectid);
				ASSERT(found_key.offset == block_group->key.offset);
				done = 1;
				break;
			} else if (found_key.type == BTRFS_FREE_SPACE_BITMAP_KEY) {
				unsigned long ptr;
				u8 *bitmap_cursor;
				u32 bitmap_pos, data_size;

				ASSERT(found_key.objectid >= start);
				ASSERT(found_key.objectid < end);
				ASSERT(found_key.objectid + found_key.offset <= end);

				bitmap_pos = div_u64(found_key.objectid - start,
						     fs_info->sectorsize *
						     BITS_PER_BYTE);
				bitmap_cursor = bitmap + bitmap_pos;
				data_size = free_space_bitmap_size(found_key.offset,
								   fs_info->sectorsize);

				ptr = btrfs_item_ptr_offset(leaf, path->slots[0] - 1);
				read_extent_buffer(leaf, bitmap_cursor, ptr,
						   data_size);

				nr++;
				path->slots[0]--;
			} else {
				ASSERT(0);
			}
		}

		ret = btrfs_del_items(trans, root, path, path->slots[0], nr);
		if (ret)
			goto out;
		btrfs_release_path(path);
	}

	info = search_free_space_info(trans, fs_info, block_group, path, 1);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out;
	}
	leaf = path->nodes[0];
	flags = btrfs_free_space_flags(leaf, info);
	flags &= ~BTRFS_FREE_SPACE_USING_BITMAPS;
	btrfs_set_free_space_flags(leaf, info, flags);
	expected_extent_count = btrfs_free_space_extent_count(leaf, info);
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	offset = start;
	bitnr = 0;
	while (offset < end) {
		bit = !!le_test_bit(bitnr, bitmap);
		if (prev_bit == 0 && bit == 1) {
			extent_start = offset;
		} else if (prev_bit == 1 && bit == 0) {
			key.objectid = extent_start;
			key.type = BTRFS_FREE_SPACE_EXTENT_KEY;
			key.offset = offset - extent_start;

			ret = btrfs_insert_empty_item(trans, root, path, &key, 0);
			if (ret)
				goto out;
			btrfs_release_path(path);

			extent_count++;
		}
		prev_bit = bit;
		offset += fs_info->sectorsize;
		bitnr++;
	}
	if (prev_bit == 1) {
		key.objectid = extent_start;
		key.type = BTRFS_FREE_SPACE_EXTENT_KEY;
		key.offset = end - extent_start;

		ret = btrfs_insert_empty_item(trans, root, path, &key, 0);
		if (ret)
			goto out;
		btrfs_release_path(path);

		extent_count++;
	}

	if (extent_count != expected_extent_count) {
		btrfs_err(fs_info,
			  "incorrect extent count for %llu; counted %u, expected %u",
			  block_group->key.objectid, extent_count,
			  expected_extent_count);
		ASSERT(0);
		ret = -EIO;
		goto out;
	}

	ret = 0;
out:
	kvfree(bitmap);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;
}

static int update_free_space_extent_count(struct btrfs_trans_handle *trans,
					  struct btrfs_fs_info *fs_info,
					  struct btrfs_block_group_cache *block_group,
					  struct btrfs_path *path,
					  int new_extents)
{
	struct btrfs_free_space_info *info;
	u32 flags;
	u32 extent_count;
	int ret = 0;

	if (new_extents == 0)
		return 0;

	info = search_free_space_info(trans, fs_info, block_group, path, 1);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out;
	}
	flags = btrfs_free_space_flags(path->nodes[0], info);
	extent_count = btrfs_free_space_extent_count(path->nodes[0], info);

	extent_count += new_extents;
	btrfs_set_free_space_extent_count(path->nodes[0], info, extent_count);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_release_path(path);

	if (!(flags & BTRFS_FREE_SPACE_USING_BITMAPS) &&
	    extent_count > block_group->bitmap_high_thresh) {
		ret = convert_free_space_to_bitmaps(trans, fs_info, block_group,
						    path);
	} else if ((flags & BTRFS_FREE_SPACE_USING_BITMAPS) &&
		   extent_count < block_group->bitmap_low_thresh) {
		ret = convert_free_space_to_extents(trans, fs_info, block_group,
						    path);
	}

out:
	return ret;
}

int free_space_test_bit(struct btrfs_block_group_cache *block_group,
			struct btrfs_path *path, u64 offset)
{
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 found_start, found_end;
	unsigned long ptr, i;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	ASSERT(key.type == BTRFS_FREE_SPACE_BITMAP_KEY);

	found_start = key.objectid;
	found_end = key.objectid + key.offset;
	ASSERT(offset >= found_start && offset < found_end);

	ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
	i = div_u64(offset - found_start,
		    block_group->fs_info->sectorsize);
	return !!extent_buffer_test_bit(leaf, ptr, i);
}

static void free_space_set_bits(struct btrfs_block_group_cache *block_group,
				struct btrfs_path *path, u64 *start, u64 *size,
				int bit)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 end = *start + *size;
	u64 found_start, found_end;
	unsigned long ptr, first, last;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	ASSERT(key.type == BTRFS_FREE_SPACE_BITMAP_KEY);

	found_start = key.objectid;
	found_end = key.objectid + key.offset;
	ASSERT(*start >= found_start && *start < found_end);
	ASSERT(end > found_start);

	if (end > found_end)
		end = found_end;

	ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
	first = div_u64(*start - found_start, fs_info->sectorsize);
	last = div_u64(end - found_start, fs_info->sectorsize);
	if (bit)
		extent_buffer_bitmap_set(leaf, ptr, first, last - first);
	else
		extent_buffer_bitmap_clear(leaf, ptr, first, last - first);
	btrfs_mark_buffer_dirty(leaf);

	*size -= end - *start;
	*start = end;
}

/*
 * We can't use btrfs_next_item() in modify_free_space_bitmap() because
 * btrfs_next_leaf() doesn't get the path for writing. We can forgo the fancy
 * tree walking in btrfs_next_leaf() anyways because we know exactly what we're
 * looking for.
 */
static int free_space_next_bitmap(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root, struct btrfs_path *p)
{
	struct btrfs_key key;

	if (p->slots[0] + 1 < btrfs_header_nritems(p->nodes[0])) {
		p->slots[0]++;
		return 0;
	}

	btrfs_item_key_to_cpu(p->nodes[0], &key, p->slots[0]);
	btrfs_release_path(p);

	key.objectid += key.offset;
	key.type = (u8)-1;
	key.offset = (u64)-1;

	return btrfs_search_prev_slot(trans, root, &key, p, 0, 1);
}

/*
 * If remove is 1, then we are removing free space, thus clearing bits in the
 * bitmap. If remove is 0, then we are adding free space, thus setting bits in
 * the bitmap.
 */
static int modify_free_space_bitmap(struct btrfs_trans_handle *trans,
				    struct btrfs_fs_info *fs_info,
				    struct btrfs_block_group_cache *block_group,
				    struct btrfs_path *path,
				    u64 start, u64 size, int remove)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_key key;
	u64 end = start + size;
	u64 cur_start, cur_size;
	int prev_bit, next_bit;
	int new_extents;
	int ret;

	/*
	 * Read the bit for the block immediately before the extent of space if
	 * that block is within the block group.
	 */
	if (start > block_group->key.objectid) {
		u64 prev_block = start - block_group->fs_info->sectorsize;

		key.objectid = prev_block;
		key.type = (u8)-1;
		key.offset = (u64)-1;

		ret = btrfs_search_prev_slot(trans, root, &key, path, 0, 1);
		if (ret)
			goto out;

		prev_bit = free_space_test_bit(block_group, path, prev_block);

		/* The previous block may have been in the previous bitmap. */
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (start >= key.objectid + key.offset) {
			ret = free_space_next_bitmap(trans, root, path);
			if (ret)
				goto out;
		}
	} else {
		key.objectid = start;
		key.type = (u8)-1;
		key.offset = (u64)-1;

		ret = btrfs_search_prev_slot(trans, root, &key, path, 0, 1);
		if (ret)
			goto out;

		prev_bit = -1;
	}

	/*
	 * Iterate over all of the bitmaps overlapped by the extent of space,
	 * clearing/setting bits as required.
	 */
	cur_start = start;
	cur_size = size;
	while (1) {
		free_space_set_bits(block_group, path, &cur_start, &cur_size,
				    !remove);
		if (cur_size == 0)
			break;
		ret = free_space_next_bitmap(trans, root, path);
		if (ret)
			goto out;
	}

	/*
	 * Read the bit for the block immediately after the extent of space if
	 * that block is within the block group.
	 */
	if (end < block_group->key.objectid + block_group->key.offset) {
		/* The next block may be in the next bitmap. */
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (end >= key.objectid + key.offset) {
			ret = free_space_next_bitmap(trans, root, path);
			if (ret)
				goto out;
		}

		next_bit = free_space_test_bit(block_group, path, end);
	} else {
		next_bit = -1;
	}

	if (remove) {
		new_extents = -1;
		if (prev_bit == 1) {
			/* Leftover on the left. */
			new_extents++;
		}
		if (next_bit == 1) {
			/* Leftover on the right. */
			new_extents++;
		}
	} else {
		new_extents = 1;
		if (prev_bit == 1) {
			/* Merging with neighbor on the left. */
			new_extents--;
		}
		if (next_bit == 1) {
			/* Merging with neighbor on the right. */
			new_extents--;
		}
	}

	btrfs_release_path(path);
	ret = update_free_space_extent_count(trans, fs_info, block_group, path,
					     new_extents);

out:
	return ret;
}

static int remove_free_space_extent(struct btrfs_trans_handle *trans,
				    struct btrfs_fs_info *fs_info,
				    struct btrfs_block_group_cache *block_group,
				    struct btrfs_path *path,
				    u64 start, u64 size)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_key key;
	u64 found_start, found_end;
	u64 end = start + size;
	int new_extents = -1;
	int ret;

	key.objectid = start;
	key.type = (u8)-1;
	key.offset = (u64)-1;

	ret = btrfs_search_prev_slot(trans, root, &key, path, -1, 1);
	if (ret)
		goto out;

	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

	ASSERT(key.type == BTRFS_FREE_SPACE_EXTENT_KEY);

	found_start = key.objectid;
	found_end = key.objectid + key.offset;
	ASSERT(start >= found_start && end <= found_end);

	/*
	 * Okay, now that we've found the free space extent which contains the
	 * free space that we are removing, there are four cases:
	 *
	 * 1. We're using the whole extent: delete the key we found and
	 * decrement the free space extent count.
	 * 2. We are using part of the extent starting at the beginning: delete
	 * the key we found and insert a new key representing the leftover at
	 * the end. There is no net change in the number of extents.
	 * 3. We are using part of the extent ending at the end: delete the key
	 * we found and insert a new key representing the leftover at the
	 * beginning. There is no net change in the number of extents.
	 * 4. We are using part of the extent in the middle: delete the key we
	 * found and insert two new keys representing the leftovers on each
	 * side. Where we used to have one extent, we now have two, so increment
	 * the extent count. We may need to convert the block group to bitmaps
	 * as a result.
	 */

	/* Delete the existing key (cases 1-4). */
	ret = btrfs_del_item(trans, root, path);
	if (ret)
		goto out;

	/* Add a key for leftovers at the beginning (cases 3 and 4). */
	if (start > found_start) {
		key.objectid = found_start;
		key.type = BTRFS_FREE_SPACE_EXTENT_KEY;
		key.offset = start - found_start;

		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, root, path, &key, 0);
		if (ret)
			goto out;
		new_extents++;
	}

	/* Add a key for leftovers at the end (cases 2 and 4). */
	if (end < found_end) {
		key.objectid = end;
		key.type = BTRFS_FREE_SPACE_EXTENT_KEY;
		key.offset = found_end - end;

		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, root, path, &key, 0);
		if (ret)
			goto out;
		new_extents++;
	}

	btrfs_release_path(path);
	ret = update_free_space_extent_count(trans, fs_info, block_group, path,
					     new_extents);

out:
	return ret;
}

int __remove_from_free_space_tree(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group,
				  struct btrfs_path *path, u64 start, u64 size)
{
	struct btrfs_free_space_info *info;
	u32 flags;
	int ret;

	if (block_group->needs_free_space) {
		ret = __add_block_group_free_space(trans, fs_info, block_group,
						   path);
		if (ret)
			return ret;
	}

	info = search_free_space_info(NULL, fs_info, block_group, path, 0);
	if (IS_ERR(info))
		return PTR_ERR(info);
	flags = btrfs_free_space_flags(path->nodes[0], info);
	btrfs_release_path(path);

	if (flags & BTRFS_FREE_SPACE_USING_BITMAPS) {
		return modify_free_space_bitmap(trans, fs_info, block_group,
						path, start, size, 1);
	} else {
		return remove_free_space_extent(trans, fs_info, block_group,
						path, start, size);
	}
}

int remove_from_free_space_tree(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info,
				u64 start, u64 size)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_path *path;
	int ret;

	if (!btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	block_group = btrfs_lookup_block_group(fs_info, start);
	if (!block_group) {
		ASSERT(0);
		ret = -ENOENT;
		goto out;
	}

	mutex_lock(&block_group->free_space_lock);
	ret = __remove_from_free_space_tree(trans, fs_info, block_group, path,
					    start, size);
	mutex_unlock(&block_group->free_space_lock);

	btrfs_put_block_group(block_group);
out:
	btrfs_free_path(path);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;
}

static int add_free_space_extent(struct btrfs_trans_handle *trans,
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_block_group_cache *block_group,
				 struct btrfs_path *path,
				 u64 start, u64 size)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_key key, new_key;
	u64 found_start, found_end;
	u64 end = start + size;
	int new_extents = 1;
	int ret;

	/*
	 * We are adding a new extent of free space, but we need to merge
	 * extents. There are four cases here:
	 *
	 * 1. The new extent does not have any immediate neighbors to merge
	 * with: add the new key and increment the free space extent count. We
	 * may need to convert the block group to bitmaps as a result.
	 * 2. The new extent has an immediate neighbor before it: remove the
	 * previous key and insert a new key combining both of them. There is no
	 * net change in the number of extents.
	 * 3. The new extent has an immediate neighbor after it: remove the next
	 * key and insert a new key combining both of them. There is no net
	 * change in the number of extents.
	 * 4. The new extent has immediate neighbors on both sides: remove both
	 * of the keys and insert a new key combining all of them. Where we used
	 * to have two extents, we now have one, so decrement the extent count.
	 */

	new_key.objectid = start;
	new_key.type = BTRFS_FREE_SPACE_EXTENT_KEY;
	new_key.offset = size;

	/* Search for a neighbor on the left. */
	if (start == block_group->key.objectid)
		goto right;
	key.objectid = start - 1;
	key.type = (u8)-1;
	key.offset = (u64)-1;

	ret = btrfs_search_prev_slot(trans, root, &key, path, -1, 1);
	if (ret)
		goto out;

	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

	if (key.type != BTRFS_FREE_SPACE_EXTENT_KEY) {
		ASSERT(key.type == BTRFS_FREE_SPACE_INFO_KEY);
		btrfs_release_path(path);
		goto right;
	}

	found_start = key.objectid;
	found_end = key.objectid + key.offset;
	ASSERT(found_start >= block_group->key.objectid &&
	       found_end > block_group->key.objectid);
	ASSERT(found_start < start && found_end <= start);

	/*
	 * Delete the neighbor on the left and absorb it into the new key (cases
	 * 2 and 4).
	 */
	if (found_end == start) {
		ret = btrfs_del_item(trans, root, path);
		if (ret)
			goto out;
		new_key.objectid = found_start;
		new_key.offset += key.offset;
		new_extents--;
	}
	btrfs_release_path(path);

right:
	/* Search for a neighbor on the right. */
	if (end == block_group->key.objectid + block_group->key.offset)
		goto insert;
	key.objectid = end;
	key.type = (u8)-1;
	key.offset = (u64)-1;

	ret = btrfs_search_prev_slot(trans, root, &key, path, -1, 1);
	if (ret)
		goto out;

	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

	if (key.type != BTRFS_FREE_SPACE_EXTENT_KEY) {
		ASSERT(key.type == BTRFS_FREE_SPACE_INFO_KEY);
		btrfs_release_path(path);
		goto insert;
	}

	found_start = key.objectid;
	found_end = key.objectid + key.offset;
	ASSERT(found_start >= block_group->key.objectid &&
	       found_end > block_group->key.objectid);
	ASSERT((found_start < start && found_end <= start) ||
	       (found_start >= end && found_end > end));

	/*
	 * Delete the neighbor on the right and absorb it into the new key
	 * (cases 3 and 4).
	 */
	if (found_start == end) {
		ret = btrfs_del_item(trans, root, path);
		if (ret)
			goto out;
		new_key.offset += key.offset;
		new_extents--;
	}
	btrfs_release_path(path);

insert:
	/* Insert the new key (cases 1-4). */
	ret = btrfs_insert_empty_item(trans, root, path, &new_key, 0);
	if (ret)
		goto out;

	btrfs_release_path(path);
	ret = update_free_space_extent_count(trans, fs_info, block_group, path,
					     new_extents);

out:
	return ret;
}

int __add_to_free_space_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info,
			     struct btrfs_block_group_cache *block_group,
			     struct btrfs_path *path, u64 start, u64 size)
{
	struct btrfs_free_space_info *info;
	u32 flags;
	int ret;

	if (block_group->needs_free_space) {
		ret = __add_block_group_free_space(trans, fs_info, block_group,
						   path);
		if (ret)
			return ret;
	}

	info = search_free_space_info(NULL, fs_info, block_group, path, 0);
	if (IS_ERR(info))
		return PTR_ERR(info);
	flags = btrfs_free_space_flags(path->nodes[0], info);
	btrfs_release_path(path);

	if (flags & BTRFS_FREE_SPACE_USING_BITMAPS) {
		return modify_free_space_bitmap(trans, fs_info, block_group,
						path, start, size, 0);
	} else {
		return add_free_space_extent(trans, fs_info, block_group, path,
					     start, size);
	}
}

int add_to_free_space_tree(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   u64 start, u64 size)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_path *path;
	int ret;

	if (!btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE))
		return 0;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	block_group = btrfs_lookup_block_group(fs_info, start);
	if (!block_group) {
		ASSERT(0);
		ret = -ENOENT;
		goto out;
	}

	mutex_lock(&block_group->free_space_lock);
	ret = __add_to_free_space_tree(trans, fs_info, block_group, path, start,
				       size);
	mutex_unlock(&block_group->free_space_lock);

	btrfs_put_block_group(block_group);
out:
	btrfs_free_path(path);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;
}

/*
 * Populate the free space tree by walking the extent tree. Operations on the
 * extent tree that happen as a result of writes to the free space tree will go
 * through the normal add/remove hooks.
 */
static int populate_free_space_tree(struct btrfs_trans_handle *trans,
				    struct btrfs_fs_info *fs_info,
				    struct btrfs_block_group_cache *block_group)
{
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_path *path, *path2;
	struct btrfs_key key;
	u64 start, end;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 1;

	path2 = btrfs_alloc_path();
	if (!path2) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	ret = add_new_free_space_info(trans, fs_info, block_group, path2);
	if (ret)
		goto out;

	mutex_lock(&block_group->free_space_lock);

	/*
	 * Iterate through all of the extent and metadata items in this block
	 * group, adding the free space between them and the free space at the
	 * end. Note that EXTENT_ITEM and METADATA_ITEM are less than
	 * BLOCK_GROUP_ITEM, so an extent may precede the block group that it's
	 * contained in.
	 */
	key.objectid = block_group->key.objectid;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(extent_root, &key, path, 1, 0);
	if (ret < 0)
		goto out_locked;
	ASSERT(ret == 0);

	start = block_group->key.objectid;
	end = block_group->key.objectid + block_group->key.offset;
	while (1) {
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.type == BTRFS_EXTENT_ITEM_KEY ||
		    key.type == BTRFS_METADATA_ITEM_KEY) {
			if (key.objectid >= end)
				break;

			if (start < key.objectid) {
				ret = __add_to_free_space_tree(trans, fs_info,
							       block_group,
							       path2, start,
							       key.objectid -
							       start);
				if (ret)
					goto out_locked;
			}
			start = key.objectid;
			if (key.type == BTRFS_METADATA_ITEM_KEY)
				start += fs_info->nodesize;
			else
				start += key.offset;
		} else if (key.type == BTRFS_BLOCK_GROUP_ITEM_KEY) {
			if (key.objectid != block_group->key.objectid)
				break;
		}

		ret = btrfs_next_item(extent_root, path);
		if (ret < 0)
			goto out_locked;
		if (ret)
			break;
	}
	if (start < end) {
		ret = __add_to_free_space_tree(trans, fs_info, block_group,
					       path2, start, end - start);
		if (ret)
			goto out_locked;
	}

	ret = 0;
out_locked:
	mutex_unlock(&block_group->free_space_lock);
out:
	btrfs_free_path(path2);
	btrfs_free_path(path);
	return ret;
}

int btrfs_create_free_space_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *free_space_root;
	struct btrfs_block_group_cache *block_group;
	struct rb_node *node;
	int ret;

	trans = btrfs_start_transaction(tree_root, 0);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	set_bit(BTRFS_FS_CREATING_FREE_SPACE_TREE, &fs_info->flags);
	free_space_root = btrfs_create_tree(trans, fs_info,
					    BTRFS_FREE_SPACE_TREE_OBJECTID);
	if (IS_ERR(free_space_root)) {
		ret = PTR_ERR(free_space_root);
		goto abort;
	}
	fs_info->free_space_root = free_space_root;

	node = rb_first(&fs_info->block_group_cache_tree);
	while (node) {
		block_group = rb_entry(node, struct btrfs_block_group_cache,
				       cache_node);
		ret = populate_free_space_tree(trans, fs_info, block_group);
		if (ret)
			goto abort;
		node = rb_next(node);
	}

	btrfs_set_fs_compat_ro(fs_info, FREE_SPACE_TREE);
	btrfs_set_fs_compat_ro(fs_info, FREE_SPACE_TREE_VALID);
	clear_bit(BTRFS_FS_CREATING_FREE_SPACE_TREE, &fs_info->flags);

	return btrfs_commit_transaction(trans);

abort:
	clear_bit(BTRFS_FS_CREATING_FREE_SPACE_TREE, &fs_info->flags);
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	return ret;
}

static int clear_free_space_tree(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int nr;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;

	key.objectid = 0;
	key.type = 0;
	key.offset = 0;

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0)
			goto out;

		nr = btrfs_header_nritems(path->nodes[0]);
		if (!nr)
			break;

		path->slots[0] = 0;
		ret = btrfs_del_items(trans, root, path, 0, nr);
		if (ret)
			goto out;

		btrfs_release_path(path);
	}

	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_clear_free_space_tree(struct btrfs_fs_info *fs_info)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root *free_space_root = fs_info->free_space_root;
	int ret;

	trans = btrfs_start_transaction(tree_root, 0);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	btrfs_clear_fs_compat_ro(fs_info, FREE_SPACE_TREE);
	btrfs_clear_fs_compat_ro(fs_info, FREE_SPACE_TREE_VALID);
	fs_info->free_space_root = NULL;

	ret = clear_free_space_tree(trans, free_space_root);
	if (ret)
		goto abort;

	ret = btrfs_del_root(trans, fs_info, &free_space_root->root_key);
	if (ret)
		goto abort;

	list_del(&free_space_root->dirty_list);

	btrfs_tree_lock(free_space_root->node);
	clean_tree_block(fs_info, free_space_root->node);
	btrfs_tree_unlock(free_space_root->node);
	btrfs_free_tree_block(trans, free_space_root, free_space_root->node,
			      0, 1);

	free_extent_buffer(free_space_root->node);
	free_extent_buffer(free_space_root->commit_root);
	kfree(free_space_root);

	return btrfs_commit_transaction(trans);

abort:
	btrfs_abort_transaction(trans, ret);
	btrfs_end_transaction(trans);
	return ret;
}

static int __add_block_group_free_space(struct btrfs_trans_handle *trans,
					struct btrfs_fs_info *fs_info,
					struct btrfs_block_group_cache *block_group,
					struct btrfs_path *path)
{
	int ret;

	block_group->needs_free_space = 0;

	ret = add_new_free_space_info(trans, fs_info, block_group, path);
	if (ret)
		return ret;

	return __add_to_free_space_tree(trans, fs_info, block_group, path,
					block_group->key.objectid,
					block_group->key.offset);
}

int add_block_group_free_space(struct btrfs_trans_handle *trans,
			       struct btrfs_fs_info *fs_info,
			       struct btrfs_block_group_cache *block_group)
{
	struct btrfs_path *path = NULL;
	int ret = 0;

	if (!btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE))
		return 0;

	mutex_lock(&block_group->free_space_lock);
	if (!block_group->needs_free_space)
		goto out;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ret = __add_block_group_free_space(trans, fs_info, block_group, path);

out:
	btrfs_free_path(path);
	mutex_unlock(&block_group->free_space_lock);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;
}

int remove_block_group_free_space(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group)
{
	struct btrfs_root *root = fs_info->free_space_root;
	struct btrfs_path *path;
	struct btrfs_key key, found_key;
	struct extent_buffer *leaf;
	u64 start, end;
	int done = 0, nr;
	int ret;

	if (!btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE))
		return 0;

	if (block_group->needs_free_space) {
		/* We never added this block group to the free space tree. */
		return 0;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	start = block_group->key.objectid;
	end = block_group->key.objectid + block_group->key.offset;

	key.objectid = end - 1;
	key.type = (u8)-1;
	key.offset = (u64)-1;

	while (!done) {
		ret = btrfs_search_prev_slot(trans, root, &key, path, -1, 1);
		if (ret)
			goto out;

		leaf = path->nodes[0];
		nr = 0;
		path->slots[0]++;
		while (path->slots[0] > 0) {
			btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0] - 1);

			if (found_key.type == BTRFS_FREE_SPACE_INFO_KEY) {
				ASSERT(found_key.objectid == block_group->key.objectid);
				ASSERT(found_key.offset == block_group->key.offset);
				done = 1;
				nr++;
				path->slots[0]--;
				break;
			} else if (found_key.type == BTRFS_FREE_SPACE_EXTENT_KEY ||
				   found_key.type == BTRFS_FREE_SPACE_BITMAP_KEY) {
				ASSERT(found_key.objectid >= start);
				ASSERT(found_key.objectid < end);
				ASSERT(found_key.objectid + found_key.offset <= end);
				nr++;
				path->slots[0]--;
			} else {
				ASSERT(0);
			}
		}

		ret = btrfs_del_items(trans, root, path, path->slots[0], nr);
		if (ret)
			goto out;
		btrfs_release_path(path);
	}

	ret = 0;
out:
	btrfs_free_path(path);
	if (ret)
		btrfs_abort_transaction(trans, ret);
	return ret;
}

static int load_free_space_bitmaps(struct btrfs_caching_control *caching_ctl,
				   struct btrfs_path *path,
				   u32 expected_extent_count)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root;
	struct btrfs_key key;
	int prev_bit = 0, bit;
	/* Initialize to silence GCC. */
	u64 extent_start = 0;
	u64 end, offset;
	u64 total_found = 0;
	u32 extent_count = 0;
	int ret;

	block_group = caching_ctl->block_group;
	fs_info = block_group->fs_info;
	root = fs_info->free_space_root;

	end = block_group->key.objectid + block_group->key.offset;

	while (1) {
		ret = btrfs_next_item(root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.type == BTRFS_FREE_SPACE_INFO_KEY)
			break;

		ASSERT(key.type == BTRFS_FREE_SPACE_BITMAP_KEY);
		ASSERT(key.objectid < end && key.objectid + key.offset <= end);

		caching_ctl->progress = key.objectid;

		offset = key.objectid;
		while (offset < key.objectid + key.offset) {
			bit = free_space_test_bit(block_group, path, offset);
			if (prev_bit == 0 && bit == 1) {
				extent_start = offset;
			} else if (prev_bit == 1 && bit == 0) {
				total_found += add_new_free_space(block_group,
								  fs_info,
								  extent_start,
								  offset);
				if (total_found > CACHING_CTL_WAKE_UP) {
					total_found = 0;
					wake_up(&caching_ctl->wait);
				}
				extent_count++;
			}
			prev_bit = bit;
			offset += fs_info->sectorsize;
		}
	}
	if (prev_bit == 1) {
		total_found += add_new_free_space(block_group, fs_info,
						  extent_start, end);
		extent_count++;
	}

	if (extent_count != expected_extent_count) {
		btrfs_err(fs_info,
			  "incorrect extent count for %llu; counted %u, expected %u",
			  block_group->key.objectid, extent_count,
			  expected_extent_count);
		ASSERT(0);
		ret = -EIO;
		goto out;
	}

	caching_ctl->progress = (u64)-1;

	ret = 0;
out:
	return ret;
}

static int load_free_space_extents(struct btrfs_caching_control *caching_ctl,
				   struct btrfs_path *path,
				   u32 expected_extent_count)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root;
	struct btrfs_key key;
	u64 end;
	u64 total_found = 0;
	u32 extent_count = 0;
	int ret;

	block_group = caching_ctl->block_group;
	fs_info = block_group->fs_info;
	root = fs_info->free_space_root;

	end = block_group->key.objectid + block_group->key.offset;

	while (1) {
		ret = btrfs_next_item(root, path);
		if (ret < 0)
			goto out;
		if (ret)
			break;

		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);

		if (key.type == BTRFS_FREE_SPACE_INFO_KEY)
			break;

		ASSERT(key.type == BTRFS_FREE_SPACE_EXTENT_KEY);
		ASSERT(key.objectid < end && key.objectid + key.offset <= end);

		caching_ctl->progress = key.objectid;

		total_found += add_new_free_space(block_group, fs_info,
						  key.objectid,
						  key.objectid + key.offset);
		if (total_found > CACHING_CTL_WAKE_UP) {
			total_found = 0;
			wake_up(&caching_ctl->wait);
		}
		extent_count++;
	}

	if (extent_count != expected_extent_count) {
		btrfs_err(fs_info,
			  "incorrect extent count for %llu; counted %u, expected %u",
			  block_group->key.objectid, extent_count,
			  expected_extent_count);
		ASSERT(0);
		ret = -EIO;
		goto out;
	}

	caching_ctl->progress = (u64)-1;

	ret = 0;
out:
	return ret;
}

int load_free_space_tree(struct btrfs_caching_control *caching_ctl)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_fs_info *fs_info;
	struct btrfs_free_space_info *info;
	struct btrfs_path *path;
	u32 extent_count, flags;
	int ret;

	block_group = caching_ctl->block_group;
	fs_info = block_group->fs_info;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * Just like caching_thread() doesn't want to deadlock on the extent
	 * tree, we don't want to deadlock on the free space tree.
	 */
	path->skip_locking = 1;
	path->search_commit_root = 1;
	path->reada = 1;

	info = search_free_space_info(NULL, fs_info, block_group, path, 0);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out;
	}
	extent_count = btrfs_free_space_extent_count(path->nodes[0], info);
	flags = btrfs_free_space_flags(path->nodes[0], info);

	/*
	 * We left path pointing to the free space info item, so now
	 * load_free_space_foo can just iterate through the free space tree from
	 * there.
	 */
	if (flags & BTRFS_FREE_SPACE_USING_BITMAPS)
		ret = load_free_space_bitmaps(caching_ctl, path, extent_count);
	else
		ret = load_free_space_extents(caching_ctl, path, extent_count);

out:
	btrfs_free_path(path);
	return ret;
}
