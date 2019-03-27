/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <geom/geom.h>
#include <geom/nop/g_nop.h>


SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, nop, CTLFLAG_RW, 0, "GEOM_NOP stuff");
static u_int g_nop_debug = 0;
SYSCTL_UINT(_kern_geom_nop, OID_AUTO, debug, CTLFLAG_RW, &g_nop_debug, 0,
    "Debug level");

static int g_nop_destroy(struct g_geom *gp, boolean_t force);
static int g_nop_destroy_geom(struct gctl_req *req, struct g_class *mp,
    struct g_geom *gp);
static void g_nop_config(struct gctl_req *req, struct g_class *mp,
    const char *verb);
static void g_nop_dumpconf(struct sbuf *sb, const char *indent,
    struct g_geom *gp, struct g_consumer *cp, struct g_provider *pp);

struct g_class g_nop_class = {
	.name = G_NOP_CLASS_NAME,
	.version = G_VERSION,
	.ctlreq = g_nop_config,
	.destroy_geom = g_nop_destroy_geom
};


static void
g_nop_orphan(struct g_consumer *cp)
{

	g_topology_assert();
	g_nop_destroy(cp->geom, 1);
}

static void
g_nop_resize(struct g_consumer *cp)
{
	struct g_nop_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp;
	off_t size;

	g_topology_assert();

	gp = cp->geom;
	sc = gp->softc;

	if (sc->sc_explicitsize != 0)
		return;
	if (cp->provider->mediasize < sc->sc_offset) {
		g_nop_destroy(gp, 1);
		return;
	}
	size = cp->provider->mediasize - sc->sc_offset;
	LIST_FOREACH(pp, &gp->provider, provider)
		g_resize_provider(pp, size);
}

static void
g_nop_start(struct bio *bp)
{
	struct g_nop_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp;
	struct bio *cbp;
	u_int failprob = 0;

	gp = bp->bio_to->geom;
	sc = gp->softc;
	G_NOP_LOGREQ(bp, "Request received.");
	mtx_lock(&sc->sc_lock);
	switch (bp->bio_cmd) {
	case BIO_READ:
		sc->sc_reads++;
		sc->sc_readbytes += bp->bio_length;
		failprob = sc->sc_rfailprob;
		break;
	case BIO_WRITE:
		sc->sc_writes++;
		sc->sc_wrotebytes += bp->bio_length;
		failprob = sc->sc_wfailprob;
		break;
	case BIO_DELETE:
		sc->sc_deletes++;
		break;
	case BIO_GETATTR:
		sc->sc_getattrs++;
		if (sc->sc_physpath && 
		    g_handleattr_str(bp, "GEOM::physpath", sc->sc_physpath)) {
			mtx_unlock(&sc->sc_lock);
			return;
		}
		break;
	case BIO_FLUSH:
		sc->sc_flushes++;
		break;
	case BIO_CMD0:
		sc->sc_cmd0s++;
		break;
	case BIO_CMD1:
		sc->sc_cmd1s++;
		break;
	case BIO_CMD2:
		sc->sc_cmd2s++;
		break;
	}
	mtx_unlock(&sc->sc_lock);
	if (failprob > 0) {
		u_int rval;

		rval = arc4random() % 100;
		if (rval < failprob) {
			G_NOP_LOGREQLVL(1, bp, "Returning error=%d.", sc->sc_error);
			g_io_deliver(bp, sc->sc_error);
			return;
		}
	}
	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		g_io_deliver(bp, ENOMEM);
		return;
	}
	cbp->bio_done = g_std_done;
	cbp->bio_offset = bp->bio_offset + sc->sc_offset;
	pp = LIST_FIRST(&gp->provider);
	KASSERT(pp != NULL, ("NULL pp"));
	cbp->bio_to = pp;
	G_NOP_LOGREQ(cbp, "Sending request.");
	g_io_request(cbp, LIST_FIRST(&gp->consumer));
}

static int
g_nop_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	int error;

	gp = pp->geom;
	cp = LIST_FIRST(&gp->consumer);
	error = g_access(cp, dr, dw, de);

	return (error);
}

static int
g_nop_create(struct gctl_req *req, struct g_class *mp, struct g_provider *pp,
    int ioerror, u_int rfailprob, u_int wfailprob, off_t offset, off_t size,
    u_int secsize, off_t stripesize, off_t stripeoffset, const char *physpath)
{
	struct g_nop_softc *sc;
	struct g_geom *gp;
	struct g_provider *newpp;
	struct g_consumer *cp;
	char name[64];
	int error;
	off_t explicitsize;

	g_topology_assert();

	gp = NULL;
	newpp = NULL;
	cp = NULL;

	if ((offset % pp->sectorsize) != 0) {
		gctl_error(req, "Invalid offset for provider %s.", pp->name);
		return (EINVAL);
	}
	if ((size % pp->sectorsize) != 0) {
		gctl_error(req, "Invalid size for provider %s.", pp->name);
		return (EINVAL);
	}
	if (offset >= pp->mediasize) {
		gctl_error(req, "Invalid offset for provider %s.", pp->name);
		return (EINVAL);
	}
	explicitsize = size;
	if (size == 0)
		size = pp->mediasize - offset;
	if (offset + size > pp->mediasize) {
		gctl_error(req, "Invalid size for provider %s.", pp->name);
		return (EINVAL);
	}
	if (secsize == 0)
		secsize = pp->sectorsize;
	else if ((secsize % pp->sectorsize) != 0) {
		gctl_error(req, "Invalid secsize for provider %s.", pp->name);
		return (EINVAL);
	}
	if (secsize > MAXPHYS) {
		gctl_error(req, "secsize is too big.");
		return (EINVAL);
	}
	size -= size % secsize;
	if ((stripesize % pp->sectorsize) != 0) {
		gctl_error(req, "Invalid stripesize for provider %s.", pp->name);
		return (EINVAL);
	}
	if ((stripeoffset % pp->sectorsize) != 0) {
		gctl_error(req, "Invalid stripeoffset for provider %s.", pp->name);
		return (EINVAL);
	}
	if (stripesize != 0 && stripeoffset >= stripesize) {
		gctl_error(req, "stripeoffset is too big.");
		return (EINVAL);
	}
	snprintf(name, sizeof(name), "%s%s", pp->name, G_NOP_SUFFIX);
	LIST_FOREACH(gp, &mp->geom, geom) {
		if (strcmp(gp->name, name) == 0) {
			gctl_error(req, "Provider %s already exists.", name);
			return (EEXIST);
		}
	}
	gp = g_new_geomf(mp, "%s", name);
	sc = g_malloc(sizeof(*sc), M_WAITOK | M_ZERO);
	sc->sc_offset = offset;
	sc->sc_explicitsize = explicitsize;
	sc->sc_stripesize = stripesize;
	sc->sc_stripeoffset = stripeoffset;
	if (physpath && strcmp(physpath, G_NOP_PHYSPATH_PASSTHROUGH)) {
		sc->sc_physpath = strndup(physpath, MAXPATHLEN, M_GEOM);
	} else
		sc->sc_physpath = NULL;
	sc->sc_error = ioerror;
	sc->sc_rfailprob = rfailprob;
	sc->sc_wfailprob = wfailprob;
	sc->sc_reads = 0;
	sc->sc_writes = 0;
	sc->sc_deletes = 0;
	sc->sc_getattrs = 0;
	sc->sc_flushes = 0;
	sc->sc_cmd0s = 0;
	sc->sc_cmd1s = 0;
	sc->sc_cmd2s = 0;
	sc->sc_readbytes = 0;
	sc->sc_wrotebytes = 0;
	mtx_init(&sc->sc_lock, "gnop lock", NULL, MTX_DEF);
	gp->softc = sc;
	gp->start = g_nop_start;
	gp->orphan = g_nop_orphan;
	gp->resize = g_nop_resize;
	gp->access = g_nop_access;
	gp->dumpconf = g_nop_dumpconf;

	newpp = g_new_providerf(gp, "%s", gp->name);
	newpp->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE;
	newpp->mediasize = size;
	newpp->sectorsize = secsize;
	newpp->stripesize = stripesize;
	newpp->stripeoffset = stripeoffset;

	cp = g_new_consumer(gp);
	cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
	error = g_attach(cp, pp);
	if (error != 0) {
		gctl_error(req, "Cannot attach to provider %s.", pp->name);
		goto fail;
	}

	newpp->flags |= pp->flags & G_PF_ACCEPT_UNMAPPED;
	g_error_provider(newpp, 0);
	G_NOP_DEBUG(0, "Device %s created.", gp->name);
	return (0);
fail:
	if (cp->provider != NULL)
		g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_provider(newpp);
	mtx_destroy(&sc->sc_lock);
	free(sc->sc_physpath, M_GEOM);
	g_free(gp->softc);
	g_destroy_geom(gp);
	return (error);
}

static int
g_nop_destroy(struct g_geom *gp, boolean_t force)
{
	struct g_nop_softc *sc;
	struct g_provider *pp;

	g_topology_assert();
	sc = gp->softc;
	if (sc == NULL)
		return (ENXIO);
	free(sc->sc_physpath, M_GEOM);
	pp = LIST_FIRST(&gp->provider);
	if (pp != NULL && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		if (force) {
			G_NOP_DEBUG(0, "Device %s is still open, so it "
			    "can't be definitely removed.", pp->name);
		} else {
			G_NOP_DEBUG(1, "Device %s is still open (r%dw%de%d).",
			    pp->name, pp->acr, pp->acw, pp->ace);
			return (EBUSY);
		}
	} else {
		G_NOP_DEBUG(0, "Device %s removed.", gp->name);
	}
	gp->softc = NULL;
	mtx_destroy(&sc->sc_lock);
	g_free(sc);
	g_wither_geom(gp, ENXIO);

	return (0);
}

static int
g_nop_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{

	return (g_nop_destroy(gp, 0));
}

static void
g_nop_ctl_create(struct gctl_req *req, struct g_class *mp)
{
	struct g_provider *pp;
	intmax_t *error, *rfailprob, *wfailprob, *offset, *secsize, *size,
	    *stripesize, *stripeoffset;
	const char *name, *physpath;
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
	error = gctl_get_paraml(req, "error", sizeof(*error));
	if (error == NULL) {
		gctl_error(req, "No '%s' argument", "error");
		return;
	}
	rfailprob = gctl_get_paraml(req, "rfailprob", sizeof(*rfailprob));
	if (rfailprob == NULL) {
		gctl_error(req, "No '%s' argument", "rfailprob");
		return;
	}
	if (*rfailprob < -1 || *rfailprob > 100) {
		gctl_error(req, "Invalid '%s' argument", "rfailprob");
		return;
	}
	wfailprob = gctl_get_paraml(req, "wfailprob", sizeof(*wfailprob));
	if (wfailprob == NULL) {
		gctl_error(req, "No '%s' argument", "wfailprob");
		return;
	}
	if (*wfailprob < -1 || *wfailprob > 100) {
		gctl_error(req, "Invalid '%s' argument", "wfailprob");
		return;
	}
	offset = gctl_get_paraml(req, "offset", sizeof(*offset));
	if (offset == NULL) {
		gctl_error(req, "No '%s' argument", "offset");
		return;
	}
	if (*offset < 0) {
		gctl_error(req, "Invalid '%s' argument", "offset");
		return;
	}
	size = gctl_get_paraml(req, "size", sizeof(*size));
	if (size == NULL) {
		gctl_error(req, "No '%s' argument", "size");
		return;
	}
	if (*size < 0) {
		gctl_error(req, "Invalid '%s' argument", "size");
		return;
	}
	secsize = gctl_get_paraml(req, "secsize", sizeof(*secsize));
	if (secsize == NULL) {
		gctl_error(req, "No '%s' argument", "secsize");
		return;
	}
	if (*secsize < 0) {
		gctl_error(req, "Invalid '%s' argument", "secsize");
		return;
	}
	stripesize = gctl_get_paraml(req, "stripesize", sizeof(*stripesize));
	if (stripesize == NULL) {
		gctl_error(req, "No '%s' argument", "stripesize");
		return;
	}
	if (*stripesize < 0) {
		gctl_error(req, "Invalid '%s' argument", "stripesize");
		return;
	}
	stripeoffset = gctl_get_paraml(req, "stripeoffset", sizeof(*stripeoffset));
	if (stripeoffset == NULL) {
		gctl_error(req, "No '%s' argument", "stripeoffset");
		return;
	}
	if (*stripeoffset < 0) {
		gctl_error(req, "Invalid '%s' argument", "stripeoffset");
		return;
	}
	physpath = gctl_get_asciiparam(req, "physpath");

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
			G_NOP_DEBUG(1, "Provider %s is invalid.", name);
			gctl_error(req, "Provider %s is invalid.", name);
			return;
		}
		if (g_nop_create(req, mp, pp,
		    *error == -1 ? EIO : (int)*error,
		    *rfailprob == -1 ? 0 : (u_int)*rfailprob,
		    *wfailprob == -1 ? 0 : (u_int)*wfailprob,
		    (off_t)*offset, (off_t)*size, (u_int)*secsize,
		    (off_t)*stripesize, (off_t)*stripeoffset,
		    physpath) != 0) {
			return;
		}
	}
}

static void
g_nop_ctl_configure(struct gctl_req *req, struct g_class *mp)
{
	struct g_nop_softc *sc;
	struct g_provider *pp;
	intmax_t *error, *rfailprob, *wfailprob;
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
	error = gctl_get_paraml(req, "error", sizeof(*error));
	if (error == NULL) {
		gctl_error(req, "No '%s' argument", "error");
		return;
	}
	rfailprob = gctl_get_paraml(req, "rfailprob", sizeof(*rfailprob));
	if (rfailprob == NULL) {
		gctl_error(req, "No '%s' argument", "rfailprob");
		return;
	}
	if (*rfailprob < -1 || *rfailprob > 100) {
		gctl_error(req, "Invalid '%s' argument", "rfailprob");
		return;
	}
	wfailprob = gctl_get_paraml(req, "wfailprob", sizeof(*wfailprob));
	if (wfailprob == NULL) {
		gctl_error(req, "No '%s' argument", "wfailprob");
		return;
	}
	if (*wfailprob < -1 || *wfailprob > 100) {
		gctl_error(req, "Invalid '%s' argument", "wfailprob");
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
		if (pp == NULL || pp->geom->class != mp) {
			G_NOP_DEBUG(1, "Provider %s is invalid.", name);
			gctl_error(req, "Provider %s is invalid.", name);
			return;
		}
		sc = pp->geom->softc;
		if (*error != -1)
			sc->sc_error = (int)*error;
		if (*rfailprob != -1)
			sc->sc_rfailprob = (u_int)*rfailprob;
		if (*wfailprob != -1)
			sc->sc_wfailprob = (u_int)*wfailprob;
	}
}

static struct g_geom *
g_nop_find_geom(struct g_class *mp, const char *name)
{
	struct g_geom *gp;

	LIST_FOREACH(gp, &mp->geom, geom) {
		if (strcmp(gp->name, name) == 0)
			return (gp);
	}
	return (NULL);
}

static void
g_nop_ctl_destroy(struct gctl_req *req, struct g_class *mp)
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
		gp = g_nop_find_geom(mp, name);
		if (gp == NULL) {
			G_NOP_DEBUG(1, "Device %s is invalid.", name);
			gctl_error(req, "Device %s is invalid.", name);
			return;
		}
		error = g_nop_destroy(gp, *force);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    gp->name, error);
			return;
		}
	}
}

static void
g_nop_ctl_reset(struct gctl_req *req, struct g_class *mp)
{
	struct g_nop_softc *sc;
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
		if (pp == NULL || pp->geom->class != mp) {
			G_NOP_DEBUG(1, "Provider %s is invalid.", name);
			gctl_error(req, "Provider %s is invalid.", name);
			return;
		}
		sc = pp->geom->softc;
		sc->sc_reads = 0;
		sc->sc_writes = 0;
		sc->sc_deletes = 0;
		sc->sc_getattrs = 0;
		sc->sc_flushes = 0;
		sc->sc_cmd0s = 0;
		sc->sc_cmd1s = 0;
		sc->sc_cmd2s = 0;
		sc->sc_readbytes = 0;
		sc->sc_wrotebytes = 0;
	}
}

static void
g_nop_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_NOP_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "create") == 0) {
		g_nop_ctl_create(req, mp);
		return;
	} else if (strcmp(verb, "configure") == 0) {
		g_nop_ctl_configure(req, mp);
		return;
	} else if (strcmp(verb, "destroy") == 0) {
		g_nop_ctl_destroy(req, mp);
		return;
	} else if (strcmp(verb, "reset") == 0) {
		g_nop_ctl_reset(req, mp);
		return;
	}

	gctl_error(req, "Unknown verb.");
}

static void
g_nop_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_nop_softc *sc;

	if (pp != NULL || cp != NULL)
		return;
	sc = gp->softc;
	sbuf_printf(sb, "%s<Offset>%jd</Offset>\n", indent,
	    (intmax_t)sc->sc_offset);
	sbuf_printf(sb, "%s<ReadFailProb>%u</ReadFailProb>\n", indent,
	    sc->sc_rfailprob);
	sbuf_printf(sb, "%s<WriteFailProb>%u</WriteFailProb>\n", indent,
	    sc->sc_wfailprob);
	sbuf_printf(sb, "%s<Error>%d</Error>\n", indent, sc->sc_error);
	sbuf_printf(sb, "%s<Reads>%ju</Reads>\n", indent, sc->sc_reads);
	sbuf_printf(sb, "%s<Writes>%ju</Writes>\n", indent, sc->sc_writes);
	sbuf_printf(sb, "%s<Deletes>%ju</Deletes>\n", indent, sc->sc_deletes);
	sbuf_printf(sb, "%s<Getattrs>%ju</Getattrs>\n", indent, sc->sc_getattrs);
	sbuf_printf(sb, "%s<Flushes>%ju</Flushes>\n", indent, sc->sc_flushes);
	sbuf_printf(sb, "%s<Cmd0s>%ju</Cmd0s>\n", indent, sc->sc_cmd0s);
	sbuf_printf(sb, "%s<Cmd1s>%ju</Cmd1s>\n", indent, sc->sc_cmd1s);
	sbuf_printf(sb, "%s<Cmd2s>%ju</Cmd2s>\n", indent, sc->sc_cmd2s);
	sbuf_printf(sb, "%s<ReadBytes>%ju</ReadBytes>\n", indent,
	    sc->sc_readbytes);
	sbuf_printf(sb, "%s<WroteBytes>%ju</WroteBytes>\n", indent,
	    sc->sc_wrotebytes);
}

DECLARE_GEOM_CLASS(g_nop_class, g_nop);
MODULE_VERSION(geom_nop, 0);
