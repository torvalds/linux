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
#include <linux/latencytop.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/slab.h>
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
		for (q = 0 ; q < LT_BACKTRACEDEPTH ; q++) {
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

static inline void store_stacktrace(struct task_struct *tsk, struct latency_record *lat)
{
	struct stack_trace trace;

	memset(&trace, 0, sizeof(trace));
	trace.max_entries = LT_BACKTRACEDEPTH;
	trace.entries = &lat->backtrace[0];
	trace.skip = 0;
	save_stack_trace_tsk(tsk, &trace);
}

void __sched
account_scheduler_latency(struct task_struct *tsk, int usecs, int inter)
{
	unsigned long flags;
	int i, q;
	struct latency_record lat;

	if (!latencytop_enabled)
		return;

	/* Long interruptible waits are generally user requested... */
	if (inter && usecs > 5000)
		return;

	memset(&lat, 0, sizeof(lat));
	lat.count = 1;
	lat.time = usecs;
	lat.max = usecs;
	store_stacktrace(tsk, &lat);

	spin_lock_irqsave(&latency_lock, flags);

	account_global_scheduler_latency(tsk, &lat);

	/*
	 * short term hack; if we're > 32 we stop; future we recycle:
	 */
	tsk->latency_record_count++;
	if (tsk->latency_record_count >= LT_SAVECOUNT)
		goto out_unlock;

	for (i = 0; i < LT_SAVECOUNT ; i++) {
		struct latency_record *mylat;
		int same = 1;

		mylat = &tsk->latency_record[i];
		for (q = 0 ; q < LT_BACKTRACEDEPTH ; q++) {
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

	/* Allocated a new one: */
	i = tsk->latency_record_count;
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
			seq_printf(m, "%i %li %li ",
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

static struct file_operations lstats_fops = {
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
__initcall(init_lstats_procfs);
