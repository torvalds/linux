/*	$OpenBSD: task.h,v 1.18 2020/08/01 08:40:20 anton Exp $ */

/*
 * Copyright (c) 2013 David Gwynne <dlg@openbsd.org>
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

#ifndef _SYS_TASK_H_
#define _SYS_TASK_H_

#include <sys/queue.h>

struct taskq;

struct task {
	TAILQ_ENTRY(task) t_entry;
	void		(*t_func)(void *);
	void		*t_arg;
	unsigned int	t_flags;
#if 1 /* NKCOV > 0 */
	struct process	*t_process;
#endif
};

#define TASK_ONQUEUE		1

TAILQ_HEAD(task_list, task);

#define TASKQ_MPSAFE		(1 << 0)

#define TASK_INITIALIZER(_f, _a)  {{ NULL, NULL }, (_f), (_a), 0 }

#ifdef _KERNEL
extern struct taskq *const systq;
extern struct taskq *const systqmp;

struct taskq	*taskq_create(const char *, unsigned int, int, unsigned int);
void		 taskq_destroy(struct taskq *);
void		 taskq_barrier(struct taskq *);

void		 taskq_del_barrier(struct taskq *, struct task *);

void		 task_set(struct task *, void (*)(void *), void *);
int		 task_add(struct taskq *, struct task *);
int		 task_del(struct taskq *, struct task *);

#define task_pending(_t)	((_t)->t_flags & TASK_ONQUEUE)

#endif /* _KERNEL */

#endif /* _SYS_TASK_H_ */
