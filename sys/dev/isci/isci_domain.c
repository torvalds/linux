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

#include <dev/isci/scil/scif_domain.h>
#include <dev/isci/scil/scif_remote_device.h>
#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scif_user_callback.h>

/**
 * @brief This callback method informs the framework user that something
 *        in the supplied domain has changed (e.g. a device was added or
 *        removed).
 *
 * This callback is called by the framework outside of discovery or
 * target reset processes.  Specifically, domain changes occurring
 * during these processes are handled by the framework.  For example,
 * in the case of Serial Attached SCSI, reception of a BROADCAST (CHANGE)
 * during discovery will cause discovery to restart.  Thus, discovery
 * does not complete until all BCNs are processed. Note, during controller
 * stopping/reset process, the framework user should not expect this call
 * back.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 *
 * @return none
 */
void
scif_cb_domain_change_notification(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain)
{
	struct ISCI_CONTROLLER *isci_controller =
	    (struct ISCI_CONTROLLER *)sci_object_get_association(controller);

	/* When the controller start is complete, we will explicitly discover
	 *  all of the domains then.  This is because SCIF will not allow
	 *  any I/O to start until the controller is ready, meaning internal SMP
	 *  requests triggered by domain discovery won't work until the controller
	 *  is ready.
	 */
	if (isci_controller->is_started == TRUE)
	    scif_domain_discover(domain,
	        scif_domain_get_suggested_discover_timeout(domain),
	        DEVICE_TIMEOUT);
}

/**
 * @brief This callback method informs the framework user that a previously
 *        requested discovery operation on the domain has completed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  completion_status This parameter indicates the results of the
 *             discovery operation.
 *
 * @return none
 */
void
scif_cb_domain_discovery_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_STATUS completion_status)
{

	if(completion_status != SCI_SUCCESS)
		isci_log_message(0, "ISCI",
		    "scif_cb_domain_discovery_complete status = 0x%x\n",
		    completion_status);

	isci_controller_domain_discovery_complete(
	    (struct ISCI_CONTROLLER *)sci_object_get_association(controller),
	    (struct ISCI_DOMAIN *) sci_object_get_association(domain));
}

/**
 * @brief This callback method informs the framework user that a previously
 *        requested reset operation on the domain has completed.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  completion_status This parameter indicates the results of the
 *             reset operation.
 *
 * @return none
 */
void
scif_cb_domain_reset_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_STATUS completion_status)
{

}

/**
 * @brief This callback method informs the framework user that the domain
 *        is ready and capable of processing IO requests for devices found
 *        inside it.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 *
 * @return none
 */
void
scif_cb_domain_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain)
{
	uint32_t i;
	struct ISCI_DOMAIN *isci_domain = sci_object_get_association(domain);
	struct ISCI_CONTROLLER *isci_controller =
	    sci_object_get_association(controller);

	for (i = 0; i < SCI_MAX_REMOTE_DEVICES; i++) {
		struct ISCI_REMOTE_DEVICE *remote_device =
		    isci_controller->remote_device[i];

		if (remote_device != NULL &&
		    remote_device->domain == isci_domain)
			isci_remote_device_release_device_queue(remote_device);
	}
}

/**
 * @brief This callback method informs the framework user that the domain
 *        is no longer ready. Thus, it is incapable of processing IO
 *        requests for devices found inside it.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 *
 * @return none
 */
void
scif_cb_domain_not_ready(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain)
{

}

/**
 * @brief This callback method informs the framework user that a new
 *        direct attached device was found in the domain.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  sas_address This parameter specifies the SAS address of
 *             the new device.
 * @param[in]  protocols This parameter specifies the protocols
 *             supported by the newly discovered device.
 *
 * @return none
 */
void
scif_cb_domain_da_device_added(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_SAS_ADDRESS_T *sas_address,
    SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T *protocols)
{
	struct ISCI_REMOTE_DEVICE *remote_device;
	struct ISCI_DOMAIN *isci_domain =
	    (struct ISCI_DOMAIN *)sci_object_get_association(domain);

	/*
	 * For direct-attached devices, do not pull the device object from
	 *  the pool.  Rather, use the one stored in the domain object which
	 *  will ensure that we always get consistent target ids for direct
	 *  attached devices.
	 */
	remote_device = isci_domain->da_remote_device;

	scif_remote_device_construct(domain,
	    (uint8_t*)remote_device + sizeof(struct ISCI_REMOTE_DEVICE),
	    &(remote_device->sci_object));

	sci_object_set_association(remote_device->sci_object, remote_device);

	scif_remote_device_da_construct(remote_device->sci_object, sas_address,
	    protocols);

	/* We do not put the device in the ISCI_CONTROLLER's device array yet.
	 *  That will happen once the device becomes ready (see
	 *  scif_cb_remote_device_ready).
	 */

	remote_device->domain = isci_domain;
}

/**
 * @brief This callback method informs the framework user that a new
 *        expander attached device was found in the domain.
 *
 * @param[in]  controller This parameter specifies the controller object
 *             with which this callback is associated.
 * @param[in]  domain This parameter specifies the domain object with
 *             which this callback is associated.
 * @param[in]  containing_device This parameter specifies the remote
 *             device that contains the device that was added.
 * @param[in]  smp_response This parameter specifies the SMP response
 *             data associated with the newly discovered device.
 *
 * @return none
 */
void
scif_cb_domain_ea_device_added(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T containing_device,
    SMP_RESPONSE_DISCOVER_T *smp_response)
{
	struct ISCI_REMOTE_DEVICE *remote_device;
	struct ISCI_DOMAIN *isci_domain =
		(struct ISCI_DOMAIN *)sci_object_get_association(domain);
	struct ISCI_CONTROLLER *isci_controller =
		(struct ISCI_CONTROLLER *)sci_object_get_association(controller);

	sci_pool_get(isci_controller->remote_device_pool, remote_device);

	scif_remote_device_construct( domain,
	    (uint8_t*)remote_device + sizeof(struct ISCI_REMOTE_DEVICE),
	    &(remote_device->sci_object));

	sci_object_set_association(remote_device->sci_object, remote_device);

	scif_remote_device_ea_construct(remote_device->sci_object,
	    containing_device, smp_response);

	/* We do not put the device in the ISCI_CONTROLLER's device array yet.
	 *  That will happen once the device becomes ready (see
	 *  scif_cb_remote_device_ready).
	 */
	remote_device->domain = isci_domain;
}

/**
 * @brief This callback method informs the framework user that a device
 *        has been removed from the domain.
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
scif_cb_domain_device_removed(SCI_CONTROLLER_HANDLE_T controller,
    SCI_DOMAIN_HANDLE_T domain, SCI_REMOTE_DEVICE_HANDLE_T remote_device)
{
	struct ISCI_REMOTE_DEVICE *isci_remote_device =
	    (struct ISCI_REMOTE_DEVICE *)sci_object_get_association(remote_device);
	struct ISCI_DOMAIN *isci_domain =
	    (struct ISCI_DOMAIN *)sci_object_get_association(domain);
	struct ISCI_CONTROLLER *isci_controller =
	    (struct ISCI_CONTROLLER *)sci_object_get_association(controller);
	uint32_t path = cam_sim_path(isci_controller->sim);
	union ccb *ccb = xpt_alloc_ccb_nowait();

	isci_controller->remote_device[isci_remote_device->index] = NULL;

	xpt_create_path(&ccb->ccb_h.path, NULL, path,
	    isci_remote_device->index, CAM_LUN_WILDCARD);

	xpt_rescan(ccb);

	scif_remote_device_destruct(remote_device);

	/*
	 * Only put the remote device back into the pool if it was an
	 *  expander-attached device.
	 */
	if (isci_remote_device != isci_domain->da_remote_device)
		sci_pool_put(isci_controller->remote_device_pool,
		    isci_remote_device);
}

void
isci_domain_construct(struct ISCI_DOMAIN *domain, uint32_t domain_index,
    struct ISCI_CONTROLLER *controller)
{

	scif_controller_get_domain_handle( controller->scif_controller_handle,
	    domain_index, &domain->sci_object);

	domain->index = domain_index;
	domain->controller = controller;
	sci_object_set_association(domain->sci_object, (void *)domain);
}
