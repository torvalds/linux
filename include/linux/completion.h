#ifndef __LINUX_COMPLETION_H
#define __LINUX_COMPLETION_H

/*
 * (C) Copyright 2001 Linus Torvalds
 *
 * Atomic wait-for-completion handler data structures.
 * See kernel/sched.c for details.
 */

#include <linux/wait.h>

struct completion {
	unsigned int done;
	wait_queue_head_t wait;
};

#define COMPLETION_INITIALIZER(work) \
	{ 0, __WAIT_QUEUE_HEAD_INITIALIZER((work).wait) }

#define DECLARE_COMPLETION(work) \
	struct completion work = COMPLETION_INITIALIZER(work)

static inline void init_completion(struct completion *x)
{
	x->done = 0;
	init_waitqueue_head(&x->wait);
}

extern void FASTCALL(wait_for_completion(struct completion *));
extern int FASTCALL(wait_for_completion_interruptible(struct completion *x));
extern unsigned long FASTCALL(wait_for_completion_timeout(struct completion *x,
						   unsigned long timeout));
extern unsigned long FASTCALL(wait_for_completion_interruptible_timeout(
			struct completion *x, unsigned long timeout));

extern void FASTCALL(complete(struct completion *));
extern void FASTCALL(complete_all(struct completion *));

#define INIT_COMPLETION(x)	((x).done = 0)

#endif
