/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/rculist.h>
#include <linux/wait.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

#include <linux/atomic.h>

/*
 * Final freeing of a group
 */
void fsnotify_final_destroy_group(struct fsnotify_group *group)
{
	/* clear the notification queue of all events */
	fsnotify_flush_notify(group);

	if (group->ops->free_group_priv)
		group->ops->free_group_priv(group);

	kfree(group);
}

/*
 * Trying to get rid of a group.  We need to first get rid of any outstanding
 * allocations and then free the group.  Remember that fsnotify_clear_marks_by_group
 * could miss marks that are being freed by inode and those marks could still
 * hold a reference to this group (via group->num_marks)  If we get into that
 * situtation, the fsnotify_final_destroy_group will get called when that final
 * mark is freed.
 */
static void fsnotify_destroy_group(struct fsnotify_group *group)
{
	/* clear all inode marks for this group */
	fsnotify_clear_marks_by_group(group);

	synchronize_srcu(&fsnotify_mark_srcu);

	/* past the point of no return, matches the initial value of 1 */
	if (atomic_dec_and_test(&group->num_marks))
		fsnotify_final_destroy_group(group);
}

/*
 * Drop a reference to a group.  Free it if it's through.
 */
void fsnotify_put_group(struct fsnotify_group *group)
{
	if (atomic_dec_and_test(&group->refcnt))
		fsnotify_destroy_group(group);
}

/*
 * Create a new fsnotify_group and hold a reference for the group returned.
 */
struct fsnotify_group *fsnotify_alloc_group(const struct fsnotify_ops *ops)
{
	struct fsnotify_group *group;

	group = kzalloc(sizeof(struct fsnotify_group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	/* set to 0 when there a no external references to this group */
	atomic_set(&group->refcnt, 1);
	/*
	 * hits 0 when there are no external references AND no marks for
	 * this group
	 */
	atomic_set(&group->num_marks, 1);

	mutex_init(&group->notification_mutex);
	INIT_LIST_HEAD(&group->notification_list);
	init_waitqueue_head(&group->notification_waitq);
	group->max_events = UINT_MAX;

	spin_lock_init(&group->mark_lock);
	INIT_LIST_HEAD(&group->marks_list);

	group->ops = ops;

	return group;
}
