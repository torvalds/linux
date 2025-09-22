// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2016-2022 Advanced Micro Devices, Inc.
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
#include "kfd_pm4_headers_ai.h"
#include "kfd_pm4_headers_aldebaran.h"
#include "kfd_pm4_opcodes.h"
#include "gc/gc_10_1_0_sh_mask.h"

static int pm_map_process_v9(struct packet_manager *pm,
		uint32_t *buffer, struct qcm_process_device *qpd)
{
	struct pm4_mes_map_process *packet;
	uint64_t vm_page_table_base_addr = qpd->page_table_base;
	struct kfd_node *kfd = pm->dqm->dev;
	struct kfd_process_device *pdd =
			container_of(qpd, struct kfd_process_device, qpd);
	struct amdgpu_device *adev = kfd->adev;

	packet = (struct pm4_mes_map_process *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_map_process));
	packet->header.u32All = pm_build_pm4_header(IT_MAP_PROCESS,
					sizeof(struct pm4_mes_map_process));
	if (adev->enforce_isolation[kfd->node_id])
		packet->bitfields2.exec_cleaner_shader = 1;
	packet->bitfields2.diq_enable = (qpd->is_debug) ? 1 : 0;
	packet->bitfields2.process_quantum = 10;
	packet->bitfields2.pasid = qpd->pqm->process->pasid;
	packet->bitfields14.gds_size = qpd->gds_size & 0x3F;
	packet->bitfields14.gds_size_hi = (qpd->gds_size >> 6) & 0xF;
	packet->bitfields14.num_gws = (qpd->mapped_gws_queue) ? qpd->num_gws : 0;
	packet->bitfields14.num_oac = qpd->num_oac;
	packet->bitfields14.sdma_enable = 1;
	packet->bitfields14.num_queues = (qpd->is_debug) ? 0 : qpd->queue_count;

	if (kfd->dqm->trap_debug_vmid && pdd->process->debug_trap_enabled &&
			pdd->process->runtime_info.runtime_state == DEBUG_RUNTIME_STATE_ENABLED) {
		packet->bitfields2.debug_vmid = kfd->dqm->trap_debug_vmid;
		packet->bitfields2.new_debug = 1;
	}

	packet->sh_mem_config = qpd->sh_mem_config;
	packet->sh_mem_bases = qpd->sh_mem_bases;
	if (qpd->tba_addr) {
		packet->sq_shader_tba_lo = lower_32_bits(qpd->tba_addr >> 8);
		/* On GFX9, unlike GFX10, bit TRAP_EN of SQ_SHADER_TBA_HI is
		 * not defined, so setting it won't do any harm.
		 */
		packet->sq_shader_tba_hi = upper_32_bits(qpd->tba_addr >> 8)
				| 1 << SQ_SHADER_TBA_HI__TRAP_EN__SHIFT;

		packet->sq_shader_tma_lo = lower_32_bits(qpd->tma_addr >> 8);
		packet->sq_shader_tma_hi = upper_32_bits(qpd->tma_addr >> 8);
	}

	packet->gds_addr_lo = lower_32_bits(qpd->gds_context_area);
	packet->gds_addr_hi = upper_32_bits(qpd->gds_context_area);

	packet->vm_context_page_table_base_addr_lo32 =
			lower_32_bits(vm_page_table_base_addr);
	packet->vm_context_page_table_base_addr_hi32 =
			upper_32_bits(vm_page_table_base_addr);

	return 0;
}

static int pm_map_process_aldebaran(struct packet_manager *pm,
		uint32_t *buffer, struct qcm_process_device *qpd)
{
	struct pm4_mes_map_process_aldebaran *packet;
	uint64_t vm_page_table_base_addr = qpd->page_table_base;
	struct kfd_dev *kfd = pm->dqm->dev->kfd;
	struct kfd_node *knode = pm->dqm->dev;
	struct kfd_process_device *pdd =
			container_of(qpd, struct kfd_process_device, qpd);
	int i;
	struct amdgpu_device *adev = kfd->adev;

	packet = (struct pm4_mes_map_process_aldebaran *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_map_process_aldebaran));
	packet->header.u32All = pm_build_pm4_header(IT_MAP_PROCESS,
			sizeof(struct pm4_mes_map_process_aldebaran));
	if (adev->enforce_isolation[knode->node_id])
		packet->bitfields2.exec_cleaner_shader = 1;
	packet->bitfields2.diq_enable = (qpd->is_debug) ? 1 : 0;
	packet->bitfields2.process_quantum = 10;
	packet->bitfields2.pasid = qpd->pqm->process->pasid;
	packet->bitfields14.gds_size = qpd->gds_size & 0x3F;
	packet->bitfields14.gds_size_hi = (qpd->gds_size >> 6) & 0xF;
	packet->bitfields14.num_gws = (qpd->mapped_gws_queue) ? qpd->num_gws : 0;
	packet->bitfields14.num_oac = qpd->num_oac;
	packet->bitfields14.sdma_enable = 1;
	packet->bitfields14.num_queues = (qpd->is_debug) ? 0 : qpd->queue_count;
	packet->spi_gdbg_per_vmid_cntl = pdd->spi_dbg_override |
						pdd->spi_dbg_launch_mode;

	if (pdd->process->debug_trap_enabled) {
		for (i = 0; i < kfd->device_info.num_of_watch_points; i++)
			packet->tcp_watch_cntl[i] = pdd->watch_points[i];

		packet->bitfields2.single_memops =
				!!(pdd->process->dbg_flags & KFD_DBG_TRAP_FLAG_SINGLE_MEM_OP);
	}

	packet->sh_mem_config = qpd->sh_mem_config;
	packet->sh_mem_bases = qpd->sh_mem_bases;
	if (qpd->tba_addr) {
		packet->sq_shader_tba_lo = lower_32_bits(qpd->tba_addr >> 8);
		packet->sq_shader_tba_hi = upper_32_bits(qpd->tba_addr >> 8);
		packet->sq_shader_tma_lo = lower_32_bits(qpd->tma_addr >> 8);
		packet->sq_shader_tma_hi = upper_32_bits(qpd->tma_addr >> 8);
	}

	packet->gds_addr_lo = lower_32_bits(qpd->gds_context_area);
	packet->gds_addr_hi = upper_32_bits(qpd->gds_context_area);

	packet->vm_context_page_table_base_addr_lo32 =
			lower_32_bits(vm_page_table_base_addr);
	packet->vm_context_page_table_base_addr_hi32 =
			upper_32_bits(vm_page_table_base_addr);

	return 0;
}

static int pm_runlist_v9(struct packet_manager *pm, uint32_t *buffer,
			uint64_t ib, size_t ib_size_in_dwords, bool chain)
{
	struct pm4_mes_runlist *packet;

	int concurrent_proc_cnt = 0;
	struct kfd_node *kfd = pm->dqm->dev;
	struct amdgpu_device *adev = kfd->adev;

	/* Determine the number of processes to map together to HW:
	 * it can not exceed the number of VMIDs available to the
	 * scheduler, and it is determined by the smaller of the number
	 * of processes in the runlist and kfd module parameter
	 * hws_max_conc_proc.
	 * However, if enforce_isolation is set (toggle LDS/VGPRs/SGPRs
	 * cleaner between process switch), enable single-process mode
	 * in HWS.
	 * Note: the arbitration between the number of VMIDs and
	 * hws_max_conc_proc has been done in
	 * kgd2kfd_device_init().
	 */
	concurrent_proc_cnt = adev->enforce_isolation[kfd->node_id] ?
			1 : min(pm->dqm->processes_count,
			kfd->max_proc_per_quantum);

	packet = (struct pm4_mes_runlist *)buffer;

	memset(buffer, 0, sizeof(struct pm4_mes_runlist));
	packet->header.u32All = pm_build_pm4_header(IT_RUN_LIST,
						sizeof(struct pm4_mes_runlist));

	packet->bitfields4.ib_size = ib_size_in_dwords;
	packet->bitfields4.chain = chain ? 1 : 0;
	packet->bitfields4.offload_polling = 0;
	packet->bitfields4.chained_runlist_idle_disable = chain ? 1 : 0;
	packet->bitfields4.valid = 1;
	packet->bitfields4.process_cnt = concurrent_proc_cnt;
	packet->ordinal2 = lower_32_bits(ib);
	packet->ib_base_hi = upper_32_bits(ib);

	return 0;
}

static int pm_set_resources_v9(struct packet_manager *pm, uint32_t *buffer,
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

static inline bool pm_use_ext_eng(struct kfd_dev *dev)
{
	return amdgpu_ip_version(dev->adev, SDMA0_HWIP, 0) >=
	       IP_VERSION(5, 2, 0);
}

static int pm_map_queues_v9(struct packet_manager *pm, uint32_t *buffer,
		struct queue *q, bool is_static)
{
	struct pm4_mes_map_queues *packet;

	packet = (struct pm4_mes_map_queues *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_map_queues));

	packet->header.u32All = pm_build_pm4_header(IT_MAP_QUEUES,
					sizeof(struct pm4_mes_map_queues));
	packet->bitfields2.num_queues = 1;
	packet->bitfields2.queue_sel =
		queue_sel__mes_map_queues__map_to_hws_determined_queue_slots_vi;

	packet->bitfields2.engine_sel =
		engine_sel__mes_map_queues__compute_vi;
	packet->bitfields2.gws_control_queue = q->properties.is_gws ? 1 : 0;
	packet->bitfields2.extended_engine_sel =
		extended_engine_sel__mes_map_queues__legacy_engine_sel;
	packet->bitfields2.queue_type =
		queue_type__mes_map_queues__normal_compute_vi;

	switch (q->properties.type) {
	case KFD_QUEUE_TYPE_COMPUTE:
		if (is_static)
			packet->bitfields2.queue_type =
		queue_type__mes_map_queues__normal_latency_static_queue_vi;
		break;
	case KFD_QUEUE_TYPE_DIQ:
		packet->bitfields2.queue_type =
			queue_type__mes_map_queues__debug_interface_queue_vi;
		break;
	case KFD_QUEUE_TYPE_SDMA:
	case KFD_QUEUE_TYPE_SDMA_XGMI:
		if (q->properties.sdma_engine_id < 2 &&
		    !pm_use_ext_eng(q->device->kfd))
			packet->bitfields2.engine_sel = q->properties.sdma_engine_id +
				engine_sel__mes_map_queues__sdma0_vi;
		else {
			/*
			 * For GFX9.4.3, SDMA engine id can be greater than 8.
			 * For such cases, set extended_engine_sel to 2 and
			 * ensure engine_sel lies between 0-7.
			 */
			if (q->properties.sdma_engine_id >= 8)
				packet->bitfields2.extended_engine_sel =
					extended_engine_sel__mes_map_queues__sdma8_to_15_sel;
			else
				packet->bitfields2.extended_engine_sel =
					extended_engine_sel__mes_map_queues__sdma0_to_7_sel;

			packet->bitfields2.engine_sel = q->properties.sdma_engine_id % 8;
		}
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

static int pm_set_grace_period_v9(struct packet_manager *pm,
		uint32_t *buffer,
		uint32_t grace_period)
{
	struct pm4_mec_write_data_mmio *packet;
	uint32_t reg_offset = 0;
	uint32_t reg_data = 0;

	pm->dqm->dev->kfd2kgd->build_grace_period_packet_info(
			pm->dqm->dev->adev,
			pm->dqm->wait_times,
			grace_period,
			&reg_offset,
			&reg_data);

	if (grace_period == USE_DEFAULT_GRACE_PERIOD)
		reg_data = pm->dqm->wait_times;

	packet = (struct pm4_mec_write_data_mmio *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mec_write_data_mmio));

	packet->header.u32All = pm_build_pm4_header(IT_WRITE_DATA,
					sizeof(struct pm4_mec_write_data_mmio));

	packet->bitfields2.dst_sel  = dst_sel___write_data__mem_mapped_register;
	packet->bitfields2.addr_incr =
			addr_incr___write_data__do_not_increment_address;

	packet->bitfields3.dst_mmreg_addr = reg_offset;

	packet->data = reg_data;

	return 0;
}

static int pm_unmap_queues_v9(struct packet_manager *pm, uint32_t *buffer,
			enum kfd_unmap_queues_filter filter,
			uint32_t filter_param, bool reset)
{
	struct pm4_mes_unmap_queues *packet;

	packet = (struct pm4_mes_unmap_queues *)buffer;
	memset(buffer, 0, sizeof(struct pm4_mes_unmap_queues));

	packet->header.u32All = pm_build_pm4_header(IT_UNMAP_QUEUES,
					sizeof(struct pm4_mes_unmap_queues));

	packet->bitfields2.extended_engine_sel =
				pm_use_ext_eng(pm->dqm->dev->kfd) ?
		extended_engine_sel__mes_unmap_queues__sdma0_to_7_sel :
		extended_engine_sel__mes_unmap_queues__legacy_engine_sel;

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

static int pm_query_status_v9(struct packet_manager *pm, uint32_t *buffer,
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

const struct packet_manager_funcs kfd_v9_pm_funcs = {
	.map_process		= pm_map_process_v9,
	.runlist		= pm_runlist_v9,
	.set_resources		= pm_set_resources_v9,
	.map_queues		= pm_map_queues_v9,
	.unmap_queues		= pm_unmap_queues_v9,
	.set_grace_period       = pm_set_grace_period_v9,
	.query_status		= pm_query_status_v9,
	.release_mem		= NULL,
	.map_process_size	= sizeof(struct pm4_mes_map_process),
	.runlist_size		= sizeof(struct pm4_mes_runlist),
	.set_resources_size	= sizeof(struct pm4_mes_set_resources),
	.map_queues_size	= sizeof(struct pm4_mes_map_queues),
	.unmap_queues_size	= sizeof(struct pm4_mes_unmap_queues),
	.set_grace_period_size  = sizeof(struct pm4_mec_write_data_mmio),
	.query_status_size	= sizeof(struct pm4_mes_query_status),
	.release_mem_size	= 0,
};

const struct packet_manager_funcs kfd_aldebaran_pm_funcs = {
	.map_process		= pm_map_process_aldebaran,
	.runlist		= pm_runlist_v9,
	.set_resources		= pm_set_resources_v9,
	.map_queues		= pm_map_queues_v9,
	.unmap_queues		= pm_unmap_queues_v9,
	.set_grace_period       = pm_set_grace_period_v9,
	.query_status		= pm_query_status_v9,
	.release_mem		= NULL,
	.map_process_size	= sizeof(struct pm4_mes_map_process_aldebaran),
	.runlist_size		= sizeof(struct pm4_mes_runlist),
	.set_resources_size	= sizeof(struct pm4_mes_set_resources),
	.map_queues_size	= sizeof(struct pm4_mes_map_queues),
	.unmap_queues_size	= sizeof(struct pm4_mes_unmap_queues),
	.set_grace_period_size  = sizeof(struct pm4_mec_write_data_mmio),
	.query_status_size	= sizeof(struct pm4_mes_query_status),
	.release_mem_size	= 0,
};
