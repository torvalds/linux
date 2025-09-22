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
 *
 */

#include "amdgpu.h"
#include "mmhub_v2_3.h"

#include "mmhub/mmhub_2_3_0_offset.h"
#include "mmhub/mmhub_2_3_0_sh_mask.h"
#include "mmhub/mmhub_2_3_0_default.h"
#include "navi10_enum.h"

#include "soc15_common.h"

static const char *mmhub_client_ids_vangogh[][2] = {
	[0][0] = "MP0",
	[1][0] = "MP1",
	[2][0] = "DCEDMC",
	[3][0] = "DCEVGA",
	[13][0] = "UTCL2",
	[26][0] = "OSS",
	[27][0] = "HDP",
	[28][0] = "VCN",
	[29][0] = "VCNU",
	[30][0] = "JPEG",
	[0][1] = "MP0",
	[1][1] = "MP1",
	[2][1] = "DCEDMC",
	[3][1] = "DCEVGA",
	[4][1] = "DCEDWB",
	[5][1] = "XDP",
	[26][1] = "OSS",
	[27][1] = "HDP",
	[28][1] = "VCN",
	[29][1] = "VCNU",
	[30][1] = "JPEG",
};

static uint32_t mmhub_v2_3_get_invalidate_req(unsigned int vmid,
					      uint32_t flush_type)
{
	u32 req = 0;

	/* invalidate using legacy mode on vmid*/
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, FLUSH_TYPE, flush_type);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PTES, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE0, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE1, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE2, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ, INVALIDATE_L1_PTES, 1);
	req = REG_SET_FIELD(req, MMVM_INVALIDATE_ENG0_REQ,
			    CLEAR_PROTECTION_FAULT_STATUS_ADDR,	0);

	return req;
}

static void
mmhub_v2_3_print_l2_protection_fault_status(struct amdgpu_device *adev,
					     uint32_t status)
{
	uint32_t cid, rw;
	const char *mmhub_cid = NULL;

	cid = REG_GET_FIELD(status,
			    MMVM_L2_PROTECTION_FAULT_STATUS, CID);
	rw = REG_GET_FIELD(status,
			   MMVM_L2_PROTECTION_FAULT_STATUS, RW);

	dev_err(adev->dev,
		"MMVM_L2_PROTECTION_FAULT_STATUS:0x%08X\n",
		status);
	switch (amdgpu_ip_version(adev, MMHUB_HWIP, 0)) {
	case IP_VERSION(2, 3, 0):
	case IP_VERSION(2, 4, 0):
	case IP_VERSION(2, 4, 1):
		mmhub_cid = mmhub_client_ids_vangogh[cid][rw];
		break;
	default:
		mmhub_cid = NULL;
		break;
	}
	dev_err(adev->dev, "\t Faulty UTCL2 client ID: %s (0x%x)\n",
		mmhub_cid ? mmhub_cid : "unknown", cid);
	dev_err(adev->dev, "\t MORE_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS, MORE_FAULTS));
	dev_err(adev->dev, "\t WALKER_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS, WALKER_ERROR));
	dev_err(adev->dev, "\t PERMISSION_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS, PERMISSION_FAULTS));
	dev_err(adev->dev, "\t MAPPING_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		MMVM_L2_PROTECTION_FAULT_STATUS, MAPPING_ERROR));
	dev_err(adev->dev, "\t RW: 0x%x\n", rw);
}

static void mmhub_v2_3_setup_vm_pt_regs(struct amdgpu_device *adev,
					uint32_t vmid,
					uint64_t page_table_base)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_MMHUB0(0)];

	WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
			    hub->ctx_addr_distance * vmid, lower_32_bits(page_table_base));

	WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
			    hub->ctx_addr_distance * vmid, upper_32_bits(page_table_base));
}

static void mmhub_v2_3_init_gart_aperture_regs(struct amdgpu_device *adev)
{
	uint64_t pt_base = amdgpu_gmc_pd_addr(adev->gart.bo);

	mmhub_v2_3_setup_vm_pt_regs(adev, 0, pt_base);

	WREG32_SOC15(MMHUB, 0, mmMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,
		     (u32)(adev->gmc.gart_start >> 12));
	WREG32_SOC15(MMHUB, 0, mmMMVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,
		     (u32)(adev->gmc.gart_start >> 44));

	WREG32_SOC15(MMHUB, 0, mmMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
		     (u32)(adev->gmc.gart_end >> 12));
	WREG32_SOC15(MMHUB, 0, mmMMVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
		     (u32)(adev->gmc.gart_end >> 44));
}

static void mmhub_v2_3_init_system_aperture_regs(struct amdgpu_device *adev)
{
	uint64_t value;
	uint32_t tmp;

	/* Disable AGP. */
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_AGP_BASE, 0);
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_AGP_BOT, adev->gmc.agp_start >> 24);
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_AGP_TOP, adev->gmc.agp_end >> 24);

	/* Program the system aperture low logical page number. */
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_SYSTEM_APERTURE_LOW_ADDR,
		     min(adev->gmc.fb_start, adev->gmc.agp_start) >> 18);
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR,
		     max(adev->gmc.fb_end, adev->gmc.agp_end) >> 18);

	/* Set default page address. */
	value = amdgpu_gmc_vram_mc2pa(adev, adev->mem_scratch.gpu_addr);
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB,
		     (u32)(value >> 12));
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB,
		     (u32)(value >> 44));

	/* Program "protection fault". */
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32,
		     (u32)(adev->dummy_page_addr >> 12));
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32,
		     (u32)((u64)adev->dummy_page_addr >> 44));

	tmp = RREG32_SOC15(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_CNTL2);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL2,
			    ACTIVE_PAGE_MIGRATION_PTE_READ_RETRY, 1);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_CNTL2, tmp);
}

static void mmhub_v2_3_init_tlb_regs(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* Setup TLB control */
	tmp = RREG32_SOC15(MMHUB, 0, mmMMMC_VM_MX_L1_TLB_CNTL);

	tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 1);
	tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, SYSTEM_ACCESS_MODE, 3);
	tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
			    ENABLE_ADVANCED_DRIVER_MODEL, 1);
	tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
			    SYSTEM_APERTURE_UNMAPPED_ACCESS, 0);
	tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
			    MTYPE, MTYPE_UC); /* UC, uncached */

	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_MX_L1_TLB_CNTL, tmp);
}

static void mmhub_v2_3_init_cache_regs(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* Setup L2 cache */
	tmp = RREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_CACHE, 1);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_FRAGMENT_PROCESSING, 0);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL,
			    ENABLE_DEFAULT_PAGE_OUT_TO_SYSTEM_MEMORY, 1);
	/* XXX for emulation, Refer to closed source code.*/
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, L2_PDE0_CACHE_TAG_GENERATION_MODE,
			    0);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, PDE_FAULT_CLASSIFICATION, 0);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, CONTEXT1_IDENTITY_ACCESS_MODE, 1);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, IDENTITY_MODE_FRAGMENT_SIZE, 0);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL, tmp);

	tmp = RREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL2);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL2, INVALIDATE_ALL_L1_TLBS, 1);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL2, INVALIDATE_L2_CACHE, 1);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL2, tmp);

	tmp = mmMMVM_L2_CNTL3_DEFAULT;
	if (adev->gmc.translate_further) {
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3, BANK_SELECT, 12);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3,
				    L2_CACHE_BIGK_FRAGMENT_SIZE, 9);
	} else {
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3, BANK_SELECT, 9);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL3,
				    L2_CACHE_BIGK_FRAGMENT_SIZE, 6);
	}
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL3, tmp);

	tmp = mmMMVM_L2_CNTL4_DEFAULT;
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL4, VMC_TAP_PDE_REQUEST_PHYSICAL, 0);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL4, VMC_TAP_PTE_REQUEST_PHYSICAL, 0);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL4, tmp);

	tmp = mmMMVM_L2_CNTL5_DEFAULT;
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL5, L2_CACHE_SMALLK_FRAGMENT_SIZE, 0);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL5, tmp);
}

static void mmhub_v2_3_enable_system_domain(struct amdgpu_device *adev)
{
	uint32_t tmp;

	tmp = RREG32_SOC15(MMHUB, 0, mmMMVM_CONTEXT0_CNTL);
	tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL, ENABLE_CONTEXT, 1);
	tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL, PAGE_TABLE_DEPTH, 0);
	tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT0_CNTL,
			    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT, 0);
	WREG32_SOC15(MMHUB, 0, mmMMVM_CONTEXT0_CNTL, tmp);
}

static void mmhub_v2_3_disable_identity_aperture(struct amdgpu_device *adev)
{
	WREG32_SOC15(MMHUB, 0,
		     mmMMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32,
		     0xFFFFFFFF);
	WREG32_SOC15(MMHUB, 0,
		     mmMMVM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32,
		     0x0000000F);

	WREG32_SOC15(MMHUB, 0,
		     mmMMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32, 0);
	WREG32_SOC15(MMHUB, 0,
		     mmMMVM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32, 0);

	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32,
		     0);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32,
		     0);
}

static void mmhub_v2_3_setup_vmid_config(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_MMHUB0(0)];
	int i;
	uint32_t tmp;

	for (i = 0; i <= 14; i++) {
		tmp = RREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT1_CNTL, i * hub->ctx_distance);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL, ENABLE_CONTEXT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL, PAGE_TABLE_DEPTH,
				    adev->vm_manager.num_level);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT,
				    1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    READ_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    PAGE_TABLE_BLOCK_SIZE,
				    adev->vm_manager.block_size - 9);
		/* Send no-retry XNACK on fault to suppress VM fault storm. */
		tmp = REG_SET_FIELD(tmp, MMVM_CONTEXT1_CNTL,
				    RETRY_PERMISSION_OR_INVALID_PAGE_FAULT,
				    !adev->gmc.noretry);
		WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT1_CNTL,
				    i * hub->ctx_distance, tmp);
		WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT1_PAGE_TABLE_START_ADDR_LO32,
				    i * hub->ctx_addr_distance, 0);
		WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT1_PAGE_TABLE_START_ADDR_HI32,
				    i * hub->ctx_addr_distance, 0);
		WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT1_PAGE_TABLE_END_ADDR_LO32,
				    i * hub->ctx_addr_distance,
				    lower_32_bits(adev->vm_manager.max_pfn - 1));
		WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT1_PAGE_TABLE_END_ADDR_HI32,
				    i * hub->ctx_addr_distance,
				    upper_32_bits(adev->vm_manager.max_pfn - 1));
	}

	hub->vm_cntx_cntl = tmp;
}

static void mmhub_v2_3_program_invalidation(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_MMHUB0(0)];
	unsigned int i;

	for (i = 0; i < 18; ++i) {
		WREG32_SOC15_OFFSET(MMHUB, 0,
				    mmMMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32,
				    i * hub->eng_addr_distance, 0xffffffff);
		WREG32_SOC15_OFFSET(MMHUB, 0,
				    mmMMVM_INVALIDATE_ENG0_ADDR_RANGE_HI32,
				    i * hub->eng_addr_distance, 0x1f);
	}
}

static int mmhub_v2_3_gart_enable(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev)) {
		/*
		 * MMMC_VM_FB_LOCATION_BASE/TOP is NULL for VF, becuase they are
		 * VF copy registers so vbios post doesn't program them, for
		 * SRIOV driver need to program them
		 */
		WREG32_SOC15(MMHUB, 0, mmMMMC_VM_FB_LOCATION_BASE,
			     adev->gmc.vram_start >> 24);
		WREG32_SOC15(MMHUB, 0, mmMMMC_VM_FB_LOCATION_TOP,
			     adev->gmc.vram_end >> 24);
	}

	/* GART Enable. */
	mmhub_v2_3_init_gart_aperture_regs(adev);
	mmhub_v2_3_init_system_aperture_regs(adev);
	mmhub_v2_3_init_tlb_regs(adev);
	mmhub_v2_3_init_cache_regs(adev);

	mmhub_v2_3_enable_system_domain(adev);
	mmhub_v2_3_disable_identity_aperture(adev);
	mmhub_v2_3_setup_vmid_config(adev);
	mmhub_v2_3_program_invalidation(adev);

	return 0;
}

static void mmhub_v2_3_gart_disable(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_MMHUB0(0)];
	u32 tmp;
	u32 i;

	/* Disable all tables */
	for (i = 0; i < AMDGPU_NUM_VMID; i++)
		WREG32_SOC15_OFFSET(MMHUB, 0, mmMMVM_CONTEXT0_CNTL,
				    i * hub->ctx_distance, 0);

	/* Setup TLB control */
	tmp = RREG32_SOC15(MMHUB, 0, mmMMMC_VM_MX_L1_TLB_CNTL);
	tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL, ENABLE_L1_TLB, 0);
	tmp = REG_SET_FIELD(tmp, MMMC_VM_MX_L1_TLB_CNTL,
			    ENABLE_ADVANCED_DRIVER_MODEL, 0);
	WREG32_SOC15(MMHUB, 0, mmMMMC_VM_MX_L1_TLB_CNTL, tmp);

	/* Setup L2 cache */
	tmp = RREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_CNTL, ENABLE_L2_CACHE, 0);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL, tmp);
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_CNTL3, 0);
}

/**
 * mmhub_v2_3_set_fault_enable_default - update GART/VM fault handling
 *
 * @adev: amdgpu_device pointer
 * @value: true redirects VM faults to the default page
 */
static void mmhub_v2_3_set_fault_enable_default(struct amdgpu_device *adev,
						bool value)
{
	u32 tmp;

	tmp = RREG32_SOC15(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_CNTL);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    RANGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    PDE0_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    PDE1_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    PDE2_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    TRANSLATE_FURTHER_PROTECTION_FAULT_ENABLE_DEFAULT,
			    value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    NACK_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    DUMMY_PAGE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    VALID_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    READ_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    WRITE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
			    EXECUTE_PROTECTION_FAULT_ENABLE_DEFAULT, value);
	if (!value) {
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
				CRASH_ON_NO_RETRY_FAULT, 1);
		tmp = REG_SET_FIELD(tmp, MMVM_L2_PROTECTION_FAULT_CNTL,
				CRASH_ON_RETRY_FAULT, 1);
	}
	WREG32_SOC15(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_CNTL, tmp);
}

static const struct amdgpu_vmhub_funcs mmhub_v2_3_vmhub_funcs = {
	.print_l2_protection_fault_status = mmhub_v2_3_print_l2_protection_fault_status,
	.get_invalidate_req = mmhub_v2_3_get_invalidate_req,
};

static void mmhub_v2_3_init(struct amdgpu_device *adev)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_MMHUB0(0)];

	hub->ctx0_ptb_addr_lo32 =
		SOC15_REG_OFFSET(MMHUB, 0,
				 mmMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32);
	hub->ctx0_ptb_addr_hi32 =
		SOC15_REG_OFFSET(MMHUB, 0,
				 mmMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32);
	hub->vm_inv_eng0_sem =
		SOC15_REG_OFFSET(MMHUB, 0,
				 mmMMVM_INVALIDATE_ENG0_SEM);
	hub->vm_inv_eng0_req =
		SOC15_REG_OFFSET(MMHUB, 0, mmMMVM_INVALIDATE_ENG0_REQ);
	hub->vm_inv_eng0_ack =
		SOC15_REG_OFFSET(MMHUB, 0, mmMMVM_INVALIDATE_ENG0_ACK);
	hub->vm_context0_cntl =
		SOC15_REG_OFFSET(MMHUB, 0, mmMMVM_CONTEXT0_CNTL);
	hub->vm_l2_pro_fault_status =
		SOC15_REG_OFFSET(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_STATUS);
	hub->vm_l2_pro_fault_cntl =
		SOC15_REG_OFFSET(MMHUB, 0, mmMMVM_L2_PROTECTION_FAULT_CNTL);

	hub->ctx_distance = mmMMVM_CONTEXT1_CNTL - mmMMVM_CONTEXT0_CNTL;
	hub->ctx_addr_distance = mmMMVM_CONTEXT1_PAGE_TABLE_BASE_ADDR_LO32 -
		mmMMVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32;
	hub->eng_distance = mmMMVM_INVALIDATE_ENG1_REQ -
		mmMMVM_INVALIDATE_ENG0_REQ;
	hub->eng_addr_distance = mmMMVM_INVALIDATE_ENG1_ADDR_RANGE_LO32 -
		mmMMVM_INVALIDATE_ENG0_ADDR_RANGE_LO32;

	hub->vm_cntx_cntl_vm_fault = MMVM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		MMVM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

	hub->vmhub_funcs = &mmhub_v2_3_vmhub_funcs;
}

static void
mmhub_v2_3_update_medium_grain_clock_gating(struct amdgpu_device *adev,
					    bool enable)
{
	uint32_t def, data, def1, data1;

	def  = data  = RREG32_SOC15(MMHUB, 0, mmMM_ATC_L2_CGTT_CLK_CTRL);
	def1 = data1 = RREG32_SOC15(MMHUB, 0, mmDAGB0_CNTL_MISC2);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_MC_MGCG)) {
		data &= ~MM_ATC_L2_CGTT_CLK_CTRL__SOFT_OVERRIDE_MASK;
		data1 &= ~(DAGB0_CNTL_MISC2__DISABLE_WRREQ_CG_MASK |
			   DAGB0_CNTL_MISC2__DISABLE_WRRET_CG_MASK |
			   DAGB0_CNTL_MISC2__DISABLE_RDREQ_CG_MASK |
			   DAGB0_CNTL_MISC2__DISABLE_RDRET_CG_MASK |
			   DAGB0_CNTL_MISC2__DISABLE_TLBWR_CG_MASK |
			   DAGB0_CNTL_MISC2__DISABLE_TLBRD_CG_MASK);

	} else {
		data |= MM_ATC_L2_CGTT_CLK_CTRL__SOFT_OVERRIDE_MASK;
		data1 |= (DAGB0_CNTL_MISC2__DISABLE_WRREQ_CG_MASK |
			  DAGB0_CNTL_MISC2__DISABLE_WRRET_CG_MASK |
			  DAGB0_CNTL_MISC2__DISABLE_RDREQ_CG_MASK |
			  DAGB0_CNTL_MISC2__DISABLE_RDRET_CG_MASK |
			  DAGB0_CNTL_MISC2__DISABLE_TLBWR_CG_MASK |
			  DAGB0_CNTL_MISC2__DISABLE_TLBRD_CG_MASK);
	}

	if (def != data)
		WREG32_SOC15(MMHUB, 0, mmMM_ATC_L2_CGTT_CLK_CTRL, data);
	if (def1 != data1)
		WREG32_SOC15(MMHUB, 0, mmDAGB0_CNTL_MISC2, data1);
}

static void
mmhub_v2_3_update_medium_grain_light_sleep(struct amdgpu_device *adev,
					   bool enable)
{
	uint32_t def, data, def1, data1, def2, data2;

	def  = data  = RREG32_SOC15(MMHUB, 0, mmMM_ATC_L2_CGTT_CLK_CTRL);
	def1 = data1 = RREG32_SOC15(MMHUB, 0, mmDAGB0_WR_CGTT_CLK_CTRL);
	def2 = data2 = RREG32_SOC15(MMHUB, 0, mmDAGB0_RD_CGTT_CLK_CTRL);

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_MC_LS)) {
		data &= ~MM_ATC_L2_CGTT_CLK_CTRL__MGLS_OVERRIDE_MASK;
		data1 &= ~(DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_WRITE_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_READ_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_RETURN_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_REGISTER_MASK);
		data2 &= ~(DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_WRITE_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_READ_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_RETURN_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_REGISTER_MASK);
	} else {
		data |= MM_ATC_L2_CGTT_CLK_CTRL__MGLS_OVERRIDE_MASK;
		data1 |= (DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_WRITE_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_READ_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_RETURN_MASK |
			DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_REGISTER_MASK);
		data2 |= (DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_WRITE_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_READ_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_RETURN_MASK |
			DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_REGISTER_MASK);
	}

	if (def != data)
		WREG32_SOC15(MMHUB, 0, mmMM_ATC_L2_CGTT_CLK_CTRL, data);
	if (def1 != data1)
		WREG32_SOC15(MMHUB, 0, mmDAGB0_WR_CGTT_CLK_CTRL, data1);
	if (def2 != data2)
		WREG32_SOC15(MMHUB, 0, mmDAGB0_RD_CGTT_CLK_CTRL, data2);
}

static int mmhub_v2_3_set_clockgating(struct amdgpu_device *adev,
				      enum amd_clockgating_state state)
{
	if (amdgpu_sriov_vf(adev))
		return 0;

	mmhub_v2_3_update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
	mmhub_v2_3_update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);

	return 0;
}

static void mmhub_v2_3_get_clockgating(struct amdgpu_device *adev, u64 *flags)
{
	int data, data1, data2, data3;

	if (amdgpu_sriov_vf(adev))
		*flags = 0;

	data = RREG32_SOC15(MMHUB, 0, mmDAGB0_CNTL_MISC2);
	data1  = RREG32_SOC15(MMHUB, 0, mmMM_ATC_L2_CGTT_CLK_CTRL);
	data2 = RREG32_SOC15(MMHUB, 0, mmDAGB0_WR_CGTT_CLK_CTRL);
	data3 = RREG32_SOC15(MMHUB, 0, mmDAGB0_RD_CGTT_CLK_CTRL);

	/* AMD_CG_SUPPORT_MC_MGCG */
	if (!(data & (DAGB0_CNTL_MISC2__DISABLE_WRREQ_CG_MASK |
			DAGB0_CNTL_MISC2__DISABLE_WRRET_CG_MASK |
			DAGB0_CNTL_MISC2__DISABLE_RDREQ_CG_MASK |
			DAGB0_CNTL_MISC2__DISABLE_RDRET_CG_MASK |
			DAGB0_CNTL_MISC2__DISABLE_TLBWR_CG_MASK |
			DAGB0_CNTL_MISC2__DISABLE_TLBRD_CG_MASK))
		&& !(data1 & MM_ATC_L2_CGTT_CLK_CTRL__SOFT_OVERRIDE_MASK)) {
		*flags |= AMD_CG_SUPPORT_MC_MGCG;
	}

	/* AMD_CG_SUPPORT_MC_LS */
	if (!(data1 & MM_ATC_L2_CGTT_CLK_CTRL__MGLS_OVERRIDE_MASK)
		&& !(data2 & (DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_MASK |
				DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_WRITE_MASK |
				DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_READ_MASK |
				DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_RETURN_MASK |
				DAGB0_WR_CGTT_CLK_CTRL__LS_OVERRIDE_REGISTER_MASK))
		&& !(data3 & (DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_MASK |
				DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_WRITE_MASK |
				DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_READ_MASK |
				DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_RETURN_MASK |
				DAGB0_RD_CGTT_CLK_CTRL__LS_OVERRIDE_REGISTER_MASK)))
		*flags |= AMD_CG_SUPPORT_MC_LS;
}

const struct amdgpu_mmhub_funcs mmhub_v2_3_funcs = {
	.init = mmhub_v2_3_init,
	.gart_enable = mmhub_v2_3_gart_enable,
	.set_fault_enable_default = mmhub_v2_3_set_fault_enable_default,
	.gart_disable = mmhub_v2_3_gart_disable,
	.set_clockgating = mmhub_v2_3_set_clockgating,
	.get_clockgating = mmhub_v2_3_get_clockgating,
	.setup_vm_pt_regs = mmhub_v2_3_setup_vm_pt_regs,
};
