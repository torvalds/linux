/*	$OpenBSD: rd.c,v 1.14 2022/04/06 18:59:27 naddy Exp $	*/

/*
 * Copyright (c) 2011 Matthew Dempsky <matthew@dempsky.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/dkio.h>
#include <sys/vnode.h>

#ifndef MINIROOTSIZE
#define MINIROOTSIZE 512
#endif

#define ROOTBYTES (MINIROOTSIZE << DEV_BSHIFT)

/*
 * This array will be patched to contain a file-system image.
 * See the program:  src/distrib/common/rdsetroot.c
 */
u_int32_t rd_root_size = ROOTBYTES;
char rd_root_image[ROOTBYTES] = "|This is the root ramdisk!\n";

void	rdattach(int);
int	rd_match(struct device *, void *, void *);
void	rd_attach(struct device *, struct device *, void *);
int	rd_detach(struct device *, int);

struct rd_softc {
	struct device	sc_dev;
	struct disk	sc_dk;
};

const struct cfattach rd_ca = {
	sizeof(struct rd_softc),
	rd_match,
	rd_attach,
	rd_detach
};

struct cfdriver rd_cd = {
	NULL,
	"rd",
	DV_DISK
};

#define rdlookup(unit)	((struct rd_softc *)disk_lookup(&rd_cd, (unit)))

int	rdgetdisklabel(dev_t, struct rd_softc *, struct disklabel *, int);

void
rdattach(int num)
{
	static struct cfdata cf; /* Fake cf. */
	struct rd_softc *sc;
	int i;

	/* There's only one rd_root_image, so only attach one rd. */
	num = 1;

	/* XXX: Fake up more? */
	cf.cf_attach = &rd_ca;
	cf.cf_driver = &rd_cd;

	rd_cd.cd_ndevs = num;
	rd_cd.cd_devs = mallocarray(num, sizeof(void *), M_DEVBUF, M_NOWAIT);
	if (rd_cd.cd_devs == NULL)
		panic("rdattach: out of memory");

	for (i = 0; i < num; ++i) {
		/* Allocate the softc and initialize it. */
		sc = malloc(sizeof(*sc), M_DEVBUF, M_NOWAIT|M_ZERO);
		if (sc == NULL)
			panic("rdattach: out of memory");
		sc->sc_dev.dv_class = DV_DISK;
		sc->sc_dev.dv_cfdata = &cf;
		sc->sc_dev.dv_flags = DVF_ACTIVE;
		sc->sc_dev.dv_unit = i;
	        if (snprintf(sc->sc_dev.dv_xname, sizeof(sc->sc_dev.dv_xname),
		    "rd%d", i) >= sizeof(sc->sc_dev.dv_xname))
			panic("rdattach: device name too long");
		sc->sc_dev.dv_ref = 1;

		/* Attach it to the device tree. */
		rd_cd.cd_devs[i] = sc;
		TAILQ_INSERT_TAIL(&alldevs, &sc->sc_dev, dv_list);
		device_ref(&sc->sc_dev);

		/* Finish initializing. */
		rd_attach(NULL, &sc->sc_dev, NULL);
	}
}

int
rd_match(struct device *parent, void *match, void *aux)
{
	return (0);
}

void
rd_attach(struct device *parent, struct device *self, void *aux)
{
	struct rd_softc *sc = (struct rd_softc *)self;

	/* Attach disk. */
	sc->sc_dk.dk_name = sc->sc_dev.dv_xname;
	disk_attach(&sc->sc_dev, &sc->sc_dk);
}

int
rd_detach(struct device *self, int flags)
{
	struct rd_softc *sc = (struct rd_softc *)self;

	disk_gone(rdopen, self->dv_unit);

	/* Detach disk. */
	disk_detach(&sc->sc_dk);

	return (0);
}

int
rdopen(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct rd_softc *sc;
	u_int unit, part;
	int error;

	unit = DISKUNIT(dev);
	part = DISKPART(dev);

	sc = rdlookup(unit);
	if (sc == NULL)
		return (ENXIO);

	if ((error = disk_lock(&sc->sc_dk)) != 0)
		goto unref;

	if (sc->sc_dk.dk_openmask == 0) {
		/* Load the partition info if not already loaded. */
		if ((error = rdgetdisklabel(dev, sc, sc->sc_dk.dk_label, 0))
		    != 0)
			goto unlock;
	}

	error = disk_openpart(&sc->sc_dk, part, fmt, 1);

 unlock:
	disk_unlock(&sc->sc_dk);
 unref:
	device_unref(&sc->sc_dev);
	return (error);
}

int
rdclose(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct rd_softc *sc;
	u_int unit, part;

	unit = DISKUNIT(dev);
	part = DISKPART(dev);

	sc = rdlookup(unit);
	if (sc == NULL)
		return (ENXIO);

	disk_lock_nointr(&sc->sc_dk);

	disk_closepart(&sc->sc_dk, part, fmt);

	disk_unlock(&sc->sc_dk);
	device_unref(&sc->sc_dev);
	return (0);
}

void
rdstrategy(struct buf *bp)
{
	struct rd_softc *sc;
	struct partition *p;
	size_t off, xfer;
	caddr_t addr;
	int s;

	sc = rdlookup(DISKUNIT(bp->b_dev));
	if (sc == NULL) {
		bp->b_error = ENXIO;
		goto bad;
	}

	/* Validate the request. */
	if (bounds_check_with_label(bp, sc->sc_dk.dk_label) == -1)
		goto done;

	/* Do the transfer. */
	/* XXX: Worry about overflow when computing off? */

	p = &sc->sc_dk.dk_label->d_partitions[DISKPART(bp->b_dev)];
	off = DL_GETPOFFSET(p) * sc->sc_dk.dk_label->d_secsize +
	    (u_int64_t)bp->b_blkno * DEV_BSIZE;
	if (off > rd_root_size)
		off = rd_root_size;
	xfer = bp->b_bcount;
	if (xfer > rd_root_size - off)
		xfer = rd_root_size - off;
	addr = rd_root_image + off;
	if (bp->b_flags & B_READ)
		memcpy(bp->b_data, addr, xfer);
	else
		memcpy(addr, bp->b_data, xfer);
	bp->b_resid = bp->b_bcount - xfer;
	goto done;

 bad:
	bp->b_flags |= B_ERROR;
	bp->b_resid = bp->b_bcount;
 done:
	s = splbio();
	biodone(bp);
	splx(s);
	if (sc != NULL)
		device_unref(&sc->sc_dev);
}

int
rdioctl(dev_t dev, u_long cmd, caddr_t data, int fflag, struct proc *p)
{
	struct rd_softc *sc;
	struct disklabel *lp;
	int error = 0;

	sc = rdlookup(DISKUNIT(dev));
	if (sc == NULL)
		return (ENXIO);

	switch (cmd) {
	case DIOCRLDINFO:
		lp = malloc(sizeof(*lp), M_TEMP, M_WAITOK);
		rdgetdisklabel(dev, sc, lp, 0);
		memcpy(sc->sc_dk.dk_label, lp, sizeof(*lp));
		free(lp, M_TEMP, sizeof(*lp));
		goto done;

	case DIOCGPDINFO:
		rdgetdisklabel(dev, sc, (struct disklabel *)data, 1);
		goto done;

	case DIOCGDINFO:
		*(struct disklabel *)data = *(sc->sc_dk.dk_label);
		goto done;

	case DIOCGPART:
		((struct partinfo *)data)->disklab = sc->sc_dk.dk_label;
		((struct partinfo *)data)->part =
		    &sc->sc_dk.dk_label->d_partitions[DISKPART(dev)];
		goto done;

	case DIOCWDINFO:
	case DIOCSDINFO:
		if ((fflag & FWRITE) == 0) {
			error = EBADF;
			goto done;
		}

		if ((error = disk_lock(&sc->sc_dk)) != 0)
			goto done;

		error = setdisklabel(sc->sc_dk.dk_label,
		    (struct disklabel *)data, sc->sc_dk.dk_openmask);
		if (error == 0) {
			if (cmd == DIOCWDINFO)
				error = writedisklabel(DISKLABELDEV(dev),
				    rdstrategy, sc->sc_dk.dk_label);
		}

		disk_unlock(&sc->sc_dk);
		goto done;
	}

 done:
	device_unref(&sc->sc_dev);
	return (error);
}

int
rdgetdisklabel(dev_t dev, struct rd_softc *sc, struct disklabel *lp,
    int spoofonly)
{
	bzero(lp, sizeof(struct disklabel));

	lp->d_secsize = DEV_BSIZE;
	lp->d_ntracks = 1;
	lp->d_nsectors = rd_root_size >> DEV_BSHIFT;
	lp->d_ncylinders = 1;
	lp->d_secpercyl = lp->d_nsectors;
	if (lp->d_secpercyl == 0) {
		lp->d_secpercyl = 100;
		/* as long as it's not 0 - readdisklabel divides by it */
	}

	strncpy(lp->d_typename, "RAM disk", sizeof(lp->d_typename));
	lp->d_type = DTYPE_SCSI;
	strncpy(lp->d_packname, "fictitious", sizeof(lp->d_packname));
	DL_SETDSIZE(lp, lp->d_nsectors);
	lp->d_version = 1;

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = dkcksum(lp);

	/* Call the generic disklabel extraction routine. */
	return (readdisklabel(DISKLABELDEV(dev), rdstrategy, lp, spoofonly));
}

int
rdread(dev_t dev, struct uio *uio, int ioflag)
{
	return (physio(rdstrategy, dev, B_READ, minphys, uio));
}

int
rdwrite(dev_t dev, struct uio *uio, int ioflag)
{
	return (physio(rdstrategy, dev, B_WRITE, minphys, uio));
}

int
rddump(dev_t dev, daddr_t blkno, caddr_t va, size_t size)
{
	return (ENXIO);
}

daddr_t
rdsize(dev_t dev)
{
	return (-1);
}
