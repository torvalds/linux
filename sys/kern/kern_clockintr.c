/* $OpenBSD: kern_clockintr.c,v 1.71 2024/11/07 16:02:29 miod Exp $ */
/*
 * Copyright (c) 2003 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/clockintr.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/resourcevar.h>
#include <sys/queue.h>
#include <sys/sched.h>
#include <sys/stdint.h>
#include <sys/sysctl.h>
#include <sys/time.h>

void clockintr_cancel_locked(struct clockintr *);
void clockintr_hardclock(struct clockrequest *, void *, void *);
void clockintr_schedule_locked(struct clockintr *, uint64_t);
void clockqueue_intrclock_install(struct clockqueue *,
    const struct intrclock *);
void clockqueue_intrclock_reprogram(struct clockqueue *);
uint64_t clockqueue_next(const struct clockqueue *);
void clockqueue_pend_delete(struct clockqueue *, struct clockintr *);
void clockqueue_pend_insert(struct clockqueue *, struct clockintr *,
    uint64_t);
void intrclock_rearm(struct intrclock *, uint64_t);
void intrclock_trigger(struct intrclock *);
uint64_t nsec_advance(uint64_t *, uint64_t, uint64_t);

/*
 * Ready the calling CPU for clockintr_dispatch().  If this is our
 * first time here, install the intrclock, if any, and set necessary
 * flags.  Advance the schedule as needed.
 */
void
clockintr_cpu_init(const struct intrclock *ic)
{
	uint64_t multiplier = 0;
	struct cpu_info *ci = curcpu();
	struct clockqueue *cq = &ci->ci_queue;
	struct schedstate_percpu *spc = &ci->ci_schedstate;
	int reset_cq_intrclock = 0;

	if (ic != NULL)
		clockqueue_intrclock_install(cq, ic);

	/* TODO: Remove this from struct clockqueue. */
	if (CPU_IS_PRIMARY(ci) && cq->cq_hardclock.cl_expiration == 0) {
		clockintr_bind(&cq->cq_hardclock, ci, clockintr_hardclock,
		    NULL);
	}

	/*
	 * Mask CQ_INTRCLOCK while we're advancing the internal clock
	 * interrupts.  We don't want the intrclock to fire until this
	 * thread reaches clockintr_trigger().
	 */
	if (ISSET(cq->cq_flags, CQ_INTRCLOCK)) {
		CLR(cq->cq_flags, CQ_INTRCLOCK);
		reset_cq_intrclock = 1;
	}

	/*
	 * Until we understand scheduler lock contention better, stagger
	 * the hardclock and statclock so they don't all happen at once.
	 * If we have no intrclock it doesn't matter, we have no control
	 * anyway.  The primary CPU's starting offset is always zero, so
	 * leave the multiplier zero.
	 */
	if (!CPU_IS_PRIMARY(ci) && reset_cq_intrclock)
		multiplier = CPU_INFO_UNIT(ci);

	/*
	 * The first time we do this, the primary CPU cannot skip any
	 * hardclocks.  We can skip hardclocks on subsequent calls because
	 * the global tick value is advanced during inittodr(9) on our
	 * behalf.
	 */
	if (CPU_IS_PRIMARY(ci)) {
		if (cq->cq_hardclock.cl_expiration == 0)
			clockintr_schedule(&cq->cq_hardclock, 0);
		else
			clockintr_advance(&cq->cq_hardclock, hardclock_period);
	}

	/*
	 * We can always advance the statclock.  There is no reason to
	 * stagger a randomized statclock.
	 */
	if (!statclock_is_randomized) {
		if (spc->spc_statclock.cl_expiration == 0) {
			clockintr_stagger(&spc->spc_statclock, statclock_avg,
			    multiplier, MAXCPUS);
		}
	}
	clockintr_advance(&spc->spc_statclock, statclock_avg);

	/*
	 * XXX Need to find a better place to do this.  We can't do it in
	 * sched_init_cpu() because initclocks() runs after it.
	 */
	if (spc->spc_itimer.cl_expiration == 0) {
		clockintr_stagger(&spc->spc_itimer, hardclock_period,
		    multiplier, MAXCPUS);
	}
	if (spc->spc_profclock.cl_expiration == 0) {
		clockintr_stagger(&spc->spc_profclock, profclock_period,
		    multiplier, MAXCPUS);
	}
	if (spc->spc_roundrobin.cl_expiration == 0) {
		clockintr_stagger(&spc->spc_roundrobin, hardclock_period,
		    multiplier, MAXCPUS);
	}
	clockintr_advance(&spc->spc_roundrobin, roundrobin_period);

	if (reset_cq_intrclock)
		SET(cq->cq_flags, CQ_INTRCLOCK);
}

/*
 * If we have an intrclock, trigger it to start the dispatch cycle.
 */
void
clockintr_trigger(void)
{
	struct clockqueue *cq = &curcpu()->ci_queue;

	KASSERT(ISSET(cq->cq_flags, CQ_INIT));

	if (ISSET(cq->cq_flags, CQ_INTRCLOCK))
		intrclock_trigger(&cq->cq_intrclock);
}

/*
 * Run all expired events scheduled on the calling CPU.
 */
int
clockintr_dispatch(void *frame)
{
	uint64_t lateness, run = 0, start;
	struct cpu_info *ci = curcpu();
	struct clockintr *cl;
	struct clockqueue *cq = &ci->ci_queue;
	struct clockrequest *request = &cq->cq_request;
	void *arg;
	void (*func)(struct clockrequest *, void *, void *);
	uint32_t ogen;

	if (cq->cq_dispatch != 0)
		panic("%s: recursive dispatch", __func__);
	cq->cq_dispatch = 1;

	splassert(IPL_CLOCK);
	KASSERT(ISSET(cq->cq_flags, CQ_INIT));

	mtx_enter(&cq->cq_mtx);

	/*
	 * If nothing is scheduled or we arrived too early, we have
	 * nothing to do.
	 */
	start = nsecuptime();
	cq->cq_uptime = start;
	if (TAILQ_EMPTY(&cq->cq_pend))
		goto stats;
	if (cq->cq_uptime < clockqueue_next(cq))
		goto rearm;
	lateness = start - clockqueue_next(cq);

	/*
	 * Dispatch expired events.
	 */
	for (;;) {
		cl = TAILQ_FIRST(&cq->cq_pend);
		if (cl == NULL)
			break;
		if (cq->cq_uptime < cl->cl_expiration) {
			/* Double-check the time before giving up. */
			cq->cq_uptime = nsecuptime();
			if (cq->cq_uptime < cl->cl_expiration)
				break;
		}

		/*
		 * This clockintr has expired.  Execute it.
		 */
		clockqueue_pend_delete(cq, cl);
		request->cr_expiration = cl->cl_expiration;
		arg = cl->cl_arg;
		func = cl->cl_func;
		cq->cq_running = cl;
		mtx_leave(&cq->cq_mtx);

		func(request, frame, arg);

		mtx_enter(&cq->cq_mtx);
		cq->cq_running = NULL;
		if (ISSET(cq->cq_flags, CQ_IGNORE_REQUEST)) {
			CLR(cq->cq_flags, CQ_IGNORE_REQUEST);
			CLR(request->cr_flags, CR_RESCHEDULE);
		}
		if (ISSET(request->cr_flags, CR_RESCHEDULE)) {
			CLR(request->cr_flags, CR_RESCHEDULE);
			clockqueue_pend_insert(cq, cl, request->cr_expiration);
		}
		if (ISSET(cq->cq_flags, CQ_NEED_WAKEUP)) {
			CLR(cq->cq_flags, CQ_NEED_WAKEUP);
			mtx_leave(&cq->cq_mtx);
			wakeup(&cq->cq_running);
			mtx_enter(&cq->cq_mtx);
		}
		run++;
	}

	/*
	 * Dispatch complete.
	 */
rearm:
	/* Rearm the interrupt clock if we have one. */
	if (ISSET(cq->cq_flags, CQ_INTRCLOCK)) {
		if (!TAILQ_EMPTY(&cq->cq_pend)) {
			intrclock_rearm(&cq->cq_intrclock,
			    clockqueue_next(cq) - cq->cq_uptime);
		}
	}
stats:
	/* Update our stats. */
	ogen = cq->cq_gen;
	cq->cq_gen = 0;
	membar_producer();
	cq->cq_stat.cs_dispatched += cq->cq_uptime - start;
	if (run > 0) {
		cq->cq_stat.cs_lateness += lateness;
		cq->cq_stat.cs_prompt++;
		cq->cq_stat.cs_run += run;
	} else if (!TAILQ_EMPTY(&cq->cq_pend)) {
		cq->cq_stat.cs_early++;
		cq->cq_stat.cs_earliness += clockqueue_next(cq) - cq->cq_uptime;
	} else
		cq->cq_stat.cs_spurious++;
	membar_producer();
	cq->cq_gen = MAX(1, ogen + 1);

	mtx_leave(&cq->cq_mtx);

	if (cq->cq_dispatch != 1)
		panic("%s: unexpected value: %u", __func__, cq->cq_dispatch);
	cq->cq_dispatch = 0;

	return run > 0;
}

uint64_t
clockintr_advance(struct clockintr *cl, uint64_t period)
{
	uint64_t count, expiration;
	struct clockqueue *cq = cl->cl_queue;

	mtx_enter(&cq->cq_mtx);
	expiration = cl->cl_expiration;
	count = nsec_advance(&expiration, period, nsecuptime());
	clockintr_schedule_locked(cl, expiration);
	mtx_leave(&cq->cq_mtx);

	return count;
}

uint64_t
clockrequest_advance(struct clockrequest *cr, uint64_t period)
{
	struct clockqueue *cq = cr->cr_queue;

	KASSERT(cr == &cq->cq_request);

	SET(cr->cr_flags, CR_RESCHEDULE);
	return nsec_advance(&cr->cr_expiration, period, cq->cq_uptime);
}

uint64_t
clockrequest_advance_random(struct clockrequest *cr, uint64_t min,
    uint32_t mask)
{
	uint64_t count = 0;
	struct clockqueue *cq = cr->cr_queue;
	uint32_t off;

	KASSERT(cr == &cq->cq_request);

	while (cr->cr_expiration <= cq->cq_uptime) {
		while ((off = (random() & mask)) == 0)
			continue;
		cr->cr_expiration += min + off;
		count++;
	}
	SET(cr->cr_flags, CR_RESCHEDULE);
	return count;
}

void
clockintr_cancel(struct clockintr *cl)
{
	struct clockqueue *cq = cl->cl_queue;

	mtx_enter(&cq->cq_mtx);
	clockintr_cancel_locked(cl);
	mtx_leave(&cq->cq_mtx);
}

void
clockintr_cancel_locked(struct clockintr *cl)
{
	struct clockqueue *cq = cl->cl_queue;
	int was_next;

	MUTEX_ASSERT_LOCKED(&cq->cq_mtx);

	if (ISSET(cl->cl_flags, CLST_PENDING)) {
		was_next = cl == TAILQ_FIRST(&cq->cq_pend);
		clockqueue_pend_delete(cq, cl);
		if (ISSET(cq->cq_flags, CQ_INTRCLOCK)) {
			if (was_next && !TAILQ_EMPTY(&cq->cq_pend)) {
				if (cq == &curcpu()->ci_queue)
					clockqueue_intrclock_reprogram(cq);
			}
		}
	}
	if (cl == cq->cq_running)
		SET(cq->cq_flags, CQ_IGNORE_REQUEST);
}

void
clockintr_bind(struct clockintr *cl, struct cpu_info *ci,
    void (*func)(struct clockrequest *, void *, void *), void *arg)
{
	struct clockqueue *cq = &ci->ci_queue;

	splassert(IPL_NONE);
	KASSERT(cl->cl_queue == NULL);

	mtx_enter(&cq->cq_mtx);
	cl->cl_arg = arg;
	cl->cl_func = func;
	cl->cl_queue = cq;
	TAILQ_INSERT_TAIL(&cq->cq_all, cl, cl_alink);
	mtx_leave(&cq->cq_mtx);
}

void
clockintr_unbind(struct clockintr *cl, uint32_t flags)
{
	struct clockqueue *cq = cl->cl_queue;

	KASSERT(!ISSET(flags, ~CL_FLAG_MASK));

	mtx_enter(&cq->cq_mtx);

	clockintr_cancel_locked(cl);

	cl->cl_arg = NULL;
	cl->cl_func = NULL;
	cl->cl_queue = NULL;
	TAILQ_REMOVE(&cq->cq_all, cl, cl_alink);

	if (ISSET(flags, CL_BARRIER) && cl == cq->cq_running) {
		SET(cq->cq_flags, CQ_NEED_WAKEUP);
		msleep_nsec(&cq->cq_running, &cq->cq_mtx, PWAIT | PNORELOCK,
		    "clkbar", INFSLP);
	} else
		mtx_leave(&cq->cq_mtx);
}

void
clockintr_schedule(struct clockintr *cl, uint64_t expiration)
{
	struct clockqueue *cq = cl->cl_queue;

	mtx_enter(&cq->cq_mtx);
	clockintr_schedule_locked(cl, expiration);
	mtx_leave(&cq->cq_mtx);
}

void
clockintr_schedule_locked(struct clockintr *cl, uint64_t expiration)
{
	struct clockqueue *cq = cl->cl_queue;

	MUTEX_ASSERT_LOCKED(&cq->cq_mtx);

	if (ISSET(cl->cl_flags, CLST_PENDING))
		clockqueue_pend_delete(cq, cl);
	clockqueue_pend_insert(cq, cl, expiration);
	if (ISSET(cq->cq_flags, CQ_INTRCLOCK)) {
		if (cl == TAILQ_FIRST(&cq->cq_pend)) {
			if (cq == &curcpu()->ci_queue)
				clockqueue_intrclock_reprogram(cq);
		}
	}
	if (cl == cq->cq_running)
		SET(cq->cq_flags, CQ_IGNORE_REQUEST);
}

void
clockintr_stagger(struct clockintr *cl, uint64_t period, uint32_t numer,
    uint32_t denom)
{
	struct clockqueue *cq = cl->cl_queue;

	KASSERT(numer < denom);

	mtx_enter(&cq->cq_mtx);
	if (ISSET(cl->cl_flags, CLST_PENDING))
		panic("%s: clock interrupt pending", __func__);
	cl->cl_expiration = period / denom * numer;
	mtx_leave(&cq->cq_mtx);
}

void
clockintr_hardclock(struct clockrequest *cr, void *frame, void *arg)
{
	uint64_t count, i;

	count = clockrequest_advance(cr, hardclock_period);
	for (i = 0; i < count; i++)
		hardclock(frame);
}

void
clockqueue_init(struct clockqueue *cq)
{
	if (ISSET(cq->cq_flags, CQ_INIT))
		return;

	cq->cq_request.cr_queue = cq;
	mtx_init(&cq->cq_mtx, IPL_CLOCK);
	TAILQ_INIT(&cq->cq_all);
	TAILQ_INIT(&cq->cq_pend);
	cq->cq_gen = 1;
	SET(cq->cq_flags, CQ_INIT);
}

void
clockqueue_intrclock_install(struct clockqueue *cq,
    const struct intrclock *ic)
{
	mtx_enter(&cq->cq_mtx);
	if (!ISSET(cq->cq_flags, CQ_INTRCLOCK)) {
		cq->cq_intrclock = *ic;
		SET(cq->cq_flags, CQ_INTRCLOCK);
	}
	mtx_leave(&cq->cq_mtx);
}

uint64_t
clockqueue_next(const struct clockqueue *cq)
{
	MUTEX_ASSERT_LOCKED(&cq->cq_mtx);
	return TAILQ_FIRST(&cq->cq_pend)->cl_expiration;
}

void
clockqueue_pend_delete(struct clockqueue *cq, struct clockintr *cl)
{
	MUTEX_ASSERT_LOCKED(&cq->cq_mtx);
	KASSERT(ISSET(cl->cl_flags, CLST_PENDING));

	TAILQ_REMOVE(&cq->cq_pend, cl, cl_plink);
	CLR(cl->cl_flags, CLST_PENDING);
}

void
clockqueue_pend_insert(struct clockqueue *cq, struct clockintr *cl,
    uint64_t expiration)
{
	struct clockintr *elm;

	MUTEX_ASSERT_LOCKED(&cq->cq_mtx);
	KASSERT(!ISSET(cl->cl_flags, CLST_PENDING));

	cl->cl_expiration = expiration;
	TAILQ_FOREACH(elm, &cq->cq_pend, cl_plink) {
		if (cl->cl_expiration < elm->cl_expiration)
			break;
	}
	if (elm == NULL)
		TAILQ_INSERT_TAIL(&cq->cq_pend, cl, cl_plink);
	else
		TAILQ_INSERT_BEFORE(elm, cl, cl_plink);
	SET(cl->cl_flags, CLST_PENDING);
}

void
clockqueue_intrclock_reprogram(struct clockqueue *cq)
{
	uint64_t exp, now;

	MUTEX_ASSERT_LOCKED(&cq->cq_mtx);
	KASSERT(ISSET(cq->cq_flags, CQ_INTRCLOCK));

	exp = clockqueue_next(cq);
	now = nsecuptime();
	if (now < exp)
		intrclock_rearm(&cq->cq_intrclock, exp - now);
	else
		intrclock_trigger(&cq->cq_intrclock);
}

void
intrclock_rearm(struct intrclock *ic, uint64_t nsecs)
{
	ic->ic_rearm(ic->ic_cookie, nsecs);
}

void
intrclock_trigger(struct intrclock *ic)
{
	ic->ic_trigger(ic->ic_cookie);
}

/*
 * Advance *next in increments of period until it exceeds now.
 * Returns the number of increments *next was advanced.
 *
 * We check the common cases first to avoid division if possible.
 * This does no overflow checking.
 */
uint64_t
nsec_advance(uint64_t *next, uint64_t period, uint64_t now)
{
	uint64_t elapsed;

	if (now < *next)
		return 0;

	if (now < *next + period) {
		*next += period;
		return 1;
	}

	elapsed = (now - *next) / period + 1;
	*next += period * elapsed;
	return elapsed;
}

int
sysctl_clockintr(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	struct clockintr_stat sum, tmp;
	struct clockqueue *cq;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	uint32_t gen;

	if (namelen != 1)
		return ENOTDIR;

	switch (name[0]) {
	case KERN_CLOCKINTR_STATS:
		memset(&sum, 0, sizeof sum);
		CPU_INFO_FOREACH(cii, ci) {
			cq = &ci->ci_queue;
			if (!ISSET(cq->cq_flags, CQ_INIT))
				continue;
			do {
				gen = cq->cq_gen;
				membar_consumer();
				tmp = cq->cq_stat;
				membar_consumer();
			} while (gen == 0 || gen != cq->cq_gen);
			sum.cs_dispatched += tmp.cs_dispatched;
			sum.cs_early += tmp.cs_early;
			sum.cs_earliness += tmp.cs_earliness;
			sum.cs_lateness += tmp.cs_lateness;
			sum.cs_prompt += tmp.cs_prompt;
			sum.cs_run += tmp.cs_run;
			sum.cs_spurious += tmp.cs_spurious;
		}
		return sysctl_rdstruct(oldp, oldlenp, newp, &sum, sizeof sum);
	default:
		break;
	}

	return EINVAL;
}

#ifdef DDB

#include <machine/db_machdep.h>

#include <ddb/db_interface.h>
#include <ddb/db_output.h>
#include <ddb/db_sym.h>

void db_show_clockintr(const struct clockintr *, const char *, u_int);
void db_show_clockintr_cpu(struct cpu_info *);

void
db_show_all_clockintr(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	struct timespec now;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	int width = sizeof(long) * 2 + 2;	/* +2 for "0x" prefix */

	nanouptime(&now);
	db_printf("%20s\n", "UPTIME");
	db_printf("%10lld.%09ld\n", now.tv_sec, now.tv_nsec);
	db_printf("\n");
	db_printf("%20s  %5s  %3s  %*s  %s\n",
	    "EXPIRATION", "STATE", "CPU", width, "ARG", "NAME");
	CPU_INFO_FOREACH(cii, ci) {
		if (ISSET(ci->ci_queue.cq_flags, CQ_INIT))
			db_show_clockintr_cpu(ci);
	}
}

void
db_show_clockintr_cpu(struct cpu_info *ci)
{
	struct clockintr *elm;
	struct clockqueue *cq = &ci->ci_queue;
	u_int cpu = CPU_INFO_UNIT(ci);

	if (cq->cq_running != NULL)
		db_show_clockintr(cq->cq_running, "run", cpu);
	TAILQ_FOREACH(elm, &cq->cq_pend, cl_plink)
		db_show_clockintr(elm, "pend", cpu);
	TAILQ_FOREACH(elm, &cq->cq_all, cl_alink) {
		if (!ISSET(elm->cl_flags, CLST_PENDING))
			db_show_clockintr(elm, "idle", cpu);
	}
}

void
db_show_clockintr(const struct clockintr *cl, const char *state, u_int cpu)
{
	struct timespec ts;
	const char *name;
	db_expr_t offset;
	int width = sizeof(long) * 2;

	NSEC_TO_TIMESPEC(cl->cl_expiration, &ts);
	db_find_sym_and_offset((vaddr_t)cl->cl_func, &name, &offset);
	if (name == NULL)
		name = "?";
	db_printf("%10lld.%09ld  %5s  %3u  0x%0*lx  %s\n",
	    ts.tv_sec, ts.tv_nsec, state, cpu,
	    width, (unsigned long)cl->cl_arg, name);
}

#endif /* DDB */
