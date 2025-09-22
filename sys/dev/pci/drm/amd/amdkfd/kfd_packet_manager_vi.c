// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "kfd_kernel_queue.h"
#include "kfd_device_queue_manager.h"
#include "kfd_pm4_headers_vi.h"
#include "kfd_pm4_opcodes.h"

unsigned int pm_build_pm4_header(unsigned int opcode, size_t packet_size)
{
	union PM4_MES_TYPE_3_HEADER header;

	header.u32All = 0;
	header.opcode = opcode;
	header.count = packet_size / 4 - 2;
	header.type = PM4_TYPE_3;

	return header.u32All;
}

static int pm_map_process_vi(struct packet_manager *pm, uint32_t *buffer,
				struct qcm_process_device *qpd)
{
	struct pm4_mes_map_process *packet;

	packet = (struct pm4_mes_map_process *)buffer;

	memset(buffer, 0, sizeof(struct pm4_mes_map_process));

	packet->header.u32All = pm_build_pm4_header(IT_MAP_PROCESS,
					sizeof(struct pm4_mes_map_process));
	packet->bitfields2.diq_enable = (qpd->is_debug) ? 1 : 0;
	packet->bitfields2.process_quantum = 10;
	packet->bitfields2.pasid = qpd->pqm->process->pasid;
	packet->bitfields3.page_table_base = qpd->page_table_base;
	packet->bitfields10.gds_size = qpd->gds_size;
	packet->bitfields10.num_gws = qpd->num_gws;
	packet->bitfields10.num_oac = qpd->num_oac;
	packet->bitfields10.num_queues = (qpd->is_debug) ? 0 : qpd->queue_count;

	packet->sh_mem_config = qpd->sh_mem_config;
	packet->sh_mem_bases = qpd->sh_mem_bases;
	packet->sh_mem_ape1_base = qpd->sh_mem_ape1_base;
	packet->sh_mem_ape1_limit = qpd->sh_mem_ape1_limit;

	packet->sh_hidden_private_base_vmid = qpd->sh_hidden_private_base;

	packet->gds_addr_lo = lower_32_bits(qpd->gds_context_area);
	packet->gds_addr_hi = upper_32_bits(qpd->gds_context_area);

	return 0;
}

static int pm_runlist_vi(struct packet_manager *pm, uint32_t *buffer,
			uint64_t ib, size_t ib_size_in_dwords, bool chain)
{
	struct pm4_mes_runlist *packet;
	int concurrent_proc_cnt = 0;
	struct kfd_node *kfd = pm->dqm->dev;

	if (WARN_ON(!ib))
		return -EFAULT;

	/* Determine the number of processes to map together to HW:
	 * it can not exceed the number of VMIDs available to the
	 * scheduler, and it is determined by the smaller of the number
	 * of processes in the runlist and kfd module parameter
	 * hws_max_conc_proc.
	 * Note: the arbitration between the number of VMIDs and
	 * hws_max_conc_proc has been done in
	 * kgd2kfd_device_init().
	 */
	concurrent_proc_cnt = min(pm->dqm->processes_count,
			kfd->max_proc_per_quantum);

	packet = (struct pm4_mes_runlist *)buffer;

	memset(buffer, 0, sizeof(struct pm4_mes_runlist));
	packet->header.u32All = pm_build_pm4_header(IT_RUN_LIST,
						sizeof(struct pm4_mes_runlist));

	packet->bitfields4.ib_size = ib_size_in_dwords;
	packet->bitfields4.chain = chain ? 1 : 0;
	packet->bitfields4.offload_polling = 0;
	packet->bitfields4.valid = 1;
	packet->bitfields4.process_cnt = concurrent_proc_cnt;
	packet->ordinal2 = lower_32_bits(ib);
	packet->bitfields3.ib_base_hi = upper_32_bits(ib);

	return 0;
}

static int pm_set_resources_vi(struct packet_manager *pm, uint32_t *buffer,
			       struct scheduling_resources *res)
{
	struct pm4_mes_set_resources *packet;

	packet = (struct pm4_mes_set_resources *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_set_resources));

	packet->header.u32All = pm_build_pm4_header(IT_SET_RESOURCES,
					sizeof(struct pm4_mes_set_resources));

	packet->bitfields2.queue_type =
			queue_type__mes_set_resources__hsa_interface_queue_hiq;
	packet->bitfields2.vmid_mask = res->vmid_mask;
	packet->bitfields2.unmap_latency = KFD_UNMAP_LATENCY_MS / 100;
	packet->bitfields7.oac_mask = res->oac_mask;
	packet->bitfields8.gds_heap_base = res->gds_heap_base;
	packet->bitfields8.gds_heap_size = res->gds_heap_size;

	packet->gws_mask_lo = lower_32_bits(res->gws_mask);
	packet->gws_mask_hi = upper_32_bits(res->gws_mask);

	packet->queue_mask_lo = lower_32_bits(res->queue_mask);
	packet->queue_mask_hi = upper_32_bits(res->queue_mask);

	return 0;
}

static int pm_map_queues_vi(struct packet_manager *pm, uint32_t *buffer,
		struct queue *q, bool is_static)
{
	struct pm4_mes_map_queues *packet;
	bool use_static = is_static;

	packet = (struct pm4_mes_map_queues *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_map_queues));

	packet->header.u32All = pm_build_pm4_header(IT_MAP_QUEUES,
					sizeof(struct pm4_mes_map_queues));
	packet->bitfields2.num_queues = 1;
	packet->bitfields2.queue_sel =
		queue_sel__mes_map_queues__map_to_hws_determined_queue_slots_vi;

	packet->bitfields2.engine_sel =
		engine_sel__mes_map_queues__compute_vi;
	packet->bitfields2.queue_type =
		queue_type__mes_map_queues__normal_compute_vi;

	switch (q->properties.type) {
	case KFD_QUEUE_TYPE_COMPUTE:
		if (use_static)
			packet->bitfields2.queue_type =
		queue_type__mes_map_queues__normal_latency_static_queue_vi;
		break;
	case KFD_QUEUE_TYPE_DIQ:
		packet->bitfields2.queue_type =
			queue_type__mes_map_queues__debug_interface_queue_vi;
		break;
	case KFD_QUEUE_TYPE_SDMA:
	case KFD_QUEUE_TYPE_SDMA_XGMI:
		packet->bitfields2.engine_sel = q->properties.sdma_engine_id +
				engine_sel__mes_map_queues__sdma0_vi;
		use_static = false; /* no static queues under SDMA */
		break;
	default:
		WARN(1, "queue type %d", q->properties.type);
		return -EINVAL;
	}
	packet->bitfields3.doorbell_offset =
			q->properties.doorbell_off;

	packet->mqd_addr_lo =
			lower_32_bits(q->gart_mqd_addr);

	packet->mqd_addr_hi =
			upper_32_bits(q->gart_mqd_addr);

	packet->wptr_addr_lo =
			lower_32_bits((uint64_t)q->properties.write_ptr);

	packet->wptr_addr_hi =
			upper_32_bits((uint64_t)q->properties.write_ptr);

	return 0;
}

static int pm_unmap_queues_vi(struct packet_manager *pm, uint32_t *buffer,
			enum kfd_unmap_queues_filter filter,
			uint32_t filter_param, bool reset)
{
	struct pm4_mes_unmap_queues *packet;

	packet = (struct pm4_mes_unmap_queues *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_unmap_queues));

	packet->header.u32All = pm_build_pm4_header(IT_UNMAP_QUEUES,
					sizeof(struct pm4_mes_unmap_queues));

	packet->bitfields2.engine_sel =
			engine_sel__mes_unmap_queues__compute;

	if (reset)
		packet->bitfields2.action =
			action__mes_unmap_queues__reset_queues;
	else
		packet->bitfields2.action =
			action__mes_unmap_queues__preempt_queues;

	switch (filter) {
	case KFD_UNMAP_QUEUES_FILTER_BY_PASID:
		packet->bitfields2.queue_sel =
			queue_sel__mes_unmap_queues__perform_request_on_pasid_queues;
		packet->bitfields3a.pasid = filter_param;
		break;
	case KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES:
		packet->bitfields2.queue_sel =
			queue_sel__mes_unmap_queues__unmap_all_queues;
		break;
	case KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES:
		/* in this case, we do not preempt static queues */
		packet->bitfields2.queue_sel =
			queue_sel__mes_unmap_queues__unmap_all_non_static_queues;
		break;
	default:
		WARN(1, "filter %d", filter);
		return -EINVAL;
	}

	return 0;

}

static int pm_query_status_vi(struct packet_manager *pm, uint32_t *buffer,
			uint64_t fence_address,	uint64_t fence_value)
{
	struct pm4_mes_query_status *packet;

	packet = (struct pm4_mes_query_status *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_query_status));

	packet->header.u32All = pm_build_pm4_header(IT_QUERY_STATUS,
					sizeof(struct pm4_mes_query_status));

	packet->bitfields2.context_id = 0;
	packet->bitfields2.interrupt_sel =
			interrupt_sel__mes_query_status__completion_status;
	packet->bitfields2.command =
			command__mes_query_status__fence_only_after_write_ack;

	packet->addr_hi = upper_32_bits((uint64_t)fence_address);
	packet->addr_lo = lower_32_bits((uint64_t)fence_address);
	packet->data_hi = upper_32_bits((uint64_t)fence_value);
	packet->data_lo = lower_32_bits((uint64_t)fence_value);

	return 0;
}

static int pm_release_mem_vi(uint64_t gpu_addr, uint32_t *buffer)
{
	struct pm4_mec_release_mem *packet;

	packet = (struct pm4_mec_release_mem *)buffer;
	memset(buffer, 0, sizeof(*packet));

	packet->header.u32All = pm_build_pm4_header(IT_RELEASE_MEM,
						 sizeof(*packet));

	packet->bitfields2.event_type = CACHE_FLUSH_AND_INV_TS_EVENT;
	packet->bitfields2.event_index = event_index___release_mem__end_of_pipe;
	packet->bitfields2.tcl1_action_ena = 1;
	packet->bitfields2.tc_action_ena = 1;
	packet->bitfields2.cache_policy = cache_policy___release_mem__lru;
	packet->bitfields2.atc = 0;

	packet->bitfields3.data_sel = data_sel___release_mem__send_32_bit_low;
	packet->bitfields3.int_sel =
		int_sel___release_mem__send_interrupt_after_write_confirm;

	packet->bitfields4.address_lo_32b = (gpu_addr & 0xffffffff) >> 2;
	packet->address_hi = upper_32_bits(gpu_addr);

	packet->data_lo = 0;

	return 0;
}

const struct packet_manager_funcs kfd_vi_pm_funcs = {
	.map_process		= pm_map_process_vi,
	.runlist		= pm_runlist_vi,
	.set_resources		= pm_set_resources_vi,
	.map_queues		= pm_map_queues_vi,
	.unmap_queues		= pm_unmap_queues_vi,
	.set_grace_period	= NULL,
	.query_status		= pm_query_status_vi,
	.release_mem		= pm_release_mem_vi,
	.map_process_size	= sizeof(struct pm4_mes_map_process),
	.runlist_size		= sizeof(struct pm4_mes_runlist),
	.set_resources_size	= sizeof(struct pm4_mes_set_resources),
	.map_queues_size	= sizeof(struct pm4_mes_map_queues),
	.unmap_queues_size	= sizeof(struct pm4_mes_unmap_queues),
	.set_grace_period_size	= 0,
	.query_status_size	= sizeof(struct pm4_mes_query_status),
	.release_mem_size	= sizeof(struct pm4_mec_release_mem)
};
