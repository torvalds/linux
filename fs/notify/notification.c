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

/*
 * Basic idea behind the notification queue: An fsnotify group (like inotify)
 * sends the userspace notification about events asynchronously some time after
 * the event happened.  When inotify gets an event it will need to add that
 * event to the group notify queue.  Since a single event might need to be on
 * multiple group's notification queues we can't add the event directly to each
 * queue and instead add a small "event_holder" to each queue.  This event_holder
 * has a pointer back to the original event.  Since the majority of events are
 * going to end up on one, and only one, notification queue we embed one
 * event_holder into each event.  This means we have a single allocation instead
 * of always needing two.  If the embedded event_holder is already in use by
 * another group a new event_holder (from fsnotify_event_holder_cachep) will be
 * allocated and used.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/atomic.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

static atomic_t fsnotify_sync_cookie = ATOMIC_INIT(0);

/**
 * fsnotify_get_cookie - return a unique cookie for use in synchronizing events.
 * Called from fsnotify_move, which is inlined into filesystem modules.
 */
u32 fsnotify_get_cookie(void)
{
	return atomic_inc_return(&fsnotify_sync_cookie);
}
EXPORT_SYMBOL_GPL(fsnotify_get_cookie);

/* return true if the notify queue is empty, false otherwise */
bool fsnotify_notify_queue_is_empty(struct fsnotify_group *group)
{
	BUG_ON(!mutex_is_locked(&group->notification_mutex));
	return list_empty(&group->notification_list) ? true : false;
}

void fsnotify_destroy_event(struct fsnotify_group *group,
			    struct fsnotify_event *event)
{
	/* Overflow events are per-group and we don't want to free them */
	if (!event || event->mask == FS_Q_OVERFLOW)
		return;

	group->ops->free_event(event);
}

/*
 * Add an event to the group notification queue.  The group can later pull this
 * event off the queue to deal with.  The function returns 0 if the event was
 * added to the queue, 1 if the event was merged with some other queued event,
 * 2 if the queue of events has overflown.
 */
int fsnotify_add_notify_event(struct fsnotify_group *group,
			      struct fsnotify_event *event,
			      int (*merge)(struct list_head *,
					   struct fsnotify_event *))
{
	int ret = 0;
	struct list_head *list = &group->notification_list;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	mutex_lock(&group->notification_mutex);

	if (group->q_len >= group->max_events) {
		ret = 2;
		/* Queue overflow event only if it isn't already queued */
		if (!list_empty(&group->overflow_event->list)) {
			mutex_unlock(&group->notification_mutex);
			return ret;
		}
		event = group->overflow_event;
		goto queue;
	}

	if (!list_empty(list) && merge) {
		ret = merge(list, event);
		if (ret) {
			mutex_unlock(&group->notification_mutex);
			return ret;
		}
	}

queue:
	group->q_len++;
	list_add_tail(&event->list, list);
	mutex_unlock(&group->notification_mutex);

	wake_up(&group->notification_waitq);
	kill_fasync(&group->fsn_fa, SIGIO, POLL_IN);
	return ret;
}

/*
 * Remove and return the first event from the notification list.  It is the
 * responsibility of the caller to destroy the obtained event
 */
struct fsnotify_event *fsnotify_remove_notify_event(struct fsnotify_group *group)
{
	struct fsnotify_event *event;

	BUG_ON(!mutex_is_locked(&group->notification_mutex));

	pr_debug("%s: group=%p\n", __func__, group);

	event = list_first_entry(&group->notification_list,
				 struct fsnotify_event, list);
	/*
	 * We need to init list head for the case of overflow event so that
	 * check in fsnotify_add_notify_events() works
	 */
	list_del_init(&event->list);
	group->q_len--;

	return event;
}

/*
 * This will not remove the event, that must be done with fsnotify_remove_notify_event()
 */
struct fsnotify_event *fsnotify_peek_notify_event(struct fsnotify_group *group)
{
	BUG_ON(!mutex_is_locked(&group->notification_mutex));

	return list_first_entry(&group->notification_list,
				struct fsnotify_event, list);
}

/*
 * Called when a group is being torn down to clean up any outstanding
 * event notifications.
 */
void fsnotify_flush_notify(struct fsnotify_group *group)
{
	struct fsnotify_event *event;

	mutex_lock(&group->notification_mutex);
	while (!fsnotify_notify_queue_is_empty(group)) {
		event = fsnotify_remove_notify_event(group);
		fsnotify_destroy_event(group, event);
	}
	mutex_unlock(&group->notification_mutex);
}

/*
 * fsnotify_create_event - Allocate a new event which will be sent to each
 * group's handle_event function if the group was interested in this
 * particular event.
 *
 * @inode the inode which is supposed to receive the event (sometimes a
 *	parent of the inode to which the event happened.
 * @mask what actually happened.
 * @data pointer to the object which was actually affected
 * @data_type flag indication if the data is a file, path, inode, nothing...
 * @name the filename, if available
 */
void fsnotify_init_event(struct fsnotify_event *event, struct inode *inode,
			 u32 mask)
{
	INIT_LIST_HEAD(&event->list);
	event->inode = inode;
	event->mask = mask;
}
