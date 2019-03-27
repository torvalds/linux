/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Pawel Jakub Dawidek
 * under sponsorship from the FreeBSD Foundation.
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
#include <sys/bio.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/fcntl.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/signalvar.h>
#include <sys/time.h>
#include <machine/atomic.h>

#include <geom/geom.h>
#include <geom/gate/g_gate.h>

FEATURE(geom_gate, "GEOM Gate module");

static MALLOC_DEFINE(M_GATE, "gg_data", "GEOM Gate Data");

SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, gate, CTLFLAG_RW, 0,
    "GEOM_GATE configuration");
static int g_gate_debug = 0;
SYSCTL_INT(_kern_geom_gate, OID_AUTO, debug, CTLFLAG_RWTUN, &g_gate_debug, 0,
    "Debug level");
static u_int g_gate_maxunits = 256;
SYSCTL_UINT(_kern_geom_gate, OID_AUTO, maxunits, CTLFLAG_RDTUN,
    &g_gate_maxunits, 0, "Maximum number of ggate devices");

struct g_class g_gate_class = {
	.name = G_GATE_CLASS_NAME,
	.version = G_VERSION,
};

static struct cdev *status_dev;
static d_ioctl_t g_gate_ioctl;
static struct cdevsw g_gate_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	g_gate_ioctl,
	.d_name =	G_GATE_CTL_NAME
};


static struct g_gate_softc **g_gate_units;
static u_int g_gate_nunits;
static struct mtx g_gate_units_lock;

static int
g_gate_destroy(struct g_gate_softc *sc, boolean_t force)
{
	struct bio_queue_head queue;
	struct g_provider *pp;
	struct g_consumer *cp;
	struct g_geom *gp;
	struct bio *bp;

	g_topology_assert();
	mtx_assert(&g_gate_units_lock, MA_OWNED);
	pp = sc->sc_provider;
	if (!force && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		mtx_unlock(&g_gate_units_lock);
		return (EBUSY);
	}
	mtx_unlock(&g_gate_units_lock);
	mtx_lock(&sc->sc_queue_mtx);
	if ((sc->sc_flags & G_GATE_FLAG_DESTROY) == 0)
		sc->sc_flags |= G_GATE_FLAG_DESTROY;
	wakeup(sc);
	mtx_unlock(&sc->sc_queue_mtx);
	gp = pp->geom;
	g_wither_provider(pp, ENXIO);
	callout_drain(&sc->sc_callout);
	bioq_init(&queue);
	mtx_lock(&sc->sc_queue_mtx);
	while ((bp = bioq_takefirst(&sc->sc_inqueue)) != NULL) {
		sc->sc_queue_count--;
		bioq_insert_tail(&queue, bp);
	}
	while ((bp = bioq_takefirst(&sc->sc_outqueue)) != NULL) {
		sc->sc_queue_count--;
		bioq_insert_tail(&queue, bp);
	}
	mtx_unlock(&sc->sc_queue_mtx);
	g_topology_unlock();
	while ((bp = bioq_takefirst(&queue)) != NULL) {
		G_GATE_LOGREQ(1, bp, "Request canceled.");
		g_io_deliver(bp, ENXIO);
	}
	mtx_lock(&g_gate_units_lock);
	/* One reference is ours. */
	sc->sc_ref--;
	while (sc->sc_ref > 0)
		msleep(&sc->sc_ref, &g_gate_units_lock, 0, "gg:destroy", 0);
	g_gate_units[sc->sc_unit] = NULL;
	KASSERT(g_gate_nunits > 0, ("negative g_gate_nunits?"));
	g_gate_nunits--;
	mtx_unlock(&g_gate_units_lock);
	mtx_destroy(&sc->sc_queue_mtx);
	g_topology_lock();
	if ((cp = sc->sc_readcons) != NULL) {
		sc->sc_readcons = NULL;
		(void)g_access(cp, -1, 0, 0);
		g_detach(cp);
		g_destroy_consumer(cp);
	}
	G_GATE_DEBUG(1, "Device %s destroyed.", gp->name);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);
	sc->sc_provider = NULL;
	free(sc, M_GATE);
	return (0);
}

static int
g_gate_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_gate_softc *sc;

	if (dr <= 0 && dw <= 0 && de <= 0)
		return (0);
	sc = pp->geom->softc;
	if (sc == NULL || (sc->sc_flags & G_GATE_FLAG_DESTROY) != 0)
		return (ENXIO);
	/* XXX: Hack to allow read-only mounts. */
#if 0
	if ((sc->sc_flags & G_GATE_FLAG_READONLY) != 0 && dw > 0)
		return (EPERM);
#endif
	if ((sc->sc_flags & G_GATE_FLAG_WRITEONLY) != 0 && dr > 0)
		return (EPERM);
	return (0);
}

static void
g_gate_queue_io(struct bio *bp)
{
	struct g_gate_softc *sc;

	sc = bp->bio_to->geom->softc;
	if (sc == NULL || (sc->sc_flags & G_GATE_FLAG_DESTROY) != 0) {
		g_io_deliver(bp, ENXIO);
		return;
	}

	mtx_lock(&sc->sc_queue_mtx);

	if (sc->sc_queue_size > 0 && sc->sc_queue_count > sc->sc_queue_size) {
		mtx_unlock(&sc->sc_queue_mtx);
		G_GATE_LOGREQ(1, bp, "Queue full, request canceled.");
		g_io_deliver(bp, ENOMEM);
		return;
	}

	bp->bio_driver1 = (void *)sc->sc_seq;
	sc->sc_seq++;
	sc->sc_queue_count++;

	bioq_insert_tail(&sc->sc_inqueue, bp);
	wakeup(sc);

	mtx_unlock(&sc->sc_queue_mtx);
}

static void
g_gate_done(struct bio *cbp)
{
	struct bio *pbp;

	pbp = cbp->bio_parent;
	if (cbp->bio_error == 0) {
		pbp->bio_completed = cbp->bio_completed;
		g_destroy_bio(cbp);
		pbp->bio_inbed++;
		g_io_deliver(pbp, 0);
	} else {
		/* If direct read failed, pass it through userland daemon. */
		g_destroy_bio(cbp);
		pbp->bio_children--;
		g_gate_queue_io(pbp);
	}
}

static void
g_gate_start(struct bio *pbp)
{
	struct g_gate_softc *sc;

	sc = pbp->bio_to->geom->softc;
	if (sc == NULL || (sc->sc_flags & G_GATE_FLAG_DESTROY) != 0) {
		g_io_deliver(pbp, ENXIO);
		return;
	}
	G_GATE_LOGREQ(2, pbp, "Request received.");
	switch (pbp->bio_cmd) {
	case BIO_READ:
		if (sc->sc_readcons != NULL) {
			struct bio *cbp;

			cbp = g_clone_bio(pbp);
			if (cbp == NULL) {
				g_io_deliver(pbp, ENOMEM);
				return;
			}
			cbp->bio_done = g_gate_done;
			cbp->bio_offset = pbp->bio_offset + sc->sc_readoffset;
			cbp->bio_to = sc->sc_readcons->provider;
			g_io_request(cbp, sc->sc_readcons);
			return;
		}
		break;
	case BIO_DELETE:
	case BIO_WRITE:
	case BIO_FLUSH:
		/* XXX: Hack to allow read-only mounts. */
		if ((sc->sc_flags & G_GATE_FLAG_READONLY) != 0) {
			g_io_deliver(pbp, EPERM);
			return;
		}
		break;
	case BIO_GETATTR:
	default:
		G_GATE_LOGREQ(2, pbp, "Ignoring request.");
		g_io_deliver(pbp, EOPNOTSUPP);
		return;
	}

	g_gate_queue_io(pbp);
}

static struct g_gate_softc *
g_gate_hold(int unit, const char *name)
{
	struct g_gate_softc *sc = NULL;

	mtx_lock(&g_gate_units_lock);
	if (unit >= 0 && unit < g_gate_maxunits)
		sc = g_gate_units[unit];
	else if (unit == G_GATE_NAME_GIVEN) {
		KASSERT(name != NULL, ("name is NULL"));
		for (unit = 0; unit < g_gate_maxunits; unit++) {
			if (g_gate_units[unit] == NULL)
				continue;
			if (strcmp(name,
			    g_gate_units[unit]->sc_provider->name) != 0) {
				continue;
			}
			sc = g_gate_units[unit];
			break;
		}
	}
	if (sc != NULL)
		sc->sc_ref++;
	mtx_unlock(&g_gate_units_lock);
	return (sc);
}

static void
g_gate_release(struct g_gate_softc *sc)
{

	g_topology_assert_not();
	mtx_lock(&g_gate_units_lock);
	sc->sc_ref--;
	KASSERT(sc->sc_ref >= 0, ("Negative sc_ref for %s.", sc->sc_name));
	if (sc->sc_ref == 0 && (sc->sc_flags & G_GATE_FLAG_DESTROY) != 0)
		wakeup(&sc->sc_ref);
	mtx_unlock(&g_gate_units_lock);
}

static int
g_gate_getunit(int unit, int *errorp)
{

	mtx_assert(&g_gate_units_lock, MA_OWNED);
	if (unit >= 0) {
		if (unit >= g_gate_maxunits)
			*errorp = EINVAL;
		else if (g_gate_units[unit] == NULL)
			return (unit);
		else
			*errorp = EEXIST;
	} else {
		for (unit = 0; unit < g_gate_maxunits; unit++) {
			if (g_gate_units[unit] == NULL)
				return (unit);
		}
		*errorp = ENFILE;
	}
	return (-1);
}

static void
g_gate_guard(void *arg)
{
	struct bio_queue_head queue;
	struct g_gate_softc *sc;
	struct bintime curtime;
	struct bio *bp, *bp2;

	sc = arg;
	binuptime(&curtime);
	g_gate_hold(sc->sc_unit, NULL);
	bioq_init(&queue);
	mtx_lock(&sc->sc_queue_mtx);
	TAILQ_FOREACH_SAFE(bp, &sc->sc_inqueue.queue, bio_queue, bp2) {
		if (curtime.sec - bp->bio_t0.sec < 5)
			continue;
		bioq_remove(&sc->sc_inqueue, bp);
		sc->sc_queue_count--;
		bioq_insert_tail(&queue, bp);
	}
	TAILQ_FOREACH_SAFE(bp, &sc->sc_outqueue.queue, bio_queue, bp2) {
		if (curtime.sec - bp->bio_t0.sec < 5)
			continue;
		bioq_remove(&sc->sc_outqueue, bp);
		sc->sc_queue_count--;
		bioq_insert_tail(&queue, bp);
	}
	mtx_unlock(&sc->sc_queue_mtx);
	while ((bp = bioq_takefirst(&queue)) != NULL) {
		G_GATE_LOGREQ(1, bp, "Request timeout.");
		g_io_deliver(bp, EIO);
	}
	if ((sc->sc_flags & G_GATE_FLAG_DESTROY) == 0) {
		callout_reset(&sc->sc_callout, sc->sc_timeout * hz,
		    g_gate_guard, sc);
	}
	g_gate_release(sc);
}

static void
g_gate_orphan(struct g_consumer *cp)
{
	struct g_gate_softc *sc;
	struct g_geom *gp;

	g_topology_assert();
	gp = cp->geom;
	sc = gp->softc;
	if (sc == NULL)
		return;
	KASSERT(cp == sc->sc_readcons, ("cp=%p sc_readcons=%p", cp,
	    sc->sc_readcons));
	sc->sc_readcons = NULL;
	G_GATE_DEBUG(1, "Destroying read consumer on provider %s orphan.",
	    cp->provider->name);
	(void)g_access(cp, -1, 0, 0);
	g_detach(cp);
	g_destroy_consumer(cp);
}

static void
g_gate_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_gate_softc *sc;

	sc = gp->softc;
	if (sc == NULL || pp != NULL || cp != NULL)
		return;
	sc = g_gate_hold(sc->sc_unit, NULL);
	if (sc == NULL)
		return;
	if ((sc->sc_flags & G_GATE_FLAG_READONLY) != 0) {
		sbuf_printf(sb, "%s<access>%s</access>\n", indent, "read-only");
	} else if ((sc->sc_flags & G_GATE_FLAG_WRITEONLY) != 0) {
		sbuf_printf(sb, "%s<access>%s</access>\n", indent,
		    "write-only");
	} else {
		sbuf_printf(sb, "%s<access>%s</access>\n", indent,
		    "read-write");
	}
	if (sc->sc_readcons != NULL) {
		sbuf_printf(sb, "%s<read_offset>%jd</read_offset>\n",
		    indent, (intmax_t)sc->sc_readoffset);
		sbuf_printf(sb, "%s<read_provider>%s</read_provider>\n",
		    indent, sc->sc_readcons->provider->name);
	}
	sbuf_printf(sb, "%s<timeout>%u</timeout>\n", indent, sc->sc_timeout);
	sbuf_printf(sb, "%s<info>%s</info>\n", indent, sc->sc_info);
	sbuf_printf(sb, "%s<queue_count>%u</queue_count>\n", indent,
	    sc->sc_queue_count);
	sbuf_printf(sb, "%s<queue_size>%u</queue_size>\n", indent,
	    sc->sc_queue_size);
	sbuf_printf(sb, "%s<ref>%u</ref>\n", indent, sc->sc_ref);
	sbuf_printf(sb, "%s<unit>%d</unit>\n", indent, sc->sc_unit);
	g_topology_unlock();
	g_gate_release(sc);
	g_topology_lock();
}

static int
g_gate_create(struct g_gate_ctl_create *ggio)
{
	struct g_gate_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp, *ropp;
	struct g_consumer *cp;
	char name[NAME_MAX];
	int error = 0, unit;

	if (ggio->gctl_mediasize <= 0) {
		G_GATE_DEBUG(1, "Invalid media size.");
		return (EINVAL);
	}
	if (ggio->gctl_sectorsize <= 0) {
		G_GATE_DEBUG(1, "Invalid sector size.");
		return (EINVAL);
	}
	if (!powerof2(ggio->gctl_sectorsize)) {
		G_GATE_DEBUG(1, "Invalid sector size.");
		return (EINVAL);
	}
	if ((ggio->gctl_mediasize % ggio->gctl_sectorsize) != 0) {
		G_GATE_DEBUG(1, "Invalid media size.");
		return (EINVAL);
	}
	if ((ggio->gctl_flags & G_GATE_FLAG_READONLY) != 0 &&
	    (ggio->gctl_flags & G_GATE_FLAG_WRITEONLY) != 0) {
		G_GATE_DEBUG(1, "Invalid flags.");
		return (EINVAL);
	}
	if (ggio->gctl_unit != G_GATE_UNIT_AUTO &&
	    ggio->gctl_unit != G_GATE_NAME_GIVEN &&
	    ggio->gctl_unit < 0) {
		G_GATE_DEBUG(1, "Invalid unit number.");
		return (EINVAL);
	}
	if (ggio->gctl_unit == G_GATE_NAME_GIVEN &&
	    ggio->gctl_name[0] == '\0') {
		G_GATE_DEBUG(1, "No device name.");
		return (EINVAL);
	}

	sc = malloc(sizeof(*sc), M_GATE, M_WAITOK | M_ZERO);
	sc->sc_flags = (ggio->gctl_flags & G_GATE_USERFLAGS);
	strlcpy(sc->sc_info, ggio->gctl_info, sizeof(sc->sc_info));
	sc->sc_seq = 1;
	bioq_init(&sc->sc_inqueue);
	bioq_init(&sc->sc_outqueue);
	mtx_init(&sc->sc_queue_mtx, "gg:queue", NULL, MTX_DEF);
	sc->sc_queue_count = 0;
	sc->sc_queue_size = ggio->gctl_maxcount;
	if (sc->sc_queue_size > G_GATE_MAX_QUEUE_SIZE)
		sc->sc_queue_size = G_GATE_MAX_QUEUE_SIZE;
	sc->sc_timeout = ggio->gctl_timeout;
	callout_init(&sc->sc_callout, 1);

	mtx_lock(&g_gate_units_lock);
	sc->sc_unit = g_gate_getunit(ggio->gctl_unit, &error);
	if (sc->sc_unit < 0)
		goto fail1;
	if (ggio->gctl_unit == G_GATE_NAME_GIVEN)
		snprintf(name, sizeof(name), "%s", ggio->gctl_name);
	else {
		snprintf(name, sizeof(name), "%s%d", G_GATE_PROVIDER_NAME,
		    sc->sc_unit);
	}
	/* Check for name collision. */
	for (unit = 0; unit < g_gate_maxunits; unit++) {
		if (g_gate_units[unit] == NULL)
			continue;
		if (strcmp(name, g_gate_units[unit]->sc_name) != 0)
			continue;
		error = EEXIST;
		goto fail1;
	}
	sc->sc_name = name;
	g_gate_units[sc->sc_unit] = sc;
	g_gate_nunits++;
	mtx_unlock(&g_gate_units_lock);

	g_topology_lock();

	if (ggio->gctl_readprov[0] == '\0') {
		ropp = NULL;
	} else {
		ropp = g_provider_by_name(ggio->gctl_readprov);
		if (ropp == NULL) {
			G_GATE_DEBUG(1, "Provider %s doesn't exist.",
			    ggio->gctl_readprov);
			error = EINVAL;
			goto fail2;
		}
		if ((ggio->gctl_readoffset % ggio->gctl_sectorsize) != 0) {
			G_GATE_DEBUG(1, "Invalid read offset.");
			error = EINVAL;
			goto fail2;
		}
		if (ggio->gctl_mediasize + ggio->gctl_readoffset >
		    ropp->mediasize) {
			G_GATE_DEBUG(1, "Invalid read offset or media size.");
			error = EINVAL;
			goto fail2;
		}
	}

	gp = g_new_geomf(&g_gate_class, "%s", name);
	gp->start = g_gate_start;
	gp->access = g_gate_access;
	gp->orphan = g_gate_orphan;
	gp->dumpconf = g_gate_dumpconf;
	gp->softc = sc;

	if (ropp != NULL) {
		cp = g_new_consumer(gp);
		cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
		error = g_attach(cp, ropp);
		if (error != 0) {
			G_GATE_DEBUG(1, "Unable to attach to %s.", ropp->name);
			goto fail3;
		}
		error = g_access(cp, 1, 0, 0);
		if (error != 0) {
			G_GATE_DEBUG(1, "Unable to access %s.", ropp->name);
			g_detach(cp);
			goto fail3;
		}
		sc->sc_readcons = cp;
		sc->sc_readoffset = ggio->gctl_readoffset;
	}

	ggio->gctl_unit = sc->sc_unit;

	pp = g_new_providerf(gp, "%s", name);
	pp->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE;
	pp->mediasize = ggio->gctl_mediasize;
	pp->sectorsize = ggio->gctl_sectorsize;
	sc->sc_provider = pp;
	g_error_provider(pp, 0);

	g_topology_unlock();
	mtx_lock(&g_gate_units_lock);
	sc->sc_name = sc->sc_provider->name;
	mtx_unlock(&g_gate_units_lock);
	G_GATE_DEBUG(1, "Device %s created.", gp->name);

	if (sc->sc_timeout > 0) {
		callout_reset(&sc->sc_callout, sc->sc_timeout * hz,
		    g_gate_guard, sc);
	}
	return (0);
fail3:
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
fail2:
	g_topology_unlock();
	mtx_lock(&g_gate_units_lock);
	g_gate_units[sc->sc_unit] = NULL;
	KASSERT(g_gate_nunits > 0, ("negative g_gate_nunits?"));
	g_gate_nunits--;
fail1:
	mtx_unlock(&g_gate_units_lock);
	mtx_destroy(&sc->sc_queue_mtx);
	free(sc, M_GATE);
	return (error);
}

static int
g_gate_modify(struct g_gate_softc *sc, struct g_gate_ctl_modify *ggio)
{
	struct g_provider *pp;
	struct g_consumer *cp;
	int error;

	if ((ggio->gctl_modify & GG_MODIFY_MEDIASIZE) != 0) {
		if (ggio->gctl_mediasize <= 0) {
			G_GATE_DEBUG(1, "Invalid media size.");
			return (EINVAL);
		}
		pp = sc->sc_provider;
		if ((ggio->gctl_mediasize % pp->sectorsize) != 0) {
			G_GATE_DEBUG(1, "Invalid media size.");
			return (EINVAL);
		}
		g_resize_provider(pp, ggio->gctl_mediasize);
		return (0);
	}

	if ((ggio->gctl_modify & GG_MODIFY_INFO) != 0)
		(void)strlcpy(sc->sc_info, ggio->gctl_info, sizeof(sc->sc_info));

	cp = NULL;

	if ((ggio->gctl_modify & GG_MODIFY_READPROV) != 0) {
		g_topology_lock();
		if (sc->sc_readcons != NULL) {
			cp = sc->sc_readcons;
			sc->sc_readcons = NULL;
			(void)g_access(cp, -1, 0, 0);
			g_detach(cp);
			g_destroy_consumer(cp);
		}
		if (ggio->gctl_readprov[0] != '\0') {
			pp = g_provider_by_name(ggio->gctl_readprov);
			if (pp == NULL) {
				g_topology_unlock();
				G_GATE_DEBUG(1, "Provider %s doesn't exist.",
				    ggio->gctl_readprov);
				return (EINVAL);
			}
			cp = g_new_consumer(sc->sc_provider->geom);
			cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
			error = g_attach(cp, pp);
			if (error != 0) {
				G_GATE_DEBUG(1, "Unable to attach to %s.",
				    pp->name);
			} else {
				error = g_access(cp, 1, 0, 0);
				if (error != 0) {
					G_GATE_DEBUG(1, "Unable to access %s.",
					    pp->name);
					g_detach(cp);
				}
			}
			if (error != 0) {
				g_destroy_consumer(cp);
				g_topology_unlock();
				return (error);
			}
		}
	} else {
		cp = sc->sc_readcons;
	}

	if ((ggio->gctl_modify & GG_MODIFY_READOFFSET) != 0) {
		if (cp == NULL) {
			G_GATE_DEBUG(1, "No read provider.");
			return (EINVAL);
		}
		pp = sc->sc_provider;
		if ((ggio->gctl_readoffset % pp->sectorsize) != 0) {
			G_GATE_DEBUG(1, "Invalid read offset.");
			return (EINVAL);
		}
		if (pp->mediasize + ggio->gctl_readoffset >
		    cp->provider->mediasize) {
			G_GATE_DEBUG(1, "Invalid read offset or media size.");
			return (EINVAL);
		}
		sc->sc_readoffset = ggio->gctl_readoffset;
	}

	if ((ggio->gctl_modify & GG_MODIFY_READPROV) != 0) {
		sc->sc_readcons = cp;
		g_topology_unlock();
	}

	return (0);
}

#define	G_GATE_CHECK_VERSION(ggio)	do {				\
	if ((ggio)->gctl_version != G_GATE_VERSION) {			\
		printf("Version mismatch %d != %d.\n",			\
		    ggio->gctl_version, G_GATE_VERSION);		\
		return (EINVAL);					\
	}								\
} while (0)
static int
g_gate_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags, struct thread *td)
{
	struct g_gate_softc *sc;
	struct bio *bp;
	int error = 0;

	G_GATE_DEBUG(4, "ioctl(%s, %lx, %p, %x, %p)", devtoname(dev), cmd, addr,
	    flags, td);

	switch (cmd) {
	case G_GATE_CMD_CREATE:
	    {
		struct g_gate_ctl_create *ggio = (void *)addr;

		G_GATE_CHECK_VERSION(ggio);
		error = g_gate_create(ggio);
		/*
		 * Reset TDP_GEOM flag.
		 * There are pending events for sure, because we just created
		 * new provider and other classes want to taste it, but we
		 * cannot answer on I/O requests until we're here.
		 */
		td->td_pflags &= ~TDP_GEOM;
		return (error);
	    }
	case G_GATE_CMD_MODIFY:
	    {
		struct g_gate_ctl_modify *ggio = (void *)addr;

		G_GATE_CHECK_VERSION(ggio);
		sc = g_gate_hold(ggio->gctl_unit, NULL);
		if (sc == NULL)
			return (ENXIO);
		error = g_gate_modify(sc, ggio);
		g_gate_release(sc);
		return (error);
	    }
	case G_GATE_CMD_DESTROY:
	    {
		struct g_gate_ctl_destroy *ggio = (void *)addr;

		G_GATE_CHECK_VERSION(ggio);
		sc = g_gate_hold(ggio->gctl_unit, ggio->gctl_name);
		if (sc == NULL)
			return (ENXIO);
		g_topology_lock();
		mtx_lock(&g_gate_units_lock);
		error = g_gate_destroy(sc, ggio->gctl_force);
		g_topology_unlock();
		if (error != 0)
			g_gate_release(sc);
		return (error);
	    }
	case G_GATE_CMD_CANCEL:
	    {
		struct g_gate_ctl_cancel *ggio = (void *)addr;
		struct bio *tbp, *lbp;

		G_GATE_CHECK_VERSION(ggio);
		sc = g_gate_hold(ggio->gctl_unit, ggio->gctl_name);
		if (sc == NULL)
			return (ENXIO);
		lbp = NULL;
		mtx_lock(&sc->sc_queue_mtx);
		TAILQ_FOREACH_SAFE(bp, &sc->sc_outqueue.queue, bio_queue, tbp) {
			if (ggio->gctl_seq == 0 ||
			    ggio->gctl_seq == (uintptr_t)bp->bio_driver1) {
				G_GATE_LOGREQ(1, bp, "Request canceled.");
				bioq_remove(&sc->sc_outqueue, bp);
				/*
				 * Be sure to put requests back onto incoming
				 * queue in the proper order.
				 */
				if (lbp == NULL)
					bioq_insert_head(&sc->sc_inqueue, bp);
				else {
					TAILQ_INSERT_AFTER(&sc->sc_inqueue.queue,
					    lbp, bp, bio_queue);
				}
				lbp = bp;
				/*
				 * If only one request was canceled, leave now.
				 */
				if (ggio->gctl_seq != 0)
					break;
			}
		}
		if (ggio->gctl_unit == G_GATE_NAME_GIVEN)
			ggio->gctl_unit = sc->sc_unit;
		mtx_unlock(&sc->sc_queue_mtx);
		g_gate_release(sc);
		return (error);
	    }
	case G_GATE_CMD_START:
	    {
		struct g_gate_ctl_io *ggio = (void *)addr;

		G_GATE_CHECK_VERSION(ggio);
		sc = g_gate_hold(ggio->gctl_unit, NULL);
		if (sc == NULL)
			return (ENXIO);
		error = 0;
		for (;;) {
			mtx_lock(&sc->sc_queue_mtx);
			bp = bioq_first(&sc->sc_inqueue);
			if (bp != NULL)
				break;
			if ((sc->sc_flags & G_GATE_FLAG_DESTROY) != 0) {
				ggio->gctl_error = ECANCELED;
				mtx_unlock(&sc->sc_queue_mtx);
				goto start_end;
			}
			if (msleep(sc, &sc->sc_queue_mtx,
			    PPAUSE | PDROP | PCATCH, "ggwait", 0) != 0) {
				ggio->gctl_error = ECANCELED;
				goto start_end;
			}
		}
		ggio->gctl_cmd = bp->bio_cmd;
		if (bp->bio_cmd == BIO_WRITE &&
		    bp->bio_length > ggio->gctl_length) {
			mtx_unlock(&sc->sc_queue_mtx);
			ggio->gctl_length = bp->bio_length;
			ggio->gctl_error = ENOMEM;
			goto start_end;
		}
		bioq_remove(&sc->sc_inqueue, bp);
		bioq_insert_tail(&sc->sc_outqueue, bp);
		mtx_unlock(&sc->sc_queue_mtx);

		ggio->gctl_seq = (uintptr_t)bp->bio_driver1;
		ggio->gctl_offset = bp->bio_offset;
		ggio->gctl_length = bp->bio_length;

		switch (bp->bio_cmd) {
		case BIO_READ:
		case BIO_DELETE:
		case BIO_FLUSH:
			break;
		case BIO_WRITE:
			error = copyout(bp->bio_data, ggio->gctl_data,
			    bp->bio_length);
			if (error != 0) {
				mtx_lock(&sc->sc_queue_mtx);
				bioq_remove(&sc->sc_outqueue, bp);
				bioq_insert_head(&sc->sc_inqueue, bp);
				mtx_unlock(&sc->sc_queue_mtx);
				goto start_end;
			}
			break;
		}
start_end:
		g_gate_release(sc);
		return (error);
	    }
	case G_GATE_CMD_DONE:
	    {
		struct g_gate_ctl_io *ggio = (void *)addr;

		G_GATE_CHECK_VERSION(ggio);
		sc = g_gate_hold(ggio->gctl_unit, NULL);
		if (sc == NULL)
			return (ENOENT);
		error = 0;
		mtx_lock(&sc->sc_queue_mtx);
		TAILQ_FOREACH(bp, &sc->sc_outqueue.queue, bio_queue) {
			if (ggio->gctl_seq == (uintptr_t)bp->bio_driver1)
				break;
		}
		if (bp != NULL) {
			bioq_remove(&sc->sc_outqueue, bp);
			sc->sc_queue_count--;
		}
		mtx_unlock(&sc->sc_queue_mtx);
		if (bp == NULL) {
			/*
			 * Request was probably canceled.
			 */
			goto done_end;
		}
		if (ggio->gctl_error == EAGAIN) {
			bp->bio_error = 0;
			G_GATE_LOGREQ(1, bp, "Request desisted.");
			mtx_lock(&sc->sc_queue_mtx);
			sc->sc_queue_count++;
			bioq_insert_head(&sc->sc_inqueue, bp);
			wakeup(sc);
			mtx_unlock(&sc->sc_queue_mtx);
		} else {
			bp->bio_error = ggio->gctl_error;
			if (bp->bio_error == 0) {
				bp->bio_completed = bp->bio_length;
				switch (bp->bio_cmd) {
				case BIO_READ:
					error = copyin(ggio->gctl_data,
					    bp->bio_data, bp->bio_length);
					if (error != 0)
						bp->bio_error = error;
					break;
				case BIO_DELETE:
				case BIO_WRITE:
				case BIO_FLUSH:
					break;
				}
			}
			G_GATE_LOGREQ(2, bp, "Request done.");
			g_io_deliver(bp, bp->bio_error);
		}
done_end:
		g_gate_release(sc);
		return (error);
	    }
	}
	return (ENOIOCTL);
}

static void
g_gate_device(void)
{

	status_dev = make_dev(&g_gate_cdevsw, 0x0, UID_ROOT, GID_WHEEL, 0600,
	    G_GATE_CTL_NAME);
}

static int
g_gate_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		mtx_init(&g_gate_units_lock, "gg_units_lock", NULL, MTX_DEF);
		g_gate_units = malloc(g_gate_maxunits * sizeof(g_gate_units[0]),
		    M_GATE, M_WAITOK | M_ZERO);
		g_gate_nunits = 0;
		g_gate_device();
		break;
	case MOD_UNLOAD:
		mtx_lock(&g_gate_units_lock);
		if (g_gate_nunits > 0) {
			mtx_unlock(&g_gate_units_lock);
			error = EBUSY;
			break;
		}
		mtx_unlock(&g_gate_units_lock);
		mtx_destroy(&g_gate_units_lock);
		if (status_dev != NULL)
			destroy_dev(status_dev);
		free(g_gate_units, M_GATE);
		break;
	default:
		return (EOPNOTSUPP);
		break;
	}

	return (error);
}
static moduledata_t g_gate_module = {
	G_GATE_MOD_NAME,
	g_gate_modevent,
	NULL
};
DECLARE_MODULE(geom_gate, g_gate_module, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
DECLARE_GEOM_CLASS(g_gate_class, g_gate);
MODULE_VERSION(geom_gate, 0);
