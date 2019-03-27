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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
 * Main control module for geom-based disk schedulers ('sched').
 *
 * USER VIEW
 * A 'sched' node is typically inserted transparently between
 * an existing provider pp and its original geom gp
 *
 *	[pp --> gp  ..]
 *
 * using the command "geom sched insert <provider>" and
 * resulting in the following topology
 *
 *	[pp --> sched_gp --> cp]   [new_pp --> gp ... ]
 *
 * Deletion "geom sched destroy <provider>.sched." restores the
 * original chain. The normal "geom sched create <provide>"
 * is also supported.
 *
 * INTERNALS
 * Internally, the 'sched' uses the following data structures
 *
 *   geom{}         g_sched_softc{}      g_gsched{}
 * +----------+    +---------------+   +-------------+
 * |  softc *-|--->| sc_gsched   *-|-->|  gs_init    |
 * |  ...     |    |               |   |  gs_fini    |
 * |          |    | [ hash table] |   |  gs_start   |
 * +----------+    |               |   |  ...        |
 *                 |               |   +-------------+
 *                 |               |
 *                 |               |     g_*_softc{}
 *                 |               |   +-------------+
 *                 | sc_data     *-|-->|             |
 *                 +---------------+   |  algorithm- |
 *                                     |  specific   |
 *                                     +-------------+
 *
 * A g_sched_softc{} is created with a "geom sched insert" call.
 * In turn this instantiates a specific scheduling algorithm,
 * which sets sc_gsched to point to the algorithm callbacks,
 * and calls gs_init() to create the g_*_softc{} .
 * The other callbacks (gs_start, gs_next, ...) are invoked
 * as needed 
 *
 * g_sched_softc{} is defined in g_sched.h and mostly used here;
 * g_gsched{}, and the gs_callbacks, are documented in gs_scheduler.h;
 * g_*_softc{} is defined/implemented by each algorithm (gs_*.c)
 *
 * DATA MOVING
 * When a bio is received on the provider, it goes to the
 * g_sched_start() which calls gs_start() to initially queue it;
 * then we call g_sched_dispatch() that loops around gs_next()
 * to select zero or more bio's to be sent downstream.
 *
 * g_sched_dispatch() can also be called as a result of a timeout,
 * e.g. when doing anticipation or pacing requests.
 *
 * When a bio comes back, it goes to g_sched_done() which in turn
 * calls gs_done(). The latter does any necessary housekeeping in
 * the scheduling algorithm, and may decide to call g_sched_dispatch()
 * to send more bio's downstream.
 *
 * If an algorithm needs per-flow queues, these are created
 * calling gs_init_class() and destroyed with gs_fini_class(),
 * and they are also inserted in the hash table implemented in
 * the g_sched_softc{}
 *
 * If an algorithm is replaced, or a transparently-inserted node is
 * removed with "geom sched destroy", we need to remove all references
 * to the g_*_softc{} and g_sched_softc from the bio's still in
 * the scheduler. g_sched_forced_dispatch() helps doing this.
 * XXX need to explain better.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/limits.h>
#include <sys/hash.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/proc.h>		/* we access curthread */
#include <geom/geom.h>
#include "gs_scheduler.h"
#include "g_sched.h"		/* geom hooks */

/*
 * Size of the per-geom hash table storing traffic classes.
 * We may decide to change it at a later time, it has no ABI
 * implications as it is only used for run-time allocations.
 */
#define G_SCHED_HASH_SIZE	32

static int g_sched_destroy(struct g_geom *gp, boolean_t force);
static int g_sched_destroy_geom(struct gctl_req *req,
    struct g_class *mp, struct g_geom *gp);
static void g_sched_config(struct gctl_req *req, struct g_class *mp,
    const char *verb);
static struct g_geom *g_sched_taste(struct g_class *mp,
    struct g_provider *pp, int flags __unused);
static void g_sched_dumpconf(struct sbuf *sb, const char *indent,
    struct g_geom *gp, struct g_consumer *cp, struct g_provider *pp);
static void g_sched_init(struct g_class *mp);
static void g_sched_fini(struct g_class *mp);
static int g_sched_ioctl(struct g_provider *pp, u_long cmd, void *data,
    int fflag, struct thread *td);

struct g_class g_sched_class = {
	.name = G_SCHED_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_sched_config,
	.taste = g_sched_taste,
	.destroy_geom = g_sched_destroy_geom,
	.init = g_sched_init,
	.ioctl = g_sched_ioctl,
	.fini = g_sched_fini
};

MALLOC_DEFINE(M_GEOM_SCHED, "GEOM_SCHED", "Geom schedulers data structures");

/*
 * Global variables describing the state of the geom_sched module.
 * There is only one static instance of this structure.
 */
LIST_HEAD(gs_list, g_gsched);	/* type, link field */
struct geom_sched_vars {
	struct mtx	gs_mtx;
	struct gs_list	gs_scheds;	/* list of algorithms */
	u_int		gs_debug;
	u_int		gs_sched_count;	/* how many algorithms ? */
	u_int 		gs_patched;	/* g_io_request was patched */

	u_int		gs_initialized;
	u_int		gs_expire_secs;	/* expiration of hash entries */

	struct bio_queue_head gs_pending;
	u_int		gs_npending;

	/* The following are for stats, usually protected by gs_mtx. */
	u_long		gs_requests;	/* total requests */
	u_long		gs_done;	/* total done */
	u_int 		gs_in_flight;	/* requests in flight */
	u_int 		gs_writes_in_flight;
	u_int 		gs_bytes_in_flight;
	u_int 		gs_write_bytes_in_flight;

	char		gs_names[256];	/* names of schedulers */
};

static struct geom_sched_vars me = {
	.gs_expire_secs = 10,
};

SYSCTL_DECL(_kern_geom);
SYSCTL_NODE(_kern_geom, OID_AUTO, sched, CTLFLAG_RW, 0,
    "GEOM_SCHED stuff");

SYSCTL_UINT(_kern_geom_sched, OID_AUTO, in_flight_wb, CTLFLAG_RD,
    &me.gs_write_bytes_in_flight, 0, "Write bytes in flight");

SYSCTL_UINT(_kern_geom_sched, OID_AUTO, in_flight_b, CTLFLAG_RD,
    &me.gs_bytes_in_flight, 0, "Bytes in flight");

SYSCTL_UINT(_kern_geom_sched, OID_AUTO, in_flight_w, CTLFLAG_RD,
    &me.gs_writes_in_flight, 0, "Write Requests in flight");

SYSCTL_UINT(_kern_geom_sched, OID_AUTO, in_flight, CTLFLAG_RD,
    &me.gs_in_flight, 0, "Requests in flight");

SYSCTL_ULONG(_kern_geom_sched, OID_AUTO, done, CTLFLAG_RD,
    &me.gs_done, 0, "Total done");

SYSCTL_ULONG(_kern_geom_sched, OID_AUTO, requests, CTLFLAG_RD,
    &me.gs_requests, 0, "Total requests");

SYSCTL_STRING(_kern_geom_sched, OID_AUTO, algorithms, CTLFLAG_RD,
    &me.gs_names, 0, "Algorithm names");

SYSCTL_UINT(_kern_geom_sched, OID_AUTO, alg_count, CTLFLAG_RD,
    &me.gs_sched_count, 0, "Number of algorithms");

SYSCTL_UINT(_kern_geom_sched, OID_AUTO, debug, CTLFLAG_RW,
    &me.gs_debug, 0, "Debug level");

SYSCTL_UINT(_kern_geom_sched, OID_AUTO, expire_secs, CTLFLAG_RW,
    &me.gs_expire_secs, 0, "Expire time in seconds");

/*
 * g_sched calls the scheduler algorithms with this lock held.
 * The locking functions are exposed so the scheduler algorithms can also
 * protect themselves e.g. when running a callout handler.
 */
void
g_sched_lock(struct g_geom *gp)
{
	struct g_sched_softc *sc = gp->softc;

	mtx_lock(&sc->sc_mtx);
}

void
g_sched_unlock(struct g_geom *gp)
{
	struct g_sched_softc *sc = gp->softc;

	mtx_unlock(&sc->sc_mtx);
}

/*
 * Support functions to handle references to the module,
 * which are coming from devices using this scheduler.
 */
static inline void
g_gsched_ref(struct g_gsched *gsp)
{

	atomic_add_int(&gsp->gs_refs, 1);
}

static inline void
g_gsched_unref(struct g_gsched *gsp)
{

	atomic_add_int(&gsp->gs_refs, -1);
}

/*
 * Update the stats when this request is done.
 */
static void
g_sched_update_stats(struct bio *bio)
{

	me.gs_done++;
	me.gs_in_flight--;
	me.gs_bytes_in_flight -= bio->bio_length;
	if (bio->bio_cmd == BIO_WRITE) {
		me.gs_writes_in_flight--;
		me.gs_write_bytes_in_flight -= bio->bio_length;
	}
}

/*
 * Dispatch any pending request.
 */
static void
g_sched_forced_dispatch(struct g_geom *gp)
{
	struct g_sched_softc *sc = gp->softc;
	struct g_gsched *gsp = sc->sc_gsched;
	struct bio *bp;

	KASSERT(mtx_owned(&sc->sc_mtx),
	    ("sc_mtx not owned during forced dispatch"));

	while ((bp = gsp->gs_next(sc->sc_data, 1)) != NULL)
		g_io_request(bp, LIST_FIRST(&gp->consumer));
}

/*
 * The main dispatch loop, called either here after the start
 * routine, or by scheduling algorithms when they receive a timeout
 * or a 'done' notification.  Does not share code with the forced
 * dispatch path, since the gs_done() callback can call us.
 */
void
g_sched_dispatch(struct g_geom *gp)
{
	struct g_sched_softc *sc = gp->softc;
	struct g_gsched *gsp = sc->sc_gsched;
	struct bio *bp;

	KASSERT(mtx_owned(&sc->sc_mtx), ("sc_mtx not owned during dispatch"));

	if ((sc->sc_flags & G_SCHED_FLUSHING))
		return;

	while ((bp = gsp->gs_next(sc->sc_data, 0)) != NULL)
		g_io_request(bp, LIST_FIRST(&gp->consumer));
}

/*
 * Recent (8.0 and above) versions of FreeBSD have support to
 * register classifiers of disk requests. The classifier is
 * invoked by g_io_request(), and stores the information into
 * bp->bio_classifier1.
 *
 * Support for older versions, which is left here only for
 * documentation purposes, relies on two hacks:
 * 1. classification info is written into the bio_caller1
 *    field of the topmost node in the bio chain. This field
 *    is rarely used, but this module is incompatible with
 *    those that use bio_caller1 for other purposes,
 *    such as ZFS and gjournal;
 * 2. g_io_request() is patched in-memory when the module is
 *    loaded, so that the function calls a classifier as its
 *    first thing. g_io_request() is restored when the module
 *    is unloaded. This functionality is only supported for
 *    x86 and amd64, other architectures need source code changes.
 */

/*
 * Lookup the identity of the issuer of the original request.
 * In the current implementation we use the curthread of the
 * issuer, but different mechanisms may be implemented later
 * so we do not make assumptions on the return value which for
 * us is just an opaque identifier.
 */

static inline u_long
g_sched_classify(struct bio *bp)
{

	/* we have classifier fields in the struct bio */
	return ((u_long)bp->bio_classifier1);
}

/* Return the hash chain for the given key. */
static inline struct g_hash *
g_sched_hash(struct g_sched_softc *sc, u_long key)
{

	return (&sc->sc_hash[key & sc->sc_mask]);
}

/*
 * Helper function for the children classes, which takes
 * a geom and a bio and returns the private descriptor
 * associated to the request.  This involves fetching
 * the classification field and [al]locating the
 * corresponding entry in the hash table.
 */
void *
g_sched_get_class(struct g_geom *gp, struct bio *bp)
{
	struct g_sched_softc *sc;
	struct g_sched_class *gsc;
	struct g_gsched *gsp;
	struct g_hash *bucket;
	u_long key;

	sc = gp->softc;
	key = g_sched_classify(bp);
	bucket = g_sched_hash(sc, key);
	LIST_FOREACH(gsc, bucket, gsc_clist) {
		if (key == gsc->gsc_key) {
			gsc->gsc_refs++;
			return (gsc->gsc_priv);
		}
	}

	gsp = sc->sc_gsched;
	gsc = malloc(sizeof(*gsc) + gsp->gs_priv_size,
	    M_GEOM_SCHED, M_NOWAIT | M_ZERO);
	if (!gsc)
		return (NULL);

	if (gsp->gs_init_class(sc->sc_data, gsc->gsc_priv)) {
		free(gsc, M_GEOM_SCHED);
		return (NULL);
	}

	gsc->gsc_refs = 2;	/* 1 for the hash table, 1 for the caller. */
	gsc->gsc_key = key;
	LIST_INSERT_HEAD(bucket, gsc, gsc_clist);

	gsc->gsc_expire = ticks + me.gs_expire_secs * hz;

	return (gsc->gsc_priv);
}

/*
 * Release a reference to the per-client descriptor,
 */
void
g_sched_put_class(struct g_geom *gp, void *priv)
{
	struct g_sched_class *gsc;
	struct g_sched_softc *sc;

	gsc = g_sched_priv2class(priv);
	gsc->gsc_expire = ticks + me.gs_expire_secs * hz;

	if (--gsc->gsc_refs > 0)
		return;

	sc = gp->softc;
	sc->sc_gsched->gs_fini_class(sc->sc_data, priv);

	LIST_REMOVE(gsc, gsc_clist);
	free(gsc, M_GEOM_SCHED);
}

static void
g_sched_hash_fini(struct g_geom *gp, struct g_hash *hp, u_long mask,
    struct g_gsched *gsp, void *data)
{
	struct g_sched_class *cp, *cp2;
	int i;

	if (!hp)
		return;

	if (data && gsp->gs_hash_unref)
		gsp->gs_hash_unref(data);

	for (i = 0; i < G_SCHED_HASH_SIZE; i++) {
		LIST_FOREACH_SAFE(cp, &hp[i], gsc_clist, cp2)
			g_sched_put_class(gp, cp->gsc_priv);
	}

	hashdestroy(hp, M_GEOM_SCHED, mask);
}

static struct g_hash *
g_sched_hash_init(struct g_gsched *gsp, u_long *mask, int flags)
{
	struct g_hash *hash;

	if (gsp->gs_priv_size == 0)
		return (NULL);

	hash = hashinit_flags(G_SCHED_HASH_SIZE, M_GEOM_SCHED, mask, flags);

	return (hash);
}

static void
g_sched_flush_classes(struct g_geom *gp)
{
	struct g_sched_softc *sc;
	struct g_sched_class *cp, *cp2;
	int i;

	sc = gp->softc;

	if (!sc->sc_hash || ticks - sc->sc_flush_ticks <= 0)
		return;

	for (i = 0; i < G_SCHED_HASH_SIZE; i++) {
		LIST_FOREACH_SAFE(cp, &sc->sc_hash[i], gsc_clist, cp2) {
			if (cp->gsc_refs == 1 && ticks - cp->gsc_expire > 0)
				g_sched_put_class(gp, cp->gsc_priv);
		}
	}

	sc->sc_flush_ticks = ticks + me.gs_expire_secs * hz;
}

/*
 * Wait for the completion of any outstanding request.  To ensure
 * that this does not take forever the caller has to make sure that
 * no new request enter the scehduler before calling us.
 *
 * Must be called with the gp mutex held and topology locked.
 */
static int
g_sched_wait_pending(struct g_geom *gp)
{
	struct g_sched_softc *sc = gp->softc;
	int endticks = ticks + hz;

	g_topology_assert();

	while (sc->sc_pending && endticks - ticks >= 0)
		msleep(gp, &sc->sc_mtx, 0, "sched_wait_pending", hz / 4);

	return (sc->sc_pending ? ETIMEDOUT : 0);
}

static int
g_sched_remove_locked(struct g_geom *gp, struct g_gsched *gsp)
{
	struct g_sched_softc *sc = gp->softc;
	int error;

	/* Set the flushing flag: new bios will not enter the scheduler. */
	sc->sc_flags |= G_SCHED_FLUSHING;

	g_sched_forced_dispatch(gp);
	error = g_sched_wait_pending(gp);
	if (error)
		goto failed;
	
	/* No more requests pending or in flight from the old gsp. */

	g_sched_hash_fini(gp, sc->sc_hash, sc->sc_mask, gsp, sc->sc_data);
	sc->sc_hash = NULL;

	/*
	 * Avoid deadlock here by releasing the gp mutex and reacquiring
	 * it once done.  It should be safe, since no reconfiguration or
	 * destruction can take place due to the geom topology lock; no
	 * new request can use the current sc_data since we flagged the
	 * geom as being flushed.
	 */
	g_sched_unlock(gp);
	gsp->gs_fini(sc->sc_data);
	g_sched_lock(gp);

	sc->sc_gsched = NULL;
	sc->sc_data = NULL;
	g_gsched_unref(gsp);

failed:
	sc->sc_flags &= ~G_SCHED_FLUSHING;

	return (error);
}

static int
g_sched_remove(struct g_geom *gp, struct g_gsched *gsp)
{
	int error;

	g_sched_lock(gp);
	error = g_sched_remove_locked(gp, gsp); /* gsp is surely non-null */
	g_sched_unlock(gp);

	return (error);
}

/*
 * Support function for create/taste -- locate the desired
 * algorithm and grab a reference to it.
 */
static struct g_gsched *
g_gsched_find(const char *name)
{
	struct g_gsched *gsp = NULL;

	mtx_lock(&me.gs_mtx);
	LIST_FOREACH(gsp, &me.gs_scheds, glist) {
		if (strcmp(name, gsp->gs_name) == 0) {
			g_gsched_ref(gsp);
			break;
		}
	}
	mtx_unlock(&me.gs_mtx);

	return (gsp);
}

/*
 * Rebuild the list of scheduler names.
 * To be called with me.gs_mtx lock held.
 */
static void
g_gsched_build_names(struct g_gsched *gsp)
{
	int pos, l;
	struct g_gsched *cur;

	pos = 0;
	LIST_FOREACH(cur, &me.gs_scheds, glist) {
		l = strlen(cur->gs_name);
		if (l + pos + 1 + 1 < sizeof(me.gs_names)) {
			if (pos != 0)
				me.gs_names[pos++] = ' ';
			strcpy(me.gs_names + pos, cur->gs_name);
			pos += l;
		}
	}
	me.gs_names[pos] = '\0';
}

/*
 * Register or unregister individual scheduling algorithms.
 */
static int
g_gsched_register(struct g_gsched *gsp)
{
	struct g_gsched *cur;
	int error = 0;

	mtx_lock(&me.gs_mtx);
	LIST_FOREACH(cur, &me.gs_scheds, glist) {
		if (strcmp(gsp->gs_name, cur->gs_name) == 0)
			break;
	}
	if (cur != NULL) {
		G_SCHED_DEBUG(0, "A scheduler named %s already"
		    "exists.", gsp->gs_name);
		error = EEXIST;
	} else {
		LIST_INSERT_HEAD(&me.gs_scheds, gsp, glist);
		gsp->gs_refs = 1;
		me.gs_sched_count++;
		g_gsched_build_names(gsp);
	}
	mtx_unlock(&me.gs_mtx);

	return (error);
}

struct g_gsched_unregparm {
	struct g_gsched *gup_gsp;
	int		gup_error;
};

static void
g_gsched_unregister(void *arg, int flag)
{
	struct g_gsched_unregparm *parm = arg;
	struct g_gsched *gsp = parm->gup_gsp, *cur, *tmp;
	struct g_sched_softc *sc;
	struct g_geom *gp, *gp_tmp;
	int error;

	parm->gup_error = 0;

	g_topology_assert();

	if (flag == EV_CANCEL)
		return;

	mtx_lock(&me.gs_mtx);

	LIST_FOREACH_SAFE(gp, &g_sched_class.geom, geom, gp_tmp) {
		if (gp->class != &g_sched_class)
			continue;	/* Should not happen. */

		sc = gp->softc;
		if (sc->sc_gsched == gsp) {
			error = g_sched_remove(gp, gsp);
			if (error)
				goto failed;
		}
	}
		
	LIST_FOREACH_SAFE(cur, &me.gs_scheds, glist, tmp) {
		if (cur != gsp)
			continue;

		if (gsp->gs_refs != 1) {
			G_SCHED_DEBUG(0, "%s still in use.",
			    gsp->gs_name);
			parm->gup_error = EBUSY;
		} else {
			LIST_REMOVE(gsp, glist);
			me.gs_sched_count--;
			g_gsched_build_names(gsp);
		}
		break;
	}

	if (cur == NULL) {
		G_SCHED_DEBUG(0, "%s not registered.", gsp->gs_name);
		parm->gup_error = ENOENT;
	}

failed:
	mtx_unlock(&me.gs_mtx);
}

static inline void
g_gsched_global_init(void)
{

	if (!me.gs_initialized) {
		G_SCHED_DEBUG(0, "Initializing global data.");
		mtx_init(&me.gs_mtx, "gsched", NULL, MTX_DEF);
		LIST_INIT(&me.gs_scheds);
		bioq_init(&me.gs_pending);
		me.gs_initialized = 1;
	}
}

/*
 * Module event called when a scheduling algorithm module is loaded or
 * unloaded.
 */
int
g_gsched_modevent(module_t mod, int cmd, void *arg)
{
	struct g_gsched *gsp = arg;
	struct g_gsched_unregparm parm;
	int error;

	G_SCHED_DEBUG(0, "Modevent %d.", cmd);

	/*
	 * If the module is loaded at boot, the geom thread that calls
	 * g_sched_init() might actually run after g_gsched_modevent(),
	 * so make sure that the module is properly initialized.
	 */
	g_gsched_global_init();

	error = EOPNOTSUPP;
	switch (cmd) {
	case MOD_LOAD:
		error = g_gsched_register(gsp);
		G_SCHED_DEBUG(0, "Loaded module %s error %d.",
		    gsp->gs_name, error);
		if (error == 0)
			g_retaste(&g_sched_class);
		break;

	case MOD_UNLOAD:
		parm.gup_gsp = gsp;
		parm.gup_error = 0;

		error = g_waitfor_event(g_gsched_unregister,
		    &parm, M_WAITOK, NULL);
		if (error == 0)
			error = parm.gup_error;
		G_SCHED_DEBUG(0, "Unloaded module %s error %d.",
		    gsp->gs_name, error);
		break;
	}

	return (error);
}

#ifdef KTR
#define	TRC_BIO_EVENT(e, bp)	g_sched_trace_bio_ ## e (bp)

static inline char
g_sched_type(struct bio *bp)
{

	if (bp->bio_cmd == BIO_READ)
		return ('R');
	else if (bp->bio_cmd == BIO_WRITE)
		return ('W');
	return ('U');
}

static inline void
g_sched_trace_bio_START(struct bio *bp)
{

	CTR5(KTR_GSCHED, "S %lu %c %lu/%lu %lu", g_sched_classify(bp),
	    g_sched_type(bp), bp->bio_offset / ULONG_MAX,
	    bp->bio_offset, bp->bio_length);
}

static inline void
g_sched_trace_bio_DONE(struct bio *bp)
{

	CTR5(KTR_GSCHED, "D %lu %c %lu/%lu %lu", g_sched_classify(bp),
	    g_sched_type(bp), bp->bio_offset / ULONG_MAX,
	    bp->bio_offset, bp->bio_length);
}
#else /* !KTR */
#define	TRC_BIO_EVENT(e, bp)
#endif /* !KTR */

/*
 * g_sched_done() and g_sched_start() dispatch the geom requests to
 * the scheduling algorithm in use.
 */
static void
g_sched_done(struct bio *bio)
{
	struct g_geom *gp = bio->bio_caller2;
	struct g_sched_softc *sc = gp->softc;

	TRC_BIO_EVENT(DONE, bio);

	KASSERT(bio->bio_caller1, ("null bio_caller1 in g_sched_done"));

	g_sched_lock(gp);

	g_sched_update_stats(bio);
	sc->sc_gsched->gs_done(sc->sc_data, bio);
	if (!--sc->sc_pending)
		wakeup(gp);

	g_sched_flush_classes(gp);
	g_sched_unlock(gp);

	g_std_done(bio);
}

static void
g_sched_start(struct bio *bp)
{
	struct g_geom *gp = bp->bio_to->geom;
	struct g_sched_softc *sc = gp->softc;
	struct bio *cbp;

	TRC_BIO_EVENT(START, bp);
	G_SCHED_LOGREQ(bp, "Request received.");

	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		g_io_deliver(bp, ENOMEM);
		return;
	}
	cbp->bio_done = g_sched_done;
	cbp->bio_to = LIST_FIRST(&gp->provider);
	KASSERT(cbp->bio_to != NULL, ("NULL provider"));

	/* We only schedule reads and writes. */
	if (bp->bio_cmd != BIO_READ && bp->bio_cmd != BIO_WRITE)
		goto bypass;

	G_SCHED_LOGREQ(cbp, "Sending request.");

	g_sched_lock(gp);
	/*
	 * Call the algorithm's gs_start to queue the request in the
	 * scheduler. If gs_start fails then pass the request down,
	 * otherwise call g_sched_dispatch() which tries to push
	 * one or more requests down.
	 */
	if (!sc->sc_gsched || (sc->sc_flags & G_SCHED_FLUSHING) ||
	    sc->sc_gsched->gs_start(sc->sc_data, cbp)) {
		g_sched_unlock(gp);
		goto bypass;
	}
	/*
	 * We use bio_caller1 to mark requests that are scheduled
	 * so make sure it is not NULL.
	 */
	if (cbp->bio_caller1 == NULL)
		cbp->bio_caller1 = &me;	/* anything not NULL */

	cbp->bio_caller2 = gp;
	sc->sc_pending++;

	/* Update general stats. */
	me.gs_in_flight++;
	me.gs_requests++;
	me.gs_bytes_in_flight += bp->bio_length;
	if (bp->bio_cmd == BIO_WRITE) {
		me.gs_writes_in_flight++;
		me.gs_write_bytes_in_flight += bp->bio_length;
	}
	g_sched_dispatch(gp);
	g_sched_unlock(gp);
	return;

bypass:
	cbp->bio_done = g_std_done;
	cbp->bio_caller1 = NULL; /* not scheduled */
	g_io_request(cbp, LIST_FIRST(&gp->consumer));
}

/*
 * The next few functions are the geom glue.
 */
static void
g_sched_orphan(struct g_consumer *cp)
{

	g_topology_assert();
	g_sched_destroy(cp->geom, 1);
}

static int
g_sched_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	int error;

	gp = pp->geom;
	cp = LIST_FIRST(&gp->consumer);
	error = g_access(cp, dr, dw, de);

	return (error);
}

static void
g_sched_temporary_start(struct bio *bio)
{

	mtx_lock(&me.gs_mtx);
	me.gs_npending++;
	bioq_disksort(&me.gs_pending, bio);
	mtx_unlock(&me.gs_mtx);
}

static void
g_sched_flush_pending(g_start_t *start)
{
	struct bio *bp;

	while ((bp = bioq_takefirst(&me.gs_pending)))
		start(bp);
}

static int
g_insert_proxy(struct g_geom *gp, struct g_provider *newpp,
    struct g_geom *dstgp, struct g_provider *pp, struct g_consumer *cp)
{
	struct g_sched_softc *sc = gp->softc;
	g_start_t *saved_start, *flush = g_sched_start;
	int error = 0, endticks = ticks + hz;

	g_cancel_event(newpp);	/* prevent taste() */
	/* copy private fields */
	newpp->private = pp->private;
	newpp->index = pp->index;

	/* Queue all the early requests coming for us. */
	me.gs_npending = 0;
	saved_start = pp->geom->start;
	dstgp->start = g_sched_temporary_start;

	while (pp->nstart - pp->nend != me.gs_npending &&
	    endticks - ticks >= 0)
		tsleep(pp, PRIBIO, "-", hz/10);

	if (pp->nstart - pp->nend != me.gs_npending) {
		flush = saved_start;
		error = ETIMEDOUT;
		goto fail;
	}

	/* link pp to this geom */
	LIST_REMOVE(pp, provider);
	pp->geom = gp;
	LIST_INSERT_HEAD(&gp->provider, pp, provider);

	/*
	 * replicate the counts from the parent in the
	 * new provider and consumer nodes
	 */
	cp->acr = newpp->acr = pp->acr;
	cp->acw = newpp->acw = pp->acw;
	cp->ace = newpp->ace = pp->ace;
	sc->sc_flags |= G_SCHED_PROXYING;

fail:
	dstgp->start = saved_start;

	g_sched_flush_pending(flush);

	return (error);
}

/*
 * Create a geom node for the device passed as *pp.
 * If successful, add a reference to this gsp.
 */
static int
g_sched_create(struct gctl_req *req, struct g_class *mp,
    struct g_provider *pp, struct g_gsched *gsp, int proxy)
{
	struct g_sched_softc *sc = NULL;
	struct g_geom *gp, *dstgp;
	struct g_provider *newpp = NULL;
	struct g_consumer *cp = NULL;
	char name[64];
	int error;

	g_topology_assert();

	snprintf(name, sizeof(name), "%s%s", pp->name, G_SCHED_SUFFIX);
	LIST_FOREACH(gp, &mp->geom, geom) {
		if (strcmp(gp->name, name) == 0) {
			gctl_error(req, "Geom %s already exists.",
			    name);
			return (EEXIST);
		}
	}

	gp = g_new_geomf(mp, "%s", name);
	dstgp = proxy ? pp->geom : gp; /* where do we link the provider */

	sc = g_malloc(sizeof(*sc), M_WAITOK | M_ZERO);
	sc->sc_gsched = gsp;
	sc->sc_data = gsp->gs_init(gp);
	if (sc->sc_data == NULL) {
		error = ENOMEM;
		goto fail;
	}

	sc->sc_hash = g_sched_hash_init(gsp, &sc->sc_mask, HASH_WAITOK);

	/*
	 * Do not initialize the flush mechanism, will be initialized
	 * on the first insertion on the hash table.
	 */

	mtx_init(&sc->sc_mtx, "g_sched_mtx", NULL, MTX_DEF);

	gp->softc = sc;
	gp->start = g_sched_start;
	gp->orphan = g_sched_orphan;
	gp->access = g_sched_access;
	gp->dumpconf = g_sched_dumpconf;

	newpp = g_new_providerf(dstgp, "%s", gp->name);
	newpp->mediasize = pp->mediasize;
	newpp->sectorsize = pp->sectorsize;

	cp = g_new_consumer(gp);
	error = g_attach(cp, proxy ? newpp : pp);
	if (error != 0) {
		gctl_error(req, "Cannot attach to provider %s.",
		    pp->name);
		goto fail;
	}

	g_error_provider(newpp, 0);
	if (proxy) {
		error = g_insert_proxy(gp, newpp, dstgp, pp, cp);
		if (error)
			goto fail;
	}
	G_SCHED_DEBUG(0, "Device %s created.", gp->name);

	g_gsched_ref(gsp);

	return (0);

fail:
	if (cp != NULL) {
		if (cp->provider != NULL)
			g_detach(cp);
		g_destroy_consumer(cp);
	}
	if (newpp != NULL)
		g_destroy_provider(newpp);
	if (sc->sc_hash)
		g_sched_hash_fini(gp, sc->sc_hash, sc->sc_mask,
		    gsp, sc->sc_data);
	if (sc->sc_data)
		gsp->gs_fini(sc->sc_data);
	g_free(gp->softc);
	g_destroy_geom(gp);

	return (error);
}

/*
 * Support for dynamic switching of scheduling algorithms.
 * First initialize the data structures for the new algorithm,
 * then call g_sched_remove_locked() to flush all references
 * to the old one, finally link the new algorithm.
 */
static int
g_sched_change_algo(struct gctl_req *req, struct g_class *mp,
    struct g_provider *pp, struct g_gsched *gsp)
{
	struct g_sched_softc *sc;
	struct g_geom *gp;
	struct g_hash *newh;
	void *data;
	u_long mask;
	int error = 0;

	gp = pp->geom;
	sc = gp->softc;

	data = gsp->gs_init(gp);
	if (data == NULL)
		return (ENOMEM);

	newh = g_sched_hash_init(gsp, &mask, HASH_WAITOK);
	if (gsp->gs_priv_size && !newh) {
		error = ENOMEM;
		goto fail;
	}

	g_sched_lock(gp);
	if (sc->sc_gsched) {	/* can be NULL in some cases */
		error = g_sched_remove_locked(gp, sc->sc_gsched);
		if (error)
			goto fail;
	}

	g_gsched_ref(gsp);
	sc->sc_gsched = gsp;
	sc->sc_data = data;
	sc->sc_hash = newh;
	sc->sc_mask = mask;

	g_sched_unlock(gp);

	return (0);

fail:
	if (newh)
		g_sched_hash_fini(gp, newh, mask, gsp, data);

	if (data)
		gsp->gs_fini(data);

	g_sched_unlock(gp);

	return (error);
}

/*
 * Stop the request flow directed to the proxy, redirecting the new
 * requests to the me.gs_pending queue.
 */
static struct g_provider *
g_detach_proxy(struct g_geom *gp)
{
	struct g_consumer *cp;
	struct g_provider *pp, *newpp;

	do {
		pp = LIST_FIRST(&gp->provider);
		if (pp == NULL)
			break;
		cp = LIST_FIRST(&gp->consumer);
		if (cp == NULL)
			break;
		newpp = cp->provider;
		if (newpp == NULL)
			break;

		me.gs_npending = 0;
		pp->geom->start = g_sched_temporary_start;

		return (pp);
	} while (0);
	printf("%s error detaching proxy %s\n", __FUNCTION__, gp->name);

	return (NULL);
}

static void
g_sched_blackhole(struct bio *bp)
{

	g_io_deliver(bp, ENXIO);
}

static inline void
g_reparent_provider(struct g_provider *pp, struct g_geom *gp,
    struct g_provider *newpp)
{

	LIST_REMOVE(pp, provider);
	if (newpp) {
		pp->private = newpp->private;
		pp->index = newpp->index;
	}
	pp->geom = gp;
	LIST_INSERT_HEAD(&gp->provider, pp, provider);
}

static inline void
g_unproxy_provider(struct g_provider *oldpp, struct g_provider *newpp)
{
	struct g_geom *gp = oldpp->geom;

	g_reparent_provider(oldpp, newpp->geom, newpp);

	/*
	 * Hackish: let the system destroy the old provider for us, just
	 * in case someone attached a consumer to it, in which case a
	 * direct call to g_destroy_provider() would not work.
	 */
	g_reparent_provider(newpp, gp, NULL);
}

/*
 * Complete the proxy destruction, linking the old provider to its
 * original geom, and destroying the proxy provider.  Also take care
 * of issuing the pending requests collected in me.gs_pending (if any).
 */
static int
g_destroy_proxy(struct g_geom *gp, struct g_provider *oldpp)
{
	struct g_consumer *cp;
	struct g_provider *newpp;

	do {
		cp = LIST_FIRST(&gp->consumer);
		if (cp == NULL)
			break;
		newpp = cp->provider;
		if (newpp == NULL)
			break;

		/* Relink the provider to its original geom. */
		g_unproxy_provider(oldpp, newpp);

		/* Detach consumer from provider, and destroy provider. */
		cp->acr = newpp->acr = 0;
		cp->acw = newpp->acw = 0;
		cp->ace = newpp->ace = 0;
		g_detach(cp);

		/* Send the pending bios through the right start function. */
		g_sched_flush_pending(oldpp->geom->start);

		return (0);
	} while (0);
	printf("%s error destroying proxy %s\n", __FUNCTION__, gp->name);

	/* We cannot send the pending bios anywhere... */
	g_sched_flush_pending(g_sched_blackhole);

	return (EINVAL);
}

static int
g_sched_destroy(struct g_geom *gp, boolean_t force)
{
	struct g_provider *pp, *oldpp = NULL;
	struct g_sched_softc *sc;
	struct g_gsched *gsp;
	int error;

	g_topology_assert();
	sc = gp->softc;
	if (sc == NULL)
		return (ENXIO);
	if (!(sc->sc_flags & G_SCHED_PROXYING)) {
		pp = LIST_FIRST(&gp->provider);
		if (pp && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
			const char *msg = force ?
				"but we force removal" : "cannot remove";

			G_SCHED_DEBUG(!force,
			    "Device %s is still open (r%dw%de%d), %s.",
			    pp->name, pp->acr, pp->acw, pp->ace, msg);
			if (!force)
				return (EBUSY);
		} else {
			G_SCHED_DEBUG(0, "Device %s removed.", gp->name);
		}
	} else
		oldpp = g_detach_proxy(gp);

	gsp = sc->sc_gsched;
	if (gsp) {
		/*
		 * XXX bad hack here: force a dispatch to release
		 * any reference to the hash table still held by
		 * the scheduler.
		 */
		g_sched_lock(gp);
		/*
		 * We are dying here, no new requests should enter
		 * the scheduler.  This is granted by the topolgy,
		 * either in case we were proxying (new bios are
		 * being redirected) or not (see the access check
		 * above).
		 */
		g_sched_forced_dispatch(gp);
		error = g_sched_wait_pending(gp);

		if (error) {
			/*
			 * Not all the requests came home: this might happen
			 * under heavy load, or if we were waiting for any
			 * bio which is served in the event path (see
			 * geom_slice.c for an example of how this can
			 * happen).  Try to restore a working configuration
			 * if we can fail.
			 */
			if ((sc->sc_flags & G_SCHED_PROXYING) && oldpp) {
				g_sched_flush_pending(force ?
				    g_sched_blackhole : g_sched_start);
			}

			/*
			 * In the forced destroy case there is not so much
			 * we can do, we have pending bios that will call
			 * g_sched_done() somehow, and we don't want them
			 * to crash the system using freed memory.  We tell
			 * the user that something went wrong, and leak some
			 * memory here.
			 * Note: the callers using force = 1 ignore the
			 * return value.
			 */
			if (force) {
				G_SCHED_DEBUG(0, "Pending requests while "
				    " destroying geom, some memory leaked.");
			}

			return (error);
		}

		g_sched_unlock(gp);
		g_sched_hash_fini(gp, sc->sc_hash, sc->sc_mask,
		    gsp, sc->sc_data);
		sc->sc_hash = NULL;
		gsp->gs_fini(sc->sc_data);
		g_gsched_unref(gsp);
		sc->sc_gsched = NULL;
	} else
		error = 0;

	if ((sc->sc_flags & G_SCHED_PROXYING) && oldpp) {
		error = g_destroy_proxy(gp, oldpp);

		if (error) {
			if (force) {
				G_SCHED_DEBUG(0, "Unrecoverable error while "
				    "destroying a proxy geom, leaking some "
				    " memory.");
			}

			return (error);
		}
	}

	mtx_destroy(&sc->sc_mtx);

	g_free(gp->softc);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);

	return (error);
}

static int
g_sched_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp)
{

	return (g_sched_destroy(gp, 0));
}

/*
 * Functions related to the classification of requests.
 *
 * On recent FreeBSD versions (8.0 and above), we store a reference
 * to the issuer of a request in bp->bio_classifier1 as soon
 * as the bio is posted to the geom queue (and not later, because
 * requests are managed by the g_down thread afterwards).
 */

/*
 * Classifier support for recent FreeBSD versions: we use
 * a very simple classifier, only use curthread to tag a request.
 * The classifier is registered at module load, and unregistered
 * at module unload.
 */
static int
g_sched_tag(void *arg, struct bio *bp)
{

	bp->bio_classifier1 = curthread;
	return (1);
}

static struct g_classifier_hook g_sched_classifier = {
	.func =	g_sched_tag,
};

static inline void
g_classifier_ini(void)
{

	g_register_classifier(&g_sched_classifier);
}

static inline void
g_classifier_fini(void)
{

	g_unregister_classifier(&g_sched_classifier);
}

static void
g_sched_init(struct g_class *mp)
{

	g_gsched_global_init();

	G_SCHED_DEBUG(0, "Loading: mp = %p, g_sched_class = %p.",
	    mp, &g_sched_class);

	/* Patch g_io_request to store classification info in the bio. */
	g_classifier_ini();
}

static void
g_sched_fini(struct g_class *mp)
{

	g_classifier_fini();

	G_SCHED_DEBUG(0, "Unloading...");

	KASSERT(LIST_EMPTY(&me.gs_scheds), ("still registered schedulers"));
	mtx_destroy(&me.gs_mtx);
}

static int
g_sched_ioctl(struct g_provider *pp, u_long cmd, void *data, int fflag,
    struct thread *td)
{
	struct g_consumer *cp;
	struct g_geom *gp;

	cp = LIST_FIRST(&pp->geom->consumer);
	if (cp == NULL)
		return (ENOIOCTL);
	gp = cp->provider->geom;
	if (gp->ioctl == NULL)
		return (ENOIOCTL);
	return (gp->ioctl(cp->provider, cmd, data, fflag, td));
}

/*
 * Read the i-th argument for a request, skipping the /dev/
 * prefix if present.
 */
static const char *
g_sched_argi(struct gctl_req *req, int i)
{
	static const char *dev_prefix = "/dev/";
	const char *name;
	char param[16];
	int l = strlen(dev_prefix);

	snprintf(param, sizeof(param), "arg%d", i);
	name = gctl_get_asciiparam(req, param);
	if (name == NULL)
		gctl_error(req, "No 'arg%d' argument", i);
	else if (strncmp(name, dev_prefix, l) == 0)
		name += l;
	return (name);
}

/*
 * Fetch nargs and do appropriate checks.
 */
static int
g_sched_get_nargs(struct gctl_req *req)
{
	int *nargs;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No 'nargs' argument");
		return (0);
	}
	if (*nargs <= 0)
		gctl_error(req, "Missing device(s).");
	return (*nargs);
}

/*
 * Check whether we should add the class on certain volumes when
 * this geom is created. Right now this is under control of a kenv
 * variable containing the names of all devices that we care about.
 * Probably we should only support transparent insertion as the
 * preferred mode of operation.
 */
static struct g_geom *
g_sched_taste(struct g_class *mp, struct g_provider *pp,
		int flags __unused)
{
	struct g_gsched *gsp = NULL;	/* the . algorithm we want */
	const char *s;			/* generic string pointer */
	const char *taste_names;	/* devices we like */
	int l;
    
        g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__,
	    mp->name, pp->name);
        g_topology_assert();
 
        G_SCHED_DEBUG(2, "Tasting %s.", pp->name);

	do {
		/* do not taste on ourselves */
		if (pp->geom->class == mp)
                	break;

		taste_names = kern_getenv("geom.sched.taste");
		if (taste_names == NULL)
			break;

		l = strlen(pp->name);
		for (s = taste_names; *s &&
		    (s = strstr(s, pp->name)); s++) {
			/* further checks for an exact match */
			if ( (s == taste_names || s[-1] == ' ') &&
			     (s[l] == '\0' || s[l] == ' ') )
				break;
		}
		if (s == NULL)
			break;
		G_SCHED_DEBUG(0, "Attach device %s match [%s]\n",
		    pp->name, s);

		/* look up the provider name in the list */
		s = kern_getenv("geom.sched.algo");
		if (s == NULL)
			s = "rr";

		gsp = g_gsched_find(s);	/* also get a reference */
		if (gsp == NULL) {
			G_SCHED_DEBUG(0, "Bad '%s' algorithm.", s);
			break;
		}

		/* XXX create with 1 as last argument ? */
		g_sched_create(NULL, mp, pp, gsp, 0);
		g_gsched_unref(gsp);
	} while (0);
	return NULL;
}

static void
g_sched_ctl_create(struct gctl_req *req, struct g_class *mp, int proxy)
{
	struct g_provider *pp;
	struct g_gsched *gsp;
	const char *name;
	int i, nargs;

	g_topology_assert();

	name = gctl_get_asciiparam(req, "algo");
	if (name == NULL) {
		gctl_error(req, "No '%s' argument", "algo");
		return;
	}

	gsp = g_gsched_find(name);	/* also get a reference */
	if (gsp == NULL) {
		gctl_error(req, "Bad algorithm '%s'", name);
		return;
	}

	nargs = g_sched_get_nargs(req);

	/*
	 * Run on the arguments, and break on any error.
	 * We look for a device name, but skip the /dev/ prefix if any.
	 */
	for (i = 0; i < nargs; i++) {
		name = g_sched_argi(req, i);
		if (name == NULL)
			break;
		pp = g_provider_by_name(name);
		if (pp == NULL) {
			G_SCHED_DEBUG(1, "Provider %s is invalid.", name);
			gctl_error(req, "Provider %s is invalid.", name);
			break;
		}
		if (g_sched_create(req, mp, pp, gsp, proxy) != 0)
			break;
	}

	g_gsched_unref(gsp);
}

static void
g_sched_ctl_configure(struct gctl_req *req, struct g_class *mp)
{
	struct g_provider *pp;
	struct g_gsched *gsp;
	const char *name;
	int i, nargs;

	g_topology_assert();

	name = gctl_get_asciiparam(req, "algo");
	if (name == NULL) {
		gctl_error(req, "No '%s' argument", "algo");
		return;
	}

	gsp = g_gsched_find(name);	/* also get a reference */
	if (gsp == NULL) {
		gctl_error(req, "Bad algorithm '%s'", name);
		return;
	}

	nargs = g_sched_get_nargs(req);

	/*
	 * Run on the arguments, and break on any error.
	 * We look for a device name, but skip the /dev/ prefix if any.
	 */
	for (i = 0; i < nargs; i++) {
		name = g_sched_argi(req, i);
		if (name == NULL)
			break;
		pp = g_provider_by_name(name);
		if (pp == NULL || pp->geom->class != mp) {
			G_SCHED_DEBUG(1, "Provider %s is invalid.", name);
			gctl_error(req, "Provider %s is invalid.", name);
			break;
		}
		if (g_sched_change_algo(req, mp, pp, gsp) != 0)
			break;
	}

	g_gsched_unref(gsp);
}

static struct g_geom *
g_sched_find_geom(struct g_class *mp, const char *name)
{
	struct g_geom *gp;

	LIST_FOREACH(gp, &mp->geom, geom) {
		if (strcmp(gp->name, name) == 0)
			return (gp);
	}
	return (NULL);
}

static void
g_sched_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	int nargs, *force, error, i;
	struct g_geom *gp;
	const char *name;

	g_topology_assert();

	nargs = g_sched_get_nargs(req);

	force = gctl_get_paraml(req, "force", sizeof(*force));
	if (force == NULL) {
		gctl_error(req, "No 'force' argument");
		return;
	}

	for (i = 0; i < nargs; i++) {
		name = g_sched_argi(req, i);
		if (name == NULL)
			break;

		gp = g_sched_find_geom(mp, name);
		if (gp == NULL) {
			G_SCHED_DEBUG(1, "Device %s is invalid.", name);
			gctl_error(req, "Device %s is invalid.", name);
			break;
		}

		error = g_sched_destroy(gp, *force);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    gp->name, error);
			break;
		}
	}
}

static void
g_sched_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}

	if (*version != G_SCHED_VERSION) {
		gctl_error(req, "Userland and kernel parts are "
		    "out of sync.");
		return;
	}

	if (strcmp(verb, "create") == 0) {
		g_sched_ctl_create(req, mp, 0);
		return;
	} else if (strcmp(verb, "insert") == 0) {
		g_sched_ctl_create(req, mp, 1);
		return;
	} else if (strcmp(verb, "configure") == 0) {
		g_sched_ctl_configure(req, mp);
		return;
	} else if (strcmp(verb, "destroy") == 0) {
		g_sched_ctl_destroy(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

static void
g_sched_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_sched_softc *sc = gp->softc;
	struct g_gsched *gsp = sc->sc_gsched;
	if (indent == NULL) {	/* plaintext */
		sbuf_printf(sb, " algo %s", gsp ? gsp->gs_name : "--");
	}
	if (gsp != NULL && gsp->gs_dumpconf)
		gsp->gs_dumpconf(sb, indent, gp, cp, pp);
}

DECLARE_GEOM_CLASS(g_sched_class, g_sched);
MODULE_VERSION(geom_sched, 0);
