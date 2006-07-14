/* delayacct.c - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/sysctl.h>
#include <linux/delayacct.h>

int delayacct_on __read_mostly;	/* Delay accounting turned on/off */
kmem_cache_t *delayacct_cache;

static int __init delayacct_setup_enable(char *str)
{
	delayacct_on = 1;
	return 1;
}
__setup("delayacct", delayacct_setup_enable);

void delayacct_init(void)
{
	delayacct_cache = kmem_cache_create("delayacct_cache",
					sizeof(struct task_delay_info),
					0,
					SLAB_PANIC,
					NULL, NULL);
	delayacct_tsk_init(&init_task);
}

void __delayacct_tsk_init(struct task_struct *tsk)
{
	tsk->delays = kmem_cache_zalloc(delayacct_cache, SLAB_KERNEL);
	if (tsk->delays)
		spin_lock_init(&tsk->delays->lock);
}

void __delayacct_tsk_exit(struct task_struct *tsk)
{
	kmem_cache_free(delayacct_cache, tsk->delays);
	tsk->delays = NULL;
}

/*
 * Start accounting for a delay statistic using
 * its starting timestamp (@start)
 */

static inline void delayacct_start(struct timespec *start)
{
	do_posix_clock_monotonic_gettime(start);
}

/*
 * Finish delay accounting for a statistic using
 * its timestamps (@start, @end), accumalator (@total) and @count
 */

static void delayacct_end(struct timespec *start, struct timespec *end,
				u64 *total, u32 *count)
{
	struct timespec ts;
	s64 ns;

	do_posix_clock_monotonic_gettime(end);
	ts = timespec_sub(*end, *start);
	ns = timespec_to_ns(&ts);
	if (ns < 0)
		return;

	spin_lock(&current->delays->lock);
	*total += ns;
	(*count)++;
	spin_unlock(&current->delays->lock);
}

void __delayacct_blkio_start(void)
{
	delayacct_start(&current->delays->blkio_start);
}

void __delayacct_blkio_end(void)
{
	if (current->delays->flags & DELAYACCT_PF_SWAPIN)
		/* Swapin block I/O */
		delayacct_end(&current->delays->blkio_start,
			&current->delays->blkio_end,
			&current->delays->swapin_delay,
			&current->delays->swapin_count);
	else	/* Other block I/O */
		delayacct_end(&current->delays->blkio_start,
			&current->delays->blkio_end,
			&current->delays->blkio_delay,
			&current->delays->blkio_count);
}
