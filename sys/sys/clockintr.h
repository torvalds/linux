/* $OpenBSD: clockintr.h,v 1.29 2024/02/25 19:15:50 cheloha Exp $ */
/*
 * Copyright (c) 2020-2024 Scott Cheloha <cheloha@openbsd.org>
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

#ifndef _SYS_CLOCKINTR_H_
#define _SYS_CLOCKINTR_H_

#include <sys/stdint.h>

struct clockintr_stat {
	uint64_t cs_dispatched;		/* total time in dispatch (ns) */
	uint64_t cs_early;		/* number of early dispatch calls */
	uint64_t cs_earliness;		/* total earliness (ns) */
	uint64_t cs_lateness;		/* total lateness (ns) */
	uint64_t cs_prompt;		/* number of prompt dispatch calls */
	uint64_t cs_run;		/* number of events dispatched */
	uint64_t cs_spurious;		/* number of spurious dispatch calls */
};

#ifdef _KERNEL

#include <sys/mutex.h>
#include <sys/queue.h>

struct clockqueue;
struct clockrequest;
struct cpu_info;

/*
 * Platform API
 */

struct intrclock {
	void *ic_cookie;
	void (*ic_rearm)(void *, uint64_t);
	void (*ic_trigger)(void *);
};

/*
 * Schedulable clock interrupt callback.
 *
 * Struct member protections:
 *
 *	I	Immutable after initialization.
 *	m	Parent queue mutex (cl_queue->cq_mtx).
 */
struct clockintr {
	uint64_t cl_expiration;				/* [m] dispatch time */
	TAILQ_ENTRY(clockintr) cl_alink;		/* [m] cq_all glue */
	TAILQ_ENTRY(clockintr) cl_plink;		/* [m] cq_pend glue */
	void *cl_arg;					/* [I] argument */
	void (*cl_func)(struct clockrequest *, void*, void*); /* [I] callback */
	struct clockqueue *cl_queue;			/* [I] parent queue */
	uint32_t cl_flags;				/* [m] CLST_* flags */
};

#define CLST_PENDING		0x00000001	/* scheduled to run */

/*
 * Interface for callback rescheduling requests.
 *
 * Struct member protections:
 *
 *	I	Immutable after initialization.
 *	o	Owned by a single CPU.
 */
struct clockrequest {
	uint64_t cr_expiration;			/* [o] copy of dispatch time */
	struct clockqueue *cr_queue;		/* [I] enclosing queue */
	uint32_t cr_flags;			/* [o] CR_* flags */
};

#define CR_RESCHEDULE		0x00000001	/* reschedule upon return */

/*
 * Per-CPU clock interrupt state.
 *
 * Struct member protections:
 *
 *	a	Modified atomically.
 *	I	Immutable after initialization.
 *	m	Per-queue mutex (cq_mtx).
 *	o	Owned by a single CPU.
 */
struct clockqueue {
	struct clockrequest cq_request;	/* [o] callback request object */
	struct mutex cq_mtx;		/* [a] per-queue mutex */
	uint64_t cq_uptime;		/* [o] cached uptime */
	TAILQ_HEAD(, clockintr) cq_all;	/* [m] established clockintr list */
	TAILQ_HEAD(, clockintr) cq_pend;/* [m] pending clockintr list */
	struct clockintr *cq_running;	/* [m] running clockintr */
	struct clockintr cq_hardclock;	/* [o] hardclock handle */
	struct intrclock cq_intrclock;	/* [I] local interrupt clock */
	struct clockintr_stat cq_stat;	/* [o] dispatch statistics */
	volatile uint32_t cq_gen;	/* [o] cq_stat update generation */ 
	volatile uint32_t cq_dispatch;	/* [o] dispatch is running */
	uint32_t cq_flags;		/* [m] CQ_* flags; see below */
};

#define CQ_INIT			0x00000001	/* clockintr_cpu_init() done */
#define CQ_INTRCLOCK		0x00000002	/* intrclock installed */
#define CQ_IGNORE_REQUEST	0x00000004	/* ignore callback requests */
#define CQ_NEED_WAKEUP		0x00000008	/* caller at barrier */
#define CQ_STATE_MASK		0x0000000f

void clockintr_cpu_init(const struct intrclock *);
int clockintr_dispatch(void *);
void clockintr_trigger(void);

/*
 * Kernel API
 */

#define CL_BARRIER	0x00000001	/* block if callback is running */
#define CL_FLAG_MASK	0x00000001

uint64_t clockintr_advance(struct clockintr *, uint64_t);
void clockintr_bind(struct clockintr *, struct cpu_info *,
    void (*)(struct clockrequest *, void *, void *), void *);
void clockintr_cancel(struct clockintr *);
void clockintr_schedule(struct clockintr *, uint64_t);
void clockintr_stagger(struct clockintr *, uint64_t, uint32_t, uint32_t);
void clockintr_unbind(struct clockintr *, uint32_t);
uint64_t clockrequest_advance(struct clockrequest *, uint64_t);
uint64_t clockrequest_advance_random(struct clockrequest *, uint64_t, uint32_t);
void clockqueue_init(struct clockqueue *);
int sysctl_clockintr(int *, u_int, void *, size_t *, void *, size_t);

#endif /* _KERNEL */

#endif /* !_SYS_CLOCKINTR_H_ */
