/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/isci/isci.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/isci/scil/intel_sas.h>

#include <dev/isci/scil/sci_util.h>

#include <dev/isci/scil/scif_io_request.h>
#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scif_remote_device.h>
#include <dev/isci/scil/scif_user_callback.h>

#include <dev/isci/scil/scic_io_request.h>
#include <dev/isci/scil/scic_user_callback.h>

/**
 * @brief This user callback will inform the user that an IO request has
 *        completed.
 *
 * @param[in]  controller This parameter specifies the controller on
 *             which the IO request is completing.
 * @param[in]  remote_device This parameter specifies the remote device on
 *             which this request is completing.
 * @param[in]  io_request This parameter specifies the IO request that has
 *             completed.
 * @param[in]  completion_status This parameter specifies the results of
 *             the IO request operation.  SCI_IO_SUCCESS indicates
 *             successful completion.
 *
 * @return none
 */
void
scif_cb_io_request_complete(SCI_CONTROLLER_HANDLE_T scif_controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_IO_REQUEST_HANDLE_T io_request, SCI_IO_STATUS completion_status)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)sci_object_get_association(io_request);

	scif_controller_complete_io(scif_controller, remote_device, io_request);
	isci_io_request_complete(scif_controller, remote_device, isci_request,
	    completion_status);
}

void
isci_io_request_complete(SCI_CONTROLLER_HANDLE_T scif_controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    struct ISCI_IO_REQUEST *isci_request, SCI_IO_STATUS completion_status)
{
	struct ISCI_CONTROLLER *isci_controller;
	struct ISCI_REMOTE_DEVICE *isci_remote_device;
	union ccb *ccb;
	BOOL complete_ccb;
	struct ccb_scsiio *csio;

	complete_ccb = TRUE;
	isci_controller = (struct ISCI_CONTROLLER *) sci_object_get_association(scif_controller);
	isci_remote_device =
		(struct ISCI_REMOTE_DEVICE *) sci_object_get_association(remote_device);

	ccb = isci_request->ccb;
	csio = &ccb->csio;
	ccb->ccb_h.status &= ~CAM_STATUS_MASK;

	switch (completion_status) {
	case SCI_IO_SUCCESS:
	case SCI_IO_SUCCESS_COMPLETE_BEFORE_START:
#if __FreeBSD_version >= 900026
		if (ccb->ccb_h.func_code == XPT_SMP_IO) {
			void *smp_response =
			    scif_io_request_get_response_iu_address(
			        isci_request->sci_object);

			memcpy(ccb->smpio.smp_response, smp_response,
			    ccb->smpio.smp_response_len);
		}
#endif
		ccb->ccb_h.status |= CAM_REQ_CMP;
		break;

	case SCI_IO_SUCCESS_IO_DONE_EARLY:
		ccb->ccb_h.status |= CAM_REQ_CMP;
		ccb->csio.resid = ccb->csio.dxfer_len -
		    scif_io_request_get_number_of_bytes_transferred(
		        isci_request->sci_object);
		break;

	case SCI_IO_FAILURE_RESPONSE_VALID:
	{
		SCI_SSP_RESPONSE_IU_T * response_buffer;
		uint32_t sense_length;
		int error_code, sense_key, asc, ascq;

		response_buffer = (SCI_SSP_RESPONSE_IU_T *)
		    scif_io_request_get_response_iu_address(
		        isci_request->sci_object);

		sense_length = sci_ssp_get_sense_data_length(
		    response_buffer->sense_data_length);

		sense_length = MIN(csio->sense_len, sense_length);

		memcpy(&csio->sense_data, response_buffer->data, sense_length);

		csio->sense_resid = csio->sense_len - sense_length;
		csio->scsi_status = response_buffer->status;
		ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
		ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
		scsi_extract_sense( &csio->sense_data, &error_code, &sense_key,
		    &asc, &ascq );
		isci_log_message(1, "ISCI",
		    "isci: bus=%x target=%x lun=%x cdb[0]=%x status=%x key=%x asc=%x ascq=%x\n",
		    ccb->ccb_h.path_id, ccb->ccb_h.target_id,
		    ccb->ccb_h.target_lun, scsiio_cdb_ptr(csio),
		    csio->scsi_status, sense_key, asc, ascq);
		break;
	}

	case SCI_IO_FAILURE_REMOTE_DEVICE_RESET_REQUIRED:
		isci_remote_device_reset(isci_remote_device, NULL);
		ccb->ccb_h.status |= CAM_REQ_TERMIO;
		isci_log_message(0, "ISCI",
		    "isci: bus=%x target=%x lun=%x cdb[0]=%x remote device reset required\n",
		    ccb->ccb_h.path_id, ccb->ccb_h.target_id,
		    ccb->ccb_h.target_lun, scsiio_cdb_ptr(csio));
		break;

	case SCI_IO_FAILURE_TERMINATED:
		ccb->ccb_h.status |= CAM_REQ_TERMIO;
		isci_log_message(0, "ISCI",
		    "isci: bus=%x target=%x lun=%x cdb[0]=%x terminated\n",
		    ccb->ccb_h.path_id, ccb->ccb_h.target_id,
		    ccb->ccb_h.target_lun, scsiio_cdb_ptr(csio));
		break;

	case SCI_IO_FAILURE_INVALID_STATE:
	case SCI_IO_FAILURE_INSUFFICIENT_RESOURCES:
		complete_ccb = FALSE;
		break;

	case SCI_IO_FAILURE_INVALID_REMOTE_DEVICE:
		ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
		break;

	case SCI_IO_FAILURE_NO_NCQ_TAG_AVAILABLE:
		{
			struct ccb_relsim ccb_relsim;
			struct cam_path *path;

			xpt_create_path(&path, NULL,
			    cam_sim_path(isci_controller->sim),
			    isci_remote_device->index, 0);

			xpt_setup_ccb(&ccb_relsim.ccb_h, path, 5);
			ccb_relsim.ccb_h.func_code = XPT_REL_SIMQ;
			ccb_relsim.ccb_h.flags = CAM_DEV_QFREEZE;
			ccb_relsim.release_flags = RELSIM_ADJUST_OPENINGS;
			ccb_relsim.openings =
			    scif_remote_device_get_max_queue_depth(remote_device);
			xpt_action((union ccb *)&ccb_relsim);
			xpt_free_path(path);
			complete_ccb = FALSE;
		}
		break;

	case SCI_IO_FAILURE:
	case SCI_IO_FAILURE_REQUIRES_SCSI_ABORT:
	case SCI_IO_FAILURE_UNSUPPORTED_PROTOCOL:
	case SCI_IO_FAILURE_PROTOCOL_VIOLATION:
	case SCI_IO_FAILURE_INVALID_PARAMETER_VALUE:
	case SCI_IO_FAILURE_CONTROLLER_SPECIFIC_ERR:
	default:
		isci_log_message(1, "ISCI",
		    "isci: bus=%x target=%x lun=%x cdb[0]=%x completion status=%x\n",
		    ccb->ccb_h.path_id, ccb->ccb_h.target_id,
		    ccb->ccb_h.target_lun, scsiio_cdb_ptr(csio),
		    completion_status);
		ccb->ccb_h.status |= CAM_REQ_CMP_ERR;
		break;
	}

	callout_stop(&isci_request->parent.timer);
	bus_dmamap_sync(isci_request->parent.dma_tag,
	    isci_request->parent.dma_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	bus_dmamap_unload(isci_request->parent.dma_tag,
	    isci_request->parent.dma_map);

	isci_request->ccb = NULL;

	sci_pool_put(isci_controller->request_pool,
	    (struct ISCI_REQUEST *)isci_request);

	if (complete_ccb) {
		if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			/* ccb will be completed with some type of non-success
			 *  status.  So temporarily freeze the queue until the
			 *  upper layers can act on the status.  The
			 *  CAM_DEV_QFRZN flag will then release the queue
			 *  after the status is acted upon.
			 */
			ccb->ccb_h.status |= CAM_DEV_QFRZN;
			xpt_freeze_devq(ccb->ccb_h.path, 1);
		}

		if (ccb->ccb_h.status & CAM_SIM_QUEUED) {

			KASSERT(ccb == isci_remote_device->queued_ccb_in_progress,
			    ("multiple internally queued ccbs in flight"));

			TAILQ_REMOVE(&isci_remote_device->queued_ccbs,
			    &ccb->ccb_h, sim_links.tqe);
			ccb->ccb_h.status &= ~CAM_SIM_QUEUED;

			/*
			 * This CCB that was in the queue was completed, so
			 *  set the in_progress pointer to NULL denoting that
			 *  we can retry another CCB from the queue.  We only
			 *  allow one CCB at a time from the queue to be
			 *  in progress so that we can effectively maintain
			 *  ordering.
			 */
			isci_remote_device->queued_ccb_in_progress = NULL;
		}

		if (isci_remote_device->frozen_lun_mask != 0) {
			isci_remote_device_release_device_queue(isci_remote_device);
		}

		xpt_done(ccb);

		if (isci_controller->is_frozen == TRUE) {
			isci_controller->is_frozen = FALSE;
			xpt_release_simq(isci_controller->sim, TRUE);
		}
	} else {
		isci_remote_device_freeze_lun_queue(isci_remote_device,
		    ccb->ccb_h.target_lun);

		if (ccb->ccb_h.status & CAM_SIM_QUEUED) {

			KASSERT(ccb == isci_remote_device->queued_ccb_in_progress,
			    ("multiple internally queued ccbs in flight"));

			/*
			 *  Do nothing, CCB is already on the device's queue.
			 *   We leave it on the queue, to be retried again
			 *   next time a CCB on this device completes, or we
			 *   get a ready notification for this device.
			 */
			isci_log_message(1, "ISCI", "already queued %p %x\n",
			    ccb, scsiio_cdb_ptr(csio));

			isci_remote_device->queued_ccb_in_progress = NULL;

		} else {
			isci_log_message(1, "ISCI", "queue %p %x\n", ccb,
			    scsiio_cdb_ptr(csio));
			ccb->ccb_h.status |= CAM_SIM_QUEUED;

			TAILQ_INSERT_TAIL(&isci_remote_device->queued_ccbs,
			    &ccb->ccb_h, sim_links.tqe);
		}
	}
}

/**
 * @brief This callback method asks the user to provide the physical
 *        address for the supplied virtual address when building an
 *        io request object.
 *
 * @param[in] controller This parameter is the core controller object
 *            handle.
 * @param[in] io_request This parameter is the io request object handle
 *            for which the physical address is being requested.
 * @param[in] virtual_address This parameter is the virtual address which
 *            is to be returned as a physical address.
 * @param[out] physical_address The physical address for the supplied virtual
 *             address.
 *
 * @return None.
 */
void
scic_cb_io_request_get_physical_address(SCI_CONTROLLER_HANDLE_T	controller,
    SCI_IO_REQUEST_HANDLE_T io_request, void *virtual_address,
    SCI_PHYSICAL_ADDRESS *physical_address)
{
	SCI_IO_REQUEST_HANDLE_T scif_request =
	    sci_object_get_association(io_request);
	struct ISCI_REQUEST *isci_request =
	    sci_object_get_association(scif_request);

	if(isci_request != NULL) {
		/* isci_request is not NULL, meaning this is a request initiated
		 *  by CAM or the isci layer (i.e. device reset for I/O
		 *  timeout).  Therefore we can calculate the physical address
		 *  based on the address we stored in the struct ISCI_REQUEST
		 *  object.
		 */
		*physical_address = isci_request->physical_address +
		    (uintptr_t)virtual_address -
		    (uintptr_t)isci_request;
	} else {
		/* isci_request is NULL, meaning this is a request generated
		 *  internally by SCIL (i.e. for SMP requests or NCQ error
		 *  recovery).  Therefore we calculate the physical address
		 *  based on the controller's uncached controller memory buffer,
		 *  since we know that this is what SCIL uses for internal
		 *  framework requests.
		 */
		SCI_CONTROLLER_HANDLE_T scif_controller =
		    (SCI_CONTROLLER_HANDLE_T) sci_object_get_association(controller);
		struct ISCI_CONTROLLER *isci_controller =
		    (struct ISCI_CONTROLLER *)sci_object_get_association(scif_controller);
		U64 virt_addr_offset = (uintptr_t)virtual_address -
		    (U64)isci_controller->uncached_controller_memory.virtual_address;

		*physical_address =
		    isci_controller->uncached_controller_memory.physical_address
		    + virt_addr_offset;
	}
}

/**
 * @brief This callback method asks the user to provide the address for
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the virtual address of the CDB.
 */
void *
scif_cb_io_request_get_cdb_address(void * scif_user_io_request)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)scif_user_io_request;

	return (scsiio_cdb_ptr(&isci_request->ccb->csio));
}

/**
 * @brief This callback method asks the user to provide the length of
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the length of the CDB.
 */
uint32_t
scif_cb_io_request_get_cdb_length(void * scif_user_io_request)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)scif_user_io_request;

	return (isci_request->ccb->csio.cdb_len);
}

/**
 * @brief This callback method asks the user to provide the Logical Unit (LUN)
 *        associated with this IO request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport command information unit description
 *       in the associated standard.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the LUN associated with this request.
 */
uint32_t
scif_cb_io_request_get_lun(void * scif_user_io_request)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)scif_user_io_request;

	return (isci_request->ccb->ccb_h.target_lun);
}

/**
 * @brief This callback method asks the user to provide the task attribute
 *        associated with this IO request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport command information unit description
 *       in the associated standard.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the task attribute associated with this
 *         IO request.
 */
uint32_t
scif_cb_io_request_get_task_attribute(void * scif_user_io_request)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)scif_user_io_request;
	uint32_t task_attribute;

	if((isci_request->ccb->ccb_h.flags & CAM_TAG_ACTION_VALID) != 0)
		switch(isci_request->ccb->csio.tag_action) {
		case MSG_HEAD_OF_Q_TAG:
			task_attribute = SCI_SAS_HEAD_OF_QUEUE_ATTRIBUTE;
			break;

		case MSG_ORDERED_Q_TAG:
			task_attribute = SCI_SAS_ORDERED_ATTRIBUTE;
			break;

		case MSG_ACA_TASK:
			task_attribute = SCI_SAS_ACA_ATTRIBUTE;
			break;

		default:
			task_attribute = SCI_SAS_SIMPLE_ATTRIBUTE;
			break;
		}
	else
		task_attribute = SCI_SAS_SIMPLE_ATTRIBUTE;

	return (task_attribute);
}

/**
 * @brief This callback method asks the user to provide the command priority
 *        associated with this IO request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport command information unit description
 *       in the associated standard.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the command priority associated with this
 *         IO request.
 */
uint32_t
scif_cb_io_request_get_command_priority(void * scif_user_io_request)
{
	return (0);
}

/**
 * @brief This method simply returns the virtual address associated
 *        with the scsi_io and byte_offset supplied parameters.
 *
 * @note This callback is not utilized in the fast path.  The expectation
 *       is that this method is utilized for items such as SCSI to ATA
 *       translation for commands like INQUIRY, READ CAPACITY, etc.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] byte_offset This parameter specifies the offset into the data
 *            buffers pointed to by the SGL.  The byte offset starts at 0
 *            and continues until the last byte pointed to be the last SGL
 *            element.
 *
 * @return A virtual address pointer to the location specified by the
 *         parameters.
 */
uint8_t *
scif_cb_io_request_get_virtual_address_from_sgl(void * scif_user_io_request,
    uint32_t byte_offset)
{
	struct ISCI_IO_REQUEST	*isci_request;
	union ccb		*ccb;


	isci_request = scif_user_io_request;
	ccb = isci_request->ccb;

	/*
	 * This callback is only invoked for SCSI/ATA translation of
	 *  PIO commands such as INQUIRY and READ_CAPACITY, to allow
	 *  the driver to write the translated data directly into the
	 *  data buffer.  It is never invoked for READ/WRITE commands.
	 *  The driver currently assumes only READ/WRITE commands will
	 *  be unmapped.
	 *
	 * As a safeguard against future changes to unmapped commands,
	 *  add an explicit panic here should the DATA_MASK != VADDR.
	 *  Otherwise, we would return some garbage pointer back to the
	 *  caller which would result in a panic or more subtle data
	 *  corruption later on.
	 */
	if ((ccb->ccb_h.flags & CAM_DATA_MASK) != CAM_DATA_VADDR)
		panic("%s: requesting pointer into unmapped ccb", __func__);

	return (ccb->csio.data_ptr + byte_offset);
}

/**
 * @brief This callback method asks the user to provide the number of
 *        bytes to be transferred as part of this request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the number of payload data bytes to be
 *         transferred for this IO request.
 */
uint32_t
scif_cb_io_request_get_transfer_length(void * scif_user_io_request)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)scif_user_io_request;

	return (isci_request->ccb->csio.dxfer_len);

}

/**
 * @brief This callback method asks the user to provide the data direction
 *        for this request.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the value of SCI_IO_REQUEST_DATA_OUT,
 *         SCI_IO_REQUEST_DATA_IN, or SCI_IO_REQUEST_NO_DATA.
 */
SCI_IO_REQUEST_DATA_DIRECTION
scif_cb_io_request_get_data_direction(void * scif_user_io_request)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)scif_user_io_request;

	switch (isci_request->ccb->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		return (SCI_IO_REQUEST_DATA_IN);
	case CAM_DIR_OUT:
		return (SCI_IO_REQUEST_DATA_OUT);
	default:
		return (SCI_IO_REQUEST_NO_DATA);
	}
}

/**
 * @brief This callback method asks the user to provide the address
 *        to where the next Scatter-Gather Element is located.
 *
 * Details regarding usage:
 *   - Regarding the first SGE: the user should initialize an index,
 *     or a pointer, prior to construction of the request that will
 *     reference the very first scatter-gather element.  This is
 *     important since this method is called for every scatter-gather
 *     element, including the first element.
 *   - Regarding the last SGE: the user should return NULL from this
 *     method when this method is called and the SGL has exhausted
 *     all elements.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] current_sge_address This parameter specifies the address for
 *            the current SGE (i.e. the one that has just processed).
 * @param[out] next_sge An address specifying the location for the next scatter
 *             gather element to be processed.
 *
 * @return None.
 */
void
scif_cb_io_request_get_next_sge(void * scif_user_io_request,
    void * current_sge_address, void ** next_sge)
{
	struct ISCI_IO_REQUEST *isci_request =
	    (struct ISCI_IO_REQUEST *)scif_user_io_request;

	if (isci_request->current_sge_index == isci_request->num_segments)
		*next_sge = NULL;
	else {
		bus_dma_segment_t *sge =
		    &isci_request->sge[isci_request->current_sge_index];

		isci_request->current_sge_index++;
		*next_sge = sge;
	}
}

/**
 * @brief This callback method asks the user to provide the contents of the
 *        "address" field in the Scatter-Gather Element.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] sge_address This parameter specifies the address for the
 *            SGE from which to retrieve the address field.
 *
 * @return A physical address specifying the contents of the SGE's address
 *         field.
 */
SCI_PHYSICAL_ADDRESS
scif_cb_sge_get_address_field(void *scif_user_io_request, void *sge_address)
{
	bus_dma_segment_t *sge = (bus_dma_segment_t *)sge_address;

	return ((SCI_PHYSICAL_ADDRESS)sge->ds_addr);
}

/**
 * @brief This callback method asks the user to provide the contents of the
 *        "length" field in the Scatter-Gather Element.
 *
 * @param[in] scif_user_io_request This parameter points to the user's
 *            IO request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 * @param[in] sge_address This parameter specifies the address for the
 *            SGE from which to retrieve the address field.
 *
 * @return This method returns the length field specified inside the SGE
 *         referenced by the sge_address parameter.
 */
uint32_t
scif_cb_sge_get_length_field(void *scif_user_io_request, void *sge_address)
{
	bus_dma_segment_t *sge = (bus_dma_segment_t *)sge_address;

	return ((uint32_t)sge->ds_len);
}

void
isci_request_construct(struct ISCI_REQUEST *request,
    SCI_CONTROLLER_HANDLE_T scif_controller_handle,
    bus_dma_tag_t io_buffer_dma_tag, bus_addr_t physical_address)
{

	request->controller_handle = scif_controller_handle;
	request->dma_tag = io_buffer_dma_tag;
	request->physical_address = physical_address;
	bus_dmamap_create(request->dma_tag, 0, &request->dma_map);
	callout_init(&request->timer, 1);
}

static void
isci_io_request_construct(void *arg, bus_dma_segment_t *seg, int nseg,
    int error)
{
	union ccb *ccb;
	struct ISCI_IO_REQUEST *io_request = (struct ISCI_IO_REQUEST *)arg;
	SCI_REMOTE_DEVICE_HANDLE_T *device = io_request->parent.remote_device_handle;
	SCI_STATUS status;

	io_request->num_segments = nseg;
	io_request->sge = seg;
	ccb = io_request->ccb;

	if (error != 0) {
		ccb->ccb_h.status = CAM_REQ_INVALID;
		xpt_done(ccb);
		return;
	}

	status = scif_io_request_construct(
	    io_request->parent.controller_handle,
	    io_request->parent.remote_device_handle,
	    SCI_CONTROLLER_INVALID_IO_TAG, (void *)io_request,
	    (void *)((char*)io_request + sizeof(struct ISCI_IO_REQUEST)),
	    &io_request->sci_object);

	if (status != SCI_SUCCESS) {
		isci_io_request_complete(io_request->parent.controller_handle,
		    device, io_request, (SCI_IO_STATUS)status);
		return;
	}

	sci_object_set_association(io_request->sci_object, io_request);

	bus_dmamap_sync(io_request->parent.dma_tag, io_request->parent.dma_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	status = (SCI_STATUS)scif_controller_start_io(
	    io_request->parent.controller_handle, device,
	    io_request->sci_object, SCI_CONTROLLER_INVALID_IO_TAG);

	if (status != SCI_SUCCESS) {
		isci_io_request_complete(io_request->parent.controller_handle,
		    device, io_request, (SCI_IO_STATUS)status);
		return;
	}

	if (ccb->ccb_h.timeout != CAM_TIME_INFINITY)
		callout_reset_sbt(&io_request->parent.timer,
		    SBT_1MS * ccb->ccb_h.timeout, 0, isci_io_request_timeout,
		    io_request, 0);
}

void
isci_io_request_execute_scsi_io(union ccb *ccb,
    struct ISCI_CONTROLLER *controller)
{
	target_id_t target_id = ccb->ccb_h.target_id;
	struct ISCI_REQUEST *request;
	struct ISCI_IO_REQUEST *io_request;
	struct ISCI_REMOTE_DEVICE *device =
	    controller->remote_device[target_id];
	int error;

	if (device == NULL) {
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
		xpt_done(ccb);
		return;
	}

	if (sci_pool_empty(controller->request_pool)) {
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		xpt_freeze_simq(controller->sim, 1);
		controller->is_frozen = TRUE;
		xpt_done(ccb);
		return;
	}

	ASSERT(device->is_resetting == FALSE);

	sci_pool_get(controller->request_pool, request);
	io_request = (struct ISCI_IO_REQUEST *)request;

	io_request->ccb = ccb;
	io_request->current_sge_index = 0;
	io_request->parent.remote_device_handle = device->sci_object;

	error = bus_dmamap_load_ccb(io_request->parent.dma_tag,
	    io_request->parent.dma_map, ccb,
	    isci_io_request_construct, io_request, 0x0);
	/* A resource shortage from BUSDMA will be automatically
	 * continued at a later point, pushing the CCB processing
	 * forward, which will in turn unfreeze the simq.
	 */
	if (error == EINPROGRESS) {
		xpt_freeze_simq(controller->sim, 1);
		ccb->ccb_h.flags |= CAM_RELEASE_SIMQ;
	}
}

void
isci_io_request_timeout(void *arg)
{
	struct ISCI_IO_REQUEST *request = (struct ISCI_IO_REQUEST *)arg;
	struct ISCI_REMOTE_DEVICE *remote_device = (struct ISCI_REMOTE_DEVICE *)
		sci_object_get_association(request->parent.remote_device_handle);
	struct ISCI_CONTROLLER *controller = remote_device->domain->controller;

	mtx_lock(&controller->lock);
	isci_remote_device_reset(remote_device, NULL);
	mtx_unlock(&controller->lock);
}

#if __FreeBSD_version >= 900026
/**
 * @brief This callback method gets the size of and pointer to the buffer
 *         (if any) containing the request buffer for an SMP request.
 *
 * @param[in]  core_request This parameter specifies the SCI core's request
 *             object associated with the SMP request.
 * @param[out] smp_request_buffer This parameter returns a pointer to the
 *             payload portion of the SMP request - i.e. everything after
 *             the SMP request header.
 *
 * @return Size of the request buffer in bytes.  This does *not* include
 *          the size of the SMP request header.
 */
static uint32_t
smp_io_request_cb_get_request_buffer(SCI_IO_REQUEST_HANDLE_T core_request,
    uint8_t ** smp_request_buffer)
{
	struct ISCI_IO_REQUEST *isci_request = (struct ISCI_IO_REQUEST *)
	    sci_object_get_association(sci_object_get_association(core_request));

	*smp_request_buffer = isci_request->ccb->smpio.smp_request +
	    sizeof(SMP_REQUEST_HEADER_T);

	return (isci_request->ccb->smpio.smp_request_len -
	    sizeof(SMP_REQUEST_HEADER_T));
}

/**
 * @brief This callback method gets the SMP function for an SMP request.
 *
 * @param[in]  core_request This parameter specifies the SCI core's request
 *             object associated with the SMP request.
 *
 * @return SMP function for the SMP request.
 */
static uint8_t
smp_io_request_cb_get_function(SCI_IO_REQUEST_HANDLE_T core_request)
{
	struct ISCI_IO_REQUEST *isci_request = (struct ISCI_IO_REQUEST *)
	    sci_object_get_association(sci_object_get_association(core_request));
	SMP_REQUEST_HEADER_T *header =
	    (SMP_REQUEST_HEADER_T *)isci_request->ccb->smpio.smp_request;

	return (header->function);
}

/**
 * @brief This callback method gets the SMP frame type for an SMP request.
 *
 * @param[in]  core_request This parameter specifies the SCI core's request
 *             object associated with the SMP request.
 *
 * @return SMP frame type for the SMP request.
 */
static uint8_t
smp_io_request_cb_get_frame_type(SCI_IO_REQUEST_HANDLE_T core_request)
{
	struct ISCI_IO_REQUEST *isci_request = (struct ISCI_IO_REQUEST *)
	    sci_object_get_association(sci_object_get_association(core_request));
	SMP_REQUEST_HEADER_T *header =
	    (SMP_REQUEST_HEADER_T *)isci_request->ccb->smpio.smp_request;

	return (header->smp_frame_type);
}

/**
 * @brief This callback method gets the allocated response length for an SMP request.
 *
 * @param[in]  core_request This parameter specifies the SCI core's request
 *             object associated with the SMP request.
 *
 * @return Allocated response length for the SMP request.
 */
static uint8_t
smp_io_request_cb_get_allocated_response_length(
    SCI_IO_REQUEST_HANDLE_T core_request)
{
	struct ISCI_IO_REQUEST *isci_request = (struct ISCI_IO_REQUEST *)
	    sci_object_get_association(sci_object_get_association(core_request));
	SMP_REQUEST_HEADER_T *header =
	    (SMP_REQUEST_HEADER_T *)isci_request->ccb->smpio.smp_request;

	return (header->allocated_response_length);
}

static SCI_STATUS
isci_smp_request_construct(struct ISCI_IO_REQUEST *request)
{
	SCI_STATUS status;
	SCIC_SMP_PASSTHRU_REQUEST_CALLBACKS_T callbacks;

	status = scif_request_construct(request->parent.controller_handle,
	    request->parent.remote_device_handle, SCI_CONTROLLER_INVALID_IO_TAG,
	    (void *)request,
	    (void *)((char*)request + sizeof(struct ISCI_IO_REQUEST)),
	    &request->sci_object);

	if (status == SCI_SUCCESS) {
		callbacks.scic_cb_smp_passthru_get_request =
		    &smp_io_request_cb_get_request_buffer;
		callbacks.scic_cb_smp_passthru_get_function =
		    &smp_io_request_cb_get_function;
		callbacks.scic_cb_smp_passthru_get_frame_type =
		    &smp_io_request_cb_get_frame_type;
		callbacks.scic_cb_smp_passthru_get_allocated_response_length =
		    &smp_io_request_cb_get_allocated_response_length;

		/* create the smp passthrough part of the io request */
		status = scic_io_request_construct_smp_pass_through(
		    scif_io_request_get_scic_handle(request->sci_object),
		    &callbacks);
	}

	return (status);
}

void
isci_io_request_execute_smp_io(union ccb *ccb,
    struct ISCI_CONTROLLER *controller)
{
	SCI_STATUS status;
	target_id_t target_id = ccb->ccb_h.target_id;
	struct ISCI_REQUEST *request;
	struct ISCI_IO_REQUEST *io_request;
	SCI_REMOTE_DEVICE_HANDLE_T smp_device_handle;
	struct ISCI_REMOTE_DEVICE *end_device = controller->remote_device[target_id];

	/* SMP commands are sent to an end device, because SMP devices are not
	 *  exposed to the kernel.  It is our responsibility to use this method
	 *  to get the SMP device that contains the specified end device.  If
	 *  the device is direct-attached, the handle will come back NULL, and
	 *  we'll just fail the SMP_IO with DEV_NOT_THERE.
	 */
	scif_remote_device_get_containing_device(end_device->sci_object,
	    &smp_device_handle);

	if (smp_device_handle == NULL) {
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
		xpt_done(ccb);
		return;
	}

	if (sci_pool_empty(controller->request_pool)) {
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_REQUEUE_REQ;
		xpt_freeze_simq(controller->sim, 1);
		controller->is_frozen = TRUE;
		xpt_done(ccb);
		return;
	}

	ASSERT(device->is_resetting == FALSE);

	sci_pool_get(controller->request_pool, request);
	io_request = (struct ISCI_IO_REQUEST *)request;

	io_request->ccb = ccb;
	io_request->parent.remote_device_handle = smp_device_handle;

	status = isci_smp_request_construct(io_request);

	if (status != SCI_SUCCESS) {
		isci_io_request_complete(controller->scif_controller_handle,
		    smp_device_handle, io_request, (SCI_IO_STATUS)status);
		return;
	}

	sci_object_set_association(io_request->sci_object, io_request);

	status = (SCI_STATUS) scif_controller_start_io(
	    controller->scif_controller_handle, smp_device_handle,
	    io_request->sci_object, SCI_CONTROLLER_INVALID_IO_TAG);

	if (status != SCI_SUCCESS) {
		isci_io_request_complete(controller->scif_controller_handle,
		    smp_device_handle, io_request, (SCI_IO_STATUS)status);
		return;
	}

	if (ccb->ccb_h.timeout != CAM_TIME_INFINITY)
		callout_reset_sbt(&io_request->parent.timer,
		    SBT_1MS *  ccb->ccb_h.timeout, 0, isci_io_request_timeout,
		    request, 0);
}
#endif
