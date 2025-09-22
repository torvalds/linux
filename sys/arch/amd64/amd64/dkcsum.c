/*	$OpenBSD: dkcsum.c,v 1.21 2014/09/14 14:17:23 jsg Exp $	*/

/*-
 * Copyright (c) 1997 Niklas Hallqvist.  All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * A checksumming pseudo device used to get unique labels of each disk
 * that needs to be matched to BIOS disks.
 */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/fcntl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/systm.h>

#include <machine/biosvar.h>

#include <lib/libz/zlib.h>

dev_t dev_rawpart(struct device *);	/* XXX */

extern u_int32_t bios_cksumlen;
extern bios_diskinfo_t *bios_diskinfo;
extern dev_t bootdev;

void
dkcsumattach(void)
{
	struct device *dv;
	struct buf *bp;
	struct bdevsw *bdsw;
	dev_t dev, pribootdev, altbootdev;
	int error, picked;
	u_int32_t csum;
	bios_diskinfo_t *bdi, *hit;

	/* do nothing if no diskinfo passed from /boot, or a bad length */
	if (bios_diskinfo == NULL || bios_cksumlen * DEV_BSIZE > MAXBSIZE)
		return;

	/* Do nothing if bootdev is a CD drive. */
	if (B_TYPE(bootdev) == 6)
		return;

#ifdef DEBUG
	printf("dkcsum: bootdev=%#x\n", bootdev);
	for (bdi = bios_diskinfo; bdi->bios_number != -1; bdi++) {
		if (bdi->bios_number & 0x80) {
			printf("dkcsum: BIOS drive %#x bsd_dev=%#x "
			    "checksum=%#x\n", bdi->bios_number, bdi->bsd_dev,
			    bdi->checksum);
		}
	}
#endif
	pribootdev = altbootdev = 0;

	/*
	 * XXX What if DEV_BSIZE is changed to something else than the BIOS
	 * blocksize?  Today, /boot doesn't cover that case so neither need
	 * I care here.
	 */
	bp = geteblk(bios_cksumlen * DEV_BSIZE);	/* XXX error check?  */

	TAILQ_FOREACH(dv, &alldevs, dv_list) {
		if (dv->dv_class != DV_DISK)
			continue;
		bp->b_dev = dev = dev_rawpart(dv);
		if (dev == NODEV)
			continue;
		bdsw = &bdevsw[major(dev)];

		/*
		 * This open operation guarantees a proper initialization
		 * of the device, for future strategy calls.
		 */
		error = (*bdsw->d_open)(dev, FREAD, S_IFCHR, curproc);
		if (error) {
			/* XXX What to do here? */
#ifdef DEBUG
			printf("dkcsum: %s open failed (%d)\n",
			    dv->dv_xname, error);
#endif
			continue;
		}

		/* Read blocks to cksum.  XXX maybe a d_read should be used. */
		bp->b_blkno = 0;
		bp->b_bcount = bios_cksumlen * DEV_BSIZE;
		bp->b_error = 0; /* B_ERROR and b_error may have stale data. */
		CLR(bp->b_flags, B_READ | B_WRITE | B_DONE | B_ERROR);
		SET(bp->b_flags, B_BUSY | B_READ | B_RAW);
		(*bdsw->d_strategy)(bp);
		if ((error = biowait(bp))) {
			/* XXX What to do here? */
#ifdef DEBUG
			printf("dkcsum: %s read failed (%d)\n",
			    dv->dv_xname, error);
#endif
			error = (*bdsw->d_close)(dev, 0, S_IFCHR, curproc);
#ifdef DEBUG
			if (error)
				printf("dkcsum: %s close failed (%d)\n",
				    dv->dv_xname, error);
#endif
			continue;
		}
		error = (*bdsw->d_close)(dev, FREAD, S_IFCHR, curproc);
		if (error) {
			/* XXX What to do here? */
#ifdef DEBUG
			printf("dkcsum: %s closed failed (%d)\n",
			    dv->dv_xname, error);
#endif
			continue;
		}

		csum = adler32(0, bp->b_data, bios_cksumlen * DEV_BSIZE);
#ifdef DEBUG
		printf("dkcsum: %s checksum is %#x\n", dv->dv_xname, csum);
#endif

		/* Find the BIOS device */
		hit = 0;
		for (bdi = bios_diskinfo; bdi->bios_number != -1; bdi++) {
			/* Skip non-harddrives */
			if (!(bdi->bios_number & 0x80))
				continue;
			if (bdi->checksum != csum)
				continue;
			picked = hit || (bdi->flags & BDI_PICKED);
			if (!picked)
				hit = bdi;
#ifdef DEBUG
			printf("dkcsum: %s matches BIOS drive %#x%s\n",
			    dv->dv_xname, bdi->bios_number,
			    (picked ? " IGNORED" : ""));
#endif
		}

		/*
		 * If we have no hit, that's OK, we can see a lot more devices
		 * than the BIOS can, so this case is pretty normal.
		 */
		if (!hit) {
#ifdef DEBUG
			printf("dkcsum: %s has no matching BIOS drive\n",
			    dv->dv_xname);
#endif	
			continue;
		}

		/*
		 * Fixup bootdev if units match.  This means that all of
		 * hd*, sd*, wd*, will be interpreted the same.  Not 100%
		 * backwards compatible, but sd* and wd* should be phased-
		 * out in the bootblocks.
		 */

		/* B_TYPE dependent hd unit counting bootblocks */
		if ((B_ADAPTOR(bootdev) == B_ADAPTOR(hit->bsd_dev)) &&
		    (B_CONTROLLER(bootdev) == B_CONTROLLER(hit->bsd_dev)) &&
		    (B_TYPE(bootdev) == B_TYPE(hit->bsd_dev)) &&
		    (B_UNIT(bootdev) == B_UNIT(hit->bsd_dev))) {
			int type, ctrl, adap, part, unit;

			type = major(bp->b_dev);
			adap = B_ADAPTOR(bootdev);
			ctrl = B_CONTROLLER(bootdev);
			unit = DISKUNIT(bp->b_dev);
			part = B_PARTITION(bootdev);

			pribootdev = MAKEBOOTDEV(type, ctrl, adap, unit, part);
#ifdef DEBUG
			printf("dkcsum: %s is primary boot disk\n",
			    dv->dv_xname);
#endif
		}
		/* B_TYPE independent hd unit counting bootblocks */
		if (B_UNIT(bootdev) == (hit->bios_number & 0x7F)) {
			int type, ctrl, adap, part, unit;

			type = major(bp->b_dev);
			adap = B_ADAPTOR(bootdev);
			ctrl = B_CONTROLLER(bootdev);
			unit = DISKUNIT(bp->b_dev);
			part = B_PARTITION(bootdev);

			altbootdev = MAKEBOOTDEV(type, ctrl, adap, unit, part);
#ifdef DEBUG
			printf("dkcsum: %s is alternate boot disk\n",
			    dv->dv_xname);
#endif
		}

		/* This will overwrite /boot's guess, just so you remember */
		hit->bsd_dev = MAKEBOOTDEV(major(bp->b_dev), 0, 0,
		    DISKUNIT(bp->b_dev), RAW_PART);
		hit->flags |= BDI_PICKED;
	}
	bootdev = pribootdev ? pribootdev : altbootdev ? altbootdev : bootdev;

	bp->b_flags |= B_INVAL;
	brelse(bp);
}
