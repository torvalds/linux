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

#include <asm/atomic.h>

/* protects writes to fsnotify_groups and fsnotify_mask */
static DEFINE_MUTEX(fsnotify_grp_mutex);
/* protects reads while running the fsnotify_groups list */
struct srcu_struct fsnotify_grp_srcu;
/* all groups registered to receive filesystem notifications */
LIST_HEAD(fsnotify_inode_groups);
/* bitwise OR of all events (FS_*) interesting to some group on this system */
__u32 fsnotify_inode_mask;

/*
 * When a new group registers or changes it's set of interesting events
 * this function updates the fsnotify_mask to contain all interesting events
 */
void fsnotify_recalc_global_mask(void)
{
	struct fsnotify_group *group;
	__u32 mask = 0;
	int idx;

	idx = srcu_read_lock(&fsnotify_grp_srcu);
	list_for_each_entry_rcu(group, &fsnotify_inode_groups, inode_group_list)
		mask |= group->mask;
	srcu_read_unlock(&fsnotify_grp_srcu, idx);
	fsnotify_inode_mask = mask;
}

/*
 * Update the group->mask by running all of the marks associated with this
 * group and finding the bitwise | of all of the mark->mask.  If we change
 * the group->mask we need to update the global mask of events interesting
 * to the system.
 */
void fsnotify_recalc_group_mask(struct fsnotify_group *group)
{
	__u32 mask = 0;
	__u32 old_mask = group->mask;
	struct fsnotify_mark_entry *entry;

	spin_lock(&group->mark_lock);
	list_for_each_entry(entry, &group->mark_entries, g_list)
		mask |= entry->mask;
	spin_unlock(&group->mark_lock);

	group->mask = mask;

	if (old_mask != mask)
		fsnotify_recalc_global_mask();
}

static void fsnotify_add_group(struct fsnotify_group *group)
{
	BUG_ON(!mutex_is_locked(&fsnotify_grp_mutex));

	group->on_inode_group_list = 1;
	/* being on the fsnotify_groups list holds one num_marks */
	atomic_inc(&group->num_marks);

	list_add_tail_rcu(&group->inode_group_list, &fsnotify_inode_groups);
}

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
	/* clear all inode mark entries for this group */
	fsnotify_clear_marks_by_group(group);

	/* past the point of no return, matches the initial value of 1 */
	if (atomic_dec_and_test(&group->num_marks))
		fsnotify_final_destroy_group(group);
}

/*
 * Remove this group from the global list of groups that will get events
 * this can be done even if there are still references and things still using
 * this group.  This just stops the group from getting new events.
 */
static void __fsnotify_evict_group(struct fsnotify_group *group)
{
	BUG_ON(!mutex_is_locked(&fsnotify_grp_mutex));

	if (group->on_inode_group_list)
		list_del_rcu(&group->inode_group_list);
	group->on_inode_group_list = 0;
}

/*
 * Called when a group is no longer interested in getting events.  This can be
 * used if a group is misbehaving or if for some reason a group should no longer
 * get any filesystem events.
 */
void fsnotify_evict_group(struct fsnotify_group *group)
{
	mutex_lock(&fsnotify_grp_mutex);
	__fsnotify_evict_group(group);
	mutex_unlock(&fsnotify_grp_mutex);
}

/*
 * Drop a reference to a group.  Free it if it's through.
 */
void fsnotify_put_group(struct fsnotify_group *group)
{
	if (!atomic_dec_and_mutex_lock(&group->refcnt, &fsnotify_grp_mutex))
		return;

	/*
	 * OK, now we know that there's no other users *and* we hold mutex,
	 * so no new references will appear
	 */
	__fsnotify_evict_group(group);

	/*
	 * now it's off the list, so the only thing we might care about is
	 * srcu access....
	 */
	mutex_unlock(&fsnotify_grp_mutex);
	synchronize_srcu(&fsnotify_grp_srcu);

	/* and now it is really dead. _Nothing_ could be seeing it */
	fsnotify_recalc_global_mask();
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

	atomic_set(&group->refcnt, 1);

	mutex_init(&group->notification_mutex);
	INIT_LIST_HEAD(&group->notification_list);
	init_waitqueue_head(&group->notification_waitq);
	group->max_events = UINT_MAX;

	spin_lock_init(&group->mark_lock);
	INIT_LIST_HEAD(&group->mark_entries);

	group->ops = ops;

	mutex_lock(&fsnotify_grp_mutex);

	fsnotify_add_group(group);

	mutex_unlock(&fsnotify_grp_mutex);

	return group;
}
