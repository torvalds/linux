/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap.
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
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 * Modifications by: Manjunath Ranganathaiah
 */


/*
 * FreeBSD CAM related functions.
 */


#include <dev/twa/tw_osl_includes.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

static TW_VOID	twa_action(struct cam_sim *sim, union ccb *ccb);
static TW_VOID	twa_poll(struct cam_sim *sim);

static TW_INT32	tw_osli_execute_scsi(struct tw_osli_req_context *req,
	union ccb *ccb);



/*
 * Function name:	tw_osli_cam_attach
 * Description:		Attaches the driver to CAM.
 *
 * Input:		sc	-- ptr to OSL internal ctlr context
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_osli_cam_attach(struct twa_softc *sc)
{
	struct cam_devq		*devq;

	tw_osli_dbg_dprintf(3, sc, "entered");

	/*
	 * Create the device queue for our SIM.
	 */
	if ((devq = cam_simq_alloc(TW_OSLI_MAX_NUM_IOS)) == NULL) {
		tw_osli_printf(sc, "error = %d",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2100,
			"Failed to create SIM device queue",
			ENOMEM);
		return(ENOMEM);
	}

	/*
	 * Create a SIM entry.  Though we can support TW_OSLI_MAX_NUM_REQUESTS
	 * simultaneous requests, we claim to be able to handle only
	 * TW_OSLI_MAX_NUM_IOS (two less), so that we always have a request
	 * packet available to service ioctls and AENs.
	 */
	tw_osli_dbg_dprintf(3, sc, "Calling cam_sim_alloc");
	sc->sim = cam_sim_alloc(twa_action, twa_poll, "twa", sc,
			device_get_unit(sc->bus_dev), sc->sim_lock,
			TW_OSLI_MAX_NUM_IOS, 1, devq);
	if (sc->sim == NULL) {
		cam_simq_free(devq);
		tw_osli_printf(sc, "error = %d",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2101,
			"Failed to create a SIM entry",
			ENOMEM);
		return(ENOMEM);
	}

	/*
	 * Register the bus.
	 */
	tw_osli_dbg_dprintf(3, sc, "Calling xpt_bus_register");
	mtx_lock(sc->sim_lock);
	if (xpt_bus_register(sc->sim, sc->bus_dev, 0) != CAM_SUCCESS) {
		cam_sim_free(sc->sim, TRUE);
		sc->sim = NULL; /* so cam_detach will not try to free it */
		tw_osli_printf(sc, "error = %d",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2102,
			"Failed to register the bus",
			ENXIO);
		mtx_unlock(sc->sim_lock);
		return(ENXIO);
	}

	tw_osli_dbg_dprintf(3, sc, "Calling xpt_create_path");
	if (xpt_create_path(&sc->path, NULL,
				cam_sim_path(sc->sim),
				CAM_TARGET_WILDCARD,
				CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path (sc->sim));
		/* Passing TRUE to cam_sim_free will free the devq as well. */
		cam_sim_free(sc->sim, TRUE);
		tw_osli_printf(sc, "error = %d",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2103,
			"Failed to create path",
			ENXIO);
		mtx_unlock(sc->sim_lock);
		return(ENXIO);
	}
	mtx_unlock(sc->sim_lock);

	tw_osli_dbg_dprintf(3, sc, "exiting");
	return(0);
}



/*
 * Function name:	tw_osli_cam_detach
 * Description:		Detaches the driver from CAM.
 *
 * Input:		sc	-- ptr to OSL internal ctlr context
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_osli_cam_detach(struct twa_softc *sc)
{
	tw_osli_dbg_dprintf(3, sc, "entered");

	mtx_lock(sc->sim_lock);
           
	if (sc->path)
		xpt_free_path(sc->path);
	if (sc->sim) {
		xpt_bus_deregister(cam_sim_path(sc->sim));
		/* Passing TRUE to cam_sim_free will free the devq as well. */
		cam_sim_free(sc->sim, TRUE);
	}
	/* It's ok have 1 hold count while destroying the mutex */
	mtx_destroy(sc->sim_lock);
}



/*
 * Function name:	tw_osli_execute_scsi
 * Description:		Build a fw cmd, based on a CAM style ccb, and
 *			send it down.
 *
 * Input:		req	-- ptr to OSL internal request context
 *			ccb	-- ptr to CAM style ccb
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_osli_execute_scsi(struct tw_osli_req_context *req, union ccb *ccb)
{
	struct twa_softc		*sc = req->ctlr;
	struct tw_cl_req_packet		*req_pkt;
	struct tw_cl_scsi_req_packet	*scsi_req;
	struct ccb_hdr			*ccb_h = &(ccb->ccb_h);
	struct ccb_scsiio		*csio = &(ccb->csio);
	TW_INT32			error;

	tw_osli_dbg_dprintf(10, sc, "SCSI I/O request 0x%x",
		csio->cdb_io.cdb_bytes[0]);

	if (ccb_h->target_id >= TW_CL_MAX_NUM_UNITS) {
		tw_osli_dbg_dprintf(3, sc, "Invalid target. PTL = %x %x %jx",
			ccb_h->path_id, ccb_h->target_id,
			(uintmax_t)ccb_h->target_lun);
		ccb_h->status |= CAM_TID_INVALID;
		xpt_done(ccb);
		return(1);
	}
	if (ccb_h->target_lun >= TW_CL_MAX_NUM_LUNS) {
		tw_osli_dbg_dprintf(3, sc, "Invalid lun. PTL = %x %x %jx",
			ccb_h->path_id, ccb_h->target_id,
			(uintmax_t)ccb_h->target_lun);
		ccb_h->status |= CAM_LUN_INVALID;
		xpt_done(ccb);
		return(1);
	}

	if(ccb_h->flags & CAM_CDB_PHYS) {
		tw_osli_printf(sc, "",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2105,
			"Physical CDB address!");
		ccb_h->status = CAM_REQ_INVALID;
		xpt_done(ccb);
		return(1);
	}

	/*
	 * We are going to work on this request.  Mark it as enqueued (though
	 * we don't actually queue it...)
	 */
	ccb_h->status |= CAM_SIM_QUEUED;

	if((ccb_h->flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		if(ccb_h->flags & CAM_DIR_IN)
			req->flags |= TW_OSLI_REQ_FLAGS_DATA_IN;
		else
			req->flags |= TW_OSLI_REQ_FLAGS_DATA_OUT;
	}

	/* Build the CL understood request packet for SCSI cmds. */
	req_pkt = &req->req_pkt;
	req_pkt->status = 0;
	req_pkt->tw_osl_callback = tw_osl_complete_io;
	scsi_req = &(req_pkt->gen_req_pkt.scsi_req);
	scsi_req->unit = ccb_h->target_id;
	scsi_req->lun = ccb_h->target_lun;
	scsi_req->sense_len = 0;
	scsi_req->sense_data = (TW_UINT8 *)(&csio->sense_data);
	scsi_req->scsi_status = 0;
	if(ccb_h->flags & CAM_CDB_POINTER)
		scsi_req->cdb = csio->cdb_io.cdb_ptr;
	else
		scsi_req->cdb = csio->cdb_io.cdb_bytes;
	scsi_req->cdb_len = csio->cdb_len;

	if (csio->dxfer_len > TW_CL_MAX_IO_SIZE) {
		tw_osli_printf(sc, "size = %d",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2106,
			"I/O size too big",
			csio->dxfer_len);
		ccb_h->status = CAM_REQ_TOO_BIG;
		ccb_h->status &= ~CAM_SIM_QUEUED;
		xpt_done(ccb);
		return(1);
	}
	if ((ccb_h->flags & CAM_DATA_MASK) == CAM_DATA_VADDR) {
		if ((req->length = csio->dxfer_len) != 0) {
			req->data = csio->data_ptr;
			scsi_req->sgl_entries = 1;
		}
	} else
		req->flags |= TW_OSLI_REQ_FLAGS_CCB;
	req->deadline = tw_osl_get_local_time() + (ccb_h->timeout / 1000);

	/*
	 * twa_map_load_data_callback will fill in the SGL,
	 * and submit the I/O.
	 */
	error = tw_osli_map_request(req);
	if ((error) && (req->flags & TW_OSLI_REQ_FLAGS_FAILED)) {
		req->deadline = 0;
		ccb_h->status = CAM_REQ_CMP_ERR;
		ccb_h->status &= ~CAM_SIM_QUEUED;
		xpt_done(ccb);
	}
	return(error);
}



/*
 * Function name:	twa_action
 * Description:		Driver entry point for CAM's use.
 *
 * Input:		sim	-- sim corresponding to the ctlr
 *			ccb	-- ptr to CAM request
 * Output:		None
 * Return value:	None
 */
TW_VOID
twa_action(struct cam_sim *sim, union ccb *ccb)
{
	struct twa_softc	*sc = (struct twa_softc *)cam_sim_softc(sim);
	struct ccb_hdr		*ccb_h = &(ccb->ccb_h);

	switch (ccb_h->func_code) {
	case XPT_SCSI_IO:	/* SCSI I/O */
	{
		struct tw_osli_req_context	*req;

		req = tw_osli_get_request(sc);
		if (req == NULL) {
			tw_osli_dbg_dprintf(2, sc, "Cannot get request pkt.");
			/*
			 * Freeze the simq to maintain ccb ordering.  The next
			 * ccb that gets completed will unfreeze the simq.
			 */
			ccb_h->status &= ~CAM_SIM_QUEUED;
			ccb_h->status |= CAM_REQUEUE_REQ;
			xpt_done(ccb);
			break;
		}

		if ((tw_cl_is_reset_needed(&(req->ctlr->ctlr_handle)))) {
			ccb_h->status &= ~CAM_SIM_QUEUED;
			ccb_h->status |= CAM_REQUEUE_REQ;
			xpt_done(ccb);
			tw_osli_req_q_insert_tail(req, TW_OSLI_FREE_Q);
			break;
		}

		req->req_handle.osl_req_ctxt = req;
		req->req_handle.is_io = TW_CL_TRUE;
		req->orig_req = ccb;
		if (tw_osli_execute_scsi(req, ccb))
			tw_osli_req_q_insert_tail(req, TW_OSLI_FREE_Q);
		break;
	}

	case XPT_ABORT:
		tw_osli_dbg_dprintf(2, sc, "Abort request.");
		ccb_h->status = CAM_UA_ABORT;
		xpt_done(ccb);
		break;

	case XPT_RESET_BUS:
		tw_cl_create_event(&(sc->ctlr_handle), TW_CL_FALSE,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2108, 0x3, TW_CL_SEVERITY_INFO_STRING,
			"Received Reset Bus request from CAM",
			" ");

		tw_cl_set_reset_needed(&(sc->ctlr_handle));
		ccb_h->status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;

	case XPT_SET_TRAN_SETTINGS:
		tw_osli_dbg_dprintf(3, sc, "XPT_SET_TRAN_SETTINGS");

		/*
		 * This command is not supported, since it's very specific
		 * to SCSI, and we are doing ATA.
		 */
  		ccb_h->status = CAM_FUNC_NOTAVAIL;
  		xpt_done(ccb);
  		break;

	case XPT_GET_TRAN_SETTINGS: 
	{
		struct ccb_trans_settings	*cts = &ccb->cts;
		struct ccb_trans_settings_scsi *scsi =
		    &cts->proto_specific.scsi;
		struct ccb_trans_settings_spi *spi =
		    &cts->xport_specific.spi;

		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_2;
		cts->transport = XPORT_SPI;
		cts->transport_version = 2;

		spi->valid = CTS_SPI_VALID_DISC;
		spi->flags = CTS_SPI_FLAGS_DISC_ENB;
		scsi->valid = CTS_SCSI_VALID_TQ;
		scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;
		tw_osli_dbg_dprintf(3, sc, "XPT_GET_TRAN_SETTINGS");
		ccb_h->status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}

	case XPT_CALC_GEOMETRY:
		tw_osli_dbg_dprintf(3, sc, "XPT_CALC_GEOMETRY");
		cam_calc_geometry(&ccb->ccg, 1/* extended */);
		xpt_done(ccb);
		break;

	case XPT_PATH_INQ:    /* Path inquiry -- get twa properties */
	{
		struct ccb_pathinq	*path_inq = &ccb->cpi;

		tw_osli_dbg_dprintf(3, sc, "XPT_PATH_INQ request");

		path_inq->version_num = 1;
		path_inq->hba_inquiry = 0;
		path_inq->target_sprt = 0;
		path_inq->hba_misc = 0;
		path_inq->hba_eng_cnt = 0;
		path_inq->max_target = TW_CL_MAX_NUM_UNITS;
		path_inq->max_lun = TW_CL_MAX_NUM_LUNS - 1;
		path_inq->unit_number = cam_sim_unit(sim);
		path_inq->bus_id = cam_sim_bus(sim);
		path_inq->initiator_id = TW_CL_MAX_NUM_UNITS;
		path_inq->base_transfer_speed = 100000;
		strlcpy(path_inq->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(path_inq->hba_vid, "3ware", HBA_IDLEN);
		strlcpy(path_inq->dev_name, cam_sim_name(sim), DEV_IDLEN);
		path_inq->transport = XPORT_SPI;
		path_inq->transport_version = 2;
		path_inq->protocol = PROTO_SCSI;
		path_inq->protocol_version = SCSI_REV_2;
		path_inq->maxio = TW_CL_MAX_IO_SIZE;
		ccb_h->status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}

	default:
		tw_osli_dbg_dprintf(3, sc, "func_code = %x", ccb_h->func_code);
		ccb_h->status = CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	}
}



/*
 * Function name:	twa_poll
 * Description:		Driver entry point called when interrupts are not
 *			available.
 *
 * Input:		sim	-- sim corresponding to the controller
 * Output:		None
 * Return value:	None
 */
TW_VOID
twa_poll(struct cam_sim *sim)
{
	struct twa_softc *sc = (struct twa_softc *)(cam_sim_softc(sim));

	tw_osli_dbg_dprintf(3, sc, "entering; sc = %p", sc);
	tw_cl_interrupt(&(sc->ctlr_handle));
	tw_osli_dbg_dprintf(3, sc, "exiting; sc = %p", sc);
}



/*
 * Function name:	tw_osli_request_bus_scan
 * Description:		Requests CAM for a scan of the bus.
 *
 * Input:		sc	-- ptr to per ctlr structure
 * Output:		None
 * Return value:	0	-- success
 *			non-zero-- failure
 */
TW_INT32
tw_osli_request_bus_scan(struct twa_softc *sc)
{
	union ccb	*ccb;

	tw_osli_dbg_dprintf(3, sc, "entering");

	/* If we get here before sc->sim is initialized, return an error. */
	if (!(sc->sim))
		return(ENXIO);
	if ((ccb = xpt_alloc_ccb()) == NULL)
		return(ENOMEM);
	mtx_lock(sc->sim_lock);
	if (xpt_create_path(&ccb->ccb_h.path, NULL, cam_sim_path(sc->sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_free_ccb(ccb);
		mtx_unlock(sc->sim_lock);
		return(EIO);
	}

	xpt_rescan(ccb);
	mtx_unlock(sc->sim_lock);
	return(0);
}



/*
 * Function name:	tw_osli_disallow_new_requests
 * Description:		Calls the appropriate CAM function, so as to freeze
 *			the flow of new requests from CAM to this controller.
 *
 * Input:		sc	-- ptr to OSL internal ctlr context
 *			req_handle -- ptr to request handle sent by OSL.
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_osli_disallow_new_requests(struct twa_softc *sc,
	struct tw_cl_req_handle *req_handle)
{
	/* Only freeze/release the simq for IOs */
	if (req_handle->is_io) {
		struct tw_osli_req_context	*req = req_handle->osl_req_ctxt;
		union ccb			*ccb = (union ccb *)(req->orig_req);

		xpt_freeze_simq(sc->sim, 1);
		ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
	}
}



/*
 * Function name:	tw_osl_timeout
 * Description:		Call to timeout().
 *
 * Input:		req_handle -- ptr to request handle sent by OSL.
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_osl_timeout(struct tw_cl_req_handle *req_handle)
{
	struct tw_osli_req_context	*req = req_handle->osl_req_ctxt;
	union ccb			*ccb = (union ccb *)(req->orig_req);
	struct ccb_hdr			*ccb_h = &(ccb->ccb_h);

	req->deadline = tw_osl_get_local_time() + (ccb_h->timeout / 1000);
}



/*
 * Function name:	tw_osl_untimeout
 * Description:		Inverse of call to timeout().
 *
 * Input:		req_handle -- ptr to request handle sent by OSL.
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_osl_untimeout(struct tw_cl_req_handle *req_handle)
{
	struct tw_osli_req_context	*req = req_handle->osl_req_ctxt;

	req->deadline = 0;
}



/*
 * Function name:	tw_osl_scan_bus
 * Description:		CL calls this function to request for a bus scan.
 *
 * Input:		ctlr_handle	-- ptr to controller handle
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_osl_scan_bus(struct tw_cl_ctlr_handle *ctlr_handle)
{
	struct twa_softc	*sc = ctlr_handle->osl_ctlr_ctxt;
	TW_INT32		error;

	if ((error = tw_osli_request_bus_scan(sc)))
		tw_osli_printf(sc, "error = %d",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x2109,
			"Bus scan request to CAM failed",
			error);
}



/*
 * Function name:	tw_osl_complete_io
 * Description:		Called to complete CAM scsi requests.
 *
 * Input:		req_handle	-- ptr to request handle
 * Output:		None
 * Return value:	None
 */
TW_VOID
tw_osl_complete_io(struct tw_cl_req_handle *req_handle)
{
	struct tw_osli_req_context	*req = req_handle->osl_req_ctxt;
	struct tw_cl_req_packet		*req_pkt =
		(struct tw_cl_req_packet *)(&req->req_pkt);
	struct tw_cl_scsi_req_packet	*scsi_req;
	struct twa_softc		*sc = req->ctlr;
	union ccb			*ccb = (union ccb *)(req->orig_req);

	tw_osli_dbg_dprintf(10, sc, "entering");

	if (req->state != TW_OSLI_REQ_STATE_BUSY)
		tw_osli_printf(sc, "request = %p, status = %d",
			TW_CL_SEVERITY_ERROR_STRING,
			TW_CL_MESSAGE_SOURCE_FREEBSD_DRIVER,
			0x210A,
			"Unposted command completed!!",
			req, req->state);

	/*
	 * Remove request from the busy queue.  Just mark it complete.
	 * There's no need to move it into the complete queue as we are
	 * going to be done with it right now.
	 */
	req->state = TW_OSLI_REQ_STATE_COMPLETE;
	tw_osli_req_q_remove_item(req, TW_OSLI_BUSY_Q);

	tw_osli_unmap_request(req);

	req->deadline = 0;
	if (req->error_code) {
		/* This request never got submitted to the firmware. */
		if (req->error_code == EBUSY) {
			/*
			 * Cmd queue is full, or the Common Layer is out of
			 * resources.  The simq will already have been frozen.
			 * When this ccb gets completed will unfreeze the simq.
			 */
			ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		}
		else if (req->error_code == EFBIG)
			ccb->ccb_h.status = CAM_REQ_TOO_BIG;
		else
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
	} else {
		scsi_req = &(req_pkt->gen_req_pkt.scsi_req);
		if (req_pkt->status == TW_CL_ERR_REQ_SUCCESS)
			ccb->ccb_h.status = CAM_REQ_CMP;
		else {
			if (req_pkt->status & TW_CL_ERR_REQ_INVALID_TARGET)
				ccb->ccb_h.status |= CAM_SEL_TIMEOUT;
			else if (req_pkt->status & TW_CL_ERR_REQ_INVALID_LUN)
				ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
			else if (req_pkt->status & TW_CL_ERR_REQ_SCSI_ERROR)
				ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
			else if (req_pkt->status & TW_CL_ERR_REQ_BUS_RESET)
				ccb->ccb_h.status |= (CAM_REQUEUE_REQ | CAM_SCSI_BUS_RESET);
			/*
			 * If none of the above errors occurred, simply
			 * mark completion error.
			 */
			if (ccb->ccb_h.status == 0)
				ccb->ccb_h.status = CAM_REQ_CMP_ERR;

			if (req_pkt->status & TW_CL_ERR_REQ_AUTO_SENSE_VALID) {
				ccb->csio.sense_len = scsi_req->sense_len;
				ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
			}
		}

		ccb->csio.scsi_status = scsi_req->scsi_status;
	}

	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
	mtx_lock(sc->sim_lock);
	xpt_done(ccb);
	mtx_unlock(sc->sim_lock);
	if (! req->error_code)
		 /* twa_action will free the request otherwise */
		tw_osli_req_q_insert_tail(req, TW_OSLI_FREE_Q);
}

