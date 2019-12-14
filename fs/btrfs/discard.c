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

static struct list_head *get_discard_list(struct btrfs_discard_ctl *discard_ctl,
					  struct btrfs_block_group *block_group)
{
	return &discard_ctl->discard_list[block_group->discard_index];
}

static void add_to_discard_list(struct btrfs_discard_ctl *discard_ctl,
				struct btrfs_block_group *block_group)
{
	spin_lock(&discard_ctl->lock);

	if (!btrfs_run_discard_work(discard_ctl)) {
		spin_unlock(&discard_ctl->lock);
		return;
	}

	if (list_empty(&block_group->discard_list))
		block_group->discard_eligible_time = (ktime_get_ns() +
						      BTRFS_DISCARD_DELAY);

	list_move_tail(&block_group->discard_list,
		       get_discard_list(discard_ctl, block_group));

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
 *
 * This wraps find_next_block_group() and sets the block_group to be in use.
 */
static struct btrfs_block_group *peek_discard_list(
					struct btrfs_discard_ctl *discard_ctl)
{
	struct btrfs_block_group *block_group;
	const u64 now = ktime_get_ns();

	spin_lock(&discard_ctl->lock);

	block_group = find_next_block_group(discard_ctl, now);

	if (block_group && now < block_group->discard_eligible_time)
		block_group = NULL;

	discard_ctl->block_group = block_group;

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
 * btrfs_discard_workfn - discard work function
 * @work: work
 *
 * This finds the next block_group to start discarding and then discards it.
 */
static void btrfs_discard_workfn(struct work_struct *work)
{
	struct btrfs_discard_ctl *discard_ctl;
	struct btrfs_block_group *block_group;
	u64 trimmed = 0;

	discard_ctl = container_of(work, struct btrfs_discard_ctl, work.work);

	block_group = peek_discard_list(discard_ctl);
	if (!block_group || !btrfs_run_discard_work(discard_ctl))
		return;

	btrfs_trim_block_group(block_group, &trimmed, block_group->start,
			       btrfs_block_group_end(block_group), 0);

	remove_from_discard_list(discard_ctl, block_group);
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

void btrfs_discard_resume(struct btrfs_fs_info *fs_info)
{
	if (!btrfs_test_opt(fs_info, DISCARD_ASYNC)) {
		btrfs_discard_cleanup(fs_info);
		return;
	}

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
}

void btrfs_discard_cleanup(struct btrfs_fs_info *fs_info)
{
	btrfs_discard_stop(fs_info);
	cancel_delayed_work_sync(&fs_info->discard_ctl.work);
}
