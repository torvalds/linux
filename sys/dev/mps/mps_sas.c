/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Yahoo! Inc.
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2015 Avago Technologies
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Communications core for Avago Technologies (LSI) MPT2 */

/* TODO Move headers to mpsvar */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/selinfo.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/sbuf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <machine/stdarg.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#if __FreeBSD_version >= 900026
#include <cam/scsi/smp_all.h>
#endif

#include <dev/mps/mpi/mpi2_type.h>
#include <dev/mps/mpi/mpi2.h>
#include <dev/mps/mpi/mpi2_ioc.h>
#include <dev/mps/mpi/mpi2_sas.h>
#include <dev/mps/mpi/mpi2_cnfg.h>
#include <dev/mps/mpi/mpi2_init.h>
#include <dev/mps/mpi/mpi2_tool.h>
#include <dev/mps/mps_ioctl.h>
#include <dev/mps/mpsvar.h>
#include <dev/mps/mps_table.h>
#include <dev/mps/mps_sas.h>

#define MPSSAS_DISCOVERY_TIMEOUT	20
#define MPSSAS_MAX_DISCOVERY_TIMEOUTS	10 /* 200 seconds */

/*
 * static array to check SCSI OpCode for EEDP protection bits
 */
#define	PRO_R MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP
#define	PRO_W MPI2_SCSIIO_EEDPFLAGS_INSERT_OP
#define	PRO_V MPI2_SCSIIO_EEDPFLAGS_INSERT_OP
static uint8_t op_code_prot[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, PRO_R, 0, PRO_W, 0, 0, 0, PRO_W, PRO_V,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, PRO_W, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, PRO_R, 0, PRO_W, 0, 0, 0, PRO_W, PRO_V,
	0, 0, 0, PRO_W, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, PRO_R, 0, PRO_W, 0, 0, 0, PRO_W, PRO_V,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

MALLOC_DEFINE(M_MPSSAS, "MPSSAS", "MPS SAS memory");

static void mpssas_remove_device(struct mps_softc *, struct mps_command *);
static void mpssas_remove_complete(struct mps_softc *, struct mps_command *);
static void mpssas_action(struct cam_sim *sim, union ccb *ccb);
static void mpssas_poll(struct cam_sim *sim);
static int mpssas_send_abort(struct mps_softc *sc, struct mps_command *tm,
    struct mps_command *cm);
static void mpssas_scsiio_timeout(void *data);
static void mpssas_abort_complete(struct mps_softc *sc, struct mps_command *cm);
static void mpssas_direct_drive_io(struct mpssas_softc *sassc,
    struct mps_command *cm, union ccb *ccb);
static void mpssas_action_scsiio(struct mpssas_softc *, union ccb *);
static void mpssas_scsiio_complete(struct mps_softc *, struct mps_command *);
static void mpssas_action_resetdev(struct mpssas_softc *, union ccb *);
#if __FreeBSD_version >= 900026
static void mpssas_smpio_complete(struct mps_softc *sc, struct mps_command *cm);
static void mpssas_send_smpcmd(struct mpssas_softc *sassc, union ccb *ccb,
			       uint64_t sasaddr);
static void mpssas_action_smpio(struct mpssas_softc *sassc, union ccb *ccb);
#endif //FreeBSD_version >= 900026
static void mpssas_resetdev_complete(struct mps_softc *, struct mps_command *);
static void mpssas_async(void *callback_arg, uint32_t code,
			 struct cam_path *path, void *arg);
#if (__FreeBSD_version < 901503) || \
    ((__FreeBSD_version >= 1000000) && (__FreeBSD_version < 1000006))
static void mpssas_check_eedp(struct mps_softc *sc, struct cam_path *path,
			      struct ccb_getdev *cgd);
static void mpssas_read_cap_done(struct cam_periph *periph, union ccb *done_ccb);
#endif
static int mpssas_send_portenable(struct mps_softc *sc);
static void mpssas_portenable_complete(struct mps_softc *sc,
    struct mps_command *cm);

struct mpssas_target *
mpssas_find_target_by_handle(struct mpssas_softc *sassc, int start, uint16_t handle)
{
	struct mpssas_target *target;
	int i;

	for (i = start; i < sassc->maxtargets; i++) {
		target = &sassc->targets[i];
		if (target->handle == handle)
			return (target);
	}

	return (NULL);
}

/* we need to freeze the simq during attach and diag reset, to avoid failing
 * commands before device handles have been found by discovery.  Since
 * discovery involves reading config pages and possibly sending commands,
 * discovery actions may continue even after we receive the end of discovery
 * event, so refcount discovery actions instead of assuming we can unfreeze
 * the simq when we get the event.
 */
void
mpssas_startup_increment(struct mpssas_softc *sassc)
{
	MPS_FUNCTRACE(sassc->sc);

	if ((sassc->flags & MPSSAS_IN_STARTUP) != 0) {
		if (sassc->startup_refcount++ == 0) {
			/* just starting, freeze the simq */
			mps_dprint(sassc->sc, MPS_INIT,
			    "%s freezing simq\n", __func__);
#if __FreeBSD_version >= 1000039
			xpt_hold_boot();
#endif
			xpt_freeze_simq(sassc->sim, 1);
		}
		mps_dprint(sassc->sc, MPS_INIT, "%s refcount %u\n", __func__,
		    sassc->startup_refcount);
	}
}

void
mpssas_release_simq_reinit(struct mpssas_softc *sassc)
{
	if (sassc->flags & MPSSAS_QUEUE_FROZEN) {
		sassc->flags &= ~MPSSAS_QUEUE_FROZEN;
		xpt_release_simq(sassc->sim, 1);
		mps_dprint(sassc->sc, MPS_INFO, "Unfreezing SIM queue\n");
	}
}

void
mpssas_startup_decrement(struct mpssas_softc *sassc)
{
	MPS_FUNCTRACE(sassc->sc);

	if ((sassc->flags & MPSSAS_IN_STARTUP) != 0) {
		if (--sassc->startup_refcount == 0) {
			/* finished all discovery-related actions, release
			 * the simq and rescan for the latest topology.
			 */
			mps_dprint(sassc->sc, MPS_INIT,
			    "%s releasing simq\n", __func__);
			sassc->flags &= ~MPSSAS_IN_STARTUP;
			xpt_release_simq(sassc->sim, 1);
#if __FreeBSD_version >= 1000039
			xpt_release_boot();
#else
			mpssas_rescan_target(sassc->sc, NULL);
#endif
		}
		mps_dprint(sassc->sc, MPS_INIT, "%s refcount %u\n", __func__,
		    sassc->startup_refcount);
	}
}

/*
 * The firmware requires us to stop sending commands when we're doing task
 * management.
 * XXX The logic for serializing the device has been made lazy and moved to
 * mpssas_prepare_for_tm().
 */
struct mps_command *
mpssas_alloc_tm(struct mps_softc *sc)
{
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mps_command *tm;

	tm = mps_alloc_high_priority_command(sc);
	if (tm == NULL)
		return (NULL);

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	req->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	return tm;
}

void
mpssas_free_tm(struct mps_softc *sc, struct mps_command *tm)
{
	int target_id = 0xFFFFFFFF;
 
	if (tm == NULL)
		return;

	/*
	 * For TM's the devq is frozen for the device.  Unfreeze it here and
	 * free the resources used for freezing the devq.  Must clear the
	 * INRESET flag as well or scsi I/O will not work.
	 */
	if (tm->cm_targ != NULL) {
		tm->cm_targ->flags &= ~MPSSAS_TARGET_INRESET;
		target_id = tm->cm_targ->tid;
	}
	if (tm->cm_ccb) {
		mps_dprint(sc, MPS_INFO, "Unfreezing devq for target ID %d\n",
		    target_id);
		xpt_release_devq(tm->cm_ccb->ccb_h.path, 1, TRUE);
		xpt_free_path(tm->cm_ccb->ccb_h.path);
		xpt_free_ccb(tm->cm_ccb);
	}

	mps_free_high_priority_command(sc, tm);
}

void
mpssas_rescan_target(struct mps_softc *sc, struct mpssas_target *targ)
{
	struct mpssas_softc *sassc = sc->sassc;
	path_id_t pathid;
	target_id_t targetid;
	union ccb *ccb;

	MPS_FUNCTRACE(sc);
	pathid = cam_sim_path(sassc->sim);
	if (targ == NULL)
		targetid = CAM_TARGET_WILDCARD;
	else
		targetid = targ - sassc->targets;

	/*
	 * Allocate a CCB and schedule a rescan.
	 */
	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		mps_dprint(sc, MPS_ERROR, "unable to alloc CCB for rescan\n");
		return;
	}

	if (xpt_create_path(&ccb->ccb_h.path, NULL, pathid,
	    targetid, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		mps_dprint(sc, MPS_ERROR, "unable to create path for rescan\n");
		xpt_free_ccb(ccb);
		return;
	}

	if (targetid == CAM_TARGET_WILDCARD)
		ccb->ccb_h.func_code = XPT_SCAN_BUS;
	else
		ccb->ccb_h.func_code = XPT_SCAN_TGT;     

	mps_dprint(sc, MPS_TRACE, "%s targetid %u\n", __func__, targetid);
	xpt_rescan(ccb);
}

static void
mpssas_log_command(struct mps_command *cm, u_int level, const char *fmt, ...)
{
	struct sbuf sb;
	va_list ap;
	char str[192];
	char path_str[64];

	if (cm == NULL)
		return;

	/* No need to be in here if debugging isn't enabled */
	if ((cm->cm_sc->mps_debug & level) == 0)
		return;

	sbuf_new(&sb, str, sizeof(str), 0);

	va_start(ap, fmt);

	if (cm->cm_ccb != NULL) {
		xpt_path_string(cm->cm_ccb->csio.ccb_h.path, path_str,
				sizeof(path_str));
		sbuf_cat(&sb, path_str);
		if (cm->cm_ccb->ccb_h.func_code == XPT_SCSI_IO) {
			scsi_command_string(&cm->cm_ccb->csio, &sb);
			sbuf_printf(&sb, "length %d ",
				    cm->cm_ccb->csio.dxfer_len);
		}
	}
	else {
		sbuf_printf(&sb, "(noperiph:%s%d:%u:%u:%u): ",
		    cam_sim_name(cm->cm_sc->sassc->sim),
		    cam_sim_unit(cm->cm_sc->sassc->sim),
		    cam_sim_bus(cm->cm_sc->sassc->sim),
		    cm->cm_targ ? cm->cm_targ->tid : 0xFFFFFFFF,
		    cm->cm_lun);
	}

	sbuf_printf(&sb, "SMID %u ", cm->cm_desc.Default.SMID);
	sbuf_vprintf(&sb, fmt, ap);
	sbuf_finish(&sb);
	mps_print_field(cm->cm_sc, "%s", sbuf_data(&sb));

	va_end(ap);
}


static void
mpssas_remove_volume(struct mps_softc *sc, struct mps_command *tm)
{
	MPI2_SCSI_TASK_MANAGE_REPLY *reply;
	struct mpssas_target *targ;
	uint16_t handle;

	MPS_FUNCTRACE(sc);

	reply = (MPI2_SCSI_TASK_MANAGE_REPLY *)tm->cm_reply;
	handle = (uint16_t)(uintptr_t)tm->cm_complete_data;
	targ = tm->cm_targ;

	if (reply == NULL) {
		/* XXX retry the remove after the diag reset completes? */
		mps_dprint(sc, MPS_FAULT,
		    "%s NULL reply resetting device 0x%04x\n", __func__,
		    handle);
		mpssas_free_tm(sc, tm);
		return;
	}

	if ((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS) {
		mps_dprint(sc, MPS_ERROR,
		   "IOCStatus = 0x%x while resetting device 0x%x\n",
		   le16toh(reply->IOCStatus), handle);
	}

	mps_dprint(sc, MPS_XINFO,
	    "Reset aborted %u commands\n", reply->TerminationCount);
	mps_free_reply(sc, tm->cm_reply_data);
	tm->cm_reply = NULL;	/* Ensures the reply won't get re-freed */

	mps_dprint(sc, MPS_XINFO,
	    "clearing target %u handle 0x%04x\n", targ->tid, handle);
	
	/*
	 * Don't clear target if remove fails because things will get confusing.
	 * Leave the devname and sasaddr intact so that we know to avoid reusing
	 * this target id if possible, and so we can assign the same target id
	 * to this device if it comes back in the future.
	 */
	if ((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) ==
	    MPI2_IOCSTATUS_SUCCESS) {
		targ = tm->cm_targ;
		targ->handle = 0x0;
		targ->encl_handle = 0x0;
		targ->encl_slot = 0x0;
		targ->exp_dev_handle = 0x0;
		targ->phy_num = 0x0;
		targ->linkrate = 0x0;
		targ->devinfo = 0x0;
		targ->flags = 0x0;
	}

	mpssas_free_tm(sc, tm);
}


/*
 * No Need to call "MPI2_SAS_OP_REMOVE_DEVICE" For Volume removal.
 * Otherwise Volume Delete is same as Bare Drive Removal.
 */
void
mpssas_prepare_volume_remove(struct mpssas_softc *sassc, uint16_t handle)
{
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mps_softc *sc;
	struct mps_command *tm;
	struct mpssas_target *targ = NULL;

	MPS_FUNCTRACE(sassc->sc);
	sc = sassc->sc;

#ifdef WD_SUPPORT
	/*
	 * If this is a WD controller, determine if the disk should be exposed
	 * to the OS or not.  If disk should be exposed, return from this
	 * function without doing anything.
	 */
	if (sc->WD_available && (sc->WD_hide_expose ==
	    MPS_WD_EXPOSE_ALWAYS)) {
		return;
	}
#endif //WD_SUPPORT

	targ = mpssas_find_target_by_handle(sassc, 0, handle);
	if (targ == NULL) {
		/* FIXME: what is the action? */
		/* We don't know about this device? */
		mps_dprint(sc, MPS_ERROR,
		   "%s %d : invalid handle 0x%x \n", __func__,__LINE__, handle);
		return;
	}

	targ->flags |= MPSSAS_TARGET_INREMOVAL;

	tm = mpssas_alloc_tm(sc);
	if (tm == NULL) {
		mps_dprint(sc, MPS_ERROR,
		    "%s: command alloc failure\n", __func__);
		return;
	}

	mpssas_rescan_target(sc, targ);

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	req->DevHandle = targ->handle;
	req->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;

	/* SAS Hard Link Reset / SATA Link Reset */
	req->MsgFlags = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	tm->cm_targ = targ;
	tm->cm_data = NULL;
	tm->cm_complete = mpssas_remove_volume;
	tm->cm_complete_data = (void *)(uintptr_t)handle;

	mps_dprint(sc, MPS_INFO, "%s: Sending reset for target ID %d\n",
	    __func__, targ->tid);
	mpssas_prepare_for_tm(sc, tm, targ, CAM_LUN_WILDCARD);

	mps_map_command(sc, tm);
}

/*
 * The MPT2 firmware performs debounce on the link to avoid transient link
 * errors and false removals.  When it does decide that link has been lost
 * and a device need to go away, it expects that the host will perform a
 * target reset and then an op remove.  The reset has the side-effect of
 * aborting any outstanding requests for the device, which is required for
 * the op-remove to succeed.  It's not clear if the host should check for
 * the device coming back alive after the reset.
 */
void
mpssas_prepare_remove(struct mpssas_softc *sassc, uint16_t handle)
{
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mps_softc *sc;
	struct mps_command *cm;
	struct mpssas_target *targ = NULL;

	MPS_FUNCTRACE(sassc->sc);

	sc = sassc->sc;

	targ = mpssas_find_target_by_handle(sassc, 0, handle);
	if (targ == NULL) {
		/* FIXME: what is the action? */
		/* We don't know about this device? */
		mps_dprint(sc, MPS_ERROR,
		    "%s : invalid handle 0x%x \n", __func__, handle);
		return;
	}

	targ->flags |= MPSSAS_TARGET_INREMOVAL;

	cm = mpssas_alloc_tm(sc);
	if (cm == NULL) {
		mps_dprint(sc, MPS_ERROR,
		    "%s: command alloc failure\n", __func__);
		return;
	}

	mpssas_rescan_target(sc, targ);

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)cm->cm_req;
	memset(req, 0, sizeof(*req));
	req->DevHandle = htole16(targ->handle);
	req->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;

	/* SAS Hard Link Reset / SATA Link Reset */
	req->MsgFlags = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	cm->cm_targ = targ;
	cm->cm_data = NULL;
	cm->cm_complete = mpssas_remove_device;
	cm->cm_complete_data = (void *)(uintptr_t)handle;

	mps_dprint(sc, MPS_INFO, "%s: Sending reset for target ID %d\n",
	    __func__, targ->tid);
	mpssas_prepare_for_tm(sc, cm, targ, CAM_LUN_WILDCARD);

	mps_map_command(sc, cm);
}

static void
mpssas_remove_device(struct mps_softc *sc, struct mps_command *tm)
{
	MPI2_SCSI_TASK_MANAGE_REPLY *reply;
	MPI2_SAS_IOUNIT_CONTROL_REQUEST *req;
	struct mpssas_target *targ;
	struct mps_command *next_cm;
	uint16_t handle;

	MPS_FUNCTRACE(sc);

	reply = (MPI2_SCSI_TASK_MANAGE_REPLY *)tm->cm_reply;
	handle = (uint16_t)(uintptr_t)tm->cm_complete_data;
	targ = tm->cm_targ;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and
	 * task management commands don't have S/G lists.
	 */
	if ((tm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		mps_dprint(sc, MPS_ERROR,
		    "%s: cm_flags = %#x for remove of handle %#04x! "
		    "This should not happen!\n", __func__, tm->cm_flags,
		    handle);
	}

	if (reply == NULL) {
		/* XXX retry the remove after the diag reset completes? */
		mps_dprint(sc, MPS_FAULT,
		    "%s NULL reply resetting device 0x%04x\n", __func__,
		    handle);
		mpssas_free_tm(sc, tm);
		return;
	}

	if ((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS) {
		mps_dprint(sc, MPS_ERROR,
		   "IOCStatus = 0x%x while resetting device 0x%x\n",
		   le16toh(reply->IOCStatus), handle);
	}

	mps_dprint(sc, MPS_XINFO, "Reset aborted %u commands\n",
	    le32toh(reply->TerminationCount));
	mps_free_reply(sc, tm->cm_reply_data);
	tm->cm_reply = NULL;	/* Ensures the reply won't get re-freed */

	/* Reuse the existing command */
	req = (MPI2_SAS_IOUNIT_CONTROL_REQUEST *)tm->cm_req;
	memset(req, 0, sizeof(*req));
	req->Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
	req->Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	req->DevHandle = htole16(handle);
	tm->cm_data = NULL;
	tm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	tm->cm_complete = mpssas_remove_complete;
	tm->cm_complete_data = (void *)(uintptr_t)handle;

	mps_map_command(sc, tm);

	mps_dprint(sc, MPS_XINFO, "clearing target %u handle 0x%04x\n",
		   targ->tid, handle);
	TAILQ_FOREACH_SAFE(tm, &targ->commands, cm_link, next_cm) {
		union ccb *ccb;

		mps_dprint(sc, MPS_XINFO, "Completing missed command %p\n", tm);
		ccb = tm->cm_complete_data;
		mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
		mpssas_scsiio_complete(sc, tm);
	}
}

static void
mpssas_remove_complete(struct mps_softc *sc, struct mps_command *tm)
{
	MPI2_SAS_IOUNIT_CONTROL_REPLY *reply;
	uint16_t handle;
	struct mpssas_target *targ;
	struct mpssas_lun *lun;

	MPS_FUNCTRACE(sc);

	reply = (MPI2_SAS_IOUNIT_CONTROL_REPLY *)tm->cm_reply;
	handle = (uint16_t)(uintptr_t)tm->cm_complete_data;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and
	 * task management commands don't have S/G lists.
	 */
	if ((tm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		mps_dprint(sc, MPS_XINFO,
			   "%s: cm_flags = %#x for remove of handle %#04x! "
			   "This should not happen!\n", __func__, tm->cm_flags,
			   handle);
		mpssas_free_tm(sc, tm);
		return;
	}

	if (reply == NULL) {
		/* most likely a chip reset */
		mps_dprint(sc, MPS_FAULT,
		    "%s NULL reply removing device 0x%04x\n", __func__, handle);
		mpssas_free_tm(sc, tm);
		return;
	}

	mps_dprint(sc, MPS_XINFO,
	    "%s on handle 0x%04x, IOCStatus= 0x%x\n", __func__, 
	    handle, le16toh(reply->IOCStatus));

	/*
	 * Don't clear target if remove fails because things will get confusing.
	 * Leave the devname and sasaddr intact so that we know to avoid reusing
	 * this target id if possible, and so we can assign the same target id
	 * to this device if it comes back in the future.
	 */
	if ((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) ==
	    MPI2_IOCSTATUS_SUCCESS) {
		targ = tm->cm_targ;
		targ->handle = 0x0;
		targ->encl_handle = 0x0;
		targ->encl_slot = 0x0;
		targ->exp_dev_handle = 0x0;
		targ->phy_num = 0x0;
		targ->linkrate = 0x0;
		targ->devinfo = 0x0;
		targ->flags = 0x0;
		
		while(!SLIST_EMPTY(&targ->luns)) {
			lun = SLIST_FIRST(&targ->luns);
			SLIST_REMOVE_HEAD(&targ->luns, lun_link);
			free(lun, M_MPT2);
		}
	}
	

	mpssas_free_tm(sc, tm);
}

static int
mpssas_register_events(struct mps_softc *sc)
{
	u32 events[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];

	bzero(events, 16);
	setbit(events, MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE);
	setbit(events, MPI2_EVENT_SAS_DISCOVERY);
	setbit(events, MPI2_EVENT_SAS_BROADCAST_PRIMITIVE);
	setbit(events, MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE);
	setbit(events, MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW);
	setbit(events, MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST);
	setbit(events, MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE);
	setbit(events, MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST);
	setbit(events, MPI2_EVENT_IR_VOLUME);
	setbit(events, MPI2_EVENT_IR_PHYSICAL_DISK);
	setbit(events, MPI2_EVENT_IR_OPERATION_STATUS);
	setbit(events, MPI2_EVENT_LOG_ENTRY_ADDED);

	mps_register_events(sc, events, mpssas_evt_handler, NULL,
	    &sc->sassc->mpssas_eh);

	return (0);
}

int
mps_attach_sas(struct mps_softc *sc)
{
	struct mpssas_softc *sassc;
	cam_status status;
	int unit, error = 0, reqs;

	MPS_FUNCTRACE(sc);
	mps_dprint(sc, MPS_INIT, "%s entered\n", __func__);

	sassc = malloc(sizeof(struct mpssas_softc), M_MPT2, M_WAITOK|M_ZERO);
	if(!sassc) {
		mps_dprint(sc, MPS_INIT|MPS_ERROR,
		    "Cannot allocate SAS controller memory\n");
		return (ENOMEM);
	}

	/*
	 * XXX MaxTargets could change during a reinit.  Since we don't
	 * resize the targets[] array during such an event, cache the value
	 * of MaxTargets here so that we don't get into trouble later.  This
	 * should move into the reinit logic.
	 */
	sassc->maxtargets = sc->facts->MaxTargets + sc->facts->MaxVolumes;
	sassc->targets = malloc(sizeof(struct mpssas_target) *
	    sassc->maxtargets, M_MPT2, M_WAITOK|M_ZERO);
	if(!sassc->targets) {
		mps_dprint(sc, MPS_INIT|MPS_ERROR,
		    "Cannot allocate SAS target memory\n");
		free(sassc, M_MPT2);
		return (ENOMEM);
	}
	sc->sassc = sassc;
	sassc->sc = sc;

	reqs = sc->num_reqs - sc->num_prireqs - 1;
	if ((sassc->devq = cam_simq_alloc(reqs)) == NULL) {
		mps_dprint(sc, MPS_ERROR, "Cannot allocate SIMQ\n");
		error = ENOMEM;
		goto out;
	}

	unit = device_get_unit(sc->mps_dev);
	sassc->sim = cam_sim_alloc(mpssas_action, mpssas_poll, "mps", sassc,
	    unit, &sc->mps_mtx, reqs, reqs, sassc->devq);
	if (sassc->sim == NULL) {
		mps_dprint(sc, MPS_INIT|MPS_ERROR, "Cannot allocate SIM\n");
		error = EINVAL;
		goto out;
	}

	TAILQ_INIT(&sassc->ev_queue);

	/* Initialize taskqueue for Event Handling */
	TASK_INIT(&sassc->ev_task, 0, mpssas_firmware_event_work, sc);
	sassc->ev_tq = taskqueue_create("mps_taskq", M_NOWAIT | M_ZERO,
	    taskqueue_thread_enqueue, &sassc->ev_tq);
	taskqueue_start_threads(&sassc->ev_tq, 1, PRIBIO, "%s taskq", 
	    device_get_nameunit(sc->mps_dev));

	mps_lock(sc);

	/*
	 * XXX There should be a bus for every port on the adapter, but since
	 * we're just going to fake the topology for now, we'll pretend that
	 * everything is just a target on a single bus.
	 */
	if ((error = xpt_bus_register(sassc->sim, sc->mps_dev, 0)) != 0) {
		mps_dprint(sc, MPS_INIT|MPS_ERROR,
		    "Error %d registering SCSI bus\n", error);
		mps_unlock(sc);
		goto out;
	}

	/*
	 * Assume that discovery events will start right away.
	 *
	 * Hold off boot until discovery is complete.
	 */
	sassc->flags |= MPSSAS_IN_STARTUP | MPSSAS_IN_DISCOVERY;
	sc->sassc->startup_refcount = 0;
	mpssas_startup_increment(sassc);

	callout_init(&sassc->discovery_callout, 1 /*mpsafe*/);

	/*
	 * Register for async events so we can determine the EEDP
	 * capabilities of devices.
	 */
	status = xpt_create_path(&sassc->path, /*periph*/NULL,
	    cam_sim_path(sc->sassc->sim), CAM_TARGET_WILDCARD,
	    CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP) {
		mps_dprint(sc, MPS_ERROR|MPS_INIT,
		    "Error %#x creating sim path\n", status);
		sassc->path = NULL;
	} else {
		int event;

#if (__FreeBSD_version >= 1000006) || \
    ((__FreeBSD_version >= 901503) && (__FreeBSD_version < 1000000))
		event = AC_ADVINFO_CHANGED;
#else
		event = AC_FOUND_DEVICE;
#endif
		status = xpt_register_async(event, mpssas_async, sc,
					    sassc->path);
		if (status != CAM_REQ_CMP) {
			mps_dprint(sc, MPS_ERROR,
			    "Error %#x registering async handler for "
			    "AC_ADVINFO_CHANGED events\n", status);
			xpt_free_path(sassc->path);
			sassc->path = NULL;
		}
	}
	if (status != CAM_REQ_CMP) {
		/*
		 * EEDP use is the exception, not the rule.
		 * Warn the user, but do not fail to attach.
		 */
		mps_printf(sc, "EEDP capabilities disabled.\n");
	}

	mps_unlock(sc);

	mpssas_register_events(sc);
out:
	if (error)
		mps_detach_sas(sc);

	mps_dprint(sc, MPS_INIT, "%s exit error= %d\n", __func__, error);
	return (error);
}

int
mps_detach_sas(struct mps_softc *sc)
{
	struct mpssas_softc *sassc;
	struct mpssas_lun *lun, *lun_tmp;
	struct mpssas_target *targ;
	int i;

	MPS_FUNCTRACE(sc);

	if (sc->sassc == NULL)
		return (0);

	sassc = sc->sassc;
	mps_deregister_events(sc, sassc->mpssas_eh);

	/*
	 * Drain and free the event handling taskqueue with the lock
	 * unheld so that any parallel processing tasks drain properly
	 * without deadlocking.
	 */
	if (sassc->ev_tq != NULL)
		taskqueue_free(sassc->ev_tq);

	/* Make sure CAM doesn't wedge if we had to bail out early. */
	mps_lock(sc);

	while (sassc->startup_refcount != 0)
		mpssas_startup_decrement(sassc);

	/* Deregister our async handler */
	if (sassc->path != NULL) {
		xpt_register_async(0, mpssas_async, sc, sassc->path);
		xpt_free_path(sassc->path);
		sassc->path = NULL;
	}

	if (sassc->flags & MPSSAS_IN_STARTUP)
		xpt_release_simq(sassc->sim, 1);

	if (sassc->sim != NULL) {
		xpt_bus_deregister(cam_sim_path(sassc->sim));
		cam_sim_free(sassc->sim, FALSE);
	}

	mps_unlock(sc);

	if (sassc->devq != NULL)
		cam_simq_free(sassc->devq);

	for(i=0; i< sassc->maxtargets ;i++) {
		targ = &sassc->targets[i];
		SLIST_FOREACH_SAFE(lun, &targ->luns, lun_link, lun_tmp) {
			free(lun, M_MPT2);
		}
	}
	free(sassc->targets, M_MPT2);
	free(sassc, M_MPT2);
	sc->sassc = NULL;

	return (0);
}

void
mpssas_discovery_end(struct mpssas_softc *sassc)
{
	struct mps_softc *sc = sassc->sc;

	MPS_FUNCTRACE(sc);

	if (sassc->flags & MPSSAS_DISCOVERY_TIMEOUT_PENDING)
		callout_stop(&sassc->discovery_callout);

	/*
	 * After discovery has completed, check the mapping table for any
	 * missing devices and update their missing counts. Only do this once
	 * whenever the driver is initialized so that missing counts aren't
	 * updated unnecessarily. Note that just because discovery has
	 * completed doesn't mean that events have been processed yet. The
	 * check_devices function is a callout timer that checks if ALL devices
	 * are missing. If so, it will wait a little longer for events to
	 * complete and keep resetting itself until some device in the mapping
	 * table is not missing, meaning that event processing has started.
	 */
	if (sc->track_mapping_events) {
		mps_dprint(sc, MPS_XINFO | MPS_MAPPING, "Discovery has "
		    "completed. Check for missing devices in the mapping "
		    "table.\n");
		callout_reset(&sc->device_check_callout,
		    MPS_MISSING_CHECK_DELAY * hz, mps_mapping_check_devices,
		    sc);
	}
}

static void
mpssas_action(struct cam_sim *sim, union ccb *ccb)
{
	struct mpssas_softc *sassc;

	sassc = cam_sim_softc(sim);

	MPS_FUNCTRACE(sassc->sc);
	mps_dprint(sassc->sc, MPS_TRACE, "ccb func_code 0x%x\n",
	    ccb->ccb_h.func_code);
	mtx_assert(&sassc->sc->mps_mtx, MA_OWNED);

	switch (ccb->ccb_h.func_code) {
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;
		struct mps_softc *sc = sassc->sc;

		cpi->version_num = 1;
		cpi->hba_inquiry = PI_SDTR_ABLE|PI_TAG_ABLE|PI_WIDE_16;
		cpi->target_sprt = 0;
#if __FreeBSD_version >= 1000039
		cpi->hba_misc = PIM_NOBUSRESET | PIM_UNMAPPED | PIM_NOSCAN;
#else
		cpi->hba_misc = PIM_NOBUSRESET | PIM_UNMAPPED;
#endif
		cpi->hba_eng_cnt = 0;
		cpi->max_target = sassc->maxtargets - 1;
		cpi->max_lun = 255;

		/*
		 * initiator_id is set here to an ID outside the set of valid
		 * target IDs (including volumes).
		 */
		cpi->initiator_id = sassc->maxtargets;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "Avago Tech", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000;
		cpi->transport = XPORT_SAS;
		cpi->transport_version = 0;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_SPC;
		cpi->maxio = sc->maxio;
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings	*cts;
		struct ccb_trans_settings_sas	*sas;
		struct ccb_trans_settings_scsi	*scsi;
		struct mpssas_target *targ;

		cts = &ccb->cts;
		sas = &cts->xport_specific.sas;
		scsi = &cts->proto_specific.scsi;

		KASSERT(cts->ccb_h.target_id < sassc->maxtargets,
		    ("Target %d out of bounds in XPT_GET_TRANS_SETTINGS\n",
		    cts->ccb_h.target_id));
		targ = &sassc->targets[cts->ccb_h.target_id];
		if (targ->handle == 0x0) {
			mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
			break;
		}

		cts->protocol_version = SCSI_REV_SPC2;
		cts->transport = XPORT_SAS;
		cts->transport_version = 0;

		sas->valid = CTS_SAS_VALID_SPEED;
		switch (targ->linkrate) {
		case 0x08:
			sas->bitrate = 150000;
			break;
		case 0x09:
			sas->bitrate = 300000;
			break;
		case 0x0a:
			sas->bitrate = 600000;
			break;
		default:
			sas->valid = 0;
		}

		cts->protocol = PROTO_SCSI;
		scsi->valid = CTS_SCSI_VALID_TQ;
		scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;

		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		break;
	}
	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, /*extended*/1);
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		break;
	case XPT_RESET_DEV:
		mps_dprint(sassc->sc, MPS_XINFO, "mpssas_action XPT_RESET_DEV\n");
		mpssas_action_resetdev(sassc, ccb);
		return;
	case XPT_RESET_BUS:
	case XPT_ABORT:
	case XPT_TERM_IO:
		mps_dprint(sassc->sc, MPS_XINFO,
		    "mpssas_action faking success for abort or reset\n");
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		break;
	case XPT_SCSI_IO:
		mpssas_action_scsiio(sassc, ccb);
		return;
#if __FreeBSD_version >= 900026
	case XPT_SMP_IO:
		mpssas_action_smpio(sassc, ccb);
		return;
#endif
	default:
		mpssas_set_ccbstatus(ccb, CAM_FUNC_NOTAVAIL);
		break;
	}
	xpt_done(ccb);

}

static void
mpssas_announce_reset(struct mps_softc *sc, uint32_t ac_code,
    target_id_t target_id, lun_id_t lun_id)
{
	path_id_t path_id = cam_sim_path(sc->sassc->sim);
	struct cam_path *path;

	mps_dprint(sc, MPS_XINFO, "%s code %x target %d lun %jx\n", __func__,
	    ac_code, target_id, (uintmax_t)lun_id);

	if (xpt_create_path(&path, NULL, 
		path_id, target_id, lun_id) != CAM_REQ_CMP) {
		mps_dprint(sc, MPS_ERROR, "unable to create path for reset "
			   "notification\n");
		return;
	}

	xpt_async(ac_code, path, NULL);
	xpt_free_path(path);
}

static void 
mpssas_complete_all_commands(struct mps_softc *sc)
{
	struct mps_command *cm;
	int i;
	int completed;

	MPS_FUNCTRACE(sc);
	mtx_assert(&sc->mps_mtx, MA_OWNED);

	/* complete all commands with a NULL reply */
	for (i = 1; i < sc->num_reqs; i++) {
		cm = &sc->commands[i];
		if (cm->cm_state == MPS_CM_STATE_FREE)
			continue;

		cm->cm_state = MPS_CM_STATE_BUSY;
		cm->cm_reply = NULL;
		completed = 0;

		if (cm->cm_flags & MPS_CM_FLAGS_SATA_ID_TIMEOUT) {
			MPASS(cm->cm_data);
			free(cm->cm_data, M_MPT2);
			cm->cm_data = NULL;
		}

		if (cm->cm_flags & MPS_CM_FLAGS_POLLED)
			cm->cm_flags |= MPS_CM_FLAGS_COMPLETE;

		if (cm->cm_complete != NULL) {
			mpssas_log_command(cm, MPS_RECOVERY,
			    "completing cm %p state %x ccb %p for diag reset\n",
			    cm, cm->cm_state, cm->cm_ccb);

			cm->cm_complete(sc, cm);
			completed = 1;
		} else if (cm->cm_flags & MPS_CM_FLAGS_WAKEUP) {
			mpssas_log_command(cm, MPS_RECOVERY,
			    "waking up cm %p state %x ccb %p for diag reset\n", 
			    cm, cm->cm_state, cm->cm_ccb);
			wakeup(cm);
			completed = 1;
		}

		if ((completed == 0) && (cm->cm_state != MPS_CM_STATE_FREE)) {
			/* this should never happen, but if it does, log */
			mpssas_log_command(cm, MPS_RECOVERY,
			    "cm %p state %x flags 0x%x ccb %p during diag "
			    "reset\n", cm, cm->cm_state, cm->cm_flags,
			    cm->cm_ccb);
		}
	}

	sc->io_cmds_active = 0;
}

void
mpssas_handle_reinit(struct mps_softc *sc)
{
	int i;

	/* Go back into startup mode and freeze the simq, so that CAM
	 * doesn't send any commands until after we've rediscovered all
	 * targets and found the proper device handles for them.
	 *
	 * After the reset, portenable will trigger discovery, and after all
	 * discovery-related activities have finished, the simq will be
	 * released.
	 */
	mps_dprint(sc, MPS_INIT, "%s startup\n", __func__);
	sc->sassc->flags |= MPSSAS_IN_STARTUP;
	sc->sassc->flags |= MPSSAS_IN_DISCOVERY;
	mpssas_startup_increment(sc->sassc);

	/* notify CAM of a bus reset */
	mpssas_announce_reset(sc, AC_BUS_RESET, CAM_TARGET_WILDCARD, 
	    CAM_LUN_WILDCARD);

	/* complete and cleanup after all outstanding commands */
	mpssas_complete_all_commands(sc);

	mps_dprint(sc, MPS_INIT,
	    "%s startup %u after command completion\n", __func__,
	    sc->sassc->startup_refcount);

	/* zero all the target handles, since they may change after the
	 * reset, and we have to rediscover all the targets and use the new
	 * handles.  
	 */
	for (i = 0; i < sc->sassc->maxtargets; i++) {
		if (sc->sassc->targets[i].outstanding != 0)
			mps_dprint(sc, MPS_INIT, "target %u outstanding %u\n", 
			    i, sc->sassc->targets[i].outstanding);
		sc->sassc->targets[i].handle = 0x0;
		sc->sassc->targets[i].exp_dev_handle = 0x0;
		sc->sassc->targets[i].outstanding = 0;
		sc->sassc->targets[i].flags = MPSSAS_TARGET_INDIAGRESET;
	}
}

static void
mpssas_tm_timeout(void *data)
{
	struct mps_command *tm = data;
	struct mps_softc *sc = tm->cm_sc;

	mtx_assert(&sc->mps_mtx, MA_OWNED);

	mpssas_log_command(tm, MPS_INFO|MPS_RECOVERY,
	    "task mgmt %p timed out\n", tm);

	KASSERT(tm->cm_state == MPS_CM_STATE_INQUEUE,
	    ("command not inqueue\n"));

	tm->cm_state = MPS_CM_STATE_BUSY;
	mps_reinit(sc);
}

static void
mpssas_logical_unit_reset_complete(struct mps_softc *sc, struct mps_command *tm)
{
	MPI2_SCSI_TASK_MANAGE_REPLY *reply;
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	unsigned int cm_count = 0;
	struct mps_command *cm;
	struct mpssas_target *targ;

	callout_stop(&tm->cm_callout);

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	reply = (MPI2_SCSI_TASK_MANAGE_REPLY *)tm->cm_reply;
	targ = tm->cm_targ;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and
	 * task management commands don't have S/G lists.
	 * XXXSL So should it be an assertion?
	 */
	if ((tm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		mps_dprint(sc, MPS_RECOVERY|MPS_ERROR,
		    "%s: cm_flags = %#x for LUN reset! "
		   "This should not happen!\n", __func__, tm->cm_flags);
		mpssas_free_tm(sc, tm);
		return;
	}

	if (reply == NULL) {
		mps_dprint(sc, MPS_RECOVERY, "NULL reset reply for tm %p\n",
		    tm);
		if ((sc->mps_flags & MPS_FLAGS_DIAGRESET) != 0) {
			/* this completion was due to a reset, just cleanup */
			mps_dprint(sc, MPS_RECOVERY, "Hardware undergoing "
			    "reset, ignoring NULL LUN reset reply\n");
			targ->tm = NULL;
			mpssas_free_tm(sc, tm);
		}
		else {
			/* we should have gotten a reply. */
			mps_dprint(sc, MPS_INFO|MPS_RECOVERY, "NULL reply on "
			    "LUN reset attempt, resetting controller\n");
			mps_reinit(sc);
		}
		return;
	}

	mps_dprint(sc, MPS_RECOVERY,
	    "logical unit reset status 0x%x code 0x%x count %u\n",
	    le16toh(reply->IOCStatus), le32toh(reply->ResponseCode),
	    le32toh(reply->TerminationCount));
		
	/*
	 * See if there are any outstanding commands for this LUN.
	 * This could be made more efficient by using a per-LU data
	 * structure of some sort.
	 */
	TAILQ_FOREACH(cm, &targ->commands, cm_link) {
		if (cm->cm_lun == tm->cm_lun)
			cm_count++;
	}

	if (cm_count == 0) {
		mps_dprint(sc, MPS_RECOVERY|MPS_INFO,
		    "Finished recovery after LUN reset for target %u\n",
		    targ->tid);

		mpssas_announce_reset(sc, AC_SENT_BDR, targ->tid, tm->cm_lun);

		/*
		 * We've finished recovery for this logical unit.  check and
		 * see if some other logical unit has a timedout command
		 * that needs to be processed.
		 */
		cm = TAILQ_FIRST(&targ->timedout_commands);
		if (cm) {
			mps_dprint(sc, MPS_INFO|MPS_RECOVERY,
			    "More commands to abort for target %u\n",
			    targ->tid);
			mpssas_send_abort(sc, tm, cm);
		} else {
			targ->tm = NULL;
			mpssas_free_tm(sc, tm);
		}
	} else {
		/*
		 * If we still have commands for this LUN, the reset
		 * effectively failed, regardless of the status reported.
		 * Escalate to a target reset.
		 */
		mps_dprint(sc, MPS_INFO|MPS_RECOVERY,
		    "logical unit reset complete for target %u, but still "
		    "have %u command(s), sending target reset\n", targ->tid,
		    cm_count);
		mpssas_send_reset(sc, tm,
		    MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET);
	}
}

static void
mpssas_target_reset_complete(struct mps_softc *sc, struct mps_command *tm)
{
	MPI2_SCSI_TASK_MANAGE_REPLY *reply;
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mpssas_target *targ;

	callout_stop(&tm->cm_callout);

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	reply = (MPI2_SCSI_TASK_MANAGE_REPLY *)tm->cm_reply;
	targ = tm->cm_targ;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and
	 * task management commands don't have S/G lists.
	 */
	if ((tm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		mps_dprint(sc, MPS_ERROR,"%s: cm_flags = %#x for target reset! "
			   "This should not happen!\n", __func__, tm->cm_flags);
		mpssas_free_tm(sc, tm);
		return;
	}

	if (reply == NULL) {
		mps_dprint(sc, MPS_RECOVERY,
		    "NULL target reset reply for tm %pi TaskMID %u\n",
		    tm, le16toh(req->TaskMID));
		if ((sc->mps_flags & MPS_FLAGS_DIAGRESET) != 0) {
			/* this completion was due to a reset, just cleanup */
			mps_dprint(sc, MPS_RECOVERY, "Hardware undergoing "
			    "reset, ignoring NULL target reset reply\n");
			targ->tm = NULL;
			mpssas_free_tm(sc, tm);
		} else {
			/* we should have gotten a reply. */
			mps_dprint(sc, MPS_INFO|MPS_RECOVERY, "NULL reply on "
			    "target reset attempt, resetting controller\n");
			mps_reinit(sc);
		}
		return;
	}

	mps_dprint(sc, MPS_RECOVERY,
	    "target reset status 0x%x code 0x%x count %u\n",
	    le16toh(reply->IOCStatus), le32toh(reply->ResponseCode),
	    le32toh(reply->TerminationCount));

	if (targ->outstanding == 0) {
		/* we've finished recovery for this target and all
		 * of its logical units.
		 */
		mps_dprint(sc, MPS_RECOVERY|MPS_INFO,
		    "Finished reset recovery for target %u\n", targ->tid);

		mpssas_announce_reset(sc, AC_SENT_BDR, tm->cm_targ->tid,
		    CAM_LUN_WILDCARD);

		targ->tm = NULL;
		mpssas_free_tm(sc, tm);
	} else {
		/*
		 * After a target reset, if this target still has
		 * outstanding commands, the reset effectively failed,
		 * regardless of the status reported.  escalate.
		 */
		mps_dprint(sc, MPS_INFO|MPS_RECOVERY,
		    "Target reset complete for target %u, but still have %u "
		    "command(s), resetting controller\n", targ->tid,
		    targ->outstanding);
		mps_reinit(sc);
	}
}

#define MPS_RESET_TIMEOUT 30

int
mpssas_send_reset(struct mps_softc *sc, struct mps_command *tm, uint8_t type)
{
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mpssas_target *target;
	int err;

	target = tm->cm_targ;
	if (target->handle == 0) {
		mps_dprint(sc, MPS_ERROR,"%s null devhandle for target_id %d\n",
		    __func__, target->tid);
		return -1;
	}

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	req->DevHandle = htole16(target->handle);
	req->TaskType = type;

	if (type == MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET) {
		/* XXX Need to handle invalid LUNs */
		MPS_SET_LUN(req->LUN, tm->cm_lun);
		tm->cm_targ->logical_unit_resets++;
		mps_dprint(sc, MPS_RECOVERY|MPS_INFO,
		    "Sending logical unit reset to target %u lun %d\n",
		    target->tid, tm->cm_lun);
		tm->cm_complete = mpssas_logical_unit_reset_complete;
		mpssas_prepare_for_tm(sc, tm, target, tm->cm_lun);
	} else if (type == MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET) {
		/*
		 * Target reset method =
		 * 	SAS Hard Link Reset / SATA Link Reset
		 */
		req->MsgFlags = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;
		tm->cm_targ->target_resets++;
		mps_dprint(sc, MPS_RECOVERY|MPS_INFO,
		    "Sending target reset to target %u\n", target->tid);
		tm->cm_complete = mpssas_target_reset_complete;
		mpssas_prepare_for_tm(sc, tm, target, CAM_LUN_WILDCARD);
	} else {
		mps_dprint(sc, MPS_ERROR, "unexpected reset type 0x%x\n", type);
		return -1;
	}

	tm->cm_data = NULL;
	tm->cm_complete_data = (void *)tm;

	callout_reset(&tm->cm_callout, MPS_RESET_TIMEOUT * hz,
	    mpssas_tm_timeout, tm);

	err = mps_map_command(sc, tm);
	if (err)
		mps_dprint(sc, MPS_ERROR|MPS_RECOVERY,
		    "error %d sending reset type %u\n",
		    err, type);

	return err;
}


static void
mpssas_abort_complete(struct mps_softc *sc, struct mps_command *tm)
{
	struct mps_command *cm;
	MPI2_SCSI_TASK_MANAGE_REPLY *reply;
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mpssas_target *targ;

	callout_stop(&tm->cm_callout);

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	reply = (MPI2_SCSI_TASK_MANAGE_REPLY *)tm->cm_reply;
	targ = tm->cm_targ;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and
	 * task management commands don't have S/G lists.
	 */
	if ((tm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		mps_dprint(sc, MPS_RECOVERY,
		    "cm_flags = %#x for abort %p TaskMID %u!\n", 
		    tm->cm_flags, tm, le16toh(req->TaskMID));
		mpssas_free_tm(sc, tm);
		return;
	}

	if (reply == NULL) {
		mps_dprint(sc, MPS_RECOVERY,
		    "NULL abort reply for tm %p TaskMID %u\n", 
		    tm, le16toh(req->TaskMID));
		if ((sc->mps_flags & MPS_FLAGS_DIAGRESET) != 0) {
			/* this completion was due to a reset, just cleanup */
			mps_dprint(sc, MPS_RECOVERY, "Hardware undergoing "
			    "reset, ignoring NULL abort reply\n");
			targ->tm = NULL;
			mpssas_free_tm(sc, tm);
		} else {
			/* we should have gotten a reply. */
			mps_dprint(sc, MPS_INFO|MPS_RECOVERY, "NULL reply on "
			    "abort attempt, resetting controller\n");
			mps_reinit(sc);
		}
		return;
	}

	mps_dprint(sc, MPS_RECOVERY,
	    "abort TaskMID %u status 0x%x code 0x%x count %u\n",
	    le16toh(req->TaskMID),
	    le16toh(reply->IOCStatus), le32toh(reply->ResponseCode),
	    le32toh(reply->TerminationCount));

	cm = TAILQ_FIRST(&tm->cm_targ->timedout_commands);
	if (cm == NULL) {
		/*
		 * If there are no more timedout commands, we're done with
		 * error recovery for this target.
		 */
		mps_dprint(sc, MPS_INFO|MPS_RECOVERY,
		    "Finished abort recovery for target %u\n", targ->tid);

		targ->tm = NULL;
		mpssas_free_tm(sc, tm);
	} else if (le16toh(req->TaskMID) != cm->cm_desc.Default.SMID) {
		/* abort success, but we have more timedout commands to abort */
		mps_dprint(sc, MPS_INFO|MPS_RECOVERY,
		    "Continuing abort recovery for target %u\n", targ->tid);
		
		mpssas_send_abort(sc, tm, cm);
	} else {
		/* we didn't get a command completion, so the abort
		 * failed as far as we're concerned.  escalate.
		 */
		mps_dprint(sc, MPS_RECOVERY,
		    "Abort failed for target %u, sending logical unit reset\n",
		    targ->tid);

		mpssas_send_reset(sc, tm, 
		    MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET);
	}
}

#define MPS_ABORT_TIMEOUT 5

static int
mpssas_send_abort(struct mps_softc *sc, struct mps_command *tm, struct mps_command *cm)
{
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mpssas_target *targ;
	int err;

	targ = cm->cm_targ;
	if (targ->handle == 0) {
		mps_dprint(sc, MPS_ERROR|MPS_RECOVERY,
		    "%s null devhandle for target_id %d\n",
		    __func__, cm->cm_ccb->ccb_h.target_id);
		return -1;
	}

	mpssas_log_command(cm, MPS_RECOVERY|MPS_INFO,
	    "Aborting command %p\n", cm);

	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	req->DevHandle = htole16(targ->handle);
	req->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK;

	/* XXX Need to handle invalid LUNs */
	MPS_SET_LUN(req->LUN, cm->cm_ccb->ccb_h.target_lun);

	req->TaskMID = htole16(cm->cm_desc.Default.SMID);

	tm->cm_data = NULL;
	tm->cm_complete = mpssas_abort_complete;
	tm->cm_complete_data = (void *)tm;
	tm->cm_targ = cm->cm_targ;
	tm->cm_lun = cm->cm_lun;

	callout_reset(&tm->cm_callout, MPS_ABORT_TIMEOUT * hz,
	    mpssas_tm_timeout, tm);

	targ->aborts++;

	mpssas_prepare_for_tm(sc, tm, targ, tm->cm_lun);

	err = mps_map_command(sc, tm);
	if (err)
		mps_dprint(sc, MPS_ERROR|MPS_RECOVERY,
		    "error %d sending abort for cm %p SMID %u\n",
		    err, cm, req->TaskMID);
	return err;
}

static void
mpssas_scsiio_timeout(void *data)
{
	sbintime_t elapsed, now;
	union ccb *ccb;
	struct mps_softc *sc;
	struct mps_command *cm;
	struct mpssas_target *targ;

	cm = (struct mps_command *)data;
	sc = cm->cm_sc;
	ccb = cm->cm_ccb;
	now = sbinuptime();

	MPS_FUNCTRACE(sc);
	mtx_assert(&sc->mps_mtx, MA_OWNED);

	mps_dprint(sc, MPS_XINFO|MPS_RECOVERY, "Timeout checking cm %p\n", sc);

	/*
	 * Run the interrupt handler to make sure it's not pending.  This
	 * isn't perfect because the command could have already completed
	 * and been re-used, though this is unlikely.
	 */
	mps_intr_locked(sc);
	if (cm->cm_state != MPS_CM_STATE_INQUEUE) {
		mpssas_log_command(cm, MPS_XINFO,
		    "SCSI command %p almost timed out\n", cm);
		return;
	}

	if (cm->cm_ccb == NULL) {
		mps_dprint(sc, MPS_ERROR, "command timeout with NULL ccb\n");
		return;
	}

	targ = cm->cm_targ;
	targ->timeouts++;

	elapsed = now - ccb->ccb_h.qos.sim_data;
	mpssas_log_command(cm, MPS_INFO|MPS_RECOVERY,
	    "Command timeout on target %u(0x%04x) %d set, %d.%d elapsed\n",
	    targ->tid, targ->handle, ccb->ccb_h.timeout,
	    sbintime_getsec(elapsed), elapsed & 0xffffffff);

	/* XXX first, check the firmware state, to see if it's still
	 * operational.  if not, do a diag reset.
	 */
	mpssas_set_ccbstatus(cm->cm_ccb, CAM_CMD_TIMEOUT);
	cm->cm_state = MPS_CM_STATE_TIMEDOUT;
	TAILQ_INSERT_TAIL(&targ->timedout_commands, cm, cm_recovery);

	if (targ->tm != NULL) {
		/* target already in recovery, just queue up another
		 * timedout command to be processed later.
		 */
		mps_dprint(sc, MPS_RECOVERY,
		    "queued timedout cm %p for processing by tm %p\n",
		    cm, targ->tm);
	} else if ((targ->tm = mpssas_alloc_tm(sc)) != NULL) {
		mps_dprint(sc, MPS_RECOVERY|MPS_INFO,
		    "Sending abort to target %u for SMID %d\n", targ->tid,
		    cm->cm_desc.Default.SMID);
		mps_dprint(sc, MPS_RECOVERY, "timedout cm %p allocated tm %p\n",
		    cm, targ->tm);

		/* start recovery by aborting the first timedout command */
		mpssas_send_abort(sc, targ->tm, cm);
	} else {
		/* XXX queue this target up for recovery once a TM becomes
		 * available.  The firmware only has a limited number of
		 * HighPriority credits for the high priority requests used
		 * for task management, and we ran out.
		 * 
		 * Isilon: don't worry about this for now, since we have
		 * more credits than disks in an enclosure, and limit
		 * ourselves to one TM per target for recovery.
		 */
		mps_dprint(sc, MPS_ERROR|MPS_RECOVERY,
		    "timedout cm %p failed to allocate a tm\n", cm);
	}

}

static void
mpssas_action_scsiio(struct mpssas_softc *sassc, union ccb *ccb)
{
	MPI2_SCSI_IO_REQUEST *req;
	struct ccb_scsiio *csio;
	struct mps_softc *sc;
	struct mpssas_target *targ;
	struct mpssas_lun *lun;
	struct mps_command *cm;
	uint8_t i, lba_byte, *ref_tag_addr;
	uint16_t eedp_flags;
	uint32_t mpi_control;

	sc = sassc->sc;
	MPS_FUNCTRACE(sc);
	mtx_assert(&sc->mps_mtx, MA_OWNED);

	csio = &ccb->csio;
	KASSERT(csio->ccb_h.target_id < sassc->maxtargets,
	    ("Target %d out of bounds in XPT_SCSI_IO\n",
	     csio->ccb_h.target_id));
	targ = &sassc->targets[csio->ccb_h.target_id];
	mps_dprint(sc, MPS_TRACE, "ccb %p target flag %x\n", ccb, targ->flags);
	if (targ->handle == 0x0) {
		mps_dprint(sc, MPS_ERROR, "%s NULL handle for target %u\n", 
		    __func__, csio->ccb_h.target_id);
		mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
		xpt_done(ccb);
		return;
	}
	if (targ->flags & MPS_TARGET_FLAGS_RAID_COMPONENT) {
		mps_dprint(sc, MPS_ERROR, "%s Raid component no SCSI IO "
		    "supported %u\n", __func__, csio->ccb_h.target_id);
		mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
		xpt_done(ccb);
		return;
	}
	/*
	 * Sometimes, it is possible to get a command that is not "In
	 * Progress" and was actually aborted by the upper layer.  Check for
	 * this here and complete the command without error.
	 */
	if (mpssas_get_ccbstatus(ccb) != CAM_REQ_INPROG) {
		mps_dprint(sc, MPS_TRACE, "%s Command is not in progress for "
		    "target %u\n", __func__, csio->ccb_h.target_id);
		xpt_done(ccb);
		return;
	}
	/*
	 * If devinfo is 0 this will be a volume.  In that case don't tell CAM
	 * that the volume has timed out.  We want volumes to be enumerated
	 * until they are deleted/removed, not just failed.
	 */
	if (targ->flags & MPSSAS_TARGET_INREMOVAL) {
		if (targ->devinfo == 0)
			mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		else
			mpssas_set_ccbstatus(ccb, CAM_SEL_TIMEOUT);
		xpt_done(ccb);
		return;
	}

	if ((sc->mps_flags & MPS_FLAGS_SHUTDOWN) != 0) {
		mps_dprint(sc, MPS_INFO, "%s shutting down\n", __func__);
		mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
		xpt_done(ccb);
		return;
	}

	/*
	 * If target has a reset in progress, freeze the devq and return.  The
	 * devq will be released when the TM reset is finished.
	 */
	if (targ->flags & MPSSAS_TARGET_INRESET) {
		ccb->ccb_h.status = CAM_BUSY | CAM_DEV_QFRZN;
		mps_dprint(sc, MPS_INFO, "%s: Freezing devq for target ID %d\n",
		    __func__, targ->tid);
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		xpt_done(ccb);
		return;
	}

	cm = mps_alloc_command(sc);
	if (cm == NULL || (sc->mps_flags & MPS_FLAGS_DIAGRESET)) {
		if (cm != NULL) {
			mps_free_command(sc, cm);
		}
		if ((sassc->flags & MPSSAS_QUEUE_FROZEN) == 0) {
			xpt_freeze_simq(sassc->sim, 1);
			sassc->flags |= MPSSAS_QUEUE_FROZEN;
		}
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		xpt_done(ccb);
		return;
	}

	req = (MPI2_SCSI_IO_REQUEST *)cm->cm_req;
	bzero(req, sizeof(*req));
	req->DevHandle = htole16(targ->handle);
	req->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	req->MsgFlags = 0;
	req->SenseBufferLowAddress = htole32(cm->cm_sense_busaddr);
	req->SenseBufferLength = MPS_SENSE_LEN;
	req->SGLFlags = 0;
	req->ChainOffset = 0;
	req->SGLOffset0 = 24;	/* 32bit word offset to the SGL */
	req->SGLOffset1= 0;
	req->SGLOffset2= 0;
	req->SGLOffset3= 0;
	req->SkipCount = 0;
	req->DataLength = htole32(csio->dxfer_len);
	req->BidirectionalDataLength = 0;
	req->IoFlags = htole16(csio->cdb_len);
	req->EEDPFlags = 0;

	/* Note: BiDirectional transfers are not supported */
	switch (csio->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		mpi_control = MPI2_SCSIIO_CONTROL_READ;
		cm->cm_flags |= MPS_CM_FLAGS_DATAIN;
		break;
	case CAM_DIR_OUT:
		mpi_control = MPI2_SCSIIO_CONTROL_WRITE;
		cm->cm_flags |= MPS_CM_FLAGS_DATAOUT;
		break;
	case CAM_DIR_NONE:
	default:
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;
		break;
	}
 
	if (csio->cdb_len == 32)
                mpi_control |= 4 << MPI2_SCSIIO_CONTROL_ADDCDBLEN_SHIFT;
	/*
	 * It looks like the hardware doesn't require an explicit tag
	 * number for each transaction.  SAM Task Management not supported
	 * at the moment.
	 */
	switch (csio->tag_action) {
	case MSG_HEAD_OF_Q_TAG:
		mpi_control |= MPI2_SCSIIO_CONTROL_HEADOFQ;
		break;
	case MSG_ORDERED_Q_TAG:
		mpi_control |= MPI2_SCSIIO_CONTROL_ORDEREDQ;
		break;
	case MSG_ACA_TASK:
		mpi_control |= MPI2_SCSIIO_CONTROL_ACAQ;
		break;
	case CAM_TAG_ACTION_NONE:
	case MSG_SIMPLE_Q_TAG:
	default:
		mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
		break;
	}
	mpi_control |= sc->mapping_table[csio->ccb_h.target_id].TLR_bits;
	req->Control = htole32(mpi_control);
	if (MPS_SET_LUN(req->LUN, csio->ccb_h.target_lun) != 0) {
		mps_free_command(sc, cm);
		mpssas_set_ccbstatus(ccb, CAM_LUN_INVALID);
		xpt_done(ccb);
		return;
	}

	if (csio->ccb_h.flags & CAM_CDB_POINTER)
		bcopy(csio->cdb_io.cdb_ptr, &req->CDB.CDB32[0], csio->cdb_len);
	else
		bcopy(csio->cdb_io.cdb_bytes, &req->CDB.CDB32[0],csio->cdb_len);
	req->IoFlags = htole16(csio->cdb_len);

	/*
	 * Check if EEDP is supported and enabled.  If it is then check if the
	 * SCSI opcode could be using EEDP.  If so, make sure the LUN exists and
	 * is formatted for EEDP support.  If all of this is true, set CDB up
	 * for EEDP transfer.
	 */
	eedp_flags = op_code_prot[req->CDB.CDB32[0]];
	if (sc->eedp_enabled && eedp_flags) {
		SLIST_FOREACH(lun, &targ->luns, lun_link) {
			if (lun->lun_id == csio->ccb_h.target_lun) {
				break;
			}
		}

		if ((lun != NULL) && (lun->eedp_formatted)) {
			req->EEDPBlockSize = htole16(lun->eedp_block_size);
			eedp_flags |= (MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
			    MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
			    MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD);
			req->EEDPFlags = htole16(eedp_flags);

			/*
			 * If CDB less than 32, fill in Primary Ref Tag with
			 * low 4 bytes of LBA.  If CDB is 32, tag stuff is
			 * already there.  Also, set protection bit.  FreeBSD
			 * currently does not support CDBs bigger than 16, but
			 * the code doesn't hurt, and will be here for the
			 * future.
			 */
			if (csio->cdb_len != 32) {
				lba_byte = (csio->cdb_len == 16) ? 6 : 2;
				ref_tag_addr = (uint8_t *)&req->CDB.EEDP32.
				    PrimaryReferenceTag;
				for (i = 0; i < 4; i++) {
					*ref_tag_addr =
					    req->CDB.CDB32[lba_byte + i];
					ref_tag_addr++;
				}
				req->CDB.EEDP32.PrimaryReferenceTag = 
					htole32(req->CDB.EEDP32.PrimaryReferenceTag);
				req->CDB.EEDP32.PrimaryApplicationTagMask =
				    0xFFFF;
				req->CDB.CDB32[1] = (req->CDB.CDB32[1] & 0x1F) |
				    0x20;
			} else {
				eedp_flags |=
				    MPI2_SCSIIO_EEDPFLAGS_INC_PRI_APPTAG;
				req->EEDPFlags = htole16(eedp_flags);
				req->CDB.CDB32[10] = (req->CDB.CDB32[10] &
				    0x1F) | 0x20;
			}
		}
	}

	cm->cm_length = csio->dxfer_len;
	if (cm->cm_length != 0) {
		cm->cm_data = ccb;
		cm->cm_flags |= MPS_CM_FLAGS_USE_CCB;
	} else {
		cm->cm_data = NULL;
	}
	cm->cm_sge = &req->SGL;
	cm->cm_sglsize = (32 - 24) * 4;
	cm->cm_desc.SCSIIO.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	cm->cm_desc.SCSIIO.DevHandle = htole16(targ->handle);
	cm->cm_complete = mpssas_scsiio_complete;
	cm->cm_complete_data = ccb;
	cm->cm_targ = targ;
	cm->cm_lun = csio->ccb_h.target_lun;
	cm->cm_ccb = ccb;

	/*
	 * If HBA is a WD and the command is not for a retry, try to build a
	 * direct I/O message. If failed, or the command is for a retry, send
	 * the I/O to the IR volume itself.
	 */
	if (sc->WD_valid_config) {
		if (ccb->ccb_h.sim_priv.entries[0].field == MPS_WD_RETRY) {
			mpssas_direct_drive_io(sassc, cm, ccb);
		} else {
			mpssas_set_ccbstatus(ccb, CAM_REQ_INPROG);
		}
	}

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	if (csio->bio != NULL)
		biotrack(csio->bio, __func__);
#endif
	csio->ccb_h.qos.sim_data = sbinuptime();
	callout_reset_sbt(&cm->cm_callout, SBT_1MS * ccb->ccb_h.timeout, 0,
	    mpssas_scsiio_timeout, cm, 0);

	targ->issued++;
	targ->outstanding++;
	TAILQ_INSERT_TAIL(&targ->commands, cm, cm_link);
	ccb->ccb_h.status |= CAM_SIM_QUEUED;

	mpssas_log_command(cm, MPS_XINFO, "%s cm %p ccb %p outstanding %u\n",
	    __func__, cm, ccb, targ->outstanding);

	mps_map_command(sc, cm);
	return;
}

/**
 * mps_sc_failed_io_info - translated non-succesfull SCSI_IO request
 */
static void
mps_sc_failed_io_info(struct mps_softc *sc, struct ccb_scsiio *csio,
    Mpi2SCSIIOReply_t *mpi_reply)
{
	u32 response_info;
	u8 *response_bytes;
	u16 ioc_status = le16toh(mpi_reply->IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	u8 scsi_state = mpi_reply->SCSIState;
	u8 scsi_status = mpi_reply->SCSIStatus;
	u32 log_info = le32toh(mpi_reply->IOCLogInfo);
	const char *desc_ioc_state, *desc_scsi_status;
	
	if (log_info == 0x31170000)
		return;

	desc_ioc_state = mps_describe_table(mps_iocstatus_string,
	    ioc_status);
	desc_scsi_status = mps_describe_table(mps_scsi_status_string,
	    scsi_status);

	mps_dprint(sc, MPS_XINFO, "\thandle(0x%04x), ioc_status(%s)(0x%04x)\n",
	    le16toh(mpi_reply->DevHandle), desc_ioc_state, ioc_status);

	/*
	 *We can add more detail about underflow data here
	 * TO-DO
	 */
	mps_dprint(sc, MPS_XINFO, "\tscsi_status(%s)(0x%02x), "
	    "scsi_state %b\n", desc_scsi_status, scsi_status,
	    scsi_state, "\20" "\1AutosenseValid" "\2AutosenseFailed"
	    "\3NoScsiStatus" "\4Terminated" "\5Response InfoValid");

	if (sc->mps_debug & MPS_XINFO &&
		scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		mps_dprint(sc, MPS_XINFO, "-> Sense Buffer Data : Start :\n");
		scsi_sense_print(csio);
		mps_dprint(sc, MPS_XINFO, "-> Sense Buffer Data : End :\n");
	}

	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID) {
		response_info = le32toh(mpi_reply->ResponseInfo);
		response_bytes = (u8 *)&response_info;
		mps_dprint(sc, MPS_XINFO, "response code(0x%1x): %s\n",
		    response_bytes[0],
		    mps_describe_table(mps_scsi_taskmgmt_string,
		    response_bytes[0]));
	}
}

static void
mpssas_scsiio_complete(struct mps_softc *sc, struct mps_command *cm)
{
	MPI2_SCSI_IO_REPLY *rep;
	union ccb *ccb;
	struct ccb_scsiio *csio;
	struct mpssas_softc *sassc;
	struct scsi_vpd_supported_page_list *vpd_list = NULL;
	u8 *TLR_bits, TLR_on;
	int dir = 0, i;
	u16 alloc_len;
	struct mpssas_target *target;
	target_id_t target_id;

	MPS_FUNCTRACE(sc);
	mps_dprint(sc, MPS_TRACE,
	    "cm %p SMID %u ccb %p reply %p outstanding %u\n", cm,
	    cm->cm_desc.Default.SMID, cm->cm_ccb, cm->cm_reply,
	    cm->cm_targ->outstanding);

	callout_stop(&cm->cm_callout);
	mtx_assert(&sc->mps_mtx, MA_OWNED);

	sassc = sc->sassc;
	ccb = cm->cm_complete_data;
	csio = &ccb->csio;
	target_id = csio->ccb_h.target_id;
	rep = (MPI2_SCSI_IO_REPLY *)cm->cm_reply;
	/*
	 * XXX KDM if the chain allocation fails, does it matter if we do
	 * the sync and unload here?  It is simpler to do it in every case,
	 * assuming it doesn't cause problems.
	 */
	if (cm->cm_data != NULL) {
		if (cm->cm_flags & MPS_CM_FLAGS_DATAIN)
			dir = BUS_DMASYNC_POSTREAD;
		else if (cm->cm_flags & MPS_CM_FLAGS_DATAOUT)
			dir = BUS_DMASYNC_POSTWRITE;
		bus_dmamap_sync(sc->buffer_dmat, cm->cm_dmamap, dir);
		bus_dmamap_unload(sc->buffer_dmat, cm->cm_dmamap);
	}

	cm->cm_targ->completed++;
	cm->cm_targ->outstanding--;
	TAILQ_REMOVE(&cm->cm_targ->commands, cm, cm_link);
	ccb->ccb_h.status &= ~(CAM_STATUS_MASK | CAM_SIM_QUEUED);

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	if (ccb->csio.bio != NULL)
		biotrack(ccb->csio.bio, __func__);
#endif

	if (cm->cm_state == MPS_CM_STATE_TIMEDOUT) {
		TAILQ_REMOVE(&cm->cm_targ->timedout_commands, cm, cm_recovery);
		cm->cm_state = MPS_CM_STATE_BUSY;
		if (cm->cm_reply != NULL)
			mpssas_log_command(cm, MPS_RECOVERY,
			    "completed timedout cm %p ccb %p during recovery "
			    "ioc %x scsi %x state %x xfer %u\n",
			    cm, cm->cm_ccb, le16toh(rep->IOCStatus),
			    rep->SCSIStatus, rep->SCSIState,
			    le32toh(rep->TransferCount));
		else
			mpssas_log_command(cm, MPS_RECOVERY,
			    "completed timedout cm %p ccb %p during recovery\n",
			    cm, cm->cm_ccb);
	} else if (cm->cm_targ->tm != NULL) {
		if (cm->cm_reply != NULL)
			mpssas_log_command(cm, MPS_RECOVERY,
			    "completed cm %p ccb %p during recovery "
			    "ioc %x scsi %x state %x xfer %u\n",
			    cm, cm->cm_ccb, le16toh(rep->IOCStatus),
			    rep->SCSIStatus, rep->SCSIState,
			    le32toh(rep->TransferCount));
		else
			mpssas_log_command(cm, MPS_RECOVERY,
			    "completed cm %p ccb %p during recovery\n",
			    cm, cm->cm_ccb);
	} else if ((sc->mps_flags & MPS_FLAGS_DIAGRESET) != 0) {
		mpssas_log_command(cm, MPS_RECOVERY,
		    "reset completed cm %p ccb %p\n",
		    cm, cm->cm_ccb);
	}

	if ((cm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		/*
		 * We ran into an error after we tried to map the command,
		 * so we're getting a callback without queueing the command
		 * to the hardware.  So we set the status here, and it will
		 * be retained below.  We'll go through the "fast path",
		 * because there can be no reply when we haven't actually
		 * gone out to the hardware.
		 */
		mpssas_set_ccbstatus(ccb, CAM_REQUEUE_REQ);

		/*
		 * Currently the only error included in the mask is
		 * MPS_CM_FLAGS_CHAIN_FAILED, which means we're out of
		 * chain frames.  We need to freeze the queue until we get
		 * a command that completed without this error, which will
		 * hopefully have some chain frames attached that we can
		 * use.  If we wanted to get smarter about it, we would
		 * only unfreeze the queue in this condition when we're
		 * sure that we're getting some chain frames back.  That's
		 * probably unnecessary.
		 */
		if ((sassc->flags & MPSSAS_QUEUE_FROZEN) == 0) {
			xpt_freeze_simq(sassc->sim, 1);
			sassc->flags |= MPSSAS_QUEUE_FROZEN;
			mps_dprint(sc, MPS_XINFO, "Error sending command, "
				   "freezing SIM queue\n");
		}
	}

	/*
	 * If this is a Start Stop Unit command and it was issued by the driver
	 * during shutdown, decrement the refcount to account for all of the
	 * commands that were sent.  All SSU commands should be completed before
	 * shutdown completes, meaning SSU_refcount will be 0 after SSU_started
	 * is TRUE.
	 */
	if (sc->SSU_started && (csio->cdb_io.cdb_bytes[0] == START_STOP_UNIT)) {
		mps_dprint(sc, MPS_INFO, "Decrementing SSU count.\n");
		sc->SSU_refcount--;
	}

	/* Take the fast path to completion */
	if (cm->cm_reply == NULL) {
		if (mpssas_get_ccbstatus(ccb) == CAM_REQ_INPROG) {
			if ((sc->mps_flags & MPS_FLAGS_DIAGRESET) != 0)
				mpssas_set_ccbstatus(ccb, CAM_SCSI_BUS_RESET);
			else {
				mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
				ccb->csio.scsi_status = SCSI_STATUS_OK;
			}
			if (sassc->flags & MPSSAS_QUEUE_FROZEN) {
				ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
				sassc->flags &= ~MPSSAS_QUEUE_FROZEN;
				mps_dprint(sc, MPS_XINFO,
				    "Unfreezing SIM queue\n");
			}
		} 

		/*
		 * There are two scenarios where the status won't be
		 * CAM_REQ_CMP.  The first is if MPS_CM_FLAGS_ERROR_MASK is
		 * set, the second is in the MPS_FLAGS_DIAGRESET above.
		 */
		if (mpssas_get_ccbstatus(ccb) != CAM_REQ_CMP) {
			/*
			 * Freeze the dev queue so that commands are
			 * executed in the correct order after error
			 * recovery.
			 */
			ccb->ccb_h.status |= CAM_DEV_QFRZN;
			xpt_freeze_devq(ccb->ccb_h.path, /*count*/ 1);
		}
		mps_free_command(sc, cm);
		xpt_done(ccb);
		return;
	}

	mpssas_log_command(cm, MPS_XINFO,
	    "ioc %x scsi %x state %x xfer %u\n",
	    le16toh(rep->IOCStatus), rep->SCSIStatus, rep->SCSIState,
	    le32toh(rep->TransferCount));

	/*
	 * If this is a Direct Drive I/O, reissue the I/O to the original IR
	 * Volume if an error occurred (normal I/O retry).  Use the original
	 * CCB, but set a flag that this will be a retry so that it's sent to
	 * the original volume.  Free the command but reuse the CCB.
	 */
	if (cm->cm_flags & MPS_CM_FLAGS_DD_IO) {
		mps_free_command(sc, cm);
		ccb->ccb_h.sim_priv.entries[0].field = MPS_WD_RETRY;
		mpssas_action_scsiio(sassc, ccb);
		return;
	} else
		ccb->ccb_h.sim_priv.entries[0].field = 0;

	switch (le16toh(rep->IOCStatus) & MPI2_IOCSTATUS_MASK) {
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
		csio->resid = cm->cm_length - le32toh(rep->TransferCount);
		/* FALLTHROUGH */
	case MPI2_IOCSTATUS_SUCCESS:
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:

		if ((le16toh(rep->IOCStatus) & MPI2_IOCSTATUS_MASK) ==
		    MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR)
			mpssas_log_command(cm, MPS_XINFO, "recovered error\n");

		/* Completion failed at the transport level. */
		if (rep->SCSIState & (MPI2_SCSI_STATE_NO_SCSI_STATUS |
		    MPI2_SCSI_STATE_TERMINATED)) {
			mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);
			break;
		}

		/* In a modern packetized environment, an autosense failure
		 * implies that there's not much else that can be done to
		 * recover the command.
		 */
		if (rep->SCSIState & MPI2_SCSI_STATE_AUTOSENSE_FAILED) {
			mpssas_set_ccbstatus(ccb, CAM_AUTOSENSE_FAIL);
			break;
		}

		/*
		 * CAM doesn't care about SAS Response Info data, but if this is
		 * the state check if TLR should be done.  If not, clear the
		 * TLR_bits for the target.
		 */
		if ((rep->SCSIState & MPI2_SCSI_STATE_RESPONSE_INFO_VALID) &&
		    ((le32toh(rep->ResponseInfo) &
		    MPI2_SCSI_RI_MASK_REASONCODE) ==
		    MPS_SCSI_RI_INVALID_FRAME)) {
			sc->mapping_table[target_id].TLR_bits =
			    (u8)MPI2_SCSIIO_CONTROL_NO_TLR;
		}

		/*
		 * Intentionally override the normal SCSI status reporting
		 * for these two cases.  These are likely to happen in a
		 * multi-initiator environment, and we want to make sure that
		 * CAM retries these commands rather than fail them.
		 */
		if ((rep->SCSIStatus == MPI2_SCSI_STATUS_COMMAND_TERMINATED) ||
		    (rep->SCSIStatus == MPI2_SCSI_STATUS_TASK_ABORTED)) {
			mpssas_set_ccbstatus(ccb, CAM_REQ_ABORTED);
			break;
		}

		/* Handle normal status and sense */
		csio->scsi_status = rep->SCSIStatus;
		if (rep->SCSIStatus == MPI2_SCSI_STATUS_GOOD)
			mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		else
			mpssas_set_ccbstatus(ccb, CAM_SCSI_STATUS_ERROR);

		if (rep->SCSIState & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
			int sense_len, returned_sense_len;

			returned_sense_len = min(le32toh(rep->SenseCount),
			    sizeof(struct scsi_sense_data));
			if (returned_sense_len < ccb->csio.sense_len)
				ccb->csio.sense_resid = ccb->csio.sense_len -
					returned_sense_len;
			else
				ccb->csio.sense_resid = 0;

			sense_len = min(returned_sense_len,
			    ccb->csio.sense_len - ccb->csio.sense_resid);
			bzero(&ccb->csio.sense_data,
			      sizeof(ccb->csio.sense_data));
			bcopy(cm->cm_sense, &ccb->csio.sense_data, sense_len);
			ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
		}

		/*
		 * Check if this is an INQUIRY command.  If it's a VPD inquiry,
		 * and it's page code 0 (Supported Page List), and there is
		 * inquiry data, and this is for a sequential access device, and
		 * the device is an SSP target, and TLR is supported by the
		 * controller, turn the TLR_bits value ON if page 0x90 is
		 * supported.
		 */
		if ((csio->cdb_io.cdb_bytes[0] == INQUIRY) &&
		    (csio->cdb_io.cdb_bytes[1] & SI_EVPD) &&
		    (csio->cdb_io.cdb_bytes[2] == SVPD_SUPPORTED_PAGE_LIST) &&
		    ((csio->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_VADDR) &&
		    (csio->data_ptr != NULL) &&
		    ((csio->data_ptr[0] & 0x1f) == T_SEQUENTIAL) &&
		    (sc->control_TLR) &&
		    (sc->mapping_table[target_id].device_info &
		    MPI2_SAS_DEVICE_INFO_SSP_TARGET)) {
			vpd_list = (struct scsi_vpd_supported_page_list *)
			    csio->data_ptr;
			TLR_bits = &sc->mapping_table[target_id].TLR_bits;
			*TLR_bits = (u8)MPI2_SCSIIO_CONTROL_NO_TLR;
			TLR_on = (u8)MPI2_SCSIIO_CONTROL_TLR_ON;
			alloc_len = ((u16)csio->cdb_io.cdb_bytes[3] << 8) +
			    csio->cdb_io.cdb_bytes[4];
			alloc_len -= csio->resid;
			for (i = 0; i < MIN(vpd_list->length, alloc_len); i++) {
				if (vpd_list->list[i] == 0x90) {
					*TLR_bits = TLR_on;
					break;
				}
			}
		}

		/*
		 * If this is a SATA direct-access end device, mark it so that
		 * a SCSI StartStopUnit command will be sent to it when the
		 * driver is being shutdown.
		 */
		if ((csio->cdb_io.cdb_bytes[0] == INQUIRY) &&
		    ((csio->data_ptr[0] & 0x1f) == T_DIRECT) &&
		    (sc->mapping_table[target_id].device_info &
		    MPI2_SAS_DEVICE_INFO_SATA_DEVICE) &&
		    ((sc->mapping_table[target_id].device_info &
		    MPI2_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) ==
		    MPI2_SAS_DEVICE_INFO_END_DEVICE)) {
			target = &sassc->targets[target_id];
			target->supports_SSU = TRUE;
			mps_dprint(sc, MPS_XINFO, "Target %d supports SSU\n",
			    target_id);
		}
		break;
	case MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		/*
		 * If devinfo is 0 this will be a volume.  In that case don't
		 * tell CAM that the volume is not there.  We want volumes to
		 * be enumerated until they are deleted/removed, not just
		 * failed.
		 */
		if (cm->cm_targ->devinfo == 0)
			mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		else
			mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
		break;
	case MPI2_IOCSTATUS_INVALID_SGL:
		mps_print_scsiio_cmd(sc, cm);
		mpssas_set_ccbstatus(ccb, CAM_UNREC_HBA_ERROR);
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
		/*
		 * This is one of the responses that comes back when an I/O
		 * has been aborted.  If it is because of a timeout that we
		 * initiated, just set the status to CAM_CMD_TIMEOUT.
		 * Otherwise set it to CAM_REQ_ABORTED.  The effect on the
		 * command is the same (it gets retried, subject to the
		 * retry counter), the only difference is what gets printed
		 * on the console.
		 */
		if (cm->cm_state == MPS_CM_STATE_TIMEDOUT)
			mpssas_set_ccbstatus(ccb, CAM_CMD_TIMEOUT);
		else
			mpssas_set_ccbstatus(ccb, CAM_REQ_ABORTED);
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
		/* resid is ignored for this condition */
		csio->resid = 0;
		mpssas_set_ccbstatus(ccb, CAM_DATA_RUN_ERR);
		break;
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		/*
		 * These can sometimes be transient transport-related
		 * errors, and sometimes persistent drive-related errors.
		 * We used to retry these without decrementing the retry
		 * count by returning CAM_REQUEUE_REQ.  Unfortunately, if
		 * we hit a persistent drive problem that returns one of
		 * these error codes, we would retry indefinitely.  So,
		 * return CAM_REQ_CMP_ERROR so that we decrement the retry
		 * count and avoid infinite retries.  We're taking the
		 * potential risk of flagging false failures in the event
		 * of a topology-related error (e.g. a SAS expander problem
		 * causes a command addressed to a drive to fail), but
		 * avoiding getting into an infinite retry loop.
		 */
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);
		mps_dprint(sc, MPS_INFO,
		    "Controller reported %s tgt %u SMID %u loginfo %x\n",
		    mps_describe_table(mps_iocstatus_string,
		    le16toh(rep->IOCStatus) & MPI2_IOCSTATUS_MASK),
		    target_id, cm->cm_desc.Default.SMID,
		    le32toh(rep->IOCLogInfo));
		mps_dprint(sc, MPS_XINFO,
		    "SCSIStatus %x SCSIState %x xfercount %u\n",
		    rep->SCSIStatus, rep->SCSIState,
		    le32toh(rep->TransferCount));
		break;
	case MPI2_IOCSTATUS_INVALID_FUNCTION:
	case MPI2_IOCSTATUS_INTERNAL_ERROR:
	case MPI2_IOCSTATUS_INVALID_VPID:
	case MPI2_IOCSTATUS_INVALID_FIELD:
	case MPI2_IOCSTATUS_INVALID_STATE:
	case MPI2_IOCSTATUS_OP_STATE_NOT_SUPPORTED:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	default:
		mpssas_log_command(cm, MPS_XINFO,
		    "completed ioc %x loginfo %x scsi %x state %x xfer %u\n",
		    le16toh(rep->IOCStatus), le32toh(rep->IOCLogInfo),
		    rep->SCSIStatus, rep->SCSIState,
		    le32toh(rep->TransferCount));
		csio->resid = cm->cm_length;
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);
		break;
	}
	
	mps_sc_failed_io_info(sc,csio,rep);

	if (sassc->flags & MPSSAS_QUEUE_FROZEN) {
		ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
		sassc->flags &= ~MPSSAS_QUEUE_FROZEN;
		mps_dprint(sc, MPS_XINFO, "Command completed, "
		    "unfreezing SIM queue\n");
	}

	if (mpssas_get_ccbstatus(ccb) != CAM_REQ_CMP) {
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
		xpt_freeze_devq(ccb->ccb_h.path, /*count*/ 1);
	}

	mps_free_command(sc, cm);
	xpt_done(ccb);
}

/* All Request reached here are Endian safe */
static void
mpssas_direct_drive_io(struct mpssas_softc *sassc, struct mps_command *cm,
    union ccb *ccb) {
	pMpi2SCSIIORequest_t	pIO_req;
	struct mps_softc	*sc = sassc->sc;
	uint64_t		virtLBA;
	uint32_t		physLBA, stripe_offset, stripe_unit;
	uint32_t		io_size, column;
	uint8_t			*ptrLBA, lba_idx, physLBA_byte, *CDB;

	/*
	 * If this is a valid SCSI command (Read6, Read10, Read16, Write6,
	 * Write10, or Write16), build a direct I/O message.  Otherwise, the I/O
	 * will be sent to the IR volume itself.  Since Read6 and Write6 are a
	 * bit different than the 10/16 CDBs, handle them separately.
	 */
	pIO_req = (pMpi2SCSIIORequest_t)cm->cm_req;
	CDB = pIO_req->CDB.CDB32;

	/*
	 * Handle 6 byte CDBs.
	 */
	if ((pIO_req->DevHandle == sc->DD_dev_handle) && ((CDB[0] == READ_6) ||
	    (CDB[0] == WRITE_6))) {
		/*
		 * Get the transfer size in blocks.
		 */
		io_size = (cm->cm_length >> sc->DD_block_exponent);

		/*
		 * Get virtual LBA given in the CDB.
		 */
		virtLBA = ((uint64_t)(CDB[1] & 0x1F) << 16) |
		    ((uint64_t)CDB[2] << 8) | (uint64_t)CDB[3];

		/*
		 * Check that LBA range for I/O does not exceed volume's
		 * MaxLBA.
		 */
		if ((virtLBA + (uint64_t)io_size - 1) <=
		    sc->DD_max_lba) {
			/*
			 * Check if the I/O crosses a stripe boundary.  If not,
			 * translate the virtual LBA to a physical LBA and set
			 * the DevHandle for the PhysDisk to be used.  If it
			 * does cross a boundary, do normal I/O.  To get the
			 * right DevHandle to use, get the map number for the
			 * column, then use that map number to look up the
			 * DevHandle of the PhysDisk.
			 */
			stripe_offset = (uint32_t)virtLBA &
			    (sc->DD_stripe_size - 1);
			if ((stripe_offset + io_size) <= sc->DD_stripe_size) {
				physLBA = (uint32_t)virtLBA >>
				    sc->DD_stripe_exponent;
				stripe_unit = physLBA / sc->DD_num_phys_disks;
				column = physLBA % sc->DD_num_phys_disks;
				pIO_req->DevHandle =
				    htole16(sc->DD_column_map[column].dev_handle);
				/* ???? Is this endian safe*/
				cm->cm_desc.SCSIIO.DevHandle =
				    pIO_req->DevHandle;

				physLBA = (stripe_unit <<
				    sc->DD_stripe_exponent) + stripe_offset;
				ptrLBA = &pIO_req->CDB.CDB32[1];
				physLBA_byte = (uint8_t)(physLBA >> 16);
				*ptrLBA = physLBA_byte;
				ptrLBA = &pIO_req->CDB.CDB32[2];
				physLBA_byte = (uint8_t)(physLBA >> 8);
				*ptrLBA = physLBA_byte;
				ptrLBA = &pIO_req->CDB.CDB32[3];
				physLBA_byte = (uint8_t)physLBA;
				*ptrLBA = physLBA_byte;

				/*
				 * Set flag that Direct Drive I/O is
				 * being done.
				 */
				cm->cm_flags |= MPS_CM_FLAGS_DD_IO;
			}
		}
		return;
	}

	/*
	 * Handle 10, 12 or 16 byte CDBs.
	 */
	if ((pIO_req->DevHandle == sc->DD_dev_handle) && ((CDB[0] == READ_10) ||
	    (CDB[0] == WRITE_10) || (CDB[0] == READ_16) ||
	    (CDB[0] == WRITE_16) || (CDB[0] == READ_12) ||
	    (CDB[0] == WRITE_12))) {
		/*
		 * For 16-byte CDB's, verify that the upper 4 bytes of the CDB
		 * are 0.  If not, this is accessing beyond 2TB so handle it in
		 * the else section.  10-byte and 12-byte CDB's are OK.
		 * FreeBSD sends very rare 12 byte READ/WRITE, but driver is 
		 * ready to accept 12byte CDB for Direct IOs.
		 */
		if ((CDB[0] == READ_10 || CDB[0] == WRITE_10) ||
		    (CDB[0] == READ_12 || CDB[0] == WRITE_12) ||
		    !(CDB[2] | CDB[3] | CDB[4] | CDB[5])) {
			/*
			 * Get the transfer size in blocks.
			 */
			io_size = (cm->cm_length >> sc->DD_block_exponent);

			/*
			 * Get virtual LBA.  Point to correct lower 4 bytes of
			 * LBA in the CDB depending on command.
			 */
			lba_idx = ((CDB[0] == READ_12) || 
				(CDB[0] == WRITE_12) ||
				(CDB[0] == READ_10) ||
				(CDB[0] == WRITE_10))? 2 : 6;
			virtLBA = ((uint64_t)CDB[lba_idx] << 24) |
			    ((uint64_t)CDB[lba_idx + 1] << 16) |
			    ((uint64_t)CDB[lba_idx + 2] << 8) |
			    (uint64_t)CDB[lba_idx + 3];

			/*
			 * Check that LBA range for I/O does not exceed volume's
			 * MaxLBA.
			 */
			if ((virtLBA + (uint64_t)io_size - 1) <=
			    sc->DD_max_lba) {
				/*
				 * Check if the I/O crosses a stripe boundary.
				 * If not, translate the virtual LBA to a
				 * physical LBA and set the DevHandle for the
				 * PhysDisk to be used.  If it does cross a
				 * boundary, do normal I/O.  To get the right
				 * DevHandle to use, get the map number for the
				 * column, then use that map number to look up
				 * the DevHandle of the PhysDisk.
				 */
				stripe_offset = (uint32_t)virtLBA &
				    (sc->DD_stripe_size - 1);
				if ((stripe_offset + io_size) <=
				    sc->DD_stripe_size) {
					physLBA = (uint32_t)virtLBA >>
					    sc->DD_stripe_exponent;
					stripe_unit = physLBA /
					    sc->DD_num_phys_disks;
					column = physLBA %
					    sc->DD_num_phys_disks;
					pIO_req->DevHandle =
					    htole16(sc->DD_column_map[column].
					    dev_handle);
					cm->cm_desc.SCSIIO.DevHandle =
					    pIO_req->DevHandle;

					physLBA = (stripe_unit <<
					    sc->DD_stripe_exponent) +
					    stripe_offset;
					ptrLBA =
					    &pIO_req->CDB.CDB32[lba_idx];
					physLBA_byte = (uint8_t)(physLBA >> 24);
					*ptrLBA = physLBA_byte;
					ptrLBA =
					    &pIO_req->CDB.CDB32[lba_idx + 1];
					physLBA_byte = (uint8_t)(physLBA >> 16);
					*ptrLBA = physLBA_byte;
					ptrLBA =
					    &pIO_req->CDB.CDB32[lba_idx + 2];
					physLBA_byte = (uint8_t)(physLBA >> 8);
					*ptrLBA = physLBA_byte;
					ptrLBA =
					    &pIO_req->CDB.CDB32[lba_idx + 3];
					physLBA_byte = (uint8_t)physLBA;
					*ptrLBA = physLBA_byte;

					/*
					 * Set flag that Direct Drive I/O is
					 * being done.
					 */
					cm->cm_flags |= MPS_CM_FLAGS_DD_IO;
				}
			}
		} else {
			/*
			 * 16-byte CDB and the upper 4 bytes of the CDB are not
			 * 0.  Get the transfer size in blocks.
			 */
			io_size = (cm->cm_length >> sc->DD_block_exponent);

			/*
			 * Get virtual LBA.
			 */
			virtLBA = ((uint64_t)CDB[2] << 54) |
			    ((uint64_t)CDB[3] << 48) |
			    ((uint64_t)CDB[4] << 40) |
			    ((uint64_t)CDB[5] << 32) |
			    ((uint64_t)CDB[6] << 24) |
			    ((uint64_t)CDB[7] << 16) |
			    ((uint64_t)CDB[8] << 8) |
			    (uint64_t)CDB[9]; 

			/*
			 * Check that LBA range for I/O does not exceed volume's
			 * MaxLBA.
			 */
			if ((virtLBA + (uint64_t)io_size - 1) <=
			    sc->DD_max_lba) {
				/*
				 * Check if the I/O crosses a stripe boundary.
				 * If not, translate the virtual LBA to a
				 * physical LBA and set the DevHandle for the
				 * PhysDisk to be used.  If it does cross a
				 * boundary, do normal I/O.  To get the right
				 * DevHandle to use, get the map number for the
				 * column, then use that map number to look up
				 * the DevHandle of the PhysDisk.
				 */
				stripe_offset = (uint32_t)virtLBA &
				    (sc->DD_stripe_size - 1);
				if ((stripe_offset + io_size) <=
				    sc->DD_stripe_size) {
					physLBA = (uint32_t)(virtLBA >>
					    sc->DD_stripe_exponent);
					stripe_unit = physLBA /
					    sc->DD_num_phys_disks;
					column = physLBA %
					    sc->DD_num_phys_disks;
					pIO_req->DevHandle =
					    htole16(sc->DD_column_map[column].
					    dev_handle);
					cm->cm_desc.SCSIIO.DevHandle =
					    pIO_req->DevHandle;

					physLBA = (stripe_unit <<
					    sc->DD_stripe_exponent) +
					    stripe_offset;

					/*
					 * Set upper 4 bytes of LBA to 0.  We
					 * assume that the phys disks are less
					 * than 2 TB's in size.  Then, set the
					 * lower 4 bytes.
					 */
					pIO_req->CDB.CDB32[2] = 0;
					pIO_req->CDB.CDB32[3] = 0;
					pIO_req->CDB.CDB32[4] = 0;
					pIO_req->CDB.CDB32[5] = 0;
					ptrLBA = &pIO_req->CDB.CDB32[6];
					physLBA_byte = (uint8_t)(physLBA >> 24);
					*ptrLBA = physLBA_byte;
					ptrLBA = &pIO_req->CDB.CDB32[7];
					physLBA_byte = (uint8_t)(physLBA >> 16);
					*ptrLBA = physLBA_byte;
					ptrLBA = &pIO_req->CDB.CDB32[8];
					physLBA_byte = (uint8_t)(physLBA >> 8);
					*ptrLBA = physLBA_byte;
					ptrLBA = &pIO_req->CDB.CDB32[9];
					physLBA_byte = (uint8_t)physLBA;
					*ptrLBA = physLBA_byte;

					/*
					 * Set flag that Direct Drive I/O is
					 * being done.
					 */
					cm->cm_flags |= MPS_CM_FLAGS_DD_IO;
				}
			}
		}
	}
}

#if __FreeBSD_version >= 900026
static void
mpssas_smpio_complete(struct mps_softc *sc, struct mps_command *cm)
{
	MPI2_SMP_PASSTHROUGH_REPLY *rpl;
	MPI2_SMP_PASSTHROUGH_REQUEST *req;
	uint64_t sasaddr;
	union ccb *ccb;

	ccb = cm->cm_complete_data;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and SMP
	 * commands require two S/G elements only.  That should be handled
	 * in the standard request size.
	 */
	if ((cm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		mps_dprint(sc, MPS_ERROR,"%s: cm_flags = %#x on SMP request!\n",
			   __func__, cm->cm_flags);
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);
		goto bailout;
        }

	rpl = (MPI2_SMP_PASSTHROUGH_REPLY *)cm->cm_reply;
	if (rpl == NULL) {
		mps_dprint(sc, MPS_ERROR, "%s: NULL cm_reply!\n", __func__);
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);
		goto bailout;
	}

	req = (MPI2_SMP_PASSTHROUGH_REQUEST *)cm->cm_req;
	sasaddr = le32toh(req->SASAddress.Low);
	sasaddr |= ((uint64_t)(le32toh(req->SASAddress.High))) << 32;

	if ((le16toh(rpl->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS ||
	    rpl->SASStatus != MPI2_SASSTATUS_SUCCESS) {
		mps_dprint(sc, MPS_XINFO, "%s: IOCStatus %04x SASStatus %02x\n",
		    __func__, le16toh(rpl->IOCStatus), rpl->SASStatus);
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);
		goto bailout;
	}

	mps_dprint(sc, MPS_XINFO, "%s: SMP request to SAS address "
		   "%#jx completed successfully\n", __func__,
		   (uintmax_t)sasaddr);

	if (ccb->smpio.smp_response[2] == SMP_FR_ACCEPTED)
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
	else
		mpssas_set_ccbstatus(ccb, CAM_SMP_STATUS_ERROR);

bailout:
	/*
	 * We sync in both directions because we had DMAs in the S/G list
	 * in both directions.
	 */
	bus_dmamap_sync(sc->buffer_dmat, cm->cm_dmamap,
			BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->buffer_dmat, cm->cm_dmamap);
	mps_free_command(sc, cm);
	xpt_done(ccb);
}

static void
mpssas_send_smpcmd(struct mpssas_softc *sassc, union ccb *ccb, uint64_t sasaddr)
{
	struct mps_command *cm;
	uint8_t *request, *response;
	MPI2_SMP_PASSTHROUGH_REQUEST *req;
	struct mps_softc *sc;
	int error;

	sc = sassc->sc;
	error = 0;

	/*
	 * XXX We don't yet support physical addresses here.
	 */
	switch ((ccb->ccb_h.flags & CAM_DATA_MASK)) {
	case CAM_DATA_PADDR:
	case CAM_DATA_SG_PADDR:
		mps_dprint(sc, MPS_ERROR,
			   "%s: physical addresses not supported\n", __func__);
		mpssas_set_ccbstatus(ccb, CAM_REQ_INVALID);
		xpt_done(ccb);
		return;
	case CAM_DATA_SG:
		/*
		 * The chip does not support more than one buffer for the
		 * request or response.
		 */
	 	if ((ccb->smpio.smp_request_sglist_cnt > 1)
		  || (ccb->smpio.smp_response_sglist_cnt > 1)) {
			mps_dprint(sc, MPS_ERROR,
				   "%s: multiple request or response "
				   "buffer segments not supported for SMP\n",
				   __func__);
			mpssas_set_ccbstatus(ccb, CAM_REQ_INVALID);
			xpt_done(ccb);
			return;
		}

		/*
		 * The CAM_SCATTER_VALID flag was originally implemented
		 * for the XPT_SCSI_IO CCB, which only has one data pointer.
		 * We have two.  So, just take that flag to mean that we
		 * might have S/G lists, and look at the S/G segment count
		 * to figure out whether that is the case for each individual
		 * buffer.
		 */
		if (ccb->smpio.smp_request_sglist_cnt != 0) {
			bus_dma_segment_t *req_sg;

			req_sg = (bus_dma_segment_t *)ccb->smpio.smp_request;
			request = (uint8_t *)(uintptr_t)req_sg[0].ds_addr;
		} else
			request = ccb->smpio.smp_request;

		if (ccb->smpio.smp_response_sglist_cnt != 0) {
			bus_dma_segment_t *rsp_sg;

			rsp_sg = (bus_dma_segment_t *)ccb->smpio.smp_response;
			response = (uint8_t *)(uintptr_t)rsp_sg[0].ds_addr;
		} else
			response = ccb->smpio.smp_response;
		break;
	case CAM_DATA_VADDR:
		request = ccb->smpio.smp_request;
		response = ccb->smpio.smp_response;
		break;
	default:
		mpssas_set_ccbstatus(ccb, CAM_REQ_INVALID);
		xpt_done(ccb);
		return;
	}

	cm = mps_alloc_command(sc);
	if (cm == NULL) {
		mps_dprint(sc, MPS_ERROR,
		    "%s: cannot allocate command\n", __func__);
		mpssas_set_ccbstatus(ccb, CAM_RESRC_UNAVAIL);
		xpt_done(ccb);
		return;
	}

	req = (MPI2_SMP_PASSTHROUGH_REQUEST *)cm->cm_req;
	bzero(req, sizeof(*req));
	req->Function = MPI2_FUNCTION_SMP_PASSTHROUGH;

	/* Allow the chip to use any route to this SAS address. */
	req->PhysicalPort = 0xff;

	req->RequestDataLength = htole16(ccb->smpio.smp_request_len);
	req->SGLFlags = 
	    MPI2_SGLFLAGS_SYSTEM_ADDRESS_SPACE | MPI2_SGLFLAGS_SGL_TYPE_MPI;

	mps_dprint(sc, MPS_XINFO, "%s: sending SMP request to SAS "
	    "address %#jx\n", __func__, (uintmax_t)sasaddr);

	mpi_init_sge(cm, req, &req->SGL);

	/*
	 * Set up a uio to pass into mps_map_command().  This allows us to
	 * do one map command, and one busdma call in there.
	 */
	cm->cm_uio.uio_iov = cm->cm_iovec;
	cm->cm_uio.uio_iovcnt = 2;
	cm->cm_uio.uio_segflg = UIO_SYSSPACE;

	/*
	 * The read/write flag isn't used by busdma, but set it just in
	 * case.  This isn't exactly accurate, either, since we're going in
	 * both directions.
	 */
	cm->cm_uio.uio_rw = UIO_WRITE;

	cm->cm_iovec[0].iov_base = request;
	cm->cm_iovec[0].iov_len = le16toh(req->RequestDataLength);
	cm->cm_iovec[1].iov_base = response;
	cm->cm_iovec[1].iov_len = ccb->smpio.smp_response_len;

	cm->cm_uio.uio_resid = cm->cm_iovec[0].iov_len +
			       cm->cm_iovec[1].iov_len;

	/*
	 * Trigger a warning message in mps_data_cb() for the user if we
	 * wind up exceeding two S/G segments.  The chip expects one
	 * segment for the request and another for the response.
	 */
	cm->cm_max_segs = 2;

	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_complete = mpssas_smpio_complete;
	cm->cm_complete_data = ccb;

	/*
	 * Tell the mapping code that we're using a uio, and that this is
	 * an SMP passthrough request.  There is a little special-case
	 * logic there (in mps_data_cb()) to handle the bidirectional
	 * transfer.  
	 */
	cm->cm_flags |= MPS_CM_FLAGS_USE_UIO | MPS_CM_FLAGS_SMP_PASS |
			MPS_CM_FLAGS_DATAIN | MPS_CM_FLAGS_DATAOUT;

	/* The chip data format is little endian. */
	req->SASAddress.High = htole32(sasaddr >> 32);
	req->SASAddress.Low = htole32(sasaddr);

	/*
	 * XXX Note that we don't have a timeout/abort mechanism here.
	 * From the manual, it looks like task management requests only
	 * work for SCSI IO and SATA passthrough requests.  We may need to
	 * have a mechanism to retry requests in the event of a chip reset
	 * at least.  Hopefully the chip will insure that any errors short
	 * of that are relayed back to the driver.
	 */
	error = mps_map_command(sc, cm);
	if ((error != 0) && (error != EINPROGRESS)) {
		mps_dprint(sc, MPS_ERROR,
			   "%s: error %d returned from mps_map_command()\n",
			   __func__, error);
		goto bailout_error;
	}

	return;

bailout_error:
	mps_free_command(sc, cm);
	mpssas_set_ccbstatus(ccb, CAM_RESRC_UNAVAIL);
	xpt_done(ccb);
	return;

}

static void
mpssas_action_smpio(struct mpssas_softc *sassc, union ccb *ccb)
{
	struct mps_softc *sc;
	struct mpssas_target *targ;
	uint64_t sasaddr = 0;

	sc = sassc->sc;

	/*
	 * Make sure the target exists.
	 */
	KASSERT(ccb->ccb_h.target_id < sassc->maxtargets,
	    ("Target %d out of bounds in XPT_SMP_IO\n", ccb->ccb_h.target_id));
	targ = &sassc->targets[ccb->ccb_h.target_id];
	if (targ->handle == 0x0) {
		mps_dprint(sc, MPS_ERROR,
			   "%s: target %d does not exist!\n", __func__,
			   ccb->ccb_h.target_id);
		mpssas_set_ccbstatus(ccb, CAM_SEL_TIMEOUT);
		xpt_done(ccb);
		return;
	}

	/*
	 * If this device has an embedded SMP target, we'll talk to it
	 * directly.
	 * figure out what the expander's address is.
	 */
	if ((targ->devinfo & MPI2_SAS_DEVICE_INFO_SMP_TARGET) != 0)
		sasaddr = targ->sasaddr;

	/*
	 * If we don't have a SAS address for the expander yet, try
	 * grabbing it from the page 0x83 information cached in the
	 * transport layer for this target.  LSI expanders report the
	 * expander SAS address as the port-associated SAS address in
	 * Inquiry VPD page 0x83.  Maxim expanders don't report it in page
	 * 0x83.
	 *
	 * XXX KDM disable this for now, but leave it commented out so that
	 * it is obvious that this is another possible way to get the SAS
	 * address.
	 *
	 * The parent handle method below is a little more reliable, and
	 * the other benefit is that it works for devices other than SES
	 * devices.  So you can send a SMP request to a da(4) device and it
	 * will get routed to the expander that device is attached to.
	 * (Assuming the da(4) device doesn't contain an SMP target...)
	 */
#if 0
	if (sasaddr == 0)
		sasaddr = xpt_path_sas_addr(ccb->ccb_h.path);
#endif

	/*
	 * If we still don't have a SAS address for the expander, look for
	 * the parent device of this device, which is probably the expander.
	 */
	if (sasaddr == 0) {
#ifdef OLD_MPS_PROBE
		struct mpssas_target *parent_target;
#endif

		if (targ->parent_handle == 0x0) {
			mps_dprint(sc, MPS_ERROR,
				   "%s: handle %d does not have a valid "
				   "parent handle!\n", __func__, targ->handle);
			mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
			goto bailout;
		}
#ifdef OLD_MPS_PROBE
		parent_target = mpssas_find_target_by_handle(sassc, 0,
			targ->parent_handle);

		if (parent_target == NULL) {
			mps_dprint(sc, MPS_ERROR,
				   "%s: handle %d does not have a valid "
				   "parent target!\n", __func__, targ->handle);
			mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
			goto bailout;
		}

		if ((parent_target->devinfo &
		     MPI2_SAS_DEVICE_INFO_SMP_TARGET) == 0) {
			mps_dprint(sc, MPS_ERROR,
				   "%s: handle %d parent %d does not "
				   "have an SMP target!\n", __func__,
				   targ->handle, parent_target->handle);
			mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
			goto bailout;

		}

		sasaddr = parent_target->sasaddr;
#else /* OLD_MPS_PROBE */
		if ((targ->parent_devinfo &
		     MPI2_SAS_DEVICE_INFO_SMP_TARGET) == 0) {
			mps_dprint(sc, MPS_ERROR,
				   "%s: handle %d parent %d does not "
				   "have an SMP target!\n", __func__,
				   targ->handle, targ->parent_handle);
			mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
			goto bailout;

		}
		if (targ->parent_sasaddr == 0x0) {
			mps_dprint(sc, MPS_ERROR,
				   "%s: handle %d parent handle %d does "
				   "not have a valid SAS address!\n",
				   __func__, targ->handle, targ->parent_handle);
			mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
			goto bailout;
		}

		sasaddr = targ->parent_sasaddr;
#endif /* OLD_MPS_PROBE */

	}

	if (sasaddr == 0) {
		mps_dprint(sc, MPS_INFO,
			   "%s: unable to find SAS address for handle %d\n",
			   __func__, targ->handle);
		mpssas_set_ccbstatus(ccb, CAM_DEV_NOT_THERE);
		goto bailout;
	}
	mpssas_send_smpcmd(sassc, ccb, sasaddr);

	return;

bailout:
	xpt_done(ccb);

}
#endif //__FreeBSD_version >= 900026

static void
mpssas_action_resetdev(struct mpssas_softc *sassc, union ccb *ccb)
{
	MPI2_SCSI_TASK_MANAGE_REQUEST *req;
	struct mps_softc *sc;
	struct mps_command *tm;
	struct mpssas_target *targ;

	MPS_FUNCTRACE(sassc->sc);
	mtx_assert(&sassc->sc->mps_mtx, MA_OWNED);

	KASSERT(ccb->ccb_h.target_id < sassc->maxtargets,
	    ("Target %d out of bounds in XPT_RESET_DEV\n",
	     ccb->ccb_h.target_id));
	sc = sassc->sc;
	tm = mpssas_alloc_tm(sc);
	if (tm == NULL) {
		mps_dprint(sc, MPS_ERROR,
		    "command alloc failure in mpssas_action_resetdev\n");
		mpssas_set_ccbstatus(ccb, CAM_RESRC_UNAVAIL);
		xpt_done(ccb);
		return;
	}

	targ = &sassc->targets[ccb->ccb_h.target_id];
	req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;
	req->DevHandle = htole16(targ->handle);
	req->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;

	/* SAS Hard Link Reset / SATA Link Reset */
	req->MsgFlags = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	tm->cm_data = NULL;
	tm->cm_complete = mpssas_resetdev_complete;
	tm->cm_complete_data = ccb;
	tm->cm_targ = targ;

	mpssas_prepare_for_tm(sc, tm, targ, CAM_LUN_WILDCARD);
	mps_map_command(sc, tm);
}

static void
mpssas_resetdev_complete(struct mps_softc *sc, struct mps_command *tm)
{
	MPI2_SCSI_TASK_MANAGE_REPLY *resp;
	union ccb *ccb;

	MPS_FUNCTRACE(sc);
	mtx_assert(&sc->mps_mtx, MA_OWNED);

	resp = (MPI2_SCSI_TASK_MANAGE_REPLY *)tm->cm_reply;
	ccb = tm->cm_complete_data;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and
	 * task management commands don't have S/G lists.
	 */
	if ((tm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		MPI2_SCSI_TASK_MANAGE_REQUEST *req;

		req = (MPI2_SCSI_TASK_MANAGE_REQUEST *)tm->cm_req;

		mps_dprint(sc, MPS_ERROR,
			   "%s: cm_flags = %#x for reset of handle %#04x! "
			   "This should not happen!\n", __func__, tm->cm_flags,
			   req->DevHandle);
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);
		goto bailout;
	}

	mps_dprint(sc, MPS_XINFO,
	    "%s: IOCStatus = 0x%x ResponseCode = 0x%x\n", __func__,
	    le16toh(resp->IOCStatus), le32toh(resp->ResponseCode));

	if (le32toh(resp->ResponseCode) == MPI2_SCSITASKMGMT_RSP_TM_COMPLETE) {
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP);
		mpssas_announce_reset(sc, AC_SENT_BDR, tm->cm_targ->tid,
		    CAM_LUN_WILDCARD);
	}
	else
		mpssas_set_ccbstatus(ccb, CAM_REQ_CMP_ERR);

bailout:

	mpssas_free_tm(sc, tm);
	xpt_done(ccb);
}

static void
mpssas_poll(struct cam_sim *sim)
{
	struct mpssas_softc *sassc;

	sassc = cam_sim_softc(sim);

	if (sassc->sc->mps_debug & MPS_TRACE) {
		/* frequent debug messages during a panic just slow
		 * everything down too much.
		 */
		mps_printf(sassc->sc, "%s clearing MPS_TRACE\n", __func__);
		sassc->sc->mps_debug &= ~MPS_TRACE;
	}

	mps_intr_locked(sassc->sc);
}

static void
mpssas_async(void *callback_arg, uint32_t code, struct cam_path *path,
	     void *arg)
{
	struct mps_softc *sc;

	sc = (struct mps_softc *)callback_arg;

	switch (code) {
#if (__FreeBSD_version >= 1000006) || \
    ((__FreeBSD_version >= 901503) && (__FreeBSD_version < 1000000))
	case AC_ADVINFO_CHANGED: {
		struct mpssas_target *target;
		struct mpssas_softc *sassc;
		struct scsi_read_capacity_data_long rcap_buf;
		struct ccb_dev_advinfo cdai;
		struct mpssas_lun *lun;
		lun_id_t lunid;
		int found_lun;
		uintptr_t buftype;

		buftype = (uintptr_t)arg;

		found_lun = 0;
		sassc = sc->sassc;

		/*
		 * We're only interested in read capacity data changes.
		 */
		if (buftype != CDAI_TYPE_RCAPLONG)
			break;

		/*
		 * We should have a handle for this, but check to make sure.
		 */
		KASSERT(xpt_path_target_id(path) < sassc->maxtargets,
		    ("Target %d out of bounds in mpssas_async\n",
		    xpt_path_target_id(path)));
		target = &sassc->targets[xpt_path_target_id(path)];
		if (target->handle == 0)
			break;

		lunid = xpt_path_lun_id(path);

		SLIST_FOREACH(lun, &target->luns, lun_link) {
			if (lun->lun_id == lunid) {
				found_lun = 1;
				break;
			}
		}

		if (found_lun == 0) {
			lun = malloc(sizeof(struct mpssas_lun), M_MPT2,
				     M_NOWAIT | M_ZERO);
			if (lun == NULL) {
				mps_dprint(sc, MPS_ERROR, "Unable to alloc "
					   "LUN for EEDP support.\n");
				break;
			}
			lun->lun_id = lunid;
			SLIST_INSERT_HEAD(&target->luns, lun, lun_link);
		}

		bzero(&rcap_buf, sizeof(rcap_buf));
		xpt_setup_ccb(&cdai.ccb_h, path, CAM_PRIORITY_NORMAL);
		cdai.ccb_h.func_code = XPT_DEV_ADVINFO;
		cdai.ccb_h.flags = CAM_DIR_IN;
		cdai.buftype = CDAI_TYPE_RCAPLONG;
#if (__FreeBSD_version >= 1100061) || \
    ((__FreeBSD_version >= 1001510) && (__FreeBSD_version < 1100000))
		cdai.flags = CDAI_FLAG_NONE;
#else
		cdai.flags = 0;
#endif
		cdai.bufsiz = sizeof(rcap_buf);
		cdai.buf = (uint8_t *)&rcap_buf;
		xpt_action((union ccb *)&cdai);
		if ((cdai.ccb_h.status & CAM_DEV_QFRZN) != 0)
			cam_release_devq(cdai.ccb_h.path,
					 0, 0, 0, FALSE);

		if ((mpssas_get_ccbstatus((union ccb *)&cdai) == CAM_REQ_CMP)
		 && (rcap_buf.prot & SRC16_PROT_EN)) {
			switch (rcap_buf.prot & SRC16_P_TYPE) {
			case SRC16_PTYPE_1:
			case SRC16_PTYPE_3:
				lun->eedp_formatted = TRUE;
				lun->eedp_block_size =
				    scsi_4btoul(rcap_buf.length);
				break;
			case SRC16_PTYPE_2:
			default:
				lun->eedp_formatted = FALSE;
				lun->eedp_block_size = 0;
				break;
			}
		} else {
			lun->eedp_formatted = FALSE;
			lun->eedp_block_size = 0;
		}
		break;
	}
#else
	case AC_FOUND_DEVICE: {
		struct ccb_getdev *cgd;

		cgd = arg;
		mpssas_check_eedp(sc, path, cgd);
		break;
	}
#endif
	default:
		break;
	}
}

#if (__FreeBSD_version < 901503) || \
    ((__FreeBSD_version >= 1000000) && (__FreeBSD_version < 1000006))
static void
mpssas_check_eedp(struct mps_softc *sc, struct cam_path *path,
		  struct ccb_getdev *cgd)
{
	struct mpssas_softc *sassc = sc->sassc;
	struct ccb_scsiio *csio;
	struct scsi_read_capacity_16 *scsi_cmd;
	struct scsi_read_capacity_eedp *rcap_buf;
	path_id_t pathid;
	target_id_t targetid;
	lun_id_t lunid;
	union ccb *ccb;
	struct cam_path *local_path;
	struct mpssas_target *target;
	struct mpssas_lun *lun;
	uint8_t	found_lun;
	char path_str[64];

	sassc = sc->sassc;
	pathid = cam_sim_path(sassc->sim);
	targetid = xpt_path_target_id(path);
	lunid = xpt_path_lun_id(path);

	KASSERT(targetid < sassc->maxtargets,
	    ("Target %d out of bounds in mpssas_check_eedp\n",
	     targetid));
	target = &sassc->targets[targetid];
	if (target->handle == 0x0)
		return;

	/*
	 * Determine if the device is EEDP capable.
	 *
	 * If this flag is set in the inquiry data, 
	 * the device supports protection information,
	 * and must support the 16 byte read
	 * capacity command, otherwise continue without
	 * sending read cap 16
	 */
	if ((cgd->inq_data.spc3_flags & SPC3_SID_PROTECT) == 0)
		return;

	/*
	 * Issue a READ CAPACITY 16 command.  This info
	 * is used to determine if the LUN is formatted
	 * for EEDP support.
	 */
	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		mps_dprint(sc, MPS_ERROR, "Unable to alloc CCB "
		    "for EEDP support.\n");
		return;
	}

	if (xpt_create_path(&local_path, xpt_periph,
	    pathid, targetid, lunid) != CAM_REQ_CMP) {
		mps_dprint(sc, MPS_ERROR, "Unable to create "
		    "path for EEDP support\n");
		xpt_free_ccb(ccb);
		return;
	}

	/*
	 * If LUN is already in list, don't create a new
	 * one.
	 */
	found_lun = FALSE;
	SLIST_FOREACH(lun, &target->luns, lun_link) {
		if (lun->lun_id == lunid) {
			found_lun = TRUE;
			break;
		}
	}
	if (!found_lun) {
		lun = malloc(sizeof(struct mpssas_lun), M_MPT2,
		    M_NOWAIT | M_ZERO);
		if (lun == NULL) {
			mps_dprint(sc, MPS_ERROR,
			    "Unable to alloc LUN for EEDP support.\n");
			xpt_free_path(local_path);
			xpt_free_ccb(ccb);
			return;
		}
		lun->lun_id = lunid;
		SLIST_INSERT_HEAD(&target->luns, lun,
		    lun_link);
	}

	xpt_path_string(local_path, path_str, sizeof(path_str));

	mps_dprint(sc, MPS_INFO, "Sending read cap: path %s handle %d\n",
	    path_str, target->handle);

	/*
	 * Issue a READ CAPACITY 16 command for the LUN.
	 * The mpssas_read_cap_done function will load
	 * the read cap info into the LUN struct.
	 */
	rcap_buf = malloc(sizeof(struct scsi_read_capacity_eedp),
	    M_MPT2, M_NOWAIT | M_ZERO);
	if (rcap_buf == NULL) {
		mps_dprint(sc, MPS_FAULT,
		    "Unable to alloc read capacity buffer for EEDP support.\n");
		xpt_free_path(ccb->ccb_h.path);
		xpt_free_ccb(ccb);
		return;
	}
	xpt_setup_ccb(&ccb->ccb_h, local_path, CAM_PRIORITY_XPT);
	csio = &ccb->csio;
	csio->ccb_h.func_code = XPT_SCSI_IO;
	csio->ccb_h.flags = CAM_DIR_IN;
	csio->ccb_h.retry_count = 4;	
	csio->ccb_h.cbfcnp = mpssas_read_cap_done;
	csio->ccb_h.timeout = 60000;
	csio->data_ptr = (uint8_t *)rcap_buf;
	csio->dxfer_len = sizeof(struct scsi_read_capacity_eedp);
	csio->sense_len = MPS_SENSE_LEN;
	csio->cdb_len = sizeof(*scsi_cmd);
	csio->tag_action = MSG_SIMPLE_Q_TAG;

	scsi_cmd = (struct scsi_read_capacity_16 *)&csio->cdb_io.cdb_bytes;
	bzero(scsi_cmd, sizeof(*scsi_cmd));
	scsi_cmd->opcode = 0x9E;
	scsi_cmd->service_action = SRC16_SERVICE_ACTION;
	((uint8_t *)scsi_cmd)[13] = sizeof(struct scsi_read_capacity_eedp);

	ccb->ccb_h.ppriv_ptr1 = sassc;
	xpt_action(ccb);
}

static void
mpssas_read_cap_done(struct cam_periph *periph, union ccb *done_ccb)
{
	struct mpssas_softc *sassc;
	struct mpssas_target *target;
	struct mpssas_lun *lun;
	struct scsi_read_capacity_eedp *rcap_buf;

	if (done_ccb == NULL)
		return;
	
	/* Driver need to release devq, it Scsi command is
	 * generated by driver internally.
	 * Currently there is a single place where driver
	 * calls scsi command internally. In future if driver
	 * calls more scsi command internally, it needs to release
	 * devq internally, since those command will not go back to
	 * cam_periph.
	 */
	if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) ) {
        	done_ccb->ccb_h.status &= ~CAM_DEV_QFRZN;
		xpt_release_devq(done_ccb->ccb_h.path,
			       	/*count*/ 1, /*run_queue*/TRUE);
	}

	rcap_buf = (struct scsi_read_capacity_eedp *)done_ccb->csio.data_ptr;

	/*
	 * Get the LUN ID for the path and look it up in the LUN list for the
	 * target.
	 */
	sassc = (struct mpssas_softc *)done_ccb->ccb_h.ppriv_ptr1;
	KASSERT(done_ccb->ccb_h.target_id < sassc->maxtargets,
	    ("Target %d out of bounds in mpssas_read_cap_done\n",
	     done_ccb->ccb_h.target_id));
	target = &sassc->targets[done_ccb->ccb_h.target_id];
	SLIST_FOREACH(lun, &target->luns, lun_link) {
		if (lun->lun_id != done_ccb->ccb_h.target_lun)
			continue;

		/*
		 * Got the LUN in the target's LUN list.  Fill it in
		 * with EEDP info.  If the READ CAP 16 command had some
		 * SCSI error (common if command is not supported), mark
		 * the lun as not supporting EEDP and set the block size
		 * to 0.
		 */
		if ((mpssas_get_ccbstatus(done_ccb) != CAM_REQ_CMP)
		 || (done_ccb->csio.scsi_status != SCSI_STATUS_OK)) {
			lun->eedp_formatted = FALSE;
			lun->eedp_block_size = 0;
			break;
		}

		if (rcap_buf->protect & 0x01) {
			mps_dprint(sassc->sc, MPS_INFO, "LUN %d for "
 			    "target ID %d is formatted for EEDP "
 			    "support.\n", done_ccb->ccb_h.target_lun,
 			    done_ccb->ccb_h.target_id);
			lun->eedp_formatted = TRUE;
			lun->eedp_block_size = scsi_4btoul(rcap_buf->length);
		}
		break;
	}

	// Finished with this CCB and path.
	free(rcap_buf, M_MPT2);
	xpt_free_path(done_ccb->ccb_h.path);
	xpt_free_ccb(done_ccb);
}
#endif /* (__FreeBSD_version < 901503) || \
          ((__FreeBSD_version >= 1000000) && (__FreeBSD_version < 1000006)) */

/*
 * Set the INRESET flag for this target so that no I/O will be sent to
 * the target until the reset has completed.  If an I/O request does
 * happen, the devq will be frozen.  The CCB holds the path which is
 * used to release the devq.  The devq is released and the CCB is freed
 * when the TM completes.
 */
void
mpssas_prepare_for_tm(struct mps_softc *sc, struct mps_command *tm,
    struct mpssas_target *target, lun_id_t lun_id)
{
	union ccb *ccb;
	path_id_t path_id;

	ccb = xpt_alloc_ccb_nowait();
	if (ccb) {
		path_id = cam_sim_path(sc->sassc->sim);
		if (xpt_create_path(&ccb->ccb_h.path, xpt_periph, path_id,
		    target->tid, lun_id) != CAM_REQ_CMP) {
			xpt_free_ccb(ccb);
		} else {
			tm->cm_ccb = ccb;
			tm->cm_targ = target;
			target->flags |= MPSSAS_TARGET_INRESET;
		}
	}
}

int
mpssas_startup(struct mps_softc *sc)
{

	/*
	 * Send the port enable message and set the wait_for_port_enable flag.
	 * This flag helps to keep the simq frozen until all discovery events
	 * are processed.
	 */
	sc->wait_for_port_enable = 1;
	mpssas_send_portenable(sc);
	return (0);
}

static int
mpssas_send_portenable(struct mps_softc *sc)
{
	MPI2_PORT_ENABLE_REQUEST *request;
	struct mps_command *cm;

	MPS_FUNCTRACE(sc);

	if ((cm = mps_alloc_command(sc)) == NULL)
		return (EBUSY);
	request = (MPI2_PORT_ENABLE_REQUEST *)cm->cm_req;
	request->Function = MPI2_FUNCTION_PORT_ENABLE;
	request->MsgFlags = 0;
	request->VP_ID = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_complete = mpssas_portenable_complete;
	cm->cm_data = NULL;
	cm->cm_sge = NULL;

	mps_map_command(sc, cm);
	mps_dprint(sc, MPS_XINFO, 
	    "mps_send_portenable finished cm %p req %p complete %p\n",
	    cm, cm->cm_req, cm->cm_complete);
	return (0);
}

static void
mpssas_portenable_complete(struct mps_softc *sc, struct mps_command *cm)
{
	MPI2_PORT_ENABLE_REPLY *reply;
	struct mpssas_softc *sassc;

	MPS_FUNCTRACE(sc);
	sassc = sc->sassc;

	/*
	 * Currently there should be no way we can hit this case.  It only
	 * happens when we have a failure to allocate chain frames, and
	 * port enable commands don't have S/G lists.
	 */
	if ((cm->cm_flags & MPS_CM_FLAGS_ERROR_MASK) != 0) {
		mps_dprint(sc, MPS_ERROR, "%s: cm_flags = %#x for port enable! "
			   "This should not happen!\n", __func__, cm->cm_flags);
	}

	reply = (MPI2_PORT_ENABLE_REPLY *)cm->cm_reply;
	if (reply == NULL)
		mps_dprint(sc, MPS_FAULT, "Portenable NULL reply\n");
	else if (le16toh(reply->IOCStatus & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS)
		mps_dprint(sc, MPS_FAULT, "Portenable failed\n");

	mps_free_command(sc, cm);

	/*
	 * Get WarpDrive info after discovery is complete but before the scan
	 * starts.  At this point, all devices are ready to be exposed to the
	 * OS.  If devices should be hidden instead, take them out of the
	 * 'targets' array before the scan.  The devinfo for a disk will have
	 * some info and a volume's will be 0.  Use that to remove disks.
	 */
	mps_wd_config_pages(sc);

	/*
	 * Done waiting for port enable to complete.  Decrement the refcount.
	 * If refcount is 0, discovery is complete and a rescan of the bus can
	 * take place.  Since the simq was explicitly frozen before port
	 * enable, it must be explicitly released here to keep the
	 * freeze/release count in sync.
	 */
	sc->wait_for_port_enable = 0;
	sc->port_enable_complete = 1;
	wakeup(&sc->port_enable_complete);
	mpssas_startup_decrement(sassc);
}

int
mpssas_check_id(struct mpssas_softc *sassc, int id)
{
	struct mps_softc *sc = sassc->sc;
	char *ids;
	char *name;

	ids = &sc->exclude_ids[0];
	while((name = strsep(&ids, ",")) != NULL) {
		if (name[0] == '\0')
			continue;
		if (strtol(name, NULL, 0) == (long)id)
			return (1);
	}

	return (0);
}

void
mpssas_realloc_targets(struct mps_softc *sc, int maxtargets)
{
	struct mpssas_softc *sassc;
	struct mpssas_lun *lun, *lun_tmp;
	struct mpssas_target *targ;
	int i;

	sassc = sc->sassc;
	/*
	 * The number of targets is based on IOC Facts, so free all of
	 * the allocated LUNs for each target and then the target buffer
	 * itself.
	 */
	for (i=0; i< maxtargets; i++) {
		targ = &sassc->targets[i];
		SLIST_FOREACH_SAFE(lun, &targ->luns, lun_link, lun_tmp) {
			free(lun, M_MPT2);
		}
	}
	free(sassc->targets, M_MPT2);

	sassc->targets = malloc(sizeof(struct mpssas_target) * maxtargets,
	    M_MPT2, M_WAITOK|M_ZERO);
	if (!sassc->targets) {
		panic("%s failed to alloc targets with error %d\n",
		    __func__, ENOMEM);
	}
}
