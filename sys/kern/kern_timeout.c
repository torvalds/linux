/*	$OpenBSD: kern_timeout.c,v 1.112 2025/07/28 05:25:44 dlg Exp $	*/
/*
 * Copyright (c) 2001 Thomas Nordin <nordin@openbsd.org>
 * Copyright (c) 2000-2001 Artur Grabowski <art@openbsd.org>
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/timeout.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/queue.h>			/* _Q_INVALIDATE */
#include <sys/sysctl.h>
#include <sys/witness.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_interface.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>
#endif

#include "kcov.h"
#if NKCOV > 0
#include <sys/kcov.h>
#endif

struct timeout_ctx {
	struct circq		*tctx_todo;
	struct timeout		*tctx_running;
};

/*
 * Locks used to protect global variables in this file:
 *
 *	I	immutable after initialization
 *	T	timeout_mutex
 */
struct mutex timeout_mutex = MUTEX_INITIALIZER(IPL_HIGH);

void *softclock_si;			/* [I] softclock() interrupt handle */
struct timeoutstat tostat;		/* [T] statistics and totals */

/*
 * Timeouts are kept in a hierarchical timing wheel. The to_time is the value
 * of the global variable "ticks" when the timeout should be called. There are
 * four levels with 256 buckets each.
 */
#define WHEELCOUNT 4
#define WHEELSIZE 256
#define WHEELMASK 255
#define WHEELBITS 8
#define BUCKETS (WHEELCOUNT * WHEELSIZE)

struct circq timeout_wheel[BUCKETS];	/* [T] Tick-based timeouts */
struct circq timeout_wheel_kc[BUCKETS];	/* [T] Clock-based timeouts */
struct circq timeout_new;		/* [T] New, unscheduled timeouts */
struct circq timeout_todo;		/* [T] Due or needs rescheduling */
static struct timeout_ctx timeout_ctx_si = {
	.tctx_todo = &timeout_todo,	/* [I] */
	.tctx_running = NULL,		/* [T] */
};
struct circq timeout_proc;		/* [T] Due + needs process context */
static struct timeout_ctx timeout_ctx_proc = {
	.tctx_todo = &timeout_proc,	/* [I] */
	.tctx_running = NULL,		/* [T] */
};
#ifdef MULTIPROCESSOR
struct circq timeout_proc_mp;		/* [T] Process ctx + no kernel lock */
static struct timeout_ctx timeout_ctx_proc_mp = {
	.tctx_todo = &timeout_proc_mp,	/* [I] */
	.tctx_running = NULL,		/* [T] */
};
#endif

time_t timeout_level_width[WHEELCOUNT];	/* [I] Wheel level width (seconds) */
struct timespec tick_ts;		/* [I] Length of a tick (1/hz secs) */

struct kclock {
	struct timespec kc_lastscan;	/* [T] Clock time at last wheel scan */
	struct timespec kc_late;	/* [T] Late if due prior */
	struct timespec kc_offset;	/* [T] Offset from primary kclock */
} timeout_kclock[KCLOCK_MAX];

#define MASKWHEEL(wheel, time) (((time) >> ((wheel)*WHEELBITS)) & WHEELMASK)

#define BUCKET(rel, abs)						\
    (timeout_wheel[							\
	((rel) <= (1 << (2*WHEELBITS)))					\
	    ? ((rel) <= (1 << WHEELBITS))				\
		? MASKWHEEL(0, (abs))					\
		: MASKWHEEL(1, (abs)) + WHEELSIZE			\
	    : ((rel) <= (1 << (3*WHEELBITS)))				\
		? MASKWHEEL(2, (abs)) + 2*WHEELSIZE			\
		: MASKWHEEL(3, (abs)) + 3*WHEELSIZE])

#define MOVEBUCKET(wheel, time)						\
    CIRCQ_CONCAT(&timeout_todo,						\
        &timeout_wheel[MASKWHEEL((wheel), (time)) + (wheel)*WHEELSIZE])

/*
 * Circular queue definitions.
 */

#define CIRCQ_INIT(elem) do {			\
	(elem)->next = (elem);			\
	(elem)->prev = (elem);			\
} while (0)

#define CIRCQ_INSERT_HEAD(list, elem) do {      \
	(elem)->next = (list)->next;		\
	(list)->next->prev = (elem);		\
	(list)->next = (elem);			\
	(elem)->prev = (list);			\
	tostat.tos_pending++;			\
} while (0)

#define CIRCQ_INSERT_TAIL(list, elem) do {	\
	(elem)->prev = (list)->prev;		\
	(elem)->next = (list);			\
	(list)->prev->next = (elem);		\
	(list)->prev = (elem);			\
	tostat.tos_pending++;			\
} while (0)

#define CIRCQ_CONCAT(fst, snd) do {		\
	if (!CIRCQ_EMPTY(snd)) {		\
		(fst)->prev->next = (snd)->next;\
		(snd)->next->prev = (fst)->prev;\
		(snd)->prev->next = (fst);      \
		(fst)->prev = (snd)->prev;      \
		CIRCQ_INIT(snd);		\
	}					\
} while (0)

#define CIRCQ_REMOVE(elem) do {			\
	(elem)->next->prev = (elem)->prev;      \
	(elem)->prev->next = (elem)->next;      \
	_Q_INVALIDATE((elem)->prev);		\
	_Q_INVALIDATE((elem)->next);		\
	tostat.tos_pending--;			\
} while (0)

#define CIRCQ_FIRST(elem) ((elem)->next)

#define CIRCQ_EMPTY(elem) (CIRCQ_FIRST(elem) == (elem))

#define CIRCQ_FOREACH(elem, list)		\
	for ((elem) = CIRCQ_FIRST(list);	\
	    (elem) != (list);			\
	    (elem) = CIRCQ_FIRST(elem))

#ifdef WITNESS
struct lock_object timeout_sleeplock_obj = {
	.lo_name = "timeout",
	.lo_flags = LO_WITNESS | LO_INITIALIZED | LO_SLEEPABLE |
	    (LO_CLASS_RWLOCK << LO_CLASSSHIFT)
};
struct lock_object timeout_spinlock_obj = {
	.lo_name = "timeout",
	.lo_flags = LO_WITNESS | LO_INITIALIZED |
	    (LO_CLASS_MUTEX << LO_CLASSSHIFT)
};
struct lock_type timeout_sleeplock_type = {
	.lt_name = "timeout"
};
struct lock_type timeout_spinlock_type = {
	.lt_name = "timeout"
};
#define TIMEOUT_LOCK_OBJ(needsproc) \
	((needsproc) ? &timeout_sleeplock_obj : &timeout_spinlock_obj)
#endif

void softclock(void *);
void softclock_create_thread(void *);
void softclock_process_kclock_timeout(struct timeout *, int);
void softclock_process_tick_timeout(struct timeout *, int);
void softclock_thread(void *);
#ifdef MULTIPROCESSOR
void softclock_thread_mp(void *);
#endif
uint32_t timeout_bucket(const struct timeout *);
uint32_t timeout_maskwheel(uint32_t, const struct timespec *);
void timeout_run(struct timeout_ctx *, struct timeout *);

/*
 * The first thing in a struct timeout is its struct circq, so we
 * can get back from a pointer to the latter to a pointer to the
 * whole timeout with just a cast.
 */
static inline struct timeout *
timeout_from_circq(struct circq *p)
{
	return ((struct timeout *)(p));
}

static inline void
timeout_sync_order(int needsproc)
{
	WITNESS_CHECKORDER(TIMEOUT_LOCK_OBJ(needsproc), LOP_NEWORDER, NULL);
}

static inline void
timeout_sync_enter(int needsproc)
{
	timeout_sync_order(needsproc);
	WITNESS_LOCK(TIMEOUT_LOCK_OBJ(needsproc), 0);
}

static inline void
timeout_sync_leave(int needsproc)
{
	WITNESS_UNLOCK(TIMEOUT_LOCK_OBJ(needsproc), 0);
}

/*
 * Some of the "math" in here is a bit tricky.
 *
 * We have to beware of wrapping ints.
 * We use the fact that any element added to the queue must be added with a
 * positive time. That means that any element `to' on the queue cannot be
 * scheduled to timeout further in time than INT_MAX, but to->to_time can
 * be positive or negative so comparing it with anything is dangerous.
 * The only way we can use the to->to_time value in any predictable way
 * is when we calculate how far in the future `to' will timeout -
 * "to->to_time - ticks". The result will always be positive for future
 * timeouts and 0 or negative for due timeouts.
 */

void
timeout_startup(void)
{
	int b, level;

	CIRCQ_INIT(&timeout_new);
	CIRCQ_INIT(&timeout_todo);
	CIRCQ_INIT(&timeout_proc);
#ifdef MULTIPROCESSOR
	CIRCQ_INIT(&timeout_proc_mp);
#endif
	for (b = 0; b < nitems(timeout_wheel); b++)
		CIRCQ_INIT(&timeout_wheel[b]);
	for (b = 0; b < nitems(timeout_wheel_kc); b++)
		CIRCQ_INIT(&timeout_wheel_kc[b]);

	for (level = 0; level < nitems(timeout_level_width); level++)
		timeout_level_width[level] = 2 << (level * WHEELBITS);
	NSEC_TO_TIMESPEC(tick_nsec, &tick_ts);
}

void
timeout_proc_init(void)
{
	softclock_si = softintr_establish(IPL_SOFTCLOCK, softclock, NULL);
	if (softclock_si == NULL)
		panic("%s: unable to register softclock interrupt", __func__);

	WITNESS_INIT(&timeout_sleeplock_obj, &timeout_sleeplock_type);
	WITNESS_INIT(&timeout_spinlock_obj, &timeout_spinlock_type);

	kthread_create_deferred(softclock_create_thread, NULL);
}

void
timeout_set(struct timeout *new, void (*fn)(void *), void *arg)
{
	timeout_set_flags(new, fn, arg, KCLOCK_NONE, 0);
}

void
timeout_set_flags(struct timeout *to, void (*fn)(void *), void *arg, int kclock,
    int flags)
{
	KASSERT(!ISSET(flags, ~(TIMEOUT_PROC | TIMEOUT_MPSAFE)));
	KASSERT(kclock >= KCLOCK_NONE && kclock < KCLOCK_MAX);

	to->to_func = fn;
	to->to_arg = arg;
	to->to_kclock = kclock;
	to->to_flags = flags | TIMEOUT_INITIALIZED;

	/* For now, only process context timeouts may be marked MP-safe. */
	if (ISSET(to->to_flags, TIMEOUT_MPSAFE))
		KASSERT(ISSET(to->to_flags, TIMEOUT_PROC));
}

void
timeout_set_proc(struct timeout *new, void (*fn)(void *), void *arg)
{
	timeout_set_flags(new, fn, arg, KCLOCK_NONE, TIMEOUT_PROC);
}

int
timeout_add(struct timeout *new, int to_ticks)
{
	int old_time;
	int ret = 1;

	KASSERT(ISSET(new->to_flags, TIMEOUT_INITIALIZED));
	KASSERT(new->to_kclock == KCLOCK_NONE);
	KASSERT(to_ticks >= 0);

	mtx_enter(&timeout_mutex);

	/* Initialize the time here, it won't change. */
	old_time = new->to_time;
	new->to_time = to_ticks + ticks;
	CLR(new->to_flags, TIMEOUT_TRIGGERED);

	/*
	 * If this timeout already is scheduled and now is moved
	 * earlier, reschedule it now. Otherwise leave it in place
	 * and let it be rescheduled later.
	 */
	if (ISSET(new->to_flags, TIMEOUT_ONQUEUE)) {
		if (new->to_time - ticks < old_time - ticks) {
			CIRCQ_REMOVE(&new->to_list);
			CIRCQ_INSERT_TAIL(&timeout_new, &new->to_list);
		}
		tostat.tos_readded++;
		ret = 0;
	} else {
		SET(new->to_flags, TIMEOUT_ONQUEUE);
		CIRCQ_INSERT_TAIL(&timeout_new, &new->to_list);
	}
#if NKCOV > 0
	if (!kcov_cold)
		new->to_process = curproc->p_p;
#endif
	tostat.tos_added++;
	mtx_leave(&timeout_mutex);

	return ret;
}

static inline int
timeout_add_ticks(struct timeout *to, uint64_t to_ticks)
{
	/*
	 * XXX to_ticks is added to the current ticks value, but
	 * timeouts are run in the next clock interrupt after ticks
	 * is incremented. however, the deadline comparison in
	 * softclock_process_tick_timeout will fire a timeout if it's
	 * deadline is at the current ticks value. eg, a to_ticks
	 * value of 1 plus the current ticks value will fire in the
	 * next interrupt, which will be too early. add 1 here to
	 * ensure the requested time has elapsed.
	 */

	if (to_ticks >= INT_MAX)
		to_ticks = INT_MAX;
	else
		to_ticks++;

	return timeout_add(to, (int)to_ticks);
}

int
timeout_add_sec(struct timeout *to, int secs)
{
	uint64_t to_ticks;

	KASSERT(secs >= 0);
	/* secs is a 31bit int, so this can't overflow 64bits */
	to_ticks = (uint64_t)hz * (uint64_t)secs;

	return timeout_add_ticks(to, to_ticks);
}

/*
 * interpret the specified times below as a AT LEAST how long the
 * system should wait before firing the timeouts. this requires
 * rounding up, which has the potential to overflow. if we detect
 * overflow, interpret it as "wait for as long as possible". this will
 * be shorter than specified time, which violates the "wait at least
 * this much time", but it's on the other end of the timescale.
 */

int
timeout_add_msec(struct timeout *to, uint64_t msecs)
{
	if (msecs >= (UINT64_MAX / 1000))
		return timeout_add(to, INT_MAX);

	return timeout_add_usec(to, msecs * 1000);
}

int
timeout_add_usec(struct timeout *to, uint64_t usecs)
{
	uint64_t to_ticks;

	if (usecs >= (UINT64_MAX - tick))
		return timeout_add(to, INT_MAX);

	to_ticks = (usecs + (tick - 1)) / tick;

	return timeout_add_ticks(to, to_ticks);
}

int
timeout_add_nsec(struct timeout *to, uint64_t nsecs)
{
	uint64_t to_ticks;

	if (nsecs >= (UINT64_MAX - tick_nsec))
		return timeout_add(to, INT_MAX);

	to_ticks = (nsecs + (tick_nsec - 1)) / tick_nsec;

	return timeout_add_ticks(to, to_ticks);
}

int
timeout_abs_ts(struct timeout *to, const struct timespec *abstime)
{
	struct timespec old_abstime;
	int ret = 1;

	mtx_enter(&timeout_mutex);

	KASSERT(ISSET(to->to_flags, TIMEOUT_INITIALIZED));
	KASSERT(to->to_kclock == KCLOCK_UPTIME);

	old_abstime = to->to_abstime;
	to->to_abstime = *abstime;
	CLR(to->to_flags, TIMEOUT_TRIGGERED);

	if (ISSET(to->to_flags, TIMEOUT_ONQUEUE)) {
		if (timespeccmp(abstime, &old_abstime, <)) {
			CIRCQ_REMOVE(&to->to_list);
			CIRCQ_INSERT_TAIL(&timeout_new, &to->to_list);
		}
		tostat.tos_readded++;
		ret = 0;
	} else {
		SET(to->to_flags, TIMEOUT_ONQUEUE);
		CIRCQ_INSERT_TAIL(&timeout_new, &to->to_list);
	}
#if NKCOV > 0
	if (!kcov_cold)
		to->to_process = curproc->p_p;
#endif
	tostat.tos_added++;

	mtx_leave(&timeout_mutex);

	return ret;
}

int
timeout_del(struct timeout *to)
{
	int ret = 0;

	mtx_enter(&timeout_mutex);
	if (ISSET(to->to_flags, TIMEOUT_ONQUEUE)) {
		CIRCQ_REMOVE(&to->to_list);
		CLR(to->to_flags, TIMEOUT_ONQUEUE);
		tostat.tos_cancelled++;
		ret = 1;
	}
	CLR(to->to_flags, TIMEOUT_TRIGGERED);
	tostat.tos_deleted++;
	mtx_leave(&timeout_mutex);

	return ret;
}

int
timeout_del_barrier(struct timeout *to)
{
	int removed;

	timeout_sync_order(ISSET(to->to_flags, TIMEOUT_PROC));

	removed = timeout_del(to);
	timeout_barrier(to);

	return removed;
}

void
timeout_barrier(struct timeout *to)
{
	struct timeout_ctx *tctx;
	struct timeout barrier;
	struct cond c;
	int flags;

	flags = to->to_flags & (TIMEOUT_PROC | TIMEOUT_MPSAFE);
	timeout_sync_order(ISSET(flags, TIMEOUT_PROC));

	timeout_set_flags(&barrier, cond_signal_handler, &c, KCLOCK_NONE,
	    flags);
	barrier.to_process = curproc->p_p;
	cond_init(&c);

	mtx_enter(&timeout_mutex);
	if (ISSET(flags, TIMEOUT_PROC)) {
#ifdef MULTIPROCESSOR
		if (ISSET(flags, TIMEOUT_MPSAFE))
			tctx = &timeout_ctx_proc_mp;
		else
#endif
			tctx = &timeout_ctx_proc;
	} else
		tctx = &timeout_ctx_si;

	if (tctx->tctx_running != to) {
		mtx_leave(&timeout_mutex);
		return;
	}

	barrier.to_time = ticks;
	SET(barrier.to_flags, TIMEOUT_ONQUEUE);
	CIRCQ_INSERT_HEAD(tctx->tctx_todo, &barrier.to_list);
	mtx_leave(&timeout_mutex);

	/*
	 * We know the relevant timeout context was running something
	 * and now also has the barrier to run, so we just have to
	 * wait for it to pick up the barrier task now.
	 */

	cond_wait(&c, "tmobar");
}

uint32_t
timeout_bucket(const struct timeout *to)
{
	struct timespec diff, shifted_abstime;
	struct kclock *kc;
	uint32_t level;

	KASSERT(to->to_kclock == KCLOCK_UPTIME);
	kc = &timeout_kclock[to->to_kclock];

	KASSERT(timespeccmp(&kc->kc_lastscan, &to->to_abstime, <));
	timespecsub(&to->to_abstime, &kc->kc_lastscan, &diff);
	for (level = 0; level < nitems(timeout_level_width) - 1; level++) {
		if (diff.tv_sec < timeout_level_width[level])
			break;
	}
	timespecadd(&to->to_abstime, &kc->kc_offset, &shifted_abstime);
	return level * WHEELSIZE + timeout_maskwheel(level, &shifted_abstime);
}

/*
 * Hash the absolute time into a bucket on a given level of the wheel.
 *
 * The complete hash is 32 bits.  The upper 25 bits are seconds, the
 * lower 7 bits are nanoseconds.  tv_nsec is a positive value less
 * than one billion so we need to divide it to isolate the desired
 * bits.  We can't just shift it.
 *
 * The level is used to isolate an 8-bit portion of the hash.  The
 * resulting number indicates which bucket the absolute time belongs
 * in on the given level of the wheel.
 */
uint32_t
timeout_maskwheel(uint32_t level, const struct timespec *abstime)
{
	uint32_t hi, lo;

 	hi = abstime->tv_sec << 7;
	lo = abstime->tv_nsec / 7812500;

	return ((hi | lo) >> (level * WHEELBITS)) & WHEELMASK;
}

/*
 * This is called from hardclock() on the primary CPU at the start of
 * every tick.
 */
void
timeout_hardclock_update(void)
{
	struct timespec elapsed, now;
	struct kclock *kc;
	struct timespec *lastscan = &timeout_kclock[KCLOCK_UPTIME].kc_lastscan;
	int b, done, first, i, last, level, need_softclock = 1, off;

	mtx_enter(&timeout_mutex);

	MOVEBUCKET(0, ticks);
	if (MASKWHEEL(0, ticks) == 0) {
		MOVEBUCKET(1, ticks);
		if (MASKWHEEL(1, ticks) == 0) {
			MOVEBUCKET(2, ticks);
			if (MASKWHEEL(2, ticks) == 0)
				MOVEBUCKET(3, ticks);
		}
	}

	/*
	 * Dump the buckets that expired while we were away.
	 *
	 * If the elapsed time has exceeded a level's limit then we need
	 * to dump every bucket in the level.  We have necessarily completed
	 * a lap of that level, too, so we need to process buckets in the
	 * next level.
	 *
	 * Otherwise we need to compare indices: if the index of the first
	 * expired bucket is greater than that of the last then we have
	 * completed a lap of the level and need to process buckets in the
	 * next level.
	 */
	nanouptime(&now);
	timespecsub(&now, lastscan, &elapsed);
	for (level = 0; level < nitems(timeout_level_width); level++) {
		first = timeout_maskwheel(level, lastscan);
		if (elapsed.tv_sec >= timeout_level_width[level]) {
			last = (first == 0) ? WHEELSIZE - 1 : first - 1;
			done = 0;
		} else {
			last = timeout_maskwheel(level, &now);
			done = first <= last;
		}
		off = level * WHEELSIZE;
		for (b = first;; b = (b + 1) % WHEELSIZE) {
			CIRCQ_CONCAT(&timeout_todo, &timeout_wheel_kc[off + b]);
			if (b == last)
				break;
		}
		if (done)
			break;
	}

	/*
	 * Update the cached state for each kclock.
	 */
	for (i = 0; i < nitems(timeout_kclock); i++) {
		kc = &timeout_kclock[i];
		timespecadd(&now, &kc->kc_offset, &kc->kc_lastscan);
		timespecsub(&kc->kc_lastscan, &tick_ts, &kc->kc_late);
	}

	if (CIRCQ_EMPTY(&timeout_new) && CIRCQ_EMPTY(&timeout_todo))
		need_softclock = 0;

	mtx_leave(&timeout_mutex);

	if (need_softclock)
		softintr_schedule(softclock_si);
}

void
timeout_run(struct timeout_ctx *tctx, struct timeout *to)
{
	void (*fn)(void *);
	void *arg;
	int needsproc;

	MUTEX_ASSERT_LOCKED(&timeout_mutex);

	CLR(to->to_flags, TIMEOUT_ONQUEUE);
	SET(to->to_flags, TIMEOUT_TRIGGERED);

	fn = to->to_func;
	arg = to->to_arg;
	needsproc = ISSET(to->to_flags, TIMEOUT_PROC);
#if NKCOV > 0
	struct process *kcov_process = to->to_process;
#endif

	tctx->tctx_running = to;
	mtx_leave(&timeout_mutex);
	timeout_sync_enter(needsproc);
#if NKCOV > 0
	kcov_remote_enter(KCOV_REMOTE_COMMON, kcov_process);
#endif
	fn(arg);
#if NKCOV > 0
	kcov_remote_leave(KCOV_REMOTE_COMMON, kcov_process);
#endif
	timeout_sync_leave(needsproc);
	mtx_enter(&timeout_mutex);
	tctx->tctx_running = NULL;
}

void
softclock_process_kclock_timeout(struct timeout *to, int new)
{
	struct kclock *kc = &timeout_kclock[to->to_kclock];

	if (timespeccmp(&to->to_abstime, &kc->kc_lastscan, >)) {
		tostat.tos_scheduled++;
		if (!new)
			tostat.tos_rescheduled++;
		CIRCQ_INSERT_TAIL(&timeout_wheel_kc[timeout_bucket(to)],
		    &to->to_list);
		return;
	}
	if (!new && timespeccmp(&to->to_abstime, &kc->kc_late, <=))
		tostat.tos_late++;
	if (ISSET(to->to_flags, TIMEOUT_PROC)) {
#ifdef MULTIPROCESSOR
		if (ISSET(to->to_flags, TIMEOUT_MPSAFE))
			CIRCQ_INSERT_TAIL(&timeout_proc_mp, &to->to_list);
		else
#endif
			CIRCQ_INSERT_TAIL(&timeout_proc, &to->to_list);
		return;
	}
	timeout_run(&timeout_ctx_si, to);
	tostat.tos_run_softclock++;
}

void
softclock_process_tick_timeout(struct timeout *to, int new)
{
	int delta = to->to_time - ticks;

	if (delta > 0) {
		tostat.tos_scheduled++;
		if (!new)
			tostat.tos_rescheduled++;
		CIRCQ_INSERT_TAIL(&BUCKET(delta, to->to_time), &to->to_list);
		return;
	}
	if (!new && delta < 0)
		tostat.tos_late++;
	if (ISSET(to->to_flags, TIMEOUT_PROC)) {
#ifdef MULTIPROCESSOR
		if (ISSET(to->to_flags, TIMEOUT_MPSAFE))
			CIRCQ_INSERT_TAIL(&timeout_proc_mp, &to->to_list);
		else
#endif
			CIRCQ_INSERT_TAIL(&timeout_proc, &to->to_list);
		return;
	}
	timeout_run(&timeout_ctx_si, to);
	tostat.tos_run_softclock++;
}

/*
 * Timeouts are processed here instead of timeout_hardclock_update()
 * to avoid doing any more work at IPL_CLOCK than absolutely necessary.
 * Down here at IPL_SOFTCLOCK other interrupts can be serviced promptly
 * so the system remains responsive even if there is a surge of timeouts.
 */
void
softclock(void *arg)
{
	struct timeout *first_new, *to;
	int needsproc, new;
#ifdef MULTIPROCESSOR
	int need_proc_mp;
#endif

	first_new = NULL;
	new = 0;

	mtx_enter(&timeout_mutex);
	if (!CIRCQ_EMPTY(&timeout_new))
		first_new = timeout_from_circq(CIRCQ_FIRST(&timeout_new));
	CIRCQ_CONCAT(&timeout_todo, &timeout_new);
	while (!CIRCQ_EMPTY(&timeout_todo)) {
		to = timeout_from_circq(CIRCQ_FIRST(&timeout_todo));
		CIRCQ_REMOVE(&to->to_list);
		if (to == first_new)
			new = 1;
		if (to->to_kclock == KCLOCK_NONE)
			softclock_process_tick_timeout(to, new);
		else if (to->to_kclock == KCLOCK_UPTIME)
			softclock_process_kclock_timeout(to, new);
		else {
			panic("%s: invalid to_clock: %d",
			    __func__, to->to_kclock);
		}
	}
	tostat.tos_softclocks++;
	needsproc = !CIRCQ_EMPTY(&timeout_proc);
#ifdef MULTIPROCESSOR
	need_proc_mp = !CIRCQ_EMPTY(&timeout_proc_mp);
#endif
	mtx_leave(&timeout_mutex);

	if (needsproc)
		wakeup(&timeout_proc);
#ifdef MULTIPROCESSOR
	if (need_proc_mp)
		wakeup(&timeout_proc_mp);
#endif
}

void
softclock_create_thread(void *arg)
{
	if (kthread_create(softclock_thread, NULL, NULL, "softclock"))
		panic("fork softclock");
#ifdef MULTIPROCESSOR
	if (kthread_create(softclock_thread_mp, NULL, NULL, "softclockmp"))
		panic("kthread_create softclock_thread_mp");
#endif
}

static void
softclock_thread_run(struct timeout_ctx *tctx)
{
	struct circq *todo = tctx->tctx_todo;
	struct timeout *to;

	for (;;) {
		/*
		 * Avoid holding both timeout_mutex and SCHED_LOCK
		 * at the same time.
		 */
		sleep_setup(todo, PSWP, "tmoslp");
		sleep_finish(INFSLP, CIRCQ_EMPTY(tctx->tctx_todo));

		mtx_enter(&timeout_mutex);
		tostat.tos_thread_wakeups++;
		while (!CIRCQ_EMPTY(todo)) {
			to = timeout_from_circq(CIRCQ_FIRST(todo));
			CIRCQ_REMOVE(&to->to_list);
			timeout_run(tctx, to);
			tostat.tos_run_thread++;
		}
		mtx_leave(&timeout_mutex);
	}
}

void
softclock_thread(void *arg)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	int s;

	KERNEL_ASSERT_LOCKED();

	/* Be conservative for the moment */
	CPU_INFO_FOREACH(cii, ci) {
		if (CPU_IS_PRIMARY(ci))
			break;
	}
	KASSERT(ci != NULL);
	sched_peg_curproc(ci);

	s = splsoftclock();
	softclock_thread_run(&timeout_ctx_proc);
	splx(s);
}

#ifdef MULTIPROCESSOR
void
softclock_thread_mp(void *arg)
{
	KERNEL_ASSERT_LOCKED();
	KERNEL_UNLOCK();

	softclock_thread_run(&timeout_ctx_proc_mp);
}
#endif /* MULTIPROCESSOR */

#ifndef SMALL_KERNEL
void
timeout_adjust_ticks(int adj)
{
	struct timeout *to;
	struct circq *p;
	int new_ticks, b;

	/* adjusting the monotonic clock backwards would be a Bad Thing */
	if (adj <= 0)
		return;

	mtx_enter(&timeout_mutex);
	new_ticks = ticks + adj;
	for (b = 0; b < nitems(timeout_wheel); b++) {
		p = CIRCQ_FIRST(&timeout_wheel[b]);
		while (p != &timeout_wheel[b]) {
			to = timeout_from_circq(p);
			p = CIRCQ_FIRST(p);

			/* when moving a timeout forward need to reinsert it */
			if (to->to_time - ticks < adj)
				to->to_time = new_ticks;
			CIRCQ_REMOVE(&to->to_list);
			CIRCQ_INSERT_TAIL(&timeout_todo, &to->to_list);
		}
	}
	ticks = new_ticks;
	mtx_leave(&timeout_mutex);
}
#endif

#ifndef SMALL_KERNEL
int
timeout_sysctl(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	struct timeoutstat status;

	mtx_enter(&timeout_mutex);
	memcpy(&status, &tostat, sizeof(status));
	mtx_leave(&timeout_mutex);

	return sysctl_rdstruct(oldp, oldlenp, newp, &status, sizeof(status));
}
#endif /* SMALL_KERNEL */

#ifdef DDB
const char *db_kclock(int);
void db_show_callout_bucket(struct circq *);
void db_show_timeout(struct timeout *, struct circq *);
const char *db_timespec(const struct timespec *);

const char *
db_kclock(int kclock)
{
	switch (kclock) {
	case KCLOCK_UPTIME:
		return "uptime";
	default:
		return "invalid";
	}
}

const char *
db_timespec(const struct timespec *ts)
{
	static char buf[32];
	struct timespec tmp, zero;

	if (ts->tv_sec >= 0) {
		snprintf(buf, sizeof(buf), "%lld.%09ld",
		    ts->tv_sec, ts->tv_nsec);
		return buf;
	}

	timespecclear(&zero);
	timespecsub(&zero, ts, &tmp);
	snprintf(buf, sizeof(buf), "-%lld.%09ld", tmp.tv_sec, tmp.tv_nsec);
	return buf;
}

void
db_show_callout_bucket(struct circq *bucket)
{
	struct circq *p;

	CIRCQ_FOREACH(p, bucket)
		db_show_timeout(timeout_from_circq(p), bucket);
}

void
db_show_timeout(struct timeout *to, struct circq *bucket)
{
	struct timespec remaining;
	struct kclock *kc;
	char buf[8];
	db_expr_t offset;
	struct circq *wheel;
	const char *name, *where;
	int width = sizeof(long) * 2;

	db_find_sym_and_offset((vaddr_t)to->to_func, &name, &offset);
	name = name ? name : "?";
	if (bucket == &timeout_new)
		where = "new";
	else if (bucket == &timeout_todo)
		where = "softint";
	else if (bucket == &timeout_proc)
		where = "thread";
#ifdef MULTIPROCESSOR
	else if (bucket == &timeout_proc_mp)
		where = "thread-mp";
#endif
	else {
		if (to->to_kclock == KCLOCK_UPTIME)
			wheel = timeout_wheel_kc;
		else if (to->to_kclock == KCLOCK_NONE)
			wheel = timeout_wheel;
		else
			goto invalid;
		snprintf(buf, sizeof(buf), "%3ld/%1ld",
		    (bucket - wheel) % WHEELSIZE,
		    (bucket - wheel) / WHEELSIZE);
		where = buf;
	}
	if (to->to_kclock == KCLOCK_UPTIME) {
		kc = &timeout_kclock[to->to_kclock];
		timespecsub(&to->to_abstime, &kc->kc_lastscan, &remaining);
		db_printf("%20s  %8s  %9s  0x%0*lx  %s\n",
		    db_timespec(&remaining), db_kclock(to->to_kclock), where,
		    width, (ulong)to->to_arg, name);
	} else if (to->to_kclock == KCLOCK_NONE) {
		db_printf("%20d  %8s  %9s  0x%0*lx  %s\n",
		    to->to_time - ticks, "ticks", where,
		    width, (ulong)to->to_arg, name);
	} else
		goto invalid;
	return;

 invalid:
	db_printf("%s: timeout 0x%p: invalid to_kclock: %d",
	    __func__, to, to->to_kclock);
}

void
db_show_callout(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	struct kclock *kc;
	int width = sizeof(long) * 2 + 2;
	int b, i;

	db_printf("%20s  %8s\n", "lastscan", "clock");
	db_printf("%20d  %8s\n", ticks, "ticks");
	for (i = 0; i < nitems(timeout_kclock); i++) {
		kc = &timeout_kclock[i];
		db_printf("%20s  %8s\n",
		    db_timespec(&kc->kc_lastscan), db_kclock(i));
	}
	db_printf("\n");
	db_printf("%20s  %8s  %9s  %*s  %s\n",
	    "remaining", "clock", "wheel", width, "arg", "func");
	db_show_callout_bucket(&timeout_new);
	db_show_callout_bucket(&timeout_todo);
	db_show_callout_bucket(&timeout_proc);
#ifdef MULTIPROCESSOR
	db_show_callout_bucket(&timeout_proc_mp);
#endif
	for (b = 0; b < nitems(timeout_wheel); b++)
		db_show_callout_bucket(&timeout_wheel[b]);
	for (b = 0; b < nitems(timeout_wheel_kc); b++)
		db_show_callout_bucket(&timeout_wheel_kc[b]);
}
#endif
