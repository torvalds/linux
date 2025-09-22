/*	$OpenBSD: cd.c,v 1.266 2022/09/01 13:45:27 krw Exp $	*/
/*	$NetBSD: cd.c,v 1.100 1997/04/02 02:29:30 mycroft Exp $	*/

/*
 * Copyright (c) 1994, 1995, 1997 Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/cdio.h>
#include <sys/conf.h>
#include <sys/scsiio.h>
#include <sys/dkio.h>
#include <sys/vnode.h>

#include <scsi/scsi_all.h>
#include <scsi/cd.h>
#include <scsi/scsi_debug.h>
#include <scsi/scsi_disk.h>	/* rw_10 and start_stop come from there */
#include <scsi/scsiconf.h>


#include <ufs/ffs/fs.h>		/* for BBSIZE and SBSIZE */

#define	CDOUTSTANDING	4

#define MAXTRACK	99
#define CD_FRAMES	75
#define CD_SECS		60

struct cd_toc {
	struct ioc_toc_header header;
	struct cd_toc_entry entries[MAXTRACK+1];	/* One extra for the */
							/* leadout */
};

int	cdmatch(struct device *, void *, void *);
void	cdattach(struct device *, struct device *, void *);
int	cdactivate(struct device *, int);
int	cddetach(struct device *, int);

struct cd_softc {
	struct device		 sc_dev;
	struct disk		 sc_dk;

	int			 sc_flags;
#define	CDF_DYING	0x40		/* dying, when deactivated */
	struct scsi_link	*sc_link;	/* contains targ, lun, etc. */
	struct cd_parms {
		u_int32_t secsize;
		u_int64_t disksize;	/* total number sectors */
	}			 params;
	struct bufq		 sc_bufq;
	struct scsi_xshandler	 sc_xsh;
};

void	cdstart(struct scsi_xfer *);
void	cd_buf_done(struct scsi_xfer *);
int	cd_cmd_rw6(struct scsi_generic *, int, u_int64_t, u_int32_t);
int	cd_cmd_rw10(struct scsi_generic *, int, u_int64_t, u_int32_t);
int	cd_cmd_rw12(struct scsi_generic *, int, u_int64_t, u_int32_t);
void	cdminphys(struct buf *);
int	cdgetdisklabel(dev_t, struct cd_softc *, struct disklabel *, int);
int	cd_setchan(struct cd_softc *, int, int, int, int, int);
int	cd_getvol(struct cd_softc *cd, struct ioc_vol *, int);
int	cd_setvol(struct cd_softc *, const struct ioc_vol *, int);
int	cd_load_unload(struct cd_softc *, int, int);
int	cd_set_pa_immed(struct cd_softc *, int);
int	cd_play(struct cd_softc *, int, int);
int	cd_play_tracks(struct cd_softc *, int, int, int, int);
int	cd_play_msf(struct cd_softc *, int, int, int, int, int, int);
int	cd_pause(struct cd_softc *, int);
int	cd_reset(struct cd_softc *);
int	cd_read_subchannel(struct cd_softc *, int, int, int,
	    struct cd_sub_channel_info *, int );
int	cd_read_toc(struct cd_softc *, int, int, void *, int, int);
int	cd_get_parms(struct cd_softc *, int);
int	cd_load_toc(struct cd_softc *, struct cd_toc *, int);
int	cd_interpret_sense(struct scsi_xfer *);
u_int64_t cd_size(struct scsi_link *, int, u_int32_t *);

int	dvd_auth(struct cd_softc *, union dvd_authinfo *);
int	dvd_read_physical(struct cd_softc *, union dvd_struct *);
int	dvd_read_copyright(struct cd_softc *, union dvd_struct *);
int	dvd_read_disckey(struct cd_softc *, union dvd_struct *);
int	dvd_read_bca(struct cd_softc *, union dvd_struct *);
int	dvd_read_manufact(struct cd_softc *, union dvd_struct *);
int	dvd_read_struct(struct cd_softc *, union dvd_struct *);

#if defined(__macppc__)
int	cd_eject(void);
#endif /* __macppc__ */

const struct cfattach cd_ca = {
	sizeof(struct cd_softc), cdmatch, cdattach,
	cddetach, cdactivate
};

struct cfdriver cd_cd = {
	NULL, "cd", DV_DISK
};

const struct scsi_inquiry_pattern cd_patterns[] = {
	{T_CDROM, T_REMOV,
	 "",         "",                 ""},
	{T_CDROM, T_FIXED,
	 "",         "",                 ""},
	{T_WORM, T_REMOV,
	 "",         "",                 ""},
	{T_WORM, T_FIXED,
	 "",         "",                 ""},
	{T_DIRECT, T_REMOV,
	 "NEC                 CD-ROM DRIVE:260", "", ""},
#if 0
	{T_CDROM, T_REMOV, /* more luns */
	 "PIONEER ", "CD-ROM DRM-600  ", ""},
#endif /* 0 */
};

#define cdlookup(unit) (struct cd_softc *)disk_lookup(&cd_cd, (unit))

int
cdmatch(struct device *parent, void *match, void *aux)
{
	struct scsi_attach_args		*sa = aux;
	struct scsi_inquiry_data	*inq = &sa->sa_sc_link->inqdata;
	int				 priority;

	scsi_inqmatch(inq, cd_patterns, nitems(cd_patterns),
	    sizeof(cd_patterns[0]), &priority);

	return priority;
}

/*
 * The routine called by the low level scsi routine when it discovers
 * A device suitable for this driver
 */
void
cdattach(struct device *parent, struct device *self, void *aux)
{
	struct cd_softc			*sc = (struct cd_softc *)self;
	struct scsi_attach_args		*sa = aux;
	struct scsi_link		*link = sa->sa_sc_link;

	SC_DEBUG(link, SDEV_DB2, ("cdattach:\n"));

	/*
	 * Store information needed to contact our base driver
	 */
	sc->sc_link = link;
	link->interpret_sense = cd_interpret_sense;
	link->device_softc = sc;
	if (link->openings > CDOUTSTANDING)
		link->openings = CDOUTSTANDING;

	/*
	 * Initialize disk structures.
	 */
	sc->sc_dk.dk_name = sc->sc_dev.dv_xname;
	bufq_init(&sc->sc_bufq, BUFQ_DEFAULT);

	printf("\n");

	scsi_xsh_set(&sc->sc_xsh, link, cdstart);

	/* Attach disk. */
	sc->sc_dk.dk_flags = DKF_NOLABELREAD;
	disk_attach(&sc->sc_dev, &sc->sc_dk);
}


int
cdactivate(struct device *self, int act)
{
	struct cd_softc *sc = (struct cd_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		/*
		 * When resuming, hardware may have forgotten we locked it. So
		 * if there are any open partitions, lock the CD.
		 */
		if (sc->sc_dk.dk_openmask != 0)
			scsi_prevent(sc->sc_link, PR_PREVENT,
			    SCSI_IGNORE_ILLEGAL_REQUEST |
			    SCSI_IGNORE_MEDIA_CHANGE |
			    SCSI_SILENT | SCSI_AUTOCONF);
		break;
	case DVACT_DEACTIVATE:
		SET(sc->sc_flags, CDF_DYING);
		scsi_xsh_del(&sc->sc_xsh);
		break;
	}
	return 0;
}

int
cddetach(struct device *self, int flags)
{
	struct cd_softc *sc = (struct cd_softc *)self;

	bufq_drain(&sc->sc_bufq);

	disk_gone(cdopen, self->dv_unit);

	/* Detach disk. */
	bufq_destroy(&sc->sc_bufq);
	disk_detach(&sc->sc_dk);

	return 0;
}

/*
 * Open the device. Make sure the partition info is as up-to-date as can be.
 */
int
cdopen(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct scsi_link	*link;
	struct cd_softc		*sc;
	int			 error = 0, part, rawopen, unit;

	unit = DISKUNIT(dev);
	part = DISKPART(dev);

	rawopen = (part == RAW_PART) && (fmt == S_IFCHR);

	sc = cdlookup(unit);
	if (sc == NULL)
		return ENXIO;
	if (ISSET(sc->sc_flags, CDF_DYING)) {
		device_unref(&sc->sc_dev);
		return ENXIO;
	}

	link = sc->sc_link;
	SC_DEBUG(link, SDEV_DB1,
	    ("cdopen: dev=0x%x (unit %d (of %d), partition %d)\n", dev, unit,
	    cd_cd.cd_ndevs, part));

	if ((error = disk_lock(&sc->sc_dk)) != 0) {
		device_unref(&sc->sc_dev);
		return error;
	}

	if (sc->sc_dk.dk_openmask != 0) {
		/*
		 * If any partition is open, but the disk has been invalidated,
		 * disallow further opens.
		 */
		if (!ISSET(link->flags, SDEV_MEDIA_LOADED)) {
			if (rawopen)
				goto out;
			error = EIO;
			goto bad;
		}
	} else {
		/*
		 * Check that it is still responding and ok.  Drive can be in
		 * progress of loading media so use increased retries number
		 * and don't ignore NOT_READY.
		 */

		/* Use cd_interpret_sense() now. */
		SET(link->flags, SDEV_OPEN);

		error = scsi_test_unit_ready(link, TEST_READY_RETRIES,
		    (rawopen ? SCSI_SILENT : 0) | SCSI_IGNORE_ILLEGAL_REQUEST |
		    SCSI_IGNORE_MEDIA_CHANGE);

		/* Start the cd spinning if necessary. */
		if (error == EIO)
			error = scsi_start(link, SSS_START,
			    SCSI_IGNORE_ILLEGAL_REQUEST |
			    SCSI_IGNORE_MEDIA_CHANGE | SCSI_SILENT);

		if (error) {
			if (rawopen) {
				error = 0;
				goto out;
			} else
				goto bad;
		}

		/* Lock the cd in. */
		error = scsi_prevent(link, PR_PREVENT,
		    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_MEDIA_CHANGE |
		    SCSI_SILENT);
		if (error)
			goto bad;

		/* Load the physical device parameters. */
		SET(link->flags, SDEV_MEDIA_LOADED);
		if (cd_get_parms(sc, (rawopen ? SCSI_SILENT : 0) |
		    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_MEDIA_CHANGE)) {
			CLR(link->flags, SDEV_MEDIA_LOADED);
			error = ENXIO;
			goto bad;
		}
		SC_DEBUG(link, SDEV_DB3, ("Params loaded\n"));

		/* Fabricate a disk label. */
		cdgetdisklabel(dev, sc, sc->sc_dk.dk_label, 0);
		SC_DEBUG(link, SDEV_DB3, ("Disklabel fabricated\n"));
	}

out:
	if ((error = disk_openpart(&sc->sc_dk, part, fmt, 1)) != 0)
		goto bad;

	SET(link->flags, SDEV_OPEN);
	SC_DEBUG(link, SDEV_DB3, ("open complete\n"));

	/* It's OK to fall through because dk_openmask is now non-zero. */
bad:
	if (sc->sc_dk.dk_openmask == 0) {
		scsi_prevent(link, PR_ALLOW,
		    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_MEDIA_CHANGE |
		    SCSI_SILENT);
		CLR(link->flags, SDEV_OPEN | SDEV_MEDIA_LOADED);
	}

	disk_unlock(&sc->sc_dk);
	device_unref(&sc->sc_dev);
	return error;
}

/*
 * Close the device. Only called if we are the last occurrence of an open
 * device.
 */
int
cdclose(dev_t dev, int flag, int fmt, struct proc *p)
{
	struct cd_softc		*sc;
	int			 part = DISKPART(dev);

	sc = cdlookup(DISKUNIT(dev));
	if (sc == NULL)
		return ENXIO;
	if (ISSET(sc->sc_flags, CDF_DYING)) {
		device_unref(&sc->sc_dev);
		return ENXIO;
	}

	disk_lock_nointr(&sc->sc_dk);

	disk_closepart(&sc->sc_dk, part, fmt);

	if (sc->sc_dk.dk_openmask == 0) {
		/* XXXX Must wait for I/O to complete! */

		scsi_prevent(sc->sc_link, PR_ALLOW,
		    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_NOT_READY |
		    SCSI_SILENT);
		CLR(sc->sc_link->flags, SDEV_OPEN | SDEV_MEDIA_LOADED);

		if (ISSET(sc->sc_link->flags, SDEV_EJECTING)) {
			scsi_start(sc->sc_link, SSS_STOP|SSS_LOEJ, 0);

			CLR(sc->sc_link->flags, SDEV_EJECTING);
		}

		scsi_xsh_del(&sc->sc_xsh);
	}

	disk_unlock(&sc->sc_dk);

	device_unref(&sc->sc_dev);
	return 0;
}

/*
 * Actually translate the requested transfer into one the physical driver can
 * understand.  The transfer is described by a buf and will include only one
 * physical transfer.
 */
void
cdstrategy(struct buf *bp)
{
	struct cd_softc		*sc;
	int			 s;

	sc = cdlookup(DISKUNIT(bp->b_dev));
	if (sc == NULL) {
		bp->b_error = ENXIO;
		goto bad;
	}
	if (ISSET(sc->sc_flags, CDF_DYING)) {
		bp->b_error = ENXIO;
		goto bad;
	}

	SC_DEBUG(sc->sc_link, SDEV_DB2, ("cdstrategy: %ld bytes @ blk %lld\n",
	    bp->b_bcount, (long long)bp->b_blkno));
	/*
	 * If the device has been made invalid, error out
	 * maybe the media changed, or no media loaded
	 */
	if (!ISSET(sc->sc_link->flags, SDEV_MEDIA_LOADED)) {
		bp->b_error = EIO;
		goto bad;
	}

	/* Validate the request. */
	if (bounds_check_with_label(bp, sc->sc_dk.dk_label) == -1)
		goto done;

	/* Place it in the queue of disk activities for this disk. */
	bufq_queue(&sc->sc_bufq, bp);

	/*
	 * Tell the device to get going on the transfer if it's
	 * not doing anything, otherwise just wait for completion
	 */
	scsi_xsh_add(&sc->sc_xsh);

	device_unref(&sc->sc_dev);
	return;

bad:
	SET(bp->b_flags, B_ERROR);
	bp->b_resid = bp->b_bcount;
done:
	s = splbio();
	biodone(bp);
	splx(s);
	if (sc != NULL)
		device_unref(&sc->sc_dev);
}

int
cd_cmd_rw6(struct scsi_generic *generic, int read, u_int64_t secno,
    u_int32_t nsecs)
{
	struct scsi_rw *cmd = (struct scsi_rw *)generic;

	cmd->opcode = read ? READ_COMMAND : WRITE_COMMAND;
	_lto3b(secno, cmd->addr);
	cmd->length = nsecs & 0xff;

	return sizeof(*cmd);
}

int
cd_cmd_rw10(struct scsi_generic *generic, int read, u_int64_t secno,
    u_int32_t nsecs)
{
	struct scsi_rw_10 *cmd = (struct scsi_rw_10 *)generic;

	cmd->opcode = read ? READ_10 : WRITE_10;
	_lto4b(secno, cmd->addr);
	_lto2b(nsecs, cmd->length);

	return sizeof(*cmd);
}

int
cd_cmd_rw12(struct scsi_generic *generic, int read, u_int64_t secno,
    u_int32_t nsecs)
{
	struct scsi_rw_12 *cmd = (struct scsi_rw_12 *)generic;

	cmd->opcode = read ? READ_12 : WRITE_12;
	_lto4b(secno, cmd->addr);
	_lto4b(nsecs, cmd->length);

	return sizeof(*cmd);
}

/*
 * cdstart looks to see if there is a buf waiting for the device
 * and that the device is not already busy. If both are true,
 * It dequeues the buf and creates a scsi command to perform the
 * transfer in the buf. The transfer request will call scsi_done
 * on completion, which will in turn call this routine again
 * so that the next queued transfer is performed.
 * The bufs are queued by the strategy routine (cdstrategy)
 *
 * This routine is also called after other non-queued requests
 * have been made of the scsi driver, to ensure that the queue
 * continues to be drained.
 *
 * must be called at the correct (highish) spl level
 * cdstart() is called at splbio from cdstrategy and scsi_done
 */
void
cdstart(struct scsi_xfer *xs)
{
	struct scsi_link	*link = xs->sc_link;
	struct cd_softc		*sc = link->device_softc;
	struct buf		*bp;
	struct partition	*p;
	u_int64_t		 secno;
	u_int32_t		 nsecs;
	int			 read;

	SC_DEBUG(link, SDEV_DB2, ("cdstart\n"));

	if (ISSET(sc->sc_flags, CDF_DYING)) {
		scsi_xs_put(xs);
		return;
	}

	if (!ISSET(link->flags, SDEV_MEDIA_LOADED)) {
		bufq_drain(&sc->sc_bufq);
		scsi_xs_put(xs);
		return;
	}

	bp = bufq_dequeue(&sc->sc_bufq);
	if (bp == NULL) {
		scsi_xs_put(xs);
		return;
	}
	read = ISSET(bp->b_flags, B_READ);

	SET(xs->flags, (read ? SCSI_DATA_IN : SCSI_DATA_OUT));
	xs->timeout = 30000;
	xs->data = bp->b_data;
	xs->datalen = bp->b_bcount;
	xs->done = cd_buf_done;
	xs->cookie = bp;
	xs->bp = bp;

	p = &sc->sc_dk.dk_label->d_partitions[DISKPART(bp->b_dev)];
	secno = DL_GETPOFFSET(p) + DL_BLKTOSEC(sc->sc_dk.dk_label, bp->b_blkno);
	nsecs = howmany(bp->b_bcount, sc->sc_dk.dk_label->d_secsize);

	if (!ISSET(link->flags, SDEV_ATAPI | SDEV_UMASS) &&
	    (SID_ANSII_REV(&link->inqdata) < SCSI_REV_2) &&
	    ((secno & 0x1fffff) == secno) &&
	    ((nsecs & 0xff) == nsecs))
		xs->cmdlen = cd_cmd_rw6(&xs->cmd, read, secno, nsecs);
	else if (((secno & 0xffffffff) == secno) &&
	    ((nsecs & 0xffff) == nsecs))
		xs->cmdlen = cd_cmd_rw10(&xs->cmd, read, secno, nsecs);
	else
		xs->cmdlen = cd_cmd_rw12(&xs->cmd, read, secno, nsecs);

	disk_busy(&sc->sc_dk);
	scsi_xs_exec(xs);

	/* Move onto the next io. */
	if (bufq_peek(&sc->sc_bufq))
		scsi_xsh_add(&sc->sc_xsh);
}

void
cd_buf_done(struct scsi_xfer *xs)
{
	struct cd_softc		*sc = xs->sc_link->device_softc;
	struct buf		*bp = xs->cookie;
	int			 error, s;

	switch (xs->error) {
	case XS_NOERROR:
		bp->b_error = 0;
		CLR(bp->b_flags, B_ERROR);
		bp->b_resid = xs->resid;
		break;

	case XS_SENSE:
	case XS_SHORTSENSE:
		SC_DEBUG_SENSE(xs);
		error = cd_interpret_sense(xs);
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

	disk_unbusy(&sc->sc_dk, bp->b_bcount - xs->resid, bp->b_blkno,
	    bp->b_flags & B_READ);

	s = splbio();
	biodone(bp);
	splx(s);
	scsi_xs_put(xs);
}

void
cdminphys(struct buf *bp)
{
	struct scsi_link	*link;
	struct cd_softc		*sc;
	long			 max;

	sc = cdlookup(DISKUNIT(bp->b_dev));
	if (sc == NULL)
		return;
	link = sc->sc_link;

	/*
	 * If the device is ancient, we want to make sure that
	 * the transfer fits into a 6-byte cdb.
	 *
	 * XXX Note that the SCSI-I spec says that 256-block transfers
	 * are allowed in a 6-byte read/write, and are specified
	 * by setting the "length" to 0.  However, we're conservative
	 * here, allowing only 255-block transfers in case an
	 * ancient device gets confused by length == 0.  A length of 0
	 * in a 10-byte read/write actually means 0 blocks.
	 */
	if (!ISSET(link->flags, SDEV_ATAPI | SDEV_UMASS) &&
	    SID_ANSII_REV(&link->inqdata) < SCSI_REV_2) {
		max = sc->sc_dk.dk_label->d_secsize * 0xff;

		if (bp->b_bcount > max)
			bp->b_bcount = max;
	}

	if (link->bus->sb_adapter->dev_minphys != NULL)
		(*link->bus->sb_adapter->dev_minphys)(bp, link);
	else
		minphys(bp);

	device_unref(&sc->sc_dev);
}

int
cdread(dev_t dev, struct uio *uio, int ioflag)
{
	return physio(cdstrategy, dev, B_READ, cdminphys, uio);
}

int
cdwrite(dev_t dev, struct uio *uio, int ioflag)
{
	return physio(cdstrategy, dev, B_WRITE, cdminphys, uio);
}

/*
 * Perform special action on behalf of the user.
 * Knows about the internals of this device
 */
int
cdioctl(dev_t dev, u_long cmd, caddr_t addr, int flag, struct proc *p)
{
	struct cd_softc		*sc;
	struct disklabel	*lp;
	int			 part = DISKPART(dev);
	int			 error = 0;

	sc = cdlookup(DISKUNIT(dev));
	if (sc == NULL)
		return ENXIO;
	if (ISSET(sc->sc_flags, CDF_DYING)) {
		device_unref(&sc->sc_dev);
		return ENXIO;
	}

	SC_DEBUG(sc->sc_link, SDEV_DB2, ("cdioctl 0x%lx\n", cmd));

	/*
	 * If the device is not valid.. abandon ship
	 */
	if (!ISSET(sc->sc_link->flags, SDEV_MEDIA_LOADED)) {
		switch (cmd) {
		case DIOCLOCK:
		case DIOCEJECT:
		case SCIOCIDENTIFY:
		case SCIOCCOMMAND:
		case SCIOCDEBUG:
		case CDIOCLOADUNLOAD:
		case SCIOCRESET:
		case CDIOCGETVOL:
		case CDIOCSETVOL:
		case CDIOCSETMONO:
		case CDIOCSETSTEREO:
		case CDIOCSETMUTE:
		case CDIOCSETLEFT:
		case CDIOCSETRIGHT:
		case CDIOCCLOSE:
		case CDIOCEJECT:
		case CDIOCALLOW:
		case CDIOCPREVENT:
		case CDIOCSETDEBUG:
		case CDIOCCLRDEBUG:
		case CDIOCRESET:
		case DVD_AUTH:
		case DVD_READ_STRUCT:
		case MTIOCTOP:
			if (part == RAW_PART)
				break;
		/* FALLTHROUGH */
		default:
			if (!ISSET(sc->sc_link->flags, SDEV_OPEN))
				error = ENODEV;
			else
				error = EIO;
			goto exit;
		}
	}

	switch (cmd) {
	case DIOCRLDINFO:
		lp = malloc(sizeof(*lp), M_TEMP, M_WAITOK);
		cdgetdisklabel(dev, sc, lp, 0);
		memcpy(sc->sc_dk.dk_label, lp, sizeof(*lp));
		free(lp, M_TEMP, sizeof(*lp));
		break;

	case DIOCGPDINFO:
		cdgetdisklabel(dev, sc, (struct disklabel *)addr, 1);
		break;

	case DIOCGDINFO:
		*(struct disklabel *)addr = *(sc->sc_dk.dk_label);
		break;

	case DIOCGPART:
		((struct partinfo *)addr)->disklab = sc->sc_dk.dk_label;
		((struct partinfo *)addr)->part =
		    &sc->sc_dk.dk_label->d_partitions[DISKPART(dev)];
		break;

	case DIOCWDINFO:
	case DIOCSDINFO:
		if (!ISSET(flag, FWRITE)) {
			error = EBADF;
			break;
		}

		if ((error = disk_lock(&sc->sc_dk)) != 0)
			break;

		error = setdisklabel(sc->sc_dk.dk_label,
		    (struct disklabel *)addr, sc->sc_dk.dk_openmask);
		if (error == 0) {
		}

		disk_unlock(&sc->sc_dk);
		break;

	case CDIOCPLAYTRACKS: {
		struct ioc_play_track *args = (struct ioc_play_track *)addr;

		if ((error = cd_set_pa_immed(sc, 0)) != 0)
			break;
		error = cd_play_tracks(sc, args->start_track,
		    args->start_index, args->end_track, args->end_index);
		break;
	}
	case CDIOCPLAYMSF: {
		struct ioc_play_msf *args = (struct ioc_play_msf *)addr;

		if ((error = cd_set_pa_immed(sc, 0)) != 0)
			break;
		error = cd_play_msf(sc, args->start_m, args->start_s,
		    args->start_f, args->end_m, args->end_s, args->end_f);
		break;
	}
	case CDIOCPLAYBLOCKS: {
		struct ioc_play_blocks *args = (struct ioc_play_blocks *)addr;

		if ((error = cd_set_pa_immed(sc, 0)) != 0)
			break;
		error = cd_play(sc, args->blk, args->len);
		break;
	}
	case CDIOCREADSUBCHANNEL: {
		struct ioc_read_subchannel *args =
		    (struct ioc_read_subchannel *)addr;
		struct cd_sub_channel_info *data;
		int len = args->data_len;

		if (len > sizeof(*data) ||
		    len < sizeof(struct cd_sub_channel_header)) {
			error = EINVAL;
			break;
		}
		data = dma_alloc(sizeof(*data), PR_WAITOK);
		error = cd_read_subchannel(sc, args->address_format,
		    args->data_format, args->track, data, len);
		if (error) {
			dma_free(data, sizeof(*data));
			break;
		}
		len = min(len, _2btol(data->header.data_len) +
		    sizeof(struct cd_sub_channel_header));
		error = copyout(data, args->data, len);
		dma_free(data, sizeof(*data));
		break;
	}
	case CDIOREADTOCHEADER: {
		struct ioc_toc_header *th;

		th = dma_alloc(sizeof(*th), PR_WAITOK);
		if ((error = cd_read_toc(sc, 0, 0, th, sizeof(*th), 0)) != 0) {
			dma_free(th, sizeof(*th));
			break;
		}
		if (ISSET(sc->sc_link->quirks, ADEV_LITTLETOC))
			th->len = letoh16(th->len);
		else
			th->len = betoh16(th->len);
		if (th->len > 0)
			memcpy(addr, th, sizeof(*th));
		else
			error = EIO;
		dma_free(th, sizeof(*th));
		break;
	}
	case CDIOREADTOCENTRYS: {
		struct cd_toc *toc;
		struct ioc_read_toc_entry *te =
		    (struct ioc_read_toc_entry *)addr;
		struct ioc_toc_header *th;
		struct cd_toc_entry *cte;
		int len = te->data_len;
		int ntracks;

		toc = dma_alloc(sizeof(*toc), PR_WAITOK | PR_ZERO);

		th = &toc->header;

		if (len > sizeof(toc->entries) ||
		    len < sizeof(struct cd_toc_entry)) {
			dma_free(toc, sizeof(*toc));
			error = EINVAL;
			break;
		}
		error = cd_read_toc(sc, te->address_format, te->starting_track,
		    toc, len + sizeof(struct ioc_toc_header), 0);
		if (error) {
			dma_free(toc, sizeof(*toc));
			break;
		}
		if (te->address_format == CD_LBA_FORMAT)
			for (ntracks =
			    th->ending_track - th->starting_track + 1;
			    ntracks >= 0; ntracks--) {
				cte = &toc->entries[ntracks];
				cte->addr_type = CD_LBA_FORMAT;
				if (ISSET(sc->sc_link->quirks,
				    ADEV_LITTLETOC)) {
#if BYTE_ORDER == BIG_ENDIAN
					swap16_multi((u_int16_t *)&cte->addr,
					    sizeof(cte->addr) / 2);
#endif /* BYTE_ORDER == BIG_ENDIAN */
				} else
					cte->addr.lba = betoh32(cte->addr.lba);
			}
		if (ISSET(sc->sc_link->quirks, ADEV_LITTLETOC)) {
			th->len = letoh16(th->len);
		} else
			th->len = betoh16(th->len);
		len = min(len, th->len - (sizeof(th->starting_track) +
		    sizeof(th->ending_track)));

		error = copyout(toc->entries, te->data, len);
		dma_free(toc, sizeof(*toc));
		break;
	}
	case CDIOREADMSADDR: {
		struct cd_toc *toc;
		int sessno = *(int *)addr;
		struct cd_toc_entry *cte;

		if (sessno != 0) {
			error = EINVAL;
			break;
		}

		toc = dma_alloc(sizeof(*toc), PR_WAITOK | PR_ZERO);

		error = cd_read_toc(sc, 0, 0, toc,
		    sizeof(struct ioc_toc_header) + sizeof(struct cd_toc_entry),
		    0x40 /* control word for "get MS info" */);

		if (error) {
			dma_free(toc, sizeof(*toc));
			break;
		}

		cte = &toc->entries[0];
		if (ISSET(sc->sc_link->quirks, ADEV_LITTLETOC)) {
#if BYTE_ORDER == BIG_ENDIAN
			swap16_multi((u_int16_t *)&cte->addr,
			    sizeof(cte->addr) / 2);
#endif /* BYTE_ORDER == BIG_ENDIAN */
		} else
			cte->addr.lba = betoh32(cte->addr.lba);
		if (ISSET(sc->sc_link->quirks, ADEV_LITTLETOC))
			toc->header.len = letoh16(toc->header.len);
		else
			toc->header.len = betoh16(toc->header.len);

		*(int *)addr = (toc->header.len >= 10 && cte->track > 1) ?
			cte->addr.lba : 0;
		dma_free(toc, sizeof(*toc));
		break;
	}
	case CDIOCSETPATCH: {
		struct ioc_patch *arg = (struct ioc_patch *)addr;

		error = cd_setchan(sc, arg->patch[0], arg->patch[1],
		    arg->patch[2], arg->patch[3], 0);
		break;
	}
	case CDIOCGETVOL: {
		struct ioc_vol *arg = (struct ioc_vol *)addr;

		error = cd_getvol(sc, arg, 0);
		break;
	}
	case CDIOCSETVOL: {
		struct ioc_vol *arg = (struct ioc_vol *)addr;

		error = cd_setvol(sc, arg, 0);
		break;
	}

	case CDIOCSETMONO:
		error = cd_setchan(sc, BOTH_CHANNEL, BOTH_CHANNEL, MUTE_CHANNEL,
		    MUTE_CHANNEL, 0);
		break;

	case CDIOCSETSTEREO:
		error = cd_setchan(sc, LEFT_CHANNEL, RIGHT_CHANNEL,
		    MUTE_CHANNEL, MUTE_CHANNEL, 0);
		break;

	case CDIOCSETMUTE:
		error = cd_setchan(sc, MUTE_CHANNEL, MUTE_CHANNEL, MUTE_CHANNEL,
		    MUTE_CHANNEL, 0);
		break;

	case CDIOCSETLEFT:
		error = cd_setchan(sc, LEFT_CHANNEL, LEFT_CHANNEL, MUTE_CHANNEL,
		    MUTE_CHANNEL, 0);
		break;

	case CDIOCSETRIGHT:
		error = cd_setchan(sc, RIGHT_CHANNEL, RIGHT_CHANNEL,
		    MUTE_CHANNEL, MUTE_CHANNEL, 0);
		break;

	case CDIOCRESUME:
		error = cd_pause(sc, 1);
		break;

	case CDIOCPAUSE:
		error = cd_pause(sc, 0);
		break;
	case CDIOCSTART:
		error = scsi_start(sc->sc_link, SSS_START, 0);
		break;

	case CDIOCSTOP:
		error = scsi_start(sc->sc_link, SSS_STOP, 0);
		break;

close_tray:
	case CDIOCCLOSE:
		error = scsi_start(sc->sc_link, SSS_START|SSS_LOEJ,
		    SCSI_IGNORE_NOT_READY | SCSI_IGNORE_MEDIA_CHANGE);
		break;

	case MTIOCTOP:
		if (((struct mtop *)addr)->mt_op == MTRETEN)
			goto close_tray;
		if (((struct mtop *)addr)->mt_op != MTOFFL) {
			error = EIO;
			break;
		}
		/* FALLTHROUGH */
	case CDIOCEJECT: /* FALLTHROUGH */
	case DIOCEJECT:
		SET(sc->sc_link->flags, SDEV_EJECTING);
		break;
	case CDIOCALLOW:
		error = scsi_prevent(sc->sc_link, PR_ALLOW, 0);
		break;
	case CDIOCPREVENT:
		error = scsi_prevent(sc->sc_link, PR_PREVENT, 0);
		break;
	case DIOCLOCK:
		error = scsi_prevent(sc->sc_link,
		    (*(int *)addr) ? PR_PREVENT : PR_ALLOW, 0);
		break;
	case CDIOCSETDEBUG:
		SET(sc->sc_link->flags, SDEV_DB1 | SDEV_DB2);
		break;
	case CDIOCCLRDEBUG:
		CLR(sc->sc_link->flags, SDEV_DB1 | SDEV_DB2);
		break;
	case CDIOCRESET:
	case SCIOCRESET:
		error = cd_reset(sc);
		break;
	case CDIOCLOADUNLOAD: {
		struct ioc_load_unload *args = (struct ioc_load_unload *)addr;

		error = cd_load_unload(sc, args->options, args->slot);
		break;
	}

	case DVD_AUTH:
		error = dvd_auth(sc, (union dvd_authinfo *)addr);
		break;
	case DVD_READ_STRUCT:
		error = dvd_read_struct(sc, (union dvd_struct *)addr);
		break;
	default:
		if (DISKPART(dev) != RAW_PART) {
			error = ENOTTY;
			break;
		}
		error = scsi_do_ioctl(sc->sc_link, cmd, addr, flag);
		break;
	}

exit:

	device_unref(&sc->sc_dev);
	return error;
}

/*
 * Load the label information on the named device
 * Actually fabricate a disklabel
 *
 * EVENTUALLY take information about different
 * data tracks from the TOC and put it in the disklabel
 */
int
cdgetdisklabel(dev_t dev, struct cd_softc *sc, struct disklabel *lp,
    int spoofonly)
{
	struct cd_toc		*toc;
	int			 tocidx, n, audioonly = 1;

	bzero(lp, sizeof(struct disklabel));

	lp->d_secsize = sc->params.secsize;
	lp->d_ntracks = 1;
	lp->d_nsectors = 100;
	lp->d_secpercyl = 100;
	lp->d_ncylinders = (sc->params.disksize / 100) + 1;

	if (ISSET(sc->sc_link->flags, SDEV_ATAPI)) {
		strncpy(lp->d_typename, "ATAPI CD-ROM", sizeof(lp->d_typename));
		lp->d_type = DTYPE_ATAPI;
	} else {
		strncpy(lp->d_typename, "SCSI CD-ROM", sizeof(lp->d_typename));
		lp->d_type = DTYPE_SCSI;
	}

	strncpy(lp->d_packname, "fictitious", sizeof(lp->d_packname));
	DL_SETDSIZE(lp, sc->params.disksize);
	lp->d_version = 1;

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = dkcksum(lp);

	toc = dma_alloc(sizeof(*toc), PR_WAITOK | PR_ZERO);
	if (cd_load_toc(sc, toc, CD_LBA_FORMAT)) {
		audioonly = 0; /* No valid TOC found == not an audio CD. */
		goto done;
	}

	n = toc->header.ending_track - toc->header.starting_track + 1;
	for (tocidx = 0; tocidx < n; tocidx++)
		if (toc->entries[tocidx].control & 4) {
			audioonly = 0; /* Found a non-audio track. */
			goto done;
		}

done:
	dma_free(toc, sizeof(*toc));

	if (audioonly)
		return 0;
	return readdisklabel(DISKLABELDEV(dev), cdstrategy, lp, spoofonly);
}

int
cd_setchan(struct cd_softc *sc, int p0, int p1, int p2, int p3, int flags)
{
	union scsi_mode_sense_buf	*data;
	struct cd_audio_page		*audio = NULL;
	int				 error, big;

	data = dma_alloc(sizeof(*data), PR_NOWAIT);
	if (data == NULL)
		return ENOMEM;

	error = scsi_do_mode_sense(sc->sc_link, AUDIO_PAGE, data,
	    (void **)&audio, sizeof(*audio), flags, &big);
	if (error == 0 && audio == NULL)
		error = EIO;

	if (error == 0) {
		audio->port[LEFT_PORT].channels = p0;
		audio->port[RIGHT_PORT].channels = p1;
		audio->port[2].channels = p2;
		audio->port[3].channels = p3;
		if (big)
			error = scsi_mode_select_big(sc->sc_link, SMS_PF,
			    &data->hdr_big, flags, 20000);
		else
			error = scsi_mode_select(sc->sc_link, SMS_PF,
			    &data->hdr, flags, 20000);
	}

	dma_free(data, sizeof(*data));
	return error;
}

int
cd_getvol(struct cd_softc *sc, struct ioc_vol *arg, int flags)
{
	union scsi_mode_sense_buf	*data;
	struct cd_audio_page		*audio = NULL;
	int				 big, error;

	data = dma_alloc(sizeof(*data), PR_NOWAIT);
	if (data == NULL)
		return ENOMEM;

	error = scsi_do_mode_sense(sc->sc_link, AUDIO_PAGE, data,
	    (void **)&audio, sizeof(*audio), flags, &big);
	if (error == 0 && audio == NULL)
		error = EIO;

	if (error == 0) {
		arg->vol[0] = audio->port[0].volume;
		arg->vol[1] = audio->port[1].volume;
		arg->vol[2] = audio->port[2].volume;
		arg->vol[3] = audio->port[3].volume;
	}

	dma_free(data, sizeof(*data));
	return 0;
}

int
cd_setvol(struct cd_softc *sc, const struct ioc_vol *arg, int flags)
{
	union scsi_mode_sense_buf	*data;
	struct cd_audio_page		*audio = NULL;
	u_int8_t			 mask_volume[4];
	int				 error, big;

	data = dma_alloc(sizeof(*data), PR_NOWAIT);
	if (data == NULL)
		return ENOMEM;

	error = scsi_do_mode_sense(sc->sc_link,
	    AUDIO_PAGE | SMS_PAGE_CTRL_CHANGEABLE, data, (void **)&audio,
	    sizeof(*audio), flags, &big);
	if (error == 0 && audio == NULL)
		error = EIO;
	if (error != 0) {
		dma_free(data, sizeof(*data));
		return error;
	}

	mask_volume[0] = audio->port[0].volume;
	mask_volume[1] = audio->port[1].volume;
	mask_volume[2] = audio->port[2].volume;
	mask_volume[3] = audio->port[3].volume;

	error = scsi_do_mode_sense(sc->sc_link, AUDIO_PAGE, data,
	    (void **)&audio, sizeof(*audio), flags, &big);
	if (error == 0 && audio == NULL)
		error = EIO;
	if (error != 0) {
		dma_free(data, sizeof(*data));
		return error;
	}

	audio->port[0].volume = arg->vol[0] & mask_volume[0];
	audio->port[1].volume = arg->vol[1] & mask_volume[1];
	audio->port[2].volume = arg->vol[2] & mask_volume[2];
	audio->port[3].volume = arg->vol[3] & mask_volume[3];

	if (big)
		error = scsi_mode_select_big(sc->sc_link, SMS_PF,
		    &data->hdr_big, flags, 20000);
	else
		error = scsi_mode_select(sc->sc_link, SMS_PF,
		    &data->hdr, flags, 20000);

	dma_free(data, sizeof(*data));
	return error;
}

int
cd_load_unload(struct cd_softc *sc, int options, int slot)
{
	struct scsi_load_unload		*cmd;
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = 200000;

	cmd = (struct scsi_load_unload *)&xs->cmd;
	cmd->opcode = LOAD_UNLOAD;
	cmd->options = options;    /* ioctl uses ATAPI values */
	cmd->slot = slot;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

int
cd_set_pa_immed(struct cd_softc *sc, int flags)
{
	union scsi_mode_sense_buf	*data;
	struct cd_audio_page		*audio = NULL;
	int				 error, oflags, big;

	if (ISSET(sc->sc_link->flags, SDEV_ATAPI))
		/* XXX Noop? */
		return 0;

	data = dma_alloc(sizeof(*data), PR_NOWAIT);
	if (data == NULL)
		return ENOMEM;

	error = scsi_do_mode_sense(sc->sc_link, AUDIO_PAGE, data,
	    (void **)&audio, sizeof(*audio), flags, &big);
	if (error == 0 && audio == NULL)
		error = EIO;

	if (error == 0) {
		oflags = audio->flags;
		CLR(audio->flags, CD_PA_SOTC);
		SET(audio->flags, CD_PA_IMMED);
		if (audio->flags != oflags) {
			if (big)
				error = scsi_mode_select_big(sc->sc_link,
				    SMS_PF, &data->hdr_big, flags, 20000);
			else
				error = scsi_mode_select(sc->sc_link, SMS_PF,
				    &data->hdr, flags, 20000);
		}
	}

	dma_free(data, sizeof(*data));
	return error;
}

/*
 * Get scsi driver to send a "start playing" command
 */
int
cd_play(struct cd_softc *sc, int secno, int nsecs)
{
	struct scsi_play	*cmd;
	struct scsi_xfer	*xs;
	int			 error;

	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = 200000;

	cmd = (struct scsi_play *)&xs->cmd;
	cmd->opcode = PLAY;
	_lto4b(secno, cmd->blk_addr);
	_lto2b(nsecs, cmd->xfer_len);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Get scsi driver to send a "start playing" command
 */
int
cd_play_tracks(struct cd_softc *sc, int strack, int sindex, int etrack,
    int eindex)
{
	struct cd_toc		*toc;
	int			 error;
	u_char			 endf, ends, endm;

	if (!etrack)
		return EIO;
	if (strack > etrack)
		return EINVAL;

	toc = dma_alloc(sizeof(*toc), PR_WAITOK | PR_ZERO);

	if ((error = cd_load_toc(sc, toc, CD_MSF_FORMAT)) != 0)
		goto done;

	if (++etrack > (toc->header.ending_track+1))
		etrack = toc->header.ending_track+1;

	strack -= toc->header.starting_track;
	etrack -= toc->header.starting_track;
	if (strack < 0) {
		error = EINVAL;
		goto done;
	}

	/*
	 * The track ends one frame before the next begins.  The last track
	 * is taken care of by the leadoff track.
	 */
	endm = toc->entries[etrack].addr.msf.minute;
	ends = toc->entries[etrack].addr.msf.second;
	endf = toc->entries[etrack].addr.msf.frame;
	if (endf-- == 0) {
		endf = CD_FRAMES - 1;
		if (ends-- == 0) {
			ends = CD_SECS - 1;
			if (endm-- == 0) {
				error = EINVAL;
				goto done;
			}
		}
	}

	error = cd_play_msf(sc, toc->entries[strack].addr.msf.minute,
	    toc->entries[strack].addr.msf.second,
	    toc->entries[strack].addr.msf.frame,
	    endm, ends, endf);

done:
	dma_free(toc, sizeof(*toc));
	return error;
}

/*
 * Get scsi driver to send a "play msf" command
 */
int
cd_play_msf(struct cd_softc *sc, int startm, int starts, int startf, int endm,
    int ends, int endf)
{
	struct scsi_play_msf		*cmd;
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = 20000;

	cmd = (struct scsi_play_msf *)&xs->cmd;
	cmd->opcode = PLAY_MSF;
	cmd->start_m = startm;
	cmd->start_s = starts;
	cmd->start_f = startf;
	cmd->end_m = endm;
	cmd->end_s = ends;
	cmd->end_f = endf;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Get scsi driver to send a "start up" command
 */
int
cd_pause(struct cd_softc *sc, int go)
{
	struct scsi_pause		*cmd;
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = 2000;

	cmd = (struct scsi_pause *)&xs->cmd;
	cmd->opcode = PAUSE;
	cmd->resume = go;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Get scsi driver to send a "RESET" command
 */
int
cd_reset(struct cd_softc *sc)
{
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(sc->sc_link, SCSI_RESET);
	if (xs == NULL)
		return ENOMEM;

	xs->timeout = 2000;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Read subchannel
 */
int
cd_read_subchannel(struct cd_softc *sc, int mode, int format, int track,
    struct cd_sub_channel_info *data, int len)
{
	struct scsi_read_subchannel	*cmd;
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN | SCSI_SILENT);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)data;
	xs->datalen = len;
	xs->timeout = 5000;

	cmd = (struct scsi_read_subchannel *)&xs->cmd;
	cmd->opcode = READ_SUBCHANNEL;
	if (mode == CD_MSF_FORMAT)
		SET(cmd->byte2, CD_MSF);
	cmd->byte3 = SRS_SUBQ;
	cmd->subchan_format = format;
	cmd->track = track;
	_lto2b(len, cmd->data_len);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

/*
 * Read table of contents
 */
int
cd_read_toc(struct cd_softc *sc, int mode, int start, void *data, int len,
    int control)
{
	struct scsi_read_toc		*cmd;
	struct scsi_xfer		*xs;
	int				 error;

	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN |
	    SCSI_IGNORE_ILLEGAL_REQUEST);
	if (xs == NULL)
		return ENOMEM;
	xs->cmdlen = sizeof(*cmd);
	xs->data = data;
	xs->datalen = len;
	xs->timeout = 5000;

	bzero(data, len);

	cmd = (struct scsi_read_toc *)&xs->cmd;
	cmd->opcode = READ_TOC;

	if (mode == CD_MSF_FORMAT)
		SET(cmd->byte2, CD_MSF);
	cmd->from_track = start;
	_lto2b(len, cmd->data_len);
	cmd->control = control;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	return error;
}

int
cd_load_toc(struct cd_softc *sc, struct cd_toc *toc, int fmt)
{
	int	n, len, error;

	error = cd_read_toc(sc, 0, 0, toc, sizeof(toc->header), 0);

	if (error == 0) {
		if (toc->header.ending_track < toc->header.starting_track)
			return EIO;
		/* +2 to account for leading out track. */
		n = toc->header.ending_track - toc->header.starting_track + 2;
		len = n * sizeof(struct cd_toc_entry) + sizeof(toc->header);
		error = cd_read_toc(sc, fmt, 0, toc, len, 0);
	}

	return error;
}


/*
 * Get the scsi driver to send a full inquiry to the device and use the
 * results to fill out the disk parameter structure.
 */
int
cd_get_parms(struct cd_softc *sc, int flags)
{
	/* Reasonable defaults for drives that don't support READ_CAPACITY */
	sc->params.secsize = 2048;
	sc->params.disksize = 400000;

	if (ISSET(sc->sc_link->quirks, ADEV_NOCAPACITY))
		return 0;

	sc->params.disksize = cd_size(sc->sc_link, flags, &sc->params.secsize);

	if ((sc->params.secsize < 512) ||
	    ((sc->params.secsize & 511) != 0))
		sc->params.secsize = 2048;	/* some drives lie ! */

	if (sc->params.disksize < 100)
		sc->params.disksize = 400000;

	return 0;
}

daddr_t
cdsize(dev_t dev)
{
	/* CD-ROMs are read-only. */
	return -1;
}

int
cddump(dev_t dev, daddr_t blkno, caddr_t va, size_t size)
{
	/* Not implemented. */
	return ENXIO;
}

#define	dvd_copy_key(dst, src)		memcpy((dst), (src), DVD_KEY_SIZE)
#define	dvd_copy_challenge(dst, src)	memcpy((dst), (src), DVD_CHALLENGE_SIZE)

#define DVD_AUTH_BUFSIZE		20

int
dvd_auth(struct cd_softc *sc, union dvd_authinfo *a)
{
	struct scsi_generic	*cmd;
	struct scsi_xfer	*xs;
	u_int8_t		*buf;
	int			 error;

	buf = dma_alloc(DVD_AUTH_BUFSIZE, PR_WAITOK | PR_ZERO);
	if (buf == NULL)
		return ENOMEM;

	xs = scsi_xs_get(sc->sc_link, 0);
	if (xs == NULL) {
		error = ENOMEM;
		goto done;
	}
	xs->cmdlen = sizeof(*cmd);
	xs->timeout = 30000;
	xs->data = buf;

	cmd = &xs->cmd;

	switch (a->type) {
	case DVD_LU_SEND_AGID:
		cmd->opcode = GPCMD_REPORT_KEY;
		cmd->bytes[8] = 8;
		cmd->bytes[9] = 0 | (0 << 6);
		xs->datalen = 8;
		SET(xs->flags, SCSI_DATA_IN);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);

		if (error == 0)
			a->lsa.agid = buf[7] >> 6;
		break;

	case DVD_LU_SEND_CHALLENGE:
		cmd->opcode = GPCMD_REPORT_KEY;
		cmd->bytes[8] = 16;
		cmd->bytes[9] = 1 | (a->lsc.agid << 6);
		xs->datalen = 16;
		SET(xs->flags, SCSI_DATA_IN);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);
		if (error == 0)
			dvd_copy_challenge(a->lsc.chal, &buf[4]);
		break;

	case DVD_LU_SEND_KEY1:
		cmd->opcode = GPCMD_REPORT_KEY;
		cmd->bytes[8] = 12;
		cmd->bytes[9] = 2 | (a->lsk.agid << 6);
		xs->datalen = 12;
		SET(xs->flags, SCSI_DATA_IN);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);

		if (error == 0)
			dvd_copy_key(a->lsk.key, &buf[4]);
		break;

	case DVD_LU_SEND_TITLE_KEY:
		cmd->opcode = GPCMD_REPORT_KEY;
		_lto4b(a->lstk.lba, &cmd->bytes[1]);
		cmd->bytes[8] = 12;
		cmd->bytes[9] = 4 | (a->lstk.agid << 6);
		xs->datalen = 12;
		SET(xs->flags, SCSI_DATA_IN);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);

		if (error == 0) {
			a->lstk.cpm = (buf[4] >> 7) & 1;
			a->lstk.cp_sec = (buf[4] >> 6) & 1;
			a->lstk.cgms = (buf[4] >> 4) & 3;
			dvd_copy_key(a->lstk.title_key, &buf[5]);
		}
		break;

	case DVD_LU_SEND_ASF:
		cmd->opcode = GPCMD_REPORT_KEY;
		cmd->bytes[8] = 8;
		cmd->bytes[9] = 5 | (a->lsasf.agid << 6);
		xs->datalen = 8;
		SET(xs->flags, SCSI_DATA_IN);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);

		if (error == 0)
			a->lsasf.asf = buf[7] & 1;
		break;

	case DVD_HOST_SEND_CHALLENGE:
		cmd->opcode = GPCMD_SEND_KEY;
		cmd->bytes[8] = 16;
		cmd->bytes[9] = 1 | (a->hsc.agid << 6);
		buf[1] = 14;
		dvd_copy_challenge(&buf[4], a->hsc.chal);
		xs->datalen = 16;
		SET(xs->flags, SCSI_DATA_OUT);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);

		if (error == 0)
			a->type = DVD_LU_SEND_KEY1;
		break;

	case DVD_HOST_SEND_KEY2:
		cmd->opcode = GPCMD_SEND_KEY;
		cmd->bytes[8] = 12;
		cmd->bytes[9] = 3 | (a->hsk.agid << 6);
		buf[1] = 10;
		dvd_copy_key(&buf[4], a->hsk.key);
		xs->datalen = 12;
		SET(xs->flags, SCSI_DATA_OUT);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);

		if (error == 0)
			a->type = DVD_AUTH_ESTABLISHED;
		else
			a->type = DVD_AUTH_FAILURE;
		break;

	case DVD_INVALIDATE_AGID:
		cmd->opcode = GPCMD_REPORT_KEY;
		cmd->bytes[9] = 0x3f | (a->lsa.agid << 6);
		xs->data = NULL;

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);
		break;

	case DVD_LU_SEND_RPC_STATE:
		cmd->opcode = GPCMD_REPORT_KEY;
		cmd->bytes[8] = 8;
		cmd->bytes[9] = 8 | (0 << 6);
		xs->datalen = 8;
		SET(xs->flags, SCSI_DATA_IN);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);

		if (error == 0) {
			a->lrpcs.type = (buf[4] >> 6) & 3;
			a->lrpcs.vra = (buf[4] >> 3) & 7;
			a->lrpcs.ucca = (buf[4]) & 7;
			a->lrpcs.region_mask = buf[5];
			a->lrpcs.rpc_scheme = buf[6];
		}
		break;

	case DVD_HOST_SEND_RPC_STATE:
		cmd->opcode = GPCMD_SEND_KEY;
		cmd->bytes[8] = 8;
		cmd->bytes[9] = 6 | (0 << 6);
		buf[1] = 6;
		buf[4] = a->hrpcs.pdrc;
		xs->datalen = 8;
		SET(xs->flags, SCSI_DATA_OUT);

		error = scsi_xs_sync(xs);
		scsi_xs_put(xs);
		break;

	default:
		scsi_xs_put(xs);
		error = ENOTTY;
		break;
	}
done:
	dma_free(buf, DVD_AUTH_BUFSIZE);
	return error;
}

#define DVD_READ_PHYSICAL_BUFSIZE (4 + 4 * 20)
int
dvd_read_physical(struct cd_softc *sc, union dvd_struct *s)
{
	struct scsi_generic		*cmd;
	struct dvd_layer		*layer;
	struct scsi_xfer		*xs;
	u_int8_t			*buf, *bufp;
	int				 error, i;

	buf = dma_alloc(DVD_READ_PHYSICAL_BUFSIZE, PR_WAITOK | PR_ZERO);
	if (buf == NULL)
		return ENOMEM;

	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN);
	if (xs == NULL) {
		error = ENOMEM;
		goto done;
	}
	xs->cmdlen = sizeof(*cmd);
	xs->data = buf;
	xs->datalen = DVD_READ_PHYSICAL_BUFSIZE;
	xs->timeout = 30000;

	cmd = &xs->cmd;
	cmd->opcode = GPCMD_READ_DVD_STRUCTURE;
	cmd->bytes[6] = s->type;
	_lto2b(xs->datalen, &cmd->bytes[7]);

	cmd->bytes[5] = s->physical.layer_num;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0) {
		for (i = 0, bufp = &buf[4], layer = &s->physical.layer[0];
		    i < 4; i++, bufp += 20, layer++) {
			bzero(layer, sizeof(*layer));
			layer->book_version = bufp[0] & 0xf;
			layer->book_type = bufp[0] >> 4;
			layer->min_rate = bufp[1] & 0xf;
			layer->disc_size = bufp[1] >> 4;
			layer->layer_type = bufp[2] & 0xf;
			layer->track_path = (bufp[2] >> 4) & 1;
			layer->nlayers = (bufp[2] >> 5) & 3;
			layer->track_density = bufp[3] & 0xf;
			layer->linear_density = bufp[3] >> 4;
			layer->start_sector = _4btol(&bufp[4]);
			layer->end_sector = _4btol(&bufp[8]);
			layer->end_sector_l0 = _4btol(&bufp[12]);
			layer->bca = bufp[16] >> 7;
		}
	}
done:
	dma_free(buf, DVD_READ_PHYSICAL_BUFSIZE);
	return error;
}

#define DVD_READ_COPYRIGHT_BUFSIZE	8
int
dvd_read_copyright(struct cd_softc *sc, union dvd_struct *s)
{
	struct scsi_generic		*cmd;
	struct scsi_xfer		*xs;
	u_int8_t			*buf;
	int				 error;

	buf = dma_alloc(DVD_READ_COPYRIGHT_BUFSIZE, PR_WAITOK | PR_ZERO);
	if (buf == NULL)
		return ENOMEM;

	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN);
	if (xs == NULL) {
		error = ENOMEM;
		goto done;
	}
	xs->cmdlen = sizeof(*cmd);
	xs->data = buf;
	xs->datalen = DVD_READ_COPYRIGHT_BUFSIZE;
	xs->timeout = 30000;

	cmd = &xs->cmd;
	cmd->opcode = GPCMD_READ_DVD_STRUCTURE;
	cmd->bytes[6] = s->type;
	_lto2b(xs->datalen, &cmd->bytes[7]);

	cmd->bytes[5] = s->copyright.layer_num;

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0) {
		s->copyright.cpst = buf[4];
		s->copyright.rmi = buf[5];
	}
done:
	dma_free(buf, DVD_READ_COPYRIGHT_BUFSIZE);
	return error;
}

int
dvd_read_disckey(struct cd_softc *sc, union dvd_struct *s)
{
	struct scsi_read_dvd_structure_data	*buf;
	struct scsi_read_dvd_structure		*cmd;
	struct scsi_xfer			*xs;
	int					 error;

	buf = dma_alloc(sizeof(*buf), PR_WAITOK | PR_ZERO);
	if (buf == NULL)
		return ENOMEM;

	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN);
	if (xs == NULL) {
		error = ENOMEM;
		goto done;
	}
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)buf;
	xs->datalen = sizeof(*buf);
	xs->timeout = 30000;

	cmd = (struct scsi_read_dvd_structure *)&xs->cmd;
	cmd->opcode = GPCMD_READ_DVD_STRUCTURE;
	cmd->format = s->type;
	cmd->agid = s->disckey.agid << 6;
	_lto2b(xs->datalen, cmd->length);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0)
		memcpy(s->disckey.value, buf->data, sizeof(s->disckey.value));
done:
	dma_free(buf, sizeof(*buf));
	return error;
}

#define DVD_READ_BCA_BUFLEN (4 + 188)

int
dvd_read_bca(struct cd_softc *sc, union dvd_struct *s)
{
	struct scsi_generic		*cmd;
	struct scsi_xfer		*xs;
	u_int8_t			*buf;
	int				 error;

	buf = dma_alloc(DVD_READ_BCA_BUFLEN, PR_WAITOK | PR_ZERO);
	if (buf == NULL)
		return ENOMEM;

	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN);
	if (xs == NULL) {
		error = ENOMEM;
		goto done;
	}
	xs->cmdlen = sizeof(*cmd);
	xs->data = buf;
	xs->datalen = DVD_READ_BCA_BUFLEN;
	xs->timeout = 30000;

	cmd = &xs->cmd;
	cmd->opcode = GPCMD_READ_DVD_STRUCTURE;
	cmd->bytes[6] = s->type;
	_lto2b(xs->datalen, &cmd->bytes[7]);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0) {
		s->bca.len = _2btol(&buf[0]);
		if (s->bca.len < 12 || s->bca.len > 188)
			return EIO;
		memcpy(s->bca.value, &buf[4], s->bca.len);
	}
done:
	dma_free(buf, DVD_READ_BCA_BUFLEN);
	return error;
}

int
dvd_read_manufact(struct cd_softc *sc, union dvd_struct *s)
{
	struct scsi_read_dvd_structure_data	*buf;
	struct scsi_read_dvd_structure		*cmd;
	struct scsi_xfer			*xs;
	int					 error;

	buf = dma_alloc(sizeof(*buf), PR_WAITOK | PR_ZERO);
	if (buf == NULL)
		return ENOMEM;

	xs = scsi_xs_get(sc->sc_link, SCSI_DATA_IN);
	if (xs == NULL) {
		error = ENOMEM;
		goto done;
	}
	xs->cmdlen = sizeof(*cmd);
	xs->data = (void *)buf;
	xs->datalen = sizeof(*buf);
	xs->timeout = 30000;

	cmd = (struct scsi_read_dvd_structure *)&xs->cmd;
	cmd->opcode = GPCMD_READ_DVD_STRUCTURE;
	cmd->format = s->type;
	_lto2b(xs->datalen, cmd->length);

	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);

	if (error == 0) {
		s->manufact.len = _2btol(buf->len);
		if (s->manufact.len >= 0 && s->manufact.len <= 2048)
			memcpy(s->manufact.value, buf->data, s->manufact.len);
		else
			error = EIO;
	}
done:
	dma_free(buf, sizeof(*buf));
	return error;
}

int
dvd_read_struct(struct cd_softc *sc, union dvd_struct *s)
{
	switch (s->type) {
	case DVD_STRUCT_PHYSICAL:
		return dvd_read_physical(sc, s);
	case DVD_STRUCT_COPYRIGHT:
		return dvd_read_copyright(sc, s);
	case DVD_STRUCT_DISCKEY:
		return dvd_read_disckey(sc, s);
	case DVD_STRUCT_BCA:
		return dvd_read_bca(sc, s);
	case DVD_STRUCT_MANUFACT:
		return dvd_read_manufact(sc, s);
	default:
		return EINVAL;
	}
}

int
cd_interpret_sense(struct scsi_xfer *xs)
{
	struct scsi_sense_data		*sense = &xs->sense;
	struct scsi_link		*link = xs->sc_link;
	u_int8_t			 skey = sense->flags & SSD_KEY;
	u_int8_t			 serr = sense->error_code & SSD_ERRCODE;

	if (!ISSET(link->flags, SDEV_OPEN) ||
	    (serr != SSD_ERRCODE_CURRENT && serr != SSD_ERRCODE_DEFERRED))
		return scsi_interpret_sense(xs);

	/*
	 * We do custom processing in cd for the unit becoming ready
	 * case.  We do not allow xs->retries to be decremented on the
	 * "Unit Becoming Ready" case. This is because CD drives
	 * report "Unit Becoming Ready" when loading media and can
	 * take a long time.  Rather than having a massive timeout for
	 * all operations (which would cause other problems), we allow
	 * operations to wait (but be interruptible with Ctrl-C)
	 * forever as long as the drive is reporting that it is
	 * becoming ready.  All other cases of not being ready are
	 * handled by the default handler.
	 */
	switch(skey) {
	case SKEY_NOT_READY:
		if (ISSET(xs->flags, SCSI_IGNORE_NOT_READY))
			return 0;
		if (ASC_ASCQ(sense) == SENSE_NOT_READY_BECOMING_READY) {
			SC_DEBUG(link, SDEV_DB1, ("not ready: busy (%#x)\n",
			    sense->add_sense_code_qual));
			/* don't count this as a retry */
			xs->retries++;
			return scsi_delay(xs, 1);
		}
		break;
		/* XXX more to come here for a few other cases */
	default:
		break;
	}
	return scsi_interpret_sense(xs);
}

/*
 * Find out from the device what its capacity is.
 */
u_int64_t
cd_size(struct scsi_link *link, int flags, u_int32_t *blksize)
{
	struct scsi_read_cap_data_16	*rdcap16;
	struct scsi_read_cap_data	*rdcap;
	u_int64_t			 max_addr;
	int				 error;

	if (blksize != NULL)
		*blksize = 0;

	CLR(flags, SCSI_IGNORE_ILLEGAL_REQUEST);

	/*
	 * Start with a READ CAPACITY(10).
	 */
	rdcap = dma_alloc(sizeof(*rdcap), ((flags & SCSI_NOSLEEP) ?
	    PR_NOWAIT : PR_WAITOK) | PR_ZERO);
	if (rdcap == NULL)
		return 0;

	error = scsi_read_cap_10(link, rdcap, flags);
	if (error) {
		dma_free(rdcap, sizeof(*rdcap));
		return 0;
	}

	max_addr = _4btol(rdcap->addr);
	if (blksize != NULL)
		*blksize = _4btol(rdcap->length);
	dma_free(rdcap, sizeof(*rdcap));

	/*
	 * pre-SPC (i.e. pre-SCSI-3) devices reporting less than 2^32-1 sectors
	 * can stop here.
	 */
	if (SID_ANSII_REV(&link->inqdata) < SCSI_REV_SPC &&
	    max_addr != 0xffffffff)
		goto exit;

	rdcap16 = dma_alloc(sizeof(*rdcap16), ((flags & SCSI_NOSLEEP) ?
	    PR_NOWAIT : PR_WAITOK) | PR_ZERO);
	if (rdcap16 == NULL)
		goto exit;

	error = scsi_read_cap_16(link, rdcap16, flags);
	if (error) {
		dma_free(rdcap16, sizeof(*rdcap16));
		goto exit;
	}

	max_addr = _8btol(rdcap16->addr);
	if (blksize != NULL)
		*blksize = _4btol(rdcap16->length);
	/* XXX The other READ CAPACITY(16) info could be stored away. */
	dma_free(rdcap16, sizeof(*rdcap16));

	return max_addr + 1;

exit:
	/* Return READ CAPACITY 10 values. */
	if (max_addr != 0xffffffff)
		return max_addr + 1;
	else if (blksize != NULL)
		*blksize = 0;
	return 0;
}

#if defined(__macppc__)
int
cd_eject(void)
{
	struct cd_softc			*sc;
	int				 error = 0;

	if (cd_cd.cd_ndevs == 0 || (sc = cd_cd.cd_devs[0]) == NULL)
		return ENXIO;

	if ((error = disk_lock(&sc->sc_dk)) != 0)
		return error;

	if (sc->sc_dk.dk_openmask == 0) {
		SET(sc->sc_link->flags, SDEV_EJECTING);

		scsi_prevent(sc->sc_link, PR_ALLOW,
		    SCSI_IGNORE_ILLEGAL_REQUEST | SCSI_IGNORE_NOT_READY |
		    SCSI_SILENT | SCSI_IGNORE_MEDIA_CHANGE);
		CLR(sc->sc_link->flags, SDEV_MEDIA_LOADED);

		scsi_start(sc->sc_link, SSS_STOP|SSS_LOEJ, 0);

		CLR(sc->sc_link->flags, SDEV_EJECTING);
	}
	disk_unlock(&sc->sc_dk);

	return error;
}
#endif /* __macppc__ */
