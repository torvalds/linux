/*	$OpenBSD: disksubr.c,v 1.91 2022/10/11 23:39:07 krw Exp $	*/

/*
 * Copyright (c) 1999 Michael Shalayeff
 * Copyright (c) 1997 Niklas Hallqvist
 * Copyright (c) 1996 Theo de Raadt
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/disk.h>

int	readliflabel(struct buf *, void (*)(struct buf *),
	    struct disklabel *, daddr_t *, int);

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
	struct buf *bp = NULL;
	int error;

	if ((error = initdisklabel(lp)))
		goto done;

	/* get a buffer and initialize it */
	bp = geteblk(lp->d_secsize);
	bp->b_dev = dev;

	error = readliflabel(bp, strat, lp, NULL, spoofonly);
	if (error == 0)
		goto done;

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

int
readliflabel(struct buf *bp, void (*strat)(struct buf *),
    struct disklabel *lp, daddr_t *partoffp, int spoofonly)
{
	struct lifdir *p;
	struct lifvol *lvp;
	int error = 0;
	daddr_t fsoff = 0, openbsdstart = MAXLIFSPACE;
	int i;

	/* read LIF volume header */
	error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp,
	    btodb(LIF_VOLSTART)));
	if (error)
		return (error);

	lvp = (struct lifvol *)bp->b_data;
	if (lvp->vol_id != LIF_VOL_ID) {
		error = EINVAL;		/* no LIF volume header */
		goto done;
	}

	/* read LIF directory */
	error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp,
	    lifstodb(lvp->vol_addr)));
	if (error)
		goto done;

	/* scan for LIF_DIR_FS dir entry */
	for (i=0, p=(struct lifdir *)bp->b_data; i < LIF_NUMDIR; p++, i++) {
		if (p->dir_type == LIF_DIR_FS || p->dir_type == LIF_DIR_HPLBL)
			break;
	}

	if (p->dir_type == LIF_DIR_FS) {
		fsoff = lifstodb(p->dir_addr);
		openbsdstart = 0;
		goto finished;
	}

	/* Only came here to find the offset... */
	if (partoffp)
		goto finished;

	if (p->dir_type == LIF_DIR_HPLBL) {
		struct hpux_label *hl;
		struct partition *pp;
		u_int8_t fstype;
		int i;

		/* read LIF directory */
		error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp,
		    lifstodb(p->dir_addr)));
		if (error)
			goto done;

		hl = (struct hpux_label *)bp->b_data;
		if (hl->hl_magic1 != hl->hl_magic2 ||
		    hl->hl_magic != HPUX_MAGIC || hl->hl_version != 1) {
			error = EINVAL;	 /* HPUX label magic mismatch */
			goto done;
		}

		for (i = 0; i < MAXPARTITIONS; i++) {
			DL_SETPSIZE(&lp->d_partitions[i], 0);
			DL_SETPOFFSET(&lp->d_partitions[i], 0);
			lp->d_partitions[i].p_fstype = 0;
		}

		for (i = 0; i < HPUX_MAXPART; i++) {
			if (!hl->hl_flags[i])
				continue;
			if (hl->hl_flags[i] == HPUX_PART_ROOT) {
				pp = &lp->d_partitions[0];
				fstype = FS_BSDFFS;
			} else if (hl->hl_flags[i] == HPUX_PART_SWAP) {
				pp = &lp->d_partitions[1];
				fstype = FS_SWAP;
			} else if (hl->hl_flags[i] == HPUX_PART_BOOT) {
				pp = &lp->d_partitions[RAW_PART + 1];
				fstype = FS_BSDFFS;
			} else
				continue;

			DL_SETPSIZE(pp, hl->hl_parts[i].hlp_length * 2);
			DL_SETPOFFSET(pp, hl->hl_parts[i].hlp_start * 2);
			pp->p_fstype = fstype;
		}

		DL_SETPSIZE(&lp->d_partitions[RAW_PART], DL_GETDSIZE(lp));
		DL_SETPOFFSET(&lp->d_partitions[RAW_PART], 0);
		lp->d_partitions[RAW_PART].p_fstype = FS_UNUSED;
		lp->d_npartitions = MAXPARTITIONS;
		lp->d_magic = DISKMAGIC;
		lp->d_magic2 = DISKMAGIC;
		lp->d_version = 1;
		lp->d_checksum = 0;
		lp->d_checksum = dkcksum(lp);
		/* drop through */
	}

finished:
	/* record the OpenBSD partition's placement for the caller */
	if (partoffp)
		*partoffp = fsoff;
	else {
		DL_SETBSTART(lp, DL_BLKTOSEC(lp, openbsdstart));
		DL_SETBEND(lp, DL_GETDSIZE(lp));	/* XXX */
	}

	/* don't read the on-disk label if we are in spoofed-only mode */
	if (spoofonly)
		goto done;

	error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp, fsoff +
	    LABELSECTOR));
	if (error)
		goto done;

	error = checkdisklabel(bp->b_dev, bp->b_data, lp, openbsdstart,
	    DL_GETDSIZE(lp));

done:
	return (error);
}

/*
 * Write disk label back to device after modification.
 */
int
writedisklabel(dev_t dev, void (*strat)(struct buf *), struct disklabel *lp)
{
	daddr_t partoff = -1;
	int error = EIO;
	int offset;
	struct disklabel *dlp;
	struct buf *bp = NULL;

	/* get a buffer and initialize it */
	bp = geteblk(lp->d_secsize);
	bp->b_dev = dev;

	/* Read it in, slap the new label in, and write it back out */
	if (readliflabel(bp, strat, lp, &partoff, 1) == 0) {
		error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp,
		    partoff + LABELSECTOR));
		offset = LABELOFFSET;
	} else if (readdoslabel(bp, strat, lp, &partoff, 1) == 0) {
		error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp,
		    partoff + DOS_LABELSECTOR));
		offset = DL_BLKOFFSET(lp, partoff + DOS_LABELSECTOR);
	} else
		goto done;

	if (error)
		goto done;

	dlp = (struct disklabel *)(bp->b_data + offset);
	*dlp = *lp;
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
