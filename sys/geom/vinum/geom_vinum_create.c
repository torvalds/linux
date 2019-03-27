/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Lukas Ertl
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
#include <sys/conf.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>

#define DEFAULT_STRIPESIZE	262144

/*
 * Create a new drive object, either by user request, during taste of the drive
 * itself, or because it was referenced by a subdisk during taste.
 */
int
gv_create_drive(struct gv_softc *sc, struct gv_drive *d)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_consumer *cp, *cp2;
	struct gv_drive *d2;
	struct gv_hdr *hdr;
	struct gv_freelist *fl;

	KASSERT(d != NULL, ("gv_create_drive: NULL d"));

	gp = sc->geom;

	pp = NULL;
	cp = cp2 = NULL;

	/* The drive already has a consumer if it was tasted before. */
	if (d->consumer != NULL) {
		cp = d->consumer;
		cp->private = d;
		pp = cp->provider;
	} else if (!(d->flags & GV_DRIVE_REFERENCED)) {
		if (gv_find_drive(sc, d->name) != NULL) {
			G_VINUM_DEBUG(0, "drive '%s' already exists", d->name);
			g_free(d);
			return (GV_ERR_CREATE);
		}

		if (gv_find_drive_device(sc, d->device) != NULL) {
			G_VINUM_DEBUG(0, "provider '%s' already in use by "
			    "gvinum", d->device);
			return (GV_ERR_CREATE);
		}

		pp = g_provider_by_name(d->device);
		if (pp == NULL) {
			G_VINUM_DEBUG(0, "create '%s': device '%s' disappeared",
			    d->name, d->device);
			g_free(d);
			return (GV_ERR_CREATE);
		}

		g_topology_lock();
		cp = g_new_consumer(gp);
		if (g_attach(cp, pp) != 0) {
			g_destroy_consumer(cp);
			g_topology_unlock();
			G_VINUM_DEBUG(0, "create drive '%s': unable to attach",
			    d->name);
			g_free(d);
			return (GV_ERR_CREATE);
		}
		g_topology_unlock();

		d->consumer = cp;
		cp->private = d;
	}

	/*
	 * If this was just a "referenced" drive, we're almost finished, but
	 * insert this drive not on the head of the drives list, as
	 * gv_drive_is_newer() expects a "real" drive from LIST_FIRST().
	 */
	if (d->flags & GV_DRIVE_REFERENCED) {
		snprintf(d->device, sizeof(d->device), "???");
		d2 = LIST_FIRST(&sc->drives);
		if (d2 == NULL)
			LIST_INSERT_HEAD(&sc->drives, d, drive);
		else
			LIST_INSERT_AFTER(d2, d, drive);
		return (0);
	}

	/*
	 * Update access counts of the new drive to those of an already
	 * existing drive.
	 */
	LIST_FOREACH(d2, &sc->drives, drive) {
		if ((d == d2) || (d2->consumer == NULL))
			continue;

		cp2 = d2->consumer;
		g_topology_lock();
		if ((cp2->acr || cp2->acw || cp2->ace) &&
		    (g_access(cp, cp2->acr, cp2->acw, cp2->ace) != 0)) {
			g_detach(cp);
			g_destroy_consumer(cp);
			g_topology_unlock();
			G_VINUM_DEBUG(0, "create drive '%s': unable to update "
			    "access counts", d->name);
			if (d->hdr != NULL)
				g_free(d->hdr);
			g_free(d);
			return (GV_ERR_CREATE);
		}
		g_topology_unlock();
		break;
	}

	d->size = pp->mediasize - GV_DATA_START;
	d->avail = d->size;
	d->vinumconf = sc;
	LIST_INIT(&d->subdisks);
	LIST_INIT(&d->freelist);

	/* The header might have been set during taste. */
	if (d->hdr == NULL) {
		hdr = g_malloc(sizeof(*hdr), M_WAITOK | M_ZERO);
		hdr->magic = GV_MAGIC;
		hdr->config_length = GV_CFG_LEN;
		getcredhostname(NULL, hdr->label.sysname, GV_HOSTNAME_LEN);
		strlcpy(hdr->label.name, d->name, sizeof(hdr->label.name));
		microtime(&hdr->label.date_of_birth);
		d->hdr = hdr;
	}

	/* We also need a freelist entry. */
	fl = g_malloc(sizeof(struct gv_freelist), M_WAITOK | M_ZERO);
	fl->offset = GV_DATA_START;
	fl->size = d->avail;
	LIST_INSERT_HEAD(&d->freelist, fl, freelist);
	d->freelist_entries = 1;

	if (gv_find_drive(sc, d->name) == NULL)
		LIST_INSERT_HEAD(&sc->drives, d, drive);

	gv_set_drive_state(d, GV_DRIVE_UP, 0);
	return (0);
}

int
gv_create_volume(struct gv_softc *sc, struct gv_volume *v)
{
	KASSERT(v != NULL, ("gv_create_volume: NULL v"));

	v->vinumconf = sc;
	v->flags |= GV_VOL_NEWBORN;
	LIST_INIT(&v->plexes);
	LIST_INSERT_HEAD(&sc->volumes, v, volume);
	v->wqueue = g_malloc(sizeof(struct bio_queue_head), M_WAITOK | M_ZERO);
	bioq_init(v->wqueue);
	return (0);
}

int
gv_create_plex(struct gv_softc *sc, struct gv_plex *p)
{
	struct gv_volume *v;

	KASSERT(p != NULL, ("gv_create_plex: NULL p"));

	/* Find the volume this plex should be attached to. */
	v = gv_find_vol(sc, p->volume);
	if (v == NULL) {
		G_VINUM_DEBUG(0, "create plex '%s': volume '%s' not found",
		    p->name, p->volume);
		g_free(p);
		return (GV_ERR_CREATE);
	}
	if (!(v->flags & GV_VOL_NEWBORN))
		p->flags |= GV_PLEX_ADDED;
	p->vol_sc = v;
	v->plexcount++;
	p->vinumconf = sc;
	p->synced = 0;
	p->flags |= GV_PLEX_NEWBORN;
	LIST_INSERT_HEAD(&v->plexes, p, in_volume);
	LIST_INIT(&p->subdisks);
	TAILQ_INIT(&p->packets);
	LIST_INSERT_HEAD(&sc->plexes, p, plex);
	p->bqueue = g_malloc(sizeof(struct bio_queue_head), M_WAITOK | M_ZERO);
	bioq_init(p->bqueue);
	p->wqueue = g_malloc(sizeof(struct bio_queue_head), M_WAITOK | M_ZERO);
	bioq_init(p->wqueue);
	p->rqueue = g_malloc(sizeof(struct bio_queue_head), M_WAITOK | M_ZERO);
	bioq_init(p->rqueue);
	return (0);
}

int
gv_create_sd(struct gv_softc *sc, struct gv_sd *s)
{
	struct gv_plex *p;
	struct gv_drive *d;

	KASSERT(s != NULL, ("gv_create_sd: NULL s"));

	/* Find the drive where this subdisk should be put on. */
	d = gv_find_drive(sc, s->drive);
	if (d == NULL) {
		/*
		 * It's possible that the subdisk references a drive that
		 * doesn't exist yet (during the taste process), so create a
		 * practically empty "referenced" drive.
		 */
		if (s->flags & GV_SD_TASTED) {
			d = g_malloc(sizeof(struct gv_drive),
			    M_WAITOK | M_ZERO);
			d->flags |= GV_DRIVE_REFERENCED;
			strlcpy(d->name, s->drive, sizeof(d->name));
			gv_create_drive(sc, d);
		} else {
			G_VINUM_DEBUG(0, "create sd '%s': drive '%s' not found",
			    s->name, s->drive);
			g_free(s);
			return (GV_ERR_CREATE);
		}
	}

	/* Find the plex where this subdisk belongs to. */
	p = gv_find_plex(sc, s->plex);
	if (p == NULL) {
		G_VINUM_DEBUG(0, "create sd '%s': plex '%s' not found",
		    s->name, s->plex);
		g_free(s);
		return (GV_ERR_CREATE);
	}

	/*
	 * First we give the subdisk to the drive, to handle autosized
	 * values ...
	 */
	if (gv_sd_to_drive(s, d) != 0) {
		g_free(s);
		return (GV_ERR_CREATE);
	}

	/*
	 * Then, we give the subdisk to the plex; we check if the
	 * given values are correct and maybe adjust them.
	 */
	if (gv_sd_to_plex(s, p) != 0) {
		G_VINUM_DEBUG(0, "unable to give sd '%s' to plex '%s'",
		    s->name, p->name);
		if (s->drive_sc && !(s->drive_sc->flags & GV_DRIVE_REFERENCED))
			LIST_REMOVE(s, from_drive);
		gv_free_sd(s);
		g_free(s);
		/*
		 * If this subdisk can't be created, we won't create
		 * the attached plex either, if it is also a new one.
		 */
		if (!(p->flags & GV_PLEX_NEWBORN))
			return (GV_ERR_CREATE);
		gv_rm_plex(sc, p);
		return (GV_ERR_CREATE);
	}
	s->flags |= GV_SD_NEWBORN;

	s->vinumconf = sc;
	LIST_INSERT_HEAD(&sc->subdisks, s, sd);

	return (0);
}

/*
 * Create a concatenated volume from specified drives or drivegroups.
 */
void
gv_concat(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_drive *d;
	struct gv_sd *s;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_softc *sc;
	char *drive, buf[30], *vol;
	int *drives, dcount;

	sc = gp->softc;
	dcount = 0;
	vol = gctl_get_param(req, "name", NULL);
	if (vol == NULL) {
		gctl_error(req, "volume name not given");	
		return;
	}

	drives = gctl_get_paraml(req, "drives", sizeof(*drives));

	if (drives == NULL) { 
		gctl_error(req, "drive names not given");
		return;
	}

	/* First we create the volume. */
	v = g_malloc(sizeof(*v), M_WAITOK | M_ZERO);
	strlcpy(v->name, vol, sizeof(v->name));
	v->state = GV_VOL_UP;
	gv_post_event(sc, GV_EVENT_CREATE_VOLUME, v, NULL, 0, 0);

	/* Then we create the plex. */
	p = g_malloc(sizeof(*p), M_WAITOK | M_ZERO);
	snprintf(p->name, sizeof(p->name), "%s.p%d", v->name, v->plexcount);
	strlcpy(p->volume, v->name, sizeof(p->volume));
	p->org = GV_PLEX_CONCAT;
	p->stripesize = 0;
	gv_post_event(sc, GV_EVENT_CREATE_PLEX, p, NULL, 0, 0);

	/* Drives are first (right now) priority */
	for (dcount = 0; dcount < *drives; dcount++) {
		snprintf(buf, sizeof(buf), "drive%d", dcount);
		drive = gctl_get_param(req, buf, NULL);
		d = gv_find_drive(sc, drive);
		if (d == NULL) {
			gctl_error(req, "No such drive '%s'", drive);
			continue;
		}
		s = g_malloc(sizeof(*s), M_WAITOK | M_ZERO);
		snprintf(s->name, sizeof(s->name), "%s.s%d", p->name, dcount);
		strlcpy(s->plex, p->name, sizeof(s->plex));
		strlcpy(s->drive, drive, sizeof(s->drive));
		s->plex_offset = -1;
		s->drive_offset = -1;
		s->size = -1;
		gv_post_event(sc, GV_EVENT_CREATE_SD, s, NULL, 0, 0);
	}
	gv_post_event(sc, GV_EVENT_SETUP_OBJECTS, sc, NULL, 0, 0);
	gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
}

/*
 * Create a mirrored volume from specified drives or drivegroups.
 */
void
gv_mirror(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_drive *d;
	struct gv_sd *s;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_softc *sc;
	char *drive, buf[30], *vol;
	int *drives, *flags, dcount, pcount, scount;

	sc = gp->softc;
	dcount = 0;
	scount = 0;
	pcount = 0;
	vol = gctl_get_param(req, "name", NULL);
	if (vol == NULL) {
		gctl_error(req, "volume name not given");	
		return;
	}

	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	drives = gctl_get_paraml(req, "drives", sizeof(*drives));

	if (drives == NULL) { 
		gctl_error(req, "drive names not given");
		return;
	}

	/* We must have an even number of drives. */
	if (*drives % 2 != 0) {
		gctl_error(req, "mirror organization must have an even number "
		    "of drives");
		return;
	}
	if (*flags & GV_FLAG_S && *drives < 4) {
		gctl_error(req, "must have at least 4 drives for striped plex");
		return;
	}

	/* First we create the volume. */
	v = g_malloc(sizeof(*v), M_WAITOK | M_ZERO);
	strlcpy(v->name, vol, sizeof(v->name));
	v->state = GV_VOL_UP;
	gv_post_event(sc, GV_EVENT_CREATE_VOLUME, v, NULL, 0, 0);

	/* Then we create the plexes. */
	for (pcount = 0; pcount < 2; pcount++) {
		p = g_malloc(sizeof(*p), M_WAITOK | M_ZERO);
		snprintf(p->name, sizeof(p->name), "%s.p%d", v->name,
		    pcount);
		strlcpy(p->volume, v->name, sizeof(p->volume));
		if (*flags & GV_FLAG_S) {
			p->org = GV_PLEX_STRIPED;
			p->stripesize = DEFAULT_STRIPESIZE;
		} else {
			p->org = GV_PLEX_CONCAT;
			p->stripesize = -1;
		}
		gv_post_event(sc, GV_EVENT_CREATE_PLEX, p, NULL, 0, 0);

		/*
		 * We just gives each even drive to plex one, and each odd to
		 * plex two.
		 */
		scount = 0;
		for (dcount = pcount; dcount < *drives; dcount += 2) {
			snprintf(buf, sizeof(buf), "drive%d", dcount);
			drive = gctl_get_param(req, buf, NULL);
			d = gv_find_drive(sc, drive);
			if (d == NULL) {
				gctl_error(req, "No such drive '%s', aborting",
				    drive);
				scount++;
				break;
			}
			s = g_malloc(sizeof(*s), M_WAITOK | M_ZERO);
			snprintf(s->name, sizeof(s->name), "%s.s%d", p->name,
			    scount);
			strlcpy(s->plex, p->name, sizeof(s->plex));
			strlcpy(s->drive, drive, sizeof(s->drive));
			s->plex_offset = -1;
			s->drive_offset = -1;
			s->size = -1;
			gv_post_event(sc, GV_EVENT_CREATE_SD, s, NULL, 0, 0);
			scount++;
		}
	}
	gv_post_event(sc, GV_EVENT_SETUP_OBJECTS, sc, NULL, 0, 0);
	gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
}

void
gv_raid5(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_softc *sc;
	struct gv_drive *d;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_sd *s;
	int *drives, *flags, dcount;
	char *vol, *drive, buf[30];
	off_t *stripesize;

	sc = gp->softc;

	vol = gctl_get_param(req, "name", NULL);
	if (vol == NULL) {
		gctl_error(req, "volume name not given");	
		return;
	}
	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	drives = gctl_get_paraml(req, "drives", sizeof(*drives));
	stripesize = gctl_get_paraml(req, "stripesize", sizeof(*stripesize));

	if (stripesize == NULL) {
		gctl_error(req, "no stripesize given");
		return;
	}

	if (drives == NULL) {
		gctl_error(req, "drive names not given");
		return;
	}

	/* We must have at least three drives. */
	if (*drives < 3) {
		gctl_error(req, "must have at least three drives for this "
		    "plex organisation");
		return;
	}
	/* First we create the volume. */
	v = g_malloc(sizeof(*v), M_WAITOK | M_ZERO);
	strlcpy(v->name, vol, sizeof(v->name));
	v->state = GV_VOL_UP;
	gv_post_event(sc, GV_EVENT_CREATE_VOLUME, v, NULL, 0, 0);

	/* Then we create the plex. */
	p = g_malloc(sizeof(*p), M_WAITOK | M_ZERO);
	snprintf(p->name, sizeof(p->name), "%s.p%d", v->name, v->plexcount);
	strlcpy(p->volume, v->name, sizeof(p->volume));
	p->org = GV_PLEX_RAID5;
	p->stripesize = *stripesize;
	gv_post_event(sc, GV_EVENT_CREATE_PLEX, p, NULL, 0, 0);

	/* Create subdisks on drives. */
	for (dcount = 0; dcount < *drives; dcount++) {
		snprintf(buf, sizeof(buf), "drive%d", dcount);
		drive = gctl_get_param(req, buf, NULL);
		d = gv_find_drive(sc, drive);
		if (d == NULL) {
			gctl_error(req, "No such drive '%s'", drive);
			continue;
		}
		s = g_malloc(sizeof(*s), M_WAITOK | M_ZERO);
		snprintf(s->name, sizeof(s->name), "%s.s%d", p->name, dcount);
		strlcpy(s->plex, p->name, sizeof(s->plex));
		strlcpy(s->drive, drive, sizeof(s->drive));
		s->plex_offset = -1;
		s->drive_offset = -1;
		s->size = -1;
		gv_post_event(sc, GV_EVENT_CREATE_SD, s, NULL, 0, 0);
	}
	gv_post_event(sc, GV_EVENT_SETUP_OBJECTS, sc, NULL, 0, 0);
	gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
}

/*
 * Create a striped volume from specified drives or drivegroups.
 */
void
gv_stripe(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_drive *d;
	struct gv_sd *s;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_softc *sc;
	char *drive, buf[30], *vol;
	int *drives, *flags, dcount, pcount;

	sc = gp->softc;
	dcount = 0;
	pcount = 0;
	vol = gctl_get_param(req, "name", NULL);
	if (vol == NULL) {
		gctl_error(req, "volume name not given");	
		return;
	}
	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	drives = gctl_get_paraml(req, "drives", sizeof(*drives));

	if (drives == NULL) { 
		gctl_error(req, "drive names not given");
		return;
	}

	/* We must have at least two drives. */
	if (*drives < 2) {
		gctl_error(req, "must have at least 2 drives");
		return;
	}

	/* First we create the volume. */
	v = g_malloc(sizeof(*v), M_WAITOK | M_ZERO);
	strlcpy(v->name, vol, sizeof(v->name));
	v->state = GV_VOL_UP;
	gv_post_event(sc, GV_EVENT_CREATE_VOLUME, v, NULL, 0, 0);

	/* Then we create the plex. */
	p = g_malloc(sizeof(*p), M_WAITOK | M_ZERO);
	snprintf(p->name, sizeof(p->name), "%s.p%d", v->name, v->plexcount);
	strlcpy(p->volume, v->name, sizeof(p->volume));
	p->org = GV_PLEX_STRIPED;
	p->stripesize = 262144;
	gv_post_event(sc, GV_EVENT_CREATE_PLEX, p, NULL, 0, 0);

	/* Create subdisks on drives. */
	for (dcount = 0; dcount < *drives; dcount++) {
		snprintf(buf, sizeof(buf), "drive%d", dcount);
		drive = gctl_get_param(req, buf, NULL);
		d = gv_find_drive(sc, drive);
		if (d == NULL) {
			gctl_error(req, "No such drive '%s'", drive);
			continue;
		}
		s = g_malloc(sizeof(*s), M_WAITOK | M_ZERO);
		snprintf(s->name, sizeof(s->name), "%s.s%d", p->name, dcount);
		strlcpy(s->plex, p->name, sizeof(s->plex));
		strlcpy(s->drive, drive, sizeof(s->drive));
		s->plex_offset = -1;
		s->drive_offset = -1;
		s->size = -1;
		gv_post_event(sc, GV_EVENT_CREATE_SD, s, NULL, 0, 0);
	}
	gv_post_event(sc, GV_EVENT_SETUP_OBJECTS, sc, NULL, 0, 0);
	gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
}
