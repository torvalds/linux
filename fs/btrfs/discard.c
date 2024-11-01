// SPDX-License-Identifier: GPL-2.0

#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/sizes.h>
#include <linux/workqueue.h>
#include "ctree.h"
#include "block-group.h"
#include "discard.h"
#include "free-space-cache.h"

/*
 * This contains the logic to handle async discard.
 *
 * Async discard manages trimming of free space outside of transaction commit.
 * Discarding is done by managing the block_groups on a LRU list based on free
 * space recency.  Two passes are used to first prioritize discarding extents
 * and then allow for trimming in the bitmap the best opportunity to coalesce.
 * The block_groups are maintained on multiple lists to allow for multiple
 * passes with different discard filter requirements.  A delayed work item is
 * used to manage discarding with timeout determined by a max of the delay
 * incurred by the iops rate limit, the byte rate limit, and the max delay of
 * BTRFS_DISCARD_MAX_DELAY.
 *
 * Note, this only keeps track of block_groups that are explicitly for data.
 * Mixed block_groups are not supported.
 *
 * The first list is special to manage discarding of fully free block groups.
 * This is necessary because we issue a final trim for a full free block group
 * after forgetting it.  When a block group becomes unused, instead of directly
 * being added to the unused_bgs list, we add it to this first list.  Then
 * from there, if it becomes fully discarded, we place it onto the unused_bgs
 * list.
 *
 * The in-memory free space cache serves as the backing state for discard.
 * Consequently this means there is no persistence.  We opt to load all the
 * block groups in as not discarded, so the mount case degenerates to the
 * crashing case.
 *
 * As the free space cache uses bitmaps, there exists a tradeoff between
 * ease/efficiency for find_free_extent() and the accuracy of discard state.
 * Here we opt to let untrimmed regions merge with everything while only letting
 * trimmed regions merge with other trimmed regions.  This can cause
 * overtrimming, but the coalescing benefit seems to be worth it.  Additionally,
 * bitmap state is tracked as a whole.  If we're able to fully trim a bitmap,
 * the trimmed flag is set on the bitmap.  Otherwise, if an allocation comes in,
 * this resets the state and we will retry trimming the whole bitmap.  This is a
 * tradeoff between discard state accuracy and the cost of accounting.
 */

/* This is an initial delay to give some chance for block reuse */
#define BTRFS_DISCARD_DELAY		(120ULL * NSEC_PER_SEC)
#define BTRFS_DISCARD_UNUSED_DELAY	(10ULL * NSEC_PER_SEC)

/* Target completion latency of discarding all discardable extents */
#define BTRFS_DISCARD_TARGET_MSEC	(6 * 60 * 60UL * MSEC_PER_SEC)
#define BTRFS_DISCARD_MIN_DELAY_MSEC	(1UL)
#define BTRFS_DISCARD_MAX_DELAY_MSEC	(1000UL)
#define BTRFS_DISCARD_MAX_IOPS		(10U)

/* Montonically decreasing minimum length filters after index 0 */
static int discard_minlen[BTRFS_NR_DISCARD_LISTS] = {
	0,
	BTRFS_ASYNC_DISCARD_MAX_FILTER,
	BTRFS_ASYNC_DISCARD_MIN_FILTER
};

static struct list_head *get_discard_list(struct btrfs_discard_ctl *discard_ctl,
					  struct btrfs_block_group *block_group)
{
	return &discard_ctl->discard_list[block_group->discard_index];
}

static void __add_to_discard_list(struct btrfs_discard_ctl *discard_ctl,
				  struct btrfs_block_group *block_group)
{
	lockdep_assert_held(&discard_ctl->lock);
	if (!btrfs_run_discard_work(discard_ctl))
		return;

	if (list_empty(&block_group->discard_list) ||
	    block_group->discard_index == BTRFS_DISCARD_INDEX_UNUSED) {
		if (block_group->discard_index == BTRFS_DISCARD_INDEX_UNUSED)
			block_group->discard_index = BTRFS_DISCARD_INDEX_START;
		block_group->discard_eligible_time = (ktime_get_ns() +
						      BTRFS_DISCARD_DELAY);
		block_group->discard_state = BTRFS_DISCARD_RESET_CURSOR;
	}
	if (list_empty(&block_group->discard_list))
		btrfs_get_block_group(block_group);

	list_move_tail(&block_group->discard_list,
		       get_discard_list(discard_ctl, block_group));
}

static void add_to_discard_list(struct btrfs_discard_ctl *discard_ctl,
				struct btrfs_block_group *block_group)
{
	if (!btrfs_is_block_group_data_only(block_group))
		return;

	spin_lock(&discard_ctl->lock);
	__add_to_discard_list(discard_ctl, block_group);
	spin_unlock(&discard_ctl->lock);
}

static void add_to_discard_unused_list(struct btrfs_discard_ctl *discard_ctl,
				       struct btrfs_block_group *block_group)
{
	bool queued;

	spin_lock(&discard_ctl->lock);

	queued = !list_empty(&block_group->discard_list);

	if (!btrfs_run_discard_work(discard_ctl)) {
		spin_unlock(&discard_ctl->lock);
		return;
	}

	list_del_init(&block_group->discard_list);

	block_group->discard_index = BTRFS_DISCARD_INDEX_UNUSED;
	block_group->discard_eligible_time = (ktime_get_ns() +
					      BTRFS_DISCARD_UNUSED_DELAY);
	block_group->discard_state = BTRFS_DISCARD_RESET_CURSOR;
	if (!queued)
		btrfs_get_block_group(block_group);
	list_add_tail(&block_group->discard_list,
		      &discard_ctl->discard_list[BTRFS_DISCARD_INDEX_UNUSED]);

	spin_unlock(&discard_ctl->lock);
}

static bool remove_from_discard_list(struct btrfs_discard_ctl *discard_ctl,
				     struct btrfs_block_group *block_group)
{
	bool running = false;
	bool queued = false;

	spin_lock(&discard_ctl->lock);

	if (block_group == discard_ctl->block_group) {
		running = true;
		discard_ctl->block_group = NULL;
	}

	block_group->discard_eligible_time = 0;
	queued = !list_empty(&block_group->discard_list);
	list_del_init(&block_group->discard_list);
	/*
	 * If the block group is currently running in the discard workfn, we
	 * don't want to deref it, since it's still being used by the workfn.
	 * The workfn will notice this case and deref the block group when it is
	 * finished.
	 */
	if (queued && !running)
		btrfs_put_block_group(block_group);

	spin_unlock(&discard_ctl->lock);

	return running;
}

/**
 * find_next_block_group - find block_group that's up next for discarding
 * @discard_ctl: discard control
 * @now: current time
 *
 * Iterate over the discard lists to find the next block_group up for
 * discarding checking the discard_eligible_time of block_group.
 */
static struct btrfs_block_group *find_next_block_group(
					struct btrfs_discard_ctl *discard_ctl,
					u64 now)
{
	struct btrfs_block_group *ret_block_group = NULL, *block_group;
	int i;

	for (i = 0; i < BTRFS_NR_DISCARD_LISTS; i++) {
		struct list_head *discard_list = &discard_ctl->discard_list[i];

		if (!list_empty(discard_list)) {
			block_group = list_first_entry(discard_list,
						       struct btrfs_block_group,
						       discard_list);

			if (!ret_block_group)
				ret_block_group = block_group;

			if (ret_block_group->discard_eligible_time < now)
				break;

			if (ret_block_group->discard_eligible_time >
			    block_group->discard_eligible_time)
				ret_block_group = block_group;
		}
	}

	return ret_block_group;
}

/**
 * Wrap find_next_block_group()
 *
 * @discard_ctl:   discard control
 * @discard_state: the discard_state of the block_group after state management
 * @discard_index: the discard_index of the block_group after state management
 * @now:           time when discard was invoked, in ns
 *
 * This wraps find_next_block_group() and sets the block_group to be in use.
 * discard_state's control flow is managed here.  Variables related to
 * discard_state are reset here as needed (eg discard_cursor).  @discard_state
 * and @discard_index are remembered as it may change while we're discarding,
 * but we want the discard to execute in the context determined here.
 */
static struct btrfs_block_group *peek_discard_list(
					struct btrfs_discard_ctl *discard_ctl,
					enum btrfs_discard_state *discard_state,
					int *discard_index, u64 now)
{
	struct btrfs_block_group *block_group;

	spin_lock(&discard_ctl->lock);
again:
	block_group = find_next_block_group(discard_ctl, now);

	if (block_group && now >= block_group->discard_eligible_time) {
		if (block_group->discard_index == BTRFS_DISCARD_INDEX_UNUSED &&
		    block_group->used != 0) {
			if (btrfs_is_block_group_data_only(block_group)) {
				__add_to_discard_list(discard_ctl, block_group);
			} else {
				list_del_init(&block_group->discard_list);
				btrfs_put_block_group(block_group);
			}
			goto again;
		}
		if (block_group->discard_state == BTRFS_DISCARD_RESET_CURSOR) {
			block_group->discard_cursor = block_group->start;
			block_group->discard_state = BTRFS_DISCARD_EXTENTS;
		}
		discard_ctl->block_group = block_group;
	}
	if (block_group) {
		*discard_state = block_group->discard_state;
		*discard_index = block_group->discard_index;
	}
	spin_unlock(&discard_ctl->lock);

	return block_group;
}

/**
 * btrfs_discard_check_filter - updates a block groups filters
 * @block_group: block group of interest
 * @bytes: recently freed region size after coalescing
 *
 * Async discard maintains multiple lists with progressively smaller filters
 * to prioritize discarding based on size.  Should a free space that matches
 * a larger filter be returned to the free_space_cache, prioritize that discard
 * by moving @block_group to the proper filter.
 */
void btrfs_discard_check_filter(struct btrfs_block_group *block_group,
				u64 bytes)
{
	struct btrfs_discard_ctl *discard_ctl;

	if (!block_group ||
	    !btrfs_test_opt(block_group->fs_info, DISCARD_ASYNC))
		return;

	discard_ctl = &block_group->fs_info->discard_ctl;

	if (block_group->discard_index > BTRFS_DISCARD_INDEX_START &&
	    bytes >= discard_minlen[block_group->discard_index - 1]) {
		int i;

		remove_from_discard_list(discard_ctl, block_group);

		for (i = BTRFS_DISCARD_INDEX_START; i < BTRFS_NR_DISCARD_LISTS;
		     i++) {
			if (bytes >= discard_minlen[i]) {
				block_group->discard_index = i;
				add_to_discard_list(discard_ctl, block_group);
				break;
			}
		}
	}
}

/**
 * btrfs_update_discard_index - moves a block group along the discard lists
 * @discard_ctl: discard control
 * @block_group: block_group of interest
 *
 * Increment @block_group's discard_index.  If it falls of the list, let it be.
 * Otherwise add it back to the appropriate list.
 */
static void btrfs_update_discard_index(struct btrfs_discard_ctl *discard_ctl,
				       struct btrfs_block_group *block_group)
{
	block_group->discard_index++;
	if (block_group->discard_index == BTRFS_NR_DISCARD_LISTS) {
		block_group->discard_index = 1;
		return;
	}

	add_to_discard_list(discard_ctl, block_group);
}

/**
 * btrfs_discard_cancel_work - remove a block_group from the discard lists
 * @discard_ctl: discard control
 * @block_group: block_group of interest
 *
 * This removes @block_group from the discard lists.  If necessary, it waits on
 * the current work and then reschedules the delayed work.
 */
void btrfs_discard_cancel_work(struct btrfs_discard_ctl *discard_ctl,
			       struct btrfs_block_group *block_group)
{
	if (remove_from_discard_list(discard_ctl, block_group)) {
		cancel_delayed_work_sync(&discard_ctl->work);
		btrfs_discard_schedule_work(discard_ctl, true);
	}
}

/**
 * btrfs_discard_queue_work - handles queuing the block_groups
 * @discard_ctl: discard control
 * @block_group: block_group of interest
 *
 * This maintains the LRU order of the discard lists.
 */
void btrfs_discard_queue_work(struct btrfs_discard_ctl *discard_ctl,
			      struct btrfs_block_group *block_group)
{
	if (!block_group || !btrfs_test_opt(block_group->fs_info, DISCARD_ASYNC))
		return;

	if (block_group->used == 0)
		add_to_discard_unused_list(discard_ctl, block_group);
	else
		add_to_discard_list(discard_ctl, block_group);

	if (!delayed_work_pending(&discard_ctl->work))
		btrfs_discard_schedule_work(discard_ctl, false);
}

static void __btrfs_discard_schedule_work(struct btrfs_discard_ctl *discard_ctl,
					  u64 now, bool override)
{
	struct btrfs_block_group *block_group;

	if (!btrfs_run_discard_work(discard_ctl))
		return;
	if (!override && delayed_work_pending(&discard_ctl->work))
		return;

	block_group = find_next_block_group(discard_ctl, now);
	if (block_group) {
		u64 delay = discard_ctl->delay_ms * NSEC_PER_MSEC;
		u32 kbps_limit = READ_ONCE(discard_ctl->kbps_limit);

		/*
		 * A single delayed workqueue item is responsible for
		 * discarding, so we can manage the bytes rate limit by keeping
		 * track of the previous discard.
		 */
		if (kbps_limit && discard_ctl->prev_discard) {
			u64 bps_limit = ((u64)kbps_limit) * SZ_1K;
			u64 bps_delay = div64_u64(discard_ctl->prev_discard *
						  NSEC_PER_SEC, bps_limit);

			delay = max(delay, bps_delay);
		}

		/*
		 * This timeout is to hopefully prevent immediate discarding
		 * in a recently allocated block group.
		 */
		if (now < block_group->discard_eligible_time) {
			u64 bg_timeout = block_group->discard_eligible_time - now;

			delay = max(delay, bg_timeout);
		}

		if (override && discard_ctl->prev_discard) {
			u64 elapsed = now - discard_ctl->prev_discard_time;

			if (delay > elapsed)
				delay -= elapsed;
			else
				delay = 0;
		}

		mod_delayed_work(discard_ctl->discard_workers,
				 &discard_ctl->work, nsecs_to_jiffies(delay));
	}
}

/*
 * btrfs_discard_schedule_work - responsible for scheduling the discard work
 * @discard_ctl:  discard control
 * @override:     override the current timer
 *
 * Discards are issued by a delayed workqueue item.  @override is used to
 * update the current delay as the baseline delay interval is reevaluated on
 * transaction commit.  This is also maxed with any other rate limit.
 */
void btrfs_discard_schedule_work(struct btrfs_discard_ctl *discard_ctl,
				 bool override)
{
	const u64 now = ktime_get_ns();

	spin_lock(&discard_ctl->lock);
	__btrfs_discard_schedule_work(discard_ctl, now, override);
	spin_unlock(&discard_ctl->lock);
}

/**
 * btrfs_finish_discard_pass - determine next step of a block_group
 * @discard_ctl: discard control
 * @block_group: block_group of interest
 *
 * This determines the next step for a block group after it's finished going
 * through a pass on a discard list.  If it is unused and fully trimmed, we can
 * mark it unused and send it to the unused_bgs path.  Otherwise, pass it onto
 * the appropriate filter list or let it fall off.
 */
static void btrfs_finish_discard_pass(struct btrfs_discard_ctl *discard_ctl,
				      struct btrfs_block_group *block_group)
{
	remove_from_discard_list(discard_ctl, block_group);

	if (block_group->used == 0) {
		if (btrfs_is_free_space_trimmed(block_group))
			btrfs_mark_bg_unused(block_group);
		else
			add_to_discard_unused_list(discard_ctl, block_group);
	} else {
		btrfs_update_discard_index(discard_ctl, block_group);
	}
}

/**
 * btrfs_discard_workfn - discard work function
 * @work: work
 *
 * This finds the next block_group to start discarding and then discards a
 * single region.  It does this in a two-pass fashion: first extents and second
 * bitmaps.  Completely discarded block groups are sent to the unused_bgs path.
 */
static void btrfs_discard_workfn(struct work_struct *work)
{
	struct btrfs_discard_ctl *discard_ctl;
	struct btrfs_block_group *block_group;
	enum btrfs_discard_state discard_state;
	int discard_index = 0;
	u64 trimmed = 0;
	u64 minlen = 0;
	u64 now = ktime_get_ns();

	discard_ctl = container_of(work, struct btrfs_discard_ctl, work.work);

	block_group = peek_discard_list(discard_ctl, &discard_state,
					&discard_index, now);
	if (!block_group || !btrfs_run_discard_work(discard_ctl))
		return;
	if (now < block_group->discard_eligible_time) {
		btrfs_discard_schedule_work(discard_ctl, false);
		return;
	}

	/* Perform discarding */
	minlen = discard_minlen[discard_index];

	if (discard_state == BTRFS_DISCARD_BITMAPS) {
		u64 maxlen = 0;

		/*
		 * Use the previous levels minimum discard length as the max
		 * length filter.  In the case something is added to make a
		 * region go beyond the max filter, the entire bitmap is set
		 * back to BTRFS_TRIM_STATE_UNTRIMMED.
		 */
		if (discard_index != BTRFS_DISCARD_INDEX_UNUSED)
			maxlen = discard_minlen[discard_index - 1];

		btrfs_trim_block_group_bitmaps(block_group, &trimmed,
				       block_group->discard_cursor,
				       btrfs_block_group_end(block_group),
				       minlen, maxlen, true);
		discard_ctl->discard_bitmap_bytes += trimmed;
	} else {
		btrfs_trim_block_group_extents(block_group, &trimmed,
				       block_group->discard_cursor,
				       btrfs_block_group_end(block_group),
				       minlen, true);
		discard_ctl->discard_extent_bytes += trimmed;
	}

	/* Determine next steps for a block_group */
	if (block_group->discard_cursor >= btrfs_block_group_end(block_group)) {
		if (discard_state == BTRFS_DISCARD_BITMAPS) {
			btrfs_finish_discard_pass(discard_ctl, block_group);
		} else {
			block_group->discard_cursor = block_group->start;
			spin_lock(&discard_ctl->lock);
			if (block_group->discard_state !=
			    BTRFS_DISCARD_RESET_CURSOR)
				block_group->discard_state =
							BTRFS_DISCARD_BITMAPS;
			spin_unlock(&discard_ctl->lock);
		}
	}

	now = ktime_get_ns();
	spin_lock(&discard_ctl->lock);
	discard_ctl->prev_discard = trimmed;
	discard_ctl->prev_discard_time = now;
	/*
	 * If the block group was removed from the discard list while it was
	 * running in this workfn, then we didn't deref it, since this function
	 * still owned that reference. But we set the discard_ctl->block_group
	 * back to NULL, so we can use that condition to know that now we need
	 * to deref the block_group.
	 */
	if (discard_ctl->block_group == NULL)
		btrfs_put_block_group(block_group);
	discard_ctl->block_group = NULL;
	__btrfs_discard_schedule_work(discard_ctl, now, false);
	spin_unlock(&discard_ctl->lock);
}

/**
 * btrfs_run_discard_work - determines if async discard should be running
 * @discard_ctl: discard control
 *
 * Checks if the file system is writeable and BTRFS_FS_DISCARD_RUNNING is set.
 */
bool btrfs_run_discard_work(struct btrfs_discard_ctl *discard_ctl)
{
	struct btrfs_fs_info *fs_info = container_of(discard_ctl,
						     struct btrfs_fs_info,
						     discard_ctl);

	return (!(fs_info->sb->s_flags & SB_RDONLY) &&
		test_bit(BTRFS_FS_DISCARD_RUNNING, &fs_info->flags));
}

/**
 * btrfs_discard_calc_delay - recalculate the base delay
 * @discard_ctl: discard control
 *
 * Recalculate the base delay which is based off the total number of
 * discardable_extents.  Clamp this between the lower_limit (iops_limit or 1ms)
 * and the upper_limit (BTRFS_DISCARD_MAX_DELAY_MSEC).
 */
void btrfs_discard_calc_delay(struct btrfs_discard_ctl *discard_ctl)
{
	s32 discardable_extents;
	s64 discardable_bytes;
	u32 iops_limit;
	unsigned long delay;

	discardable_extents = atomic_read(&discard_ctl->discardable_extents);
	if (!discardable_extents)
		return;

	spin_lock(&discard_ctl->lock);

	/*
	 * The following is to fix a potential -1 discrepenancy that we're not
	 * sure how to reproduce. But given that this is the only place that
	 * utilizes these numbers and this is only called by from
	 * btrfs_finish_extent_commit() which is synchronized, we can correct
	 * here.
	 */
	if (discardable_extents < 0)
		atomic_add(-discardable_extents,
			   &discard_ctl->discardable_extents);

	discardable_bytes = atomic64_read(&discard_ctl->discardable_bytes);
	if (discardable_bytes < 0)
		atomic64_add(-discardable_bytes,
			     &discard_ctl->discardable_bytes);

	if (discardable_extents <= 0) {
		spin_unlock(&discard_ctl->lock);
		return;
	}

	iops_limit = READ_ONCE(discard_ctl->iops_limit);
	if (iops_limit)
		delay = MSEC_PER_SEC / iops_limit;
	else
		delay = BTRFS_DISCARD_TARGET_MSEC / discardable_extents;

	delay = clamp(delay, BTRFS_DISCARD_MIN_DELAY_MSEC,
		      BTRFS_DISCARD_MAX_DELAY_MSEC);
	discard_ctl->delay_ms = delay;

	spin_unlock(&discard_ctl->lock);
}

/**
 * btrfs_discard_update_discardable - propagate discard counters
 * @block_group: block_group of interest
 *
 * This propagates deltas of counters up to the discard_ctl.  It maintains a
 * current counter and a previous counter passing the delta up to the global
 * stat.  Then the current counter value becomes the previous counter value.
 */
void btrfs_discard_update_discardable(struct btrfs_block_group *block_group)
{
	struct btrfs_free_space_ctl *ctl;
	struct btrfs_discard_ctl *discard_ctl;
	s32 extents_delta;
	s64 bytes_delta;

	if (!block_group ||
	    !btrfs_test_opt(block_group->fs_info, DISCARD_ASYNC) ||
	    !btrfs_is_block_group_data_only(block_group))
		return;

	ctl = block_group->free_space_ctl;
	discard_ctl = &block_group->fs_info->discard_ctl;

	lockdep_assert_held(&ctl->tree_lock);
	extents_delta = ctl->discardable_extents[BTRFS_STAT_CURR] -
			ctl->discardable_extents[BTRFS_STAT_PREV];
	if (extents_delta) {
		atomic_add(extents_delta, &discard_ctl->discardable_extents);
		ctl->discardable_extents[BTRFS_STAT_PREV] =
			ctl->discardable_extents[BTRFS_STAT_CURR];
	}

	bytes_delta = ctl->discardable_bytes[BTRFS_STAT_CURR] -
		      ctl->discardable_bytes[BTRFS_STAT_PREV];
	if (bytes_delta) {
		atomic64_add(bytes_delta, &discard_ctl->discardable_bytes);
		ctl->discardable_bytes[BTRFS_STAT_PREV] =
			ctl->discardable_bytes[BTRFS_STAT_CURR];
	}
}

/**
 * btrfs_discard_punt_unused_bgs_list - punt unused_bgs list to discard lists
 * @fs_info: fs_info of interest
 *
 * The unused_bgs list needs to be punted to the discard lists because the
 * order of operations is changed.  In the normal synchronous discard path, the
 * block groups are trimmed via a single large trim in transaction commit.  This
 * is ultimately what we are trying to avoid with asynchronous discard.  Thus,
 * it must be done before going down the unused_bgs path.
 */
void btrfs_discard_punt_unused_bgs_list(struct btrfs_fs_info *fs_info)
{
	struct btrfs_block_group *block_group, *next;

	spin_lock(&fs_info->unused_bgs_lock);
	/* We enabled async discard, so punt all to the queue */
	list_for_each_entry_safe(block_group, next, &fs_info->unused_bgs,
				 bg_list) {
		list_del_init(&block_group->bg_list);
		btrfs_discard_queue_work(&fs_info->discard_ctl, block_group);
		/*
		 * This put is for the get done by btrfs_mark_bg_unused.
		 * Queueing discard incremented it for discard's reference.
		 */
		btrfs_put_block_group(block_group);
	}
	spin_unlock(&fs_info->unused_bgs_lock);
}

/**
 * btrfs_discard_purge_list - purge discard lists
 * @discard_ctl: discard control
 *
 * If we are disabling async discard, we may have intercepted block groups that
 * are completely free and ready for the unused_bgs path.  As discarding will
 * now happen in transaction commit or not at all, we can safely mark the
 * corresponding block groups as unused and they will be sent on their merry
 * way to the unused_bgs list.
 */
static void btrfs_discard_purge_list(struct btrfs_discard_ctl *discard_ctl)
{
	struct btrfs_block_group *block_group, *next;
	int i;

	spin_lock(&discard_ctl->lock);
	for (i = 0; i < BTRFS_NR_DISCARD_LISTS; i++) {
		list_for_each_entry_safe(block_group, next,
					 &discard_ctl->discard_list[i],
					 discard_list) {
			list_del_init(&block_group->discard_list);
			spin_unlock(&discard_ctl->lock);
			if (block_group->used == 0)
				btrfs_mark_bg_unused(block_group);
			spin_lock(&discard_ctl->lock);
			btrfs_put_block_group(block_group);
		}
	}
	spin_unlock(&discard_ctl->lock);
}

void btrfs_discard_resume(struct btrfs_fs_info *fs_info)
{
	if (!btrfs_test_opt(fs_info, DISCARD_ASYNC)) {
		btrfs_discard_cleanup(fs_info);
		return;
	}

	btrfs_discard_punt_unused_bgs_list(fs_info);

	set_bit(BTRFS_FS_DISCARD_RUNNING, &fs_info->flags);
}

void btrfs_discard_stop(struct btrfs_fs_info *fs_info)
{
	clear_bit(BTRFS_FS_DISCARD_RUNNING, &fs_info->flags);
}

void btrfs_discard_init(struct btrfs_fs_info *fs_info)
{
	struct btrfs_discard_ctl *discard_ctl = &fs_info->discard_ctl;
	int i;

	spin_lock_init(&discard_ctl->lock);
	INIT_DELAYED_WORK(&discard_ctl->work, btrfs_discard_workfn);

	for (i = 0; i < BTRFS_NR_DISCARD_LISTS; i++)
		INIT_LIST_HEAD(&discard_ctl->discard_list[i]);

	discard_ctl->prev_discard = 0;
	discard_ctl->prev_discard_time = 0;
	atomic_set(&discard_ctl->discardable_extents, 0);
	atomic64_set(&discard_ctl->discardable_bytes, 0);
	discard_ctl->max_discard_size = BTRFS_ASYNC_DISCARD_DEFAULT_MAX_SIZE;
	discard_ctl->delay_ms = BTRFS_DISCARD_MAX_DELAY_MSEC;
	discard_ctl->iops_limit = BTRFS_DISCARD_MAX_IOPS;
	discard_ctl->kbps_limit = 0;
	discard_ctl->discard_extent_bytes = 0;
	discard_ctl->discard_bitmap_bytes = 0;
	atomic64_set(&discard_ctl->discard_bytes_saved, 0);
}

void btrfs_discard_cleanup(struct btrfs_fs_info *fs_info)
{
	btrfs_discard_stop(fs_info);
	cancel_delayed_work_sync(&fs_info->discard_ctl.work);
	btrfs_discard_purge_list(&fs_info->discard_ctl);
}
