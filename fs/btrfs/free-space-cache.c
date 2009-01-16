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

#include <linux/sched.h>
#include "ctree.h"

static int tree_insert_offset(struct rb_root *root, u64 offset,
			      struct rb_node *node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_free_space *info;

	while (*p) {
		parent = *p;
		info = rb_entry(parent, struct btrfs_free_space, offset_index);

		if (offset < info->offset)
			p = &(*p)->rb_left;
		else if (offset > info->offset)
			p = &(*p)->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, root);

	return 0;
}

static int tree_insert_bytes(struct rb_root *root, u64 bytes,
			     struct rb_node *node)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct btrfs_free_space *info;

	while (*p) {
		parent = *p;
		info = rb_entry(parent, struct btrfs_free_space, bytes_index);

		if (bytes < info->bytes)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, root);

	return 0;
}

/*
 * searches the tree for the given offset.  If contains is set we will return
 * the free space that contains the given offset.  If contains is not set we
 * will return the free space that starts at or after the given offset and is
 * at least bytes long.
 */
static struct btrfs_free_space *tree_search_offset(struct rb_root *root,
						   u64 offset, u64 bytes,
						   int contains)
{
	struct rb_node *n = root->rb_node;
	struct btrfs_free_space *entry, *ret = NULL;

	while (n) {
		entry = rb_entry(n, struct btrfs_free_space, offset_index);

		if (offset < entry->offset) {
			if (!contains &&
			    (!ret || entry->offset < ret->offset) &&
			    (bytes <= entry->bytes))
				ret = entry;
			n = n->rb_left;
		} else if (offset > entry->offset) {
			if ((entry->offset + entry->bytes - 1) >= offset &&
			    bytes <= entry->bytes) {
				ret = entry;
				break;
			}
			n = n->rb_right;
		} else {
			if (bytes > entry->bytes) {
				n = n->rb_right;
				continue;
			}
			ret = entry;
			break;
		}
	}

	return ret;
}

/*
 * return a chunk at least bytes size, as close to offset that we can get.
 */
static struct btrfs_free_space *tree_search_bytes(struct rb_root *root,
						  u64 offset, u64 bytes)
{
	struct rb_node *n = root->rb_node;
	struct btrfs_free_space *entry, *ret = NULL;

	while (n) {
		entry = rb_entry(n, struct btrfs_free_space, bytes_index);

		if (bytes < entry->bytes) {
			/*
			 * We prefer to get a hole size as close to the size we
			 * are asking for so we don't take small slivers out of
			 * huge holes, but we also want to get as close to the
			 * offset as possible so we don't have a whole lot of
			 * fragmentation.
			 */
			if (offset <= entry->offset) {
				if (!ret)
					ret = entry;
				else if (entry->bytes < ret->bytes)
					ret = entry;
				else if (entry->offset < ret->offset)
					ret = entry;
			}
			n = n->rb_left;
		} else if (bytes > entry->bytes) {
			n = n->rb_right;
		} else {
			/*
			 * Ok we may have multiple chunks of the wanted size,
			 * so we don't want to take the first one we find, we
			 * want to take the one closest to our given offset, so
			 * keep searching just in case theres a better match.
			 */
			n = n->rb_right;
			if (offset > entry->offset)
				continue;
			else if (!ret || entry->offset < ret->offset)
				ret = entry;
		}
	}

	return ret;
}

static void unlink_free_space(struct btrfs_block_group_cache *block_group,
			      struct btrfs_free_space *info)
{
	rb_erase(&info->offset_index, &block_group->free_space_offset);
	rb_erase(&info->bytes_index, &block_group->free_space_bytes);
}

static int link_free_space(struct btrfs_block_group_cache *block_group,
			   struct btrfs_free_space *info)
{
	int ret = 0;


	ret = tree_insert_offset(&block_group->free_space_offset, info->offset,
				 &info->offset_index);
	if (ret)
		return ret;

	ret = tree_insert_bytes(&block_group->free_space_bytes, info->bytes,
				&info->bytes_index);
	if (ret)
		return ret;

	return ret;
}

static int __btrfs_add_free_space(struct btrfs_block_group_cache *block_group,
				  u64 offset, u64 bytes)
{
	struct btrfs_free_space *right_info;
	struct btrfs_free_space *left_info;
	struct btrfs_free_space *info = NULL;
	struct btrfs_free_space *alloc_info;
	int ret = 0;

	alloc_info = kzalloc(sizeof(struct btrfs_free_space), GFP_NOFS);
	if (!alloc_info)
		return -ENOMEM;

	/*
	 * first we want to see if there is free space adjacent to the range we
	 * are adding, if there is remove that struct and add a new one to
	 * cover the entire range
	 */
	right_info = tree_search_offset(&block_group->free_space_offset,
					offset+bytes, 0, 1);
	left_info = tree_search_offset(&block_group->free_space_offset,
				       offset-1, 0, 1);

	if (right_info && right_info->offset == offset+bytes) {
		unlink_free_space(block_group, right_info);
		info = right_info;
		info->offset = offset;
		info->bytes += bytes;
	} else if (right_info && right_info->offset != offset+bytes) {
		printk(KERN_ERR "btrfs adding space in the middle of an "
		       "existing free space area. existing: "
		       "offset=%llu, bytes=%llu. new: offset=%llu, "
		       "bytes=%llu\n", (unsigned long long)right_info->offset,
		       (unsigned long long)right_info->bytes,
		       (unsigned long long)offset,
		       (unsigned long long)bytes);
		BUG();
	}

	if (left_info) {
		unlink_free_space(block_group, left_info);

		if (unlikely((left_info->offset + left_info->bytes) !=
			     offset)) {
			printk(KERN_ERR "btrfs free space to the left "
			       "of new free space isn't "
			       "quite right. existing: offset=%llu, "
			       "bytes=%llu. new: offset=%llu, bytes=%llu\n",
			       (unsigned long long)left_info->offset,
			       (unsigned long long)left_info->bytes,
			       (unsigned long long)offset,
			       (unsigned long long)bytes);
			BUG();
		}

		if (info) {
			info->offset = left_info->offset;
			info->bytes += left_info->bytes;
			kfree(left_info);
		} else {
			info = left_info;
			info->bytes += bytes;
		}
	}

	if (info) {
		ret = link_free_space(block_group, info);
		if (!ret)
			info = NULL;
		goto out;
	}

	info = alloc_info;
	alloc_info = NULL;
	info->offset = offset;
	info->bytes = bytes;

	ret = link_free_space(block_group, info);
	if (ret)
		kfree(info);
out:
	if (ret) {
		printk(KERN_ERR "btrfs: unable to add free space :%d\n", ret);
		if (ret == -EEXIST)
			BUG();
	}

	kfree(alloc_info);

	return ret;
}

static int
__btrfs_remove_free_space(struct btrfs_block_group_cache *block_group,
			  u64 offset, u64 bytes)
{
	struct btrfs_free_space *info;
	int ret = 0;

	info = tree_search_offset(&block_group->free_space_offset, offset, 0,
				  1);

	if (info && info->offset == offset) {
		if (info->bytes < bytes) {
			printk(KERN_ERR "Found free space at %llu, size %llu,"
			       "trying to use %llu\n",
			       (unsigned long long)info->offset,
			       (unsigned long long)info->bytes,
			       (unsigned long long)bytes);
			WARN_ON(1);
			ret = -EINVAL;
			goto out;
		}
		unlink_free_space(block_group, info);

		if (info->bytes == bytes) {
			kfree(info);
			goto out;
		}

		info->offset += bytes;
		info->bytes -= bytes;

		ret = link_free_space(block_group, info);
		BUG_ON(ret);
	} else if (info && info->offset < offset &&
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
			BUG_ON(ret);
		} else {
			/* the hole we're creating ends at the end
			 * of the info struct, just free the info
			 */
			kfree(info);
		}

		/* step two, insert a new info struct to cover anything
		 * before the hole
		 */
		ret = __btrfs_add_free_space(block_group, old_start,
					     offset - old_start);
		BUG_ON(ret);
	} else {
		WARN_ON(1);
	}
out:
	return ret;
}

int btrfs_add_free_space(struct btrfs_block_group_cache *block_group,
			 u64 offset, u64 bytes)
{
	int ret;
	struct btrfs_free_space *sp;

	mutex_lock(&block_group->alloc_mutex);
	ret = __btrfs_add_free_space(block_group, offset, bytes);
	sp = tree_search_offset(&block_group->free_space_offset, offset, 0, 1);
	BUG_ON(!sp);
	mutex_unlock(&block_group->alloc_mutex);

	return ret;
}

int btrfs_add_free_space_lock(struct btrfs_block_group_cache *block_group,
			      u64 offset, u64 bytes)
{
	int ret;
	struct btrfs_free_space *sp;

	ret = __btrfs_add_free_space(block_group, offset, bytes);
	sp = tree_search_offset(&block_group->free_space_offset, offset, 0, 1);
	BUG_ON(!sp);

	return ret;
}

int btrfs_remove_free_space(struct btrfs_block_group_cache *block_group,
			    u64 offset, u64 bytes)
{
	int ret = 0;

	mutex_lock(&block_group->alloc_mutex);
	ret = __btrfs_remove_free_space(block_group, offset, bytes);
	mutex_unlock(&block_group->alloc_mutex);

	return ret;
}

int btrfs_remove_free_space_lock(struct btrfs_block_group_cache *block_group,
				 u64 offset, u64 bytes)
{
	int ret;

	ret = __btrfs_remove_free_space(block_group, offset, bytes);

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
	}
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

void btrfs_remove_free_space_cache(struct btrfs_block_group_cache *block_group)
{
	struct btrfs_free_space *info;
	struct rb_node *node;

	mutex_lock(&block_group->alloc_mutex);
	while ((node = rb_last(&block_group->free_space_bytes)) != NULL) {
		info = rb_entry(node, struct btrfs_free_space, bytes_index);
		unlink_free_space(block_group, info);
		kfree(info);
		if (need_resched()) {
			mutex_unlock(&block_group->alloc_mutex);
			cond_resched();
			mutex_lock(&block_group->alloc_mutex);
		}
	}
	mutex_unlock(&block_group->alloc_mutex);
}

#if 0
static struct btrfs_free_space *btrfs_find_free_space_offset(struct
						      btrfs_block_group_cache
						      *block_group, u64 offset,
						      u64 bytes)
{
	struct btrfs_free_space *ret;

	mutex_lock(&block_group->alloc_mutex);
	ret = tree_search_offset(&block_group->free_space_offset, offset,
				 bytes, 0);
	mutex_unlock(&block_group->alloc_mutex);

	return ret;
}

static struct btrfs_free_space *btrfs_find_free_space_bytes(struct
						     btrfs_block_group_cache
						     *block_group, u64 offset,
						     u64 bytes)
{
	struct btrfs_free_space *ret;

	mutex_lock(&block_group->alloc_mutex);

	ret = tree_search_bytes(&block_group->free_space_bytes, offset, bytes);
	mutex_unlock(&block_group->alloc_mutex);

	return ret;
}
#endif

struct btrfs_free_space *btrfs_find_free_space(struct btrfs_block_group_cache
					       *block_group, u64 offset,
					       u64 bytes)
{
	struct btrfs_free_space *ret = NULL;

	ret = tree_search_offset(&block_group->free_space_offset, offset,
				 bytes, 0);
	if (!ret)
		ret = tree_search_bytes(&block_group->free_space_bytes,
					offset, bytes);

	return ret;
}
