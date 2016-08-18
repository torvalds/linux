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
#include <linux/percpu_counter.h>
#include "hash.h"
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

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root, u64 bytenr,
			      u64 num_bytes, int alloc);
static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_delayed_ref_node *node, u64 parent,
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
static int btrfs_add_reserved_bytes(struct btrfs_block_group_cache *cache,
				    u64 ram_bytes, u64 num_bytes, int delalloc);
static int btrfs_free_reserved_bytes(struct btrfs_block_group_cache *cache,
				     u64 num_bytes, int delalloc);
static int block_rsv_use_bytes(struct btrfs_block_rsv *block_rsv,
			       u64 num_bytes);
int btrfs_pin_extent(struct btrfs_root *root,
		     u64 bytenr, u64 num_bytes, int reserved);
static int __reserve_metadata_bytes(struct btrfs_root *root,
				    struct btrfs_space_info *space_info,
				    u64 orig_bytes,
				    enum btrfs_reserve_flush_enum flush);
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
			start, end, EXTENT_UPTODATE);
	set_extent_bits(&root->fs_info->freed_extents[1],
			start, end, EXTENT_UPTODATE);
	return 0;
}

static void free_excluded_extents(struct btrfs_root *root,
				  struct btrfs_block_group_cache *cache)
{
	u64 start, end;

	start = cache->key.objectid;
	end = start + cache->key.offset - 1;

	clear_extent_bits(&root->fs_info->freed_extents[0],
			  start, end, EXTENT_UPTODATE);
	clear_extent_bits(&root->fs_info->freed_extents[1],
			  start, end, EXTENT_UPTODATE);
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
			ret = add_excluded_extent(root, start, len);
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
	atomic_inc(&ctl->count);
	spin_unlock(&cache->lock);
	return ctl;
}

static void put_caching_control(struct btrfs_caching_control *ctl)
{
	if (atomic_dec_and_test(&ctl->count))
		kfree(ctl);
}

#ifdef CONFIG_BTRFS_DEBUG
static void fragment_free_space(struct btrfs_root *root,
				struct btrfs_block_group_cache *block_group)
{
	u64 start = block_group->key.objectid;
	u64 len = block_group->key.offset;
	u64 chunk = block_group->flags & BTRFS_BLOCK_GROUP_METADATA ?
		root->nodesize : root->sectorsize;
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
	struct btrfs_block_group_cache *block_group;
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *extent_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 total_found = 0;
	u64 last = 0;
	u32 nritems;
	int ret;
	bool wakeup = true;

	block_group = caching_ctl->block_group;
	fs_info = block_group->fs_info;
	extent_root = fs_info->extent_root;

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
	if (btrfs_should_fragment_free_space(extent_root, block_group))
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
					fs_info->tree_root->nodesize;
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
	struct btrfs_root *extent_root;
	int ret;

	caching_ctl = container_of(work, struct btrfs_caching_control, work);
	block_group = caching_ctl->block_group;
	fs_info = block_group->fs_info;
	extent_root = fs_info->extent_root;

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
	if (btrfs_should_fragment_free_space(extent_root, block_group)) {
		u64 bytes_used;

		spin_lock(&block_group->space_info->lock);
		spin_lock(&block_group->lock);
		bytes_used = block_group->key.offset -
			btrfs_block_group_used(&block_group->item);
		block_group->space_info->bytes_used += bytes_used >> 1;
		spin_unlock(&block_group->lock);
		spin_unlock(&block_group->space_info->lock);
		fragment_free_space(extent_root, block_group);
	}
#endif

	caching_ctl->progress = (u64)-1;

	up_read(&fs_info->commit_root_sem);
	free_excluded_extents(fs_info->extent_root, block_group);
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
	atomic_set(&caching_ctl->count, 1);
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
		    btrfs_should_fragment_free_space(fs_info->extent_root,
						     cache)) {
			u64 bytes_used;

			spin_lock(&cache->space_info->lock);
			spin_lock(&cache->lock);
			bytes_used = cache->key.offset -
				btrfs_block_group_used(&cache->item);
			cache->space_info->bytes_used += bytes_used >> 1;
			spin_unlock(&cache->lock);
			spin_unlock(&cache->space_info->lock);
			fragment_free_space(fs_info->extent_root, cache);
		}
#endif
		mutex_unlock(&caching_ctl->mutex);

		wake_up(&caching_ctl->wait);
		if (ret == 1) {
			put_caching_control(caching_ctl);
			free_excluded_extents(fs_info->extent_root, cache);
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
	atomic_inc(&caching_ctl->count);
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

/* simple helper to search for an existing data extent at a given offset */
int btrfs_lookup_data_extent(struct btrfs_root *root, u64 start, u64 len)
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
		offset = root->nodesize;
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

	ret = btrfs_search_slot(trans, root->fs_info->extent_root,
				&key, path, 0, 0);
	if (ret < 0)
		goto out_free;

	if (ret > 0 && metadata && key.type == BTRFS_METADATA_ITEM_KEY) {
		if (path->slots[0]) {
			path->slots[0]--;
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == bytenr &&
			    key.type == BTRFS_EXTENT_ITEM_KEY &&
			    key.offset == root->nodesize)
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
			goto search_again;
		}
		spin_lock(&head->lock);
		if (head->extent_op && head->extent_op->update_flags)
			extent_flags |= head->extent_op->flags_to_set;
		else
			BUG_ON(num_refs == 0);

		num_refs += head->node.ref_mod;
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

	btrfs_extend_item(root, path, new_size);

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
	high_crc = btrfs_crc32c(high_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(owner);
	low_crc = btrfs_crc32c(low_crc, &lenum, sizeof(lenum));
	lenum = cpu_to_le64(offset);
	low_crc = btrfs_crc32c(low_crc, &lenum, sizeof(lenum));

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
		ret = btrfs_del_item(trans, root, path);
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
void setup_inline_extent_backref(struct btrfs_root *root,
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

	btrfs_extend_item(root, path, size);

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
void update_inline_extent_backref(struct btrfs_root *root,
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
		*last_ref = 1;
		size =  btrfs_extent_inline_ref_size(type);
		item_size = btrfs_item_size_nr(leaf, path->slots[0]);
		ptr = (unsigned long)iref;
		end = (unsigned long)ei + item_size;
		if (ptr + size < end)
			memmove_extent_buffer(leaf, ptr, ptr + size,
					      end - ptr - size);
		item_size -= size;
		btrfs_truncate_item(root, path, item_size, 1);
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
		update_inline_extent_backref(root, path, iref,
					     refs_to_add, extent_op, NULL);
	} else if (ret == -ENOENT) {
		setup_inline_extent_backref(root, path, iref, parent,
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
				 int refs_to_drop, int is_data, int *last_ref)
{
	int ret = 0;

	BUG_ON(!is_data && refs_to_drop != 1);
	if (iref) {
		update_inline_extent_backref(root, path, iref,
					     -refs_to_drop, NULL, last_ref);
	} else if (is_data) {
		ret = remove_extent_data_ref(trans, root, path, refs_to_drop,
					     last_ref);
	} else {
		*last_ref = 1;
		ret = btrfs_del_item(trans, root, path);
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

int btrfs_discard_extent(struct btrfs_root *root, u64 bytenr,
			 u64 num_bytes, u64 *actual_bytes)
{
	int ret;
	u64 discarded_bytes = 0;
	struct btrfs_bio *bbio = NULL;


	/*
	 * Avoid races with device replace and make sure our bbio has devices
	 * associated to its stripes that don't go away while we are discarding.
	 */
	btrfs_bio_counter_inc_blocked(root->fs_info);
	/* Tell the block device(s) that the sectors can be discarded */
	ret = btrfs_map_block(root->fs_info, REQ_DISCARD,
			      bytenr, &num_bytes, &bbio, 0);
	/* Error condition is -ENOMEM */
	if (!ret) {
		struct btrfs_bio_stripe *stripe = bbio->stripes;
		int i;


		for (i = 0; i < bbio->num_stripes; i++, stripe++) {
			u64 bytes;
			if (!stripe->dev->can_discard)
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
	btrfs_bio_counter_dec(root->fs_info);

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
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;

	BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID &&
	       root_objectid == BTRFS_TREE_LOG_OBJECTID);

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(fs_info, trans, bytenr,
					num_bytes,
					parent, root_objectid, (int)owner,
					BTRFS_ADD_DELAYED_REF, NULL);
	} else {
		ret = btrfs_add_delayed_data_ref(fs_info, trans, bytenr,
					num_bytes, parent, root_objectid,
					owner, offset, 0,
					BTRFS_ADD_DELAYED_REF, NULL);
	}
	return ret;
}

static int __btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_delayed_ref_node *node,
				  u64 parent, u64 root_objectid,
				  u64 owner, u64 offset, int refs_to_add,
				  struct btrfs_delayed_extent_op *extent_op)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
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
	ret = insert_inline_extent_backref(trans, fs_info->extent_root, path,
					   bytenr, num_bytes, parent,
					   root_objectid, owner, offset,
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
	ret = insert_extent_backref(trans, root->fs_info->extent_root,
				    path, bytenr, parent, root_objectid,
				    owner, offset, refs_to_add);
	if (ret)
		btrfs_abort_transaction(trans, ret);
out:
	btrfs_free_path(path);
	return ret;
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
	trace_run_delayed_data_ref(root->fs_info, node, ref, node->action);

	if (node->type == BTRFS_SHARED_DATA_REF_KEY)
		parent = ref->parent;
	ref_root = ref->root;

	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		if (extent_op)
			flags |= extent_op->flags_to_set;
		ret = alloc_reserved_file_extent(trans, root,
						 parent, ref_root, flags,
						 ref->objectid, ref->offset,
						 &ins, node->ref_mod);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, root, node, parent,
					     ref_root, ref->objectid,
					     ref->offset, node->ref_mod,
					     extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, root, node, parent,
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
	int metadata = !extent_op->is_data;

	if (trans->aborted)
		return 0;

	if (metadata && !btrfs_fs_incompat(root->fs_info, SKINNY_METADATA))
		metadata = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = node->bytenr;

	if (metadata) {
		key.type = BTRFS_METADATA_ITEM_KEY;
		key.offset = extent_op->level;
	} else {
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = node->num_bytes;
	}

again:
	path->reada = READA_FORWARD;
	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key,
				path, 0, 1);
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
				if (key.objectid == node->bytenr &&
				    key.type == BTRFS_EXTENT_ITEM_KEY &&
				    key.offset == node->num_bytes)
					ret = 0;
			}
			if (ret > 0) {
				btrfs_release_path(path);
				metadata = 0;

				key.objectid = node->bytenr;
				key.offset = node->num_bytes;
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
	trace_run_delayed_tree_ref(root->fs_info, node, ref, node->action);

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

	BUG_ON(node->ref_mod != 1);
	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		BUG_ON(!extent_op || !extent_op->update_flags);
		ret = alloc_reserved_tree_block(trans, root,
						parent, ref_root,
						extent_op->flags_to_set,
						&extent_op->key,
						ref->level, &ins);
	} else if (node->action == BTRFS_ADD_DELAYED_REF) {
		ret = __btrfs_inc_extent_ref(trans, root, node,
					     parent, ref_root,
					     ref->level, 0, 1,
					     extent_op);
	} else if (node->action == BTRFS_DROP_DELAYED_REF) {
		ret = __btrfs_free_extent(trans, root, node,
					  parent, ref_root,
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

	if (trans->aborted) {
		if (insert_reserved)
			btrfs_pin_extent(root, node->bytenr,
					 node->num_bytes, 1);
		return 0;
	}

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
		trace_run_delayed_ref_head(root->fs_info, node, head,
					   node->action);

		if (insert_reserved) {
			btrfs_pin_extent(root, node->bytenr,
					 node->num_bytes, 1);
			if (head->is_data) {
				ret = btrfs_del_csums(trans, root,
						      node->bytenr,
						      node->num_bytes);
			}
		}

		/* Also free its reserved qgroup space */
		btrfs_qgroup_free_delayed_ref(root->fs_info,
					      head->qgroup_ref_root,
					      head->qgroup_reserved);
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

static inline struct btrfs_delayed_ref_node *
select_delayed_ref(struct btrfs_delayed_ref_head *head)
{
	struct btrfs_delayed_ref_node *ref;

	if (list_empty(&head->ref_list))
		return NULL;

	/*
	 * Select a delayed ref of type BTRFS_ADD_DELAYED_REF first.
	 * This is to prevent a ref count from going down to zero, which deletes
	 * the extent item from the extent tree, when there still are references
	 * to add, which would fail because they would not find the extent item.
	 */
	list_for_each_entry(ref, &head->ref_list, list) {
		if (ref->action == BTRFS_ADD_DELAYED_REF)
			return ref;
	}

	return list_entry(head->ref_list.next, struct btrfs_delayed_ref_node,
			  list);
}

/*
 * Returns 0 on success or if called with an already aborted transaction.
 * Returns -ENOMEM or -EIO on failure and will abort the transaction.
 */
static noinline int __btrfs_run_delayed_refs(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root,
					     unsigned long nr)
{
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_head *locked_ref = NULL;
	struct btrfs_delayed_extent_op *extent_op;
	struct btrfs_fs_info *fs_info = root->fs_info;
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
			btrfs_delayed_ref_unlock(locked_ref);
			spin_lock(&delayed_refs->lock);
			locked_ref->processing = 0;
			delayed_refs->num_heads_ready++;
			spin_unlock(&delayed_refs->lock);
			locked_ref = NULL;
			cond_resched();
			count++;
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
				spin_unlock(&locked_ref->lock);
				ret = run_delayed_extent_op(trans, root,
							    ref, extent_op);
				btrfs_free_delayed_extent_op(extent_op);

				if (ret) {
					/*
					 * Need to reset must_insert_reserved if
					 * there was an error so the abort stuff
					 * can cleanup the reserved space
					 * properly.
					 */
					if (must_insert_reserved)
						locked_ref->must_insert_reserved = 1;
					locked_ref->processing = 0;
					btrfs_debug(fs_info, "run_delayed_extent_op returned %d", ret);
					btrfs_delayed_ref_unlock(locked_ref);
					return ret;
				}
				continue;
			}

			/*
			 * Need to drop our head ref lock and re-acquire the
			 * delayed ref lock and then re-check to make sure
			 * nobody got added.
			 */
			spin_unlock(&locked_ref->lock);
			spin_lock(&delayed_refs->lock);
			spin_lock(&locked_ref->lock);
			if (!list_empty(&locked_ref->ref_list) ||
			    locked_ref->extent_op) {
				spin_unlock(&locked_ref->lock);
				spin_unlock(&delayed_refs->lock);
				continue;
			}
			ref->in_tree = 0;
			delayed_refs->num_heads--;
			rb_erase(&locked_ref->href_node,
				 &delayed_refs->href_root);
			spin_unlock(&delayed_refs->lock);
		} else {
			actual_count++;
			ref->in_tree = 0;
			list_del(&ref->list);
		}
		atomic_dec(&delayed_refs->num_entries);

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
		spin_unlock(&locked_ref->lock);

		ret = run_one_delayed_ref(trans, root, ref, extent_op,
					  must_insert_reserved);

		btrfs_free_delayed_extent_op(extent_op);
		if (ret) {
			locked_ref->processing = 0;
			btrfs_delayed_ref_unlock(locked_ref);
			btrfs_put_delayed_ref(ref);
			btrfs_debug(fs_info, "run_one_delayed_ref returned %d", ret);
			return ret;
		}

		/*
		 * If this node is a head, that means all the refs in this head
		 * have been dealt with, and we will pick the next head to deal
		 * with, so we must unlock the head and drop it from the cluster
		 * list before we release it.
		 */
		if (btrfs_delayed_ref_is_head(ref)) {
			if (locked_ref->is_data &&
			    locked_ref->total_ref_mod < 0) {
				spin_lock(&delayed_refs->lock);
				delayed_refs->pending_csums -= ref->num_bytes;
				spin_unlock(&delayed_refs->lock);
			}
			btrfs_delayed_ref_unlock(locked_ref);
			locked_ref = NULL;
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

static inline u64 heads_to_leaves(struct btrfs_root *root, u64 heads)
{
	u64 num_bytes;

	num_bytes = heads * (sizeof(struct btrfs_extent_item) +
			     sizeof(struct btrfs_extent_inline_ref));
	if (!btrfs_fs_incompat(root->fs_info, SKINNY_METADATA))
		num_bytes += heads * sizeof(struct btrfs_tree_block_info);

	/*
	 * We don't ever fill up leaves all the way so multiply by 2 just to be
	 * closer to what we're really going to want to use.
	 */
	return div_u64(num_bytes, BTRFS_LEAF_DATA_SIZE(root));
}

/*
 * Takes the number of bytes to be csumm'ed and figures out how many leaves it
 * would require to store the csums for that many bytes.
 */
u64 btrfs_csum_bytes_to_leaves(struct btrfs_root *root, u64 csum_bytes)
{
	u64 csum_size;
	u64 num_csums_per_leaf;
	u64 num_csums;

	csum_size = BTRFS_MAX_ITEM_SIZE(root);
	num_csums_per_leaf = div64_u64(csum_size,
			(u64)btrfs_super_csum_size(root->fs_info->super_copy));
	num_csums = div64_u64(csum_bytes, root->sectorsize);
	num_csums += num_csums_per_leaf - 1;
	num_csums = div64_u64(num_csums, num_csums_per_leaf);
	return num_csums;
}

int btrfs_check_space_for_delayed_refs(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root)
{
	struct btrfs_block_rsv *global_rsv;
	u64 num_heads = trans->transaction->delayed_refs.num_heads_ready;
	u64 csum_bytes = trans->transaction->delayed_refs.pending_csums;
	u64 num_dirty_bgs = trans->transaction->num_dirty_bgs;
	u64 num_bytes, num_dirty_bgs_bytes;
	int ret = 0;

	num_bytes = btrfs_calc_trans_metadata_size(root, 1);
	num_heads = heads_to_leaves(root, num_heads);
	if (num_heads > 1)
		num_bytes += (num_heads - 1) * root->nodesize;
	num_bytes <<= 1;
	num_bytes += btrfs_csum_bytes_to_leaves(root, csum_bytes) * root->nodesize;
	num_dirty_bgs_bytes = btrfs_calc_trans_metadata_size(root,
							     num_dirty_bgs);
	global_rsv = &root->fs_info->global_block_rsv;

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
				       struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 num_entries =
		atomic_read(&trans->transaction->delayed_refs.num_entries);
	u64 avg_runtime;
	u64 val;

	smp_mb();
	avg_runtime = fs_info->avg_delayed_ref_runtime;
	val = num_entries * avg_runtime;
	if (num_entries * avg_runtime >= NSEC_PER_SEC)
		return 1;
	if (val >= NSEC_PER_SEC / 2)
		return 2;

	return btrfs_check_space_for_delayed_refs(trans, root);
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

static void delayed_ref_async_start(struct btrfs_work *work)
{
	struct async_delayed_refs *async;
	struct btrfs_trans_handle *trans;
	int ret;

	async = container_of(work, struct async_delayed_refs, work);

	/* if the commit is already started, we don't need to wait here */
	if (btrfs_transaction_blocked(async->root->fs_info))
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

	ret = btrfs_run_delayed_refs(trans, async->root, async->count);
	if (ret)
		async->error = ret;
end:
	ret = btrfs_end_transaction(trans, async->root);
	if (ret && !async->error)
		async->error = ret;
done:
	if (async->sync)
		complete(&async->wait);
	else
		kfree(async);
}

int btrfs_async_run_delayed_refs(struct btrfs_root *root,
				 unsigned long count, u64 transid, int wait)
{
	struct async_delayed_refs *async;
	int ret;

	async = kmalloc(sizeof(*async), GFP_NOFS);
	if (!async)
		return -ENOMEM;

	async->root = root->fs_info->tree_root;
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

	btrfs_queue_work(root->fs_info->extent_workers, &async->work);

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
			   struct btrfs_root *root, unsigned long count)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_head *head;
	int ret;
	int run_all = count == (unsigned long)-1;
	bool can_flush_pending_bgs = trans->can_flush_pending_bgs;

	/* We'll clean this up in btrfs_cleanup_transaction */
	if (trans->aborted)
		return 0;

	if (root->fs_info->creating_free_space_tree)
		return 0;

	if (root == root->fs_info->extent_root)
		root = root->fs_info->tree_root;

	delayed_refs = &trans->transaction->delayed_refs;
	if (count == 0)
		count = atomic_read(&delayed_refs->num_entries) * 2;

again:
#ifdef SCRAMBLE_DELAYED_REFS
	delayed_refs->run_delayed_start = find_middle(&delayed_refs->root);
#endif
	trans->can_flush_pending_bgs = false;
	ret = __btrfs_run_delayed_refs(trans, root, count);
	if (ret < 0) {
		btrfs_abort_transaction(trans, ret);
		return ret;
	}

	if (run_all) {
		if (!list_empty(&trans->new_bgs))
			btrfs_create_pending_block_groups(trans, root);

		spin_lock(&delayed_refs->lock);
		node = rb_first(&delayed_refs->href_root);
		if (!node) {
			spin_unlock(&delayed_refs->lock);
			goto out;
		}
		count = (unsigned long)-1;

		while (node) {
			head = rb_entry(node, struct btrfs_delayed_ref_head,
					href_node);
			if (btrfs_delayed_ref_is_head(&head->node)) {
				struct btrfs_delayed_ref_node *ref;

				ref = &head->node;
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
			} else {
				WARN_ON(1);
			}
			node = rb_next(node);
		}
		spin_unlock(&delayed_refs->lock);
		cond_resched();
		goto again;
	}
out:
	assert_qgroups_uptodate(trans);
	trans->can_flush_pending_bgs = can_flush_pending_bgs;
	return 0;
}

int btrfs_set_disk_extent_flags(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
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
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(trans, bytenr);
	if (!head) {
		spin_unlock(&delayed_refs->lock);
		return 0;
	}

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
	spin_unlock(&delayed_refs->lock);

	spin_lock(&head->lock);
	list_for_each_entry(ref, &head->ref_list, list) {
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
			   int full_backref, int inc)
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
			    u64, u64, u64, u64, u64, u64);


	if (btrfs_is_testing(root->fs_info))
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
			num_bytes = root->nodesize;
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
				 struct btrfs_root *root,
				 struct btrfs_path *path,
				 struct btrfs_block_group_cache *cache)
{
	int ret;
	struct btrfs_root *extent_root = root->fs_info->extent_root;
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
next_block_group(struct btrfs_root *root,
		 struct btrfs_block_group_cache *cache)
{
	struct rb_node *node;

	spin_lock(&root->fs_info->block_group_cache_lock);

	/* If our block group was removed, we need a full search. */
	if (RB_EMPTY_NODE(&cache->cache_node)) {
		const u64 next_bytenr = cache->key.objectid + cache->key.offset;

		spin_unlock(&root->fs_info->block_group_cache_lock);
		btrfs_put_block_group(cache);
		cache = btrfs_lookup_first_block_group(root->fs_info,
						       next_bytenr);
		return cache;
	}
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

	if (i_size_read(inode) > 0) {
		ret = btrfs_check_trunc_cache_free_space(root,
					&root->fs_info->global_block_rsv);
		if (ret)
			goto out_put;

		ret = btrfs_truncate_free_space_cache(root, trans, NULL, inode);
		if (ret)
			goto out_put;
	}

	spin_lock(&block_group->lock);
	if (block_group->cached != BTRFS_CACHE_FINISHED ||
	    !btrfs_test_opt(root->fs_info, SPACE_CACHE)) {
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

	ret = btrfs_check_data_free_space(inode, 0, num_pages);
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

	return ret;
}

int btrfs_setup_space_cache(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root)
{
	struct btrfs_block_group_cache *cache, *tmp;
	struct btrfs_transaction *cur_trans = trans->transaction;
	struct btrfs_path *path;

	if (list_empty(&cur_trans->dirty_bgs) ||
	    !btrfs_test_opt(root->fs_info, SPACE_CACHE))
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
int btrfs_start_dirty_block_groups(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
{
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
	btrfs_create_pending_block_groups(trans, root);

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
			btrfs_wait_cache_io(root, trans, cache,
					    &cache->io_ctl, path,
					    cache->key.objectid);
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
			ret = btrfs_write_out_cache(root, trans, cache, path);
			if (ret == 0 && cache->io_ctl.inode) {
				num_started++;
				should_put = 0;

				/*
				 * the cache_write_mutex is protecting
				 * the io_list
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
			ret = write_one_cache_group(trans, root, path, cache);
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
	ret = btrfs_run_delayed_refs(trans, root, 0);
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
	}

	btrfs_free_path(path);
	return ret;
}

int btrfs_write_dirty_block_groups(struct btrfs_trans_handle *trans,
				   struct btrfs_root *root)
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
			btrfs_wait_cache_io(root, trans, cache,
					    &cache->io_ctl, path,
					    cache->key.objectid);
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
			ret = btrfs_run_delayed_refs(trans, root, (unsigned long) -1);

		if (!ret && cache->disk_cache_state == BTRFS_DC_SETUP) {
			cache->io_ctl.inode = NULL;
			ret = btrfs_write_out_cache(root, trans, cache, path);
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
			ret = write_one_cache_group(trans, root, path, cache);
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
				ret = write_one_cache_group(trans, root, path,
							    cache);
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

	while (!list_empty(io)) {
		cache = list_first_entry(io, struct btrfs_block_group_cache,
					 io_list);
		list_del_init(&cache->io_list);
		btrfs_wait_cache_io(root, trans, cache,
				    &cache->io_ctl, path, cache->key.objectid);
		btrfs_put_block_group(cache);
	}

	btrfs_free_path(path);
	return ret;
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

static int btrfs_wait_nocow_writers_atomic_t(atomic_t *a)
{
	schedule();
	return 0;
}

void btrfs_wait_nocow_writers(struct btrfs_block_group_cache *bg)
{
	wait_on_atomic_t(&bg->nocow_writers,
			 btrfs_wait_nocow_writers_atomic_t,
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

static int update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     u64 bytes_readonly,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;
	int i;
	int factor;
	int ret;

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
		found->bytes_readonly += bytes_readonly;
		if (total_bytes > 0)
			found->full = 0;
		space_info_add_new_bytes(info, found, total_bytes -
					 bytes_used - bytes_readonly);
		spin_unlock(&found->lock);
		*space_info = found;
		return 0;
	}
	found = kzalloc(sizeof(*found), GFP_NOFS);
	if (!found)
		return -ENOMEM;

	ret = percpu_counter_init(&found->total_bytes_pinned, 0, GFP_KERNEL);
	if (ret) {
		kfree(found);
		return ret;
	}

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
	found->bytes_readonly = bytes_readonly;
	found->bytes_may_use = 0;
	found->full = 0;
	found->max_extent_size = 0;
	found->force_alloc = CHUNK_ALLOC_NO_FORCE;
	found->chunk_alloc = 0;
	found->flush = 0;
	init_waitqueue_head(&found->wait);
	INIT_LIST_HEAD(&found->ro_bgs);
	INIT_LIST_HEAD(&found->tickets);
	INIT_LIST_HEAD(&found->priority_tickets);

	ret = kobject_init_and_add(&found->kobj, &space_info_ktype,
				    info->space_info_kobj, "%s",
				    alloc_name(found->flags));
	if (ret) {
		kfree(found);
		return ret;
	}

	*space_info = found;
	list_add_rcu(&found->list, &info->space_info);
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		info->data_sinfo = found;

	return ret;
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
static u64 btrfs_reduce_alloc_profile(struct btrfs_root *root, u64 flags)
{
	u64 num_devices = root->fs_info->fs_devices->rw_devices;
	u64 target;
	u64 raid_type;
	u64 allowed = 0;

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

static u64 get_alloc_profile(struct btrfs_root *root, u64 orig_flags)
{
	unsigned seq;
	u64 flags;

	do {
		flags = orig_flags;
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

int btrfs_alloc_data_chunk_ondemand(struct inode *inode, u64 bytes)
{
	struct btrfs_space_info *data_sinfo;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 used;
	int ret = 0;
	int need_commit = 2;
	int have_pinned_space;

	/* make sure bytes are sectorsize aligned */
	bytes = ALIGN(bytes, root->sectorsize);

	if (btrfs_is_free_space_inode(inode)) {
		need_commit = 0;
		ASSERT(current->journal_info);
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
		if (!data_sinfo->full) {
			u64 alloc_target;

			data_sinfo->force_alloc = CHUNK_ALLOC_FORCE;
			spin_unlock(&data_sinfo->lock);
alloc:
			alloc_target = btrfs_get_alloc_profile(root, 1);
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

			ret = do_chunk_alloc(trans, root->fs_info->extent_root,
					     alloc_target,
					     CHUNK_ALLOC_NO_FORCE);
			btrfs_end_transaction(trans, root);
			if (ret < 0) {
				if (ret != -ENOSPC)
					return ret;
				else {
					have_pinned_space = 1;
					goto commit_trans;
				}
			}

			if (!data_sinfo)
				data_sinfo = fs_info->data_sinfo;

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
		if (need_commit &&
		    !atomic_read(&root->fs_info->open_ioctl_trans)) {
			need_commit--;

			if (need_commit > 0) {
				btrfs_start_delalloc_roots(fs_info, 0, -1);
				btrfs_wait_ordered_roots(fs_info, -1, 0, (u64)-1);
			}

			trans = btrfs_join_transaction(root);
			if (IS_ERR(trans))
				return PTR_ERR(trans);
			if (have_pinned_space >= 0 ||
			    test_bit(BTRFS_TRANS_HAVE_FREE_BGS,
				     &trans->transaction->flags) ||
			    need_commit > 0) {
				ret = btrfs_commit_transaction(trans, root);
				if (ret)
					return ret;
				/*
				 * The cleaner kthread might still be doing iput
				 * operations. Wait for it to finish so that
				 * more space is released.
				 */
				mutex_lock(&root->fs_info->cleaner_delayed_iput_mutex);
				mutex_unlock(&root->fs_info->cleaner_delayed_iput_mutex);
				goto again;
			} else {
				btrfs_end_transaction(trans, root);
			}
		}

		trace_btrfs_space_reservation(root->fs_info,
					      "space_info:enospc",
					      data_sinfo->flags, bytes, 1);
		return -ENOSPC;
	}
	data_sinfo->bytes_may_use += bytes;
	trace_btrfs_space_reservation(root->fs_info, "space_info",
				      data_sinfo->flags, bytes, 1);
	spin_unlock(&data_sinfo->lock);

	return ret;
}

/*
 * New check_data_free_space() with ability for precious data reservation
 * Will replace old btrfs_check_data_free_space(), but for patch split,
 * add a new function first and then replace it.
 */
int btrfs_check_data_free_space(struct inode *inode, u64 start, u64 len)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	/* align the range */
	len = round_up(start + len, root->sectorsize) -
	      round_down(start, root->sectorsize);
	start = round_down(start, root->sectorsize);

	ret = btrfs_alloc_data_chunk_ondemand(inode, len);
	if (ret < 0)
		return ret;

	/*
	 * Use new btrfs_qgroup_reserve_data to reserve precious data space
	 *
	 * TODO: Find a good method to avoid reserve data space for NOCOW
	 * range, but don't impact performance on quota disable case.
	 */
	ret = btrfs_qgroup_reserve_data(inode, start, len);
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
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_space_info *data_sinfo;

	/* Make sure the range is aligned to sectorsize */
	len = round_up(start + len, root->sectorsize) -
	      round_down(start, root->sectorsize);
	start = round_down(start, root->sectorsize);

	data_sinfo = root->fs_info->data_sinfo;
	spin_lock(&data_sinfo->lock);
	if (WARN_ON(data_sinfo->bytes_may_use < len))
		data_sinfo->bytes_may_use = 0;
	else
		data_sinfo->bytes_may_use -= len;
	trace_btrfs_space_reservation(root->fs_info, "space_info",
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
void btrfs_free_reserved_data_space(struct inode *inode, u64 start, u64 len)
{
	btrfs_free_reserved_data_space_noquota(inode, start, len);
	btrfs_qgroup_free_data(inode, start, len);
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
		num_allocated += calc_global_rsv_need_space(global_rsv);

	/*
	 * in limited mode, we want to have some free space up to
	 * about 1% of the FS size.
	 */
	if (force == CHUNK_ALLOC_LIMITED) {
		thresh = btrfs_super_total_bytes(root->fs_info->super_copy);
		thresh = max_t(u64, SZ_64M, div_factor_fine(thresh, 1));

		if (num_bytes - num_allocated < thresh)
			return 1;
	}

	if (num_allocated + SZ_2M < div_factor(num_bytes, 8))
		return 0;
	return 1;
}

static u64 get_profile_num_devs(struct btrfs_root *root, u64 type)
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

	return num_dev;
}

/*
 * If @is_allocation is true, reserve space in the system space info necessary
 * for allocating a chunk, otherwise if it's false, reserve space necessary for
 * removing a chunk.
 */
void check_system_chunk(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			u64 type)
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
	ASSERT(mutex_is_locked(&root->fs_info->chunk_mutex));

	info = __find_space_info(root->fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
	spin_lock(&info->lock);
	left = info->total_bytes - info->bytes_used - info->bytes_pinned -
		info->bytes_reserved - info->bytes_readonly -
		info->bytes_may_use;
	spin_unlock(&info->lock);

	num_devs = get_profile_num_devs(root, type);

	/* num_devs device items to update and 1 chunk item to add or remove */
	thresh = btrfs_calc_trunc_metadata_size(root, num_devs) +
		btrfs_calc_trans_metadata_size(root, 1);

	if (left < thresh && btrfs_test_opt(root->fs_info, ENOSPC_DEBUG)) {
		btrfs_info(root->fs_info, "left=%llu, need=%llu, flags=%llu",
			left, thresh, type);
		dump_space_info(info, 0, 0);
	}

	if (left < thresh) {
		u64 flags;

		flags = btrfs_get_alloc_profile(root->fs_info->chunk_root, 0);
		/*
		 * Ignore failure to create system chunk. We might end up not
		 * needing it, as we might not need to COW all nodes/leafs from
		 * the paths we visit in the chunk tree (they were already COWed
		 * or created in the current transaction for example).
		 */
		ret = btrfs_alloc_chunk(trans, root, flags);
	}

	if (!ret) {
		ret = btrfs_block_rsv_add(root->fs_info->chunk_root,
					  &root->fs_info->chunk_block_rsv,
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
					0, 0, 0, &space_info);
		BUG_ON(ret); /* -ENOMEM */
	}
	BUG_ON(!space_info); /* Logic error */

again:
	spin_lock(&space_info->lock);
	if (force < space_info->force_alloc)
		force = space_info->force_alloc;
	if (space_info->full) {
		if (should_alloc_chunk(extent_root, space_info, force))
			ret = -ENOSPC;
		else
			ret = 0;
		spin_unlock(&space_info->lock);
		return ret;
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
		btrfs_create_pending_block_groups(trans, extent_root);
		btrfs_trans_release_chunk_metadata(trans);
	}
	return ret;
}

static int can_overcommit(struct btrfs_root *root,
			  struct btrfs_space_info *space_info, u64 bytes,
			  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_block_rsv *global_rsv;
	u64 profile;
	u64 space_size;
	u64 avail;
	u64 used;

	/* Don't overcommit when in mixed mode. */
	if (space_info->flags & BTRFS_BLOCK_GROUP_DATA)
		return 0;

	BUG_ON(root->fs_info == NULL);
	global_rsv = &root->fs_info->global_block_rsv;
	profile = btrfs_get_alloc_profile(root, 0);
	used = space_info->bytes_used + space_info->bytes_reserved +
		space_info->bytes_pinned + space_info->bytes_readonly;

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

static void btrfs_writeback_inodes_sb_nr(struct btrfs_root *root,
					 unsigned long nr_pages, int nr_items)
{
	struct super_block *sb = root->fs_info->sb;

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
		btrfs_start_delalloc_roots(root->fs_info, 0, nr_items);
		if (!current->journal_info)
			btrfs_wait_ordered_roots(root->fs_info, nr_items,
						 0, (u64)-1);
	}
}

static inline int calc_reclaim_items_nr(struct btrfs_root *root, u64 to_reclaim)
{
	u64 bytes;
	int nr;

	bytes = btrfs_calc_trans_metadata_size(root, 1);
	nr = (int)div64_u64(to_reclaim, bytes);
	if (!nr)
		nr = 1;
	return nr;
}

#define EXTENT_SIZE_PER_ITEM	SZ_256K

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
	unsigned long nr_pages;
	int loops;
	int items;
	enum btrfs_reserve_flush_enum flush;

	/* Calc the number of the pages we need flush for space reservation */
	items = calc_reclaim_items_nr(root, to_reclaim);
	to_reclaim = (u64)items * EXTENT_SIZE_PER_ITEM;

	trans = (struct btrfs_trans_handle *)current->journal_info;
	block_rsv = &root->fs_info->delalloc_block_rsv;
	space_info = block_rsv->space_info;

	delalloc_bytes = percpu_counter_sum_positive(
						&root->fs_info->delalloc_bytes);
	if (delalloc_bytes == 0) {
		if (trans)
			return;
		if (wait_ordered)
			btrfs_wait_ordered_roots(root->fs_info, items,
						 0, (u64)-1);
		return;
	}

	loops = 0;
	while (delalloc_bytes && loops < 3) {
		max_reclaim = min(delalloc_bytes, to_reclaim);
		nr_pages = max_reclaim >> PAGE_SHIFT;
		btrfs_writeback_inodes_sb_nr(root, nr_pages, items);
		/*
		 * We need to wait for the async pages to actually start before
		 * we do anything.
		 */
		max_reclaim = atomic_read(&root->fs_info->async_delalloc_pages);
		if (!max_reclaim)
			goto skip_async;

		if (max_reclaim <= nr_pages)
			max_reclaim = 0;
		else
			max_reclaim -= nr_pages;

		wait_event(root->fs_info->async_submit_wait,
			   atomic_read(&root->fs_info->async_delalloc_pages) <=
			   (int)max_reclaim);
skip_async:
		if (!trans)
			flush = BTRFS_RESERVE_FLUSH_ALL;
		else
			flush = BTRFS_RESERVE_NO_FLUSH;
		spin_lock(&space_info->lock);
		if (can_overcommit(root, space_info, orig, flush)) {
			spin_unlock(&space_info->lock);
			break;
		}
		if (list_empty(&space_info->tickets) &&
		    list_empty(&space_info->priority_tickets)) {
			spin_unlock(&space_info->lock);
			break;
		}
		spin_unlock(&space_info->lock);

		loops++;
		if (wait_ordered && !trans) {
			btrfs_wait_ordered_roots(root->fs_info, items,
						 0, (u64)-1);
		} else {
			time_left = schedule_timeout_killable(1);
			if (time_left)
				break;
		}
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
	if (percpu_counter_compare(&space_info->total_bytes_pinned,
				   bytes - delayed_rsv->size) >= 0) {
		spin_unlock(&delayed_rsv->lock);
		return -ENOSPC;
	}
	spin_unlock(&delayed_rsv->lock);

commit:
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return -ENOSPC;

	return btrfs_commit_transaction(trans, root);
}

struct reserve_ticket {
	u64 bytes;
	int error;
	struct list_head list;
	wait_queue_head_t wait;
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
		if (state == FLUSH_DELAYED_ITEMS_NR)
			nr = calc_reclaim_items_nr(root, num_bytes) * 2;
		else
			nr = -1;

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
		shrink_delalloc(root, num_bytes * 2, orig_bytes,
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
		if (ret > 0 || ret == -ENOSPC)
			ret = 0;
		break;
	case COMMIT_TRANS:
		ret = may_commit_transaction(root, space_info, orig_bytes, 0);
		break;
	default:
		ret = -ENOSPC;
		break;
	}

	trace_btrfs_flush_space(root->fs_info, space_info->flags, num_bytes,
				orig_bytes, state, ret);
	return ret;
}

static inline u64
btrfs_calc_reclaim_metadata_size(struct btrfs_root *root,
				 struct btrfs_space_info *space_info)
{
	struct reserve_ticket *ticket;
	u64 used;
	u64 expected;
	u64 to_reclaim = 0;

	to_reclaim = min_t(u64, num_online_cpus() * SZ_1M, SZ_16M);
	if (can_overcommit(root, space_info, to_reclaim,
			   BTRFS_RESERVE_FLUSH_ALL))
		return 0;

	list_for_each_entry(ticket, &space_info->tickets, list)
		to_reclaim += ticket->bytes;
	list_for_each_entry(ticket, &space_info->priority_tickets, list)
		to_reclaim += ticket->bytes;
	if (to_reclaim)
		return to_reclaim;

	used = space_info->bytes_used + space_info->bytes_reserved +
	       space_info->bytes_pinned + space_info->bytes_readonly +
	       space_info->bytes_may_use;
	if (can_overcommit(root, space_info, SZ_1M, BTRFS_RESERVE_FLUSH_ALL))
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

static inline int need_do_async_reclaim(struct btrfs_space_info *space_info,
					struct btrfs_root *root, u64 used)
{
	u64 thresh = div_factor_fine(space_info->total_bytes, 98);

	/* If we're just plain full then async reclaim just slows us down. */
	if ((space_info->bytes_used + space_info->bytes_reserved) >= thresh)
		return 0;

	if (!btrfs_calc_reclaim_metadata_size(root, space_info))
		return 0;

	return (used >= thresh && !btrfs_fs_closing(root->fs_info) &&
		!test_bit(BTRFS_FS_STATE_REMOUNTING,
			  &root->fs_info->fs_state));
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
	struct reserve_ticket *last_ticket = NULL;
	struct btrfs_fs_info *fs_info;
	struct btrfs_space_info *space_info;
	u64 to_reclaim;
	int flush_state;
	int commit_cycles = 0;

	fs_info = container_of(work, struct btrfs_fs_info, async_reclaim_work);
	space_info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);

	spin_lock(&space_info->lock);
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info->fs_root,
						      space_info);
	if (!to_reclaim) {
		space_info->flush = 0;
		spin_unlock(&space_info->lock);
		return;
	}
	last_ticket = list_first_entry(&space_info->tickets,
				       struct reserve_ticket, list);
	spin_unlock(&space_info->lock);

	flush_state = FLUSH_DELAYED_ITEMS_NR;
	do {
		struct reserve_ticket *ticket;
		int ret;

		ret = flush_space(fs_info->fs_root, space_info, to_reclaim,
			    to_reclaim, flush_state);
		spin_lock(&space_info->lock);
		if (list_empty(&space_info->tickets)) {
			space_info->flush = 0;
			spin_unlock(&space_info->lock);
			return;
		}
		to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info->fs_root,
							      space_info);
		ticket = list_first_entry(&space_info->tickets,
					  struct reserve_ticket, list);
		if (last_ticket == ticket) {
			flush_state++;
		} else {
			last_ticket = ticket;
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
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info->fs_root,
						      space_info);
	if (!to_reclaim) {
		spin_unlock(&space_info->lock);
		return;
	}
	spin_unlock(&space_info->lock);

	do {
		flush_space(fs_info->fs_root, space_info, to_reclaim,
			    to_reclaim, flush_state);
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
static int __reserve_metadata_bytes(struct btrfs_root *root,
				    struct btrfs_space_info *space_info,
				    u64 orig_bytes,
				    enum btrfs_reserve_flush_enum flush)
{
	struct reserve_ticket ticket;
	u64 used;
	int ret = 0;

	ASSERT(orig_bytes);
	ASSERT(!current->journal_info || flush != BTRFS_RESERVE_FLUSH_ALL);

	spin_lock(&space_info->lock);
	ret = -ENOSPC;
	used = space_info->bytes_used + space_info->bytes_reserved +
		space_info->bytes_pinned + space_info->bytes_readonly +
		space_info->bytes_may_use;

	/*
	 * If we have enough space then hooray, make our reservation and carry
	 * on.  If not see if we can overcommit, and if we can, hooray carry on.
	 * If not things get more complicated.
	 */
	if (used + orig_bytes <= space_info->total_bytes) {
		space_info->bytes_may_use += orig_bytes;
		trace_btrfs_space_reservation(root->fs_info, "space_info",
					      space_info->flags, orig_bytes,
					      1);
		ret = 0;
	} else if (can_overcommit(root, space_info, orig_bytes, flush)) {
		space_info->bytes_may_use += orig_bytes;
		trace_btrfs_space_reservation(root->fs_info, "space_info",
					      space_info->flags, orig_bytes,
					      1);
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
				trace_btrfs_trigger_flush(root->fs_info,
							  space_info->flags,
							  orig_bytes, flush,
							  "enospc");
				queue_work(system_unbound_wq,
					   &root->fs_info->async_reclaim_work);
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
		if (!root->fs_info->log_root_recovering &&
		    need_do_async_reclaim(space_info, root, used) &&
		    !work_busy(&root->fs_info->async_reclaim_work)) {
			trace_btrfs_trigger_flush(root->fs_info,
						  space_info->flags,
						  orig_bytes, flush,
						  "preempt");
			queue_work(system_unbound_wq,
				   &root->fs_info->async_reclaim_work);
		}
	}
	spin_unlock(&space_info->lock);
	if (!ret || flush == BTRFS_RESERVE_NO_FLUSH)
		return ret;

	if (flush == BTRFS_RESERVE_FLUSH_ALL)
		return wait_reserve_ticket(root->fs_info, space_info, &ticket,
					   orig_bytes);

	ret = 0;
	priority_reclaim_metadata_space(root->fs_info, space_info, &ticket);
	spin_lock(&space_info->lock);
	if (ticket.bytes) {
		if (ticket.bytes < orig_bytes) {
			u64 num_bytes = orig_bytes - ticket.bytes;
			space_info->bytes_may_use -= num_bytes;
			trace_btrfs_space_reservation(root->fs_info,
					"space_info", space_info->flags,
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
	int ret;

	ret = __reserve_metadata_bytes(root, block_rsv->space_info, orig_bytes,
				       flush);
	if (ret == -ENOSPC &&
	    unlikely(root->orphan_cleanup_state == ORPHAN_CLEANUP_STARTED)) {
		struct btrfs_block_rsv *global_rsv =
			&root->fs_info->global_block_rsv;

		if (block_rsv != global_rsv &&
		    !block_rsv_use_bytes(global_rsv, orig_bytes))
			ret = 0;
	}
	if (ret == -ENOSPC)
		trace_btrfs_space_reservation(root->fs_info,
					      "space_info:enospc",
					      block_rsv->space_info->flags,
					      orig_bytes, 1);
	return ret;
}

static struct btrfs_block_rsv *get_block_rsv(
					const struct btrfs_trans_handle *trans,
					const struct btrfs_root *root)
{
	struct btrfs_block_rsv *block_rsv = NULL;

	if (test_bit(BTRFS_ROOT_REF_COWS, &root->state) ||
	    (root == root->fs_info->csum_root && trans->adding_csums) ||
	     (root == root->fs_info->uuid_root))
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
	used = space_info->bytes_used + space_info->bytes_reserved +
		space_info->bytes_pinned + space_info->bytes_readonly +
		space_info->bytes_may_use;
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
		    !can_overcommit(fs_info->extent_root, space_info, 0,
				    flush))
			break;
		if (num_bytes >= ticket->bytes) {
			list_del_init(&ticket->list);
			num_bytes -= ticket->bytes;
			ticket->bytes = 0;
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
		if (num_bytes)
			space_info_add_old_bytes(fs_info, space_info,
						 num_bytes);
	}
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

void btrfs_block_rsv_release(struct btrfs_root *root,
			     struct btrfs_block_rsv *block_rsv,
			     u64 num_bytes)
{
	struct btrfs_block_rsv *global_rsv = &root->fs_info->global_block_rsv;
	if (global_rsv == block_rsv ||
	    block_rsv->space_info != global_rsv->space_info)
		global_rsv = NULL;
	block_rsv_release_bytes(root->fs_info, block_rsv, global_rsv,
				num_bytes);
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
		num_bytes = sinfo->bytes_used + sinfo->bytes_pinned +
			sinfo->bytes_reserved + sinfo->bytes_readonly +
			sinfo->bytes_may_use;
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
	fs_info->delalloc_block_rsv.space_info = space_info;
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
				  struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
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
	u64 num_bytes = btrfs_calc_trans_metadata_size(root, 1);
	trace_btrfs_space_reservation(root->fs_info, "orphan",
				      btrfs_ino(inode), num_bytes, 1);
	return btrfs_block_rsv_migrate(src_rsv, dst_rsv, num_bytes, 1);
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
	struct btrfs_block_rsv *global_rsv = &root->fs_info->global_block_rsv;

	if (root->fs_info->quota_enabled) {
		/* One for parent inode, two for dir entries */
		num_bytes = 3 * root->nodesize;
		ret = btrfs_qgroup_reserve_meta(root, num_bytes);
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

	if (ret == -ENOSPC && use_global_rsv)
		ret = btrfs_block_rsv_migrate(global_rsv, rsv, num_bytes, 1);

	if (ret && *qgroup_reserved)
		btrfs_qgroup_free_meta(root, *qgroup_reserved);

	return ret;
}

void btrfs_subvolume_release_metadata(struct btrfs_root *root,
				      struct btrfs_block_rsv *rsv,
				      u64 qgroup_reserved)
{
	btrfs_block_rsv_release(root, rsv, (u64)-1);
}

/**
 * drop_outstanding_extent - drop an outstanding extent
 * @inode: the inode we're dropping the extent for
 * @num_bytes: the number of bytes we're releasing.
 *
 * This is called when we are freeing up an outstanding extent, either called
 * after an error or after an extent is written.  This will return the number of
 * reserved extents that need to be freed.  This must be called with
 * BTRFS_I(inode)->lock held.
 */
static unsigned drop_outstanding_extent(struct inode *inode, u64 num_bytes)
{
	unsigned drop_inode_space = 0;
	unsigned dropped_extents = 0;
	unsigned num_extents = 0;

	num_extents = (unsigned)div64_u64(num_bytes +
					  BTRFS_MAX_EXTENT_SIZE - 1,
					  BTRFS_MAX_EXTENT_SIZE);
	ASSERT(num_extents);
	ASSERT(BTRFS_I(inode)->outstanding_extents >= num_extents);
	BTRFS_I(inode)->outstanding_extents -= num_extents;

	if (BTRFS_I(inode)->outstanding_extents == 0 &&
	    test_and_clear_bit(BTRFS_INODE_DELALLOC_META_RESERVED,
			       &BTRFS_I(inode)->runtime_flags))
		drop_inode_space = 1;

	/*
	 * If we have more or the same amount of outstanding extents than we have
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
 * calc_csum_metadata_size - return the amount of metadata space that must be
 *	reserved/freed for the given bytes.
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
	u64 old_csums, num_csums;

	if (BTRFS_I(inode)->flags & BTRFS_INODE_NODATASUM &&
	    BTRFS_I(inode)->csum_bytes == 0)
		return 0;

	old_csums = btrfs_csum_bytes_to_leaves(root, BTRFS_I(inode)->csum_bytes);
	if (reserve)
		BTRFS_I(inode)->csum_bytes += num_bytes;
	else
		BTRFS_I(inode)->csum_bytes -= num_bytes;
	num_csums = btrfs_csum_bytes_to_leaves(root, BTRFS_I(inode)->csum_bytes);

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
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_FLUSH_ALL;
	int ret = 0;
	bool delalloc_lock = true;
	u64 to_free = 0;
	unsigned dropped;
	bool release_extra = false;

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
	} else if (current->journal_info) {
		flush = BTRFS_RESERVE_FLUSH_LIMIT;
	}

	if (flush != BTRFS_RESERVE_NO_FLUSH &&
	    btrfs_transaction_in_commit(root->fs_info))
		schedule_timeout(1);

	if (delalloc_lock)
		mutex_lock(&BTRFS_I(inode)->delalloc_mutex);

	num_bytes = ALIGN(num_bytes, root->sectorsize);

	spin_lock(&BTRFS_I(inode)->lock);
	nr_extents = (unsigned)div64_u64(num_bytes +
					 BTRFS_MAX_EXTENT_SIZE - 1,
					 BTRFS_MAX_EXTENT_SIZE);
	BTRFS_I(inode)->outstanding_extents += nr_extents;

	nr_extents = 0;
	if (BTRFS_I(inode)->outstanding_extents >
	    BTRFS_I(inode)->reserved_extents)
		nr_extents += BTRFS_I(inode)->outstanding_extents -
			BTRFS_I(inode)->reserved_extents;

	/* We always want to reserve a slot for updating the inode. */
	to_reserve = btrfs_calc_trans_metadata_size(root, nr_extents + 1);
	to_reserve += calc_csum_metadata_size(inode, num_bytes, 1);
	csum_bytes = BTRFS_I(inode)->csum_bytes;
	spin_unlock(&BTRFS_I(inode)->lock);

	if (root->fs_info->quota_enabled) {
		ret = btrfs_qgroup_reserve_meta(root,
				nr_extents * root->nodesize);
		if (ret)
			goto out_fail;
	}

	ret = btrfs_block_rsv_add(root, block_rsv, to_reserve, flush);
	if (unlikely(ret)) {
		btrfs_qgroup_free_meta(root, nr_extents * root->nodesize);
		goto out_fail;
	}

	spin_lock(&BTRFS_I(inode)->lock);
	if (test_and_set_bit(BTRFS_INODE_DELALLOC_META_RESERVED,
			     &BTRFS_I(inode)->runtime_flags)) {
		to_reserve -= btrfs_calc_trans_metadata_size(root, 1);
		release_extra = true;
	}
	BTRFS_I(inode)->reserved_extents += nr_extents;
	spin_unlock(&BTRFS_I(inode)->lock);

	if (delalloc_lock)
		mutex_unlock(&BTRFS_I(inode)->delalloc_mutex);

	if (to_reserve)
		trace_btrfs_space_reservation(root->fs_info, "delalloc",
					      btrfs_ino(inode), to_reserve, 1);
	if (release_extra)
		btrfs_block_rsv_release(root, block_rsv,
					btrfs_calc_trans_metadata_size(root,
								       1));
	return 0;

out_fail:
	spin_lock(&BTRFS_I(inode)->lock);
	dropped = drop_outstanding_extent(inode, num_bytes);
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
		 * freed from any free-ers that occurred during this
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
		 * more than to_free then we would have freed more space had we
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
	dropped = drop_outstanding_extent(inode, num_bytes);

	if (num_bytes)
		to_free = calc_csum_metadata_size(inode, num_bytes, 0);
	spin_unlock(&BTRFS_I(inode)->lock);
	if (dropped > 0)
		to_free += btrfs_calc_trans_metadata_size(root, dropped);

	if (btrfs_is_testing(root->fs_info))
		return;

	trace_btrfs_space_reservation(root->fs_info, "delalloc",
				      btrfs_ino(inode), to_free, 0);

	btrfs_block_rsv_release(root, &root->fs_info->delalloc_block_rsv,
				to_free);
}

/**
 * btrfs_delalloc_reserve_space - reserve data and metadata space for
 * delalloc
 * @inode: inode we're writing to
 * @start: start range we are writing to
 * @len: how long the range we are writing to
 *
 * TODO: This function will finally replace old btrfs_delalloc_reserve_space()
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
int btrfs_delalloc_reserve_space(struct inode *inode, u64 start, u64 len)
{
	int ret;

	ret = btrfs_check_data_free_space(inode, start, len);
	if (ret < 0)
		return ret;
	ret = btrfs_delalloc_reserve_metadata(inode, len);
	if (ret < 0)
		btrfs_free_reserved_data_space(inode, start, len);
	return ret;
}

/**
 * btrfs_delalloc_release_space - release data and metadata space for delalloc
 * @inode: inode we're releasing space for
 * @start: start position of the space already reserved
 * @len: the len of the space already reserved
 *
 * This must be matched with a call to btrfs_delalloc_reserve_space.  This is
 * called in the case that we don't need the metadata AND data reservations
 * anymore.  So if there is an error or we insert an inline extent.
 *
 * This function will release the metadata space that was not used and will
 * decrement ->delalloc_bytes and remove it from the fs_info delalloc_inodes
 * list if there are no delalloc bytes left.
 * Also it will handle the qgroup reserved space.
 */
void btrfs_delalloc_release_space(struct inode *inode, u64 start, u64 len)
{
	btrfs_delalloc_release_metadata(inode, len);
	btrfs_free_reserved_data_space(inode, start, len);
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root, u64 bytenr,
			      u64 num_bytes, int alloc)
{
	struct btrfs_block_group_cache *cache = NULL;
	struct btrfs_fs_info *info = root->fs_info;
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

		if (btrfs_test_opt(root->fs_info, SPACE_CACHE) &&
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

			trace_btrfs_space_reservation(root->fs_info, "pinned",
						      cache->space_info->flags,
						      num_bytes, 1);
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

	trace_btrfs_space_reservation(root->fs_info, "pinned",
				      cache->space_info->flags, num_bytes, 1);
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
	int ret;

	cache = btrfs_lookup_block_group(root->fs_info, bytenr);
	if (!cache)
		return -EINVAL;

	/*
	 * pull in the free space cache (if any) so that our pin
	 * removes the free space from the cache.  We have load_only set
	 * to one because the slow code to read in the free extents does check
	 * the pinned extents.
	 */
	cache_block_group(cache, 1);

	pin_down_extent(root, cache, bytenr, num_bytes, 0);

	/* remove us from the free space cache (if we're there at all) */
	ret = btrfs_remove_free_space(cache, bytenr, num_bytes);
	btrfs_put_block_group(cache);
	return ret;
}

static int __exclude_logged_extent(struct btrfs_root *root, u64 start, u64 num_bytes)
{
	int ret;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_caching_control *caching_ctl;

	block_group = btrfs_lookup_block_group(root->fs_info, start);
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
			ret = add_excluded_extent(root, start, num_bytes);
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
			ret = add_excluded_extent(root, start, num_bytes);
		}
out_lock:
		mutex_unlock(&caching_ctl->mutex);
		put_caching_control(caching_ctl);
	}
	btrfs_put_block_group(block_group);
	return ret;
}

int btrfs_exclude_logged_extents(struct btrfs_root *log,
				 struct extent_buffer *eb)
{
	struct btrfs_file_extent_item *item;
	struct btrfs_key key;
	int found_type;
	int i;

	if (!btrfs_fs_incompat(log->fs_info, MIXED_GROUPS))
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
		__exclude_logged_extent(log, key.objectid, key.offset);
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

static int btrfs_wait_bg_reservations_atomic_t(atomic_t *a)
{
	schedule();
	return 0;
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

	wait_on_atomic_t(&bg->reservations,
			 btrfs_wait_bg_reservations_atomic_t,
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
 * This is called by the allocator when it reserves space. Metadata
 * reservations should be called with RESERVE_ALLOC so we do the proper
 * ENOSPC accounting.  For data we handle the reservation through clearing the
 * delalloc bits in the io_tree.  We have to do this since we could end up
 * allocating less disk space for the amount of data we have reserved in the
 * case of compression.
 *
 * If this is a reservation and the block group has become read only we cannot
 * make the reservation and return -EAGAIN, otherwise this function always
 * succeeds.
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
void btrfs_prepare_extent_commit(struct btrfs_trans_handle *trans,
				struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
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
fetch_cluster_info(struct btrfs_root *root, struct btrfs_space_info *space_info,
		   u64 *empty_cluster)
{
	struct btrfs_free_cluster *ret = NULL;
	bool ssd = btrfs_test_opt(root->fs_info, SSD);

	*empty_cluster = 0;
	if (btrfs_mixed_space_info(space_info))
		return ret;

	if (ssd)
		*empty_cluster = SZ_2M;
	if (space_info->flags & BTRFS_BLOCK_GROUP_METADATA) {
		ret = &root->fs_info->meta_alloc_cluster;
		if (!ssd)
			*empty_cluster = SZ_64K;
	} else if ((space_info->flags & BTRFS_BLOCK_GROUP_DATA) && ssd) {
		ret = &root->fs_info->data_alloc_cluster;
	}

	return ret;
}

static int unpin_extent_range(struct btrfs_root *root, u64 start, u64 end,
			      const bool return_free_space)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
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

			cluster = fetch_cluster_info(root,
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
			WARN_ON(!return_free_space);
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

int btrfs_finish_extent_commit(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
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

		if (btrfs_test_opt(root->fs_info, DISCARD))
			ret = btrfs_discard_extent(root, start,
						   end + 1 - start, NULL);

		clear_extent_dirty(unpin, start, end);
		unpin_extent_range(root, start, end, true);
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
			ret = btrfs_discard_extent(root,
						   block_group->key.objectid,
						   block_group->key.offset,
						   &trimmed);

		list_del_init(&block_group->bg_list);
		btrfs_put_block_group_trimming(block_group);
		btrfs_put_block_group(block_group);

		if (ret) {
			const char *errstr = btrfs_decode_error(ret);
			btrfs_warn(fs_info,
				   "Discard failed while removing blockgroup: errno=%d %s\n",
				   ret, errstr);
		}
	}

	return 0;
}

static void add_pinned_bytes(struct btrfs_fs_info *fs_info, u64 num_bytes,
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
	BUG_ON(!space_info); /* Logic bug */
	percpu_counter_add(&space_info->total_bytes_pinned, num_bytes);
}


static int __btrfs_free_extent(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				struct btrfs_delayed_ref_node *node, u64 parent,
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
	u64 bytenr = node->bytenr;
	u64 num_bytes = node->num_bytes;
	int last_ref = 0;
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = READA_FORWARD;
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
				btrfs_err(info, "umm, got %d back from search, was looking for %llu",
					ret, bytenr);
				if (ret > 0)
					btrfs_print_leaf(extent_root,
							 path->nodes[0]);
			}
			if (ret < 0) {
				btrfs_abort_transaction(trans, ret);
				goto out;
			}
			extent_slot = path->slots[0];
		}
	} else if (WARN_ON(ret == -ENOENT)) {
		btrfs_print_leaf(extent_root, path->nodes[0]);
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
		ret = convert_extent_item_v0(trans, extent_root, path,
					     owner_objectid, 0);
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
			btrfs_err(info, "umm, got %d back from search, was looking for %llu",
				ret, bytenr);
			btrfs_print_leaf(extent_root, path->nodes[0]);
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
		btrfs_err(info, "trying to drop %d refs but we only have %Lu "
			  "for bytenr %Lu", refs_to_drop, refs, bytenr);
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
			ret = remove_extent_backref(trans, extent_root, path,
						    iref, refs_to_drop,
						    is_data, &last_ref);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				goto out;
			}
		}
		add_pinned_bytes(root->fs_info, -num_bytes, owner_objectid,
				 root_objectid);
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
			ret = btrfs_del_csums(trans, root, bytenr, num_bytes);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				goto out;
			}
		}

		ret = add_to_free_space_tree(trans, root->fs_info, bytenr,
					     num_bytes);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		ret = update_block_group(trans, root, bytenr, num_bytes, 0);
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
				      struct btrfs_root *root, u64 bytenr)
{
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_root *delayed_refs;
	int ret = 0;

	delayed_refs = &trans->transaction->delayed_refs;
	spin_lock(&delayed_refs->lock);
	head = btrfs_find_delayed_ref_head(trans, bytenr);
	if (!head)
		goto out_delayed_unlock;

	spin_lock(&head->lock);
	if (!list_empty(&head->ref_list))
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
	rb_erase(&head->href_node, &delayed_refs->href_root);

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
	btrfs_put_delayed_ref(&head->node);
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
	int pin = 1;
	int ret;

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(root->fs_info, trans,
					buf->start, buf->len,
					parent, root->root_key.objectid,
					btrfs_header_level(buf),
					BTRFS_DROP_DELAYED_REF, NULL);
		BUG_ON(ret); /* -ENOMEM */
	}

	if (!last_ref)
		return;

	if (btrfs_header_generation(buf) == trans->transid) {
		struct btrfs_block_group_cache *cache;

		if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
			ret = check_ref_cleanup(trans, root, buf->start);
			if (!ret)
				goto out;
		}

		cache = btrfs_lookup_block_group(root->fs_info, buf->start);

		if (btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
			pin_down_extent(root, cache, buf->start, buf->len, 1);
			btrfs_put_block_group(cache);
			goto out;
		}

		WARN_ON(test_bit(EXTENT_BUFFER_DIRTY, &buf->bflags));

		btrfs_add_free_space(cache, buf->start, buf->len);
		btrfs_free_reserved_bytes(cache, buf->len, 0);
		btrfs_put_block_group(cache);
		trace_btrfs_reserved_extent_free(root, buf->start, buf->len);
		pin = 0;
	}
out:
	if (pin)
		add_pinned_bytes(root->fs_info, buf->len,
				 btrfs_header_level(buf),
				 root->root_key.objectid);

	/*
	 * Deleting the buffer, clear the corrupt flag since it doesn't matter
	 * anymore.
	 */
	clear_bit(EXTENT_BUFFER_CORRUPT, &buf->bflags);
}

/* Can return -ENOMEM */
int btrfs_free_extent(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      u64 bytenr, u64 num_bytes, u64 parent, u64 root_objectid,
		      u64 owner, u64 offset)
{
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;

	if (btrfs_is_testing(fs_info))
		return 0;

	add_pinned_bytes(root->fs_info, num_bytes, owner, root_objectid);

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
					BTRFS_DROP_DELAYED_REF, NULL);
	} else {
		ret = btrfs_add_delayed_data_ref(fs_info, trans, bytenr,
						num_bytes,
						parent, root_objectid, owner,
						offset, 0,
						BTRFS_DROP_DELAYED_REF, NULL);
	}
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

int get_block_group_index(struct btrfs_block_group_cache *cache)
{
	return __get_raid_index(cache->flags);
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

		down_read(&used_bg->data_rwsem);

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
static noinline int find_free_extent(struct btrfs_root *orig_root,
				u64 ram_bytes, u64 num_bytes, u64 empty_size,
				u64 hint_byte, struct btrfs_key *ins,
				u64 flags, int delalloc)
{
	int ret = 0;
	struct btrfs_root *root = orig_root->fs_info->extent_root;
	struct btrfs_free_cluster *last_ptr = NULL;
	struct btrfs_block_group_cache *block_group = NULL;
	u64 search_start = 0;
	u64 max_extent_size = 0;
	u64 empty_cluster = 0;
	struct btrfs_space_info *space_info;
	int loop = 0;
	int index = __get_raid_index(flags);
	bool failed_cluster_refill = false;
	bool failed_alloc = false;
	bool use_cluster = true;
	bool have_caching_bg = false;
	bool orig_have_caching_bg = false;
	bool full_search = false;

	WARN_ON(num_bytes < root->sectorsize);
	ins->type = BTRFS_EXTENT_ITEM_KEY;
	ins->objectid = 0;
	ins->offset = 0;

	trace_find_free_extent(orig_root, num_bytes, empty_size, flags);

	space_info = __find_space_info(root->fs_info, flags);
	if (!space_info) {
		btrfs_err(root->fs_info, "No space info for %llu", flags);
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

	last_ptr = fetch_cluster_info(orig_root, space_info, &empty_cluster);
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

	search_start = max(search_start, first_logical_byte(root, 0));
	search_start = max(search_start, hint_byte);
	if (search_start == hint_byte) {
		block_group = btrfs_lookup_block_group(root->fs_info,
						       search_start);
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
				index = get_block_group_index(block_group);
				btrfs_lock_block_group(block_group, delalloc);
				goto have_block_group;
			}
		} else if (block_group) {
			btrfs_put_block_group(block_group);
		}
	}
search:
	have_caching_bg = false;
	if (index == 0 || index == __get_raid_index(flags))
		full_search = true;
	down_read(&space_info->groups_sem);
	list_for_each_entry(block_group, &space_info->block_groups[index],
			    list) {
		u64 offset;
		int cached;

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
		if (unlikely(block_group->ro))
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
				trace_btrfs_reserve_extent_cluster(root,
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
			ret = btrfs_find_space_cluster(root, block_group,
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
		spin_lock(&block_group->free_space_ctl->tree_lock);
		if (cached &&
		    block_group->free_space_ctl->free_space <
		    num_bytes + empty_cluster + empty_size) {
			if (block_group->free_space_ctl->free_space >
			    max_extent_size)
				max_extent_size =
					block_group->free_space_ctl->free_space;
			spin_unlock(&block_group->free_space_ctl->tree_lock);
			goto loop;
		}
		spin_unlock(&block_group->free_space_ctl->tree_lock);

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
		search_start = ALIGN(offset, root->stripesize);

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

		trace_btrfs_reserve_extent(orig_root, block_group,
					   search_start, num_bytes);
		btrfs_release_block_group(block_group, delalloc);
		break;
loop:
		failed_cluster_refill = false;
		failed_alloc = false;
		BUG_ON(index != get_block_group_index(block_group));
		btrfs_release_block_group(block_group, delalloc);
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

			ret = do_chunk_alloc(trans, root, flags,
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
				btrfs_end_transaction(trans, root);
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

static void dump_space_info(struct btrfs_space_info *info, u64 bytes,
			    int dump_block_groups)
{
	struct btrfs_block_group_cache *cache;
	int index = 0;

	spin_lock(&info->lock);
	printk(KERN_INFO "BTRFS: space_info %llu has %llu free, is %sfull\n",
	       info->flags,
	       info->total_bytes - info->bytes_used - info->bytes_pinned -
	       info->bytes_reserved - info->bytes_readonly -
	       info->bytes_may_use, (info->full) ? "" : "not ");
	printk(KERN_INFO "BTRFS: space_info total=%llu, used=%llu, pinned=%llu, "
	       "reserved=%llu, may_use=%llu, readonly=%llu\n",
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
		printk(KERN_INFO "BTRFS: "
			   "block group %llu has %llu bytes, "
			   "%llu used %llu pinned %llu reserved %s\n",
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

int btrfs_reserve_extent(struct btrfs_root *root, u64 ram_bytes,
			 u64 num_bytes, u64 min_alloc_size,
			 u64 empty_size, u64 hint_byte,
			 struct btrfs_key *ins, int is_data, int delalloc)
{
	bool final_tried = num_bytes == min_alloc_size;
	u64 flags;
	int ret;

	flags = btrfs_get_alloc_profile(root, is_data);
again:
	WARN_ON(num_bytes < root->sectorsize);
	ret = find_free_extent(root, ram_bytes, num_bytes, empty_size,
			       hint_byte, ins, flags, delalloc);
	if (!ret && !is_data) {
		btrfs_dec_block_group_reservations(root->fs_info,
						   ins->objectid);
	} else if (ret == -ENOSPC) {
		if (!final_tried && ins->offset) {
			num_bytes = min(num_bytes >> 1, ins->offset);
			num_bytes = round_down(num_bytes, root->sectorsize);
			num_bytes = max(num_bytes, min_alloc_size);
			ram_bytes = num_bytes;
			if (num_bytes == min_alloc_size)
				final_tried = true;
			goto again;
		} else if (btrfs_test_opt(root->fs_info, ENOSPC_DEBUG)) {
			struct btrfs_space_info *sinfo;

			sinfo = __find_space_info(root->fs_info, flags);
			btrfs_err(root->fs_info, "allocation failed flags %llu, wanted %llu",
				flags, num_bytes);
			if (sinfo)
				dump_space_info(sinfo, num_bytes, 1);
		}
	}

	return ret;
}

static int __btrfs_free_reserved_extent(struct btrfs_root *root,
					u64 start, u64 len,
					int pin, int delalloc)
{
	struct btrfs_block_group_cache *cache;
	int ret = 0;

	cache = btrfs_lookup_block_group(root->fs_info, start);
	if (!cache) {
		btrfs_err(root->fs_info, "Unable to find block group for %llu",
			start);
		return -ENOSPC;
	}

	if (pin)
		pin_down_extent(root, cache, start, len, 1);
	else {
		if (btrfs_test_opt(root->fs_info, DISCARD))
			ret = btrfs_discard_extent(root, start, len, NULL);
		btrfs_add_free_space(cache, start, len);
		btrfs_free_reserved_bytes(cache, len, delalloc);
		trace_btrfs_reserved_extent_free(root, start, len);
	}

	btrfs_put_block_group(cache);
	return ret;
}

int btrfs_free_reserved_extent(struct btrfs_root *root,
			       u64 start, u64 len, int delalloc)
{
	return __btrfs_free_reserved_extent(root, start, len, 0, delalloc);
}

int btrfs_free_and_pin_reserved_extent(struct btrfs_root *root,
				       u64 start, u64 len)
{
	return __btrfs_free_reserved_extent(root, start, len, 1, 0);
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

	ret = remove_from_free_space_tree(trans, fs_info, ins->objectid,
					  ins->offset);
	if (ret)
		return ret;

	ret = update_block_group(trans, root, ins->objectid, ins->offset, 1);
	if (ret) { /* -ENOENT, logic error */
		btrfs_err(fs_info, "update block group failed for %llu %llu",
			ins->objectid, ins->offset);
		BUG();
	}
	trace_btrfs_reserved_extent_alloc(root, ins->objectid, ins->offset);
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
	u64 num_bytes = ins->offset;
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

	if (!skinny_metadata)
		size += sizeof(*block_info);

	path = btrfs_alloc_path();
	if (!path) {
		btrfs_free_and_pin_reserved_extent(root, ins->objectid,
						   root->nodesize);
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	if (ret) {
		btrfs_free_path(path);
		btrfs_free_and_pin_reserved_extent(root, ins->objectid,
						   root->nodesize);
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
		num_bytes = root->nodesize;
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

	ret = update_block_group(trans, root, ins->objectid, root->nodesize,
				 1);
	if (ret) { /* -ENOENT, logic error */
		btrfs_err(fs_info, "update block group failed for %llu %llu",
			ins->objectid, ins->offset);
		BUG();
	}

	trace_btrfs_reserved_extent_alloc(root, ins->objectid, root->nodesize);
	return ret;
}

int btrfs_alloc_reserved_file_extent(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     u64 root_objectid, u64 owner,
				     u64 offset, u64 ram_bytes,
				     struct btrfs_key *ins)
{
	int ret;

	BUG_ON(root_objectid == BTRFS_TREE_LOG_OBJECTID);

	ret = btrfs_add_delayed_data_ref(root->fs_info, trans, ins->objectid,
					 ins->offset, 0,
					 root_objectid, owner, offset,
					 ram_bytes, BTRFS_ADD_DELAYED_EXTENT,
					 NULL);
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

	/*
	 * Mixed block groups will exclude before processing the log so we only
	 * need to do the exclude dance if this fs isn't mixed.
	 */
	if (!btrfs_fs_incompat(root->fs_info, MIXED_GROUPS)) {
		ret = __exclude_logged_extent(root, ins->objectid, ins->offset);
		if (ret)
			return ret;
	}

	block_group = btrfs_lookup_block_group(root->fs_info, ins->objectid);
	if (!block_group)
		return -EINVAL;

	ret = btrfs_add_reserved_bytes(block_group, ins->offset,
				       ins->offset, 0);
	BUG_ON(ret); /* logic error */
	ret = alloc_reserved_file_extent(trans, root, 0, root_objectid,
					 0, owner, offset, ins, 1);
	btrfs_put_block_group(block_group);
	return ret;
}

static struct extent_buffer *
btrfs_init_new_buffer(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      u64 bytenr, int level)
{
	struct extent_buffer *buf;

	buf = btrfs_find_create_tree_block(root, bytenr);
	if (IS_ERR(buf))
		return buf;

	btrfs_set_header_generation(buf, trans->transid);
	btrfs_set_buffer_lockdep_class(root->root_key.objectid, buf, level);
	btrfs_tree_lock(buf);
	clean_tree_block(trans, root->fs_info, buf);
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
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *global_rsv = &root->fs_info->global_block_rsv;
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
		update_global_block_rsv(root->fs_info);
		goto again;
	}

	if (btrfs_test_opt(root->fs_info, ENOSPC_DEBUG)) {
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
					struct btrfs_disk_key *key, int level,
					u64 hint, u64 empty_size)
{
	struct btrfs_key ins;
	struct btrfs_block_rsv *block_rsv;
	struct extent_buffer *buf;
	struct btrfs_delayed_extent_op *extent_op;
	u64 flags = 0;
	int ret;
	u32 blocksize = root->nodesize;
	bool skinny_metadata = btrfs_fs_incompat(root->fs_info,
						 SKINNY_METADATA);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
	if (btrfs_is_testing(root->fs_info)) {
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

		ret = btrfs_add_delayed_tree_ref(root->fs_info, trans,
						 ins.objectid, ins.offset,
						 parent, root_objectid, level,
						 BTRFS_ADD_DELAYED_EXTENT,
						 extent_op);
		if (ret)
			goto out_free_delayed;
	}
	return buf;

out_free_delayed:
	btrfs_free_delayed_extent_op(extent_op);
out_free_buf:
	free_extent_buffer(buf);
out_free_reserved:
	btrfs_free_reserved_extent(root, ins.objectid, ins.offset, 0);
out_unuse:
	unuse_block_rsv(root->fs_info, block_rsv, blocksize);
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
	blocksize = root->nodesize;

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
		readahead_tree_block(root, bytenr);
		nread++;
	}
	wc->reada_slot = slot;
}

static int account_leaf_items(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      struct extent_buffer *eb)
{
	int nr = btrfs_header_nritems(eb);
	int i, extent_type, ret;
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	u64 bytenr, num_bytes;

	/* We can be called directly from walk_up_proc() */
	if (!root->fs_info->quota_enabled)
		return 0;

	for (i = 0; i < nr; i++) {
		btrfs_item_key_to_cpu(eb, &key, i);

		if (key.type != BTRFS_EXTENT_DATA_KEY)
			continue;

		fi = btrfs_item_ptr(eb, i, struct btrfs_file_extent_item);
		/* filter out non qgroup-accountable extents  */
		extent_type = btrfs_file_extent_type(eb, fi);

		if (extent_type == BTRFS_FILE_EXTENT_INLINE)
			continue;

		bytenr = btrfs_file_extent_disk_bytenr(eb, fi);
		if (!bytenr)
			continue;

		num_bytes = btrfs_file_extent_disk_num_bytes(eb, fi);

		ret = btrfs_qgroup_insert_dirty_extent(trans, root->fs_info,
				bytenr, num_bytes, GFP_NOFS);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Walk up the tree from the bottom, freeing leaves and any interior
 * nodes which have had all slots visited. If a node (leaf or
 * interior) is freed, the node above it will have it's slot
 * incremented. The root node will never be freed.
 *
 * At the end of this function, we should have a path which has all
 * slots incremented to the next position for a search. If we need to
 * read a new node it will be NULL and the node above it will have the
 * correct slot selected for a later read.
 *
 * If we increment the root nodes slot counter past the number of
 * elements, 1 is returned to signal completion of the search.
 */
static int adjust_slots_upwards(struct btrfs_root *root,
				struct btrfs_path *path, int root_level)
{
	int level = 0;
	int nr, slot;
	struct extent_buffer *eb;

	if (root_level == 0)
		return 1;

	while (level <= root_level) {
		eb = path->nodes[level];
		nr = btrfs_header_nritems(eb);
		path->slots[level]++;
		slot = path->slots[level];
		if (slot >= nr || level == 0) {
			/*
			 * Don't free the root -  we will detect this
			 * condition after our loop and return a
			 * positive value for caller to stop walking the tree.
			 */
			if (level != root_level) {
				btrfs_tree_unlock_rw(eb, path->locks[level]);
				path->locks[level] = 0;

				free_extent_buffer(eb);
				path->nodes[level] = NULL;
				path->slots[level] = 0;
			}
		} else {
			/*
			 * We have a valid slot to walk back down
			 * from. Stop here so caller can process these
			 * new nodes.
			 */
			break;
		}

		level++;
	}

	eb = path->nodes[root_level];
	if (path->slots[root_level] >= btrfs_header_nritems(eb))
		return 1;

	return 0;
}

/*
 * root_eb is the subtree root and is locked before this function is called.
 */
static int account_shared_subtree(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct extent_buffer *root_eb,
				  u64 root_gen,
				  int root_level)
{
	int ret = 0;
	int level;
	struct extent_buffer *eb = root_eb;
	struct btrfs_path *path = NULL;

	BUG_ON(root_level < 0 || root_level > BTRFS_MAX_LEVEL);
	BUG_ON(root_eb == NULL);

	if (!root->fs_info->quota_enabled)
		return 0;

	if (!extent_buffer_uptodate(root_eb)) {
		ret = btrfs_read_buffer(root_eb, root_gen);
		if (ret)
			goto out;
	}

	if (root_level == 0) {
		ret = account_leaf_items(trans, root, root_eb);
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * Walk down the tree.  Missing extent blocks are filled in as
	 * we go. Metadata is accounted every time we read a new
	 * extent block.
	 *
	 * When we reach a leaf, we account for file extent items in it,
	 * walk back up the tree (adjusting slot pointers as we go)
	 * and restart the search process.
	 */
	extent_buffer_get(root_eb); /* For path */
	path->nodes[root_level] = root_eb;
	path->slots[root_level] = 0;
	path->locks[root_level] = 0; /* so release_path doesn't try to unlock */
walk_down:
	level = root_level;
	while (level >= 0) {
		if (path->nodes[level] == NULL) {
			int parent_slot;
			u64 child_gen;
			u64 child_bytenr;

			/* We need to get child blockptr/gen from
			 * parent before we can read it. */
			eb = path->nodes[level + 1];
			parent_slot = path->slots[level + 1];
			child_bytenr = btrfs_node_blockptr(eb, parent_slot);
			child_gen = btrfs_node_ptr_generation(eb, parent_slot);

			eb = read_tree_block(root, child_bytenr, child_gen);
			if (IS_ERR(eb)) {
				ret = PTR_ERR(eb);
				goto out;
			} else if (!extent_buffer_uptodate(eb)) {
				free_extent_buffer(eb);
				ret = -EIO;
				goto out;
			}

			path->nodes[level] = eb;
			path->slots[level] = 0;

			btrfs_tree_read_lock(eb);
			btrfs_set_lock_blocking_rw(eb, BTRFS_READ_LOCK);
			path->locks[level] = BTRFS_READ_LOCK_BLOCKING;

			ret = btrfs_qgroup_insert_dirty_extent(trans,
					root->fs_info, child_bytenr,
					root->nodesize, GFP_NOFS);
			if (ret)
				goto out;
		}

		if (level == 0) {
			ret = account_leaf_items(trans, root, path->nodes[level]);
			if (ret)
				goto out;

			/* Nonzero return here means we completed our search */
			ret = adjust_slots_upwards(root, path, root_level);
			if (ret)
				break;

			/* Restart search with new slots */
			goto walk_down;
		}

		level--;
	}

	ret = 0;
out:
	btrfs_free_path(path);

	return ret;
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
		ret = btrfs_inc_ref(trans, root, eb, 1);
		BUG_ON(ret); /* -ENOMEM */
		ret = btrfs_dec_ref(trans, root, eb, 0);
		BUG_ON(ret); /* -ENOMEM */
		ret = btrfs_set_disk_extent_flags(trans, root, eb->start,
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
	u64 bytenr;
	u64 generation;
	u64 parent;
	u32 blocksize;
	struct btrfs_key key;
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
	blocksize = root->nodesize;

	next = btrfs_find_tree_block(root->fs_info, bytenr);
	if (!next) {
		next = btrfs_find_create_tree_block(root, bytenr);
		if (IS_ERR(next))
			return PTR_ERR(next);

		btrfs_set_buffer_lockdep_class(root->root_key.objectid, next,
					       level - 1);
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
		next = read_tree_block(root, bytenr, generation);
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

		if (need_account) {
			ret = account_shared_subtree(trans, root, next,
						     generation, level - 1);
			if (ret) {
				btrfs_err_rl(root->fs_info,
					"Error "
					"%d accounting shared subtree. Quota "
					"is out of sync, rescan required.",
					ret);
			}
		}
		ret = btrfs_free_extent(trans, root, bytenr, blocksize, parent,
				root->root_key.objectid, level - 1, 0);
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
				ret = btrfs_dec_ref(trans, root, eb, 1);
			else
				ret = btrfs_dec_ref(trans, root, eb, 0);
			BUG_ON(ret); /* -ENOMEM */
			ret = account_leaf_items(trans, root, eb);
			if (ret) {
				btrfs_err_rl(root->fs_info,
					"error "
					"%d accounting leaf items. Quota "
					"is out of sync, rescan required.",
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
		clean_tree_block(trans, root->fs_info, eb);
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
	bool root_dropped = false;

	btrfs_debug(root->fs_info, "Drop subvolume %llu", root->objectid);

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
	wc->reada_count = BTRFS_NODEPTRS_PER_BLOCK(root);

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
		if (btrfs_should_end_transaction(trans, tree_root) ||
		    (!for_reloc && btrfs_need_cleaner_sleep(root))) {
			ret = btrfs_update_root(trans, tree_root,
						&root->root_key,
						root_item);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				err = ret;
				goto out_end_trans;
			}

			btrfs_end_transaction_throttle(trans, tree_root);
			if (!for_reloc && btrfs_need_cleaner_sleep(root)) {
				pr_debug("BTRFS: drop snapshot early exit\n");
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

	ret = btrfs_del_root(trans, tree_root, &root->root_key);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
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
	btrfs_end_transaction_throttle(trans, tree_root);
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
	if (!for_reloc && root_dropped == false)
		btrfs_add_dead_root(root);
	if (err && err != -EAGAIN)
		btrfs_handle_fs_error(root->fs_info, err, NULL);
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

	num_devices = root->fs_info->fs_devices->rw_devices;

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

	if (sinfo->bytes_used + sinfo->bytes_reserved + sinfo->bytes_pinned +
	    sinfo->bytes_may_use + sinfo->bytes_readonly + num_bytes +
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

int btrfs_inc_block_group_ro(struct btrfs_root *root,
			     struct btrfs_block_group_cache *cache)

{
	struct btrfs_trans_handle *trans;
	u64 alloc_flags;
	int ret;

again:
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	/*
	 * we're not allowed to set block groups readonly after the dirty
	 * block groups cache has started writing.  If it already started,
	 * back off and let this transaction commit
	 */
	mutex_lock(&root->fs_info->ro_block_group_mutex);
	if (test_bit(BTRFS_TRANS_DIRTY_BG_RUN, &trans->transaction->flags)) {
		u64 transid = trans->transid;

		mutex_unlock(&root->fs_info->ro_block_group_mutex);
		btrfs_end_transaction(trans, root);

		ret = btrfs_wait_for_commit(root, transid);
		if (ret)
			return ret;
		goto again;
	}

	/*
	 * if we are changing raid levels, try to allocate a corresponding
	 * block group with the new raid level.
	 */
	alloc_flags = update_block_group_flags(root, cache->flags);
	if (alloc_flags != cache->flags) {
		ret = do_chunk_alloc(trans, root, alloc_flags,
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
	alloc_flags = get_alloc_profile(root, cache->space_info->flags);
	ret = do_chunk_alloc(trans, root, alloc_flags,
			     CHUNK_ALLOC_FORCE);
	if (ret < 0)
		goto out;
	ret = inc_block_group_ro(cache, 0);
out:
	if (cache->flags & BTRFS_BLOCK_GROUP_SYSTEM) {
		alloc_flags = update_block_group_flags(root, cache->flags);
		lock_chunks(root->fs_info->chunk_root);
		check_system_chunk(trans, root, alloc_flags);
		unlock_chunks(root->fs_info->chunk_root);
	}
	mutex_unlock(&root->fs_info->ro_block_group_mutex);

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

void btrfs_dec_block_group_ro(struct btrfs_root *root,
			      struct btrfs_block_group_cache *cache)
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
int btrfs_can_relocate(struct btrfs_root *root, u64 bytenr)
{
	struct btrfs_block_group_cache *block_group;
	struct btrfs_space_info *space_info;
	struct btrfs_fs_devices *fs_devices = root->fs_info->fs_devices;
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

	debug = btrfs_test_opt(root->fs_info, ENOSPC_DEBUG);

	block_group = btrfs_lookup_block_group(root->fs_info, bytenr);

	/* odd, couldn't find the block group, leave it alone */
	if (!block_group) {
		if (debug)
			btrfs_warn(root->fs_info,
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
		if (full) {
			if (debug)
				btrfs_warn(root->fs_info,
					"no space to alloc new chunk for block group %llu",
					block_group->key.objectid);
			goto out;
		}

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
		min_free = div64_u64(min_free, dev_min);
	}

	/* We need to do this so that we can look at pending chunks */
	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
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
		btrfs_warn(root->fs_info,
			"no space to allocate a new chunk for block group %llu",
			block_group->key.objectid);
	mutex_unlock(&root->fs_info->chunk_mutex);
	btrfs_end_transaction(trans, root);
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
			struct extent_map_tree *em_tree;
			struct extent_map *em;

			em_tree = &root->fs_info->mapping_tree.map_tree;
			read_lock(&em_tree->lock);
			em = lookup_extent_mapping(em_tree, found_key.objectid,
						   found_key.offset);
			read_unlock(&em_tree->lock);
			if (!em) {
				btrfs_err(root->fs_info,
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
		ASSERT(block_group->io_ctl.inode == NULL);
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

		if (block_group->cached == BTRFS_CACHE_STARTED)
			wait_block_group_cache_done(block_group);

		/*
		 * We haven't cached this block group, which means we could
		 * possibly have excluded extents on this block group.
		 */
		if (block_group->cached == BTRFS_CACHE_NO ||
		    block_group->cached == BTRFS_CACHE_ERROR)
			free_excluded_extents(info->extent_root, block_group);

		btrfs_remove_free_space_cache(block_group);
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
			dump_space_info(space_info, 0, 0);
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

static void __link_block_group(struct btrfs_space_info *space_info,
			       struct btrfs_block_group_cache *cache)
{
	int index = get_block_group_index(cache);
	bool first = false;

	down_write(&space_info->groups_sem);
	if (list_empty(&space_info->block_groups[index]))
		first = true;
	list_add_tail(&cache->list, &space_info->block_groups[index]);
	up_write(&space_info->groups_sem);

	if (first) {
		struct raid_kobject *rkobj;
		int ret;

		rkobj = kzalloc(sizeof(*rkobj), GFP_NOFS);
		if (!rkobj)
			goto out_err;
		rkobj->raid_type = index;
		kobject_init(&rkobj->kobj, &btrfs_raid_ktype);
		ret = kobject_add(&rkobj->kobj, &space_info->kobj,
				  "%s", get_raid_name(index));
		if (ret) {
			kobject_put(&rkobj->kobj);
			goto out_err;
		}
		space_info->block_group_kobjs[index] = &rkobj->kobj;
	}

	return;
out_err:
	pr_warn("BTRFS: failed to add kobject for block cache. ignoring.\n");
}

static struct btrfs_block_group_cache *
btrfs_create_block_group_cache(struct btrfs_root *root, u64 start, u64 size)
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

	cache->sectorsize = root->sectorsize;
	cache->fs_info = root->fs_info;
	cache->full_stripe_len = btrfs_full_stripe_len(root,
					       &root->fs_info->mapping_tree,
					       start);
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

	return cache;
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
	key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_FORWARD;

	cache_gen = btrfs_super_cache_generation(root->fs_info->super_copy);
	if (btrfs_test_opt(root->fs_info, SPACE_CACHE) &&
	    btrfs_super_generation(root->fs_info->super_copy) != cache_gen)
		need_clear = 1;
	if (btrfs_test_opt(root->fs_info, CLEAR_CACHE))
		need_clear = 1;

	while (1) {
		ret = find_first_block_group(root, path, &key);
		if (ret > 0)
			break;
		if (ret != 0)
			goto error;

		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);

		cache = btrfs_create_block_group_cache(root, found_key.objectid,
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
			if (btrfs_test_opt(root->fs_info, SPACE_CACHE))
				cache->disk_cache_state = BTRFS_DC_CLEAR;
		}

		read_extent_buffer(leaf, &cache->item,
				   btrfs_item_ptr_offset(leaf, path->slots[0]),
				   sizeof(cache->item));
		cache->flags = btrfs_block_group_flags(&cache->item);

		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(path);

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

		ret = btrfs_add_block_group_cache(root->fs_info, cache);
		if (ret) {
			btrfs_remove_free_space_cache(cache);
			btrfs_put_block_group(cache);
			goto error;
		}

		trace_btrfs_add_block_group(root->fs_info, cache, 0);
		ret = update_space_info(info, cache->flags, found_key.offset,
					btrfs_block_group_used(&cache->item),
					cache->bytes_super, &space_info);
		if (ret) {
			btrfs_remove_free_space_cache(cache);
			spin_lock(&info->block_group_cache_lock);
			rb_erase(&cache->cache_node,
				 &info->block_group_cache_tree);
			RB_CLEAR_NODE(&cache->cache_node);
			spin_unlock(&info->block_group_cache_lock);
			btrfs_put_block_group(cache);
			goto error;
		}

		cache->space_info = space_info;

		__link_block_group(space_info, cache);

		set_avail_alloc_bits(root->fs_info, cache->flags);
		if (btrfs_chunk_readonly(root, cache->key.objectid)) {
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
		list_for_each_entry(cache,
				&space_info->block_groups[BTRFS_RAID_RAID0],
				list)
			inc_block_group_ro(cache, 1);
		list_for_each_entry(cache,
				&space_info->block_groups[BTRFS_RAID_SINGLE],
				list)
			inc_block_group_ro(cache, 1);
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
		ret = btrfs_finish_chunk_alloc(trans, extent_root,
					       key.objectid, key.offset);
		if (ret)
			btrfs_abort_transaction(trans, ret);
		add_block_group_free_space(trans, root->fs_info, block_group);
		/* already aborted the transaction if it failed. */
next:
		list_del_init(&block_group->bg_list);
	}
	trans->can_flush_pending_bgs = can_flush_pending_bgs;
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

	btrfs_set_log_full_commit(root->fs_info, trans);

	cache = btrfs_create_block_group_cache(root, chunk_offset, size);
	if (!cache)
		return -ENOMEM;

	btrfs_set_block_group_used(&cache->item, bytes_used);
	btrfs_set_block_group_chunk_objectid(&cache->item, chunk_objectid);
	btrfs_set_block_group_flags(&cache->item, type);

	cache->flags = type;
	cache->last_byte_to_unpin = (u64)-1;
	cache->cached = BTRFS_CACHE_FINISHED;
	cache->needs_free_space = 1;
	ret = exclude_super_stripes(root, cache);
	if (ret) {
		/*
		 * We may have excluded something, so call this just in
		 * case.
		 */
		free_excluded_extents(root, cache);
		btrfs_put_block_group(cache);
		return ret;
	}

	add_new_free_space(cache, root->fs_info, chunk_offset,
			   chunk_offset + size);

	free_excluded_extents(root, cache);

#ifdef CONFIG_BTRFS_DEBUG
	if (btrfs_should_fragment_free_space(root, cache)) {
		u64 new_bytes_used = size - bytes_used;

		bytes_used += new_bytes_used >> 1;
		fragment_free_space(root, cache);
	}
#endif
	/*
	 * Call to ensure the corresponding space_info object is created and
	 * assigned to our block group, but don't update its counters just yet.
	 * We want our bg to be added to the rbtree with its ->space_info set.
	 */
	ret = update_space_info(root->fs_info, cache->flags, 0, 0, 0,
				&cache->space_info);
	if (ret) {
		btrfs_remove_free_space_cache(cache);
		btrfs_put_block_group(cache);
		return ret;
	}

	ret = btrfs_add_block_group_cache(root->fs_info, cache);
	if (ret) {
		btrfs_remove_free_space_cache(cache);
		btrfs_put_block_group(cache);
		return ret;
	}

	/*
	 * Now that our block group has its ->space_info set and is inserted in
	 * the rbtree, update the space info's counters.
	 */
	trace_btrfs_add_block_group(root->fs_info, cache, 1);
	ret = update_space_info(root->fs_info, cache->flags, size, bytes_used,
				cache->bytes_super, &cache->space_info);
	if (ret) {
		btrfs_remove_free_space_cache(cache);
		spin_lock(&root->fs_info->block_group_cache_lock);
		rb_erase(&cache->cache_node,
			 &root->fs_info->block_group_cache_tree);
		RB_CLEAR_NODE(&cache->cache_node);
		spin_unlock(&root->fs_info->block_group_cache_lock);
		btrfs_put_block_group(cache);
		return ret;
	}
	update_global_block_rsv(root->fs_info);

	__link_block_group(cache->space_info, cache);

	list_add_tail(&cache->bg_list, &trans->new_bgs);

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
			     struct btrfs_root *root, u64 group_start,
			     struct extent_map *em)
{
	struct btrfs_path *path;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_free_cluster *cluster;
	struct btrfs_root *tree_root = root->fs_info->tree_root;
	struct btrfs_key key;
	struct inode *inode;
	struct kobject *kobj = NULL;
	int ret;
	int index;
	int factor;
	struct btrfs_caching_control *caching_ctl = NULL;
	bool remove_em;

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

	/*
	 * get the inode first so any iput calls done for the io_list
	 * aren't the final iput (no unlinks allowed now)
	 */
	inode = lookup_free_space_inode(tree_root, block_group, path);

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
		btrfs_wait_cache_io(root, trans, block_group,
				    &block_group->io_ctl, path,
				    block_group->key.objectid);
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
	RB_CLEAR_NODE(&block_group->cache_node);

	if (root->fs_info->first_logical_byte == block_group->key.objectid)
		root->fs_info->first_logical_byte = (u64)-1;
	spin_unlock(&root->fs_info->block_group_cache_lock);

	down_write(&block_group->space_info->groups_sem);
	/*
	 * we must use list_del_init so people can check to see if they
	 * are still on the list after taking the semaphore
	 */
	list_del_init(&block_group->list);
	if (list_empty(&block_group->space_info->block_groups[index])) {
		kobj = block_group->space_info->block_group_kobjs[index];
		block_group->space_info->block_group_kobjs[index] = NULL;
		clear_avail_alloc_bits(root->fs_info, block_group->flags);
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
		down_write(&root->fs_info->commit_root_sem);
		if (!caching_ctl) {
			struct btrfs_caching_control *ctl;

			list_for_each_entry(ctl,
				    &root->fs_info->caching_block_groups, list)
				if (ctl->block_group == block_group) {
					caching_ctl = ctl;
					atomic_inc(&caching_ctl->count);
					break;
				}
		}
		if (caching_ctl)
			list_del_init(&caching_ctl->list);
		up_write(&root->fs_info->commit_root_sem);
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

	if (btrfs_test_opt(root->fs_info, ENOSPC_DEBUG)) {
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

	lock_chunks(root);
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
		list_move_tail(&em->list, &root->fs_info->pinned_chunks);
	}
	spin_unlock(&block_group->lock);

	if (remove_em) {
		struct extent_map_tree *em_tree;

		em_tree = &root->fs_info->mapping_tree.map_tree;
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

	unlock_chunks(root);

	ret = remove_block_group_free_space(trans, root->fs_info, block_group);
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
	struct btrfs_root *root = fs_info->extent_root;
	struct btrfs_trans_handle *trans;
	int ret = 0;

	if (!fs_info->open)
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
			btrfs_dec_block_group_ro(root, block_group);
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
			btrfs_dec_block_group_ro(root, block_group);
			goto end_trans;
		}
		ret = clear_extent_bits(&fs_info->freed_extents[1], start, end,
				  EXTENT_DIRTY);
		if (ret) {
			mutex_unlock(&fs_info->unused_bg_unpin_mutex);
			btrfs_dec_block_group_ro(root, block_group);
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
		trimming = btrfs_test_opt(root->fs_info, DISCARD);

		/* Implicit trim during transaction commit. */
		if (trimming)
			btrfs_get_block_group_trimming(block_group);

		/*
		 * Btrfs_remove_chunk will abort the transaction if things go
		 * horribly wrong.
		 */
		ret = btrfs_remove_chunk(trans, root,
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
		btrfs_end_transaction(trans, root);
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
	ret = update_space_info(fs_info, flags, 0, 0, 0, &space_info);
	if (ret)
		goto out;

	if (mixed) {
		flags = BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA;
		ret = update_space_info(fs_info, flags, 0, 0, 0, &space_info);
	} else {
		flags = BTRFS_BLOCK_GROUP_METADATA;
		ret = update_space_info(fs_info, flags, 0, 0, 0, &space_info);
		if (ret)
			goto out;

		flags = BTRFS_BLOCK_GROUP_DATA;
		ret = update_space_info(fs_info, flags, 0, 0, 0, &space_info);
	}
out:
	return ret;
}

int btrfs_error_unpin_extent_range(struct btrfs_root *root, u64 start, u64 end)
{
	return unpin_extent_range(root, start, end, false);
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
	if (!device->writeable)
		return 0;

	/* No free space = nothing to do. */
	if (device->total_bytes <= device->bytes_used)
		return 0;

	ret = 0;

	while (1) {
		struct btrfs_fs_info *fs_info = device->dev_root->fs_info;
		struct btrfs_transaction *trans;
		u64 bytes;

		ret = mutex_lock_interruptible(&fs_info->chunk_mutex);
		if (ret)
			return ret;

		down_read(&fs_info->commit_root_sem);

		spin_lock(&fs_info->trans_lock);
		trans = fs_info->running_transaction;
		if (trans)
			atomic_inc(&trans->use_count);
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

int btrfs_trim_fs(struct btrfs_root *root, struct fstrim_range *range)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
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

		cache = next_block_group(fs_info->tree_root, cache);
	}

	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	devices = &root->fs_info->fs_devices->alloc_list;
	list_for_each_entry(device, devices, dev_alloc_list) {
		ret = btrfs_trim_free_extents(device, range->minlen,
					      &group_trimmed);
		if (ret)
			break;

		trimmed += group_trimmed;
	}
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	range->len = trimmed;
	return ret;
}

/*
 * btrfs_{start,end}_write_no_snapshoting() are similar to
 * mnt_{want,drop}_write(), they are used to prevent some tasks from writing
 * data into the page cache through nocow before the subvolume is snapshoted,
 * but flush the data into disk after the snapshot creation, or to prevent
 * operations while snapshoting is ongoing and that cause the snapshot to be
 * inconsistent (writes followed by expanding truncates for example).
 */
void btrfs_end_write_no_snapshoting(struct btrfs_root *root)
{
	percpu_counter_dec(&root->subv_writers->counter);
	/*
	 * Make sure counter is updated before we wake up waiters.
	 */
	smp_mb();
	if (waitqueue_active(&root->subv_writers->wait))
		wake_up(&root->subv_writers->wait);
}

int btrfs_start_write_no_snapshoting(struct btrfs_root *root)
{
	if (atomic_read(&root->will_be_snapshoted))
		return 0;

	percpu_counter_inc(&root->subv_writers->counter);
	/*
	 * Make sure counter is updated before we check for snapshot creation.
	 */
	smp_mb();
	if (atomic_read(&root->will_be_snapshoted)) {
		btrfs_end_write_no_snapshoting(root);
		return 0;
	}
	return 1;
}

static int wait_snapshoting_atomic_t(atomic_t *a)
{
	schedule();
	return 0;
}

void btrfs_wait_for_snapshot_creation(struct btrfs_root *root)
{
	while (true) {
		int ret;

		ret = btrfs_start_write_no_snapshoting(root);
		if (ret)
			break;
		wait_on_atomic_t(&root->will_be_snapshoted,
				 wait_snapshoting_atomic_t,
				 TASK_UNINTERRUPTIBLE);
	}
}
