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

#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scif_user_callback.h>

/**
 * @brief This user callback will inform the user that a task management
 *        request completed.
 *
 * @param[in]  controller This parameter specifies the controller on
 *             which the task management request is completing.
 * @param[in]  remote_device This parameter specifies the remote device on
 *             which this task management request is completing.
 * @param[in]  task_request This parameter specifies the task management
 *             request that has completed.
 * @param[in]  completion_status This parameter specifies the results of
 *             the IO request operation.  SCI_TASK_SUCCESS indicates
 *             successful completion.
 *
 * @return none
 */
void
scif_cb_task_request_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_TASK_REQUEST_HANDLE_T task_request, SCI_TASK_STATUS completion_status)
{

	scif_controller_complete_task(controller, remote_device, task_request);
	isci_task_request_complete(controller, remote_device, task_request,
	    completion_status);
}

/**
 * @brief This method returns the Logical Unit to be utilized for this
 *        task management request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport task information unit description
 *       in the associated standard.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the LUN associated with this request.
 * @todo This should be U64?
 */
uint32_t
scif_cb_task_request_get_lun(void * scif_user_task_request)
{

	/* Currently we are only doing hard resets, not LUN resets.  So
	 *  always returning 0 is OK here, since LUN doesn't matter for
	 *  a hard device reset.
	 */
	return (0);
}

/**
 * @brief This method returns the task management function to be utilized
 *        for this task request.
 *
 * @note The contents of the value returned from this callback are defined
 *       by the protocol standard (e.g. T10 SAS specification).  Please
 *       refer to the transport task information unit description
 *       in the associated standard.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns an unsigned byte representing the task
 *         management function to be performed.
 */
uint8_t scif_cb_task_request_get_function(void * scif_user_task_request)
{
	/* SCIL supports many types of task management functions, but this
	 *  driver only uses HARD_RESET.
	 */
	return (SCI_SAS_HARD_RESET);
}

/**
 * @brief This method returns the task management IO tag to be managed.
 *        Depending upon the task management function the value returned
 *        from this method may be ignored.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns an unsigned 16-bit word depicting the IO
 *         tag to be managed.
 */
uint16_t
scif_cb_task_request_get_io_tag_to_manage(void * scif_user_task_request)
{

	return (0);
}

/**
 * @brief This callback method asks the user to provide the virtual
 *        address of the response data buffer for the supplied IO request.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the virtual address for the response data buffer
 *         associated with this IO request.
 */
void *
scif_cb_task_request_get_response_data_address(void * scif_user_task_request)
{
	struct ISCI_TASK_REQUEST *task_request =
	    (struct ISCI_TASK_REQUEST *)scif_user_task_request;

	return (&task_request->sense_data);
}

/**
 * @brief This callback method asks the user to provide the length of the
 *        response data buffer for the supplied IO request.
 *
 * @param[in] scif_user_task_request This parameter points to the user's
 *            task request object.  It is a cookie that allows the user to
 *            provide the necessary information for this callback.
 *
 * @return This method returns the length of the response buffer data
 *         associated with this IO request.
 */
uint32_t
scif_cb_task_request_get_response_data_length(void * scif_user_task_request)
{

	return (sizeof(struct scsi_sense_data));
}

void
isci_task_request_complete(SCI_CONTROLLER_HANDLE_T scif_controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_TASK_REQUEST_HANDLE_T task_request, SCI_TASK_STATUS completion_status)
{
	struct ISCI_TASK_REQUEST *isci_task_request =
		(struct ISCI_TASK_REQUEST *)sci_object_get_association(task_request);
	struct ISCI_CONTROLLER *isci_controller =
		(struct ISCI_CONTROLLER *)sci_object_get_association(scif_controller);
	struct ISCI_REMOTE_DEVICE *isci_remote_device =
		(struct ISCI_REMOTE_DEVICE *)sci_object_get_association(remote_device);
	struct ISCI_REMOTE_DEVICE *pending_remote_device;
	BOOL retry_task = FALSE;
	union ccb *ccb = isci_task_request->ccb;

	isci_remote_device->is_resetting = FALSE;

	switch ((int)completion_status) {
	case SCI_TASK_SUCCESS:
	case SCI_TASK_FAILURE_RESPONSE_VALID:
		break;

	case SCI_TASK_FAILURE_INVALID_STATE:
		retry_task = TRUE;
		isci_log_message(0, "ISCI",
		    "task failure (invalid state) - retrying\n");
		break;

	case SCI_TASK_FAILURE_INSUFFICIENT_RESOURCES:
		retry_task = TRUE;
		isci_log_message(0, "ISCI",
		    "task failure (insufficient resources) - retrying\n");
		break;

	case SCI_FAILURE_TIMEOUT:
		if (isci_controller->fail_on_task_timeout) {
			retry_task = FALSE;
			isci_log_message(0, "ISCI",
			    "task timeout - not retrying\n");
			scif_cb_domain_device_removed(scif_controller,
			    isci_remote_device->domain->sci_object,
			    remote_device);
		} else {
			retry_task = TRUE;
			isci_log_message(0, "ISCI",
			    "task timeout - retrying\n");
		}
		break;

	case SCI_TASK_FAILURE:
	case SCI_TASK_FAILURE_UNSUPPORTED_PROTOCOL:
	case SCI_TASK_FAILURE_INVALID_TAG:
	case SCI_TASK_FAILURE_CONTROLLER_SPECIFIC_ERR:
	case SCI_TASK_FAILURE_TERMINATED:
	case SCI_TASK_FAILURE_INVALID_PARAMETER_VALUE:
		isci_log_message(0, "ISCI",
		    "unhandled task completion code 0x%x\n", completion_status);
		break;

	default:
		isci_log_message(0, "ISCI",
		    "unhandled task completion code 0x%x\n", completion_status);
		break;
	}

	if (isci_controller->is_frozen == TRUE) {
		isci_controller->is_frozen = FALSE;
		xpt_release_simq(isci_controller->sim, TRUE);
	}

	sci_pool_put(isci_controller->request_pool,
	    (struct ISCI_REQUEST *)isci_task_request);

	/* Make sure we release the device queue, since it may have been frozen
	 *  if someone tried to start an I/O while the task was in progress.
	 */
	isci_remote_device_release_device_queue(isci_remote_device);

	if (retry_task == TRUE)
		isci_remote_device_reset(isci_remote_device, ccb);
	else {
		pending_remote_device = sci_fast_list_remove_head(
		    &isci_controller->pending_device_reset_list);

		if (pending_remote_device != NULL) {
			/* Any resets that were triggered from an XPT_RESET_DEV
			 *  CCB are never put in the pending list if the request
			 *  pool is empty - they are given back to CAM to be
			 *  requeued.  So we will alawys pass NULL here,
			 *  denoting that there is no CCB associated with the
			 *  device reset.
			 */
			isci_remote_device_reset(pending_remote_device, NULL);
		} else if (ccb != NULL) {
			/* There was a CCB associated with this reset, so mark
			 *  it complete and return it to CAM.
			 */
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_REQ_CMP;
			xpt_done(ccb);
		}
	}
}

