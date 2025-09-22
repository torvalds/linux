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

#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/mm_types.h>

#include "kfd_priv.h"
#include "kfd_mqd_manager.h"
#include "cik_regs.h"
#include "cik_structs.h"
#include "oss/oss_2_4_sh_mask.h"

static inline struct cik_mqd *get_mqd(void *mqd)
{
	return (struct cik_mqd *)mqd;
}

static inline struct cik_sdma_rlc_registers *get_sdma_mqd(void *mqd)
{
	return (struct cik_sdma_rlc_registers *)mqd;
}

static void update_cu_mask(struct mqd_manager *mm, void *mqd,
			struct mqd_update_info *minfo)
{
	struct cik_mqd *m;
	uint32_t se_mask[4] = {0}; /* 4 is the max # of SEs */

	if (!minfo || !minfo->cu_mask.ptr)
		return;

	mqd_symmetrically_map_cu_mask(mm,
		minfo->cu_mask.ptr, minfo->cu_mask.count, se_mask, 0);

	m = get_mqd(mqd);
	m->compute_static_thread_mgmt_se0 = se_mask[0];
	m->compute_static_thread_mgmt_se1 = se_mask[1];
	m->compute_static_thread_mgmt_se2 = se_mask[2];
	m->compute_static_thread_mgmt_se3 = se_mask[3];

	pr_debug("Update cu mask to %#x %#x %#x %#x\n",
		m->compute_static_thread_mgmt_se0,
		m->compute_static_thread_mgmt_se1,
		m->compute_static_thread_mgmt_se2,
		m->compute_static_thread_mgmt_se3);
}

static void set_priority(struct cik_mqd *m, struct queue_properties *q)
{
	m->cp_hqd_pipe_priority = pipe_priority_map[q->priority];
	m->cp_hqd_queue_priority = q->priority;
}

static struct kfd_mem_obj *allocate_mqd(struct kfd_node *kfd,
					struct queue_properties *q)
{
	struct kfd_mem_obj *mqd_mem_obj;

	if (kfd_gtt_sa_allocate(kfd, sizeof(struct cik_mqd),
			&mqd_mem_obj))
		return NULL;

	return mqd_mem_obj;
}

static void init_mqd(struct mqd_manager *mm, void **mqd,
		struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
		struct queue_properties *q)
{
	uint64_t addr;
	struct cik_mqd *m;

	m = (struct cik_mqd *) mqd_mem_obj->cpu_ptr;
	addr = mqd_mem_obj->gpu_addr;

	memset(m, 0, ALIGN(sizeof(struct cik_mqd), 256));

	m->header = 0xC0310800;
	m->compute_pipelinestat_enable = 1;
	m->compute_static_thread_mgmt_se0 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se1 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se2 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se3 = 0xFFFFFFFF;

	/*
	 * Make sure to use the last queue state saved on mqd when the cp
	 * reassigns the queue, so when queue is switched on/off (e.g over
	 * subscription or quantum timeout) the context will be consistent
	 */
	m->cp_hqd_persistent_state =
				DEFAULT_CP_HQD_PERSISTENT_STATE | PRELOAD_REQ;

	m->cp_mqd_control             = MQD_CONTROL_PRIV_STATE_EN;
	m->cp_mqd_base_addr_lo        = lower_32_bits(addr);
	m->cp_mqd_base_addr_hi        = upper_32_bits(addr);

	m->cp_hqd_quantum = QUANTUM_EN | QUANTUM_SCALE_1MS |
				QUANTUM_DURATION(10);

	/*
	 * Pipe Priority
	 * Identifies the pipe relative priority when this queue is connected
	 * to the pipeline. The pipe priority is against the GFX pipe and HP3D.
	 * In KFD we are using a fixed pipe priority set to CS_MEDIUM.
	 * 0 = CS_LOW (typically below GFX)
	 * 1 = CS_MEDIUM (typically between HP3D and GFX
	 * 2 = CS_HIGH (typically above HP3D)
	 */
	set_priority(m, q);

	if (q->format == KFD_QUEUE_FORMAT_AQL)
		m->cp_hqd_iq_rptr = AQL_ENABLE;

	*mqd = m;
	if (gart_addr)
		*gart_addr = addr;
	mm->update_mqd(mm, m, q, NULL);
}

static void init_mqd_sdma(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	struct cik_sdma_rlc_registers *m;

	m = (struct cik_sdma_rlc_registers *) mqd_mem_obj->cpu_ptr;

	memset(m, 0, sizeof(struct cik_sdma_rlc_registers));

	*mqd = m;
	if (gart_addr)
		*gart_addr = mqd_mem_obj->gpu_addr;

	mm->update_mqd(mm, m, q, NULL);
}

static int load_mqd(struct mqd_manager *mm, void *mqd, uint32_t pipe_id,
		    uint32_t queue_id, struct queue_properties *p,
		    struct mm_struct *mms)
{
	/* AQL write pointer counts in 64B packets, PM4/CP counts in dwords. */
	uint32_t wptr_shift = (p->format == KFD_QUEUE_FORMAT_AQL ? 4 : 0);
	uint32_t wptr_mask = (uint32_t)((p->queue_size / 4) - 1);

	return mm->dev->kfd2kgd->hqd_load(mm->dev->adev, mqd, pipe_id, queue_id,
					  (uint32_t __user *)p->write_ptr,
					  wptr_shift, wptr_mask, mms, 0);
}

static void __update_mqd(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q, struct mqd_update_info *minfo,
			unsigned int atc_bit)
{
	struct cik_mqd *m;

	m = get_mqd(mqd);
	m->cp_hqd_pq_control = DEFAULT_RPTR_BLOCK_SIZE |
				DEFAULT_MIN_AVAIL_SIZE;
	m->cp_hqd_ib_control = DEFAULT_MIN_IB_AVAIL_SIZE;
	if (atc_bit) {
		m->cp_hqd_pq_control |= PQ_ATC_EN;
		m->cp_hqd_ib_control |= IB_ATC_EN;
	}

	/*
	 * Calculating queue size which is log base 2 of actual queue size -1
	 * dwords and another -1 for ffs
	 */
	m->cp_hqd_pq_control |= order_base_2(q->queue_size / 4) - 1;
	m->cp_hqd_pq_base_lo = lower_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_base_hi = upper_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_rptr_report_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_rptr_report_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_doorbell_control = DOORBELL_OFFSET(q->doorbell_off);

	m->cp_hqd_vmid = q->vmid;

	if (q->format == KFD_QUEUE_FORMAT_AQL)
		m->cp_hqd_pq_control |= NO_UPDATE_RPTR;

	update_cu_mask(mm, mqd, minfo);
	set_priority(m, q);

	q->is_active = QUEUE_IS_ACTIVE(*q);
}

static bool check_preemption_failed(struct mqd_manager *mm, void *mqd)
{
	struct cik_mqd *m = (struct cik_mqd *)mqd;

	return kfd_check_hiq_mqd_doorbell_id(mm->dev, m->queue_doorbell_id0, 0);
}

static void update_mqd(struct mqd_manager *mm, void *mqd,
		       struct queue_properties *q,
		       struct mqd_update_info *minfo)
{
	__update_mqd(mm, mqd, q, minfo, 0);
}

static void update_mqd_sdma(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q,
			struct mqd_update_info *minfo)
{
	struct cik_sdma_rlc_registers *m;

	m = get_sdma_mqd(mqd);
	m->sdma_rlc_rb_cntl = order_base_2(q->queue_size / 4)
			<< SDMA0_RLC0_RB_CNTL__RB_SIZE__SHIFT |
			q->vmid << SDMA0_RLC0_RB_CNTL__RB_VMID__SHIFT |
			1 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT |
			6 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT;

	m->sdma_rlc_rb_base = lower_32_bits(q->queue_address >> 8);
	m->sdma_rlc_rb_base_hi = upper_32_bits(q->queue_address >> 8);
	m->sdma_rlc_rb_rptr_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->sdma_rlc_rb_rptr_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->sdma_rlc_doorbell =
		q->doorbell_off << SDMA0_RLC0_DOORBELL__OFFSET__SHIFT;

	m->sdma_rlc_virtual_addr = q->sdma_vm_addr;

	m->sdma_engine_id = q->sdma_engine_id;
	m->sdma_queue_id = q->sdma_queue_id;

	q->is_active = QUEUE_IS_ACTIVE(*q);
}

static void checkpoint_mqd(struct mqd_manager *mm, void *mqd, void *mqd_dst, void *ctl_stack_dst)
{
	struct cik_mqd *m;

	m = get_mqd(mqd);

	memcpy(mqd_dst, m, sizeof(struct cik_mqd));
}

static void restore_mqd(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *qp,
			const void *mqd_src,
			const void *ctl_stack_src, const u32 ctl_stack_size)
{
	uint64_t addr;
	struct cik_mqd *m;

	m = (struct cik_mqd *) mqd_mem_obj->cpu_ptr;
	addr = mqd_mem_obj->gpu_addr;

	memcpy(m, mqd_src, sizeof(*m));

	*mqd = m;
	if (gart_addr)
		*gart_addr = addr;

	m->cp_hqd_pq_doorbell_control = DOORBELL_OFFSET(qp->doorbell_off);

	pr_debug("cp_hqd_pq_doorbell_control 0x%x\n",
			m->cp_hqd_pq_doorbell_control);

	qp->is_active = 0;
}

static void checkpoint_mqd_sdma(struct mqd_manager *mm,
				void *mqd,
				void *mqd_dst,
				void *ctl_stack_dst)
{
	struct cik_sdma_rlc_registers *m;

	m = get_sdma_mqd(mqd);

	memcpy(mqd_dst, m, sizeof(struct cik_sdma_rlc_registers));
}

static void restore_mqd_sdma(struct mqd_manager *mm, void **mqd,
				struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
				struct queue_properties *qp,
				const void *mqd_src,
				const void *ctl_stack_src, const u32 ctl_stack_size)
{
	uint64_t addr;
	struct cik_sdma_rlc_registers *m;

	m = (struct cik_sdma_rlc_registers *) mqd_mem_obj->cpu_ptr;
	addr = mqd_mem_obj->gpu_addr;

	memcpy(m, mqd_src, sizeof(*m));

	m->sdma_rlc_doorbell =
		qp->doorbell_off << SDMA0_RLC0_DOORBELL__OFFSET__SHIFT;

	*mqd = m;
	if (gart_addr)
		*gart_addr = addr;

	qp->is_active = 0;
}

/*
 * HIQ MQD Implementation, concrete implementation for HIQ MQD implementation.
 * The HIQ queue in Kaveri is using the same MQD structure as all the user mode
 * queues but with different initial values.
 */

static void init_mqd_hiq(struct mqd_manager *mm, void **mqd,
		struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
		struct queue_properties *q)
{
	init_mqd(mm, mqd, mqd_mem_obj, gart_addr, q);
}

static void update_mqd_hiq(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q,
			struct mqd_update_info *minfo)
{
	struct cik_mqd *m;

	m = get_mqd(mqd);
	m->cp_hqd_pq_control = DEFAULT_RPTR_BLOCK_SIZE |
				DEFAULT_MIN_AVAIL_SIZE |
				PRIV_STATE |
				KMD_QUEUE;

	/*
	 * Calculating queue size which is log base 2 of actual queue
	 * size -1 dwords
	 */
	m->cp_hqd_pq_control |= order_base_2(q->queue_size / 4) - 1;
	m->cp_hqd_pq_base_lo = lower_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_base_hi = upper_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_rptr_report_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_rptr_report_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_doorbell_control = DOORBELL_OFFSET(q->doorbell_off);

	m->cp_hqd_vmid = q->vmid;

	q->is_active = QUEUE_IS_ACTIVE(*q);

	set_priority(m, q);
}

#if defined(CONFIG_DEBUG_FS)

static int debugfs_show_mqd(struct seq_file *m, void *data)
{
	seq_hex_dump(m, "    ", DUMP_PREFIX_OFFSET, 32, 4,
		     data, sizeof(struct cik_mqd), false);
	return 0;
}

static int debugfs_show_mqd_sdma(struct seq_file *m, void *data)
{
	seq_hex_dump(m, "    ", DUMP_PREFIX_OFFSET, 32, 4,
		     data, sizeof(struct cik_sdma_rlc_registers), false);
	return 0;
}

#endif

struct mqd_manager *mqd_manager_init_cik(enum KFD_MQD_TYPE type,
		struct kfd_node *dev)
{
	struct mqd_manager *mqd;

	if (WARN_ON(type >= KFD_MQD_TYPE_MAX))
		return NULL;

	mqd = kzalloc(sizeof(*mqd), GFP_KERNEL);
	if (!mqd)
		return NULL;

	mqd->dev = dev;

	switch (type) {
	case KFD_MQD_TYPE_CP:
		mqd->allocate_mqd = allocate_mqd;
		mqd->init_mqd = init_mqd;
		mqd->free_mqd = kfd_free_mqd_cp;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd;
		mqd->destroy_mqd = kfd_destroy_mqd_cp;
		mqd->is_occupied = kfd_is_occupied_cp;
		mqd->checkpoint_mqd = checkpoint_mqd;
		mqd->restore_mqd = restore_mqd;
		mqd->mqd_size = sizeof(struct cik_mqd);
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		break;
	case KFD_MQD_TYPE_HIQ:
		mqd->allocate_mqd = allocate_hiq_mqd;
		mqd->init_mqd = init_mqd_hiq;
		mqd->free_mqd = free_mqd_hiq_sdma;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd_hiq;
		mqd->destroy_mqd = kfd_destroy_mqd_cp;
		mqd->is_occupied = kfd_is_occupied_cp;
		mqd->mqd_size = sizeof(struct cik_mqd);
		mqd->mqd_stride = kfd_mqd_stride;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		mqd->check_preemption_failed = check_preemption_failed;
		break;
	case KFD_MQD_TYPE_DIQ:
		mqd->allocate_mqd = allocate_mqd;
		mqd->init_mqd = init_mqd_hiq;
		mqd->free_mqd = kfd_free_mqd_cp;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd_hiq;
		mqd->destroy_mqd = kfd_destroy_mqd_cp;
		mqd->is_occupied = kfd_is_occupied_cp;
		mqd->mqd_size = sizeof(struct cik_mqd);
		mqd->mqd_stride = kfd_mqd_stride;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		break;
	case KFD_MQD_TYPE_SDMA:
		mqd->allocate_mqd = allocate_sdma_mqd;
		mqd->init_mqd = init_mqd_sdma;
		mqd->free_mqd = free_mqd_hiq_sdma;
		mqd->load_mqd = kfd_load_mqd_sdma;
		mqd->update_mqd = update_mqd_sdma;
		mqd->destroy_mqd = kfd_destroy_mqd_sdma;
		mqd->is_occupied = kfd_is_occupied_sdma;
		mqd->checkpoint_mqd = checkpoint_mqd_sdma;
		mqd->restore_mqd = restore_mqd_sdma;
		mqd->mqd_size = sizeof(struct cik_sdma_rlc_registers);
		mqd->mqd_stride = kfd_mqd_stride;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd_sdma;
#endif
		break;
	default:
		kfree(mqd);
		return NULL;
	}

	return mqd;
}
