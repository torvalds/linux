/*	$OpenBSD: fd.c,v 1.54 2025/05/11 19:41:05 miod Exp $	*/
/*	$NetBSD: fd.c,v 1.112 2003/08/07 16:29:35 agc Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 */

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
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

/*-
 * Copyright (c) 1993, 1994, 1995 Charles M. Hannum.
 * Copyright (c) 1995 Paul Kranenburg.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
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
 *	@(#)fd.c	7.4 (Berkeley) 5/25/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/mtio.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/dkio.h>

#include <dev/cons.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/intr.h>
#include <machine/ioctl_fd.h>

#include <sparc64/dev/auxioreg.h>
#include <sparc64/dev/auxiovar.h>
#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>
#include <sparc64/dev/fdreg.h>
#include <sparc64/dev/fdvar.h>

#define FDUNIT(dev)	((minor(dev) / MAXPARTITIONS) / 8)
#define FDTYPE(dev)	((minor(dev) / MAXPARTITIONS) % 8)

#define	FTC_FLIP \
	do { \
		auxio_fd_control(AUXIO_LED_FTC); \
		auxio_fd_control(0); \
	} while (0)

/* XXX misuse a flag to identify format operation */
#define B_FORMAT B_XXX

#ifdef FD_DEBUG
int	fdc_debug = 0;
#endif

enum fdc_state {
	DEVIDLE = 0,
	MOTORWAIT,	/*  1 */
	DOSEEK,		/*  2 */
	SEEKWAIT,	/*  3 */
	SEEKTIMEDOUT,	/*  4 */
	SEEKCOMPLETE,	/*  5 */
	DOIO,		/*  6 */
	IOCOMPLETE,	/*  7 */
	IOTIMEDOUT,	/*  8 */
	IOCLEANUPWAIT,	/*  9 */
	IOCLEANUPTIMEDOUT,/*10 */
	DORESET,	/* 11 */
	RESETCOMPLETE,	/* 12 */
	RESETTIMEDOUT,	/* 13 */
	DORECAL,	/* 14 */
	RECALWAIT,	/* 15 */
	RECALTIMEDOUT,	/* 16 */
	RECALCOMPLETE,	/* 17 */
	DODSKCHG,	/* 18 */
	DSKCHGWAIT,	/* 19 */
	DSKCHGTIMEDOUT,	/* 20 */
};

/* software state, per controller */
struct fdc_softc {
	struct device	sc_dev;		/* boilerplate */
	bus_space_tag_t	sc_bustag;

	struct timeout fdctimeout_to;
	struct timeout fdcpseudointr_to;

	struct fd_softc *sc_fd[4];	/* pointers to children */
	TAILQ_HEAD(drivehead, fd_softc) sc_drives;
	enum fdc_state	sc_state;
	int		sc_flags;
#define	FDC_EBUS		0x01
#define FDC_NEEDHEADSETTLE	0x02
#define FDC_EIS			0x04
#define FDC_NEEDMOTORWAIT	0x08
#define	FDC_NOEJECT		0x10
	int		sc_errors;		/* number of retries so far */
	int		sc_overruns;		/* number of DMA overruns */
	int		sc_cfg;			/* current configuration */
	struct fdcio	sc_io;
#define sc_handle	sc_io.fdcio_handle
#define sc_itask	sc_io.fdcio_itask
#define sc_istatus	sc_io.fdcio_istatus
#define sc_data		sc_io.fdcio_data
#define sc_tc		sc_io.fdcio_tc
#define sc_nstat	sc_io.fdcio_nstat
#define sc_status	sc_io.fdcio_status

	void		*sc_sicookie;	/* softintr(9) cookie */
};

/* controller driver configuration */
int	fdcmatch_sbus(struct device *, void *, void *);
int	fdcmatch_ebus(struct device *, void *, void *);
void	fdcattach_sbus(struct device *, struct device *, void *);
void	fdcattach_ebus(struct device *, struct device *, void *);

int	fdcattach(struct fdc_softc *, int);

const struct cfattach fdc_sbus_ca = {
	sizeof(struct fdc_softc), fdcmatch_sbus, fdcattach_sbus
};

const struct cfattach fdc_ebus_ca = {
	sizeof(struct fdc_softc), fdcmatch_ebus, fdcattach_ebus
};

struct cfdriver fdc_cd = {
	NULL, "fdc", DV_DULL
};

__inline struct fd_type *fd_dev_to_type(struct fd_softc *, dev_t);

/* The order of entries in the following table is important -- BEWARE! */
struct fd_type fd_types[] = {
	{ 18,2,36,2,0xff,0xcf,0x1b,0x6c,80,2880,1,FDC_500KBPS, "1.44MB"    }, /* 1.44MB diskette */
	{  9,2,18,2,0xff,0xdf,0x2a,0x50,80,1440,1,FDC_250KBPS, "720KB"    }, /* 3.5" 720kB diskette */
	{  9,2,18,2,0xff,0xdf,0x2a,0x50,40, 720,2,FDC_250KBPS, "360KB/x"  }, /* 360kB in 720kB drive */
	{  8,2,16,3,0xff,0xdf,0x35,0x74,77,1232,1,FDC_500KBPS, "1.2MB/NEC" } /* 1.2 MB japanese format */
};

/* software state, per disk (with up to 4 disks per ctlr) */
struct fd_softc {
	struct device	sc_dv;		/* generic device info */
	struct disk	sc_dk;		/* generic disk info */

	struct fd_type *sc_deftype;	/* default type descriptor */
	struct fd_type *sc_type;	/* current type descriptor */

	struct timeout sc_motoron_to;
	struct timeout sc_motoroff_to;

	daddr_t sc_blkno;	/* starting block number */
	int sc_bcount;		/* byte count left */
	int sc_skip;		/* bytes already transferred */
	int sc_nblks;		/* number of blocks currently transferring */
	int sc_nbytes;		/* number of bytes currently transferring */

	int sc_drive;		/* physical unit number */
	int sc_flags;
#define	FD_OPEN		0x01		/* it's open */
#define	FD_MOTOR	0x02		/* motor should be on */
#define	FD_MOTOR_WAIT	0x04		/* motor coming up */
	int sc_cylin;		/* where we think the head is */
	int sc_opts;		/* user-set options */

	TAILQ_ENTRY(fd_softc) sc_drivechain;
	int sc_ops;		/* I/O ops since last switch */
	struct bufq sc_bufq;	/* pending I/O requests */
	struct buf *sc_bp;	/* current I/O */
};

/* floppy driver configuration */
int	fdmatch(struct device *, void *, void *);
void	fdattach(struct device *, struct device *, void *);
int	fdactivate(struct device *, int);

const struct cfattach fd_ca = {
	sizeof(struct fd_softc), fdmatch, fdattach,
	NULL, fdactivate
};

struct cfdriver fd_cd = {
	NULL, "fd", DV_DISK
};

int fdgetdisklabel(dev_t, struct fd_softc *, struct disklabel *, int);
void fdstrategy(struct buf *);
void fdstart(struct fd_softc *);
int fdprint(void *, const char *);

struct	fd_type *fd_nvtotype(char *, int, int);
void	fd_set_motor(struct fdc_softc *fdc);
void	fd_motor_off(void *arg);
void	fd_motor_on(void *arg);
int	fdcresult(struct fdc_softc *fdc);
int	fdc_wrfifo(struct fdc_softc *fdc, u_char x);
void	fdcstart(struct fdc_softc *fdc);
void	fdcstatus(struct fdc_softc *fdc, char *s);
void	fdc_reset(struct fdc_softc *fdc);
int	fdc_diskchange(struct fdc_softc *fdc);
void	fdctimeout(void *arg);
void	fdcpseudointr(void *arg);
int	fdchwintr(void *);
void	fdcswintr(void *);
int	fdcstate(struct fdc_softc *);
void	fdcretry(struct fdc_softc *fdc);
void	fdfinish(struct fd_softc *fd, struct buf *bp);
int	fdformat(dev_t, struct fd_formb *, struct proc *);
void	fd_do_eject(struct fd_softc *);
static int fdconf(struct fdc_softc *);

int
fdcmatch_sbus(struct device *parent, void *match, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp("SUNW,fdtwo", sa->sa_name) == 0);
}

void
fdcattach_sbus(struct device *parent, struct device *self, void *aux)
{
	struct fdc_softc *fdc = (void *)self;
	struct sbus_attach_args *sa = aux;

	if (sa->sa_nintr == 0) {
		printf(": no interrupt line configured\n");
		return;
	}

	if (auxio_fd_control(0) != 0) {
		printf(": can't attach before auxio\n");
		return;
	}

	fdc->sc_bustag = sa->sa_bustag;

	if (sbus_bus_map(sa->sa_bustag,
			 sa->sa_slot, sa->sa_offset, sa->sa_size,
			 BUS_SPACE_MAP_LINEAR, 0, &fdc->sc_handle) != 0) {
		printf(": cannot map control registers\n");
		return;
	}

	if (strcmp(getpropstring(sa->sa_node, "status"), "disabled") == 0) {
		printf(": no drives attached\n");
		return;
	}

	if (getproplen(sa->sa_node, "manual") >= 0)
		fdc->sc_flags |= FDC_NOEJECT;

	if (fdcattach(fdc, sa->sa_pri) != 0)
		bus_space_unmap(sa->sa_bustag, fdc->sc_handle, sa->sa_size);
}

int
fdcmatch_ebus(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	return (strcmp("fdthree", ea->ea_name) == 0);
}

void
fdcattach_ebus(struct device *parent, struct device *self, void *aux)
{
	struct fdc_softc *fdc = (void *)self;
	struct ebus_attach_args *ea = aux;

	if (ea->ea_nintrs == 0) {
		printf(": no interrupt line configured\n");
		return;
	}

	if (ea->ea_nregs < 3) {
		printf(": expected 3 registers, only got %d\n",
		    ea->ea_nregs);
		return;
	}

	if (ea->ea_nvaddrs > 0) {
		if (bus_space_map(ea->ea_memtag, ea->ea_vaddrs[0], 0,
		    BUS_SPACE_MAP_PROMADDRESS, &fdc->sc_handle) != 0) {
			printf(": can't map control registers\n");
			return;
		}
		fdc->sc_bustag = ea->ea_memtag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &fdc->sc_handle) == 0) {
		fdc->sc_bustag = ea->ea_memtag;
	} else if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &fdc->sc_handle) == 0) {
		fdc->sc_bustag = ea->ea_iotag;
	} else {
		printf(": can't map control registers\n");
		return;
	}

	if (strcmp(getpropstring(ea->ea_node, "status"), "disabled") == 0) {
		printf(": no drives attached\n");
		return;
	}

	fdc->sc_flags |= FDC_EBUS;

	if (getproplen(ea->ea_node, "manual") >= 0)
		fdc->sc_flags |= FDC_NOEJECT;

	/* XXX unmapping if it fails */
	fdcattach(fdc, ea->ea_intrs[0]);
}

/*
 * Arguments passed between fdcattach and fdprobe.
 */
struct fdc_attach_args {
	int fa_drive;
	struct fd_type *fa_deftype;
};

/*
 * Print the location of a disk drive (called just before attaching the
 * the drive).  If `fdc' is not NULL, the drive was found but was not
 * in the system config file; print the drive name as well.
 * Return QUIET (config_find ignores this if the device was configured) to
 * avoid printing `fdN not configured' messages.
 */
int
fdprint(void *aux, const char *fdc)
{
	register struct fdc_attach_args *fa = aux;

	if (!fdc)
		printf(" drive %d", fa->fa_drive);
	return (QUIET);
}

/*
 * Configure several parameters and features on the FDC.
 * Return 0 on success.
 */
static int
fdconf(struct fdc_softc *fdc)
{
	int	vroom;

	if (fdc_wrfifo(fdc, NE7CMD_DUMPREG) || fdcresult(fdc) != 10)
		return (-1);

	/*
	 * dumpreg[7] seems to be a motor-off timeout; set it to whatever
	 * the PROM thinks is appropriate.
	 */
	if ((vroom = fdc->sc_status[7]) == 0)
		vroom = 0x64;

	/* Configure controller to use FIFO and Implied Seek */
	if (fdc_wrfifo(fdc, NE7CMD_CFG) != 0)
		return (-1);
	if (fdc_wrfifo(fdc, vroom) != 0)
		return (-1);
	if (fdc_wrfifo(fdc, fdc->sc_cfg) != 0)
		return (-1);
	if (fdc_wrfifo(fdc, 0) != 0)	/* PRETRK */
		return (-1);
	/* No result phase for the NE7CMD_CFG command */

	/* Lock configuration across soft resets. */
	if (fdc_wrfifo(fdc, NE7CMD_LOCK | CFG_LOCK) != 0 ||
	    fdcresult(fdc) != 1) {
#ifdef FD_DEBUG
		printf("fdconf: CFGLOCK failed");
#endif
		return (-1);
	}

	return (0);
#if 0
	if (fdc_wrfifo(fdc, NE7CMD_VERSION) == 0 &&
	    fdcresult(fdc) == 1 && fdc->sc_status[0] == 0x90) {
		if (fdc_debug)
			printf("[version cmd]");
	}
#endif
}

int
fdcattach(struct fdc_softc *fdc, int pri)
{
	struct fdc_attach_args fa;
	int drive_attached;

	timeout_set(&fdc->fdctimeout_to, fdctimeout, fdc);
	timeout_set(&fdc->fdcpseudointr_to, fdcpseudointr, fdc);

	fdc->sc_state = DEVIDLE;
	fdc->sc_itask = FDC_ITASK_NONE;
	fdc->sc_istatus = FDC_ISTATUS_NONE;
	fdc->sc_flags |= FDC_EIS | FDC_NEEDMOTORWAIT;
	TAILQ_INIT(&fdc->sc_drives);

	/*
	 * Configure controller; enable FIFO, Implied seek, no POLL mode?.
	 * Note: CFG_EFIFO is active-low, initial threshold value: 8
	 */
	fdc->sc_cfg = CFG_EIS|/*CFG_EFIFO|*/CFG_POLL|(8 & CFG_THRHLD_MASK);
	if (fdconf(fdc) != 0) {
		printf("\n%s: no drives attached\n", fdc->sc_dev.dv_xname);
		return (-1);
	}

	if (bus_intr_establish(fdc->sc_bustag, pri, IPL_BIO,
	    0, fdchwintr, fdc, fdc->sc_dev.dv_xname) == NULL) {
		printf("\n%s: cannot register interrupt handler\n",
			fdc->sc_dev.dv_xname);
		return (-1);
	}

	fdc->sc_sicookie = softintr_establish_raw(IPL_BIO, fdcswintr, fdc);
	if (fdc->sc_sicookie == NULL) {
		printf("\n%s: cannot register soft interrupt handler\n",
			fdc->sc_dev.dv_xname);
		return (-1);
	}

	if (fdc->sc_flags & FDC_NOEJECT)
		printf(": manual eject");
	printf("\n");

	/* physical limit: four drives per controller. */
	drive_attached = 0;
	for (fa.fa_drive = 0; fa.fa_drive < 4; fa.fa_drive++) {
		fa.fa_deftype = NULL;		/* unknown */
		fa.fa_deftype = &fd_types[0];	/* XXX */
		if (config_found(&fdc->sc_dev, (void *)&fa, fdprint) != NULL)
			drive_attached = 1;
	}

	if (drive_attached == 0) {
		/* XXX - dis-establish interrupts here */
		/* return (-1); */
	}

	return (0);
}

int
fdmatch(struct device *parent, void *match, void *aux)
{
	struct fdc_softc *fdc = (void *)parent;
	bus_space_tag_t t = fdc->sc_bustag;
	bus_space_handle_t h = fdc->sc_handle;
	struct fdc_attach_args *fa = aux;
	int drive = fa->fa_drive;
	int n, ok;

	if (drive > 0)
		/* XXX - for now, punt on more than one drive */
		return (0);

	/* select drive and turn on motor */
	bus_space_write_1(t, h, FDREG77_DOR,
	    drive | FDO_FRST | FDO_MOEN(drive));
	/* wait for motor to spin up */
	delay(250000);

	fdc->sc_nstat = 0;
	fdc_wrfifo(fdc, NE7CMD_RECAL);
	fdc_wrfifo(fdc, drive);

	/* Wait for recalibration to complete */
	for (n = 0; n < 10000; n++) {
		u_int8_t v;

		delay(1000);
		v = bus_space_read_1(t, h, FDREG77_MSR);
		if ((v & (NE7_RQM|NE7_DIO|NE7_CB)) == NE7_RQM) {
			/* wait a bit longer till device *really* is ready */
			delay(100000);
			if (fdc_wrfifo(fdc, NE7CMD_SENSEI))
				break;
			if (fdcresult(fdc) == 1 && fdc->sc_status[0] == 0x80)
				/*
				 * Got `invalid command'; we interpret it
				 * to mean that the re-calibrate hasn't in
				 * fact finished yet
				 */
				continue;
			break;
		}
	}
	n = fdc->sc_nstat;
#ifdef FD_DEBUG
	if (fdc_debug) {
		int i;
		printf("fdprobe: %d stati:", n);
		for (i = 0; i < n; i++)
			printf(" 0x%x", fdc->sc_status[i]);
		printf("\n");
	}
#endif
	ok = (n == 2 && (fdc->sc_status[0] & 0xf8) == 0x20) ? 1 : 0;

	/* deselect drive and turn motor off */
	bus_space_write_1(t, h, FDREG77_DOR, FDO_FRST | FDO_DS);

	return (ok);
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

	timeout_set(&fd->sc_motoron_to, fd_motor_on, fd);
	timeout_set(&fd->sc_motoroff_to, fd_motor_off, fd);

	/* XXX Allow `flags' to override device type? */

	if (type)
		printf(": %s %d cyl, %d head, %d sec\n", type->name,
		    type->tracks, type->heads, type->sectrac);
	else
		printf(": density unknown\n");

	fd->sc_cylin = -1;
	fd->sc_drive = drive;
	fd->sc_deftype = type;
	fdc->sc_fd[drive] = fd;

	fdc_wrfifo(fdc, NE7CMD_SPECIFY);
	fdc_wrfifo(fdc, type->steprate);
	/* XXX head load time == 6ms */
	fdc_wrfifo(fdc, 6 | NE7_SPECIFY_NODMA);

	/*
	 * Initialize and attach the disk structure.
	 */
	fd->sc_dk.dk_flags = DKF_NOLABELREAD;
	fd->sc_dk.dk_name = fd->sc_dv.dv_xname;
	bufq_init(&fd->sc_bufq, BUFQ_DEFAULT);
	disk_attach(&fd->sc_dv, &fd->sc_dk);
}

int
fdactivate(struct device *self, int act)
{
	int ret = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		/* Make sure the drive motor gets turned off at shutdown time. */
		fd_motor_off(self);
		break;
	}

	return (ret);
}

__inline struct fd_type *
fd_dev_to_type(struct fd_softc *fd, dev_t dev)
{
	int type = FDTYPE(dev);

	if (type > (sizeof(fd_types) / sizeof(fd_types[0])))
		return (NULL);
	return (type ? &fd_types[type - 1] : fd->sc_deftype);
}

void
fdstrategy(struct buf *bp)
{
	struct fd_softc *fd;
	int unit = FDUNIT(bp->b_dev);
	int sz;
 	int s;

	/* Valid unit, controller, and request? */
	if (unit >= fd_cd.cd_ndevs ||
	    (fd = fd_cd.cd_devs[unit]) == 0 ||
	    bp->b_blkno < 0 ||
	    (((bp->b_bcount % FD_BSIZE(fd)) != 0 ||
	      (bp->b_blkno * DEV_BSIZE) % FD_BSIZE(fd) != 0) &&
	     (bp->b_flags & B_FORMAT) == 0)) {
		bp->b_error = EINVAL;
		goto bad;
	}

	/* If it's a null transfer, return immediately. */
	if (bp->b_bcount == 0)
		goto done;

	bp->b_resid = bp->b_bcount;
	sz = howmany(bp->b_bcount, DEV_BSIZE);

	if (bp->b_blkno + sz > (fd->sc_type->size * DEV_BSIZE) / FD_BSIZE(fd)) {
		sz = (fd->sc_type->size * DEV_BSIZE) / FD_BSIZE(fd)
		     - bp->b_blkno;
		if (sz == 0) {
			/* If exactly at end of disk, return EOF. */
			goto done;
		}
		if (sz < 0) {
			/* If past end of disk, return EINVAL. */
			bp->b_error = EINVAL;
			goto bad;
		}
		/* Otherwise, truncate request. */
		bp->b_bcount = sz << DEV_BSHIFT;
	}

#ifdef FD_DEBUG
	if (fdc_debug > 1)
	    printf("fdstrategy: b_blkno %lld b_bcount %d blkno %lld\n",
		    (long long)bp->b_blkno, bp->b_bcount,
		    (long long)fd->sc_blkno);
#endif

	/* Queue transfer */
	bufq_queue(&fd->sc_bufq, bp);

	/* Queue transfer on drive, activate drive and controller if idle. */
	s = splbio();
	timeout_del(&fd->sc_motoroff_to);		/* a good idea */
	if (fd->sc_bp == NULL)
		fdstart(fd);
#ifdef DIAGNOSTIC
	else {
		struct fdc_softc *fdc = (void *)fd->sc_dv.dv_parent;
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
	s = splbio();
	biodone(bp);
	splx(s);
}

void
fdstart(struct fd_softc *fd)
{
	struct fdc_softc *fdc = (void *)fd->sc_dv.dv_parent;
	int active = !TAILQ_EMPTY(&fdc->sc_drives);

	/* Link into controller queue. */
	fd->sc_bp = bufq_dequeue(&fd->sc_bufq);
	TAILQ_INSERT_TAIL(&fdc->sc_drives, fd, sc_drivechain);

	/* If controller not already active, start it. */
	if (!active)
		fdcstart(fdc);
}

void
fdfinish(struct fd_softc *fd, struct buf *bp)
{
	struct fdc_softc *fdc = (void *)fd->sc_dv.dv_parent;

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
		TAILQ_REMOVE(&fdc->sc_drives, fd, sc_drivechain);
		if (fd->sc_bp != NULL)
			TAILQ_INSERT_TAIL(&fdc->sc_drives, fd, sc_drivechain);
	}

	biodone(bp);
	/* turn off motor 5s from now */
	timeout_add_sec(&fd->sc_motoroff_to, 5);
	fdc->sc_state = DEVIDLE;
}

void
fdc_reset(struct fdc_softc *fdc)
{
	bus_space_tag_t t = fdc->sc_bustag;
	bus_space_handle_t h = fdc->sc_handle;

	bus_space_write_1(t, h, FDREG77_DOR, FDO_FDMAEN | FDO_MOEN(0));

	bus_space_write_1(t, h, FDREG77_DRS, DRS_RESET);
	delay(10);
	bus_space_write_1(t, h, FDREG77_DRS, 0);

	bus_space_write_1(t, h, FDREG77_DOR,
	    FDO_FRST | FDO_FDMAEN | FDO_DS);
#ifdef FD_DEBUG
	if (fdc_debug)
		printf("fdc reset\n");
#endif
}

void
fd_set_motor(struct fdc_softc *fdc)
{
	struct fd_softc *fd;
	u_char status;
	int n;

	status = FDO_FRST | FDO_FDMAEN;
	if ((fd = TAILQ_FIRST(&fdc->sc_drives)) != NULL)
		status |= fd->sc_drive;

	for (n = 0; n < 4; n++)
		if ((fd = fdc->sc_fd[n]) && (fd->sc_flags & FD_MOTOR))
			status |= FDO_MOEN(n);
	bus_space_write_1(fdc->sc_bustag, fdc->sc_handle,
	    FDREG77_DOR, status);
}

void
fd_motor_off(void *arg)
{
	struct fd_softc *fd = arg;
	int s;

	s = splbio();
	fd->sc_flags &= ~(FD_MOTOR | FD_MOTOR_WAIT);
	fd_set_motor((struct fdc_softc *)fd->sc_dv.dv_parent);
	splx(s);
}

void
fd_motor_on(void *arg)
{
	struct fd_softc *fd = arg;
	struct fdc_softc *fdc = (void *)fd->sc_dv.dv_parent;
	int s;

	s = splbio();
	fd->sc_flags &= ~FD_MOTOR_WAIT;
	if (fd == TAILQ_FIRST(&fdc->sc_drives) && fdc->sc_state == MOTORWAIT)
		(void) fdcstate(fdc);
	splx(s);
}

/*
 * Get status bytes off the FDC after a command has finished
 * Returns the number of status bytes read; -1 on error.
 * The return value is also stored in `sc_nstat'.
 */
int
fdcresult(struct fdc_softc *fdc)
{
	bus_space_tag_t t = fdc->sc_bustag;
	bus_space_handle_t h = fdc->sc_handle;
	int j, n = 0;

	for (j = 100000; j; j--) {
		u_int8_t v = bus_space_read_1(t, h, FDREG77_MSR);
		v &= (NE7_DIO | NE7_RQM | NE7_CB);
		if (v == NE7_RQM)
			return (fdc->sc_nstat = n);
		if (v == (NE7_DIO | NE7_RQM | NE7_CB)) {
			if (n >= sizeof(fdc->sc_status)) {
				log(LOG_ERR, "fdcresult: overrun\n");
				return (-1);
			}
			fdc->sc_status[n++] =
			    bus_space_read_1(t, h, FDREG77_FIFO);
		} else
			delay(1);
	}

	log(LOG_ERR, "fdcresult: timeout\n");
	return (fdc->sc_nstat = -1);
}

/*
 * Write a command byte to the FDC.
 * Returns 0 on success; -1 on failure (i.e. timeout)
 */
int
fdc_wrfifo(struct fdc_softc *fdc, u_int8_t x)
{
	bus_space_tag_t t = fdc->sc_bustag;
	bus_space_handle_t h = fdc->sc_handle;
	int i;

	for (i = 100000; i-- != 0;) {
		u_int8_t v = bus_space_read_1(t, h, FDREG77_MSR);
		if ((v & (NE7_DIO|NE7_RQM)) == NE7_RQM) {
			/* The chip is ready */
			bus_space_write_1(t, h, FDREG77_FIFO, x);
			return (0);
		}
		delay(1);
	}
	return (-1);
}

int
fdc_diskchange(struct fdc_softc *fdc)
{
	bus_space_tag_t t = fdc->sc_bustag;
	bus_space_handle_t h = fdc->sc_handle;

	u_int8_t v = bus_space_read_1(t, h, FDREG77_DIR);
	return ((v & FDI_DCHG) != 0);
}

int
fdopen(dev_t dev, int flags, int fmt, struct proc *p)
{
 	int unit, pmask;
	struct fd_softc *fd;
	struct fd_type *type;

	unit = FDUNIT(dev);
	if (unit >= fd_cd.cd_ndevs)
		return (ENXIO);
	fd = fd_cd.cd_devs[unit];
	if (fd == NULL)
		return (ENXIO);
	type = fd_dev_to_type(fd, dev);
	if (type == NULL)
		return (ENXIO);

	if ((fd->sc_flags & FD_OPEN) != 0 &&
	    fd->sc_type != type)
		return (EBUSY);

	fd->sc_type = type;
	fd->sc_cylin = -1;
	fd->sc_flags |= FD_OPEN;

	/*
	 * Only update the disklabel if we're not open anywhere else.
	 */
	if (fd->sc_dk.dk_openmask == 0)
		fdgetdisklabel(dev, fd, fd->sc_dk.dk_label, 0);

	pmask = (1 << DISKPART(dev));

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

	return (0);
}

int
fdclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct fd_softc *fd = fd_cd.cd_devs[FDUNIT(dev)];
	int pmask = (1 << DISKPART(dev));

	fd->sc_flags &= ~FD_OPEN;
	fd->sc_opts &= ~(FDOPT_NORETRY|FDOPT_SILENT);

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

int
fdread(dev_t dev, struct uio *uio, int flag)
{

        return (physio(fdstrategy, dev, B_READ, minphys, uio));
}

int
fdwrite(dev_t dev, struct uio *uio, int flag)
{

        return (physio(fdstrategy, dev, B_WRITE, minphys, uio));
}

void
fdcstart(struct fdc_softc *fdc)
{

#ifdef DIAGNOSTIC
	/* only got here if controller's drive queue was inactive; should
	   be in idle state */
	if (fdc->sc_state != DEVIDLE) {
		printf("fdcstart: not idle\n");
		return;
	}
#endif
	(void) fdcstate(fdc);
}

void
fdcstatus(struct fdc_softc *fdc, char *s)
{
	struct fd_softc *fd = TAILQ_FIRST(&fdc->sc_drives);
	int n;

	/* Just print last status */
	n = fdc->sc_nstat;

#if 0
	if (n == 0) {
		fdc_wrfifo(fdc, NE7CMD_SENSEI);
		(void) fdcresult(fdc);
		n = 2;
	}
#endif

	printf("%s: %s: state %d",
		fd ? fd->sc_dv.dv_xname : "fdc", s, fdc->sc_state);

	switch (n) {
	case 0:
		printf("\n");
		break;
	case 2:
		printf(" (st0 %b cyl %d)\n",
		    fdc->sc_status[0], NE7_ST0BITS,
		    fdc->sc_status[1]);
		break;
	case 7:
		printf(" (st0 %b st1 %b st2 %b cyl %d head %d sec %d)\n",
		    fdc->sc_status[0], NE7_ST0BITS,
		    fdc->sc_status[1], NE7_ST1BITS,
		    fdc->sc_status[2], NE7_ST2BITS,
		    fdc->sc_status[3], fdc->sc_status[4], fdc->sc_status[5]);
		break;
#ifdef DIAGNOSTIC
	default:
		printf(" fdcstatus: weird size: %d\n", n);
		break;
#endif
	}
}

void
fdctimeout(void *arg)
{
	struct fdc_softc *fdc = arg;
	struct fd_softc *fd;
	int s;

	s = splbio();
	fd = TAILQ_FIRST(&fdc->sc_drives);
	if (fd == NULL) {
		printf("%s: timeout but no I/O pending: state %d, istatus=%d\n",
		    fdc->sc_dev.dv_xname, fdc->sc_state, fdc->sc_istatus);
		fdc->sc_state = DEVIDLE;
		goto out;
	}

	if (fd->sc_bp != NULL)
		fdc->sc_state++;
	else
		fdc->sc_state = DEVIDLE;

	(void) fdcstate(fdc);
out:
	splx(s);

}

void
fdcpseudointr(void *arg)
{
	struct fdc_softc *fdc = arg;
	int s;

	/* Just ensure it has the right spl. */
	s = splbio();
	(void) fdcstate(fdc);
	splx(s);
}


/*
 * Hardware interrupt entry point.
 * Unfortunately, we have no reliable way to determine that the
 * interrupt really came from the floppy controller; just hope
 * that the other devices that share this interrupt can do better..
 */
int
fdchwintr(void *arg)
{
	struct fdc_softc *fdc = arg;
	bus_space_tag_t t = fdc->sc_bustag;
	bus_space_handle_t h = fdc->sc_handle;

	switch (fdc->sc_itask) {
	case FDC_ITASK_NONE:
		return (0);
	case FDC_ITASK_SENSEI:
		if (fdc_wrfifo(fdc, NE7CMD_SENSEI) != 0 || fdcresult(fdc) == -1)
			fdc->sc_istatus = FDC_ISTATUS_ERROR;
		else
			fdc->sc_istatus = FDC_ISTATUS_DONE;
		softintr_schedule_raw(fdc->sc_sicookie);
		return (1);
	case FDC_ITASK_RESULT:
		if (fdcresult(fdc) == -1)
			fdc->sc_istatus = FDC_ISTATUS_ERROR;
		else
			fdc->sc_istatus = FDC_ISTATUS_DONE;
		softintr_schedule_raw(fdc->sc_sicookie);
		return (1);
	case FDC_ITASK_DMA:
		/* Proceed with pseudo-DMA below */
		break;
	default:
		printf("fdc: stray hard interrupt: itask=%d\n", fdc->sc_itask);
		fdc->sc_istatus = FDC_ISTATUS_SPURIOUS;
		softintr_schedule_raw(fdc->sc_sicookie);
		return (1);
	}

	/*
	 * Pseudo DMA in progress
	 */
	for (;;) {
		u_int8_t msr;

		msr = bus_space_read_1(t, h, FDREG77_MSR);

		if ((msr & NE7_RQM) == 0)
			/* That's all this round */
			break;

		if ((msr & NE7_NDM) == 0) {
			fdcresult(fdc);
			fdc->sc_istatus = FDC_ISTATUS_DONE;
			softintr_schedule_raw(fdc->sc_sicookie);
#ifdef FD_DEBUG
			if (fdc_debug > 1)
				printf("fdc: overrun: msr = %x, tc = %d\n",
				    msr, fdc->sc_tc);
#endif
			break;
		}

		/* Another byte can be transferred */
		if ((msr & NE7_DIO) != 0)
			*fdc->sc_data =
			     bus_space_read_1(t, h, FDREG77_FIFO);
		else
			bus_space_write_1(t, h, FDREG77_FIFO,
			    *fdc->sc_data);

		fdc->sc_data++;
		if (--fdc->sc_tc == 0) {
			fdc->sc_istatus = FDC_ISTATUS_DONE;
			FTC_FLIP;
			fdcresult(fdc);
			softintr_schedule_raw(fdc->sc_sicookie);
			break;
		}
	}
	return (1);
}

void
fdcswintr(void *arg)
{
	struct fdc_softc *fdc = arg;
	int s;

	if (fdc->sc_istatus == FDC_ISTATUS_NONE)
		/* This (software) interrupt is not for us */
		return;

	switch (fdc->sc_istatus) {
	case FDC_ISTATUS_ERROR:
		printf("fdc: ierror status: state %d\n", fdc->sc_state);
		break;
	case FDC_ISTATUS_SPURIOUS:
		printf("fdc: spurious interrupt: state %d\n", fdc->sc_state);
		break;
	}

	s = splbio();
	fdcstate(fdc);
	splx(s);
	return;
}

int
fdcstate(struct fdc_softc *fdc)
{
#define	st0	fdc->sc_status[0]
#define	st1	fdc->sc_status[1]
#define	cyl	fdc->sc_status[1]
#define FDC_WRFIFO(fdc, c) \
	do {						\
		if (fdc_wrfifo(fdc, (c))) {		\
			goto xxx;			\
		}					\
	} while(0)

	struct fd_softc *fd;
	struct buf *bp;
	int read, head, sec, nblks, cylin;
	struct fd_type *type;
	struct fd_formb *finfo = NULL;

	if (fdc->sc_istatus == FDC_ISTATUS_ERROR) {
		/* Prevent loop if the reset sequence produces errors */
		if (fdc->sc_state != RESETCOMPLETE &&
		    fdc->sc_state != RECALWAIT &&
		    fdc->sc_state != RECALCOMPLETE)
			fdc->sc_state = DORESET;
	}

	/* Clear I task/status field */
	fdc->sc_istatus = FDC_ISTATUS_NONE;
	fdc->sc_itask = FDC_ITASK_NONE;

loop:
	/* Is there a drive for the controller to do a transfer with? */
	fd = TAILQ_FIRST(&fdc->sc_drives);
	if (fd == NULL) {
		fdc->sc_state = DEVIDLE;
 		return (0);
	}

	/* Is there a transfer to this drive?  If not, deactivate drive. */
	bp = fd->sc_bp;
	if (bp == NULL) {
		fd->sc_ops = 0;
		TAILQ_REMOVE(&fdc->sc_drives, fd, sc_drivechain);
		goto loop;
	}

	if (bp->b_flags & B_FORMAT)
		finfo = (struct fd_formb *)bp->b_data;

	cylin = ((bp->b_blkno * DEV_BSIZE) - (bp->b_bcount - bp->b_resid)) /
	    (FD_BSIZE(fd) * fd->sc_type->seccyl);

	switch (fdc->sc_state) {
	case DEVIDLE:
		fdc->sc_errors = 0;
		fd->sc_skip = 0;
		fd->sc_bcount = bp->b_bcount;
		fd->sc_blkno = (bp->b_blkno * DEV_BSIZE) / FD_BSIZE(fd);
		timeout_del(&fd->sc_motoroff_to);
		if ((fd->sc_flags & FD_MOTOR_WAIT) != 0) {
			fdc->sc_state = MOTORWAIT;
			return (1);
		}
		if ((fd->sc_flags & FD_MOTOR) == 0) {
			/* Turn on the motor, being careful about pairing. */
			struct fd_softc *ofd = fdc->sc_fd[fd->sc_drive ^ 1];
			if (ofd && ofd->sc_flags & FD_MOTOR) {
				timeout_del(&ofd->sc_motoroff_to);
				ofd->sc_flags &= ~(FD_MOTOR | FD_MOTOR_WAIT);
			}
			fd->sc_flags |= FD_MOTOR | FD_MOTOR_WAIT;
			fd_set_motor(fdc);
			fdc->sc_state = MOTORWAIT;
			if ((fdc->sc_flags & FDC_NEEDMOTORWAIT) != 0) { /*XXX*/
				/* Allow .25s for motor to stabilize. */
				timeout_add_msec(&fd->sc_motoron_to, 250);
			} else {
				fd->sc_flags &= ~FD_MOTOR_WAIT;
				goto loop;
			}
			return (1);
		}
		/* Make sure the right drive is selected. */
		fd_set_motor(fdc);

		if (fdc_diskchange(fdc))
			goto dodskchg;

		/*FALLTHROUGH*/
	case DOSEEK:
	doseek:
		if ((fdc->sc_flags & FDC_EIS) &&
		    (bp->b_flags & B_FORMAT) == 0) {
			fd->sc_cylin = cylin;
			/* We use implied seek */
			goto doio;
		}

		if (fd->sc_cylin == cylin)
			goto doio;

		fd->sc_cylin = -1;
		fdc->sc_state = SEEKWAIT;
		fdc->sc_nstat = 0;

		fd->sc_dk.dk_seek++;

		disk_busy(&fd->sc_dk);
		timeout_add_sec(&fdc->fdctimeout_to, 4);

		/* specify command */
		FDC_WRFIFO(fdc, NE7CMD_SPECIFY);
		FDC_WRFIFO(fdc, fd->sc_type->steprate);
		/* XXX head load time == 6ms */
		FDC_WRFIFO(fdc, 6 | NE7_SPECIFY_NODMA);

		fdc->sc_itask = FDC_ITASK_SENSEI;
		/* seek function */
		FDC_WRFIFO(fdc, NE7CMD_SEEK);
		FDC_WRFIFO(fdc, fd->sc_drive); /* drive number */
		FDC_WRFIFO(fdc, cylin * fd->sc_type->step);
		return (1);

	case DODSKCHG:
	dodskchg:
		/*
		 * Disk change: force a seek operation by going to cyl 1
		 * followed by a recalibrate.
		 */
		disk_busy(&fd->sc_dk);
		timeout_add_sec(&fdc->fdctimeout_to, 4);
		fd->sc_cylin = -1;
		fdc->sc_nstat = 0;
		fdc->sc_state = DSKCHGWAIT;

		fdc->sc_itask = FDC_ITASK_SENSEI;
		/* seek function */
		FDC_WRFIFO(fdc, NE7CMD_SEEK);
		FDC_WRFIFO(fdc, fd->sc_drive); /* drive number */
		FDC_WRFIFO(fdc, 1 * fd->sc_type->step);
		return (1);

	case DSKCHGWAIT:
		timeout_del(&fdc->fdctimeout_to);
		disk_unbusy(&fd->sc_dk, 0, 0, 0);
		if (fdc->sc_nstat != 2 || (st0 & 0xf8) != 0x20 ||
		    cyl != 1 * fd->sc_type->step) {
			fdcstatus(fdc, "dskchg seek failed");
			fdc->sc_state = DORESET;
		} else
			fdc->sc_state = DORECAL;

		if (fdc_diskchange(fdc)) {
			printf("%s: cannot clear disk change status\n",
				fdc->sc_dev.dv_xname);
			fdc->sc_state = DORESET;
		}
		goto loop;

	case DOIO:
	doio:
		if (finfo != NULL)
			fd->sc_skip = (char *)&(finfo->fd_formb_cylno(0)) -
				      (char *)finfo;
		type = fd->sc_type;
		sec = fd->sc_blkno % type->seccyl;
		nblks = type->seccyl - sec;
		nblks = min(nblks, fd->sc_bcount / FD_BSIZE(fd));
		nblks = min(nblks, FDC_MAXIOSIZE / FD_BSIZE(fd));
		fd->sc_nblks = nblks;
		fd->sc_nbytes = finfo ? bp->b_bcount : nblks * FD_BSIZE(fd);
		head = sec / type->sectrac;
		sec -= head * type->sectrac;
#ifdef DIAGNOSTIC
		{
			daddr_t block;

			block = (fd->sc_cylin * type->heads + head) *
			    type->sectrac + sec;
			if (block != fd->sc_blkno) {
				printf("fdcintr: block %lld != blkno %lld\n",
				    (long long)block, (long long)fd->sc_blkno);
#if defined(FD_DEBUG) && defined(DDB)
				 db_enter();
#endif
			}
		}
#endif
		read = bp->b_flags & B_READ;

		/* Setup for pseudo DMA */
		fdc->sc_data = bp->b_data + fd->sc_skip;
		fdc->sc_tc = fd->sc_nbytes;

		bus_space_write_1(fdc->sc_bustag, fdc->sc_handle,
		    FDREG77_DRS, type->rate);
#ifdef FD_DEBUG
		if (fdc_debug > 1)
			printf("fdcstate: doio: %s drive %d "
				"track %d head %d sec %d nblks %d\n",
				finfo ? "format" :
					(read ? "read" : "write"),
				fd->sc_drive, fd->sc_cylin, head, sec, nblks);
#endif
		fdc->sc_state = IOCOMPLETE;
		fdc->sc_itask = FDC_ITASK_DMA;
		fdc->sc_nstat = 0;

		disk_busy(&fd->sc_dk);

		/* allow 3 seconds for operation */
		timeout_add_sec(&fdc->fdctimeout_to, 3);

		if (finfo != NULL) {
			/* formatting */
			FDC_WRFIFO(fdc, NE7CMD_FORMAT);
			FDC_WRFIFO(fdc, (head << 2) | fd->sc_drive);
			FDC_WRFIFO(fdc, finfo->fd_formb_secshift);
			FDC_WRFIFO(fdc, finfo->fd_formb_nsecs);
			FDC_WRFIFO(fdc, finfo->fd_formb_gaplen);
			FDC_WRFIFO(fdc, finfo->fd_formb_fillbyte);
		} else {
			if (read)
				FDC_WRFIFO(fdc, NE7CMD_READ);
			else
				FDC_WRFIFO(fdc, NE7CMD_WRITE);
			FDC_WRFIFO(fdc, (head << 2) | fd->sc_drive);
			FDC_WRFIFO(fdc, fd->sc_cylin);	/*track*/
			FDC_WRFIFO(fdc, head);
			FDC_WRFIFO(fdc, sec + 1);	/*sector+1*/
			FDC_WRFIFO(fdc, type->secsize);	/*sector size*/
			FDC_WRFIFO(fdc, type->sectrac);	/*secs/track*/
			FDC_WRFIFO(fdc, type->gap1);	/*gap1 size*/
			FDC_WRFIFO(fdc, type->datalen);	/*data length*/
		}

		return (1);				/* will return later */

	case SEEKWAIT:
		timeout_del(&fdc->fdctimeout_to);
		fdc->sc_state = SEEKCOMPLETE;
		if (fdc->sc_flags & FDC_NEEDHEADSETTLE) {
			/* allow 1/50 second for heads to settle */
			timeout_add_msec(&fdc->fdcpseudointr_to, 20);
			return (1);		/* will return later */
		}
		/*FALLTHROUGH*/
	case SEEKCOMPLETE:
		/* no data on seek */
		disk_unbusy(&fd->sc_dk, 0, 0, 0);

		/* Make sure seek really happened. */
		if (fdc->sc_nstat != 2 || (st0 & 0xf8) != 0x20 ||
		    cyl != cylin * fd->sc_type->step) {
#ifdef FD_DEBUG
			if (fdc_debug)
				fdcstatus(fdc, "seek failed");
#endif
			fdcretry(fdc);
			goto loop;
		}
		fd->sc_cylin = cylin;
		goto doio;

	case IOTIMEDOUT:
		/*
		 * Try to abort the I/O operation without resetting
		 * the chip first.  Poke TC and arrange to pick up
		 * the timed out I/O command's status.
		 */
		fdc->sc_itask = FDC_ITASK_RESULT;
		fdc->sc_state = IOCLEANUPWAIT;
		fdc->sc_nstat = 0;
		/* 1/10 second should be enough */
		timeout_add_msec(&fdc->fdctimeout_to, 100);
		return (1);

	case IOCLEANUPTIMEDOUT:
	case SEEKTIMEDOUT:
	case RECALTIMEDOUT:
	case RESETTIMEDOUT:
	case DSKCHGTIMEDOUT:
		fdcstatus(fdc, "timeout");

		/* All other timeouts always roll through to a chip reset */
		fdcretry(fdc);

		/* Force reset, no matter what fdcretry() says */
		fdc->sc_state = DORESET;
		goto loop;

	case IOCLEANUPWAIT: /* IO FAILED, cleanup succeeded */
		timeout_del(&fdc->fdctimeout_to);
		disk_unbusy(&fd->sc_dk, (bp->b_bcount - bp->b_resid),
		    bp->b_blkno, (bp->b_flags & B_READ));
		fdcretry(fdc);
		goto loop;

	case IOCOMPLETE: /* IO DONE, post-analyze */
		timeout_del(&fdc->fdctimeout_to);

		disk_unbusy(&fd->sc_dk, (bp->b_bcount - bp->b_resid),
		    bp->b_blkno, (bp->b_flags & B_READ));

		if (fdc->sc_nstat != 7 || st1 != 0 ||
		    ((st0 & 0xf8) != 0 &&
		     ((st0 & 0xf8) != 0x20 || (fdc->sc_cfg & CFG_EIS) == 0))) {
#ifdef FD_DEBUG
			if (fdc_debug) {
				fdcstatus(fdc,
					bp->b_flags & B_READ
					? "read failed" : "write failed");
				printf("blkno %lld nblks %d nstat %d tc %d\n",
				       (long long)fd->sc_blkno, fd->sc_nblks,
				       fdc->sc_nstat, fdc->sc_tc);
			}
#endif
			if (fdc->sc_nstat == 7 &&
			    (st1 & ST1_OVERRUN) == ST1_OVERRUN) {

				/*
				 * Silently retry overruns if no other
				 * error bit is set. Adjust threshold.
				 */
				int thr = fdc->sc_cfg & CFG_THRHLD_MASK;
				if (thr < 15) {
					thr++;
					fdc->sc_cfg &= ~CFG_THRHLD_MASK;
					fdc->sc_cfg |= (thr & CFG_THRHLD_MASK);
#ifdef FD_DEBUG
					if (fdc_debug)
						printf("fdc: %d -> threshold\n", thr);
#endif
					fdconf(fdc);
					fdc->sc_overruns = 0;
				}
				if (++fdc->sc_overruns < 3) {
					fdc->sc_state = DOIO;
					goto loop;
				}
			}
			fdcretry(fdc);
			goto loop;
		}
		if (fdc->sc_errors) {
			diskerr(bp, "fd", "soft error", LOG_PRINTF,
			    fd->sc_skip / FD_BSIZE(fd),
			    (struct disklabel *)NULL);
			printf("\n");
			fdc->sc_errors = 0;
		} else {
			if (--fdc->sc_overruns < -20) {
				int thr = fdc->sc_cfg & CFG_THRHLD_MASK;
				if (thr > 0) {
					thr--;
					fdc->sc_cfg &= ~CFG_THRHLD_MASK;
					fdc->sc_cfg |= (thr & CFG_THRHLD_MASK);
#ifdef FD_DEBUG
					if (fdc_debug)
						printf("fdc: %d -> threshold\n", thr);
#endif
					fdconf(fdc);
				}
				fdc->sc_overruns = 0;
			}
		}
		fd->sc_blkno += fd->sc_nblks;
		fd->sc_skip += fd->sc_nbytes;
		fd->sc_bcount -= fd->sc_nbytes;
		bp->b_resid -= fd->sc_nbytes;
		if (finfo == NULL && fd->sc_bcount > 0) {
			cylin = fd->sc_blkno / fd->sc_type->seccyl;
			goto doseek;
		}
		fdfinish(fd, bp);
		goto loop;

	case DORESET:
		/* try a reset, keep motor on */
		fd_set_motor(fdc);
		delay(100);
		fdc->sc_nstat = 0;
		fdc->sc_itask = FDC_ITASK_SENSEI;
		fdc->sc_state = RESETCOMPLETE;
		timeout_add_msec(&fdc->fdctimeout_to, 500);
		fdc_reset(fdc);
		return (1);			/* will return later */

	case RESETCOMPLETE:
		timeout_del(&fdc->fdctimeout_to);
		fdconf(fdc);

		/* FALLTHROUGH */
	case DORECAL:
		fdc->sc_state = RECALWAIT;
		fdc->sc_itask = FDC_ITASK_SENSEI;
		fdc->sc_nstat = 0;
		timeout_add_sec(&fdc->fdctimeout_to, 5);
		/* recalibrate function */
		FDC_WRFIFO(fdc, NE7CMD_RECAL);
		FDC_WRFIFO(fdc, fd->sc_drive);
		return (1);			/* will return later */

	case RECALWAIT:
		timeout_del(&fdc->fdctimeout_to);
		fdc->sc_state = RECALCOMPLETE;
		if (fdc->sc_flags & FDC_NEEDHEADSETTLE) {
			/* allow 1/30 second for heads to settle */
			timeout_add_msec(&fdc->fdcpseudointr_to, 1000 / 30);
			return (1);		/* will return later */
		}

	case RECALCOMPLETE:
		if (fdc->sc_nstat != 2 || (st0 & 0xf8) != 0x20 || cyl != 0) {
#ifdef FD_DEBUG
			if (fdc_debug)
				fdcstatus(fdc, "recalibrate failed");
#endif
			fdcretry(fdc);
			goto loop;
		}
		fd->sc_cylin = 0;
		goto doseek;

	case MOTORWAIT:
		if (fd->sc_flags & FD_MOTOR_WAIT)
			return (1);		/* time's not up yet */
		goto doseek;

	default:
		fdcstatus(fdc, "stray interrupt");
		return (1);
	}
#ifdef DIAGNOSTIC
	panic("fdcintr: impossible");
#endif

xxx:
	/*
	 * We get here if the chip locks up in FDC_WRFIFO()
	 * Cancel any operation and schedule a reset
	 */
	timeout_del(&fdc->fdctimeout_to);
	fdcretry(fdc);
	fdc->sc_state = DORESET;
	goto loop;

#undef	st0
#undef	st1
#undef	cyl
}

void
fdcretry(struct fdc_softc *fdc)
{
	struct fd_softc *fd;
	struct buf *bp;
	int error = EIO;

	fd = TAILQ_FIRST(&fdc->sc_drives);
	bp = fd->sc_bp;

	fdc->sc_overruns = 0;
	if (fd->sc_opts & FDOPT_NORETRY)
		goto fail;

	switch (fdc->sc_errors) {
	case 0:
		if (fdc->sc_nstat == 7 &&
		    (fdc->sc_status[0] & 0xd8) == 0x40 &&
		    (fdc->sc_status[1] & 0x2) == 0x2) {
			printf("%s: read-only medium\n", fd->sc_dv.dv_xname);
			error = EROFS;
			goto failsilent;
		}
		/* try again */
		fdc->sc_state =
		    (fdc->sc_flags & FDC_EIS) ? DOIO : DOSEEK;
		break;

	case 1: case 2: case 3:
		/* didn't work; try recalibrating */
		fdc->sc_state = DORECAL;
		break;

	case 4:
		if (fdc->sc_nstat == 7 &&
		    fdc->sc_status[0] == 0 &&
		    fdc->sc_status[1] == 0 &&
		    fdc->sc_status[2] == 0) {
			/*
			 * We've retried a few times and we've got
			 * valid status and all three status bytes
			 * are zero.  Assume this condition is the
			 * result of no disk loaded into the drive.
			 */
			printf("%s: no medium?\n", fd->sc_dv.dv_xname);
			error = ENODEV;
			goto failsilent;
		}

		/* still no go; reset the bastard */
		fdc->sc_state = DORESET;
		break;

	default:
	fail:
		if ((fd->sc_opts & FDOPT_SILENT) == 0) {
			diskerr(bp, "fd", "hard error", LOG_PRINTF,
				fd->sc_skip / FD_BSIZE(fd),
				(struct disklabel *)NULL);
			printf("\n");
			fdcstatus(fdc, "controller status");
		}

	failsilent:
		bp->b_flags |= B_ERROR;
		bp->b_error = error;
		bp->b_resid = bp->b_bcount;
		fdfinish(fd, bp);
	}
	fdc->sc_errors++;
}

daddr_t
fdsize(dev_t dev)
{

	/* Swapping to floppies would not make sense. */
	return (-1);
}

int
fddump(dev_t dev, daddr_t blkno, caddr_t va, size_t size)
{

	/* Not implemented. */
	return (EINVAL);
}

int
fdioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct fd_softc *fd;
	struct fdc_softc *fdc;
	struct disklabel *lp;
	int unit;
	int error;
#ifdef FD_DEBUG
	int i;
#endif

	unit = FDUNIT(dev);
	if (unit >= fd_cd.cd_ndevs)
		return (ENXIO);

	fd = fd_cd.cd_devs[FDUNIT(dev)];
	fdc = (struct fdc_softc *)fd->sc_dv.dv_parent;

	switch (cmd) {
	case DIOCRLDINFO:
		lp = malloc(sizeof(*lp), M_TEMP, M_WAITOK);
		fdgetdisklabel(dev, fd, lp, 0);
		bcopy(lp, fd->sc_dk.dk_label, sizeof(*lp));
		free(lp, M_TEMP, 0);
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
		    &fd->sc_dk.dk_label->d_partitions[DISKPART(dev)];
		return 0;

	case DIOCWDINFO:
	case DIOCSDINFO:
		if ((flag & FWRITE) == 0)
			return (EBADF);

		error = setdisklabel(fd->sc_dk.dk_label,
		    (struct disklabel *)addr, 0);
		if (error == 0) {
			if (cmd == DIOCWDINFO)
				error = writedisklabel(DISKLABELDEV(dev),
				    fdstrategy, fd->sc_dk.dk_label);
		}
		return (error);

	case DIOCLOCK:
		/*
		 * Nothing to do here, really.
		 */
		return (0);

	case MTIOCTOP:
		if (((struct mtop *)addr)->mt_op != MTOFFL)
			return (EIO);
		/* FALLTHROUGH */
	case DIOCEJECT:
		if (fdc->sc_flags & FDC_NOEJECT)
			return (ENODEV);
		fd_do_eject(fd);
		return (0);

	case FD_FORM:
		if ((flag & FWRITE) == 0)
			return (EBADF);	/* must be opened for writing */
		else if (((struct fd_formb *)addr)->format_version !=
		    FD_FORMAT_VERSION)
			return (EINVAL);/* wrong version of formatting prog */
		else
			return fdformat(dev, (struct fd_formb *)addr, p);
		break;

	case FD_GTYPE:		/* get drive options */
		*(struct fd_type *)addr = *fd->sc_type;
		return (0);

	case FD_GOPTS:		/* get drive options */
		*(int *)addr = fd->sc_opts;
		return (0);

	case FD_SOPTS:		/* set drive options */
		fd->sc_opts = *(int *)addr;
		return (0);

#ifdef FD_DEBUG
	case _IO('f', 100):
		fdc_wrfifo(fdc, NE7CMD_DUMPREG);
		fdcresult(fdc);
		printf("fdc: dumpreg(%d regs): <", fdc->sc_nstat);
		for (i = 0; i < fdc->sc_nstat; i++)
			printf(" 0x%x", fdc->sc_status[i]);
		printf(">\n");
		return (0);

	case _IOW('f', 101, int):
		fdc->sc_cfg &= ~CFG_THRHLD_MASK;
		fdc->sc_cfg |= (*(int *)addr & CFG_THRHLD_MASK);
		fdconf(fdc);
		return (0);

	case _IO('f', 102):
		fdc_wrfifo(fdc, NE7CMD_SENSEI);
		fdcresult(fdc);
		printf("fdc: sensei(%d regs): <", fdc->sc_nstat);
		for (i=0; i< fdc->sc_nstat; i++)
			printf(" 0x%x", fdc->sc_status[i]);
		printf(">\n");
		return (0);
#endif
	default:
		return (ENOTTY);
	}
}

int
fdformat(dev_t dev, struct fd_formb *finfo, struct proc *p)
{
	int rv = 0;
	struct fd_softc *fd = fd_cd.cd_devs[FDUNIT(dev)];
	struct fd_type *type = fd->sc_type;
	struct buf *bp;

	/* set up a buffer header for fdstrategy() */
	bp = malloc(sizeof(*bp), M_TEMP, M_NOWAIT | M_ZERO);
	if (bp == NULL)
		return (ENOBUFS);

	bp->b_flags = B_BUSY | B_PHYS | B_FORMAT | B_RAW;
	bp->b_proc = p;
	bp->b_dev = dev;

	/*
	 * Calculate a fake blkno, so fdstrategy() would initiate a
	 * seek to the requested cylinder.
	 */
	bp->b_blkno = ((finfo->cyl * (type->sectrac * type->heads)
		       + finfo->head * type->sectrac) * FD_BSIZE(fd))
		      / DEV_BSIZE;

	bp->b_bcount = sizeof(struct fd_idfield_data) * finfo->fd_formb_nsecs;
	bp->b_data = (caddr_t)finfo;

#ifdef FD_DEBUG
	if (fdc_debug) {
		int i;

		printf("fdformat: blkno 0x%llx count %ld\n",
			(unsigned long long)bp->b_blkno, bp->b_bcount);

		printf("\tcyl:\t%d\n", finfo->cyl);
		printf("\thead:\t%d\n", finfo->head);
		printf("\tnsecs:\t%d\n", finfo->fd_formb_nsecs);
		printf("\tsshft:\t%d\n", finfo->fd_formb_secshift);
		printf("\tgaplen:\t%d\n", finfo->fd_formb_gaplen);
		printf("\ttrack data:");
		for (i = 0; i < finfo->fd_formb_nsecs; i++) {
			printf(" [c%d h%d s%d]",
					finfo->fd_formb_cylno(i),
					finfo->fd_formb_headno(i),
					finfo->fd_formb_secno(i) );
			if (finfo->fd_formb_secsize(i) != 2)
				printf("<sz:%d>", finfo->fd_formb_secsize(i));
		}
		printf("\n");
	}
#endif

	/* now do the format */
	fdstrategy(bp);

	/* ...and wait for it to complete */
	rv = biowait(bp);
	free(bp, M_TEMP, 0);
	return (rv);
}

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
	DL_SETDSIZE(lp, (u_int64_t)lp->d_secpercyl * lp->d_ncylinders);

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

void
fd_do_eject(struct fd_softc *fd)
{
	struct fdc_softc *fdc = (void *)fd->sc_dv.dv_parent;
	bus_space_tag_t t = fdc->sc_bustag;
	bus_space_handle_t h = fdc->sc_handle;
	u_int8_t dor = FDO_FRST | FDO_FDMAEN | FDO_MOEN(0);

	bus_space_write_1(t, h, FDREG77_DOR, dor | FDO_EJ);
	delay(10);
	bus_space_write_1(t, h, FDREG77_DOR, FDO_FRST | FDO_DS);
}
