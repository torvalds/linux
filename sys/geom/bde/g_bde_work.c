/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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
/*
 * This source file contains the state-engine which makes things happen in the
 * right order.
 *
 * Outline:
 *   1) g_bde_start1()
 *	Break the struct bio into multiple work packets one per zone.
 *   2) g_bde_start2()
 *	Setup the necessary sector buffers and start those read operations
 *	which we can start at this time and put the item on the work-list.
 *   3) g_bde_worker()
 *	Scan the work-list for items which are ready for crypto processing
 *	and call the matching crypto function in g_bde_crypt.c and schedule
 *	any writes needed.  Read operations finish here by releasing the
 *	sector buffers and delivering the original bio request.
 *   4) g_bde_write_done()
 *	Release sector buffers and deliver the original bio request.
 *
 * Because of the C-scope rules, the functions are almost perfectly in the
 * opposite order in this source file.
 *
 * XXX: A switch to the hardware assisted crypto in src/sys/opencrypto will add
 * XXX: additional states to this state-engine.  Since no hardware available
 * XXX: at this time has AES support, implementing this has been postponed
 * XXX: until such time as it would result in a benefit.
 */

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/kthread.h>

#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha512.h>
#include <geom/geom.h>
#include <geom/bde/g_bde.h>

static void g_bde_delete_sector(struct g_bde_softc *wp, struct g_bde_sector *sp);
static struct g_bde_sector * g_bde_new_sector(struct g_bde_work *wp, u_int len);
static void g_bde_release_keysector(struct g_bde_work *wp);
static struct g_bde_sector *g_bde_get_keysector(struct g_bde_work *wp);
static int g_bde_start_read(struct g_bde_sector *sp);
static void g_bde_purge_sector(struct g_bde_softc *sc, int fraction);

/*
 * Work item allocation.
 *
 * C++ would call these constructors and destructors.
 */
static u_int g_bde_nwork;
SYSCTL_UINT(_debug, OID_AUTO, gbde_nwork, CTLFLAG_RD, &g_bde_nwork, 0, "");

static MALLOC_DEFINE(M_GBDE, "gbde", "GBDE data structures");

static struct g_bde_work *
g_bde_new_work(struct g_bde_softc *sc)
{
	struct g_bde_work *wp;

	wp = malloc(sizeof *wp, M_GBDE, M_NOWAIT | M_ZERO);
	if (wp == NULL)
		return (wp);
	wp->state = SETUP;
	wp->softc = sc;
	g_bde_nwork++;
	sc->nwork++;
	TAILQ_INSERT_TAIL(&sc->worklist, wp, list);
	return (wp);
}

static void
g_bde_delete_work(struct g_bde_work *wp)
{
	struct g_bde_softc *sc;

	sc = wp->softc;
	g_bde_nwork--;
	sc->nwork--;
	TAILQ_REMOVE(&sc->worklist, wp, list);
	free(wp, M_GBDE);
}

/*
 * Sector buffer allocation
 *
 * These two functions allocate and free back variable sized sector buffers
 */

static u_int g_bde_nsect;
SYSCTL_UINT(_debug, OID_AUTO, gbde_nsect, CTLFLAG_RD, &g_bde_nsect, 0, "");

static void
g_bde_delete_sector(struct g_bde_softc *sc, struct g_bde_sector *sp)
{

	g_bde_nsect--;
	sc->nsect--;
	if (sp->malloc)
		free(sp->data, M_GBDE);
	free(sp, M_GBDE);
}

static struct g_bde_sector *
g_bde_new_sector(struct g_bde_work *wp, u_int len)
{
	struct g_bde_sector *sp;

	sp = malloc(sizeof *sp, M_GBDE, M_NOWAIT | M_ZERO);
	if (sp == NULL)
		return (sp);
	if (len > 0) {
		sp->data = malloc(len, M_GBDE, M_NOWAIT | M_ZERO);
		if (sp->data == NULL) {
			free(sp, M_GBDE);
			return (NULL);
		}
		sp->malloc = 1;
	}
	g_bde_nsect++;
	wp->softc->nsect++;
	sp->size = len;
	sp->softc = wp->softc;
	sp->ref = 1;
	sp->owner = wp;
	sp->offset = wp->so;
	sp->state = JUNK;
	return (sp);
}

/*
 * Skey sector cache.
 *
 * Nothing prevents two separate I/O requests from addressing the same zone
 * and thereby needing the same skey sector.  We therefore need to sequence
 * I/O operations to the skey sectors.  A certain amount of caching is also
 * desirable, although the extent of benefit from this is not at this point
 * determined.
 *
 * XXX: GEOM may be able to grow a generic caching facility at some point
 * XXX: to support such needs.
 */

static u_int g_bde_ncache;
SYSCTL_UINT(_debug, OID_AUTO, gbde_ncache, CTLFLAG_RD, &g_bde_ncache, 0, "");

static void
g_bde_purge_one_sector(struct g_bde_softc *sc, struct g_bde_sector *sp)
{

	g_trace(G_T_TOPOLOGY, "g_bde_purge_one_sector(%p, %p)", sc, sp);
	if (sp->ref != 0)
		return;
	TAILQ_REMOVE(&sc->freelist, sp, list);
	g_bde_ncache--;
	sc->ncache--;
	bzero(sp->data, sp->size);
	g_bde_delete_sector(sc, sp);
}

static struct g_bde_sector *
g_bde_get_keysector(struct g_bde_work *wp)
{
	struct g_bde_sector *sp;
	struct g_bde_softc *sc;
	off_t offset;

	offset = wp->kso;
	g_trace(G_T_TOPOLOGY, "g_bde_get_keysector(%p, %jd)", wp, (intmax_t)offset);
	sc = wp->softc;

	if (malloc_last_fail() < g_bde_ncache)
		g_bde_purge_sector(sc, -1);

	sp = TAILQ_FIRST(&sc->freelist);
	if (sp != NULL && sp->ref == 0 && sp->used + 300 < time_uptime)
		g_bde_purge_one_sector(sc, sp);

	TAILQ_FOREACH(sp, &sc->freelist, list) {
		if (sp->offset == offset)
			break;
	}
	if (sp != NULL) {
		sp->ref++;
		KASSERT(sp->offset == offset, ("wrong offset"));
		KASSERT(sp->softc == wp->softc, ("wrong softc"));
		if (sp->ref == 1)
			sp->owner = wp;
	} else {
		if (malloc_last_fail() < g_bde_ncache) {
			TAILQ_FOREACH(sp, &sc->freelist, list)
				if (sp->ref == 0)
					break;
		}
		if (sp == NULL && !TAILQ_EMPTY(&sc->freelist))
			sp = TAILQ_FIRST(&sc->freelist);
		if (sp != NULL && sp->ref > 0)
			sp = NULL;
		if (sp == NULL) {
			sp = g_bde_new_sector(wp, sc->sectorsize);
			if (sp != NULL) {
				g_bde_ncache++;
				sc->ncache++;
				TAILQ_INSERT_TAIL(&sc->freelist, sp, list);
				sp->malloc = 2;
			}
		}
		if (sp != NULL) {
			sp->offset = offset;
			sp->softc = wp->softc;
			sp->ref = 1;
			sp->owner = wp;
			sp->state = JUNK;
			sp->error = 0;
		}
	}
	if (sp != NULL) {
		TAILQ_REMOVE(&sc->freelist, sp, list);
		TAILQ_INSERT_TAIL(&sc->freelist, sp, list);
		sp->used = time_uptime;
	}
	wp->ksp = sp;
	return(sp);
}

static void
g_bde_release_keysector(struct g_bde_work *wp)
{
	struct g_bde_softc *sc;
	struct g_bde_work *wp2;
	struct g_bde_sector *sp;

	sp = wp->ksp;
	g_trace(G_T_TOPOLOGY, "g_bde_release_keysector(%p)", sp);
	KASSERT(sp->malloc == 2, ("Wrong sector released"));
	sc = sp->softc;
	KASSERT(sc != NULL, ("NULL sp->softc"));
	KASSERT(wp == sp->owner, ("Releasing, not owner"));
	sp->owner = NULL;
	wp->ksp = NULL;
	sp->ref--;
	if (sp->ref > 0) {
		TAILQ_REMOVE(&sc->freelist, sp, list);
		TAILQ_INSERT_TAIL(&sc->freelist, sp, list);
		TAILQ_FOREACH(wp2, &sc->worklist, list) {
			if (wp2->ksp == sp) {
				KASSERT(wp2 != wp, ("Self-reowning"));
				sp->owner = wp2;
				wakeup(sp->softc);
				break;
			}
		}
		KASSERT(wp2 != NULL, ("Failed to pick up owner for %p\n", sp));
	} else if (sp->error != 0) {
		sp->offset = ~0;
		sp->error = 0;
		sp->state = JUNK;
	}
	TAILQ_REMOVE(&sc->freelist, sp, list);
	TAILQ_INSERT_HEAD(&sc->freelist, sp, list);
}

static void
g_bde_purge_sector(struct g_bde_softc *sc, int fraction)
{
	struct g_bde_sector *sp;
	int n;

	g_trace(G_T_TOPOLOGY, "g_bde_purge_sector(%p)", sc);
	if (fraction > 0)
		n = sc->ncache / fraction + 1;
	else 
		n = g_bde_ncache - malloc_last_fail();
	if (n < 0)
		return;
	if (n > sc->ncache)
		n = sc->ncache;
	while(n--) {
		TAILQ_FOREACH(sp, &sc->freelist, list) {
			if (sp->ref != 0)
				continue;
			TAILQ_REMOVE(&sc->freelist, sp, list);
			g_bde_ncache--;
			sc->ncache--;
			bzero(sp->data, sp->size);
			g_bde_delete_sector(sc, sp);
			break;
		}
	}
}

static struct g_bde_sector *
g_bde_read_keysector(struct g_bde_softc *sc, struct g_bde_work *wp)
{
	struct g_bde_sector *sp;

	g_trace(G_T_TOPOLOGY, "g_bde_read_keysector(%p)", wp);
	sp = g_bde_get_keysector(wp);
	if (sp == NULL) {
		g_bde_purge_sector(sc, -1);
		sp = g_bde_get_keysector(wp);
	}
	if (sp == NULL)
		return (sp);
	if (sp->owner != wp)
		return (sp);
	if (sp->state == VALID)
		return (sp);
	if (g_bde_start_read(sp) == 0)
		return (sp);
	g_bde_release_keysector(wp);
	return (NULL);
}

/*
 * Contribute to the completion of the original bio request.
 *
 * We have no simple way to tell how many bits the original bio request has
 * been segmented into, so the easiest way to determine when we can deliver
 * it is to keep track of the number of bytes we have completed.  We keep
 * track of any errors underway and latch onto the first one.
 *
 * We always report "nothing done" in case of error, because random bits here
 * and there may be completed and returning a number of completed bytes does
 * not convey any useful information about which bytes they were.  If some
 * piece of broken code somewhere interprets this to mean that nothing has
 * changed on the underlying media they deserve the lossage headed for them.
 *
 * A single mutex per g_bde instance is used to prevent contention.
 */

static void
g_bde_contribute(struct bio *bp, off_t bytes, int error)
{

	g_trace(G_T_TOPOLOGY, "g_bde_contribute bp %p bytes %jd error %d",
	     bp, (intmax_t)bytes, error);
	if (bp->bio_error == 0)
		bp->bio_error = error;
	bp->bio_completed += bytes;
	KASSERT(bp->bio_completed <= bp->bio_length, ("Too large contribution"));
	if (bp->bio_completed == bp->bio_length) {
		if (bp->bio_error != 0)
			bp->bio_completed = 0;
		g_io_deliver(bp, bp->bio_error);
	}
}

/*
 * This is the common case "we're done with this work package" function
 */

static void
g_bde_work_done(struct g_bde_work *wp, int error)
{

	g_bde_contribute(wp->bp, wp->length, error);
	if (wp->sp != NULL)
		g_bde_delete_sector(wp->softc, wp->sp);
	if (wp->ksp != NULL)
		g_bde_release_keysector(wp);
	g_bde_delete_work(wp);
}

/*
 * A write operation has finished.  When we have all expected cows in the
 * barn close the door and call it a day.
 */

static void
g_bde_write_done(struct bio *bp)
{
	struct g_bde_sector *sp;
	struct g_bde_work *wp;
	struct g_bde_softc *sc;

	sp = bp->bio_caller1;
	sc = bp->bio_caller2;
	mtx_lock(&sc->worklist_mutex);
	KASSERT(sp != NULL, ("NULL sp"));
	KASSERT(sc != NULL, ("NULL sc"));
	KASSERT(sp->owner != NULL, ("NULL sp->owner"));
	g_trace(G_T_TOPOLOGY, "g_bde_write_done(%p)", sp);
	if (bp->bio_error == 0 && bp->bio_completed != sp->size)
		bp->bio_error = EIO;
	sp->error = bp->bio_error;
	g_destroy_bio(bp);
	wp = sp->owner;
	if (wp->error == 0)
		wp->error = sp->error;

	if (wp->bp->bio_cmd == BIO_DELETE) {
		KASSERT(sp == wp->sp, ("trashed delete op"));
		g_bde_work_done(wp, wp->error);
		mtx_unlock(&sc->worklist_mutex);
		return;
	}

	KASSERT(wp->bp->bio_cmd == BIO_WRITE, ("Confused in g_bde_write_done()"));
	KASSERT(sp == wp->sp || sp == wp->ksp, ("trashed write op"));
	if (wp->sp == sp) {
		g_bde_delete_sector(sc, wp->sp);
		wp->sp = NULL;
	} else {
		sp->state = VALID;
	}
	if (wp->sp == NULL && wp->ksp != NULL && wp->ksp->state == VALID)
		g_bde_work_done(wp, wp->error);
	mtx_unlock(&sc->worklist_mutex);
	return;
}

/*
 * Send a write request for the given sector down the pipeline.
 */

static int
g_bde_start_write(struct g_bde_sector *sp)
{
	struct bio *bp;
	struct g_bde_softc *sc;

	g_trace(G_T_TOPOLOGY, "g_bde_start_write(%p)", sp);
	sc = sp->softc;
	KASSERT(sc != NULL, ("NULL sc in g_bde_start_write"));
	KASSERT(sp->owner != NULL, ("NULL sp->owner in g_bde_start_write"));
	bp = g_new_bio();
	if (bp == NULL)
		return (ENOMEM);
	bp->bio_cmd = BIO_WRITE;
	bp->bio_offset = sp->offset;
	bp->bio_data = sp->data;
	bp->bio_length = sp->size;
	bp->bio_done = g_bde_write_done;
	bp->bio_caller1 = sp;
	bp->bio_caller2 = sc;
	sp->state = IO;
	g_io_request(bp, sc->consumer);
	return(0);
}

/*
 * A read operation has finished.  Mark the sector no longer iobusy and
 * wake up the worker thread and let it do its thing.
 */

static void
g_bde_read_done(struct bio *bp)
{
	struct g_bde_sector *sp;
	struct g_bde_softc *sc;

	sp = bp->bio_caller1;
	g_trace(G_T_TOPOLOGY, "g_bde_read_done(%p)", sp);
	sc = bp->bio_caller2;
	mtx_lock(&sc->worklist_mutex);
	if (bp->bio_error == 0 && bp->bio_completed != sp->size)
		bp->bio_error = EIO;
	sp->error = bp->bio_error;
	if (sp->error == 0)
		sp->state = VALID;
	else
		sp->state = JUNK;
	wakeup(sc);
	g_destroy_bio(bp);
	mtx_unlock(&sc->worklist_mutex);
}

/*
 * Send a read request for the given sector down the pipeline.
 */

static int
g_bde_start_read(struct g_bde_sector *sp)
{
	struct bio *bp;
	struct g_bde_softc *sc;

	g_trace(G_T_TOPOLOGY, "g_bde_start_read(%p)", sp);
	sc = sp->softc;
	KASSERT(sc != NULL, ("Null softc in sp %p", sp));
	bp = g_new_bio();
	if (bp == NULL)
		return (ENOMEM);
	bp->bio_cmd = BIO_READ;
	bp->bio_offset = sp->offset;
	bp->bio_data = sp->data;
	bp->bio_length = sp->size;
	bp->bio_done = g_bde_read_done;
	bp->bio_caller1 = sp;
	bp->bio_caller2 = sc;
	sp->state = IO;
	g_io_request(bp, sc->consumer);
	return(0);
}

/*
 * The worker thread.
 *
 * The up/down path of GEOM is not allowed to sleep or do any major work
 * so we use this thread to do the actual crypto operations and to push
 * the state engine onwards.
 *
 * XXX: if we switch to the src/sys/opencrypt hardware assisted encryption
 * XXX: using a thread here is probably not needed.
 */

void
g_bde_worker(void *arg)
{
	struct g_bde_softc *sc;
	struct g_bde_work *wp, *twp;
	struct g_geom *gp;
	int restart, error;

	gp = arg;
	sc = gp->softc;

	mtx_lock(&sc->worklist_mutex);
	for (;;) {
		restart = 0;
		g_trace(G_T_TOPOLOGY, "g_bde_worker scan");
		TAILQ_FOREACH_SAFE(wp, &sc->worklist, list, twp) {
			KASSERT(wp != NULL, ("NULL wp"));
			KASSERT(wp->softc != NULL, ("NULL wp->softc"));
			if (wp->state != WAIT)
				continue;	/* Not interesting here */

			KASSERT(wp->bp != NULL, ("NULL wp->bp"));
			KASSERT(wp->sp != NULL, ("NULL wp->sp"));

			if (wp->ksp != NULL) {
				if (wp->ksp->owner != wp)
					continue;
				if (wp->ksp->state == IO)
					continue;
				KASSERT(wp->ksp->state == VALID,
				    ("Illegal sector state (%d)",
				    wp->ksp->state));
			}

			if (wp->bp->bio_cmd == BIO_READ && wp->sp->state == IO)
				continue;

			if (wp->ksp != NULL && wp->ksp->error != 0) {
				g_bde_work_done(wp, wp->ksp->error);
				continue;
			} 
			switch(wp->bp->bio_cmd) {
			case BIO_READ:
				if (wp->ksp == NULL) {
					KASSERT(wp->error != 0,
					    ("BIO_READ, no ksp and no error"));
					g_bde_work_done(wp, wp->error);
					break;
				}
				if (wp->sp->error != 0) {
					g_bde_work_done(wp, wp->sp->error);
					break;
				}
				mtx_unlock(&sc->worklist_mutex);
				g_bde_crypt_read(wp);
				mtx_lock(&sc->worklist_mutex);
				restart++;
				g_bde_work_done(wp, wp->sp->error);
				break;
			case BIO_WRITE:
				wp->state = FINISH;
				KASSERT(wp->sp->owner == wp,
				    ("Write not owner sp"));
				KASSERT(wp->ksp->owner == wp,
				    ("Write not owner ksp"));
				mtx_unlock(&sc->worklist_mutex);
				g_bde_crypt_write(wp);
				mtx_lock(&sc->worklist_mutex);
				restart++;
				error = g_bde_start_write(wp->sp);
				if (error) {
					g_bde_work_done(wp, error);
					break;
				}
				error = g_bde_start_write(wp->ksp);
				if (wp->error != 0)
					wp->error = error;
				break;
			case BIO_DELETE:
				wp->state = FINISH;
				mtx_unlock(&sc->worklist_mutex);
				g_bde_crypt_delete(wp);
				mtx_lock(&sc->worklist_mutex);
				restart++;
				g_bde_start_write(wp->sp);
				break;
			}
			if (restart)
				break;
		}
		if (!restart) {
			/*
			 * We don't look for our death-warrant until we are
			 * idle.  Shouldn't make a difference in practice.
			 */
			if (sc->dead)
				break;
			g_trace(G_T_TOPOLOGY, "g_bde_worker sleep");
			error = msleep(sc, &sc->worklist_mutex,
			    PRIBIO, "-", hz);
			if (error == EWOULDBLOCK) {
				/*
				 * Lose our skey cache in an orderly fashion.
				 * The exact rate can be tuned to be less
				 * aggressive if this is desirable.  10% per
				 * second means that the cache is gone in a
				 * few minutes.
				 */
				g_bde_purge_sector(sc, 10);
			}
		}
	}
	g_trace(G_T_TOPOLOGY, "g_bde_worker die");
	g_bde_purge_sector(sc, 1);
	KASSERT(sc->nwork == 0, ("Dead but %d work remaining", sc->nwork));
	KASSERT(sc->ncache == 0, ("Dead but %d cache remaining", sc->ncache));
	KASSERT(sc->nsect == 0, ("Dead but %d sect remaining", sc->nsect));
	mtx_unlock(&sc->worklist_mutex);
	sc->dead = 2;
	wakeup(sc);
	kproc_exit(0);
}

/*
 * g_bde_start1 has chopped the incoming request up so all the requests
 * we see here are inside a single zone.  Map the data and key locations
 * grab the buffers we need and fire off the first volley of read requests.
 */

static void
g_bde_start2(struct g_bde_work *wp)
{
	struct g_bde_softc *sc;

	KASSERT(wp != NULL, ("NULL wp in g_bde_start2"));
	KASSERT(wp->softc != NULL, ("NULL wp->softc"));
	g_trace(G_T_TOPOLOGY, "g_bde_start2(%p)", wp);
	sc = wp->softc;
	switch (wp->bp->bio_cmd) {
	case BIO_READ:
		wp->sp = g_bde_new_sector(wp, 0);
		if (wp->sp == NULL) {
			g_bde_work_done(wp, ENOMEM);
			return;
		}
		wp->sp->size = wp->length;
		wp->sp->data = wp->data;
		if (g_bde_start_read(wp->sp) != 0) {
			g_bde_work_done(wp, ENOMEM);
			return;
		}
		g_bde_read_keysector(sc, wp);
		if (wp->ksp == NULL)
			wp->error = ENOMEM;
		break;
	case BIO_DELETE:
		wp->sp = g_bde_new_sector(wp, wp->length);
		if (wp->sp == NULL) {
			g_bde_work_done(wp, ENOMEM);
			return;
		}
		break;
	case BIO_WRITE:
		wp->sp = g_bde_new_sector(wp, wp->length);
		if (wp->sp == NULL) {
			g_bde_work_done(wp, ENOMEM);
			return;
		}
		g_bde_read_keysector(sc, wp);
		if (wp->ksp == NULL) {
			g_bde_work_done(wp, ENOMEM);
			return;
		}
		break;
	default:
		KASSERT(0 == 1, 
		    ("Wrong bio_cmd %d in g_bde_start2", wp->bp->bio_cmd));
	}

	wp->state = WAIT;
	wakeup(sc);
}

/*
 * Create a sequence of work structures, and have g_bde_map_sector() determine
 * how long they each can be.  Feed them to g_bde_start2().
 */

void
g_bde_start1(struct bio *bp)
{
	struct g_bde_softc *sc;
	struct g_bde_work *wp;
	off_t done;

	sc = bp->bio_to->geom->softc;
	bp->bio_driver1 = sc;

	mtx_lock(&sc->worklist_mutex);
	for(done = 0; done < bp->bio_length; ) {
		wp = g_bde_new_work(sc);
		if (wp != NULL) {
			wp->bp = bp;
			wp->offset = bp->bio_offset + done;
			wp->data = bp->bio_data + done;
			wp->length = bp->bio_length - done;
			g_bde_map_sector(wp);
			done += wp->length;
			g_bde_start2(wp);
		}
		if (wp == NULL || bp->bio_error != 0) {
			g_bde_contribute(bp, bp->bio_length - done, ENOMEM);
			break;
		}
	}
	mtx_unlock(&sc->worklist_mutex);
	return;
}
