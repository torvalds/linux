/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2007 Lukas Ertl
 *  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>

void
gv_post_event(struct gv_softc *sc, int event, void *arg1, void *arg2,
    intmax_t arg3, intmax_t arg4)
{
	struct gv_event *ev;

	ev = g_malloc(sizeof(*ev), M_WAITOK | M_ZERO);
	ev->type = event;
	ev->arg1 = arg1;
	ev->arg2 = arg2;
	ev->arg3 = arg3;
	ev->arg4 = arg4;

	mtx_lock(&sc->equeue_mtx);
	TAILQ_INSERT_TAIL(&sc->equeue, ev, events);
	wakeup(sc);
	mtx_unlock(&sc->equeue_mtx);
}

void
gv_worker_exit(struct gv_softc *sc)
{
	struct gv_event *ev;

	ev = g_malloc(sizeof(*ev), M_WAITOK | M_ZERO);
	ev->type = GV_EVENT_THREAD_EXIT;

	mtx_lock(&sc->equeue_mtx);
	TAILQ_INSERT_TAIL(&sc->equeue, ev, events);
	wakeup(sc);
	msleep(sc->worker, &sc->equeue_mtx, PDROP, "gv_wor", 0);
}

struct gv_event *
gv_get_event(struct gv_softc *sc)
{
	struct gv_event *ev;

	KASSERT(sc != NULL, ("NULL sc"));
	mtx_lock(&sc->equeue_mtx);
	ev = TAILQ_FIRST(&sc->equeue);
	mtx_unlock(&sc->equeue_mtx);
	return (ev);
}

void
gv_remove_event(struct gv_softc *sc, struct gv_event *ev)
{

	KASSERT(sc != NULL, ("NULL sc"));
	KASSERT(ev != NULL, ("NULL ev"));
	mtx_lock(&sc->equeue_mtx);
	TAILQ_REMOVE(&sc->equeue, ev, events);
	mtx_unlock(&sc->equeue_mtx);
}

void
gv_drive_tasted(struct gv_softc *sc, struct g_provider *pp)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	struct gv_hdr *hdr;
	struct gv_drive *d;
	char *buf;
	int error;

	hdr = NULL;
	buf = NULL;

	G_VINUM_DEBUG(2, "tasted drive on '%s'", pp->name);
	if ((GV_CFG_OFFSET % pp->sectorsize) != 0 ||
	    (GV_CFG_LEN % pp->sectorsize) != 0) {
		G_VINUM_DEBUG(0, "provider %s has unsupported sectorsize.",
		    pp->name);
		return;
	}

	gp = sc->geom;
	g_topology_lock();
	cp = g_new_consumer(gp);
	if (g_attach(cp, pp) != 0) {
		g_destroy_consumer(cp);
		g_topology_unlock();
		G_VINUM_DEBUG(0, "failed to attach to provider on taste event");
		return;
	}
	if (g_access(cp, 1, 0, 0) != 0) {
		g_detach(cp);
		g_destroy_consumer(cp);
		g_topology_unlock();
		G_VINUM_DEBUG(0, "failed to access consumer on taste event");
		return;
	}
	g_topology_unlock();

	hdr = g_malloc(GV_HDR_LEN, M_WAITOK | M_ZERO);
	/* Read header and on-disk configuration. */
	error = gv_read_header(cp, hdr);
	if (error) {
		G_VINUM_DEBUG(0, "failed to read header during taste");
		goto failed;
	}

	/*
	 * Setup the drive before we parse the on-disk configuration, so that
	 * we already know about the drive then.
	 */
	d = gv_find_drive(sc, hdr->label.name);
	if (d == NULL) {
		d = g_malloc(sizeof(*d), M_WAITOK | M_ZERO);
		strlcpy(d->name, hdr->label.name, sizeof(d->name));
		strlcpy(d->device, pp->name, sizeof(d->device));
	} else if (d->flags & GV_DRIVE_REFERENCED) {
		strlcpy(d->device, pp->name, sizeof(d->device));
		d->flags &= ~GV_DRIVE_REFERENCED;
	} else {
		G_VINUM_DEBUG(2, "drive '%s' is already known", d->name);
		goto failed;
	}

	/* Add the consumer and header to the new drive. */
	d->consumer = cp;
	d->hdr = hdr;
	gv_create_drive(sc, d);

	buf = g_read_data(cp, GV_CFG_OFFSET, GV_CFG_LEN, NULL);
	if (buf == NULL) {
		G_VINUM_DEBUG(0, "failed to read config during taste");
		goto failed;
	}
	gv_parse_config(sc, buf, d);
	g_free(buf);

	g_topology_lock();
	g_access(cp, -1, 0, 0);
	g_topology_unlock();

	gv_setup_objects(sc);
	gv_set_drive_state(d, GV_DRIVE_UP, 0);

	return;

failed:
	if (hdr != NULL)
		g_free(hdr);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_topology_unlock();
}

/*
 * When losing a drive (e.g. hardware failure), we cut down the consumer
 * attached to the underlying device and bring the drive itself to a
 * "referenced" state so that normal tasting could bring it up cleanly if it
 * possibly arrives again.
 */
void
gv_drive_lost(struct gv_softc *sc, struct gv_drive *d)
{
	struct g_consumer *cp;
	struct gv_drive *d2;
	struct gv_sd *s, *s2;
	struct gv_freelist *fl, *fl2;

	gv_set_drive_state(d, GV_DRIVE_DOWN,
	    GV_SETSTATE_FORCE | GV_SETSTATE_CONFIG);

	cp = d->consumer;

	if (cp != NULL) {
		if (cp->nstart != cp->nend) {
			G_VINUM_DEBUG(0, "dead drive '%s' has still active "
			    "requests, unable to detach consumer", d->name);
			gv_post_event(sc, GV_EVENT_DRIVE_LOST, d, NULL, 0, 0);
			return;
		}
		g_topology_lock();
		if (cp->acr != 0 || cp->acw != 0 || cp->ace != 0)
			g_access(cp, -cp->acr, -cp->acw, -cp->ace);
		g_detach(cp);
		g_destroy_consumer(cp);
		g_topology_unlock();
	}

	LIST_FOREACH_SAFE(fl, &d->freelist, freelist, fl2) {
		LIST_REMOVE(fl, freelist);
		g_free(fl);
	}

	d->consumer = NULL;
	g_free(d->hdr);
	d->hdr = NULL;
	d->flags |= GV_DRIVE_REFERENCED;
	snprintf(d->device, sizeof(d->device), "???");
	d->size = 0;
	d->avail = 0;
	d->freelist_entries = 0;
	d->sdcount = 0;

	/* Put the subdisk in tasted mode, and remove from drive list. */
	LIST_FOREACH_SAFE(s, &d->subdisks, from_drive, s2) {
		LIST_REMOVE(s, from_drive);
		s->flags |= GV_SD_TASTED;
	}

	/*
	 * Don't forget that gv_is_newer wants a "real" drive at the beginning
	 * of the list, so, just to be safe, we shuffle around.
	 */
	LIST_REMOVE(d, drive);
	d2 = LIST_FIRST(&sc->drives);
	if (d2 == NULL)
		LIST_INSERT_HEAD(&sc->drives, d, drive);
	else
		LIST_INSERT_AFTER(d2, d, drive);
	gv_save_config(sc);
}
