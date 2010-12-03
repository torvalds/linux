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
#include "compat.h"
#include "hash.h"
#include "ctree.h"
#include "disk-io.h"
#include "print-tree.h"
#include "transaction.h"
#include "volumes.h"
#include "locking.h"
#include "free-space-cache.h"

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
			      u64 bytenr, u64 num_bytes, int alloc);
static int update_reserved_bytes(struct btrfs_block_group_cache *cache,
				 u64 num_bytes, int reserve, int sinfo);
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
			  struct btrfs_root *extent_root, u64 alloc_bytes,
			  u64 flags, int force);
static int find_next_key(struct btrfs_path *path, int level,
			 struct btrfs_key *key);
static void dump_space_info(struct btrfs_space_info *info, u64 bytes,
			    int dump_block_groups);

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

void btrfs_get_block_group(struct btrfs_block_group_cache *cache)
{
	atomic_inc(&cache->count);
}

void btrfs_put_block_group(struct btrfs_block_group_cache *cache)
{
	if (atomic_dec_and_test(&cache->count)) {
		WARN_ON(cache->pinned > 0);
		WARN_ON(cache->reserved > 0);
		WARN_ON(cache->reserved_pinned > 0);
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
	if (ret)
		btrfs_get_block_group(ret);
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
		BUG_ON(ret);
	}

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		ret = btrfs_rmap_block(&root->fs_info->mapping_tree,
				       cache->key.objectid, bytenr,
				       0, &logical, &nr, &stripe_len);
		BUG_ON(ret);

		while (nr--) {
			cache->bytes_super += stripe_len;
			ret = add_excluded_extent(root, logical[nr],
						  stripe_len);
			BUG_ON(ret);
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
					    EXTENT_DIRTY | EXTENT_UPTODATE);
		if (ret)
			break;

		if (extent_start <= start) {
			start = extent_end + 1;
		} else if (extent_start > start && extent_start < end) {
			size = extent_start - start;
			total_added += size;
			ret = btrfs_add_free_space(block_group, start,
						   size);
			BUG_ON(ret);
			start = extent_end + 1;
		} else {
			break;
		}
	}

	if (start < end) {
		size = end - start;
		total_added += size;
		ret = btrfs_add_free_space(block_group, start, size);
		BUG_ON(ret);
	}

	return total_added;
}

static int caching_kthread(void *data)
{
	struct btrfs_block_group_cache *block_group = data;
	struct btrfs_fs_info *fs_info = block_group->fs_info;
	struct btrfs_caching_control *caching_ctl = block_group->caching_ctl;
	struct btrfs_root *extent_root = fs_info->extent_root;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	u64 total_found = 0;
	u64 last = 0;
	u32 nritems;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	exclude_super_stripes(extent_root, block_group);
	spin_lock(&block_group->space_info->lock);
	block_group->space_info->bytes_readonly += block_group->bytes_super;
	spin_unlock(&block_group->space_info->lock);

	last = max_t(u64, block_group->key.objectid, BTRFS_SUPER_INFO_OFFSET);

	/*
	 * We don't want to deadlock with somebody trying to allocate a new
	 * extent for the extent root while also trying to search the extent
	 * root to add free space.  So we skip locking and search the commit
	 * root, since its read-only
	 */
	path->skip_locking = 1;
	path->search_commit_root = 1;
	path->reada = 2;

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
		smp_mb();
		if (fs_info->closing > 1) {
			last = (u64)-1;
			break;
		}

		if (path->slots[0] < nritems) {
			btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		} else {
			ret = find_next_key(path, 0, &key);
			if (ret)
				break;

			caching_ctl->progress = last;
			btrfs_release_path(extent_root, path);
			up_read(&fs_info->extent_commit_sem);
			mutex_unlock(&caching_ctl->mutex);
			if (btrfs_transaction_in_commit(fs_info))
				schedule_timeout(1);
			else
				cond_resched();
			goto again;
		}

		if (key.objectid < block_group->key.objectid) {
			path->slots[0]++;
			continue;
		}

		if (key.objectid >= block_group->key.objectid +
		    block_group->key.offset)
			break;

		if (key.type == BTRFS_EXTENT_ITEM_KEY) {
			total_found += add_new_free_space(block_group,
							  fs_info, last,
							  key.objectid);
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
	wake_up(&caching_ctl->wait);

	put_caching_control(caching_ctl);
	atomic_dec(&block_group->space_info->caching_threads);
	btrfs_put_block_group(block_group);

	return 0;
}

static int cache_block_group(struct btrfs_block_group_cache *cache,
			     struct btrfs_trans_handle *trans,
			     int load_cache_only)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;
	struct btrfs_caching_control *caching_ctl;
	struct task_struct *tsk;
	int ret = 0;

	smp_mb();
	if (cache->cached != BTRFS_CACHE_NO)
		return 0;

	/*
	 * We can't do the read from on-disk cache during a commit since we need
	 * to have the normal tree locking.
	 */
	if (!trans->transaction->in_commit) {
		spin_lock(&cache->lock);
		if (cache->cached != BTRFS_CACHE_NO) {
			spin_unlock(&cache->lock);
			return 0;
		}
		cache->cached = BTRFS_CACHE_STARTED;
		spin_unlock(&cache->lock);

		ret = load_free_space_cache(fs_info, cache);

		spin_lock(&cache->lock);
		if (ret == 1) {
			cache->cached = BTRFS_CACHE_FINISHED;
			cache->last_byte_to_unpin = (u64)-1;
		} else {
			cache->cached = BTRFS_CACHE_NO;
		}
		spin_unlock(&cache->lock);
		if (ret == 1)
			return 0;
	}

	if (load_cache_only)
		return 0;

	caching_ctl = kzalloc(sizeof(*caching_ctl), GFP_KERNEL);
	BUG_ON(!caching_ctl);

	INIT_LIST_HEAD(&caching_ctl->list);
	mutex_init(&caching_ctl->mutex);
	init_waitqueue_head(&caching_ctl->wait);
	caching_ctl->block_group = cache;
	caching_ctl->progress = cache->key.objectid;
	/* one for caching kthread, one for caching block group list */
	atomic_set(&caching_ctl->count, 2);

	spin_lock(&cache->lock);
	if (cache->cached != BTRFS_CACHE_NO) {
		spin_unlock(&cache->lock);
		kfree(caching_ctl);
		return 0;
	}
	cache->caching_ctl = caching_ctl;
	cache->cached = BTRFS_CACHE_STARTED;
	spin_unlock(&cache->lock);

	down_write(&fs_info->extent_commit_sem);
	list_add_tail(&caching_ctl->list, &fs_info->caching_block_groups);
	up_write(&fs_info->extent_commit_sem);

	atomic_inc(&cache->space_info->caching_threads);
	btrfs_get_block_group(cache);

	tsk = kthread_run(caching_kthread, cache, "btrfs-cache-%llu\n",
			  cache->key.objectid);
	if (IS_ERR(tsk)) {
		ret = PTR_ERR(tsk);
		printk(KERN_ERR "error running thread %d\n", ret);
		BUG();
	}

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

	flags &= BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_SYSTEM |
		 BTRFS_BLOCK_GROUP_METADATA;

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

static u64 div_factor(u64 num, int factor)
{
	if (factor == 10)
		return num;
	num *= factor;
	do_div(num, 10);
	return num;
}

static u64 div_factor_fine(u64 num, int factor)
{
	if (factor == 100)
		return num;
	num *= factor;
	do_div(num, 100);
	return num;
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
	BUG_ON(!path);
	key.objectid = start;
	key.offset = len;
	btrfs_set_key_type(&key, BTRFS_EXTENT_ITEM_KEY);
	ret = btrfs_search_slot(NULL, root->fs_info->extent_root, &key, path,
				0, 0);
	btrfs_free_path(path);
	return ret;
}

/*
 * helper function to lookup reference count and flags of extent.
 *
 * the head node for delayed ref is used to store the sum of all the
 * reference count modifications queued up in the rbtree. the head
 * node may also store the extent flags to set. This way you can check
 * to see what the reference count and extent flags would be if all of
 * the delayed refs are not processed.
 */
int btrfs_lookup_extent_info(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 bytenr,
			     u64 num_bytes, u64 *refs, u64 *flags)
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

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;
	if (!trans) {
		path->skip_locking = 1;
		path->search_commit_root = 1;
	}
again:
	ret = btrfs_search_slot(trans, root->fs_info->extent_root,
				&key, path, 0, 0);
	if (ret < 0)
		goto out_free;

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

			btrfs_release_path(root->fs_info->extent_root, path);

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
				BUG_ON(ret > 0);
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
	btrfs_release_path(root, path);

	if (owner < BTRFS_FIRST_FREE_OBJECTID)
		new_size += sizeof(*bi);

	new_size -= sizeof(*ei0);
	ret = btrfs_search_slot(trans, root, &key, path,
				new_size + extra_size, 1);
	if (ret < 0)
		return ret;
	BUG_ON(ret);

	ret = btrfs_extend_item(trans, root, path, new_size);
	BUG_ON(ret);

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
		btrfs_release_path(root, path);
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
				btrfs_release_path(root, path);
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
			btrfs_release_path(root, path);
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
	btrfs_release_path(root, path);
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
		btrfs_release_path(root, path);
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
	btrfs_release_path(root, path);
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

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	want = extent_ref_type(parent, owner);
	if (insert) {
		extra_size = btrfs_extent_inline_ref_size(want);
		path->keep_locks = 1;
	} else
		extra_size = -1;
	ret = btrfs_search_slot(trans, root, &key, path, extra_size, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	BUG_ON(ret);

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

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		ptr += sizeof(struct btrfs_tree_block_info);
		BUG_ON(ptr > end);
	} else {
		BUG_ON(!(flags & BTRFS_EXTENT_FLAG_DATA));
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
int setup_inline_extent_backref(struct btrfs_trans_handle *trans,
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
	int ret;

	leaf = path->nodes[0];
	ei = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	item_offset = (unsigned long)iref - (unsigned long)ei;

	type = extent_ref_type(parent, owner);
	size = btrfs_extent_inline_ref_size(type);

	ret = btrfs_extend_item(trans, root, path, size);
	BUG_ON(ret);

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
	return 0;
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

	btrfs_release_path(root, path);
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
int update_inline_extent_backref(struct btrfs_trans_handle *trans,
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
	int ret;
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
		ret = btrfs_truncate_item(trans, root, path, item_size, 1);
		BUG_ON(ret);
	}
	btrfs_mark_buffer_dirty(leaf);
	return 0;
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
		ret = update_inline_extent_backref(trans, root, path, iref,
						   refs_to_add, extent_op);
	} else if (ret == -ENOENT) {
		ret = setup_inline_extent_backref(trans, root, path, iref,
						  parent, root_objectid,
						  owner, offset, refs_to_add,
						  extent_op);
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
	int ret;

	BUG_ON(!is_data && refs_to_drop != 1);
	if (iref) {
		ret = update_inline_extent_backref(trans, root, path, iref,
						   -refs_to_drop, NULL);
	} else if (is_data) {
		ret = remove_extent_data_ref(trans, root, path, refs_to_drop);
	} else {
		ret = btrfs_del_item(trans, root, path);
	}
	return ret;
}

static void btrfs_issue_discard(struct block_device *bdev,
				u64 start, u64 len)
{
	blkdev_issue_discard(bdev, start >> 9, len >> 9, GFP_KERNEL,
			BLKDEV_IFL_WAIT | BLKDEV_IFL_BARRIER);
}

static int btrfs_discard_extent(struct btrfs_root *root, u64 bytenr,
				u64 num_bytes)
{
	int ret;
	u64 map_length = num_bytes;
	struct btrfs_multi_bio *multi = NULL;

	if (!btrfs_test_opt(root, DISCARD))
		return 0;

	/* Tell the block device(s) that the sectors can be discarded */
	ret = btrfs_map_block(&root->fs_info->mapping_tree, READ,
			      bytenr, &map_length, &multi, 0);
	if (!ret) {
		struct btrfs_bio_stripe *stripe = multi->stripes;
		int i;

		if (map_length > num_bytes)
			map_length = num_bytes;

		for (i = 0; i < multi->num_stripes; i++, stripe++) {
			btrfs_issue_discard(stripe->dev->bdev,
					    stripe->physical,
					    map_length);
		}
		kfree(multi);
	}

	return ret;
}

int btrfs_inc_extent_ref(struct btrfs_trans_handle *trans,
			 struct btrfs_root *root,
			 u64 bytenr, u64 num_bytes, u64 parent,
			 u64 root_objectid, u64 owner, u64 offset)
{
	int ret;
	BUG_ON(owner < BTRFS_FIRST_FREE_OBJECTID &&
	       root_objectid == BTRFS_TREE_LOG_OBJECTID);

	if (owner < BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(trans, bytenr, num_bytes,
					parent, root_objectid, (int)owner,
					BTRFS_ADD_DELAYED_REF, NULL);
	} else {
		ret = btrfs_add_delayed_data_ref(trans, bytenr, num_bytes,
					parent, root_objectid, owner, offset,
					BTRFS_ADD_DELAYED_REF, NULL);
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
	btrfs_release_path(root->fs_info->extent_root, path);

	path->reada = 1;
	path->leave_spinning = 1;

	/* now insert the actual backref */
	ret = insert_extent_backref(trans, root->fs_info->extent_root,
				    path, bytenr, parent, root_objectid,
				    owner, offset, refs_to_add);
	BUG_ON(ret);
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
		if (extent_op) {
			BUG_ON(extent_op->update_key);
			flags |= extent_op->flags_to_set;
		}
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

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = node->bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = node->num_bytes;

	path->reada = 1;
	path->leave_spinning = 1;
	ret = btrfs_search_slot(trans, root->fs_info->extent_root, &key,
				path, 0, 1);
	if (ret < 0) {
		err = ret;
		goto out;
	}
	if (ret > 0) {
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

	ins.objectid = node->bytenr;
	ins.offset = node->num_bytes;
	ins.type = BTRFS_EXTENT_ITEM_KEY;

	ref = btrfs_delayed_node_to_tree_ref(node);
	if (node->type == BTRFS_SHARED_BLOCK_REF_KEY)
		parent = ref->parent;
	else
		ref_root = ref->root;

	BUG_ON(node->ref_mod != 1);
	if (node->action == BTRFS_ADD_DELAYED_REF && insert_reserved) {
		BUG_ON(!extent_op || !extent_op->update_flags ||
		       !extent_op->update_key);
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
	int ret;
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
				BUG_ON(ret);
			}
		}
		mutex_unlock(&head->mutex);
		return 0;
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

static noinline int run_clustered_refs(struct btrfs_trans_handle *trans,
				       struct btrfs_root *root,
				       struct list_head *cluster)
{
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct btrfs_delayed_ref_head *locked_ref = NULL;
	struct btrfs_delayed_extent_op *extent_op;
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
		 * record the must insert reserved flag before we
		 * drop the spin lock.
		 */
		must_insert_reserved = locked_ref->must_insert_reserved;
		locked_ref->must_insert_reserved = 0;

		extent_op = locked_ref->extent_op;
		locked_ref->extent_op = NULL;

		/*
		 * locked_ref is the head node, so we have to go one
		 * node back for any delayed ref updates
		 */
		ref = select_delayed_ref(locked_ref);
		if (!ref) {
			/* All delayed refs have been processed, Go ahead
			 * and send the head node to run_one_delayed_ref,
			 * so that any accounting fixes can happen
			 */
			ref = &locked_ref->node;

			if (extent_op && must_insert_reserved) {
				kfree(extent_op);
				extent_op = NULL;
			}

			if (extent_op) {
				spin_unlock(&delayed_refs->lock);

				ret = run_delayed_extent_op(trans, root,
							    ref, extent_op);
				BUG_ON(ret);
				kfree(extent_op);

				cond_resched();
				spin_lock(&delayed_refs->lock);
				continue;
			}

			list_del_init(&locked_ref->cluster);
			locked_ref = NULL;
		}

		ref->in_tree = 0;
		rb_erase(&ref->rb_node, &delayed_refs->root);
		delayed_refs->num_entries--;

		spin_unlock(&delayed_refs->lock);

		ret = run_one_delayed_ref(trans, root, ref, extent_op,
					  must_insert_reserved);
		BUG_ON(ret);

		btrfs_put_delayed_ref(ref);
		kfree(extent_op);
		count++;

		cond_resched();
		spin_lock(&delayed_refs->lock);
	}
	return count;
}

/*
 * this starts processing the delayed reference count updates and
 * extent insertions we have queued up so far.  count can be
 * 0, which means to process everything in the tree at the start
 * of the run (but not newly added entries), or it can be some target
 * number you'd like to process.
 */
int btrfs_run_delayed_refs(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, unsigned long count)
{
	struct rb_node *node;
	struct btrfs_delayed_ref_root *delayed_refs;
	struct btrfs_delayed_ref_node *ref;
	struct list_head cluster;
	int ret;
	int run_all = count == (unsigned long)-1;
	int run_most = 0;

	if (root == root->fs_info->extent_root)
		root = root->fs_info->tree_root;

	delayed_refs = &trans->transaction->delayed_refs;
	INIT_LIST_HEAD(&cluster);
again:
	spin_lock(&delayed_refs->lock);
	if (count == 0) {
		count = delayed_refs->num_entries * 2;
		run_most = 1;
	}
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
		ret = btrfs_find_ref_cluster(trans, &cluster,
					     delayed_refs->run_delayed_start);
		if (ret)
			break;

		ret = run_clustered_refs(trans, root, &cluster);
		BUG_ON(ret < 0);

		count -= min_t(unsigned long, ret, count);

		if (count == 0)
			break;
	}

	if (run_all) {
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
	spin_unlock(&delayed_refs->lock);
	return 0;
}

int btrfs_set_disk_extent_flags(struct btrfs_trans_handle *trans,
				struct btrfs_root *root,
				u64 bytenr, u64 num_bytes, u64 flags,
				int is_data)
{
	struct btrfs_delayed_extent_op *extent_op;
	int ret;

	extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
	if (!extent_op)
		return -ENOMEM;

	extent_op->flags_to_set = flags;
	extent_op->update_flags = 1;
	extent_op->update_key = 0;
	extent_op->is_data = is_data ? 1 : 0;

	ret = btrfs_add_delayed_extent_op(trans, bytenr, num_bytes, extent_op);
	if (ret)
		kfree(extent_op);
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

		btrfs_release_path(root->fs_info->extent_root, path);

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
		ref = rb_entry(node, struct btrfs_delayed_ref_node, rb_node);
		if (ref->bytenr == bytenr)
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
	BUG_ON(ret == 0);

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

#if 0
int btrfs_cache_ref(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		    struct extent_buffer *buf, u32 nr_extents)
{
	struct btrfs_key key;
	struct btrfs_file_extent_item *fi;
	u64 root_gen;
	u32 nritems;
	int i;
	int level;
	int ret = 0;
	int shared = 0;

	if (!root->ref_cows)
		return 0;

	if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
		shared = 0;
		root_gen = root->root_key.offset;
	} else {
		shared = 1;
		root_gen = trans->transid - 1;
	}

	level = btrfs_header_level(buf);
	nritems = btrfs_header_nritems(buf);

	if (level == 0) {
		struct btrfs_leaf_ref *ref;
		struct btrfs_extent_info *info;

		ref = btrfs_alloc_leaf_ref(root, nr_extents);
		if (!ref) {
			ret = -ENOMEM;
			goto out;
		}

		ref->root_gen = root_gen;
		ref->bytenr = buf->start;
		ref->owner = btrfs_header_owner(buf);
		ref->generation = btrfs_header_generation(buf);
		ref->nritems = nr_extents;
		info = ref->extents;

		for (i = 0; nr_extents > 0 && i < nritems; i++) {
			u64 disk_bytenr;
			btrfs_item_key_to_cpu(buf, &key, i);
			if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
				continue;
			fi = btrfs_item_ptr(buf, i,
					    struct btrfs_file_extent_item);
			if (btrfs_file_extent_type(buf, fi) ==
			    BTRFS_FILE_EXTENT_INLINE)
				continue;
			disk_bytenr = btrfs_file_extent_disk_bytenr(buf, fi);
			if (disk_bytenr == 0)
				continue;

			info->bytenr = disk_bytenr;
			info->num_bytes =
				btrfs_file_extent_disk_num_bytes(buf, fi);
			info->objectid = key.objectid;
			info->offset = key.offset;
			info++;
		}

		ret = btrfs_add_leaf_ref(root, ref, shared);
		if (ret == -EEXIST && shared) {
			struct btrfs_leaf_ref *old;
			old = btrfs_lookup_leaf_ref(root, ref->bytenr);
			BUG_ON(!old);
			btrfs_remove_leaf_ref(root, old);
			btrfs_free_leaf_ref(root, old);
			ret = btrfs_add_leaf_ref(root, ref, shared);
		}
		WARN_ON(ret);
		btrfs_free_leaf_ref(root, ref);
	}
out:
	return ret;
}

/* when a block goes through cow, we update the reference counts of
 * everything that block points to.  The internal pointers of the block
 * can be in just about any order, and it is likely to have clusters of
 * things that are close together and clusters of things that are not.
 *
 * To help reduce the seeks that come with updating all of these reference
 * counts, sort them by byte number before actual updates are done.
 *
 * struct refsort is used to match byte number to slot in the btree block.
 * we sort based on the byte number and then use the slot to actually
 * find the item.
 *
 * struct refsort is smaller than strcut btrfs_item and smaller than
 * struct btrfs_key_ptr.  Since we're currently limited to the page size
 * for a btree block, there's no way for a kmalloc of refsorts for a
 * single node to be bigger than a page.
 */
struct refsort {
	u64 bytenr;
	u32 slot;
};

/*
 * for passing into sort()
 */
static int refsort_cmp(const void *a_void, const void *b_void)
{
	const struct refsort *a = a_void;
	const struct refsort *b = b_void;

	if (a->bytenr < b->bytenr)
		return -1;
	if (a->bytenr > b->bytenr)
		return 1;
	return 0;
}
#endif

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
					   key.offset);
			if (ret)
				goto fail;
		} else {
			bytenr = btrfs_node_blockptr(buf, i);
			num_bytes = btrfs_level_size(root, level - 1);
			ret = process_func(trans, root, bytenr, num_bytes,
					   parent, ref_root, level - 1, 0);
			if (ret)
				goto fail;
		}
	}
	return 0;
fail:
	BUG();
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
	if (ret < 0)
		goto fail;
	BUG_ON(ret);

	leaf = path->nodes[0];
	bi = btrfs_item_ptr_offset(leaf, path->slots[0]);
	write_extent_buffer(leaf, &cache->item, bi, sizeof(cache->item));
	btrfs_mark_buffer_dirty(leaf);
	btrfs_release_path(extent_root, path);
fail:
	if (ret)
		return ret;
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
		btrfs_release_path(root, path);
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
	if (block_group->cached != BTRFS_CACHE_FINISHED) {
		/* We're not cached, don't bother trying to write stuff out */
		dcs = BTRFS_DC_WRITTEN;
		spin_unlock(&block_group->lock);
		goto out_put;
	}
	spin_unlock(&block_group->lock);

	num_pages = (int)div64_u64(block_group->key.offset, 1024 * 1024 * 1024);
	if (!num_pages)
		num_pages = 1;

	/*
	 * Just to make absolutely sure we have enough space, we're going to
	 * preallocate 12 pages worth of space for each block group.  In
	 * practice we ought to use at most 8, but we need extra space so we can
	 * add our header and have a terminator between the extents and the
	 * bitmaps.
	 */
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
	btrfs_release_path(root, path);
out:
	spin_lock(&block_group->lock);
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
			BUG_ON(err);
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
		BUG_ON(err);
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
			BUG_ON(err);
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

		btrfs_write_out_cache(root, trans, cache, path);

		/*
		 * If we didn't have an error then the cache state is still
		 * NEED_WRITE, so we can set it to WRITTEN.
		 */
		if (cache->disk_cache_state == BTRFS_DC_NEED_WRITE)
			cache->disk_cache_state = BTRFS_DC_WRITTEN;
		last = cache->key.objectid + cache->key.offset;
		btrfs_put_block_group(cache);
	}

	btrfs_free_path(path);
	return 0;
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
	found->flags = flags & (BTRFS_BLOCK_GROUP_DATA |
				BTRFS_BLOCK_GROUP_SYSTEM |
				BTRFS_BLOCK_GROUP_METADATA);
	found->total_bytes = total_bytes;
	found->disk_total = total_bytes * factor;
	found->bytes_used = bytes_used;
	found->disk_used = bytes_used * factor;
	found->bytes_pinned = 0;
	found->bytes_reserved = 0;
	found->bytes_readonly = 0;
	found->bytes_may_use = 0;
	found->full = 0;
	found->force_alloc = 0;
	*space_info = found;
	list_add_rcu(&found->list, &info->space_info);
	atomic_set(&found->caching_threads, 0);
	return 0;
}

static void set_avail_alloc_bits(struct btrfs_fs_info *fs_info, u64 flags)
{
	u64 extra_flags = flags & (BTRFS_BLOCK_GROUP_RAID0 |
				   BTRFS_BLOCK_GROUP_RAID1 |
				   BTRFS_BLOCK_GROUP_RAID10 |
				   BTRFS_BLOCK_GROUP_DUP);
	if (extra_flags) {
		if (flags & BTRFS_BLOCK_GROUP_DATA)
			fs_info->avail_data_alloc_bits |= extra_flags;
		if (flags & BTRFS_BLOCK_GROUP_METADATA)
			fs_info->avail_metadata_alloc_bits |= extra_flags;
		if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
			fs_info->avail_system_alloc_bits |= extra_flags;
	}
}

u64 btrfs_reduce_alloc_profile(struct btrfs_root *root, u64 flags)
{
	u64 num_devices = root->fs_info->fs_devices->rw_devices;

	if (num_devices == 1)
		flags &= ~(BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID0);
	if (num_devices < 4)
		flags &= ~BTRFS_BLOCK_GROUP_RAID10;

	if ((flags & BTRFS_BLOCK_GROUP_DUP) &&
	    (flags & (BTRFS_BLOCK_GROUP_RAID1 |
		      BTRFS_BLOCK_GROUP_RAID10))) {
		flags &= ~BTRFS_BLOCK_GROUP_DUP;
	}

	if ((flags & BTRFS_BLOCK_GROUP_RAID1) &&
	    (flags & BTRFS_BLOCK_GROUP_RAID10)) {
		flags &= ~BTRFS_BLOCK_GROUP_RAID1;
	}

	if ((flags & BTRFS_BLOCK_GROUP_RAID0) &&
	    ((flags & BTRFS_BLOCK_GROUP_RAID1) |
	     (flags & BTRFS_BLOCK_GROUP_RAID10) |
	     (flags & BTRFS_BLOCK_GROUP_DUP)))
		flags &= ~BTRFS_BLOCK_GROUP_RAID0;
	return flags;
}

static u64 get_alloc_profile(struct btrfs_root *root, u64 flags)
{
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		flags |= root->fs_info->avail_data_alloc_bits &
			 root->fs_info->data_alloc_profile;
	else if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
		flags |= root->fs_info->avail_system_alloc_bits &
			 root->fs_info->system_alloc_profile;
	else if (flags & BTRFS_BLOCK_GROUP_METADATA)
		flags |= root->fs_info->avail_metadata_alloc_bits &
			 root->fs_info->metadata_alloc_profile;
	return btrfs_reduce_alloc_profile(root, flags);
}

static u64 btrfs_get_alloc_profile(struct btrfs_root *root, int data)
{
	u64 flags;

	if (data)
		flags = BTRFS_BLOCK_GROUP_DATA;
	else if (root == root->fs_info->chunk_root)
		flags = BTRFS_BLOCK_GROUP_SYSTEM;
	else
		flags = BTRFS_BLOCK_GROUP_METADATA;

	return get_alloc_profile(root, flags);
}

void btrfs_set_inode_space_info(struct btrfs_root *root, struct inode *inode)
{
	BTRFS_I(inode)->space_info = __find_space_info(root->fs_info,
						       BTRFS_BLOCK_GROUP_DATA);
}

/*
 * This will check the space that the inode allocates from to make sure we have
 * enough space for bytes.
 */
int btrfs_check_data_free_space(struct inode *inode, u64 bytes)
{
	struct btrfs_space_info *data_sinfo;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 used;
	int ret = 0, committed = 0, alloc_chunk = 1;

	/* make sure bytes are sectorsize aligned */
	bytes = (bytes + root->sectorsize - 1) & ~((u64)root->sectorsize - 1);

	if (root == root->fs_info->tree_root) {
		alloc_chunk = 0;
		committed = 1;
	}

	data_sinfo = BTRFS_I(inode)->space_info;
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

			data_sinfo->force_alloc = 1;
			spin_unlock(&data_sinfo->lock);
alloc:
			alloc_target = btrfs_get_alloc_profile(root, 1);
			trans = btrfs_join_transaction(root, 1);
			if (IS_ERR(trans))
				return PTR_ERR(trans);

			ret = do_chunk_alloc(trans, root->fs_info->extent_root,
					     bytes + 2 * 1024 * 1024,
					     alloc_target, 0);
			btrfs_end_transaction(trans, root);
			if (ret < 0)
				return ret;

			if (!data_sinfo) {
				btrfs_set_inode_space_info(root, inode);
				data_sinfo = BTRFS_I(inode)->space_info;
			}
			goto again;
		}
		spin_unlock(&data_sinfo->lock);

		/* commit the current transaction and try again */
		if (!committed && !root->fs_info->open_ioctl_trans) {
			committed = 1;
			trans = btrfs_join_transaction(root, 1);
			if (IS_ERR(trans))
				return PTR_ERR(trans);
			ret = btrfs_commit_transaction(trans, root);
			if (ret)
				return ret;
			goto again;
		}

#if 0 /* I hope we never need this code again, just in case */
		printk(KERN_ERR "no space left, need %llu, %llu bytes_used, "
		       "%llu bytes_reserved, " "%llu bytes_pinned, "
		       "%llu bytes_readonly, %llu may use %llu total\n",
		       (unsigned long long)bytes,
		       (unsigned long long)data_sinfo->bytes_used,
		       (unsigned long long)data_sinfo->bytes_reserved,
		       (unsigned long long)data_sinfo->bytes_pinned,
		       (unsigned long long)data_sinfo->bytes_readonly,
		       (unsigned long long)data_sinfo->bytes_may_use,
		       (unsigned long long)data_sinfo->total_bytes);
#endif
		return -ENOSPC;
	}
	data_sinfo->bytes_may_use += bytes;
	BTRFS_I(inode)->reserved_bytes += bytes;
	spin_unlock(&data_sinfo->lock);

	return 0;
}

/*
 * called when we are clearing an delalloc extent from the
 * inode's io_tree or there was an error for whatever reason
 * after calling btrfs_check_data_free_space
 */
void btrfs_free_reserved_data_space(struct inode *inode, u64 bytes)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_space_info *data_sinfo;

	/* make sure bytes are sectorsize aligned */
	bytes = (bytes + root->sectorsize - 1) & ~((u64)root->sectorsize - 1);

	data_sinfo = BTRFS_I(inode)->space_info;
	spin_lock(&data_sinfo->lock);
	data_sinfo->bytes_may_use -= bytes;
	BTRFS_I(inode)->reserved_bytes -= bytes;
	spin_unlock(&data_sinfo->lock);
}

static void force_metadata_allocation(struct btrfs_fs_info *info)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	rcu_read_lock();
	list_for_each_entry_rcu(found, head, list) {
		if (found->flags & BTRFS_BLOCK_GROUP_METADATA)
			found->force_alloc = 1;
	}
	rcu_read_unlock();
}

static int should_alloc_chunk(struct btrfs_root *root,
			      struct btrfs_space_info *sinfo, u64 alloc_bytes)
{
	u64 num_bytes = sinfo->total_bytes - sinfo->bytes_readonly;
	u64 thresh;

	if (sinfo->bytes_used + sinfo->bytes_reserved +
	    alloc_bytes + 256 * 1024 * 1024 < num_bytes)
		return 0;

	if (sinfo->bytes_used + sinfo->bytes_reserved +
	    alloc_bytes < div_factor(num_bytes, 8))
		return 0;

	thresh = btrfs_super_total_bytes(&root->fs_info->super_copy);
	thresh = max_t(u64, 256 * 1024 * 1024, div_factor_fine(thresh, 5));

	if (num_bytes > thresh && sinfo->bytes_used < div_factor(num_bytes, 3))
		return 0;

	return 1;
}

static int do_chunk_alloc(struct btrfs_trans_handle *trans,
			  struct btrfs_root *extent_root, u64 alloc_bytes,
			  u64 flags, int force)
{
	struct btrfs_space_info *space_info;
	struct btrfs_fs_info *fs_info = extent_root->fs_info;
	int ret = 0;

	mutex_lock(&fs_info->chunk_mutex);

	flags = btrfs_reduce_alloc_profile(extent_root, flags);

	space_info = __find_space_info(extent_root->fs_info, flags);
	if (!space_info) {
		ret = update_space_info(extent_root->fs_info, flags,
					0, 0, &space_info);
		BUG_ON(ret);
	}
	BUG_ON(!space_info);

	spin_lock(&space_info->lock);
	if (space_info->force_alloc)
		force = 1;
	if (space_info->full) {
		spin_unlock(&space_info->lock);
		goto out;
	}

	if (!force && !should_alloc_chunk(extent_root, space_info,
					  alloc_bytes)) {
		spin_unlock(&space_info->lock);
		goto out;
	}
	spin_unlock(&space_info->lock);

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

	ret = btrfs_alloc_chunk(trans, extent_root, flags);
	spin_lock(&space_info->lock);
	if (ret)
		space_info->full = 1;
	else
		ret = 1;
	space_info->force_alloc = 0;
	spin_unlock(&space_info->lock);
out:
	mutex_unlock(&extent_root->fs_info->chunk_mutex);
	return ret;
}

/*
 * shrink metadata reservation for delalloc
 */
static int shrink_delalloc(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, u64 to_reclaim, int sync)
{
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_space_info *space_info;
	u64 reserved;
	u64 max_reclaim;
	u64 reclaimed = 0;
	int pause = 1;
	int nr_pages = (2 * 1024 * 1024) >> PAGE_CACHE_SHIFT;

	block_rsv = &root->fs_info->delalloc_block_rsv;
	space_info = block_rsv->space_info;

	smp_mb();
	reserved = space_info->bytes_reserved;

	if (reserved == 0)
		return 0;

	max_reclaim = min(reserved, to_reclaim);

	while (1) {
		/* have the flusher threads jump in and do some IO */
		smp_mb();
		nr_pages = min_t(unsigned long, nr_pages,
		       root->fs_info->delalloc_bytes >> PAGE_CACHE_SHIFT);
		writeback_inodes_sb_nr_if_idle(root->fs_info->sb, nr_pages);

		spin_lock(&space_info->lock);
		if (reserved > space_info->bytes_reserved)
			reclaimed += reserved - space_info->bytes_reserved;
		reserved = space_info->bytes_reserved;
		spin_unlock(&space_info->lock);

		if (reserved == 0 || reclaimed >= max_reclaim)
			break;

		if (trans && trans->transaction->blocked)
			return -EAGAIN;

		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(pause);
		pause <<= 1;
		if (pause > HZ / 10)
			pause = HZ / 10;

	}
	return reclaimed >= to_reclaim;
}

/*
 * Retries tells us how many times we've called reserve_metadata_bytes.  The
 * idea is if this is the first call (retries == 0) then we will add to our
 * reserved count if we can't make the allocation in order to hold our place
 * while we go and try and free up space.  That way for retries > 1 we don't try
 * and add space, we just check to see if the amount of unused space is >= the
 * total space, meaning that our reservation is valid.
 *
 * However if we don't intend to retry this reservation, pass -1 as retries so
 * that it short circuits this logic.
 */
static int reserve_metadata_bytes(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  struct btrfs_block_rsv *block_rsv,
				  u64 orig_bytes, int flush)
{
	struct btrfs_space_info *space_info = block_rsv->space_info;
	u64 unused;
	u64 num_bytes = orig_bytes;
	int retries = 0;
	int ret = 0;
	bool reserved = false;
	bool committed = false;

again:
	ret = -ENOSPC;
	if (reserved)
		num_bytes = 0;

	spin_lock(&space_info->lock);
	unused = space_info->bytes_used + space_info->bytes_reserved +
		 space_info->bytes_pinned + space_info->bytes_readonly +
		 space_info->bytes_may_use;

	/*
	 * The idea here is that we've not already over-reserved the block group
	 * then we can go ahead and save our reservation first and then start
	 * flushing if we need to.  Otherwise if we've already overcommitted
	 * lets start flushing stuff first and then come back and try to make
	 * our reservation.
	 */
	if (unused <= space_info->total_bytes) {
		unused = space_info->total_bytes - unused;
		if (unused >= num_bytes) {
			if (!reserved)
				space_info->bytes_reserved += orig_bytes;
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
		num_bytes = unused - space_info->total_bytes +
			(orig_bytes * (retries + 1));
	}

	/*
	 * Couldn't make our reservation, save our place so while we're trying
	 * to reclaim space we can actually use it instead of somebody else
	 * stealing it from us.
	 */
	if (ret && !reserved) {
		space_info->bytes_reserved += orig_bytes;
		reserved = true;
	}

	spin_unlock(&space_info->lock);

	if (!ret)
		return 0;

	if (!flush)
		goto out;

	/*
	 * We do synchronous shrinking since we don't actually unreserve
	 * metadata until after the IO is completed.
	 */
	ret = shrink_delalloc(trans, root, num_bytes, 1);
	if (ret > 0)
		return 0;
	else if (ret < 0)
		goto out;

	/*
	 * So if we were overcommitted it's possible that somebody else flushed
	 * out enough space and we simply didn't have enough space to reclaim,
	 * so go back around and try again.
	 */
	if (retries < 2) {
		retries++;
		goto again;
	}

	spin_lock(&space_info->lock);
	/*
	 * Not enough space to be reclaimed, don't bother committing the
	 * transaction.
	 */
	if (space_info->bytes_pinned < orig_bytes)
		ret = -ENOSPC;
	spin_unlock(&space_info->lock);
	if (ret)
		goto out;

	ret = -EAGAIN;
	if (trans || committed)
		goto out;

	ret = -ENOSPC;
	trans = btrfs_join_transaction(root, 1);
	if (IS_ERR(trans))
		goto out;
	ret = btrfs_commit_transaction(trans, root);
	if (!ret) {
		trans = NULL;
		committed = true;
		goto again;
	}

out:
	if (reserved) {
		spin_lock(&space_info->lock);
		space_info->bytes_reserved -= orig_bytes;
		spin_unlock(&space_info->lock);
	}

	return ret;
}

static struct btrfs_block_rsv *get_block_rsv(struct btrfs_trans_handle *trans,
					     struct btrfs_root *root)
{
	struct btrfs_block_rsv *block_rsv;
	if (root->ref_cows)
		block_rsv = trans->block_rsv;
	else
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

void block_rsv_release_bytes(struct btrfs_block_rsv *block_rsv,
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
			block_rsv_add_bytes(dest, num_bytes, 0);
		} else {
			spin_lock(&space_info->lock);
			space_info->bytes_reserved -= num_bytes;
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

void btrfs_init_block_rsv(struct btrfs_block_rsv *rsv)
{
	memset(rsv, 0, sizeof(*rsv));
	spin_lock_init(&rsv->lock);
	atomic_set(&rsv->usage, 1);
	rsv->priority = 6;
	INIT_LIST_HEAD(&rsv->list);
}

struct btrfs_block_rsv *btrfs_alloc_block_rsv(struct btrfs_root *root)
{
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_fs_info *fs_info = root->fs_info;

	block_rsv = kmalloc(sizeof(*block_rsv), GFP_NOFS);
	if (!block_rsv)
		return NULL;

	btrfs_init_block_rsv(block_rsv);
	block_rsv->space_info = __find_space_info(fs_info,
						  BTRFS_BLOCK_GROUP_METADATA);
	return block_rsv;
}

void btrfs_free_block_rsv(struct btrfs_root *root,
			  struct btrfs_block_rsv *rsv)
{
	if (rsv && atomic_dec_and_test(&rsv->usage)) {
		btrfs_block_rsv_release(root, rsv, (u64)-1);
		if (!rsv->durable)
			kfree(rsv);
	}
}

/*
 * make the block_rsv struct be able to capture freed space.
 * the captured space will re-add to the the block_rsv struct
 * after transaction commit
 */
void btrfs_add_durable_block_rsv(struct btrfs_fs_info *fs_info,
				 struct btrfs_block_rsv *block_rsv)
{
	block_rsv->durable = 1;
	mutex_lock(&fs_info->durable_block_rsv_mutex);
	list_add_tail(&block_rsv->list, &fs_info->durable_block_rsv_list);
	mutex_unlock(&fs_info->durable_block_rsv_mutex);
}

int btrfs_block_rsv_add(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct btrfs_block_rsv *block_rsv,
			u64 num_bytes)
{
	int ret;

	if (num_bytes == 0)
		return 0;

	ret = reserve_metadata_bytes(trans, root, block_rsv, num_bytes, 1);
	if (!ret) {
		block_rsv_add_bytes(block_rsv, num_bytes, 1);
		return 0;
	}

	return ret;
}

int btrfs_block_rsv_check(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_block_rsv *block_rsv,
			  u64 min_reserved, int min_factor)
{
	u64 num_bytes = 0;
	int commit_trans = 0;
	int ret = -ENOSPC;

	if (!block_rsv)
		return 0;

	spin_lock(&block_rsv->lock);
	if (min_factor > 0)
		num_bytes = div_factor(block_rsv->size, min_factor);
	if (min_reserved > num_bytes)
		num_bytes = min_reserved;

	if (block_rsv->reserved >= num_bytes) {
		ret = 0;
	} else {
		num_bytes -= block_rsv->reserved;
		if (block_rsv->durable &&
		    block_rsv->freed[0] + block_rsv->freed[1] >= num_bytes)
			commit_trans = 1;
	}
	spin_unlock(&block_rsv->lock);
	if (!ret)
		return 0;

	if (block_rsv->refill_used) {
		ret = reserve_metadata_bytes(trans, root, block_rsv,
					     num_bytes, 0);
		if (!ret) {
			block_rsv_add_bytes(block_rsv, num_bytes, 0);
			return 0;
		}
	}

	if (commit_trans) {
		if (trans)
			return -EAGAIN;

		trans = btrfs_join_transaction(root, 1);
		BUG_ON(IS_ERR(trans));
		ret = btrfs_commit_transaction(trans, root);
		return 0;
	}

	WARN_ON(1);
	printk(KERN_INFO"block_rsv size %llu reserved %llu freed %llu %llu\n",
		block_rsv->size, block_rsv->reserved,
		block_rsv->freed[0], block_rsv->freed[1]);

	return -ENOSPC;
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
	block_rsv_release_bytes(block_rsv, global_rsv, num_bytes);
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
	int csum_size = btrfs_super_csum_size(&fs_info->super_copy);
#if 0
	/*
	 * per tree used space accounting can be inaccuracy, so we
	 * can't rely on it.
	 */
	spin_lock(&fs_info->extent_root->accounting_lock);
	num_bytes = btrfs_root_used(&fs_info->extent_root->root_item);
	spin_unlock(&fs_info->extent_root->accounting_lock);

	spin_lock(&fs_info->csum_root->accounting_lock);
	num_bytes += btrfs_root_used(&fs_info->csum_root->root_item);
	spin_unlock(&fs_info->csum_root->accounting_lock);

	spin_lock(&fs_info->tree_root->accounting_lock);
	num_bytes += btrfs_root_used(&fs_info->tree_root->root_item);
	spin_unlock(&fs_info->tree_root->accounting_lock);
#endif
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

	spin_lock(&block_rsv->lock);
	spin_lock(&sinfo->lock);

	block_rsv->size = num_bytes;

	num_bytes = sinfo->bytes_used + sinfo->bytes_pinned +
		    sinfo->bytes_reserved + sinfo->bytes_readonly +
		    sinfo->bytes_may_use;

	if (sinfo->total_bytes > num_bytes) {
		num_bytes = sinfo->total_bytes - num_bytes;
		block_rsv->reserved += num_bytes;
		sinfo->bytes_reserved += num_bytes;
	}

	if (block_rsv->reserved >= block_rsv->size) {
		num_bytes = block_rsv->reserved - block_rsv->size;
		sinfo->bytes_reserved -= num_bytes;
		block_rsv->reserved = block_rsv->size;
		block_rsv->full = 1;
	}
#if 0
	printk(KERN_INFO"global block rsv size %llu reserved %llu\n",
		block_rsv->size, block_rsv->reserved);
#endif
	spin_unlock(&sinfo->lock);
	spin_unlock(&block_rsv->lock);
}

static void init_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	struct btrfs_space_info *space_info;

	space_info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_SYSTEM);
	fs_info->chunk_block_rsv.space_info = space_info;
	fs_info->chunk_block_rsv.priority = 10;

	space_info = __find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);
	fs_info->global_block_rsv.space_info = space_info;
	fs_info->global_block_rsv.priority = 10;
	fs_info->global_block_rsv.refill_used = 1;
	fs_info->delalloc_block_rsv.space_info = space_info;
	fs_info->trans_block_rsv.space_info = space_info;
	fs_info->empty_block_rsv.space_info = space_info;
	fs_info->empty_block_rsv.priority = 10;

	fs_info->extent_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->csum_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->dev_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->tree_root->block_rsv = &fs_info->global_block_rsv;
	fs_info->chunk_root->block_rsv = &fs_info->chunk_block_rsv;

	btrfs_add_durable_block_rsv(fs_info, &fs_info->global_block_rsv);

	btrfs_add_durable_block_rsv(fs_info, &fs_info->delalloc_block_rsv);

	update_global_block_rsv(fs_info);
}

static void release_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	block_rsv_release_bytes(&fs_info->global_block_rsv, NULL, (u64)-1);
	WARN_ON(fs_info->delalloc_block_rsv.size > 0);
	WARN_ON(fs_info->delalloc_block_rsv.reserved > 0);
	WARN_ON(fs_info->trans_block_rsv.size > 0);
	WARN_ON(fs_info->trans_block_rsv.reserved > 0);
	WARN_ON(fs_info->chunk_block_rsv.size > 0);
	WARN_ON(fs_info->chunk_block_rsv.reserved > 0);
}

static u64 calc_trans_metadata_size(struct btrfs_root *root, int num_items)
{
	return (root->leafsize + root->nodesize * (BTRFS_MAX_LEVEL - 1)) *
		3 * num_items;
}

int btrfs_trans_reserve_metadata(struct btrfs_trans_handle *trans,
				 struct btrfs_root *root,
				 int num_items)
{
	u64 num_bytes;
	int ret;

	if (num_items == 0 || root->fs_info->chunk_root == root)
		return 0;

	num_bytes = calc_trans_metadata_size(root, num_items);
	ret = btrfs_block_rsv_add(trans, root, &root->fs_info->trans_block_rsv,
				  num_bytes);
	if (!ret) {
		trans->bytes_reserved += num_bytes;
		trans->block_rsv = &root->fs_info->trans_block_rsv;
	}
	return ret;
}

void btrfs_trans_release_metadata(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root)
{
	if (!trans->bytes_reserved)
		return;

	BUG_ON(trans->block_rsv != &root->fs_info->trans_block_rsv);
	btrfs_block_rsv_release(root, trans->block_rsv,
				trans->bytes_reserved);
	trans->bytes_reserved = 0;
}

int btrfs_orphan_reserve_metadata(struct btrfs_trans_handle *trans,
				  struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *src_rsv = get_block_rsv(trans, root);
	struct btrfs_block_rsv *dst_rsv = root->orphan_block_rsv;

	/*
	 * one for deleting orphan item, one for updating inode and
	 * two for calling btrfs_truncate_inode_items.
	 *
	 * btrfs_truncate_inode_items is a delete operation, it frees
	 * more space than it uses in most cases. So two units of
	 * metadata space should be enough for calling it many times.
	 * If all of the metadata space is used, we can commit
	 * transaction and use space it freed.
	 */
	u64 num_bytes = calc_trans_metadata_size(root, 4);
	return block_rsv_migrate_bytes(src_rsv, dst_rsv, num_bytes);
}

void btrfs_orphan_release_metadata(struct inode *inode)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 num_bytes = calc_trans_metadata_size(root, 4);
	btrfs_block_rsv_release(root, root->orphan_block_rsv, num_bytes);
}

int btrfs_snap_reserve_metadata(struct btrfs_trans_handle *trans,
				struct btrfs_pending_snapshot *pending)
{
	struct btrfs_root *root = pending->root;
	struct btrfs_block_rsv *src_rsv = get_block_rsv(trans, root);
	struct btrfs_block_rsv *dst_rsv = &pending->block_rsv;
	/*
	 * two for root back/forward refs, two for directory entries
	 * and one for root of the snapshot.
	 */
	u64 num_bytes = calc_trans_metadata_size(root, 5);
	dst_rsv->space_info = src_rsv->space_info;
	return block_rsv_migrate_bytes(src_rsv, dst_rsv, num_bytes);
}

static u64 calc_csum_metadata_size(struct inode *inode, u64 num_bytes)
{
	return num_bytes >>= 3;
}

int btrfs_delalloc_reserve_metadata(struct inode *inode, u64 num_bytes)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_block_rsv *block_rsv = &root->fs_info->delalloc_block_rsv;
	u64 to_reserve;
	int nr_extents;
	int ret;

	if (btrfs_transaction_in_commit(root->fs_info))
		schedule_timeout(1);

	num_bytes = ALIGN(num_bytes, root->sectorsize);

	spin_lock(&BTRFS_I(inode)->accounting_lock);
	nr_extents = atomic_read(&BTRFS_I(inode)->outstanding_extents) + 1;
	if (nr_extents > BTRFS_I(inode)->reserved_extents) {
		nr_extents -= BTRFS_I(inode)->reserved_extents;
		to_reserve = calc_trans_metadata_size(root, nr_extents);
	} else {
		nr_extents = 0;
		to_reserve = 0;
	}
	spin_unlock(&BTRFS_I(inode)->accounting_lock);

	to_reserve += calc_csum_metadata_size(inode, num_bytes);
	ret = reserve_metadata_bytes(NULL, root, block_rsv, to_reserve, 1);
	if (ret)
		return ret;

	spin_lock(&BTRFS_I(inode)->accounting_lock);
	BTRFS_I(inode)->reserved_extents += nr_extents;
	atomic_inc(&BTRFS_I(inode)->outstanding_extents);
	spin_unlock(&BTRFS_I(inode)->accounting_lock);

	block_rsv_add_bytes(block_rsv, to_reserve, 1);

	if (block_rsv->size > 512 * 1024 * 1024)
		shrink_delalloc(NULL, root, to_reserve, 0);

	return 0;
}

void btrfs_delalloc_release_metadata(struct inode *inode, u64 num_bytes)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	u64 to_free;
	int nr_extents;

	num_bytes = ALIGN(num_bytes, root->sectorsize);
	atomic_dec(&BTRFS_I(inode)->outstanding_extents);

	spin_lock(&BTRFS_I(inode)->accounting_lock);
	nr_extents = atomic_read(&BTRFS_I(inode)->outstanding_extents);
	if (nr_extents < BTRFS_I(inode)->reserved_extents) {
		nr_extents = BTRFS_I(inode)->reserved_extents - nr_extents;
		BTRFS_I(inode)->reserved_extents -= nr_extents;
	} else {
		nr_extents = 0;
	}
	spin_unlock(&BTRFS_I(inode)->accounting_lock);

	to_free = calc_csum_metadata_size(inode, num_bytes);
	if (nr_extents > 0)
		to_free += calc_trans_metadata_size(root, nr_extents);

	btrfs_block_rsv_release(root, &root->fs_info->delalloc_block_rsv,
				to_free);
}

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

void btrfs_delalloc_release_space(struct inode *inode, u64 num_bytes)
{
	btrfs_delalloc_release_metadata(inode, num_bytes);
	btrfs_free_reserved_data_space(inode, num_bytes);
}

static int update_block_group(struct btrfs_trans_handle *trans,
			      struct btrfs_root *root,
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
	old_val = btrfs_super_bytes_used(&info->super_copy);
	if (alloc)
		old_val += num_bytes;
	else
		old_val -= num_bytes;
	btrfs_set_super_bytes_used(&info->super_copy, old_val);
	spin_unlock(&info->delalloc_lock);

	while (total) {
		cache = btrfs_lookup_block_group(info, bytenr);
		if (!cache)
			return -1;
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
			cache_block_group(cache, trans, 1);

		byte_in_group = bytenr - cache->key.objectid;
		WARN_ON(byte_in_group > cache->key.offset);

		spin_lock(&cache->space_info->lock);
		spin_lock(&cache->lock);

		if (btrfs_super_cache_generation(&info->super_copy) != 0 &&
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
	BUG_ON(!cache);

	pin_down_extent(root, cache, bytenr, num_bytes, reserved);

	btrfs_put_block_group(cache);
	return 0;
}

/*
 * update size of reserved extents. this function may return -EAGAIN
 * if 'reserve' is true or 'sinfo' is false.
 */
static int update_reserved_bytes(struct btrfs_block_group_cache *cache,
				 u64 num_bytes, int reserve, int sinfo)
{
	int ret = 0;
	if (sinfo) {
		struct btrfs_space_info *space_info = cache->space_info;
		spin_lock(&space_info->lock);
		spin_lock(&cache->lock);
		if (reserve) {
			if (cache->ro) {
				ret = -EAGAIN;
			} else {
				cache->reserved += num_bytes;
				space_info->bytes_reserved += num_bytes;
			}
		} else {
			if (cache->ro)
				space_info->bytes_readonly += num_bytes;
			cache->reserved -= num_bytes;
			space_info->bytes_reserved -= num_bytes;
		}
		spin_unlock(&cache->lock);
		spin_unlock(&space_info->lock);
	} else {
		spin_lock(&cache->lock);
		if (cache->ro) {
			ret = -EAGAIN;
		} else {
			if (reserve)
				cache->reserved += num_bytes;
			else
				cache->reserved -= num_bytes;
		}
		spin_unlock(&cache->lock);
	}
	return ret;
}

int btrfs_prepare_extent_commit(struct btrfs_trans_handle *trans,
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
	return 0;
}

static int unpin_extent_range(struct btrfs_root *root, u64 start, u64 end)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_group_cache *cache = NULL;
	u64 len;

	while (start <= end) {
		if (!cache ||
		    start >= cache->key.objectid + cache->key.offset) {
			if (cache)
				btrfs_put_block_group(cache);
			cache = btrfs_lookup_block_group(fs_info, start);
			BUG_ON(!cache);
		}

		len = cache->key.objectid + cache->key.offset - start;
		len = min(len, end + 1 - start);

		if (start < cache->last_byte_to_unpin) {
			len = min(len, cache->last_byte_to_unpin - start);
			btrfs_add_free_space(cache, start, len);
		}

		start += len;

		spin_lock(&cache->space_info->lock);
		spin_lock(&cache->lock);
		cache->pinned -= len;
		cache->space_info->bytes_pinned -= len;
		if (cache->ro) {
			cache->space_info->bytes_readonly += len;
		} else if (cache->reserved_pinned > 0) {
			len = min(len, cache->reserved_pinned);
			cache->reserved_pinned -= len;
			cache->space_info->bytes_reserved += len;
		}
		spin_unlock(&cache->lock);
		spin_unlock(&cache->space_info->lock);
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
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_rsv *next_rsv;
	u64 start;
	u64 end;
	int idx;
	int ret;

	if (fs_info->pinned_extents == &fs_info->freed_extents[0])
		unpin = &fs_info->freed_extents[1];
	else
		unpin = &fs_info->freed_extents[0];

	while (1) {
		ret = find_first_extent_bit(unpin, 0, &start, &end,
					    EXTENT_DIRTY);
		if (ret)
			break;

		ret = btrfs_discard_extent(root, start, end + 1 - start);

		clear_extent_dirty(unpin, start, end, GFP_NOFS);
		unpin_extent_range(root, start, end);
		cond_resched();
	}

	mutex_lock(&fs_info->durable_block_rsv_mutex);
	list_for_each_entry_safe(block_rsv, next_rsv,
				 &fs_info->durable_block_rsv_list, list) {

		idx = trans->transid & 0x1;
		if (block_rsv->freed[idx] > 0) {
			block_rsv_add_bytes(block_rsv,
					    block_rsv->freed[idx], 0);
			block_rsv->freed[idx] = 0;
		}
		if (atomic_read(&block_rsv->usage) == 0) {
			btrfs_block_rsv_release(root, block_rsv, (u64)-1);

			if (block_rsv->freed[0] == 0 &&
			    block_rsv->freed[1] == 0) {
				list_del_init(&block_rsv->list);
				kfree(block_rsv);
			}
		} else {
			btrfs_block_rsv_release(root, block_rsv, 0);
		}
	}
	mutex_unlock(&fs_info->durable_block_rsv_mutex);

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

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 1;
	path->leave_spinning = 1;

	is_data = owner_objectid >= BTRFS_FIRST_FREE_OBJECTID;
	BUG_ON(!is_data && refs_to_drop != 1);

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
			BUG_ON(ret);
			btrfs_release_path(extent_root, path);
			path->leave_spinning = 1;

			key.objectid = bytenr;
			key.type = BTRFS_EXTENT_ITEM_KEY;
			key.offset = num_bytes;

			ret = btrfs_search_slot(trans, extent_root,
						&key, path, -1, 1);
			if (ret) {
				printk(KERN_ERR "umm, got %d back from search"
				       ", was looking for %llu\n", ret,
				       (unsigned long long)bytenr);
				btrfs_print_leaf(extent_root, path->nodes[0]);
			}
			BUG_ON(ret);
			extent_slot = path->slots[0];
		}
	} else {
		btrfs_print_leaf(extent_root, path->nodes[0]);
		WARN_ON(1);
		printk(KERN_ERR "btrfs unable to find ref byte nr %llu "
		       "parent %llu root %llu  owner %llu offset %llu\n",
		       (unsigned long long)bytenr,
		       (unsigned long long)parent,
		       (unsigned long long)root_objectid,
		       (unsigned long long)owner_objectid,
		       (unsigned long long)owner_offset);
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, extent_slot);
#ifdef BTRFS_COMPAT_EXTENT_TREE_V0
	if (item_size < sizeof(*ei)) {
		BUG_ON(found_extent || extent_slot != path->slots[0]);
		ret = convert_extent_item_v0(trans, extent_root, path,
					     owner_objectid, 0);
		BUG_ON(ret < 0);

		btrfs_release_path(extent_root, path);
		path->leave_spinning = 1;

		key.objectid = bytenr;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = num_bytes;

		ret = btrfs_search_slot(trans, extent_root, &key, path,
					-1, 1);
		if (ret) {
			printk(KERN_ERR "umm, got %d back from search"
			       ", was looking for %llu\n", ret,
			       (unsigned long long)bytenr);
			btrfs_print_leaf(extent_root, path->nodes[0]);
		}
		BUG_ON(ret);
		extent_slot = path->slots[0];
		leaf = path->nodes[0];
		item_size = btrfs_item_size_nr(leaf, extent_slot);
	}
#endif
	BUG_ON(item_size < sizeof(*ei));
	ei = btrfs_item_ptr(leaf, extent_slot,
			    struct btrfs_extent_item);
	if (owner_objectid < BTRFS_FIRST_FREE_OBJECTID) {
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
			BUG_ON(ret);
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
		BUG_ON(ret);
		btrfs_release_path(extent_root, path);

		if (is_data) {
			ret = btrfs_del_csums(trans, root, bytenr, num_bytes);
			BUG_ON(ret);
		} else {
			invalidate_mapping_pages(info->btree_inode->i_mapping,
			     bytenr >> PAGE_CACHE_SHIFT,
			     (bytenr + num_bytes - 1) >> PAGE_CACHE_SHIFT);
		}

		ret = update_block_group(trans, root, bytenr, num_bytes, 0);
		BUG_ON(ret);
	}
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
		kfree(head->extent_op);
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
	struct btrfs_block_rsv *block_rsv;
	struct btrfs_block_group_cache *cache = NULL;
	int ret;

	if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
		ret = btrfs_add_delayed_tree_ref(trans, buf->start, buf->len,
						parent, root->root_key.objectid,
						btrfs_header_level(buf),
						BTRFS_DROP_DELAYED_REF, NULL);
		BUG_ON(ret);
	}

	if (!last_ref)
		return;

	block_rsv = get_block_rsv(trans, root);
	cache = btrfs_lookup_block_group(root->fs_info, buf->start);
	if (block_rsv->space_info != cache->space_info)
		goto out;

	if (btrfs_header_generation(buf) == trans->transid) {
		if (root->root_key.objectid != BTRFS_TREE_LOG_OBJECTID) {
			ret = check_ref_cleanup(trans, root, buf->start);
			if (!ret)
				goto pin;
		}

		if (btrfs_header_flag(buf, BTRFS_HEADER_FLAG_WRITTEN)) {
			pin_down_extent(root, cache, buf->start, buf->len, 1);
			goto pin;
		}

		WARN_ON(test_bit(EXTENT_BUFFER_DIRTY, &buf->bflags));

		btrfs_add_free_space(cache, buf->start, buf->len);
		ret = update_reserved_bytes(cache, buf->len, 0, 0);
		if (ret == -EAGAIN) {
			/* block group became read-only */
			update_reserved_bytes(cache, buf->len, 0, 1);
			goto out;
		}

		ret = 1;
		spin_lock(&block_rsv->lock);
		if (block_rsv->reserved < block_rsv->size) {
			block_rsv->reserved += buf->len;
			ret = 0;
		}
		spin_unlock(&block_rsv->lock);

		if (ret) {
			spin_lock(&cache->space_info->lock);
			cache->space_info->bytes_reserved -= buf->len;
			spin_unlock(&cache->space_info->lock);
		}
		goto out;
	}
pin:
	if (block_rsv->durable && !cache->ro) {
		ret = 0;
		spin_lock(&cache->lock);
		if (!cache->ro) {
			cache->reserved_pinned += buf->len;
			ret = 1;
		}
		spin_unlock(&cache->lock);

		if (ret) {
			spin_lock(&block_rsv->lock);
			block_rsv->freed[trans->transid & 0x1] += buf->len;
			spin_unlock(&block_rsv->lock);
		}
	}
out:
	btrfs_put_block_group(cache);
}

int btrfs_free_extent(struct btrfs_trans_handle *trans,
		      struct btrfs_root *root,
		      u64 bytenr, u64 num_bytes, u64 parent,
		      u64 root_objectid, u64 owner, u64 offset)
{
	int ret;

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
		ret = btrfs_add_delayed_tree_ref(trans, bytenr, num_bytes,
					parent, root_objectid, (int)owner,
					BTRFS_DROP_DELAYED_REF, NULL);
		BUG_ON(ret);
	} else {
		ret = btrfs_add_delayed_data_ref(trans, bytenr, num_bytes,
					parent, root_objectid, owner,
					offset, BTRFS_DROP_DELAYED_REF, NULL);
		BUG_ON(ret);
	}
	return ret;
}

static u64 stripe_align(struct btrfs_root *root, u64 val)
{
	u64 mask = ((u64)root->stripesize - 1);
	u64 ret = (val + mask) & ~mask;
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
	DEFINE_WAIT(wait);

	caching_ctl = get_caching_control(cache);
	if (!caching_ctl)
		return 0;

	wait_event(caching_ctl->wait, block_group_cache_done(cache) ||
		   (cache->free_space >= num_bytes));

	put_caching_control(caching_ctl);
	return 0;
}

static noinline int
wait_block_group_cache_done(struct btrfs_block_group_cache *cache)
{
	struct btrfs_caching_control *caching_ctl;
	DEFINE_WAIT(wait);

	caching_ctl = get_caching_control(cache);
	if (!caching_ctl)
		return 0;

	wait_event(caching_ctl->wait, block_group_cache_done(cache));

	put_caching_control(caching_ctl);
	return 0;
}

static int get_block_group_index(struct btrfs_block_group_cache *cache)
{
	int index;
	if (cache->flags & BTRFS_BLOCK_GROUP_RAID10)
		index = 0;
	else if (cache->flags & BTRFS_BLOCK_GROUP_RAID1)
		index = 1;
	else if (cache->flags & BTRFS_BLOCK_GROUP_DUP)
		index = 2;
	else if (cache->flags & BTRFS_BLOCK_GROUP_RAID0)
		index = 3;
	else
		index = 4;
	return index;
}

enum btrfs_loop_type {
	LOOP_FIND_IDEAL = 0,
	LOOP_CACHING_NOWAIT = 1,
	LOOP_CACHING_WAIT = 2,
	LOOP_ALLOC_CHUNK = 3,
	LOOP_NO_EMPTY_SIZE = 4,
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
				     u64 search_start, u64 search_end,
				     u64 hint_byte, struct btrfs_key *ins,
				     int data)
{
	int ret = 0;
	struct btrfs_root *root = orig_root->fs_info->extent_root;
	struct btrfs_free_cluster *last_ptr = NULL;
	struct btrfs_block_group_cache *block_group = NULL;
	int empty_cluster = 2 * 1024 * 1024;
	int allowed_chunk_alloc = 0;
	int done_chunk_alloc = 0;
	struct btrfs_space_info *space_info;
	int last_ptr_loop = 0;
	int loop = 0;
	int index = 0;
	bool found_uncached_bg = false;
	bool failed_cluster_refill = false;
	bool failed_alloc = false;
	bool use_cluster = true;
	u64 ideal_cache_percent = 0;
	u64 ideal_cache_offset = 0;

	WARN_ON(num_bytes < root->sectorsize);
	btrfs_set_key_type(ins, BTRFS_EXTENT_ITEM_KEY);
	ins->objectid = 0;
	ins->offset = 0;

	space_info = __find_space_info(root->fs_info, data);
	if (!space_info) {
		printk(KERN_ERR "No space info for %d\n", data);
		return -ENOSPC;
	}

	/*
	 * If the space info is for both data and metadata it means we have a
	 * small filesystem and we can't use the clustering stuff.
	 */
	if (btrfs_mixed_space_info(space_info))
		use_cluster = false;

	if (orig_root->ref_cows || empty_size)
		allowed_chunk_alloc = 1;

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
ideal_cache:
		block_group = btrfs_lookup_block_group(root->fs_info,
						       search_start);
		/*
		 * we don't want to use the block group if it doesn't match our
		 * allocation bits, or if its not cached.
		 *
		 * However if we are re-searching with an ideal block group
		 * picked out then we don't care that the block group is cached.
		 */
		if (block_group && block_group_bits(block_group, data) &&
		    (block_group->cached != BTRFS_CACHE_NO ||
		     search_start == ideal_cache_offset)) {
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
	down_read(&space_info->groups_sem);
	list_for_each_entry(block_group, &space_info->block_groups[index],
			    list) {
		u64 offset;
		int cached;

		btrfs_get_block_group(block_group);
		search_start = block_group->key.objectid;

have_block_group:
		if (unlikely(block_group->cached == BTRFS_CACHE_NO)) {
			u64 free_percent;

			ret = cache_block_group(block_group, trans, 1);
			if (block_group->cached == BTRFS_CACHE_FINISHED)
				goto have_block_group;

			free_percent = btrfs_block_group_used(&block_group->item);
			free_percent *= 100;
			free_percent = div64_u64(free_percent,
						 block_group->key.offset);
			free_percent = 100 - free_percent;
			if (free_percent > ideal_cache_percent &&
			    likely(!block_group->ro)) {
				ideal_cache_offset = block_group->key.objectid;
				ideal_cache_percent = free_percent;
			}

			/*
			 * We only want to start kthread caching if we are at
			 * the point where we will wait for caching to make
			 * progress, or if our ideal search is over and we've
			 * found somebody to start caching.
			 */
			if (loop > LOOP_CACHING_NOWAIT ||
			    (loop > LOOP_FIND_IDEAL &&
			     atomic_read(&space_info->caching_threads) < 2)) {
				ret = cache_block_group(block_group, trans, 0);
				BUG_ON(ret);
			}
			found_uncached_bg = true;

			/*
			 * If loop is set for cached only, try the next block
			 * group.
			 */
			if (loop == LOOP_FIND_IDEAL)
				goto loop;
		}

		cached = block_group_cache_done(block_group);
		if (unlikely(!cached))
			found_uncached_bg = true;

		if (unlikely(block_group->ro))
			goto loop;

		/*
		 * Ok we want to try and use the cluster allocator, so lets look
		 * there, unless we are on LOOP_NO_EMPTY_SIZE, since we will
		 * have tried the cluster allocator plenty of times at this
		 * point and not have found anything, so we are likely way too
		 * fragmented for the clustering stuff to find anything, so lets
		 * just skip it and let the allocator find whatever block it can
		 * find
		 */
		if (last_ptr && loop < LOOP_NO_EMPTY_SIZE) {
			/*
			 * the refill lock keeps out other
			 * people trying to start a new cluster
			 */
			spin_lock(&last_ptr->refill_lock);
			if (last_ptr->block_group &&
			    (last_ptr->block_group->ro ||
			    !block_group_bits(last_ptr->block_group, data))) {
				offset = 0;
				goto refill_cluster;
			}

			offset = btrfs_alloc_from_cluster(block_group, last_ptr,
						 num_bytes, search_start);
			if (offset) {
				/* we have a block, we're done */
				spin_unlock(&last_ptr->refill_lock);
				goto checks;
			}

			spin_lock(&last_ptr->lock);
			/*
			 * whoops, this cluster doesn't actually point to
			 * this block group.  Get a ref on the block
			 * group is does point to and try again
			 */
			if (!last_ptr_loop && last_ptr->block_group &&
			    last_ptr->block_group != block_group) {

				btrfs_put_block_group(block_group);
				block_group = last_ptr->block_group;
				btrfs_get_block_group(block_group);
				spin_unlock(&last_ptr->lock);
				spin_unlock(&last_ptr->refill_lock);

				last_ptr_loop = 1;
				search_start = block_group->key.objectid;
				/*
				 * we know this block group is properly
				 * in the list because
				 * btrfs_remove_block_group, drops the
				 * cluster before it removes the block
				 * group from the list
				 */
				goto have_block_group;
			}
			spin_unlock(&last_ptr->lock);
refill_cluster:
			/*
			 * this cluster didn't work out, free it and
			 * start over
			 */
			btrfs_return_cluster_to_free_space(NULL, last_ptr);

			last_ptr_loop = 0;

			/* allocate a cluster in this block group */
			ret = btrfs_find_space_cluster(trans, root,
					       block_group, last_ptr,
					       offset, num_bytes,
					       empty_cluster + empty_size);
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
			goto loop;
		}
checks:
		search_start = stripe_align(root, offset);
		/* move on to the next group */
		if (search_start + num_bytes >= search_end) {
			btrfs_add_free_space(block_group, offset, num_bytes);
			goto loop;
		}

		/* move on to the next group */
		if (search_start + num_bytes >
		    block_group->key.objectid + block_group->key.offset) {
			btrfs_add_free_space(block_group, offset, num_bytes);
			goto loop;
		}

		ins->objectid = search_start;
		ins->offset = num_bytes;

		if (offset < search_start)
			btrfs_add_free_space(block_group, offset,
					     search_start - offset);
		BUG_ON(offset > search_start);

		ret = update_reserved_bytes(block_group, num_bytes, 1,
					    (data & BTRFS_BLOCK_GROUP_DATA));
		if (ret == -EAGAIN) {
			btrfs_add_free_space(block_group, offset, num_bytes);
			goto loop;
		}

		/* we are all good, lets return */
		ins->objectid = search_start;
		ins->offset = num_bytes;

		if (offset < search_start)
			btrfs_add_free_space(block_group, offset,
					     search_start - offset);
		BUG_ON(offset > search_start);
		break;
loop:
		failed_cluster_refill = false;
		failed_alloc = false;
		BUG_ON(index != get_block_group_index(block_group));
		btrfs_put_block_group(block_group);
	}
	up_read(&space_info->groups_sem);

	if (!ins->objectid && ++index < BTRFS_NR_RAID_TYPES)
		goto search;

	/* LOOP_FIND_IDEAL, only search caching/cached bg's, and don't wait for
	 *			for them to make caching progress.  Also
	 *			determine the best possible bg to cache
	 * LOOP_CACHING_NOWAIT, search partially cached block groups, kicking
	 *			caching kthreads as we move along
	 * LOOP_CACHING_WAIT, search everything, and wait if our bg is caching
	 * LOOP_ALLOC_CHUNK, force a chunk allocation and try again
	 * LOOP_NO_EMPTY_SIZE, set empty_size and empty_cluster to 0 and try
	 *			again
	 */
	if (!ins->objectid && loop < LOOP_NO_EMPTY_SIZE &&
	    (found_uncached_bg || empty_size || empty_cluster ||
	     allowed_chunk_alloc)) {
		index = 0;
		if (loop == LOOP_FIND_IDEAL && found_uncached_bg) {
			found_uncached_bg = false;
			loop++;
			if (!ideal_cache_percent &&
			    atomic_read(&space_info->caching_threads))
				goto search;

			/*
			 * 1 of the following 2 things have happened so far
			 *
			 * 1) We found an ideal block group for caching that
			 * is mostly full and will cache quickly, so we might
			 * as well wait for it.
			 *
			 * 2) We searched for cached only and we didn't find
			 * anything, and we didn't start any caching kthreads
			 * either, so chances are we will loop through and
			 * start a couple caching kthreads, and then come back
			 * around and just wait for them.  This will be slower
			 * because we will have 2 caching kthreads reading at
			 * the same time when we could have just started one
			 * and waited for it to get far enough to give us an
			 * allocation, so go ahead and go to the wait caching
			 * loop.
			 */
			loop = LOOP_CACHING_WAIT;
			search_start = ideal_cache_offset;
			ideal_cache_percent = 0;
			goto ideal_cache;
		} else if (loop == LOOP_FIND_IDEAL) {
			/*
			 * Didn't find a uncached bg, wait on anything we find
			 * next.
			 */
			loop = LOOP_CACHING_WAIT;
			goto search;
		}

		if (loop < LOOP_CACHING_WAIT) {
			loop++;
			goto search;
		}

		if (loop == LOOP_ALLOC_CHUNK) {
			empty_size = 0;
			empty_cluster = 0;
		}

		if (allowed_chunk_alloc) {
			ret = do_chunk_alloc(trans, root, num_bytes +
					     2 * 1024 * 1024, data, 1);
			allowed_chunk_alloc = 0;
			done_chunk_alloc = 1;
		} else if (!done_chunk_alloc) {
			space_info->force_alloc = 1;
		}

		if (loop < LOOP_NO_EMPTY_SIZE) {
			loop++;
			goto search;
		}
		ret = -ENOSPC;
	} else if (!ins->objectid) {
		ret = -ENOSPC;
	}

	/* we found what we needed */
	if (ins->objectid) {
		if (!(data & BTRFS_BLOCK_GROUP_DATA))
			trans->block_group = block_group->key.objectid;

		btrfs_put_block_group(block_group);
		ret = 0;
	}

	return ret;
}

static void dump_space_info(struct btrfs_space_info *info, u64 bytes,
			    int dump_block_groups)
{
	struct btrfs_block_group_cache *cache;
	int index = 0;

	spin_lock(&info->lock);
	printk(KERN_INFO "space_info has %llu free, is %sfull\n",
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
		printk(KERN_INFO "block group %llu has %llu bytes, %llu used "
		       "%llu pinned %llu reserved\n",
		       (unsigned long long)cache->key.objectid,
		       (unsigned long long)cache->key.offset,
		       (unsigned long long)btrfs_block_group_used(&cache->item),
		       (unsigned long long)cache->pinned,
		       (unsigned long long)cache->reserved);
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
			 u64 search_end, struct btrfs_key *ins,
			 u64 data)
{
	int ret;
	u64 search_start = 0;

	data = btrfs_get_alloc_profile(root, data);
again:
	/*
	 * the only place that sets empty_size is btrfs_realloc_node, which
	 * is not called recursively on allocations
	 */
	if (empty_size || root->ref_cows)
		ret = do_chunk_alloc(trans, root->fs_info->extent_root,
				     num_bytes + 2 * 1024 * 1024, data, 0);

	WARN_ON(num_bytes < root->sectorsize);
	ret = find_free_extent(trans, root, num_bytes, empty_size,
			       search_start, search_end, hint_byte,
			       ins, data);

	if (ret == -ENOSPC && num_bytes > min_alloc_size) {
		num_bytes = num_bytes >> 1;
		num_bytes = num_bytes & ~(root->sectorsize - 1);
		num_bytes = max(num_bytes, min_alloc_size);
		do_chunk_alloc(trans, root->fs_info->extent_root,
			       num_bytes, data, 1);
		goto again;
	}
	if (ret == -ENOSPC) {
		struct btrfs_space_info *sinfo;

		sinfo = __find_space_info(root->fs_info, data);
		printk(KERN_ERR "btrfs allocation failed flags %llu, "
		       "wanted %llu\n", (unsigned long long)data,
		       (unsigned long long)num_bytes);
		dump_space_info(sinfo, num_bytes, 1);
	}

	return ret;
}

int btrfs_free_reserved_extent(struct btrfs_root *root, u64 start, u64 len)
{
	struct btrfs_block_group_cache *cache;
	int ret = 0;

	cache = btrfs_lookup_block_group(root->fs_info, start);
	if (!cache) {
		printk(KERN_ERR "Unable to find block group for %llu\n",
		       (unsigned long long)start);
		return -ENOSPC;
	}

	ret = btrfs_discard_extent(root, start, len);

	btrfs_add_free_space(cache, start, len);
	update_reserved_bytes(cache, len, 0, 1);
	btrfs_put_block_group(cache);

	return ret;
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
	BUG_ON(!path);

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	BUG_ON(ret);

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

	ret = update_block_group(trans, root, ins->objectid, ins->offset, 1);
	if (ret) {
		printk(KERN_ERR "btrfs update block group failed for %llu "
		       "%llu\n", (unsigned long long)ins->objectid,
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
	u32 size = sizeof(*extent_item) + sizeof(*block_info) + sizeof(*iref);

	path = btrfs_alloc_path();
	BUG_ON(!path);

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, fs_info->extent_root, path,
				      ins, size);
	BUG_ON(ret);

	leaf = path->nodes[0];
	extent_item = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, extent_item, 1);
	btrfs_set_extent_generation(leaf, extent_item, trans->transid);
	btrfs_set_extent_flags(leaf, extent_item,
			       flags | BTRFS_EXTENT_FLAG_TREE_BLOCK);
	block_info = (struct btrfs_tree_block_info *)(extent_item + 1);

	btrfs_set_tree_block_key(leaf, block_info, key);
	btrfs_set_tree_block_level(leaf, block_info, level);

	iref = (struct btrfs_extent_inline_ref *)(block_info + 1);
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

	ret = update_block_group(trans, root, ins->objectid, ins->offset, 1);
	if (ret) {
		printk(KERN_ERR "btrfs update block group failed for %llu "
		       "%llu\n", (unsigned long long)ins->objectid,
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

	ret = btrfs_add_delayed_data_ref(trans, ins->objectid, ins->offset,
					 0, root_objectid, owner, offset,
					 BTRFS_ADD_DELAYED_EXTENT, NULL);
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
	cache_block_group(block_group, trans, 0);
	caching_ctl = get_caching_control(block_group);

	if (!caching_ctl) {
		BUG_ON(!block_group_cache_done(block_group));
		ret = btrfs_remove_free_space(block_group, start, num_bytes);
		BUG_ON(ret);
	} else {
		mutex_lock(&caching_ctl->mutex);

		if (start >= caching_ctl->progress) {
			ret = add_excluded_extent(root, start, num_bytes);
			BUG_ON(ret);
		} else if (start + num_bytes <= caching_ctl->progress) {
			ret = btrfs_remove_free_space(block_group,
						      start, num_bytes);
			BUG_ON(ret);
		} else {
			num_bytes = caching_ctl->progress - start;
			ret = btrfs_remove_free_space(block_group,
						      start, num_bytes);
			BUG_ON(ret);

			start = caching_ctl->progress;
			num_bytes = ins->objectid + ins->offset -
				    caching_ctl->progress;
			ret = add_excluded_extent(root, start, num_bytes);
			BUG_ON(ret);
		}

		mutex_unlock(&caching_ctl->mutex);
		put_caching_control(caching_ctl);
	}

	ret = update_reserved_bytes(block_group, ins->offset, 1, 1);
	BUG_ON(ret);
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
	btrfs_set_buffer_lockdep_class(buf, level);
	btrfs_tree_lock(buf);
	clean_tree_block(trans, root, buf);

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
	int ret;

	block_rsv = get_block_rsv(trans, root);

	if (block_rsv->size == 0) {
		ret = reserve_metadata_bytes(trans, root, block_rsv,
					     blocksize, 0);
		if (ret)
			return ERR_PTR(ret);
		return block_rsv;
	}

	ret = block_rsv_use_bytes(block_rsv, blocksize);
	if (!ret)
		return block_rsv;

	return ERR_PTR(-ENOSPC);
}

static void unuse_block_rsv(struct btrfs_block_rsv *block_rsv, u32 blocksize)
{
	block_rsv_add_bytes(block_rsv, blocksize, 0);
	block_rsv_release_bytes(block_rsv, NULL, 0);
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


	block_rsv = use_block_rsv(trans, root, blocksize);
	if (IS_ERR(block_rsv))
		return ERR_CAST(block_rsv);

	ret = btrfs_reserve_extent(trans, root, blocksize, blocksize,
				   empty_size, hint, (u64)-1, &ins, 0);
	if (ret) {
		unuse_block_rsv(block_rsv, blocksize);
		return ERR_PTR(ret);
	}

	buf = btrfs_init_new_buffer(trans, root, ins.objectid,
				    blocksize, level);
	BUG_ON(IS_ERR(buf));

	if (root_objectid == BTRFS_TREE_RELOC_OBJECTID) {
		if (parent == 0)
			parent = ins.objectid;
		flags |= BTRFS_BLOCK_FLAG_FULL_BACKREF;
	} else
		BUG_ON(parent > 0);

	if (root_objectid != BTRFS_TREE_LOG_OBJECTID) {
		struct btrfs_delayed_extent_op *extent_op;
		extent_op = kmalloc(sizeof(*extent_op), GFP_NOFS);
		BUG_ON(!extent_op);
		if (key)
			memcpy(&extent_op->key, key, sizeof(extent_op->key));
		else
			memset(&extent_op->key, 0, sizeof(extent_op->key));
		extent_op->flags_to_set = flags;
		extent_op->update_key = 1;
		extent_op->update_flags = 1;
		extent_op->is_data = 0;

		ret = btrfs_add_delayed_tree_ref(trans, ins.objectid,
					ins.offset, parent, root_objectid,
					level, BTRFS_ADD_DELAYED_EXTENT,
					extent_op);
		BUG_ON(ret);
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
		ret = btrfs_lookup_extent_info(trans, root, bytenr, blocksize,
					       &refs, &flags);
		BUG_ON(ret);
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
 * hepler to process tree block while walking down the tree.
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
					       eb->start, eb->len,
					       &wc->refs[level],
					       &wc->flags[level]);
		BUG_ON(ret);
		BUG_ON(wc->refs[level] == 0);
	}

	if (wc->stage == DROP_REFERENCE) {
		if (wc->refs[level] > 1)
			return 1;

		if (path->locks[level] && !wc->keep_locks) {
			btrfs_tree_unlock(eb);
			path->locks[level] = 0;
		}
		return 0;
	}

	/* wc->stage == UPDATE_BACKREF */
	if (!(wc->flags[level] & flag)) {
		BUG_ON(!path->locks[level]);
		ret = btrfs_inc_ref(trans, root, eb, 1);
		BUG_ON(ret);
		ret = btrfs_dec_ref(trans, root, eb, 0);
		BUG_ON(ret);
		ret = btrfs_set_disk_extent_flags(trans, root, eb->start,
						  eb->len, flag, 0);
		BUG_ON(ret);
		wc->flags[level] |= flag;
	}

	/*
	 * the block is shared by multiple trees, so it's not good to
	 * keep the tree lock
	 */
	if (path->locks[level] && level > 0) {
		btrfs_tree_unlock(eb);
		path->locks[level] = 0;
	}
	return 0;
}

/*
 * hepler to process tree block pointer.
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

	ret = btrfs_lookup_extent_info(trans, root, bytenr, blocksize,
				       &wc->refs[level - 1],
				       &wc->flags[level - 1]);
	BUG_ON(ret);
	BUG_ON(wc->refs[level - 1] == 0);
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

	if (!btrfs_buffer_uptodate(next, generation)) {
		btrfs_tree_unlock(next);
		free_extent_buffer(next);
		next = NULL;
		*lookup_info = 1;
	}

	if (!next) {
		if (reada && level == 1)
			reada_walk_down(trans, root, wc, path);
		next = read_tree_block(root, bytenr, blocksize, generation);
		btrfs_tree_lock(next);
		btrfs_set_lock_blocking(next);
	}

	level--;
	BUG_ON(level != btrfs_header_level(next));
	path->nodes[level] = next;
	path->slots[level] = 0;
	path->locks[level] = 1;
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
					root->root_key.objectid, level - 1, 0);
		BUG_ON(ret);
	}
	btrfs_tree_unlock(next);
	free_extent_buffer(next);
	*lookup_info = 1;
	return 1;
}

/*
 * hepler to process tree block while walking up the tree.
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
			path->locks[level] = 1;

			ret = btrfs_lookup_extent_info(trans, root,
						       eb->start, eb->len,
						       &wc->refs[level],
						       &wc->flags[level]);
			BUG_ON(ret);
			BUG_ON(wc->refs[level] == 0);
			if (wc->refs[level] == 1) {
				btrfs_tree_unlock(eb);
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
			BUG_ON(ret);
		}
		/* make block locked assertion in clean_tree_block happy */
		if (!path->locks[level] &&
		    btrfs_header_generation(eb) == trans->transid) {
			btrfs_tree_lock(eb);
			btrfs_set_lock_blocking(eb);
			path->locks[level] = 1;
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
				btrfs_tree_unlock(path->nodes[level]);
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
 */
int btrfs_drop_snapshot(struct btrfs_root *root,
			struct btrfs_block_rsv *block_rsv, int update_ref)
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
	BUG_ON(!path);

	wc = kzalloc(sizeof(*wc), GFP_NOFS);
	BUG_ON(!wc);

	trans = btrfs_start_transaction(tree_root, 0);
	if (block_rsv)
		trans->block_rsv = block_rsv;

	if (btrfs_disk_key_objectid(&root_item->drop_progress) == 0) {
		level = btrfs_header_level(root->node);
		path->nodes[level] = btrfs_lock_root_node(root);
		btrfs_set_lock_blocking(path->nodes[level]);
		path->slots[level] = 0;
		path->locks[level] = 1;
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
			goto out;
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
						path->nodes[level]->len,
						&wc->refs[level],
						&wc->flags[level]);
			BUG_ON(ret);
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
		if (btrfs_should_end_transaction(trans, tree_root)) {
			ret = btrfs_update_root(trans, tree_root,
						&root->root_key,
						root_item);
			BUG_ON(ret);

			btrfs_end_transaction_throttle(trans, tree_root);
			trans = btrfs_start_transaction(tree_root, 0);
			if (block_rsv)
				trans->block_rsv = block_rsv;
		}
	}
	btrfs_release_path(root, path);
	BUG_ON(err);

	ret = btrfs_del_root(trans, tree_root, &root->root_key);
	BUG_ON(ret);

	if (root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID) {
		ret = btrfs_find_last_root(tree_root, root->root_key.objectid,
					   NULL, NULL);
		BUG_ON(ret < 0);
		if (ret > 0) {
			ret = btrfs_del_orphan_item(trans, tree_root,
						    root->root_key.objectid);
			BUG_ON(ret);
		}
	}

	if (root->in_radix) {
		btrfs_free_fs_root(tree_root->fs_info, root);
	} else {
		free_extent_buffer(root->node);
		free_extent_buffer(root->commit_root);
		kfree(root);
	}
out:
	btrfs_end_transaction_throttle(trans, tree_root);
	kfree(wc);
	btrfs_free_path(path);
	return err;
}

/*
 * drop subtree rooted at tree block 'node'.
 *
 * NOTE: this function will unlock and release tree block 'node'
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
	BUG_ON(!path);

	wc = kzalloc(sizeof(*wc), GFP_NOFS);
	BUG_ON(!wc);

	btrfs_assert_tree_locked(parent);
	parent_level = btrfs_header_level(parent);
	extent_buffer_get(parent);
	path->nodes[parent_level] = parent;
	path->slots[parent_level] = btrfs_header_nritems(parent);

	btrfs_assert_tree_locked(node);
	level = btrfs_header_level(node);
	path->nodes[level] = node;
	path->slots[level] = 0;
	path->locks[level] = 1;

	wc->refs[parent_level] = 1;
	wc->flags[parent_level] = BTRFS_BLOCK_FLAG_FULL_BACKREF;
	wc->level = level;
	wc->shared_level = -1;
	wc->stage = DROP_REFERENCE;
	wc->update_ref = 0;
	wc->keep_locks = 1;
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

#if 0
static unsigned long calc_ra(unsigned long start, unsigned long last,
			     unsigned long nr)
{
	return min(last, start + nr - 1);
}

static noinline int relocate_inode_pages(struct inode *inode, u64 start,
					 u64 len)
{
	u64 page_start;
	u64 page_end;
	unsigned long first_index;
	unsigned long last_index;
	unsigned long i;
	struct page *page;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct file_ra_state *ra;
	struct btrfs_ordered_extent *ordered;
	unsigned int total_read = 0;
	unsigned int total_dirty = 0;
	int ret = 0;

	ra = kzalloc(sizeof(*ra), GFP_NOFS);

	mutex_lock(&inode->i_mutex);
	first_index = start >> PAGE_CACHE_SHIFT;
	last_index = (start + len - 1) >> PAGE_CACHE_SHIFT;

	/* make sure the dirty trick played by the caller work */
	ret = invalidate_inode_pages2_range(inode->i_mapping,
					    first_index, last_index);
	if (ret)
		goto out_unlock;

	file_ra_state_init(ra, inode->i_mapping);

	for (i = first_index ; i <= last_index; i++) {
		if (total_read % ra->ra_pages == 0) {
			btrfs_force_ra(inode->i_mapping, ra, NULL, i,
				       calc_ra(i, last_index, ra->ra_pages));
		}
		total_read++;
again:
		if (((u64)i << PAGE_CACHE_SHIFT) > i_size_read(inode))
			BUG_ON(1);
		page = grab_cache_page(inode->i_mapping, i);
		if (!page) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_cache_release(page);
				ret = -EIO;
				goto out_unlock;
			}
		}
		wait_on_page_writeback(page);

		page_start = (u64)page->index << PAGE_CACHE_SHIFT;
		page_end = page_start + PAGE_CACHE_SIZE - 1;
		lock_extent(io_tree, page_start, page_end, GFP_NOFS);

		ordered = btrfs_lookup_ordered_extent(inode, page_start);
		if (ordered) {
			unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
			unlock_page(page);
			page_cache_release(page);
			btrfs_start_ordered_extent(inode, ordered, 1);
			btrfs_put_ordered_extent(ordered);
			goto again;
		}
		set_page_extent_mapped(page);

		if (i == first_index)
			set_extent_bits(io_tree, page_start, page_end,
					EXTENT_BOUNDARY, GFP_NOFS);
		btrfs_set_extent_delalloc(inode, page_start, page_end);

		set_page_dirty(page);
		total_dirty++;

		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
		unlock_page(page);
		page_cache_release(page);
	}

out_unlock:
	kfree(ra);
	mutex_unlock(&inode->i_mutex);
	balance_dirty_pages_ratelimited_nr(inode->i_mapping, total_dirty);
	return ret;
}

static noinline int relocate_data_extent(struct inode *reloc_inode,
					 struct btrfs_key *extent_key,
					 u64 offset)
{
	struct btrfs_root *root = BTRFS_I(reloc_inode)->root;
	struct extent_map_tree *em_tree = &BTRFS_I(reloc_inode)->extent_tree;
	struct extent_map *em;
	u64 start = extent_key->objectid - offset;
	u64 end = start + extent_key->offset - 1;

	em = alloc_extent_map(GFP_NOFS);
	BUG_ON(!em || IS_ERR(em));

	em->start = start;
	em->len = extent_key->offset;
	em->block_len = extent_key->offset;
	em->block_start = extent_key->objectid;
	em->bdev = root->fs_info->fs_devices->latest_bdev;
	set_bit(EXTENT_FLAG_PINNED, &em->flags);

	/* setup extent map to cheat btrfs_readpage */
	lock_extent(&BTRFS_I(reloc_inode)->io_tree, start, end, GFP_NOFS);
	while (1) {
		int ret;
		write_lock(&em_tree->lock);
		ret = add_extent_mapping(em_tree, em);
		write_unlock(&em_tree->lock);
		if (ret != -EEXIST) {
			free_extent_map(em);
			break;
		}
		btrfs_drop_extent_cache(reloc_inode, start, end, 0);
	}
	unlock_extent(&BTRFS_I(reloc_inode)->io_tree, start, end, GFP_NOFS);

	return relocate_inode_pages(reloc_inode, start, extent_key->offset);
}

struct btrfs_ref_path {
	u64 extent_start;
	u64 nodes[BTRFS_MAX_LEVEL];
	u64 root_objectid;
	u64 root_generation;
	u64 owner_objectid;
	u32 num_refs;
	int lowest_level;
	int current_level;
	int shared_level;

	struct btrfs_key node_keys[BTRFS_MAX_LEVEL];
	u64 new_nodes[BTRFS_MAX_LEVEL];
};

struct disk_extent {
	u64 ram_bytes;
	u64 disk_bytenr;
	u64 disk_num_bytes;
	u64 offset;
	u64 num_bytes;
	u8 compression;
	u8 encryption;
	u16 other_encoding;
};

static int is_cowonly_root(u64 root_objectid)
{
	if (root_objectid == BTRFS_ROOT_TREE_OBJECTID ||
	    root_objectid == BTRFS_EXTENT_TREE_OBJECTID ||
	    root_objectid == BTRFS_CHUNK_TREE_OBJECTID ||
	    root_objectid == BTRFS_DEV_TREE_OBJECTID ||
	    root_objectid == BTRFS_TREE_LOG_OBJECTID ||
	    root_objectid == BTRFS_CSUM_TREE_OBJECTID)
		return 1;
	return 0;
}

static noinline int __next_ref_path(struct btrfs_trans_handle *trans,
				    struct btrfs_root *extent_root,
				    struct btrfs_ref_path *ref_path,
				    int first_time)
{
	struct extent_buffer *leaf;
	struct btrfs_path *path;
	struct btrfs_extent_ref *ref;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u64 bytenr;
	u32 nritems;
	int level;
	int ret = 1;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (first_time) {
		ref_path->lowest_level = -1;
		ref_path->current_level = -1;
		ref_path->shared_level = -1;
		goto walk_up;
	}
walk_down:
	level = ref_path->current_level - 1;
	while (level >= -1) {
		u64 parent;
		if (level < ref_path->lowest_level)
			break;

		if (level >= 0)
			bytenr = ref_path->nodes[level];
		else
			bytenr = ref_path->extent_start;
		BUG_ON(bytenr == 0);

		parent = ref_path->nodes[level + 1];
		ref_path->nodes[level + 1] = 0;
		ref_path->current_level = level;
		BUG_ON(parent == 0);

		key.objectid = bytenr;
		key.offset = parent + 1;
		key.type = BTRFS_EXTENT_REF_KEY;

		ret = btrfs_search_slot(trans, extent_root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
		BUG_ON(ret == 0);

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(extent_root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				goto next;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid == bytenr &&
		    found_key.type == BTRFS_EXTENT_REF_KEY) {
			if (level < ref_path->shared_level)
				ref_path->shared_level = level;
			goto found;
		}
next:
		level--;
		btrfs_release_path(extent_root, path);
		cond_resched();
	}
	/* reached lowest level */
	ret = 1;
	goto out;
walk_up:
	level = ref_path->current_level;
	while (level < BTRFS_MAX_LEVEL - 1) {
		u64 ref_objectid;

		if (level >= 0)
			bytenr = ref_path->nodes[level];
		else
			bytenr = ref_path->extent_start;

		BUG_ON(bytenr == 0);

		key.objectid = bytenr;
		key.offset = 0;
		key.type = BTRFS_EXTENT_REF_KEY;

		ret = btrfs_search_slot(trans, extent_root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(extent_root, path);
			if (ret < 0)
				goto out;
			if (ret > 0) {
				/* the extent was freed by someone */
				if (ref_path->lowest_level == level)
					goto out;
				btrfs_release_path(extent_root, path);
				goto walk_down;
			}
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.objectid != bytenr ||
				found_key.type != BTRFS_EXTENT_REF_KEY) {
			/* the extent was freed by someone */
			if (ref_path->lowest_level == level) {
				ret = 1;
				goto out;
			}
			btrfs_release_path(extent_root, path);
			goto walk_down;
		}
found:
		ref = btrfs_item_ptr(leaf, path->slots[0],
				struct btrfs_extent_ref);
		ref_objectid = btrfs_ref_objectid(leaf, ref);
		if (ref_objectid < BTRFS_FIRST_FREE_OBJECTID) {
			if (first_time) {
				level = (int)ref_objectid;
				BUG_ON(level >= BTRFS_MAX_LEVEL);
				ref_path->lowest_level = level;
				ref_path->current_level = level;
				ref_path->nodes[level] = bytenr;
			} else {
				WARN_ON(ref_objectid != level);
			}
		} else {
			WARN_ON(level != -1);
		}
		first_time = 0;

		if (ref_path->lowest_level == level) {
			ref_path->owner_objectid = ref_objectid;
			ref_path->num_refs = btrfs_ref_num_refs(leaf, ref);
		}

		/*
		 * the block is tree root or the block isn't in reference
		 * counted tree.
		 */
		if (found_key.objectid == found_key.offset ||
		    is_cowonly_root(btrfs_ref_root(leaf, ref))) {
			ref_path->root_objectid = btrfs_ref_root(leaf, ref);
			ref_path->root_generation =
				btrfs_ref_generation(leaf, ref);
			if (level < 0) {
				/* special reference from the tree log */
				ref_path->nodes[0] = found_key.offset;
				ref_path->current_level = 0;
			}
			ret = 0;
			goto out;
		}

		level++;
		BUG_ON(ref_path->nodes[level] != 0);
		ref_path->nodes[level] = found_key.offset;
		ref_path->current_level = level;

		/*
		 * the reference was created in the running transaction,
		 * no need to continue walking up.
		 */
		if (btrfs_ref_generation(leaf, ref) == trans->transid) {
			ref_path->root_objectid = btrfs_ref_root(leaf, ref);
			ref_path->root_generation =
				btrfs_ref_generation(leaf, ref);
			ret = 0;
			goto out;
		}

		btrfs_release_path(extent_root, path);
		cond_resched();
	}
	/* reached max tree level, but no tree root found. */
	BUG();
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_first_ref_path(struct btrfs_trans_handle *trans,
				struct btrfs_root *extent_root,
				struct btrfs_ref_path *ref_path,
				u64 extent_start)
{
	memset(ref_path, 0, sizeof(*ref_path));
	ref_path->extent_start = extent_start;

	return __next_ref_path(trans, extent_root, ref_path, 1);
}

static int btrfs_next_ref_path(struct btrfs_trans_handle *trans,
			       struct btrfs_root *extent_root,
			       struct btrfs_ref_path *ref_path)
{
	return __next_ref_path(trans, extent_root, ref_path, 0);
}

static noinline int get_new_locations(struct inode *reloc_inode,
				      struct btrfs_key *extent_key,
				      u64 offset, int no_fragment,
				      struct disk_extent **extents,
				      int *nr_extents)
{
	struct btrfs_root *root = BTRFS_I(reloc_inode)->root;
	struct btrfs_path *path;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *leaf;
	struct disk_extent *exts = *extents;
	struct btrfs_key found_key;
	u64 cur_pos;
	u64 last_byte;
	u32 nritems;
	int nr = 0;
	int max = *nr_extents;
	int ret;

	WARN_ON(!no_fragment && *extents);
	if (!exts) {
		max = 1;
		exts = kmalloc(sizeof(*exts) * max, GFP_NOFS);
		if (!exts)
			return -ENOMEM;
	}

	path = btrfs_alloc_path();
	BUG_ON(!path);

	cur_pos = extent_key->objectid - offset;
	last_byte = extent_key->objectid + extent_key->offset;
	ret = btrfs_lookup_file_extent(NULL, root, path, reloc_inode->i_ino,
				       cur_pos, 0);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	while (1) {
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		if (found_key.offset != cur_pos ||
		    found_key.type != BTRFS_EXTENT_DATA_KEY ||
		    found_key.objectid != reloc_inode->i_ino)
			break;

		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) !=
		    BTRFS_FILE_EXTENT_REG ||
		    btrfs_file_extent_disk_bytenr(leaf, fi) == 0)
			break;

		if (nr == max) {
			struct disk_extent *old = exts;
			max *= 2;
			exts = kzalloc(sizeof(*exts) * max, GFP_NOFS);
			memcpy(exts, old, sizeof(*exts) * nr);
			if (old != *extents)
				kfree(old);
		}

		exts[nr].disk_bytenr =
			btrfs_file_extent_disk_bytenr(leaf, fi);
		exts[nr].disk_num_bytes =
			btrfs_file_extent_disk_num_bytes(leaf, fi);
		exts[nr].offset = btrfs_file_extent_offset(leaf, fi);
		exts[nr].num_bytes = btrfs_file_extent_num_bytes(leaf, fi);
		exts[nr].ram_bytes = btrfs_file_extent_ram_bytes(leaf, fi);
		exts[nr].compression = btrfs_file_extent_compression(leaf, fi);
		exts[nr].encryption = btrfs_file_extent_encryption(leaf, fi);
		exts[nr].other_encoding = btrfs_file_extent_other_encoding(leaf,
									   fi);
		BUG_ON(exts[nr].offset > 0);
		BUG_ON(exts[nr].compression || exts[nr].encryption);
		BUG_ON(exts[nr].num_bytes != exts[nr].disk_num_bytes);

		cur_pos += exts[nr].num_bytes;
		nr++;

		if (cur_pos + offset >= last_byte)
			break;

		if (no_fragment) {
			ret = 1;
			goto out;
		}
		path->slots[0]++;
	}

	BUG_ON(cur_pos + offset > last_byte);
	if (cur_pos + offset < last_byte) {
		ret = -ENOENT;
		goto out;
	}
	ret = 0;
out:
	btrfs_free_path(path);
	if (ret) {
		if (exts != *extents)
			kfree(exts);
	} else {
		*extents = exts;
		*nr_extents = nr;
	}
	return ret;
}

static noinline int replace_one_extent(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_key *extent_key,
					struct btrfs_key *leaf_key,
					struct btrfs_ref_path *ref_path,
					struct disk_extent *new_extents,
					int nr_extents)
{
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	struct inode *inode = NULL;
	struct btrfs_key key;
	u64 lock_start = 0;
	u64 lock_end = 0;
	u64 num_bytes;
	u64 ext_offset;
	u64 search_end = (u64)-1;
	u32 nritems;
	int nr_scaned = 0;
	int extent_locked = 0;
	int extent_type;
	int ret;

	memcpy(&key, leaf_key, sizeof(key));
	if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS) {
		if (key.objectid < ref_path->owner_objectid ||
		    (key.objectid == ref_path->owner_objectid &&
		     key.type < BTRFS_EXTENT_DATA_KEY)) {
			key.objectid = ref_path->owner_objectid;
			key.type = BTRFS_EXTENT_DATA_KEY;
			key.offset = 0;
		}
	}

	while (1) {
		ret = btrfs_search_slot(trans, root, &key, path, 0, 1);
		if (ret < 0)
			goto out;

		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
next:
		if (extent_locked && ret > 0) {
			/*
			 * the file extent item was modified by someone
			 * before the extent got locked.
			 */
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
			extent_locked = 0;
		}

		if (path->slots[0] >= nritems) {
			if (++nr_scaned > 2)
				break;

			BUG_ON(extent_locked);
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);

		if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS) {
			if ((key.objectid > ref_path->owner_objectid) ||
			    (key.objectid == ref_path->owner_objectid &&
			     key.type > BTRFS_EXTENT_DATA_KEY) ||
			    key.offset >= search_end)
				break;
		}

		if (inode && key.objectid != inode->i_ino) {
			BUG_ON(extent_locked);
			btrfs_release_path(root, path);
			mutex_unlock(&inode->i_mutex);
			iput(inode);
			inode = NULL;
			continue;
		}

		if (key.type != BTRFS_EXTENT_DATA_KEY) {
			path->slots[0]++;
			ret = 1;
			goto next;
		}
		fi = btrfs_item_ptr(leaf, path->slots[0],
				    struct btrfs_file_extent_item);
		extent_type = btrfs_file_extent_type(leaf, fi);
		if ((extent_type != BTRFS_FILE_EXTENT_REG &&
		     extent_type != BTRFS_FILE_EXTENT_PREALLOC) ||
		    (btrfs_file_extent_disk_bytenr(leaf, fi) !=
		     extent_key->objectid)) {
			path->slots[0]++;
			ret = 1;
			goto next;
		}

		num_bytes = btrfs_file_extent_num_bytes(leaf, fi);
		ext_offset = btrfs_file_extent_offset(leaf, fi);

		if (search_end == (u64)-1) {
			search_end = key.offset - ext_offset +
				btrfs_file_extent_ram_bytes(leaf, fi);
		}

		if (!extent_locked) {
			lock_start = key.offset;
			lock_end = lock_start + num_bytes - 1;
		} else {
			if (lock_start > key.offset ||
			    lock_end + 1 < key.offset + num_bytes) {
				unlock_extent(&BTRFS_I(inode)->io_tree,
					      lock_start, lock_end, GFP_NOFS);
				extent_locked = 0;
			}
		}

		if (!inode) {
			btrfs_release_path(root, path);

			inode = btrfs_iget_locked(root->fs_info->sb,
						  key.objectid, root);
			if (inode->i_state & I_NEW) {
				BTRFS_I(inode)->root = root;
				BTRFS_I(inode)->location.objectid =
					key.objectid;
				BTRFS_I(inode)->location.type =
					BTRFS_INODE_ITEM_KEY;
				BTRFS_I(inode)->location.offset = 0;
				btrfs_read_locked_inode(inode);
				unlock_new_inode(inode);
			}
			/*
			 * some code call btrfs_commit_transaction while
			 * holding the i_mutex, so we can't use mutex_lock
			 * here.
			 */
			if (is_bad_inode(inode) ||
			    !mutex_trylock(&inode->i_mutex)) {
				iput(inode);
				inode = NULL;
				key.offset = (u64)-1;
				goto skip;
			}
		}

		if (!extent_locked) {
			struct btrfs_ordered_extent *ordered;

			btrfs_release_path(root, path);

			lock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				    lock_end, GFP_NOFS);
			ordered = btrfs_lookup_first_ordered_extent(inode,
								    lock_end);
			if (ordered &&
			    ordered->file_offset <= lock_end &&
			    ordered->file_offset + ordered->len > lock_start) {
				unlock_extent(&BTRFS_I(inode)->io_tree,
					      lock_start, lock_end, GFP_NOFS);
				btrfs_start_ordered_extent(inode, ordered, 1);
				btrfs_put_ordered_extent(ordered);
				key.offset += num_bytes;
				goto skip;
			}
			if (ordered)
				btrfs_put_ordered_extent(ordered);

			extent_locked = 1;
			continue;
		}

		if (nr_extents == 1) {
			/* update extent pointer in place */
			btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extents[0].disk_bytenr);
			btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extents[0].disk_num_bytes);
			btrfs_mark_buffer_dirty(leaf);

			btrfs_drop_extent_cache(inode, key.offset,
						key.offset + num_bytes - 1, 0);

			ret = btrfs_inc_extent_ref(trans, root,
						new_extents[0].disk_bytenr,
						new_extents[0].disk_num_bytes,
						leaf->start,
						root->root_key.objectid,
						trans->transid,
						key.objectid);
			BUG_ON(ret);

			ret = btrfs_free_extent(trans, root,
						extent_key->objectid,
						extent_key->offset,
						leaf->start,
						btrfs_header_owner(leaf),
						btrfs_header_generation(leaf),
						key.objectid, 0);
			BUG_ON(ret);

			btrfs_release_path(root, path);
			key.offset += num_bytes;
		} else {
			BUG_ON(1);
#if 0
			u64 alloc_hint;
			u64 extent_len;
			int i;
			/*
			 * drop old extent pointer at first, then insert the
			 * new pointers one bye one
			 */
			btrfs_release_path(root, path);
			ret = btrfs_drop_extents(trans, root, inode, key.offset,
						 key.offset + num_bytes,
						 key.offset, &alloc_hint);
			BUG_ON(ret);

			for (i = 0; i < nr_extents; i++) {
				if (ext_offset >= new_extents[i].num_bytes) {
					ext_offset -= new_extents[i].num_bytes;
					continue;
				}
				extent_len = min(new_extents[i].num_bytes -
						 ext_offset, num_bytes);

				ret = btrfs_insert_empty_item(trans, root,
							      path, &key,
							      sizeof(*fi));
				BUG_ON(ret);

				leaf = path->nodes[0];
				fi = btrfs_item_ptr(leaf, path->slots[0],
						struct btrfs_file_extent_item);
				btrfs_set_file_extent_generation(leaf, fi,
							trans->transid);
				btrfs_set_file_extent_type(leaf, fi,
							BTRFS_FILE_EXTENT_REG);
				btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extents[i].disk_bytenr);
				btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extents[i].disk_num_bytes);
				btrfs_set_file_extent_ram_bytes(leaf, fi,
						new_extents[i].ram_bytes);

				btrfs_set_file_extent_compression(leaf, fi,
						new_extents[i].compression);
				btrfs_set_file_extent_encryption(leaf, fi,
						new_extents[i].encryption);
				btrfs_set_file_extent_other_encoding(leaf, fi,
						new_extents[i].other_encoding);

				btrfs_set_file_extent_num_bytes(leaf, fi,
							extent_len);
				ext_offset += new_extents[i].offset;
				btrfs_set_file_extent_offset(leaf, fi,
							ext_offset);
				btrfs_mark_buffer_dirty(leaf);

				btrfs_drop_extent_cache(inode, key.offset,
						key.offset + extent_len - 1, 0);

				ret = btrfs_inc_extent_ref(trans, root,
						new_extents[i].disk_bytenr,
						new_extents[i].disk_num_bytes,
						leaf->start,
						root->root_key.objectid,
						trans->transid, key.objectid);
				BUG_ON(ret);
				btrfs_release_path(root, path);

				inode_add_bytes(inode, extent_len);

				ext_offset = 0;
				num_bytes -= extent_len;
				key.offset += extent_len;

				if (num_bytes == 0)
					break;
			}
			BUG_ON(i >= nr_extents);
#endif
		}

		if (extent_locked) {
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
			extent_locked = 0;
		}
skip:
		if (ref_path->owner_objectid != BTRFS_MULTIPLE_OBJECTIDS &&
		    key.offset >= search_end)
			break;

		cond_resched();
	}
	ret = 0;
out:
	btrfs_release_path(root, path);
	if (inode) {
		mutex_unlock(&inode->i_mutex);
		if (extent_locked) {
			unlock_extent(&BTRFS_I(inode)->io_tree, lock_start,
				      lock_end, GFP_NOFS);
		}
		iput(inode);
	}
	return ret;
}

int btrfs_reloc_tree_cache_ref(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct extent_buffer *buf, u64 orig_start)
{
	int level;
	int ret;

	BUG_ON(btrfs_header_generation(buf) != trans->transid);
	BUG_ON(root->root_key.objectid != BTRFS_TREE_RELOC_OBJECTID);

	level = btrfs_header_level(buf);
	if (level == 0) {
		struct btrfs_leaf_ref *ref;
		struct btrfs_leaf_ref *orig_ref;

		orig_ref = btrfs_lookup_leaf_ref(root, orig_start);
		if (!orig_ref)
			return -ENOENT;

		ref = btrfs_alloc_leaf_ref(root, orig_ref->nritems);
		if (!ref) {
			btrfs_free_leaf_ref(root, orig_ref);
			return -ENOMEM;
		}

		ref->nritems = orig_ref->nritems;
		memcpy(ref->extents, orig_ref->extents,
			sizeof(ref->extents[0]) * ref->nritems);

		btrfs_free_leaf_ref(root, orig_ref);

		ref->root_gen = trans->transid;
		ref->bytenr = buf->start;
		ref->owner = btrfs_header_owner(buf);
		ref->generation = btrfs_header_generation(buf);

		ret = btrfs_add_leaf_ref(root, ref, 0);
		WARN_ON(ret);
		btrfs_free_leaf_ref(root, ref);
	}
	return 0;
}

static noinline int invalidate_extent_cache(struct btrfs_root *root,
					struct extent_buffer *leaf,
					struct btrfs_block_group_cache *group,
					struct btrfs_root *target_root)
{
	struct btrfs_key key;
	struct inode *inode = NULL;
	struct btrfs_file_extent_item *fi;
	struct extent_state *cached_state = NULL;
	u64 num_bytes;
	u64 skip_objectid = 0;
	u32 nritems;
	u32 i;

	nritems = btrfs_header_nritems(leaf);
	for (i = 0; i < nritems; i++) {
		btrfs_item_key_to_cpu(leaf, &key, i);
		if (key.objectid == skip_objectid ||
		    key.type != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;
		if (btrfs_file_extent_disk_bytenr(leaf, fi) == 0)
			continue;
		if (!inode || inode->i_ino != key.objectid) {
			iput(inode);
			inode = btrfs_ilookup(target_root->fs_info->sb,
					      key.objectid, target_root, 1);
		}
		if (!inode) {
			skip_objectid = key.objectid;
			continue;
		}
		num_bytes = btrfs_file_extent_num_bytes(leaf, fi);

		lock_extent_bits(&BTRFS_I(inode)->io_tree, key.offset,
				 key.offset + num_bytes - 1, 0, &cached_state,
				 GFP_NOFS);
		btrfs_drop_extent_cache(inode, key.offset,
					key.offset + num_bytes - 1, 1);
		unlock_extent_cached(&BTRFS_I(inode)->io_tree, key.offset,
				     key.offset + num_bytes - 1, &cached_state,
				     GFP_NOFS);
		cond_resched();
	}
	iput(inode);
	return 0;
}

static noinline int replace_extents_in_leaf(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct extent_buffer *leaf,
					struct btrfs_block_group_cache *group,
					struct inode *reloc_inode)
{
	struct btrfs_key key;
	struct btrfs_key extent_key;
	struct btrfs_file_extent_item *fi;
	struct btrfs_leaf_ref *ref;
	struct disk_extent *new_extent;
	u64 bytenr;
	u64 num_bytes;
	u32 nritems;
	u32 i;
	int ext_index;
	int nr_extent;
	int ret;

	new_extent = kmalloc(sizeof(*new_extent), GFP_NOFS);
	BUG_ON(!new_extent);

	ref = btrfs_lookup_leaf_ref(root, leaf->start);
	BUG_ON(!ref);

	ext_index = -1;
	nritems = btrfs_header_nritems(leaf);
	for (i = 0; i < nritems; i++) {
		btrfs_item_key_to_cpu(leaf, &key, i);
		if (btrfs_key_type(&key) != BTRFS_EXTENT_DATA_KEY)
			continue;
		fi = btrfs_item_ptr(leaf, i, struct btrfs_file_extent_item);
		if (btrfs_file_extent_type(leaf, fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			continue;
		bytenr = btrfs_file_extent_disk_bytenr(leaf, fi);
		num_bytes = btrfs_file_extent_disk_num_bytes(leaf, fi);
		if (bytenr == 0)
			continue;

		ext_index++;
		if (bytenr >= group->key.objectid + group->key.offset ||
		    bytenr + num_bytes <= group->key.objectid)
			continue;

		extent_key.objectid = bytenr;
		extent_key.offset = num_bytes;
		extent_key.type = BTRFS_EXTENT_ITEM_KEY;
		nr_extent = 1;
		ret = get_new_locations(reloc_inode, &extent_key,
					group->key.objectid, 1,
					&new_extent, &nr_extent);
		if (ret > 0)
			continue;
		BUG_ON(ret < 0);

		BUG_ON(ref->extents[ext_index].bytenr != bytenr);
		BUG_ON(ref->extents[ext_index].num_bytes != num_bytes);
		ref->extents[ext_index].bytenr = new_extent->disk_bytenr;
		ref->extents[ext_index].num_bytes = new_extent->disk_num_bytes;

		btrfs_set_file_extent_disk_bytenr(leaf, fi,
						new_extent->disk_bytenr);
		btrfs_set_file_extent_disk_num_bytes(leaf, fi,
						new_extent->disk_num_bytes);
		btrfs_mark_buffer_dirty(leaf);

		ret = btrfs_inc_extent_ref(trans, root,
					new_extent->disk_bytenr,
					new_extent->disk_num_bytes,
					leaf->start,
					root->root_key.objectid,
					trans->transid, key.objectid);
		BUG_ON(ret);

		ret = btrfs_free_extent(trans, root,
					bytenr, num_bytes, leaf->start,
					btrfs_header_owner(leaf),
					btrfs_header_generation(leaf),
					key.objectid, 0);
		BUG_ON(ret);
		cond_resched();
	}
	kfree(new_extent);
	BUG_ON(ext_index + 1 != ref->nritems);
	btrfs_free_leaf_ref(root, ref);
	return 0;
}

int btrfs_free_reloc_root(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	int ret;

	if (root->reloc_root) {
		reloc_root = root->reloc_root;
		root->reloc_root = NULL;
		list_add(&reloc_root->dead_list,
			 &root->fs_info->dead_reloc_roots);

		btrfs_set_root_bytenr(&reloc_root->root_item,
				      reloc_root->node->start);
		btrfs_set_root_level(&root->root_item,
				     btrfs_header_level(reloc_root->node));
		memset(&reloc_root->root_item.drop_progress, 0,
			sizeof(struct btrfs_disk_key));
		reloc_root->root_item.drop_level = 0;

		ret = btrfs_update_root(trans, root->fs_info->tree_root,
					&reloc_root->root_key,
					&reloc_root->root_item);
		BUG_ON(ret);
	}
	return 0;
}

int btrfs_drop_dead_reloc_roots(struct btrfs_root *root)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *reloc_root;
	struct btrfs_root *prev_root = NULL;
	struct list_head dead_roots;
	int ret;
	unsigned long nr;

	INIT_LIST_HEAD(&dead_roots);
	list_splice_init(&root->fs_info->dead_reloc_roots, &dead_roots);

	while (!list_empty(&dead_roots)) {
		reloc_root = list_entry(dead_roots.prev,
					struct btrfs_root, dead_list);
		list_del_init(&reloc_root->dead_list);

		BUG_ON(reloc_root->commit_root != NULL);
		while (1) {
			trans = btrfs_join_transaction(root, 1);
			BUG_ON(!trans);

			mutex_lock(&root->fs_info->drop_mutex);
			ret = btrfs_drop_snapshot(trans, reloc_root);
			if (ret != -EAGAIN)
				break;
			mutex_unlock(&root->fs_info->drop_mutex);

			nr = trans->blocks_used;
			ret = btrfs_end_transaction(trans, root);
			BUG_ON(ret);
			btrfs_btree_balance_dirty(root, nr);
		}

		free_extent_buffer(reloc_root->node);

		ret = btrfs_del_root(trans, root->fs_info->tree_root,
				     &reloc_root->root_key);
		BUG_ON(ret);
		mutex_unlock(&root->fs_info->drop_mutex);

		nr = trans->blocks_used;
		ret = btrfs_end_transaction(trans, root);
		BUG_ON(ret);
		btrfs_btree_balance_dirty(root, nr);

		kfree(prev_root);
		prev_root = reloc_root;
	}
	if (prev_root) {
		btrfs_remove_leaf_refs(prev_root, (u64)-1, 0);
		kfree(prev_root);
	}
	return 0;
}

int btrfs_add_dead_reloc_root(struct btrfs_root *root)
{
	list_add(&root->dead_list, &root->fs_info->dead_reloc_roots);
	return 0;
}

int btrfs_cleanup_reloc_trees(struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	struct btrfs_trans_handle *trans;
	struct btrfs_key location;
	int found;
	int ret;

	mutex_lock(&root->fs_info->tree_reloc_mutex);
	ret = btrfs_find_dead_roots(root, BTRFS_TREE_RELOC_OBJECTID, NULL);
	BUG_ON(ret);
	found = !list_empty(&root->fs_info->dead_reloc_roots);
	mutex_unlock(&root->fs_info->tree_reloc_mutex);

	if (found) {
		trans = btrfs_start_transaction(root, 1);
		BUG_ON(!trans);
		ret = btrfs_commit_transaction(trans, root);
		BUG_ON(ret);
	}

	location.objectid = BTRFS_DATA_RELOC_TREE_OBJECTID;
	location.offset = (u64)-1;
	location.type = BTRFS_ROOT_ITEM_KEY;

	reloc_root = btrfs_read_fs_root_no_name(root->fs_info, &location);
	BUG_ON(!reloc_root);
	btrfs_orphan_cleanup(reloc_root);
	return 0;
}

static noinline int init_reloc_tree(struct btrfs_trans_handle *trans,
				    struct btrfs_root *root)
{
	struct btrfs_root *reloc_root;
	struct extent_buffer *eb;
	struct btrfs_root_item *root_item;
	struct btrfs_key root_key;
	int ret;

	BUG_ON(!root->ref_cows);
	if (root->reloc_root)
		return 0;

	root_item = kmalloc(sizeof(*root_item), GFP_NOFS);
	BUG_ON(!root_item);

	ret = btrfs_copy_root(trans, root, root->commit_root,
			      &eb, BTRFS_TREE_RELOC_OBJECTID);
	BUG_ON(ret);

	root_key.objectid = BTRFS_TREE_RELOC_OBJECTID;
	root_key.offset = root->root_key.objectid;
	root_key.type = BTRFS_ROOT_ITEM_KEY;

	memcpy(root_item, &root->root_item, sizeof(root_item));
	btrfs_set_root_refs(root_item, 0);
	btrfs_set_root_bytenr(root_item, eb->start);
	btrfs_set_root_level(root_item, btrfs_header_level(eb));
	btrfs_set_root_generation(root_item, trans->transid);

	btrfs_tree_unlock(eb);
	free_extent_buffer(eb);

	ret = btrfs_insert_root(trans, root->fs_info->tree_root,
				&root_key, root_item);
	BUG_ON(ret);
	kfree(root_item);

	reloc_root = btrfs_read_fs_root_no_radix(root->fs_info->tree_root,
						 &root_key);
	BUG_ON(!reloc_root);
	reloc_root->last_trans = trans->transid;
	reloc_root->commit_root = NULL;
	reloc_root->ref_tree = &root->fs_info->reloc_ref_tree;

	root->reloc_root = reloc_root;
	return 0;
}

/*
 * Core function of space balance.
 *
 * The idea is using reloc trees to relocate tree blocks in reference
 * counted roots. There is one reloc tree for each subvol, and all
 * reloc trees share same root key objectid. Reloc trees are snapshots
 * of the latest committed roots of subvols (root->commit_root).
 *
 * To relocate a tree block referenced by a subvol, there are two steps.
 * COW the block through subvol's reloc tree, then update block pointer
 * in the subvol to point to the new block. Since all reloc trees share
 * same root key objectid, doing special handing for tree blocks owned
 * by them is easy. Once a tree block has been COWed in one reloc tree,
 * we can use the resulting new block directly when the same block is
 * required to COW again through other reloc trees. By this way, relocated
 * tree blocks are shared between reloc trees, so they are also shared
 * between subvols.
 */
static noinline int relocate_one_path(struct btrfs_trans_handle *trans,
				      struct btrfs_root *root,
				      struct btrfs_path *path,
				      struct btrfs_key *first_key,
				      struct btrfs_ref_path *ref_path,
				      struct btrfs_block_group_cache *group,
				      struct inode *reloc_inode)
{
	struct btrfs_root *reloc_root;
	struct extent_buffer *eb = NULL;
	struct btrfs_key *keys;
	u64 *nodes;
	int level;
	int shared_level;
	int lowest_level = 0;
	int ret;

	if (ref_path->owner_objectid < BTRFS_FIRST_FREE_OBJECTID)
		lowest_level = ref_path->owner_objectid;

	if (!root->ref_cows) {
		path->lowest_level = lowest_level;
		ret = btrfs_search_slot(trans, root, first_key, path, 0, 1);
		BUG_ON(ret < 0);
		path->lowest_level = 0;
		btrfs_release_path(root, path);
		return 0;
	}

	mutex_lock(&root->fs_info->tree_reloc_mutex);
	ret = init_reloc_tree(trans, root);
	BUG_ON(ret);
	reloc_root = root->reloc_root;

	shared_level = ref_path->shared_level;
	ref_path->shared_level = BTRFS_MAX_LEVEL - 1;

	keys = ref_path->node_keys;
	nodes = ref_path->new_nodes;
	memset(&keys[shared_level + 1], 0,
	       sizeof(*keys) * (BTRFS_MAX_LEVEL - shared_level - 1));
	memset(&nodes[shared_level + 1], 0,
	       sizeof(*nodes) * (BTRFS_MAX_LEVEL - shared_level - 1));

	if (nodes[lowest_level] == 0) {
		path->lowest_level = lowest_level;
		ret = btrfs_search_slot(trans, reloc_root, first_key, path,
					0, 1);
		BUG_ON(ret);
		for (level = lowest_level; level < BTRFS_MAX_LEVEL; level++) {
			eb = path->nodes[level];
			if (!eb || eb == reloc_root->node)
				break;
			nodes[level] = eb->start;
			if (level == 0)
				btrfs_item_key_to_cpu(eb, &keys[level], 0);
			else
				btrfs_node_key_to_cpu(eb, &keys[level], 0);
		}
		if (nodes[0] &&
		    ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
			eb = path->nodes[0];
			ret = replace_extents_in_leaf(trans, reloc_root, eb,
						      group, reloc_inode);
			BUG_ON(ret);
		}
		btrfs_release_path(reloc_root, path);
	} else {
		ret = btrfs_merge_path(trans, reloc_root, keys, nodes,
				       lowest_level);
		BUG_ON(ret);
	}

	/*
	 * replace tree blocks in the fs tree with tree blocks in
	 * the reloc tree.
	 */
	ret = btrfs_merge_path(trans, root, keys, nodes, lowest_level);
	BUG_ON(ret < 0);

	if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
		ret = btrfs_search_slot(trans, reloc_root, first_key, path,
					0, 0);
		BUG_ON(ret);
		extent_buffer_get(path->nodes[0]);
		eb = path->nodes[0];
		btrfs_release_path(reloc_root, path);
		ret = invalidate_extent_cache(reloc_root, eb, group, root);
		BUG_ON(ret);
		free_extent_buffer(eb);
	}

	mutex_unlock(&root->fs_info->tree_reloc_mutex);
	path->lowest_level = 0;
	return 0;
}

static noinline int relocate_tree_block(struct btrfs_trans_handle *trans,
					struct btrfs_root *root,
					struct btrfs_path *path,
					struct btrfs_key *first_key,
					struct btrfs_ref_path *ref_path)
{
	int ret;

	ret = relocate_one_path(trans, root, path, first_key,
				ref_path, NULL, NULL);
	BUG_ON(ret);

	return 0;
}

static noinline int del_extent_zero(struct btrfs_trans_handle *trans,
				    struct btrfs_root *extent_root,
				    struct btrfs_path *path,
				    struct btrfs_key *extent_key)
{
	int ret;

	ret = btrfs_search_slot(trans, extent_root, extent_key, path, -1, 1);
	if (ret)
		goto out;
	ret = btrfs_del_item(trans, extent_root, path);
out:
	btrfs_release_path(extent_root, path);
	return ret;
}

static noinline struct btrfs_root *read_ref_root(struct btrfs_fs_info *fs_info,
						struct btrfs_ref_path *ref_path)
{
	struct btrfs_key root_key;

	root_key.objectid = ref_path->root_objectid;
	root_key.type = BTRFS_ROOT_ITEM_KEY;
	if (is_cowonly_root(ref_path->root_objectid))
		root_key.offset = 0;
	else
		root_key.offset = (u64)-1;

	return btrfs_read_fs_root_no_name(fs_info, &root_key);
}

static noinline int relocate_one_extent(struct btrfs_root *extent_root,
					struct btrfs_path *path,
					struct btrfs_key *extent_key,
					struct btrfs_block_group_cache *group,
					struct inode *reloc_inode, int pass)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_root *found_root;
	struct btrfs_ref_path *ref_path = NULL;
	struct disk_extent *new_extents = NULL;
	int nr_extents = 0;
	int loops;
	int ret;
	int level;
	struct btrfs_key first_key;
	u64 prev_block = 0;


	trans = btrfs_start_transaction(extent_root, 1);
	BUG_ON(!trans);

	if (extent_key->objectid == 0) {
		ret = del_extent_zero(trans, extent_root, path, extent_key);
		goto out;
	}

	ref_path = kmalloc(sizeof(*ref_path), GFP_NOFS);
	if (!ref_path) {
		ret = -ENOMEM;
		goto out;
	}

	for (loops = 0; ; loops++) {
		if (loops == 0) {
			ret = btrfs_first_ref_path(trans, extent_root, ref_path,
						   extent_key->objectid);
		} else {
			ret = btrfs_next_ref_path(trans, extent_root, ref_path);
		}
		if (ret < 0)
			goto out;
		if (ret > 0)
			break;

		if (ref_path->root_objectid == BTRFS_TREE_LOG_OBJECTID ||
		    ref_path->root_objectid == BTRFS_TREE_RELOC_OBJECTID)
			continue;

		found_root = read_ref_root(extent_root->fs_info, ref_path);
		BUG_ON(!found_root);
		/*
		 * for reference counted tree, only process reference paths
		 * rooted at the latest committed root.
		 */
		if (found_root->ref_cows &&
		    ref_path->root_generation != found_root->root_key.offset)
			continue;

		if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
			if (pass == 0) {
				/*
				 * copy data extents to new locations
				 */
				u64 group_start = group->key.objectid;
				ret = relocate_data_extent(reloc_inode,
							   extent_key,
							   group_start);
				if (ret < 0)
					goto out;
				break;
			}
			level = 0;
		} else {
			level = ref_path->owner_objectid;
		}

		if (prev_block != ref_path->nodes[level]) {
			struct extent_buffer *eb;
			u64 block_start = ref_path->nodes[level];
			u64 block_size = btrfs_level_size(found_root, level);

			eb = read_tree_block(found_root, block_start,
					     block_size, 0);
			btrfs_tree_lock(eb);
			BUG_ON(level != btrfs_header_level(eb));

			if (level == 0)
				btrfs_item_key_to_cpu(eb, &first_key, 0);
			else
				btrfs_node_key_to_cpu(eb, &first_key, 0);

			btrfs_tree_unlock(eb);
			free_extent_buffer(eb);
			prev_block = block_start;
		}

		mutex_lock(&extent_root->fs_info->trans_mutex);
		btrfs_record_root_in_trans(found_root);
		mutex_unlock(&extent_root->fs_info->trans_mutex);
		if (ref_path->owner_objectid >= BTRFS_FIRST_FREE_OBJECTID) {
			/*
			 * try to update data extent references while
			 * keeping metadata shared between snapshots.
			 */
			if (pass == 1) {
				ret = relocate_one_path(trans, found_root,
						path, &first_key, ref_path,
						group, reloc_inode);
				if (ret < 0)
					goto out;
				continue;
			}
			/*
			 * use fallback method to process the remaining
			 * references.
			 */
			if (!new_extents) {
				u64 group_start = group->key.objectid;
				new_extents = kmalloc(sizeof(*new_extents),
						      GFP_NOFS);
				nr_extents = 1;
				ret = get_new_locations(reloc_inode,
							extent_key,
							group_start, 1,
							&new_extents,
							&nr_extents);
				if (ret)
					goto out;
			}
			ret = replace_one_extent(trans, found_root,
						path, extent_key,
						&first_key, ref_path,
						new_extents, nr_extents);
		} else {
			ret = relocate_tree_block(trans, found_root, path,
						  &first_key, ref_path);
		}
		if (ret < 0)
			goto out;
	}
	ret = 0;
out:
	btrfs_end_transaction(trans, extent_root);
	kfree(new_extents);
	kfree(ref_path);
	return ret;
}
#endif

static u64 update_block_group_flags(struct btrfs_root *root, u64 flags)
{
	u64 num_devices;
	u64 stripped = BTRFS_BLOCK_GROUP_RAID0 |
		BTRFS_BLOCK_GROUP_RAID1 | BTRFS_BLOCK_GROUP_RAID10;

	num_devices = root->fs_info->fs_devices->rw_devices;
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
		return flags;
	} else {
		/* they already had raid on here, just return */
		if (flags & stripped)
			return flags;

		stripped |= BTRFS_BLOCK_GROUP_DUP;
		stripped = flags & ~stripped;

		/* switch duplicated blocks with raid1 */
		if (flags & BTRFS_BLOCK_GROUP_DUP)
			return stripped | BTRFS_BLOCK_GROUP_RAID1;

		/* turn single device chunks into raid0 */
		return stripped | BTRFS_BLOCK_GROUP_RAID0;
	}
	return flags;
}

static int set_block_group_ro(struct btrfs_block_group_cache *cache)
{
	struct btrfs_space_info *sinfo = cache->space_info;
	u64 num_bytes;
	int ret = -ENOSPC;

	if (cache->ro)
		return 0;

	spin_lock(&sinfo->lock);
	spin_lock(&cache->lock);
	num_bytes = cache->key.offset - cache->reserved - cache->pinned -
		    cache->bytes_super - btrfs_block_group_used(&cache->item);

	if (sinfo->bytes_used + sinfo->bytes_reserved + sinfo->bytes_pinned +
	    sinfo->bytes_may_use + sinfo->bytes_readonly +
	    cache->reserved_pinned + num_bytes < sinfo->total_bytes) {
		sinfo->bytes_readonly += num_bytes;
		sinfo->bytes_reserved += cache->reserved_pinned;
		cache->reserved_pinned = 0;
		cache->ro = 1;
		ret = 0;
	}
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

	trans = btrfs_join_transaction(root, 1);
	BUG_ON(IS_ERR(trans));

	alloc_flags = update_block_group_flags(root, cache->flags);
	if (alloc_flags != cache->flags)
		do_chunk_alloc(trans, root, 2 * 1024 * 1024, alloc_flags, 1);

	ret = set_block_group_ro(cache);
	if (!ret)
		goto out;
	alloc_flags = get_alloc_profile(root, cache->space_info->flags);
	ret = do_chunk_alloc(trans, root, 2 * 1024 * 1024, alloc_flags, 1);
	if (ret < 0)
		goto out;
	ret = set_block_group_ro(cache);
out:
	btrfs_end_transaction(trans, root);
	return ret;
}

int btrfs_set_block_group_rw(struct btrfs_root *root,
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
	return 0;
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
	int full = 0;
	int ret = 0;

	block_group = btrfs_lookup_block_group(root->fs_info, bytenr);

	/* odd, couldn't find the block group, leave it alone */
	if (!block_group)
		return -1;

	/* no bytes used, we're good */
	if (!btrfs_block_group_used(&block_group->item))
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
	    btrfs_block_group_used(&block_group->item) <
	    space_info->total_bytes)) {
		spin_unlock(&space_info->lock);
		goto out;
	}
	spin_unlock(&space_info->lock);

	/*
	 * ok we don't have enough space, but maybe we have free space on our
	 * devices to allocate new chunks for relocation, so loop through our
	 * alloc devices and guess if we have enough space.  However, if we
	 * were marked as full, then we know there aren't enough chunks, and we
	 * can just return.
	 */
	ret = -1;
	if (full)
		goto out;

	mutex_lock(&root->fs_info->chunk_mutex);
	list_for_each_entry(device, &fs_devices->alloc_list, dev_alloc_list) {
		u64 min_free = btrfs_block_group_used(&block_group->item);
		u64 dev_offset, max_avail;

		/*
		 * check to make sure we can actually find a chunk with enough
		 * space to fit our block group in.
		 */
		if (device->total_bytes > device->bytes_used + min_free) {
			ret = find_free_dev_extent(NULL, device, min_free,
						   &dev_offset, &max_avail);
			if (!ret)
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
		if (space_info->bytes_pinned > 0 ||
		    space_info->bytes_reserved > 0) {
			WARN_ON(1);
			dump_space_info(space_info, 0, 0);
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

	cache_gen = btrfs_super_cache_generation(&root->fs_info->super_copy);
	if (cache_gen != 0 &&
	    btrfs_super_generation(&root->fs_info->super_copy) != cache_gen)
		need_clear = 1;
	if (btrfs_test_opt(root, CLEAR_CACHE))
		need_clear = 1;
	if (!btrfs_test_opt(root, SPACE_CACHE) && cache_gen)
		printk(KERN_INFO "btrfs: disk space caching is enabled\n");

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

		atomic_set(&cache->count, 1);
		spin_lock_init(&cache->lock);
		spin_lock_init(&cache->tree_lock);
		cache->fs_info = info;
		INIT_LIST_HEAD(&cache->list);
		INIT_LIST_HEAD(&cache->cluster_list);

		if (need_clear)
			cache->disk_cache_state = BTRFS_DC_CLEAR;

		/*
		 * we only want to have 32k of ram per block group for keeping
		 * track of free space, and if we pass 1/2 of that we want to
		 * start converting things over to using bitmaps
		 */
		cache->extents_thresh = ((1024 * 32) / 2) /
			sizeof(struct btrfs_free_space);

		read_extent_buffer(leaf, &cache->item,
				   btrfs_item_ptr_offset(leaf, path->slots[0]),
				   sizeof(cache->item));
		memcpy(&cache->key, &found_key, sizeof(found_key));

		key.objectid = found_key.objectid + found_key.offset;
		btrfs_release_path(root, path);
		cache->flags = btrfs_block_group_flags(&cache->item);
		cache->sectorsize = root->sectorsize;

		/*
		 * check for two cases, either we are full, and therefore
		 * don't need to bother with the caching work since we won't
		 * find any space, or we are empty, and we can just add all
		 * the space in and be done with it.  This saves us _alot_ of
		 * time, particularly in the full case.
		 */
		if (found_key.offset == btrfs_block_group_used(&cache->item)) {
			exclude_super_stripes(root, cache);
			cache->last_byte_to_unpin = (u64)-1;
			cache->cached = BTRFS_CACHE_FINISHED;
			free_excluded_extents(root, cache);
		} else if (btrfs_block_group_used(&cache->item) == 0) {
			exclude_super_stripes(root, cache);
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
		BUG_ON(ret);
		cache->space_info = space_info;
		spin_lock(&cache->space_info->lock);
		cache->space_info->bytes_readonly += cache->bytes_super;
		spin_unlock(&cache->space_info->lock);

		__link_block_group(space_info, cache);

		ret = btrfs_add_block_group_cache(root->fs_info, cache);
		BUG_ON(ret);

		set_avail_alloc_bits(root->fs_info, cache->flags);
		if (btrfs_chunk_readonly(root, cache->key.objectid))
			set_block_group_ro(cache);
	}

	list_for_each_entry_rcu(space_info, &root->fs_info->space_info, list) {
		if (!(get_alloc_profile(root, space_info->flags) &
		      (BTRFS_BLOCK_GROUP_RAID10 |
		       BTRFS_BLOCK_GROUP_RAID1 |
		       BTRFS_BLOCK_GROUP_DUP)))
			continue;
		/*
		 * avoid allocating from un-mirrored block group if there are
		 * mirrored block groups.
		 */
		list_for_each_entry(cache, &space_info->block_groups[3], list)
			set_block_group_ro(cache);
		list_for_each_entry(cache, &space_info->block_groups[4], list)
			set_block_group_ro(cache);
	}

	init_global_block_rsv(info);
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
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

	cache->key.objectid = chunk_offset;
	cache->key.offset = size;
	cache->key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	cache->sectorsize = root->sectorsize;
	cache->fs_info = root->fs_info;

	/*
	 * we only want to have 32k of ram per block group for keeping track
	 * of free space, and if we pass 1/2 of that we want to start
	 * converting things over to using bitmaps
	 */
	cache->extents_thresh = ((1024 * 32) / 2) /
		sizeof(struct btrfs_free_space);
	atomic_set(&cache->count, 1);
	spin_lock_init(&cache->lock);
	spin_lock_init(&cache->tree_lock);
	INIT_LIST_HEAD(&cache->list);
	INIT_LIST_HEAD(&cache->cluster_list);

	btrfs_set_block_group_used(&cache->item, bytes_used);
	btrfs_set_block_group_chunk_objectid(&cache->item, chunk_objectid);
	cache->flags = type;
	btrfs_set_block_group_flags(&cache->item, type);

	cache->last_byte_to_unpin = (u64)-1;
	cache->cached = BTRFS_CACHE_FINISHED;
	exclude_super_stripes(root, cache);

	add_new_free_space(cache, root->fs_info, chunk_offset,
			   chunk_offset + size);

	free_excluded_extents(root, cache);

	ret = update_space_info(root->fs_info, cache->flags, size, bytes_used,
				&cache->space_info);
	BUG_ON(ret);

	spin_lock(&cache->space_info->lock);
	cache->space_info->bytes_readonly += cache->bytes_super;
	spin_unlock(&cache->space_info->lock);

	__link_block_group(cache->space_info, cache);

	ret = btrfs_add_block_group_cache(root->fs_info, cache);
	BUG_ON(ret);

	ret = btrfs_insert_item(trans, extent_root, &cache->key, &cache->item,
				sizeof(cache->item));
	BUG_ON(ret);

	set_avail_alloc_bits(extent_root->fs_info, type);

	return 0;
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
	int factor;

	root = root->fs_info->extent_root;

	block_group = btrfs_lookup_block_group(root->fs_info, group_start);
	BUG_ON(!block_group);
	BUG_ON(!block_group->ro);

	memcpy(&key, &block_group->key, sizeof(key));
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
	BUG_ON(!path);

	inode = lookup_free_space_inode(root, block_group, path);
	if (!IS_ERR(inode)) {
		btrfs_orphan_add(trans, inode);
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
		iput(inode);
	}

	key.objectid = BTRFS_FREE_SPACE_OBJECTID;
	key.offset = block_group->key.objectid;
	key.type = 0;

	ret = btrfs_search_slot(trans, tree_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret > 0)
		btrfs_release_path(tree_root, path);
	if (ret == 0) {
		ret = btrfs_del_item(trans, tree_root, path);
		if (ret)
			goto out;
		btrfs_release_path(tree_root, path);
	}

	spin_lock(&root->fs_info->block_group_cache_lock);
	rb_erase(&block_group->cache_node,
		 &root->fs_info->block_group_cache_tree);
	spin_unlock(&root->fs_info->block_group_cache_lock);

	down_write(&block_group->space_info->groups_sem);
	/*
	 * we must use list_del_init so people can check to see if they
	 * are still on the list after taking the semaphore
	 */
	list_del_init(&block_group->list);
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
