/*	$OpenBSD: ufs_disksubr.c,v 1.7 2023/04/10 04:21:20 jsg Exp $	*/
/*	$NetBSD: ufs_disksubr.c,v 1.2 2013/01/14 01:37:57 tsutsui Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
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
 *
 *	@(#)ufs_disksubr.c	8.1 (Berkeley) 6/10/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
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
 *	@(#)ufs_disksubr.c	8.1 (Berkeley) 6/10/93
 */

/*
 * ufs_disksubr.c -- disk utility routines
 * by A.Fujita, FEB-26-1992
 */

#include <sys/param.h>
#include <sys/disklabel.h>
#include <dev/sun/disklabel.h>
#include <luna88k/stand/boot/samachdep.h>
#include <luna88k/stand/boot/scsireg.h>

#define	BBSIZE 8192
#define	LABEL_SIZE BBSIZE
u_char lbl_buff[LABEL_SIZE];

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
 * Attempt to read a disk label from a device
 * using the indicated strategy routine.
 * The label must be partly set up before this:
 * secpercyl and anything required in the strategy routine
 * (e.g., sector size) must be filled in before calling us.
 * Returns null on success and an error string on failure.
 */
char *
readdisklabel(struct scsi_softc *sc, uint tgt, struct disklabel *lp)
{
	u_char *bp = lbl_buff;
	struct sun_disklabel *slp;
	struct partition *npp;
	struct sun_dkpart *spp;
	u_short cksum = 0, *sp1, *sp2;
	int i, secpercyl;
	static struct scsi_generic_cdb cdb = {
		6,
		{ CMD_READ, 0, 0, 0, 1, 0 }
	};

	if (DL_GETDSIZE(lp) == 0)
		DL_SETDSIZE(lp, 0x1fffffff);
	lp->d_npartitions = 1;
	if (DL_GETPSIZE(&lp->d_partitions[0]) == 0)
		DL_SETPSIZE(&lp->d_partitions[0], 0x1fffffff);
	DL_SETPSIZE(&lp->d_partitions[0], 0);

	if (scsi_immed_command(sc, tgt, 0, &cdb, bp, DEV_BSIZE) != 0)
		return "I/O error";

	slp = (struct sun_disklabel *)bp;
	if (slp->sl_magic != SUN_DKMAGIC)
		return "no disk label";

	sp1 = (u_short *)slp;
	sp2 = (u_short *)(slp + 1);
	while (sp1 < sp2)
		cksum ^= *sp1++;
	if (cksum != 0)
		return "disk label corrupted";

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	memcpy(lp->d_packname, slp->sl_text, sizeof(lp->d_packname));
	lp->d_nsectors = slp->sl_nsectors;
	lp->d_ntracks = slp->sl_ntracks;
	lp->d_ncylinders = slp->sl_ncylinders;

	secpercyl = slp->sl_nsectors * slp->sl_ntracks;
	lp->d_secpercyl = secpercyl;
	if (DL_GETDSIZE(lp) == 0)
		DL_SETDSIZE(lp, (u_int64_t)secpercyl * slp->sl_ncylinders);
	lp->d_version = 1;

	memcpy(&lp->d_uid, &slp->sl_uid, sizeof(slp->sl_uid));

	lp->d_acylinders = slp->sl_acylinders;

	lp->d_npartitions = MAXPARTITIONS;

	for (i = 0; i < 8; i++) {
		spp = &slp->sl_part[i];
		npp = &lp->d_partitions[i];
		/* UniOS label uses blkoffset, not cyloffset */
		DL_SETPOFFSET(npp, spp->sdkp_cyloffset);
		DL_SETPSIZE(npp, spp->sdkp_nsectors);
		if (DL_GETPSIZE(npp) == 0) {
			npp->p_fstype = FS_UNUSED;
		} else {
			npp->p_fstype = i == 2 ? FS_UNUSED :
			    i == 1 ? FS_SWAP : FS_BSDFFS;
			if (npp->p_fstype == FS_BSDFFS) {
				/*
				 * The sun label does not store the FFS fields,
				 * so just set them with default values here.
				 */
				npp->p_fragblock = 8 | 3
				    /* DISKLABELV1_FFS_FRAGBLOCK(2048, 8); */ ;
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
	if (slp->sl_rpm == 0 && DL_GETPOFFSET(&lp->d_partitions[2]) != 0) {
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
	if ((slp->sl_xpmag == SL_XPMAG &&
	    sun_extended_sum(slp, &slp->sl_types) == slp->sl_xpsum) ||
	    (slp->sl_xpmag == SL_XPMAGTYP &&
	    sun_extended_sum(slp, &slp->sl_xxx1) == slp->sl_xpsum)) {
		/*
		 * There is.  Copy over the "extended" partitions.
		 * This code parallels the loop for partitions a-h.
		 */
		for (i = 0; i < SUNXPART; i++) {
			spp = &slp->sl_xpart[i];
			npp = &lp->d_partitions[i+8];
			DL_SETPOFFSET(npp, spp->sdkp_cyloffset);
			DL_SETPSIZE(npp, spp->sdkp_nsectors);
			if (DL_GETPSIZE(npp) == 0) {
				npp->p_fstype = FS_UNUSED;
				continue;
			}
			npp->p_fstype = FS_BSDFFS;
			if (npp->p_fstype == FS_BSDFFS) {
				npp->p_fragblock = 8 | 3
				    /* DISKLABELV1_FFS_FRAGBLOCK(2048, 8); */ ;
				npp->p_cpg = 16;
			}
		}
		if (slp->sl_xpmag == SL_XPMAGTYP) {
			for (i = 0; i < MAXPARTITIONS; i++) {
				npp = &lp->d_partitions[i];
				npp->p_fstype = slp->sl_types[i];
				npp->p_fragblock = slp->sl_fragblock[i];
				npp->p_cpg = slp->sl_cpg[i];
			}
		}
	}

	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);

	return NULL;
}
