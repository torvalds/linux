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
#include <linux/sched/signal.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/sort.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/percpu_counter.h>
#include <linux/lockdep.h>
#include <linux/crc32c.h>
#include "tree-log.h"
#include "disk-io.h"
#include "print-tree.h"
#include "volumes.h"
#include "raid56.h"
#include "locking.h"
#include "free-space-cache.h"
#include "free-space-tree.h"
#include "math.h"
#include "sysfs.h"
#include "qgroup.h"
#include "ref-verify.h"

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

static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
			       struct btrfs_fs_info *fs_info,
				struct btrfs_delayed_ref_node *node, u64 parent,
				u64 root_objectid, u64 owner_objectid,
				u64 owner_offset, int refs_to_drop,
				struct btrfs_delayed_extent_op *extra_op);
static void __run_delayed_extent_op(struct btrfs_delayed_extent_op *extent_op,
				    struct extent_buffer *leaf,
				    struct btrfs_extent_item *ei);
static int alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				      struct btrfs_fs_info *fs_info,
				      u64 parent, u64 root_objectid,
				      u64 flags, u64 owner, u64 offset,
				      struct btrfs_key *ins, int ref_mod);
static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_fs_info *fs_info,
				     u64 parent, u64 root_objectid,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins);
static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_fs_info *fs_info, u64 flags,
			  int force);
static int find_next_key(struct btrfs_path *path, int level,
			 struct btrfs_key *key);
static void dump_space_info(struct btrfs_fs_info *fs_info,
			    struct btrfs_space_info *info, u64 bytes,
			    int dump_block_groups);
static int block_rsv_use_bytes(struct btrfs_block_rsv *block_rsv,
			       u64 num_bytes);
static void space_info_add_new_bytes(struct btrfs_fs_info *fs_info,
				     struct btrfs_space_info *space_info,
				     u64 num_bytes);
static void space_info_add_old_bytes(struct btrfs_fs_info *fs_info,
				     struct btrfs_space_info *space_info,
				     u64 num_bytes);

static noinline int
block_group_cache_done(struct btrfs_block_group_cache *cache)
{
	smp_mb();
	return cache->cached == BTRFS_CACHE_FINISHED ||
		cache->cached == BTRFS_CACHE_ERROR;
}

static int block_group_bits(struct btrfs_block_group_cache *cache, u64 bits)
{
	return (cache->flags & bits) == bits;
}

void btrfs_get_block_group(struct btrfs_block_group_cache *cache)
{
	atomic_inc(&cache->count);
}

void btrfs_put_block_group(struct btrfs_block_group_cache *cache)
{
	if (atomic_dec_and_test(&cache->count)) {
		WARN_ON(cache->pinned > 0);
		WARN_ON(cache->reserved > 0);

		/*
		 * If not empty, someone is still holding mutex of
		 * full_stripe_lock, which can only be released by caller.
		 * And it will definitely cause use-after-free when caller
		 * tries to release full stripe lock.
		 *
		 * No better way to resolve, but only to warn.
		 */
		WARN_ON(!RB_EMPTY_ROOT(&cache->full_stripe_locks_root.root));
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

static int add_excluded_extent(struct btrfs_fs_info *fs_info,
			       u64 start, u64 num_bytes)
{
	u64 end = start + num_bytes - 1;
	set_extent_bits(&fs_info->freed_extents[0],
			start, end, EXTENT_UPTODATE);
	set_extent_bits(&fs_info->freed_extents[1],
			start, end, EXTENT_UPTODATE);
	return 0;
}

static void free_excluded_extents(struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *cache)
{
	u64 start, end;

	start = cache->key.objectid;
	end = start + cache->key.offset - 1;

	clear_extent_bits(&fs_info->freed_extents[0],
			  start, end, EXTENT_UPTODATE);
	clear_extent_bits(&fs_info->freed_extents[1],
			  start, end, EXTENT_UPTODATE);
}

static int exclude_super_stripes(struct btrfs_fs_info *fs_info,
				 struct btrfs_block_group_cache *cache)
{
	u64 bytenr;
	u64 *logical;
	int stripe_len;
	int i, nr, ret;

	if (cache->key.objectid < BTRFS_SUPER_INFO_OFFSET) {
		stripe_len = BTRFS_SUPER_INFO_OFFSET - cache->key.objectid;
		cache->bytes_super += stripe_len;
		ret = add_excluded_extent(fs_info, cache->key.objectid,
					  stripe_len);
		if (ret)
			return ret;
	}

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		ret = btrfs_rmap_block(fs_info, cache->key.objectid,
				       bytenr, 0, &logical, &nr, &stripe_len);
		if (ret)
			return ret;

		while (nr--) {
			u64 start, len;

			if (logical[nr] > cache->key.objectid +
			    cache->key.offset)
				continue;

			if (logical[nr] + stripe_len <= cache->key.objectid)
				continue;

			start = logical[nr];
			if (start < cache->key.objectid) {
				start = cache->key.objectid;
				len = (logical[nr] + stripe_len) - start;
			} else {
				len = min_t(u64, stripe_len,
					    cache->key.objectid +
					    cache->key.offset - start);
			}

			cache->bytes_super += len;
			ret = add_excluded_extent(fs_info, start, len);
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
	if (!cache->caching_ctl) {
		spin_unlock(&cache->lock);
		return NULL;
	}

	ctl = cache->caching_ctl;
	refcount_inc(&ctl->count);
	spin_unlock(&cache->lock);
	return ctl;
}

static void put_caching_control(struct btrfs_caching_control *ctl)
{
	if (refcount_dec_and_test(&ctl->count))
		kfree(ctl);
}

#ifdef CONFIG_BTRFS_DEBUG
static void fragment_free_space(struct btrfs_block_group_cache *block_group)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	u64 start = block_group->key.objectid;
	u64 len = block_group->key.offset;
	u64 chunk = block_group->flags & BTRFS_BLOCK_GROUP_METADATA ?
		fs_info->nodesize : fs_info->sectorsize;
	u64 step = chunk << 1;

	while (len > chunk) {
		btrfs_remove_free_space(block_group, start, chunk);
		start += step;
		if (len < step)
			len = 0;
		else
			len -= step;
	}
}
#endif

/*
 * this is only called by cache_block_group, since we could have freed extents
 * we need to check the pinned_extents for any extents that can't be used yet
 * since their free space will be released as soon as the transaction commits.
 */
u64 add_new_free_space(struct btrfs_block_group_cache *block_group,
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

static int load_extent_tree_free(struct btrfs_caching_control *caching_ctl)
{
	struct btrfs_block_group_cache *block_group = caching_ctl->block_group;
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 total_found = 0;
	u64 last = 0;
	u32 nritems;
	int ret;
	bool wakeup = true;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	last = max_t(u64, block_group->key.objectid, BTRFS_SUPER_INFO_OFFSET);

#ifdef CONFIG_BTRFS_DEBUG
	/*
	 * If we're fragmenting we don't want to make anybody think we can
	 * allocate from this block group until we've had a chance to fragment
	 * the free space.
	 */
	if (btrfs_should_fragment_free_space(block_group))
		wakeup = false;
#endif
	/*
	 * We don't want to deadlock with somebody trying to allocate a new
	 * extent for the extent root while also trying to search the extent
	 * root to add free space.  So we skip locking and search the commit
	 * root, since its read-only
	 */
	path->skip_locking = 1;
	path->search_commit_root = 1;
	path->reada = READA_FORWARD;

	key.objectid = last;
	key.offset = 0;
	key.type = BTRFS_EXTENT_ITEM_KEY;

next:
	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

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
			    rwsem_is_contended(&fs_info->commit_root_sem)) {
				if (wakeup)
					caching_ctl->progress = last;
				btrfs_release_path(path);
				up_read(&fs_info->commit_root_sem);
				mutex_unlock(&caching_ctl->mutex);
				cond_resched();
				mutex_lock(&caching_ctl->mutex);
				down_read(&fs_info->commit_root_sem);
				goto next;
			}

			ret = btrfs_next_leaf(extent_root, path);
			if (ret < 0)
				goto out;
			if (ret)
				break;
			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
			continue;
		}

		if (key.objectid < last) {
			key.objectid = last;
			key.offset = 0;
			key.type = BTRFS_EXTENT_ITEM_KEY;

			if (wakeup)
				caching_ctl->progress = last;
			btrfs_release_path(path);
			goto next;
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
					fs_info->nodesize;
			else
				last = key.objectid + key.offset;

			if (total_found > CACHING_CTL_WAKE_UP) {
				total_found = 0;
				if (wakeup)
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

out:
	btrfs_free_path(path);
	return ret;
}

static noinline void caching_thread(struct btrfs_work *work)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_fs_info *fs_info;
	struct btrfs_caching_control *caching_ctl;
	int ret;

	caching_ctl = container_of(work, struct btrfs_caching_control, work);
	block_group = caching_ctl->block_group;
	fs_info = block_group->fs_info;

	mutex_lock(&caching_ctl->mutex);
	down_read(&fs_info->commit_root_sem);

	if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE))
		ret = load_free_space_tree(caching_ctl);
	else
		ret = load_extent_tree_free(caching_ctl);

	spin_lock(&block_group->lock);
	block_group->caching_ctl = NULL;
	block_group->cached = ret ? BTRFS_CACHE_ERROR : BTRFS_CACHE_FINISHED;
	spin_unlock(&block_group->lock);

#ifdef CONFIG_BTRFS_DEBUG
	if (btrfs_should_fragment_free_space(block_group)) {
		u64 bytes_used;

		spin_lock(&block_group->space_info->lock);
		spin_lock(&block_group->lock);
		bytes_used = block_group->key.offset -
			btrfs_block_group_used(&block_group->item);
		block_group->space_info->bytes_used += bytes_used >> 1;
		spin_unlock(&block_group->lock);
		spin_unlock(&block_group->space_info->lock);
		fragment_free_space(block_group);
	}
#endif

	caching_ctl->progress = (u64)-1;

	up_read(&fs_info->commit_root_sem);
	free_excluded_extents(fs_info, block_group);
	mutex_unlock(&caching_ctl->mutex);

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
	refcount_set(&caching_ctl->count, 1);
	btrfs_init_work(&caching_ctl->work, btrfs_cache_helper,
			caching_thread, NULL, NULL);

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
		refcount_inc(&ctl->count);
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

	if (btrfs_test_opt(fs_info, SPACE_CACHE)) {
		mutex_lock(&caching_ctl->mutex);
		ret = load_free_space_cache(fs_info, cache);

		spin_lock(&cache->lock);
		if (ret == 1) {
			cache->caching_ctl = NULL;
			cache->cached = BTRFS_CACHE_FINISHED;
			cache->last_byte_to_unpin = (u64)-1;
			caching_ctl->progress = (u64)-1;
		} else {
			if (load_cache_only) {
				cache->caching_ctl = NULL;
				cache->cached = BTRFS_CACHE_NO;
			} else {
				cache->cached = BTRFS_CACHE_STARTED;
				cache->has_caching_ctl = 1;
			}
		}
		spin_unlock(&cache->lock);
#ifdef CONFIG_BTRFS_DEBUG
		if (ret == 1 &&
		    btrfs_should_fragment_free_space(cache)) {
			u64 bytes_used;

			spin_lock(&cache->space_info->lock);
			spin_lock(&cache->lock);
			bytes_used = cache->key.offset -
				btrfs_block_group_used(&cache->item);
			cache->space_info->bytes_used += bytes_used >> 1;
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);
			fragment_free_space(cache);
		}
#endif
		mutex_unlock(&caching_ctl->mutex);

		wake_up(&caching_ctl->wait);
		if (ret == 1) {
			put_caching_control(caching_ctl);
			free_excluded_extents(fs_info, cache);
			return 0;
		}
	} else {
		/*
		 * We're either using the free space tree or no caching at all.
		 * Set cached to the appropriate value and wakeup any waiters.
		 */
		spin_lock(&cache->lock);
		if (load_cache_only) {
			cache->caching_ctl = NULL;
			cache->cached = BTRFS_CACHE_NO;
		} else {
			cache->cached = BTRFS_CACHE_STARTED;
			cache->has_caching_ctl = 1;
		}
		spin_unlock(&cache->lock);
		wake_up(&caching_ctl->wait);
	}

	if (load_cache_only) {
		put_caching_control(caching_ctl);
		return 0;
	}

	down_write(&fs_info->commit_root_sem);
	refcount_inc(&caching_ctl->count);
	list_add_tail(&caching_ctl->list, &fs_info->caching_block_groups);
	up_write(&fs_info->commit_root_sem);

	btrfs_get_block_group(cache);

	btrfs_queue_work(fs_info->caching_workers, &caching_ctl->work);

	return ret;
}

/*
 * return the block group that starts at or after bytenr
 */
static struct btrfs_block_group_cache *
btrfs_lookup_first_block_group(struct btrfs_fs_info *info, u64 bytenr)
{
	return block_group_cache_tree_search(info, bytenr, 0);
}

/*
 * return the block group that contains the given bytenr
 */
struct btrfs_block_group_cache *btrfs_lookup_block_group(
						 struct btrfs_fs_info *info,
						 u64 bytenr)
{
	return block_group_cache_tree_search(info, bytenr, 1);
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

static void add_pinned_bytes(struct btrfs_fs_info *fs_info, s64 num_bytes,
			     u64 owner, u64 root_objectid)
{
	struct btrfs_space_info *space_info;
	u64 flags;

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		if (root_objectid == BTRFS_CHUNK_TREE_OBJECTID)
			flags = BTRFS_BLOCK_GROUP_SYSTEM;
		else
			flags = BTRFS_BLOCK_GROUP_METADATA;
	} else {
		flags = BTRFS_BLOCK_GROUP_DATA;
	}

	space_info = __find_space_info(fs_info, flags);
	ASSERT(space_info);
	percpu_counter_add(&space_info->total_bytes_pinned, num_bytes);
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

/* simple helper to search for an existing data extent at a given offset */
int btrfs_lookup_data_extent(struct btrfs_fs_info *fs_info, u64 start, u64 len)
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
	ret = btrfs_search_slot(NULL, fs_info->extent_root, &key, path, 0, 0);
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
			     struct btrfs_fs_info *fs_info, u64 bytenr,
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
	if (metadata && !btrfs_fs_incompat(fs_info, SKINNY_METADATA)) {
		offset = fs_info->nodesize;
		metadata = 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (!trans) {
		path->skip_locking = 1;
		path->search_commit_root = 1;
	}

search_again:
	key.objectid = bytenr;
	key.offset = offset;
	if (metadata)
		key.type = BTRFS_METADATA_ITEM_KEY;
	else
		key.type = BTRFS_EXTENT_ITEM_KEY;

	ret = btrfs_search_slot(trans, fs_info->extent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out_free;

	if (ret > 0 && metadata && key.type == BTRFS_METADATA_ITEM_KEY) {
		if (path->slots[0]) {
			path->slots[0]--;
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == bytenr &&
			    key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == fs_info->nodesize)
				ret = 0;
		}
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
	head = btrfs_find_delayed_ref_head(delayed_refs, bytenr);
	if (head) {
		if (!mutex_trylock(&head->mutex)) {
			refcount_inc(&head->refs);
			spin_unlock(&delayed_refs->lock);

			btrfs_release_path(path);

			/*
			 * Mutex was contended, block until it's released and try
			 * again
			 */
			mutex_lock(&head->mutex);
			mutex_unlock(&head->mutex);
			btrfs_put_delayed_ref_head(head);
			goto search_again;
		}
		spin_lock(&head->lock);
		if (head->extent_op && head->extent_op->update_flags)
			extent_flags |= head->extent_op->flags_to_set;
		else
			BUG_ON(num_refs == 0);

		num_refs += head->ref_mod;
		spin_unlock(&head->lock);
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
 * When a tree block is COWed through a tree, there are four cases:
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
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_path *path,
				  u64 owner, u32 extra_size)
{
	struct btrfs_root *root = fs_info->extent_root;
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

	btrfs_extend_item(fs_info, path, new_size);

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
		memzero_extent_buffer(leaf, (unsigned long)bi, sizeof(*bi));
		btrfs_set_tree_block_level(leaf, bi, (int)owner);
	} else {
		btrfs_set_extent_flags(leaf, item, BTRFS_EXTENT_FLAG_DATA);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
}
#endif

/*
 * is_data == BTRFS_REF_TYPE_BLOCK, tree block type is required,
 * is_data == BTRFS_REF_TYPE_DATA, data type is requried,
 * is_data == BTRFS_REF_TYPE_ANY, either type is OK.
 */
int btrfs_get_extent_inline_ref_type(const struct extent_buffer *eb,
				     struct btrfs_extent_inline_ref *iref,
				     enum btrfs_inline_ref_type is_data)
{
	int type = btrfs_extent_inline_ref_type(eb, iref);
	u64 offset = btrfs_extent_inline_ref_offset(eb, iref);

	if (type == BTRFS_TREE_BLOCK_REF_KEY ||
	    type == BTRFS_SHARED_BLOCK_REF_KEY ||
	    type == BTRFS_SHARED_DATA_REF_KEY ||
	    type == BTRFS_EXTENT_DATA_REF_KEY) {
		if (is_data == BTRFS_REF_TYPE_BLOCK) {
			if (type == BTRFS_TREE_BLOCK_REF_KEY)
				return type;
			if (type == BTRFS_SHARED_BLOCK_REF_KEY) {
				ASSERT(eb->fs_info);
				/*
				 * Every shared one has parent tree
				 * block, which must be aligned to
				 * nodesize.
				 */
				if (offset &&
				    IS_ALIGNED(offset, eb->fs_info->nodesize))
					return type;
			}
		} else if (is_data == BTRFS_REF_TYPE_DATA) {
			if (type == BTRFS_EXTENT_DATA_REF_KEY)
				return type;
			if (type == BTRFS_SHARED_DATA_REF_KEY) {
				ASSERT(eb->fs_info);
				/*
				 * Every shared one has parent tree
				 * block, which must be aligned to
				 * nodesize.
				 */
				if (offset &&
				    IS_ALIGNED(offset, eb->fs_info->nodesize))
					return type;
			}
		} else {
			ASSERT(is_data == BTRFS_REF_TYPE_ANY);
			return type;
		}
	}

	btrfs_print_leaf((struct extent_buffer *)eb);
	btrfs_err(eb->fs_info, "eb %llu invalid extent inline ref type %d",
		  eb->start, type);
	WARN_ON(1);

	return BTRFS_REF_TYPE_INVALID;
}

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
					   struct btrfs_fs_info *fs_info,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid,
					   u64 owner, u64 offset)
{
	struct btrfs_root *root = fs_info->extent_root;
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
					   struct btrfs_fs_info *fs_info,
					   struct btrfs_path *path,
					   u64 bytenr, u64 parent,
					   u64 root_objectid, u64 owner,
					   u64 offset, int refs_to_add)
{
	struct btrfs_root *root = fs_info->extent_root;
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
					   struct btrfs_fs_info *fs_info,
					   struct btrfs_path *path,
					   int refs_to_drop, int *last_ref)
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
		ret = btrfs_del_item(trans, fs_info->extent_root, path);
		*last_ref = 1;
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

static noinline u32 extent_data_ref_count(struct btrfs_path *path,
					  struct btrfs_extent_inline_ref *iref)
{
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_extent_data_ref *ref1;
	struct btrfs_shared_data_ref *ref2;
	u32 num_refs = 0;
	int type;

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (iref) {
		/*
		 * If type is invalid, we should have bailed out earlier than
		 * this call.
		 */
		type = btrfs_get_extent_inline_ref_type(leaf, iref, BTRFS_REF_TYPE_DATA);
		ASSERT(type != BTRFS_REF_TYPE_INVALID);
		if (type == BTRFS_EXTENT_DATA_REF_KEY) {
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
					  struct btrfs_fs_info *fs_info,
					  struct btrfs_path *path,
					  u64 bytenr, u64 parent,
					  u64 root_objectid)
{
	struct btrfs_root *root = fs_info->extent_root;
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
					  struct btrfs_fs_info *fs_info,
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

	ret = btrfs_insert_empty_item(trans, fs_info->extent_root,
				      path, &key, 0);
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
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes,
				 u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int insert)
{
	struct btrfs_root *root = fs_info->extent_root;
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
	bool skinny_metadata = btrfs_fs_incompat(fs_info, SKINNY_METADATA);
	int needed;

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
			key.objectid = bytenr;
			key.type = BTRFS_EXTENT_ITEM_KEY;
			key.offset = num_bytes;
			btrfs_release_path(path);
			goto again;
		}
	}

	if (ret && !insert) {
		err = -ENOENT;
		goto out;
	} else if (WARN_ON(ret)) {
		err = -EIO;
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
		ret = convert_extent_item_v0(trans, fs_info, path, owner,
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

	if (owner >= BTRFS_FIRST_FREE_OBJECTID)
		needed = BTRFS_REF_TYPE_DATA;
	else
		needed = BTRFS_REF_TYPE_BLOCK;

	err = -ENOENT;
	while (1) {
		if (ptr >= end) {
			WARN_ON(ptr > end);
			break;
		}
		iref = (struct btrfs_extent_inline_ref *)ptr;
		type = btrfs_get_extent_inline_ref_type(leaf, iref, needed);
		if (type == BTRFS_REF_TYPE_INVALID) {
			err = -EINVAL;
			goto out;
		}

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
void setup_inline_extent_backref(struct btrfs_fs_info *fs_info,
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

	btrfs_extend_item(fs_info, path, size);

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
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref **ref_ret,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner, u64 offset)
{
	int ret;

	ret = lookup_inline_extent_backref(trans, fs_info, path, ref_ret,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 0);
	if (ret != -ENOENT)
		return ret;

	btrfs_release_path(path);
	*ref_ret = NULL;

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = lookup_tree_block_ref(trans, fs_info, path, bytenr,
					    parent, root_objectid);
	} else {
		ret = lookup_extent_data_ref(trans, fs_info, path, bytenr,
					     parent, root_objectid, owner,
					     offset);
	}
	return ret;
}

/*
 * helper to update/remove inline back ref
 */
static noinline_for_stack
void update_inline_extent_backref(struct btrfs_fs_info *fs_info,
				  struct btrfs_path *path,
				  struct btrfs_extent_inline_ref *iref,
				  int refs_to_mod,
				  struct btrfs_delayed_extent_op *extent_op,
				  int *last_ref)
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

	/*
	 * If type is invalid, we should have bailed out after
	 * lookup_inline_extent_backref().
	 */
	type = btrfs_get_extent_inline_ref_type(leaf, iref, BTRFS_REF_TYPE_ANY);
	ASSERT(type != BTRFS_REF_TYPE_INVALID);

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
		*last_ref = 1;
		size =  btrfs_extent_inline_ref_size(type);
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		ptr = (unsigned long)iref;
		end = (unsigned long)ei + item_size;
		if (ptr + size < end)
			memmove_extent_buffer(leaf, ptr, ptr + size,
					      end - ptr - size);
		item_size -= size;
		btrfs_truncate_item(fs_info, path, item_size, 1);
	}
	btrfs_mark_buffer_dirty(leaf);
}

static noinline_for_stack
int insert_inline_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_path *path,
				 u64 bytenr, u64 num_bytes, u64 parent,
				 u64 root_objectid, u64 owner,
				 u64 offset, int refs_to_add,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_extent_inline_ref *iref;
	int ret;

	ret = lookup_inline_extent_backref(trans, fs_info, path, &iref,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset, 1);
	if (ret == 0) {
		BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID);
		update_inline_extent_backref(fs_info, path, iref,
					     refs_to_add, extent_op, NULL);
	} else if (ret == -ENOENT) {
		setup_inline_extent_backref(fs_info, path, iref, parent,
					    root_objectid, owner, offset,
					    refs_to_add, extent_op);
		ret = 0;
	}
	return ret;
}

static int insert_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_path *path,
				 u64 bytenr, u64 parent, u64 root_objectid,
				 u64 owner, u64 offset, int refs_to_add)
{
	int ret;
	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		BUG_ON(refs_to_add != 1);
		ret = insert_tree_block_ref(trans, fs_info, path, bytenr,
					    parent, root_objectid);
	} else {
		ret = insert_extent_data_ref(trans, fs_info, path, bytenr,
					     parent, root_objectid,
					     owner, offset, refs_to_add);
	}
	return ret;
}

static int remove_extent_backref(struct btrfs_trans_handle *trans,
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_path *path,
				 struct btrfs_extent_inline_ref *iref,
				 int refs_to_drop, int is_data, int *last_ref)
{
	int ret = 0;

	BUG_ON(!is_data && refs_to_drop != 1);
	if (iref) {
		update_inline_extent_backref(fs_info, path, iref,
					     -refs_to_drop, NULL, last_ref);
	} else if (is_data) {
		ret = remove_extent_data_ref(trans, fs_info, path, refs_to_drop,
					     last_ref);
	} else {
		*last_ref = 1;
		ret = btrfs_del_item(trans, fs_info->extent_root, path);
	}
	return ret;
}

#define in_range(b, first, len)        ((b) >= (first) && (b) < (first) + (len))
static int btrfs_issue_discard(struct block_device *bdev, u64 start, u64 len,
			       u64 *discarded_bytes)
{
	int j, ret = 0;
	u64 bytes_left, end;
	u64 aligned_start = ALIGN(start, 1 << 9);

	if (WARN_ON(start != aligned_start)) {
		len -= aligned_start - start;
		len = round_down(len, 1 << 9);
		start = aligned_start;
	}

	*discarded_bytes = 0;

	if (!len)
		return 0;

	end = start + len;
	bytes_left = len;

	/* Skip any superblocks on this device. */
	for (j = 0; j < BTRFS_SUPER_MIRROR_MAX; j++) {
		u64 sb_start = btrfs_sb_offset(j);
		u64 sb_end = sb_start + BTRFS_SUPER_INFO_SIZE;
		u64 size = sb_start - start;

		if (!in_range(sb_start, start, bytes_left) &&
		    !in_range(sb_end, start, bytes_left) &&
		    !in_range(start, sb_start, BTRFS_SUPER_INFO_SIZE))
			continue;

		/*
		 * Superblock spans beginning of range.  Adjust start and
		 * try again.
		 */
		if (sb_start <= start) {
			start += sb_end - start;
			if (start > end) {
				bytes_left = 0;
				break;
			}
			bytes_left = end - start;
			continue;
		}

		if (size) {
			ret = blkdev_issue_discard(bdev, start >> 9, size >> 9,
						   GFP_NOFS, 0);
			if (!ret)
				*discarded_bytes += size;
			else if (ret != -EOPNOTSUPP)
				return ret;
		}

		start = sb_end;
		if (start > end) {
			bytes_left = 0;
			break;
		}
		bytes_left = end - start;
	}

	if (bytes_left) {
		ret = blkdev_issue_discard(bdev, start >> 9, bytes_left >> 9,
					   GFP_NOFS, 0);
		if (!ret)
			*discarded_bytes += bytes_left;
	}
	return ret;
}

int btrfs_discard_extent(struct btrfs_fs_info *fs_info, u64 bytenr,
			 u64 num_bytes, u64 *actual_bytes)
{
	int ret;
	u64 discarded_bytes = 0;
	struct btrfs_bio *bbio = NULL;


	/*
	 * Avoid races with device replace and make sure our bbio has devices
	 * associated to its stripes that don't go away while we are discarding.
	 */
	btrfs_bio_counter_inc_blocked(fs_info);
	/* Tell the block device(s) that the sectors can be discarded */
	ret = btrfs_map_block(fs_info, BTRFS_MAP_DISCARD, bytenr, &num_bytes,
			      &bbio, 0);
	/* Error condition is -ENOMEM */
	if (!ret) {
		struct btrfs_bio_stripe *stripe = bbio->stripes;
		int i;


		for (i = 0; i < bbio->num_stripes; i++, stripe++) {
			u64 bytes;
			struct request_queue *req_q;

			if (!stripe->dev->bdev) {
				ASSERT(btrfs_test_opt(fs_info, DEGRADED));
				continue;
			}
			req_q = bdev_get_queue(stripe->dev->bdev);
			if (!blk_queue_discard(req_q))
				continue;

			ret = btrfs_issue_discard(stripe->dev->bdev,
						  stripe->physical,
						  stripe->length,
						  &bytes);
			if (!ret)
				discarded_bytes += bytes;
			else if (ret != -EOPNOTSUPP)
				break; /* Logic errors or -ENOMEM, or -EIO but I don't know how that could happen JDM */

			/*
			 * Just in case we get back EOPNOTSUPP for some reason,
			 * just ignore the return value so we don't screw up
			 * people calling discard_extent.
			 */
			ret = 0;
		}
		btrfs_put_bbio(bbio);
	}
	btrfs_bio_counter_dec(fs_info);

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
			 u64 root_objectid, u64 owner, u64 offset)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int old_ref_mod, new_ref_mod;
	int ret;

	BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID &&
	       root_objectid == BTRFS_TREE_LOG_OBJECTID);

	btrfs_ref_tree_mod(root, bytenr, num_bytes, parent, root_objectid,
			   owner, offset, BTRFS_ADD_DELAYED_REF);

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(fs_info, trans, bytenr,
						 num_bytes, parent,
						 root_objectid, (int)owner,
						 BTRFS_ADD_DELAYED_REF, NULL,
						 &old_ref_mod, &new_ref_mod);
	} else {
		ret = btrfs_add_delayed_data_ref(fs_info, trans, bytenr,
						 num_bytes, parent,
						 root_objectid, owner, offset,
						 0, BTRFS_ADD_DELAYED_REF,
						 &old_ref_mod, &new_ref_mod);
	}

	if (ret == 0 && old_ref_mod < 0 && new_ref_mod >= 0)
		add_pinned_bytes(fs_info, -num_bytes, owner, root_objectid);

	return ret;
}

static int __btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_delayed_ref_node *node,
				  u64 parent, u64 root_objectid,
				  u64 owner, u64 offset, int refs_to_add,
				  struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_extent_item *item;
	struct btrfs_key key;
	u64 bytenr = node->bytenr;
	u64 num_bytes = node->num_bytes;
	u64 refs;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = READA_FORWARD;
	path->leave_spinning = 1;
	/* this will setup the path even if it fails to insert the back ref */
	ret = insert_inline_extent_backref(trans, fs_info, path, bytenr,
					   num_bytes, parent, root_objectid,
					   owner, offset,
					   refs_to_add, extent_op);
	if ((ret < 0 && ret != -EAGAIN) || !ret)
		goto out;

	/*
	 * Ok we had -EAGAIN which means we didn't have space to insert and
	 * inline extent ref, so just update the reference count and add a
	 * normal backref.
	 */
	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	refs = btrfs_extent_refs(leaf, item);
	btrfs_set_extent_refs(leaf, item, refs + refs_to_add);
	if (extent_op)
		__run_delayed_extent_op(extent_op, leaf, item);

	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(path);

	path->reada = READA_FORWARD;
	path->leave_spinning = 1;
	/* now insert the actual backref */
	ret = insert_extent_backref(trans, fs_info, path, bytenr, parent,
				    root_objectid, owner, offset, refs_to_add);
	if (ret)
		btrfs_abort_transaction(trans, ret);
out:
	btrfs_free_path(path);
	return ret;
}

static int run_delayed_data_ref(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info,
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
	trace_run_delayed_data_ref(fs_info, node, ref, node->action);

	if (node->type == BTRFS_SHARED_DATA_REF_KEY)
		parent = ref->parent;
	ref_root = ref->root;

	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		if (extent_op)
			flags |= extent_op->flags_to_set;
		ret = alloc_reserved_file_extent(trans, fs_info,
						 parent, ref_root, flags,
						 ref->objectid, ref->offset,
						 &ins, node->ref_mod);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, fs_info, node, parent,
					     ref_root, ref->objectid,
					     ref->offset, node->ref_mod,
					     extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, fs_info, node, parent,
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
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_delayed_ref_head *head,
				 struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_key key;
	struct btrfs_path *path;
	struct btrfs_extent_item *ei;
	struct extent_buffer *leaf;
	u32 item_size;
	int ret;
	int err = 0;
	int metadata = !extent_op->is_data;

	if (trans->aborted)
		return 0;

	if (metadata && !btrfs_fs_incompat(fs_info, SKINNY_METADATA))
		metadata = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = head->bytenr;

	if (metadata) {
		key.type = BTRFS_METADATA_ITEM_KEY;
		key.offset = extent_op->level;
	} else {
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = head->num_bytes;
	}

again:
	path->reada = READA_FORWARD;
	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, fs_info->extent_root, &key, path, 0, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	if (ret > 0) {
		if (metadata) {
			if (path->slots[0] > 0) {
				path->slots[0]--;
				btrfs_item_key_to_cpu(path->nodes[0], &key,
						      path->slots[0]);
				if (key.objectid == head->bytenr &&
				    key.type == BTRFS_EXTENT_ITEM_KEY &&
				    key.offset == head->num_bytes)
					ret = 0;
			}
			if (ret > 0) {
				btrfs_release_path(path);
				metadata = 0;

				key.objectid = head->bytenr;
				key.offset = head->num_bytes;
				key.type = BTRFS_EXTENT_ITEM_KEY;
				goto again;
			}
		} else {
			err = -EIO;
			goto out;
		}
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		ret = convert_extent_item_v0(trans, fs_info, path, (u64)-1, 0);
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
				struct btrfs_fs_info *fs_info,
				struct btrfs_delayed_ref_node *node,
				struct btrfs_delayed_extent_op *extent_op,
				int insert_reserved)
{
	int ret = 0;
	struct btrfs_delayed_tree_ref *ref;
	struct btrfs_key ins;
	u64 parent = 0;
	u64 ref_root = 0;
	bool skinny_metadata = btrfs_fs_incompat(fs_info, SKINNY_METADATA);

	ref = btrfs_delayed_node_to_tree_ref(node);
	trace_run_delayed_tree_ref(fs_info, node, ref, node->action);

	if (node->type == BTRFS_SHARED_BLOCK_REF_KEY)
		parent = ref->parent;
	ref_root = ref->root;

	ins.objectid = node->bytenr;
	if (skinny_metadata) {
		ins.offset = ref->level;
		ins.type = BTRFS_METADATA_ITEM_KEY;
	} else {
		ins.offset = node->num_bytes;
		ins.type = BTRFS_EXTENT_ITEM_KEY;
	}

	if (node->ref_mod != 1) {
		btrfs_err(fs_info,
	"btree block(%llu) has %d references rather than 1: action %d ref_root %llu parent %llu",
			  node->bytenr, node->ref_mod, node->action, ref_root,
			  parent);
		return -EIO;
	}
	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		BUG_ON(!extent_op || !extent_op->update_flags);
		ret = alloc_reserved_tree_block(trans, fs_info,
						parent, ref_root,
						extent_op->flags_to_set,
						&extent_op->key,
						ref->level, &ins);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, fs_info, node,
					     parent, ref_root,
					     ref->level, 0, 1,
					     extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, fs_info, node,
					  parent, ref_root,
					  ref->level, 0, 1, extent_op);
	} else {
		BUG();
	}
	return ret;
}

/* helper function to actually process a single delayed ref entry */
static int run_one_delayed_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_fs_info *fs_info,
			       struct btrfs_delayed_ref_node *node,
			       struct btrfs_delayed_extent_op *extent_op,
			       int insert_reserved)
{
	int ret = 0;

	if (trans->aborted) {
		if (insert_reserved)
			btrfs_pin_extent(fs_info, node->bytenr,
					 node->num_bytes, 1);
		return 0;
	}

	if (node->type == BTRFS_TREE_BLOCK_REF_KEY ||
	    node->type == BTRFS_SHARED_BLOCK_REF_KEY)
		ret = run_delayed_tree_ref(trans, fs_info, node, extent_op,
					   insert_reserved);
	else if (node->type == BTRFS_EXTENT_DATA_REF_KEY ||
		 node->type == BTRFS_SHARED_DATA_REF_KEY)
		ret = run_delayed_data_ref(trans, fs_info, node, extent_op,
					   insert_reserved);
	else
		BUG();
	return ret;
}

static inline struct btrfs_delayed_ref_node *
select_delayed_ref(struct btrfs_delayed_ref_head *head)
{
	struct btrfs_delayed_ref_node *ref;

	if (RB_EMPTY_ROOT(&head->ref_tree))
		return NULL;

	/*
	 * Select a delayed ref of type BTRFS_ADD_DELAYED_REF first.
	 * This is to prevent a ref count from going down to zero, which deletes
	 * the extent item from the extent tree, when there still are references
	 * to add, which would fail because they would not find the extent item.
	 */
	if (!list_empty(&head->ref_add_list))
		return list_first_entry(&head->ref_add_list,
				struct btrfs_delayed_ref_node, add_list);

	ref = rb_entry(rb_first(&head->ref_tree),
		       struct btrfs_delayed_ref_node, ref_node);
	ASSERT(list_empty(&ref->add_list));
	return ref;
}

static void unselect_delayed_ref_head(struct btrfs_delayed_ref_root *delayed_refs,
				      struct btrfs_delayed_ref_head *head)
{
	spin_lock(&delayed_refs->lock);
	head->processing = 0;
	delayed_refs->num_heads_ready++;
	spin_unlock(&delayed_refs->lock);
	btrfs_delayed_ref_unlock(head);
}

static int cleanup_extent_op(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info,
			     struct btrfs_delayed_ref_head *head)
{
	struct btrfs_delayed_extent_op *extent_op = head->extent_op;
	int ret;

	if (!extent_op)
		return 0;
	head->extent_op = NULL;
	if (head->must_insert_reserved) {
		btrfs_free_delayed_extent_op(extent_op);
		return 0;
	}
	spin_unlock(&head->lock);
	ret = run_delayed_extent_op(trans, fs_info, head, extent_op);
	btrfs_free_delayed_extent_op(extent_op);
	return ret ? ret : 1;
}

static int cleanup_ref_head(struct btrfs_trans_handle *trans,
			    struct btrfs_fs_info *fs_info,
			    struct btrfs_delayed_ref_head *head)
{
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret;

	delayed_refs = &trans->transaction->delayed_refs;

	ret = cleanup_extent_op(trans, fs_info, head);
	if (ret < 0) {
		unselect_delayed_ref_head(delayed_refs, head);
		btrfs_debug(fs_info, "run_delayed_extent_op returned %d", ret);
		return ret;
	} else if (ret) {
		return ret;
	}

	/*
	 * Need to drop our head ref lock and re-acquire the delayed ref lock
	 * and then re-check to make sure nobody got added.
	 */
	spin_unlock(&head->lock);
	spin_lock(&delayed_refs->lock);
	spin_lock(&head->lock);
	if (!RB_EMPTY_ROOT(&head->ref_tree) || head->extent_op) {
		spin_unlock(&head->lock);
		spin_unlock(&delayed_refs->lock);
		return 1;
	}
	delayed_refs->num_heads--;
	rb_erase(&head->href_node, &delayed_refs->href_root);
	RB_CLEAR_NODE(&head->href_node);
	spin_unlock(&delayed_refs->lock);
	spin_unlock(&head->lock);
	atomic_dec(&delayed_refs->num_entries);

	trace_run_delayed_ref_head(fs_info, head, 0);

	if (head->total_ref_mod < 0) {
		struct btrfs_block_group_cache *cache;

		cache = btrfs_lookup_block_group(fs_info, head->bytenr);
		ASSERT(cache);
		percpu_counter_add(&cache->space_info->total_bytes_pinned,
				   -head->num_bytes);
		btrfs_put_block_group(cache);

		if (head->is_data) {
			spin_lock(&delayed_refs->lock);
			delayed_refs->pending_csums -= head->num_bytes;
			spin_unlock(&delayed_refs->lock);
		}
	}

	if (head->must_insert_reserved) {
		btrfs_pin_extent(fs_info, head->bytenr,
				 head->num_bytes, 1);
		if (head->is_data) {
			ret = btrfs_del_csums(trans, fs_info, head->bytenr,
					      head->num_bytes);
		}
	}

	/* Also free its reserved qgroup space */
	btrfs_qgroup_free_delayed_ref(fs_info, head->qgroup_ref_root,
				      head->qgroup_reserved);
	btrfs_delayed_ref_unlock(head);
	btrfs_put_delayed_ref_head(head);
	return 0;
}

/*
 * Returns 0 on success or if called with an already aborted transaction.
 * Returns -ENOMEM or -EIO on failure and will abort the transaction.
 */
static noinline int __btrfs_run_delayed_refs(struct btrfs_trans_handle *trans,
					     unsigned long nr)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_head *locked_ref = NULL;
	struct btrfs_delayed_extent_op *extent_op;
	ktime_t start = ktime_get();
	int ret;
	unsigned long count = 0;
	unsigned long actual_count = 0;
	int must_insert_reserved = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	while (1) {
		if (!locked_ref) {
			if (count >= nr)
				break;

			spin_lock(&delayed_refs->lock);
			locked_ref = btrfs_select_ref_head(trans);
			if (!locked_ref) {
				spin_unlock(&delayed_refs->lock);
				break;
			}

			/* grab the lock that says we are going to process
			 * all the refs for this head */
			ret = btrfs_delayed_ref_lock(trans, locked_ref);
			spin_unlock(&delayed_refs->lock);
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
		 * Or we can get node references of the same type that weren't
		 * merged when created due to bumps in the tree mod seq, and
		 * we need to merge them to prevent adding an inline extent
		 * backref before dropping it (triggering a BUG_ON at
		 * insert_inline_extent_backref()).
		 */
		spin_lock(&locked_ref->lock);
		btrfs_merge_delayed_refs(trans, fs_info, delayed_refs,
					 locked_ref);

		/*
		 * locked_ref is the head node, so we have to go one
		 * node back for any delayed ref updates
		 */
		ref = select_delayed_ref(locked_ref);

		if (ref && ref->seq &&
		    btrfs_check_delayed_seq(fs_info, delayed_refs, ref->seq)) {
			spin_unlock(&locked_ref->lock);
			unselect_delayed_ref_head(delayed_refs, locked_ref);
			locked_ref = NULL;
			cond_resched();
			count++;
			continue;
		}

		/*
		 * We're done processing refs in this ref_head, clean everything
		 * up and move on to the next ref_head.
		 */
		if (!ref) {
			ret = cleanup_ref_head(trans, fs_info, locked_ref);
			if (ret > 0 ) {
				/* We dropped our lock, we need to loop. */
				ret = 0;
				continue;
			} else if (ret) {
				return ret;
			}
			locked_ref = NULL;
			count++;
			continue;
		}

		actual_count++;
		ref->in_tree = 0;
		rb_erase(&ref->ref_node, &locked_ref->ref_tree);
		RB_CLEAR_NODE(&ref->ref_node);
		if (!list_empty(&ref->add_list))
			list_del(&ref->add_list);
		/*
		 * When we play the delayed ref, also correct the ref_mod on
		 * head
		 */
		switch (ref->action) {
		case BTRFS_ADD_DELAYED_REF:
		case BTRFS_ADD_DELAYED_EXTENT:
			locked_ref->ref_mod -= ref->ref_mod;
			break;
		case BTRFS_DROP_DELAYED_REF:
			locked_ref->ref_mod += ref->ref_mod;
			break;
		default:
			WARN_ON(1);
		}
		atomic_dec(&delayed_refs->num_entries);

		/*
		 * Record the must-insert_reserved flag before we drop the spin
		 * lock.
		 */
		must_insert_reserved = locked_ref->must_insert_reserved;
		locked_ref->must_insert_reserved = 0;

		extent_op = locked_ref->extent_op;
		locked_ref->extent_op = NULL;
		spin_unlock(&locked_ref->lock);

		ret = run_one_delayed_ref(trans, fs_info, ref, extent_op,
					  must_insert_reserved);

		btrfs_free_delayed_extent_op(extent_op);
		if (ret) {
			unselect_delayed_ref_head(delayed_refs, locked_ref);
			btrfs_put_delayed_ref(ref);
			btrfs_debug(fs_info, "run_one_delayed_ref returned %d",
				    ret);
			return ret;
		}

		btrfs_put_delayed_ref(ref);
		count++;
		cond_resched();
	}

	/*
	 * We don't want to include ref heads since we can have empty ref heads
	 * and those will drastically skew our runtime down since we just do
	 * accounting, no actual extent tree updates.
	 */
	if (actual_count > 0) {
		u64 runtime = ktime_to_ns(ktime_sub(ktime_get(), start));
		u64 avg;

		/*
		 * We weigh the current average higher than our current runtime
		 * to avoid large swings in the average.
		 */
		spin_lock(&delayed_refs->lock);
		avg = fs_info->avg_delayed_ref_runtime * 3 + runtime;
		fs_info->avg_delayed_ref_runtime = avg >> 2;	/* div by 4 */
		spin_unlock(&delayed_refs->lock);
	}
	return 0;
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

static inline u64 heads_to_leaves(struct btrfs_fs_info *fs_info, u64 heads)
{
	u64 num_bytes;

	num_bytes = heads * (sizeof(struct btrfs_extent_item) +
			     sizeof(struct btrfs_extent_inline_ref));
	if (!btrfs_fs_incompat(fs_info, SKINNY_METADATA))
		num_bytes += heads * sizeof(struct btrfs_tree_block_info);

	/*
	 * We don't ever fill up leaves all the way so multiply by 2 just to be
	 * closer to what we're really going to want to use.
	 */
	return div_u64(num_bytes, BTRFS_LEAF_DATA_SIZE(fs_info));
}

/*
 * Takes the number of bytes to be csumm'ed and figures out how many leaves it
 * would require to store the csums for that many bytes.
 */
u64 btrfs_csum_bytes_to_leaves(struct btrfs_fs_info *fs_info, u64 csum_bytes)
{
	u64 csum_size;
	u64 num_csums_per_leaf;
	u64 num_csums;

	csum_size = BTRFS_MAX_ITEM_SIZE(fs_info);
	num_csums_per_leaf = div64_u64(csum_size,
			(u64)btrfs_super_csum_size(fs_info->super_copy));
	num_csums = div64_u64(csum_bytes, fs_info->sectorsize);
	num_csums += num_csums_per_leaf - 1;
	num_csums = div64_u64(num_csums, num_csums_per_leaf);
	return num_csums;
}

int btrfs_check_space_for_delayed_refs(struct btrfs_trans_handle *trans,
				       struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *global_rsv;
	u64 num_heads = trans->transaction->delayed_refs.num_heads_ready;
	u64 csum_bytes = trans->transaction->delayed_refs.pending_csums;
	unsigned int num_dirty_bgs = trans->transaction->num_dirty_bgs;
	u64 num_bytes, num_dirty_bgs_bytes;
	int ret = 0;

	num_bytes = btrfs_calc_trans_metadata_size(fs_info, 1);
	num_heads = heads_to_leaves(fs_info, num_heads);
	if (num_heads > 1)
		num_bytes += (num_heads - 1) * fs_info->nodesize;
	num_bytes <<= 1;
	num_bytes += btrfs_csum_bytes_to_leaves(fs_info, csum_bytes) *
							fs_info->nodesize;
	num_dirty_bgs_bytes = btrfs_calc_trans_metadata_size(fs_info,
							     num_dirty_bgs);
	global_rsv = &fs_info->global_block_rsv;

	/*
	 * If we can't allocate any more chunks lets make sure we have _lots_ of
	 * wiggle room since running delayed refs can create more delayed refs.
	 */
	if (global_rsv->space_info->full) {
		num_dirty_bgs_bytes <<= 1;
		num_bytes <<= 1;
	}

	spin_lock(&global_rsv->lock);
	if (global_rsv->reserved <= num_bytes + num_dirty_bgs_bytes)
		ret = 1;
	spin_unlock(&global_rsv->lock);
	return ret;
}

int btrfs_should_throttle_delayed_refs(struct btrfs_trans_handle *trans,
				       struct btrfs_fs_info *fs_info)
{
	u64 num_entries =
		atomic_read(&trans->transaction->delayed_refs.num_entries);
	u64 avg_runtime;
	u64 val;

	smp_mb();
	avg_runtime = fs_info->avg_delayed_ref_runtime;
	val = num_entries * avg_runtime;
	if (val >= NSEC_PER_SEC)
		return 1;
	if (val >= NSEC_PER_SEC / 2)
		return 2;

	return btrfs_check_space_for_delayed_refs(trans, fs_info);
}

struct async_delayed_refs {
	struct btrfs_root *root;
	u64 transid;
	int count;
	int error;
	int sync;
	struct completion wait;
	struct btrfs_work work;
};

static inline struct async_delayed_refs *
to_async_delayed_refs(struct btrfs_work *work)
{
	return container_of(work, struct async_delayed_refs, work);
}

static void delayed_ref_async_start(struct btrfs_work *work)
{
	struct async_delayed_refs *async = to_async_delayed_refs(work);
	struct btrfs_trans_handle *trans;
	struct btrfs_fs_info *fs_info = async->root->fs_info;
	int ret;

	/* if the commit is already started, we don't need to wait here */
	if (btrfs_transaction_blocked(fs_info))
		goto done;

	trans = btrfs_join_transaction(async->root);
	if (IS_ERR(trans)) {
		async->error = PTR_ERR(trans);
		goto done;
	}

	/*
	 * trans->sync means that when we call end_transaction, we won't
	 * wait on delayed refs
	 */
	trans->sync = true;

	/* Don't bother flushing if we got into a different transaction */
	if (trans->transid > async->transid)
		goto end;

	ret = btrfs_run_delayed_refs(trans, async->count);
	if (ret)
		async->error = ret;
end:
	ret = btrfs_end_transaction(trans);
	if (ret && !async->error)
		async->error = ret;
done:
	if (async->sync)
		complete(&async->wait);
	else
		kfree(async);
}

int btrfs_async_run_delayed_refs(struct btrfs_fs_info *fs_info,
				 unsigned long count, u64 transid, int wait)
{
	struct async_delayed_refs *async;
	int ret;

	async = kmalloc(sizeof(*async), GFP_NOFS);
	if (!async)
		return -ENOMEM;

	async->root = fs_info->tree_root;
	async->count = count;
	async->error = 0;
	async->transid = transid;
	if (wait)
		async->sync = 1;
	else
		async->sync = 0;
	init_completion(&async->wait);

	btrfs_init_work(&async->work, btrfs_extent_refs_helper,
			delayed_ref_async_start, NULL, NULL);

	btrfs_queue_work(fs_info->extent_workers, &async->work);

	if (wait) {
		wait_for_completion(&async->wait);
		ret = async->error;
		kfree(async);
		return ret;
	}
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
			   unsigned long count)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct rb_node *node;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_head *head;
	int ret;
	int run_all = count == (unsigned long)-1;
	bool can_flush_pending_bgs = trans->can_flush_pending_bgs;

	/* We'll clean this up in btrfs_cleanup_transaction */
	if (trans->aborted)
		return 0;

	if (test_bit(BTRFS_FS_CREATING_FREE_SPACE_TREE, &fs_info->flags))
		return 0;

	delayed_refs = &trans->transaction->delayed_refs;
	if (count == 0)
		count = atomic_read(&delayed_refs->num_entries) * 2;

again:
#ifdef SCRAMBLE_DELAYED_REFS
	delayed_refs->run_delayed_start = find_middle(&delayed_refs->root);
#endif
	trans->can_flush_pending_bgs = false;
	ret = __btrfs_run_delayed_refs(trans, count);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	if (run_all) {
		if (!list_empty(&trans->new_bgs))
			btrfs_create_pending_block_groups(trans);

		spin_lock(&delayed_refs->lock);
		node = rb_first(&delayed_refs->href_root);
		if (!node) {
			spin_unlock(&delayed_refs->lock);
			goto out;
		}
		head = rb_entry(node, struct btrfs_delayed_ref_head,
				href_node);
		refcount_inc(&head->refs);
		spin_unlock(&delayed_refs->lock);

		/* Mutex was contended, block until it's released and retry. */
		mutex_lock(&head->mutex);
		mutex_unlock(&head->mutex);

		btrfs_put_delayed_ref_head(head);
		cond_resched();
		goto again;
	}
out:
	trans->can_flush_pending_bgs = can_flush_pending_bgs;
	return 0;
}

int btrfs_set_disk_extent_flags(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info,
				u64 bytenr, u64 num_bytes, u64 flags,
				int level, int is_data)
{
	struct btrfs_delayed_extent_op *extent_op;
	int ret;

	extent_op = btrfs_alloc_delayed_extent_op();
	if (!extent_op)
		return -ENOMEM;

	extent_op->flags_to_set = flags;
	extent_op->update_flags = true;
	extent_op->update_key = false;
	extent_op->is_data = is_data ? true : false;
	extent_op->level = level;

	ret = btrfs_add_delayed_extent_op(fs_info, trans, bytenr,
					  num_bytes, extent_op);
	if (ret)
		btrfs_free_delayed_extent_op(extent_op);
	return ret;
}

static noinline int check_delayed_ref(struct btrfs_root *root,
				      struct btrfs_path *path,
				      u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_data_ref *data_ref;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_transaction *cur_trans;
	struct rb_node *node;
	int ret = 0;

	cur_trans = root->fs_info->running_transaction;
	if (!cur_trans)
		return 0;

	delayed_refs = &cur_trans->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(delayed_refs, bytenr);
	if (!head) {
		spin_unlock(&delayed_refs->lock);
		return 0;
	}

	if (!mutex_trylock(&head->mutex)) {
		refcount_inc(&head->refs);
		spin_unlock(&delayed_refs->lock);

		btrfs_release_path(path);

		/*
		 * Mutex was contended, block until it's released and let
		 * caller try again
		 */
		mutex_lock(&head->mutex);
		mutex_unlock(&head->mutex);
		btrfs_put_delayed_ref_head(head);
		return -EAGAIN;
	}
	spin_unlock(&delayed_refs->lock);

	spin_lock(&head->lock);
	/*
	 * XXX: We should replace this with a proper search function in the
	 * future.
	 */
	for (node = rb_first(&head->ref_tree); node; node = rb_next(node)) {
		ref = rb_entry(node, struct btrfs_delayed_ref_node, ref_node);
		/* If it's a shared ref we know a cross reference exists */
		if (ref->type != BTRFS_EXTENT_DATA_REF_KEY) {
			ret = 1;
			break;
		}

		data_ref = btrfs_delayed_node_to_data_ref(ref);

		/*
		 * If our ref doesn't match the one we're currently looking at
		 * then we have a cross reference.
		 */
		if (data_ref->root != root->root_key.objectid ||
		    data_ref->objectid != objectid ||
		    data_ref->offset != offset) {
			ret = 1;
			break;
		}
	}
	spin_unlock(&head->lock);
	mutex_unlock(&head->mutex);
	return ret;
}

static noinline int check_committed_ref(struct btrfs_root *root,
					struct btrfs_path *path,
					u64 objectid, u64 offset, u64 bytenr)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct extent_buffer *leaf;
	struct btrfs_extent_data_ref *ref;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_extent_item *ei;
	struct btrfs_key key;
	u32 item_size;
	int type;
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

	type = btrfs_get_extent_inline_ref_type(leaf, iref, BTRFS_REF_TYPE_DATA);
	if (type != BTRFS_EXTENT_DATA_REF_KEY)
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

int btrfs_cross_ref_exist(struct btrfs_root *root, u64 objectid, u64 offset,
			  u64 bytenr)
{
	struct btrfs_path *path;
	int ret;
	int ret2;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOENT;

	do {
		ret = check_committed_ref(root, path, objectid,
					  offset, bytenr);
		if (ret && ret != -ENOENT)
			goto out;

		ret2 = check_delayed_ref(root, path, objectid,
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
			   int full_backref, int inc)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
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
	int (*process_func)(struct btrfs_trans_handle *,
			    struct btrfs_root *,
			    u64, u64, u64, u64, u64, u64);


	if (btrfs_is_testing(fs_info))
		return 0;

	ref_root = btrfs_header_owner(buf);
	nritems = btrfs_header_nritems(buf);
	level = btrfs_header_level(buf);

	if (!test_bit(BTRFS_ROOT_REF_COWS, &root->state) && level == 0)
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
			if (key.type != BTRFS_EXTENT_DATA_KEY)
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
					   key.offset);
			if (ret)
				goto fail;
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			num_bytes = fs_info->nodesize;
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, level - 1, 0);
			if (ret)
				goto fail;
		}
	}
	return 0;
fail:
	return ret;
}

int btrfs_inc_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref)
{
	return __btrfs_mod_ref(trans, root, buf, full_backref, 1);
}

int btrfs_dec_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		  struct extent_buffer *buf, int full_backref)
{
	return __btrfs_mod_ref(trans, root, buf, full_backref, 0);
}

static int write_one_cache_group(struct btrfs_trans_handle *trans,
				 struct btrfs_fs_info *fs_info,
				 struct btrfs_path *path,
				 struct btrfs_block_group_cache *cache)
{
	int ret;
	struct btrfs_root *extent_root = fs_info->extent_root;
	unsigned long bi;
	struct extent_buffer *leaf;

	ret = btrfs_search_slot(trans, extent_root, &cache->key, path, 0, 1);
	if (ret) {
		if (ret > 0)
			ret = -ENOENT;
		goto fail;
	}

	leaf = path->nodes[0];
	bi = btrfs_item_ptr_offset(leaf, path->slots[0]);
	write_extent_buffer(leaf, &cache->item, bi, sizeof(cache->item));
	btrfs_mark_buffer_dirty(leaf);
fail:
	btrfs_release_path(path);
	return ret;

}

static struct btrfs_block_group_cache *
next_block_group(struct btrfs_fs_info *fs_info,
		 struct btrfs_block_group_cache *cache)
{
	struct rb_node *node;

	spin_lock(&fs_info->block_group_cache_lock);

	/* If our block group was removed, we need a full search. */
	if (RB_EMPTY_NODE(&cache->cache_node)) {
		const u64 next_bytenr = cache->key.objectid + cache->key.offset;

		spin_unlock(&fs_info->block_group_cache_lock);
		btrfs_put_block_group(cache);
		cache = btrfs_lookup_first_block_group(fs_info, next_bytenr); return cache;
	}
	node = rb_next(&cache->cache_node);
	btrfs_put_block_group(cache);
	if (node) {
		cache = rb_entry(node, struct btrfs_block_group_cache,
				 cache_node);
		btrfs_get_block_group(cache);
	} else
		cache = NULL;
	spin_unlock(&fs_info->block_group_cache_lock);
	return cache;
}

static int cache_save_setup(struct btrfs_block_group_cache *block_group,
			    struct btrfs_trans_handle *trans,
			    struct btrfs_path *path)
{
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_root *root = fs_info->tree_root;
	struct inode *inode = NULL;
	struct extent_changeset *data_reserved = NULL;
	u64 alloc_hint = 0;
	int dcs = BTRFS_DC_ERROR;
	u64 num_pages = 0;
	int retries = 0;
	int ret = 0;

	/*
	 * If this block group is smaller than 100 megs don't bother caching the
	 * block group.
	 */
	if (block_group->key.offset < (100 * SZ_1M)) {
		spin_lock(&block_group->lock);
		block_group->disk_cache_state = BTRFS_DC_WRITTEN;
		spin_unlock(&block_group->lock);
		return 0;
	}

	if (trans->aborted)
		return 0;
again:
	inode = lookup_free_space_inode(fs_info, block_group, path);
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

		ret = create_free_space_inode(fs_info, trans, block_group,
					      path);
		if (ret)
			goto out_free;
		goto again;
	}

	/*
	 * We want to set the generation to 0, that way if anything goes wrong
	 * from here on out we know not to trust this cache when we load up next
	 * time.
	 */
	BTRFS_I(inode)->generation = 0;
	ret = btrfs_update_inode(trans, root, inode);
	if (ret) {
		/*
		 * So theoretically we could recover from this, simply set the
		 * super cache generation to 0 so we know to invalidate the
		 * cache, but then we'd have to keep track of the block groups
		 * that fail this way so we know we _have_ to reset this cache
		 * before the next commit or risk reading stale cache.  So to
		 * limit our exposure to horrible edge cases lets just abort the
		 * transaction, this only happens in really bad situations
		 * anyway.
		 */
		btrfs_abort_transaction(trans, ret);
		goto out_put;
	}
	WARN_ON(ret);

	/* We've already setup this transaction, go ahead and exit */
	if (block_group->cache_generation == trans->transid &&
	    i_size_read(inode)) {
		dcs = BTRFS_DC_SETUP;
		goto out_put;
	}

	if (i_size_read(inode) > 0) {
		ret = btrfs_check_trunc_cache_free_space(fs_info,
					&fs_info->global_block_rsv);
		if (ret)
			goto out_put;

		ret = btrfs_truncate_free_space_cache(trans, NULL, inode);
		if (ret)
			goto out_put;
	}

	spin_lock(&block_group->lock);
	if (block_group->cached != BTRFS_CACHE_FINISHED ||
	    !btrfs_test_opt(fs_info, SPACE_CACHE)) {
		/*
		 * don't bother trying to write stuff out _if_
		 * a) we're not cached,
		 * b) we're with nospace_cache mount option,
		 * c) we're with v2 space_cache (FREE_SPACE_TREE).
		 */
		dcs = BTRFS_DC_WRITTEN;
		spin_unlock(&block_group->lock);
		goto out_put;
	}
	spin_unlock(&block_group->lock);

	/*
	 * We hit an ENOSPC when setting up the cache in this transaction, just
	 * skip doing the setup, we've already cleared the cache so we're safe.
	 */
	if (test_bit(BTRFS_TRANS_CACHE_ENOSPC, &trans->transaction->flags)) {
		ret = -ENOSPC;
		goto out_put;
	}

	/*
	 * Try to preallocate enough space based on how big the block group is.
	 * Keep in mind this has to include any pinned space which could end up
	 * taking up quite a bit since it's not folded into the other space
	 * cache.
	 */
	num_pages = div_u64(block_group->key.offset, SZ_256M);
	if (!num_pages)
		num_pages = 1;

	num_pages *= 16;
	num_pages *= PAGE_SIZE;

	ret = btrfs_check_data_free_space(inode, &data_reserved, 0, num_pages);
	if (ret)
		goto out_put;

	ret = btrfs_prealloc_file_range_trans(inode, trans, 0, 0, num_pages,
					      num_pages, num_pages,
					      &alloc_hint);
	/*
	 * Our cache requires contiguous chunks so that we don't modify a bunch
	 * of metadata or split extents when writing the cache out, which means
	 * we can enospc if we are heavily fragmented in addition to just normal
	 * out of space conditions.  So if we hit this just skip setting up any
	 * other block groups for this transaction, maybe we'll unpin enough
	 * space the next time around.
	 */
	if (!ret)
		dcs = BTRFS_DC_SETUP;
	else if (ret == -ENOSPC)
		set_bit(BTRFS_TRANS_CACHE_ENOSPC, &trans->transaction->flags);

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

	extent_changeset_free(data_reserved);
	return ret;
}

int btrfs_setup_space_cache(struct btrfs_trans_handle *trans,
			    struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_group_cache *cache, *tmp;
	struct btrfs_transaction *cur_trans = trans->transaction;
	struct btrfs_path *path;

	if (list_empty(&cur_trans->dirty_bgs) ||
	    !btrfs_test_opt(fs_info, SPACE_CACHE))
		return 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/* Could add new block groups, use _safe just in case */
	list_for_each_entry_safe(cache, tmp, &cur_trans->dirty_bgs,
				 dirty_list) {
		if (cache->disk_cache_state == BTRFS_DC_CLEAR)
			cache_save_setup(cache, trans, path);
	}

	btrfs_free_path(path);
	return 0;
}

/*
 * transaction commit does final block group cache writeback during a
 * critical section where nothing is allowed to change the FS.  This is
 * required in order for the cache to actually match the block group,
 * but can introduce a lot of latency into the commit.
 *
 * So, btrfs_start_dirty_block_groups is here to kick off block group
 * cache IO.  There's a chance we'll have to redo some of it if the
 * block group changes again during the commit, but it greatly reduces
 * the commit latency by getting rid of the easy block groups while
 * we're still allowing others to join the commit.
 */
int btrfs_start_dirty_block_groups(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_block_group_cache *cache;
	struct btrfs_transaction *cur_trans = trans->transaction;
	int ret = 0;
	int should_put;
	struct btrfs_path *path = NULL;
	LIST_HEAD(dirty);
	struct list_head *io = &cur_trans->io_bgs;
	int num_started = 0;
	int loops = 0;

	spin_lock(&cur_trans->dirty_bgs_lock);
	if (list_empty(&cur_trans->dirty_bgs)) {
		spin_unlock(&cur_trans->dirty_bgs_lock);
		return 0;
	}
	list_splice_init(&cur_trans->dirty_bgs, &dirty);
	spin_unlock(&cur_trans->dirty_bgs_lock);

again:
	/*
	 * make sure all the block groups on our dirty list actually
	 * exist
	 */
	btrfs_create_pending_block_groups(trans);

	if (!path) {
		path = btrfs_alloc_path();
		if (!path)
			return -ENOMEM;
	}

	/*
	 * cache_write_mutex is here only to save us from balance or automatic
	 * removal of empty block groups deleting this block group while we are
	 * writing out the cache
	 */
	mutex_lock(&trans->transaction->cache_write_mutex);
	while (!list_empty(&dirty)) {
		cache = list_first_entry(&dirty,
					 struct btrfs_block_group_cache,
					 dirty_list);
		/*
		 * this can happen if something re-dirties a block
		 * group that is already under IO.  Just wait for it to
		 * finish and then do it all again
		 */
		if (!list_empty(&cache->io_list)) {
			list_del_init(&cache->io_list);
			btrfs_wait_cache_io(trans, cache, path);
			btrfs_put_block_group(cache);
		}


		/*
		 * btrfs_wait_cache_io uses the cache->dirty_list to decide
		 * if it should update the cache_state.  Don't delete
		 * until after we wait.
		 *
		 * Since we're not running in the commit critical section
		 * we need the dirty_bgs_lock to protect from update_block_group
		 */
		spin_lock(&cur_trans->dirty_bgs_lock);
		list_del_init(&cache->dirty_list);
		spin_unlock(&cur_trans->dirty_bgs_lock);

		should_put = 1;

		cache_save_setup(cache, trans, path);

		if (cache->disk_cache_state == BTRFS_DC_SETUP) {
			cache->io_ctl.inode = NULL;
			ret = btrfs_write_out_cache(fs_info, trans,
						    cache, path);
			if (ret == 0 && cache->io_ctl.inode) {
				num_started++;
				should_put = 0;

				/*
				 * The cache_write_mutex is protecting the
				 * io_list, also refer to the definition of
				 * btrfs_transaction::io_bgs for more details
				 */
				list_add_tail(&cache->io_list, io);
			} else {
				/*
				 * if we failed to write the cache, the
				 * generation will be bad and life goes on
				 */
				ret = 0;
			}
		}
		if (!ret) {
			ret = write_one_cache_group(trans, fs_info,
						    path, cache);
			/*
			 * Our block group might still be attached to the list
			 * of new block groups in the transaction handle of some
			 * other task (struct btrfs_trans_handle->new_bgs). This
			 * means its block group item isn't yet in the extent
			 * tree. If this happens ignore the error, as we will
			 * try again later in the critical section of the
			 * transaction commit.
			 */
			if (ret == -ENOENT) {
				ret = 0;
				spin_lock(&cur_trans->dirty_bgs_lock);
				if (list_empty(&cache->dirty_list)) {
					list_add_tail(&cache->dirty_list,
						      &cur_trans->dirty_bgs);
					btrfs_get_block_group(cache);
				}
				spin_unlock(&cur_trans->dirty_bgs_lock);
			} else if (ret) {
				btrfs_abort_transaction(trans, ret);
			}
		}

		/* if its not on the io list, we need to put the block group */
		if (should_put)
			btrfs_put_block_group(cache);

		if (ret)
			break;

		/*
		 * Avoid blocking other tasks for too long. It might even save
		 * us from writing caches for block groups that are going to be
		 * removed.
		 */
		mutex_unlock(&trans->transaction->cache_write_mutex);
		mutex_lock(&trans->transaction->cache_write_mutex);
	}
	mutex_unlock(&trans->transaction->cache_write_mutex);

	/*
	 * go through delayed refs for all the stuff we've just kicked off
	 * and then loop back (just once)
	 */
	ret = btrfs_run_delayed_refs(trans, 0);
	if (!ret && loops == 0) {
		loops++;
		spin_lock(&cur_trans->dirty_bgs_lock);
		list_splice_init(&cur_trans->dirty_bgs, &dirty);
		/*
		 * dirty_bgs_lock protects us from concurrent block group
		 * deletes too (not just cache_write_mutex).
		 */
		if (!list_empty(&dirty)) {
			spin_unlock(&cur_trans->dirty_bgs_lock);
			goto again;
		}
		spin_unlock(&cur_trans->dirty_bgs_lock);
	} else if (ret < 0) {
		btrfs_cleanup_dirty_bgs(cur_trans, fs_info);
	}

	btrfs_free_path(path);
	return ret;
}

int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				   struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_group_cache *cache;
	struct btrfs_transaction *cur_trans = trans->transaction;
	int ret = 0;
	int should_put;
	struct btrfs_path *path;
	struct list_head *io = &cur_trans->io_bgs;
	int num_started = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * Even though we are in the critical section of the transaction commit,
	 * we can still have concurrent tasks adding elements to this
	 * transaction's list of dirty block groups. These tasks correspond to
	 * endio free space workers started when writeback finishes for a
	 * space cache, which run inode.c:btrfs_finish_ordered_io(), and can
	 * allocate new block groups as a result of COWing nodes of the root
	 * tree when updating the free space inode. The writeback for the space
	 * caches is triggered by an earlier call to
	 * btrfs_start_dirty_block_groups() and iterations of the following
	 * loop.
	 * Also we want to do the cache_save_setup first and then run the
	 * delayed refs to make sure we have the best chance at doing this all
	 * in one shot.
	 */
	spin_lock(&cur_trans->dirty_bgs_lock);
	while (!list_empty(&cur_trans->dirty_bgs)) {
		cache = list_first_entry(&cur_trans->dirty_bgs,
					 struct btrfs_block_group_cache,
					 dirty_list);

		/*
		 * this can happen if cache_save_setup re-dirties a block
		 * group that is already under IO.  Just wait for it to
		 * finish and then do it all again
		 */
		if (!list_empty(&cache->io_list)) {
			spin_unlock(&cur_trans->dirty_bgs_lock);
			list_del_init(&cache->io_list);
			btrfs_wait_cache_io(trans, cache, path);
			btrfs_put_block_group(cache);
			spin_lock(&cur_trans->dirty_bgs_lock);
		}

		/*
		 * don't remove from the dirty list until after we've waited
		 * on any pending IO
		 */
		list_del_init(&cache->dirty_list);
		spin_unlock(&cur_trans->dirty_bgs_lock);
		should_put = 1;

		cache_save_setup(cache, trans, path);

		if (!ret)
			ret = btrfs_run_delayed_refs(trans,
						     (unsigned long) -1);

		if (!ret && cache->disk_cache_state == BTRFS_DC_SETUP) {
			cache->io_ctl.inode = NULL;
			ret = btrfs_write_out_cache(fs_info, trans,
						    cache, path);
			if (ret == 0 && cache->io_ctl.inode) {
				num_started++;
				should_put = 0;
				list_add_tail(&cache->io_list, io);
			} else {
				/*
				 * if we failed to write the cache, the
				 * generation will be bad and life goes on
				 */
				ret = 0;
			}
		}
		if (!ret) {
			ret = write_one_cache_group(trans, fs_info,
						    path, cache);
			/*
			 * One of the free space endio workers might have
			 * created a new block group while updating a free space
			 * cache's inode (at inode.c:btrfs_finish_ordered_io())
			 * and hasn't released its transaction handle yet, in
			 * which case the new block group is still attached to
			 * its transaction handle and its creation has not
			 * finished yet (no block group item in the extent tree
			 * yet, etc). If this is the case, wait for all free
			 * space endio workers to finish and retry. This is a
			 * a very rare case so no need for a more efficient and
			 * complex approach.
			 */
			if (ret == -ENOENT) {
				wait_event(cur_trans->writer_wait,
				   atomic_read(&cur_trans->num_writers) == 1);
				ret = write_one_cache_group(trans, fs_info,
							    path, cache);
			}
			if (ret)
				btrfs_abort_transaction(trans, ret);
		}

		/* if its not on the io list, we need to put the block group */
		if (should_put)
			btrfs_put_block_group(cache);
		spin_lock(&cur_trans->dirty_bgs_lock);
	}
	spin_unlock(&cur_trans->dirty_bgs_lock);

	/*
	 * Refer to the definition of io_bgs member for details why it's safe
	 * to use it without any locking
	 */
	while (!list_empty(io)) {
		cache = list_first_entry(io, struct btrfs_block_group_cache,
					 io_list);
		list_del_init(&cache->io_list);
		btrfs_wait_cache_io(trans, cache, path);
		btrfs_put_block_group(cache);
	}

	btrfs_free_path(path);
	return ret;
}

int btrfs_extent_readonly(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_block_group_cache *block_group;
	int readonly = 0;

	block_group = btrfs_lookup_block_group(fs_info, bytenr);
	if (!block_group || block_group->ro)
		readonly = 1;
	if (block_group)
		btrfs_put_block_group(block_group);
	return readonly;
}

bool btrfs_inc_nocow_writers(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_block_group_cache *bg;
	bool ret = true;

	bg = btrfs_lookup_block_group(fs_info, bytenr);
	if (!bg)
		return false;

	spin_lock(&bg->lock);
	if (bg->ro)
		ret = false;
	else
		atomic_inc(&bg->nocow_writers);
	spin_unlock(&bg->lock);

	/* no put on block group, done by btrfs_dec_nocow_writers */
	if (!ret)
		btrfs_put_block_group(bg);

	return ret;

}

void btrfs_dec_nocow_writers(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_block_group_cache *bg;

	bg = btrfs_lookup_block_group(fs_info, bytenr);
	ASSERT(bg);
	if (atomic_dec_and_test(&bg->nocow_writers))
		wake_up_atomic_t(&bg->nocow_writers);
	/*
	 * Once for our lookup and once for the lookup done by a previous call
	 * to btrfs_inc_nocow_writers()
	 */
	btrfs_put_block_group(bg);
	btrfs_put_block_group(bg);
}

void btrfs_wait_nocow_writers(struct btrfs_block_group_cache *bg)
{
	wait_on_atomic_t(&bg->nocow_writers, atomic_t_wait,
			 TASK_UNINTERRUPTIBLE);
}

static const char *alloc_name(u64 flags)
{
	switch (flags) {
	case BTRFS_BLOCK_GROUP_METADATA|BTRFS_BLOCK_GROUP_DATA:
		return "mixed";
	case BTRFS_BLOCK_GROUP_METADATA:
		return "metadata";
	case BTRFS_BLOCK_GROUP_DATA:
		return "data";
	case BTRFS_BLOCK_GROUP_SYSTEM:
		return "system";
	default:
		WARN_ON(1);
		return "invalid-combination";
	};
}

static int create_space_info(struct btrfs_fs_info *info, u64 flags,
			     struct btrfs_space_info **new)
{

	struct btrfs_space_info *space_info;
	int i;
	int ret;

	space_info = kzalloc(sizeof(*space_info), GFP_NOFS);
	if (!space_info)
		return -ENOMEM;

	ret = percpu_counter_init(&space_info->total_bytes_pinned, 0,
				 GFP_KERNEL);
	if (ret) {
		kfree(space_info);
		return ret;
	}

	for (i = 0; i < BTRFS_NR_RAID_TYPES; i++)
		INIT_LIST_HEAD(&space_info->block_groups[i]);
	init_rwsem(&space_info->groups_sem);
	spin_lock_init(&space_info->lock);
	space_info->flags = flags & BTRFS_BLOCK_GROUP_TYPE_MASK;
	space_info->force_alloc = CHUNK_ALLOC_NO_FORCE;
	init_waitqueue_head(&space_info->wait);
	INIT_LIST_HEAD(&space_info->ro_bgs);
	INIT_LIST_HEAD(&space_info->tickets);
	INIT_LIST_HEAD(&space_info->priority_tickets);

	ret = kobject_init_and_add(&space_info->kobj, &space_info_ktype,
				    info->space_info_kobj, "%s",
				    alloc_name(space_info->flags));
	if (ret) {
		percpu_counter_destroy(&space_info->total_bytes_pinned);
		kfree(space_info);
		return ret;
	}

	*new = space_info;
	list_add_rcu(&space_info->list, &info->space_info);
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		info->data_sinfo = space_info;

	return ret;
}

static void update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     u64 bytes_readonly,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;
	int factor;

	if (flags & (BTRFS_BLOCK_GROUP_DUP | BTRFS_BLOCK_GROUP_RAID1 |
		     BTRFS_BLOCK_GROUP_RAID10))
		factor = 2;
	else
		factor = 1;

	found = __find_space_info(info, flags);
	ASSERT(found);
	spin_lock(&found->lock);
	found->total_bytes += total_bytes;
	found->disk_total += total_bytes * factor;
	found->bytes_used += bytes_used;
	found->disk_used += bytes_used * factor;
	found->bytes_readonly += bytes_readonly;
	if (total_bytes > 0)
		found->full = 0;
	space_info_add_new_bytes(info, found, total_bytes -
				 bytes_used - bytes_readonly);
	spin_unlock(&found->lock);
	*space_info = found;
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
static u64 btrfs_reduce_alloc_profile(struct btrfs_fs_info *fs_info, u64 flags)
{
	u64 num_devices = fs_info->fs_devices->rw_devices;
	u64 target;
	u64 raid_type;
	u64 allowed = 0;

	/*
	 * see if restripe for this chunk_type is in progress, if so
	 * try to reduce to the target profile
	 */
	spin_lock(&fs_info->balance_lock);
	target = get_restripe_target(fs_info, flags);
	if (target) {
		/* pick target profile only if it's already available */
		if ((flags & target) & BTRFS_EXTENDED_PROFILE_MASK) {
			spin_unlock(&fs_info->balance_lock);
			return extended_to_chunk(target);
		}
	}
	spin_unlock(&fs_info->balance_lock);

	/* First, mask out the RAID levels which aren't possible */
	for (raid_type = 0; raid_type < BTRFS_NR_RAID_TYPES; raid_type++) {
		if (num_devices >= btrfs_raid_array[raid_type].devs_min)
			allowed |= btrfs_raid_group[raid_type];
	}
	allowed &= flags;

	if (allowed & BTRFS_BLOCK_GROUP_RAID6)
		allowed = BTRFS_BLOCK_GROUP_RAID6;
	else if (allowed & BTRFS_BLOCK_GROUP_RAID5)
		allowed = BTRFS_BLOCK_GROUP_RAID5;
	else if (allowed & BTRFS_BLOCK_GROUP_RAID10)
		allowed = BTRFS_BLOCK_GROUP_RAID10;
	else if (allowed & BTRFS_BLOCK_GROUP_RAID1)
		allowed = BTRFS_BLOCK_GROUP_RAID1;
	else if (allowed & BTRFS_BLOCK_GROUP_RAID0)
		allowed = BTRFS_BLOCK_GROUP_RAID0;

	flags &= ~BTRFS_BLOCK_GROUP_PROFILE_MASK;

	return extended_to_chunk(flags | allowed);
}

static u64 get_alloc_profile(struct btrfs_fs_info *fs_info, u64 orig_flags)
{
	unsigned seq;
	u64 flags;

	do {
		flags = orig_flags;
		seq = read_seqbegin(&fs_info->profiles_lock);

		if (flags & BTRFS_BLOCK_GROUP_DATA)
			flags |= fs_info->avail_data_alloc_bits;
		else if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
			flags |= fs_info->avail_system_alloc_bits;
		else if (flags & BTRFS_BLOCK_GROUP_METADATA)
			flags |= fs_info->avail_metadata_alloc_bits;
	} while (read_seqretry(&fs_info->profiles_lock, seq));

	return btrfs_reduce_alloc_profile(fs_info, flags);
}

static u64 get_alloc_profile_by_root(struct btrfs_root *root, int data)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 flags;
	u64 ret;

	if (data)
		flags = BTRFS_BLOCK_GROUP_DATA;
	else if (root == fs_info->chunk_root)
		flags = BTRFS_BLOCK_GROUP_SYSTEM;
	else
		flags = BTRFS_BLOCK_GROUP_METADATA;

	ret = get_alloc_profile(fs_info, flags);
	return ret;
}

u64 btrfs_data_alloc_profile(struct btrfs_fs_info *fs_info)
{
	return get_alloc_profile(fs_info, BTRFS_BLOCK_GROUP_DATA);
}

u64 btrfs_metadata_alloc_profile(struct btrfs_fs_info *fs_info)
{
	return get_alloc_profile(fs_info, BTRFS_BLOCK_GROUP_METADATA);
}

u64 btrfs_system_alloc_profile(struct btrfs_fs_info *fs_info)
{
	return get_alloc_profile(fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
}

static u64 btrfs_space_info_used(struct btrfs_space_info *s_info,
				 bool may_use_included)
{
	ASSERT(s_info);
	return s_info->bytes_used + s_info->bytes_reserved +
		s_info->bytes_pinned + s_info->bytes_readonly +
		(may_use_included ? s_info->bytes_may_use : 0);
}

int btrfs_alloc_data_chunk_ondemand(struct btrfs_inode *inode, u64 bytes)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_space_info *data_sinfo = fs_info->data_sinfo;
	u64 used;
	int ret = 0;
	int need_commit = 2;
	int have_pinned_space;

	/* make sure bytes are sectorsize aligned */
	bytes = ALIGN(bytes, fs_info->sectorsize);

	if (btrfs_is_free_space_inode(inode)) {
		need_commit = 0;
		ASSERT(current->journal_info);
	}

again:
	/* make sure we have enough space to handle the data first */
	spin_lock(&data_sinfo->lock);
	used = btrfs_space_info_used(data_sinfo, true);

	if (used + bytes > data_sinfo->total_bytes) {
		struct btrfs_trans_handle *trans;

		/*
		 * if we don't have enough free bytes in this space then we need
		 * to alloc a new chunk.
		 */
		if (!data_sinfo->full) {
			u64 alloc_target;

			data_sinfo->force_alloc = CHUNK_ALLOC_FORCE;
			spin_unlock(&data_sinfo->lock);

			alloc_target = btrfs_data_alloc_profile(fs_info);
			/*
			 * It is ugly that we don't call nolock join
			 * transaction for the free space inode case here.
			 * But it is safe because we only do the data space
			 * reservation for the free space cache in the
			 * transaction context, the common join transaction
			 * just increase the counter of the current transaction
			 * handler, doesn't try to acquire the trans_lock of
			 * the fs.
			 */
			trans = btrfs_join_transaction(root);
			if (IS_ERR(trans))
				return PTR_ERR(trans);

			ret = do_chunk_alloc(trans, fs_info, alloc_target,
					     CHUNK_ALLOC_NO_FORCE);
			btrfs_end_transaction(trans);
			if (ret < 0) {
				if (ret != -ENOSPC)
					return ret;
				else {
					have_pinned_space = 1;
					goto commit_trans;
				}
			}

			goto again;
		}

		/*
		 * If we don't have enough pinned space to deal with this
		 * allocation, and no removed chunk in current transaction,
		 * don't bother committing the transaction.
		 */
		have_pinned_space = percpu_counter_compare(
			&data_sinfo->total_bytes_pinned,
			used + bytes - data_sinfo->total_bytes);
		spin_unlock(&data_sinfo->lock);

		/* commit the current transaction and try again */
commit_trans:
		if (need_commit) {
			need_commit--;

			if (need_commit > 0) {
				btrfs_start_delalloc_roots(fs_info, 0, -1);
				btrfs_wait_ordered_roots(fs_info, U64_MAX, 0,
							 (u64)-1);
			}

			trans = btrfs_join_transaction(root);
			if (IS_ERR(trans))
				return PTR_ERR(trans);
			if (have_pinned_space >= 0 ||
			    test_bit(BTRFS_TRANS_HAVE_FREE_BGS,
				     &trans->transaction->flags) ||
			    need_commit > 0) {
				ret = btrfs_commit_transaction(trans);
				if (ret)
					return ret;
				/*
				 * The cleaner kthread might still be doing iput
				 * operations. Wait for it to finish so that
				 * more space is released.
				 */
				mutex_lock(&fs_info->cleaner_delayed_iput_mutex);
				mutex_unlock(&fs_info->cleaner_delayed_iput_mutex);
				goto again;
			} else {
				btrfs_end_transaction(trans);
			}
		}

		trace_btrfs_space_reservation(fs_info,
					      "space_info:enospc",
					      data_sinfo->flags, bytes, 1);
		return -ENOSPC;
	}
	data_sinfo->bytes_may_use += bytes;
	trace_btrfs_space_reservation(fs_info, "space_info",
				      data_sinfo->flags, bytes, 1);
	spin_unlock(&data_sinfo->lock);

	return ret;
}

int btrfs_check_data_free_space(struct inode *inode,
			struct extent_changeset **reserved, u64 start, u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret;

	/* align the range */
	len = round_up(start + len, fs_info->sectorsize) -
	      round_down(start, fs_info->sectorsize);
	start = round_down(start, fs_info->sectorsize);

	ret = btrfs_alloc_data_chunk_ondemand(BTRFS_I(inode), len);
	if (ret < 0)
		return ret;

	/* Use new btrfs_qgroup_reserve_data to reserve precious data space. */
	ret = btrfs_qgroup_reserve_data(inode, reserved, start, len);
	if (ret < 0)
		btrfs_free_reserved_data_space_noquota(inode, start, len);
	else
		ret = 0;
	return ret;
}

/*
 * Called if we need to clear a data reservation for this inode
 * Normally in a error case.
 *
 * This one will *NOT* use accurate qgroup reserved space API, just for case
 * which we can't sleep and is sure it won't affect qgroup reserved space.
 * Like clear_bit_hook().
 */
void btrfs_free_reserved_data_space_noquota(struct inode *inode, u64 start,
					    u64 len)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_space_info *data_sinfo;

	/* Make sure the range is aligned to sectorsize */
	len = round_up(start + len, fs_info->sectorsize) -
	      round_down(start, fs_info->sectorsize);
	start = round_down(start, fs_info->sectorsize);

	data_sinfo = fs_info->data_sinfo;
	spin_lock(&data_sinfo->lock);
	if (WARN_ON(data_sinfo->bytes_may_use < len))
		data_sinfo->bytes_may_use = 0;
	else
		data_sinfo->bytes_may_use -= len;
	trace_btrfs_space_reservation(fs_info, "space_info",
				      data_sinfo->flags, len, 0);
	spin_unlock(&data_sinfo->lock);
}

/*
 * Called if we need to clear a data reservation for this inode
 * Normally in a error case.
 *
 * This one will handle the per-inode data rsv map for accurate reserved
 * space framework.
 */
void btrfs_free_reserved_data_space(struct inode *inode,
			struct extent_changeset *reserved, u64 start, u64 len)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;

	/* Make sure the range is aligned to sectorsize */
	len = round_up(start + len, root->fs_info->sectorsize) -
	      round_down(start, root->fs_info->sectorsize);
	start = round_down(start, root->fs_info->sectorsize);

	btrfs_free_reserved_data_space_noquota(inode, start, len);
	btrfs_qgroup_free_data(inode, reserved, start, len);
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

static inline u64 calc_global_rsv_need_space(struct btrfs_block_rsv *global)
{
	return (global->size << 1);
}

static int should_alloc_chunk(struct btrfs_fs_info *fs_info,
			      struct btrfs_space_info *sinfo, int force)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 bytes_used = btrfs_space_info_used(sinfo, false);
	u64 thresh;

	if (force == CHUNK_ALLOC_FORCE)
		return 1;

	/*
	 * We need to take into account the global rsv because for all intents
	 * and purposes it's used space.  Don't worry about locking the
	 * global_rsv, it doesn't change except when the transaction commits.
	 */
	if (sinfo->flags & BTRFS_BLOCK_GROUP_METADATA)
		bytes_used += calc_global_rsv_need_space(global_rsv);

	/*
	 * in limited mode, we want to have some free space up to
	 * about 1% of the FS size.
	 */
	if (force == CHUNK_ALLOC_LIMITED) {
		thresh = btrfs_super_total_bytes(fs_info->super_copy);
		thresh = max_t(u64, SZ_64M, div_factor_fine(thresh, 1));

		if (sinfo->total_bytes - bytes_used < thresh)
			return 1;
	}

	if (bytes_used + SZ_2M < div_factor(sinfo->total_bytes, 8))
		return 0;
	return 1;
}

static u64 get_profile_num_devs(struct btrfs_fs_info *fs_info, u64 type)
{
	u64 num_dev;

	if (type & (BTRFS_BLOCK_GROUP_RAID10 |
		    BTRFS_BLOCK_GROUP_RAID0 |
		    BTRFS_BLOCK_GROUP_RAID5 |
		    BTRFS_BLOCK_GROUP_RAID6))
		num_dev = fs_info->fs_devices->rw_devices;
	else if (type & BTRFS_BLOCK_GROUP_RAID1)
		num_dev = 2;
	else
		num_dev = 1;	/* DUP or single */

	return num_dev;
}

/*
 * If @is_allocation is true, reserve space in the system space info necessary
 * for allocating a chunk, otherwise if it's false, reserve space necessary for
 * removing a chunk.
 */
void check_system_chunk(struct btrfs_trans_handle *trans,
			struct btrfs_fs_info *fs_info, u64 type)
{
	struct btrfs_space_info *info;
	u64 left;
	u64 thresh;
	int ret = 0;
	u64 num_devs;

	/*
	 * Needed because we can end up allocating a system chunk and for an
	 * atomic and race free space reservation in the chunk block reserve.
	 */
	lockdep_assert_held(&fs_info->chunk_mutex);

	info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
	spin_lock(&info->lock);
	left = info->total_bytes - btrfs_space_info_used(info, true);
	spin_unlock(&info->lock);

	num_devs = get_profile_num_devs(fs_info, type);

	/* num_devs device items to update and 1 chunk item to add or remove */
	thresh = btrfs_calc_trunc_metadata_size(fs_info, num_devs) +
		btrfs_calc_trans_metadata_size(fs_info, 1);

	if (left < thresh && btrfs_test_opt(fs_info, ENOSPC_DEBUG)) {
		btrfs_info(fs_info, "left=%llu, need=%llu, flags=%llu",
			   left, thresh, type);
		dump_space_info(fs_info, info, 0, 0);
	}

	if (left < thresh) {
		u64 flags = btrfs_system_alloc_profile(fs_info);

		/*
		 * Ignore failure to create system chunk. We might end up not
		 * needing it, as we might not need to COW all nodes/leafs from
		 * the paths we visit in the chunk tree (they were already COWed
		 * or created in the current transaction for example).
		 */
		ret = btrfs_alloc_chunk(trans, fs_info, flags);
	}

	if (!ret) {
		ret = btrfs_block_rsv_add(fs_info->chunk_root,
					  &fs_info->chunk_block_rsv,
					  thresh, BTRFS_RESERVE_NO_FLUSH);
		if (!ret)
			trans->chunk_bytes_reserved += thresh;
	}
}

/*
 * If force is CHUNK_ALLOC_FORCE:
 *    - return 1 if it successfully allocates a chunk,
 *    - return errors including -ENOSPC otherwise.
 * If force is NOT CHUNK_ALLOC_FORCE:
 *    - return 0 if it doesn't need to allocate a new chunk,
 *    - return 1 if it successfully allocates a chunk,
 *    - return errors including -ENOSPC otherwise.
 */
static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_fs_info *fs_info, u64 flags, int force)
{
	struct btrfs_space_info *space_info;
	int wait_for_alloc = 0;
	int ret = 0;

	/* Don't re-enter if we're already allocating a chunk */
	if (trans->allocating_chunk)
		return -ENOSPC;

	space_info = __find_space_info(fs_info, flags);
	ASSERT(space_info);

again:
	spin_lock(&space_info->lock);
	if (force < space_info->force_alloc)
		force = space_info->force_alloc;
	if (space_info->full) {
		if (should_alloc_chunk(fs_info, space_info, force))
			ret = -ENOSPC;
		else
			ret = 0;
		spin_unlock(&space_info->lock);
		return ret;
	}

	if (!should_alloc_chunk(fs_info, space_info, force)) {
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
		cond_resched();
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
	check_system_chunk(trans, fs_info, flags);

	ret = btrfs_alloc_chunk(trans, fs_info, flags);
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
	/*
	 * When we allocate a new chunk we reserve space in the chunk block
	 * reserve to make sure we can COW nodes/leafs in the chunk tree or
	 * add new nodes/leafs to it if we end up needing to do it when
	 * inserting the chunk item and updating device items as part of the
	 * second phase of chunk allocation, performed by
	 * btrfs_finish_chunk_alloc(). So make sure we don't accumulate a
	 * large number of new block groups to create in our transaction
	 * handle's new_bgs list to avoid exhausting the chunk block reserve
	 * in extreme cases - like having a single transaction create many new
	 * block groups when starting to write out the free space caches of all
	 * the block groups that were made dirty during the lifetime of the
	 * transaction.
	 */
	if (trans->can_flush_pending_bgs &&
	    trans->chunk_bytes_reserved >= (u64)SZ_2M) {
		btrfs_create_pending_block_groups(trans);
		btrfs_trans_release_chunk_metadata(trans);
	}
	return ret;
}

static int can_overcommit(struct btrfs_fs_info *fs_info,
			  struct btrfs_space_info *space_info, u64 bytes,
			  enum btrfs_reserve_flush_enum flush,
			  bool system_chunk)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 profile;
	u64 space_size;
	u64 avail;
	u64 used;

	/* Don't overcommit when in mixed mode. */
	if (space_info->flags & BTRFS_BLOCK_GROUP_DATA)
		return 0;

	if (system_chunk)
		profile = btrfs_system_alloc_profile(fs_info);
	else
		profile = btrfs_metadata_alloc_profile(fs_info);

	used = btrfs_space_info_used(space_info, false);

	/*
	 * We only want to allow over committing if we have lots of actual space
	 * free, but if we don't have enough space to handle the global reserve
	 * space then we could end up having a real enospc problem when trying
	 * to allocate a chunk or some other such important allocation.
	 */
	spin_lock(&global_rsv->lock);
	space_size = calc_global_rsv_need_space(global_rsv);
	spin_unlock(&global_rsv->lock);
	if (used + space_size >= space_info->total_bytes)
		return 0;

	used += space_info->bytes_may_use;

	avail = atomic64_read(&fs_info->free_chunk_space);

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

	/*
	 * If we aren't flushing all things, let us overcommit up to
	 * 1/2th of the space. If we can flush, don't let us overcommit
	 * too much, let it overcommit up to 1/8 of the space.
	 */
	if (flush == BTRFS_RESERVE_FLUSH_ALL)
		avail >>= 3;
	else
		avail >>= 1;

	if (used + bytes < space_info->total_bytes + avail)
		return 1;
	return 0;
}

static void btrfs_writeback_inodes_sb_nr(struct btrfs_fs_info *fs_info,
					 unsigned long nr_pages, int nr_items)
{
	struct super_block *sb = fs_info->sb;

	if (down_read_trylock(&sb->s_umount)) {
		writeback_inodes_sb_nr(sb, nr_pages, WB_REASON_FS_FREE_SPACE);
		up_read(&sb->s_umount);
	} else {
		/*
		 * We needn't worry the filesystem going from r/w to r/o though
		 * we don't acquire ->s_umount mutex, because the filesystem
		 * should guarantee the delalloc inodes list be empty after
		 * the filesystem is readonly(all dirty pages are written to
		 * the disk).
		 */
		btrfs_start_delalloc_roots(fs_info, 0, nr_items);
		if (!current->journal_info)
			btrfs_wait_ordered_roots(fs_info, nr_items, 0, (u64)-1);
	}
}

static inline u64 calc_reclaim_items_nr(struct btrfs_fs_info *fs_info,
					u64 to_reclaim)
{
	u64 bytes;
	u64 nr;

	bytes = btrfs_calc_trans_metadata_size(fs_info, 1);
	nr = div64_u64(to_reclaim, bytes);
	if (!nr)
		nr = 1;
	return nr;
}

#define EXTENT_SIZE_PER_ITEM	SZ_256K

/*
 * shrink metadata reservation for delalloc
 */
static void shrink_delalloc(struct btrfs_fs_info *fs_info, u64 to_reclaim,
			    u64 orig, bool wait_ordered)
{
	struct btrfs_space_info *space_info;
	struct btrfs_trans_handle *trans;
	u64 delalloc_bytes;
	u64 max_reclaim;
	u64 items;
	long time_left;
	unsigned long nr_pages;
	int loops;

	/* Calc the number of the pages we need flush for space reservation */
	items = calc_reclaim_items_nr(fs_info, to_reclaim);
	to_reclaim = items * EXTENT_SIZE_PER_ITEM;

	trans = (struct btrfs_trans_handle *)current->journal_info;
	space_info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);

	delalloc_bytes = percpu_counter_sum_positive(
						&fs_info->delalloc_bytes);
	if (delalloc_bytes == 0) {
		if (trans)
			return;
		if (wait_ordered)
			btrfs_wait_ordered_roots(fs_info, items, 0, (u64)-1);
		return;
	}

	loops = 0;
	while (delalloc_bytes && loops < 3) {
		max_reclaim = min(delalloc_bytes, to_reclaim);
		nr_pages = max_reclaim >> PAGE_SHIFT;
		btrfs_writeback_inodes_sb_nr(fs_info, nr_pages, items);
		/*
		 * We need to wait for the async pages to actually start before
		 * we do anything.
		 */
		max_reclaim = atomic_read(&fs_info->async_delalloc_pages);
		if (!max_reclaim)
			goto skip_async;

		if (max_reclaim <= nr_pages)
			max_reclaim = 0;
		else
			max_reclaim -= nr_pages;

		wait_event(fs_info->async_submit_wait,
			   atomic_read(&fs_info->async_delalloc_pages) <=
			   (int)max_reclaim);
skip_async:
		spin_lock(&space_info->lock);
		if (list_empty(&space_info->tickets) &&
		    list_empty(&space_info->priority_tickets)) {
			spin_unlock(&space_info->lock);
			break;
		}
		spin_unlock(&space_info->lock);

		loops++;
		if (wait_ordered && !trans) {
			btrfs_wait_ordered_roots(fs_info, items, 0, (u64)-1);
		} else {
			time_left = schedule_timeout_killable(1);
			if (time_left)
				break;
		}
		delalloc_bytes = percpu_counter_sum_positive(
						&fs_info->delalloc_bytes);
	}
}

struct reserve_ticket {
	u64 bytes;
	int error;
	struct list_head list;
	wait_queue_head_t wait;
};

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
static int may_commit_transaction(struct btrfs_fs_info *fs_info,
				  struct btrfs_space_info *space_info)
{
	struct reserve_ticket *ticket = NULL;
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_block_rsv;
	struct btrfs_trans_handle *trans;
	u64 bytes;

	trans = (struct btrfs_trans_handle *)current->journal_info;
	if (trans)
		return -EAGAIN;

	spin_lock(&space_info->lock);
	if (!list_empty(&space_info->priority_tickets))
		ticket = list_first_entry(&space_info->priority_tickets,
					  struct reserve_ticket, list);
	else if (!list_empty(&space_info->tickets))
		ticket = list_first_entry(&space_info->tickets,
					  struct reserve_ticket, list);
	bytes = (ticket) ? ticket->bytes : 0;
	spin_unlock(&space_info->lock);

	if (!bytes)
		return 0;

	/* See if there is enough pinned space to make this reservation */
	if (percpu_counter_compare(&space_info->total_bytes_pinned,
				   bytes) >= 0)
		goto commit;

	/*
	 * See if there is some space in the delayed insertion reservation for
	 * this reservation.
	 */
	if (space_info != delayed_rsv->space_info)
		return -ENOSPC;

	spin_lock(&delayed_rsv->lock);
	if (delayed_rsv->size > bytes)
		bytes = 0;
	else
		bytes -= delayed_rsv->size;
	spin_unlock(&delayed_rsv->lock);

	if (percpu_counter_compare(&space_info->total_bytes_pinned,
				   bytes) < 0) {
		return -ENOSPC;
	}

commit:
	trans = btrfs_join_transaction(fs_info->extent_root);
	if (IS_ERR(trans))
		return -ENOSPC;

	return btrfs_commit_transaction(trans);
}

/*
 * Try to flush some data based on policy set by @state. This is only advisory
 * and may fail for various reasons. The caller is supposed to examine the
 * state of @space_info to detect the outcome.
 */
static void flush_space(struct btrfs_fs_info *fs_info,
		       struct btrfs_space_info *space_info, u64 num_bytes,
		       int state)
{
	struct btrfs_root *root = fs_info->extent_root;
	struct btrfs_trans_handle *trans;
	int nr;
	int ret = 0;

	switch (state) {
	case FLUSH_DELAYED_ITEMS_NR:
	case FLUSH_DELAYED_ITEMS:
		if (state == FLUSH_DELAYED_ITEMS_NR)
			nr = calc_reclaim_items_nr(fs_info, num_bytes) * 2;
		else
			nr = -1;

		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}
		ret = btrfs_run_delayed_items_nr(trans, nr);
		btrfs_end_transaction(trans);
		break;
	case FLUSH_DELALLOC:
	case FLUSH_DELALLOC_WAIT:
		shrink_delalloc(fs_info, num_bytes * 2, num_bytes,
				state == FLUSH_DELALLOC_WAIT);
		break;
	case ALLOC_CHUNK:
		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}
		ret = do_chunk_alloc(trans, fs_info,
				     btrfs_metadata_alloc_profile(fs_info),
				     CHUNK_ALLOC_NO_FORCE);
		btrfs_end_transaction(trans);
		if (ret > 0 || ret == -ENOSPC)
			ret = 0;
		break;
	case COMMIT_TRANS:
		ret = may_commit_transaction(fs_info, space_info);
		break;
	default:
		ret = -ENOSPC;
		break;
	}

	trace_btrfs_flush_space(fs_info, space_info->flags, num_bytes, state,
				ret);
	return;
}

static inline u64
btrfs_calc_reclaim_metadata_size(struct btrfs_fs_info *fs_info,
				 struct btrfs_space_info *space_info,
				 bool system_chunk)
{
	struct reserve_ticket *ticket;
	u64 used;
	u64 expected;
	u64 to_reclaim = 0;

	list_for_each_entry(ticket, &space_info->tickets, list)
		to_reclaim += ticket->bytes;
	list_for_each_entry(ticket, &space_info->priority_tickets, list)
		to_reclaim += ticket->bytes;
	if (to_reclaim)
		return to_reclaim;

	to_reclaim = min_t(u64, num_online_cpus() * SZ_1M, SZ_16M);
	if (can_overcommit(fs_info, space_info, to_reclaim,
			   BTRFS_RESERVE_FLUSH_ALL, system_chunk))
		return 0;

	used = btrfs_space_info_used(space_info, true);

	if (can_overcommit(fs_info, space_info, SZ_1M,
			   BTRFS_RESERVE_FLUSH_ALL, system_chunk))
		expected = div_factor_fine(space_info->total_bytes, 95);
	else
		expected = div_factor_fine(space_info->total_bytes, 90);

	if (used > expected)
		to_reclaim = used - expected;
	else
		to_reclaim = 0;
	to_reclaim = min(to_reclaim, space_info->bytes_may_use +
				     space_info->bytes_reserved);
	return to_reclaim;
}

static inline int need_do_async_reclaim(struct btrfs_fs_info *fs_info,
					struct btrfs_space_info *space_info,
					u64 used, bool system_chunk)
{
	u64 thresh = div_factor_fine(space_info->total_bytes, 98);

	/* If we're just plain full then async reclaim just slows us down. */
	if ((space_info->bytes_used + space_info->bytes_reserved) >= thresh)
		return 0;

	if (!btrfs_calc_reclaim_metadata_size(fs_info, space_info,
					      system_chunk))
		return 0;

	return (used >= thresh && !btrfs_fs_closing(fs_info) &&
		!test_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state));
}

static void wake_all_tickets(struct list_head *head)
{
	struct reserve_ticket *ticket;

	while (!list_empty(head)) {
		ticket = list_first_entry(head, struct reserve_ticket, list);
		list_del_init(&ticket->list);
		ticket->error = -ENOSPC;
		wake_up(&ticket->wait);
	}
}

/*
 * This is for normal flushers, we can wait all goddamned day if we want to.  We
 * will loop and continuously try to flush as long as we are making progress.
 * We count progress as clearing off tickets each time we have to loop.
 */
static void btrfs_async_reclaim_metadata_space(struct work_struct *work)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_space_info *space_info;
	u64 to_reclaim;
	int flush_state;
	int commit_cycles = 0;
	u64 last_tickets_id;

	fs_info = container_of(work, struct btrfs_fs_info, async_reclaim_work);
	space_info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);

	spin_lock(&space_info->lock);
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info, space_info,
						      false);
	if (!to_reclaim) {
		space_info->flush = 0;
		spin_unlock(&space_info->lock);
		return;
	}
	last_tickets_id = space_info->tickets_id;
	spin_unlock(&space_info->lock);

	flush_state = FLUSH_DELAYED_ITEMS_NR;
	do {
		flush_space(fs_info, space_info, to_reclaim, flush_state);
		spin_lock(&space_info->lock);
		if (list_empty(&space_info->tickets)) {
			space_info->flush = 0;
			spin_unlock(&space_info->lock);
			return;
		}
		to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info,
							      space_info,
							      false);
		if (last_tickets_id == space_info->tickets_id) {
			flush_state++;
		} else {
			last_tickets_id = space_info->tickets_id;
			flush_state = FLUSH_DELAYED_ITEMS_NR;
			if (commit_cycles)
				commit_cycles--;
		}

		if (flush_state > COMMIT_TRANS) {
			commit_cycles++;
			if (commit_cycles > 2) {
				wake_all_tickets(&space_info->tickets);
				space_info->flush = 0;
			} else {
				flush_state = FLUSH_DELAYED_ITEMS_NR;
			}
		}
		spin_unlock(&space_info->lock);
	} while (flush_state <= COMMIT_TRANS);
}

void btrfs_init_async_reclaim_work(struct work_struct *work)
{
	INIT_WORK(work, btrfs_async_reclaim_metadata_space);
}

static void priority_reclaim_metadata_space(struct btrfs_fs_info *fs_info,
					    struct btrfs_space_info *space_info,
					    struct reserve_ticket *ticket)
{
	u64 to_reclaim;
	int flush_state = FLUSH_DELAYED_ITEMS_NR;

	spin_lock(&space_info->lock);
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info, space_info,
						      false);
	if (!to_reclaim) {
		spin_unlock(&space_info->lock);
		return;
	}
	spin_unlock(&space_info->lock);

	do {
		flush_space(fs_info, space_info, to_reclaim, flush_state);
		flush_state++;
		spin_lock(&space_info->lock);
		if (ticket->bytes == 0) {
			spin_unlock(&space_info->lock);
			return;
		}
		spin_unlock(&space_info->lock);

		/*
		 * Priority flushers can't wait on delalloc without
		 * deadlocking.
		 */
		if (flush_state == FLUSH_DELALLOC ||
		    flush_state == FLUSH_DELALLOC_WAIT)
			flush_state = ALLOC_CHUNK;
	} while (flush_state < COMMIT_TRANS);
}

static int wait_reserve_ticket(struct btrfs_fs_info *fs_info,
			       struct btrfs_space_info *space_info,
			       struct reserve_ticket *ticket, u64 orig_bytes)

{
	DEFINE_WAIT(wait);
	int ret = 0;

	spin_lock(&space_info->lock);
	while (ticket->bytes > 0 && ticket->error == 0) {
		ret = prepare_to_wait_event(&ticket->wait, &wait, TASK_KILLABLE);
		if (ret) {
			ret = -EINTR;
			break;
		}
		spin_unlock(&space_info->lock);

		schedule();

		finish_wait(&ticket->wait, &wait);
		spin_lock(&space_info->lock);
	}
	if (!ret)
		ret = ticket->error;
	if (!list_empty(&ticket->list))
		list_del_init(&ticket->list);
	if (ticket->bytes && ticket->bytes < orig_bytes) {
		u64 num_bytes = orig_bytes - ticket->bytes;
		space_info->bytes_may_use -= num_bytes;
		trace_btrfs_space_reservation(fs_info, "space_info",
					      space_info->flags, num_bytes, 0);
	}
	spin_unlock(&space_info->lock);

	return ret;
}

/**
 * reserve_metadata_bytes - try to reserve bytes from the block_rsv's space
 * @root - the root we're allocating for
 * @space_info - the space info we want to allocate from
 * @orig_bytes - the number of bytes we want
 * @flush - whether or not we can flush to make our reservation
 *
 * This will reserve orig_bytes number of bytes from the space info associated
 * with the block_rsv.  If there is not enough space it will make an attempt to
 * flush out space to make room.  It will do this by flushing delalloc if
 * possible or committing the transaction.  If flush is 0 then no attempts to
 * regain reservations will be made and this will fail if there is not enough
 * space already.
 */
static int __reserve_metadata_bytes(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *space_info,
				    u64 orig_bytes,
				    enum btrfs_reserve_flush_enum flush,
				    bool system_chunk)
{
	struct reserve_ticket ticket;
	u64 used;
	int ret = 0;

	ASSERT(orig_bytes);
	ASSERT(!current->journal_info || flush != BTRFS_RESERVE_FLUSH_ALL);

	spin_lock(&space_info->lock);
	ret = -ENOSPC;
	used = btrfs_space_info_used(space_info, true);

	/*
	 * If we have enough space then hooray, make our reservation and carry
	 * on.  If not see if we can overcommit, and if we can, hooray carry on.
	 * If not things get more complicated.
	 */
	if (used + orig_bytes <= space_info->total_bytes) {
		space_info->bytes_may_use += orig_bytes;
		trace_btrfs_space_reservation(fs_info, "space_info",
					      space_info->flags, orig_bytes, 1);
		ret = 0;
	} else if (can_overcommit(fs_info, space_info, orig_bytes, flush,
				  system_chunk)) {
		space_info->bytes_may_use += orig_bytes;
		trace_btrfs_space_reservation(fs_info, "space_info",
					      space_info->flags, orig_bytes, 1);
		ret = 0;
	}

	/*
	 * If we couldn't make a reservation then setup our reservation ticket
	 * and kick the async worker if it's not already running.
	 *
	 * If we are a priority flusher then we just need to add our ticket to
	 * the list and we will do our own flushing further down.
	 */
	if (ret && flush != BTRFS_RESERVE_NO_FLUSH) {
		ticket.bytes = orig_bytes;
		ticket.error = 0;
		init_waitqueue_head(&ticket.wait);
		if (flush == BTRFS_RESERVE_FLUSH_ALL) {
			list_add_tail(&ticket.list, &space_info->tickets);
			if (!space_info->flush) {
				space_info->flush = 1;
				trace_btrfs_trigger_flush(fs_info,
							  space_info->flags,
							  orig_bytes, flush,
							  "enospc");
				queue_work(system_unbound_wq,
					   &fs_info->async_reclaim_work);
			}
		} else {
			list_add_tail(&ticket.list,
				      &space_info->priority_tickets);
		}
	} else if (!ret && space_info->flags & BTRFS_BLOCK_GROUP_METADATA) {
		used += orig_bytes;
		/*
		 * We will do the space reservation dance during log replay,
		 * which means we won't have fs_info->fs_root set, so don't do
		 * the async reclaim as we will panic.
		 */
		if (!test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags) &&
		    need_do_async_reclaim(fs_info, space_info,
					  used, system_chunk) &&
		    !work_busy(&fs_info->async_reclaim_work)) {
			trace_btrfs_trigger_flush(fs_info, space_info->flags,
						  orig_bytes, flush, "preempt");
			queue_work(system_unbound_wq,
				   &fs_info->async_reclaim_work);
		}
	}
	spin_unlock(&space_info->lock);
	if (!ret || flush == BTRFS_RESERVE_NO_FLUSH)
		return ret;

	if (flush == BTRFS_RESERVE_FLUSH_ALL)
		return wait_reserve_ticket(fs_info, space_info, &ticket,
					   orig_bytes);

	ret = 0;
	priority_reclaim_metadata_space(fs_info, space_info, &ticket);
	spin_lock(&space_info->lock);
	if (ticket.bytes) {
		if (ticket.bytes < orig_bytes) {
			u64 num_bytes = orig_bytes - ticket.bytes;
			space_info->bytes_may_use -= num_bytes;
			trace_btrfs_space_reservation(fs_info, "space_info",
						      space_info->flags,
						      num_bytes, 0);

		}
		list_del_init(&ticket.list);
		ret = -ENOSPC;
	}
	spin_unlock(&space_info->lock);
	ASSERT(list_empty(&ticket.list));
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
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	int ret;
	bool system_chunk = (root == fs_info->chunk_root);

	ret = __reserve_metadata_bytes(fs_info, block_rsv->space_info,
				       orig_bytes, flush, system_chunk);
	if (ret == -ENOSPC &&
	    unlikely(root->orphan_cleanup_state == ORPHAN_CLEANUP_STARTED)) {
		if (block_rsv != global_rsv &&
		    !block_rsv_use_bytes(global_rsv, orig_bytes))
			ret = 0;
	}
	if (ret == -ENOSPC) {
		trace_btrfs_space_reservation(fs_info, "space_info:enospc",
					      block_rsv->space_info->flags,
					      orig_bytes, 1);

		if (btrfs_test_opt(fs_info, ENOSPC_DEBUG))
			dump_space_info(fs_info, block_rsv->space_info,
					orig_bytes, 0);
	}
	return ret;
}

static struct btrfs_block_rsv *get_block_rsv(
					const struct btrfs_trans_handle *trans,
					const struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *block_rsv = NULL;

	if (test_bit(BTRFS_ROOT_REF_COWS, &root->state) ||
	    (root == fs_info->csum_root && trans->adding_csums) ||
	    (root == fs_info->uuid_root))
		block_rsv = trans->block_rsv;

	if (!block_rsv)
		block_rsv = root->block_rsv;

	if (!block_rsv)
		block_rsv = &fs_info->empty_block_rsv;

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

int btrfs_cond_migrate_bytes(struct btrfs_fs_info *fs_info,
			     struct btrfs_block_rsv *dest, u64 num_bytes,
			     int min_factor)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 min_bytes;

	if (global_rsv->space_info != dest->space_info)
		return -ENOSPC;

	spin_lock(&global_rsv->lock);
	min_bytes = div_factor(global_rsv->size, min_factor);
	if (global_rsv->reserved < min_bytes + num_bytes) {
		spin_unlock(&global_rsv->lock);
		return -ENOSPC;
	}
	global_rsv->reserved -= num_bytes;
	if (global_rsv->reserved < global_rsv->size)
		global_rsv->full = 0;
	spin_unlock(&global_rsv->lock);

	block_rsv_add_bytes(dest, num_bytes, 1);
	return 0;
}

/*
 * This is for space we already have accounted in space_info->bytes_may_use, so
 * basically when we're returning space from block_rsv's.
 */
static void space_info_add_old_bytes(struct btrfs_fs_info *fs_info,
				     struct btrfs_space_info *space_info,
				     u64 num_bytes)
{
	struct reserve_ticket *ticket;
	struct list_head *head;
	u64 used;
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_NO_FLUSH;
	bool check_overcommit = false;

	spin_lock(&space_info->lock);
	head = &space_info->priority_tickets;

	/*
	 * If we are over our limit then we need to check and see if we can
	 * overcommit, and if we can't then we just need to free up our space
	 * and not satisfy any requests.
	 */
	used = btrfs_space_info_used(space_info, true);
	if (used - num_bytes >= space_info->total_bytes)
		check_overcommit = true;
again:
	while (!list_empty(head) && num_bytes) {
		ticket = list_first_entry(head, struct reserve_ticket,
					  list);
		/*
		 * We use 0 bytes because this space is already reserved, so
		 * adding the ticket space would be a double count.
		 */
		if (check_overcommit &&
		    !can_overcommit(fs_info, space_info, 0, flush, false))
			break;
		if (num_bytes >= ticket->bytes) {
			list_del_init(&ticket->list);
			num_bytes -= ticket->bytes;
			ticket->bytes = 0;
			space_info->tickets_id++;
			wake_up(&ticket->wait);
		} else {
			ticket->bytes -= num_bytes;
			num_bytes = 0;
		}
	}

	if (num_bytes && head == &space_info->priority_tickets) {
		head = &space_info->tickets;
		flush = BTRFS_RESERVE_FLUSH_ALL;
		goto again;
	}
	space_info->bytes_may_use -= num_bytes;
	trace_btrfs_space_reservation(fs_info, "space_info",
				      space_info->flags, num_bytes, 0);
	spin_unlock(&space_info->lock);
}

/*
 * This is for newly allocated space that isn't accounted in
 * space_info->bytes_may_use yet.  So if we allocate a chunk or unpin an extent
 * we use this helper.
 */
static void space_info_add_new_bytes(struct btrfs_fs_info *fs_info,
				     struct btrfs_space_info *space_info,
				     u64 num_bytes)
{
	struct reserve_ticket *ticket;
	struct list_head *head = &space_info->priority_tickets;

again:
	while (!list_empty(head) && num_bytes) {
		ticket = list_first_entry(head, struct reserve_ticket,
					  list);
		if (num_bytes >= ticket->bytes) {
			trace_btrfs_space_reservation(fs_info, "space_info",
						      space_info->flags,
						      ticket->bytes, 1);
			list_del_init(&ticket->list);
			num_bytes -= ticket->bytes;
			space_info->bytes_may_use += ticket->bytes;
			ticket->bytes = 0;
			space_info->tickets_id++;
			wake_up(&ticket->wait);
		} else {
			trace_btrfs_space_reservation(fs_info, "space_info",
						      space_info->flags,
						      num_bytes, 1);
			space_info->bytes_may_use += num_bytes;
			ticket->bytes -= num_bytes;
			num_bytes = 0;
		}
	}

	if (num_bytes && head == &space_info->priority_tickets) {
		head = &space_info->tickets;
		goto again;
	}
}

static u64 block_rsv_release_bytes(struct btrfs_fs_info *fs_info,
				    struct btrfs_block_rsv *block_rsv,
				    struct btrfs_block_rsv *dest, u64 num_bytes)
{
	struct btrfs_space_info *space_info = block_rsv->space_info;
	u64 ret;

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

	ret = num_bytes;
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
		if (num_bytes)
			space_info_add_old_bytes(fs_info, space_info,
						 num_bytes);
	}
	return ret;
}

int btrfs_block_rsv_migrate(struct btrfs_block_rsv *src,
			    struct btrfs_block_rsv *dst, u64 num_bytes,
			    int update_size)
{
	int ret;

	ret = block_rsv_use_bytes(src, num_bytes);
	if (ret)
		return ret;

	block_rsv_add_bytes(dst, num_bytes, update_size);
	return 0;
}

void btrfs_init_block_rsv(struct btrfs_block_rsv *rsv, unsigned short type)
{
	memset(rsv, 0, sizeof(*rsv));
	spin_lock_init(&rsv->lock);
	rsv->type = type;
}

void btrfs_init_metadata_block_rsv(struct btrfs_fs_info *fs_info,
				   struct btrfs_block_rsv *rsv,
				   unsigned short type)
{
	btrfs_init_block_rsv(rsv, type);
	rsv->space_info = __find_space_info(fs_info,
					    BTRFS_BLOCK_GROUP_METADATA);
}

struct btrfs_block_rsv *btrfs_alloc_block_rsv(struct btrfs_fs_info *fs_info,
					      unsigned short type)
{
	struct btrfs_block_rsv *block_rsv;

	block_rsv = kmalloc(sizeof(*block_rsv), GFP_NOFS);
	if (!block_rsv)
		return NULL;

	btrfs_init_metadata_block_rsv(fs_info, block_rsv, type);
	return block_rsv;
}

void btrfs_free_block_rsv(struct btrfs_fs_info *fs_info,
			  struct btrfs_block_rsv *rsv)
{
	if (!rsv)
		return;
	btrfs_block_rsv_release(fs_info, rsv, (u64)-1);
	kfree(rsv);
}

void __btrfs_free_block_rsv(struct btrfs_block_rsv *rsv)
{
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

int btrfs_block_rsv_check(struct btrfs_block_rsv *block_rsv, int min_factor)
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

/**
 * btrfs_inode_rsv_refill - refill the inode block rsv.
 * @inode - the inode we are refilling.
 * @flush - the flusing restriction.
 *
 * Essentially the same as btrfs_block_rsv_refill, except it uses the
 * block_rsv->size as the minimum size.  We'll either refill the missing amount
 * or return if we already have enough space.  This will also handle the resreve
 * tracepoint for the reserved amount.
 */
static int btrfs_inode_rsv_refill(struct btrfs_inode *inode,
				  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_root *root = inode->root;
	struct btrfs_block_rsv *block_rsv = &inode->block_rsv;
	u64 num_bytes = 0;
	int ret = -ENOSPC;

	spin_lock(&block_rsv->lock);
	if (block_rsv->reserved < block_rsv->size)
		num_bytes = block_rsv->size - block_rsv->reserved;
	spin_unlock(&block_rsv->lock);

	if (num_bytes == 0)
		return 0;

	ret = btrfs_qgroup_reserve_meta_prealloc(root, num_bytes, true);
	if (ret)
		return ret;
	ret = reserve_metadata_bytes(root, block_rsv, num_bytes, flush);
	if (!ret) {
		block_rsv_add_bytes(block_rsv, num_bytes, 0);
		trace_btrfs_space_reservation(root->fs_info, "delalloc",
					      btrfs_ino(inode), num_bytes, 1);
	}
	return ret;
}

/**
 * btrfs_inode_rsv_release - release any excessive reservation.
 * @inode - the inode we need to release from.
 * @qgroup_free - free or convert qgroup meta.
 *   Unlike normal operation, qgroup meta reservation needs to know if we are
 *   freeing qgroup reservation or just converting it into per-trans.  Normally
 *   @qgroup_free is true for error handling, and false for normal release.
 *
 * This is the same as btrfs_block_rsv_release, except that it handles the
 * tracepoint for the reservation.
 */
static void btrfs_inode_rsv_release(struct btrfs_inode *inode, bool qgroup_free)
{
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	struct btrfs_block_rsv *block_rsv = &inode->block_rsv;
	u64 released = 0;

	/*
	 * Since we statically set the block_rsv->size we just want to say we
	 * are releasing 0 bytes, and then we'll just get the reservation over
	 * the size free'd.
	 */
	released = block_rsv_release_bytes(fs_info, block_rsv, global_rsv, 0);
	if (released > 0)
		trace_btrfs_space_reservation(fs_info, "delalloc",
					      btrfs_ino(inode), released, 0);
	if (qgroup_free)
		btrfs_qgroup_free_meta_prealloc(inode->root, released);
	else
		btrfs_qgroup_convert_reserved_meta(inode->root, released);
}

void btrfs_block_rsv_release(struct btrfs_fs_info *fs_info,
			     struct btrfs_block_rsv *block_rsv,
			     u64 num_bytes)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;

	if (global_rsv == block_rsv ||
	    block_rsv->space_info != global_rsv->space_info)
		global_rsv = NULL;
	block_rsv_release_bytes(fs_info, block_rsv, global_rsv, num_bytes);
}

static void update_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;
	struct btrfs_space_info *sinfo = block_rsv->space_info;
	u64 num_bytes;

	/*
	 * The global block rsv is based on the size of the extent tree, the
	 * checksum tree and the root tree.  If the fs is empty we want to set
	 * it to a minimal amount for safety.
	 */
	num_bytes = btrfs_root_used(&fs_info->extent_root->root_item) +
		btrfs_root_used(&fs_info->csum_root->root_item) +
		btrfs_root_used(&fs_info->tree_root->root_item);
	num_bytes = max_t(u64, num_bytes, SZ_16M);

	spin_lock(&sinfo->lock);
	spin_lock(&block_rsv->lock);

	block_rsv->size = min_t(u64, num_bytes, SZ_512M);

	if (block_rsv->reserved < block_rsv->size) {
		num_bytes = btrfs_space_info_used(sinfo, true);
		if (sinfo->total_bytes > num_bytes) {
			num_bytes = sinfo->total_bytes - num_bytes;
			num_bytes = min(num_bytes,
					block_rsv->size - block_rsv->reserved);
			block_rsv->reserved += num_bytes;
			sinfo->bytes_may_use += num_bytes;
			trace_btrfs_space_reservation(fs_info, "space_info",
						      sinfo->flags, num_bytes,
						      1);
		}
	} else if (block_rsv->reserved > block_rsv->size) {
		num_bytes = block_rsv->reserved - block_rsv->size;
		sinfo->bytes_may_use -= num_bytes;
		trace_btrfs_space_reservation(fs_info, "space_info",
				      sinfo->flags, num_bytes, 0);
		block_rsv->reserved = block_rsv->size;
	}

	if (block_rsv->reserved == block_rsv->size)
		block_rsv->full = 1;
	else
		block_rsv->full = 0;

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
	fs_info->trans_block_rsv.space_info = space_info;
	fs_info->empty_block_rsv.space_info = space_info;
	fs_info->delayed_block_rsv.space_info = space_info;

	fs_info->extent_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->csum_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->dev_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->tree_root->block_rsv = &fs_info->global_block_rsv;
	if (fs_info->quota_root)
		fs_info->quota_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->chunk_root->block_rsv = &fs_info->chunk_block_rsv;

	update_global_block_rsv(fs_info);
}

static void release_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	block_rsv_release_bytes(fs_info, &fs_info->global_block_rsv, NULL,
				(u64)-1);
	WARN_ON(fs_info->trans_block_rsv.size > 0);
	WARN_ON(fs_info->trans_block_rsv.reserved > 0);
	WARN_ON(fs_info->chunk_block_rsv.size > 0);
	WARN_ON(fs_info->chunk_block_rsv.reserved > 0);
	WARN_ON(fs_info->delayed_block_rsv.size > 0);
	WARN_ON(fs_info->delayed_block_rsv.reserved > 0);
}


/*
 * To be called after all the new block groups attached to the transaction
 * handle have been created (btrfs_create_pending_block_groups()).
 */
void btrfs_trans_release_chunk_metadata(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;

	if (!trans->chunk_bytes_reserved)
		return;

	WARN_ON_ONCE(!list_empty(&trans->new_bgs));

	block_rsv_release_bytes(fs_info, &fs_info->chunk_block_rsv, NULL,
				trans->chunk_bytes_reserved);
	trans->chunk_bytes_reserved = 0;
}

/* Can only return 0 or -ENOSPC */
int btrfs_orphan_reserve_metadata(struct btrfs_trans_handle *trans,
				  struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->vfs_inode.i_sb);
	struct btrfs_root *root = inode->root;
	/*
	 * We always use trans->block_rsv here as we will have reserved space
	 * for our orphan when starting the transaction, using get_block_rsv()
	 * here will sometimes make us choose the wrong block rsv as we could be
	 * doing a reloc inode for a non refcounted root.
	 */
	struct btrfs_block_rsv *src_rsv = trans->block_rsv;
	struct btrfs_block_rsv *dst_rsv = root->orphan_block_rsv;

	/*
	 * We need to hold space in order to delete our orphan item once we've
	 * added it, so this takes the reservation so we can release it later
	 * when we are truly done with the orphan item.
	 */
	u64 num_bytes = btrfs_calc_trans_metadata_size(fs_info, 1);

	trace_btrfs_space_reservation(fs_info, "orphan", btrfs_ino(inode),
			num_bytes, 1);
	return btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, 1);
}

void btrfs_orphan_release_metadata(struct btrfs_inode *inode)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->vfs_inode.i_sb);
	struct btrfs_root *root = inode->root;
	u64 num_bytes = btrfs_calc_trans_metadata_size(fs_info, 1);

	trace_btrfs_space_reservation(fs_info, "orphan", btrfs_ino(inode),
			num_bytes, 0);
	btrfs_block_rsv_release(fs_info, root->orphan_block_rsv, num_bytes);
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
 * the space reservation mechanism in start_transaction().
 */
int btrfs_subvolume_reserve_metadata(struct btrfs_root *root,
				     struct btrfs_block_rsv *rsv,
				     int items,
				     u64 *qgroup_reserved,
				     bool use_global_rsv)
{
	u64 num_bytes;
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;

	if (test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags)) {
		/* One for parent inode, two for dir entries */
		num_bytes = 3 * fs_info->nodesize;
		ret = btrfs_qgroup_reserve_meta_prealloc(root, num_bytes, true);
		if (ret)
			return ret;
	} else {
		num_bytes = 0;
	}

	*qgroup_reserved = num_bytes;

	num_bytes = btrfs_calc_trans_metadata_size(fs_info, items);
	rsv->space_info = __find_space_info(fs_info,
					    BTRFS_BLOCK_GROUP_METADATA);
	ret = btrfs_block_rsv_add(root, rsv, num_bytes,
				  BTRFS_RESERVE_FLUSH_ALL);

	if (ret == -ENOSPC && use_global_rsv)
		ret = btrfs_block_rsv_migrate(global_rsv, rsv, num_bytes, 1);

	if (ret && *qgroup_reserved)
		btrfs_qgroup_free_meta_prealloc(root, *qgroup_reserved);

	return ret;
}

void btrfs_subvolume_release_metadata(struct btrfs_fs_info *fs_info,
				      struct btrfs_block_rsv *rsv)
{
	btrfs_block_rsv_release(fs_info, rsv, (u64)-1);
}

static void btrfs_calculate_inode_block_rsv_size(struct btrfs_fs_info *fs_info,
						 struct btrfs_inode *inode)
{
	struct btrfs_block_rsv *block_rsv = &inode->block_rsv;
	u64 reserve_size = 0;
	u64 csum_leaves;
	unsigned outstanding_extents;

	lockdep_assert_held(&inode->lock);
	outstanding_extents = inode->outstanding_extents;
	if (outstanding_extents)
		reserve_size = btrfs_calc_trans_metadata_size(fs_info,
						outstanding_extents + 1);
	csum_leaves = btrfs_csum_bytes_to_leaves(fs_info,
						 inode->csum_bytes);
	reserve_size += btrfs_calc_trans_metadata_size(fs_info,
						       csum_leaves);

	spin_lock(&block_rsv->lock);
	block_rsv->size = reserve_size;
	spin_unlock(&block_rsv->lock);
}

int btrfs_delalloc_reserve_metadata(struct btrfs_inode *inode, u64 num_bytes)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->vfs_inode.i_sb);
	unsigned nr_extents;
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_FLUSH_ALL;
	int ret = 0;
	bool delalloc_lock = true;

	/* If we are a free space inode we need to not flush since we will be in
	 * the middle of a transaction commit.  We also don't need the delalloc
	 * mutex since we won't race with anybody.  We need this mostly to make
	 * lockdep shut its filthy mouth.
	 *
	 * If we have a transaction open (can happen if we call truncate_block
	 * from truncate), then we need FLUSH_LIMIT so we don't deadlock.
	 */
	if (btrfs_is_free_space_inode(inode)) {
		flush = BTRFS_RESERVE_NO_FLUSH;
		delalloc_lock = false;
	} else {
		if (current->journal_info)
			flush = BTRFS_RESERVE_FLUSH_LIMIT;

		if (btrfs_transaction_in_commit(fs_info))
			schedule_timeout(1);
	}

	if (delalloc_lock)
		mutex_lock(&inode->delalloc_mutex);

	num_bytes = ALIGN(num_bytes, fs_info->sectorsize);

	/* Add our new extents and calculate the new rsv size. */
	spin_lock(&inode->lock);
	nr_extents = count_max_extents(num_bytes);
	btrfs_mod_outstanding_extents(inode, nr_extents);
	inode->csum_bytes += num_bytes;
	btrfs_calculate_inode_block_rsv_size(fs_info, inode);
	spin_unlock(&inode->lock);

	ret = btrfs_inode_rsv_refill(inode, flush);
	if (unlikely(ret))
		goto out_fail;

	if (delalloc_lock)
		mutex_unlock(&inode->delalloc_mutex);
	return 0;

out_fail:
	spin_lock(&inode->lock);
	nr_extents = count_max_extents(num_bytes);
	btrfs_mod_outstanding_extents(inode, -nr_extents);
	inode->csum_bytes -= num_bytes;
	btrfs_calculate_inode_block_rsv_size(fs_info, inode);
	spin_unlock(&inode->lock);

	btrfs_inode_rsv_release(inode, true);
	if (delalloc_lock)
		mutex_unlock(&inode->delalloc_mutex);
	return ret;
}

/**
 * btrfs_delalloc_release_metadata - release a metadata reservation for an inode
 * @inode: the inode to release the reservation for.
 * @num_bytes: the number of bytes we are releasing.
 * @qgroup_free: free qgroup reservation or convert it to per-trans reservation
 *
 * This will release the metadata reservation for an inode.  This can be called
 * once we complete IO for a given set of bytes to release their metadata
 * reservations, or on error for the same reason.
 */
void btrfs_delalloc_release_metadata(struct btrfs_inode *inode, u64 num_bytes,
				     bool qgroup_free)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->vfs_inode.i_sb);

	num_bytes = ALIGN(num_bytes, fs_info->sectorsize);
	spin_lock(&inode->lock);
	inode->csum_bytes -= num_bytes;
	btrfs_calculate_inode_block_rsv_size(fs_info, inode);
	spin_unlock(&inode->lock);

	if (btrfs_is_testing(fs_info))
		return;

	btrfs_inode_rsv_release(inode, qgroup_free);
}

/**
 * btrfs_delalloc_release_extents - release our outstanding_extents
 * @inode: the inode to balance the reservation for.
 * @num_bytes: the number of bytes we originally reserved with
 * @qgroup_free: do we need to free qgroup meta reservation or convert them.
 *
 * When we reserve space we increase outstanding_extents for the extents we may
 * add.  Once we've set the range as delalloc or created our ordered extents we
 * have outstanding_extents to track the real usage, so we use this to free our
 * temporarily tracked outstanding_extents.  This _must_ be used in conjunction
 * with btrfs_delalloc_reserve_metadata.
 */
void btrfs_delalloc_release_extents(struct btrfs_inode *inode, u64 num_bytes,
				    bool qgroup_free)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->vfs_inode.i_sb);
	unsigned num_extents;

	spin_lock(&inode->lock);
	num_extents = count_max_extents(num_bytes);
	btrfs_mod_outstanding_extents(inode, -num_extents);
	btrfs_calculate_inode_block_rsv_size(fs_info, inode);
	spin_unlock(&inode->lock);

	if (btrfs_is_testing(fs_info))
		return;

	btrfs_inode_rsv_release(inode, qgroup_free);
}

/**
 * btrfs_delalloc_reserve_space - reserve data and metadata space for
 * delalloc
 * @inode: inode we're writing to
 * @start: start range we are writing to
 * @len: how long the range we are writing to
 * @reserved: mandatory parameter, record actually reserved qgroup ranges of
 * 	      current reservation.
 *
 * This will do the following things
 *
 * o reserve space in data space info for num bytes
 *   and reserve precious corresponding qgroup space
 *   (Done in check_data_free_space)
 *
 * o reserve space for metadata space, based on the number of outstanding
 *   extents and how much csums will be needed
 *   also reserve metadata space in a per root over-reserve method.
 * o add to the inodes->delalloc_bytes
 * o add it to the fs_info's delalloc inodes list.
 *   (Above 3 all done in delalloc_reserve_metadata)
 *
 * Return 0 for success
 * Return <0 for error(-ENOSPC or -EQUOT)
 */
int btrfs_delalloc_reserve_space(struct inode *inode,
			struct extent_changeset **reserved, u64 start, u64 len)
{
	int ret;

	ret = btrfs_check_data_free_space(inode, reserved, start, len);
	if (ret < 0)
		return ret;
	ret = btrfs_delalloc_reserve_metadata(BTRFS_I(inode), len);
	if (ret < 0)
		btrfs_free_reserved_data_space(inode, *reserved, start, len);
	return ret;
}

/**
 * btrfs_delalloc_release_space - release data and metadata space for delalloc
 * @inode: inode we're releasing space for
 * @start: start position of the space already reserved
 * @len: the len of the space already reserved
 * @release_bytes: the len of the space we consumed or didn't use
 *
 * This function will release the metadata space that was not used and will
 * decrement ->delalloc_bytes and remove it from the fs_info delalloc_inodes
 * list if there are no delalloc bytes left.
 * Also it will handle the qgroup reserved space.
 */
void btrfs_delalloc_release_space(struct inode *inode,
				  struct extent_changeset *reserved,
				  u64 start, u64 len, bool qgroup_free)
{
	btrfs_delalloc_release_metadata(BTRFS_I(inode), len, qgroup_free);
	btrfs_free_reserved_data_space(inode, reserved, start, len);
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_fs_info *info, u64 bytenr,
			      u64 num_bytes, int alloc)
{
	struct btrfs_block_group_cache *cache = NULL;
	u64 total = num_bytes;
	u64 old_val;
	u64 byte_in_group;
	int factor;

	/* block accounting for super block */
	spin_lock(&info->delalloc_root_lock);
	old_val = btrfs_super_bytes_used(info->super_copy);
	if (alloc)
		old_val += num_bytes;
	else
		old_val -= num_bytes;
	btrfs_set_super_bytes_used(info->super_copy, old_val);
	spin_unlock(&info->delalloc_root_lock);

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

		if (btrfs_test_opt(info, SPACE_CACHE) &&
		    cache->disk_cache_state < BTRFS_DC_CLEAR)
			cache->disk_cache_state = BTRFS_DC_CLEAR;

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

			trace_btrfs_space_reservation(info, "pinned",
						      cache->space_info->flags,
						      num_bytes, 1);
			percpu_counter_add(&cache->space_info->total_bytes_pinned,
					   num_bytes);
			set_extent_dirty(info->pinned_extents,
					 bytenr, bytenr + num_bytes - 1,
					 GFP_NOFS | __GFP_NOFAIL);
		}

		spin_lock(&trans->transaction->dirty_bgs_lock);
		if (list_empty(&cache->dirty_list)) {
			list_add_tail(&cache->dirty_list,
				      &trans->transaction->dirty_bgs);
				trans->transaction->num_dirty_bgs++;
			btrfs_get_block_group(cache);
		}
		spin_unlock(&trans->transaction->dirty_bgs_lock);

		/*
		 * No longer have used bytes in this block group, queue it for
		 * deletion. We do this after adding the block group to the
		 * dirty list to avoid races between cleaner kthread and space
		 * cache writeout.
		 */
		if (!alloc && old_val == 0) {
			spin_lock(&info->unused_bgs_lock);
			if (list_empty(&cache->bg_list)) {
				btrfs_get_block_group(cache);
				list_add_tail(&cache->bg_list,
					      &info->unused_bgs);
			}
			spin_unlock(&info->unused_bgs_lock);
		}

		btrfs_put_block_group(cache);
		total -= num_bytes;
		bytenr += num_bytes;
	}
	return 0;
}

static u64 first_logical_byte(struct btrfs_fs_info *fs_info, u64 search_start)
{
	struct btrfs_block_group_cache *cache;
	u64 bytenr;

	spin_lock(&fs_info->block_group_cache_lock);
	bytenr = fs_info->first_logical_byte;
	spin_unlock(&fs_info->block_group_cache_lock);

	if (bytenr < (u64)-1)
		return bytenr;

	cache = btrfs_lookup_first_block_group(fs_info, search_start);
	if (!cache)
		return 0;

	bytenr = cache->key.objectid;
	btrfs_put_block_group(cache);

	return bytenr;
}

static int pin_down_extent(struct btrfs_fs_info *fs_info,
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

	trace_btrfs_space_reservation(fs_info, "pinned",
				      cache->space_info->flags, num_bytes, 1);
	percpu_counter_add(&cache->space_info->total_bytes_pinned, num_bytes);
	set_extent_dirty(fs_info->pinned_extents, bytenr,
			 bytenr + num_bytes - 1, GFP_NOFS | __GFP_NOFAIL);
	return 0;
}

/*
 * this function must be called within transaction
 */
int btrfs_pin_extent(struct btrfs_fs_info *fs_info,
		     u64 bytenr, u64 num_bytes, int reserved)
{
	struct btrfs_block_group_cache *cache;

	cache = btrfs_lookup_block_group(fs_info, bytenr);
	BUG_ON(!cache); /* Logic error */

	pin_down_extent(fs_info, cache, bytenr, num_bytes, reserved);

	btrfs_put_block_group(cache);
	return 0;
}

/*
 * this function must be called within transaction
 */
int btrfs_pin_extent_for_log_replay(struct btrfs_fs_info *fs_info,
				    u64 bytenr, u64 num_bytes)
{
	struct btrfs_block_group_cache *cache;
	int ret;

	cache = btrfs_lookup_block_group(fs_info, bytenr);
	if (!cache)
		return -EINVAL;

	/*
	 * pull in the free space cache (if any) so that our pin
	 * removes the free space from the cache.  We have load_only set
	 * to one because the slow code to read in the free extents does check
	 * the pinned extents.
	 */
	cache_block_group(cache, 1);

	pin_down_extent(fs_info, cache, bytenr, num_bytes, 0);

	/* remove us from the free space cache (if we're there at all) */
	ret = btrfs_remove_free_space(cache, bytenr, num_bytes);
	btrfs_put_block_group(cache);
	return ret;
}

static int __exclude_logged_extent(struct btrfs_fs_info *fs_info,
				   u64 start, u64 num_bytes)
{
	int ret;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_caching_control *caching_ctl;

	block_group = btrfs_lookup_block_group(fs_info, start);
	if (!block_group)
		return -EINVAL;

	cache_block_group(block_group, 0);
	caching_ctl = get_caching_control(block_group);

	if (!caching_ctl) {
		/* Logic error */
		BUG_ON(!block_group_cache_done(block_group));
		ret = btrfs_remove_free_space(block_group, start, num_bytes);
	} else {
		mutex_lock(&caching_ctl->mutex);

		if (start >= caching_ctl->progress) {
			ret = add_excluded_extent(fs_info, start, num_bytes);
		} else if (start + num_bytes <= caching_ctl->progress) {
			ret = btrfs_remove_free_space(block_group,
						      start, num_bytes);
		} else {
			num_bytes = caching_ctl->progress - start;
			ret = btrfs_remove_free_space(block_group,
						      start, num_bytes);
			if (ret)
				goto out_lock;

			num_bytes = (start + num_bytes) -
				caching_ctl->progress;
			start = caching_ctl->progress;
			ret = add_excluded_extent(fs_info, start, num_bytes);
		}
out_lock:
		mutex_unlock(&caching_ctl->mutex);
		put_caching_control(caching_ctl);
	}
	btrfs_put_block_group(block_group);
	return ret;
}

int btrfs_exclude_logged_extents(struct btrfs_fs_info *fs_info,
				 struct extent_buffer *eb)
{
	struct btrfs_file_extent_item *item;
	struct btrfs_key key;
	int found_type;
	int i;

	if (!btrfs_fs_incompat(fs_info, MIXED_GROUPS))
		return 0;

	for (i = 0; i < btrfs_header_nritems(eb); i++) {
		btrfs_item_key_to_cpu(eb, &key, i);
		if (key.type != BTRFS_EXTENT_DATA_KEY)
			continue;
		item = btrfs_item_ptr(eb, i, struct btrfs_file_extent_item);
		found_type = btrfs_file_extent_type(eb, item);
		if (found_type == BTRFS_FILE_EXTENT_INLINE)
			continue;
		if (btrfs_file_extent_disk_bytenr(eb, item) == 0)
			continue;
		key.objectid = btrfs_file_extent_disk_bytenr(eb, item);
		key.offset = btrfs_file_extent_disk_num_bytes(eb, item);
		__exclude_logged_extent(fs_info, key.objectid, key.offset);
	}

	return 0;
}

static void
btrfs_inc_block_group_reservations(struct btrfs_block_group_cache *bg)
{
	atomic_inc(&bg->reservations);
}

void btrfs_dec_block_group_reservations(struct btrfs_fs_info *fs_info,
					const u64 start)
{
	struct btrfs_block_group_cache *bg;

	bg = btrfs_lookup_block_group(fs_info, start);
	ASSERT(bg);
	if (atomic_dec_and_test(&bg->reservations))
		wake_up_atomic_t(&bg->reservations);
	btrfs_put_block_group(bg);
}

void btrfs_wait_block_group_reservations(struct btrfs_block_group_cache *bg)
{
	struct btrfs_space_info *space_info = bg->space_info;

	ASSERT(bg->ro);

	if (!(bg->flags & BTRFS_BLOCK_GROUP_DATA))
		return;

	/*
	 * Our block group is read only but before we set it to read only,
	 * some task might have had allocated an extent from it already, but it
	 * has not yet created a respective ordered extent (and added it to a
	 * root's list of ordered extents).
	 * Therefore wait for any task currently allocating extents, since the
	 * block group's reservations counter is incremented while a read lock
	 * on the groups' semaphore is held and decremented after releasing
	 * the read access on that semaphore and creating the ordered extent.
	 */
	down_write(&space_info->groups_sem);
	up_write(&space_info->groups_sem);

	wait_on_atomic_t(&bg->reservations, atomic_t_wait,
			 TASK_UNINTERRUPTIBLE);
}

/**
 * btrfs_add_reserved_bytes - update the block_group and space info counters
 * @cache:	The cache we are manipulating
 * @ram_bytes:  The number of bytes of file content, and will be same to
 *              @num_bytes except for the compress path.
 * @num_bytes:	The number of bytes in question
 * @delalloc:   The blocks are allocated for the delalloc write
 *
 * This is called by the allocator when it reserves space. If this is a
 * reservation and the block group has become read only we cannot make the
 * reservation and return -EAGAIN, otherwise this function always succeeds.
 */
static int btrfs_add_reserved_bytes(struct btrfs_block_group_cache *cache,
				    u64 ram_bytes, u64 num_bytes, int delalloc)
{
	struct btrfs_space_info *space_info = cache->space_info;
	int ret = 0;

	spin_lock(&space_info->lock);
	spin_lock(&cache->lock);
	if (cache->ro) {
		ret = -EAGAIN;
	} else {
		cache->reserved += num_bytes;
		space_info->bytes_reserved += num_bytes;

		trace_btrfs_space_reservation(cache->fs_info,
				"space_info", space_info->flags,
				ram_bytes, 0);
		space_info->bytes_may_use -= ram_bytes;
		if (delalloc)
			cache->delalloc_bytes += num_bytes;
	}
	spin_unlock(&cache->lock);
	spin_unlock(&space_info->lock);
	return ret;
}

/**
 * btrfs_free_reserved_bytes - update the block_group and space info counters
 * @cache:      The cache we are manipulating
 * @num_bytes:  The number of bytes in question
 * @delalloc:   The blocks are allocated for the delalloc write
 *
 * This is called by somebody who is freeing space that was never actually used
 * on disk.  For example if you reserve some space for a new leaf in transaction
 * A and before transaction A commits you free that leaf, you call this with
 * reserve set to 0 in order to clear the reservation.
 */

static int btrfs_free_reserved_bytes(struct btrfs_block_group_cache *cache,
				     u64 num_bytes, int delalloc)
{
	struct btrfs_space_info *space_info = cache->space_info;
	int ret = 0;

	spin_lock(&space_info->lock);
	spin_lock(&cache->lock);
	if (cache->ro)
		space_info->bytes_readonly += num_bytes;
	cache->reserved -= num_bytes;
	space_info->bytes_reserved -= num_bytes;

	if (delalloc)
		cache->delalloc_bytes -= num_bytes;
	spin_unlock(&cache->lock);
	spin_unlock(&space_info->lock);
	return ret;
}
void btrfs_prepare_extent_commit(struct btrfs_fs_info *fs_info)
{
	struct btrfs_caching_control *next;
	struct btrfs_caching_control *caching_ctl;
	struct btrfs_block_group_cache *cache;

	down_write(&fs_info->commit_root_sem);

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

	up_write(&fs_info->commit_root_sem);

	update_global_block_rsv(fs_info);
}

/*
 * Returns the free cluster for the given space info and sets empty_cluster to
 * what it should be based on the mount options.
 */
static struct btrfs_free_cluster *
fetch_cluster_info(struct btrfs_fs_info *fs_info,
		   struct btrfs_space_info *space_info, u64 *empty_cluster)
{
	struct btrfs_free_cluster *ret = NULL;

	*empty_cluster = 0;
	if (btrfs_mixed_space_info(space_info))
		return ret;

	if (space_info->flags & BTRFS_BLOCK_GROUP_METADATA) {
		ret = &fs_info->meta_alloc_cluster;
		if (btrfs_test_opt(fs_info, SSD))
			*empty_cluster = SZ_2M;
		else
			*empty_cluster = SZ_64K;
	} else if ((space_info->flags & BTRFS_BLOCK_GROUP_DATA) &&
		   btrfs_test_opt(fs_info, SSD_SPREAD)) {
		*empty_cluster = SZ_2M;
		ret = &fs_info->data_alloc_cluster;
	}

	return ret;
}

static int unpin_extent_range(struct btrfs_fs_info *fs_info,
			      u64 start, u64 end,
			      const bool return_free_space)
{
	struct btrfs_block_group_cache *cache = NULL;
	struct btrfs_space_info *space_info;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	struct btrfs_free_cluster *cluster = NULL;
	u64 len;
	u64 total_unpinned = 0;
	u64 empty_cluster = 0;
	bool readonly;

	while (start <= end) {
		readonly = false;
		if (!cache ||
		    start >= cache->key.objectid + cache->key.offset) {
			if (cache)
				btrfs_put_block_group(cache);
			total_unpinned = 0;
			cache = btrfs_lookup_block_group(fs_info, start);
			BUG_ON(!cache); /* Logic error */

			cluster = fetch_cluster_info(fs_info,
						     cache->space_info,
						     &empty_cluster);
			empty_cluster <<= 1;
		}

		len = cache->key.objectid + cache->key.offset - start;
		len = min(len, end + 1 - start);

		if (start < cache->last_byte_to_unpin) {
			len = min(len, cache->last_byte_to_unpin - start);
			if (return_free_space)
				btrfs_add_free_space(cache, start, len);
		}

		start += len;
		total_unpinned += len;
		space_info = cache->space_info;

		/*
		 * If this space cluster has been marked as fragmented and we've
		 * unpinned enough in this block group to potentially allow a
		 * cluster to be created inside of it go ahead and clear the
		 * fragmented check.
		 */
		if (cluster && cluster->fragmented &&
		    total_unpinned > empty_cluster) {
			spin_lock(&cluster->lock);
			cluster->fragmented = 0;
			spin_unlock(&cluster->lock);
		}

		spin_lock(&space_info->lock);
		spin_lock(&cache->lock);
		cache->pinned -= len;
		space_info->bytes_pinned -= len;

		trace_btrfs_space_reservation(fs_info, "pinned",
					      space_info->flags, len, 0);
		space_info->max_extent_size = 0;
		percpu_counter_add(&space_info->total_bytes_pinned, -len);
		if (cache->ro) {
			space_info->bytes_readonly += len;
			readonly = true;
		}
		spin_unlock(&cache->lock);
		if (!readonly && return_free_space &&
		    global_rsv->space_info == space_info) {
			u64 to_add = len;

			spin_lock(&global_rsv->lock);
			if (!global_rsv->full) {
				to_add = min(len, global_rsv->size -
					     global_rsv->reserved);
				global_rsv->reserved += to_add;
				space_info->bytes_may_use += to_add;
				if (global_rsv->reserved >= global_rsv->size)
					global_rsv->full = 1;
				trace_btrfs_space_reservation(fs_info,
							      "space_info",
							      space_info->flags,
							      to_add, 1);
				len -= to_add;
			}
			spin_unlock(&global_rsv->lock);
			/* Add to any tickets we may have */
			if (len)
				space_info_add_new_bytes(fs_info, space_info,
							 len);
		}
		spin_unlock(&space_info->lock);
	}

	if (cache)
		btrfs_put_block_group(cache);
	return 0;
}

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_block_group_cache *block_group, *tmp;
	struct list_head *deleted_bgs;
	struct extent_io_tree *unpin;
	u64 start;
	u64 end;
	int ret;

	if (fs_info->pinned_extents == &fs_info->freed_extents[0])
		unpin = &fs_info->freed_extents[1];
	else
		unpin = &fs_info->freed_extents[0];

	while (!trans->aborted) {
		mutex_lock(&fs_info->unused_bg_unpin_mutex);
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY, NULL);
		if (ret) {
			mutex_unlock(&fs_info->unused_bg_unpin_mutex);
			break;
		}

		if (btrfs_test_opt(fs_info, DISCARD))
			ret = btrfs_discard_extent(fs_info, start,
						   end + 1 - start, NULL);

		clear_extent_dirty(unpin, start, end);
		unpin_extent_range(fs_info, start, end, true);
		mutex_unlock(&fs_info->unused_bg_unpin_mutex);
		cond_resched();
	}

	/*
	 * Transaction is finished.  We don't need the lock anymore.  We
	 * do need to clean up the block groups in case of a transaction
	 * abort.
	 */
	deleted_bgs = &trans->transaction->deleted_bgs;
	list_for_each_entry_safe(block_group, tmp, deleted_bgs, bg_list) {
		u64 trimmed = 0;

		ret = -EROFS;
		if (!trans->aborted)
			ret = btrfs_discard_extent(fs_info,
						   block_group->key.objectid,
						   block_group->key.offset,
						   &trimmed);

		list_del_init(&block_group->bg_list);
		btrfs_put_block_group_trimming(block_group);
		btrfs_put_block_group(block_group);

		if (ret) {
			const char *errstr = btrfs_decode_error(ret);
			btrfs_warn(fs_info,
			   "discard failed while removing blockgroup: errno=%d %s",
				   ret, errstr);
		}
	}

	return 0;
}

static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *info,
				struct btrfs_delayed_ref_node *node, u64 parent,
				u64 root_objectid, u64 owner_objectid,
				u64 owner_offset, int refs_to_drop,
				struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_key key;
	struct btrfs_path *path;
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
	u64 bytenr = node->bytenr;
	u64 num_bytes = node->num_bytes;
	int last_ref = 0;
	bool skinny_metadata = btrfs_fs_incompat(info, SKINNY_METADATA);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = READA_FORWARD;
	path->leave_spinning = 1;

	is_data = owner_objectid >= BTRFS_FIRST_FREE_OBJECTID;
	BUG_ON(!is_data && refs_to_drop != 1);

	if (is_data)
		skinny_metadata = false;

	ret = lookup_extent_backref(trans, info, path, &iref,
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
			ret = remove_extent_backref(trans, info, path, NULL,
						    refs_to_drop,
						    is_data, &last_ref);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
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
				key.objectid = bytenr;
				key.type = BTRFS_EXTENT_ITEM_KEY;
				key.offset = num_bytes;
				btrfs_release_path(path);
				ret = btrfs_search_slot(trans, extent_root,
							&key, path, -1, 1);
			}

			if (ret) {
				btrfs_err(info,
					  "umm, got %d back from search, was looking for %llu",
					  ret, bytenr);
				if (ret > 0)
					btrfs_print_leaf(path->nodes[0]);
			}
			if (ret < 0) {
				btrfs_abort_transaction(trans, ret);
				goto out;
			}
			extent_slot = path->slots[0];
		}
	} else if (WARN_ON(ret == -ENOENT)) {
		btrfs_print_leaf(path->nodes[0]);
		btrfs_err(info,
			"unable to find ref byte nr %llu parent %llu root %llu  owner %llu offset %llu",
			bytenr, parent, root_objectid, owner_objectid,
			owner_offset);
		btrfs_abort_transaction(trans, ret);
		goto out;
	} else {
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, extent_slot);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		BUG_ON(found_extent || extent_slot != path->slots[0]);
		ret = convert_extent_item_v0(trans, info, path, owner_objectid,
					     0);
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
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
			btrfs_err(info,
				  "umm, got %d back from search, was looking for %llu",
				ret, bytenr);
			btrfs_print_leaf(path->nodes[0]);
		}
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
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
	if (refs < refs_to_drop) {
		btrfs_err(info,
			  "trying to drop %d refs but we only have %Lu for bytenr %Lu",
			  refs_to_drop, refs, bytenr);
		ret = -EINVAL;
		btrfs_abort_transaction(trans, ret);
		goto out;
	}
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
			ret = remove_extent_backref(trans, info, path,
						    iref, refs_to_drop,
						    is_data, &last_ref);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				goto out;
			}
		}
	} else {
		if (found_extent) {
			BUG_ON(is_data && refs_to_drop !=
			       extent_data_ref_count(path, iref));
			if (iref) {
				BUG_ON(path->slots[0] != extent_slot);
			} else {
				BUG_ON(path->slots[0] != extent_slot + 1);
				path->slots[0] = extent_slot;
				num_to_del = 2;
			}
		}

		last_ref = 1;
		ret = btrfs_del_items(trans, extent_root, path, path->slots[0],
				      num_to_del);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
		btrfs_release_path(path);

		if (is_data) {
			ret = btrfs_del_csums(trans, info, bytenr, num_bytes);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				goto out;
			}
		}

		ret = add_to_free_space_tree(trans, info, bytenr, num_bytes);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		ret = update_block_group(trans, info, bytenr, num_bytes, 0);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
	}
	btrfs_release_path(path);

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
				      u64 bytenr)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(delayed_refs, bytenr);
	if (!head)
		goto out_delayed_unlock;

	spin_lock(&head->lock);
	if (!RB_EMPTY_ROOT(&head->ref_tree))
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
	rb_erase(&head->href_node, &delayed_refs->href_root);
	RB_CLEAR_NODE(&head->href_node);
	atomic_dec(&delayed_refs->num_entries);

	/*
	 * we don't take a ref on the node because we're removing it from the
	 * tree, so we just steal the ref the tree was holding.
	 */
	delayed_refs->num_heads--;
	if (head->processing == 0)
		delayed_refs->num_heads_ready--;
	head->processing = 0;
	spin_unlock(&head->lock);
	spin_unlock(&delayed_refs->lock);

	BUG_ON(head->extent_op);
	if (head->must_insert_reserved)
		ret = 1;

	mutex_unlock(&head->mutex);
	btrfs_put_delayed_ref_head(head);
	return ret;
out:
	spin_unlock(&head->lock);

out_delayed_unlock:
	spin_unlock(&delayed_refs->lock);
	return 0;
}

void btrfs_free_tree_block(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   struct extent_buffer *buf,
			   u64 parent, int last_ref)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int pin = 1;
	int ret;

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
		int old_ref_mod, new_ref_mod;

		btrfs_ref_tree_mod(root, buf->start, buf->len, parent,
				   root->root_key.objectid,
				   btrfs_header_level(buf), 0,
				   BTRFS_DROP_DELAYED_REF);
		ret = btrfs_add_delayed_tree_ref(fs_info, trans, buf->start,
						 buf->len, parent,
						 root->root_key.objectid,
						 btrfs_header_level(buf),
						 BTRFS_DROP_DELAYED_REF, NULL,
						 &old_ref_mod, &new_ref_mod);
		BUG_ON(ret); /* -ENOMEM */
		pin = old_ref_mod >= 0 && new_ref_mod < 0;
	}

	if (last_ref && btrfs_header_generation(buf) == trans->transid) {
		struct btrfs_block_group_cache *cache;

		if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
			ret = check_ref_cleanup(trans, buf->start);
			if (!ret)
				goto out;
		}

		pin = 0;
		cache = btrfs_lookup_block_group(fs_info, buf->start);

		if (btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
			pin_down_extent(fs_info, cache, buf->start,
					buf->len, 1);
			btrfs_put_block_group(cache);
			goto out;
		}

		WARN_ON(test_bit(EXTENT_BUFFER_DIRTY, &buf->bflags));

		btrfs_add_free_space(cache, buf->start, buf->len);
		btrfs_free_reserved_bytes(cache, buf->len, 0);
		btrfs_put_block_group(cache);
		trace_btrfs_reserved_extent_free(fs_info, buf->start, buf->len);
	}
out:
	if (pin)
		add_pinned_bytes(fs_info, buf->len, btrfs_header_level(buf),
				 root->root_key.objectid);

	if (last_ref) {
		/*
		 * Deleting the buffer, clear the corrupt flag since it doesn't
		 * matter anymore.
		 */
		clear_bit(EXTENT_BUFFER_CORRUPT, &buf->bflags);
	}
}

/* Can return -ENOMEM */
int btrfs_free_extent(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      u64 bytenr, u64 num_bytes, u64 parent, u64 root_objectid,
		      u64 owner, u64 offset)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int old_ref_mod, new_ref_mod;
	int ret;

	if (btrfs_is_testing(fs_info))
		return 0;

	if (root_objectid != BTRFS_TREE_LOG_OBJECTID)
		btrfs_ref_tree_mod(root, bytenr, num_bytes, parent,
				   root_objectid, owner, offset,
				   BTRFS_DROP_DELAYED_REF);

	/*
	 * tree log blocks never actually go into the extent allocation
	 * tree, just update pinning info and exit early.
	 */
	if (root_objectid == BTRFS_TREE_LOG_OBJECTID) {
		WARN_ON(owner >= BTRFS_FIRST_FREE_OBJECTID);
		/* unlocks the pinned mutex */
		btrfs_pin_extent(fs_info, bytenr, num_bytes, 1);
		old_ref_mod = new_ref_mod = 0;
		ret = 0;
	} else if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(fs_info, trans, bytenr,
						 num_bytes, parent,
						 root_objectid, (int)owner,
						 BTRFS_DROP_DELAYED_REF, NULL,
						 &old_ref_mod, &new_ref_mod);
	} else {
		ret = btrfs_add_delayed_data_ref(fs_info, trans, bytenr,
						 num_bytes, parent,
						 root_objectid, owner, offset,
						 0, BTRFS_DROP_DELAYED_REF,
						 &old_ref_mod, &new_ref_mod);
	}

	if (ret == 0 && old_ref_mod >= 0 && new_ref_mod < 0)
		add_pinned_bytes(fs_info, num_bytes, owner, root_objectid);

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
 *
 * Callers of this must check if cache->cached == BTRFS_CACHE_ERROR before using
 * any of the information in this block group.
 */
static noinline void
wait_block_group_cache_progress(struct btrfs_block_group_cache *cache,
				u64 num_bytes)
{
	struct btrfs_caching_control *caching_ctl;

	caching_ctl = get_caching_control(cache);
	if (!caching_ctl)
		return;

	wait_event(caching_ctl->wait, block_group_cache_done(cache) ||
		   (cache->free_space_ctl->free_space >= num_bytes));

	put_caching_control(caching_ctl);
}

static noinline int
wait_block_group_cache_done(struct btrfs_block_group_cache *cache)
{
	struct btrfs_caching_control *caching_ctl;
	int ret = 0;

	caching_ctl = get_caching_control(cache);
	if (!caching_ctl)
		return (cache->cached == BTRFS_CACHE_ERROR) ? -EIO : 0;

	wait_event(caching_ctl->wait, block_group_cache_done(cache));
	if (cache->cached == BTRFS_CACHE_ERROR)
		ret = -EIO;
	put_caching_control(caching_ctl);
	return ret;
}

static const char *btrfs_raid_type_names[BTRFS_NR_RAID_TYPES] = {
	[BTRFS_RAID_RAID10]	= "raid10",
	[BTRFS_RAID_RAID1]	= "raid1",
	[BTRFS_RAID_DUP]	= "dup",
	[BTRFS_RAID_RAID0]	= "raid0",
	[BTRFS_RAID_SINGLE]	= "single",
	[BTRFS_RAID_RAID5]	= "raid5",
	[BTRFS_RAID_RAID6]	= "raid6",
};

static const char *get_raid_name(enum btrfs_raid_types type)
{
	if (type >= BTRFS_NR_RAID_TYPES)
		return NULL;

	return btrfs_raid_type_names[type];
}

enum btrfs_loop_type {
	LOOP_CACHING_NOWAIT = 0,
	LOOP_CACHING_WAIT = 1,
	LOOP_ALLOC_CHUNK = 2,
	LOOP_NO_EMPTY_SIZE = 3,
};

static inline void
btrfs_lock_block_group(struct btrfs_block_group_cache *cache,
		       int delalloc)
{
	if (delalloc)
		down_read(&cache->data_rwsem);
}

static inline void
btrfs_grab_block_group(struct btrfs_block_group_cache *cache,
		       int delalloc)
{
	btrfs_get_block_group(cache);
	if (delalloc)
		down_read(&cache->data_rwsem);
}

static struct btrfs_block_group_cache *
btrfs_lock_cluster(struct btrfs_block_group_cache *block_group,
		   struct btrfs_free_cluster *cluster,
		   int delalloc)
{
	struct btrfs_block_group_cache *used_bg = NULL;

	spin_lock(&cluster->refill_lock);
	while (1) {
		used_bg = cluster->block_group;
		if (!used_bg)
			return NULL;

		if (used_bg == block_group)
			return used_bg;

		btrfs_get_block_group(used_bg);

		if (!delalloc)
			return used_bg;

		if (down_read_trylock(&used_bg->data_rwsem))
			return used_bg;

		spin_unlock(&cluster->refill_lock);

		/* We should only have one-level nested. */
		down_read_nested(&used_bg->data_rwsem, SINGLE_DEPTH_NESTING);

		spin_lock(&cluster->refill_lock);
		if (used_bg == cluster->block_group)
			return used_bg;

		up_read(&used_bg->data_rwsem);
		btrfs_put_block_group(used_bg);
	}
}

static inline void
btrfs_release_block_group(struct btrfs_block_group_cache *cache,
			 int delalloc)
{
	if (delalloc)
		up_read(&cache->data_rwsem);
	btrfs_put_block_group(cache);
}

/*
 * walks the btree of allocated extents and find a hole of a given size.
 * The key ins is changed to record the hole:
 * ins->objectid == start position
 * ins->flags = BTRFS_EXTENT_ITEM_KEY
 * ins->offset == the size of the hole.
 * Any available blocks before search_start are skipped.
 *
 * If there is no suitable free space, we will record the max size of
 * the free space extent currently.
 */
static noinline int find_free_extent(struct btrfs_fs_info *fs_info,
				u64 ram_bytes, u64 num_bytes, u64 empty_size,
				u64 hint_byte, struct btrfs_key *ins,
				u64 flags, int delalloc)
{
	int ret = 0;
	struct btrfs_root *root = fs_info->extent_root;
	struct btrfs_free_cluster *last_ptr = NULL;
	struct btrfs_block_group_cache *block_group = NULL;
	u64 search_start = 0;
	u64 max_extent_size = 0;
	u64 empty_cluster = 0;
	struct btrfs_space_info *space_info;
	int loop = 0;
	int index = btrfs_bg_flags_to_raid_index(flags);
	bool failed_cluster_refill = false;
	bool failed_alloc = false;
	bool use_cluster = true;
	bool have_caching_bg = false;
	bool orig_have_caching_bg = false;
	bool full_search = false;

	WARN_ON(num_bytes < fs_info->sectorsize);
	ins->type = BTRFS_EXTENT_ITEM_KEY;
	ins->objectid = 0;
	ins->offset = 0;

	trace_find_free_extent(fs_info, num_bytes, empty_size, flags);

	space_info = __find_space_info(fs_info, flags);
	if (!space_info) {
		btrfs_err(fs_info, "No space info for %llu", flags);
		return -ENOSPC;
	}

	/*
	 * If our free space is heavily fragmented we may not be able to make
	 * big contiguous allocations, so instead of doing the expensive search
	 * for free space, simply return ENOSPC with our max_extent_size so we
	 * can go ahead and search for a more manageable chunk.
	 *
	 * If our max_extent_size is large enough for our allocation simply
	 * disable clustering since we will likely not be able to find enough
	 * space to create a cluster and induce latency trying.
	 */
	if (unlikely(space_info->max_extent_size)) {
		spin_lock(&space_info->lock);
		if (space_info->max_extent_size &&
		    num_bytes > space_info->max_extent_size) {
			ins->offset = space_info->max_extent_size;
			spin_unlock(&space_info->lock);
			return -ENOSPC;
		} else if (space_info->max_extent_size) {
			use_cluster = false;
		}
		spin_unlock(&space_info->lock);
	}

	last_ptr = fetch_cluster_info(fs_info, space_info, &empty_cluster);
	if (last_ptr) {
		spin_lock(&last_ptr->lock);
		if (last_ptr->block_group)
			hint_byte = last_ptr->window_start;
		if (last_ptr->fragmented) {
			/*
			 * We still set window_start so we can keep track of the
			 * last place we found an allocation to try and save
			 * some time.
			 */
			hint_byte = last_ptr->window_start;
			use_cluster = false;
		}
		spin_unlock(&last_ptr->lock);
	}

	search_start = max(search_start, first_logical_byte(fs_info, 0));
	search_start = max(search_start, hint_byte);
	if (search_start == hint_byte) {
		block_group = btrfs_lookup_block_group(fs_info, search_start);
		/*
		 * we don't want to use the block group if it doesn't match our
		 * allocation bits, or if its not cached.
		 *
		 * However if we are re-searching with an ideal block group
		 * picked out then we don't care that the block group is cached.
		 */
		if (block_group && block_group_bits(block_group, flags) &&
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
				index = btrfs_bg_flags_to_raid_index(
						block_group->flags);
				btrfs_lock_block_group(block_group, delalloc);
				goto have_block_group;
			}
		} else if (block_group) {
			btrfs_put_block_group(block_group);
		}
	}
search:
	have_caching_bg = false;
	if (index == 0 || index == btrfs_bg_flags_to_raid_index(flags))
		full_search = true;
	down_read(&space_info->groups_sem);
	list_for_each_entry(block_group, &space_info->block_groups[index],
			    list) {
		u64 offset;
		int cached;

		/* If the block group is read-only, we can skip it entirely. */
		if (unlikely(block_group->ro))
			continue;

		btrfs_grab_block_group(block_group, delalloc);
		search_start = block_group->key.objectid;

		/*
		 * this can happen if we end up cycling through all the
		 * raid types, but we want to make sure we only allocate
		 * for the proper type.
		 */
		if (!block_group_bits(block_group, flags)) {
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
			if ((flags & extra) && !(block_group->flags & extra))
				goto loop;
		}

have_block_group:
		cached = block_group_cache_done(block_group);
		if (unlikely(!cached)) {
			have_caching_bg = true;
			ret = cache_block_group(block_group, 0);
			BUG_ON(ret < 0);
			ret = 0;
		}

		if (unlikely(block_group->cached == BTRFS_CACHE_ERROR))
			goto loop;

		/*
		 * Ok we want to try and use the cluster allocator, so
		 * lets look there
		 */
		if (last_ptr && use_cluster) {
			struct btrfs_block_group_cache *used_block_group;
			unsigned long aligned_cluster;
			/*
			 * the refill lock keeps out other
			 * people trying to start a new cluster
			 */
			used_block_group = btrfs_lock_cluster(block_group,
							      last_ptr,
							      delalloc);
			if (!used_block_group)
				goto refill_cluster;

			if (used_block_group != block_group &&
			    (used_block_group->ro ||
			     !block_group_bits(used_block_group, flags)))
				goto release_cluster;

			offset = btrfs_alloc_from_cluster(used_block_group,
						last_ptr,
						num_bytes,
						used_block_group->key.objectid,
						&max_extent_size);
			if (offset) {
				/* we have a block, we're done */
				spin_unlock(&last_ptr->refill_lock);
				trace_btrfs_reserve_extent_cluster(fs_info,
						used_block_group,
						search_start, num_bytes);
				if (used_block_group != block_group) {
					btrfs_release_block_group(block_group,
								  delalloc);
					block_group = used_block_group;
				}
				goto checks;
			}

			WARN_ON(last_ptr->block_group != used_block_group);
release_cluster:
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
			    used_block_group != block_group) {
				spin_unlock(&last_ptr->refill_lock);
				btrfs_release_block_group(used_block_group,
							  delalloc);
				goto unclustered_alloc;
			}

			/*
			 * this cluster didn't work out, free it and
			 * start over
			 */
			btrfs_return_cluster_to_free_space(NULL, last_ptr);

			if (used_block_group != block_group)
				btrfs_release_block_group(used_block_group,
							  delalloc);
refill_cluster:
			if (loop >= LOOP_NO_EMPTY_SIZE) {
				spin_unlock(&last_ptr->refill_lock);
				goto unclustered_alloc;
			}

			aligned_cluster = max_t(unsigned long,
						empty_cluster + empty_size,
					      block_group->full_stripe_len);

			/* allocate a cluster in this block group */
			ret = btrfs_find_space_cluster(fs_info, block_group,
						       last_ptr, search_start,
						       num_bytes,
						       aligned_cluster);
			if (ret == 0) {
				/*
				 * now pull our allocation out of this
				 * cluster
				 */
				offset = btrfs_alloc_from_cluster(block_group,
							last_ptr,
							num_bytes,
							search_start,
							&max_extent_size);
				if (offset) {
					/* we found one, proceed */
					spin_unlock(&last_ptr->refill_lock);
					trace_btrfs_reserve_extent_cluster(fs_info,
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
		/*
		 * We are doing an unclustered alloc, set the fragmented flag so
		 * we don't bother trying to setup a cluster again until we get
		 * more space.
		 */
		if (unlikely(last_ptr)) {
			spin_lock(&last_ptr->lock);
			last_ptr->fragmented = 1;
			spin_unlock(&last_ptr->lock);
		}
		if (cached) {
			struct btrfs_free_space_ctl *ctl =
				block_group->free_space_ctl;

			spin_lock(&ctl->tree_lock);
			if (ctl->free_space <
			    num_bytes + empty_cluster + empty_size) {
				if (ctl->free_space > max_extent_size)
					max_extent_size = ctl->free_space;
				spin_unlock(&ctl->tree_lock);
				goto loop;
			}
			spin_unlock(&ctl->tree_lock);
		}

		offset = btrfs_find_space_for_alloc(block_group, search_start,
						    num_bytes, empty_size,
						    &max_extent_size);
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
			goto loop;
		}
checks:
		search_start = ALIGN(offset, fs_info->stripesize);

		/* move on to the next group */
		if (search_start + num_bytes >
		    block_group->key.objectid + block_group->key.offset) {
			btrfs_add_free_space(block_group, offset, num_bytes);
			goto loop;
		}

		if (offset < search_start)
			btrfs_add_free_space(block_group, offset,
					     search_start - offset);
		BUG_ON(offset > search_start);

		ret = btrfs_add_reserved_bytes(block_group, ram_bytes,
				num_bytes, delalloc);
		if (ret == -EAGAIN) {
			btrfs_add_free_space(block_group, offset, num_bytes);
			goto loop;
		}
		btrfs_inc_block_group_reservations(block_group);

		/* we are all good, lets return */
		ins->objectid = search_start;
		ins->offset = num_bytes;

		trace_btrfs_reserve_extent(fs_info, block_group,
					   search_start, num_bytes);
		btrfs_release_block_group(block_group, delalloc);
		break;
loop:
		failed_cluster_refill = false;
		failed_alloc = false;
		BUG_ON(btrfs_bg_flags_to_raid_index(block_group->flags) !=
		       index);
		btrfs_release_block_group(block_group, delalloc);
		cond_resched();
	}
	up_read(&space_info->groups_sem);

	if ((loop == LOOP_CACHING_NOWAIT) && have_caching_bg
		&& !orig_have_caching_bg)
		orig_have_caching_bg = true;

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
		if (loop == LOOP_CACHING_NOWAIT) {
			/*
			 * We want to skip the LOOP_CACHING_WAIT step if we
			 * don't have any uncached bgs and we've already done a
			 * full search through.
			 */
			if (orig_have_caching_bg || !full_search)
				loop = LOOP_CACHING_WAIT;
			else
				loop = LOOP_ALLOC_CHUNK;
		} else {
			loop++;
		}

		if (loop == LOOP_ALLOC_CHUNK) {
			struct btrfs_trans_handle *trans;
			int exist = 0;

			trans = current->journal_info;
			if (trans)
				exist = 1;
			else
				trans = btrfs_join_transaction(root);

			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				goto out;
			}

			ret = do_chunk_alloc(trans, fs_info, flags,
					     CHUNK_ALLOC_FORCE);

			/*
			 * If we can't allocate a new chunk we've already looped
			 * through at least once, move on to the NO_EMPTY_SIZE
			 * case.
			 */
			if (ret == -ENOSPC)
				loop = LOOP_NO_EMPTY_SIZE;

			/*
			 * Do not bail out on ENOSPC since we
			 * can do more things.
			 */
			if (ret < 0 && ret != -ENOSPC)
				btrfs_abort_transaction(trans, ret);
			else
				ret = 0;
			if (!exist)
				btrfs_end_transaction(trans);
			if (ret)
				goto out;
		}

		if (loop == LOOP_NO_EMPTY_SIZE) {
			/*
			 * Don't loop again if we already have no empty_size and
			 * no empty_cluster.
			 */
			if (empty_size == 0 &&
			    empty_cluster == 0) {
				ret = -ENOSPC;
				goto out;
			}
			empty_size = 0;
			empty_cluster = 0;
		}

		goto search;
	} else if (!ins->objectid) {
		ret = -ENOSPC;
	} else if (ins->objectid) {
		if (!use_cluster && last_ptr) {
			spin_lock(&last_ptr->lock);
			last_ptr->window_start = ins->objectid;
			spin_unlock(&last_ptr->lock);
		}
		ret = 0;
	}
out:
	if (ret == -ENOSPC) {
		spin_lock(&space_info->lock);
		space_info->max_extent_size = max_extent_size;
		spin_unlock(&space_info->lock);
		ins->offset = max_extent_size;
	}
	return ret;
}

static void dump_space_info(struct btrfs_fs_info *fs_info,
			    struct btrfs_space_info *info, u64 bytes,
			    int dump_block_groups)
{
	struct btrfs_block_group_cache *cache;
	int index = 0;

	spin_lock(&info->lock);
	btrfs_info(fs_info, "space_info %llu has %llu free, is %sfull",
		   info->flags,
		   info->total_bytes - btrfs_space_info_used(info, true),
		   info->full ? "" : "not ");
	btrfs_info(fs_info,
		"space_info total=%llu, used=%llu, pinned=%llu, reserved=%llu, may_use=%llu, readonly=%llu",
		info->total_bytes, info->bytes_used, info->bytes_pinned,
		info->bytes_reserved, info->bytes_may_use,
		info->bytes_readonly);
	spin_unlock(&info->lock);

	if (!dump_block_groups)
		return;

	down_read(&info->groups_sem);
again:
	list_for_each_entry(cache, &info->block_groups[index], list) {
		spin_lock(&cache->lock);
		btrfs_info(fs_info,
			"block group %llu has %llu bytes, %llu used %llu pinned %llu reserved %s",
			cache->key.objectid, cache->key.offset,
			btrfs_block_group_used(&cache->item), cache->pinned,
			cache->reserved, cache->ro ? "[readonly]" : "");
		btrfs_dump_free_space(cache, bytes);
		spin_unlock(&cache->lock);
	}
	if (++index < BTRFS_NR_RAID_TYPES)
		goto again;
	up_read(&info->groups_sem);
}

/*
 * btrfs_reserve_extent - entry point to the extent allocator. Tries to find a
 *			  hole that is at least as big as @num_bytes.
 *
 * @root           -	The root that will contain this extent
 *
 * @ram_bytes      -	The amount of space in ram that @num_bytes take. This
 *			is used for accounting purposes. This value differs
 *			from @num_bytes only in the case of compressed extents.
 *
 * @num_bytes      -	Number of bytes to allocate on-disk.
 *
 * @min_alloc_size -	Indicates the minimum amount of space that the
 *			allocator should try to satisfy. In some cases
 *			@num_bytes may be larger than what is required and if
 *			the filesystem is fragmented then allocation fails.
 *			However, the presence of @min_alloc_size gives a
 *			chance to try and satisfy the smaller allocation.
 *
 * @empty_size     -	A hint that you plan on doing more COW. This is the
 *			size in bytes the allocator should try to find free
 *			next to the block it returns.  This is just a hint and
 *			may be ignored by the allocator.
 *
 * @hint_byte      -	Hint to the allocator to start searching above the byte
 *			address passed. It might be ignored.
 *
 * @ins            -	This key is modified to record the found hole. It will
 *			have the following values:
 *			ins->objectid == start position
 *			ins->flags = BTRFS_EXTENT_ITEM_KEY
 *			ins->offset == the size of the hole.
 *
 * @is_data        -	Boolean flag indicating whether an extent is
 *			allocated for data (true) or metadata (false)
 *
 * @delalloc       -	Boolean flag indicating whether this allocation is for
 *			delalloc or not. If 'true' data_rwsem of block groups
 *			is going to be acquired.
 *
 *
 * Returns 0 when an allocation succeeded or < 0 when an error occurred. In
 * case -ENOSPC is returned then @ins->offset will contain the size of the
 * largest available hole the allocator managed to find.
 */
int btrfs_reserve_extent(struct btrfs_root *root, u64 ram_bytes,
			 u64 num_bytes, u64 min_alloc_size,
			 u64 empty_size, u64 hint_byte,
			 struct btrfs_key *ins, int is_data, int delalloc)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	bool final_tried = num_bytes == min_alloc_size;
	u64 flags;
	int ret;

	flags = get_alloc_profile_by_root(root, is_data);
again:
	WARN_ON(num_bytes < fs_info->sectorsize);
	ret = find_free_extent(fs_info, ram_bytes, num_bytes, empty_size,
			       hint_byte, ins, flags, delalloc);
	if (!ret && !is_data) {
		btrfs_dec_block_group_reservations(fs_info, ins->objectid);
	} else if (ret == -ENOSPC) {
		if (!final_tried && ins->offset) {
			num_bytes = min(num_bytes >> 1, ins->offset);
			num_bytes = round_down(num_bytes,
					       fs_info->sectorsize);
			num_bytes = max(num_bytes, min_alloc_size);
			ram_bytes = num_bytes;
			if (num_bytes == min_alloc_size)
				final_tried = true;
			goto again;
		} else if (btrfs_test_opt(fs_info, ENOSPC_DEBUG)) {
			struct btrfs_space_info *sinfo;

			sinfo = __find_space_info(fs_info, flags);
			btrfs_err(fs_info,
				  "allocation failed flags %llu, wanted %llu",
				  flags, num_bytes);
			if (sinfo)
				dump_space_info(fs_info, sinfo, num_bytes, 1);
		}
	}

	return ret;
}

static int __btrfs_free_reserved_extent(struct btrfs_fs_info *fs_info,
					u64 start, u64 len,
					int pin, int delalloc)
{
	struct btrfs_block_group_cache *cache;
	int ret = 0;

	cache = btrfs_lookup_block_group(fs_info, start);
	if (!cache) {
		btrfs_err(fs_info, "Unable to find block group for %llu",
			  start);
		return -ENOSPC;
	}

	if (pin)
		pin_down_extent(fs_info, cache, start, len, 1);
	else {
		if (btrfs_test_opt(fs_info, DISCARD))
			ret = btrfs_discard_extent(fs_info, start, len, NULL);
		btrfs_add_free_space(cache, start, len);
		btrfs_free_reserved_bytes(cache, len, delalloc);
		trace_btrfs_reserved_extent_free(fs_info, start, len);
	}

	btrfs_put_block_group(cache);
	return ret;
}

int btrfs_free_reserved_extent(struct btrfs_fs_info *fs_info,
			       u64 start, u64 len, int delalloc)
{
	return __btrfs_free_reserved_extent(fs_info, start, len, 0, delalloc);
}

int btrfs_free_and_pin_reserved_extent(struct btrfs_fs_info *fs_info,
				       u64 start, u64 len)
{
	return __btrfs_free_reserved_extent(fs_info, start, len, 1, 0);
}

static int alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				      struct btrfs_fs_info *fs_info,
				      u64 parent, u64 root_objectid,
				      u64 flags, u64 owner, u64 offset,
				      struct btrfs_key *ins, int ref_mod)
{
	int ret;
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

	ret = remove_from_free_space_tree(trans, fs_info, ins->objectid,
					  ins->offset);
	if (ret)
		return ret;

	ret = update_block_group(trans, fs_info, ins->objectid, ins->offset, 1);
	if (ret) { /* -ENOENT, logic error */
		btrfs_err(fs_info, "update block group failed for %llu %llu",
			ins->objectid, ins->offset);
		BUG();
	}
	trace_btrfs_reserved_extent_alloc(fs_info, ins->objectid, ins->offset);
	return ret;
}

static int alloc_reserved_tree_block(struct btrfs_trans_handle *trans,
				     struct btrfs_fs_info *fs_info,
				     u64 parent, u64 root_objectid,
				     u64 flags, struct btrfs_disk_key *key,
				     int level, struct btrfs_key *ins)
{
	int ret;
	struct btrfs_extent_item *extent_item;
	struct btrfs_tree_block_info *block_info;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	u32 size = sizeof(*extent_item) + sizeof(*iref);
	u64 num_bytes = ins->offset;
	bool skinny_metadata = btrfs_fs_incompat(fs_info, SKINNY_METADATA);

	if (!skinny_metadata)
		size += sizeof(*block_info);

	path = btrfs_alloc_path();
	if (!path) {
		btrfs_free_and_pin_reserved_extent(fs_info, ins->objectid,
						   fs_info->nodesize);
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	if (ret) {
		btrfs_free_path(path);
		btrfs_free_and_pin_reserved_extent(fs_info, ins->objectid,
						   fs_info->nodesize);
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
		num_bytes = fs_info->nodesize;
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

	ret = remove_from_free_space_tree(trans, fs_info, ins->objectid,
					  num_bytes);
	if (ret)
		return ret;

	ret = update_block_group(trans, fs_info, ins->objectid,
				 fs_info->nodesize, 1);
	if (ret) { /* -ENOENT, logic error */
		btrfs_err(fs_info, "update block group failed for %llu %llu",
			ins->objectid, ins->offset);
		BUG();
	}

	trace_btrfs_reserved_extent_alloc(fs_info, ins->objectid,
					  fs_info->nodesize);
	return ret;
}

int btrfs_alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root, u64 owner,
				     u64 offset, u64 ram_bytes,
				     struct btrfs_key *ins)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;

	BUG_ON(root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID);

	btrfs_ref_tree_mod(root, ins->objectid, ins->offset, 0,
			   root->root_key.objectid, owner, offset,
			   BTRFS_ADD_DELAYED_EXTENT);

	ret = btrfs_add_delayed_data_ref(fs_info, trans, ins->objectid,
					 ins->offset, 0,
					 root->root_key.objectid, owner,
					 offset, ram_bytes,
					 BTRFS_ADD_DELAYED_EXTENT, NULL, NULL);
	return ret;
}

/*
 * this is used by the tree logging recovery code.  It records that
 * an extent has been allocated and makes sure to clear the free
 * space cache bits as well
 */
int btrfs_alloc_logged_file_extent(struct btrfs_trans_handle *trans,
				   struct btrfs_fs_info *fs_info,
				   u64 root_objectid, u64 owner, u64 offset,
				   struct btrfs_key *ins)
{
	int ret;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_space_info *space_info;

	/*
	 * Mixed block groups will exclude before processing the log so we only
	 * need to do the exclude dance if this fs isn't mixed.
	 */
	if (!btrfs_fs_incompat(fs_info, MIXED_GROUPS)) {
		ret = __exclude_logged_extent(fs_info, ins->objectid,
					      ins->offset);
		if (ret)
			return ret;
	}

	block_group = btrfs_lookup_block_group(fs_info, ins->objectid);
	if (!block_group)
		return -EINVAL;

	space_info = block_group->space_info;
	spin_lock(&space_info->lock);
	spin_lock(&block_group->lock);
	space_info->bytes_reserved += ins->offset;
	block_group->reserved += ins->offset;
	spin_unlock(&block_group->lock);
	spin_unlock(&space_info->lock);

	ret = alloc_reserved_file_extent(trans, fs_info, 0, root_objectid,
					 0, owner, offset, ins, 1);
	btrfs_put_block_group(block_group);
	return ret;
}

static struct extent_buffer *
btrfs_init_new_buffer(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      u64 bytenr, int level)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct extent_buffer *buf;

	buf = btrfs_find_create_tree_block(fs_info, bytenr);
	if (IS_ERR(buf))
		return buf;

	btrfs_set_header_generation(buf, trans->transid);
	btrfs_set_buffer_lockdep_class(root->root_key.objectid, buf, level);
	btrfs_tree_lock(buf);
	clean_tree_block(fs_info, buf);
	clear_bit(EXTENT_BUFFER_STALE, &buf->bflags);

	btrfs_set_lock_blocking(buf);
	set_extent_buffer_uptodate(buf);

	if (root->root_key.objectid == BTRFS_TREE_LOG_OBJECTID) {
		buf->log_index = root->log_transid % 2;
		/*
		 * we allow two log transactions at a time, use different
		 * EXENT bit to differentiate dirty pages.
		 */
		if (buf->log_index == 0)
			set_extent_dirty(&root->dirty_log_pages, buf->start,
					buf->start + buf->len - 1, GFP_NOFS);
		else
			set_extent_new(&root->dirty_log_pages, buf->start,
					buf->start + buf->len - 1);
	} else {
		buf->log_index = -1;
		set_extent_dirty(&trans->transaction->dirty_pages, buf->start,
			 buf->start + buf->len - 1, GFP_NOFS);
	}
	trans->dirty = true;
	/* this returns a buffer locked for blocking */
	return buf;
}

static struct btrfs_block_rsv *
use_block_rsv(struct btrfs_trans_handle *trans,
	      struct btrfs_root *root, u32 blocksize)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	int ret;
	bool global_updated = false;

	block_rsv = get_block_rsv(trans, root);

	if (unlikely(block_rsv->size == 0))
		goto try_reserve;
again:
	ret = block_rsv_use_bytes(block_rsv, blocksize);
	if (!ret)
		return block_rsv;

	if (block_rsv->failfast)
		return ERR_PTR(ret);

	if (block_rsv->type == BTRFS_BLOCK_RSV_GLOBAL && !global_updated) {
		global_updated = true;
		update_global_block_rsv(fs_info);
		goto again;
	}

	if (btrfs_test_opt(fs_info, ENOSPC_DEBUG)) {
		static DEFINE_RATELIMIT_STATE(_rs,
				DEFAULT_RATELIMIT_INTERVAL * 10,
				/*DEFAULT_RATELIMIT_BURST*/ 1);
		if (__ratelimit(&_rs))
			WARN(1, KERN_DEBUG
				"BTRFS: block rsv returned %d\n", ret);
	}
try_reserve:
	ret = reserve_metadata_bytes(root, block_rsv, blocksize,
				     BTRFS_RESERVE_NO_FLUSH);
	if (!ret)
		return block_rsv;
	/*
	 * If we couldn't reserve metadata bytes try and use some from
	 * the global reserve if its space type is the same as the global
	 * reservation.
	 */
	if (block_rsv->type != BTRFS_BLOCK_RSV_GLOBAL &&
	    block_rsv->space_info == global_rsv->space_info) {
		ret = block_rsv_use_bytes(global_rsv, blocksize);
		if (!ret)
			return global_rsv;
	}
	return ERR_PTR(ret);
}

static void unuse_block_rsv(struct btrfs_fs_info *fs_info,
			    struct btrfs_block_rsv *block_rsv, u32 blocksize)
{
	block_rsv_add_bytes(block_rsv, blocksize, 0);
	block_rsv_release_bytes(fs_info, block_rsv, NULL, 0);
}

/*
 * finds a free extent and does all the dirty work required for allocation
 * returns the tree buffer or an ERR_PTR on error.
 */
struct extent_buffer *btrfs_alloc_tree_block(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     u64 parent, u64 root_objectid,
					     const struct btrfs_disk_key *key,
					     int level, u64 hint,
					     u64 empty_size)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_key ins;
	struct btrfs_block_rsv *block_rsv;
	struct extent_buffer *buf;
	struct btrfs_delayed_extent_op *extent_op;
	u64 flags = 0;
	int ret;
	u32 blocksize = fs_info->nodesize;
	bool skinny_metadata = btrfs_fs_incompat(fs_info, SKINNY_METADATA);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	if (btrfs_is_testing(fs_info)) {
		buf = btrfs_init_new_buffer(trans, root, root->alloc_bytenr,
					    level);
		if (!IS_ERR(buf))
			root->alloc_bytenr += blocksize;
		return buf;
	}
#endif

	block_rsv = use_block_rsv(trans, root, blocksize);
	if (IS_ERR(block_rsv))
		return ERR_CAST(block_rsv);

	ret = btrfs_reserve_extent(root, blocksize, blocksize, blocksize,
				   empty_size, hint, &ins, 0, 0);
	if (ret)
		goto out_unuse;

	buf = btrfs_init_new_buffer(trans, root, ins.objectid, level);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto out_free_reserved;
	}

	if (root_objectid == BTRFS_TREE_RELOC_OBJECTID) {
		if (parent == 0)
			parent = ins.objectid;
		flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
	} else
		BUG_ON(parent > 0);

	if (root_objectid != BTRFS_TREE_LOG_OBJECTID) {
		extent_op = btrfs_alloc_delayed_extent_op();
		if (!extent_op) {
			ret = -ENOMEM;
			goto out_free_buf;
		}
		if (key)
			memcpy(&extent_op->key, key, sizeof(extent_op->key));
		else
			memset(&extent_op->key, 0, sizeof(extent_op->key));
		extent_op->flags_to_set = flags;
		extent_op->update_key = skinny_metadata ? false : true;
		extent_op->update_flags = true;
		extent_op->is_data = false;
		extent_op->level = level;

		btrfs_ref_tree_mod(root, ins.objectid, ins.offset, parent,
				   root_objectid, level, 0,
				   BTRFS_ADD_DELAYED_EXTENT);
		ret = btrfs_add_delayed_tree_ref(fs_info, trans, ins.objectid,
						 ins.offset, parent,
						 root_objectid, level,
						 BTRFS_ADD_DELAYED_EXTENT,
						 extent_op, NULL, NULL);
		if (ret)
			goto out_free_delayed;
	}
	return buf;

out_free_delayed:
	btrfs_free_delayed_extent_op(extent_op);
out_free_buf:
	free_extent_buffer(buf);
out_free_reserved:
	btrfs_free_reserved_extent(fs_info, ins.objectid, ins.offset, 0);
out_unuse:
	unuse_block_rsv(fs_info, block_rsv, blocksize);
	return ERR_PTR(ret);
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
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 bytenr;
	u64 generation;
	u64 refs;
	u64 flags;
	u32 nritems;
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
					BTRFS_NODEPTRS_PER_BLOCK(fs_info));
	}

	eb = path->nodes[wc->level];
	nritems = btrfs_header_nritems(eb);

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
		ret = btrfs_lookup_extent_info(trans, fs_info, bytenr,
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
		readahead_tree_block(fs_info, bytenr);
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
	struct btrfs_fs_info *fs_info = root->fs_info;
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
		ret = btrfs_lookup_extent_info(trans, fs_info,
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
		ret = btrfs_inc_ref(trans, root, eb, 1);
		BUG_ON(ret); /* -ENOMEM */
		ret = btrfs_dec_ref(trans, root, eb, 0);
		BUG_ON(ret); /* -ENOMEM */
		ret = btrfs_set_disk_extent_flags(trans, fs_info, eb->start,
						  eb->len, flag,
						  btrfs_header_level(eb), 0);
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
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 bytenr;
	u64 generation;
	u64 parent;
	u32 blocksize;
	struct btrfs_key key;
	struct btrfs_key first_key;
	struct extent_buffer *next;
	int level = wc->level;
	int reada = 0;
	int ret = 0;
	bool need_account = false;

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
	btrfs_node_key_to_cpu(path->nodes[level], &first_key,
			      path->slots[level]);
	blocksize = fs_info->nodesize;

	next = find_extent_buffer(fs_info, bytenr);
	if (!next) {
		next = btrfs_find_create_tree_block(fs_info, bytenr);
		if (IS_ERR(next))
			return PTR_ERR(next);

		btrfs_set_buffer_lockdep_class(root->root_key.objectid, next,
					       level - 1);
		reada = 1;
	}
	btrfs_tree_lock(next);
	btrfs_set_lock_blocking(next);

	ret = btrfs_lookup_extent_info(trans, fs_info, bytenr, level - 1, 1,
				       &wc->refs[level - 1],
				       &wc->flags[level - 1]);
	if (ret < 0)
		goto out_unlock;

	if (unlikely(wc->refs[level - 1] == 0)) {
		btrfs_err(fs_info, "Missing references.");
		ret = -EIO;
		goto out_unlock;
	}
	*lookup_info = 0;

	if (wc->stage == DROP_REFERENCE) {
		if (wc->refs[level - 1] > 1) {
			need_account = true;
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
		next = read_tree_block(fs_info, bytenr, generation, level - 1,
				       &first_key);
		if (IS_ERR(next)) {
			return PTR_ERR(next);
		} else if (!extent_buffer_uptodate(next)) {
			free_extent_buffer(next);
			return -EIO;
		}
		btrfs_tree_lock(next);
		btrfs_set_lock_blocking(next);
	}

	level--;
	ASSERT(level == btrfs_header_level(next));
	if (level != btrfs_header_level(next)) {
		btrfs_err(root->fs_info, "mismatched level");
		ret = -EIO;
		goto out_unlock;
	}
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
			ASSERT(root->root_key.objectid ==
			       btrfs_header_owner(path->nodes[level]));
			if (root->root_key.objectid !=
			    btrfs_header_owner(path->nodes[level])) {
				btrfs_err(root->fs_info,
						"mismatched block owner");
				ret = -EIO;
				goto out_unlock;
			}
			parent = 0;
		}

		if (need_account) {
			ret = btrfs_qgroup_trace_subtree(trans, root, next,
							 generation, level - 1);
			if (ret) {
				btrfs_err_rl(fs_info,
					     "Error %d accounting shared subtree. Quota is out of sync, rescan required.",
					     ret);
			}
		}
		ret = btrfs_free_extent(trans, root, bytenr, blocksize,
					parent, root->root_key.objectid,
					level - 1, 0);
		if (ret)
			goto out_unlock;
	}

	*lookup_info = 1;
	ret = 1;

out_unlock:
	btrfs_tree_unlock(next);
	free_extent_buffer(next);

	return ret;
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
	struct btrfs_fs_info *fs_info = root->fs_info;
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

			ret = btrfs_lookup_extent_info(trans, fs_info,
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
				ret = btrfs_dec_ref(trans, root, eb, 1);
			else
				ret = btrfs_dec_ref(trans, root, eb, 0);
			BUG_ON(ret); /* -ENOMEM */
			ret = btrfs_qgroup_trace_leaf_items(trans, fs_info, eb);
			if (ret) {
				btrfs_err_rl(fs_info,
					     "error %d accounting leaf items. Quota is out of sync, rescan required.",
					     ret);
			}
		}
		/* make block locked assertion in clean_tree_block happy */
		if (!path->locks[level] &&
		    btrfs_header_generation(eb) == trans->transid) {
			btrfs_tree_lock(eb);
			btrfs_set_lock_blocking(eb);
			path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;
		}
		clean_tree_block(fs_info, eb);
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
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct btrfs_trans_handle *trans;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_root_item *root_item = &root->root_item;
	struct walk_control *wc;
	struct btrfs_key key;
	int err = 0;
	int ret;
	int level;
	bool root_dropped = false;

	btrfs_debug(fs_info, "Drop subvolume %llu", root->objectid);

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
			path->locks[level] = BTRFS_WRITE_LOCK_BLOCKING;

			ret = btrfs_lookup_extent_info(trans, fs_info,
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
			path->locks[level] = 0;
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
	wc->reada_count = BTRFS_NODEPTRS_PER_BLOCK(fs_info);

	while (1) {

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
		if (btrfs_should_end_transaction(trans) ||
		    (!for_reloc && btrfs_need_cleaner_sleep(fs_info))) {
			ret = btrfs_update_root(trans, tree_root,
						&root->root_key,
						root_item);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				err = ret;
				goto out_end_trans;
			}

			btrfs_end_transaction_throttle(trans);
			if (!for_reloc && btrfs_need_cleaner_sleep(fs_info)) {
				btrfs_debug(fs_info,
					    "drop snapshot early exit");
				err = -EAGAIN;
				goto out_free;
			}

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

	ret = btrfs_del_root(trans, fs_info, &root->root_key);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		err = ret;
		goto out_end_trans;
	}

	if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
		ret = btrfs_find_root(tree_root, &root->root_key, path,
				      NULL, NULL);
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
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

	if (test_bit(BTRFS_ROOT_IN_RADIX, &root->state)) {
		btrfs_add_dropped_root(trans, root);
	} else {
		free_extent_buffer(root->node);
		free_extent_buffer(root->commit_root);
		btrfs_put_fs_root(root);
	}
	root_dropped = true;
out_end_trans:
	btrfs_end_transaction_throttle(trans);
out_free:
	kfree(wc);
	btrfs_free_path(path);
out:
	/*
	 * So if we need to stop dropping the snapshot for whatever reason we
	 * need to make sure to add it back to the dead root list so that we
	 * keep trying to do the work later.  This also cleans up roots if we
	 * don't have it in the radix (like when we recover after a power fail
	 * or unmount) so we don't leak memory.
	 */
	if (!for_reloc && !root_dropped)
		btrfs_add_dead_root(root);
	if (err && err != -EAGAIN)
		btrfs_handle_fs_error(fs_info, err, NULL);
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
	struct btrfs_fs_info *fs_info = root->fs_info;
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
	wc->reada_count = BTRFS_NODEPTRS_PER_BLOCK(fs_info);

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

static u64 update_block_group_flags(struct btrfs_fs_info *fs_info, u64 flags)
{
	u64 num_devices;
	u64 stripped;

	/*
	 * if restripe for this chunk_type is on pick target profile and
	 * return, otherwise do the usual balance
	 */
	stripped = get_restripe_target(fs_info, flags);
	if (stripped)
		return extended_to_chunk(stripped);

	num_devices = fs_info->fs_devices->rw_devices;

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

static int inc_block_group_ro(struct btrfs_block_group_cache *cache, int force)
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
		min_allocable_bytes = SZ_1M;
	else
		min_allocable_bytes = 0;

	spin_lock(&sinfo->lock);
	spin_lock(&cache->lock);

	if (cache->ro) {
		cache->ro++;
		ret = 0;
		goto out;
	}

	num_bytes = cache->key.offset - cache->reserved - cache->pinned -
		    cache->bytes_super - btrfs_block_group_used(&cache->item);

	if (btrfs_space_info_used(sinfo, true) + num_bytes +
	    min_allocable_bytes <= sinfo->total_bytes) {
		sinfo->bytes_readonly += num_bytes;
		cache->ro++;
		list_add_tail(&cache->ro_list, &sinfo->ro_bgs);
		ret = 0;
	}
out:
	spin_unlock(&cache->lock);
	spin_unlock(&sinfo->lock);
	return ret;
}

int btrfs_inc_block_group_ro(struct btrfs_fs_info *fs_info,
			     struct btrfs_block_group_cache *cache)

{
	struct btrfs_trans_handle *trans;
	u64 alloc_flags;
	int ret;

again:
	trans = btrfs_join_transaction(fs_info->extent_root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	/*
	 * we're not allowed to set block groups readonly after the dirty
	 * block groups cache has started writing.  If it already started,
	 * back off and let this transaction commit
	 */
	mutex_lock(&fs_info->ro_block_group_mutex);
	if (test_bit(BTRFS_TRANS_DIRTY_BG_RUN, &trans->transaction->flags)) {
		u64 transid = trans->transid;

		mutex_unlock(&fs_info->ro_block_group_mutex);
		btrfs_end_transaction(trans);

		ret = btrfs_wait_for_commit(fs_info, transid);
		if (ret)
			return ret;
		goto again;
	}

	/*
	 * if we are changing raid levels, try to allocate a corresponding
	 * block group with the new raid level.
	 */
	alloc_flags = update_block_group_flags(fs_info, cache->flags);
	if (alloc_flags != cache->flags) {
		ret = do_chunk_alloc(trans, fs_info, alloc_flags,
				     CHUNK_ALLOC_FORCE);
		/*
		 * ENOSPC is allowed here, we may have enough space
		 * already allocated at the new raid level to
		 * carry on
		 */
		if (ret == -ENOSPC)
			ret = 0;
		if (ret < 0)
			goto out;
	}

	ret = inc_block_group_ro(cache, 0);
	if (!ret)
		goto out;
	alloc_flags = get_alloc_profile(fs_info, cache->space_info->flags);
	ret = do_chunk_alloc(trans, fs_info, alloc_flags,
			     CHUNK_ALLOC_FORCE);
	if (ret < 0)
		goto out;
	ret = inc_block_group_ro(cache, 0);
out:
	if (cache->flags & BTRFS_BLOCK_GROUP_SYSTEM) {
		alloc_flags = update_block_group_flags(fs_info, cache->flags);
		mutex_lock(&fs_info->chunk_mutex);
		check_system_chunk(trans, fs_info, alloc_flags);
		mutex_unlock(&fs_info->chunk_mutex);
	}
	mutex_unlock(&fs_info->ro_block_group_mutex);

	btrfs_end_transaction(trans);
	return ret;
}

int btrfs_force_chunk_alloc(struct btrfs_trans_handle *trans,
			    struct btrfs_fs_info *fs_info, u64 type)
{
	u64 alloc_flags = get_alloc_profile(fs_info, type);

	return do_chunk_alloc(trans, fs_info, alloc_flags, CHUNK_ALLOC_FORCE);
}

/*
 * helper to account the unused space of all the readonly block group in the
 * space_info. takes mirrors into account.
 */
u64 btrfs_account_ro_block_groups_free_space(struct btrfs_space_info *sinfo)
{
	struct btrfs_block_group_cache *block_group;
	u64 free_bytes = 0;
	int factor;

	/* It's df, we don't care if it's racy */
	if (list_empty(&sinfo->ro_bgs))
		return 0;

	spin_lock(&sinfo->lock);
	list_for_each_entry(block_group, &sinfo->ro_bgs, ro_list) {
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
	spin_unlock(&sinfo->lock);

	return free_bytes;
}

void btrfs_dec_block_group_ro(struct btrfs_block_group_cache *cache)
{
	struct btrfs_space_info *sinfo = cache->space_info;
	u64 num_bytes;

	BUG_ON(!cache->ro);

	spin_lock(&sinfo->lock);
	spin_lock(&cache->lock);
	if (!--cache->ro) {
		num_bytes = cache->key.offset - cache->reserved -
			    cache->pinned - cache->bytes_super -
			    btrfs_block_group_used(&cache->item);
		sinfo->bytes_readonly -= num_bytes;
		list_del_init(&cache->ro_list);
	}
	spin_unlock(&cache->lock);
	spin_unlock(&sinfo->lock);
}

/*
 * checks to see if its even possible to relocate this block group.
 *
 * @return - -1 if it's not a good idea to relocate this block group, 0 if its
 * ok to go ahead and try.
 */
int btrfs_can_relocate(struct btrfs_fs_info *fs_info, u64 bytenr)
{
	struct btrfs_root *root = fs_info->extent_root;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_space_info *space_info;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	struct btrfs_trans_handle *trans;
	u64 min_free;
	u64 dev_min = 1;
	u64 dev_nr = 0;
	u64 target;
	int debug;
	int index;
	int full = 0;
	int ret = 0;

	debug = btrfs_test_opt(fs_info, ENOSPC_DEBUG);

	block_group = btrfs_lookup_block_group(fs_info, bytenr);

	/* odd, couldn't find the block group, leave it alone */
	if (!block_group) {
		if (debug)
			btrfs_warn(fs_info,
				   "can't find block group for bytenr %llu",
				   bytenr);
		return -1;
	}

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
	    (btrfs_space_info_used(space_info, false) + min_free <
	     space_info->total_bytes)) {
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
	target = get_restripe_target(fs_info, block_group->flags);
	if (target) {
		index = btrfs_bg_flags_to_raid_index(extended_to_chunk(target));
	} else {
		/*
		 * this is just a balance, so if we were marked as full
		 * we know there is no space for a new chunk
		 */
		if (full) {
			if (debug)
				btrfs_warn(fs_info,
					   "no space to alloc new chunk for block group %llu",
					   block_group->key.objectid);
			goto out;
		}

		index = btrfs_bg_flags_to_raid_index(block_group->flags);
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
		min_free = div64_u64(min_free, dev_min);
	}

	/* We need to do this so that we can look at pending chunks */
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	mutex_lock(&fs_info->chunk_mutex);
	list_for_each_entry(device, &fs_devices->alloc_list, dev_alloc_list) {
		u64 dev_offset;

		/*
		 * check to make sure we can actually find a chunk with enough
		 * space to fit our block group in.
		 */
		if (device->total_bytes > device->bytes_used + min_free &&
		    !test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state)) {
			ret = find_free_dev_extent(trans, device, min_free,
						   &dev_offset, NULL);
			if (!ret)
				dev_nr++;

			if (dev_nr >= dev_min)
				break;

			ret = -1;
		}
	}
	if (debug && ret == -1)
		btrfs_warn(fs_info,
			   "no space to allocate a new chunk for block group %llu",
			   block_group->key.objectid);
	mutex_unlock(&fs_info->chunk_mutex);
	btrfs_end_transaction(trans);
out:
	btrfs_put_block_group(block_group);
	return ret;
}

static int find_first_block_group(struct btrfs_fs_info *fs_info,
				  struct btrfs_path *path,
				  struct btrfs_key *key)
{
	struct btrfs_root *root = fs_info->extent_root;
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
			struct extent_map_tree *em_tree;
			struct extent_map *em;

			em_tree = &root->fs_info->mapping_tree.map_tree;
			read_lock(&em_tree->lock);
			em = lookup_extent_mapping(em_tree, found_key.objectid,
						   found_key.offset);
			read_unlock(&em_tree->lock);
			if (!em) {
				btrfs_err(fs_info,
			"logical %llu len %llu found bg but no related chunk",
					  found_key.objectid, found_key.offset);
				ret = -ENOENT;
			} else {
				ret = 0;
			}
			free_extent_map(em);
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
			block_group = next_block_group(info, block_group);
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
		ASSERT(block_group->io_ctl.inode == NULL);
		iput(inode);
		last = block_group->key.objectid + block_group->key.offset;
		btrfs_put_block_group(block_group);
	}
}

/*
 * Must be called only after stopping all workers, since we could have block
 * group caching kthreads running, and therefore they could race with us if we
 * freed the block groups before stopping them.
 */
int btrfs_free_block_groups(struct btrfs_fs_info *info)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_space_info *space_info;
	struct btrfs_caching_control *caching_ctl;
	struct rb_node *n;

	down_write(&info->commit_root_sem);
	while (!list_empty(&info->caching_block_groups)) {
		caching_ctl = list_entry(info->caching_block_groups.next,
					 struct btrfs_caching_control, list);
		list_del(&caching_ctl->list);
		put_caching_control(caching_ctl);
	}
	up_write(&info->commit_root_sem);

	spin_lock(&info->unused_bgs_lock);
	while (!list_empty(&info->unused_bgs)) {
		block_group = list_first_entry(&info->unused_bgs,
					       struct btrfs_block_group_cache,
					       bg_list);
		list_del_init(&block_group->bg_list);
		btrfs_put_block_group(block_group);
	}
	spin_unlock(&info->unused_bgs_lock);

	spin_lock(&info->block_group_cache_lock);
	while ((n = rb_last(&info->block_group_cache_tree)) != NULL) {
		block_group = rb_entry(n, struct btrfs_block_group_cache,
				       cache_node);
		rb_erase(&block_group->cache_node,
			 &info->block_group_cache_tree);
		RB_CLEAR_NODE(&block_group->cache_node);
		spin_unlock(&info->block_group_cache_lock);

		down_write(&block_group->space_info->groups_sem);
		list_del(&block_group->list);
		up_write(&block_group->space_info->groups_sem);

		/*
		 * We haven't cached this block group, which means we could
		 * possibly have excluded extents on this block group.
		 */
		if (block_group->cached == BTRFS_CACHE_NO ||
		    block_group->cached == BTRFS_CACHE_ERROR)
			free_excluded_extents(info, block_group);

		btrfs_remove_free_space_cache(block_group);
		ASSERT(block_group->cached != BTRFS_CACHE_STARTED);
		ASSERT(list_empty(&block_group->dirty_list));
		ASSERT(list_empty(&block_group->io_list));
		ASSERT(list_empty(&block_group->bg_list));
		ASSERT(atomic_read(&block_group->count) == 1);
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

	while (!list_empty(&info->space_info)) {
		int i;

		space_info = list_entry(info->space_info.next,
					struct btrfs_space_info,
					list);

		/*
		 * Do not hide this behind enospc_debug, this is actually
		 * important and indicates a real bug if this happens.
		 */
		if (WARN_ON(space_info->bytes_pinned > 0 ||
			    space_info->bytes_reserved > 0 ||
			    space_info->bytes_may_use > 0))
			dump_space_info(info, space_info, 0, 0);
		list_del(&space_info->list);
		for (i = 0; i < BTRFS_NR_RAID_TYPES; i++) {
			struct kobject *kobj;
			kobj = space_info->block_group_kobjs[i];
			space_info->block_group_kobjs[i] = NULL;
			if (kobj) {
				kobject_del(kobj);
				kobject_put(kobj);
			}
		}
		kobject_del(&space_info->kobj);
		kobject_put(&space_info->kobj);
	}
	return 0;
}

/* link_block_group will queue up kobjects to add when we're reclaim-safe */
void btrfs_add_raid_kobjects(struct btrfs_fs_info *fs_info)
{
	struct btrfs_space_info *space_info;
	struct raid_kobject *rkobj;
	LIST_HEAD(list);
	int index;
	int ret = 0;

	spin_lock(&fs_info->pending_raid_kobjs_lock);
	list_splice_init(&fs_info->pending_raid_kobjs, &list);
	spin_unlock(&fs_info->pending_raid_kobjs_lock);

	list_for_each_entry(rkobj, &list, list) {
		space_info = __find_space_info(fs_info, rkobj->flags);
		index = btrfs_bg_flags_to_raid_index(rkobj->flags);

		ret = kobject_add(&rkobj->kobj, &space_info->kobj,
				  "%s", get_raid_name(index));
		if (ret) {
			kobject_put(&rkobj->kobj);
			break;
		}
	}
	if (ret)
		btrfs_warn(fs_info,
			   "failed to add kobject for block cache, ignoring");
}

static void link_block_group(struct btrfs_block_group_cache *cache)
{
	struct btrfs_space_info *space_info = cache->space_info;
	struct btrfs_fs_info *fs_info = cache->fs_info;
	int index = btrfs_bg_flags_to_raid_index(cache->flags);
	bool first = false;

	down_write(&space_info->groups_sem);
	if (list_empty(&space_info->block_groups[index]))
		first = true;
	list_add_tail(&cache->list, &space_info->block_groups[index]);
	up_write(&space_info->groups_sem);

	if (first) {
		struct raid_kobject *rkobj = kzalloc(sizeof(*rkobj), GFP_NOFS);
		if (!rkobj) {
			btrfs_warn(cache->fs_info,
				"couldn't alloc memory for raid level kobject");
			return;
		}
		rkobj->flags = cache->flags;
		kobject_init(&rkobj->kobj, &btrfs_raid_ktype);

		spin_lock(&fs_info->pending_raid_kobjs_lock);
		list_add_tail(&rkobj->list, &fs_info->pending_raid_kobjs);
		spin_unlock(&fs_info->pending_raid_kobjs_lock);
		space_info->block_group_kobjs[index] = &rkobj->kobj;
	}
}

static struct btrfs_block_group_cache *
btrfs_create_block_group_cache(struct btrfs_fs_info *fs_info,
			       u64 start, u64 size)
{
	struct btrfs_block_group_cache *cache;

	cache = kzalloc(sizeof(*cache), GFP_NOFS);
	if (!cache)
		return NULL;

	cache->free_space_ctl = kzalloc(sizeof(*cache->free_space_ctl),
					GFP_NOFS);
	if (!cache->free_space_ctl) {
		kfree(cache);
		return NULL;
	}

	cache->key.objectid = start;
	cache->key.offset = size;
	cache->key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;

	cache->fs_info = fs_info;
	cache->full_stripe_len = btrfs_full_stripe_len(fs_info, start);
	set_free_space_tree_thresholds(cache);

	atomic_set(&cache->count, 1);
	spin_lock_init(&cache->lock);
	init_rwsem(&cache->data_rwsem);
	INIT_LIST_HEAD(&cache->list);
	INIT_LIST_HEAD(&cache->cluster_list);
	INIT_LIST_HEAD(&cache->bg_list);
	INIT_LIST_HEAD(&cache->ro_list);
	INIT_LIST_HEAD(&cache->dirty_list);
	INIT_LIST_HEAD(&cache->io_list);
	btrfs_init_free_space_ctl(cache);
	atomic_set(&cache->trimming, 0);
	mutex_init(&cache->free_space_lock);
	btrfs_init_full_stripe_locks_tree(&cache->full_stripe_locks_root);

	return cache;
}

int btrfs_read_block_groups(struct btrfs_fs_info *info)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_block_group_cache *cache;
	struct btrfs_space_info *space_info;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *leaf;
	int need_clear = 0;
	u64 cache_gen;
	u64 feature;
	int mixed;

	feature = btrfs_super_incompat_flags(info->super_copy);
	mixed = !!(feature & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS);

	key.objectid = 0;
	key.offset = 0;
	key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_FORWARD;

	cache_gen = btrfs_super_cache_generation(info->super_copy);
	if (btrfs_test_opt(info, SPACE_CACHE) &&
	    btrfs_super_generation(info->super_copy) != cache_gen)
		need_clear = 1;
	if (btrfs_test_opt(info, CLEAR_CACHE))
		need_clear = 1;

	while (1) {
		ret = find_first_block_group(info, path, &key);
		if (ret > 0)
			break;
		if (ret != 0)
			goto error;

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		cache = btrfs_create_block_group_cache(info, found_key.objectid,
						       found_key.offset);
		if (!cache) {
			ret = -ENOMEM;
			goto error;
		}

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
			if (btrfs_test_opt(info, SPACE_CACHE))
				cache->disk_cache_state = BTRFS_DC_CLEAR;
		}

		read_extent_buffer(leaf, &cache->item,
				   btrfs_item_ptr_offset(leaf, path->slots[0]),
				   sizeof(cache->item));
		cache->flags = btrfs_block_group_flags(&cache->item);
		if (!mixed &&
		    ((cache->flags & BTRFS_BLOCK_GROUP_METADATA) &&
		    (cache->flags & BTRFS_BLOCK_GROUP_DATA))) {
			btrfs_err(info,
"bg %llu is a mixed block group but filesystem hasn't enabled mixed block groups",
				  cache->key.objectid);
			ret = -EINVAL;
			goto error;
		}

		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(path);

		/*
		 * We need to exclude the super stripes now so that the space
		 * info has super bytes accounted for, otherwise we'll think
		 * we have more space than we actually do.
		 */
		ret = exclude_super_stripes(info, cache);
		if (ret) {
			/*
			 * We may have excluded something, so call this just in
			 * case.
			 */
			free_excluded_extents(info, cache);
			btrfs_put_block_group(cache);
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
			free_excluded_extents(info, cache);
		} else if (btrfs_block_group_used(&cache->item) == 0) {
			cache->last_byte_to_unpin = (u64)-1;
			cache->cached = BTRFS_CACHE_FINISHED;
			add_new_free_space(cache, info,
					   found_key.objectid,
					   found_key.objectid +
					   found_key.offset);
			free_excluded_extents(info, cache);
		}

		ret = btrfs_add_block_group_cache(info, cache);
		if (ret) {
			btrfs_remove_free_space_cache(cache);
			btrfs_put_block_group(cache);
			goto error;
		}

		trace_btrfs_add_block_group(info, cache, 0);
		update_space_info(info, cache->flags, found_key.offset,
				  btrfs_block_group_used(&cache->item),
				  cache->bytes_super, &space_info);

		cache->space_info = space_info;

		link_block_group(cache);

		set_avail_alloc_bits(info, cache->flags);
		if (btrfs_chunk_readonly(info, cache->key.objectid)) {
			inc_block_group_ro(cache, 1);
		} else if (btrfs_block_group_used(&cache->item) == 0) {
			spin_lock(&info->unused_bgs_lock);
			/* Should always be true but just in case. */
			if (list_empty(&cache->bg_list)) {
				btrfs_get_block_group(cache);
				list_add_tail(&cache->bg_list,
					      &info->unused_bgs);
			}
			spin_unlock(&info->unused_bgs_lock);
		}
	}

	list_for_each_entry_rcu(space_info, &info->space_info, list) {
		if (!(get_alloc_profile(info, space_info->flags) &
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
		list_for_each_entry(cache,
				&space_info->block_groups[BTRFS_RAID_RAID0],
				list)
			inc_block_group_ro(cache, 1);
		list_for_each_entry(cache,
				&space_info->block_groups[BTRFS_RAID_SINGLE],
				list)
			inc_block_group_ro(cache, 1);
	}

	btrfs_add_raid_kobjects(info);
	init_global_block_rsv(info);
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

void btrfs_create_pending_block_groups(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_block_group_cache *block_group, *tmp;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_block_group_item item;
	struct btrfs_key key;
	int ret = 0;
	bool can_flush_pending_bgs = trans->can_flush_pending_bgs;

	trans->can_flush_pending_bgs = false;
	list_for_each_entry_safe(block_group, tmp, &trans->new_bgs, bg_list) {
		if (ret)
			goto next;

		spin_lock(&block_group->lock);
		memcpy(&item, &block_group->item, sizeof(item));
		memcpy(&key, &block_group->key, sizeof(key));
		spin_unlock(&block_group->lock);

		ret = btrfs_insert_item(trans, extent_root, &key, &item,
					sizeof(item));
		if (ret)
			btrfs_abort_transaction(trans, ret);
		ret = btrfs_finish_chunk_alloc(trans, fs_info, key.objectid,
					       key.offset);
		if (ret)
			btrfs_abort_transaction(trans, ret);
		add_block_group_free_space(trans, fs_info, block_group);
		/* already aborted the transaction if it failed. */
next:
		list_del_init(&block_group->bg_list);
	}
	trans->can_flush_pending_bgs = can_flush_pending_bgs;
}

int btrfs_make_block_group(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info, u64 bytes_used,
			   u64 type, u64 chunk_offset, u64 size)
{
	struct btrfs_block_group_cache *cache;
	int ret;

	btrfs_set_log_full_commit(fs_info, trans);

	cache = btrfs_create_block_group_cache(fs_info, chunk_offset, size);
	if (!cache)
		return -ENOMEM;

	btrfs_set_block_group_used(&cache->item, bytes_used);
	btrfs_set_block_group_chunk_objectid(&cache->item,
					     BTRFS_FIRST_CHUNK_TREE_OBJECTID);
	btrfs_set_block_group_flags(&cache->item, type);

	cache->flags = type;
	cache->last_byte_to_unpin = (u64)-1;
	cache->cached = BTRFS_CACHE_FINISHED;
	cache->needs_free_space = 1;
	ret = exclude_super_stripes(fs_info, cache);
	if (ret) {
		/*
		 * We may have excluded something, so call this just in
		 * case.
		 */
		free_excluded_extents(fs_info, cache);
		btrfs_put_block_group(cache);
		return ret;
	}

	add_new_free_space(cache, fs_info, chunk_offset, chunk_offset + size);

	free_excluded_extents(fs_info, cache);

#ifdef CONFIG_BTRFS_DEBUG
	if (btrfs_should_fragment_free_space(cache)) {
		u64 new_bytes_used = size - bytes_used;

		bytes_used += new_bytes_used >> 1;
		fragment_free_space(cache);
	}
#endif
	/*
	 * Ensure the corresponding space_info object is created and
	 * assigned to our block group. We want our bg to be added to the rbtree
	 * with its ->space_info set.
	 */
	cache->space_info = __find_space_info(fs_info, cache->flags);
	ASSERT(cache->space_info);

	ret = btrfs_add_block_group_cache(fs_info, cache);
	if (ret) {
		btrfs_remove_free_space_cache(cache);
		btrfs_put_block_group(cache);
		return ret;
	}

	/*
	 * Now that our block group has its ->space_info set and is inserted in
	 * the rbtree, update the space info's counters.
	 */
	trace_btrfs_add_block_group(fs_info, cache, 1);
	update_space_info(fs_info, cache->flags, size, bytes_used,
				cache->bytes_super, &cache->space_info);
	update_global_block_rsv(fs_info);

	link_block_group(cache);

	list_add_tail(&cache->bg_list, &trans->new_bgs);

	set_avail_alloc_bits(fs_info, type);
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
			     struct btrfs_fs_info *fs_info, u64 group_start,
			     struct extent_map *em)
{
	struct btrfs_root *root = fs_info->extent_root;
	struct btrfs_path *path;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_free_cluster *cluster;
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct btrfs_key key;
	struct inode *inode;
	struct kobject *kobj = NULL;
	int ret;
	int index;
	int factor;
	struct btrfs_caching_control *caching_ctl = NULL;
	bool remove_em;

	block_group = btrfs_lookup_block_group(fs_info, group_start);
	BUG_ON(!block_group);
	BUG_ON(!block_group->ro);

	/*
	 * Free the reserved super bytes from this block group before
	 * remove it.
	 */
	free_excluded_extents(fs_info, block_group);
	btrfs_free_ref_tree_range(fs_info, block_group->key.objectid,
				  block_group->key.offset);

	memcpy(&key, &block_group->key, sizeof(key));
	index = btrfs_bg_flags_to_raid_index(block_group->flags);
	if (block_group->flags & (BTRFS_BLOCK_GROUP_DUP |
				  BTRFS_BLOCK_GROUP_RAID1 |
				  BTRFS_BLOCK_GROUP_RAID10))
		factor = 2;
	else
		factor = 1;

	/* make sure this block group isn't part of an allocation cluster */
	cluster = &fs_info->data_alloc_cluster;
	spin_lock(&cluster->refill_lock);
	btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&cluster->refill_lock);

	/*
	 * make sure this block group isn't part of a metadata
	 * allocation cluster
	 */
	cluster = &fs_info->meta_alloc_cluster;
	spin_lock(&cluster->refill_lock);
	btrfs_return_cluster_to_free_space(block_group, cluster);
	spin_unlock(&cluster->refill_lock);

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * get the inode first so any iput calls done for the io_list
	 * aren't the final iput (no unlinks allowed now)
	 */
	inode = lookup_free_space_inode(fs_info, block_group, path);

	mutex_lock(&trans->transaction->cache_write_mutex);
	/*
	 * make sure our free spache cache IO is done before remove the
	 * free space inode
	 */
	spin_lock(&trans->transaction->dirty_bgs_lock);
	if (!list_empty(&block_group->io_list)) {
		list_del_init(&block_group->io_list);

		WARN_ON(!IS_ERR(inode) && inode != block_group->io_ctl.inode);

		spin_unlock(&trans->transaction->dirty_bgs_lock);
		btrfs_wait_cache_io(trans, block_group, path);
		btrfs_put_block_group(block_group);
		spin_lock(&trans->transaction->dirty_bgs_lock);
	}

	if (!list_empty(&block_group->dirty_list)) {
		list_del_init(&block_group->dirty_list);
		btrfs_put_block_group(block_group);
	}
	spin_unlock(&trans->transaction->dirty_bgs_lock);
	mutex_unlock(&trans->transaction->cache_write_mutex);

	if (!IS_ERR(inode)) {
		ret = btrfs_orphan_add(trans, BTRFS_I(inode));
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

	spin_lock(&fs_info->block_group_cache_lock);
	rb_erase(&block_group->cache_node,
		 &fs_info->block_group_cache_tree);
	RB_CLEAR_NODE(&block_group->cache_node);

	if (fs_info->first_logical_byte == block_group->key.objectid)
		fs_info->first_logical_byte = (u64)-1;
	spin_unlock(&fs_info->block_group_cache_lock);

	down_write(&block_group->space_info->groups_sem);
	/*
	 * we must use list_del_init so people can check to see if they
	 * are still on the list after taking the semaphore
	 */
	list_del_init(&block_group->list);
	if (list_empty(&block_group->space_info->block_groups[index])) {
		kobj = block_group->space_info->block_group_kobjs[index];
		block_group->space_info->block_group_kobjs[index] = NULL;
		clear_avail_alloc_bits(fs_info, block_group->flags);
	}
	up_write(&block_group->space_info->groups_sem);
	if (kobj) {
		kobject_del(kobj);
		kobject_put(kobj);
	}

	if (block_group->has_caching_ctl)
		caching_ctl = get_caching_control(block_group);
	if (block_group->cached == BTRFS_CACHE_STARTED)
		wait_block_group_cache_done(block_group);
	if (block_group->has_caching_ctl) {
		down_write(&fs_info->commit_root_sem);
		if (!caching_ctl) {
			struct btrfs_caching_control *ctl;

			list_for_each_entry(ctl,
				    &fs_info->caching_block_groups, list)
				if (ctl->block_group == block_group) {
					caching_ctl = ctl;
					refcount_inc(&caching_ctl->count);
					break;
				}
		}
		if (caching_ctl)
			list_del_init(&caching_ctl->list);
		up_write(&fs_info->commit_root_sem);
		if (caching_ctl) {
			/* Once for the caching bgs list and once for us. */
			put_caching_control(caching_ctl);
			put_caching_control(caching_ctl);
		}
	}

	spin_lock(&trans->transaction->dirty_bgs_lock);
	if (!list_empty(&block_group->dirty_list)) {
		WARN_ON(1);
	}
	if (!list_empty(&block_group->io_list)) {
		WARN_ON(1);
	}
	spin_unlock(&trans->transaction->dirty_bgs_lock);
	btrfs_remove_free_space_cache(block_group);

	spin_lock(&block_group->space_info->lock);
	list_del_init(&block_group->ro_list);

	if (btrfs_test_opt(fs_info, ENOSPC_DEBUG)) {
		WARN_ON(block_group->space_info->total_bytes
			< block_group->key.offset);
		WARN_ON(block_group->space_info->bytes_readonly
			< block_group->key.offset);
		WARN_ON(block_group->space_info->disk_total
			< block_group->key.offset * factor);
	}
	block_group->space_info->total_bytes -= block_group->key.offset;
	block_group->space_info->bytes_readonly -= block_group->key.offset;
	block_group->space_info->disk_total -= block_group->key.offset * factor;

	spin_unlock(&block_group->space_info->lock);

	memcpy(&key, &block_group->key, sizeof(key));

	mutex_lock(&fs_info->chunk_mutex);
	if (!list_empty(&em->list)) {
		/* We're in the transaction->pending_chunks list. */
		free_extent_map(em);
	}
	spin_lock(&block_group->lock);
	block_group->removed = 1;
	/*
	 * At this point trimming can't start on this block group, because we
	 * removed the block group from the tree fs_info->block_group_cache_tree
	 * so no one can't find it anymore and even if someone already got this
	 * block group before we removed it from the rbtree, they have already
	 * incremented block_group->trimming - if they didn't, they won't find
	 * any free space entries because we already removed them all when we
	 * called btrfs_remove_free_space_cache().
	 *
	 * And we must not remove the extent map from the fs_info->mapping_tree
	 * to prevent the same logical address range and physical device space
	 * ranges from being reused for a new block group. This is because our
	 * fs trim operation (btrfs_trim_fs() / btrfs_ioctl_fitrim()) is
	 * completely transactionless, so while it is trimming a range the
	 * currently running transaction might finish and a new one start,
	 * allowing for new block groups to be created that can reuse the same
	 * physical device locations unless we take this special care.
	 *
	 * There may also be an implicit trim operation if the file system
	 * is mounted with -odiscard. The same protections must remain
	 * in place until the extents have been discarded completely when
	 * the transaction commit has completed.
	 */
	remove_em = (atomic_read(&block_group->trimming) == 0);
	/*
	 * Make sure a trimmer task always sees the em in the pinned_chunks list
	 * if it sees block_group->removed == 1 (needs to lock block_group->lock
	 * before checking block_group->removed).
	 */
	if (!remove_em) {
		/*
		 * Our em might be in trans->transaction->pending_chunks which
		 * is protected by fs_info->chunk_mutex ([lock|unlock]_chunks),
		 * and so is the fs_info->pinned_chunks list.
		 *
		 * So at this point we must be holding the chunk_mutex to avoid
		 * any races with chunk allocation (more specifically at
		 * volumes.c:contains_pending_extent()), to ensure it always
		 * sees the em, either in the pending_chunks list or in the
		 * pinned_chunks list.
		 */
		list_move_tail(&em->list, &fs_info->pinned_chunks);
	}
	spin_unlock(&block_group->lock);

	if (remove_em) {
		struct extent_map_tree *em_tree;

		em_tree = &fs_info->mapping_tree.map_tree;
		write_lock(&em_tree->lock);
		/*
		 * The em might be in the pending_chunks list, so make sure the
		 * chunk mutex is locked, since remove_extent_mapping() will
		 * delete us from that list.
		 */
		remove_extent_mapping(em_tree, em);
		write_unlock(&em_tree->lock);
		/* once for the tree */
		free_extent_map(em);
	}

	mutex_unlock(&fs_info->chunk_mutex);

	ret = remove_block_group_free_space(trans, fs_info, block_group);
	if (ret)
		goto out;

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

struct btrfs_trans_handle *
btrfs_start_trans_remove_block_group(struct btrfs_fs_info *fs_info,
				     const u64 chunk_offset)
{
	struct extent_map_tree *em_tree = &fs_info->mapping_tree.map_tree;
	struct extent_map *em;
	struct map_lookup *map;
	unsigned int num_items;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, chunk_offset, 1);
	read_unlock(&em_tree->lock);
	ASSERT(em && em->start == chunk_offset);

	/*
	 * We need to reserve 3 + N units from the metadata space info in order
	 * to remove a block group (done at btrfs_remove_chunk() and at
	 * btrfs_remove_block_group()), which are used for:
	 *
	 * 1 unit for adding the free space inode's orphan (located in the tree
	 * of tree roots).
	 * 1 unit for deleting the block group item (located in the extent
	 * tree).
	 * 1 unit for deleting the free space item (located in tree of tree
	 * roots).
	 * N units for deleting N device extent items corresponding to each
	 * stripe (located in the device tree).
	 *
	 * In order to remove a block group we also need to reserve units in the
	 * system space info in order to update the chunk tree (update one or
	 * more device items and remove one chunk item), but this is done at
	 * btrfs_remove_chunk() through a call to check_system_chunk().
	 */
	map = em->map_lookup;
	num_items = 3 + map->num_stripes;
	free_extent_map(em);

	return btrfs_start_transaction_fallback_global_rsv(fs_info->extent_root,
							   num_items, 1);
}

/*
 * Process the unused_bgs list and remove any that don't have any allocated
 * space inside of them.
 */
void btrfs_delete_unused_bgs(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_space_info *space_info;
	struct btrfs_trans_handle *trans;
	int ret = 0;

	if (!test_bit(BTRFS_FS_OPEN, &fs_info->flags))
		return;

	spin_lock(&fs_info->unused_bgs_lock);
	while (!list_empty(&fs_info->unused_bgs)) {
		u64 start, end;
		int trimming;

		block_group = list_first_entry(&fs_info->unused_bgs,
					       struct btrfs_block_group_cache,
					       bg_list);
		list_del_init(&block_group->bg_list);

		space_info = block_group->space_info;

		if (ret || btrfs_mixed_space_info(space_info)) {
			btrfs_put_block_group(block_group);
			continue;
		}
		spin_unlock(&fs_info->unused_bgs_lock);

		mutex_lock(&fs_info->delete_unused_bgs_mutex);

		/* Don't want to race with allocators so take the groups_sem */
		down_write(&space_info->groups_sem);
		spin_lock(&block_group->lock);
		if (block_group->reserved ||
		    btrfs_block_group_used(&block_group->item) ||
		    block_group->ro ||
		    list_is_singular(&block_group->list)) {
			/*
			 * We want to bail if we made new allocations or have
			 * outstanding allocations in this block group.  We do
			 * the ro check in case balance is currently acting on
			 * this block group.
			 */
			spin_unlock(&block_group->lock);
			up_write(&space_info->groups_sem);
			goto next;
		}
		spin_unlock(&block_group->lock);

		/* We don't want to force the issue, only flip if it's ok. */
		ret = inc_block_group_ro(block_group, 0);
		up_write(&space_info->groups_sem);
		if (ret < 0) {
			ret = 0;
			goto next;
		}

		/*
		 * Want to do this before we do anything else so we can recover
		 * properly if we fail to join the transaction.
		 */
		trans = btrfs_start_trans_remove_block_group(fs_info,
						     block_group->key.objectid);
		if (IS_ERR(trans)) {
			btrfs_dec_block_group_ro(block_group);
			ret = PTR_ERR(trans);
			goto next;
		}

		/*
		 * We could have pending pinned extents for this block group,
		 * just delete them, we don't care about them anymore.
		 */
		start = block_group->key.objectid;
		end = start + block_group->key.offset - 1;
		/*
		 * Hold the unused_bg_unpin_mutex lock to avoid racing with
		 * btrfs_finish_extent_commit(). If we are at transaction N,
		 * another task might be running finish_extent_commit() for the
		 * previous transaction N - 1, and have seen a range belonging
		 * to the block group in freed_extents[] before we were able to
		 * clear the whole block group range from freed_extents[]. This
		 * means that task can lookup for the block group after we
		 * unpinned it from freed_extents[] and removed it, leading to
		 * a BUG_ON() at btrfs_unpin_extent_range().
		 */
		mutex_lock(&fs_info->unused_bg_unpin_mutex);
		ret = clear_extent_bits(&fs_info->freed_extents[0], start, end,
				  EXTENT_DIRTY);
		if (ret) {
			mutex_unlock(&fs_info->unused_bg_unpin_mutex);
			btrfs_dec_block_group_ro(block_group);
			goto end_trans;
		}
		ret = clear_extent_bits(&fs_info->freed_extents[1], start, end,
				  EXTENT_DIRTY);
		if (ret) {
			mutex_unlock(&fs_info->unused_bg_unpin_mutex);
			btrfs_dec_block_group_ro(block_group);
			goto end_trans;
		}
		mutex_unlock(&fs_info->unused_bg_unpin_mutex);

		/* Reset pinned so btrfs_put_block_group doesn't complain */
		spin_lock(&space_info->lock);
		spin_lock(&block_group->lock);

		space_info->bytes_pinned -= block_group->pinned;
		space_info->bytes_readonly += block_group->pinned;
		percpu_counter_add(&space_info->total_bytes_pinned,
				   -block_group->pinned);
		block_group->pinned = 0;

		spin_unlock(&block_group->lock);
		spin_unlock(&space_info->lock);

		/* DISCARD can flip during remount */
		trimming = btrfs_test_opt(fs_info, DISCARD);

		/* Implicit trim during transaction commit. */
		if (trimming)
			btrfs_get_block_group_trimming(block_group);

		/*
		 * Btrfs_remove_chunk will abort the transaction if things go
		 * horribly wrong.
		 */
		ret = btrfs_remove_chunk(trans, fs_info,
					 block_group->key.objectid);

		if (ret) {
			if (trimming)
				btrfs_put_block_group_trimming(block_group);
			goto end_trans;
		}

		/*
		 * If we're not mounted with -odiscard, we can just forget
		 * about this block group. Otherwise we'll need to wait
		 * until transaction commit to do the actual discard.
		 */
		if (trimming) {
			spin_lock(&fs_info->unused_bgs_lock);
			/*
			 * A concurrent scrub might have added us to the list
			 * fs_info->unused_bgs, so use a list_move operation
			 * to add the block group to the deleted_bgs list.
			 */
			list_move(&block_group->bg_list,
				  &trans->transaction->deleted_bgs);
			spin_unlock(&fs_info->unused_bgs_lock);
			btrfs_get_block_group(block_group);
		}
end_trans:
		btrfs_end_transaction(trans);
next:
		mutex_unlock(&fs_info->delete_unused_bgs_mutex);
		btrfs_put_block_group(block_group);
		spin_lock(&fs_info->unused_bgs_lock);
	}
	spin_unlock(&fs_info->unused_bgs_lock);
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
		return -EINVAL;

	features = btrfs_super_incompat_flags(disk_super);
	if (features & BTRFS_FEATURE_INCOMPAT_MIXED_GROUPS)
		mixed = 1;

	flags = BTRFS_BLOCK_GROUP_SYSTEM;
	ret = create_space_info(fs_info, flags, &space_info);
	if (ret)
		goto out;

	if (mixed) {
		flags = BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA;
		ret = create_space_info(fs_info, flags, &space_info);
	} else {
		flags = BTRFS_BLOCK_GROUP_METADATA;
		ret = create_space_info(fs_info, flags, &space_info);
		if (ret)
			goto out;

		flags = BTRFS_BLOCK_GROUP_DATA;
		ret = create_space_info(fs_info, flags, &space_info);
	}
out:
	return ret;
}

int btrfs_error_unpin_extent_range(struct btrfs_fs_info *fs_info,
				   u64 start, u64 end)
{
	return unpin_extent_range(fs_info, start, end, false);
}

/*
 * It used to be that old block groups would be left around forever.
 * Iterating over them would be enough to trim unused space.  Since we
 * now automatically remove them, we also need to iterate over unallocated
 * space.
 *
 * We don't want a transaction for this since the discard may take a
 * substantial amount of time.  We don't require that a transaction be
 * running, but we do need to take a running transaction into account
 * to ensure that we're not discarding chunks that were released in
 * the current transaction.
 *
 * Holding the chunks lock will prevent other threads from allocating
 * or releasing chunks, but it won't prevent a running transaction
 * from committing and releasing the memory that the pending chunks
 * list head uses.  For that, we need to take a reference to the
 * transaction.
 */
static int btrfs_trim_free_extents(struct btrfs_device *device,
				   u64 minlen, u64 *trimmed)
{
	u64 start = 0, len = 0;
	int ret;

	*trimmed = 0;

	/* Not writeable = nothing to do. */
	if (!test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state))
		return 0;

	/* No free space = nothing to do. */
	if (device->total_bytes <= device->bytes_used)
		return 0;

	ret = 0;

	while (1) {
		struct btrfs_fs_info *fs_info = device->fs_info;
		struct btrfs_transaction *trans;
		u64 bytes;

		ret = mutex_lock_interruptible(&fs_info->chunk_mutex);
		if (ret)
			return ret;

		down_read(&fs_info->commit_root_sem);

		spin_lock(&fs_info->trans_lock);
		trans = fs_info->running_transaction;
		if (trans)
			refcount_inc(&trans->use_count);
		spin_unlock(&fs_info->trans_lock);

		ret = find_free_dev_extent_start(trans, device, minlen, start,
						 &start, &len);
		if (trans)
			btrfs_put_transaction(trans);

		if (ret) {
			up_read(&fs_info->commit_root_sem);
			mutex_unlock(&fs_info->chunk_mutex);
			if (ret == -ENOSPC)
				ret = 0;
			break;
		}

		ret = btrfs_issue_discard(device->bdev, start, len, &bytes);
		up_read(&fs_info->commit_root_sem);
		mutex_unlock(&fs_info->chunk_mutex);

		if (ret)
			break;

		start += len;
		*trimmed += bytes;

		if (fatal_signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		cond_resched();
	}

	return ret;
}

int btrfs_trim_fs(struct btrfs_fs_info *fs_info, struct fstrim_range *range)
{
	struct btrfs_block_group_cache *cache = NULL;
	struct btrfs_device *device;
	struct list_head *devices;
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
				if (ret) {
					btrfs_put_block_group(cache);
					break;
				}
				ret = wait_block_group_cache_done(cache);
				if (ret) {
					btrfs_put_block_group(cache);
					break;
				}
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

		cache = next_block_group(fs_info, cache);
	}

	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	devices = &fs_info->fs_devices->alloc_list;
	list_for_each_entry(device, devices, dev_alloc_list) {
		ret = btrfs_trim_free_extents(device, range->minlen,
					      &group_trimmed);
		if (ret)
			break;

		trimmed += group_trimmed;
	}
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);

	range->len = trimmed;
	return ret;
}

/*
 * btrfs_{start,end}_write_no_snapshotting() are similar to
 * mnt_{want,drop}_write(), they are used to prevent some tasks from writing
 * data into the page cache through nocow before the subvolume is snapshoted,
 * but flush the data into disk after the snapshot creation, or to prevent
 * operations while snapshotting is ongoing and that cause the snapshot to be
 * inconsistent (writes followed by expanding truncates for example).
 */
void btrfs_end_write_no_snapshotting(struct btrfs_root *root)
{
	percpu_counter_dec(&root->subv_writers->counter);
	/*
	 * Make sure counter is updated before we wake up waiters.
	 */
	smp_mb();
	if (waitqueue_active(&root->subv_writers->wait))
		wake_up(&root->subv_writers->wait);
}

int btrfs_start_write_no_snapshotting(struct btrfs_root *root)
{
	if (atomic_read(&root->will_be_snapshotted))
		return 0;

	percpu_counter_inc(&root->subv_writers->counter);
	/*
	 * Make sure counter is updated before we check for snapshot creation.
	 */
	smp_mb();
	if (atomic_read(&root->will_be_snapshotted)) {
		btrfs_end_write_no_snapshotting(root);
		return 0;
	}
	return 1;
}

void btrfs_wait_for_snapshot_creation(struct btrfs_root *root)
{
	while (true) {
		int ret;

		ret = btrfs_start_write_no_snapshotting(root);
		if (ret)
			break;
		wait_on_atomic_t(&root->will_be_snapshotted, atomic_t_wait,
				 TASK_UNINTERRUPTIBLE);
	}
}
