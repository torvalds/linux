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

#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/srcu.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

/*
 * This is the main call to fsnotify.  The VFS calls into hook specific functions
 * in linux/fsnotify.h.  Those functions then in turn call here.  Here will call
 * out to all of the registered fsnotify_group.  Those groups can then use the
 * notification event in whatever means they feel necessary.
 */
void fsnotify(struct inode *to_tell, __u32 mask, void *data, int data_is)
{
	struct fsnotify_group *group;
	struct fsnotify_event *event = NULL;
	int idx;

	if (list_empty(&fsnotify_groups))
		return;

	if (!(mask & fsnotify_mask))
		return;

	/*
	 * SRCU!!  the groups list is very very much read only and the path is
	 * very hot.  The VAST majority of events are not going to need to do
	 * anything other than walk the list so it's crazy to pre-allocate.
	 */
	idx = srcu_read_lock(&fsnotify_grp_srcu);
	list_for_each_entry_rcu(group, &fsnotify_groups, group_list) {
		if (mask & group->mask) {
			if (!event) {
				event = fsnotify_create_event(to_tell, mask, data, data_is);
				/* shit, we OOM'd and now we can't tell, maybe
				 * someday someone else will want to do something
				 * here */
				if (!event)
					break;
			}
			group->ops->handle_event(group, event);
		}
	}
	srcu_read_unlock(&fsnotify_grp_srcu, idx);
	/*
	 * fsnotify_create_event() took a reference so the event can't be cleaned
	 * up while we are still trying to add it to lists, drop that one.
	 */
	if (event)
		fsnotify_put_event(event);
}
EXPORT_SYMBOL_GPL(fsnotify);

static __init int fsnotify_init(void)
{
	return init_srcu_struct(&fsnotify_grp_srcu);
}
subsys_initcall(fsnotify_init);
