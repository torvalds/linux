/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @defgroup scsi_api_target SCSI Target API
 * @defgroup scsi_api_initiator SCSI Initiator API
 * @defgroup cam_api Common Access Method (CAM) API
 * @defgroup cam_io CAM IO
 */

/**
 * @file
 * Provides CAM functionality.
 */

#include "ocs.h"
#include "ocs_scsi.h"
#include "ocs_device.h"

/* Default IO timeout value for initiators is 30 seconds */
#define OCS_CAM_IO_TIMEOUT	30

typedef struct {
	ocs_scsi_sgl_t *sgl;
	uint32_t sgl_max;
	uint32_t sgl_count;
	int32_t rc;
} ocs_dmamap_load_arg_t;

static void ocs_action(struct cam_sim *, union ccb *);
static void ocs_poll(struct cam_sim *);

static ocs_tgt_resource_t *ocs_tgt_resource_get(ocs_fcport *,
					struct ccb_hdr *, uint32_t *);
static int32_t ocs_tgt_resource_abort(struct ocs_softc *, ocs_tgt_resource_t *);
static uint32_t ocs_abort_initiator_io(struct ocs_softc *ocs, union ccb *accb);
static void ocs_abort_inot(struct ocs_softc *ocs, union ccb *ccb);
static void ocs_abort_atio(struct ocs_softc *ocs, union ccb *ccb);
static int32_t ocs_target_tmf_cb(ocs_io_t *, ocs_scsi_io_status_e, uint32_t, void *);
static int32_t ocs_io_abort_cb(ocs_io_t *, ocs_scsi_io_status_e, uint32_t, void *);
static int32_t ocs_task_set_full_or_busy(ocs_io_t *io);
static int32_t ocs_initiator_tmf_cb(ocs_io_t *, ocs_scsi_io_status_e,
		ocs_scsi_cmd_resp_t *, uint32_t, void *);
static uint32_t
ocs_fcp_change_role(struct ocs_softc *ocs, ocs_fcport *fcp, uint32_t new_role);

static void ocs_ldt(void *arg);
static void ocs_ldt_task(void *arg, int pending);
static void ocs_delete_target(ocs_t *ocs, ocs_fcport *fcp, int tgt);
uint32_t ocs_add_new_tgt(ocs_node_t *node, ocs_fcport *fcp);
uint32_t ocs_update_tgt(ocs_node_t *node, ocs_fcport *fcp, uint32_t tgt_id);

int32_t ocs_tgt_find(ocs_fcport *fcp, ocs_node_t *node);

static inline ocs_io_t *ocs_scsi_find_io(struct ocs_softc *ocs, uint32_t tag)
{

	return ocs_io_get_instance(ocs, tag);
}

static inline void ocs_target_io_free(ocs_io_t *io)
{
	io->tgt_io.state = OCS_CAM_IO_FREE;
	io->tgt_io.flags = 0;
	io->tgt_io.app = NULL;
	ocs_scsi_io_complete(io);
	if(io->ocs->io_in_use != 0)
		atomic_subtract_acq_32(&io->ocs->io_in_use, 1);
}

static int32_t
ocs_attach_port(ocs_t *ocs, int chan)
{

	struct cam_sim	*sim = NULL;
	struct cam_path	*path = NULL;
	uint32_t	max_io = ocs_scsi_get_property(ocs, OCS_SCSI_MAX_IOS);
	ocs_fcport *fcp = FCPORT(ocs, chan);

	if (NULL == (sim = cam_sim_alloc(ocs_action, ocs_poll, 
				device_get_name(ocs->dev), ocs, 
				device_get_unit(ocs->dev), &ocs->sim_lock,
				max_io, max_io, ocs->devq))) {
		device_printf(ocs->dev, "Can't allocate SIM\n");
		return 1;
	}

	mtx_lock(&ocs->sim_lock);
	if (CAM_SUCCESS != xpt_bus_register(sim, ocs->dev, chan)) {
		device_printf(ocs->dev, "Can't register bus %d\n", 0);
		mtx_unlock(&ocs->sim_lock);
		cam_sim_free(sim, FALSE);
		return 1;
	}
	mtx_unlock(&ocs->sim_lock);

	if (CAM_REQ_CMP != xpt_create_path(&path, NULL, cam_sim_path(sim),
				CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD)) {
		device_printf(ocs->dev, "Can't create path\n");
		xpt_bus_deregister(cam_sim_path(sim));
		mtx_unlock(&ocs->sim_lock);
		cam_sim_free(sim, FALSE);
		return 1;
	}
	
	fcp->ocs = ocs;
	fcp->sim  = sim;
	fcp->path = path;

	callout_init_mtx(&fcp->ldt, &ocs->sim_lock, 0);
	TASK_INIT(&fcp->ltask, 1, ocs_ldt_task, fcp);

	return 0;
}

static int32_t
ocs_detach_port(ocs_t *ocs, int32_t chan)
{
	ocs_fcport *fcp = NULL;
	struct cam_sim	*sim = NULL;
	struct cam_path	*path = NULL;
	fcp = FCPORT(ocs, chan);

	sim = fcp->sim;
	path = fcp->path;

	callout_drain(&fcp->ldt);
	ocs_ldt_task(fcp, 0);	

	if (fcp->sim) {
		mtx_lock(&ocs->sim_lock);
			ocs_tgt_resource_abort(ocs, &fcp->targ_rsrc_wildcard);
			if (path) {
				xpt_async(AC_LOST_DEVICE, path, NULL);
				xpt_free_path(path);
				fcp->path = NULL;
			}
			xpt_bus_deregister(cam_sim_path(sim));

			cam_sim_free(sim, FALSE);
			fcp->sim = NULL;
		mtx_unlock(&ocs->sim_lock);
	}
	
	return 0;
}

int32_t
ocs_cam_attach(ocs_t *ocs)
{
	struct cam_devq	*devq = NULL;
	int	i = 0;
	uint32_t	max_io = ocs_scsi_get_property(ocs, OCS_SCSI_MAX_IOS);

	if (NULL == (devq = cam_simq_alloc(max_io))) {
		device_printf(ocs->dev, "Can't allocate SIMQ\n");
		return -1;
	}

	ocs->devq = devq;

	if (mtx_initialized(&ocs->sim_lock) == 0) {
		mtx_init(&ocs->sim_lock, "ocs_sim_lock", NULL, MTX_DEF);
	}

	for (i = 0; i < (ocs->num_vports + 1); i++) {
		if (ocs_attach_port(ocs, i)) {
			ocs_log_err(ocs, "Attach port failed for chan: %d\n", i);
			goto detach_port;
		}
	}
	
	ocs->io_high_watermark = max_io;
	ocs->io_in_use = 0;
	return 0;

detach_port:
	while (--i >= 0) {
		ocs_detach_port(ocs, i);
	}

	cam_simq_free(ocs->devq);

	if (mtx_initialized(&ocs->sim_lock))
		mtx_destroy(&ocs->sim_lock);

	return 1;	
}

int32_t
ocs_cam_detach(ocs_t *ocs)
{
	int i = 0;

	for (i = (ocs->num_vports); i >= 0; i--) {
		ocs_detach_port(ocs, i);
	}

	cam_simq_free(ocs->devq);

	if (mtx_initialized(&ocs->sim_lock))
		mtx_destroy(&ocs->sim_lock);

	return 0;
}

/***************************************************************************
 * Functions required by SCSI base driver API
 */

/**
 * @ingroup scsi_api_target
 * @brief Attach driver to the BSD SCSI layer (a.k.a CAM)
 *
 * Allocates + initializes CAM related resources and attaches to the CAM
 *
 * @param ocs the driver instance's software context
 *
 * @return 0 on success, non-zero otherwise
 */
int32_t
ocs_scsi_tgt_new_device(ocs_t *ocs)
{
	ocs->enable_task_set_full = ocs_scsi_get_property(ocs, 
					OCS_SCSI_ENABLE_TASK_SET_FULL);
	ocs_log_debug(ocs, "task set full processing is %s\n",
		ocs->enable_task_set_full ? "enabled" : "disabled");

	return 0;
}

/**
 * @ingroup scsi_api_target
 * @brief Tears down target members of ocs structure.
 *
 * Called by OS code when device is removed.
 *
 * @param ocs pointer to ocs
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_tgt_del_device(ocs_t *ocs)
{

	return 0;
}

/**
 * @ingroup scsi_api_target
 * @brief accept new domain notification
 *
 * Called by base drive when new domain is discovered.  A target-server
 * will use this call to prepare for new remote node notifications
 * arising from ocs_scsi_new_initiator().
 *
 * The domain context has an element <b>ocs_scsi_tgt_domain_t tgt_domain</b> 
 * which is declared by the target-server code and is used for target-server 
 * private data.
 *
 * This function will only be called if the base-driver has been enabled for 
 * target capability.
 *
 * Note that this call is made to target-server backends, 
 * the ocs_scsi_ini_new_domain() function is called to initiator-client backends.
 *
 * @param domain pointer to domain
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_tgt_new_domain(ocs_domain_t *domain)
{
	return 0;
}

/**
 * @ingroup scsi_api_target
 * @brief accept domain lost notification
 *
 * Called by base-driver when a domain goes away.  A target-server will
 * use this call to clean up all domain scoped resources.
 *
 * Note that this call is made to target-server backends,
 * the ocs_scsi_ini_del_domain() function is called to initiator-client backends.
 *
 * @param domain pointer to domain
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
void
ocs_scsi_tgt_del_domain(ocs_domain_t *domain)
{
}


/**
 * @ingroup scsi_api_target
 * @brief accept new sli port (sport) notification
 *
 * Called by base drive when new sport is discovered.  A target-server
 * will use this call to prepare for new remote node notifications
 * arising from ocs_scsi_new_initiator().
 *
 * The domain context has an element <b>ocs_scsi_tgt_sport_t tgt_sport</b> 
 * which is declared by the target-server code and is used for
 * target-server private data.
 *
 * This function will only be called if the base-driver has been enabled for 
 * target capability.
 *
 * Note that this call is made to target-server backends,
 * the ocs_scsi_tgt_new_domain() is called to initiator-client backends.
 *
 * @param sport pointer to SLI port
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_tgt_new_sport(ocs_sport_t *sport)
{
	ocs_t *ocs = sport->ocs;

	if(!sport->is_vport) {
		sport->tgt_data = FCPORT(ocs, 0);
	}

	return 0;
}

/**
 * @ingroup scsi_api_target
 * @brief accept SLI port gone notification
 *
 * Called by base-driver when a sport goes away.  A target-server will
 * use this call to clean up all sport scoped resources.
 *
 * Note that this call is made to target-server backends,
 * the ocs_scsi_ini_del_sport() is called to initiator-client backends.
 *
 * @param sport pointer to SLI port
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
void
ocs_scsi_tgt_del_sport(ocs_sport_t *sport)
{
	return;
}

/**
 * @ingroup scsi_api_target
 * @brief receive notification of a new SCSI initiator node
 *
 * Sent by base driver to notify a target-server of the presense of a new
 * remote initiator.   The target-server may use this call to prepare for
 * inbound IO from this node.
 *
 * The ocs_node_t structure has and elment of type ocs_scsi_tgt_node_t named
 * tgt_node that is declared and used by a target-server for private
 * information.
 *
 * This function is only called if the target capability is enabled in driver.
 *
 * @param node pointer to new remote initiator node
 *
 * @return returns 0 for success, a negative error code value for failure.
 *
 * @note
 */
int32_t
ocs_scsi_new_initiator(ocs_node_t *node)
{
	ocs_t	*ocs = node->ocs;
	struct ac_contract ac;
	struct ac_device_changed *adc;

	ocs_fcport	*fcp = NULL;

	fcp = node->sport->tgt_data;
	if (fcp == NULL) {
		ocs_log_err(ocs, "FCP is NULL \n");
		return 1;
	}	

	/*
	 * Update the IO watermark by decrementing it by the
	 * number of IOs reserved for each initiator.
	 */
	atomic_subtract_acq_32(&ocs->io_high_watermark, OCS_RSVD_INI_IO);

	ac.contract_number = AC_CONTRACT_DEV_CHG;
	adc = (struct ac_device_changed *) ac.contract_data;
	adc->wwpn = ocs_node_get_wwpn(node);
	adc->port = node->rnode.fc_id;
	adc->target = node->instance_index;
	adc->arrived = 1;
	xpt_async(AC_CONTRACT, fcp->path, &ac);

	return 0;
}

/**
 * @ingroup scsi_api_target
 * @brief validate new initiator
 *
 * Sent by base driver to validate a remote initiatiator.   The target-server
 * returns TRUE if this initiator should be accepted.
 *
 * This function is only called if the target capability is enabled in driver.
 *
 * @param node pointer to remote initiator node to validate
 *
 * @return TRUE if initiator should be accepted, FALSE if it should be rejected
 *
 * @note
 */

int32_t
ocs_scsi_validate_initiator(ocs_node_t *node)
{
	return 1;
}

/**
 * @ingroup scsi_api_target
 * @brief Delete a SCSI initiator node
 *
 * Sent by base driver to notify a target-server that a remote initiator
 * is now gone. The base driver will have terminated all outstanding IOs 
 * and the target-server will receive appropriate completions.
 *
 * This function is only called if the base driver is enabled for
 * target capability.
 *
 * @param node pointer node being deleted
 * @param reason Reason why initiator is gone.
 *
 * @return OCS_SCSI_CALL_COMPLETE to indicate that all work was completed
 *
 * @note
 */
int32_t
ocs_scsi_del_initiator(ocs_node_t *node, ocs_scsi_del_initiator_reason_e reason)
{
	ocs_t	*ocs = node->ocs;

	struct ac_contract ac;
	struct ac_device_changed *adc;
	ocs_fcport	*fcp = NULL;

	fcp = node->sport->tgt_data;
	if (fcp == NULL) {
		ocs_log_err(ocs, "FCP is NULL \n");
		return 1;
	}

	ac.contract_number = AC_CONTRACT_DEV_CHG;
	adc = (struct ac_device_changed *) ac.contract_data;
	adc->wwpn = ocs_node_get_wwpn(node);
	adc->port = node->rnode.fc_id;
	adc->target = node->instance_index;
	adc->arrived = 0;
	xpt_async(AC_CONTRACT, fcp->path, &ac);


	if (reason == OCS_SCSI_INITIATOR_MISSING) {
		return OCS_SCSI_CALL_COMPLETE;
	}

	/*
	 * Update the IO watermark by incrementing it by the
	 * number of IOs reserved for each initiator.
	 */
	atomic_add_acq_32(&ocs->io_high_watermark, OCS_RSVD_INI_IO);

	return OCS_SCSI_CALL_COMPLETE;
}

/**
 * @ingroup scsi_api_target
 * @brief receive FCP SCSI Command
 *
 * Called by the base driver when a new SCSI command has been received.   The
 * target-server will process the command, and issue data and/or response phase
 * requests to the base driver.
 *
 * The IO context (ocs_io_t) structure has and element of type 
 * ocs_scsi_tgt_io_t named tgt_io that is declared and used by 
 * a target-server for private information.
 *
 * @param io pointer to IO context
 * @param lun LUN for this IO
 * @param cdb pointer to SCSI CDB
 * @param cdb_len length of CDB in bytes
 * @param flags command flags
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t ocs_scsi_recv_cmd(ocs_io_t *io, uint64_t lun, uint8_t *cdb,
				uint32_t cdb_len, uint32_t flags)
{
	ocs_t *ocs = io->ocs;
	struct ccb_accept_tio *atio = NULL;
	ocs_node_t	*node = io->node;
	ocs_tgt_resource_t *trsrc = NULL;
	int32_t		rc = -1;
	ocs_fcport	*fcp = NULL;

	fcp = node->sport->tgt_data;
	if (fcp == NULL) {
		ocs_log_err(ocs, "FCP is NULL \n");
		return 1;
	}

	atomic_add_acq_32(&ocs->io_in_use, 1);

	/* set target io timeout */
	io->timeout = ocs->target_io_timer_sec;

	if (ocs->enable_task_set_full && 
		(ocs->io_in_use >= ocs->io_high_watermark)) {
		return ocs_task_set_full_or_busy(io);
	} else {
		atomic_store_rel_32(&io->node->tgt_node.busy_sent, FALSE);
	}

	if ((lun < OCS_MAX_LUN) && fcp->targ_rsrc[lun].enabled) {
		trsrc = &fcp->targ_rsrc[lun];
	} else if (fcp->targ_rsrc_wildcard.enabled) {
		trsrc = &fcp->targ_rsrc_wildcard;
	}

	if (trsrc) {
		atio = (struct ccb_accept_tio *)STAILQ_FIRST(&trsrc->atio);
	}

	if (atio) {

		STAILQ_REMOVE_HEAD(&trsrc->atio, sim_links.stqe);

		atio->ccb_h.status = CAM_CDB_RECVD;
		atio->ccb_h.target_lun = lun;
		atio->sense_len = 0;

		atio->init_id = node->instance_index;
		atio->tag_id = io->tag;
		atio->ccb_h.ccb_io_ptr = io;

		if (flags & OCS_SCSI_CMD_SIMPLE)
			atio->tag_action = MSG_SIMPLE_Q_TAG;
		else if (flags &  FCP_TASK_ATTR_HEAD_OF_QUEUE)
			atio->tag_action = MSG_HEAD_OF_Q_TAG;
		else if (flags & FCP_TASK_ATTR_ORDERED)
			atio->tag_action = MSG_ORDERED_Q_TAG;
		else
			atio->tag_action = 0;

		atio->cdb_len = cdb_len;
		ocs_memcpy(atio->cdb_io.cdb_bytes, cdb, cdb_len);

		io->tgt_io.flags = 0;
		io->tgt_io.state = OCS_CAM_IO_COMMAND;
		io->tgt_io.lun = lun;

		xpt_done((union ccb *)atio);

		rc = 0;
	} else {
		device_printf(
			ocs->dev, "%s: no ATIO for LUN %lx (en=%s) OX_ID %#x\n",
			__func__, (unsigned long)lun,
			trsrc ? (trsrc->enabled ? "T" : "F") : "X",
			be16toh(io->init_task_tag));

		io->tgt_io.state = OCS_CAM_IO_MAX;
		ocs_target_io_free(io);
	}

	return rc;
}

/**
 * @ingroup scsi_api_target
 * @brief receive FCP SCSI Command with first burst data.
 *
 * Receive a new FCP SCSI command from the base driver with first burst data.
 *
 * @param io pointer to IO context
 * @param lun LUN for this IO
 * @param cdb pointer to SCSI CDB
 * @param cdb_len length of CDB in bytes
 * @param flags command flags
 * @param first_burst_buffers first burst buffers
 * @param first_burst_buffer_count The number of bytes received in the first burst
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t ocs_scsi_recv_cmd_first_burst(ocs_io_t *io, uint64_t lun, uint8_t *cdb,
		 			uint32_t cdb_len, uint32_t flags, 
					ocs_dma_t first_burst_buffers[], 
					uint32_t first_burst_buffer_count)
{
	return -1;
}

/**
 * @ingroup scsi_api_target
 * @brief receive a TMF command IO
 *
 * Called by the base driver when a SCSI TMF command has been received.   The
 * target-server will process the command, aborting commands as needed, and post
 * a response using ocs_scsi_send_resp()
 *
 * The IO context (ocs_io_t) structure has and element of type ocs_scsi_tgt_io_t named
 * tgt_io that is declared and used by a target-server for private information.
 *
 * If the target-server walks the nodes active_ios linked list, and starts IO
 * abort processing, the code <b>must</b> be sure not to abort the IO passed into the
 * ocs_scsi_recv_tmf() command.
 *
 * @param tmfio pointer to IO context
 * @param lun logical unit value
 * @param cmd command request
 * @param abortio pointer to IO object to abort for TASK_ABORT (NULL for all other TMF)
 * @param flags flags
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t ocs_scsi_recv_tmf(ocs_io_t *tmfio, uint64_t lun, ocs_scsi_tmf_cmd_e cmd,
				ocs_io_t *abortio, uint32_t flags)
{
	ocs_t *ocs = tmfio->ocs;
	ocs_node_t *node = tmfio->node;
	ocs_tgt_resource_t *trsrc = NULL;
	struct ccb_immediate_notify *inot = NULL;
	int32_t		rc = -1;
	ocs_fcport	*fcp = NULL;

	fcp = node->sport->tgt_data;
	if (fcp == NULL) {
		ocs_log_err(ocs, "FCP is NULL \n");
		return 1;
	}

	if ((lun < OCS_MAX_LUN) && fcp->targ_rsrc[lun].enabled) {
		trsrc = &fcp->targ_rsrc[lun];
	} else if (fcp->targ_rsrc_wildcard.enabled) {
		trsrc = &fcp->targ_rsrc_wildcard;
	}

	device_printf(tmfio->ocs->dev, "%s: io=%p cmd=%#x LU=%lx en=%s\n",
			__func__, tmfio, cmd, (unsigned long)lun,
			trsrc ? (trsrc->enabled ? "T" : "F") : "X");
	if (trsrc) {
		inot = (struct ccb_immediate_notify *)STAILQ_FIRST(&trsrc->inot);
	}

	if (!inot) {
		device_printf(
			ocs->dev, "%s: no INOT for LUN %llx (en=%s) OX_ID %#x\n",
			__func__, (unsigned long long)lun, trsrc ? (trsrc->enabled ? "T" : "F") : "X",
			be16toh(tmfio->init_task_tag));

		if (abortio) {
			ocs_scsi_io_complete(abortio);
		}
		ocs_scsi_io_complete(tmfio);
		goto ocs_scsi_recv_tmf_out;
	}


	tmfio->tgt_io.app = abortio;

	STAILQ_REMOVE_HEAD(&trsrc->inot, sim_links.stqe);

	inot->tag_id = tmfio->tag;
	inot->seq_id = tmfio->tag;

	if ((lun < OCS_MAX_LUN) && fcp->targ_rsrc[lun].enabled) {
		inot->initiator_id = node->instance_index;
	} else {
		inot->initiator_id = CAM_TARGET_WILDCARD;
	} 

	inot->ccb_h.status = CAM_MESSAGE_RECV;
	inot->ccb_h.target_lun = lun;

	switch (cmd) {
	case OCS_SCSI_TMF_ABORT_TASK:
		inot->arg = MSG_ABORT_TASK;
		inot->seq_id = abortio->tag;
		device_printf(ocs->dev, "%s: ABTS IO.%#x st=%#x\n", 
			__func__, abortio->tag,	abortio->tgt_io.state);
		abortio->tgt_io.flags |= OCS_CAM_IO_F_ABORT_RECV;
		abortio->tgt_io.flags |= OCS_CAM_IO_F_ABORT_NOTIFY;
		break;
	case OCS_SCSI_TMF_QUERY_TASK_SET:
		device_printf(ocs->dev, 
			"%s: OCS_SCSI_TMF_QUERY_TASK_SET not supported\n",
				__func__);
		STAILQ_INSERT_TAIL(&trsrc->inot, &inot->ccb_h, sim_links.stqe);
		ocs_scsi_io_complete(tmfio);
		goto ocs_scsi_recv_tmf_out;
		break;
	case OCS_SCSI_TMF_ABORT_TASK_SET:
		inot->arg = MSG_ABORT_TASK_SET;
		break;
	case OCS_SCSI_TMF_CLEAR_TASK_SET:
		inot->arg = MSG_CLEAR_TASK_SET;
		break;
	case OCS_SCSI_TMF_QUERY_ASYNCHRONOUS_EVENT:
		inot->arg = MSG_QUERY_ASYNC_EVENT;
		break;
	case OCS_SCSI_TMF_LOGICAL_UNIT_RESET:
		inot->arg = MSG_LOGICAL_UNIT_RESET;
		break;
	case OCS_SCSI_TMF_CLEAR_ACA:
		inot->arg = MSG_CLEAR_ACA;
		break;
	case OCS_SCSI_TMF_TARGET_RESET:
		inot->arg = MSG_TARGET_RESET;
		break;
	default:
		device_printf(ocs->dev, "%s: unsupported TMF %#x\n",
							 __func__, cmd);
		STAILQ_INSERT_TAIL(&trsrc->inot, &inot->ccb_h, sim_links.stqe);
		goto ocs_scsi_recv_tmf_out;
	}

	rc = 0;

	xpt_print(inot->ccb_h.path, "%s: func=%#x stat=%#x id=%#x lun=%#x"
			" flags=%#x tag=%#x seq=%#x ini=%#x arg=%#x\n", 
			__func__, inot->ccb_h.func_code, inot->ccb_h.status,
			inot->ccb_h.target_id, 
			(unsigned int)inot->ccb_h.target_lun, inot->ccb_h.flags,
			inot->tag_id, inot->seq_id, inot->initiator_id,
			inot->arg);
	xpt_done((union ccb *)inot);

	if (abortio) {
		abortio->tgt_io.flags |= OCS_CAM_IO_F_ABORT_DEV;
		rc = ocs_scsi_tgt_abort_io(abortio, ocs_io_abort_cb, tmfio);
	}
	
ocs_scsi_recv_tmf_out:
	return rc;
}

/**
 * @ingroup scsi_api_initiator
 * @brief Initializes any initiator fields on the ocs structure.
 *
 * Called by OS initialization code when a new device is discovered.
 *
 * @param ocs pointer to ocs
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_ini_new_device(ocs_t *ocs)
{

	return 0;
}

/**
 * @ingroup scsi_api_initiator
 * @brief Tears down initiator members of ocs structure.
 *
 * Called by OS code when device is removed.
 *
 * @param ocs pointer to ocs
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

int32_t
ocs_scsi_ini_del_device(ocs_t *ocs)
{

	return 0;
}


/**
 * @ingroup scsi_api_initiator
 * @brief accept new domain notification
 *
 * Called by base drive when new domain is discovered.  An initiator-client
 * will accept this call to prepare for new remote node notifications
 * arising from ocs_scsi_new_target().
 *
 * The domain context has the element <b>ocs_scsi_ini_domain_t ini_domain</b>
 * which is declared by the initiator-client code and is used for 
 * initiator-client private data.
 *
 * This function will only be called if the base-driver has been enabled for 
 * initiator capability.
 *
 * Note that this call is made to initiator-client backends, 
 * the ocs_scsi_tgt_new_domain() function is called to target-server backends.
 *
 * @param domain pointer to domain
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_ini_new_domain(ocs_domain_t *domain)
{
	return 0;
}

/**
 * @ingroup scsi_api_initiator
 * @brief accept domain lost notification
 *
 * Called by base-driver when a domain goes away.  An initiator-client will
 * use this call to clean up all domain scoped resources.
 *
 * This function will only be called if the base-driver has been enabled for
 * initiator capability.
 *
 * Note that this call is made to initiator-client backends,
 * the ocs_scsi_tgt_del_domain() function is called to target-server backends.
 *
 * @param domain pointer to domain
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
void
ocs_scsi_ini_del_domain(ocs_domain_t *domain)
{
}

/**
 * @ingroup scsi_api_initiator
 * @brief accept new sli port notification
 *
 * Called by base drive when new sli port (sport) is discovered.
 * A target-server will use this call to prepare for new remote node
 * notifications arising from ocs_scsi_new_initiator().
 *
 * This function will only be called if the base-driver has been enabled for
 * target capability.
 *
 * Note that this call is made to target-server backends,
 * the ocs_scsi_ini_new_sport() function is called to initiator-client backends.
 *
 * @param sport pointer to sport
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_ini_new_sport(ocs_sport_t *sport)
{
	ocs_t *ocs = sport->ocs;
	ocs_fcport *fcp = FCPORT(ocs, 0);

	if (!sport->is_vport) {
		sport->tgt_data = fcp;
		fcp->fc_id = sport->fc_id;	
	}

	return 0;
}

/**
 * @ingroup scsi_api_initiator
 * @brief accept sli port gone notification
 *
 * Called by base-driver when a sport goes away.  A target-server will
 * use this call to clean up all sport scoped resources.
 *
 * Note that this call is made to target-server backends,
 * the ocs_scsi_ini_del_sport() function is called to initiator-client backends.
 *
 * @param sport pointer to SLI port
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
void
ocs_scsi_ini_del_sport(ocs_sport_t *sport)
{
	ocs_t *ocs = sport->ocs;
	ocs_fcport *fcp = FCPORT(ocs, 0);

	if (!sport->is_vport) {
		fcp->fc_id = 0;	
	}
}

void 
ocs_scsi_sport_deleted(ocs_sport_t *sport)
{
	ocs_t *ocs = sport->ocs;
	ocs_fcport *fcp = NULL;

	ocs_xport_stats_t value;

	if (!sport->is_vport) {
		return;
	}

	fcp = sport->tgt_data;

	ocs_xport_status(ocs->xport, OCS_XPORT_PORT_STATUS, &value);

	if (value.value == 0) {
		ocs_log_debug(ocs, "PORT offline,.. skipping\n");
		return;
	}	

	if ((fcp->role != KNOB_ROLE_NONE)) {
		if(fcp->vport->sport != NULL) {
			ocs_log_debug(ocs,"sport is not NULL, skipping\n");
			return;
		}

		ocs_sport_vport_alloc(ocs->domain, fcp->vport);
		return;
	}

}

int32_t
ocs_tgt_find(ocs_fcport *fcp, ocs_node_t *node)
{
	ocs_fc_target_t *tgt = NULL;
	uint32_t i;
	
	for (i = 0; i < OCS_MAX_TARGETS; i++) {
		tgt = &fcp->tgt[i];

		if (tgt->state == OCS_TGT_STATE_NONE)
			continue;
		
		if (ocs_node_get_wwpn(node) == tgt->wwpn) {
			return i;
		}
	}
	
	return -1;
}

/**
 * @ingroup scsi_api_initiator
 * @brief receive notification of a new SCSI target node
 *
 * Sent by base driver to notify an initiator-client of the presense of a new
 * remote target.   The initiator-server may use this call to prepare for
 * inbound IO from this node.
 *
 * This function is only called if the base driver is enabled for
 * initiator capability.
 *
 * @param node pointer to new remote initiator node
 *
 * @return none
 *
 * @note
 */

uint32_t
ocs_update_tgt(ocs_node_t *node, ocs_fcport *fcp, uint32_t tgt_id)
{
	ocs_fc_target_t *tgt = NULL;
	
	tgt = &fcp->tgt[tgt_id];

	tgt->node_id = node->instance_index;
	tgt->state = OCS_TGT_STATE_VALID;
	
	tgt->port_id = node->rnode.fc_id;
	tgt->wwpn = ocs_node_get_wwpn(node);
	tgt->wwnn = ocs_node_get_wwnn(node);
	return 0;
}

uint32_t
ocs_add_new_tgt(ocs_node_t *node, ocs_fcport *fcp)
{
	uint32_t i;

	struct ocs_softc *ocs = node->ocs;
	union ccb *ccb = NULL;
	for (i = 0; i < OCS_MAX_TARGETS; i++) {
		if (fcp->tgt[i].state == OCS_TGT_STATE_NONE)
			break;
	}

	if (NULL == (ccb = xpt_alloc_ccb_nowait())) {
		device_printf(ocs->dev, "%s: ccb allocation failed\n", __func__);
		return -1;
	}

	if (CAM_REQ_CMP != xpt_create_path(&ccb->ccb_h.path, xpt_periph,
				cam_sim_path(fcp->sim),
				i, CAM_LUN_WILDCARD)) {
		device_printf(
			ocs->dev, "%s: target path creation failed\n", __func__);
		xpt_free_ccb(ccb);
		return -1;
	}

	ocs_update_tgt(node, fcp, i);
	xpt_rescan(ccb);
	return 0;
}

int32_t
ocs_scsi_new_target(ocs_node_t *node)
{
	ocs_fcport	*fcp = NULL;
	int32_t i;

	fcp = node->sport->tgt_data;
	if (fcp == NULL) {
		printf("%s:FCP is NULL \n", __func__);
		return 0;
	}

	i = ocs_tgt_find(fcp, node);
	
	if (i < 0) {
		ocs_add_new_tgt(node, fcp);
		return 0;
	}

	ocs_update_tgt(node, fcp, i);
	return 0;
}

static void
ocs_delete_target(ocs_t *ocs, ocs_fcport *fcp, int tgt)
{
	struct cam_path *cpath = NULL;

	if (!fcp->sim) { 
		device_printf(ocs->dev, "%s: calling with NULL sim\n", __func__); 
		return;
	}
	
	if (CAM_REQ_CMP == xpt_create_path(&cpath, NULL, cam_sim_path(fcp->sim),
				tgt, CAM_LUN_WILDCARD)) {
		xpt_async(AC_LOST_DEVICE, cpath, NULL);
		
		xpt_free_path(cpath);
	}
}

/*
 * Device Lost Timer Function- when we have decided that a device was lost,
 * we wait a specific period of time prior to telling the OS about lost device.
 *
 * This timer function gets activated when the device was lost. 
 * This function fires once a second and then scans the port database
 * for devices that are marked dead but still have a virtual target assigned.
 * We decrement a counter for that port database entry, and when it hits zero,
 * we tell the OS the device was lost. Timer will be stopped when the device
 * comes back active or removed from the OS.
 */
static void
ocs_ldt(void *arg)
{
	ocs_fcport *fcp = arg;
	taskqueue_enqueue(taskqueue_thread, &fcp->ltask);
}

static void
ocs_ldt_task(void *arg, int pending)
{
	ocs_fcport *fcp = arg;
	ocs_t	*ocs = fcp->ocs;
	int i, more_to_do = 0;
	ocs_fc_target_t *tgt = NULL;

	for (i = 0; i < OCS_MAX_TARGETS; i++) {
		tgt = &fcp->tgt[i];

		if (tgt->state != OCS_TGT_STATE_LOST) {
			continue;
		}

		if ((tgt->gone_timer != 0) && (ocs->attached)){
			tgt->gone_timer -= 1;
			more_to_do++;
			continue;
		}

		if (tgt->is_target) {
			tgt->is_target = 0;
			ocs_delete_target(ocs, fcp, i);
		}

		tgt->state = OCS_TGT_STATE_NONE;
	}

	if (more_to_do) {
		callout_reset(&fcp->ldt, hz, ocs_ldt, fcp);
	} else {
		callout_deactivate(&fcp->ldt);
	}

}

/**
 * @ingroup scsi_api_initiator
 * @brief Delete a SCSI target node
 *
 * Sent by base driver to notify a initiator-client that a remote target 
 * is now gone. The base driver will have terminated all  outstanding IOs 
 * and the initiator-client will receive appropriate completions.
 *
 * The ocs_node_t structure has and elment of type ocs_scsi_ini_node_t named
 * ini_node that is declared and used by a target-server for private
 * information.
 *
 * This function is only called if the base driver is enabled for
 * initiator capability.
 *
 * @param node pointer node being deleted
 * @param reason reason for deleting the target
 *
 * @return Returns OCS_SCSI_CALL_ASYNC if target delete is queued for async 
 * completion and OCS_SCSI_CALL_COMPLETE if call completed or error.
 *
 * @note
 */
int32_t
ocs_scsi_del_target(ocs_node_t *node, ocs_scsi_del_target_reason_e reason)
{
	struct ocs_softc *ocs = node->ocs;
	ocs_fcport	*fcp = NULL;
	ocs_fc_target_t *tgt = NULL;
	int32_t	tgt_id;

	if (ocs == NULL) {
		ocs_log_err(ocs,"OCS is NULL \n");
		return -1;
	}

	fcp = node->sport->tgt_data;
	if (fcp == NULL) {
		ocs_log_err(ocs,"FCP is NULL \n");
		return -1;
	}

	tgt_id = ocs_tgt_find(fcp, node);
	if (tgt_id == -1) {
		ocs_log_err(ocs,"target is invalid\n");
		return -1;
	}

	tgt = &fcp->tgt[tgt_id];

	// IF in shutdown delete target.
	if(!ocs->attached) {
		ocs_delete_target(ocs, fcp, tgt_id);
	} else {
	
		tgt->state = OCS_TGT_STATE_LOST;
		tgt->gone_timer = 30;
		if (!callout_active(&fcp->ldt)) {
			callout_reset(&fcp->ldt, hz, ocs_ldt, fcp);
		}
	}
	
	return 0;
}

/**
 * @brief Initialize SCSI IO
 *
 * Initialize SCSI IO, this function is called once per IO during IO pool
 * allocation so that the target server may initialize any of its own private
 * data.
 *
 * @param io pointer to SCSI IO object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_tgt_io_init(ocs_io_t *io)
{
	return 0;
}

/**
 * @brief Uninitialize SCSI IO
 *
 * Uninitialize target server private data in a SCSI io object
 *
 * @param io pointer to SCSI IO object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_tgt_io_exit(ocs_io_t *io)
{
	return 0;
}

/**
 * @brief Initialize SCSI IO
 *
 * Initialize SCSI IO, this function is called once per IO during IO pool
 * allocation so that the initiator client may initialize any of its own private
 * data.
 *
 * @param io pointer to SCSI IO object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_ini_io_init(ocs_io_t *io)
{
	return 0;
}

/**
 * @brief Uninitialize SCSI IO
 *
 * Uninitialize initiator client private data in a SCSI io object
 *
 * @param io pointer to SCSI IO object
 *
 * @return returns 0 for success, a negative error code value for failure.
 */
int32_t
ocs_scsi_ini_io_exit(ocs_io_t *io)
{
	return 0;
}
/*
 * End of functions required by SCSI base driver API
 ***************************************************************************/

static __inline void
ocs_set_ccb_status(union ccb *ccb, cam_status status)
{
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;
	ccb->ccb_h.status |= status;
}

static int32_t
ocs_task_set_full_or_busy_cb(ocs_io_t *io, ocs_scsi_io_status_e scsi_status,
						uint32_t flags, void *arg)
{

	ocs_target_io_free(io);

	return 0;
}

/**
 * @brief send SCSI task set full or busy status
 *
 * A SCSI task set full or busy response is sent depending on whether
 * another IO is already active on the LUN.
 *
 * @param io pointer to IO context
 *
 * @return returns 0 for success, a negative error code value for failure.
 */

static int32_t
ocs_task_set_full_or_busy(ocs_io_t *io)
{
	ocs_scsi_cmd_resp_t rsp = { 0 };
	ocs_t *ocs = io->ocs;

	/*
	 * If there is another command for the LUN, then send task set full,
	 * if this is the first one, then send the busy status.
	 *
	 * if 'busy sent' is FALSE, set it to TRUE and send BUSY
	 * otherwise send FULL
	 */
	if (atomic_cmpset_acq_32(&io->node->tgt_node.busy_sent, FALSE, TRUE)) {
		rsp.scsi_status = SCSI_STATUS_BUSY; /* Busy */
		printf("%s: busy [%s] tag=%x iiu=%d ihw=%d\n", __func__,
				io->node->display_name, io->tag,
				io->ocs->io_in_use, io->ocs->io_high_watermark);
	} else {
		rsp.scsi_status = SCSI_STATUS_TASK_SET_FULL; /* Task set full */
		printf("%s: full tag=%x iiu=%d\n", __func__, io->tag,
			io->ocs->io_in_use);
	}

	/* Log a message here indicating a busy or task set full state */
	if (OCS_LOG_ENABLE_Q_FULL_BUSY_MSG(ocs)) {
		/* Log Task Set Full */
		if (rsp.scsi_status == SCSI_STATUS_TASK_SET_FULL) {
			/* Task Set Full Message */
			ocs_log_info(ocs, "OCS CAM TASK SET FULL. Tasks >= %d\n",
			 		ocs->io_high_watermark);
		}
		else if (rsp.scsi_status == SCSI_STATUS_BUSY) {
			/* Log Busy Message */
			ocs_log_info(ocs, "OCS CAM SCSI BUSY\n");
		}
	}

	/* Send the response */
	return 
	ocs_scsi_send_resp(io, 0, &rsp, ocs_task_set_full_or_busy_cb, NULL);
}

/**
 * @ingroup cam_io
 * @brief Process target IO completions
 *
 * @param io 
 * @param scsi_status did the IO complete successfully
 * @param flags 
 * @param arg application specific pointer provided in the call to ocs_target_io()
 *
 * @todo
 */
static int32_t ocs_scsi_target_io_cb(ocs_io_t *io, 
				ocs_scsi_io_status_e scsi_status,
				uint32_t flags, void *arg)
{
	union ccb *ccb = arg;
	struct ccb_scsiio *csio = &ccb->csio;
	struct ocs_softc *ocs = csio->ccb_h.ccb_ocs_ptr;
	uint32_t cam_dir = ccb->ccb_h.flags & CAM_DIR_MASK;
	uint32_t io_is_done = 
		(ccb->ccb_h.flags & CAM_SEND_STATUS) == CAM_SEND_STATUS;

	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;

	if (CAM_DIR_NONE != cam_dir) {
		bus_dmasync_op_t op;

		if (CAM_DIR_IN == cam_dir) {
			op = BUS_DMASYNC_POSTREAD;
		} else {
			op = BUS_DMASYNC_POSTWRITE;
		}
		/* Synchronize the DMA memory with the CPU and free the mapping */
		bus_dmamap_sync(ocs->buf_dmat, io->tgt_io.dmap, op);
		if (io->tgt_io.flags & OCS_CAM_IO_F_DMAPPED) {
			bus_dmamap_unload(ocs->buf_dmat, io->tgt_io.dmap);
		}
	}

	if (io->tgt_io.sendresp) {
		io->tgt_io.sendresp = 0;
		ocs_scsi_cmd_resp_t  resp = { 0 };
		io->tgt_io.state = OCS_CAM_IO_RESP;
		resp.scsi_status = scsi_status;
		if (ccb->ccb_h.flags & CAM_SEND_SENSE) {
			resp.sense_data = (uint8_t *)&csio->sense_data;
			resp.sense_data_length = csio->sense_len;
		}
		resp.residual = io->exp_xfer_len - io->transferred;

		return ocs_scsi_send_resp(io, 0, &resp, ocs_scsi_target_io_cb, ccb);
	}

	switch (scsi_status) {
	case OCS_SCSI_STATUS_GOOD:
		ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		break;
	case OCS_SCSI_STATUS_ABORTED:
		ocs_set_ccb_status(ccb, CAM_REQ_ABORTED);
		break;
	default:
		ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
	}

	if (io_is_done) {
		if ((io->tgt_io.flags & OCS_CAM_IO_F_ABORT_NOTIFY) == 0) {
			ocs_target_io_free(io);
		}
	} else {
		io->tgt_io.state = OCS_CAM_IO_DATA_DONE;
		/*device_printf(ocs->dev, "%s: CTIO state=%d tag=%#x\n",
				__func__, io->tgt_io.state, io->tag);*/
	}

	xpt_done(ccb);

	return 0;
}

/**
 * @note	1. Since the CCB is assigned to the ocs_io_t on an XPT_CONT_TARGET_IO
 * 		   action, if an initiator aborts a command prior to the SIM receiving
 * 		   a CTIO, the IO's CCB will be NULL.
 */
static int32_t
ocs_io_abort_cb(ocs_io_t *io, ocs_scsi_io_status_e scsi_status, uint32_t flags, void *arg)
{
	struct ocs_softc *ocs = NULL;
	ocs_io_t	*tmfio = arg;
	ocs_scsi_tmf_resp_e tmf_resp = OCS_SCSI_TMF_FUNCTION_COMPLETE;
	int32_t	rc = 0;

	ocs = io->ocs;

	io->tgt_io.flags &= ~OCS_CAM_IO_F_ABORT_DEV;

	/* A good status indicates the IO was aborted and will be completed in
	 * the IO's completion handler. Handle the other cases here. */
	switch (scsi_status) {
	case OCS_SCSI_STATUS_GOOD:
		break;
	case OCS_SCSI_STATUS_NO_IO:
		break;
	default:
		device_printf(ocs->dev, "%s: unhandled status %d\n",
				__func__, scsi_status);
		tmf_resp = OCS_SCSI_TMF_FUNCTION_REJECTED;
		rc = -1;
	}

	ocs_scsi_send_tmf_resp(tmfio, tmf_resp, NULL, ocs_target_tmf_cb, NULL);

	return rc;
}

/**
 * @ingroup cam_io
 * @brief Process initiator IO completions
 *
 * @param io 
 * @param scsi_status did the IO complete successfully
 * @param rsp pointer to response buffer
 * @param flags 
 * @param arg application specific pointer provided in the call to ocs_target_io()
 *
 * @todo
 */
static int32_t ocs_scsi_initiator_io_cb(ocs_io_t *io,
					ocs_scsi_io_status_e scsi_status,
					ocs_scsi_cmd_resp_t *rsp,
					uint32_t flags, void *arg)
{
	union ccb *ccb = arg;
	struct ccb_scsiio *csio = &ccb->csio;
	struct ocs_softc *ocs = csio->ccb_h.ccb_ocs_ptr;
	uint32_t cam_dir = ccb->ccb_h.flags & CAM_DIR_MASK;
	cam_status ccb_status= CAM_REQ_CMP_ERR;

	if (CAM_DIR_NONE != cam_dir) {
		bus_dmasync_op_t op;

		if (CAM_DIR_IN == cam_dir) {
			op = BUS_DMASYNC_POSTREAD;
		} else {
			op = BUS_DMASYNC_POSTWRITE;
		}
		/* Synchronize the DMA memory with the CPU and free the mapping */
		bus_dmamap_sync(ocs->buf_dmat, io->tgt_io.dmap, op);
		if (io->tgt_io.flags & OCS_CAM_IO_F_DMAPPED) {
			bus_dmamap_unload(ocs->buf_dmat, io->tgt_io.dmap);
		}
	}

	if (scsi_status == OCS_SCSI_STATUS_CHECK_RESPONSE) {
		csio->scsi_status = rsp->scsi_status;
		if (SCSI_STATUS_OK != rsp->scsi_status) {
			ccb_status = CAM_SCSI_STATUS_ERROR;
		}

		csio->resid = rsp->residual;
		if (rsp->residual > 0) {
			uint32_t length = rsp->response_wire_length;
			/* underflow */
			if (csio->dxfer_len == (length + csio->resid)) {
				ccb_status = CAM_REQ_CMP;
			}
		} else if (rsp->residual < 0) {
			ccb_status = CAM_DATA_RUN_ERR;
		}

		if ((rsp->sense_data_length) &&
			!(ccb->ccb_h.flags & (CAM_SENSE_PHYS | CAM_SENSE_PTR))) {
			uint32_t	sense_len = 0;

			ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
			if (rsp->sense_data_length < csio->sense_len) {
				csio->sense_resid = 
					csio->sense_len - rsp->sense_data_length;
				sense_len = rsp->sense_data_length;
			} else {
				csio->sense_resid = 0;
				sense_len = csio->sense_len;
			}
			ocs_memcpy(&csio->sense_data, rsp->sense_data, sense_len);
		}
	} else if (scsi_status != OCS_SCSI_STATUS_GOOD) {
		ccb_status = CAM_REQ_CMP_ERR;
		ocs_set_ccb_status(ccb, ccb_status);
		csio->ccb_h.status |= CAM_DEV_QFRZN;
		xpt_freeze_devq(csio->ccb_h.path, 1);

	} else {
		ccb_status = CAM_REQ_CMP;
	}

	ocs_set_ccb_status(ccb, ccb_status);

	ocs_scsi_io_free(io);

	csio->ccb_h.ccb_io_ptr = NULL;
	csio->ccb_h.ccb_ocs_ptr = NULL;
	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;

	xpt_done(ccb);

	return 0;
}

/**
 * @brief Load scatter-gather list entries into an IO
 *
 * This routine relies on the driver instance's software context pointer and
 * the IO object pointer having been already assigned to hooks in the CCB.
 * Although the routine does not return success/fail, callers can look at the
 * n_sge member to determine if the mapping failed (0 on failure).
 *
 * @param arg pointer to the CAM ccb for this IO
 * @param seg DMA address/length pairs
 * @param nseg number of DMA address/length pairs
 * @param error any errors while mapping the IO
 */
static void
ocs_scsi_dmamap_load(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	ocs_dmamap_load_arg_t *sglarg = (ocs_dmamap_load_arg_t*) arg;

	if (error) {
		printf("%s: seg=%p nseg=%d error=%d\n",
				__func__, seg, nseg, error);
		sglarg->rc = -1;
	} else {
		uint32_t i = 0;
		uint32_t c = 0;

		if ((sglarg->sgl_count + nseg) > sglarg->sgl_max) {
			printf("%s: sgl_count=%d nseg=%d max=%d\n", __func__,
				sglarg->sgl_count, nseg, sglarg->sgl_max);
			sglarg->rc = -2;
			return;
		}

		for (i = 0, c = sglarg->sgl_count; i < nseg; i++, c++) {
			sglarg->sgl[c].addr = seg[i].ds_addr;
			sglarg->sgl[c].len  = seg[i].ds_len;
		}

		sglarg->sgl_count = c;

		sglarg->rc = 0;
	}
}

/**
 * @brief Build a scatter-gather list from a CAM CCB
 *
 * @param ocs the driver instance's software context
 * @param ccb pointer to the CCB
 * @param io pointer to the previously allocated IO object
 * @param sgl pointer to SGL
 * @param sgl_max number of entries in sgl
 *
 * @return 0 on success, non-zero otherwise
 */
static int32_t
ocs_build_scsi_sgl(struct ocs_softc *ocs, union ccb *ccb, ocs_io_t *io,
		ocs_scsi_sgl_t *sgl, uint32_t sgl_max)
{
	ocs_dmamap_load_arg_t dmaarg;
	int32_t	err = 0;

	if (!ocs || !ccb || !io || !sgl) {
		printf("%s: bad param o=%p c=%p i=%p s=%p\n", __func__,
				ocs, ccb, io, sgl);
		return -1;
	}

	io->tgt_io.flags &= ~OCS_CAM_IO_F_DMAPPED;

	dmaarg.sgl = sgl;
	dmaarg.sgl_count = 0;
	dmaarg.sgl_max = sgl_max;
	dmaarg.rc = 0;

	err = bus_dmamap_load_ccb(ocs->buf_dmat, io->tgt_io.dmap, ccb,
			ocs_scsi_dmamap_load, &dmaarg, 0);

	if (err || dmaarg.rc) {
		device_printf(
			ocs->dev, "%s: bus_dmamap_load_ccb error (%d %d)\n",
				__func__, err, dmaarg.rc);
		return -1;
	}

	io->tgt_io.flags |= OCS_CAM_IO_F_DMAPPED;
	return dmaarg.sgl_count;
}

/**
 * @ingroup cam_io
 * @brief Send a target IO
 *
 * @param ocs the driver instance's software context
 * @param ccb pointer to the CCB
 *
 * @return 0 on success, non-zero otherwise
 */
static int32_t
ocs_target_io(struct ocs_softc *ocs, union ccb *ccb)
{
	struct ccb_scsiio *csio = &ccb->csio;
	ocs_io_t *io = NULL;
	uint32_t cam_dir = ccb->ccb_h.flags & CAM_DIR_MASK;
	bool sendstatus = ccb->ccb_h.flags & CAM_SEND_STATUS;
	uint32_t xferlen = csio->dxfer_len;
	int32_t rc = 0;

	io = ocs_scsi_find_io(ocs, csio->tag_id);
	if (io == NULL) {
		ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		panic("bad tag value");
		return 1;
	}

	/* Received an ABORT TASK for this IO */
	if (io->tgt_io.flags & OCS_CAM_IO_F_ABORT_RECV) {
		/*device_printf(ocs->dev,
			"%s: XPT_CONT_TARGET_IO state=%d tag=%#x xid=%#x flags=%#x\n",
			__func__, io->tgt_io.state, io->tag, io->init_task_tag,
			io->tgt_io.flags);*/
		io->tgt_io.flags |= OCS_CAM_IO_F_ABORT_CAM;

		if (ccb->ccb_h.flags & CAM_SEND_STATUS) {
			ocs_set_ccb_status(ccb, CAM_REQ_CMP);
			ocs_target_io_free(io);
			return 1;
		} 

		ocs_set_ccb_status(ccb, CAM_REQ_ABORTED);

		return 1;
	}

	io->tgt_io.app = ccb;

	ocs_set_ccb_status(ccb, CAM_REQ_INPROG);
	ccb->ccb_h.status |= CAM_SIM_QUEUED;

	csio->ccb_h.ccb_ocs_ptr = ocs;
	csio->ccb_h.ccb_io_ptr  = io;

	if ((sendstatus && (xferlen == 0))) {
		ocs_scsi_cmd_resp_t	resp = { 0 };

		ocs_assert(ccb->ccb_h.flags & CAM_SEND_STATUS, -1);

		io->tgt_io.state = OCS_CAM_IO_RESP;

		resp.scsi_status = csio->scsi_status;

		if (ccb->ccb_h.flags & CAM_SEND_SENSE) {
			resp.sense_data = (uint8_t *)&csio->sense_data;
			resp.sense_data_length = csio->sense_len;
		}

		resp.residual = io->exp_xfer_len - io->transferred;
		rc = ocs_scsi_send_resp(io, 0, &resp, ocs_scsi_target_io_cb, ccb);

	} else if (xferlen != 0) {
		ocs_scsi_sgl_t sgl[OCS_FC_MAX_SGL];
		int32_t sgl_count = 0;

		io->tgt_io.state = OCS_CAM_IO_DATA;
		
		if (sendstatus)
			io->tgt_io.sendresp = 1;

		sgl_count = ocs_build_scsi_sgl(ocs, ccb, io, sgl, ARRAY_SIZE(sgl));
		if (sgl_count > 0) {
			if (cam_dir == CAM_DIR_IN) {
				rc = ocs_scsi_send_rd_data(io, 0, NULL, sgl,
						sgl_count, csio->dxfer_len,
						ocs_scsi_target_io_cb, ccb);
			} else if (cam_dir == CAM_DIR_OUT) {
				rc = ocs_scsi_recv_wr_data(io, 0, NULL, sgl,
						sgl_count, csio->dxfer_len,
						ocs_scsi_target_io_cb, ccb);
			} else {
				device_printf(ocs->dev, "%s:"
						" unknown CAM direction %#x\n",
						__func__, cam_dir);
				ocs_set_ccb_status(ccb, CAM_REQ_INVALID);
				rc = 1;
			}
		} else {
			device_printf(ocs->dev, "%s: building SGL failed\n",
						__func__);
			ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
			rc = 1;
		}
	} else {
		device_printf(ocs->dev, "%s: Wrong value xfer and sendstatus"
					" are 0 \n", __func__);
		ocs_set_ccb_status(ccb, CAM_REQ_INVALID);
		rc = 1;

	}

	if (rc) {
		ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		io->tgt_io.state = OCS_CAM_IO_DATA_DONE;
		device_printf(ocs->dev, "%s: CTIO state=%d tag=%#x\n",
				__func__, io->tgt_io.state, io->tag);
	if ((sendstatus && (xferlen == 0))) {
			ocs_target_io_free(io);
		}
	}

	return rc;
}

static int32_t
ocs_target_tmf_cb(ocs_io_t *io, ocs_scsi_io_status_e scsi_status, uint32_t flags,
		void *arg)
{

	/*device_printf(io->ocs->dev, "%s: tag=%x io=%p s=%#x\n",
			 __func__, io->tag, io, scsi_status);*/
	ocs_scsi_io_complete(io);

	return 0;
}

/**
 * @ingroup cam_io
 * @brief Send an initiator IO
 *
 * @param ocs the driver instance's software context
 * @param ccb pointer to the CCB
 *
 * @return 0 on success, non-zero otherwise
 */
static int32_t
ocs_initiator_io(struct ocs_softc *ocs, union ccb *ccb)
{
	int32_t rc;
	struct ccb_scsiio *csio = &ccb->csio;
	struct ccb_hdr *ccb_h = &csio->ccb_h;
	ocs_node_t *node = NULL;
	ocs_io_t *io = NULL;
	ocs_scsi_sgl_t sgl[OCS_FC_MAX_SGL];
	int32_t sgl_count;
	ocs_fcport	*fcp;

	fcp = FCPORT(ocs, cam_sim_bus(xpt_path_sim((ccb)->ccb_h.path)));

	if (fcp->tgt[ccb_h->target_id].state == OCS_TGT_STATE_LOST) {
		device_printf(ocs->dev, "%s: device LOST %d\n", __func__,
							ccb_h->target_id);
		return CAM_REQUEUE_REQ;
	}

	if (fcp->tgt[ccb_h->target_id].state == OCS_TGT_STATE_NONE) {
		device_printf(ocs->dev, "%s: device not ready %d\n", __func__,
							ccb_h->target_id);
		return CAM_SEL_TIMEOUT;
	}

	node = ocs_node_get_instance(ocs, fcp->tgt[ccb_h->target_id].node_id);
	if (node == NULL) {
		device_printf(ocs->dev, "%s: no device %d\n", __func__,
							ccb_h->target_id);
		return CAM_SEL_TIMEOUT;
	}

	if (!node->targ) {
		device_printf(ocs->dev, "%s: not target device %d\n", __func__,
							ccb_h->target_id);
		return CAM_SEL_TIMEOUT;
	}

	io = ocs_scsi_io_alloc(node, OCS_SCSI_IO_ROLE_ORIGINATOR);
	if (io == NULL) {
		device_printf(ocs->dev, "%s: unable to alloc IO\n", __func__);
		return -1;
	}

	/* eventhough this is INI, use target structure as ocs_build_scsi_sgl
	 * only references the tgt_io part of an ocs_io_t */
	io->tgt_io.app = ccb;

	csio->ccb_h.ccb_ocs_ptr = ocs;
	csio->ccb_h.ccb_io_ptr  = io;

	sgl_count = ocs_build_scsi_sgl(ocs, ccb, io, sgl, ARRAY_SIZE(sgl));
	if (sgl_count < 0) {
		ocs_scsi_io_free(io);
		device_printf(ocs->dev, "%s: building SGL failed\n", __func__);
		return -1;
	}

	if (ccb->ccb_h.timeout == CAM_TIME_INFINITY) {
		io->timeout = 0;
	} else if (ccb->ccb_h.timeout == CAM_TIME_DEFAULT) {
		io->timeout = OCS_CAM_IO_TIMEOUT;
	} else {
		io->timeout = ccb->ccb_h.timeout;
	}

	switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_NONE:
		rc = ocs_scsi_send_nodata_io(node, io, ccb_h->target_lun,
				ccb->ccb_h.flags & CAM_CDB_POINTER ? 
				csio->cdb_io.cdb_ptr: csio->cdb_io.cdb_bytes,
				csio->cdb_len,
				ocs_scsi_initiator_io_cb, ccb);
		break;
	case CAM_DIR_IN:
		rc = ocs_scsi_send_rd_io(node, io, ccb_h->target_lun,
				ccb->ccb_h.flags & CAM_CDB_POINTER ? 
				csio->cdb_io.cdb_ptr: csio->cdb_io.cdb_bytes,
				csio->cdb_len,
				NULL,
				sgl, sgl_count, csio->dxfer_len,
				ocs_scsi_initiator_io_cb, ccb);
		break;
	case CAM_DIR_OUT:
		rc = ocs_scsi_send_wr_io(node, io, ccb_h->target_lun,
				ccb->ccb_h.flags & CAM_CDB_POINTER ? 
				csio->cdb_io.cdb_ptr: csio->cdb_io.cdb_bytes,
				csio->cdb_len,
				NULL,
				sgl, sgl_count, csio->dxfer_len,
				ocs_scsi_initiator_io_cb, ccb);
		break;
	default:
		panic("%s invalid data direction %08x\n", __func__, 
							ccb->ccb_h.flags);
		break;
	}

	return rc;
}

static uint32_t
ocs_fcp_change_role(struct ocs_softc *ocs, ocs_fcport *fcp, uint32_t new_role)
{

	uint32_t rc = 0, was = 0, i = 0;
	ocs_vport_spec_t *vport = fcp->vport;

	for (was = 0, i = 0; i < (ocs->num_vports + 1); i++) {
		if (FCPORT(ocs, i)->role != KNOB_ROLE_NONE)
		was++;
	}

	// Physical port	
	if ((was == 0) || (vport == NULL)) { 
		fcp->role = new_role;
		if (vport == NULL) {
			ocs->enable_ini = (new_role & KNOB_ROLE_INITIATOR)? 1:0;
			ocs->enable_tgt = (new_role & KNOB_ROLE_TARGET)? 1:0;
		} else {
			vport->enable_ini = (new_role & KNOB_ROLE_INITIATOR)? 1:0;
			vport->enable_tgt = (new_role & KNOB_ROLE_TARGET)? 1:0;
		}

		rc = ocs_xport_control(ocs->xport, OCS_XPORT_PORT_OFFLINE);
		if (rc) {
			ocs_log_debug(ocs, "port offline failed : %d\n", rc);
		}

		rc = ocs_xport_control(ocs->xport, OCS_XPORT_PORT_ONLINE);
		if (rc) {
			ocs_log_debug(ocs, "port online failed : %d\n", rc);
		}
		
		return 0;
	}
	
	if ((fcp->role != KNOB_ROLE_NONE)){
		fcp->role = new_role;
		vport->enable_ini = (new_role & KNOB_ROLE_INITIATOR)? 1:0;
		vport->enable_tgt = (new_role & KNOB_ROLE_TARGET)? 1:0;
		/* New Sport will be created in sport deleted cb */
		return ocs_sport_vport_del(ocs, ocs->domain, vport->wwpn, vport->wwnn);
	}

	fcp->role = new_role;
	
	vport->enable_ini = (new_role & KNOB_ROLE_INITIATOR)? 1:0;
	vport->enable_tgt = (new_role & KNOB_ROLE_TARGET)? 1:0;

	if (fcp->role != KNOB_ROLE_NONE) {
		return ocs_sport_vport_alloc(ocs->domain, vport);
	}

	return (0);
}

/**
 * @ingroup cam_api
 * @brief Process CAM actions
 *
 * The driver supplies this routine to the CAM during intialization and
 * is the main entry point for processing CAM Control Blocks (CCB)
 *
 * @param sim pointer to the SCSI Interface Module
 * @param ccb CAM control block
 *
 * @todo
 *  - populate path inquiry data via info retrieved from SLI port
 */
static void
ocs_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ocs_softc *ocs = (struct ocs_softc *)cam_sim_softc(sim);
	struct ccb_hdr	*ccb_h = &ccb->ccb_h;

	int32_t	rc, bus;
	bus = cam_sim_bus(sim);

	switch (ccb_h->func_code) {
	case XPT_SCSI_IO:

		if ((ccb->ccb_h.flags & CAM_CDB_POINTER) != 0) {
			if ((ccb->ccb_h.flags & CAM_CDB_PHYS) != 0) {
				ccb->ccb_h.status = CAM_REQ_INVALID;
				xpt_done(ccb);
				break;
			}
		}

		rc = ocs_initiator_io(ocs, ccb);
		if (0 == rc) {
			ocs_set_ccb_status(ccb, CAM_REQ_INPROG | CAM_SIM_QUEUED);
			break;
		} else {
		  	if (rc == CAM_REQUEUE_REQ) {
				cam_freeze_devq(ccb->ccb_h.path);
				cam_release_devq(ccb->ccb_h.path, RELSIM_RELEASE_AFTER_TIMEOUT, 0, 100, 0);
				ccb->ccb_h.status = CAM_REQUEUE_REQ;
				xpt_done(ccb);
				break;
			}

			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
			if (rc > 0) {
				ocs_set_ccb_status(ccb, rc);
			} else {
				ocs_set_ccb_status(ccb, CAM_SEL_TIMEOUT);
			}
		}
		xpt_done(ccb);
		break;
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;
		struct ccb_pathinq_settings_fc *fc = &cpi->xport_specific.fc;
		ocs_fcport *fcp = FCPORT(ocs, bus);

		uint64_t wwn = 0;
		ocs_xport_stats_t value;

		cpi->version_num = 1;

		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_SPC;

		if (ocs->ocs_xport == OCS_XPORT_FC) {
			cpi->transport = XPORT_FC;
		} else {
			cpi->transport = XPORT_UNKNOWN;
		}

		cpi->transport_version = 0;

		/* Set the transport parameters of the SIM */
		ocs_xport_status(ocs->xport, OCS_XPORT_LINK_SPEED, &value);
		fc->bitrate = value.value * 1000;	/* speed in Mbps */

		wwn = *((uint64_t *)ocs_scsi_get_property_ptr(ocs, OCS_SCSI_WWPN));
		fc->wwpn = be64toh(wwn);

		wwn = *((uint64_t *)ocs_scsi_get_property_ptr(ocs, OCS_SCSI_WWNN));
		fc->wwnn = be64toh(wwn);

		fc->port = fcp->fc_id;

		if (ocs->config_tgt) {
			cpi->target_sprt =
				PIT_PROCESSOR | PIT_DISCONNECT | PIT_TERM_IO;
		}

		cpi->hba_misc = PIM_NOBUSRESET | PIM_UNMAPPED;
		cpi->hba_misc |= PIM_EXTLUNS | PIM_NOSCAN;

		cpi->hba_inquiry = PI_TAG_ABLE; 
		cpi->max_target = OCS_MAX_TARGETS;
		cpi->initiator_id = ocs->max_remote_nodes + 1;

		if (!ocs->enable_ini) {
			cpi->hba_misc |= PIM_NOINITIATOR;
		}

		cpi->max_lun = OCS_MAX_LUN;
		cpi->bus_id = cam_sim_bus(sim);

		/* Need to supply a base transfer speed prior to linking up
		 * Worst case, this would be FC 1Gbps */
		cpi->base_transfer_speed = 1 * 1000 * 1000;

		/* Calculate the max IO supported
		 * Worst case would be an OS page per SGL entry */
		cpi->maxio = PAGE_SIZE * 
			(ocs_scsi_get_property(ocs, OCS_SCSI_MAX_SGL) - 1);

		strncpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strncpy(cpi->hba_vid, "Emulex", HBA_IDLEN);
		strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);

		cpi->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	}
	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings *cts = &ccb->cts;
		struct ccb_trans_settings_scsi *scsi = &cts->proto_specific.scsi;
		struct ccb_trans_settings_fc *fc = &cts->xport_specific.fc;
		ocs_xport_stats_t value;
		ocs_fcport *fcp = FCPORT(ocs, bus);
		ocs_fc_target_t *tgt = NULL;

		if (ocs->ocs_xport != OCS_XPORT_FC) {
			ocs_set_ccb_status(ccb, CAM_REQ_INVALID);
			xpt_done(ccb);
			break;
		}

		if (cts->ccb_h.target_id > OCS_MAX_TARGETS) {
			ocs_set_ccb_status(ccb, CAM_DEV_NOT_THERE);
			xpt_done(ccb);
			break;
		}

		tgt = &fcp->tgt[cts->ccb_h.target_id];
		if (tgt->state == OCS_TGT_STATE_NONE) { 
			ocs_set_ccb_status(ccb, CAM_DEV_NOT_THERE);
			xpt_done(ccb);
			break;
		}

		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_SPC2;
		cts->transport = XPORT_FC;
		cts->transport_version = 2;

		scsi->valid = CTS_SCSI_VALID_TQ;
		scsi->flags = CTS_SCSI_FLAGS_TAG_ENB;

		/* speed in Mbps */
		ocs_xport_status(ocs->xport, OCS_XPORT_LINK_SPEED, &value);
		fc->bitrate = value.value * 100;

		fc->wwpn = tgt->wwpn;

		fc->wwnn = tgt->wwnn;

		fc->port = tgt->port_id;

		fc->valid = CTS_FC_VALID_SPEED |
			CTS_FC_VALID_WWPN |
			CTS_FC_VALID_WWNN |
			CTS_FC_VALID_PORT;

		ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		xpt_done(ccb);
		break;
	}
	case XPT_SET_TRAN_SETTINGS:
		ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		xpt_done(ccb);
		break;

	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, TRUE);
		xpt_done(ccb);
		break;

	case XPT_GET_SIM_KNOB:
	{
		struct ccb_sim_knob *knob = &ccb->knob;
		uint64_t wwn = 0;
		ocs_fcport *fcp = FCPORT(ocs, bus);

		if (ocs->ocs_xport != OCS_XPORT_FC) {
			ocs_set_ccb_status(ccb, CAM_REQ_INVALID);
			xpt_done(ccb);
			break;
		}
		
		if (bus == 0) {
			wwn = *((uint64_t *)ocs_scsi_get_property_ptr(ocs,
						OCS_SCSI_WWNN));
			knob->xport_specific.fc.wwnn = be64toh(wwn);

			wwn = *((uint64_t *)ocs_scsi_get_property_ptr(ocs,
						OCS_SCSI_WWPN));
			knob->xport_specific.fc.wwpn = be64toh(wwn);
		} else {
			knob->xport_specific.fc.wwnn = fcp->vport->wwnn;
			knob->xport_specific.fc.wwpn = fcp->vport->wwpn;
		}

		knob->xport_specific.fc.role = fcp->role;
		knob->xport_specific.fc.valid = KNOB_VALID_ADDRESS |
						KNOB_VALID_ROLE;

		ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		xpt_done(ccb);
		break;
	}
	case XPT_SET_SIM_KNOB:
	{
		struct ccb_sim_knob *knob = &ccb->knob;
		bool role_changed = FALSE;
		ocs_fcport *fcp = FCPORT(ocs, bus);

		if (ocs->ocs_xport != OCS_XPORT_FC) {
			ocs_set_ccb_status(ccb, CAM_REQ_INVALID);
			xpt_done(ccb);
			break;
		}
			
		if (knob->xport_specific.fc.valid & KNOB_VALID_ADDRESS) {
			device_printf(ocs->dev, 
				"%s: XPT_SET_SIM_KNOB wwnn=%llx wwpn=%llx\n",
					__func__,
					(unsigned long long)knob->xport_specific.fc.wwnn,
					(unsigned long long)knob->xport_specific.fc.wwpn);
		}

		if (knob->xport_specific.fc.valid & KNOB_VALID_ROLE) {
			switch (knob->xport_specific.fc.role) {
			case KNOB_ROLE_NONE:
				if (fcp->role != KNOB_ROLE_NONE) {
					role_changed = TRUE;
				}
				break;
			case KNOB_ROLE_TARGET:
				if (fcp->role != KNOB_ROLE_TARGET) {
					role_changed = TRUE;
				}
				break;
			case KNOB_ROLE_INITIATOR:
				if (fcp->role != KNOB_ROLE_INITIATOR) {
					role_changed = TRUE;
				}
				break;
			case KNOB_ROLE_BOTH:
				if (fcp->role != KNOB_ROLE_BOTH) {
					role_changed = TRUE;
				}
				break;
			default:
				device_printf(ocs->dev,
					"%s: XPT_SET_SIM_KNOB unsupported role: %d\n",
					__func__, knob->xport_specific.fc.role);
			}

			if (role_changed) {
				device_printf(ocs->dev,
						"BUS:%d XPT_SET_SIM_KNOB old_role: %d new_role: %d\n",
						bus, fcp->role, knob->xport_specific.fc.role);

				ocs_fcp_change_role(ocs, fcp, knob->xport_specific.fc.role);
			}
		}

		

		ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		xpt_done(ccb);
		break;
	}
	case XPT_ABORT:
	{
		union ccb *accb = ccb->cab.abort_ccb;

		switch (accb->ccb_h.func_code) {
		case XPT_ACCEPT_TARGET_IO:
			ocs_abort_atio(ocs, ccb);
			break;
		case XPT_IMMEDIATE_NOTIFY:
			ocs_abort_inot(ocs, ccb);
			break;
		case XPT_SCSI_IO:
			rc = ocs_abort_initiator_io(ocs, accb);
			if (rc) {
				ccb->ccb_h.status = CAM_UA_ABORT;
			} else {
				ccb->ccb_h.status = CAM_REQ_CMP;
			}

			break;
		default:
			printf("abort of unknown func %#x\n",
					accb->ccb_h.func_code);
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		}
		break;
	}
	case XPT_RESET_BUS:
		if (ocs_xport_control(ocs->xport, OCS_XPORT_PORT_OFFLINE) == 0) {
			rc = ocs_xport_control(ocs->xport, OCS_XPORT_PORT_ONLINE);
			if (rc) {
				ocs_log_debug(ocs, "Failed to bring port online"
								" : %d\n", rc);
			}
			ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		} else {
			ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		}
		xpt_done(ccb);
		break;
	case XPT_RESET_DEV:
	{
		ocs_node_t	*node = NULL;
		ocs_io_t	*io = NULL;
		int32_t		rc = 0;
		ocs_fcport *fcp = FCPORT(ocs, bus);

		node = ocs_node_get_instance(ocs, fcp->tgt[ccb_h->target_id].node_id);
		if (node == NULL) {
			device_printf(ocs->dev, "%s: no device %d\n",
						__func__, ccb_h->target_id);
			ocs_set_ccb_status(ccb, CAM_DEV_NOT_THERE);
			xpt_done(ccb);
			break;
		}

		io = ocs_scsi_io_alloc(node, OCS_SCSI_IO_ROLE_ORIGINATOR);
		if (io == NULL) {
			device_printf(ocs->dev, "%s: unable to alloc IO\n",
								 __func__);
			ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
			xpt_done(ccb);
			break;
		}

		rc = ocs_scsi_send_tmf(node, io, NULL, ccb_h->target_lun,
				OCS_SCSI_TMF_LOGICAL_UNIT_RESET,
				NULL, 0, 0,	/* sgl, sgl_count, length */
				ocs_initiator_tmf_cb, NULL/*arg*/);

		if (rc) {
			ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
		} else {
			ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		}
		
		if (node->fcp2device) {
			ocs_reset_crn(node, ccb_h->target_lun);
		}

		xpt_done(ccb);
		break;
	}
	case XPT_EN_LUN:	/* target support */
	{
		ocs_tgt_resource_t *trsrc = NULL;
		uint32_t	status = 0;
		ocs_fcport *fcp = FCPORT(ocs, bus);

		device_printf(ocs->dev, "XPT_EN_LUN %sable %d:%d\n",
				ccb->cel.enable ? "en" : "dis",
				ccb->ccb_h.target_id,
				(unsigned int)ccb->ccb_h.target_lun);

		trsrc = ocs_tgt_resource_get(fcp, &ccb->ccb_h, &status);
		if (trsrc) {
			trsrc->enabled = ccb->cel.enable;

			/* Abort all ATIO/INOT on LUN disable */
			if (trsrc->enabled == FALSE) {
				ocs_tgt_resource_abort(ocs, trsrc);
			} else {
				STAILQ_INIT(&trsrc->atio);
				STAILQ_INIT(&trsrc->inot);
			}
			status = CAM_REQ_CMP;
		}

		ocs_set_ccb_status(ccb, status);
		xpt_done(ccb);
		break;
	}
	/*
	 * The flow of target IOs in CAM is:
	 *  - CAM supplies a number of CCBs to the driver used for received
	 *    commands.
	 *  - when the driver receives a command, it copies the relevant
	 *    information to the CCB and returns it to the CAM using xpt_done()
	 *  - after the target server processes the request, it creates
	 *    a new CCB containing information on how to continue the IO and 
	 *    passes that to the driver
	 *  - the driver processes the "continue IO" (a.k.a CTIO) CCB
	 *  - once the IO completes, the driver returns the CTIO to the CAM 
	 *    using xpt_done()
	 */
	case XPT_ACCEPT_TARGET_IO:	/* used to inform upper layer of 
						received CDB (a.k.a. ATIO) */
	case XPT_IMMEDIATE_NOTIFY:	/* used to inform upper layer of other
							 event (a.k.a. INOT) */
	{
		ocs_tgt_resource_t *trsrc = NULL;
		uint32_t	status = 0;
		ocs_fcport *fcp = FCPORT(ocs, bus);

		/*printf("XPT_%s %p\n", ccb_h->func_code == XPT_ACCEPT_TARGET_IO ?
				"ACCEPT_TARGET_IO" : "IMMEDIATE_NOTIFY", ccb);*/
		trsrc = ocs_tgt_resource_get(fcp, &ccb->ccb_h, &status);
		if (trsrc == NULL) {
			ocs_set_ccb_status(ccb, CAM_DEV_NOT_THERE);
			xpt_done(ccb);
			break;
		}

		if (XPT_ACCEPT_TARGET_IO == ccb->ccb_h.func_code) {
			struct ccb_accept_tio *atio = NULL;

			atio = (struct ccb_accept_tio *)ccb;
			atio->init_id = 0x0badbeef;
			atio->tag_id  = 0xdeadc0de;

			STAILQ_INSERT_TAIL(&trsrc->atio, &ccb->ccb_h, 
					sim_links.stqe);
		} else {
			STAILQ_INSERT_TAIL(&trsrc->inot, &ccb->ccb_h, 
					sim_links.stqe);
		}
		ccb->ccb_h.ccb_io_ptr  = NULL;
		ccb->ccb_h.ccb_ocs_ptr = ocs;
		ocs_set_ccb_status(ccb, CAM_REQ_INPROG);
		/*
		 * These actions give resources to the target driver.
		 * If we didn't return here, this function would call
		 * xpt_done(), signaling to the upper layers that an
		 * IO or other event had arrived.
		 */
		break;
	}
	case XPT_NOTIFY_ACKNOWLEDGE:
	{
		ocs_io_t *io = NULL;
		ocs_io_t *abortio = NULL;

		/* Get the IO reference for this tag */
		io = ocs_scsi_find_io(ocs, ccb->cna2.tag_id);
		if (io == NULL) {
			device_printf(ocs->dev,
				"%s: XPT_NOTIFY_ACKNOWLEDGE no IO with tag %#x\n",
					__func__, ccb->cna2.tag_id);
			ocs_set_ccb_status(ccb, CAM_REQ_CMP_ERR);
			xpt_done(ccb);
			break;
		}

		abortio = io->tgt_io.app;
		if (abortio) {
			abortio->tgt_io.flags &= ~OCS_CAM_IO_F_ABORT_NOTIFY;
			device_printf(ocs->dev,
				"%s: XPT_NOTIFY_ACK state=%d tag=%#x xid=%#x"
				" flags=%#x\n",	__func__, abortio->tgt_io.state,
				abortio->tag, abortio->init_task_tag,
					abortio->tgt_io.flags);
			/* TMF response was sent in abort callback */
		} else {
			ocs_scsi_send_tmf_resp(io, 
					OCS_SCSI_TMF_FUNCTION_COMPLETE,
					NULL, ocs_target_tmf_cb, NULL);
		}

		ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		xpt_done(ccb);
		break;
	}
	case XPT_CONT_TARGET_IO:	/* continue target IO, sending data/response (a.k.a. CTIO) */
		if (ocs_target_io(ocs, ccb)) {
			device_printf(ocs->dev, 
				"XPT_CONT_TARGET_IO failed flags=%x tag=%#x\n",
				ccb->ccb_h.flags, ccb->csio.tag_id);
			xpt_done(ccb);
		}
		break;
	default:
		device_printf(ocs->dev, "unhandled func_code = %#x\n",
				ccb_h->func_code);
		ccb_h->status = CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	}
}

/**
 * @ingroup cam_api
 * @brief Process events
 *
 * @param sim pointer to the SCSI Interface Module
 *
 */
static void
ocs_poll(struct cam_sim *sim)
{
	printf("%s\n", __func__);
}

static int32_t
ocs_initiator_tmf_cb(ocs_io_t *io, ocs_scsi_io_status_e scsi_status,
		ocs_scsi_cmd_resp_t *rsp, uint32_t flags, void *arg)
{
	int32_t	rc = 0;

	switch (scsi_status) {
	case OCS_SCSI_STATUS_GOOD:
	case OCS_SCSI_STATUS_NO_IO:
		break;
	case OCS_SCSI_STATUS_CHECK_RESPONSE:
		if (rsp->response_data_length == 0) {
			ocs_log_test(io->ocs, "check response without data?!?\n");
			rc = -1;
			break;
		}

		if (rsp->response_data[3] != 0) {
			ocs_log_test(io->ocs, "TMF status %08x\n",
				be32toh(*((uint32_t *)rsp->response_data)));
			rc = -1;
			break;
		}
		break;
	default:
		ocs_log_test(io->ocs, "status=%#x\n", scsi_status);
		rc = -1;
	}

	ocs_scsi_io_free(io);

	return rc;
}

/**
 * @brief lookup target resource structure
 *
 * Arbitrarily support
 *  - wildcard target ID + LU
 *  - 0 target ID + non-wildcard LU
 *
 * @param ocs the driver instance's software context
 * @param ccb_h pointer to the CCB header
 * @param status returned status value
 *
 * @return pointer to the target resource, NULL if none available (e.g. if LU
 * 	   is not enabled)
 */
static ocs_tgt_resource_t *ocs_tgt_resource_get(ocs_fcport *fcp, 
				struct ccb_hdr *ccb_h, uint32_t *status)
{
	target_id_t	tid = ccb_h->target_id;
	lun_id_t	lun = ccb_h->target_lun;

	if (CAM_TARGET_WILDCARD == tid) {
		if (CAM_LUN_WILDCARD != lun) {
			*status = CAM_LUN_INVALID;
			return NULL;
		}
		return &fcp->targ_rsrc_wildcard;
	} else {
		if (lun < OCS_MAX_LUN) {
			return &fcp->targ_rsrc[lun];
		} else {
			*status = CAM_LUN_INVALID;
			return NULL;
		}
	} 

}

static int32_t
ocs_tgt_resource_abort(struct ocs_softc *ocs, ocs_tgt_resource_t *trsrc)
{
	union ccb *ccb = NULL;
	uint32_t	count;

	count = 0;
	do {
		ccb = (union ccb *)STAILQ_FIRST(&trsrc->atio);
		if (ccb) {
			STAILQ_REMOVE_HEAD(&trsrc->atio, sim_links.stqe);
			ccb->ccb_h.status = CAM_REQ_ABORTED;
			xpt_done(ccb);
			count++;
		}
	} while (ccb);

	count = 0;
	do {
		ccb = (union ccb *)STAILQ_FIRST(&trsrc->inot);
		if (ccb) {
			STAILQ_REMOVE_HEAD(&trsrc->inot, sim_links.stqe);
			ccb->ccb_h.status = CAM_REQ_ABORTED;
			xpt_done(ccb);
			count++;
		}
	} while (ccb);

	return 0;
}

static void
ocs_abort_atio(struct ocs_softc *ocs, union ccb *ccb)
{

	ocs_io_t	*aio = NULL;
	ocs_tgt_resource_t *trsrc = NULL;
	uint32_t	status = CAM_REQ_INVALID;
	struct ccb_hdr *cur = NULL;
	union ccb *accb = ccb->cab.abort_ccb;

	int bus = cam_sim_bus(xpt_path_sim((ccb)->ccb_h.path));
	ocs_fcport *fcp = FCPORT(ocs, bus); 

	trsrc = ocs_tgt_resource_get(fcp, &accb->ccb_h, &status);
	if (trsrc != NULL) {
		STAILQ_FOREACH(cur, &trsrc->atio, sim_links.stqe) {
			if (cur != &accb->ccb_h) 
				continue;

			STAILQ_REMOVE(&trsrc->atio, cur, ccb_hdr,
							sim_links.stqe);
			accb->ccb_h.status = CAM_REQ_ABORTED;
			xpt_done(accb);
			ocs_set_ccb_status(ccb, CAM_REQ_CMP);
			return;
		}
	}

	/* if the ATIO has a valid IO pointer, CAM is telling
	 * the driver that the ATIO (which represents the entire
	 * exchange) has been aborted. */

	aio = accb->ccb_h.ccb_io_ptr;
	if (aio == NULL) {
		ccb->ccb_h.status = CAM_UA_ABORT;
		return;
	}

	device_printf(ocs->dev,
			"%s: XPT_ABORT ATIO state=%d tag=%#x"
			" xid=%#x flags=%#x\n",	__func__, 
			aio->tgt_io.state, aio->tag, 
			aio->init_task_tag, aio->tgt_io.flags);
	/* Expectations are:
	 *  - abort task was received
	 *  - already aborted IO in the DEVICE
	 *  - already received NOTIFY ACKNOWLEDGE */

	if ((aio->tgt_io.flags & OCS_CAM_IO_F_ABORT_RECV) == 0) {
		device_printf(ocs->dev,	"%s: abort not received or io completed \n", __func__);
		ocs_set_ccb_status(ccb, CAM_REQ_CMP);
		return;
	}

	aio->tgt_io.flags |= OCS_CAM_IO_F_ABORT_CAM;
	ocs_target_io_free(aio);
	ocs_set_ccb_status(ccb, CAM_REQ_CMP);
	
	return;
}

static void
ocs_abort_inot(struct ocs_softc *ocs, union ccb *ccb)
{
	ocs_tgt_resource_t *trsrc = NULL;
	uint32_t	status = CAM_REQ_INVALID;
	struct ccb_hdr *cur = NULL;
	union ccb *accb = ccb->cab.abort_ccb;

	int bus = cam_sim_bus(xpt_path_sim((ccb)->ccb_h.path));
	ocs_fcport *fcp = FCPORT(ocs, bus); 

	trsrc = ocs_tgt_resource_get(fcp, &accb->ccb_h, &status);
	if (trsrc) {
		STAILQ_FOREACH(cur, &trsrc->inot, sim_links.stqe) {
			if (cur != &accb->ccb_h) 
				continue;

			STAILQ_REMOVE(&trsrc->inot, cur, ccb_hdr,
							sim_links.stqe);
			accb->ccb_h.status = CAM_REQ_ABORTED;
			xpt_done(accb);
			ocs_set_ccb_status(ccb, CAM_REQ_CMP);
			return;
		}
	}

	ocs_set_ccb_status(ccb, CAM_UA_ABORT);
	return;
}

static uint32_t
ocs_abort_initiator_io(struct ocs_softc *ocs, union ccb *accb)
{

	ocs_node_t	*node = NULL;
	ocs_io_t	*io = NULL;
	int32_t		rc = 0;
	struct ccb_scsiio *csio = &accb->csio;

	ocs_fcport *fcp = FCPORT(ocs, cam_sim_bus(xpt_path_sim((accb)->ccb_h.path)));
	node = ocs_node_get_instance(ocs, fcp->tgt[accb->ccb_h.target_id].node_id);
	if (node == NULL) {
		device_printf(ocs->dev, "%s: no device %d\n", 
				__func__, accb->ccb_h.target_id);
		ocs_set_ccb_status(accb, CAM_DEV_NOT_THERE);
		xpt_done(accb);
		return (-1);
	}

	io = ocs_scsi_io_alloc(node, OCS_SCSI_IO_ROLE_ORIGINATOR);
	if (io == NULL) {
		device_printf(ocs->dev,
				"%s: unable to alloc IO\n", __func__);
		ocs_set_ccb_status(accb, CAM_REQ_CMP_ERR);
		xpt_done(accb);
		return (-1);
	}

	rc = ocs_scsi_send_tmf(node, io, 
			(ocs_io_t *)csio->ccb_h.ccb_io_ptr,
			accb->ccb_h.target_lun,
			OCS_SCSI_TMF_ABORT_TASK,
			NULL, 0, 0,
			ocs_initiator_tmf_cb, NULL/*arg*/);

	return rc;
}

void
ocs_scsi_ini_ddump(ocs_textbuf_t *textbuf, ocs_scsi_ddump_type_e type, void *obj)
{
	switch(type) {
	case OCS_SCSI_DDUMP_DEVICE: {
		//ocs_t *ocs = obj;
		break;
	}
	case OCS_SCSI_DDUMP_DOMAIN: {
		//ocs_domain_t *domain = obj;
		break;
	}
	case OCS_SCSI_DDUMP_SPORT: {
		//ocs_sport_t *sport = obj;
		break;
	}
	case OCS_SCSI_DDUMP_NODE: {
		//ocs_node_t *node = obj;
		break;
	}
	case OCS_SCSI_DDUMP_IO: {
		//ocs_io_t *io = obj;
		break;
	}
	default: {
		break;
	}
	}
}

void
ocs_scsi_tgt_ddump(ocs_textbuf_t *textbuf, ocs_scsi_ddump_type_e type, void *obj)
{
	switch(type) {
	case OCS_SCSI_DDUMP_DEVICE: {
		//ocs_t *ocs = obj;
		break;
	}
	case OCS_SCSI_DDUMP_DOMAIN: {
		//ocs_domain_t *domain = obj;
		break;
	}
	case OCS_SCSI_DDUMP_SPORT: {
		//ocs_sport_t *sport = obj;
		break;
	}
	case OCS_SCSI_DDUMP_NODE: {
		//ocs_node_t *node = obj;
		break;
	}
	case OCS_SCSI_DDUMP_IO: {
		ocs_io_t *io = obj;
		char *state_str = NULL;

		switch (io->tgt_io.state) {
		case OCS_CAM_IO_FREE:
			state_str = "FREE";
			break;
		case OCS_CAM_IO_COMMAND:
			state_str = "COMMAND";
			break;
		case OCS_CAM_IO_DATA:
			state_str = "DATA";
			break;
		case OCS_CAM_IO_DATA_DONE:
			state_str = "DATA_DONE";
			break;
		case OCS_CAM_IO_RESP:
			state_str = "RESP";
			break;
		default:
			state_str = "xxx BAD xxx";
		}
		ocs_ddump_value(textbuf, "cam_st", "%s", state_str);
		if (io->tgt_io.app) {
			ocs_ddump_value(textbuf, "cam_flags", "%#x",
				((union ccb *)(io->tgt_io.app))->ccb_h.flags);
			ocs_ddump_value(textbuf, "cam_status", "%#x",
				((union ccb *)(io->tgt_io.app))->ccb_h.status);
		}

		break;
	}
	default: {
		break;
	}
	}
}

int32_t ocs_scsi_get_block_vaddr(ocs_io_t *io, uint64_t blocknumber,
				ocs_scsi_vaddr_len_t addrlen[],
				uint32_t max_addrlen, void **dif_vaddr)
{
	return -1;
}

uint32_t
ocs_get_crn(ocs_node_t *node, uint8_t *crn, uint64_t lun)
{
	uint32_t idx;
	struct ocs_lun_crn *lcrn = NULL;
	idx = lun % OCS_MAX_LUN;

	lcrn = node->ini_node.lun_crn[idx];

	if (lcrn == NULL) {
		lcrn = ocs_malloc(node->ocs, sizeof(struct ocs_lun_crn),
					M_ZERO|M_NOWAIT);
		if (lcrn == NULL) {
			return (1);
		}
		
		lcrn->lun = lun;
		node->ini_node.lun_crn[idx] = lcrn;
	}

	if (lcrn->lun != lun) {
		return (1);
	}	
	
	if (lcrn->crnseed == 0)
		lcrn->crnseed = 1;

	*crn = lcrn->crnseed++;
	return (0);
}

void
ocs_del_crn(ocs_node_t *node)
{
	uint32_t i;
	struct ocs_lun_crn *lcrn = NULL;
	
	for(i = 0; i < OCS_MAX_LUN; i++) {
		lcrn = node->ini_node.lun_crn[i];
		if (lcrn) {
			ocs_free(node->ocs, lcrn, sizeof(*lcrn));
		}
	}

	return;
}

void
ocs_reset_crn(ocs_node_t *node, uint64_t lun)
{
	uint32_t idx;
	struct ocs_lun_crn *lcrn = NULL;
	idx = lun % OCS_MAX_LUN;

	lcrn = node->ini_node.lun_crn[idx];
	if (lcrn)
		lcrn->crnseed = 0;

	return;
}
