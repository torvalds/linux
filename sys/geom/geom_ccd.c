/*-
 * SPDX-License-Identifier: (BSD-2-Clause-NetBSD AND BSD-3-Clause)
 *
 * Copyright (c) 2003 Poul-Henning Kamp.
 * Copyright (c) 1996, 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $NetBSD: ccd.c,v 1.22 1995/12/08 19:13:26 thorpej Exp $ 
 */

/*-
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: Utah $Hdr: cd.c 1.6 90/11/28$
 *
 *	@(#)cd.c	8.2 (Berkeley) 11/16/93
 */

/*
 * Dynamic configuration and disklabel support by:
 *	Jason R. Thorpe <thorpej@nas.nasa.gov>
 *	Numerical Aerodynamic Simulation Facility
 *	Mail Stop 258-6
 *	NASA Ames Research Center
 *	Moffett Field, CA 94035
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>
#include <geom/geom.h>

/*
 * Number of blocks to untouched in front of a component partition.
 * This is to avoid violating its disklabel area when it starts at the
 * beginning of the slice.
 */
#if !defined(CCD_OFFSET)
#define CCD_OFFSET 16
#endif

/* sc_flags */
#define CCDF_UNIFORM	0x02	/* use LCCD of sizes for uniform interleave */
#define CCDF_MIRROR	0x04	/* use mirroring */
#define CCDF_NO_OFFSET	0x08	/* do not leave space in front */
#define CCDF_LINUX	0x10	/* use Linux compatibility mode */

/* Mask of user-settable ccd flags. */
#define CCDF_USERMASK	(CCDF_UNIFORM|CCDF_MIRROR)

/*
 * Interleave description table.
 * Computed at boot time to speed irregular-interleave lookups.
 * The idea is that we interleave in "groups".  First we interleave
 * evenly over all component disks up to the size of the smallest
 * component (the first group), then we interleave evenly over all
 * remaining disks up to the size of the next-smallest (second group),
 * and so on.
 *
 * Each table entry describes the interleave characteristics of one
 * of these groups.  For example if a concatenated disk consisted of
 * three components of 5, 3, and 7 DEV_BSIZE blocks interleaved at
 * DEV_BSIZE (1), the table would have three entries:
 *
 *	ndisk	startblk	startoff	dev
 *	3	0		0		0, 1, 2
 *	2	9		3		0, 2
 *	1	13		5		2
 *	0	-		-		-
 *
 * which says that the first nine blocks (0-8) are interleaved over
 * 3 disks (0, 1, 2) starting at block offset 0 on any component disk,
 * the next 4 blocks (9-12) are interleaved over 2 disks (0, 2) starting
 * at component block 3, and the remaining blocks (13-14) are on disk
 * 2 starting at offset 5.
 */
struct ccdiinfo {
	int	ii_ndisk;	/* # of disks range is interleaved over */
	daddr_t	ii_startblk;	/* starting scaled block # for range */
	daddr_t	ii_startoff;	/* starting component offset (block #) */
	int	*ii_index;	/* ordered list of components in range */
};

/*
 * Component info table.
 * Describes a single component of a concatenated disk.
 */
struct ccdcinfo {
	daddr_t		ci_size; 		/* size */
	struct g_provider *ci_provider;		/* provider */
	struct g_consumer *ci_consumer;		/* consumer */
};

/*
 * A concatenated disk is described by this structure.
 */

struct ccd_s {
	LIST_ENTRY(ccd_s) list;

	int		 sc_unit;		/* logical unit number */
	int		 sc_flags;		/* flags */
	daddr_t		 sc_size;		/* size of ccd */
	int		 sc_ileave;		/* interleave */
	u_int		 sc_ndisks;		/* number of components */
	struct ccdcinfo	 *sc_cinfo;		/* component info */
	struct ccdiinfo	 *sc_itable;		/* interleave table */
	u_int32_t	 sc_secsize;		/* # bytes per sector */
	int		 sc_pick;		/* side of mirror picked */
	daddr_t		 sc_blk[2];		/* mirror localization */
	u_int32_t	 sc_offset;		/* actual offset used */
};

static g_start_t g_ccd_start;
static void ccdiodone(struct bio *bp);
static void ccdinterleave(struct ccd_s *);
static int ccdinit(struct gctl_req *req, struct ccd_s *);
static int ccdbuffer(struct bio **ret, struct ccd_s *,
		      struct bio *, daddr_t, caddr_t, long);

static void
g_ccd_orphan(struct g_consumer *cp)
{
	/*
	 * XXX: We don't do anything here.  It is not obvious
	 * XXX: what DTRT would be, so we do what the previous
	 * XXX: code did: ignore it and let the user cope.
	 */
}

static int
g_ccd_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_geom *gp;
	struct g_consumer *cp1, *cp2;
	int error;

	de += dr;
	de += dw;

	gp = pp->geom;
	error = ENXIO;
	LIST_FOREACH(cp1, &gp->consumer, consumer) {
		error = g_access(cp1, dr, dw, de);
		if (error) {
			LIST_FOREACH(cp2, &gp->consumer, consumer) {
				if (cp1 == cp2)
					break;
				g_access(cp2, -dr, -dw, -de);
			}
			break;
		}
	}
	return (error);
}

/*
 * Free the softc and its substructures.
 */
static void
g_ccd_freesc(struct ccd_s *sc)
{
	struct ccdiinfo *ii;

	g_free(sc->sc_cinfo);
	if (sc->sc_itable != NULL) {
		for (ii = sc->sc_itable; ii->ii_ndisk > 0; ii++)
			if (ii->ii_index != NULL)
				g_free(ii->ii_index);
		g_free(sc->sc_itable);
	}
	g_free(sc);
}


static int
ccdinit(struct gctl_req *req, struct ccd_s *cs)
{
	struct ccdcinfo *ci;
	daddr_t size;
	int ix;
	daddr_t minsize;
	int maxsecsize;
	off_t mediasize;
	u_int sectorsize;

	cs->sc_size = 0;

	maxsecsize = 0;
	minsize = 0;

	if (cs->sc_flags & CCDF_LINUX) {
		cs->sc_offset = 0;
		cs->sc_ileave *= 2;
		if (cs->sc_flags & CCDF_MIRROR && cs->sc_ndisks != 2)
			gctl_error(req, "Mirror mode for Linux raids is "
			                "only supported with 2 devices");
	} else {
		if (cs->sc_flags & CCDF_NO_OFFSET)
			cs->sc_offset = 0;
		else
			cs->sc_offset = CCD_OFFSET;

	}
	for (ix = 0; ix < cs->sc_ndisks; ix++) {
		ci = &cs->sc_cinfo[ix];

		mediasize = ci->ci_provider->mediasize;
		sectorsize = ci->ci_provider->sectorsize;
		if (sectorsize > maxsecsize)
			maxsecsize = sectorsize;
		size = mediasize / DEV_BSIZE - cs->sc_offset;

		/* Truncate to interleave boundary */

		if (cs->sc_ileave > 1)
			size -= size % cs->sc_ileave;

		if (size == 0) {
			gctl_error(req, "Component %s has effective size zero",
			    ci->ci_provider->name);
			return(ENODEV);
		}

		if (minsize == 0 || size < minsize)
			minsize = size;
		ci->ci_size = size;
		cs->sc_size += size;
	}

	/*
	 * Don't allow the interleave to be smaller than
	 * the biggest component sector.
	 */
	if ((cs->sc_ileave > 0) &&
	    (cs->sc_ileave < (maxsecsize / DEV_BSIZE))) {
		gctl_error(req, "Interleave to small for sector size");
		return(EINVAL);
	}

	/*
	 * If uniform interleave is desired set all sizes to that of
	 * the smallest component.  This will guarantee that a single
	 * interleave table is generated.
	 *
	 * Lost space must be taken into account when calculating the
	 * overall size.  Half the space is lost when CCDF_MIRROR is
	 * specified.
	 */
	if (cs->sc_flags & CCDF_UNIFORM) {
		for (ix = 0; ix < cs->sc_ndisks; ix++) {
			ci = &cs->sc_cinfo[ix];
			ci->ci_size = minsize;
		}
		cs->sc_size = cs->sc_ndisks * minsize;
	}

	if (cs->sc_flags & CCDF_MIRROR) {
		/*
		 * Check to see if an even number of components
		 * have been specified.  The interleave must also
		 * be non-zero in order for us to be able to 
		 * guarantee the topology.
		 */
		if (cs->sc_ndisks % 2) {
			gctl_error(req,
			      "Mirroring requires an even number of disks");
			return(EINVAL);
		}
		if (cs->sc_ileave == 0) {
			gctl_error(req,
			     "An interleave must be specified when mirroring");
			return(EINVAL);
		}
		cs->sc_size = (cs->sc_ndisks/2) * minsize;
	} 

	/*
	 * Construct the interleave table.
	 */
	ccdinterleave(cs);

	/*
	 * Create pseudo-geometry based on 1MB cylinders.  It's
	 * pretty close.
	 */
	cs->sc_secsize = maxsecsize;

	return (0);
}

static void
ccdinterleave(struct ccd_s *cs)
{
	struct ccdcinfo *ci, *smallci;
	struct ccdiinfo *ii;
	daddr_t bn, lbn;
	int ix;
	daddr_t size;


	/*
	 * Allocate an interleave table.  The worst case occurs when each
	 * of N disks is of a different size, resulting in N interleave
	 * tables.
	 *
	 * Chances are this is too big, but we don't care.
	 */
	size = (cs->sc_ndisks + 1) * sizeof(struct ccdiinfo);
	cs->sc_itable = g_malloc(size, M_WAITOK | M_ZERO);

	/*
	 * Trivial case: no interleave (actually interleave of disk size).
	 * Each table entry represents a single component in its entirety.
	 *
	 * An interleave of 0 may not be used with a mirror setup.
	 */
	if (cs->sc_ileave == 0) {
		bn = 0;
		ii = cs->sc_itable;

		for (ix = 0; ix < cs->sc_ndisks; ix++) {
			/* Allocate space for ii_index. */
			ii->ii_index = g_malloc(sizeof(int), M_WAITOK);
			ii->ii_ndisk = 1;
			ii->ii_startblk = bn;
			ii->ii_startoff = 0;
			ii->ii_index[0] = ix;
			bn += cs->sc_cinfo[ix].ci_size;
			ii++;
		}
		ii->ii_ndisk = 0;
		return;
	}

	/*
	 * The following isn't fast or pretty; it doesn't have to be.
	 */
	size = 0;
	bn = lbn = 0;
	for (ii = cs->sc_itable; ; ii++) {
		/*
		 * Allocate space for ii_index.  We might allocate more then
		 * we use.
		 */
		ii->ii_index = g_malloc((sizeof(int) * cs->sc_ndisks),
		    M_WAITOK);

		/*
		 * Locate the smallest of the remaining components
		 */
		smallci = NULL;
		for (ci = cs->sc_cinfo; ci < &cs->sc_cinfo[cs->sc_ndisks]; 
		    ci++) {
			if (ci->ci_size > size &&
			    (smallci == NULL ||
			     ci->ci_size < smallci->ci_size)) {
				smallci = ci;
			}
		}

		/*
		 * Nobody left, all done
		 */
		if (smallci == NULL) {
			ii->ii_ndisk = 0;
			g_free(ii->ii_index);
			ii->ii_index = NULL;
			break;
		}

		/*
		 * Record starting logical block using an sc_ileave blocksize.
		 */
		ii->ii_startblk = bn / cs->sc_ileave;

		/*
		 * Record starting component block using an sc_ileave 
		 * blocksize.  This value is relative to the beginning of
		 * a component disk.
		 */
		ii->ii_startoff = lbn;

		/*
		 * Determine how many disks take part in this interleave
		 * and record their indices.
		 */
		ix = 0;
		for (ci = cs->sc_cinfo; 
		    ci < &cs->sc_cinfo[cs->sc_ndisks]; ci++) {
			if (ci->ci_size >= smallci->ci_size) {
				ii->ii_index[ix++] = ci - cs->sc_cinfo;
			}
		}
		ii->ii_ndisk = ix;
		bn += ix * (smallci->ci_size - size);
		lbn = smallci->ci_size / cs->sc_ileave;
		size = smallci->ci_size;
	}
}

static void
g_ccd_start(struct bio *bp)
{
	long bcount, rcount;
	struct bio *cbp[2];
	caddr_t addr;
	daddr_t bn;
	int err;
	struct ccd_s *cs;

	cs = bp->bio_to->geom->softc;

	/*
	 * Block all GETATTR requests, we wouldn't know which of our
	 * subdevices we should ship it off to.
	 * XXX: this may not be the right policy.
	 */
	if(bp->bio_cmd == BIO_GETATTR) {
		g_io_deliver(bp, EINVAL);
		return;
	}

	/*
	 * Translate the partition-relative block number to an absolute.
	 */
	bn = bp->bio_offset / cs->sc_secsize;

	/*
	 * Allocate component buffers and fire off the requests
	 */
	addr = bp->bio_data;
	for (bcount = bp->bio_length; bcount > 0; bcount -= rcount) {
		err = ccdbuffer(cbp, cs, bp, bn, addr, bcount);
		if (err) {
			bp->bio_completed += bcount;
			if (bp->bio_error == 0)
				bp->bio_error = err;
			if (bp->bio_completed == bp->bio_length)
				g_io_deliver(bp, bp->bio_error);
			return;
		}
		rcount = cbp[0]->bio_length;

		if (cs->sc_flags & CCDF_MIRROR) {
			/*
			 * Mirroring.  Writes go to both disks, reads are
			 * taken from whichever disk seems most appropriate.
			 *
			 * We attempt to localize reads to the disk whos arm
			 * is nearest the read request.  We ignore seeks due
			 * to writes when making this determination and we
			 * also try to avoid hogging.
			 */
			if (cbp[0]->bio_cmd != BIO_READ) {
				g_io_request(cbp[0], cbp[0]->bio_from);
				g_io_request(cbp[1], cbp[1]->bio_from);
			} else {
				int pick = cs->sc_pick;
				daddr_t range = cs->sc_size / 16;

				if (bn < cs->sc_blk[pick] - range ||
				    bn > cs->sc_blk[pick] + range
				) {
					cs->sc_pick = pick = 1 - pick;
				}
				cs->sc_blk[pick] = bn + btodb(rcount);
				g_io_request(cbp[pick], cbp[pick]->bio_from);
			}
		} else {
			/*
			 * Not mirroring
			 */
			g_io_request(cbp[0], cbp[0]->bio_from);
		}
		bn += btodb(rcount);
		addr += rcount;
	}
}

/*
 * Build a component buffer header.
 */
static int
ccdbuffer(struct bio **cb, struct ccd_s *cs, struct bio *bp, daddr_t bn, caddr_t addr, long bcount)
{
	struct ccdcinfo *ci, *ci2 = NULL;
	struct bio *cbp;
	daddr_t cbn, cboff;
	off_t cbc;

	/*
	 * Determine which component bn falls in.
	 */
	cbn = bn;
	cboff = 0;

	if (cs->sc_ileave == 0) {
		/*
		 * Serially concatenated and neither a mirror nor a parity
		 * config.  This is a special case.
		 */
		daddr_t sblk;

		sblk = 0;
		for (ci = cs->sc_cinfo; cbn >= sblk + ci->ci_size; ci++)
			sblk += ci->ci_size;
		cbn -= sblk;
	} else {
		struct ccdiinfo *ii;
		int ccdisk, off;

		/*
		 * Calculate cbn, the logical superblock (sc_ileave chunks),
		 * and cboff, a normal block offset (DEV_BSIZE chunks) relative
		 * to cbn.
		 */
		cboff = cbn % cs->sc_ileave;	/* DEV_BSIZE gran */
		cbn = cbn / cs->sc_ileave;	/* DEV_BSIZE * ileave gran */

		/*
		 * Figure out which interleave table to use.
		 */
		for (ii = cs->sc_itable; ii->ii_ndisk; ii++) {
			if (ii->ii_startblk > cbn)
				break;
		}
		ii--;

		/*
		 * off is the logical superblock relative to the beginning 
		 * of this interleave block.  
		 */
		off = cbn - ii->ii_startblk;

		/*
		 * We must calculate which disk component to use (ccdisk),
		 * and recalculate cbn to be the superblock relative to
		 * the beginning of the component.  This is typically done by
		 * adding 'off' and ii->ii_startoff together.  However, 'off'
		 * must typically be divided by the number of components in
		 * this interleave array to be properly convert it from a
		 * CCD-relative logical superblock number to a 
		 * component-relative superblock number.
		 */
		if (ii->ii_ndisk == 1) {
			/*
			 * When we have just one disk, it can't be a mirror
			 * or a parity config.
			 */
			ccdisk = ii->ii_index[0];
			cbn = ii->ii_startoff + off;
		} else {
			if (cs->sc_flags & CCDF_MIRROR) {
				/*
				 * We have forced a uniform mapping, resulting
				 * in a single interleave array.  We double
				 * up on the first half of the available
				 * components and our mirror is in the second
				 * half.  This only works with a single 
				 * interleave array because doubling up
				 * doubles the number of sectors, so there
				 * cannot be another interleave array because
				 * the next interleave array's calculations
				 * would be off.
				 */
				int ndisk2 = ii->ii_ndisk / 2;
				ccdisk = ii->ii_index[off % ndisk2];
				cbn = ii->ii_startoff + off / ndisk2;
				ci2 = &cs->sc_cinfo[ccdisk + ndisk2];
			} else {
				ccdisk = ii->ii_index[off % ii->ii_ndisk];
				cbn = ii->ii_startoff + off / ii->ii_ndisk;
			}
		}

		ci = &cs->sc_cinfo[ccdisk];

		/*
		 * Convert cbn from a superblock to a normal block so it
		 * can be used to calculate (along with cboff) the normal
		 * block index into this particular disk.
		 */
		cbn *= cs->sc_ileave;
	}

	/*
	 * Fill in the component buf structure.
	 */
	cbp = g_clone_bio(bp);
	if (cbp == NULL)
		return (ENOMEM);
	cbp->bio_done = g_std_done;
	cbp->bio_offset = dbtob(cbn + cboff + cs->sc_offset);
	cbp->bio_data = addr;
	if (cs->sc_ileave == 0)
              cbc = dbtob((off_t)(ci->ci_size - cbn));
	else
              cbc = dbtob((off_t)(cs->sc_ileave - cboff));
	cbp->bio_length = (cbc < bcount) ? cbc : bcount;

	cbp->bio_from = ci->ci_consumer;
	cb[0] = cbp;

	if (cs->sc_flags & CCDF_MIRROR) {
		cbp = g_clone_bio(bp);
		if (cbp == NULL)
			return (ENOMEM);
		cbp->bio_done = cb[0]->bio_done = ccdiodone;
		cbp->bio_offset = cb[0]->bio_offset;
		cbp->bio_data = cb[0]->bio_data;
		cbp->bio_length = cb[0]->bio_length;
		cbp->bio_from = ci2->ci_consumer;
		cbp->bio_caller1 = cb[0];
		cb[0]->bio_caller1 = cbp;
		cb[1] = cbp;
	}
	return (0);
}

/*
 * Called only for mirrored operations.
 */
static void
ccdiodone(struct bio *cbp)
{
	struct bio *mbp, *pbp;

	mbp = cbp->bio_caller1;
	pbp = cbp->bio_parent;

	if (pbp->bio_cmd == BIO_READ) {
		if (cbp->bio_error == 0) {
			/* We will not be needing the partner bio */
			if (mbp != NULL) {
				pbp->bio_inbed++;
				g_destroy_bio(mbp);
			}
			g_std_done(cbp);
			return;
		}
		if (mbp != NULL) {
			/* Try partner the bio instead */
			mbp->bio_caller1 = NULL;
			pbp->bio_inbed++;
			g_destroy_bio(cbp);
			g_io_request(mbp, mbp->bio_from);
			/*
			 * XXX: If this comes back OK, we should actually
			 * try to write the good data on the failed mirror
			 */
			return;
		}
		g_std_done(cbp);
		return;
	}
	if (mbp != NULL) {
		mbp->bio_caller1 = NULL;
		pbp->bio_inbed++;
		if (cbp->bio_error != 0 && pbp->bio_error == 0)
			pbp->bio_error = cbp->bio_error;
		g_destroy_bio(cbp);
		return;
	}
	g_std_done(cbp);
}

static void
g_ccd_create(struct gctl_req *req, struct g_class *mp)
{
	int *unit, *ileave, *nprovider;
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_provider *pp;
	struct ccd_s *sc;
	struct sbuf *sb;
	char buf[20];
	int i, error;

	g_topology_assert();
	unit = gctl_get_paraml(req, "unit", sizeof (*unit));
	if (unit == NULL) {
		gctl_error(req, "unit parameter not given");
		return;
	}
	ileave = gctl_get_paraml(req, "ileave", sizeof (*ileave));
	if (ileave == NULL) {
		gctl_error(req, "ileave parameter not given");
		return;
	}
	nprovider = gctl_get_paraml(req, "nprovider", sizeof (*nprovider));
	if (nprovider == NULL) {
		gctl_error(req, "nprovider parameter not given");
		return;
	}

	/* Check for duplicate unit */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc != NULL && sc->sc_unit == *unit) {
			gctl_error(req, "Unit %d already configured", *unit);
			return;
		}
	}

	if (*nprovider <= 0) {
		gctl_error(req, "Bogus nprovider argument (= %d)", *nprovider);
		return;
	}

	/* Check all providers are valid */
	for (i = 0; i < *nprovider; i++) {
		sprintf(buf, "provider%d", i);
		pp = gctl_get_provider(req, buf);
		if (pp == NULL)
			return;
	}

	gp = g_new_geomf(mp, "ccd%d", *unit);
	sc = g_malloc(sizeof *sc, M_WAITOK | M_ZERO);
	gp->softc = sc;
	sc->sc_ndisks = *nprovider;

	/* Allocate space for the component info. */
	sc->sc_cinfo = g_malloc(sc->sc_ndisks * sizeof(struct ccdcinfo),
	    M_WAITOK | M_ZERO);

	/* Create consumers and attach to all providers */
	for (i = 0; i < *nprovider; i++) {
		sprintf(buf, "provider%d", i);
		pp = gctl_get_provider(req, buf);
		cp = g_new_consumer(gp);
		error = g_attach(cp, pp);
		KASSERT(error == 0, ("attach to %s failed", pp->name));
		sc->sc_cinfo[i].ci_consumer = cp;
		sc->sc_cinfo[i].ci_provider = pp;
	}

	sc->sc_unit = *unit;
	sc->sc_ileave = *ileave;

	if (gctl_get_param(req, "no_offset", NULL))
		sc->sc_flags |= CCDF_NO_OFFSET;
	if (gctl_get_param(req, "linux", NULL))
		sc->sc_flags |= CCDF_LINUX;

	if (gctl_get_param(req, "uniform", NULL))
		sc->sc_flags |= CCDF_UNIFORM;
	if (gctl_get_param(req, "mirror", NULL))
		sc->sc_flags |= CCDF_MIRROR;

	if (sc->sc_ileave == 0 && (sc->sc_flags & CCDF_MIRROR)) {
		printf("%s: disabling mirror, interleave is 0\n", gp->name);
		sc->sc_flags &= ~(CCDF_MIRROR);
	}

	if ((sc->sc_flags & CCDF_MIRROR) && !(sc->sc_flags & CCDF_UNIFORM)) {
		printf("%s: mirror/parity forces uniform flag\n", gp->name);
		sc->sc_flags |= CCDF_UNIFORM;
	}

	error = ccdinit(req, sc);
	if (error != 0) {
		g_ccd_freesc(sc);
		gp->softc = NULL;
		g_wither_geom(gp, ENXIO);
		return;
	}

	pp = g_new_providerf(gp, "%s", gp->name);
	pp->mediasize = sc->sc_size * (off_t)sc->sc_secsize;
	pp->sectorsize = sc->sc_secsize;
	g_error_provider(pp, 0);

	sb = sbuf_new_auto();
	sbuf_printf(sb, "ccd%d: %d components ", sc->sc_unit, *nprovider);
	for (i = 0; i < *nprovider; i++) {
		sbuf_printf(sb, "%s%s",
		    i == 0 ? "(" : ", ", 
		    sc->sc_cinfo[i].ci_provider->name);
	}
	sbuf_printf(sb, "), %jd blocks ", (off_t)pp->mediasize / DEV_BSIZE);
	if (sc->sc_ileave != 0)
		sbuf_printf(sb, "interleaved at %d blocks\n",
			sc->sc_ileave);
	else
		sbuf_printf(sb, "concatenated\n");
	sbuf_finish(sb);
	gctl_set_param_err(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
	sbuf_delete(sb);
}

static int
g_ccd_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp)
{
	struct g_provider *pp;
	struct ccd_s *sc;

	g_topology_assert();
	sc = gp->softc;
	pp = LIST_FIRST(&gp->provider);
	if (sc == NULL || pp == NULL)
		return (EBUSY);
	if (pp->acr != 0 || pp->acw != 0 || pp->ace != 0) {
		gctl_error(req, "%s is open(r%dw%de%d)", gp->name,
		    pp->acr, pp->acw, pp->ace);
		return (EBUSY);
	}
	g_ccd_freesc(sc);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);
	return (0);
}

static void
g_ccd_list(struct gctl_req *req, struct g_class *mp)
{
	struct sbuf *sb;
	struct ccd_s *cs;
	struct g_geom *gp;
	int i, unit, *up;

	up = gctl_get_paraml(req, "unit", sizeof (*up));
	if (up == NULL) {
		gctl_error(req, "unit parameter not given");
		return;
	}
	unit = *up;
	sb = sbuf_new_auto();
	LIST_FOREACH(gp, &mp->geom, geom) {
		cs = gp->softc;
		if (cs == NULL || (unit >= 0 && unit != cs->sc_unit))
			continue;
		sbuf_printf(sb, "ccd%d\t\t%d\t%d\t",
		    cs->sc_unit, cs->sc_ileave, cs->sc_flags & CCDF_USERMASK);
			
		for (i = 0; i < cs->sc_ndisks; ++i) {
			sbuf_printf(sb, "%s/dev/%s", i == 0 ? "" : " ",
			    cs->sc_cinfo[i].ci_provider->name);
		}
		sbuf_printf(sb, "\n");
	}
	sbuf_finish(sb);
	gctl_set_param_err(req, "output", sbuf_data(sb), sbuf_len(sb) + 1);
	sbuf_delete(sb);
}

static void
g_ccd_config(struct gctl_req *req, struct g_class *mp, char const *verb)
{
	struct g_geom *gp;

	g_topology_assert();
	if (!strcmp(verb, "create geom")) {
		g_ccd_create(req, mp);
	} else if (!strcmp(verb, "destroy geom")) {
		gp = gctl_get_geom(req, mp, "geom");
		if (gp != NULL)
		g_ccd_destroy_geom(req, mp, gp);
	} else if (!strcmp(verb, "list")) {
		g_ccd_list(req, mp);
	} else {
		gctl_error(req, "unknown verb");
	}
}

static struct g_class g_ccd_class = {
	.name = "CCD",
	.version = G_VERSION,
	.ctlreq = g_ccd_config,
	.destroy_geom = g_ccd_destroy_geom,
	.start = g_ccd_start,
	.orphan = g_ccd_orphan,
	.access = g_ccd_access,
};

DECLARE_GEOM_CLASS(g_ccd_class, g_ccd);
MODULE_VERSION(geom_ccd, 0);
