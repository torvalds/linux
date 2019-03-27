/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Poul-Henning Kamp
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

/* This is a GEOM module for handling path selection for multi-path
 * storage devices.  It is named "fox" because it, like they, prefer
 * to have multiple exits to choose from.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/libkern.h>
#include <sys/endian.h>
#include <sys/md5.h>
#include <sys/errno.h>
#include <geom/geom.h>

#define FOX_CLASS_NAME "FOX"
#define FOX_MAGIC	"GEOM::FOX"

static int g_fox_once;

FEATURE(geom_fox, "GEOM FOX redundant path mitigation support");

struct g_fox_softc {
	off_t			mediasize;
	u_int			sectorsize;
	TAILQ_HEAD(, bio)	queue;
	struct mtx		lock;
	u_char 			magic[16];
	struct g_consumer 	*path;
	struct g_consumer 	*opath;
	int			waiting;
	int			cr, cw, ce;
};

/*
 * This function is called whenever we need to select a new path.
 */
static void
g_fox_select_path(void *arg, int flag)
{
	struct g_geom *gp;
	struct g_fox_softc *sc;
	struct g_consumer *cp1;
	struct bio *bp;
	int error;

	g_topology_assert();
	if (flag == EV_CANCEL)
		return;
	gp = arg;
	sc = gp->softc;

	if (sc->opath != NULL) {
		/*
		 * First, close the old path entirely.
		 */
		printf("Closing old path (%s) on fox (%s)\n",
			sc->opath->provider->name, gp->name);

		cp1 = LIST_NEXT(sc->opath, consumer);

		g_access(sc->opath, -sc->cr, -sc->cw, -(sc->ce + 1));

		/*
		 * The attempt to reopen it with a exclusive count
		 */
		error = g_access(sc->opath, 0, 0, 1);
		if (error) {
			/*
			 * Ok, ditch this consumer, we can't use it.
			 */
			printf("Drop old path (%s) on fox (%s)\n",
				sc->opath->provider->name, gp->name);
			g_detach(sc->opath);
			g_destroy_consumer(sc->opath);
			if (LIST_EMPTY(&gp->consumer)) {
				/* No consumers left */
				g_wither_geom(gp, ENXIO);
				for (;;) {
					bp = TAILQ_FIRST(&sc->queue);
					if (bp == NULL)
						break;
					TAILQ_REMOVE(&sc->queue, bp, bio_queue);
					bp->bio_error = ENXIO;
					g_std_done(bp);
				}
				return;
			}
		} else {
			printf("Got e-bit on old path (%s) on fox (%s)\n",
				sc->opath->provider->name, gp->name);
		}
		sc->opath = NULL;
	} else {
		cp1 = LIST_FIRST(&gp->consumer);
	}
	if (cp1 == NULL)
		cp1 = LIST_FIRST(&gp->consumer);
	printf("Open new path (%s) on fox (%s)\n",
		cp1->provider->name, gp->name);
	error = g_access(cp1, sc->cr, sc->cw, sc->ce);
	if (error) {
		/*
		 * If we failed, we take another trip through here
		 */
		printf("Open new path (%s) on fox (%s) failed, reselect.\n",
			cp1->provider->name, gp->name);
		sc->opath = cp1;
		g_post_event(g_fox_select_path, gp, M_WAITOK, gp, NULL);
	} else {
		printf("Open new path (%s) on fox (%s) succeeded\n",
			cp1->provider->name, gp->name);
		mtx_lock(&sc->lock);
		sc->path = cp1;
		sc->waiting = 0;
		for (;;) {
			bp = TAILQ_FIRST(&sc->queue);
			if (bp == NULL)
				break;
			TAILQ_REMOVE(&sc->queue, bp, bio_queue);
			g_io_request(bp, sc->path);
		}
		mtx_unlock(&sc->lock);
	}
}

static void
g_fox_orphan(struct g_consumer *cp)
{
	struct g_geom *gp;
	struct g_fox_softc *sc;
	int error, mark;

	g_topology_assert();
	gp = cp->geom;
	sc = gp->softc;
	printf("Removing path (%s) from fox (%s)\n",
	    cp->provider->name, gp->name);
	mtx_lock(&sc->lock);
	if (cp == sc->path) {
		sc->opath = NULL;
		sc->path = NULL;
		sc->waiting = 1;
		mark = 1;
	} else {
		mark = 0;
	}
	mtx_unlock(&sc->lock);
	    
	g_access(cp, -cp->acr, -cp->acw, -cp->ace);
	error = cp->provider->error;
	g_detach(cp);
	g_destroy_consumer(cp);	
	if (!LIST_EMPTY(&gp->consumer)) {
		if (mark)
			g_post_event(g_fox_select_path, gp, M_WAITOK, gp, NULL);
		return;
	}

	mtx_destroy(&sc->lock);
	g_free(gp->softc);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);
}

static void
g_fox_done(struct bio *bp)
{
	struct g_geom *gp;
	struct g_fox_softc *sc;
	int error;

	if (bp->bio_error == 0) {
		g_std_done(bp);
		return;
	}
	gp = bp->bio_from->geom;
	sc = gp->softc;
	if (bp->bio_from != sc->path) {
		g_io_request(bp, sc->path);
		return;
	}
	mtx_lock(&sc->lock);
	sc->opath = sc->path;
	sc->path = NULL;
	error = g_post_event(g_fox_select_path, gp, M_NOWAIT, gp, NULL);
	if (error) {
		bp->bio_error = ENOMEM;
		g_std_done(bp);
	} else {
		sc->waiting = 1;
		TAILQ_INSERT_TAIL(&sc->queue, bp, bio_queue);
	}
	mtx_unlock(&sc->lock);
}

static void
g_fox_start(struct bio *bp)
{
	struct g_geom *gp;
	struct bio *bp2;
	struct g_fox_softc *sc;
	int error;

	gp = bp->bio_to->geom;
	sc = gp->softc;
	if (sc == NULL) {
		g_io_deliver(bp, ENXIO);
		return;
	}
	switch(bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
		bp2 = g_clone_bio(bp);
		if (bp2 == NULL) {
			g_io_deliver(bp, ENOMEM);
			break;
		}
		bp2->bio_offset += sc->sectorsize;
		bp2->bio_done = g_fox_done;
		mtx_lock(&sc->lock);
		if (sc->path == NULL || !TAILQ_EMPTY(&sc->queue)) {
			if (sc->waiting == 0) {
				error = g_post_event(g_fox_select_path, gp,
				    M_NOWAIT, gp, NULL);
				if (error) {
					g_destroy_bio(bp2);
					bp2 = NULL;
					g_io_deliver(bp, error);
				} else {
					sc->waiting = 1;
				}
			}
			if (bp2 != NULL)
				TAILQ_INSERT_TAIL(&sc->queue, bp2,
				    bio_queue);
		} else {
			g_io_request(bp2, sc->path);
		}
		mtx_unlock(&sc->lock);
		break;
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		break;
	}
	return;
}

static int
g_fox_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_fox_softc *sc;
	struct g_consumer *cp1;
	int error;

	g_topology_assert();
	gp = pp->geom;
	sc = gp->softc;
	if (sc == NULL) {
		if (dr <= 0 && dw <= 0 && de <= 0)
			return (0);
		else
			return (ENXIO);
	}

	if (sc->cr == 0 && sc->cw == 0 && sc->ce == 0) {
		/*
		 * First open, open all consumers with an exclusive bit
		 */
		error = 0;
		LIST_FOREACH(cp1, &gp->consumer, consumer) {
			error = g_access(cp1, 0, 0, 1);
			if (error) {
				printf("FOX: access(%s,0,0,1) = %d\n",
				    cp1->provider->name, error);
				break;
			}
		}
		if (error) {
			LIST_FOREACH(cp1, &gp->consumer, consumer) {
				if (cp1->ace)
					g_access(cp1, 0, 0, -1);
			}
			return (error);
		}
	}
	if (sc->path == NULL)
		g_fox_select_path(gp, 0);
	if (sc->path == NULL)
		error = ENXIO;
	else
		error = g_access(sc->path, dr, dw, de);
	if (error == 0) {
		sc->cr += dr;
		sc->cw += dw;
		sc->ce += de;
		if (sc->cr == 0 && sc->cw == 0 && sc->ce == 0) {
			/*
			 * Last close, remove e-bit on all consumers
			 */
			LIST_FOREACH(cp1, &gp->consumer, consumer)
				g_access(cp1, 0, 0, -1);
		}
	}
	return (error);
}

static struct g_geom *
g_fox_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_geom *gp, *gp2;
	struct g_provider *pp2;
	struct g_consumer *cp, *cp2;
	struct g_fox_softc *sc, *sc2;
	int error;
	u_int sectorsize;
	u_char *buf;

	g_trace(G_T_TOPOLOGY, "fox_taste(%s, %s)", mp->name, pp->name);
	g_topology_assert();
	if (!strcmp(pp->geom->class->name, mp->name))
		return (NULL);
	gp = g_new_geomf(mp, "%s.fox", pp->name);
	gp->softc = g_malloc(sizeof(struct g_fox_softc), M_WAITOK | M_ZERO);
	sc = gp->softc;

	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_access(cp, 1, 0, 0);
	if (error) {
		g_free(sc);
		g_detach(cp);
		g_destroy_consumer(cp);	
		g_destroy_geom(gp);
		return(NULL);
	}
	do {
		sectorsize = cp->provider->sectorsize;
		g_topology_unlock();
		buf = g_read_data(cp, 0, sectorsize, NULL);
		g_topology_lock();
		if (buf == NULL)
			break;
		if (memcmp(buf, FOX_MAGIC, strlen(FOX_MAGIC)))
			break;

		/*
		 * First we need to see if this a new path for an existing fox.
		 */
		LIST_FOREACH(gp2, &mp->geom, geom) {
			sc2 = gp2->softc;
			if (sc2 == NULL)
				continue;
			if (memcmp(buf + 16, sc2->magic, sizeof sc2->magic))
				continue;
			break;
		}
		if (gp2 != NULL) {
			/*
			 * It was.  Create a new consumer for that fox,
			 * attach it, and if the fox is open, open this
			 * path with an exclusive count of one.
			 */
			printf("Adding path (%s) to fox (%s)\n",
			    pp->name, gp2->name);
			cp2 = g_new_consumer(gp2);
			g_attach(cp2, pp);
			pp2 = LIST_FIRST(&gp2->provider);
			if (pp2->acr > 0 || pp2->acw > 0 || pp2->ace > 0) {
				error = g_access(cp2, 0, 0, 1);
				if (error) {
					/*
					 * This is bad, or more likely,
					 * the user is doing something stupid
					 */
					printf(
	"WARNING: New path (%s) to fox(%s) not added: %s\n%s",
					    cp2->provider->name, gp2->name,
	"Could not get exclusive bit.",
	"WARNING: This indicates a risk of data inconsistency."
					);
					g_detach(cp2);
					g_destroy_consumer(cp2);
				}
			}
			break;
		}
		printf("Creating new fox (%s)\n", pp->name);
		sc->path = cp;
		memcpy(sc->magic, buf + 16, sizeof sc->magic);
		pp2 = g_new_providerf(gp, "%s", gp->name);
		pp2->mediasize = sc->mediasize = pp->mediasize - pp->sectorsize;
		pp2->sectorsize = sc->sectorsize = pp->sectorsize;
printf("fox %s lock %p\n", gp->name, &sc->lock);

		mtx_init(&sc->lock, "fox queue", NULL, MTX_DEF);
		TAILQ_INIT(&sc->queue);
		g_error_provider(pp2, 0);
	} while (0);
	if (buf != NULL)
		g_free(buf);
	g_access(cp, -1, 0, 0);

	if (!LIST_EMPTY(&gp->provider)) {
		if (!g_fox_once) {
			g_fox_once = 1;
			printf(
			    "WARNING: geom_fox (geom %s) is deprecated, "
			    "use gmultipath instead.\n", gp->name);
		}
		return (gp);
	}

	g_free(gp->softc);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	return (NULL);
}

static int
g_fox_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{
	struct g_fox_softc *sc;

	g_topology_assert();
	sc = gp->softc;
	mtx_destroy(&sc->lock);
	g_free(gp->softc);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);
	return (0);
}

static struct g_class g_fox_class	= {
	.name = FOX_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_fox_taste,
	.destroy_geom = g_fox_destroy_geom,
	.start = g_fox_start,
	.spoiled = g_fox_orphan,
	.orphan = g_fox_orphan,
	.access= g_fox_access,
};

DECLARE_GEOM_CLASS(g_fox_class, g_fox);
MODULE_VERSION(geom_fox, 0);
