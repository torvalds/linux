/* $OpenBSD: disksubr.c,v 1.66 2025/06/26 20:27:37 miod Exp $ */
/* $NetBSD: disksubr.c,v 1.12 2002/02/19 17:09:44 wiz Exp $ */

/*
 * Copyright (c) 1994, 1995 Gordon W. Ross
 * Copyright (c) 1994 Theo de Raadt
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/disk.h>

#include <dev/sun/disklabel.h>

/*
 * UniOS disklabel (== ISI disklabel) is very similar to SunOS.
 *	SunOS				UniOS/ISI
 *	text		128			128
 *	(pad)		292			294
 *	rpm		2		-
 *	pcyl		2		badchk	2
 *	sparecyl	2		maxblk	4
 *	(pad)		4		dtype	2
 *	interleave	2		ndisk	2
 *	ncyl		2			2
 *	acyl		2			2
 *	ntrack		2			2
 *	nsect		2			2
 *	(pad)		4		bhead	2
 *	-				ppart	2
 *	dkpart[8]	64			64
 *	magic		2			2
 *	cksum		2			2
 *
 * Magic number value and checksum calculation are identical.  Subtle
 * difference is partition start address; UniOS/ISI maintains sector
 * numbers while SunOS label has cylinder number.
 *
 * It is found that LUNA Mach2.5 has BSD label embedded at offset 64
 * retaining UniOS/ISI label at the end of label block.  LUNA Mach
 * manipulates BSD disklabel in the same manner as 4.4BSD.  It's
 * uncertain LUNA Mach can create a disklabel on fresh disks since
 * Mach writedisklabel logic seems to fail when no BSD label is found.
 *
 * Kernel handles disklabel in this way;
 *	- searches BSD label at offset 64
 *	- if not found, searches UniOS/ISI label at the end of block
 *	- kernel can distinguish whether it was SunOS label or UniOS/ISI
 *	  label and understand both
 *	- kernel writes UniOS/ISI label combined with BSD label to update
 *	  the label block
 */

#if LABELSECTOR != 0
#error	"Default value of LABELSECTOR no longer zero?"
#endif

int disklabel_om_to_bsd(dev_t, struct sun_disklabel *, struct disklabel *);
int disklabel_bsd_to_om(struct disklabel *, struct sun_disklabel *);
static __inline u_int sun_extended_sum(struct sun_disklabel *, void *);

/*
 * Attempt to read a disk label from a device
 * using the indicated strategy routine.
 * The label must be partly set up before this:
 * secpercyl, secsize and anything required for a block i/o read
 * operation in the driver's strategy/start routines
 * must be filled in before calling us.
 */
int
readdisklabel(dev_t dev, void (*strat)(struct buf *),
    struct disklabel *lp, int spoofonly)
{
	struct sun_disklabel *slp;
	struct buf *bp = NULL;
	int error;

	if ((error = initdisklabel(lp)))
		goto done;

	/* get a buffer and initialize it */
	bp = geteblk(lp->d_secsize);
	bp->b_dev = dev;

	if (spoofonly)
		goto doslabel;

	error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp, LABELSECTOR));
	if (error)
		goto done;

	slp = (struct sun_disklabel *)bp->b_data;
	if (slp->sl_magic == SUN_DKMAGIC) {
		error = disklabel_om_to_bsd(bp->b_dev, slp, lp);
		goto done;
	}

	error = checkdisklabel(bp->b_dev, bp->b_data + LABELOFFSET, lp, 0,
	    DL_GETDSIZE(lp));
	if (error == 0)
		goto done;

 doslabel:
	error = readdoslabel(bp, strat, lp, NULL, spoofonly);
	if (error == 0)
		goto done;

#if defined(CD9660)
	error = iso_disklabelspoof(dev, strat, lp);
	if (error == 0)
		goto done;
#endif
#if defined(UDF)
	error = udf_disklabelspoof(dev, strat, lp);
	if (error == 0)
		goto done;
#endif

done:
	if (bp) {
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	disk_change = 1;
	return (error);
}

/*
 * Write disk label back to device after modification.
 */
int
writedisklabel(dev_t dev, void (*strat)(struct buf *), struct disklabel *lp)
{
	struct buf *bp = NULL;
	int error;

	/* get a buffer and initialize it */
	bp = geteblk(lp->d_secsize);
	bp->b_dev = dev;

	error = disklabel_bsd_to_om(lp, (struct sun_disklabel *)bp->b_data);
	if (error)
		goto done;

	/* Write out the updated label. */
	bp->b_blkno = LABELSECTOR;
	bp->b_bcount = lp->d_secsize;
	CLR(bp->b_flags, B_READ | B_WRITE | B_DONE);
	SET(bp->b_flags, B_BUSY | B_WRITE | B_RAW);
	(*strat)(bp);
	error = biowait(bp);

done:
	if (bp) {
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	disk_change = 1;
	return (error);
}

/************************************************************************
 *
 * The rest of this was taken from arch/sparc/scsi/sun_disklabel.c
 * and then substantially rewritten by Gordon W. Ross
 *
 ************************************************************************/

/* What partition types to assume for Sun disklabels: */
static const u_char
sun_fstypes[16] = {
	FS_BSDFFS,	/* a */
	FS_SWAP,	/* b */
	FS_UNUSED,	/* c - whole disk */
	FS_BSDFFS,	/* d */
	FS_BSDFFS,	/* e */
	FS_BSDFFS,	/* f */
	FS_BSDFFS,	/* g */
	FS_BSDFFS,	/* h */
	FS_BSDFFS,	/* i */
	FS_BSDFFS,	/* j */
	FS_BSDFFS,	/* k */
	FS_BSDFFS,	/* l */
	FS_BSDFFS,	/* m */
	FS_BSDFFS,	/* n */
	FS_BSDFFS,	/* o */
	FS_BSDFFS	/* p */
};

/*
 * Given a struct sun_disklabel, assume it has an extended partition
 * table and compute the correct value for sl_xpsum.
 */
static __inline u_int
sun_extended_sum(struct sun_disklabel *sl, void *end)
{
	u_int sum, *xp, *ep;

	xp = (u_int *)&sl->sl_xpmag;
	ep = (u_int *)end;

	sum = 0;
	for (; xp < ep; xp++)
		sum += *xp;
	return (sum);
}

/*
 * Given a UniOS/ISI disk label, set lp to a BSD disk label.
 *
 * The BSD label is cleared out before this is called.
 */
int
disklabel_om_to_bsd(dev_t dev, struct sun_disklabel *sl, struct disklabel *lp)
{
	struct partition *npp;
	struct sun_dkpart *spp;
	int i, secpercyl;
	u_short cksum = 0, *sp1, *sp2;

	/* Verify the XOR check. */
	sp1 = (u_short *)sl;
	sp2 = (u_short *)(sl + 1);
	while (sp1 < sp2)
		cksum ^= *sp1++;
	if (cksum != 0)
		return (EINVAL);	/* UniOS disk label, bad checksum */

	/* Format conversion. */
	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	memcpy(lp->d_packname, sl->sl_text, sizeof(lp->d_packname));

	lp->d_secsize = DEV_BSIZE;
	lp->d_nsectors = sl->sl_nsectors;
	lp->d_ntracks = sl->sl_ntracks;
	lp->d_ncylinders = sl->sl_ncylinders;

	secpercyl = sl->sl_nsectors * sl->sl_ntracks;
	lp->d_secpercyl = secpercyl;
	/* If unset or initialized as full disk, permit refinement */
	if (DL_GETDSIZE(lp) == 0 || DL_GETDSIZE(lp) == MAXDISKSIZE)
		DL_SETDSIZE(lp, (u_int64_t)secpercyl * sl->sl_ncylinders);
	lp->d_version = 1;

	memcpy(&lp->d_uid, &sl->sl_uid, sizeof(sl->sl_uid));

	lp->d_acylinders = sl->sl_acylinders;

	lp->d_npartitions = MAXPARTITIONS;

	for (i = 0; i < 8; i++) {
		spp = &sl->sl_part[i];
		npp = &lp->d_partitions[i];
		/* UniOS label uses blkoffset, not cyloffset */
		DL_SETPOFFSET(npp, spp->sdkp_cyloffset);
		DL_SETPSIZE(npp, spp->sdkp_nsectors);
		if (DL_GETPSIZE(npp) == 0) {
			npp->p_fstype = FS_UNUSED;
		} else {
			npp->p_fstype = sun_fstypes[i];
			if (npp->p_fstype == FS_BSDFFS) {
				/*
				 * The sun label does not store the FFS fields,
				 * so just set them with default values here.
				 */
				npp->p_fragblock =
				    DISKLABELV1_FFS_FRAGBLOCK(2048, 8);
				npp->p_cpg = 16;
			}
		}
	}

	/*
	 * XXX BandAid XXX
	 * UniOS rootfs sits on part c which don't begin at sect 0,
	 * and impossible to mount.  Thus, make it usable as part b.
	 * XXX how to setup a swap partition on disks shared with UniOS???
	 */
	if (sl->sl_rpm == 0 && DL_GETPOFFSET(&lp->d_partitions[2]) != 0) {
		lp->d_partitions[1] = lp->d_partitions[2];
		lp->d_partitions[1].p_fstype = FS_BSDFFS;
	}

	/* Clear "extended" partition info, tentatively */
	for (i = 0; i < SUNXPART; i++) {
		npp = &lp->d_partitions[i+8];
		DL_SETPOFFSET(npp, 0);
		DL_SETPSIZE(npp, 0);
		npp->p_fstype = FS_UNUSED;
	}

	/* Check to see if there's an "extended" partition table
	 * SL_XPMAG partitions had checksums up to just before the
	 * (new) sl_types variable, while SL_XPMAGTYP partitions have
	 * checksums up to the just before the (new) sl_xxx1 variable.
	 */
	if ((sl->sl_xpmag == SL_XPMAG &&
	    sun_extended_sum(sl, &sl->sl_types) == sl->sl_xpsum) ||
	    (sl->sl_xpmag == SL_XPMAGTYP &&
	    sun_extended_sum(sl, &sl->sl_xxx1) == sl->sl_xpsum)) {
		/*
		 * There is.  Copy over the "extended" partitions.
		 * This code parallels the loop for partitions a-h.
		 */
		for (i = 0; i < SUNXPART; i++) {
			spp = &sl->sl_xpart[i];
			npp = &lp->d_partitions[i+8];
			DL_SETPOFFSET(npp, spp->sdkp_cyloffset);
			DL_SETPSIZE(npp, spp->sdkp_nsectors);
			if (DL_GETPSIZE(npp) == 0) {
				npp->p_fstype = FS_UNUSED;
				continue;
			}
			npp->p_fstype = sun_fstypes[i+8];
			if (npp->p_fstype == FS_BSDFFS) {
				npp->p_fragblock =
				    DISKLABELV1_FFS_FRAGBLOCK(2048, 8);
				npp->p_cpg = 16;
			}
		}
		if (sl->sl_xpmag == SL_XPMAGTYP) {
			for (i = 0; i < MAXPARTITIONS; i++) {
				npp = &lp->d_partitions[i];
				npp->p_fstype = sl->sl_types[i];
				npp->p_fragblock = sl->sl_fragblock[i];
				npp->p_cpg = sl->sl_cpg[i];
			}
		}
	}

	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);
	return (checkdisklabel(dev, lp, lp, 0, DL_GETDSIZE(lp)));
}

/*
 * Given a BSD disk label, update the UniOS disklabel
 * pointed to by sl with the new info.  Note that the
 * UniOS disklabel may have other info we need to keep.
 */
int
disklabel_bsd_to_om(struct disklabel *lp, struct sun_disklabel *sl)
{
	struct partition *npp;
	struct sun_dkpart *spp;
	int i;
	u_short cksum, *sp1, *sp2;

	if (lp->d_secsize != DEV_BSIZE || lp->d_nsectors == 0 ||
	    lp->d_ntracks == 0)
		return (EINVAL);

	/* Format conversion. */
	bzero(sl, sizeof(*sl));
	memcpy(sl->sl_text, lp->d_packname, sizeof(lp->d_packname));
	sl->sl_rpm = 0;					/* UniOS compatible */
#if 0 /* leave as was */
	sl->sl_pcylinders = lp->d_ncylinders + lp->d_acylinders; /* XXX */
#endif
	sl->sl_interleave = 1;
	sl->sl_ncylinders = lp->d_ncylinders;
	sl->sl_acylinders = lp->d_acylinders;
	sl->sl_ntracks = lp->d_ntracks;
	sl->sl_nsectors = lp->d_nsectors;

	memcpy(&sl->sl_uid, &lp->d_uid, sizeof(lp->d_uid));

	for (i = 0; i < 8; i++) {
		spp = &sl->sl_part[i];
		npp = &lp->d_partitions[i];
		spp->sdkp_cyloffset = 0;
		spp->sdkp_nsectors = 0;
		if (DL_GETPSIZE(npp)) {
			spp->sdkp_cyloffset = DL_GETPOFFSET(npp); /* UniOS */
			spp->sdkp_nsectors = DL_GETPSIZE(npp);
		}
	}
	sl->sl_magic = SUN_DKMAGIC;

	for (i = 0; i < SUNXPART; i++) {
		spp = &sl->sl_xpart[i];
		npp = &lp->d_partitions[i+8];
		spp->sdkp_cyloffset = 0;
		spp->sdkp_nsectors = 0;
		if (DL_GETPSIZE(npp)) {
			spp->sdkp_cyloffset = DL_GETPOFFSET(npp);
			spp->sdkp_nsectors = DL_GETPSIZE(npp);
		}
	}
	for (i = 0; i < MAXPARTITIONS; i++) {
		npp = &lp->d_partitions[i];
		sl->sl_types[i] = npp->p_fstype;
		sl->sl_fragblock[i] = npp->p_fragblock;
		sl->sl_cpg[i] = npp->p_cpg;
	}
	sl->sl_xpmag = SL_XPMAGTYP;
	sl->sl_xpsum = sun_extended_sum(sl, &sl->sl_xxx1);

	/* Correct the XOR check. */
	sp1 = (u_short *)sl;
	sp2 = (u_short *)(sl + 1);
	sl->sl_cksum = cksum = 0;
	while (sp1 < sp2)
		cksum ^= *sp1++;
	sl->sl_cksum = cksum;

	return (0);
}
