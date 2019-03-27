/*-
 * Copyright (c) 2015 Netflix, Inc.
 *
 * Derived from gs_rr.c:
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
 * A simple scheduler that just delays certain transactions by a certain
 * amount. We collect all the transactions that are 'done' and put them on
 * a queue. The queue is run through every so often and the transactions that
 * have taken longer than the threshold delay are completed.
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

/* Useful constants */
#define BTFRAC_1US 18446744073709ULL	/* 2^64 / 1000000 */

/* list of scheduler instances */
LIST_HEAD(g_scheds, g_delay_softc);

/*
 * Per device descriptor, holding the Round Robin list of queues
 * accessing the disk, a reference to the geom, and the timer.
 */
struct g_delay_softc {
	struct g_geom	*sc_geom;

	struct bio_queue_head sc_bioq;	/* queue of pending requests */
	struct callout	sc_wait;	/* timer for completing with delays */

	/* Statistics */
	int		sc_in_flight;	/* requests in the driver */
};

/*
 * parameters, config and stats
 */
struct g_delay_params {
	uint64_t io;
	int	bypass;			/* bypass scheduling */
	int	units;			/* how many instances */
	int	latency;		/* How big a latncy are hoping for */
};

static struct g_delay_params me = {
	.bypass = 0,
	.units = 0,
	.latency = 0,
	.io = 0,
};
struct g_delay_params *gs_delay_me = &me;

SYSCTL_DECL(_kern_geom_sched);
static SYSCTL_NODE(_kern_geom_sched, OID_AUTO, delay, CTLFLAG_RW, 0,
    "GEOM_SCHED DELAY stuff");
SYSCTL_INT(_kern_geom_sched_delay, OID_AUTO, bypass, CTLFLAG_RD,
    &me.bypass, 0, "Scheduler bypass");
SYSCTL_INT(_kern_geom_sched_delay, OID_AUTO, units, CTLFLAG_RD,
    &me.units, 0, "Scheduler instances");
SYSCTL_INT(_kern_geom_sched_delay, OID_AUTO, latency, CTLFLAG_RW,
    &me.latency, 0, "Minimum latency for requests, in microseconds (1/hz resolution)");
SYSCTL_QUAD(_kern_geom_sched_delay, OID_AUTO, io, CTLFLAG_RW,
    &me.io, 0, "I/Os delayed\n");

static int
g_delay_init_class(void *data, void *priv)
{
	return (0);
}

static void
g_delay_fini_class(void *data, void *priv)
{
}

/*
 * Called on a request arrival, timeout or completion.
 * Try to serve a request among those queued.
 */
static struct bio *
g_delay_next(void *data, int force)
{
	struct g_delay_softc *sc = data;
	struct bio *bp;
	struct bintime bt;

	bp = bioq_first(&sc->sc_bioq);
	if (bp == NULL)
		return (NULL);

	/*
	 * If the time isn't yet ripe for this bp to be let loose,
	 * then the time isn't ripe for any of its friends either
	 * since we insert in-order. Terminate if the bio hasn't
	 * aged appropriately. Note that there's pathology here
	 * such that we may be up to one tick early in releasing
	 * this I/O. We could implement this up to a tick late too
	 * but choose not to.
	 */
	getbinuptime(&bt);	/* BIO's bio_t0 is uptime */
	if (bintime_cmp(&bp->bio_t0, &bt, >))
		return (NULL);
	me.io++;
	
	/*
	 * The bp has mellowed enough, let it through and update stats.
	 * If there's others, we'll catch them next time we get called.
	 */
	sc->sc_in_flight++;

	bp = bioq_takefirst(&sc->sc_bioq);
	return (bp);
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
g_delay_start(void *data, struct bio *bp)
{
	struct g_delay_softc *sc = data;

	if (me.bypass)
		return (-1);	/* bypass the scheduler */

	bp->bio_caller1 = sc;
	getbinuptime(&bp->bio_t0);	/* BIO's bio_t0 is uptime */
	bintime_addx(&bp->bio_t0, BTFRAC_1US * me.latency);

	/*
	 * Keep the I/Os ordered. Lower layers will reorder as we release them down.
	 * We rely on this in g_delay_next() so that we delay all things equally. Even
	 * if we move to multiple queues to push stuff down the stack, we'll want to
	 * insert in order and let the lower layers do whatever reordering they want.
	 */
	bioq_insert_tail(&sc->sc_bioq, bp);

	return (0);
}

static void
g_delay_timeout(void *data)
{
	struct g_delay_softc *sc = data;
	
	g_sched_lock(sc->sc_geom);
	g_sched_dispatch(sc->sc_geom);
	g_sched_unlock(sc->sc_geom);
	callout_reset(&sc->sc_wait, 1, g_delay_timeout, sc);
}

/*
 * Module glue: allocate descriptor, initialize its fields.
 */
static void *
g_delay_init(struct g_geom *geom)
{
	struct g_delay_softc *sc;

	sc = malloc(sizeof *sc, M_GEOM_SCHED, M_WAITOK | M_ZERO);
	sc->sc_geom = geom;
	bioq_init(&sc->sc_bioq);
	callout_init(&sc->sc_wait, CALLOUT_MPSAFE);
	callout_reset(&sc->sc_wait, 1, g_delay_timeout, sc);
	me.units++;

	return (sc);
}

/*
 * Module glue -- drain the callout structure, destroy the
 * hash table and its element, and free the descriptor.
 */
static void
g_delay_fini(void *data)
{
	struct g_delay_softc *sc = data;

	/* We're force drained before getting here */

	/* Kick out timers */
	callout_drain(&sc->sc_wait);
	me.units--;
	free(sc, M_GEOM_SCHED);
}

/*
 * Called when the request under service terminates.
 * Start the anticipation timer if needed.
 */
static void
g_delay_done(void *data, struct bio *bp)
{
	struct g_delay_softc *sc = data;

	sc->sc_in_flight--;

	g_sched_dispatch(sc->sc_geom);
}

static void
g_delay_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
}

static struct g_gsched g_delay = {
	.gs_name = "delay",
	.gs_priv_size = 0,
	.gs_init = g_delay_init,
	.gs_fini = g_delay_fini,
	.gs_start = g_delay_start,
	.gs_done = g_delay_done,
	.gs_next = g_delay_next,
	.gs_dumpconf = g_delay_dumpconf,
	.gs_init_class = g_delay_init_class,
	.gs_fini_class = g_delay_fini_class,
};

DECLARE_GSCHED_MODULE(delay, &g_delay);
