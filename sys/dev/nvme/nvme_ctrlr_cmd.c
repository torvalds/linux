/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012-2013 Intel Corporation
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

#include "nvme_private.h"

void
nvme_ctrlr_cmd_identify_controller(struct nvme_controller *ctrlr, void *payload,
	nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_vaddr(payload,
	    sizeof(struct nvme_controller_data), cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_IDENTIFY;

	/*
	 * TODO: create an identify command data structure, which
	 *  includes this CNS bit in cdw10.
	 */
	cmd->cdw10 = htole32(1);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_identify_namespace(struct nvme_controller *ctrlr, uint32_t nsid,
	void *payload, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_vaddr(payload,
	    sizeof(struct nvme_namespace_data), cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_IDENTIFY;

	/*
	 * TODO: create an identify command data structure
	 */
	cmd->nsid = htole32(nsid);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_create_io_cq(struct nvme_controller *ctrlr,
    struct nvme_qpair *io_que, uint16_t vector, nvme_cb_fn_t cb_fn,
    void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_null(cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_CREATE_IO_CQ;

	/*
	 * TODO: create a create io completion queue command data
	 *  structure.
	 */
	cmd->cdw10 = htole32(((io_que->num_entries-1) << 16) | io_que->id);
	/* 0x3 = interrupts enabled | physically contiguous */
	cmd->cdw11 = htole32((vector << 16) | 0x3);
	cmd->prp1 = htole64(io_que->cpl_bus_addr);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_create_io_sq(struct nvme_controller *ctrlr,
    struct nvme_qpair *io_que, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_null(cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_CREATE_IO_SQ;

	/*
	 * TODO: create a create io submission queue command data
	 *  structure.
	 */
	cmd->cdw10 = htole32(((io_que->num_entries-1) << 16) | io_que->id);
	/* 0x1 = physically contiguous */
	cmd->cdw11 = htole32((io_que->id << 16) | 0x1);
	cmd->prp1 = htole64(io_que->cmd_bus_addr);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_delete_io_cq(struct nvme_controller *ctrlr,
    struct nvme_qpair *io_que, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_null(cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_DELETE_IO_CQ;

	/*
	 * TODO: create a delete io completion queue command data
	 *  structure.
	 */
	cmd->cdw10 = htole32(io_que->id);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_delete_io_sq(struct nvme_controller *ctrlr,
    struct nvme_qpair *io_que, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_null(cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_DELETE_IO_SQ;

	/*
	 * TODO: create a delete io submission queue command data
	 *  structure.
	 */
	cmd->cdw10 = htole32(io_que->id);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_set_feature(struct nvme_controller *ctrlr, uint8_t feature,
    uint32_t cdw11, void *payload, uint32_t payload_size,
    nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_null(cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_SET_FEATURES;
	cmd->cdw10 = htole32(feature);
	cmd->cdw11 = htole32(cdw11);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_get_feature(struct nvme_controller *ctrlr, uint8_t feature,
    uint32_t cdw11, void *payload, uint32_t payload_size,
    nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_null(cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_GET_FEATURES;
	cmd->cdw10 = htole32(feature);
	cmd->cdw11 = htole32(cdw11);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_set_num_queues(struct nvme_controller *ctrlr,
    uint32_t num_queues, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	uint32_t cdw11;

	cdw11 = ((num_queues - 1) << 16) | (num_queues - 1);
	nvme_ctrlr_cmd_set_feature(ctrlr, NVME_FEAT_NUMBER_OF_QUEUES, cdw11,
	    NULL, 0, cb_fn, cb_arg);
}

void
nvme_ctrlr_cmd_set_async_event_config(struct nvme_controller *ctrlr,
    uint32_t state, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	uint32_t cdw11;

	cdw11 = state;
	nvme_ctrlr_cmd_set_feature(ctrlr,
	    NVME_FEAT_ASYNC_EVENT_CONFIGURATION, cdw11, NULL, 0, cb_fn,
	    cb_arg);
}

void
nvme_ctrlr_cmd_set_interrupt_coalescing(struct nvme_controller *ctrlr,
    uint32_t microseconds, uint32_t threshold, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	uint32_t cdw11;

	if ((microseconds/100) >= 0x100) {
		nvme_printf(ctrlr, "invalid coal time %d, disabling\n",
		    microseconds);
		microseconds = 0;
		threshold = 0;
	}

	if (threshold >= 0x100) {
		nvme_printf(ctrlr, "invalid threshold %d, disabling\n",
		    threshold);
		threshold = 0;
		microseconds = 0;
	}

	cdw11 = ((microseconds/100) << 8) | threshold;
	nvme_ctrlr_cmd_set_feature(ctrlr, NVME_FEAT_INTERRUPT_COALESCING, cdw11,
	    NULL, 0, cb_fn, cb_arg);
}

void
nvme_ctrlr_cmd_get_log_page(struct nvme_controller *ctrlr, uint8_t log_page,
    uint32_t nsid, void *payload, uint32_t payload_size, nvme_cb_fn_t cb_fn,
    void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_vaddr(payload, payload_size, cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_GET_LOG_PAGE;
	cmd->nsid = htole32(nsid);
	cmd->cdw10 = ((payload_size/sizeof(uint32_t)) - 1) << 16;
	cmd->cdw10 |= log_page;
	cmd->cdw10 = htole32(cmd->cdw10);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}

void
nvme_ctrlr_cmd_get_error_page(struct nvme_controller *ctrlr,
    struct nvme_error_information_entry *payload, uint32_t num_entries,
    nvme_cb_fn_t cb_fn, void *cb_arg)
{

	KASSERT(num_entries > 0, ("%s called with num_entries==0\n", __func__));

	/* Controller's error log page entries is 0-based. */
	KASSERT(num_entries <= (ctrlr->cdata.elpe + 1),
	    ("%s called with num_entries=%d but (elpe+1)=%d\n", __func__,
	    num_entries, ctrlr->cdata.elpe + 1));

	if (num_entries > (ctrlr->cdata.elpe + 1))
		num_entries = ctrlr->cdata.elpe + 1;

	nvme_ctrlr_cmd_get_log_page(ctrlr, NVME_LOG_ERROR,
	    NVME_GLOBAL_NAMESPACE_TAG, payload, sizeof(*payload) * num_entries,
	    cb_fn, cb_arg);
}

void
nvme_ctrlr_cmd_get_health_information_page(struct nvme_controller *ctrlr,
    uint32_t nsid, struct nvme_health_information_page *payload,
    nvme_cb_fn_t cb_fn, void *cb_arg)
{

	nvme_ctrlr_cmd_get_log_page(ctrlr, NVME_LOG_HEALTH_INFORMATION,
	    nsid, payload, sizeof(*payload), cb_fn, cb_arg);
}

void
nvme_ctrlr_cmd_get_firmware_page(struct nvme_controller *ctrlr,
    struct nvme_firmware_page *payload, nvme_cb_fn_t cb_fn, void *cb_arg)
{

	nvme_ctrlr_cmd_get_log_page(ctrlr, NVME_LOG_FIRMWARE_SLOT, 
	    NVME_GLOBAL_NAMESPACE_TAG, payload, sizeof(*payload), cb_fn,
	    cb_arg);
}

void
nvme_ctrlr_cmd_abort(struct nvme_controller *ctrlr, uint16_t cid,
    uint16_t sqid, nvme_cb_fn_t cb_fn, void *cb_arg)
{
	struct nvme_request *req;
	struct nvme_command *cmd;

	req = nvme_allocate_request_null(cb_fn, cb_arg);

	cmd = &req->cmd;
	cmd->opc = NVME_OPC_ABORT;
	cmd->cdw10 = htole32((cid << 16) | sqid);

	nvme_ctrlr_submit_admin_request(ctrlr, req);
}
