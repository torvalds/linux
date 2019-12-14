// SPDX-License-Identifier: GPL-2.0

#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/sizes.h>
#include <linux/workqueue.h>
#include "ctree.h"
#include "block-group.h"
#include "discard.h"
#include "free-space-cache.h"

/* This is an initial delay to give some chance for block reuse */
#define BTRFS_DISCARD_DELAY		(120ULL * NSEC_PER_SEC)
#define BTRFS_DISCARD_UNUSED_DELAY	(10ULL * NSEC_PER_SEC)

static struct list_head *get_discard_list(struct btrfs_discard_ctl *discard_ctl,
					  struct btrfs_block_group *block_group)
{
	return &discard_ctl->discard_list[block_group->discard_index];
}

static void __add_to_discard_list(struct btrfs_discard_ctl *discard_ctl,
				  struct btrfs_block_group *block_group)
{
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

	list_move_tail(&block_group->discard_list,
		       get_discard_list(discard_ctl, block_group));
}

static void add_to_discard_list(struct btrfs_discard_ctl *discard_ctl,
				struct btrfs_block_group *block_group)
{
	spin_lock(&discard_ctl->lock);
	__add_to_discard_list(discard_ctl, block_group);
	spin_unlock(&discard_ctl->lock);
}

static void add_to_discard_unused_list(struct btrfs_discard_ctl *discard_ctl,
				       struct btrfs_block_group *block_group)
{
	spin_lock(&discard_ctl->lock);

	if (!btrfs_run_discard_work(discard_ctl)) {
		spin_unlock(&discard_ctl->lock);
		return;
	}

	list_del_init(&block_group->discard_list);

	block_group->discard_index = BTRFS_DISCARD_INDEX_UNUSED;
	block_group->discard_eligible_time = (ktime_get_ns() +
					      BTRFS_DISCARD_UNUSED_DELAY);
	block_group->discard_state = BTRFS_DISCARD_RESET_CURSOR;
	list_add_tail(&block_group->discard_list,
		      &discard_ctl->discard_list[BTRFS_DISCARD_INDEX_UNUSED]);

	spin_unlock(&discard_ctl->lock);
}

static bool remove_from_discard_list(struct btrfs_discard_ctl *discard_ctl,
				     struct btrfs_block_group *block_group)
{
	bool running = false;

	spin_lock(&discard_ctl->lock);

	if (block_group == discard_ctl->block_group) {
		running = true;
		discard_ctl->block_group = NULL;
	}

	block_group->discard_eligible_time = 0;
	list_del_init(&block_group->discard_list);

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
 * peek_discard_list - wrap find_next_block_group()
 * @discard_ctl: discard control
 * @discard_state: the discard_state of the block_group after state management
 *
 * This wraps find_next_block_group() and sets the block_group to be in use.
 * discard_state's control flow is managed here.  Variables related to
 * discard_state are reset here as needed (eg. discard_cursor).  @discard_state
 * is remembered as it may change while we're discarding, but we want the
 * discard to execute in the context determined here.
 */
static struct btrfs_block_group *peek_discard_list(
					struct btrfs_discard_ctl *discard_ctl,
					enum btrfs_discard_state *discard_state)
{
	struct btrfs_block_group *block_group;
	const u64 now = ktime_get_ns();

	spin_lock(&discard_ctl->lock);
again:
	block_group = find_next_block_group(discard_ctl, now);

	if (block_group && now > block_group->discard_eligible_time) {
		if (block_group->discard_index == BTRFS_DISCARD_INDEX_UNUSED &&
		    block_group->used != 0) {
			__add_to_discard_list(discard_ctl, block_group);
			goto again;
		}
		if (block_group->discard_state == BTRFS_DISCARD_RESET_CURSOR) {
			block_group->discard_cursor = block_group->start;
			block_group->discard_state = BTRFS_DISCARD_EXTENTS;
		}
		discard_ctl->block_group = block_group;
		*discard_state = block_group->discard_state;
	} else {
		block_group = NULL;
	}

	spin_unlock(&discard_ctl->lock);

	return block_group;
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

/**
 * btrfs_discard_schedule_work - responsible for scheduling the discard work
 * @discard_ctl: discard control
 * @override: override the current timer
 *
 * Discards are issued by a delayed workqueue item.  @override is used to
 * update the current delay as the baseline delay interview is reevaluated
 * on transaction commit.  This is also maxed with any other rate limit.
 */
void btrfs_discard_schedule_work(struct btrfs_discard_ctl *discard_ctl,
				 bool override)
{
	struct btrfs_block_group *block_group;
	const u64 now = ktime_get_ns();

	spin_lock(&discard_ctl->lock);

	if (!btrfs_run_discard_work(discard_ctl))
		goto out;

	if (!override && delayed_work_pending(&discard_ctl->work))
		goto out;

	block_group = find_next_block_group(discard_ctl, now);
	if (block_group) {
		u64 delay = 0;

		if (now < block_group->discard_eligible_time)
			delay = nsecs_to_jiffies(
				block_group->discard_eligible_time - now);

		mod_delayed_work(discard_ctl->discard_workers,
				 &discard_ctl->work, delay);
	}
out:
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
	u64 trimmed = 0;

	discard_ctl = container_of(work, struct btrfs_discard_ctl, work.work);

	block_group = peek_discard_list(discard_ctl, &discard_state);
	if (!block_group || !btrfs_run_discard_work(discard_ctl))
		return;

	/* Perform discarding */
	if (discard_state == BTRFS_DISCARD_BITMAPS)
		btrfs_trim_block_group_bitmaps(block_group, &trimmed,
				       block_group->discard_cursor,
				       btrfs_block_group_end(block_group),
				       0, true);
	else
		btrfs_trim_block_group_extents(block_group, &trimmed,
				       block_group->discard_cursor,
				       btrfs_block_group_end(block_group),
				       0, true);

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

	spin_lock(&discard_ctl->lock);
	discard_ctl->block_group = NULL;
	spin_unlock(&discard_ctl->lock);

	btrfs_discard_schedule_work(discard_ctl, false);
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
 * btrfs_discard_update_discardable - propagate discard counters
 * @block_group: block_group of interest
 * @ctl: free_space_ctl of @block_group
 *
 * This propagates deltas of counters up to the discard_ctl.  It maintains a
 * current counter and a previous counter passing the delta up to the global
 * stat.  Then the current counter value becomes the previous counter value.
 */
void btrfs_discard_update_discardable(struct btrfs_block_group *block_group,
				      struct btrfs_free_space_ctl *ctl)
{
	struct btrfs_discard_ctl *discard_ctl;
	s32 extents_delta;

	if (!block_group || !btrfs_test_opt(block_group->fs_info, DISCARD_ASYNC))
		return;

	discard_ctl = &block_group->fs_info->discard_ctl;

	extents_delta = ctl->discardable_extents[BTRFS_STAT_CURR] -
			ctl->discardable_extents[BTRFS_STAT_PREV];
	if (extents_delta) {
		atomic_add(extents_delta, &discard_ctl->discardable_extents);
		ctl->discardable_extents[BTRFS_STAT_PREV] =
			ctl->discardable_extents[BTRFS_STAT_CURR];
	}
}

/**
 * btrfs_discard_punt_unused_bgs_list - punt unused_bgs list to discard lists
 * @fs_info: fs_info of interest
 *
 * The unused_bgs list needs to be punted to the discard lists because the
 * order of operations is changed.  In the normal sychronous discard path, the
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

	atomic_set(&discard_ctl->discardable_extents, 0);
}

void btrfs_discard_cleanup(struct btrfs_fs_info *fs_info)
{
	btrfs_discard_stop(fs_info);
	cancel_delayed_work_sync(&fs_info->discard_ctl.work);
	btrfs_discard_purge_list(&fs_info->discard_ctl);
}
