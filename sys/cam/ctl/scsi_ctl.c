/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, 2009 Silicon Graphics International Corp.
 * Copyright (c) 2014-2015 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/scsi_ctl.c#4 $
 */
/*
 * Peripheral driver interface between CAM and CTL (CAM Target Layer).
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <machine/bus.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_queue.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_error.h>

struct ctlfe_softc {
	struct ctl_port	port;
	path_id_t	path_id;
	target_id_t	target_id;
	uint32_t	hba_misc;
	u_int		maxio;
	struct cam_sim *sim;
	char		port_name[DEV_IDLEN];
	struct mtx	lun_softc_mtx;
	STAILQ_HEAD(, ctlfe_lun_softc) lun_softc_list;
	STAILQ_ENTRY(ctlfe_softc) links;
};

STAILQ_HEAD(, ctlfe_softc) ctlfe_softc_list;
struct mtx ctlfe_list_mtx;
static char ctlfe_mtx_desc[] = "ctlfelist";

typedef enum {
	CTLFE_LUN_NONE		= 0x00,
	CTLFE_LUN_WILDCARD	= 0x01
} ctlfe_lun_flags;

struct ctlfe_lun_softc {
	struct ctlfe_softc *parent_softc;
	struct cam_periph *periph;
	ctlfe_lun_flags flags;
	int	 ctios_sent;		/* Number of active CTIOs */
	int	 refcount;		/* Number of active xpt_action() */
	int	 atios_alloced;		/* Number of ATIOs not freed */
	int	 inots_alloced;		/* Number of INOTs not freed */
	struct task	refdrain_task;
	STAILQ_HEAD(, ccb_hdr) work_queue;
	LIST_HEAD(, ccb_hdr) atio_list;	/* List of ATIOs queued to SIM. */
	LIST_HEAD(, ccb_hdr) inot_list;	/* List of INOTs queued to SIM. */
	STAILQ_ENTRY(ctlfe_lun_softc) links;
};

typedef enum {
	CTLFE_CMD_NONE		= 0x00,
	CTLFE_CMD_PIECEWISE	= 0x01
} ctlfe_cmd_flags;

struct ctlfe_cmd_info {
	int cur_transfer_index;
	size_t cur_transfer_off;
	ctlfe_cmd_flags flags;
	/*
	 * XXX KDM struct bus_dma_segment is 8 bytes on i386, and 16
	 * bytes on amd64.  So with 32 elements, this is 256 bytes on
	 * i386 and 512 bytes on amd64.
	 */
#define CTLFE_MAX_SEGS	32
	bus_dma_segment_t cam_sglist[CTLFE_MAX_SEGS];
};

/*
 * When we register the adapter/bus, request that this many ctl_ios be
 * allocated.  This should be the maximum supported by the adapter, but we
 * currently don't have a way to get that back from the path inquiry.
 * XXX KDM add that to the path inquiry.
 */
#define	CTLFE_REQ_CTL_IO	4096
/*
 * Number of Accept Target I/O CCBs to allocate and queue down to the
 * adapter per LUN.
 * XXX KDM should this be controlled by CTL?
 */
#define	CTLFE_ATIO_PER_LUN	1024
/*
 * Number of Immediate Notify CCBs (used for aborts, resets, etc.) to
 * allocate and queue down to the adapter per LUN.
 * XXX KDM should this be controlled by CTL?
 */
#define	CTLFE_IN_PER_LUN	1024

/*
 * Timeout (in seconds) on CTIO CCB doing DMA or sending status
 */
#define	CTLFE_TIMEOUT	5

/*
 * Turn this on to enable extra debugging prints.
 */
#if 0
#define	CTLFE_DEBUG
#endif

MALLOC_DEFINE(M_CTLFE, "CAM CTL FE", "CAM CTL FE interface");

#define	io_ptr		ppriv_ptr0

/* This is only used in the CTIO */
#define	ccb_atio	ppriv_ptr1

#define PRIV_CCB(io)	((io)->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptrs[0])
#define PRIV_INFO(io)	((io)->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptrs[1])

static int		ctlfeinitialize(void);
static int		ctlfeshutdown(void);
static periph_init_t	ctlfeperiphinit;
static periph_deinit_t	ctlfeperiphdeinit;
static void		ctlfeasync(void *callback_arg, uint32_t code,
				   struct cam_path *path, void *arg);
static periph_ctor_t	ctlferegister;
static periph_oninv_t	ctlfeoninvalidate;
static periph_dtor_t	ctlfecleanup;
static periph_start_t	ctlfestart;
static void		ctlfedone(struct cam_periph *periph,
				  union ccb *done_ccb);

static void 		ctlfe_onoffline(void *arg, int online);
static void 		ctlfe_online(void *arg);
static void 		ctlfe_offline(void *arg);
static int 		ctlfe_lun_enable(void *arg, int lun_id);
static int 		ctlfe_lun_disable(void *arg, int lun_id);
static void		ctlfe_dump_sim(struct cam_sim *sim);
static void		ctlfe_dump_queue(struct ctlfe_lun_softc *softc);
static void 		ctlfe_datamove(union ctl_io *io);
static void 		ctlfe_done(union ctl_io *io);
static void 		ctlfe_dump(void);
static void		ctlfe_free_ccb(struct cam_periph *periph,
			    union ccb *ccb);
static void		ctlfe_requeue_ccb(struct cam_periph *periph,
			    union ccb *ccb, int unlock);

static struct periph_driver ctlfe_driver =
{
	ctlfeperiphinit, "ctl",
	TAILQ_HEAD_INITIALIZER(ctlfe_driver.units), /*generation*/ 0,
	CAM_PERIPH_DRV_EARLY,
	ctlfeperiphdeinit
};

static struct ctl_frontend ctlfe_frontend =
{
	.name = "camtgt",
	.init = ctlfeinitialize,
	.fe_dump = ctlfe_dump,
	.shutdown = ctlfeshutdown,
};
CTL_FRONTEND_DECLARE(ctlfe, ctlfe_frontend);

static int
ctlfeinitialize(void)
{

	STAILQ_INIT(&ctlfe_softc_list);
	mtx_init(&ctlfe_list_mtx, ctlfe_mtx_desc, NULL, MTX_DEF);
	periphdriver_register(&ctlfe_driver);
	return (0);
}

static int
ctlfeshutdown(void)
{
	int error;

	error = periphdriver_unregister(&ctlfe_driver);
	if (error != 0)
		return (error);
	mtx_destroy(&ctlfe_list_mtx);
	return (0);
}

static void
ctlfeperiphinit(void)
{
	cam_status status;

	status = xpt_register_async(AC_PATH_REGISTERED | AC_PATH_DEREGISTERED |
				    AC_CONTRACT, ctlfeasync, NULL, NULL);
	if (status != CAM_REQ_CMP) {
		printf("ctl: Failed to attach async callback due to CAM "
		       "status 0x%x!\n", status);
	}
}

static int
ctlfeperiphdeinit(void)
{

	/* XXX: It would be good to tear down active ports here. */
	if (!TAILQ_EMPTY(&ctlfe_driver.units))
		return (EBUSY);
	xpt_register_async(0, ctlfeasync, NULL, NULL);
	return (0);
}

static void
ctlfeasync(void *callback_arg, uint32_t code, struct cam_path *path, void *arg)
{
	struct ctlfe_softc *softc;

#ifdef CTLFEDEBUG
	printf("%s: entered\n", __func__);
#endif

	mtx_lock(&ctlfe_list_mtx);
	STAILQ_FOREACH(softc, &ctlfe_softc_list, links) {
		if (softc->path_id == xpt_path_path_id(path))
			break;
	}
	mtx_unlock(&ctlfe_list_mtx);

	/*
	 * When a new path gets registered, and it is capable of target
	 * mode, go ahead and attach.  Later on, we may need to be more
	 * selective, but for now this will be sufficient.
 	 */
	switch (code) {
	case AC_PATH_REGISTERED: {
		struct ctl_port *port;
		struct ccb_pathinq *cpi;
		int retval;

		cpi = (struct ccb_pathinq *)arg;

		/* Don't attach if it doesn't support target mode */
		if ((cpi->target_sprt & PIT_PROCESSOR) == 0) {
#ifdef CTLFEDEBUG
			printf("%s: SIM %s%d doesn't support target mode\n",
			       __func__, cpi->dev_name, cpi->unit_number);
#endif
			break;
		}

		if (softc != NULL) {
#ifdef CTLFEDEBUG
			printf("%s: CTL port for CAM path %u already exists\n",
			       __func__, xpt_path_path_id(path));
#endif
			break;
		}

		/*
		 * We're in an interrupt context here, so we have to
		 * use M_NOWAIT.  Of course this means trouble if we
		 * can't allocate memory.
		 */
		softc = malloc(sizeof(*softc), M_CTLFE, M_NOWAIT | M_ZERO);
		if (softc == NULL) {
			printf("%s: unable to malloc %zd bytes for softc\n",
			       __func__, sizeof(*softc));
			return;
		}

		softc->path_id = cpi->ccb_h.path_id;
		softc->target_id = cpi->initiator_id;
		softc->sim = xpt_path_sim(path);
		softc->hba_misc = cpi->hba_misc;
		if (cpi->maxio != 0)
			softc->maxio = cpi->maxio;
		else
			softc->maxio = DFLTPHYS;
		mtx_init(&softc->lun_softc_mtx, "LUN softc mtx", NULL, MTX_DEF);
		STAILQ_INIT(&softc->lun_softc_list);

		port = &softc->port;
		port->frontend = &ctlfe_frontend;

		/*
		 * XXX KDM should we be more accurate here ?
		 */
		if (cpi->transport == XPORT_FC)
			port->port_type = CTL_PORT_FC;
		else if (cpi->transport == XPORT_SAS)
			port->port_type = CTL_PORT_SAS;
		else
			port->port_type = CTL_PORT_SCSI;

		/* XXX KDM what should the real number be here? */
		port->num_requested_ctl_io = CTLFE_REQ_CTL_IO;
		snprintf(softc->port_name, sizeof(softc->port_name),
			 "%s%d", cpi->dev_name, cpi->unit_number);
		/*
		 * XXX KDM it would be nice to allocate storage in the
		 * frontend structure itself.
	 	 */
		port->port_name = softc->port_name;
		port->physical_port = cpi->bus_id;
		port->virtual_port = 0;
		port->port_online = ctlfe_online;
		port->port_offline = ctlfe_offline;
		port->onoff_arg = softc;
		port->lun_enable = ctlfe_lun_enable;
		port->lun_disable = ctlfe_lun_disable;
		port->targ_lun_arg = softc;
		port->fe_datamove = ctlfe_datamove;
		port->fe_done = ctlfe_done;
		port->targ_port = -1;

		retval = ctl_port_register(port);
		if (retval != 0) {
			printf("%s: ctl_port_register() failed with "
			       "error %d!\n", __func__, retval);
			mtx_destroy(&softc->lun_softc_mtx);
			free(softc, M_CTLFE);
			break;
		} else {
			mtx_lock(&ctlfe_list_mtx);
			STAILQ_INSERT_TAIL(&ctlfe_softc_list, softc, links);
			mtx_unlock(&ctlfe_list_mtx);
		}

		break;
	}
	case AC_PATH_DEREGISTERED: {

		if (softc != NULL) {
			/*
			 * XXX KDM are we certain at this point that there
			 * are no outstanding commands for this frontend?
			 */
			mtx_lock(&ctlfe_list_mtx);
			STAILQ_REMOVE(&ctlfe_softc_list, softc, ctlfe_softc,
			    links);
			mtx_unlock(&ctlfe_list_mtx);
			ctl_port_deregister(&softc->port);
			mtx_destroy(&softc->lun_softc_mtx);
			free(softc, M_CTLFE);
		}
		break;
	}
	case AC_CONTRACT: {
		struct ac_contract *ac;

		ac = (struct ac_contract *)arg;

		switch (ac->contract_number) {
		case AC_CONTRACT_DEV_CHG: {
			struct ac_device_changed *dev_chg;
			int retval;

			dev_chg = (struct ac_device_changed *)ac->contract_data;

			printf("%s: WWPN %#jx port 0x%06x path %u target %u %s\n",
			       __func__, dev_chg->wwpn, dev_chg->port,
			       xpt_path_path_id(path), dev_chg->target,
			       (dev_chg->arrived == 0) ?  "left" : "arrived");

			if (softc == NULL) {
				printf("%s: CTL port for CAM path %u not "
				       "found!\n", __func__,
				       xpt_path_path_id(path));
				break;
			}
			if (dev_chg->arrived != 0) {
				retval = ctl_add_initiator(&softc->port,
				    dev_chg->target, dev_chg->wwpn, NULL);
			} else {
				retval = ctl_remove_initiator(&softc->port,
				    dev_chg->target);
			}

			if (retval < 0) {
				printf("%s: could not %s port %d iid %u "
				       "WWPN %#jx!\n", __func__,
				       (dev_chg->arrived != 0) ? "add" :
				       "remove", softc->port.targ_port,
				       dev_chg->target,
				       (uintmax_t)dev_chg->wwpn);
			}
			break;
		}
		default:
			printf("%s: unsupported contract number %ju\n",
			       __func__, (uintmax_t)ac->contract_number);
			break;
		}
		break;
	}
	default:
		break;
	}
}

static cam_status
ctlferegister(struct cam_periph *periph, void *arg)
{
	struct ctlfe_softc *bus_softc;
	struct ctlfe_lun_softc *softc;
	union ccb ccb;
	cam_status status;
	int i, acstatus;

	softc = (struct ctlfe_lun_softc *)arg;
	bus_softc = softc->parent_softc;
	
	STAILQ_INIT(&softc->work_queue);
	LIST_INIT(&softc->atio_list);
	LIST_INIT(&softc->inot_list);
	softc->periph = periph;
	periph->softc = softc;

	/* Increase device openings to maximum for the SIM. */
	if (bus_softc->sim->max_tagged_dev_openings >
	    bus_softc->sim->max_dev_openings) {
		cam_release_devq(periph->path,
		    /*relsim_flags*/RELSIM_ADJUST_OPENINGS,
		    /*openings*/bus_softc->sim->max_tagged_dev_openings,
		    /*timeout*/0,
		    /*getcount_only*/1);
	}

	xpt_setup_ccb(&ccb.ccb_h, periph->path, CAM_PRIORITY_NONE);
	ccb.ccb_h.func_code = XPT_EN_LUN;
	ccb.cel.grp6_len = 0;
	ccb.cel.grp7_len = 0;
	ccb.cel.enable = 1;
	xpt_action(&ccb);
	status = (ccb.ccb_h.status & CAM_STATUS_MASK);
	if (status != CAM_REQ_CMP) {
		xpt_print(periph->path, "%s: Enable LUN failed, status 0x%x\n", 
			  __func__, ccb.ccb_h.status);
		return (status);
	}

	status = CAM_REQ_CMP;

	for (i = 0; i < CTLFE_ATIO_PER_LUN; i++) {
		union ccb *new_ccb;
		union ctl_io *new_io;
		struct ctlfe_cmd_info *cmd_info;

		new_ccb = (union ccb *)malloc(sizeof(*new_ccb), M_CTLFE,
					      M_ZERO|M_NOWAIT);
		if (new_ccb == NULL) {
			status = CAM_RESRC_UNAVAIL;
			break;
		}
		new_io = ctl_alloc_io_nowait(bus_softc->port.ctl_pool_ref);
		if (new_io == NULL) {
			free(new_ccb, M_CTLFE);
			status = CAM_RESRC_UNAVAIL;
			break;
		}
		cmd_info = malloc(sizeof(*cmd_info), M_CTLFE,
		    M_ZERO | M_NOWAIT);
		if (cmd_info == NULL) {
			ctl_free_io(new_io);
			free(new_ccb, M_CTLFE);
			status = CAM_RESRC_UNAVAIL;
			break;
		}
		PRIV_INFO(new_io) = cmd_info;
		softc->atios_alloced++;
		new_ccb->ccb_h.io_ptr = new_io;
		LIST_INSERT_HEAD(&softc->atio_list, &new_ccb->ccb_h, periph_links.le);

		xpt_setup_ccb(&new_ccb->ccb_h, periph->path, CAM_PRIORITY_NONE);
		new_ccb->ccb_h.func_code = XPT_ACCEPT_TARGET_IO;
		new_ccb->ccb_h.cbfcnp = ctlfedone;
		new_ccb->ccb_h.flags |= CAM_UNLOCKED;
		xpt_action(new_ccb);
		status = new_ccb->ccb_h.status;
		if ((status & CAM_STATUS_MASK) != CAM_REQ_INPROG) {
			free(cmd_info, M_CTLFE);
			ctl_free_io(new_io);
			free(new_ccb, M_CTLFE);
			break;
		}
	}

	acstatus = cam_periph_acquire(periph);
	if (acstatus != 0) {
		xpt_print(periph->path, "%s: could not acquire reference "
			  "count, status = %#x\n", __func__, acstatus);
		return (CAM_REQ_CMP_ERR);
	}

	if (i == 0) {
		xpt_print(periph->path, "%s: could not allocate ATIO CCBs, "
			  "status 0x%x\n", __func__, status);
		return (CAM_REQ_CMP_ERR);
	}

	for (i = 0; i < CTLFE_IN_PER_LUN; i++) {
		union ccb *new_ccb;
		union ctl_io *new_io;

		new_ccb = (union ccb *)malloc(sizeof(*new_ccb), M_CTLFE,
					      M_ZERO|M_NOWAIT);
		if (new_ccb == NULL) {
			status = CAM_RESRC_UNAVAIL;
			break;
		}
		new_io = ctl_alloc_io_nowait(bus_softc->port.ctl_pool_ref);
		if (new_io == NULL) {
			free(new_ccb, M_CTLFE);
			status = CAM_RESRC_UNAVAIL;
			break;
		}
		softc->inots_alloced++;
		new_ccb->ccb_h.io_ptr = new_io;
		LIST_INSERT_HEAD(&softc->inot_list, &new_ccb->ccb_h, periph_links.le);

		xpt_setup_ccb(&new_ccb->ccb_h, periph->path, CAM_PRIORITY_NONE);
		new_ccb->ccb_h.func_code = XPT_IMMEDIATE_NOTIFY;
		new_ccb->ccb_h.cbfcnp = ctlfedone;
		new_ccb->ccb_h.flags |= CAM_UNLOCKED;
		xpt_action(new_ccb);
		status = new_ccb->ccb_h.status;
		if ((status & CAM_STATUS_MASK) != CAM_REQ_INPROG) {
			/*
			 * Note that we don't free the CCB here.  If the
			 * status is not CAM_REQ_INPROG, then we're
			 * probably talking to a SIM that says it is
			 * target-capable but doesn't support the 
			 * XPT_IMMEDIATE_NOTIFY CCB.  i.e. it supports the
			 * older API.  In that case, it'll call xpt_done()
			 * on the CCB, and we need to free it in our done
			 * routine as a result.
			 */
			break;
		}
	}
	if ((i == 0)
	 || (status != CAM_REQ_INPROG)) {
		xpt_print(periph->path, "%s: could not allocate immediate "
			  "notify CCBs, status 0x%x\n", __func__, status);
		return (CAM_REQ_CMP_ERR);
	}
	mtx_lock(&bus_softc->lun_softc_mtx);
	STAILQ_INSERT_TAIL(&bus_softc->lun_softc_list, softc, links);
	mtx_unlock(&bus_softc->lun_softc_mtx);
	return (CAM_REQ_CMP);
}

static void
ctlfeoninvalidate(struct cam_periph *periph)
{
	struct ctlfe_lun_softc *softc = (struct ctlfe_lun_softc *)periph->softc;
	struct ctlfe_softc *bus_softc;
	union ccb ccb;
	struct ccb_hdr *hdr;
	cam_status status;

	/* Abort all ATIOs and INOTs queued to SIM. */
	xpt_setup_ccb(&ccb.ccb_h, periph->path, CAM_PRIORITY_NONE);
	ccb.ccb_h.func_code = XPT_ABORT;
	LIST_FOREACH(hdr, &softc->atio_list, periph_links.le) {
		ccb.cab.abort_ccb = (union ccb *)hdr;
		xpt_action(&ccb);
	}
	LIST_FOREACH(hdr, &softc->inot_list, periph_links.le) {
		ccb.cab.abort_ccb = (union ccb *)hdr;
		xpt_action(&ccb);
	}

	/* Disable the LUN in SIM. */
	ccb.ccb_h.func_code = XPT_EN_LUN;
	ccb.cel.grp6_len = 0;
	ccb.cel.grp7_len = 0;
	ccb.cel.enable = 0;
	xpt_action(&ccb);
	status = (ccb.ccb_h.status & CAM_STATUS_MASK);
	if (status != CAM_REQ_CMP) {
		xpt_print(periph->path, "%s: Disable LUN failed, status 0x%x\n",
			  __func__, ccb.ccb_h.status);
		/*
		 * XXX KDM what do we do now?
		 */
	}

	bus_softc = softc->parent_softc;
	mtx_lock(&bus_softc->lun_softc_mtx);
	STAILQ_REMOVE(&bus_softc->lun_softc_list, softc, ctlfe_lun_softc, links);
	mtx_unlock(&bus_softc->lun_softc_mtx);
}

static void
ctlfecleanup(struct cam_periph *periph)
{
	struct ctlfe_lun_softc *softc;

	softc = (struct ctlfe_lun_softc *)periph->softc;

	KASSERT(softc->ctios_sent == 0, ("%s: ctios_sent %d != 0",
	    __func__, softc->ctios_sent));
	KASSERT(softc->refcount == 0, ("%s: refcount %d != 0",
	    __func__, softc->refcount));
	KASSERT(softc->atios_alloced == 0, ("%s: atios_alloced %d != 0",
	    __func__, softc->atios_alloced));
	KASSERT(softc->inots_alloced == 0, ("%s: inots_alloced %d != 0",
	    __func__, softc->inots_alloced));

	free(softc, M_CTLFE);
}

static void
ctlfedata(struct ctlfe_lun_softc *softc, union ctl_io *io,
    ccb_flags *flags, uint8_t **data_ptr, uint32_t *dxfer_len,
    u_int16_t *sglist_cnt)
{
	struct ctlfe_softc *bus_softc;
	struct ctlfe_cmd_info *cmd_info;
	struct ctl_sg_entry *ctl_sglist;
	bus_dma_segment_t *cam_sglist;
	size_t off;
	int i, idx;

	cmd_info = PRIV_INFO(io);
	bus_softc = softc->parent_softc;

	/*
	 * Set the direction, relative to the initiator.
	 */
	*flags &= ~CAM_DIR_MASK;
	if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_IN)
		*flags |= CAM_DIR_IN;
	else
		*flags |= CAM_DIR_OUT;

	*flags &= ~CAM_DATA_MASK;
	idx = cmd_info->cur_transfer_index;
	off = cmd_info->cur_transfer_off;
	cmd_info->flags &= ~CTLFE_CMD_PIECEWISE;
	if (io->scsiio.kern_sg_entries == 0) {	/* No S/G list. */

		/* One time shift for SRR offset. */
		off += io->scsiio.ext_data_filled;
		io->scsiio.ext_data_filled = 0;

		*data_ptr = io->scsiio.kern_data_ptr + off;
		if (io->scsiio.kern_data_len - off <= bus_softc->maxio) {
			*dxfer_len = io->scsiio.kern_data_len - off;
		} else {
			*dxfer_len = bus_softc->maxio;
			cmd_info->cur_transfer_off += bus_softc->maxio;
			cmd_info->flags |= CTLFE_CMD_PIECEWISE;
		}
		*sglist_cnt = 0;

		if (io->io_hdr.flags & CTL_FLAG_BUS_ADDR)
			*flags |= CAM_DATA_PADDR;
		else
			*flags |= CAM_DATA_VADDR;
	} else {	/* S/G list with physical or virtual pointers. */
		ctl_sglist = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;

		/* One time shift for SRR offset. */
		while (io->scsiio.ext_data_filled >= ctl_sglist[idx].len - off) {
			io->scsiio.ext_data_filled -= ctl_sglist[idx].len - off;
			idx++;
			off = 0;
		}
		off += io->scsiio.ext_data_filled;
		io->scsiio.ext_data_filled = 0;

		cam_sglist = cmd_info->cam_sglist;
		*dxfer_len = 0;
		for (i = 0; i < io->scsiio.kern_sg_entries - idx; i++) {
			cam_sglist[i].ds_addr = (bus_addr_t)(uintptr_t)ctl_sglist[i + idx].addr + off;
			if (ctl_sglist[i + idx].len - off <= bus_softc->maxio - *dxfer_len) {
				cam_sglist[i].ds_len = ctl_sglist[idx + i].len - off;
				*dxfer_len += cam_sglist[i].ds_len;
			} else {
				cam_sglist[i].ds_len = bus_softc->maxio - *dxfer_len;
				cmd_info->cur_transfer_index = idx + i;
				cmd_info->cur_transfer_off = cam_sglist[i].ds_len + off;
				cmd_info->flags |= CTLFE_CMD_PIECEWISE;
				*dxfer_len += cam_sglist[i].ds_len;
				if (ctl_sglist[i].len != 0)
					i++;
				break;
			}
			if (i == (CTLFE_MAX_SEGS - 1) &&
			    idx + i < (io->scsiio.kern_sg_entries - 1)) {
				cmd_info->cur_transfer_index = idx + i + 1;
				cmd_info->cur_transfer_off = 0;
				cmd_info->flags |= CTLFE_CMD_PIECEWISE;
				i++;
				break;
			}
			off = 0;
		}
		*sglist_cnt = i;
		if (io->io_hdr.flags & CTL_FLAG_BUS_ADDR)
			*flags |= CAM_DATA_SG_PADDR;
		else
			*flags |= CAM_DATA_SG;
		*data_ptr = (uint8_t *)cam_sglist;
	}
}

static void
ctlfestart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct ctlfe_lun_softc *softc;
	struct ctlfe_cmd_info *cmd_info;
	struct ccb_hdr *ccb_h;
	struct ccb_accept_tio *atio;
	struct ccb_scsiio *csio;
	uint8_t *data_ptr;
	uint32_t dxfer_len;
	ccb_flags flags;
	union ctl_io *io;
	uint8_t scsi_status;

	softc = (struct ctlfe_lun_softc *)periph->softc;

next:
	/* Take the ATIO off the work queue */
	ccb_h = STAILQ_FIRST(&softc->work_queue);
	if (ccb_h == NULL) {
		xpt_release_ccb(start_ccb);
		return;
	}
	STAILQ_REMOVE_HEAD(&softc->work_queue, periph_links.stqe);
	atio = (struct ccb_accept_tio *)ccb_h;
	io = (union ctl_io *)ccb_h->io_ptr;
	csio = &start_ccb->csio;

	flags = atio->ccb_h.flags &
		(CAM_DIS_DISCONNECT|CAM_TAG_ACTION_VALID|CAM_DIR_MASK);
	cmd_info = PRIV_INFO(io);
	cmd_info->cur_transfer_index = 0;
	cmd_info->cur_transfer_off = 0;
	cmd_info->flags = 0;

	if (io->io_hdr.flags & CTL_FLAG_DMA_QUEUED) {
		/*
		 * Datamove call, we need to setup the S/G list.
		 */
		ctlfedata(softc, io, &flags, &data_ptr, &dxfer_len,
		    &csio->sglist_cnt);
	} else {
		/*
		 * We're done, send status back.
		 */
		if ((io->io_hdr.flags & CTL_FLAG_ABORT) &&
		    (io->io_hdr.flags & CTL_FLAG_ABORT_STATUS) == 0) {
			io->io_hdr.flags &= ~CTL_FLAG_STATUS_QUEUED;

			/* Tell the SIM that we've aborted this ATIO */
#ifdef CTLFEDEBUG
			printf("%s: tag %04x abort\n", __func__, atio->tag_id);
#endif
			KASSERT(atio->ccb_h.func_code == XPT_ACCEPT_TARGET_IO,
			    ("func_code %#x is not ATIO", atio->ccb_h.func_code));
			start_ccb->ccb_h.func_code = XPT_ABORT;
			start_ccb->cab.abort_ccb = (union ccb *)atio;
			xpt_action(start_ccb);

			ctlfe_requeue_ccb(periph, (union ccb *)atio,
			    /* unlock */0);

			/* XPT_ABORT is not queued, so we can take next I/O. */
			goto next;
		}
		data_ptr = NULL;
		dxfer_len = 0;
		csio->sglist_cnt = 0;
	}
	scsi_status = 0;
	if ((io->io_hdr.flags & CTL_FLAG_STATUS_QUEUED) &&
	    (cmd_info->flags & CTLFE_CMD_PIECEWISE) == 0 &&
	    ((io->io_hdr.flags & CTL_FLAG_DMA_QUEUED) == 0 ||
	     io->io_hdr.status == CTL_SUCCESS)) {
		flags |= CAM_SEND_STATUS;
		scsi_status = io->scsiio.scsi_status;
		csio->sense_len = io->scsiio.sense_len;
#ifdef CTLFEDEBUG
		printf("%s: tag %04x status %x\n", __func__,
		       atio->tag_id, io->io_hdr.status);
#endif
		if (csio->sense_len != 0) {
			csio->sense_data = io->scsiio.sense_data;
			flags |= CAM_SEND_SENSE;
		}
	}

#ifdef CTLFEDEBUG
	printf("%s: %s: tag %04x flags %x ptr %p len %u\n", __func__,
	       (flags & CAM_SEND_STATUS) ? "done" : "datamove",
	       atio->tag_id, flags, data_ptr, dxfer_len);
#endif

	/*
	 * Valid combinations:
	 *  - CAM_SEND_STATUS, CAM_DATA_SG = 0, dxfer_len = 0,
	 *    sglist_cnt = 0
	 *  - CAM_SEND_STATUS = 0, CAM_DATA_SG = 0, dxfer_len != 0,
	 *    sglist_cnt = 0
	 *  - CAM_SEND_STATUS = 0, CAM_DATA_SG, dxfer_len != 0,
	 *    sglist_cnt != 0
	 */
#ifdef CTLFEDEBUG
	if (((flags & CAM_SEND_STATUS)
	  && (((flags & CAM_DATA_SG) != 0)
	   || (dxfer_len != 0)
	   || (csio->sglist_cnt != 0)))
	 || (((flags & CAM_SEND_STATUS) == 0)
	  && (dxfer_len == 0))
	 || ((flags & CAM_DATA_SG)
	  && (csio->sglist_cnt == 0))
	 || (((flags & CAM_DATA_SG) == 0)
	  && (csio->sglist_cnt != 0))) {
		printf("%s: tag %04x cdb %02x flags %#x dxfer_len "
		       "%d sg %u\n", __func__, atio->tag_id,
		       atio_cdb_ptr(atio)[0], flags, dxfer_len,
		       csio->sglist_cnt);
		printf("%s: tag %04x io status %#x\n", __func__,
		       atio->tag_id, io->io_hdr.status);
	}
#endif
	cam_fill_ctio(csio,
		      /*retries*/ 2,
		      ctlfedone,
		      flags,
		      (flags & CAM_TAG_ACTION_VALID) ? MSG_SIMPLE_Q_TAG : 0,
		      atio->tag_id,
		      atio->init_id,
		      scsi_status,
		      /*data_ptr*/ data_ptr,
		      /*dxfer_len*/ dxfer_len,
		      /*timeout*/ CTLFE_TIMEOUT * 1000);
	start_ccb->ccb_h.flags |= CAM_UNLOCKED;
	start_ccb->ccb_h.ccb_atio = atio;
	if (io->io_hdr.flags & CTL_FLAG_DMA_QUEUED)
		io->io_hdr.flags |= CTL_FLAG_DMA_INPROG;
	io->io_hdr.flags &= ~(CTL_FLAG_DMA_QUEUED | CTL_FLAG_STATUS_QUEUED);

	softc->ctios_sent++;
	softc->refcount++;
	cam_periph_unlock(periph);
	xpt_action(start_ccb);
	cam_periph_lock(periph);
	softc->refcount--;

	/*
	 * If we still have work to do, ask for another CCB.
	 */
	if (!STAILQ_EMPTY(&softc->work_queue))
		xpt_schedule(periph, CAM_PRIORITY_NORMAL);
}

static void
ctlfe_drain(void *context, int pending)
{
	struct cam_periph *periph = context;
	struct ctlfe_lun_softc *softc = periph->softc;

	cam_periph_lock(periph);
	while (softc->refcount != 0) {
		cam_periph_sleep(periph, &softc->refcount, PRIBIO,
		    "ctlfe_drain", 1);
	}
	cam_periph_unlock(periph);
	cam_periph_release(periph);
}

static void
ctlfe_free_ccb(struct cam_periph *periph, union ccb *ccb)
{
	struct ctlfe_lun_softc *softc;
	union ctl_io *io;
	struct ctlfe_cmd_info *cmd_info;

	softc = (struct ctlfe_lun_softc *)periph->softc;
	io = ccb->ccb_h.io_ptr;

	switch (ccb->ccb_h.func_code) {
	case XPT_ACCEPT_TARGET_IO:
		softc->atios_alloced--;
		cmd_info = PRIV_INFO(io);
		free(cmd_info, M_CTLFE);
		break;
	case XPT_IMMEDIATE_NOTIFY:
	case XPT_NOTIFY_ACKNOWLEDGE:
		softc->inots_alloced--;
		break;
	default:
		break;
	}

	ctl_free_io(io);
	free(ccb, M_CTLFE);

	KASSERT(softc->atios_alloced >= 0, ("%s: atios_alloced %d < 0",
	    __func__, softc->atios_alloced));
	KASSERT(softc->inots_alloced >= 0, ("%s: inots_alloced %d < 0",
	    __func__, softc->inots_alloced));

	/*
	 * If we have received all of our CCBs, we can release our
	 * reference on the peripheral driver.  It will probably go away
	 * now.
	 */
	if (softc->atios_alloced == 0 && softc->inots_alloced == 0) {
		if (softc->refcount == 0) {
			cam_periph_release_locked(periph);
		} else {
			TASK_INIT(&softc->refdrain_task, 0, ctlfe_drain, periph);
			taskqueue_enqueue(taskqueue_thread,
			    &softc->refdrain_task);
		}
	}
}

/*
 * Send the ATIO/INOT back to the SIM, or free it if periph was invalidated.
 */
static void
ctlfe_requeue_ccb(struct cam_periph *periph, union ccb *ccb, int unlock)
{
	struct ctlfe_lun_softc *softc;
	struct mtx *mtx;

	if (periph->flags & CAM_PERIPH_INVALID) {
		mtx = cam_periph_mtx(periph);
		ctlfe_free_ccb(periph, ccb);
		if (unlock)
			mtx_unlock(mtx);
		return;
	}
	softc = (struct ctlfe_lun_softc *)periph->softc;
	if (ccb->ccb_h.func_code == XPT_ACCEPT_TARGET_IO)
		LIST_INSERT_HEAD(&softc->atio_list, &ccb->ccb_h, periph_links.le);
	else
		LIST_INSERT_HEAD(&softc->inot_list, &ccb->ccb_h, periph_links.le);
	if (unlock)
		cam_periph_unlock(periph);

	/*
	 * For a wildcard attachment, commands can come in with a specific
	 * target/lun.  Reset the target and LUN fields back to the wildcard
	 * values before we send them back down to the SIM.
	 */
	xpt_setup_ccb_flags(&ccb->ccb_h, periph->path, CAM_PRIORITY_NONE,
	    ccb->ccb_h.flags);

	xpt_action(ccb);
}

static int
ctlfe_adjust_cdb(struct ccb_accept_tio *atio, uint32_t offset)
{
	uint64_t lba;
	uint32_t num_blocks, nbc;
	uint8_t *cmdbyt = atio_cdb_ptr(atio);

	nbc = offset >> 9;	/* ASSUMING 512 BYTE BLOCKS */

	switch (cmdbyt[0]) {
	case READ_6:
	case WRITE_6:
	{
		struct scsi_rw_6 *cdb = (struct scsi_rw_6 *)cmdbyt;
		lba = scsi_3btoul(cdb->addr);
		lba &= 0x1fffff;
		num_blocks = cdb->length;
		if (num_blocks == 0)
			num_blocks = 256;
		lba += nbc;
		num_blocks -= nbc;
		scsi_ulto3b(lba, cdb->addr);
		cdb->length = num_blocks;
		break;
	}
	case READ_10:
	case WRITE_10:
	{
		struct scsi_rw_10 *cdb = (struct scsi_rw_10 *)cmdbyt;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_2btoul(cdb->length);
		lba += nbc;
		num_blocks -= nbc;
		scsi_ulto4b(lba, cdb->addr);
		scsi_ulto2b(num_blocks, cdb->length);
		break;
	}
	case READ_12:
	case WRITE_12:
	{
		struct scsi_rw_12 *cdb = (struct scsi_rw_12 *)cmdbyt;
		lba = scsi_4btoul(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		lba += nbc;
		num_blocks -= nbc;
		scsi_ulto4b(lba, cdb->addr);
		scsi_ulto4b(num_blocks, cdb->length);
		break;
	}
	case READ_16:
	case WRITE_16:
	{
		struct scsi_rw_16 *cdb = (struct scsi_rw_16 *)cmdbyt;
		lba = scsi_8btou64(cdb->addr);
		num_blocks = scsi_4btoul(cdb->length);
		lba += nbc;
		num_blocks -= nbc;
		scsi_u64to8b(lba, cdb->addr);
		scsi_ulto4b(num_blocks, cdb->length);
		break;
	}
	default:
		return -1;
	}
	return (0);
}

static void
ctlfedone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct ctlfe_lun_softc *softc;
	struct ctlfe_softc *bus_softc;
	struct ctlfe_cmd_info *cmd_info;
	struct ccb_accept_tio *atio = NULL;
	union ctl_io *io = NULL;
	struct mtx *mtx;
	cam_status status;

	KASSERT((done_ccb->ccb_h.flags & CAM_UNLOCKED) != 0,
	    ("CCB in ctlfedone() without CAM_UNLOCKED flag"));
#ifdef CTLFE_DEBUG
	printf("%s: entered, func_code = %#x\n", __func__,
	       done_ccb->ccb_h.func_code);
#endif

	/*
	 * At this point CTL has no known use case for device queue freezes.
	 * In case some SIM think different -- drop its freeze right here.
	 */
	if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
		cam_release_devq(periph->path,
				 /*relsim_flags*/0,
				 /*reduction*/0,
				 /*timeout*/0,
				 /*getcount_only*/0);
		done_ccb->ccb_h.status &= ~CAM_DEV_QFRZN;
	}

	softc = (struct ctlfe_lun_softc *)periph->softc;
	bus_softc = softc->parent_softc;
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);

	switch (done_ccb->ccb_h.func_code) {
	case XPT_ACCEPT_TARGET_IO: {

		LIST_REMOVE(&done_ccb->ccb_h, periph_links.le);
		atio = &done_ccb->atio;
		status = atio->ccb_h.status & CAM_STATUS_MASK;
		if (status != CAM_CDB_RECVD) {
			ctlfe_free_ccb(periph, done_ccb);
			goto out;
		}

 resubmit:
		/*
		 * Allocate a ctl_io, pass it to CTL, and wait for the
		 * datamove or done.
		 */
		mtx_unlock(mtx);
		io = done_ccb->ccb_h.io_ptr;
		cmd_info = PRIV_INFO(io);
		ctl_zero_io(io);

		/* Save pointers on both sides */
		PRIV_CCB(io) = done_ccb;
		PRIV_INFO(io) = cmd_info;
		done_ccb->ccb_h.io_ptr = io;

		/*
		 * Only SCSI I/O comes down this path, resets, etc. come
		 * down the immediate notify path below.
		 */
		io->io_hdr.io_type = CTL_IO_SCSI;
		io->io_hdr.nexus.initid = atio->init_id;
		io->io_hdr.nexus.targ_port = bus_softc->port.targ_port;
		if (bus_softc->hba_misc & PIM_EXTLUNS) {
			io->io_hdr.nexus.targ_lun = ctl_decode_lun(
			    CAM_EXTLUN_BYTE_SWIZZLE(atio->ccb_h.target_lun));
		} else {
			io->io_hdr.nexus.targ_lun = atio->ccb_h.target_lun;
		}
		io->scsiio.tag_num = atio->tag_id;
		switch (atio->tag_action) {
		case CAM_TAG_ACTION_NONE:
			io->scsiio.tag_type = CTL_TAG_UNTAGGED;
			break;
		case MSG_SIMPLE_TASK:
			io->scsiio.tag_type = CTL_TAG_SIMPLE;
			break;
		case MSG_HEAD_OF_QUEUE_TASK:
        		io->scsiio.tag_type = CTL_TAG_HEAD_OF_QUEUE;
			break;
		case MSG_ORDERED_TASK:
        		io->scsiio.tag_type = CTL_TAG_ORDERED;
			break;
		case MSG_ACA_TASK:
			io->scsiio.tag_type = CTL_TAG_ACA;
			break;
		default:
			io->scsiio.tag_type = CTL_TAG_UNTAGGED;
			printf("%s: unhandled tag type %#x!!\n", __func__,
			       atio->tag_action);
			break;
		}
		if (atio->cdb_len > sizeof(io->scsiio.cdb)) {
			printf("%s: WARNING: CDB len %d > ctl_io space %zd\n",
			       __func__, atio->cdb_len, sizeof(io->scsiio.cdb));
		}
		io->scsiio.cdb_len = min(atio->cdb_len, sizeof(io->scsiio.cdb));
		bcopy(atio_cdb_ptr(atio), io->scsiio.cdb, io->scsiio.cdb_len);

#ifdef CTLFEDEBUG
		printf("%s: %u:%u:%u: tag %04x CDB %02x\n", __func__,
		        io->io_hdr.nexus.initid,
		        io->io_hdr.nexus.targ_port,
		        io->io_hdr.nexus.targ_lun,
			io->scsiio.tag_num, io->scsiio.cdb[0]);
#endif

		ctl_queue(io);
		return;
	}
	case XPT_CONT_TARGET_IO: {
		int srr = 0;
		uint32_t srr_off = 0;

		atio = (struct ccb_accept_tio *)done_ccb->ccb_h.ccb_atio;
		io = (union ctl_io *)atio->ccb_h.io_ptr;

		softc->ctios_sent--;
#ifdef CTLFEDEBUG
		printf("%s: got XPT_CONT_TARGET_IO tag %#x flags %#x\n",
		       __func__, atio->tag_id, done_ccb->ccb_h.flags);
#endif
		/*
		 * Handle SRR case were the data pointer is pushed back hack
		 */
		if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_MESSAGE_RECV
		    && done_ccb->csio.msg_ptr != NULL
		    && done_ccb->csio.msg_ptr[0] == MSG_EXTENDED
		    && done_ccb->csio.msg_ptr[1] == 5
       		    && done_ccb->csio.msg_ptr[2] == 0) {
			srr = 1;
			srr_off =
			    (done_ccb->csio.msg_ptr[3] << 24)
			    | (done_ccb->csio.msg_ptr[4] << 16)
			    | (done_ccb->csio.msg_ptr[5] << 8)
			    | (done_ccb->csio.msg_ptr[6]);
		}

		/*
		 * If we have an SRR and we're still sending data, we
		 * should be able to adjust offsets and cycle again.
		 * It is possible only if offset is from this datamove.
		 */
		if (srr && (io->io_hdr.flags & CTL_FLAG_DMA_INPROG) &&
		    srr_off >= io->scsiio.kern_rel_offset &&
		    srr_off < io->scsiio.kern_rel_offset +
		     io->scsiio.kern_data_len) {
			io->scsiio.kern_data_resid =
			    io->scsiio.kern_rel_offset +
			    io->scsiio.kern_data_len - srr_off;
			io->scsiio.ext_data_filled = srr_off;
			io->scsiio.io_hdr.status = CTL_STATUS_NONE;
			io->io_hdr.flags |= CTL_FLAG_DMA_QUEUED;
			xpt_release_ccb(done_ccb);
			STAILQ_INSERT_HEAD(&softc->work_queue, &atio->ccb_h,
					  periph_links.stqe);
			xpt_schedule(periph, CAM_PRIORITY_NORMAL);
			break;
		}

		/*
		 * If status was being sent, the back end data is now history.
		 * Hack it up and resubmit a new command with the CDB adjusted.
		 * If the SIM does the right thing, all of the resid math
		 * should work.
		 */
		if (srr && (io->io_hdr.flags & CTL_FLAG_DMA_INPROG) == 0) {
			xpt_release_ccb(done_ccb);
			if (ctlfe_adjust_cdb(atio, srr_off) == 0) {
				done_ccb = (union ccb *)atio;
				goto resubmit;
			}
			/*
			 * Fall through to doom....
			 */
		}

		if ((done_ccb->ccb_h.flags & CAM_SEND_STATUS) &&
		    (done_ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
			io->io_hdr.flags |= CTL_FLAG_STATUS_SENT;

		/*
		 * If we were sending status back to the initiator, free up
		 * resources.  If we were doing a datamove, call the
		 * datamove done routine.
		 */
		if ((io->io_hdr.flags & CTL_FLAG_DMA_INPROG) == 0) {
			/*
			 * If we asked to send sense data but it wasn't sent,
			 * queue the I/O back to CTL for later REQUEST SENSE.
			 */
			if ((done_ccb->ccb_h.flags & CAM_SEND_SENSE) != 0 &&
			    (done_ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP &&
			    (done_ccb->ccb_h.status & CAM_SENT_SENSE) == 0 &&
			    (io = ctl_alloc_io_nowait(bus_softc->port.ctl_pool_ref)) != NULL) {
				PRIV_INFO(io) = PRIV_INFO(
				    (union ctl_io *)atio->ccb_h.io_ptr);
				ctl_queue_sense(atio->ccb_h.io_ptr);
				atio->ccb_h.io_ptr = io;
			}

			/* Abort ATIO if CTIO sending status has failed. */
			if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) !=
			    CAM_REQ_CMP) {
				done_ccb->ccb_h.func_code = XPT_ABORT;
				done_ccb->cab.abort_ccb = (union ccb *)atio;
				xpt_action(done_ccb);
			}

			xpt_release_ccb(done_ccb);
			ctlfe_requeue_ccb(periph, (union ccb *)atio,
			    /* unlock */1);
			return;
		} else {
			struct ctlfe_cmd_info *cmd_info;
			struct ccb_scsiio *csio;

			csio = &done_ccb->csio;
			cmd_info = PRIV_INFO(io);

			io->io_hdr.flags &= ~CTL_FLAG_DMA_INPROG;

			/*
			 * Translate CAM status to CTL status.  Success
			 * does not change the overall, ctl_io status.  In
			 * that case we just set port_status to 0.  If we
			 * have a failure, though, set a data phase error
			 * for the overall ctl_io.
			 */
			switch (done_ccb->ccb_h.status & CAM_STATUS_MASK) {
			case CAM_REQ_CMP:
				io->scsiio.kern_data_resid -=
				    csio->dxfer_len - csio->resid;
				io->io_hdr.port_status = 0;
				break;
			default:
				/*
				 * XXX KDM we probably need to figure out a
				 * standard set of errors that the SIM
				 * drivers should return in the event of a
				 * data transfer failure.  A data phase
				 * error will at least point the user to a
				 * data transfer error of some sort.
				 * Hopefully the SIM printed out some
				 * additional information to give the user
				 * a clue what happened.
				 */
				io->io_hdr.port_status = 0xbad1;
				ctl_set_data_phase_error(&io->scsiio);
				/*
				 * XXX KDM figure out residual.
				 */
				break;
			}
			/*
			 * If we had to break this S/G list into multiple
			 * pieces, figure out where we are in the list, and
			 * continue sending pieces if necessary.
			 */
			if ((cmd_info->flags & CTLFE_CMD_PIECEWISE) &&
			    io->io_hdr.port_status == 0 && csio->resid == 0) {
				ccb_flags flags;
				uint8_t *data_ptr;
				uint32_t dxfer_len;

				flags = atio->ccb_h.flags &
					(CAM_DIS_DISCONNECT|
					 CAM_TAG_ACTION_VALID);

				ctlfedata(softc, io, &flags, &data_ptr,
				    &dxfer_len, &csio->sglist_cnt);

				if (((flags & CAM_SEND_STATUS) == 0)
				 && (dxfer_len == 0)) {
					printf("%s: tag %04x no status or "
					       "len cdb = %02x\n", __func__,
					       atio->tag_id,
					       atio_cdb_ptr(atio)[0]);
					printf("%s: tag %04x io status %#x\n",
					       __func__, atio->tag_id,
					       io->io_hdr.status);
				}

				cam_fill_ctio(csio,
					      /*retries*/ 2,
					      ctlfedone,
					      flags,
					      (flags & CAM_TAG_ACTION_VALID) ?
					       MSG_SIMPLE_Q_TAG : 0,
					      atio->tag_id,
					      atio->init_id,
					      0,
					      /*data_ptr*/ data_ptr,
					      /*dxfer_len*/ dxfer_len,
					      CTLFE_TIMEOUT * 1000);

				csio->ccb_h.flags |= CAM_UNLOCKED;
				csio->resid = 0;
				csio->ccb_h.ccb_atio = atio;
				io->io_hdr.flags |= CTL_FLAG_DMA_INPROG;
				softc->ctios_sent++;
				mtx_unlock(mtx);
				xpt_action((union ccb *)csio);
			} else {
				/*
				 * Release the CTIO.  The ATIO will be sent back
				 * down to the SIM once we send status.
				 */
				xpt_release_ccb(done_ccb);
				mtx_unlock(mtx);

				/* Call the backend move done callback */
				io->scsiio.be_move_done(io);
			}
			return;
		}
		break;
	}
	case XPT_IMMEDIATE_NOTIFY: {
		union ctl_io *io;
		struct ccb_immediate_notify *inot;
		int send_ctl_io;

		LIST_REMOVE(&done_ccb->ccb_h, periph_links.le);
		inot = &done_ccb->cin1;
		io = done_ccb->ccb_h.io_ptr;
		ctl_zero_io(io);

		send_ctl_io = 1;

		io->io_hdr.io_type = CTL_IO_TASK;
		PRIV_CCB(io) = done_ccb;
		inot->ccb_h.io_ptr = io;
		io->io_hdr.nexus.initid = inot->initiator_id;
		io->io_hdr.nexus.targ_port = bus_softc->port.targ_port;
		if (bus_softc->hba_misc & PIM_EXTLUNS) {
			io->io_hdr.nexus.targ_lun = ctl_decode_lun(
			    CAM_EXTLUN_BYTE_SWIZZLE(inot->ccb_h.target_lun));
		} else {
			io->io_hdr.nexus.targ_lun = inot->ccb_h.target_lun;
		}
		/* XXX KDM should this be the tag_id? */
		io->taskio.tag_num = inot->seq_id;

		status = inot->ccb_h.status & CAM_STATUS_MASK;
		switch (status) {
		case CAM_SCSI_BUS_RESET:
			io->taskio.task_action = CTL_TASK_BUS_RESET;
			break;
		case CAM_BDR_SENT:
			io->taskio.task_action = CTL_TASK_TARGET_RESET;
			break;
		case CAM_MESSAGE_RECV:
			switch (inot->arg) {
			case MSG_ABORT_TASK_SET:
				io->taskio.task_action =
				    CTL_TASK_ABORT_TASK_SET;
				break;
			case MSG_TARGET_RESET:
				io->taskio.task_action = CTL_TASK_TARGET_RESET;
				break;
			case MSG_ABORT_TASK:
				io->taskio.task_action = CTL_TASK_ABORT_TASK;
				break;
			case MSG_LOGICAL_UNIT_RESET:
				io->taskio.task_action = CTL_TASK_LUN_RESET;
				break;
			case MSG_CLEAR_TASK_SET:
				io->taskio.task_action =
				    CTL_TASK_CLEAR_TASK_SET;
				break;
			case MSG_CLEAR_ACA:
				io->taskio.task_action = CTL_TASK_CLEAR_ACA;
				break;
			case MSG_QUERY_TASK:
				io->taskio.task_action = CTL_TASK_QUERY_TASK;
				break;
			case MSG_QUERY_TASK_SET:
				io->taskio.task_action =
				    CTL_TASK_QUERY_TASK_SET;
				break;
			case MSG_QUERY_ASYNC_EVENT:
				io->taskio.task_action =
				    CTL_TASK_QUERY_ASYNC_EVENT;
				break;
			case MSG_NOOP:
				send_ctl_io = 0;
				break;
			default:
				xpt_print(periph->path,
				    "%s: unsupported INOT message 0x%x\n",
				    __func__, inot->arg);
				send_ctl_io = 0;
				break;
			}
			break;
		default:
			xpt_print(periph->path,
			    "%s: unsupported INOT status 0x%x\n",
			    __func__, status);
			/* FALLTHROUGH */
		case CAM_REQ_ABORTED:
		case CAM_REQ_INVALID:
		case CAM_DEV_NOT_THERE:
		case CAM_PROVIDE_FAIL:
			ctlfe_free_ccb(periph, done_ccb);
			goto out;
		}
		if (send_ctl_io != 0) {
			ctl_queue(io);
		} else {
			done_ccb->ccb_h.status = CAM_REQ_INPROG;
			done_ccb->ccb_h.func_code = XPT_NOTIFY_ACKNOWLEDGE;
			xpt_action(done_ccb);
		}
		break;
	}
	case XPT_NOTIFY_ACKNOWLEDGE:
		/* Queue this back down to the SIM as an immediate notify. */
		done_ccb->ccb_h.status = CAM_REQ_INPROG;
		done_ccb->ccb_h.func_code = XPT_IMMEDIATE_NOTIFY;
		ctlfe_requeue_ccb(periph, done_ccb, /* unlock */1);
		return;
	case XPT_SET_SIM_KNOB:
	case XPT_GET_SIM_KNOB:
	case XPT_GET_SIM_KNOB_OLD:
		break;
	default:
		panic("%s: unexpected CCB type %#x", __func__,
		      done_ccb->ccb_h.func_code);
		break;
	}

out:
	mtx_unlock(mtx);
}

static void
ctlfe_onoffline(void *arg, int online)
{
	struct ctlfe_softc *bus_softc = arg;
	union ccb *ccb;
	cam_status status;
	struct cam_path *path;
	int set_wwnn = 0;

	status = xpt_create_path(&path, /*periph*/ NULL, bus_softc->path_id,
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP) {
		printf("%s: unable to create path!\n", __func__);
		return;
	}
	ccb = xpt_alloc_ccb();
	xpt_setup_ccb(&ccb->ccb_h, path, CAM_PRIORITY_NONE);
	ccb->ccb_h.func_code = XPT_GET_SIM_KNOB;
	xpt_action(ccb);

	/* Check whether we should change WWNs. */
	if (online != 0) {
		if ((ccb->knob.xport_specific.valid & KNOB_VALID_ADDRESS) != 0){
			printf("%s: %s current WWNN %#jx\n", __func__,
			       bus_softc->port_name,
			       ccb->knob.xport_specific.fc.wwnn);
			printf("%s: %s current WWPN %#jx\n", __func__,
			       bus_softc->port_name,
			       ccb->knob.xport_specific.fc.wwpn);

			/*
			 * If the user has specified a WWNN/WWPN, send them
			 * down to the SIM.  Otherwise, record what the SIM
			 * has reported.
			 */
			if (bus_softc->port.wwnn != 0 && bus_softc->port.wwnn
			    != ccb->knob.xport_specific.fc.wwnn) {
				ccb->knob.xport_specific.fc.wwnn =
				    bus_softc->port.wwnn;
				set_wwnn = 1;
			} else {
				ctl_port_set_wwns(&bus_softc->port,
				    true, ccb->knob.xport_specific.fc.wwnn,
				    false, 0);
			}
			if (bus_softc->port.wwpn != 0 && bus_softc->port.wwpn
			     != ccb->knob.xport_specific.fc.wwpn) {
				ccb->knob.xport_specific.fc.wwpn =
				    bus_softc->port.wwpn;
				set_wwnn = 1;
			} else {
				ctl_port_set_wwns(&bus_softc->port,
				    false, 0,
				    true, ccb->knob.xport_specific.fc.wwpn);
			}
		} else {
			printf("%s: %s has no valid WWNN/WWPN\n", __func__,
			       bus_softc->port_name);
			if (bus_softc->port.wwnn != 0) {
				ccb->knob.xport_specific.fc.wwnn =
				    bus_softc->port.wwnn;
				set_wwnn = 1;
			}
			if (bus_softc->port.wwpn != 0) {
				ccb->knob.xport_specific.fc.wwpn =
				    bus_softc->port.wwpn;
				set_wwnn = 1;
			}
		}
	}
	if (set_wwnn) {
		ccb->ccb_h.func_code = XPT_SET_SIM_KNOB;
		ccb->knob.xport_specific.valid = KNOB_VALID_ADDRESS;
		xpt_action(ccb);
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			printf("%s: %s (path id %d) failed set WWNs: %#x\n",
			    __func__, bus_softc->port_name, bus_softc->path_id,
			    ccb->ccb_h.status);
		} else {
			printf("%s: %s new WWNN %#jx\n", __func__,
			       bus_softc->port_name,
			       ccb->knob.xport_specific.fc.wwnn);
			printf("%s: %s new WWPN %#jx\n", __func__,
			       bus_softc->port_name,
			       ccb->knob.xport_specific.fc.wwpn);
		}
	}

	/* Check whether we should change role. */
	if ((ccb->knob.xport_specific.valid & KNOB_VALID_ROLE) == 0 ||
	    ((online != 0) ^
	    ((ccb->knob.xport_specific.fc.role & KNOB_ROLE_TARGET) != 0)) != 0) {
		ccb->ccb_h.func_code = XPT_SET_SIM_KNOB;
		ccb->knob.xport_specific.valid = KNOB_VALID_ROLE;
		if (online)
			ccb->knob.xport_specific.fc.role |= KNOB_ROLE_TARGET;
		else
			ccb->knob.xport_specific.fc.role &= ~KNOB_ROLE_TARGET;
		xpt_action(ccb);
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			printf("%s: %s (path id %d) failed %s target role: %#x\n",
			    __func__, bus_softc->port_name, bus_softc->path_id,
			    online ? "enable" : "disable", ccb->ccb_h.status);
		} else {
			printf("%s: %s (path id %d) target role %s succeeded\n",
			    __func__, bus_softc->port_name, bus_softc->path_id,
			    online ? "enable" : "disable");
		}
	}

	xpt_free_path(path);
	xpt_free_ccb(ccb);
}

static void
ctlfe_online(void *arg)
{
	struct ctlfe_softc *bus_softc;
	struct cam_path *path;
	cam_status status;
	struct ctlfe_lun_softc *lun_softc;
	struct cam_periph *periph;

	bus_softc = (struct ctlfe_softc *)arg;

	/*
	 * Create the wildcard LUN before bringing the port online.
	 */
	status = xpt_create_path(&path, /*periph*/ NULL,
				 bus_softc->path_id, CAM_TARGET_WILDCARD,
				 CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP) {
		printf("%s: unable to create path for wildcard periph\n",
				__func__);
		return;
	}

	lun_softc = malloc(sizeof(*lun_softc), M_CTLFE, M_WAITOK | M_ZERO);

	xpt_path_lock(path);
	periph = cam_periph_find(path, "ctl");
	if (periph != NULL) {
		/* We've already got a periph, no need to alloc a new one. */
		xpt_path_unlock(path);
		xpt_free_path(path);
		free(lun_softc, M_CTLFE);
		return;
	}
	lun_softc->parent_softc = bus_softc;
	lun_softc->flags |= CTLFE_LUN_WILDCARD;

	status = cam_periph_alloc(ctlferegister,
				  ctlfeoninvalidate,
				  ctlfecleanup,
				  ctlfestart,
				  "ctl",
				  CAM_PERIPH_BIO,
				  path,
				  ctlfeasync,
				  0,
				  lun_softc);

	if ((status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		const struct cam_status_entry *entry;

		entry = cam_fetch_status_entry(status);
		printf("%s: CAM error %s (%#x) returned from "
		       "cam_periph_alloc()\n", __func__, (entry != NULL) ?
		       entry->status_text : "Unknown", status);
		free(lun_softc, M_CTLFE);
	}

	xpt_path_unlock(path);
	ctlfe_onoffline(arg, /*online*/ 1);
	xpt_free_path(path);
}

static void
ctlfe_offline(void *arg)
{
	struct ctlfe_softc *bus_softc;
	struct cam_path *path;
	cam_status status;
	struct cam_periph *periph;

	bus_softc = (struct ctlfe_softc *)arg;

	ctlfe_onoffline(arg, /*online*/ 0);

	/*
	 * Disable the wildcard LUN for this port now that we have taken
	 * the port offline.
	 */
	status = xpt_create_path(&path, /*periph*/ NULL,
				 bus_softc->path_id, CAM_TARGET_WILDCARD,
				 CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP) {
		printf("%s: unable to create path for wildcard periph\n",
		       __func__);
		return;
	}
	xpt_path_lock(path);
	if ((periph = cam_periph_find(path, "ctl")) != NULL)
		cam_periph_invalidate(periph);
	xpt_path_unlock(path);
	xpt_free_path(path);
}

/*
 * This will get called to enable a LUN on every bus that is attached to
 * CTL.  So we only need to create a path/periph for this particular bus.
 */
static int
ctlfe_lun_enable(void *arg, int lun_id)
{
	struct ctlfe_softc *bus_softc;
	struct ctlfe_lun_softc *softc;
	struct cam_path *path;
	struct cam_periph *periph;
	cam_status status;

	bus_softc = (struct ctlfe_softc *)arg;
	if (bus_softc->hba_misc & PIM_EXTLUNS)
		lun_id = CAM_EXTLUN_BYTE_SWIZZLE(ctl_encode_lun(lun_id));

	status = xpt_create_path(&path, /*periph*/ NULL,
	    bus_softc->path_id, bus_softc->target_id, lun_id);
	/* XXX KDM need some way to return status to CTL here? */
	if (status != CAM_REQ_CMP) {
		printf("%s: could not create path, status %#x\n", __func__,
		       status);
		return (1);
	}

	softc = malloc(sizeof(*softc), M_CTLFE, M_WAITOK | M_ZERO);
	xpt_path_lock(path);
	periph = cam_periph_find(path, "ctl");
	if (periph != NULL) {
		/* We've already got a periph, no need to alloc a new one. */
		xpt_path_unlock(path);
		xpt_free_path(path);
		free(softc, M_CTLFE);
		return (0);
	}
	softc->parent_softc = bus_softc;

	status = cam_periph_alloc(ctlferegister,
				  ctlfeoninvalidate,
				  ctlfecleanup,
				  ctlfestart,
				  "ctl",
				  CAM_PERIPH_BIO,
				  path,
				  ctlfeasync,
				  0,
				  softc);

	if ((status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		const struct cam_status_entry *entry;

		entry = cam_fetch_status_entry(status);
		printf("%s: CAM error %s (%#x) returned from "
		       "cam_periph_alloc()\n", __func__, (entry != NULL) ?
		       entry->status_text : "Unknown", status);
		free(softc, M_CTLFE);
	}

	xpt_path_unlock(path);
	xpt_free_path(path);
	return (0);
}

/*
 * This will get called when the user removes a LUN to disable that LUN
 * on every bus that is attached to CTL.  
 */
static int
ctlfe_lun_disable(void *arg, int lun_id)
{
	struct ctlfe_softc *softc;
	struct ctlfe_lun_softc *lun_softc;

	softc = (struct ctlfe_softc *)arg;
	if (softc->hba_misc & PIM_EXTLUNS)
		lun_id = CAM_EXTLUN_BYTE_SWIZZLE(ctl_encode_lun(lun_id));

	mtx_lock(&softc->lun_softc_mtx);
	STAILQ_FOREACH(lun_softc, &softc->lun_softc_list, links) {
		struct cam_path *path;

		path = lun_softc->periph->path;

		if ((xpt_path_target_id(path) == softc->target_id)
		 && (xpt_path_lun_id(path) == lun_id)) {
			break;
		}
	}
	if (lun_softc == NULL) {
		mtx_unlock(&softc->lun_softc_mtx);
		printf("%s: can't find lun %d\n", __func__, lun_id);
		return (1);
	}
	cam_periph_acquire(lun_softc->periph);
	mtx_unlock(&softc->lun_softc_mtx);

	cam_periph_lock(lun_softc->periph);
	cam_periph_invalidate(lun_softc->periph);
	cam_periph_unlock(lun_softc->periph);
	cam_periph_release(lun_softc->periph);
	return (0);
}

static void
ctlfe_dump_sim(struct cam_sim *sim)
{

	printf("%s%d: max dev openings: %d, max tagged dev openings: %d\n",
	    sim->sim_name, sim->unit_number, sim->max_dev_openings,
	    sim->max_tagged_dev_openings);
}

/*
 * Assumes that the SIM lock is held.
 */
static void
ctlfe_dump_queue(struct ctlfe_lun_softc *softc)
{
	struct cam_periph *periph = softc->periph;
	struct ccb_hdr *hdr;
	struct ccb_getdevstats cgds;
	int num_items;

	xpt_setup_ccb(&cgds.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
	cgds.ccb_h.func_code = XPT_GDEV_STATS;
	xpt_action((union ccb *)&cgds);
	if ((cgds.ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP) {
		xpt_print(periph->path, "devq: openings %d, active %d, "
		    "allocated %d, queued %d, held %d\n",
		    cgds.dev_openings, cgds.dev_active, cgds.allocated,
		    cgds.queued, cgds.held);
	}

	num_items = 0;

	STAILQ_FOREACH(hdr, &softc->work_queue, periph_links.stqe) {
		union ctl_io *io = hdr->io_ptr;

		num_items++;

		/*
		 * Only regular SCSI I/O is put on the work
		 * queue, so we can print sense here.  There may be no
		 * sense if it's no the queue for a DMA, but this serves to
		 * print out the CCB as well.
		 *
		 * XXX KDM switch this over to scsi_sense_print() when
		 * CTL is merged in with CAM.
		 */
		ctl_io_error_print(io, NULL);

		/*
		 * Print DMA status if we are DMA_QUEUED.
		 */
		if (io->io_hdr.flags & CTL_FLAG_DMA_QUEUED) {
			xpt_print(periph->path,
			    "Total %u, Current %u, Resid %u\n",
			    io->scsiio.kern_total_len,
			    io->scsiio.kern_data_len,
			    io->scsiio.kern_data_resid);
		}
	}

	xpt_print(periph->path, "%d requests waiting for CCBs\n", num_items);
	xpt_print(periph->path, "%d CTIOs outstanding\n", softc->ctios_sent);
}

/*
 * Datamove/done routine called by CTL.  Put ourselves on the queue to
 * receive a CCB from CAM so we can queue the continue I/O request down
 * to the adapter.
 */
static void
ctlfe_datamove(union ctl_io *io)
{
	union ccb *ccb;
	struct cam_periph *periph;
	struct ctlfe_lun_softc *softc;

	KASSERT(io->io_hdr.io_type == CTL_IO_SCSI,
	    ("Unexpected io_type (%d) in ctlfe_datamove", io->io_hdr.io_type));

	io->scsiio.ext_data_filled = 0;
	ccb = PRIV_CCB(io);
	periph = xpt_path_periph(ccb->ccb_h.path);
	cam_periph_lock(periph);
	softc = (struct ctlfe_lun_softc *)periph->softc;
	io->io_hdr.flags |= CTL_FLAG_DMA_QUEUED;
	if ((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE)
		io->io_hdr.flags |= CTL_FLAG_STATUS_QUEUED;
	STAILQ_INSERT_TAIL(&softc->work_queue, &ccb->ccb_h,
			  periph_links.stqe);
	xpt_schedule(periph, CAM_PRIORITY_NORMAL);
	cam_periph_unlock(periph);
}

static void
ctlfe_done(union ctl_io *io)
{
	union ccb *ccb;
	struct cam_periph *periph;
	struct ctlfe_lun_softc *softc;

	ccb = PRIV_CCB(io);
	periph = xpt_path_periph(ccb->ccb_h.path);
	cam_periph_lock(periph);
	softc = (struct ctlfe_lun_softc *)periph->softc;

	if (io->io_hdr.io_type == CTL_IO_TASK) {
		/*
		 * Send the notify acknowledge down to the SIM, to let it
		 * know we processed the task management command.
		 */
		ccb->ccb_h.status = CAM_REQ_INPROG;
		ccb->ccb_h.func_code = XPT_NOTIFY_ACKNOWLEDGE;
		switch (io->taskio.task_status) {
		case CTL_TASK_FUNCTION_COMPLETE:
			ccb->cna2.arg = CAM_RSP_TMF_COMPLETE;
			break;
		case CTL_TASK_FUNCTION_SUCCEEDED:
			ccb->cna2.arg = CAM_RSP_TMF_SUCCEEDED;
			ccb->ccb_h.flags |= CAM_SEND_STATUS;
			break;
		case CTL_TASK_FUNCTION_REJECTED:
			ccb->cna2.arg = CAM_RSP_TMF_REJECTED;
			ccb->ccb_h.flags |= CAM_SEND_STATUS;
			break;
		case CTL_TASK_LUN_DOES_NOT_EXIST:
			ccb->cna2.arg = CAM_RSP_TMF_INCORRECT_LUN;
			ccb->ccb_h.flags |= CAM_SEND_STATUS;
			break;
		case CTL_TASK_FUNCTION_NOT_SUPPORTED:
			ccb->cna2.arg = CAM_RSP_TMF_FAILED;
			ccb->ccb_h.flags |= CAM_SEND_STATUS;
			break;
		}
		ccb->cna2.arg |= scsi_3btoul(io->taskio.task_resp) << 8;
		xpt_action(ccb);
	} else if (io->io_hdr.flags & CTL_FLAG_STATUS_SENT) {
		ctlfe_requeue_ccb(periph, ccb, /* unlock */1);
		return;
	} else {
		io->io_hdr.flags |= CTL_FLAG_STATUS_QUEUED;
		STAILQ_INSERT_TAIL(&softc->work_queue, &ccb->ccb_h,
				  periph_links.stqe);
		xpt_schedule(periph, CAM_PRIORITY_NORMAL);
	}

	cam_periph_unlock(periph);
}

static void
ctlfe_dump(void)
{
	struct ctlfe_softc *bus_softc;
	struct ctlfe_lun_softc *lun_softc;

	STAILQ_FOREACH(bus_softc, &ctlfe_softc_list, links) {
		ctlfe_dump_sim(bus_softc->sim);
		STAILQ_FOREACH(lun_softc, &bus_softc->lun_softc_list, links)
			ctlfe_dump_queue(lun_softc);
	}
}
