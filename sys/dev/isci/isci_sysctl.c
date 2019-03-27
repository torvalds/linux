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

#include <sys/sysctl.h>

#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scic_phy.h>

static int
isci_sysctl_coalesce_timeout(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc *isci = (struct isci_softc *)arg1;
	int error = sysctl_handle_int(oidp, &isci->coalesce_timeout, 0, req);
	int i;

	if (error)
		return (error);

	for (i = 0; i < isci->controller_count; i++)
		scif_controller_set_interrupt_coalescence(
		    isci->controllers[i].scif_controller_handle,
		    isci->coalesce_number, isci->coalesce_timeout);

	return (0);
}

static int
isci_sysctl_coalesce_number(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc *isci = (struct isci_softc *)arg1;
	int error = sysctl_handle_int(oidp, &isci->coalesce_number, 0, req);
	int i;

	if (error)
		return (error);

	for (i = 0; i < isci->controller_count; i++)
		scif_controller_set_interrupt_coalescence(
		    isci->controllers[i].scif_controller_handle,
		    isci->coalesce_number, isci->coalesce_timeout);

	return (0);
}

static void
isci_sysctl_reset_remote_devices(struct ISCI_CONTROLLER *controller,
    uint32_t remote_devices_to_be_reset)
{
	uint32_t i = 0;

	while (remote_devices_to_be_reset != 0) {
		if (remote_devices_to_be_reset & 0x1) {
			struct ISCI_REMOTE_DEVICE *remote_device =
				controller->remote_device[i];

			if (remote_device != NULL) {
				mtx_lock(&controller->lock);
				isci_remote_device_reset(remote_device, NULL);
				mtx_unlock(&controller->lock);
			}
		}
		remote_devices_to_be_reset >>= 1;
		i++;
	}
}

static int
isci_sysctl_reset_remote_device_on_controller0(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc *isci = (struct isci_softc *)arg1;
	uint32_t remote_devices_to_be_reset = 0;
	struct ISCI_CONTROLLER *controller = &isci->controllers[0];
	int error = sysctl_handle_int(oidp, &remote_devices_to_be_reset, 0, req);

	if (error || remote_devices_to_be_reset == 0)
		return (error);

	isci_sysctl_reset_remote_devices(controller, remote_devices_to_be_reset);

	return (0);
}

static int
isci_sysctl_reset_remote_device_on_controller1(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc *isci = (struct isci_softc *)arg1;
	uint32_t remote_devices_to_be_reset = 0;
	struct ISCI_CONTROLLER *controller = &isci->controllers[1];
	int error =
	    sysctl_handle_int(oidp, &remote_devices_to_be_reset, 0, req);

	if (error || remote_devices_to_be_reset == 0)
		return (error);

	isci_sysctl_reset_remote_devices(controller,
	    remote_devices_to_be_reset);

	return (0);
}

static void
isci_sysctl_stop(struct ISCI_CONTROLLER *controller, uint32_t phy_to_be_stopped)
{
	SCI_PHY_HANDLE_T phy_handle = NULL;

	scic_controller_get_phy_handle(
	    scif_controller_get_scic_handle(controller->scif_controller_handle),
	    phy_to_be_stopped, &phy_handle);

	scic_phy_stop(phy_handle);
}

static int
isci_sysctl_stop_phy(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc *isci = (struct isci_softc *)arg1;
	uint32_t phy_to_be_stopped = 0xff;
	uint32_t controller_index, phy_index;
	int error = sysctl_handle_int(oidp, &phy_to_be_stopped, 0, req);

	controller_index = phy_to_be_stopped / SCI_MAX_PHYS;
	phy_index = phy_to_be_stopped % SCI_MAX_PHYS;

	if(error || controller_index >= isci->controller_count)
		return (error);

	isci_sysctl_stop(&isci->controllers[controller_index], phy_index);

	return (0);
}

static void
isci_sysctl_start(struct ISCI_CONTROLLER *controller,
    uint32_t phy_to_be_started)
{
	SCI_PHY_HANDLE_T phy_handle = NULL;

	scic_controller_get_phy_handle(
	    scif_controller_get_scic_handle(controller->scif_controller_handle),
	    phy_to_be_started, &phy_handle);

	scic_phy_start(phy_handle);
}

static int
isci_sysctl_start_phy(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc *isci = (struct isci_softc *)arg1;
	uint32_t phy_to_be_started = 0xff;
	uint32_t controller_index, phy_index;
	int error = sysctl_handle_int(oidp, &phy_to_be_started, 0, req);

	controller_index = phy_to_be_started / SCI_MAX_PHYS;
	phy_index = phy_to_be_started % SCI_MAX_PHYS;

	if(error || controller_index >= isci->controller_count)
		return error;

	isci_sysctl_start(&isci->controllers[controller_index], phy_index);

	return 0;
}

static int
isci_sysctl_log_frozen_lun_masks(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc	*isci = (struct isci_softc *)arg1;
	struct ISCI_REMOTE_DEVICE *device;
	int32_t			log_frozen_devices = 0;
	int			error, i, j;

	error = sysctl_handle_int(oidp, &log_frozen_devices, 0, req);

	if (error || log_frozen_devices == 0)
		return (error);

	for (i = 0; i < isci->controller_count; i++) {
		for (j = 0; j < SCI_MAX_REMOTE_DEVICES; j++) {
			device = isci->controllers[i].remote_device[j];

			if (device == NULL)
				continue;

			device_printf(isci->device,
			    "controller %d device %3d frozen_lun_mask 0x%02x\n",
			    i, j, device->frozen_lun_mask);
		}
	}

	return (0);
}

static int
isci_sysctl_fail_on_task_timeout(SYSCTL_HANDLER_ARGS)
{
	struct isci_softc	*isci = (struct isci_softc *)arg1;
	int32_t			fail_on_timeout;
	int			error, i;

	fail_on_timeout = isci->controllers[0].fail_on_task_timeout;
	error = sysctl_handle_int(oidp, &fail_on_timeout, 0, req);

	if (error || req->newptr == NULL)
		return (error);

	for (i = 0; i < isci->controller_count; i++)
		isci->controllers[i].fail_on_task_timeout = fail_on_timeout;

	return (0);
}

void isci_sysctl_initialize(struct isci_softc *isci)
{
	struct sysctl_ctx_list *sysctl_ctx = device_get_sysctl_ctx(isci->device);
	struct sysctl_oid *sysctl_tree = device_get_sysctl_tree(isci->device);

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "coalesce_timeout", CTLTYPE_UINT | CTLFLAG_RW, isci, 0,
	    isci_sysctl_coalesce_timeout, "IU",
	    "Interrupt coalescing timeout (in microseconds)");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "coalesce_number", CTLTYPE_UINT | CTLFLAG_RW, isci, 0,
	    isci_sysctl_coalesce_number, "IU",
	    "Interrupt coalescing number");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "reset_remote_device_on_controller0", CTLTYPE_UINT| CTLFLAG_RW,
	    isci, 0, isci_sysctl_reset_remote_device_on_controller0, "IU",
	    "Reset remote device on controller 0");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "reset_remote_device_on_controller1", CTLTYPE_UINT| CTLFLAG_RW,
	    isci, 0, isci_sysctl_reset_remote_device_on_controller1, "IU",
	    "Reset remote device on controller 1");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "stop_phy", CTLTYPE_UINT| CTLFLAG_RW, isci, 0, isci_sysctl_stop_phy,
	    "IU", "Stop PHY on a controller");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "start_phy", CTLTYPE_UINT| CTLFLAG_RW, isci, 0,
	    isci_sysctl_start_phy, "IU", "Start PHY on a controller");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "log_frozen_lun_masks", CTLTYPE_UINT| CTLFLAG_RW, isci, 0,
	    isci_sysctl_log_frozen_lun_masks, "IU",
	    "Log frozen lun masks to kernel log");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
	    "fail_on_task_timeout", CTLTYPE_UINT | CTLFLAG_RW, isci, 0,
	    isci_sysctl_fail_on_task_timeout, "IU",
	    "Fail a command that has encountered a task management timeout");
}

