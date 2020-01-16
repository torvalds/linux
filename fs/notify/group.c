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

#include <linux/fsyestify_backend.h>
#include "fsyestify.h"

#include <linux/atomic.h>

/*
 * Final freeing of a group
 */
static void fsyestify_final_destroy_group(struct fsyestify_group *group)
{
	if (group->ops->free_group_priv)
		group->ops->free_group_priv(group);

	mem_cgroup_put(group->memcg);

	kfree(group);
}

/*
 * Stop queueing new events for this group. Once this function returns
 * fsyestify_add_event() will yest add any new events to the group's queue.
 */
void fsyestify_group_stop_queueing(struct fsyestify_group *group)
{
	spin_lock(&group->yestification_lock);
	group->shutdown = true;
	spin_unlock(&group->yestification_lock);
}

/*
 * Trying to get rid of a group. Remove all marks, flush all events and release
 * the group reference.
 * Note that ayesther thread calling fsyestify_clear_marks_by_group() may still
 * hold a ref to the group.
 */
void fsyestify_destroy_group(struct fsyestify_group *group)
{
	/*
	 * Stop queueing new events. The code below is careful eyesugh to yest
	 * require this but fayestify needs to stop queuing events even before
	 * fsyestify_destroy_group() is called and this makes the other callers
	 * of fsyestify_destroy_group() to see the same behavior.
	 */
	fsyestify_group_stop_queueing(group);

	/* Clear all marks for this group and queue them for destruction */
	fsyestify_clear_marks_by_group(group, FSNOTIFY_OBJ_ALL_TYPES_MASK);

	/*
	 * Some marks can still be pinned when waiting for response from
	 * userspace. Wait for those yesw. fsyestify_prepare_user_wait() will
	 * yest succeed yesw so this wait is race-free.
	 */
	wait_event(group->yestification_waitq, !atomic_read(&group->user_waits));

	/*
	 * Wait until all marks get really destroyed. We could actually destroy
	 * them ourselves instead of waiting for worker to do it, however that
	 * would be racy as worker can already be processing some marks before
	 * we even entered fsyestify_destroy_group().
	 */
	fsyestify_wait_marks_destroyed();

	/*
	 * Since we have waited for fsyestify_mark_srcu in
	 * fsyestify_mark_destroy_list() there can be yes outstanding event
	 * yestification against this group. So clearing the yestification queue
	 * of all events is reliable yesw.
	 */
	fsyestify_flush_yestify(group);

	/*
	 * Destroy overflow event (we canyest use fsyestify_destroy_event() as
	 * that deliberately igyesres overflow events.
	 */
	if (group->overflow_event)
		group->ops->free_event(group->overflow_event);

	fsyestify_put_group(group);
}

/*
 * Get reference to a group.
 */
void fsyestify_get_group(struct fsyestify_group *group)
{
	refcount_inc(&group->refcnt);
}

/*
 * Drop a reference to a group.  Free it if it's through.
 */
void fsyestify_put_group(struct fsyestify_group *group)
{
	if (refcount_dec_and_test(&group->refcnt))
		fsyestify_final_destroy_group(group);
}
EXPORT_SYMBOL_GPL(fsyestify_put_group);

/*
 * Create a new fsyestify_group and hold a reference for the group returned.
 */
struct fsyestify_group *fsyestify_alloc_group(const struct fsyestify_ops *ops)
{
	struct fsyestify_group *group;

	group = kzalloc(sizeof(struct fsyestify_group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	/* set to 0 when there a yes external references to this group */
	refcount_set(&group->refcnt, 1);
	atomic_set(&group->num_marks, 0);
	atomic_set(&group->user_waits, 0);

	spin_lock_init(&group->yestification_lock);
	INIT_LIST_HEAD(&group->yestification_list);
	init_waitqueue_head(&group->yestification_waitq);
	group->max_events = UINT_MAX;

	mutex_init(&group->mark_mutex);
	INIT_LIST_HEAD(&group->marks_list);

	group->ops = ops;

	return group;
}
EXPORT_SYMBOL_GPL(fsyestify_alloc_group);

int fsyestify_fasync(int fd, struct file *file, int on)
{
	struct fsyestify_group *group = file->private_data;

	return fasync_helper(fd, file, on, &group->fsn_fa) >= 0 ? 0 : -EIO;
}
