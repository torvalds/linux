/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <geom/stripe/g_stripe.h>

FEATURE(geom_stripe, "GEOM striping support");

static MALLOC_DEFINE(M_STRIPE, "stripe_data", "GEOM_STRIPE Data");

static uma_zone_t g_stripe_zone;

static int g_stripe_destroy(struct g_stripe_softc *sc, boolean_t force);
static int g_stripe_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp);

static g_taste_t g_stripe_taste;
static g_ctl_req_t g_stripe_config;
static g_dumpconf_t g_stripe_dumpconf;
static g_init_t g_stripe_init;
static g_fini_t g_stripe_fini;

struct g_class g_stripe_class = {
	.name = G_STRIPE_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_stripe_config,
	.taste = g_stripe_taste,
	.destroy_geom = g_stripe_destroy_geom,
	.init = g_stripe_init,
	.fini = g_stripe_fini
};

SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, stripe, CTLFLAG_RW, 0,
    "GEOM_STRIPE stuff");
static u_int g_stripe_debug = 0;
SYSCTL_UINT(_kern_geom_stripe, OID_AUTO, debug, CTLFLAG_RWTUN, &g_stripe_debug, 0,
    "Debug level");
static int g_stripe_fast = 0;
static int
g_sysctl_stripe_fast(SYSCTL_HANDLER_ARGS)
{
	int error, fast;

	fast = g_stripe_fast;
	error = sysctl_handle_int(oidp, &fast, 0, req);
	if (error == 0 && req->newptr != NULL)
		g_stripe_fast = fast;
	return (error);
}
SYSCTL_PROC(_kern_geom_stripe, OID_AUTO, fast, CTLTYPE_INT | CTLFLAG_RWTUN,
    NULL, 0, g_sysctl_stripe_fast, "I", "Fast, but memory-consuming, mode");
static u_int g_stripe_maxmem = MAXPHYS * 100;
SYSCTL_UINT(_kern_geom_stripe, OID_AUTO, maxmem, CTLFLAG_RDTUN, &g_stripe_maxmem,
    0, "Maximum memory that can be allocated in \"fast\" mode (in bytes)");
static u_int g_stripe_fast_failed = 0;
SYSCTL_UINT(_kern_geom_stripe, OID_AUTO, fast_failed, CTLFLAG_RD,
    &g_stripe_fast_failed, 0, "How many times \"fast\" mode failed");

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
g_stripe_init(struct g_class *mp __unused)
{

	g_stripe_zone = uma_zcreate("g_stripe_zone", MAXPHYS, NULL, NULL,
	    NULL, NULL, 0, 0);
	g_stripe_maxmem -= g_stripe_maxmem % MAXPHYS;
	uma_zone_set_max(g_stripe_zone, g_stripe_maxmem / MAXPHYS);
}

static void
g_stripe_fini(struct g_class *mp __unused)
{

	uma_zdestroy(g_stripe_zone);
}

/*
 * Return the number of valid disks.
 */
static u_int
g_stripe_nvalid(struct g_stripe_softc *sc)
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
g_stripe_remove_disk(struct g_consumer *cp)
{
	struct g_stripe_softc *sc;

	g_topology_assert();
	KASSERT(cp != NULL, ("Non-valid disk in %s.", __func__));
	sc = (struct g_stripe_softc *)cp->geom->softc;
	KASSERT(sc != NULL, ("NULL sc in %s.", __func__));

	if (cp->private == NULL) {
		G_STRIPE_DEBUG(0, "Disk %s removed from %s.",
		    cp->provider->name, sc->sc_name);
		cp->private = (void *)(uintptr_t)-1;
	}

	if (sc->sc_provider != NULL) {
		G_STRIPE_DEBUG(0, "Device %s deactivated.",
		    sc->sc_provider->name);
		g_wither_provider(sc->sc_provider, ENXIO);
		sc->sc_provider = NULL;
	}

	if (cp->acr > 0 || cp->acw > 0 || cp->ace > 0)
		return;
	sc->sc_disks[cp->index] = NULL;
	cp->index = 0;
	g_detach(cp);
	g_destroy_consumer(cp);
	/* If there are no valid disks anymore, remove device. */
	if (LIST_EMPTY(&sc->sc_geom->consumer))
		g_stripe_destroy(sc, 1);
}

static void
g_stripe_orphan(struct g_consumer *cp)
{
	struct g_stripe_softc *sc;
	struct g_geom *gp;

	g_topology_assert();
	gp = cp->geom;
	sc = gp->softc;
	if (sc == NULL)
		return;

	g_stripe_remove_disk(cp);
}

static int
g_stripe_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_consumer *cp1, *cp2, *tmp;
	struct g_stripe_softc *sc;
	struct g_geom *gp;
	int error;

	g_topology_assert();
	gp = pp->geom;
	sc = gp->softc;
	KASSERT(sc != NULL, ("NULL sc in %s.", __func__));

	/* On first open, grab an extra "exclusive" bit */
	if (pp->acr == 0 && pp->acw == 0 && pp->ace == 0)
		de++;
	/* ... and let go of it on last close */
	if ((pp->acr + dr) == 0 && (pp->acw + dw) == 0 && (pp->ace + de) == 0)
		de--;

	LIST_FOREACH_SAFE(cp1, &gp->consumer, consumer, tmp) {
		error = g_access(cp1, dr, dw, de);
		if (error != 0)
			goto fail;
		if (cp1->acr == 0 && cp1->acw == 0 && cp1->ace == 0 &&
		    cp1->private != NULL) {
			g_stripe_remove_disk(cp1); /* May destroy geom. */
		}
	}
	return (0);

fail:
	LIST_FOREACH(cp2, &gp->consumer, consumer) {
		if (cp1 == cp2)
			break;
		g_access(cp2, -dr, -dw, -de);
	}
	return (error);
}

static void
g_stripe_copy(struct g_stripe_softc *sc, char *src, char *dst, off_t offset,
    off_t length, int mode)
{
	off_t stripesize;
	size_t len;

	stripesize = sc->sc_stripesize;
	len = (size_t)(stripesize - (offset & (stripesize - 1)));
	do {
		bcopy(src, dst, len);
		if (mode) {
			dst += len + stripesize * (sc->sc_ndisks - 1);
			src += len;
		} else {
			dst += len;
			src += len + stripesize * (sc->sc_ndisks - 1);
		}
		length -= len;
		KASSERT(length >= 0,
		    ("Length < 0 (stripesize=%ju, offset=%ju, length=%jd).",
		    (uintmax_t)stripesize, (uintmax_t)offset, (intmax_t)length));
		if (length > stripesize)
			len = stripesize;
		else
			len = length;
	} while (length > 0);
}

static void
g_stripe_done(struct bio *bp)
{
	struct g_stripe_softc *sc;
	struct bio *pbp;

	pbp = bp->bio_parent;
	sc = pbp->bio_to->geom->softc;
	if (bp->bio_cmd == BIO_READ && bp->bio_caller1 != NULL) {
		g_stripe_copy(sc, bp->bio_data, bp->bio_caller1, bp->bio_offset,
		    bp->bio_length, 1);
		bp->bio_data = bp->bio_caller1;
		bp->bio_caller1 = NULL;
	}
	mtx_lock(&sc->sc_lock);
	if (pbp->bio_error == 0)
		pbp->bio_error = bp->bio_error;
	pbp->bio_completed += bp->bio_completed;
	pbp->bio_inbed++;
	if (pbp->bio_children == pbp->bio_inbed) {
		mtx_unlock(&sc->sc_lock);
		if (pbp->bio_driver1 != NULL)
			uma_zfree(g_stripe_zone, pbp->bio_driver1);
		g_io_deliver(pbp, pbp->bio_error);
	} else
		mtx_unlock(&sc->sc_lock);
	g_destroy_bio(bp);
}

static int
g_stripe_start_fast(struct bio *bp, u_int no, off_t offset, off_t length)
{
	TAILQ_HEAD(, bio) queue = TAILQ_HEAD_INITIALIZER(queue);
	struct g_stripe_softc *sc;
	char *addr, *data = NULL;
	struct bio *cbp;
	off_t stripesize;
	u_int nparts = 0;
	int error;

	sc = bp->bio_to->geom->softc;

	addr = bp->bio_data;
	stripesize = sc->sc_stripesize;

	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		error = ENOMEM;
		goto failure;
	}
	TAILQ_INSERT_TAIL(&queue, cbp, bio_queue);
	nparts++;
	/*
	 * Fill in the component buf structure.
	 */
	cbp->bio_done = g_stripe_done;
	cbp->bio_offset = offset;
	cbp->bio_data = addr;
	cbp->bio_caller1 = NULL;
	cbp->bio_length = length;
	cbp->bio_caller2 = sc->sc_disks[no];

	/* offset -= offset % stripesize; */
	offset -= offset & (stripesize - 1);
	addr += length;
	length = bp->bio_length - length;
	for (no++; length > 0; no++, length -= stripesize, addr += stripesize) {
		if (no > sc->sc_ndisks - 1) {
			no = 0;
			offset += stripesize;
		}
		if (nparts >= sc->sc_ndisks) {
			cbp = TAILQ_NEXT(cbp, bio_queue);
			if (cbp == NULL)
				cbp = TAILQ_FIRST(&queue);
			nparts++;
			/*
			 * Update bio structure.
			 */
			/*
			 * MIN() is in case when
			 * (bp->bio_length % sc->sc_stripesize) != 0.
			 */
			cbp->bio_length += MIN(stripesize, length);
			if (cbp->bio_caller1 == NULL) {
				cbp->bio_caller1 = cbp->bio_data;
				cbp->bio_data = NULL;
				if (data == NULL) {
					data = uma_zalloc(g_stripe_zone,
					    M_NOWAIT);
					if (data == NULL) {
						error = ENOMEM;
						goto failure;
					}
				}
			}
		} else {
			cbp = g_clone_bio(bp);
			if (cbp == NULL) {
				error = ENOMEM;
				goto failure;
			}
			TAILQ_INSERT_TAIL(&queue, cbp, bio_queue);
			nparts++;
			/*
			 * Fill in the component buf structure.
			 */
			cbp->bio_done = g_stripe_done;
			cbp->bio_offset = offset;
			cbp->bio_data = addr;
			cbp->bio_caller1 = NULL;
			/*
			 * MIN() is in case when
			 * (bp->bio_length % sc->sc_stripesize) != 0.
			 */
			cbp->bio_length = MIN(stripesize, length);
			cbp->bio_caller2 = sc->sc_disks[no];
		}
	}
	if (data != NULL)
		bp->bio_driver1 = data;
	/*
	 * Fire off all allocated requests!
	 */
	while ((cbp = TAILQ_FIRST(&queue)) != NULL) {
		struct g_consumer *cp;

		TAILQ_REMOVE(&queue, cbp, bio_queue);
		cp = cbp->bio_caller2;
		cbp->bio_caller2 = NULL;
		cbp->bio_to = cp->provider;
		if (cbp->bio_caller1 != NULL) {
			cbp->bio_data = data;
			if (bp->bio_cmd == BIO_WRITE) {
				g_stripe_copy(sc, cbp->bio_caller1, data,
				    cbp->bio_offset, cbp->bio_length, 0);
			}
			data += cbp->bio_length;
		}
		G_STRIPE_LOGREQ(cbp, "Sending request.");
		g_io_request(cbp, cp);
	}
	return (0);
failure:
	if (data != NULL)
		uma_zfree(g_stripe_zone, data);
	while ((cbp = TAILQ_FIRST(&queue)) != NULL) {
		TAILQ_REMOVE(&queue, cbp, bio_queue);
		if (cbp->bio_caller1 != NULL) {
			cbp->bio_data = cbp->bio_caller1;
			cbp->bio_caller1 = NULL;
		}
		bp->bio_children--;
		g_destroy_bio(cbp);
	}
	return (error);
}

static int
g_stripe_start_economic(struct bio *bp, u_int no, off_t offset, off_t length)
{
	TAILQ_HEAD(, bio) queue = TAILQ_HEAD_INITIALIZER(queue);
	struct g_stripe_softc *sc;
	off_t stripesize;
	struct bio *cbp;
	char *addr;
	int error;

	sc = bp->bio_to->geom->softc;

	stripesize = sc->sc_stripesize;

	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		error = ENOMEM;
		goto failure;
	}
	TAILQ_INSERT_TAIL(&queue, cbp, bio_queue);
	/*
	 * Fill in the component buf structure.
	 */
	if (bp->bio_length == length)
		cbp->bio_done = g_std_done;	/* Optimized lockless case. */
	else
		cbp->bio_done = g_stripe_done;
	cbp->bio_offset = offset;
	cbp->bio_length = length;
	if ((bp->bio_flags & BIO_UNMAPPED) != 0) {
		bp->bio_ma_n = round_page(bp->bio_ma_offset +
		    bp->bio_length) / PAGE_SIZE;
		addr = NULL;
	} else
		addr = bp->bio_data;
	cbp->bio_caller2 = sc->sc_disks[no];

	/* offset -= offset % stripesize; */
	offset -= offset & (stripesize - 1);
	if (bp->bio_cmd != BIO_DELETE)
		addr += length;
	length = bp->bio_length - length;
	for (no++; length > 0; no++, length -= stripesize) {
		if (no > sc->sc_ndisks - 1) {
			no = 0;
			offset += stripesize;
		}
		cbp = g_clone_bio(bp);
		if (cbp == NULL) {
			error = ENOMEM;
			goto failure;
		}
		TAILQ_INSERT_TAIL(&queue, cbp, bio_queue);

		/*
		 * Fill in the component buf structure.
		 */
		cbp->bio_done = g_stripe_done;
		cbp->bio_offset = offset;
		/*
		 * MIN() is in case when
		 * (bp->bio_length % sc->sc_stripesize) != 0.
		 */
		cbp->bio_length = MIN(stripesize, length);
		if ((bp->bio_flags & BIO_UNMAPPED) != 0) {
			cbp->bio_ma_offset += (uintptr_t)addr;
			cbp->bio_ma += cbp->bio_ma_offset / PAGE_SIZE;
			cbp->bio_ma_offset %= PAGE_SIZE;
			cbp->bio_ma_n = round_page(cbp->bio_ma_offset +
			    cbp->bio_length) / PAGE_SIZE;
		} else
			cbp->bio_data = addr;

		cbp->bio_caller2 = sc->sc_disks[no];

		if (bp->bio_cmd != BIO_DELETE)
			addr += stripesize;
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
		G_STRIPE_LOGREQ(cbp, "Sending request.");
		g_io_request(cbp, cp);
	}
	return (0);
failure:
	while ((cbp = TAILQ_FIRST(&queue)) != NULL) {
		TAILQ_REMOVE(&queue, cbp, bio_queue);
		bp->bio_children--;
		g_destroy_bio(cbp);
	}
	return (error);
}

static void
g_stripe_flush(struct g_stripe_softc *sc, struct bio *bp)
{
	struct bio_queue_head queue;
	struct g_consumer *cp;
	struct bio *cbp;
	u_int no;

	bioq_init(&queue);
	for (no = 0; no < sc->sc_ndisks; no++) {
		cbp = g_clone_bio(bp);
		if (cbp == NULL) {
			for (cbp = bioq_first(&queue); cbp != NULL;
			    cbp = bioq_first(&queue)) {
				bioq_remove(&queue, cbp);
				g_destroy_bio(cbp);
			}
			if (bp->bio_error == 0)
				bp->bio_error = ENOMEM;
			g_io_deliver(bp, bp->bio_error);
			return;
		}
		bioq_insert_tail(&queue, cbp);
		cbp->bio_done = g_stripe_done;
		cbp->bio_caller2 = sc->sc_disks[no];
		cbp->bio_to = sc->sc_disks[no]->provider;
	}
	for (cbp = bioq_first(&queue); cbp != NULL; cbp = bioq_first(&queue)) {
		bioq_remove(&queue, cbp);
		G_STRIPE_LOGREQ(cbp, "Sending request.");
		cp = cbp->bio_caller2;
		cbp->bio_caller2 = NULL;
		g_io_request(cbp, cp);
	}
}

static void
g_stripe_start(struct bio *bp)
{
	off_t offset, start, length, nstripe, stripesize;
	struct g_stripe_softc *sc;
	u_int no;
	int error, fast = 0;

	sc = bp->bio_to->geom->softc;
	/*
	 * If sc == NULL, provider's error should be set and g_stripe_start()
	 * should not be called at all.
	 */
	KASSERT(sc != NULL,
	    ("Provider's error should be set (error=%d)(device=%s).",
	    bp->bio_to->error, bp->bio_to->name));

	G_STRIPE_LOGREQ(bp, "Request received.");

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
		break;
	case BIO_FLUSH:
		g_stripe_flush(sc, bp);
		return;
	case BIO_GETATTR:
		/* To which provider it should be delivered? */
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}

	stripesize = sc->sc_stripesize;

	/*
	 * Calculations are quite messy, but fast I hope.
	 */

	/* Stripe number. */
	/* nstripe = bp->bio_offset / stripesize; */
	nstripe = bp->bio_offset >> (off_t)sc->sc_stripebits;
	/* Disk number. */
	no = nstripe % sc->sc_ndisks;
	/* Start position in stripe. */
	/* start = bp->bio_offset % stripesize; */
	start = bp->bio_offset & (stripesize - 1);
	/* Start position in disk. */
	/* offset = (nstripe / sc->sc_ndisks) * stripesize + start; */
	offset = ((nstripe / sc->sc_ndisks) << sc->sc_stripebits) + start;
	/* Length of data to operate. */
	length = MIN(bp->bio_length, stripesize - start);

	/*
	 * Do use "fast" mode when:
	 * 1. "Fast" mode is ON.
	 * and
	 * 2. Request size is less than or equal to MAXPHYS,
	 *    which should always be true.
	 * and
	 * 3. Request size is bigger than stripesize * ndisks. If it isn't,
	 *    there will be no need to send more than one I/O request to
	 *    a provider, so there is nothing to optmize.
	 * and
	 * 4. Request is not unmapped.
	 * and
	 * 5. It is not a BIO_DELETE.
	 */
	if (g_stripe_fast && bp->bio_length <= MAXPHYS &&
	    bp->bio_length >= stripesize * sc->sc_ndisks &&
	    (bp->bio_flags & BIO_UNMAPPED) == 0 &&
	    bp->bio_cmd != BIO_DELETE) {
		fast = 1;
	}
	error = 0;
	if (fast) {
		error = g_stripe_start_fast(bp, no, offset, length);
		if (error != 0)
			g_stripe_fast_failed++;
	}
	/*
	 * Do use "economic" when:
	 * 1. "Economic" mode is ON.
	 * or
	 * 2. "Fast" mode failed. It can only fail if there is no memory.
	 */
	if (!fast || error != 0)
		error = g_stripe_start_economic(bp, no, offset, length);
	if (error != 0) {
		if (bp->bio_error == 0)
			bp->bio_error = error;
		g_io_deliver(bp, bp->bio_error);
	}
}

static void
g_stripe_check_and_run(struct g_stripe_softc *sc)
{
	struct g_provider *dp;
	off_t mediasize, ms;
	u_int no, sectorsize = 0;

	g_topology_assert();
	if (g_stripe_nvalid(sc) != sc->sc_ndisks)
		return;

	sc->sc_provider = g_new_providerf(sc->sc_geom, "stripe/%s",
	    sc->sc_name);
	sc->sc_provider->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE;
	if (g_stripe_fast == 0)
		sc->sc_provider->flags |= G_PF_ACCEPT_UNMAPPED;
	/*
	 * Find the smallest disk.
	 */
	mediasize = sc->sc_disks[0]->provider->mediasize;
	if (sc->sc_type == G_STRIPE_TYPE_AUTOMATIC)
		mediasize -= sc->sc_disks[0]->provider->sectorsize;
	mediasize -= mediasize % sc->sc_stripesize;
	sectorsize = sc->sc_disks[0]->provider->sectorsize;
	for (no = 1; no < sc->sc_ndisks; no++) {
		dp = sc->sc_disks[no]->provider;
		ms = dp->mediasize;
		if (sc->sc_type == G_STRIPE_TYPE_AUTOMATIC)
			ms -= dp->sectorsize;
		ms -= ms % sc->sc_stripesize;
		if (ms < mediasize)
			mediasize = ms;
		sectorsize = lcm(sectorsize, dp->sectorsize);

		/* A provider underneath us doesn't support unmapped */
		if ((dp->flags & G_PF_ACCEPT_UNMAPPED) == 0) {
			G_STRIPE_DEBUG(1, "Cancelling unmapped "
			    "because of %s.", dp->name);
			sc->sc_provider->flags &= ~G_PF_ACCEPT_UNMAPPED;
		}
	}
	sc->sc_provider->sectorsize = sectorsize;
	sc->sc_provider->mediasize = mediasize * sc->sc_ndisks;
	sc->sc_provider->stripesize = sc->sc_stripesize;
	sc->sc_provider->stripeoffset = 0;
	g_error_provider(sc->sc_provider, 0);

	G_STRIPE_DEBUG(0, "Device %s activated.", sc->sc_provider->name);
}

static int
g_stripe_read_metadata(struct g_consumer *cp, struct g_stripe_metadata *md)
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
	stripe_metadata_decode(buf, md);
	g_free(buf);

	return (0);
}

/*
 * Add disk to given device.
 */
static int
g_stripe_add_disk(struct g_stripe_softc *sc, struct g_provider *pp, u_int no)
{
	struct g_consumer *cp, *fcp;
	struct g_geom *gp;
	int error;

	g_topology_assert();
	/* Metadata corrupted? */
	if (no >= sc->sc_ndisks)
		return (EINVAL);

	/* Check if disk is not already attached. */
	if (sc->sc_disks[no] != NULL)
		return (EEXIST);

	gp = sc->sc_geom;
	fcp = LIST_FIRST(&gp->consumer);

	cp = g_new_consumer(gp);
	cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
	cp->private = NULL;
	cp->index = no;
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
	if (sc->sc_type == G_STRIPE_TYPE_AUTOMATIC) {
		struct g_stripe_metadata md;

		/* Reread metadata. */
		error = g_stripe_read_metadata(cp, &md);
		if (error != 0)
			goto fail;

		if (strcmp(md.md_magic, G_STRIPE_MAGIC) != 0 ||
		    strcmp(md.md_name, sc->sc_name) != 0 ||
		    md.md_id != sc->sc_id) {
			G_STRIPE_DEBUG(0, "Metadata on %s changed.", pp->name);
			goto fail;
		}
	}

	sc->sc_disks[no] = cp;
	G_STRIPE_DEBUG(0, "Disk %s attached to %s.", pp->name, sc->sc_name);
	g_stripe_check_and_run(sc);

	return (0);
fail:
	if (fcp != NULL && (fcp->acr > 0 || fcp->acw > 0 || fcp->ace > 0))
		g_access(cp, -fcp->acr, -fcp->acw, -fcp->ace);
	g_detach(cp);
	g_destroy_consumer(cp);
	return (error);
}

static struct g_geom *
g_stripe_create(struct g_class *mp, const struct g_stripe_metadata *md,
    u_int type)
{
	struct g_stripe_softc *sc;
	struct g_geom *gp;
	u_int no;

	g_topology_assert();
	G_STRIPE_DEBUG(1, "Creating device %s (id=%u).", md->md_name,
	    md->md_id);

	/* Two disks is minimum. */
	if (md->md_all < 2) {
		G_STRIPE_DEBUG(0, "Too few disks defined for %s.", md->md_name);
		return (NULL);
	}
#if 0
	/* Stripe size have to be grater than or equal to sector size. */
	if (md->md_stripesize < sectorsize) {
		G_STRIPE_DEBUG(0, "Invalid stripe size for %s.", md->md_name);
		return (NULL);
	}
#endif
	/* Stripe size have to be power of 2. */
	if (!powerof2(md->md_stripesize)) {
		G_STRIPE_DEBUG(0, "Invalid stripe size for %s.", md->md_name);
		return (NULL);
	}

	/* Check for duplicate unit */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc != NULL && strcmp(sc->sc_name, md->md_name) == 0) {
			G_STRIPE_DEBUG(0, "Device %s already configured.",
			    sc->sc_name);
			return (NULL);
		}
	}
	gp = g_new_geomf(mp, "%s", md->md_name);
	sc = malloc(sizeof(*sc), M_STRIPE, M_WAITOK | M_ZERO);
	gp->start = g_stripe_start;
	gp->spoiled = g_stripe_orphan;
	gp->orphan = g_stripe_orphan;
	gp->access = g_stripe_access;
	gp->dumpconf = g_stripe_dumpconf;

	sc->sc_id = md->md_id;
	sc->sc_stripesize = md->md_stripesize;
	sc->sc_stripebits = bitcount32(sc->sc_stripesize - 1);
	sc->sc_ndisks = md->md_all;
	sc->sc_disks = malloc(sizeof(struct g_consumer *) * sc->sc_ndisks,
	    M_STRIPE, M_WAITOK | M_ZERO);
	for (no = 0; no < sc->sc_ndisks; no++)
		sc->sc_disks[no] = NULL;
	sc->sc_type = type;
	mtx_init(&sc->sc_lock, "gstripe lock", NULL, MTX_DEF);

	gp->softc = sc;
	sc->sc_geom = gp;
	sc->sc_provider = NULL;

	G_STRIPE_DEBUG(0, "Device %s created (id=%u).", sc->sc_name, sc->sc_id);

	return (gp);
}

static int
g_stripe_destroy(struct g_stripe_softc *sc, boolean_t force)
{
	struct g_provider *pp;
	struct g_consumer *cp, *cp1;
	struct g_geom *gp;

	g_topology_assert();

	if (sc == NULL)
		return (ENXIO);

	pp = sc->sc_provider;
	if (pp != NULL && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		if (force) {
			G_STRIPE_DEBUG(0, "Device %s is still open, so it "
			    "can't be definitely removed.", pp->name);
		} else {
			G_STRIPE_DEBUG(1,
			    "Device %s is still open (r%dw%de%d).", pp->name,
			    pp->acr, pp->acw, pp->ace);
			return (EBUSY);
		}
	}

	gp = sc->sc_geom;
	LIST_FOREACH_SAFE(cp, &gp->consumer, consumer, cp1) {
		g_stripe_remove_disk(cp);
		if (cp1 == NULL)
			return (0);	/* Recursion happened. */
	}
	if (!LIST_EMPTY(&gp->consumer))
		return (EINPROGRESS);

	gp->softc = NULL;
	KASSERT(sc->sc_provider == NULL, ("Provider still exists? (device=%s)",
	    gp->name));
	free(sc->sc_disks, M_STRIPE);
	mtx_destroy(&sc->sc_lock);
	free(sc, M_STRIPE);
	G_STRIPE_DEBUG(0, "Device %s destroyed.", gp->name);
	g_wither_geom(gp, ENXIO);
	return (0);
}

static int
g_stripe_destroy_geom(struct gctl_req *req __unused,
    struct g_class *mp __unused, struct g_geom *gp)
{
	struct g_stripe_softc *sc;

	sc = gp->softc;
	return (g_stripe_destroy(sc, 0));
}

static struct g_geom *
g_stripe_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_stripe_metadata md;
	struct g_stripe_softc *sc;
	struct g_consumer *cp;
	struct g_geom *gp;
	int error;

	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	g_topology_assert();

	/* Skip providers that are already open for writing. */
	if (pp->acw > 0)
		return (NULL);

	G_STRIPE_DEBUG(3, "Tasting %s.", pp->name);

	gp = g_new_geomf(mp, "stripe:taste");
	gp->start = g_stripe_start;
	gp->access = g_stripe_access;
	gp->orphan = g_stripe_orphan;
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_stripe_read_metadata(cp, &md);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	if (error != 0)
		return (NULL);
	gp = NULL;

	if (strcmp(md.md_magic, G_STRIPE_MAGIC) != 0)
		return (NULL);
	if (md.md_version > G_STRIPE_VERSION) {
		printf("geom_stripe.ko module is too old to handle %s.\n",
		    pp->name);
		return (NULL);
	}
	/*
	 * Backward compatibility:
	 */
	/* There was no md_provider field in earlier versions of metadata. */
	if (md.md_version < 2)
		bzero(md.md_provider, sizeof(md.md_provider));
	/* There was no md_provsize field in earlier versions of metadata. */
	if (md.md_version < 3)
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
		if (sc->sc_type != G_STRIPE_TYPE_AUTOMATIC)
			continue;
		if (strcmp(md.md_name, sc->sc_name) != 0)
			continue;
		if (md.md_id != sc->sc_id)
			continue;
		break;
	}
	if (gp != NULL) {
		G_STRIPE_DEBUG(1, "Adding disk %s to %s.", pp->name, gp->name);
		error = g_stripe_add_disk(sc, pp, md.md_no);
		if (error != 0) {
			G_STRIPE_DEBUG(0,
			    "Cannot add disk %s to %s (error=%d).", pp->name,
			    gp->name, error);
			return (NULL);
		}
	} else {
		gp = g_stripe_create(mp, &md, G_STRIPE_TYPE_AUTOMATIC);
		if (gp == NULL) {
			G_STRIPE_DEBUG(0, "Cannot create device %s.",
			    md.md_name);
			return (NULL);
		}
		sc = gp->softc;
		G_STRIPE_DEBUG(1, "Adding disk %s to %s.", pp->name, gp->name);
		error = g_stripe_add_disk(sc, pp, md.md_no);
		if (error != 0) {
			G_STRIPE_DEBUG(0,
			    "Cannot add disk %s to %s (error=%d).", pp->name,
			    gp->name, error);
			g_stripe_destroy(sc, 1);
			return (NULL);
		}
	}

	return (gp);
}

static void
g_stripe_ctl_create(struct gctl_req *req, struct g_class *mp)
{
	u_int attached, no;
	struct g_stripe_metadata md;
	struct g_provider *pp;
	struct g_stripe_softc *sc;
	struct g_geom *gp;
	struct sbuf *sb;
	off_t *stripesize;
	const char *name;
	char param[16];
	int *nargs;

	g_topology_assert();
	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs <= 2) {
		gctl_error(req, "Too few arguments.");
		return;
	}

	strlcpy(md.md_magic, G_STRIPE_MAGIC, sizeof(md.md_magic));
	md.md_version = G_STRIPE_VERSION;
	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 0);
		return;
	}
	strlcpy(md.md_name, name, sizeof(md.md_name));
	md.md_id = arc4random();
	md.md_no = 0;
	md.md_all = *nargs - 1;
	stripesize = gctl_get_paraml(req, "stripesize", sizeof(*stripesize));
	if (stripesize == NULL) {
		gctl_error(req, "No '%s' argument.", "stripesize");
		return;
	}
	md.md_stripesize = (uint32_t)*stripesize;
	bzero(md.md_provider, sizeof(md.md_provider));
	/* This field is not important here. */
	md.md_provsize = 0;

	/* Check all providers are valid */
	for (no = 1; no < *nargs; no++) {
		snprintf(param, sizeof(param), "arg%u", no);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%u' argument.", no);
			return;
		}
		if (strncmp(name, "/dev/", strlen("/dev/")) == 0)
			name += strlen("/dev/");
		pp = g_provider_by_name(name);
		if (pp == NULL) {
			G_STRIPE_DEBUG(1, "Disk %s is invalid.", name);
			gctl_error(req, "Disk %s is invalid.", name);
			return;
		}
	}

	gp = g_stripe_create(mp, &md, G_STRIPE_TYPE_MANUAL);
	if (gp == NULL) {
		gctl_error(req, "Can't configure %s.", md.md_name);
		return;
	}

	sc = gp->softc;
	sb = sbuf_new_auto();
	sbuf_printf(sb, "Can't attach disk(s) to %s:", gp->name);
	for (attached = 0, no = 1; no < *nargs; no++) {
		snprintf(param, sizeof(param), "arg%u", no);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%u' argument.", no);
			continue;
		}
		if (strncmp(name, "/dev/", strlen("/dev/")) == 0)
			name += strlen("/dev/");
		pp = g_provider_by_name(name);
		KASSERT(pp != NULL, ("Provider %s disappear?!", name));
		if (g_stripe_add_disk(sc, pp, no - 1) != 0) {
			G_STRIPE_DEBUG(1, "Disk %u (%s) not attached to %s.",
			    no, pp->name, gp->name);
			sbuf_printf(sb, " %s", pp->name);
			continue;
		}
		attached++;
	}
	sbuf_finish(sb);
	if (md.md_all != attached) {
		g_stripe_destroy(gp->softc, 1);
		gctl_error(req, "%s", sbuf_data(sb));
	}
	sbuf_delete(sb);
}

static struct g_stripe_softc *
g_stripe_find_device(struct g_class *mp, const char *name)
{
	struct g_stripe_softc *sc;
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
g_stripe_ctl_destroy(struct gctl_req *req, struct g_class *mp)
{
	struct g_stripe_softc *sc;
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
		sc = g_stripe_find_device(mp, name);
		if (sc == NULL) {
			gctl_error(req, "No such device: %s.", name);
			return;
		}
		error = g_stripe_destroy(sc, *force);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    sc->sc_name, error);
			return;
		}
	}
}

static void
g_stripe_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_STRIPE_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "create") == 0) {
		g_stripe_ctl_create(req, mp);
		return;
	} else if (strcmp(verb, "destroy") == 0 ||
	    strcmp(verb, "stop") == 0) {
		g_stripe_ctl_destroy(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

static void
g_stripe_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_stripe_softc *sc;

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
		sbuf_printf(sb, "%s<Stripesize>%ju</Stripesize>\n", indent,
		    (uintmax_t)sc->sc_stripesize);
		sbuf_printf(sb, "%s<Type>", indent);
		switch (sc->sc_type) {
		case G_STRIPE_TYPE_AUTOMATIC:
			sbuf_printf(sb, "AUTOMATIC");
			break;
		case G_STRIPE_TYPE_MANUAL:
			sbuf_printf(sb, "MANUAL");
			break;
		default:
			sbuf_printf(sb, "UNKNOWN");
			break;
		}
		sbuf_printf(sb, "</Type>\n");
		sbuf_printf(sb, "%s<Status>Total=%u, Online=%u</Status>\n",
		    indent, sc->sc_ndisks, g_stripe_nvalid(sc));
		sbuf_printf(sb, "%s<State>", indent);
		if (sc->sc_provider != NULL && sc->sc_provider->error == 0)
			sbuf_printf(sb, "UP");
		else
			sbuf_printf(sb, "DOWN");
		sbuf_printf(sb, "</State>\n");
	}
}

DECLARE_GEOM_CLASS(g_stripe_class, g_stripe);
MODULE_VERSION(geom_stripe, 0);
