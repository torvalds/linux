/*	$OpenBSD: fd.c,v 1.110 2025/09/15 10:33:03 krw Exp $	*/
/*	$NetBSD: fd.c,v 1.90 1996/05/12 23:12:03 mycroft Exp $	*/

/*-
 * Copyright (c) 1993, 1994, 1995, 1996 Charles Hannum.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
 *
 * Portions Copyright (c) 1993, 1994 by
 *  jc@irbs.UUCP (John Capo)
 *  vak@zebub.msk.su (Serge Vakulenko)
 *  ache@astral.msk.su (Andrew A. Chernov)
 *  joerg_wunsch@uriah.sax.de (Joerg Wunsch)
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
 *	@(#)fd.c	7.4 (Berkeley) 5/25/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/mtio.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/timeout.h>
#include <sys/dkio.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/ioctl_fd.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>
#include <dev/isa/fdreg.h>

#if defined(__i386__) || defined(__amd64__)	/* XXX */
#include <i386/isa/nvram.h>
#endif

#include <dev/isa/fdlink.h>

/* XXX misuse a flag to identify format operation */
#define B_FORMAT B_XXX

/* fd_type struct now in ioctl_fd.h */

/* The order of entries in the following table is important -- BEWARE! */
struct fd_type fd_types[] = {
        { 18,2,36,2,0xff,0xcf,0x1b,0x6c,80,2880,1,FDC_500KBPS,"1.44MB"    }, /* 1.44MB diskette */
        { 15,2,30,2,0xff,0xdf,0x1b,0x54,80,2400,1,FDC_500KBPS, "1.2MB"    }, /* 1.2 MB AT-diskettes */
        {  9,2,18,2,0xff,0xdf,0x23,0x50,40, 720,2,FDC_300KBPS, "360KB/AT" }, /* 360kB in 1.2MB drive */
        {  9,2,18,2,0xff,0xdf,0x2a,0x50,40, 720,1,FDC_250KBPS, "360KB/PC" }, /* 360kB PC diskettes */
        {  9,2,18,2,0xff,0xdf,0x2a,0x50,80,1440,1,FDC_250KBPS, "720KB"    }, /* 3.5" 720kB diskette */
        {  9,2,18,2,0xff,0xdf,0x23,0x50,80,1440,1,FDC_300KBPS, "720KB/x"  }, /* 720kB in 1.2MB drive */
        {  9,2,18,2,0xff,0xdf,0x2a,0x50,40, 720,2,FDC_250KBPS, "360KB/x"  }, /* 360kB in 720kB drive */
	{ 36,2,72,2,0xff,0xaf,0x1b,0x54,80,5760,1,FDC_500KBPS,"2.88MB"    },  /* 2.88MB diskette */
	{  8,2,16,3,0xff,0xdf,0x35,0x74,77,1232,1,FDC_500KBPS,"1.2MB/[1024bytes/sector]" }	/* 1.2 MB japanese format */
};

/* software state, per disk (with up to 4 disks per ctlr) */
struct fd_softc {
	struct device sc_dev;
	struct disk sc_dk;

	struct fd_type *sc_deftype;	/* default type descriptor */
	struct fd_type *sc_type;	/* current type descriptor */

	daddr_t	sc_blkno;	/* starting block number */
	int sc_bcount;		/* byte count left */
 	int sc_opts;			/* user-set options */
	int sc_skip;		/* bytes already transferred */
	int sc_nblks;		/* number of blocks currently transferring */
	int sc_nbytes;		/* number of bytes currently transferring */

	int sc_drive;		/* physical unit number */
	int sc_flags;
#define	FD_OPEN		0x01		/* it's open */
#define	FD_MOTOR	0x02		/* motor should be on */
#define	FD_MOTOR_WAIT	0x04		/* motor coming up */
	int sc_cylin;		/* where we think the head is */

	TAILQ_ENTRY(fd_softc) sc_drivechain;
	int sc_ops;		/* I/O ops since last switch */
	struct bufq sc_bufq;	/* pending I/O */
	struct buf *sc_bp;	/* the current I/O */
	struct timeout fd_motor_on_to;
	struct timeout fd_motor_off_to;
	struct timeout fdtimeout_to;
};

/* floppy driver configuration */
int fdprobe(struct device *, void *, void *);
void fdattach(struct device *, struct device *, void *);
int fdactivate(struct device *, int);

const struct cfattach fd_ca = {
	sizeof(struct fd_softc), fdprobe, fdattach, NULL, fdactivate
};

struct cfdriver fd_cd = {
	NULL, "fd", DV_DISK
};

int fdgetdisklabel(dev_t, struct fd_softc *, struct disklabel *, int);
void fdstrategy(struct buf *);
void fdstart(struct fd_softc *);
int fdintr(struct fdc_softc *);

void fd_set_motor(struct fdc_softc *fdc, int reset);
void fd_motor_off(void *arg);
void fd_motor_on(void *arg);
void fdfinish(struct fd_softc *fd, struct buf *bp);
int fdformat(dev_t, struct fd_formb *, struct proc *);
static __inline struct fd_type *fd_dev_to_type(struct fd_softc *, dev_t);
void fdretry(struct fd_softc *);
void fdtimeout(void *);

int
fdgetdisklabel(dev_t dev, struct fd_softc *fd, struct disklabel *lp,
    int spoofonly)
{
	bzero(lp, sizeof(struct disklabel));

	lp->d_type = DTYPE_FLOPPY;
	lp->d_secsize = FD_BSIZE(fd);
	lp->d_secpercyl = fd->sc_type->seccyl;
	lp->d_nsectors = fd->sc_type->sectrac;
	lp->d_ncylinders = fd->sc_type->tracks;
	lp->d_ntracks = fd->sc_type->heads;	/* Go figure... */
	DL_SETDSIZE(lp, fd->sc_type->size);

	strncpy(lp->d_typename, "floppy disk", sizeof(lp->d_typename));
	strncpy(lp->d_packname, "fictitious", sizeof(lp->d_packname));
	lp->d_version = 1;

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = dkcksum(lp);

	/*
	 * Call the generic disklabel extraction routine.  If there's
	 * not a label there, fake it.
	 */
	return readdisklabel(DISKLABELDEV(dev), fdstrategy, lp, spoofonly);
}

int
fdprobe(struct device *parent, void *match, void *aux)
{
	struct fdc_softc *fdc = (void *)parent;
	struct cfdata *cf = match;
	struct fdc_attach_args *fa = aux;
	int drive = fa->fa_drive;
	bus_space_tag_t iot = fdc->sc_iot;
	bus_space_handle_t ioh = fdc->sc_ioh;
	int n;

	if (cf->cf_loc[0] != -1 && cf->cf_loc[0] != drive)
		return 0;
	/*
	 * XXX
	 * This is to work around some odd interactions between this driver
	 * and SMC Ethernet cards.
	 */
	if (cf->cf_loc[0] == -1 && drive >= 2)
		return 0;

	/*
	 * We want to keep the flags config gave us.
	 */
	fa->fa_flags = cf->cf_flags;

	/* select drive and turn on motor */
	bus_space_write_1(iot, ioh, fdout, drive | FDO_FRST | FDO_MOEN(drive));
	/* wait for motor to spin up */
	tsleep_nsec(fdc, 0, "fdprobe", MSEC_TO_NSEC(250));
	out_fdc(iot, ioh, NE7CMD_RECAL);
	out_fdc(iot, ioh, drive);
	/* wait for recalibrate */
	tsleep_nsec(fdc, 0, "fdprobe", MSEC_TO_NSEC(2000));
	out_fdc(iot, ioh, NE7CMD_SENSEI);
	n = fdcresult(fdc);
#ifdef FD_DEBUG
	{
		int i;
		printf("fdprobe: status");
		for (i = 0; i < n; i++)
			printf(" %x", fdc->sc_status[i]);
		printf("\n");
	}
#endif

	/* turn off motor */
	tsleep_nsec(fdc, 0, "fdprobe", MSEC_TO_NSEC(250));
	bus_space_write_1(iot, ioh, fdout, FDO_FRST);

	/* flags & 0x20 forces the drive to be found even if it won't probe */
	if (!(fa->fa_flags & 0x20) && (n != 2 || (fdc->sc_status[0] & 0xf8) != 0x20))
		return 0;

	return 1;
}

/*
 * Controller is working, and drive responded.  Attach it.
 */
void
fdattach(struct device *parent, struct device *self, void *aux)
{
	struct fdc_softc *fdc = (void *)parent;
	struct fd_softc *fd = (void *)self;
	struct fdc_attach_args *fa = aux;
	struct fd_type *type = fa->fa_deftype;
	int drive = fa->fa_drive;

	if (!type || (fa->fa_flags & 0x10)) {
		/* The config has overridden this. */
		switch (fa->fa_flags & 0x07) {
		case 1:	/* 2.88MB */
			type = &fd_types[7];
			break;
		case 2:	/* 1.44MB */
			type = &fd_types[0];
			break;
		case 3: /* 1.2MB */
			type = &fd_types[1];
			break;
		case 4: /* 720K */
			type = &fd_types[4];
			break;
		case 5: /* 360K */
			type = &fd_types[3];
			break;
		case 6:	/* 1.2 MB japanese format */
			type = &fd_types[8];
			break;
#ifdef __alpha__
		default:
			/* 1.44MB, how to detect others?
			 * idea from NetBSD -- jay@rootaction.net
                         */
			type = &fd_types[0];
#endif
		}
	}

	if (type)
		printf(": %s %d cyl, %d head, %d sec\n", type->name,
		    type->tracks, type->heads, type->sectrac);
	else
		printf(": density unknown\n");

	fd->sc_cylin = -1;
	fd->sc_drive = drive;
	fd->sc_deftype = type;
	fdc->sc_type[drive] = FDC_TYPE_DISK;
	fdc->sc_link.fdlink.sc_fd[drive] = fd;

	/*
	 * Initialize and attach the disk structure.
	 */
	fd->sc_dk.dk_flags = DKF_NOLABELREAD;
	fd->sc_dk.dk_name = fd->sc_dev.dv_xname;
	bufq_init(&fd->sc_bufq, BUFQ_DEFAULT);
	disk_attach(&fd->sc_dev, &fd->sc_dk);

	/* Setup timeout structures */
	timeout_set(&fd->fd_motor_on_to, fd_motor_on, fd);
	timeout_set(&fd->fd_motor_off_to, fd_motor_off, fd);
	timeout_set(&fd->fdtimeout_to, fdtimeout, fd);
}

int
fdactivate(struct device *self, int act)
{
	struct fd_softc *fd = (void *)self;
	struct fdc_softc *fdc = (void *)fd->sc_dev.dv_parent;
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		if (fdc->sc_state != DEVIDLE) {
			timeout_del(&fd->fd_motor_on_to);
			timeout_del(&fd->fd_motor_off_to);
			timeout_del(&fd->fdtimeout_to);
			fdc->sc_state = IOTIMEDOUT;
			fdc->sc_errors = 4;
		}
		break;
	case DVACT_POWERDOWN:
		fd_motor_off(self);
		break;
	}

	return (rv);
}

/*
 * Translate nvram type into internal data structure.  Return NULL for
 * none/unknown/unusable.
 */
struct fd_type *
fd_nvtotype(char *fdc, int nvraminfo, int drive)
{
#ifdef __alpha__
	/* Alpha:  assume 1.44MB, idea from NetBSD sys/dev/isa/fd.c
	 * -- jay@rootaction.net
	 */
	return &fd_types[0]; /* 1.44MB */
#else
	int type;

	type = (drive == 0 ? nvraminfo : nvraminfo << 4) & 0xf0;
	switch (type) {
	case NVRAM_DISKETTE_NONE:
		return NULL;
	case NVRAM_DISKETTE_12M:
		return &fd_types[1];
	case NVRAM_DISKETTE_TYPE5:
	case NVRAM_DISKETTE_TYPE6:
		return &fd_types[7];
	case NVRAM_DISKETTE_144M:
		return &fd_types[0];
	case NVRAM_DISKETTE_360K:
		return &fd_types[3];
	case NVRAM_DISKETTE_720K:
		return &fd_types[4];
	default:
		printf("%s: drive %d: unknown device type 0x%x\n",
		    fdc, drive, type);
		return NULL;
	}
#endif
}

static __inline struct fd_type *
fd_dev_to_type(struct fd_softc *fd, dev_t dev)
{
	int type = FDTYPE(dev);

	if (type > (sizeof(fd_types) / sizeof(fd_types[0])))
		return NULL;
	return type ? &fd_types[type - 1] : fd->sc_deftype;
}

void
fdstrategy(struct buf *bp)
{
	struct fd_softc *fd = fd_cd.cd_devs[FDUNIT(bp->b_dev)];
	int sz;
 	int s;
	int fd_bsize = FD_BSIZE(fd);
	int bf = fd_bsize / DEV_BSIZE;

	/* Valid unit, controller, and request? */
	if (bp->b_blkno < 0 ||
	    (((bp->b_blkno % bf) != 0 ||
	      (bp->b_bcount % fd_bsize) != 0) &&
	     (bp->b_flags & B_FORMAT) == 0)) {
		bp->b_error = EINVAL;
		goto bad;
	}

	/* If it's a null transfer, return immediately. */
	if (bp->b_bcount == 0)
		goto done;

	sz = howmany(bp->b_bcount, DEV_BSIZE);

	if (bp->b_blkno + sz > fd->sc_type->size * bf) {
		sz = fd->sc_type->size * bf - bp->b_blkno;
		if (sz == 0)
			/* If exactly at end of disk, return EOF. */
			goto done;
		if (sz < 0) {
			/* If past end of disk, return EINVAL. */
			bp->b_error = EINVAL;
			goto bad;
		}
		/* Otherwise, truncate request. */
		bp->b_bcount = sz << DEV_BSHIFT;
	}

	bp->b_resid = bp->b_bcount;

#ifdef FD_DEBUG
	printf("fdstrategy: b_blkno %lld b_bcount %d blkno %lld sz %d\n",
	    (long long)bp->b_blkno, bp->b_bcount,
	    (long long)fd->sc_blkno, sz);
#endif

	/* Queue I/O */
	bufq_queue(&fd->sc_bufq, bp);

	/* Queue transfer on drive, activate drive and controller if idle. */
	s = splbio();
	timeout_del(&fd->fd_motor_off_to); /* a good idea */
	if (fd->sc_bp == NULL)
		fdstart(fd);
#ifdef DIAGNOSTIC
	else {
		struct fdc_softc *fdc = (void *)fd->sc_dev.dv_parent;
		if (fdc->sc_state == DEVIDLE) {
			printf("fdstrategy: controller inactive\n");
			fdcstart(fdc);
		}
	}
#endif
	splx(s);
	return;

bad:
	bp->b_flags |= B_ERROR;
done:
	/* Toss transfer; we're done early. */
	bp->b_resid = bp->b_bcount;
	s = splbio();
	biodone(bp);
	splx(s);
}

void
fdstart(struct fd_softc *fd)
{
	struct fdc_softc *fdc = (void *)fd->sc_dev.dv_parent;
	int active = !TAILQ_EMPTY(&fdc->sc_link.fdlink.sc_drives);

	/* Link into controller queue. */
	fd->sc_bp = bufq_dequeue(&fd->sc_bufq);
	TAILQ_INSERT_TAIL(&fdc->sc_link.fdlink.sc_drives, fd, sc_drivechain);

	/* If controller not already active, start it. */
	if (!active)
		fdcstart(fdc);
}

void
fdfinish(struct fd_softc *fd, struct buf *bp)
{
	struct fdc_softc *fdc = (void *)fd->sc_dev.dv_parent;

	splassert(IPL_BIO);

	fd->sc_skip = 0;
	fd->sc_bp = bufq_dequeue(&fd->sc_bufq);

	/*
	 * Move this drive to the end of the queue to give others a `fair'
	 * chance.  We only force a switch if N operations are completed while
	 * another drive is waiting to be serviced, since there is a long motor
	 * startup delay whenever we switch.
	 */
	if (TAILQ_NEXT(fd, sc_drivechain) != NULL && ++fd->sc_ops >= 8) {
		fd->sc_ops = 0;
		TAILQ_REMOVE(&fdc->sc_link.fdlink.sc_drives, fd, sc_drivechain);
		if (fd->sc_bp != NULL) {
			TAILQ_INSERT_TAIL(&fdc->sc_link.fdlink.sc_drives, fd,
					  sc_drivechain);
		}
	}

	biodone(bp);
	/* turn off motor 5s from now */
	timeout_add_sec(&fd->fd_motor_off_to, 5);
	fdc->sc_state = DEVIDLE;
}

int
fdread(dev_t dev, struct uio *uio, int flags)
{
	return (physio(fdstrategy, dev, B_READ, minphys, uio));
}

int
fdwrite(dev_t dev, struct uio *uio, int flags)
{
	return (physio(fdstrategy, dev, B_WRITE, minphys, uio));
}

void
fd_set_motor(struct fdc_softc *fdc, int reset)
{
	struct fd_softc *fd;
	u_char status;
	int n;

	if ((fd = TAILQ_FIRST(&fdc->sc_link.fdlink.sc_drives)) != NULL)
		status = fd->sc_drive;
	else
		status = 0;
	if (!reset)
		status |= FDO_FRST | FDO_FDMAEN;
	for (n = 0; n < 4; n++)
		if ((fd = fdc->sc_link.fdlink.sc_fd[n])
		    && (fd->sc_flags & FD_MOTOR))
			status |= FDO_MOEN(n);
	bus_space_write_1(fdc->sc_iot, fdc->sc_ioh, fdout, status);
}

void
fd_motor_off(void *arg)
{
	struct fd_softc *fd = arg;
	int s;

	s = splbio();
	fd->sc_flags &= ~(FD_MOTOR | FD_MOTOR_WAIT);
	fd_set_motor((struct fdc_softc *)fd->sc_dev.dv_parent, 0);
	splx(s);
}

void
fd_motor_on(void *arg)
{
	struct fd_softc *fd = arg;
	struct fdc_softc *fdc = (void *)fd->sc_dev.dv_parent;
	int s;

	s = splbio();
	fd->sc_flags &= ~FD_MOTOR_WAIT;
	if ((TAILQ_FIRST(&fdc->sc_link.fdlink.sc_drives) == fd)
	    && (fdc->sc_state == MOTORWAIT))
		(void) fdintr(fdc);
	splx(s);
}

int
fdopen(dev_t dev, int flags, int fmt, struct proc *p)
{
 	uint64_t pmask;
 	int unit;
	struct fd_softc *fd;
	struct fd_type *type;

	unit = FDUNIT(dev);
	if (unit >= fd_cd.cd_ndevs)
		return ENXIO;
	fd = fd_cd.cd_devs[unit];
	if (fd == 0)
		return ENXIO;
	type = fd_dev_to_type(fd, dev);
	if (type == NULL)
		return ENXIO;

	if ((fd->sc_flags & FD_OPEN) != 0 &&
	    fd->sc_type != type)
		return EBUSY;

	fd->sc_type = type;
	fd->sc_cylin = -1;
	fd->sc_flags |= FD_OPEN;

	/*
	 * Only update the disklabel if we're not open anywhere else.
	 */
	if (fd->sc_dk.dk_openmask == 0)
		fdgetdisklabel(dev, fd, fd->sc_dk.dk_label, 0);

	pmask = (1 << FDPART(dev));

	switch (fmt) {
	case S_IFCHR:
		fd->sc_dk.dk_copenmask |= pmask;
		break;

	case S_IFBLK:
		fd->sc_dk.dk_bopenmask |= pmask;
		break;
	}
	fd->sc_dk.dk_openmask =
	    fd->sc_dk.dk_copenmask | fd->sc_dk.dk_bopenmask;

	return 0;
}

int
fdclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct fd_softc *fd = fd_cd.cd_devs[FDUNIT(dev)];
	uint64_t pmask = (1 << FDPART(dev));

	fd->sc_flags &= ~FD_OPEN;
	fd->sc_opts &= ~FDOPT_NORETRY;

	switch (fmt) {
	case S_IFCHR:
		fd->sc_dk.dk_copenmask &= ~pmask;
		break;

	case S_IFBLK:
		fd->sc_dk.dk_bopenmask &= ~pmask;
		break;
	}
	fd->sc_dk.dk_openmask =
	    fd->sc_dk.dk_copenmask | fd->sc_dk.dk_bopenmask;

	return (0);
}

daddr_t
fdsize(dev_t dev)
{
	/* Swapping to floppies would not make sense. */
	return -1;
}

int
fddump(dev_t dev, daddr_t blkno, caddr_t va, size_t size)
{
	/* Not implemented. */
	return ENXIO;
}

/*
 * Called from the controller.
 */
int
fdintr(struct fdc_softc *fdc)
{
#define	st0	fdc->sc_status[0]
#define	cyl	fdc->sc_status[1]
	struct fd_softc *fd;
	struct buf *bp;
	bus_space_tag_t iot = fdc->sc_iot;
	bus_space_handle_t ioh = fdc->sc_ioh;
	bus_space_handle_t ioh_ctl = fdc->sc_ioh_ctl;
	int read, head, sec, i, nblks, cylin;
	struct fd_type *type;
	struct fd_formb *finfo = NULL;
	int fd_bsize;

loop:
	/* Is there a transfer to this drive?  If not, deactivate drive. */
	fd = TAILQ_FIRST(&fdc->sc_link.fdlink.sc_drives);
	if (fd == NULL) {
		fdc->sc_state = DEVIDLE;
		return 1;
	}
	fd_bsize = FD_BSIZE(fd);

	bp = fd->sc_bp;
	if (bp == NULL) {
		fd->sc_ops = 0;
		TAILQ_REMOVE(&fdc->sc_link.fdlink.sc_drives, fd, sc_drivechain);
		goto loop;
	}

	if (bp->b_flags & B_FORMAT)
	    finfo = (struct fd_formb *)bp->b_data;

	cylin = ((bp->b_blkno * DEV_BSIZE) + (bp->b_bcount - bp->b_resid)) /
	    (fd_bsize * fd->sc_type->seccyl);

	switch (fdc->sc_state) {
	case DEVIDLE:
		fdc->sc_errors = 0;
		fd->sc_skip = 0;
		fd->sc_bcount = bp->b_bcount;
		fd->sc_blkno = bp->b_blkno / (fd_bsize / DEV_BSIZE);
		timeout_del(&fd->fd_motor_off_to);
		if ((fd->sc_flags & FD_MOTOR_WAIT) != 0) {
			fdc->sc_state = MOTORWAIT;
			return 1;
		}
		if ((fd->sc_flags & FD_MOTOR) == 0) {
			/* Turn on the motor, being careful about pairing. */
			struct fd_softc *ofd =
				fdc->sc_link.fdlink.sc_fd[fd->sc_drive ^ 1];
			if (ofd && ofd->sc_flags & FD_MOTOR) {
				timeout_del(&ofd->fd_motor_off_to);
				ofd->sc_flags &= ~(FD_MOTOR | FD_MOTOR_WAIT);
			}
			fd->sc_flags |= FD_MOTOR | FD_MOTOR_WAIT;
			fd_set_motor(fdc, 0);
			fdc->sc_state = MOTORWAIT;
			/* Allow .25s for motor to stabilize. */
			timeout_add_msec(&fd->fd_motor_on_to, 250);
			return 1;
		}
		/* Make sure the right drive is selected. */
		fd_set_motor(fdc, 0);

		/* FALLTHROUGH */
	case DOSEEK:
	doseek:
		if (fd->sc_cylin == cylin)
			goto doio;

		out_fdc(iot, ioh, NE7CMD_SPECIFY);/* specify command */
		out_fdc(iot, ioh, fd->sc_type->steprate);
		out_fdc(iot, ioh, 6);		/* XXX head load time == 6ms */

		out_fdc(iot, ioh, NE7CMD_SEEK);	/* seek function */
		out_fdc(iot, ioh, fd->sc_drive);	/* drive number */
		out_fdc(iot, ioh, cylin * fd->sc_type->step);

		fd->sc_cylin = -1;
		fdc->sc_state = SEEKWAIT;

		fd->sc_dk.dk_seek++;
		disk_busy(&fd->sc_dk);

		timeout_add_sec(&fd->fdtimeout_to, 4);
		return 1;

	case DOIO:
	doio:
		type = fd->sc_type;
		if (finfo)
		    fd->sc_skip = (char *)&(finfo->fd_formb_cylno(0)) -
			(char *)finfo;
		sec = fd->sc_blkno % type->seccyl;
		nblks = type->seccyl - sec;
		nblks = min(nblks, fd->sc_bcount / fd_bsize);
		nblks = min(nblks, FDC_MAXIOSIZE / fd_bsize);
		fd->sc_nblks = nblks;
		fd->sc_nbytes = finfo ? bp->b_bcount : nblks * fd_bsize;
		head = sec / type->sectrac;
		sec -= head * type->sectrac;
#ifdef DIAGNOSTIC
		{int block;
		 block = (fd->sc_cylin * type->heads + head) * type->sectrac + sec;
		 if (block != fd->sc_blkno) {
			 panic("fdintr: block %d != blkno %llu", block, fd->sc_blkno);
		 }}
#endif
		read = bp->b_flags & B_READ ? DMAMODE_READ : DMAMODE_WRITE;
		isadma_start(bp->b_data + fd->sc_skip, fd->sc_nbytes,
		    fdc->sc_drq, read);
		bus_space_write_1(iot, ioh_ctl, fdctl, type->rate);
#ifdef FD_DEBUG
		printf("fdintr: %s drive %d track %d head %d sec %d nblks %d\n",
		    read ? "read" : "write", fd->sc_drive, fd->sc_cylin, head,
		    sec, nblks);
#endif
		if (finfo) {
                        /* formatting */
			if (out_fdc(iot, ioh, NE7CMD_FORMAT) < 0) {
			    fdc->sc_errors = 4;
			    fdretry(fd);
			    goto loop;
			}
                        out_fdc(iot, ioh, (head << 2) | fd->sc_drive);
                        out_fdc(iot, ioh, finfo->fd_formb_secshift);
                        out_fdc(iot, ioh, finfo->fd_formb_nsecs);
                        out_fdc(iot, ioh, finfo->fd_formb_gaplen);
                        out_fdc(iot, ioh, finfo->fd_formb_fillbyte);
		} else {
			if (read)
				out_fdc(iot, ioh, NE7CMD_READ);	/* READ */
			else
				out_fdc(iot, ioh, NE7CMD_WRITE);/* WRITE */
			out_fdc(iot, ioh, (head << 2) | fd->sc_drive);
			out_fdc(iot, ioh, fd->sc_cylin);	/* track */
			out_fdc(iot, ioh, head);
			out_fdc(iot, ioh, sec + 1);		/* sec +1 */
			out_fdc(iot, ioh, type->secsize);	/* sec size */
			out_fdc(iot, ioh, type->sectrac);	/* secs/track */
			out_fdc(iot, ioh, type->gap1);		/* gap1 size */
			out_fdc(iot, ioh, type->datalen);	/* data len */
		}
		fdc->sc_state = IOCOMPLETE;

		disk_busy(&fd->sc_dk);

		/* allow 2 seconds for operation */
		timeout_add_sec(&fd->fdtimeout_to, 2);
		return 1;				/* will return later */

	case SEEKWAIT:
		timeout_del(&fd->fdtimeout_to);
		fdc->sc_state = SEEKCOMPLETE;
		/* allow 1/50 second for heads to settle */
		timeout_add_msec(&fdc->fdcpseudointr_to, 20);
		return 1;

	case SEEKCOMPLETE:
		disk_unbusy(&fd->sc_dk, 0, 0, 0);	/* no data on seek */

		/* Make sure seek really happened. */
		out_fdc(iot, ioh, NE7CMD_SENSEI);
		if (fdcresult(fdc) != 2 || (st0 & 0xf8) != 0x20 ||
		    cyl != cylin * fd->sc_type->step) {
#ifdef FD_DEBUG
			fdcstatus(&fd->sc_dev, 2, "seek failed");
#endif
			fdretry(fd);
			goto loop;
		}
		fd->sc_cylin = cylin;
		goto doio;

	case IOTIMEDOUT:
		isadma_abort(fdc->sc_drq);
	case SEEKTIMEDOUT:
	case RECALTIMEDOUT:
	case RESETTIMEDOUT:
		fdretry(fd);
		goto loop;

	case IOCOMPLETE: /* IO DONE, post-analyze */
		timeout_del(&fd->fdtimeout_to);

		disk_unbusy(&fd->sc_dk, (bp->b_bcount - bp->b_resid),
		    fd->sc_blkno, (bp->b_flags & B_READ));

		if (fdcresult(fdc) != 7 || (st0 & 0xf8) != 0) {
			isadma_abort(fdc->sc_drq);
#ifdef FD_DEBUG
			fdcstatus(&fd->sc_dev, 7, bp->b_flags & B_READ ?
			    "read failed" : "write failed");
			printf("blkno %lld nblks %d\n",
			    (long long)fd->sc_blkno, fd->sc_nblks);
#endif
			fdretry(fd);
			goto loop;
		}
		read = bp->b_flags & B_READ ? DMAMODE_READ : DMAMODE_WRITE;
		isadma_done(fdc->sc_drq);
		if (fdc->sc_errors) {
			diskerr(bp, "fd", "soft error", LOG_PRINTF,
			    fd->sc_skip / fd_bsize, (struct disklabel *)NULL);
			printf("\n");
			fdc->sc_errors = 0;
		}

		fd->sc_blkno += fd->sc_nblks;
		fd->sc_skip += fd->sc_nbytes;
		fd->sc_bcount -= fd->sc_nbytes;
		bp->b_resid -= fd->sc_nbytes;
		if (!finfo && fd->sc_bcount > 0) {
			cylin = fd->sc_blkno / fd->sc_type->seccyl;
			goto doseek;
		}
		fdfinish(fd, bp);
		goto loop;

	case DORESET:
		/* try a reset, keep motor on */
		fd_set_motor(fdc, 1);
		delay(100);
		fd_set_motor(fdc, 0);
		fdc->sc_state = RESETCOMPLETE;
		timeout_add_msec(&fd->fdtimeout_to, 500);
		return 1;			/* will return later */

	case RESETCOMPLETE:
		timeout_del(&fd->fdtimeout_to);
		/* clear the controller output buffer */
		for (i = 0; i < 4; i++) {
			out_fdc(iot, ioh, NE7CMD_SENSEI);
			(void) fdcresult(fdc);
		}

		/* FALLTHROUGH */
	case DORECAL:
		out_fdc(iot, ioh, NE7CMD_RECAL);	/* recal function */
		out_fdc(iot, ioh, fd->sc_drive);
		fdc->sc_state = RECALWAIT;
		timeout_add_sec(&fd->fdtimeout_to, 5);
		return 1;			/* will return later */

	case RECALWAIT:
		timeout_del(&fd->fdtimeout_to);
		fdc->sc_state = RECALCOMPLETE;
		/* allow 1/30 second for heads to settle */
		timeout_add_msec(&fdc->fdcpseudointr_to, 1000 / 30);
		return 1;			/* will return later */

	case RECALCOMPLETE:
		out_fdc(iot, ioh, NE7CMD_SENSEI);
		if (fdcresult(fdc) != 2 || (st0 & 0xf8) != 0x20 || cyl != 0) {
#ifdef FD_DEBUG
			fdcstatus(&fd->sc_dev, 2, "recalibrate failed");
#endif
			fdretry(fd);
			goto loop;
		}
		fd->sc_cylin = 0;
		goto doseek;

	case MOTORWAIT:
		if (fd->sc_flags & FD_MOTOR_WAIT)
			return 1;		/* time's not up yet */
		goto doseek;

	default:
		fdcstatus(&fd->sc_dev, 0, "stray interrupt");
		return 1;
	}
#ifdef DIAGNOSTIC
	panic("fdintr: impossible");
#endif
#undef	st0
#undef	cyl
}

void
fdtimeout(void *arg)
{
	struct fd_softc *fd = arg;
	struct fdc_softc *fdc = (void *)fd->sc_dev.dv_parent;
	int s;

	s = splbio();
#ifdef DEBUG
	log(LOG_ERR,"fdtimeout: state %d\n", fdc->sc_state);
#endif
	fdcstatus(&fd->sc_dev, 0, "timeout");

	if (fd->sc_bp != NULL)
		fdc->sc_state++;
	else
		fdc->sc_state = DEVIDLE;

	(void) fdintr(fdc);
	splx(s);
}

void
fdretry(struct fd_softc *fd)
{
	struct fdc_softc *fdc = (void *)fd->sc_dev.dv_parent;
	struct buf *bp = fd->sc_bp;

	if (fd->sc_opts & FDOPT_NORETRY)
	    goto fail;
	switch (fdc->sc_errors) {
	case 0:
		/* try again */
		fdc->sc_state = DOSEEK;
		break;

	case 1: case 2: case 3:
		/* didn't work; try recalibrating */
		fdc->sc_state = DORECAL;
		break;

	case 4:
		/* still no go; reset the bastard */
		fdc->sc_state = DORESET;
		break;

	default:
	fail:
		diskerr(bp, "fd", "hard error", LOG_PRINTF,
		    fd->sc_skip / FD_BSIZE(fd), (struct disklabel *)NULL);
		printf(" (st0 %b st1 %b st2 %b cyl %d head %d sec %d)\n",
		    fdc->sc_status[0], NE7_ST0BITS,
		    fdc->sc_status[1], NE7_ST1BITS,
		    fdc->sc_status[2], NE7_ST2BITS,
		    fdc->sc_status[3], fdc->sc_status[4], fdc->sc_status[5]);

		bp->b_flags |= B_ERROR;
		bp->b_error = EIO;
		bp->b_resid = bp->b_bcount;
		fdfinish(fd, bp);
	}
	fdc->sc_errors++;
}

int
fdioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct fd_softc *fd = fd_cd.cd_devs[FDUNIT(dev)];
	struct disklabel *lp;
	int error;

	switch (cmd) {
	case MTIOCTOP:
		if (((struct mtop *)addr)->mt_op != MTOFFL)
			return EIO;
		return (0);

	case DIOCRLDINFO:
		lp = malloc(sizeof(*lp), M_TEMP, M_WAITOK);
		fdgetdisklabel(dev, fd, lp, 0);
		bcopy(lp, fd->sc_dk.dk_label, sizeof(*lp));
		free(lp, M_TEMP, sizeof(*lp));
		return 0;

	case DIOCGPDINFO:
		fdgetdisklabel(dev, fd, (struct disklabel *)addr, 1);
		return 0;

	case DIOCGDINFO:
		*(struct disklabel *)addr = *(fd->sc_dk.dk_label);
		return 0;

	case DIOCGPART:
		((struct partinfo *)addr)->disklab = fd->sc_dk.dk_label;
		((struct partinfo *)addr)->part =
		    &fd->sc_dk.dk_label->d_partitions[FDPART(dev)];
		return 0;

	case DIOCWDINFO:
	case DIOCSDINFO:
		if ((flag & FWRITE) == 0)
			return EBADF;

		error = setdisklabel(fd->sc_dk.dk_label,
		    (struct disklabel *)addr, 0);
		if (error == 0) {
			if (cmd == DIOCWDINFO)
				error = writedisklabel(DISKLABELDEV(dev),
				    fdstrategy, fd->sc_dk.dk_label);
		}
		return error;

        case FD_FORM:
                if((flag & FWRITE) == 0)
                        return EBADF;  /* must be opened for writing */
                else if(((struct fd_formb *)addr)->format_version !=
                        FD_FORMAT_VERSION)
                        return EINVAL; /* wrong version of formatting prog */
                else
                        return fdformat(dev, (struct fd_formb *)addr, p);
                break;

        case FD_GTYPE:                  /* get drive type */
                *(struct fd_type *)addr = *fd->sc_type;
		return 0;

        case FD_GOPTS:                  /* get drive options */
                *(int *)addr = fd->sc_opts;
                return 0;
                
        case FD_SOPTS:                  /* set drive options */
                fd->sc_opts = *(int *)addr;
		return 0;

	default:
		return ENOTTY;
	}

#ifdef DIAGNOSTIC
	panic("fdioctl: impossible");
#endif
}

int
fdformat(dev_t dev, struct fd_formb *finfo, struct proc *p)
{
        int rv = 0;
	struct fd_softc *fd = fd_cd.cd_devs[FDUNIT(dev)];
	struct fd_type *type = fd->sc_type;
        struct buf *bp;
	int fd_bsize = FD_BSIZE(fd);

        /* set up a buffer header for fdstrategy() */
        bp = malloc(sizeof(*bp), M_TEMP, M_NOWAIT | M_ZERO);
        if (bp == NULL)
                return ENOBUFS;

        bp->b_flags = B_BUSY | B_PHYS | B_FORMAT | B_RAW;
        bp->b_proc = p;
        bp->b_dev = dev;

        /*
         * calculate a fake blkno, so fdstrategy() would initiate a
         * seek to the requested cylinder
         */
        bp->b_blkno = (finfo->cyl * (type->sectrac * type->heads)
                + finfo->head * type->sectrac) * fd_bsize / DEV_BSIZE;

        bp->b_bcount = sizeof(struct fd_idfield_data) * finfo->fd_formb_nsecs;
        bp->b_data = (caddr_t)finfo;
        
#ifdef DEBUG
	printf("fdformat: blkno %llx count %lx\n", bp->b_blkno, bp->b_bcount);
#endif

        /* now do the format */
        fdstrategy(bp);

        /* ...and wait for it to complete */
	rv = biowait(bp);
        free(bp, M_TEMP, sizeof(*bp));
        return (rv);
}
