/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_hw_ip.h"
#include "vcn_v2_0.h"

#include "vcn/vcn_5_0_0_offset.h"
#include "vcn/vcn_5_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_4_0.h"
#include "vcn_v5_0_0.h"

#include <drm/drm_drv.h>

static const struct amdgpu_hwip_reg_entry vcn_reg_list_5_0[] = {
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_POWER_STATUS),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_STATUS),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_CONTEXT_ID),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_CONTEXT_ID2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_GPCOM_VCPU_DATA0),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_GPCOM_VCPU_DATA1),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_GPCOM_VCPU_CMD),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_HI4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_BASE_LO4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_RPTR4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_WPTR4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE2),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE3),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_RB_SIZE4),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_LMA_CTL),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_LMA_DATA),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_LMA_MASK),
	SOC15_REG_ENTRY_STR(VCN, 0, regUVD_DPG_PAUSE)
};

static int amdgpu_ih_clientid_vcns[] = {
	SOC15_IH_CLIENTID_VCN,
	SOC15_IH_CLIENTID_VCN1
};

static void vcn_v5_0_0_set_unified_ring_funcs(struct amdgpu_device *adev);
static void vcn_v5_0_0_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v5_0_0_set_powergating_state(void *handle,
		enum amd_powergating_state state);
static int vcn_v5_0_0_pause_dpg_mode(struct amdgpu_device *adev,
		int inst_idx, struct dpg_pause_state *new_state);
static void vcn_v5_0_0_unified_ring_set_wptr(struct amdgpu_ring *ring);

/**
 * vcn_v5_0_0_early_init - set function pointers and load microcode
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 * Load microcode from filesystem
 */
static int vcn_v5_0_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* re-use enc ring as unified ring */
	adev->vcn.num_enc_rings = 1;

	vcn_v5_0_0_set_unified_ring_funcs(adev);
	vcn_v5_0_0_set_irq_funcs(adev);

	return amdgpu_vcn_early_init(adev);
}

/**
 * vcn_v5_0_0_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v5_0_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, r;
	uint32_t reg_count = ARRAY_SIZE(vcn_reg_list_5_0);
	uint32_t *ptr;

	r = amdgpu_vcn_sw_init(adev);
	if (r)
		return r;

	amdgpu_vcn_setup_ucode(adev);

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		volatile struct amdgpu_vcn5_fw_shared *fw_shared;

		if (adev->vcn.harvest_config & (1 << i))
			continue;

		atomic_set(&adev->vcn.inst[i].sched_score, 0);

		/* VCN UNIFIED TRAP */
		r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				VCN_4_0__SRCID__UVD_ENC_GENERAL_PURPOSE, &adev->vcn.inst[i].irq);
		if (r)
			return r;

		/* VCN POISON TRAP */
		r = amdgpu_irq_add_id(adev, amdgpu_ih_clientid_vcns[i],
				VCN_4_0__SRCID_UVD_POISON, &adev->vcn.inst[i].irq);
		if (r)
			return r;

		ring = &adev->vcn.inst[i].ring_enc[0];
		ring->use_doorbell = true;
		ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 2 + 8 * i;

		ring->vm_hub = AMDGPU_MMHUB0(0);
		snprintf(ring->name, sizeof(ring->name), "vcn_unified_%d", i);

		r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst[i].irq, 0,
						AMDGPU_RING_PRIO_0, &adev->vcn.inst[i].sched_score);
		if (r)
			return r;

		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
		fw_shared->present_flag_0 = cpu_to_le32(AMDGPU_FW_SHARED_FLAG_0_UNIFIED_QUEUE);
		fw_shared->sq.is_enabled = 1;

		if (amdgpu_vcnfw_log)
			amdgpu_vcn_fwlog_init(&adev->vcn.inst[i]);
	}

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
		adev->vcn.pause_dpg_mode = vcn_v5_0_0_pause_dpg_mode;

	/* Allocate memory for VCN IP Dump buffer */
	ptr = kcalloc(adev->vcn.num_vcn_inst * reg_count, sizeof(uint32_t), GFP_KERNEL);
	if (!ptr) {
		DRM_ERROR("Failed to allocate memory for VCN IP Dump\n");
		adev->vcn.ip_dump = NULL;
	} else {
		adev->vcn.ip_dump = ptr;
	}
	return 0;
}

/**
 * vcn_v5_0_0_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v5_0_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, r, idx;

	if (drm_dev_enter(adev_to_drm(adev), &idx)) {
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			volatile struct amdgpu_vcn5_fw_shared *fw_shared;

			if (adev->vcn.harvest_config & (1 << i))
				continue;

			fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
			fw_shared->present_flag_0 = 0;
			fw_shared->sq.is_enabled = 0;
		}

		drm_dev_exit(idx);
	}

	r = amdgpu_vcn_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vcn_sw_fini(adev);

	kfree(adev->vcn.ip_dump);

	return r;
}

/**
 * vcn_v5_0_0_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v5_0_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int i, r;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ring = &adev->vcn.inst[i].ring_enc[0];

		adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
			((adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 8 * i), i);

		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;
	}

	return 0;
}

/**
 * vcn_v5_0_0_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v5_0_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	cancel_delayed_work_sync(&adev->vcn.idle_work);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;
		if (!amdgpu_sriov_vf(adev)) {
			if ((adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) ||
				(adev->vcn.cur_state != AMD_PG_STATE_GATE &&
				RREG32_SOC15(VCN, i, regUVD_STATUS))) {
				vcn_v5_0_0_set_powergating_state(adev, AMD_PG_STATE_GATE);
			}
		}
	}

	return 0;
}

/**
 * vcn_v5_0_0_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v5_0_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vcn_v5_0_0_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v5_0_0_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v5_0_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v5_0_0_hw_init(adev);

	return r;
}

/**
 * vcn_v5_0_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v5_0_0_mc_resume(struct amdgpu_device *adev, int inst)
{
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.fw[inst]->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_lo));
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_hi));
		WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->vcn.inst[inst].gpu_addr));
		WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->vcn.inst[inst].gpu_addr));
		offset = size;
		WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET0, AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	}
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(VCN, inst, regUVD_VCPU_CACHE_SIZE2, AMDGPU_VCN_CONTEXT_SIZE);

	/* non-cache window */
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].fw_shared.gpu_addr));
	WREG32_SOC15(VCN, inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].fw_shared.gpu_addr));
	WREG32_SOC15(VCN, inst, regUVD_VCPU_NONCACHE_OFFSET0, 0);
	WREG32_SOC15(VCN, inst, regUVD_VCPU_NONCACHE_SIZE0,
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn5_fw_shared)));
}

/**
 * vcn_v5_0_0_mc_resume_dpg_mode - memory controller programming for dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Let the VCN memory controller know it's offsets with dpg mode
 */
static void vcn_v5_0_0_mc_resume_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.fw[inst_idx]->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (!indirect) {
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_lo), 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst_idx].tmr_mc_addr_hi), 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		} else {
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW), 0, 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH), 0, 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		}
		offset = 0;
	} else {
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		offset = size;
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET0),
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3, 0, indirect);
	}

	if (!indirect)
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_SIZE0), size, 0, indirect);
	else
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_SIZE0), 0, 0, indirect);

	/* cache window 1: stack */
	if (!indirect) {
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	} else {
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW), 0, 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH), 0, 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	}
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, inst_idx, regUVD_VCPU_CACHE_SIZE1), AMDGPU_VCN_STACK_SIZE, 0, indirect);

	/* cache window 2: context */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
		lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
		upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CACHE_OFFSET2), 0, 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CACHE_SIZE2), AMDGPU_VCN_CONTEXT_SIZE, 0, indirect);

	/* non-cache window */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
		lower_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
		upper_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_NONCACHE_OFFSET0), 0, 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_NONCACHE_SIZE0),
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn5_fw_shared)), 0, indirect);

	/* VCN global tiling registers */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_GFX10_ADDR_CONFIG),
		adev->gfx.config.gb_addr_config, 0, indirect);

	return;
}

/**
 * vcn_v5_0_0_disable_static_power_gating - disable VCN static power gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Disable static power gating for VCN block
 */
static void vcn_v5_0_0_disable_static_power_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data = 0;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		data = 1 << UVD_IPX_DLDO_CONFIG__ONO2_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS, 0,
				UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS_MASK);

		data = 2 << UVD_IPX_DLDO_CONFIG__ONO3_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
				1 << UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS__SHIFT,
				UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS_MASK);

		data = 2 << UVD_IPX_DLDO_CONFIG__ONO4_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
				1 << UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS__SHIFT,
				UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS_MASK);

		data = 2 << UVD_IPX_DLDO_CONFIG__ONO5_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
				1 << UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS__SHIFT,
				UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS_MASK);
	} else {
		data = 1 << UVD_IPX_DLDO_CONFIG__ONO2_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS, 0,
				UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS_MASK);

		data = 1 << UVD_IPX_DLDO_CONFIG__ONO3_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS, 0,
				UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS_MASK);

		data = 1 << UVD_IPX_DLDO_CONFIG__ONO4_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS, 0,
				UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS_MASK);

		data = 1 << UVD_IPX_DLDO_CONFIG__ONO5_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS, 0,
				UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS_MASK);
	}

	data = RREG32_SOC15(VCN, inst, regUVD_POWER_STATUS);
	data &= ~0x103;
	if (adev->pg_flags & AMD_PG_SUPPORT_VCN)
		data |= UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON |
			UVD_POWER_STATUS__UVD_PG_EN_MASK;

	WREG32_SOC15(VCN, inst, regUVD_POWER_STATUS, data);
	return;
}

/**
 * vcn_v5_0_0_enable_static_power_gating - enable VCN static power gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Enable static power gating for VCN block
 */
static void vcn_v5_0_0_enable_static_power_gating(struct amdgpu_device *adev, int inst)
{
	uint32_t data;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		/* Before power off, this indicator has to be turned on */
		data = RREG32_SOC15(VCN, inst, regUVD_POWER_STATUS);
		data &= ~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK;
		data |= UVD_POWER_STATUS__UVD_POWER_STATUS_TILES_OFF;
		WREG32_SOC15(VCN, inst, regUVD_POWER_STATUS, data);

		data = 2 << UVD_IPX_DLDO_CONFIG__ONO5_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
				1 << UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS__SHIFT,
				UVD_IPX_DLDO_STATUS__ONO5_PWR_STATUS_MASK);

		data = 2 << UVD_IPX_DLDO_CONFIG__ONO4_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
				1 << UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS__SHIFT,
				UVD_IPX_DLDO_STATUS__ONO4_PWR_STATUS_MASK);

		data = 2 << UVD_IPX_DLDO_CONFIG__ONO3_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
				1 << UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS__SHIFT,
				UVD_IPX_DLDO_STATUS__ONO3_PWR_STATUS_MASK);

		data = 2 << UVD_IPX_DLDO_CONFIG__ONO2_PWR_CONFIG__SHIFT;
		WREG32_SOC15(VCN, inst, regUVD_IPX_DLDO_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, inst, regUVD_IPX_DLDO_STATUS,
				1 << UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS__SHIFT,
				UVD_IPX_DLDO_STATUS__ONO2_PWR_STATUS_MASK);
	}
	return;
}

/**
 * vcn_v5_0_0_disable_clock_gating - disable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Disable clock gating for VCN block
 */
static void vcn_v5_0_0_disable_clock_gating(struct amdgpu_device *adev, int inst)
{
	return;
}

#if 0
/**
 * vcn_v5_0_0_disable_clock_gating_dpg_mode - disable VCN clock gating dpg mode
 *
 * @adev: amdgpu_device pointer
 * @sram_sel: sram select
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Disable clock gating for VCN block with dpg mode
 */
static void vcn_v5_0_0_disable_clock_gating_dpg_mode(struct amdgpu_device *adev, uint8_t sram_sel,
	int inst_idx, uint8_t indirect)
{
	return;
}
#endif

/**
 * vcn_v5_0_0_enable_clock_gating - enable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Enable clock gating for VCN block
 */
static void vcn_v5_0_0_enable_clock_gating(struct amdgpu_device *adev, int inst)
{
	return;
}

/**
 * vcn_v5_0_0_start_dpg_mode - VCN start with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Start VCN block with dpg mode
 */
static int vcn_v5_0_0_start_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	volatile struct amdgpu_vcn5_fw_shared *fw_shared = adev->vcn.inst[inst_idx].fw_shared.cpu_addr;
	struct amdgpu_ring *ring;
	uint32_t tmp;

	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, regUVD_POWER_STATUS), 1,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* enable dynamic power gating mode */
	tmp = RREG32_SOC15(VCN, inst_idx, regUVD_POWER_STATUS);
	tmp |= UVD_POWER_STATUS__UVD_PG_MODE_MASK;
	tmp |= UVD_POWER_STATUS__UVD_PG_EN_MASK;
	WREG32_SOC15(VCN, inst_idx, regUVD_POWER_STATUS, tmp);

	if (indirect)
		adev->vcn.inst[inst_idx].dpg_sram_curr_addr = (uint32_t *)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr;

	/* enable VCPU clock */
	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK | UVD_VCPU_CNTL__BLK_RST_MASK;
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* disable master interrupt */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MASTINT_EN), 0, 0, indirect);

	/* setup regUVD_LMI_CTRL */
	tmp = (UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__REQ_MODE_MASK |
		UVD_LMI_CTRL__CRC_RESET_MASK |
		UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
		(8 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
		0x00100000L);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_CTRL), tmp, 0, indirect);

	vcn_v5_0_0_mc_resume_dpg_mode(adev, inst_idx, indirect);

	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* enable LMI MC and UMC channels */
	tmp = 0x1f << UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM__SHIFT;
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_LMI_CTRL2), tmp, 0, indirect);

	/* enable master interrupt */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, inst_idx, regUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK, 0, indirect);

	if (indirect)
		amdgpu_vcn_psp_update_sram(adev, inst_idx, 0);

	ring = &adev->vcn.inst[inst_idx].ring_enc[0];

	WREG32_SOC15(VCN, inst_idx, regUVD_RB_BASE_LO, ring->gpu_addr);
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_SIZE, ring->ring_size / 4);

	tmp = RREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE);
	tmp &= ~(VCN_RB_ENABLE__RB1_EN_MASK);
	WREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode |= FW_QUEUE_RING_RESET;
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_RPTR, 0);
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR, 0);

	tmp = RREG32_SOC15(VCN, inst_idx, regUVD_RB_RPTR);
	WREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR, tmp);
	ring->wptr = RREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR);

	tmp = RREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE);
	tmp |= VCN_RB_ENABLE__RB1_EN_MASK;
	WREG32_SOC15(VCN, inst_idx, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode &= ~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF);

	WREG32_SOC15(VCN, inst_idx, regVCN_RB1_DB_CTRL,
		ring->doorbell_index << VCN_RB1_DB_CTRL__OFFSET__SHIFT |
		VCN_RB1_DB_CTRL__EN_MASK);

	return 0;
}

/**
 * vcn_v5_0_0_start - VCN start
 *
 * @adev: amdgpu_device pointer
 *
 * Start VCN block
 */
static int vcn_v5_0_0_start(struct amdgpu_device *adev)
{
	volatile struct amdgpu_vcn5_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	uint32_t tmp;
	int i, j, k, r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, true);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
			r = vcn_v5_0_0_start_dpg_mode(adev, i, adev->vcn.indirect_sram);
			continue;
		}

		/* disable VCN power gating */
		vcn_v5_0_0_disable_static_power_gating(adev, i);

		/* set VCN status busy */
		tmp = RREG32_SOC15(VCN, i, regUVD_STATUS) | UVD_STATUS__UVD_BUSY;
		WREG32_SOC15(VCN, i, regUVD_STATUS, tmp);

		/* enable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__CLK_EN_MASK, ~UVD_VCPU_CNTL__CLK_EN_MASK);

		/* disable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_MASTINT_EN), 0,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* enable LMI MC and UMC channels */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

		tmp = RREG32_SOC15(VCN, i, regUVD_SOFT_RESET);
		tmp &= ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		tmp &= ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, regUVD_SOFT_RESET, tmp);

		/* setup regUVD_LMI_CTRL */
		tmp = RREG32_SOC15(VCN, i, regUVD_LMI_CTRL);
		WREG32_SOC15(VCN, i, regUVD_LMI_CTRL, tmp |
			UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
			UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
			UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
			UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

		vcn_v5_0_0_mc_resume(adev, i);

		/* VCN global tiling registers */
		WREG32_SOC15(VCN, i, regUVD_GFX10_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);

		/* unblock VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_RB_ARB_CTRL), 0,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* release VCPU reset to boot */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL), 0,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		for (j = 0; j < 10; ++j) {
			uint32_t status;

			for (k = 0; k < 100; ++k) {
				status = RREG32_SOC15(VCN, i, regUVD_STATUS);
				if (status & 2)
					break;
				mdelay(10);
				if (amdgpu_emu_mode == 1)
					drm_msleep(1);
			}

			if (amdgpu_emu_mode == 1) {
				r = -1;
				if (status & 2) {
					r = 0;
					break;
				}
			} else {
				r = 0;
				if (status & 2)
					break;

				dev_err(adev->dev,
					"VCN[%d] is not responding, trying to reset the VCPU!!!\n", i);
				WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL),
							UVD_VCPU_CNTL__BLK_RST_MASK,
							~UVD_VCPU_CNTL__BLK_RST_MASK);
				mdelay(10);
				WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL), 0,
							~UVD_VCPU_CNTL__BLK_RST_MASK);

				mdelay(10);
				r = -1;
			}
		}

		if (r) {
			dev_err(adev->dev, "VCN[%d] is not responding, giving up!!!\n", i);
			return r;
		}

		/* enable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_MASTINT_EN),
				UVD_MASTINT_EN__VCPU_EN_MASK,
				~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* clear the busy bit of VCN_STATUS */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_STATUS), 0,
			~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

		ring = &adev->vcn.inst[i].ring_enc[0];
		WREG32_SOC15(VCN, i, regVCN_RB1_DB_CTRL,
			ring->doorbell_index << VCN_RB1_DB_CTRL__OFFSET__SHIFT |
			VCN_RB1_DB_CTRL__EN_MASK);

		WREG32_SOC15(VCN, i, regUVD_RB_BASE_LO, ring->gpu_addr);
		WREG32_SOC15(VCN, i, regUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, i, regUVD_RB_SIZE, ring->ring_size / 4);

		tmp = RREG32_SOC15(VCN, i, regVCN_RB_ENABLE);
		tmp &= ~(VCN_RB_ENABLE__RB1_EN_MASK);
		WREG32_SOC15(VCN, i, regVCN_RB_ENABLE, tmp);
		fw_shared->sq.queue_mode |= FW_QUEUE_RING_RESET;
		WREG32_SOC15(VCN, i, regUVD_RB_RPTR, 0);
		WREG32_SOC15(VCN, i, regUVD_RB_WPTR, 0);

		tmp = RREG32_SOC15(VCN, i, regUVD_RB_RPTR);
		WREG32_SOC15(VCN, i, regUVD_RB_WPTR, tmp);
		ring->wptr = RREG32_SOC15(VCN, i, regUVD_RB_WPTR);

		tmp = RREG32_SOC15(VCN, i, regVCN_RB_ENABLE);
		tmp |= VCN_RB_ENABLE__RB1_EN_MASK;
		WREG32_SOC15(VCN, i, regVCN_RB_ENABLE, tmp);
		fw_shared->sq.queue_mode &= ~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF);
	}

	return 0;
}

/**
 * vcn_v5_0_0_stop_dpg_mode - VCN stop with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 *
 * Stop VCN block with dpg mode
 */
static void vcn_v5_0_0_stop_dpg_mode(struct amdgpu_device *adev, int inst_idx)
{
	struct dpg_pause_state state = {.fw_based = VCN_DPG_STATE__UNPAUSE};
	uint32_t tmp;

	vcn_v5_0_0_pause_dpg_mode(adev, inst_idx, &state);

	/* Wait for power status to be 1 */
	SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* wait for read ptr to be equal to write ptr */
	tmp = RREG32_SOC15(VCN, inst_idx, regUVD_RB_WPTR);
	SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_RB_RPTR, tmp, 0xFFFFFFFF);

	/* disable dynamic power gating mode */
	WREG32_P(SOC15_REG_OFFSET(VCN, inst_idx, regUVD_POWER_STATUS), 0,
		~UVD_POWER_STATUS__UVD_PG_MODE_MASK);

	return;
}

/**
 * vcn_v5_0_0_stop - VCN stop
 *
 * @adev: amdgpu_device pointer
 *
 * Stop VCN block
 */
static int vcn_v5_0_0_stop(struct amdgpu_device *adev)
{
	volatile struct amdgpu_vcn5_fw_shared *fw_shared;
	uint32_t tmp;
	int i, r = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
		fw_shared->sq.queue_mode |= FW_QUEUE_DPG_HOLD_OFF;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
			vcn_v5_0_0_stop_dpg_mode(adev, i);
			continue;
		}

		/* wait for vcn idle */
		r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_STATUS, UVD_STATUS__IDLE, 0x7);
		if (r)
			return r;

		tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
		      UVD_LMI_STATUS__READ_CLEAN_MASK |
		      UVD_LMI_STATUS__WRITE_CLEAN_MASK |
		      UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* disable LMI UMC channel */
		tmp = RREG32_SOC15(VCN, i, regUVD_LMI_CTRL2);
		tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
		WREG32_SOC15(VCN, i, regUVD_LMI_CTRL2, tmp);
		tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK |
		      UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, i, regUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* block VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_RB_ARB_CTRL),
			UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* reset VCPU */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__BLK_RST_MASK,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		/* disable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, i, regUVD_VCPU_CNTL), 0,
			~(UVD_VCPU_CNTL__CLK_EN_MASK));

		/* apply soft reset */
		tmp = RREG32_SOC15(VCN, i, regUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, regUVD_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(VCN, i, regUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, i, regUVD_SOFT_RESET, tmp);

		/* clear status */
		WREG32_SOC15(VCN, i, regUVD_STATUS, 0);

		/* enable VCN power gating */
		vcn_v5_0_0_enable_static_power_gating(adev, i);
	}

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, false);

	return 0;
}

/**
 * vcn_v5_0_0_pause_dpg_mode - VCN pause with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @new_state: pause state
 *
 * Pause dpg mode for VCN block
 */
static int vcn_v5_0_0_pause_dpg_mode(struct amdgpu_device *adev, int inst_idx,
	struct dpg_pause_state *new_state)
{
	uint32_t reg_data = 0;
	int ret_code;

	/* pause/unpause if state is changed */
	if (adev->vcn.inst[inst_idx].pause_state.fw_based != new_state->fw_based) {
		DRM_DEV_DEBUG(adev->dev, "dpg pause state changed %d -> %d",
			adev->vcn.inst[inst_idx].pause_state.fw_based,  new_state->fw_based);
		reg_data = RREG32_SOC15(VCN, inst_idx, regUVD_DPG_PAUSE) &
			(~UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK);

		if (new_state->fw_based == VCN_DPG_STATE__PAUSE) {
			ret_code = SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_POWER_STATUS, 0x1,
					UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

			if (!ret_code) {
				/* pause DPG */
				reg_data |= UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ_MASK;
				WREG32_SOC15(VCN, inst_idx, regUVD_DPG_PAUSE, reg_data);

				/* wait for ACK */
				SOC15_WAIT_ON_RREG(VCN, inst_idx, regUVD_DPG_PAUSE,
					UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK,
					UVD_DPG_PAUSE__NJ_PAUSE_DPG_ACK_MASK);
			}
		} else {
			/* unpause dpg, no need to wait */
			reg_data &= ~UVD_DPG_PAUSE__NJ_PAUSE_DPG_REQ_MASK;
			WREG32_SOC15(VCN, inst_idx, regUVD_DPG_PAUSE, reg_data);
		}
		adev->vcn.inst[inst_idx].pause_state.fw_based = new_state->fw_based;
	}

	return 0;
}

/**
 * vcn_v5_0_0_unified_ring_get_rptr - get unified read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified read pointer
 */
static uint64_t vcn_v5_0_0_unified_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	return RREG32_SOC15(VCN, ring->me, regUVD_RB_RPTR);
}

/**
 * vcn_v5_0_0_unified_ring_get_wptr - get unified write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified write pointer
 */
static uint64_t vcn_v5_0_0_unified_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;
	else
		return RREG32_SOC15(VCN, ring->me, regUVD_RB_WPTR);
}

/**
 * vcn_v5_0_0_unified_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v5_0_0_unified_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell) {
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(VCN, ring->me, regUVD_RB_WPTR, lower_32_bits(ring->wptr));
	}
}

static const struct amdgpu_ring_funcs vcn_v5_0_0_unified_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.get_rptr = vcn_v5_0_0_unified_ring_get_rptr,
	.get_wptr = vcn_v5_0_0_unified_ring_get_wptr,
	.set_wptr = vcn_v5_0_0_unified_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vcn_v2_0_enc_ring_emit_vm_flush */
		5 + 5 + /* vcn_v2_0_enc_ring_emit_fence x2 vm fence */
		1, /* vcn_v2_0_enc_ring_insert_end */
	.emit_ib_size = 5, /* vcn_v2_0_enc_ring_emit_ib */
	.emit_ib = vcn_v2_0_enc_ring_emit_ib,
	.emit_fence = vcn_v2_0_enc_ring_emit_fence,
	.emit_vm_flush = vcn_v2_0_enc_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_enc_ring_test_ring,
	.test_ib = amdgpu_vcn_unified_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v2_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v5_0_0_set_unified_ring_funcs - set unified ring functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set unified ring functions
 */
static void vcn_v5_0_0_set_unified_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].ring_enc[0].funcs = &vcn_v5_0_0_unified_ring_vm_funcs;
		adev->vcn.inst[i].ring_enc[0].me = i;
	}
}

/**
 * vcn_v5_0_0_is_idle - check VCN block is idle
 *
 * @handle: amdgpu_device pointer
 *
 * Check whether VCN block is idle
 */
static bool vcn_v5_0_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 1;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret &= (RREG32_SOC15(VCN, i, regUVD_STATUS) == UVD_STATUS__IDLE);
	}

	return ret;
}

/**
 * vcn_v5_0_0_wait_for_idle - wait for VCN block idle
 *
 * @handle: amdgpu_device pointer
 *
 * Wait for VCN block idle
 */
static int vcn_v5_0_0_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret = SOC15_WAIT_ON_RREG(VCN, i, regUVD_STATUS, UVD_STATUS__IDLE,
			UVD_STATUS__IDLE);
		if (ret)
			return ret;
	}

	return ret;
}

/**
 * vcn_v5_0_0_set_clockgating_state - set VCN block clockgating state
 *
 * @handle: amdgpu_device pointer
 * @state: clock gating state
 *
 * Set VCN block clockgating state
 */
static int vcn_v5_0_0_set_clockgating_state(void *handle, enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		if (enable) {
			if (RREG32_SOC15(VCN, i, regUVD_STATUS) != UVD_STATUS__IDLE)
				return -EBUSY;
			vcn_v5_0_0_enable_clock_gating(adev, i);
		} else {
			vcn_v5_0_0_disable_clock_gating(adev, i);
		}
	}

	return 0;
}

/**
 * vcn_v5_0_0_set_powergating_state - set VCN block powergating state
 *
 * @handle: amdgpu_device pointer
 * @state: power gating state
 *
 * Set VCN block powergating state
 */
static int vcn_v5_0_0_set_powergating_state(void *handle, enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	if (state == adev->vcn.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v5_0_0_stop(adev);
	else
		ret = vcn_v5_0_0_start(adev);

	if (!ret)
		adev->vcn.cur_state = state;

	return ret;
}

/**
 * vcn_v5_0_0_process_interrupt - process VCN block interrupt
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @entry: interrupt entry from clients and sources
 *
 * Process VCN block interrupt
 */
static int vcn_v5_0_0_process_interrupt(struct amdgpu_device *adev, struct amdgpu_irq_src *source,
	struct amdgpu_iv_entry *entry)
{
	uint32_t ip_instance;

	switch (entry->client_id) {
	case SOC15_IH_CLIENTID_VCN:
		ip_instance = 0;
		break;
	case SOC15_IH_CLIENTID_VCN1:
		ip_instance = 1;
		break;
	default:
		DRM_ERROR("Unhandled client id: %d\n", entry->client_id);
		return 0;
	}

	DRM_DEBUG("IH: VCN TRAP\n");

	switch (entry->src_id) {
	case VCN_4_0__SRCID__UVD_ENC_GENERAL_PURPOSE:
		amdgpu_fence_process(&adev->vcn.inst[ip_instance].ring_enc[0]);
		break;
	case VCN_4_0__SRCID_UVD_POISON:
		amdgpu_vcn_process_poison_irq(adev, source, entry);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v5_0_0_irq_funcs = {
	.process = vcn_v5_0_0_process_interrupt,
};

/**
 * vcn_v5_0_0_set_irq_funcs - set VCN block interrupt irq functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set VCN block interrupt irq functions
 */
static void vcn_v5_0_0_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst[i].irq.num_types = adev->vcn.num_enc_rings + 1;
		adev->vcn.inst[i].irq.funcs = &vcn_v5_0_0_irq_funcs;
	}
}

static void vcn_v5_0_print_ip_state(void *handle, struct drm_printer *p)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, j;
	uint32_t reg_count = ARRAY_SIZE(vcn_reg_list_5_0);
	uint32_t inst_off, is_powered;

	if (!adev->vcn.ip_dump)
		return;

	drm_printf(p, "num_instances:%d\n", adev->vcn.num_vcn_inst);
	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i)) {
			drm_printf(p, "\nHarvested Instance:VCN%d Skipping dump\n", i);
			continue;
		}

		inst_off = i * reg_count;
		is_powered = (adev->vcn.ip_dump[inst_off] &
				UVD_POWER_STATUS__UVD_POWER_STATUS_MASK) != 1;

		if (is_powered) {
			drm_printf(p, "\nActive Instance:VCN%d\n", i);
			for (j = 0; j < reg_count; j++)
				drm_printf(p, "%-50s \t 0x%08x\n", vcn_reg_list_5_0[j].reg_name,
					   adev->vcn.ip_dump[inst_off + j]);
		} else {
			drm_printf(p, "\nInactive Instance:VCN%d\n", i);
		}
	}
}

static void vcn_v5_0_dump_ip_state(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, j;
	bool is_powered;
	uint32_t inst_off;
	uint32_t reg_count = ARRAY_SIZE(vcn_reg_list_5_0);

	if (!adev->vcn.ip_dump)
		return;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		inst_off = i * reg_count;
		/* mmUVD_POWER_STATUS is always readable and is first element of the array */
		adev->vcn.ip_dump[inst_off] = RREG32_SOC15(VCN, i, regUVD_POWER_STATUS);
		is_powered = (adev->vcn.ip_dump[inst_off] &
				UVD_POWER_STATUS__UVD_POWER_STATUS_MASK) != 1;

		if (is_powered)
			for (j = 1; j < reg_count; j++)
				adev->vcn.ip_dump[inst_off + j] =
					RREG32(SOC15_REG_ENTRY_OFFSET_INST(vcn_reg_list_5_0[j], i));
	}
}

static const struct amd_ip_funcs vcn_v5_0_0_ip_funcs = {
	.name = "vcn_v5_0_0",
	.early_init = vcn_v5_0_0_early_init,
	.late_init = NULL,
	.sw_init = vcn_v5_0_0_sw_init,
	.sw_fini = vcn_v5_0_0_sw_fini,
	.hw_init = vcn_v5_0_0_hw_init,
	.hw_fini = vcn_v5_0_0_hw_fini,
	.suspend = vcn_v5_0_0_suspend,
	.resume = vcn_v5_0_0_resume,
	.is_idle = vcn_v5_0_0_is_idle,
	.wait_for_idle = vcn_v5_0_0_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = vcn_v5_0_0_set_clockgating_state,
	.set_powergating_state = vcn_v5_0_0_set_powergating_state,
	.dump_ip_state = vcn_v5_0_dump_ip_state,
	.print_ip_state = vcn_v5_0_print_ip_state,
};

const struct amdgpu_ip_block_version vcn_v5_0_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 5,
	.minor = 0,
	.rev = 0,
	.funcs = &vcn_v5_0_0_ip_funcs,
};
