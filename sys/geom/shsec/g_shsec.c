/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <vm/uma.h>
#include <geom/geom.h>
#include <geom/shsec/g_shsec.h>

FEATURE(geom_shsec, "GEOM shared secret device support");

static MALLOC_DEFINE(M_SHSEC, "shsec_data", "GEOM_SHSEC Data");

static uma_zone_t g_shsec_zone;

static int g_shsec_destroy(struct g_shsec_softc *sc, boolean_t force);
static int g_shsec_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp);

static g_taste_t g_shsec_taste;
static g_ctl_req_t g_shsec_config;
static g_dumpconf_t g_shsec_dumpconf;
static g_init_t g_shsec_init;
static g_fini_t g_shsec_fini;

struct g_class g_shsec_class = {
	.name = G_SHSEC_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_shsec_config,
	.taste = g_shsec_taste,
	.destroy_geom = g_shsec_destroy_geom,
	.init = g_shsec_init,
	.fini = g_shsec_fini
};

SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, shsec, CTLFLAG_RW, 0,
    "GEOM_SHSEC stuff");
static u_int g_shsec_debug = 0;
SYSCTL_UINT(_kern_geom_shsec, OID_AUTO, debug, CTLFLAG_RWTUN, &g_shsec_debug, 0,
    "Debug level");
static u_int g_shsec_maxmem = MAXPHYS * 100;
SYSCTL_UINT(_kern_geom_shsec, OID_AUTO, maxmem, CTLFLAG_RDTUN, &g_shsec_maxmem,
    0, "Maximum memory that can be allocated for I/O (in bytes)");
static u_int g_shsec_alloc_failed = 0;
SYSCTL_UINT(_kern_geom_shsec, OID_AUTO, alloc_failed, CTLFLAG_RD,
    &g_shsec_alloc_failed, 0, "How many times I/O allocation failed");

/*
 * Greatest Common Divisor.
 */
static u_int
gcd(u_int a, u_int b)
{
	u_int c;

	while (b != 0) {
		c = a;
		a = b;
		b = (c % b);
	}
	return (a);
}

/*
 * Least Common Multiple.
 */
static u_int
lcm(u_int a, u_int b)
{

	return ((a * b) / gcd(a, b));
}

static void
g_shsec_init(struct g_class *mp __unused)
{

	g_shsec_zone = uma_zcreate("g_shsec_zone", MAXPHYS, NULL, NULL, NULL,
	    NULL, 0, 0);
	g_shsec_maxmem -= g_shsec_maxmem % MAXPHYS;
	uma_zone_set_max(g_shsec_zone, g_shsec_maxmem / MAXPHYS);
}

static void
g_shsec_fini(struct g_class *mp __unused)
{

	uma_zdestroy(g_shsec_zone);
}

/*
 * Return the number of valid disks.
 */
static u_int
g_shsec_nvalid(struct g_shsec_softc *sc)
{
	u_int i, no;

	no = 0;
	for (i = 0; i < sc->sc_ndisks; i++) {
		if (sc->sc_disks[i] != NULL)
			no++;
	}

	return (no);
}

static void
g_shsec_remove_disk(struct g_consumer *cp)
{
	struct g_shsec_softc *sc;
	u_int no;

	KASSERT(cp != NULL, ("Non-valid disk in %s.", __func__));
	sc = (struct g_shsec_softc *)cp->private;
	KASSERT(sc != NULL, ("NULL sc in %s.", __func__));
	no = cp->index;

	G_SHSEC_DEBUG(0, "Disk %s removed from %s.", cp->provider->name,
	    sc->sc_name);

	sc->sc_disks[no] = NULL;
	if (sc->sc_provider != NULL) {
		g_wither_provider(sc->sc_provider, ENXIO);
		sc->sc_provider = NULL;
		G_SHSEC_DEBUG(0, "Device %s removed.", sc->sc_name);
	}

	if (cp->acr > 0 || cp->acw > 0 || cp->ace > 0)
		g_access(cp, -cp->acr, -cp->acw, -cp->ace);
	g_detach(cp);
	g_destroy_consumer(cp);
}

static void
g_shsec_orphan(struct g_consumer *cp)
{
	struct g_shsec_softc *sc;
	struct g_geom *gp;

	g_topology_assert();
	gp = cp->geom;
	sc = gp->softc;
	if (sc == NULL)
		return;

	g_shsec_remove_disk(cp);
	/* If there are no valid disks anymore, remove device. */
	if (g_shsec_nvalid(sc) == 0)
		g_shsec_destroy(sc, 1);
}

static int
g_shsec_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_consumer *cp1, *cp2;
	struct g_shsec_softc *sc;
	struct g_geom *gp;
	int error;

	gp = pp->geom;
	sc = gp->softc;

	if (sc == NULL) {
		/*
		 * It looks like geom is being withered.
		 * In that case we allow only negative requests.
		 */
		KASSERT(dr <= 0 && dw <= 0 && de <= 0,
		    ("Positive access request (device=%s).", pp->name));
		if ((pp->acr + dr) == 0 && (pp->acw + dw) == 0 &&
		    (pp->ace + de) == 0) {
			G_SHSEC_DEBUG(0, "Device %s definitely destroyed.",
			    gp->name);
		}
		return (0);
	}

	/* On first open, grab an extra "exclusive" bit */
	if (pp->acr == 0 && pp->acw == 0 && pp->ace == 0)
		de++;
	/* ... and let go of it on last close */
	if ((pp->acr + dr) == 0 && (pp->acw + dw) == 0 && (pp->ace + de) == 0)
		de--;

	error = ENXIO;
	LIST_FOREACH(cp1, &gp->consumer, consumer) {
		error = g_access(cp1, dr, dw, de);
		if (error == 0)
			continue;
		/*
		 * If we fail here, backout all previous changes.
		 */
		LIST_FOREACH(cp2, &gp->consumer, consumer) {
			if (cp1 == cp2)
				return (error);
			g_access(cp2, -dr, -dw, -de);
		}
		/* NOTREACHED */
	}

	return (error);
}

static void
g_shsec_xor1(uint32_t *src, uint32_t *dst, ssize_t len)
{

	for (; len > 0; len -= sizeof(uint32_t), dst++)
		*dst = *dst ^ *src++;
	KASSERT(len == 0, ("len != 0 (len=%zd)", len));
}

static void
g_shsec_done(struct bio *bp)
{
	struct g_shsec_softc *sc;
	struct bio *pbp;

	pbp = bp->bio_parent;
	sc = pbp->bio_to->geom->softc;
	if (bp->bio_error == 0)
		G_SHSEC_LOGREQ(2, bp, "Request done.");
	else {
		G_SHSEC_LOGREQ(0, bp, "Request failed (error=%d).",
		    bp->bio_error);
		if (pbp->bio_error == 0)
			pbp->bio_error = bp->bio_error;
	}
	if (pbp->bio_cmd == BIO_READ) {
		if ((pbp->bio_pflags & G_SHSEC_BFLAG_FIRST) != 0) {
			bcopy(bp->bio_data, pbp->bio_data, pbp->bio_length);
			pbp->bio_pflags = 0;
		} else {
			g_shsec_xor1((uint32_t *)bp->bio_data,
			    (uint32_t *)pbp->bio_data,
			    (ssize_t)pbp->bio_length);
		}
	}
	bzero(bp->bio_data, bp->bio_length);
	uma_zfree(g_shsec_zone, bp->bio_data);
	g_destroy_bio(bp);
	pbp->bio_inbed++;
	if (pbp->bio_children == pbp->bio_inbed) {
		pbp->bio_completed = pbp->bio_length;
		g_io_deliver(pbp, pbp->bio_error);
	}
}

static void
g_shsec_xor2(uint32_t *rand, uint32_t *dst, ssize_t len)
{

	for (; len > 0; len -= sizeof(uint32_t), dst++) {
		*rand = arc4random();
		*dst = *dst ^ *rand++;
	}
	KASSERT(len == 0, ("len != 0 (len=%zd)", len));
}

static void
g_shsec_start(struct bio *bp)
{
	TAILQ_HEAD(, bio) queue = TAILQ_HEAD_INITIALIZER(queue);
	struct g_shsec_softc *sc;
	struct bio *cbp;
	uint32_t *dst;
	ssize_t len;
	u_int no;
	int error;

	sc = bp->bio_to->geom->softc;
	/*
	 * If sc == NULL, provider's error should be set and g_shsec_start()
	 * should not be called at all.
	 */
	KASSERT(sc != NULL,
	    ("Provider's error should be set (error=%d)(device=%s).",
	    bp->bio_to->error, bp->bio_to->name));

	G_SHSEC_LOGREQ(2, bp, "Request received.");

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_FLUSH:
		/*
		 * Only those requests are supported.
		 */
		break;
	case BIO_DELETE:
	case BIO_GETATTR:
		/* To which provider it should be delivered? */
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}

	/*
	 * Allocate all bios first and calculate XOR.
	 */
	dst = NULL;
	len = bp->bio_length;
	if (bp->bio_cmd == BIO_READ)
		bp->bio_pflags = G_SHSEC_BFLAG_FIRST;
	for (no = 0; no < sc->sc_ndisks; no++) {
		cbp = g_clone_bio(bp);
		if (cbp == NULL) {
			error = ENOMEM;
			goto failure;
		}
		TAILQ_INSERT_TAIL(&queue, cbp, bio_queue);

		/*
		 * Fill in the component buf structure.
		 */
		cbp->bio_done = g_shsec_done;
		cbp->bio_data = uma_zalloc(g_shsec_zone, M_NOWAIT);
		if (cbp->bio_data == NULL) {
			g_shsec_alloc_failed++;
			error = ENOMEM;
			goto failure;
		}
		cbp->bio_caller2 = sc->sc_disks[no];
		if (bp->bio_cmd == BIO_WRITE) {
			if (no == 0) {
				dst = (uint32_t *)cbp->bio_data;
				bcopy(bp->bio_data, dst, len);
			} else {
				g_shsec_xor2((uint32_t *)cbp->bio_data, dst,
				    len);
			}
		}
	}
	/*
	 * Fire off all allocated requests!
	 */
	while ((cbp = TAILQ_FIRST(&queue)) != NULL) {
		struct g_consumer *cp;

		TAILQ_REMOVE(&queue, cbp, bio_queue);
		cp = cbp->bio_caller2;
		cbp->bio_caller2 = NULL;
		cbp->bio_to = cp->provider;
		G_SHSEC_LOGREQ(2, cbp, "Sending request.");
		g_io_request(cbp, cp);
	}
	return;
failure:
	while ((cbp = TAILQ_FIRST(&queue)) != NULL) {
		TAILQ_REMOVE(&queue, cbp, bio_queue);
		bp->bio_children--;
		if (cbp->bio_data != NULL) {
			bzero(cbp->bio_data, cbp->bio_length);
			uma_zfree(g_shsec_zone, cbp->bio_data);
		}
		g_destroy_bio(cbp);
	}
	if (bp->bio_error == 0)
		bp->bio_error = error;
	g_io_deliver(bp, bp->bio_error);
}

static void
g_shsec_check_and_run(struct g_shsec_softc *sc)
{
	off_t mediasize, ms;
	u_int no, sectorsize = 0;

	if (g_shsec_nvalid(sc) != sc->sc_ndisks)
		return;

	sc->sc_provider = g_new_providerf(sc->sc_geom, "shsec/%s", sc->sc_name);
	/*
	 * Find the smallest disk.
	 */
	mediasize = sc->sc_disks[0]->provider->mediasize;
	mediasize -= sc->sc_disks[0]->provider->sectorsize;
	sectorsize = sc->sc_disks[0]->provider->sectorsize;
	for (no = 1; no < sc->sc_ndisks; no++) {
		ms = sc->sc_disks[no]->provider->mediasize;
		ms -= sc->sc_disks[no]->provider->sectorsize;
		if (ms < mediasize)
			mediasize = ms;
		sectorsize = lcm(sectorsize,
		    sc->sc_disks[no]->provider->sectorsize);
	}
	sc->sc_provider->sectorsize = sectorsize;
	sc->sc_provider->mediasize = mediasize;
	g_error_provider(sc->sc_provider, 0);

	G_SHSEC_DEBUG(0, "Device %s activated.", sc->sc_name);
}

static int
g_shsec_read_metadata(struct g_consumer *cp, struct g_shsec_metadata *md)
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
	shsec_metadata_decode(buf, md);
	g_free(buf);

	return (0);
}

/*
 * Add disk to given device.
 */
static int
g_shsec_add_disk(struct g_shsec_softc *sc, struct g_provider *pp, u_int no)
{
	struct g_consumer *cp, *fcp;
	struct g_geom *gp;
	struct g_shsec_metadata md;
	int error;

	/* Metadata corrupted? */
	if (no >= sc->sc_ndisks)
		return (EINVAL);

	/* Check if disk is not already attached. */
	if (sc->sc_disks[no] != NULL)
		return (EEXIST);

	gp = sc->sc_geom;
	fcp = LIST_FIRST(&gp->consumer);

	cp = g_new_consumer(gp);
	error = g_attach(cp, pp);
	if (error != 0) {
		g_destroy_consumer(cp);
		return (error);
	}

	if (fcp != NULL && (fcp->acr > 0 || fcp->acw > 0 || fcp->ace > 0)) {
		error = g_access(cp, fcp->acr, fcp->acw, fcp->ace);
		if (error != 0) {
			g_detach(cp);
			g_destroy_consumer(cp);
			return (error);
		}
	}

	/* Reread metadata. */
	error = g_shsec_read_metadata(cp, &md);
	if (error != 0)
		goto fail;

	if (strcmp(md.md_magic, G_SHSEC_MAGIC) != 0 ||
	    strcmp(md.md_name, sc->sc_name) != 0 || md.md_id != sc->sc_id) {
		G_SHSEC_DEBUG(0, "Metadata on %s changed.", pp->name);
		goto fail;
	}

	cp->private = sc;
	cp->index = no;
	sc->sc_disks[no] = cp;

	G_SHSEC_DEBUG(0, "Disk %s attached to %s.", pp->name, sc->sc_name);

	g_shsec_check_and_run(sc);

	return (0);
fail:
	if (fcp != NULL && (fcp->acr > 0 || fcp->acw > 0 || fcp->ace > 0))
		g_access(cp, -fcp->acr, -fcp->acw, -fcp->ace);
	g_detach(cp);
	g_destroy_consumer(cp);
	return (error);
}

static struct g_geom *
g_shsec_create(struct g_class *mp, const struct g_shsec_metadata *md)
{
	struct g_shsec_softc *sc;
	struct g_geom *gp;
	u_int no;

	G_SHSEC_DEBUG(1, "Creating device %s (id=%u).", md->md_name, md->md_id);

	/* Two disks is minimum. */
	if (md->md_all < 2) {
		G_SHSEC_DEBUG(0, "Too few disks defined for %s.", md->md_name);
		return (NULL);
	}

	/* Check for duplicate unit */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc != NULL && strcmp(sc->sc_name, md->md_name) == 0) {
			G_SHSEC_DEBUG(0, "Device %s already configured.",
			    sc->sc_name);
			return (NULL);
		}
	}
	gp = g_new_geomf(mp, "%s", md->md_name);
	sc = malloc(sizeof(*sc), M_SHSEC, M_WAITOK | M_ZERO);
	gp->start = g_shsec_start;
	gp->spoiled = g_shsec_orphan;
	gp->orphan = g_shsec_orphan;
	gp->access = g_shsec_access;
	gp->dumpconf = g_shsec_dumpconf;

	sc->sc_id = md->md_id;
	sc->sc_ndisks = md->md_all;
	sc->sc_disks = malloc(sizeof(struct g_consumer *) * sc->sc_ndisks,
	    M_SHSEC, M_WAITOK | M_ZERO);
	for (no = 0; no < sc->sc_ndisks; no++)
		sc->sc_disks[no] = NULL;

	gp->softc = sc;
	sc->sc_geom = gp;
	sc->sc_provider = NULL;

	G_SHSEC_DEBUG(0, "Device %s created (id=%u).", sc->sc_name, sc->sc_id);

	return (gp);
}

static int
g_shsec_destroy(struct g_shsec_softc *sc, boolean_t force)
{
	struct g_provider *pp;
	struct g_geom *gp;
	u_int no;

	g_topology_assert();

	if (sc == NULL)
		return (ENXIO);

	pp = sc->sc_provider;
	if (pp != NULL && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		if (force) {
			G_SHSEC_DEBUG(0, "Device %s is still open, so it "
			    "can't be definitely removed.", pp->name);
		} else {
			G_SHSEC_DEBUG(1,
			    "Device %s is still open (r%dw%de%d).", pp->name,
			    pp->acr, pp->acw, pp->ace);
			return (EBUSY);
		}
	}

	for (no = 0; no < sc->sc_ndisks; no++) {
		if (sc->sc_disks[no] != NULL)
			g_shsec_remove_disk(sc->sc_disks[no]);
	}

	gp = sc->sc_geom;
	gp->softc = NULL;
	KASSERT(sc->sc_provider == NULL, ("Provider still exists? (device=%s)",
	    gp->name));
	free(sc->sc_disks, M_SHSEC);
	free(sc, M_SHSEC);

	pp = LIST_FIRST(&gp->provider);
	if (pp == NULL || (pp->acr == 0 && pp->acw == 0 && pp->ace == 0))
		G_SHSEC_DEBUG(0, "Device %s destroyed.", gp->name);

	g_wither_geom(gp, ENXIO);

	return (0);
}

static int
g_shsec_destroy_geom(struct gctl_req *req __unused, struct g_class *mp __unused,
    struct g_geom *gp)
{
	struct g_shsec_softc *sc;

	sc = gp->softc;
	return (g_shsec_destroy(sc, 0));
}

static struct g_geom *
g_shsec_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_shsec_metadata md;
	struct g_shsec_softc *sc;
	struct g_consumer *cp;
	struct g_geom *gp;
	int error;

	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	g_topology_assert();

	/* Skip providers that are already open for writing. */
	if (pp->acw > 0)
		return (NULL);

	G_SHSEC_DEBUG(3, "Tasting %s.", pp->name);

	gp = g_new_geomf(mp, "shsec:taste");
	gp->start = g_shsec_start;
	gp->access = g_shsec_access;
	gp->orphan = g_shsec_orphan;
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_shsec_read_metadata(cp, &md);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	if (error != 0)
		return (NULL);
	gp = NULL;

	if (strcmp(md.md_magic, G_SHSEC_MAGIC) != 0)
		return (NULL);
	if (md.md_version > G_SHSEC_VERSION) {
		G_SHSEC_DEBUG(0, "Kernel module is too old to handle %s.\n",
		    pp->name);
		return (NULL);
	}
	/*
	 * Backward compatibility:
	 */
	/* There was no md_provsize field in earlier versions of metadata. */
	if (md.md_version < 1)
		md.md_provsize = pp->mediasize;

	if (md.md_provider[0] != '\0' &&
	    !g_compare_names(md.md_provider, pp->name))
		return (NULL);
	if (md.md_provsize != pp->mediasize)
		return (NULL);

	/*
	 * Let's check if device already exists.
	 */
	sc = NULL;
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (strcmp(md.md_name, sc->sc_name) != 0)
			continue;
		if (md.md_id != sc->sc_id)
			continue;
		break;
	}
	if (gp != NULL) {
		G_SHSEC_DEBUG(1, "Adding disk %s to %s.", pp->name, gp->name);
		error = g_shsec_add_disk(sc, pp, md.md_no);
		if (error != 0) {
			G_SHSEC_DEBUG(0, "Cannot add disk %s to %s (error=%d).",
			    pp->name, gp->name, error);
			return (NULL);
		}
	} else {
		gp = g_shsec_create(mp, &md);
		if (gp == NULL) {
			G_SHSEC_DEBUG(0, "Cannot create device %s.", md.md_name);
			return (NULL);
		}
		sc = gp->softc;
		G_SHSEC_DEBUG(1, "Adding disk %s to %s.", pp->name, gp->name);
		error = g_shsec_add_disk(sc, pp, md.md_no);
		if (error != 0) {
			G_SHSEC_DEBUG(0, "Cannot add disk %s to %s (error=%d).",
			    pp->name, gp->name, error);
			g_shsec_destroy(sc, 1);
			return (NULL);
		}
	}
	return (gp);
}

static struct g_shsec_softc *
g_shsec_find_device(struct g_class *mp, const char *name)
{
	struct g_shsec_softc *sc;
	struct g_geom *gp;

	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (strcmp(sc->sc_name, name) == 0)
			return (sc);
	}
	return (NULL);
}

static void
g_shsec_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	struct g_shsec_softc *sc;
	int *force, *nargs, error;
	const char *name;
	char param[16];
	u_int i;

	g_topology_assert();

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs <= 0) {
		gctl_error(req, "Missing device(s).");
		return;
	}
	force = gctl_get_paraml(req, "force", sizeof(*force));
	if (force == NULL) {
		gctl_error(req, "No '%s' argument.", "force");
		return;
	}

	for (i = 0; i < (u_int)*nargs; i++) {
		snprintf(param, sizeof(param), "arg%u", i);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%u' argument.", i);
			return;
		}
		sc = g_shsec_find_device(mp, name);
		if (sc == NULL) {
			gctl_error(req, "No such device: %s.", name);
			return;
		}
		error = g_shsec_destroy(sc, *force);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    sc->sc_name, error);
			return;
		}
	}
}

static void
g_shsec_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_SHSEC_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "stop") == 0) {
		g_shsec_ctl_destroy(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

static void
g_shsec_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_shsec_softc *sc;

	sc = gp->softc;
	if (sc == NULL)
		return;
	if (pp != NULL) {
		/* Nothing here. */
	} else if (cp != NULL) {
		sbuf_printf(sb, "%s<Number>%u</Number>\n", indent,
		    (u_int)cp->index);
	} else {
		sbuf_printf(sb, "%s<ID>%u</ID>\n", indent, (u_int)sc->sc_id);
		sbuf_printf(sb, "%s<Status>Total=%u, Online=%u</Status>\n",
		    indent, sc->sc_ndisks, g_shsec_nvalid(sc));
		sbuf_printf(sb, "%s<State>", indent);
		if (sc->sc_provider != NULL && sc->sc_provider->error == 0)
			sbuf_printf(sb, "UP");
		else
			sbuf_printf(sb, "DOWN");
		sbuf_printf(sb, "</State>\n");
	}
}

DECLARE_GEOM_CLASS(g_shsec_class, g_shsec);
MODULE_VERSION(geom_shsec, 0);
