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
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/sort.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include "compat.h"
#include "hash.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"
#include "volumes.h"
#include "raid56.h"
#include "locking.h"
#include "free-space-cache.h"
#include "math.h"

#undef SCRAMBLE_DELAYED_REFS

/*
 * control flags for do_chunk_alloc's force field
 * CHUNK_ALLOC_NO_FORCE means to only allocate a chunk
 * if we really need one.
 *
 * CHUNK_ALLOC_LIMITED means to only try and allocate one
 * if we have very few chunks already allocated.  This is
 * used as part of the clustering code to help make sure
 * we have a good pool of storage to cluster in, without
 * filling the FS with empty chunks
 *
 * CHUNK_ALLOC_FORCE means it must try to allocate one
 *
 */
enum {
	CHUNK_ALLOC_NO_FORCE = 0,
	CHUNK_ALLOC_LIMITED = 1,
	CHUNK_ALLOC_FORCE = 2,
};

/*
 * Control how reservations are dealt with.
 *
 * RESERVE_FREE - freeing a reservation.
 * RESERVE_ALLOC - allocating space and we need to update bytes_may_use for
 *   ENOSPC accounting
 * RESERVE_ALLOC_NO_ACCOUNT - allocating space and we should not update
 *   bytes_may_use as the ENOSPC accounting is done elsewhere
 */
enum {
	RESERVE_FREE = 0,
	RESERVE_ALLOC = 1,
	RESERVE_ALLOC_NO_ACCOUNT = 2,
};

static int update_block_group(struct btrfs_root *root,
			      u64 bytenr, u64 num_bytes, int alloc);
static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes, u64 parent,
				u64 root_objectid, u64 owner_objectid,
				u64 owner_offset, int refs_to_drop,
				struct btrfs_delayed_extent_op *extra_op);
static void __run_delayed_extent_op(struct btrfs_delayed_extent_op *extent_op,
				    struct extent_buffer *leaf,
				    struct btrfs_extent_item *ei);
static int alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      u64 parent, u64 root_objectid,
				      u64 flags, u64 owner, u64 offset,
				      struct btrfs_key *ins, int ref_mod);
static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 parent, u64 root_objectid,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins);
static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_root *extent_root, u64 flags,
			  int force);
static int find_next_key(struct btrfs_path *path, int level,
			 struct btrfs_key *key);
static void dump_space_info(struct btrfs_space_info *info, u64 bytes,
			    int dump_block_groups);
static int btrfs_update_reserved_bytes(struct btrfs_block_group_cache *cache,
				       u64 num_bytes, int reserve);
static int block_rsv_use_bytes(struct btrfs_block_rsv *block_rsv,
			       u64 num_bytes);

static noinline int
block_group_cache_done(struct btrfs_block_group_cache *cache)
{
	smp_mb();
	return cache->cached == BTRFS_CACHE_FINISHED;
}

static int block_group_bits(struct btrfs_block_group_cache *cache, u64 bits)
{
	return (cache->flags & bits) == bits;
}

static void btrfs_get_block_group(struct btrfs_block_group_cache *cache)
{
	atomic_inc(&cache->count);
}

void btrfs_put_block_group(struct btrfs_block_group_cache *cache)
{
	if (atomic_dec_and_test(&cache->count)) {
		WARN_ON(cache->pinned > 0);
		WARN_ON(cache->reserved > 0);
		kfree(cache->free_space_ctl);
		kfree(cache);
	}
}

/*
 * this adds the block group to the fs_info rb tree for the block group
 * cache
 */
static int btrfs_add_block_group_cache(struct btrfs_fs_info *info,
				struct btrfs_block_group_cache *block_group)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct btrfs_block_group_cache *cache;

	spin_lock(&info->block_group_cache_lock);
	p = &info->block_group_cache_tree.rb_node;

	while (*p) {
		parent = *p;
		cache = rb_entry(parent, struct btrfs_block_group_cache,
				 cache_node);
		if (block_group->key.objectid < cache->key.objectid) {
			p = &(*p)->rb_left;
		} else if (block_group->key.objectid > cache->key.objectid) {
			p = &(*p)->rb_right;
		} else {
			spin_unlock(&info->block_group_cache_lock);
			return -EEXIST;
		}
	}

	rb_link_node(&block_group->cache_node, parent, p);
	rb_insert_color(&block_group->cache_node,
			&info->block_group_cache_tree);

	if (info->first_logical_byte > block_group->key.objectid)
		info->first_logical_byte = block_group->key.objectid;

	spin_unlock(&info->block_group_cache_lock);

	return 0;
}

/*
 * This will return the block group at or after bytenr if contains is 0, else
 * it will return the block group that contains the bytenr
 */
static struct btrfs_block_group_cache *
block_group_cache_tree_search(struct btrfs_fs_info *info, u64 bytenr,
			      int contains)
{
	struct btrfs_block_group_cache *cache, *ret = NULL;
	struct rb_node *n;
	u64 end, start;

	spin_lock(&info->block_group_cache_lock);
	n = info->block_group_cache_tree.rb_node;

	while (n) {
		cache = rb_entry(n, struct btrfs_block_group_cache,
				 cache_node);
		end = cache->key.objectid + cache->key.offset - 1;
		start = cache->key.objectid;

		if (bytenr < start) {
			if (!contains && (!ret || start < ret->key.objectid))
				ret = cache;
			n = n->rb_left;
		} else if (bytenr > start) {
			if (contains && bytenr <= end) {
				ret = cache;
				break;
			}
			n = n->rb_right;
		} else {
			ret = cache;
			break;
		}
	}
	if (ret) {
		btrfs_get_block_group(ret);
		if (bytenr == 0 && info->first_logical_byte > ret->key.objectid)
			info->first_logical_byte = ret->key.objectid;
	}
	spin_unlock(&info->block_group_cache_lock);

	return ret;
}

static int add_excluded_extent(struct btrfs_root *root,
			       u64 start, u64 num_bytes)
{
	u64 end = start + num_bytes - 1;
	set_extent_bits(&root->fs_info->freed_extents[0],
			start, end, EXTENT_UPTODATE, GFP_NOFS);
	set_extent_bits(&root->fs_info->freed_extents[1],
			start, end, EXTENT_UPTODATE, GFP_NOFS);
	return 0;
}

static void free_excluded_extents(struct btrfs_root *root,
				  struct btrfs_block_group_cache *cache)
{
	u64 start, end;

	start = cache->key.objectid;
	end = start + cache->key.offset - 1;

	clear_extent_bits(&root->fs_info->freed_extents[0],
			  start, end, EXTENT_UPTODATE, GFP_NOFS);
	clear_extent_bits(&root->fs_info->freed_extents[1],
			  start, end, EXTENT_UPTODATE, GFP_NOFS);
}

static int exclude_super_stripes(struct btrfs_root *root,
				 struct btrfs_block_group_cache *cache)
{
	u64 bytenr;
	u64 *logical;
	int stripe_len;
	int i, nr, ret;

	if (cache->key.objectid < BTRFS_SUPER_INFO_OFFSET) {
		stripe_len = BTRFS_SUPER_INFO_OFFSET - cache->key.objectid;
		cache->bytes_super += stripe_len;
		ret = add_excluded_extent(root, cache->key.objectid,
					  stripe_len);
		if (ret)
			return ret;
	}

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		ret = btrfs_rmap_block(&root->fs_info->mapping_tree,
				       cache->key.objectid, bytenr,
				       0, &logical, &nr, &stripe_len);
		if (ret)
			return ret;

		while (nr--) {
			cache->bytes_super += stripe_len;
			ret = add_excluded_extent(root, logical[nr],
						  stripe_len);
			if (ret) {
				kfree(logical);
				return ret;
			}
		}

		kfree(logical);
	}
	return 0;
}

static struct btrfs_caching_control *
get_caching_control(struct btrfs_block_group_cache *cache)
{
	struct btrfs_caching_control *ctl;

	spin_lock(&cache->lock);
	if (cache->cached != BTRFS_CACHE_STARTED) {
		spin_unlock(&cache->lock);
		return NULL;
	}

	/* We're loading it the fast way, so we don't have a caching_ctl. */
	if (!cache->caching_ctl) {
		spin_unlock(&cache->lock);
		return NULL;
	}

	ctl = cache->caching_ctl;
	atomic_inc(&ctl->count);
	spin_unlock(&cache->lock);
	return ctl;
}

static void put_caching_control(struct btrfs_caching_control *ctl)
{
	if (atomic_dec_and_test(&ctl->count))
		kfree(ctl);
}

/*
 * this is only called by cache_block_group, since we could have freed extents
 * we need to check the pinned_extents for any extents that can't be used yet
 * since their free space will be released as soon as the transaction commits.
 */
static u64 add_new_free_space(struct btrfs_block_group_cache *block_group,
			      struct btrfs_fs_info *info, u64 start, u64 end)
{
	u64 extent_start, extent_end, size, total_added = 0;
	int ret;

	while (start < end) {
		ret = find_first_extent_bit(info->pinned_extents, start,
					    &extent_start, &extent_end,
					    EXTENT_DIRTY | EXTENT_UPTODATE,
					    NULL);
		if (ret)
			break;

		if (extent_start <= start) {
			start = extent_end + 1;
		} else if (extent_start > start && extent_start < end) {
			size = extent_start - start;
			total_added += size;
			ret = btrfs_add_free_space(block_group, start,
						   size);
			BUG_ON(ret); /* -ENOMEM or logic error */
			start = extent_end + 1;
		} else {
			break;
		}
	}

	if (start < end) {
		size = end - start;
		total_added += size;
		ret = btrfs_add_free_space(block_group, start, size);
		BUG_ON(ret); /* -ENOMEM or logic error */
	}

	return total_added;
}

static noinline void caching_thread(struct btrfs_work *work)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_fs_info *fs_info;
	struct btrfs_caching_control *caching_ctl;
	struct btrfs_root *extent_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 total_found = 0;
	u64 last = 0;
	u32 nritems;
	int ret = 0;

	caching_ctl = container_of(work, struct btrfs_caching_control, work);
	block_group = caching_ctl->block_group;
	fs_info = block_group->fs_info;
	extent_root = fs_info->extent_root;

	path = btrfs_alloc_path();
	if (!path)
		goto out;

	last = max_t(u64, block_group->key.objectid, BTRFS_SUPER_INFO_OFFSET);

	/*
	 * We don't want to deadlock with somebody trying to allocate a new
	 * extent for the extent root while also trying to search the extent
	 * root to add free space.  So we skip locking and search the commit
	 * root, since its read-only
	 */
	path->skip_locking = 1;
	path->search_commit_root = 1;
	path->reada = 1;

	key.objectid = last;
	key.offset = 0;
	key.type = BTRFS_EXTENT_ITEM_KEY;
again:
	mutex_lock(&caching_ctl->mutex);
	/* need to make sure the commit_root doesn't disappear */
	down_read(&fs_info->extent_commit_sem);

	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto err;

	leaf = path->nodes[0];
	nritems = btrfs_header_nritems(leaf);

	while (1) {
		if (btrfs_fs_closing(fs_info) > 1) {
			last = (u64)-1;
			break;
		}

		if (path->slots[0] < nritems) {
			btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		} else {
			ret = find_next_key(path, 0, &key);
			if (ret)
				break;

			if (need_resched() ||
			    btrfs_next_leaf(extent_root, path)) {
				caching_ctl->progress = last;
				btrfs_release_path(path);
				up_read(&fs_info->extent_commit_sem);
				mutex_unlock(&caching_ctl->mutex);
				cond_resched();
				goto again;
			}
			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
			continue;
		}

		if (key.objectid < block_group->key.objectid) {
			path->slots[0]++;
			continue;
		}

		if (key.objectid >= block_group->key.objectid +
		    block_group->key.offset)
			break;

		if (key.type == BTRFS_EXTENT_ITEM_KEY ||
		    key.type == BTRFS_METADATA_ITEM_KEY) {
			total_found += add_new_free_space(block_group,
							  fs_info, last,
							  key.objectid);
			if (key.type == BTRFS_METADATA_ITEM_KEY)
				last = key.objectid +
					fs_info->tree_root->leafsize;
			else
				last = key.objectid + key.offset;

			if (total_found > (1024 * 1024 * 2)) {
				total_found = 0;
				wake_up(&caching_ctl->wait);
			}
		}
		path->slots[0]++;
	}
	ret = 0;

	total_found += add_new_free_space(block_group, fs_info, last,
					  block_group->key.objectid +
					  block_group->key.offset);
	caching_ctl->progress = (u64)-1;

	spin_lock(&block_group->lock);
	block_group->caching_ctl = NULL;
	block_group->cached = BTRFS_CACHE_FINISHED;
	spin_unlock(&block_group->lock);

err:
	btrfs_free_path(path);
	up_read(&fs_info->extent_commit_sem);

	free_excluded_extents(extent_root, block_group);

	mutex_unlock(&caching_ctl->mutex);
out:
	wake_up(&caching_ctl->wait);

	put_caching_control(caching_ctl);
	btrfs_put_block_group(block_group);
}

static int cache_block_group(struct btrfs_block_group_cache *cache,
			     int load_cache_only)
{
	DEFINE_WAIT(wait);
	struct btrfs_fs_info *fs_info = cache->fs_info;
	struct btrfs_caching_control *caching_ctl;
	int ret = 0;

	caching_ctl = kzalloc(sizeof(*caching_ctl), GFP_NOFS);
	if (!caching_ctl)
		return -ENOMEM;

	INIT_LIST_HEAD(&caching_ctl->list);
	mutex_init(&caching_ctl->mutex);
	init_waitqueue_head(&caching_ctl->wait);
	caching_ctl->block_group = cache;
	caching_ctl->progress = cache->key.objectid;
	atomic_set(&caching_ctl->count, 1);
	caching_ctl->work.func = caching_thread;

	spin_lock(&cache->lock);
	/*
	 * This should be a rare occasion, but this could happen I think in the
	 * case where one thread starts to load the space cache info, and then
	 * some other thread starts a transaction commit which tries to do an
	 * allocation while the other thread is still loading the space cache
	 * info.  The previous loop should have kept us from choosing this block
	 * group, but if we've moved to the state where we will wait on caching
	 * block groups we need to first check if we're doing a fast load here,
	 * so we can wait for it to finish, otherwise we could end up allocating
	 * from a block group who's cache gets evicted for one reason or
	 * another.
	 */
	while (cache->cached == BTRFS_CACHE_FAST) {
		struct btrfs_caching_control *ctl;

		ctl = cache->caching_ctl;
		atomic_inc(&ctl->count);
		prepare_to_wait(&ctl->wait, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock(&cache->lock);

		schedule();

		finish_wait(&ctl->wait, &wait);
		put_caching_control(ctl);
		spin_lock(&cache->lock);
	}

	if (cache->cached != BTRFS_CACHE_NO) {
		spin_unlock(&cache->lock);
		kfree(caching_ctl);
		return 0;
	}
	WARN_ON(cache->caching_ctl);
	cache->caching_ctl = caching_ctl;
	cache->cached = BTRFS_CACHE_FAST;
	spin_unlock(&cache->lock);

	if (fs_info->mount_opt & BTRFS_MOUNT_SPACE_CACHE) {
		ret = load_free_space_cache(fs_info, cache);

		spin_lock(&cache->lock);
		if (ret == 1) {
			cache->caching_ctl = NULL;
			cache->cached = BTRFS_CACHE_FINISHED;
			cache->last_byte_to_unpin = (u64)-1;
		} else {
			if (load_cache_only) {
				cache->caching_ctl = NULL;
				cache->cached = BTRFS_CACHE_NO;
			} else {
				cache->cached = BTRFS_CACHE_STARTED;
			}
		}
		spin_unlock(&cache->lock);
		wake_up(&caching_ctl->wait);
		if (ret == 1) {
			put_caching_control(caching_ctl);
			free_excluded_extents(fs_info->extent_root, cache);
			return 0;
		}
	} else {
		/*
		 * We are not going to do the fast caching, set cached to the
		 * appropriate value and wakeup any waiters.
		 */
		spin_lock(&cache->lock);
		if (load_cache_only) {
			cache->caching_ctl = NULL;
			cache->cached = BTRFS_CACHE_NO;
		} else {
			cache->cached = BTRFS_CACHE_STARTED;
		}
		spin_unlock(&cache->lock);
		wake_up(&caching_ctl->wait);
	}

	if (load_cache_only) {
		put_caching_control(caching_ctl);
		return 0;
	}

	down_write(&fs_info->extent_commit_sem);
	atomic_inc(&caching_ctl->count);
	list_add_tail(&caching_ctl->list, &fs_info->caching_block_groups);
	up_write(&fs_info->extent_commit_sem);

	btrfs_get_block_group(cache);

	btrfs_queue_worker(&fs_info->caching_workers, &caching_ctl->work);

	return ret;
}

/*
 * return the block group that starts at or after bytenr
 */
static struct btrfs_block_group_cache *
btrfs_lookup_first_block_group(struct btrfs_fs_info *info, u64 bytenr)
{
	struct btrfs_block_group_cache *cache;

	cache = block_group_cache_tree_search(info, bytenr, 0);

	return cache;
}

/*
 * return the block group that contains the given bytenr
 */
struct btrfs_block_group_cache *btrfs_lookup_block_group(
						 struct btrfs_fs_info *info,
						 u64 bytenr)
{
	struct btrfs_block_group_cache *cache;

	cache = block_group_cache_tree_search(info, bytenr, 1);

	return cache;
}

static struct btrfs_space_info *__find_space_info(struct btrfs_fs_info *info,
						  u64 flags)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	flags &= BTRFS_BLOCK_GROUP_TYPE_MASK;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list) {
		if (found->flags & flags) {
			rcu_read_unlock();
			return found;
		}
	}
	rcu_read_unlock();
	return NULL;
}

/*
 * after adding space to the filesystem, we need to clear the full flags
 * on all the space infos.
 */
void btrfs_clear_space_info_full(struct btrfs_fs_info *info)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list)
		found->full = 0;
	rcu_read_unlock();
}

u64 btrfs_find_block_group(struct btrfs_root *root,
			   u64 search_start, u64 search_hint, int owner)
{
	struct btrfs_block_group_cache *cache;
	u64 used;
	u64 last = max(search_hint, search_start);
	u64 group_start = 0;
	int full_search = 0;
	int factor = 9;
	int wrapped = 0;
again:
	while (1) {
		cache = btrfs_lookup_first_block_group(root->fs_info, last);
		if (!cache)
			break;

		spin_lock(&cache->lock);
		last = cache->key.objectid + cache->key.offset;
		used = btrfs_block_group_used(&cache->item);

		if ((full_search || !cache->ro) &&
		    block_group_bits(cache, BTRFS_BLOCK_GROUP_METADATA)) {
			if (used + cache->pinned + cache->reserved <
			    div_factor(cache->key.offset, factor)) {
				group_start = cache->key.objectid;
				spin_unlock(&cache->lock);
				btrfs_put_block_group(cache);
				goto found;
			}
		}
		spin_unlock(&cache->lock);
		btrfs_put_block_group(cache);
		cond_resched();
	}
	if (!wrapped) {
		last = search_start;
		wrapped = 1;
		goto again;
	}
	if (!full_search && factor < 10) {
		last = search_start;
		full_search = 1;
		factor = 10;
		goto again;
	}
found:
	return group_start;
}

/* simple helper to search for an existing extent at a given offset */
int btrfs_lookup_extent(struct btrfs_root *root, u64 start, u64 len)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = start;
	key.offset = len;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	ret = btrfs_search_slot(NULL, root->fs_info->extent_root, &key, path,
				0, 0);
	if (ret > 0) {
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid == start &&
		    key.type == BTRFS_METADATA_ITEM_KEY)
			ret = 0;
	}
	btrfs_free_path(path);
	return ret;
}

/*
 * helper function to lookup reference count and flags of a tree block.
 *
 * the head node for delayed ref is used to store the sum of all the
 * reference count modifications queued up in the rbtree. the head
 * node may also store the extent flags to set. This way you can check
 * to see what the reference count and extent flags would be if all of
 * the delayed refs are not processed.
 */
int btrfs_lookup_extent_info(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 bytenr,
			     u64 offset, int metadata, u64 *refs, u64 *flags)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_path *path;
	struct btrfs_extent_item *ei;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u32 item_size;
	u64 num_refs;
	u64 extent_flags;
	int ret;

	/*
	 * If we don't have skinny metadata, don't bother doing anything
	 * different
	 */
	if (metadata && !btrfs_fs_incompat(root->fs_info, SKINNY_METADATA)) {
		offset = root->leafsize;
		metadata = 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (metadata) {
		key.objectid = bytenr;
		key.type = BTRFS_METADATA_ITEM_KEY;
		key.offset = offset;
	} else {
		key.objectid = bytenr;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = offset;
	}

	if (!trans) {
		path->skip_locking = 1;
		path->search_commit_root = 1;
	}
again:
	ret = btrfs_search_slot(trans, root->fs_info->extent_root,
				&key, path, 0, 0);
	if (ret < 0)
		goto out_free;

	if (ret > 0 && metadata && key.type == BTRFS_METADATA_ITEM_KEY) {
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = root->leafsize;
		btrfs_release_path(path);
		goto again;
	}

	if (ret == 0) {
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		if (item_size >= sizeof(*ei)) {
			ei = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_extent_item);
			num_refs = btrfs_extent_refs(leaf, ei);
			extent_flags = btrfs_extent_flags(leaf, ei);
		} else {
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
			struct btrfs_extent_item_v0 *ei0;
			BUG_ON(item_size != sizeof(*ei0));
			ei0 = btrfs_item_ptr(leaf, path->slots[0],
					     struct btrfs_extent_item_v0);
			num_refs = btrfs_extent_refs_v0(leaf, ei0);
			/* FIXME: this isn't correct for data */
			extent_flags = BTRFS_BLOCK_FLAG_FULL_BACKREF;
#else
			BUG();
#endif
		}
		BUG_ON(num_refs == 0);
	} else {
		num_refs = 0;
		extent_flags = 0;
		ret = 0;
	}

	if (!trans)
		goto out;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(trans, bytenr);
	if (head) {
		if (!mutex_trylock(&head->mutex)) {
			atomic_inc(&head->node.refs);
			spin_unlock(&delayed_refs->lock);

			btrfs_release_path(path);

			/*
			 * Mutex was contended, block until it's released and try
			 * again
			 */
			mutex_lock(&head->mutex);
			mutex_unlock(&head->mutex);
			btrfs_put_delayed_ref(&head->node);
			goto again;
		}
		if (head->extent_op && head->extent_op->update_flags)
			extent_flags |= head->extent_op->flags_to_set;
		else
			BUG_ON(num_refs == 0);

		num_refs += head->node.ref_mod;
		mutex_unlock(&head->mutex);
	}
	spin_unlock(&delayed_refs->lock);
out:
	WARN_ON(num_refs == 0);
	if (refs)
		*refs = num_refs;
	if (flags)
		*flags = extent_flags;
out_free:
	btrfs_free_path(path);
	return ret;
}

/*
 * Back reference rules.  Back refs have three main goals:
 *
 * 1) differentiate between all holders of references to an extent so that
 *    when a reference is dropped we can make sure it was a valid reference
 *    before freeing the extent.
 *
 * 2) Provide enough information to quickly find the holders of an extent
 *    if we notice a given block is corrupted or bad.
 *
 * 3) Make it easy to migrate blocks for FS shrinking or storage pool
 *    maintenance.  This is actually the same as #2, but with a slightly
 *    different use case.
 *
 * There are two kinds of back refs. The implicit back refs is optimized
 * for pointers in non-shared tree blocks. For a given pointer in a block,
 * back refs of this kind provide information about the block's owner tree
 * and the pointer's key. These information allow us to find the block by
 * b-tree searching. The full back refs is for pointers in tree blocks not
 * referenced by their owner trees. The location of tree block is recorded
 * in the back refs. Actually the full back refs is generic, and can be
 * used in all cases the implicit back refs is used. The major shortcoming
 * of the full back refs is its overhead. Every time a tree block gets
 * COWed, we have to update back refs entry for all pointers in it.
 *
 * For a newly allocated tree block, we use implicit back refs for
 * pointers in it. This means most tree related operations only involve
 * implicit back refs. For a tree block created in old transaction, the
 * only way to drop a reference to it is COW it. So we can detect the
 * event that tree block loses its owner tree's reference and do the
 * back refs conversion.
 *
 * When a tree block is COW'd through a tree, there are four cases:
 *
 * The reference count of the block is one and the tree is the block's
 * owner tree. Nothing to do in this case.
 *
 * The reference count of the block is one and the tree is not the
 * block's owner tree. In this case, full back refs is used for pointers
 * in the block. Remove these full back refs, add implicit back refs for
 * every pointers in the new block.
 *
 * The reference count of the block is greater than one and the tree is
 * the block's owner tree. In this case, implicit back refs is used for
 * pointers in the block. Add full back refs for every pointers in the
 * block, increase lower level extents' reference counts. The original
 * implicit back refs are entailed to the new block.
 *
 * The reference count of the block is greater than one and the tree is
 * not the block's owner tree. Add implicit back refs for every pointer in
 * the new block, increase lower level extents' reference count.
 *
 * Back Reference Key composing:
 *
 * The key objectid corresponds to the first byte in the extent,
 * The key type is used to differentiate between types of back refs.
 * There are different meanings of the key offset for different types
 * of back refs.
 *
 * File extents can be referenced by:
 *
 * - multiple snapshots, subvolumes, or different generations in one subvol
 * - different files inside a single subvolume
 * - different offsets inside a file (bookend extents in file.c)
 *
 * The extent ref structure for the implicit back refs has fields for:
 *
 * - Objectid of the subvolume root
 * - objectid of the file holding the reference
 * - original offset in the file
 * - how many bookend extents
 *
 * The key offset for the implicit back refs is hash of the first
 * three fields.
 *
 * The extent ref structure for the full back refs has field for:
 *
 * - number of pointers in the tree leaf
 *
 * The key offset for the implicit back refs is the first byte of
 * the tree leaf
 *
 * When a file extent is allocated, The implicit back refs is used.
 * the fields are filled in:
 *
 *     (root_key.objectid, inode objectid, offset in file, 1)
 *
 * When a file extent is removed file truncation, we find the
 * corresponding implicit back refs and check the following fields:
 *
 *     (btrfs_header_owner(leaf), inode objectid, offset in file)
 *
 * Btree extents can be referenced by:
 *
 * - Different subvolumes
 *
 * Both the implicit back refs and the full back refs for tree blocks
 * only consist of key. The key offset for the implicit back refs is
 * objectid of block's owner tree. The key offset for the full back refs
 * is the first byte of parent block.
 *
 * When implicit back refs is used, information about the lowest key and
 * level of the tree block are required. These information are stored in
 * tree block info structure.
 */

#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
static int convert_extent_item_v0(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_path *path,
				  u64 owner, u32 extra_size)
{
	struct btrfs_extent_item *item;
	struct btrfs_extent_item_v0 *ei0;
	struct btrfs_extent_ref_v0 *ref0;
	struct btrfs_tree_block_info *bi;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u32 new_size = sizeof(*item);
	u64 refs;
	int ret;

	leaf = path->nodes[0];
	BUG_ON(btrfs_item_size_nr(leaf, path->slots[0]) != sizeof(*ei0));

	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	ei0 = btrfs_item_ptr(leaf, path->slots[0],
			     struct btrfs_extent_item_v0);
	refs = btrfs_extent_refs_v0(leaf, ei0);

	if (owner == (u64)-1) {
		while (1) {
			if (path->slots[0] >= btrfs_header_nritems(leaf)) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					return ret;
				BUG_ON(ret > 0); /* Corruption */
				leaf = path->nodes[0];
			}
			btrfs_item_key_to_cpu(leaf, &found_key,
					      path->slots[0]);
			BUG_ON(key.objectid != found_key.objectid);
			if (found_key.type != BTRFS_EXTENT_REF_V0_KEY) {
				path->slots[0]++;
				continue;
			}
			ref0 = btrfs_item_ptr(leaf, path->slots[0],
					      struct btrfs_extent_ref_v0);
			owner = btrfs_ref_objectid_v0(leaf, ref0);
			break;
		}
	}
	btrfs_release_path(path);

	if (owner < BTRFS_FIRST_FREE_OBJECTID)
		new_size += sizeof(*bi);

	new_size -= sizeof(*ei0);
	ret = btrfs_search_slot(trans, root, &key, path,
				new_size + extra_size, 1);
	if (ret < 0)
		return ret;
	BUG_ON(ret); /* Corruption */

	btrfs_extend_item(trans, root, path, new_size);

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, item, refs);
	/* FIXME: get real generation */
	btrfs_set_extent_generation(leaf, item, 0);
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		btrfs_set_extent_flags(leaf, item,
				       BTRFS_EXTENT_FLAG_TREE_BLOCK |
				       BTRFS_BLOCK_FLAG_FULL_BACKREF);
		bi = (struct btrfs_tree_block_info *)(item + 1);
		/* FIXME: get first key of the block */
		memset_extent_buffer(leaf, 0, (unsigned long)bi, sizeof(*bi));
		btrfs_set_tree_block_level(leaf, bi, (int)owner);
	} else {
		btrfs_set_extent_flags(leaf, item, BTRFS_EXTENT_FLAG_DATA);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}
#endif

static u64 hash_extent_data_ref(u64 root_objectid, u64 owner, u64 offset)
{
	u32 high_crc = ~(u32)0;
	u32 low_crc = ~(u32)0;
	__le64 lenum;

	lenum = cpu_to_le64(root_objectid);
	high_crc = crc32c(high_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(owner);
	low_crc = crc32c(low_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(offset);
	low_crc = crc32c(low_crc, &lenum, sizeof(lenum));

	return ((u64)high_crc << 31) ^ (u64)low_crc;
}

static u64 hash_extent_data_ref_item(struct extent_buffer *leaf,
				     struct btrfs_extent_data_ref *ref)
{
	return hash_extent_data_ref(btrfs_extent_data_ref_root(leaf, ref),
				    btrfs_extent_data_ref_objectid(leaf, ref),
				    btrfs_extent_data_ref_offset(leaf, ref));
}

static int match_extent_data_ref(struct extent_buffer *leaf,
				 struct btrfs_extent_data_ref *ref,
				 u64 root_objectid, u64 owner, u64 offset)
{
	if (btrfs_extent_data_ref_root(leaf, ref) != root_objectid ||
	    btrfs_extent_data_ref_objectid(leaf, ref) != owner ||
	    btrfs_extent_data_ref_offset(leaf, ref) != offset)
		return 0;
	return 1;
}

static noinline int lookup_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid,
					   u64 owner, u64 offset)
{
	struct btrfs_key key;
	struct btrfs_extent_data_ref *ref;
	struct extent_buffer *leaf;
	u32 nritems;
	int ret;
	int recow;
	int err = -ENOENT;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_DATA_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_EXTENT_DATA_REF_KEY;
		key.offset = hash_extent_data_ref(root_objectid,
						  owner, offset);
	}
again:
	recow = 0;
	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0) {
		err = ret;
		goto fail;
	}

	if (parent) {
		if (!ret)
			return 0;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		key.type = BTRFS_EXTENT_REF_V0_KEY;
		btrfs_release_path(path);
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret < 0) {
			err = ret;
			goto fail;
		}
		if (!ret)
			return 0;
#endif
		goto fail;
	}

	leaf = path->nodes[0];
	nritems = btrfs_header_nritems(leaf);
	while (1) {
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				err = ret;
			if (ret)
				goto fail;

			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
			recow = 1;
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		if (key.objectid != bytenr ||
		    key.type != BTRFS_EXTENT_DATA_REF_KEY)
			goto fail;

		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_data_ref);

		if (match_extent_data_ref(leaf, ref, root_objectid,
					  owner, offset)) {
			if (recow) {
				btrfs_release_path(path);
				goto again;
			}
			err = 0;
			break;
		}
		path->slots[0]++;
	}
fail:
	return err;
}

static noinline int insert_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid, u64 owner,
					   u64 offset, int refs_to_add)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u32 size;
	u32 num_refs;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_DATA_REF_KEY;
		key.offset = parent;
		size = sizeof(struct btrfs_shared_data_ref);
	} else {
		key.type = BTRFS_EXTENT_DATA_REF_KEY;
		key.offset = hash_extent_data_ref(root_objectid,
						  owner, offset);
		size = sizeof(struct btrfs_extent_data_ref);
	}

	ret = btrfs_insert_empty_item(trans, root, path, &key, size);
	if (ret && ret != -EEXIST)
		goto fail;

	leaf = path->nodes[0];
	if (parent) {
		struct btrfs_shared_data_ref *ref;
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_shared_data_ref);
		if (ret == 0) {
			btrfs_set_shared_data_ref_count(leaf, ref, refs_to_add);
		} else {
			num_refs = btrfs_shared_data_ref_count(leaf, ref);
			num_refs += refs_to_add;
			btrfs_set_shared_data_ref_count(leaf, ref, num_refs);
		}
	} else {
		struct btrfs_extent_data_ref *ref;
		while (ret == -EEXIST) {
			ref = btrfs_item_ptr(leaf, path->slots[0],
					     struct btrfs_extent_data_ref);
			if (match_extent_data_ref(leaf, ref, root_objectid,
						  owner, offset))
				break;
			btrfs_release_path(path);
			key.offset++;
			ret = btrfs_insert_empty_item(trans, root, path, &key,
						      size);
			if (ret && ret != -EEXIST)
				goto fail;

			leaf = path->nodes[0];
		}
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_data_ref);
		if (ret == 0) {
			btrfs_set_extent_data_ref_root(leaf, ref,
						       root_objectid);
			btrfs_set_extent_data_ref_objectid(leaf, ref, owner);
			btrfs_set_extent_data_ref_offset(leaf, ref, offset);
			btrfs_set_extent_data_ref_count(leaf, ref, refs_to_add);
		} else {
			num_refs = btrfs_extent_data_ref_count(leaf, ref);
			num_refs += refs_to_add;
			btrfs_set_extent_data_ref_count(leaf, ref, num_refs);
		}
	}
	btrfs_mark_buffer_dirty(leaf);
	ret = 0;
fail:
	btrfs_release_path(path);
	return ret;
}

static noinline int remove_extent_data_ref(struct btrfs_trans_handle *trans,
					   struct btrfs_root *root,
					   struct btrfs_path *path,
					   int refs_to_drop)
{
	struct btrfs_key key;
	struct btrfs_extent_data_ref *ref1 = NULL;
	struct btrfs_shared_data_ref *ref2 = NULL;
	struct extent_buffer *leaf;
	u32 num_refs = 0;
	int ret = 0;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
		ref1 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_data_ref);
		num_refs = btrfs_extent_data_ref_count(leaf, ref1);
	} else if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
		ref2 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_shared_data_ref);
		num_refs = btrfs_shared_data_ref_count(leaf, ref2);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	} else if (key.type == BTRFS_EXTENT_REF_V0_KEY) {
		struct btrfs_extent_ref_v0 *ref0;
		ref0 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_ref_v0);
		num_refs = btrfs_ref_count_v0(leaf, ref0);
#endif
	} else {
		BUG();
	}

	BUG_ON(num_refs < refs_to_drop);
	num_refs -= refs_to_drop;

	if (num_refs == 0) {
		ret = btrfs_del_item(trans, root, path);
	} else {
		if (key.type == BTRFS_EXTENT_DATA_REF_KEY)
			btrfs_set_extent_data_ref_count(leaf, ref1, num_refs);
		else if (key.type == BTRFS_SHARED_DATA_REF_KEY)
			btrfs_set_shared_data_ref_count(leaf, ref2, num_refs);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		else {
			struct btrfs_extent_ref_v0 *ref0;
			ref0 = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_extent_ref_v0);
			btrfs_set_ref_count_v0(leaf, ref0, num_refs);
		}
#endif
		btrfs_mark_buffer_dirty(leaf);
	}
	return ret;
}

static noinline u32 extent_data_ref_count(struct btrfs_root *root,
					  struct btrfs_path *path,
					  struct btrfs_extent_inline_ref *iref)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_data_ref *ref1;
	struct btrfs_shared_data_ref *ref2;
	u32 num_refs = 0;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (iref) {
		if (btrfs_extent_inline_ref_type(leaf, iref) ==
		    BTRFS_EXTENT_DATA_REF_KEY) {
			ref1 = (struct btrfs_extent_data_ref *)(&iref->offset);
			num_refs = btrfs_extent_data_ref_count(leaf, ref1);
		} else {
			ref2 = (struct btrfs_shared_data_ref *)(iref + 1);
			num_refs = btrfs_shared_data_ref_count(leaf, ref2);
		}
	} else if (key.type == BTRFS_EXTENT_DATA_REF_KEY) {
		ref1 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_data_ref);
		num_refs = btrfs_extent_data_ref_count(leaf, ref1);
	} else if (key.type == BTRFS_SHARED_DATA_REF_KEY) {
		ref2 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_shared_data_ref);
		num_refs = btrfs_shared_data_ref_count(leaf, ref2);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	} else if (key.type == BTRFS_EXTENT_REF_V0_KEY) {
		struct btrfs_extent_ref_v0 *ref0;
		ref0 = btrfs_item_ptr(leaf, path->slots[0],
				      struct btrfs_extent_ref_v0);
		num_refs = btrfs_ref_count_v0(leaf, ref0);
#endif
	} else {
		WARN_ON(1);
	}
	return num_refs;
}

static noinline int lookup_tree_block_ref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 root_objectid)
{
	struct btrfs_key key;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0)
		ret = -ENOENT;
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (ret == -ENOENT && parent) {
		btrfs_release_path(path);
		key.type = BTRFS_EXTENT_REF_V0_KEY;
		ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
		if (ret > 0)
			ret = -ENOENT;
	}
#endif
	return ret;
}

static noinline int insert_tree_block_ref(struct btrfs_trans_handle *trans,
					  struct btrfs_root *root,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 root_objectid)
{
	struct btrfs_key key;
	int ret;

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_insert_empty_item(trans, root, path, &key, 0);
	btrfs_release_path(path);
	return ret;
}

static inline int extent_ref_type(u64 parent, u64 owner)
{
	int type;
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		if (parent > 0)
			type = BTRFS_SHARED_BLOCK_REF_KEY;
		else
			type = BTRFS_TREE_BLOCK_REF_KEY;
	} else {
		if (parent > 0)
			type = BTRFS_SHARED_DATA_REF_KEY;
		else
			type = BTRFS_EXTENT_DATA_REF_KEY;
	}
	return type;
}

static int find_next_key(struct btrfs_path *path, int level,
			 struct btrfs_key *key)

{
	for (; level < BTRFS_MAX_LEVEL; level++) {
		if (!path->nodes[level])
			break;
		if (path->slots[level] + 1 >=
		    btrfs_header_nritems(path->nodes[level]))
			continue;
		if (level == 0)
			btrfs_item_key_to_cpu(path->nodes[level], key,
					      path->slots[level] + 1);
		else
			btrfs_node_key_to_cpu(path->nodes[level], key,
					      path->slots[level] + 1);
		return 0;
	}
	return 1;
}

/*
 * look for inline back ref. if back ref is found, *ref_ret is set
 * to the address of inline back ref, and 0 is returned.
 *
 * if back ref isn't found, *ref_ret is set to the address where it
 * should be inserted, and -ENOENT is returned.
 *
 * if insert is true and there are too many inline back refs, the path
 * points to the extent item, and -EAGAIN is returned.
 *
 * NOTE: inline back refs are ordered in the same way that back ref
 *	 items in the tree are ordered.
 */
static noinline_for_stack
int lookup_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes,
				 u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int insert)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	u64 flags;
	u64 item_size;
	unsigned long ptr;
	unsigned long end;
	int extra_size;
	int type;
	int want;
	int ret;
	int err = 0;
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	want = extent_ref_type(parent, owner);
	if (insert) {
		extra_size = btrfs_extent_inline_ref_size(want);
		path->keep_locks = 1;
	} else
		extra_size = -1;

	/*
	 * Owner is our parent level, so we can just add one to get the level
	 * for the block we are interested in.
	 */
	if (skinny_metadata && owner < BTRFS_FIRST_FREE_OBJECTID) {
		key.type = BTRFS_METADATA_ITEM_KEY;
		key.offset = owner;
	}

again:
	ret = btrfs_search_slot(trans, root, &key, path, extra_size, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}

	/*
	 * We may be a newly converted file system which still has the old fat
	 * extent entries for metadata, so try and see if we have one of those.
	 */
	if (ret > 0 && skinny_metadata) {
		skinny_metadata = false;
		if (path->slots[0]) {
			path->slots[0]--;
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == bytenr &&
			    key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == num_bytes)
				ret = 0;
		}
		if (ret) {
			key.type = BTRFS_EXTENT_ITEM_KEY;
			key.offset = num_bytes;
			btrfs_release_path(path);
			goto again;
		}
	}

	if (ret && !insert) {
		err = -ENOENT;
		goto out;
	} else if (ret) {
		err = -EIO;
		WARN_ON(1);
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		if (!insert) {
			err = -ENOENT;
			goto out;
		}
		ret = convert_extent_item_v0(trans, root, path, owner,
					     extra_size);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	flags = btrfs_extent_flags(leaf, ei);

	ptr = (unsigned long)(ei + 1);
	end = (unsigned long)ei + item_size;

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK && !skinny_metadata) {
		ptr += sizeof(struct btrfs_tree_block_info);
		BUG_ON(ptr > end);
	}

	err = -ENOENT;
	while (1) {
		if (ptr >= end) {
			WARN_ON(ptr > end);
			break;
		}
		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_extent_inline_ref_type(leaf, iref);
		if (want < type)
			break;
		if (want > type) {
			ptr += btrfs_extent_inline_ref_size(type);
			continue;
		}

		if (type == BTRFS_EXTENT_DATA_REF_KEY) {
			struct btrfs_extent_data_ref *dref;
			dref = (struct btrfs_extent_data_ref *)(&iref->offset);
			if (match_extent_data_ref(leaf, dref, root_objectid,
						  owner, offset)) {
				err = 0;
				break;
			}
			if (hash_extent_data_ref_item(leaf, dref) <
			    hash_extent_data_ref(root_objectid, owner, offset))
				break;
		} else {
			u64 ref_offset;
			ref_offset = btrfs_extent_inline_ref_offset(leaf, iref);
			if (parent > 0) {
				if (parent == ref_offset) {
					err = 0;
					break;
				}
				if (ref_offset < parent)
					break;
			} else {
				if (root_objectid == ref_offset) {
					err = 0;
					break;
				}
				if (ref_offset < root_objectid)
					break;
			}
		}
		ptr += btrfs_extent_inline_ref_size(type);
	}
	if (err == -ENOENT && insert) {
		if (item_size + extra_size >=
		    BTRFS_MAX_EXTENT_ITEM_SIZE(root)) {
			err = -EAGAIN;
			goto out;
		}
		/*
		 * To add new inline back ref, we have to make sure
		 * there is no corresponding back ref item.
		 * For simplicity, we just do not add new inline back
		 * ref if there is any kind of item for this block
		 */
		if (find_next_key(path, 0, &key) == 0 &&
		    key.objectid == bytenr &&
		    key.type < BTRFS_BLOCK_GROUP_ITEM_KEY) {
			err = -EAGAIN;
			goto out;
		}
	}
	*ref_ret = (struct btrfs_extent_inline_ref *)ptr;
out:
	if (insert) {
		path->keep_locks = 0;
		btrfs_unlock_up_safe(path, 1);
	}
	return err;
}

/*
 * helper to add new inline back ref
 */
static noinline_for_stack
void setup_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref *iref,
				 u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int refs_to_add,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	unsigned long ptr;
	unsigned long end;
	unsigned long item_offset;
	u64 refs;
	int size;
	int type;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	item_offset = (unsigned long)iref - (unsigned long)ei;

	type = extent_ref_type(parent, owner);
	size = btrfs_extent_inline_ref_size(type);

	btrfs_extend_item(trans, root, path, size);

	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	refs += refs_to_add;
	btrfs_set_extent_refs(leaf, ei, refs);
	if (extent_op)
		__run_delayed_extent_op(extent_op, leaf, ei);

	ptr = (unsigned long)ei + item_offset;
	end = (unsigned long)ei + btrfs_item_size_nr(leaf, path->slots[0]);
	if (ptr < end - size)
		memmove_extent_buffer(leaf, ptr + size, ptr,
				      end - size - ptr);

	iref = (struct btrfs_extent_inline_ref *)ptr;
	btrfs_set_extent_inline_ref_type(leaf, iref, type);
	if (type == BTRFS_EXTENT_DATA_REF_KEY) {
		struct btrfs_extent_data_ref *dref;
		dref = (struct btrfs_extent_data_ref *)(&iref->offset);
		btrfs_set_extent_data_ref_root(leaf, dref, root_objectid);
		btrfs_set_extent_data_ref_objectid(leaf, dref, owner);
		btrfs_set_extent_data_ref_offset(leaf, dref, offset);
		btrfs_set_extent_data_ref_count(leaf, dref, refs_to_add);
	} else if (type == BTRFS_SHARED_DATA_REF_KEY) {
		struct btrfs_shared_data_ref *sref;
		sref = (struct btrfs_shared_data_ref *)(iref + 1);
		btrfs_set_shared_data_ref_count(leaf, sref, refs_to_add);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else if (type == BTRFS_SHARED_BLOCK_REF_KEY) {
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else {
		btrfs_set_extent_inline_ref_offset(leaf, iref, root_objectid);
	}
	btrfs_mark_buffer_dirty(leaf);
}

static int lookup_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner, u64 offset)
{
	int ret;

	ret = lookup_inline_extent_backref(trans, root, path, ref_ret,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 0);
	if (ret != -ENOENT)
		return ret;

	btrfs_release_path(path);
	*ref_ret = NULL;

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = lookup_tree_block_ref(trans, root, path, bytenr, parent,
					    root_objectid);
	} else {
		ret = lookup_extent_data_ref(trans, root, path, bytenr, parent,
					     root_objectid, owner, offset);
	}
	return ret;
}

/*
 * helper to update/remove inline back ref
 */
static noinline_for_stack
void update_inline_extent_backref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_path *path,
				  struct btrfs_extent_inline_ref *iref,
				  int refs_to_mod,
				  struct btrfs_delayed_extent_op *extent_op)
{
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_data_ref *dref = NULL;
	struct btrfs_shared_data_ref *sref = NULL;
	unsigned long ptr;
	unsigned long end;
	u32 item_size;
	int size;
	int type;
	u64 refs;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, ei);
	WARN_ON(refs_to_mod < 0 && refs + refs_to_mod <= 0);
	refs += refs_to_mod;
	btrfs_set_extent_refs(leaf, ei, refs);
	if (extent_op)
		__run_delayed_extent_op(extent_op, leaf, ei);

	type = btrfs_extent_inline_ref_type(leaf, iref);

	if (type == BTRFS_EXTENT_DATA_REF_KEY) {
		dref = (struct btrfs_extent_data_ref *)(&iref->offset);
		refs = btrfs_extent_data_ref_count(leaf, dref);
	} else if (type == BTRFS_SHARED_DATA_REF_KEY) {
		sref = (struct btrfs_shared_data_ref *)(iref + 1);
		refs = btrfs_shared_data_ref_count(leaf, sref);
	} else {
		refs = 1;
		BUG_ON(refs_to_mod != -1);
	}

	BUG_ON(refs_to_mod < 0 && refs < -refs_to_mod);
	refs += refs_to_mod;

	if (refs > 0) {
		if (type == BTRFS_EXTENT_DATA_REF_KEY)
			btrfs_set_extent_data_ref_count(leaf, dref, refs);
		else
			btrfs_set_shared_data_ref_count(leaf, sref, refs);
	} else {
		size =  btrfs_extent_inline_ref_size(type);
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		ptr = (unsigned long)iref;
		end = (unsigned long)ei + item_size;
		if (ptr + size < end)
			memmove_extent_buffer(leaf, ptr, ptr + size,
					      end - ptr - size);
		item_size -= size;
		btrfs_truncate_item(trans, root, path, item_size, 1);
	}
	btrfs_mark_buffer_dirty(leaf);
}

static noinline_for_stack
int insert_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner,
				 u64 offset, int refs_to_add,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_extent_inline_ref *iref;
	int ret;

	ret = lookup_inline_extent_backref(trans, root, path, &iref,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 1);
	if (ret == 0) {
		BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID);
		update_inline_extent_backref(trans, root, path, iref,
					     refs_to_add, extent_op);
	} else if (ret == -ENOENT) {
		setup_inline_extent_backref(trans, root, path, iref, parent,
					    root_objectid, owner, offset,
					    refs_to_add, extent_op);
		ret = 0;
	}
	return ret;
}

static int insert_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 u64 bytenr, u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int refs_to_add)
{
	int ret;
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		BUG_ON(refs_to_add != 1);
		ret = insert_tree_block_ref(trans, root, path, bytenr,
					    parent, root_objectid);
	} else {
		ret = insert_extent_data_ref(trans, root, path, bytenr,
					     parent, root_objectid,
					     owner, offset, refs_to_add);
	}
	return ret;
}

static int remove_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref *iref,
				 int refs_to_drop, int is_data)
{
	int ret = 0;

	BUG_ON(!is_data && refs_to_drop != 1);
	if (iref) {
		update_inline_extent_backref(trans, root, path, iref,
					     -refs_to_drop, NULL);
	} else if (is_data) {
		ret = remove_extent_data_ref(trans, root, path, refs_to_drop);
	} else {
		ret = btrfs_del_item(trans, root, path);
	}
	return ret;
}

static int btrfs_issue_discard(struct block_device *bdev,
				u64 start, u64 len)
{
	return blkdev_issue_discard(bdev, start >> 9, len >> 9, GFP_NOFS, 0);
}

static int btrfs_discard_extent(struct btrfs_root *root, u64 bytenr,
				u64 num_bytes, u64 *actual_bytes)
{
	int ret;
	u64 discarded_bytes = 0;
	struct btrfs_bio *bbio = NULL;


	/* Tell the block device(s) that the sectors can be discarded */
	ret = btrfs_map_block(root->fs_info, REQ_DISCARD,
			      bytenr, &num_bytes, &bbio, 0);
	/* Error condition is -ENOMEM */
	if (!ret) {
		struct btrfs_bio_stripe *stripe = bbio->stripes;
		int i;


		for (i = 0; i < bbio->num_stripes; i++, stripe++) {
			if (!stripe->dev->can_discard)
				continue;

			ret = btrfs_issue_discard(stripe->dev->bdev,
						  stripe->physical,
						  stripe->length);
			if (!ret)
				discarded_bytes += stripe->length;
			else if (ret != -EOPNOTSUPP)
				break; /* Logic errors or -ENOMEM, or -EIO but I don't know how that could happen JDM */

			/*
			 * Just in case we get back EOPNOTSUPP for some reason,
			 * just ignore the return value so we don't screw up
			 * people calling discard_extent.
			 */
			ret = 0;
		}
		kfree(bbio);
	}

	if (actual_bytes)
		*actual_bytes = discarded_bytes;


	if (ret == -EOPNOTSUPP)
		ret = 0;
	return ret;
}

/* Can return -ENOMEM */
int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 root_objectid, u64 owner, u64 offset, int for_cow)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;

	BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID &&
	       root_objectid == BTRFS_TREE_LOG_OBJECTID);

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(fs_info, trans, bytenr,
					num_bytes,
					parent, root_objectid, (int)owner,
					BTRFS_ADD_DELAYED_REF, NULL, for_cow);
	} else {
		ret = btrfs_add_delayed_data_ref(fs_info, trans, bytenr,
					num_bytes,
					parent, root_objectid, owner, offset,
					BTRFS_ADD_DELAYED_REF, NULL, for_cow);
	}
	return ret;
}

static int __btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  u64 bytenr, u64 num_bytes,
				  u64 parent, u64 root_objectid,
				  u64 owner, u64 offset, int refs_to_add,
				  struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *item;
	u64 refs;
	int ret;
	int err = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;
	path->leave_spinning = 1;
	/* this will setup the path even if it fails to insert the back ref */
	ret = insert_inline_extent_backref(trans, root->fs_info->extent_root,
					   path, bytenr, num_bytes, parent,
					   root_objectid, owner, offset,
					   refs_to_add, extent_op);
	if (ret == 0)
		goto out;

	if (ret != -EAGAIN) {
		err = ret;
		goto out;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, item);
	btrfs_set_extent_refs(leaf, item, refs + refs_to_add);
	if (extent_op)
		__run_delayed_extent_op(extent_op, leaf, item);

	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	path->reada = 1;
	path->leave_spinning = 1;

	/* now insert the actual backref */
	ret = insert_extent_backref(trans, root->fs_info->extent_root,
				    path, bytenr, parent, root_objectid,
				    owner, offset, refs_to_add);
	if (ret)
		btrfs_abort_transaction(trans, root, ret);
out:
	btrfs_free_path(path);
	return err;
}

static int run_delayed_data_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_delayed_ref_node *node,
				struct btrfs_delayed_extent_op *extent_op,
				int insert_reserved)
{
	int ret = 0;
	struct btrfs_delayed_data_ref *ref;
	struct btrfs_key ins;
	u64 parent = 0;
	u64 ref_root = 0;
	u64 flags = 0;

	ins.objectid = node->bytenr;
	ins.offset = node->num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	ref = btrfs_delayed_node_to_data_ref(node);
	if (node->type == BTRFS_SHARED_DATA_REF_KEY)
		parent = ref->parent;
	else
		ref_root = ref->root;

	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		if (extent_op)
			flags |= extent_op->flags_to_set;
		ret = alloc_reserved_file_extent(trans, root,
						 parent, ref_root, flags,
						 ref->objectid, ref->offset,
						 &ins, node->ref_mod);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, root, node->bytenr,
					     node->num_bytes, parent,
					     ref_root, ref->objectid,
					     ref->offset, node->ref_mod,
					     extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, root, node->bytenr,
					  node->num_bytes, parent,
					  ref_root, ref->objectid,
					  ref->offset, node->ref_mod,
					  extent_op);
	} else {
		BUG();
	}
	return ret;
}

static void __run_delayed_extent_op(struct btrfs_delayed_extent_op *extent_op,
				    struct extent_buffer *leaf,
				    struct btrfs_extent_item *ei)
{
	u64 flags = btrfs_extent_flags(leaf, ei);
	if (extent_op->update_flags) {
		flags |= extent_op->flags_to_set;
		btrfs_set_extent_flags(leaf, ei, flags);
	}

	if (extent_op->update_key) {
		struct btrfs_tree_block_info *bi;
		BUG_ON(!(flags & BTRFS_EXTENT_FLAG_TREE_BLOCK));
		bi = (struct btrfs_tree_block_info *)(ei + 1);
		btrfs_set_tree_block_key(leaf, bi, &extent_op->key);
	}
}

static int run_delayed_extent_op(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_delayed_ref_node *node,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_extent_item *ei;
	struct extent_buffer *leaf;
	u32 item_size;
	int ret;
	int err = 0;
	int metadata = (node->type == BTRFS_TREE_BLOCK_REF_KEY ||
			node->type == BTRFS_SHARED_BLOCK_REF_KEY);

	if (trans->aborted)
		return 0;

	if (metadata && !btrfs_fs_incompat(root->fs_info, SKINNY_METADATA))
		metadata = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = node->bytenr;

	if (metadata) {
		struct btrfs_delayed_tree_ref *tree_ref;

		tree_ref = btrfs_delayed_node_to_tree_ref(node);
		key.type = BTRFS_METADATA_ITEM_KEY;
		key.offset = tree_ref->level;
	} else {
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = node->num_bytes;
	}

again:
	path->reada = 1;
	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key,
				path, 0, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	if (ret > 0) {
		if (metadata) {
			btrfs_release_path(path);
			metadata = 0;

			key.offset = node->num_bytes;
			key.type = BTRFS_EXTENT_ITEM_KEY;
			goto again;
		}
		err = -EIO;
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		ret = convert_extent_item_v0(trans, root->fs_info->extent_root,
					     path, (u64)-1, 0);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	__run_delayed_extent_op(extent_op, leaf, ei);

	btrfs_mark_buffer_dirty(leaf);
out:
	btrfs_free_path(path);
	return err;
}

static int run_delayed_tree_ref(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_delayed_ref_node *node,
				struct btrfs_delayed_extent_op *extent_op,
				int insert_reserved)
{
	int ret = 0;
	struct btrfs_delayed_tree_ref *ref;
	struct btrfs_key ins;
	u64 parent = 0;
	u64 ref_root = 0;
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

	ref = btrfs_delayed_node_to_tree_ref(node);
	if (node->type == BTRFS_SHARED_BLOCK_REF_KEY)
		parent = ref->parent;
	else
		ref_root = ref->root;

	ins.objectid = node->bytenr;
	if (skinny_metadata) {
		ins.offset = ref->level;
		ins.type = BTRFS_METADATA_ITEM_KEY;
	} else {
		ins.offset = node->num_bytes;
		ins.type = BTRFS_EXTENT_ITEM_KEY;
	}

	BUG_ON(node->ref_mod != 1);
	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		BUG_ON(!extent_op || !extent_op->update_flags);
		ret = alloc_reserved_tree_block(trans, root,
						parent, ref_root,
						extent_op->flags_to_set,
						&extent_op->key,
						ref->level, &ins);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, root, node->bytenr,
					     node->num_bytes, parent, ref_root,
					     ref->level, 0, 1, extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, root, node->bytenr,
					  node->num_bytes, parent, ref_root,
					  ref->level, 0, 1, extent_op);
	} else {
		BUG();
	}
	return ret;
}

/* helper function to actually process a single delayed ref entry */
static int run_one_delayed_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_delayed_ref_node *node,
			       struct btrfs_delayed_extent_op *extent_op,
			       int insert_reserved)
{
	int ret = 0;

	if (trans->aborted)
		return 0;

	if (btrfs_delayed_ref_is_head(node)) {
		struct btrfs_delayed_ref_head *head;
		/*
		 * we've hit the end of the chain and we were supposed
		 * to insert this extent into the tree.  But, it got
		 * deleted before we ever needed to insert it, so all
		 * we have to do is clean up the accounting
		 */
		BUG_ON(extent_op);
		head = btrfs_delayed_node_to_head(node);
		if (insert_reserved) {
			btrfs_pin_extent(root, node->bytenr,
					 node->num_bytes, 1);
			if (head->is_data) {
				ret = btrfs_del_csums(trans, root,
						      node->bytenr,
						      node->num_bytes);
			}
		}
		return ret;
	}

	if (node->type == BTRFS_TREE_BLOCK_REF_KEY ||
	    node->type == BTRFS_SHARED_BLOCK_REF_KEY)
		ret = run_delayed_tree_ref(trans, root, node, extent_op,
					   insert_reserved);
	else if (node->type == BTRFS_EXTENT_DATA_REF_KEY ||
		 node->type == BTRFS_SHARED_DATA_REF_KEY)
		ret = run_delayed_data_ref(trans, root, node, extent_op,
					   insert_reserved);
	else
		BUG();
	return ret;
}

static noinline struct btrfs_delayed_ref_node *
select_delayed_ref(struct btrfs_delayed_ref_head *head)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_node *ref;
	int action = BTRFS_ADD_DELAYED_REF;
again:
	/*
	 * select delayed ref of type BTRFS_ADD_DELAYED_REF first.
	 * this prevents ref count from going down to zero when
	 * there still are pending delayed ref.
	 */
	node = rb_prev(&head->node.rb_node);
	while (1) {
		if (!node)
			break;
		ref = rb_entry(node, struct btrfs_delayed_ref_node,
				rb_node);
		if (ref->bytenr != head->node.bytenr)
			break;
		if (ref->action == action)
			return ref;
		node = rb_prev(node);
	}
	if (action == BTRFS_ADD_DELAYED_REF) {
		action = BTRFS_DROP_DELAYED_REF;
		goto again;
	}
	return NULL;
}

/*
 * Returns 0 on success or if called with an already aborted transaction.
 * Returns -ENOMEM or -EIO on failure and will abort the transaction.
 */
static noinline int run_clustered_refs(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct list_head *cluster)
{
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_head *locked_ref = NULL;
	struct btrfs_delayed_extent_op *extent_op;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;
	int count = 0;
	int must_insert_reserved = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	while (1) {
		if (!locked_ref) {
			/* pick a new head ref from the cluster list */
			if (list_empty(cluster))
				break;

			locked_ref = list_entry(cluster->next,
				     struct btrfs_delayed_ref_head, cluster);

			/* grab the lock that says we are going to process
			 * all the refs for this head */
			ret = btrfs_delayed_ref_lock(trans, locked_ref);

			/*
			 * we may have dropped the spin lock to get the head
			 * mutex lock, and that might have given someone else
			 * time to free the head.  If that's true, it has been
			 * removed from our list and we can move on.
			 */
			if (ret == -EAGAIN) {
				locked_ref = NULL;
				count++;
				continue;
			}
		}

		/*
		 * We need to try and merge add/drops of the same ref since we
		 * can run into issues with relocate dropping the implicit ref
		 * and then it being added back again before the drop can
		 * finish.  If we merged anything we need to re-loop so we can
		 * get a good ref.
		 */
		btrfs_merge_delayed_refs(trans, fs_info, delayed_refs,
					 locked_ref);

		/*
		 * locked_ref is the head node, so we have to go one
		 * node back for any delayed ref updates
		 */
		ref = select_delayed_ref(locked_ref);

		if (ref && ref->seq &&
		    btrfs_check_delayed_seq(fs_info, delayed_refs, ref->seq)) {
			/*
			 * there are still refs with lower seq numbers in the
			 * process of being added. Don't run this ref yet.
			 */
			list_del_init(&locked_ref->cluster);
			btrfs_delayed_ref_unlock(locked_ref);
			locked_ref = NULL;
			delayed_refs->num_heads_ready++;
			spin_unlock(&delayed_refs->lock);
			cond_resched();
			spin_lock(&delayed_refs->lock);
			continue;
		}

		/*
		 * record the must insert reserved flag before we
		 * drop the spin lock.
		 */
		must_insert_reserved = locked_ref->must_insert_reserved;
		locked_ref->must_insert_reserved = 0;

		extent_op = locked_ref->extent_op;
		locked_ref->extent_op = NULL;

		if (!ref) {
			/* All delayed refs have been processed, Go ahead
			 * and send the head node to run_one_delayed_ref,
			 * so that any accounting fixes can happen
			 */
			ref = &locked_ref->node;

			if (extent_op && must_insert_reserved) {
				btrfs_free_delayed_extent_op(extent_op);
				extent_op = NULL;
			}

			if (extent_op) {
				spin_unlock(&delayed_refs->lock);

				ret = run_delayed_extent_op(trans, root,
							    ref, extent_op);
				btrfs_free_delayed_extent_op(extent_op);

				if (ret) {
					btrfs_debug(fs_info, "run_delayed_extent_op returned %d", ret);
					spin_lock(&delayed_refs->lock);
					btrfs_delayed_ref_unlock(locked_ref);
					return ret;
				}

				goto next;
			}
		}

		ref->in_tree = 0;
		rb_erase(&ref->rb_node, &delayed_refs->root);
		delayed_refs->num_entries--;
		if (!btrfs_delayed_ref_is_head(ref)) {
			/*
			 * when we play the delayed ref, also correct the
			 * ref_mod on head
			 */
			switch (ref->action) {
			case BTRFS_ADD_DELAYED_REF:
			case BTRFS_ADD_DELAYED_EXTENT:
				locked_ref->node.ref_mod -= ref->ref_mod;
				break;
			case BTRFS_DROP_DELAYED_REF:
				locked_ref->node.ref_mod += ref->ref_mod;
				break;
			default:
				WARN_ON(1);
			}
		}
		spin_unlock(&delayed_refs->lock);

		ret = run_one_delayed_ref(trans, root, ref, extent_op,
					  must_insert_reserved);

		btrfs_free_delayed_extent_op(extent_op);
		if (ret) {
			btrfs_delayed_ref_unlock(locked_ref);
			btrfs_put_delayed_ref(ref);
			btrfs_debug(fs_info, "run_one_delayed_ref returned %d", ret);
			spin_lock(&delayed_refs->lock);
			return ret;
		}

		/*
		 * If this node is a head, that means all the refs in this head
		 * have been dealt with, and we will pick the next head to deal
		 * with, so we must unlock the head and drop it from the cluster
		 * list before we release it.
		 */
		if (btrfs_delayed_ref_is_head(ref)) {
			list_del_init(&locked_ref->cluster);
			btrfs_delayed_ref_unlock(locked_ref);
			locked_ref = NULL;
		}
		btrfs_put_delayed_ref(ref);
		count++;
next:
		cond_resched();
		spin_lock(&delayed_refs->lock);
	}
	return count;
}

#ifdef SCRAMBLE_DELAYED_REFS
/*
 * Normally delayed refs get processed in ascending bytenr order. This
 * correlates in most cases to the order added. To expose dependencies on this
 * order, we start to process the tree in the middle instead of the beginning
 */
static u64 find_middle(struct rb_root *root)
{
	struct rb_node *n = root->rb_node;
	struct btrfs_delayed_ref_node *entry;
	int alt = 1;
	u64 middle;
	u64 first = 0, last = 0;

	n = rb_first(root);
	if (n) {
		entry = rb_entry(n, struct btrfs_delayed_ref_node, rb_node);
		first = entry->bytenr;
	}
	n = rb_last(root);
	if (n) {
		entry = rb_entry(n, struct btrfs_delayed_ref_node, rb_node);
		last = entry->bytenr;
	}
	n = root->rb_node;

	while (n) {
		entry = rb_entry(n, struct btrfs_delayed_ref_node, rb_node);
		WARN_ON(!entry->in_tree);

		middle = entry->bytenr;

		if (alt)
			n = n->rb_left;
		else
			n = n->rb_right;

		alt = 1 - alt;
	}
	return middle;
}
#endif

int btrfs_delayed_refs_qgroup_accounting(struct btrfs_trans_handle *trans,
					 struct btrfs_fs_info *fs_info)
{
	struct qgroup_update *qgroup_update;
	int ret = 0;

	if (list_empty(&trans->qgroup_ref_list) !=
	    !trans->delayed_ref_elem.seq) {
		/* list without seq or seq without list */
		btrfs_err(fs_info,
			"qgroup accounting update error, list is%s empty, seq is %llu",
			list_empty(&trans->qgroup_ref_list) ? "" : " not",
			trans->delayed_ref_elem.seq);
		BUG();
	}

	if (!trans->delayed_ref_elem.seq)
		return 0;

	while (!list_empty(&trans->qgroup_ref_list)) {
		qgroup_update = list_first_entry(&trans->qgroup_ref_list,
						 struct qgroup_update, list);
		list_del(&qgroup_update->list);
		if (!ret)
			ret = btrfs_qgroup_account_ref(
					trans, fs_info, qgroup_update->node,
					qgroup_update->extent_op);
		kfree(qgroup_update);
	}

	btrfs_put_tree_mod_seq(fs_info, &trans->delayed_ref_elem);

	return ret;
}

static int refs_newer(struct btrfs_delayed_ref_root *delayed_refs, int seq,
		      int count)
{
	int val = atomic_read(&delayed_refs->ref_seq);

	if (val < seq || val >= seq + count)
		return 1;
	return 0;
}

/*
 * this starts processing the delayed reference count updates and
 * extent insertions we have queued up so far.  count can be
 * 0, which means to process everything in the tree at the start
 * of the run (but not newly added entries), or it can be some target
 * number you'd like to process.
 *
 * Returns 0 on success or if called with an aborted transaction
 * Returns <0 on error and aborts the transaction
 */
int btrfs_run_delayed_refs(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, unsigned long count)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct list_head cluster;
	int ret;
	u64 delayed_start;
	int run_all = count == (unsigned long)-1;
	int run_most = 0;
	int loops;

	/* We'll clean this up in btrfs_cleanup_transaction */
	if (trans->aborted)
		return 0;

	if (root == root->fs_info->extent_root)
		root = root->fs_info->tree_root;

	btrfs_delayed_refs_qgroup_accounting(trans, root->fs_info);

	delayed_refs = &trans->transaction->delayed_refs;
	INIT_LIST_HEAD(&cluster);
	if (count == 0) {
		count = delayed_refs->num_entries * 2;
		run_most = 1;
	}

	if (!run_all && !run_most) {
		int old;
		int seq = atomic_read(&delayed_refs->ref_seq);

progress:
		old = atomic_cmpxchg(&delayed_refs->procs_running_refs, 0, 1);
		if (old) {
			DEFINE_WAIT(__wait);
			if (delayed_refs->num_entries < 16348)
				return 0;

			prepare_to_wait(&delayed_refs->wait, &__wait,
					TASK_UNINTERRUPTIBLE);

			old = atomic_cmpxchg(&delayed_refs->procs_running_refs, 0, 1);
			if (old) {
				schedule();
				finish_wait(&delayed_refs->wait, &__wait);

				if (!refs_newer(delayed_refs, seq, 256))
					goto progress;
				else
					return 0;
			} else {
				finish_wait(&delayed_refs->wait, &__wait);
				goto again;
			}
		}

	} else {
		atomic_inc(&delayed_refs->procs_running_refs);
	}

again:
	loops = 0;
	spin_lock(&delayed_refs->lock);

#ifdef SCRAMBLE_DELAYED_REFS
	delayed_refs->run_delayed_start = find_middle(&delayed_refs->root);
#endif

	while (1) {
		if (!(run_all || run_most) &&
		    delayed_refs->num_heads_ready < 64)
			break;

		/*
		 * go find something we can process in the rbtree.  We start at
		 * the beginning of the tree, and then build a cluster
		 * of refs to process starting at the first one we are able to
		 * lock
		 */
		delayed_start = delayed_refs->run_delayed_start;
		ret = btrfs_find_ref_cluster(trans, &cluster,
					     delayed_refs->run_delayed_start);
		if (ret)
			break;

		ret = run_clustered_refs(trans, root, &cluster);
		if (ret < 0) {
			btrfs_release_ref_cluster(&cluster);
			spin_unlock(&delayed_refs->lock);
			btrfs_abort_transaction(trans, root, ret);
			atomic_dec(&delayed_refs->procs_running_refs);
			return ret;
		}

		atomic_add(ret, &delayed_refs->ref_seq);

		count -= min_t(unsigned long, ret, count);

		if (count == 0)
			break;

		if (delayed_start >= delayed_refs->run_delayed_start) {
			if (loops == 0) {
				/*
				 * btrfs_find_ref_cluster looped. let's do one
				 * more cycle. if we don't run any delayed ref
				 * during that cycle (because we can't because
				 * all of them are blocked), bail out.
				 */
				loops = 1;
			} else {
				/*
				 * no runnable refs left, stop trying
				 */
				BUG_ON(run_all);
				break;
			}
		}
		if (ret) {
			/* refs were run, let's reset staleness detection */
			loops = 0;
		}
	}

	if (run_all) {
		if (!list_empty(&trans->new_bgs)) {
			spin_unlock(&delayed_refs->lock);
			btrfs_create_pending_block_groups(trans, root);
			spin_lock(&delayed_refs->lock);
		}

		node = rb_first(&delayed_refs->root);
		if (!node)
			goto out;
		count = (unsigned long)-1;

		while (node) {
			ref = rb_entry(node, struct btrfs_delayed_ref_node,
				       rb_node);
			if (btrfs_delayed_ref_is_head(ref)) {
				struct btrfs_delayed_ref_head *head;

				head = btrfs_delayed_node_to_head(ref);
				atomic_inc(&ref->refs);

				spin_unlock(&delayed_refs->lock);
				/*
				 * Mutex was contended, block until it's
				 * released and try again
				 */
				mutex_lock(&head->mutex);
				mutex_unlock(&head->mutex);

				btrfs_put_delayed_ref(ref);
				cond_resched();
				goto again;
			}
			node = rb_next(node);
		}
		spin_unlock(&delayed_refs->lock);
		schedule_timeout(1);
		goto again;
	}
out:
	atomic_dec(&delayed_refs->procs_running_refs);
	smp_mb();
	if (waitqueue_active(&delayed_refs->wait))
		wake_up(&delayed_refs->wait);

	spin_unlock(&delayed_refs->lock);
	assert_qgroups_uptodate(trans);
	return 0;
}

int btrfs_set_disk_extent_flags(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes, u64 flags,
				int is_data)
{
	struct btrfs_delayed_extent_op *extent_op;
	int ret;

	extent_op = btrfs_alloc_delayed_extent_op();
	if (!extent_op)
		return -ENOMEM;

	extent_op->flags_to_set = flags;
	extent_op->update_flags = 1;
	extent_op->update_key = 0;
	extent_op->is_data = is_data ? 1 : 0;

	ret = btrfs_add_delayed_extent_op(root->fs_info, trans, bytenr,
					  num_bytes, extent_op);
	if (ret)
		btrfs_free_delayed_extent_op(extent_op);
	return ret;
}

static noinline int check_delayed_ref(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_data_ref *data_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct rb_node *node;
	int ret = 0;

	ret = -ENOENT;
	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(trans, bytenr);
	if (!head)
		goto out;

	if (!mutex_trylock(&head->mutex)) {
		atomic_inc(&head->node.refs);
		spin_unlock(&delayed_refs->lock);

		btrfs_release_path(path);

		/*
		 * Mutex was contended, block until it's released and let
		 * caller try again
		 */
		mutex_lock(&head->mutex);
		mutex_unlock(&head->mutex);
		btrfs_put_delayed_ref(&head->node);
		return -EAGAIN;
	}

	node = rb_prev(&head->node.rb_node);
	if (!node)
		goto out_unlock;

	ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);

	if (ref->bytenr != bytenr)
		goto out_unlock;

	ret = 1;
	if (ref->type != BTRFS_EXTENT_DATA_REF_KEY)
		goto out_unlock;

	data_ref = btrfs_delayed_node_to_data_ref(ref);

	node = rb_prev(node);
	if (node) {
		int seq = ref->seq;

		ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
		if (ref->bytenr == bytenr && ref->seq == seq)
			goto out_unlock;
	}

	if (data_ref->root != root->root_key.objectid ||
	    data_ref->objectid != objectid || data_ref->offset != offset)
		goto out_unlock;

	ret = 0;
out_unlock:
	mutex_unlock(&head->mutex);
out:
	spin_unlock(&delayed_refs->lock);
	return ret;
}

static noinline int check_committed_ref(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct extent_buffer *leaf;
	struct btrfs_extent_data_ref *ref;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_extent_item *ei;
	struct btrfs_key key;
	u32 item_size;
	int ret;

	key.objectid = bytenr;
	key.offset = (u64)-1;
	key.type = BTRFS_EXTENT_ITEM_KEY;

	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0); /* Corruption */

	ret = -ENOENT;
	if (path->slots[0] == 0)
		goto out;

	path->slots[0]--;
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

	if (key.objectid != bytenr || key.type != BTRFS_EXTENT_ITEM_KEY)
		goto out;

	ret = 1;
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		WARN_ON(item_size != sizeof(struct btrfs_extent_item_v0));
		goto out;
	}
#endif
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);

	if (item_size != sizeof(*ei) +
	    btrfs_extent_inline_ref_size(BTRFS_EXTENT_DATA_REF_KEY))
		goto out;

	if (btrfs_extent_generation(leaf, ei) <=
	    btrfs_root_last_snapshot(&root->root_item))
		goto out;

	iref = (struct btrfs_extent_inline_ref *)(ei + 1);
	if (btrfs_extent_inline_ref_type(leaf, iref) !=
	    BTRFS_EXTENT_DATA_REF_KEY)
		goto out;

	ref = (struct btrfs_extent_data_ref *)(&iref->offset);
	if (btrfs_extent_refs(leaf, ei) !=
	    btrfs_extent_data_ref_count(leaf, ref) ||
	    btrfs_extent_data_ref_root(leaf, ref) !=
	    root->root_key.objectid ||
	    btrfs_extent_data_ref_objectid(leaf, ref) != objectid ||
	    btrfs_extent_data_ref_offset(leaf, ref) != offset)
		goto out;

	ret = 0;
out:
	return ret;
}

int btrfs_cross_ref_exist(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_path *path;
	int ret;
	int ret2;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOENT;

	do {
		ret = check_committed_ref(trans, root, path, objectid,
					  offset, bytenr);
		if (ret && ret != -ENOENT)
			goto out;

		ret2 = check_delayed_ref(trans, root, path, objectid,
					 offset, bytenr);
	} while (ret2 == -EAGAIN);

	if (ret2 && ret2 != -ENOENT) {
		ret = ret2;
		goto out;
	}

	if (ret != -ENOENT || ret2 != -ENOENT)
		ret = 0;
out:
	btrfs_free_path(path);
	if (root->root_key.objectid == BTRFS_DATA_RELOC_TREE_OBJECTID)
		WARN_ON(ret > 0);
	return ret;
}

static int __btrfs_mod_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct extent_buffer *buf,
			   int full_backref, int inc, int for_cow)
{
	u64 bytenr;
	u64 num_bytes;
	u64 parent;
	u64 ref_root;
	u32 nritems;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	int i;
	int level;
	int ret = 0;
	int (*process_func)(struct btrfs_trans_handle *, struct btrfs_root *,
			    u64, u64, u64, u64, u64, u64, int);

	ref_root = btrfs_header_owner(buf);
	nritems = btrfs_header_nritems(buf);
	level = btrfs_header_level(buf);

	if (!root->ref_cows && level == 0)
		return 0;

	if (inc)
		process_func = btrfs_inc_extent_ref;
	else
		process_func = btrfs_free_extent;

	if (full_backref)
		parent = buf->start;
	else
		parent = 0;

	for (i = 0; i < nritems; i++) {
		if (level == 0) {
			btrfs_item_key_to_cpu(buf, &key, i);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			bytenr = btrfs_file_extent_disk_bytenr(buf, fi);
			if (bytenr == 0)
				continue;

			num_bytes = btrfs_file_extent_disk_num_bytes(buf, fi);
			key.offset -= btrfs_file_extent_offset(buf, fi);
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, key.objectid,
					   key.offset, for_cow);
			if (ret)
				goto fail;
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			num_bytes = btrfs_level_size(root, level - 1);
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, level - 1, 0,
					   for_cow);
			if (ret)
				goto fail;
		}
	}
	return 0;
fail:
	return ret;
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref, int for_cow)
{
	return __btrfs_mod_ref(trans, root, buf, full_backref, 1, for_cow);
}

int btrfs_dec_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref, int for_cow)
{
	return __btrfs_mod_ref(trans, root, buf, full_backref, 0, for_cow);
}

static int write_one_cache_group(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_block_group_cache *cache)
{
	int ret;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	unsigned long bi;
	struct extent_buffer *leaf;

	ret = btrfs_search_slot(trans, extent_root, &cache->key, path, 0, 1);
	if (ret < 0)
		goto fail;
	BUG_ON(ret); /* Corruption */

	leaf = path->nodes[0];
	bi = btrfs_item_ptr_offset(leaf, path->slots[0]);
	write_extent_buffer(leaf, &cache->item, bi, sizeof(cache->item));
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);
fail:
	if (ret) {
		btrfs_abort_transaction(trans, root, ret);
		return ret;
	}
	return 0;

}

static struct btrfs_block_group_cache *
next_block_group(struct btrfs_root *root,
		 struct btrfs_block_group_cache *cache)
{
	struct rb_node *node;
	spin_lock(&root->fs_info->block_group_cache_lock);
	node = rb_next(&cache->cache_node);
	btrfs_put_block_group(cache);
	if (node) {
		cache = rb_entry(node, struct btrfs_block_group_cache,
				 cache_node);
		btrfs_get_block_group(cache);
	} else
		cache = NULL;
	spin_unlock(&root->fs_info->block_group_cache_lock);
	return cache;
}

static int cache_save_setup(struct btrfs_block_group_cache *block_group,
			    struct btrfs_trans_handle *trans,
			    struct btrfs_path *path)
{
	struct btrfs_root *root = block_group->fs_info->tree_root;
	struct inode *inode = NULL;
	u64 alloc_hint = 0;
	int dcs = BTRFS_DC_ERROR;
	int num_pages = 0;
	int retries = 0;
	int ret = 0;

	/*
	 * If this block group is smaller than 100 megs don't bother caching the
	 * block group.
	 */
	if (block_group->key.offset < (100 * 1024 * 1024)) {
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_WRITTEN;
		spin_unlock(&block_group->lock);
		return 0;
	}

again:
	inode = lookup_free_space_inode(root, block_group, path);
	if (IS_ERR(inode) && PTR_ERR(inode) != -ENOENT) {
		ret = PTR_ERR(inode);
		btrfs_release_path(path);
		goto out;
	}

	if (IS_ERR(inode)) {
		BUG_ON(retries);
		retries++;

		if (block_group->ro)
			goto out_free;

		ret = create_free_space_inode(root, trans, block_group, path);
		if (ret)
			goto out_free;
		goto again;
	}

	/* We've already setup this transaction, go ahead and exit */
	if (block_group->cache_generation == trans->transid &&
	    i_size_read(inode)) {
		dcs = BTRFS_DC_SETUP;
		goto out_put;
	}

	/*
	 * We want to set the generation to 0, that way if anything goes wrong
	 * from here on out we know not to trust this cache when we load up next
	 * time.
	 */
	BTRFS_I(inode)->generation = 0;
	ret = btrfs_update_inode(trans, root, inode);
	WARN_ON(ret);

	if (i_size_read(inode) > 0) {
		ret = btrfs_truncate_free_space_cache(root, trans, path,
						      inode);
		if (ret)
			goto out_put;
	}

	spin_lock(&block_group->lock);
	if (block_group->cached != BTRFS_CACHE_FINISHED ||
	    !btrfs_test_opt(root, SPACE_CACHE)) {
		/*
		 * don't bother trying to write stuff out _if_
		 * a) we're not cached,
		 * b) we're with nospace_cache mount option.
		 */
		dcs = BTRFS_DC_WRITTEN;
		spin_unlock(&block_group->lock);
		goto out_put;
	}
	spin_unlock(&block_group->lock);

	/*
	 * Try to preallocate enough space based on how big the block group is.
	 * Keep in mind this has to include any pinned space which could end up
	 * taking up quite a bit since it's not folded into the other space
	 * cache.
	 */
	num_pages = (int)div64_u64(block_group->key.offset, 256 * 1024 * 1024);
	if (!num_pages)
		num_pages = 1;

	num_pages *= 16;
	num_pages *= PAGE_CACHE_SIZE;

	ret = btrfs_check_data_free_space(inode, num_pages);
	if (ret)
		goto out_put;

	ret = btrfs_prealloc_file_range_trans(inode, trans, 0, 0, num_pages,
					      num_pages, num_pages,
					      &alloc_hint);
	if (!ret)
		dcs = BTRFS_DC_SETUP;
	btrfs_free_reserved_data_space(inode, num_pages);

out_put:
	iput(inode);
out_free:
	btrfs_release_path(path);
out:
	spin_lock(&block_group->lock);
	if (!ret && dcs == BTRFS_DC_SETUP)
		block_group->cache_generation = trans->transid;
	block_group->disk_cache_state = dcs;
	spin_unlock(&block_group->lock);

	return ret;
}

int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
	struct btrfs_block_group_cache *cache;
	int err = 0;
	struct btrfs_path *path;
	u64 last = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

again:
	while (1) {
		cache = btrfs_lookup_first_block_group(root->fs_info, last);
		while (cache) {
			if (cache->disk_cache_state == BTRFS_DC_CLEAR)
				break;
			cache = next_block_group(root, cache);
		}
		if (!cache) {
			if (last == 0)
				break;
			last = 0;
			continue;
		}
		err = cache_save_setup(cache, trans, path);
		last = cache->key.objectid + cache->key.offset;
		btrfs_put_block_group(cache);
	}

	while (1) {
		if (last == 0) {
			err = btrfs_run_delayed_refs(trans, root,
						     (unsigned long)-1);
			if (err) /* File system offline */
				goto out;
		}

		cache = btrfs_lookup_first_block_group(root->fs_info, last);
		while (cache) {
			if (cache->disk_cache_state == BTRFS_DC_CLEAR) {
				btrfs_put_block_group(cache);
				goto again;
			}

			if (cache->dirty)
				break;
			cache = next_block_group(root, cache);
		}
		if (!cache) {
			if (last == 0)
				break;
			last = 0;
			continue;
		}

		if (cache->disk_cache_state == BTRFS_DC_SETUP)
			cache->disk_cache_state = BTRFS_DC_NEED_WRITE;
		cache->dirty = 0;
		last = cache->key.objectid + cache->key.offset;

		err = write_one_cache_group(trans, root, path, cache);
		if (err) /* File system offline */
			goto out;

		btrfs_put_block_group(cache);
	}

	while (1) {
		/*
		 * I don't think this is needed since we're just marking our
		 * preallocated extent as written, but just in case it can't
		 * hurt.
		 */
		if (last == 0) {
			err = btrfs_run_delayed_refs(trans, root,
						     (unsigned long)-1);
			if (err) /* File system offline */
				goto out;
		}

		cache = btrfs_lookup_first_block_group(root->fs_info, last);
		while (cache) {
			/*
			 * Really this shouldn't happen, but it could if we
			 * couldn't write the entire preallocated extent and
			 * splitting the extent resulted in a new block.
			 */
			if (cache->dirty) {
				btrfs_put_block_group(cache);
				goto again;
			}
			if (cache->disk_cache_state == BTRFS_DC_NEED_WRITE)
				break;
			cache = next_block_group(root, cache);
		}
		if (!cache) {
			if (last == 0)
				break;
			last = 0;
			continue;
		}

		err = btrfs_write_out_cache(root, trans, cache, path);

		/*
		 * If we didn't have an error then the cache state is still
		 * NEED_WRITE, so we can set it to WRITTEN.
		 */
		if (!err && cache->disk_cache_state == BTRFS_DC_NEED_WRITE)
			cache->disk_cache_state = BTRFS_DC_WRITTEN;
		last = cache->key.objectid + cache->key.offset;
		btrfs_put_block_group(cache);
	}
out:

	btrfs_free_path(path);
	return err;
}

int btrfs_extent_readonly(struct btrfs_root *root, u64 bytenr)
{
	struct btrfs_block_group_cache *block_group;
	int readonly = 0;

	block_group = btrfs_lookup_block_group(root->fs_info, bytenr);
	if (!block_group || block_group->ro)
		readonly = 1;
	if (block_group)
		btrfs_put_block_group(block_group);
	return readonly;
}

static int update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;
	int i;
	int factor;

	if (flags & (BTRFS_BLOCK_GROUP_DUP | BTRFS_BLOCK_GROUP_RAID1 |
		     BTRFS_BLOCK_GROUP_RAID10))
		factor = 2;
	else
		factor = 1;

	found = __find_space_info(info, flags);
	if (found) {
		spin_lock(&found->lock);
		found->total_bytes += total_bytes;
		found->disk_total += total_bytes * factor;
		found->bytes_used += bytes_used;
		found->disk_used += bytes_used * factor;
		found->full = 0;
		spin_unlock(&found->lock);
		*space_info = found;
		return 0;
	}
	found = kzalloc(sizeof(*found), GFP_NOFS);
	if (!found)
		return -ENOMEM;

	for (i = 0; i < BTRFS_NR_RAID_TYPES; i++)
		INIT_LIST_HEAD(&found->block_groups[i]);
	init_rwsem(&found->groups_sem);
	spin_lock_init(&found->lock);
	found->flags = flags & BTRFS_BLOCK_GROUP_TYPE_MASK;
	found->total_bytes = total_bytes;
	found->disk_total = total_bytes * factor;
	found->bytes_used = bytes_used;
	found->disk_used = bytes_used * factor;
	found->bytes_pinned = 0;
	found->bytes_reserved = 0;
	found->bytes_readonly = 0;
	found->bytes_may_use = 0;
	found->full = 0;
	found->force_alloc = CHUNK_ALLOC_NO_FORCE;
	found->chunk_alloc = 0;
	found->flush = 0;
	init_waitqueue_head(&found->wait);
	*space_info = found;
	list_add_rcu(&found->list, &info->space_info);
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		info->data_sinfo = found;
	return 0;
}

static void set_avail_alloc_bits(struct btrfs_fs_info *fs_info, u64 flags)
{
	u64 extra_flags = chunk_to_extended(flags) &
				BTRFS_EXTENDED_PROFILE_MASK;

	write_seqlock(&fs_info->profiles_lock);
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		fs_info->avail_data_alloc_bits |= extra_flags;
	if (flags & BTRFS_BLOCK_GROUP_METADATA)
		fs_info->avail_metadata_alloc_bits |= extra_flags;
	if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
		fs_info->avail_system_alloc_bits |= extra_flags;
	write_sequnlock(&fs_info->profiles_lock);
}

/*
 * returns target flags in extended format or 0 if restripe for this
 * chunk_type is not in progress
 *
 * should be called with either volume_mutex or balance_lock held
 */
static u64 get_restripe_target(struct btrfs_fs_info *fs_info, u64 flags)
{
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;
	u64 target = 0;

	if (!bctl)
		return 0;

	if (flags & BTRFS_BLOCK_GROUP_DATA &&
	    bctl->data.flags & BTRFS_BALANCE_ARGS_CONVERT) {
		target = BTRFS_BLOCK_GROUP_DATA | bctl->data.target;
	} else if (flags & BTRFS_BLOCK_GROUP_SYSTEM &&
		   bctl->sys.flags & BTRFS_BALANCE_ARGS_CONVERT) {
		target = BTRFS_BLOCK_GROUP_SYSTEM | bctl->sys.target;
	} else if (flags & BTRFS_BLOCK_GROUP_METADATA &&
		   bctl->meta.flags & BTRFS_BALANCE_ARGS_CONVERT) {
		target = BTRFS_BLOCK_GROUP_METADATA | bctl->meta.target;
	}

	return target;
}

/*
 * @flags: available profiles in extended format (see ctree.h)
 *
 * Returns reduced profile in chunk format.  If profile changing is in
 * progress (either running or paused) picks the target profile (if it's
 * already available), otherwise falls back to plain reducing.
 */
u64 btrfs_reduce_alloc_profile(struct btrfs_root *root, u64 flags)
{
	/*
	 * we add in the count of missing devices because we want
	 * to make sure that any RAID levels on a degraded FS
	 * continue to be honored.
	 */
	u64 num_devices = root->fs_info->fs_devices->rw_devices +
		root->fs_info->fs_devices->missing_devices;
	u64 target;
	u64 tmp;

	/*
	 * see if restripe for this chunk_type is in progress, if so
	 * try to reduce to the target profile
	 */
	spin_lock(&root->fs_info->balance_lock);
	target = get_restripe_target(root->fs_info, flags);
	if (target) {
		/* pick target profile only if it's already available */
		if ((flags & target) & BTRFS_EXTENDED_PROFILE_MASK) {
			spin_unlock(&root->fs_info->balance_lock);
			return extended_to_chunk(target);
		}
	}
	spin_unlock(&root->fs_info->balance_lock);

	/* First, mask out the RAID levels which aren't possible */
	if (num_devices == 1)
		flags &= ~(BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID0 |
			   BTRFS_BLOCK_GROUP_RAID5);
	if (num_devices < 3)
		flags &= ~BTRFS_BLOCK_GROUP_RAID6;
	if (num_devices < 4)
		flags &= ~BTRFS_BLOCK_GROUP_RAID10;

	tmp = flags & (BTRFS_BLOCK_GROUP_DUP | BTRFS_BLOCK_GROUP_RAID0 |
		       BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID5 |
		       BTRFS_BLOCK_GROUP_RAID6 | BTRFS_BLOCK_GROUP_RAID10);
	flags &= ~tmp;

	if (tmp & BTRFS_BLOCK_GROUP_RAID6)
		tmp = BTRFS_BLOCK_GROUP_RAID6;
	else if (tmp & BTRFS_BLOCK_GROUP_RAID5)
		tmp = BTRFS_BLOCK_GROUP_RAID5;
	else if (tmp & BTRFS_BLOCK_GROUP_RAID10)
		tmp = BTRFS_BLOCK_GROUP_RAID10;
	else if (tmp & BTRFS_BLOCK_GROUP_RAID1)
		tmp = BTRFS_BLOCK_GROUP_RAID1;
	else if (tmp & BTRFS_BLOCK_GROUP_RAID0)
		tmp = BTRFS_BLOCK_GROUP_RAID0;

	return extended_to_chunk(flags | tmp);
}

static u64 get_alloc_profile(struct btrfs_root *root, u64 flags)
{
	unsigned seq;

	do {
		seq = read_seqbegin(&root->fs_info->profiles_lock);

		if (flags & BTRFS_BLOCK_GROUP_DATA)
			flags |= root->fs_info->avail_data_alloc_bits;
		else if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
			flags |= root->fs_info->avail_system_alloc_bits;
		else if (flags & BTRFS_BLOCK_GROUP_METADATA)
			flags |= root->fs_info->avail_metadata_alloc_bits;
	} while (read_seqretry(&root->fs_info->profiles_lock, seq));

	return btrfs_reduce_alloc_profile(root, flags);
}

u64 btrfs_get_alloc_profile(struct btrfs_root *root, int data)
{
	u64 flags;
	u64 ret;

	if (data)
		flags = BTRFS_BLOCK_GROUP_DATA;
	else if (root == root->fs_info->chunk_root)
		flags = BTRFS_BLOCK_GROUP_SYSTEM;
	else
		flags = BTRFS_BLOCK_GROUP_METADATA;

	ret = get_alloc_profile(root, flags);
	return ret;
}

/*
 * This will check the space that the inode allocates from to make sure we have
 * enough space for bytes.
 */
int btrfs_check_data_free_space(struct inode *inode, u64 bytes)
{
	struct btrfs_space_info *data_sinfo;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 used;
	int ret = 0, committed = 0, alloc_chunk = 1;

	/* make sure bytes are sectorsize aligned */
	bytes = ALIGN(bytes, root->sectorsize);

	if (root == root->fs_info->tree_root ||
	    BTRFS_I(inode)->location.objectid == BTRFS_FREE_INO_OBJECTID) {
		alloc_chunk = 0;
		committed = 1;
	}

	data_sinfo = fs_info->data_sinfo;
	if (!data_sinfo)
		goto alloc;

again:
	/* make sure we have enough space to handle the data first */
	spin_lock(&data_sinfo->lock);
	used = data_sinfo->bytes_used + data_sinfo->bytes_reserved +
		data_sinfo->bytes_pinned + data_sinfo->bytes_readonly +
		data_sinfo->bytes_may_use;

	if (used + bytes > data_sinfo->total_bytes) {
		struct btrfs_trans_handle *trans;

		/*
		 * if we don't have enough free bytes in this space then we need
		 * to alloc a new chunk.
		 */
		if (!data_sinfo->full && alloc_chunk) {
			u64 alloc_target;

			data_sinfo->force_alloc = CHUNK_ALLOC_FORCE;
			spin_unlock(&data_sinfo->lock);
alloc:
			alloc_target = btrfs_get_alloc_profile(root, 1);
			trans = btrfs_join_transaction(root);
			if (IS_ERR(trans))
				return PTR_ERR(trans);

			ret = do_chunk_alloc(trans, root->fs_info->extent_root,
					     alloc_target,
					     CHUNK_ALLOC_NO_FORCE);
			btrfs_end_transaction(trans, root);
			if (ret < 0) {
				if (ret != -ENOSPC)
					return ret;
				else
					goto commit_trans;
			}

			if (!data_sinfo)
				data_sinfo = fs_info->data_sinfo;

			goto again;
		}

		/*
		 * If we have less pinned bytes than we want to allocate then
		 * don't bother committing the transaction, it won't help us.
		 */
		if (data_sinfo->bytes_pinned < bytes)
			committed = 1;
		spin_unlock(&data_sinfo->lock);

		/* commit the current transaction and try again */
commit_trans:
		if (!committed &&
		    !atomic_read(&root->fs_info->open_ioctl_trans)) {
			committed = 1;
			trans = btrfs_join_transaction(root);
			if (IS_ERR(trans))
				return PTR_ERR(trans);
			ret = btrfs_commit_transaction(trans, root);
			if (ret)
				return ret;
			goto again;
		}

		return -ENOSPC;
	}
	data_sinfo->bytes_may_use += bytes;
	trace_btrfs_space_reservation(root->fs_info, "space_info",
				      data_sinfo->flags, bytes, 1);
	spin_unlock(&data_sinfo->lock);

	return 0;
}

/*
 * Called if we need to clear a data reservation for this inode.
 */
void btrfs_free_reserved_data_space(struct inode *inode, u64 bytes)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_space_info *data_sinfo;

	/* make sure bytes are sectorsize aligned */
	bytes = ALIGN(bytes, root->sectorsize);

	data_sinfo = root->fs_info->data_sinfo;
	spin_lock(&data_sinfo->lock);
	data_sinfo->bytes_may_use -= bytes;
	trace_btrfs_space_reservation(root->fs_info, "space_info",
				      data_sinfo->flags, bytes, 0);
	spin_unlock(&data_sinfo->lock);
}

static void force_metadata_allocation(struct btrfs_fs_info *info)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list) {
		if (found->flags & BTRFS_BLOCK_GROUP_METADATA)
			found->force_alloc = CHUNK_ALLOC_FORCE;
	}
	rcu_read_unlock();
}

static int should_alloc_chunk(struct btrfs_root *root,
			      struct btrfs_space_info *sinfo, int force)
{
	struct btrfs_block_rsv *global_rsv = &root->fs_info->global_block_rsv;
	u64 num_bytes = sinfo->total_bytes - sinfo->bytes_readonly;
	u64 num_allocated = sinfo->bytes_used + sinfo->bytes_reserved;
	u64 thresh;

	if (force == CHUNK_ALLOC_FORCE)
		return 1;

	/*
	 * We need to take into account the global rsv because for all intents
	 * and purposes it's used space.  Don't worry about locking the
	 * global_rsv, it doesn't change except when the transaction commits.
	 */
	if (sinfo->flags & BTRFS_BLOCK_GROUP_METADATA)
		num_allocated += global_rsv->size;

	/*
	 * in limited mode, we want to have some free space up to
	 * about 1% of the FS size.
	 */
	if (force == CHUNK_ALLOC_LIMITED) {
		thresh = btrfs_super_total_bytes(root->fs_info->super_copy);
		thresh = max_t(u64, 64 * 1024 * 1024,
			       div_factor_fine(thresh, 1));

		if (num_bytes - num_allocated < thresh)
			return 1;
	}

	if (num_allocated + 2 * 1024 * 1024 < div_factor(num_bytes, 8))
		return 0;
	return 1;
}

static u64 get_system_chunk_thresh(struct btrfs_root *root, u64 type)
{
	u64 num_dev;

	if (type & (BTRFS_BLOCK_GROUP_RAID10 |
		    BTRFS_BLOCK_GROUP_RAID0 |
		    BTRFS_BLOCK_GROUP_RAID5 |
		    BTRFS_BLOCK_GROUP_RAID6))
		num_dev = root->fs_info->fs_devices->rw_devices;
	else if (type & BTRFS_BLOCK_GROUP_RAID1)
		num_dev = 2;
	else
		num_dev = 1;	/* DUP or single */

	/* metadata for updaing devices and chunk tree */
	return btrfs_calc_trans_metadata_size(root, num_dev + 1);
}

static void check_system_chunk(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root, u64 type)
{
	struct btrfs_space_info *info;
	u64 left;
	u64 thresh;

	info = __find_space_info(root->fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
	spin_lock(&info->lock);
	left = info->total_bytes - info->bytes_used - info->bytes_pinned -
		info->bytes_reserved - info->bytes_readonly;
	spin_unlock(&info->lock);

	thresh = get_system_chunk_thresh(root, type);
	if (left < thresh && btrfs_test_opt(root, ENOSPC_DEBUG)) {
		btrfs_info(root->fs_info, "left=%llu, need=%llu, flags=%llu",
			left, thresh, type);
		dump_space_info(info, 0, 0);
	}

	if (left < thresh) {
		u64 flags;

		flags = btrfs_get_alloc_profile(root->fs_info->chunk_root, 0);
		btrfs_alloc_chunk(trans, root, flags);
	}
}

static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_root *extent_root, u64 flags, int force)
{
	struct btrfs_space_info *space_info;
	struct btrfs_fs_info *fs_info = extent_root->fs_info;
	int wait_for_alloc = 0;
	int ret = 0;

	/* Don't re-enter if we're already allocating a chunk */
	if (trans->allocating_chunk)
		return -ENOSPC;

	space_info = __find_space_info(extent_root->fs_info, flags);
	if (!space_info) {
		ret = update_space_info(extent_root->fs_info, flags,
					0, 0, &space_info);
		BUG_ON(ret); /* -ENOMEM */
	}
	BUG_ON(!space_info); /* Logic error */

again:
	spin_lock(&space_info->lock);
	if (force < space_info->force_alloc)
		force = space_info->force_alloc;
	if (space_info->full) {
		spin_unlock(&space_info->lock);
		return 0;
	}

	if (!should_alloc_chunk(extent_root, space_info, force)) {
		spin_unlock(&space_info->lock);
		return 0;
	} else if (space_info->chunk_alloc) {
		wait_for_alloc = 1;
	} else {
		space_info->chunk_alloc = 1;
	}

	spin_unlock(&space_info->lock);

	mutex_lock(&fs_info->chunk_mutex);

	/*
	 * The chunk_mutex is held throughout the entirety of a chunk
	 * allocation, so once we've acquired the chunk_mutex we know that the
	 * other guy is done and we need to recheck and see if we should
	 * allocate.
	 */
	if (wait_for_alloc) {
		mutex_unlock(&fs_info->chunk_mutex);
		wait_for_alloc = 0;
		goto again;
	}

	trans->allocating_chunk = true;

	/*
	 * If we have mixed data/metadata chunks we want to make sure we keep
	 * allocating mixed chunks instead of individual chunks.
	 */
	if (btrfs_mixed_space_info(space_info))
		flags |= (BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_METADATA);

	/*
	 * if we're doing a data chunk, go ahead and make sure that
	 * we keep a reasonable number of metadata chunks allocated in the
	 * FS as well.
	 */
	if (flags & BTRFS_BLOCK_GROUP_DATA && fs_info->metadata_ratio) {
		fs_info->data_chunk_allocations++;
		if (!(fs_info->data_chunk_allocations %
		      fs_info->metadata_ratio))
			force_metadata_allocation(fs_info);
	}

	/*
	 * Check if we have enough space in SYSTEM chunk because we may need
	 * to update devices.
	 */
	check_system_chunk(trans, extent_root, flags);

	ret = btrfs_alloc_chunk(trans, extent_root, flags);
	trans->allocating_chunk = false;

	spin_lock(&space_info->lock);
	if (ret < 0 && ret != -ENOSPC)
		goto out;
	if (ret)
		space_info->full = 1;
	else
		ret = 1;

	space_info->force_alloc = CHUNK_ALLOC_NO_FORCE;
out:
	space_info->chunk_alloc = 0;
	spin_unlock(&space_info->lock);
	mutex_unlock(&fs_info->chunk_mutex);
	return ret;
}

static int can_overcommit(struct btrfs_root *root,
			  struct btrfs_space_info *space_info, u64 bytes,
			  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_block_rsv *global_rsv = &root->fs_info->global_block_rsv;
	u64 profile = btrfs_get_alloc_profile(root, 0);
	u64 rsv_size = 0;
	u64 avail;
	u64 used;
	u64 to_add;

	used = space_info->bytes_used + space_info->bytes_reserved +
		space_info->bytes_pinned + space_info->bytes_readonly;

	spin_lock(&global_rsv->lock);
	rsv_size = global_rsv->size;
	spin_unlock(&global_rsv->lock);

	/*
	 * We only want to allow over committing if we have lots of actual space
	 * free, but if we don't have enough space to handle the global reserve
	 * space then we could end up having a real enospc problem when trying
	 * to allocate a chunk or some other such important allocation.
	 */
	rsv_size <<= 1;
	if (used + rsv_size >= space_info->total_bytes)
		return 0;

	used += space_info->bytes_may_use;

	spin_lock(&root->fs_info->free_chunk_lock);
	avail = root->fs_info->free_chunk_space;
	spin_unlock(&root->fs_info->free_chunk_lock);

	/*
	 * If we have dup, raid1 or raid10 then only half of the free
	 * space is actually useable.  For raid56, the space info used
	 * doesn't include the parity drive, so we don't have to
	 * change the math
	 */
	if (profile & (BTRFS_BLOCK_GROUP_DUP |
		       BTRFS_BLOCK_GROUP_RAID1 |
		       BTRFS_BLOCK_GROUP_RAID10))
		avail >>= 1;

	to_add = space_info->total_bytes;

	/*
	 * If we aren't flushing all things, let us overcommit up to
	 * 1/2th of the space. If we can flush, don't let us overcommit
	 * too much, let it overcommit up to 1/8 of the space.
	 */
	if (flush == BTRFS_RESERVE_FLUSH_ALL)
		to_add >>= 3;
	else
		to_add >>= 1;

	/*
	 * Limit the overcommit to the amount of free space we could possibly
	 * allocate for chunks.
	 */
	to_add = min(avail, to_add);

	if (used + bytes < space_info->total_bytes + to_add)
		return 1;
	return 0;
}

void btrfs_writeback_inodes_sb_nr(struct btrfs_root *root,
				  unsigned long nr_pages)
{
	struct super_block *sb = root->fs_info->sb;
	int started;

	/* If we can not start writeback, just sync all the delalloc file. */
	started = try_to_writeback_inodes_sb_nr(sb, nr_pages,
						      WB_REASON_FS_FREE_SPACE);
	if (!started) {
		/*
		 * We needn't worry the filesystem going from r/w to r/o though
		 * we don't acquire ->s_umount mutex, because the filesystem
		 * should guarantee the delalloc inodes list be empty after
		 * the filesystem is readonly(all dirty pages are written to
		 * the disk).
		 */
		btrfs_start_delalloc_inodes(root, 0);
		btrfs_wait_ordered_extents(root, 0);
	}
}

/*
 * shrink metadata reservation for delalloc
 */
static void shrink_delalloc(struct btrfs_root *root, u64 to_reclaim, u64 orig,
			    bool wait_ordered)
{
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_space_info *space_info;
	struct btrfs_trans_handle *trans;
	u64 delalloc_bytes;
	u64 max_reclaim;
	long time_left;
	unsigned long nr_pages = (2 * 1024 * 1024) >> PAGE_CACHE_SHIFT;
	int loops = 0;
	enum btrfs_reserve_flush_enum flush;

	trans = (struct btrfs_trans_handle *)current->journal_info;
	block_rsv = &root->fs_info->delalloc_block_rsv;
	space_info = block_rsv->space_info;

	smp_mb();
	delalloc_bytes = percpu_counter_sum_positive(
						&root->fs_info->delalloc_bytes);
	if (delalloc_bytes == 0) {
		if (trans)
			return;
		btrfs_wait_ordered_extents(root, 0);
		return;
	}

	while (delalloc_bytes && loops < 3) {
		max_reclaim = min(delalloc_bytes, to_reclaim);
		nr_pages = max_reclaim >> PAGE_CACHE_SHIFT;
		btrfs_writeback_inodes_sb_nr(root, nr_pages);
		/*
		 * We need to wait for the async pages to actually start before
		 * we do anything.
		 */
		wait_event(root->fs_info->async_submit_wait,
			   !atomic_read(&root->fs_info->async_delalloc_pages));

		if (!trans)
			flush = BTRFS_RESERVE_FLUSH_ALL;
		else
			flush = BTRFS_RESERVE_NO_FLUSH;
		spin_lock(&space_info->lock);
		if (can_overcommit(root, space_info, orig, flush)) {
			spin_unlock(&space_info->lock);
			break;
		}
		spin_unlock(&space_info->lock);

		loops++;
		if (wait_ordered && !trans) {
			btrfs_wait_ordered_extents(root, 0);
		} else {
			time_left = schedule_timeout_killable(1);
			if (time_left)
				break;
		}
		smp_mb();
		delalloc_bytes = percpu_counter_sum_positive(
						&root->fs_info->delalloc_bytes);
	}
}

/**
 * maybe_commit_transaction - possibly commit the transaction if its ok to
 * @root - the root we're allocating for
 * @bytes - the number of bytes we want to reserve
 * @force - force the commit
 *
 * This will check to make sure that committing the transaction will actually
 * get us somewhere and then commit the transaction if it does.  Otherwise it
 * will return -ENOSPC.
 */
static int may_commit_transaction(struct btrfs_root *root,
				  struct btrfs_space_info *space_info,
				  u64 bytes, int force)
{
	struct btrfs_block_rsv *delayed_rsv = &root->fs_info->delayed_block_rsv;
	struct btrfs_trans_handle *trans;

	trans = (struct btrfs_trans_handle *)current->journal_info;
	if (trans)
		return -EAGAIN;

	if (force)
		goto commit;

	/* See if there is enough pinned space to make this reservation */
	spin_lock(&space_info->lock);
	if (space_info->bytes_pinned >= bytes) {
		spin_unlock(&space_info->lock);
		goto commit;
	}
	spin_unlock(&space_info->lock);

	/*
	 * See if there is some space in the delayed insertion reservation for
	 * this reservation.
	 */
	if (space_info != delayed_rsv->space_info)
		return -ENOSPC;

	spin_lock(&space_info->lock);
	spin_lock(&delayed_rsv->lock);
	if (space_info->bytes_pinned + delayed_rsv->size < bytes) {
		spin_unlock(&delayed_rsv->lock);
		spin_unlock(&space_info->lock);
		return -ENOSPC;
	}
	spin_unlock(&delayed_rsv->lock);
	spin_unlock(&space_info->lock);

commit:
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return -ENOSPC;

	return btrfs_commit_transaction(trans, root);
}

enum flush_state {
	FLUSH_DELAYED_ITEMS_NR	=	1,
	FLUSH_DELAYED_ITEMS	=	2,
	FLUSH_DELALLOC		=	3,
	FLUSH_DELALLOC_WAIT	=	4,
	ALLOC_CHUNK		=	5,
	COMMIT_TRANS		=	6,
};

static int flush_space(struct btrfs_root *root,
		       struct btrfs_space_info *space_info, u64 num_bytes,
		       u64 orig_bytes, int state)
{
	struct btrfs_trans_handle *trans;
	int nr;
	int ret = 0;

	switch (state) {
	case FLUSH_DELAYED_ITEMS_NR:
	case FLUSH_DELAYED_ITEMS:
		if (state == FLUSH_DELAYED_ITEMS_NR) {
			u64 bytes = btrfs_calc_trans_metadata_size(root, 1);

			nr = (int)div64_u64(num_bytes, bytes);
			if (!nr)
				nr = 1;
			nr *= 2;
		} else {
			nr = -1;
		}
		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}
		ret = btrfs_run_delayed_items_nr(trans, root, nr);
		btrfs_end_transaction(trans, root);
		break;
	case FLUSH_DELALLOC:
	case FLUSH_DELALLOC_WAIT:
		shrink_delalloc(root, num_bytes, orig_bytes,
				state == FLUSH_DELALLOC_WAIT);
		break;
	case ALLOC_CHUNK:
		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}
		ret = do_chunk_alloc(trans, root->fs_info->extent_root,
				     btrfs_get_alloc_profile(root, 0),
				     CHUNK_ALLOC_NO_FORCE);
		btrfs_end_transaction(trans, root);
		if (ret == -ENOSPC)
			ret = 0;
		break;
	case COMMIT_TRANS:
		ret = may_commit_transaction(root, space_info, orig_bytes, 0);
		break;
	default:
		ret = -ENOSPC;
		break;
	}

	return ret;
}
/**
 * reserve_metadata_bytes - try to reserve bytes from the block_rsv's space
 * @root - the root we're allocating for
 * @block_rsv - the block_rsv we're allocating for
 * @orig_bytes - the number of bytes we want
 * @flush - whether or not we can flush to make our reservation
 *
 * This will reserve orgi_bytes number of bytes from the space info associated
 * with the block_rsv.  If there is not enough space it will make an attempt to
 * flush out space to make room.  It will do this by flushing delalloc if
 * possible or committing the transaction.  If flush is 0 then no attempts to
 * regain reservations will be made and this will fail if there is not enough
 * space already.
 */
static int reserve_metadata_bytes(struct btrfs_root *root,
				  struct btrfs_block_rsv *block_rsv,
				  u64 orig_bytes,
				  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_space_info *space_info = block_rsv->space_info;
	u64 used;
	u64 num_bytes = orig_bytes;
	int flush_state = FLUSH_DELAYED_ITEMS_NR;
	int ret = 0;
	bool flushing = false;

again:
	ret = 0;
	spin_lock(&space_info->lock);
	/*
	 * We only want to wait if somebody other than us is flushing and we
	 * are actually allowed to flush all things.
	 */
	while (flush == BTRFS_RESERVE_FLUSH_ALL && !flushing &&
	       space_info->flush) {
		spin_unlock(&space_info->lock);
		/*
		 * If we have a trans handle we can't wait because the flusher
		 * may have to commit the transaction, which would mean we would
		 * deadlock since we are waiting for the flusher to finish, but
		 * hold the current transaction open.
		 */
		if (current->journal_info)
			return -EAGAIN;
		ret = wait_event_killable(space_info->wait, !space_info->flush);
		/* Must have been killed, return */
		if (ret)
			return -EINTR;

		spin_lock(&space_info->lock);
	}

	ret = -ENOSPC;
	used = space_info->bytes_used + space_info->bytes_reserved +
		space_info->bytes_pinned + space_info->bytes_readonly +
		space_info->bytes_may_use;

	/*
	 * The idea here is that we've not already over-reserved the block group
	 * then we can go ahead and save our reservation first and then start
	 * flushing if we need to.  Otherwise if we've already overcommitted
	 * lets start flushing stuff first and then come back and try to make
	 * our reservation.
	 */
	if (used <= space_info->total_bytes) {
		if (used + orig_bytes <= space_info->total_bytes) {
			space_info->bytes_may_use += orig_bytes;
			trace_btrfs_space_reservation(root->fs_info,
				"space_info", space_info->flags, orig_bytes, 1);
			ret = 0;
		} else {
			/*
			 * Ok set num_bytes to orig_bytes since we aren't
			 * overocmmitted, this way we only try and reclaim what
			 * we need.
			 */
			num_bytes = orig_bytes;
		}
	} else {
		/*
		 * Ok we're over committed, set num_bytes to the overcommitted
		 * amount plus the amount of bytes that we need for this
		 * reservation.
		 */
		num_bytes = used - space_info->total_bytes +
			(orig_bytes * 2);
	}

	if (ret && can_overcommit(root, space_info, orig_bytes, flush)) {
		space_info->bytes_may_use += orig_bytes;
		trace_btrfs_space_reservation(root->fs_info, "space_info",
					      space_info->flags, orig_bytes,
					      1);
		ret = 0;
	}

	/*
	 * Couldn't make our reservation, save our place so while we're trying
	 * to reclaim space we can actually use it instead of somebody else
	 * stealing it from us.
	 *
	 * We make the other tasks wait for the flush only when we can flush
	 * all things.
	 */
	if (ret && flush != BTRFS_RESERVE_NO_FLUSH) {
		flushing = true;
		space_info->flush = 1;
	}

	spin_unlock(&space_info->lock);

	if (!ret || flush == BTRFS_RESERVE_NO_FLUSH)
		goto out;

	ret = flush_space(root, space_info, num_bytes, orig_bytes,
			  flush_state);
	flush_state++;

	/*
	 * If we are FLUSH_LIMIT, we can not flush delalloc, or the deadlock
	 * would happen. So skip delalloc flush.
	 */
	if (flush == BTRFS_RESERVE_FLUSH_LIMIT &&
	    (flush_state == FLUSH_DELALLOC ||
	     flush_state == FLUSH_DELALLOC_WAIT))
		flush_state = ALLOC_CHUNK;

	if (!ret)
		goto again;
	else if (flush == BTRFS_RESERVE_FLUSH_LIMIT &&
		 flush_state < COMMIT_TRANS)
		goto again;
	else if (flush == BTRFS_RESERVE_FLUSH_ALL &&
		 flush_state <= COMMIT_TRANS)
		goto again;

out:
	if (ret == -ENOSPC &&
	    unlikely(root->orphan_cleanup_state == ORPHAN_CLEANUP_STARTED)) {
		struct btrfs_block_rsv *global_rsv =
			&root->fs_info->global_block_rsv;

		if (block_rsv != global_rsv &&
		    !block_rsv_use_bytes(global_rsv, orig_bytes))
			ret = 0;
	}
	if (flushing) {
		spin_lock(&space_info->lock);
		space_info->flush = 0;
		wake_up_all(&space_info->wait);
		spin_unlock(&space_info->lock);
	}
	return ret;
}

static struct btrfs_block_rsv *get_block_rsv(
					const struct btrfs_trans_handle *trans,
					const struct btrfs_root *root)
{
	struct btrfs_block_rsv *block_rsv = NULL;

	if (root->ref_cows)
		block_rsv = trans->block_rsv;

	if (root == root->fs_info->csum_root && trans->adding_csums)
		block_rsv = trans->block_rsv;

	if (!block_rsv)
		block_rsv = root->block_rsv;

	if (!block_rsv)
		block_rsv = &root->fs_info->empty_block_rsv;

	return block_rsv;
}

static int block_rsv_use_bytes(struct btrfs_block_rsv *block_rsv,
			       u64 num_bytes)
{
	int ret = -ENOSPC;
	spin_lock(&block_rsv->lock);
	if (block_rsv->reserved >= num_bytes) {
		block_rsv->reserved -= num_bytes;
		if (block_rsv->reserved < block_rsv->size)
			block_rsv->full = 0;
		ret = 0;
	}
	spin_unlock(&block_rsv->lock);
	return ret;
}

static void block_rsv_add_bytes(struct btrfs_block_rsv *block_rsv,
				u64 num_bytes, int update_size)
{
	spin_lock(&block_rsv->lock);
	block_rsv->reserved += num_bytes;
	if (update_size)
		block_rsv->size += num_bytes;
	else if (block_rsv->reserved >= block_rsv->size)
		block_rsv->full = 1;
	spin_unlock(&block_rsv->lock);
}

static void block_rsv_release_bytes(struct btrfs_fs_info *fs_info,
				    struct btrfs_block_rsv *block_rsv,
				    struct btrfs_block_rsv *dest, u64 num_bytes)
{
	struct btrfs_space_info *space_info = block_rsv->space_info;

	spin_lock(&block_rsv->lock);
	if (num_bytes == (u64)-1)
		num_bytes = block_rsv->size;
	block_rsv->size -= num_bytes;
	if (block_rsv->reserved >= block_rsv->size) {
		num_bytes = block_rsv->reserved - block_rsv->size;
		block_rsv->reserved = block_rsv->size;
		block_rsv->full = 1;
	} else {
		num_bytes = 0;
	}
	spin_unlock(&block_rsv->lock);

	if (num_bytes > 0) {
		if (dest) {
			spin_lock(&dest->lock);
			if (!dest->full) {
				u64 bytes_to_add;

				bytes_to_add = dest->size - dest->reserved;
				bytes_to_add = min(num_bytes, bytes_to_add);
				dest->reserved += bytes_to_add;
				if (dest->reserved >= dest->size)
					dest->full = 1;
				num_bytes -= bytes_to_add;
			}
			spin_unlock(&dest->lock);
		}
		if (num_bytes) {
			spin_lock(&space_info->lock);
			space_info->bytes_may_use -= num_bytes;
			trace_btrfs_space_reservation(fs_info, "space_info",
					space_info->flags, num_bytes, 0);
			space_info->reservation_progress++;
			spin_unlock(&space_info->lock);
		}
	}
}

static int block_rsv_migrate_bytes(struct btrfs_block_rsv *src,
				   struct btrfs_block_rsv *dst, u64 num_bytes)
{
	int ret;

	ret = block_rsv_use_bytes(src, num_bytes);
	if (ret)
		return ret;

	block_rsv_add_bytes(dst, num_bytes, 1);
	return 0;
}

void btrfs_init_block_rsv(struct btrfs_block_rsv *rsv, unsigned short type)
{
	memset(rsv, 0, sizeof(*rsv));
	spin_lock_init(&rsv->lock);
	rsv->type = type;
}

struct btrfs_block_rsv *btrfs_alloc_block_rsv(struct btrfs_root *root,
					      unsigned short type)
{
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_fs_info *fs_info = root->fs_info;

	block_rsv = kmalloc(sizeof(*block_rsv), GFP_NOFS);
	if (!block_rsv)
		return NULL;

	btrfs_init_block_rsv(block_rsv, type);
	block_rsv->space_info = __find_space_info(fs_info,
						  BTRFS_BLOCK_GROUP_METADATA);
	return block_rsv;
}

void btrfs_free_block_rsv(struct btrfs_root *root,
			  struct btrfs_block_rsv *rsv)
{
	if (!rsv)
		return;
	btrfs_block_rsv_release(root, rsv, (u64)-1);
	kfree(rsv);
}

int btrfs_block_rsv_add(struct btrfs_root *root,
			struct btrfs_block_rsv *block_rsv, u64 num_bytes,
			enum btrfs_reserve_flush_enum flush)
{
	int ret;

	if (num_bytes == 0)
		return 0;

	ret = reserve_metadata_bytes(root, block_rsv, num_bytes, flush);
	if (!ret) {
		block_rsv_add_bytes(block_rsv, num_bytes, 1);
		return 0;
	}

	return ret;
}

int btrfs_block_rsv_check(struct btrfs_root *root,
			  struct btrfs_block_rsv *block_rsv, int min_factor)
{
	u64 num_bytes = 0;
	int ret = -ENOSPC;

	if (!block_rsv)
		return 0;

	spin_lock(&block_rsv->lock);
	num_bytes = div_factor(block_rsv->size, min_factor);
	if (block_rsv->reserved >= num_bytes)
		ret = 0;
	spin_unlock(&block_rsv->lock);

	return ret;
}

int btrfs_block_rsv_refill(struct btrfs_root *root,
			   struct btrfs_block_rsv *block_rsv, u64 min_reserved,
			   enum btrfs_reserve_flush_enum flush)
{
	u64 num_bytes = 0;
	int ret = -ENOSPC;

	if (!block_rsv)
		return 0;

	spin_lock(&block_rsv->lock);
	num_bytes = min_reserved;
	if (block_rsv->reserved >= num_bytes)
		ret = 0;
	else
		num_bytes -= block_rsv->reserved;
	spin_unlock(&block_rsv->lock);

	if (!ret)
		return 0;

	ret = reserve_metadata_bytes(root, block_rsv, num_bytes, flush);
	if (!ret) {
		block_rsv_add_bytes(block_rsv, num_bytes, 0);
		return 0;
	}

	return ret;
}

int btrfs_block_rsv_migrate(struct btrfs_block_rsv *src_rsv,
			    struct btrfs_block_rsv *dst_rsv,
			    u64 num_bytes)
{
	return block_rsv_migrate_bytes(src_rsv, dst_rsv, num_bytes);
}

void btrfs_block_rsv_release(struct btrfs_root *root,
			     struct btrfs_block_rsv *block_rsv,
			     u64 num_bytes)
{
	struct btrfs_block_rsv *global_rsv = &root->fs_info->global_block_rsv;
	if (global_rsv->full || global_rsv == block_rsv ||
	    block_rsv->space_info != global_rsv->space_info)
		global_rsv = NULL;
	block_rsv_release_bytes(root->fs_info, block_rsv, global_rsv,
				num_bytes);
}

/*
 * helper to calculate size of global block reservation.
 * the desired value is sum of space used by extent tree,
 * checksum tree and root tree
 */
static u64 calc_global_metadata_size(struct btrfs_fs_info *fs_info)
{
	struct btrfs_space_info *sinfo;
	u64 num_bytes;
	u64 meta_used;
	u64 data_used;
	int csum_size = btrfs_super_csum_size(fs_info->super_copy);

	sinfo = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_DATA);
	spin_lock(&sinfo->lock);
	data_used = sinfo->bytes_used;
	spin_unlock(&sinfo->lock);

	sinfo = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);
	spin_lock(&sinfo->lock);
	if (sinfo->flags & BTRFS_BLOCK_GROUP_DATA)
		data_used = 0;
	meta_used = sinfo->bytes_used;
	spin_unlock(&sinfo->lock);

	num_bytes = (data_used >> fs_info->sb->s_blocksize_bits) *
		    csum_size * 2;
	num_bytes += div64_u64(data_used + meta_used, 50);

	if (num_bytes * 3 > meta_used)
		num_bytes = div64_u64(meta_used, 3);

	return ALIGN(num_bytes, fs_info->extent_root->leafsize << 10);
}

static void update_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;
	struct btrfs_space_info *sinfo = block_rsv->space_info;
	u64 num_bytes;

	num_bytes = calc_global_metadata_size(fs_info);

	spin_lock(&sinfo->lock);
	spin_lock(&block_rsv->lock);

	block_rsv->size = min_t(u64, num_bytes, 512 * 1024 * 1024);

	num_bytes = sinfo->bytes_used + sinfo->bytes_pinned +
		    sinfo->bytes_reserved + sinfo->bytes_readonly +
		    sinfo->bytes_may_use;

	if (sinfo->total_bytes > num_bytes) {
		num_bytes = sinfo->total_bytes - num_bytes;
		block_rsv->reserved += num_bytes;
		sinfo->bytes_may_use += num_bytes;
		trace_btrfs_space_reservation(fs_info, "space_info",
				      sinfo->flags, num_bytes, 1);
	}

	if (block_rsv->reserved >= block_rsv->size) {
		num_bytes = block_rsv->reserved - block_rsv->size;
		sinfo->bytes_may_use -= num_bytes;
		trace_btrfs_space_reservation(fs_info, "space_info",
				      sinfo->flags, num_bytes, 0);
		sinfo->reservation_progress++;
		block_rsv->reserved = block_rsv->size;
		block_rsv->full = 1;
	}

	spin_unlock(&block_rsv->lock);
	spin_unlock(&sinfo->lock);
}

static void init_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	struct btrfs_space_info *space_info;

	space_info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
	fs_info->chunk_block_rsv.space_info = space_info;

	space_info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);
	fs_info->global_block_rsv.space_info = space_info;
	fs_info->delalloc_block_rsv.space_info = space_info;
	fs_info->trans_block_rsv.space_info = space_info;
	fs_info->empty_block_rsv.space_info = space_info;
	fs_info->delayed_block_rsv.space_info = space_info;

	fs_info->extent_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->csum_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->dev_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->tree_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->chunk_root->block_rsv = &fs_info->chunk_block_rsv;

	update_global_block_rsv(fs_info);
}

static void release_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	block_rsv_release_bytes(fs_info, &fs_info->global_block_rsv, NULL,
				(u64)-1);
	WARN_ON(fs_info->delalloc_block_rsv.size > 0);
	WARN_ON(fs_info->delalloc_block_rsv.reserved > 0);
	WARN_ON(fs_info->trans_block_rsv.size > 0);
	WARN_ON(fs_info->trans_block_rsv.reserved > 0);
	WARN_ON(fs_info->chunk_block_rsv.size > 0);
	WARN_ON(fs_info->chunk_block_rsv.reserved > 0);
	WARN_ON(fs_info->delayed_block_rsv.size > 0);
	WARN_ON(fs_info->delayed_block_rsv.reserved > 0);
}

void btrfs_trans_release_metadata(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root)
{
	if (!trans->block_rsv)
		return;

	if (!trans->bytes_reserved)
		return;

	trace_btrfs_space_reservation(root->fs_info, "transaction",
				      trans->transid, trans->bytes_reserved, 0);
	btrfs_block_rsv_release(root, trans->block_rsv, trans->bytes_reserved);
	trans->bytes_reserved = 0;
}

/* Can only return 0 or -ENOSPC */
int btrfs_orphan_reserve_metadata(struct btrfs_trans_handle *trans,
				  struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *src_rsv = get_block_rsv(trans, root);
	struct btrfs_block_rsv *dst_rsv = root->orphan_block_rsv;

	/*
	 * We need to hold space in order to delete our orphan item once we've
	 * added it, so this takes the reservation so we can release it later
	 * when we are truly done with the orphan item.
	 */
	u64 num_bytes = btrfs_calc_trans_metadata_size(root, 1);
	trace_btrfs_space_reservation(root->fs_info, "orphan",
				      btrfs_ino(inode), num_bytes, 1);
	return block_rsv_migrate_bytes(src_rsv, dst_rsv, num_bytes);
}

void btrfs_orphan_release_metadata(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 num_bytes = btrfs_calc_trans_metadata_size(root, 1);
	trace_btrfs_space_reservation(root->fs_info, "orphan",
				      btrfs_ino(inode), num_bytes, 0);
	btrfs_block_rsv_release(root, root->orphan_block_rsv, num_bytes);
}

/*
 * btrfs_subvolume_reserve_metadata() - reserve space for subvolume operation
 * root: the root of the parent directory
 * rsv: block reservation
 * items: the number of items that we need do reservation
 * qgroup_reserved: used to return the reserved size in qgroup
 *
 * This function is used to reserve the space for snapshot/subvolume
 * creation and deletion. Those operations are different with the
 * common file/directory operations, they change two fs/file trees
 * and root tree, the number of items that the qgroup reserves is
 * different with the free space reservation. So we can not use
 * the space reseravtion mechanism in start_transaction().
 */
int btrfs_subvolume_reserve_metadata(struct btrfs_root *root,
				     struct btrfs_block_rsv *rsv,
				     int items,
				     u64 *qgroup_reserved)
{
	u64 num_bytes;
	int ret;

	if (root->fs_info->quota_enabled) {
		/* One for parent inode, two for dir entries */
		num_bytes = 3 * root->leafsize;
		ret = btrfs_qgroup_reserve(root, num_bytes);
		if (ret)
			return ret;
	} else {
		num_bytes = 0;
	}

	*qgroup_reserved = num_bytes;

	num_bytes = btrfs_calc_trans_metadata_size(root, items);
	rsv->space_info = __find_space_info(root->fs_info,
					    BTRFS_BLOCK_GROUP_METADATA);
	ret = btrfs_block_rsv_add(root, rsv, num_bytes,
				  BTRFS_RESERVE_FLUSH_ALL);
	if (ret) {
		if (*qgroup_reserved)
			btrfs_qgroup_free(root, *qgroup_reserved);
	}

	return ret;
}

void btrfs_subvolume_release_metadata(struct btrfs_root *root,
				      struct btrfs_block_rsv *rsv,
				      u64 qgroup_reserved)
{
	btrfs_block_rsv_release(root, rsv, (u64)-1);
	if (qgroup_reserved)
		btrfs_qgroup_free(root, qgroup_reserved);
}

/**
 * drop_outstanding_extent - drop an outstanding extent
 * @inode: the inode we're dropping the extent for
 *
 * This is called when we are freeing up an outstanding extent, either called
 * after an error or after an extent is written.  This will return the number of
 * reserved extents that need to be freed.  This must be called with
 * BTRFS_I(inode)->lock held.
 */
static unsigned drop_outstanding_extent(struct inode *inode)
{
	unsigned drop_inode_space = 0;
	unsigned dropped_extents = 0;

	BUG_ON(!BTRFS_I(inode)->outstanding_extents);
	BTRFS_I(inode)->outstanding_extents--;

	if (BTRFS_I(inode)->outstanding_extents == 0 &&
	    test_and_clear_bit(BTRFS_INODE_DELALLOC_META_RESERVED,
			       &BTRFS_I(inode)->runtime_flags))
		drop_inode_space = 1;

	/*
	 * If we have more or the same amount of outsanding extents than we have
	 * reserved then we need to leave the reserved extents count alone.
	 */
	if (BTRFS_I(inode)->outstanding_extents >=
	    BTRFS_I(inode)->reserved_extents)
		return drop_inode_space;

	dropped_extents = BTRFS_I(inode)->reserved_extents -
		BTRFS_I(inode)->outstanding_extents;
	BTRFS_I(inode)->reserved_extents -= dropped_extents;
	return dropped_extents + drop_inode_space;
}

/**
 * calc_csum_metadata_size - return the amount of metada space that must be
 *	reserved/free'd for the given bytes.
 * @inode: the inode we're manipulating
 * @num_bytes: the number of bytes in question
 * @reserve: 1 if we are reserving space, 0 if we are freeing space
 *
 * This adjusts the number of csum_bytes in the inode and then returns the
 * correct amount of metadata that must either be reserved or freed.  We
 * calculate how many checksums we can fit into one leaf and then divide the
 * number of bytes that will need to be checksumed by this value to figure out
 * how many checksums will be required.  If we are adding bytes then the number
 * may go up and we will return the number of additional bytes that must be
 * reserved.  If it is going down we will return the number of bytes that must
 * be freed.
 *
 * This must be called with BTRFS_I(inode)->lock held.
 */
static u64 calc_csum_metadata_size(struct inode *inode, u64 num_bytes,
				   int reserve)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 csum_size;
	int num_csums_per_leaf;
	int num_csums;
	int old_csums;

	if (BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM &&
	    BTRFS_I(inode)->csum_bytes == 0)
		return 0;

	old_csums = (int)div64_u64(BTRFS_I(inode)->csum_bytes, root->sectorsize);
	if (reserve)
		BTRFS_I(inode)->csum_bytes += num_bytes;
	else
		BTRFS_I(inode)->csum_bytes -= num_bytes;
	csum_size = BTRFS_LEAF_DATA_SIZE(root) - sizeof(struct btrfs_item);
	num_csums_per_leaf = (int)div64_u64(csum_size,
					    sizeof(struct btrfs_csum_item) +
					    sizeof(struct btrfs_disk_key));
	num_csums = (int)div64_u64(BTRFS_I(inode)->csum_bytes, root->sectorsize);
	num_csums = num_csums + num_csums_per_leaf - 1;
	num_csums = num_csums / num_csums_per_leaf;

	old_csums = old_csums + num_csums_per_leaf - 1;
	old_csums = old_csums / num_csums_per_leaf;

	/* No change, no need to reserve more */
	if (old_csums == num_csums)
		return 0;

	if (reserve)
		return btrfs_calc_trans_metadata_size(root,
						      num_csums - old_csums);

	return btrfs_calc_trans_metadata_size(root, old_csums - num_csums);
}

int btrfs_delalloc_reserve_metadata(struct inode *inode, u64 num_bytes)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *block_rsv = &root->fs_info->delalloc_block_rsv;
	u64 to_reserve = 0;
	u64 csum_bytes;
	unsigned nr_extents = 0;
	int extra_reserve = 0;
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_FLUSH_ALL;
	int ret = 0;
	bool delalloc_lock = true;
	u64 to_free = 0;
	unsigned dropped;

	/* If we are a free space inode we need to not flush since we will be in
	 * the middle of a transaction commit.  We also don't need the delalloc
	 * mutex since we won't race with anybody.  We need this mostly to make
	 * lockdep shut its filthy mouth.
	 */
	if (btrfs_is_free_space_inode(inode)) {
		flush = BTRFS_RESERVE_NO_FLUSH;
		delalloc_lock = false;
	}

	if (flush != BTRFS_RESERVE_NO_FLUSH &&
	    btrfs_transaction_in_commit(root->fs_info))
		schedule_timeout(1);

	if (delalloc_lock)
		mutex_lock(&BTRFS_I(inode)->delalloc_mutex);

	num_bytes = ALIGN(num_bytes, root->sectorsize);

	spin_lock(&BTRFS_I(inode)->lock);
	BTRFS_I(inode)->outstanding_extents++;

	if (BTRFS_I(inode)->outstanding_extents >
	    BTRFS_I(inode)->reserved_extents)
		nr_extents = BTRFS_I(inode)->outstanding_extents -
			BTRFS_I(inode)->reserved_extents;

	/*
	 * Add an item to reserve for updating the inode when we complete the
	 * delalloc io.
	 */
	if (!test_bit(BTRFS_INODE_DELALLOC_META_RESERVED,
		      &BTRFS_I(inode)->runtime_flags)) {
		nr_extents++;
		extra_reserve = 1;
	}

	to_reserve = btrfs_calc_trans_metadata_size(root, nr_extents);
	to_reserve += calc_csum_metadata_size(inode, num_bytes, 1);
	csum_bytes = BTRFS_I(inode)->csum_bytes;
	spin_unlock(&BTRFS_I(inode)->lock);

	if (root->fs_info->quota_enabled) {
		ret = btrfs_qgroup_reserve(root, num_bytes +
					   nr_extents * root->leafsize);
		if (ret)
			goto out_fail;
	}

	ret = reserve_metadata_bytes(root, block_rsv, to_reserve, flush);
	if (unlikely(ret)) {
		if (root->fs_info->quota_enabled)
			btrfs_qgroup_free(root, num_bytes +
						nr_extents * root->leafsize);
		goto out_fail;
	}

	spin_lock(&BTRFS_I(inode)->lock);
	if (extra_reserve) {
		set_bit(BTRFS_INODE_DELALLOC_META_RESERVED,
			&BTRFS_I(inode)->runtime_flags);
		nr_extents--;
	}
	BTRFS_I(inode)->reserved_extents += nr_extents;
	spin_unlock(&BTRFS_I(inode)->lock);

	if (delalloc_lock)
		mutex_unlock(&BTRFS_I(inode)->delalloc_mutex);

	if (to_reserve)
		trace_btrfs_space_reservation(root->fs_info,"delalloc",
					      btrfs_ino(inode), to_reserve, 1);
	block_rsv_add_bytes(block_rsv, to_reserve, 1);

	return 0;

out_fail:
	spin_lock(&BTRFS_I(inode)->lock);
	dropped = drop_outstanding_extent(inode);
	/*
	 * If the inodes csum_bytes is the same as the original
	 * csum_bytes then we know we haven't raced with any free()ers
	 * so we can just reduce our inodes csum bytes and carry on.
	 */
	if (BTRFS_I(inode)->csum_bytes == csum_bytes) {
		calc_csum_metadata_size(inode, num_bytes, 0);
	} else {
		u64 orig_csum_bytes = BTRFS_I(inode)->csum_bytes;
		u64 bytes;

		/*
		 * This is tricky, but first we need to figure out how much we
		 * free'd from any free-ers that occured during this
		 * reservation, so we reset ->csum_bytes to the csum_bytes
		 * before we dropped our lock, and then call the free for the
		 * number of bytes that were freed while we were trying our
		 * reservation.
		 */
		bytes = csum_bytes - BTRFS_I(inode)->csum_bytes;
		BTRFS_I(inode)->csum_bytes = csum_bytes;
		to_free = calc_csum_metadata_size(inode, bytes, 0);


		/*
		 * Now we need to see how much we would have freed had we not
		 * been making this reservation and our ->csum_bytes were not
		 * artificially inflated.
		 */
		BTRFS_I(inode)->csum_bytes = csum_bytes - num_bytes;
		bytes = csum_bytes - orig_csum_bytes;
		bytes = calc_csum_metadata_size(inode, bytes, 0);

		/*
		 * Now reset ->csum_bytes to what it should be.  If bytes is
		 * more than to_free then we would have free'd more space had we
		 * not had an artificially high ->csum_bytes, so we need to free
		 * the remainder.  If bytes is the same or less then we don't
		 * need to do anything, the other free-ers did the correct
		 * thing.
		 */
		BTRFS_I(inode)->csum_bytes = orig_csum_bytes - num_bytes;
		if (bytes > to_free)
			to_free = bytes - to_free;
		else
			to_free = 0;
	}
	spin_unlock(&BTRFS_I(inode)->lock);
	if (dropped)
		to_free += btrfs_calc_trans_metadata_size(root, dropped);

	if (to_free) {
		btrfs_block_rsv_release(root, block_rsv, to_free);
		trace_btrfs_space_reservation(root->fs_info, "delalloc",
					      btrfs_ino(inode), to_free, 0);
	}
	if (delalloc_lock)
		mutex_unlock(&BTRFS_I(inode)->delalloc_mutex);
	return ret;
}

/**
 * btrfs_delalloc_release_metadata - release a metadata reservation for an inode
 * @inode: the inode to release the reservation for
 * @num_bytes: the number of bytes we're releasing
 *
 * This will release the metadata reservation for an inode.  This can be called
 * once we complete IO for a given set of bytes to release their metadata
 * reservations.
 */
void btrfs_delalloc_release_metadata(struct inode *inode, u64 num_bytes)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 to_free = 0;
	unsigned dropped;

	num_bytes = ALIGN(num_bytes, root->sectorsize);
	spin_lock(&BTRFS_I(inode)->lock);
	dropped = drop_outstanding_extent(inode);

	if (num_bytes)
		to_free = calc_csum_metadata_size(inode, num_bytes, 0);
	spin_unlock(&BTRFS_I(inode)->lock);
	if (dropped > 0)
		to_free += btrfs_calc_trans_metadata_size(root, dropped);

	trace_btrfs_space_reservation(root->fs_info, "delalloc",
				      btrfs_ino(inode), to_free, 0);
	if (root->fs_info->quota_enabled) {
		btrfs_qgroup_free(root, num_bytes +
					dropped * root->leafsize);
	}

	btrfs_block_rsv_release(root, &root->fs_info->delalloc_block_rsv,
				to_free);
}

/**
 * btrfs_delalloc_reserve_space - reserve data and metadata space for delalloc
 * @inode: inode we're writing to
 * @num_bytes: the number of bytes we want to allocate
 *
 * This will do the following things
 *
 * o reserve space in the data space info for num_bytes
 * o reserve space in the metadata space info based on number of outstanding
 *   extents and how much csums will be needed
 * o add to the inodes ->delalloc_bytes
 * o add it to the fs_info's delalloc inodes list.
 *
 * This will return 0 for success and -ENOSPC if there is no space left.
 */
int btrfs_delalloc_reserve_space(struct inode *inode, u64 num_bytes)
{
	int ret;

	ret = btrfs_check_data_free_space(inode, num_bytes);
	if (ret)
		return ret;

	ret = btrfs_delalloc_reserve_metadata(inode, num_bytes);
	if (ret) {
		btrfs_free_reserved_data_space(inode, num_bytes);
		return ret;
	}

	return 0;
}

/**
 * btrfs_delalloc_release_space - release data and metadata space for delalloc
 * @inode: inode we're releasing space for
 * @num_bytes: the number of bytes we want to free up
 *
 * This must be matched with a call to btrfs_delalloc_reserve_space.  This is
 * called in the case that we don't need the metadata AND data reservations
 * anymore.  So if there is an error or we insert an inline extent.
 *
 * This function will release the metadata space that was not used and will
 * decrement ->delalloc_bytes and remove it from the fs_info delalloc_inodes
 * list if there are no delalloc bytes left.
 */
void btrfs_delalloc_release_space(struct inode *inode, u64 num_bytes)
{
	btrfs_delalloc_release_metadata(inode, num_bytes);
	btrfs_free_reserved_data_space(inode, num_bytes);
}

static int update_block_group(struct btrfs_root *root,
			      u64 bytenr, u64 num_bytes, int alloc)
{
	struct btrfs_block_group_cache *cache = NULL;
	struct btrfs_fs_info *info = root->fs_info;
	u64 total = num_bytes;
	u64 old_val;
	u64 byte_in_group;
	int factor;

	/* block accounting for super block */
	spin_lock(&info->delalloc_lock);
	old_val = btrfs_super_bytes_used(info->super_copy);
	if (alloc)
		old_val += num_bytes;
	else
		old_val -= num_bytes;
	btrfs_set_super_bytes_used(info->super_copy, old_val);
	spin_unlock(&info->delalloc_lock);

	while (total) {
		cache = btrfs_lookup_block_group(info, bytenr);
		if (!cache)
			return -ENOENT;
		if (cache->flags & (BTRFS_BLOCK_GROUP_DUP |
				    BTRFS_BLOCK_GROUP_RAID1 |
				    BTRFS_BLOCK_GROUP_RAID10))
			factor = 2;
		else
			factor = 1;
		/*
		 * If this block group has free space cache written out, we
		 * need to make sure to load it if we are removing space.  This
		 * is because we need the unpinning stage to actually add the
		 * space back to the block group, otherwise we will leak space.
		 */
		if (!alloc && cache->cached == BTRFS_CACHE_NO)
			cache_block_group(cache, 1);

		byte_in_group = bytenr - cache->key.objectid;
		WARN_ON(byte_in_group > cache->key.offset);

		spin_lock(&cache->space_info->lock);
		spin_lock(&cache->lock);

		if (btrfs_test_opt(root, SPACE_CACHE) &&
		    cache->disk_cache_state < BTRFS_DC_CLEAR)
			cache->disk_cache_state = BTRFS_DC_CLEAR;

		cache->dirty = 1;
		old_val = btrfs_block_group_used(&cache->item);
		num_bytes = min(total, cache->key.offset - byte_in_group);
		if (alloc) {
			old_val += num_bytes;
			btrfs_set_block_group_used(&cache->item, old_val);
			cache->reserved -= num_bytes;
			cache->space_info->bytes_reserved -= num_bytes;
			cache->space_info->bytes_used += num_bytes;
			cache->space_info->disk_used += num_bytes * factor;
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);
		} else {
			old_val -= num_bytes;
			btrfs_set_block_group_used(&cache->item, old_val);
			cache->pinned += num_bytes;
			cache->space_info->bytes_pinned += num_bytes;
			cache->space_info->bytes_used -= num_bytes;
			cache->space_info->disk_used -= num_bytes * factor;
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);

			set_extent_dirty(info->pinned_extents,
					 bytenr, bytenr + num_bytes - 1,
					 GFP_NOFS | __GFP_NOFAIL);
		}
		btrfs_put_block_group(cache);
		total -= num_bytes;
		bytenr += num_bytes;
	}
	return 0;
}

static u64 first_logical_byte(struct btrfs_root *root, u64 search_start)
{
	struct btrfs_block_group_cache *cache;
	u64 bytenr;

	spin_lock(&root->fs_info->block_group_cache_lock);
	bytenr = root->fs_info->first_logical_byte;
	spin_unlock(&root->fs_info->block_group_cache_lock);

	if (bytenr < (u64)-1)
		return bytenr;

	cache = btrfs_lookup_first_block_group(root->fs_info, search_start);
	if (!cache)
		return 0;

	bytenr = cache->key.objectid;
	btrfs_put_block_group(cache);

	return bytenr;
}

static int pin_down_extent(struct btrfs_root *root,
			   struct btrfs_block_group_cache *cache,
			   u64 bytenr, u64 num_bytes, int reserved)
{
	spin_lock(&cache->space_info->lock);
	spin_lock(&cache->lock);
	cache->pinned += num_bytes;
	cache->space_info->bytes_pinned += num_bytes;
	if (reserved) {
		cache->reserved -= num_bytes;
		cache->space_info->bytes_reserved -= num_bytes;
	}
	spin_unlock(&cache->lock);
	spin_unlock(&cache->space_info->lock);

	set_extent_dirty(root->fs_info->pinned_extents, bytenr,
			 bytenr + num_bytes - 1, GFP_NOFS | __GFP_NOFAIL);
	return 0;
}

/*
 * this function must be called within transaction
 */
int btrfs_pin_extent(struct btrfs_root *root,
		     u64 bytenr, u64 num_bytes, int reserved)
{
	struct btrfs_block_group_cache *cache;

	cache = btrfs_lookup_block_group(root->fs_info, bytenr);
	BUG_ON(!cache); /* Logic error */

	pin_down_extent(root, cache, bytenr, num_bytes, reserved);

	btrfs_put_block_group(cache);
	return 0;
}

/*
 * this function must be called within transaction
 */
int btrfs_pin_extent_for_log_replay(struct btrfs_root *root,
				    u64 bytenr, u64 num_bytes)
{
	struct btrfs_block_group_cache *cache;

	cache = btrfs_lookup_block_group(root->fs_info, bytenr);
	BUG_ON(!cache); /* Logic error */

	/*
	 * pull in the free space cache (if any) so that our pin
	 * removes the free space from the cache.  We have load_only set
	 * to one because the slow code to read in the free extents does check
	 * the pinned extents.
	 */
	cache_block_group(cache, 1);

	pin_down_extent(root, cache, bytenr, num_bytes, 0);

	/* remove us from the free space cache (if we're there at all) */
	btrfs_remove_free_space(cache, bytenr, num_bytes);
	btrfs_put_block_group(cache);
	return 0;
}

/**
 * btrfs_update_reserved_bytes - update the block_group and space info counters
 * @cache:	The cache we are manipulating
 * @num_bytes:	The number of bytes in question
 * @reserve:	One of the reservation enums
 *
 * This is called by the allocator when it reserves space, or by somebody who is
 * freeing space that was never actually used on disk.  For example if you
 * reserve some space for a new leaf in transaction A and before transaction A
 * commits you free that leaf, you call this with reserve set to 0 in order to
 * clear the reservation.
 *
 * Metadata reservations should be called with RESERVE_ALLOC so we do the proper
 * ENOSPC accounting.  For data we handle the reservation through clearing the
 * delalloc bits in the io_tree.  We have to do this since we could end up
 * allocating less disk space for the amount of data we have reserved in the
 * case of compression.
 *
 * If this is a reservation and the block group has become read only we cannot
 * make the reservation and return -EAGAIN, otherwise this function always
 * succeeds.
 */
static int btrfs_update_reserved_bytes(struct btrfs_block_group_cache *cache,
				       u64 num_bytes, int reserve)
{
	struct btrfs_space_info *space_info = cache->space_info;
	int ret = 0;

	spin_lock(&space_info->lock);
	spin_lock(&cache->lock);
	if (reserve != RESERVE_FREE) {
		if (cache->ro) {
			ret = -EAGAIN;
		} else {
			cache->reserved += num_bytes;
			space_info->bytes_reserved += num_bytes;
			if (reserve == RESERVE_ALLOC) {
				trace_btrfs_space_reservation(cache->fs_info,
						"space_info", space_info->flags,
						num_bytes, 0);
				space_info->bytes_may_use -= num_bytes;
			}
		}
	} else {
		if (cache->ro)
			space_info->bytes_readonly += num_bytes;
		cache->reserved -= num_bytes;
		space_info->bytes_reserved -= num_bytes;
		space_info->reservation_progress++;
	}
	spin_unlock(&cache->lock);
	spin_unlock(&space_info->lock);
	return ret;
}

void btrfs_prepare_extent_commit(struct btrfs_trans_handle *trans,
				struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_caching_control *next;
	struct btrfs_caching_control *caching_ctl;
	struct btrfs_block_group_cache *cache;

	down_write(&fs_info->extent_commit_sem);

	list_for_each_entry_safe(caching_ctl, next,
				 &fs_info->caching_block_groups, list) {
		cache = caching_ctl->block_group;
		if (block_group_cache_done(cache)) {
			cache->last_byte_to_unpin = (u64)-1;
			list_del_init(&caching_ctl->list);
			put_caching_control(caching_ctl);
		} else {
			cache->last_byte_to_unpin = caching_ctl->progress;
		}
	}

	if (fs_info->pinned_extents == &fs_info->freed_extents[0])
		fs_info->pinned_extents = &fs_info->freed_extents[1];
	else
		fs_info->pinned_extents = &fs_info->freed_extents[0];

	up_write(&fs_info->extent_commit_sem);

	update_global_block_rsv(fs_info);
}

static int unpin_extent_range(struct btrfs_root *root, u64 start, u64 end)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_group_cache *cache = NULL;
	struct btrfs_space_info *space_info;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 len;
	bool readonly;

	while (start <= end) {
		readonly = false;
		if (!cache ||
		    start >= cache->key.objectid + cache->key.offset) {
			if (cache)
				btrfs_put_block_group(cache);
			cache = btrfs_lookup_block_group(fs_info, start);
			BUG_ON(!cache); /* Logic error */
		}

		len = cache->key.objectid + cache->key.offset - start;
		len = min(len, end + 1 - start);

		if (start < cache->last_byte_to_unpin) {
			len = min(len, cache->last_byte_to_unpin - start);
			btrfs_add_free_space(cache, start, len);
		}

		start += len;
		space_info = cache->space_info;

		spin_lock(&space_info->lock);
		spin_lock(&cache->lock);
		cache->pinned -= len;
		space_info->bytes_pinned -= len;
		if (cache->ro) {
			space_info->bytes_readonly += len;
			readonly = true;
		}
		spin_unlock(&cache->lock);
		if (!readonly && global_rsv->space_info == space_info) {
			spin_lock(&global_rsv->lock);
			if (!global_rsv->full) {
				len = min(len, global_rsv->size -
					  global_rsv->reserved);
				global_rsv->reserved += len;
				space_info->bytes_may_use += len;
				if (global_rsv->reserved >= global_rsv->size)
					global_rsv->full = 1;
			}
			spin_unlock(&global_rsv->lock);
		}
		spin_unlock(&space_info->lock);
	}

	if (cache)
		btrfs_put_block_group(cache);
	return 0;
}

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_io_tree *unpin;
	u64 start;
	u64 end;
	int ret;

	if (trans->aborted)
		return 0;

	if (fs_info->pinned_extents == &fs_info->freed_extents[0])
		unpin = &fs_info->freed_extents[1];
	else
		unpin = &fs_info->freed_extents[0];

	while (1) {
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY, NULL);
		if (ret)
			break;

		if (btrfs_test_opt(root, DISCARD))
			ret = btrfs_discard_extent(root, start,
						   end + 1 - start, NULL);

		clear_extent_dirty(unpin, start, end, GFP_NOFS);
		unpin_extent_range(root, start, end);
		cond_resched();
	}

	return 0;
}

static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes, u64 parent,
				u64 root_objectid, u64 owner_objectid,
				u64 owner_offset, int refs_to_drop,
				struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_root *extent_root = info->extent_root;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *ei;
	struct btrfs_extent_inline_ref *iref;
	int ret;
	int is_data;
	int extent_slot = 0;
	int found_extent = 0;
	int num_to_del = 1;
	u32 item_size;
	u64 refs;
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;
	path->leave_spinning = 1;

	is_data = owner_objectid >= BTRFS_FIRST_FREE_OBJECTID;
	BUG_ON(!is_data && refs_to_drop != 1);

	if (is_data)
		skinny_metadata = 0;

	ret = lookup_extent_backref(trans, extent_root, path, &iref,
				    bytenr, num_bytes, parent,
				    root_objectid, owner_objectid,
				    owner_offset);
	if (ret == 0) {
		extent_slot = path->slots[0];
		while (extent_slot >= 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      extent_slot);
			if (key.objectid != bytenr)
				break;
			if (key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == num_bytes) {
				found_extent = 1;
				break;
			}
			if (key.type == BTRFS_METADATA_ITEM_KEY &&
			    key.offset == owner_objectid) {
				found_extent = 1;
				break;
			}
			if (path->slots[0] - extent_slot > 5)
				break;
			extent_slot--;
		}
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
		item_size = btrfs_item_size_nr(path->nodes[0], extent_slot);
		if (found_extent && item_size < sizeof(*ei))
			found_extent = 0;
#endif
		if (!found_extent) {
			BUG_ON(iref);
			ret = remove_extent_backref(trans, extent_root, path,
						    NULL, refs_to_drop,
						    is_data);
			if (ret) {
				btrfs_abort_transaction(trans, extent_root, ret);
				goto out;
			}
			btrfs_release_path(path);
			path->leave_spinning = 1;

			key.objectid = bytenr;
			key.type = BTRFS_EXTENT_ITEM_KEY;
			key.offset = num_bytes;

			if (!is_data && skinny_metadata) {
				key.type = BTRFS_METADATA_ITEM_KEY;
				key.offset = owner_objectid;
			}

			ret = btrfs_search_slot(trans, extent_root,
						&key, path, -1, 1);
			if (ret > 0 && skinny_metadata && path->slots[0]) {
				/*
				 * Couldn't find our skinny metadata item,
				 * see if we have ye olde extent item.
				 */
				path->slots[0]--;
				btrfs_item_key_to_cpu(path->nodes[0], &key,
						      path->slots[0]);
				if (key.objectid == bytenr &&
				    key.type == BTRFS_EXTENT_ITEM_KEY &&
				    key.offset == num_bytes)
					ret = 0;
			}

			if (ret > 0 && skinny_metadata) {
				skinny_metadata = false;
				key.type = BTRFS_EXTENT_ITEM_KEY;
				key.offset = num_bytes;
				btrfs_release_path(path);
				ret = btrfs_search_slot(trans, extent_root,
							&key, path, -1, 1);
			}

			if (ret) {
				btrfs_err(info, "umm, got %d back from search, was looking for %llu",
					ret, (unsigned long long)bytenr);
				if (ret > 0)
					btrfs_print_leaf(extent_root,
							 path->nodes[0]);
			}
			if (ret < 0) {
				btrfs_abort_transaction(trans, extent_root, ret);
				goto out;
			}
			extent_slot = path->slots[0];
		}
	} else if (ret == -ENOENT) {
		btrfs_print_leaf(extent_root, path->nodes[0]);
		WARN_ON(1);
		btrfs_err(info,
			"unable to find ref byte nr %llu parent %llu root %llu  owner %llu offset %llu",
			(unsigned long long)bytenr,
			(unsigned long long)parent,
			(unsigned long long)root_objectid,
			(unsigned long long)owner_objectid,
			(unsigned long long)owner_offset);
	} else {
		btrfs_abort_transaction(trans, extent_root, ret);
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, extent_slot);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		BUG_ON(found_extent || extent_slot != path->slots[0]);
		ret = convert_extent_item_v0(trans, extent_root, path,
					     owner_objectid, 0);
		if (ret < 0) {
			btrfs_abort_transaction(trans, extent_root, ret);
			goto out;
		}

		btrfs_release_path(path);
		path->leave_spinning = 1;

		key.objectid = bytenr;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = num_bytes;

		ret = btrfs_search_slot(trans, extent_root, &key, path,
					-1, 1);
		if (ret) {
			btrfs_err(info, "umm, got %d back from search, was looking for %llu",
				ret, (unsigned long long)bytenr);
			btrfs_print_leaf(extent_root, path->nodes[0]);
		}
		if (ret < 0) {
			btrfs_abort_transaction(trans, extent_root, ret);
			goto out;
		}

		extent_slot = path->slots[0];
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, extent_slot);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));
	ei = btrfs_item_ptr(leaf, extent_slot,
			    struct btrfs_extent_item);
	if (owner_objectid < BTRFS_FIRST_FREE_OBJECTID &&
	    key.type == BTRFS_EXTENT_ITEM_KEY) {
		struct btrfs_tree_block_info *bi;
		BUG_ON(item_size < sizeof(*ei) + sizeof(*bi));
		bi = (struct btrfs_tree_block_info *)(ei + 1);
		WARN_ON(owner_objectid != btrfs_tree_block_level(leaf, bi));
	}

	refs = btrfs_extent_refs(leaf, ei);
	BUG_ON(refs < refs_to_drop);
	refs -= refs_to_drop;

	if (refs > 0) {
		if (extent_op)
			__run_delayed_extent_op(extent_op, leaf, ei);
		/*
		 * In the case of inline back ref, reference count will
		 * be updated by remove_extent_backref
		 */
		if (iref) {
			BUG_ON(!found_extent);
		} else {
			btrfs_set_extent_refs(leaf, ei, refs);
			btrfs_mark_buffer_dirty(leaf);
		}
		if (found_extent) {
			ret = remove_extent_backref(trans, extent_root, path,
						    iref, refs_to_drop,
						    is_data);
			if (ret) {
				btrfs_abort_transaction(trans, extent_root, ret);
				goto out;
			}
		}
	} else {
		if (found_extent) {
			BUG_ON(is_data && refs_to_drop !=
			       extent_data_ref_count(root, path, iref));
			if (iref) {
				BUG_ON(path->slots[0] != extent_slot);
			} else {
				BUG_ON(path->slots[0] != extent_slot + 1);
				path->slots[0] = extent_slot;
				num_to_del = 2;
			}
		}

		ret = btrfs_del_items(trans, extent_root, path, path->slots[0],
				      num_to_del);
		if (ret) {
			btrfs_abort_transaction(trans, extent_root, ret);
			goto out;
		}
		btrfs_release_path(path);

		if (is_data) {
			ret = btrfs_del_csums(trans, root, bytenr, num_bytes);
			if (ret) {
				btrfs_abort_transaction(trans, extent_root, ret);
				goto out;
			}
		}

		ret = update_block_group(root, bytenr, num_bytes, 0);
		if (ret) {
			btrfs_abort_transaction(trans, extent_root, ret);
			goto out;
		}
	}
out:
	btrfs_free_path(path);
	return ret;
}

/*
 * when we free an block, it is possible (and likely) that we free the last
 * delayed ref for that extent as well.  This searches the delayed ref tree for
 * a given extent, and if there are no other delayed refs to be processed, it
 * removes it from the tree.
 */
static noinline int check_ref_cleanup(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root, u64 bytenr)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct rb_node *node;
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(trans, bytenr);
	if (!head)
		goto out;

	node = rb_prev(&head->node.rb_node);
	if (!node)
		goto out;

	ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);

	/* there are still entries for this ref, we can't drop it */
	if (ref->bytenr == bytenr)
		goto out;

	if (head->extent_op) {
		if (!head->must_insert_reserved)
			goto out;
		btrfs_free_delayed_extent_op(head->extent_op);
		head->extent_op = NULL;
	}

	/*
	 * waiting for the lock here would deadlock.  If someone else has it
	 * locked they are already in the process of dropping it anyway
	 */
	if (!mutex_trylock(&head->mutex))
		goto out;

	/*
	 * at this point we have a head with no other entries.  Go
	 * ahead and process it.
	 */
	head->node.in_tree = 0;
	rb_erase(&head->node.rb_node, &delayed_refs->root);

	delayed_refs->num_entries--;

	/*
	 * we don't take a ref on the node because we're removing it from the
	 * tree, so we just steal the ref the tree was holding.
	 */
	delayed_refs->num_heads--;
	if (list_empty(&head->cluster))
		delayed_refs->num_heads_ready--;

	list_del_init(&head->cluster);
	spin_unlock(&delayed_refs->lock);

	BUG_ON(head->extent_op);
	if (head->must_insert_reserved)
		ret = 1;

	mutex_unlock(&head->mutex);
	btrfs_put_delayed_ref(&head->node);
	return ret;
out:
	spin_unlock(&delayed_refs->lock);
	return 0;
}

void btrfs_free_tree_block(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct extent_buffer *buf,
			   u64 parent, int last_ref)
{
	struct btrfs_block_group_cache *cache = NULL;
	int ret;

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(root->fs_info, trans,
					buf->start, buf->len,
					parent, root->root_key.objectid,
					btrfs_header_level(buf),
					BTRFS_DROP_DELAYED_REF, NULL, 0);
		BUG_ON(ret); /* -ENOMEM */
	}

	if (!last_ref)
		return;

	cache = btrfs_lookup_block_group(root->fs_info, buf->start);

	if (btrfs_header_generation(buf) == trans->transid) {
		if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
			ret = check_ref_cleanup(trans, root, buf->start);
			if (!ret)
				goto out;
		}

		if (btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
			pin_down_extent(root, cache, buf->start, buf->len, 1);
			goto out;
		}

		WARN_ON(test_bit(EXTENT_BUFFER_DIRTY, &buf->bflags));

		btrfs_add_free_space(cache, buf->start, buf->len);
		btrfs_update_reserved_bytes(cache, buf->len, RESERVE_FREE);
	}
out:
	/*
	 * Deleting the buffer, clear the corrupt flag since it doesn't matter
	 * anymore.
	 */
	clear_bit(EXTENT_BUFFER_CORRUPT, &buf->bflags);
	btrfs_put_block_group(cache);
}

/* Can return -ENOMEM */
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      u64 bytenr, u64 num_bytes, u64 parent, u64 root_objectid,
		      u64 owner, u64 offset, int for_cow)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;

	/*
	 * tree log blocks never actually go into the extent allocation
	 * tree, just update pinning info and exit early.
	 */
	if (root_objectid == BTRFS_TREE_LOG_OBJECTID) {
		WARN_ON(owner >= BTRFS_FIRST_FREE_OBJECTID);
		/* unlocks the pinned mutex */
		btrfs_pin_extent(root, bytenr, num_bytes, 1);
		ret = 0;
	} else if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(fs_info, trans, bytenr,
					num_bytes,
					parent, root_objectid, (int)owner,
					BTRFS_DROP_DELAYED_REF, NULL, for_cow);
	} else {
		ret = btrfs_add_delayed_data_ref(fs_info, trans, bytenr,
						num_bytes,
						parent, root_objectid, owner,
						offset, BTRFS_DROP_DELAYED_REF,
						NULL, for_cow);
	}
	return ret;
}

static u64 stripe_align(struct btrfs_root *root,
			struct btrfs_block_group_cache *cache,
			u64 val, u64 num_bytes)
{
	u64 ret = ALIGN(val, root->stripesize);
	return ret;
}

/*
 * when we wait for progress in the block group caching, its because
 * our allocation attempt failed at least once.  So, we must sleep
 * and let some progress happen before we try again.
 *
 * This function will sleep at least once waiting for new free space to
 * show up, and then it will check the block group free space numbers
 * for our min num_bytes.  Another option is to have it go ahead
 * and look in the rbtree for a free extent of a given size, but this
 * is a good start.
 */
static noinline int
wait_block_group_cache_progress(struct btrfs_block_group_cache *cache,
				u64 num_bytes)
{
	struct btrfs_caching_control *caching_ctl;

	caching_ctl = get_caching_control(cache);
	if (!caching_ctl)
		return 0;

	wait_event(caching_ctl->wait, block_group_cache_done(cache) ||
		   (cache->free_space_ctl->free_space >= num_bytes));

	put_caching_control(caching_ctl);
	return 0;
}

static noinline int
wait_block_group_cache_done(struct btrfs_block_group_cache *cache)
{
	struct btrfs_caching_control *caching_ctl;

	caching_ctl = get_caching_control(cache);
	if (!caching_ctl)
		return 0;

	wait_event(caching_ctl->wait, block_group_cache_done(cache));

	put_caching_control(caching_ctl);
	return 0;
}

int __get_raid_index(u64 flags)
{
	if (flags & BTRFS_BLOCK_GROUP_RAID10)
		return BTRFS_RAID_RAID10;
	else if (flags & BTRFS_BLOCK_GROUP_RAID1)
		return BTRFS_RAID_RAID1;
	else if (flags & BTRFS_BLOCK_GROUP_DUP)
		return BTRFS_RAID_DUP;
	else if (flags & BTRFS_BLOCK_GROUP_RAID0)
		return BTRFS_RAID_RAID0;
	else if (flags & BTRFS_BLOCK_GROUP_RAID5)
		return BTRFS_RAID_RAID5;
	else if (flags & BTRFS_BLOCK_GROUP_RAID6)
		return BTRFS_RAID_RAID6;

	return BTRFS_RAID_SINGLE; /* BTRFS_BLOCK_GROUP_SINGLE */
}

static int get_block_group_index(struct btrfs_block_group_cache *cache)
{
	return __get_raid_index(cache->flags);
}

enum btrfs_loop_type {
	LOOP_CACHING_NOWAIT = 0,
	LOOP_CACHING_WAIT = 1,
	LOOP_ALLOC_CHUNK = 2,
	LOOP_NO_EMPTY_SIZE = 3,
};

/*
 * walks the btree of allocated extents and find a hole of a given size.
 * The key ins is changed to record the hole:
 * ins->objectid == block start
 * ins->flags = BTRFS_EXTENT_ITEM_KEY
 * ins->offset == number of blocks
 * Any available blocks before search_start are skipped.
 */
static noinline int find_free_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *orig_root,
				     u64 num_bytes, u64 empty_size,
				     u64 hint_byte, struct btrfs_key *ins,
				     u64 data)
{
	int ret = 0;
	struct btrfs_root *root = orig_root->fs_info->extent_root;
	struct btrfs_free_cluster *last_ptr = NULL;
	struct btrfs_block_group_cache *block_group = NULL;
	struct btrfs_block_group_cache *used_block_group;
	u64 search_start = 0;
	int empty_cluster = 2 * 1024 * 1024;
	struct btrfs_space_info *space_info;
	int loop = 0;
	int index = __get_raid_index(data);
	int alloc_type = (data & BTRFS_BLOCK_GROUP_DATA) ?
		RESERVE_ALLOC_NO_ACCOUNT : RESERVE_ALLOC;
	bool found_uncached_bg = false;
	bool failed_cluster_refill = false;
	bool failed_alloc = false;
	bool use_cluster = true;
	bool have_caching_bg = false;

	WARN_ON(num_bytes < root->sectorsize);
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);
	ins->objectid = 0;
	ins->offset = 0;

	trace_find_free_extent(orig_root, num_bytes, empty_size, data);

	space_info = __find_space_info(root->fs_info, data);
	if (!space_info) {
		btrfs_err(root->fs_info, "No space info for %llu", data);
		return -ENOSPC;
	}

	/*
	 * If the space info is for both data and metadata it means we have a
	 * small filesystem and we can't use the clustering stuff.
	 */
	if (btrfs_mixed_space_info(space_info))
		use_cluster = false;

	if (data & BTRFS_BLOCK_GROUP_METADATA && use_cluster) {
		last_ptr = &root->fs_info->meta_alloc_cluster;
		if (!btrfs_test_opt(root, SSD))
			empty_cluster = 64 * 1024;
	}

	if ((data & BTRFS_BLOCK_GROUP_DATA) && use_cluster &&
	    btrfs_test_opt(root, SSD)) {
		last_ptr = &root->fs_info->data_alloc_cluster;
	}

	if (last_ptr) {
		spin_lock(&last_ptr->lock);
		if (last_ptr->block_group)
			hint_byte = last_ptr->window_start;
		spin_unlock(&last_ptr->lock);
	}

	search_start = max(search_start, first_logical_byte(root, 0));
	search_start = max(search_start, hint_byte);

	if (!last_ptr)
		empty_cluster = 0;

	if (search_start == hint_byte) {
		block_group = btrfs_lookup_block_group(root->fs_info,
						       search_start);
		used_block_group = block_group;
		/*
		 * we don't want to use the block group if it doesn't match our
		 * allocation bits, or if its not cached.
		 *
		 * However if we are re-searching with an ideal block group
		 * picked out then we don't care that the block group is cached.
		 */
		if (block_group && block_group_bits(block_group, data) &&
		    block_group->cached != BTRFS_CACHE_NO) {
			down_read(&space_info->groups_sem);
			if (list_empty(&block_group->list) ||
			    block_group->ro) {
				/*
				 * someone is removing this block group,
				 * we can't jump into the have_block_group
				 * target because our list pointers are not
				 * valid
				 */
				btrfs_put_block_group(block_group);
				up_read(&space_info->groups_sem);
			} else {
				index = get_block_group_index(block_group);
				goto have_block_group;
			}
		} else if (block_group) {
			btrfs_put_block_group(block_group);
		}
	}
search:
	have_caching_bg = false;
	down_read(&space_info->groups_sem);
	list_for_each_entry(block_group, &space_info->block_groups[index],
			    list) {
		u64 offset;
		int cached;

		used_block_group = block_group;
		btrfs_get_block_group(block_group);
		search_start = block_group->key.objectid;

		/*
		 * this can happen if we end up cycling through all the
		 * raid types, but we want to make sure we only allocate
		 * for the proper type.
		 */
		if (!block_group_bits(block_group, data)) {
		    u64 extra = BTRFS_BLOCK_GROUP_DUP |
				BTRFS_BLOCK_GROUP_RAID1 |
				BTRFS_BLOCK_GROUP_RAID5 |
				BTRFS_BLOCK_GROUP_RAID6 |
				BTRFS_BLOCK_GROUP_RAID10;

			/*
			 * if they asked for extra copies and this block group
			 * doesn't provide them, bail.  This does allow us to
			 * fill raid0 from raid1.
			 */
			if ((data & extra) && !(block_group->flags & extra))
				goto loop;
		}

have_block_group:
		cached = block_group_cache_done(block_group);
		if (unlikely(!cached)) {
			found_uncached_bg = true;
			ret = cache_block_group(block_group, 0);
			BUG_ON(ret < 0);
			ret = 0;
		}

		if (unlikely(block_group->ro))
			goto loop;

		/*
		 * Ok we want to try and use the cluster allocator, so
		 * lets look there
		 */
		if (last_ptr) {
			unsigned long aligned_cluster;
			/*
			 * the refill lock keeps out other
			 * people trying to start a new cluster
			 */
			spin_lock(&last_ptr->refill_lock);
			used_block_group = last_ptr->block_group;
			if (used_block_group != block_group &&
			    (!used_block_group ||
			     used_block_group->ro ||
			     !block_group_bits(used_block_group, data))) {
				used_block_group = block_group;
				goto refill_cluster;
			}

			if (used_block_group != block_group)
				btrfs_get_block_group(used_block_group);

			offset = btrfs_alloc_from_cluster(used_block_group,
			  last_ptr, num_bytes, used_block_group->key.objectid);
			if (offset) {
				/* we have a block, we're done */
				spin_unlock(&last_ptr->refill_lock);
				trace_btrfs_reserve_extent_cluster(root,
					block_group, search_start, num_bytes);
				goto checks;
			}

			WARN_ON(last_ptr->block_group != used_block_group);
			if (used_block_group != block_group) {
				btrfs_put_block_group(used_block_group);
				used_block_group = block_group;
			}
refill_cluster:
			BUG_ON(used_block_group != block_group);
			/* If we are on LOOP_NO_EMPTY_SIZE, we can't
			 * set up a new clusters, so lets just skip it
			 * and let the allocator find whatever block
			 * it can find.  If we reach this point, we
			 * will have tried the cluster allocator
			 * plenty of times and not have found
			 * anything, so we are likely way too
			 * fragmented for the clustering stuff to find
			 * anything.
			 *
			 * However, if the cluster is taken from the
			 * current block group, release the cluster
			 * first, so that we stand a better chance of
			 * succeeding in the unclustered
			 * allocation.  */
			if (loop >= LOOP_NO_EMPTY_SIZE &&
			    last_ptr->block_group != block_group) {
				spin_unlock(&last_ptr->refill_lock);
				goto unclustered_alloc;
			}

			/*
			 * this cluster didn't work out, free it and
			 * start over
			 */
			btrfs_return_cluster_to_free_space(NULL, last_ptr);

			if (loop >= LOOP_NO_EMPTY_SIZE) {
				spin_unlock(&last_ptr->refill_lock);
				goto unclustered_alloc;
			}

			aligned_cluster = max_t(unsigned long,
						empty_cluster + empty_size,
					      block_group->full_stripe_len);

			/* allocate a cluster in this block group */
			ret = btrfs_find_space_cluster(trans, root,
					       block_group, last_ptr,
					       search_start, num_bytes,
					       aligned_cluster);
			if (ret == 0) {
				/*
				 * now pull our allocation out of this
				 * cluster
				 */
				offset = btrfs_alloc_from_cluster(block_group,
						  last_ptr, num_bytes,
						  search_start);
				if (offset) {
					/* we found one, proceed */
					spin_unlock(&last_ptr->refill_lock);
					trace_btrfs_reserve_extent_cluster(root,
						block_group, search_start,
						num_bytes);
					goto checks;
				}
			} else if (!cached && loop > LOOP_CACHING_NOWAIT
				   && !failed_cluster_refill) {
				spin_unlock(&last_ptr->refill_lock);

				failed_cluster_refill = true;
				wait_block_group_cache_progress(block_group,
				       num_bytes + empty_cluster + empty_size);
				goto have_block_group;
			}

			/*
			 * at this point we either didn't find a cluster
			 * or we weren't able to allocate a block from our
			 * cluster.  Free the cluster we've been trying
			 * to use, and go to the next block group
			 */
			btrfs_return_cluster_to_free_space(NULL, last_ptr);
			spin_unlock(&last_ptr->refill_lock);
			goto loop;
		}

unclustered_alloc:
		spin_lock(&block_group->free_space_ctl->tree_lock);
		if (cached &&
		    block_group->free_space_ctl->free_space <
		    num_bytes + empty_cluster + empty_size) {
			spin_unlock(&block_group->free_space_ctl->tree_lock);
			goto loop;
		}
		spin_unlock(&block_group->free_space_ctl->tree_lock);

		offset = btrfs_find_space_for_alloc(block_group, search_start,
						    num_bytes, empty_size);
		/*
		 * If we didn't find a chunk, and we haven't failed on this
		 * block group before, and this block group is in the middle of
		 * caching and we are ok with waiting, then go ahead and wait
		 * for progress to be made, and set failed_alloc to true.
		 *
		 * If failed_alloc is true then we've already waited on this
		 * block group once and should move on to the next block group.
		 */
		if (!offset && !failed_alloc && !cached &&
		    loop > LOOP_CACHING_NOWAIT) {
			wait_block_group_cache_progress(block_group,
						num_bytes + empty_size);
			failed_alloc = true;
			goto have_block_group;
		} else if (!offset) {
			if (!cached)
				have_caching_bg = true;
			goto loop;
		}
checks:
		search_start = stripe_align(root, used_block_group,
					    offset, num_bytes);

		/* move on to the next group */
		if (search_start + num_bytes >
		    used_block_group->key.objectid + used_block_group->key.offset) {
			btrfs_add_free_space(used_block_group, offset, num_bytes);
			goto loop;
		}

		if (offset < search_start)
			btrfs_add_free_space(used_block_group, offset,
					     search_start - offset);
		BUG_ON(offset > search_start);

		ret = btrfs_update_reserved_bytes(used_block_group, num_bytes,
						  alloc_type);
		if (ret == -EAGAIN) {
			btrfs_add_free_space(used_block_group, offset, num_bytes);
			goto loop;
		}

		/* we are all good, lets return */
		ins->objectid = search_start;
		ins->offset = num_bytes;

		trace_btrfs_reserve_extent(orig_root, block_group,
					   search_start, num_bytes);
		if (used_block_group != block_group)
			btrfs_put_block_group(used_block_group);
		btrfs_put_block_group(block_group);
		break;
loop:
		failed_cluster_refill = false;
		failed_alloc = false;
		BUG_ON(index != get_block_group_index(block_group));
		if (used_block_group != block_group)
			btrfs_put_block_group(used_block_group);
		btrfs_put_block_group(block_group);
	}
	up_read(&space_info->groups_sem);

	if (!ins->objectid && loop >= LOOP_CACHING_WAIT && have_caching_bg)
		goto search;

	if (!ins->objectid && ++index < BTRFS_NR_RAID_TYPES)
		goto search;

	/*
	 * LOOP_CACHING_NOWAIT, search partially cached block groups, kicking
	 *			caching kthreads as we move along
	 * LOOP_CACHING_WAIT, search everything, and wait if our bg is caching
	 * LOOP_ALLOC_CHUNK, force a chunk allocation and try again
	 * LOOP_NO_EMPTY_SIZE, set empty_size and empty_cluster to 0 and try
	 *			again
	 */
	if (!ins->objectid && loop < LOOP_NO_EMPTY_SIZE) {
		index = 0;
		loop++;
		if (loop == LOOP_ALLOC_CHUNK) {
			ret = do_chunk_alloc(trans, root, data,
					     CHUNK_ALLOC_FORCE);
			/*
			 * Do not bail out on ENOSPC since we
			 * can do more things.
			 */
			if (ret < 0 && ret != -ENOSPC) {
				btrfs_abort_transaction(trans,
							root, ret);
				goto out;
			}
		}

		if (loop == LOOP_NO_EMPTY_SIZE) {
			empty_size = 0;
			empty_cluster = 0;
		}

		goto search;
	} else if (!ins->objectid) {
		ret = -ENOSPC;
	} else if (ins->objectid) {
		ret = 0;
	}
out:

	return ret;
}

static void dump_space_info(struct btrfs_space_info *info, u64 bytes,
			    int dump_block_groups)
{
	struct btrfs_block_group_cache *cache;
	int index = 0;

	spin_lock(&info->lock);
	printk(KERN_INFO "space_info %llu has %llu free, is %sfull\n",
	       (unsigned long long)info->flags,
	       (unsigned long long)(info->total_bytes - info->bytes_used -
				    info->bytes_pinned - info->bytes_reserved -
				    info->bytes_readonly),
	       (info->full) ? "" : "not ");
	printk(KERN_INFO "space_info total=%llu, used=%llu, pinned=%llu, "
	       "reserved=%llu, may_use=%llu, readonly=%llu\n",
	       (unsigned long long)info->total_bytes,
	       (unsigned long long)info->bytes_used,
	       (unsigned long long)info->bytes_pinned,
	       (unsigned long long)info->bytes_reserved,
	       (unsigned long long)info->bytes_may_use,
	       (unsigned long long)info->bytes_readonly);
	spin_unlock(&info->lock);

	if (!dump_block_groups)
		return;

	down_read(&info->groups_sem);
again:
	list_for_each_entry(cache, &info->block_groups[index], list) {
		spin_lock(&cache->lock);
		printk(KERN_INFO "block group %llu has %llu bytes, %llu used %llu pinned %llu reserved %s\n",
		       (unsigned long long)cache->key.objectid,
		       (unsigned long long)cache->key.offset,
		       (unsigned long long)btrfs_block_group_used(&cache->item),
		       (unsigned long long)cache->pinned,
		       (unsigned long long)cache->reserved,
		       cache->ro ? "[readonly]" : "");
		btrfs_dump_free_space(cache, bytes);
		spin_unlock(&cache->lock);
	}
	if (++index < BTRFS_NR_RAID_TYPES)
		goto again;
	up_read(&info->groups_sem);
}

int btrfs_reserve_extent(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 num_bytes, u64 min_alloc_size,
			 u64 empty_size, u64 hint_byte,
			 struct btrfs_key *ins, u64 data)
{
	bool final_tried = false;
	int ret;

	data = btrfs_get_alloc_profile(root, data);
again:
	WARN_ON(num_bytes < root->sectorsize);
	ret = find_free_extent(trans, root, num_bytes, empty_size,
			       hint_byte, ins, data);

	if (ret == -ENOSPC) {
		if (!final_tried) {
			num_bytes = num_bytes >> 1;
			num_bytes = round_down(num_bytes, root->sectorsize);
			num_bytes = max(num_bytes, min_alloc_size);
			if (num_bytes == min_alloc_size)
				final_tried = true;
			goto again;
		} else if (btrfs_test_opt(root, ENOSPC_DEBUG)) {
			struct btrfs_space_info *sinfo;

			sinfo = __find_space_info(root->fs_info, data);
			btrfs_err(root->fs_info, "allocation failed flags %llu, wanted %llu",
				(unsigned long long)data,
				(unsigned long long)num_bytes);
			if (sinfo)
				dump_space_info(sinfo, num_bytes, 1);
		}
	}

	trace_btrfs_reserved_extent_alloc(root, ins->objectid, ins->offset);

	return ret;
}

static int __btrfs_free_reserved_extent(struct btrfs_root *root,
					u64 start, u64 len, int pin)
{
	struct btrfs_block_group_cache *cache;
	int ret = 0;

	cache = btrfs_lookup_block_group(root->fs_info, start);
	if (!cache) {
		btrfs_err(root->fs_info, "Unable to find block group for %llu",
			(unsigned long long)start);
		return -ENOSPC;
	}

	if (btrfs_test_opt(root, DISCARD))
		ret = btrfs_discard_extent(root, start, len, NULL);

	if (pin)
		pin_down_extent(root, cache, start, len, 1);
	else {
		btrfs_add_free_space(cache, start, len);
		btrfs_update_reserved_bytes(cache, len, RESERVE_FREE);
	}
	btrfs_put_block_group(cache);

	trace_btrfs_reserved_extent_free(root, start, len);

	return ret;
}

int btrfs_free_reserved_extent(struct btrfs_root *root,
					u64 start, u64 len)
{
	return __btrfs_free_reserved_extent(root, start, len, 0);
}

int btrfs_free_and_pin_reserved_extent(struct btrfs_root *root,
				       u64 start, u64 len)
{
	return __btrfs_free_reserved_extent(root, start, len, 1);
}

static int alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      u64 parent, u64 root_objectid,
				      u64 flags, u64 owner, u64 offset,
				      struct btrfs_key *ins, int ref_mod)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_extent_item *extent_item;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	int type;
	u32 size;

	if (parent > 0)
		type = BTRFS_SHARED_DATA_REF_KEY;
	else
		type = BTRFS_EXTENT_DATA_REF_KEY;

	size = sizeof(*extent_item) + btrfs_extent_inline_ref_size(type);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	if (ret) {
		btrfs_free_path(path);
		return ret;
	}

	leaf = path->nodes[0];
	extent_item = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, extent_item, ref_mod);
	btrfs_set_extent_generation(leaf, extent_item, trans->transid);
	btrfs_set_extent_flags(leaf, extent_item,
			       flags | BTRFS_EXTENT_FLAG_DATA);

	iref = (struct btrfs_extent_inline_ref *)(extent_item + 1);
	btrfs_set_extent_inline_ref_type(leaf, iref, type);
	if (parent > 0) {
		struct btrfs_shared_data_ref *ref;
		ref = (struct btrfs_shared_data_ref *)(iref + 1);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
		btrfs_set_shared_data_ref_count(leaf, ref, ref_mod);
	} else {
		struct btrfs_extent_data_ref *ref;
		ref = (struct btrfs_extent_data_ref *)(&iref->offset);
		btrfs_set_extent_data_ref_root(leaf, ref, root_objectid);
		btrfs_set_extent_data_ref_objectid(leaf, ref, owner);
		btrfs_set_extent_data_ref_offset(leaf, ref, offset);
		btrfs_set_extent_data_ref_count(leaf, ref, ref_mod);
	}

	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);

	ret = update_block_group(root, ins->objectid, ins->offset, 1);
	if (ret) { /* -ENOENT, logic error */
		btrfs_err(fs_info, "update block group failed for %llu %llu",
			(unsigned long long)ins->objectid,
			(unsigned long long)ins->offset);
		BUG();
	}
	return ret;
}

static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 parent, u64 root_objectid,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_extent_item *extent_item;
	struct btrfs_tree_block_info *block_info;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	u32 size = sizeof(*extent_item) + sizeof(*iref);
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

	if (!skinny_metadata)
		size += sizeof(*block_info);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	if (ret) {
		btrfs_free_path(path);
		return ret;
	}

	leaf = path->nodes[0];
	extent_item = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, extent_item, 1);
	btrfs_set_extent_generation(leaf, extent_item, trans->transid);
	btrfs_set_extent_flags(leaf, extent_item,
			       flags | BTRFS_EXTENT_FLAG_TREE_BLOCK);

	if (skinny_metadata) {
		iref = (struct btrfs_extent_inline_ref *)(extent_item + 1);
	} else {
		block_info = (struct btrfs_tree_block_info *)(extent_item + 1);
		btrfs_set_tree_block_key(leaf, block_info, key);
		btrfs_set_tree_block_level(leaf, block_info, level);
		iref = (struct btrfs_extent_inline_ref *)(block_info + 1);
	}

	if (parent > 0) {
		BUG_ON(!(flags & BTRFS_BLOCK_FLAG_FULL_BACKREF));
		btrfs_set_extent_inline_ref_type(leaf, iref,
						 BTRFS_SHARED_BLOCK_REF_KEY);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else {
		btrfs_set_extent_inline_ref_type(leaf, iref,
						 BTRFS_TREE_BLOCK_REF_KEY);
		btrfs_set_extent_inline_ref_offset(leaf, iref, root_objectid);
	}

	btrfs_mark_buffer_dirty(leaf);
	btrfs_free_path(path);

	ret = update_block_group(root, ins->objectid, root->leafsize, 1);
	if (ret) { /* -ENOENT, logic error */
		btrfs_err(fs_info, "update block group failed for %llu %llu",
			(unsigned long long)ins->objectid,
			(unsigned long long)ins->offset);
		BUG();
	}
	return ret;
}

int btrfs_alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 root_objectid, u64 owner,
				     u64 offset, struct btrfs_key *ins)
{
	int ret;

	BUG_ON(root_objectid == BTRFS_TREE_LOG_OBJECTID);

	ret = btrfs_add_delayed_data_ref(root->fs_info, trans, ins->objectid,
					 ins->offset, 0,
					 root_objectid, owner, offset,
					 BTRFS_ADD_DELAYED_EXTENT, NULL, 0);
	return ret;
}

/*
 * this is used by the tree logging recovery code.  It records that
 * an extent has been allocated and makes sure to clear the free
 * space cache bits as well
 */
int btrfs_alloc_logged_file_extent(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   u64 root_objectid, u64 owner, u64 offset,
				   struct btrfs_key *ins)
{
	int ret;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_caching_control *caching_ctl;
	u64 start = ins->objectid;
	u64 num_bytes = ins->offset;

	block_group = btrfs_lookup_block_group(root->fs_info, ins->objectid);
	cache_block_group(block_group, 0);
	caching_ctl = get_caching_control(block_group);

	if (!caching_ctl) {
		BUG_ON(!block_group_cache_done(block_group));
		ret = btrfs_remove_free_space(block_group, start, num_bytes);
		BUG_ON(ret); /* -ENOMEM */
	} else {
		mutex_lock(&caching_ctl->mutex);

		if (start >= caching_ctl->progress) {
			ret = add_excluded_extent(root, start, num_bytes);
			BUG_ON(ret); /* -ENOMEM */
		} else if (start + num_bytes <= caching_ctl->progress) {
			ret = btrfs_remove_free_space(block_group,
						      start, num_bytes);
			BUG_ON(ret); /* -ENOMEM */
		} else {
			num_bytes = caching_ctl->progress - start;
			ret = btrfs_remove_free_space(block_group,
						      start, num_bytes);
			BUG_ON(ret); /* -ENOMEM */

			start = caching_ctl->progress;
			num_bytes = ins->objectid + ins->offset -
				    caching_ctl->progress;
			ret = add_excluded_extent(root, start, num_bytes);
			BUG_ON(ret); /* -ENOMEM */
		}

		mutex_unlock(&caching_ctl->mutex);
		put_caching_control(caching_ctl);
	}

	ret = btrfs_update_reserved_bytes(block_group, ins->offset,
					  RESERVE_ALLOC_NO_ACCOUNT);
	BUG_ON(ret); /* logic error */
	btrfs_put_block_group(block_group);
	ret = alloc_reserved_file_extent(trans, root, 0, root_objectid,
					 0, owner, offset, ins, 1);
	return ret;
}

struct extent_buffer *btrfs_init_new_buffer(struct btrfs_trans_handle *trans,
					    struct btrfs_root *root,
					    u64 bytenr, u32 blocksize,
					    int level)
{
	struct extent_buffer *buf;

	buf = btrfs_find_create_tree_block(root, bytenr, blocksize);
	if (!buf)
		return ERR_PTR(-ENOMEM);
	btrfs_set_header_generation(buf, trans->transid);
	btrfs_set_buffer_lockdep_class(root->root_key.objectid, buf, level);
	btrfs_tree_lock(buf);
	clean_tree_block(trans, root, buf);
	clear_bit(EXTENT_BUFFER_STALE, &buf->bflags);

	btrfs_set_lock_blocking(buf);
	btrfs_set_buffer_uptodate(buf);

	if (root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID) {
		/*
		 * we allow two log transactions at a time, use different
		 * EXENT bit to differentiate dirty pages.
		 */
		if (root->log_transid % 2 == 0)
			set_extent_dirty(&root->dirty_log_pages, buf->start,
					buf->start + buf->len - 1, GFP_NOFS);
		else
			set_extent_new(&root->dirty_log_pages, buf->start,
					buf->start + buf->len - 1, GFP_NOFS);
	} else {
		set_extent_dirty(&trans->transaction->dirty_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	}
	trans->blocks_used++;
	/* this returns a buffer locked for blocking */
	return buf;
}

static struct btrfs_block_rsv *
use_block_rsv(struct btrfs_trans_handle *trans,
	      struct btrfs_root *root, u32 blocksize)
{
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *global_rsv = &root->fs_info->global_block_rsv;
	int ret;

	block_rsv = get_block_rsv(trans, root);

	if (block_rsv->size == 0) {
		ret = reserve_metadata_bytes(root, block_rsv, blocksize,
					     BTRFS_RESERVE_NO_FLUSH);
		/*
		 * If we couldn't reserve metadata bytes try and use some from
		 * the global reserve.
		 */
		if (ret && block_rsv != global_rsv) {
			ret = block_rsv_use_bytes(global_rsv, blocksize);
			if (!ret)
				return global_rsv;
			return ERR_PTR(ret);
		} else if (ret) {
			return ERR_PTR(ret);
		}
		return block_rsv;
	}

	ret = block_rsv_use_bytes(block_rsv, blocksize);
	if (!ret)
		return block_rsv;
	if (ret && !block_rsv->failfast) {
		if (btrfs_test_opt(root, ENOSPC_DEBUG)) {
			static DEFINE_RATELIMIT_STATE(_rs,
					DEFAULT_RATELIMIT_INTERVAL * 10,
					/*DEFAULT_RATELIMIT_BURST*/ 1);
			if (__ratelimit(&_rs))
				WARN(1, KERN_DEBUG
					"btrfs: block rsv returned %d\n", ret);
		}
		ret = reserve_metadata_bytes(root, block_rsv, blocksize,
					     BTRFS_RESERVE_NO_FLUSH);
		if (!ret) {
			return block_rsv;
		} else if (ret && block_rsv != global_rsv) {
			ret = block_rsv_use_bytes(global_rsv, blocksize);
			if (!ret)
				return global_rsv;
		}
	}

	return ERR_PTR(-ENOSPC);
}

static void unuse_block_rsv(struct btrfs_fs_info *fs_info,
			    struct btrfs_block_rsv *block_rsv, u32 blocksize)
{
	block_rsv_add_bytes(block_rsv, blocksize, 0);
	block_rsv_release_bytes(fs_info, block_rsv, NULL, 0);
}

/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the key for the extent through ins, and a tree buffer for
 * the first block of the extent through buf.
 *
 * returns the tree buffer or NULL.
 */
struct extent_buffer *btrfs_alloc_free_block(struct btrfs_trans_handle *trans,
					struct btrfs_root *root, u32 blocksize,
					u64 parent, u64 root_objectid,
					struct btrfs_disk_key *key, int level,
					u64 hint, u64 empty_size)
{
	struct btrfs_key ins;
	struct btrfs_block_rsv *block_rsv;
	struct extent_buffer *buf;
	u64 flags = 0;
	int ret;
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

	block_rsv = use_block_rsv(trans, root, blocksize);
	if (IS_ERR(block_rsv))
		return ERR_CAST(block_rsv);

	ret = btrfs_reserve_extent(trans, root, blocksize, blocksize,
				   empty_size, hint, &ins, 0);
	if (ret) {
		unuse_block_rsv(root->fs_info, block_rsv, blocksize);
		return ERR_PTR(ret);
	}

	buf = btrfs_init_new_buffer(trans, root, ins.objectid,
				    blocksize, level);
	BUG_ON(IS_ERR(buf)); /* -ENOMEM */

	if (root_objectid == BTRFS_TREE_RELOC_OBJECTID) {
		if (parent == 0)
			parent = ins.objectid;
		flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
	} else
		BUG_ON(parent > 0);

	if (root_objectid != BTRFS_TREE_LOG_OBJECTID) {
		struct btrfs_delayed_extent_op *extent_op;
		extent_op = btrfs_alloc_delayed_extent_op();
		BUG_ON(!extent_op); /* -ENOMEM */
		if (key)
			memcpy(&extent_op->key, key, sizeof(extent_op->key));
		else
			memset(&extent_op->key, 0, sizeof(extent_op->key));
		extent_op->flags_to_set = flags;
		if (skinny_metadata)
			extent_op->update_key = 0;
		else
			extent_op->update_key = 1;
		extent_op->update_flags = 1;
		extent_op->is_data = 0;

		ret = btrfs_add_delayed_tree_ref(root->fs_info, trans,
					ins.objectid,
					ins.offset, parent, root_objectid,
					level, BTRFS_ADD_DELAYED_EXTENT,
					extent_op, 0);
		BUG_ON(ret); /* -ENOMEM */
	}
	return buf;
}

struct walk_control {
	u64 refs[BTRFS_MAX_LEVEL];
	u64 flags[BTRFS_MAX_LEVEL];
	struct btrfs_key update_progress;
	int stage;
	int level;
	int shared_level;
	int update_ref;
	int keep_locks;
	int reada_slot;
	int reada_count;
	int for_reloc;
};

#define DROP_REFERENCE	1
#define UPDATE_BACKREF	2

static noinline void reada_walk_down(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     struct walk_control *wc,
				     struct btrfs_path *path)
{
	u64 bytenr;
	u64 generation;
	u64 refs;
	u64 flags;
	u32 nritems;
	u32 blocksize;
	struct btrfs_key key;
	struct extent_buffer *eb;
	int ret;
	int slot;
	int nread = 0;

	if (path->slots[wc->level] < wc->reada_slot) {
		wc->reada_count = wc->reada_count * 2 / 3;
		wc->reada_count = max(wc->reada_count, 2);
	} else {
		wc->reada_count = wc->reada_count * 3 / 2;
		wc->reada_count = min_t(int, wc->reada_count,
					BTRFS_NODEPTRS_PER_BLOCK(root));
	}

	eb = path->nodes[wc->level];
	nritems = btrfs_header_nritems(eb);
	blocksize = btrfs_level_size(root, wc->level - 1);

	for (slot = path->slots[wc->level]; slot < nritems; slot++) {
		if (nread >= wc->reada_count)
			break;

		cond_resched();
		bytenr = btrfs_node_blockptr(eb, slot);
		generation = btrfs_node_ptr_generation(eb, slot);

		if (slot == path->slots[wc->level])
			goto reada;

		if (wc->stage == UPDATE_BACKREF &&
		    generation <= root->root_key.offset)
			continue;

		/* We don't lock the tree block, it's OK to be racy here */
		ret = btrfs_lookup_extent_info(trans, root, bytenr,
					       wc->level - 1, 1, &refs,
					       &flags);
		/* We don't care about errors in readahead. */
		if (ret < 0)
			continue;
		BUG_ON(refs == 0);

		if (wc->stage == DROP_REFERENCE) {
			if (refs == 1)
				goto reada;

			if (wc->level == 1 &&
			    (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF))
				continue;
			if (!wc->update_ref ||
			    generation <= root->root_key.offset)
				continue;
			btrfs_node_key_to_cpu(eb, &key, slot);
			ret = btrfs_comp_cpu_keys(&key,
						  &wc->update_progress);
			if (ret < 0)
				continue;
		} else {
			if (wc->level == 1 &&
			    (flags & BTRFS_BLOCK_FLAG_FULL_BACKREF))
				continue;
		}
reada:
		ret = readahead_tree_block(root, bytenr, blocksize,
					   generation);
		if (ret)
			break;
		nread++;
	}
	wc->reada_slot = slot;
}

/*
 * helper to process tree block while walking down the tree.
 *
 * when wc->stage == UPDATE_BACKREF, this function updates
 * back refs for pointers in the block.
 *
 * NOTE: return value 1 means we should stop walking down.
 */
static noinline int walk_down_proc(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path,
				   struct walk_control *wc, int lookup_info)
{
	int level = wc->level;
	struct extent_buffer *eb = path->nodes[level];
	u64 flag = BTRFS_BLOCK_FLAG_FULL_BACKREF;
	int ret;

	if (wc->stage == UPDATE_BACKREF &&
	    btrfs_header_owner(eb) != root->root_key.objectid)
		return 1;

	/*
	 * when reference count of tree block is 1, it won't increase
	 * again. once full backref flag is set, we never clear it.
	 */
	if (lookup_info &&
	    ((wc->stage == DROP_REFERENCE && wc->refs[level] != 1) ||
	     (wc->stage == UPDATE_BACKREF && !(wc->flags[level] & flag)))) {
		BUG_ON(!path->locks[level]);
		ret = btrfs_lookup_extent_info(trans, root,
					       eb->start, level, 1,
					       &wc->refs[level],
					       &wc->flags[level]);
		BUG_ON(ret == -ENOMEM);
		if (ret)
			return ret;
		BUG_ON(wc->refs[level] == 0);
	}

	if (wc->stage == DROP_REFERENCE) {
		if (wc->refs[level] > 1)
			return 1;

		if (path->locks[level] && !wc->keep_locks) {
			btrfs_tree_unlock_rw(eb, path->locks[level]);
			path->locks[level] = 0;
		}
		return 0;
	}

	/* wc->stage == UPDATE_BACKREF */
	if (!(wc->flags[level] & flag)) {
		BUG_ON(!path->locks[level]);
		ret = btrfs_inc_ref(trans, root, eb, 1, wc->for_reloc);
		BUG_ON(ret); /* -ENOMEM */
		ret = btrfs_dec_ref(trans, root, eb, 0, wc->for_reloc);
		BUG_ON(ret); /* -ENOMEM */
		ret = btrfs_set_disk_extent_flags(trans, root, eb->start,
						  eb->len, flag, 0);
		BUG_ON(ret); /* -ENOMEM */
		wc->flags[level] |= flag;
	}

	/*
	 * the block is shared by multiple trees, so it's not good to
	 * keep the tree lock
	 */
	if (path->locks[level] && level > 0) {
		btrfs_tree_unlock_rw(eb, path->locks[level]);
		path->locks[level] = 0;
	}
	return 0;
}

/*
 * helper to process tree block pointer.
 *
 * when wc->stage == DROP_REFERENCE, this function checks
 * reference count of the block pointed to. if the block
 * is shared and we need update back refs for the subtree
 * rooted at the block, this function changes wc->stage to
 * UPDATE_BACKREF. if the block is shared and there is no
 * need to update back, this function drops the reference
 * to the block.
 *
 * NOTE: return value 1 means we should stop walking down.
 */
static noinline int do_walk_down(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct walk_control *wc, int *lookup_info)
{
	u64 bytenr;
	u64 generation;
	u64 parent;
	u32 blocksize;
	struct btrfs_key key;
	struct extent_buffer *next;
	int level = wc->level;
	int reada = 0;
	int ret = 0;

	generation = btrfs_node_ptr_generation(path->nodes[level],
					       path->slots[level]);
	/*
	 * if the lower level block was created before the snapshot
	 * was created, we know there is no need to update back refs
	 * for the subtree
	 */
	if (wc->stage == UPDATE_BACKREF &&
	    generation <= root->root_key.offset) {
		*lookup_info = 1;
		return 1;
	}

	bytenr = btrfs_node_blockptr(path->nodes[level], path->slots[level]);
	blocksize = btrfs_level_size(root, level - 1);

	next = btrfs_find_tree_block(root, bytenr, blocksize);
	if (!next) {
		next = btrfs_find_create_tree_block(root, bytenr, blocksize);
		if (!next)
			return -ENOMEM;
		reada = 1;
	}
	btrfs_tree_lock(next);
	btrfs_set_lock_blocking(next);

	ret = btrfs_lookup_extent_info(trans, root, bytenr, level - 1, 1,
				       &wc->refs[level - 1],
				       &wc->flags[level - 1]);
	if (ret < 0) {
		btrfs_tree_unlock(next);
		return ret;
	}

	if (unlikely(wc->refs[level - 1] == 0)) {
		btrfs_err(root->fs_info, "Missing references.");
		BUG();
	}
	*lookup_info = 0;

	if (wc->stage == DROP_REFERENCE) {
		if (wc->refs[level - 1] > 1) {
			if (level == 1 &&
			    (wc->flags[0] & BTRFS_BLOCK_FLAG_FULL_BACKREF))
				goto skip;

			if (!wc->update_ref ||
			    generation <= root->root_key.offset)
				goto skip;

			btrfs_node_key_to_cpu(path->nodes[level], &key,
					      path->slots[level]);
			ret = btrfs_comp_cpu_keys(&key, &wc->update_progress);
			if (ret < 0)
				goto skip;

			wc->stage = UPDATE_BACKREF;
			wc->shared_level = level - 1;
		}
	} else {
		if (level == 1 &&
		    (wc->flags[0] & BTRFS_BLOCK_FLAG_FULL_BACKREF))
			goto skip;
	}

	if (!btrfs_buffer_uptodate(next, generation, 0)) {
		btrfs_tree_unlock(next);
		free_extent_buffer(next);
		next = NULL;
		*lookup_info = 1;
	}

	if (!next) {
		if (reada && level == 1)
			reada_walk_down(trans, root, wc, path);
		next = read_tree_block(root, bytenr, blocksize, generation);
		if (!next)
			return -EIO;
		btrfs_tree_lock(next);
		btrfs_set_lock_blocking(next);
	}

	level--;
	BUG_ON(level != btrfs_header_level(next));
	path->nodes[level] = next;
	path->slots[level] = 0;
	path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;
	wc->level = level;
	if (wc->level == 1)
		wc->reada_slot = 0;
	return 0;
skip:
	wc->refs[level - 1] = 0;
	wc->flags[level - 1] = 0;
	if (wc->stage == DROP_REFERENCE) {
		if (wc->flags[level] & BTRFS_BLOCK_FLAG_FULL_BACKREF) {
			parent = path->nodes[level]->start;
		} else {
			BUG_ON(root->root_key.objectid !=
			       btrfs_header_owner(path->nodes[level]));
			parent = 0;
		}

		ret = btrfs_free_extent(trans, root, bytenr, blocksize, parent,
				root->root_key.objectid, level - 1, 0, 0);
		BUG_ON(ret); /* -ENOMEM */
	}
	btrfs_tree_unlock(next);
	free_extent_buffer(next);
	*lookup_info = 1;
	return 1;
}

/*
 * helper to process tree block while walking up the tree.
 *
 * when wc->stage == DROP_REFERENCE, this function drops
 * reference count on the block.
 *
 * when wc->stage == UPDATE_BACKREF, this function changes
 * wc->stage back to DROP_REFERENCE if we changed wc->stage
 * to UPDATE_BACKREF previously while processing the block.
 *
 * NOTE: return value 1 means we should stop walking up.
 */
static noinline int walk_up_proc(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct walk_control *wc)
{
	int ret;
	int level = wc->level;
	struct extent_buffer *eb = path->nodes[level];
	u64 parent = 0;

	if (wc->stage == UPDATE_BACKREF) {
		BUG_ON(wc->shared_level < level);
		if (level < wc->shared_level)
			goto out;

		ret = find_next_key(path, level + 1, &wc->update_progress);
		if (ret > 0)
			wc->update_ref = 0;

		wc->stage = DROP_REFERENCE;
		wc->shared_level = -1;
		path->slots[level] = 0;

		/*
		 * check reference count again if the block isn't locked.
		 * we should start walking down the tree again if reference
		 * count is one.
		 */
		if (!path->locks[level]) {
			BUG_ON(level == 0);
			btrfs_tree_lock(eb);
			btrfs_set_lock_blocking(eb);
			path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;

			ret = btrfs_lookup_extent_info(trans, root,
						       eb->start, level, 1,
						       &wc->refs[level],
						       &wc->flags[level]);
			if (ret < 0) {
				btrfs_tree_unlock_rw(eb, path->locks[level]);
				path->locks[level] = 0;
				return ret;
			}
			BUG_ON(wc->refs[level] == 0);
			if (wc->refs[level] == 1) {
				btrfs_tree_unlock_rw(eb, path->locks[level]);
				path->locks[level] = 0;
				return 1;
			}
		}
	}

	/* wc->stage == DROP_REFERENCE */
	BUG_ON(wc->refs[level] > 1 && !path->locks[level]);

	if (wc->refs[level] == 1) {
		if (level == 0) {
			if (wc->flags[level] & BTRFS_BLOCK_FLAG_FULL_BACKREF)
				ret = btrfs_dec_ref(trans, root, eb, 1,
						    wc->for_reloc);
			else
				ret = btrfs_dec_ref(trans, root, eb, 0,
						    wc->for_reloc);
			BUG_ON(ret); /* -ENOMEM */
		}
		/* make block locked assertion in clean_tree_block happy */
		if (!path->locks[level] &&
		    btrfs_header_generation(eb) == trans->transid) {
			btrfs_tree_lock(eb);
			btrfs_set_lock_blocking(eb);
			path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;
		}
		clean_tree_block(trans, root, eb);
	}

	if (eb == root->node) {
		if (wc->flags[level] & BTRFS_BLOCK_FLAG_FULL_BACKREF)
			parent = eb->start;
		else
			BUG_ON(root->root_key.objectid !=
			       btrfs_header_owner(eb));
	} else {
		if (wc->flags[level + 1] & BTRFS_BLOCK_FLAG_FULL_BACKREF)
			parent = path->nodes[level + 1]->start;
		else
			BUG_ON(root->root_key.objectid !=
			       btrfs_header_owner(path->nodes[level + 1]));
	}

	btrfs_free_tree_block(trans, root, eb, parent, wc->refs[level] == 1);
out:
	wc->refs[level] = 0;
	wc->flags[level] = 0;
	return 0;
}

static noinline int walk_down_tree(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root,
				   struct btrfs_path *path,
				   struct walk_control *wc)
{
	int level = wc->level;
	int lookup_info = 1;
	int ret;

	while (level >= 0) {
		ret = walk_down_proc(trans, root, path, wc, lookup_info);
		if (ret > 0)
			break;

		if (level == 0)
			break;

		if (path->slots[level] >=
		    btrfs_header_nritems(path->nodes[level]))
			break;

		ret = do_walk_down(trans, root, path, wc, &lookup_info);
		if (ret > 0) {
			path->slots[level]++;
			continue;
		} else if (ret < 0)
			return ret;
		level = wc->level;
	}
	return 0;
}

static noinline int walk_up_tree(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct walk_control *wc, int max_level)
{
	int level = wc->level;
	int ret;

	path->slots[level] = btrfs_header_nritems(path->nodes[level]);
	while (level < max_level && path->nodes[level]) {
		wc->level = level;
		if (path->slots[level] + 1 <
		    btrfs_header_nritems(path->nodes[level])) {
			path->slots[level]++;
			return 0;
		} else {
			ret = walk_up_proc(trans, root, path, wc);
			if (ret > 0)
				return 0;

			if (path->locks[level]) {
				btrfs_tree_unlock_rw(path->nodes[level],
						     path->locks[level]);
				path->locks[level] = 0;
			}
			free_extent_buffer(path->nodes[level]);
			path->nodes[level] = NULL;
			level++;
		}
	}
	return 1;
}

/*
 * drop a subvolume tree.
 *
 * this function traverses the tree freeing any blocks that only
 * referenced by the tree.
 *
 * when a shared tree block is found. this function decreases its
 * reference count by one. if update_ref is true, this function
 * also make sure backrefs for the shared block and all lower level
 * blocks are properly updated.
 *
 * If called with for_reloc == 0, may exit early with -EAGAIN
 */
int btrfs_drop_snapshot(struct btrfs_root *root,
			 struct btrfs_block_rsv *block_rsv, int update_ref,
			 int for_reloc)
{
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = root->fs_info->tree_root;
	struct btrfs_root_item *root_item = &root->root_item;
	struct walk_control *wc;
	struct btrfs_key key;
	int err = 0;
	int ret;
	int level;

	path = btrfs_alloc_path();
	if (!path) {
		err = -ENOMEM;
		goto out;
	}

	wc = kzalloc(sizeof(*wc), GFP_NOFS);
	if (!wc) {
		btrfs_free_path(path);
		err = -ENOMEM;
		goto out;
	}

	trans = btrfs_start_transaction(tree_root, 0);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_free;
	}

	if (block_rsv)
		trans->block_rsv = block_rsv;

	if (btrfs_disk_key_objectid(&root_item->drop_progress) == 0) {
		level = btrfs_header_level(root->node);
		path->nodes[level] = btrfs_lock_root_node(root);
		btrfs_set_lock_blocking(path->nodes[level]);
		path->slots[level] = 0;
		path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;
		memset(&wc->update_progress, 0,
		       sizeof(wc->update_progress));
	} else {
		btrfs_disk_key_to_cpu(&key, &root_item->drop_progress);
		memcpy(&wc->update_progress, &key,
		       sizeof(wc->update_progress));

		level = root_item->drop_level;
		BUG_ON(level == 0);
		path->lowest_level = level;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		path->lowest_level = 0;
		if (ret < 0) {
			err = ret;
			goto out_end_trans;
		}
		WARN_ON(ret > 0);

		/*
		 * unlock our path, this is safe because only this
		 * function is allowed to delete this snapshot
		 */
		btrfs_unlock_up_safe(path, 0);

		level = btrfs_header_level(root->node);
		while (1) {
			btrfs_tree_lock(path->nodes[level]);
			btrfs_set_lock_blocking(path->nodes[level]);

			ret = btrfs_lookup_extent_info(trans, root,
						path->nodes[level]->start,
						level, 1, &wc->refs[level],
						&wc->flags[level]);
			if (ret < 0) {
				err = ret;
				goto out_end_trans;
			}
			BUG_ON(wc->refs[level] == 0);

			if (level == root_item->drop_level)
				break;

			btrfs_tree_unlock(path->nodes[level]);
			WARN_ON(wc->refs[level] != 1);
			level--;
		}
	}

	wc->level = level;
	wc->shared_level = -1;
	wc->stage = DROP_REFERENCE;
	wc->update_ref = update_ref;
	wc->keep_locks = 0;
	wc->for_reloc = for_reloc;
	wc->reada_count = BTRFS_NODEPTRS_PER_BLOCK(root);

	while (1) {
		if (!for_reloc && btrfs_fs_closing(root->fs_info)) {
			pr_debug("btrfs: drop snapshot early exit\n");
			err = -EAGAIN;
			goto out_end_trans;
		}

		ret = walk_down_tree(trans, root, path, wc);
		if (ret < 0) {
			err = ret;
			break;
		}

		ret = walk_up_tree(trans, root, path, wc, BTRFS_MAX_LEVEL);
		if (ret < 0) {
			err = ret;
			break;
		}

		if (ret > 0) {
			BUG_ON(wc->stage != DROP_REFERENCE);
			break;
		}

		if (wc->stage == DROP_REFERENCE) {
			level = wc->level;
			btrfs_node_key(path->nodes[level],
				       &root_item->drop_progress,
				       path->slots[level]);
			root_item->drop_level = level;
		}

		BUG_ON(wc->level == 0);
		if (btrfs_should_end_transaction(trans, tree_root)) {
			ret = btrfs_update_root(trans, tree_root,
						&root->root_key,
						root_item);
			if (ret) {
				btrfs_abort_transaction(trans, tree_root, ret);
				err = ret;
				goto out_end_trans;
			}

			btrfs_end_transaction_throttle(trans, tree_root);
			trans = btrfs_start_transaction(tree_root, 0);
			if (IS_ERR(trans)) {
				err = PTR_ERR(trans);
				goto out_free;
			}
			if (block_rsv)
				trans->block_rsv = block_rsv;
		}
	}
	btrfs_release_path(path);
	if (err)
		goto out_end_trans;

	ret = btrfs_del_root(trans, tree_root, &root->root_key);
	if (ret) {
		btrfs_abort_transaction(trans, tree_root, ret);
		goto out_end_trans;
	}

	if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
		ret = btrfs_find_last_root(tree_root, root->root_key.objectid,
					   NULL, NULL);
		if (ret < 0) {
			btrfs_abort_transaction(trans, tree_root, ret);
			err = ret;
			goto out_end_trans;
		} else if (ret > 0) {
			/* if we fail to delete the orphan item this time
			 * around, it'll get picked up the next time.
			 *
			 * The most common failure here is just -ENOENT.
			 */
			btrfs_del_orphan_item(trans, tree_root,
					      root->root_key.objectid);
		}
	}

	if (root->in_radix) {
		btrfs_free_fs_root(tree_root->fs_info, root);
	} else {
		free_extent_buffer(root->node);
		free_extent_buffer(root->commit_root);
		kfree(root);
	}
out_end_trans:
	btrfs_end_transaction_throttle(trans, tree_root);
out_free:
	kfree(wc);
	btrfs_free_path(path);
out:
	if (err)
		btrfs_std_error(root->fs_info, err);
	return err;
}

/*
 * drop subtree rooted at tree block 'node'.
 *
 * NOTE: this function will unlock and release tree block 'node'
 * only used by relocation code
 */
int btrfs_drop_subtree(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct extent_buffer *node,
			struct extent_buffer *parent)
{
	struct btrfs_path *path;
	struct walk_control *wc;
	int level;
	int parent_level;
	int ret = 0;
	int wret;

	BUG_ON(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	wc = kzalloc(sizeof(*wc), GFP_NOFS);
	if (!wc) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	btrfs_assert_tree_locked(parent);
	parent_level = btrfs_header_level(parent);
	extent_buffer_get(parent);
	path->nodes[parent_level] = parent;
	path->slots[parent_level] = btrfs_header_nritems(parent);

	btrfs_assert_tree_locked(node);
	level = btrfs_header_level(node);
	path->nodes[level] = node;
	path->slots[level] = 0;
	path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;

	wc->refs[parent_level] = 1;
	wc->flags[parent_level] = BTRFS_BLOCK_FLAG_FULL_BACKREF;
	wc->level = level;
	wc->shared_level = -1;
	wc->stage = DROP_REFERENCE;
	wc->update_ref = 0;
	wc->keep_locks = 1;
	wc->for_reloc = 1;
	wc->reada_count = BTRFS_NODEPTRS_PER_BLOCK(root);

	while (1) {
		wret = walk_down_tree(trans, root, path, wc);
		if (wret < 0) {
			ret = wret;
			break;
		}

		wret = walk_up_tree(trans, root, path, wc, parent_level);
		if (wret < 0)
			ret = wret;
		if (wret != 0)
			break;
	}

	kfree(wc);
	btrfs_free_path(path);
	return ret;
}

static u64 update_block_group_flags(struct btrfs_root *root, u64 flags)
{
	u64 num_devices;
	u64 stripped;

	/*
	 * if restripe for this chunk_type is on pick target profile and
	 * return, otherwise do the usual balance
	 */
	stripped = get_restripe_target(root->fs_info, flags);
	if (stripped)
		return extended_to_chunk(stripped);

	/*
	 * we add in the count of missing devices because we want
	 * to make sure that any RAID levels on a degraded FS
	 * continue to be honored.
	 */
	num_devices = root->fs_info->fs_devices->rw_devices +
		root->fs_info->fs_devices->missing_devices;

	stripped = BTRFS_BLOCK_GROUP_RAID0 |
		BTRFS_BLOCK_GROUP_RAID5 | BTRFS_BLOCK_GROUP_RAID6 |
		BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID10;

	if (num_devices == 1) {
		stripped |= BTRFS_BLOCK_GROUP_DUP;
		stripped = flags & ~stripped;

		/* turn raid0 into single device chunks */
		if (flags & BTRFS_BLOCK_GROUP_RAID0)
			return stripped;

		/* turn mirroring into duplication */
		if (flags & (BTRFS_BLOCK_GROUP_RAID1 |
			     BTRFS_BLOCK_GROUP_RAID10))
			return stripped | BTRFS_BLOCK_GROUP_DUP;
	} else {
		/* they already had raid on here, just return */
		if (flags & stripped)
			return flags;

		stripped |= BTRFS_BLOCK_GROUP_DUP;
		stripped = flags & ~stripped;

		/* switch duplicated blocks with raid1 */
		if (flags & BTRFS_BLOCK_GROUP_DUP)
			return stripped | BTRFS_BLOCK_GROUP_RAID1;

		/* this is drive concat, leave it alone */
	}

	return flags;
}

static int set_block_group_ro(struct btrfs_block_group_cache *cache, int force)
{
	struct btrfs_space_info *sinfo = cache->space_info;
	u64 num_bytes;
	u64 min_allocable_bytes;
	int ret = -ENOSPC;


	/*
	 * We need some metadata space and system metadata space for
	 * allocating chunks in some corner cases until we force to set
	 * it to be readonly.
	 */
	if ((sinfo->flags &
	     (BTRFS_BLOCK_GROUP_SYSTEM | BTRFS_BLOCK_GROUP_METADATA)) &&
	    !force)
		min_allocable_bytes = 1 * 1024 * 1024;
	else
		min_allocable_bytes = 0;

	spin_lock(&sinfo->lock);
	spin_lock(&cache->lock);

	if (cache->ro) {
		ret = 0;
		goto out;
	}

	num_bytes = cache->key.offset - cache->reserved - cache->pinned -
		    cache->bytes_super - btrfs_block_group_used(&cache->item);

	if (sinfo->bytes_used + sinfo->bytes_reserved + sinfo->bytes_pinned +
	    sinfo->bytes_may_use + sinfo->bytes_readonly + num_bytes +
	    min_allocable_bytes <= sinfo->total_bytes) {
		sinfo->bytes_readonly += num_bytes;
		cache->ro = 1;
		ret = 0;
	}
out:
	spin_unlock(&cache->lock);
	spin_unlock(&sinfo->lock);
	return ret;
}

int btrfs_set_block_group_ro(struct btrfs_root *root,
			     struct btrfs_block_group_cache *cache)

{
	struct btrfs_trans_handle *trans;
	u64 alloc_flags;
	int ret;

	BUG_ON(cache->ro);

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	alloc_flags = update_block_group_flags(root, cache->flags);
	if (alloc_flags != cache->flags) {
		ret = do_chunk_alloc(trans, root, alloc_flags,
				     CHUNK_ALLOC_FORCE);
		if (ret < 0)
			goto out;
	}

	ret = set_block_group_ro(cache, 0);
	if (!ret)
		goto out;
	alloc_flags = get_alloc_profile(root, cache->space_info->flags);
	ret = do_chunk_alloc(trans, root, alloc_flags,
			     CHUNK_ALLOC_FORCE);
	if (ret < 0)
		goto out;
	ret = set_block_group_ro(cache, 0);
out:
	btrfs_end_transaction(trans, root);
	return ret;
}

int btrfs_force_chunk_alloc(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root, u64 type)
{
	u64 alloc_flags = get_alloc_profile(root, type);
	return do_chunk_alloc(trans, root, alloc_flags,
			      CHUNK_ALLOC_FORCE);
}

/*
 * helper to account the unused space of all the readonly block group in the
 * list. takes mirrors into account.
 */
static u64 __btrfs_get_ro_block_group_free_space(struct list_head *groups_list)
{
	struct btrfs_block_group_cache *block_group;
	u64 free_bytes = 0;
	int factor;

	list_for_each_entry(block_group, groups_list, list) {
		spin_lock(&block_group->lock);

		if (!block_group->ro) {
			spin_unlock(&block_group->lock);
			continue;
		}

		if (block_group->flags & (BTRFS_BLOCK_GROUP_RAID1 |
					  BTRFS_BLOCK_GROUP_RAID10 |
					  BTRFS_BLOCK_GROUP_DUP))
			factor = 2;
		else
			factor = 1;

		free_bytes += (block_group->key.offset -
			       btrfs_block_group_used(&block_group->item)) *
			       factor;

		spin_unlock(&block_group->lock);
	}

	return free_bytes;
}

/*
 * helper to account the unused space of all the readonly block group in the
 * space_info. takes mirrors into account.
 */
u64 btrfs_account_ro_block_groups_free_space(struct btrfs_space_info *sinfo)
{
	int i;
	u64 free_bytes = 0;

	spin_lock(&sinfo->lock);

	for(i = 0; i < BTRFS_NR_RAID_TYPES; i++)
		if (!list_empty(&sinfo->block_groups[i]))
			free_bytes += __btrfs_get_ro_block_group_free_space(
						&sinfo->block_groups[i]);

	spin_unlock(&sinfo->lock);

	return free_bytes;
}

void btrfs_set_block_group_rw(struct btrfs_root *root,
			      struct btrfs_block_group_cache *cache)
{
	struct btrfs_space_info *sinfo = cache->space_info;
	u64 num_bytes;

	BUG_ON(!cache->ro);

	spin_lock(&sinfo->lock);
	spin_lock(&cache->lock);
	num_bytes = cache->key.offset - cache->reserved - cache->pinned -
		    cache->bytes_super - btrfs_block_group_used(&cache->item);
	sinfo->bytes_readonly -= num_bytes;
	cache->ro = 0;
	spin_unlock(&cache->lock);
	spin_unlock(&sinfo->lock);
}

/*
 * checks to see if its even possible to relocate this block group.
 *
 * @return - -1 if it's not a good idea to relocate this block group, 0 if its
 * ok to go ahead and try.
 */
int btrfs_can_relocate(struct btrfs_root *root, u64 bytenr)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_space_info *space_info;
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;
	struct btrfs_device *device;
	u64 min_free;
	u64 dev_min = 1;
	u64 dev_nr = 0;
	u64 target;
	int index;
	int full = 0;
	int ret = 0;

	block_group = btrfs_lookup_block_group(root->fs_info, bytenr);

	/* odd, couldn't find the block group, leave it alone */
	if (!block_group)
		return -1;

	min_free = btrfs_block_group_used(&block_group->item);

	/* no bytes used, we're good */
	if (!min_free)
		goto out;

	space_info = block_group->space_info;
	spin_lock(&space_info->lock);

	full = space_info->full;

	/*
	 * if this is the last block group we have in this space, we can't
	 * relocate it unless we're able to allocate a new chunk below.
	 *
	 * Otherwise, we need to make sure we have room in the space to handle
	 * all of the extents from this block group.  If we can, we're good
	 */
	if ((space_info->total_bytes != block_group->key.offset) &&
	    (space_info->bytes_used + space_info->bytes_reserved +
	     space_info->bytes_pinned + space_info->bytes_readonly +
	     min_free < space_info->total_bytes)) {
		spin_unlock(&space_info->lock);
		goto out;
	}
	spin_unlock(&space_info->lock);

	/*
	 * ok we don't have enough space, but maybe we have free space on our
	 * devices to allocate new chunks for relocation, so loop through our
	 * alloc devices and guess if we have enough space.  if this block
	 * group is going to be restriped, run checks against the target
	 * profile instead of the current one.
	 */
	ret = -1;

	/*
	 * index:
	 *      0: raid10
	 *      1: raid1
	 *      2: dup
	 *      3: raid0
	 *      4: single
	 */
	target = get_restripe_target(root->fs_info, block_group->flags);
	if (target) {
		index = __get_raid_index(extended_to_chunk(target));
	} else {
		/*
		 * this is just a balance, so if we were marked as full
		 * we know there is no space for a new chunk
		 */
		if (full)
			goto out;

		index = get_block_group_index(block_group);
	}

	if (index == BTRFS_RAID_RAID10) {
		dev_min = 4;
		/* Divide by 2 */
		min_free >>= 1;
	} else if (index == BTRFS_RAID_RAID1) {
		dev_min = 2;
	} else if (index == BTRFS_RAID_DUP) {
		/* Multiply by 2 */
		min_free <<= 1;
	} else if (index == BTRFS_RAID_RAID0) {
		dev_min = fs_devices->rw_devices;
		do_div(min_free, dev_min);
	}

	mutex_lock(&root->fs_info->chunk_mutex);
	list_for_each_entry(device, &fs_devices->alloc_list, dev_alloc_list) {
		u64 dev_offset;

		/*
		 * check to make sure we can actually find a chunk with enough
		 * space to fit our block group in.
		 */
		if (device->total_bytes > device->bytes_used + min_free &&
		    !device->is_tgtdev_for_dev_replace) {
			ret = find_free_dev_extent(device, min_free,
						   &dev_offset, NULL);
			if (!ret)
				dev_nr++;

			if (dev_nr >= dev_min)
				break;

			ret = -1;
		}
	}
	mutex_unlock(&root->fs_info->chunk_mutex);
out:
	btrfs_put_block_group(block_group);
	return ret;
}

static int find_first_block_group(struct btrfs_root *root,
		struct btrfs_path *path, struct btrfs_key *key)
{
	int ret = 0;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	int slot;

	ret = btrfs_search_slot(NULL, root, key, path, 0, 0);
	if (ret < 0)
		goto out;

	while (1) {
		slot = path->slots[0];
		leaf = path->nodes[0];
		if (slot >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto out;
			break;
		}
		btrfs_item_key_to_cpu(leaf, &found_key, slot);

		if (found_key.objectid >= key->objectid &&
		    found_key.type == BTRFS_BLOCK_GROUP_ITEM_KEY) {
			ret = 0;
			goto out;
		}
		path->slots[0]++;
	}
out:
	return ret;
}

void btrfs_put_block_group_cache(struct btrfs_fs_info *info)
{
	struct btrfs_block_group_cache *block_group;
	u64 last = 0;

	while (1) {
		struct inode *inode;

		block_group = btrfs_lookup_first_block_group(info, last);
		while (block_group) {
			spin_lock(&block_group->lock);
			if (block_group->iref)
				break;
			spin_unlock(&block_group->lock);
			block_group = next_block_group(info->tree_root,
						       block_group);
		}
		if (!block_group) {
			if (last == 0)
				break;
			last = 0;
			continue;
		}

		inode = block_group->inode;
		block_group->iref = 0;
		block_group->inode = NULL;
		spin_unlock(&block_group->lock);
		iput(inode);
		last = block_group->key.objectid + block_group->key.offset;
		btrfs_put_block_group(block_group);
	}
}

int btrfs_free_block_groups(struct btrfs_fs_info *info)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_space_info *space_info;
	struct btrfs_caching_control *caching_ctl;
	struct rb_node *n;

	down_write(&info->extent_commit_sem);
	while (!list_empty(&info->caching_block_groups)) {
		caching_ctl = list_entry(info->caching_block_groups.next,
					 struct btrfs_caching_control, list);
		list_del(&caching_ctl->list);
		put_caching_control(caching_ctl);
	}
	up_write(&info->extent_commit_sem);

	spin_lock(&info->block_group_cache_lock);
	while ((n = rb_last(&info->block_group_cache_tree)) != NULL) {
		block_group = rb_entry(n, struct btrfs_block_group_cache,
				       cache_node);
		rb_erase(&block_group->cache_node,
			 &info->block_group_cache_tree);
		spin_unlock(&info->block_group_cache_lock);

		down_write(&block_group->space_info->groups_sem);
		list_del(&block_group->list);
		up_write(&block_group->space_info->groups_sem);

		if (block_group->cached == BTRFS_CACHE_STARTED)
			wait_block_group_cache_done(block_group);

		/*
		 * We haven't cached this block group, which means we could
		 * possibly have excluded extents on this block group.
		 */
		if (block_group->cached == BTRFS_CACHE_NO)
			free_excluded_extents(info->extent_root, block_group);

		btrfs_remove_free_space_cache(block_group);
		btrfs_put_block_group(block_group);

		spin_lock(&info->block_group_cache_lock);
	}
	spin_unlock(&info->block_group_cache_lock);

	/* now that all the block groups are freed, go through and
	 * free all the space_info structs.  This is only called during
	 * the final stages of unmount, and so we know nobody is
	 * using them.  We call synchronize_rcu() once before we start,
	 * just to be on the safe side.
	 */
	synchronize_rcu();

	release_global_block_rsv(info);

	while(!list_empty(&info->space_info)) {
		space_info = list_entry(info->space_info.next,
					struct btrfs_space_info,
					list);
		if (btrfs_test_opt(info->tree_root, ENOSPC_DEBUG)) {
			if (space_info->bytes_pinned > 0 ||
			    space_info->bytes_reserved > 0 ||
			    space_info->bytes_may_use > 0) {
				WARN_ON(1);
				dump_space_info(space_info, 0, 0);
			}
		}
		list_del(&space_info->list);
		kfree(space_info);
	}
	return 0;
}

static void __link_block_group(struct btrfs_space_info *space_info,
			       struct btrfs_block_group_cache *cache)
{
	int index = get_block_group_index(cache);

	down_write(&space_info->groups_sem);
	list_add_tail(&cache->list, &space_info->block_groups[index]);
	up_write(&space_info->groups_sem);
}

int btrfs_read_block_groups(struct btrfs_root *root)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_block_group_cache *cache;
	struct btrfs_fs_info *info = root->fs_info;
	struct btrfs_space_info *space_info;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	int need_clear = 0;
	u64 cache_gen;

	root = info->extent_root;
	key.objectid = 0;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_BLOCK_GROUP_ITEM_KEY);
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = 1;

	cache_gen = btrfs_super_cache_generation(root->fs_info->super_copy);
	if (btrfs_test_opt(root, SPACE_CACHE) &&
	    btrfs_super_generation(root->fs_info->super_copy) != cache_gen)
		need_clear = 1;
	if (btrfs_test_opt(root, CLEAR_CACHE))
		need_clear = 1;

	while (1) {
		ret = find_first_block_group(root, path, &key);
		if (ret > 0)
			break;
		if (ret != 0)
			goto error;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		cache = kzalloc(sizeof(*cache), GFP_NOFS);
		if (!cache) {
			ret = -ENOMEM;
			goto error;
		}
		cache->free_space_ctl = kzalloc(sizeof(*cache->free_space_ctl),
						GFP_NOFS);
		if (!cache->free_space_ctl) {
			kfree(cache);
			ret = -ENOMEM;
			goto error;
		}

		atomic_set(&cache->count, 1);
		spin_lock_init(&cache->lock);
		cache->fs_info = info;
		INIT_LIST_HEAD(&cache->list);
		INIT_LIST_HEAD(&cache->cluster_list);

		if (need_clear) {
			/*
			 * When we mount with old space cache, we need to
			 * set BTRFS_DC_CLEAR and set dirty flag.
			 *
			 * a) Setting 'BTRFS_DC_CLEAR' makes sure that we
			 *    truncate the old free space cache inode and
			 *    setup a new one.
			 * b) Setting 'dirty flag' makes sure that we flush
			 *    the new space cache info onto disk.
			 */
			cache->disk_cache_state = BTRFS_DC_CLEAR;
			if (btrfs_test_opt(root, SPACE_CACHE))
				cache->dirty = 1;
		}

		read_extent_buffer(leaf, &cache->item,
				   btrfs_item_ptr_offset(leaf, path->slots[0]),
				   sizeof(cache->item));
		memcpy(&cache->key, &found_key, sizeof(found_key));

		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(path);
		cache->flags = btrfs_block_group_flags(&cache->item);
		cache->sectorsize = root->sectorsize;
		cache->full_stripe_len = btrfs_full_stripe_len(root,
					       &root->fs_info->mapping_tree,
					       found_key.objectid);
		btrfs_init_free_space_ctl(cache);

		/*
		 * We need to exclude the super stripes now so that the space
		 * info has super bytes accounted for, otherwise we'll think
		 * we have more space than we actually do.
		 */
		ret = exclude_super_stripes(root, cache);
		if (ret) {
			/*
			 * We may have excluded something, so call this just in
			 * case.
			 */
			free_excluded_extents(root, cache);
			kfree(cache->free_space_ctl);
			kfree(cache);
			goto error;
		}

		/*
		 * check for two cases, either we are full, and therefore
		 * don't need to bother with the caching work since we won't
		 * find any space, or we are empty, and we can just add all
		 * the space in and be done with it.  This saves us _alot_ of
		 * time, particularly in the full case.
		 */
		if (found_key.offset == btrfs_block_group_used(&cache->item)) {
			cache->last_byte_to_unpin = (u64)-1;
			cache->cached = BTRFS_CACHE_FINISHED;
			free_excluded_extents(root, cache);
		} else if (btrfs_block_group_used(&cache->item) == 0) {
			cache->last_byte_to_unpin = (u64)-1;
			cache->cached = BTRFS_CACHE_FINISHED;
			add_new_free_space(cache, root->fs_info,
					   found_key.objectid,
					   found_key.objectid +
					   found_key.offset);
			free_excluded_extents(root, cache);
		}

		ret = update_space_info(info, cache->flags, found_key.offset,
					btrfs_block_group_used(&cache->item),
					&space_info);
		BUG_ON(ret); /* -ENOMEM */
		cache->space_info = space_info;
		spin_lock(&cache->space_info->lock);
		cache->space_info->bytes_readonly += cache->bytes_super;
		spin_unlock(&cache->space_info->lock);

		__link_block_group(space_info, cache);

		ret = btrfs_add_block_group_cache(root->fs_info, cache);
		BUG_ON(ret); /* Logic error */

		set_avail_alloc_bits(root->fs_info, cache->flags);
		if (btrfs_chunk_readonly(root, cache->key.objectid))
			set_block_group_ro(cache, 1);
	}

	list_for_each_entry_rcu(space_info, &root->fs_info->space_info, list) {
		if (!(get_alloc_profile(root, space_info->flags) &
		      (BTRFS_BLOCK_GROUP_RAID10 |
		       BTRFS_BLOCK_GROUP_RAID1 |
		       BTRFS_BLOCK_GROUP_RAID5 |
		       BTRFS_BLOCK_GROUP_RAID6 |
		       BTRFS_BLOCK_GROUP_DUP)))
			continue;
		/*
		 * avoid allocating from un-mirrored block group if there are
		 * mirrored block groups.
		 */
		list_for_each_entry(cache, &space_info->block_groups[3], list)
			set_block_group_ro(cache, 1);
		list_for_each_entry(cache, &space_info->block_groups[4], list)
			set_block_group_ro(cache, 1);
	}

	init_global_block_rsv(info);
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

void btrfs_create_pending_block_groups(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root)
{
	struct btrfs_block_group_cache *block_group, *tmp;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
	struct btrfs_block_group_item item;
	struct btrfs_key key;
	int ret = 0;

	list_for_each_entry_safe(block_group, tmp, &trans->new_bgs,
				 new_bg_list) {
		list_del_init(&block_group->new_bg_list);

		if (ret)
			continue;

		spin_lock(&block_group->lock);
		memcpy(&item, &block_group->item, sizeof(item));
		memcpy(&key, &block_group->key, sizeof(key));
		spin_unlock(&block_group->lock);

		ret = btrfs_insert_item(trans, extent_root, &key, &item,
					sizeof(item));
		if (ret)
			btrfs_abort_transaction(trans, extent_root, ret);
	}
}

int btrfs_make_block_group(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, u64 bytes_used,
			   u64 type, u64 chunk_objectid, u64 chunk_offset,
			   u64 size)
{
	int ret;
	struct btrfs_root *extent_root;
	struct btrfs_block_group_cache *cache;

	extent_root = root->fs_info->extent_root;

	root->fs_info->last_trans_log_full_commit = trans->transid;

	cache = kzalloc(sizeof(*cache), GFP_NOFS);
	if (!cache)
		return -ENOMEM;
	cache->free_space_ctl = kzalloc(sizeof(*cache->free_space_ctl),
					GFP_NOFS);
	if (!cache->free_space_ctl) {
		kfree(cache);
		return -ENOMEM;
	}

	cache->key.objectid = chunk_offset;
	cache->key.offset = size;
	cache->key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	cache->sectorsize = root->sectorsize;
	cache->fs_info = root->fs_info;
	cache->full_stripe_len = btrfs_full_stripe_len(root,
					       &root->fs_info->mapping_tree,
					       chunk_offset);

	atomic_set(&cache->count, 1);
	spin_lock_init(&cache->lock);
	INIT_LIST_HEAD(&cache->list);
	INIT_LIST_HEAD(&cache->cluster_list);
	INIT_LIST_HEAD(&cache->new_bg_list);

	btrfs_init_free_space_ctl(cache);

	btrfs_set_block_group_used(&cache->item, bytes_used);
	btrfs_set_block_group_chunk_objectid(&cache->item, chunk_objectid);
	cache->flags = type;
	btrfs_set_block_group_flags(&cache->item, type);

	cache->last_byte_to_unpin = (u64)-1;
	cache->cached = BTRFS_CACHE_FINISHED;
	ret = exclude_super_stripes(root, cache);
	if (ret) {
		/*
		 * We may have excluded something, so call this just in
		 * case.
		 */
		free_excluded_extents(root, cache);
		kfree(cache->free_space_ctl);
		kfree(cache);
		return ret;
	}

	add_new_free_space(cache, root->fs_info, chunk_offset,
			   chunk_offset + size);

	free_excluded_extents(root, cache);

	ret = update_space_info(root->fs_info, cache->flags, size, bytes_used,
				&cache->space_info);
	BUG_ON(ret); /* -ENOMEM */
	update_global_block_rsv(root->fs_info);

	spin_lock(&cache->space_info->lock);
	cache->space_info->bytes_readonly += cache->bytes_super;
	spin_unlock(&cache->space_info->lock);

	__link_block_group(cache->space_info, cache);

	ret = btrfs_add_block_group_cache(root->fs_info, cache);
	BUG_ON(ret); /* Logic error */

	list_add_tail(&cache->new_bg_list, &trans->new_bgs);

	set_avail_alloc_bits(extent_root->fs_info, type);

	return 0;
}

static void clear_avail_alloc_bits(struct btrfs_fs_info *fs_info, u64 flags)
{
	u64 extra_flags = chunk_to_extended(flags) &
				BTRFS_EXTENDED_PROFILE_MASK;

	write_seqlock(&fs_info->profiles_lock);
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		fs_info->avail_data_alloc_bits &= ~extra_flags;
	if (flags & BTRFS_BLOCK_GROUP_METADATA)
		fs_info->avail_metadata_alloc_bits &= ~extra_flags;
	if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
		fs_info->avail_system_alloc_bits &= ~extra_flags;
	write_sequnlock(&fs_info->profiles_lock);
}

int btrfs_remove_block_group(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 group_start)
{
	struct btrfs_path *path;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_free_cluster *cluster;
	struct btrfs_root *tree_root = root->fs_info->tree_root;
	struct btrfs_key key;
	struct inode *inode;
	int ret;
	int index;
	int factor;

	root = root->fs_info->extent_root;

	block_group = btrfs_lookup_block_group(root->fs_info, group_start);
	BUG_ON(!block_group);
	BUG_ON(!block_group->ro);

	/*
	 * Free the reserved super bytes from this block group before
	 * remove it.
	 */
	free_excluded_extents(root, block_group);

	memcpy(&key, &block_group->key, sizeof(key));
	index = get_block_group_index(block_group);
	if (block_group->flags & (BTRFS_BLOCK_GROUP_DUP |
				  BTRFS_BLOCK_GROUP_RAID1 |
				  BTRFS_BLOCK_GROUP_RAID10))
		factor = 2;
	else
		factor = 1;

	/* make sure this block group isn't part of an allocation cluster */
	cluster = &root->fs_info->data_alloc_cluster;
	spin_lock(&cluster->refill_lock);
	btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&cluster->refill_lock);

	/*
	 * make sure this block group isn't part of a metadata
	 * allocation cluster
	 */
	cluster = &root->fs_info->meta_alloc_cluster;
	spin_lock(&cluster->refill_lock);
	btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&cluster->refill_lock);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	inode = lookup_free_space_inode(tree_root, block_group, path);
	if (!IS_ERR(inode)) {
		ret = btrfs_orphan_add(trans, inode);
		if (ret) {
			btrfs_add_delayed_iput(inode);
			goto out;
		}
		clear_nlink(inode);
		/* One for the block groups ref */
		spin_lock(&block_group->lock);
		if (block_group->iref) {
			block_group->iref = 0;
			block_group->inode = NULL;
			spin_unlock(&block_group->lock);
			iput(inode);
		} else {
			spin_unlock(&block_group->lock);
		}
		/* One for our lookup ref */
		btrfs_add_delayed_iput(inode);
	}

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = block_group->key.objectid;
	key.type = 0;

	ret = btrfs_search_slot(trans, tree_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret > 0)
		btrfs_release_path(path);
	if (ret == 0) {
		ret = btrfs_del_item(trans, tree_root, path);
		if (ret)
			goto out;
		btrfs_release_path(path);
	}

	spin_lock(&root->fs_info->block_group_cache_lock);
	rb_erase(&block_group->cache_node,
		 &root->fs_info->block_group_cache_tree);

	if (root->fs_info->first_logical_byte == block_group->key.objectid)
		root->fs_info->first_logical_byte = (u64)-1;
	spin_unlock(&root->fs_info->block_group_cache_lock);

	down_write(&block_group->space_info->groups_sem);
	/*
	 * we must use list_del_init so people can check to see if they
	 * are still on the list after taking the semaphore
	 */
	list_del_init(&block_group->list);
	if (list_empty(&block_group->space_info->block_groups[index]))
		clear_avail_alloc_bits(root->fs_info, block_group->flags);
	up_write(&block_group->space_info->groups_sem);

	if (block_group->cached == BTRFS_CACHE_STARTED)
		wait_block_group_cache_done(block_group);

	btrfs_remove_free_space_cache(block_group);

	spin_lock(&block_group->space_info->lock);
	block_group->space_info->total_bytes -= block_group->key.offset;
	block_group->space_info->bytes_readonly -= block_group->key.offset;
	block_group->space_info->disk_total -= block_group->key.offset * factor;
	spin_unlock(&block_group->space_info->lock);

	memcpy(&key, &block_group->key, sizeof(key));

	btrfs_clear_space_info_full(root->fs_info);

	btrfs_put_block_group(block_group);
	btrfs_put_block_group(block_group);

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0)
		ret = -EIO;
	if (ret < 0)
		goto out;

	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_init_space_info(struct btrfs_fs_info *fs_info)
{
	struct btrfs_space_info *space_info;
	struct btrfs_super_block *disk_super;
	u64 features;
	u64 flags;
	int mixed = 0;
	int ret;

	disk_super = fs_info->super_copy;
	if (!btrfs_super_root(disk_super))
		return 1;

	features = btrfs_super_incompat_flags(disk_super);
	if (features & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS)
		mixed = 1;

	flags = BTRFS_BLOCK_GROUP_SYSTEM;
	ret = update_space_info(fs_info, flags, 0, 0, &space_info);
	if (ret)
		goto out;

	if (mixed) {
		flags = BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA;
		ret = update_space_info(fs_info, flags, 0, 0, &space_info);
	} else {
		flags = BTRFS_BLOCK_GROUP_METADATA;
		ret = update_space_info(fs_info, flags, 0, 0, &space_info);
		if (ret)
			goto out;

		flags = BTRFS_BLOCK_GROUP_DATA;
		ret = update_space_info(fs_info, flags, 0, 0, &space_info);
	}
out:
	return ret;
}

int btrfs_error_unpin_extent_range(struct btrfs_root *root, u64 start, u64 end)
{
	return unpin_extent_range(root, start, end);
}

int btrfs_error_discard_extent(struct btrfs_root *root, u64 bytenr,
			       u64 num_bytes, u64 *actual_bytes)
{
	return btrfs_discard_extent(root, bytenr, num_bytes, actual_bytes);
}

int btrfs_trim_fs(struct btrfs_root *root, struct fstrim_range *range)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_group_cache *cache = NULL;
	u64 group_trimmed;
	u64 start;
	u64 end;
	u64 trimmed = 0;
	u64 total_bytes = btrfs_super_total_bytes(fs_info->super_copy);
	int ret = 0;

	/*
	 * try to trim all FS space, our block group may start from non-zero.
	 */
	if (range->len == total_bytes)
		cache = btrfs_lookup_first_block_group(fs_info, range->start);
	else
		cache = btrfs_lookup_block_group(fs_info, range->start);

	while (cache) {
		if (cache->key.objectid >= (range->start + range->len)) {
			btrfs_put_block_group(cache);
			break;
		}

		start = max(range->start, cache->key.objectid);
		end = min(range->start + range->len,
				cache->key.objectid + cache->key.offset);

		if (end - start >= range->minlen) {
			if (!block_group_cache_done(cache)) {
				ret = cache_block_group(cache, 0);
				if (!ret)
					wait_block_group_cache_done(cache);
			}
			ret = btrfs_trim_block_group(cache,
						     &group_trimmed,
						     start,
						     end,
						     range->minlen);

			trimmed += group_trimmed;
			if (ret) {
				btrfs_put_block_group(cache);
				break;
			}
		}

		cache = next_block_group(fs_info->tree_root, cache);
	}

	range->len = trimmed;
	return ret;
}
