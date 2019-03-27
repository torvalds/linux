/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2004 Poul-Henning Kamp
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Don Ahn.
 *
 * Libretto PCMCIA floppy support by David Horwitt (dhorwitt@ucsd.edu)
 * aided by the Linux floppy driver modifications from David Bateman
 * (dbateman@eng.uts.edu.au).
 *
 * Copyright (c) 1993, 1994 by
 *  jc@irbs.UUCP (John Capo)
 *  vak@zebub.msk.su (Serge Vakulenko)
 *  ache@astral.msk.su (Andrew A. Chernov)
 *
 * Copyright (c) 1993, 1994, 1995 by
 *  joerg_wunsch@uriah.sax.de (Joerg Wunsch)
 *  dufault@hda.com (Peter Dufault)
 *
 * Copyright (c) 2001 Joerg Wunsch,
 *  joerg_wunsch@uriah.heep.sax.de (Joerg Wunsch)
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
 *	from:	@(#)fd.c	7.4 (Berkeley) 5/25/91
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_fdc.h"

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/fcntl.h>
#include <sys/fdcio.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <geom/geom.h>

#include <machine/bus.h>
#include <machine/clock.h>
#include <machine/stdarg.h>

#include <isa/isavar.h>
#include <isa/isareg.h>
#include <isa/rtc.h>
#include <dev/fdc/fdcvar.h>

#include <dev/ic/nec765.h>

/*
 * Runtime configuration hints/flags
 */

/* configuration flags for fd */
#define FD_TYPEMASK	0x0f	/* drive type, matches enum
				 * fd_drivetype; on i386 machines, if
				 * given as 0, use RTC type for fd0
				 * and fd1 */
#define	FD_NO_CHLINE	0x10	/* drive does not support changeline
				 * aka. unit attention */
#define FD_NO_PROBE	0x20	/* don't probe drive (seek test), just
				 * assume it is there */

/*
 * Things that could conceiveably considered parameters or tweakables
 */

/*
 * Maximal number of bytes in a cylinder.
 * This is used for ISADMA bouncebuffer allocation and sets the max
 * xfersize we support.
 *
 * 2.88M format has 2 x 36 x 512, allow for hacked up density.
 */
#define MAX_BYTES_PER_CYL	(2 * 40 * 512)

/*
 * Timeout value for the PIO loops to wait until the FDC main status
 * register matches our expectations (request for master, direction
 * bit).  This is supposed to be a number of microseconds, although
 * timing might actually not be very accurate.
 *
 * Timeouts of 100 msec are believed to be required for some broken
 * (old) hardware.
 */
#define	FDSTS_TIMEOUT	100000

/*
 * After this many errors, stop whining.  Close will reset this count.
 */
#define FDC_ERRMAX	100

/*
 * AutoDensity search lists for each drive type.
 */

static struct fd_type fd_searchlist_360k[] = {
	{ FDF_5_360 },
	{ 0 }
};

static struct fd_type fd_searchlist_12m[] = {
	{ FDF_5_1200 | FL_AUTO },
	{ FDF_5_400 | FL_AUTO },
	{ FDF_5_360 | FL_2STEP | FL_AUTO},
	{ 0 }
};

static struct fd_type fd_searchlist_720k[] = {
	{ FDF_3_720 },
	{ 0 }
};

static struct fd_type fd_searchlist_144m[] = {
	{ FDF_3_1440 | FL_AUTO},
	{ FDF_3_720 | FL_AUTO},
	{ 0 }
};

static struct fd_type fd_searchlist_288m[] = {
	{ FDF_3_1440 | FL_AUTO },
#if 0
	{ FDF_3_2880 | FL_AUTO }, /* XXX: probably doesn't work */
#endif
	{ FDF_3_720 | FL_AUTO},
	{ 0 }
};

/*
 * Order must match enum fd_drivetype in <sys/fdcio.h>.
 */
static struct fd_type *fd_native_types[] = {
	NULL,				/* FDT_NONE */
	fd_searchlist_360k, 		/* FDT_360K */
	fd_searchlist_12m, 		/* FDT_12M */
	fd_searchlist_720k, 		/* FDT_720K */
	fd_searchlist_144m, 		/* FDT_144M */
	fd_searchlist_288m,		/* FDT_288M_1 (mapped to FDT_288M) */
	fd_searchlist_288m, 		/* FDT_288M */
};

/*
 * Internals start here
 */

/* registers */
#define	FDOUT	2	/* Digital Output Register (W) */
#define	FDO_FDSEL	0x03	/*  floppy device select */
#define	FDO_FRST	0x04	/*  floppy controller reset */
#define	FDO_FDMAEN	0x08	/*  enable floppy DMA and Interrupt */
#define	FDO_MOEN0	0x10	/*  motor enable drive 0 */
#define	FDO_MOEN1	0x20	/*  motor enable drive 1 */
#define	FDO_MOEN2	0x40	/*  motor enable drive 2 */
#define	FDO_MOEN3	0x80	/*  motor enable drive 3 */

#define	FDSTS	4	/* NEC 765 Main Status Register (R) */
#define FDDSR	4	/* Data Rate Select Register (W) */
#define	FDDATA	5	/* NEC 765 Data Register (R/W) */
#define	FDCTL	7	/* Control Register (W) */

/*
 * The YE-DATA PC Card floppies use PIO to read in the data rather
 * than DMA due to the wild variability of DMA for the PC Card
 * devices.  DMA was deleted from the PC Card specification in version
 * 7.2 of the standard, but that post-dates the YE-DATA devices by many
 * years.
 *
 * In addition, if we cannot setup the DMA resources for the ISA
 * attachment, we'll use this same offset for data transfer.  However,
 * that almost certainly won't work.
 *
 * For this mode, offset 0 and 1 must be used to setup the transfer
 * for this floppy.  This is OK for PC Card YE Data devices, but for
 * ISA this is likely wrong.  These registers are only available on
 * those systems that map them to the floppy drive.  Newer systems do
 * not do this, and we should likely prohibit access to them (or
 * disallow NODMA to be set).
 */
#define FDBCDR		0	/* And 1 */
#define FD_YE_DATAPORT	6	/* Drive Data port */

#define	FDI_DCHG	0x80	/* diskette has been changed */
				/* requires drive and motor being selected */
				/* is cleared by any step pulse to drive */

/*
 * We have three private BIO commands.
 */
#define BIO_PROBE	BIO_CMD0
#define BIO_RDID	BIO_CMD1
#define BIO_FMT		BIO_CMD2

/*
 * Per drive structure (softc).
 */
struct fd_data {
	u_char 	*fd_ioptr;	/* IO pointer */
	u_int	fd_iosize;	/* Size of IO chunks */
	u_int	fd_iocount;	/* Outstanding requests */
	struct	fdc_data *fdc;	/* pointer to controller structure */
	int	fdsu;		/* this units number on this controller */
	enum	fd_drivetype type; /* drive type */
	struct	fd_type *ft;	/* pointer to current type descriptor */
	struct	fd_type fts;	/* type descriptors */
	int	sectorsize;
	int	flags;
#define	FD_WP		(1<<0)	/* Write protected	*/
#define	FD_MOTOR	(1<<1)	/* motor should be on	*/
#define	FD_MOTORWAIT	(1<<2)	/* motor should be on	*/
#define	FD_EMPTY	(1<<3)	/* no media		*/
#define	FD_NEWDISK	(1<<4)	/* media changed	*/
#define	FD_ISADMA	(1<<5)	/* isa dma started 	*/
	int	track;		/* where we think the head is */
#define FD_NO_TRACK	 -2
	int	options;	/* FDOPT_* */
	struct	callout toffhandle;
	struct g_geom *fd_geom;
	struct g_provider *fd_provider;
	device_t dev;
	struct bio_queue_head fd_bq;
};

#define FD_NOT_VALID -2

static driver_intr_t fdc_intr;
static driver_filter_t fdc_intr_fast;
static void fdc_reset(struct fdc_data *);
static int fd_probe_disk(struct fd_data *, int *);

static SYSCTL_NODE(_debug, OID_AUTO, fdc, CTLFLAG_RW, 0, "fdc driver");

static int fifo_threshold = 8;
SYSCTL_INT(_debug_fdc, OID_AUTO, fifo, CTLFLAG_RW, &fifo_threshold, 0,
	"FIFO threshold setting");

static int debugflags = 0;
SYSCTL_INT(_debug_fdc, OID_AUTO, debugflags, CTLFLAG_RW, &debugflags, 0,
	"Debug flags");

static int retries = 10;
SYSCTL_INT(_debug_fdc, OID_AUTO, retries, CTLFLAG_RW, &retries, 0,
	"Number of retries to attempt");

static int spec1 = NE7_SPEC_1(6, 240);
SYSCTL_INT(_debug_fdc, OID_AUTO, spec1, CTLFLAG_RW, &spec1, 0,
	"Specification byte one (step-rate + head unload)");

static int spec2 = NE7_SPEC_2(16, 0);
SYSCTL_INT(_debug_fdc, OID_AUTO, spec2, CTLFLAG_RW, &spec2, 0,
	"Specification byte two (head load time + no-dma)");

static int settle;
SYSCTL_INT(_debug_fdc, OID_AUTO, settle, CTLFLAG_RW, &settle, 0,
	"Head settling time in sec/hz");

static void
fdprinttype(struct fd_type *ft)
{

	printf("(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,0x%x)",
	    ft->sectrac, ft->secsize, ft->datalen, ft->gap, ft->tracks,
	    ft->size, ft->trans, ft->heads, ft->f_gap, ft->f_inter,
	    ft->offset_side2, ft->flags);
}

static void
fdsettype(struct fd_data *fd, struct fd_type *ft)
{
	fd->ft = ft;
	ft->size = ft->sectrac * ft->heads * ft->tracks;
	fd->sectorsize = 128 << fd->ft->secsize;
}

/*
 * Bus space handling (access to low-level IO).
 */
static inline void
fdregwr(struct fdc_data *fdc, int reg, uint8_t v)
{

	bus_space_write_1(fdc->iot, fdc->ioh[reg], fdc->ioff[reg], v);
}

static inline uint8_t
fdregrd(struct fdc_data *fdc, int reg)
{

	return bus_space_read_1(fdc->iot, fdc->ioh[reg], fdc->ioff[reg]);
}

static void
fdctl_wr(struct fdc_data *fdc, u_int8_t v)
{

	fdregwr(fdc, FDCTL, v);
}

static void
fdout_wr(struct fdc_data *fdc, u_int8_t v)
{

	fdregwr(fdc, FDOUT, v);
}

static u_int8_t
fdsts_rd(struct fdc_data *fdc)
{

	return fdregrd(fdc, FDSTS);
}

static void
fddsr_wr(struct fdc_data *fdc, u_int8_t v)
{

	fdregwr(fdc, FDDSR, v);
}

static void
fddata_wr(struct fdc_data *fdc, u_int8_t v)
{

	fdregwr(fdc, FDDATA, v);
}

static u_int8_t
fddata_rd(struct fdc_data *fdc)
{

	return fdregrd(fdc, FDDATA);
}

static u_int8_t
fdin_rd(struct fdc_data *fdc)
{

	return fdregrd(fdc, FDCTL);
}

/*
 * Magic pseudo-DMA initialization for YE FDC. Sets count and
 * direction.
 */
static void
fdbcdr_wr(struct fdc_data *fdc, int iswrite, uint16_t count)
{
	fdregwr(fdc, FDBCDR, (count - 1) & 0xff);
	fdregwr(fdc, FDBCDR + 1,
	    (iswrite ? 0x80 : 0) | (((count - 1) >> 8) & 0x7f));
}

static int
fdc_err(struct fdc_data *fdc, const char *s)
{
	fdc->fdc_errs++;
	if (s) {
		if (fdc->fdc_errs < FDC_ERRMAX)
			device_printf(fdc->fdc_dev, "%s", s);
		else if (fdc->fdc_errs == FDC_ERRMAX)
			device_printf(fdc->fdc_dev, "too many errors, not "
						    "logging any more\n");
	}

	return (1);
}

/*
 * FDC IO functions, take care of the main status register, timeout
 * in case the desired status bits are never set.
 *
 * These PIO loops initially start out with short delays between
 * each iteration in the expectation that the required condition
 * is usually met quickly, so it can be handled immediately.
 */
static int
fdc_in(struct fdc_data *fdc, int *ptr)
{
	int i, j, step;

	step = 1;
	for (j = 0; j < FDSTS_TIMEOUT; j += step) {
	        i = fdsts_rd(fdc) & (NE7_DIO | NE7_RQM);
	        if (i == (NE7_DIO|NE7_RQM)) {
			i = fddata_rd(fdc);
			if (ptr)
				*ptr = i;
			return (0);
		}
		if (i == NE7_RQM)
			return (fdc_err(fdc, "ready for output in input\n"));
		step += step;
		DELAY(step);
	}
	return (fdc_err(fdc, bootverbose? "input ready timeout\n": 0));
}

static int
fdc_out(struct fdc_data *fdc, int x)
{
	int i, j, step;

	step = 1;
	for (j = 0; j < FDSTS_TIMEOUT; j += step) {
	        i = fdsts_rd(fdc) & (NE7_DIO | NE7_RQM);
	        if (i == NE7_RQM) {
			fddata_wr(fdc, x);
			return (0);
		}
		if (i == (NE7_DIO|NE7_RQM))
			return (fdc_err(fdc, "ready for input in output\n"));
		step += step;
		DELAY(step);
	}
	return (fdc_err(fdc, bootverbose? "output ready timeout\n": 0));
}

/*
 * fdc_cmd: Send a command to the chip.
 * Takes a varargs with this structure:
 *	# of output bytes
 *	output bytes as int [...]
 *	# of input bytes
 *	input bytes as int* [...]
 */
static int
fdc_cmd(struct fdc_data *fdc, int n_out, ...)
{
	u_char cmd = 0;
	int n_in;
	int n, i;
	va_list ap;

	va_start(ap, n_out);
	for (n = 0; n < n_out; n++) {
		i = va_arg(ap, int);
		if (n == 0)
			cmd = i;
		if (fdc_out(fdc, i) < 0) {
			char msg[50];
			snprintf(msg, sizeof(msg),
				"cmd %x failed at out byte %d of %d\n",
				cmd, n + 1, n_out);
			fdc->flags |= FDC_NEEDS_RESET;
			va_end(ap);
			return fdc_err(fdc, msg);
		}
	}
	n_in = va_arg(ap, int);
	for (n = 0; n < n_in; n++) {
		int *ptr = va_arg(ap, int *);
		if (fdc_in(fdc, ptr) < 0) {
			char msg[50];
			snprintf(msg, sizeof(msg),
				"cmd %02x failed at in byte %d of %d\n",
				cmd, n + 1, n_in);
			fdc->flags |= FDC_NEEDS_RESET;
			va_end(ap);
			return fdc_err(fdc, msg);
		}
	}
	va_end(ap);
	return (0);
}

static void
fdc_reset(struct fdc_data *fdc)
{
	int i, r[10];

	if (fdc->fdct == FDC_ENHANCED) {
		/* Try a software reset, default precomp, and 500 kb/s */
		fddsr_wr(fdc, I8207X_DSR_SR);
	} else {
		/* Try a hardware reset, keep motor on */
		fdout_wr(fdc, fdc->fdout & ~(FDO_FRST|FDO_FDMAEN));
		DELAY(100);
		/* enable FDC, but defer interrupts a moment */
		fdout_wr(fdc, fdc->fdout & ~FDO_FDMAEN);
	}
	DELAY(100);
	fdout_wr(fdc, fdc->fdout);

	/* XXX after a reset, silently believe the FDC will accept commands */
	if (fdc_cmd(fdc, 3, NE7CMD_SPECIFY, spec1, spec2, 0))
		device_printf(fdc->fdc_dev, " SPECIFY failed in reset\n");

	if (fdc->fdct == FDC_ENHANCED) {
		if (fdc_cmd(fdc, 4,
		    I8207X_CONFIG,
		    0,
		    /* 0x40 | */		/* Enable Implied Seek -
						 * breaks 2step! */
		    0x10 |			/* Polling disabled */
		    (fifo_threshold - 1),	/* Fifo threshold */
		    0x00,			/* Precomp track */
		    0))
			device_printf(fdc->fdc_dev,
			    " CONFIGURE failed in reset\n");
		if (debugflags & 1) {
			if (fdc_cmd(fdc, 1,
			    I8207X_DUMPREG,
			    10, &r[0], &r[1], &r[2], &r[3], &r[4],
			    &r[5], &r[6], &r[7], &r[8], &r[9]))
				device_printf(fdc->fdc_dev,
				    " DUMPREG failed in reset\n");
			for (i = 0; i < 10; i++)
				printf(" %02x", r[i]);
			printf("\n");
		}
	}
}

static int
fdc_sense_drive(struct fdc_data *fdc, int *st3p)
{
	int st3;

	if (fdc_cmd(fdc, 2, NE7CMD_SENSED, fdc->fd->fdsu, 1, &st3))
		return (fdc_err(fdc, "Sense Drive Status failed\n"));
	if (st3p)
		*st3p = st3;
	return (0);
}

static int
fdc_sense_int(struct fdc_data *fdc, int *st0p, int *cylp)
{
	int cyl, st0, ret;

	ret = fdc_cmd(fdc, 1, NE7CMD_SENSEI, 1, &st0);
	if (ret) {
		(void)fdc_err(fdc, "sense intr err reading stat reg 0\n");
		return (ret);
	}

	if (st0p)
		*st0p = st0;

	if ((st0 & NE7_ST0_IC) == NE7_ST0_IC_IV) {
		/*
		 * There doesn't seem to have been an interrupt.
		 */
		return (FD_NOT_VALID);
	}

	if (fdc_in(fdc, &cyl) < 0)
		return fdc_err(fdc, "can't get cyl num\n");

	if (cylp)
		*cylp = cyl;

	return (0);
}

static int
fdc_read_status(struct fdc_data *fdc)
{
	int i, ret, status;

	for (i = ret = 0; i < 7; i++) {
		ret = fdc_in(fdc, &status);
		fdc->status[i] = status;
		if (ret != 0)
			break;
	}

	if (ret == 0)
		fdc->flags |= FDC_STAT_VALID;
	else
		fdc->flags &= ~FDC_STAT_VALID;

	return ret;
}

/*
 * Select this drive
 */
static void
fd_select(struct fd_data *fd)
{
	struct fdc_data *fdc;

	/* XXX: lock controller */
	fdc = fd->fdc;
	fdc->fdout &= ~FDO_FDSEL;
	fdc->fdout |= FDO_FDMAEN | FDO_FRST | fd->fdsu;
	fdout_wr(fdc, fdc->fdout);
}

static void
fd_turnon(void *arg)
{
	struct fd_data *fd;
	struct bio *bp;
	int once;

	fd = arg;
	mtx_assert(&fd->fdc->fdc_mtx, MA_OWNED);
	fd->flags &= ~FD_MOTORWAIT;
	fd->flags |= FD_MOTOR;
	once = 0;
	for (;;) {
		bp = bioq_takefirst(&fd->fd_bq);
		if (bp == NULL)
			break;
		bioq_disksort(&fd->fdc->head, bp);
		once = 1;
	}
	if (once)
		wakeup(&fd->fdc->head);
}

static void
fd_motor(struct fd_data *fd, int turnon)
{
	struct fdc_data *fdc;

	fdc = fd->fdc;
/*
	mtx_assert(&fdc->fdc_mtx, MA_OWNED);
*/
	if (turnon) {
		fd->flags |= FD_MOTORWAIT;
		fdc->fdout |= (FDO_MOEN0 << fd->fdsu);
		callout_reset(&fd->toffhandle, hz, fd_turnon, fd);
	} else {
		callout_stop(&fd->toffhandle);
		fd->flags &= ~(FD_MOTOR|FD_MOTORWAIT);
		fdc->fdout &= ~(FDO_MOEN0 << fd->fdsu);
	}
	fdout_wr(fdc, fdc->fdout);
}

static void
fd_turnoff(void *xfd)
{
	struct fd_data *fd = xfd;

	mtx_assert(&fd->fdc->fdc_mtx, MA_OWNED);
	fd_motor(fd, 0);
}

/*
 * fdc_intr - wake up the worker thread.
 */

static void
fdc_intr(void *arg)
{

	wakeup(arg);
}

static int
fdc_intr_fast(void *arg)
{

	wakeup(arg);
	return(FILTER_HANDLED);
}

/*
 * fdc_pio(): perform programmed IO read/write for YE PCMCIA floppy.
 */
static void
fdc_pio(struct fdc_data *fdc)
{
	u_char *cptr;
	struct bio *bp;
	u_int count;

	bp = fdc->bp;
	cptr = fdc->fd->fd_ioptr;
	count = fdc->fd->fd_iosize;

	if (bp->bio_cmd == BIO_READ) {
		fdbcdr_wr(fdc, 0, count);
		bus_space_read_multi_1(fdc->iot, fdc->ioh[FD_YE_DATAPORT],
		    fdc->ioff[FD_YE_DATAPORT], cptr, count);
	} else {
		bus_space_write_multi_1(fdc->iot, fdc->ioh[FD_YE_DATAPORT],
		    fdc->ioff[FD_YE_DATAPORT], cptr, count);
		fdbcdr_wr(fdc, 0, count);	/* needed? */
	}
}

static int
fdc_biodone(struct fdc_data *fdc, int error)
{
	struct fd_data *fd;
	struct bio *bp;

	fd = fdc->fd;
	bp = fdc->bp;

	mtx_lock(&fdc->fdc_mtx);
	if (--fd->fd_iocount == 0)
		callout_reset(&fd->toffhandle, 4 * hz, fd_turnoff, fd);
	fdc->bp = NULL;
	fdc->fd = NULL;
	mtx_unlock(&fdc->fdc_mtx);
	if (bp->bio_to != NULL) {
		if ((debugflags & 2) && fd->fdc->retry > 0)
			printf("retries: %d\n", fd->fdc->retry);
		g_io_deliver(bp, error);
		return (0);
	}
	bp->bio_error = error;
	bp->bio_flags |= BIO_DONE;
	wakeup(bp);
	return (0);
}

static int retry_line;

static int
fdc_worker(struct fdc_data *fdc)
{
	struct fd_data *fd;
	struct bio *bp;
	int i, nsect;
	int st0, st3, cyl, mfm, steptrac, cylinder, descyl, sec;
	int head;
	int override_error;
	static int need_recal;
	struct fdc_readid *idp;
	struct fd_formb *finfo;

	override_error = 0;

	/* Have we exhausted our retries ? */
	bp = fdc->bp;
	fd = fdc->fd;
	if (bp != NULL &&
		(fdc->retry >= retries || (fd->options & FDOPT_NORETRY))) {
		if ((debugflags & 4))
			printf("Too many retries (EIO)\n");
		if (fdc->flags & FDC_NEEDS_RESET) {
			mtx_lock(&fdc->fdc_mtx);
			fd->flags |= FD_EMPTY;
			mtx_unlock(&fdc->fdc_mtx);
		}
		return (fdc_biodone(fdc, EIO));
	}

	/* Disable ISADMA if we bailed while it was active */
	if (fd != NULL && (fd->flags & FD_ISADMA)) {
		isa_dmadone(
		    bp->bio_cmd == BIO_READ ? ISADMA_READ : ISADMA_WRITE,
		    fd->fd_ioptr, fd->fd_iosize, fdc->dmachan);
		mtx_lock(&fdc->fdc_mtx);
		fd->flags &= ~FD_ISADMA;
		mtx_unlock(&fdc->fdc_mtx);
	}

	/* Unwedge the controller ? */
	if (fdc->flags & FDC_NEEDS_RESET) {
		fdc->flags &= ~FDC_NEEDS_RESET;
		fdc_reset(fdc);
		if (cold)
			DELAY(1000000);
		else
			tsleep(fdc, PRIBIO, "fdcrst", hz);
		/* Discard results */
		for (i = 0; i < 4; i++)
			fdc_sense_int(fdc, &st0, &cyl);
		/* All drives must recal */
		need_recal = 0xf;
	}

	/* Pick up a request, if need be wait for it */
	if (fdc->bp == NULL) {
		mtx_lock(&fdc->fdc_mtx);
		do {
			fdc->bp = bioq_takefirst(&fdc->head);
			if (fdc->bp == NULL)
				msleep(&fdc->head, &fdc->fdc_mtx,
				    PRIBIO, "-", 0);
		} while (fdc->bp == NULL &&
		    (fdc->flags & FDC_KTHREAD_EXIT) == 0);
		mtx_unlock(&fdc->fdc_mtx);

		if (fdc->bp == NULL)
			/*
			 * Nothing to do, worker thread has been
			 * requested to stop.
			 */
			return (0);

		bp = fdc->bp;
		fd = fdc->fd = bp->bio_driver1;
		fdc->retry = 0;
		fd->fd_ioptr = bp->bio_data;
		if (bp->bio_cmd == BIO_FMT) {
			i = offsetof(struct fd_formb, fd_formb_cylno(0));
			fd->fd_ioptr += i;
			fd->fd_iosize = bp->bio_length - i;
		}
	}

	/* Select drive, setup params */
	fd_select(fd);
	if (fdc->fdct == FDC_ENHANCED)
		fddsr_wr(fdc, fd->ft->trans);
	else
		fdctl_wr(fdc, fd->ft->trans);

	if (bp->bio_cmd == BIO_PROBE) {
		if ((!(device_get_flags(fd->dev) & FD_NO_CHLINE) &&
		    !(fdin_rd(fdc) & FDI_DCHG) &&
		    !(fd->flags & FD_EMPTY)) ||
		    fd_probe_disk(fd, &need_recal) == 0)
			return (fdc_biodone(fdc, 0));
		return (1);
	}

	/*
	 * If we are dead just flush the requests
	 */
	if (fd->flags & FD_EMPTY)
		return (fdc_biodone(fdc, ENXIO));

	/* Check if we lost our media */
	if (fdin_rd(fdc) & FDI_DCHG) {
		if (debugflags & 0x40)
			printf("Lost disk\n");
		mtx_lock(&fdc->fdc_mtx);
		fd->flags |= FD_EMPTY;
		fd->flags |= FD_NEWDISK;
		mtx_unlock(&fdc->fdc_mtx);
		g_topology_lock();
		g_orphan_provider(fd->fd_provider, ENXIO);
		fd->fd_provider->flags |= G_PF_WITHER;
		fd->fd_provider =
		    g_new_providerf(fd->fd_geom, "%s", fd->fd_geom->name);
		g_error_provider(fd->fd_provider, 0);
		g_topology_unlock();
		return (fdc_biodone(fdc, ENXIO));
	}

	/* Check if the floppy is write-protected */
	if (bp->bio_cmd == BIO_FMT || bp->bio_cmd == BIO_WRITE) {
		retry_line = __LINE__;
		if(fdc_sense_drive(fdc, &st3) != 0)
			return (1);
		if(st3 & NE7_ST3_WP)
			return (fdc_biodone(fdc, EROFS));
	}

	mfm = (fd->ft->flags & FL_MFM)? NE7CMD_MFM: 0;
	steptrac = (fd->ft->flags & FL_2STEP)? 2: 1;
	i = fd->ft->sectrac * fd->ft->heads;
	cylinder = bp->bio_pblkno / i;
	descyl = cylinder * steptrac;
	sec = bp->bio_pblkno % i;
	nsect = i - sec;
	head = sec / fd->ft->sectrac;
	sec = sec % fd->ft->sectrac + 1;

	/* If everything is going swimmingly, use multisector xfer */
	if (fdc->retry == 0 &&
	    (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE)) {
		fd->fd_iosize = imin(nsect * fd->sectorsize, bp->bio_resid);
		nsect = fd->fd_iosize / fd->sectorsize;
	} else if (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE) {
		fd->fd_iosize = fd->sectorsize;
		nsect = 1;
	}

	/* Do RECAL if we need to or are going to track zero anyway */
	if ((need_recal & (1 << fd->fdsu)) ||
	    (cylinder == 0 && fd->track != 0) ||
	    fdc->retry > 2) {
		retry_line = __LINE__;
		if (fdc_cmd(fdc, 2, NE7CMD_RECAL, fd->fdsu, 0))
			return (1);
		tsleep(fdc, PRIBIO, "fdrecal", hz);
		retry_line = __LINE__;
		if (fdc_sense_int(fdc, &st0, &cyl) == FD_NOT_VALID)
			return (1); /* XXX */
		retry_line = __LINE__;
		if ((st0 & 0xc0) || cyl != 0)
			return (1);
		need_recal &= ~(1 << fd->fdsu);
		fd->track = 0;
		/* let the heads settle */
		if (settle)
			tsleep(fdc->fd, PRIBIO, "fdhdstl", settle);
	}

	/*
	 * SEEK to where we want to be
	 */
	if (cylinder != fd->track) {
		retry_line = __LINE__;
		if (fdc_cmd(fdc, 3, NE7CMD_SEEK, fd->fdsu, descyl, 0))
			return (1);
		tsleep(fdc, PRIBIO, "fdseek", hz);
		retry_line = __LINE__;
		if (fdc_sense_int(fdc, &st0, &cyl) == FD_NOT_VALID)
			return (1); /* XXX */
		retry_line = __LINE__;
		if ((st0 & 0xc0) || cyl != descyl) {
			need_recal |= (1 << fd->fdsu);
			return (1);
		}
		/* let the heads settle */
		if (settle)
			tsleep(fdc->fd, PRIBIO, "fdhdstl", settle);
	}
	fd->track = cylinder;

	if (debugflags & 8)
		printf("op %x bn %ju siz %u ptr %p retry %d\n",
		    bp->bio_cmd, bp->bio_pblkno, fd->fd_iosize,
		    fd->fd_ioptr, fdc->retry);

	/* Setup ISADMA if we need it and have it */
	if ((bp->bio_cmd == BIO_READ ||
		bp->bio_cmd == BIO_WRITE ||
		bp->bio_cmd == BIO_FMT)
	     && !(fdc->flags & FDC_NODMA)) {
		isa_dmastart(
		    bp->bio_cmd == BIO_READ ? ISADMA_READ : ISADMA_WRITE,
		    fd->fd_ioptr, fd->fd_iosize, fdc->dmachan);
		mtx_lock(&fdc->fdc_mtx);
		fd->flags |= FD_ISADMA;
		mtx_unlock(&fdc->fdc_mtx);
	}

	/* Do PIO if we have to */
	if (fdc->flags & FDC_NODMA) {
		if (bp->bio_cmd == BIO_READ ||
		    bp->bio_cmd == BIO_WRITE ||
		    bp->bio_cmd == BIO_FMT)
			fdbcdr_wr(fdc, 1, fd->fd_iosize);
		if (bp->bio_cmd == BIO_WRITE ||
		    bp->bio_cmd == BIO_FMT)
			fdc_pio(fdc);
	}

	switch(bp->bio_cmd) {
	case BIO_FMT:
		/* formatting */
		finfo = (struct fd_formb *)bp->bio_data;
		retry_line = __LINE__;
		if (fdc_cmd(fdc, 6,
		    NE7CMD_FORMAT | mfm,
		    head << 2 | fd->fdsu,
		    finfo->fd_formb_secshift,
		    finfo->fd_formb_nsecs,
		    finfo->fd_formb_gaplen,
		    finfo->fd_formb_fillbyte, 0))
			return (1);
		break;
	case BIO_RDID:
		retry_line = __LINE__;
		if (fdc_cmd(fdc, 2,
		    NE7CMD_READID | mfm,
		    head << 2 | fd->fdsu, 0))
			return (1);
		break;
	case BIO_READ:
		retry_line = __LINE__;
		if (fdc_cmd(fdc, 9,
		    NE7CMD_READ | NE7CMD_SK | mfm | NE7CMD_MT,
		    head << 2 | fd->fdsu,	/* head & unit */
		    fd->track,			/* track */
		    head,			/* head */
		    sec,			/* sector + 1 */
		    fd->ft->secsize,		/* sector size */
		    fd->ft->sectrac,		/* sectors/track */
		    fd->ft->gap,		/* gap size */
		    fd->ft->datalen,		/* data length */
		    0))
			return (1);
		break;
	case BIO_WRITE:
		retry_line = __LINE__;
		if (fdc_cmd(fdc, 9,
		    NE7CMD_WRITE | mfm | NE7CMD_MT,
		    head << 2 | fd->fdsu,	/* head & unit */
		    fd->track,			/* track */
		    head,			/* head */
		    sec,			/* sector + 1 */
		    fd->ft->secsize,		/* sector size */
		    fd->ft->sectrac,		/* sectors/track */
		    fd->ft->gap,		/* gap size */
		    fd->ft->datalen,		/* data length */
		    0))
			return (1);
		break;
	default:
		KASSERT(0 == 1, ("Wrong bio_cmd %x\n", bp->bio_cmd));
	}

	/* Wait for interrupt */
	i = tsleep(fdc, PRIBIO, "fddata", hz);

	/* PIO if the read looks good */
	if (i == 0 && (fdc->flags & FDC_NODMA) && (bp->bio_cmd == BIO_READ))
		fdc_pio(fdc);

	/* Finish DMA */
	if (fd->flags & FD_ISADMA) {
		isa_dmadone(
		    bp->bio_cmd == BIO_READ ? ISADMA_READ : ISADMA_WRITE,
		    fd->fd_ioptr, fd->fd_iosize, fdc->dmachan);
		mtx_lock(&fdc->fdc_mtx);
		fd->flags &= ~FD_ISADMA;
		mtx_unlock(&fdc->fdc_mtx);
	}

	if (i != 0) {
		/*
		 * Timeout.
		 *
		 * Due to IBM's brain-dead design, the FDC has a faked ready
		 * signal, hardwired to ready == true. Thus, any command
		 * issued if there's no diskette in the drive will _never_
		 * complete, and must be aborted by resetting the FDC.
		 * Many thanks, Big Blue!
		 */
		retry_line = __LINE__;
		fdc->flags |= FDC_NEEDS_RESET;
		return (1);
	}

	retry_line = __LINE__;
	if (fdc_read_status(fdc))
		return (1);

	if (debugflags & 0x10)
		printf("  -> %x %x %x %x\n",
		    fdc->status[0], fdc->status[1],
		    fdc->status[2], fdc->status[3]);

	st0 = fdc->status[0] & NE7_ST0_IC;
	if (st0 != 0) {
		retry_line = __LINE__;
		if (st0 == NE7_ST0_IC_AT && fdc->status[1] & NE7_ST1_OR) {
			/*
			 * DMA overrun. Someone hogged the bus and
			 * didn't release it in time for the next
			 * FDC transfer.
			 */
			return (1);
		}
		retry_line = __LINE__;
		if(st0 == NE7_ST0_IC_IV) {
			fdc->flags |= FDC_NEEDS_RESET;
			return (1);
		}
		retry_line = __LINE__;
		if(st0 == NE7_ST0_IC_AT && fdc->status[2] & NE7_ST2_WC) {
			need_recal |= (1 << fd->fdsu);
			return (1);
		}
		if (debugflags & 0x20) {
			printf("status %02x %02x %02x %02x %02x %02x\n",
			    fdc->status[0], fdc->status[1], fdc->status[2],
			    fdc->status[3], fdc->status[4], fdc->status[5]);
		}
		retry_line = __LINE__;
		if (fd->options & FDOPT_NOERROR)
			override_error = 1;
		else
			return (1);
	}
	/* All OK */
	switch(bp->bio_cmd) {
	case BIO_RDID:
		/* copy out ID field contents */
		idp = (struct fdc_readid *)bp->bio_data;
		idp->cyl = fdc->status[3];
		idp->head = fdc->status[4];
		idp->sec = fdc->status[5];
		idp->secshift = fdc->status[6];
		if (debugflags & 0x40)
			printf("c %d h %d s %d z %d\n",
			    idp->cyl, idp->head, idp->sec, idp->secshift);
		break;
	case BIO_READ:
	case BIO_WRITE:
		bp->bio_pblkno += nsect;
		bp->bio_resid -= fd->fd_iosize;
		bp->bio_completed += fd->fd_iosize;
		fd->fd_ioptr += fd->fd_iosize;
		if (override_error) {
			if ((debugflags & 4))
				printf("FDOPT_NOERROR: returning bad data\n");
		} else {
			/* Since we managed to get something done,
			 * reset the retry */
			fdc->retry = 0;
			if (bp->bio_resid > 0)
				return (0);
		}
		break;
	case BIO_FMT:
		break;
	}
	return (fdc_biodone(fdc, 0));
}

static void
fdc_thread(void *arg)
{
	struct fdc_data *fdc;

	fdc = arg;
	int i;

	mtx_lock(&fdc->fdc_mtx);
	fdc->flags |= FDC_KTHREAD_ALIVE;
	while ((fdc->flags & FDC_KTHREAD_EXIT) == 0) {
		mtx_unlock(&fdc->fdc_mtx);
		i = fdc_worker(fdc);
		if (i && debugflags & 0x20) {
			if (fdc->bp != NULL) {
				g_print_bio(fdc->bp);
				printf("\n");
			}
			printf("Retry line %d\n", retry_line);
		}
		fdc->retry += i;
		mtx_lock(&fdc->fdc_mtx);
	}
	fdc->flags &= ~(FDC_KTHREAD_EXIT | FDC_KTHREAD_ALIVE);
	mtx_unlock(&fdc->fdc_mtx);

	kproc_exit(0);
}

/*
 * Enqueue a request.
 */
static void
fd_enqueue(struct fd_data *fd, struct bio *bp)
{
	struct fdc_data *fdc;
	int call;

	call = 0;
	fdc = fd->fdc;
	mtx_lock(&fdc->fdc_mtx);
	/* If we go from idle, cancel motor turnoff */
	if (fd->fd_iocount++ == 0)
		callout_stop(&fd->toffhandle);
	if (fd->flags & FD_MOTOR) {
		/* The motor is on, send it directly to the controller */
		bioq_disksort(&fdc->head, bp);
		wakeup(&fdc->head);
	} else {
		/* Queue it on the drive until the motor has started */
		bioq_insert_tail(&fd->fd_bq, bp);
		if (!(fd->flags & FD_MOTORWAIT))
			fd_motor(fd, 1);
	}
	mtx_unlock(&fdc->fdc_mtx);
}

/*
 * Try to find out if we have a disk in the drive.
 */
static int
fd_probe_disk(struct fd_data *fd, int *recal)
{
	struct fdc_data *fdc;
	int st0, st3, cyl;
	int oopts, ret;

	fdc = fd->fdc;
	oopts = fd->options;
	fd->options |= FDOPT_NOERRLOG | FDOPT_NORETRY;
	ret = 1;

	/*
	 * First recal, then seek to cyl#1, this clears the old condition on
	 * the disk change line so we can examine it for current status.
	 */
	if (debugflags & 0x40)
		printf("New disk in probe\n");
	mtx_lock(&fdc->fdc_mtx);
	fd->flags |= FD_NEWDISK;
	mtx_unlock(&fdc->fdc_mtx);
	if (fdc_cmd(fdc, 2, NE7CMD_RECAL, fd->fdsu, 0))
		goto done;
	tsleep(fdc, PRIBIO, "fdrecal", hz);
	if (fdc_sense_int(fdc, &st0, &cyl) == FD_NOT_VALID)
		goto done;	/* XXX */
	if ((st0 & 0xc0) || cyl != 0)
		goto done;

	/* Seek to track 1 */
	if (fdc_cmd(fdc, 3, NE7CMD_SEEK, fd->fdsu, 1, 0))
		goto done;
	tsleep(fdc, PRIBIO, "fdseek", hz);
	if (fdc_sense_int(fdc, &st0, &cyl) == FD_NOT_VALID)
		goto done;	/* XXX */
	*recal |= (1 << fd->fdsu);
	if (fdin_rd(fdc) & FDI_DCHG) {
		if (debugflags & 0x40)
			printf("Empty in probe\n");
		mtx_lock(&fdc->fdc_mtx);
		fd->flags |= FD_EMPTY;
		mtx_unlock(&fdc->fdc_mtx);
	} else {
		if (fdc_sense_drive(fdc, &st3) != 0)
			goto done;
		if (debugflags & 0x40)
			printf("Got disk in probe\n");
		mtx_lock(&fdc->fdc_mtx);
		fd->flags &= ~FD_EMPTY;
		if (st3 & NE7_ST3_WP)
			fd->flags |= FD_WP;
		else
			fd->flags &= ~FD_WP;
		mtx_unlock(&fdc->fdc_mtx);
	}
	ret = 0;

done:
	fd->options = oopts;
	return (ret);
}

static int
fdmisccmd(struct fd_data *fd, u_int cmd, void *data)
{
	struct bio *bp;
	struct fd_formb *finfo;
	struct fdc_readid *idfield;
	int error;

	bp = malloc(sizeof(struct bio), M_TEMP, M_WAITOK | M_ZERO);

	/*
	 * Set up a bio request for fdstrategy().  bio_offset is faked
	 * so that fdstrategy() will seek to the requested
	 * cylinder, and use the desired head.
	 */
	bp->bio_cmd = cmd;
	if (cmd == BIO_FMT) {
		finfo = (struct fd_formb *)data;
		bp->bio_pblkno =
		    (finfo->cyl * fd->ft->heads + finfo->head) *
		    fd->ft->sectrac;
		bp->bio_length = sizeof *finfo;
	} else if (cmd == BIO_RDID) {
		idfield = (struct fdc_readid *)data;
		bp->bio_pblkno =
		    (idfield->cyl * fd->ft->heads + idfield->head) *
		    fd->ft->sectrac;
		bp->bio_length = sizeof(struct fdc_readid);
	} else if (cmd == BIO_PROBE) {
		/* nothing */
	} else
		panic("wrong cmd in fdmisccmd()");
	bp->bio_offset = bp->bio_pblkno * fd->sectorsize;
	bp->bio_data = data;
	bp->bio_driver1 = fd;
	bp->bio_flags = 0;

	fd_enqueue(fd, bp);

	do {
		tsleep(bp, PRIBIO, "fdwait", hz);
	} while (!(bp->bio_flags & BIO_DONE));
	error = bp->bio_error;

	free(bp, M_TEMP);
	return (error);
}

/*
 * Try figuring out the density of the media present in our device.
 */
static int
fdautoselect(struct fd_data *fd)
{
	struct fd_type *fdtp;
	struct fdc_readid id;
	int oopts, rv;

	if (!(fd->ft->flags & FL_AUTO))
		return (0);

	fdtp = fd_native_types[fd->type];
	fdsettype(fd, fdtp);
	if (!(fd->ft->flags & FL_AUTO))
		return (0);

	/*
	 * Try reading sector ID fields, first at cylinder 0, head 0,
	 * then at cylinder 2, head N.  We don't probe cylinder 1,
	 * since for 5.25in DD media in a HD drive, there are no data
	 * to read (2 step pulses per media cylinder required).  For
	 * two-sided media, the second probe always goes to head 1, so
	 * we can tell them apart from single-sided media.  As a
	 * side-effect this means that single-sided media should be
	 * mentioned in the search list after two-sided media of an
	 * otherwise identical density.  Media with a different number
	 * of sectors per track but otherwise identical parameters
	 * cannot be distinguished at all.
	 *
	 * If we successfully read an ID field on both cylinders where
	 * the recorded values match our expectation, we are done.
	 * Otherwise, we try the next density entry from the table.
	 *
	 * Stepping to cylinder 2 has the side-effect of clearing the
	 * unit attention bit.
	 */
	oopts = fd->options;
	fd->options |= FDOPT_NOERRLOG | FDOPT_NORETRY;
	for (; fdtp->heads; fdtp++) {
		fdsettype(fd, fdtp);

		id.cyl = id.head = 0;
		rv = fdmisccmd(fd, BIO_RDID, &id);
		if (rv != 0)
			continue;
		if (id.cyl != 0 || id.head != 0 || id.secshift != fdtp->secsize)
			continue;
		id.cyl = 2;
		id.head = fd->ft->heads - 1;
		rv = fdmisccmd(fd, BIO_RDID, &id);
		if (id.cyl != 2 || id.head != fdtp->heads - 1 ||
		    id.secshift != fdtp->secsize)
			continue;
		if (rv == 0)
			break;
	}

	fd->options = oopts;
	if (fdtp->heads == 0) {
		if (debugflags & 0x40)
			device_printf(fd->dev, "autoselection failed\n");
		fdsettype(fd, fd_native_types[fd->type]);
		return (-1);
	} else {
		if (debugflags & 0x40) {
			device_printf(fd->dev,
			    "autoselected %d KB medium\n",
			    fd->ft->size / 2);
			fdprinttype(fd->ft);
		}
		return (0);
	}
}

/*
 * GEOM class implementation
 */

static g_access_t	fd_access;
static g_start_t	fd_start;
static g_ioctl_t	fd_ioctl;

struct g_class g_fd_class = {
	.name =		"FD",
	.version =	G_VERSION,
	.start =	fd_start,
	.access =	fd_access,
	.ioctl =	fd_ioctl,
};

static int
fd_access(struct g_provider *pp, int r, int w, int e)
{
	struct fd_data *fd;
	struct fdc_data *fdc;
	int ar, aw, ae;
	int busy;

	fd = pp->geom->softc;
	fdc = fd->fdc;

	/*
	 * If our provider is withering, we can only get negative requests
	 * and we don't want to even see them
	 */
	if (pp->flags & G_PF_WITHER)
		return (0);

	ar = r + pp->acr;
	aw = w + pp->acw;
	ae = e + pp->ace;

	if (ar == 0 && aw == 0 && ae == 0) {
		fd->options &= ~(FDOPT_NORETRY | FDOPT_NOERRLOG | FDOPT_NOERROR);
		device_unbusy(fd->dev);
		return (0);
	}

	busy = 0;
	if (pp->acr == 0 && pp->acw == 0 && pp->ace == 0) {
		if (fdmisccmd(fd, BIO_PROBE, NULL))
			return (ENXIO);
		if (fd->flags & FD_EMPTY)
			return (ENXIO);
		if (fd->flags & FD_NEWDISK) {
			if (fdautoselect(fd) != 0 &&
			    (device_get_flags(fd->dev) & FD_NO_CHLINE)) {
				mtx_lock(&fdc->fdc_mtx);
				fd->flags |= FD_EMPTY;
				mtx_unlock(&fdc->fdc_mtx);
				return (ENXIO);
			}
			mtx_lock(&fdc->fdc_mtx);
			fd->flags &= ~FD_NEWDISK;
			mtx_unlock(&fdc->fdc_mtx);
		}
		device_busy(fd->dev);
		busy = 1;
	}

	if (w > 0 && (fd->flags & FD_WP)) {
		if (busy)
			device_unbusy(fd->dev);
		return (EROFS);
	}

	pp->sectorsize = fd->sectorsize;
	pp->stripesize = fd->ft->heads * fd->ft->sectrac * fd->sectorsize;
	pp->mediasize = pp->stripesize * fd->ft->tracks;
	return (0);
}

static void
fd_start(struct bio *bp)
{
 	struct fdc_data *	fdc;
 	struct fd_data *	fd;

	fd = bp->bio_to->geom->softc;
	fdc = fd->fdc;
	bp->bio_driver1 = fd;
	if (bp->bio_cmd == BIO_GETATTR) {
		if (g_handleattr_int(bp, "GEOM::fwsectors", fd->ft->sectrac))
			return;
		if (g_handleattr_int(bp, "GEOM::fwheads", fd->ft->heads))
			return;
		g_io_deliver(bp, ENOIOCTL);
		return;
	}
	if (!(bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE)) {
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}
	bp->bio_pblkno = bp->bio_offset / fd->sectorsize;
	bp->bio_resid = bp->bio_length;
	fd_enqueue(fd, bp);
	return;
}

static int
fd_ioctl(struct g_provider *pp, u_long cmd, void *data, int fflag, struct thread *td)
{
	struct fd_data *fd;
	struct fdc_status *fsp;
	struct fdc_readid *rid;
	int error;

	fd = pp->geom->softc;

	switch (cmd) {
	case FD_GTYPE:                  /* get drive type */
		*(struct fd_type *)data = *fd->ft;
		return (0);

	case FD_STYPE:                  /* set drive type */
		/*
		 * Allow setting drive type temporarily iff
		 * currently unset.  Used for fdformat so any
		 * user can set it, and then start formatting.
		 */
		fd->fts = *(struct fd_type *)data;
		if (fd->fts.sectrac) {
			/* XXX: check for rubbish */
			fdsettype(fd, &fd->fts);
		} else {
			fdsettype(fd, fd_native_types[fd->type]);
		}
		if (debugflags & 0x40)
			fdprinttype(fd->ft);
		return (0);

	case FD_GOPTS:			/* get drive options */
		*(int *)data = fd->options;
		return (0);

	case FD_SOPTS:			/* set drive options */
		fd->options = *(int *)data;
		return (0);

	case FD_CLRERR:
		error = priv_check(td, PRIV_DRIVER);
		if (error)
			return (error);
		fd->fdc->fdc_errs = 0;
		return (0);

	case FD_GSTAT:
		fsp = (struct fdc_status *)data;
		if ((fd->fdc->flags & FDC_STAT_VALID) == 0)
			return (EINVAL);
		memcpy(fsp->status, fd->fdc->status, 7 * sizeof(u_int));
		return (0);

	case FD_GDTYPE:
		*(enum fd_drivetype *)data = fd->type;
		return (0);

	case FD_FORM:
		if (!(fflag & FWRITE))
			return (EPERM);
		if (((struct fd_formb *)data)->format_version !=
		    FD_FORMAT_VERSION)
			return (EINVAL); /* wrong version of formatting prog */
		error = fdmisccmd(fd, BIO_FMT, data);
		mtx_lock(&fd->fdc->fdc_mtx);
		fd->flags |= FD_NEWDISK;
		mtx_unlock(&fd->fdc->fdc_mtx);
		break;

	case FD_READID:
		rid = (struct fdc_readid *)data;
		if (rid->cyl > 85 || rid->head > 1)
			return (EINVAL);
		error = fdmisccmd(fd, BIO_RDID, data);
		break;

	case FIONBIO:
	case FIOASYNC:
		/* For backwards compat with old fd*(8) tools */
		error = 0;
		break;

	default:
		if (debugflags & 0x80)
			printf("Unknown ioctl %lx\n", cmd);
		error = ENOIOCTL;
		break;
	}
	return (error);
};



/*
 * Configuration/initialization stuff, per controller.
 */

devclass_t fdc_devclass;
static devclass_t fd_devclass;

struct fdc_ivars {
	int	fdunit;
	int	fdtype;
};

void
fdc_release_resources(struct fdc_data *fdc)
{
	device_t dev;
	struct resource *last;
	int i;

	dev = fdc->fdc_dev;
	if (fdc->fdc_intr)
		bus_teardown_intr(dev, fdc->res_irq, fdc->fdc_intr);
	fdc->fdc_intr = NULL;
	if (fdc->res_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, fdc->rid_irq,
		    fdc->res_irq);
	fdc->res_irq = NULL;
	last = NULL;
	for (i = 0; i < FDC_MAXREG; i++) {
		if (fdc->resio[i] != NULL && fdc->resio[i] != last) {
			bus_release_resource(dev, SYS_RES_IOPORT,
			    fdc->ridio[i], fdc->resio[i]);
			last = fdc->resio[i];
			fdc->resio[i] = NULL;
		}
	}
	if (fdc->res_drq != NULL)
		bus_release_resource(dev, SYS_RES_DRQ, fdc->rid_drq,
		    fdc->res_drq);
	fdc->res_drq = NULL;
}

int
fdc_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct fdc_ivars *ivars = device_get_ivars(child);

	switch (which) {
	case FDC_IVAR_FDUNIT:
		*result = ivars->fdunit;
		break;
	case FDC_IVAR_FDTYPE:
		*result = ivars->fdtype;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

int
fdc_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
	struct fdc_ivars *ivars = device_get_ivars(child);

	switch (which) {
	case FDC_IVAR_FDUNIT:
		ivars->fdunit = value;
		break;
	case FDC_IVAR_FDTYPE:
		ivars->fdtype = value;
		break;
	default:
		return (ENOENT);
	}
	return (0);
}

int
fdc_initial_reset(device_t dev, struct fdc_data *fdc)
{
	int ic_type, part_id;

	/*
	 * A status value of 0xff is very unlikely, but not theoretically
	 * impossible, but it is far more likely to indicate an empty bus.
	 */
	if (fdsts_rd(fdc) == 0xff)
		return (ENXIO);

	/*
	 * Assert a reset to the floppy controller and check that the status
	 * register goes to zero.
	 */
	fdout_wr(fdc, 0);
	fdout_wr(fdc, 0);
	if (fdsts_rd(fdc) != 0)
		return (ENXIO);

	/*
	 * Clear the reset and see it come ready.
	 */
	fdout_wr(fdc, FDO_FRST);
	DELAY(100);
	if (fdsts_rd(fdc) != 0x80)
		return (ENXIO);

	/* Then, see if it can handle a command. */
	if (fdc_cmd(fdc, 3, NE7CMD_SPECIFY, NE7_SPEC_1(6, 240),
	    NE7_SPEC_2(31, 0), 0))
		return (ENXIO);

	/*
	 * Try to identify the chip.
	 *
	 * The i8272 datasheet documents that unknown commands
	 * will return ST0 as 0x80.  The i8272 is supposedly identical
	 * to the NEC765.
	 * The i82077SL datasheet says 0x90 for the VERSION command,
	 * and several "superio" chips emulate this.
	 */
	if (fdc_cmd(fdc, 1, NE7CMD_VERSION, 1, &ic_type))
		return (ENXIO);
	if (fdc_cmd(fdc, 1, 0x18, 1, &part_id))
		return (ENXIO);
	if (bootverbose)
		device_printf(dev,
		    "ic_type %02x part_id %02x\n", ic_type, part_id);
	switch (ic_type & 0xff) {
	case 0x80:
		device_set_desc(dev, "NEC 765 or clone");
		fdc->fdct = FDC_NE765;
		break;
	case 0x81:
	case 0x90:
		device_set_desc(dev,
		    "Enhanced floppy controller");
		fdc->fdct = FDC_ENHANCED;
		break;
	default:
		device_set_desc(dev, "Generic floppy controller");
		fdc->fdct = FDC_UNKNOWN;
		break;
	}
	return (0);
}

int
fdc_detach(device_t dev)
{
	struct	fdc_data *fdc;
	int	error;

	fdc = device_get_softc(dev);

	/* have our children detached first */
	if ((error = bus_generic_detach(dev)))
		return (error);

	if (fdc->fdc_intr)
		bus_teardown_intr(dev, fdc->res_irq, fdc->fdc_intr);
	fdc->fdc_intr = NULL;

	/* kill worker thread */
	mtx_lock(&fdc->fdc_mtx);
	fdc->flags |= FDC_KTHREAD_EXIT;
	wakeup(&fdc->head);
	while ((fdc->flags & FDC_KTHREAD_ALIVE) != 0)
		msleep(fdc->fdc_thread, &fdc->fdc_mtx, PRIBIO, "fdcdet", 0);
	mtx_unlock(&fdc->fdc_mtx);

	/* reset controller, turn motor off */
	fdout_wr(fdc, 0);

	if (!(fdc->flags & FDC_NODMA))
		isa_dma_release(fdc->dmachan);
	fdc_release_resources(fdc);
	mtx_destroy(&fdc->fdc_mtx);
	return (0);
}

/*
 * Add a child device to the fdc controller.  It will then be probed etc.
 */
device_t
fdc_add_child(device_t dev, const char *name, int unit)
{
	struct fdc_ivars *ivar;
	device_t child;

	ivar = malloc(sizeof *ivar, M_DEVBUF /* XXX */, M_NOWAIT | M_ZERO);
	if (ivar == NULL)
		return (NULL);
	child = device_add_child(dev, name, unit);
	if (child == NULL) {
		free(ivar, M_DEVBUF);
		return (NULL);
	}
	device_set_ivars(child, ivar);
	ivar->fdunit = unit;
	ivar->fdtype = FDT_NONE;
	if (resource_disabled(name, unit))
		device_disable(child);
	return (child);
}

int
fdc_attach(device_t dev)
{
	struct	fdc_data *fdc;
	int	error;

	fdc = device_get_softc(dev);
	fdc->fdc_dev = dev;
	error = fdc_initial_reset(dev, fdc);
	if (error) {
		device_printf(dev, "does not respond\n");
		return (error);
	}
	error = bus_setup_intr(dev, fdc->res_irq,
	    INTR_TYPE_BIO | INTR_ENTROPY | 
	    ((fdc->flags & FDC_NOFAST) ? INTR_MPSAFE : 0),		       
            ((fdc->flags & FDC_NOFAST) ? NULL : fdc_intr_fast), 	    
	    ((fdc->flags & FDC_NOFAST) ? fdc_intr : NULL), 
			       fdc, &fdc->fdc_intr);
	if (error) {
		device_printf(dev, "cannot setup interrupt\n");
		return (error);
	}
	if (!(fdc->flags & FDC_NODMA)) {
		error = isa_dma_acquire(fdc->dmachan);
		if (!error) {
			error = isa_dma_init(fdc->dmachan,
			    MAX_BYTES_PER_CYL, M_WAITOK);
			if (error)
				isa_dma_release(fdc->dmachan);
		}
		if (error)
			return (error);
	}
	fdc->fdcu = device_get_unit(dev);
	fdc->flags |= FDC_NEEDS_RESET;

	mtx_init(&fdc->fdc_mtx, "fdc lock", NULL, MTX_DEF);

	/* reset controller, turn motor off, clear fdout mirror reg */
	fdout_wr(fdc, fdc->fdout = 0);
	bioq_init(&fdc->head);

	settle = hz / 8;

	return (0);
}

void
fdc_start_worker(device_t dev)
{
	struct	fdc_data *fdc;

	fdc = device_get_softc(dev);
	kproc_create(fdc_thread, fdc, &fdc->fdc_thread, 0, 0,
	    "fdc%d", device_get_unit(dev));
}

int
fdc_hints_probe(device_t dev)
{
	const char *name, *dname;
	int i, error, dunit;

	/*
	 * Probe and attach any children.  We should probably detect
	 * devices from the BIOS unless overridden.
	 */
	name = device_get_nameunit(dev);
	i = 0;
	while ((resource_find_match(&i, &dname, &dunit, "at", name)) == 0) {
		resource_int_value(dname, dunit, "drive", &dunit);
		fdc_add_child(dev, dname, dunit);
	}

	if ((error = bus_generic_attach(dev)) != 0)
		return (error);
	return (0);
}

int
fdc_print_child(device_t me, device_t child)
{
	int retval = 0, flags;

	retval += bus_print_child_header(me, child);
	retval += printf(" on %s drive %d", device_get_nameunit(me),
	       fdc_get_fdunit(child));
	if ((flags = device_get_flags(me)) != 0)
		retval += printf(" flags %#x", flags);
	retval += printf("\n");

	return (retval);
}

/*
 * Configuration/initialization, per drive.
 */
static int
fd_probe(device_t dev)
{
	int	unit;
	int	i;
	u_int	st0, st3;
	struct	fd_data *fd;
	struct	fdc_data *fdc;
	int	fdsu;
	int	flags, type;

	fdsu = fdc_get_fdunit(dev);
	fd = device_get_softc(dev);
	fdc = device_get_softc(device_get_parent(dev));
	flags = device_get_flags(dev);

	fd->dev = dev;
	fd->fdc = fdc;
	fd->fdsu = fdsu;
	unit = device_get_unit(dev);

	/* Auto-probe if fdinfo is present, but always allow override. */
	type = flags & FD_TYPEMASK;
	if (type == FDT_NONE && (type = fdc_get_fdtype(dev)) != FDT_NONE) {
		fd->type = type;
		goto done;
	} else {
		/* make sure fdautoselect() will be called */
		fd->flags = FD_EMPTY;
		fd->type = type;
	}

#if defined(__i386__) || defined(__amd64__)
	if (fd->type == FDT_NONE && (unit == 0 || unit == 1)) {
		/* Look up what the BIOS thinks we have. */
		if (unit == 0)
			fd->type = (rtcin(RTC_FDISKETTE) & 0xf0) >> 4;
		else
			fd->type = rtcin(RTC_FDISKETTE) & 0x0f;
		if (fd->type == FDT_288M_1)
			fd->type = FDT_288M;
	}
#endif /* __i386__ || __amd64__ */
	/* is there a unit? */
	if (fd->type == FDT_NONE)
		return (ENXIO);

	mtx_lock(&fdc->fdc_mtx);

	/* select it */
	fd_select(fd);
	fd_motor(fd, 1);
	fdc->fd = fd;
	fdc_reset(fdc);		/* XXX reset, then unreset, etc. */
	DELAY(1000000);	/* 1 sec */

	if ((flags & FD_NO_PROBE) == 0) {
		/* If we're at track 0 first seek inwards. */
		if ((fdc_sense_drive(fdc, &st3) == 0) &&
		    (st3 & NE7_ST3_T0)) {
			/* Seek some steps... */
			if (fdc_cmd(fdc, 3, NE7CMD_SEEK, fdsu, 10, 0) == 0) {
				/* ...wait a moment... */
				DELAY(300000);
				/* make ctrlr happy: */
				fdc_sense_int(fdc, NULL, NULL);
			}
		}

		for (i = 0; i < 2; i++) {
			/*
			 * we must recalibrate twice, just in case the
			 * heads have been beyond cylinder 76, since
			 * most FDCs still barf when attempting to
			 * recalibrate more than 77 steps
			 */
			/* go back to 0: */
			if (fdc_cmd(fdc, 2, NE7CMD_RECAL, fdsu, 0) == 0) {
				/* a second being enough for full stroke seek*/
				DELAY(i == 0 ? 1000000 : 300000);

				/* anything responding? */
				if (fdc_sense_int(fdc, &st0, NULL) == 0 &&
				    (st0 & NE7_ST0_EC) == 0)
					break; /* already probed successfully */
			}
		}
	}

	fd_motor(fd, 0);
	fdc->fd = NULL;
	mtx_unlock(&fdc->fdc_mtx);

	if ((flags & FD_NO_PROBE) == 0 &&
	    (st0 & NE7_ST0_EC) != 0) /* no track 0 -> no drive present */
		return (ENXIO);

done:

	switch (fd->type) {
	case FDT_12M:
		device_set_desc(dev, "1200-KB 5.25\" drive");
		break;
	case FDT_144M:
		device_set_desc(dev, "1440-KB 3.5\" drive");
		break;
	case FDT_288M:
		device_set_desc(dev, "2880-KB 3.5\" drive (in 1440-KB mode)");
		break;
	case FDT_360K:
		device_set_desc(dev, "360-KB 5.25\" drive");
		break;
	case FDT_720K:
		device_set_desc(dev, "720-KB 3.5\" drive");
		break;
	default:
		return (ENXIO);
	}
	fd->track = FD_NO_TRACK;
	fd->fdc = fdc;
	fd->fdsu = fdsu;
	fd->options = 0;
	callout_init_mtx(&fd->toffhandle, &fd->fdc->fdc_mtx, 0);

	/* initialize densities for subdevices */
	fdsettype(fd, fd_native_types[fd->type]);
	return (0);
}

/*
 * We have to do this in a geom event because GEOM is not running
 * when fd_attach() is.
 * XXX: move fd_attach after geom like ata/scsi disks
 */
static void
fd_attach2(void *arg, int flag)
{
	struct	fd_data *fd;

	fd = arg;

	fd->fd_geom = g_new_geomf(&g_fd_class,
	    "fd%d", device_get_unit(fd->dev));
	fd->fd_provider = g_new_providerf(fd->fd_geom, "%s", fd->fd_geom->name);
	fd->fd_geom->softc = fd;
	g_error_provider(fd->fd_provider, 0);
}

static int
fd_attach(device_t dev)
{
	struct	fd_data *fd;

	fd = device_get_softc(dev);
	g_post_event(fd_attach2, fd, M_WAITOK, NULL);
	fd->flags |= FD_EMPTY;
	bioq_init(&fd->fd_bq);

	return (0);
}

static void
fd_detach_geom(void *arg, int flag)
{
	struct	fd_data *fd = arg;

	g_topology_assert();
	g_wither_geom(fd->fd_geom, ENXIO);
}

static int
fd_detach(device_t dev)
{
	struct	fd_data *fd;

	fd = device_get_softc(dev);
	g_waitfor_event(fd_detach_geom, fd, M_WAITOK, NULL);
	while (device_get_state(dev) == DS_BUSY)
		tsleep(fd, PZERO, "fdd", hz/10);
	callout_drain(&fd->toffhandle);

	return (0);
}

static device_method_t fd_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fd_probe),
	DEVMETHOD(device_attach,	fd_attach),
	DEVMETHOD(device_detach,	fd_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend), /* XXX */
	DEVMETHOD(device_resume,	bus_generic_resume), /* XXX */
	{ 0, 0 }
};

static driver_t fd_driver = {
	"fd",
	fd_methods,
	sizeof(struct fd_data)
};

static int
fdc_modevent(module_t mod, int type, void *data)
{

	return (g_modevent(NULL, type, &g_fd_class));
}

DRIVER_MODULE(fd, fdc, fd_driver, fd_devclass, fdc_modevent, 0);
