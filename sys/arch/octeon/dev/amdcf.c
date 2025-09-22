/*	$OpenBSD: amdcf.c,v 1.10 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2007, Juniper Networks, Inc.
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2009 Sam Leffler, Errno Consulting
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
 * Copyright (c) 2015 Paul Irofti.
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
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mutex.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/dkio.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/autoconf.h>

#include <octeon/dev/iobusvar.h>
#include <machine/octeonreg.h>
#include <machine/octeonvar.h>


#define CFI_QRY_CMD_ADDR	0x55
#define CFI_QRY_CMD_DATA	0x98

#define CFI_QRY_TTO_WRITE	0x1f
#define CFI_QRY_TTO_ERASE	0x21
#define CFI_QRY_MTO_WRITE	0x23
#define CFI_QRY_MTO_ERASE	0x25

#define CFI_QRY_SIZE		0x27
#define	CFI_QRY_NREGIONS	0x2c
#define CFI_QRY_REGION0 	0x31
#define CFI_QRY_REGION(x)	(CFI_QRY_REGION0 + (x) * 4)

#define CFI_BCS_READ_ARRAY	0xff

#define CFI_DISK_SECSIZE	512
#define CFI_DISK_MAXIOSIZE	65536

#define AMDCF_MAP_SIZE		0x02000000

#define CFI_AMD_BLOCK_ERASE	0x30
#define CFI_AMD_UNLOCK		0xaa
#define CFI_AMD_UNLOCK_ACK	0x55
#define CFI_AMD_PROGRAM		0xa0
#define CFI_AMD_RESET		0xf0

#define AMD_ADDR_START		0x555
#define AMD_ADDR_ACK		0x2aa

#define BOOTLOADER_ADDR		0xa0000

struct cfi_region {
	u_int r_blocks;
	u_int r_blksz;
};

struct amdcf_softc {
	/* General disk infos */
	struct device sc_dev;
	struct disk sc_dk;
	struct bufq sc_bufq;
	struct buf *sc_bp;

	int sc_flags;
#define AMDCF_LOADED	0x10

	struct iobus_attach_args *sc_io;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	size_t sc_size;		/* Disk size in bytes */
	u_int sc_regions;	/* Erase regions. */
	struct cfi_region *sc_region;	/* Array of region info. */

	u_int sc_width;
	u_int sc_shift;
	u_int sc_mask;

	u_int sc_erase_timeout;
	u_int sc_erase_max_timeout;
	u_int sc_write_timeout;
	u_int sc_write_max_timeout;
	u_int sc_rstcmd;

	u_char *sc_wrbuf;
	u_int sc_wrbufsz;
	u_int sc_wrofs;
	u_int sc_writing;
};

int	amdcf_match(struct device *, void *, void *);
void	amdcf_attach(struct device *, struct device *, void *);
int	amdcf_detach(struct device *, int);

const struct cfattach amdcf_ca = {
	sizeof(struct amdcf_softc), amdcf_match, amdcf_attach, amdcf_detach
};

struct cfdriver amdcf_cd = {
	NULL, "amdcf", DV_DISK
};

cdev_decl(amdcf);
bdev_decl(amdcf);

#define amdcflookup(unit) (struct amdcf_softc *)disk_lookup(&amdcf_cd, (unit))
int amdcfgetdisklabel(dev_t, struct amdcf_softc *, struct disklabel *, int);

void amdcfstart(void *);
void _amdcfstart(struct amdcf_softc *, struct buf *);
void amdcfdone(void *);

void amdcf_disk_read(struct amdcf_softc *, struct buf *, off_t);
void amdcf_disk_write(struct amdcf_softc *, struct buf *, off_t);

int cfi_block_start(struct amdcf_softc *, u_int);
int cfi_write_block(struct amdcf_softc *);
int cfi_erase_block(struct amdcf_softc *, u_int);
int cfi_block_finish(struct amdcf_softc *);

void cfi_array_write(struct amdcf_softc *sc, u_int, u_int, u_int);
void cfi_amd_write(struct amdcf_softc *, u_int, u_int, u_int);

uint8_t cfi_read(struct amdcf_softc *, bus_size_t, bus_size_t);
void cfi_write(struct amdcf_softc *, bus_size_t, bus_size_t, uint8_t);
int cfi_wait_ready(struct amdcf_softc *, u_int, u_int, u_int);
int cfi_make_cmd(uint8_t, u_int);

int
amdcf_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *maa = aux;
	struct cfdata *cf = match;

	if (strcmp(maa->maa_name, cf->cf_driver->cd_name) != 0)
		return 0;

	/* Only for DSR machines */
	if (octeon_board != BOARD_DLINK_DSR_500)
		return 0;

	return 1;
}

void
amdcf_attach(struct device *parent, struct device *self, void *aux)
{
	struct amdcf_softc *sc = (void *)self;
	u_int blksz, blocks, r;

	sc->sc_io = aux;
	sc->sc_iot = sc->sc_io->aa_bust;

	if (bus_space_map(sc->sc_iot, OCTEON_AMDCF_BASE, AMDCF_MAP_SIZE, 0,
	    &sc->sc_ioh)) {
		printf(": can't map registers");
	}

	/* should be detected in the generic driver */
	sc->sc_width = 1;
	sc->sc_shift = 2;
	sc->sc_mask = 0x000000ff;
	sc->sc_rstcmd = CFI_AMD_RESET;

	/* Initialize the Query Database from the CF */
	cfi_array_write(sc, 0, 0, sc->sc_rstcmd);
	cfi_write(sc, 0, CFI_QRY_CMD_ADDR, CFI_QRY_CMD_DATA);

	/* Get time-out values for erase and write. */
	sc->sc_write_timeout = 1 << cfi_read(sc, 0, CFI_QRY_TTO_WRITE);
	sc->sc_erase_timeout = 1 << cfi_read(sc, 0, CFI_QRY_TTO_ERASE);
	sc->sc_write_max_timeout = 1 << cfi_read(sc, 0, CFI_QRY_MTO_WRITE);
	sc->sc_erase_max_timeout = 1 << cfi_read(sc, 0, CFI_QRY_MTO_ERASE);

	/* Get the device size. */
	sc->sc_size = 1U << cfi_read(sc, 0, CFI_QRY_SIZE);
	printf(": AMD/Fujitsu %zu bytes\n", sc->sc_size);

	/* Get erase regions. */
	sc->sc_regions = cfi_read(sc, 0, CFI_QRY_NREGIONS);
	sc->sc_region = malloc(sc->sc_regions *
	    sizeof(struct cfi_region), M_TEMP, M_WAITOK | M_ZERO);

	for (r = 0; r < sc->sc_regions; r++) {
		blocks = cfi_read(sc, 0, CFI_QRY_REGION(r)) |
		    (cfi_read(sc, 0, CFI_QRY_REGION(r) + 1) << 8);
		sc->sc_region[r].r_blocks = blocks + 1;

		blksz = cfi_read(sc, 0, CFI_QRY_REGION(r) + 2) |
		    (cfi_read(sc, 0, CFI_QRY_REGION(r) + 3) << 8);
		sc->sc_region[r].r_blksz = (blksz == 0) ? 128 :
		    blksz * 256;
	}

	/* Reset the device to the default state */
	cfi_array_write(sc, 0, 0, sc->sc_rstcmd);

	/*
	 * Initialize disk structures.
	 */
	sc->sc_dk.dk_name = sc->sc_dev.dv_xname;
	bufq_init(&sc->sc_bufq, BUFQ_DEFAULT);

	/* Attach disk. */
	disk_attach(&sc->sc_dev, &sc->sc_dk);

}

int
amdcf_detach(struct device *self, int flags)
{
	struct amdcf_softc *sc = (struct amdcf_softc *)self;

	bufq_drain(&sc->sc_bufq);

	disk_gone(amdcfopen, self->dv_unit);

	/* Detach disk. */
	bufq_destroy(&sc->sc_bufq);
	disk_detach(&sc->sc_dk);

	return 0;
}


int
amdcfopen(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct amdcf_softc *sc;
	int unit, part;
	int error;

	unit = DISKUNIT(dev);
	sc = amdcflookup(unit);
	if (sc == NULL)
		return ENXIO;

	/*
	 * If this is the first open of this device, add a reference
	 * to the adapter.
	 */
	if ((error = disk_lock(&sc->sc_dk)) != 0)
		goto out1;

	if (sc->sc_dk.dk_openmask != 0) {
		/*
		 * If any partition is open, but the disk has been invalidated,
		 * disallow further opens.
		 */
		if ((sc->sc_flags & AMDCF_LOADED) == 0) {
			error = EIO;
			goto out;
		}
	} else {
		if ((sc->sc_flags & AMDCF_LOADED) == 0) {
			sc->sc_flags |= AMDCF_LOADED;

			/* Load the partition info if not already loaded. */
			if (amdcfgetdisklabel(dev, sc,
			    sc->sc_dk.dk_label, 0) == EIO) {
				error = EIO;
				goto out;
			}
		}
	}

	part = DISKPART(dev);

	if ((error = disk_openpart(&sc->sc_dk, part, fmt, 1)) != 0)
		goto out;

	disk_unlock(&sc->sc_dk);
	device_unref(&sc->sc_dev);
	return 0;

out:
	disk_unlock(&sc->sc_dk);
out1:
	device_unref(&sc->sc_dev);
	return error;
}

/*
 * Load the label information on the named device
 */
int
amdcfgetdisklabel(dev_t dev, struct amdcf_softc *sc, struct disklabel *lp,
    int spoofonly)
{
	memset(lp, 0, sizeof(struct disklabel));

	lp->d_secsize = DEV_BSIZE;
	lp->d_nsectors = 1;	/* bogus */
	lp->d_ntracks = 1;	/* bogus */
	lp->d_secpercyl = lp->d_ntracks * lp->d_nsectors;
	lp->d_ncylinders = sc->sc_size / lp->d_secpercyl;

	strlcpy(lp->d_typename, "amdcf device", sizeof(lp->d_typename));
	lp->d_type = DTYPE_SCSI;	/* bogus type, can be anything */
	strlcpy(lp->d_packname, "CFI Disk", sizeof(lp->d_packname));
	DL_SETDSIZE(lp, sc->sc_size / DEV_BSIZE);
	lp->d_version = 1;

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = dkcksum(lp);

	/* Call the generic disklabel extraction routine */
	return readdisklabel(DISKLABELDEV(dev), amdcfstrategy, lp, spoofonly);
}

int
amdcfclose(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct amdcf_softc *sc;
	int part = DISKPART(dev);

	sc = amdcflookup(DISKUNIT(dev));
	if (sc == NULL)
		return ENXIO;

	disk_lock_nointr(&sc->sc_dk);

	disk_closepart(&sc->sc_dk, part, fmt);

	disk_unlock(&sc->sc_dk);

	device_unref(&sc->sc_dev);
	return 0;
}

int
amdcfread(dev_t dev, struct uio *uio, int flags)
{
	return (physio(amdcfstrategy, dev, B_READ, minphys, uio));
}

int
amdcfwrite(dev_t dev, struct uio *uio, int flags)
{
#ifdef AMDCF_DISK_WRITE_ENABLE
	return (physio(amdcfstrategy, dev, B_WRITE, minphys, uio));
#else
	return 0;
#endif
}

void
amdcfstrategy(struct buf *bp)
{
	struct amdcf_softc *sc;
	int s;

	sc = amdcflookup(DISKUNIT(bp->b_dev));
	if (sc == NULL) {
		bp->b_error = ENXIO;
		goto bad;
	}
	/* If device invalidated (e.g. media change, door open), error. */
	if ((sc->sc_flags & AMDCF_LOADED) == 0) {
		bp->b_error = EIO;
		goto bad;
	}

	/* Validate the request. */
	if (bounds_check_with_label(bp, sc->sc_dk.dk_label) == -1)
		goto done;

	/* Check that the number of sectors can fit in a byte. */
	if ((bp->b_bcount / sc->sc_dk.dk_label->d_secsize) >= (1 << NBBY)) {
		bp->b_error = EINVAL;
		goto bad;
	}

	/* Queue transfer on drive, activate drive and controller if idle. */
	bufq_queue(&sc->sc_bufq, bp);
	s = splbio();
	amdcfstart(sc);
	splx(s);
	device_unref(&sc->sc_dev);
	return;

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
amdcfioctl(dev_t dev, u_long xfer, caddr_t addr, int flag, struct proc *p)
{
	struct amdcf_softc *sc;
	struct disklabel *lp;
	int error = 0;

	sc = amdcflookup(DISKUNIT(dev));
	if (sc == NULL)
		return ENXIO;

	if ((sc->sc_flags & AMDCF_LOADED) == 0) {
		error = EIO;
		goto exit;
	}

	switch (xfer) {
	case DIOCRLDINFO:
		lp = malloc(sizeof(*lp), M_TEMP, M_WAITOK);
		amdcfgetdisklabel(dev, sc, lp, 0);
		bcopy(lp, sc->sc_dk.dk_label, sizeof(*lp));
		free(lp, M_TEMP, sizeof(*lp));
		goto exit;

	case DIOCGPDINFO:
		amdcfgetdisklabel(dev, sc, (struct disklabel *)addr, 1);
		goto exit;

	case DIOCGDINFO:
		*(struct disklabel *)addr = *(sc->sc_dk.dk_label);
		goto exit;

	case DIOCGPART:
		((struct partinfo *)addr)->disklab = sc->sc_dk.dk_label;
		((struct partinfo *)addr)->part =
		    &sc->sc_dk.dk_label->d_partitions[DISKPART(dev)];
		goto exit;

	case DIOCWDINFO:
	case DIOCSDINFO:
		if ((flag & FWRITE) == 0) {
			error = EBADF;
			goto exit;
		}

		if ((error = disk_lock(&sc->sc_dk)) != 0)
			goto exit;

		error = setdisklabel(sc->sc_dk.dk_label,
		    (struct disklabel *)addr, sc->sc_dk.dk_openmask);
		if (error == 0) {
			if (xfer == DIOCWDINFO)
				error = writedisklabel(DISKLABELDEV(dev),
				    amdcfstrategy, sc->sc_dk.dk_label);
		}

		disk_unlock(&sc->sc_dk);
		goto exit;

	default:
		error = ENOTTY;
		goto exit;
	}

#ifdef DIAGNOSTIC
	panic("amdcfioctl: impossible");
#endif

 exit:
	device_unref(&sc->sc_dev);
	return error;
}

/*
 * Dump core after a system crash.
 */
int
amdcfdump(dev_t dev, daddr_t blkno, caddr_t va, size_t size)
{
	return ENXIO;
}

daddr_t
amdcfsize(dev_t dev)
{
	struct amdcf_softc *sc;
	struct disklabel *lp;
	int part, omask;
	daddr_t size;

	sc = amdcflookup(DISKUNIT(dev));
	if (sc == NULL)
		return (-1);

	part = DISKPART(dev);
	omask = sc->sc_dk.dk_openmask & (1 << part);

	if (omask == 0 && amdcfopen(dev, 0, S_IFBLK, NULL) != 0) {
		size = -1;
		goto exit;
	}

	lp = sc->sc_dk.dk_label;
	size = DL_SECTOBLK(lp, DL_GETPSIZE(&lp->d_partitions[part]));
	if (omask == 0 && amdcfclose(dev, 0, S_IFBLK, NULL) != 0)
		size = -1;

 exit:
	device_unref(&sc->sc_dev);
	return size;
}


/*
 * Queue a drive for I/O.
 */
void
amdcfstart(void *arg)
{
	struct amdcf_softc *sc = arg;
	struct buf *bp;

	while ((bp = bufq_dequeue(&sc->sc_bufq)) != NULL) {
		/* Transfer this buffer now. */
		_amdcfstart(sc, bp);
	}
}

void
_amdcfstart(struct amdcf_softc *sc, struct buf *bp)
{
	off_t off;
	struct partition *p;

	sc->sc_bp = bp;

	/* Fetch buffer's read/write offset */
	p = &sc->sc_dk.dk_label->d_partitions[DISKPART(bp->b_dev)];
	off = DL_GETPOFFSET(p) * sc->sc_dk.dk_label->d_secsize +
	    (u_int64_t)bp->b_blkno * DEV_BSIZE;
	if (off > sc->sc_size) {
		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		return;
	}

	/* Instrumentation. */
	disk_busy(&sc->sc_dk);

	if (bp->b_flags & B_READ)
		amdcf_disk_read(sc, bp, off);
#ifdef AMDCF_DISK_WRITE_ENABLE
	else
		amdcf_disk_write(sc, bp, off);
#endif

	amdcfdone(sc);
}

void
amdcfdone(void *arg)
{
	struct amdcf_softc *sc = arg;
	struct buf *bp = sc->sc_bp;

	if (bp->b_error == 0)
		bp->b_resid = 0;
	else
		bp->b_flags |= B_ERROR;

	disk_unbusy(&sc->sc_dk, (bp->b_bcount - bp->b_resid),
	    bp->b_blkno, (bp->b_flags & B_READ));
	biodone(bp);
}

void
amdcf_disk_read(struct amdcf_softc *sc, struct buf *bp, off_t off)
{
	long resid;

	if (sc->sc_writing) {
		bp->b_error = cfi_block_finish(sc);
		if (bp->b_error) {
			bp->b_flags |= B_ERROR;
			return;
		}
	}

	resid = bp->b_bcount;
	uint8_t *dp = (uint8_t *)bp->b_data;
	while (resid > 0 && off < sc->sc_size) {
		*dp++ = cfi_read(sc, off, 0);
		off += 1, resid -= 1;
	}
	bp->b_resid = resid;
}

void
amdcf_disk_write(struct amdcf_softc *sc, struct buf *bp, off_t off)
{
	long resid;
	u_int top;

	resid = bp->b_bcount;
	while (resid > 0) {
		/*
		 * Finish the current block if we're about to write
		 * to a different block.
		 */
		if (sc->sc_writing) {
			top = sc->sc_wrofs + sc->sc_wrbufsz;
			if (off < sc->sc_wrofs || off >= top)
				cfi_block_finish(sc);
		}

		/* Start writing to a (new) block if applicable. */
		if (!sc->sc_writing) {
			bp->b_error = cfi_block_start(sc, off);
			if (bp->b_error) {
				bp->b_flags |= B_ERROR;
				return;
			}
		}

		top = sc->sc_wrofs + sc->sc_wrbufsz;
		bcopy(bp->b_data,
		    sc->sc_wrbuf + off - sc->sc_wrofs,
		    MIN(top - off, resid));
		resid -= MIN(top - off, resid);
	}
	bp->b_resid = resid;
}

/*
 * Begin writing into a new block/sector.  We read the sector into
 * memory and keep updating that, until we move into another sector
 * or the process stops writing. At that time we write the whole
 * sector to flash (see cfi_block_finish).
 */
int
cfi_block_start(struct amdcf_softc *sc, u_int ofs)
{
	u_int rofs, rsz;
	int r;
	uint8_t *ptr;

	rofs = 0;
	for (r = 0; r < sc->sc_regions; r++) {
		rsz = sc->sc_region[r].r_blocks * sc->sc_region[r].r_blksz;
		if (ofs < rofs + rsz)
			break;
		rofs += rsz;
	}
	if (r == sc->sc_regions)
		return (EFAULT);

	sc->sc_wrbufsz = sc->sc_region[r].r_blksz;
	sc->sc_wrbuf = malloc(sc->sc_wrbufsz, M_TEMP, M_WAITOK);
	sc->sc_wrofs = ofs - (ofs - rofs) % sc->sc_wrbufsz;

	ptr = sc->sc_wrbuf;
	/* Read the block from flash for byte-serving. */
	for (r = 0; r < sc->sc_wrbufsz; r++)
		*(ptr)++ = cfi_read(sc, sc->sc_wrofs + r, 0);

	sc->sc_writing = 1;
	return (0);
}

/*
 * Finish updating the current block/sector by writing the compound
 * set of changes to the flash.
 */
int
cfi_block_finish(struct amdcf_softc *sc)
{
	int error;

	error = cfi_write_block(sc);
	free(sc->sc_wrbuf, M_TEMP, sc->sc_wrbufsz);
	sc->sc_wrbuf = NULL;
	sc->sc_wrbufsz = 0;
	sc->sc_wrofs = 0;
	sc->sc_writing = 0;
	return (error);
}

int
cfi_write_block(struct amdcf_softc *sc)
{
	uint8_t *ptr;
	int error, i, s;

	if (sc->sc_wrofs > sc->sc_size)
		panic("CFI: write offset (%x) bigger "
		    "than cfi array size (%zu)\n",
		    sc->sc_wrofs, sc->sc_size);

	if ((sc->sc_wrofs < BOOTLOADER_ADDR) ||
	    ((sc->sc_wrofs + sc->sc_wrbufsz) < BOOTLOADER_ADDR))
		return EOPNOTSUPP;

	error = cfi_erase_block(sc, sc->sc_wrofs);
	if (error)
		goto out;

	/* Write the block. */
	ptr = sc->sc_wrbuf;

	for (i = 0; i < sc->sc_wrbufsz; i += sc->sc_width) {

		/*
		 * Make sure the command to start a write and the
		 * actual write happens back-to-back without any
		 * excessive delays.
		 */
		s = splbio();

		cfi_amd_write(sc, sc->sc_wrofs, AMD_ADDR_START,
			    CFI_AMD_PROGRAM);
		/* Raw data do not use cfi_array_write */
		cfi_write(sc, sc->sc_wrofs + i, 0, *(ptr)++);

		splx(s);

		error = cfi_wait_ready(sc, sc->sc_wrofs + i,
		    sc->sc_write_timeout, sc->sc_write_max_timeout);
		if (error)
			goto out;
	}

out:
	cfi_array_write(sc, sc->sc_wrofs, 0, sc->sc_rstcmd);
	return error;
}

int
cfi_erase_block(struct amdcf_softc *sc, u_int offset)
{
	int error = 0;

	if (offset > sc->sc_size)
		panic("CFI: erase offset (%x) bigger "
		    "than cfi array size (%zu)\n",
		    sc->sc_wrofs, sc->sc_size);

	/* Erase the block. */
	cfi_amd_write(sc, offset, 0, CFI_AMD_BLOCK_ERASE);

	error = cfi_wait_ready(sc, offset, sc->sc_erase_timeout,
	    sc->sc_erase_max_timeout);

	return error;
}



int
cfi_wait_ready(struct amdcf_softc *sc, u_int ofs, u_int timeout, u_int count)
{
	int done, error;
	u_int st0 = 0, st = 0;

	done = 0;
	error = 0;

	if (!timeout)
		timeout = 100;  /* Default to 100 uS */
	if (!count)
		count = 100;    /* Max timeout is 10 mS */

	while (!done && !error && count) {
		DELAY(timeout);

		count--;

		/*
		 * read sc->sc_width bytes, and check for toggle bit.
		 */
		st0 = cfi_read(sc, ofs, 0);
		st = cfi_read(sc, ofs, 0);
		done = ((st & cfi_make_cmd(0x40, sc->sc_mask)) ==
		    (st0 & cfi_make_cmd(0x40, sc->sc_mask))) ? 1 : 0;

		break;
	}
	if (!done && !error)
		error = ETIMEDOUT;
	if (error)
		printf("\nerror=%d (st 0x%x st0 0x%x) at offset=%x\n",
		    error, st, st0, ofs);
	return error;
}

/*
 * cfi_array_write
 * fill "bus width" word with value of var data by array mask sc->sc_mask
 */
void
cfi_array_write(struct amdcf_softc *sc, u_int ofs, u_int addr, u_int data)
{
	data &= 0xff;
	cfi_write(sc, ofs, addr, cfi_make_cmd(data, sc->sc_mask));
}

void
cfi_amd_write(struct amdcf_softc *sc, u_int ofs, u_int addr, u_int data)
{
	cfi_array_write(sc, ofs, AMD_ADDR_START, CFI_AMD_UNLOCK);
	cfi_array_write(sc, ofs, AMD_ADDR_ACK, CFI_AMD_UNLOCK_ACK);
	cfi_array_write(sc, ofs, addr, data);
}



/*
 * The following routines assume width=1 and shift=2 as that is
 * the case on the Octeon DSR machines.
 * If this assumption fails a new detection routine should be written
 * and called during attach.
 */
uint8_t
cfi_read(struct amdcf_softc *sc, bus_size_t base, bus_size_t offset)
{
	return bus_space_read_1(sc->sc_iot, sc->sc_ioh,
	    base | (offset * sc->sc_shift));
}

void
cfi_write(struct amdcf_softc *sc, bus_size_t base, bus_size_t offset,
    uint8_t val)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh,
	    base | (offset * sc->sc_shift), val);
}

int
cfi_make_cmd(uint8_t cmd, u_int mask)
{
	int i;
	u_int data = 0;

	for (i = 0; i < sizeof(int); i ++) {
		if (mask & (0xff << (i*8)))
			data |= cmd << (i*8);
	}

	return data;
}
