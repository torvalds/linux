/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Edward Tomasz Napierala <trasz@FreeBSD.org>
 * Copyright (c) 2004-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/disk.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/eventhandler.h>
#include <geom/geom.h>
#include <geom/mountver/g_mountver.h>


SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, mountver, CTLFLAG_RW,
    0, "GEOM_MOUNTVER stuff");
static u_int g_mountver_debug = 0;
static u_int g_mountver_check_ident = 1;
SYSCTL_UINT(_kern_geom_mountver, OID_AUTO, debug, CTLFLAG_RW,
    &g_mountver_debug, 0, "Debug level");
SYSCTL_UINT(_kern_geom_mountver, OID_AUTO, check_ident, CTLFLAG_RW,
    &g_mountver_check_ident, 0, "Check disk ident when reattaching");

static eventhandler_tag g_mountver_pre_sync = NULL;

static void g_mountver_queue(struct bio *bp);
static void g_mountver_orphan(struct g_consumer *cp);
static void g_mountver_resize(struct g_consumer *cp);
static int g_mountver_destroy(struct g_geom *gp, boolean_t force);
static g_taste_t g_mountver_taste;
static int g_mountver_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp);
static void g_mountver_config(struct gctl_req *req, struct g_class *mp,
    const char *verb);
static void g_mountver_dumpconf(struct sbuf *sb, const char *indent,
    struct g_geom *gp, struct g_consumer *cp, struct g_provider *pp);
static void g_mountver_init(struct g_class *mp);
static void g_mountver_fini(struct g_class *mp);

struct g_class g_mountver_class = {
	.name = G_MOUNTVER_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_mountver_config,
	.taste = g_mountver_taste,
	.destroy_geom = g_mountver_destroy_geom,
	.init = g_mountver_init,
	.fini = g_mountver_fini
};

static void
g_mountver_done(struct bio *bp)
{
	struct g_geom *gp;
	struct bio *pbp;

	if (bp->bio_error != ENXIO) {
		g_std_done(bp);
		return;
	}

	/*
	 * When the device goes away, it's possible that few requests
	 * will be completed with ENXIO before g_mountver_orphan()
	 * gets called.  To work around that, we have to queue requests
	 * that failed with ENXIO, in order to send them later.
	 */
	gp = bp->bio_from->geom;

	pbp = bp->bio_parent;
	KASSERT(pbp->bio_to == LIST_FIRST(&gp->provider),
	    ("parent request was for someone else"));
	g_destroy_bio(bp);
	pbp->bio_inbed++;
	g_mountver_queue(pbp);
}

static void
g_mountver_send(struct bio *bp)
{
	struct g_geom *gp;
	struct bio *cbp;

	gp = bp->bio_to->geom;

	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		g_io_deliver(bp, ENOMEM);
		return;
	}

	cbp->bio_done = g_mountver_done;
	g_io_request(cbp, LIST_FIRST(&gp->consumer));
}

static void
g_mountver_queue(struct bio *bp)
{
	struct g_mountver_softc *sc;
	struct g_geom *gp;

	gp = bp->bio_to->geom;
	sc = gp->softc;

	mtx_lock(&sc->sc_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_queue, bp, bio_queue);
	mtx_unlock(&sc->sc_mtx);
}

static void
g_mountver_send_queued(struct g_geom *gp)
{
	struct g_mountver_softc *sc;
	struct bio *bp;

	sc = gp->softc;

	mtx_lock(&sc->sc_mtx);
	while ((bp = TAILQ_FIRST(&sc->sc_queue)) != NULL) {
		TAILQ_REMOVE(&sc->sc_queue, bp, bio_queue);
		G_MOUNTVER_LOGREQ(bp, "Sending queued request.");
		g_mountver_send(bp);
	}
	mtx_unlock(&sc->sc_mtx);
}

static void
g_mountver_discard_queued(struct g_geom *gp)
{
	struct g_mountver_softc *sc;
	struct bio *bp;

	sc = gp->softc;

	mtx_lock(&sc->sc_mtx);
	while ((bp = TAILQ_FIRST(&sc->sc_queue)) != NULL) {
		TAILQ_REMOVE(&sc->sc_queue, bp, bio_queue);
		G_MOUNTVER_LOGREQ(bp, "Discarding queued request.");
		g_io_deliver(bp, ENXIO);
	}
	mtx_unlock(&sc->sc_mtx);
}

static void
g_mountver_start(struct bio *bp)
{
	struct g_mountver_softc *sc;
	struct g_geom *gp;

	gp = bp->bio_to->geom;
	sc = gp->softc;
	G_MOUNTVER_LOGREQ(bp, "Request received.");

	/*
	 * It is possible that some bios were returned with ENXIO, even though
	 * orphaning didn't happen yet.  In that case, queue all subsequent
	 * requests in order to maintain ordering.
	 */
	if (sc->sc_orphaned || !TAILQ_EMPTY(&sc->sc_queue)) {
		if (sc->sc_shutting_down) {
			G_MOUNTVER_LOGREQ(bp, "Discarding request due to shutdown.");
			g_io_deliver(bp, ENXIO);
			return;
		}
		G_MOUNTVER_LOGREQ(bp, "Queueing request.");
		g_mountver_queue(bp);
		if (!sc->sc_orphaned)
			g_mountver_send_queued(gp);
	} else {
		G_MOUNTVER_LOGREQ(bp, "Sending request.");
		g_mountver_send(bp);
	}
}

static int
g_mountver_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_mountver_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp;

	g_topology_assert();

	gp = pp->geom;
	cp = LIST_FIRST(&gp->consumer);
	sc = gp->softc;
	if (sc == NULL && dr <= 0 && dw <= 0 && de <= 0)
		return (0);
	KASSERT(sc != NULL, ("Trying to access withered provider \"%s\".", pp->name));

	sc->sc_access_r += dr;
	sc->sc_access_w += dw;
	sc->sc_access_e += de;

	if (sc->sc_orphaned)
		return (0);

	return (g_access(cp, dr, dw, de));
}

static int
g_mountver_create(struct gctl_req *req, struct g_class *mp, struct g_provider *pp)
{
	struct g_mountver_softc *sc;
	struct g_geom *gp;
	struct g_provider *newpp;
	struct g_consumer *cp;
	char name[64];
	int error;
	int identsize = DISK_IDENT_SIZE;

	g_topology_assert();

	gp = NULL;
	newpp = NULL;
	cp = NULL;

	snprintf(name, sizeof(name), "%s%s", pp->name, G_MOUNTVER_SUFFIX);
	LIST_FOREACH(gp, &mp->geom, geom) {
		if (strcmp(gp->name, name) == 0) {
			gctl_error(req, "Provider %s already exists.", name);
			return (EEXIST);
		}
	}
	gp = g_new_geomf(mp, "%s", name);
	sc = g_malloc(sizeof(*sc), M_WAITOK | M_ZERO);
	mtx_init(&sc->sc_mtx, "gmountver", NULL, MTX_DEF | MTX_RECURSE);
	TAILQ_INIT(&sc->sc_queue);
	sc->sc_provider_name = strdup(pp->name, M_GEOM);
	gp->softc = sc;
	gp->start = g_mountver_start;
	gp->orphan = g_mountver_orphan;
	gp->resize = g_mountver_resize;
	gp->access = g_mountver_access;
	gp->dumpconf = g_mountver_dumpconf;

	newpp = g_new_providerf(gp, "%s", gp->name);
	newpp->mediasize = pp->mediasize;
	newpp->sectorsize = pp->sectorsize;
	newpp->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE;

	if ((pp->flags & G_PF_ACCEPT_UNMAPPED) != 0) {
		G_MOUNTVER_DEBUG(0, "Unmapped supported for %s.", gp->name);
		newpp->flags |= G_PF_ACCEPT_UNMAPPED;
	} else {
		G_MOUNTVER_DEBUG(0, "Unmapped unsupported for %s.", gp->name);
		newpp->flags &= ~G_PF_ACCEPT_UNMAPPED;
	}

	cp = g_new_consumer(gp);
	cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
	error = g_attach(cp, pp);
	if (error != 0) {
		gctl_error(req, "Cannot attach to provider %s.", pp->name);
		goto fail;
	}
	error = g_access(cp, 1, 0, 0);
	if (error != 0) {
		gctl_error(req, "Cannot access provider %s.", pp->name);
		goto fail;
	}
	error = g_io_getattr("GEOM::ident", cp, &identsize, sc->sc_ident);
	g_access(cp, -1, 0, 0);
	if (error != 0) {
		if (g_mountver_check_ident) {
			gctl_error(req, "Cannot get disk ident from %s; error = %d.", pp->name, error);
			goto fail;
		}

		G_MOUNTVER_DEBUG(0, "Cannot get disk ident from %s; error = %d.", pp->name, error);
		sc->sc_ident[0] = '\0';
	}

	g_error_provider(newpp, 0);
	G_MOUNTVER_DEBUG(0, "Device %s created.", gp->name);
	return (0);
fail:
	g_free(sc->sc_provider_name);
	if (cp->provider != NULL)
		g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_provider(newpp);
	g_free(gp->softc);
	g_destroy_geom(gp);
	return (error);
}

static int
g_mountver_destroy(struct g_geom *gp, boolean_t force)
{
	struct g_mountver_softc *sc;
	struct g_provider *pp;

	g_topology_assert();
	if (gp->softc == NULL)
		return (ENXIO);
	sc = gp->softc;
	pp = LIST_FIRST(&gp->provider);
	if (pp != NULL && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		if (force) {
			G_MOUNTVER_DEBUG(0, "Device %s is still open, so it "
			    "can't be definitely removed.", pp->name);
		} else {
			G_MOUNTVER_DEBUG(1, "Device %s is still open (r%dw%de%d).",
			    pp->name, pp->acr, pp->acw, pp->ace);
			return (EBUSY);
		}
	} else {
		G_MOUNTVER_DEBUG(0, "Device %s removed.", gp->name);
	}
	if (pp != NULL)
		g_wither_provider(pp, ENXIO);
	g_mountver_discard_queued(gp);
	g_free(sc->sc_provider_name);
	g_free(gp->softc);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);

	return (0);
}

static int
g_mountver_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{

	return (g_mountver_destroy(gp, 0));
}

static void
g_mountver_ctl_create(struct gctl_req *req, struct g_class *mp)
{
	struct g_provider *pp;
	const char *name;
	char param[16];
	int i, *nargs;

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument", "nargs");
		return;
	}
	if (*nargs <= 0) {
		gctl_error(req, "Missing device(s).");
		return;
	}
	for (i = 0; i < *nargs; i++) {
		snprintf(param, sizeof(param), "arg%d", i);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%d' argument", i);
			return;
		}
		if (strncmp(name, "/dev/", strlen("/dev/")) == 0)
			name += strlen("/dev/");
		pp = g_provider_by_name(name);
		if (pp == NULL) {
			G_MOUNTVER_DEBUG(1, "Provider %s is invalid.", name);
			gctl_error(req, "Provider %s is invalid.", name);
			return;
		}
		if (g_mountver_create(req, mp, pp) != 0)
			return;
	}
}

static struct g_geom *
g_mountver_find_geom(struct g_class *mp, const char *name)
{
	struct g_geom *gp;

	LIST_FOREACH(gp, &mp->geom, geom) {
		if (strcmp(gp->name, name) == 0)
			return (gp);
	}
	return (NULL);
}

static void
g_mountver_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	int *nargs, *force, error, i;
	struct g_geom *gp;
	const char *name;
	char param[16];

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument", "nargs");
		return;
	}
	if (*nargs <= 0) {
		gctl_error(req, "Missing device(s).");
		return;
	}
	force = gctl_get_paraml(req, "force", sizeof(*force));
	if (force == NULL) {
		gctl_error(req, "No 'force' argument");
		return;
	}

	for (i = 0; i < *nargs; i++) {
		snprintf(param, sizeof(param), "arg%d", i);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%d' argument", i);
			return;
		}
		if (strncmp(name, "/dev/", strlen("/dev/")) == 0)
			name += strlen("/dev/");
		gp = g_mountver_find_geom(mp, name);
		if (gp == NULL) {
			G_MOUNTVER_DEBUG(1, "Device %s is invalid.", name);
			gctl_error(req, "Device %s is invalid.", name);
			return;
		}
		error = g_mountver_destroy(gp, *force);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    gp->name, error);
			return;
		}
	}
}

static void
g_mountver_orphan(struct g_consumer *cp)
{
	struct g_mountver_softc *sc;

	g_topology_assert();

	sc = cp->geom->softc;
	sc->sc_orphaned = 1;
	if (cp->acr > 0 || cp->acw > 0 || cp->ace > 0)
		g_access(cp, -cp->acr, -cp->acw, -cp->ace);
	g_detach(cp);
	G_MOUNTVER_DEBUG(0, "%s is offline.  Mount verification in progress.", sc->sc_provider_name);
}

static void
g_mountver_resize(struct g_consumer *cp)
{
	struct g_geom *gp;
	struct g_provider *pp;

	gp = cp->geom;

	LIST_FOREACH(pp, &gp->provider, provider)
		g_resize_provider(pp, cp->provider->mediasize);
}

static int
g_mountver_ident_matches(struct g_geom *gp)
{
	struct g_consumer *cp;
	struct g_mountver_softc *sc;
	char ident[DISK_IDENT_SIZE];
	int error, identsize = DISK_IDENT_SIZE;

	sc = gp->softc;
	cp = LIST_FIRST(&gp->consumer);

	if (g_mountver_check_ident == 0)
		return (0);

	error = g_access(cp, 1, 0, 0);
	if (error != 0) {
		G_MOUNTVER_DEBUG(0, "Cannot access %s; "
		    "not attaching; error = %d.", gp->name, error);
		return (1);
	}
	error = g_io_getattr("GEOM::ident", cp, &identsize, ident);
	g_access(cp, -1, 0, 0);
	if (error != 0) {
		G_MOUNTVER_DEBUG(0, "Cannot get disk ident for %s; "
		    "not attaching; error = %d.", gp->name, error);
		return (1);
	}
	if (strcmp(ident, sc->sc_ident) != 0) {
		G_MOUNTVER_DEBUG(1, "Disk ident for %s (\"%s\") is different "
		    "from expected \"%s\", not attaching.", gp->name, ident,
		    sc->sc_ident);
		return (1);
	}

	return (0);
}
	
static struct g_geom *
g_mountver_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_mountver_softc *sc;
	struct g_consumer *cp;
	struct g_geom *gp;
	int error;

	g_topology_assert();
	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	G_MOUNTVER_DEBUG(2, "Tasting %s.", pp->name);

	/*
	 * Let's check if device already exists.
	 */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;

		/* Already attached? */
		if (pp == LIST_FIRST(&gp->provider))
			return (NULL);

		if (sc->sc_orphaned && strcmp(pp->name, sc->sc_provider_name) == 0)
			break;
	}
	if (gp == NULL)
		return (NULL);

	cp = LIST_FIRST(&gp->consumer);
	g_attach(cp, pp);
	error = g_mountver_ident_matches(gp);
	if (error != 0) {
		g_detach(cp);
		return (NULL);
	}
	if (sc->sc_access_r > 0 || sc->sc_access_w > 0 || sc->sc_access_e > 0) {
		error = g_access(cp, sc->sc_access_r, sc->sc_access_w, sc->sc_access_e);
		if (error != 0) {
			G_MOUNTVER_DEBUG(0, "Cannot access %s; error = %d.", pp->name, error);
			g_detach(cp);
			return (NULL);
		}
	}
	g_mountver_send_queued(gp);
	sc->sc_orphaned = 0;
	G_MOUNTVER_DEBUG(0, "%s has completed mount verification.", sc->sc_provider_name);

	return (gp);
}

static void
g_mountver_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_MOUNTVER_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "create") == 0) {
		g_mountver_ctl_create(req, mp);
		return;
	} else if (strcmp(verb, "destroy") == 0) {
		g_mountver_ctl_destroy(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

static void
g_mountver_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_mountver_softc *sc;

	if (pp != NULL || cp != NULL)
		return;

	sc = gp->softc;
	sbuf_printf(sb, "%s<State>%s</State>\n", indent,
	    sc->sc_orphaned ? "OFFLINE" : "ONLINE");
	sbuf_printf(sb, "%s<Provider-Name>%s</Provider-Name>\n", indent, sc->sc_provider_name);
	sbuf_printf(sb, "%s<Disk-Ident>%s</Disk-Ident>\n", indent, sc->sc_ident);
}

static void
g_mountver_shutdown_pre_sync(void *arg, int howto)
{
	struct g_mountver_softc *sc;
	struct g_class *mp;
	struct g_geom *gp, *gp2;

	mp = arg;
	g_topology_lock();
	LIST_FOREACH_SAFE(gp, &mp->geom, geom, gp2) {
		if (gp->softc == NULL)
			continue;
		sc = gp->softc;
		sc->sc_shutting_down = 1;
		if (sc->sc_orphaned)
			g_mountver_destroy(gp, 1);
	}
	g_topology_unlock();
}

static void
g_mountver_init(struct g_class *mp)
{

	g_mountver_pre_sync = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    g_mountver_shutdown_pre_sync, mp, SHUTDOWN_PRI_FIRST);
	if (g_mountver_pre_sync == NULL)
		G_MOUNTVER_DEBUG(0, "Warning! Cannot register shutdown event.");
}

static void
g_mountver_fini(struct g_class *mp)
{

	if (g_mountver_pre_sync != NULL)
		EVENTHANDLER_DEREGISTER(shutdown_pre_sync, g_mountver_pre_sync);
}

DECLARE_GEOM_CLASS(g_mountver_class, g_mountver);
MODULE_VERSION(geom_mountver, 0);
