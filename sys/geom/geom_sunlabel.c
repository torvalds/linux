/*-
 * SPDX-License-Identifier: BSD-3-Clause
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/md5.h>
#include <sys/sbuf.h>
#include <sys/sun_disklabel.h>
#include <geom/geom.h>
#include <geom/geom_slice.h>
#include <machine/endian.h>

FEATURE(geom_sunlabel, "GEOM Sun/Solaris partitioning support");

#define SUNLABEL_CLASS_NAME "SUN"

struct g_sunlabel_softc {
	int sectorsize;
	int nheads;
	int nsects;
	int nalt;
	u_char labelsum[16];
};

static int g_sunlabel_once = 0;

static int
g_sunlabel_modify(struct g_geom *gp, struct g_sunlabel_softc *ms, u_char *sec0)
{
	int i, error;
	u_int u, v, csize;
	struct sun_disklabel sl;
	MD5_CTX md5sum;

	error = sunlabel_dec(sec0, &sl);
	if (error)
		return (error);

	csize = sl.sl_ntracks * sl.sl_nsectors;

	for (i = 0; i < SUN_NPART; i++) {
		v = sl.sl_part[i].sdkp_cyloffset;
		u = sl.sl_part[i].sdkp_nsectors;
		error = g_slice_config(gp, i, G_SLICE_CONFIG_CHECK,
		    ((off_t)v * csize) << 9ULL,
		    ((off_t)u) << 9ULL,
		    ms->sectorsize,
		    "%s%c", gp->name, 'a' + i);
		if (error)
			return (error);
	}
	for (i = 0; i < SUN_NPART; i++) {
		v = sl.sl_part[i].sdkp_cyloffset;
		u = sl.sl_part[i].sdkp_nsectors;
		g_slice_config(gp, i, G_SLICE_CONFIG_SET,
		    ((off_t)v * csize) << 9ULL,
		    ((off_t)u) << 9ULL,
		    ms->sectorsize,
		    "%s%c", gp->name, 'a' + i);
	}
	ms->nalt = sl.sl_acylinders;
	ms->nheads = sl.sl_ntracks;
	ms->nsects = sl.sl_nsectors;

	/*
	 * Calculate MD5 from the first sector and use it for avoiding
	 * recursive labels creation.
	 */
	MD5Init(&md5sum);
	MD5Update(&md5sum, sec0, ms->sectorsize);
	MD5Final(ms->labelsum, &md5sum);

	return (0);
}

static void
g_sunlabel_hotwrite(void *arg, int flag)
{
	struct bio *bp;
	struct g_geom *gp;
	struct g_slicer *gsp;
	struct g_slice *gsl;
	struct g_sunlabel_softc *ms;
	u_char *p;
	int error;

	KASSERT(flag != EV_CANCEL, ("g_sunlabel_hotwrite cancelled"));
	bp = arg;
	gp = bp->bio_to->geom;
	gsp = gp->softc;
	ms = gsp->softc;
	gsl = &gsp->slices[bp->bio_to->index];
	/*
	 * XXX: For all practical purposes, this whould be equvivalent to
	 * XXX: "p = (u_char *)bp->bio_data;" because the label is always
	 * XXX: in the first sector and we refuse sectors smaller than the
	 * XXX: label.
	 */
	p = (u_char *)bp->bio_data - (bp->bio_offset + gsl->offset);

	error = g_sunlabel_modify(gp, ms, p);
	if (error) {
		g_io_deliver(bp, EPERM);
		return;
	}
	g_slice_finish_hot(bp);
}

static void
g_sunlabel_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp, struct g_consumer *cp __unused, struct g_provider *pp)
{
	struct g_slicer *gsp;
	struct g_sunlabel_softc *ms;

	gsp = gp->softc;
	ms = gsp->softc;
	g_slice_dumpconf(sb, indent, gp, cp, pp);
	if (indent == NULL) {
		sbuf_printf(sb, " sc %u hd %u alt %u",
		    ms->nsects, ms->nheads, ms->nalt);
	}
}

struct g_hh01 {
	struct g_geom *gp;
	struct g_sunlabel_softc *ms;
	u_char *label;
	int error;
};

static void
g_sunlabel_callconfig(void *arg, int flag)
{
	struct g_hh01 *hp;

	hp = arg;
	hp->error = g_sunlabel_modify(hp->gp, hp->ms, hp->label);
	if (!hp->error)
		hp->error = g_write_data(LIST_FIRST(&hp->gp->consumer),
		    0, hp->label, SUN_SIZE);
}

/*
 * NB! curthread is user process which GCTL'ed.
 */
static void
g_sunlabel_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	u_char *label;
	int error, i;
	struct g_hh01 h0h0;
	struct g_slicer *gsp;
	struct g_geom *gp;
	struct g_consumer *cp;

	g_topology_assert();
	gp = gctl_get_geom(req, mp, "geom");
	if (gp == NULL)
		return;
	cp = LIST_FIRST(&gp->consumer);
	gsp = gp->softc;
	if (!strcmp(verb, "write label")) {
		label = gctl_get_paraml(req, "label", SUN_SIZE);
		if (label == NULL)
			return;
		h0h0.gp = gp;
		h0h0.ms = gsp->softc;
		h0h0.label = label;
		h0h0.error = -1;
		/* XXX: Does this reference register with our selfdestruct code ? */
		error = g_access(cp, 1, 1, 1);
		if (error) {
			gctl_error(req, "could not access consumer");
			return;
		}
		g_sunlabel_callconfig(&h0h0, 0);
		g_access(cp, -1, -1, -1);
	} else if (!strcmp(verb, "write bootcode")) {
		label = gctl_get_paraml(req, "bootcode", SUN_BOOTSIZE);
		if (label == NULL)
			return;
		/* XXX: Does this reference register with our selfdestruct code ? */
		error = g_access(cp, 1, 1, 1);
		if (error) {
			gctl_error(req, "could not access consumer");
			return;
		}
		for (i = 0; i < SUN_NPART; i++) {
			if (gsp->slices[i].length <= SUN_BOOTSIZE)
				continue;
			g_write_data(cp,
			    gsp->slices[i].offset + SUN_SIZE, label + SUN_SIZE,
			    SUN_BOOTSIZE - SUN_SIZE);
		}
		g_access(cp, -1, -1, -1);
	} else {
		gctl_error(req, "Unknown verb parameter");
	}
}

static int
g_sunlabel_start(struct bio *bp)
{
	struct g_sunlabel_softc *mp;
	struct g_slicer *gsp;

	gsp = bp->bio_to->geom->softc;
	mp = gsp->softc;
	if (bp->bio_cmd == BIO_GETATTR) {
		if (g_handleattr(bp, "SUN::labelsum", mp->labelsum,
		    sizeof(mp->labelsum)))
			return (1);
	}
	return (0);
}

static struct g_geom *
g_sunlabel_taste(struct g_class *mp, struct g_provider *pp, int flags)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_sunlabel_softc *ms;
	struct g_slicer *gsp;
	u_char *buf, hash[16];
	MD5_CTX md5sum;
	int error;

	g_trace(G_T_TOPOLOGY, "g_sunlabel_taste(%s,%s)", mp->name, pp->name);
	g_topology_assert();
	if (flags == G_TF_NORMAL &&
	    !strcmp(pp->geom->class->name, SUNLABEL_CLASS_NAME))
		return (NULL);
	gp = g_slice_new(mp, 8, pp, &cp, &ms, sizeof *ms, g_sunlabel_start);
	if (gp == NULL)
		return (NULL);
	gsp = gp->softc;
	do {
		ms->sectorsize = cp->provider->sectorsize;
		if (ms->sectorsize < 512)
			break;
		g_topology_unlock();
		buf = g_read_data(cp, 0, ms->sectorsize, NULL);
		g_topology_lock();
		if (buf == NULL)
			break;

		/*
		 * Calculate MD5 from the first sector and use it for avoiding
		 * recursive labels creation.
		 */
		MD5Init(&md5sum);
		MD5Update(&md5sum, buf, ms->sectorsize);
		MD5Final(ms->labelsum, &md5sum);
 
		error = g_getattr("SUN::labelsum", cp, &hash);
		if (!error && !bcmp(ms->labelsum, hash, sizeof(hash))) {
			g_free(buf);
			break;
		}

		g_sunlabel_modify(gp, ms, buf);
		g_free(buf);

		break;
	} while (0);
	g_access(cp, -1, 0, 0);
	if (LIST_EMPTY(&gp->provider)) {
		g_slice_spoiled(cp);
		return (NULL);
	}
	g_slice_conf_hot(gp, 0, 0, SUN_SIZE,
	    G_SLICE_HOT_ALLOW, G_SLICE_HOT_DENY, G_SLICE_HOT_CALL);
	gsp->hot = g_sunlabel_hotwrite;
	if (!g_sunlabel_once) {
		g_sunlabel_once = 1;
		printf(
		    "WARNING: geom_sunlabel (geom %s) is deprecated, "
		    "use gpart instead.\n", gp->name);
	}
	return (gp);
}

static struct g_class g_sunlabel_class = {
	.name = SUNLABEL_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_sunlabel_taste,
	.ctlreq = g_sunlabel_config,
	.dumpconf = g_sunlabel_dumpconf,
};

DECLARE_GEOM_CLASS(g_sunlabel_class, g_sunlabel);
MODULE_VERSION(geom_sunlabel, 0);
