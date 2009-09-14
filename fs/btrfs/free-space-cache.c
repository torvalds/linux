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
#include <linux/math64.h>
#include "ctree.h"
#include "free-space-cache.h"
#include "transaction.h"

#define BITS_PER_BITMAP		(PAGE_CACHE_SIZE * 8)
#define MAX_CACHE_BYTES_PER_GIG	(32 * 1024)

static inline unsigned long offset_to_bit(u64 bitmap_start, u64 sectorsize,
					  u64 offset)
{
	BUG_ON(offset < bitmap_start);
	offset -= bitmap_start;
	return (unsigned long)(div64_u64(offset, sectorsize));
}

static inline unsigned long bytes_to_bits(u64 bytes, u64 sectorsize)
{
	return (unsigned long)(div64_u64(bytes, sectorsize));
}

static inline u64 offset_to_bitmap(struct btrfs_block_group_cache *block_group,
				   u64 offset)
{
	u64 bitmap_start;
	u64 bytes_per_bitmap;

	bytes_per_bitmap = BITS_PER_BITMAP * block_group->sectorsize;
	bitmap_start = offset - block_group->key.objectid;
	bitmap_start = div64_u64(bitmap_start, bytes_per_bitmap);
	bitmap_start *= bytes_per_bitmap;
	bitmap_start += block_group->key.objectid;

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
				WARN_ON(info->bitmap);
				p = &(*p)->rb_right;
			} else {
				WARN_ON(!info->bitmap);
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
tree_search_offset(struct btrfs_block_group_cache *block_group,
		   u64 offset, int bitmap_only, int fuzzy)
{
	struct rb_node *n = block_group->free_space_offset.rb_node;
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
		if (entry->offset + BITS_PER_BITMAP *
		    block_group->sectorsize > offset)
			return entry;
	} else if (entry->offset + entry->bytes > offset)
		return entry;

	if (!fuzzy)
		return NULL;

	while (1) {
		if (entry->bitmap) {
			if (entry->offset + BITS_PER_BITMAP *
			    block_group->sectorsize > offset)
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

static void unlink_free_space(struct btrfs_block_group_cache *block_group,
			      struct btrfs_free_space *info)
{
	rb_erase(&info->offset_index, &block_group->free_space_offset);
	block_group->free_extents--;
	block_group->free_space -= info->bytes;
}

static int link_free_space(struct btrfs_block_group_cache *block_group,
			   struct btrfs_free_space *info)
{
	int ret = 0;

	BUG_ON(!info->bitmap && !info->bytes);
	ret = tree_insert_offset(&block_group->free_space_offset, info->offset,
				 &info->offset_index, (info->bitmap != NULL));
	if (ret)
		return ret;

	block_group->free_space += info->bytes;
	block_group->free_extents++;
	return ret;
}

static void recalculate_thresholds(struct btrfs_block_group_cache *block_group)
{
	u64 max_bytes, possible_bytes;

	/*
	 * The goal is to keep the total amount of memory used per 1gb of space
	 * at or below 32k, so we need to adjust how much memory we allow to be
	 * used by extent based free space tracking
	 */
	max_bytes = MAX_CACHE_BYTES_PER_GIG *
		(div64_u64(block_group->key.offset, 1024 * 1024 * 1024));

	possible_bytes = (block_group->total_bitmaps * PAGE_CACHE_SIZE) +
		(sizeof(struct btrfs_free_space) *
		 block_group->extents_thresh);

	if (possible_bytes > max_bytes) {
		int extent_bytes = max_bytes -
			(block_group->total_bitmaps * PAGE_CACHE_SIZE);

		if (extent_bytes <= 0) {
			block_group->extents_thresh = 0;
			return;
		}

		block_group->extents_thresh = extent_bytes /
			(sizeof(struct btrfs_free_space));
	}
}

static void bitmap_clear_bits(struct btrfs_block_group_cache *block_group,
			      struct btrfs_free_space *info, u64 offset,
			      u64 bytes)
{
	unsigned long start, end;
	unsigned long i;

	start = offset_to_bit(info->offset, block_group->sectorsize, offset);
	end = start + bytes_to_bits(bytes, block_group->sectorsize);
	BUG_ON(end > BITS_PER_BITMAP);

	for (i = start; i < end; i++)
		clear_bit(i, info->bitmap);

	info->bytes -= bytes;
	block_group->free_space -= bytes;
}

static void bitmap_set_bits(struct btrfs_block_group_cache *block_group,
			    struct btrfs_free_space *info, u64 offset,
			    u64 bytes)
{
	unsigned long start, end;
	unsigned long i;

	start = offset_to_bit(info->offset, block_group->sectorsize, offset);
	end = start + bytes_to_bits(bytes, block_group->sectorsize);
	BUG_ON(end > BITS_PER_BITMAP);

	for (i = start; i < end; i++)
		set_bit(i, info->bitmap);

	info->bytes += bytes;
	block_group->free_space += bytes;
}

static int search_bitmap(struct btrfs_block_group_cache *block_group,
			 struct btrfs_free_space *bitmap_info, u64 *offset,
			 u64 *bytes)
{
	unsigned long found_bits = 0;
	unsigned long bits, i;
	unsigned long next_zero;

	i = offset_to_bit(bitmap_info->offset, block_group->sectorsize,
			  max_t(u64, *offset, bitmap_info->offset));
	bits = bytes_to_bits(*bytes, block_group->sectorsize);

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
		*offset = (u64)(i * block_group->sectorsize) +
			bitmap_info->offset;
		*bytes = (u64)(found_bits) * block_group->sectorsize;
		return 0;
	}

	return -1;
}

static struct btrfs_free_space *find_free_space(struct btrfs_block_group_cache
						*block_group, u64 *offset,
						u64 *bytes, int debug)
{
	struct btrfs_free_space *entry;
	struct rb_node *node;
	int ret;

	if (!block_group->free_space_offset.rb_node)
		return NULL;

	entry = tree_search_offset(block_group,
				   offset_to_bitmap(block_group, *offset),
				   0, 1);
	if (!entry)
		return NULL;

	for (node = &entry->offset_index; node; node = rb_next(node)) {
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		if (entry->bytes < *bytes)
			continue;

		if (entry->bitmap) {
			ret = search_bitmap(block_group, entry, offset, bytes);
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

static void add_new_bitmap(struct btrfs_block_group_cache *block_group,
			   struct btrfs_free_space *info, u64 offset)
{
	u64 bytes_per_bg = BITS_PER_BITMAP * block_group->sectorsize;
	int max_bitmaps = (int)div64_u64(block_group->key.offset +
					 bytes_per_bg - 1, bytes_per_bg);
	BUG_ON(block_group->total_bitmaps >= max_bitmaps);

	info->offset = offset_to_bitmap(block_group, offset);
	link_free_space(block_group, info);
	block_group->total_bitmaps++;

	recalculate_thresholds(block_group);
}

static noinline int remove_from_bitmap(struct btrfs_block_group_cache *block_group,
			      struct btrfs_free_space *bitmap_info,
			      u64 *offset, u64 *bytes)
{
	u64 end;
	u64 search_start, search_bytes;
	int ret;

again:
	end = bitmap_info->offset +
		(u64)(BITS_PER_BITMAP * block_group->sectorsize) - 1;

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
	ret = search_bitmap(block_group, bitmap_info, &search_start,
			    &search_bytes);
	BUG_ON(ret < 0 || search_start != *offset);

	if (*offset > bitmap_info->offset && *offset + *bytes > end) {
		bitmap_clear_bits(block_group, bitmap_info, *offset,
				  end - *offset + 1);
		*bytes -= end - *offset + 1;
		*offset = end + 1;
	} else if (*offset >= bitmap_info->offset && *offset + *bytes <= end) {
		bitmap_clear_bits(block_group, bitmap_info, *offset, *bytes);
		*bytes = 0;
	}

	if (*bytes) {
		struct rb_node *next = rb_next(&bitmap_info->offset_index);
		if (!bitmap_info->bytes) {
			unlink_free_space(block_group, bitmap_info);
			kfree(bitmap_info->bitmap);
			kfree(bitmap_info);
			block_group->total_bitmaps--;
			recalculate_thresholds(block_group);
		}

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
		ret = search_bitmap(block_group, bitmap_info, &search_start,
				    &search_bytes);
		if (ret < 0 || search_start != *offset)
			return -EAGAIN;

		goto again;
	} else if (!bitmap_info->bytes) {
		unlink_free_space(block_group, bitmap_info);
		kfree(bitmap_info->bitmap);
		kfree(bitmap_info);
		block_group->total_bitmaps--;
		recalculate_thresholds(block_group);
	}

	return 0;
}

static int insert_into_bitmap(struct btrfs_block_group_cache *block_group,
			      struct btrfs_free_space *info)
{
	struct btrfs_free_space *bitmap_info;
	int added = 0;
	u64 bytes, offset, end;
	int ret;

	/*
	 * If we are below the extents threshold then we can add this as an
	 * extent, and don't have to deal with the bitmap
	 */
	if (block_group->free_extents < block_group->extents_thresh &&
	    info->bytes > block_group->sectorsize * 4)
		return 0;

	/*
	 * some block groups are so tiny they can't be enveloped by a bitmap, so
	 * don't even bother to create a bitmap for this
	 */
	if (BITS_PER_BITMAP * block_group->sectorsize >
	    block_group->key.offset)
		return 0;

	bytes = info->bytes;
	offset = info->offset;

again:
	bitmap_info = tree_search_offset(block_group,
					 offset_to_bitmap(block_group, offset),
					 1, 0);
	if (!bitmap_info) {
		BUG_ON(added);
		goto new_bitmap;
	}

	end = bitmap_info->offset +
		(u64)(BITS_PER_BITMAP * block_group->sectorsize);

	if (offset >= bitmap_info->offset && offset + bytes > end) {
		bitmap_set_bits(block_group, bitmap_info, offset,
				end - offset);
		bytes -= end - offset;
		offset = end;
		added = 0;
	} else if (offset >= bitmap_info->offset && offset + bytes <= end) {
		bitmap_set_bits(block_group, bitmap_info, offset, bytes);
		bytes = 0;
	} else {
		BUG();
	}

	if (!bytes) {
		ret = 1;
		goto out;
	} else
		goto again;

new_bitmap:
	if (info && info->bitmap) {
		add_new_bitmap(block_group, info, offset);
		added = 1;
		info = NULL;
		goto again;
	} else {
		spin_unlock(&block_group->tree_lock);

		/* no pre-allocated info, allocate a new one */
		if (!info) {
			info = kzalloc(sizeof(struct btrfs_free_space),
				       GFP_NOFS);
			if (!info) {
				spin_lock(&block_group->tree_lock);
				ret = -ENOMEM;
				goto out;
			}
		}

		/* allocate the bitmap */
		info->bitmap = kzalloc(PAGE_CACHE_SIZE, GFP_NOFS);
		spin_lock(&block_group->tree_lock);
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
		kfree(info);
	}

	return ret;
}

int btrfs_add_free_space(struct btrfs_block_group_cache *block_group,
			 u64 offset, u64 bytes)
{
	struct btrfs_free_space *right_info = NULL;
	struct btrfs_free_space *left_info = NULL;
	struct btrfs_free_space *info = NULL;
	int ret = 0;

	info = kzalloc(sizeof(struct btrfs_free_space), GFP_NOFS);
	if (!info)
		return -ENOMEM;

	info->offset = offset;
	info->bytes = bytes;

	spin_lock(&block_group->tree_lock);

	/*
	 * first we want to see if there is free space adjacent to the range we
	 * are adding, if there is remove that struct and add a new one to
	 * cover the entire range
	 */
	right_info = tree_search_offset(block_group, offset + bytes, 0, 0);
	if (right_info && rb_prev(&right_info->offset_index))
		left_info = rb_entry(rb_prev(&right_info->offset_index),
				     struct btrfs_free_space, offset_index);
	else
		left_info = tree_search_offset(block_group, offset - 1, 0, 0);

	/*
	 * If there was no extent directly to the left or right of this new
	 * extent then we know we're going to have to allocate a new extent, so
	 * before we do that see if we need to drop this into a bitmap
	 */
	if ((!left_info || left_info->bitmap) &&
	    (!right_info || right_info->bitmap)) {
		ret = insert_into_bitmap(block_group, info);

		if (ret < 0) {
			goto out;
		} else if (ret) {
			ret = 0;
			goto out;
		}
	}

	if (right_info && !right_info->bitmap) {
		unlink_free_space(block_group, right_info);
		info->bytes += right_info->bytes;
		kfree(right_info);
	}

	if (left_info && !left_info->bitmap &&
	    left_info->offset + left_info->bytes == offset) {
		unlink_free_space(block_group, left_info);
		info->offset = left_info->offset;
		info->bytes += left_info->bytes;
		kfree(left_info);
	}

	ret = link_free_space(block_group, info);
	if (ret)
		kfree(info);
out:
	spin_unlock(&block_group->tree_lock);

	if (ret) {
		printk(KERN_CRIT "btrfs: unable to add free space :%d\n", ret);
		BUG_ON(ret == -EEXIST);
	}

	return ret;
}

int btrfs_remove_free_space(struct btrfs_block_group_cache *block_group,
			    u64 offset, u64 bytes)
{
	struct btrfs_free_space *info;
	struct btrfs_free_space *next_info = NULL;
	int ret = 0;

	spin_lock(&block_group->tree_lock);

again:
	info = tree_search_offset(block_group, offset, 0, 0);
	if (!info) {
		/*
		 * oops didn't find an extent that matched the space we wanted
		 * to remove, look for a bitmap instead
		 */
		info = tree_search_offset(block_group,
					  offset_to_bitmap(block_group, offset),
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
			end = next_info->offset + BITS_PER_BITMAP *
				block_group->sectorsize - 1;
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
		unlink_free_space(block_group, info);
		if (info->bitmap) {
			kfree(info->bitmap);
			block_group->total_bitmaps--;
		}
		kfree(info);
		goto out_lock;
	}

	if (!info->bitmap && info->offset == offset) {
		unlink_free_space(block_group, info);
		info->offset += bytes;
		info->bytes -= bytes;
		link_free_space(block_group, info);
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
		unlink_free_space(block_group, info);
		if (offset + bytes < info->offset + info->bytes) {
			u64 old_end = info->offset + info->bytes;

			info->offset = offset + bytes;
			info->bytes = old_end - info->offset;
			ret = link_free_space(block_group, info);
			WARN_ON(ret);
			if (ret)
				goto out_lock;
		} else {
			/* the hole we're creating ends at the end
			 * of the info struct, just free the info
			 */
			kfree(info);
		}
		spin_unlock(&block_group->tree_lock);

		/* step two, insert a new info struct to cover
		 * anything before the hole
		 */
		ret = btrfs_add_free_space(block_group, old_start,
					   offset - old_start);
		WARN_ON(ret);
		goto out;
	}

	ret = remove_from_bitmap(block_group, info, &offset, &bytes);
	if (ret == -EAGAIN)
		goto again;
	BUG_ON(ret);
out_lock:
	spin_unlock(&block_group->tree_lock);
out:
	return ret;
}

void btrfs_dump_free_space(struct btrfs_block_group_cache *block_group,
			   u64 bytes)
{
	struct btrfs_free_space *info;
	struct rb_node *n;
	int count = 0;

	for (n = rb_first(&block_group->free_space_offset); n; n = rb_next(n)) {
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

u64 btrfs_block_group_free_space(struct btrfs_block_group_cache *block_group)
{
	struct btrfs_free_space *info;
	struct rb_node *n;
	u64 ret = 0;

	for (n = rb_first(&block_group->free_space_offset); n;
	     n = rb_next(n)) {
		info = rb_entry(n, struct btrfs_free_space, offset_index);
		ret += info->bytes;
	}

	return ret;
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
	struct btrfs_free_space *entry;
	struct rb_node *node;
	bool bitmap;

	spin_lock(&cluster->lock);
	if (cluster->block_group != block_group)
		goto out;

	bitmap = cluster->points_to_bitmap;
	cluster->block_group = NULL;
	cluster->window_start = 0;
	list_del_init(&cluster->block_group_list);
	cluster->points_to_bitmap = false;

	if (bitmap)
		goto out;

	node = rb_first(&cluster->root);
	while (node) {
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		node = rb_next(&entry->offset_index);
		rb_erase(&entry->offset_index, &cluster->root);
		BUG_ON(entry->bitmap);
		tree_insert_offset(&block_group->free_space_offset,
				   entry->offset, &entry->offset_index, 0);
	}
	cluster->root.rb_node = NULL;

out:
	spin_unlock(&cluster->lock);
	btrfs_put_block_group(block_group);
	return 0;
}

void btrfs_remove_free_space_cache(struct btrfs_block_group_cache *block_group)
{
	struct btrfs_free_space *info;
	struct rb_node *node;
	struct btrfs_free_cluster *cluster;
	struct list_head *head;

	spin_lock(&block_group->tree_lock);
	while ((head = block_group->cluster_list.next) !=
	       &block_group->cluster_list) {
		cluster = list_entry(head, struct btrfs_free_cluster,
				     block_group_list);

		WARN_ON(cluster->block_group != block_group);
		__btrfs_return_cluster_to_free_space(block_group, cluster);
		if (need_resched()) {
			spin_unlock(&block_group->tree_lock);
			cond_resched();
			spin_lock(&block_group->tree_lock);
		}
	}

	while ((node = rb_last(&block_group->free_space_offset)) != NULL) {
		info = rb_entry(node, struct btrfs_free_space, offset_index);
		unlink_free_space(block_group, info);
		if (info->bitmap)
			kfree(info->bitmap);
		kfree(info);
		if (need_resched()) {
			spin_unlock(&block_group->tree_lock);
			cond_resched();
			spin_lock(&block_group->tree_lock);
		}
	}

	spin_unlock(&block_group->tree_lock);
}

u64 btrfs_find_space_for_alloc(struct btrfs_block_group_cache *block_group,
			       u64 offset, u64 bytes, u64 empty_size)
{
	struct btrfs_free_space *entry = NULL;
	u64 bytes_search = bytes + empty_size;
	u64 ret = 0;

	spin_lock(&block_group->tree_lock);
	entry = find_free_space(block_group, &offset, &bytes_search, 0);
	if (!entry)
		goto out;

	ret = offset;
	if (entry->bitmap) {
		bitmap_clear_bits(block_group, entry, offset, bytes);
		if (!entry->bytes) {
			unlink_free_space(block_group, entry);
			kfree(entry->bitmap);
			kfree(entry);
			block_group->total_bitmaps--;
			recalculate_thresholds(block_group);
		}
	} else {
		unlink_free_space(block_group, entry);
		entry->offset += bytes;
		entry->bytes -= bytes;
		if (!entry->bytes)
			kfree(entry);
		else
			link_free_space(block_group, entry);
	}

out:
	spin_unlock(&block_group->tree_lock);

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

	/* now return any extents the cluster had on it */
	spin_lock(&block_group->tree_lock);
	ret = __btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&block_group->tree_lock);

	/* finally drop our ref */
	btrfs_put_block_group(block_group);
	return ret;
}

static u64 btrfs_alloc_from_bitmap(struct btrfs_block_group_cache *block_group,
				   struct btrfs_free_cluster *cluster,
				   u64 bytes, u64 min_start)
{
	struct btrfs_free_space *entry;
	int err;
	u64 search_start = cluster->window_start;
	u64 search_bytes = bytes;
	u64 ret = 0;

	spin_lock(&block_group->tree_lock);
	spin_lock(&cluster->lock);

	if (!cluster->points_to_bitmap)
		goto out;

	if (cluster->block_group != block_group)
		goto out;

	/*
	 * search_start is the beginning of the bitmap, but at some point it may
	 * be a good idea to point to the actual start of the free area in the
	 * bitmap, so do the offset_to_bitmap trick anyway, and set bitmap_only
	 * to 1 to make sure we get the bitmap entry
	 */
	entry = tree_search_offset(block_group,
				   offset_to_bitmap(block_group, search_start),
				   1, 0);
	if (!entry || !entry->bitmap)
		goto out;

	search_start = min_start;
	search_bytes = bytes;

	err = search_bitmap(block_group, entry, &search_start,
			    &search_bytes);
	if (err)
		goto out;

	ret = search_start;
	bitmap_clear_bits(block_group, entry, ret, bytes);
out:
	spin_unlock(&cluster->lock);
	spin_unlock(&block_group->tree_lock);

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
	struct btrfs_free_space *entry = NULL;
	struct rb_node *node;
	u64 ret = 0;

	if (cluster->points_to_bitmap)
		return btrfs_alloc_from_bitmap(block_group, cluster, bytes,
					       min_start);

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
		if (entry->bytes < bytes || entry->offset < min_start) {
			struct rb_node *node;

			node = rb_next(&entry->offset_index);
			if (!node)
				break;
			entry = rb_entry(node, struct btrfs_free_space,
					 offset_index);
			continue;
		}
		ret = entry->offset;

		entry->offset += bytes;
		entry->bytes -= bytes;

		if (entry->bytes == 0) {
			rb_erase(&entry->offset_index, &cluster->root);
			kfree(entry);
		}
		break;
	}
out:
	spin_unlock(&cluster->lock);

	return ret;
}

static int btrfs_bitmap_cluster(struct btrfs_block_group_cache *block_group,
				struct btrfs_free_space *entry,
				struct btrfs_free_cluster *cluster,
				u64 offset, u64 bytes, u64 min_bytes)
{
	unsigned long next_zero;
	unsigned long i;
	unsigned long search_bits;
	unsigned long total_bits;
	unsigned long found_bits;
	unsigned long start = 0;
	unsigned long total_found = 0;
	bool found = false;

	i = offset_to_bit(entry->offset, block_group->sectorsize,
			  max_t(u64, offset, entry->offset));
	search_bits = bytes_to_bits(min_bytes, block_group->sectorsize);
	total_bits = bytes_to_bits(bytes, block_group->sectorsize);

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
		return -1;

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
	cluster->points_to_bitmap = true;

	return 0;
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
	struct btrfs_free_space *entry = NULL;
	struct rb_node *node;
	struct btrfs_free_space *next;
	struct btrfs_free_space *last = NULL;
	u64 min_bytes;
	u64 window_start;
	u64 window_free;
	u64 max_extent = 0;
	bool found_bitmap = false;
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

	spin_lock(&block_group->tree_lock);
	spin_lock(&cluster->lock);

	/* someone already found a cluster, hooray */
	if (cluster->block_group) {
		ret = 0;
		goto out;
	}
again:
	entry = tree_search_offset(block_group, offset, found_bitmap, 1);
	if (!entry) {
		ret = -ENOSPC;
		goto out;
	}

	/*
	 * If found_bitmap is true, we exhausted our search for extent entries,
	 * and we just want to search all of the bitmaps that we can find, and
	 * ignore any extent entries we find.
	 */
	while (entry->bitmap || found_bitmap ||
	       (!entry->bitmap && entry->bytes < min_bytes)) {
		struct rb_node *node = rb_next(&entry->offset_index);

		if (entry->bitmap && entry->bytes > bytes + empty_size) {
			ret = btrfs_bitmap_cluster(block_group, entry, cluster,
						   offset, bytes + empty_size,
						   min_bytes);
			if (!ret)
				goto got_it;
		}

		if (!node) {
			ret = -ENOSPC;
			goto out;
		}
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
	}

	/*
	 * We already searched all the extent entries from the passed in offset
	 * to the end and didn't find enough space for the cluster, and we also
	 * didn't find any bitmaps that met our criteria, just go ahead and exit
	 */
	if (found_bitmap) {
		ret = -ENOSPC;
		goto out;
	}

	cluster->points_to_bitmap = false;
	window_start = entry->offset;
	window_free = entry->bytes;
	last = entry;
	max_extent = entry->bytes;

	while (1) {
		/* out window is just right, lets fill it */
		if (window_free >= bytes + empty_size)
			break;

		node = rb_next(&last->offset_index);
		if (!node) {
			if (found_bitmap)
				goto again;
			ret = -ENOSPC;
			goto out;
		}
		next = rb_entry(node, struct btrfs_free_space, offset_index);

		/*
		 * we found a bitmap, so if this search doesn't result in a
		 * cluster, we know to go and search again for the bitmaps and
		 * start looking for space there
		 */
		if (next->bitmap) {
			if (!found_bitmap)
				offset = next->offset;
			found_bitmap = true;
			last = next;
			continue;
		}

		/*
		 * we haven't filled the empty size and the window is
		 * very large.  reset and try again
		 */
		if (next->offset - (last->offset + last->bytes) > 128 * 1024 ||
		    next->offset - window_start > (bytes + empty_size) * 2) {
			entry = next;
			window_start = entry->offset;
			window_free = entry->bytes;
			last = entry;
			max_extent = 0;
		} else {
			last = next;
			window_free += next->bytes;
			if (entry->bytes > max_extent)
				max_extent = entry->bytes;
		}
	}

	cluster->window_start = entry->offset;

	/*
	 * now we've found our entries, pull them out of the free space
	 * cache and put them into the cluster rbtree
	 *
	 * The cluster includes an rbtree, but only uses the offset index
	 * of each free space cache entry.
	 */
	while (1) {
		node = rb_next(&entry->offset_index);
		if (entry->bitmap && node) {
			entry = rb_entry(node, struct btrfs_free_space,
					 offset_index);
			continue;
		} else if (entry->bitmap && !node) {
			break;
		}

		rb_erase(&entry->offset_index, &block_group->free_space_offset);
		ret = tree_insert_offset(&cluster->root, entry->offset,
					 &entry->offset_index, 0);
		BUG_ON(ret);

		if (!node || entry == last)
			break;

		entry = rb_entry(node, struct btrfs_free_space, offset_index);
	}

	cluster->max_size = max_extent;
got_it:
	ret = 0;
	atomic_inc(&block_group->count);
	list_add_tail(&cluster->block_group_list, &block_group->cluster_list);
	cluster->block_group = block_group;
out:
	spin_unlock(&cluster->lock);
	spin_unlock(&block_group->tree_lock);

	return ret;
}

/*
 * simple code to zero out a cluster
 */
void btrfs_init_free_cluster(struct btrfs_free_cluster *cluster)
{
	spin_lock_init(&cluster->lock);
	spin_lock_init(&cluster->refill_lock);
	cluster->root.rb_node = NULL;
	cluster->max_size = 0;
	cluster->points_to_bitmap = false;
	INIT_LIST_HEAD(&cluster->block_group_list);
	cluster->block_group = NULL;
}

