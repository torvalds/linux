/*-
 * Bus independent FreeBSD shim for the aic7xxx based Adaptec SCSI controllers
 *
 * Copyright (c) 1994-2001 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU Public License ("GPL").
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
 *
 * $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/aic7xxx_osm.c#20 $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/aic7xxx/aic7xxx_osm.h>
#include <dev/aic7xxx/aic7xxx_inline.h>

#include <sys/kthread.h>

#ifndef AHC_TMODE_ENABLE
#define AHC_TMODE_ENABLE 0
#endif

#include <dev/aic7xxx/aic_osm_lib.c>

#define ccb_scb_ptr spriv_ptr0

devclass_t ahc_devclass;

#if 0
static void	ahc_dump_targcmd(struct target_cmd *cmd);
#endif
static int	ahc_modevent(module_t mod, int type, void *data);
static void	ahc_action(struct cam_sim *sim, union ccb *ccb);
static void	ahc_get_tran_settings(struct ahc_softc *ahc,
				      int our_id, char channel,
				      struct ccb_trans_settings *cts);
static void	ahc_async(void *callback_arg, uint32_t code,
			  struct cam_path *path, void *arg);
static void	ahc_execute_scb(void *arg, bus_dma_segment_t *dm_segs,
				int nsegments, int error);
static void	ahc_poll(struct cam_sim *sim);
static void	ahc_setup_data(struct ahc_softc *ahc, struct cam_sim *sim,
			       struct ccb_scsiio *csio, struct scb *scb);
static void	ahc_abort_ccb(struct ahc_softc *ahc, struct cam_sim *sim,
			      union ccb *ccb);
static int	ahc_create_path(struct ahc_softc *ahc,
				char channel, u_int target, u_int lun,
				struct cam_path **path);


static int
ahc_create_path(struct ahc_softc *ahc, char channel, u_int target,
	        u_int lun, struct cam_path **path)
{
	path_id_t path_id;

	if (channel == 'B')
		path_id = cam_sim_path(ahc->platform_data->sim_b);
	else 
		path_id = cam_sim_path(ahc->platform_data->sim);

	return (xpt_create_path(path, /*periph*/NULL,
				path_id, target, lun));
}

int
ahc_map_int(struct ahc_softc *ahc)
{
	int error;
	int zero;
	int shareable;

	zero = 0;
	shareable = (ahc->flags & AHC_EDGE_INTERRUPT) ? 0: RF_SHAREABLE;
	ahc->platform_data->irq =
	    bus_alloc_resource_any(ahc->dev_softc, SYS_RES_IRQ, &zero,
				   RF_ACTIVE | shareable);
	if (ahc->platform_data->irq == NULL) {
		device_printf(ahc->dev_softc,
			      "bus_alloc_resource() failed to allocate IRQ\n");
		return (ENOMEM);
	}
	ahc->platform_data->irq_res_type = SYS_RES_IRQ;

	/* Hook up our interrupt handler */
	error = bus_setup_intr(ahc->dev_softc, ahc->platform_data->irq,
			       INTR_TYPE_CAM|INTR_MPSAFE, NULL, 
			       ahc_platform_intr, ahc, &ahc->platform_data->ih);

	if (error != 0)
		device_printf(ahc->dev_softc, "bus_setup_intr() failed: %d\n",
			      error);
	return (error);
}

int
aic7770_map_registers(struct ahc_softc *ahc, u_int unused_ioport_arg)
{
	struct	resource *regs;
	int	rid;

	rid = 0;
	regs = bus_alloc_resource_any(ahc->dev_softc, SYS_RES_IOPORT, &rid,
				      RF_ACTIVE);
	if (regs == NULL) {
		device_printf(ahc->dev_softc, "Unable to map I/O space?!\n");
		return ENOMEM;
	}
	ahc->platform_data->regs_res_type = SYS_RES_IOPORT;
	ahc->platform_data->regs_res_id = rid;
	ahc->platform_data->regs = regs;
	ahc->tag = rman_get_bustag(regs);
	ahc->bsh = rman_get_bushandle(regs);
	return (0);
}

/*
 * Attach all the sub-devices we can find
 */
int
ahc_attach(struct ahc_softc *ahc)
{
	char   ahc_info[256];
	struct ccb_setasync csa;
	struct cam_devq *devq;
	int bus_id;
	int bus_id2;
	struct cam_sim *sim;
	struct cam_sim *sim2;
	struct cam_path *path;
	struct cam_path *path2;
	int count;

	count = 0;
	sim = NULL;
	sim2 = NULL;
	path = NULL;
	path2 = NULL;


	/*
	 * Create a thread to perform all recovery.
	 */
	if (ahc_spawn_recovery_thread(ahc) != 0)
		goto fail;

	ahc_controller_info(ahc, ahc_info);
	printf("%s\n", ahc_info);
	ahc_lock(ahc);

	/*
	 * Attach secondary channel first if the user has
	 * declared it the primary channel.
	 */
	if ((ahc->features & AHC_TWIN) != 0
	 && (ahc->flags & AHC_PRIMARY_CHANNEL) != 0) {
		bus_id = 1;
		bus_id2 = 0;
	} else {
		bus_id = 0;
		bus_id2 = 1;
	}

	/*
	 * Create the device queue for our SIM(s).
	 */
	devq = cam_simq_alloc(AHC_MAX_QUEUE);
	if (devq == NULL)
		goto fail;

	/*
	 * Construct our first channel SIM entry
	 */
	sim = cam_sim_alloc(ahc_action, ahc_poll, "ahc", ahc,
			    device_get_unit(ahc->dev_softc),
			    &ahc->platform_data->mtx, 1, AHC_MAX_QUEUE, devq);
	if (sim == NULL) {
		cam_simq_free(devq);
		goto fail;
	}

	if (xpt_bus_register(sim, ahc->dev_softc, bus_id) != CAM_SUCCESS) {
		cam_sim_free(sim, /*free_devq*/TRUE);
		sim = NULL;
		goto fail;
	}
	
	if (xpt_create_path(&path, /*periph*/NULL,
			    cam_sim_path(sim), CAM_TARGET_WILDCARD,
			    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path(sim));
		cam_sim_free(sim, /*free_devq*/TRUE);
		sim = NULL;
		goto fail;
	}
		
	xpt_setup_ccb(&csa.ccb_h, path, /*priority*/5);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = AC_LOST_DEVICE;
	csa.callback = ahc_async;
	csa.callback_arg = sim;
	xpt_action((union ccb *)&csa);
	count++;

	if (ahc->features & AHC_TWIN) {
		sim2 = cam_sim_alloc(ahc_action, ahc_poll, "ahc",
				    ahc, device_get_unit(ahc->dev_softc),
				    &ahc->platform_data->mtx, 1,
				    AHC_MAX_QUEUE, devq);

		if (sim2 == NULL) {
			printf("ahc_attach: Unable to attach second "
			       "bus due to resource shortage");
			goto fail;
		}
		
		if (xpt_bus_register(sim2, ahc->dev_softc, bus_id2) !=
		    CAM_SUCCESS) {
			printf("ahc_attach: Unable to attach second "
			       "bus due to resource shortage");
			/*
			 * We do not want to destroy the device queue
			 * because the first bus is using it.
			 */
			cam_sim_free(sim2, /*free_devq*/FALSE);
			goto fail;
		}

		if (xpt_create_path(&path2, /*periph*/NULL,
				    cam_sim_path(sim2),
				    CAM_TARGET_WILDCARD,
				    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
			xpt_bus_deregister(cam_sim_path(sim2));
			cam_sim_free(sim2, /*free_devq*/FALSE);
			sim2 = NULL;
			goto fail;
		}
		xpt_setup_ccb(&csa.ccb_h, path2, /*priority*/5);
		csa.ccb_h.func_code = XPT_SASYNC_CB;
		csa.event_enable = AC_LOST_DEVICE;
		csa.callback = ahc_async;
		csa.callback_arg = sim2;
		xpt_action((union ccb *)&csa);
		count++;
	}

fail:
	if ((ahc->features & AHC_TWIN) != 0
	 && (ahc->flags & AHC_PRIMARY_CHANNEL) != 0) {
		ahc->platform_data->sim_b = sim;
		ahc->platform_data->path_b = path;
		ahc->platform_data->sim = sim2;
		ahc->platform_data->path = path2;
	} else {
		ahc->platform_data->sim = sim;
		ahc->platform_data->path = path;
		ahc->platform_data->sim_b = sim2;
		ahc->platform_data->path_b = path2;
	}
	ahc_unlock(ahc);

	if (count != 0) {
		/* We have to wait until after any system dumps... */
		ahc->platform_data->eh =
		    EVENTHANDLER_REGISTER(shutdown_final, ahc_shutdown,
					  ahc, SHUTDOWN_PRI_DEFAULT);
		ahc_intr_enable(ahc, TRUE);
	}

	return (count);
}

/*
 * Catch an interrupt from the adapter
 */
void
ahc_platform_intr(void *arg)
{
	struct	ahc_softc *ahc;

	ahc = (struct ahc_softc *)arg; 
	ahc_lock(ahc);
	ahc_intr(ahc);
	ahc_unlock(ahc);
}

/*
 * We have an scb which has been processed by the
 * adaptor, now we look to see how the operation
 * went.
 */
void
ahc_done(struct ahc_softc *ahc, struct scb *scb)
{
	union ccb *ccb;

	CAM_DEBUG(scb->io_ctx->ccb_h.path, CAM_DEBUG_TRACE,
		  ("ahc_done - scb %d\n", scb->hscb->tag));

	ccb = scb->io_ctx;
	LIST_REMOVE(scb, pending_links);
	if ((scb->flags & SCB_TIMEDOUT) != 0)
		LIST_REMOVE(scb, timedout_links);
	if ((scb->flags & SCB_UNTAGGEDQ) != 0) {
		struct scb_tailq *untagged_q;
		int target_offset;

		target_offset = SCB_GET_TARGET_OFFSET(ahc, scb);
		untagged_q = &ahc->untagged_queues[target_offset];
		TAILQ_REMOVE(untagged_q, scb, links.tqe);
		scb->flags &= ~SCB_UNTAGGEDQ;
		ahc_run_untagged_queue(ahc, untagged_q);
	}

	callout_stop(&scb->io_timer);

	if ((ccb->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		bus_dmasync_op_t op;

		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			op = BUS_DMASYNC_POSTREAD;
		else
			op = BUS_DMASYNC_POSTWRITE;
		bus_dmamap_sync(ahc->buffer_dmat, scb->dmamap, op);
		bus_dmamap_unload(ahc->buffer_dmat, scb->dmamap);
	}

	if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
		struct cam_path *ccb_path;

		/*
		 * If we have finally disconnected, clean up our
		 * pending device state.
		 * XXX - There may be error states that cause where
		 *       we will remain connected.
		 */
		ccb_path = ccb->ccb_h.path;
		if (ahc->pending_device != NULL
		 && xpt_path_comp(ahc->pending_device->path, ccb_path) == 0) {

			if ((ccb->ccb_h.flags & CAM_SEND_STATUS) != 0) {
				ahc->pending_device = NULL;
			} else {
				if (bootverbose) {
					xpt_print_path(ccb->ccb_h.path);
					printf("Still connected\n");
				}
				aic_freeze_ccb(ccb);
			}
		}

		if (aic_get_transaction_status(scb) == CAM_REQ_INPROG)
			ccb->ccb_h.status |= CAM_REQ_CMP;
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ahc_free_scb(ahc, scb);
		xpt_done(ccb);
		return;
	}

	/*
	 * If the recovery SCB completes, we have to be
	 * out of our timeout.
	 */
	if ((scb->flags & SCB_RECOVERY_SCB) != 0) {
		struct	scb *list_scb;

		ahc->scb_data->recovery_scbs--;

		if (aic_get_transaction_status(scb) == CAM_BDR_SENT
		 || aic_get_transaction_status(scb) == CAM_REQ_ABORTED)
			aic_set_transaction_status(scb, CAM_CMD_TIMEOUT);

		if (ahc->scb_data->recovery_scbs == 0) {
			/*
			 * All recovery actions have completed successfully,
			 * so reinstate the timeouts for all other pending
			 * commands.
			 */
			LIST_FOREACH(list_scb, &ahc->pending_scbs,
				     pending_links) {

				aic_scb_timer_reset(list_scb,
						    aic_get_timeout(scb));
			}

			ahc_print_path(ahc, scb);
			printf("no longer in timeout, status = %x\n",
			       ccb->ccb_h.status);
		}
	}

	/* Don't clobber any existing error state */
	if (aic_get_transaction_status(scb) == CAM_REQ_INPROG) {
		ccb->ccb_h.status |= CAM_REQ_CMP;
	} else if ((scb->flags & SCB_SENSE) != 0) {
		/*
		 * We performed autosense retrieval.
		 *
		 * Zero any sense not transferred by the
		 * device.  The SCSI spec mandates that any
		 * untransfered data should be assumed to be
		 * zero.  Complete the 'bounce' of sense information
		 * through buffers accessible via bus-space by
		 * copying it into the clients csio.
		 */
		memset(&ccb->csio.sense_data, 0, sizeof(ccb->csio.sense_data));
		memcpy(&ccb->csio.sense_data,
		       ahc_get_sense_buf(ahc, scb),
		       (aic_le32toh(scb->sg_list->len) & AHC_SG_LEN_MASK)
		       - ccb->csio.sense_resid);
		scb->io_ctx->ccb_h.status |= CAM_AUTOSNS_VALID;
	}
	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
	ahc_free_scb(ahc, scb);
	xpt_done(ccb);
}

static void
ahc_action(struct cam_sim *sim, union ccb *ccb)
{
	struct	ahc_softc *ahc;
	struct	ahc_tmode_lstate *lstate;
	u_int	target_id;
	u_int	our_id;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE, ("ahc_action\n"));
	
	ahc = (struct ahc_softc *)cam_sim_softc(sim);

	target_id = ccb->ccb_h.target_id;
	our_id = SIM_SCSI_ID(ahc, sim);
	
	switch (ccb->ccb_h.func_code) {
	/* Common cases first */
	case XPT_ACCEPT_TARGET_IO:	/* Accept Host Target Mode CDB */
	case XPT_CONT_TARGET_IO:/* Continue Host Target I/O Connection*/
	{
		struct	   ahc_tmode_tstate *tstate;
		cam_status status;

		status = ahc_find_tmode_devs(ahc, sim, ccb, &tstate,
					     &lstate, TRUE);

		if (status != CAM_REQ_CMP) {
			if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
				/* Response from the black hole device */
				tstate = NULL;
				lstate = ahc->black_hole;
			} else {
				ccb->ccb_h.status = status;
				xpt_done(ccb);
				break;
			}
		}
		if (ccb->ccb_h.func_code == XPT_ACCEPT_TARGET_IO) {

			SLIST_INSERT_HEAD(&lstate->accept_tios, &ccb->ccb_h,
					  sim_links.sle);
			ccb->ccb_h.status = CAM_REQ_INPROG;
			if ((ahc->flags & AHC_TQINFIFO_BLOCKED) != 0)
				ahc_run_tqinfifo(ahc, /*paused*/FALSE);
			break;
		}

		/*
		 * The target_id represents the target we attempt to
		 * select.  In target mode, this is the initiator of
		 * the original command.
		 */
		our_id = target_id;
		target_id = ccb->csio.init_id;
		/* FALLTHROUGH */
	}
	case XPT_SCSI_IO:	/* Execute the requested I/O operation */
	case XPT_RESET_DEV:	/* Bus Device Reset the specified SCSI device */
	{
		struct	scb *scb;
		struct	hardware_scb *hscb;	

		if ((ahc->flags & AHC_INITIATORROLE) == 0
		 && (ccb->ccb_h.func_code == XPT_SCSI_IO
		  || ccb->ccb_h.func_code == XPT_RESET_DEV)) {
			ccb->ccb_h.status = CAM_PROVIDE_FAIL;
			xpt_done(ccb);
			return;
		}

		/*
		 * get an scb to use.
		 */
		if ((scb = ahc_get_scb(ahc)) == NULL) {
	
			xpt_freeze_simq(sim, /*count*/1);
			ahc->flags |= AHC_RESOURCE_SHORTAGE;
			ccb->ccb_h.status = CAM_REQUEUE_REQ;
			xpt_done(ccb);
			return;
		}
		
		hscb = scb->hscb;
		
		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_SUBTRACE,
			  ("start scb(%p)\n", scb));
		scb->io_ctx = ccb;
		/*
		 * So we can find the SCB when an abort is requested
		 */
		ccb->ccb_h.ccb_scb_ptr = scb;

		/*
		 * Put all the arguments for the xfer in the scb
		 */
		hscb->control = 0;
		hscb->scsiid = BUILD_SCSIID(ahc, sim, target_id, our_id);
		hscb->lun = ccb->ccb_h.target_lun;
		if (ccb->ccb_h.func_code == XPT_RESET_DEV) {
			hscb->cdb_len = 0;
			scb->flags |= SCB_DEVICE_RESET;
			hscb->control |= MK_MESSAGE;
			ahc_execute_scb(scb, NULL, 0, 0);
		} else {
			if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
				struct target_data *tdata;

				tdata = &hscb->shared_data.tdata;
				if (ahc->pending_device == lstate)
					scb->flags |= SCB_TARGET_IMMEDIATE;
				hscb->control |= TARGET_SCB;
				scb->flags |= SCB_TARGET_SCB;
				tdata->target_phases = 0;
				if ((ccb->ccb_h.flags & CAM_SEND_STATUS) != 0) {
					tdata->target_phases |= SPHASE_PENDING;
					tdata->scsi_status =
					    ccb->csio.scsi_status;
				}
	 			if (ccb->ccb_h.flags & CAM_DIS_DISCONNECT)
					tdata->target_phases |= NO_DISCONNECT;

				tdata->initiator_tag = ccb->csio.tag_id;
			}
			if (ccb->ccb_h.flags & CAM_TAG_ACTION_VALID)
				hscb->control |= ccb->csio.tag_action;
			
			ahc_setup_data(ahc, sim, &ccb->csio, scb);
		}
		break;
	}
	case XPT_NOTIFY_ACKNOWLEDGE:
	case XPT_IMMEDIATE_NOTIFY:
	{
		struct	   ahc_tmode_tstate *tstate;
		struct	   ahc_tmode_lstate *lstate;
		cam_status status;

		status = ahc_find_tmode_devs(ahc, sim, ccb, &tstate,
					     &lstate, TRUE);

		if (status != CAM_REQ_CMP) {
			ccb->ccb_h.status = status;
			xpt_done(ccb);
			break;
		}
		SLIST_INSERT_HEAD(&lstate->immed_notifies, &ccb->ccb_h,
				  sim_links.sle);
		ccb->ccb_h.status = CAM_REQ_INPROG;
		ahc_send_lstate_events(ahc, lstate);
		break;
	}
	case XPT_EN_LUN:		/* Enable LUN as a target */
		ahc_handle_en_lun(ahc, sim, ccb);
		xpt_done(ccb);
		break;
	case XPT_ABORT:			/* Abort the specified CCB */
	{
		ahc_abort_ccb(ahc, sim, ccb);
		break;
	}
	case XPT_SET_TRAN_SETTINGS:
	{
		struct	ahc_devinfo devinfo;
		struct	ccb_trans_settings *cts;
		struct	ccb_trans_settings_scsi *scsi;
		struct	ccb_trans_settings_spi *spi;
		struct	ahc_initiator_tinfo *tinfo;
		struct	ahc_tmode_tstate *tstate;
		uint16_t *discenable;
		uint16_t *tagenable;
		u_int	update_type;

		cts = &ccb->cts;
		scsi = &cts->proto_specific.scsi;
		spi = &cts->xport_specific.spi;
		ahc_compile_devinfo(&devinfo, SIM_SCSI_ID(ahc, sim),
				    cts->ccb_h.target_id,
				    cts->ccb_h.target_lun,
				    SIM_CHANNEL(ahc, sim),
				    ROLE_UNKNOWN);
		tinfo = ahc_fetch_transinfo(ahc, devinfo.channel,
					    devinfo.our_scsiid,
					    devinfo.target, &tstate);
		update_type = 0;
		if (cts->type == CTS_TYPE_CURRENT_SETTINGS) {
			update_type |= AHC_TRANS_GOAL;
			discenable = &tstate->discenable;
			tagenable = &tstate->tagenable;
			tinfo->curr.protocol_version =
			    cts->protocol_version;
			tinfo->curr.transport_version =
			    cts->transport_version;
			tinfo->goal.protocol_version =
			    cts->protocol_version;
			tinfo->goal.transport_version =
			    cts->transport_version;
		} else if (cts->type == CTS_TYPE_USER_SETTINGS) {
			update_type |= AHC_TRANS_USER;
			discenable = &ahc->user_discenable;
			tagenable = &ahc->user_tagenable;
			tinfo->user.protocol_version =
			    cts->protocol_version;
			tinfo->user.transport_version =
			    cts->transport_version;
		} else {
			ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(ccb);
			break;
		}
		
		if ((spi->valid & CTS_SPI_VALID_DISC) != 0) {
			if ((spi->flags & CTS_SPI_FLAGS_DISC_ENB) != 0)
				*discenable |= devinfo.target_mask;
			else
				*discenable &= ~devinfo.target_mask;
		}
		
		if ((scsi->valid & CTS_SCSI_VALID_TQ) != 0) {
			if ((scsi->flags & CTS_SCSI_FLAGS_TAG_ENB) != 0)
				*tagenable |= devinfo.target_mask;
			else
				*tagenable &= ~devinfo.target_mask;
		}	

		if ((spi->valid & CTS_SPI_VALID_BUS_WIDTH) != 0) {
			ahc_validate_width(ahc, /*tinfo limit*/NULL,
					   &spi->bus_width, ROLE_UNKNOWN);
			ahc_set_width(ahc, &devinfo, spi->bus_width,
				      update_type, /*paused*/FALSE);
		}

		if ((spi->valid & CTS_SPI_VALID_PPR_OPTIONS) == 0) {
			if (update_type == AHC_TRANS_USER)
				spi->ppr_options = tinfo->user.ppr_options;
			else
				spi->ppr_options = tinfo->goal.ppr_options;
		}

		if ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) == 0) {
			if (update_type == AHC_TRANS_USER)
				spi->sync_offset = tinfo->user.offset;
			else
				spi->sync_offset = tinfo->goal.offset;
		}

		if ((spi->valid & CTS_SPI_VALID_SYNC_RATE) == 0) {
			if (update_type == AHC_TRANS_USER)
				spi->sync_period = tinfo->user.period;
			else
				spi->sync_period = tinfo->goal.period;
		}

		if (((spi->valid & CTS_SPI_VALID_SYNC_RATE) != 0)
		 || ((spi->valid & CTS_SPI_VALID_SYNC_OFFSET) != 0)) {
			struct ahc_syncrate *syncrate;
			u_int maxsync;

			if ((ahc->features & AHC_ULTRA2) != 0)
				maxsync = AHC_SYNCRATE_DT;
			else if ((ahc->features & AHC_ULTRA) != 0)
				maxsync = AHC_SYNCRATE_ULTRA;
			else
				maxsync = AHC_SYNCRATE_FAST;

			if (spi->bus_width != MSG_EXT_WDTR_BUS_16_BIT)
				spi->ppr_options &= ~MSG_EXT_PPR_DT_REQ;

			syncrate = ahc_find_syncrate(ahc, &spi->sync_period,
						     &spi->ppr_options,
						     maxsync);
			ahc_validate_offset(ahc, /*tinfo limit*/NULL,
					    syncrate, &spi->sync_offset,
					    spi->bus_width, ROLE_UNKNOWN);

			/* We use a period of 0 to represent async */
			if (spi->sync_offset == 0) {
				spi->sync_period = 0;
				spi->ppr_options = 0;
			}

			ahc_set_syncrate(ahc, &devinfo, syncrate,
					 spi->sync_period, spi->sync_offset,
					 spi->ppr_options, update_type,
					 /*paused*/FALSE);
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	/* Get default/user set transfer settings for the target */
	{

		ahc_get_tran_settings(ahc, SIM_SCSI_ID(ahc, sim),
				      SIM_CHANNEL(ahc, sim), &ccb->cts);
		xpt_done(ccb);
		break;
	}
	case XPT_CALC_GEOMETRY:
	{
		int extended;

		extended = SIM_IS_SCSIBUS_B(ahc, sim)
			 ? ahc->flags & AHC_EXTENDED_TRANS_B
			 : ahc->flags & AHC_EXTENDED_TRANS_A;
		aic_calc_geometry(&ccb->ccg, extended);
		xpt_done(ccb);
		break;
	}
	case XPT_RESET_BUS:		/* Reset the specified SCSI bus */
	{
		int  found;
		
		found = ahc_reset_channel(ahc, SIM_CHANNEL(ahc, sim),
					  /*initiate reset*/TRUE);
		if (bootverbose) {
			xpt_print_path(SIM_PATH(ahc, sim));
			printf("SCSI bus reset delivered. "
			       "%d SCBs aborted.\n", found);
		}
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	case XPT_TERM_IO:		/* Terminate the I/O process */
		/* XXX Implement */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi = &ccb->cpi;
		
		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE|PI_TAG_ABLE;
		if ((ahc->features & AHC_WIDE) != 0)
			cpi->hba_inquiry |= PI_WIDE_16;
		if ((ahc->features & AHC_TARGETMODE) != 0) {
			cpi->target_sprt = PIT_PROCESSOR
					 | PIT_DISCONNECT
					 | PIT_TERM_IO;
		} else {
			cpi->target_sprt = 0;
		}
		cpi->hba_misc = 0;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = (ahc->features & AHC_WIDE) ? 15 : 7;
		cpi->max_lun = AHC_NUM_LUNS - 1;
		if (SIM_IS_SCSIBUS_B(ahc, sim)) {
			cpi->initiator_id = ahc->our_id_b;
			if ((ahc->flags & AHC_RESET_BUS_B) == 0)
				cpi->hba_misc |= PIM_NOBUSRESET;
		} else {
			cpi->initiator_id = ahc->our_id;
			if ((ahc->flags & AHC_RESET_BUS_A) == 0)
				cpi->hba_misc |= PIM_NOBUSRESET;
		}
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 3300;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "Adaptec", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->xport_specific.spi.ppr_options = SID_SPI_CLOCK_ST;
		if ((ahc->features & AHC_DT) != 0) {
			cpi->transport_version = 3;
			cpi->xport_specific.spi.ppr_options =
			    SID_SPI_CLOCK_DT_ST;
		}
		cpi->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	default:
		ccb->ccb_h.status = CAM_PROVIDE_FAIL;
		xpt_done(ccb);
		break;
	}
}

static void
ahc_get_tran_settings(struct ahc_softc *ahc, int our_id, char channel,
		      struct ccb_trans_settings *cts)
{
	struct	ahc_devinfo devinfo;
	struct	ccb_trans_settings_scsi *scsi;
	struct	ccb_trans_settings_spi *spi;
	struct	ahc_initiator_tinfo *targ_info;
	struct	ahc_tmode_tstate *tstate;
	struct	ahc_transinfo *tinfo;

	scsi = &cts->proto_specific.scsi;
	spi = &cts->xport_specific.spi;
	ahc_compile_devinfo(&devinfo, our_id,
			    cts->ccb_h.target_id,
			    cts->ccb_h.target_lun,
			    channel, ROLE_UNKNOWN);
	targ_info = ahc_fetch_transinfo(ahc, devinfo.channel,
					devinfo.our_scsiid,
					devinfo.target, &tstate);
	
	if (cts->type == CTS_TYPE_CURRENT_SETTINGS)
		tinfo = &targ_info->curr;
	else
		tinfo = &targ_info->user;
	
	scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
	spi->flags &= ~CTS_SPI_FLAGS_DISC_ENB;
	if (cts->type == CTS_TYPE_USER_SETTINGS) {
		if ((ahc->user_discenable & devinfo.target_mask) != 0)
			spi->flags |= CTS_SPI_FLAGS_DISC_ENB;

		if ((ahc->user_tagenable & devinfo.target_mask) != 0)
			scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
	} else {
		if ((tstate->discenable & devinfo.target_mask) != 0)
			spi->flags |= CTS_SPI_FLAGS_DISC_ENB;

		if ((tstate->tagenable & devinfo.target_mask) != 0)
			scsi->flags |= CTS_SCSI_FLAGS_TAG_ENB;
	}
	cts->protocol_version = tinfo->protocol_version;
	cts->transport_version = tinfo->transport_version;

	spi->sync_period = tinfo->period;
	spi->sync_offset = tinfo->offset;
	spi->bus_width = tinfo->width;
	spi->ppr_options = tinfo->ppr_options;
	
	cts->protocol = PROTO_SCSI;
	cts->transport = XPORT_SPI;
	spi->valid = CTS_SPI_VALID_SYNC_RATE
		   | CTS_SPI_VALID_SYNC_OFFSET
		   | CTS_SPI_VALID_BUS_WIDTH
		   | CTS_SPI_VALID_PPR_OPTIONS;

	if (cts->ccb_h.target_lun != CAM_LUN_WILDCARD) {
		scsi->valid = CTS_SCSI_VALID_TQ;
		spi->valid |= CTS_SPI_VALID_DISC;
	} else {
		scsi->valid = 0;
	}

	cts->ccb_h.status = CAM_REQ_CMP;
}

static void
ahc_async(void *callback_arg, uint32_t code, struct cam_path *path, void *arg)
{
	struct ahc_softc *ahc;
	struct cam_sim *sim;

	sim = (struct cam_sim *)callback_arg;
	ahc = (struct ahc_softc *)cam_sim_softc(sim);
	switch (code) {
	case AC_LOST_DEVICE:
	{
		struct	ahc_devinfo devinfo;

		ahc_compile_devinfo(&devinfo, SIM_SCSI_ID(ahc, sim),
				    xpt_path_target_id(path),
				    xpt_path_lun_id(path),
				    SIM_CHANNEL(ahc, sim),
				    ROLE_UNKNOWN);

		/*
		 * Revert to async/narrow transfers
		 * for the next device.
		 */
		ahc_set_width(ahc, &devinfo, MSG_EXT_WDTR_BUS_8_BIT,
			      AHC_TRANS_GOAL|AHC_TRANS_CUR, /*paused*/FALSE);
		ahc_set_syncrate(ahc, &devinfo, /*syncrate*/NULL,
				 /*period*/0, /*offset*/0, /*ppr_options*/0,
				 AHC_TRANS_GOAL|AHC_TRANS_CUR,
				 /*paused*/FALSE);
		break;
	}
	default:
		break;
	}
}

static void
ahc_execute_scb(void *arg, bus_dma_segment_t *dm_segs, int nsegments,
		int error)
{
	struct	scb *scb;
	union	ccb *ccb;
	struct	ahc_softc *ahc;
	struct	ahc_initiator_tinfo *tinfo;
	struct	ahc_tmode_tstate *tstate;
	u_int	mask;

	scb = (struct scb *)arg;
	ccb = scb->io_ctx;
	ahc = scb->ahc_softc;

	if (error != 0) {
		if (error == EFBIG)
			aic_set_transaction_status(scb, CAM_REQ_TOO_BIG);
		else
			aic_set_transaction_status(scb, CAM_REQ_CMP_ERR);
		if (nsegments != 0)
			bus_dmamap_unload(ahc->buffer_dmat, scb->dmamap);
		ahc_free_scb(ahc, scb);
		xpt_done(ccb);
		return;
	}
	if (nsegments != 0) {
		struct	  ahc_dma_seg *sg;
		bus_dma_segment_t *end_seg;
		bus_dmasync_op_t op;

		end_seg = dm_segs + nsegments;

		/* Copy the segments into our SG list */
		sg = scb->sg_list;
		while (dm_segs < end_seg) {
			uint32_t len;

			sg->addr = aic_htole32(dm_segs->ds_addr);
			len = dm_segs->ds_len
			    | ((dm_segs->ds_addr >> 8) & 0x7F000000);
			sg->len = aic_htole32(len);
			sg++;
			dm_segs++;
		}
		
		/*
		 * Note where to find the SG entries in bus space.
		 * We also set the full residual flag which the 
		 * sequencer will clear as soon as a data transfer
		 * occurs.
		 */
		scb->hscb->sgptr = aic_htole32(scb->sg_list_phys|SG_FULL_RESID);

		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
			op = BUS_DMASYNC_PREREAD;
		else
			op = BUS_DMASYNC_PREWRITE;

		bus_dmamap_sync(ahc->buffer_dmat, scb->dmamap, op);

		if (ccb->ccb_h.func_code == XPT_CONT_TARGET_IO) {
			struct target_data *tdata;

			tdata = &scb->hscb->shared_data.tdata;
			tdata->target_phases |= DPHASE_PENDING;
			/*
			 * CAM data direction is relative to the initiator.
			 */
			if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
				tdata->data_phase = P_DATAOUT;
			else
				tdata->data_phase = P_DATAIN;

			/*
			 * If the transfer is of an odd length and in the
			 * "in" direction (scsi->HostBus), then it may
			 * trigger a bug in the 'WideODD' feature of
			 * non-Ultra2 chips.  Force the total data-length
			 * to be even by adding an extra, 1 byte, SG,
			 * element.  We do this even if we are not currently
			 * negotiated wide as negotiation could occur before
			 * this command is executed.
			 */
			if ((ahc->bugs & AHC_TMODE_WIDEODD_BUG) != 0
			 && (ccb->csio.dxfer_len & 0x1) != 0
			 && (ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT) {

				nsegments++;
				if (nsegments > AHC_NSEG) {

					aic_set_transaction_status(scb,
					    CAM_REQ_TOO_BIG);
					bus_dmamap_unload(ahc->buffer_dmat,
							  scb->dmamap);
					ahc_free_scb(ahc, scb);
					xpt_done(ccb);
					return;
				}
				sg->addr = aic_htole32(ahc->dma_bug_buf);
				sg->len = aic_htole32(1);
				sg++;
			}
		}
		sg--;
		sg->len |= aic_htole32(AHC_DMA_LAST_SEG);

		/* Copy the first SG into the "current" data pointer area */
		scb->hscb->dataptr = scb->sg_list->addr;
		scb->hscb->datacnt = scb->sg_list->len;
	} else {
		scb->hscb->sgptr = aic_htole32(SG_LIST_NULL);
		scb->hscb->dataptr = 0;
		scb->hscb->datacnt = 0;
	}
	
	scb->sg_count = nsegments;

	/*
	 * Last time we need to check if this SCB needs to
	 * be aborted.
	 */
	if (aic_get_transaction_status(scb) != CAM_REQ_INPROG) {
		if (nsegments != 0)
			bus_dmamap_unload(ahc->buffer_dmat, scb->dmamap);
		ahc_free_scb(ahc, scb);
		xpt_done(ccb);
		return;
	}

	tinfo = ahc_fetch_transinfo(ahc, SCSIID_CHANNEL(ahc, scb->hscb->scsiid),
				    SCSIID_OUR_ID(scb->hscb->scsiid),
				    SCSIID_TARGET(ahc, scb->hscb->scsiid),
				    &tstate);

	mask = SCB_GET_TARGET_MASK(ahc, scb);
	scb->hscb->scsirate = tinfo->scsirate;
	scb->hscb->scsioffset = tinfo->curr.offset;
	if ((tstate->ultraenb & mask) != 0)
		scb->hscb->control |= ULTRAENB;

	if ((tstate->discenable & mask) != 0
	 && (ccb->ccb_h.flags & CAM_DIS_DISCONNECT) == 0)
		scb->hscb->control |= DISCENB;

	if ((ccb->ccb_h.flags & CAM_NEGOTIATE) != 0
	 && (tinfo->goal.width != 0
	  || tinfo->goal.offset != 0
	  || tinfo->goal.ppr_options != 0)) {
		scb->flags |= SCB_NEGOTIATE;
		scb->hscb->control |= MK_MESSAGE;
	} else if ((tstate->auto_negotiate & mask) != 0) {
		scb->flags |= SCB_AUTO_NEGOTIATE;
		scb->hscb->control |= MK_MESSAGE;
	}

	LIST_INSERT_HEAD(&ahc->pending_scbs, scb, pending_links);

	ccb->ccb_h.status |= CAM_SIM_QUEUED;

	/*
	 * We only allow one untagged transaction
	 * per target in the initiator role unless
	 * we are storing a full busy target *lun*
	 * table in SCB space.
	 */
	if ((scb->hscb->control & (TARGET_SCB|TAG_ENB)) == 0
	 && (ahc->flags & AHC_SCB_BTT) == 0) {
		struct scb_tailq *untagged_q;
		int target_offset;

		target_offset = SCB_GET_TARGET_OFFSET(ahc, scb);
		untagged_q = &(ahc->untagged_queues[target_offset]);
		TAILQ_INSERT_TAIL(untagged_q, scb, links.tqe);
		scb->flags |= SCB_UNTAGGEDQ;
		if (TAILQ_FIRST(untagged_q) != scb) {
			return;
		}
	}
	scb->flags |= SCB_ACTIVE;

	/*
	 * Timers are disabled while recovery is in progress.
	 */
	aic_scb_timer_start(scb);

	if ((scb->flags & SCB_TARGET_IMMEDIATE) != 0) {
		/* Define a mapping from our tag to the SCB. */
		ahc->scb_data->scbindex[scb->hscb->tag] = scb;
		ahc_pause(ahc);
		if ((ahc->flags & AHC_PAGESCBS) == 0)
			ahc_outb(ahc, SCBPTR, scb->hscb->tag);
		ahc_outb(ahc, TARG_IMMEDIATE_SCB, scb->hscb->tag);
		ahc_unpause(ahc);
	} else {
		ahc_queue_scb(ahc, scb);
	}
}

static void
ahc_poll(struct cam_sim *sim)
{
	struct ahc_softc *ahc;

	ahc = (struct ahc_softc *)cam_sim_softc(sim);
	ahc_intr(ahc);
}

static void
ahc_setup_data(struct ahc_softc *ahc, struct cam_sim *sim,
	       struct ccb_scsiio *csio, struct scb *scb)
{
	struct hardware_scb *hscb;
	struct ccb_hdr *ccb_h;
	int error;
	
	hscb = scb->hscb;
	ccb_h = &csio->ccb_h;
	
	csio->resid = 0;
	csio->sense_resid = 0;
	if (ccb_h->func_code == XPT_SCSI_IO) {
		hscb->cdb_len = csio->cdb_len;
		if ((ccb_h->flags & CAM_CDB_POINTER) != 0) {

			if (hscb->cdb_len > sizeof(hscb->cdb32)
			 || (ccb_h->flags & CAM_CDB_PHYS) != 0) {
				aic_set_transaction_status(scb,
							   CAM_REQ_INVALID);
				ahc_free_scb(ahc, scb);
				xpt_done((union ccb *)csio);
				return;
			}
			if (hscb->cdb_len > 12) {
				memcpy(hscb->cdb32, 
				       csio->cdb_io.cdb_ptr,
				       hscb->cdb_len);
				scb->flags |= SCB_CDB32_PTR;
			} else {
				memcpy(hscb->shared_data.cdb, 
				       csio->cdb_io.cdb_ptr,
				       hscb->cdb_len);
			}
		} else {
			if (hscb->cdb_len > 12) {
				memcpy(hscb->cdb32, csio->cdb_io.cdb_bytes,
				       hscb->cdb_len);
				scb->flags |= SCB_CDB32_PTR;
			} else {
				memcpy(hscb->shared_data.cdb,
				       csio->cdb_io.cdb_bytes,
				       hscb->cdb_len);
			}
		}
	}
		
	error = bus_dmamap_load_ccb(ahc->buffer_dmat,
				    scb->dmamap,
				    (union ccb *)csio,
				    ahc_execute_scb,
				    scb,
				    0);
	if (error == EINPROGRESS) {
		/*
		 * So as to maintain ordering,
		 * freeze the controller queue
		 * until our mapping is
		 * returned.
		 */
		xpt_freeze_simq(sim, /*count*/1);
		scb->io_ctx->ccb_h.status |= CAM_RELEASE_SIMQ;
	}
}

static void
ahc_abort_ccb(struct ahc_softc *ahc, struct cam_sim *sim, union ccb *ccb)
{
	union ccb *abort_ccb;

	abort_ccb = ccb->cab.abort_ccb;
	switch (abort_ccb->ccb_h.func_code) {
	case XPT_ACCEPT_TARGET_IO:
	case XPT_IMMEDIATE_NOTIFY:
	case XPT_CONT_TARGET_IO:
	{
		struct ahc_tmode_tstate *tstate;
		struct ahc_tmode_lstate *lstate;
		struct ccb_hdr_slist *list;
		cam_status status;

		status = ahc_find_tmode_devs(ahc, sim, abort_ccb, &tstate,
					     &lstate, TRUE);

		if (status != CAM_REQ_CMP) {
			ccb->ccb_h.status = status;
			break;
		}

		if (abort_ccb->ccb_h.func_code == XPT_ACCEPT_TARGET_IO)
			list = &lstate->accept_tios;
		else if (abort_ccb->ccb_h.func_code == XPT_IMMEDIATE_NOTIFY)
			list = &lstate->immed_notifies;
		else
			list = NULL;

		if (list != NULL) {
			struct ccb_hdr *curelm;
			int found;

			curelm = SLIST_FIRST(list);
			found = 0;
			if (curelm == &abort_ccb->ccb_h) {
				found = 1;
				SLIST_REMOVE_HEAD(list, sim_links.sle);
			} else {
				while(curelm != NULL) {
					struct ccb_hdr *nextelm;

					nextelm =
					    SLIST_NEXT(curelm, sim_links.sle);

					if (nextelm == &abort_ccb->ccb_h) {
						found = 1;
						SLIST_NEXT(curelm,
							   sim_links.sle) =
						    SLIST_NEXT(nextelm,
							       sim_links.sle);
						break;
					}
					curelm = nextelm;
				}
			}

			if (found) {
				abort_ccb->ccb_h.status = CAM_REQ_ABORTED;
				xpt_done(abort_ccb);
				ccb->ccb_h.status = CAM_REQ_CMP;
			} else {
				xpt_print_path(abort_ccb->ccb_h.path);
				printf("Not found\n");
				ccb->ccb_h.status = CAM_PATH_INVALID;
			}
			break;
		}
		/* FALLTHROUGH */
	}
	case XPT_SCSI_IO:
		/* XXX Fully implement the hard ones */
		ccb->ccb_h.status = CAM_UA_ABORT;
		break;
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
}

void
ahc_send_async(struct ahc_softc *ahc, char channel, u_int target,
		u_int lun, ac_code code, void *opt_arg)
{
	struct	ccb_trans_settings cts;
	struct cam_path *path;
	void *arg;
	int error;

	arg = NULL;
	error = ahc_create_path(ahc, channel, target, lun, &path);

	if (error != CAM_REQ_CMP)
		return;

	switch (code) {
	case AC_TRANSFER_NEG:
	{
		struct	ccb_trans_settings_scsi *scsi;
	
		cts.type = CTS_TYPE_CURRENT_SETTINGS;
		scsi = &cts.proto_specific.scsi;
		cts.ccb_h.path = path;
		cts.ccb_h.target_id = target;
		cts.ccb_h.target_lun = lun;
		ahc_get_tran_settings(ahc, channel == 'A' ? ahc->our_id
							  : ahc->our_id_b,
				      channel, &cts);
		arg = &cts;
		scsi->valid &= ~CTS_SCSI_VALID_TQ;
		scsi->flags &= ~CTS_SCSI_FLAGS_TAG_ENB;
		if (opt_arg == NULL)
			break;
		if (*((ahc_queue_alg *)opt_arg) == AHC_QUEUE_TAGGED)
			scsi->flags |= ~CTS_SCSI_FLAGS_TAG_ENB;
		scsi->valid |= CTS_SCSI_VALID_TQ;
		break;
	}
	case AC_SENT_BDR:
	case AC_BUS_RESET:
		break;
	default:
		panic("ahc_send_async: Unexpected async event");
	}
	xpt_async(code, path, arg);
	xpt_free_path(path);
}

void
ahc_platform_set_tags(struct ahc_softc *ahc,
		      struct ahc_devinfo *devinfo, int enable)
{
}

int
ahc_platform_alloc(struct ahc_softc *ahc, void *platform_arg)
{
	ahc->platform_data = malloc(sizeof(struct ahc_platform_data), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (ahc->platform_data == NULL)
		return (ENOMEM);
	return (0);
}

void
ahc_platform_free(struct ahc_softc *ahc)
{
	struct ahc_platform_data *pdata;

	pdata = ahc->platform_data;
	if (pdata != NULL) {
		if (pdata->regs != NULL)
			bus_release_resource(ahc->dev_softc,
					     pdata->regs_res_type,
					     pdata->regs_res_id,
					     pdata->regs);

		if (pdata->irq != NULL)
			bus_release_resource(ahc->dev_softc,
					     pdata->irq_res_type,
					     0, pdata->irq);

		if (pdata->sim_b != NULL) {
			xpt_async(AC_LOST_DEVICE, pdata->path_b, NULL);
			xpt_free_path(pdata->path_b);
			xpt_bus_deregister(cam_sim_path(pdata->sim_b));
			cam_sim_free(pdata->sim_b, /*free_devq*/TRUE);
		}
		if (pdata->sim != NULL) {
			xpt_async(AC_LOST_DEVICE, pdata->path, NULL);
			xpt_free_path(pdata->path);
			xpt_bus_deregister(cam_sim_path(pdata->sim));
			cam_sim_free(pdata->sim, /*free_devq*/TRUE);
		}
		if (pdata->eh != NULL)
			EVENTHANDLER_DEREGISTER(shutdown_final, pdata->eh);
		free(ahc->platform_data, M_DEVBUF);
	}
}

int
ahc_softc_comp(struct ahc_softc *lahc, struct ahc_softc *rahc)
{
	/* We don't sort softcs under FreeBSD so report equal always */
	return (0);
}

int
ahc_detach(device_t dev)
{
	struct ahc_softc *ahc;

	device_printf(dev, "detaching device\n");
	ahc = device_get_softc(dev);
	ahc_lock(ahc);
	TAILQ_REMOVE(&ahc_tailq, ahc, links);
	ahc_intr_enable(ahc, FALSE);
	bus_teardown_intr(dev, ahc->platform_data->irq, ahc->platform_data->ih);
	ahc_unlock(ahc);
	ahc_free(ahc);
	return (0);
}

#if 0
static void
ahc_dump_targcmd(struct target_cmd *cmd)
{
	uint8_t *byte;
	uint8_t *last_byte;
	int i;

	byte = &cmd->initiator_channel;
	/* Debugging info for received commands */
	last_byte = &cmd[1].initiator_channel;

	i = 0;
	while (byte < last_byte) {
		if (i == 0)
			printf("\t");
		printf("%#x", *byte++);
		i++;
		if (i == 8) {
			printf("\n");
			i = 0;
		} else {
			printf(", ");
		}
	}
}
#endif

static int
ahc_modevent(module_t mod, int type, void *data)
{
	/* XXX Deal with busy status on unload. */
	/* XXX Deal with unknown events */
	return 0;
}
  
static moduledata_t ahc_mod = {
	"ahc",
	ahc_modevent,
	NULL
};

DECLARE_MODULE(ahc, ahc_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(ahc, cam, 1, 1, 1);
MODULE_VERSION(ahc, 1);
