/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 Fabio Checconi
 * Copyright (c) 2009-2010 Luigi Rizzo, Universita` di Pisa
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
 */

/*
 * $Id$
 * $FreeBSD$
 *
 * A round-robin (RR) anticipatory scheduler, with per-client queues.
 *
 * The goal of this implementation is to improve throughput compared
 * to the pure elevator algorithm, and insure some fairness among
 * clients.
 * 
 * Requests coming from the same client are put in the same queue.
 * We use anticipation to help reducing seeks, and each queue
 * is never served continuously for more than a given amount of
 * time or data. Queues are then served in a round-robin fashion.
 *
 * Each queue can be in any of the following states:
 *     READY	immediately serve the first pending request;
 *     BUSY	one request is under service, wait for completion;
 *     IDLING	do not serve incoming requests immediately, unless
 * 		they are "eligible" as defined later.
 *
 * Scheduling is made looking at the status of all queues,
 * and the first one in round-robin order is privileged.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include "gs_scheduler.h"

/* possible states of the scheduler */
enum g_rr_state {
	G_QUEUE_READY = 0,	/* Ready to dispatch. */
	G_QUEUE_BUSY,		/* Waiting for a completion. */
	G_QUEUE_IDLING		/* Waiting for a new request. */
};

/* possible queue flags */
enum g_rr_flags {
	/* G_FLAG_COMPLETED means that the field q_slice_end is valid. */
	G_FLAG_COMPLETED = 1,	/* Completed a req. in the current budget. */
};

struct g_rr_softc;

/*
 * Queue descriptor, containing reference count, scheduling
 * state, a queue of pending requests, configuration parameters.
 * Queues with pending request(s) and not under service are also
 * stored in a Round Robin (RR) list.
 */
struct g_rr_queue {
	struct g_rr_softc *q_sc;	/* link to the parent */

	enum g_rr_state	q_status;
	unsigned int	q_service;	/* service received so far */
	int		q_slice_end;	/* actual slice end time, in ticks */
	enum g_rr_flags	q_flags;	/* queue flags */
	struct bio_queue_head q_bioq;

	/* Scheduling parameters */
	unsigned int	q_budget;	/* slice size in bytes */
	unsigned int	q_slice_duration; /* slice size in ticks */
	unsigned int	q_wait_ticks;	/* wait time for anticipation */

	/* Stats to drive the various heuristics. */
	struct g_savg	q_thinktime;	/* Thinktime average. */
	struct g_savg	q_seekdist;	/* Seek distance average. */

	int		q_bionum;	/* Number of requests. */

	off_t		q_lastoff;	/* Last submitted req. offset. */
	int		q_lastsub;	/* Last submitted req. time. */

	/* Expiration deadline for an empty queue. */
	int		q_expire;

	TAILQ_ENTRY(g_rr_queue) q_tailq; /* RR list link field */
};

/* List types. */
TAILQ_HEAD(g_rr_tailq, g_rr_queue);

/* list of scheduler instances */
LIST_HEAD(g_scheds, g_rr_softc);

/* Default quantum for RR between queues. */
#define	G_RR_DEFAULT_BUDGET	0x00800000

/*
 * Per device descriptor, holding the Round Robin list of queues
 * accessing the disk, a reference to the geom, and the timer.
 */
struct g_rr_softc {
	struct g_geom	*sc_geom;

	/*
	 * sc_active is the queue we are anticipating for.
	 * It is set only in gs_rr_next(), and possibly cleared
	 * only in gs_rr_next() or on a timeout.
	 * The active queue is never in the Round Robin list
	 * even if it has requests queued.
	 */
	struct g_rr_queue *sc_active;
	struct callout	sc_wait;	/* timer for sc_active */

	struct g_rr_tailq sc_rr_tailq;	/* the round-robin list */
	int		sc_nqueues;	/* number of queues */

	/* Statistics */
	int		sc_in_flight;	/* requests in the driver */

	LIST_ENTRY(g_rr_softc)	sc_next;
};

/* Descriptor for bounded values, min and max are constant. */
struct x_bound {		
	const int	x_min;
	int		x_cur;
	const int	x_max;
};

/*
 * parameters, config and stats
 */
struct g_rr_params {
	int	queues;			/* total number of queues */
	int	w_anticipate;		/* anticipate writes */
	int	bypass;			/* bypass scheduling writes */

	int	units;			/* how many instances */
	/* sc_head is used for debugging */
	struct g_scheds	sc_head;	/* first scheduler instance */

	struct x_bound queue_depth;	/* max parallel requests */
	struct x_bound wait_ms;		/* wait time, milliseconds */
	struct x_bound quantum_ms;	/* quantum size, milliseconds */
	struct x_bound quantum_kb;	/* quantum size, Kb (1024 bytes) */

	/* statistics */
	int	wait_hit;		/* success in anticipation */
	int	wait_miss;		/* failure in anticipation */
};

/*
 * Default parameters for the scheduler.  The quantum sizes target
 * a 80MB/s disk; if the hw is faster or slower the minimum of the
 * two will have effect: the clients will still be isolated but
 * the fairness may be limited.  A complete solution would involve
 * the on-line measurement of the actual disk throughput to derive
 * these parameters.  Or we may just choose to ignore service domain
 * fairness and accept what can be achieved with time-only budgets.
 */
static struct g_rr_params me = {
	.sc_head = LIST_HEAD_INITIALIZER(&me.sc_head),
	.w_anticipate =	1,
	.queue_depth =	{ 1,	1,	50 },
	.wait_ms =	{ 1, 	10,	30 },
	.quantum_ms =	{ 1, 	100,	500 },
	.quantum_kb =	{ 16, 	8192,	65536 },
};

struct g_rr_params *gs_rr_me = &me;

SYSCTL_DECL(_kern_geom_sched);
static SYSCTL_NODE(_kern_geom_sched, OID_AUTO, rr, CTLFLAG_RW, 0,
    "GEOM_SCHED ROUND ROBIN stuff");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, units, CTLFLAG_RD,
    &me.units, 0, "Scheduler instances");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, queues, CTLFLAG_RD,
    &me.queues, 0, "Total rr queues");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, wait_ms, CTLFLAG_RW,
    &me.wait_ms.x_cur, 0, "Wait time milliseconds");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, quantum_ms, CTLFLAG_RW,
    &me.quantum_ms.x_cur, 0, "Quantum size milliseconds");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, bypass, CTLFLAG_RW,
    &me.bypass, 0, "Bypass scheduler");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, w_anticipate, CTLFLAG_RW,
    &me.w_anticipate, 0, "Do anticipation on writes");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, quantum_kb, CTLFLAG_RW,
    &me.quantum_kb.x_cur, 0, "Quantum size Kbytes");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, queue_depth, CTLFLAG_RW,
    &me.queue_depth.x_cur, 0, "Maximum simultaneous requests");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, wait_hit, CTLFLAG_RW,
    &me.wait_hit, 0, "Hits in anticipation");
SYSCTL_INT(_kern_geom_sched_rr, OID_AUTO, wait_miss, CTLFLAG_RW,
    &me.wait_miss, 0, "Misses in anticipation");

#ifdef DEBUG_QUEUES
/* print the status of a queue */
static void
gs_rr_dump_q(struct g_rr_queue *qp, int index)
{
	int l = 0;
	struct bio *bp;

	TAILQ_FOREACH(bp, &(qp->q_bioq.queue), bio_queue) {
		l++;
	}
	printf("--- rr queue %d %p status %d len %d ---\n",
	    index, qp, qp->q_status, l);
}

/*
 * Dump the scheduler status when writing to this sysctl variable.
 * XXX right now we only dump the status of the last instance created.
 * not a severe issue because this is only for debugging
 */
static int
gs_rr_sysctl_status(SYSCTL_HANDLER_ARGS)
{
        int error, val = 0;
	struct g_rr_softc *sc;

        error = sysctl_handle_int(oidp, &val, 0, req);
        if (error || !req->newptr )
                return (error);

        printf("called %s\n", __FUNCTION__);

	LIST_FOREACH(sc, &me.sc_head, sc_next) {
		int i, tot = 0;
		printf("--- sc %p active %p nqueues %d "
		    "callout %d in_flight %d ---\n",
		    sc, sc->sc_active, sc->sc_nqueues,
		    callout_active(&sc->sc_wait),
		    sc->sc_in_flight);
		for (i = 0; i < G_RR_HASH_SIZE; i++) {
			struct g_rr_queue *qp;
			LIST_FOREACH(qp, &sc->sc_hash[i], q_hash) {
				gs_rr_dump_q(qp, tot);
				tot++;
			}
		}
	}
        return (0);
}

SYSCTL_PROC(_kern_geom_sched_rr, OID_AUTO, status,
	CTLTYPE_UINT | CTLFLAG_RW,
    0, sizeof(int), gs_rr_sysctl_status, "I", "status");

#endif	/* DEBUG_QUEUES */

/*
 * Get a bounded value, optionally convert to a min of t_min ticks.
 */
static int
get_bounded(struct x_bound *v, int t_min)
{
	int x;

	x = v->x_cur;
	if (x < v->x_min)
		x = v->x_min;
	else if (x > v->x_max)
		x = v->x_max;
	if (t_min) {
		x = x * hz / 1000;	/* convert to ticks */
		if (x < t_min)
			x = t_min;
	}
	return x;
}

/*
 * Get a reference to the queue for bp, using the generic
 * classification mechanism.
 */
static struct g_rr_queue *
g_rr_queue_get(struct g_rr_softc *sc, struct bio *bp)
{

	return (g_sched_get_class(sc->sc_geom, bp));
}

static int
g_rr_init_class(void *data, void *priv)
{
	struct g_rr_softc *sc = data;
	struct g_rr_queue *qp = priv;

	bioq_init(&qp->q_bioq);

	/*
	 * Set the initial parameters for the client:
	 * slice size in bytes and ticks, and wait ticks.
	 * Right now these are constant, but we could have
	 * autoconfiguration code to adjust the values based on
	 * the actual workload.
	 */
	qp->q_budget = 1024 * get_bounded(&me.quantum_kb, 0);
	qp->q_slice_duration = get_bounded(&me.quantum_ms, 2);
	qp->q_wait_ticks = get_bounded(&me.wait_ms, 2);

	qp->q_sc = sc;		/* link to the parent */
	qp->q_sc->sc_nqueues++;
	me.queues++;

	return (0);
}

/*
 * Release a reference to the queue.
 */
static void
g_rr_queue_put(struct g_rr_queue *qp)
{

	g_sched_put_class(qp->q_sc->sc_geom, qp);
}

static void
g_rr_fini_class(void *data, void *priv)
{
	struct g_rr_queue *qp = priv;

	KASSERT(bioq_first(&qp->q_bioq) == NULL,
			("released nonempty queue"));
	qp->q_sc->sc_nqueues--;
	me.queues--;
}

static inline int
g_rr_queue_expired(struct g_rr_queue *qp)
{

	if (qp->q_service >= qp->q_budget)
		return (1);

	if ((qp->q_flags & G_FLAG_COMPLETED) &&
	    ticks - qp->q_slice_end >= 0)
		return (1);

	return (0);
}

static inline int
g_rr_should_anticipate(struct g_rr_queue *qp, struct bio *bp)
{
	int wait = get_bounded(&me.wait_ms, 2);

	if (!me.w_anticipate && (bp->bio_cmd == BIO_WRITE))
		return (0);

	if (g_savg_valid(&qp->q_thinktime) &&
	    g_savg_read(&qp->q_thinktime) > wait)
		return (0);

	if (g_savg_valid(&qp->q_seekdist) &&
	    g_savg_read(&qp->q_seekdist) > 8192)
		return (0);

	return (1);
}

/*
 * Called on a request arrival, timeout or completion.
 * Try to serve a request among those queued.
 */
static struct bio *
g_rr_next(void *data, int force)
{
	struct g_rr_softc *sc = data;
	struct g_rr_queue *qp;
	struct bio *bp, *next;
	int expired;

	qp = sc->sc_active;
	if (me.bypass == 0 && !force) {
		if (sc->sc_in_flight >= get_bounded(&me.queue_depth, 0))
			return (NULL);

		/* Try with the queue under service first. */
		if (qp != NULL && qp->q_status != G_QUEUE_READY) {
			/*
			 * Queue is anticipating, ignore request.
			 * We should check that we are not past
			 * the timeout, but in that case the timeout
			 * will fire immediately afterwards so we
			 * don't bother.
			 */
			return (NULL);
		}
	} else if (qp != NULL && qp->q_status != G_QUEUE_READY) {
		g_rr_queue_put(qp);
		sc->sc_active = qp = NULL;
	}

	/*
	 * No queue under service, look for the first in RR order.
	 * If we find it, select if as sc_active, clear service
	 * and record the end time of the slice.
	 */
	if (qp == NULL) {
		qp = TAILQ_FIRST(&sc->sc_rr_tailq);
		if (qp == NULL)
			return (NULL); /* no queues at all, return */
		/* otherwise select the new queue for service. */
		TAILQ_REMOVE(&sc->sc_rr_tailq, qp, q_tailq);
		sc->sc_active = qp;
		qp->q_service = 0;
		qp->q_flags &= ~G_FLAG_COMPLETED;
	}

	bp = bioq_takefirst(&qp->q_bioq);	/* surely not NULL */
	qp->q_service += bp->bio_length;	/* charge the service */

	/*
	 * The request at the head of the active queue is always
	 * dispatched, and gs_rr_next() will be called again
	 * immediately.
	 * We need to prepare for what to do next:
	 *
	 * 1. have we reached the end of the (time or service) slice ?
	 *    If so, clear sc_active and possibly requeue the previous
	 *    active queue if it has more requests pending;
	 * 2. do we have more requests in sc_active ?
	 *    If yes, do not anticipate, as gs_rr_next() will run again;
	 *    if no, decide whether or not to anticipate depending
	 *    on read or writes (e.g., anticipate only on reads).
	 */
	expired = g_rr_queue_expired(qp);	/* are we expired ? */
	next = bioq_first(&qp->q_bioq);	/* do we have one more ? */
 	if (expired) {
		sc->sc_active = NULL;
		/* Either requeue or release reference. */
		if (next != NULL)
			TAILQ_INSERT_TAIL(&sc->sc_rr_tailq, qp, q_tailq);
		else
			g_rr_queue_put(qp);
	} else if (next != NULL) {
		qp->q_status = G_QUEUE_READY;
	} else {
		if (!force && g_rr_should_anticipate(qp, bp)) {
			/* anticipate */
			qp->q_status = G_QUEUE_BUSY;
		} else {
			/* do not anticipate, release reference */
			g_rr_queue_put(qp);
			sc->sc_active = NULL;
		}
	}
	/* If sc_active != NULL, its q_status is always correct. */

	sc->sc_in_flight++;

	return (bp);
}

static inline void
g_rr_update_thinktime(struct g_rr_queue *qp)
{
	int delta = ticks - qp->q_lastsub, wait = get_bounded(&me.wait_ms, 2);

	if (qp->q_sc->sc_active != qp)
		return;

	qp->q_lastsub = ticks;
	delta = (delta > 2 * wait) ? 2 * wait : delta;
	if (qp->q_bionum > 7)
		g_savg_add_sample(&qp->q_thinktime, delta);
}

static inline void
g_rr_update_seekdist(struct g_rr_queue *qp, struct bio *bp)
{
	off_t dist;

	if (qp->q_lastoff > bp->bio_offset)
		dist = qp->q_lastoff - bp->bio_offset;
	else
		dist = bp->bio_offset - qp->q_lastoff;

	if (dist > (8192 * 8))
		dist = 8192 * 8;

	qp->q_lastoff = bp->bio_offset + bp->bio_length;

	if (qp->q_bionum > 7)
		g_savg_add_sample(&qp->q_seekdist, dist);
}

/*
 * Called when a real request for disk I/O arrives.
 * Locate the queue associated with the client.
 * If the queue is the one we are anticipating for, reset its timeout;
 * if the queue is not in the round robin list, insert it in the list.
 * On any error, do not queue the request and return -1, the caller
 * will take care of this request.
 */
static int
g_rr_start(void *data, struct bio *bp)
{
	struct g_rr_softc *sc = data;
	struct g_rr_queue *qp;

	if (me.bypass)
		return (-1);	/* bypass the scheduler */

	/* Get the queue for the request. */
	qp = g_rr_queue_get(sc, bp);
	if (qp == NULL)
		return (-1); /* allocation failed, tell upstream */

	if (bioq_first(&qp->q_bioq) == NULL) {
		/*
		 * We are inserting into an empty queue.
		 * Reset its state if it is sc_active,
		 * otherwise insert it in the RR list.
		 */
		if (qp == sc->sc_active) {
			qp->q_status = G_QUEUE_READY;
			callout_stop(&sc->sc_wait);
		} else {
			g_sched_priv_ref(qp);
			TAILQ_INSERT_TAIL(&sc->sc_rr_tailq, qp, q_tailq);
		}
	}

	qp->q_bionum = 1 + qp->q_bionum - (qp->q_bionum >> 3);

	g_rr_update_thinktime(qp);
	g_rr_update_seekdist(qp, bp);

	/* Inherit the reference returned by g_rr_queue_get(). */
	bp->bio_caller1 = qp;
	bioq_disksort(&qp->q_bioq, bp);

	return (0);
}

/*
 * Callout executed when a queue times out anticipating a new request.
 */
static void
g_rr_wait_timeout(void *data)
{
	struct g_rr_softc *sc = data;
	struct g_geom *geom = sc->sc_geom;

	g_sched_lock(geom);
	/*
	 * We can race with other events, so check if
	 * sc_active is still valid.
	 */
	if (sc->sc_active != NULL) {
		/* Release the reference to the queue. */
		g_rr_queue_put(sc->sc_active);
		sc->sc_active = NULL;
		me.wait_hit--;
		me.wait_miss++;	/* record the miss */
	}
	g_sched_dispatch(geom);
	g_sched_unlock(geom);
}

/*
 * Module glue: allocate descriptor, initialize its fields.
 */
static void *
g_rr_init(struct g_geom *geom)
{
	struct g_rr_softc *sc;

	/* XXX check whether we can sleep */
	sc = malloc(sizeof *sc, M_GEOM_SCHED, M_NOWAIT | M_ZERO);
	sc->sc_geom = geom;
	TAILQ_INIT(&sc->sc_rr_tailq);
	callout_init(&sc->sc_wait, 1);
	LIST_INSERT_HEAD(&me.sc_head, sc, sc_next);
	me.units++;

	return (sc);
}

/*
 * Module glue -- drain the callout structure, destroy the
 * hash table and its element, and free the descriptor.
 */
static void
g_rr_fini(void *data)
{
	struct g_rr_softc *sc = data;

	callout_drain(&sc->sc_wait);
	KASSERT(sc->sc_active == NULL, ("still a queue under service"));
	KASSERT(TAILQ_EMPTY(&sc->sc_rr_tailq), ("still scheduled queues"));

	LIST_REMOVE(sc, sc_next);
	me.units--;
	free(sc, M_GEOM_SCHED);
}

/*
 * Called when the request under service terminates.
 * Start the anticipation timer if needed.
 */
static void
g_rr_done(void *data, struct bio *bp)
{
	struct g_rr_softc *sc = data;
	struct g_rr_queue *qp;

	sc->sc_in_flight--;

	qp = bp->bio_caller1;

	/*
	 * When the first request for this queue completes, update the
	 * duration and end of the slice. We do not do it when the
	 * slice starts to avoid charging to the queue the time for
	 * the first seek.
	 */
	if (!(qp->q_flags & G_FLAG_COMPLETED)) {
		qp->q_flags |= G_FLAG_COMPLETED;
		/*
		 * recompute the slice duration, in case we want
		 * to make it adaptive. This is not used right now.
		 * XXX should we do the same for q_quantum and q_wait_ticks ?
		 */
		qp->q_slice_duration = get_bounded(&me.quantum_ms, 2);
		qp->q_slice_end = ticks + qp->q_slice_duration;
	}

	if (qp == sc->sc_active && qp->q_status == G_QUEUE_BUSY) {
		/* The queue is trying anticipation, start the timer. */
		qp->q_status = G_QUEUE_IDLING;
		/* may make this adaptive */
		qp->q_wait_ticks = get_bounded(&me.wait_ms, 2);
		me.wait_hit++;
		callout_reset(&sc->sc_wait, qp->q_wait_ticks,
		    g_rr_wait_timeout, sc);
	} else
		g_sched_dispatch(sc->sc_geom);

	/* Release a reference to the queue. */
	g_rr_queue_put(qp);
}

static void
g_rr_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	if (indent == NULL) {   /* plaintext */
		sbuf_printf(sb, " units %d queues %d",
			me.units, me.queues);
        }
}

static struct g_gsched g_rr = {
	.gs_name = "rr",
	.gs_priv_size = sizeof(struct g_rr_queue),
	.gs_init = g_rr_init,
	.gs_fini = g_rr_fini,
	.gs_start = g_rr_start,
	.gs_done = g_rr_done,
	.gs_next = g_rr_next,
	.gs_dumpconf = g_rr_dumpconf,
	.gs_init_class = g_rr_init_class,
	.gs_fini_class = g_rr_fini_class,
};

DECLARE_GSCHED_MODULE(rr, &g_rr);
