// SPDX-License-Identifier: GPL-2.0

#include "ctree.h"
#include "space-info.h"
#include "sysfs.h"
#include "volumes.h"
#include "free-space-cache.h"
#include "ordered-data.h"
#include "transaction.h"
#include "math.h"

u64 btrfs_space_info_used(struct btrfs_space_info *s_info,
			  bool may_use_included)
{
	ASSERT(s_info);
	return s_info->bytes_used + s_info->bytes_reserved +
		s_info->bytes_pinned + s_info->bytes_readonly +
		(may_use_included ? s_info->bytes_may_use : 0);
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

static int create_space_info(struct btrfs_fs_info *info, u64 flags)
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
		kobject_put(&space_info->kobj);
		return ret;
	}

	list_add_rcu(&space_info->list, &info->space_info);
	if (flags & BTRFS_BLOCK_GROUP_DATA)
		info->data_sinfo = space_info;

	return ret;
}

int btrfs_init_space_info(struct btrfs_fs_info *fs_info)
{
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
	ret = create_space_info(fs_info, flags);
	if (ret)
		goto out;

	if (mixed) {
		flags = BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA;
		ret = create_space_info(fs_info, flags);
	} else {
		flags = BTRFS_BLOCK_GROUP_METADATA;
		ret = create_space_info(fs_info, flags);
		if (ret)
			goto out;

		flags = BTRFS_BLOCK_GROUP_DATA;
		ret = create_space_info(fs_info, flags);
	}
out:
	return ret;
}

void btrfs_update_space_info(struct btrfs_fs_info *info, u64 flags,
			     u64 total_bytes, u64 bytes_used,
			     u64 bytes_readonly,
			     struct btrfs_space_info **space_info)
{
	struct btrfs_space_info *found;
	int factor;

	factor = btrfs_bg_type_to_factor(flags);

	found = btrfs_find_space_info(info, flags);
	ASSERT(found);
	spin_lock(&found->lock);
	found->total_bytes += total_bytes;
	found->disk_total += total_bytes * factor;
	found->bytes_used += bytes_used;
	found->disk_used += bytes_used * factor;
	found->bytes_readonly += bytes_readonly;
	if (total_bytes > 0)
		found->full = 0;
	btrfs_space_info_add_new_bytes(info, found,
				       total_bytes - bytes_used -
				       bytes_readonly);
	spin_unlock(&found->lock);
	*space_info = found;
}

struct btrfs_space_info *btrfs_find_space_info(struct btrfs_fs_info *info,
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

static inline u64 calc_global_rsv_need_space(struct btrfs_block_rsv *global)
{
	return (global->size << 1);
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
	int factor;

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
	 * space is actually usable.  For raid56, the space info used
	 * doesn't include the parity drive, so we don't have to
	 * change the math
	 */
	factor = btrfs_bg_type_to_factor(profile);
	avail = div_u64(avail, factor);

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

/*
 * This is for space we already have accounted in space_info->bytes_may_use, so
 * basically when we're returning space from block_rsv's.
 */
void btrfs_space_info_add_old_bytes(struct btrfs_fs_info *fs_info,
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
	btrfs_space_info_update_bytes_may_use(fs_info, space_info, -num_bytes);
	trace_btrfs_space_reservation(fs_info, "space_info",
				      space_info->flags, num_bytes, 0);
	spin_unlock(&space_info->lock);
}

/*
 * This is for newly allocated space that isn't accounted in
 * space_info->bytes_may_use yet.  So if we allocate a chunk or unpin an extent
 * we use this helper.
 */
void btrfs_space_info_add_new_bytes(struct btrfs_fs_info *fs_info,
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
			btrfs_space_info_update_bytes_may_use(fs_info,
							      space_info,
							      ticket->bytes);
			ticket->bytes = 0;
			space_info->tickets_id++;
			wake_up(&ticket->wait);
		} else {
			trace_btrfs_space_reservation(fs_info, "space_info",
						      space_info->flags,
						      num_bytes, 1);
			btrfs_space_info_update_bytes_may_use(fs_info,
							      space_info,
							      num_bytes);
			ticket->bytes -= num_bytes;
			num_bytes = 0;
		}
	}

	if (num_bytes && head == &space_info->priority_tickets) {
		head = &space_info->tickets;
		goto again;
	}
}

#define DUMP_BLOCK_RSV(fs_info, rsv_name)				\
do {									\
	struct btrfs_block_rsv *__rsv = &(fs_info)->rsv_name;		\
	spin_lock(&__rsv->lock);					\
	btrfs_info(fs_info, #rsv_name ": size %llu reserved %llu",	\
		   __rsv->size, __rsv->reserved);			\
	spin_unlock(&__rsv->lock);					\
} while (0)

void btrfs_dump_space_info(struct btrfs_fs_info *fs_info,
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

	DUMP_BLOCK_RSV(fs_info, global_block_rsv);
	DUMP_BLOCK_RSV(fs_info, trans_block_rsv);
	DUMP_BLOCK_RSV(fs_info, chunk_block_rsv);
	DUMP_BLOCK_RSV(fs_info, delayed_block_rsv);
	DUMP_BLOCK_RSV(fs_info, delayed_refs_rsv);

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
		btrfs_start_delalloc_roots(fs_info, nr_items);
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
	u64 dio_bytes;
	u64 async_pages;
	u64 items;
	long time_left;
	unsigned long nr_pages;
	int loops;

	/* Calc the number of the pages we need flush for space reservation */
	items = calc_reclaim_items_nr(fs_info, to_reclaim);
	to_reclaim = items * EXTENT_SIZE_PER_ITEM;

	trans = (struct btrfs_trans_handle *)current->journal_info;
	space_info = btrfs_find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);

	delalloc_bytes = percpu_counter_sum_positive(
						&fs_info->delalloc_bytes);
	dio_bytes = percpu_counter_sum_positive(&fs_info->dio_bytes);
	if (delalloc_bytes == 0 && dio_bytes == 0) {
		if (trans)
			return;
		if (wait_ordered)
			btrfs_wait_ordered_roots(fs_info, items, 0, (u64)-1);
		return;
	}

	/*
	 * If we are doing more ordered than delalloc we need to just wait on
	 * ordered extents, otherwise we'll waste time trying to flush delalloc
	 * that likely won't give us the space back we need.
	 */
	if (dio_bytes > delalloc_bytes)
		wait_ordered = true;

	loops = 0;
	while ((delalloc_bytes || dio_bytes) && loops < 3) {
		nr_pages = min(delalloc_bytes, to_reclaim) >> PAGE_SHIFT;

		/*
		 * Triggers inode writeback for up to nr_pages. This will invoke
		 * ->writepages callback and trigger delalloc filling
		 *  (btrfs_run_delalloc_range()).
		 */
		btrfs_writeback_inodes_sb_nr(fs_info, nr_pages, items);

		/*
		 * We need to wait for the compressed pages to start before
		 * we continue.
		 */
		async_pages = atomic_read(&fs_info->async_delalloc_pages);
		if (!async_pages)
			goto skip_async;

		/*
		 * Calculate how many compressed pages we want to be written
		 * before we continue. I.e if there are more async pages than we
		 * require wait_event will wait until nr_pages are written.
		 */
		if (async_pages <= nr_pages)
			async_pages = 0;
		else
			async_pages -= nr_pages;

		wait_event(fs_info->async_submit_wait,
			   atomic_read(&fs_info->async_delalloc_pages) <=
			   (int)async_pages);
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
		dio_bytes = percpu_counter_sum_positive(&fs_info->dio_bytes);
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
static int may_commit_transaction(struct btrfs_fs_info *fs_info,
				  struct btrfs_space_info *space_info)
{
	struct reserve_ticket *ticket = NULL;
	struct btrfs_block_rsv *delayed_rsv = &fs_info->delayed_block_rsv;
	struct btrfs_block_rsv *delayed_refs_rsv = &fs_info->delayed_refs_rsv;
	struct btrfs_trans_handle *trans;
	u64 bytes_needed;
	u64 reclaim_bytes = 0;

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
	bytes_needed = (ticket) ? ticket->bytes : 0;
	spin_unlock(&space_info->lock);

	if (!bytes_needed)
		return 0;

	trans = btrfs_join_transaction(fs_info->extent_root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);

	/*
	 * See if there is enough pinned space to make this reservation, or if
	 * we have block groups that are going to be freed, allowing us to
	 * possibly do a chunk allocation the next loop through.
	 */
	if (test_bit(BTRFS_TRANS_HAVE_FREE_BGS, &trans->transaction->flags) ||
	    __percpu_counter_compare(&space_info->total_bytes_pinned,
				     bytes_needed,
				     BTRFS_TOTAL_BYTES_PINNED_BATCH) >= 0)
		goto commit;

	/*
	 * See if there is some space in the delayed insertion reservation for
	 * this reservation.
	 */
	if (space_info != delayed_rsv->space_info)
		goto enospc;

	spin_lock(&delayed_rsv->lock);
	reclaim_bytes += delayed_rsv->reserved;
	spin_unlock(&delayed_rsv->lock);

	spin_lock(&delayed_refs_rsv->lock);
	reclaim_bytes += delayed_refs_rsv->reserved;
	spin_unlock(&delayed_refs_rsv->lock);
	if (reclaim_bytes >= bytes_needed)
		goto commit;
	bytes_needed -= reclaim_bytes;

	if (__percpu_counter_compare(&space_info->total_bytes_pinned,
				   bytes_needed,
				   BTRFS_TOTAL_BYTES_PINNED_BATCH) < 0)
		goto enospc;

commit:
	return btrfs_commit_transaction(trans);
enospc:
	btrfs_end_transaction(trans);
	return -ENOSPC;
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
	case FLUSH_DELAYED_REFS_NR:
	case FLUSH_DELAYED_REFS:
		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}
		if (state == FLUSH_DELAYED_REFS_NR)
			nr = calc_reclaim_items_nr(fs_info, num_bytes);
		else
			nr = 0;
		btrfs_run_delayed_refs(trans, nr);
		btrfs_end_transaction(trans);
		break;
	case ALLOC_CHUNK:
	case ALLOC_CHUNK_FORCE:
		trans = btrfs_join_transaction(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			break;
		}
		ret = btrfs_chunk_alloc(trans,
				btrfs_metadata_alloc_profile(fs_info),
				(state == ALLOC_CHUNK) ? CHUNK_ALLOC_NO_FORCE :
					CHUNK_ALLOC_FORCE);
		btrfs_end_transaction(trans);
		if (ret > 0 || ret == -ENOSPC)
			ret = 0;
		break;
	case COMMIT_TRANS:
		/*
		 * If we have pending delayed iputs then we could free up a
		 * bunch of pinned space, so make sure we run the iputs before
		 * we do our pinned bytes check below.
		 */
		btrfs_run_delayed_iputs(fs_info);
		btrfs_wait_on_delayed_iputs(fs_info);

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

static bool wake_all_tickets(struct list_head *head)
{
	struct reserve_ticket *ticket;

	while (!list_empty(head)) {
		ticket = list_first_entry(head, struct reserve_ticket, list);
		list_del_init(&ticket->list);
		ticket->error = -ENOSPC;
		wake_up(&ticket->wait);
		if (ticket->bytes != ticket->orig_bytes)
			return true;
	}
	return false;
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
	space_info = btrfs_find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);

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

		/*
		 * We don't want to force a chunk allocation until we've tried
		 * pretty hard to reclaim space.  Think of the case where we
		 * freed up a bunch of space and so have a lot of pinned space
		 * to reclaim.  We would rather use that than possibly create a
		 * underutilized metadata chunk.  So if this is our first run
		 * through the flushing state machine skip ALLOC_CHUNK_FORCE and
		 * commit the transaction.  If nothing has changed the next go
		 * around then we can force a chunk allocation.
		 */
		if (flush_state == ALLOC_CHUNK_FORCE && !commit_cycles)
			flush_state++;

		if (flush_state > COMMIT_TRANS) {
			commit_cycles++;
			if (commit_cycles > 2) {
				if (wake_all_tickets(&space_info->tickets)) {
					flush_state = FLUSH_DELAYED_ITEMS_NR;
					commit_cycles--;
				} else {
					space_info->flush = 0;
				}
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

static const enum btrfs_flush_state priority_flush_states[] = {
	FLUSH_DELAYED_ITEMS_NR,
	FLUSH_DELAYED_ITEMS,
	ALLOC_CHUNK,
};

static void priority_reclaim_metadata_space(struct btrfs_fs_info *fs_info,
					    struct btrfs_space_info *space_info,
					    struct reserve_ticket *ticket)
{
	u64 to_reclaim;
	int flush_state;

	spin_lock(&space_info->lock);
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info, space_info,
						      false);
	if (!to_reclaim) {
		spin_unlock(&space_info->lock);
		return;
	}
	spin_unlock(&space_info->lock);

	flush_state = 0;
	do {
		flush_space(fs_info, space_info, to_reclaim,
			    priority_flush_states[flush_state]);
		flush_state++;
		spin_lock(&space_info->lock);
		if (ticket->bytes == 0) {
			spin_unlock(&space_info->lock);
			return;
		}
		spin_unlock(&space_info->lock);
	} while (flush_state < ARRAY_SIZE(priority_flush_states));
}

static int wait_reserve_ticket(struct btrfs_fs_info *fs_info,
			       struct btrfs_space_info *space_info,
			       struct reserve_ticket *ticket)

{
	DEFINE_WAIT(wait);
	u64 reclaim_bytes = 0;
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
	if (ticket->bytes && ticket->bytes < ticket->orig_bytes)
		reclaim_bytes = ticket->orig_bytes - ticket->bytes;
	spin_unlock(&space_info->lock);

	if (reclaim_bytes)
		btrfs_space_info_add_old_bytes(fs_info, space_info,
					       reclaim_bytes);
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
	u64 reclaim_bytes = 0;
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
		btrfs_space_info_update_bytes_may_use(fs_info, space_info,
						      orig_bytes);
		trace_btrfs_space_reservation(fs_info, "space_info",
					      space_info->flags, orig_bytes, 1);
		ret = 0;
	} else if (can_overcommit(fs_info, space_info, orig_bytes, flush,
				  system_chunk)) {
		btrfs_space_info_update_bytes_may_use(fs_info, space_info,
						      orig_bytes);
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
		ticket.orig_bytes = orig_bytes;
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
		return wait_reserve_ticket(fs_info, space_info, &ticket);

	ret = 0;
	priority_reclaim_metadata_space(fs_info, space_info, &ticket);
	spin_lock(&space_info->lock);
	if (ticket.bytes) {
		if (ticket.bytes < orig_bytes)
			reclaim_bytes = orig_bytes - ticket.bytes;
		list_del_init(&ticket.list);
		ret = -ENOSPC;
	}
	spin_unlock(&space_info->lock);

	if (reclaim_bytes)
		btrfs_space_info_add_old_bytes(fs_info, space_info,
					       reclaim_bytes);
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
 * This will reserve orig_bytes number of bytes from the space info associated
 * with the block_rsv.  If there is not enough space it will make an attempt to
 * flush out space to make room.  It will do this by flushing delalloc if
 * possible or committing the transaction.  If flush is 0 then no attempts to
 * regain reservations will be made and this will fail if there is not enough
 * space already.
 */
int btrfs_reserve_metadata_bytes(struct btrfs_root *root,
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
		    !btrfs_block_rsv_use_bytes(global_rsv, orig_bytes))
			ret = 0;
	}
	if (ret == -ENOSPC) {
		trace_btrfs_space_reservation(fs_info, "space_info:enospc",
					      block_rsv->space_info->flags,
					      orig_bytes, 1);

		if (btrfs_test_opt(fs_info, ENOSPC_DEBUG))
			btrfs_dump_space_info(fs_info, block_rsv->space_info,
					      orig_bytes, 0);
	}
	return ret;
}
