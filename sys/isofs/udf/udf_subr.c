/*	$OpenBSD: udf_subr.c,v 1.27 2024/04/13 23:44:11 jsg Exp $	*/

/*
 * Copyright (c) 2006, Miodrag Vallat
 * Copyright (c) 2006, Pedro Martelletto
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/dirent.h>
#include <sys/disklabel.h>

#include <crypto/siphash.h>

#include <isofs/udf/ecma167-udf.h>
#include <isofs/udf/udf.h>
#include <isofs/udf/udf_extern.h>

int udf_vat_read(struct umount *, uint32_t *);

/*
 * Convert a CS0 dstring to a 16-bit Unicode string.
 * Returns the length of the Unicode string, in unicode characters (not
 * bytes!), or -1 if an error arises.
 * Note that the transname destination buffer is expected to be large
 * enough to hold the result, and will not be terminated in any way.
 */
int
udf_rawnametounicode(u_int len, char *cs0string, unicode_t *transname)
{
	unicode_t *origname = transname;

	if (len-- == 0)
		return (-1);

	switch (*cs0string++) {
	case 8:		/* bytes string */
		while (len-- != 0)
			*transname++ = (unicode_t)*cs0string++;
		break;
	case 16:	/* 16 bit unicode string */
		if (len & 1)
			return (-1);
		len >>= 1;
		while (len-- != 0) {
			unicode_t tmpchar;

			tmpchar = (unicode_t)*cs0string++;
			tmpchar = (tmpchar << 8) | (unicode_t)*cs0string++;
			*transname++ = tmpchar;
		}
		break;
	default:
		return (-1);
	}

	return (transname - origname);
}

/*
 * Do a lazy probe on the underlying media to check if it's a UDF volume, in
 * which case we fake a disk label for it.
 */
int
udf_disklabelspoof(dev_t dev, void (*strat)(struct buf *),
    struct disklabel *lp)
{
	char vid[32];
	int i, bsize = 2048, error = EINVAL;
	uint32_t sector = 256, mvds_start, mvds_end;
	struct buf *bp;
	struct anchor_vdp avdp;
	struct pri_vol_desc *pvd;

	/*
	 * Get a buffer to work with.
	 */
	bp = geteblk(bsize);
	bp->b_dev = dev;

	/*
	 * Look for an Anchor Volume Descriptor at sector 256.
	 */
	bp->b_blkno = sector * btodb(bsize);
	bp->b_bcount = bsize;
	CLR(bp->b_flags, B_READ | B_WRITE | B_DONE);
	SET(bp->b_flags, B_BUSY | B_READ | B_RAW);
	bp->b_resid = bp->b_blkno / lp->d_secpercyl;

	(*strat)(bp);
	if (biowait(bp))
		goto out;

	if (udf_checktag((struct desc_tag *)bp->b_data, TAGID_ANCHOR))
		goto out;

	bcopy(bp->b_data, &avdp, sizeof(avdp));
	mvds_start = letoh32(avdp.main_vds_ex.loc);
	mvds_end = mvds_start + (letoh32(avdp.main_vds_ex.len) - 1) / bsize;

	/*
	 * Then try to find a reference to a Primary Volume Descriptor.
	 */
	for (sector = mvds_start; sector < mvds_end; sector++) {
		bp->b_blkno = sector * btodb(bsize);
		bp->b_bcount = bsize;
		CLR(bp->b_flags, B_READ | B_WRITE | B_DONE);
		SET(bp->b_flags, B_BUSY | B_READ | B_RAW);
		bp->b_resid = bp->b_blkno / lp->d_secpercyl;

		(*strat)(bp);
		if (biowait(bp))
			goto out;

		pvd = (struct pri_vol_desc *)bp->b_data;
		if (!udf_checktag(&pvd->tag, TAGID_PRI_VOL))
			break;
	}

	/*
	 * If we couldn't find a reference, bail out.
	 */
	if (sector == mvds_end)
		goto out;

	/*
	 * Okay, it's a UDF volume. Spoof a disk label for it.
	 */
	if (udf_transname(pvd->vol_id, vid, sizeof(pvd->vol_id) - 1, NULL))
		strlcpy(lp->d_typename, vid, sizeof(lp->d_typename));

	for (i = 0; i < MAXPARTITIONS; i++) {
		DL_SETPSIZE(&lp->d_partitions[i], 0);
		DL_SETPOFFSET(&lp->d_partitions[i], 0);
	}

	/*
	 * Fake two partitions, 'a' and 'c'.
	 */
	DL_SETPSIZE(&lp->d_partitions[0], DL_GETDSIZE(lp));
	lp->d_partitions[0].p_fstype = FS_UDF;
	DL_SETPSIZE(&lp->d_partitions[RAW_PART], DL_GETDSIZE(lp));
	lp->d_partitions[RAW_PART].p_fstype = FS_UDF;
	lp->d_npartitions = MAXPARTITIONS;
	lp->d_version = 1;

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = dkcksum(lp);

	error = 0;
out:
	bp->b_flags |= B_INVAL;
	brelse(bp);

	return (error);
}

/* Get a vnode for the Virtual Allocation Table (VAT) */
int
udf_vat_get(struct umount *ump, uint32_t lb)
{
	struct vnode *vp;
	struct unode *up;
	int error;

	error = udf_vget(ump->um_mountp, lb - ump->um_start - 3, &vp);
	if (error)
		return (error);

	up = VTOU(vp);
	up->u_vatlen = (letoh64(up->u_fentry->inf_len) - 36) >> 2;

	ump->um_vat = malloc(sizeof(struct unode), M_UDFMOUNT, M_WAITOK);
	*ump->um_vat = *up;

	ump->um_flags &= ~UDF_MNT_FIND_VAT;
	ump->um_flags |=  UDF_MNT_USES_VAT;

	vput(vp);

	return (0);
}

/* Look up a sector in the VAT */
int
udf_vat_map(struct umount *ump, uint32_t *sector)
{
	/* If there's no VAT, then it's easy */
	if (!(ump->um_flags & UDF_MNT_USES_VAT)) {
		*sector += ump->um_start;
		return (0);
	}

	/* Sanity check the given sector */
	if (*sector >= ump->um_vat->u_vatlen)
		return (EINVAL);

	return (udf_vat_read(ump, sector));
}

/* Read from the VAT */
int
udf_vat_read(struct umount *ump, uint32_t *sector)
{
	struct buf *bp;
	uint8_t *data;
	int error, size;

	size = 4;

	/*
	 * Note that we rely on the buffer cache to keep frequently accessed
	 * buffers around to avoid reading them from the disk all the time.
	 */
	error = udf_readatoffset(ump->um_vat, &size, *sector << 2, &bp, &data);
	if (error) {
		if (bp != NULL)
			brelse(bp);

		return (error);
	}

	/* Make sure we read at least a whole entry */
	if (size < 4) {
		if (bp != NULL)
			brelse(bp);

		return (EINVAL);
	}

	/* Map the sector */
	*sector = letoh32(*(uint32_t *)data) + ump->um_start;

	brelse(bp);

	return (0);
}
