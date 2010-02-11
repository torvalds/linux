/* Slow work private definitions
 *
 * Copyright (C) 2009 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define SLOW_WORK_CULL_TIMEOUT (5 * HZ)	/* cull threads 5s after running out of
					 * things to do */
#define SLOW_WORK_OOM_TIMEOUT (5 * HZ)	/* can't start new threads for 5s after
					 * OOM */

#define SLOW_WORK_THREAD_LIMIT	255	/* abs maximum number of slow-work threads */

/*
 * slow-work.c
 */
#ifdef CONFIG_SLOW_WORK_DEBUG
extern struct slow_work *slow_work_execs[];
extern pid_t slow_work_pids[];
extern rwlock_t slow_work_execs_lock;
#endif

extern struct list_head slow_work_queue;
extern struct list_head vslow_work_queue;
extern spinlock_t slow_work_queue_lock;

/*
 * slow-work-debugfs.c
 */
#ifdef CONFIG_SLOW_WORK_DEBUG
extern const struct file_operations slow_work_runqueue_fops;

extern void slow_work_new_thread_desc(struct slow_work *, struct seq_file *);
#endif

/*
 * Helper functions
 */
static inline void slow_work_set_thread_pid(int id, pid_t pid)
{
#ifdef CONFIG_SLOW_WORK_PROC
	slow_work_pids[id] = pid;
#endif
}

static inline void slow_work_mark_time(struct slow_work *work)
{
#ifdef CONFIG_SLOW_WORK_PROC
	work->mark = CURRENT_TIME;
#endif
}

static inline void slow_work_begin_exec(int id, struct slow_work *work)
{
#ifdef CONFIG_SLOW_WORK_PROC
	slow_work_execs[id] = work;
#endif
}

static inline void slow_work_end_exec(int id, struct slow_work *work)
{
#ifdef CONFIG_SLOW_WORK_PROC
	write_lock(&slow_work_execs_lock);
	slow_work_execs[id] = NULL;
	write_unlock(&slow_work_execs_lock);
#endif
}
