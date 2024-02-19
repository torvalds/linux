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
#include "zoned.h"
#include "fs.h"
#include "accessors.h"
#include "extent-tree.h"

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
 *     This will commit the transaction.  Historically we had a lot of logic
 *     surrounding whether or not we'd commit the transaction, but this waits born
 *     out of a pre-tickets era where we could end up committing the transaction
 *     thousands of times in a row without making progress.  Now thanks to our
 *     ticketing system we know if we're not making progress and can error
 *     everybody out after a few commits rather than burning the disk hoping for
 *     a different answer.
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
		s_info->bytes_zone_unusable +
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

	list_for_each_entry(found, head, list)
		found->full = 0;
}

/*
 * Block groups with more than this value (percents) of unusable space will be
 * scheduled for background reclaim.
 */
#define BTRFS_DEFAULT_ZONED_RECLAIM_THRESH			(75)

/*
 * Calculate chunk size depending on volume type (regular or zoned).
 */
static u64 calc_chunk_size(const struct btrfs_fs_info *fs_info, u64 flags)
{
	if (btrfs_is_zoned(fs_info))
		return fs_info->zone_size;

	ASSERT(flags & BTRFS_BLOCK_GROUP_TYPE_MASK);

	if (flags & BTRFS_BLOCK_GROUP_DATA)
		return BTRFS_MAX_DATA_CHUNK_SIZE;
	else if (flags & BTRFS_BLOCK_GROUP_SYSTEM)
		return SZ_32M;

	/* Handle BTRFS_BLOCK_GROUP_METADATA */
	if (fs_info->fs_devices->total_rw_bytes > 50ULL * SZ_1G)
		return SZ_1G;

	return SZ_256M;
}

/*
 * Update default chunk size.
 */
void btrfs_update_space_info_chunk_size(struct btrfs_space_info *space_info,
					u64 chunk_size)
{
	WRITE_ONCE(space_info->chunk_size, chunk_size);
}

static int create_space_info(struct btrfs_fs_info *info, u64 flags)
{

	struct btrfs_space_info *space_info;
	int i;
	int ret;

	space_info = kzalloc(sizeof(*space_info), GFP_NOFS);
	if (!space_info)
		return -ENOMEM;

	for (i = 0; i < BTRFS_NR_RAID_TYPES; i++)
		INIT_LIST_HEAD(&space_info->block_groups[i]);
	init_rwsem(&space_info->groups_sem);
	spin_lock_init(&space_info->lock);
	space_info->flags = flags & BTRFS_BLOCK_GROUP_TYPE_MASK;
	space_info->force_alloc = CHUNK_ALLOC_NO_FORCE;
	INIT_LIST_HEAD(&space_info->ro_bgs);
	INIT_LIST_HEAD(&space_info->tickets);
	INIT_LIST_HEAD(&space_info->priority_tickets);
	space_info->clamp = 1;
	btrfs_update_space_info_chunk_size(space_info, calc_chunk_size(info, flags));

	if (btrfs_is_zoned(info))
		space_info->bg_reclaim_threshold = BTRFS_DEFAULT_ZONED_RECLAIM_THRESH;

	ret = btrfs_sysfs_add_space_info_type(info, space_info);
	if (ret)
		return ret;

	list_add(&space_info->list, &info->space_info);
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

void btrfs_add_bg_to_space_info(struct btrfs_fs_info *info,
				struct btrfs_block_group *block_group)
{
	struct btrfs_space_info *found;
	int factor, index;

	factor = btrfs_bg_type_to_factor(block_group->flags);

	found = btrfs_find_space_info(info, block_group->flags);
	ASSERT(found);
	spin_lock(&found->lock);
	found->total_bytes += block_group->length;
	found->disk_total += block_group->length * factor;
	found->bytes_used += block_group->used;
	found->disk_used += block_group->used * factor;
	found->bytes_readonly += block_group->bytes_super;
	found->bytes_zone_unusable += block_group->zone_unusable;
	if (block_group->length > 0)
		found->full = 0;
	btrfs_try_granting_tickets(info, found);
	spin_unlock(&found->lock);

	block_group->space_info = found;

	index = btrfs_bg_flags_to_raid_index(block_group->flags);
	down_write(&found->groups_sem);
	list_add_tail(&block_group->list, &found->block_groups[index]);
	up_write(&found->groups_sem);
}

struct btrfs_space_info *btrfs_find_space_info(struct btrfs_fs_info *info,
					       u64 flags)
{
	struct list_head *head = &info->space_info;
	struct btrfs_space_info *found;

	flags &= BTRFS_BLOCK_GROUP_TYPE_MASK;

	list_for_each_entry(found, head, list) {
		if (found->flags & flags)
			return found;
	}
	return NULL;
}

static u64 calc_available_free_space(struct btrfs_fs_info *fs_info,
			  struct btrfs_space_info *space_info,
			  enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_space_info *data_sinfo;
	u64 profile;
	u64 avail;
	u64 data_chunk_size;
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
	if (avail == 0)
		return 0;

	/*
	 * Calculate the data_chunk_size, space_info->chunk_size is the
	 * "optimal" chunk size based on the fs size.  However when we actually
	 * allocate the chunk we will strip this down further, making it no more
	 * than 10% of the disk or 1G, whichever is smaller.
	 */
	data_sinfo = btrfs_find_space_info(fs_info, BTRFS_BLOCK_GROUP_DATA);
	data_chunk_size = min(data_sinfo->chunk_size,
			      mult_perc(fs_info->fs_devices->total_rw_bytes, 10));
	data_chunk_size = min_t(u64, data_chunk_size, SZ_1G);

	/*
	 * Since data allocations immediately use block groups as part of the
	 * reservation, because we assume that data reservations will == actual
	 * usage, we could potentially overcommit and then immediately have that
	 * available space used by a data allocation, which could put us in a
	 * bind when we get close to filling the file system.
	 *
	 * To handle this simply remove the data_chunk_size from the available
	 * space.  If we are relatively empty this won't affect our ability to
	 * overcommit much, and if we're very close to full it'll keep us from
	 * getting into a position where we've given ourselves very little
	 * metadata wiggle room.
	 */
	if (avail <= data_chunk_size)
		return 0;
	avail -= data_chunk_size;

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

		/* Check and see if our ticket can be satisfied now. */
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

static const char *space_info_flag_to_str(const struct btrfs_space_info *space_info)
{
	switch (space_info->flags) {
	case BTRFS_BLOCK_GROUP_SYSTEM:
		return "SYSTEM";
	case BTRFS_BLOCK_GROUP_METADATA | BTRFS_BLOCK_GROUP_DATA:
		return "DATA+METADATA";
	case BTRFS_BLOCK_GROUP_DATA:
		return "DATA";
	case BTRFS_BLOCK_GROUP_METADATA:
		return "METADATA";
	default:
		return "UNKNOWN";
	}
}

static void dump_global_block_rsv(struct btrfs_fs_info *fs_info)
{
	DUMP_BLOCK_RSV(fs_info, global_block_rsv);
	DUMP_BLOCK_RSV(fs_info, trans_block_rsv);
	DUMP_BLOCK_RSV(fs_info, chunk_block_rsv);
	DUMP_BLOCK_RSV(fs_info, delayed_block_rsv);
	DUMP_BLOCK_RSV(fs_info, delayed_refs_rsv);
}

static void __btrfs_dump_space_info(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *info)
{
	const char *flag_str = space_info_flag_to_str(info);
	lockdep_assert_held(&info->lock);

	/* The free space could be negative in case of overcommit */
	btrfs_info(fs_info, "space_info %s has %lld free, is %sfull",
		   flag_str,
		   (s64)(info->total_bytes - btrfs_space_info_used(info, true)),
		   info->full ? "" : "not ");
	btrfs_info(fs_info,
"space_info total=%llu, used=%llu, pinned=%llu, reserved=%llu, may_use=%llu, readonly=%llu zone_unusable=%llu",
		info->total_bytes, info->bytes_used, info->bytes_pinned,
		info->bytes_reserved, info->bytes_may_use,
		info->bytes_readonly, info->bytes_zone_unusable);
}

void btrfs_dump_space_info(struct btrfs_fs_info *fs_info,
			   struct btrfs_space_info *info, u64 bytes,
			   int dump_block_groups)
{
	struct btrfs_block_group *cache;
	u64 total_avail = 0;
	int index = 0;

	spin_lock(&info->lock);
	__btrfs_dump_space_info(fs_info, info);
	dump_global_block_rsv(fs_info);
	spin_unlock(&info->lock);

	if (!dump_block_groups)
		return;

	down_read(&info->groups_sem);
again:
	list_for_each_entry(cache, &info->block_groups[index], list) {
		u64 avail;

		spin_lock(&cache->lock);
		avail = cache->length - cache->used - cache->pinned -
			cache->reserved - cache->delalloc_bytes -
			cache->bytes_super - cache->zone_unusable;
		btrfs_info(fs_info,
"block group %llu has %llu bytes, %llu used %llu pinned %llu reserved %llu delalloc %llu super %llu zone_unusable (%llu bytes available) %s",
			   cache->start, cache->length, cache->used, cache->pinned,
			   cache->reserved, cache->delalloc_bytes,
			   cache->bytes_super, cache->zone_unusable,
			   avail, cache->ro ? "[readonly]" : "");
		spin_unlock(&cache->lock);
		btrfs_dump_free_space(cache, bytes);
		total_avail += avail;
	}
	if (++index < BTRFS_NR_RAID_TYPES)
		goto again;
	up_read(&info->groups_sem);

	btrfs_info(fs_info, "%llu bytes available across all block groups", total_avail);
}

static inline u64 calc_reclaim_items_nr(const struct btrfs_fs_info *fs_info,
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
			    u64 to_reclaim, bool wait_ordered,
			    bool for_preempt)
{
	struct btrfs_trans_handle *trans;
	u64 delalloc_bytes;
	u64 ordered_bytes;
	u64 items;
	long time_left;
	int loops;

	delalloc_bytes = percpu_counter_sum_positive(&fs_info->delalloc_bytes);
	ordered_bytes = percpu_counter_sum_positive(&fs_info->ordered_bytes);
	if (delalloc_bytes == 0 && ordered_bytes == 0)
		return;

	/* Calc the number of the pages we need flush for space reservation */
	if (to_reclaim == U64_MAX) {
		items = U64_MAX;
	} else {
		/*
		 * to_reclaim is set to however much metadata we need to
		 * reclaim, but reclaiming that much data doesn't really track
		 * exactly.  What we really want to do is reclaim full inode's
		 * worth of reservations, however that's not available to us
		 * here.  We will take a fraction of the delalloc bytes for our
		 * flushing loops and hope for the best.  Delalloc will expand
		 * the amount we write to cover an entire dirty extent, which
		 * will reclaim the metadata reservation for that range.  If
		 * it's not enough subsequent flush stages will be more
		 * aggressive.
		 */
		to_reclaim = max(to_reclaim, delalloc_bytes >> 3);
		items = calc_reclaim_items_nr(fs_info, to_reclaim) * 2;
	}

	trans = current->journal_info;

	/*
	 * If we are doing more ordered than delalloc we need to just wait on
	 * ordered extents, otherwise we'll waste time trying to flush delalloc
	 * that likely won't give us the space back we need.
	 */
	if (ordered_bytes > delalloc_bytes && !for_preempt)
		wait_ordered = true;

	loops = 0;
	while ((delalloc_bytes || ordered_bytes) && loops < 3) {
		u64 temp = min(delalloc_bytes, to_reclaim) >> PAGE_SHIFT;
		long nr_pages = min_t(u64, temp, LONG_MAX);
		int async_pages;

		btrfs_start_delalloc_roots(fs_info, nr_pages, true);

		/*
		 * We need to make sure any outstanding async pages are now
		 * processed before we continue.  This is because things like
		 * sync_inode() try to be smart and skip writing if the inode is
		 * marked clean.  We don't use filemap_fwrite for flushing
		 * because we want to control how many pages we write out at a
		 * time, thus this is the only safe way to make sure we've
		 * waited for outstanding compressed workers to have started
		 * their jobs and thus have ordered extents set up properly.
		 *
		 * This exists because we do not want to wait for each
		 * individual inode to finish its async work, we simply want to
		 * start the IO on everybody, and then come back here and wait
		 * for all of the async work to catch up.  Once we're done with
		 * that we know we'll have ordered extents for everything and we
		 * can decide if we wait for that or not.
		 *
		 * If we choose to replace this in the future, make absolutely
		 * sure that the proper waiting is being done in the async case,
		 * as there have been bugs in that area before.
		 */
		async_pages = atomic_read(&fs_info->async_delalloc_pages);
		if (!async_pages)
			goto skip_async;

		/*
		 * We don't want to wait forever, if we wrote less pages in this
		 * loop than we have outstanding, only wait for that number of
		 * pages, otherwise we can wait for all async pages to finish
		 * before continuing.
		 */
		if (async_pages > nr_pages)
			async_pages -= nr_pages;
		else
			async_pages = 0;
		wait_event(fs_info->async_submit_wait,
			   atomic_read(&fs_info->async_delalloc_pages) <=
			   async_pages);
skip_async:
		loops++;
		if (wait_ordered && !trans) {
			btrfs_wait_ordered_roots(fs_info, items, 0, (u64)-1);
		} else {
			time_left = schedule_timeout_killable(1);
			if (time_left)
				break;
		}

		/*
		 * If we are for preemption we just want a one-shot of delalloc
		 * flushing so we can stop flushing if we decide we don't need
		 * to anymore.
		 */
		if (for_preempt)
			break;

		spin_lock(&space_info->lock);
		if (list_empty(&space_info->tickets) &&
		    list_empty(&space_info->priority_tickets)) {
			spin_unlock(&space_info->lock);
			break;
		}
		spin_unlock(&space_info->lock);

		delalloc_bytes = percpu_counter_sum_positive(
						&fs_info->delalloc_bytes);
		ordered_bytes = percpu_counter_sum_positive(
						&fs_info->ordered_bytes);
	}
}

/*
 * Try to flush some data based on policy set by @state. This is only advisory
 * and may fail for various reasons. The caller is supposed to examine the
 * state of @space_info to detect the outcome.
 */
static void flush_space(struct btrfs_fs_info *fs_info,
		       struct btrfs_space_info *space_info, u64 num_bytes,
		       enum btrfs_flush_state state, bool for_preempt)
{
	struct btrfs_root *root = fs_info->tree_root;
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

		trans = btrfs_join_transaction_nostart(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			if (ret == -ENOENT)
				ret = 0;
			break;
		}
		ret = btrfs_run_delayed_items_nr(trans, nr);
		btrfs_end_transaction(trans);
		break;
	case FLUSH_DELALLOC:
	case FLUSH_DELALLOC_WAIT:
	case FLUSH_DELALLOC_FULL:
		if (state == FLUSH_DELALLOC_FULL)
			num_bytes = U64_MAX;
		shrink_delalloc(fs_info, space_info, num_bytes,
				state != FLUSH_DELALLOC, for_preempt);
		break;
	case FLUSH_DELAYED_REFS_NR:
	case FLUSH_DELAYED_REFS:
		trans = btrfs_join_transaction_nostart(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			if (ret == -ENOENT)
				ret = 0;
			break;
		}
		if (state == FLUSH_DELAYED_REFS_NR)
			btrfs_run_delayed_refs(trans, num_bytes);
		else
			btrfs_run_delayed_refs(trans, 0);
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
		ASSERT(current->journal_info == NULL);
		/*
		 * We don't want to start a new transaction, just attach to the
		 * current one or wait it fully commits in case its commit is
		 * happening at the moment. Note: we don't use a nostart join
		 * because that does not wait for a transaction to fully commit
		 * (only for it to be unblocked, state TRANS_STATE_UNBLOCKED).
		 */
		trans = btrfs_attach_transaction_barrier(root);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			if (ret == -ENOENT)
				ret = 0;
			break;
		}
		ret = btrfs_commit_transaction(trans);
		break;
	default:
		ret = -ENOSPC;
		break;
	}

	trace_btrfs_flush_space(fs_info, space_info->flags, num_bytes, state,
				ret, for_preempt);
	return;
}

static inline u64
btrfs_calc_reclaim_metadata_size(struct btrfs_fs_info *fs_info,
				 struct btrfs_space_info *space_info)
{
	u64 used;
	u64 avail;
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

	return to_reclaim;
}

static bool need_preemptive_reclaim(struct btrfs_fs_info *fs_info,
				    struct btrfs_space_info *space_info)
{
	const u64 global_rsv_size = btrfs_block_rsv_reserved(&fs_info->global_block_rsv);
	u64 ordered, delalloc;
	u64 thresh;
	u64 used;

	thresh = mult_perc(space_info->total_bytes, 90);

	lockdep_assert_held(&space_info->lock);

	/* If we're just plain full then async reclaim just slows us down. */
	if ((space_info->bytes_used + space_info->bytes_reserved +
	     global_rsv_size) >= thresh)
		return false;

	used = space_info->bytes_may_use + space_info->bytes_pinned;

	/* The total flushable belongs to the global rsv, don't flush. */
	if (global_rsv_size >= used)
		return false;

	/*
	 * 128MiB is 1/4 of the maximum global rsv size.  If we have less than
	 * that devoted to other reservations then there's no sense in flushing,
	 * we don't have a lot of things that need flushing.
	 */
	if (used - global_rsv_size <= SZ_128M)
		return false;

	/*
	 * We have tickets queued, bail so we don't compete with the async
	 * flushers.
	 */
	if (space_info->reclaim_size)
		return false;

	/*
	 * If we have over half of the free space occupied by reservations or
	 * pinned then we want to start flushing.
	 *
	 * We do not do the traditional thing here, which is to say
	 *
	 *   if (used >= ((total_bytes + avail) / 2))
	 *     return 1;
	 *
	 * because this doesn't quite work how we want.  If we had more than 50%
	 * of the space_info used by bytes_used and we had 0 available we'd just
	 * constantly run the background flusher.  Instead we want it to kick in
	 * if our reclaimable space exceeds our clamped free space.
	 *
	 * Our clamping range is 2^1 -> 2^8.  Practically speaking that means
	 * the following:
	 *
	 * Amount of RAM        Minimum threshold       Maximum threshold
	 *
	 *        256GiB                     1GiB                  128GiB
	 *        128GiB                   512MiB                   64GiB
	 *         64GiB                   256MiB                   32GiB
	 *         32GiB                   128MiB                   16GiB
	 *         16GiB                    64MiB                    8GiB
	 *
	 * These are the range our thresholds will fall in, corresponding to how
	 * much delalloc we need for the background flusher to kick in.
	 */

	thresh = calc_available_free_space(fs_info, space_info,
					   BTRFS_RESERVE_FLUSH_ALL);
	used = space_info->bytes_used + space_info->bytes_reserved +
	       space_info->bytes_readonly + global_rsv_size;
	if (used < space_info->total_bytes)
		thresh += space_info->total_bytes - used;
	thresh >>= space_info->clamp;

	used = space_info->bytes_pinned;

	/*
	 * If we have more ordered bytes than delalloc bytes then we're either
	 * doing a lot of DIO, or we simply don't have a lot of delalloc waiting
	 * around.  Preemptive flushing is only useful in that it can free up
	 * space before tickets need to wait for things to finish.  In the case
	 * of ordered extents, preemptively waiting on ordered extents gets us
	 * nothing, if our reservations are tied up in ordered extents we'll
	 * simply have to slow down writers by forcing them to wait on ordered
	 * extents.
	 *
	 * In the case that ordered is larger than delalloc, only include the
	 * block reserves that we would actually be able to directly reclaim
	 * from.  In this case if we're heavy on metadata operations this will
	 * clearly be heavy enough to warrant preemptive flushing.  In the case
	 * of heavy DIO or ordered reservations, preemptive flushing will just
	 * waste time and cause us to slow down.
	 *
	 * We want to make sure we truly are maxed out on ordered however, so
	 * cut ordered in half, and if it's still higher than delalloc then we
	 * can keep flushing.  This is to avoid the case where we start
	 * flushing, and now delalloc == ordered and we stop preemptively
	 * flushing when we could still have several gigs of delalloc to flush.
	 */
	ordered = percpu_counter_read_positive(&fs_info->ordered_bytes) >> 1;
	delalloc = percpu_counter_read_positive(&fs_info->delalloc_bytes);
	if (ordered >= delalloc)
		used += btrfs_block_rsv_reserved(&fs_info->delayed_refs_rsv) +
			btrfs_block_rsv_reserved(&fs_info->delayed_block_rsv);
	else
		used += space_info->bytes_may_use - global_rsv_size;

	return (used >= thresh && !btrfs_fs_closing(fs_info) &&
		!test_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state));
}

static bool steal_from_global_rsv(struct btrfs_fs_info *fs_info,
				  struct btrfs_space_info *space_info,
				  struct reserve_ticket *ticket)
{
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;
	u64 min_bytes;

	if (!ticket->steal)
		return false;

	if (global_rsv->space_info != space_info)
		return false;

	spin_lock(&global_rsv->lock);
	min_bytes = mult_perc(global_rsv->size, 10);
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
 * We've exhausted our flushing, start failing tickets.
 *
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
	const bool aborted = BTRFS_FS_ERROR(fs_info);

	trace_btrfs_fail_all_tickets(fs_info, space_info);

	if (btrfs_test_opt(fs_info, ENOSPC_DEBUG)) {
		btrfs_info(fs_info, "cannot satisfy tickets, dumping space info");
		__btrfs_dump_space_info(fs_info, space_info);
	}

	while (!list_empty(&space_info->tickets) &&
	       tickets_id == space_info->tickets_id) {
		ticket = list_first_entry(&space_info->tickets,
					  struct reserve_ticket, list);

		if (!aborted && steal_from_global_rsv(fs_info, space_info, ticket))
			return true;

		if (!aborted && btrfs_test_opt(fs_info, ENOSPC_DEBUG))
			btrfs_info(fs_info, "failing ticket with %llu bytes",
				   ticket->bytes);

		remove_ticket(space_info, ticket);
		if (aborted)
			ticket->error = -EIO;
		else
			ticket->error = -ENOSPC;
		wake_up(&ticket->wait);

		/*
		 * We're just throwing tickets away, so more flushing may not
		 * trip over btrfs_try_granting_tickets, so we need to call it
		 * here to see if we can make progress with the next ticket in
		 * the list.
		 */
		if (!aborted)
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
	enum btrfs_flush_state flush_state;
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
		flush_space(fs_info, space_info, to_reclaim, flush_state, false);
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
		 * We do not want to empty the system of delalloc unless we're
		 * under heavy pressure, so allow one trip through the flushing
		 * logic before we start doing a FLUSH_DELALLOC_FULL.
		 */
		if (flush_state == FLUSH_DELALLOC_FULL && !commit_cycles)
			flush_state++;

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

/*
 * This handles pre-flushing of metadata space before we get to the point that
 * we need to start blocking threads on tickets.  The logic here is different
 * from the other flush paths because it doesn't rely on tickets to tell us how
 * much we need to flush, instead it attempts to keep us below the 80% full
 * watermark of space by flushing whichever reservation pool is currently the
 * largest.
 */
static void btrfs_preempt_reclaim_metadata_space(struct work_struct *work)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_space_info *space_info;
	struct btrfs_block_rsv *delayed_block_rsv;
	struct btrfs_block_rsv *delayed_refs_rsv;
	struct btrfs_block_rsv *global_rsv;
	struct btrfs_block_rsv *trans_rsv;
	int loops = 0;

	fs_info = container_of(work, struct btrfs_fs_info,
			       preempt_reclaim_work);
	space_info = btrfs_find_space_info(fs_info, BTRFS_BLOCK_GROUP_METADATA);
	delayed_block_rsv = &fs_info->delayed_block_rsv;
	delayed_refs_rsv = &fs_info->delayed_refs_rsv;
	global_rsv = &fs_info->global_block_rsv;
	trans_rsv = &fs_info->trans_block_rsv;

	spin_lock(&space_info->lock);
	while (need_preemptive_reclaim(fs_info, space_info)) {
		enum btrfs_flush_state flush;
		u64 delalloc_size = 0;
		u64 to_reclaim, block_rsv_size;
		const u64 global_rsv_size = btrfs_block_rsv_reserved(global_rsv);

		loops++;

		/*
		 * We don't have a precise counter for the metadata being
		 * reserved for delalloc, so we'll approximate it by subtracting
		 * out the block rsv's space from the bytes_may_use.  If that
		 * amount is higher than the individual reserves, then we can
		 * assume it's tied up in delalloc reservations.
		 */
		block_rsv_size = global_rsv_size +
			btrfs_block_rsv_reserved(delayed_block_rsv) +
			btrfs_block_rsv_reserved(delayed_refs_rsv) +
			btrfs_block_rsv_reserved(trans_rsv);
		if (block_rsv_size < space_info->bytes_may_use)
			delalloc_size = space_info->bytes_may_use - block_rsv_size;

		/*
		 * We don't want to include the global_rsv in our calculation,
		 * because that's space we can't touch.  Subtract it from the
		 * block_rsv_size for the next checks.
		 */
		block_rsv_size -= global_rsv_size;

		/*
		 * We really want to avoid flushing delalloc too much, as it
		 * could result in poor allocation patterns, so only flush it if
		 * it's larger than the rest of the pools combined.
		 */
		if (delalloc_size > block_rsv_size) {
			to_reclaim = delalloc_size;
			flush = FLUSH_DELALLOC;
		} else if (space_info->bytes_pinned >
			   (btrfs_block_rsv_reserved(delayed_block_rsv) +
			    btrfs_block_rsv_reserved(delayed_refs_rsv))) {
			to_reclaim = space_info->bytes_pinned;
			flush = COMMIT_TRANS;
		} else if (btrfs_block_rsv_reserved(delayed_block_rsv) >
			   btrfs_block_rsv_reserved(delayed_refs_rsv)) {
			to_reclaim = btrfs_block_rsv_reserved(delayed_block_rsv);
			flush = FLUSH_DELAYED_ITEMS_NR;
		} else {
			to_reclaim = btrfs_block_rsv_reserved(delayed_refs_rsv);
			flush = FLUSH_DELAYED_REFS_NR;
		}

		spin_unlock(&space_info->lock);

		/*
		 * We don't want to reclaim everything, just a portion, so scale
		 * down the to_reclaim by 1/4.  If it takes us down to 0,
		 * reclaim 1 items worth.
		 */
		to_reclaim >>= 2;
		if (!to_reclaim)
			to_reclaim = btrfs_calc_insert_metadata_size(fs_info, 1);
		flush_space(fs_info, space_info, to_reclaim, flush, true);
		cond_resched();
		spin_lock(&space_info->lock);
	}

	/* We only went through once, back off our clamping. */
	if (loops == 1 && !space_info->reclaim_size)
		space_info->clamp = max(1, space_info->clamp - 1);
	trace_btrfs_done_preemptive_reclaim(fs_info, space_info);
	spin_unlock(&space_info->lock);
}

/*
 * FLUSH_DELALLOC_WAIT:
 *   Space is freed from flushing delalloc in one of two ways.
 *
 *   1) compression is on and we allocate less space than we reserved
 *   2) we are overwriting existing space
 *
 *   For #1 that extra space is reclaimed as soon as the delalloc pages are
 *   COWed, by way of btrfs_add_reserved_bytes() which adds the actual extent
 *   length to ->bytes_reserved, and subtracts the reserved space from
 *   ->bytes_may_use.
 *
 *   For #2 this is trickier.  Once the ordered extent runs we will drop the
 *   extent in the range we are overwriting, which creates a delayed ref for
 *   that freed extent.  This however is not reclaimed until the transaction
 *   commits, thus the next stages.
 *
 * RUN_DELAYED_IPUTS
 *   If we are freeing inodes, we want to make sure all delayed iputs have
 *   completed, because they could have been on an inode with i_nlink == 0, and
 *   thus have been truncated and freed up space.  But again this space is not
 *   immediately re-usable, it comes in the form of a delayed ref, which must be
 *   run and then the transaction must be committed.
 *
 * COMMIT_TRANS
 *   This is where we reclaim all of the pinned space generated by running the
 *   iputs
 *
 * ALLOC_CHUNK_FORCE
 *   For data we start with alloc chunk force, however we could have been full
 *   before, and then the transaction commit could have freed new block groups,
 *   so if we now have space to allocate do the force chunk allocation.
 */
static const enum btrfs_flush_state data_flush_states[] = {
	FLUSH_DELALLOC_FULL,
	RUN_DELAYED_IPUTS,
	COMMIT_TRANS,
	ALLOC_CHUNK_FORCE,
};

static void btrfs_async_reclaim_data_space(struct work_struct *work)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_space_info *space_info;
	u64 last_tickets_id;
	enum btrfs_flush_state flush_state = 0;

	fs_info = container_of(work, struct btrfs_fs_info, async_data_reclaim_work);
	space_info = fs_info->data_sinfo;

	spin_lock(&space_info->lock);
	if (list_empty(&space_info->tickets)) {
		space_info->flush = 0;
		spin_unlock(&space_info->lock);
		return;
	}
	last_tickets_id = space_info->tickets_id;
	spin_unlock(&space_info->lock);

	while (!space_info->full) {
		flush_space(fs_info, space_info, U64_MAX, ALLOC_CHUNK_FORCE, false);
		spin_lock(&space_info->lock);
		if (list_empty(&space_info->tickets)) {
			space_info->flush = 0;
			spin_unlock(&space_info->lock);
			return;
		}

		/* Something happened, fail everything and bail. */
		if (BTRFS_FS_ERROR(fs_info))
			goto aborted_fs;
		last_tickets_id = space_info->tickets_id;
		spin_unlock(&space_info->lock);
	}

	while (flush_state < ARRAY_SIZE(data_flush_states)) {
		flush_space(fs_info, space_info, U64_MAX,
			    data_flush_states[flush_state], false);
		spin_lock(&space_info->lock);
		if (list_empty(&space_info->tickets)) {
			space_info->flush = 0;
			spin_unlock(&space_info->lock);
			return;
		}

		if (last_tickets_id == space_info->tickets_id) {
			flush_state++;
		} else {
			last_tickets_id = space_info->tickets_id;
			flush_state = 0;
		}

		if (flush_state >= ARRAY_SIZE(data_flush_states)) {
			if (space_info->full) {
				if (maybe_fail_all_tickets(fs_info, space_info))
					flush_state = 0;
				else
					space_info->flush = 0;
			} else {
				flush_state = 0;
			}

			/* Something happened, fail everything and bail. */
			if (BTRFS_FS_ERROR(fs_info))
				goto aborted_fs;

		}
		spin_unlock(&space_info->lock);
	}
	return;

aborted_fs:
	maybe_fail_all_tickets(fs_info, space_info);
	space_info->flush = 0;
	spin_unlock(&space_info->lock);
}

void btrfs_init_async_reclaim_work(struct btrfs_fs_info *fs_info)
{
	INIT_WORK(&fs_info->async_reclaim_work, btrfs_async_reclaim_metadata_space);
	INIT_WORK(&fs_info->async_data_reclaim_work, btrfs_async_reclaim_data_space);
	INIT_WORK(&fs_info->preempt_reclaim_work,
		  btrfs_preempt_reclaim_metadata_space);
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
	FLUSH_DELALLOC_FULL,
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
	int flush_state = 0;

	spin_lock(&space_info->lock);
	to_reclaim = btrfs_calc_reclaim_metadata_size(fs_info, space_info);
	/*
	 * This is the priority reclaim path, so to_reclaim could be >0 still
	 * because we may have only satisfied the priority tickets and still
	 * left non priority tickets on the list.  We would then have
	 * to_reclaim but ->bytes == 0.
	 */
	if (ticket->bytes == 0) {
		spin_unlock(&space_info->lock);
		return;
	}

	while (flush_state < states_nr) {
		spin_unlock(&space_info->lock);
		flush_space(fs_info, space_info, to_reclaim, states[flush_state],
			    false);
		flush_state++;
		spin_lock(&space_info->lock);
		if (ticket->bytes == 0) {
			spin_unlock(&space_info->lock);
			return;
		}
	}

	/*
	 * Attempt to steal from the global rsv if we can, except if the fs was
	 * turned into error mode due to a transaction abort when flushing space
	 * above, in that case fail with the abort error instead of returning
	 * success to the caller if we can steal from the global rsv - this is
	 * just to have caller fail immeditelly instead of later when trying to
	 * modify the fs, making it easier to debug -ENOSPC problems.
	 */
	if (BTRFS_FS_ERROR(fs_info)) {
		ticket->error = BTRFS_FS_ERROR(fs_info);
		remove_ticket(space_info, ticket);
	} else if (!steal_from_global_rsv(fs_info, space_info, ticket)) {
		ticket->error = -ENOSPC;
		remove_ticket(space_info, ticket);
	}

	/*
	 * We must run try_granting_tickets here because we could be a large
	 * ticket in front of a smaller ticket that can now be satisfied with
	 * the available space.
	 */
	btrfs_try_granting_tickets(fs_info, space_info);
	spin_unlock(&space_info->lock);
}

static void priority_reclaim_data_space(struct btrfs_fs_info *fs_info,
					struct btrfs_space_info *space_info,
					struct reserve_ticket *ticket)
{
	spin_lock(&space_info->lock);

	/* We could have been granted before we got here. */
	if (ticket->bytes == 0) {
		spin_unlock(&space_info->lock);
		return;
	}

	while (!space_info->full) {
		spin_unlock(&space_info->lock);
		flush_space(fs_info, space_info, U64_MAX, ALLOC_CHUNK_FORCE, false);
		spin_lock(&space_info->lock);
		if (ticket->bytes == 0) {
			spin_unlock(&space_info->lock);
			return;
		}
	}

	ticket->error = -ENOSPC;
	remove_ticket(space_info, ticket);
	btrfs_try_granting_tickets(fs_info, space_info);
	spin_unlock(&space_info->lock);
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

/*
 * Do the appropriate flushing and waiting for a ticket.
 *
 * @fs_info:    the filesystem
 * @space_info: space info for the reservation
 * @ticket:     ticket for the reservation
 * @start_ns:   timestamp when the reservation started
 * @orig_bytes: amount of bytes originally reserved
 * @flush:      how much we can flush
 *
 * This does the work of figuring out how to flush for the ticket, waiting for
 * the reservation, and returning the appropriate error if there is one.
 */
static int handle_reserve_ticket(struct btrfs_fs_info *fs_info,
				 struct btrfs_space_info *space_info,
				 struct reserve_ticket *ticket,
				 u64 start_ns, u64 orig_bytes,
				 enum btrfs_reserve_flush_enum flush)
{
	int ret;

	switch (flush) {
	case BTRFS_RESERVE_FLUSH_DATA:
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
	case BTRFS_RESERVE_FLUSH_FREE_SPACE_INODE:
		priority_reclaim_data_space(fs_info, space_info, ticket);
		break;
	default:
		ASSERT(0);
		break;
	}

	ret = ticket->error;
	ASSERT(list_empty(&ticket->list));
	/*
	 * Check that we can't have an error set if the reservation succeeded,
	 * as that would confuse tasks and lead them to error out without
	 * releasing reserved space (if an error happens the expectation is that
	 * space wasn't reserved at all).
	 */
	ASSERT(!(ticket->bytes == 0 && ticket->error));
	trace_btrfs_reserve_ticket(fs_info, space_info->flags, orig_bytes,
				   start_ns, flush, ticket->error);
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

static inline void maybe_clamp_preempt(struct btrfs_fs_info *fs_info,
				       struct btrfs_space_info *space_info)
{
	u64 ordered = percpu_counter_sum_positive(&fs_info->ordered_bytes);
	u64 delalloc = percpu_counter_sum_positive(&fs_info->delalloc_bytes);

	/*
	 * If we're heavy on ordered operations then clamping won't help us.  We
	 * need to clamp specifically to keep up with dirty'ing buffered
	 * writers, because there's not a 1:1 correlation of writing delalloc
	 * and freeing space, like there is with flushing delayed refs or
	 * delayed nodes.  If we're already more ordered than delalloc then
	 * we're keeping up, otherwise we aren't and should probably clamp.
	 */
	if (ordered < delalloc)
		space_info->clamp = min(space_info->clamp + 1, 8);
}

static inline bool can_steal(enum btrfs_reserve_flush_enum flush)
{
	return (flush == BTRFS_RESERVE_FLUSH_ALL_STEAL ||
		flush == BTRFS_RESERVE_FLUSH_EVICT);
}

/*
 * NO_FLUSH and FLUSH_EMERGENCY don't want to create a ticket, they just want to
 * fail as quickly as possible.
 */
static inline bool can_ticket(enum btrfs_reserve_flush_enum flush)
{
	return (flush != BTRFS_RESERVE_NO_FLUSH &&
		flush != BTRFS_RESERVE_FLUSH_EMERGENCY);
}

/*
 * Try to reserve bytes from the block_rsv's space.
 *
 * @fs_info:    the filesystem
 * @space_info: space info we want to allocate from
 * @orig_bytes: number of bytes we want
 * @flush:      whether or not we can flush to make our reservation
 *
 * This will reserve orig_bytes number of bytes from the space info associated
 * with the block_rsv.  If there is not enough space it will make an attempt to
 * flush out space to make room.  It will do this by flushing delalloc if
 * possible or committing the transaction.  If flush is 0 then no attempts to
 * regain reservations will be made and this will fail if there is not enough
 * space already.
 */
static int __reserve_bytes(struct btrfs_fs_info *fs_info,
			   struct btrfs_space_info *space_info, u64 orig_bytes,
			   enum btrfs_reserve_flush_enum flush)
{
	struct work_struct *async_work;
	struct reserve_ticket ticket;
	u64 start_ns = 0;
	u64 used;
	int ret = -ENOSPC;
	bool pending_tickets;

	ASSERT(orig_bytes);
	/*
	 * If have a transaction handle (current->journal_info != NULL), then
	 * the flush method can not be neither BTRFS_RESERVE_FLUSH_ALL* nor
	 * BTRFS_RESERVE_FLUSH_EVICT, as we could deadlock because those
	 * flushing methods can trigger transaction commits.
	 */
	if (current->journal_info) {
		/* One assert per line for easier debugging. */
		ASSERT(flush != BTRFS_RESERVE_FLUSH_ALL);
		ASSERT(flush != BTRFS_RESERVE_FLUSH_ALL_STEAL);
		ASSERT(flush != BTRFS_RESERVE_FLUSH_EVICT);
	}

	if (flush == BTRFS_RESERVE_FLUSH_DATA)
		async_work = &fs_info->async_data_reclaim_work;
	else
		async_work = &fs_info->async_reclaim_work;

	spin_lock(&space_info->lock);
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
	 * Things are dire, we need to make a reservation so we don't abort.  We
	 * will let this reservation go through as long as we have actual space
	 * left to allocate for the block.
	 */
	if (ret && unlikely(flush == BTRFS_RESERVE_FLUSH_EMERGENCY)) {
		used = btrfs_space_info_used(space_info, false);
		if (used + orig_bytes <= space_info->total_bytes) {
			btrfs_space_info_update_bytes_may_use(fs_info, space_info,
							      orig_bytes);
			ret = 0;
		}
	}

	/*
	 * If we couldn't make a reservation then setup our reservation ticket
	 * and kick the async worker if it's not already running.
	 *
	 * If we are a priority flusher then we just need to add our ticket to
	 * the list and we will do our own flushing further down.
	 */
	if (ret && can_ticket(flush)) {
		ticket.bytes = orig_bytes;
		ticket.error = 0;
		space_info->reclaim_size += ticket.bytes;
		init_waitqueue_head(&ticket.wait);
		ticket.steal = can_steal(flush);
		if (trace_btrfs_reserve_ticket_enabled())
			start_ns = ktime_get_ns();

		if (flush == BTRFS_RESERVE_FLUSH_ALL ||
		    flush == BTRFS_RESERVE_FLUSH_ALL_STEAL ||
		    flush == BTRFS_RESERVE_FLUSH_DATA) {
			list_add_tail(&ticket.list, &space_info->tickets);
			if (!space_info->flush) {
				/*
				 * We were forced to add a reserve ticket, so
				 * our preemptive flushing is unable to keep
				 * up.  Clamp down on the threshold for the
				 * preemptive flushing in order to keep up with
				 * the workload.
				 */
				maybe_clamp_preempt(fs_info, space_info);

				space_info->flush = 1;
				trace_btrfs_trigger_flush(fs_info,
							  space_info->flags,
							  orig_bytes, flush,
							  "enospc");
				queue_work(system_unbound_wq, async_work);
			}
		} else {
			list_add_tail(&ticket.list,
				      &space_info->priority_tickets);
		}
	} else if (!ret && space_info->flags & BTRFS_BLOCK_GROUP_METADATA) {
		/*
		 * We will do the space reservation dance during log replay,
		 * which means we won't have fs_info->fs_root set, so don't do
		 * the async reclaim as we will panic.
		 */
		if (!test_bit(BTRFS_FS_LOG_RECOVERING, &fs_info->flags) &&
		    !work_busy(&fs_info->preempt_reclaim_work) &&
		    need_preemptive_reclaim(fs_info, space_info)) {
			trace_btrfs_trigger_flush(fs_info, space_info->flags,
						  orig_bytes, flush, "preempt");
			queue_work(system_unbound_wq,
				   &fs_info->preempt_reclaim_work);
		}
	}
	spin_unlock(&space_info->lock);
	if (!ret || !can_ticket(flush))
		return ret;

	return handle_reserve_ticket(fs_info, space_info, &ticket, start_ns,
				     orig_bytes, flush);
}

/*
 * Try to reserve metadata bytes from the block_rsv's space.
 *
 * @fs_info:    the filesystem
 * @space_info: the space_info we're allocating for
 * @orig_bytes: number of bytes we want
 * @flush:      whether or not we can flush to make our reservation
 *
 * This will reserve orig_bytes number of bytes from the space info associated
 * with the block_rsv.  If there is not enough space it will make an attempt to
 * flush out space to make room.  It will do this by flushing delalloc if
 * possible or committing the transaction.  If flush is 0 then no attempts to
 * regain reservations will be made and this will fail if there is not enough
 * space already.
 */
int btrfs_reserve_metadata_bytes(struct btrfs_fs_info *fs_info,
				 struct btrfs_space_info *space_info,
				 u64 orig_bytes,
				 enum btrfs_reserve_flush_enum flush)
{
	int ret;

	ret = __reserve_bytes(fs_info, space_info, orig_bytes, flush);
	if (ret == -ENOSPC) {
		trace_btrfs_space_reservation(fs_info, "space_info:enospc",
					      space_info->flags, orig_bytes, 1);

		if (btrfs_test_opt(fs_info, ENOSPC_DEBUG))
			btrfs_dump_space_info(fs_info, space_info, orig_bytes, 0);
	}
	return ret;
}

/*
 * Try to reserve data bytes for an allocation.
 *
 * @fs_info: the filesystem
 * @bytes:   number of bytes we need
 * @flush:   how we are allowed to flush
 *
 * This will reserve bytes from the data space info.  If there is not enough
 * space then we will attempt to flush space as specified by flush.
 */
int btrfs_reserve_data_bytes(struct btrfs_fs_info *fs_info, u64 bytes,
			     enum btrfs_reserve_flush_enum flush)
{
	struct btrfs_space_info *data_sinfo = fs_info->data_sinfo;
	int ret;

	ASSERT(flush == BTRFS_RESERVE_FLUSH_DATA ||
	       flush == BTRFS_RESERVE_FLUSH_FREE_SPACE_INODE ||
	       flush == BTRFS_RESERVE_NO_FLUSH);
	ASSERT(!current->journal_info || flush != BTRFS_RESERVE_FLUSH_DATA);

	ret = __reserve_bytes(fs_info, data_sinfo, bytes, flush);
	if (ret == -ENOSPC) {
		trace_btrfs_space_reservation(fs_info, "space_info:enospc",
					      data_sinfo->flags, bytes, 1);
		if (btrfs_test_opt(fs_info, ENOSPC_DEBUG))
			btrfs_dump_space_info(fs_info, data_sinfo, bytes, 0);
	}
	return ret;
}

/* Dump all the space infos when we abort a transaction due to ENOSPC. */
__cold void btrfs_dump_space_info_for_trans_abort(struct btrfs_fs_info *fs_info)
{
	struct btrfs_space_info *space_info;

	btrfs_info(fs_info, "dumping space info:");
	list_for_each_entry(space_info, &fs_info->space_info, list) {
		spin_lock(&space_info->lock);
		__btrfs_dump_space_info(fs_info, space_info);
		spin_unlock(&space_info->lock);
	}
	dump_global_block_rsv(fs_info);
}

/*
 * Account the unused space of all the readonly block group in the space_info.
 * takes mirrors into account.
 */
u64 btrfs_account_ro_block_groups_free_space(struct btrfs_space_info *sinfo)
{
	struct btrfs_block_group *block_group;
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

		factor = btrfs_bg_type_to_factor(block_group->flags);
		free_bytes += (block_group->length -
			       block_group->used) * factor;

		spin_unlock(&block_group->lock);
	}
	spin_unlock(&sinfo->lock);

	return free_bytes;
}
