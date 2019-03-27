/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND BSD-4-Clause)
 *
 * Copyright (c) 1997 Justin T. Gibbs.
 * Copyright (c) 1997, 1998, 1999 Kenneth D. Merry.
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
 * Copyright (c) 1996, 1997 Jason R. Thorpe <thorpej@and.com>
 * All rights reserved.
 *
 * Partially based on an autochanger driver written by Stefan Grefen
 * and on an autochanger driver written by the Systems Programming Group
 * at the University of Utah Computer Science Department.
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
 *    must display the following acknowledgements:
 *	This product includes software developed by Jason R. Thorpe
 *	for And Communications, http://www.and.com/
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * $NetBSD: ch.c,v 1.34 1998/08/31 22:28:06 cgd Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/chio.h>
#include <sys/errno.h>
#include <sys/devicestat.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_debug.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_ch.h>

/*
 * Timeout definitions for various changer related commands.  They may
 * be too short for some devices (especially the timeout for INITIALIZE
 * ELEMENT STATUS).
 */

static const u_int32_t	CH_TIMEOUT_MODE_SENSE                = 6000;
static const u_int32_t	CH_TIMEOUT_MOVE_MEDIUM               = 15 * 60 * 1000;
static const u_int32_t	CH_TIMEOUT_EXCHANGE_MEDIUM           = 15 * 60 * 1000;
static const u_int32_t	CH_TIMEOUT_POSITION_TO_ELEMENT       = 15 * 60 * 1000;
static const u_int32_t	CH_TIMEOUT_READ_ELEMENT_STATUS       = 5 * 60 * 1000;
static const u_int32_t	CH_TIMEOUT_SEND_VOLTAG		     = 10000;
static const u_int32_t	CH_TIMEOUT_INITIALIZE_ELEMENT_STATUS = 500000;

typedef enum {
	CH_FLAG_INVALID		= 0x001
} ch_flags;

typedef enum {
	CH_STATE_PROBE,
	CH_STATE_NORMAL
} ch_state;

typedef enum {
	CH_CCB_PROBE
} ch_ccb_types;

typedef enum {
	CH_Q_NONE	= 0x00,
	CH_Q_NO_DBD	= 0x01,
	CH_Q_NO_DVCID	= 0x02
} ch_quirks;

#define CH_Q_BIT_STRING	\
	"\020"		\
	"\001NO_DBD"	\
	"\002NO_DVCID"

#define ccb_state	ppriv_field0
#define ccb_bp		ppriv_ptr1

struct scsi_mode_sense_data {
	struct scsi_mode_header_6 header;
	struct scsi_mode_blk_desc blk_desc;
	union {
		struct page_element_address_assignment ea;
		struct page_transport_geometry_parameters tg;
		struct page_device_capabilities cap;
	} pages;
};

struct ch_softc {
	ch_flags	flags;
	ch_state	state;
	ch_quirks	quirks;
	union ccb	saved_ccb;
	struct devstat	*device_stats;
	struct cdev     *dev;
	int		open_count;

	int		sc_picker;	/* current picker */

	/*
	 * The following information is obtained from the
	 * element address assignment page.
	 */
	int		sc_firsts[CHET_MAX + 1];	/* firsts */
	int		sc_counts[CHET_MAX + 1];	/* counts */

	/*
	 * The following mask defines the legal combinations
	 * of elements for the MOVE MEDIUM command.
	 */
	u_int8_t	sc_movemask[CHET_MAX + 1];

	/*
	 * As above, but for EXCHANGE MEDIUM.
	 */
	u_int8_t	sc_exchangemask[CHET_MAX + 1];

	/*
	 * Quirks; see below.  XXX KDM not implemented yet
	 */
	int		sc_settledelay;	/* delay for settle */
};

static	d_open_t	chopen;
static	d_close_t	chclose;
static	d_ioctl_t	chioctl;
static	periph_init_t	chinit;
static  periph_ctor_t	chregister;
static	periph_oninv_t	choninvalidate;
static  periph_dtor_t   chcleanup;
static  periph_start_t  chstart;
static	void		chasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	void		chdone(struct cam_periph *periph,
			       union ccb *done_ccb);
static	int		cherror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);
static	int		chmove(struct cam_periph *periph,
			       struct changer_move *cm);
static	int		chexchange(struct cam_periph *periph,
				   struct changer_exchange *ce);
static	int		chposition(struct cam_periph *periph,
				   struct changer_position *cp);
static	int		chgetelemstatus(struct cam_periph *periph,
				int scsi_version, u_long cmd,
				struct changer_element_status_request *csr);
static	int		chsetvoltag(struct cam_periph *periph,
				    struct changer_set_voltag_request *csvr);
static	int		chielem(struct cam_periph *periph, 
				unsigned int timeout);
static	int		chgetparams(struct cam_periph *periph);
static	int		chscsiversion(struct cam_periph *periph);

static struct periph_driver chdriver =
{
	chinit, "ch",
	TAILQ_HEAD_INITIALIZER(chdriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(ch, chdriver);

static struct cdevsw ch_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_TRACKCLOSE,
	.d_open =	chopen,
	.d_close =	chclose,
	.d_ioctl =	chioctl,
	.d_name =	"ch",
};

static MALLOC_DEFINE(M_SCSICH, "scsi_ch", "scsi_ch buffers");

static void
chinit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, chasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("ch: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}
}

static void
chdevgonecb(void *arg)
{
	struct ch_softc   *softc;
	struct cam_periph *periph;
	struct mtx *mtx;
	int i;

	periph = (struct cam_periph *)arg;
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	softc = (struct ch_softc *)periph->softc;
	KASSERT(softc->open_count >= 0, ("Negative open count %d",
		softc->open_count));

	/*
	 * When we get this callback, we will get no more close calls from
	 * devfs.  So if we have any dangling opens, we need to release the
	 * reference held for that particular context.
	 */
	for (i = 0; i < softc->open_count; i++)
		cam_periph_release_locked(periph);

	softc->open_count = 0;

	/*
	 * Release the reference held for the device node, it is gone now.
	 */
	cam_periph_release_locked(periph);

	/*
	 * We reference the lock directly here, instead of using
	 * cam_periph_unlock().  The reason is that the final call to
	 * cam_periph_release_locked() above could result in the periph
	 * getting freed.  If that is the case, dereferencing the periph
	 * with a cam_periph_unlock() call would cause a page fault.
	 */
	mtx_unlock(mtx);
}

static void
choninvalidate(struct cam_periph *periph)
{
	struct ch_softc *softc;

	softc = (struct ch_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, chasync, periph, periph->path);

	softc->flags |= CH_FLAG_INVALID;

	/*
	 * Tell devfs this device has gone away, and ask for a callback
	 * when it has cleaned up its state.
	 */
	destroy_dev_sched_cb(softc->dev, chdevgonecb, periph);
}

static void
chcleanup(struct cam_periph *periph)
{
	struct ch_softc *softc;

	softc = (struct ch_softc *)periph->softc;

	devstat_remove_entry(softc->device_stats);

	free(softc, M_DEVBUF);
}

static void
chasync(void *callback_arg, u_int32_t code, struct cam_path *path, void *arg)
{
	struct cam_periph *periph;

	periph = (struct cam_periph *)callback_arg;

	switch(code) {
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
		if (SID_TYPE(&cgd->inq_data)!= T_CHANGER)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(chregister, choninvalidate,
					  chcleanup, chstart, "ch",
					  CAM_PERIPH_BIO, path,
					  chasync, AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("chasync: Unable to probe new device "
			       "due to status 0x%x\n", status);

		break;

	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static cam_status
chregister(struct cam_periph *periph, void *arg)
{
	struct ch_softc *softc;
	struct ccb_getdev *cgd;
	struct ccb_pathinq cpi;
	struct make_dev_args args;
	int error;

	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("chregister: no getdev CCB, can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = (struct ch_softc *)malloc(sizeof(*softc),M_DEVBUF,M_NOWAIT);

	if (softc == NULL) {
		printf("chregister: Unable to probe new device. "
		       "Unable to allocate softc\n");				
		return(CAM_REQ_CMP_ERR);
	}

	bzero(softc, sizeof(*softc));
	softc->state = CH_STATE_PROBE;
	periph->softc = softc;
	softc->quirks = CH_Q_NONE;

	/*
	 * The DVCID and CURDATA bits were not introduced until the SMC
	 * spec.  If this device claims SCSI-2 or earlier support, then it
	 * very likely does not support these bits.
	 */
	if (cgd->inq_data.version <= SCSI_REV_2)
		softc->quirks |= CH_Q_NO_DVCID;

	xpt_path_inq(&cpi, periph->path);

	/*
	 * Changers don't have a blocksize, and obviously don't support
	 * tagged queueing.
	 */
	cam_periph_unlock(periph);
	softc->device_stats = devstat_new_entry("ch",
			  periph->unit_number, 0,
			  DEVSTAT_NO_BLOCKSIZE | DEVSTAT_NO_ORDERED_TAGS,
			  SID_TYPE(&cgd->inq_data) |
			  XPORT_DEVSTAT_TYPE(cpi.transport),
			  DEVSTAT_PRIORITY_OTHER);

	/*
	 * Acquire a reference to the periph before we create the devfs
	 * instance for it.  We'll release this reference once the devfs
	 * instance has been freed.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}


	/* Register the device */
	make_dev_args_init(&args);
	args.mda_devsw = &ch_cdevsw;
	args.mda_unit = periph->unit_number;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0600;
	args.mda_si_drv1 = periph;
	error = make_dev_s(&args, &softc->dev, "%s%d", periph->periph_name,
	    periph->unit_number);
	cam_periph_lock(periph);
	if (error != 0) {
		cam_periph_release_locked(periph);
		return (CAM_REQ_CMP_ERR);
	}

	/*
	 * Add an async callback so that we get
	 * notified if this device goes away.
	 */
	xpt_register_async(AC_LOST_DEVICE, chasync, periph, periph->path);

	/*
	 * Lock this periph until we are setup.
	 * This first call can't block
	 */
	(void)cam_periph_hold(periph, PRIBIO);
	xpt_schedule(periph, CAM_PRIORITY_DEV);

	return(CAM_REQ_CMP);
}

static int
chopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct cam_periph *periph;
	struct ch_softc *softc;
	int error;

	periph = (struct cam_periph *)dev->si_drv1;
	if (cam_periph_acquire(periph) != 0)
		return (ENXIO);

	softc = (struct ch_softc *)periph->softc;

	cam_periph_lock(periph);
	
	if (softc->flags & CH_FLAG_INVALID) {
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return(ENXIO);
	}

	if ((error = cam_periph_hold(periph, PRIBIO | PCATCH)) != 0) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (error);
	}

	/*
	 * Load information about this changer device into the softc.
	 */
	if ((error = chgetparams(periph)) != 0) {
		cam_periph_unhold(periph);
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return(error);
	}

	cam_periph_unhold(periph);

	softc->open_count++;

	cam_periph_unlock(periph);

	return(error);
}

static int
chclose(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct	cam_periph *periph;
	struct  ch_softc *softc;
	struct mtx *mtx;

	periph = (struct cam_periph *)dev->si_drv1;
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	softc = (struct ch_softc *)periph->softc;
	softc->open_count--;

	cam_periph_release_locked(periph);

	/*
	 * We reference the lock directly here, instead of using
	 * cam_periph_unlock().  The reason is that the call to
	 * cam_periph_release_locked() above could result in the periph
	 * getting freed.  If that is the case, dereferencing the periph
	 * with a cam_periph_unlock() call would cause a page fault.
	 *
	 * cam_periph_release() avoids this problem using the same method,
	 * but we're manually acquiring and dropping the lock here to
	 * protect the open count and avoid another lock acquisition and
	 * release.
	 */
	mtx_unlock(mtx);

	return(0);
}

static void
chstart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct ch_softc *softc;

	softc = (struct ch_softc *)periph->softc;

	switch (softc->state) {
	case CH_STATE_NORMAL:
	{
		xpt_release_ccb(start_ccb);
		break;
	}
	case CH_STATE_PROBE:
	{
		int mode_buffer_len;
		void *mode_buffer;

		/*
		 * Include the block descriptor when calculating the mode
		 * buffer length,
		 */
		mode_buffer_len = sizeof(struct scsi_mode_header_6) +
				  sizeof(struct scsi_mode_blk_desc) +
				 sizeof(struct page_element_address_assignment);

		mode_buffer = malloc(mode_buffer_len, M_SCSICH, M_NOWAIT);

		if (mode_buffer == NULL) {
			printf("chstart: couldn't malloc mode sense data\n");
			break;
		}
		bzero(mode_buffer, mode_buffer_len);

		/*
		 * Get the element address assignment page.
		 */
		scsi_mode_sense(&start_ccb->csio,
				/* retries */ 1,
				/* cbfcnp */ chdone,
				/* tag_action */ MSG_SIMPLE_Q_TAG,
				/* dbd */ (softc->quirks & CH_Q_NO_DBD) ?
					FALSE : TRUE,
				/* pc */ SMS_PAGE_CTRL_CURRENT,
				/* page */ CH_ELEMENT_ADDR_ASSIGN_PAGE,
				/* param_buf */ (u_int8_t *)mode_buffer,
				/* param_len */ mode_buffer_len,
				/* sense_len */ SSD_FULL_SIZE,
				/* timeout */ CH_TIMEOUT_MODE_SENSE);

		start_ccb->ccb_h.ccb_bp = NULL;
		start_ccb->ccb_h.ccb_state = CH_CCB_PROBE;
		xpt_action(start_ccb);
		break;
	}
	}
}

static void
chdone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct ch_softc *softc;
	struct ccb_scsiio *csio;

	softc = (struct ch_softc *)periph->softc;
	csio = &done_ccb->csio;

	switch(done_ccb->ccb_h.ccb_state) {
	case CH_CCB_PROBE:
	{
		struct scsi_mode_header_6 *mode_header;
		struct page_element_address_assignment *ea;
		char announce_buf[80];


		mode_header = (struct scsi_mode_header_6 *)csio->data_ptr;

		ea = (struct page_element_address_assignment *)
			find_mode_page_6(mode_header);

		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP){
			
			softc->sc_firsts[CHET_MT] = scsi_2btoul(ea->mtea);
			softc->sc_counts[CHET_MT] = scsi_2btoul(ea->nmte);
			softc->sc_firsts[CHET_ST] = scsi_2btoul(ea->fsea);
			softc->sc_counts[CHET_ST] = scsi_2btoul(ea->nse);
			softc->sc_firsts[CHET_IE] = scsi_2btoul(ea->fieea);
			softc->sc_counts[CHET_IE] = scsi_2btoul(ea->niee);
			softc->sc_firsts[CHET_DT] = scsi_2btoul(ea->fdtea);
			softc->sc_counts[CHET_DT] = scsi_2btoul(ea->ndte);
			softc->sc_picker = softc->sc_firsts[CHET_MT];

#define PLURAL(c)	(c) == 1 ? "" : "s"
			snprintf(announce_buf, sizeof(announce_buf),
				"%d slot%s, %d drive%s, "
				"%d picker%s, %d portal%s",
		    		softc->sc_counts[CHET_ST],
				PLURAL(softc->sc_counts[CHET_ST]),
		    		softc->sc_counts[CHET_DT],
				PLURAL(softc->sc_counts[CHET_DT]),
		    		softc->sc_counts[CHET_MT],
				PLURAL(softc->sc_counts[CHET_MT]),
		    		softc->sc_counts[CHET_IE],
				PLURAL(softc->sc_counts[CHET_IE]));
#undef PLURAL
			if (announce_buf[0] != '\0') {
				xpt_announce_periph(periph, announce_buf);
				xpt_announce_quirks(periph, softc->quirks,
				    CH_Q_BIT_STRING);
			}
		} else {
			int error;

			error = cherror(done_ccb, CAM_RETRY_SELTO,
					SF_RETRY_UA | SF_NO_PRINT);
			/*
			 * Retry any UNIT ATTENTION type errors.  They
			 * are expected at boot.
			 */
			if (error == ERESTART) {
				/*
				 * A retry was scheduled, so
				 * just return.
				 */
				return;
			} else if (error != 0) {
				struct scsi_mode_sense_6 *sms;
				int frozen, retry_scheduled;

				sms = (struct scsi_mode_sense_6 *)
					done_ccb->csio.cdb_io.cdb_bytes;
				frozen = (done_ccb->ccb_h.status &
				    CAM_DEV_QFRZN) != 0;

				/*
				 * Check to see if block descriptors were
				 * disabled.  Some devices don't like that.
				 * We're taking advantage of the fact that
				 * the first few bytes of the 6 and 10 byte
				 * mode sense commands are the same.  If
				 * block descriptors were disabled, enable
				 * them and re-send the command.
				 */
				if ((sms->byte2 & SMS_DBD) != 0 &&
				    (periph->flags & CAM_PERIPH_INVALID) == 0) {
					sms->byte2 &= ~SMS_DBD;
					xpt_action(done_ccb);
					softc->quirks |= CH_Q_NO_DBD;
					retry_scheduled = 1;
				} else
					retry_scheduled = 0;

				/* Don't wedge this device's queue */
				if (frozen)
					cam_release_devq(done_ccb->ccb_h.path,
						 /*relsim_flags*/0,
						 /*reduction*/0,
						 /*timeout*/0,
						 /*getcount_only*/0);

				if (retry_scheduled)
					return;

				if ((done_ccb->ccb_h.status & CAM_STATUS_MASK)
				    == CAM_SCSI_STATUS_ERROR) 
					scsi_sense_print(&done_ccb->csio);
				else {
					xpt_print(periph->path,
					    "got CAM status %#x\n",
					    done_ccb->ccb_h.status);
				}
				xpt_print(periph->path, "fatal error, failed "
				    "to attach to device\n");

				cam_periph_invalidate(periph);

			}
		}
		softc->state = CH_STATE_NORMAL;
		free(mode_header, M_SCSICH);
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
	default:
		break;
	}
	xpt_release_ccb(done_ccb);
}

static int
cherror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
	struct ch_softc *softc;
	struct cam_periph *periph;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct ch_softc *)periph->softc;

	return (cam_periph_error(ccb, cam_flags, sense_flags));
}

static int
chioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	struct cam_periph *periph;
	struct ch_softc *softc;
	int error;

	periph = (struct cam_periph *)dev->si_drv1;
	cam_periph_lock(periph);
	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("entering chioctl\n"));

	softc = (struct ch_softc *)periph->softc;

	error = 0;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, 
		  ("trying to do ioctl %#lx\n", cmd));

	/*
	 * If this command can change the device's state, we must
	 * have the device open for writing.
	 */
	switch (cmd) {
	case CHIOGPICKER:
	case CHIOGPARAMS:
	case OCHIOGSTATUS:
	case CHIOGSTATUS:
		break;

	default:
		if ((flag & FWRITE) == 0) {
			cam_periph_unlock(periph);
			return (EBADF);
		}
	}

	switch (cmd) {
	case CHIOMOVE:
		error = chmove(periph, (struct changer_move *)addr);
		break;

	case CHIOEXCHANGE:
		error = chexchange(periph, (struct changer_exchange *)addr);
		break;

	case CHIOPOSITION:
		error = chposition(periph, (struct changer_position *)addr);
		break;

	case CHIOGPICKER:
		*(int *)addr = softc->sc_picker - softc->sc_firsts[CHET_MT];
		break;

	case CHIOSPICKER:
	{
		int new_picker = *(int *)addr;

		if (new_picker > (softc->sc_counts[CHET_MT] - 1)) {
			error = EINVAL;
			break;
		}
		softc->sc_picker = softc->sc_firsts[CHET_MT] + new_picker;
		break;
	}
	case CHIOGPARAMS:
	{
		struct changer_params *cp = (struct changer_params *)addr;

		cp->cp_npickers = softc->sc_counts[CHET_MT];
		cp->cp_nslots = softc->sc_counts[CHET_ST];
		cp->cp_nportals = softc->sc_counts[CHET_IE];
		cp->cp_ndrives = softc->sc_counts[CHET_DT];
		break;
	}
	case CHIOIELEM:
		error = chielem(periph, *(unsigned int *)addr);
		break;

	case OCHIOGSTATUS:
	{
		error = chgetelemstatus(periph, SCSI_REV_2, cmd,
		    (struct changer_element_status_request *)addr);
		break;
	}

	case CHIOGSTATUS:
	{
		int scsi_version;

		scsi_version = chscsiversion(periph);
		if (scsi_version >= SCSI_REV_0) {
			error = chgetelemstatus(periph, scsi_version, cmd,
			    (struct changer_element_status_request *)addr);
	  	}
		else { /* unable to determine the SCSI version */
			cam_periph_unlock(periph);
			return (ENXIO);
		}
		break;
	}

	case CHIOSETVOLTAG:
	{
		error = chsetvoltag(periph,
				    (struct changer_set_voltag_request *) addr);
		break;
	}

	/* Implement prevent/allow? */

	default:
		error = cam_periph_ioctl(periph, cmd, addr, cherror);
		break;
	}

	cam_periph_unlock(periph);
	return (error);
}

static int
chmove(struct cam_periph *periph, struct changer_move *cm)
{
	struct ch_softc *softc;
	u_int16_t fromelem, toelem;
	union ccb *ccb;
	int error;

	error = 0;
	softc = (struct ch_softc *)periph->softc;

	/*
	 * Check arguments.
	 */
	if ((cm->cm_fromtype > CHET_DT) || (cm->cm_totype > CHET_DT))
		return (EINVAL);
	if ((cm->cm_fromunit > (softc->sc_counts[cm->cm_fromtype] - 1)) ||
	    (cm->cm_tounit > (softc->sc_counts[cm->cm_totype] - 1)))
		return (ENODEV);

	/*
	 * Check the request against the changer's capabilities.
	 */
	if ((softc->sc_movemask[cm->cm_fromtype] & (1 << cm->cm_totype)) == 0)
		return (ENODEV);

	/*
	 * Calculate the source and destination elements.
	 */
	fromelem = softc->sc_firsts[cm->cm_fromtype] + cm->cm_fromunit;
	toelem = softc->sc_firsts[cm->cm_totype] + cm->cm_tounit;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_move_medium(&ccb->csio,
			 /* retries */ 1,
			 /* cbfcnp */ chdone,
			 /* tag_action */ MSG_SIMPLE_Q_TAG,
			 /* tea */ softc->sc_picker,
			 /* src */ fromelem,
			 /* dst */ toelem,
			 /* invert */ (cm->cm_flags & CM_INVERT) ? TRUE : FALSE,
			 /* sense_len */ SSD_FULL_SIZE,
			 /* timeout */ CH_TIMEOUT_MOVE_MEDIUM);

	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/CAM_RETRY_SELTO,
				  /*sense_flags*/ SF_RETRY_UA,
				  softc->device_stats);

	xpt_release_ccb(ccb);

	return(error);
}

static int
chexchange(struct cam_periph *periph, struct changer_exchange *ce)
{
	struct ch_softc *softc;
	u_int16_t src, dst1, dst2;
	union ccb *ccb;
	int error;

	error = 0;
	softc = (struct ch_softc *)periph->softc;
	/*
	 * Check arguments.
	 */
	if ((ce->ce_srctype > CHET_DT) || (ce->ce_fdsttype > CHET_DT) ||
	    (ce->ce_sdsttype > CHET_DT))
		return (EINVAL);
	if ((ce->ce_srcunit > (softc->sc_counts[ce->ce_srctype] - 1)) ||
	    (ce->ce_fdstunit > (softc->sc_counts[ce->ce_fdsttype] - 1)) ||
	    (ce->ce_sdstunit > (softc->sc_counts[ce->ce_sdsttype] - 1)))
		return (ENODEV);

	/*
	 * Check the request against the changer's capabilities.
	 */
	if (((softc->sc_exchangemask[ce->ce_srctype] &
	     (1 << ce->ce_fdsttype)) == 0) ||
	    ((softc->sc_exchangemask[ce->ce_fdsttype] &
	     (1 << ce->ce_sdsttype)) == 0))
		return (ENODEV);

	/*
	 * Calculate the source and destination elements.
	 */
	src = softc->sc_firsts[ce->ce_srctype] + ce->ce_srcunit;
	dst1 = softc->sc_firsts[ce->ce_fdsttype] + ce->ce_fdstunit;
	dst2 = softc->sc_firsts[ce->ce_sdsttype] + ce->ce_sdstunit;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_exchange_medium(&ccb->csio,
			     /* retries */ 1,
			     /* cbfcnp */ chdone,
			     /* tag_action */ MSG_SIMPLE_Q_TAG,
			     /* tea */ softc->sc_picker,
			     /* src */ src,
			     /* dst1 */ dst1,
			     /* dst2 */ dst2,
			     /* invert1 */ (ce->ce_flags & CE_INVERT1) ?
			                   TRUE : FALSE,
			     /* invert2 */ (ce->ce_flags & CE_INVERT2) ?
			                   TRUE : FALSE,
			     /* sense_len */ SSD_FULL_SIZE,
			     /* timeout */ CH_TIMEOUT_EXCHANGE_MEDIUM);

	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/CAM_RETRY_SELTO,
				  /*sense_flags*/ SF_RETRY_UA,
				  softc->device_stats);

	xpt_release_ccb(ccb);

	return(error);
}

static int
chposition(struct cam_periph *periph, struct changer_position *cp)
{
	struct ch_softc *softc;
	u_int16_t dst;
	union ccb *ccb;
	int error;

	error = 0;
	softc = (struct ch_softc *)periph->softc;

	/*
	 * Check arguments.
	 */
	if (cp->cp_type > CHET_DT)
		return (EINVAL);
	if (cp->cp_unit > (softc->sc_counts[cp->cp_type] - 1))
		return (ENODEV);

	/*
	 * Calculate the destination element.
	 */
	dst = softc->sc_firsts[cp->cp_type] + cp->cp_unit;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_position_to_element(&ccb->csio,
				 /* retries */ 1,
				 /* cbfcnp */ chdone,
				 /* tag_action */ MSG_SIMPLE_Q_TAG,
				 /* tea */ softc->sc_picker,
				 /* dst */ dst,
				 /* invert */ (cp->cp_flags & CP_INVERT) ?
					      TRUE : FALSE,
				 /* sense_len */ SSD_FULL_SIZE,
				 /* timeout */ CH_TIMEOUT_POSITION_TO_ELEMENT);

	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/ CAM_RETRY_SELTO,
				  /*sense_flags*/ SF_RETRY_UA,
				  softc->device_stats);

	xpt_release_ccb(ccb);

	return(error);
}

/*
 * Copy a volume tag to a volume_tag struct, converting SCSI byte order
 * to host native byte order in the volume serial number.  The volume
 * label as returned by the changer is transferred to user mode as
 * nul-terminated string.  Volume labels are truncated at the first
 * space, as suggested by SCSI-2.
 */
static	void
copy_voltag(struct changer_voltag *uvoltag, struct volume_tag *voltag)
{
	int i;
	for (i=0; i<CH_VOLTAG_MAXLEN; i++) {
		char c = voltag->vif[i];
		if (c && c != ' ')
			uvoltag->cv_volid[i] = c;
	        else
			break;
	}
	uvoltag->cv_serial = scsi_2btoul(voltag->vsn);
}

/*
 * Copy an element status descriptor to a user-mode
 * changer_element_status structure.
 */
static void
copy_element_status(struct ch_softc *softc,
		    u_int16_t flags,
		    struct read_element_status_descriptor *desc,
		    struct changer_element_status *ces,
		    int scsi_version)
{
	u_int16_t eaddr = scsi_2btoul(desc->eaddr);
	u_int16_t et;
	struct volume_tag *pvol_tag = NULL, *avol_tag = NULL;
	struct read_element_status_device_id *devid = NULL;

	ces->ces_int_addr = eaddr;
	/* set up logical address in element status */
	for (et = CHET_MT; et <= CHET_DT; et++) {
		if ((softc->sc_firsts[et] <= eaddr)
		    && ((softc->sc_firsts[et] + softc->sc_counts[et])
			> eaddr)) {
			ces->ces_addr = eaddr - softc->sc_firsts[et];
			ces->ces_type = et;
			break;
		}
	}

	ces->ces_flags = desc->flags1;

	ces->ces_sensecode = desc->sense_code;
	ces->ces_sensequal = desc->sense_qual;

	if (desc->flags2 & READ_ELEMENT_STATUS_INVERT)
		ces->ces_flags |= CES_INVERT;

	if (desc->flags2 & READ_ELEMENT_STATUS_SVALID) {

		eaddr = scsi_2btoul(desc->ssea);

		/* convert source address to logical format */
		for (et = CHET_MT; et <= CHET_DT; et++) {
			if ((softc->sc_firsts[et] <= eaddr)
			    && ((softc->sc_firsts[et] + softc->sc_counts[et])
				> eaddr)) {
				ces->ces_source_addr =
					eaddr - softc->sc_firsts[et];
				ces->ces_source_type = et;
				ces->ces_flags |= CES_SOURCE_VALID;
				break;
			}
		}

		if (!(ces->ces_flags & CES_SOURCE_VALID))
			printf("ch: warning: could not map element source "
			       "address %ud to a valid element type\n",
			       eaddr);
	}

	/*
	 * pvoltag and avoltag are common between SCSI-2 and later versions
	 */
	if (flags & READ_ELEMENT_STATUS_PVOLTAG)
		pvol_tag = &desc->voltag_devid.pvoltag;
	if (flags & READ_ELEMENT_STATUS_AVOLTAG)
		avol_tag = (flags & READ_ELEMENT_STATUS_PVOLTAG) ?
		    &desc->voltag_devid.voltag[1] :&desc->voltag_devid.pvoltag;
	/*
	 * For SCSI-3 and later, element status can carry designator and
	 * other information.
	 */
	if (scsi_version >= SCSI_REV_SPC) {
		if ((flags & READ_ELEMENT_STATUS_PVOLTAG) ^
		    (flags & READ_ELEMENT_STATUS_AVOLTAG))
			devid = &desc->voltag_devid.pvol_and_devid.devid;
		else if (!(flags & READ_ELEMENT_STATUS_PVOLTAG) &&
			 !(flags & READ_ELEMENT_STATUS_AVOLTAG))
			devid = &desc->voltag_devid.devid;
		else /* Have both PVOLTAG and AVOLTAG */
			devid = &desc->voltag_devid.vol_tags_and_devid.devid;
	}

	if (pvol_tag)
		copy_voltag(&(ces->ces_pvoltag), pvol_tag);
	if (avol_tag)
		copy_voltag(&(ces->ces_pvoltag), avol_tag);
	if (devid != NULL) {
		if (devid->designator_length > 0) {
			bcopy((void *)devid->designator,
			      (void *)ces->ces_designator,
			      devid->designator_length);
			ces->ces_designator_length = devid->designator_length;
			/*
			 * Make sure we are always NUL terminated.  The
			 * This won't matter for the binary code set,
			 * since the user will only pay attention to the
			 * length field.
			 */
			ces->ces_designator[devid->designator_length]= '\0';
		}
		if (devid->piv_assoc_designator_type &
		    READ_ELEMENT_STATUS_PIV_SET) {
			ces->ces_flags |= CES_PIV;
			ces->ces_protocol_id =
			    READ_ELEMENT_STATUS_PROTOCOL_ID(
			    devid->prot_code_set);
		}
		ces->ces_code_set =
		    READ_ELEMENT_STATUS_CODE_SET(devid->prot_code_set);
		ces->ces_assoc = READ_ELEMENT_STATUS_ASSOCIATION(
		    devid->piv_assoc_designator_type);
		ces->ces_designator_type = READ_ELEMENT_STATUS_DESIGNATOR_TYPE(
		    devid->piv_assoc_designator_type);
	} else if (scsi_version > SCSI_REV_2) {
		/* SCSI-SPC and No devid, no designator */
		ces->ces_designator_length = 0;
		ces->ces_designator[0] = '\0';
		ces->ces_protocol_id = CES_PROTOCOL_ID_FCP_4;
	}

	if (scsi_version <= SCSI_REV_2) {
		if (desc->dt_or_obsolete.scsi_2.dt_scsi_flags &
		    READ_ELEMENT_STATUS_DT_IDVALID) {
			ces->ces_flags |= CES_SCSIID_VALID;
			ces->ces_scsi_id =
			    desc->dt_or_obsolete.scsi_2.dt_scsi_addr;
		}

		if (desc->dt_or_obsolete.scsi_2.dt_scsi_addr &
		    READ_ELEMENT_STATUS_DT_LUVALID) {
			ces->ces_flags |= CES_LUN_VALID;
			ces->ces_scsi_lun =
			    desc->dt_or_obsolete.scsi_2.dt_scsi_flags &
			    READ_ELEMENT_STATUS_DT_LUNMASK;
		}
	}
}

static int
chgetelemstatus(struct cam_periph *periph, int scsi_version, u_long cmd,
		struct changer_element_status_request *cesr)
{
	struct read_element_status_header *st_hdr;
	struct read_element_status_page_header *pg_hdr;
	struct read_element_status_descriptor *desc;
	caddr_t data = NULL;
	size_t size, desclen;
	u_int avail, i;
	int curdata, dvcid, sense_flags;
	int try_no_dvcid = 0;
	struct changer_element_status *user_data = NULL;
	struct ch_softc *softc;
	union ccb *ccb;
	int chet = cesr->cesr_element_type;
	int error = 0;
	int want_voltags = (cesr->cesr_flags & CESR_VOLTAGS) ? 1 : 0;

	softc = (struct ch_softc *)periph->softc;

	/* perform argument checking */

	/*
	 * Perform a range check on the cesr_element_{base,count}
	 * request argument fields.
	 */
	if ((softc->sc_counts[chet] - cesr->cesr_element_base) <= 0
	    || (cesr->cesr_element_base + cesr->cesr_element_count)
	        > softc->sc_counts[chet])
		return (EINVAL);

	/*
	 * Request one descriptor for the given element type.  This
	 * is used to determine the size of the descriptor so that
	 * we can allocate enough storage for all of them.  We assume
	 * that the first one can fit into 1k.
	 */
	cam_periph_unlock(periph);
	data = (caddr_t)malloc(1024, M_DEVBUF, M_WAITOK);

	cam_periph_lock(periph);
	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	sense_flags = SF_RETRY_UA;
	if (softc->quirks & CH_Q_NO_DVCID) {
		dvcid = 0;
		curdata = 0;
	} else {
		dvcid = 1;
		curdata = 1;
		/*
		 * Don't print anything for an Illegal Request, because
		 * these flags can cause some changers to complain.  We'll
		 * retry without them if we get an error.
		 */
		sense_flags |= SF_QUIET_IR;
	}

retry_einval:

	scsi_read_element_status(&ccb->csio,
				 /* retries */ 1,
				 /* cbfcnp */ chdone,
				 /* tag_action */ MSG_SIMPLE_Q_TAG,
				 /* voltag */ want_voltags,
				 /* sea */ softc->sc_firsts[chet],
				 /* curdata */ curdata,
				 /* dvcid */ dvcid,
				 /* count */ 1,
				 /* data_ptr */ data,
				 /* dxfer_len */ 1024,
				 /* sense_len */ SSD_FULL_SIZE,
				 /* timeout */ CH_TIMEOUT_READ_ELEMENT_STATUS);

	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/ CAM_RETRY_SELTO,
				  /*sense_flags*/ sense_flags,
				  softc->device_stats);

	/*
	 * An Illegal Request sense key (only used if there is no asc/ascq)
	 * or 0x24,0x00 for an ASC/ASCQ both map to EINVAL.  If dvcid or
	 * curdata are set (we set both or neither), try turning them off
	 * and see if the command is successful.
	 */
	if ((error == EINVAL)
	 && (dvcid || curdata))  {
		dvcid = 0;
		curdata = 0;
		error = 0;
		/* At this point we want to report any Illegal Request */
		sense_flags &= ~SF_QUIET_IR;
		try_no_dvcid = 1;
		goto retry_einval;
	}

	/*
	 * In this case, we tried a read element status with dvcid and
	 * curdata set, and it failed.  We retried without those bits, and
	 * it succeeded.  Suggest to the user that he set a quirk, so we
	 * don't go through the retry process the first time in the future.
	 * This should only happen on changers that claim SCSI-3 or higher,
	 * but don't support these bits.
	 */
	if ((try_no_dvcid != 0)
	 && (error == 0))
		softc->quirks |= CH_Q_NO_DVCID;

	if (error)
		goto done;
	cam_periph_unlock(periph);

	st_hdr = (struct read_element_status_header *)data;
	pg_hdr = (struct read_element_status_page_header *)((uintptr_t)st_hdr +
		  sizeof(struct read_element_status_header));
	desclen = scsi_2btoul(pg_hdr->edl);

	size = sizeof(struct read_element_status_header) +
	       sizeof(struct read_element_status_page_header) +
	       (desclen * cesr->cesr_element_count);
	/*
	 * Reallocate storage for descriptors and get them from the
	 * device.
	 */
	free(data, M_DEVBUF);
	data = (caddr_t)malloc(size, M_DEVBUF, M_WAITOK);

	cam_periph_lock(periph);
	scsi_read_element_status(&ccb->csio,
				 /* retries */ 1,
				 /* cbfcnp */ chdone,
				 /* tag_action */ MSG_SIMPLE_Q_TAG,
				 /* voltag */ want_voltags,
				 /* sea */ softc->sc_firsts[chet]
				 + cesr->cesr_element_base,
				 /* curdata */ curdata,
				 /* dvcid */ dvcid,
				 /* count */ cesr->cesr_element_count,
				 /* data_ptr */ data,
				 /* dxfer_len */ size,
				 /* sense_len */ SSD_FULL_SIZE,
				 /* timeout */ CH_TIMEOUT_READ_ELEMENT_STATUS);

	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/ CAM_RETRY_SELTO,
				  /*sense_flags*/ SF_RETRY_UA,
				  softc->device_stats);

	if (error)
		goto done;
	cam_periph_unlock(periph);

	/*
	 * Fill in the user status array.
	 */
	st_hdr = (struct read_element_status_header *)data;
	pg_hdr = (struct read_element_status_page_header *)((uintptr_t)st_hdr +
		  sizeof(struct read_element_status_header));
	avail = scsi_2btoul(st_hdr->count);

	if (avail != cesr->cesr_element_count) {
		xpt_print(periph->path,
		    "warning, READ ELEMENT STATUS avail != count\n");
	}

	user_data = (struct changer_element_status *)
		malloc(avail * sizeof(struct changer_element_status),
		       M_DEVBUF, M_WAITOK | M_ZERO);

	desc = (struct read_element_status_descriptor *)((uintptr_t)data +
		sizeof(struct read_element_status_header) +
		sizeof(struct read_element_status_page_header));
	/*
	 * Set up the individual element status structures
	 */
	for (i = 0; i < avail; ++i) {
		struct changer_element_status *ces;

		/*
		 * In the changer_element_status structure, fields from
		 * the beginning to the field of ces_scsi_lun are common
		 * between SCSI-2 and SCSI-3, while all the rest are new
		 * from SCSI-3. In order to maintain backward compatibility
		 * of the chio command, the ces pointer, below, is computed
		 * such that it lines up with the structure boundary
		 * corresponding to the SCSI version.
		 */
		ces = cmd == OCHIOGSTATUS ?
		    (struct changer_element_status *)
		    ((unsigned char *)user_data + i *
		     (offsetof(struct changer_element_status,ces_scsi_lun)+1)):
		    &user_data[i];

		copy_element_status(softc, pg_hdr->flags, desc,
				    ces, scsi_version);

		desc = (struct read_element_status_descriptor *)
		       ((unsigned char *)desc + desclen);
	}

	/* Copy element status structures out to userspace. */
	if (cmd == OCHIOGSTATUS)
		error = copyout(user_data,
				cesr->cesr_element_status,
				avail* (offsetof(struct changer_element_status,
				ces_scsi_lun) + 1));
	else
		error = copyout(user_data,
				cesr->cesr_element_status,
				avail * sizeof(struct changer_element_status));

	cam_periph_lock(periph);

 done:
	xpt_release_ccb(ccb);

	if (data != NULL)
		free(data, M_DEVBUF);
	if (user_data != NULL)
		free(user_data, M_DEVBUF);

	return (error);
}

static int
chielem(struct cam_periph *periph,
	unsigned int timeout)
{
	union ccb *ccb;
	struct ch_softc *softc;
	int error;

	if (!timeout) {
		timeout = CH_TIMEOUT_INITIALIZE_ELEMENT_STATUS;
	} else {
		timeout *= 1000;
	}

	error = 0;
	softc = (struct ch_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_initialize_element_status(&ccb->csio,
				      /* retries */ 1,
				      /* cbfcnp */ chdone,
				      /* tag_action */ MSG_SIMPLE_Q_TAG,
				      /* sense_len */ SSD_FULL_SIZE,
				      /* timeout */ timeout);

	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/ CAM_RETRY_SELTO,
				  /*sense_flags*/ SF_RETRY_UA,
				  softc->device_stats);

	xpt_release_ccb(ccb);

	return(error);
}

static int
chsetvoltag(struct cam_periph *periph,
	    struct changer_set_voltag_request *csvr)
{
	union ccb *ccb;
	struct ch_softc *softc;
	u_int16_t ea;
	u_int8_t sac;
	struct scsi_send_volume_tag_parameters ssvtp;
	int error;
	int i;

	error = 0;
	softc = (struct ch_softc *)periph->softc;

	bzero(&ssvtp, sizeof(ssvtp));
	for (i=0; i<sizeof(ssvtp.vitf); i++) {
		ssvtp.vitf[i] = ' ';
	}

	/*
	 * Check arguments.
	 */
	if (csvr->csvr_type > CHET_DT)
		return EINVAL;
	if (csvr->csvr_addr > (softc->sc_counts[csvr->csvr_type] - 1))
		return ENODEV;

	ea = softc->sc_firsts[csvr->csvr_type] + csvr->csvr_addr;

	if (csvr->csvr_flags & CSVR_ALTERNATE) {
		switch (csvr->csvr_flags & CSVR_MODE_MASK) {
		case CSVR_MODE_SET:
			sac = SEND_VOLUME_TAG_ASSERT_ALTERNATE;
			break;
		case CSVR_MODE_REPLACE:
			sac = SEND_VOLUME_TAG_REPLACE_ALTERNATE;
			break;
		case CSVR_MODE_CLEAR:
			sac = SEND_VOLUME_TAG_UNDEFINED_ALTERNATE;
			break;
		default:
			error = EINVAL;
			goto out;
		}
	} else {
		switch (csvr->csvr_flags & CSVR_MODE_MASK) {
		case CSVR_MODE_SET:
			sac = SEND_VOLUME_TAG_ASSERT_PRIMARY;
			break;
		case CSVR_MODE_REPLACE:
			sac = SEND_VOLUME_TAG_REPLACE_PRIMARY;
			break;
		case CSVR_MODE_CLEAR:
			sac = SEND_VOLUME_TAG_UNDEFINED_PRIMARY;
			break;
		default:
			error = EINVAL;
			goto out;
		}
	}

	memcpy(ssvtp.vitf, csvr->csvr_voltag.cv_volid,
	       min(strlen(csvr->csvr_voltag.cv_volid), sizeof(ssvtp.vitf)));
	scsi_ulto2b(csvr->csvr_voltag.cv_serial, ssvtp.minvsn);

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	scsi_send_volume_tag(&ccb->csio,
			     /* retries */ 1,
			     /* cbfcnp */ chdone,
			     /* tag_action */ MSG_SIMPLE_Q_TAG,
			     /* element_address */ ea,
			     /* send_action_code */ sac,
			     /* parameters */ &ssvtp,
			     /* sense_len */ SSD_FULL_SIZE,
			     /* timeout */ CH_TIMEOUT_SEND_VOLTAG);
	
	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/ CAM_RETRY_SELTO,
				  /*sense_flags*/ SF_RETRY_UA,
				  softc->device_stats);

	xpt_release_ccb(ccb);

 out:
	return error;
}

static int
chgetparams(struct cam_periph *periph)
{
	union ccb *ccb;
	struct ch_softc *softc;
	void *mode_buffer;
	int mode_buffer_len;
	struct page_element_address_assignment *ea;
	struct page_device_capabilities *cap;
	int error, from, dbd;
	u_int8_t *moves, *exchanges;

	error = 0;

	softc = (struct ch_softc *)periph->softc;

	ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);

	/*
	 * The scsi_mode_sense_data structure is just a convenience
	 * structure that allows us to easily calculate the worst-case
	 * storage size of the mode sense buffer.
	 */
	mode_buffer_len = sizeof(struct scsi_mode_sense_data);

	mode_buffer = malloc(mode_buffer_len, M_SCSICH, M_NOWAIT);

	if (mode_buffer == NULL) {
		printf("chgetparams: couldn't malloc mode sense data\n");
		xpt_release_ccb(ccb);
		return(ENOSPC);
	}

	bzero(mode_buffer, mode_buffer_len);

	if (softc->quirks & CH_Q_NO_DBD)
		dbd = FALSE;
	else
		dbd = TRUE;

	/*
	 * Get the element address assignment page.
	 */
	scsi_mode_sense(&ccb->csio,
			/* retries */ 1,
			/* cbfcnp */ chdone,
			/* tag_action */ MSG_SIMPLE_Q_TAG,
			/* dbd */ dbd,
			/* pc */ SMS_PAGE_CTRL_CURRENT,
			/* page */ CH_ELEMENT_ADDR_ASSIGN_PAGE,
			/* param_buf */ (u_int8_t *)mode_buffer,
			/* param_len */ mode_buffer_len,
			/* sense_len */ SSD_FULL_SIZE,
			/* timeout */ CH_TIMEOUT_MODE_SENSE);

	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/ CAM_RETRY_SELTO,
				  /* sense_flags */ SF_RETRY_UA|SF_NO_PRINT,
				  softc->device_stats);

	if (error) {
		if (dbd) {
			struct scsi_mode_sense_6 *sms;

			sms = (struct scsi_mode_sense_6 *)
				ccb->csio.cdb_io.cdb_bytes;

			sms->byte2 &= ~SMS_DBD;
			error = cam_periph_runccb(ccb, cherror,
						  /*cam_flags*/ CAM_RETRY_SELTO,
				  		  /*sense_flags*/ SF_RETRY_UA,
						  softc->device_stats);
		} else {
			/*
			 * Since we disabled sense printing above, print
			 * out the sense here since we got an error.
			 */
			scsi_sense_print(&ccb->csio);
		}

		if (error) {
			xpt_print(periph->path,
			    "chgetparams: error getting element "
			    "address page\n");
			xpt_release_ccb(ccb);
			free(mode_buffer, M_SCSICH);
			return(error);
		}
	}

	ea = (struct page_element_address_assignment *)
		find_mode_page_6((struct scsi_mode_header_6 *)mode_buffer);

	softc->sc_firsts[CHET_MT] = scsi_2btoul(ea->mtea);
	softc->sc_counts[CHET_MT] = scsi_2btoul(ea->nmte);
	softc->sc_firsts[CHET_ST] = scsi_2btoul(ea->fsea);
	softc->sc_counts[CHET_ST] = scsi_2btoul(ea->nse);
	softc->sc_firsts[CHET_IE] = scsi_2btoul(ea->fieea);
	softc->sc_counts[CHET_IE] = scsi_2btoul(ea->niee);
	softc->sc_firsts[CHET_DT] = scsi_2btoul(ea->fdtea);
	softc->sc_counts[CHET_DT] = scsi_2btoul(ea->ndte);

	bzero(mode_buffer, mode_buffer_len);

	/*
	 * Now get the device capabilities page.
	 */
	scsi_mode_sense(&ccb->csio,
			/* retries */ 1,
			/* cbfcnp */ chdone,
			/* tag_action */ MSG_SIMPLE_Q_TAG,
			/* dbd */ dbd,
			/* pc */ SMS_PAGE_CTRL_CURRENT,
			/* page */ CH_DEVICE_CAP_PAGE,
			/* param_buf */ (u_int8_t *)mode_buffer,
			/* param_len */ mode_buffer_len,
			/* sense_len */ SSD_FULL_SIZE,
			/* timeout */ CH_TIMEOUT_MODE_SENSE);
	
	error = cam_periph_runccb(ccb, cherror, /*cam_flags*/ CAM_RETRY_SELTO,
				  /* sense_flags */ SF_RETRY_UA | SF_NO_PRINT,
				  softc->device_stats);

	if (error) {
		if (dbd) {
			struct scsi_mode_sense_6 *sms;

			sms = (struct scsi_mode_sense_6 *)
				ccb->csio.cdb_io.cdb_bytes;

			sms->byte2 &= ~SMS_DBD;
			error = cam_periph_runccb(ccb, cherror,
						  /*cam_flags*/ CAM_RETRY_SELTO,
				  		  /*sense_flags*/ SF_RETRY_UA,
						  softc->device_stats);
		} else {
			/*
			 * Since we disabled sense printing above, print
			 * out the sense here since we got an error.
			 */
			scsi_sense_print(&ccb->csio);
		}

		if (error) {
			xpt_print(periph->path,
			    "chgetparams: error getting device "
			    "capabilities page\n");
			xpt_release_ccb(ccb);
			free(mode_buffer, M_SCSICH);
			return(error);
		}
	}

	xpt_release_ccb(ccb);

	cap = (struct page_device_capabilities *)
		find_mode_page_6((struct scsi_mode_header_6 *)mode_buffer);

	bzero(softc->sc_movemask, sizeof(softc->sc_movemask));
	bzero(softc->sc_exchangemask, sizeof(softc->sc_exchangemask));
	moves = cap->move_from;
	exchanges = cap->exchange_with;
	for (from = CHET_MT; from <= CHET_MAX; ++from) {
		softc->sc_movemask[from] = moves[from];
		softc->sc_exchangemask[from] = exchanges[from];
	}

	free(mode_buffer, M_SCSICH);

	return(error);
}

static int
chscsiversion(struct cam_periph *periph)
{
	struct scsi_inquiry_data *inq_data;
	struct ccb_getdev *cgd;
	int dev_scsi_version;

	cam_periph_assert(periph, MA_OWNED);
	if ((cgd = (struct ccb_getdev *)xpt_alloc_ccb_nowait()) == NULL)
		return (-1);
	/*
	 * Get the device information.
	 */
	xpt_setup_ccb(&cgd->ccb_h,
		      periph->path,
		      CAM_PRIORITY_NORMAL);
	cgd->ccb_h.func_code = XPT_GDEV_TYPE;
	xpt_action((union ccb *)cgd);

	if (cgd->ccb_h.status != CAM_REQ_CMP) {
		xpt_free_ccb((union ccb *)cgd);
		return -1;
	}

	inq_data = &cgd->inq_data;
	dev_scsi_version = inq_data->version;
	xpt_free_ccb((union ccb *)cgd);

	return dev_scsi_version;
}

void
scsi_move_medium(struct ccb_scsiio *csio, u_int32_t retries,
		 void (*cbfcnp)(struct cam_periph *, union ccb *),
		 u_int8_t tag_action, u_int32_t tea, u_int32_t src,
		 u_int32_t dst, int invert, u_int8_t sense_len,
		 u_int32_t timeout)
{
	struct scsi_move_medium *scsi_cmd;

	scsi_cmd = (struct scsi_move_medium *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = MOVE_MEDIUM;

	scsi_ulto2b(tea, scsi_cmd->tea);
	scsi_ulto2b(src, scsi_cmd->src);
	scsi_ulto2b(dst, scsi_cmd->dst);

	if (invert)
		scsi_cmd->invert |= MOVE_MEDIUM_INVERT;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/ NULL,
		      /*dxfer_len*/ 0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_exchange_medium(struct ccb_scsiio *csio, u_int32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     u_int8_t tag_action, u_int32_t tea, u_int32_t src,
		     u_int32_t dst1, u_int32_t dst2, int invert1,
		     int invert2, u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_exchange_medium *scsi_cmd;

	scsi_cmd = (struct scsi_exchange_medium *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = EXCHANGE_MEDIUM;

	scsi_ulto2b(tea, scsi_cmd->tea);
	scsi_ulto2b(src, scsi_cmd->src);
	scsi_ulto2b(dst1, scsi_cmd->fdst);
	scsi_ulto2b(dst2, scsi_cmd->sdst);

	if (invert1)
		scsi_cmd->invert |= EXCHANGE_MEDIUM_INV1;

	if (invert2)
		scsi_cmd->invert |= EXCHANGE_MEDIUM_INV2;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/ NULL,
		      /*dxfer_len*/ 0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_position_to_element(struct ccb_scsiio *csio, u_int32_t retries,
			 void (*cbfcnp)(struct cam_periph *, union ccb *),
			 u_int8_t tag_action, u_int32_t tea, u_int32_t dst,
			 int invert, u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_position_to_element *scsi_cmd;

	scsi_cmd = (struct scsi_position_to_element *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = POSITION_TO_ELEMENT;

	scsi_ulto2b(tea, scsi_cmd->tea);
	scsi_ulto2b(dst, scsi_cmd->dst);

	if (invert)
		scsi_cmd->invert |= POSITION_TO_ELEMENT_INVERT;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_NONE,
		      tag_action,
		      /*data_ptr*/ NULL,
		      /*dxfer_len*/ 0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_read_element_status(struct ccb_scsiio *csio, u_int32_t retries,
			 void (*cbfcnp)(struct cam_periph *, union ccb *),
			 u_int8_t tag_action, int voltag, u_int32_t sea,
			 int curdata, int dvcid,
			 u_int32_t count, u_int8_t *data_ptr,
			 u_int32_t dxfer_len, u_int8_t sense_len,
			 u_int32_t timeout)
{
	struct scsi_read_element_status *scsi_cmd;

	scsi_cmd = (struct scsi_read_element_status *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = READ_ELEMENT_STATUS;

	scsi_ulto2b(sea, scsi_cmd->sea);
	scsi_ulto2b(count, scsi_cmd->count);
	scsi_ulto3b(dxfer_len, scsi_cmd->len);
	if (dvcid)
		scsi_cmd->flags |= READ_ELEMENT_STATUS_DVCID;
	if (curdata)
		scsi_cmd->flags |= READ_ELEMENT_STATUS_CURDATA;

	if (voltag)
		scsi_cmd->byte2 |= READ_ELEMENT_STATUS_VOLTAG;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_IN,
		      tag_action,
		      data_ptr,
		      dxfer_len,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_initialize_element_status(struct ccb_scsiio *csio, u_int32_t retries,
			       void (*cbfcnp)(struct cam_periph *, union ccb *),
			       u_int8_t tag_action, u_int8_t sense_len,
			       u_int32_t timeout)
{
	struct scsi_initialize_element_status *scsi_cmd;

	scsi_cmd = (struct scsi_initialize_element_status *)
		    &csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = INITIALIZE_ELEMENT_STATUS;

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_NONE,
		      tag_action,
		      /* data_ptr */ NULL,
		      /* dxfer_len */ 0,
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}

void
scsi_send_volume_tag(struct ccb_scsiio *csio, u_int32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     u_int8_t tag_action, 
		     u_int16_t element_address,
		     u_int8_t send_action_code,
		     struct scsi_send_volume_tag_parameters *parameters,
		     u_int8_t sense_len, u_int32_t timeout)
{
	struct scsi_send_volume_tag *scsi_cmd;

	scsi_cmd = (struct scsi_send_volume_tag *) &csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));

	scsi_cmd->opcode = SEND_VOLUME_TAG;
	scsi_ulto2b(element_address, scsi_cmd->ea);
	scsi_cmd->sac = send_action_code;
	scsi_ulto2b(sizeof(*parameters), scsi_cmd->pll);

	cam_fill_csio(csio,
		      retries,
		      cbfcnp,
		      /*flags*/ CAM_DIR_OUT,
		      tag_action,
		      /* data_ptr */ (u_int8_t *) parameters,
		      sizeof(*parameters),
		      sense_len,
		      sizeof(*scsi_cmd),
		      timeout);
}
