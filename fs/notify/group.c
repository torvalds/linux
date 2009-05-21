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
LIST_HEAD(fsnotify_groups);
/* bitwise OR of all events (FS_*) interesting to some group on this system */
__u32 fsnotify_mask;

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
	list_for_each_entry_rcu(group, &fsnotify_groups, group_list)
		mask |= group->mask;
	srcu_read_unlock(&fsnotify_grp_srcu, idx);
	fsnotify_mask = mask;
}

/*
 * Take a reference to a group so things found under the fsnotify_grp_mutex
 * can't get freed under us
 */
static void fsnotify_get_group(struct fsnotify_group *group)
{
	atomic_inc(&group->refcnt);
}

/*
 * Final freeing of a group
 */
static void fsnotify_destroy_group(struct fsnotify_group *group)
{
	if (group->ops->free_group_priv)
		group->ops->free_group_priv(group);

	kfree(group);
}

/*
 * Remove this group from the global list of groups that will get events
 * this can be done even if there are still references and things still using
 * this group.  This just stops the group from getting new events.
 */
static void __fsnotify_evict_group(struct fsnotify_group *group)
{
	BUG_ON(!mutex_is_locked(&fsnotify_grp_mutex));

	if (group->on_group_list)
		list_del_rcu(&group->group_list);
	group->on_group_list = 0;
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
 * Simply run the fsnotify_groups list and find a group which matches
 * the given parameters.  If a group is found we take a reference to that
 * group.
 */
static struct fsnotify_group *fsnotify_find_group(unsigned int group_num, __u32 mask,
						  const struct fsnotify_ops *ops)
{
	struct fsnotify_group *group_iter;
	struct fsnotify_group *group = NULL;

	BUG_ON(!mutex_is_locked(&fsnotify_grp_mutex));

	list_for_each_entry_rcu(group_iter, &fsnotify_groups, group_list) {
		if (group_iter->group_num == group_num) {
			if ((group_iter->mask == mask) &&
			    (group_iter->ops == ops)) {
				fsnotify_get_group(group_iter);
				group = group_iter;
			} else
				group = ERR_PTR(-EEXIST);
		}
	}
	return group;
}

/*
 * Either finds an existing group which matches the group_num, mask, and ops or
 * creates a new group and adds it to the global group list.  In either case we
 * take a reference for the group returned.
 */
struct fsnotify_group *fsnotify_obtain_group(unsigned int group_num, __u32 mask,
					     const struct fsnotify_ops *ops)
{
	struct fsnotify_group *group, *tgroup;

	/* very low use, simpler locking if we just always alloc */
	group = kmalloc(sizeof(struct fsnotify_group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);

	atomic_set(&group->refcnt, 1);

	group->on_group_list = 0;
	group->group_num = group_num;
	group->mask = mask;

	group->ops = ops;

	mutex_lock(&fsnotify_grp_mutex);
	tgroup = fsnotify_find_group(group_num, mask, ops);
	if (tgroup) {
		/* group already exists */
		mutex_unlock(&fsnotify_grp_mutex);
		/* destroy the new one we made */
		fsnotify_put_group(group);
		return tgroup;
	}

	/* group not found, add a new one */
	list_add_rcu(&group->group_list, &fsnotify_groups);
	group->on_group_list = 1;

	mutex_unlock(&fsnotify_grp_mutex);

	if (mask)
		fsnotify_recalc_global_mask();

	return group;
}
