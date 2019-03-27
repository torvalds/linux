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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/md5.h>
#include <sys/proc.h>

#include <sys/diskmbr.h>
#include <sys/sbuf.h>
#include <geom/geom.h>
#include <geom/geom_slice.h>

FEATURE(geom_mbr, "GEOM DOS/MBR partitioning support");

#define MBR_CLASS_NAME "MBR"
#define MBREXT_CLASS_NAME "MBREXT"

static int g_mbr_once = 0;

static struct dos_partition historical_bogus_partition_table[NDOSPART] = {
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
        { 0x80, 0, 1, 0, DOSPTYP_386BSD, 255, 255, 255, 0, 50000, },
};

static struct dos_partition historical_bogus_partition_table_fixed[NDOSPART] = {
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
        { 0x80, 0, 1, 0, DOSPTYP_386BSD, 254, 255, 255, 0, 50000, },
};

static void
g_mbr_print(int i, struct dos_partition *dp)
{

	printf("[%d] f:%02x typ:%d", i, dp->dp_flag, dp->dp_typ);
	printf(" s(CHS):%d/%d/%d", DPCYL(dp->dp_scyl, dp->dp_ssect),
	    dp->dp_shd, DPSECT(dp->dp_ssect));
	printf(" e(CHS):%d/%d/%d", DPCYL(dp->dp_ecyl, dp->dp_esect),
	    dp->dp_ehd, DPSECT(dp->dp_esect));
	printf(" s:%d l:%d\n", dp->dp_start, dp->dp_size);
}

struct g_mbr_softc {
	int		type [NDOSPART];
	u_int		sectorsize;
	u_char		sec0[512];
	u_char		slicesum[16];
};

/*
 * XXX: Add gctl_req arg and give good error msgs.
 * XXX: Check that length argument does not bring boot code inside any slice.
 */
static int
g_mbr_modify(struct g_geom *gp, struct g_mbr_softc *ms, u_char *sec0, int len __unused)
{
	int i, error;
	off_t l[NDOSPART];
	struct dos_partition ndp[NDOSPART], *dp;
	MD5_CTX md5sum;

	g_topology_assert();

	if (sec0[0x1fe] != 0x55 && sec0[0x1ff] != 0xaa)
		return (EBUSY);

	dp = ndp;
	for (i = 0; i < NDOSPART; i++) {
		dos_partition_dec(
		    sec0 + DOSPARTOFF + i * sizeof(struct dos_partition),
		    dp + i);
	}
	if ((!bcmp(dp, historical_bogus_partition_table,
	    sizeof historical_bogus_partition_table)) ||
	    (!bcmp(dp, historical_bogus_partition_table_fixed,
	    sizeof historical_bogus_partition_table_fixed))) {
		/*
		 * We will not allow people to write these from "the inside",
		 * Since properly selfdestructing takes too much code.  If 
		 * people really want to do this, they cannot have any
		 * providers of this geom open, and in that case they can just
		 * as easily overwrite the MBR in the parent device.
		 */
		return(EBUSY);
	}
	for (i = 0; i < NDOSPART; i++) {
		/* 
		 * A Protective MBR (PMBR) has a single partition of
		 * type 0xEE spanning the whole disk. Such a MBR
		 * protects a GPT on the disk from MBR tools that
		 * don't know anything about GPT. We're interpreting
		 * it a bit more loosely: any partition of type 0xEE
		 * is to be skipped as it doesn't contain any data
		 * that we should care about. We still allow other
		 * partitions to be present in the MBR. A PMBR will
		 * be handled correctly anyway.
		 */
		if (dp[i].dp_typ == DOSPTYP_PMBR)
			l[i] = 0;
		else if (dp[i].dp_flag != 0 && dp[i].dp_flag != 0x80)
			l[i] = 0;
		else if (dp[i].dp_typ == 0)
			l[i] = 0;
		else
			l[i] = (off_t)dp[i].dp_size * ms->sectorsize;
		error = g_slice_config(gp, i, G_SLICE_CONFIG_CHECK,
		    (off_t)dp[i].dp_start * ms->sectorsize, l[i],
		    ms->sectorsize, "%ss%d", gp->name, 1 + i);
		if (error)
			return (error);
	}
	for (i = 0; i < NDOSPART; i++) {
		ms->type[i] = dp[i].dp_typ;
		g_slice_config(gp, i, G_SLICE_CONFIG_SET,
		    (off_t)dp[i].dp_start * ms->sectorsize, l[i],
		    ms->sectorsize, "%ss%d", gp->name, 1 + i);
	}
	bcopy(sec0, ms->sec0, 512);

	/*
	 * Calculate MD5 from the first sector and use it for avoiding
	 * recursive slices creation.
	 */
	MD5Init(&md5sum);
	MD5Update(&md5sum, ms->sec0, sizeof(ms->sec0));
	MD5Final(ms->slicesum, &md5sum);

	return (0);
}

static int
g_mbr_ioctl(struct g_provider *pp, u_long cmd, void *data, int fflag, struct thread *td)
{
	struct g_geom *gp;
	struct g_mbr_softc *ms;
	struct g_slicer *gsp;
	struct g_consumer *cp;
	int error, opened;

	gp = pp->geom;
	gsp = gp->softc;
	ms = gsp->softc;

	opened = 0;
	error = 0;
	switch(cmd) {
	case DIOCSMBR: {
		if (!(fflag & FWRITE))
			return (EPERM);
		g_topology_lock();
		cp = LIST_FIRST(&gp->consumer);
		if (cp->acw == 0) {
			error = g_access(cp, 0, 1, 0);
			if (error == 0)
				opened = 1;
		}
		if (!error)
			error = g_mbr_modify(gp, ms, data, 512);
		if (!error)
			error = g_write_data(cp, 0, data, 512);
		if (opened)
			g_access(cp, 0, -1 , 0);
		g_topology_unlock();
		return(error);
	}
	default:
		return (ENOIOCTL);
	}
}

static int
g_mbr_start(struct bio *bp)
{
	struct g_provider *pp;
	struct g_geom *gp;
	struct g_mbr_softc *mp;
	struct g_slicer *gsp;
	int idx;

	pp = bp->bio_to;
	idx = pp->index;
	gp = pp->geom;
	gsp = gp->softc;
	mp = gsp->softc;
	if (bp->bio_cmd == BIO_GETATTR) {
		if (g_handleattr_int(bp, "MBR::type", mp->type[idx]))
			return (1);
		if (g_handleattr_off_t(bp, "MBR::offset",
		    gsp->slices[idx].offset))
			return (1);
		if (g_handleattr(bp, "MBR::slicesum", mp->slicesum,
		    sizeof(mp->slicesum)))
			return (1);
	}

	return (0);
}

static void
g_mbr_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp, struct g_consumer *cp __unused, struct g_provider *pp)
{
	struct g_mbr_softc *mp;
	struct g_slicer *gsp;

	gsp = gp->softc;
	mp = gsp->softc;
	g_slice_dumpconf(sb, indent, gp, cp, pp);
	if (pp != NULL) {
		if (indent == NULL)
			sbuf_printf(sb, " ty %d", mp->type[pp->index]);
		else
			sbuf_printf(sb, "%s<type>%d</type>\n", indent,
			    mp->type[pp->index]);
	}
}

static struct g_geom *
g_mbr_taste(struct g_class *mp, struct g_provider *pp, int insist)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	int error;
	struct g_mbr_softc *ms;
	u_int fwsectors, sectorsize;
	u_char *buf;
	u_char hash[16];
	MD5_CTX md5sum;

	g_trace(G_T_TOPOLOGY, "mbr_taste(%s,%s)", mp->name, pp->name);
	g_topology_assert();
	if (!strcmp(pp->geom->class->name, MBR_CLASS_NAME))
		return (NULL);
	gp = g_slice_new(mp, NDOSPART, pp, &cp, &ms, sizeof *ms, g_mbr_start);
	if (gp == NULL)
		return (NULL);
	g_topology_unlock();
	do {
		error = g_getattr("GEOM::fwsectors", cp, &fwsectors);
		if (error)
			fwsectors = 17;
		sectorsize = cp->provider->sectorsize;
		if (sectorsize < 512)
			break;
		ms->sectorsize = sectorsize;
		buf = g_read_data(cp, 0, sectorsize, NULL);
		if (buf == NULL)
			break;

		/*
		 * Calculate MD5 from the first sector and use it for avoiding
		 * recursive slices creation.
		 */
		bcopy(buf, ms->sec0, 512);
		MD5Init(&md5sum);
		MD5Update(&md5sum, ms->sec0, sizeof(ms->sec0));
		MD5Final(ms->slicesum, &md5sum);

		error = g_getattr("MBR::slicesum", cp, &hash);
		if (!error && !bcmp(ms->slicesum, hash, sizeof(hash))) {
			g_free(buf);
			break;
		}

		g_topology_lock();
		g_mbr_modify(gp, ms, buf, 512);
		g_topology_unlock();
		g_free(buf);
		break;
	} while (0);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (LIST_EMPTY(&gp->provider)) {
		g_slice_spoiled(cp);
		return (NULL);
	}
	if (!g_mbr_once) {
		g_mbr_once = 1;
		printf(
		    "WARNING: geom_mbr (geom %s) is deprecated, "
		    "use gpart instead.\n", gp->name);
	}
	return (gp);
}

static void
g_mbr_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_mbr_softc *ms;
	struct g_slicer *gsp;
	int opened = 0, error = 0;
	void *data;
	int len;

	g_topology_assert();
	gp = gctl_get_geom(req, mp, "geom");
	if (gp == NULL)
		return;
	if (strcmp(verb, "write MBR")) {
		gctl_error(req, "Unknown verb");
		return;
	}
	gsp = gp->softc;
	ms = gsp->softc;
	data = gctl_get_param(req, "data", &len);
	if (data == NULL)
		return;
	if (len < 512 || (len % 512)) {
		gctl_error(req, "Wrong request length");
		return;
	}
	cp = LIST_FIRST(&gp->consumer);
	if (cp->acw == 0) {
		error = g_access(cp, 0, 1, 0);
		if (error == 0)
			opened = 1;
	}
	if (!error)
		error = g_mbr_modify(gp, ms, data, len);
	if (error)
		gctl_error(req, "conflict with open slices");
	if (!error)
		error = g_write_data(cp, 0, data, len);
	if (error)
		gctl_error(req, "sector zero write failed");
	if (opened)
		g_access(cp, 0, -1 , 0);
	return;
}

static struct g_class g_mbr_class	= {
	.name = MBR_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_mbr_taste,
	.dumpconf = g_mbr_dumpconf,
	.ctlreq = g_mbr_config,
	.ioctl = g_mbr_ioctl,
};

DECLARE_GEOM_CLASS(g_mbr_class, g_mbr);

#define NDOSEXTPART		32
struct g_mbrext_softc {
	int		type [NDOSEXTPART];
};

static int
g_mbrext_start(struct bio *bp)
{
	struct g_provider *pp;
	struct g_geom *gp;
	struct g_mbrext_softc *mp;
	struct g_slicer *gsp;
	int idx;

	pp = bp->bio_to;
	idx = pp->index;
	gp = pp->geom;
	gsp = gp->softc;
	mp = gsp->softc;
	if (bp->bio_cmd == BIO_GETATTR) {
		if (g_handleattr_int(bp, "MBR::type", mp->type[idx]))
			return (1);
	}
	return (0);
}

static void
g_mbrext_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp, struct g_consumer *cp __unused, struct g_provider *pp)
{
	struct g_mbrext_softc *mp;
	struct g_slicer *gsp;

	g_slice_dumpconf(sb, indent, gp, cp, pp);
	gsp = gp->softc;
	mp = gsp->softc;
	if (pp != NULL) {
		if (indent == NULL)
			sbuf_printf(sb, " ty %d", mp->type[pp->index]);
		else
			sbuf_printf(sb, "%s<type>%d</type>\n", indent,
			    mp->type[pp->index]);
	}
}

static struct g_geom *
g_mbrext_taste(struct g_class *mp, struct g_provider *pp, int insist __unused)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	int error, i, slice;
	struct g_mbrext_softc *ms;
	off_t off;
	u_char *buf;
	struct dos_partition dp[4];
	u_int fwsectors, sectorsize;

	g_trace(G_T_TOPOLOGY, "g_mbrext_taste(%s,%s)", mp->name, pp->name);
	g_topology_assert();
	if (strcmp(pp->geom->class->name, MBR_CLASS_NAME))
		return (NULL);
	gp = g_slice_new(mp, NDOSEXTPART, pp, &cp, &ms, sizeof *ms,
	    g_mbrext_start);
	if (gp == NULL)
		return (NULL);
	g_topology_unlock();
	off = 0;
	slice = 0;
	do {
		error = g_getattr("MBR::type", cp, &i);
		if (error || (i != DOSPTYP_EXT && i != DOSPTYP_EXTLBA))
			break;
		error = g_getattr("GEOM::fwsectors", cp, &fwsectors);
		if (error)
			fwsectors = 17;
		sectorsize = cp->provider->sectorsize;
		if (sectorsize != 512)
			break;
		for (;;) {
			buf = g_read_data(cp, off, sectorsize, NULL);
			if (buf == NULL)
				break;
			if (buf[0x1fe] != 0x55 && buf[0x1ff] != 0xaa) {
				g_free(buf);
				break;
			}
			for (i = 0; i < NDOSPART; i++) 
				dos_partition_dec(
				    buf + DOSPARTOFF + 
				    i * sizeof(struct dos_partition), dp + i);
			g_free(buf);
			if (0 && bootverbose) {
				printf("MBREXT Slice %d on %s:\n",
				    slice + 5, gp->name);
				g_mbr_print(0, dp);
				g_mbr_print(1, dp + 1);
			}
			if ((dp[0].dp_flag & 0x7f) == 0 &&
			     dp[0].dp_size != 0 && dp[0].dp_typ != 0) {
				g_topology_lock();
				g_slice_config(gp, slice, G_SLICE_CONFIG_SET,
				    (((off_t)dp[0].dp_start) << 9ULL) + off,
				    ((off_t)dp[0].dp_size) << 9ULL,
				    sectorsize,
				    "%*.*s%d",
				    (int)strlen(gp->name) - 1,
				    (int)strlen(gp->name) - 1,
				    gp->name,
				    slice + 5);
				g_topology_unlock();
				ms->type[slice] = dp[0].dp_typ;
				slice++;
			}
			if (dp[1].dp_flag != 0)
				break;
			if (dp[1].dp_typ != DOSPTYP_EXT &&
			    dp[1].dp_typ != DOSPTYP_EXTLBA)
				break;
			if (dp[1].dp_size == 0)
				break;
			off = ((off_t)dp[1].dp_start) << 9ULL;
		}
		break;
	} while (0);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (LIST_EMPTY(&gp->provider)) {
		g_slice_spoiled(cp);
		return (NULL);
	}
	return (gp);
}


static struct g_class g_mbrext_class	= {
	.name = MBREXT_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_mbrext_taste,
	.dumpconf = g_mbrext_dumpconf,
};

DECLARE_GEOM_CLASS(g_mbrext_class, g_mbrext);
MODULE_VERSION(geom_mbr, 0);
