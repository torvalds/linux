/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 2000 Justin T. Gibbs.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/types.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/devicestat.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/sdt.h>
#include <sys/sysent.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_queue.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_debug.h>
#include <cam/cam_compat.h>
#include <cam/cam_xpt_periph.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_pass.h>

typedef enum {
	PASS_FLAG_OPEN			= 0x01,
	PASS_FLAG_LOCKED		= 0x02,
	PASS_FLAG_INVALID		= 0x04,
	PASS_FLAG_INITIAL_PHYSPATH	= 0x08,
	PASS_FLAG_ZONE_INPROG		= 0x10,
	PASS_FLAG_ZONE_VALID		= 0x20,
	PASS_FLAG_UNMAPPED_CAPABLE	= 0x40,
	PASS_FLAG_ABANDONED_REF_SET	= 0x80
} pass_flags;

typedef enum {
	PASS_STATE_NORMAL
} pass_state;

typedef enum {
	PASS_CCB_BUFFER_IO,
	PASS_CCB_QUEUED_IO
} pass_ccb_types;

#define ccb_type	ppriv_field0
#define ccb_ioreq	ppriv_ptr1

/*
 * The maximum number of memory segments we preallocate.
 */
#define	PASS_MAX_SEGS	16

typedef enum {
	PASS_IO_NONE		= 0x00,
	PASS_IO_USER_SEG_MALLOC	= 0x01,
	PASS_IO_KERN_SEG_MALLOC	= 0x02,
	PASS_IO_ABANDONED	= 0x04
} pass_io_flags; 

struct pass_io_req {
	union ccb			 ccb;
	union ccb			*alloced_ccb;
	union ccb			*user_ccb_ptr;
	camq_entry			 user_periph_links;
	ccb_ppriv_area			 user_periph_priv;
	struct cam_periph_map_info	 mapinfo;
	pass_io_flags			 flags;
	ccb_flags			 data_flags;
	int				 num_user_segs;
	bus_dma_segment_t		 user_segs[PASS_MAX_SEGS];
	int				 num_kern_segs;
	bus_dma_segment_t		 kern_segs[PASS_MAX_SEGS];
	bus_dma_segment_t		*user_segptr;
	bus_dma_segment_t		*kern_segptr;
	int				 num_bufs;
	uint32_t			 dirs[CAM_PERIPH_MAXMAPS];
	uint32_t			 lengths[CAM_PERIPH_MAXMAPS];
	uint8_t				*user_bufs[CAM_PERIPH_MAXMAPS];
	uint8_t				*kern_bufs[CAM_PERIPH_MAXMAPS];
	struct bintime			 start_time;
	TAILQ_ENTRY(pass_io_req)	 links;
};

struct pass_softc {
	pass_state		  state;
	pass_flags		  flags;
	u_int8_t		  pd_type;
	union ccb		  saved_ccb;
	int			  open_count;
	u_int		 	  maxio;
	struct devstat		 *device_stats;
	struct cdev		 *dev;
	struct cdev		 *alias_dev;
	struct task		  add_physpath_task;
	struct task		  shutdown_kqueue_task;
	struct selinfo		  read_select;
	TAILQ_HEAD(, pass_io_req) incoming_queue;
	TAILQ_HEAD(, pass_io_req) active_queue;
	TAILQ_HEAD(, pass_io_req) abandoned_queue;
	TAILQ_HEAD(, pass_io_req) done_queue;
	struct cam_periph	 *periph;
	char			  zone_name[12];
	char			  io_zone_name[12];
	uma_zone_t		  pass_zone;
	uma_zone_t		  pass_io_zone;
	size_t			  io_zone_size;
};

static	d_open_t	passopen;
static	d_close_t	passclose;
static	d_ioctl_t	passioctl;
static	d_ioctl_t	passdoioctl;
static	d_poll_t	passpoll;
static	d_kqfilter_t	passkqfilter;
static	void		passreadfiltdetach(struct knote *kn);
static	int		passreadfilt(struct knote *kn, long hint);

static	periph_init_t	passinit;
static	periph_ctor_t	passregister;
static	periph_oninv_t	passoninvalidate;
static	periph_dtor_t	passcleanup;
static	periph_start_t	passstart;
static	void		pass_shutdown_kqueue(void *context, int pending);
static	void		pass_add_physpath(void *context, int pending);
static	void		passasync(void *callback_arg, u_int32_t code,
				  struct cam_path *path, void *arg);
static	void		passdone(struct cam_periph *periph, 
				 union ccb *done_ccb);
static	int		passcreatezone(struct cam_periph *periph);
static	void		passiocleanup(struct pass_softc *softc, 
				      struct pass_io_req *io_req);
static	int		passcopysglist(struct cam_periph *periph,
				       struct pass_io_req *io_req,
				       ccb_flags direction);
static	int		passmemsetup(struct cam_periph *periph,
				     struct pass_io_req *io_req);
static	int		passmemdone(struct cam_periph *periph,
				    struct pass_io_req *io_req);
static	int		passerror(union ccb *ccb, u_int32_t cam_flags, 
				  u_int32_t sense_flags);
static 	int		passsendccb(struct cam_periph *periph, union ccb *ccb,
				    union ccb *inccb);

static struct periph_driver passdriver =
{
	passinit, "pass",
	TAILQ_HEAD_INITIALIZER(passdriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(pass, passdriver);

static struct cdevsw pass_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_TRACKCLOSE,
	.d_open =	passopen,
	.d_close =	passclose,
	.d_ioctl =	passioctl,
	.d_poll = 	passpoll,
	.d_kqfilter = 	passkqfilter,
	.d_name =	"pass",
};

static struct filterops passread_filtops = {
	.f_isfd	=	1,
	.f_detach =	passreadfiltdetach,
	.f_event =	passreadfilt
};

static MALLOC_DEFINE(M_SCSIPASS, "scsi_pass", "scsi passthrough buffers");

static void
passinit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, passasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("pass: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}

}

static void
passrejectios(struct cam_periph *periph)
{
	struct pass_io_req *io_req, *io_req2;
	struct pass_softc *softc;

	softc = (struct pass_softc *)periph->softc;

	/*
	 * The user can no longer get status for I/O on the done queue, so
	 * clean up all outstanding I/O on the done queue.
	 */
	TAILQ_FOREACH_SAFE(io_req, &softc->done_queue, links, io_req2) {
		TAILQ_REMOVE(&softc->done_queue, io_req, links);
		passiocleanup(softc, io_req);
		uma_zfree(softc->pass_zone, io_req);
	}

	/*
	 * The underlying device is gone, so we can't issue these I/Os.
	 * The devfs node has been shut down, so we can't return status to
	 * the user.  Free any I/O left on the incoming queue.
	 */
	TAILQ_FOREACH_SAFE(io_req, &softc->incoming_queue, links, io_req2) {
		TAILQ_REMOVE(&softc->incoming_queue, io_req, links);
		passiocleanup(softc, io_req);
		uma_zfree(softc->pass_zone, io_req);
	}

	/*
	 * Normally we would put I/Os on the abandoned queue and acquire a
	 * reference when we saw the final close.  But, the device went
	 * away and devfs may have moved everything off to deadfs by the
	 * time the I/O done callback is called; as a result, we won't see
	 * any more closes.  So, if we have any active I/Os, we need to put
	 * them on the abandoned queue.  When the abandoned queue is empty,
	 * we'll release the remaining reference (see below) to the peripheral.
	 */
	TAILQ_FOREACH_SAFE(io_req, &softc->active_queue, links, io_req2) {
		TAILQ_REMOVE(&softc->active_queue, io_req, links);
		io_req->flags |= PASS_IO_ABANDONED;
		TAILQ_INSERT_TAIL(&softc->abandoned_queue, io_req, links);
	}

	/*
	 * If we put any I/O on the abandoned queue, acquire a reference.
	 */
	if ((!TAILQ_EMPTY(&softc->abandoned_queue))
	 && ((softc->flags & PASS_FLAG_ABANDONED_REF_SET) == 0)) {
		cam_periph_doacquire(periph);
		softc->flags |= PASS_FLAG_ABANDONED_REF_SET;
	}
}

static void
passdevgonecb(void *arg)
{
	struct cam_periph *periph;
	struct mtx *mtx;
	struct pass_softc *softc;
	int i;

	periph = (struct cam_periph *)arg;
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	softc = (struct pass_softc *)periph->softc;
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
	 * Accordingly, inform all queued I/Os of their fate.
	 */
	cam_periph_release_locked(periph);
	passrejectios(periph);

	/*
	 * We reference the SIM lock directly here, instead of using
	 * cam_periph_unlock().  The reason is that the final call to
	 * cam_periph_release_locked() above could result in the periph
	 * getting freed.  If that is the case, dereferencing the periph
	 * with a cam_periph_unlock() call would cause a page fault.
	 */
	mtx_unlock(mtx);

	/*
	 * We have to remove our kqueue context from a thread because it
	 * may sleep.  It would be nice if we could get a callback from
	 * kqueue when it is done cleaning up resources.
	 */
	taskqueue_enqueue(taskqueue_thread, &softc->shutdown_kqueue_task);
}

static void
passoninvalidate(struct cam_periph *periph)
{
	struct pass_softc *softc;

	softc = (struct pass_softc *)periph->softc;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, passasync, periph, periph->path);

	softc->flags |= PASS_FLAG_INVALID;

	/*
	 * Tell devfs this device has gone away, and ask for a callback
	 * when it has cleaned up its state.
	 */
	destroy_dev_sched_cb(softc->dev, passdevgonecb, periph);
}

static void
passcleanup(struct cam_periph *periph)
{
	struct pass_softc *softc;

	softc = (struct pass_softc *)periph->softc;

	cam_periph_assert(periph, MA_OWNED);
	KASSERT(TAILQ_EMPTY(&softc->active_queue),
		("%s called when there are commands on the active queue!\n",
		__func__));
	KASSERT(TAILQ_EMPTY(&softc->abandoned_queue),
		("%s called when there are commands on the abandoned queue!\n",
		__func__));
	KASSERT(TAILQ_EMPTY(&softc->incoming_queue),
		("%s called when there are commands on the incoming queue!\n",
		__func__));
	KASSERT(TAILQ_EMPTY(&softc->done_queue),
		("%s called when there are commands on the done queue!\n",
		__func__));

	devstat_remove_entry(softc->device_stats);

	cam_periph_unlock(periph);

	/*
	 * We call taskqueue_drain() for the physpath task to make sure it
	 * is complete.  We drop the lock because this can potentially
	 * sleep.  XXX KDM that is bad.  Need a way to get a callback when
	 * a taskqueue is drained.
	 *
 	 * Note that we don't drain the kqueue shutdown task queue.  This
	 * is because we hold a reference on the periph for kqueue, and
	 * release that reference from the kqueue shutdown task queue.  So
	 * we cannot come into this routine unless we've released that
	 * reference.  Also, because that could be the last reference, we
	 * could be called from the cam_periph_release() call in
	 * pass_shutdown_kqueue().  In that case, the taskqueue_drain()
	 * would deadlock.  It would be preferable if we had a way to
	 * get a callback when a taskqueue is done.
	 */
	taskqueue_drain(taskqueue_thread, &softc->add_physpath_task);

	cam_periph_lock(periph);

	free(softc, M_DEVBUF);
}

static void
pass_shutdown_kqueue(void *context, int pending)
{
	struct cam_periph *periph;
	struct pass_softc *softc;

	periph = context;
	softc = periph->softc;

	knlist_clear(&softc->read_select.si_note, /*is_locked*/ 0);
	knlist_destroy(&softc->read_select.si_note);

	/*
	 * Release the reference we held for kqueue.
	 */
	cam_periph_release(periph);
}

static void
pass_add_physpath(void *context, int pending)
{
	struct cam_periph *periph;
	struct pass_softc *softc;
	struct mtx *mtx;
	char *physpath;

	/*
	 * If we have one, create a devfs alias for our
	 * physical path.
	 */
	periph = context;
	softc = periph->softc;
	physpath = malloc(MAXPATHLEN, M_DEVBUF, M_WAITOK);
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	if (periph->flags & CAM_PERIPH_INVALID)
		goto out;

	if (xpt_getattr(physpath, MAXPATHLEN,
			"GEOM::physpath", periph->path) == 0
	 && strlen(physpath) != 0) {

		mtx_unlock(mtx);
		make_dev_physpath_alias(MAKEDEV_WAITOK, &softc->alias_dev,
					softc->dev, softc->alias_dev, physpath);
		mtx_lock(mtx);
	}

out:
	/*
	 * Now that we've made our alias, we no longer have to have a
	 * reference to the device.
	 */
	if ((softc->flags & PASS_FLAG_INITIAL_PHYSPATH) == 0)
		softc->flags |= PASS_FLAG_INITIAL_PHYSPATH;

	/*
	 * We always acquire a reference to the periph before queueing this
	 * task queue function, so it won't go away before we run.
	 */
	while (pending-- > 0)
		cam_periph_release_locked(periph);
	mtx_unlock(mtx);

	free(physpath, M_DEVBUF);
}

static void
passasync(void *callback_arg, u_int32_t code,
	  struct cam_path *path, void *arg)
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

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(passregister, passoninvalidate,
					  passcleanup, passstart, "pass",
					  CAM_PERIPH_BIO, path,
					  passasync, AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG) {
			const struct cam_status_entry *entry;

			entry = cam_fetch_status_entry(status);

			printf("passasync: Unable to attach new device "
			       "due to status %#x: %s\n", status, entry ?
			       entry->status_text : "Unknown");
		}

		break;
	}
	case AC_ADVINFO_CHANGED:
	{
		uintptr_t buftype;

		buftype = (uintptr_t)arg;
		if (buftype == CDAI_TYPE_PHYS_PATH) {
			struct pass_softc *softc;

			softc = (struct pass_softc *)periph->softc;
			/*
			 * Acquire a reference to the periph before we
			 * start the taskqueue, so that we don't run into
			 * a situation where the periph goes away before
			 * the task queue has a chance to run.
			 */
			if (cam_periph_acquire(periph) != 0)
				break;

			taskqueue_enqueue(taskqueue_thread,
					  &softc->add_physpath_task);
		}
		break;
	}
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static cam_status
passregister(struct cam_periph *periph, void *arg)
{
	struct pass_softc *softc;
	struct ccb_getdev *cgd;
	struct ccb_pathinq cpi;
	struct make_dev_args args;
	int error, no_tags;

	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("%s: no getdev CCB, can't register device\n", __func__);
		return(CAM_REQ_CMP_ERR);
	}

	softc = (struct pass_softc *)malloc(sizeof(*softc),
					    M_DEVBUF, M_NOWAIT);

	if (softc == NULL) {
		printf("%s: Unable to probe new device. "
		       "Unable to allocate softc\n", __func__);
		return(CAM_REQ_CMP_ERR);
	}

	bzero(softc, sizeof(*softc));
	softc->state = PASS_STATE_NORMAL;
	if (cgd->protocol == PROTO_SCSI || cgd->protocol == PROTO_ATAPI)
		softc->pd_type = SID_TYPE(&cgd->inq_data);
	else if (cgd->protocol == PROTO_SATAPM)
		softc->pd_type = T_ENCLOSURE;
	else
		softc->pd_type = T_DIRECT;

	periph->softc = softc;
	softc->periph = periph;
	TAILQ_INIT(&softc->incoming_queue);
	TAILQ_INIT(&softc->active_queue);
	TAILQ_INIT(&softc->abandoned_queue);
	TAILQ_INIT(&softc->done_queue);
	snprintf(softc->zone_name, sizeof(softc->zone_name), "%s%d",
		 periph->periph_name, periph->unit_number);
	snprintf(softc->io_zone_name, sizeof(softc->io_zone_name), "%s%dIO",
		 periph->periph_name, periph->unit_number);
	softc->io_zone_size = MAXPHYS;
	knlist_init_mtx(&softc->read_select.si_note, cam_periph_mtx(periph));

	xpt_path_inq(&cpi, periph->path);

	if (cpi.maxio == 0)
		softc->maxio = DFLTPHYS;	/* traditional default */
	else if (cpi.maxio > MAXPHYS)
		softc->maxio = MAXPHYS;		/* for safety */
	else
		softc->maxio = cpi.maxio;	/* real value */

	if (cpi.hba_misc & PIM_UNMAPPED)
		softc->flags |= PASS_FLAG_UNMAPPED_CAPABLE;

	/*
	 * We pass in 0 for a blocksize, since we don't 
	 * know what the blocksize of this device is, if 
	 * it even has a blocksize.
	 */
	cam_periph_unlock(periph);
	no_tags = (cgd->inq_data.flags & SID_CmdQue) == 0;
	softc->device_stats = devstat_new_entry("pass",
			  periph->unit_number, 0,
			  DEVSTAT_NO_BLOCKSIZE
			  | (no_tags ? DEVSTAT_NO_ORDERED_TAGS : 0),
			  softc->pd_type |
			  XPORT_DEVSTAT_TYPE(cpi.transport) |
			  DEVSTAT_TYPE_PASS,
			  DEVSTAT_PRIORITY_PASS);

	/*
	 * Initialize the taskqueue handler for shutting down kqueue.
	 */
	TASK_INIT(&softc->shutdown_kqueue_task, /*priority*/ 0,
		  pass_shutdown_kqueue, periph);

	/*
	 * Acquire a reference to the periph that we can release once we've
	 * cleaned up the kqueue.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}

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
	args.mda_devsw = &pass_cdevsw;
	args.mda_unit = periph->unit_number;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0600;
	args.mda_si_drv1 = periph;
	error = make_dev_s(&args, &softc->dev, "%s%d", periph->periph_name,
	    periph->unit_number);
	if (error != 0) {
		cam_periph_lock(periph);
		cam_periph_release_locked(periph);
		return (CAM_REQ_CMP_ERR);
	}

	/*
	 * Hold a reference to the periph before we create the physical
	 * path alias so it can't go away.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
			  "registration!\n", __func__);
		cam_periph_lock(periph);
		return (CAM_REQ_CMP_ERR);
	}

	cam_periph_lock(periph);

	TASK_INIT(&softc->add_physpath_task, /*priority*/0,
		  pass_add_physpath, periph);

	/*
	 * See if physical path information is already available.
	 */
	taskqueue_enqueue(taskqueue_thread, &softc->add_physpath_task);

	/*
	 * Add an async callback so that we get notified if
	 * this device goes away or its physical path
	 * (stored in the advanced info data of the EDT) has
	 * changed.
	 */
	xpt_register_async(AC_LOST_DEVICE | AC_ADVINFO_CHANGED,
			   passasync, periph, periph->path);

	if (bootverbose)
		xpt_announce_periph(periph, NULL);

	return(CAM_REQ_CMP);
}

static int
passopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct cam_periph *periph;
	struct pass_softc *softc;
	int error;

	periph = (struct cam_periph *)dev->si_drv1;
	if (cam_periph_acquire(periph) != 0)
		return (ENXIO);

	cam_periph_lock(periph);

	softc = (struct pass_softc *)periph->softc;

	if (softc->flags & PASS_FLAG_INVALID) {
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return(ENXIO);
	}

	/*
	 * Don't allow access when we're running at a high securelevel.
	 */
	error = securelevel_gt(td->td_ucred, 1);
	if (error) {
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return(error);
	}

	/*
	 * Only allow read-write access.
	 */
	if (((flags & FWRITE) == 0) || ((flags & FREAD) == 0)) {
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return(EPERM);
	}

	/*
	 * We don't allow nonblocking access.
	 */
	if ((flags & O_NONBLOCK) != 0) {
		xpt_print(periph->path, "can't do nonblocking access\n");
		cam_periph_release_locked(periph);
		cam_periph_unlock(periph);
		return(EINVAL);
	}

	softc->open_count++;

	cam_periph_unlock(periph);

	return (error);
}

static int
passclose(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct 	cam_periph *periph;
	struct  pass_softc *softc;
	struct mtx *mtx;

	periph = (struct cam_periph *)dev->si_drv1;
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	softc = periph->softc;
	softc->open_count--;

	if (softc->open_count == 0) {
		struct pass_io_req *io_req, *io_req2;

		TAILQ_FOREACH_SAFE(io_req, &softc->done_queue, links, io_req2) {
			TAILQ_REMOVE(&softc->done_queue, io_req, links);
			passiocleanup(softc, io_req);
			uma_zfree(softc->pass_zone, io_req);
		}

		TAILQ_FOREACH_SAFE(io_req, &softc->incoming_queue, links,
				   io_req2) {
			TAILQ_REMOVE(&softc->incoming_queue, io_req, links);
			passiocleanup(softc, io_req);
			uma_zfree(softc->pass_zone, io_req);
		}

		/*
		 * If there are any active I/Os, we need to forcibly acquire a
		 * reference to the peripheral so that we don't go away
		 * before they complete.  We'll release the reference when
		 * the abandoned queue is empty.
		 */
		io_req = TAILQ_FIRST(&softc->active_queue);
		if ((io_req != NULL)
		 && (softc->flags & PASS_FLAG_ABANDONED_REF_SET) == 0) {
			cam_periph_doacquire(periph);
			softc->flags |= PASS_FLAG_ABANDONED_REF_SET;
		}

		/*
		 * Since the I/O in the active queue is not under our
		 * control, just set a flag so that we can clean it up when
		 * it completes and put it on the abandoned queue.  This
		 * will prevent our sending spurious completions in the
		 * event that the device is opened again before these I/Os
		 * complete.
		 */
		TAILQ_FOREACH_SAFE(io_req, &softc->active_queue, links,
				   io_req2) {
			TAILQ_REMOVE(&softc->active_queue, io_req, links);
			io_req->flags |= PASS_IO_ABANDONED;
			TAILQ_INSERT_TAIL(&softc->abandoned_queue, io_req,
					  links);
		}
	}

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


static void
passstart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct pass_softc *softc;

	softc = (struct pass_softc *)periph->softc;

	switch (softc->state) {
	case PASS_STATE_NORMAL: {
		struct pass_io_req *io_req;

		/*
		 * Check for any queued I/O requests that require an
		 * allocated slot.
		 */
		io_req = TAILQ_FIRST(&softc->incoming_queue);
		if (io_req == NULL) {
			xpt_release_ccb(start_ccb);
			break;
		}
		TAILQ_REMOVE(&softc->incoming_queue, io_req, links);
		TAILQ_INSERT_TAIL(&softc->active_queue, io_req, links);
		/*
		 * Merge the user's CCB into the allocated CCB.
		 */
		xpt_merge_ccb(start_ccb, &io_req->ccb);
		start_ccb->ccb_h.ccb_type = PASS_CCB_QUEUED_IO;
		start_ccb->ccb_h.ccb_ioreq = io_req;
		start_ccb->ccb_h.cbfcnp = passdone;
		io_req->alloced_ccb = start_ccb;
		binuptime(&io_req->start_time);
		devstat_start_transaction(softc->device_stats,
					  &io_req->start_time);

		xpt_action(start_ccb);

		/*
		 * If we have any more I/O waiting, schedule ourselves again.
		 */
		if (!TAILQ_EMPTY(&softc->incoming_queue))
			xpt_schedule(periph, CAM_PRIORITY_NORMAL);
		break;
	}
	default:
		break;
	}
}

static void
passdone(struct cam_periph *periph, union ccb *done_ccb)
{ 
	struct pass_softc *softc;
	struct ccb_scsiio *csio;

	softc = (struct pass_softc *)periph->softc;

	cam_periph_assert(periph, MA_OWNED);

	csio = &done_ccb->csio;
	switch (csio->ccb_h.ccb_type) {
	case PASS_CCB_QUEUED_IO: {
		struct pass_io_req *io_req;

		io_req = done_ccb->ccb_h.ccb_ioreq;
#if 0
		xpt_print(periph->path, "%s: called for user CCB %p\n",
			  __func__, io_req->user_ccb_ptr);
#endif
		if (((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
		 && (done_ccb->ccb_h.flags & CAM_PASS_ERR_RECOVER)
		 && ((io_req->flags & PASS_IO_ABANDONED) == 0)) {
			int error;

			error = passerror(done_ccb, CAM_RETRY_SELTO,
					  SF_RETRY_UA | SF_NO_PRINT);

			if (error == ERESTART) {
				/*
				 * A retry was scheduled, so
 				 * just return.
				 */
				return;
			}
		}

		/*
		 * Copy the allocated CCB contents back to the malloced CCB
		 * so we can give status back to the user when he requests it.
		 */
		bcopy(done_ccb, &io_req->ccb, sizeof(*done_ccb));

		/*
		 * Log data/transaction completion with devstat(9).
		 */
		switch (done_ccb->ccb_h.func_code) {
		case XPT_SCSI_IO:
			devstat_end_transaction(softc->device_stats,
			    done_ccb->csio.dxfer_len - done_ccb->csio.resid,
			    done_ccb->csio.tag_action & 0x3,
			    ((done_ccb->ccb_h.flags & CAM_DIR_MASK) ==
			    CAM_DIR_NONE) ? DEVSTAT_NO_DATA :
			    (done_ccb->ccb_h.flags & CAM_DIR_OUT) ?
			    DEVSTAT_WRITE : DEVSTAT_READ, NULL,
			    &io_req->start_time);
			break;
		case XPT_ATA_IO:
			devstat_end_transaction(softc->device_stats,
			    done_ccb->ataio.dxfer_len - done_ccb->ataio.resid,
			    0, /* Not used in ATA */
			    ((done_ccb->ccb_h.flags & CAM_DIR_MASK) ==
			    CAM_DIR_NONE) ? DEVSTAT_NO_DATA : 
			    (done_ccb->ccb_h.flags & CAM_DIR_OUT) ?
			    DEVSTAT_WRITE : DEVSTAT_READ, NULL,
			    &io_req->start_time);
			break;
		case XPT_SMP_IO:
			/*
			 * XXX KDM this isn't quite right, but there isn't
			 * currently an easy way to represent a bidirectional 
			 * transfer in devstat.  The only way to do it
			 * and have the byte counts come out right would
			 * mean that we would have to record two
			 * transactions, one for the request and one for the
			 * response.  For now, so that we report something,
			 * just treat the entire thing as a read.
			 */
			devstat_end_transaction(softc->device_stats,
			    done_ccb->smpio.smp_request_len +
			    done_ccb->smpio.smp_response_len,
			    DEVSTAT_TAG_SIMPLE, DEVSTAT_READ, NULL,
			    &io_req->start_time);
			break;
		default:
			devstat_end_transaction(softc->device_stats, 0,
			    DEVSTAT_TAG_NONE, DEVSTAT_NO_DATA, NULL,
			    &io_req->start_time);
			break;
		}

		/*
		 * In the normal case, take the completed I/O off of the
		 * active queue and put it on the done queue.  Notitfy the
		 * user that we have a completed I/O.
		 */
		if ((io_req->flags & PASS_IO_ABANDONED) == 0) {
			TAILQ_REMOVE(&softc->active_queue, io_req, links);
			TAILQ_INSERT_TAIL(&softc->done_queue, io_req, links);
			selwakeuppri(&softc->read_select, PRIBIO);
			KNOTE_LOCKED(&softc->read_select.si_note, 0);
		} else {
			/*
			 * In the case of an abandoned I/O (final close
			 * without fetching the I/O), take it off of the
			 * abandoned queue and free it.
			 */
			TAILQ_REMOVE(&softc->abandoned_queue, io_req, links);
			passiocleanup(softc, io_req);
			uma_zfree(softc->pass_zone, io_req);

			/*
			 * Release the done_ccb here, since we may wind up
			 * freeing the peripheral when we decrement the
			 * reference count below.
			 */
			xpt_release_ccb(done_ccb);

			/*
			 * If the abandoned queue is empty, we can release
			 * our reference to the periph since we won't have
			 * any more completions coming.
			 */
			if ((TAILQ_EMPTY(&softc->abandoned_queue))
			 && (softc->flags & PASS_FLAG_ABANDONED_REF_SET)) {
				softc->flags &= ~PASS_FLAG_ABANDONED_REF_SET;
				cam_periph_release_locked(periph);
			}

			/*
			 * We have already released the CCB, so we can
			 * return.
			 */
			return;
		}
		break;
	}
	}
	xpt_release_ccb(done_ccb);
}

static int
passcreatezone(struct cam_periph *periph)
{
	struct pass_softc *softc;
	int error;

	error = 0;
	softc = (struct pass_softc *)periph->softc;

	cam_periph_assert(periph, MA_OWNED);
	KASSERT(((softc->flags & PASS_FLAG_ZONE_VALID) == 0), 
		("%s called when the pass(4) zone is valid!\n", __func__));
	KASSERT((softc->pass_zone == NULL), 
		("%s called when the pass(4) zone is allocated!\n", __func__));

	if ((softc->flags & PASS_FLAG_ZONE_INPROG) == 0) {

		/*
		 * We're the first context through, so we need to create
		 * the pass(4) UMA zone for I/O requests.
		 */
		softc->flags |= PASS_FLAG_ZONE_INPROG;

		/*
		 * uma_zcreate() does a blocking (M_WAITOK) allocation,
		 * so we cannot hold a mutex while we call it.
		 */
		cam_periph_unlock(periph);

		softc->pass_zone = uma_zcreate(softc->zone_name,
		    sizeof(struct pass_io_req), NULL, NULL, NULL, NULL,
		    /*align*/ 0, /*flags*/ 0);

		softc->pass_io_zone = uma_zcreate(softc->io_zone_name,
		    softc->io_zone_size, NULL, NULL, NULL, NULL,
		    /*align*/ 0, /*flags*/ 0);

		cam_periph_lock(periph);

		if ((softc->pass_zone == NULL)
		 || (softc->pass_io_zone == NULL)) {
			if (softc->pass_zone == NULL)
				xpt_print(periph->path, "unable to allocate "
				    "IO Req UMA zone\n");
			else
				xpt_print(periph->path, "unable to allocate "
				    "IO UMA zone\n");
			softc->flags &= ~PASS_FLAG_ZONE_INPROG;
			goto bailout;
		}

		/*
		 * Set the flags appropriately and notify any other waiters.
		 */
		softc->flags &= PASS_FLAG_ZONE_INPROG;
		softc->flags |= PASS_FLAG_ZONE_VALID;
		wakeup(&softc->pass_zone);
	} else {
		/*
		 * In this case, the UMA zone has not yet been created, but
		 * another context is in the process of creating it.  We
		 * need to sleep until the creation is either done or has
		 * failed.
		 */
		while ((softc->flags & PASS_FLAG_ZONE_INPROG)
		    && ((softc->flags & PASS_FLAG_ZONE_VALID) == 0)) {
			error = msleep(&softc->pass_zone,
				       cam_periph_mtx(periph), PRIBIO,
				       "paszon", 0);
			if (error != 0)
				goto bailout;
		}
		/*
		 * If the zone creation failed, no luck for the user.
		 */
		if ((softc->flags & PASS_FLAG_ZONE_VALID) == 0){
			error = ENOMEM;
			goto bailout;
		}
	}
bailout:
	return (error);
}

static void
passiocleanup(struct pass_softc *softc, struct pass_io_req *io_req)
{
	union ccb *ccb;
	u_int8_t **data_ptrs[CAM_PERIPH_MAXMAPS];
	int i, numbufs;

	ccb = &io_req->ccb;

	switch (ccb->ccb_h.func_code) {
	case XPT_DEV_MATCH:
		numbufs = min(io_req->num_bufs, 2);

		if (numbufs == 1) {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.matches;
		} else {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.patterns;
			data_ptrs[1] = (u_int8_t **)&ccb->cdm.matches;
		}
		break;
	case XPT_SCSI_IO:
	case XPT_CONT_TARGET_IO:
		data_ptrs[0] = &ccb->csio.data_ptr;
		numbufs = min(io_req->num_bufs, 1);
		break;
	case XPT_ATA_IO:
		data_ptrs[0] = &ccb->ataio.data_ptr;
		numbufs = min(io_req->num_bufs, 1);
		break;
	case XPT_SMP_IO:
		numbufs = min(io_req->num_bufs, 2);
		data_ptrs[0] = &ccb->smpio.smp_request;
		data_ptrs[1] = &ccb->smpio.smp_response;
		break;
	case XPT_DEV_ADVINFO:
		numbufs = min(io_req->num_bufs, 1);
		data_ptrs[0] = (uint8_t **)&ccb->cdai.buf;
		break;
	case XPT_NVME_IO:
	case XPT_NVME_ADMIN:
		data_ptrs[0] = &ccb->nvmeio.data_ptr;
		numbufs = min(io_req->num_bufs, 1);
		break;
	default:
		/* allow ourselves to be swapped once again */
		return;
		break; /* NOTREACHED */ 
	}

	if (io_req->flags & PASS_IO_USER_SEG_MALLOC) {
		free(io_req->user_segptr, M_SCSIPASS);
		io_req->user_segptr = NULL;
	}

	/*
	 * We only want to free memory we malloced.
	 */
	if (io_req->data_flags == CAM_DATA_VADDR) {
		for (i = 0; i < io_req->num_bufs; i++) {
			if (io_req->kern_bufs[i] == NULL)
				continue;

			free(io_req->kern_bufs[i], M_SCSIPASS);
			io_req->kern_bufs[i] = NULL;
		}
	} else if (io_req->data_flags == CAM_DATA_SG) {
		for (i = 0; i < io_req->num_kern_segs; i++) {
			if ((uint8_t *)(uintptr_t)
			    io_req->kern_segptr[i].ds_addr == NULL)
				continue;

			uma_zfree(softc->pass_io_zone, (uint8_t *)(uintptr_t)
			    io_req->kern_segptr[i].ds_addr);
			io_req->kern_segptr[i].ds_addr = 0;
		}
	}

	if (io_req->flags & PASS_IO_KERN_SEG_MALLOC) {
		free(io_req->kern_segptr, M_SCSIPASS);
		io_req->kern_segptr = NULL;
	}

	if (io_req->data_flags != CAM_DATA_PADDR) {
		for (i = 0; i < numbufs; i++) {
			/*
			 * Restore the user's buffer pointers to their
			 * previous values.
			 */
			if (io_req->user_bufs[i] != NULL)
				*data_ptrs[i] = io_req->user_bufs[i];
		}
	}

}

static int
passcopysglist(struct cam_periph *periph, struct pass_io_req *io_req,
	       ccb_flags direction)
{
	bus_size_t kern_watermark, user_watermark, len_copied, len_to_copy;
	bus_dma_segment_t *user_sglist, *kern_sglist;
	int i, j, error;

	error = 0;
	kern_watermark = 0;
	user_watermark = 0;
	len_to_copy = 0;
	len_copied = 0;
	user_sglist = io_req->user_segptr;
	kern_sglist = io_req->kern_segptr;

	for (i = 0, j = 0; i < io_req->num_user_segs &&
	     j < io_req->num_kern_segs;) {
		uint8_t *user_ptr, *kern_ptr;

		len_to_copy = min(user_sglist[i].ds_len -user_watermark,
		    kern_sglist[j].ds_len - kern_watermark);

		user_ptr = (uint8_t *)(uintptr_t)user_sglist[i].ds_addr;
		user_ptr = user_ptr + user_watermark;
		kern_ptr = (uint8_t *)(uintptr_t)kern_sglist[j].ds_addr;
		kern_ptr = kern_ptr + kern_watermark;

		user_watermark += len_to_copy;
		kern_watermark += len_to_copy;

		if (!useracc(user_ptr, len_to_copy,
		    (direction == CAM_DIR_IN) ? VM_PROT_WRITE : VM_PROT_READ)) {
			xpt_print(periph->path, "%s: unable to access user "
				  "S/G list element %p len %zu\n", __func__,
				  user_ptr, len_to_copy);
			error = EFAULT;
			goto bailout;
		}

		if (direction == CAM_DIR_IN) {
			error = copyout(kern_ptr, user_ptr, len_to_copy);
			if (error != 0) {
				xpt_print(periph->path, "%s: copyout of %u "
					  "bytes from %p to %p failed with "
					  "error %d\n", __func__, len_to_copy,
					  kern_ptr, user_ptr, error);
				goto bailout;
			}
		} else {
			error = copyin(user_ptr, kern_ptr, len_to_copy);
			if (error != 0) {
				xpt_print(periph->path, "%s: copyin of %u "
					  "bytes from %p to %p failed with "
					  "error %d\n", __func__, len_to_copy,
					  user_ptr, kern_ptr, error);
				goto bailout;
			}
		}

		len_copied += len_to_copy;

		if (user_sglist[i].ds_len == user_watermark) {
			i++;
			user_watermark = 0;
		}

		if (kern_sglist[j].ds_len == kern_watermark) {
			j++;
			kern_watermark = 0;
		}
	}

bailout:

	return (error);
}

static int
passmemsetup(struct cam_periph *periph, struct pass_io_req *io_req)
{
	union ccb *ccb;
	struct pass_softc *softc;
	int numbufs, i;
	uint8_t **data_ptrs[CAM_PERIPH_MAXMAPS];
	uint32_t lengths[CAM_PERIPH_MAXMAPS];
	uint32_t dirs[CAM_PERIPH_MAXMAPS];
	uint32_t num_segs;
	uint16_t *seg_cnt_ptr;
	size_t maxmap;
	int error;

	cam_periph_assert(periph, MA_NOTOWNED);

	softc = periph->softc;

	error = 0;
	ccb = &io_req->ccb;
	maxmap = 0;
	num_segs = 0;
	seg_cnt_ptr = NULL;

	switch(ccb->ccb_h.func_code) {
	case XPT_DEV_MATCH:
		if (ccb->cdm.match_buf_len == 0) {
			printf("%s: invalid match buffer length 0\n", __func__);
			return(EINVAL);
		}
		if (ccb->cdm.pattern_buf_len > 0) {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.patterns;
			lengths[0] = ccb->cdm.pattern_buf_len;
			dirs[0] = CAM_DIR_OUT;
			data_ptrs[1] = (u_int8_t **)&ccb->cdm.matches;
			lengths[1] = ccb->cdm.match_buf_len;
			dirs[1] = CAM_DIR_IN;
			numbufs = 2;
		} else {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.matches;
			lengths[0] = ccb->cdm.match_buf_len;
			dirs[0] = CAM_DIR_IN;
			numbufs = 1;
		}
		io_req->data_flags = CAM_DATA_VADDR;
		break;
	case XPT_SCSI_IO:
	case XPT_CONT_TARGET_IO:
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE)
			return(0);

		/*
		 * The user shouldn't be able to supply a bio.
		 */
		if ((ccb->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_BIO)
			return (EINVAL);

		io_req->data_flags = ccb->ccb_h.flags & CAM_DATA_MASK;

		data_ptrs[0] = &ccb->csio.data_ptr;
		lengths[0] = ccb->csio.dxfer_len;
		dirs[0] = ccb->ccb_h.flags & CAM_DIR_MASK;
		num_segs = ccb->csio.sglist_cnt;
		seg_cnt_ptr = &ccb->csio.sglist_cnt;
		numbufs = 1;
		maxmap = softc->maxio;
		break;
	case XPT_ATA_IO:
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE)
			return(0);

		/*
		 * We only support a single virtual address for ATA I/O.
		 */
		if ((ccb->ccb_h.flags & CAM_DATA_MASK) != CAM_DATA_VADDR)
			return (EINVAL);

		io_req->data_flags = CAM_DATA_VADDR;

		data_ptrs[0] = &ccb->ataio.data_ptr;
		lengths[0] = ccb->ataio.dxfer_len;
		dirs[0] = ccb->ccb_h.flags & CAM_DIR_MASK;
		numbufs = 1;
		maxmap = softc->maxio;
		break;
	case XPT_SMP_IO:
		io_req->data_flags = CAM_DATA_VADDR;

		data_ptrs[0] = &ccb->smpio.smp_request;
		lengths[0] = ccb->smpio.smp_request_len;
		dirs[0] = CAM_DIR_OUT;
		data_ptrs[1] = &ccb->smpio.smp_response;
		lengths[1] = ccb->smpio.smp_response_len;
		dirs[1] = CAM_DIR_IN;
		numbufs = 2;
		maxmap = softc->maxio;
		break;
	case XPT_DEV_ADVINFO:
		if (ccb->cdai.bufsiz == 0)
			return (0);

		io_req->data_flags = CAM_DATA_VADDR;

		data_ptrs[0] = (uint8_t **)&ccb->cdai.buf;
		lengths[0] = ccb->cdai.bufsiz;
		dirs[0] = CAM_DIR_IN;
		numbufs = 1;
		break;
	case XPT_NVME_ADMIN:
	case XPT_NVME_IO:
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE)
			return (0);

		io_req->data_flags = ccb->ccb_h.flags & CAM_DATA_MASK;

		data_ptrs[0] = &ccb->nvmeio.data_ptr;
		lengths[0] = ccb->nvmeio.dxfer_len;
		dirs[0] = ccb->ccb_h.flags & CAM_DIR_MASK;
		num_segs = ccb->nvmeio.sglist_cnt;
		seg_cnt_ptr = &ccb->nvmeio.sglist_cnt;
		numbufs = 1;
		maxmap = softc->maxio;
		break;
	default:
		return(EINVAL);
		break; /* NOTREACHED */
	}

	io_req->num_bufs = numbufs;

	/*
	 * If there is a maximum, check to make sure that the user's
	 * request fits within the limit.  In general, we should only have
	 * a maximum length for requests that go to hardware.  Otherwise it
	 * is whatever we're able to malloc.
	 */
	for (i = 0; i < numbufs; i++) {
		io_req->user_bufs[i] = *data_ptrs[i];
		io_req->dirs[i] = dirs[i];
		io_req->lengths[i] = lengths[i];

		if (maxmap == 0)
			continue;

		if (lengths[i] <= maxmap)
			continue;

		xpt_print(periph->path, "%s: data length %u > max allowed %u "
			  "bytes\n", __func__, lengths[i], maxmap);
		error = EINVAL;
		goto bailout;
	}

	switch (io_req->data_flags) {
	case CAM_DATA_VADDR:
		/* Map or copy the buffer into kernel address space */
		for (i = 0; i < numbufs; i++) {
			uint8_t *tmp_buf;

			/*
			 * If for some reason no length is specified, we
			 * don't need to allocate anything.
			 */
			if (io_req->lengths[i] == 0)
				continue;

			/*
			 * Make sure that the user's buffer is accessible
			 * to that process.
			 */
			if (!useracc(io_req->user_bufs[i], io_req->lengths[i],
			    (io_req->dirs[i] == CAM_DIR_IN) ? VM_PROT_WRITE :
			     VM_PROT_READ)) {
				xpt_print(periph->path, "%s: user address %p "
				    "length %u is not accessible\n", __func__,
				    io_req->user_bufs[i], io_req->lengths[i]);
				error = EFAULT;
				goto bailout;
			}

			tmp_buf = malloc(lengths[i], M_SCSIPASS,
					 M_WAITOK | M_ZERO);
			io_req->kern_bufs[i] = tmp_buf;
			*data_ptrs[i] = tmp_buf;

#if 0
			xpt_print(periph->path, "%s: malloced %p len %u, user "
				  "buffer %p, operation: %s\n", __func__,
				  tmp_buf, lengths[i], io_req->user_bufs[i],
				  (dirs[i] == CAM_DIR_IN) ? "read" : "write");
#endif
			/*
			 * We only need to copy in if the user is writing.
			 */
			if (dirs[i] != CAM_DIR_OUT)
				continue;

			error = copyin(io_req->user_bufs[i],
				       io_req->kern_bufs[i], lengths[i]);
			if (error != 0) {
				xpt_print(periph->path, "%s: copy of user "
					  "buffer from %p to %p failed with "
					  "error %d\n", __func__,
					  io_req->user_bufs[i],
					  io_req->kern_bufs[i], error);
				goto bailout;
			}
		}
		break;
	case CAM_DATA_PADDR:
		/* Pass down the pointer as-is */
		break;
	case CAM_DATA_SG: {
		size_t sg_length, size_to_go, alloc_size;
		uint32_t num_segs_needed;

		/*
		 * Copy the user S/G list in, and then copy in the
		 * individual segments.
		 */
		/*
		 * We shouldn't see this, but check just in case.
		 */
		if (numbufs != 1) {
			xpt_print(periph->path, "%s: cannot currently handle "
				  "more than one S/G list per CCB\n", __func__);
			error = EINVAL;
			goto bailout;
		}

		/*
		 * We have to have at least one segment.
		 */
		if (num_segs == 0) {
			xpt_print(periph->path, "%s: CAM_DATA_SG flag set, "
				  "but sglist_cnt=0!\n", __func__);
			error = EINVAL;
			goto bailout;
		}

		/*
		 * Make sure the user specified the total length and didn't
		 * just leave it to us to decode the S/G list.
		 */
		if (lengths[0] == 0) {
			xpt_print(periph->path, "%s: no dxfer_len specified, "
				  "but CAM_DATA_SG flag is set!\n", __func__);
			error = EINVAL;
			goto bailout;
		}

		/*
		 * We allocate buffers in io_zone_size increments for an
		 * S/G list.  This will generally be MAXPHYS.
		 */
		if (lengths[0] <= softc->io_zone_size)
			num_segs_needed = 1;
		else {
			num_segs_needed = lengths[0] / softc->io_zone_size;
			if ((lengths[0] % softc->io_zone_size) != 0)
				num_segs_needed++;
		}

		/* Figure out the size of the S/G list */
		sg_length = num_segs * sizeof(bus_dma_segment_t);
		io_req->num_user_segs = num_segs;
		io_req->num_kern_segs = num_segs_needed;

		/* Save the user's S/G list pointer for later restoration */
		io_req->user_bufs[0] = *data_ptrs[0];

		/*
		 * If we have enough segments allocated by default to handle
		 * the length of the user's S/G list,
		 */
		if (num_segs > PASS_MAX_SEGS) {
			io_req->user_segptr = malloc(sizeof(bus_dma_segment_t) *
			    num_segs, M_SCSIPASS, M_WAITOK | M_ZERO);
			io_req->flags |= PASS_IO_USER_SEG_MALLOC;
		} else
			io_req->user_segptr = io_req->user_segs;

		if (!useracc(*data_ptrs[0], sg_length, VM_PROT_READ)) {
			xpt_print(periph->path, "%s: unable to access user "
				  "S/G list at %p\n", __func__, *data_ptrs[0]);
			error = EFAULT;
			goto bailout;
		}

		error = copyin(*data_ptrs[0], io_req->user_segptr, sg_length);
		if (error != 0) {
			xpt_print(periph->path, "%s: copy of user S/G list "
				  "from %p to %p failed with error %d\n",
				  __func__, *data_ptrs[0], io_req->user_segptr,
				  error);
			goto bailout;
		}

		if (num_segs_needed > PASS_MAX_SEGS) {
			io_req->kern_segptr = malloc(sizeof(bus_dma_segment_t) *
			    num_segs_needed, M_SCSIPASS, M_WAITOK | M_ZERO);
			io_req->flags |= PASS_IO_KERN_SEG_MALLOC;
		} else {
			io_req->kern_segptr = io_req->kern_segs;
		}

		/*
		 * Allocate the kernel S/G list.
		 */
		for (size_to_go = lengths[0], i = 0;
		     size_to_go > 0 && i < num_segs_needed;
		     i++, size_to_go -= alloc_size) {
			uint8_t *kern_ptr;

			alloc_size = min(size_to_go, softc->io_zone_size);
			kern_ptr = uma_zalloc(softc->pass_io_zone, M_WAITOK);
			io_req->kern_segptr[i].ds_addr =
			    (bus_addr_t)(uintptr_t)kern_ptr;
			io_req->kern_segptr[i].ds_len = alloc_size;
		}
		if (size_to_go > 0) {
			printf("%s: size_to_go = %zu, software error!\n",
			       __func__, size_to_go);
			error = EINVAL;
			goto bailout;
		}

		*data_ptrs[0] = (uint8_t *)io_req->kern_segptr;
		*seg_cnt_ptr = io_req->num_kern_segs;

		/*
		 * We only need to copy data here if the user is writing.
		 */
		if (dirs[0] == CAM_DIR_OUT)
			error = passcopysglist(periph, io_req, dirs[0]);
		break;
	}
	case CAM_DATA_SG_PADDR: {
		size_t sg_length;

		/*
		 * We shouldn't see this, but check just in case.
		 */
		if (numbufs != 1) {
			printf("%s: cannot currently handle more than one "
			       "S/G list per CCB\n", __func__);
			error = EINVAL;
			goto bailout;
		}

		/*
		 * We have to have at least one segment.
		 */
		if (num_segs == 0) {
			xpt_print(periph->path, "%s: CAM_DATA_SG_PADDR flag "
				  "set, but sglist_cnt=0!\n", __func__);
			error = EINVAL;
			goto bailout;
		}

		/*
		 * Make sure the user specified the total length and didn't
		 * just leave it to us to decode the S/G list.
		 */
		if (lengths[0] == 0) {
			xpt_print(periph->path, "%s: no dxfer_len specified, "
				  "but CAM_DATA_SG flag is set!\n", __func__);
			error = EINVAL;
			goto bailout;
		}

		/* Figure out the size of the S/G list */
		sg_length = num_segs * sizeof(bus_dma_segment_t);
		io_req->num_user_segs = num_segs;
		io_req->num_kern_segs = io_req->num_user_segs;

		/* Save the user's S/G list pointer for later restoration */
		io_req->user_bufs[0] = *data_ptrs[0];

		if (num_segs > PASS_MAX_SEGS) {
			io_req->user_segptr = malloc(sizeof(bus_dma_segment_t) *
			    num_segs, M_SCSIPASS, M_WAITOK | M_ZERO);
			io_req->flags |= PASS_IO_USER_SEG_MALLOC;
		} else
			io_req->user_segptr = io_req->user_segs;

		io_req->kern_segptr = io_req->user_segptr;

		error = copyin(*data_ptrs[0], io_req->user_segptr, sg_length);
		if (error != 0) {
			xpt_print(periph->path, "%s: copy of user S/G list "
				  "from %p to %p failed with error %d\n",
				  __func__, *data_ptrs[0], io_req->user_segptr,
				  error);
			goto bailout;
		}
		break;
	}
	default:
	case CAM_DATA_BIO:
		/*
		 * A user shouldn't be attaching a bio to the CCB.  It
		 * isn't a user-accessible structure.
		 */
		error = EINVAL;
		break;
	}

bailout:
	if (error != 0)
		passiocleanup(softc, io_req);

	return (error);
}

static int
passmemdone(struct cam_periph *periph, struct pass_io_req *io_req)
{
	struct pass_softc *softc;
	int error;
	int i;

	error = 0;
	softc = (struct pass_softc *)periph->softc;

	switch (io_req->data_flags) {
	case CAM_DATA_VADDR:
		/*
		 * Copy back to the user buffer if this was a read.
		 */
		for (i = 0; i < io_req->num_bufs; i++) {
			if (io_req->dirs[i] != CAM_DIR_IN)
				continue;

			error = copyout(io_req->kern_bufs[i],
			    io_req->user_bufs[i], io_req->lengths[i]);
			if (error != 0) {
				xpt_print(periph->path, "Unable to copy %u "
					  "bytes from %p to user address %p\n",
					  io_req->lengths[i],
					  io_req->kern_bufs[i],
					  io_req->user_bufs[i]);
				goto bailout;
			}

		}
		break;
	case CAM_DATA_PADDR:
		/* Do nothing.  The pointer is a physical address already */
		break;
	case CAM_DATA_SG:
		/*
		 * Copy back to the user buffer if this was a read.
		 * Restore the user's S/G list buffer pointer.
		 */
		if (io_req->dirs[0] == CAM_DIR_IN)
			error = passcopysglist(periph, io_req, io_req->dirs[0]);
		break;
	case CAM_DATA_SG_PADDR:
		/*
		 * Restore the user's S/G list buffer pointer.  No need to
		 * copy.
		 */
		break;
	default:
	case CAM_DATA_BIO:
		error = EINVAL;
		break;
	}

bailout:
	/*
	 * Reset the user's pointers to their original values and free
	 * allocated memory.
	 */
	passiocleanup(softc, io_req);

	return (error);
}

static int
passioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	int error;

	if ((error = passdoioctl(dev, cmd, addr, flag, td)) == ENOTTY) {
		error = cam_compat_ioctl(dev, cmd, addr, flag, td, passdoioctl);
	}
	return (error);
}

static int
passdoioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	struct	cam_periph *periph;
	struct	pass_softc *softc;
	int	error;
	uint32_t priority;

	periph = (struct cam_periph *)dev->si_drv1;
	cam_periph_lock(periph);
	softc = (struct pass_softc *)periph->softc;

	error = 0;

	switch (cmd) {

	case CAMIOCOMMAND:
	{
		union ccb *inccb;
		union ccb *ccb;
		int ccb_malloced;

		inccb = (union ccb *)addr;
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
		if (inccb->ccb_h.func_code == XPT_SCSI_IO)
			inccb->csio.bio = NULL;
#endif

		if (inccb->ccb_h.flags & CAM_UNLOCKED) {
			error = EINVAL;
			break;
		}

		/*
		 * Some CCB types, like scan bus and scan lun can only go
		 * through the transport layer device.
		 */
		if (inccb->ccb_h.func_code & XPT_FC_XPT_ONLY) {
			xpt_print(periph->path, "CCB function code %#x is "
			    "restricted to the XPT device\n",
			    inccb->ccb_h.func_code);
			error = ENODEV;
			break;
		}

		/* Compatibility for RL/priority-unaware code. */
		priority = inccb->ccb_h.pinfo.priority;
		if (priority <= CAM_PRIORITY_OOB)
		    priority += CAM_PRIORITY_OOB + 1;

		/*
		 * Non-immediate CCBs need a CCB from the per-device pool
		 * of CCBs, which is scheduled by the transport layer.
		 * Immediate CCBs and user-supplied CCBs should just be
		 * malloced.
		 */
		if ((inccb->ccb_h.func_code & XPT_FC_QUEUED)
		 && ((inccb->ccb_h.func_code & XPT_FC_USER_CCB) == 0)) {
			ccb = cam_periph_getccb(periph, priority);
			ccb_malloced = 0;
		} else {
			ccb = xpt_alloc_ccb_nowait();

			if (ccb != NULL)
				xpt_setup_ccb(&ccb->ccb_h, periph->path,
					      priority);
			ccb_malloced = 1;
		}

		if (ccb == NULL) {
			xpt_print(periph->path, "unable to allocate CCB\n");
			error = ENOMEM;
			break;
		}

		error = passsendccb(periph, ccb, inccb);

		if (ccb_malloced)
			xpt_free_ccb(ccb);
		else
			xpt_release_ccb(ccb);

		break;
	}
	case CAMIOQUEUE:
	{
		struct pass_io_req *io_req;
		union ccb **user_ccb, *ccb;
		xpt_opcode fc;

#ifdef COMPAT_FREEBSD32
		if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
			error = ENOTTY;
			goto bailout;
		}
#endif
		if ((softc->flags & PASS_FLAG_ZONE_VALID) == 0) {
			error = passcreatezone(periph);
			if (error != 0)
				goto bailout;
		}

		/*
		 * We're going to do a blocking allocation for this I/O
		 * request, so we have to drop the lock.
		 */
		cam_periph_unlock(periph);

		io_req = uma_zalloc(softc->pass_zone, M_WAITOK | M_ZERO);
		ccb = &io_req->ccb;
		user_ccb = (union ccb **)addr;

		/*
		 * Unlike the CAMIOCOMMAND ioctl above, we only have a
		 * pointer to the user's CCB, so we have to copy the whole
		 * thing in to a buffer we have allocated (above) instead
		 * of allowing the ioctl code to malloc a buffer and copy
		 * it in.
		 *
		 * This is an advantage for this asynchronous interface,
		 * since we don't want the memory to get freed while the
		 * CCB is outstanding.
		 */
#if 0
		xpt_print(periph->path, "Copying user CCB %p to "
			  "kernel address %p\n", *user_ccb, ccb);
#endif
		error = copyin(*user_ccb, ccb, sizeof(*ccb));
		if (error != 0) {
			xpt_print(periph->path, "Copy of user CCB %p to "
				  "kernel address %p failed with error %d\n",
				  *user_ccb, ccb, error);
			goto camioqueue_error;
		}
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
		if (ccb->ccb_h.func_code == XPT_SCSI_IO)
			ccb->csio.bio = NULL;
#endif

		if (ccb->ccb_h.flags & CAM_UNLOCKED) {
			error = EINVAL;
			goto camioqueue_error;
		}

		if (ccb->ccb_h.flags & CAM_CDB_POINTER) {
			if (ccb->csio.cdb_len > IOCDBLEN) {
				error = EINVAL;
				goto camioqueue_error;
			}
			error = copyin(ccb->csio.cdb_io.cdb_ptr,
			    ccb->csio.cdb_io.cdb_bytes, ccb->csio.cdb_len);
			if (error != 0)
				goto camioqueue_error;
			ccb->ccb_h.flags &= ~CAM_CDB_POINTER;
		}

		/*
		 * Some CCB types, like scan bus and scan lun can only go
		 * through the transport layer device.
		 */
		if (ccb->ccb_h.func_code & XPT_FC_XPT_ONLY) {
			xpt_print(periph->path, "CCB function code %#x is "
			    "restricted to the XPT device\n",
			    ccb->ccb_h.func_code);
			error = ENODEV;
			goto camioqueue_error;
		}

		/*
		 * Save the user's CCB pointer as well as his linked list
		 * pointers and peripheral private area so that we can
		 * restore these later.
		 */
		io_req->user_ccb_ptr = *user_ccb;
		io_req->user_periph_links = ccb->ccb_h.periph_links;
		io_req->user_periph_priv = ccb->ccb_h.periph_priv;

		/*
		 * Now that we've saved the user's values, we can set our
		 * own peripheral private entry.
		 */
		ccb->ccb_h.ccb_ioreq = io_req;

		/* Compatibility for RL/priority-unaware code. */
		priority = ccb->ccb_h.pinfo.priority;
		if (priority <= CAM_PRIORITY_OOB)
		    priority += CAM_PRIORITY_OOB + 1;

		/*
		 * Setup fields in the CCB like the path and the priority.
		 * The path in particular cannot be done in userland, since
		 * it is a pointer to a kernel data structure.
		 */
		xpt_setup_ccb_flags(&ccb->ccb_h, periph->path, priority,
				    ccb->ccb_h.flags);

		/*
		 * Setup our done routine.  There is no way for the user to
		 * have a valid pointer here.
		 */
		ccb->ccb_h.cbfcnp = passdone;

		fc = ccb->ccb_h.func_code;
		/*
		 * If this function code has memory that can be mapped in
		 * or out, we need to call passmemsetup().
		 */
		if ((fc == XPT_SCSI_IO) || (fc == XPT_ATA_IO)
		 || (fc == XPT_SMP_IO) || (fc == XPT_DEV_MATCH)
		 || (fc == XPT_DEV_ADVINFO)
		 || (fc == XPT_NVME_ADMIN) || (fc == XPT_NVME_IO)) {
			error = passmemsetup(periph, io_req);
			if (error != 0)
				goto camioqueue_error;
		} else
			io_req->mapinfo.num_bufs_used = 0;

		cam_periph_lock(periph);

		/*
		 * Everything goes on the incoming queue initially.
		 */
		TAILQ_INSERT_TAIL(&softc->incoming_queue, io_req, links);

		/*
		 * If the CCB is queued, and is not a user CCB, then
		 * we need to allocate a slot for it.  Call xpt_schedule()
		 * so that our start routine will get called when a CCB is
		 * available.
		 */
		if ((fc & XPT_FC_QUEUED)
		 && ((fc & XPT_FC_USER_CCB) == 0)) {
			xpt_schedule(periph, priority);
			break;
		} 

		/*
		 * At this point, the CCB in question is either an
		 * immediate CCB (like XPT_DEV_ADVINFO) or it is a user CCB
		 * and therefore should be malloced, not allocated via a slot.
		 * Remove the CCB from the incoming queue and add it to the
		 * active queue.
		 */
		TAILQ_REMOVE(&softc->incoming_queue, io_req, links);
		TAILQ_INSERT_TAIL(&softc->active_queue, io_req, links);

		xpt_action(ccb);

		/*
		 * If this is not a queued CCB (i.e. it is an immediate CCB),
		 * then it is already done.  We need to put it on the done
		 * queue for the user to fetch.
		 */
		if ((fc & XPT_FC_QUEUED) == 0) {
			TAILQ_REMOVE(&softc->active_queue, io_req, links);
			TAILQ_INSERT_TAIL(&softc->done_queue, io_req, links);
		}
		break;

camioqueue_error:
		uma_zfree(softc->pass_zone, io_req);
		cam_periph_lock(periph);
		break;
	}
	case CAMIOGET:
	{
		union ccb **user_ccb;
		struct pass_io_req *io_req;
		int old_error;

#ifdef COMPAT_FREEBSD32
		if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
			error = ENOTTY;
			goto bailout;
		}
#endif
		user_ccb = (union ccb **)addr;
		old_error = 0;

		io_req = TAILQ_FIRST(&softc->done_queue);
		if (io_req == NULL) {
			error = ENOENT;
			break;
		}

		/*
		 * Remove the I/O from the done queue.
		 */
		TAILQ_REMOVE(&softc->done_queue, io_req, links);

		/*
		 * We have to drop the lock during the copyout because the
		 * copyout can result in VM faults that require sleeping.
		 */
		cam_periph_unlock(periph);

		/*
		 * Do any needed copies (e.g. for reads) and revert the
		 * pointers in the CCB back to the user's pointers.
		 */
		error = passmemdone(periph, io_req);

		old_error = error;

		io_req->ccb.ccb_h.periph_links = io_req->user_periph_links;
		io_req->ccb.ccb_h.periph_priv = io_req->user_periph_priv;

#if 0
		xpt_print(periph->path, "Copying to user CCB %p from "
			  "kernel address %p\n", *user_ccb, &io_req->ccb);
#endif

		error = copyout(&io_req->ccb, *user_ccb, sizeof(union ccb));
		if (error != 0) {
			xpt_print(periph->path, "Copy to user CCB %p from "
				  "kernel address %p failed with error %d\n",
				  *user_ccb, &io_req->ccb, error);
		}

		/*
		 * Prefer the first error we got back, and make sure we
		 * don't overwrite bad status with good.
		 */
		if (old_error != 0)
			error = old_error;

		cam_periph_lock(periph);

		/*
		 * At this point, if there was an error, we could potentially
		 * re-queue the I/O and try again.  But why?  The error
		 * would almost certainly happen again.  We might as well
		 * not leak memory.
		 */
		uma_zfree(softc->pass_zone, io_req);
		break;
	}
	default:
		error = cam_periph_ioctl(periph, cmd, addr, passerror);
		break;
	}

bailout:
	cam_periph_unlock(periph);

	return(error);
}

static int
passpoll(struct cdev *dev, int poll_events, struct thread *td)
{
	struct cam_periph *periph;
	struct pass_softc *softc;
	int revents;

	periph = (struct cam_periph *)dev->si_drv1;
	softc = (struct pass_softc *)periph->softc;

	revents = poll_events & (POLLOUT | POLLWRNORM);
	if ((poll_events & (POLLIN | POLLRDNORM)) != 0) {
		cam_periph_lock(periph);

		if (!TAILQ_EMPTY(&softc->done_queue)) {
			revents |= poll_events & (POLLIN | POLLRDNORM);
		}
		cam_periph_unlock(periph);
		if (revents == 0)
			selrecord(td, &softc->read_select);
	}

	return (revents);
}

static int
passkqfilter(struct cdev *dev, struct knote *kn)
{
	struct cam_periph *periph;
	struct pass_softc *softc;

	periph = (struct cam_periph *)dev->si_drv1;
	softc = (struct pass_softc *)periph->softc;

	kn->kn_hook = (caddr_t)periph;
	kn->kn_fop = &passread_filtops;
	knlist_add(&softc->read_select.si_note, kn, 0);

	return (0);
}

static void
passreadfiltdetach(struct knote *kn)
{
	struct cam_periph *periph;
	struct pass_softc *softc;

	periph = (struct cam_periph *)kn->kn_hook;
	softc = (struct pass_softc *)periph->softc;

	knlist_remove(&softc->read_select.si_note, kn, 0);
}

static int
passreadfilt(struct knote *kn, long hint)
{
	struct cam_periph *periph;
	struct pass_softc *softc;
	int retval;

	periph = (struct cam_periph *)kn->kn_hook;
	softc = (struct pass_softc *)periph->softc;

	cam_periph_assert(periph, MA_OWNED);

	if (TAILQ_EMPTY(&softc->done_queue))
		retval = 0;
	else
		retval = 1;

	return (retval);
}

/*
 * Generally, "ccb" should be the CCB supplied by the kernel.  "inccb"
 * should be the CCB that is copied in from the user.
 */
static int
passsendccb(struct cam_periph *periph, union ccb *ccb, union ccb *inccb)
{
	struct pass_softc *softc;
	struct cam_periph_map_info mapinfo;
	uint8_t *cmd;
	xpt_opcode fc;
	int error;

	softc = (struct pass_softc *)periph->softc;

	/*
	 * There are some fields in the CCB header that need to be
	 * preserved, the rest we get from the user.
	 */
	xpt_merge_ccb(ccb, inccb);

	if (ccb->ccb_h.flags & CAM_CDB_POINTER) {
		cmd = __builtin_alloca(ccb->csio.cdb_len);
		error = copyin(ccb->csio.cdb_io.cdb_ptr, cmd, ccb->csio.cdb_len);
		if (error)
			return (error);
		ccb->csio.cdb_io.cdb_ptr = cmd;
	}

	/*
	 * Let cam_periph_mapmem do a sanity check on the data pointer format.
	 * Even if no data transfer is needed, it's a cheap check and it
	 * simplifies the code.
	 */
	fc = ccb->ccb_h.func_code;
	if ((fc == XPT_SCSI_IO) || (fc == XPT_ATA_IO) || (fc == XPT_SMP_IO)
            || (fc == XPT_DEV_MATCH) || (fc == XPT_DEV_ADVINFO) || (fc == XPT_MMC_IO)
            || (fc == XPT_NVME_ADMIN) || (fc == XPT_NVME_IO)) {

		bzero(&mapinfo, sizeof(mapinfo));

		/*
		 * cam_periph_mapmem calls into proc and vm functions that can
		 * sleep as well as trigger I/O, so we can't hold the lock.
		 * Dropping it here is reasonably safe.
		 */
		cam_periph_unlock(periph);
		error = cam_periph_mapmem(ccb, &mapinfo, softc->maxio);
		cam_periph_lock(periph);

		/*
		 * cam_periph_mapmem returned an error, we can't continue.
		 * Return the error to the user.
		 */
		if (error)
			return(error);
	} else
		/* Ensure that the unmap call later on is a no-op. */
		mapinfo.num_bufs_used = 0;

	/*
	 * If the user wants us to perform any error recovery, then honor
	 * that request.  Otherwise, it's up to the user to perform any
	 * error recovery.
	 */
	cam_periph_runccb(ccb, (ccb->ccb_h.flags & CAM_PASS_ERR_RECOVER) ? 
	    passerror : NULL, /* cam_flags */ CAM_RETRY_SELTO,
	    /* sense_flags */ SF_RETRY_UA | SF_NO_PRINT,
	    softc->device_stats);

	cam_periph_unmapmem(ccb, &mapinfo);

	ccb->ccb_h.cbfcnp = NULL;
	ccb->ccb_h.periph_priv = inccb->ccb_h.periph_priv;
	bcopy(ccb, inccb, sizeof(union ccb));

	return(0);
}

static int
passerror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
	struct cam_periph *periph;
	struct pass_softc *softc;

	periph = xpt_path_periph(ccb->ccb_h.path);
	softc = (struct pass_softc *)periph->softc;
	
	return(cam_periph_error(ccb, cam_flags, sense_flags));
}
