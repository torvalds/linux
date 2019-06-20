// SPDX-License-Identifier: GPL-2.0

#include "ctree.h"
#include "block-group.h"
#include "space-info.h"
#include "disk-io.h"
#include "free-space-cache.h"
#include "free-space-tree.h"
#include "disk-io.h"
#include "volumes.h"
#include "transaction.h"
#include "ref-verify.h"

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
 * This will return the block group at or after bytenr if contains is 0, else
 * it will return the block group that contains the bytenr
 */
static struct btrfs_block_group_cache *block_group_cache_tree_search(
		struct btrfs_fs_info *info, u64 bytenr, int contains)
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

/*
 * Return the block group that starts at or after bytenr
 */
struct btrfs_block_group_cache *btrfs_lookup_first_block_group(
		struct btrfs_fs_info *info, u64 bytenr)
{
	return block_group_cache_tree_search(info, bytenr, 0);
}

/*
 * Return the block group that contains the given bytenr
 */
struct btrfs_block_group_cache *btrfs_lookup_block_group(
		struct btrfs_fs_info *info, u64 bytenr)
{
	return block_group_cache_tree_search(info, bytenr, 1);
}

struct btrfs_block_group_cache *btrfs_next_block_group(
		struct btrfs_block_group_cache *cache)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;
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

	/* No put on block group, done by btrfs_dec_nocow_writers */
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
		wake_up_var(&bg->nocow_writers);
	/*
	 * Once for our lookup and once for the lookup done by a previous call
	 * to btrfs_inc_nocow_writers()
	 */
	btrfs_put_block_group(bg);
	btrfs_put_block_group(bg);
}

void btrfs_wait_nocow_writers(struct btrfs_block_group_cache *bg)
{
	wait_var_event(&bg->nocow_writers, !atomic_read(&bg->nocow_writers));
}

void btrfs_dec_block_group_reservations(struct btrfs_fs_info *fs_info,
					const u64 start)
{
	struct btrfs_block_group_cache *bg;

	bg = btrfs_lookup_block_group(fs_info, start);
	ASSERT(bg);
	if (atomic_dec_and_test(&bg->reservations))
		wake_up_var(&bg->reservations);
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

	wait_var_event(&bg->reservations, !atomic_read(&bg->reservations));
}

struct btrfs_caching_control *btrfs_get_caching_control(
		struct btrfs_block_group_cache *cache)
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

void btrfs_put_caching_control(struct btrfs_caching_control *ctl)
{
	if (refcount_dec_and_test(&ctl->count))
		kfree(ctl);
}

/*
 * When we wait for progress in the block group caching, its because our
 * allocation attempt failed at least once.  So, we must sleep and let some
 * progress happen before we try again.
 *
 * This function will sleep at least once waiting for new free space to show
 * up, and then it will check the block group free space numbers for our min
 * num_bytes.  Another option is to have it go ahead and look in the rbtree for
 * a free extent of a given size, but this is a good start.
 *
 * Callers of this must check if cache->cached == BTRFS_CACHE_ERROR before using
 * any of the information in this block group.
 */
void btrfs_wait_block_group_cache_progress(struct btrfs_block_group_cache *cache,
					   u64 num_bytes)
{
	struct btrfs_caching_control *caching_ctl;

	caching_ctl = btrfs_get_caching_control(cache);
	if (!caching_ctl)
		return;

	wait_event(caching_ctl->wait, btrfs_block_group_cache_done(cache) ||
		   (cache->free_space_ctl->free_space >= num_bytes));

	btrfs_put_caching_control(caching_ctl);
}

int btrfs_wait_block_group_cache_done(struct btrfs_block_group_cache *cache)
{
	struct btrfs_caching_control *caching_ctl;
	int ret = 0;

	caching_ctl = btrfs_get_caching_control(cache);
	if (!caching_ctl)
		return (cache->cached == BTRFS_CACHE_ERROR) ? -EIO : 0;

	wait_event(caching_ctl->wait, btrfs_block_group_cache_done(cache));
	if (cache->cached == BTRFS_CACHE_ERROR)
		ret = -EIO;
	btrfs_put_caching_control(caching_ctl);
	return ret;
}

#ifdef CONFIG_BTRFS_DEBUG
void btrfs_fragment_free_space(struct btrfs_block_group_cache *block_group)
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
 * This is only called by btrfs_cache_block_group, since we could have freed
 * extents we need to check the pinned_extents for any extents that can't be
 * used yet since their free space will be released as soon as the transaction
 * commits.
 */
u64 add_new_free_space(struct btrfs_block_group_cache *block_group,
		       u64 start, u64 end)
{
	struct btrfs_fs_info *info = block_group->fs_info;
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
			ret = btrfs_find_next_key(extent_root, path, &key, 0, 0);
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
			total_found += add_new_free_space(block_group, last,
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

	total_found += add_new_free_space(block_group, last,
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
		btrfs_fragment_free_space(block_group);
	}
#endif

	caching_ctl->progress = (u64)-1;

	up_read(&fs_info->commit_root_sem);
	btrfs_free_excluded_extents(block_group);
	mutex_unlock(&caching_ctl->mutex);

	wake_up(&caching_ctl->wait);

	btrfs_put_caching_control(caching_ctl);
	btrfs_put_block_group(block_group);
}

int btrfs_cache_block_group(struct btrfs_block_group_cache *cache,
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
		btrfs_put_caching_control(ctl);
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
		ret = load_free_space_cache(cache);

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
			btrfs_fragment_free_space(cache);
		}
#endif
		mutex_unlock(&caching_ctl->mutex);

		wake_up(&caching_ctl->wait);
		if (ret == 1) {
			btrfs_put_caching_control(caching_ctl);
			btrfs_free_excluded_extents(cache);
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
		btrfs_put_caching_control(caching_ctl);
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

/*
 * Clear incompat bits for the following feature(s):
 *
 * - RAID56 - in case there's neither RAID5 nor RAID6 profile block group
 *            in the whole filesystem
 */
static void clear_incompat_bg_bits(struct btrfs_fs_info *fs_info, u64 flags)
{
	if (flags & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		struct list_head *head = &fs_info->space_info;
		struct btrfs_space_info *sinfo;

		list_for_each_entry_rcu(sinfo, head, list) {
			bool found = false;

			down_read(&sinfo->groups_sem);
			if (!list_empty(&sinfo->block_groups[BTRFS_RAID_RAID5]))
				found = true;
			if (!list_empty(&sinfo->block_groups[BTRFS_RAID_RAID6]))
				found = true;
			up_read(&sinfo->groups_sem);

			if (found)
				return;
		}
		btrfs_clear_fs_incompat(fs_info, RAID56);
	}
}

int btrfs_remove_block_group(struct btrfs_trans_handle *trans,
			     u64 group_start, struct extent_map *em)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
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
	bool remove_rsv = false;

	block_group = btrfs_lookup_block_group(fs_info, group_start);
	BUG_ON(!block_group);
	BUG_ON(!block_group->ro);

	trace_btrfs_remove_block_group(block_group);
	/*
	 * Free the reserved super bytes from this block group before
	 * remove it.
	 */
	btrfs_free_excluded_extents(block_group);
	btrfs_free_ref_tree_range(fs_info, block_group->key.objectid,
				  block_group->key.offset);

	memcpy(&key, &block_group->key, sizeof(key));
	index = btrfs_bg_flags_to_raid_index(block_group->flags);
	factor = btrfs_bg_type_to_factor(block_group->flags);

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
	inode = lookup_free_space_inode(block_group, path);

	mutex_lock(&trans->transaction->cache_write_mutex);
	/*
	 * Make sure our free space cache IO is done before removing the
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
		remove_rsv = true;
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
	clear_incompat_bg_bits(fs_info, block_group->flags);
	if (kobj) {
		kobject_del(kobj);
		kobject_put(kobj);
	}

	if (block_group->has_caching_ctl)
		caching_ctl = btrfs_get_caching_control(block_group);
	if (block_group->cached == BTRFS_CACHE_STARTED)
		btrfs_wait_block_group_cache_done(block_group);
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
			btrfs_put_caching_control(caching_ctl);
			btrfs_put_caching_control(caching_ctl);
		}
	}

	spin_lock(&trans->transaction->dirty_bgs_lock);
	WARN_ON(!list_empty(&block_group->dirty_list));
	WARN_ON(!list_empty(&block_group->io_list));
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
	spin_unlock(&block_group->lock);

	mutex_unlock(&fs_info->chunk_mutex);

	ret = remove_block_group_free_space(trans, block_group);
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
	if (ret)
		goto out;

	if (remove_em) {
		struct extent_map_tree *em_tree;

		em_tree = &fs_info->mapping_tree;
		write_lock(&em_tree->lock);
		remove_extent_mapping(em_tree, em);
		write_unlock(&em_tree->lock);
		/* once for the tree */
		free_extent_map(em);
	}
out:
	if (remove_rsv)
		btrfs_delayed_refs_rsv_release(fs_info, 1);
	btrfs_free_path(path);
	return ret;
}

struct btrfs_trans_handle *btrfs_start_trans_remove_block_group(
		struct btrfs_fs_info *fs_info, const u64 chunk_offset)
{
	struct extent_map_tree *em_tree = &fs_info->mapping_tree;
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
		if (block_group->reserved || block_group->pinned ||
		    btrfs_block_group_used(&block_group->item) ||
		    block_group->ro ||
		    list_is_singular(&block_group->list)) {
			/*
			 * We want to bail if we made new allocations or have
			 * outstanding allocations in this block group.  We do
			 * the ro check in case balance is currently acting on
			 * this block group.
			 */
			trace_btrfs_skip_unused_block_group(block_group);
			spin_unlock(&block_group->lock);
			up_write(&space_info->groups_sem);
			goto next;
		}
		spin_unlock(&block_group->lock);

		/* We don't want to force the issue, only flip if it's ok. */
		ret = __btrfs_inc_block_group_ro(block_group, 0);
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

		btrfs_space_info_update_bytes_pinned(fs_info, space_info,
						     -block_group->pinned);
		space_info->bytes_readonly += block_group->pinned;
		percpu_counter_add_batch(&space_info->total_bytes_pinned,
				   -block_group->pinned,
				   BTRFS_TOTAL_BYTES_PINNED_BATCH);
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
		ret = btrfs_remove_chunk(trans, block_group->key.objectid);

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

void btrfs_mark_bg_unused(struct btrfs_block_group_cache *bg)
{
	struct btrfs_fs_info *fs_info = bg->fs_info;

	spin_lock(&fs_info->unused_bgs_lock);
	if (list_empty(&bg->bg_list)) {
		btrfs_get_block_group(bg);
		trace_btrfs_add_unused_block_group(bg);
		list_add_tail(&bg->bg_list, &fs_info->unused_bgs);
	}
	spin_unlock(&fs_info->unused_bgs_lock);
}
