/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/cons.h>
#include <geom/geom_disk.h>
#endif /* _KERNEL */

#ifndef _KERNEL
#include <stdio.h>
#include <string.h>
#endif /* _KERNEL */

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_xpt_internal.h>
#include <cam/cam_sim.h>

#include <cam/ata/ata_all.h>

#ifdef _KERNEL

typedef enum {
	PMP_STATE_NORMAL,
	PMP_STATE_PORTS,
	PMP_STATE_PM_QUIRKS_1,
	PMP_STATE_PM_QUIRKS_2,
	PMP_STATE_PM_QUIRKS_3,
	PMP_STATE_PRECONFIG,
	PMP_STATE_RESET,
	PMP_STATE_CONNECT,
	PMP_STATE_CHECK,
	PMP_STATE_CLEAR,
	PMP_STATE_CONFIG,
	PMP_STATE_SCAN
} pmp_state;

typedef enum {
	PMP_FLAG_SCTX_INIT	= 0x200
} pmp_flags;

typedef enum {
	PMP_CCB_PROBE		= 0x01,
} pmp_ccb_state;

/* Offsets into our private area for storing information */
#define ccb_state	ppriv_field0
#define ccb_bp		ppriv_ptr1

struct pmp_softc {
	SLIST_ENTRY(pmp_softc)	links;
	pmp_state		state;
	pmp_flags		flags;
	uint32_t		pm_pid;
	uint32_t		pm_prv;
	int			pm_ports;
	int			pm_step;
	int			pm_try;
	int			found;
	int			reset;
	int			frozen;
	int			restart;
	int			events;
#define PMP_EV_RESET	1
#define PMP_EV_RESCAN	2
	u_int			caps;
	struct task		sysctl_task;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};

static	periph_init_t	pmpinit;
static	void		pmpasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	void		pmpsysctlinit(void *context, int pending);
static	periph_ctor_t	pmpregister;
static	periph_dtor_t	pmpcleanup;
static	periph_start_t	pmpstart;
static	periph_oninv_t	pmponinvalidate;
static	void		pmpdone(struct cam_periph *periph,
			       union ccb *done_ccb);

#ifndef PMP_DEFAULT_TIMEOUT
#define PMP_DEFAULT_TIMEOUT 30	/* Timeout in seconds */
#endif

#ifndef	PMP_DEFAULT_RETRY
#define	PMP_DEFAULT_RETRY	1
#endif

#ifndef	PMP_DEFAULT_HIDE_SPECIAL
#define	PMP_DEFAULT_HIDE_SPECIAL	1
#endif

static int pmp_retry_count = PMP_DEFAULT_RETRY;
static int pmp_default_timeout = PMP_DEFAULT_TIMEOUT;
static int pmp_hide_special = PMP_DEFAULT_HIDE_SPECIAL;

static SYSCTL_NODE(_kern_cam, OID_AUTO, pmp, CTLFLAG_RD, 0,
            "CAM Direct Access Disk driver");
SYSCTL_INT(_kern_cam_pmp, OID_AUTO, retry_count, CTLFLAG_RWTUN,
           &pmp_retry_count, 0, "Normal I/O retry count");
SYSCTL_INT(_kern_cam_pmp, OID_AUTO, default_timeout, CTLFLAG_RWTUN,
           &pmp_default_timeout, 0, "Normal I/O timeout (in seconds)");
SYSCTL_INT(_kern_cam_pmp, OID_AUTO, hide_special, CTLFLAG_RWTUN,
           &pmp_hide_special, 0, "Hide extra ports");

static struct periph_driver pmpdriver =
{
	pmpinit, "pmp",
	TAILQ_HEAD_INITIALIZER(pmpdriver.units), /* generation */ 0,
	CAM_PERIPH_DRV_EARLY
};

PERIPHDRIVER_DECLARE(pmp, pmpdriver);

static void
pmpinit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, pmpasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("pmp: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}
}

static void
pmpfreeze(struct cam_periph *periph, int mask)
{
	struct pmp_softc *softc = (struct pmp_softc *)periph->softc;
	struct cam_path *dpath;
	int i;

	mask &= ~softc->frozen;
	for (i = 0; i < 15; i++) {
		if ((mask & (1 << i)) == 0)
			continue;
		if (xpt_create_path(&dpath, periph,
		    xpt_path_path_id(periph->path),
		    i, 0) == CAM_REQ_CMP) {
			softc->frozen |= (1 << i);
			xpt_acquire_device(dpath->device);
			cam_freeze_devq(dpath);
			xpt_free_path(dpath);
		}
	}
}

static void
pmprelease(struct cam_periph *periph, int mask)
{
	struct pmp_softc *softc = (struct pmp_softc *)periph->softc;
	struct cam_path *dpath;
	int i;

	mask &= softc->frozen;
	for (i = 0; i < 15; i++) {
		if ((mask & (1 << i)) == 0)
			continue;
		if (xpt_create_path(&dpath, periph,
		    xpt_path_path_id(periph->path),
		    i, 0) == CAM_REQ_CMP) {
			softc->frozen &= ~(1 << i);
			cam_release_devq(dpath, 0, 0, 0, FALSE);
			xpt_release_device(dpath->device);
			xpt_free_path(dpath);
		}
	}
}

static void
pmponinvalidate(struct cam_periph *periph)
{
	struct cam_path *dpath;
	int i;

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, pmpasync, periph, periph->path);

	for (i = 0; i < 15; i++) {
		if (xpt_create_path(&dpath, periph,
		    xpt_path_path_id(periph->path),
		    i, 0) == CAM_REQ_CMP) {
			xpt_async(AC_LOST_DEVICE, dpath, NULL);
			xpt_free_path(dpath);
		}
	}
	pmprelease(periph, -1);
}

static void
pmpcleanup(struct cam_periph *periph)
{
	struct pmp_softc *softc;

	softc = (struct pmp_softc *)periph->softc;

	cam_periph_unlock(periph);

	/*
	 * If we can't free the sysctl tree, oh well...
	 */
	if ((softc->flags & PMP_FLAG_SCTX_INIT) != 0
	    && sysctl_ctx_free(&softc->sysctl_ctx) != 0) {
		xpt_print(periph->path, "can't remove sysctl context\n");
	}

	free(softc, M_DEVBUF);
	cam_periph_lock(periph);
}

static void
pmpasync(void *callback_arg, u_int32_t code,
	struct cam_path *path, void *arg)
{
	struct cam_periph *periph;
	struct pmp_softc *softc;

	periph = (struct cam_periph *)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
	{
		struct ccb_getdev *cgd;
		cam_status status;
 
		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL)
			break;

		if (cgd->protocol != PROTO_SATAPM)
			break;

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(pmpregister, pmponinvalidate,
					  pmpcleanup, pmpstart,
					  "pmp", CAM_PERIPH_BIO,
					  path, pmpasync,
					  AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("pmpasync: Unable to attach to new device "
				"due to status 0x%x\n", status);
		break;
	}
	case AC_SCSI_AEN:
	case AC_SENT_BDR:
	case AC_BUS_RESET:
		softc = (struct pmp_softc *)periph->softc;
		cam_periph_async(periph, code, path, arg);
		if (code == AC_SCSI_AEN)
			softc->events |= PMP_EV_RESCAN;
		else
			softc->events |= PMP_EV_RESET;
		if (code == AC_SCSI_AEN && softc->state != PMP_STATE_NORMAL)
			break;
		xpt_hold_boot();
		pmpfreeze(periph, softc->found);
		if (code == AC_SENT_BDR || code == AC_BUS_RESET)
			softc->found = 0; /* We have to reset everything. */
		if (softc->state == PMP_STATE_NORMAL) {
			if (cam_periph_acquire(periph) == 0) {
				if (softc->pm_pid == 0x37261095 ||
				    softc->pm_pid == 0x38261095)
					softc->state = PMP_STATE_PM_QUIRKS_1;
				else
					softc->state = PMP_STATE_PRECONFIG;
				xpt_schedule(periph, CAM_PRIORITY_DEV);
			} else {
				pmprelease(periph, softc->found);
				xpt_release_boot();
			}
		} else
			softc->restart = 1;
		break;
	default:
		cam_periph_async(periph, code, path, arg);
		break;
	}
}

static void
pmpsysctlinit(void *context, int pending)
{
	struct cam_periph *periph;
	struct pmp_softc *softc;
	char tmpstr[32], tmpstr2[16];

	periph = (struct cam_periph *)context;
	if (cam_periph_acquire(periph) != 0)
		return;

	softc = (struct pmp_softc *)periph->softc;
	snprintf(tmpstr, sizeof(tmpstr), "CAM PMP unit %d", periph->unit_number);
	snprintf(tmpstr2, sizeof(tmpstr2), "%d", periph->unit_number);

	sysctl_ctx_init(&softc->sysctl_ctx);
	softc->flags |= PMP_FLAG_SCTX_INIT;
	softc->sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&softc->sysctl_ctx,
		SYSCTL_STATIC_CHILDREN(_kern_cam_pmp), OID_AUTO, tmpstr2,
		CTLFLAG_RD, 0, tmpstr, "device_index");
	if (softc->sysctl_tree == NULL) {
		printf("pmpsysctlinit: unable to allocate sysctl tree\n");
		cam_periph_release(periph);
		return;
	}

	cam_periph_release(periph);
}

static cam_status
pmpregister(struct cam_periph *periph, void *arg)
{
	struct pmp_softc *softc;
	struct ccb_getdev *cgd;

	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("pmpregister: no getdev CCB, can't register device\n");
		return(CAM_REQ_CMP_ERR);
	}

	softc = (struct pmp_softc *)malloc(sizeof(*softc), M_DEVBUF,
	    M_NOWAIT|M_ZERO);

	if (softc == NULL) {
		printf("pmpregister: Unable to probe new device. "
		       "Unable to allocate softc\n");				
		return(CAM_REQ_CMP_ERR);
	}
	periph->softc = softc;

	softc->pm_pid = ((uint32_t *)&cgd->ident_data)[0];
	softc->pm_prv = ((uint32_t *)&cgd->ident_data)[1];
	TASK_INIT(&softc->sysctl_task, 0, pmpsysctlinit, periph);

	xpt_announce_periph(periph, NULL);

	/*
	 * Add async callbacks for bus reset and
	 * bus device reset calls.  I don't bother
	 * checking if this fails as, in most cases,
	 * the system will function just fine without
	 * them and the only alternative would be to
	 * not attach the device on failure.
	 */
	xpt_register_async(AC_SENT_BDR | AC_BUS_RESET | AC_LOST_DEVICE |
		AC_SCSI_AEN, pmpasync, periph, periph->path);

	/*
	 * Take an exclusive refcount on the periph while pmpstart is called
	 * to finish the probe.  The reference will be dropped in pmpdone at
	 * the end of probe.
	 */
	(void)cam_periph_acquire(periph);
	xpt_hold_boot();
	softc->state = PMP_STATE_PORTS;
	softc->events = PMP_EV_RESCAN;
	xpt_schedule(periph, CAM_PRIORITY_DEV);

	return(CAM_REQ_CMP);
}

static void
pmpstart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct ccb_trans_settings cts;
	struct ccb_ataio *ataio;
	struct pmp_softc *softc;
	struct cam_path *dpath;
	int revision = 0;

	softc = (struct pmp_softc *)periph->softc;
	ataio = &start_ccb->ataio;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("pmpstart\n"));

	if (softc->restart) {
		softc->restart = 0;
		if (softc->pm_pid == 0x37261095 || softc->pm_pid == 0x38261095)
			softc->state = min(softc->state, PMP_STATE_PM_QUIRKS_1);
		else
			softc->state = min(softc->state, PMP_STATE_PRECONFIG);
	}
	/* Fetch user wanted device speed. */
	if (softc->state == PMP_STATE_RESET ||
	    softc->state == PMP_STATE_CONNECT) {
		if (xpt_create_path(&dpath, periph,
		    xpt_path_path_id(periph->path),
		    softc->pm_step, 0) == CAM_REQ_CMP) {
			bzero(&cts, sizeof(cts));
			xpt_setup_ccb(&cts.ccb_h, dpath, CAM_PRIORITY_NONE);
			cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
			cts.type = CTS_TYPE_USER_SETTINGS;
			xpt_action((union ccb *)&cts);
			if (cts.xport_specific.sata.valid & CTS_SATA_VALID_REVISION)
				revision = cts.xport_specific.sata.revision;
			xpt_free_path(dpath);
		}
	}
	switch (softc->state) {
	case PMP_STATE_PORTS:
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_read_cmd(ataio, 2, 15);
		break;

	case PMP_STATE_PM_QUIRKS_1:
	case PMP_STATE_PM_QUIRKS_3:
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_read_cmd(ataio, 129, 15);
		break;

	case PMP_STATE_PM_QUIRKS_2:
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_write_cmd(ataio, 129, 15, softc->caps & ~0x1);
		break;

	case PMP_STATE_PRECONFIG:
		/* Get/update host SATA capabilities. */
		bzero(&cts, sizeof(cts));
		xpt_setup_ccb(&cts.ccb_h, periph->path, CAM_PRIORITY_NONE);
		cts.ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		xpt_action((union ccb *)&cts);
		if (cts.xport_specific.sata.valid & CTS_SATA_VALID_CAPS)
			softc->caps = cts.xport_specific.sata.caps;
		else
			softc->caps = 0;
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_write_cmd(ataio, 0x60, 15, 0x0);
		break;
	case PMP_STATE_RESET:
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_write_cmd(ataio, 2, softc->pm_step,
		    (revision << 4) |
		    ((softc->found & (1 << softc->pm_step)) ? 0 : 1));
		break;
	case PMP_STATE_CONNECT:
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_write_cmd(ataio, 2, softc->pm_step,
		    (revision << 4));
		break;
	case PMP_STATE_CHECK:
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_read_cmd(ataio, 0, softc->pm_step);
		break;
	case PMP_STATE_CLEAR:
		softc->reset = 0;
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_write_cmd(ataio, 1, softc->pm_step, 0xFFFFFFFF);
		break;
	case PMP_STATE_CONFIG:
		cam_fill_ataio(ataio,
		      pmp_retry_count,
		      pmpdone,
		      /*flags*/CAM_DIR_NONE,
		      0,
		      /*data_ptr*/NULL,
		      /*dxfer_len*/0,
		      pmp_default_timeout * 1000);
		ata_pm_write_cmd(ataio, 0x60, 15, 0x07 |
		    ((softc->caps & CTS_SATA_CAPS_H_AN) ? 0x08 : 0));
		break;
	default:
		break;
	}
	xpt_action(start_ccb);
}

static void
pmpdone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct ccb_trans_settings cts;
	struct pmp_softc *softc;
	struct ccb_ataio *ataio;
	struct cam_path *dpath;
	u_int32_t  priority, res;
	int i;

	softc = (struct pmp_softc *)periph->softc;
	ataio = &done_ccb->ataio;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("pmpdone\n"));

	priority = done_ccb->ccb_h.pinfo.priority;

	if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (cam_periph_error(done_ccb, 0, 0) == ERESTART) {
			return;
		} else if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
			cam_release_devq(done_ccb->ccb_h.path,
			    /*relsim_flags*/0,
			    /*reduction*/0,
			    /*timeout*/0,
			    /*getcount_only*/0);
		}
		goto done;
	}

	if (softc->restart) {
		softc->restart = 0;
		xpt_release_ccb(done_ccb);
		if (softc->pm_pid == 0x37261095 || softc->pm_pid == 0x38261095)
			softc->state = min(softc->state, PMP_STATE_PM_QUIRKS_1);
		else
			softc->state = min(softc->state, PMP_STATE_PRECONFIG);
		xpt_schedule(periph, priority);
		return;
	}

	switch (softc->state) {
	case PMP_STATE_PORTS:
		softc->pm_ports = (ataio->res.lba_high << 24) +
		    (ataio->res.lba_mid << 16) +
		    (ataio->res.lba_low << 8) +
		    ataio->res.sector_count;
		if (pmp_hide_special) {
			/*
			 * This PMP declares 6 ports, while only 5 of them
			 * are real. Port 5 is a SEMB port, probing which
			 * causes timeouts if external SEP is not connected
			 * to PMP over I2C.
			 */
			if ((softc->pm_pid == 0x37261095 ||
			     softc->pm_pid == 0x38261095) &&
			    softc->pm_ports == 6)
				softc->pm_ports = 5;

			/*
			 * This PMP declares 7 ports, while only 5 of them
			 * are real. Port 5 is a fake "Config  Disk" with
			 * 640 sectors size. Port 6 is a SEMB port.
			 */
			if (softc->pm_pid == 0x47261095 && softc->pm_ports == 7)
				softc->pm_ports = 5;

			/*
			 * These PMPs have extra configuration port.
			 */
			if (softc->pm_pid == 0x57231095 ||
			    softc->pm_pid == 0x57331095 ||
			    softc->pm_pid == 0x57341095 ||
			    softc->pm_pid == 0x57441095)
				softc->pm_ports--;
		}
		printf("%s%d: %d fan-out ports\n",
		    periph->periph_name, periph->unit_number,
		    softc->pm_ports);
		if (softc->pm_pid == 0x37261095 || softc->pm_pid == 0x38261095)
			softc->state = PMP_STATE_PM_QUIRKS_1;
		else
			softc->state = PMP_STATE_PRECONFIG;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;

	case PMP_STATE_PM_QUIRKS_1:
		softc->caps = (ataio->res.lba_high << 24) +
		    (ataio->res.lba_mid << 16) +
		    (ataio->res.lba_low << 8) +
		    ataio->res.sector_count;
		if (softc->caps & 0x1)
			softc->state = PMP_STATE_PM_QUIRKS_2;
		else
			softc->state = PMP_STATE_PRECONFIG;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;

	case PMP_STATE_PM_QUIRKS_2:
		if (bootverbose)
			softc->state = PMP_STATE_PM_QUIRKS_3;
		else
			softc->state = PMP_STATE_PRECONFIG;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;

	case PMP_STATE_PM_QUIRKS_3:
		res = (ataio->res.lba_high << 24) +
		    (ataio->res.lba_mid << 16) +
		    (ataio->res.lba_low << 8) +
		    ataio->res.sector_count;
		printf("%s%d: Disabling SiI3x26 R_OK in GSCR_POLL: %x->%x\n",
		    periph->periph_name, periph->unit_number, softc->caps, res);
		softc->state = PMP_STATE_PRECONFIG;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;

	case PMP_STATE_PRECONFIG:
		softc->pm_step = 0;
		softc->state = PMP_STATE_RESET;
		softc->reset |= ~softc->found;
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	case PMP_STATE_RESET:
		softc->pm_step++;
		if (softc->pm_step >= softc->pm_ports) {
			softc->pm_step = 0;
			cam_freeze_devq(periph->path);
			cam_release_devq(periph->path,
			    RELSIM_RELEASE_AFTER_TIMEOUT,
			    /*reduction*/0,
			    /*timeout*/5,
			    /*getcount_only*/0);
			softc->state = PMP_STATE_CONNECT;
		}
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	case PMP_STATE_CONNECT:
		softc->pm_step++;
		if (softc->pm_step >= softc->pm_ports) {
			softc->pm_step = 0;
			softc->pm_try = 0;
			cam_freeze_devq(periph->path);
			cam_release_devq(periph->path,
			    RELSIM_RELEASE_AFTER_TIMEOUT,
			    /*reduction*/0,
			    /*timeout*/10,
			    /*getcount_only*/0);
			softc->state = PMP_STATE_CHECK;
		}
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	case PMP_STATE_CHECK:
		res = (ataio->res.lba_high << 24) +
		    (ataio->res.lba_mid << 16) +
		    (ataio->res.lba_low << 8) +
		    ataio->res.sector_count;
		if (((res & 0xf0f) == 0x103 && (res & 0x0f0) != 0) ||
		    (res & 0x600) != 0) {
			if (bootverbose) {
				printf("%s%d: port %d status: %08x\n",
				    periph->periph_name, periph->unit_number,
				    softc->pm_step, res);
			}
			/* Report device speed if it is online. */
			if ((res & 0xf0f) == 0x103 &&
			    xpt_create_path(&dpath, periph,
			    xpt_path_path_id(periph->path),
			    softc->pm_step, 0) == CAM_REQ_CMP) {
				bzero(&cts, sizeof(cts));
				xpt_setup_ccb(&cts.ccb_h, dpath, CAM_PRIORITY_NONE);
				cts.ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
				cts.type = CTS_TYPE_CURRENT_SETTINGS;
				cts.xport_specific.sata.revision = (res & 0x0f0) >> 4;
				cts.xport_specific.sata.valid = CTS_SATA_VALID_REVISION;
				cts.xport_specific.sata.caps = softc->caps &
				    (CTS_SATA_CAPS_H_PMREQ |
				     CTS_SATA_CAPS_H_DMAAA |
				     CTS_SATA_CAPS_H_AN);
				cts.xport_specific.sata.valid |= CTS_SATA_VALID_CAPS;
				xpt_action((union ccb *)&cts);
				xpt_free_path(dpath);
			}
			softc->found |= (1 << softc->pm_step);
			softc->pm_step++;
		} else {
			if (softc->pm_try < 10) {
				cam_freeze_devq(periph->path);
				cam_release_devq(periph->path,
				    RELSIM_RELEASE_AFTER_TIMEOUT,
				    /*reduction*/0,
				    /*timeout*/10,
				    /*getcount_only*/0);
				softc->pm_try++;
			} else {
				if (bootverbose) {
					printf("%s%d: port %d status: %08x\n",
					    periph->periph_name, periph->unit_number,
					    softc->pm_step, res);
				}
				softc->found &= ~(1 << softc->pm_step);
				if (xpt_create_path(&dpath, periph,
				    done_ccb->ccb_h.path_id,
				    softc->pm_step, 0) == CAM_REQ_CMP) {
					xpt_async(AC_LOST_DEVICE, dpath, NULL);
					xpt_free_path(dpath);
				}
				softc->pm_step++;
			}
		}
		if (softc->pm_step >= softc->pm_ports) {
			if (softc->reset & softc->found) {
				cam_freeze_devq(periph->path);
				cam_release_devq(periph->path,
				    RELSIM_RELEASE_AFTER_TIMEOUT,
				    /*reduction*/0,
				    /*timeout*/1000,
				    /*getcount_only*/0);
			}
			softc->state = PMP_STATE_CLEAR;
			softc->pm_step = 0;
		}
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	case PMP_STATE_CLEAR:
		softc->pm_step++;
		if (softc->pm_step >= softc->pm_ports) {
			softc->state = PMP_STATE_CONFIG;
			softc->pm_step = 0;
		}
		xpt_release_ccb(done_ccb);
		xpt_schedule(periph, priority);
		return;
	case PMP_STATE_CONFIG:
		for (i = 0; i < softc->pm_ports; i++) {
			union ccb *ccb;

			if ((softc->found & (1 << i)) == 0)
				continue;
			if (xpt_create_path(&dpath, periph,
			    xpt_path_path_id(periph->path),
			    i, 0) != CAM_REQ_CMP) {
				printf("pmpdone: xpt_create_path failed\n");
				continue;
			}
			/* If we did hard reset to this device, inform XPT. */
			if ((softc->reset & softc->found & (1 << i)) != 0)
				xpt_async(AC_SENT_BDR, dpath, NULL);
			/* If rescan requested, scan this device. */
			if (softc->events & PMP_EV_RESCAN) {
				ccb = xpt_alloc_ccb_nowait();
				if (ccb == NULL) {
					xpt_free_path(dpath);
					goto done;
				}
				xpt_setup_ccb(&ccb->ccb_h, dpath, CAM_PRIORITY_XPT);
				xpt_rescan(ccb);
			} else
				xpt_free_path(dpath);
		}
		break;
	default:
		break;
	}
done:
	xpt_release_ccb(done_ccb);
	softc->state = PMP_STATE_NORMAL;
	softc->events = 0;
	xpt_release_boot();
	pmprelease(periph, -1);
	cam_periph_release_locked(periph);
}

#endif /* _KERNEL */
