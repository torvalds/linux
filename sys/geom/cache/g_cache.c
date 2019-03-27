/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Ruslan Ermilov <ru@FreeBSD.org>
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/time.h>
#include <vm/uma.h>
#include <geom/geom.h>
#include <geom/cache/g_cache.h>

FEATURE(geom_cache, "GEOM cache module");

static MALLOC_DEFINE(M_GCACHE, "gcache_data", "GEOM_CACHE Data");

SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, cache, CTLFLAG_RW, 0,
    "GEOM_CACHE stuff");
static u_int g_cache_debug = 0;
SYSCTL_UINT(_kern_geom_cache, OID_AUTO, debug, CTLFLAG_RW, &g_cache_debug, 0,
    "Debug level");
static u_int g_cache_enable = 1;
SYSCTL_UINT(_kern_geom_cache, OID_AUTO, enable, CTLFLAG_RW, &g_cache_enable, 0,
    "");
static u_int g_cache_timeout = 10;
SYSCTL_UINT(_kern_geom_cache, OID_AUTO, timeout, CTLFLAG_RW, &g_cache_timeout,
    0, "");
static u_int g_cache_idletime = 5;
SYSCTL_UINT(_kern_geom_cache, OID_AUTO, idletime, CTLFLAG_RW, &g_cache_idletime,
    0, "");
static u_int g_cache_used_lo = 5;
static u_int g_cache_used_hi = 20;
static int
sysctl_handle_pct(SYSCTL_HANDLER_ARGS)
{
	u_int val = *(u_int *)arg1;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return (error);
	if (val > 100)
		return (EINVAL);
	if ((arg1 == &g_cache_used_lo && val > g_cache_used_hi) ||
	    (arg1 == &g_cache_used_hi && g_cache_used_lo > val))
		return (EINVAL);
	*(u_int *)arg1 = val;
	return (0);
}
SYSCTL_PROC(_kern_geom_cache, OID_AUTO, used_lo, CTLTYPE_UINT|CTLFLAG_RW,
	&g_cache_used_lo, 0, sysctl_handle_pct, "IU", "");
SYSCTL_PROC(_kern_geom_cache, OID_AUTO, used_hi, CTLTYPE_UINT|CTLFLAG_RW,
	&g_cache_used_hi, 0, sysctl_handle_pct, "IU", "");


static int g_cache_destroy(struct g_cache_softc *sc, boolean_t force);
static g_ctl_destroy_geom_t g_cache_destroy_geom;

static g_taste_t g_cache_taste;
static g_ctl_req_t g_cache_config;
static g_dumpconf_t g_cache_dumpconf;

struct g_class g_cache_class = {
	.name = G_CACHE_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_cache_config,
	.taste = g_cache_taste,
	.destroy_geom = g_cache_destroy_geom
};

#define	OFF2BNO(off, sc)	((off) >> (sc)->sc_bshift)
#define	BNO2OFF(bno, sc)	((bno) << (sc)->sc_bshift)


static struct g_cache_desc *
g_cache_alloc(struct g_cache_softc *sc)
{
	struct g_cache_desc *dp;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	if (!TAILQ_EMPTY(&sc->sc_usedlist)) {
		dp = TAILQ_FIRST(&sc->sc_usedlist);
		TAILQ_REMOVE(&sc->sc_usedlist, dp, d_used);
		sc->sc_nused--;
		dp->d_flags = 0;
		LIST_REMOVE(dp, d_next);
		return (dp);
	}
	if (sc->sc_nent > sc->sc_maxent) {
		sc->sc_cachefull++;
		return (NULL);
	}
	dp = malloc(sizeof(*dp), M_GCACHE, M_NOWAIT | M_ZERO);
	if (dp == NULL)
		return (NULL);
	dp->d_data = uma_zalloc(sc->sc_zone, M_NOWAIT);
	if (dp->d_data == NULL) {
		free(dp, M_GCACHE);
		return (NULL);
	}
	sc->sc_nent++;
	return (dp);
}

static void
g_cache_free(struct g_cache_softc *sc, struct g_cache_desc *dp)
{

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	uma_zfree(sc->sc_zone, dp->d_data);
	free(dp, M_GCACHE);
	sc->sc_nent--;
}

static void
g_cache_free_used(struct g_cache_softc *sc)
{
	struct g_cache_desc *dp;
	u_int n;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	n = g_cache_used_lo * sc->sc_maxent / 100;
	while (sc->sc_nused > n) {
		KASSERT(!TAILQ_EMPTY(&sc->sc_usedlist), ("used list empty"));
		dp = TAILQ_FIRST(&sc->sc_usedlist);
		TAILQ_REMOVE(&sc->sc_usedlist, dp, d_used);
		sc->sc_nused--;
		LIST_REMOVE(dp, d_next);
		g_cache_free(sc, dp);
	}
}

static void
g_cache_deliver(struct g_cache_softc *sc, struct bio *bp,
    struct g_cache_desc *dp, int error)
{
	off_t off1, off, len;

	mtx_assert(&sc->sc_mtx, MA_OWNED);
	KASSERT(OFF2BNO(bp->bio_offset, sc) <= dp->d_bno, ("wrong entry"));
	KASSERT(OFF2BNO(bp->bio_offset + bp->bio_length - 1, sc) >=
	    dp->d_bno, ("wrong entry"));

	off1 = BNO2OFF(dp->d_bno, sc);
	off = MAX(bp->bio_offset, off1);
	len = MIN(bp->bio_offset + bp->bio_length, off1 + sc->sc_bsize) - off;

	if (bp->bio_error == 0)
		bp->bio_error = error;
	if (bp->bio_error == 0) {
		bcopy(dp->d_data + (off - off1),
		    bp->bio_data + (off - bp->bio_offset), len);
	}
	bp->bio_completed += len;
	KASSERT(bp->bio_completed <= bp->bio_length, ("extra data"));
	if (bp->bio_completed == bp->bio_length) {
		if (bp->bio_error != 0)
			bp->bio_completed = 0;
		g_io_deliver(bp, bp->bio_error);
	}

	if (dp->d_flags & D_FLAG_USED) {
		TAILQ_REMOVE(&sc->sc_usedlist, dp, d_used);
		TAILQ_INSERT_TAIL(&sc->sc_usedlist, dp, d_used);
	} else if (OFF2BNO(off + len, sc) > dp->d_bno) {
		TAILQ_INSERT_TAIL(&sc->sc_usedlist, dp, d_used);
		sc->sc_nused++;
		dp->d_flags |= D_FLAG_USED;
	}
	dp->d_atime = time_uptime;
}

static void
g_cache_done(struct bio *bp)
{
	struct g_cache_softc *sc;
	struct g_cache_desc *dp;
	struct bio *bp2, *tmpbp;

	sc = bp->bio_from->geom->softc;
	KASSERT(G_CACHE_DESC1(bp) == sc, ("corrupt bio_caller in g_cache_done()"));
	dp = G_CACHE_DESC2(bp);
	mtx_lock(&sc->sc_mtx);
	bp2 = dp->d_biolist;
	while (bp2 != NULL) {
		KASSERT(G_CACHE_NEXT_BIO1(bp2) == sc, ("corrupt bio_driver in g_cache_done()"));
		tmpbp = G_CACHE_NEXT_BIO2(bp2);
		g_cache_deliver(sc, bp2, dp, bp->bio_error);
		bp2 = tmpbp;
	}
	dp->d_biolist = NULL;
	if (dp->d_flags & D_FLAG_INVALID) {
		sc->sc_invalid--;
		g_cache_free(sc, dp);
	} else if (bp->bio_error) {
		LIST_REMOVE(dp, d_next);
		if (dp->d_flags & D_FLAG_USED) {
			TAILQ_REMOVE(&sc->sc_usedlist, dp, d_used);
			sc->sc_nused--;
		}
		g_cache_free(sc, dp);
	}
	mtx_unlock(&sc->sc_mtx);
	g_destroy_bio(bp);
}

static struct g_cache_desc *
g_cache_lookup(struct g_cache_softc *sc, off_t bno)
{
	struct g_cache_desc *dp;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	LIST_FOREACH(dp, &sc->sc_desclist[G_CACHE_BUCKET(bno)], d_next)
		if (dp->d_bno == bno)
			return (dp);
	return (NULL);
}

static int
g_cache_read(struct g_cache_softc *sc, struct bio *bp)
{
	struct bio *cbp;
	struct g_cache_desc *dp;

	mtx_lock(&sc->sc_mtx);
	dp = g_cache_lookup(sc,
	    OFF2BNO(bp->bio_offset + bp->bio_completed, sc));
	if (dp != NULL) {
		/* Add to waiters list or deliver. */
		sc->sc_cachehits++;
		if (dp->d_biolist != NULL) {
			G_CACHE_NEXT_BIO1(bp) = sc;
			G_CACHE_NEXT_BIO2(bp) = dp->d_biolist;
			dp->d_biolist = bp;
		} else
			g_cache_deliver(sc, bp, dp, 0);
		mtx_unlock(&sc->sc_mtx);
		return (0);
	}

	/* Cache miss.  Allocate entry and schedule bio.  */
	sc->sc_cachemisses++;
	dp = g_cache_alloc(sc);
	if (dp == NULL) {
		mtx_unlock(&sc->sc_mtx);
		return (ENOMEM);
	}
	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		g_cache_free(sc, dp);
		mtx_unlock(&sc->sc_mtx);
		return (ENOMEM);
	}

	dp->d_bno = OFF2BNO(bp->bio_offset + bp->bio_completed, sc);
	G_CACHE_NEXT_BIO1(bp) = sc;
	G_CACHE_NEXT_BIO2(bp) = NULL;
	dp->d_biolist = bp;
	LIST_INSERT_HEAD(&sc->sc_desclist[G_CACHE_BUCKET(dp->d_bno)],
	    dp, d_next);
	mtx_unlock(&sc->sc_mtx);

	G_CACHE_DESC1(cbp) = sc;
	G_CACHE_DESC2(cbp) = dp;
	cbp->bio_done = g_cache_done;
	cbp->bio_offset = BNO2OFF(dp->d_bno, sc);
	cbp->bio_data = dp->d_data;
	cbp->bio_length = sc->sc_bsize;
	g_io_request(cbp, LIST_FIRST(&bp->bio_to->geom->consumer));
	return (0);
}

static void
g_cache_invalidate(struct g_cache_softc *sc, struct bio *bp)
{
	struct g_cache_desc *dp;
	off_t bno, lim;

	mtx_lock(&sc->sc_mtx);
	bno = OFF2BNO(bp->bio_offset, sc);
	lim = OFF2BNO(bp->bio_offset + bp->bio_length - 1, sc);
	do {
		if ((dp = g_cache_lookup(sc, bno)) != NULL) {
			LIST_REMOVE(dp, d_next);
			if (dp->d_flags & D_FLAG_USED) {
				TAILQ_REMOVE(&sc->sc_usedlist, dp, d_used);
				sc->sc_nused--;
			}
			if (dp->d_biolist == NULL)
				g_cache_free(sc, dp);
			else {
				dp->d_flags = D_FLAG_INVALID;
				sc->sc_invalid++;
			}
		}
		bno++;
	} while (bno <= lim);
	mtx_unlock(&sc->sc_mtx);
}

static void
g_cache_start(struct bio *bp)
{
	struct g_cache_softc *sc;
	struct g_geom *gp;
	struct g_cache_desc *dp;
	struct bio *cbp;

	gp = bp->bio_to->geom;
	sc = gp->softc;
	G_CACHE_LOGREQ(bp, "Request received.");
	switch (bp->bio_cmd) {
	case BIO_READ:
		sc->sc_reads++;
		sc->sc_readbytes += bp->bio_length;
		if (!g_cache_enable)
			break;
		if (bp->bio_offset + bp->bio_length > sc->sc_tail)
			break;
		if (OFF2BNO(bp->bio_offset, sc) ==
		    OFF2BNO(bp->bio_offset + bp->bio_length - 1, sc)) {
			sc->sc_cachereads++;
			sc->sc_cachereadbytes += bp->bio_length;
			if (g_cache_read(sc, bp) == 0)
				return;
			sc->sc_cachereads--;
			sc->sc_cachereadbytes -= bp->bio_length;
			break;
		} else if (OFF2BNO(bp->bio_offset, sc) + 1 ==
		    OFF2BNO(bp->bio_offset + bp->bio_length - 1, sc)) {
			mtx_lock(&sc->sc_mtx);
			dp = g_cache_lookup(sc, OFF2BNO(bp->bio_offset, sc));
			if (dp == NULL || dp->d_biolist != NULL) {
				mtx_unlock(&sc->sc_mtx);
				break;
			}
			sc->sc_cachereads++;
			sc->sc_cachereadbytes += bp->bio_length;
			g_cache_deliver(sc, bp, dp, 0);
			mtx_unlock(&sc->sc_mtx);
			if (g_cache_read(sc, bp) == 0)
				return;
			sc->sc_cachereads--;
			sc->sc_cachereadbytes -= bp->bio_length;
			break;
		}
		break;
	case BIO_WRITE:
		sc->sc_writes++;
		sc->sc_wrotebytes += bp->bio_length;
		g_cache_invalidate(sc, bp);
		break;
	}
	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		g_io_deliver(bp, ENOMEM);
		return;
	}
	cbp->bio_done = g_std_done;
	G_CACHE_LOGREQ(cbp, "Sending request.");
	g_io_request(cbp, LIST_FIRST(&gp->consumer));
}

static void
g_cache_go(void *arg)
{
	struct g_cache_softc *sc = arg;
	struct g_cache_desc *dp;
	int i;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	/* Forcibly mark idle ready entries as used. */
	for (i = 0; i < G_CACHE_BUCKETS; i++) {
		LIST_FOREACH(dp, &sc->sc_desclist[i], d_next) {
			if (dp->d_flags & D_FLAG_USED ||
			    dp->d_biolist != NULL ||
			    time_uptime - dp->d_atime < g_cache_idletime)
				continue;
			TAILQ_INSERT_TAIL(&sc->sc_usedlist, dp, d_used);
			sc->sc_nused++;
			dp->d_flags |= D_FLAG_USED;
		}
	}

	/* Keep the number of used entries low. */
	if (sc->sc_nused > g_cache_used_hi * sc->sc_maxent / 100)
		g_cache_free_used(sc);

	callout_reset(&sc->sc_callout, g_cache_timeout * hz, g_cache_go, sc);
}

static int
g_cache_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	int error;

	gp = pp->geom;
	cp = LIST_FIRST(&gp->consumer);
	error = g_access(cp, dr, dw, de);

	return (error);
}

static void
g_cache_orphan(struct g_consumer *cp)
{

	g_topology_assert();
	g_cache_destroy(cp->geom->softc, 1);
}

static struct g_cache_softc *
g_cache_find_device(struct g_class *mp, const char *name)
{
	struct g_geom *gp;

	LIST_FOREACH(gp, &mp->geom, geom) {
		if (strcmp(gp->name, name) == 0)
			return (gp->softc);
	}
	return (NULL);
}

static struct g_geom *
g_cache_create(struct g_class *mp, struct g_provider *pp,
    const struct g_cache_metadata *md, u_int type)
{
	struct g_cache_softc *sc;
	struct g_geom *gp;
	struct g_provider *newpp;
	struct g_consumer *cp;
	u_int bshift;
	int i;

	g_topology_assert();

	gp = NULL;
	newpp = NULL;
	cp = NULL;

	G_CACHE_DEBUG(1, "Creating device %s.", md->md_name);

	/* Cache size is minimum 100. */
	if (md->md_size < 100) {
		G_CACHE_DEBUG(0, "Invalid size for device %s.", md->md_name);
		return (NULL);
	}

	/* Block size restrictions. */
	bshift = ffs(md->md_bsize) - 1;
	if (md->md_bsize == 0 || md->md_bsize > MAXPHYS ||
	    md->md_bsize != 1 << bshift ||
	    (md->md_bsize % pp->sectorsize) != 0) {
		G_CACHE_DEBUG(0, "Invalid blocksize for provider %s.", pp->name);
		return (NULL);
	}

	/* Check for duplicate unit. */
	if (g_cache_find_device(mp, (const char *)&md->md_name) != NULL) {
		G_CACHE_DEBUG(0, "Provider %s already exists.", md->md_name);
		return (NULL);
	}

	gp = g_new_geomf(mp, "%s", md->md_name);
	sc = g_malloc(sizeof(*sc), M_WAITOK | M_ZERO);
	sc->sc_type = type;
	sc->sc_bshift = bshift;
	sc->sc_bsize = 1 << bshift;
	sc->sc_zone = uma_zcreate("gcache", sc->sc_bsize, NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	mtx_init(&sc->sc_mtx, "GEOM CACHE mutex", NULL, MTX_DEF);
	for (i = 0; i < G_CACHE_BUCKETS; i++)
		LIST_INIT(&sc->sc_desclist[i]);
	TAILQ_INIT(&sc->sc_usedlist);
	sc->sc_maxent = md->md_size;
	callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);
	gp->softc = sc;
	sc->sc_geom = gp;
	gp->start = g_cache_start;
	gp->orphan = g_cache_orphan;
	gp->access = g_cache_access;
	gp->dumpconf = g_cache_dumpconf;

	newpp = g_new_providerf(gp, "cache/%s", gp->name);
	newpp->sectorsize = pp->sectorsize;
	newpp->mediasize = pp->mediasize;
	if (type == G_CACHE_TYPE_AUTOMATIC)
		newpp->mediasize -= pp->sectorsize;
	sc->sc_tail = BNO2OFF(OFF2BNO(newpp->mediasize, sc), sc);

	cp = g_new_consumer(gp);
	if (g_attach(cp, pp) != 0) {
		G_CACHE_DEBUG(0, "Cannot attach to provider %s.", pp->name);
		g_destroy_consumer(cp);
		g_destroy_provider(newpp);
		mtx_destroy(&sc->sc_mtx);
		g_free(sc);
		g_destroy_geom(gp);
		return (NULL);
	}

	g_error_provider(newpp, 0);
	G_CACHE_DEBUG(0, "Device %s created.", gp->name);
	callout_reset(&sc->sc_callout, g_cache_timeout * hz, g_cache_go, sc);
	return (gp);
}

static int
g_cache_destroy(struct g_cache_softc *sc, boolean_t force)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_cache_desc *dp, *dp2;
	int i;

	g_topology_assert();
	if (sc == NULL)
		return (ENXIO);
	gp = sc->sc_geom;
	pp = LIST_FIRST(&gp->provider);
	if (pp != NULL && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		if (force) {
			G_CACHE_DEBUG(0, "Device %s is still open, so it "
			    "can't be definitely removed.", pp->name);
		} else {
			G_CACHE_DEBUG(1, "Device %s is still open (r%dw%de%d).",
			    pp->name, pp->acr, pp->acw, pp->ace);
			return (EBUSY);
		}
	} else {
		G_CACHE_DEBUG(0, "Device %s removed.", gp->name);
	}
	callout_drain(&sc->sc_callout);
	mtx_lock(&sc->sc_mtx);
	for (i = 0; i < G_CACHE_BUCKETS; i++) {
		dp = LIST_FIRST(&sc->sc_desclist[i]);
		while (dp != NULL) {
			dp2 = LIST_NEXT(dp, d_next);
			g_cache_free(sc, dp);
			dp = dp2;
		}
	}
	mtx_unlock(&sc->sc_mtx);
	mtx_destroy(&sc->sc_mtx);
	uma_zdestroy(sc->sc_zone);
	g_free(sc);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);

	return (0);
}

static int
g_cache_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{

	return (g_cache_destroy(gp->softc, 0));
}

static int
g_cache_read_metadata(struct g_consumer *cp, struct g_cache_metadata *md)
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
	buf = g_read_data(cp, pp->mediasize - pp->sectorsize, pp->sectorsize,
	    &error);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (buf == NULL)
		return (error);

	/* Decode metadata. */
	cache_metadata_decode(buf, md);
	g_free(buf);

	return (0);
}

static int
g_cache_write_metadata(struct g_consumer *cp, struct g_cache_metadata *md)
{
	struct g_provider *pp;
	u_char *buf;
	int error;

	g_topology_assert();

	error = g_access(cp, 0, 1, 0);
	if (error != 0)
		return (error);
	pp = cp->provider;
	buf = malloc((size_t)pp->sectorsize, M_GCACHE, M_WAITOK | M_ZERO);
	cache_metadata_encode(md, buf);
	g_topology_unlock();
	error = g_write_data(cp, pp->mediasize - pp->sectorsize, buf, pp->sectorsize);
	g_topology_lock();
	g_access(cp, 0, -1, 0);
	free(buf, M_GCACHE);

	return (error);
}

static struct g_geom *
g_cache_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_cache_metadata md;
	struct g_consumer *cp;
	struct g_geom *gp;
	int error;

	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	g_topology_assert();

	G_CACHE_DEBUG(3, "Tasting %s.", pp->name);

	gp = g_new_geomf(mp, "cache:taste");
	gp->start = g_cache_start;
	gp->orphan = g_cache_orphan;
	gp->access = g_cache_access;
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_cache_read_metadata(cp, &md);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	if (error != 0)
		return (NULL);

	if (strcmp(md.md_magic, G_CACHE_MAGIC) != 0)
		return (NULL);
	if (md.md_version > G_CACHE_VERSION) {
		printf("geom_cache.ko module is too old to handle %s.\n",
		    pp->name);
		return (NULL);
	}
	if (md.md_provsize != pp->mediasize)
		return (NULL);

	gp = g_cache_create(mp, pp, &md, G_CACHE_TYPE_AUTOMATIC);
	if (gp == NULL) {
		G_CACHE_DEBUG(0, "Can't create %s.", md.md_name);
		return (NULL);
	}
	return (gp);
}

static void
g_cache_ctl_create(struct gctl_req *req, struct g_class *mp)
{
	struct g_cache_metadata md;
	struct g_provider *pp;
	struct g_geom *gp;
	intmax_t *bsize, *size;
	const char *name;
	int *nargs;

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument", "nargs");
		return;
	}
	if (*nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}

	strlcpy(md.md_magic, G_CACHE_MAGIC, sizeof(md.md_magic));
	md.md_version = G_CACHE_VERSION;
	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg0' argument");
		return;
	}
	strlcpy(md.md_name, name, sizeof(md.md_name));

	size = gctl_get_paraml(req, "size", sizeof(*size));
	if (size == NULL) {
		gctl_error(req, "No '%s' argument", "size");
		return;
	}
	if ((u_int)*size < 100) {
		gctl_error(req, "Invalid '%s' argument", "size");
		return;
	}
	md.md_size = (u_int)*size;

	bsize = gctl_get_paraml(req, "blocksize", sizeof(*bsize));
	if (bsize == NULL) {
		gctl_error(req, "No '%s' argument", "blocksize");
		return;
	}
	if (*bsize < 0) {
		gctl_error(req, "Invalid '%s' argument", "blocksize");
		return;
	}
	md.md_bsize = (u_int)*bsize;

	/* This field is not important here. */
	md.md_provsize = 0;

	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg1' argument");
		return;
	}
	if (strncmp(name, "/dev/", strlen("/dev/")) == 0)
		name += strlen("/dev/");
	pp = g_provider_by_name(name);
	if (pp == NULL) {
		G_CACHE_DEBUG(1, "Provider %s is invalid.", name);
		gctl_error(req, "Provider %s is invalid.", name);
		return;
	}
	gp = g_cache_create(mp, pp, &md, G_CACHE_TYPE_MANUAL);
	if (gp == NULL) {
		gctl_error(req, "Can't create %s.", md.md_name);
		return;
	}
}

static void
g_cache_ctl_configure(struct gctl_req *req, struct g_class *mp)
{
	struct g_cache_metadata md;
	struct g_cache_softc *sc;
	struct g_consumer *cp;
	intmax_t *bsize, *size;
	const char *name;
	int error, *nargs;

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument", "nargs");
		return;
	}
	if (*nargs != 1) {
		gctl_error(req, "Missing device.");
		return;
	}

	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg0' argument");
		return;
	}
	sc = g_cache_find_device(mp, name);
	if (sc == NULL) {
		G_CACHE_DEBUG(1, "Device %s is invalid.", name);
		gctl_error(req, "Device %s is invalid.", name);
		return;
	}

	size = gctl_get_paraml(req, "size", sizeof(*size));
	if (size == NULL) {
		gctl_error(req, "No '%s' argument", "size");
		return;
	}
	if ((u_int)*size != 0 && (u_int)*size < 100) {
		gctl_error(req, "Invalid '%s' argument", "size");
		return;
	}
	if ((u_int)*size != 0)
		sc->sc_maxent = (u_int)*size;

	bsize = gctl_get_paraml(req, "blocksize", sizeof(*bsize));
	if (bsize == NULL) {
		gctl_error(req, "No '%s' argument", "blocksize");
		return;
	}
	if (*bsize < 0) {
		gctl_error(req, "Invalid '%s' argument", "blocksize");
		return;
	}

	if (sc->sc_type != G_CACHE_TYPE_AUTOMATIC)
		return;

	strlcpy(md.md_name, name, sizeof(md.md_name));
	strlcpy(md.md_magic, G_CACHE_MAGIC, sizeof(md.md_magic));
	md.md_version = G_CACHE_VERSION;
	if ((u_int)*size != 0)
		md.md_size = (u_int)*size;
	else
		md.md_size = sc->sc_maxent;
	if ((u_int)*bsize != 0)
		md.md_bsize = (u_int)*bsize;
	else
		md.md_bsize = sc->sc_bsize;
	cp = LIST_FIRST(&sc->sc_geom->consumer);
	md.md_provsize = cp->provider->mediasize;
	error = g_cache_write_metadata(cp, &md);
	if (error == 0)
		G_CACHE_DEBUG(2, "Metadata on %s updated.", cp->provider->name);
	else
		G_CACHE_DEBUG(0, "Cannot update metadata on %s (error=%d).",
		    cp->provider->name, error);
}

static void
g_cache_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	int *nargs, *force, error, i;
	struct g_cache_softc *sc;
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
		sc = g_cache_find_device(mp, name);
		if (sc == NULL) {
			G_CACHE_DEBUG(1, "Device %s is invalid.", name);
			gctl_error(req, "Device %s is invalid.", name);
			return;
		}
		error = g_cache_destroy(sc, *force);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    sc->sc_name, error);
			return;
		}
	}
}

static void
g_cache_ctl_reset(struct gctl_req *req, struct g_class *mp)
{
	struct g_cache_softc *sc;
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
		sc = g_cache_find_device(mp, name);
		if (sc == NULL) {
			G_CACHE_DEBUG(1, "Device %s is invalid.", name);
			gctl_error(req, "Device %s is invalid.", name);
			return;
		}
		sc->sc_reads = 0;
		sc->sc_readbytes = 0;
		sc->sc_cachereads = 0;
		sc->sc_cachereadbytes = 0;
		sc->sc_cachehits = 0;
		sc->sc_cachemisses = 0;
		sc->sc_cachefull = 0;
		sc->sc_writes = 0;
		sc->sc_wrotebytes = 0;
	}
}

static void
g_cache_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_CACHE_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "create") == 0) {
		g_cache_ctl_create(req, mp);
		return;
	} else if (strcmp(verb, "configure") == 0) {
		g_cache_ctl_configure(req, mp);
		return;
	} else if (strcmp(verb, "destroy") == 0 ||
	    strcmp(verb, "stop") == 0) {
		g_cache_ctl_destroy(req, mp);
		return;
	} else if (strcmp(verb, "reset") == 0) {
		g_cache_ctl_reset(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

static void
g_cache_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_cache_softc *sc;

	if (pp != NULL || cp != NULL)
		return;
	sc = gp->softc;
	sbuf_printf(sb, "%s<Size>%u</Size>\n", indent, sc->sc_maxent);
	sbuf_printf(sb, "%s<BlockSize>%u</BlockSize>\n", indent, sc->sc_bsize);
	sbuf_printf(sb, "%s<TailOffset>%ju</TailOffset>\n", indent,
	    (uintmax_t)sc->sc_tail);
	sbuf_printf(sb, "%s<Entries>%u</Entries>\n", indent, sc->sc_nent);
	sbuf_printf(sb, "%s<UsedEntries>%u</UsedEntries>\n", indent,
	    sc->sc_nused);
	sbuf_printf(sb, "%s<InvalidEntries>%u</InvalidEntries>\n", indent,
	    sc->sc_invalid);
	sbuf_printf(sb, "%s<Reads>%ju</Reads>\n", indent, sc->sc_reads);
	sbuf_printf(sb, "%s<ReadBytes>%ju</ReadBytes>\n", indent,
	    sc->sc_readbytes);
	sbuf_printf(sb, "%s<CacheReads>%ju</CacheReads>\n", indent,
	    sc->sc_cachereads);
	sbuf_printf(sb, "%s<CacheReadBytes>%ju</CacheReadBytes>\n", indent,
	    sc->sc_cachereadbytes);
	sbuf_printf(sb, "%s<CacheHits>%ju</CacheHits>\n", indent,
	    sc->sc_cachehits);
	sbuf_printf(sb, "%s<CacheMisses>%ju</CacheMisses>\n", indent,
	    sc->sc_cachemisses);
	sbuf_printf(sb, "%s<CacheFull>%ju</CacheFull>\n", indent,
	    sc->sc_cachefull);
	sbuf_printf(sb, "%s<Writes>%ju</Writes>\n", indent, sc->sc_writes);
	sbuf_printf(sb, "%s<WroteBytes>%ju</WroteBytes>\n", indent,
	    sc->sc_wrotebytes);
}

DECLARE_GEOM_CLASS(g_cache_class, g_cache);
MODULE_VERSION(geom_cache, 0);
