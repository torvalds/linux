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

#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>

#include <dev/isci/scil/scif_task_request.h>
#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scif_domain.h>
#include <dev/isci/scil/scif_user_callback.h>

#include <dev/isci/scil/scic_port.h>
#include <dev/isci/scil/scic_phy.h>

/**
 * @brief This callback method informs the framework user that the remote
 *        device is ready and capable of processing IO requests.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  remote_device This parameter specifies the device object with
 *             which this callback is associated.
 *
 * @return none
 */
void
scif_cb_remote_device_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device)
{
	struct ISCI_REMOTE_DEVICE *isci_remote_device =
	    sci_object_get_association(remote_device);
	struct ISCI_CONTROLLER *isci_controller =
	    sci_object_get_association(controller);
	uint32_t device_index = isci_remote_device->index;

	if (isci_controller->remote_device[device_index] == NULL) {
		/* This new device is now ready, so put it in the controller's
		 *  remote device list so it is visible to CAM.
		 */
		isci_controller->remote_device[device_index] =
		    isci_remote_device;

		if (isci_controller->has_been_scanned) {
			/* The sim object has been scanned at least once
			 *  already.  In that case, create a CCB to instruct
			 *  CAM to rescan this device.
			 * If the sim object has not been scanned, this device
			 *  will get scanned as part of the initial scan.
			 */
			union ccb *ccb = xpt_alloc_ccb_nowait();

			xpt_create_path(&ccb->ccb_h.path, NULL,
			    cam_sim_path(isci_controller->sim),
			    isci_remote_device->index, CAM_LUN_WILDCARD);

			xpt_rescan(ccb);
		}
	}

	isci_remote_device_release_device_queue(isci_remote_device);
}

/**
 * @brief This callback method informs the framework user that the remote
 *              device is not ready.  Thus, it is incapable of processing IO
 *              requests.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  remote_device This parameter specifies the device object with
 *             which this callback is associated.
 *
 * @return none
 */
void
scif_cb_remote_device_not_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device)
{

}

/**
 * @brief This callback method informs the framework user that the remote
 *        device failed.  This typically occurs shortly after the device
 *        has been discovered, during the configuration phase for the device.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  remote_device This parameter specifies the device object with
 *             which this callback is associated.
 * @param[in]  status This parameter specifies the specific failure condition
 *             associated with this device failure.
 *
 * @return none
 */
void
scif_cb_remote_device_failed(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_STATUS status)
{

}

void
isci_remote_device_reset(struct ISCI_REMOTE_DEVICE *remote_device,
    union ccb *ccb)
{
	struct ISCI_CONTROLLER *controller = remote_device->domain->controller;
	struct ISCI_REQUEST *request;
	struct ISCI_TASK_REQUEST *task_request;
	SCI_STATUS status;

	if (remote_device->is_resetting == TRUE) {
		/* device is already being reset, so return immediately */
		return;
	}

	if (sci_pool_empty(controller->request_pool)) {
		/* No requests are available in our request pool.  If this reset is tied
		 *  to a CCB, ask CAM to requeue it.  Otherwise, we need to put it on our
		 *  pending device reset list, so that the reset will occur when a request
		 *  frees up.
		 */
		if (ccb == NULL)
			sci_fast_list_insert_tail(
			    &controller->pending_device_reset_list,
			    &remote_device->pending_device_reset_element);
		else {
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_REQUEUE_REQ;
			xpt_done(ccb);
		}
		return;
	}

	isci_log_message(0, "ISCI",
	    "Sending reset to device on controller %d domain %d CAM index %d\n",
	    controller->index, remote_device->domain->index,
	    remote_device->index
	);

	sci_pool_get(controller->request_pool, request);
	task_request = (struct ISCI_TASK_REQUEST *)request;

	task_request->parent.remote_device_handle = remote_device->sci_object;
	task_request->ccb = ccb;

	remote_device->is_resetting = TRUE;

	status = (SCI_STATUS) scif_task_request_construct(
	    controller->scif_controller_handle, remote_device->sci_object,
	    SCI_CONTROLLER_INVALID_IO_TAG, (void *)task_request,
	    (void *)((char*)task_request + sizeof(struct ISCI_TASK_REQUEST)),
	    &task_request->sci_object);

	if (status != SCI_SUCCESS) {
		isci_task_request_complete(controller->scif_controller_handle,
		    remote_device->sci_object, task_request->sci_object,
		    (SCI_TASK_STATUS)status);
		return;
	}

	status = (SCI_STATUS)scif_controller_start_task(
	    controller->scif_controller_handle, remote_device->sci_object,
	    task_request->sci_object, SCI_CONTROLLER_INVALID_IO_TAG);

	if (status != SCI_SUCCESS) {
		isci_task_request_complete(
		    controller->scif_controller_handle,
		    remote_device->sci_object, task_request->sci_object,
		    (SCI_TASK_STATUS)status);
		return;
	}
}

uint32_t
isci_remote_device_get_bitrate(struct ISCI_REMOTE_DEVICE *remote_device)
{
	struct ISCI_DOMAIN *domain = remote_device->domain;
	struct ISCI_CONTROLLER *controller = domain->controller;
	SCI_PORT_HANDLE_T port_handle;
	SCIC_PORT_PROPERTIES_T port_properties;
	uint8_t phy_index;
	SCI_PHY_HANDLE_T phy_handle;
	SCIC_PHY_PROPERTIES_T phy_properties;

	/* get a handle to the port associated with this remote device's
	 *  domain
	 */
	port_handle = scif_domain_get_scic_port_handle(domain->sci_object);
	scic_port_get_properties(port_handle, &port_properties);

	/* get the lowest numbered phy in the port */
	phy_index = 0;
	while ((port_properties.phy_mask != 0) &&
	    !(port_properties.phy_mask & 0x1)) {

		phy_index++;
		port_properties.phy_mask >>= 1;
	}

	/* get the properties for the lowest numbered phy */
	scic_controller_get_phy_handle(
	    scif_controller_get_scic_handle(controller->scif_controller_handle),
	    phy_index, &phy_handle);
	scic_phy_get_properties(phy_handle, &phy_properties);

	switch (phy_properties.negotiated_link_rate) {
	case SCI_SAS_150_GB:
		return (150000);
	case SCI_SAS_300_GB:
		return (300000);
	case SCI_SAS_600_GB:
		return (600000);
	default:
		return (0);
	}
}

void
isci_remote_device_freeze_lun_queue(struct ISCI_REMOTE_DEVICE *remote_device,
    lun_id_t lun)
{
	if (!(remote_device->frozen_lun_mask & (1 << lun))) {
		struct cam_path *path;

		xpt_create_path(&path, NULL,
		    cam_sim_path(remote_device->domain->controller->sim),
		    remote_device->index, lun);
		xpt_freeze_devq(path, 1);
		xpt_free_path(path);
		remote_device->frozen_lun_mask |= (1 << lun);
	}
}

void
isci_remote_device_release_lun_queue(struct ISCI_REMOTE_DEVICE *remote_device,
    lun_id_t lun)
{
	if (remote_device->frozen_lun_mask & (1 << lun)) {
		struct cam_path *path;

		remote_device->frozen_lun_mask &= ~(1 << lun);
		xpt_create_path(&path, NULL,
		    cam_sim_path(remote_device->domain->controller->sim),
		    remote_device->index, lun);
		xpt_release_devq(path, 1, TRUE);
		xpt_free_path(path);
	}
}

void
isci_remote_device_release_device_queue(
    struct ISCI_REMOTE_DEVICE *device)
{
	if (TAILQ_EMPTY(&device->queued_ccbs)) {
		lun_id_t lun;

		for (lun = 0; lun < ISCI_MAX_LUN; lun++)
			isci_remote_device_release_lun_queue(device, lun);
	} else {
		/*
		 * We cannot unfreeze the devq, because there are still
		 *  CCBs in our internal queue that need to be processed
		 *  first.  Mark this device, and the controller, so that
		 *  the first CCB in this device's internal queue will be
		 *  resubmitted after the current completion context
		 *  unwinds.
		 */
		device->release_queued_ccb = TRUE;
		device->domain->controller->release_queued_ccbs = TRUE;

		isci_log_message(1, "ISCI", "schedule %p for release\n",
		    device);
	}
}
