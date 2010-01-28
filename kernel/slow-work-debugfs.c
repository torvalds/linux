/* Slow work debugging
 *
 * Copyright (C) 2009 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/slow-work.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/seq_file.h>
#include "slow-work.h"

#define ITERATOR_SHIFT		(BITS_PER_LONG - 4)
#define ITERATOR_SELECTOR	(0xfUL << ITERATOR_SHIFT)
#define ITERATOR_COUNTER	(~ITERATOR_SELECTOR)

void slow_work_new_thread_desc(struct slow_work *work, struct seq_file *m)
{
	seq_puts(m, "Slow-work: New thread");
}

/*
 * Render the time mark field on a work item into a 5-char time with units plus
 * a space
 */
static void slow_work_print_mark(struct seq_file *m, struct slow_work *work)
{
	struct timespec now, diff;

	now = CURRENT_TIME;
	diff = timespec_sub(now, work->mark);

	if (diff.tv_sec < 0)
		seq_puts(m, "  -ve ");
	else if (diff.tv_sec == 0 && diff.tv_nsec < 1000)
		seq_printf(m, "%3luns ", diff.tv_nsec);
	else if (diff.tv_sec == 0 && diff.tv_nsec < 1000000)
		seq_printf(m, "%3luus ", diff.tv_nsec / 1000);
	else if (diff.tv_sec == 0 && diff.tv_nsec < 1000000000)
		seq_printf(m, "%3lums ", diff.tv_nsec / 1000000);
	else if (diff.tv_sec <= 1)
		seq_puts(m, "   1s ");
	else if (diff.tv_sec < 60)
		seq_printf(m, "%4lus ", diff.tv_sec);
	else if (diff.tv_sec < 60 * 60)
		seq_printf(m, "%4lum ", diff.tv_sec / 60);
	else if (diff.tv_sec < 60 * 60 * 24)
		seq_printf(m, "%4luh ", diff.tv_sec / 3600);
	else
		seq_puts(m, "exces ");
}

/*
 * Describe a slow work item for debugfs
 */
static int slow_work_runqueue_show(struct seq_file *m, void *v)
{
	struct slow_work *work;
	struct list_head *p = v;
	unsigned long id;

	switch ((unsigned long) v) {
	case 1:
		seq_puts(m, "THR PID   ITEM ADDR        FL MARK  DESC\n");
		return 0;
	case 2:
		seq_puts(m, "=== ===== ================ == ===== ==========\n");
		return 0;

	case 3 ... 3 + SLOW_WORK_THREAD_LIMIT - 1:
		id = (unsigned long) v - 3;

		read_lock(&slow_work_execs_lock);
		work = slow_work_execs[id];
		if (work) {
			smp_read_barrier_depends();

			seq_printf(m, "%3lu %5d %16p %2lx ",
				   id, slow_work_pids[id], work, work->flags);
			slow_work_print_mark(m, work);

			if (work->ops->desc)
				work->ops->desc(work, m);
			seq_putc(m, '\n');
		}
		read_unlock(&slow_work_execs_lock);
		return 0;

	default:
		work = list_entry(p, struct slow_work, link);
		seq_printf(m, "%3s     - %16p %2lx ",
			   work->flags & SLOW_WORK_VERY_SLOW ? "vsq" : "sq",
			   work, work->flags);
		slow_work_print_mark(m, work);

		if (work->ops->desc)
			work->ops->desc(work, m);
		seq_putc(m, '\n');
		return 0;
	}
}

/*
 * map the iterator to a work item
 */
static void *slow_work_runqueue_index(struct seq_file *m, loff_t *_pos)
{
	struct list_head *p;
	unsigned long count, id;

	switch (*_pos >> ITERATOR_SHIFT) {
	case 0x0:
		if (*_pos == 0)
			*_pos = 1;
		if (*_pos < 3)
			return (void *)(unsigned long) *_pos;
		if (*_pos < 3 + SLOW_WORK_THREAD_LIMIT)
			for (id = *_pos - 3;
			     id < SLOW_WORK_THREAD_LIMIT;
			     id++, (*_pos)++)
				if (slow_work_execs[id])
					return (void *)(unsigned long) *_pos;
		*_pos = 0x1UL << ITERATOR_SHIFT;

	case 0x1:
		count = *_pos & ITERATOR_COUNTER;
		list_for_each(p, &slow_work_queue) {
			if (count == 0)
				return p;
			count--;
		}
		*_pos = 0x2UL << ITERATOR_SHIFT;

	case 0x2:
		count = *_pos & ITERATOR_COUNTER;
		list_for_each(p, &vslow_work_queue) {
			if (count == 0)
				return p;
			count--;
		}
		*_pos = 0x3UL << ITERATOR_SHIFT;

	default:
		return NULL;
	}
}

/*
 * set up the iterator to start reading from the first line
 */
static void *slow_work_runqueue_start(struct seq_file *m, loff_t *_pos)
{
	spin_lock_irq(&slow_work_queue_lock);
	return slow_work_runqueue_index(m, _pos);
}

/*
 * move to the next line
 */
static void *slow_work_runqueue_next(struct seq_file *m, void *v, loff_t *_pos)
{
	struct list_head *p = v;
	unsigned long selector = *_pos >> ITERATOR_SHIFT;

	(*_pos)++;
	switch (selector) {
	case 0x0:
		return slow_work_runqueue_index(m, _pos);

	case 0x1:
		if (*_pos >> ITERATOR_SHIFT == 0x1) {
			p = p->next;
			if (p != &slow_work_queue)
				return p;
		}
		*_pos = 0x2UL << ITERATOR_SHIFT;
		p = &vslow_work_queue;

	case 0x2:
		if (*_pos >> ITERATOR_SHIFT == 0x2) {
			p = p->next;
			if (p != &vslow_work_queue)
				return p;
		}
		*_pos = 0x3UL << ITERATOR_SHIFT;

	default:
		return NULL;
	}
}

/*
 * clean up after reading
 */
static void slow_work_runqueue_stop(struct seq_file *m, void *v)
{
	spin_unlock_irq(&slow_work_queue_lock);
}

static const struct seq_operations slow_work_runqueue_ops = {
	.start		= slow_work_runqueue_start,
	.stop		= slow_work_runqueue_stop,
	.next		= slow_work_runqueue_next,
	.show		= slow_work_runqueue_show,
};

/*
 * open "/sys/kernel/debug/slow_work/runqueue" to list queue contents
 */
static int slow_work_runqueue_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &slow_work_runqueue_ops);
}

const struct file_operations slow_work_runqueue_fops = {
	.owner		= THIS_MODULE,
	.open		= slow_work_runqueue_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
