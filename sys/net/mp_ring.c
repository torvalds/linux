/*-
 * Copyright (c) 2014 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <machine/cpu.h>

#if defined(__i386__)
#define atomic_cmpset_acq_64 atomic_cmpset_64
#define atomic_cmpset_rel_64 atomic_cmpset_64
#endif

#include <net/mp_ring.h>

union ring_state {
	struct {
		uint16_t pidx_head;
		uint16_t pidx_tail;
		uint16_t cidx;
		uint16_t flags;
	};
	uint64_t state;
};

enum {
	IDLE = 0,	/* consumer ran to completion, nothing more to do. */
	BUSY,		/* consumer is running already, or will be shortly. */
	STALLED,	/* consumer stopped due to lack of resources. */
	ABDICATED,	/* consumer stopped even though there was work to be
			   done because it wants another thread to take over. */
};

static inline uint16_t
space_available(struct ifmp_ring *r, union ring_state s)
{
	uint16_t x = r->size - 1;

	if (s.cidx == s.pidx_head)
		return (x);
	else if (s.cidx > s.pidx_head)
		return (s.cidx - s.pidx_head - 1);
	else
		return (x - s.pidx_head + s.cidx);
}

static inline uint16_t
increment_idx(struct ifmp_ring *r, uint16_t idx, uint16_t n)
{
	int x = r->size - idx;

	MPASS(x > 0);
	return (x > n ? idx + n : n - x);
}

/* Consumer is about to update the ring's state to s */
static inline uint16_t
state_to_flags(union ring_state s, int abdicate)
{

	if (s.cidx == s.pidx_tail)
		return (IDLE);
	else if (abdicate && s.pidx_tail != s.pidx_head)
		return (ABDICATED);

	return (BUSY);
}

#ifdef MP_RING_NO_64BIT_ATOMICS
static void
drain_ring_locked(struct ifmp_ring *r, union ring_state os, uint16_t prev, int budget)
{
	union ring_state ns;
	int n, pending, total;
	uint16_t cidx = os.cidx;
	uint16_t pidx = os.pidx_tail;

	MPASS(os.flags == BUSY);
	MPASS(cidx != pidx);

	if (prev == IDLE)
		counter_u64_add(r->starts, 1);
	pending = 0;
	total = 0;

	while (cidx != pidx) {

		/* Items from cidx to pidx are available for consumption. */
		n = r->drain(r, cidx, pidx);
		if (n == 0) {
			os.state = ns.state = r->state;
			ns.cidx = cidx;
			ns.flags = STALLED;
			r->state = ns.state;
			if (prev != STALLED)
				counter_u64_add(r->stalls, 1);
			else if (total > 0) {
				counter_u64_add(r->restarts, 1);
				counter_u64_add(r->stalls, 1);
			}
			break;
		}
		cidx = increment_idx(r, cidx, n);
		pending += n;
		total += n;

		/*
		 * We update the cidx only if we've caught up with the pidx, the
		 * real cidx is getting too far ahead of the one visible to
		 * everyone else, or we have exceeded our budget.
		 */
		if (cidx != pidx && pending < 64 && total < budget)
			continue;

		os.state = ns.state = r->state;
		ns.cidx = cidx;
		ns.flags = state_to_flags(ns, total >= budget);
		r->state = ns.state;

		if (ns.flags == ABDICATED)
			counter_u64_add(r->abdications, 1);
		if (ns.flags != BUSY) {
			/* Wrong loop exit if we're going to stall. */
			MPASS(ns.flags != STALLED);
			if (prev == STALLED) {
				MPASS(total > 0);
				counter_u64_add(r->restarts, 1);
			}
			break;
		}

		/*
		 * The acquire style atomic above guarantees visibility of items
		 * associated with any pidx change that we notice here.
		 */
		pidx = ns.pidx_tail;
		pending = 0;
	}
}
#else
/*
 * Caller passes in a state, with a guarantee that there is work to do and that
 * all items up to the pidx_tail in the state are visible.
 */
static void
drain_ring_lockless(struct ifmp_ring *r, union ring_state os, uint16_t prev, int budget)
{
	union ring_state ns;
	int n, pending, total;
	uint16_t cidx = os.cidx;
	uint16_t pidx = os.pidx_tail;

	MPASS(os.flags == BUSY);
	MPASS(cidx != pidx);

	if (prev == IDLE)
		counter_u64_add(r->starts, 1);
	pending = 0;
	total = 0;

	while (cidx != pidx) {

		/* Items from cidx to pidx are available for consumption. */
		n = r->drain(r, cidx, pidx);
		if (n == 0) {
			critical_enter();
			do {
				os.state = ns.state = r->state;
				ns.cidx = cidx;
				ns.flags = STALLED;
			} while (atomic_cmpset_64(&r->state, os.state,
			    ns.state) == 0);
			critical_exit();
			if (prev != STALLED)
				counter_u64_add(r->stalls, 1);
			else if (total > 0) {
				counter_u64_add(r->restarts, 1);
				counter_u64_add(r->stalls, 1);
			}
			break;
		}
		cidx = increment_idx(r, cidx, n);
		pending += n;
		total += n;

		/*
		 * We update the cidx only if we've caught up with the pidx, the
		 * real cidx is getting too far ahead of the one visible to
		 * everyone else, or we have exceeded our budget.
		 */
		if (cidx != pidx && pending < 64 && total < budget)
			continue;
		critical_enter();
		do {
			os.state = ns.state = r->state;
			ns.cidx = cidx;
			ns.flags = state_to_flags(ns, total >= budget);
		} while (atomic_cmpset_acq_64(&r->state, os.state, ns.state) == 0);
		critical_exit();

		if (ns.flags == ABDICATED)
			counter_u64_add(r->abdications, 1);
		if (ns.flags != BUSY) {
			/* Wrong loop exit if we're going to stall. */
			MPASS(ns.flags != STALLED);
			if (prev == STALLED) {
				MPASS(total > 0);
				counter_u64_add(r->restarts, 1);
			}
			break;
		}

		/*
		 * The acquire style atomic above guarantees visibility of items
		 * associated with any pidx change that we notice here.
		 */
		pidx = ns.pidx_tail;
		pending = 0;
	}
}
#endif

int
ifmp_ring_alloc(struct ifmp_ring **pr, int size, void *cookie, mp_ring_drain_t drain,
    mp_ring_can_drain_t can_drain, struct malloc_type *mt, int flags)
{
	struct ifmp_ring *r;

	/* All idx are 16b so size can be 65536 at most */
	if (pr == NULL || size < 2 || size > 65536 || drain == NULL ||
	    can_drain == NULL)
		return (EINVAL);
	*pr = NULL;
	flags &= M_NOWAIT | M_WAITOK;
	MPASS(flags != 0);

	r = malloc(__offsetof(struct ifmp_ring, items[size]), mt, flags | M_ZERO);
	if (r == NULL)
		return (ENOMEM);
	r->size = size;
	r->cookie = cookie;
	r->mt = mt;
	r->drain = drain;
	r->can_drain = can_drain;
	r->enqueues = counter_u64_alloc(flags);
	r->drops = counter_u64_alloc(flags);
	r->starts = counter_u64_alloc(flags);
	r->stalls = counter_u64_alloc(flags);
	r->restarts = counter_u64_alloc(flags);
	r->abdications = counter_u64_alloc(flags);
	if (r->enqueues == NULL || r->drops == NULL || r->starts == NULL ||
	    r->stalls == NULL || r->restarts == NULL ||
	    r->abdications == NULL) {
		ifmp_ring_free(r);
		return (ENOMEM);
	}

	*pr = r;
#ifdef MP_RING_NO_64BIT_ATOMICS
	mtx_init(&r->lock, "mp_ring lock", NULL, MTX_DEF);
#endif
	return (0);
}

void
ifmp_ring_free(struct ifmp_ring *r)
{

	if (r == NULL)
		return;

	if (r->enqueues != NULL)
		counter_u64_free(r->enqueues);
	if (r->drops != NULL)
		counter_u64_free(r->drops);
	if (r->starts != NULL)
		counter_u64_free(r->starts);
	if (r->stalls != NULL)
		counter_u64_free(r->stalls);
	if (r->restarts != NULL)
		counter_u64_free(r->restarts);
	if (r->abdications != NULL)
		counter_u64_free(r->abdications);

	free(r, r->mt);
}

/*
 * Enqueue n items and maybe drain the ring for some time.
 *
 * Returns an errno.
 */
#ifdef MP_RING_NO_64BIT_ATOMICS
int
ifmp_ring_enqueue(struct ifmp_ring *r, void **items, int n, int budget, int abdicate)
{
	union ring_state os, ns;
	uint16_t pidx_start, pidx_stop;
	int i;

	MPASS(items != NULL);
	MPASS(n > 0);

	mtx_lock(&r->lock);
	/*
	 * Reserve room for the new items.  Our reservation, if successful, is
	 * from 'pidx_start' to 'pidx_stop'.
	 */
	os.state = r->state;
	if (n >= space_available(r, os)) {
		counter_u64_add(r->drops, n);
		MPASS(os.flags != IDLE);
		mtx_unlock(&r->lock);
		if (os.flags == STALLED)
			ifmp_ring_check_drainage(r, 0);
		return (ENOBUFS);
	}
	ns.state = os.state;
	ns.pidx_head = increment_idx(r, os.pidx_head, n);
	r->state = ns.state;
	pidx_start = os.pidx_head;
	pidx_stop = ns.pidx_head;

	/*
	 * Wait for other producers who got in ahead of us to enqueue their
	 * items, one producer at a time.  It is our turn when the ring's
	 * pidx_tail reaches the beginning of our reservation (pidx_start).
	 */
	while (ns.pidx_tail != pidx_start) {
		cpu_spinwait();
		ns.state = r->state;
	}

	/* Now it is our turn to fill up the area we reserved earlier. */
	i = pidx_start;
	do {
		r->items[i] = *items++;
		if (__predict_false(++i == r->size))
			i = 0;
	} while (i != pidx_stop);

	/*
	 * Update the ring's pidx_tail.  The release style atomic guarantees
	 * that the items are visible to any thread that sees the updated pidx.
	 */
	os.state = ns.state = r->state;
	ns.pidx_tail = pidx_stop;
	if (abdicate) {
		if (os.flags == IDLE)
			ns.flags = ABDICATED;
	}
	else {
		ns.flags = BUSY;
	}
	r->state = ns.state;
	counter_u64_add(r->enqueues, n);

	if (!abdicate) {
		/*
		 * Turn into a consumer if some other thread isn't active as a consumer
		 * already.
		 */
		if (os.flags != BUSY)
			drain_ring_locked(r, ns, os.flags, budget);
	}

	mtx_unlock(&r->lock);
	return (0);
}

#else
int
ifmp_ring_enqueue(struct ifmp_ring *r, void **items, int n, int budget, int abdicate)
{
	union ring_state os, ns;
	uint16_t pidx_start, pidx_stop;
	int i;

	MPASS(items != NULL);
	MPASS(n > 0);

	/*
	 * Reserve room for the new items.  Our reservation, if successful, is
	 * from 'pidx_start' to 'pidx_stop'.
	 */
	for (;;) {
		os.state = r->state;
		if (n >= space_available(r, os)) {
			counter_u64_add(r->drops, n);
			MPASS(os.flags != IDLE);
			if (os.flags == STALLED)
				ifmp_ring_check_drainage(r, 0);
			return (ENOBUFS);
		}
		ns.state = os.state;
		ns.pidx_head = increment_idx(r, os.pidx_head, n);
		critical_enter();
		if (atomic_cmpset_64(&r->state, os.state, ns.state))
			break;
		critical_exit();
		cpu_spinwait();
	}
	pidx_start = os.pidx_head;
	pidx_stop = ns.pidx_head;

	/*
	 * Wait for other producers who got in ahead of us to enqueue their
	 * items, one producer at a time.  It is our turn when the ring's
	 * pidx_tail reaches the beginning of our reservation (pidx_start).
	 */
	while (ns.pidx_tail != pidx_start) {
		cpu_spinwait();
		ns.state = r->state;
	}

	/* Now it is our turn to fill up the area we reserved earlier. */
	i = pidx_start;
	do {
		r->items[i] = *items++;
		if (__predict_false(++i == r->size))
			i = 0;
	} while (i != pidx_stop);

	/*
	 * Update the ring's pidx_tail.  The release style atomic guarantees
	 * that the items are visible to any thread that sees the updated pidx.
	 */
	do {
		os.state = ns.state = r->state;
		ns.pidx_tail = pidx_stop;
		if (abdicate) {
			if (os.flags == IDLE)
				ns.flags = ABDICATED;
		}
		else {
			ns.flags = BUSY;
		}
	} while (atomic_cmpset_rel_64(&r->state, os.state, ns.state) == 0);
	critical_exit();
	counter_u64_add(r->enqueues, n);

	if (!abdicate) {
		/*
		 * Turn into a consumer if some other thread isn't active as a consumer
		 * already.
		 */
		if (os.flags != BUSY)
			drain_ring_lockless(r, ns, os.flags, budget);
	}

	return (0);
}
#endif

void
ifmp_ring_check_drainage(struct ifmp_ring *r, int budget)
{
	union ring_state os, ns;

	os.state = r->state;
	if ((os.flags != STALLED && os.flags != ABDICATED) ||	// Only continue in STALLED and ABDICATED
	    os.pidx_head != os.pidx_tail ||			// Require work to be available
	    (os.flags != ABDICATED && r->can_drain(r) == 0))	// Can either drain, or everyone left
		return;

	MPASS(os.cidx != os.pidx_tail);	/* implied by STALLED */
	ns.state = os.state;
	ns.flags = BUSY;


#ifdef MP_RING_NO_64BIT_ATOMICS
	mtx_lock(&r->lock);
	if (r->state != os.state) {
		mtx_unlock(&r->lock);
		return;
	}
	r->state = ns.state;
	drain_ring_locked(r, ns, os.flags, budget);
	mtx_unlock(&r->lock);
#else
	/*
	 * The acquire style atomic guarantees visibility of items associated
	 * with the pidx that we read here.
	 */
	if (!atomic_cmpset_acq_64(&r->state, os.state, ns.state))
		return;


	drain_ring_lockless(r, ns, os.flags, budget);
#endif
}

void
ifmp_ring_reset_stats(struct ifmp_ring *r)
{

	counter_u64_zero(r->enqueues);
	counter_u64_zero(r->drops);
	counter_u64_zero(r->starts);
	counter_u64_zero(r->stalls);
	counter_u64_zero(r->restarts);
	counter_u64_zero(r->abdications);
}

int
ifmp_ring_is_idle(struct ifmp_ring *r)
{
	union ring_state s;

	s.state = r->state;
	if (s.pidx_head == s.pidx_tail && s.pidx_tail == s.cidx &&
	    s.flags == IDLE)
		return (1);

	return (0);
}

int
ifmp_ring_is_stalled(struct ifmp_ring *r)
{
	union ring_state s;

	s.state = r->state;
	if (s.pidx_head == s.pidx_tail && s.flags == STALLED)
		return (1);

	return (0);
}
