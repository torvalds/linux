/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "cikd.h"
#include "cik_sdma.h"
#include "gfx_v7_0.h"
#include "gca/gfx_7_2_d.h"
#include "gca/gfx_7_2_enum.h"
#include "gca/gfx_7_2_sh_mask.h"
#include "oss/oss_2_0_d.h"
#include "oss/oss_2_0_sh_mask.h"
#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"
#include "cik_structs.h"

enum hqd_dequeue_request_type {
	NO_ACTION = 0,
	DRAIN_PIPE,
	RESET_WAVES
};

enum {
	MAX_TRAPID = 8,		/* 3 bits in the bitfield. */
	MAX_WATCH_ADDRESSES = 4
};

static void lock_srbm(struct amdgpu_device *adev, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	uint32_t value = PIPEID(pipe) | MEID(mec) | VMID(vmid) | QUEUEID(queue);

	mutex_lock(&adev->srbm_mutex);
	WREG32(mmSRBM_GFX_CNTL, value);
}

static void unlock_srbm(struct amdgpu_device *adev)
{
	WREG32(mmSRBM_GFX_CNTL, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void acquire_queue(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	uint32_t pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, queue_id, 0);
}

static void release_queue(struct amdgpu_device *adev)
{
	unlock_srbm(adev);
}

static void kgd_program_sh_mem_settings(struct amdgpu_device *adev, uint32_t vmid,
					uint32_t sh_mem_config,
					uint32_t sh_mem_ape1_base,
					uint32_t sh_mem_ape1_limit,
					uint32_t sh_mem_bases, uint32_t inst)
{
	lock_srbm(adev, 0, 0, 0, vmid);

	WREG32(mmSH_MEM_CONFIG, sh_mem_config);
	WREG32(mmSH_MEM_APE1_BASE, sh_mem_ape1_base);
	WREG32(mmSH_MEM_APE1_LIMIT, sh_mem_ape1_limit);
	WREG32(mmSH_MEM_BASES, sh_mem_bases);

	unlock_srbm(adev);
}

static int kgd_set_pasid_vmid_mapping(struct amdgpu_device *adev, u32 pasid,
					unsigned int vmid, uint32_t inst)
{
	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0 because
	 * a mapping is in progress or because a mapping finished and the
	 * SW cleared it. So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid |
			ATC_VMID0_PASID_MAPPING__VALID_MASK;

	WREG32(mmATC_VMID0_PASID_MAPPING + vmid, pasid_mapping);

	while (!(RREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS) & (1U << vmid)))
		cpu_relax();
	WREG32(mmATC_VMID_PASID_MAPPING_UPDATE_STATUS, 1U << vmid);

	/* Mapping vmid to pasid also for IH block */
	WREG32(mmIH_VMID_0_LUT + vmid, pasid_mapping);

	return 0;
}

static int kgd_init_interrupts(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t inst)
{
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, 0, 0);

	WREG32(mmCPC_INT_CNTL, CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK |
			CP_INT_CNTL_RING0__OPCODE_ERROR_INT_ENABLE_MASK);

	unlock_srbm(adev);

	return 0;
}

static inline uint32_t get_sdma_rlc_reg_offset(struct cik_sdma_rlc_registers *m)
{
	uint32_t retval;

	retval = m->sdma_engine_id * SDMA1_REGISTER_OFFSET +
			m->sdma_queue_id * KFD_CIK_SDMA_QUEUE_OFFSET;

	pr_debug("RLC register offset for SDMA%d RLC%d: 0x%x\n",
			m->sdma_engine_id, m->sdma_queue_id, retval);

	return retval;
}

static inline struct cik_mqd *get_mqd(void *mqd)
{
	return (struct cik_mqd *)mqd;
}

static inline struct cik_sdma_rlc_registers *get_sdma_mqd(void *mqd)
{
	return (struct cik_sdma_rlc_registers *)mqd;
}

static int kgd_hqd_load(struct amdgpu_device *adev, void *mqd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t __user *wptr, uint32_t wptr_shift,
			uint32_t wptr_mask, struct mm_struct *mm, uint32_t inst)
{
	struct cik_mqd *m;
	uint32_t *mqd_hqd;
	uint32_t reg, wptr_val, data;
	bool valid_wptr = false;

	m = get_mqd(mqd);

	acquire_queue(adev, pipe_id, queue_id);

	/* HQD registers extend from CP_MQD_BASE_ADDR to CP_MQD_CONTROL. */
	mqd_hqd = &m->cp_mqd_base_addr_lo;

	for (reg = mmCP_MQD_BASE_ADDR; reg <= mmCP_MQD_CONTROL; reg++)
		WREG32(reg, mqd_hqd[reg - mmCP_MQD_BASE_ADDR]);

	/* Copy userspace write pointer value to register.
	 * Activate doorbell logic to monitor subsequent changes.
	 */
	data = REG_SET_FIELD(m->cp_hqd_pq_doorbell_control,
			     CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
	WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, data);

	/* read_user_ptr may take the mm->mmap_lock.
	 * release srbm_mutex to avoid circular dependency between
	 * srbm_mutex->mmap_lock->reservation_ww_class_mutex->srbm_mutex.
	 */
	release_queue(adev);
	valid_wptr = read_user_wptr(mm, wptr, wptr_val);
	acquire_queue(adev, pipe_id, queue_id);
	if (valid_wptr)
		WREG32(mmCP_HQD_PQ_WPTR, (wptr_val << wptr_shift) & wptr_mask);

	data = REG_SET_FIELD(m->cp_hqd_active, CP_HQD_ACTIVE, ACTIVE, 1);
	WREG32(mmCP_HQD_ACTIVE, data);

	release_queue(adev);

	return 0;
}

static int kgd_hqd_dump(struct amdgpu_device *adev,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs, uint32_t inst)
{
	uint32_t i = 0, reg;
#define HQD_N_REGS (35+4)
#define DUMP_REG(addr) do {				\
		if (WARN_ON_ONCE(i >= HQD_N_REGS))	\
			break;				\
		(*dump)[i][0] = (addr) << 2;		\
		(*dump)[i++][1] = RREG32(addr);		\
	} while (0)

	*dump = kmalloc_array(HQD_N_REGS, sizeof(**dump), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	acquire_queue(adev, pipe_id, queue_id);

	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE0);
	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE1);
	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE2);
	DUMP_REG(mmCOMPUTE_STATIC_THREAD_MGMT_SE3);

	for (reg = mmCP_MQD_BASE_ADDR; reg <= mmCP_MQD_CONTROL; reg++)
		DUMP_REG(reg);

	release_queue(adev);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int kgd_hqd_sdma_load(struct amdgpu_device *adev, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm)
{
	struct cik_sdma_rlc_registers *m;
	unsigned long end_jiffies;
	uint32_t sdma_rlc_reg_offset;
	uint32_t data;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(m);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL,
		m->sdma_rlc_rb_cntl & (~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK));

	end_jiffies = msecs_to_jiffies(2000) + jiffies;
	while (true) {
		data = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (data & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	data = REG_SET_FIELD(m->sdma_rlc_doorbell, SDMA0_RLC0_DOORBELL,
			     ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_DOORBELL, data);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR,
				m->sdma_rlc_rb_rptr);

	if (read_user_wptr(mm, wptr, data))
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR, data);
	else
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR,
		       m->sdma_rlc_rb_rptr);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_VIRTUAL_ADDR,
				m->sdma_rlc_virtual_addr);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_BASE, m->sdma_rlc_rb_base);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_BASE_HI,
			m->sdma_rlc_rb_base_hi);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_ADDR_LO,
			m->sdma_rlc_rb_rptr_addr_lo);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_ADDR_HI,
			m->sdma_rlc_rb_rptr_addr_hi);

	data = REG_SET_FIELD(m->sdma_rlc_rb_cntl, SDMA0_RLC0_RB_CNTL,
			     RB_ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL, data);

	return 0;
}

static int kgd_hqd_sdma_dump(struct amdgpu_device *adev,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	uint32_t sdma_offset = engine_id * SDMA1_REGISTER_OFFSET +
		queue_id * KFD_CIK_SDMA_QUEUE_OFFSET;
	uint32_t i = 0, reg;
#undef HQD_N_REGS
#define HQD_N_REGS (19+4)

	*dump = kmalloc_array(HQD_N_REGS, sizeof(**dump), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = mmSDMA0_RLC0_RB_CNTL; reg <= mmSDMA0_RLC0_DOORBELL; reg++)
		DUMP_REG(sdma_offset + reg);
	for (reg = mmSDMA0_RLC0_VIRTUAL_ADDR; reg <= mmSDMA0_RLC0_WATERMARK;
	     reg++)
		DUMP_REG(sdma_offset + reg);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static bool kgd_hqd_is_occupied(struct amdgpu_device *adev,
				uint64_t queue_address, uint32_t pipe_id,
				uint32_t queue_id, uint32_t inst)
{
	uint32_t act;
	bool retval = false;
	uint32_t low, high;

	acquire_queue(adev, pipe_id, queue_id);
	act = RREG32(mmCP_HQD_ACTIVE);
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == RREG32(mmCP_HQD_PQ_BASE) &&
				high == RREG32(mmCP_HQD_PQ_BASE_HI))
			retval = true;
	}
	release_queue(adev);
	return retval;
}

static bool kgd_hqd_sdma_is_occupied(struct amdgpu_device *adev, void *mqd)
{
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(m);

	sdma_rlc_rb_cntl = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL);

	if (sdma_rlc_rb_cntl & SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK)
		return true;

	return false;
}

static int kgd_hqd_destroy(struct amdgpu_device *adev, void *mqd,
				enum kfd_preempt_type reset_type,
				unsigned int utimeout, uint32_t pipe_id,
				uint32_t queue_id, uint32_t inst)
{
	uint32_t temp;
	enum hqd_dequeue_request_type type;
	unsigned long flags, end_jiffies;
	int retry;

	if (amdgpu_in_reset(adev))
		return -EIO;

	acquire_queue(adev, pipe_id, queue_id);
	WREG32(mmCP_HQD_PQ_DOORBELL_CONTROL, 0);

	switch (reset_type) {
	case KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN:
		type = DRAIN_PIPE;
		break;
	case KFD_PREEMPT_TYPE_WAVEFRONT_RESET:
		type = RESET_WAVES;
		break;
	default:
		type = DRAIN_PIPE;
		break;
	}

	/* Workaround: If IQ timer is active and the wait time is close to or
	 * equal to 0, dequeueing is not safe. Wait until either the wait time
	 * is larger or timer is cleared. Also, ensure that IQ_REQ_PEND is
	 * cleared before continuing. Also, ensure wait times are set to at
	 * least 0x3.
	 */
	local_irq_save(flags);
	preempt_disable();
	retry = 5000; /* wait for 500 usecs at maximum */
	while (true) {
		temp = RREG32(mmCP_HQD_IQ_TIMER);
		if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, PROCESSING_IQ)) {
			pr_debug("HW is processing IQ\n");
			goto loop;
		}
		if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, ACTIVE)) {
			if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, RETRY_TYPE)
					== 3) /* SEM-rearm is safe */
				break;
			/* Wait time 3 is safe for CP, but our MMIO read/write
			 * time is close to 1 microsecond, so check for 10 to
			 * leave more buffer room
			 */
			if (REG_GET_FIELD(temp, CP_HQD_IQ_TIMER, WAIT_TIME)
					>= 10)
				break;
			pr_debug("IQ timer is active\n");
		} else
			break;
loop:
		if (!retry) {
			pr_err("CP HQD IQ timer status time out\n");
			break;
		}
		ndelay(100);
		--retry;
	}
	retry = 1000;
	while (true) {
		temp = RREG32(mmCP_HQD_DEQUEUE_REQUEST);
		if (!(temp & CP_HQD_DEQUEUE_REQUEST__IQ_REQ_PEND_MASK))
			break;
		pr_debug("Dequeue request is pending\n");

		if (!retry) {
			pr_err("CP HQD dequeue request time out\n");
			break;
		}
		ndelay(100);
		--retry;
	}
	local_irq_restore(flags);
	preempt_enable();

	WREG32(mmCP_HQD_DEQUEUE_REQUEST, type);

	end_jiffies = (utimeout * HZ / 1000) + jiffies;
	while (true) {
		temp = RREG32(mmCP_HQD_ACTIVE);
		if (!(temp & CP_HQD_ACTIVE__ACTIVE_MASK))
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("cp queue preemption time out\n");
			release_queue(adev);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	release_queue(adev);
	return 0;
}

static int kgd_hqd_sdma_destroy(struct amdgpu_device *adev, void *mqd,
				unsigned int utimeout)
{
	struct cik_sdma_rlc_registers *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t temp;
	unsigned long end_jiffies = (utimeout * HZ / 1000) + jiffies;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(m);

	temp = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL);
	temp = temp & ~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK;
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL, temp);

	while (true) {
		temp = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_CONTEXT_STATUS);
		if (temp & SDMA0_RLC0_CONTEXT_STATUS__IDLE_MASK)
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("SDMA RLC not idle in %s\n", __func__);
			return -ETIME;
		}
		usleep_range(500, 1000);
	}

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_DOORBELL, 0);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL,
		RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL) |
		SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK);

	m->sdma_rlc_rb_rptr = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR);

	return 0;
}

static int kgd_wave_control_execute(struct amdgpu_device *adev,
					uint32_t gfx_index_val,
					uint32_t sq_cmd, uint32_t inst)
{
	uint32_t data;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32(mmGRBM_GFX_INDEX, gfx_index_val);
	WREG32(mmSQ_CMD, sq_cmd);

	/*  Restore the GRBM_GFX_INDEX register  */

	data = GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK |
		GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK |
		GRBM_GFX_INDEX__SE_BROADCAST_WRITES_MASK;

	WREG32(mmGRBM_GFX_INDEX, data);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

static bool get_atc_vmid_pasid_mapping_info(struct amdgpu_device *adev,
					uint8_t vmid, uint16_t *p_pasid)
{
	uint32_t value;

	value = RREG32(mmATC_VMID0_PASID_MAPPING + vmid);
	*p_pasid = value & ATC_VMID0_PASID_MAPPING__PASID_MASK;

	return !!(value & ATC_VMID0_PASID_MAPPING__VALID_MASK);
}

static void set_scratch_backing_va(struct amdgpu_device *adev,
					uint64_t va, uint32_t vmid)
{
	lock_srbm(adev, 0, 0, 0, vmid);
	WREG32(mmSH_HIDDEN_PRIVATE_BASE_VMID, va);
	unlock_srbm(adev);
}

static void set_vm_context_page_table_base(struct amdgpu_device *adev,
			uint32_t vmid, uint64_t page_table_base)
{
	if (!amdgpu_amdkfd_is_kfd_vmid(adev, vmid)) {
		pr_err("trying to set page table base for wrong VMID\n");
		return;
	}
	WREG32(mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vmid - 8,
		lower_32_bits(page_table_base));
}

 /**
  * read_vmid_from_vmfault_reg - read vmid from register
  *
  * adev: amdgpu_device pointer
  * @vmid: vmid pointer
  * read vmid from register (CIK).
  */
static uint32_t read_vmid_from_vmfault_reg(struct amdgpu_device *adev)
{
	uint32_t status = RREG32(mmVM_CONTEXT1_PROTECTION_FAULT_STATUS);

	return REG_GET_FIELD(status, VM_CONTEXT1_PROTECTION_FAULT_STATUS, VMID);
}

const struct kfd2kgd_calls gfx_v7_kfd2kgd = {
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_interrupts = kgd_init_interrupts,
	.hqd_load = kgd_hqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_dump = kgd_hqd_dump,
	.hqd_sdma_dump = kgd_hqd_sdma_dump,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.wave_control_execute = kgd_wave_control_execute,
	.get_atc_vmid_pasid_mapping_info = get_atc_vmid_pasid_mapping_info,
	.set_scratch_backing_va = set_scratch_backing_va,
	.set_vm_context_page_table_base = set_vm_context_page_table_base,
	.read_vmid_from_vmfault_reg = read_vmid_from_vmfault_reg,
};
