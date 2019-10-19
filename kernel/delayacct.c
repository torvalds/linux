// SPDX-License-Identifier: GPL-2.0-or-later
/* delayacct.c - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 */

#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/cputime.h>
#include <linux/slab.h>
#include <linux/taskstats.h>
#include <linux/time.h>
#include <linux/sysctl.h>
#include <linux/delayacct.h>
#include <linux/module.h>

int delayacct_on __read_mostly = 1;	/* Delay accounting turned on/off */
EXPORT_SYMBOL_GPL(delayacct_on);
struct kmem_cache *delayacct_cache;

static int __init delayacct_setup_disable(char *str)
{
	delayacct_on = 0;
	return 1;
}
__setup("nodelayacct", delayacct_setup_disable);

void delayacct_init(void)
{
	delayacct_cache = KMEM_CACHE(task_delay_info, SLAB_PANIC|SLAB_ACCOUNT);
	delayacct_tsk_init(&init_task);
}

void __delayacct_tsk_init(struct task_struct *tsk)
{
	tsk->delays = kmem_cache_zalloc(delayacct_cache, GFP_KERNEL);
	if (tsk->delays)
		raw_spin_lock_init(&tsk->delays->lock);
}

/*
 * Finish delay accounting for a statistic using its timestamps (@start),
 * accumalator (@total) and @count
 */
static void delayacct_end(raw_spinlock_t *lock, u64 *start, u64 *total,
			  u32 *count)
{
	s64 ns = ktime_get_ns() - *start;
	unsigned long flags;

	if (ns > 0) {
		raw_spin_lock_irqsave(lock, flags);
		*total += ns;
		(*count)++;
		raw_spin_unlock_irqrestore(lock, flags);
	}
}

void __delayacct_blkio_start(void)
{
	current->delays->blkio_start = ktime_get_ns();
}

/*
 * We cannot rely on the `current` macro, as we haven't yet switched back to
 * the process being woken.
 */
void __delayacct_blkio_end(struct task_struct *p)
{
	struct task_delay_info *delays = p->delays;
	u64 *total;
	u32 *count;

	if (p->delays->flags & DELAYACCT_PF_SWAPIN) {
		total = &delays->swapin_delay;
		count = &delays->swapin_count;
	} else {
		total = &delays->blkio_delay;
		count = &delays->blkio_count;
	}

	delayacct_end(&delays->lock, &delays->blkio_start, total, count);
}

int __delayacct_add_tsk(struct taskstats *d, struct task_struct *tsk)
{
	u64 utime, stime, stimescaled, utimescaled;
	unsigned long long t2, t3;
	unsigned long flags, t1;
	s64 tmp;

	task_cputime(tsk, &utime, &stime);
	tmp = (s64)d->cpu_run_real_total;
	tmp += utime + stime;
	d->cpu_run_real_total = (tmp < (s64)d->cpu_run_real_total) ? 0 : tmp;

	task_cputime_scaled(tsk, &utimescaled, &stimescaled);
	tmp = (s64)d->cpu_scaled_run_real_total;
	tmp += utimescaled + stimescaled;
	d->cpu_scaled_run_real_total =
		(tmp < (s64)d->cpu_scaled_run_real_total) ? 0 : tmp;

	/*
	 * No locking available for sched_info (and too expensive to add one)
	 * Mitigate by taking snapshot of values
	 */
	t1 = tsk->sched_info.pcount;
	t2 = tsk->sched_info.run_delay;
	t3 = tsk->se.sum_exec_runtime;

	d->cpu_count += t1;

	tmp = (s64)d->cpu_delay_total + t2;
	d->cpu_delay_total = (tmp < (s64)d->cpu_delay_total) ? 0 : tmp;

	tmp = (s64)d->cpu_run_virtual_total + t3;
	d->cpu_run_virtual_total =
		(tmp < (s64)d->cpu_run_virtual_total) ?	0 : tmp;

	/* zero XXX_total, non-zero XXX_count implies XXX stat overflowed */

	raw_spin_lock_irqsave(&tsk->delays->lock, flags);
	tmp = d->blkio_delay_total + tsk->delays->blkio_delay;
	d->blkio_delay_total = (tmp < d->blkio_delay_total) ? 0 : tmp;
	tmp = d->swapin_delay_total + tsk->delays->swapin_delay;
	d->swapin_delay_total = (tmp < d->swapin_delay_total) ? 0 : tmp;
	tmp = d->freepages_delay_total + tsk->delays->freepages_delay;
	d->freepages_delay_total = (tmp < d->freepages_delay_total) ? 0 : tmp;
	tmp = d->thrashing_delay_total + tsk->delays->thrashing_delay;
	d->thrashing_delay_total = (tmp < d->thrashing_delay_total) ? 0 : tmp;
	d->blkio_count += tsk->delays->blkio_count;
	d->swapin_count += tsk->delays->swapin_count;
	d->freepages_count += tsk->delays->freepages_count;
	d->thrashing_count += tsk->delays->thrashing_count;
	raw_spin_unlock_irqrestore(&tsk->delays->lock, flags);

	return 0;
}

__u64 __delayacct_blkio_ticks(struct task_struct *tsk)
{
	__u64 ret;
	unsigned long flags;

	raw_spin_lock_irqsave(&tsk->delays->lock, flags);
	ret = nsec_to_clock_t(tsk->delays->blkio_delay +
				tsk->delays->swapin_delay);
	raw_spin_unlock_irqrestore(&tsk->delays->lock, flags);
	return ret;
}

void __delayacct_freepages_start(void)
{
	current->delays->freepages_start = ktime_get_ns();
}

void __delayacct_freepages_end(void)
{
	delayacct_end(
		&current->delays->lock,
		&current->delays->freepages_start,
		&current->delays->freepages_delay,
		&current->delays->freepages_count);
}

void __delayacct_thrashing_start(void)
{
	current->delays->thrashing_start = ktime_get_ns();
}

void __delayacct_thrashing_end(void)
{
	delayacct_end(&current->delays->lock,
		      &current->delays->thrashing_start,
		      &current->delays->thrashing_delay,
		      &current->delays->thrashing_count);
}
