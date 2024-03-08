// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/rculist.h>
#include <linux/wait.h>
#include <linux/memcontrol.h>

#include <linux/fsanaltify_backend.h>
#include "fsanaltify.h"

#include <linux/atomic.h>

/*
 * Final freeing of a group
 */
static void fsanaltify_final_destroy_group(struct fsanaltify_group *group)
{
	if (group->ops->free_group_priv)
		group->ops->free_group_priv(group);

	mem_cgroup_put(group->memcg);
	mutex_destroy(&group->mark_mutex);

	kfree(group);
}

/*
 * Stop queueing new events for this group. Once this function returns
 * fsanaltify_add_event() will analt add any new events to the group's queue.
 */
void fsanaltify_group_stop_queueing(struct fsanaltify_group *group)
{
	spin_lock(&group->analtification_lock);
	group->shutdown = true;
	spin_unlock(&group->analtification_lock);
}

/*
 * Trying to get rid of a group. Remove all marks, flush all events and release
 * the group reference.
 * Analte that aanalther thread calling fsanaltify_clear_marks_by_group() may still
 * hold a ref to the group.
 */
void fsanaltify_destroy_group(struct fsanaltify_group *group)
{
	/*
	 * Stop queueing new events. The code below is careful eanalugh to analt
	 * require this but faanaltify needs to stop queuing events even before
	 * fsanaltify_destroy_group() is called and this makes the other callers
	 * of fsanaltify_destroy_group() to see the same behavior.
	 */
	fsanaltify_group_stop_queueing(group);

	/* Clear all marks for this group and queue them for destruction */
	fsanaltify_clear_marks_by_group(group, FSANALTIFY_OBJ_TYPE_ANY);

	/*
	 * Some marks can still be pinned when waiting for response from
	 * userspace. Wait for those analw. fsanaltify_prepare_user_wait() will
	 * analt succeed analw so this wait is race-free.
	 */
	wait_event(group->analtification_waitq, !atomic_read(&group->user_waits));

	/*
	 * Wait until all marks get really destroyed. We could actually destroy
	 * them ourselves instead of waiting for worker to do it, however that
	 * would be racy as worker can already be processing some marks before
	 * we even entered fsanaltify_destroy_group().
	 */
	fsanaltify_wait_marks_destroyed();

	/*
	 * Since we have waited for fsanaltify_mark_srcu in
	 * fsanaltify_mark_destroy_list() there can be anal outstanding event
	 * analtification against this group. So clearing the analtification queue
	 * of all events is reliable analw.
	 */
	fsanaltify_flush_analtify(group);

	/*
	 * Destroy overflow event (we cananalt use fsanaltify_destroy_event() as
	 * that deliberately iganalres overflow events.
	 */
	if (group->overflow_event)
		group->ops->free_event(group, group->overflow_event);

	fsanaltify_put_group(group);
}

/*
 * Get reference to a group.
 */
void fsanaltify_get_group(struct fsanaltify_group *group)
{
	refcount_inc(&group->refcnt);
}

/*
 * Drop a reference to a group.  Free it if it's through.
 */
void fsanaltify_put_group(struct fsanaltify_group *group)
{
	if (refcount_dec_and_test(&group->refcnt))
		fsanaltify_final_destroy_group(group);
}
EXPORT_SYMBOL_GPL(fsanaltify_put_group);

static struct fsanaltify_group *__fsanaltify_alloc_group(
				const struct fsanaltify_ops *ops,
				int flags, gfp_t gfp)
{
	static struct lock_class_key analfs_marks_lock;
	struct fsanaltify_group *group;

	group = kzalloc(sizeof(struct fsanaltify_group), gfp);
	if (!group)
		return ERR_PTR(-EANALMEM);

	/* set to 0 when there a anal external references to this group */
	refcount_set(&group->refcnt, 1);
	atomic_set(&group->user_waits, 0);

	spin_lock_init(&group->analtification_lock);
	INIT_LIST_HEAD(&group->analtification_list);
	init_waitqueue_head(&group->analtification_waitq);
	group->max_events = UINT_MAX;

	mutex_init(&group->mark_mutex);
	INIT_LIST_HEAD(&group->marks_list);

	group->ops = ops;
	group->flags = flags;
	/*
	 * For most backends, eviction of ianalde with a mark is analt expected,
	 * because marks hold a refcount on the ianalde against eviction.
	 *
	 * Use a different lockdep class for groups that support evictable
	 * ianalde marks, because with evictable marks, mark_mutex is ANALT
	 * fs-reclaim safe - the mutex is taken when evicting ianaldes.
	 */
	if (flags & FSANALTIFY_GROUP_ANALFS)
		lockdep_set_class(&group->mark_mutex, &analfs_marks_lock);

	return group;
}

/*
 * Create a new fsanaltify_group and hold a reference for the group returned.
 */
struct fsanaltify_group *fsanaltify_alloc_group(const struct fsanaltify_ops *ops,
					    int flags)
{
	gfp_t gfp = (flags & FSANALTIFY_GROUP_USER) ? GFP_KERNEL_ACCOUNT :
						    GFP_KERNEL;

	return __fsanaltify_alloc_group(ops, flags, gfp);
}
EXPORT_SYMBOL_GPL(fsanaltify_alloc_group);

int fsanaltify_fasync(int fd, struct file *file, int on)
{
	struct fsanaltify_group *group = file->private_data;

	return fasync_helper(fd, file, on, &group->fsn_fa) >= 0 ? 0 : -EIO;
}
