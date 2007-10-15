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

int delayacct_on __read_mostly = 1;	/* Delay accounting turned on/off */
struct kmem_cache *delayacct_cache;

static int __init delayacct_setup_disable(char *str)
{
	delayacct_on = 0;
	return 1;
}
__setup("nodelayacct", delayacct_setup_disable);

void delayacct_init(void)
{
	delayacct_cache = KMEM_CACHE(task_delay_info, SLAB_PANIC);
	delayacct_tsk_init(&init_task);
}

void __delayacct_tsk_init(struct task_struct *tsk)
{
	tsk->delays = kmem_cache_zalloc(delayacct_cache, GFP_KERNEL);
	if (tsk->delays)
		spin_lock_init(&tsk->delays->lock);
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
	unsigned long flags;

	do_posix_clock_monotonic_gettime(end);
	ts = timespec_sub(*end, *start);
	ns = timespec_to_ns(&ts);
	if (ns < 0)
		return;

	spin_lock_irqsave(&current->delays->lock, flags);
	*total += ns;
	(*count)++;
	spin_unlock_irqrestore(&current->delays->lock, flags);
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

int __delayacct_add_tsk(struct taskstats *d, struct task_struct *tsk)
{
	s64 tmp;
	unsigned long t1;
	unsigned long long t2, t3;
	unsigned long flags;
	struct timespec ts;

	/* Though tsk->delays accessed later, early exit avoids
	 * unnecessary returning of other data
	 */
	if (!tsk->delays)
		goto done;

	tmp = (s64)d->cpu_run_real_total;
	cputime_to_timespec(tsk->utime + tsk->stime, &ts);
	tmp += timespec_to_ns(&ts);
	d->cpu_run_real_total = (tmp < (s64)d->cpu_run_real_total) ? 0 : tmp;

	/*
	 * No locking available for sched_info (and too expensive to add one)
	 * Mitigate by taking snapshot of values
	 */
	t1 = tsk->sched_info.pcount;
	t2 = tsk->sched_info.run_delay;
	t3 = tsk->sched_info.cpu_time;

	d->cpu_count += t1;

	tmp = (s64)d->cpu_delay_total + t2;
	d->cpu_delay_total = (tmp < (s64)d->cpu_delay_total) ? 0 : tmp;

	tmp = (s64)d->cpu_run_virtual_total + t3;
	d->cpu_run_virtual_total =
		(tmp < (s64)d->cpu_run_virtual_total) ?	0 : tmp;

	/* zero XXX_total, non-zero XXX_count implies XXX stat overflowed */

	spin_lock_irqsave(&tsk->delays->lock, flags);
	tmp = d->blkio_delay_total + tsk->delays->blkio_delay;
	d->blkio_delay_total = (tmp < d->blkio_delay_total) ? 0 : tmp;
	tmp = d->swapin_delay_total + tsk->delays->swapin_delay;
	d->swapin_delay_total = (tmp < d->swapin_delay_total) ? 0 : tmp;
	d->blkio_count += tsk->delays->blkio_count;
	d->swapin_count += tsk->delays->swapin_count;
	spin_unlock_irqrestore(&tsk->delays->lock, flags);

done:
	return 0;
}

__u64 __delayacct_blkio_ticks(struct task_struct *tsk)
{
	__u64 ret;
	unsigned long flags;

	spin_lock_irqsave(&tsk->delays->lock, flags);
	ret = nsec_to_clock_t(tsk->delays->blkio_delay +
				tsk->delays->swapin_delay);
	spin_unlock_irqrestore(&tsk->delays->lock, flags);
	return ret;
}

