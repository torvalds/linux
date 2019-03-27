/*-
 * Copyright (c) 2017 Hans Petter Selasky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/gtaskqueue.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include <linux/compiler.h>
#include <linux/interrupt.h>
#include <linux/compat.h>

#define	TASKLET_ST_IDLE 0
#define	TASKLET_ST_BUSY 1
#define	TASKLET_ST_EXEC 2
#define	TASKLET_ST_LOOP 3
#define	TASKLET_ST_PAUSED 4

#define	TASKLET_ST_CMPSET(ts, old, new)	\
	atomic_cmpset_ptr((volatile uintptr_t *)&(ts)->entry.tqe_prev, old, new)

#define	TASKLET_ST_SET(ts, new)	\
	WRITE_ONCE(*(volatile uintptr_t *)&(ts)->entry.tqe_prev, new)

#define	TASKLET_ST_GET(ts) \
	READ_ONCE(*(volatile uintptr_t *)&(ts)->entry.tqe_prev)

struct tasklet_worker {
	struct mtx mtx;
	TAILQ_HEAD(, tasklet_struct) head;
	struct grouptask gtask;
} __aligned(CACHE_LINE_SIZE);

#define	TASKLET_WORKER_LOCK(tw) mtx_lock(&(tw)->mtx)
#define	TASKLET_WORKER_UNLOCK(tw) mtx_unlock(&(tw)->mtx)

DPCPU_DEFINE_STATIC(struct tasklet_worker, tasklet_worker);

static void
tasklet_handler(void *arg)
{
	struct tasklet_worker *tw = (struct tasklet_worker *)arg;
	struct tasklet_struct *ts;

	linux_set_current(curthread);

	TASKLET_WORKER_LOCK(tw);
	while (1) {
		ts = TAILQ_FIRST(&tw->head);
		if (ts == NULL)
			break;
		TAILQ_REMOVE(&tw->head, ts, entry);

		TASKLET_WORKER_UNLOCK(tw);
		do {
			/* reset executing state */
			TASKLET_ST_SET(ts, TASKLET_ST_EXEC);

			ts->func(ts->data);

		} while (TASKLET_ST_CMPSET(ts, TASKLET_ST_EXEC, TASKLET_ST_IDLE) == 0);
		TASKLET_WORKER_LOCK(tw);
	}
	TASKLET_WORKER_UNLOCK(tw);
}

static void
tasklet_subsystem_init(void *arg __unused)
{
	struct tasklet_worker *tw;
	char buf[32];
	int i;

	CPU_FOREACH(i) {
		if (CPU_ABSENT(i))
			continue;

		tw = DPCPU_ID_PTR(i, tasklet_worker);

		mtx_init(&tw->mtx, "linux_tasklet", NULL, MTX_DEF);
		TAILQ_INIT(&tw->head);
		GROUPTASK_INIT(&tw->gtask, 0, tasklet_handler, tw);
		snprintf(buf, sizeof(buf), "softirq%d", i);
		taskqgroup_attach_cpu(qgroup_softirq, &tw->gtask,
		    "tasklet", i, NULL, NULL, buf);
       }
}
SYSINIT(linux_tasklet, SI_SUB_TASKQ, SI_ORDER_THIRD, tasklet_subsystem_init, NULL);

static void
tasklet_subsystem_uninit(void *arg __unused)
{
	struct tasklet_worker *tw;
	int i;

	CPU_FOREACH(i) {
		if (CPU_ABSENT(i))
			continue;

		tw = DPCPU_ID_PTR(i, tasklet_worker);

		taskqgroup_detach(qgroup_softirq, &tw->gtask);
		mtx_destroy(&tw->mtx);
	}
}
SYSUNINIT(linux_tasklet, SI_SUB_TASKQ, SI_ORDER_THIRD, tasklet_subsystem_uninit, NULL);

void
tasklet_init(struct tasklet_struct *ts,
    tasklet_func_t *func, unsigned long data)
{
	ts->entry.tqe_prev = NULL;
	ts->entry.tqe_next = NULL;
	ts->func = func;
	ts->data = data;
}

void
local_bh_enable(void)
{
	sched_unpin();
}

void
local_bh_disable(void)
{
	sched_pin();
}

void
tasklet_schedule(struct tasklet_struct *ts)
{

	if (TASKLET_ST_CMPSET(ts, TASKLET_ST_EXEC, TASKLET_ST_LOOP)) {
		/* tasklet_handler() will loop */
	} else if (TASKLET_ST_CMPSET(ts, TASKLET_ST_IDLE, TASKLET_ST_BUSY)) {
		struct tasklet_worker *tw;

		tw = &DPCPU_GET(tasklet_worker);

		/* tasklet_handler() was not queued */
		TASKLET_WORKER_LOCK(tw);
		/* enqueue tasklet */
		TAILQ_INSERT_TAIL(&tw->head, ts, entry);
		/* schedule worker */
		GROUPTASK_ENQUEUE(&tw->gtask);
		TASKLET_WORKER_UNLOCK(tw);
	} else {
		/*
		 * tasklet_handler() is already executing
		 *
		 * If the state is neither EXEC nor IDLE, it is either
		 * LOOP or BUSY. If the state changed between the two
		 * CMPSET's above the only possible transitions by
		 * elimination are LOOP->EXEC and BUSY->EXEC. If a
		 * EXEC->LOOP transition was missed that is not a
		 * problem because the callback function is then
		 * already about to be called again.
		 */
	}
}

void
tasklet_kill(struct tasklet_struct *ts)
{

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, "tasklet_kill() can sleep");

	/* wait until tasklet is no longer busy */
	while (TASKLET_ST_GET(ts) != TASKLET_ST_IDLE)
		pause("W", 1);
}

void
tasklet_enable(struct tasklet_struct *ts)
{
	(void) TASKLET_ST_CMPSET(ts, TASKLET_ST_PAUSED, TASKLET_ST_IDLE);
}

void
tasklet_disable(struct tasklet_struct *ts)
{
	while (1) {
		if (TASKLET_ST_GET(ts) == TASKLET_ST_PAUSED) 
			break;
		if (TASKLET_ST_CMPSET(ts, TASKLET_ST_IDLE, TASKLET_ST_PAUSED))
			break;
		pause("W", 1);
	}
}
