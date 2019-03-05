/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common header file for generic dynamic events.
 */

#ifndef _TRACE_DYNEVENT_H
#define _TRACE_DYNEVENT_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>

#include "trace.h"

struct dyn_event;

/**
 * struct dyn_event_operations - Methods for each type of dynamic events
 *
 * These methods must be set for each type, since there is no default method.
 * Before using this for dyn_event_init(), it must be registered by
 * dyn_event_register().
 *
 * @create: Parse and create event method. This is invoked when user passes
 *  a event definition to dynamic_events interface. This must not destruct
 *  the arguments and return -ECANCELED if given arguments doesn't match its
 *  command prefix.
 * @show: Showing method. This is invoked when user reads the event definitions
 *  via dynamic_events interface.
 * @is_busy: Check whether given event is busy so that it can not be deleted.
 *  Return true if it is busy, otherwides false.
 * @free: Delete the given event. Return 0 if success, otherwides error.
 * @match: Check whether given event and system name match this event.
 *  Return true if it matches, otherwides false.
 *
 * Except for @create, these methods are called under holding event_mutex.
 */
struct dyn_event_operations {
	struct list_head	list;
	int (*create)(int argc, const char *argv[]);
	int (*show)(struct seq_file *m, struct dyn_event *ev);
	bool (*is_busy)(struct dyn_event *ev);
	int (*free)(struct dyn_event *ev);
	bool (*match)(const char *system, const char *event,
			struct dyn_event *ev);
};

/* Register new dyn_event type -- must be called at first */
int dyn_event_register(struct dyn_event_operations *ops);

/**
 * struct dyn_event - Dynamic event list header
 *
 * The dyn_event structure encapsulates a list and a pointer to the operators
 * for making a global list of dynamic events.
 * User must includes this in each event structure, so that those events can
 * be added/removed via dynamic_events interface.
 */
struct dyn_event {
	struct list_head		list;
	struct dyn_event_operations	*ops;
};

extern struct list_head dyn_event_list;

static inline
int dyn_event_init(struct dyn_event *ev, struct dyn_event_operations *ops)
{
	if (!ev || !ops)
		return -EINVAL;

	INIT_LIST_HEAD(&ev->list);
	ev->ops = ops;
	return 0;
}

static inline int dyn_event_add(struct dyn_event *ev)
{
	lockdep_assert_held(&event_mutex);

	if (!ev || !ev->ops)
		return -EINVAL;

	list_add_tail(&ev->list, &dyn_event_list);
	return 0;
}

static inline void dyn_event_remove(struct dyn_event *ev)
{
	lockdep_assert_held(&event_mutex);
	list_del_init(&ev->list);
}

void *dyn_event_seq_start(struct seq_file *m, loff_t *pos);
void *dyn_event_seq_next(struct seq_file *m, void *v, loff_t *pos);
void dyn_event_seq_stop(struct seq_file *m, void *v);
int dyn_events_release_all(struct dyn_event_operations *type);
int dyn_event_release(int argc, char **argv, struct dyn_event_operations *type);

/*
 * for_each_dyn_event	-	iterate over the dyn_event list
 * @pos:	the struct dyn_event * to use as a loop cursor
 *
 * This is just a basement of for_each macro. Wrap this for
 * each actual event structure with ops filtering.
 */
#define for_each_dyn_event(pos)	\
	list_for_each_entry(pos, &dyn_event_list, list)

/*
 * for_each_dyn_event	-	iterate over the dyn_event list safely
 * @pos:	the struct dyn_event * to use as a loop cursor
 * @n:		the struct dyn_event * to use as temporary storage
 */
#define for_each_dyn_event_safe(pos, n)	\
	list_for_each_entry_safe(pos, n, &dyn_event_list, list)

#endif
