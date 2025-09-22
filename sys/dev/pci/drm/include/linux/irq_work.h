/*	$OpenBSD: irq_work.h,v 1.9 2022/07/27 07:08:34 jsg Exp $	*/
/*
 * Copyright (c) 2015 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_IRQ_WORK_H
#define _LINUX_IRQ_WORK_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/task.h>

#include <linux/llist.h>

struct workqueue_struct;

extern struct workqueue_struct *system_wq;

struct irq_node {
	struct llist_node llist;
};

struct irq_work {
	struct task task;
	struct taskq *tq;
	struct irq_node node;
};

typedef void (*irq_work_func_t)(struct irq_work *);

static inline void
init_irq_work(struct irq_work *work, irq_work_func_t func)
{
	work->tq = (struct taskq *)system_wq;
	task_set(&work->task, (void (*)(void *))func, work);
}

static inline bool
irq_work_queue(struct irq_work *work)
{
	return task_add(work->tq, &work->task);
}

static inline void
irq_work_sync(struct irq_work *work)
{
	taskq_barrier(work->tq);
}

#endif
