/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2004, 2007 Lukas Ertl
 *  Copyright (c) 2007, 2009 Ulf Lilleengen
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
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>
#include <geom/vinum/geom_vinum_raid5.h>

SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, vinum, CTLFLAG_RW, 0,
    "GEOM_VINUM stuff");
u_int g_vinum_debug = 0;
SYSCTL_UINT(_kern_geom_vinum, OID_AUTO, debug, CTLFLAG_RWTUN, &g_vinum_debug, 0,
    "Debug level");

static int	gv_create(struct g_geom *, struct gctl_req *);
static void	gv_attach(struct gv_softc *, struct gctl_req *);
static void	gv_detach(struct gv_softc *, struct gctl_req *);
static void	gv_parityop(struct gv_softc *, struct gctl_req *);


static void
gv_orphan(struct g_consumer *cp)
{
	struct g_geom *gp;
	struct gv_softc *sc;
	struct gv_drive *d;
	
	g_topology_assert();

	KASSERT(cp != NULL, ("gv_orphan: null cp"));
	gp = cp->geom;
	KASSERT(gp != NULL, ("gv_orphan: null gp"));
	sc = gp->softc;
	KASSERT(sc != NULL, ("gv_orphan: null sc"));
	d = cp->private;
	KASSERT(d != NULL, ("gv_orphan: null d"));

	g_trace(G_T_TOPOLOGY, "gv_orphan(%s)", gp->name);

	gv_post_event(sc, GV_EVENT_DRIVE_LOST, d, NULL, 0, 0);
}

void
gv_start(struct bio *bp)
{
	struct g_geom *gp;
	struct gv_softc *sc;
	
	gp = bp->bio_to->geom;
	sc = gp->softc;

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
		break;
	case BIO_GETATTR:
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}
	mtx_lock(&sc->bqueue_mtx);
	bioq_disksort(sc->bqueue_down, bp);
	wakeup(sc);
	mtx_unlock(&sc->bqueue_mtx);
}

void
gv_done(struct bio *bp)
{
	struct g_geom *gp;
	struct gv_softc *sc;
	
	KASSERT(bp != NULL, ("NULL bp"));

	gp = bp->bio_from->geom;
	sc = gp->softc;

	mtx_lock(&sc->bqueue_mtx);
	bioq_disksort(sc->bqueue_up, bp);
	wakeup(sc);
	mtx_unlock(&sc->bqueue_mtx);
}

int
gv_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct gv_softc *sc;
	struct gv_drive *d, *d2;
	int error;
	
	gp = pp->geom;
	sc = gp->softc;
	/*
	 * We want to modify the read count with the write count in case we have
	 * plexes in a RAID-5 organization.
	 */
	dr += dw;

	LIST_FOREACH(d, &sc->drives, drive) {
		if (d->consumer == NULL)
			continue;
		error = g_access(d->consumer, dr, dw, de);
		if (error) {
			LIST_FOREACH(d2, &sc->drives, drive) {
				if (d == d2)
					break;
				g_access(d2->consumer, -dr, -dw, -de);
			}
			G_VINUM_DEBUG(0, "g_access '%s' failed: %d", d->name,
			    error);
			return (error);
		}
	}
	return (0);
}

static void
gv_init(struct g_class *mp)
{
	struct g_geom *gp;
	struct gv_softc *sc;

	g_trace(G_T_TOPOLOGY, "gv_init(%p)", mp);

	gp = g_new_geomf(mp, "VINUM");
	gp->spoiled = gv_orphan;
	gp->orphan = gv_orphan;
	gp->access = gv_access;
	gp->start = gv_start;
	gp->softc = g_malloc(sizeof(struct gv_softc), M_WAITOK | M_ZERO);
	sc = gp->softc;
	sc->geom = gp;
	sc->bqueue_down = g_malloc(sizeof(struct bio_queue_head),
	    M_WAITOK | M_ZERO);
	sc->bqueue_up = g_malloc(sizeof(struct bio_queue_head),
	    M_WAITOK | M_ZERO);
	bioq_init(sc->bqueue_down);
	bioq_init(sc->bqueue_up);
	LIST_INIT(&sc->drives);
	LIST_INIT(&sc->subdisks);
	LIST_INIT(&sc->plexes);
	LIST_INIT(&sc->volumes);
	TAILQ_INIT(&sc->equeue);
	mtx_init(&sc->config_mtx, "gv_config", NULL, MTX_DEF);
	mtx_init(&sc->equeue_mtx, "gv_equeue", NULL, MTX_DEF);
	mtx_init(&sc->bqueue_mtx, "gv_bqueue", NULL, MTX_DEF);
	kproc_create(gv_worker, sc, &sc->worker, 0, 0, "gv_worker");
}

static int
gv_unload(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{
	struct gv_softc *sc;

	g_trace(G_T_TOPOLOGY, "gv_unload(%p)", mp);

	g_topology_assert();
	sc = gp->softc;

	if (sc != NULL) {
		gv_worker_exit(sc);
		gp->softc = NULL;
		g_wither_geom(gp, ENXIO);
	}

	return (0);
}

/* Handle userland request of attaching object. */
static void
gv_attach(struct gv_softc *sc, struct gctl_req *req)
{
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_sd *s;
	off_t *offset;
	int *rename, type_child, type_parent;
	char *child, *parent;

	child = gctl_get_param(req, "child", NULL);
	if (child == NULL) {
		gctl_error(req, "no child given");
		return;
	}
	parent = gctl_get_param(req, "parent", NULL);
	if (parent == NULL) {
		gctl_error(req, "no parent given");
		return;
	}
	offset = gctl_get_paraml(req, "offset", sizeof(*offset));
	if (offset == NULL) {
		gctl_error(req, "no offset given");
		return;
	}
	rename = gctl_get_paraml(req, "rename", sizeof(*rename));
	if (rename == NULL) {
		gctl_error(req, "no rename flag given");
		return;
	}

	type_child = gv_object_type(sc, child);
	type_parent = gv_object_type(sc, parent);

	switch (type_child) {
	case GV_TYPE_PLEX:
		if (type_parent != GV_TYPE_VOL) {
			gctl_error(req, "no such volume to attach to");
			return;
		}
		v = gv_find_vol(sc, parent);
		p = gv_find_plex(sc, child);
		gv_post_event(sc, GV_EVENT_ATTACH_PLEX, p, v, *offset, *rename);
		break;
	case GV_TYPE_SD:
		if (type_parent != GV_TYPE_PLEX) {
			gctl_error(req, "no such plex to attach to");
			return;
		}
		p = gv_find_plex(sc, parent);
		s = gv_find_sd(sc, child);
		gv_post_event(sc, GV_EVENT_ATTACH_SD, s, p, *offset, *rename);
		break;
	default:
		gctl_error(req, "invalid child type");
		break;
	}
}

/* Handle userland request of detaching object. */
static void
gv_detach(struct gv_softc *sc, struct gctl_req *req)
{
	struct gv_plex *p;
	struct gv_sd *s;
	int *flags, type;
	char *object;

	object = gctl_get_param(req, "object", NULL);
	if (object == NULL) {
		gctl_error(req, "no argument given");
		return;
	}

	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	type = gv_object_type(sc, object);
	switch (type) {
	case GV_TYPE_PLEX:
		p = gv_find_plex(sc, object);
		gv_post_event(sc, GV_EVENT_DETACH_PLEX, p, NULL, *flags, 0);
		break;
	case GV_TYPE_SD:
		s = gv_find_sd(sc, object);
		gv_post_event(sc, GV_EVENT_DETACH_SD, s, NULL, *flags, 0);
		break;
	default:
		gctl_error(req, "invalid object type");
		break;
	}
}

/* Handle userland requests for creating new objects. */
static int
gv_create(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_softc *sc;
	struct gv_drive *d, *d2;
	struct gv_plex *p, *p2;
	struct gv_sd *s, *s2;
	struct gv_volume *v, *v2;
	struct g_provider *pp;
	int error, i, *drives, *flags, *plexes, *subdisks, *volumes;
	char buf[20];

	g_topology_assert();

	sc = gp->softc;

	/* Find out how many of each object have been passed in. */
	volumes = gctl_get_paraml(req, "volumes", sizeof(*volumes));
	plexes = gctl_get_paraml(req, "plexes", sizeof(*plexes));
	subdisks = gctl_get_paraml(req, "subdisks", sizeof(*subdisks));
	drives = gctl_get_paraml(req, "drives", sizeof(*drives));
	if (volumes == NULL || plexes == NULL || subdisks == NULL ||
	    drives == NULL) {
		gctl_error(req, "number of objects not given");
		return (-1);
	}
	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	if (flags == NULL) {
		gctl_error(req, "flags not given");
		return (-1);
	}

	/* First, handle drive definitions ... */
	for (i = 0; i < *drives; i++) {
		snprintf(buf, sizeof(buf), "drive%d", i);
		d2 = gctl_get_paraml(req, buf, sizeof(*d2));
		if (d2 == NULL) {
			gctl_error(req, "no drive definition given");
			return (-1);
		}
		/*
		 * Make sure that the device specified in the drive config is
		 * an active GEOM provider.
		 */
		pp = g_provider_by_name(d2->device);
		if (pp == NULL) {
			gctl_error(req, "%s: device not found", d2->device);
			goto error;
		}
		if (gv_find_drive(sc, d2->name) != NULL) {
			/* Ignore error. */
			if (*flags & GV_FLAG_F)
				continue;
			gctl_error(req, "drive '%s' already exists", d2->name);
			goto error;
		}
		if (gv_find_drive_device(sc, d2->device) != NULL) {
			gctl_error(req, "device '%s' already configured in "
			    "gvinum", d2->device);
			goto error;
		}


		d = g_malloc(sizeof(*d), M_WAITOK | M_ZERO);
		bcopy(d2, d, sizeof(*d));

		gv_post_event(sc, GV_EVENT_CREATE_DRIVE, d, NULL, 0, 0);
	}

	/* ... then volume definitions ... */
	for (i = 0; i < *volumes; i++) {
		error = 0;
		snprintf(buf, sizeof(buf), "volume%d", i);
		v2 = gctl_get_paraml(req, buf, sizeof(*v2));
		if (v2 == NULL) {
			gctl_error(req, "no volume definition given");
			return (-1);
		}
		if (gv_find_vol(sc, v2->name) != NULL) {
			/* Ignore error. */
			if (*flags & GV_FLAG_F)
				continue;
			gctl_error(req, "volume '%s' already exists", v2->name);
			goto error;
		}

		v = g_malloc(sizeof(*v), M_WAITOK | M_ZERO);
		bcopy(v2, v, sizeof(*v));

		gv_post_event(sc, GV_EVENT_CREATE_VOLUME, v, NULL, 0, 0);
	}

	/* ... then plex definitions ... */
	for (i = 0; i < *plexes; i++) {
		error = 0;
		snprintf(buf, sizeof(buf), "plex%d", i);
		p2 = gctl_get_paraml(req, buf, sizeof(*p2));
		if (p2 == NULL) {
			gctl_error(req, "no plex definition given");
			return (-1);
		}
		if (gv_find_plex(sc, p2->name) != NULL) {
			/* Ignore error. */
			if (*flags & GV_FLAG_F)
				continue;
			gctl_error(req, "plex '%s' already exists", p2->name);
			goto error;
		}

		p = g_malloc(sizeof(*p), M_WAITOK | M_ZERO);
		bcopy(p2, p, sizeof(*p));

		gv_post_event(sc, GV_EVENT_CREATE_PLEX, p, NULL, 0, 0);
	}

	/* ... and, finally, subdisk definitions. */
	for (i = 0; i < *subdisks; i++) {
		error = 0;
		snprintf(buf, sizeof(buf), "sd%d", i);
		s2 = gctl_get_paraml(req, buf, sizeof(*s2));
		if (s2 == NULL) {
			gctl_error(req, "no subdisk definition given");
			return (-1);
		}
		if (gv_find_sd(sc, s2->name) != NULL) {
			/* Ignore error. */
			if (*flags & GV_FLAG_F)
				continue;
			gctl_error(req, "sd '%s' already exists", s2->name);
			goto error;
		}

		s = g_malloc(sizeof(*s), M_WAITOK | M_ZERO);
		bcopy(s2, s, sizeof(*s));

		gv_post_event(sc, GV_EVENT_CREATE_SD, s, NULL, 0, 0);
	}

error:
	gv_post_event(sc, GV_EVENT_SETUP_OBJECTS, sc, NULL, 0, 0);
	gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);

	return (0);
}

static void
gv_config(struct gctl_req *req, struct g_class *mp, char const *verb)
{
	struct g_geom *gp;
	struct gv_softc *sc;
	struct sbuf *sb;
	char *comment;

	g_topology_assert();

	gp = LIST_FIRST(&mp->geom);
	sc = gp->softc;

	if (!strcmp(verb, "attach")) {
		gv_attach(sc, req);

	} else if (!strcmp(verb, "concat")) {
		gv_concat(gp, req);

	} else if (!strcmp(verb, "detach")) {
		gv_detach(sc, req);

	} else if (!strcmp(verb, "list")) {
		gv_list(gp, req);

	/* Save our configuration back to disk. */
	} else if (!strcmp(verb, "saveconfig")) {
		gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);

	/* Return configuration in string form. */
	} else if (!strcmp(verb, "getconfig")) {
		comment = gctl_get_param(req, "comment", NULL);
		if (comment == NULL) {
			gctl_error(req, "no comment parameter given");
			return;
		}
		sb = sbuf_new(NULL, NULL, GV_CFG_LEN, SBUF_FIXEDLEN);
		gv_format_config(sc, sb, 0, comment);
		sbuf_finish(sb);
		gctl_set_param(req, "config", sbuf_data(sb), sbuf_len(sb) + 1);
		sbuf_delete(sb);

	} else if (!strcmp(verb, "create")) {
		gv_create(gp, req);

	} else if (!strcmp(verb, "mirror")) {
		gv_mirror(gp, req);

	} else if (!strcmp(verb, "move")) {
		gv_move(gp, req);

	} else if (!strcmp(verb, "raid5")) {
		gv_raid5(gp, req);

	} else if (!strcmp(verb, "rebuildparity") ||
	    !strcmp(verb, "checkparity")) {
		gv_parityop(sc, req);

	} else if (!strcmp(verb, "remove")) {
		gv_remove(gp, req);

	} else if (!strcmp(verb, "rename")) {
		gv_rename(gp, req);
	
	} else if (!strcmp(verb, "resetconfig")) {
		gv_post_event(sc, GV_EVENT_RESET_CONFIG, sc, NULL, 0, 0);

	} else if (!strcmp(verb, "start")) {
		gv_start_obj(gp, req);

	} else if (!strcmp(verb, "stripe")) {
		gv_stripe(gp, req);

	} else if (!strcmp(verb, "setstate")) {
		gv_setstate(gp, req);
	} else
		gctl_error(req, "Unknown verb parameter");
}

static void
gv_parityop(struct gv_softc *sc, struct gctl_req *req)
{
	struct gv_plex *p;
	int *flags, *rebuild, type;
	char *plex;

	plex = gctl_get_param(req, "plex", NULL);
	if (plex == NULL) {
		gctl_error(req, "no plex given");
		return;
	}

	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	if (flags == NULL) {
		gctl_error(req, "no flags given");
		return;
	}

	rebuild = gctl_get_paraml(req, "rebuild", sizeof(*rebuild));
	if (rebuild == NULL) {
		gctl_error(req, "no operation given");
		return;
	}

	type = gv_object_type(sc, plex);
	if (type != GV_TYPE_PLEX) {
		gctl_error(req, "'%s' is not a plex", plex);
		return;
	}
	p = gv_find_plex(sc, plex);

	if (p->state != GV_PLEX_UP) {
		gctl_error(req, "plex %s is not completely accessible",
		    p->name);
		return;
	}

	if (p->org != GV_PLEX_RAID5) {
		gctl_error(req, "plex %s is not a RAID5 plex", p->name);
		return;
	}

	/* Put it in the event queue. */
	/* XXX: The state of the plex might have changed when this event is
	 * picked up ... We should perhaps check this afterwards. */
	if (*rebuild)
		gv_post_event(sc, GV_EVENT_PARITY_REBUILD, p, NULL, 0, 0);
	else
		gv_post_event(sc, GV_EVENT_PARITY_CHECK, p, NULL, 0, 0);
}


static struct g_geom *
gv_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	struct gv_softc *sc;
	struct gv_hdr vhdr;
	int error;

 	g_topology_assert();
	g_trace(G_T_TOPOLOGY, "gv_taste(%s, %s)", mp->name, pp->name);

	gp = LIST_FIRST(&mp->geom);
	if (gp == NULL) {
		G_VINUM_DEBUG(0, "error: tasting, but not initialized?");
		return (NULL);
	}
	sc = gp->softc;

	cp = g_new_consumer(gp);
	if (g_attach(cp, pp) != 0) {
		g_destroy_consumer(cp);
		return (NULL);
	}
	if (g_access(cp, 1, 0, 0) != 0) {
		g_detach(cp);
		g_destroy_consumer(cp);
		return (NULL);
	}
	g_topology_unlock();

	error = gv_read_header(cp, &vhdr);

	g_topology_lock();
	g_access(cp, -1, 0, 0);
	g_detach(cp);
	g_destroy_consumer(cp);

	/* Check if what we've been given is a valid vinum drive. */
	if (!error)
		gv_post_event(sc, GV_EVENT_DRIVE_TASTED, pp, NULL, 0, 0);

	return (NULL);
}

void
gv_worker(void *arg)
{
	struct g_provider *pp;
	struct gv_softc *sc;
	struct gv_event *ev;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_sd *s;
	struct gv_drive *d;
	struct bio *bp;
	int newstate, flags, err, rename;
	char *newname;
	off_t offset;

	sc = arg;
	KASSERT(sc != NULL, ("NULL sc"));
	for (;;) {
		/* Look at the events first... */
		ev = gv_get_event(sc);
		if (ev != NULL) {
			gv_remove_event(sc, ev);

			switch (ev->type) {
			case GV_EVENT_DRIVE_TASTED:
				G_VINUM_DEBUG(2, "event 'drive tasted'");
				pp = ev->arg1;
				gv_drive_tasted(sc, pp);
				break;

			case GV_EVENT_DRIVE_LOST:
				G_VINUM_DEBUG(2, "event 'drive lost'");
				d = ev->arg1;
				gv_drive_lost(sc, d);
				break;

			case GV_EVENT_CREATE_DRIVE:
				G_VINUM_DEBUG(2, "event 'create drive'");
				d = ev->arg1;
				gv_create_drive(sc, d);
				break;

			case GV_EVENT_CREATE_VOLUME:
				G_VINUM_DEBUG(2, "event 'create volume'");
				v = ev->arg1;
				gv_create_volume(sc, v);
				break;

			case GV_EVENT_CREATE_PLEX:
				G_VINUM_DEBUG(2, "event 'create plex'");
				p = ev->arg1;
				gv_create_plex(sc, p);
				break;

			case GV_EVENT_CREATE_SD:
				G_VINUM_DEBUG(2, "event 'create sd'");
				s = ev->arg1;
				gv_create_sd(sc, s);
				break;

			case GV_EVENT_RM_DRIVE:
				G_VINUM_DEBUG(2, "event 'remove drive'");
				d = ev->arg1;
				flags = ev->arg3;
				gv_rm_drive(sc, d, flags);
				/*gv_setup_objects(sc);*/
				break;

			case GV_EVENT_RM_VOLUME:
				G_VINUM_DEBUG(2, "event 'remove volume'");
				v = ev->arg1;
				gv_rm_vol(sc, v);
				/*gv_setup_objects(sc);*/
				break;

			case GV_EVENT_RM_PLEX:
				G_VINUM_DEBUG(2, "event 'remove plex'");
				p = ev->arg1;
				gv_rm_plex(sc, p);
				/*gv_setup_objects(sc);*/
				break;

			case GV_EVENT_RM_SD:
				G_VINUM_DEBUG(2, "event 'remove sd'");
				s = ev->arg1;
				gv_rm_sd(sc, s);
				/*gv_setup_objects(sc);*/
				break;

			case GV_EVENT_SAVE_CONFIG:
				G_VINUM_DEBUG(2, "event 'save config'");
				gv_save_config(sc);
				break;

			case GV_EVENT_SET_SD_STATE:
				G_VINUM_DEBUG(2, "event 'setstate sd'");
				s = ev->arg1;
				newstate = ev->arg3;
				flags = ev->arg4;
				err = gv_set_sd_state(s, newstate, flags);
				if (err)
					G_VINUM_DEBUG(0, "error setting subdisk"
					    " state: error code %d", err);
				break;

			case GV_EVENT_SET_DRIVE_STATE:
				G_VINUM_DEBUG(2, "event 'setstate drive'");
				d = ev->arg1;
				newstate = ev->arg3;
				flags = ev->arg4;
				err = gv_set_drive_state(d, newstate, flags);
				if (err)
					G_VINUM_DEBUG(0, "error setting drive "
					    "state: error code %d", err);
				break;

			case GV_EVENT_SET_VOL_STATE:
				G_VINUM_DEBUG(2, "event 'setstate volume'");
				v = ev->arg1;
				newstate = ev->arg3;
				flags = ev->arg4;
				err = gv_set_vol_state(v, newstate, flags);
				if (err)
					G_VINUM_DEBUG(0, "error setting volume "
					    "state: error code %d", err);
				break;

			case GV_EVENT_SET_PLEX_STATE:
				G_VINUM_DEBUG(2, "event 'setstate plex'");
				p = ev->arg1;
				newstate = ev->arg3;
				flags = ev->arg4;
				err = gv_set_plex_state(p, newstate, flags);
				if (err)
					G_VINUM_DEBUG(0, "error setting plex "
					    "state: error code %d", err);
				break;

			case GV_EVENT_SETUP_OBJECTS:
				G_VINUM_DEBUG(2, "event 'setup objects'");
				gv_setup_objects(sc);
				break;

			case GV_EVENT_RESET_CONFIG:
				G_VINUM_DEBUG(2, "event 'resetconfig'");
				err = gv_resetconfig(sc);
				if (err)
					G_VINUM_DEBUG(0, "error resetting "
					    "config: error code %d", err);
				break;

			case GV_EVENT_PARITY_REBUILD:
				/*
				 * Start the rebuild. The gv_plex_done will
				 * handle issuing of the remaining rebuild bio's
				 * until it's finished. 
				 */
				G_VINUM_DEBUG(2, "event 'rebuild'");
				p = ev->arg1;
				if (p->state != GV_PLEX_UP) {
					G_VINUM_DEBUG(0, "plex %s is not "
					    "completely accessible", p->name);
					break;
				}
				if (p->flags & GV_PLEX_SYNCING ||
				    p->flags & GV_PLEX_REBUILDING ||
				    p->flags & GV_PLEX_GROWING) {
					G_VINUM_DEBUG(0, "plex %s is busy with "
					    "syncing or parity build", p->name);
					break;
				}
				p->synced = 0;
				p->flags |= GV_PLEX_REBUILDING;
				g_topology_assert_not();
				g_topology_lock();
				err = gv_access(p->vol_sc->provider, 1, 1, 0);
				if (err) {
					G_VINUM_DEBUG(0, "unable to access "
					    "provider");
					break;
				}
				g_topology_unlock();
				gv_parity_request(p, GV_BIO_CHECK |
				    GV_BIO_PARITY, 0);
				break;

			case GV_EVENT_PARITY_CHECK:
				/* Start parity check. */
				G_VINUM_DEBUG(2, "event 'check'");
				p = ev->arg1;
				if (p->state != GV_PLEX_UP) {
					G_VINUM_DEBUG(0, "plex %s is not "
					    "completely accessible", p->name);
					break;
				}
				if (p->flags & GV_PLEX_SYNCING ||
				    p->flags & GV_PLEX_REBUILDING ||
				    p->flags & GV_PLEX_GROWING) {
					G_VINUM_DEBUG(0, "plex %s is busy with "
					    "syncing or parity build", p->name);
					break;
				}
				p->synced = 0;
				g_topology_assert_not();
				g_topology_lock();
				err = gv_access(p->vol_sc->provider, 1, 1, 0);
				if (err) {
					G_VINUM_DEBUG(0, "unable to access "
					    "provider");
					break;
				}
				g_topology_unlock();
				gv_parity_request(p, GV_BIO_CHECK, 0);
				break;

			case GV_EVENT_START_PLEX:
				G_VINUM_DEBUG(2, "event 'start' plex");
				p = ev->arg1;
				gv_start_plex(p);
				break;

			case GV_EVENT_START_VOLUME:
				G_VINUM_DEBUG(2, "event 'start' volume");
				v = ev->arg1;
				gv_start_vol(v);
				break;

			case GV_EVENT_ATTACH_PLEX:
				G_VINUM_DEBUG(2, "event 'attach' plex");
				p = ev->arg1;
				v = ev->arg2;
				rename = ev->arg4;
				err = gv_attach_plex(p, v, rename);
				if (err)
					G_VINUM_DEBUG(0, "error attaching %s to"
					    " %s: error code %d", p->name,
					    v->name, err);
				break;

			case GV_EVENT_ATTACH_SD:
				G_VINUM_DEBUG(2, "event 'attach' sd");
				s = ev->arg1;
				p = ev->arg2;
				offset = ev->arg3;
				rename = ev->arg4;
				err = gv_attach_sd(s, p, offset, rename);
				if (err)
					G_VINUM_DEBUG(0, "error attaching %s to"
					    " %s: error code %d", s->name,
					    p->name, err);
				break;

			case GV_EVENT_DETACH_PLEX:
				G_VINUM_DEBUG(2, "event 'detach' plex");
				p = ev->arg1;
				flags = ev->arg3;
				err = gv_detach_plex(p, flags);
				if (err)
					G_VINUM_DEBUG(0, "error detaching %s: "
					    "error code %d", p->name, err);
				break;

			case GV_EVENT_DETACH_SD:
				G_VINUM_DEBUG(2, "event 'detach' sd");
				s = ev->arg1;
				flags = ev->arg3;
				err = gv_detach_sd(s, flags);
				if (err)
					G_VINUM_DEBUG(0, "error detaching %s: "
					    "error code %d", s->name, err);
				break;

			case GV_EVENT_RENAME_VOL:
				G_VINUM_DEBUG(2, "event 'rename' volume");
				v = ev->arg1;
				newname = ev->arg2;
				flags = ev->arg3;
				err = gv_rename_vol(sc, v, newname, flags);
				if (err)
					G_VINUM_DEBUG(0, "error renaming %s to "
					    "%s: error code %d", v->name,
					    newname, err);
				g_free(newname);
				/* Destroy and recreate the provider if we can. */
				if (gv_provider_is_open(v->provider)) {
					G_VINUM_DEBUG(0, "unable to rename "
					    "provider to %s: provider in use",
					    v->name);
					break;
				}
				g_topology_lock();
				g_wither_provider(v->provider, ENOENT);
				g_topology_unlock();
				v->provider = NULL;
				gv_post_event(sc, GV_EVENT_SETUP_OBJECTS, sc,
				    NULL, 0, 0);
				break;

			case GV_EVENT_RENAME_PLEX:
				G_VINUM_DEBUG(2, "event 'rename' plex");
				p = ev->arg1;
				newname = ev->arg2;
				flags = ev->arg3;
				err = gv_rename_plex(sc, p, newname, flags);
				if (err)
					G_VINUM_DEBUG(0, "error renaming %s to "
					    "%s: error code %d", p->name,
					    newname, err);
				g_free(newname);
				break;

			case GV_EVENT_RENAME_SD:
				G_VINUM_DEBUG(2, "event 'rename' sd");
				s = ev->arg1;
				newname = ev->arg2;
				flags = ev->arg3;
				err = gv_rename_sd(sc, s, newname, flags);
				if (err)
					G_VINUM_DEBUG(0, "error renaming %s to "
					    "%s: error code %d", s->name,
					    newname, err);
				g_free(newname);
				break;

			case GV_EVENT_RENAME_DRIVE:
				G_VINUM_DEBUG(2, "event 'rename' drive");
				d = ev->arg1;
				newname = ev->arg2;
				flags = ev->arg3;
				err = gv_rename_drive(sc, d, newname, flags);
				if (err)
					G_VINUM_DEBUG(0, "error renaming %s to "
					    "%s: error code %d", d->name,
					    newname, err);
				g_free(newname);
				break;

			case GV_EVENT_MOVE_SD:
				G_VINUM_DEBUG(2, "event 'move' sd");
				s = ev->arg1;
				d = ev->arg2;
				flags = ev->arg3;
				err = gv_move_sd(sc, s, d, flags);
				if (err)
					G_VINUM_DEBUG(0, "error moving %s to "
					    "%s: error code %d", s->name,
					    d->name, err);
				break;

			case GV_EVENT_THREAD_EXIT:
				G_VINUM_DEBUG(2, "event 'thread exit'");
				g_free(ev);
				mtx_lock(&sc->equeue_mtx);
				mtx_lock(&sc->bqueue_mtx);
				gv_cleanup(sc);
				mtx_destroy(&sc->bqueue_mtx);
				mtx_destroy(&sc->equeue_mtx);
				g_free(sc->bqueue_down);
				g_free(sc->bqueue_up);
				g_free(sc);
				kproc_exit(0);
				/* NOTREACHED */

			default:
				G_VINUM_DEBUG(1, "unknown event %d", ev->type);
			}

			g_free(ev);
			continue;
		}

		/* ... then do I/O processing. */
		mtx_lock(&sc->bqueue_mtx);
		/* First do new requests. */
		bp = bioq_takefirst(sc->bqueue_down);
		if (bp != NULL) {
			mtx_unlock(&sc->bqueue_mtx);
			/* A bio that interfered with another bio. */
			if (bp->bio_pflags & GV_BIO_ONHOLD) {
				s = bp->bio_caller1;
				p = s->plex_sc;
				/* Is it still locked out? */
				if (gv_stripe_active(p, bp)) {
					/* Park the bio on the waiting queue. */
					bioq_disksort(p->wqueue, bp);
				} else {
					bp->bio_pflags &= ~GV_BIO_ONHOLD;
					g_io_request(bp, s->drive_sc->consumer);
				}
			/* A special request requireing special handling. */
			} else if (bp->bio_pflags & GV_BIO_INTERNAL) {
				p = bp->bio_caller1;
				gv_plex_start(p, bp);
			} else {
				gv_volume_start(sc, bp);
			}
			mtx_lock(&sc->bqueue_mtx);
		}
		/* Then do completed requests. */
		bp = bioq_takefirst(sc->bqueue_up);
		if (bp == NULL) {
			msleep(sc, &sc->bqueue_mtx, PRIBIO, "-", hz/10);
			mtx_unlock(&sc->bqueue_mtx);
			continue;
		}
		mtx_unlock(&sc->bqueue_mtx);
		gv_bio_done(sc, bp);
	}
}

#define	VINUM_CLASS_NAME "VINUM"

static struct g_class g_vinum_class	= {
	.name = VINUM_CLASS_NAME,
	.version = G_VERSION,
	.init = gv_init,
	.taste = gv_taste,
	.ctlreq = gv_config,
	.destroy_geom = gv_unload,
};

DECLARE_GEOM_CLASS(g_vinum_class, g_vinum);
MODULE_VERSION(geom_vinum, 0);
