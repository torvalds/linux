/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include "amdgpu_amdkfd_gfx_v10.h"
#include "gc/gc_10_1_0_offset.h"
#include "gc/gc_10_1_0_sh_mask.h"
#include "athub/athub_2_0_0_offset.h"
#include "athub/athub_2_0_0_sh_mask.h"
#include "oss/osssys_5_0_0_offset.h"
#include "oss/osssys_5_0_0_sh_mask.h"
#include "soc15_common.h"
#include "v10_structs.h"
#include "nv.h"
#include "nvd.h"
#include <uapi/linux/kfd_ioctl.h>

enum hqd_dequeue_request_type {
	NO_ACTION = 0,
	DRAIN_PIPE,
	RESET_WAVES,
	SAVE_WAVES
};

static void lock_srbm(struct amdgpu_device *adev, uint32_t mec, uint32_t pipe,
			uint32_t queue, uint32_t vmid)
{
	mutex_lock(&adev->srbm_mutex);
	nv_grbm_select(adev, mec, pipe, queue, vmid);
}

static void unlock_srbm(struct amdgpu_device *adev)
{
	nv_grbm_select(adev, 0, 0, 0, 0);
	mutex_unlock(&adev->srbm_mutex);
}

static void acquire_queue(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t queue_id)
{
	uint32_t mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	uint32_t pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, queue_id, 0);
}

static uint64_t get_queue_mask(struct amdgpu_device *adev,
			       uint32_t pipe_id, uint32_t queue_id)
{
	unsigned int bit = pipe_id * adev->gfx.mec.num_queue_per_pipe +
			queue_id;

	return 1ull << bit;
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

	WREG32_SOC15(GC, 0, mmSH_MEM_CONFIG, sh_mem_config);
	WREG32_SOC15(GC, 0, mmSH_MEM_BASES, sh_mem_bases);
	/* APE1 no longer exists on GFX9 */

	unlock_srbm(adev);
}

static int kgd_set_pasid_vmid_mapping(struct amdgpu_device *adev, u32 pasid,
					unsigned int vmid, uint32_t inst)
{
	/*
	 * We have to assume that there is no outstanding mapping.
	 * The ATC_VMID_PASID_MAPPING_UPDATE_STATUS bit could be 0 because
	 * a mapping is in progress or because a mapping finished
	 * and the SW cleared it.
	 * So the protocol is to always wait & clear.
	 */
	uint32_t pasid_mapping = (pasid == 0) ? 0 : (uint32_t)pasid |
			ATC_VMID0_PASID_MAPPING__VALID_MASK;

	pr_debug("pasid 0x%x vmid %d, reg value %x\n", pasid, vmid, pasid_mapping);

	pr_debug("ATHUB, reg %x\n", SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING) + vmid);
	WREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING) + vmid,
	       pasid_mapping);

#if 0
	/* TODO: uncomment this code when the hardware support is ready. */
	while (!(RREG32(SOC15_REG_OFFSET(
				ATHUB, 0,
				mmATC_VMID_PASID_MAPPING_UPDATE_STATUS)) &
		 (1U << vmid)))
		cpu_relax();

	pr_debug("ATHUB mapping update finished\n");
	WREG32(SOC15_REG_OFFSET(ATHUB, 0,
				mmATC_VMID_PASID_MAPPING_UPDATE_STATUS),
	       1U << vmid);
#endif

	/* Mapping vmid to pasid also for IH block */
	pr_debug("update mapping for IH block and mmhub");
	WREG32(SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT) + vmid,
	       pasid_mapping);

	return 0;
}

/* TODO - RING0 form of field is obsolete, seems to date back to SI
 * but still works
 */

static int kgd_init_interrupts(struct amdgpu_device *adev, uint32_t pipe_id,
				uint32_t inst)
{
	uint32_t mec;
	uint32_t pipe;

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	lock_srbm(adev, mec, pipe, 0, 0);

	WREG32_SOC15(GC, 0, mmCPC_INT_CNTL,
		CP_INT_CNTL_RING0__TIME_STAMP_INT_ENABLE_MASK |
		CP_INT_CNTL_RING0__OPCODE_ERROR_INT_ENABLE_MASK);

	unlock_srbm(adev);

	return 0;
}

static uint32_t get_sdma_rlc_reg_offset(struct amdgpu_device *adev,
				unsigned int engine_id,
				unsigned int queue_id)
{
	uint32_t sdma_engine_reg_base[2] = {
		SOC15_REG_OFFSET(SDMA0, 0,
				 mmSDMA0_RLC0_RB_CNTL) - mmSDMA0_RLC0_RB_CNTL,
		/* On gfx10, mmSDMA1_xxx registers are defined NOT based
		 * on SDMA1 base address (dw 0x1860) but based on SDMA0
		 * base address (dw 0x1260). Therefore use mmSDMA0_RLC0_RB_CNTL
		 * instead of mmSDMA1_RLC0_RB_CNTL for the base address calc
		 * below
		 */
		SOC15_REG_OFFSET(SDMA1, 0,
				 mmSDMA1_RLC0_RB_CNTL) - mmSDMA0_RLC0_RB_CNTL
	};

	uint32_t retval = sdma_engine_reg_base[engine_id]
		+ queue_id * (mmSDMA0_RLC1_RB_CNTL - mmSDMA0_RLC0_RB_CNTL);

	pr_debug("RLC register offset for SDMA%d RLC%d: 0x%x\n", engine_id,
			queue_id, retval);

	return retval;
}

#if 0
static uint32_t get_watch_base_addr(struct amdgpu_device *adev)
{
	uint32_t retval = SOC15_REG_OFFSET(GC, 0, mmTCP_WATCH0_ADDR_H) -
			mmTCP_WATCH0_ADDR_H;

	pr_debug("kfd: reg watch base address: 0x%x\n", retval);

	return retval;
}
#endif

static inline struct v10_compute_mqd *get_mqd(void *mqd)
{
	return (struct v10_compute_mqd *)mqd;
}

static inline struct v10_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct v10_sdma_mqd *)mqd;
}

static int kgd_hqd_load(struct amdgpu_device *adev, void *mqd,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t __user *wptr, uint32_t wptr_shift,
			uint32_t wptr_mask, struct mm_struct *mm, uint32_t inst)
{
	struct v10_compute_mqd *m;
	uint32_t *mqd_hqd;
	uint32_t reg, hqd_base, data;

	m = get_mqd(mqd);

	pr_debug("Load hqd of pipe %d queue %d\n", pipe_id, queue_id);
	acquire_queue(adev, pipe_id, queue_id);

	/* HQD registers extend from CP_MQD_BASE_ADDR to CP_HQD_EOP_WPTR_MEM. */
	mqd_hqd = &m->cp_mqd_base_addr_lo;
	hqd_base = SOC15_REG_OFFSET(GC, 0, mmCP_MQD_BASE_ADDR);

	for (reg = hqd_base;
	     reg <= SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_HI); reg++)
		WREG32_SOC15_IP(GC, reg, mqd_hqd[reg - hqd_base]);


	/* Activate doorbell logic before triggering WPTR poll. */
	data = REG_SET_FIELD(m->cp_hqd_pq_doorbell_control,
			     CP_HQD_PQ_DOORBELL_CONTROL, DOORBELL_EN, 1);
	WREG32_SOC15(GC, 0, mmCP_HQD_PQ_DOORBELL_CONTROL, data);

	if (wptr) {
		/* Don't read wptr with get_user because the user
		 * context may not be accessible (if this function
		 * runs in a work queue). Instead trigger a one-shot
		 * polling read from memory in the CP. This assumes
		 * that wptr is GPU-accessible in the queue's VMID via
		 * ATC or SVM. WPTR==RPTR before starting the poll so
		 * the CP starts fetching new commands from the right
		 * place.
		 *
		 * Guessing a 64-bit WPTR from a 32-bit RPTR is a bit
		 * tricky. Assume that the queue didn't overflow. The
		 * number of valid bits in the 32-bit RPTR depends on
		 * the queue size. The remaining bits are taken from
		 * the saved 64-bit WPTR. If the WPTR wrapped, add the
		 * queue size.
		 */
		uint32_t queue_size =
			2 << REG_GET_FIELD(m->cp_hqd_pq_control,
					   CP_HQD_PQ_CONTROL, QUEUE_SIZE);
		uint64_t guessed_wptr = m->cp_hqd_pq_rptr & (queue_size - 1);

		if ((m->cp_hqd_pq_wptr_lo & (queue_size - 1)) < guessed_wptr)
			guessed_wptr += queue_size;
		guessed_wptr += m->cp_hqd_pq_wptr_lo & ~(queue_size - 1);
		guessed_wptr += (uint64_t)m->cp_hqd_pq_wptr_hi << 32;

		WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_LO,
		       lower_32_bits(guessed_wptr));
		WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_HI,
		       upper_32_bits(guessed_wptr));
		WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR,
		       lower_32_bits((uint64_t)wptr));
		WREG32_SOC15(GC, 0, mmCP_HQD_PQ_WPTR_POLL_ADDR_HI,
		       upper_32_bits((uint64_t)wptr));
		pr_debug("%s setting CP_PQ_WPTR_POLL_CNTL1 to %x\n", __func__,
			 (uint32_t)get_queue_mask(adev, pipe_id, queue_id));
		WREG32_SOC15(GC, 0, mmCP_PQ_WPTR_POLL_CNTL1,
		       (uint32_t)get_queue_mask(adev, pipe_id, queue_id));
	}

	/* Start the EOP fetcher */
	WREG32_SOC15(GC, 0, mmCP_HQD_EOP_RPTR,
	       REG_SET_FIELD(m->cp_hqd_eop_rptr,
			     CP_HQD_EOP_RPTR, INIT_FETCHER, 1));

	data = REG_SET_FIELD(m->cp_hqd_active, CP_HQD_ACTIVE, ACTIVE, 1);
	WREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE, data);

	release_queue(adev);

	return 0;
}

static int kgd_hiq_mqd_load(struct amdgpu_device *adev, void *mqd,
			    uint32_t pipe_id, uint32_t queue_id,
			    uint32_t doorbell_off, uint32_t inst)
{
	struct amdgpu_ring *kiq_ring = &adev->gfx.kiq[0].ring;
	struct v10_compute_mqd *m;
	uint32_t mec, pipe;
	int r;

	m = get_mqd(mqd);

	acquire_queue(adev, pipe_id, queue_id);

	mec = (pipe_id / adev->gfx.mec.num_pipe_per_mec) + 1;
	pipe = (pipe_id % adev->gfx.mec.num_pipe_per_mec);

	pr_debug("kfd: set HIQ, mec:%d, pipe:%d, queue:%d.\n",
		 mec, pipe, queue_id);

	spin_lock(&adev->gfx.kiq[0].ring_lock);
	r = amdgpu_ring_alloc(kiq_ring, 7);
	if (r) {
		pr_err("Failed to alloc KIQ (%d).\n", r);
		goto out_unlock;
	}

	amdgpu_ring_write(kiq_ring, PACKET3(PACKET3_MAP_QUEUES, 5));
	amdgpu_ring_write(kiq_ring,
			  PACKET3_MAP_QUEUES_QUEUE_SEL(0) | /* Queue_Sel */
			  PACKET3_MAP_QUEUES_VMID(m->cp_hqd_vmid) | /* VMID */
			  PACKET3_MAP_QUEUES_QUEUE(queue_id) |
			  PACKET3_MAP_QUEUES_PIPE(pipe) |
			  PACKET3_MAP_QUEUES_ME((mec - 1)) |
			  PACKET3_MAP_QUEUES_QUEUE_TYPE(0) | /*queue_type: normal compute queue */
			  PACKET3_MAP_QUEUES_ALLOC_FORMAT(0) | /* alloc format: all_on_one_pipe */
			  PACKET3_MAP_QUEUES_ENGINE_SEL(1) | /* engine_sel: hiq */
			  PACKET3_MAP_QUEUES_NUM_QUEUES(1)); /* num_queues: must be 1 */
	amdgpu_ring_write(kiq_ring,
			  PACKET3_MAP_QUEUES_DOORBELL_OFFSET(doorbell_off));
	amdgpu_ring_write(kiq_ring, m->cp_mqd_base_addr_lo);
	amdgpu_ring_write(kiq_ring, m->cp_mqd_base_addr_hi);
	amdgpu_ring_write(kiq_ring, m->cp_hqd_pq_wptr_poll_addr_lo);
	amdgpu_ring_write(kiq_ring, m->cp_hqd_pq_wptr_poll_addr_hi);
	amdgpu_ring_commit(kiq_ring);

out_unlock:
	spin_unlock(&adev->gfx.kiq[0].ring_lock);
	release_queue(adev);

	return r;
}

static int kgd_hqd_dump(struct amdgpu_device *adev,
			uint32_t pipe_id, uint32_t queue_id,
			uint32_t (**dump)[2], uint32_t *n_regs, uint32_t inst)
{
	uint32_t i = 0, reg;
#define HQD_N_REGS 56
#define DUMP_REG(addr) do {				\
		if (WARN_ON_ONCE(i >= HQD_N_REGS))	\
			break;				\
		(*dump)[i][0] = (addr) << 2;		\
		(*dump)[i++][1] = RREG32_SOC15_IP(GC, addr);		\
	} while (0)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	acquire_queue(adev, pipe_id, queue_id);

	for (reg = SOC15_REG_OFFSET(GC, 0, mmCP_MQD_BASE_ADDR);
	     reg <= SOC15_REG_OFFSET(GC, 0, mmCP_HQD_PQ_WPTR_HI); reg++)
		DUMP_REG(reg);

	release_queue(adev);

	WARN_ON_ONCE(i != HQD_N_REGS);
	*n_regs = i;

	return 0;
}

static int kgd_hqd_sdma_load(struct amdgpu_device *adev, void *mqd,
			     uint32_t __user *wptr, struct mm_struct *mm)
{
	struct v10_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	unsigned long end_jiffies;
	uint32_t data;
	uint64_t data64;
	uint64_t __user *wptr64 = (uint64_t __user *)wptr;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL,
		m->sdmax_rlcx_rb_cntl & (~SDMA0_RLC0_RB_CNTL__RB_ENABLE_MASK));

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

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_DOORBELL_OFFSET,
	       m->sdmax_rlcx_doorbell_offset);

	data = REG_SET_FIELD(m->sdmax_rlcx_doorbell, SDMA0_RLC0_DOORBELL,
			     ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_DOORBELL, data);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR,
				m->sdmax_rlcx_rb_rptr);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_HI,
				m->sdmax_rlcx_rb_rptr_hi);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_MINOR_PTR_UPDATE, 1);
	if (read_user_wptr(mm, wptr64, data64)) {
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR,
		       lower_32_bits(data64));
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR_HI,
		       upper_32_bits(data64));
	} else {
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR,
		       m->sdmax_rlcx_rb_rptr);
		WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_WPTR_HI,
		       m->sdmax_rlcx_rb_rptr_hi);
	}
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_MINOR_PTR_UPDATE, 0);

	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_BASE, m->sdmax_rlcx_rb_base);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_BASE_HI,
			m->sdmax_rlcx_rb_base_hi);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_ADDR_LO,
			m->sdmax_rlcx_rb_rptr_addr_lo);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_ADDR_HI,
			m->sdmax_rlcx_rb_rptr_addr_hi);

	data = REG_SET_FIELD(m->sdmax_rlcx_rb_cntl, SDMA0_RLC0_RB_CNTL,
			     RB_ENABLE, 1);
	WREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_CNTL, data);

	return 0;
}

static int kgd_hqd_sdma_dump(struct amdgpu_device *adev,
			     uint32_t engine_id, uint32_t queue_id,
			     uint32_t (**dump)[2], uint32_t *n_regs)
{
	uint32_t sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev,
			engine_id, queue_id);
	uint32_t i = 0, reg;
#undef HQD_N_REGS
#define HQD_N_REGS (19+6+7+10)

	*dump = kmalloc(HQD_N_REGS*2*sizeof(uint32_t), GFP_KERNEL);
	if (*dump == NULL)
		return -ENOMEM;

	for (reg = mmSDMA0_RLC0_RB_CNTL; reg <= mmSDMA0_RLC0_DOORBELL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = mmSDMA0_RLC0_STATUS; reg <= mmSDMA0_RLC0_CSA_ADDR_HI; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = mmSDMA0_RLC0_IB_SUB_REMAIN;
	     reg <= mmSDMA0_RLC0_MINOR_PTR_UPDATE; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);
	for (reg = mmSDMA0_RLC0_MIDCMD_DATA0;
	     reg <= mmSDMA0_RLC0_MIDCMD_CNTL; reg++)
		DUMP_REG(sdma_rlc_reg_offset + reg);

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
	act = RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE);
	if (act) {
		low = lower_32_bits(queue_address >> 8);
		high = upper_32_bits(queue_address >> 8);

		if (low == RREG32_SOC15(GC, 0, mmCP_HQD_PQ_BASE) &&
		   high == RREG32_SOC15(GC, 0, mmCP_HQD_PQ_BASE_HI))
			retval = true;
	}
	release_queue(adev);
	return retval;
}

static bool kgd_hqd_sdma_is_occupied(struct amdgpu_device *adev, void *mqd)
{
	struct v10_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t sdma_rlc_rb_cntl;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

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
	enum hqd_dequeue_request_type type;
	unsigned long end_jiffies;
	uint32_t temp;
	struct v10_compute_mqd *m = get_mqd(mqd);

	if (amdgpu_in_reset(adev))
		return -EIO;

#if 0
	unsigned long flags;
	int retry;
#endif

	acquire_queue(adev, pipe_id, queue_id);

	if (m->cp_hqd_vmid == 0)
		WREG32_FIELD15(GC, 0, RLC_CP_SCHEDULERS, scheduler1, 0);

	switch (reset_type) {
	case KFD_PREEMPT_TYPE_WAVEFRONT_DRAIN:
		type = DRAIN_PIPE;
		break;
	case KFD_PREEMPT_TYPE_WAVEFRONT_RESET:
		type = RESET_WAVES;
		break;
	case KFD_PREEMPT_TYPE_WAVEFRONT_SAVE:
		type = SAVE_WAVES;
		break;
	default:
		type = DRAIN_PIPE;
		break;
	}

#if 0 /* Is this still needed? */
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
#endif

	WREG32_SOC15(GC, 0, mmCP_HQD_DEQUEUE_REQUEST, type);

	end_jiffies = (utimeout * HZ / 1000) + jiffies;
	while (true) {
		temp = RREG32_SOC15(GC, 0, mmCP_HQD_ACTIVE);
		if (!(temp & CP_HQD_ACTIVE__ACTIVE_MASK))
			break;
		if (time_after(jiffies, end_jiffies)) {
			pr_err("cp queue preemption time out.\n");
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
	struct v10_sdma_mqd *m;
	uint32_t sdma_rlc_reg_offset;
	uint32_t temp;
	unsigned long end_jiffies = (utimeout * HZ / 1000) + jiffies;

	m = get_sdma_mqd(mqd);
	sdma_rlc_reg_offset = get_sdma_rlc_reg_offset(adev, m->sdma_engine_id,
					    m->sdma_queue_id);

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

	m->sdmax_rlcx_rb_rptr = RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR);
	m->sdmax_rlcx_rb_rptr_hi =
		RREG32(sdma_rlc_reg_offset + mmSDMA0_RLC0_RB_RPTR_HI);

	return 0;
}

static bool get_atc_vmid_pasid_mapping_info(struct amdgpu_device *adev,
					uint8_t vmid, uint16_t *p_pasid)
{
	uint32_t value;

	value = RREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING)
		     + vmid);
	*p_pasid = value & ATC_VMID0_PASID_MAPPING__PASID_MASK;

	return !!(value & ATC_VMID0_PASID_MAPPING__VALID_MASK);
}

static int kgd_wave_control_execute(struct amdgpu_device *adev,
					uint32_t gfx_index_val,
					uint32_t sq_cmd, uint32_t inst)
{
	uint32_t data = 0;

	mutex_lock(&adev->grbm_idx_mutex);

	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, gfx_index_val);
	WREG32_SOC15(GC, 0, mmSQ_CMD, sq_cmd);

	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		INSTANCE_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SA_BROADCAST_WRITES, 1);
	data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
		SE_BROADCAST_WRITES, 1);

	WREG32_SOC15(GC, 0, mmGRBM_GFX_INDEX, data);
	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

static void set_vm_context_page_table_base(struct amdgpu_device *adev,
		uint32_t vmid, uint64_t page_table_base)
{
	if (!amdgpu_amdkfd_is_kfd_vmid(adev, vmid)) {
		pr_err("trying to set page table base for wrong VMID %u\n",
		       vmid);
		return;
	}

	/* SDMA is on gfxhub as well for Navi1* series */
	adev->gfxhub.funcs->setup_vm_pt_regs(adev, vmid, page_table_base);
}

/*
 * GFX10 helper for wave launch stall requirements on debug trap setting.
 *
 * vmid:
 *   Target VMID to stall/unstall.
 *
 * stall:
 *   0-unstall wave launch (enable), 1-stall wave launch (disable).
 *   After wavefront launch has been stalled, allocated waves must drain from
 *   SPI in order for debug trap settings to take effect on those waves.
 *   This is roughly a ~3500 clock cycle wait on SPI where a read on
 *   SPI_GDBG_WAVE_CNTL translates to ~32 clock cycles.
 *   KGD_GFX_V10_WAVE_LAUNCH_SPI_DRAIN_LATENCY indicates the number of reads required.
 *
 *   NOTE: We can afford to clear the entire STALL_VMID field on unstall
 *   because current GFX10 chips cannot support multi-process debugging due to
 *   trap configuration and masking being limited to global scope.  Always
 *   assume single process conditions.
 *
 */

#define KGD_GFX_V10_WAVE_LAUNCH_SPI_DRAIN_LATENCY	110
static void kgd_gfx_v10_set_wave_launch_stall(struct amdgpu_device *adev, uint32_t vmid, bool stall)
{
	uint32_t data = RREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_WAVE_CNTL));
	int i;

	data = REG_SET_FIELD(data, SPI_GDBG_WAVE_CNTL, STALL_VMID,
							stall ? 1 << vmid : 0);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_WAVE_CNTL), data);

	if (!stall)
		return;

	for (i = 0; i < KGD_GFX_V10_WAVE_LAUNCH_SPI_DRAIN_LATENCY; i++)
		RREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_WAVE_CNTL));
}

uint32_t kgd_gfx_v10_enable_debug_trap(struct amdgpu_device *adev,
				bool restore_dbg_registers,
				uint32_t vmid)
{

	mutex_lock(&adev->grbm_idx_mutex);

	kgd_gfx_v10_set_wave_launch_stall(adev, vmid, true);

	/* assume gfx off is disabled for the debug session if rlc restore not supported. */
	if (restore_dbg_registers) {
		uint32_t data = 0;

		data = REG_SET_FIELD(data, SPI_GDBG_TRAP_CONFIG,
				VMID_SEL, 1 << vmid);
		data = REG_SET_FIELD(data, SPI_GDBG_TRAP_CONFIG,
				TRAP_EN, 1);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_CONFIG), data);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_DATA0), 0);
		WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_DATA1), 0);

		kgd_gfx_v10_set_wave_launch_stall(adev, vmid, false);

		mutex_unlock(&adev->grbm_idx_mutex);

		return 0;
	}

	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_MASK), 0);

	kgd_gfx_v10_set_wave_launch_stall(adev, vmid, false);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

uint32_t kgd_gfx_v10_disable_debug_trap(struct amdgpu_device *adev,
					bool keep_trap_enabled,
					uint32_t vmid)
{
	mutex_lock(&adev->grbm_idx_mutex);

	kgd_gfx_v10_set_wave_launch_stall(adev, vmid, true);

	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_MASK), 0);

	kgd_gfx_v10_set_wave_launch_stall(adev, vmid, false);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

int kgd_gfx_v10_validate_trap_override_request(struct amdgpu_device *adev,
					      uint32_t trap_override,
					      uint32_t *trap_mask_supported)
{
	*trap_mask_supported &= KFD_DBG_TRAP_MASK_DBG_ADDRESS_WATCH;

	/* The SPI_GDBG_TRAP_MASK register is global and affects all
	 * processes. Only allow OR-ing the address-watch bit, since
	 * this only affects processes under the debugger. Other bits
	 * should stay 0 to avoid the debugger interfering with other
	 * processes.
	 */
	if (trap_override != KFD_DBG_TRAP_OVERRIDE_OR)
		return -EINVAL;

	return 0;
}

uint32_t kgd_gfx_v10_set_wave_launch_trap_override(struct amdgpu_device *adev,
					      uint32_t vmid,
					      uint32_t trap_override,
					      uint32_t trap_mask_bits,
					      uint32_t trap_mask_request,
					      uint32_t *trap_mask_prev,
					      uint32_t kfd_dbg_trap_cntl_prev)
{
	uint32_t data, wave_cntl_prev;

	mutex_lock(&adev->grbm_idx_mutex);

	wave_cntl_prev = RREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_WAVE_CNTL));

	kgd_gfx_v10_set_wave_launch_stall(adev, vmid, true);

	data = RREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_MASK));
	*trap_mask_prev = REG_GET_FIELD(data, SPI_GDBG_TRAP_MASK, EXCP_EN);

	trap_mask_bits = (trap_mask_bits & trap_mask_request) |
		(*trap_mask_prev & ~trap_mask_request);

	data = REG_SET_FIELD(data, SPI_GDBG_TRAP_MASK, EXCP_EN, trap_mask_bits);
	data = REG_SET_FIELD(data, SPI_GDBG_TRAP_MASK, REPLACE, trap_override);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_TRAP_MASK), data);

	/* We need to preserve wave launch mode stall settings. */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_WAVE_CNTL), wave_cntl_prev);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

uint32_t kgd_gfx_v10_set_wave_launch_mode(struct amdgpu_device *adev,
					uint8_t wave_launch_mode,
					uint32_t vmid)
{
	uint32_t data = 0;
	bool is_mode_set = !!wave_launch_mode;

	mutex_lock(&adev->grbm_idx_mutex);

	kgd_gfx_v10_set_wave_launch_stall(adev, vmid, true);

	data = REG_SET_FIELD(data, SPI_GDBG_WAVE_CNTL2,
			VMID_MASK, is_mode_set ? 1 << vmid : 0);
	data = REG_SET_FIELD(data, SPI_GDBG_WAVE_CNTL2,
			MODE, is_mode_set ? wave_launch_mode : 0);
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSPI_GDBG_WAVE_CNTL2), data);

	kgd_gfx_v10_set_wave_launch_stall(adev, vmid, false);

	mutex_unlock(&adev->grbm_idx_mutex);

	return 0;
}

#define TCP_WATCH_STRIDE (mmTCP_WATCH1_ADDR_H - mmTCP_WATCH0_ADDR_H)
#define SQ_WATCH_STRIDE (mmSQ_WATCH1_ADDR_H - mmSQ_WATCH0_ADDR_H)
uint32_t kgd_gfx_v10_set_address_watch(struct amdgpu_device *adev,
					uint64_t watch_address,
					uint32_t watch_address_mask,
					uint32_t watch_id,
					uint32_t watch_mode,
					uint32_t debug_vmid,
					uint32_t inst)
{
	/* SQ_WATCH?_ADDR_* and TCP_WATCH?_ADDR_* are programmed with the
	 * same values.
	 */
	uint32_t watch_address_high;
	uint32_t watch_address_low;
	uint32_t tcp_watch_address_cntl;
	uint32_t sq_watch_address_cntl;

	watch_address_low = lower_32_bits(watch_address);
	watch_address_high = upper_32_bits(watch_address) & 0xffff;

	tcp_watch_address_cntl = 0;
	tcp_watch_address_cntl = REG_SET_FIELD(tcp_watch_address_cntl,
			TCP_WATCH0_CNTL,
			VMID,
			debug_vmid);
	tcp_watch_address_cntl = REG_SET_FIELD(tcp_watch_address_cntl,
			TCP_WATCH0_CNTL,
			MODE,
			watch_mode);
	tcp_watch_address_cntl = REG_SET_FIELD(tcp_watch_address_cntl,
			TCP_WATCH0_CNTL,
			MASK,
			watch_address_mask >> 7);

	sq_watch_address_cntl = 0;
	sq_watch_address_cntl = REG_SET_FIELD(sq_watch_address_cntl,
			SQ_WATCH0_CNTL,
			VMID,
			debug_vmid);
	sq_watch_address_cntl = REG_SET_FIELD(sq_watch_address_cntl,
			SQ_WATCH0_CNTL,
			MODE,
			watch_mode);
	sq_watch_address_cntl = REG_SET_FIELD(sq_watch_address_cntl,
			SQ_WATCH0_CNTL,
			MASK,
			watch_address_mask >> 6);

	/* Turning off this watch point until we set all the registers */
	tcp_watch_address_cntl = REG_SET_FIELD(tcp_watch_address_cntl,
			TCP_WATCH0_CNTL,
			VALID,
			0);
	WREG32((SOC15_REG_OFFSET(GC, 0, mmTCP_WATCH0_CNTL) +
			(watch_id * TCP_WATCH_STRIDE)),
			tcp_watch_address_cntl);

	sq_watch_address_cntl = REG_SET_FIELD(sq_watch_address_cntl,
			SQ_WATCH0_CNTL,
			VALID,
			0);
	WREG32((SOC15_REG_OFFSET(GC, 0, mmSQ_WATCH0_CNTL) +
			(watch_id * SQ_WATCH_STRIDE)),
			sq_watch_address_cntl);

	/* Program {TCP,SQ}_WATCH?_ADDR* */
	WREG32((SOC15_REG_OFFSET(GC, 0, mmTCP_WATCH0_ADDR_H) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_high);
	WREG32((SOC15_REG_OFFSET(GC, 0, mmTCP_WATCH0_ADDR_L) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_low);

	WREG32((SOC15_REG_OFFSET(GC, 0, mmSQ_WATCH0_ADDR_H) +
			(watch_id * SQ_WATCH_STRIDE)),
			watch_address_high);
	WREG32((SOC15_REG_OFFSET(GC, 0, mmSQ_WATCH0_ADDR_L) +
			(watch_id * SQ_WATCH_STRIDE)),
			watch_address_low);

	/* Enable the watch point */
	tcp_watch_address_cntl = REG_SET_FIELD(tcp_watch_address_cntl,
			TCP_WATCH0_CNTL,
			VALID,
			1);
	WREG32((SOC15_REG_OFFSET(GC, 0, mmTCP_WATCH0_CNTL) +
			(watch_id * TCP_WATCH_STRIDE)),
			tcp_watch_address_cntl);

	sq_watch_address_cntl = REG_SET_FIELD(sq_watch_address_cntl,
			SQ_WATCH0_CNTL,
			VALID,
			1);
	WREG32((SOC15_REG_OFFSET(GC, 0, mmSQ_WATCH0_CNTL) +
			(watch_id * SQ_WATCH_STRIDE)),
			sq_watch_address_cntl);

	return 0;
}

uint32_t kgd_gfx_v10_clear_address_watch(struct amdgpu_device *adev,
					uint32_t watch_id)
{
	uint32_t watch_address_cntl;

	watch_address_cntl = 0;

	WREG32((SOC15_REG_OFFSET(GC, 0, mmTCP_WATCH0_CNTL) +
			(watch_id * TCP_WATCH_STRIDE)),
			watch_address_cntl);

	WREG32((SOC15_REG_OFFSET(GC, 0, mmSQ_WATCH0_CNTL) +
			(watch_id * SQ_WATCH_STRIDE)),
			watch_address_cntl);

	return 0;
}
#undef TCP_WATCH_STRIDE
#undef SQ_WATCH_STRIDE


/* kgd_gfx_v10_get_iq_wait_times: Returns the mmCP_IQ_WAIT_TIME1/2 values
 * The values read are:
 *     ib_offload_wait_time     -- Wait Count for Indirect Buffer Offloads.
 *     atomic_offload_wait_time -- Wait Count for L2 and GDS Atomics Offloads.
 *     wrm_offload_wait_time    -- Wait Count for WAIT_REG_MEM Offloads.
 *     gws_wait_time            -- Wait Count for Global Wave Syncs.
 *     que_sleep_wait_time      -- Wait Count for Dequeue Retry.
 *     sch_wave_wait_time       -- Wait Count for Scheduling Wave Message.
 *     sem_rearm_wait_time      -- Wait Count for Semaphore re-arm.
 *     deq_retry_wait_time      -- Wait Count for Global Wave Syncs.
 */
void kgd_gfx_v10_get_iq_wait_times(struct amdgpu_device *adev,
					uint32_t *wait_times,
					uint32_t inst)

{
	*wait_times = RREG32(SOC15_REG_OFFSET(GC, 0, mmCP_IQ_WAIT_TIME2));
}

void kgd_gfx_v10_build_grace_period_packet_info(struct amdgpu_device *adev,
						uint32_t wait_times,
						uint32_t grace_period,
						uint32_t *reg_offset,
						uint32_t *reg_data)
{
	*reg_data = wait_times;

	/*
	 * The CP cannont handle a 0 grace period input and will result in
	 * an infinite grace period being set so set to 1 to prevent this.
	 */
	if (grace_period == 0)
		grace_period = 1;

	*reg_data = REG_SET_FIELD(*reg_data,
			CP_IQ_WAIT_TIME2,
			SCH_WAVE,
			grace_period);

	*reg_offset = SOC15_REG_OFFSET(GC, 0, mmCP_IQ_WAIT_TIME2);
}

static void program_trap_handler_settings(struct amdgpu_device *adev,
		uint32_t vmid, uint64_t tba_addr, uint64_t tma_addr,
		uint32_t inst)
{
	lock_srbm(adev, 0, 0, 0, vmid);

	/*
	 * Program TBA registers
	 */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSQ_SHADER_TBA_LO),
			lower_32_bits(tba_addr >> 8));
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSQ_SHADER_TBA_HI),
			upper_32_bits(tba_addr >> 8) |
			(1 << SQ_SHADER_TBA_HI__TRAP_EN__SHIFT));

	/*
	 * Program TMA registers
	 */
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSQ_SHADER_TMA_LO),
			lower_32_bits(tma_addr >> 8));
	WREG32(SOC15_REG_OFFSET(GC, 0, mmSQ_SHADER_TMA_HI),
			upper_32_bits(tma_addr >> 8));

	unlock_srbm(adev);
}

uint64_t kgd_gfx_v10_hqd_get_pq_addr(struct amdgpu_device *adev,
				     uint32_t pipe_id, uint32_t queue_id,
				     uint32_t inst)
{
	return 0;
}

uint64_t kgd_gfx_v10_hqd_reset(struct amdgpu_device *adev,
			       uint32_t pipe_id, uint32_t queue_id,
			       uint32_t inst, unsigned int utimeout)
{
	return 0;
}

const struct kfd2kgd_calls gfx_v10_kfd2kgd = {
	.program_sh_mem_settings = kgd_program_sh_mem_settings,
	.set_pasid_vmid_mapping = kgd_set_pasid_vmid_mapping,
	.init_interrupts = kgd_init_interrupts,
	.hqd_load = kgd_hqd_load,
	.hiq_mqd_load = kgd_hiq_mqd_load,
	.hqd_sdma_load = kgd_hqd_sdma_load,
	.hqd_dump = kgd_hqd_dump,
	.hqd_sdma_dump = kgd_hqd_sdma_dump,
	.hqd_is_occupied = kgd_hqd_is_occupied,
	.hqd_sdma_is_occupied = kgd_hqd_sdma_is_occupied,
	.hqd_destroy = kgd_hqd_destroy,
	.hqd_sdma_destroy = kgd_hqd_sdma_destroy,
	.wave_control_execute = kgd_wave_control_execute,
	.get_atc_vmid_pasid_mapping_info =
			get_atc_vmid_pasid_mapping_info,
	.set_vm_context_page_table_base = set_vm_context_page_table_base,
	.enable_debug_trap = kgd_gfx_v10_enable_debug_trap,
	.disable_debug_trap = kgd_gfx_v10_disable_debug_trap,
	.validate_trap_override_request = kgd_gfx_v10_validate_trap_override_request,
	.set_wave_launch_trap_override = kgd_gfx_v10_set_wave_launch_trap_override,
	.set_wave_launch_mode = kgd_gfx_v10_set_wave_launch_mode,
	.set_address_watch = kgd_gfx_v10_set_address_watch,
	.clear_address_watch = kgd_gfx_v10_clear_address_watch,
	.get_iq_wait_times = kgd_gfx_v10_get_iq_wait_times,
	.build_grace_period_packet_info = kgd_gfx_v10_build_grace_period_packet_info,
	.program_trap_handler_settings = program_trap_handler_settings,
	.hqd_get_pq_addr = kgd_gfx_v10_hqd_get_pq_addr,
	.hqd_reset = kgd_gfx_v10_hqd_reset
};
