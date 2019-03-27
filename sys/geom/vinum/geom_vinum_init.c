/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
 * Copyright (c) 2007, 2009 Ulf Lilleengen
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/bio.h>
#include <sys/libkern.h>
#include <sys/malloc.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>

static int		 gv_sync(struct gv_volume *);
static int		 gv_rebuild_plex(struct gv_plex *);
static int		 gv_init_plex(struct gv_plex *);
static int		 gv_grow_plex(struct gv_plex *);
static int		 gv_sync_plex(struct gv_plex *, struct gv_plex *);
static struct gv_plex	*gv_find_good_plex(struct gv_volume *);

void
gv_start_obj(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_softc *sc;
	struct gv_volume *v;
	struct gv_plex *p;
	int *argc, *initsize;
	char *argv, buf[20];
	int i, type;

	argc = gctl_get_paraml(req, "argc", sizeof(*argc));
	initsize = gctl_get_paraml(req, "initsize", sizeof(*initsize));

	if (argc == NULL || *argc == 0) {
		gctl_error(req, "no arguments given");
		return;
	}

	sc = gp->softc;

	for (i = 0; i < *argc; i++) {
		snprintf(buf, sizeof(buf), "argv%d", i);
		argv = gctl_get_param(req, buf, NULL);
		if (argv == NULL)
			continue;
		type = gv_object_type(sc, argv);
		switch (type) {
		case GV_TYPE_VOL:
			v = gv_find_vol(sc, argv);
			if (v != NULL)
				gv_post_event(sc, GV_EVENT_START_VOLUME, v,
				    NULL, *initsize, 0);
			break;

		case GV_TYPE_PLEX:
			p = gv_find_plex(sc, argv);
			if (p != NULL)
				gv_post_event(sc, GV_EVENT_START_PLEX, p, NULL,
				    *initsize, 0);
			break;

		case GV_TYPE_SD:
		case GV_TYPE_DRIVE:
			/* XXX Not implemented, but what is the use? */
			gctl_error(req, "unable to start '%s' - not yet supported",
			    argv);
			return;
		default:
			gctl_error(req, "unknown object '%s'", argv);
			return;
		}
	}
}

int
gv_start_plex(struct gv_plex *p)
{
	struct gv_volume *v;
	struct gv_plex *up;
	struct gv_sd *s;
	int error;

	KASSERT(p != NULL, ("gv_start_plex: NULL p"));

	error = 0;
	v = p->vol_sc;

	/* RAID5 plexes can either be init, rebuilt or grown. */
	if (p->org == GV_PLEX_RAID5) {
		if (p->state > GV_PLEX_DEGRADED) {
			LIST_FOREACH(s, &p->subdisks, in_plex) {
				if (s->flags & GV_SD_GROW) {
					error = gv_grow_plex(p);
					return (error);
				}
			}
		} else if (p->state == GV_PLEX_DEGRADED) {
			error = gv_rebuild_plex(p);
		} else
			error = gv_init_plex(p);
	} else {
		/* We want to sync from the other plex if we're down. */
		if (p->state == GV_PLEX_DOWN && v->plexcount > 1) {
			up = gv_find_good_plex(v);
			if (up == NULL) {
				G_VINUM_DEBUG(1, "unable to find a good plex");
				return (ENXIO);
			}
			g_topology_lock();
			error = gv_access(v->provider, 1, 1, 0);
			if (error) {
				g_topology_unlock();
				G_VINUM_DEBUG(0, "sync from '%s' failed to "
				    "access volume: %d", up->name, error);
				return (error);
			}
			g_topology_unlock();
			error = gv_sync_plex(p, up);
			if (error)
				return (error);
		/*
		 * In case we have a stripe that is up, check whether it can be
		 * grown.
		 */
		} else if (p->org == GV_PLEX_STRIPED &&
		    p->state != GV_PLEX_DOWN) {
			LIST_FOREACH(s, &p->subdisks, in_plex) {
				if (s->flags & GV_SD_GROW) {
					error = gv_grow_plex(p);
					break;
				}
			}
		}
	}
	return (error);
}

int
gv_start_vol(struct gv_volume *v)
{
	struct gv_plex *p;
	int error;

	KASSERT(v != NULL, ("gv_start_vol: NULL v"));

	error = 0;

	if (v->plexcount == 0)
		return (ENXIO);

	else if (v->plexcount == 1) {
		p = LIST_FIRST(&v->plexes);
		KASSERT(p != NULL, ("gv_start_vol: NULL p on %s", v->name));
		error = gv_start_plex(p);
	} else
		error = gv_sync(v);

	return (error);
}

/* Sync a plex p from the plex up.  */
static int
gv_sync_plex(struct gv_plex *p, struct gv_plex *up)
{
	int error;

	KASSERT(p != NULL, ("%s: NULL p", __func__));
	KASSERT(up != NULL, ("%s: NULL up", __func__));
	if ((p == up) || (p->state == GV_PLEX_UP))
		return (0);
	if (p->flags & GV_PLEX_SYNCING ||
	    p->flags & GV_PLEX_REBUILDING ||
	    p->flags & GV_PLEX_GROWING) {
		return (EINPROGRESS);
	}
	p->synced = 0;
	p->flags |= GV_PLEX_SYNCING;
	G_VINUM_DEBUG(1, "starting sync of plex %s", p->name);
	error = gv_sync_request(up, p, p->synced, 
	    MIN(GV_DFLT_SYNCSIZE, up->size - p->synced), 
	    BIO_READ, NULL);
	if (error) {
		G_VINUM_DEBUG(0, "error syncing plex %s", p->name);
		return (error);
	}
	return (0);
}

/* Return a good plex from volume v. */
static struct gv_plex *
gv_find_good_plex(struct gv_volume *v)
{
	struct gv_plex *up;

	/* Find the plex that's up. */
	up = NULL;
	LIST_FOREACH(up, &v->plexes, in_volume) {
		if (up->state == GV_PLEX_UP)
			break;
	}
	/* Didn't find a good plex. */
	return (up);
}

static int
gv_sync(struct gv_volume *v)
{
	struct gv_softc *sc;
	struct gv_plex *p, *up;
	int error;

	KASSERT(v != NULL, ("gv_sync: NULL v"));
	sc = v->vinumconf;
	KASSERT(sc != NULL, ("gv_sync: NULL sc on %s", v->name));


	up = gv_find_good_plex(v);
	if (up == NULL)
		return (ENXIO);
	g_topology_lock();
	error = gv_access(v->provider, 1, 1, 0);
	if (error) {
		g_topology_unlock();
		G_VINUM_DEBUG(0, "sync from '%s' failed to access volume: %d",
		    up->name, error);
		return (error);
	}
	g_topology_unlock();

	/* Go through the good plex, and issue BIO's to all other plexes. */
	LIST_FOREACH(p, &v->plexes, in_volume) {
		error = gv_sync_plex(p, up);
		if (error)
			break;
	}
	return (0);
}

static int
gv_rebuild_plex(struct gv_plex *p)
{
	struct gv_drive *d;
	struct gv_sd *s;
	int error;

	if (p->flags & GV_PLEX_SYNCING ||
	    p->flags & GV_PLEX_REBUILDING ||
	    p->flags & GV_PLEX_GROWING)
		return (EINPROGRESS);
	/*
	 * Make sure that all subdisks have consumers. We won't allow a rebuild
	 * unless every subdisk have one.
	 */
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		d = s->drive_sc;
		if (d == NULL || (d->flags & GV_DRIVE_REFERENCED)) {
			G_VINUM_DEBUG(0, "unable to rebuild %s, subdisk(s) have"
			    " no drives", p->name);
			return (ENXIO);
		}
	}
	p->flags |= GV_PLEX_REBUILDING;
	p->synced = 0;

	g_topology_assert_not();
	g_topology_lock();
	error = gv_access(p->vol_sc->provider, 1, 1, 0);
	if (error) {
		G_VINUM_DEBUG(0, "unable to access provider");
		return (0);
	}
	g_topology_unlock();

	gv_parity_request(p, GV_BIO_REBUILD, 0);
	return (0);
}

static int
gv_grow_plex(struct gv_plex *p)
{
	struct gv_volume *v;
	struct gv_sd *s;
	off_t origsize, origlength;
	int error, sdcount;

	KASSERT(p != NULL, ("gv_grow_plex: NULL p"));
	v = p->vol_sc;
	KASSERT(v != NULL, ("gv_grow_plex: NULL v"));

	if (p->flags & GV_PLEX_GROWING || 
	    p->flags & GV_PLEX_SYNCING ||
	    p->flags & GV_PLEX_REBUILDING)
		return (EINPROGRESS);
	g_topology_lock();
	error = gv_access(v->provider, 1, 1, 0);
	g_topology_unlock();
	if (error) {
		G_VINUM_DEBUG(0, "unable to access provider");
		return (error);
	}

	/* XXX: This routine with finding origsize is used two other places as
	 * well, so we should create a function for it. */
	sdcount = p->sdcount;
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (s->flags & GV_SD_GROW)
			sdcount--;
	}
	s = LIST_FIRST(&p->subdisks);
	if (s == NULL) {
		G_VINUM_DEBUG(0, "error growing plex without subdisks");
		return (GV_ERR_NOTFOUND);
	}
	p->flags |= GV_PLEX_GROWING;
	origsize = (sdcount - 1) * s->size;
	origlength = (sdcount - 1) * p->stripesize;
	p->synced = 0;
	G_VINUM_DEBUG(1, "starting growing of plex %s", p->name);
	gv_grow_request(p, 0, MIN(origlength, origsize), BIO_READ, NULL);

	return (0);
}

static int
gv_init_plex(struct gv_plex *p)
{
	struct gv_drive *d;
	struct gv_sd *s;
	int error;
	off_t start;
	caddr_t data;

	KASSERT(p != NULL, ("gv_init_plex: NULL p"));

	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (s->state == GV_SD_INITIALIZING)
			return (EINPROGRESS);
		gv_set_sd_state(s, GV_SD_INITIALIZING, GV_SETSTATE_FORCE);
		s->init_size = GV_DFLT_SYNCSIZE;
		start = s->drive_offset + s->initialized;
		d = s->drive_sc;
		if (d == NULL) {
			G_VINUM_DEBUG(0, "subdisk %s has no drive yet", s->name);
			break;
		}
		/*
		 * Take the lock here since we need to avoid a race in
		 * gv_init_request if the BIO is completed before the lock is
		 * released.
		 */
		g_topology_lock();
		error = g_access(d->consumer, 0, 1, 0);
		g_topology_unlock();
		if (error) {
			G_VINUM_DEBUG(0, "error accessing consumer when "
			    "initializing %s", s->name);
			break;
		}
		data = g_malloc(s->init_size, M_WAITOK | M_ZERO);
		gv_init_request(s, start, data, s->init_size);
	}
	return (0);
}
