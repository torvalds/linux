/*
 * latencytop.c: Latency display infrastructure
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

/*
 * CONFIG_LATENCYTOP enables a kernel latency tracking infrastructure that is
 * used by the "latencytop" userspace tool. The latency that is tracked is not
 * the 'traditional' interrupt latency (which is primarily caused by something
 * else consuming CPU), but instead, it is the latency an application encounters
 * because the kernel sleeps on its behalf for various reasons.
 *
 * This code tracks 2 levels of statistics:
 * 1) System level latency
 * 2) Per process latency
 *
 * The latency is stored in fixed sized data structures in an accumulated form;
 * if the "same" latency cause is hit twice, this will be tracked as one entry
 * in the data structure. Both the count, total accumulated latency and maximum
 * latency are tracked in this data structure. When the fixed size structure is
 * full, no new causes are tracked until the buffer is flushed by writing to
 * the /proc file; the userspace tool does this on a regular basis.
 *
 * A latency cause is identified by a stringified backtrace at the point that
 * the scheduler gets invoked. The userland tool will use this string to
 * identify the cause of the latency in human readable form.
 *
 * The information is exported via /proc/latency_stats and /proc/<pid>/latency.
 * These files look like this:
 *
 * Latency Top version : v0.1
 * 70 59433 4897 i915_irq_wait drm_ioctl vfs_ioctl do_vfs_ioctl sys_ioctl
 * |    |    |    |
 * |    |    |    +----> the stringified backtrace
 * |    |    +---------> The maximum latency for this entry in microseconds
 * |    +--------------> The accumulated latency for this entry (microseconds)
 * +-------------------> The number of times this entry is hit
 *
 * (note: the average latency is the accumulated latency divided by the number
 * of times)
 */

#include <linux/latencytop.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/stacktrace.h>

static DEFINE_SPINLOCK(latency_lock);

#define MAXLR 128
static struct latency_record latency_record[MAXLR];

int latencytop_enabled;

void clear_all_latency_tracing(struct task_struct *p)
{
	unsigned long flags;

	if (!latencytop_enabled)
		return;

	spin_lock_irqsave(&latency_lock, flags);
	memset(&p->latency_record, 0, sizeof(p->latency_record));
	p->latency_record_count = 0;
	spin_unlock_irqrestore(&latency_lock, flags);
}

static void clear_global_latency_tracing(void)
{
	unsigned long flags;

	spin_lock_irqsave(&latency_lock, flags);
	memset(&latency_record, 0, sizeof(latency_record));
	spin_unlock_irqrestore(&latency_lock, flags);
}

static void __sched
account_global_scheduler_latency(struct task_struct *tsk, struct latency_record *lat)
{
	int firstnonnull = MAXLR + 1;
	int i;

	if (!latencytop_enabled)
		return;

	/* skip kernel threads for now */
	if (!tsk->mm)
		return;

	for (i = 0; i < MAXLR; i++) {
		int q, same = 1;

		/* Nothing stored: */
		if (!latency_record[i].backtrace[0]) {
			if (firstnonnull > i)
				firstnonnull = i;
			continue;
		}
		for (q = 0; q < LT_BACKTRACEDEPTH; q++) {
			unsigned long record = lat->backtrace[q];

			if (latency_record[i].backtrace[q] != record) {
				same = 0;
				break;
			}

			/* 0 and ULONG_MAX entries mean end of backtrace: */
			if (record == 0 || record == ULONG_MAX)
				break;
		}
		if (same) {
			latency_record[i].count++;
			latency_record[i].time += lat->time;
			if (lat->time > latency_record[i].max)
				latency_record[i].max = lat->time;
			return;
		}
	}

	i = firstnonnull;
	if (i >= MAXLR - 1)
		return;

	/* Allocted a new one: */
	memcpy(&latency_record[i], lat, sizeof(struct latency_record));
}

/*
 * Iterator to store a backtrace into a latency record entry
 */
static inline void store_stacktrace(struct task_struct *tsk,
					struct latency_record *lat)
{
	struct stack_trace trace;

	memset(&trace, 0, sizeof(trace));
	trace.max_entries = LT_BACKTRACEDEPTH;
	trace.entries = &lat->backtrace[0];
	save_stack_trace_tsk(tsk, &trace);
}

/**
 * __account_scheduler_latency - record an occured latency
 * @tsk - the task struct of the task hitting the latency
 * @usecs - the duration of the latency in microseconds
 * @inter - 1 if the sleep was interruptible, 0 if uninterruptible
 *
 * This function is the main entry point for recording latency entries
 * as called by the scheduler.
 *
 * This function has a few special cases to deal with normal 'non-latency'
 * sleeps: specifically, interruptible sleep longer than 5 msec is skipped
 * since this usually is caused by waiting for events via select() and co.
 *
 * Negative latencies (caused by time going backwards) are also explicitly
 * skipped.
 */
void __sched
__account_scheduler_latency(struct task_struct *tsk, int usecs, int inter)
{
	unsigned long flags;
	int i, q;
	struct latency_record lat;

	/* Long interruptible waits are generally user requested... */
	if (inter && usecs > 5000)
		return;

	/* Negative sleeps are time going backwards */
	/* Zero-time sleeps are non-interesting */
	if (usecs <= 0)
		return;

	memset(&lat, 0, sizeof(lat));
	lat.count = 1;
	lat.time = usecs;
	lat.max = usecs;
	store_stacktrace(tsk, &lat);

	spin_lock_irqsave(&latency_lock, flags);

	account_global_scheduler_latency(tsk, &lat);

	for (i = 0; i < tsk->latency_record_count; i++) {
		struct latency_record *mylat;
		int same = 1;

		mylat = &tsk->latency_record[i];
		for (q = 0; q < LT_BACKTRACEDEPTH; q++) {
			unsigned long record = lat.backtrace[q];

			if (mylat->backtrace[q] != record) {
				same = 0;
				break;
			}

			/* 0 and ULONG_MAX entries mean end of backtrace: */
			if (record == 0 || record == ULONG_MAX)
				break;
		}
		if (same) {
			mylat->count++;
			mylat->time += lat.time;
			if (lat.time > mylat->max)
				mylat->max = lat.time;
			goto out_unlock;
		}
	}

	/*
	 * short term hack; if we're > 32 we stop; future we recycle:
	 */
	if (tsk->latency_record_count >= LT_SAVECOUNT)
		goto out_unlock;

	/* Allocated a new one: */
	i = tsk->latency_record_count++;
	memcpy(&tsk->latency_record[i], &lat, sizeof(struct latency_record));

out_unlock:
	spin_unlock_irqrestore(&latency_lock, flags);
}

static int lstats_show(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "Latency Top version : v0.1\n");

	for (i = 0; i < MAXLR; i++) {
		if (latency_record[i].backtrace[0]) {
			int q;
			seq_printf(m, "%i %lu %lu ",
				latency_record[i].count,
				latency_record[i].time,
				latency_record[i].max);
			for (q = 0; q < LT_BACKTRACEDEPTH; q++) {
				char sym[KSYM_SYMBOL_LEN];
				char *c;
				if (!latency_record[i].backtrace[q])
					break;
				if (latency_record[i].backtrace[q] == ULONG_MAX)
					break;
				sprint_symbol(sym, latency_record[i].backtrace[q]);
				c = strchr(sym, '+');
				if (c)
					*c = 0;
				seq_printf(m, "%s ", sym);
			}
			seq_printf(m, "\n");
		}
	}
	return 0;
}

static ssize_t
lstats_write(struct file *file, const char __user *buf, size_t count,
	     loff_t *offs)
{
	clear_global_latency_tracing();

	return count;
}

static int lstats_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, lstats_show, NULL);
}

static const struct file_operations lstats_fops = {
	.open		= lstats_open,
	.read		= seq_read,
	.write		= lstats_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init init_lstats_procfs(void)
{
	proc_create("latency_stats", 0644, NULL, &lstats_fops);
	return 0;
}
device_initcall(init_lstats_procfs);
