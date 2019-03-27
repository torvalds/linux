/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Scott Long
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

/*
 * scsi_sg peripheral driver.  This driver is meant to implement the Linux
 * SG passthrough interface for SCSI.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/devicestat.h>
#include <sys/proc.h>
#include <sys/uio.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_queue.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_sg.h>

#include <compat/linux/linux_ioctl.h>

typedef enum {
	SG_FLAG_LOCKED		= 0x01,
	SG_FLAG_INVALID		= 0x02
} sg_flags;

typedef enum {
	SG_STATE_NORMAL
} sg_state;

typedef enum {
	SG_RDWR_FREE,
	SG_RDWR_INPROG,
	SG_RDWR_DONE
} sg_rdwr_state;

typedef enum {
	SG_CCB_RDWR_IO
} sg_ccb_types;

#define ccb_type	ppriv_field0
#define ccb_rdwr	ppriv_ptr1

struct sg_rdwr {
	TAILQ_ENTRY(sg_rdwr)	rdwr_link;
	int			tag;
	int			state;
	int			buf_len;
	char			*buf;
	union ccb		*ccb;
	union {
		struct sg_header hdr;
		struct sg_io_hdr io_hdr;
	} hdr;
};

struct sg_softc {
	sg_state		state;
	sg_flags		flags;
	int			open_count;
	u_int			maxio;
	struct devstat		*device_stats;
	TAILQ_HEAD(, sg_rdwr)	rdwr_done;
	struct cdev		*dev;
	int			sg_timeout;
	int			sg_user_timeout;
	uint8_t			pd_type;
	union ccb		saved_ccb;
};

static d_open_t		sgopen;
static d_close_t	sgclose;
static d_ioctl_t	sgioctl;
static d_write_t	sgwrite;
static d_read_t		sgread;

static periph_init_t	sginit;
static periph_ctor_t	sgregister;
static periph_oninv_t	sgoninvalidate;
static periph_dtor_t	sgcleanup;
static void		sgasync(void *callback_arg, uint32_t code,
				struct cam_path *path, void *arg);
static void		sgdone(struct cam_periph *periph, union ccb *done_ccb);
static int		sgsendccb(struct cam_periph *periph, union ccb *ccb);
static int		sgsendrdwr(struct cam_periph *periph, union ccb *ccb);
static int		sgerror(union ccb *ccb, uint32_t cam_flags,
				uint32_t sense_flags);
static void		sg_scsiio_status(struct ccb_scsiio *csio,
					 u_short *hoststat, u_short *drvstat);

static int		scsi_group_len(u_char cmd);

static struct periph_driver sgdriver =
{
	sginit, "sg",
	TAILQ_HEAD_INITIALIZER(sgdriver.units), /* gen */ 0
};
PERIPHDRIVER_DECLARE(sg, sgdriver);

static struct cdevsw sg_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT | D_TRACKCLOSE,
	.d_open =	sgopen,
	.d_close =	sgclose,
	.d_ioctl =	sgioctl,
	.d_write =	sgwrite,
	.d_read =	sgread,
	.d_name =	"sg",
};

static int sg_version = 30125;

static void
sginit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will receive aync
	 * callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, sgasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("sg: Failed to attach master async callbac "
			"due to status 0x%x!\n", status);
	}
}

static void
sgdevgonecb(void *arg)
{
	struct cam_periph *periph;
	struct sg_softc *softc;
	struct mtx *mtx;
	int i;

	periph = (struct cam_periph *)arg;
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	softc = (struct sg_softc *)periph->softc;
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
sgoninvalidate(struct cam_periph *periph)
{
	struct sg_softc *softc;

	softc = (struct sg_softc *)periph->softc;

	/*
	 * Deregister any async callbacks.
	 */
	xpt_register_async(0, sgasync, periph, periph->path);

	softc->flags |= SG_FLAG_INVALID;

	/*
	 * Tell devfs this device has gone away, and ask for a callback
	 * when it has cleaned up its state.
	 */
	destroy_dev_sched_cb(softc->dev, sgdevgonecb, periph);

	/*
	 * XXX Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */

}

static void
sgcleanup(struct cam_periph *periph)
{
	struct sg_softc *softc;

	softc = (struct sg_softc *)periph->softc;

	devstat_remove_entry(softc->device_stats);

	free(softc, M_DEVBUF);
}

static void
sgasync(void *callback_arg, uint32_t code, struct cam_path *path, void *arg)
{
	struct cam_periph *periph;

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

		/*
		 * Allocate a peripheral instance for this device and
		 * start the probe process.
		 */
		status = cam_periph_alloc(sgregister, sgoninvalidate,
					  sgcleanup, NULL, "sg",
					  CAM_PERIPH_BIO, path,
					  sgasync, AC_FOUND_DEVICE, cgd);
		if ((status != CAM_REQ_CMP) && (status != CAM_REQ_INPROG)) {
			const struct cam_status_entry *entry;

			entry = cam_fetch_status_entry(status);
			printf("sgasync: Unable to attach new device "
				"due to status %#x: %s\n", status, entry ?
				entry->status_text : "Unknown");
		}
		break;
	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static cam_status
sgregister(struct cam_periph *periph, void *arg)
{
	struct sg_softc *softc;
	struct ccb_getdev *cgd;
	struct ccb_pathinq cpi;
	struct make_dev_args args;
	int no_tags, error;

	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("sgregister: no getdev CCB, can't register device\n");
		return (CAM_REQ_CMP_ERR);
	}

	softc = malloc(sizeof(*softc), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (softc == NULL) {
		printf("sgregister: Unable to allocate softc\n");
		return (CAM_REQ_CMP_ERR);
	}

	softc->state = SG_STATE_NORMAL;
	softc->pd_type = SID_TYPE(&cgd->inq_data);
	softc->sg_timeout = SG_DEFAULT_TIMEOUT / SG_DEFAULT_HZ * hz;
	softc->sg_user_timeout = SG_DEFAULT_TIMEOUT;
	TAILQ_INIT(&softc->rdwr_done);
	periph->softc = softc;

	xpt_path_inq(&cpi, periph->path);

	if (cpi.maxio == 0)
		softc->maxio = DFLTPHYS;	/* traditional default */
	else if (cpi.maxio > MAXPHYS)
		softc->maxio = MAXPHYS;		/* for safety */
	else
		softc->maxio = cpi.maxio;	/* real value */

	/*
	 * We pass in 0 for all blocksize, since we don't know what the
	 * blocksize of the device is, if it even has a blocksize.
	 */
	cam_periph_unlock(periph);
	no_tags = (cgd->inq_data.flags & SID_CmdQue) == 0;
	softc->device_stats = devstat_new_entry("sg",
			periph->unit_number, 0,
			DEVSTAT_NO_BLOCKSIZE
			| (no_tags ? DEVSTAT_NO_ORDERED_TAGS : 0),
			softc->pd_type |
			XPORT_DEVSTAT_TYPE(cpi.transport) |
			DEVSTAT_TYPE_PASS,
			DEVSTAT_PRIORITY_PASS);

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
	args.mda_devsw = &sg_cdevsw;
	args.mda_unit = periph->unit_number;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0600;
	args.mda_si_drv1 = periph;
	error = make_dev_s(&args, &softc->dev, "%s%d",
	    periph->periph_name, periph->unit_number);
	if (error != 0) {
		cam_periph_lock(periph);
		cam_periph_release_locked(periph);
		return (CAM_REQ_CMP_ERR);
	}
	if (periph->unit_number < 26) {
		(void)make_dev_alias(softc->dev, "sg%c",
		    periph->unit_number + 'a');
	} else {
		(void)make_dev_alias(softc->dev, "sg%c%c",
		    ((periph->unit_number / 26) - 1) + 'a',
		    (periph->unit_number % 26) + 'a');
	}
	cam_periph_lock(periph);

	/*
	 * Add as async callback so that we get
	 * notified if this device goes away.
	 */
	xpt_register_async(AC_LOST_DEVICE, sgasync, periph, periph->path);

	if (bootverbose)
		xpt_announce_periph(periph, NULL);

	return (CAM_REQ_CMP);
}

static void
sgdone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct sg_softc *softc;
	struct ccb_scsiio *csio;

	softc = (struct sg_softc *)periph->softc;
	csio = &done_ccb->csio;
	switch (csio->ccb_h.ccb_type) {
	case SG_CCB_RDWR_IO:
	{
		struct sg_rdwr *rdwr;
		int state;

		devstat_end_transaction(softc->device_stats,
					csio->dxfer_len,
					csio->tag_action & 0xf,
					((csio->ccb_h.flags & CAM_DIR_MASK) ==
					CAM_DIR_NONE) ? DEVSTAT_NO_DATA :
					(csio->ccb_h.flags & CAM_DIR_OUT) ?
					DEVSTAT_WRITE : DEVSTAT_READ,
					NULL, NULL);

		rdwr = done_ccb->ccb_h.ccb_rdwr;
		state = rdwr->state;
		rdwr->state = SG_RDWR_DONE;
		wakeup(rdwr);
		break;
	}
	default:
		panic("unknown sg CCB type");
	}
}

static int
sgopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct cam_periph *periph;
	struct sg_softc *softc;
	int error = 0;

	periph = (struct cam_periph *)dev->si_drv1;
	if (cam_periph_acquire(periph) != 0)
		return (ENXIO);

	/*
	 * Don't allow access when we're running at a high securelevel.
	 */
	error = securelevel_gt(td->td_ucred, 1);
	if (error) {
		cam_periph_release(periph);
		return (error);
	}

	cam_periph_lock(periph);

	softc = (struct sg_softc *)periph->softc;
	if (softc->flags & SG_FLAG_INVALID) {
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return (ENXIO);
	}

	softc->open_count++;

	cam_periph_unlock(periph);

	return (error);
}

static int
sgclose(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct cam_periph *periph;
	struct sg_softc   *softc;
	struct mtx *mtx;

	periph = (struct cam_periph *)dev->si_drv1;
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	softc = periph->softc;
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

	return (0);
}

static int
sgioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct thread *td)
{
	union ccb *ccb;
	struct ccb_scsiio *csio;
	struct cam_periph *periph;
	struct sg_softc *softc;
	struct sg_io_hdr *req;
	int dir, error;

	periph = (struct cam_periph *)dev->si_drv1;
	cam_periph_lock(periph);

	softc = (struct sg_softc *)periph->softc;
	error = 0;

	switch (cmd) {
	case SG_GET_VERSION_NUM:
	{
		int *version = (int *)arg;

		*version = sg_version;
		break;
	}
	case SG_SET_TIMEOUT:
	{
		u_int user_timeout = *(u_int *)arg;

		softc->sg_user_timeout = user_timeout;
		softc->sg_timeout = user_timeout / SG_DEFAULT_HZ * hz;
		break;
	}
	case SG_GET_TIMEOUT:
		/*
		 * The value is returned directly to the syscall.
		 */
		td->td_retval[0] = softc->sg_user_timeout;
		error = 0;
		break;
	case SG_IO:
		req = (struct sg_io_hdr *)arg;

		if (req->cmd_len > IOCDBLEN) {
			error = EINVAL;
			break;
		}

		if (req->iovec_count != 0) {
			error = EOPNOTSUPP;
			break;
		}

		ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
		csio = &ccb->csio;

		error = copyin(req->cmdp, &csio->cdb_io.cdb_bytes,
		    req->cmd_len);
		if (error) {
			xpt_release_ccb(ccb);
			break;
		}

		switch(req->dxfer_direction) {
		case SG_DXFER_TO_DEV:
			dir = CAM_DIR_OUT;
			break;
		case SG_DXFER_FROM_DEV:
			dir = CAM_DIR_IN;
			break;
		case SG_DXFER_TO_FROM_DEV:
			dir = CAM_DIR_IN | CAM_DIR_OUT;
			break;
		case SG_DXFER_NONE:
		default:
			dir = CAM_DIR_NONE;
			break;
		}

		cam_fill_csio(csio,
			      /*retries*/1,
			      /*cbfcnp*/NULL,
			      dir|CAM_DEV_QFRZDIS,
			      MSG_SIMPLE_Q_TAG,
			      req->dxferp,
			      req->dxfer_len,
			      req->mx_sb_len,
			      req->cmd_len,
			      req->timeout);

		error = sgsendccb(periph, ccb);
		if (error) {
			req->host_status = DID_ERROR;
			req->driver_status = DRIVER_INVALID;
			xpt_release_ccb(ccb);
			break;
		}

		req->status = csio->scsi_status;
		req->masked_status = (csio->scsi_status >> 1) & 0x7f;
		sg_scsiio_status(csio, &req->host_status, &req->driver_status);
		req->resid = csio->resid;
		req->duration = csio->ccb_h.timeout;
		req->info = 0;

		if ((csio->ccb_h.status & CAM_AUTOSNS_VALID)
		    && (req->sbp != NULL)) {
			req->sb_len_wr = req->mx_sb_len - csio->sense_resid;
			error = copyout(&csio->sense_data, req->sbp,
					req->sb_len_wr);
		}

		xpt_release_ccb(ccb);
		break;
		
	case SG_GET_RESERVED_SIZE:
	{
		int *size = (int *)arg;
		*size = DFLTPHYS;
		break;
	}

	case SG_GET_SCSI_ID:
	{
		struct sg_scsi_id *id = (struct sg_scsi_id *)arg;

		id->host_no = cam_sim_path(xpt_path_sim(periph->path));
		id->channel = xpt_path_path_id(periph->path);
		id->scsi_id = xpt_path_target_id(periph->path);
		id->lun = xpt_path_lun_id(periph->path);
		id->scsi_type = softc->pd_type;
		id->h_cmd_per_lun = 1;
		id->d_queue_depth = 1;
		id->unused[0] = 0;
		id->unused[1] = 0;
		break;
	}

	case SG_GET_SG_TABLESIZE:
	{
		int *size = (int *)arg;
		*size = 0;
		break;
	}

	case SG_EMULATED_HOST:
	case SG_SET_TRANSFORM:
	case SG_GET_TRANSFORM:
	case SG_GET_NUM_WAITING:
	case SG_SCSI_RESET:
	case SG_GET_REQUEST_TABLE:
	case SG_SET_KEEP_ORPHAN:
	case SG_GET_KEEP_ORPHAN:
	case SG_GET_ACCESS_COUNT:
	case SG_SET_FORCE_LOW_DMA:
	case SG_GET_LOW_DMA:
	case SG_SET_FORCE_PACK_ID:
	case SG_GET_PACK_ID:
	case SG_SET_RESERVED_SIZE:
	case SG_GET_COMMAND_Q:
	case SG_SET_COMMAND_Q:
	case SG_SET_DEBUG:
	case SG_NEXT_CMD_LEN:
	default:
#ifdef CAMDEBUG
		printf("sgioctl: rejecting cmd 0x%lx\n", cmd);
#endif
		error = ENODEV;
		break;
	}

	cam_periph_unlock(periph);
	return (error);
}

static int
sgwrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	union ccb *ccb;
	struct cam_periph *periph;
	struct ccb_scsiio *csio;
	struct sg_softc *sc;
	struct sg_header *hdr;
	struct sg_rdwr *rdwr;
	u_char cdb_cmd;
	char *buf;
	int error = 0, cdb_len, buf_len, dir;

	periph = dev->si_drv1;
	rdwr = malloc(sizeof(*rdwr), M_DEVBUF, M_WAITOK | M_ZERO);
	hdr = &rdwr->hdr.hdr;

	/* Copy in the header block and sanity check it */
	if (uio->uio_resid < sizeof(*hdr)) {
		error = EINVAL;
		goto out_hdr;
	}
	error = uiomove(hdr, sizeof(*hdr), uio);
	if (error)
		goto out_hdr;

	/* XXX: We don't support SG 3.x read/write API. */
	if (hdr->reply_len < 0) {
		error = ENODEV;
		goto out_hdr;
	}

	ccb = xpt_alloc_ccb();
	if (ccb == NULL) {
		error = ENOMEM;
		goto out_hdr;
	}
	csio = &ccb->csio;

	/*
	 * Copy in the CDB block.  The designers of the interface didn't
	 * bother to provide a size for this in the header, so we have to
	 * figure it out ourselves.
	 */
	if (uio->uio_resid < 1)
		goto out_ccb;
	error = uiomove(&cdb_cmd, 1, uio);
	if (error)
		goto out_ccb;
	if (hdr->twelve_byte)
		cdb_len = 12;
	else
		cdb_len = scsi_group_len(cdb_cmd);
	/*
	 * We've already read the first byte of the CDB and advanced the uio
	 * pointer.  Just read the rest.
	 */
	csio->cdb_io.cdb_bytes[0] = cdb_cmd;
	error = uiomove(&csio->cdb_io.cdb_bytes[1], cdb_len - 1, uio);
	if (error)
		goto out_ccb;

	/*
	 * Now set up the data block.  Again, the designers didn't bother
	 * to make this reliable.
	 */
	buf_len = uio->uio_resid;
	if (buf_len != 0) {
		buf = malloc(buf_len, M_DEVBUF, M_WAITOK | M_ZERO);
		error = uiomove(buf, buf_len, uio);
		if (error)
			goto out_buf;
		dir = CAM_DIR_OUT;
	} else if (hdr->reply_len != 0) {
		buf = malloc(hdr->reply_len, M_DEVBUF, M_WAITOK | M_ZERO);
		buf_len = hdr->reply_len;
		dir = CAM_DIR_IN;
	} else {
		buf = NULL;
		buf_len = 0;
		dir = CAM_DIR_NONE;
	}

	cam_periph_lock(periph);
	sc = periph->softc;
	xpt_setup_ccb(&ccb->ccb_h, periph->path, CAM_PRIORITY_NORMAL);
	cam_fill_csio(csio,
		      /*retries*/1,
		      sgdone,
		      dir|CAM_DEV_QFRZDIS,
		      MSG_SIMPLE_Q_TAG,
		      buf,
		      buf_len,
		      SG_MAX_SENSE,
		      cdb_len,
		      sc->sg_timeout);

	/*
	 * Send off the command and hope that it works. This path does not
	 * go through sgstart because the I/O is supposed to be asynchronous.
	 */
	rdwr->buf = buf;
	rdwr->buf_len = buf_len;
	rdwr->tag = hdr->pack_id;
	rdwr->ccb = ccb;
	rdwr->state = SG_RDWR_INPROG;
	ccb->ccb_h.ccb_rdwr = rdwr;
	ccb->ccb_h.ccb_type = SG_CCB_RDWR_IO;
	TAILQ_INSERT_TAIL(&sc->rdwr_done, rdwr, rdwr_link);
	error = sgsendrdwr(periph, ccb);
	cam_periph_unlock(periph);
	return (error);

out_buf:
	free(buf, M_DEVBUF);
out_ccb:
	xpt_free_ccb(ccb);
out_hdr:
	free(rdwr, M_DEVBUF);
	return (error);
}

static int
sgread(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct ccb_scsiio *csio;
	struct cam_periph *periph;
	struct sg_softc *sc;
	struct sg_header *hdr;
	struct sg_rdwr *rdwr;
	u_short hstat, dstat;
	int error, pack_len, reply_len, pack_id;

	periph = dev->si_drv1;

	/* XXX The pack len field needs to be updated and written out instead
	 * of discarded.  Not sure how to do that.
	 */
	uio->uio_rw = UIO_WRITE;
	if ((error = uiomove(&pack_len, 4, uio)) != 0)
		return (error);
	if ((error = uiomove(&reply_len, 4, uio)) != 0)
		return (error);
	if ((error = uiomove(&pack_id, 4, uio)) != 0)
		return (error);
	uio->uio_rw = UIO_READ;

	cam_periph_lock(periph);
	sc = periph->softc;
search:
	TAILQ_FOREACH(rdwr, &sc->rdwr_done, rdwr_link) {
		if (rdwr->tag == pack_id)
			break;
	}
	if ((rdwr == NULL) || (rdwr->state != SG_RDWR_DONE)) {
		if (cam_periph_sleep(periph, rdwr, PCATCH, "sgread", 0) == ERESTART)
			return (EAGAIN);
		goto search;
	}
	TAILQ_REMOVE(&sc->rdwr_done, rdwr, rdwr_link);
	cam_periph_unlock(periph);

	hdr = &rdwr->hdr.hdr;
	csio = &rdwr->ccb->csio;
	sg_scsiio_status(csio, &hstat, &dstat);
	hdr->host_status = hstat;
	hdr->driver_status = dstat;
	hdr->target_status = csio->scsi_status >> 1;

	switch (hstat) {
	case DID_OK:
	case DID_PASSTHROUGH:
	case DID_SOFT_ERROR:
		hdr->result = 0;
		break;
	case DID_NO_CONNECT:
	case DID_BUS_BUSY:
	case DID_TIME_OUT:
		hdr->result = EBUSY;
		break;
	case DID_BAD_TARGET:
	case DID_ABORT:
	case DID_PARITY:
	case DID_RESET:
	case DID_BAD_INTR:
	case DID_ERROR:
	default:
		hdr->result = EIO;
		break;
	}

	if (dstat == DRIVER_SENSE) {
		bcopy(&csio->sense_data, hdr->sense_buffer,
		      min(csio->sense_len, SG_MAX_SENSE));
#ifdef CAMDEBUG
		scsi_sense_print(csio);
#endif
	}

	error = uiomove(&hdr->result, sizeof(*hdr) -
			offsetof(struct sg_header, result), uio);
	if ((error == 0) && (hdr->result == 0))
		error = uiomove(rdwr->buf, rdwr->buf_len, uio);

	cam_periph_lock(periph);
	xpt_free_ccb(rdwr->ccb);
	cam_periph_unlock(periph);
	free(rdwr->buf, M_DEVBUF);
	free(rdwr, M_DEVBUF);
	return (error);
}

static int
sgsendccb(struct cam_periph *periph, union ccb *ccb)
{
	struct sg_softc *softc;
	struct cam_periph_map_info mapinfo;
	int error;

	softc = periph->softc;
	bzero(&mapinfo, sizeof(mapinfo));

	/*
	 * cam_periph_mapmem calls into proc and vm functions that can
	 * sleep as well as trigger I/O, so we can't hold the lock.
	 * Dropping it here is reasonably safe.
	 * The only CCB opcode that is possible here is XPT_SCSI_IO, no
	 * need for additional checks.
	 */
	cam_periph_unlock(periph);
	error = cam_periph_mapmem(ccb, &mapinfo, softc->maxio);
	cam_periph_lock(periph);
	if (error)
		return (error);

	error = cam_periph_runccb(ccb,
				  sgerror,
				  CAM_RETRY_SELTO,
				  SF_RETRY_UA,
				  softc->device_stats);

	cam_periph_unmapmem(ccb, &mapinfo);

	return (error);
}

static int
sgsendrdwr(struct cam_periph *periph, union ccb *ccb)
{
	struct sg_softc *softc;

	softc = periph->softc;
	devstat_start_transaction(softc->device_stats, NULL);
	xpt_action(ccb);
	return (0);
}

static int
sgerror(union ccb *ccb, uint32_t cam_flags, uint32_t sense_flags)
{
	struct cam_periph *periph;
	struct sg_softc *softc;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct sg_softc *)periph->softc;

	return (cam_periph_error(ccb, cam_flags, sense_flags));
}

static void
sg_scsiio_status(struct ccb_scsiio *csio, u_short *hoststat, u_short *drvstat)
{
	int status;

	status = csio->ccb_h.status;

	switch (status & CAM_STATUS_MASK) {
	case CAM_REQ_CMP:
		*hoststat = DID_OK;
		*drvstat = 0;
		break;
	case CAM_REQ_CMP_ERR:
		*hoststat = DID_ERROR;
		*drvstat = 0;
		break;
	case CAM_REQ_ABORTED:
		*hoststat = DID_ABORT;
		*drvstat = 0;
		break;
	case CAM_REQ_INVALID:
		*hoststat = DID_ERROR;
		*drvstat = DRIVER_INVALID;
		break;
	case CAM_DEV_NOT_THERE:
		*hoststat = DID_BAD_TARGET;
		*drvstat = 0;
		break;
	case CAM_SEL_TIMEOUT:
		*hoststat = DID_NO_CONNECT;
		*drvstat = 0;
		break;
	case CAM_CMD_TIMEOUT:
		*hoststat = DID_TIME_OUT;
		*drvstat = 0;
		break;
	case CAM_SCSI_STATUS_ERROR:
		*hoststat = DID_ERROR;
		*drvstat = 0;
		break;
	case CAM_SCSI_BUS_RESET:
		*hoststat = DID_RESET;
		*drvstat = 0;
		break;
	case CAM_UNCOR_PARITY:
		*hoststat = DID_PARITY;
		*drvstat = 0;
		break;
	case CAM_SCSI_BUSY:
		*hoststat = DID_BUS_BUSY;
		*drvstat = 0;
		break;
	default:
		*hoststat = DID_ERROR;
		*drvstat = DRIVER_ERROR;
	}

	if (status & CAM_AUTOSNS_VALID)
		*drvstat = DRIVER_SENSE;
}

static int
scsi_group_len(u_char cmd)
{
	int len[] = {6, 10, 10, 12, 12, 12, 10, 10};
	int group;

	group = (cmd >> 5) & 0x7;
	return (len[group]);
}

