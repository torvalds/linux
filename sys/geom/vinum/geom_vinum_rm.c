/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2004, 2007 Lukas Ertl
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
#include <sys/libkern.h>
#include <sys/malloc.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>

/* General 'remove' routine. */
void
gv_remove(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_softc *sc;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_sd *s;
	struct gv_drive *d;
	int *argc, *flags;
	char *argv, buf[20];
	int i, type;

	argc = gctl_get_paraml(req, "argc", sizeof(*argc));

	if (argc == NULL || *argc == 0) {
		gctl_error(req, "no arguments given");
		return;
	}

	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	if (flags == NULL) {
		gctl_error(req, "no flags given");
		return;
	}

	sc = gp->softc;

	/* XXX config locking */

	for (i = 0; i < *argc; i++) {
		snprintf(buf, sizeof(buf), "argv%d", i);
		argv = gctl_get_param(req, buf, NULL);
		if (argv == NULL)
			continue;
		type = gv_object_type(sc, argv);
		switch (type) {
		case GV_TYPE_VOL:
			v = gv_find_vol(sc, argv);

			/*
			 * If this volume has plexes, we want a recursive
			 * removal.
			 */
			if (!LIST_EMPTY(&v->plexes) && !(*flags & GV_FLAG_R)) {
				gctl_error(req, "volume '%s' has attached "
				    "plexes - need recursive removal", v->name);
				return;
			}

			gv_post_event(sc, GV_EVENT_RM_VOLUME, v, NULL, 0, 0);
			break;

		case GV_TYPE_PLEX:
			p = gv_find_plex(sc, argv);

			/*
			 * If this plex has subdisks, we want a recursive
			 * removal.
			 */
			if (!LIST_EMPTY(&p->subdisks) &&
			    !(*flags & GV_FLAG_R)) {
				gctl_error(req, "plex '%s' has attached "
				    "subdisks - need recursive removal",
				    p->name);
				return;
			}

			/* Don't allow removal of the only plex of a volume. */
			if (p->vol_sc != NULL && p->vol_sc->plexcount == 1) {
				gctl_error(req, "plex '%s' is still attached "
				    "to volume '%s'", p->name, p->volume);
				return;
			}

			gv_post_event(sc, GV_EVENT_RM_PLEX, p, NULL, 0, 0);
			break;

		case GV_TYPE_SD:
			s = gv_find_sd(sc, argv);

			/* Don't allow removal if attached to a plex. */
			if (s->plex_sc != NULL) {
				gctl_error(req, "subdisk '%s' is still attached"
				    " to plex '%s'", s->name, s->plex_sc->name);
				return;
			}

			gv_post_event(sc, GV_EVENT_RM_SD, s, NULL, 0, 0);
			break;

		case GV_TYPE_DRIVE:
			d = gv_find_drive(sc, argv);
			/* We don't allow to remove open drives. */
			if (gv_consumer_is_open(d->consumer) &&
			    !(*flags & GV_FLAG_F)) {
				gctl_error(req, "drive '%s' is open", d->name);
				return;
			}

			/* A drive with subdisks needs a recursive removal. */
/*			if (!LIST_EMPTY(&d->subdisks) &&
			    !(*flags & GV_FLAG_R)) {
				gctl_error(req, "drive '%s' still has subdisks"
				    " - need recursive removal", d->name);
				return;
			}*/

			gv_post_event(sc, GV_EVENT_RM_DRIVE, d, NULL, *flags,
			    0);
			break;

		default:
			gctl_error(req, "unknown object '%s'", argv);
			return;
		}
	}

	gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
}

/* Resets configuration */
int
gv_resetconfig(struct gv_softc *sc)
{
	struct gv_drive *d, *d2;
	struct gv_volume *v, *v2;
	struct gv_plex *p, *p2;
	struct gv_sd *s, *s2;

	/* First make sure nothing is open. */
        LIST_FOREACH_SAFE(d, &sc->drives, drive, d2) {
		if (gv_consumer_is_open(d->consumer)) {
			return (GV_ERR_ISBUSY);
		}
	}

	/* Make sure nothing is going on internally. */
	LIST_FOREACH_SAFE(p, &sc->plexes, plex, p2) {
		if (p->flags & (GV_PLEX_REBUILDING | GV_PLEX_GROWING))
			return (GV_ERR_ISBUSY);
	}

	/* Then if not, we remove everything. */
	LIST_FOREACH_SAFE(s, &sc->subdisks, sd, s2)
		gv_rm_sd(sc, s);
	LIST_FOREACH_SAFE(d, &sc->drives, drive, d2)
		gv_rm_drive(sc, d, 0);
	LIST_FOREACH_SAFE(p, &sc->plexes, plex, p2)
		gv_rm_plex(sc, p);
	LIST_FOREACH_SAFE(v, &sc->volumes, volume, v2)
		gv_rm_vol(sc, v);

	gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);

	return (0);
}

/* Remove a volume. */
void
gv_rm_vol(struct gv_softc *sc, struct gv_volume *v)
{
	struct g_provider *pp;
	struct gv_plex *p, *p2;

	KASSERT(v != NULL, ("gv_rm_vol: NULL v"));
	pp = v->provider;
	KASSERT(pp != NULL, ("gv_rm_vol: NULL pp"));

	/* Check if any of our consumers is open. */
	if (gv_provider_is_open(pp)) {
		G_VINUM_DEBUG(0, "unable to remove %s: volume still in use",
		    v->name);
		return;
	}

	/* Remove the plexes our volume has. */
	LIST_FOREACH_SAFE(p, &v->plexes, in_volume, p2)
		gv_rm_plex(sc, p);

	/* Clean up. */
	LIST_REMOVE(v, volume);
	g_free(v);

	/* Get rid of the volume's provider. */
	if (pp != NULL) {
		g_topology_lock();
		g_wither_provider(pp, ENXIO);
		g_topology_unlock();
	}
}

/* Remove a plex. */
void
gv_rm_plex(struct gv_softc *sc, struct gv_plex *p)
{
	struct gv_volume *v;
	struct gv_sd *s, *s2;

	KASSERT(p != NULL, ("gv_rm_plex: NULL p"));
	v = p->vol_sc;

	/* Check if any of our consumers is open. */
	if (v != NULL && gv_provider_is_open(v->provider) && v->plexcount < 2) {
		G_VINUM_DEBUG(0, "unable to remove %s: volume still in use",
		    p->name);
		return;
	}

	/* Remove the subdisks our plex has. */
	LIST_FOREACH_SAFE(s, &p->subdisks, in_plex, s2)
		gv_rm_sd(sc, s);

	v = p->vol_sc;
	/* Clean up and let our geom fade away. */
	LIST_REMOVE(p, plex);
	if (p->vol_sc != NULL) {
		p->vol_sc->plexcount--;
		LIST_REMOVE(p, in_volume);
		p->vol_sc = NULL;
		/* Correctly update the volume size. */
		gv_update_vol_size(v, gv_vol_size(v));
	}

	g_free(p);
}

/* Remove a subdisk. */
void
gv_rm_sd(struct gv_softc *sc, struct gv_sd *s)
{
	struct gv_plex *p;
	struct gv_volume *v;

	KASSERT(s != NULL, ("gv_rm_sd: NULL s"));

	p = s->plex_sc;
	v = NULL;

	/* Clean up. */
	if (p != NULL) {
		LIST_REMOVE(s, in_plex);
		s->plex_sc = NULL;
		p->sdcount--;
		/* Update the plexsize. */
		p->size = gv_plex_size(p);
		v = p->vol_sc;
		if (v != NULL) {
			/* Update the size of our plex' volume. */
			gv_update_vol_size(v, gv_vol_size(v));
		}
	}
	if (s->drive_sc && !(s->drive_sc->flags & GV_DRIVE_REFERENCED))
		LIST_REMOVE(s, from_drive);
	LIST_REMOVE(s, sd);
	gv_free_sd(s);
	g_free(s);
}

/* Remove a drive. */
void
gv_rm_drive(struct gv_softc *sc, struct gv_drive *d, int flags)
{
	struct g_consumer *cp;
	struct gv_freelist *fl, *fl2;
	struct gv_plex *p;
	struct gv_sd *s, *s2;
	struct gv_volume *v;
	struct gv_drive *d2;
	int err;

	KASSERT(d != NULL, ("gv_rm_drive: NULL d"));

	cp = d->consumer;

	if (cp != NULL) {
		g_topology_lock();
		err = g_access(cp, 0, 1, 0);
		g_topology_unlock();

		if (err) {
			G_VINUM_DEBUG(0, "%s: unable to access '%s', "
			    "errno: %d", __func__, cp->provider->name, err);
			return;
		}

		/* Clear the Vinum Magic. */
		d->hdr->magic = GV_NOMAGIC;
		err = gv_write_header(cp, d->hdr);
		if (err)
			G_VINUM_DEBUG(0, "gv_rm_drive: error writing header to"
			    " '%s', errno: %d", cp->provider->name, err);

		g_topology_lock();
		g_access(cp, -cp->acr, -cp->acw, -cp->ace);
		g_detach(cp);
		g_destroy_consumer(cp);
		g_topology_unlock();
	}

	/* Remove all associated subdisks, plexes, volumes. */
	if (flags & GV_FLAG_R) {
		if (!LIST_EMPTY(&d->subdisks)) {
			LIST_FOREACH_SAFE(s, &d->subdisks, from_drive, s2) {
				p = s->plex_sc;
				if (p != NULL) {
					v = p->vol_sc;
					if (v != NULL)
						gv_rm_vol(sc, v);
				}
			}
		}
	}

	/* Clean up. */
	LIST_FOREACH_SAFE(fl, &d->freelist, freelist, fl2) {
		LIST_REMOVE(fl, freelist);
		g_free(fl);
	}

	LIST_REMOVE(d, drive);
	g_free(d->hdr);

	/* Put ourself into referenced state if we have subdisks. */
	if (d->sdcount > 0) {
		d->consumer = NULL;
		d->hdr = NULL;
		d->flags |= GV_DRIVE_REFERENCED;
		snprintf(d->device, sizeof(d->device), "???");
		d->size = 0;
		d->avail = 0;
		d->freelist_entries = 0;
		LIST_FOREACH(s, &d->subdisks, from_drive) {
			s->flags |= GV_SD_TASTED;
			gv_set_sd_state(s, GV_SD_DOWN, GV_SETSTATE_FORCE);
		}
		/* Shuffle around so we keep gv_is_newer happy. */
		LIST_REMOVE(d, drive);
		d2 = LIST_FIRST(&sc->drives);
		if (d2 == NULL)
			LIST_INSERT_HEAD(&sc->drives, d, drive);
		else
			LIST_INSERT_AFTER(d2, d, drive);
		return;
	}
	g_free(d);

	gv_save_config(sc);
}
