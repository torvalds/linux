/* Worker thread pool for slow items, such as filesystem lookups or mkdirs
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 *
 * See Documentation/slow-work.txt
 */

#ifndef _LINUX_SLOW_WORK_H
#define _LINUX_SLOW_WORK_H

#ifdef CONFIG_SLOW_WORK

#include <linux/sysctl.h>

struct slow_work;

/*
 * The operations used to support slow work items
 */
struct slow_work_ops {
	/* get a ref on a work item
	 * - return 0 if successful, -ve if not
	 */
	int (*get_ref)(struct slow_work *work);

	/* discard a ref to a work item */
	void (*put_ref)(struct slow_work *work);

	/* execute a work item */
	void (*execute)(struct slow_work *work);
};

/*
 * A slow work item
 * - A reference is held on the parent object by the thread pool when it is
 *   queued
 */
struct slow_work {
	unsigned long		flags;
#define SLOW_WORK_PENDING	0	/* item pending (further) execution */
#define SLOW_WORK_EXECUTING	1	/* item currently executing */
#define SLOW_WORK_ENQ_DEFERRED	2	/* item enqueue deferred */
#define SLOW_WORK_VERY_SLOW	3	/* item is very slow */
	const struct slow_work_ops *ops; /* operations table for this item */
	struct list_head	link;	/* link in queue */
};

/**
 * slow_work_init - Initialise a slow work item
 * @work: The work item to initialise
 * @ops: The operations to use to handle the slow work item
 *
 * Initialise a slow work item.
 */
static inline void slow_work_init(struct slow_work *work,
				  const struct slow_work_ops *ops)
{
	work->flags = 0;
	work->ops = ops;
	INIT_LIST_HEAD(&work->link);
}

/**
 * slow_work_init - Initialise a very slow work item
 * @work: The work item to initialise
 * @ops: The operations to use to handle the slow work item
 *
 * Initialise a very slow work item.  This item will be restricted such that
 * only a certain number of the pool threads will be able to execute items of
 * this type.
 */
static inline void vslow_work_init(struct slow_work *work,
				   const struct slow_work_ops *ops)
{
	work->flags = 1 << SLOW_WORK_VERY_SLOW;
	work->ops = ops;
	INIT_LIST_HEAD(&work->link);
}

extern int slow_work_enqueue(struct slow_work *work);
extern int slow_work_register_user(void);
extern void slow_work_unregister_user(void);

#ifdef CONFIG_SYSCTL
extern ctl_table slow_work_sysctls[];
#endif

#endif /* CONFIG_SLOW_WORK */
#endif /* _LINUX_SLOW_WORK_H */
