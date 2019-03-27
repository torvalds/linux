/*-
 * Generic SCSI Target Kernel Mode Driver
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Nate Lawson.
 * Copyright (c) 1998, 1999, 2001, 2002 Justin T. Gibbs.
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
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/devicestat.h>
#include <sys/proc.h>
/* Includes to support callout */
#include <sys/types.h>
#include <sys/systm.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_sim.h>
#include <cam/scsi/scsi_targetio.h>


/* Transaction information attached to each CCB sent by the user */
struct targ_cmd_descr {
	struct cam_periph_map_info  mapinfo;
	TAILQ_ENTRY(targ_cmd_descr) tqe;
	union ccb *user_ccb;
	int	   priority;
	int	   func_code;
};

/* Offset into the private CCB area for storing our descriptor */
#define targ_descr	periph_priv.entries[1].ptr

TAILQ_HEAD(descr_queue, targ_cmd_descr);

typedef enum {
	TARG_STATE_RESV		= 0x00, /* Invalid state */
	TARG_STATE_OPENED	= 0x01, /* Device opened, softc initialized */
	TARG_STATE_LUN_ENABLED	= 0x02  /* Device enabled for a path */
} targ_state;

/* Per-instance device software context */
struct targ_softc {
	/* CCBs (CTIOs, ATIOs, INOTs) pending on the controller */
	struct ccb_queue	 pending_ccb_queue;

	/* Command descriptors awaiting CTIO resources from the XPT */
	struct descr_queue	 work_queue;

	/* Command descriptors that have been aborted back to the user. */
	struct descr_queue	 abort_queue;

	/*
	 * Queue of CCBs that have been copied out to userland, but our
	 * userland daemon has not yet seen.
	 */
	struct ccb_queue	 user_ccb_queue;

	struct cam_periph	*periph;
	struct cam_path		*path;
	targ_state		 state;
	u_int			 maxio;
	struct selinfo		 read_select;
	struct devstat		 device_stats;
};

static d_open_t		targopen;
static d_read_t		targread;
static d_write_t	targwrite;
static d_ioctl_t	targioctl;
static d_poll_t		targpoll;
static d_kqfilter_t	targkqfilter;
static void		targreadfiltdetach(struct knote *kn);
static int		targreadfilt(struct knote *kn, long hint);
static struct filterops targread_filtops = {
	.f_isfd = 1,
	.f_detach = targreadfiltdetach,
	.f_event = targreadfilt,
};

static struct cdevsw targ_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	targopen,
	.d_read =	targread,
	.d_write =	targwrite,
	.d_ioctl =	targioctl,
	.d_poll =	targpoll,
	.d_name =	"targ",
	.d_kqfilter =	targkqfilter
};

static cam_status	targendislun(struct cam_path *path, int enable,
				     int grp6_len, int grp7_len);
static cam_status	targenable(struct targ_softc *softc,
				   struct cam_path *path,
				   int grp6_len, int grp7_len);
static cam_status	targdisable(struct targ_softc *softc);
static periph_ctor_t    targctor;
static periph_dtor_t    targdtor;
static periph_start_t   targstart;
static int		targusermerge(struct targ_softc *softc,
				      struct targ_cmd_descr *descr,
				      union ccb *ccb);
static int		targsendccb(struct targ_softc *softc, union ccb *ccb,
				    struct targ_cmd_descr *descr);
static void		targdone(struct cam_periph *periph,
				 union  ccb *done_ccb);
static int		targreturnccb(struct targ_softc *softc,
				      union  ccb *ccb);
static union ccb *	targgetccb(struct targ_softc *softc, xpt_opcode type,
				   int priority);
static void		targfreeccb(struct targ_softc *softc, union ccb *ccb);
static struct targ_cmd_descr *
			targgetdescr(struct targ_softc *softc);
static periph_init_t	targinit;
static void		targasync(void *callback_arg, u_int32_t code,
				  struct cam_path *path, void *arg);
static void		abort_all_pending(struct targ_softc *softc);
static void		notify_user(struct targ_softc *softc);
static int		targcamstatus(cam_status status);
static size_t		targccblen(xpt_opcode func_code);

static struct periph_driver targdriver =
{
	targinit, "targ",
	TAILQ_HEAD_INITIALIZER(targdriver.units), /* generation */ 0
};
PERIPHDRIVER_DECLARE(targ, targdriver);

static MALLOC_DEFINE(M_TARG, "TARG", "TARG data");

/* Disable LUN if enabled and teardown softc */
static void
targcdevdtor(void *data)
{
	struct targ_softc *softc;
	struct cam_periph *periph;

	softc = data;
	if (softc->periph == NULL) {
		printf("%s: destroying non-enabled target\n", __func__);
		free(softc, M_TARG);
		return;
	}

	/*
	 * Acquire a hold on the periph so that it doesn't go away before
	 * we are ready at the end of the function.
	 */
	periph = softc->periph;
	cam_periph_acquire(periph);
	cam_periph_lock(periph);
	(void)targdisable(softc);
	if (softc->periph != NULL) {
		cam_periph_invalidate(softc->periph);
		softc->periph = NULL;
	}
	cam_periph_unlock(periph);
	cam_periph_release(periph);
	free(softc, M_TARG);
}

/*
 * Create softc and initialize it.  There is no locking here because a
 * periph doesn't get created until an ioctl is issued to do so, and
 * that can't happen until this method returns.
 */
static int
targopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{
	struct targ_softc *softc;

	/* Allocate its softc, initialize it */
	softc = malloc(sizeof(*softc), M_TARG,
	       M_WAITOK | M_ZERO);
	softc->state = TARG_STATE_OPENED;
	softc->periph = NULL;
	softc->path = NULL;

	TAILQ_INIT(&softc->pending_ccb_queue);
	TAILQ_INIT(&softc->work_queue);
	TAILQ_INIT(&softc->abort_queue);
	TAILQ_INIT(&softc->user_ccb_queue);
	knlist_init_mtx(&softc->read_select.si_note, NULL);

	devfs_set_cdevpriv(softc, targcdevdtor);
	return (0);
}

/* Enable/disable LUNs, set debugging level */
static int
targioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	struct targ_softc *softc;
	cam_status	   status;

	devfs_get_cdevpriv((void **)&softc);

	switch (cmd) {
	case TARGIOCENABLE:
	{
		struct ioc_enable_lun	*new_lun;
		struct cam_path		*path;

		new_lun = (struct ioc_enable_lun *)addr;
		status = xpt_create_path(&path, /*periph*/NULL,
					  new_lun->path_id,
					  new_lun->target_id,
					  new_lun->lun_id);
		if (status != CAM_REQ_CMP) {
			printf("Couldn't create path, status %#x\n", status);
			break;
		}
		xpt_path_lock(path);
		status = targenable(softc, path, new_lun->grp6_len,
				    new_lun->grp7_len);
		xpt_path_unlock(path);
		xpt_free_path(path);
		break;
	}
	case TARGIOCDISABLE:
		if (softc->periph == NULL) {
			status = CAM_DEV_NOT_THERE;
			break;
		}
		cam_periph_lock(softc->periph);
		status = targdisable(softc);
		cam_periph_unlock(softc->periph);
		break;
	case TARGIOCDEBUG:
	{
		struct ccb_debug cdbg;

		/* If no periph available, disallow debugging changes */
		if ((softc->state & TARG_STATE_LUN_ENABLED) == 0) {
			status = CAM_DEV_NOT_THERE;
			break;
		}
		bzero(&cdbg, sizeof cdbg);
		if (*((int *)addr) != 0)
			cdbg.flags = CAM_DEBUG_PERIPH;
		else
			cdbg.flags = CAM_DEBUG_NONE;
		xpt_setup_ccb(&cdbg.ccb_h, softc->path, CAM_PRIORITY_NORMAL);
		cdbg.ccb_h.func_code = XPT_DEBUG;
		cdbg.ccb_h.cbfcnp = targdone;
		xpt_action((union ccb *)&cdbg);
		status = cdbg.ccb_h.status & CAM_STATUS_MASK;
		break;
	}
	default:
		status = CAM_PROVIDE_FAIL;
		break;
	}

	return (targcamstatus(status));
}

/* Writes are always ready, reads wait for user_ccb_queue or abort_queue */
static int
targpoll(struct cdev *dev, int poll_events, struct thread *td)
{
	struct targ_softc *softc;
	int	revents;

	devfs_get_cdevpriv((void **)&softc);

	/* Poll for write() is always ok. */
	revents = poll_events & (POLLOUT | POLLWRNORM);
	if ((poll_events & (POLLIN | POLLRDNORM)) != 0) {
		/* Poll for read() depends on user and abort queues. */
		cam_periph_lock(softc->periph);
		if (!TAILQ_EMPTY(&softc->user_ccb_queue) ||
		    !TAILQ_EMPTY(&softc->abort_queue)) {
			revents |= poll_events & (POLLIN | POLLRDNORM);
		}
		cam_periph_unlock(softc->periph);
		/* Only sleep if the user didn't poll for write. */
		if (revents == 0)
			selrecord(td, &softc->read_select);
	}

	return (revents);
}

static int
targkqfilter(struct cdev *dev, struct knote *kn)
{
	struct  targ_softc *softc;

	devfs_get_cdevpriv((void **)&softc);
	kn->kn_hook = (caddr_t)softc;
	kn->kn_fop = &targread_filtops;
	knlist_add(&softc->read_select.si_note, kn, 0);
	return (0);
}

static void
targreadfiltdetach(struct knote *kn)
{
	struct  targ_softc *softc;

	softc = (struct targ_softc *)kn->kn_hook;
	knlist_remove(&softc->read_select.si_note, kn, 0);
}

/* Notify the user's kqueue when the user queue or abort queue gets a CCB */
static int
targreadfilt(struct knote *kn, long hint)
{
	struct targ_softc *softc;
	int	retval;

	softc = (struct targ_softc *)kn->kn_hook;
	cam_periph_lock(softc->periph);
	retval = !TAILQ_EMPTY(&softc->user_ccb_queue) ||
		 !TAILQ_EMPTY(&softc->abort_queue);
	cam_periph_unlock(softc->periph);
	return (retval);
}

/* Send the HBA the enable/disable message */
static cam_status
targendislun(struct cam_path *path, int enable, int grp6_len, int grp7_len)
{
	struct ccb_en_lun en_ccb;
	cam_status	  status;

	/* Tell the lun to begin answering selects */
	xpt_setup_ccb(&en_ccb.ccb_h, path, CAM_PRIORITY_NORMAL);
	en_ccb.ccb_h.func_code = XPT_EN_LUN;
	/* Don't need support for any vendor specific commands */
	en_ccb.grp6_len = grp6_len;
	en_ccb.grp7_len = grp7_len;
	en_ccb.enable = enable ? 1 : 0;
	xpt_action((union ccb *)&en_ccb);
	status = en_ccb.ccb_h.status & CAM_STATUS_MASK;
	if (status != CAM_REQ_CMP) {
		xpt_print(path, "%sable lun CCB rejected, status %#x\n",
		    enable ? "en" : "dis", status);
	}
	return (status);
}

/* Enable target mode on a LUN, given its path */
static cam_status
targenable(struct targ_softc *softc, struct cam_path *path, int grp6_len,
	   int grp7_len)
{
	struct cam_periph *periph;
	struct ccb_pathinq cpi;
	cam_status	   status;

	if ((softc->state & TARG_STATE_LUN_ENABLED) != 0)
		return (CAM_LUN_ALRDY_ENA);

	/* Make sure SIM supports target mode */
	xpt_path_inq(&cpi, path);
	status = cpi.ccb_h.status & CAM_STATUS_MASK;
	if (status != CAM_REQ_CMP) {
		printf("pathinq failed, status %#x\n", status);
		goto enable_fail;
	}
	if ((cpi.target_sprt & PIT_PROCESSOR) == 0) {
		printf("controller does not support target mode\n");
		status = CAM_FUNC_NOTAVAIL;
		goto enable_fail;
	}
	if (cpi.maxio == 0)
		softc->maxio = DFLTPHYS;	/* traditional default */
	else if (cpi.maxio > MAXPHYS)
		softc->maxio = MAXPHYS;		/* for safety */
	else
		softc->maxio = cpi.maxio;	/* real value */

	/* Destroy any periph on our path if it is disabled */
	periph = cam_periph_find(path, "targ");
	if (periph != NULL) {
		struct targ_softc *del_softc;

		del_softc = (struct targ_softc *)periph->softc;
		if ((del_softc->state & TARG_STATE_LUN_ENABLED) == 0) {
			cam_periph_invalidate(del_softc->periph);
			del_softc->periph = NULL;
		} else {
			printf("Requested path still in use by targ%d\n",
			       periph->unit_number);
			status = CAM_LUN_ALRDY_ENA;
			goto enable_fail;
		}
	}

	/* Create a periph instance attached to this path */
	status = cam_periph_alloc(targctor, NULL, targdtor, targstart,
			"targ", CAM_PERIPH_BIO, path, targasync, 0, softc);
	if (status != CAM_REQ_CMP) {
		printf("cam_periph_alloc failed, status %#x\n", status);
		goto enable_fail;
	}

	/* Ensure that the periph now exists. */
	if (cam_periph_find(path, "targ") == NULL) {
		panic("targenable: succeeded but no periph?");
		/* NOTREACHED */
	}

	/* Send the enable lun message */
	status = targendislun(path, /*enable*/1, grp6_len, grp7_len);
	if (status != CAM_REQ_CMP) {
		printf("enable lun failed, status %#x\n", status);
		goto enable_fail;
	}
	softc->state |= TARG_STATE_LUN_ENABLED;

enable_fail:
	return (status);
}

/* Disable this softc's target instance if enabled */
static cam_status
targdisable(struct targ_softc *softc)
{
	cam_status status;

	if ((softc->state & TARG_STATE_LUN_ENABLED) == 0)
		return (CAM_REQ_CMP);

	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH, ("targdisable\n"));

	/* Abort any ccbs pending on the controller */
	abort_all_pending(softc);

	/* Disable this lun */
	status = targendislun(softc->path, /*enable*/0,
			      /*grp6_len*/0, /*grp7_len*/0);
	if (status == CAM_REQ_CMP)
		softc->state &= ~TARG_STATE_LUN_ENABLED;
	else
		printf("Disable lun failed, status %#x\n", status);

	return (status);
}

/* Initialize a periph (called from cam_periph_alloc) */
static cam_status
targctor(struct cam_periph *periph, void *arg)
{
	struct targ_softc *softc;

	/* Store pointer to softc for periph-driven routines */
	softc = (struct targ_softc *)arg;
	periph->softc = softc;
	softc->periph = periph;
	softc->path = periph->path;
	return (CAM_REQ_CMP);
}

static void
targdtor(struct cam_periph *periph)
{
	struct targ_softc     *softc;
	struct ccb_hdr	      *ccb_h;
	struct targ_cmd_descr *descr;

	softc = (struct targ_softc *)periph->softc;

	/* 
	 * targdisable() aborts CCBs back to the user and leaves them
	 * on user_ccb_queue and abort_queue in case the user is still
	 * interested in them.  We free them now.
	 */
	while ((ccb_h = TAILQ_FIRST(&softc->user_ccb_queue)) != NULL) {
		TAILQ_REMOVE(&softc->user_ccb_queue, ccb_h, periph_links.tqe);
		targfreeccb(softc, (union ccb *)ccb_h);
	}
	while ((descr = TAILQ_FIRST(&softc->abort_queue)) != NULL) {
		TAILQ_REMOVE(&softc->abort_queue, descr, tqe);
		free(descr, M_TARG);
	}

	softc->periph = NULL;
	softc->path = NULL;
	periph->softc = NULL;
}

/* Receive CCBs from user mode proc and send them to the HBA */
static int
targwrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	union ccb *user_ccb;
	struct targ_softc *softc;
	struct targ_cmd_descr *descr;
	int write_len, error;
	int func_code, priority;

	devfs_get_cdevpriv((void **)&softc);
	write_len = error = 0;
	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
		  ("write - uio_resid %zd\n", uio->uio_resid));
	while (uio->uio_resid >= sizeof(user_ccb) && error == 0) {
		union ccb *ccb;

		error = uiomove((caddr_t)&user_ccb, sizeof(user_ccb), uio);
		if (error != 0) {
			CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
				  ("write - uiomove failed (%d)\n", error));
			break;
		}
		priority = fuword32(&user_ccb->ccb_h.pinfo.priority);
		if (priority == CAM_PRIORITY_NONE) {
			error = EINVAL;
			break;
		}
		func_code = fuword32(&user_ccb->ccb_h.func_code);
		switch (func_code) {
		case XPT_ACCEPT_TARGET_IO:
		case XPT_IMMED_NOTIFY:
		case XPT_IMMEDIATE_NOTIFY:
			cam_periph_lock(softc->periph);
			ccb = targgetccb(softc, func_code, priority);
			descr = (struct targ_cmd_descr *)ccb->ccb_h.targ_descr;
			descr->user_ccb = user_ccb;
			descr->func_code = func_code;
			CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
				  ("Sent ATIO/INOT (%p)\n", user_ccb));
			xpt_action(ccb);
			TAILQ_INSERT_TAIL(&softc->pending_ccb_queue,
					  &ccb->ccb_h,
					  periph_links.tqe);
			cam_periph_unlock(softc->periph);
			break;
		default:
			cam_periph_lock(softc->periph);
			if ((func_code & XPT_FC_QUEUED) != 0) {
				CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
					  ("Sending queued ccb %#x (%p)\n",
					  func_code, user_ccb));
				descr = targgetdescr(softc);
				descr->user_ccb = user_ccb;
				descr->priority = priority;
				descr->func_code = func_code;
				TAILQ_INSERT_TAIL(&softc->work_queue,
						  descr, tqe);
				xpt_schedule(softc->periph, priority);
			} else {
				CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
					  ("Sending inline ccb %#x (%p)\n",
					  func_code, user_ccb));
				ccb = targgetccb(softc, func_code, priority);
				descr = (struct targ_cmd_descr *)
					 ccb->ccb_h.targ_descr;
				descr->user_ccb = user_ccb;
				descr->priority = priority;
				descr->func_code = func_code;
				if (targusermerge(softc, descr, ccb) != EFAULT)
					targsendccb(softc, ccb, descr);
				targreturnccb(softc, ccb);
			}
			cam_periph_unlock(softc->periph);
			break;
		}
		write_len += sizeof(user_ccb);
	}
	
	/*
	 * If we've successfully taken in some amount of
	 * data, return success for that data first.  If
	 * an error is persistent, it will be reported
	 * on the next write.
	 */
	if (error != 0 && write_len == 0)
		return (error);
	if (write_len == 0 && uio->uio_resid != 0)
		return (ENOSPC);
	return (0);
}

/* Process requests (descrs) via the periph-supplied CCBs */
static void
targstart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct targ_softc *softc;
	struct targ_cmd_descr *descr, *next_descr;
	int error;

	softc = (struct targ_softc *)periph->softc;
	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH, ("targstart %p\n", start_ccb));

	descr = TAILQ_FIRST(&softc->work_queue);
	if (descr == NULL) {
		xpt_release_ccb(start_ccb);
	} else {
		TAILQ_REMOVE(&softc->work_queue, descr, tqe);
		next_descr = TAILQ_FIRST(&softc->work_queue);

		/* Initiate a transaction using the descr and supplied CCB */
		error = targusermerge(softc, descr, start_ccb);
		if (error == 0)
			error = targsendccb(softc, start_ccb, descr);
		if (error != 0) {
			xpt_print(periph->path,
			    "targsendccb failed, err %d\n", error);
			xpt_release_ccb(start_ccb);
			suword(&descr->user_ccb->ccb_h.status,
			       CAM_REQ_CMP_ERR);
			TAILQ_INSERT_TAIL(&softc->abort_queue, descr, tqe);
			notify_user(softc);
		}

		/* If we have more work to do, stay scheduled */
		if (next_descr != NULL)
			xpt_schedule(periph, next_descr->priority);
	}
}

static int
targusermerge(struct targ_softc *softc, struct targ_cmd_descr *descr,
	      union ccb *ccb)
{
	struct ccb_hdr *u_ccbh, *k_ccbh;
	size_t ccb_len;
	int error;

	u_ccbh = &descr->user_ccb->ccb_h;
	k_ccbh = &ccb->ccb_h;

	/*
	 * There are some fields in the CCB header that need to be
	 * preserved, the rest we get from the user ccb. (See xpt_merge_ccb)
	 */
	xpt_setup_ccb(k_ccbh, softc->path, descr->priority);
	k_ccbh->retry_count = fuword32(&u_ccbh->retry_count);
	k_ccbh->func_code = descr->func_code;
	k_ccbh->flags = fuword32(&u_ccbh->flags);
	k_ccbh->timeout = fuword32(&u_ccbh->timeout);
	ccb_len = targccblen(k_ccbh->func_code) - sizeof(struct ccb_hdr);
	error = copyin(u_ccbh + 1, k_ccbh + 1, ccb_len);
	if (error != 0) {
		k_ccbh->status = CAM_REQ_CMP_ERR;
		return (error);
	}

	/* Translate usermode abort_ccb pointer to its kernel counterpart */
	if (k_ccbh->func_code == XPT_ABORT) {
		struct ccb_abort *cab;
		struct ccb_hdr *ccb_h;

		cab = (struct ccb_abort *)ccb;
		TAILQ_FOREACH(ccb_h, &softc->pending_ccb_queue,
		    periph_links.tqe) {
			struct targ_cmd_descr *ab_descr;

			ab_descr = (struct targ_cmd_descr *)ccb_h->targ_descr;
			if (ab_descr->user_ccb == cab->abort_ccb) {
				CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
					  ("Changing abort for %p to %p\n",
					  cab->abort_ccb, ccb_h));
				cab->abort_ccb = (union ccb *)ccb_h;
				break;
			}
		}
		/* CCB not found, set appropriate status */
		if (ccb_h == NULL) {
			k_ccbh->status = CAM_PATH_INVALID;
			error = ESRCH;
		}
	}

	return (error);
}

/* Build and send a kernel CCB formed from descr->user_ccb */
static int
targsendccb(struct targ_softc *softc, union ccb *ccb,
	    struct targ_cmd_descr *descr)
{
	struct cam_periph_map_info *mapinfo;
	struct ccb_hdr *ccb_h;
	int error;

	ccb_h = &ccb->ccb_h;
	mapinfo = &descr->mapinfo;
	mapinfo->num_bufs_used = 0;

	/*
	 * There's no way for the user to have a completion
	 * function, so we put our own completion function in here.
	 * We also stash in a reference to our descriptor so targreturnccb()
	 * can find our mapping info.
	 */
	ccb_h->cbfcnp = targdone;
	ccb_h->targ_descr = descr;

	if ((ccb_h->func_code == XPT_CONT_TARGET_IO) ||
	    (ccb_h->func_code == XPT_DEV_MATCH)) {

		error = cam_periph_mapmem(ccb, mapinfo, softc->maxio);

		/*
		 * cam_periph_mapmem returned an error, we can't continue.
		 * Return the error to the user.
		 */
		if (error) {
			ccb_h->status = CAM_REQ_CMP_ERR;
			mapinfo->num_bufs_used = 0;
			return (error);
		}
	}

	/*
	 * Once queued on the pending CCB list, this CCB will be protected
	 * by our error recovery handler.
	 */
	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH, ("sendccb %p\n", ccb));
	if (XPT_FC_IS_QUEUED(ccb)) {
		TAILQ_INSERT_TAIL(&softc->pending_ccb_queue, ccb_h,
				  periph_links.tqe);
	}
	xpt_action(ccb);

	return (0);
}

/* Completion routine for CCBs (called at splsoftcam) */
static void
targdone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct targ_softc *softc;
	cam_status status;

	CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH, ("targdone %p\n", done_ccb));
	softc = (struct targ_softc *)periph->softc;
	TAILQ_REMOVE(&softc->pending_ccb_queue, &done_ccb->ccb_h,
		     periph_links.tqe);
	status = done_ccb->ccb_h.status & CAM_STATUS_MASK;

	/* If we're no longer enabled, throw away CCB */
	if ((softc->state & TARG_STATE_LUN_ENABLED) == 0) {
		targfreeccb(softc, done_ccb);
		return;
	}
	/* abort_all_pending() waits for pending queue to be empty */
	if (TAILQ_EMPTY(&softc->pending_ccb_queue))
		wakeup(&softc->pending_ccb_queue);

	switch (done_ccb->ccb_h.func_code) {
	/* All FC_*_QUEUED CCBs go back to userland */
	case XPT_IMMED_NOTIFY:
	case XPT_IMMEDIATE_NOTIFY:
	case XPT_ACCEPT_TARGET_IO:
	case XPT_CONT_TARGET_IO:
		TAILQ_INSERT_TAIL(&softc->user_ccb_queue, &done_ccb->ccb_h,
				  periph_links.tqe);
 		cam_periph_unlock(softc->periph);
		notify_user(softc);
 		cam_periph_lock(softc->periph);
		break;
	default:
		panic("targdone: impossible xpt opcode %#x",
		      done_ccb->ccb_h.func_code);
		/* NOTREACHED */
	}
}

/* Return CCBs to the user from the user queue and abort queue */
static int
targread(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct descr_queue	*abort_queue;
	struct targ_cmd_descr	*user_descr;
	struct targ_softc	*softc;
	struct ccb_queue  *user_queue;
	struct ccb_hdr	  *ccb_h;
	union  ccb	  *user_ccb;
	int		   read_len, error;

	error = 0;
	read_len = 0;
	devfs_get_cdevpriv((void **)&softc);
	user_queue = &softc->user_ccb_queue;
	abort_queue = &softc->abort_queue;
	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH, ("targread\n"));

	/* If no data is available, wait or return immediately */
	cam_periph_lock(softc->periph);
	ccb_h = TAILQ_FIRST(user_queue);
	user_descr = TAILQ_FIRST(abort_queue);
	while (ccb_h == NULL && user_descr == NULL) {
		if ((ioflag & IO_NDELAY) == 0) {
			error = cam_periph_sleep(softc->periph, user_queue,
			    PRIBIO | PCATCH, "targrd", 0);
			ccb_h = TAILQ_FIRST(user_queue);
			user_descr = TAILQ_FIRST(abort_queue);
			if (error != 0) {
				if (error == ERESTART) {
					continue;
				} else {
					goto read_fail;
				}
			}
		} else {
			cam_periph_unlock(softc->periph);
			return (EAGAIN);
		}
	}

	/* Data is available so fill the user's buffer */
	while (ccb_h != NULL) {
		struct targ_cmd_descr *descr;

		if (uio->uio_resid < sizeof(user_ccb))
			break;
		TAILQ_REMOVE(user_queue, ccb_h, periph_links.tqe);
		descr = (struct targ_cmd_descr *)ccb_h->targ_descr;
		user_ccb = descr->user_ccb;
		CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
			  ("targread ccb %p (%p)\n", ccb_h, user_ccb));
		error = targreturnccb(softc, (union ccb *)ccb_h);
		if (error != 0)
			goto read_fail;
		cam_periph_unlock(softc->periph);
		error = uiomove((caddr_t)&user_ccb, sizeof(user_ccb), uio);
		cam_periph_lock(softc->periph);
		if (error != 0)
			goto read_fail;
		read_len += sizeof(user_ccb);

		ccb_h = TAILQ_FIRST(user_queue);
	}

	/* Flush out any aborted descriptors */
	while (user_descr != NULL) {
		if (uio->uio_resid < sizeof(user_ccb))
			break;
		TAILQ_REMOVE(abort_queue, user_descr, tqe);
		user_ccb = user_descr->user_ccb;
		CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
			  ("targread aborted descr %p (%p)\n",
			  user_descr, user_ccb));
		suword(&user_ccb->ccb_h.status, CAM_REQ_ABORTED);
		cam_periph_unlock(softc->periph);
		error = uiomove((caddr_t)&user_ccb, sizeof(user_ccb), uio);
		cam_periph_lock(softc->periph);
		if (error != 0)
			goto read_fail;
		read_len += sizeof(user_ccb);

		user_descr = TAILQ_FIRST(abort_queue);
	}

	/*
	 * If we've successfully read some amount of data, don't report an
	 * error.  If the error is persistent, it will be reported on the
	 * next read().
	 */
	if (read_len == 0 && uio->uio_resid != 0)
		error = ENOSPC;

read_fail:
	cam_periph_unlock(softc->periph);
	return (error);
}

/* Copy completed ccb back to the user */
static int
targreturnccb(struct targ_softc *softc, union ccb *ccb)
{
	struct targ_cmd_descr *descr;
	struct ccb_hdr *u_ccbh;
	size_t ccb_len;
	int error;

	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH, ("targreturnccb %p\n", ccb));
	descr = (struct targ_cmd_descr *)ccb->ccb_h.targ_descr;
	u_ccbh = &descr->user_ccb->ccb_h;

	/* Copy out the central portion of the ccb_hdr */
	copyout(&ccb->ccb_h.retry_count, &u_ccbh->retry_count,
		offsetof(struct ccb_hdr, periph_priv) -
		offsetof(struct ccb_hdr, retry_count));

	/* Copy out the rest of the ccb (after the ccb_hdr) */
	ccb_len = targccblen(ccb->ccb_h.func_code) - sizeof(struct ccb_hdr);
	if (descr->mapinfo.num_bufs_used != 0)
		cam_periph_unmapmem(ccb, &descr->mapinfo);
	error = copyout(&ccb->ccb_h + 1, u_ccbh + 1, ccb_len);
	if (error != 0) {
		xpt_print(softc->path,
		    "targreturnccb - CCB copyout failed (%d)\n", error);
	}
	/* Free CCB or send back to devq. */
	targfreeccb(softc, ccb);

	return (error);
}

static union ccb *
targgetccb(struct targ_softc *softc, xpt_opcode type, int priority)
{
	union ccb *ccb;
	int ccb_len;

	ccb_len = targccblen(type);
	ccb = malloc(ccb_len, M_TARG, M_NOWAIT);
	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH, ("getccb %p\n", ccb));
	if (ccb == NULL) {
		return (ccb);
	}
	xpt_setup_ccb(&ccb->ccb_h, softc->path, priority);
	ccb->ccb_h.func_code = type;
	ccb->ccb_h.cbfcnp = targdone;
	ccb->ccb_h.targ_descr = targgetdescr(softc);
	if (ccb->ccb_h.targ_descr == NULL) {
		free (ccb, M_TARG);
		ccb = NULL;
	}
	return (ccb);
}

static void
targfreeccb(struct targ_softc *softc, union ccb *ccb)
{
	CAM_DEBUG_PRINT(CAM_DEBUG_PERIPH, ("targfreeccb descr %p and\n",
			ccb->ccb_h.targ_descr));
	free(ccb->ccb_h.targ_descr, M_TARG);

	switch (ccb->ccb_h.func_code) {
	case XPT_ACCEPT_TARGET_IO:
	case XPT_IMMED_NOTIFY:
	case XPT_IMMEDIATE_NOTIFY:
		CAM_DEBUG_PRINT(CAM_DEBUG_PERIPH, ("freeing ccb %p\n", ccb));
		free(ccb, M_TARG);
		break;
	default:
		/* Send back CCB if we got it from the periph */
		if (XPT_FC_IS_QUEUED(ccb)) {
			CAM_DEBUG_PRINT(CAM_DEBUG_PERIPH,
					("returning queued ccb %p\n", ccb));
			xpt_release_ccb(ccb);
		} else {
			CAM_DEBUG_PRINT(CAM_DEBUG_PERIPH,
					("freeing ccb %p\n", ccb));
			free(ccb, M_TARG);
		}
		break;
	}
}

static struct targ_cmd_descr *
targgetdescr(struct targ_softc *softc)
{
	struct targ_cmd_descr *descr;

	descr = malloc(sizeof(*descr), M_TARG,
	       M_NOWAIT);
	if (descr) {
		descr->mapinfo.num_bufs_used = 0;
	}
	return (descr);
}

static void
targinit(void)
{
	struct cdev *dev;

	/* Add symbolic link to targ0 for compatibility. */
	dev = make_dev(&targ_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "targ");
	make_dev_alias(dev, "targ0");
}

static void
targasync(void *callback_arg, u_int32_t code, struct cam_path *path, void *arg)
{
	/* All events are handled in usermode by INOTs */
	panic("targasync() called, should be an INOT instead");
}

/* Cancel all pending requests and CCBs awaiting work. */
static void
abort_all_pending(struct targ_softc *softc)
{
	struct targ_cmd_descr   *descr;
	struct ccb_abort	 cab;
	struct ccb_hdr		*ccb_h;

	CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH, ("abort_all_pending\n"));

	/* First abort the descriptors awaiting resources */
	while ((descr = TAILQ_FIRST(&softc->work_queue)) != NULL) {
		CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
			  ("Aborting descr from workq %p\n", descr));
		TAILQ_REMOVE(&softc->work_queue, descr, tqe);
		TAILQ_INSERT_TAIL(&softc->abort_queue, descr, tqe);
	}

	/* 
	 * Then abort all pending CCBs.
	 * targdone() will return the aborted CCB via user_ccb_queue
	 */
	xpt_setup_ccb(&cab.ccb_h, softc->path, CAM_PRIORITY_NORMAL);
	cab.ccb_h.func_code = XPT_ABORT;
	cab.ccb_h.status = CAM_REQ_CMP_ERR;
	TAILQ_FOREACH(ccb_h, &softc->pending_ccb_queue, periph_links.tqe) {
		CAM_DEBUG(softc->path, CAM_DEBUG_PERIPH,
			  ("Aborting pending CCB %p\n", ccb_h));
		cab.abort_ccb = (union ccb *)ccb_h;
		xpt_action((union ccb *)&cab);
		if (cab.ccb_h.status != CAM_REQ_CMP) {
			xpt_print(cab.ccb_h.path,
			    "Unable to abort CCB, status %#x\n",
			    cab.ccb_h.status);
		}
	}

	/* If we aborted at least one pending CCB ok, wait for it. */
	if (cab.ccb_h.status == CAM_REQ_CMP) {
		cam_periph_sleep(softc->periph, &softc->pending_ccb_queue,
		       PRIBIO | PCATCH, "tgabrt", 0);
	}

	/* If we aborted anything from the work queue, wakeup user. */
	if (!TAILQ_EMPTY(&softc->user_ccb_queue)
	 || !TAILQ_EMPTY(&softc->abort_queue)) {
		cam_periph_unlock(softc->periph);
		notify_user(softc);
		cam_periph_lock(softc->periph);
	}
}

/* Notify the user that data is ready */
static void
notify_user(struct targ_softc *softc)
{
	/*
	 * Notify users sleeping via poll(), kqueue(), and
	 * blocking read().
	 */
	selwakeuppri(&softc->read_select, PRIBIO);
	KNOTE_UNLOCKED(&softc->read_select.si_note, 0);
	wakeup(&softc->user_ccb_queue);
}

/* Convert CAM status to errno values */
static int
targcamstatus(cam_status status)
{
	switch (status & CAM_STATUS_MASK) {
	case CAM_REQ_CMP:	/* CCB request completed without error */
		return (0);
	case CAM_REQ_INPROG:	/* CCB request is in progress */
		return (EINPROGRESS);
	case CAM_REQ_CMP_ERR:	/* CCB request completed with an error */
		return (EIO);
	case CAM_PROVIDE_FAIL:	/* Unable to provide requested capability */
		return (ENOTTY);
	case CAM_FUNC_NOTAVAIL:	/* The requested function is not available */
		return (ENOTSUP);
	case CAM_LUN_ALRDY_ENA:	/* LUN is already enabled for target mode */
		return (EADDRINUSE);
	case CAM_PATH_INVALID:	/* Supplied Path ID is invalid */
	case CAM_DEV_NOT_THERE:	/* SCSI Device Not Installed/there */
		return (ENOENT);
	case CAM_REQ_ABORTED:	/* CCB request aborted by the host */
		return (ECANCELED);
	case CAM_CMD_TIMEOUT:	/* Command timeout */
		return (ETIMEDOUT);
	case CAM_REQUEUE_REQ:	/* Requeue to preserve transaction ordering */
		return (EAGAIN);
	case CAM_REQ_INVALID:	/* CCB request was invalid */
		return (EINVAL);
	case CAM_RESRC_UNAVAIL:	/* Resource Unavailable */
		return (ENOMEM);
	case CAM_BUSY:		/* CAM subsystem is busy */
	case CAM_UA_ABORT:	/* Unable to abort CCB request */
		return (EBUSY);
	default:
		return (ENXIO);
	}
}

static size_t
targccblen(xpt_opcode func_code)
{
	int len;

	/* Codes we expect to see as a target */
	switch (func_code) {
	case XPT_CONT_TARGET_IO:
	case XPT_SCSI_IO:
		len = sizeof(struct ccb_scsiio);
		break;
	case XPT_ACCEPT_TARGET_IO:
		len = sizeof(struct ccb_accept_tio);
		break;
	case XPT_IMMED_NOTIFY:
		len = sizeof(struct ccb_immed_notify);
		break;
	case XPT_IMMEDIATE_NOTIFY:
		len = sizeof(struct ccb_immediate_notify);
		break;
	case XPT_REL_SIMQ:
		len = sizeof(struct ccb_relsim);
		break;
	case XPT_PATH_INQ:
		len = sizeof(struct ccb_pathinq);
		break;
	case XPT_DEBUG:
		len = sizeof(struct ccb_debug);
		break;
	case XPT_ABORT:
		len = sizeof(struct ccb_abort);
		break;
	case XPT_EN_LUN:
		len = sizeof(struct ccb_en_lun);
		break;
	default:
		len = sizeof(union ccb);
		break;
	}

	return (len);
}
