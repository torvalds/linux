/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2010 Adaptec, Inc.
 * Copyright (c) 2010-2012 PMC-Sierra, Inc.
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

#include "opt_aacraid.h"

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
#if __FreeBSD_version < 801000
#include <cam/cam_xpt_periph.h>
#endif
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

#include <dev/aacraid/aacraid_reg.h>
#include <sys/aac_ioctl.h>
#include <dev/aacraid/aacraid_debug.h>
#include <dev/aacraid/aacraid_var.h>

#if __FreeBSD_version >= 700025
#ifndef	CAM_NEW_TRAN_CODE
#define	CAM_NEW_TRAN_CODE	1
#endif
#endif

#ifndef SVPD_SUPPORTED_PAGE_LIST
struct scsi_vpd_supported_page_list
{
	u_int8_t device;
	u_int8_t page_code;
#define	SVPD_SUPPORTED_PAGE_LIST 0x00
	u_int8_t reserved;
	u_int8_t length;	/* number of VPD entries */
#define	SVPD_SUPPORTED_PAGES_SIZE	251
	u_int8_t list[SVPD_SUPPORTED_PAGES_SIZE];
};
#endif

/************************** Version Compatibility *************************/
#if	__FreeBSD_version < 700031
#define	aac_sim_alloc(a,b,c,d,e,f,g,h,i)	cam_sim_alloc(a,b,c,d,e,g,h,i)
#else
#define	aac_sim_alloc				cam_sim_alloc
#endif

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
static void aac_container_complete(struct aac_command *);
#if __FreeBSD_version >= 700000
static void aac_cam_rescan(struct aac_softc *sc, uint32_t channel,
	uint32_t target_id);
#endif
static void aac_set_scsi_error(struct aac_softc *sc, union ccb *ccb, 
	u_int8_t status, u_int8_t key, u_int8_t asc, u_int8_t ascq);
static int aac_load_map_command_sg(struct aac_softc *, struct aac_command *);
static u_int64_t aac_eval_blockno(u_int8_t *);
static void aac_container_rw_command(struct cam_sim *, union ccb *, u_int8_t *);
static void aac_container_special_command(struct cam_sim *, union ccb *, 
	u_int8_t *);
static void aac_passthrough_command(struct cam_sim *, union ccb *);

static u_int32_t aac_cam_reset_bus(struct cam_sim *, union ccb *);
static u_int32_t aac_cam_abort_ccb(struct cam_sim *, union ccb *);
static u_int32_t aac_cam_term_io(struct cam_sim *, union ccb *);

static devclass_t	aacraid_pass_devclass;

static device_method_t	aacraid_pass_methods[] = {
	DEVMETHOD(device_probe,		aac_cam_probe),
	DEVMETHOD(device_attach,	aac_cam_attach),
	DEVMETHOD(device_detach,	aac_cam_detach),
	{ 0, 0 }
};

static driver_t	aacraid_pass_driver = {
	"aacraidp",
	aacraid_pass_methods,
	sizeof(struct aac_cam)
};

DRIVER_MODULE(aacraidp, aacraid, aacraid_pass_driver, aacraid_pass_devclass, 0, 0);
MODULE_DEPEND(aacraidp, cam, 1, 1, 1);

MALLOC_DEFINE(M_AACRAIDCAM, "aacraidcam", "AACRAID CAM info");

static void
aac_set_scsi_error(struct aac_softc *sc, union ccb *ccb, u_int8_t status, 
	u_int8_t key, u_int8_t asc, u_int8_t ascq)
{
#if __FreeBSD_version >= 900000
	struct scsi_sense_data_fixed *sense = 
		(struct scsi_sense_data_fixed *)&ccb->csio.sense_data;
#else
	struct scsi_sense_data *sense = &ccb->csio.sense_data;
#endif

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "Error %d!", status);

	ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
	ccb->csio.scsi_status = status;
	if (status == SCSI_STATUS_CHECK_COND) {
		ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
		bzero(&ccb->csio.sense_data, ccb->csio.sense_len);
		ccb->csio.sense_data.error_code = 
			SSD_CURRENT_ERROR | SSD_ERRCODE_VALID;
		sense->flags = key;
		if (ccb->csio.sense_len >= 14) {
			sense->extra_len = 6;
			sense->add_sense_code = asc;
			sense->add_sense_code_qual = ascq;
		}
	}
}

#if __FreeBSD_version >= 700000
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

		if (xpt_create_path(&ccb->ccb_h.path, xpt_periph,
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
#endif

static void
aac_cam_event(struct aac_softc *sc, struct aac_event *event, void *arg)
{
	union ccb *ccb;
	struct aac_cam *camsc;

	switch (event->ev_type) {
	case AAC_EVENT_CMFREE:
		ccb = arg;
		camsc = ccb->ccb_h.sim_priv.entries[0].ptr;
		free(event, M_AACRAIDCAM);
		xpt_release_simq(camsc->sim, 1);
		ccb->ccb_h.status = CAM_REQUEUE_REQ;
		xpt_done(ccb);
		break;
	default:
		device_printf(sc->aac_dev, "unknown event %d in aac_cam\n",
		    event->ev_type);
		break;
	}

	return;
}

static int
aac_cam_probe(device_t dev)
{
	struct aac_cam *camsc;

	camsc = (struct aac_cam *)device_get_softc(dev);
	if (!camsc->inf)
		return (0);
	fwprintf(camsc->inf->aac_sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	return (0);
}

static int
aac_cam_detach(device_t dev)
{
	struct aac_softc *sc;
	struct aac_cam *camsc;

	camsc = (struct aac_cam *)device_get_softc(dev);
	if (!camsc->inf) 
		return (0);
	sc = camsc->inf->aac_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
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

	camsc = (struct aac_cam *)device_get_softc(dev);
	inf = (struct aac_sim *)device_get_ivars(dev);
	if (!inf)
		return (EIO);
	fwprintf(inf->aac_sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	camsc->inf = inf;
	camsc->inf->aac_cam = camsc;

	devq = cam_simq_alloc(inf->TargetsPerBus);
	if (devq == NULL)
		return (EIO);

	sim = aac_sim_alloc(aac_cam_action, aac_cam_poll, "aacraidp", camsc,
	    device_get_unit(dev), &inf->aac_sc->aac_io_lock, 1, 1, devq);
	if (sim == NULL) {
		cam_simq_free(devq);
		return (EIO);
	}

	/* Since every bus has it's own sim, every bus 'appears' as bus 0 */
	mtx_lock(&inf->aac_sc->aac_io_lock);
	if (aac_xpt_bus_register(sim, dev, 0) != CAM_SUCCESS) {
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

#if __FreeBSD_version >= 700000
	inf->aac_sc->cam_rescan_cb = aac_cam_rescan;
#endif
	mtx_unlock(&inf->aac_sc->aac_io_lock);

	camsc->sim = sim;
	camsc->path = path;

	return (0);
}

static u_int64_t 
aac_eval_blockno(u_int8_t *cmdp) 
{
	u_int64_t blockno;

	switch (cmdp[0]) {
	case READ_6:
	case WRITE_6:
		blockno = scsi_3btoul(((struct scsi_rw_6 *)cmdp)->addr);	
		break;
	case READ_10:
	case WRITE_10:
		blockno = scsi_4btoul(((struct scsi_rw_10 *)cmdp)->addr);	
		break;
	case READ_12:
	case WRITE_12:
		blockno = scsi_4btoul(((struct scsi_rw_12 *)cmdp)->addr);	
		break;
	case READ_16:
	case WRITE_16:
		blockno = scsi_8btou64(((struct scsi_rw_16 *)cmdp)->addr);	
		break;
	default:
		blockno = 0;
		break;
	}
	return(blockno);
}		

static void
aac_container_rw_command(struct cam_sim *sim, union ccb *ccb, u_int8_t *cmdp)
{
	struct	aac_cam *camsc;
	struct	aac_softc *sc;
	struct	aac_command *cm;
	struct	aac_fib *fib;
	u_int64_t blockno;

	camsc = (struct aac_cam *)cam_sim_softc(sim);
	sc = camsc->inf->aac_sc;
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	if (aacraid_alloc_command(sc, &cm)) {
		struct aac_event *event;

		xpt_freeze_simq(sim, 1);
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
		ccb->ccb_h.sim_priv.entries[0].ptr = camsc;
		event = malloc(sizeof(struct aac_event), M_AACRAIDCAM,
		    M_NOWAIT | M_ZERO);
		if (event == NULL) {
			device_printf(sc->aac_dev,
			    "Warning, out of memory for event\n");
			return;
		}
		event->ev_callback = aac_cam_event;
		event->ev_arg = ccb;
		event->ev_type = AAC_EVENT_CMFREE;
		aacraid_add_event(sc, event);
		return;
	}

	fib = cm->cm_fib;
	switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		cm->cm_flags |= AAC_CMD_DATAIN;
		break;
	case CAM_DIR_OUT:
		cm->cm_flags |= AAC_CMD_DATAOUT;
		break;
	case CAM_DIR_NONE:
		break;
	default:
		cm->cm_flags |= AAC_CMD_DATAIN | AAC_CMD_DATAOUT;
		break;
	}

	blockno = aac_eval_blockno(cmdp);

	cm->cm_complete = aac_container_complete;
	cm->cm_ccb = ccb;
	cm->cm_timestamp = time_uptime;
	cm->cm_data = (void *)ccb->csio.data_ptr;
	cm->cm_datalen = ccb->csio.dxfer_len;

	fib->Header.Size = sizeof(struct aac_fib_header);
	fib->Header.XferState =
		AAC_FIBSTATE_HOSTOWNED   |
		AAC_FIBSTATE_INITIALISED |
		AAC_FIBSTATE_EMPTY	 |
		AAC_FIBSTATE_FROMHOST	 |
		AAC_FIBSTATE_REXPECTED   |
		AAC_FIBSTATE_NORM	 |
		AAC_FIBSTATE_ASYNC	 |
		AAC_FIBSTATE_FAST_RESPONSE;

	if (sc->flags & AAC_FLAGS_NEW_COMM_TYPE2) {
		struct aac_raw_io2 *raw;
		raw = (struct aac_raw_io2 *)&fib->data[0];
		bzero(raw, sizeof(struct aac_raw_io2));
		fib->Header.Command = RawIo2;
		raw->strtBlkLow = (u_int32_t)blockno;
		raw->strtBlkHigh = (u_int32_t)(blockno >> 32);
		raw->byteCnt = cm->cm_datalen;
		raw->ldNum = ccb->ccb_h.target_id;
		fib->Header.Size += sizeof(struct aac_raw_io2);
		cm->cm_sgtable = (struct aac_sg_table *)raw->sge;
		if (cm->cm_flags & AAC_CMD_DATAIN) 
			raw->flags = RIO2_IO_TYPE_READ | RIO2_SG_FORMAT_IEEE1212;
		else
			raw->flags = RIO2_IO_TYPE_WRITE | RIO2_SG_FORMAT_IEEE1212;
	} else if (sc->flags & AAC_FLAGS_RAW_IO) {
		struct aac_raw_io *raw;
		raw = (struct aac_raw_io *)&fib->data[0];
		bzero(raw, sizeof(struct aac_raw_io));
		fib->Header.Command = RawIo;
		raw->BlockNumber = blockno;
		raw->ByteCount = cm->cm_datalen;
		raw->ContainerId = ccb->ccb_h.target_id;
		fib->Header.Size += sizeof(struct aac_raw_io);
		cm->cm_sgtable = (struct aac_sg_table *)
			&raw->SgMapRaw;
		if (cm->cm_flags & AAC_CMD_DATAIN) 
			raw->Flags = 1;
	} else if ((sc->flags & AAC_FLAGS_SG_64BIT) == 0) {
		fib->Header.Command = ContainerCommand;
		if (cm->cm_flags & AAC_CMD_DATAIN) {
			struct aac_blockread *br;
			br = (struct aac_blockread *)&fib->data[0];
			br->Command = VM_CtBlockRead;
			br->ContainerId = ccb->ccb_h.target_id;
			br->BlockNumber = blockno;
			br->ByteCount = cm->cm_datalen;
			fib->Header.Size += sizeof(struct aac_blockread);
			cm->cm_sgtable = &br->SgMap;
		} else {
			struct aac_blockwrite *bw;
			bw = (struct aac_blockwrite *)&fib->data[0];
			bw->Command = VM_CtBlockWrite;
			bw->ContainerId = ccb->ccb_h.target_id;
			bw->BlockNumber = blockno;
			bw->ByteCount = cm->cm_datalen;
			bw->Stable = CUNSTABLE;
			fib->Header.Size += sizeof(struct aac_blockwrite);
			cm->cm_sgtable = &bw->SgMap;
		}
	} else {
		fib->Header.Command = ContainerCommand64;
		if (cm->cm_flags & AAC_CMD_DATAIN) {
			struct aac_blockread64 *br;
			br = (struct aac_blockread64 *)&fib->data[0];
			br->Command = VM_CtHostRead64;
			br->ContainerId = ccb->ccb_h.target_id;
			br->SectorCount = cm->cm_datalen/AAC_BLOCK_SIZE;
			br->BlockNumber = blockno;
			br->Pad = 0;
			br->Flags = 0;
			fib->Header.Size += sizeof(struct aac_blockread64);
			cm->cm_sgtable = (struct aac_sg_table *)&br->SgMap64;
		} else {
			struct aac_blockwrite64 *bw;
			bw = (struct aac_blockwrite64 *)&fib->data[0];
			bw->Command = VM_CtHostWrite64;
			bw->ContainerId = ccb->ccb_h.target_id;
			bw->SectorCount = cm->cm_datalen/AAC_BLOCK_SIZE;
			bw->BlockNumber = blockno;
			bw->Pad = 0;
			bw->Flags = 0;
			fib->Header.Size += sizeof(struct aac_blockwrite64);
			cm->cm_sgtable = (struct aac_sg_table *)&bw->SgMap64;
		}
	}
	aac_enqueue_ready(cm);
	aacraid_startio(cm->cm_sc);
}

static void
aac_container_special_command(struct cam_sim *sim, union ccb *ccb, 
	u_int8_t *cmdp)
{
	struct	aac_cam *camsc;
	struct	aac_softc *sc;
	struct	aac_container *co;

	camsc = (struct aac_cam *)cam_sim_softc(sim);
	sc = camsc->inf->aac_sc;
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	TAILQ_FOREACH(co, &sc->aac_container_tqh, co_link) {
		fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, "found container %d search for %d", co->co_mntobj.ObjectId, ccb->ccb_h.target_id);
		if (co->co_mntobj.ObjectId == ccb->ccb_h.target_id)
			break;
	}
	if (co == NULL || ccb->ccb_h.target_lun != 0) {
		fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, 
			"Container not present: cmd 0x%x id %d lun %d len %d", 
			*cmdp, ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
			ccb->csio.dxfer_len);
		ccb->ccb_h.status = CAM_DEV_NOT_THERE;
		xpt_done(ccb);
		return;
	}

	if (ccb->csio.dxfer_len)
		bzero(ccb->csio.data_ptr, ccb->csio.dxfer_len);

	switch (*cmdp) {
	case INQUIRY:
	{
		struct scsi_inquiry *inq = (struct scsi_inquiry *)cmdp;

		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container INQUIRY id %d lun %d len %d VPD 0x%x Page 0x%x", 
			ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
			ccb->csio.dxfer_len, inq->byte2, inq->page_code);
		if (!(inq->byte2 & SI_EVPD)) {
			struct scsi_inquiry_data *p = 
				(struct scsi_inquiry_data *)ccb->csio.data_ptr;
			if (inq->page_code != 0) {
				aac_set_scsi_error(sc, ccb,
					SCSI_STATUS_CHECK_COND,
					SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x00);	
				xpt_done(ccb);
				return;	
			}	
			p->device = T_DIRECT;
			p->version = SCSI_REV_SPC2;
			p->response_format = 2;
			if (ccb->csio.dxfer_len >= 36) {
				p->additional_length = 31;
				p->flags = SID_WBus16|SID_Sync|SID_CmdQue;
				/* OEM Vendor defines */
				strncpy(p->vendor, "Adaptec ", sizeof(p->vendor));
				strncpy(p->product, "Array           ",
				    sizeof(p->product));
				strncpy(p->revision, "V1.0",
				    sizeof(p->revision));
			}	
		} else {
			if (inq->page_code == SVPD_SUPPORTED_PAGE_LIST) {
				struct scsi_vpd_supported_page_list *p =
					(struct scsi_vpd_supported_page_list *)
					ccb->csio.data_ptr;
				p->device = T_DIRECT;
				p->page_code = SVPD_SUPPORTED_PAGE_LIST;
				p->length = 2;
				p->list[0] = SVPD_SUPPORTED_PAGE_LIST;
				p->list[1] = SVPD_UNIT_SERIAL_NUMBER;
			} else if (inq->page_code == SVPD_UNIT_SERIAL_NUMBER) {
				struct scsi_vpd_unit_serial_number *p =
					(struct scsi_vpd_unit_serial_number *)
					ccb->csio.data_ptr;	
				p->device = T_DIRECT;
				p->page_code = SVPD_UNIT_SERIAL_NUMBER;
				p->length = sprintf((char *)p->serial_num, 
					"%08X%02X", co->co_uid, 
					ccb->ccb_h.target_id);
			} else {
				aac_set_scsi_error(sc, ccb, 
					SCSI_STATUS_CHECK_COND,
					SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x00);	
				xpt_done(ccb);
				return;	
			}
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case REPORT_LUNS:
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container REPORT_LUNS id %d lun %d len %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case START_STOP:
	{
		struct scsi_start_stop_unit *ss = 
			(struct scsi_start_stop_unit *)cmdp;
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container START_STOP id %d lun %d len %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		if (sc->aac_support_opt2 & AAC_SUPPORTED_POWER_MANAGEMENT) {
			struct aac_command *cm;
			struct aac_fib *fib;
			struct aac_cnt_config *ccfg;

			if (aacraid_alloc_command(sc, &cm)) {
				struct aac_event *event;

				xpt_freeze_simq(sim, 1);
				ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
				ccb->ccb_h.sim_priv.entries[0].ptr = camsc;
				event = malloc(sizeof(struct aac_event), M_AACRAIDCAM,
					M_NOWAIT | M_ZERO);
				if (event == NULL) {
					device_printf(sc->aac_dev,
						"Warning, out of memory for event\n");
					return;
				}
				event->ev_callback = aac_cam_event;
				event->ev_arg = ccb;
				event->ev_type = AAC_EVENT_CMFREE;
				aacraid_add_event(sc, event);
				return;
			}

			fib = cm->cm_fib;
			cm->cm_timestamp = time_uptime;
			cm->cm_datalen = 0;

			fib->Header.Size = 
				sizeof(struct aac_fib_header) + sizeof(struct aac_cnt_config);
			fib->Header.XferState =
				AAC_FIBSTATE_HOSTOWNED   |
				AAC_FIBSTATE_INITIALISED |
				AAC_FIBSTATE_EMPTY	 |
				AAC_FIBSTATE_FROMHOST	 |
				AAC_FIBSTATE_REXPECTED   |
				AAC_FIBSTATE_NORM	 |
				AAC_FIBSTATE_ASYNC	 |
				AAC_FIBSTATE_FAST_RESPONSE;
			fib->Header.Command = ContainerCommand;

			/* Start unit */
			ccfg = (struct aac_cnt_config *)&fib->data[0];
			bzero(ccfg, sizeof (*ccfg) - CT_PACKET_SIZE);
			ccfg->Command = VM_ContainerConfig;
			ccfg->CTCommand.command = CT_PM_DRIVER_SUPPORT;
			ccfg->CTCommand.param[0] = (ss->how & SSS_START ?
				AAC_PM_DRIVERSUP_START_UNIT : 
				AAC_PM_DRIVERSUP_STOP_UNIT);
			ccfg->CTCommand.param[1] = co->co_mntobj.ObjectId;
			ccfg->CTCommand.param[2] = 0;	/* 1 - immediate */

			if (aacraid_wait_command(cm) != 0 ||
				*(u_int32_t *)&fib->data[0] != 0) {
				printf("Power Management: Error start/stop container %d\n", 
				co->co_mntobj.ObjectId);
			}
			aacraid_release_command(cm);
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case TEST_UNIT_READY:
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container TEST_UNIT_READY id %d lun %d len %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case REQUEST_SENSE:
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container REQUEST_SENSE id %d lun %d len %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case READ_CAPACITY:
	{
		struct scsi_read_capacity_data *p = 
			(struct scsi_read_capacity_data *)ccb->csio.data_ptr;
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container READ_CAPACITY id %d lun %d len %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		scsi_ulto4b(co->co_mntobj.ObjExtension.BlockDevice.BlockSize, p->length);
		/* check if greater than 2TB */
		if (co->co_mntobj.CapacityHigh) {
			if (sc->flags & AAC_FLAGS_LBA_64BIT)
				scsi_ulto4b(0xffffffff, p->addr);
		} else {
			scsi_ulto4b(co->co_mntobj.Capacity-1, p->addr);
		} 
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case SERVICE_ACTION_IN:
	{	
		struct scsi_read_capacity_data_long *p = 
			(struct scsi_read_capacity_data_long *)
			ccb->csio.data_ptr;
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container SERVICE_ACTION_IN id %d lun %d len %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		if (((struct scsi_read_capacity_16 *)cmdp)->service_action != 
			SRC16_SERVICE_ACTION) {
			aac_set_scsi_error(sc, ccb, SCSI_STATUS_CHECK_COND,
				SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x00);	
			xpt_done(ccb);
			return;	
		}
		scsi_ulto4b(co->co_mntobj.ObjExtension.BlockDevice.BlockSize, p->length);
		scsi_ulto4b(co->co_mntobj.CapacityHigh, p->addr);
		scsi_ulto4b(co->co_mntobj.Capacity-1, &p->addr[4]);

		if (ccb->csio.dxfer_len >= 14) {		
			u_int32_t mapping = co->co_mntobj.ObjExtension.BlockDevice.bdLgclPhysMap;
			p->prot_lbppbe = 0;
			while (mapping > 1) {
				mapping >>= 1;
				p->prot_lbppbe++;
			}
			p->prot_lbppbe &= 0x0f;
		}

		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case MODE_SENSE_6:
	{
		struct scsi_mode_sense_6 *msp =(struct scsi_mode_sense_6 *)cmdp;
		struct ms6_data {
			struct scsi_mode_hdr_6 hd;
			struct scsi_mode_block_descr bd;
			char pages;
		} *p = (struct ms6_data *)ccb->csio.data_ptr;
		char *pagep;
		int return_all_pages = FALSE;

		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container MODE_SENSE id %d lun %d len %d page %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len, msp->page);
		p->hd.datalen = sizeof(struct scsi_mode_hdr_6) - 1;
		if (co->co_mntobj.ContentState & AAC_FSCS_READONLY)
			p->hd.dev_specific = 0x80;	/* WP */
		p->hd.dev_specific |= 0x10;	/* DPOFUA */
		if (msp->byte2 & SMS_DBD) {
			p->hd.block_descr_len = 0;
		} else {
			p->hd.block_descr_len = 
				sizeof(struct scsi_mode_block_descr);	
			p->hd.datalen += p->hd.block_descr_len;
			scsi_ulto3b(co->co_mntobj.ObjExtension.BlockDevice.BlockSize, p->bd.block_len);
			if (co->co_mntobj.Capacity > 0xffffff ||
				co->co_mntobj.CapacityHigh) {
				p->bd.num_blocks[0] = 0xff;
				p->bd.num_blocks[1] = 0xff;
				p->bd.num_blocks[2] = 0xff;
			} else {
				p->bd.num_blocks[0] = (u_int8_t)
					(co->co_mntobj.Capacity >> 16);
				p->bd.num_blocks[1] = (u_int8_t)
					(co->co_mntobj.Capacity >> 8);
				p->bd.num_blocks[2] = (u_int8_t)
					(co->co_mntobj.Capacity);
			}
		}
		pagep = &p->pages;	
		switch (msp->page & SMS_PAGE_CODE) {
		case SMS_ALL_PAGES_PAGE:
			return_all_pages = TRUE;
		case SMS_CONTROL_MODE_PAGE:
		{
			struct scsi_control_page *cp = 
				(struct scsi_control_page *)pagep;

			if (ccb->csio.dxfer_len <= p->hd.datalen + 8) {
				aac_set_scsi_error(sc, ccb,
					SCSI_STATUS_CHECK_COND,
					SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x00);	
				xpt_done(ccb);
				return;	
			}
			cp->page_code = SMS_CONTROL_MODE_PAGE;
			cp->page_length = 6;
			p->hd.datalen += 8;
			pagep += 8;
			if (!return_all_pages)
				break;
		}
		case SMS_VENDOR_SPECIFIC_PAGE:
			break;	
		default:	
			aac_set_scsi_error(sc, ccb, SCSI_STATUS_CHECK_COND,
				SSD_KEY_ILLEGAL_REQUEST, 0x24, 0x00);	
			xpt_done(ccb);
			return;	
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case SYNCHRONIZE_CACHE:
		fwprintf(sc, HBA_FLAGS_DBG_COMM_B, 
		"Container SYNCHRONIZE_CACHE id %d lun %d len %d", 
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	default:
		fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, 
		"Container unsupp. cmd 0x%x id %d lun %d len %d", 
		*cmdp, ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		ccb->csio.dxfer_len);
		ccb->ccb_h.status = CAM_REQ_CMP; /*CAM_REQ_INVALID*/
		break;
	}
	xpt_done(ccb);
}

static void
aac_passthrough_command(struct cam_sim *sim, union ccb *ccb)
{
	struct	aac_cam *camsc;
	struct	aac_softc *sc;
	struct	aac_command *cm;
	struct	aac_fib *fib;
	struct	aac_srb *srb;

	camsc = (struct aac_cam *)cam_sim_softc(sim);
	sc = camsc->inf->aac_sc;
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	if (aacraid_alloc_command(sc, &cm)) {
		struct aac_event *event;

		xpt_freeze_simq(sim, 1);
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
		ccb->ccb_h.sim_priv.entries[0].ptr = camsc;
		event = malloc(sizeof(struct aac_event), M_AACRAIDCAM,
		    M_NOWAIT | M_ZERO);
		if (event == NULL) {
			device_printf(sc->aac_dev,
			    "Warning, out of memory for event\n");
			return;
		}
		event->ev_callback = aac_cam_event;
		event->ev_arg = ccb;
		event->ev_type = AAC_EVENT_CMFREE;
		aacraid_add_event(sc, event);
		return;
	}

	fib = cm->cm_fib;
	switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		cm->cm_flags |= AAC_CMD_DATAIN;
		break;
	case CAM_DIR_OUT:
		cm->cm_flags |= AAC_CMD_DATAOUT;
		break;
	case CAM_DIR_NONE:
		break;
	default:
		cm->cm_flags |= AAC_CMD_DATAIN | AAC_CMD_DATAOUT;
		break;
	}

	srb = (struct aac_srb *)&fib->data[0];
	srb->function = AAC_SRB_FUNC_EXECUTE_SCSI;
	if (cm->cm_flags & (AAC_CMD_DATAIN|AAC_CMD_DATAOUT)) 
		srb->flags = AAC_SRB_FLAGS_UNSPECIFIED_DIRECTION;
	if (cm->cm_flags & AAC_CMD_DATAIN) 
		srb->flags = AAC_SRB_FLAGS_DATA_IN;
	else if (cm->cm_flags & AAC_CMD_DATAOUT) 
		srb->flags = AAC_SRB_FLAGS_DATA_OUT;
	else  
		srb->flags = AAC_SRB_FLAGS_NO_DATA_XFER;

	/*
	 * Copy the CDB into the SRB.  It's only 6-16 bytes,
	 * so a copy is not too expensive.
	 */
	srb->cdb_len = ccb->csio.cdb_len;
	if (ccb->ccb_h.flags & CAM_CDB_POINTER)
		bcopy(ccb->csio.cdb_io.cdb_ptr, (u_int8_t *)&srb->cdb[0],
			srb->cdb_len);
	else
		bcopy(ccb->csio.cdb_io.cdb_bytes, (u_int8_t *)&srb->cdb[0],
			srb->cdb_len);

	/* Set command */
	fib->Header.Command = (sc->flags & AAC_FLAGS_SG_64BIT) ? 
		ScsiPortCommandU64 : ScsiPortCommand;
	fib->Header.Size = sizeof(struct aac_fib_header) +
			sizeof(struct aac_srb);

	/* Map the s/g list */
	cm->cm_sgtable = &srb->sg_map;
	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		/*
		 * Arrange things so that the S/G
		 * map will get set up automagically
		 */
		cm->cm_data = (void *)ccb->csio.data_ptr;
		cm->cm_datalen = ccb->csio.dxfer_len;
		srb->data_len = ccb->csio.dxfer_len;
	} else {
		cm->cm_data = NULL;
		cm->cm_datalen = 0;
		srb->data_len = 0;
	}

	srb->bus = camsc->inf->BusNumber - 1; /* Bus no. rel. to the card */
	srb->target = ccb->ccb_h.target_id;
	srb->lun = ccb->ccb_h.target_lun;
	srb->timeout = ccb->ccb_h.timeout;	/* XXX */
	srb->retry_limit = 0;

	cm->cm_complete = aac_cam_complete;
	cm->cm_ccb = ccb;
	cm->cm_timestamp = time_uptime;

	fib->Header.XferState =
			AAC_FIBSTATE_HOSTOWNED	|
			AAC_FIBSTATE_INITIALISED	|
			AAC_FIBSTATE_FROMHOST	|
			AAC_FIBSTATE_REXPECTED	|
			AAC_FIBSTATE_NORM	|
			AAC_FIBSTATE_ASYNC	 |
			AAC_FIBSTATE_FAST_RESPONSE;

	aac_enqueue_ready(cm);
	aacraid_startio(cm->cm_sc);
}

static void
aac_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct	aac_cam *camsc;
	struct	aac_softc *sc;

	camsc = (struct aac_cam *)cam_sim_softc(sim);
	sc = camsc->inf->aac_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	mtx_assert(&sc->aac_io_lock, MA_OWNED);

	/* Synchronous ops, and ops that don't require communication with the
	 * controller */
	switch(ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
		/* This is handled down below */
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
		cpi->target_sprt = 0;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = camsc->inf->TargetsPerBus;
		cpi->max_lun = 8;	/* Per the controller spec */
		cpi->initiator_id = camsc->inf->InitiatorBusId;
		cpi->bus_id = camsc->inf->BusNumber;
#if __FreeBSD_version >= 800000
		cpi->maxio = sc->aac_max_sectors << 9;
#endif

		/*
		 * Resetting via the passthrough or parallel bus scan
		 * causes problems.
		 */
		cpi->hba_misc = PIM_NOBUSRESET;
		cpi->hba_inquiry = PI_TAG_ABLE;
		cpi->base_transfer_speed = 300000;
#ifdef CAM_NEW_TRAN_CODE
		cpi->hba_misc |= PIM_SEQSCAN;
		cpi->protocol = PROTO_SCSI;
		cpi->transport = XPORT_SAS;
		cpi->transport_version = 0;
		cpi->protocol_version = SCSI_REV_SPC2;
#endif
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "PMC-Sierra", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
#ifdef CAM_NEW_TRAN_CODE
		struct ccb_trans_settings_scsi *scsi =
			&ccb->cts.proto_specific.scsi;
		struct ccb_trans_settings_spi *spi =
			&ccb->cts.xport_specific.spi;
		ccb->cts.protocol = PROTO_SCSI;
		ccb->cts.protocol_version = SCSI_REV_SPC2;
		ccb->cts.transport = XPORT_SAS;
		ccb->cts.transport_version = 0;
		scsi->valid = CTS_SCSI_VALID_TQ;
		scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;
		spi->valid |= CTS_SPI_VALID_DISC;
		spi->flags |= CTS_SPI_FLAGS_DISC_ENB;
#else
		ccb->cts.flags = ~(CCB_TRANS_DISC_ENB | CCB_TRANS_TAG_ENB);
		ccb->cts.valid = CCB_TRANS_DISC_VALID | CCB_TRANS_TQ_VALID;
#endif
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return;
	}
	case XPT_SET_TRAN_SETTINGS:
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		xpt_done(ccb);
		return;
	case XPT_RESET_BUS:
		if (!(sc->flags & AAC_FLAGS_CAM_NORESET) &&
			camsc->inf->BusType != CONTAINER_BUS) {
			ccb->ccb_h.status = aac_cam_reset_bus(sim, ccb);
		} else {
			ccb->ccb_h.status = CAM_REQ_CMP;
		}
		xpt_done(ccb);
		return;
	case XPT_RESET_DEV:
		ccb->ccb_h.status = CAM_REQ_CMP;
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
	if (camsc->inf->BusType == CONTAINER_BUS) {
		u_int8_t *cmdp;

		if (ccb->ccb_h.flags & CAM_CDB_POINTER)
			cmdp = ccb->csio.cdb_io.cdb_ptr;
		else	
			cmdp = &ccb->csio.cdb_io.cdb_bytes[0];

		if (*cmdp==READ_6 || *cmdp==WRITE_6 || *cmdp==READ_10 ||
			*cmdp==WRITE_10 || *cmdp==READ_12 || *cmdp==WRITE_12 ||
			*cmdp==READ_16 || *cmdp==WRITE_16) 
			aac_container_rw_command(sim, ccb, cmdp);
		else
			aac_container_special_command(sim, ccb, cmdp);
	} else {
		aac_passthrough_command(sim, ccb);
	}
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
aac_container_complete(struct aac_command *cm)
{
	union	ccb *ccb;
	u_int32_t status;

	fwprintf(cm->cm_sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	ccb = cm->cm_ccb;
	status = ((u_int32_t *)cm->cm_fib->data)[0];

	if (cm->cm_flags & AAC_CMD_RESET) {
		ccb->ccb_h.status = CAM_SCSI_BUS_RESET;
	} else if (status == ST_OK) {
		ccb->ccb_h.status = CAM_REQ_CMP;
	} else if (status == ST_NOT_READY) {
		ccb->ccb_h.status = CAM_BUSY;
	} else {
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;
	}

	aacraid_release_command(cm);
	xpt_done(ccb);
}

static void
aac_cam_complete(struct aac_command *cm)
{
	union	ccb *ccb;
	struct 	aac_srb_response *srbr;
	struct	aac_softc *sc;

	sc = cm->cm_sc;
	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	ccb = cm->cm_ccb;
	srbr = (struct aac_srb_response *)&cm->cm_fib->data[0];

	if (cm->cm_flags & AAC_CMD_FASTRESP) {
		/* fast response */
		srbr->srb_status = CAM_REQ_CMP;
		srbr->scsi_status = SCSI_STATUS_OK;
		srbr->sense_len = 0;
	}

	if (cm->cm_flags & AAC_CMD_RESET) {
		ccb->ccb_h.status = CAM_SCSI_BUS_RESET;
	} else if (srbr->fib_status != 0) {
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
			u_int8_t command, device;

			ccb->csio.scsi_status = srbr->scsi_status;

			/* Take care of autosense */
			if (srbr->sense_len) {
				int sense_len, scsi_sense_len;

				scsi_sense_len = sizeof(struct scsi_sense_data);
				bzero(&ccb->csio.sense_data, scsi_sense_len);
				sense_len = (srbr->sense_len > 
				    scsi_sense_len) ? scsi_sense_len :
				    srbr->sense_len;
				bcopy(&srbr->sense[0], &ccb->csio.sense_data,
				    srbr->sense_len);
				ccb->csio.sense_len = sense_len;
				ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
				// scsi_sense_print(&ccb->csio);
			}

			/* If this is an inquiry command, fake things out */
			if (ccb->ccb_h.flags & CAM_CDB_POINTER)
				command = ccb->csio.cdb_io.cdb_ptr[0];
			else
				command = ccb->csio.cdb_io.cdb_bytes[0];

			if (command == INQUIRY) {
				if (ccb->ccb_h.status == CAM_REQ_CMP) {
				  device = ccb->csio.data_ptr[0] & 0x1f;
				  /*
				   * We want DASD and PROC devices to only be
				   * visible through the pass device.
				   */
				  if ((device == T_DIRECT && 
				    !(sc->aac_feature_bits & AAC_SUPPL_SUPPORTED_JBOD)) ||
				    (device == T_PROCESSOR)) 
				    ccb->csio.data_ptr[0] =
				  	((device & 0xe0) | T_NODEVICE);
					
				  /* handle phys. components of a log. drive */
				  if (ccb->csio.data_ptr[0] & 0x20) {
					if (sc->hint_flags & 8) {
					  /* expose phys. device (daXX) */
					  ccb->csio.data_ptr[0] &= 0xdf;
					} else {
					  /* phys. device only visible through pass device (passXX) */
					  ccb->csio.data_ptr[0] |= 0x10;
					}
				  }
				} else if (ccb->ccb_h.status == CAM_SEL_TIMEOUT &&
				  ccb->ccb_h.target_lun != 0) {
				  /* fix for INQUIRYs on Lun>0 */
				  ccb->ccb_h.status = CAM_DEV_NOT_THERE;
				}
			}
		}
	}

	aacraid_release_command(cm);
	xpt_done(ccb);
}

static u_int32_t
aac_cam_reset_bus(struct cam_sim *sim, union ccb *ccb)
{
	struct aac_command *cm;
	struct aac_fib *fib;
	struct aac_softc *sc;
	struct aac_cam *camsc;
	struct aac_vmioctl *vmi;
	struct aac_resetbus *rbc;
	u_int32_t rval;

	camsc = (struct aac_cam *)cam_sim_softc(sim);
	sc = camsc->inf->aac_sc;

	if (sc == NULL) {
		printf("aac: Null sc?\n");
		return (CAM_REQ_ABORTED);
	}

	if (aacraid_alloc_command(sc, &cm)) {
		struct aac_event *event;

		xpt_freeze_simq(sim, 1);
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
		ccb->ccb_h.sim_priv.entries[0].ptr = camsc;
		event = malloc(sizeof(struct aac_event), M_AACRAIDCAM,
			M_NOWAIT | M_ZERO);
		if (event == NULL) {
			device_printf(sc->aac_dev,
				"Warning, out of memory for event\n");
			return (CAM_REQ_ABORTED);
		}
		event->ev_callback = aac_cam_event;
		event->ev_arg = ccb;
		event->ev_type = AAC_EVENT_CMFREE;
		aacraid_add_event(sc, event);
		return (CAM_REQ_ABORTED);
	}

	fib = cm->cm_fib;
	cm->cm_timestamp = time_uptime;
	cm->cm_datalen = 0;

	fib->Header.Size = 
		sizeof(struct aac_fib_header) + sizeof(struct aac_vmioctl);
	fib->Header.XferState =
		AAC_FIBSTATE_HOSTOWNED   |
		AAC_FIBSTATE_INITIALISED |
		AAC_FIBSTATE_EMPTY	 |
		AAC_FIBSTATE_FROMHOST	 |
		AAC_FIBSTATE_REXPECTED   |
		AAC_FIBSTATE_NORM	 |
		AAC_FIBSTATE_ASYNC	 |
		AAC_FIBSTATE_FAST_RESPONSE;
	fib->Header.Command = ContainerCommand;

	vmi = (struct aac_vmioctl *)&fib->data[0];
	bzero(vmi, sizeof(struct aac_vmioctl));

	vmi->Command = VM_Ioctl;
	vmi->ObjType = FT_DRIVE;
	vmi->MethId = sc->scsi_method_id;
	vmi->ObjId = 0;
	vmi->IoctlCmd = ResetBus;

	rbc = (struct aac_resetbus *)&vmi->IoctlBuf[0];
	rbc->BusNumber = camsc->inf->BusNumber - 1;

	if (aacraid_wait_command(cm) != 0) {
		device_printf(sc->aac_dev,"Error sending ResetBus command\n");
		rval = CAM_REQ_ABORTED;
	} else {
		rval = CAM_REQ_CMP;
	}
	aacraid_release_command(cm);
	return (rval);
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

static int
aac_load_map_command_sg(struct aac_softc *sc, struct aac_command *cm)
{
	int error;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");
	error = bus_dmamap_load(sc->aac_buffer_dmat,
				cm->cm_datamap, cm->cm_data, cm->cm_datalen,
				aacraid_map_command_sg, cm, 0);
	if (error == EINPROGRESS) {
		fwprintf(sc, HBA_FLAGS_DBG_INIT_B, "freezing queue\n");
		sc->flags |= AAC_QUEUE_FRZN;
		error = 0;
	} else if (error != 0) {
		panic("aac_load_map_command_sg: unexpected error %d from "
	     		"busdma", error);
	}
	return(error);
}

/*
 * Start as much queued I/O as possible on the controller
 */
void
aacraid_startio(struct aac_softc *sc)
{
	struct aac_command *cm;

	fwprintf(sc, HBA_FLAGS_DBG_FUNCTION_ENTRY_B, "");

	for (;;) {
		if (sc->aac_state & AAC_STATE_RESET) {
			fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, "AAC_STATE_RESET");
			break;
		}
		/*
		 * This flag might be set if the card is out of resources.
		 * Checking it here prevents an infinite loop of deferrals.
		 */
		if (sc->flags & AAC_QUEUE_FRZN) {
			fwprintf(sc, HBA_FLAGS_DBG_ERROR_B, "AAC_QUEUE_FRZN");
			break;
		}

		/*
		 * Try to get a command that's been put off for lack of
		 * resources
		 */
		if (sc->flags & AAC_FLAGS_SYNC_MODE) {
			/* sync. transfer mode */
			if (sc->aac_sync_cm) 
				break;
			cm = aac_dequeue_ready(sc);
			sc->aac_sync_cm = cm;
		} else {
			cm = aac_dequeue_ready(sc);
		}

		/* nothing to do? */
		if (cm == NULL)
			break;

		/* don't map more than once */
		if (cm->cm_flags & AAC_CMD_MAPPED)
			panic("aac: command %p already mapped", cm);

		/*
		 * Set up the command to go to the controller.  If there are no
		 * data buffers associated with the command then it can bypass
		 * busdma.
		 */
		if (cm->cm_datalen)
			aac_load_map_command_sg(sc, cm);
		else
			aacraid_map_command_sg(cm, NULL, 0, 0);
	}
}
