/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * Copyright (c) 1997, 1998, 1999, 2000, 2001, 2002, 2003 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Portions of this driver taken from the original FreeBSD cd driver.
 * Written by Julian Elischer (julian@tfs.com)
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
 *
 *      from: cd.c,v 1.83 1997/05/04 15:24:22 joerg Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/malloc.h>
#include <sys/cdio.h>
#include <sys/cdrio.h>
#include <sys/dvdio.h>
#include <sys/devicestat.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/taskqueue.h>
#include <geom/geom_disk.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_queue.h>
#include <cam/cam_sim.h>

#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_da.h>
#include <cam/scsi/scsi_cd.h>

#define LEADOUT         0xaa            /* leadout toc entry */

struct cd_params {
	u_int32_t blksize;
	u_long    disksize;
};

typedef enum {
	CD_Q_NONE		= 0x00,
	CD_Q_NO_TOUCH		= 0x01,
	CD_Q_BCD_TRACKS		= 0x02,
	CD_Q_10_BYTE_ONLY	= 0x10,
	CD_Q_RETRY_BUSY		= 0x40
} cd_quirks;

#define CD_Q_BIT_STRING		\
	"\020"			\
	"\001NO_TOUCH"		\
	"\002BCD_TRACKS"	\
	"\00510_BYTE_ONLY"	\
	"\007RETRY_BUSY"

typedef enum {
	CD_FLAG_INVALID		= 0x0001,
	CD_FLAG_NEW_DISC	= 0x0002,
	CD_FLAG_DISC_LOCKED	= 0x0004,
	CD_FLAG_DISC_REMOVABLE	= 0x0008,
	CD_FLAG_SAW_MEDIA	= 0x0010,
	CD_FLAG_ACTIVE		= 0x0080,
	CD_FLAG_SCHED_ON_COMP	= 0x0100,
	CD_FLAG_RETRY_UA	= 0x0200,
	CD_FLAG_VALID_MEDIA	= 0x0400,
	CD_FLAG_VALID_TOC	= 0x0800,
	CD_FLAG_SCTX_INIT	= 0x1000
} cd_flags;

typedef enum {
	CD_CCB_PROBE		= 0x01,
	CD_CCB_BUFFER_IO	= 0x02,
	CD_CCB_TUR		= 0x04,
	CD_CCB_TYPE_MASK	= 0x0F,
	CD_CCB_RETRY_UA		= 0x10
} cd_ccb_state;

#define ccb_state ppriv_field0
#define ccb_bp ppriv_ptr1

struct cd_tocdata {
	struct ioc_toc_header header;
	struct cd_toc_entry entries[100];
};

struct cd_toc_single {
	struct ioc_toc_header header;
	struct cd_toc_entry entry;
};

typedef enum {
	CD_STATE_PROBE,
	CD_STATE_NORMAL
} cd_state;

struct cd_softc {
	cam_pinfo		pinfo;
	cd_state		state;
	volatile cd_flags	flags;
	struct bio_queue_head	bio_queue;
	LIST_HEAD(, ccb_hdr)	pending_ccbs;
	struct cd_params	params;
	union ccb		saved_ccb;
	cd_quirks		quirks;
	struct cam_periph	*periph;
	int			minimum_command_size;
	int			outstanding_cmds;
	int			tur;
	struct task		sysctl_task;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
	STAILQ_HEAD(, cd_mode_params)	mode_queue;
	struct cd_tocdata	toc;
	struct disk		*disk;
	struct callout		mediapoll_c;

#define CD_ANNOUNCETMP_SZ 120
	char			announce_temp[CD_ANNOUNCETMP_SZ];
#define CD_ANNOUNCE_SZ 400
	char			announce_buf[CD_ANNOUNCE_SZ];
};

struct cd_page_sizes {
	int page;
	int page_size;
};

static struct cd_page_sizes cd_page_size_table[] =
{
	{ AUDIO_PAGE, sizeof(struct cd_audio_page)}
};

struct cd_quirk_entry {
	struct scsi_inquiry_pattern inq_pat;
	cd_quirks quirks;
};

/*
 * NOTE ON 10_BYTE_ONLY quirks:  Any 10_BYTE_ONLY quirks MUST be because
 * your device hangs when it gets a 10 byte command.  Adding a quirk just
 * to get rid of the informative diagnostic message is not acceptable.  All
 * 10_BYTE_ONLY quirks must be documented in full in a PR (which should be
 * referenced in a comment along with the quirk) , and must be approved by
 * ken@FreeBSD.org.  Any quirks added that don't adhere to this policy may
 * be removed until the submitter can explain why they are needed.
 * 10_BYTE_ONLY quirks will be removed (as they will no longer be necessary)
 * when the CAM_NEW_TRAN_CODE work is done.
 */
static struct cd_quirk_entry cd_quirk_table[] =
{
	{
		{ T_CDROM, SIP_MEDIA_REMOVABLE, "CHINON", "CD-ROM CDS-535","*"},
		/* quirks */ CD_Q_BCD_TRACKS
	},
	{
		/*
		 * VMware returns BUSY status when storage has transient
		 * connectivity problems, so better wait.
		 */
		{T_CDROM, SIP_MEDIA_REMOVABLE, "NECVMWar", "VMware IDE CDR10", "*"},
		/*quirks*/ CD_Q_RETRY_BUSY
	}
};

#ifdef COMPAT_FREEBSD32
struct ioc_read_toc_entry32 {
	u_char	address_format;
	u_char	starting_track;
	u_short	data_len;
	uint32_t data;	/* (struct cd_toc_entry *) */
};
#define	CDIOREADTOCENTRYS_32	\
    _IOC_NEWTYPE(CDIOREADTOCENTRYS, struct ioc_read_toc_entry32)
#endif

static	disk_open_t	cdopen;
static	disk_close_t	cdclose;
static	disk_ioctl_t	cdioctl;
static	disk_strategy_t	cdstrategy;

static	periph_init_t	cdinit;
static	periph_ctor_t	cdregister;
static	periph_dtor_t	cdcleanup;
static	periph_start_t	cdstart;
static	periph_oninv_t	cdoninvalidate;
static	void		cdasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	int		cdcmdsizesysctl(SYSCTL_HANDLER_ARGS);
static	int		cdrunccb(union ccb *ccb,
				 int (*error_routine)(union ccb *ccb,
						      u_int32_t cam_flags,
						      u_int32_t sense_flags),
				 u_int32_t cam_flags, u_int32_t sense_flags);
static	void		cddone(struct cam_periph *periph,
			       union ccb *start_ccb);
static	union cd_pages	*cdgetpage(struct cd_mode_params *mode_params);
static	int		cdgetpagesize(int page_num);
static	void		cdprevent(struct cam_periph *periph, int action);
static	int		cdcheckmedia(struct cam_periph *periph);
static	int		cdsize(struct cam_periph *periph, u_int32_t *size);
static	int		cd6byteworkaround(union ccb *ccb);
static	int		cderror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);
static	int		cdreadtoc(struct cam_periph *periph, u_int32_t mode, 
				  u_int32_t start, u_int8_t *data, 
				  u_int32_t len, u_int32_t sense_flags);
static	int		cdgetmode(struct cam_periph *periph, 
				  struct cd_mode_params *data, u_int32_t page);
static	int		cdsetmode(struct cam_periph *periph,
				  struct cd_mode_params *data);
static	int		cdplay(struct cam_periph *periph, u_int32_t blk, 
			       u_int32_t len);
static	int		cdreadsubchannel(struct cam_periph *periph, 
					 u_int32_t mode, u_int32_t format, 
					 int track, 
					 struct cd_sub_channel_info *data, 
					 u_int32_t len);
static	int		cdplaymsf(struct cam_periph *periph, u_int32_t startm, 
				  u_int32_t starts, u_int32_t startf, 
				  u_int32_t endm, u_int32_t ends, 
				  u_int32_t endf);
static	int		cdplaytracks(struct cam_periph *periph, 
				     u_int32_t strack, u_int32_t sindex,
				     u_int32_t etrack, u_int32_t eindex);
static	int		cdpause(struct cam_periph *periph, u_int32_t go);
static	int		cdstopunit(struct cam_periph *periph, u_int32_t eject);
static	int		cdstartunit(struct cam_periph *periph, int load);
static	int		cdsetspeed(struct cam_periph *periph,
				   u_int32_t rdspeed, u_int32_t wrspeed);
static	int		cdreportkey(struct cam_periph *periph,
				    struct dvd_authinfo *authinfo);
static	int		cdsendkey(struct cam_periph *periph,
				  struct dvd_authinfo *authinfo);
static	int		cdreaddvdstructure(struct cam_periph *periph,
					   struct dvd_struct *dvdstruct);
static timeout_t	cdmediapoll;

static struct periph_driver cddriver =
{
	cdinit, "cd",
	TAILQ_HEAD_INITIALIZER(cddriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(cd, cddriver);

#ifndef	CD_DEFAULT_POLL_PERIOD
#define	CD_DEFAULT_POLL_PERIOD	3
#endif
#ifndef	CD_DEFAULT_RETRY
#define	CD_DEFAULT_RETRY	4
#endif
#ifndef	CD_DEFAULT_TIMEOUT
#define	CD_DEFAULT_TIMEOUT	30000
#endif

static int cd_poll_period = CD_DEFAULT_POLL_PERIOD;
static int cd_retry_count = CD_DEFAULT_RETRY;
static int cd_timeout = CD_DEFAULT_TIMEOUT;

static SYSCTL_NODE(_kern_cam, OID_AUTO, cd, CTLFLAG_RD, 0, "CAM CDROM driver");
SYSCTL_INT(_kern_cam_cd, OID_AUTO, poll_period, CTLFLAG_RWTUN,
           &cd_poll_period, 0, "Media polling period in seconds");
SYSCTL_INT(_kern_cam_cd, OID_AUTO, retry_count, CTLFLAG_RWTUN,
           &cd_retry_count, 0, "Normal I/O retry count");
SYSCTL_INT(_kern_cam_cd, OID_AUTO, timeout, CTLFLAG_RWTUN,
	   &cd_timeout, 0, "Timeout, in us, for read operations");

static MALLOC_DEFINE(M_SCSICD, "scsi_cd", "scsi_cd buffers");

static void
cdinit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, cdasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("cd: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}
}

/*
 * Callback from GEOM, called when it has finished cleaning up its
 * resources.
 */
static void
cddiskgonecb(struct disk *dp)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)dp->d_drv1;
	cam_periph_release(periph);
}

static void
cdoninvalidate(struct cam_periph *periph)
{
	struct cd_softc *softc;

	softc = (struct cd_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, cdasync, periph, periph->path);

	softc->flags |= CD_FLAG_INVALID;

	/*
	 * Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */
	bioq_flush(&softc->bio_queue, NULL, ENXIO);

	disk_gone(softc->disk);
}

static void
cdcleanup(struct cam_periph *periph)
{
	struct cd_softc *softc;

	softc = (struct cd_softc *)periph->softc;

	cam_periph_unlock(periph);
	if ((softc->flags & CD_FLAG_SCTX_INIT) != 0
	    && sysctl_ctx_free(&softc->sysctl_ctx) != 0) {
		xpt_print(periph->path, "can't remove sysctl context\n");
	}

	callout_drain(&softc->mediapoll_c);
	disk_destroy(softc->disk);
	free(softc, M_DEVBUF);
	cam_periph_lock(periph);
}

static void
cdasync(void *callback_arg, u_int32_t code,
	struct cam_path *path, void *arg)
{
	struct cam_periph *periph;
	struct cd_softc *softc;

	periph = (struct cam_periph *)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
	{
		struct ccb_getdev *cgd;
		cam_status status;

		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL)
			break;

		if (cgd->protocol != PROTO_SCSI)
			break;
		if (SID_QUAL(&cgd->inq_data) != SID_QUAL_LU_CONNECTED)
			break;
		if (SID_TYPE(&cgd->inq_data) != T_CDROM
		    && SID_TYPE(&cgd->inq_data) != T_WORM)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(cdregister, cdoninvalidate,
					  cdcleanup, cdstart,
					  "cd", CAM_PERIPH_BIO,
					  path, cdasync,
					  AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("cdasync: Unable to attach new device "
			       "due to status 0x%x\n", status);

		break;
	}
	case AC_UNIT_ATTENTION:
	{
		union ccb *ccb;
		int error_code, sense_key, asc, ascq;

		softc = (struct cd_softc *)periph->softc;
		ccb = (union ccb *)arg;

		/*
		 * Handle all media change UNIT ATTENTIONs except
		 * our own, as they will be handled by cderror().
		 */
		if (xpt_path_periph(ccb->ccb_h.path) != periph &&
		    scsi_extract_sense_ccb(ccb,
		     &error_code, &sense_key, &asc, &ascq)) {
			if (asc == 0x28 && ascq == 0x00)
				disk_media_changed(softc->disk, M_NOWAIT);
		}
		cam_periph_async(periph, code, path, arg);
		break;
	}
	case AC_SCSI_AEN:
		softc = (struct cd_softc *)periph->softc;
		if (softc->state == CD_STATE_NORMAL && !softc->tur) {
			if (cam_periph_acquire(periph) == 0) {
				softc->tur = 1;
				xpt_schedule(periph, CAM_PRIORITY_NORMAL);
			}
		}
		/* FALLTHROUGH */
	case AC_SENT_BDR:
	case AC_BUS_RESET:
	{
		struct ccb_hdr *ccbh;

		softc = (struct cd_softc *)periph->softc;
		/*
		 * Don't fail on the expected unit attention
		 * that will occur.
		 */
		softc->flags |= CD_FLAG_RETRY_UA;
		LIST_FOREACH(ccbh, &softc->pending_ccbs, periph_links.le)
			ccbh->ccb_state |= CD_CCB_RETRY_UA;
		/* FALLTHROUGH */
	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static void
cdsysctlinit(void *context, int pending)
{
	struct cam_periph *periph;
	struct cd_softc *softc;
	char tmpstr[32], tmpstr2[16];

	periph = (struct cam_periph *)context;
	if (cam_periph_acquire(periph) != 0)
		return;

	softc = (struct cd_softc *)periph->softc;
	snprintf(tmpstr, sizeof(tmpstr), "CAM CD unit %d", periph->unit_number);
	snprintf(tmpstr2, sizeof(tmpstr2), "%d", periph->unit_number);

	sysctl_ctx_init(&softc->sysctl_ctx);
	softc->flags |= CD_FLAG_SCTX_INIT;
	softc->sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&softc->sysctl_ctx,
		SYSCTL_STATIC_CHILDREN(_kern_cam_cd), OID_AUTO,
		tmpstr2, CTLFLAG_RD, 0, tmpstr, "device_index");

	if (softc->sysctl_tree == NULL) {
		printf("cdsysctlinit: unable to allocate sysctl tree\n");
		cam_periph_release(periph);
		return;
	}

	/*
	 * Now register the sysctl handler, so the user can the value on
	 * the fly.
	 */
	SYSCTL_ADD_PROC(&softc->sysctl_ctx,SYSCTL_CHILDREN(softc->sysctl_tree),
		OID_AUTO, "minimum_cmd_size", CTLTYPE_INT | CTLFLAG_RW,
		&softc->minimum_command_size, 0, cdcmdsizesysctl, "I",
		"Minimum CDB size");

	cam_periph_release(periph);
}

/*
 * We have a handler function for this so we can check the values when the
 * user sets them, instead of every time we look at them.
 */
static int
cdcmdsizesysctl(SYSCTL_HANDLER_ARGS)
{
	int error, value;

	value = *(int *)arg1;

	error = sysctl_handle_int(oidp, &value, 0, req);

	if ((error != 0)
	 || (req->newptr == NULL))
		return (error);

	/*
	 * The only real values we can have here are 6 or 10.  I don't
	 * really forsee having 12 be an option at any time in the future.
	 * So if the user sets something less than or equal to 6, we'll set
	 * it to 6.  If he sets something greater than 6, we'll set it to 10.
	 *
	 * I suppose we could just return an error here for the wrong values,
	 * but I don't think it's necessary to do so, as long as we can
	 * determine the user's intent without too much trouble.
	 */
	if (value < 6)
		value = 6;
	else if (value > 6)
		value = 10;

	*(int *)arg1 = value;

	return (0);
}

static cam_status
cdregister(struct cam_periph *periph, void *arg)
{
	struct cd_softc *softc;
	struct ccb_pathinq cpi;
	struct ccb_getdev *cgd;
	char tmpstr[80];
	caddr_t match;

	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("cdregister: no getdev CCB, can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = (struct cd_softc *)malloc(sizeof(*softc),M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (softc == NULL) {
		printf("cdregister: Unable to probe new device. "
		       "Unable to allocate softc\n");				
		return(CAM_REQ_CMP_ERR);
	}

	LIST_INIT(&softc->pending_ccbs);
	STAILQ_INIT(&softc->mode_queue);
	softc->state = CD_STATE_PROBE;
	bioq_init(&softc->bio_queue);
	if (SID_IS_REMOVABLE(&cgd->inq_data))
		softc->flags |= CD_FLAG_DISC_REMOVABLE;

	periph->softc = softc;
	softc->periph = periph;

	/*
	 * See if this device has any quirks.
	 */
	match = cam_quirkmatch((caddr_t)&cgd->inq_data,
			       (caddr_t)cd_quirk_table,
			       nitems(cd_quirk_table),
			       sizeof(*cd_quirk_table), scsi_inquiry_match);

	if (match != NULL)
		softc->quirks = ((struct cd_quirk_entry *)match)->quirks;
	else
		softc->quirks = CD_Q_NONE;

	/* Check if the SIM does not want 6 byte commands */
	xpt_path_inq(&cpi, periph->path);
	if (cpi.ccb_h.status == CAM_REQ_CMP && (cpi.hba_misc & PIM_NO_6_BYTE))
		softc->quirks |= CD_Q_10_BYTE_ONLY;

	TASK_INIT(&softc->sysctl_task, 0, cdsysctlinit, periph);

	/* The default is 6 byte commands, unless quirked otherwise */
	if (softc->quirks & CD_Q_10_BYTE_ONLY)
		softc->minimum_command_size = 10;
	else
		softc->minimum_command_size = 6;

	/*
	 * Refcount and block open attempts until we are setup
	 * Can't block
	 */
	(void)cam_periph_hold(periph, PRIBIO);
	cam_periph_unlock(periph);
	/*
	 * Load the user's default, if any.
	 */
	snprintf(tmpstr, sizeof(tmpstr), "kern.cam.cd.%d.minimum_cmd_size",
		 periph->unit_number);
	TUNABLE_INT_FETCH(tmpstr, &softc->minimum_command_size);

	/* 6 and 10 are the only permissible values here. */
	if (softc->minimum_command_size < 6)
		softc->minimum_command_size = 6;
	else if (softc->minimum_command_size > 6)
		softc->minimum_command_size = 10;

	/*
	 * We need to register the statistics structure for this device,
	 * but we don't have the blocksize yet for it.  So, we register
	 * the structure and indicate that we don't have the blocksize
	 * yet.  Unlike other SCSI peripheral drivers, we explicitly set
	 * the device type here to be CDROM, rather than just ORing in
	 * the device type.  This is because this driver can attach to either
	 * CDROM or WORM devices, and we want this peripheral driver to
	 * show up in the devstat list as a CD peripheral driver, not a
	 * WORM peripheral driver.  WORM drives will also have the WORM
	 * driver attached to them.
	 */
	softc->disk = disk_alloc();
	softc->disk->d_devstat = devstat_new_entry("cd",
			  periph->unit_number, 0,
			  DEVSTAT_BS_UNAVAILABLE,
			  DEVSTAT_TYPE_CDROM |
			  XPORT_DEVSTAT_TYPE(cpi.transport),
			  DEVSTAT_PRIORITY_CD);
	softc->disk->d_open = cdopen;
	softc->disk->d_close = cdclose;
	softc->disk->d_strategy = cdstrategy;
	softc->disk->d_gone = cddiskgonecb;
	softc->disk->d_ioctl = cdioctl;
	softc->disk->d_name = "cd";
	cam_strvis(softc->disk->d_descr, cgd->inq_data.vendor,
	    sizeof(cgd->inq_data.vendor), sizeof(softc->disk->d_descr));
	strlcat(softc->disk->d_descr, " ", sizeof(softc->disk->d_descr));
	cam_strvis(&softc->disk->d_descr[strlen(softc->disk->d_descr)],
	    cgd->inq_data.product, sizeof(cgd->inq_data.product),
	    sizeof(softc->disk->d_descr) - strlen(softc->disk->d_descr));
	softc->disk->d_unit = periph->unit_number;
	softc->disk->d_drv1 = periph;
	if (cpi.maxio == 0)
		softc->disk->d_maxsize = DFLTPHYS;	/* traditional default */
	else if (cpi.maxio > MAXPHYS)
		softc->disk->d_maxsize = MAXPHYS;	/* for safety */
	else
		softc->disk->d_maxsize = cpi.maxio;
	softc->disk->d_flags = 0;
	softc->disk->d_hba_vendor = cpi.hba_vendor;
	softc->disk->d_hba_device = cpi.hba_device;
	softc->disk->d_hba_subvendor = cpi.hba_subvendor;
	softc->disk->d_hba_subdevice = cpi.hba_subdevice;

	/*
	 * Acquire a reference to the periph before we register with GEOM.
	 * We'll release this reference once GEOM calls us back (via
	 * dadiskgonecb()) telling us that our provider has been freed.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}

	disk_create(softc->disk, DISK_VERSION);
	cam_periph_lock(periph);

	/*
	 * Add an async callback so that we get
	 * notified if this device goes away.
	 */
	xpt_register_async(AC_SENT_BDR | AC_BUS_RESET | AC_LOST_DEVICE |
	    AC_SCSI_AEN | AC_UNIT_ATTENTION, cdasync, periph, periph->path);

	/*
	 * Schedule a periodic media polling events.
	 */
	callout_init_mtx(&softc->mediapoll_c, cam_periph_mtx(periph), 0);
	if ((softc->flags & CD_FLAG_DISC_REMOVABLE) &&
	    (cgd->inq_flags & SID_AEN) == 0 &&
	    cd_poll_period != 0)
		callout_reset(&softc->mediapoll_c, cd_poll_period * hz,
		    cdmediapoll, periph);

	xpt_schedule(periph, CAM_PRIORITY_DEV);
	return(CAM_REQ_CMP);
}

static int
cdopen(struct disk *dp)
{
	struct cam_periph *periph;
	struct cd_softc *softc;
	int error;

	periph = (struct cam_periph *)dp->d_drv1;
	softc = (struct cd_softc *)periph->softc;

	if (cam_periph_acquire(periph) != 0)
		return(ENXIO);

	cam_periph_lock(periph);

	if (softc->flags & CD_FLAG_INVALID) {
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return(ENXIO);
	}

	if ((error = cam_periph_hold(periph, PRIBIO | PCATCH)) != 0) {
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return (error);
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("cdopen\n"));

	/*
	 * Check for media, and set the appropriate flags.  We don't bail
	 * if we don't have media, but then we don't allow anything but the
	 * CDIOCEJECT/CDIOCCLOSE ioctls if there is no media.
	 */
	cdcheckmedia(periph);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("leaving cdopen\n"));
	cam_periph_unhold(periph);

	cam_periph_unlock(periph);

	return (0);
}

static int
cdclose(struct disk *dp)
{
	struct 	cam_periph *periph;
	struct	cd_softc *softc;

	periph = (struct cam_periph *)dp->d_drv1;
	softc = (struct cd_softc *)periph->softc;

	cam_periph_lock(periph);
	if (cam_periph_hold(periph, PRIBIO) != 0) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (0);
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE | CAM_DEBUG_PERIPH,
	    ("cdclose\n"));

	if ((softc->flags & CD_FLAG_DISC_REMOVABLE) != 0)
		cdprevent(periph, PR_ALLOW);

	/*
	 * Since we're closing this CD, mark the blocksize as unavailable.
	 * It will be marked as available when the CD is opened again.
	 */
	softc->disk->d_devstat->flags |= DEVSTAT_BS_UNAVAILABLE;

	/*
	 * We'll check the media and toc again at the next open().
	 */
	softc->flags &= ~(CD_FLAG_VALID_MEDIA|CD_FLAG_VALID_TOC);

	cam_periph_unhold(periph);
	cam_periph_release_locked(periph);
	cam_periph_unlock(periph);

	return (0);
}

static int
cdrunccb(union ccb *ccb, int (*error_routine)(union ccb *ccb,
					      u_int32_t cam_flags,
					      u_int32_t sense_flags),
	 u_int32_t cam_flags, u_int32_t sense_flags)
{
	struct cd_softc *softc;
	struct cam_periph *periph;
	int error;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct cd_softc *)periph->softc;

	error = cam_periph_runccb(ccb, error_routine, cam_flags, sense_flags,
				  softc->disk->d_devstat);

	return(error);
}

/*
 * Actually translate the requested transfer into one the physical driver
 * can understand.  The transfer is described by a buf and will include
 * only one physical transfer.
 */
static void
cdstrategy(struct bio *bp)
{
	struct cam_periph *periph;
	struct cd_softc *softc;

	periph = (struct cam_periph *)bp->bio_disk->d_drv1;
	cam_periph_lock(periph);
	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE,
	    ("cdstrategy(%p)\n", bp));

	softc = (struct cd_softc *)periph->softc;

	/*
	 * If the device has been made invalid, error out
	 */
	if ((softc->flags & CD_FLAG_INVALID)) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, ENXIO);
		return;
	}

        /*
	 * If we don't have valid media, look for it before trying to
	 * schedule the I/O.
	 */
	if ((softc->flags & CD_FLAG_VALID_MEDIA) == 0) {
		int error;

		error = cdcheckmedia(periph);
		if (error != 0) {
			cam_periph_unlock(periph);
			biofinish(bp, NULL, error);
			return;
		}
	}

	/*
	 * Place it in the queue of disk activities for this disk
	 */
	bioq_disksort(&softc->bio_queue, bp);

	xpt_schedule(periph, CAM_PRIORITY_NORMAL);

	cam_periph_unlock(periph);
	return;
}

static void
cdstart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct cd_softc *softc;
	struct bio *bp;
	struct ccb_scsiio *csio;
	struct scsi_read_capacity_data *rcap;

	softc = (struct cd_softc *)periph->softc;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("entering cdstart\n"));

	switch (softc->state) {
	case CD_STATE_NORMAL:
	{
		bp = bioq_first(&softc->bio_queue);
		if (bp == NULL) {
			if (softc->tur) {
				softc->tur = 0;
				csio = &start_ccb->csio;
				scsi_test_unit_ready(csio,
				     /*retries*/ cd_retry_count,
				     cddone,
				     MSG_SIMPLE_Q_TAG,
				     SSD_FULL_SIZE,
				     cd_timeout);
				start_ccb->ccb_h.ccb_bp = NULL;
				start_ccb->ccb_h.ccb_state = CD_CCB_TUR;
				xpt_action(start_ccb);
			} else
				xpt_release_ccb(start_ccb);
		} else {
			if (softc->tur) {
				softc->tur = 0;
				cam_periph_release_locked(periph);
			}
			bioq_remove(&softc->bio_queue, bp);

			scsi_read_write(&start_ccb->csio,
					/*retries*/ cd_retry_count,
					/* cbfcnp */ cddone,
					MSG_SIMPLE_Q_TAG,
					/* read */bp->bio_cmd == BIO_READ ?
					SCSI_RW_READ : SCSI_RW_WRITE,
					/* byte2 */ 0,
					/* minimum_cmd_size */ 10,
					/* lba */ bp->bio_offset /
					  softc->params.blksize,
					bp->bio_bcount / softc->params.blksize,
					/* data_ptr */ bp->bio_data,
					/* dxfer_len */ bp->bio_bcount,
					/* sense_len */ cd_retry_count ?
					  SSD_FULL_SIZE : SF_NO_PRINT,
					/* timeout */ cd_timeout);
			/* Use READ CD command for audio tracks. */
			if (softc->params.blksize == 2352) {
				start_ccb->csio.cdb_io.cdb_bytes[0] = READ_CD;
				start_ccb->csio.cdb_io.cdb_bytes[9] = 0xf8;
				start_ccb->csio.cdb_io.cdb_bytes[10] = 0;
				start_ccb->csio.cdb_io.cdb_bytes[11] = 0;
				start_ccb->csio.cdb_len = 12;
			}
			start_ccb->ccb_h.ccb_state = CD_CCB_BUFFER_IO;

			
			LIST_INSERT_HEAD(&softc->pending_ccbs,
					 &start_ccb->ccb_h, periph_links.le);
			softc->outstanding_cmds++;

			/* We expect a unit attention from this device */
			if ((softc->flags & CD_FLAG_RETRY_UA) != 0) {
				start_ccb->ccb_h.ccb_state |= CD_CCB_RETRY_UA;
				softc->flags &= ~CD_FLAG_RETRY_UA;
			}

			start_ccb->ccb_h.ccb_bp = bp;
			bp = bioq_first(&softc->bio_queue);

			xpt_action(start_ccb);
		}
		if (bp != NULL || softc->tur) {
			/* Have more work to do, so ensure we stay scheduled */
			xpt_schedule(periph, CAM_PRIORITY_NORMAL);
		}
		break;
	}
	case CD_STATE_PROBE:
	{

		rcap = (struct scsi_read_capacity_data *)malloc(sizeof(*rcap),
		    M_SCSICD, M_NOWAIT | M_ZERO);
		if (rcap == NULL) {
			xpt_print(periph->path,
			    "cdstart: Couldn't malloc read_capacity data\n");
			/* cd_free_periph??? */
			break;
		}
		csio = &start_ccb->csio;
		scsi_read_capacity(csio,
				   /*retries*/ cd_retry_count,
				   cddone,
				   MSG_SIMPLE_Q_TAG,
				   rcap,
				   SSD_FULL_SIZE,
				   /*timeout*/20000);
		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = CD_CCB_PROBE;
		xpt_action(start_ccb);
		break;
	}
	}
}

static void
cddone(struct cam_periph *periph, union ccb *done_ccb)
{ 
	struct cd_softc *softc;
	struct ccb_scsiio *csio;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("entering cddone\n"));

	softc = (struct cd_softc *)periph->softc;
	csio = &done_ccb->csio;

	switch (csio->ccb_h.ccb_state & CD_CCB_TYPE_MASK) {
	case CD_CCB_BUFFER_IO:
	{
		struct bio	*bp;
		int		error;

		bp = (struct bio *)done_ccb->ccb_h.ccb_bp;
		error = 0;

		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			int sf;

			if ((done_ccb->ccb_h.ccb_state & CD_CCB_RETRY_UA) != 0)
				sf = SF_RETRY_UA;
			else
				sf = 0;

			error = cderror(done_ccb, CAM_RETRY_SELTO, sf);
			if (error == ERESTART) {
				/*
				 * A retry was scheuled, so
				 * just return.
				 */
				return;
			}
		}

		if (error != 0) {
			xpt_print(periph->path,
			    "cddone: got error %#x back\n", error);
			bioq_flush(&softc->bio_queue, NULL, EIO);
			bp->bio_resid = bp->bio_bcount;
			bp->bio_error = error;
			bp->bio_flags |= BIO_ERROR;
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				cam_release_devq(done_ccb->ccb_h.path,
					 /*relsim_flags*/0,
					 /*reduction*/0,
					 /*timeout*/0,
					 /*getcount_only*/0);

		} else {
			bp->bio_resid = csio->resid;
			bp->bio_error = 0;
			if (bp->bio_resid != 0) {
				/*
				 * Short transfer ??? 
				 * XXX: not sure this is correct for partial
				 * transfers at EOM
				 */
				bp->bio_flags |= BIO_ERROR;
			}
		}

		LIST_REMOVE(&done_ccb->ccb_h, periph_links.le);
		softc->outstanding_cmds--;

		biofinish(bp, NULL, 0);
		break;
	}
	case CD_CCB_PROBE:
	{
		struct	   scsi_read_capacity_data *rdcap;
		char	   *announce_buf;
		struct	   cd_params *cdp;
		int error;

		cdp = &softc->params;
		announce_buf = softc->announce_temp;
		bzero(announce_buf, CD_ANNOUNCETMP_SZ);

		rdcap = (struct scsi_read_capacity_data *)csio->data_ptr;
		
		cdp->disksize = scsi_4btoul (rdcap->addr) + 1;
		cdp->blksize = scsi_4btoul (rdcap->length);

		/*
		 * Retry any UNIT ATTENTION type errors.  They
		 * are expected at boot.
		 */
		if ((csio->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP ||
		    (error = cderror(done_ccb, CAM_RETRY_SELTO,
				SF_RETRY_UA | SF_NO_PRINT)) == 0) {
			snprintf(announce_buf, CD_ANNOUNCETMP_SZ,
			    "%juMB (%ju %u byte sectors)",
			    ((uintmax_t)cdp->disksize * cdp->blksize) /
			     (1024 * 1024),
			    (uintmax_t)cdp->disksize, cdp->blksize);
		} else {
			if (error == ERESTART) {
				/*
				 * A retry was scheuled, so
				 * just return.
				 */
				return;
			} else {
				int asc, ascq;
				int sense_key, error_code;
				int have_sense;
				cam_status status;
				struct ccb_getdev cgd;

				/* Don't wedge this device's queue */
				if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
					cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);

				status = done_ccb->ccb_h.status;

				xpt_setup_ccb(&cgd.ccb_h, 
					      done_ccb->ccb_h.path,
					      CAM_PRIORITY_NORMAL);
				cgd.ccb_h.func_code = XPT_GDEV_TYPE;
				xpt_action((union ccb *)&cgd);

				if (scsi_extract_sense_ccb(done_ccb,
				    &error_code, &sense_key, &asc, &ascq))
					have_sense = TRUE;
				else
					have_sense = FALSE;

				/*
				 * Attach to anything that claims to be a
				 * CDROM or WORM device, as long as it
				 * doesn't return a "Logical unit not
				 * supported" (0x25) error.
				 */
				if ((have_sense) && (asc != 0x25)
				 && (error_code == SSD_CURRENT_ERROR
				  || error_code == SSD_DESC_CURRENT_ERROR)) {
					const char *sense_key_desc;
					const char *asc_desc;

					scsi_sense_desc(sense_key, asc, ascq,
							&cgd.inq_data,
							&sense_key_desc,
							&asc_desc);
					snprintf(announce_buf,
					    CD_ANNOUNCETMP_SZ,
						"Attempt to query device "
						"size failed: %s, %s",
						sense_key_desc,
						asc_desc);
 				} else if ((have_sense == 0) 
 				      && ((status & CAM_STATUS_MASK) ==
 					   CAM_SCSI_STATUS_ERROR)
 				      && (csio->scsi_status ==
 					  SCSI_STATUS_BUSY)) {
 					snprintf(announce_buf,
					    CD_ANNOUNCETMP_SZ,
 					    "Attempt to query device "
 					    "size failed: SCSI Status: %s",
					    scsi_status_string(csio));
				} else if (SID_TYPE(&cgd.inq_data) == T_CDROM) {
					/*
					 * We only print out an error for
					 * CDROM type devices.  For WORM
					 * devices, we don't print out an
					 * error since a few WORM devices
					 * don't support CDROM commands.
					 * If we have sense information, go
					 * ahead and print it out.
					 * Otherwise, just say that we 
					 * couldn't attach.
					 */

					/*
					 * Just print out the error, not
					 * the full probe message, when we
					 * don't attach.
					 */
					if (have_sense)
						scsi_sense_print(
							&done_ccb->csio);
					else {
						xpt_print(periph->path,
						    "got CAM status %#x\n",
						    done_ccb->ccb_h.status);
					}
					xpt_print(periph->path, "fatal error, "
					    "failed to attach to device\n");
					/*
					 * Invalidate this peripheral.
					 */
					cam_periph_invalidate(periph);

					announce_buf = NULL;
				} else {

					/*
					 * Invalidate this peripheral.
					 */
					cam_periph_invalidate(periph);
					announce_buf = NULL;
				}
			}
		}
		free(rdcap, M_SCSICD);
		if (announce_buf != NULL) {
			struct sbuf sb;

			sbuf_new(&sb, softc->announce_buf, CD_ANNOUNCE_SZ,
			    SBUF_FIXEDLEN);
			xpt_announce_periph_sbuf(periph, &sb, announce_buf);
			xpt_announce_quirks_sbuf(periph, &sb, softc->quirks,
			    CD_Q_BIT_STRING);
			sbuf_finish(&sb);
			sbuf_putbuf(&sb);

			/*
			 * Create our sysctl variables, now that we know
			 * we have successfully attached.
			 */
			taskqueue_enqueue(taskqueue_thread,&softc->sysctl_task);
		}
		softc->state = CD_STATE_NORMAL;		
		/*
		 * Since our peripheral may be invalidated by an error
		 * above or an external event, we must release our CCB
		 * before releasing the probe lock on the peripheral.
		 * The peripheral will only go away once the last lock
		 * is removed, and we need it around for the CCB release
		 * operation.
		 */
		xpt_release_ccb(done_ccb);
		cam_periph_unhold(periph);
		return;
	}
	case CD_CCB_TUR:
	{
		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {

			if (cderror(done_ccb, CAM_RETRY_SELTO,
			    SF_RETRY_UA | SF_NO_RECOVERY | SF_NO_PRINT) ==
			    ERESTART)
				return;
			if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
				cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);
		}
		xpt_release_ccb(done_ccb);
		cam_periph_release_locked(periph);
		return;
	}
	default:
		break;
	}
	xpt_release_ccb(done_ccb);
}

static union cd_pages *
cdgetpage(struct cd_mode_params *mode_params)
{
	union cd_pages *page;

	if (mode_params->cdb_size == 10)
		page = (union cd_pages *)find_mode_page_10(
			(struct scsi_mode_header_10 *)mode_params->mode_buf);
	else
		page = (union cd_pages *)find_mode_page_6(
			(struct scsi_mode_header_6 *)mode_params->mode_buf);

	return (page);
}

static int
cdgetpagesize(int page_num)
{
	u_int i;

	for (i = 0; i < nitems(cd_page_size_table); i++) {
		if (cd_page_size_table[i].page == page_num)
			return (cd_page_size_table[i].page_size);
	}

	return (-1);
}

static struct cd_toc_entry *
te_data_get_ptr(void *irtep, u_long cmd)
{
	union {
		struct ioc_read_toc_entry irte;
#ifdef COMPAT_FREEBSD32
		struct ioc_read_toc_entry32 irte32;
#endif
	} *irteup;

	irteup = irtep;
	switch (IOCPARM_LEN(cmd)) {
	case sizeof(irteup->irte):
		return (irteup->irte.data);
#ifdef COMPAT_FREEBSD32
	case sizeof(irteup->irte32):
		return ((struct cd_toc_entry *)(uintptr_t)irteup->irte32.data);
#endif
	default:
		panic("Unhandled ioctl command %ld", cmd);
	}
}

static int
cdioctl(struct disk *dp, u_long cmd, void *addr, int flag, struct thread *td)
{

	struct 	cam_periph *periph;
	struct	cd_softc *softc;
	int	nocopyout, error = 0;

	periph = (struct cam_periph *)dp->d_drv1;
	cam_periph_lock(periph);

	softc = (struct cd_softc *)periph->softc;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE,
	    ("cdioctl(%#lx)\n", cmd));

	if ((error = cam_periph_hold(periph, PRIBIO | PCATCH)) != 0) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (error);
	}

	/*
	 * If we don't have media loaded, check for it.  If still don't
	 * have media loaded, we can only do a load or eject.
	 *
	 * We only care whether media is loaded if this is a cd-specific ioctl
	 * (thus the IOCGROUP check below).  Note that this will break if
	 * anyone adds any ioctls into the switch statement below that don't
	 * have their ioctl group set to 'c'.
	 */
	if (((softc->flags & CD_FLAG_VALID_MEDIA) == 0)
	 && ((cmd != CDIOCCLOSE)
	  && (cmd != CDIOCEJECT))
	 && (IOCGROUP(cmd) == 'c')) {
		error = cdcheckmedia(periph);
		if (error != 0) {
			cam_periph_unhold(periph);
			cam_periph_unlock(periph);
			return (error);
		}
	}
	/*
	 * Drop the lock here so later mallocs can use WAITOK.  The periph
	 * is essentially locked still with the cam_periph_hold call above.
	 */
	cam_periph_unlock(periph);

	nocopyout = 0;
	switch (cmd) {

	case CDIOCPLAYTRACKS:
		{
			struct ioc_play_track *args
			    = (struct ioc_play_track *) addr;
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCPLAYTRACKS\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.flags &= ~CD_PA_SOTC;
			page->audio.flags |= CD_PA_IMMED;
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			if (error) {
				cam_periph_unlock(periph);
				break;
			}

			/*
			 * This was originally implemented with the PLAY
			 * AUDIO TRACK INDEX command, but that command was
			 * deprecated after SCSI-2.  Most (all?) SCSI CDROM
			 * drives support it but ATAPI and ATAPI-derivative
			 * drives don't seem to support it.  So we keep a
			 * cache of the table of contents and translate
			 * track numbers to MSF format.
			 */
			if (softc->flags & CD_FLAG_VALID_TOC) {
				union msf_lba *sentry, *eentry;
				int st, et;

				if (args->end_track <
				    softc->toc.header.ending_track + 1)
					args->end_track++;
				if (args->end_track >
				    softc->toc.header.ending_track + 1)
					args->end_track =
					    softc->toc.header.ending_track + 1;
				st = args->start_track -
					softc->toc.header.starting_track;
				et = args->end_track -
					softc->toc.header.starting_track;
				if ((st < 0)
				 || (et < 0)
			 	 || (st > (softc->toc.header.ending_track -
				     softc->toc.header.starting_track))) {
					error = EINVAL;
					cam_periph_unlock(periph);
					break;
				}
				sentry = &softc->toc.entries[st].addr;
				eentry = &softc->toc.entries[et].addr;
				error = cdplaymsf(periph,
						  sentry->msf.minute,
						  sentry->msf.second,
						  sentry->msf.frame,
						  eentry->msf.minute,
						  eentry->msf.second,
						  eentry->msf.frame);
			} else {
				/*
				 * If we don't have a valid TOC, try the
				 * play track index command.  It is part of
				 * the SCSI-2 spec, but was removed in the
				 * MMC specs.  ATAPI and ATAPI-derived
				 * drives don't support it.
				 */
				if (softc->quirks & CD_Q_BCD_TRACKS) {
					args->start_track =
						bin2bcd(args->start_track);
					args->end_track =
						bin2bcd(args->end_track);
				}
				error = cdplaytracks(periph,
						     args->start_track,
						     args->start_index,
						     args->end_track,
						     args->end_index);
			}
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCPLAYMSF:
		{
			struct ioc_play_msf *args
				= (struct ioc_play_msf *) addr;
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCPLAYMSF\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.flags &= ~CD_PA_SOTC;
			page->audio.flags |= CD_PA_IMMED;
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			if (error) {
				cam_periph_unlock(periph);
				break;
			}
			error = cdplaymsf(periph,
					  args->start_m,
					  args->start_s,
					  args->start_f,
					  args->end_m,
					  args->end_s,
					  args->end_f);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCPLAYBLOCKS:
		{
			struct ioc_play_blocks *args
				= (struct ioc_play_blocks *) addr;
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCPLAYBLOCKS\n"));


			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.flags &= ~CD_PA_SOTC;
			page->audio.flags |= CD_PA_IMMED;
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			if (error) {
				cam_periph_unlock(periph);
				break;
			}
			error = cdplay(periph, args->blk, args->len);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCREADSUBCHANNEL_SYSSPACE:
		nocopyout = 1;
		/* Fallthrough */
	case CDIOCREADSUBCHANNEL:
		{
			struct ioc_read_subchannel *args
				= (struct ioc_read_subchannel *) addr;
			struct cd_sub_channel_info *data;
			u_int32_t len = args->data_len;

			data = malloc(sizeof(struct cd_sub_channel_info), 
				      M_SCSICD, M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCREADSUBCHANNEL\n"));

			if ((len > sizeof(struct cd_sub_channel_info)) ||
			    (len < sizeof(struct cd_sub_channel_header))) {
				printf(
					"scsi_cd: cdioctl: "
					"cdioreadsubchannel: error, len=%d\n",
					len);
				error = EINVAL;
				free(data, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}

			if (softc->quirks & CD_Q_BCD_TRACKS)
				args->track = bin2bcd(args->track);

			error = cdreadsubchannel(periph, args->address_format,
				args->data_format, args->track, data, len);

			if (error) {
				free(data, M_SCSICD);
				cam_periph_unlock(periph);
	 			break;
			}
			if (softc->quirks & CD_Q_BCD_TRACKS)
				data->what.track_info.track_number =
				    bcd2bin(data->what.track_info.track_number);
			len = min(len, ((data->header.data_len[0] << 8) +
				data->header.data_len[1] +
				sizeof(struct cd_sub_channel_header)));
			cam_periph_unlock(periph);
			if (nocopyout == 0) {
				if (copyout(data, args->data, len) != 0) {
					error = EFAULT;
				}
			} else {
				bcopy(data, args->data, len);
			}
			free(data, M_SCSICD);
		}
		break;

	case CDIOREADTOCHEADER:
		{
			struct ioc_toc_header *th;

			th = malloc(sizeof(struct ioc_toc_header), M_SCSICD,
				    M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOREADTOCHEADER\n"));

			error = cdreadtoc(periph, 0, 0, (u_int8_t *)th, 
				          sizeof (*th), /*sense_flags*/SF_NO_PRINT);
			if (error) {
				free(th, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			if (softc->quirks & CD_Q_BCD_TRACKS) {
				/* we are going to have to convert the BCD
				 * encoding on the cd to what is expected
				 */
				th->starting_track = 
					bcd2bin(th->starting_track);
				th->ending_track = bcd2bin(th->ending_track);
			}
			th->len = ntohs(th->len);
			bcopy(th, addr, sizeof(*th));
			free(th, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOREADTOCENTRYS:
#ifdef COMPAT_FREEBSD32
	case CDIOREADTOCENTRYS_32:
#endif
		{
			struct cd_tocdata *data;
			struct cd_toc_single *lead;
			struct ioc_read_toc_entry *te =
				(struct ioc_read_toc_entry *) addr;
			struct ioc_toc_header *th;
			u_int32_t len, readlen, idx, num;
			u_int32_t starting_track = te->starting_track;

			data = malloc(sizeof(*data), M_SCSICD, M_WAITOK | M_ZERO);
			lead = malloc(sizeof(*lead), M_SCSICD, M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOREADTOCENTRYS\n"));

			if (te->data_len < sizeof(struct cd_toc_entry)
			 || (te->data_len % sizeof(struct cd_toc_entry)) != 0
			 || (te->address_format != CD_MSF_FORMAT
			  && te->address_format != CD_LBA_FORMAT)) {
				error = EINVAL;
				printf("scsi_cd: error in readtocentries, "
				       "returning EINVAL\n");
				free(data, M_SCSICD);
				free(lead, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}

			th = &data->header;
			error = cdreadtoc(periph, 0, 0, (u_int8_t *)th, 
					  sizeof (*th), /*sense_flags*/0);
			if (error) {
				free(data, M_SCSICD);
				free(lead, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}

			if (softc->quirks & CD_Q_BCD_TRACKS) {
				/* we are going to have to convert the BCD
				 * encoding on the cd to what is expected
				 */
				th->starting_track =
				    bcd2bin(th->starting_track);
				th->ending_track = bcd2bin(th->ending_track);
			}

			if (starting_track == 0)
				starting_track = th->starting_track;
			else if (starting_track == LEADOUT)
				starting_track = th->ending_track + 1;
			else if (starting_track < th->starting_track ||
				 starting_track > th->ending_track + 1) {
				printf("scsi_cd: error in readtocentries, "
				       "returning EINVAL\n");
				free(data, M_SCSICD);
				free(lead, M_SCSICD);
				cam_periph_unlock(periph);
				error = EINVAL;
				break;
			}

			/* calculate reading length without leadout entry */
			readlen = (th->ending_track - starting_track + 1) *
				  sizeof(struct cd_toc_entry);

			/* and with leadout entry */
			len = readlen + sizeof(struct cd_toc_entry);
			if (te->data_len < len) {
				len = te->data_len;
				if (readlen > len)
					readlen = len;
			}
			if (len > sizeof(data->entries)) {
				printf("scsi_cd: error in readtocentries, "
				       "returning EINVAL\n");
				error = EINVAL;
				free(data, M_SCSICD);
				free(lead, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			num = len / sizeof(struct cd_toc_entry);

			if (readlen > 0) {
				error = cdreadtoc(periph, te->address_format,
						  starting_track,
						  (u_int8_t *)data,
						  readlen + sizeof (*th),
						  /*sense_flags*/0);
				if (error) {
					free(data, M_SCSICD);
					free(lead, M_SCSICD);
					cam_periph_unlock(periph);
					break;
				}
			}

			/* make leadout entry if needed */
			idx = starting_track + num - 1;
			if (softc->quirks & CD_Q_BCD_TRACKS)
				th->ending_track = bcd2bin(th->ending_track);
			if (idx == th->ending_track + 1) {
				error = cdreadtoc(periph, te->address_format,
						  LEADOUT, (u_int8_t *)lead,
						  sizeof(*lead),
						  /*sense_flags*/0);
				if (error) {
					free(data, M_SCSICD);
					free(lead, M_SCSICD);
					cam_periph_unlock(periph);
					break;
				}
				data->entries[idx - starting_track] = 
					lead->entry;
			}
			if (softc->quirks & CD_Q_BCD_TRACKS) {
				for (idx = 0; idx < num - 1; idx++) {
					data->entries[idx].track =
					    bcd2bin(data->entries[idx].track);
				}
			}

			cam_periph_unlock(periph);
			error = copyout(data->entries, te_data_get_ptr(te, cmd),
			    len);
			free(data, M_SCSICD);
			free(lead, M_SCSICD);
		}
		break;
	case CDIOREADTOCENTRY:
		{
			struct cd_toc_single *data;
			struct ioc_read_toc_single_entry *te =
				(struct ioc_read_toc_single_entry *) addr;
			struct ioc_toc_header *th;
			u_int32_t track;

			data = malloc(sizeof(*data), M_SCSICD, M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOREADTOCENTRY\n"));

			if (te->address_format != CD_MSF_FORMAT
			    && te->address_format != CD_LBA_FORMAT) {
				printf("error in readtocentry, "
				       " returning EINVAL\n");
				free(data, M_SCSICD);
				error = EINVAL;
				cam_periph_unlock(periph);
				break;
			}

			th = &data->header;
			error = cdreadtoc(periph, 0, 0, (u_int8_t *)th,
					  sizeof (*th), /*sense_flags*/0);
			if (error) {
				free(data, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}

			if (softc->quirks & CD_Q_BCD_TRACKS) {
				/* we are going to have to convert the BCD
				 * encoding on the cd to what is expected
				 */
				th->starting_track =
				    bcd2bin(th->starting_track);
				th->ending_track = bcd2bin(th->ending_track);
			}
			track = te->track;
			if (track == 0)
				track = th->starting_track;
			else if (track == LEADOUT)
				/* OK */;
			else if (track < th->starting_track ||
				 track > th->ending_track + 1) {
				printf("error in readtocentry, "
				       " returning EINVAL\n");
				free(data, M_SCSICD);
				error = EINVAL;
				cam_periph_unlock(periph);
				break;
			}

			error = cdreadtoc(periph, te->address_format, track,
					  (u_int8_t *)data, sizeof(*data),
					  /*sense_flags*/0);
			if (error) {
				free(data, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}

			if (softc->quirks & CD_Q_BCD_TRACKS)
				data->entry.track = bcd2bin(data->entry.track);
			bcopy(&data->entry, &te->entry,
			      sizeof(struct cd_toc_entry));
			free(data, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCSETPATCH:
		{
			struct ioc_patch *arg = (struct ioc_patch *)addr;
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD, 
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCSETPATCH\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.port[LEFT_PORT].channels = 
				arg->patch[0];
			page->audio.port[RIGHT_PORT].channels = 
				arg->patch[1];
			page->audio.port[2].channels = arg->patch[2];
			page->audio.port[3].channels = arg->patch[3];
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCGETVOL:
		{
			struct ioc_vol *arg = (struct ioc_vol *) addr;
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD, 
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCGETVOL\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			arg->vol[LEFT_PORT] = 
				page->audio.port[LEFT_PORT].volume;
			arg->vol[RIGHT_PORT] = 
				page->audio.port[RIGHT_PORT].volume;
			arg->vol[2] = page->audio.port[2].volume;
			arg->vol[3] = page->audio.port[3].volume;
			free(params.mode_buf, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCSETVOL:
		{
			struct ioc_vol *arg = (struct ioc_vol *) addr;
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD, 
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCSETVOL\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.port[LEFT_PORT].channels = CHANNEL_0;
			page->audio.port[LEFT_PORT].volume = 
				arg->vol[LEFT_PORT];
			page->audio.port[RIGHT_PORT].channels = CHANNEL_1;
			page->audio.port[RIGHT_PORT].volume = 
				arg->vol[RIGHT_PORT];
			page->audio.port[2].volume = arg->vol[2];
			page->audio.port[3].volume = arg->vol[3];
			error = cdsetmode(periph, &params);
			cam_periph_unlock(periph);
			free(params.mode_buf, M_SCSICD);
		}
		break;
	case CDIOCSETMONO:
		{
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCSETMONO\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.port[LEFT_PORT].channels = 
				LEFT_CHANNEL | RIGHT_CHANNEL;
			page->audio.port[RIGHT_PORT].channels = 
				LEFT_CHANNEL | RIGHT_CHANNEL;
			page->audio.port[2].channels = 0;
			page->audio.port[3].channels = 0;
			error = cdsetmode(periph, &params);
			cam_periph_unlock(periph);
			free(params.mode_buf, M_SCSICD);
		}
		break;
	case CDIOCSETSTEREO:
		{
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCSETSTEREO\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.port[LEFT_PORT].channels = 
				LEFT_CHANNEL;
			page->audio.port[RIGHT_PORT].channels = 
				RIGHT_CHANNEL;
			page->audio.port[2].channels = 0;
			page->audio.port[3].channels = 0;
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCSETMUTE:
		{
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCSETMUTE\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.port[LEFT_PORT].channels = 0;
			page->audio.port[RIGHT_PORT].channels = 0;
			page->audio.port[2].channels = 0;
			page->audio.port[3].channels = 0;
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCSETLEFT:
		{
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCSETLEFT\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.port[LEFT_PORT].channels = LEFT_CHANNEL;
			page->audio.port[RIGHT_PORT].channels = LEFT_CHANNEL;
			page->audio.port[2].channels = 0;
			page->audio.port[3].channels = 0;
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCSETRIGHT:
		{
			struct cd_mode_params params;
			union cd_pages *page;

			params.alloc_len = sizeof(union cd_mode_data_6_10);
			params.mode_buf = malloc(params.alloc_len, M_SCSICD,
						 M_WAITOK | M_ZERO);

			cam_periph_lock(periph);
			CAM_DEBUG(periph->path, CAM_DEBUG_SUBTRACE, 
				  ("trying to do CDIOCSETRIGHT\n"));

			error = cdgetmode(periph, &params, AUDIO_PAGE);
			if (error) {
				free(params.mode_buf, M_SCSICD);
				cam_periph_unlock(periph);
				break;
			}
			page = cdgetpage(&params);

			page->audio.port[LEFT_PORT].channels = RIGHT_CHANNEL;
			page->audio.port[RIGHT_PORT].channels = RIGHT_CHANNEL;
			page->audio.port[2].channels = 0;
			page->audio.port[3].channels = 0;
			error = cdsetmode(periph, &params);
			free(params.mode_buf, M_SCSICD);
			cam_periph_unlock(periph);
		}
		break;
	case CDIOCRESUME:
		cam_periph_lock(periph);
		error = cdpause(periph, 1);
		cam_periph_unlock(periph);
		break;
	case CDIOCPAUSE:
		cam_periph_lock(periph);
		error = cdpause(periph, 0);
		cam_periph_unlock(periph);
		break;
	case CDIOCSTART:
		cam_periph_lock(periph);
		error = cdstartunit(periph, 0);
		cam_periph_unlock(periph);
		break;
	case CDIOCCLOSE:
		cam_periph_lock(periph);
		error = cdstartunit(periph, 1);
		cam_periph_unlock(periph);
		break;
	case CDIOCSTOP:
		cam_periph_lock(periph);
		error = cdstopunit(periph, 0);
		cam_periph_unlock(periph);
		break;
	case CDIOCEJECT:
		cam_periph_lock(periph);
		error = cdstopunit(periph, 1);
		cam_periph_unlock(periph);
		break;
	case CDIOCALLOW:
		cam_periph_lock(periph);
		cdprevent(periph, PR_ALLOW);
		cam_periph_unlock(periph);
		break;
	case CDIOCPREVENT:
		cam_periph_lock(periph);
		cdprevent(periph, PR_PREVENT);
		cam_periph_unlock(periph);
		break;
	case CDIOCSETDEBUG:
		/* sc_link->flags |= (SDEV_DB1 | SDEV_DB2); */
		error = ENOTTY;
		break;
	case CDIOCCLRDEBUG:
		/* sc_link->flags &= ~(SDEV_DB1 | SDEV_DB2); */
		error = ENOTTY;
		break;
	case CDIOCRESET:
		/* return (cd_reset(periph)); */
		error = ENOTTY;
		break;
	case CDRIOCREADSPEED:
		cam_periph_lock(periph);
		error = cdsetspeed(periph, *(u_int32_t *)addr, CDR_MAX_SPEED);
		cam_periph_unlock(periph);
		break;
	case CDRIOCWRITESPEED:
		cam_periph_lock(periph);
		error = cdsetspeed(periph, CDR_MAX_SPEED, *(u_int32_t *)addr);
		cam_periph_unlock(periph);
		break;
	case CDRIOCGETBLOCKSIZE:
		*(int *)addr = softc->params.blksize;
		break;
	case CDRIOCSETBLOCKSIZE:
		if (*(int *)addr <= 0) {
			error = EINVAL;
			break;
		}
		softc->disk->d_sectorsize = softc->params.blksize = *(int *)addr;
		break;
	case DVDIOCSENDKEY:
	case DVDIOCREPORTKEY: {
		struct dvd_authinfo *authinfo;

		authinfo = (struct dvd_authinfo *)addr;

		if (cmd == DVDIOCREPORTKEY)
			error = cdreportkey(periph, authinfo);
		else
			error = cdsendkey(periph, authinfo);
		break;
		}
	case DVDIOCREADSTRUCTURE: {
		struct dvd_struct *dvdstruct;

		dvdstruct = (struct dvd_struct *)addr;

		error = cdreaddvdstructure(periph, dvdstruct);

		break;
	}
	default:
		cam_periph_lock(periph);
		error = cam_periph_ioctl(periph, cmd, addr, cderror);
		cam_periph_unlock(periph);
		break;
	}

	cam_periph_lock(periph);
	cam_periph_unhold(periph);
	
	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("leaving cdioctl\n"));
	if (error && bootverbose) {
		printf("scsi_cd.c::ioctl cmd=%08lx error=%d\n", cmd, error);
	}
	cam_periph_unlock(periph);

	return (error);
}

static void
cdprevent(struct cam_periph *periph, int action)
{
	union	ccb *ccb;
	struct	cd_softc *softc;
	int	error;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("entering cdprevent\n"));

	softc = (struct cd_softc *)periph->softc;
	
	if (((action == PR_ALLOW)
	  && (softc->flags & CD_FLAG_DISC_LOCKED) == 0)
	 || ((action == PR_PREVENT)
	  && (softc->flags & CD_FLAG_DISC_LOCKED) != 0)) {
		return;
	}
	    
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_prevent(&ccb->csio, 
		     /*retries*/ cd_retry_count,
		     /*cbfcnp*/NULL,
		     MSG_SIMPLE_Q_TAG,
		     action,
		     SSD_FULL_SIZE,
		     /* timeout */60000);
	
	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			/*sense_flags*/SF_RETRY_UA|SF_NO_PRINT);

	xpt_release_ccb(ccb);

	if (error == 0) {
		if (action == PR_ALLOW)
			softc->flags &= ~CD_FLAG_DISC_LOCKED;
		else
			softc->flags |= CD_FLAG_DISC_LOCKED;
	}
}

/*
 * XXX: the disk media and sector size is only really able to change
 * XXX: while the device is closed.
 */
static int
cdcheckmedia(struct cam_periph *periph)
{
	struct cd_softc *softc;
	struct ioc_toc_header *toch;
	struct cd_toc_single leadout;
	u_int32_t size, toclen;
	int error, num_entries, cdindex;

	softc = (struct cd_softc *)periph->softc;

	cdprevent(periph, PR_PREVENT);
	softc->disk->d_sectorsize = 2048;
	softc->disk->d_mediasize = 0;

	/*
	 * Get the disc size and block size.  If we can't get it, we don't
	 * have media, most likely.
	 */
	if ((error = cdsize(periph, &size)) != 0) {
		softc->flags &= ~(CD_FLAG_VALID_MEDIA|CD_FLAG_VALID_TOC);
		cdprevent(periph, PR_ALLOW);
		return (error);
	} else {
		softc->flags |= CD_FLAG_SAW_MEDIA | CD_FLAG_VALID_MEDIA;
		softc->disk->d_sectorsize = softc->params.blksize;
		softc->disk->d_mediasize =
		    (off_t)softc->params.blksize * softc->params.disksize;
	}

	/*
	 * Now we check the table of contents.  This (currently) is only
	 * used for the CDIOCPLAYTRACKS ioctl.  It may be used later to do
	 * things like present a separate entry in /dev for each track,
	 * like that acd(4) driver does.
	 */
	bzero(&softc->toc, sizeof(softc->toc));
	toch = &softc->toc.header;
	/*
	 * We will get errors here for media that doesn't have a table of
	 * contents.  According to the MMC-3 spec: "When a Read TOC/PMA/ATIP
	 * command is presented for a DDCD/CD-R/RW media, where the first TOC
	 * has not been recorded (no complete session) and the Format codes
	 * 0000b, 0001b, or 0010b are specified, this command shall be rejected
	 * with an INVALID FIELD IN CDB.  Devices that are not capable of
	 * reading an incomplete session on DDC/CD-R/RW media shall report
	 * CANNOT READ MEDIUM - INCOMPATIBLE FORMAT."
	 *
	 * So this isn't fatal if we can't read the table of contents, it
	 * just means that the user won't be able to issue the play tracks
	 * ioctl, and likely lots of other stuff won't work either.  They
	 * need to burn the CD before we can do a whole lot with it.  So
	 * we don't print anything here if we get an error back.
	 */
	error = cdreadtoc(periph, 0, 0, (u_int8_t *)toch, sizeof(*toch),
			  SF_NO_PRINT);
	/*
	 * Errors in reading the table of contents aren't fatal, we just
	 * won't have a valid table of contents cached.
	 */
	if (error != 0) {
		error = 0;
		bzero(&softc->toc, sizeof(softc->toc));
		goto bailout;
	}

	if (softc->quirks & CD_Q_BCD_TRACKS) {
		toch->starting_track = bcd2bin(toch->starting_track);
		toch->ending_track = bcd2bin(toch->ending_track);
	}

	/* Number of TOC entries, plus leadout */
	num_entries = (toch->ending_track - toch->starting_track) + 2;

	if (num_entries <= 0)
		goto bailout;

	toclen = num_entries * sizeof(struct cd_toc_entry);

	error = cdreadtoc(periph, CD_MSF_FORMAT, toch->starting_track,
			  (u_int8_t *)&softc->toc, toclen + sizeof(*toch),
			  SF_NO_PRINT);
	if (error != 0) {
		error = 0;
		bzero(&softc->toc, sizeof(softc->toc));
		goto bailout;
	}

	if (softc->quirks & CD_Q_BCD_TRACKS) {
		toch->starting_track = bcd2bin(toch->starting_track);
		toch->ending_track = bcd2bin(toch->ending_track);
	}
	/*
	 * XXX KDM is this necessary?  Probably only if the drive doesn't
	 * return leadout information with the table of contents.
	 */
	cdindex = toch->starting_track + num_entries -1;
	if (cdindex == toch->ending_track + 1) {

		error = cdreadtoc(periph, CD_MSF_FORMAT, LEADOUT, 
				  (u_int8_t *)&leadout, sizeof(leadout),
				  SF_NO_PRINT);
		if (error != 0) {
			error = 0;
			goto bailout;
		}
		softc->toc.entries[cdindex - toch->starting_track] =
			leadout.entry;
	}
	if (softc->quirks & CD_Q_BCD_TRACKS) {
		for (cdindex = 0; cdindex < num_entries - 1; cdindex++) {
			softc->toc.entries[cdindex].track =
				bcd2bin(softc->toc.entries[cdindex].track);
		}
	}

	softc->flags |= CD_FLAG_VALID_TOC;

	/* If the first track is audio, correct sector size. */
	if ((softc->toc.entries[0].control & 4) == 0) {
		softc->disk->d_sectorsize = softc->params.blksize = 2352;
		softc->disk->d_mediasize =
		    (off_t)softc->params.blksize * softc->params.disksize;
	}

bailout:

	/*
	 * We unconditionally (re)set the blocksize each time the
	 * CD device is opened.  This is because the CD can change,
	 * and therefore the blocksize might change.
	 * XXX problems here if some slice or partition is still
	 * open with the old size?
	 */
	if ((softc->disk->d_devstat->flags & DEVSTAT_BS_UNAVAILABLE) != 0)
		softc->disk->d_devstat->flags &= ~DEVSTAT_BS_UNAVAILABLE;
	softc->disk->d_devstat->block_size = softc->params.blksize;

	return (error);
}

static int
cdsize(struct cam_periph *periph, u_int32_t *size)
{
	struct cd_softc *softc;
	union ccb *ccb;
	struct scsi_read_capacity_data *rcap_buf;
	int error;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("entering cdsize\n"));

	softc = (struct cd_softc *)periph->softc;
             
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	/* XXX Should be M_WAITOK */
	rcap_buf = malloc(sizeof(struct scsi_read_capacity_data), 
			  M_SCSICD, M_NOWAIT | M_ZERO);
	if (rcap_buf == NULL)
		return (ENOMEM);

	scsi_read_capacity(&ccb->csio, 
			   /*retries*/ cd_retry_count,
			   /*cbfcnp*/NULL,
			   MSG_SIMPLE_Q_TAG,
			   rcap_buf,
			   SSD_FULL_SIZE,
			   /* timeout */20000);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA|SF_NO_PRINT);

	xpt_release_ccb(ccb);

	softc->params.disksize = scsi_4btoul(rcap_buf->addr) + 1;
	softc->params.blksize  = scsi_4btoul(rcap_buf->length);
	/* Make sure we got at least some block size. */
	if (error == 0 && softc->params.blksize == 0)
		error = EIO;
	/*
	 * SCSI-3 mandates that the reported blocksize shall be 2048.
	 * Older drives sometimes report funny values, trim it down to
	 * 2048, or other parts of the kernel will get confused.
	 *
	 * XXX we leave drives alone that might report 512 bytes, as
	 * well as drives reporting more weird sizes like perhaps 4K.
	 */
	if (softc->params.blksize > 2048 && softc->params.blksize <= 2352)
		softc->params.blksize = 2048;

	free(rcap_buf, M_SCSICD);
	*size = softc->params.disksize;

	return (error);

}

static int
cd6byteworkaround(union ccb *ccb)
{
	u_int8_t *cdb;
	struct cam_periph *periph;
	struct cd_softc *softc;
	struct cd_mode_params *params;
	int frozen, found;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct cd_softc *)periph->softc;

	cdb = ccb->csio.cdb_io.cdb_bytes;

	if ((ccb->ccb_h.flags & CAM_CDB_POINTER)
	 || ((cdb[0] != MODE_SENSE_6)
	  && (cdb[0] != MODE_SELECT_6)))
		return (0);

	/*
	 * Because there is no convenient place to stash the overall
	 * cd_mode_params structure pointer, we have to grab it like this.
	 * This means that ALL MODE_SENSE and MODE_SELECT requests in the
	 * cd(4) driver MUST go through cdgetmode() and cdsetmode()!
	 *
	 * XXX It would be nice if, at some point, we could increase the
	 * number of available peripheral private pointers.  Both pointers
	 * are currently used in most every peripheral driver.
	 */
	found = 0;

	STAILQ_FOREACH(params, &softc->mode_queue, links) {
		if (params->mode_buf == ccb->csio.data_ptr) {
			found = 1;
			break;
		}
	}

	/*
	 * This shouldn't happen.  All mode sense and mode select
	 * operations in the cd(4) driver MUST go through cdgetmode() and
	 * cdsetmode()!
	 */
	if (found == 0) {
		xpt_print(periph->path,
		    "mode buffer not found in mode queue!\n");
		return (0);
	}

	params->cdb_size = 10;
	softc->minimum_command_size = 10;
	xpt_print(ccb->ccb_h.path,
	    "%s(6) failed, increasing minimum CDB size to 10 bytes\n",
	    (cdb[0] == MODE_SENSE_6) ? "MODE_SENSE" : "MODE_SELECT");

	if (cdb[0] == MODE_SENSE_6) {
		struct scsi_mode_sense_10 ms10;
		struct scsi_mode_sense_6 *ms6;
		int len;

		ms6 = (struct scsi_mode_sense_6 *)cdb;

		bzero(&ms10, sizeof(ms10));
 		ms10.opcode = MODE_SENSE_10;
 		ms10.byte2 = ms6->byte2;
 		ms10.page = ms6->page;

		/*
		 * 10 byte mode header, block descriptor,
		 * sizeof(union cd_pages)
		 */
		len = sizeof(struct cd_mode_data_10);
		ccb->csio.dxfer_len = len;

		scsi_ulto2b(len, ms10.length);
		ms10.control = ms6->control;
		bcopy(&ms10, cdb, 10);
		ccb->csio.cdb_len = 10;
	} else {
		struct scsi_mode_select_10 ms10;
		struct scsi_mode_select_6 *ms6;
		struct scsi_mode_header_6 *header6;
		struct scsi_mode_header_10 *header10;
		struct scsi_mode_page_header *page_header;
		int blk_desc_len, page_num, page_size, len;

		ms6 = (struct scsi_mode_select_6 *)cdb;

		bzero(&ms10, sizeof(ms10));
		ms10.opcode = MODE_SELECT_10;
		ms10.byte2 = ms6->byte2;

		header6 = (struct scsi_mode_header_6 *)params->mode_buf;
		header10 = (struct scsi_mode_header_10 *)params->mode_buf;

		page_header = find_mode_page_6(header6);
		page_num = page_header->page_code;

		blk_desc_len = header6->blk_desc_len;

		page_size = cdgetpagesize(page_num);

		if (page_size != (page_header->page_length +
		    sizeof(*page_header)))
			page_size = page_header->page_length +
				sizeof(*page_header);

		len = sizeof(*header10) + blk_desc_len + page_size;

		len = min(params->alloc_len, len);

		/*
		 * Since the 6 byte parameter header is shorter than the 10
		 * byte parameter header, we need to copy the actual mode
		 * page data, and the block descriptor, if any, so things wind
		 * up in the right place.  The regions will overlap, but
		 * bcopy() does the right thing.
		 */
		bcopy(params->mode_buf + sizeof(*header6),
		      params->mode_buf + sizeof(*header10),
		      len - sizeof(*header10));

		/* Make sure these fields are set correctly. */
		scsi_ulto2b(0, header10->data_length);
		header10->medium_type = 0;
		scsi_ulto2b(blk_desc_len, header10->blk_desc_len);

		ccb->csio.dxfer_len = len;

		scsi_ulto2b(len, ms10.length);
		ms10.control = ms6->control;
		bcopy(&ms10, cdb, 10);
		ccb->csio.cdb_len = 10;
	}

	frozen = (ccb->ccb_h.status & CAM_DEV_QFRZN) != 0;
	ccb->ccb_h.status = CAM_REQUEUE_REQ;
	xpt_action(ccb);
	if (frozen) {
		cam_release_devq(ccb->ccb_h.path,
				 /*relsim_flags*/0,
				 /*openings*/0,
				 /*timeout*/0,
				 /*getcount_only*/0);
	}

	return (ERESTART);
}

static int
cderror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
	struct cd_softc *softc;
	struct cam_periph *periph;
	int error, error_code, sense_key, asc, ascq;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct cd_softc *)periph->softc;

	error = 0;

	/*
	 * We use a status of CAM_REQ_INVALID as shorthand -- if a 6 byte
	 * CDB comes back with this particular error, try transforming it
	 * into the 10 byte version.
	 */
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_INVALID) {
		error = cd6byteworkaround(ccb);
	} else if (scsi_extract_sense_ccb(ccb,
	    &error_code, &sense_key, &asc, &ascq)) {
		if (sense_key == SSD_KEY_ILLEGAL_REQUEST)
			error = cd6byteworkaround(ccb);
		else if (sense_key == SSD_KEY_UNIT_ATTENTION &&
		    asc == 0x28 && ascq == 0x00)
			disk_media_changed(softc->disk, M_NOWAIT);
		else if (sense_key == SSD_KEY_NOT_READY &&
		    asc == 0x3a && (softc->flags & CD_FLAG_SAW_MEDIA)) {
			softc->flags &= ~CD_FLAG_SAW_MEDIA;
			disk_media_gone(softc->disk, M_NOWAIT);
		}
	}

	if (error == ERESTART)
		return (error);

	/*
	 * XXX
	 * Until we have a better way of doing pack validation,
	 * don't treat UAs as errors.
	 */
	sense_flags |= SF_RETRY_UA;

	if (softc->quirks & CD_Q_RETRY_BUSY)
		sense_flags |= SF_RETRY_BUSY;
	return (cam_periph_error(ccb, cam_flags, sense_flags));
}

static void
cdmediapoll(void *arg)
{
	struct cam_periph *periph = arg;
	struct cd_softc *softc = periph->softc;

	if (softc->state == CD_STATE_NORMAL && !softc->tur &&
	    softc->outstanding_cmds == 0) {
		if (cam_periph_acquire(periph) == 0) {
			softc->tur = 1;
			xpt_schedule(periph, CAM_PRIORITY_NORMAL);
		}
	}
	/* Queue us up again */
	if (cd_poll_period != 0)
		callout_schedule(&softc->mediapoll_c, cd_poll_period * hz);
}

/*
 * Read table of contents
 */
static int 
cdreadtoc(struct cam_periph *periph, u_int32_t mode, u_int32_t start, 
	  u_int8_t *data, u_int32_t len, u_int32_t sense_flags)
{
	struct scsi_read_toc *scsi_cmd;
	u_int32_t ntoc;
        struct ccb_scsiio *csio;
	union ccb *ccb;
	int error;

	ntoc = len;
	error = 0;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	csio = &ccb->csio;

	cam_fill_csio(csio, 
		      /* retries */ cd_retry_count, 
		      /* cbfcnp */ NULL,
		      /* flags */ CAM_DIR_IN,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ data,
		      /* dxfer_len */ len,
		      /* sense_len */ SSD_FULL_SIZE,
		      sizeof(struct scsi_read_toc),
 		      /* timeout */ 50000);

	scsi_cmd = (struct scsi_read_toc *)&csio->cdb_io.cdb_bytes;
	bzero (scsi_cmd, sizeof(*scsi_cmd));

	if (mode == CD_MSF_FORMAT)
		scsi_cmd->byte2 |= CD_MSF;
	scsi_cmd->from_track = start;
	/* scsi_ulto2b(ntoc, (u_int8_t *)scsi_cmd->data_len); */
	scsi_cmd->data_len[0] = (ntoc) >> 8;
	scsi_cmd->data_len[1] = (ntoc) & 0xff;

	scsi_cmd->op_code = READ_TOC;

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA | sense_flags);

	xpt_release_ccb(ccb);

	return(error);
}

static int
cdreadsubchannel(struct cam_periph *periph, u_int32_t mode, 
		 u_int32_t format, int track, 
		 struct cd_sub_channel_info *data, u_int32_t len) 
{
	struct scsi_read_subchannel *scsi_cmd;
        struct ccb_scsiio *csio;
	union ccb *ccb;
	int error;

	error = 0;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	csio = &ccb->csio;

	cam_fill_csio(csio, 
		      /* retries */ cd_retry_count, 
		      /* cbfcnp */ NULL,
		      /* flags */ CAM_DIR_IN,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ (u_int8_t *)data,
		      /* dxfer_len */ len,
		      /* sense_len */ SSD_FULL_SIZE,
		      sizeof(struct scsi_read_subchannel),
 		      /* timeout */ 50000);

	scsi_cmd = (struct scsi_read_subchannel *)&csio->cdb_io.cdb_bytes;
	bzero (scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->op_code = READ_SUBCHANNEL;
	if (mode == CD_MSF_FORMAT)
		scsi_cmd->byte1 |= CD_MSF;
	scsi_cmd->byte2 = SRS_SUBQ;
	scsi_cmd->subchan_format = format;
	scsi_cmd->track = track;
	scsi_ulto2b(len, (u_int8_t *)scsi_cmd->data_len);
	scsi_cmd->control = 0;

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	return(error);
}


/*
 * All MODE_SENSE requests in the cd(4) driver MUST go through this
 * routine.  See comments in cd6byteworkaround() for details.
 */
static int
cdgetmode(struct cam_periph *periph, struct cd_mode_params *data,
	  u_int32_t page)
{
	struct ccb_scsiio *csio;
	struct cd_softc *softc;
	union ccb *ccb;
	int param_len;
	int error;

	softc = (struct cd_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	csio = &ccb->csio;

	data->cdb_size = softc->minimum_command_size;
	if (data->cdb_size < 10)
		param_len = sizeof(struct cd_mode_data);
	else
		param_len = sizeof(struct cd_mode_data_10);

	/* Don't say we've got more room than we actually allocated */
	param_len = min(param_len, data->alloc_len);

	scsi_mode_sense_len(csio,
			    /* retries */ cd_retry_count,
			    /* cbfcnp */ NULL,
			    /* tag_action */ MSG_SIMPLE_Q_TAG,
			    /* dbd */ 0,
			    /* page_code */ SMS_PAGE_CTRL_CURRENT,
			    /* page */ page,
			    /* param_buf */ data->mode_buf,
			    /* param_len */ param_len,
			    /* minimum_cmd_size */ softc->minimum_command_size,
			    /* sense_len */ SSD_FULL_SIZE,
			    /* timeout */ 50000);

	/*
	 * It would be nice not to have to do this, but there's no
	 * available pointer in the CCB that would allow us to stuff the
	 * mode params structure in there and retrieve it in
	 * cd6byteworkaround(), so we can set the cdb size.  The cdb size
	 * lets the caller know what CDB size we ended up using, so they
	 * can find the actual mode page offset.
	 */
	STAILQ_INSERT_TAIL(&softc->mode_queue, data, links);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	STAILQ_REMOVE(&softc->mode_queue, data, cd_mode_params, links);

	/*
	 * This is a bit of belt-and-suspenders checking, but if we run
	 * into a situation where the target sends back multiple block
	 * descriptors, we might not have enough space in the buffer to
	 * see the whole mode page.  Better to return an error than
	 * potentially access memory beyond our malloced region.
	 */
	if (error == 0) {
		u_int32_t data_len;

		if (data->cdb_size == 10) {
			struct scsi_mode_header_10 *hdr10;

			hdr10 = (struct scsi_mode_header_10 *)data->mode_buf;
			data_len = scsi_2btoul(hdr10->data_length);
			data_len += sizeof(hdr10->data_length);
		} else {
			struct scsi_mode_header_6 *hdr6;

			hdr6 = (struct scsi_mode_header_6 *)data->mode_buf;
			data_len = hdr6->data_length;
			data_len += sizeof(hdr6->data_length);
		}

		/*
		 * Complain if there is more mode data available than we
		 * allocated space for.  This could potentially happen if
		 * we miscalculated the page length for some reason, if the
		 * drive returns multiple block descriptors, or if it sets
		 * the data length incorrectly.
		 */
		if (data_len > data->alloc_len) {
			xpt_print(periph->path, "allocated modepage %d length "
			    "%d < returned length %d\n", page, data->alloc_len,
			    data_len);
			error = ENOSPC;
		}
	}
	return (error);
}

/*
 * All MODE_SELECT requests in the cd(4) driver MUST go through this
 * routine.  See comments in cd6byteworkaround() for details.
 */
static int
cdsetmode(struct cam_periph *periph, struct cd_mode_params *data)
{
	struct ccb_scsiio *csio;
	struct cd_softc *softc;
	union ccb *ccb;
	int cdb_size, param_len;
	int error;

	softc = (struct cd_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	csio = &ccb->csio;

	error = 0;

	/*
	 * If the data is formatted for the 10 byte version of the mode
	 * select parameter list, we need to use the 10 byte CDB.
	 * Otherwise, we use whatever the stored minimum command size.
	 */
	if (data->cdb_size == 10)
		cdb_size = data->cdb_size;
	else
		cdb_size = softc->minimum_command_size;

	if (cdb_size >= 10) {
		struct scsi_mode_header_10 *mode_header;
		u_int32_t data_len;

		mode_header = (struct scsi_mode_header_10 *)data->mode_buf;

		data_len = scsi_2btoul(mode_header->data_length);

		scsi_ulto2b(0, mode_header->data_length);
		/*
		 * SONY drives do not allow a mode select with a medium_type
		 * value that has just been returned by a mode sense; use a
		 * medium_type of 0 (Default) instead.
		 */
		mode_header->medium_type = 0;

		/*
		 * Pass back whatever the drive passed to us, plus the size
		 * of the data length field.
		 */
		param_len = data_len + sizeof(mode_header->data_length);

	} else {
		struct scsi_mode_header_6 *mode_header;

		mode_header = (struct scsi_mode_header_6 *)data->mode_buf;

		param_len = mode_header->data_length + 1;

		mode_header->data_length = 0;
		/*
		 * SONY drives do not allow a mode select with a medium_type
		 * value that has just been returned by a mode sense; use a
		 * medium_type of 0 (Default) instead.
		 */
		mode_header->medium_type = 0;
	}

	/* Don't say we've got more room than we actually allocated */
	param_len = min(param_len, data->alloc_len);

	scsi_mode_select_len(csio,
			     /* retries */ cd_retry_count,
			     /* cbfcnp */ NULL,
			     /* tag_action */ MSG_SIMPLE_Q_TAG,
			     /* scsi_page_fmt */ 1,
			     /* save_pages */ 0,
			     /* param_buf */ data->mode_buf,
			     /* param_len */ param_len,
			     /* minimum_cmd_size */ cdb_size,
			     /* sense_len */ SSD_FULL_SIZE,
			     /* timeout */ 50000);

	/* See comments in cdgetmode() and cd6byteworkaround(). */
	STAILQ_INSERT_TAIL(&softc->mode_queue, data, links);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	STAILQ_REMOVE(&softc->mode_queue, data, cd_mode_params, links);

	return (error);
}


static int 
cdplay(struct cam_periph *periph, u_int32_t blk, u_int32_t len)
{
	struct ccb_scsiio *csio;
	union ccb *ccb;
	int error;
	u_int8_t cdb_len;

	error = 0;
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
	csio = &ccb->csio;
	/*
	 * Use the smallest possible command to perform the operation.
	 */
	if ((len & 0xffff0000) == 0) {
		/*
		 * We can fit in a 10 byte cdb.
		 */
		struct scsi_play_10 *scsi_cmd;

		scsi_cmd = (struct scsi_play_10 *)&csio->cdb_io.cdb_bytes;
		bzero (scsi_cmd, sizeof(*scsi_cmd));
		scsi_cmd->op_code = PLAY_10;
		scsi_ulto4b(blk, (u_int8_t *)scsi_cmd->blk_addr);
		scsi_ulto2b(len, (u_int8_t *)scsi_cmd->xfer_len);
		cdb_len = sizeof(*scsi_cmd);
	} else  {
		struct scsi_play_12 *scsi_cmd;

		scsi_cmd = (struct scsi_play_12 *)&csio->cdb_io.cdb_bytes;
		bzero (scsi_cmd, sizeof(*scsi_cmd));
		scsi_cmd->op_code = PLAY_12;
		scsi_ulto4b(blk, (u_int8_t *)scsi_cmd->blk_addr);
		scsi_ulto4b(len, (u_int8_t *)scsi_cmd->xfer_len);
		cdb_len = sizeof(*scsi_cmd);
	}
	cam_fill_csio(csio,
		      /*retries*/ cd_retry_count,
		      /*cbfcnp*/NULL,
		      /*flags*/CAM_DIR_NONE,
		      MSG_SIMPLE_Q_TAG,
		      /*dataptr*/NULL,
		      /*datalen*/0,
		      /*sense_len*/SSD_FULL_SIZE,
		      cdb_len,
		      /*timeout*/50 * 1000);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	return(error);
}

static int
cdplaymsf(struct cam_periph *periph, u_int32_t startm, u_int32_t starts,
	  u_int32_t startf, u_int32_t endm, u_int32_t ends, u_int32_t endf)
{
	struct scsi_play_msf *scsi_cmd;
        struct ccb_scsiio *csio;
	union ccb *ccb;
	int error;

	error = 0;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	csio = &ccb->csio;

	cam_fill_csio(csio, 
		      /* retries */ cd_retry_count, 
		      /* cbfcnp */ NULL,
		      /* flags */ CAM_DIR_NONE,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ NULL,
		      /* dxfer_len */ 0,
		      /* sense_len */ SSD_FULL_SIZE,
		      sizeof(struct scsi_play_msf),
 		      /* timeout */ 50000);

	scsi_cmd = (struct scsi_play_msf *)&csio->cdb_io.cdb_bytes;
	bzero (scsi_cmd, sizeof(*scsi_cmd));

        scsi_cmd->op_code = PLAY_MSF;
        scsi_cmd->start_m = startm;
        scsi_cmd->start_s = starts;
        scsi_cmd->start_f = startf;
        scsi_cmd->end_m = endm;
        scsi_cmd->end_s = ends;
        scsi_cmd->end_f = endf; 

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);
	
	xpt_release_ccb(ccb);

	return(error);
}


static int
cdplaytracks(struct cam_periph *periph, u_int32_t strack, u_int32_t sindex,
	     u_int32_t etrack, u_int32_t eindex)
{
	struct scsi_play_track *scsi_cmd;
        struct ccb_scsiio *csio;
	union ccb *ccb;
	int error;

	error = 0;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	csio = &ccb->csio;

	cam_fill_csio(csio, 
		      /* retries */ cd_retry_count, 
		      /* cbfcnp */ NULL,
		      /* flags */ CAM_DIR_NONE,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ NULL,
		      /* dxfer_len */ 0,
		      /* sense_len */ SSD_FULL_SIZE,
		      sizeof(struct scsi_play_track),
 		      /* timeout */ 50000);

	scsi_cmd = (struct scsi_play_track *)&csio->cdb_io.cdb_bytes;
	bzero (scsi_cmd, sizeof(*scsi_cmd));

        scsi_cmd->op_code = PLAY_TRACK;
        scsi_cmd->start_track = strack;
        scsi_cmd->start_index = sindex;
        scsi_cmd->end_track = etrack;
        scsi_cmd->end_index = eindex;

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	return(error);
}

static int
cdpause(struct cam_periph *periph, u_int32_t go)
{
	struct scsi_pause *scsi_cmd;
        struct ccb_scsiio *csio;
	union ccb *ccb;
	int error;

	error = 0;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	csio = &ccb->csio;

	cam_fill_csio(csio, 
		      /* retries */ cd_retry_count, 
		      /* cbfcnp */ NULL,
		      /* flags */ CAM_DIR_NONE,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ NULL,
		      /* dxfer_len */ 0,
		      /* sense_len */ SSD_FULL_SIZE,
		      sizeof(struct scsi_pause),
 		      /* timeout */ 50000);

	scsi_cmd = (struct scsi_pause *)&csio->cdb_io.cdb_bytes;
	bzero (scsi_cmd, sizeof(*scsi_cmd));

        scsi_cmd->op_code = PAUSE;
	scsi_cmd->resume = go;

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	return(error);
}

static int
cdstartunit(struct cam_periph *periph, int load)
{
	union ccb *ccb;
	int error;

	error = 0;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_start_stop(&ccb->csio,
			/* retries */ cd_retry_count,
			/* cbfcnp */ NULL,
			/* tag_action */ MSG_SIMPLE_Q_TAG,
			/* start */ TRUE,
			/* load_eject */ load,
			/* immediate */ FALSE,
			/* sense_len */ SSD_FULL_SIZE,
			/* timeout */ 50000);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	return(error);
}

static int
cdstopunit(struct cam_periph *periph, u_int32_t eject)
{
	union ccb *ccb;
	int error;

	error = 0;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_start_stop(&ccb->csio,
			/* retries */ cd_retry_count,
			/* cbfcnp */ NULL,
			/* tag_action */ MSG_SIMPLE_Q_TAG,
			/* start */ FALSE,
			/* load_eject */ eject,
			/* immediate */ FALSE,
			/* sense_len */ SSD_FULL_SIZE,
			/* timeout */ 50000);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	return(error);
}

static int
cdsetspeed(struct cam_periph *periph, u_int32_t rdspeed, u_int32_t wrspeed)
{
	struct scsi_set_speed *scsi_cmd;
	struct ccb_scsiio *csio;
	union ccb *ccb;
	int error;

	error = 0;
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
	csio = &ccb->csio;

	/* Preserve old behavior: units in multiples of CDROM speed */
	if (rdspeed < 177)
		rdspeed *= 177;
	if (wrspeed < 177)
		wrspeed *= 177;

	cam_fill_csio(csio,
		      /* retries */ cd_retry_count,
		      /* cbfcnp */ NULL,
		      /* flags */ CAM_DIR_NONE,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* data_ptr */ NULL,
		      /* dxfer_len */ 0,
		      /* sense_len */ SSD_FULL_SIZE,
		      sizeof(struct scsi_set_speed),
 		      /* timeout */ 50000);

	scsi_cmd = (struct scsi_set_speed *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = SET_CD_SPEED;
	scsi_ulto2b(rdspeed, scsi_cmd->readspeed);
	scsi_ulto2b(wrspeed, scsi_cmd->writespeed);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);

	return(error);
}

static int
cdreportkey(struct cam_periph *periph, struct dvd_authinfo *authinfo)
{
	union ccb *ccb;
	u_int8_t *databuf;
	u_int32_t lba;
	int error;
	int length;

	error = 0;
	databuf = NULL;
	lba = 0;

	switch (authinfo->format) {
	case DVD_REPORT_AGID:
		length = sizeof(struct scsi_report_key_data_agid);
		break;
	case DVD_REPORT_CHALLENGE:
		length = sizeof(struct scsi_report_key_data_challenge);
		break;
	case DVD_REPORT_KEY1:
		length = sizeof(struct scsi_report_key_data_key1_key2);
		break;
	case DVD_REPORT_TITLE_KEY:
		length = sizeof(struct scsi_report_key_data_title);
		/* The lba field is only set for the title key */
		lba = authinfo->lba;
		break;
	case DVD_REPORT_ASF:
		length = sizeof(struct scsi_report_key_data_asf);
		break;
	case DVD_REPORT_RPC:
		length = sizeof(struct scsi_report_key_data_rpc);
		break;
	case DVD_INVALIDATE_AGID:
		length = 0;
		break;
	default:
		return (EINVAL);
	}

	if (length != 0) {
		databuf = malloc(length, M_DEVBUF, M_WAITOK | M_ZERO);
	} else
		databuf = NULL;

	cam_periph_lock(periph);
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_report_key(&ccb->csio,
			/* retries */ cd_retry_count,
			/* cbfcnp */ NULL,
			/* tag_action */ MSG_SIMPLE_Q_TAG,
			/* lba */ lba,
			/* agid */ authinfo->agid,
			/* key_format */ authinfo->format,
			/* data_ptr */ databuf,
			/* dxfer_len */ length,
			/* sense_len */ SSD_FULL_SIZE,
			/* timeout */ 50000);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	if (error != 0)
		goto bailout;

	if (ccb->csio.resid != 0) {
		xpt_print(periph->path, "warning, residual for report key "
		    "command is %d\n", ccb->csio.resid);
	}

	switch(authinfo->format) {
	case DVD_REPORT_AGID: {
		struct scsi_report_key_data_agid *agid_data;

		agid_data = (struct scsi_report_key_data_agid *)databuf;

		authinfo->agid = (agid_data->agid & RKD_AGID_MASK) >>
			RKD_AGID_SHIFT;
		break;
	}
	case DVD_REPORT_CHALLENGE: {
		struct scsi_report_key_data_challenge *chal_data;

		chal_data = (struct scsi_report_key_data_challenge *)databuf;

		bcopy(chal_data->challenge_key, authinfo->keychal,
		      min(sizeof(chal_data->challenge_key),
		          sizeof(authinfo->keychal)));
		break;
	}
	case DVD_REPORT_KEY1: {
		struct scsi_report_key_data_key1_key2 *key1_data;

		key1_data = (struct scsi_report_key_data_key1_key2 *)databuf;

		bcopy(key1_data->key1, authinfo->keychal,
		      min(sizeof(key1_data->key1), sizeof(authinfo->keychal)));
		break;
	}
	case DVD_REPORT_TITLE_KEY: {
		struct scsi_report_key_data_title *title_data;

		title_data = (struct scsi_report_key_data_title *)databuf;

		authinfo->cpm = (title_data->byte0 & RKD_TITLE_CPM) >>
			RKD_TITLE_CPM_SHIFT;
		authinfo->cp_sec = (title_data->byte0 & RKD_TITLE_CP_SEC) >>
			RKD_TITLE_CP_SEC_SHIFT;
		authinfo->cgms = (title_data->byte0 & RKD_TITLE_CMGS_MASK) >>
			RKD_TITLE_CMGS_SHIFT;
		bcopy(title_data->title_key, authinfo->keychal,
		      min(sizeof(title_data->title_key),
			  sizeof(authinfo->keychal)));
		break;
	}
	case DVD_REPORT_ASF: {
		struct scsi_report_key_data_asf *asf_data;

		asf_data = (struct scsi_report_key_data_asf *)databuf;

		authinfo->asf = asf_data->success & RKD_ASF_SUCCESS;
		break;
	}
	case DVD_REPORT_RPC: {
		struct scsi_report_key_data_rpc *rpc_data;

		rpc_data = (struct scsi_report_key_data_rpc *)databuf;

		authinfo->reg_type = (rpc_data->byte4 & RKD_RPC_TYPE_MASK) >>
			RKD_RPC_TYPE_SHIFT;
		authinfo->vend_rsts =
			(rpc_data->byte4 & RKD_RPC_VENDOR_RESET_MASK) >>
			RKD_RPC_VENDOR_RESET_SHIFT;
		authinfo->user_rsts = rpc_data->byte4 & RKD_RPC_USER_RESET_MASK;
		authinfo->region = rpc_data->region_mask;
		authinfo->rpc_scheme = rpc_data->rpc_scheme1;
		break;
	}
	case DVD_INVALIDATE_AGID:
		break;
	default:
		/* This should be impossible, since we checked above */
		error = EINVAL;
		goto bailout;
		break; /* NOTREACHED */
	}

bailout:
	xpt_release_ccb(ccb);
	cam_periph_unlock(periph);

	if (databuf != NULL)
		free(databuf, M_DEVBUF);

	return(error);
}

static int
cdsendkey(struct cam_periph *periph, struct dvd_authinfo *authinfo)
{
	union ccb *ccb;
	u_int8_t *databuf;
	int length;
	int error;

	error = 0;
	databuf = NULL;

	switch(authinfo->format) {
	case DVD_SEND_CHALLENGE: {
		struct scsi_report_key_data_challenge *challenge_data;

		length = sizeof(*challenge_data);

		challenge_data = malloc(length, M_DEVBUF, M_WAITOK | M_ZERO);

		databuf = (u_int8_t *)challenge_data;

		scsi_ulto2b(length - sizeof(challenge_data->data_len),
			    challenge_data->data_len);

		bcopy(authinfo->keychal, challenge_data->challenge_key,
		      min(sizeof(authinfo->keychal),
			  sizeof(challenge_data->challenge_key)));
		break;
	}
	case DVD_SEND_KEY2: {
		struct scsi_report_key_data_key1_key2 *key2_data;

		length = sizeof(*key2_data);

		key2_data = malloc(length, M_DEVBUF, M_WAITOK | M_ZERO);

		databuf = (u_int8_t *)key2_data;

		scsi_ulto2b(length - sizeof(key2_data->data_len),
			    key2_data->data_len);

		bcopy(authinfo->keychal, key2_data->key1,
		      min(sizeof(authinfo->keychal), sizeof(key2_data->key1)));

		break;
	}
	case DVD_SEND_RPC: {
		struct scsi_send_key_data_rpc *rpc_data;

		length = sizeof(*rpc_data);

		rpc_data = malloc(length, M_DEVBUF, M_WAITOK | M_ZERO);

		databuf = (u_int8_t *)rpc_data;

		scsi_ulto2b(length - sizeof(rpc_data->data_len),
			    rpc_data->data_len);

		rpc_data->region_code = authinfo->region;
		break;
	}
	default:
		return (EINVAL);
	}

	cam_periph_lock(periph);
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_send_key(&ccb->csio,
		      /* retries */ cd_retry_count,
		      /* cbfcnp */ NULL,
		      /* tag_action */ MSG_SIMPLE_Q_TAG,
		      /* agid */ authinfo->agid,
		      /* key_format */ authinfo->format,
		      /* data_ptr */ databuf,
		      /* dxfer_len */ length,
		      /* sense_len */ SSD_FULL_SIZE,
		      /* timeout */ 50000);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	xpt_release_ccb(ccb);
	cam_periph_unlock(periph);

	if (databuf != NULL)
		free(databuf, M_DEVBUF);

	return(error);
}

static int
cdreaddvdstructure(struct cam_periph *periph, struct dvd_struct *dvdstruct)
{
	union ccb *ccb;
	u_int8_t *databuf;
	u_int32_t address;
	int error;
	int length;

	error = 0;
	databuf = NULL;
	/* The address is reserved for many of the formats */
	address = 0;

	switch(dvdstruct->format) {
	case DVD_STRUCT_PHYSICAL:
		length = sizeof(struct scsi_read_dvd_struct_data_physical);
		break;
	case DVD_STRUCT_COPYRIGHT:
		length = sizeof(struct scsi_read_dvd_struct_data_copyright);
		break;
	case DVD_STRUCT_DISCKEY:
		length = sizeof(struct scsi_read_dvd_struct_data_disc_key);
		break;
	case DVD_STRUCT_BCA:
		length = sizeof(struct scsi_read_dvd_struct_data_bca);
		break;
	case DVD_STRUCT_MANUFACT:
		length = sizeof(struct scsi_read_dvd_struct_data_manufacturer);
		break;
	case DVD_STRUCT_CMI:
		return (ENODEV);
	case DVD_STRUCT_PROTDISCID:
		length = sizeof(struct scsi_read_dvd_struct_data_prot_discid);
		break;
	case DVD_STRUCT_DISCKEYBLOCK:
		length = sizeof(struct scsi_read_dvd_struct_data_disc_key_blk);
		break;
	case DVD_STRUCT_DDS:
		length = sizeof(struct scsi_read_dvd_struct_data_dds);
		break;
	case DVD_STRUCT_MEDIUM_STAT:
		length = sizeof(struct scsi_read_dvd_struct_data_medium_status);
		break;
	case DVD_STRUCT_SPARE_AREA:
		length = sizeof(struct scsi_read_dvd_struct_data_spare_area);
		break;
	case DVD_STRUCT_RMD_LAST:
		return (ENODEV);
	case DVD_STRUCT_RMD_RMA:
		return (ENODEV);
	case DVD_STRUCT_PRERECORDED:
		length = sizeof(struct scsi_read_dvd_struct_data_leadin);
		break;
	case DVD_STRUCT_UNIQUEID:
		length = sizeof(struct scsi_read_dvd_struct_data_disc_id);
		break;
	case DVD_STRUCT_DCB:
		return (ENODEV);
	case DVD_STRUCT_LIST:
		/*
		 * This is the maximum allocation length for the READ DVD
		 * STRUCTURE command.  There's nothing in the MMC3 spec
		 * that indicates a limit in the amount of data that can
		 * be returned from this call, other than the limits
		 * imposed by the 2-byte length variables.
		 */
		length = 65535;
		break;
	default:
		return (EINVAL);
	}

	if (length != 0) {
		databuf = malloc(length, M_DEVBUF, M_WAITOK | M_ZERO);
	} else
		databuf = NULL;

	cam_periph_lock(periph);
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_read_dvd_structure(&ccb->csio,
				/* retries */ cd_retry_count,
				/* cbfcnp */ NULL,
				/* tag_action */ MSG_SIMPLE_Q_TAG,
				/* lba */ address,
				/* layer_number */ dvdstruct->layer_num,
				/* key_format */ dvdstruct->format,
				/* agid */ dvdstruct->agid,
				/* data_ptr */ databuf,
				/* dxfer_len */ length,
				/* sense_len */ SSD_FULL_SIZE,
				/* timeout */ 50000);

	error = cdrunccb(ccb, cderror, /*cam_flags*/CAM_RETRY_SELTO,
			 /*sense_flags*/SF_RETRY_UA);

	if (error != 0)
		goto bailout;

	switch(dvdstruct->format) {
	case DVD_STRUCT_PHYSICAL: {
		struct scsi_read_dvd_struct_data_layer_desc *inlayer;
		struct dvd_layer *outlayer;
		struct scsi_read_dvd_struct_data_physical *phys_data;

		phys_data =
			(struct scsi_read_dvd_struct_data_physical *)databuf;
		inlayer = &phys_data->layer_desc;
		outlayer = (struct dvd_layer *)&dvdstruct->data;

		dvdstruct->length = sizeof(*inlayer);

		outlayer->book_type = (inlayer->book_type_version &
			RDSD_BOOK_TYPE_MASK) >> RDSD_BOOK_TYPE_SHIFT;
		outlayer->book_version = (inlayer->book_type_version &
			RDSD_BOOK_VERSION_MASK);
		outlayer->disc_size = (inlayer->disc_size_max_rate &
			RDSD_DISC_SIZE_MASK) >> RDSD_DISC_SIZE_SHIFT;
		outlayer->max_rate = (inlayer->disc_size_max_rate &
			RDSD_MAX_RATE_MASK);
		outlayer->nlayers = (inlayer->layer_info &
			RDSD_NUM_LAYERS_MASK) >> RDSD_NUM_LAYERS_SHIFT;
		outlayer->track_path = (inlayer->layer_info &
			RDSD_TRACK_PATH_MASK) >> RDSD_TRACK_PATH_SHIFT;
		outlayer->layer_type = (inlayer->layer_info &
			RDSD_LAYER_TYPE_MASK);
		outlayer->linear_density = (inlayer->density &
			RDSD_LIN_DENSITY_MASK) >> RDSD_LIN_DENSITY_SHIFT;
		outlayer->track_density = (inlayer->density &
			RDSD_TRACK_DENSITY_MASK);
		outlayer->bca = (inlayer->bca & RDSD_BCA_MASK) >>
			RDSD_BCA_SHIFT;
		outlayer->start_sector = scsi_3btoul(inlayer->main_data_start);
		outlayer->end_sector = scsi_3btoul(inlayer->main_data_end);
		outlayer->end_sector_l0 =
			scsi_3btoul(inlayer->end_sector_layer0);
		break;
	}
	case DVD_STRUCT_COPYRIGHT: {
		struct scsi_read_dvd_struct_data_copyright *copy_data;

		copy_data = (struct scsi_read_dvd_struct_data_copyright *)
			databuf;

		dvdstruct->cpst = copy_data->cps_type;
		dvdstruct->rmi = copy_data->region_info;
		dvdstruct->length = 0;

		break;
	}
	default:
		/*
		 * Tell the user what the overall length is, no matter
		 * what we can actually fit in the data buffer.
		 */
		dvdstruct->length = length - ccb->csio.resid - 
			sizeof(struct scsi_read_dvd_struct_data_header);

		/*
		 * But only actually copy out the smaller of what we read
		 * in or what the structure can take.
		 */
		bcopy(databuf + sizeof(struct scsi_read_dvd_struct_data_header),
		      dvdstruct->data,
		      min(sizeof(dvdstruct->data), dvdstruct->length));
		break;
	}

bailout:
	xpt_release_ccb(ccb);
	cam_periph_unlock(periph);

	if (databuf != NULL)
		free(databuf, M_DEVBUF);

	return(error);
}

void
scsi_report_key(struct ccb_scsiio *csio, u_int32_t retries,
		void (*cbfcnp)(struct cam_periph *, union ccb *),
		u_int8_t tag_action, u_int32_t lba, u_int8_t agid,
		u_int8_t key_format, u_int8_t *data_ptr, u_int32_t dxfer_len,
		u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_report_key *scsi_cmd;

	scsi_cmd = (struct scsi_report_key *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = REPORT_KEY;
	scsi_ulto4b(lba, scsi_cmd->lba);
	scsi_ulto2b(dxfer_len, scsi_cmd->alloc_len);
	scsi_cmd->agid_keyformat = (agid << RK_KF_AGID_SHIFT) |
		(key_format & RK_KF_KEYFORMAT_MASK);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ (dxfer_len == 0) ? CAM_DIR_NONE : CAM_DIR_IN,
		      tag_action,
		      /*data_ptr*/ data_ptr,
		      /*dxfer_len*/ dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_send_key(struct ccb_scsiio *csio, u_int32_t retries,
	      void (*cbfcnp)(struct cam_periph *, union ccb *),
	      u_int8_t tag_action, u_int8_t agid, u_int8_t key_format,
	      u_int8_t *data_ptr, u_int32_t dxfer_len, u_int8_t sense_len,
	      u_int32_t timeout)
{
	struct scsi_send_key *scsi_cmd;

	scsi_cmd = (struct scsi_send_key *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = SEND_KEY;

	scsi_ulto2b(dxfer_len, scsi_cmd->param_len);
	scsi_cmd->agid_keyformat = (agid << RK_KF_AGID_SHIFT) |
		(key_format & RK_KF_KEYFORMAT_MASK);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_OUT,
		      tag_action,
		      /*data_ptr*/ data_ptr,
		      /*dxfer_len*/ dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}


void
scsi_read_dvd_structure(struct ccb_scsiio *csio, u_int32_t retries,
			void (*cbfcnp)(struct cam_periph *, union ccb *),
			u_int8_t tag_action, u_int32_t address,
			u_int8_t layer_number, u_int8_t format, u_int8_t agid,
			u_int8_t *data_ptr, u_int32_t dxfer_len,
			u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_read_dvd_structure *scsi_cmd;

	scsi_cmd = (struct scsi_read_dvd_structure *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = READ_DVD_STRUCTURE;

	scsi_ulto4b(address, scsi_cmd->address);
	scsi_cmd->layer_number = layer_number;
	scsi_cmd->format = format;
	scsi_ulto2b(dxfer_len, scsi_cmd->alloc_len);
	/* The AGID is the top two bits of this byte */
	scsi_cmd->agid = agid << 6;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_IN,
		      tag_action,
		      /*data_ptr*/ data_ptr,
		      /*dxfer_len*/ dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}
