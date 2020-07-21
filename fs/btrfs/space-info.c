// SPDX-License-Identifier: GPL-2.0

#include "misc.h"
#include "ctree.h"
#include "space-info.h"
#include "sysfs.h"
#include "volumes.h"
#include "free-space-cache.h"
#include "ordered-data.h"
#include "transaction.h"
#include "block-group.h"

/*
 * HOW DOES SPACE RESERVATION WORK
 *
 * If you want to know about delalloc specifically, there is a separate comment
 * for that with the delalloc code.  This comment is about how the whole system
 * works generally.
 *
 * BASIC CONCEPTS
 *
 *   1) space_info.  This is the ultimate arbiter of how much space we can use.
 *   There's a description of the bytes_ fields with the struct declaration,
 *   refer to that for specifics on each field.  Suffice it to say that for
 *   reservations we care about total_bytes - SUM(space_info->bytes_) when
 *   determining if there is space to make an allocation.  There is a space_info
 *   for METADATA, SYSTEM, and DATA areas.
 *
 *   2) block_rsv's.  These are basically buckets for every different type of
 *   metadata reservation we have.  You can see the comment in the block_rsv
 *   code on the rules for each type, but generally block_rsv->reserved is how
 *   much space is accounted for in space_info->bytes_may_use.
 *
 *   3) btrfs_calc*_size.  These are the worst case calculations we used based
 *   on the number of items we will want to modify.  We have one for changing
 *   items, and one for inserting new items.  Generally we use these helpers to
 *   determine the size of the block reserves, and then use the actual bytes
 *   values to adjust the space_info counters.
 *
 * MAKING RESERVATIONS, THE NORMAL CASE
 *
 *   We call into either btrfs_reserve_data_bytes() or
 *   btrfs_reserve_metadata_bytes(), depending on which we're looking for, with
 *   num_bytes we want to reserve.
 *
 *   ->reserve
 *     space_info->bytes_may_reserve += num_bytes
 *
 *   ->extent allocation
 *     Call btrfs_add_reserved_bytes() which does
 *     space_info->bytes_may_reserve -= num_bytes
 *     space_info->bytes_reserved += extent_bytes
 *
 *   ->insert reference
 *     Call btrfs_update_block_group() which does
 *     space_info->bytes_reserved -= extent_bytes
 *     space_info->bytes_used += extent_bytes
 *
 * MAKING RESERVATIONS, FLUSHING NORMALLY (non-priority)
 *
 *   Assume we are unable to simply make the reservation because we do not have
 *   enough space
 *
 *   -> __reserve_bytes
 *     create a reserve_ticket with ->bytes set to our reservation, add it to
 *     the tail of space_info->tickets, kick async flush thread
 *
 *   ->handle_reserve_ticket
 *     wait on ticket->wait for ->bytes to be reduced to 0, or ->error to be set
 *     on the ticket.
 *
 *   -> btrfs_async_reclaim_metadata_space/btrfs_async_reclaim_data_space
 *     Flushes various things attempting to free up space.
 *
 *   -> btrfs_try_granting_tickets()
 *     This is called by anything that either subtracts space from
 *     space_info->bytes_may_use, ->bytes_pinned, etc, or adds to the
 *     space_info->total_bytes.  This loops through the ->priority_tickets and
 *     then the ->tickets list checking to see if the reservation can be
 *     completed.  If it can the space is added to space_info->bytes_may_use and
 *     the ticket is woken up.
 *
 *   -> ticket wakeup
 *     Check if ->bytes == 0, if it does we got our reservation and we can carry
 *     on, if not return the appropriate error (ENOSPC, but can be EINTR if we
 *     were interrupted.)
 *
 * MAKING RESERVATIONS, FLUSHING HIGH PRIORITY
 *
 *   Same as the above, except we add ourselves to the
 *   space_info->priority_tickets, and we do not use ticket->wait, we simply
 *   call flush_space() ourselves for the states that are safe for us to call
 *   without deadlocking and hope for the best.
 *
 * THE FLUSHING STATES
 *
 *   Generally speaking we will have two cases for each state, a "nice" state
 *   and a "ALL THE THINGS" state.  In btrfs we delay a lot of work in order to
 *   reduce the locking over head on the various trees, and even to keep from
 *   doing any work at all in the case of delayed refs.  Each of these delayed
 *   things however hold reservations, and so letting them run allows us to
 *   reclaim space so we can make new reservations.
 *
 *   FLUSH_DELAYED_ITEMS
 *     Every inode has a delayed item to update the inode.  Take a simple write
 *     for example, we would update the inode item at write time to update the
 *     mtime, and then again at finish_ordered_io() time in order to update the
 *     isize or bytes.  We keep these delayed items to coalesce these operations
 *     into a single operation done on demand.  These are an easy way to reclaim
 *     metadata space.
 *
 *   FLUSH_DELALLOC
 *     Look at the delalloc comment to get an idea of how much space is reserved
 *     for delayed allocation.  We can reclaim some of this space simply by
 *     running delalloc, but usually we need to wait for ordered extents to
 *     reclaim the bulk of this space.
 *
 *   FLUSH_DELAYED_REFS
 *     We have a block reserve for the outstanding delayed refs space, and every
 *     delayed ref operation holds a reservation.  Running these is a quick way
 *     to reclaim space, but we want to hold this until the end because COW can
 *     churn a lot and we can avoid making some extent tree modifications if we
 *     are able to delay for as long as possible.
 *
 *   ALLOC_CHUNK
 *     We will skip this the first time through space reservation, because of
 *     overcommit and we don't want to have a lot of useless metadata space when
 *     our worst case reservations will likely never come true.
 *
 *   RUN_DELAYED_IPUTS
 *     If we're freeing inodes we're likely freeing checksums, file extent
 *     items, and extent tree items.  Loads of space could be freed up by these
 *     operations, however they won't be usable until the transaction commits.
 *
 *   COMMIT_TRANS
 *     may_commit_transaction() is the ultimate arbiter on whether we commit the
 *     transaction or not.  In order to avoid constantly churning we do all the
 *     above flushing first and then commit the transaction as the last resort.
 *     However we need to take into account things like pinned space that would
 *     be freed, plus any delayed work we may not have gotten rid of in the case
 *     of metadata.
 *
 * OVERCOMMIT
 *
 *   Because we hold so many reservations for metadata we will allow you to
 *   reserve more space than is currently free in the currently allocate
 *   metadata space.  This only happens with metadata, data does not allow
 *   overcommitting.
 *
 *   You can see the current logic for when we allow overcommit in
 *   btrfs_can_overcommit(), but it only applies to unallocated space.  If there
 *   is no unallocated space to be had, all reservations are kept within the
 *   free space in the allocated metadata chunks.
 *
 *   Because of overcommitting, you generally want to use the
 *   btrfs_can_overcommit() logic for metadata allocations, as it does the right
 *   thing with or without extra unallocated space.
 */

u64 __pure btrfs_space_info_used(struct btrfs_space_info *s_info,
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
	INIT_LIST_HEAD(&space_info->ro_bgs);
	INIT_LIST_HEAD(&space_info->tickets);
	INIT_LIST_HEAD(&space_info->priority_tickets);

	ret = btrfs_sysfs_add_space_info_type(info, space_info);
	if (ret)
		return ret;

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
	btrfs_try_granting_tickets(info, found);
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

static u64 calc_available_free_space(struct btrfs_fs_info *fs_info,
			  struct btrfs_space_info *space_info,
			  enum btrfs_reserve_flush_enum flush)
{
	u64 profile;
	u64 avail;
	int factor;

	if (space_info->flags & BTRFS_BLOCK_GROUP_SYSTEM)
		profile = btrfs_system_alloc_profile(fs_info);
	else
		profile = btrfs_metadata_alloc_profile(fs_info);

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
	return avail;
}

int btrfs_can_overcommit(struct btrfs_fs_info *fs_info,
			 struct btrfs_space_info *space_info, u64 bytes,
			 enum btrfs_reserve_flush_enum flush)
{
	u64 avail;
	u64 used;

	/* Don't overcommit when in mixed mode */
	if (space_info->flags & BTRFS_BLOCK_GROUP_DATA)
		return 0;

	used = btrfs_space_info_used(space_info, true);
	avail = calc_available_free_space(fs_info, space_info, flush);

	if (used + bytes < space_info->total_bytes + avail)
		return 1;
	return 0;
}

static void remove_ticket(struct btrfs_space_info *space_info,
			  struct reserve_ticket *ticket)
{
	if (!list_empty(&ticket->list)) {
		list_del_init(&ticket->list);
		ASSERT(space_info->reclaim_size >= ticket->bytes);
		space_info->reclaim_size -= ticket->bytes;
	}
}

/*
 * This is for space we already have accounted in space_info->bytes_may_use, so
 * basically when we're returning space from block_rsv's.
 */
void btrfs_try_granting_tickets(struct btrfs_fs_info *fs_info,
				struct btrfs_space_info *space_info)
{
	struct list_head *head;
	enum btrfs_reserve_flush_enum flush = BTRFS_RESERVE_NO_FLUSH;

	lockdep_assert_held(&space_info->lock);

	head = &space_info->priority_tickets;
again:
	while (!list_empty(head)) {
		struct reserve_ticket *ticket;
		u64 used = btrfs_space_info_used(space_info, true);

		ticket = list_first_entry(head, struct reserve_ticket, list);

		/* Check and see if our ticket can be satisified now. */
		if ((used + ticket->bytes <= space_info->total_bytes) ||
		    btrfs_can_overcommit(fs_info, space_info, ticket->bytes,
					 flush)) {
			btrfs_space_info_update_bytes_may_use(fs_info,
							      space_info,
							      ticket->bytes);
			remove_ticket(space_info, ticket);
			ticket->bytes = 0;
			space_info->tickets_id++;
			wake_up(&ticket->wait);
		} else {
			break;
		}
	}

	if (head == &space_info->priority_tickets) {
		head = &space_info->tickets;
		flush = BTRFS_RESERVE_FLUSH_ALL;
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

static void __btrfs_dump_space_info(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *info)
{
	lockdep_assert_held(&info->lock);

	btrfs_info(fs_info, "space_info %llu has %llu free, is %sfull",
		   info->flags,
		   info->total_bytes - btrfs_space_info_used(info, true),
		   info->full ? "" : "not ");
	btrfs_info(fs_info,
		"space_info total=%llu, used=%llu, pinned=%llu, reserved=%llu, may_use=%llu, readonly=%llu",
		info->total_bytes, info->bytes_used, info->bytes_pinned,
		info->bytes_reserved, info->bytes_may_use,
		info->bytes_readonly);

	DUMP_BLOCK_RSV(fs_info, global_block_rsv);
	DUMP_BLOCK_RSV(fs_info, trans_block_rsv);
	DUMP_BLOCK_RSV(fs_info, chunk_block_rsv);
	DUMP_BLOCK_RSV(fs_info, delayed_block_rsv);
	DUMP_BLOCK_RSV(fs_info, delayed_refs_rsv);

}

void btrfs_dump_space_info(struct btrfs_fs_info *fs_info,
			   struct btrfs_space_info *info, u64 bytes,
			   int dump_block_groups)
{
	struct btrfs_block_group *cache;
	int index = 0;

	spin_lock(&info->lock);
	__btrfs_dump_space_info(fs_info, info);
	spin_unlock(&info->lock);

	if (!dump_block_groups)
		return;

	down_read(&info->groups_sem);
again:
	list_for_each_entry(cache, &info->block_groups[index], list) {
		spin_lock(&cache->lock);
		btrfs_info(fs_info,
			"block group %llu has %llu bytes, %llu used %llu pinned %llu reserved %s",
			cache->start, cache->length, cache->used, cache->pinned,
			cache->reserved, cache->ro ? "[readonly]" : "");
		spin_unlock(&cache->lock);
		btrfs_dump_free_space(cache, bytes);
	}
	if (++index < BTRFS_NR_RAID_TYPES)
		goto again;
	up_read(&info->groups_sem);
}

static inline u64 calc_reclaim_items_nr(struct btrfs_fs_info *fs_info,
					u64 to_reclaim)
{
	u64 bytes;
	u64 nr;

	bytes = btrfs_calc_insert_metadata_size(fs_info, 1);
	nr = div64_u64(to_reclaim, bytes);
	if (!nr)
		nr = 1;
	return nr;
}

#define EXTENT_SIZE_PER_ITEM	SZ_256K

/*
 * shrink metadata reservation for delalloc
 */
static void shrink_delalloc(struct btrfs_fs_info *fs_info,
			    struct btrfs_space_info *space_info,
			    u64 to_reclaim, bool wait_ordered)
{
	struct btrfs_trans_handle *trans;
	u64 delalloc_bytes;
	u64 dio_bytes;
	u64 items;
	long time_left;
	int loops;

	/* Calc the number of the pages we need flush for space reservation */
	if (to_reclaim == U64_MAX) {
		items = U64_MAX;
	} else {
		/*
		 * to_reclaim is set to however much metadata we need to
		 * reclaim, but reclaiming that much data doesn't really track
		 * exactly, so increase the amount to reclaim by 2x in order to
		 * make sure we're flushing enough delalloc to hopefully reclaim
		 * some metadata reservations.
		 */
		items = calc_reclaim_items_nr(fs_info, to_reclaim) * 2;
		to_reclaim = items * EXTENT_SIZE_PER_ITEM;
	}

	trans = (struct btrfs_trans_handle *)current->journal_info;

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
		btrfs_start_delalloc_roots(fs_info, items);

		loops++;
		if (wait_ordered && !trans) {
			btrfs_wait_ordered_roots(fs_info, items, 0, (u64)-1);
		} else {
			time_left = schedule_timeout_killable(1);
			if (time_left)
				break;
		}

		spin_lock(&space_info->lock);
		if (list_empty(&space_info->tickets) &&
		    list_empty(&space_info->priority_tickets)) {
			spin_unlock(&space_info->lock);
			break;
		}
		spin_unlock(&space_info->lock);

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
	struct btrfs_block_rsv *trans_rsv = &fs_info->trans_block_rsv;
	struct btrfs_trans_handle *trans;
	u64 bytes_needed;
	u64 reclaim_bytes = 0;
	u64 cur_free_bytes = 0;

	trans = (struct btrfs_trans_handle *)current->journal_info;
	if (trans)
		return -EAGAIN;

	spin_lock(&space_info->lock);
	cur_free_bytes = btrfs_space_info_used(space_info, true);
	if (cur_free_bytes < space_info->total_bytes)
		cur_free_bytes = space_info->total_bytes - cur_free_bytes;
	else
		cur_free_bytes = 0;

	if (!list_empty(&space_info->priority_tickets))
		ticket = list_first_entry(&space_info->priority_tickets,
					  struct reserve_ticket, list);
	else if (!list_empty(&space_info->tickets))
		ticket = list_first_entry(&space_info->tickets,
					  struct reserve_ticket, list);
	bytes_needed = (ticket) ? ticket->bytes : 0;

	if (bytes_needed > cur_free_bytes)
		bytes_needed -= cur_free_bytes;
	else
		bytes_needed = 0;
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

	spin_lock(&trans_rsv->lock);
	reclaim_bytes += trans_rsv->reserved;
	spin_unlock(&trans_rsv->lock);

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
		shrink_delalloc(fs_info, space_info, num_bytes,
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
				btrfs_get_alloc_profile(fs_info, space_info->flags),
				(state == ALLOC_CHUNK) ? CHUNK_ALLOC_NO_FORCE :
					CHUNK_ALLOC_FORCE);
		btrfs_end_transaction(trans);
		if (ret > 0 || ret == -ENOSPC)
			ret = 0;
		break;
	case RUN_DELAYED_IPUTS:
		/*
		 * If we have pending delayed iputs then we could free up a
		 * bunch of pinned space, so make sure we run the iputs before
		 * we do our pinned bytes check below.
		 */
		btrfs_run_delayed_iputs(fs_info);
		btrfs_wait_on_delayed_iputs(fs_info);
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
				 struct btrfs_space_info *space_info)
{
	u64 used;
	u64 avail;
	u64 expected;
	u64 to_reclaim = space_info->reclaim_size;

	lockdep_assert_held(&space_info->lock);

	avail = calc_available_free_space(fs_info, space_info,
					  BTRFS_RESERVE_FLUSH_ALL);
	used = btrfs_space_info_used(space_info, true);

	/*
	 * We may be flushing because suddenly we have less space than we had
	 * before, and now we're well over-committed based on our current free
	 * space.  If that's the case add in our overage so we make sure to put
	 * appropriate pressure on the flushing state machine.
	 */
	if (space_info->total_bytes + avail < used)
		to_reclaim += used - (space_info->total_bytes + avail);

	if (to_reclaim)
		return to_reclaim;

	to_reclaim = min_t(u64, num_online_cpus() * SZ_1M, SZ_16M);
	if (btrfs_can_overcommit(fs_info, space_info, to_reclaim,
				 BTRFS_RESERVE_FLUSH_ALL))
		return 0;

	used = btrfs_space_info_used(space_info, true);

	if (btrfs_can_overcommit(fs_info, space_info, SZ_1M,
				 BTRFS_RESERVE_FLUSH_ALL))
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
					u64 used)
{
	u64 thresh = div_factor_fine(space_info->total_bytes, 98);

	/* If we're just plain full then async reclaim just slows us down. */
	if ((space_info->bytes_used + space_info->bytes_reserved) >= thresh)
		return 0;

	if (!btrfs_calc_reclaim_metadata_size(fs_info, space_info))
		return 0;

	return (used >= thresh && !btrfs_fs_closing(fs_info) &&
		!test_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state));
}

static bool steal_from_global_rsv(struct btrfs_fs_info *fs_info,
				  struct btrfs_space_info *space_info,
				  struct reserve_ticket *ticket)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 min_bytes;

	if (global_rsv->space_info != space_info)
		return false;

	spin_lock(&global_rsv->lock);
	min_bytes = div_factor(global_rsv->size, 1);
	if (global_rsv->reserved < min_bytes + ticket->bytes) {
		spin_unlock(&global_rsv->lock);
		return false;
	}
	global_rsv->reserved -= ticket->bytes;
	remove_ticket(space_info, ticket);
	ticket->bytes = 0;
	wake_up(&ticket->wait);
	space_info->tickets_id++;
	if (global_rsv->reserved < global_rsv->size)
		global_rsv->full = 0;
	spin_unlock(&global_rsv->lock);

	return true;
}

/*
 * maybe_fail_all_tickets - we've exhausted our flushing, start failing tickets
 * @fs_info - fs_info for this fs
 * @space_info - the space info we were flushing
 *
 * We call this when we've exhausted our flushing ability and haven't made
 * progress in satisfying tickets.  The reservation code handles tickets in
 * order, so if there is a large ticket first and then smaller ones we could
 * very well satisfy the smaller tickets.  This will attempt to wake up any
 * tickets in the list to catch this case.
 *
 * This function returns true if it was able to make progress by clearing out
 * other tickets, or if it stumbles across a ticket that was smaller than the
 * first ticket.
 */
static bool maybe_fail_all_tickets(struct btrfs_fs_info *fs_info,
				   struct btrfs_space_info *space_info)
{
	struct reserve_ticket *ticket;
	u64 tickets_id = space_info->tickets_id;
	u64 first_ticket_bytes = 0;

	if (btrfs_test_opt(fs_info, ENOSPC_DEBUG)) {
		btrfs_info(fs_info, "cannot satisfy tickets, dumping space info");
		__btrfs_dump_space_info(fs_info, space_info);
	}

	while (!list_empty(&space_info->tickets) &&
	       tickets_id == space_info->tickets_id) {
		ticket = list_first_entry(&space_info->tickets,
					  struct reserve_ticket, list);

		if (ticket->steal &&
		    steal_from_global_rsv(fs_info, space_info, ticket))
			return true;

		/*
		 * may_commit_transaction will avoid committing the transaction
		 * if it doesn't feel like the space reclaimed by the commit
		 * would result in the ticket succeeding.  However if we have a
		 * smaller ticket in the queue it may be small enough to be
		 * satisified by committing the transaction, so if any
		 * subsequent ticket is smaller than the first ticket go ahead
		 * and send us back for another loop through the enospc flushing
		 * code.
		 */
		if (first_ticket_bytes == 0)
			first_ticket_bytes = ticket->bytes;
		else if (first_ticket_bytes > ticket->bytes)
			return true;

		if (btrfs_test_opt(fs_info, ENOSPC_DEBUG))
			btrfs_info(fs_info, "failing ticket with %llu bytes",
				   ticket->bytes);

		remove_ticket(space_info, ticket);
		ticket->error = -ENOSPC;
		wake_up(&ticket->wait);

		/*
		 * We're just throwing tickets away, so more flushing may not
		 * trip over btrfs_try_granting_tickets, so we need to call it
		 * here to see if we can make progress with the next ticket in
		 * the list.
		 */
		btrfs_try_granting_tickets(fs_info, space_info);
	}
	return (tickets_id != space_info->tickets_id);
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
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info, space_info);
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
							      space_info);
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
				if (maybe_fail_all_tickets(fs_info, space_info)) {
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

static const enum btrfs_flush_state evict_flush_states[] = {
	FLUSH_DELAYED_ITEMS_NR,
	FLUSH_DELAYED_ITEMS,
	FLUSH_DELAYED_REFS_NR,
	FLUSH_DELAYED_REFS,
	FLUSH_DELALLOC,
	FLUSH_DELALLOC_WAIT,
	ALLOC_CHUNK,
	COMMIT_TRANS,
};

static void priority_reclaim_metadata_space(struct btrfs_fs_info *fs_info,
				struct btrfs_space_info *space_info,
				struct reserve_ticket *ticket,
				const enum btrfs_flush_state *states,
				int states_nr)
{
	u64 to_reclaim;
	int flush_state;

	spin_lock(&space_info->lock);
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info, space_info);
	if (!to_reclaim) {
		spin_unlock(&space_info->lock);
		return;
	}
	spin_unlock(&space_info->lock);

	flush_state = 0;
	do {
		flush_space(fs_info, space_info, to_reclaim, states[flush_state]);
		flush_state++;
		spin_lock(&space_info->lock);
		if (ticket->bytes == 0) {
			spin_unlock(&space_info->lock);
			return;
		}
		spin_unlock(&space_info->lock);
	} while (flush_state < states_nr);
}

static void wait_reserve_ticket(struct btrfs_fs_info *fs_info,
				struct btrfs_space_info *space_info,
				struct reserve_ticket *ticket)

{
	DEFINE_WAIT(wait);
	int ret = 0;

	spin_lock(&space_info->lock);
	while (ticket->bytes > 0 && ticket->error == 0) {
		ret = prepare_to_wait_event(&ticket->wait, &wait, TASK_KILLABLE);
		if (ret) {
			/*
			 * Delete us from the list. After we unlock the space
			 * info, we don't want the async reclaim job to reserve
			 * space for this ticket. If that would happen, then the
			 * ticket's task would not known that space was reserved
			 * despite getting an error, resulting in a space leak
			 * (bytes_may_use counter of our space_info).
			 */
			remove_ticket(space_info, ticket);
			ticket->error = -EINTR;
			break;
		}
		spin_unlock(&space_info->lock);

		schedule();

		finish_wait(&ticket->wait, &wait);
		spin_lock(&space_info->lock);
	}
	spin_unlock(&space_info->lock);
}

/**
 * handle_reserve_ticket - do the appropriate flushing and waiting for a ticket
 * @fs_info - the fs
 * @space_info - the space_info for the reservation
 * @ticket - the ticket for the reservation
 * @flush - how much we can flush
 *
 * This does the work of figuring out how to flush for the ticket, waiting for
 * the reservation, and returning the appropriate error if there is one.
 */
static int handle_reserve_ticket(struct btrfs_fs_info *fs_info,
				 struct btrfs_space_info *space_info,
				 struct reserve_ticket *ticket,
				 enum btrfs_reserve_flush_enum flush)
{
	int ret;

	switch (flush) {
	case BTRFS_RESERVE_FLUSH_ALL:
	case BTRFS_RESERVE_FLUSH_ALL_STEAL:
		wait_reserve_ticket(fs_info, space_info, ticket);
		break;
	case BTRFS_RESERVE_FLUSH_LIMIT:
		priority_reclaim_metadata_space(fs_info, space_info, ticket,
						priority_flush_states,
						ARRAY_SIZE(priority_flush_states));
		break;
	case BTRFS_RESERVE_FLUSH_EVICT:
		priority_reclaim_metadata_space(fs_info, space_info, ticket,
						evict_flush_states,
						ARRAY_SIZE(evict_flush_states));
		break;
	default:
		ASSERT(0);
		break;
	}

	spin_lock(&space_info->lock);
	ret = ticket->error;
	if (ticket->bytes || ticket->error) {
		/*
		 * We were a priority ticket, so we need to delete ourselves
		 * from the list.  Because we could have other priority tickets
		 * behind us that require less space, run
		 * btrfs_try_granting_tickets() to see if their reservations can
		 * now be made.
		 */
		if (!list_empty(&ticket->list)) {
			remove_ticket(space_info, ticket);
			btrfs_try_granting_tickets(fs_info, space_info);
		}

		if (!ret)
			ret = -ENOSPC;
	}
	spin_unlock(&space_info->lock);
	ASSERT(list_empty(&ticket->list));
	/*
	 * Check that we can't have an error set if the reservation succeeded,
	 * as that would confuse tasks and lead them to error out without
	 * releasing reserved space (if an error happens the expectation is that
	 * space wasn't reserved at all).
	 */
	ASSERT(!(ticket->bytes == 0 && ticket->error));
	return ret;
}

/*
 * This returns true if this flush state will go through the ordinary flushing
 * code.
 */
static inline bool is_normal_flushing(enum btrfs_reserve_flush_enum flush)
{
	return	(flush == BTRFS_RESERVE_FLUSH_ALL) ||
		(flush == BTRFS_RESERVE_FLUSH_ALL_STEAL);
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
				    enum btrfs_reserve_flush_enum flush)
{
	struct reserve_ticket ticket;
	u64 used;
	int ret = 0;
	bool pending_tickets;

	ASSERT(orig_bytes);
	ASSERT(!current->journal_info || flush != BTRFS_RESERVE_FLUSH_ALL);

	spin_lock(&space_info->lock);
	ret = -ENOSPC;
	used = btrfs_space_info_used(space_info, true);

	/*
	 * We don't want NO_FLUSH allocations to jump everybody, they can
	 * generally handle ENOSPC in a different way, so treat them the same as
	 * normal flushers when it comes to skipping pending tickets.
	 */
	if (is_normal_flushing(flush) || (flush == BTRFS_RESERVE_NO_FLUSH))
		pending_tickets = !list_empty(&space_info->tickets) ||
			!list_empty(&space_info->priority_tickets);
	else
		pending_tickets = !list_empty(&space_info->priority_tickets);

	/*
	 * Carry on if we have enough space (short-circuit) OR call
	 * can_overcommit() to ensure we can overcommit to continue.
	 */
	if (!pending_tickets &&
	    ((used + orig_bytes <= space_info->total_bytes) ||
	     btrfs_can_overcommit(fs_info, space_info, orig_bytes, flush))) {
		btrfs_space_info_update_bytes_may_use(fs_info, space_info,
						      orig_bytes);
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
		space_info->reclaim_size += ticket.bytes;
		init_waitqueue_head(&ticket.wait);
		ticket.steal = (flush == BTRFS_RESERVE_FLUSH_ALL_STEAL);
		if (flush == BTRFS_RESERVE_FLUSH_ALL ||
		    flush == BTRFS_RESERVE_FLUSH_ALL_STEAL) {
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
		    need_do_async_reclaim(fs_info, space_info, used) &&
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

	return handle_reserve_ticket(fs_info, space_info, &ticket, flush);
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

	ret = __reserve_metadata_bytes(fs_info, block_rsv->space_info,
				       orig_bytes, flush);
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
