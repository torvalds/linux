/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS__TASK_H_
#define _SYS__TASK_H_

#include <sys/queue.h>

/*
 * Each task includes a function which is called from
 * taskqueue_run().  The first argument is taken from the 'ta_context'
 * field of struct task and the second argument is a count of how many
 * times the task was enqueued before the call to taskqueue_run().
 *
 * List of locks
 * (c)	const after init
 * (q)	taskqueue lock
 */
typedef void task_fn_t(void *context, int pending);

struct task {
	STAILQ_ENTRY(task) ta_link;	/* (q) link for queue */
	uint16_t ta_pending;		/* (q) count times queued */
	u_short	ta_priority;		/* (c) Priority */
	task_fn_t *ta_func;		/* (c) task handler */
	void	*ta_context;		/* (c) argument for handler */
};

#ifdef _KERNEL

typedef void gtask_fn_t(void *context);

struct gtask {
	STAILQ_ENTRY(gtask) ta_link;	/* (q) link for queue */
	uint16_t ta_flags;		/* (q) state flags */
	u_short	ta_priority;		/* (c) Priority */
	gtask_fn_t *ta_func;		/* (c) task handler */
	void	*ta_context;		/* (c) argument for handler */
};

#endif /* _KERNEL */

#endif /* !_SYS__TASK_H_ */
