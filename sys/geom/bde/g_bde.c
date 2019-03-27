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
 *
 */

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/sysctl.h>

#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha512.h>
#include <geom/geom.h>
#include <geom/bde/g_bde.h>
#define BDE_CLASS_NAME "BDE"

FEATURE(geom_bde, "GEOM-based Disk Encryption");

static void
g_bde_start(struct bio *bp)
{

	switch (bp->bio_cmd) {
	case BIO_DELETE:
	case BIO_READ:
	case BIO_WRITE:
		g_bde_start1(bp);
		break;
	case BIO_GETATTR:
		g_io_deliver(bp, EOPNOTSUPP);
		break;
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}
	return;
}

static void
g_bde_orphan(struct g_consumer *cp)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_bde_softc *sc;

	g_trace(G_T_TOPOLOGY, "g_bde_orphan(%p/%s)", cp, cp->provider->name);
	g_topology_assert();

	gp = cp->geom;
	sc = gp->softc;
	gp->flags |= G_GEOM_WITHER;
	LIST_FOREACH(pp, &gp->provider, provider)
		g_wither_provider(pp, ENXIO);
	bzero(sc, sizeof(struct g_bde_softc));	/* destroy evidence */
	return;
}

static int
g_bde_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_consumer *cp;

	gp = pp->geom;
	cp = LIST_FIRST(&gp->consumer);
	if (cp->acr == 0 && cp->acw == 0 && cp->ace == 0) {
		de++;
		dr++;
	}
	/* ... and let go of it on last close */
	if ((cp->acr + dr) == 0 && (cp->acw + dw) == 0 && (cp->ace + de) == 1) {
		de--;
		dr--;
	}
	return (g_access(cp, dr, dw, de));
}

static void
g_bde_create_geom(struct gctl_req *req, struct g_class *mp, struct g_provider *pp)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_bde_key *kp;
	int error, i;
	u_int sectorsize;
	off_t mediasize;
	struct g_bde_softc *sc;
	void *pass;
	void *key;

	g_trace(G_T_TOPOLOGY, "g_bde_create_geom(%s, %s)", mp->name, pp->name);
	g_topology_assert();
	gp = NULL;


	gp = g_new_geomf(mp, "%s.bde", pp->name);
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_access(cp, 1, 1, 1);
	if (error) {
		g_detach(cp);
		g_destroy_consumer(cp);
		g_destroy_geom(gp);
		gctl_error(req, "could not access consumer");
		return;
	}
	pass = NULL;
	key = NULL;
	do {
		pass = gctl_get_param(req, "pass", &i);
		if (pass == NULL || i != SHA512_DIGEST_LENGTH) {
			gctl_error(req, "No usable key presented");
			break;
		}
		key = gctl_get_param(req, "key", &i);
		if (key != NULL && i != 16) {
			gctl_error(req, "Invalid key presented");
			break;
		}
		sectorsize = cp->provider->sectorsize;
		mediasize = cp->provider->mediasize;
		sc = g_malloc(sizeof(struct g_bde_softc), M_WAITOK | M_ZERO);
		gp->softc = sc;
		sc->geom = gp;
		sc->consumer = cp;

		error = g_bde_decrypt_lock(sc, pass, key,
		    mediasize, sectorsize, NULL);
		bzero(sc->sha2, sizeof sc->sha2);
		if (error)
			break;
		kp = &sc->key;

		/* Initialize helper-fields */
		kp->keys_per_sector = kp->sectorsize / G_BDE_SKEYLEN;
		kp->zone_cont = kp->keys_per_sector * kp->sectorsize;
		kp->zone_width = kp->zone_cont + kp->sectorsize;
		kp->media_width = kp->sectorN - kp->sector0 -
		    G_BDE_MAXKEYS * kp->sectorsize;

		/* Our external parameters */
		sc->zone_cont = kp->zone_cont;
		sc->mediasize = g_bde_max_sector(kp);
		sc->sectorsize = kp->sectorsize;

		TAILQ_INIT(&sc->freelist);
		TAILQ_INIT(&sc->worklist);
		mtx_init(&sc->worklist_mutex, "g_bde_worklist", NULL, MTX_DEF);
		/* XXX: error check */
		kproc_create(g_bde_worker, gp, &sc->thread, 0, 0,
			"g_bde %s", gp->name);
		pp = g_new_providerf(gp, "%s", gp->name);
		pp->stripesize = kp->zone_cont;
		pp->stripeoffset = 0;
		pp->mediasize = sc->mediasize;
		pp->sectorsize = sc->sectorsize;
		g_error_provider(pp, 0);
		break;
	} while (0);
	if (pass != NULL)
		bzero(pass, SHA512_DIGEST_LENGTH);
	if (key != NULL)
		bzero(key, 16);
	if (error == 0)
		return;
	g_access(cp, -1, -1, -1);
	g_detach(cp);
	g_destroy_consumer(cp);
	if (gp->softc != NULL)
		g_free(gp->softc);
	g_destroy_geom(gp);
	switch (error) {
	case ENOENT:
		gctl_error(req, "Lock was destroyed");
		break;
	case ESRCH:
		gctl_error(req, "Lock was nuked");
		break;
	case EINVAL:
		gctl_error(req, "Could not open lock");
		break;
	case ENOTDIR:
		gctl_error(req, "Lock not found");
		break;
	default:
		gctl_error(req, "Could not open lock (%d)", error);
		break;
	}
	return;
}


static int
g_bde_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{
	struct g_consumer *cp;
	struct g_provider *pp;
	struct g_bde_softc *sc;

	g_trace(G_T_TOPOLOGY, "g_bde_destroy_geom(%s, %s)", mp->name, gp->name);
	g_topology_assert();
	/*
	 * Orderly detachment.
	 */
	KASSERT(gp != NULL, ("NULL geom"));
	pp = LIST_FIRST(&gp->provider);
	KASSERT(pp != NULL, ("NULL provider"));
	if (pp->acr > 0 || pp->acw > 0 || pp->ace > 0)
		return (EBUSY);
	sc = gp->softc;
	cp = LIST_FIRST(&gp->consumer);
	KASSERT(cp != NULL, ("NULL consumer"));
	sc->dead = 1;
	wakeup(sc);
	g_access(cp, -1, -1, -1);
	g_detach(cp);
	g_destroy_consumer(cp);
	while (sc->dead != 2 && !LIST_EMPTY(&pp->consumers))
		tsleep(sc, PRIBIO, "g_bdedie", hz);
	mtx_destroy(&sc->worklist_mutex);
	bzero(&sc->key, sizeof sc->key);
	g_free(sc);
	g_wither_geom(gp, ENXIO);
	return (0);
}

static void
g_bde_ctlreq(struct gctl_req *req, struct g_class *mp, char const *verb)
{
	struct g_geom *gp;
	struct g_provider *pp;

	if (!strcmp(verb, "create geom")) {
		pp = gctl_get_provider(req, "provider");
		if (pp != NULL)
			g_bde_create_geom(req, mp, pp);
	} else if (!strcmp(verb, "destroy geom")) {
		gp = gctl_get_geom(req, mp, "geom");
		if (gp != NULL)
			g_bde_destroy_geom(req, mp, gp);
	} else {
		gctl_error(req, "unknown verb");
	}
}

static struct g_class g_bde_class	= {
	.name = BDE_CLASS_NAME,
	.version = G_VERSION,
	.destroy_geom = g_bde_destroy_geom,
	.ctlreq = g_bde_ctlreq,
	.start = g_bde_start,
	.orphan = g_bde_orphan,
	.access = g_bde_access,
	.spoiled = g_std_spoiled,
};

DECLARE_GEOM_CLASS(g_bde_class, g_bde);
MODULE_VERSION(geom_bde, 0);
