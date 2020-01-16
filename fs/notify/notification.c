// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

/*
 * Basic idea behind the yestification queue: An fsyestify group (like iyestify)
 * sends the userspace yestification about events asynchroyesusly some time after
 * the event happened.  When iyestify gets an event it will need to add that
 * event to the group yestify queue.  Since a single event might need to be on
 * multiple group's yestification queues we can't add the event directly to each
 * queue and instead add a small "event_holder" to each queue.  This event_holder
 * has a pointer back to the original event.  Since the majority of events are
 * going to end up on one, and only one, yestification queue we embed one
 * event_holder into each event.  This means we have a single allocation instead
 * of always needing two.  If the embedded event_holder is already in use by
 * ayesther group a new event_holder (from fsyestify_event_holder_cachep) will be
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

#include <linux/fsyestify_backend.h>
#include "fsyestify.h"

static atomic_t fsyestify_sync_cookie = ATOMIC_INIT(0);

/**
 * fsyestify_get_cookie - return a unique cookie for use in synchronizing events.
 * Called from fsyestify_move, which is inlined into filesystem modules.
 */
u32 fsyestify_get_cookie(void)
{
	return atomic_inc_return(&fsyestify_sync_cookie);
}
EXPORT_SYMBOL_GPL(fsyestify_get_cookie);

/* return true if the yestify queue is empty, false otherwise */
bool fsyestify_yestify_queue_is_empty(struct fsyestify_group *group)
{
	assert_spin_locked(&group->yestification_lock);
	return list_empty(&group->yestification_list) ? true : false;
}

void fsyestify_destroy_event(struct fsyestify_group *group,
			    struct fsyestify_event *event)
{
	/* Overflow events are per-group and we don't want to free them */
	if (!event || event == group->overflow_event)
		return;
	/*
	 * If the event is still queued, we have a problem... Do an unreliable
	 * lockless check first to avoid locking in the common case. The
	 * locking may be necessary for permission events which got removed
	 * from the list by a different CPU than the one freeing the event.
	 */
	if (!list_empty(&event->list)) {
		spin_lock(&group->yestification_lock);
		WARN_ON(!list_empty(&event->list));
		spin_unlock(&group->yestification_lock);
	}
	group->ops->free_event(event);
}

/*
 * Add an event to the group yestification queue.  The group can later pull this
 * event off the queue to deal with.  The function returns 0 if the event was
 * added to the queue, 1 if the event was merged with some other queued event,
 * 2 if the event was yest queued - either the queue of events has overflown
 * or the group is shutting down.
 */
int fsyestify_add_event(struct fsyestify_group *group,
		       struct fsyestify_event *event,
		       int (*merge)(struct list_head *,
				    struct fsyestify_event *))
{
	int ret = 0;
	struct list_head *list = &group->yestification_list;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	spin_lock(&group->yestification_lock);

	if (group->shutdown) {
		spin_unlock(&group->yestification_lock);
		return 2;
	}

	if (event == group->overflow_event ||
	    group->q_len >= group->max_events) {
		ret = 2;
		/* Queue overflow event only if it isn't already queued */
		if (!list_empty(&group->overflow_event->list)) {
			spin_unlock(&group->yestification_lock);
			return ret;
		}
		event = group->overflow_event;
		goto queue;
	}

	if (!list_empty(list) && merge) {
		ret = merge(list, event);
		if (ret) {
			spin_unlock(&group->yestification_lock);
			return ret;
		}
	}

queue:
	group->q_len++;
	list_add_tail(&event->list, list);
	spin_unlock(&group->yestification_lock);

	wake_up(&group->yestification_waitq);
	kill_fasync(&group->fsn_fa, SIGIO, POLL_IN);
	return ret;
}

void fsyestify_remove_queued_event(struct fsyestify_group *group,
				  struct fsyestify_event *event)
{
	assert_spin_locked(&group->yestification_lock);
	/*
	 * We need to init list head for the case of overflow event so that
	 * check in fsyestify_add_event() works
	 */
	list_del_init(&event->list);
	group->q_len--;
}

/*
 * Remove and return the first event from the yestification list.  It is the
 * responsibility of the caller to destroy the obtained event
 */
struct fsyestify_event *fsyestify_remove_first_event(struct fsyestify_group *group)
{
	struct fsyestify_event *event;

	assert_spin_locked(&group->yestification_lock);

	pr_debug("%s: group=%p\n", __func__, group);

	event = list_first_entry(&group->yestification_list,
				 struct fsyestify_event, list);
	fsyestify_remove_queued_event(group, event);
	return event;
}

/*
 * This will yest remove the event, that must be done with
 * fsyestify_remove_first_event()
 */
struct fsyestify_event *fsyestify_peek_first_event(struct fsyestify_group *group)
{
	assert_spin_locked(&group->yestification_lock);

	return list_first_entry(&group->yestification_list,
				struct fsyestify_event, list);
}

/*
 * Called when a group is being torn down to clean up any outstanding
 * event yestifications.
 */
void fsyestify_flush_yestify(struct fsyestify_group *group)
{
	struct fsyestify_event *event;

	spin_lock(&group->yestification_lock);
	while (!fsyestify_yestify_queue_is_empty(group)) {
		event = fsyestify_remove_first_event(group);
		spin_unlock(&group->yestification_lock);
		fsyestify_destroy_event(group, event);
		spin_lock(&group->yestification_lock);
	}
	spin_unlock(&group->yestification_lock);
}
