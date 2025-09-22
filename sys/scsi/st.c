/*	$OpenBSD: st.c,v 1.191 2024/09/04 07:54:53 mglocker Exp $	*/
/*	$NetBSD: st.c,v 1.71 1997/02/21 23:03:49 thorpej Exp $	*/

/*
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * Originally written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems for use under the MACH(2.5) operating system.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * Ported to run under 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 * major changes by Julian Elischer (julian@jules.dialix.oz.au) May 1993
 */

/*
 * To do:
 * work out some better way of guessing what a good timeout is going
 * to be depending on whether we expect to retension or not.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/pool.h>
#include <sys/buf.h>
#include <sys/mtio.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/conf.h>
#include <sys/vnode.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_debug.h>
#include <scsi/scsi_tape.h>
#include <scsi/scsiconf.h>

/* Defines for device specific stuff */
#define DEF_FIXED_BSIZE  512

#define STMODE(z)	( minor(z)	 & 0x03)
#define STUNIT(z)	((minor(z) >> 4)       )

#define STMINOR(unit, mode)	(((unit) << 4) + (mode))
#define MAXSTMODES	16	/* Old max retained so minor's don't change. */

#define	ST_IO_TIME	(3 * 60 * 1000)		/* 3 minutes */
#define	ST_CTL_TIME	(30 * 1000)		/* 30 seconds */
#define	ST_SPC_TIME	(4 * 60 * 60 * 1000)	/* 4 hours */

/*
 * Maximum density code allowed in SCSI spec (SSC2R08f, Section 8.3).
 */
#define SCSI_MAX_DENSITY_CODE		0xff

/*
 * Define various devices that we know mis-behave in some way,
 * and note how they are bad, so we can correct for them
 */
struct mode {
	int blksize;
	u_int8_t density;
};

struct quirkdata {
	u_int quirks;
#define	ST_Q_SENSE_HELP		0x0001	/* must do READ for good MODE SENSE */
#define	ST_Q_IGNORE_LOADS	0x0002
#define	ST_Q_UNIMODAL		0x0004	/* unimode drive rejects mode select */
	struct mode mode;
};

struct st_quirk_inquiry_pattern {
	struct scsi_inquiry_pattern pattern;
	struct quirkdata quirkdata;
};

const struct st_quirk_inquiry_pattern st_quirk_patterns[] = {
	{{T_SEQUENTIAL, T_REMOV,
		 "        ", "                ", "    "}, {0,
							   {512, 0}}},
 	{{T_SEQUENTIAL, T_REMOV,
		 "TANDBERG", " TDC 3800       ", ""},     {0,
							   {512, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "ARCHIVE ", "VIPER 2525 25462", ""},     {ST_Q_SENSE_HELP,
							   {0, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "SANKYO  ", "CP525           ", ""},     {0,
							   {512, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "ANRITSU ", "DMT780          ", ""},     {0,
							   {512, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "ARCHIVE ", "VIPER 150  21531", ""},     {ST_Q_SENSE_HELP,
							   {0, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "WANGTEK ", "5099ES SCSI",	 ""},     {0,
							   {512, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "WANGTEK ", "5150ES SCSI",	 ""},     {0,
							   {512, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "HP      ", "T4000s          ", ""},     {ST_Q_UNIMODAL,
							   {0, QIC_3095}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "WANGTEK ", "5150ES SCSI FA15", "01 A"}, {ST_Q_IGNORE_LOADS,
							   {0, 0}}},
	{{T_SEQUENTIAL, T_REMOV,
		 "TEAC    ", "MT-2ST/N50      ", ""},     {ST_Q_IGNORE_LOADS,
							   {0, 0}}},
};

#define NOEJECT 0
#define EJECT 1

#define NOREWIND 0
#define DOREWIND 1

struct st_softc {
	struct device sc_dev;
	struct disk sc_dk;

	int flags;
#define	ST_INFO_VALID		0x00000001
#define	ST_WRITTEN		0x00000004
#define	ST_FIXEDBLOCKS		0x00000008
#define	ST_AT_FILEMARK		0x00000010
#define	ST_EIO_PENDING		0x00000020
#define	ST_EOM_PENDING  	0x00000040
#define	ST_EOD_DETECTED		0x00000080
#define	ST_FM_WRITTEN		0x00000100
#define	ST_BLANK_READ		0x00000200
#define	ST_2FM_AT_EOD		0x00000400
#define	ST_MOUNTED		0x00000800
#define	ST_DONTBUFFER		0x00001000
#define	ST_DYING		0x00004000
#define	ST_BOD_DETECTED		0x00008000
#define	ST_MODE_DENSITY		0x00010000
#define	ST_MODE_BLKSIZE		0x00040000

	u_int quirks;		/* quirks for the open mode           */
	int blksize;		/* blksize we are using               */
	u_int8_t density;	/* present density                    */
	short mt_resid;		/* last (short) resid                 */
	short mt_erreg;		/* last error (sense key) seen        */

	struct scsi_link *sc_link;	/* our link to the adapter etc.       */

	int blkmin;		/* min blk size                       */
	int blkmax;		/* max blk size                       */

	u_int32_t media_blksize;	/* 0 if not ST_FIXEDBLOCKS            */
	u_int32_t media_density;	/* this is what it said when asked    */
	int media_fileno;		/* relative to BOT. -1 means unknown. */
	int media_blkno;		/* relative to BOF. -1 means unknown. */
	int media_eom;			/* relative to BOT. -1 means unknown. */

	struct mode mode;
	struct bufq sc_bufq;
	struct scsi_xshandler sc_xsh;
};


int	stmatch(struct device *, void *, void *);
void	stattach(struct device *, struct device *, void *);
int	stactivate(struct device *, int);
int	stdetach(struct device *, int);
void	stminphys(struct buf *);
void	ststart(struct scsi_xfer *);

int	st_mount_tape(struct st_softc *, int);
void	st_unmount(struct st_softc *, int, int);
int	st_decide_mode(struct st_softc *, int);
void	st_buf_done(struct scsi_xfer *);
int	st_read(struct st_softc *, char *, int, int);
int	st_read_block_limits(struct st_softc *, int);
int	st_mode_sense(struct st_softc *, int);
int	st_mode_select(struct st_softc *, int);
int	st_space(struct st_softc *, int, u_int, int);
int	st_write_filemarks(struct st_softc *, int, int);
int	st_check_eod(struct st_softc *, int, int *, int);
int	st_load(struct st_softc *, u_int, int);
int	st_rewind(struct st_softc *, u_int, int);
int	st_interpret_sense(struct scsi_xfer *);
int	st_touch_tape(struct st_softc *);
int	st_erase(struct st_softc *, int, int);

const struct cfattach st_ca = {
	sizeof(struct st_softc), stmatch, stattach,
	stdetach, stactivate
};

struct cfdriver st_cd = {
	NULL, "st", DV_TAPE
};

#define	ST_PER_ACTION	(ST_AT_FILEMARK | ST_EIO_PENDING | ST_EOM_PENDING | \
			 ST_BLANK_READ)

#define stlookup(unit) (struct st_softc *)device_lookup(&st_cd, (unit))

const struct scsi_inquiry_pattern st_patterns[] = {
	{T_SEQUENTIAL, T_REMOV,
	 "",         "",                 ""},
};

int
stmatch(struct device *parent, void *match, void *aux)
{
	struct scsi_attach_args *sa = aux;
	struct scsi_inquiry_data *inq = &sa->sa_sc_link->inqdata;
	int priority;

	(void)scsi_inqmatch(inq, st_patterns, nitems(st_patterns),
	    sizeof(st_patterns[0]), &priority);
	return priority;
}

/*
 * The routine called by the low level scsi routine when it discovers
 * a device suitable for this driver.
 */
void
stattach(struct device *parent, struct device *self, void *aux)
{
	const struct st_quirk_inquiry_pattern *finger;
	struct st_softc *st = (void *)self;
	struct scsi_attach_args *sa = aux;
	struct scsi_link *link = sa->sa_sc_link;
	int priority;

	SC_DEBUG(link, SDEV_DB2, ("stattach:\n"));

	/*
	 * Store information needed to contact our base driver
	 */
	st->sc_link = link;
	link->interpret_sense = st_interpret_sense;
	link->device_softc = st;

	/* Get any quirks and mode information. */
	finger = (const struct st_quirk_inquiry_pattern *)scsi_inqmatch(
	    &link->inqdata,
	    st_quirk_patterns,
	    nitems(st_quirk_patterns),
	    sizeof(st_quirk_patterns[0]), &priority);
	if (finger != NULL) {
		st->quirks = finger->quirkdata.quirks;
		st->mode = finger->quirkdata.mode;
		CLR(st->flags, ST_MODE_BLKSIZE | ST_MODE_DENSITY);
		if (st->mode.blksize != 0)
			SET(st->flags, ST_MODE_BLKSIZE);
		if (st->mode.density != 0)
			SET(st->flags, ST_MODE_DENSITY);
	}
	printf("\n");

	scsi_xsh_set(&st->sc_xsh, link, ststart);

	st->sc_dk.dk_name = st->sc_dev.dv_xname;

	/* Set up the buf queue for this device. */
	bufq_init(&st->sc_bufq, BUFQ_FIFO);

	/* Start up with media position unknown. */
	st->media_fileno = -1;
	st->media_blkno = -1;
	st->media_eom = -1;

	/*
	 * Reset the media loaded flag, sometimes the data
	 * acquired at boot time is not quite accurate.  This
	 * will be checked again at the first open.
	 */
	CLR(link->flags, SDEV_MEDIA_LOADED);

	st->sc_dk.dk_flags = DKF_NOLABELREAD;
	disk_attach(&st->sc_dev, &st->sc_dk);
}

int
stactivate(struct device *self, int act)
{
	struct st_softc *st = (struct st_softc *)self;

	switch (act) {
	case DVACT_DEACTIVATE:
		SET(st->flags, ST_DYING);
		scsi_xsh_del(&st->sc_xsh);
		break;
	}

	return 0;
}

int
stdetach(struct device *self, int flags)
{
	struct st_softc *st = (struct st_softc *)self;
	int cmaj, mn;

	bufq_drain(&st->sc_bufq);

	/* Locate the lowest minor number to be detached. */
	mn = STMINOR(self->dv_unit, 0);

	for (cmaj = 0; cmaj < nchrdev; cmaj++)
		if (cdevsw[cmaj].d_open == stopen)
			vdevgone(cmaj, mn, mn + MAXSTMODES - 1, VCHR);

	bufq_destroy(&st->sc_bufq);

	disk_detach(&st->sc_dk);

	return 0;
}

/*
 * open the device.
 */
int
stopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct scsi_link *link;
	struct st_softc *st;
	int error = 0;

	st = stlookup(STUNIT(dev));
	if (st == NULL)
		return ENXIO;
	if (ISSET(st->flags, ST_DYING)) {
		device_unref(&st->sc_dev);
		return ENXIO;
	}
	link = st->sc_link;

	if ((error = disk_lock(&st->sc_dk)) != 0) {
		device_unref(&st->sc_dev);
		return error;
	}

	if (ISSET(flags, FWRITE) && ISSET(link->flags, SDEV_READONLY)) {
		error = EACCES;
		goto done;
	}

	SC_DEBUG(link, SDEV_DB1, ("open: dev=0x%x (unit %d (of %d))\n", dev,
	    STUNIT(dev), st_cd.cd_ndevs));

	/*
	 * Tape is an exclusive media. Only one open at a time.
	 */
	if (ISSET(link->flags, SDEV_OPEN)) {
		SC_DEBUG(link, SDEV_DB4, ("already open\n"));
		error = EBUSY;
		goto done;
	}

	/* Use st_interpret_sense() now. */
	SET(link->flags, SDEV_OPEN);

	/*
	 * Check the unit status. This clears any outstanding errors and
	 * will ensure that media is present.
	 */
	error = scsi_test_unit_ready(link, TEST_READY_RETRIES,
	    SCSI_SILENT | SCSI_IGNORE_MEDIA_CHANGE |
	    SCSI_IGNORE_ILLEGAL_REQUEST);

	/*
	 * Terminate any existing mount session if there is no media.
	 */
	if (!ISSET(link->flags, SDEV_MEDIA_LOADED))
		st_unmount(st, NOEJECT, DOREWIND);

	if (error != 0) {
		CLR(link->flags, SDEV_OPEN);
		goto done;
	}

	error = st_mount_tape(st, flags);
	if (error != 0) {
		CLR(link->flags, SDEV_OPEN);
		goto done;
	}

	/*
	 * Make sure that a tape opened in write-only mode will have
	 * file marks written on it when closed, even if not written to.
	 * This is for SUN compatibility
	 */
	if ((flags & O_ACCMODE) == FWRITE)
		SET(st->flags, ST_WRITTEN);

done:
	SC_DEBUG(link, SDEV_DB2, ("open complete\n"));
	disk_unlock(&st->sc_dk);
	device_unref(&st->sc_dev);
	return error;
}

/*
 * close the device.. only called if we are the LAST
 * occurrence of an open device
 */
int
stclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct scsi_link *link;
	struct st_softc *st;
	int error = 0;

	st = stlookup(STUNIT(dev));
	if (st == NULL)
		return ENXIO;
	if (ISSET(st->flags, ST_DYING)) {
		error = ENXIO;
		goto done;
	}
	link = st->sc_link;

	disk_lock_nointr(&st->sc_dk);

	SC_DEBUG(link, SDEV_DB1, ("closing\n"));

	if (ISSET(st->flags, ST_WRITTEN) && !ISSET(st->flags, ST_FM_WRITTEN))
		st_write_filemarks(st, 1, 0);

	switch (STMODE(dev)) {
	case 0:		/* /dev/rstN */
		st_unmount(st, NOEJECT, DOREWIND);
		break;
	case 1:		/* /dev/nrstN */
		st_unmount(st, NOEJECT, NOREWIND);
		break;
	case 2:		/* /dev/erstN */
		st_unmount(st, EJECT, DOREWIND);
		break;
	case 3:		/* /dev/enrstN */
		st_unmount(st, EJECT, NOREWIND);
		break;
	}
	CLR(link->flags, SDEV_OPEN);
	scsi_xsh_del(&st->sc_xsh);

	disk_unlock(&st->sc_dk);

done:
	device_unref(&st->sc_dev);
	return error;
}

/*
 * Start a new mount session if needed.
 */
int
st_mount_tape(struct st_softc *st, int flags)
{
	struct scsi_link *link = st->sc_link;
	int error = 0;

	if (ISSET(st->flags, ST_MOUNTED))
		return 0;

	SC_DEBUG(link, SDEV_DB1, ("mounting\n"));

	/*
	 * Assume the media is new and give it a chance to
	 * to do a 'load' instruction.
	 */
	if ((error = st_load(st, LD_LOAD, 0)) != 0)
		goto done;

	/*
	 * Throw another dummy instruction to catch
	 * 'Unit attention' errors. Some drives appear to give
	 * these after doing a Load instruction.
	 * (notably some DAT drives)
	 */
	/* XXX */
	scsi_test_unit_ready(link, TEST_READY_RETRIES, SCSI_SILENT);

	/*
	 * Some devices can't tell you much until they have been
	 * asked to look at the media. This quirk does this.
	 */
	if (ISSET(st->quirks, ST_Q_SENSE_HELP))
		if ((error = st_touch_tape(st)) != 0)
			return error;
	/*
	 * Load the physical device parameters
	 * loads: blkmin, blkmax
	 */
	if (!ISSET(link->flags, SDEV_ATAPI) &&
	    (error = st_read_block_limits(st, 0)) != 0)
		goto done;

	/*
	 * Load the media dependent parameters
	 * includes: media_blksize,media_density
	 * As we have a tape in, it should be reflected here.
	 * If not you may need the "quirk" above.
	 */
	if ((error = st_mode_sense(st, 0)) != 0)
		goto done;

	/*
	 * If we have gained a permanent density from somewhere,
	 * then use it in preference to the one supplied by
	 * default by the driver.
	 */
	if (ISSET(st->flags, ST_MODE_DENSITY))
		st->density = st->mode.density;
	else
		st->density = st->media_density;
	/*
	 * If we have gained a permanent blocksize
	 * then use it in preference to the one supplied by
	 * default by the driver.
	 */
	CLR(st->flags, ST_FIXEDBLOCKS);
	if (ISSET(st->flags, ST_MODE_BLKSIZE)) {
		st->blksize = st->mode.blksize;
		if (st->blksize)
			SET(st->flags, ST_FIXEDBLOCKS);
	} else {
		if ((error = st_decide_mode(st, 0)) != 0)
			goto done;
	}
	if ((error = st_mode_select(st, 0)) != 0) {
		printf("%s: cannot set selected mode\n", st->sc_dev.dv_xname);
		goto done;
	}
	scsi_prevent(link, PR_PREVENT,
	    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_NOT_READY);
	SET(st->flags, ST_MOUNTED);
	SET(link->flags, SDEV_MEDIA_LOADED);	/* move earlier? */
	st->media_fileno = 0;
	st->media_blkno = 0;
	st->media_eom = -1;

done:
	return error;
}

/*
 * End the present mount session.
 * Rewind, and optionally eject the tape.
 * Reset various flags to indicate that all new
 * operations require another mount operation
 */
void
st_unmount(struct st_softc *st, int eject, int rewind)
{
	struct scsi_link *link = st->sc_link;
	int nmarks;

	if (eject == NOEJECT && rewind == NOREWIND) {
		if (ISSET(link->flags, SDEV_MEDIA_LOADED))
			return;
	}

	st->media_fileno = -1;
	st->media_blkno = -1;

	if (!ISSET(st->flags, ST_MOUNTED))
		return;
	SC_DEBUG(link, SDEV_DB1, ("unmounting\n"));
	st_check_eod(st, 0, &nmarks, SCSI_IGNORE_NOT_READY);
	if (rewind == DOREWIND)
		st_rewind(st, 0, SCSI_IGNORE_NOT_READY);
	scsi_prevent(link, PR_ALLOW,
	    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_NOT_READY);
	if (eject == EJECT)
		st_load(st, LD_UNLOAD, SCSI_IGNORE_NOT_READY);
	CLR(st->flags, ST_MOUNTED);
	CLR(link->flags, SDEV_MEDIA_LOADED);
}

/*
 * Given all we know about the device, media, mode, 'quirks' and
 * initial operation, make a decision as to how we should be set
 * to run (regarding blocking and EOD marks)
 */
int
st_decide_mode(struct st_softc *st, int first_read)
{
	struct scsi_link *link = st->sc_link;

	SC_DEBUG(link, SDEV_DB2, ("starting block mode decision\n"));

	/* ATAPI tapes are always fixed blocksize. */
	if (ISSET(link->flags, SDEV_ATAPI)) {
		SET(st->flags, ST_FIXEDBLOCKS);
		if (st->media_blksize > 0)
			st->blksize = st->media_blksize;
		else
			st->blksize = DEF_FIXED_BSIZE;
		goto done;
	}

	/*
	 * If the drive can only handle fixed-length blocks and only at
	 * one size, perhaps we should just do that.
	 */
	if (st->blkmin && (st->blkmin == st->blkmax)) {
		SET(st->flags, ST_FIXEDBLOCKS);
		st->blksize = st->blkmin;
		SC_DEBUG(link, SDEV_DB3,
		    ("blkmin == blkmax of %d\n", st->blkmin));
		goto done;
	}
	/*
	 * If the tape density mandates (or even suggests) use of fixed
	 * or variable-length blocks, comply.
	 */
	switch (st->density) {
	case HALFINCH_800:
	case HALFINCH_1600:
	case HALFINCH_6250:
	case DDS:
		CLR(st->flags, ST_FIXEDBLOCKS);
		st->blksize = 0;
		SC_DEBUG(link, SDEV_DB3, ("density specified variable\n"));
		goto done;
	case QIC_11:
	case QIC_24:
	case QIC_120:
	case QIC_150:
	case QIC_525:
	case QIC_1320:
		SET(st->flags, ST_FIXEDBLOCKS);
		if (st->media_blksize > 0)
			st->blksize = st->media_blksize;
		else
			st->blksize = DEF_FIXED_BSIZE;
		SC_DEBUG(link, SDEV_DB3, ("density specified fixed\n"));
		goto done;
	}
	/*
	 * If we're about to read the tape, perhaps we should choose
	 * fixed or variable-length blocks and block size according to
	 * what the drive found on the tape.
	 */
	if (first_read) {
		if (st->media_blksize > 0)
			SET(st->flags, ST_FIXEDBLOCKS);
		else
			CLR(st->flags, ST_FIXEDBLOCKS);
		st->blksize = st->media_blksize;
		SC_DEBUG(link, SDEV_DB3,
		    ("Used media_blksize of %d\n", st->media_blksize));
		goto done;
	}
	/*
	 * We're getting no hints from any direction.  Choose variable-
	 * length blocks arbitrarily.
	 */
	CLR(st->flags, ST_FIXEDBLOCKS);
	st->blksize = 0;
	SC_DEBUG(link, SDEV_DB3,
	    ("Give up and default to variable mode\n"));

done:
	/*
	 * Decide whether or not to write two file marks to signify end-
	 * of-data.  Make the decision as a function of density.  If
	 * the decision is not to use a second file mark, the SCSI BLANK
	 * CHECK condition code will be recognized as end-of-data when
	 * first read.
	 * (I think this should be a by-product of fixed/variable..julian)
	 */
	switch (st->density) {
/*      case 8 mm:   What is the SCSI density code for 8 mm, anyway? */
	case QIC_11:
	case QIC_24:
	case QIC_120:
	case QIC_150:
	case QIC_525:
	case QIC_1320:
		CLR(st->flags, ST_2FM_AT_EOD);
		break;
	default:
		SET(st->flags, ST_2FM_AT_EOD);
	}
	return 0;
}

/*
 * Actually translate the requested transfer into
 * one the physical driver can understand
 * The transfer is described by a buf and will include
 * only one physical transfer.
 */
void
ststrategy(struct buf *bp)
{
	struct scsi_link *link;
	struct st_softc *st;
	int s;

	st = stlookup(STUNIT(bp->b_dev));
	if (st == NULL) {
		bp->b_error = ENXIO;
		goto bad;
	}
	if (ISSET(st->flags, ST_DYING)) {
		bp->b_error = ENXIO;
		goto bad;
	}
	link = st->sc_link;

	SC_DEBUG(link, SDEV_DB2, ("ststrategy: %ld bytes @ blk %lld\n",
	    bp->b_bcount, (long long)bp->b_blkno));

	/*
	 * If it's a null transfer, return immediately.
	 */
	if (bp->b_bcount == 0)
		goto done;
	/*
	 * Odd sized request on fixed drives are verboten
	 */
	if (ISSET(st->flags, ST_FIXEDBLOCKS)) {
		if (bp->b_bcount % st->blksize) {
			printf("%s: bad request, must be multiple of %d\n",
			    st->sc_dev.dv_xname, st->blksize);
			bp->b_error = EIO;
			goto bad;
		}
	}
	/*
	 * as are out-of-range requests on variable drives.
	 */
	else if (bp->b_bcount < st->blkmin ||
	    (st->blkmax && bp->b_bcount > st->blkmax)) {
		printf("%s: bad request, must be between %d and %d\n",
		    st->sc_dev.dv_xname, st->blkmin, st->blkmax);
		bp->b_error = EIO;
		goto bad;
	}

	/*
	 * Place it in the queue of activities for this tape
	 * at the end (a bit silly because we only have on user..
	 * (but it could fork()))
	 */
	bufq_queue(&st->sc_bufq, bp);

	/*
	 * Tell the device to get going on the transfer if it's
	 * not doing anything, otherwise just wait for completion
	 * (All a bit silly if we're only allowing 1 open but..)
	 */
	scsi_xsh_add(&st->sc_xsh);

	device_unref(&st->sc_dev);
	return;
bad:
	SET(bp->b_flags, B_ERROR);
done:
	/* Set b_resid to indicate no xfer was done. */
	bp->b_resid = bp->b_bcount;
	s = splbio();
	biodone(bp);
	splx(s);
	if (st)
		device_unref(&st->sc_dev);
}

void
ststart(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct st_softc *st = link->device_softc;
	struct buf *bp;
	struct scsi_rw_tape *cmd;
	int s;

	SC_DEBUG(link, SDEV_DB2, ("ststart\n"));

	if (ISSET(st->flags, ST_DYING)) {
		scsi_xs_put(xs);
		return;
	}

	/*
	 * if the device has been unmounted by the user
	 * then throw away all requests until done
	 */
	if (!ISSET(st->flags, ST_MOUNTED) ||
	    !ISSET(link->flags, SDEV_MEDIA_LOADED)) {
		/* make sure that one implies the other.. */
		CLR(link->flags, SDEV_MEDIA_LOADED);
		bufq_drain(&st->sc_bufq);
		scsi_xs_put(xs);
		return;
	}

	for (;;) {
		bp = bufq_dequeue(&st->sc_bufq);
		if (bp == NULL) {
			scsi_xs_put(xs);
			return;
		}

		/*
		 * Only FIXEDBLOCK devices have pending I/O or space
		 * operations.
		 */
		if (ISSET(st->flags, ST_FIXEDBLOCKS)) {
			/*
			 * If we are at a filemark but have not reported it yet
			 * then we should report it now
			 */
			if (ISSET(st->flags, ST_AT_FILEMARK)) {
				if ((bp->b_flags & B_READ) == B_WRITE) {
					/*
					 * Handling of ST_AT_FILEMARK in
					 * st_space will fill in the right file
					 * mark count.
					 * Back up over filemark
					 */
					if (st_space(st, 0, SP_FILEMARKS, 0)) {
						SET(bp->b_flags, B_ERROR);
						bp->b_resid = bp->b_bcount;
						bp->b_error = EIO;
						s = splbio();
						biodone(bp);
						splx(s);
						continue;
					}
				} else {
					bp->b_resid = bp->b_bcount;
					bp->b_error = 0;
					CLR(bp->b_flags, B_ERROR);
					CLR(st->flags, ST_AT_FILEMARK);
					s = splbio();
					biodone(bp);
					splx(s);
					continue;	/* seek more work */
				}
			}
		}

		/*
		 * If we are at EIO or EOM but have not reported it
		 * yet then we should report it now.
		 */
		if (ISSET(st->flags, ST_EOM_PENDING | ST_EIO_PENDING)) {
			bp->b_resid = bp->b_bcount;
			if (ISSET(st->flags, ST_EIO_PENDING)) {
				bp->b_error = EIO;
				SET(bp->b_flags, B_ERROR);
			}
			CLR(st->flags, ST_EOM_PENDING | ST_EIO_PENDING);
			s = splbio();
			biodone(bp);
			splx(s);
			continue;	/* seek more work */
		}
		break;
	}

	/*
	 *  Fill out the scsi command
	 */
	cmd = (struct scsi_rw_tape *)&xs->cmd;
	bzero(cmd, sizeof(*cmd));
	if ((bp->b_flags & B_READ) == B_WRITE) {
		cmd->opcode = WRITE;
		CLR(st->flags, ST_FM_WRITTEN);
		SET(st->flags, ST_WRITTEN);
		SET(xs->flags, SCSI_DATA_OUT);
	} else {
		cmd->opcode = READ;
		SET(xs->flags, SCSI_DATA_IN);
	}

	/*
	 * Handle "fixed-block-mode" tape drives by using the
	 * block count instead of the length.
	 */
	if (ISSET(st->flags, ST_FIXEDBLOCKS)) {
		SET(cmd->byte2, SRW_FIXED);
		_lto3b(bp->b_bcount / st->blksize, cmd->len);
	} else
		_lto3b(bp->b_bcount, cmd->len);

	if (st->media_blkno != -1) {
		/* Update block count now, errors will set it to -1. */
		if (ISSET(st->flags, ST_FIXEDBLOCKS))
			st->media_blkno += _3btol(cmd->len);
		else if (_3btol(cmd->len) != 0)
			st->media_blkno++;
	}

	xs->cmdlen = sizeof(*cmd);
	xs->timeout = ST_IO_TIME;
	xs->data = bp->b_data;
	xs->datalen = bp->b_bcount;
	xs->done = st_buf_done;
	xs->cookie = bp;
	xs->bp = bp;

	/* Instrumentation. */
	disk_busy(&st->sc_dk);

	/*
	 * go ask the adapter to do all this for us
	 */
	scsi_xs_exec(xs);

	/*
	 * should we try do more work now?
	 */
	if (bufq_peek(&st->sc_bufq))
		scsi_xsh_add(&st->sc_xsh);
}

void
st_buf_done(struct scsi_xfer *xs)
{
	struct st_softc *st = xs->sc_link->device_softc;
	struct buf *bp = xs->cookie;
	int error, s;

	switch (xs->error) {
	case XS_NOERROR:
		bp->b_error = 0;
		CLR(bp->b_flags, B_ERROR);
		bp->b_resid = xs->resid;
		break;

	case XS_SENSE:
	case XS_SHORTSENSE:
		SC_DEBUG_SENSE(xs);
		error = st_interpret_sense(xs);
		if (error == 0) {
			bp->b_error = 0;
			CLR(bp->b_flags, B_ERROR);
			bp->b_resid = xs->resid;
			break;
		}
		if (error != ERESTART)
			xs->retries = 0;
		goto retry;

	case XS_BUSY:
		if (xs->retries) {
			if (scsi_delay(xs, 1) != ERESTART)
				xs->retries = 0;
		}
		goto retry;

	case XS_TIMEOUT:
 retry:
		if (xs->retries--) {
			scsi_xs_exec(xs);
			return;
		}
		/* FALLTHROUGH */

	default:
		bp->b_error = EIO;
		SET(bp->b_flags, B_ERROR);
		bp->b_resid = bp->b_bcount;
		break;
	}

	disk_unbusy(&st->sc_dk, bp->b_bcount - xs->resid, bp->b_blkno,
		bp->b_flags & B_READ);

	s = splbio();
	biodone(bp);
	splx(s);
	scsi_xs_put(xs);
}

void
stminphys(struct buf *bp)
{
	struct scsi_link	*link;
	struct st_softc		*sc;

	sc = stlookup(STUNIT(bp->b_dev));
	if (sc == NULL)
		return;
	link = sc->sc_link;

	if (link->bus->sb_adapter->dev_minphys != NULL)
		(*link->bus->sb_adapter->dev_minphys)(bp, link);
	else
		minphys(bp);

	device_unref(&sc->sc_dev);
}

int
stread(dev_t dev, struct uio *uio, int iomode)
{
	return physio(ststrategy, dev, B_READ, stminphys, uio);
}

int
stwrite(dev_t dev, struct uio *uio, int iomode)
{
	return physio(ststrategy, dev, B_WRITE, stminphys, uio);
}

/*
 * Perform special action on behalf of the user;
 * knows about the internals of this device
 */
int
stioctl(dev_t dev, u_long cmd, caddr_t arg, int flag, struct proc *p)
{
	int error = 0;
	int nmarks;
	int flags = 0;
	struct st_softc *st;
	int hold_blksize;
	u_int8_t hold_density;
	struct mtop *mt = (struct mtop *) arg;
	int number;

	/*
	 * Find the device that the user is talking about
	 */
	st = stlookup(STUNIT(dev));
	if (st == NULL)
		return ENXIO;

	if (ISSET(st->flags, ST_DYING)) {
		error = ENXIO;
		goto done;
	}

	hold_blksize = st->blksize;
	hold_density = st->density;

	switch (cmd) {

	case MTIOCGET: {
		struct mtget *g = (struct mtget *) arg;

		/*
		 * (to get the current state of READONLY)
		 */
		error = st_mode_sense(st, SCSI_SILENT);
		if (error != 0)
			break;

		SC_DEBUG(st->sc_link, SDEV_DB1, ("[ioctl: get status]\n"));
		bzero(g, sizeof(struct mtget));
		g->mt_type = 0x7;	/* Ultrix compat *//*? */
		g->mt_blksiz = st->blksize;
		g->mt_density = st->density;
		g->mt_mblksiz = st->mode.blksize;
		g->mt_mdensity = st->mode.density;
		if (ISSET(st->sc_link->flags, SDEV_READONLY))
			SET(g->mt_dsreg, MT_DS_RDONLY);
		if (ISSET(st->flags, ST_MOUNTED))
			SET(g->mt_dsreg, MT_DS_MOUNTED);
		g->mt_resid = st->mt_resid;
		g->mt_erreg = st->mt_erreg;
		g->mt_fileno = st->media_fileno;
		g->mt_blkno = st->media_blkno;
		/*
		 * clear latched errors.
		 */
		st->mt_resid = 0;
		st->mt_erreg = 0;
		break;
	}
	case MTIOCTOP: {

		SC_DEBUG(st->sc_link, SDEV_DB1,
		    ("[ioctl: op=0x%x count=0x%x]\n", mt->mt_op, mt->mt_count));

		number = mt->mt_count;
		switch (mt->mt_op) {
		case MTWEOF:	/* write an end-of-file record */
			error = st_write_filemarks(st, number, flags);
			break;
		case MTBSF:	/* backward space file */
			number = -number;
		case MTFSF:	/* forward space file */
			error = st_check_eod(st, 0, &nmarks, flags);
			if (error == 0)
				error = st_space(st, number - nmarks,
				    SP_FILEMARKS, flags);
			break;
		case MTBSR:	/* backward space record */
			number = -number;
		case MTFSR:	/* forward space record */
			error = st_check_eod(st, 1, &nmarks, flags);
			if (error == 0)
				error = st_space(st, number, SP_BLKS, flags);
			break;
		case MTREW:	/* rewind */
			error = st_rewind(st, 0, flags);
			break;
		case MTOFFL:	/* rewind and put the drive offline */
			st_unmount(st, EJECT, DOREWIND);
			break;
		case MTNOP:	/* no operation, sets status only */
			break;
		case MTRETEN:	/* retension the tape */
			error = st_load(st, LD_RETENSION, flags);
			if (error == 0)
				error = st_load(st, LD_LOAD, flags);
			break;
		case MTEOM:	/* forward space to end of media */
			error = st_check_eod(st, 0, &nmarks, flags);
			if (error == 0)
				error = st_space(st, 1, SP_EOM, flags);
			break;
		case MTCACHE:	/* enable controller cache */
			CLR(st->flags, ST_DONTBUFFER);
			goto try_new_value;
		case MTNOCACHE:	/* disable controller cache */
			SET(st->flags, ST_DONTBUFFER);
			goto try_new_value;
		case MTERASE:	/* erase volume */
			error = st_erase(st, number, flags);
			break;
		case MTSETBSIZ:	/* Set block size for device and mode. */
			if (number == 0) {
				CLR(st->flags, ST_FIXEDBLOCKS);
			} else {
				if ((st->blkmin || st->blkmax) &&
				    (number < st->blkmin ||
				    number > st->blkmax)) {
					error = EINVAL;
					break;
				}
				SET(st->flags, ST_FIXEDBLOCKS);
			}
			st->blksize = number;
			goto try_new_value;

		case MTSETDNSTY:	/* Set density for device and mode. */
			if (number < 0 || number > SCSI_MAX_DENSITY_CODE) {
				error = EINVAL;
				break;
			}
			st->density = number;
			goto try_new_value;

		default:
			error = EINVAL;
		}
		break;
	}
	case MTIOCIEOT:
	case MTIOCEEOT:
		break;

#if 0
	case MTIOCRDSPOS:
		error = st_rdpos(st, 0, (u_int32_t *) arg);
		break;

	case MTIOCRDHPOS:
		error = st_rdpos(st, 1, (u_int32_t *) arg);
		break;

	case MTIOCSLOCATE:
		error = st_setpos(st, 0, (u_int32_t *) arg);
		break;

	case MTIOCHLOCATE:
		error = st_setpos(st, 1, (u_int32_t *) arg);
		break;
#endif /* 0 */

	default:
		error = scsi_do_ioctl(st->sc_link, cmd, arg, flag);
		break;
	}
	goto done;

try_new_value:
	/*
	 * Check that the mode being asked for is agreeable to the
	 * drive. If not, put it back the way it was.
	 */
	if ((error = st_mode_select(st, 0)) != 0) {/* put it back as it was */
		printf("%s: cannot set selected mode\n", st->sc_dev.dv_xname);
		st->density = hold_density;
		st->blksize = hold_blksize;
		if (st->blksize)
			SET(st->flags, ST_FIXEDBLOCKS);
		else
			CLR(st->flags, ST_FIXEDBLOCKS);
		goto done;
	}
	/*
	 * As the drive liked it, if we are setting a new default,
	 * set it into the structures as such.
	 */
	switch (mt->mt_op) {
	case MTSETBSIZ:
		st->mode.blksize = st->blksize;
		SET(st->flags, ST_MODE_BLKSIZE);
		break;
	case MTSETDNSTY:
		st->mode.density = st->density;
		SET(st->flags, ST_MODE_DENSITY);
		break;
	}

done:
	device_unref(&st->sc_dev);
	return error;
}

/*
 * Do a synchronous read.
 */
int
st_read(struct st_softc *st, char *buf, int size, int flags)
{
	struct scsi_rw_tape *cmd;
	struct scsi_xfer *xs;
	int error;

	if (size == 0)
		return 0;

	xs = scsi_xs_get(st->sc_link, flags | SCSI_DATA_IN);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = buf;
	xs->datalen = size;
	xs->retries = 0;
	xs->timeout = ST_IO_TIME;

	cmd = (struct scsi_rw_tape *)&xs->cmd;
	cmd->opcode = READ;
	if (ISSET(st->flags, ST_FIXEDBLOCKS)) {
		SET(cmd->byte2, SRW_FIXED);
		_lto3b(size / (st->blksize ? st->blksize : DEF_FIXED_BSIZE),
		    cmd->len);
	} else
		_lto3b(size, cmd->len);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Ask the drive what its min and max blk sizes are.
 */
int
st_read_block_limits(struct st_softc *st, int flags)
{
	struct scsi_block_limits_data *block_limits = NULL;
	struct scsi_block_limits *cmd;
	struct scsi_link *link = st->sc_link;
	struct scsi_xfer *xs;
	int error = 0;

	if (ISSET(link->flags, SDEV_MEDIA_LOADED))
		return 0;

	block_limits = dma_alloc(sizeof(*block_limits), PR_NOWAIT);
	if (block_limits == NULL)
		return ENOMEM;

	xs = scsi_xs_get(link, flags | SCSI_DATA_IN);
	if (xs == NULL) {
		error = ENOMEM;
		goto done;
	}

	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)block_limits;
	xs->datalen = sizeof(*block_limits);
	xs->timeout = ST_CTL_TIME;

	cmd = (struct scsi_block_limits *)&xs->cmd;
	cmd->opcode = READ_BLOCK_LIMITS;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0) {
		st->blkmin = _2btol(block_limits->min_length);
		st->blkmax = _3btol(block_limits->max_length);
		SC_DEBUG(link, SDEV_DB3,
		    ("(%d <= blksize <= %d)\n", st->blkmin, st->blkmax));
	}

done:
	if (block_limits)
		dma_free(block_limits, sizeof(*block_limits));
	return error;
}

/*
 * Get the scsi driver to send a full inquiry to the
 * device and use the results to fill out the global
 * parameter structure.
 *
 * called from:
 * attach
 * open
 * ioctl (to reset original blksize)
 */
int
st_mode_sense(struct st_softc *st, int flags)
{
	union scsi_mode_sense_buf *data = NULL;
	struct scsi_link *link = st->sc_link;
	u_int64_t block_count;
	u_int32_t density, block_size;
	u_char *page0 = NULL;
	u_int8_t dev_spec;
	int error = 0, big;

	data = dma_alloc(sizeof(*data), PR_NOWAIT);
	if (data == NULL)
		return ENOMEM;

	/*
	 * Ask for page 0 (vendor specific) mode sense data.
	 */
	density = 0;
	block_count = 0;
	block_size = 0;
	error = scsi_do_mode_sense(link, 0, data, (void **)&page0, 1,
	    flags | SCSI_SILENT, &big);
	if (error != 0)
		goto done;
	scsi_parse_blkdesc(link, data, big, &density, &block_count,
	    &block_size);

	/* It is valid for no page0 to be available. */

	if (big)
		dev_spec = data->hdr_big.dev_spec;
	else
		dev_spec = data->hdr.dev_spec;

	if (ISSET(dev_spec, SMH_DSP_WRITE_PROT))
		SET(link->flags, SDEV_READONLY);
	else
		CLR(link->flags, SDEV_READONLY);

	st->media_blksize = block_size;
	st->media_density = density;

	SC_DEBUG(link, SDEV_DB3,
	    ("density code 0x%x, %d-byte blocks, write-%s, %sbuffered\n",
	    st->media_density, st->media_blksize,
	    ISSET(link->flags, SDEV_READONLY) ? "protected" : "enabled",
	    ISSET(dev_spec, SMH_DSP_BUFF_MODE) ? "" : "un"));

	SET(link->flags, SDEV_MEDIA_LOADED);

 done:
	if (data)
		dma_free(data, sizeof(*data));
	return error;
}

/*
 * Send a filled out parameter structure to the drive to
 * set it into the desire mode etc.
 */
int
st_mode_select(struct st_softc *st, int flags)
{
	union scsi_mode_sense_buf *inbuf = NULL, *outbuf = NULL;
	struct scsi_blk_desc general;
	struct scsi_link *link = st->sc_link;
	u_int8_t *page0 = NULL;
	int error = 0, big, page0_size;

	inbuf = dma_alloc(sizeof(*inbuf), PR_NOWAIT);
	if (inbuf == NULL) {
		error = ENOMEM;
		goto done;
	}
	outbuf = dma_alloc(sizeof(*outbuf), PR_NOWAIT | PR_ZERO);
	if (outbuf == NULL) {
		error = ENOMEM;
		goto done;
	}

	/*
	 * This quirk deals with drives that have only one valid mode and think
	 * this gives them license to reject all mode selects, even if the
	 * selected mode is the one that is supported.
	 */
	if (ISSET(st->quirks, ST_Q_UNIMODAL)) {
		SC_DEBUG(link, SDEV_DB3,
		    ("not setting density 0x%x blksize 0x%x\n",
		    st->density, st->blksize));
		error = 0;
		goto done;
	}

	if (ISSET(link->flags, SDEV_ATAPI)) {
		error = 0;
		goto done;
	}

	bzero(&general, sizeof(general));

	general.density = st->density;
	if (ISSET(st->flags, ST_FIXEDBLOCKS))
		_lto3b(st->blksize, general.blklen);

	/*
	 * Ask for page 0 (vendor specific) mode sense data.
	 *
	 * page0 == NULL is a valid situation.
	 */
	error = scsi_do_mode_sense(link, 0, inbuf, (void **)&page0, 1,
	    flags | SCSI_SILENT, &big);
	if (error != 0)
		goto done;

	if (page0 == NULL) {
		page0_size = 0;
	} else if (big == 0) {
		page0_size = inbuf->hdr.data_length +
		    sizeof(inbuf->hdr.data_length) - sizeof(inbuf->hdr) -
		    inbuf->hdr.blk_desc_len;
		memcpy(&outbuf->buf[sizeof(outbuf->hdr)+ sizeof(general)],
		    page0, page0_size);
	} else {
		page0_size = _2btol(inbuf->hdr_big.data_length) +
		    sizeof(inbuf->hdr_big.data_length) -
		    sizeof(inbuf->hdr_big) -
		    _2btol(inbuf->hdr_big.blk_desc_len);
		memcpy(&outbuf->buf[sizeof(outbuf->hdr_big) + sizeof(general)],
		    page0, page0_size);
	}

	/*
	 * Set up for a mode select.
	 */
	if (big == 0) {
		outbuf->hdr.data_length = sizeof(outbuf->hdr) +
		    sizeof(general) + page0_size -
		    sizeof(outbuf->hdr.data_length);
		if (!ISSET(st->flags, ST_DONTBUFFER))
			outbuf->hdr.dev_spec = SMH_DSP_BUFF_MODE_ON;
		outbuf->hdr.blk_desc_len = sizeof(general);
		memcpy(&outbuf->buf[sizeof(outbuf->hdr)],
		    &general, sizeof(general));
		error = scsi_mode_select(st->sc_link, 0, &outbuf->hdr,
		    flags, ST_CTL_TIME);
		goto done;
	}

	/* MODE SENSE (10) header was returned, so use MODE SELECT (10). */
	_lto2b((sizeof(outbuf->hdr_big) + sizeof(general) + page0_size -
	    sizeof(outbuf->hdr_big.data_length)), outbuf->hdr_big.data_length);
	if (!ISSET(st->flags, ST_DONTBUFFER))
		outbuf->hdr_big.dev_spec = SMH_DSP_BUFF_MODE_ON;
	_lto2b(sizeof(general), outbuf->hdr_big.blk_desc_len);
	memcpy(&outbuf->buf[sizeof(outbuf->hdr_big)], &general,
	    sizeof(general));

	error = scsi_mode_select_big(st->sc_link, 0, &outbuf->hdr_big,
	    flags, ST_CTL_TIME);
done:
	if (inbuf)
		dma_free(inbuf, sizeof(*inbuf));
	if (outbuf)
		dma_free(outbuf, sizeof(*outbuf));
	return error;
}

/*
 * issue an erase command
 */
int
st_erase(struct st_softc *st, int full, int flags)
{
	struct scsi_erase *cmd;
	struct scsi_xfer *xs;
	int error;

	xs = scsi_xs_get(st->sc_link, flags);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);

	/*
	 * Full erase means set LONG bit in erase command, which asks
	 * the drive to erase the entire unit.  Without this bit, we're
	 * asking the drive to write an erase gap.
	 */
	cmd = (struct scsi_erase *)&xs->cmd;
	cmd->opcode = ERASE;
	if (full) {
		cmd->byte2 = SE_IMMED|SE_LONG;
		xs->timeout = ST_SPC_TIME;
	} else {
		cmd->byte2 = SE_IMMED;
		xs->timeout = ST_IO_TIME;
	}

	/*
	 * XXX We always do this asynchronously, for now.  How long should
	 * we wait if we want to (eventually) to it synchronously?
	 */
	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * skip N blocks/filemarks/seq filemarks/eom
 */
int
st_space(struct st_softc *st, int number, u_int what, int flags)
{
	struct scsi_space *cmd;
	struct scsi_xfer *xs;
	int error;

	switch (what) {
	case SP_BLKS:
		if (ISSET(st->flags, ST_PER_ACTION)) {
			if (number > 0) {
				CLR(st->flags, ST_PER_ACTION);
				return EIO;
			} else if (number < 0) {
				if (ISSET(st->flags, ST_AT_FILEMARK)) {
					/*
					 * Handling of ST_AT_FILEMARK
					 * in st_space will fill in the
					 * right file mark count.
					 */
					error = st_space(st, 0, SP_FILEMARKS,
					    flags);
					if (error != 0)
						return error;
				}
				if (ISSET(st->flags, ST_BLANK_READ)) {
					CLR(st->flags, ST_BLANK_READ);
					return EIO;
				}
				CLR(st->flags, ST_EIO_PENDING | ST_EOM_PENDING);
			}
		}
		break;
	case SP_FILEMARKS:
		if (ISSET(st->flags,  ST_EIO_PENDING)) {
			if (number > 0) {
				/* pretend we just discovered the error */
				CLR(st->flags, ST_EIO_PENDING);
				return EIO;
			} else if (number < 0) {
				/* back away from the error */
				CLR(st->flags, ST_EIO_PENDING);
			}
		}
		if (ISSET(st->flags, ST_AT_FILEMARK)) {
			CLR(st->flags, ST_AT_FILEMARK);
			number--;
		}
		if (ISSET(st->flags, ST_BLANK_READ) && (number < 0)) {
			/* back away from unwritten tape */
			CLR(st->flags, ST_BLANK_READ);
			number++;	/* XXX dubious */
		}
		break;
	case SP_EOM:
		if (ISSET(st->flags, ST_EOM_PENDING)) {
			/* We are already there. */
			CLR(st->flags, ST_EOM_PENDING);
			return 0;
		}
		if (ISSET(st->flags, ST_EIO_PENDING)) {
			/* pretend we just discovered the error */
			CLR(st->flags, ST_EIO_PENDING);
			return EIO;
		}
		if (ISSET(st->flags, ST_AT_FILEMARK))
			CLR(st->flags, ST_AT_FILEMARK);
		break;
	}
	if (number == 0)
		return 0;

	xs = scsi_xs_get(st->sc_link, flags);
	if (xs == NULL)
		return ENOMEM;

	cmd = (struct scsi_space *)&xs->cmd;
	cmd->opcode = SPACE;
	cmd->byte2 = what;
	_lto3b(number, cmd->number);
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = ST_SPC_TIME;

	CLR(st->flags, ST_EOD_DETECTED);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error != 0) {
		st->media_fileno = -1;
		st->media_blkno = -1;
	} else {
		switch (what) {
		case SP_BLKS:
			if (st->media_blkno != -1) {
				st->media_blkno += number;
				if (st->media_blkno < 0)
					st->media_blkno = -1;
			}
			break;
		case SP_FILEMARKS:
			if (st->media_fileno != -1) {
				if (!ISSET(st->flags, ST_EOD_DETECTED))
					st->media_fileno += number;
				if (st->media_fileno > st->media_eom)
					st->media_eom = st->media_fileno;
				st->media_blkno = 0;
			}
			break;
		case SP_EOM:
			if (st->media_eom != -1) {
				st->media_fileno = st->media_eom;
				st->media_blkno = 0;
			} else {
				st->media_fileno = -1;
				st->media_blkno = -1;
			}
			break;
		default:
			st->media_fileno = -1;
			st->media_blkno = -1;
			break;
		}
	}

	return error;
}

/*
 * write N filemarks
 */
int
st_write_filemarks(struct st_softc *st, int number, int flags)
{
	struct scsi_write_filemarks *cmd;
	struct scsi_xfer *xs;
	int error;

	if (number < 0)
		return EINVAL;

	xs = scsi_xs_get(st->sc_link, flags);
	if (xs == NULL)
		return ENOMEM;

	xs->cmdlen = sizeof(*cmd);
	xs->timeout = ST_IO_TIME * 4;

	switch (number) {
	case 0:		/* really a command to sync the drive's buffers */
		break;
	case 1:
		if (ISSET(st->flags, ST_FM_WRITTEN))	/* already have one down */
			CLR(st->flags, ST_WRITTEN);
		else
			SET(st->flags, ST_FM_WRITTEN);
		CLR(st->flags, ST_PER_ACTION);
		break;
	default:
		CLR(st->flags, ST_PER_ACTION | ST_WRITTEN);
		break;
	}

	cmd = (struct scsi_write_filemarks *)&xs->cmd;
	cmd->opcode = WRITE_FILEMARKS;
	_lto3b(number, cmd->number);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error != 0) {
		st->media_fileno = -1;
		st->media_blkno = -1;
		st->media_eom = -1;
	} else if (st->media_fileno != -1) {
		st->media_fileno += number;
		st->media_eom = st->media_fileno;
		st->media_blkno = 0;
	}

	return error;
}

/*
 * Make sure the right number of file marks is on tape if the
 * tape has been written.  If the position argument is true,
 * leave the tape positioned where it was originally.
 *
 * nmarks returns the number of marks to skip (or, if position
 * true, which were skipped) to get back original position.
 */
int
st_check_eod(struct st_softc *st, int position, int *nmarks, int flags)
{
	int error;

	switch (st->flags & (ST_WRITTEN | ST_FM_WRITTEN | ST_2FM_AT_EOD)) {
	default:
		*nmarks = 0;
		return 0;
	case ST_WRITTEN:
	case ST_WRITTEN | ST_FM_WRITTEN | ST_2FM_AT_EOD:
		*nmarks = 1;
		break;
	case ST_WRITTEN | ST_2FM_AT_EOD:
		*nmarks = 2;
	}
	error = st_write_filemarks(st, *nmarks, flags);
	if (error == 0 && position != 0)
		error = st_space(st, -*nmarks, SP_FILEMARKS, flags);
	return error;
}

/*
 * load/unload/retension
 */
int
st_load(struct st_softc *st, u_int type, int flags)
{
	struct scsi_load *cmd;
	struct scsi_xfer *xs;
	int error, nmarks;

	st->media_fileno = -1;
	st->media_blkno = -1;
	st->media_eom = -1;

	if (type != LD_LOAD) {
		error = st_check_eod(st, 0, &nmarks, flags);
		if (error != 0)
			return error;
	}

	if (ISSET(st->quirks, ST_Q_IGNORE_LOADS)) {
		if (type == LD_LOAD) {
			/*
			 * If we ignore loads, at least we should try a rewind.
			 */
			return st_rewind(st, 0, flags);
		}
		return 0;
	}


	xs = scsi_xs_get(st->sc_link, flags);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = ST_SPC_TIME;

	cmd = (struct scsi_load *)&xs->cmd;
	cmd->opcode = LOAD;
	cmd->how = type;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 *  Rewind the device
 */
int
st_rewind(struct st_softc *st, u_int immediate, int flags)
{
	struct scsi_rewind *cmd;
	struct scsi_xfer *xs;
	int error, nmarks;

	error = st_check_eod(st, 0, &nmarks, flags);
	if (error != 0)
		return error;
	CLR(st->flags, ST_PER_ACTION);

	xs = scsi_xs_get(st->sc_link, flags);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = immediate ? ST_CTL_TIME : ST_SPC_TIME;

	cmd = (struct scsi_rewind *)&xs->cmd;
	cmd->opcode = REWIND;
	cmd->byte2 = immediate;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0) {
		st->media_fileno = 0;
		st->media_blkno = 0;
	}

	return error;
}

/*
 * Look at the returned sense and act on the error to determine
 * the unix error number to pass back... (0 = report no error)
 *                            (-1 = continue processing)
 */
int
st_interpret_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data *sense = &xs->sense;
	struct scsi_link *link = xs->sc_link;
	struct scsi_space *space;
	struct st_softc *st = link->device_softc;
	u_int8_t serr = sense->error_code & SSD_ERRCODE;
	u_int8_t skey = sense->flags & SSD_KEY;
	int32_t resid, info, number;
	int datalen;

	if (!ISSET(link->flags, SDEV_OPEN) ||
	    (serr != SSD_ERRCODE_CURRENT && serr != SSD_ERRCODE_DEFERRED))
		return scsi_interpret_sense(xs);

	info = (int32_t)_4btol(sense->info);

	switch (skey) {

	/*
	 * We do custom processing in st for the unit becoming ready case.
	 * in this case we do not allow xs->retries to be decremented
	 * only on the "Unit Becoming Ready" case. This is because tape
	 * drives report "Unit Becoming Ready" when loading media, etc.
	 * and can take a long time.  Rather than having a massive timeout
	 * for all operations (which would cause other problems) we allow
	 * operations to wait (but be interruptible with Ctrl-C) forever
	 * as long as the drive is reporting that it is becoming ready.
	 * all other cases are handled as per the default.
	 */

	case SKEY_NOT_READY:
		if (ISSET(xs->flags, SCSI_IGNORE_NOT_READY))
			return 0;
		switch (ASC_ASCQ(sense)) {
		case SENSE_NOT_READY_BECOMING_READY:
			SC_DEBUG(link, SDEV_DB1, ("not ready: busy (%#x)\n",
			    sense->add_sense_code_qual));
			/* don't count this as a retry */
			xs->retries++;
			return scsi_delay(xs, 1);
		default:
			return scsi_interpret_sense(xs);
		}
		break;
	case SKEY_BLANK_CHECK:
		if (sense->error_code & SSD_ERRCODE_VALID &&
		    xs->cmd.opcode == SPACE) {
			switch (ASC_ASCQ(sense)) {
			case SENSE_END_OF_DATA_DETECTED:
				SET(st->flags, ST_EOD_DETECTED);
				space = (struct scsi_space *)&xs->cmd;
				number = _3btol(space->number);
				st->media_fileno = number - info;
				st->media_eom = st->media_fileno;
				return 0;
			case SENSE_BEGINNING_OF_MEDIUM_DETECTED:
				/* Standard says: Position is undefined! */
				SET(st->flags, ST_BOD_DETECTED);
				st->media_fileno = -1;
				st->media_blkno = -1;
				return 0;
			}
		}
		break;
	case SKEY_NO_SENSE:
	case SKEY_RECOVERED_ERROR:
	case SKEY_MEDIUM_ERROR:
	case SKEY_VOLUME_OVERFLOW:
		break;
	default:
		return scsi_interpret_sense(xs);
	}

	/*
	 * 'resid' can be in units of st->blksize or bytes. xs->resid and
	 * xs->datalen are always in units of bytes. So we need a variable
	 * to store datalen in the same units as resid and to adjust
	 * xs->resid to be in bytes.
	 */
	if (ISSET(sense->error_code, SSD_ERRCODE_VALID)) {
		if (ISSET(st->flags, ST_FIXEDBLOCKS))
			resid = info * st->blksize; /* XXXX overflow? */
		else
			resid = info;
	} else {
		resid = xs->datalen;
	}

	if (resid < 0 || resid > xs->datalen)
		xs->resid = xs->datalen;
	else
		xs->resid = resid;

	datalen = xs->datalen;
	if (ISSET(st->flags, ST_FIXEDBLOCKS)) {
		resid /= st->blksize;
		datalen /= st->blksize;
	}

	if (ISSET(sense->flags, SSD_FILEMARK)) {
		if (st->media_fileno != -1) {
			st->media_fileno++;
			if (st->media_fileno > st->media_eom)
				st->media_eom = st->media_fileno;
			st->media_blkno = 0;
		}
		if (!ISSET(st->flags, ST_FIXEDBLOCKS))
			return 0;
		SET(st->flags, ST_AT_FILEMARK);
	}

	if (ISSET(sense->flags, SSD_EOM)) {
		SET(st->flags, ST_EOM_PENDING);
		xs->resid = 0;
		if (ISSET(st->flags, ST_FIXEDBLOCKS))
			return 0;
	}

	if (ISSET(sense->flags, SSD_ILI)) {
		if (!ISSET(st->flags, ST_FIXEDBLOCKS)) {
			if (resid >= 0 && resid <= datalen)
				return 0;
			if (!ISSET(xs->flags, SCSI_SILENT))
				printf( "%s: bad residual %d out of "
				    "%d\n", st->sc_dev.dv_xname, resid,
				    datalen);
			return EIO;
		}

		/* Fixed size blocks. */
		if (ISSET(sense->error_code, SSD_ERRCODE_VALID))
			if (!ISSET(xs->flags, SCSI_SILENT))
				printf("%s: block wrong size, %d blocks "
				    "residual\n", st->sc_dev.dv_xname, resid);
		SET(st->flags, ST_EIO_PENDING);
		/*
		 * This quirk code helps the drive read the first tape block,
		 * regardless of format.  That is required for these drives to
		 * return proper MODE SENSE information.
		 */
		if (ISSET(st->quirks, ST_Q_SENSE_HELP) &&
		    !ISSET(link->flags, SDEV_MEDIA_LOADED))
			st->blksize -= 512;
	}

	if (ISSET(st->flags, ST_FIXEDBLOCKS) && xs->resid == xs->datalen) {
		if (ISSET(st->flags, ST_EIO_PENDING))
			return EIO;
		if (ISSET(st->flags, ST_AT_FILEMARK))
			return 0;
	}

	if (skey == SKEY_BLANK_CHECK) {
		/*
		 * This quirk code helps the drive read the first tape block,
		 * regardless of format.  That is required for these drives to
		 * return proper MODE SENSE information.
		 */
		if (ISSET(st->quirks, ST_Q_SENSE_HELP) &&
		    !ISSET(link->flags, SDEV_MEDIA_LOADED)) {
			/* still starting */
			st->blksize -= 512;
		} else if (!ISSET(st->flags, ST_2FM_AT_EOD | ST_BLANK_READ)) {
			SET(st->flags, ST_BLANK_READ);
			SET(st->flags, ST_EOM_PENDING);
			xs->resid = xs->datalen;
			return 0;
		}
	}

	return scsi_interpret_sense(xs);
}

/*
 * The quirk here is that the drive returns some value to st_mode_sense
 * incorrectly until the tape has actually passed by the head.
 *
 * The method is to set the drive to large fixed-block state (user-specified
 * density and 1024-byte blocks), then read and rewind to get it to sense the
 * tape.  If that doesn't work, try 512-byte fixed blocks.  If that doesn't
 * work, as a last resort, try variable- length blocks.  The result will be
 * the ability to do an accurate st_mode_sense.
 *
 * We know we can do a rewind because we just did a load, which implies rewind.
 * Rewind seems preferable to space backward if we have a virgin tape.
 *
 * The rest of the code for this quirk is in ILI processing and BLANK CHECK
 * error processing, both part of st_interpret_sense.
 */
int
st_touch_tape(struct st_softc *st)
{
	char *buf = NULL;
	int readsize, maxblksize = 1024;
	int error = 0;

	if ((error = st_mode_sense(st, 0)) != 0)
		goto done;
	buf = dma_alloc(maxblksize, PR_NOWAIT);
	if (buf == NULL) {
		error = ENOMEM;
		goto done;
	}

	st->blksize = 1024;
	do {
		switch (st->blksize) {
		case 512:
		case 1024:
			readsize = st->blksize;
			SET(st->flags, ST_FIXEDBLOCKS);
			break;
		default:
			readsize = 1;
			CLR(st->flags, ST_FIXEDBLOCKS);
		}
		if ((error = st_mode_select(st, 0)) != 0)
			goto done;
		st_read(st, buf, readsize, SCSI_SILENT);	/* XXX */
		if ((error = st_rewind(st, 0, 0)) != 0)
			goto done;
	} while (readsize != 1 && readsize > st->blksize);
done:
	dma_free(buf, maxblksize);
	return error;
}
