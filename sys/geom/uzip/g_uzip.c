/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Max Khon
 * Copyright (c) 2014 Juniper Networks, Inc.
 * Copyright (c) 2006-2016 Maxim Sobolev <sobomax@FreeBSD.org>
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
#include <sys/bio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/kthread.h>

#include <geom/geom.h>

#include <geom/uzip/g_uzip.h>
#include <geom/uzip/g_uzip_cloop.h>
#include <geom/uzip/g_uzip_softc.h>
#include <geom/uzip/g_uzip_dapi.h>
#include <geom/uzip/g_uzip_zlib.h>
#include <geom/uzip/g_uzip_lzma.h>
#include <geom/uzip/g_uzip_wrkthr.h>

#include "opt_geom.h"

MALLOC_DEFINE(M_GEOM_UZIP, "geom_uzip", "GEOM UZIP data structures");

FEATURE(geom_uzip, "GEOM read-only compressed disks support");

struct g_uzip_blk {
        uint64_t offset;
        uint32_t blen;
        unsigned char last:1;
        unsigned char padded:1;
#define BLEN_UNDEF      UINT32_MAX
};

#ifndef ABS
#define	ABS(a)			((a) < 0 ? -(a) : (a))
#endif

#define BLK_IN_RANGE(mcn, bcn, ilen)	\
    (((bcn) != BLEN_UNDEF) && ( \
	((ilen) >= 0 && (mcn >= bcn) && (mcn <= ((intmax_t)(bcn) + (ilen)))) || \
	((ilen) < 0 && (mcn <= bcn) && (mcn >= ((intmax_t)(bcn) + (ilen)))) \
    ))

#ifdef GEOM_UZIP_DEBUG
# define GEOM_UZIP_DBG_DEFAULT	3
#else
# define GEOM_UZIP_DBG_DEFAULT	0
#endif

#define	GUZ_DBG_ERR	1
#define	GUZ_DBG_INFO	2
#define	GUZ_DBG_IO	3
#define	GUZ_DBG_TOC	4

#define	GUZ_DEV_SUFX	".uzip"
#define	GUZ_DEV_NAME(p)	(p GUZ_DEV_SUFX)

static char g_uzip_attach_to[MAXPATHLEN] = {"*"};
static char g_uzip_noattach_to[MAXPATHLEN] = {GUZ_DEV_NAME("*")};
TUNABLE_STR("kern.geom.uzip.attach_to", g_uzip_attach_to,
    sizeof(g_uzip_attach_to));
TUNABLE_STR("kern.geom.uzip.noattach_to", g_uzip_noattach_to,
    sizeof(g_uzip_noattach_to));

SYSCTL_DECL(_kern_geom);
SYSCTL_NODE(_kern_geom, OID_AUTO, uzip, CTLFLAG_RW, 0, "GEOM_UZIP stuff");
static u_int g_uzip_debug = GEOM_UZIP_DBG_DEFAULT;
SYSCTL_UINT(_kern_geom_uzip, OID_AUTO, debug, CTLFLAG_RWTUN, &g_uzip_debug, 0,
    "Debug level (0-4)");
static u_int g_uzip_debug_block = BLEN_UNDEF;
SYSCTL_UINT(_kern_geom_uzip, OID_AUTO, debug_block, CTLFLAG_RWTUN,
    &g_uzip_debug_block, 0, "Debug operations around specific cluster#");

#define	DPRINTF(lvl, a)		\
	if ((lvl) <= g_uzip_debug) { \
		printf a; \
	}
#define	DPRINTF_BLK(lvl, cn, a)	\
	if ((lvl) <= g_uzip_debug || \
	    BLK_IN_RANGE(cn, g_uzip_debug_block, 8) || \
	    BLK_IN_RANGE(cn, g_uzip_debug_block, -8)) { \
		printf a; \
	}
#define	DPRINTF_BRNG(lvl, bcn, ecn, a) \
	KASSERT(bcn < ecn, ("DPRINTF_BRNG: invalid range (%ju, %ju)", \
	    (uintmax_t)bcn, (uintmax_t)ecn)); \
	if (((lvl) <= g_uzip_debug) || \
	    BLK_IN_RANGE(g_uzip_debug_block, bcn, \
	     (intmax_t)ecn - (intmax_t)bcn)) { \
		printf a; \
	}

#define	UZIP_CLASS_NAME	"UZIP"

/*
 * Maximum allowed valid block size (to prevent foot-shooting)
 */
#define	MAX_BLKSZ	(MAXPHYS)

static char CLOOP_MAGIC_START[] = "#!/bin/sh\n";

static void g_uzip_read_done(struct bio *bp);
static void g_uzip_do(struct g_uzip_softc *, struct bio *bp);

static void
g_uzip_softc_free(struct g_uzip_softc *sc, struct g_geom *gp)
{

	if (gp != NULL) {
		DPRINTF(GUZ_DBG_INFO, ("%s: %d requests, %d cached\n",
		    gp->name, sc->req_total, sc->req_cached));
	}

	mtx_lock(&sc->queue_mtx);
	sc->wrkthr_flags |= GUZ_SHUTDOWN;
	wakeup(sc);
	while (!(sc->wrkthr_flags & GUZ_EXITING)) {
		msleep(sc->procp, &sc->queue_mtx, PRIBIO, "guzfree",
		    hz / 10);
	}
	mtx_unlock(&sc->queue_mtx);

	sc->dcp->free(sc->dcp);
	free(sc->toc, M_GEOM_UZIP);
	mtx_destroy(&sc->queue_mtx);
	mtx_destroy(&sc->last_mtx);
	free(sc->last_buf, M_GEOM_UZIP);
	free(sc, M_GEOM_UZIP);
}

static int
g_uzip_cached(struct g_geom *gp, struct bio *bp)
{
	struct g_uzip_softc *sc;
	off_t ofs;
	size_t blk, blkofs, usz;

	sc = gp->softc;
	ofs = bp->bio_offset + bp->bio_completed;
	blk = ofs / sc->blksz;
	mtx_lock(&sc->last_mtx);
	if (blk == sc->last_blk) {
		blkofs = ofs % sc->blksz;
		usz = sc->blksz - blkofs;
		if (bp->bio_resid < usz)
			usz = bp->bio_resid;
		memcpy(bp->bio_data + bp->bio_completed, sc->last_buf + blkofs,
		    usz);
		sc->req_cached++;
		mtx_unlock(&sc->last_mtx);

		DPRINTF(GUZ_DBG_IO, ("%s/%s: %p: offset=%jd: got %jd bytes "
		    "from cache\n", __func__, gp->name, bp, (intmax_t)ofs,
		    (intmax_t)usz));

		bp->bio_completed += usz;
		bp->bio_resid -= usz;

		if (bp->bio_resid == 0) {
			g_io_deliver(bp, 0);
			return (1);
		}
	} else
		mtx_unlock(&sc->last_mtx);

	return (0);
}

#define BLK_ENDS(sc, bi)	((sc)->toc[(bi)].offset + \
    (sc)->toc[(bi)].blen)

#define BLK_IS_CONT(sc, bi)	(BLK_ENDS((sc), (bi) - 1) == \
    (sc)->toc[(bi)].offset)
#define	BLK_IS_NIL(sc, bi)	((sc)->toc[(bi)].blen == 0)

#define TOFF_2_BOFF(sc, pp, bi)	    ((sc)->toc[(bi)].offset - \
    (sc)->toc[(bi)].offset % (pp)->sectorsize)
#define	TLEN_2_BLEN(sc, pp, bp, ei) roundup(BLK_ENDS((sc), (ei)) - \
    (bp)->bio_offset, (pp)->sectorsize)

static int
g_uzip_request(struct g_geom *gp, struct bio *bp)
{
	struct g_uzip_softc *sc;
	struct bio *bp2;
	struct g_consumer *cp;
	struct g_provider *pp;
	off_t ofs, start_blk_ofs;
	size_t i, start_blk, end_blk, zsize;

	if (g_uzip_cached(gp, bp) != 0)
		return (1);

	sc = gp->softc;

	cp = LIST_FIRST(&gp->consumer);
	pp = cp->provider;

	ofs = bp->bio_offset + bp->bio_completed;
	start_blk = ofs / sc->blksz;
	KASSERT(start_blk < sc->nblocks, ("start_blk out of range"));
	end_blk = howmany(ofs + bp->bio_resid, sc->blksz);
	KASSERT(end_blk <= sc->nblocks, ("end_blk out of range"));

	for (; BLK_IS_NIL(sc, start_blk) && start_blk < end_blk; start_blk++) {
		/* Fill in any leading Nil blocks */
		start_blk_ofs = ofs % sc->blksz;
		zsize = MIN(sc->blksz - start_blk_ofs, bp->bio_resid);
		DPRINTF_BLK(GUZ_DBG_IO, start_blk, ("%s/%s: %p/%ju: "
		    "filling %ju zero bytes\n", __func__, gp->name, gp,
		    (uintmax_t)bp->bio_completed, (uintmax_t)zsize));
		bzero(bp->bio_data + bp->bio_completed, zsize);
		bp->bio_completed += zsize;
		bp->bio_resid -= zsize;
		ofs += zsize;
	}

	if (start_blk == end_blk) {
		KASSERT(bp->bio_resid == 0, ("bp->bio_resid is invalid"));
		/*
		 * No non-Nil data is left, complete request immediately.
		 */
		DPRINTF(GUZ_DBG_IO, ("%s/%s: %p: all done returning %ju "
		    "bytes\n", __func__, gp->name, gp,
		    (uintmax_t)bp->bio_completed));
		g_io_deliver(bp, 0);
		return (1);
	}

	for (i = start_blk + 1; i < end_blk; i++) {
		/* Trim discontinuous areas if any */
		if (!BLK_IS_CONT(sc, i)) {
			end_blk = i;
			break;
		}
	}

	DPRINTF_BRNG(GUZ_DBG_IO, start_blk, end_blk, ("%s/%s: %p: "
	    "start=%u (%ju[%jd]), end=%u (%ju)\n", __func__, gp->name, bp,
	    (u_int)start_blk, (uintmax_t)sc->toc[start_blk].offset,
	    (intmax_t)sc->toc[start_blk].blen,
	    (u_int)end_blk, (uintmax_t)BLK_ENDS(sc, end_blk - 1)));

	bp2 = g_clone_bio(bp);
	if (bp2 == NULL) {
		g_io_deliver(bp, ENOMEM);
		return (1);
	}
	bp2->bio_done = g_uzip_read_done;

	bp2->bio_offset = TOFF_2_BOFF(sc, pp, start_blk);
	while (1) {
		bp2->bio_length = TLEN_2_BLEN(sc, pp, bp2, end_blk - 1);
		if (bp2->bio_length <= MAXPHYS) {
			break;
		}
		if (end_blk == (start_blk + 1)) {
			break;
		}
		end_blk--;
	}

	DPRINTF(GUZ_DBG_IO, ("%s/%s: bp2->bio_length = %jd, "
	    "bp2->bio_offset = %jd\n", __func__, gp->name,
	    (intmax_t)bp2->bio_length, (intmax_t)bp2->bio_offset));

	bp2->bio_data = malloc(bp2->bio_length, M_GEOM_UZIP, M_NOWAIT);
	if (bp2->bio_data == NULL) {
		g_destroy_bio(bp2);
		g_io_deliver(bp, ENOMEM);
		return (1);
	}

	DPRINTF_BRNG(GUZ_DBG_IO, start_blk, end_blk, ("%s/%s: %p: "
	    "reading %jd bytes from offset %jd\n", __func__, gp->name, bp,
	    (intmax_t)bp2->bio_length, (intmax_t)bp2->bio_offset));

	g_io_request(bp2, cp);
	return (0);
}

static void
g_uzip_read_done(struct bio *bp)
{
	struct bio *bp2;
	struct g_geom *gp;
	struct g_uzip_softc *sc;

	bp2 = bp->bio_parent;
	gp = bp2->bio_to->geom;
	sc = gp->softc;

	mtx_lock(&sc->queue_mtx);
	bioq_disksort(&sc->bio_queue, bp);
	mtx_unlock(&sc->queue_mtx);
	wakeup(sc);
}

static int
g_uzip_memvcmp(const void *memory, unsigned char val, size_t size)
{
	const u_char *mm;

	mm = (const u_char *)memory;
	return (*mm == val) && memcmp(mm, mm + 1, size - 1) == 0;
}

static void
g_uzip_do(struct g_uzip_softc *sc, struct bio *bp)
{
	struct bio *bp2;
	struct g_provider *pp;
	struct g_consumer *cp;
	struct g_geom *gp;
	char *data, *data2;
	off_t ofs;
	size_t blk, blkofs, len, ulen, firstblk;
	int err;

	bp2 = bp->bio_parent;
	gp = bp2->bio_to->geom;

	cp = LIST_FIRST(&gp->consumer);
	pp = cp->provider;

	bp2->bio_error = bp->bio_error;
	if (bp2->bio_error != 0)
		goto done;

	/* Make sure there's forward progress. */
	if (bp->bio_completed == 0) {
		bp2->bio_error = ECANCELED;
		goto done;
	}

	ofs = bp2->bio_offset + bp2->bio_completed;
	firstblk = blk = ofs / sc->blksz;
	blkofs = ofs % sc->blksz;
	data = bp->bio_data + sc->toc[blk].offset % pp->sectorsize;
	data2 = bp2->bio_data + bp2->bio_completed;
	while (bp->bio_completed && bp2->bio_resid) {
		if (blk > firstblk && !BLK_IS_CONT(sc, blk)) {
			DPRINTF_BLK(GUZ_DBG_IO, blk, ("%s/%s: %p: backref'ed "
			    "cluster #%u requested, looping around\n",
			    __func__, gp->name, bp2, (u_int)blk));
			goto done;
		}
		ulen = MIN(sc->blksz - blkofs, bp2->bio_resid);
		len = sc->toc[blk].blen;
		DPRINTF(GUZ_DBG_IO, ("%s/%s: %p/%ju: data2=%p, ulen=%u, "
		    "data=%p, len=%u\n", __func__, gp->name, gp,
		    bp->bio_completed, data2, (u_int)ulen, data, (u_int)len));
		if (len == 0) {
			/* All zero block: no cache update */
zero_block:
			bzero(data2, ulen);
		} else if (len <= bp->bio_completed) {
			mtx_lock(&sc->last_mtx);
			err = sc->dcp->decompress(sc->dcp, gp->name, data,
			    len, sc->last_buf);
			if (err != 0 && sc->toc[blk].last != 0) {
				/*
				 * Last block decompression has failed, check
				 * if it's just zero padding.
				 */
				if (g_uzip_memvcmp(data, '\0', len) == 0) {
					sc->toc[blk].blen = 0;
					sc->last_blk = -1;
					mtx_unlock(&sc->last_mtx);
					len = 0;
					goto zero_block;
				}
			}
			if (err != 0) {
				sc->last_blk = -1;
				mtx_unlock(&sc->last_mtx);
				bp2->bio_error = EILSEQ;
				DPRINTF(GUZ_DBG_ERR, ("%s/%s: decompress"
				    "(%p, %ju, %ju) failed\n", __func__,
				    gp->name, sc->dcp, (uintmax_t)blk,
				    (uintmax_t)len));
				goto done;
			}
			sc->last_blk = blk;
			memcpy(data2, sc->last_buf + blkofs, ulen);
			mtx_unlock(&sc->last_mtx);
			err = sc->dcp->rewind(sc->dcp, gp->name);
			if (err != 0) {
				bp2->bio_error = EILSEQ;
				DPRINTF(GUZ_DBG_ERR, ("%s/%s: rewind(%p) "
				    "failed\n", __func__, gp->name, sc->dcp));
				goto done;
			}
			data += len;
		} else
			break;

		data2 += ulen;
		bp2->bio_completed += ulen;
		bp2->bio_resid -= ulen;
		bp->bio_completed -= len;
		blkofs = 0;
		blk++;
	}

done:
	/* Finish processing the request. */
	free(bp->bio_data, M_GEOM_UZIP);
	g_destroy_bio(bp);
	if (bp2->bio_error != 0 || bp2->bio_resid == 0)
		g_io_deliver(bp2, bp2->bio_error);
	else
		g_uzip_request(gp, bp2);
}

static void
g_uzip_start(struct bio *bp)
{
	struct g_provider *pp;
	struct g_geom *gp;
	struct g_uzip_softc *sc;

	pp = bp->bio_to;
	gp = pp->geom;

	DPRINTF(GUZ_DBG_IO, ("%s/%s: %p: cmd=%d, offset=%jd, length=%jd, "
	    "buffer=%p\n", __func__, gp->name, bp, bp->bio_cmd,
	    (intmax_t)bp->bio_offset, (intmax_t)bp->bio_length, bp->bio_data));

	sc = gp->softc;
	sc->req_total++;

	if (bp->bio_cmd == BIO_GETATTR) {
		struct bio *bp2;
		struct g_consumer *cp;
		struct g_geom *gp;
		struct g_provider *pp;

		/* pass on MNT:* requests and ignore others */
		if (strncmp(bp->bio_attribute, "MNT:", 4) == 0) {
			bp2 = g_clone_bio(bp);
			if (bp2 == NULL) {
				g_io_deliver(bp, ENOMEM);
				return;
			}
			bp2->bio_done = g_std_done;
			pp = bp->bio_to;
			gp = pp->geom;
			cp = LIST_FIRST(&gp->consumer);
			g_io_request(bp2, cp);
			return;
		}
	}
	if (bp->bio_cmd != BIO_READ) {
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}

	bp->bio_resid = bp->bio_length;
	bp->bio_completed = 0;

	g_uzip_request(gp, bp);
}

static void
g_uzip_orphan(struct g_consumer *cp)
{
	struct g_geom *gp;

	g_trace(G_T_TOPOLOGY, "%s(%p/%s)", __func__, cp, cp->provider->name);
	g_topology_assert();

	gp = cp->geom;
	g_uzip_softc_free(gp->softc, gp);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);
}

static int
g_uzip_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_consumer *cp;

	gp = pp->geom;
	cp = LIST_FIRST(&gp->consumer);
	KASSERT (cp != NULL, ("g_uzip_access but no consumer"));

	if (cp->acw + dw > 0)
		return (EROFS);

	return (g_access(cp, dr, dw, de));
}

static void
g_uzip_spoiled(struct g_consumer *cp)
{
	struct g_geom *gp;

	G_VALID_CONSUMER(cp);
	gp = cp->geom;
	g_trace(G_T_TOPOLOGY, "%s(%p/%s)", __func__, cp, gp->name);
	g_topology_assert();

	g_uzip_softc_free(gp->softc, gp);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);
}

static int
g_uzip_parse_toc(struct g_uzip_softc *sc, struct g_provider *pp,
    struct g_geom *gp)
{
	uint32_t i, j, backref_to;
	uint64_t max_offset, min_offset;
	struct g_uzip_blk *last_blk;

	min_offset = sizeof(struct cloop_header) +
	    (sc->nblocks + 1) * sizeof(uint64_t);
	max_offset = sc->toc[0].offset - 1;
	last_blk = &sc->toc[0];
	for (i = 0; i < sc->nblocks; i++) {
		/* First do some bounds checking */
		if ((sc->toc[i].offset < min_offset) ||
		    (sc->toc[i].offset > pp->mediasize)) {
			goto error_offset;
		}
		DPRINTF_BLK(GUZ_DBG_IO, i, ("%s: cluster #%u "
		    "offset=%ju max_offset=%ju\n", gp->name,
		    (u_int)i, (uintmax_t)sc->toc[i].offset,
		    (uintmax_t)max_offset));
		backref_to = BLEN_UNDEF;
		if (sc->toc[i].offset < max_offset) {
			/*
			 * For the backref'ed blocks search already parsed
			 * TOC entries for the matching offset and copy the
			 * size from matched entry.
			 */
			for (j = 0; j <= i; j++) {
                                if (sc->toc[j].offset == sc->toc[i].offset &&
				    !BLK_IS_NIL(sc, j)) {
                                        break;
                                }
                                if (j != i) {
					continue;
				}
				DPRINTF(GUZ_DBG_ERR, ("%s: cannot match "
				    "backref'ed offset at cluster #%u\n",
				    gp->name, i));
				return (-1);
			}
			sc->toc[i].blen = sc->toc[j].blen;
			backref_to = j;
		} else {
			last_blk = &sc->toc[i];
			/*
			 * For the "normal blocks" seek forward until we hit
			 * block whose offset is larger than ours and assume
			 * it's going to be the next one.
			 */
			for (j = i + 1; j < sc->nblocks; j++) {
				if (sc->toc[j].offset > max_offset) {
					break;
				}
			}
			sc->toc[i].blen = sc->toc[j].offset -
			    sc->toc[i].offset;
			if (BLK_ENDS(sc, i) > pp->mediasize) {
				DPRINTF(GUZ_DBG_ERR, ("%s: cluster #%u "
				    "extends past media boundary (%ju > %ju)\n",
				    gp->name, (u_int)i,
				    (uintmax_t)BLK_ENDS(sc, i),
				    (intmax_t)pp->mediasize));
				return (-1);
			}
			KASSERT(max_offset <= sc->toc[i].offset, (
			    "%s: max_offset is incorrect: %ju",
			    gp->name, (uintmax_t)max_offset));
			max_offset = BLK_ENDS(sc, i) - 1;
		}
		DPRINTF_BLK(GUZ_DBG_TOC, i, ("%s: cluster #%u, original %u "
		    "bytes, in %u bytes", gp->name, i, sc->blksz,
		    sc->toc[i].blen));
		if (backref_to != BLEN_UNDEF) {
			DPRINTF_BLK(GUZ_DBG_TOC, i, (" (->#%u)",
			    (u_int)backref_to));
		}
		DPRINTF_BLK(GUZ_DBG_TOC, i, ("\n"));
	}
	last_blk->last = 1;
	/* Do a second pass to validate block lengths */
	for (i = 0; i < sc->nblocks; i++) {
		if (sc->toc[i].blen > sc->dcp->max_blen) {
			if (sc->toc[i].last == 0) {
				DPRINTF(GUZ_DBG_ERR, ("%s: cluster #%u "
				    "length (%ju) exceeds "
				    "max_blen (%ju)\n", gp->name, i,
				    (uintmax_t)sc->toc[i].blen,
				    (uintmax_t)sc->dcp->max_blen));
				return (-1);
			}
			DPRINTF(GUZ_DBG_INFO, ("%s: cluster #%u extra "
			    "padding is detected, trimmed to %ju\n",
			    gp->name, i, (uintmax_t)sc->dcp->max_blen));
			    sc->toc[i].blen = sc->dcp->max_blen;
			sc->toc[i].padded = 1;
		}
	}
	return (0);

error_offset:
	DPRINTF(GUZ_DBG_ERR, ("%s: cluster #%u: invalid offset %ju, "
	    "min_offset=%ju mediasize=%jd\n", gp->name, (u_int)i,
	    sc->toc[i].offset, min_offset, pp->mediasize));
	return (-1);
}

static struct g_geom *
g_uzip_taste(struct g_class *mp, struct g_provider *pp, int flags)
{
	int error;
	uint32_t i, total_offsets, offsets_read, blk;
	void *buf;
	struct cloop_header *header;
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_provider *pp2;
	struct g_uzip_softc *sc;
	enum {
		G_UZIP = 1,
		G_ULZMA
	} type;

	g_trace(G_T_TOPOLOGY, "%s(%s,%s)", __func__, mp->name, pp->name);
	g_topology_assert();

	/* Skip providers that are already open for writing. */
	if (pp->acw > 0)
		return (NULL);

	if ((fnmatch(g_uzip_attach_to, pp->name, 0) != 0) ||
	    (fnmatch(g_uzip_noattach_to, pp->name, 0) == 0)) {
		DPRINTF(GUZ_DBG_INFO, ("%s(%s,%s), ignoring\n", __func__,
		    mp->name, pp->name));
		return (NULL);
	}

	buf = NULL;

	/*
	 * Create geom instance.
	 */
	gp = g_new_geomf(mp, GUZ_DEV_NAME("%s"), pp->name);
	cp = g_new_consumer(gp);
	error = g_attach(cp, pp);
	if (error == 0)
		error = g_access(cp, 1, 0, 0);
	if (error) {
		goto e1;
	}
	g_topology_unlock();

	/*
	 * Read cloop header, look for CLOOP magic, perform
	 * other validity checks.
	 */
	DPRINTF(GUZ_DBG_INFO, ("%s: media sectorsize %u, mediasize %jd\n",
	    gp->name, pp->sectorsize, (intmax_t)pp->mediasize));
	buf = g_read_data(cp, 0, pp->sectorsize, NULL);
	if (buf == NULL)
		goto e2;
	header = (struct cloop_header *) buf;
	if (strncmp(header->magic, CLOOP_MAGIC_START,
	    sizeof(CLOOP_MAGIC_START) - 1) != 0) {
		DPRINTF(GUZ_DBG_ERR, ("%s: no CLOOP magic\n", gp->name));
		goto e3;
	}

	switch (header->magic[CLOOP_OFS_COMPR]) {
	case CLOOP_COMP_LZMA:
	case CLOOP_COMP_LZMA_DDP:
		type = G_ULZMA;
		if (header->magic[CLOOP_OFS_VERSN] < CLOOP_MINVER_LZMA) {
			DPRINTF(GUZ_DBG_ERR, ("%s: image version too old\n",
			    gp->name));
			goto e3;
		}
		DPRINTF(GUZ_DBG_INFO, ("%s: GEOM_UZIP_LZMA image found\n",
		    gp->name));
		break;
	case CLOOP_COMP_LIBZ:
	case CLOOP_COMP_LIBZ_DDP:
		type = G_UZIP;
		if (header->magic[CLOOP_OFS_VERSN] < CLOOP_MINVER_ZLIB) {
			DPRINTF(GUZ_DBG_ERR, ("%s: image version too old\n",
			    gp->name));
			goto e3;
		}
		DPRINTF(GUZ_DBG_INFO, ("%s: GEOM_UZIP_ZLIB image found\n",
		    gp->name));
		break;
	default:
		DPRINTF(GUZ_DBG_ERR, ("%s: unsupported image type\n",
		    gp->name));
                goto e3;
        }

	/*
	 * Initialize softc and read offsets.
	 */
	sc = malloc(sizeof(*sc), M_GEOM_UZIP, M_WAITOK | M_ZERO);
	gp->softc = sc;
	sc->blksz = ntohl(header->blksz);
	sc->nblocks = ntohl(header->nblocks);
	if (sc->blksz % 512 != 0) {
		printf("%s: block size (%u) should be multiple of 512.\n",
		    gp->name, sc->blksz);
		goto e4;
	}
	if (sc->blksz > MAX_BLKSZ) {
		printf("%s: block size (%u) should not be larger than %d.\n",
		    gp->name, sc->blksz, MAX_BLKSZ);
	}
	total_offsets = sc->nblocks + 1;
	if (sizeof(struct cloop_header) +
	    total_offsets * sizeof(uint64_t) > pp->mediasize) {
		printf("%s: media too small for %u blocks\n",
		    gp->name, sc->nblocks);
		goto e4;
	}
	sc->toc = malloc(total_offsets * sizeof(struct g_uzip_blk),
	    M_GEOM_UZIP, M_WAITOK | M_ZERO);
	offsets_read = MIN(total_offsets,
	    (pp->sectorsize - sizeof(*header)) / sizeof(uint64_t));
	for (i = 0; i < offsets_read; i++) {
		sc->toc[i].offset = be64toh(((uint64_t *) (header + 1))[i]);
		sc->toc[i].blen = BLEN_UNDEF;
	}
	DPRINTF(GUZ_DBG_INFO, ("%s: %u offsets in the first sector\n",
	       gp->name, offsets_read));
	for (blk = 1; offsets_read < total_offsets; blk++) {
		uint32_t nread;

		free(buf, M_GEOM);
		buf = g_read_data(
		    cp, blk * pp->sectorsize, pp->sectorsize, NULL);
		if (buf == NULL)
			goto e5;
		nread = MIN(total_offsets - offsets_read,
		     pp->sectorsize / sizeof(uint64_t));
		DPRINTF(GUZ_DBG_TOC, ("%s: %u offsets read from sector %d\n",
		    gp->name, nread, blk));
		for (i = 0; i < nread; i++) {
			sc->toc[offsets_read + i].offset =
			    be64toh(((uint64_t *) buf)[i]);
			sc->toc[offsets_read + i].blen = BLEN_UNDEF;
		}
		offsets_read += nread;
	}
	free(buf, M_GEOM);
	buf = NULL;
	offsets_read -= 1;
	DPRINTF(GUZ_DBG_INFO, ("%s: done reading %u block offsets from %u "
	    "sectors\n", gp->name, offsets_read, blk));
	if (sc->nblocks != offsets_read) {
		DPRINTF(GUZ_DBG_ERR, ("%s: read %s offsets than expected "
		    "blocks\n", gp->name,
		    sc->nblocks < offsets_read ? "more" : "less"));
		goto e5;
	}

	if (type == G_UZIP) {
		sc->dcp = g_uzip_zlib_ctor(sc->blksz);
	} else {
		sc->dcp = g_uzip_lzma_ctor(sc->blksz);
	}
	if (sc->dcp == NULL) {
		goto e5;
	}

	/*
	 * "Fake" last+1 block, to make it easier for the TOC parser to
	 * iterate without making the last element a special case.
	 */
	sc->toc[sc->nblocks].offset = pp->mediasize;
	/* Massage TOC (table of contents), make sure it is sound */
	if (g_uzip_parse_toc(sc, pp, gp) != 0) {
		DPRINTF(GUZ_DBG_ERR, ("%s: TOC error\n", gp->name));
		goto e6;
	}
	mtx_init(&sc->last_mtx, "geom_uzip cache", NULL, MTX_DEF);
	mtx_init(&sc->queue_mtx, "geom_uzip wrkthread", NULL, MTX_DEF);
	bioq_init(&sc->bio_queue);
	sc->last_blk = -1;
	sc->last_buf = malloc(sc->blksz, M_GEOM_UZIP, M_WAITOK);
	sc->req_total = 0;
	sc->req_cached = 0;

	sc->uzip_do = &g_uzip_do;

	error = kproc_create(g_uzip_wrkthr, sc, &sc->procp, 0, 0, "%s",
	    gp->name);
	if (error != 0) {
		goto e7;
	}

	g_topology_lock();
	pp2 = g_new_providerf(gp, "%s", gp->name);
	pp2->sectorsize = 512;
	pp2->mediasize = (off_t)sc->nblocks * sc->blksz;
	pp2->stripesize = pp->stripesize;
	pp2->stripeoffset = pp->stripeoffset;
	g_error_provider(pp2, 0);
	g_access(cp, -1, 0, 0);

	DPRINTF(GUZ_DBG_INFO, ("%s: taste ok (%d, %ju), (%ju, %ju), %x\n",
	    gp->name, pp2->sectorsize, (uintmax_t)pp2->mediasize,
	    (uintmax_t)pp2->stripeoffset, (uintmax_t)pp2->stripesize, pp2->flags));
	DPRINTF(GUZ_DBG_INFO, ("%s: %u x %u blocks\n", gp->name, sc->nblocks,
	    sc->blksz));
	return (gp);

e7:
	free(sc->last_buf, M_GEOM);
	mtx_destroy(&sc->queue_mtx);
	mtx_destroy(&sc->last_mtx);
e6:
	sc->dcp->free(sc->dcp);
e5:
	free(sc->toc, M_GEOM);
e4:
	free(gp->softc, M_GEOM_UZIP);
e3:
	if (buf != NULL) {
		free(buf, M_GEOM);
	}
e2:
	g_topology_lock();
	g_access(cp, -1, 0, 0);
e1:
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);

	return (NULL);
}

static int
g_uzip_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{
	struct g_provider *pp;

	KASSERT(gp != NULL, ("NULL geom"));
	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, gp->name);
	g_topology_assert();

	if (gp->softc == NULL) {
		DPRINTF(GUZ_DBG_ERR, ("%s(%s): gp->softc == NULL\n", __func__,
		    gp->name));
		return (ENXIO);
	}

	pp = LIST_FIRST(&gp->provider);
	KASSERT(pp != NULL, ("NULL provider"));
	if (pp->acr > 0 || pp->acw > 0 || pp->ace > 0)
		return (EBUSY);

	g_uzip_softc_free(gp->softc, gp);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);

	return (0);
}

static struct g_class g_uzip_class = {
	.name = UZIP_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_uzip_taste,
	.destroy_geom = g_uzip_destroy_geom,

	.start = g_uzip_start,
	.orphan = g_uzip_orphan,
	.access = g_uzip_access,
	.spoiled = g_uzip_spoiled,
};

DECLARE_GEOM_CLASS(g_uzip_class, g_uzip);
MODULE_DEPEND(g_uzip, xz, 1, 1, 1);
MODULE_DEPEND(g_uzip, zlib, 1, 1, 1);
MODULE_VERSION(geom_uzip, 0);
