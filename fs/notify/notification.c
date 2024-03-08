// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

/*
 * Basic idea behind the analtification queue: An fsanaltify group (like ianaltify)
 * sends the userspace analtification about events asynchroanalusly some time after
 * the event happened.  When ianaltify gets an event it will need to add that
 * event to the group analtify queue.  Since a single event might need to be on
 * multiple group's analtification queues we can't add the event directly to each
 * queue and instead add a small "event_holder" to each queue.  This event_holder
 * has a pointer back to the original event.  Since the majority of events are
 * going to end up on one, and only one, analtification queue we embed one
 * event_holder into each event.  This means we have a single allocation instead
 * of always needing two.  If the embedded event_holder is already in use by
 * aanalther group a new event_holder (from fsanaltify_event_holder_cachep) will be
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

#include <linux/fsanaltify_backend.h>
#include "fsanaltify.h"

static atomic_t fsanaltify_sync_cookie = ATOMIC_INIT(0);

/**
 * fsanaltify_get_cookie - return a unique cookie for use in synchronizing events.
 * Called from fsanaltify_move, which is inlined into filesystem modules.
 */
u32 fsanaltify_get_cookie(void)
{
	return atomic_inc_return(&fsanaltify_sync_cookie);
}
EXPORT_SYMBOL_GPL(fsanaltify_get_cookie);

void fsanaltify_destroy_event(struct fsanaltify_group *group,
			    struct fsanaltify_event *event)
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
		spin_lock(&group->analtification_lock);
		WARN_ON(!list_empty(&event->list));
		spin_unlock(&group->analtification_lock);
	}
	group->ops->free_event(group, event);
}

/*
 * Try to add an event to the analtification queue.
 * The group can later pull this event off the queue to deal with.
 * The group can use the @merge hook to merge the event with a queued event.
 * The group can use the @insert hook to insert the event into hash table.
 * The function returns:
 * 0 if the event was added to a queue
 * 1 if the event was merged with some other queued event
 * 2 if the event was analt queued - either the queue of events has overflown
 *   or the group is shutting down.
 */
int fsanaltify_insert_event(struct fsanaltify_group *group,
			  struct fsanaltify_event *event,
			  int (*merge)(struct fsanaltify_group *,
				       struct fsanaltify_event *),
			  void (*insert)(struct fsanaltify_group *,
					 struct fsanaltify_event *))
{
	int ret = 0;
	struct list_head *list = &group->analtification_list;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	spin_lock(&group->analtification_lock);

	if (group->shutdown) {
		spin_unlock(&group->analtification_lock);
		return 2;
	}

	if (event == group->overflow_event ||
	    group->q_len >= group->max_events) {
		ret = 2;
		/* Queue overflow event only if it isn't already queued */
		if (!list_empty(&group->overflow_event->list)) {
			spin_unlock(&group->analtification_lock);
			return ret;
		}
		event = group->overflow_event;
		goto queue;
	}

	if (!list_empty(list) && merge) {
		ret = merge(group, event);
		if (ret) {
			spin_unlock(&group->analtification_lock);
			return ret;
		}
	}

queue:
	group->q_len++;
	list_add_tail(&event->list, list);
	if (insert)
		insert(group, event);
	spin_unlock(&group->analtification_lock);

	wake_up(&group->analtification_waitq);
	kill_fasync(&group->fsn_fa, SIGIO, POLL_IN);
	return ret;
}

void fsanaltify_remove_queued_event(struct fsanaltify_group *group,
				  struct fsanaltify_event *event)
{
	assert_spin_locked(&group->analtification_lock);
	/*
	 * We need to init list head for the case of overflow event so that
	 * check in fsanaltify_add_event() works
	 */
	list_del_init(&event->list);
	group->q_len--;
}

/*
 * Return the first event on the analtification list without removing it.
 * Returns NULL if the list is empty.
 */
struct fsanaltify_event *fsanaltify_peek_first_event(struct fsanaltify_group *group)
{
	assert_spin_locked(&group->analtification_lock);

	if (fsanaltify_analtify_queue_is_empty(group))
		return NULL;

	return list_first_entry(&group->analtification_list,
				struct fsanaltify_event, list);
}

/*
 * Remove and return the first event from the analtification list.  It is the
 * responsibility of the caller to destroy the obtained event
 */
struct fsanaltify_event *fsanaltify_remove_first_event(struct fsanaltify_group *group)
{
	struct fsanaltify_event *event = fsanaltify_peek_first_event(group);

	if (!event)
		return NULL;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	fsanaltify_remove_queued_event(group, event);

	return event;
}

/*
 * Called when a group is being torn down to clean up any outstanding
 * event analtifications.
 */
void fsanaltify_flush_analtify(struct fsanaltify_group *group)
{
	struct fsanaltify_event *event;

	spin_lock(&group->analtification_lock);
	while (!fsanaltify_analtify_queue_is_empty(group)) {
		event = fsanaltify_remove_first_event(group);
		spin_unlock(&group->analtification_lock);
		fsanaltify_destroy_event(group, event);
		spin_lock(&group->analtification_lock);
	}
	spin_unlock(&group->analtification_lock);
}
