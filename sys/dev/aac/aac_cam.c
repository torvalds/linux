/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Adaptec, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * CAM front-end for communicating with non-DASD devices
 */

#include "opt_aac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/disk.h>

#include <machine/md_var.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/aac/aacreg.h>
#include <sys/aac_ioctl.h>
#include <dev/aac/aacvar.h>

struct aac_cam {
	device_t		dev;
	struct aac_sim		*inf;
	struct cam_sim		*sim;
	struct cam_path		*path;
};

static int aac_cam_probe(device_t dev);
static int aac_cam_attach(device_t dev);
static int aac_cam_detach(device_t dev);
static void aac_cam_action(struct cam_sim *, union ccb *);
static void aac_cam_poll(struct cam_sim *);
static void aac_cam_complete(struct aac_command *);
static void aac_cam_rescan(struct aac_softc *sc, uint32_t channel,
    uint32_t target_id);

static u_int32_t aac_cam_reset_bus(struct cam_sim *, union ccb *);
static u_int32_t aac_cam_abort_ccb(struct cam_sim *, union ccb *);
static u_int32_t aac_cam_term_io(struct cam_sim *, union ccb *);

static devclass_t	aac_pass_devclass;

static device_method_t	aac_pass_methods[] = {
	DEVMETHOD(device_probe,		aac_cam_probe),
	DEVMETHOD(device_attach,	aac_cam_attach),
	DEVMETHOD(device_detach,	aac_cam_detach),
	DEVMETHOD_END
};

static driver_t	aac_pass_driver = {
	"aacp",
	aac_pass_methods,
	sizeof(struct aac_cam)
};

DRIVER_MODULE(aacp, aac, aac_pass_driver, aac_pass_devclass, NULL, NULL);
MODULE_DEPEND(aacp, cam, 1, 1, 1);

static MALLOC_DEFINE(M_AACCAM, "aaccam", "AAC CAM info");

static void
aac_cam_rescan(struct aac_softc *sc, uint32_t channel, uint32_t target_id)
{
	union ccb *ccb;
	struct aac_sim *sim;
	struct aac_cam *camsc;

	if (target_id == AAC_CAM_TARGET_WILDCARD)
		target_id = CAM_TARGET_WILDCARD;

	TAILQ_FOREACH(sim, &sc->aac_sim_tqh, sim_link) {
		camsc = sim->aac_cam;
		if (camsc == NULL || camsc->inf == NULL ||
		    camsc->inf->BusNumber != channel)
			continue;

		ccb = xpt_alloc_ccb_nowait();
		if (ccb == NULL) {
			device_printf(sc->aac_dev,
			    "Cannot allocate ccb for bus rescan.\n");
			return;
		}

		if (xpt_create_path(&ccb->ccb_h.path, NULL,
		    cam_sim_path(camsc->sim),
		    target_id, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
			xpt_free_ccb(ccb);
			device_printf(sc->aac_dev,
			    "Cannot create path for bus rescan.\n");
			return;
		}
		xpt_rescan(ccb);
		break;
	}
}


static void
aac_cam_event(struct aac_softc *sc, struct aac_event *event, void *arg)
{
	union ccb *ccb;
	struct aac_cam *camsc;

	switch (event->ev_type) {
	case AAC_EVENT_CMFREE:
		ccb = arg;
		camsc = ccb->ccb_h.sim_priv.entries[0].ptr;
		free(event, M_AACCAM);
		xpt_release_simq(camsc->sim, 1);
		ccb->ccb_h.status = CAM_REQUEUE_REQ;
		xpt_done(ccb);
		break;
	default:
		device_printf(sc->aac_dev, "unknown event %d in aac_cam\n",
		    event->ev_type);
		break;
	}
}

static int
aac_cam_probe(device_t dev)
{
	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	return (0);
}

static int
aac_cam_detach(device_t dev)
{
	struct aac_softc *sc;
	struct aac_cam *camsc;
	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	camsc = (struct aac_cam *)device_get_softc(dev);
	sc = camsc->inf->aac_sc;
	camsc->inf->aac_cam = NULL;

	mtx_lock(&sc->aac_io_lock);

	xpt_async(AC_LOST_DEVICE, camsc->path, NULL);
	xpt_free_path(camsc->path);
	xpt_bus_deregister(cam_sim_path(camsc->sim));
	cam_sim_free(camsc->sim, /*free_devq*/TRUE);

	sc->cam_rescan_cb = NULL;

	mtx_unlock(&sc->aac_io_lock);

	return (0);
}

/*
 * Register the driver as a CAM SIM
 */
static int
aac_cam_attach(device_t dev)
{
	struct cam_devq *devq;
	struct cam_sim *sim;
	struct cam_path *path;
	struct aac_cam *camsc;
	struct aac_sim *inf;

	fwprintf(NULL, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	camsc = (struct aac_cam *)device_get_softc(dev);
	inf = (struct aac_sim *)device_get_ivars(dev);
	camsc->inf = inf;
	camsc->inf->aac_cam = camsc;

	devq = cam_simq_alloc(inf->TargetsPerBus);
	if (devq == NULL)
		return (EIO);

	sim = cam_sim_alloc(aac_cam_action, aac_cam_poll, "aacp", camsc,
	    device_get_unit(dev), &inf->aac_sc->aac_io_lock, 1, 1, devq);
	if (sim == NULL) {
		cam_simq_free(devq);
		return (EIO);
	}

	/* Since every bus has it's own sim, every bus 'appears' as bus 0 */
	mtx_lock(&inf->aac_sc->aac_io_lock);
	if (xpt_bus_register(sim, dev, 0) != CAM_SUCCESS) {
		cam_sim_free(sim, TRUE);
		mtx_unlock(&inf->aac_sc->aac_io_lock);
		return (EIO);
	}

	if (xpt_create_path(&path, NULL, cam_sim_path(sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path(sim));
		cam_sim_free(sim, TRUE);
		mtx_unlock(&inf->aac_sc->aac_io_lock);
		return (EIO);
	}
	inf->aac_sc->cam_rescan_cb = aac_cam_rescan;
	mtx_unlock(&inf->aac_sc->aac_io_lock);

	camsc->sim = sim;
	camsc->path = path;

	return (0);
}

static void
aac_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct	aac_cam *camsc;
	struct	aac_softc *sc;
	struct	aac_srb *srb;
	struct	aac_fib *fib;
	struct	aac_command *cm;

	camsc = (struct aac_cam *)cam_sim_softc(sim);
	sc = camsc->inf->aac_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	/* Synchronous ops, and ops that don't require communication with the
	 * controller */
	switch(ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
	case XPT_RESET_DEV:
		/* These are handled down below */
		break;
	case XPT_CALC_GEOMETRY:
	{
		struct ccb_calc_geometry *ccg;
		u_int32_t size_mb;
		u_int32_t secs_per_cylinder;

		ccg = &ccb->ccg;
		size_mb = ccg->volume_size /
		    ((1024L * 1024L) / ccg->block_size);
		if (size_mb >= (2 * 1024)) {		/* 2GB */
			ccg->heads = 255;
			ccg->secs_per_track = 63;
		} else if (size_mb >= (1 * 1024)) {	/* 1GB */
			ccg->heads = 128;
			ccg->secs_per_track = 32;
		} else {
			ccg->heads = 64;
			ccg->secs_per_track = 32;
		}
		secs_per_cylinder = ccg->heads * ccg->secs_per_track;
		ccg->cylinders = ccg->volume_size / secs_per_cylinder;

		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->hba_inquiry = PI_WIDE_16;
		cpi->target_sprt = 0;

		/*
		 * Resetting via the passthrough or parallel bus scan
		 * causes problems.
		 */
		cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = camsc->inf->TargetsPerBus;
		cpi->max_lun = 8;	/* Per the controller spec */
		cpi->initiator_id = camsc->inf->InitiatorBusId;
		cpi->bus_id = camsc->inf->BusNumber;
		cpi->base_transfer_speed = 3300;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "Adaptec", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings_scsi *scsi =
			&ccb->cts.proto_specific.scsi;
		struct ccb_trans_settings_spi *spi =
			&ccb->cts.xport_specific.spi;
		ccb->cts.protocol = PROTO_SCSI;
		ccb->cts.protocol_version = SCSI_REV_2;
		ccb->cts.transport = XPORT_SPI;
		ccb->cts.transport_version = 2;
		if (ccb->ccb_h.target_lun != CAM_LUN_WILDCARD) {
			scsi->valid = CTS_SCSI_VALID_TQ;
			spi->valid |= CTS_SPI_VALID_DISC;
		} else {
			scsi->valid = 0;
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_SET_TRAN_SETTINGS:
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		xpt_done(ccb);
		return;
	case XPT_RESET_BUS:
		if (!(sc->flags & AAC_FLAGS_CAM_NORESET)) {
			ccb->ccb_h.status = aac_cam_reset_bus(sim, ccb);
		} else {
			ccb->ccb_h.status = CAM_REQ_CMP;
		}
		xpt_done(ccb);
		return;
	case XPT_ABORT:
		ccb->ccb_h.status = aac_cam_abort_ccb(sim, ccb);
		xpt_done(ccb);
		return;
	case XPT_TERM_IO:
		ccb->ccb_h.status = aac_cam_term_io(sim, ccb);
		xpt_done(ccb);
		return;
	default:
		device_printf(sc->aac_dev, "Unsupported command 0x%x\n",
		    ccb->ccb_h.func_code);
		ccb->ccb_h.status = CAM_PROVIDE_FAIL;
		xpt_done(ccb);
		return;
	}

	/* Async ops that require communcation with the controller */

	if (aac_alloc_command(sc, &cm)) {
		struct aac_event *event;

		xpt_freeze_simq(sim, 1);
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
		ccb->ccb_h.sim_priv.entries[0].ptr = camsc;
		event = malloc(sizeof(struct aac_event), M_AACCAM,
		    M_NOWAIT | M_ZERO);
		if (event == NULL) {
			device_printf(sc->aac_dev,
			    "Warning, out of memory for event\n");
			return;
		}
		event->ev_callback = aac_cam_event;
		event->ev_arg = ccb;
		event->ev_type = AAC_EVENT_CMFREE;
		aac_add_event(sc, event);
		return;
	}

	fib = cm->cm_fib;
	srb = (struct aac_srb *)&fib->data[0];
	cm->cm_datalen = 0;

	switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		srb->flags = AAC_SRB_FLAGS_DATA_IN;
		cm->cm_flags |= AAC_CMD_DATAIN;
		break;
	case CAM_DIR_OUT:
		srb->flags = AAC_SRB_FLAGS_DATA_OUT;
		cm->cm_flags |= AAC_CMD_DATAOUT;
		break;
	case CAM_DIR_NONE:
		srb->flags = AAC_SRB_FLAGS_NO_DATA_XFER;
		break;
	default:
		srb->flags = AAC_SRB_FLAGS_UNSPECIFIED_DIRECTION;
		cm->cm_flags |= AAC_CMD_DATAIN | AAC_CMD_DATAOUT;
		break;
	}

	switch(ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
	{
		struct ccb_scsiio *csio = &ccb->csio;

		srb->function = AAC_SRB_FUNC_EXECUTE_SCSI;

		/*
		 * Copy the CDB into the SRB.  It's only 6-16 bytes,
		 * so a copy is not too expensive.
		 */
		srb->cdb_len = csio->cdb_len;
		if (ccb->ccb_h.flags & CAM_CDB_POINTER)
			bcopy(csio->cdb_io.cdb_ptr, (u_int8_t *)&srb->cdb[0],
			    srb->cdb_len);
		else
			bcopy(csio->cdb_io.cdb_bytes, (u_int8_t *)&srb->cdb[0],
			    srb->cdb_len);

		/* Set command */
		fib->Header.Command = (sc->flags & AAC_FLAGS_SG_64BIT) ?
			ScsiPortCommandU64 : ScsiPortCommand;

		/* Map the s/g list. XXX 32bit addresses only! */
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
			switch ((ccb->ccb_h.flags & CAM_DATA_MASK)) {
			case CAM_DATA_VADDR:
				srb->data_len = csio->dxfer_len;
				/*
				 * Arrange things so that the S/G
				 * map will get set up automagically
				 */
				cm->cm_data = (void *)csio->data_ptr;
				cm->cm_datalen = csio->dxfer_len;
				cm->cm_sgtable = &srb->sg_map;
				break;
			case CAM_DATA_PADDR:
				/* Send a 32bit command */
				fib->Header.Command = ScsiPortCommand;
				srb->sg_map.SgCount = 1;
				srb->sg_map.SgEntry[0].SgAddress =
				    (uint32_t)(uintptr_t)csio->data_ptr;
				srb->sg_map.SgEntry[0].SgByteCount =
				    csio->dxfer_len;
				srb->data_len = csio->dxfer_len;
				break;
			default:
				/* XXX Need to handle multiple s/g elements */
				panic("aac_cam: multiple s/g elements");
			}
		} else {
			srb->sg_map.SgCount = 0;
			srb->sg_map.SgEntry[0].SgByteCount = 0;
			srb->data_len = 0;
		}

		break;
	}
	case XPT_RESET_DEV:
		if (!(sc->flags & AAC_FLAGS_CAM_NORESET)) {
			srb->function = AAC_SRB_FUNC_RESET_DEVICE;
			break;
		} else {
			ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
			return;
		}
	default:
		break;
	}

	srb->bus = camsc->inf->BusNumber; /* Bus number relative to the card */
	srb->target = ccb->ccb_h.target_id;
	srb->lun = ccb->ccb_h.target_lun;
	srb->timeout = ccb->ccb_h.timeout;	/* XXX */
	srb->retry_limit = 0;

	cm->cm_complete = aac_cam_complete;
	cm->cm_private = ccb;
	cm->cm_timestamp = time_uptime;

	fib->Header.XferState =
	    AAC_FIBSTATE_HOSTOWNED	|
	    AAC_FIBSTATE_INITIALISED	|
	    AAC_FIBSTATE_FROMHOST	|
	    AAC_FIBSTATE_REXPECTED	|
	    AAC_FIBSTATE_NORM;
	fib->Header.Size = sizeof(struct aac_fib_header) +
	    sizeof(struct aac_srb);

	aac_enqueue_ready(cm);
	aac_startio(cm->cm_sc);
}

static void
aac_cam_poll(struct cam_sim *sim)
{
	/*
	 * Pinging the interrupt routine isn't very safe, nor is it
	 * really necessary.  Do nothing.
	 */
}

static void
aac_cam_fix_inquiry(struct aac_softc *sc, union ccb *ccb)
{
	struct scsi_inquiry_data *inq;
	uint8_t *data;
	uint8_t device, qual;

	/* If this is an inquiry command, fake things out */
	if (ccb->ccb_h.flags & CAM_CDB_POINTER)
		data = ccb->csio.cdb_io.cdb_ptr;
	else
		data = ccb->csio.cdb_io.cdb_bytes;

	if (data[0] != INQUIRY)
		return;

	if (ccb->ccb_h.status == CAM_REQ_CMP) {
		inq = (struct scsi_inquiry_data *)ccb->csio.data_ptr;
		device = SID_TYPE(inq);
		qual = SID_QUAL(inq);

		/*
		 * We want DASD and PROC devices to only be
		 * visible through the pass device.
		 */
		if (((device == T_DIRECT) ||
		    (device == T_PROCESSOR) ||
		    (sc->flags & AAC_FLAGS_CAM_PASSONLY))) {
			/*
			 * Some aac(4) adapters will always report that a direct
			 * access device is offline in response to a INQUIRY
			 * command that does not retrieve vital product data.
			 * Force the qualifier to connected so that upper layers
			 * correctly recognize that a disk is present.
			 */
			if ((data[1] & SI_EVPD) == 0 && device == T_DIRECT &&
			    qual == SID_QUAL_LU_OFFLINE)
				qual = SID_QUAL_LU_CONNECTED;
			ccb->csio.data_ptr[0] = (qual << 5) | T_NODEVICE;
		}
	} else if (ccb->ccb_h.status == CAM_SEL_TIMEOUT &&
		ccb->ccb_h.target_lun != 0) {
		/* fix for INQUIRYs on Lun>0 */
		ccb->ccb_h.status = CAM_DEV_NOT_THERE;
	}
}

static void
aac_cam_complete(struct aac_command *cm)
{
	union	ccb *ccb;
	struct 	aac_srb_response *srbr;
	struct	aac_softc *sc;
	int	sense_returned;

	sc = cm->cm_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	ccb = cm->cm_private;
	srbr = (struct aac_srb_response *)&cm->cm_fib->data[0];

	if (srbr->fib_status != 0) {
		device_printf(sc->aac_dev, "Passthru FIB failed!\n");
		ccb->ccb_h.status = CAM_REQ_ABORTED;
	} else {
		/*
		 * The SRB error codes just happen to match the CAM error
		 * codes.  How convenient!
		 */
		ccb->ccb_h.status = srbr->srb_status;

		/* Take care of SCSI_IO ops. */
		if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
			ccb->csio.scsi_status = srbr->scsi_status;

			/* Take care of autosense */
			if (srbr->sense_len) {
				sense_returned = srbr->sense_len;
				if (sense_returned < ccb->csio.sense_len)
					ccb->csio.sense_resid =
					   ccb->csio.sense_len -
					   sense_returned;
					else
					    ccb->csio.sense_resid = 0;
				bzero(&ccb->csio.sense_data,
				    sizeof(struct scsi_sense_data));
				bcopy(&srbr->sense[0], &ccb->csio.sense_data,
				    min(ccb->csio.sense_len, sense_returned));
				ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
				// scsi_sense_print(&ccb->csio);
			}

			aac_cam_fix_inquiry(sc, ccb);
		}
	}

	aac_release_command(cm);
	xpt_done(ccb);
}

static u_int32_t
aac_cam_reset_bus(struct cam_sim *sim, union ccb *ccb)
{
	struct aac_fib *fib;
	struct aac_softc *sc;
	struct aac_cam *camsc;
	struct aac_vmioctl *vmi;
	struct aac_resetbus *rbc;
	int e;

	camsc = (struct aac_cam *)cam_sim_softc(sim);
	sc = camsc->inf->aac_sc;

	if (sc == NULL) {
		printf("aac: Null sc?\n");
		return (CAM_REQ_ABORTED);
	}

	aac_alloc_sync_fib(sc, &fib);

	vmi = (struct aac_vmioctl *)&fib->data[0];
	bzero(vmi, sizeof(struct aac_vmioctl));

	vmi->Command = VM_Ioctl;
	vmi->ObjType = FT_DRIVE;
	vmi->MethId = sc->scsi_method_id;
	vmi->ObjId = 0;
	vmi->IoctlCmd = ResetBus;

	rbc = (struct aac_resetbus *)&vmi->IoctlBuf[0];
	rbc->BusNumber = camsc->inf->BusNumber;

	e = aac_sync_fib(sc, ContainerCommand, 0, fib,
	    sizeof(struct aac_vmioctl));
	if (e) {
		device_printf(sc->aac_dev,"Error %d sending ResetBus command\n",
		    e);
		aac_release_sync_fib(sc);
		return (CAM_REQ_ABORTED);
	}

	aac_release_sync_fib(sc);
	return (CAM_REQ_CMP);
}

static u_int32_t
aac_cam_abort_ccb(struct cam_sim *sim, union ccb *ccb)
{
	return (CAM_UA_ABORT);
}

static u_int32_t
aac_cam_term_io(struct cam_sim *sim, union ccb *ccb)
{
	return (CAM_UA_TERMIO);
}
