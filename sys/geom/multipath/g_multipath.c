/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2006-2007 Matthew Jacob <mjacob@FreeBSD.org>
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
 * Based upon work by Pawel Jakub Dawidek <pjd@FreeBSD.org> for all of the
 * fine geom examples, and by Poul Henning Kamp <phk@FreeBSD.org> for GEOM
 * itself, all of which is most gratefully acknowledged.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <geom/geom.h>
#include <geom/multipath/g_multipath.h>

FEATURE(geom_multipath, "GEOM multipath support");

SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, multipath, CTLFLAG_RW, 0,
    "GEOM_MULTIPATH tunables");
static u_int g_multipath_debug = 0;
SYSCTL_UINT(_kern_geom_multipath, OID_AUTO, debug, CTLFLAG_RW,
    &g_multipath_debug, 0, "Debug level");
static u_int g_multipath_exclusive = 1;
SYSCTL_UINT(_kern_geom_multipath, OID_AUTO, exclusive, CTLFLAG_RW,
    &g_multipath_exclusive, 0, "Exclusively open providers");

static enum {
	GKT_NIL,
	GKT_RUN,
	GKT_DIE
} g_multipath_kt_state;
static struct bio_queue_head gmtbq;
static struct mtx gmtbq_mtx;

static int g_multipath_read_metadata(struct g_consumer *cp,
    struct g_multipath_metadata *md);
static int g_multipath_write_metadata(struct g_consumer *cp,
    struct g_multipath_metadata *md);

static void g_multipath_orphan(struct g_consumer *);
static void g_multipath_resize(struct g_consumer *);
static void g_multipath_start(struct bio *);
static void g_multipath_done(struct bio *);
static void g_multipath_done_error(struct bio *);
static void g_multipath_kt(void *);

static int g_multipath_destroy(struct g_geom *);
static int
g_multipath_destroy_geom(struct gctl_req *, struct g_class *, struct g_geom *);

static struct g_geom *g_multipath_find_geom(struct g_class *, const char *);
static int g_multipath_rotate(struct g_geom *);

static g_taste_t g_multipath_taste;
static g_ctl_req_t g_multipath_config;
static g_init_t g_multipath_init;
static g_fini_t g_multipath_fini;
static g_dumpconf_t g_multipath_dumpconf;

struct g_class g_multipath_class = {
	.name		= G_MULTIPATH_CLASS_NAME,
	.version	= G_VERSION,
	.ctlreq		= g_multipath_config,
	.taste		= g_multipath_taste,
	.destroy_geom	= g_multipath_destroy_geom,
	.init		= g_multipath_init,
	.fini		= g_multipath_fini
};

#define	MP_FAIL		0x00000001
#define	MP_LOST		0x00000002
#define	MP_NEW		0x00000004
#define	MP_POSTED	0x00000008
#define	MP_BAD		(MP_FAIL | MP_LOST | MP_NEW)
#define	MP_WITHER	0x00000010
#define	MP_IDLE		0x00000020
#define	MP_IDLE_MASK	0xffffffe0

static int
g_multipath_good(struct g_geom *gp)
{
	struct g_consumer *cp;
	int n = 0;

	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if ((cp->index & MP_BAD) == 0)
			n++;
	}
	return (n);
}

static void
g_multipath_fault(struct g_consumer *cp, int cause)
{
	struct g_multipath_softc *sc;
	struct g_consumer *lcp;
	struct g_geom *gp;

	gp = cp->geom;
	sc = gp->softc;
	cp->index |= cause;
	if (g_multipath_good(gp) == 0 && sc->sc_ndisks > 0) {
		LIST_FOREACH(lcp, &gp->consumer, consumer) {
			if (lcp->provider == NULL ||
			    (lcp->index & (MP_LOST | MP_NEW)))
				continue;
			if (sc->sc_ndisks > 1 && lcp == cp)
				continue;
			printf("GEOM_MULTIPATH: "
			    "all paths in %s were marked FAIL, restore %s\n",
			    sc->sc_name, lcp->provider->name);
			lcp->index &= ~MP_FAIL;
		}
	}
	if (cp != sc->sc_active)
		return;
	sc->sc_active = NULL;
	LIST_FOREACH(lcp, &gp->consumer, consumer) {
		if ((lcp->index & MP_BAD) == 0) {
			sc->sc_active = lcp;
			break;
		}
	}
	if (sc->sc_active == NULL) {
		printf("GEOM_MULTIPATH: out of providers for %s\n",
		    sc->sc_name);
	} else if (sc->sc_active_active != 1) {
		printf("GEOM_MULTIPATH: %s is now active path in %s\n",
		    sc->sc_active->provider->name, sc->sc_name);
	}
}

static struct g_consumer *
g_multipath_choose(struct g_geom *gp, struct bio *bp)
{
	struct g_multipath_softc *sc;
	struct g_consumer *best, *cp;

	sc = gp->softc;
	if (sc->sc_active_active == 0 ||
	    (sc->sc_active_active == 2 && bp->bio_cmd != BIO_READ))
		return (sc->sc_active);
	best = NULL;
	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp->index & MP_BAD)
			continue;
		cp->index += MP_IDLE;
		if (best == NULL || cp->private < best->private ||
		    (cp->private == best->private && cp->index > best->index))
			best = cp;
	}
	if (best != NULL)
		best->index &= ~MP_IDLE_MASK;
	return (best);
}

static void
g_mpd(void *arg, int flags __unused)
{
	struct g_geom *gp;
	struct g_multipath_softc *sc;
	struct g_consumer *cp;
	int w;

	g_topology_assert();
	cp = arg;
	gp = cp->geom;
	if (cp->acr > 0 || cp->acw > 0 || cp->ace > 0) {
		w = cp->acw;
		g_access(cp, -cp->acr, -cp->acw, -cp->ace);
		if (w > 0 && cp->provider != NULL &&
		    (cp->provider->geom->flags & G_GEOM_WITHER) == 0) {
			cp->index |= MP_WITHER;
			g_post_event(g_mpd, cp, M_WAITOK, NULL);
			return;
		}
	}
	sc = gp->softc;
	mtx_lock(&sc->sc_mtx);
	if (cp->provider) {
		printf("GEOM_MULTIPATH: %s removed from %s\n",
		    cp->provider->name, gp->name);
		g_detach(cp);
	}
	g_destroy_consumer(cp);
	mtx_unlock(&sc->sc_mtx);
	if (LIST_EMPTY(&gp->consumer))
		g_multipath_destroy(gp);
}

static void
g_multipath_orphan(struct g_consumer *cp)
{
	struct g_multipath_softc *sc;
	uintptr_t *cnt;

	g_topology_assert();
	printf("GEOM_MULTIPATH: %s in %s was disconnected\n",
	    cp->provider->name, cp->geom->name);
	sc = cp->geom->softc;
	cnt = (uintptr_t *)&cp->private;
	mtx_lock(&sc->sc_mtx);
	sc->sc_ndisks--;
	g_multipath_fault(cp, MP_LOST);
	if (*cnt == 0 && (cp->index & MP_POSTED) == 0) {
		cp->index |= MP_POSTED;
		mtx_unlock(&sc->sc_mtx);
		g_mpd(cp, 0);
	} else
		mtx_unlock(&sc->sc_mtx);
}

static void
g_multipath_resize(struct g_consumer *cp)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp1;
	struct g_provider *pp;
	struct g_multipath_metadata md;
	off_t size, psize, ssize;
	int error;

	g_topology_assert();

	gp = cp->geom;
	pp = cp->provider;
	sc = gp->softc;

	if (sc->sc_stopping)
		return;

	if (pp->mediasize < sc->sc_size) {
		size = pp->mediasize;
		ssize = pp->sectorsize;
	} else {
		size = ssize = OFF_MAX;
		mtx_lock(&sc->sc_mtx);
		LIST_FOREACH(cp1, &gp->consumer, consumer) {
			pp = cp1->provider;
			if (pp == NULL)
				continue;
			if (pp->mediasize < size) {
				size = pp->mediasize;
				ssize = pp->sectorsize;
			}
		}
		mtx_unlock(&sc->sc_mtx);
		if (size == OFF_MAX || size == sc->sc_size)
			return;
	}
	psize = size - ((sc->sc_uuid[0] != 0) ? ssize : 0);
	printf("GEOM_MULTIPATH: %s size changed from %jd to %jd\n",
	    sc->sc_name, sc->sc_pp->mediasize, psize);
	if (sc->sc_uuid[0] != 0 && size < sc->sc_size) {
		error = g_multipath_read_metadata(cp, &md);
		if (error ||
		    (strcmp(md.md_magic, G_MULTIPATH_MAGIC) != 0) ||
		    (memcmp(md.md_uuid, sc->sc_uuid, sizeof(sc->sc_uuid)) != 0) ||
		    (strcmp(md.md_name, sc->sc_name) != 0) ||
		    (md.md_size != 0 && md.md_size != size) ||
		    (md.md_sectorsize != 0 && md.md_sectorsize != ssize)) {
			g_multipath_destroy(gp);
			return;
		}
	}
	sc->sc_size = size;
	g_resize_provider(sc->sc_pp, psize);

	if (sc->sc_uuid[0] != 0) {
		pp = cp->provider;
		strlcpy(md.md_magic, G_MULTIPATH_MAGIC, sizeof(md.md_magic));
		memcpy(md.md_uuid, sc->sc_uuid, sizeof (sc->sc_uuid));
		strlcpy(md.md_name, sc->sc_name, sizeof(md.md_name));
		md.md_version = G_MULTIPATH_VERSION;
		md.md_size = size;
		md.md_sectorsize = ssize;
		md.md_active_active = sc->sc_active_active;
		error = g_multipath_write_metadata(cp, &md);
		if (error != 0)
			printf("GEOM_MULTIPATH: Can't update metadata on %s "
			    "(%d)\n", pp->name, error);
	}
}

static void
g_multipath_start(struct bio *bp)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp;
	struct bio *cbp;
	uintptr_t *cnt;

	gp = bp->bio_to->geom;
	sc = gp->softc;
	KASSERT(sc != NULL, ("NULL sc"));
	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		g_io_deliver(bp, ENOMEM);
		return;
	}
	mtx_lock(&sc->sc_mtx);
	cp = g_multipath_choose(gp, bp);
	if (cp == NULL) {
		mtx_unlock(&sc->sc_mtx);
		g_destroy_bio(cbp);
		g_io_deliver(bp, ENXIO);
		return;
	}
	if ((uintptr_t)bp->bio_driver1 < sc->sc_ndisks)
		bp->bio_driver1 = (void *)(uintptr_t)sc->sc_ndisks;
	cnt = (uintptr_t *)&cp->private;
	(*cnt)++;
	mtx_unlock(&sc->sc_mtx);
	cbp->bio_done = g_multipath_done;
	g_io_request(cbp, cp);
}

static void
g_multipath_done(struct bio *bp)
{
	struct g_multipath_softc *sc;
	struct g_consumer *cp;
	uintptr_t *cnt;

	if (bp->bio_error == ENXIO || bp->bio_error == EIO) {
		mtx_lock(&gmtbq_mtx);
		bioq_insert_tail(&gmtbq, bp);
		mtx_unlock(&gmtbq_mtx);
		wakeup(&g_multipath_kt_state);
	} else {
		cp = bp->bio_from;
		sc = cp->geom->softc;
		cnt = (uintptr_t *)&cp->private;
		mtx_lock(&sc->sc_mtx);
		(*cnt)--;
		if (*cnt == 0 && (cp->index & MP_LOST)) {
			if (g_post_event(g_mpd, cp, M_NOWAIT, NULL) == 0)
				cp->index |= MP_POSTED;
			mtx_unlock(&sc->sc_mtx);
		} else
			mtx_unlock(&sc->sc_mtx);
		g_std_done(bp);
	}
}

static void
g_multipath_done_error(struct bio *bp)
{
	struct bio *pbp;
	struct g_geom *gp;
	struct g_multipath_softc *sc;
	struct g_consumer *cp;
	struct g_provider *pp;
	uintptr_t *cnt;

	/*
	 * If we had a failure, we have to check first to see
	 * whether the consumer it failed on was the currently
	 * active consumer (i.e., this is the first in perhaps
	 * a number of failures). If so, we then switch consumers
	 * to the next available consumer.
	 */

	pbp = bp->bio_parent;
	gp = pbp->bio_to->geom;
	sc = gp->softc;
	cp = bp->bio_from;
	pp = cp->provider;
	cnt = (uintptr_t *)&cp->private;

	mtx_lock(&sc->sc_mtx);
	if ((cp->index & MP_FAIL) == 0) {
		printf("GEOM_MULTIPATH: Error %d, %s in %s marked FAIL\n",
		    bp->bio_error, pp->name, sc->sc_name);
		g_multipath_fault(cp, MP_FAIL);
	}
	(*cnt)--;
	if (*cnt == 0 && (cp->index & (MP_LOST | MP_POSTED)) == MP_LOST) {
		cp->index |= MP_POSTED;
		mtx_unlock(&sc->sc_mtx);
		g_post_event(g_mpd, cp, M_WAITOK, NULL);
	} else
		mtx_unlock(&sc->sc_mtx);

	/*
	 * If we can fruitfully restart the I/O, do so.
	 */
	if (pbp->bio_children < (uintptr_t)pbp->bio_driver1) {
		pbp->bio_inbed++;
		g_destroy_bio(bp);
		g_multipath_start(pbp);
	} else {
		g_std_done(bp);
	}
}

static void
g_multipath_kt(void *arg)
{

	g_multipath_kt_state = GKT_RUN;
	mtx_lock(&gmtbq_mtx);
	while (g_multipath_kt_state == GKT_RUN) {
		for (;;) {
			struct bio *bp;

			bp = bioq_takefirst(&gmtbq);
			if (bp == NULL)
				break;
			mtx_unlock(&gmtbq_mtx);
			g_multipath_done_error(bp);
			mtx_lock(&gmtbq_mtx);
		}
		if (g_multipath_kt_state != GKT_RUN)
			break;
		msleep(&g_multipath_kt_state, &gmtbq_mtx, PRIBIO,
		    "gkt:wait", 0);
	}
	mtx_unlock(&gmtbq_mtx);
	wakeup(&g_multipath_kt_state);
	kproc_exit(0);
}


static int
g_multipath_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_consumer *cp, *badcp = NULL;
	struct g_multipath_softc *sc;
	int error;

	gp = pp->geom;

	/* Error used if we have no valid consumers. */
	error = (dr > 0 || dw > 0 || de > 0) ? ENXIO : 0;

	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp->index & MP_WITHER)
			continue;

		error = g_access(cp, dr, dw, de);
		if (error) {
			badcp = cp;
			goto fail;
		}
	}

	if (error != 0)
		return (error);

	sc = gp->softc;
	sc->sc_opened += dr + dw + de;
	if (sc->sc_stopping && sc->sc_opened == 0)
		g_multipath_destroy(gp);

	return (0);

fail:
	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp == badcp)
			break;
		if (cp->index & MP_WITHER)
			continue;

		(void) g_access(cp, -dr, -dw, -de);
	}
	return (error);
}

static struct g_geom *
g_multipath_create(struct g_class *mp, struct g_multipath_metadata *md)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp;

	g_topology_assert();

	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL || sc->sc_stopping)
			continue;
		if (strcmp(gp->name, md->md_name) == 0) {
			printf("GEOM_MULTIPATH: name %s already exists\n",
			    md->md_name);
			return (NULL);
		}
	}

	gp = g_new_geomf(mp, "%s", md->md_name);
	sc = g_malloc(sizeof(*sc), M_WAITOK | M_ZERO);
	mtx_init(&sc->sc_mtx, "multipath", NULL, MTX_DEF);
	memcpy(sc->sc_uuid, md->md_uuid, sizeof (sc->sc_uuid));
	memcpy(sc->sc_name, md->md_name, sizeof (sc->sc_name));
	sc->sc_active_active = md->md_active_active;
	sc->sc_size = md->md_size;
	gp->softc = sc;
	gp->start = g_multipath_start;
	gp->orphan = g_multipath_orphan;
	gp->resize = g_multipath_resize;
	gp->access = g_multipath_access;
	gp->dumpconf = g_multipath_dumpconf;

	pp = g_new_providerf(gp, "multipath/%s", md->md_name);
	pp->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE;
	if (md->md_size != 0) {
		pp->mediasize = md->md_size -
		    ((md->md_uuid[0] != 0) ? md->md_sectorsize : 0);
		pp->sectorsize = md->md_sectorsize;
	}
	sc->sc_pp = pp;
	g_error_provider(pp, 0);
	printf("GEOM_MULTIPATH: %s created\n", gp->name);
	return (gp);
}

static int
g_multipath_add_disk(struct g_geom *gp, struct g_provider *pp)
{
	struct g_multipath_softc *sc;
	struct g_consumer *cp, *nxtcp;
	int error, acr, acw, ace;

	g_topology_assert();

	sc = gp->softc;
	KASSERT(sc, ("no softc"));

	/*
	 * Make sure that the passed provider isn't already attached
	 */
	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp->provider == pp)
			break;
	}
	if (cp) {
		printf("GEOM_MULTIPATH: provider %s already attached to %s\n",
		    pp->name, gp->name);
		return (EEXIST);
	}
	nxtcp = LIST_FIRST(&gp->consumer);
	cp = g_new_consumer(gp);
	cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
	cp->private = NULL;
	cp->index = MP_NEW;
	error = g_attach(cp, pp);
	if (error != 0) {
		printf("GEOM_MULTIPATH: cannot attach %s to %s",
		    pp->name, sc->sc_name);
		g_destroy_consumer(cp);
		return (error);
	}

	/*
	 * Set access permissions on new consumer to match other consumers
	 */
	if (sc->sc_pp) {
		acr = sc->sc_pp->acr;
		acw = sc->sc_pp->acw;
		ace = sc->sc_pp->ace;
	} else
		acr = acw = ace = 0;
	if (g_multipath_exclusive) {
		acr++;
		acw++;
		ace++;
	}
	error = g_access(cp, acr, acw, ace);
	if (error) {
		printf("GEOM_MULTIPATH: cannot set access in "
		    "attaching %s to %s (%d)\n",
		    pp->name, sc->sc_name, error);
		g_detach(cp);
		g_destroy_consumer(cp);
		return (error);
	}
	if (sc->sc_size == 0) {
		sc->sc_size = pp->mediasize -
		    ((sc->sc_uuid[0] != 0) ? pp->sectorsize : 0);
		sc->sc_pp->mediasize = sc->sc_size;
		sc->sc_pp->sectorsize = pp->sectorsize;
	}
	if (sc->sc_pp->stripesize == 0 && sc->sc_pp->stripeoffset == 0) {
		sc->sc_pp->stripesize = pp->stripesize;
		sc->sc_pp->stripeoffset = pp->stripeoffset;
	}
	sc->sc_pp->flags |= pp->flags & G_PF_ACCEPT_UNMAPPED;
	mtx_lock(&sc->sc_mtx);
	cp->index = 0;
	sc->sc_ndisks++;
	mtx_unlock(&sc->sc_mtx);
	printf("GEOM_MULTIPATH: %s added to %s\n",
	    pp->name, sc->sc_name);
	if (sc->sc_active == NULL) {
		sc->sc_active = cp;
		if (sc->sc_active_active != 1)
			printf("GEOM_MULTIPATH: %s is now active path in %s\n",
			    pp->name, sc->sc_name);
	}
	return (0);
}

static int
g_multipath_destroy(struct g_geom *gp)
{
	struct g_multipath_softc *sc;
	struct g_consumer *cp, *cp1;

	g_topology_assert();
	if (gp->softc == NULL)
		return (ENXIO);
	sc = gp->softc;
	if (!sc->sc_stopping) {
		printf("GEOM_MULTIPATH: destroying %s\n", gp->name);
		sc->sc_stopping = 1;
	}
	if (sc->sc_opened != 0) {
		g_wither_provider(sc->sc_pp, ENXIO);
		sc->sc_pp = NULL;
		return (EINPROGRESS);
	}
	LIST_FOREACH_SAFE(cp, &gp->consumer, consumer, cp1) {
		mtx_lock(&sc->sc_mtx);
		if ((cp->index & MP_POSTED) == 0) {
			cp->index |= MP_POSTED;
			mtx_unlock(&sc->sc_mtx);
			g_mpd(cp, 0);
			if (cp1 == NULL)
				return(0);	/* Recursion happened. */
		} else
			mtx_unlock(&sc->sc_mtx);
	}
	if (!LIST_EMPTY(&gp->consumer))
		return (EINPROGRESS);
	mtx_destroy(&sc->sc_mtx);
	g_free(gp->softc);
	gp->softc = NULL;
	printf("GEOM_MULTIPATH: %s destroyed\n", gp->name);
	g_wither_geom(gp, ENXIO);
	return (0);
}

static int
g_multipath_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp)
{

	return (g_multipath_destroy(gp));
}

static int
g_multipath_rotate(struct g_geom *gp)
{
	struct g_consumer *lcp, *first_good_cp = NULL;
	struct g_multipath_softc *sc = gp->softc;
	int active_cp_seen = 0;

	g_topology_assert();
	if (sc == NULL)
		return (ENXIO);
	LIST_FOREACH(lcp, &gp->consumer, consumer) {
		if ((lcp->index & MP_BAD) == 0) {
			if (first_good_cp == NULL)
				first_good_cp = lcp;
			if (active_cp_seen)
				break;
		}
		if (sc->sc_active == lcp)
			active_cp_seen = 1;
	}
	if (lcp == NULL)
		lcp = first_good_cp;
	if (lcp && lcp != sc->sc_active) {
		sc->sc_active = lcp;
		if (sc->sc_active_active != 1)
			printf("GEOM_MULTIPATH: %s is now active path in %s\n",
			    lcp->provider->name, sc->sc_name);
	}
	return (0);
}

static void
g_multipath_init(struct g_class *mp)
{
	bioq_init(&gmtbq);
	mtx_init(&gmtbq_mtx, "gmtbq", NULL, MTX_DEF);
	kproc_create(g_multipath_kt, mp, NULL, 0, 0, "g_mp_kt");
}

static void
g_multipath_fini(struct g_class *mp)
{
	if (g_multipath_kt_state == GKT_RUN) {
		mtx_lock(&gmtbq_mtx);
		g_multipath_kt_state = GKT_DIE;
		wakeup(&g_multipath_kt_state);
		msleep(&g_multipath_kt_state, &gmtbq_mtx, PRIBIO,
		    "gmp:fini", 0);
		mtx_unlock(&gmtbq_mtx);
	}
}

static int
g_multipath_read_metadata(struct g_consumer *cp,
    struct g_multipath_metadata *md)
{
	struct g_provider *pp;
	u_char *buf;
	int error;

	g_topology_assert();
	error = g_access(cp, 1, 0, 0);
	if (error != 0)
		return (error);
	pp = cp->provider;
	g_topology_unlock();
	buf = g_read_data(cp, pp->mediasize - pp->sectorsize,
	    pp->sectorsize, &error);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (buf == NULL)
		return (error);
	multipath_metadata_decode(buf, md);
	g_free(buf);
	return (0);
}

static int
g_multipath_write_metadata(struct g_consumer *cp,
    struct g_multipath_metadata *md)
{
	struct g_provider *pp;
	u_char *buf;
	int error;

	g_topology_assert();
	error = g_access(cp, 1, 1, 1);
	if (error != 0)
		return (error);
	pp = cp->provider;
	g_topology_unlock();
	buf = g_malloc(pp->sectorsize, M_WAITOK | M_ZERO);
	multipath_metadata_encode(md, buf);
	error = g_write_data(cp, pp->mediasize - pp->sectorsize,
	    buf, pp->sectorsize);
	g_topology_lock();
	g_access(cp, -1, -1, -1);
	g_free(buf);
	return (error);
}

static struct g_geom *
g_multipath_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_multipath_metadata md;
	struct g_multipath_softc *sc;
	struct g_consumer *cp;
	struct g_geom *gp, *gp1;
	int error, isnew;

	g_topology_assert();

	gp = g_new_geomf(mp, "multipath:taste");
	gp->start = g_multipath_start;
	gp->access = g_multipath_access;
	gp->orphan = g_multipath_orphan;
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_multipath_read_metadata(cp, &md);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	if (error != 0)
		return (NULL);
	gp = NULL;

	if (strcmp(md.md_magic, G_MULTIPATH_MAGIC) != 0) {
		if (g_multipath_debug)
			printf("%s is not MULTIPATH\n", pp->name);
		return (NULL);
	}
	if (md.md_version != G_MULTIPATH_VERSION) {
		printf("%s has version %d multipath id- this module is version "
		    " %d: rejecting\n", pp->name, md.md_version,
		    G_MULTIPATH_VERSION);
		return (NULL);
	}
	if (md.md_size != 0 && md.md_size != pp->mediasize)
		return (NULL);
	if (md.md_sectorsize != 0 && md.md_sectorsize != pp->sectorsize)
		return (NULL);
	if (g_multipath_debug)
		printf("MULTIPATH: %s/%s\n", md.md_name, md.md_uuid);

	/*
	 * Let's check if such a device already is present. We check against
	 * uuid alone first because that's the true distinguishor. If that
	 * passes, then we check for name conflicts. If there are conflicts, 
	 * modify the name.
	 *
	 * The whole purpose of this is to solve the problem that people don't
	 * pick good unique names, but good unique names (like uuids) are a
	 * pain to use. So, we allow people to build GEOMs with friendly names
	 * and uuids, and modify the names in case there's a collision.
	 */
	sc = NULL;
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL || sc->sc_stopping)
			continue;
		if (strncmp(md.md_uuid, sc->sc_uuid, sizeof(md.md_uuid)) == 0)
			break;
	}

	LIST_FOREACH(gp1, &mp->geom, geom) {
		if (gp1 == gp)
			continue;
		sc = gp1->softc;
		if (sc == NULL || sc->sc_stopping)
			continue;
		if (strncmp(md.md_name, sc->sc_name, sizeof(md.md_name)) == 0)
			break;
	}

	/*
	 * If gp is NULL, we had no extant MULTIPATH geom with this uuid.
	 *
	 * If gp1 is *not* NULL, that means we have a MULTIPATH geom extant
	 * with the same name (but a different UUID).
	 *
	 * If gp is NULL, then modify the name with a random number and
  	 * complain, but allow the creation of the geom to continue.
	 *
	 * If gp is *not* NULL, just use the geom's name as we're attaching
	 * this disk to the (previously generated) name.
	 */

	if (gp1) {
		sc = gp1->softc;
		if (gp == NULL) {
			char buf[16];
			u_long rand = random();

			snprintf(buf, sizeof (buf), "%s-%lu", md.md_name, rand);
			printf("GEOM_MULTIPATH: geom %s/%s exists already\n",
			    sc->sc_name, sc->sc_uuid);
			printf("GEOM_MULTIPATH: %s will be (temporarily) %s\n",
			    md.md_uuid, buf);
			strlcpy(md.md_name, buf, sizeof(md.md_name));
		} else {
			strlcpy(md.md_name, sc->sc_name, sizeof(md.md_name));
		}
	}

	if (gp == NULL) {
		gp = g_multipath_create(mp, &md);
		if (gp == NULL) {
			printf("GEOM_MULTIPATH: cannot create geom %s/%s\n",
			    md.md_name, md.md_uuid);
			return (NULL);
		}
		isnew = 1;
	} else {
		isnew = 0;
	}

	sc = gp->softc;
	KASSERT(sc != NULL, ("sc is NULL"));
	error = g_multipath_add_disk(gp, pp);
	if (error != 0) {
		if (isnew)
			g_multipath_destroy(gp);
		return (NULL);
	}
	return (gp);
}

static void
g_multipath_ctl_add_name(struct gctl_req *req, struct g_class *mp,
    const char *name)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_provider *pp;
	const char *mpname;
	static const char devpf[6] = "/dev/";
	int error;

	g_topology_assert();

	mpname = gctl_get_asciiparam(req, "arg0");
        if (mpname == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, mpname);
	if (gp == NULL) {
		gctl_error(req, "Device %s is invalid", mpname);
		return;
	}
	sc = gp->softc;

	if (strncmp(name, devpf, 5) == 0)
		name += 5;
	pp = g_provider_by_name(name);
	if (pp == NULL) {
		gctl_error(req, "Provider %s is invalid", name);
		return;
	}

	/*
	 * Check to make sure parameters match.
	 */
	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp->provider == pp) {
			gctl_error(req, "provider %s is already there",
			    pp->name);
			return;
		}
	}
	if (sc->sc_pp->mediasize != 0 &&
	    sc->sc_pp->mediasize + (sc->sc_uuid[0] != 0 ? pp->sectorsize : 0)
	     != pp->mediasize) {
		gctl_error(req, "Providers size mismatch %jd != %jd",
		    (intmax_t) sc->sc_pp->mediasize +
			(sc->sc_uuid[0] != 0 ? pp->sectorsize : 0),
		    (intmax_t) pp->mediasize);
		return;
	}
	if (sc->sc_pp->sectorsize != 0 &&
	    sc->sc_pp->sectorsize != pp->sectorsize) {
		gctl_error(req, "Providers sectorsize mismatch %u != %u",
		    sc->sc_pp->sectorsize, pp->sectorsize);
		return;
	}

	error = g_multipath_add_disk(gp, pp);
	if (error != 0)
		gctl_error(req, "Provider addition error: %d", error);
}

static void
g_multipath_ctl_prefer(struct gctl_req *req, struct g_class *mp)
{
	struct g_geom *gp;
	struct g_multipath_softc *sc;
	struct g_consumer *cp;
	const char *name, *mpname;
	static const char devpf[6] = "/dev/";
	int *nargs;

	g_topology_assert();

	mpname = gctl_get_asciiparam(req, "arg0");
        if (mpname == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, mpname);
	if (gp == NULL) {
		gctl_error(req, "Device %s is invalid", mpname);
		return;
	}
	sc = gp->softc;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No 'nargs' argument");
		return;
	}
	if (*nargs != 2) {
		gctl_error(req, "missing device");
		return;
	}

	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg1' argument");
		return;
	}
	if (strncmp(name, devpf, 5) == 0) {
		name += 5;
	}

	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp->provider != NULL
                      && strcmp(cp->provider->name, name) == 0)
		    break;
	}

	if (cp == NULL) {
		gctl_error(req, "Provider %s not found", name);
		return;
	}

	mtx_lock(&sc->sc_mtx);

	if (cp->index & MP_BAD) {
		gctl_error(req, "Consumer %s is invalid", name);
		mtx_unlock(&sc->sc_mtx);
		return;
	}

	/* Here when the consumer is present and in good shape */

	sc->sc_active = cp;
	if (!sc->sc_active_active)
	    printf("GEOM_MULTIPATH: %s now active path in %s\n",
		sc->sc_active->provider->name, sc->sc_name);

	mtx_unlock(&sc->sc_mtx);
}

static void
g_multipath_ctl_add(struct gctl_req *req, struct g_class *mp)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	const char *mpname, *name;

	mpname = gctl_get_asciiparam(req, "arg0");
        if (mpname == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, mpname);
	if (gp == NULL) {
		gctl_error(req, "Device %s not found", mpname);
		return;
	}
	sc = gp->softc;

	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg1' argument");
		return;
	}
	g_multipath_ctl_add_name(req, mp, name);
}

static void
g_multipath_ctl_create(struct gctl_req *req, struct g_class *mp)
{
	struct g_multipath_metadata md;
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	const char *mpname, *name;
	char param[16];
	int *nargs, i, *val;

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (*nargs < 2) {
		gctl_error(req, "wrong number of arguments.");
		return;
	}

	mpname = gctl_get_asciiparam(req, "arg0");
        if (mpname == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, mpname);
	if (gp != NULL) {
		gctl_error(req, "Device %s already exist", mpname);
		return;
	}

	memset(&md, 0, sizeof(md));
	strlcpy(md.md_magic, G_MULTIPATH_MAGIC, sizeof(md.md_magic));
	md.md_version = G_MULTIPATH_VERSION;
	strlcpy(md.md_name, mpname, sizeof(md.md_name));
	md.md_size = 0;
	md.md_sectorsize = 0;
	md.md_uuid[0] = 0;
	md.md_active_active = 0;
	val = gctl_get_paraml(req, "active_active", sizeof(*val));
	if (val != NULL && *val != 0)
		md.md_active_active = 1;
	val = gctl_get_paraml(req, "active_read", sizeof(*val));
	if (val != NULL && *val != 0)
		md.md_active_active = 2;
	gp = g_multipath_create(mp, &md);
	if (gp == NULL) {
		gctl_error(req, "GEOM_MULTIPATH: cannot create geom %s/%s\n",
		    md.md_name, md.md_uuid);
		return;
	}
	sc = gp->softc;

	for (i = 1; i < *nargs; i++) {
		snprintf(param, sizeof(param), "arg%d", i);
		name = gctl_get_asciiparam(req, param);
		g_multipath_ctl_add_name(req, mp, name);
	}

	if (sc->sc_ndisks != (*nargs - 1))
		g_multipath_destroy(gp);
}

static void
g_multipath_ctl_configure(struct gctl_req *req, struct g_class *mp)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_provider *pp;
	struct g_multipath_metadata md;
	const char *name;
	int error, *val;

	g_topology_assert();

	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg0' argument");
		return;
	}
	gp = g_multipath_find_geom(mp, name);
	if (gp == NULL) {
		gctl_error(req, "Device %s is invalid", name);
		return;
	}
	sc = gp->softc;
	val = gctl_get_paraml(req, "active_active", sizeof(*val));
	if (val != NULL && *val != 0)
		sc->sc_active_active = 1;
	val = gctl_get_paraml(req, "active_read", sizeof(*val));
	if (val != NULL && *val != 0)
		sc->sc_active_active = 2;
	val = gctl_get_paraml(req, "active_passive", sizeof(*val));
	if (val != NULL && *val != 0)
		sc->sc_active_active = 0;
	if (sc->sc_uuid[0] != 0 && sc->sc_active != NULL) {
		cp = sc->sc_active;
		pp = cp->provider;
		strlcpy(md.md_magic, G_MULTIPATH_MAGIC, sizeof(md.md_magic));
		memcpy(md.md_uuid, sc->sc_uuid, sizeof (sc->sc_uuid));
		strlcpy(md.md_name, name, sizeof(md.md_name));
		md.md_version = G_MULTIPATH_VERSION;
		md.md_size = pp->mediasize;
		md.md_sectorsize = pp->sectorsize;
		md.md_active_active = sc->sc_active_active;
		error = g_multipath_write_metadata(cp, &md);
		if (error != 0)
			gctl_error(req, "Can't update metadata on %s (%d)",
			    pp->name, error);
	}
}

static void
g_multipath_ctl_fail(struct gctl_req *req, struct g_class *mp, int fail)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp;
	const char *mpname, *name;
	int found;

	mpname = gctl_get_asciiparam(req, "arg0");
        if (mpname == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, mpname);
	if (gp == NULL) {
		gctl_error(req, "Device %s not found", mpname);
		return;
	}
	sc = gp->softc;

	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg1' argument");
		return;
	}

	found = 0;
	mtx_lock(&sc->sc_mtx);
	LIST_FOREACH(cp, &gp->consumer, consumer) {
		if (cp->provider != NULL &&
		    strcmp(cp->provider->name, name) == 0 &&
		    (cp->index & MP_LOST) == 0) {
			found = 1;
			if (!fail == !(cp->index & MP_FAIL))
				continue;
			printf("GEOM_MULTIPATH: %s in %s is marked %s.\n",
				name, sc->sc_name, fail ? "FAIL" : "OK");
			if (fail) {
				g_multipath_fault(cp, MP_FAIL);
			} else {
				cp->index &= ~MP_FAIL;
			}
		}
	}
	mtx_unlock(&sc->sc_mtx);
	if (found == 0)
		gctl_error(req, "Provider %s not found", name);
}

static void
g_multipath_ctl_remove(struct gctl_req *req, struct g_class *mp)
{
	struct g_multipath_softc *sc;
	struct g_geom *gp;
	struct g_consumer *cp, *cp1;
	const char *mpname, *name;
	uintptr_t *cnt;
	int found;

	mpname = gctl_get_asciiparam(req, "arg0");
        if (mpname == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, mpname);
	if (gp == NULL) {
		gctl_error(req, "Device %s not found", mpname);
		return;
	}
	sc = gp->softc;

	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg1' argument");
		return;
	}

	found = 0;
	mtx_lock(&sc->sc_mtx);
	LIST_FOREACH_SAFE(cp, &gp->consumer, consumer, cp1) {
		if (cp->provider != NULL &&
		    strcmp(cp->provider->name, name) == 0 &&
		    (cp->index & MP_LOST) == 0) {
			found = 1;
			printf("GEOM_MULTIPATH: removing %s from %s\n",
			    cp->provider->name, cp->geom->name);
			sc->sc_ndisks--;
			g_multipath_fault(cp, MP_LOST);
			cnt = (uintptr_t *)&cp->private;
			if (*cnt == 0 && (cp->index & MP_POSTED) == 0) {
				cp->index |= MP_POSTED;
				mtx_unlock(&sc->sc_mtx);
				g_mpd(cp, 0);
				if (cp1 == NULL)
					return;	/* Recursion happened. */
				mtx_lock(&sc->sc_mtx);
			}
		}
	}
	mtx_unlock(&sc->sc_mtx);
	if (found == 0)
		gctl_error(req, "Provider %s not found", name);
}

static struct g_geom *
g_multipath_find_geom(struct g_class *mp, const char *name)
{
	struct g_geom *gp;
	struct g_multipath_softc *sc;

	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL || sc->sc_stopping)
			continue;
		if (strcmp(gp->name, name) == 0)
			return (gp);
	}
	return (NULL);
}

static void
g_multipath_ctl_stop(struct gctl_req *req, struct g_class *mp)
{
	struct g_geom *gp;
	const char *name;
	int error;

	g_topology_assert();

	name = gctl_get_asciiparam(req, "arg0");
        if (name == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, name);
	if (gp == NULL) {
		gctl_error(req, "Device %s is invalid", name);
		return;
	}
	error = g_multipath_destroy(gp);
	if (error != 0 && error != EINPROGRESS)
		gctl_error(req, "failed to stop %s (err=%d)", name, error);
}

static void
g_multipath_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	struct g_geom *gp;
	struct g_multipath_softc *sc;
	struct g_consumer *cp;
	struct g_provider *pp;
	const char *name;
	uint8_t *buf;
	int error;

	g_topology_assert();

	name = gctl_get_asciiparam(req, "arg0");
        if (name == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, name);
	if (gp == NULL) {
		gctl_error(req, "Device %s is invalid", name);
		return;
	}
	sc = gp->softc;

	if (sc->sc_uuid[0] != 0 && sc->sc_active != NULL) {
		cp = sc->sc_active;
		pp = cp->provider;
		error = g_access(cp, 1, 1, 1);
		if (error != 0) {
			gctl_error(req, "Can't open %s (%d)", pp->name, error);
			goto destroy;
		}
		g_topology_unlock();
		buf = g_malloc(pp->sectorsize, M_WAITOK | M_ZERO);
		error = g_write_data(cp, pp->mediasize - pp->sectorsize,
		    buf, pp->sectorsize);
		g_topology_lock();
		g_access(cp, -1, -1, -1);
		if (error != 0)
			gctl_error(req, "Can't erase metadata on %s (%d)",
			    pp->name, error);
	}

destroy:
	error = g_multipath_destroy(gp);
	if (error != 0 && error != EINPROGRESS)
		gctl_error(req, "failed to destroy %s (err=%d)", name, error);
}

static void
g_multipath_ctl_rotate(struct gctl_req *req, struct g_class *mp)
{
	struct g_geom *gp;
	const char *name;
	int error;

	g_topology_assert();

	name = gctl_get_asciiparam(req, "arg0");
        if (name == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, name);
	if (gp == NULL) {
		gctl_error(req, "Device %s is invalid", name);
		return;
	}
	error = g_multipath_rotate(gp);
	if (error != 0) {
		gctl_error(req, "failed to rotate %s (err=%d)", name, error);
	}
}

static void
g_multipath_ctl_getactive(struct gctl_req *req, struct g_class *mp)
{
	struct sbuf *sb;
	struct g_geom *gp;
	struct g_multipath_softc *sc;
	struct g_consumer *cp;
	const char *name;
	int empty;

	sb = sbuf_new_auto();

	g_topology_assert();
	name = gctl_get_asciiparam(req, "arg0");
        if (name == NULL) {
                gctl_error(req, "No 'arg0' argument");
                return;
        }
	gp = g_multipath_find_geom(mp, name);
	if (gp == NULL) {
		gctl_error(req, "Device %s is invalid", name);
		return;
	}
	sc = gp->softc;
	if (sc->sc_active_active == 1) {
		empty = 1;
		LIST_FOREACH(cp, &gp->consumer, consumer) {
			if (cp->index & MP_BAD)
				continue;
			if (!empty)
				sbuf_cat(sb, " ");
			sbuf_cat(sb, cp->provider->name);
			empty = 0;
		}
		if (empty)
			sbuf_cat(sb, "none");
		sbuf_cat(sb, "\n");
	} else if (sc->sc_active && sc->sc_active->provider) {
		sbuf_printf(sb, "%s\n", sc->sc_active->provider->name);
	} else {
		sbuf_printf(sb, "none\n");
	}
	sbuf_finish(sb);
	gctl_set_param_err(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
	sbuf_delete(sb);
}

static void
g_multipath_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;
	g_topology_assert();
	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No 'version' argument");
	} else if (*version != G_MULTIPATH_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync");
	} else if (strcmp(verb, "add") == 0) {
		g_multipath_ctl_add(req, mp);
	} else if (strcmp(verb, "prefer") == 0) {
		g_multipath_ctl_prefer(req, mp);
	} else if (strcmp(verb, "create") == 0) {
		g_multipath_ctl_create(req, mp);
	} else if (strcmp(verb, "configure") == 0) {
		g_multipath_ctl_configure(req, mp);
	} else if (strcmp(verb, "stop") == 0) {
		g_multipath_ctl_stop(req, mp);
	} else if (strcmp(verb, "destroy") == 0) {
		g_multipath_ctl_destroy(req, mp);
	} else if (strcmp(verb, "fail") == 0) {
		g_multipath_ctl_fail(req, mp, 1);
	} else if (strcmp(verb, "restore") == 0) {
		g_multipath_ctl_fail(req, mp, 0);
	} else if (strcmp(verb, "remove") == 0) {
		g_multipath_ctl_remove(req, mp);
	} else if (strcmp(verb, "rotate") == 0) {
		g_multipath_ctl_rotate(req, mp);
	} else if (strcmp(verb, "getactive") == 0) {
		g_multipath_ctl_getactive(req, mp);
	} else {
		gctl_error(req, "Unknown verb %s", verb);
	}
}

static void
g_multipath_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_multipath_softc *sc;
	int good;

	g_topology_assert();

	sc = gp->softc;
	if (sc == NULL)
		return;
	if (cp != NULL) {
		sbuf_printf(sb, "%s<State>%s</State>\n", indent,
		    (cp->index & MP_NEW) ? "NEW" :
		    (cp->index & MP_LOST) ? "LOST" :
		    (cp->index & MP_FAIL) ? "FAIL" :
		    (sc->sc_active_active == 1 || sc->sc_active == cp) ?
		     "ACTIVE" :
		     sc->sc_active_active == 2 ? "READ" : "PASSIVE");
	} else {
		good = g_multipath_good(gp);
		sbuf_printf(sb, "%s<State>%s</State>\n", indent,
		    good == 0 ? "BROKEN" :
		    (good != sc->sc_ndisks || sc->sc_ndisks == 1) ?
		    "DEGRADED" : "OPTIMAL");
	}
	if (cp == NULL && pp == NULL) {
		sbuf_printf(sb, "%s<UUID>%s</UUID>\n", indent, sc->sc_uuid);
		sbuf_printf(sb, "%s<Mode>Active/%s</Mode>\n", indent,
		    sc->sc_active_active == 2 ? "Read" :
		    sc->sc_active_active == 1 ? "Active" : "Passive");
		sbuf_printf(sb, "%s<Type>%s</Type>\n", indent,
		    sc->sc_uuid[0] == 0 ? "MANUAL" : "AUTOMATIC");
	}
}

DECLARE_GEOM_CLASS(g_multipath_class, g_multipath);
MODULE_VERSION(geom_multipath, 0);
