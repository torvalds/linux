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
#include "free-space-cache.h"
#include "transaction.h"

struct btrfs_free_space {
	struct rb_node bytes_index;
	struct rb_node offset_index;
	u64 offset;
	u64 bytes;
};

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
 * searches the tree for the given offset.
 *
 * fuzzy == 1: this is used for allocations where we are given a hint of where
 * to look for free space.  Because the hint may not be completely on an offset
 * mark, or the hint may no longer point to free space we need to fudge our
 * results a bit.  So we look for free space starting at or after offset with at
 * least bytes size.  We prefer to find as close to the given offset as we can.
 * Also if the offset is within a free space range, then we will return the free
 * space that contains the given offset, which means we can return a free space
 * chunk with an offset before the provided offset.
 *
 * fuzzy == 0: this is just a normal tree search.  Give us the free space that
 * starts at the given offset which is at least bytes size, and if its not there
 * return NULL.
 */
static struct btrfs_free_space *tree_search_offset(struct rb_root *root,
						   u64 offset, u64 bytes,
						   int fuzzy)
{
	struct rb_node *n = root->rb_node;
	struct btrfs_free_space *entry, *ret = NULL;

	while (n) {
		entry = rb_entry(n, struct btrfs_free_space, offset_index);

		if (offset < entry->offset) {
			if (fuzzy &&
			    (!ret || entry->offset < ret->offset) &&
			    (bytes <= entry->bytes))
				ret = entry;
			n = n->rb_left;
		} else if (offset > entry->offset) {
			if (fuzzy &&
			    (entry->offset + entry->bytes - 1) >= offset &&
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


	BUG_ON(!info->bytes);
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

int btrfs_add_free_space(struct btrfs_block_group_cache *block_group,
			 u64 offset, u64 bytes)
{
	struct btrfs_free_space *right_info;
	struct btrfs_free_space *left_info;
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
	right_info = tree_search_offset(&block_group->free_space_offset,
					offset+bytes, 0, 0);
	left_info = tree_search_offset(&block_group->free_space_offset,
				       offset-1, 0, 1);

	if (right_info) {
		unlink_free_space(block_group, right_info);
		info->bytes += right_info->bytes;
		kfree(right_info);
	}

	if (left_info && left_info->offset + left_info->bytes == offset) {
		unlink_free_space(block_group, left_info);
		info->offset = left_info->offset;
		info->bytes += left_info->bytes;
		kfree(left_info);
	}

	ret = link_free_space(block_group, info);
	if (ret)
		kfree(info);

	spin_unlock(&block_group->tree_lock);

	if (ret) {
		printk(KERN_ERR "btrfs: unable to add free space :%d\n", ret);
		BUG_ON(ret == -EEXIST);
	}

	return ret;
}

int btrfs_remove_free_space(struct btrfs_block_group_cache *block_group,
			    u64 offset, u64 bytes)
{
	struct btrfs_free_space *info;
	int ret = 0;

	spin_lock(&block_group->tree_lock);

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
			spin_unlock(&block_group->tree_lock);
			goto out;
		}
		unlink_free_space(block_group, info);

		if (info->bytes == bytes) {
			kfree(info);
			spin_unlock(&block_group->tree_lock);
			goto out;
		}

		info->offset += bytes;
		info->bytes -= bytes;

		ret = link_free_space(block_group, info);
		spin_unlock(&block_group->tree_lock);
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
		spin_unlock(&block_group->tree_lock);
		/* step two, insert a new info struct to cover anything
		 * before the hole
		 */
		ret = btrfs_add_free_space(block_group, old_start,
					   offset - old_start);
		BUG_ON(ret);
	} else {
		spin_unlock(&block_group->tree_lock);
		if (!info) {
			printk(KERN_ERR "couldn't find space %llu to free\n",
			       (unsigned long long)offset);
			printk(KERN_ERR "cached is %d, offset %llu bytes %llu\n",
			       block_group->cached,
			       (unsigned long long)block_group->key.objectid,
			       (unsigned long long)block_group->key.offset);
			btrfs_dump_free_space(block_group, bytes);
		} else if (info) {
			printk(KERN_ERR "hmm, found offset=%llu bytes=%llu, "
			       "but wanted offset=%llu bytes=%llu\n",
			       (unsigned long long)info->offset,
			       (unsigned long long)info->bytes,
			       (unsigned long long)offset,
			       (unsigned long long)bytes);
		}
		WARN_ON(1);
	}
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
		printk(KERN_ERR "entry offset %llu, bytes %llu\n",
		       (unsigned long long)info->offset,
		       (unsigned long long)info->bytes);
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

	spin_lock(&cluster->lock);
	if (cluster->block_group != block_group)
		goto out;

	cluster->window_start = 0;
	node = rb_first(&cluster->root);
	while(node) {
		entry = rb_entry(node, struct btrfs_free_space, offset_index);
		node = rb_next(&entry->offset_index);
		rb_erase(&entry->offset_index, &cluster->root);
		link_free_space(block_group, entry);
	}
	list_del_init(&cluster->block_group_list);

	btrfs_put_block_group(cluster->block_group);
	cluster->block_group = NULL;
	cluster->root.rb_node = NULL;
out:
	spin_unlock(&cluster->lock);
	return 0;
}

void btrfs_remove_free_space_cache(struct btrfs_block_group_cache *block_group)
{
	struct btrfs_free_space *info;
	struct rb_node *node;
	struct btrfs_free_cluster *cluster;
	struct btrfs_free_cluster *safe;

	spin_lock(&block_group->tree_lock);

	list_for_each_entry_safe(cluster, safe, &block_group->cluster_list,
				 block_group_list) {

		WARN_ON(cluster->block_group != block_group);
		__btrfs_return_cluster_to_free_space(block_group, cluster);
	}

	while ((node = rb_last(&block_group->free_space_bytes)) != NULL) {
		info = rb_entry(node, struct btrfs_free_space, bytes_index);
		unlink_free_space(block_group, info);
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
	u64 ret = 0;

	spin_lock(&block_group->tree_lock);
	entry = tree_search_offset(&block_group->free_space_offset, offset,
				   bytes + empty_size, 1);
	if (!entry)
		entry = tree_search_bytes(&block_group->free_space_bytes,
					  offset, bytes + empty_size);
	if (entry) {
		unlink_free_space(block_group, entry);
		ret = entry->offset;
		entry->offset += bytes;
		entry->bytes -= bytes;

		if (!entry->bytes)
			kfree(entry);
		else
			link_free_space(block_group, entry);
	}
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
	struct btrfs_free_space *last;
	u64 min_bytes;
	u64 window_start;
	u64 window_free;
	u64 max_extent = 0;
	int total_retries = 0;
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
	min_bytes = min(min_bytes, bytes + empty_size);
	entry = tree_search_bytes(&block_group->free_space_bytes,
				  offset, min_bytes);
	if (!entry) {
		ret = -ENOSPC;
		goto out;
	}
	window_start = entry->offset;
	window_free = entry->bytes;
	last = entry;
	max_extent = entry->bytes;

	while(1) {
		/* out window is just right, lets fill it */
		if (window_free >= bytes + empty_size)
			break;

		node = rb_next(&last->offset_index);
		if (!node) {
			ret = -ENOSPC;
			goto out;
		}
		next = rb_entry(node, struct btrfs_free_space, offset_index);

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
			total_retries++;
			if (total_retries % 64 == 0) {
				if (min_bytes >= (bytes + empty_size)) {
					ret = -ENOSPC;
					goto out;
				}
				/*
				 * grow our allocation a bit, we're not having
				 * much luck
				 */
				min_bytes *= 2;
				goto again;
			}
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
	while(1) {
		node = rb_next(&entry->offset_index);
		unlink_free_space(block_group, entry);
		ret = tree_insert_offset(&cluster->root, entry->offset,
					 &entry->offset_index);
		BUG_ON(ret);

		if (!node || entry == last)
			break;

		entry = rb_entry(node, struct btrfs_free_space, offset_index);
	}
	ret = 0;
	cluster->max_size = max_extent;
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
	INIT_LIST_HEAD(&cluster->block_group_list);
	cluster->block_group = NULL;
}

