/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include <linux/pci.h>

#include <drm/drm_cache.h>

#include "amdgpu.h"
#include "gmc_v9_0.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_gem.h"

#include "gc/gc_9_0_sh_mask.h"
#include "dce/dce_12_0_offset.h"
#include "dce/dce_12_0_sh_mask.h"
#include "vega10_enum.h"
#include "mmhub/mmhub_1_0_offset.h"
#include "athub/athub_1_0_sh_mask.h"
#include "athub/athub_1_0_offset.h"
#include "oss/osssys_4_0_offset.h"

#include "soc15.h"
#include "soc15d.h"
#include "soc15_common.h"
#include "umc/umc_6_0_sh_mask.h"

#include "gfxhub_v1_0.h"
#include "mmhub_v1_0.h"
#include "athub_v1_0.h"
#include "gfxhub_v1_1.h"
#include "gfxhub_v1_2.h"
#include "mmhub_v9_4.h"
#include "mmhub_v1_7.h"
#include "mmhub_v1_8.h"
#include "umc_v6_1.h"
#include "umc_v6_0.h"
#include "umc_v6_7.h"
#include "umc_v12_0.h"
#include "hdp_v4_0.h"
#include "mca_v3_0.h"

#include "ivsrcid/vmc/irqsrcs_vmc_1_0.h"

#include "amdgpu_ras.h"
#include "amdgpu_xgmi.h"

/* add these here since we already include dce12 headers and these are for DCN */
#define mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION                                                          0x055d
#define mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION_BASE_IDX                                                 2
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_WIDTH__SHIFT                                        0x0
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_HEIGHT__SHIFT                                       0x10
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_WIDTH_MASK                                          0x00003FFFL
#define HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION__PRI_VIEWPORT_HEIGHT_MASK                                         0x3FFF0000L
#define mmDCHUBBUB_SDPIF_MMIO_CNTRL_0                                                                  0x049d
#define mmDCHUBBUB_SDPIF_MMIO_CNTRL_0_BASE_IDX                                                         2

#define mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION_DCN2                                                          0x05ea
#define mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION_DCN2_BASE_IDX                                                 2

#define MAX_MEM_RANGES 8

static const char * const gfxhub_client_ids[] = {
	"CB",
	"DB",
	"IA",
	"WD",
	"CPF",
	"CPC",
	"CPG",
	"RLC",
	"TCP",
	"SQC (inst)",
	"SQC (data)",
	"SQG",
	"PA",
};

static const char *mmhub_client_ids_raven[][2] = {
	[0][0] = "MP1",
	[1][0] = "MP0",
	[2][0] = "VCN",
	[3][0] = "VCNU",
	[4][0] = "HDP",
	[5][0] = "DCE",
	[13][0] = "UTCL2",
	[19][0] = "TLS",
	[26][0] = "OSS",
	[27][0] = "SDMA0",
	[0][1] = "MP1",
	[1][1] = "MP0",
	[2][1] = "VCN",
	[3][1] = "VCNU",
	[4][1] = "HDP",
	[5][1] = "XDP",
	[6][1] = "DBGU0",
	[7][1] = "DCE",
	[8][1] = "DCEDWB0",
	[9][1] = "DCEDWB1",
	[26][1] = "OSS",
	[27][1] = "SDMA0",
};

static const char *mmhub_client_ids_renoir[][2] = {
	[0][0] = "MP1",
	[1][0] = "MP0",
	[2][0] = "HDP",
	[4][0] = "DCEDMC",
	[5][0] = "DCEVGA",
	[13][0] = "UTCL2",
	[19][0] = "TLS",
	[26][0] = "OSS",
	[27][0] = "SDMA0",
	[28][0] = "VCN",
	[29][0] = "VCNU",
	[30][0] = "JPEG",
	[0][1] = "MP1",
	[1][1] = "MP0",
	[2][1] = "HDP",
	[3][1] = "XDP",
	[6][1] = "DBGU0",
	[7][1] = "DCEDMC",
	[8][1] = "DCEVGA",
	[9][1] = "DCEDWB",
	[26][1] = "OSS",
	[27][1] = "SDMA0",
	[28][1] = "VCN",
	[29][1] = "VCNU",
	[30][1] = "JPEG",
};

static const char *mmhub_client_ids_vega10[][2] = {
	[0][0] = "MP0",
	[1][0] = "UVD",
	[2][0] = "UVDU",
	[3][0] = "HDP",
	[13][0] = "UTCL2",
	[14][0] = "OSS",
	[15][0] = "SDMA1",
	[32+0][0] = "VCE0",
	[32+1][0] = "VCE0U",
	[32+2][0] = "XDMA",
	[32+3][0] = "DCE",
	[32+4][0] = "MP1",
	[32+14][0] = "SDMA0",
	[0][1] = "MP0",
	[1][1] = "UVD",
	[2][1] = "UVDU",
	[3][1] = "DBGU0",
	[4][1] = "HDP",
	[5][1] = "XDP",
	[14][1] = "OSS",
	[15][1] = "SDMA0",
	[32+0][1] = "VCE0",
	[32+1][1] = "VCE0U",
	[32+2][1] = "XDMA",
	[32+3][1] = "DCE",
	[32+4][1] = "DCEDWB",
	[32+5][1] = "MP1",
	[32+6][1] = "DBGU1",
	[32+14][1] = "SDMA1",
};

static const char *mmhub_client_ids_vega12[][2] = {
	[0][0] = "MP0",
	[1][0] = "VCE0",
	[2][0] = "VCE0U",
	[3][0] = "HDP",
	[13][0] = "UTCL2",
	[14][0] = "OSS",
	[15][0] = "SDMA1",
	[32+0][0] = "DCE",
	[32+1][0] = "XDMA",
	[32+2][0] = "UVD",
	[32+3][0] = "UVDU",
	[32+4][0] = "MP1",
	[32+15][0] = "SDMA0",
	[0][1] = "MP0",
	[1][1] = "VCE0",
	[2][1] = "VCE0U",
	[3][1] = "DBGU0",
	[4][1] = "HDP",
	[5][1] = "XDP",
	[14][1] = "OSS",
	[15][1] = "SDMA0",
	[32+0][1] = "DCE",
	[32+1][1] = "DCEDWB",
	[32+2][1] = "XDMA",
	[32+3][1] = "UVD",
	[32+4][1] = "UVDU",
	[32+5][1] = "MP1",
	[32+6][1] = "DBGU1",
	[32+15][1] = "SDMA1",
};

static const char *mmhub_client_ids_vega20[][2] = {
	[0][0] = "XDMA",
	[1][0] = "DCE",
	[2][0] = "VCE0",
	[3][0] = "VCE0U",
	[4][0] = "UVD",
	[5][0] = "UVD1U",
	[13][0] = "OSS",
	[14][0] = "HDP",
	[15][0] = "SDMA0",
	[32+0][0] = "UVD",
	[32+1][0] = "UVDU",
	[32+2][0] = "MP1",
	[32+3][0] = "MP0",
	[32+12][0] = "UTCL2",
	[32+14][0] = "SDMA1",
	[0][1] = "XDMA",
	[1][1] = "DCE",
	[2][1] = "DCEDWB",
	[3][1] = "VCE0",
	[4][1] = "VCE0U",
	[5][1] = "UVD1",
	[6][1] = "UVD1U",
	[7][1] = "DBGU0",
	[8][1] = "XDP",
	[13][1] = "OSS",
	[14][1] = "HDP",
	[15][1] = "SDMA0",
	[32+0][1] = "UVD",
	[32+1][1] = "UVDU",
	[32+2][1] = "DBGU1",
	[32+3][1] = "MP1",
	[32+4][1] = "MP0",
	[32+14][1] = "SDMA1",
};

static const char *mmhub_client_ids_arcturus[][2] = {
	[0][0] = "DBGU1",
	[1][0] = "XDP",
	[2][0] = "MP1",
	[14][0] = "HDP",
	[171][0] = "JPEG",
	[172][0] = "VCN",
	[173][0] = "VCNU",
	[203][0] = "JPEG1",
	[204][0] = "VCN1",
	[205][0] = "VCN1U",
	[256][0] = "SDMA0",
	[257][0] = "SDMA1",
	[258][0] = "SDMA2",
	[259][0] = "SDMA3",
	[260][0] = "SDMA4",
	[261][0] = "SDMA5",
	[262][0] = "SDMA6",
	[263][0] = "SDMA7",
	[384][0] = "OSS",
	[0][1] = "DBGU1",
	[1][1] = "XDP",
	[2][1] = "MP1",
	[14][1] = "HDP",
	[171][1] = "JPEG",
	[172][1] = "VCN",
	[173][1] = "VCNU",
	[203][1] = "JPEG1",
	[204][1] = "VCN1",
	[205][1] = "VCN1U",
	[256][1] = "SDMA0",
	[257][1] = "SDMA1",
	[258][1] = "SDMA2",
	[259][1] = "SDMA3",
	[260][1] = "SDMA4",
	[261][1] = "SDMA5",
	[262][1] = "SDMA6",
	[263][1] = "SDMA7",
	[384][1] = "OSS",
};

static const char *mmhub_client_ids_aldebaran[][2] = {
	[2][0] = "MP1",
	[3][0] = "MP0",
	[32+1][0] = "DBGU_IO0",
	[32+2][0] = "DBGU_IO2",
	[32+4][0] = "MPIO",
	[96+11][0] = "JPEG0",
	[96+12][0] = "VCN0",
	[96+13][0] = "VCNU0",
	[128+11][0] = "JPEG1",
	[128+12][0] = "VCN1",
	[128+13][0] = "VCNU1",
	[160+1][0] = "XDP",
	[160+14][0] = "HDP",
	[256+0][0] = "SDMA0",
	[256+1][0] = "SDMA1",
	[256+2][0] = "SDMA2",
	[256+3][0] = "SDMA3",
	[256+4][0] = "SDMA4",
	[384+0][0] = "OSS",
	[2][1] = "MP1",
	[3][1] = "MP0",
	[32+1][1] = "DBGU_IO0",
	[32+2][1] = "DBGU_IO2",
	[32+4][1] = "MPIO",
	[96+11][1] = "JPEG0",
	[96+12][1] = "VCN0",
	[96+13][1] = "VCNU0",
	[128+11][1] = "JPEG1",
	[128+12][1] = "VCN1",
	[128+13][1] = "VCNU1",
	[160+1][1] = "XDP",
	[160+14][1] = "HDP",
	[256+0][1] = "SDMA0",
	[256+1][1] = "SDMA1",
	[256+2][1] = "SDMA2",
	[256+3][1] = "SDMA3",
	[256+4][1] = "SDMA4",
	[384+0][1] = "OSS",
};

static const struct soc15_reg_golden golden_settings_mmhub_1_0_0[] = {
	SOC15_REG_GOLDEN_VALUE(MMHUB, 0, mmDAGB1_WRCLI2, 0x00000007, 0xfe5fe0fa),
	SOC15_REG_GOLDEN_VALUE(MMHUB, 0, mmMMEA1_DRAM_WR_CLI2GRP_MAP0, 0x00000030, 0x55555565)
};

static const struct soc15_reg_golden golden_settings_athub_1_0_0[] = {
	SOC15_REG_GOLDEN_VALUE(ATHUB, 0, mmRPB_ARB_CNTL, 0x0000ff00, 0x00000800),
	SOC15_REG_GOLDEN_VALUE(ATHUB, 0, mmRPB_ARB_CNTL2, 0x00ff00ff, 0x00080008)
};

static const uint32_t ecc_umc_mcumc_ctrl_addrs[] = {
	(0x000143c0 + 0x00000000),
	(0x000143c0 + 0x00000800),
	(0x000143c0 + 0x00001000),
	(0x000143c0 + 0x00001800),
	(0x000543c0 + 0x00000000),
	(0x000543c0 + 0x00000800),
	(0x000543c0 + 0x00001000),
	(0x000543c0 + 0x00001800),
	(0x000943c0 + 0x00000000),
	(0x000943c0 + 0x00000800),
	(0x000943c0 + 0x00001000),
	(0x000943c0 + 0x00001800),
	(0x000d43c0 + 0x00000000),
	(0x000d43c0 + 0x00000800),
	(0x000d43c0 + 0x00001000),
	(0x000d43c0 + 0x00001800),
	(0x001143c0 + 0x00000000),
	(0x001143c0 + 0x00000800),
	(0x001143c0 + 0x00001000),
	(0x001143c0 + 0x00001800),
	(0x001543c0 + 0x00000000),
	(0x001543c0 + 0x00000800),
	(0x001543c0 + 0x00001000),
	(0x001543c0 + 0x00001800),
	(0x001943c0 + 0x00000000),
	(0x001943c0 + 0x00000800),
	(0x001943c0 + 0x00001000),
	(0x001943c0 + 0x00001800),
	(0x001d43c0 + 0x00000000),
	(0x001d43c0 + 0x00000800),
	(0x001d43c0 + 0x00001000),
	(0x001d43c0 + 0x00001800),
};

static const uint32_t ecc_umc_mcumc_ctrl_mask_addrs[] = {
	(0x000143e0 + 0x00000000),
	(0x000143e0 + 0x00000800),
	(0x000143e0 + 0x00001000),
	(0x000143e0 + 0x00001800),
	(0x000543e0 + 0x00000000),
	(0x000543e0 + 0x00000800),
	(0x000543e0 + 0x00001000),
	(0x000543e0 + 0x00001800),
	(0x000943e0 + 0x00000000),
	(0x000943e0 + 0x00000800),
	(0x000943e0 + 0x00001000),
	(0x000943e0 + 0x00001800),
	(0x000d43e0 + 0x00000000),
	(0x000d43e0 + 0x00000800),
	(0x000d43e0 + 0x00001000),
	(0x000d43e0 + 0x00001800),
	(0x001143e0 + 0x00000000),
	(0x001143e0 + 0x00000800),
	(0x001143e0 + 0x00001000),
	(0x001143e0 + 0x00001800),
	(0x001543e0 + 0x00000000),
	(0x001543e0 + 0x00000800),
	(0x001543e0 + 0x00001000),
	(0x001543e0 + 0x00001800),
	(0x001943e0 + 0x00000000),
	(0x001943e0 + 0x00000800),
	(0x001943e0 + 0x00001000),
	(0x001943e0 + 0x00001800),
	(0x001d43e0 + 0x00000000),
	(0x001d43e0 + 0x00000800),
	(0x001d43e0 + 0x00001000),
	(0x001d43e0 + 0x00001800),
};

static int gmc_v9_0_ecc_interrupt_state(struct amdgpu_device *adev,
		struct amdgpu_irq_src *src,
		unsigned int type,
		enum amdgpu_interrupt_state state)
{
	u32 bits, i, tmp, reg;

	/* Devices newer then VEGA10/12 shall have these programming
	 * sequences performed by PSP BL
	 */
	if (adev->asic_type >= CHIP_VEGA20)
		return 0;

	bits = 0x7f;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		for (i = 0; i < ARRAY_SIZE(ecc_umc_mcumc_ctrl_addrs); i++) {
			reg = ecc_umc_mcumc_ctrl_addrs[i];
			tmp = RREG32(reg);
			tmp &= ~bits;
			WREG32(reg, tmp);
		}
		for (i = 0; i < ARRAY_SIZE(ecc_umc_mcumc_ctrl_mask_addrs); i++) {
			reg = ecc_umc_mcumc_ctrl_mask_addrs[i];
			tmp = RREG32(reg);
			tmp &= ~bits;
			WREG32(reg, tmp);
		}
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		for (i = 0; i < ARRAY_SIZE(ecc_umc_mcumc_ctrl_addrs); i++) {
			reg = ecc_umc_mcumc_ctrl_addrs[i];
			tmp = RREG32(reg);
			tmp |= bits;
			WREG32(reg, tmp);
		}
		for (i = 0; i < ARRAY_SIZE(ecc_umc_mcumc_ctrl_mask_addrs); i++) {
			reg = ecc_umc_mcumc_ctrl_mask_addrs[i];
			tmp = RREG32(reg);
			tmp |= bits;
			WREG32(reg, tmp);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int gmc_v9_0_vm_fault_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *src,
					unsigned int type,
					enum amdgpu_interrupt_state state)
{
	struct amdgpu_vmhub *hub;
	u32 tmp, reg, bits, i, j;

	bits = VM_CONTEXT1_CNTL__RANGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__DUMMY_PAGE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__PDE0_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__VALID_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__READ_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__WRITE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK |
		VM_CONTEXT1_CNTL__EXECUTE_PROTECTION_FAULT_ENABLE_INTERRUPT_MASK;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		for_each_set_bit(j, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS) {
			hub = &adev->vmhub[j];
			for (i = 0; i < 16; i++) {
				reg = hub->vm_context0_cntl + i;

				/* This works because this interrupt is only
				 * enabled at init/resume and disabled in
				 * fini/suspend, so the overall state doesn't
				 * change over the course of suspend/resume.
				 */
				if (adev->in_s0ix && (j == AMDGPU_GFXHUB(0)))
					continue;

				if (j >= AMDGPU_MMHUB0(0))
					tmp = RREG32_SOC15_IP(MMHUB, reg);
				else
					tmp = RREG32_XCC(reg, j);

				tmp &= ~bits;

				if (j >= AMDGPU_MMHUB0(0))
					WREG32_SOC15_IP(MMHUB, reg, tmp);
				else
					WREG32_XCC(reg, tmp, j);
			}
		}
		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		for_each_set_bit(j, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS) {
			hub = &adev->vmhub[j];
			for (i = 0; i < 16; i++) {
				reg = hub->vm_context0_cntl + i;

				/* This works because this interrupt is only
				 * enabled at init/resume and disabled in
				 * fini/suspend, so the overall state doesn't
				 * change over the course of suspend/resume.
				 */
				if (adev->in_s0ix && (j == AMDGPU_GFXHUB(0)))
					continue;

				if (j >= AMDGPU_MMHUB0(0))
					tmp = RREG32_SOC15_IP(MMHUB, reg);
				else
					tmp = RREG32_XCC(reg, j);

				tmp |= bits;

				if (j >= AMDGPU_MMHUB0(0))
					WREG32_SOC15_IP(MMHUB, reg, tmp);
				else
					WREG32_XCC(reg, tmp, j);
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

static int gmc_v9_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	bool retry_fault = !!(entry->src_data[1] & 0x80);
	bool write_fault = !!(entry->src_data[1] & 0x20);
	uint32_t status = 0, cid = 0, rw = 0, fed = 0;
	struct amdgpu_task_info *task_info;
	struct amdgpu_vmhub *hub;
	const char *mmhub_cid;
	const char *hub_name;
	unsigned int vmhub;
	u64 addr;
	uint32_t cam_index = 0;
	int ret, xcc_id = 0;
	uint32_t node_id;

	node_id = entry->node_id;

	addr = (u64)entry->src_data[0] << 12;
	addr |= ((u64)entry->src_data[1] & 0xf) << 44;

	if (entry->client_id == SOC15_IH_CLIENTID_VMC) {
		hub_name = "mmhub0";
		vmhub = AMDGPU_MMHUB0(node_id / 4);
	} else if (entry->client_id == SOC15_IH_CLIENTID_VMC1) {
		hub_name = "mmhub1";
		vmhub = AMDGPU_MMHUB1(0);
	} else {
		hub_name = "gfxhub0";
		if (adev->gfx.funcs->ih_node_to_logical_xcc) {
			xcc_id = adev->gfx.funcs->ih_node_to_logical_xcc(adev,
				node_id);
			if (xcc_id < 0)
				xcc_id = 0;
		}
		vmhub = xcc_id;
	}
	hub = &adev->vmhub[vmhub];

	if (retry_fault) {
		if (adev->irq.retry_cam_enabled) {
			/* Delegate it to a different ring if the hardware hasn't
			 * already done it.
			 */
			if (entry->ih == &adev->irq.ih) {
				amdgpu_irq_delegate(adev, entry, 8);
				return 1;
			}

			cam_index = entry->src_data[2] & 0x3ff;

			ret = amdgpu_vm_handle_fault(adev, entry->pasid, entry->vmid, node_id,
						     addr, entry->timestamp, write_fault);
			WDOORBELL32(adev->irq.retry_cam_doorbell_index, cam_index);
			if (ret)
				return 1;
		} else {
			/* Process it onyl if it's the first fault for this address */
			if (entry->ih != &adev->irq.ih_soft &&
			    amdgpu_gmc_filter_faults(adev, entry->ih, addr, entry->pasid,
					     entry->timestamp))
				return 1;

			/* Delegate it to a different ring if the hardware hasn't
			 * already done it.
			 */
			if (entry->ih == &adev->irq.ih) {
				amdgpu_irq_delegate(adev, entry, 8);
				return 1;
			}

			/* Try to handle the recoverable page faults by filling page
			 * tables
			 */
			if (amdgpu_vm_handle_fault(adev, entry->pasid, entry->vmid, node_id,
						   addr, entry->timestamp, write_fault))
				return 1;
		}
	}

	if (!printk_ratelimit())
		return 0;

	dev_err(adev->dev,
		"[%s] %s page fault (src_id:%u ring:%u vmid:%u pasid:%u)\n", hub_name,
		retry_fault ? "retry" : "no-retry",
		entry->src_id, entry->ring_id, entry->vmid, entry->pasid);

	task_info = amdgpu_vm_get_task_info_pasid(adev, entry->pasid);
	if (task_info) {
		dev_err(adev->dev,
			" for process %s pid %d thread %s pid %d)\n",
			task_info->process_name, task_info->tgid,
			task_info->task_name, task_info->pid);
		amdgpu_vm_put_task_info(task_info);
	}

	dev_err(adev->dev, "  in page starting at address 0x%016llx from IH client 0x%x (%s)\n",
		addr, entry->client_id,
		soc15_ih_clientid_name[entry->client_id]);

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4))
		dev_err(adev->dev, "  cookie node_id %d fault from die %s%d%s\n",
			node_id, node_id % 4 == 3 ? "RSV" : "AID", node_id / 4,
			node_id % 4 == 1 ? ".XCD0" : node_id % 4 == 2 ? ".XCD1" : "");

	if (amdgpu_sriov_vf(adev))
		return 0;

	/*
	 * Issue a dummy read to wait for the status register to
	 * be updated to avoid reading an incorrect value due to
	 * the new fast GRBM interface.
	 */
	if ((entry->vmid_src == AMDGPU_GFXHUB(0)) &&
	    (amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(9, 4, 2)))
		RREG32(hub->vm_l2_pro_fault_status);

	status = RREG32(hub->vm_l2_pro_fault_status);
	cid = REG_GET_FIELD(status, VM_L2_PROTECTION_FAULT_STATUS, CID);
	rw = REG_GET_FIELD(status, VM_L2_PROTECTION_FAULT_STATUS, RW);
	fed = REG_GET_FIELD(status, VM_L2_PROTECTION_FAULT_STATUS, FED);

	/* for fed error, kfd will handle it, return directly */
	if (fed && amdgpu_ras_is_poison_mode_supported(adev) &&
	    (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(9, 4, 2)))
		return 0;

	if (!amdgpu_sriov_vf(adev))
		WREG32_P(hub->vm_l2_pro_fault_cntl, 1, ~1);

	amdgpu_vm_update_fault_cache(adev, entry->pasid, addr, status, vmhub);

	dev_err(adev->dev,
		"VM_L2_PROTECTION_FAULT_STATUS:0x%08X\n",
		status);
	if (entry->vmid_src == AMDGPU_GFXHUB(0)) {
		dev_err(adev->dev, "\t Faulty UTCL2 client ID: %s (0x%x)\n",
			cid >= ARRAY_SIZE(gfxhub_client_ids) ? "unknown" :
			gfxhub_client_ids[cid],
			cid);
	} else {
		switch (amdgpu_ip_version(adev, MMHUB_HWIP, 0)) {
		case IP_VERSION(9, 0, 0):
			mmhub_cid = mmhub_client_ids_vega10[cid][rw];
			break;
		case IP_VERSION(9, 3, 0):
			mmhub_cid = mmhub_client_ids_vega12[cid][rw];
			break;
		case IP_VERSION(9, 4, 0):
			mmhub_cid = mmhub_client_ids_vega20[cid][rw];
			break;
		case IP_VERSION(9, 4, 1):
			mmhub_cid = mmhub_client_ids_arcturus[cid][rw];
			break;
		case IP_VERSION(9, 1, 0):
		case IP_VERSION(9, 2, 0):
			mmhub_cid = mmhub_client_ids_raven[cid][rw];
			break;
		case IP_VERSION(1, 5, 0):
		case IP_VERSION(2, 4, 0):
			mmhub_cid = mmhub_client_ids_renoir[cid][rw];
			break;
		case IP_VERSION(1, 8, 0):
		case IP_VERSION(9, 4, 2):
			mmhub_cid = mmhub_client_ids_aldebaran[cid][rw];
			break;
		default:
			mmhub_cid = NULL;
			break;
		}
		dev_err(adev->dev, "\t Faulty UTCL2 client ID: %s (0x%x)\n",
			mmhub_cid ? mmhub_cid : "unknown", cid);
	}
	dev_err(adev->dev, "\t MORE_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		VM_L2_PROTECTION_FAULT_STATUS, MORE_FAULTS));
	dev_err(adev->dev, "\t WALKER_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		VM_L2_PROTECTION_FAULT_STATUS, WALKER_ERROR));
	dev_err(adev->dev, "\t PERMISSION_FAULTS: 0x%lx\n",
		REG_GET_FIELD(status,
		VM_L2_PROTECTION_FAULT_STATUS, PERMISSION_FAULTS));
	dev_err(adev->dev, "\t MAPPING_ERROR: 0x%lx\n",
		REG_GET_FIELD(status,
		VM_L2_PROTECTION_FAULT_STATUS, MAPPING_ERROR));
	dev_err(adev->dev, "\t RW: 0x%x\n", rw);
	return 0;
}

static const struct amdgpu_irq_src_funcs gmc_v9_0_irq_funcs = {
	.set = gmc_v9_0_vm_fault_interrupt_state,
	.process = gmc_v9_0_process_interrupt,
};


static const struct amdgpu_irq_src_funcs gmc_v9_0_ecc_funcs = {
	.set = gmc_v9_0_ecc_interrupt_state,
	.process = amdgpu_umc_process_ecc_irq,
};

static void gmc_v9_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->gmc.vm_fault.num_types = 1;
	adev->gmc.vm_fault.funcs = &gmc_v9_0_irq_funcs;

	if (!amdgpu_sriov_vf(adev) &&
	    !adev->gmc.xgmi.connected_to_cpu &&
	    !adev->gmc.is_app_apu) {
		adev->gmc.ecc_irq.num_types = 1;
		adev->gmc.ecc_irq.funcs = &gmc_v9_0_ecc_funcs;
	}
}

static uint32_t gmc_v9_0_get_invalidate_req(unsigned int vmid,
					uint32_t flush_type)
{
	u32 req = 0;

	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ,
			    PER_VMID_INVALIDATE_REQ, 1 << vmid);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, FLUSH_TYPE, flush_type);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PTES, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE0, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE1, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L2_PDE2, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ, INVALIDATE_L1_PTES, 1);
	req = REG_SET_FIELD(req, VM_INVALIDATE_ENG0_REQ,
			    CLEAR_PROTECTION_FAULT_STATUS_ADDR,	0);

	return req;
}

/**
 * gmc_v9_0_use_invalidate_semaphore - judge whether to use semaphore
 *
 * @adev: amdgpu_device pointer
 * @vmhub: vmhub type
 *
 */
static bool gmc_v9_0_use_invalidate_semaphore(struct amdgpu_device *adev,
				       uint32_t vmhub)
{
	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 2) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4))
		return false;

	return ((vmhub == AMDGPU_MMHUB0(0) ||
		 vmhub == AMDGPU_MMHUB1(0)) &&
		(!amdgpu_sriov_vf(adev)) &&
		(!(!(adev->apu_flags & AMD_APU_IS_RAVEN2) &&
		   (adev->apu_flags & AMD_APU_IS_PICASSO))));
}

static bool gmc_v9_0_get_atc_vmid_pasid_mapping_info(struct amdgpu_device *adev,
					uint8_t vmid, uint16_t *p_pasid)
{
	uint32_t value;

	value = RREG32(SOC15_REG_OFFSET(ATHUB, 0, mmATC_VMID0_PASID_MAPPING)
		     + vmid);
	*p_pasid = value & ATC_VMID0_PASID_MAPPING__PASID_MASK;

	return !!(value & ATC_VMID0_PASID_MAPPING__VALID_MASK);
}

/*
 * GART
 * VMID 0 is the physical GPU addresses as used by the kernel.
 * VMIDs 1-15 are used for userspace clients and are handled
 * by the amdgpu vm/hsa code.
 */

/**
 * gmc_v9_0_flush_gpu_tlb - tlb flush with certain type
 *
 * @adev: amdgpu_device pointer
 * @vmid: vm instance to flush
 * @vmhub: which hub to flush
 * @flush_type: the flush type
 *
 * Flush the TLB for the requested page table using certain type.
 */
static void gmc_v9_0_flush_gpu_tlb(struct amdgpu_device *adev, uint32_t vmid,
					uint32_t vmhub, uint32_t flush_type)
{
	bool use_semaphore = gmc_v9_0_use_invalidate_semaphore(adev, vmhub);
	u32 j, inv_req, tmp, sem, req, ack, inst;
	const unsigned int eng = 17;
	struct amdgpu_vmhub *hub;

	BUG_ON(vmhub >= AMDGPU_MAX_VMHUBS);

	hub = &adev->vmhub[vmhub];
	inv_req = gmc_v9_0_get_invalidate_req(vmid, flush_type);
	sem = hub->vm_inv_eng0_sem + hub->eng_distance * eng;
	req = hub->vm_inv_eng0_req + hub->eng_distance * eng;
	ack = hub->vm_inv_eng0_ack + hub->eng_distance * eng;

	if (vmhub >= AMDGPU_MMHUB0(0))
		inst = 0;
	else
		inst = vmhub;

	/* This is necessary for SRIOV as well as for GFXOFF to function
	 * properly under bare metal
	 */
	if (adev->gfx.kiq[inst].ring.sched.ready &&
	    (amdgpu_sriov_runtime(adev) || !amdgpu_sriov_vf(adev))) {
		uint32_t req = hub->vm_inv_eng0_req + hub->eng_distance * eng;
		uint32_t ack = hub->vm_inv_eng0_ack + hub->eng_distance * eng;

		amdgpu_gmc_fw_reg_write_reg_wait(adev, req, ack, inv_req,
						 1 << vmid, inst);
		return;
	}

	/* This path is needed before KIQ/MES/GFXOFF are set up */
	spin_lock(&adev->gmc.invalidate_lock);

	/*
	 * It may lose gpuvm invalidate acknowldege state across power-gating
	 * off cycle, add semaphore acquire before invalidation and semaphore
	 * release after invalidation to avoid entering power gated state
	 * to WA the Issue
	 */

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore) {
		for (j = 0; j < adev->usec_timeout; j++) {
			/* a read return value of 1 means semaphore acquire */
			if (vmhub >= AMDGPU_MMHUB0(0))
				tmp = RREG32_SOC15_IP_NO_KIQ(MMHUB, sem, GET_INST(GC, inst));
			else
				tmp = RREG32_SOC15_IP_NO_KIQ(GC, sem, GET_INST(GC, inst));
			if (tmp & 0x1)
				break;
			udelay(1);
		}

		if (j >= adev->usec_timeout)
			DRM_ERROR("Timeout waiting for sem acquire in VM flush!\n");
	}

	if (vmhub >= AMDGPU_MMHUB0(0))
		WREG32_SOC15_IP_NO_KIQ(MMHUB, req, inv_req, GET_INST(GC, inst));
	else
		WREG32_SOC15_IP_NO_KIQ(GC, req, inv_req, GET_INST(GC, inst));

	/*
	 * Issue a dummy read to wait for the ACK register to
	 * be cleared to avoid a false ACK due to the new fast
	 * GRBM interface.
	 */
	if ((vmhub == AMDGPU_GFXHUB(0)) &&
	    (amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(9, 4, 2)))
		RREG32_NO_KIQ(req);

	for (j = 0; j < adev->usec_timeout; j++) {
		if (vmhub >= AMDGPU_MMHUB0(0))
			tmp = RREG32_SOC15_IP_NO_KIQ(MMHUB, ack, GET_INST(GC, inst));
		else
			tmp = RREG32_SOC15_IP_NO_KIQ(GC, ack, GET_INST(GC, inst));
		if (tmp & (1 << vmid))
			break;
		udelay(1);
	}

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore) {
		/*
		 * add semaphore release after invalidation,
		 * write with 0 means semaphore release
		 */
		if (vmhub >= AMDGPU_MMHUB0(0))
			WREG32_SOC15_IP_NO_KIQ(MMHUB, sem, 0, GET_INST(GC, inst));
		else
			WREG32_SOC15_IP_NO_KIQ(GC, sem, 0, GET_INST(GC, inst));
	}

	spin_unlock(&adev->gmc.invalidate_lock);

	if (j < adev->usec_timeout)
		return;

	DRM_ERROR("Timeout waiting for VM flush ACK!\n");
}

/**
 * gmc_v9_0_flush_gpu_tlb_pasid - tlb flush via pasid
 *
 * @adev: amdgpu_device pointer
 * @pasid: pasid to be flush
 * @flush_type: the flush type
 * @all_hub: flush all hubs
 * @inst: is used to select which instance of KIQ to use for the invalidation
 *
 * Flush the TLB for the requested pasid.
 */
static void gmc_v9_0_flush_gpu_tlb_pasid(struct amdgpu_device *adev,
					 uint16_t pasid, uint32_t flush_type,
					 bool all_hub, uint32_t inst)
{
	uint16_t queried;
	int i, vmid;

	for (vmid = 1; vmid < 16; vmid++) {
		bool valid;

		valid = gmc_v9_0_get_atc_vmid_pasid_mapping_info(adev, vmid,
								 &queried);
		if (!valid || queried != pasid)
			continue;

		if (all_hub) {
			for_each_set_bit(i, adev->vmhubs_mask,
					 AMDGPU_MAX_VMHUBS)
				gmc_v9_0_flush_gpu_tlb(adev, vmid, i,
						       flush_type);
		} else {
			gmc_v9_0_flush_gpu_tlb(adev, vmid,
					       AMDGPU_GFXHUB(0),
					       flush_type);
		}
	}
}

static uint64_t gmc_v9_0_emit_flush_gpu_tlb(struct amdgpu_ring *ring,
					    unsigned int vmid, uint64_t pd_addr)
{
	bool use_semaphore = gmc_v9_0_use_invalidate_semaphore(ring->adev, ring->vm_hub);
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_vmhub *hub = &adev->vmhub[ring->vm_hub];
	uint32_t req = gmc_v9_0_get_invalidate_req(vmid, 0);
	unsigned int eng = ring->vm_inv_eng;

	/*
	 * It may lose gpuvm invalidate acknowldege state across power-gating
	 * off cycle, add semaphore acquire before invalidation and semaphore
	 * release after invalidation to avoid entering power gated state
	 * to WA the Issue
	 */

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore)
		/* a read return value of 1 means semaphore acuqire */
		amdgpu_ring_emit_reg_wait(ring,
					  hub->vm_inv_eng0_sem +
					  hub->eng_distance * eng, 0x1, 0x1);

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_lo32 +
			      (hub->ctx_addr_distance * vmid),
			      lower_32_bits(pd_addr));

	amdgpu_ring_emit_wreg(ring, hub->ctx0_ptb_addr_hi32 +
			      (hub->ctx_addr_distance * vmid),
			      upper_32_bits(pd_addr));

	amdgpu_ring_emit_reg_write_reg_wait(ring, hub->vm_inv_eng0_req +
					    hub->eng_distance * eng,
					    hub->vm_inv_eng0_ack +
					    hub->eng_distance * eng,
					    req, 1 << vmid);

	/* TODO: It needs to continue working on debugging with semaphore for GFXHUB as well. */
	if (use_semaphore)
		/*
		 * add semaphore release after invalidation,
		 * write with 0 means semaphore release
		 */
		amdgpu_ring_emit_wreg(ring, hub->vm_inv_eng0_sem +
				      hub->eng_distance * eng, 0);

	return pd_addr;
}

static void gmc_v9_0_emit_pasid_mapping(struct amdgpu_ring *ring, unsigned int vmid,
					unsigned int pasid)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg;

	/* Do nothing because there's no lut register for mmhub1. */
	if (ring->vm_hub == AMDGPU_MMHUB1(0))
		return;

	if (ring->vm_hub == AMDGPU_GFXHUB(0))
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT) + vmid;
	else
		reg = SOC15_REG_OFFSET(OSSSYS, 0, mmIH_VMID_0_LUT_MM) + vmid;

	amdgpu_ring_emit_wreg(ring, reg, pasid);
}

/*
 * PTE format on VEGA 10:
 * 63:59 reserved
 * 58:57 mtype
 * 56 F
 * 55 L
 * 54 P
 * 53 SW
 * 52 T
 * 50:48 reserved
 * 47:12 4k physical page base address
 * 11:7 fragment
 * 6 write
 * 5 read
 * 4 exe
 * 3 Z
 * 2 snooped
 * 1 system
 * 0 valid
 *
 * PDE format on VEGA 10:
 * 63:59 block fragment size
 * 58:55 reserved
 * 54 P
 * 53:48 reserved
 * 47:6 physical base address of PD or PTE
 * 5:3 reserved
 * 2 C
 * 1 system
 * 0 valid
 */

static uint64_t gmc_v9_0_map_mtype(struct amdgpu_device *adev, uint32_t flags)

{
	switch (flags) {
	case AMDGPU_VM_MTYPE_DEFAULT:
		return AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_NC);
	case AMDGPU_VM_MTYPE_NC:
		return AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_NC);
	case AMDGPU_VM_MTYPE_WC:
		return AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_WC);
	case AMDGPU_VM_MTYPE_RW:
		return AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_RW);
	case AMDGPU_VM_MTYPE_CC:
		return AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_CC);
	case AMDGPU_VM_MTYPE_UC:
		return AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_UC);
	default:
		return AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_NC);
	}
}

static void gmc_v9_0_get_vm_pde(struct amdgpu_device *adev, int level,
				uint64_t *addr, uint64_t *flags)
{
	if (!(*flags & AMDGPU_PDE_PTE) && !(*flags & AMDGPU_PTE_SYSTEM))
		*addr = amdgpu_gmc_vram_mc2pa(adev, *addr);
	BUG_ON(*addr & 0xFFFF00000000003FULL);

	if (!adev->gmc.translate_further)
		return;

	if (level == AMDGPU_VM_PDB1) {
		/* Set the block fragment size */
		if (!(*flags & AMDGPU_PDE_PTE))
			*flags |= AMDGPU_PDE_BFS(0x9);

	} else if (level == AMDGPU_VM_PDB0) {
		if (*flags & AMDGPU_PDE_PTE) {
			*flags &= ~AMDGPU_PDE_PTE;
			if (!(*flags & AMDGPU_PTE_VALID))
				*addr |= 1 << PAGE_SHIFT;
		} else {
			*flags |= AMDGPU_PTE_TF;
		}
	}
}

static void gmc_v9_0_get_coherence_flags(struct amdgpu_device *adev,
					 struct amdgpu_bo *bo,
					 struct amdgpu_bo_va_mapping *mapping,
					 uint64_t *flags)
{
	struct amdgpu_device *bo_adev = amdgpu_ttm_adev(bo->tbo.bdev);
	bool is_vram = bo->tbo.resource &&
		bo->tbo.resource->mem_type == TTM_PL_VRAM;
	bool coherent = bo->flags & (AMDGPU_GEM_CREATE_COHERENT |
				     AMDGPU_GEM_CREATE_EXT_COHERENT);
	bool ext_coherent = bo->flags & AMDGPU_GEM_CREATE_EXT_COHERENT;
	bool uncached = bo->flags & AMDGPU_GEM_CREATE_UNCACHED;
	struct amdgpu_vm *vm = mapping->bo_va->base.vm;
	unsigned int mtype_local, mtype;
	bool snoop = false;
	bool is_local;

	dma_resv_assert_held(bo->tbo.base.resv);

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 4, 1):
	case IP_VERSION(9, 4, 2):
		if (is_vram) {
			if (bo_adev == adev) {
				if (uncached)
					mtype = MTYPE_UC;
				else if (coherent)
					mtype = MTYPE_CC;
				else
					mtype = MTYPE_RW;
				/* FIXME: is this still needed? Or does
				 * amdgpu_ttm_tt_pde_flags already handle this?
				 */
				if ((amdgpu_ip_version(adev, GC_HWIP, 0) ==
					     IP_VERSION(9, 4, 2) ||
				     amdgpu_ip_version(adev, GC_HWIP, 0) ==
					     IP_VERSION(9, 4, 3)) &&
				    adev->gmc.xgmi.connected_to_cpu)
					snoop = true;
			} else {
				if (uncached || coherent)
					mtype = MTYPE_UC;
				else
					mtype = MTYPE_NC;
				if (mapping->bo_va->is_xgmi)
					snoop = true;
			}
		} else {
			if (uncached || coherent)
				mtype = MTYPE_UC;
			else
				mtype = MTYPE_NC;
			/* FIXME: is this still needed? Or does
			 * amdgpu_ttm_tt_pde_flags already handle this?
			 */
			snoop = true;
		}
		break;
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		/* Only local VRAM BOs or system memory on non-NUMA APUs
		 * can be assumed to be local in their entirety. Choose
		 * MTYPE_NC as safe fallback for all system memory BOs on
		 * NUMA systems. Their MTYPE can be overridden per-page in
		 * gmc_v9_0_override_vm_pte_flags.
		 */
		mtype_local = MTYPE_RW;
		if (amdgpu_mtype_local == 1) {
			DRM_INFO_ONCE("Using MTYPE_NC for local memory\n");
			mtype_local = MTYPE_NC;
		} else if (amdgpu_mtype_local == 2) {
			DRM_INFO_ONCE("Using MTYPE_CC for local memory\n");
			mtype_local = MTYPE_CC;
		} else {
			DRM_INFO_ONCE("Using MTYPE_RW for local memory\n");
		}
		is_local = (!is_vram && (adev->flags & AMD_IS_APU) &&
			    num_possible_nodes() <= 1) ||
			   (is_vram && adev == bo_adev &&
			    KFD_XCP_MEM_ID(adev, bo->xcp_id) == vm->mem_id);
		snoop = true;
		if (uncached) {
			mtype = MTYPE_UC;
		} else if (ext_coherent) {
			if (adev->rev_id)
				mtype = is_local ? MTYPE_CC : MTYPE_UC;
			else
				mtype = MTYPE_UC;
		} else if (adev->flags & AMD_IS_APU) {
			mtype = is_local ? mtype_local : MTYPE_NC;
		} else {
			/* dGPU */
			if (is_local)
				mtype = mtype_local;
			else if (is_vram)
				mtype = MTYPE_NC;
			else
				mtype = MTYPE_UC;
		}

		break;
	default:
		if (uncached || coherent)
			mtype = MTYPE_UC;
		else
			mtype = MTYPE_NC;

		/* FIXME: is this still needed? Or does
		 * amdgpu_ttm_tt_pde_flags already handle this?
		 */
		if (!is_vram)
			snoop = true;
	}

	if (mtype != MTYPE_NC)
		*flags = AMDGPU_PTE_MTYPE_VG10(*flags, mtype);

	*flags |= snoop ? AMDGPU_PTE_SNOOPED : 0;
}

static void gmc_v9_0_get_vm_pte(struct amdgpu_device *adev,
				struct amdgpu_bo_va_mapping *mapping,
				uint64_t *flags)
{
	struct amdgpu_bo *bo = mapping->bo_va->base.bo;

	*flags &= ~AMDGPU_PTE_EXECUTABLE;
	*flags |= mapping->flags & AMDGPU_PTE_EXECUTABLE;

	*flags &= ~AMDGPU_PTE_MTYPE_VG10_MASK;
	*flags |= mapping->flags & AMDGPU_PTE_MTYPE_VG10_MASK;

	if (mapping->flags & AMDGPU_PTE_PRT) {
		*flags |= AMDGPU_PTE_PRT;
		*flags &= ~AMDGPU_PTE_VALID;
	}

	if ((*flags & AMDGPU_PTE_VALID) && bo)
		gmc_v9_0_get_coherence_flags(adev, bo, mapping, flags);
}

static void gmc_v9_0_override_vm_pte_flags(struct amdgpu_device *adev,
					   struct amdgpu_vm *vm,
					   uint64_t addr, uint64_t *flags)
{
	int local_node, nid;

	/* Only GFX 9.4.3 APUs associate GPUs with NUMA nodes. Local system
	 * memory can use more efficient MTYPEs.
	 */
	if (amdgpu_ip_version(adev, GC_HWIP, 0) != IP_VERSION(9, 4, 3) &&
	    amdgpu_ip_version(adev, GC_HWIP, 0) != IP_VERSION(9, 4, 4))
		return;

	/* Only direct-mapped memory allows us to determine the NUMA node from
	 * the DMA address.
	 */
	if (!adev->ram_is_direct_mapped) {
		dev_dbg_ratelimited(adev->dev, "RAM is not direct mapped\n");
		return;
	}

	/* MTYPE_NC is the same default and can be overridden.
	 * MTYPE_UC will be present if the memory is extended-coherent
	 * and can also be overridden.
	 */
	if ((*flags & AMDGPU_PTE_MTYPE_VG10_MASK) !=
	    AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_NC) &&
	    (*flags & AMDGPU_PTE_MTYPE_VG10_MASK) !=
	    AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_UC)) {
		dev_dbg_ratelimited(adev->dev, "MTYPE is not NC or UC\n");
		return;
	}

	/* FIXME: Only supported on native mode for now. For carve-out, the
	 * NUMA affinity of the GPU/VM needs to come from the PCI info because
	 * memory partitions are not associated with different NUMA nodes.
	 */
	if (adev->gmc.is_app_apu && vm->mem_id >= 0) {
		local_node = adev->gmc.mem_partitions[vm->mem_id].numa.node;
	} else {
		dev_dbg_ratelimited(adev->dev, "Only native mode APU is supported.\n");
		return;
	}

	/* Only handle real RAM. Mappings of PCIe resources don't have struct
	 * page or NUMA nodes.
	 */
#ifdef notyet
	if (!page_is_ram(addr >> PAGE_SHIFT)) {
		dev_dbg_ratelimited(adev->dev, "Page is not RAM.\n");
		return;
	}
#endif
	nid = pfn_to_nid(addr >> PAGE_SHIFT);
	dev_dbg_ratelimited(adev->dev, "vm->mem_id=%d, local_node=%d, nid=%d\n",
			    vm->mem_id, local_node, nid);
	if (nid == local_node) {
		uint64_t old_flags = *flags;
		if ((*flags & AMDGPU_PTE_MTYPE_VG10_MASK) ==
			AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_NC)) {
			unsigned int mtype_local = MTYPE_RW;

			if (amdgpu_mtype_local == 1)
				mtype_local = MTYPE_NC;
			else if (amdgpu_mtype_local == 2)
				mtype_local = MTYPE_CC;

			*flags = AMDGPU_PTE_MTYPE_VG10(*flags, mtype_local);
		} else if (adev->rev_id) {
			/* MTYPE_UC case */
			*flags = AMDGPU_PTE_MTYPE_VG10(*flags, MTYPE_CC);
		}

		dev_dbg_ratelimited(adev->dev, "flags updated from %llx to %llx\n",
				    old_flags, *flags);
	}
}

static unsigned int gmc_v9_0_get_vbios_fb_size(struct amdgpu_device *adev)
{
	u32 d1vga_control = RREG32_SOC15(DCE, 0, mmD1VGA_CONTROL);
	unsigned int size;

	/* TODO move to DC so GMC doesn't need to hard-code DCN registers */

	if (REG_GET_FIELD(d1vga_control, D1VGA_CONTROL, D1VGA_MODE_ENABLE)) {
		size = AMDGPU_VBIOS_VGA_ALLOCATION;
	} else {
		u32 viewport;

		switch (amdgpu_ip_version(adev, DCE_HWIP, 0)) {
		case IP_VERSION(1, 0, 0):
		case IP_VERSION(1, 0, 1):
			viewport = RREG32_SOC15(DCE, 0, mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION);
			size = (REG_GET_FIELD(viewport,
					      HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_HEIGHT) *
				REG_GET_FIELD(viewport,
					      HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_WIDTH) *
				4);
			break;
		case IP_VERSION(2, 1, 0):
			viewport = RREG32_SOC15(DCE, 0, mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION_DCN2);
			size = (REG_GET_FIELD(viewport,
					      HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_HEIGHT) *
				REG_GET_FIELD(viewport,
					      HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, PRI_VIEWPORT_WIDTH) *
				4);
			break;
		default:
			viewport = RREG32_SOC15(DCE, 0, mmSCL0_VIEWPORT_SIZE);
			size = (REG_GET_FIELD(viewport, SCL0_VIEWPORT_SIZE, VIEWPORT_HEIGHT) *
				REG_GET_FIELD(viewport, SCL0_VIEWPORT_SIZE, VIEWPORT_WIDTH) *
				4);
			break;
		}
	}

	return size;
}

static enum amdgpu_memory_partition
gmc_v9_0_get_memory_partition(struct amdgpu_device *adev, u32 *supp_modes)
{
	enum amdgpu_memory_partition mode = UNKNOWN_MEMORY_PARTITION_MODE;

	if (adev->nbio.funcs->get_memory_partition_mode)
		mode = adev->nbio.funcs->get_memory_partition_mode(adev,
								   supp_modes);

	return mode;
}

static enum amdgpu_memory_partition
gmc_v9_0_query_memory_partition(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		return AMDGPU_NPS1_PARTITION_MODE;

	return gmc_v9_0_get_memory_partition(adev, NULL);
}

static const struct amdgpu_gmc_funcs gmc_v9_0_gmc_funcs = {
	.flush_gpu_tlb = gmc_v9_0_flush_gpu_tlb,
	.flush_gpu_tlb_pasid = gmc_v9_0_flush_gpu_tlb_pasid,
	.emit_flush_gpu_tlb = gmc_v9_0_emit_flush_gpu_tlb,
	.emit_pasid_mapping = gmc_v9_0_emit_pasid_mapping,
	.map_mtype = gmc_v9_0_map_mtype,
	.get_vm_pde = gmc_v9_0_get_vm_pde,
	.get_vm_pte = gmc_v9_0_get_vm_pte,
	.override_vm_pte_flags = gmc_v9_0_override_vm_pte_flags,
	.get_vbios_fb_size = gmc_v9_0_get_vbios_fb_size,
	.query_mem_partition_mode = &gmc_v9_0_query_memory_partition,
};

static void gmc_v9_0_set_gmc_funcs(struct amdgpu_device *adev)
{
	adev->gmc.gmc_funcs = &gmc_v9_0_gmc_funcs;
}

static void gmc_v9_0_set_umc_funcs(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, UMC_HWIP, 0)) {
	case IP_VERSION(6, 0, 0):
		adev->umc.funcs = &umc_v6_0_funcs;
		break;
	case IP_VERSION(6, 1, 1):
		adev->umc.max_ras_err_cnt_per_query = UMC_V6_1_TOTAL_CHANNEL_NUM;
		adev->umc.channel_inst_num = UMC_V6_1_CHANNEL_INSTANCE_NUM;
		adev->umc.umc_inst_num = UMC_V6_1_UMC_INSTANCE_NUM;
		adev->umc.channel_offs = UMC_V6_1_PER_CHANNEL_OFFSET_VG20;
		adev->umc.retire_unit = 1;
		adev->umc.channel_idx_tbl = &umc_v6_1_channel_idx_tbl[0][0];
		adev->umc.ras = &umc_v6_1_ras;
		break;
	case IP_VERSION(6, 1, 2):
		adev->umc.max_ras_err_cnt_per_query = UMC_V6_1_TOTAL_CHANNEL_NUM;
		adev->umc.channel_inst_num = UMC_V6_1_CHANNEL_INSTANCE_NUM;
		adev->umc.umc_inst_num = UMC_V6_1_UMC_INSTANCE_NUM;
		adev->umc.channel_offs = UMC_V6_1_PER_CHANNEL_OFFSET_ARCT;
		adev->umc.retire_unit = 1;
		adev->umc.channel_idx_tbl = &umc_v6_1_channel_idx_tbl[0][0];
		adev->umc.ras = &umc_v6_1_ras;
		break;
	case IP_VERSION(6, 7, 0):
		adev->umc.max_ras_err_cnt_per_query =
			UMC_V6_7_TOTAL_CHANNEL_NUM * UMC_V6_7_BAD_PAGE_NUM_PER_CHANNEL;
		adev->umc.channel_inst_num = UMC_V6_7_CHANNEL_INSTANCE_NUM;
		adev->umc.umc_inst_num = UMC_V6_7_UMC_INSTANCE_NUM;
		adev->umc.channel_offs = UMC_V6_7_PER_CHANNEL_OFFSET;
		adev->umc.retire_unit = (UMC_V6_7_NA_MAP_PA_NUM * 2);
		if (!adev->gmc.xgmi.connected_to_cpu)
			adev->umc.ras = &umc_v6_7_ras;
		if (1 & adev->smuio.funcs->get_die_id(adev))
			adev->umc.channel_idx_tbl = &umc_v6_7_channel_idx_tbl_first[0][0];
		else
			adev->umc.channel_idx_tbl = &umc_v6_7_channel_idx_tbl_second[0][0];
		break;
	case IP_VERSION(12, 0, 0):
		adev->umc.max_ras_err_cnt_per_query =
			UMC_V12_0_TOTAL_CHANNEL_NUM(adev) * UMC_V12_0_BAD_PAGE_NUM_PER_CHANNEL;
		adev->umc.channel_inst_num = UMC_V12_0_CHANNEL_INSTANCE_NUM;
		adev->umc.umc_inst_num = UMC_V12_0_UMC_INSTANCE_NUM;
		adev->umc.node_inst_num /= UMC_V12_0_UMC_INSTANCE_NUM;
		adev->umc.channel_offs = UMC_V12_0_PER_CHANNEL_OFFSET;
		adev->umc.retire_unit = UMC_V12_0_BAD_PAGE_NUM_PER_CHANNEL;
		if (!adev->gmc.xgmi.connected_to_cpu && !adev->gmc.is_app_apu)
			adev->umc.ras = &umc_v12_0_ras;
		break;
	default:
		break;
	}
}

static void gmc_v9_0_set_mmhub_funcs(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, MMHUB_HWIP, 0)) {
	case IP_VERSION(9, 4, 1):
		adev->mmhub.funcs = &mmhub_v9_4_funcs;
		break;
	case IP_VERSION(9, 4, 2):
		adev->mmhub.funcs = &mmhub_v1_7_funcs;
		break;
	case IP_VERSION(1, 8, 0):
		adev->mmhub.funcs = &mmhub_v1_8_funcs;
		break;
	default:
		adev->mmhub.funcs = &mmhub_v1_0_funcs;
		break;
	}
}

static void gmc_v9_0_set_mmhub_ras_funcs(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, MMHUB_HWIP, 0)) {
	case IP_VERSION(9, 4, 0):
		adev->mmhub.ras = &mmhub_v1_0_ras;
		break;
	case IP_VERSION(9, 4, 1):
		adev->mmhub.ras = &mmhub_v9_4_ras;
		break;
	case IP_VERSION(9, 4, 2):
		adev->mmhub.ras = &mmhub_v1_7_ras;
		break;
	case IP_VERSION(1, 8, 0):
		adev->mmhub.ras = &mmhub_v1_8_ras;
		break;
	default:
		/* mmhub ras is not available */
		break;
	}
}

static void gmc_v9_0_set_gfxhub_funcs(struct amdgpu_device *adev)
{
	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4))
		adev->gfxhub.funcs = &gfxhub_v1_2_funcs;
	else
		adev->gfxhub.funcs = &gfxhub_v1_0_funcs;
}

static void gmc_v9_0_set_hdp_ras_funcs(struct amdgpu_device *adev)
{
	adev->hdp.ras = &hdp_v4_0_ras;
}

static void gmc_v9_0_set_mca_ras_funcs(struct amdgpu_device *adev)
{
	struct amdgpu_mca *mca = &adev->mca;

	/* is UMC the right IP to check for MCA?  Maybe DF? */
	switch (amdgpu_ip_version(adev, UMC_HWIP, 0)) {
	case IP_VERSION(6, 7, 0):
		if (!adev->gmc.xgmi.connected_to_cpu) {
			mca->mp0.ras = &mca_v3_0_mp0_ras;
			mca->mp1.ras = &mca_v3_0_mp1_ras;
			mca->mpio.ras = &mca_v3_0_mpio_ras;
		}
		break;
	default:
		break;
	}
}

static void gmc_v9_0_set_xgmi_ras_funcs(struct amdgpu_device *adev)
{
	if (!adev->gmc.xgmi.connected_to_cpu)
		adev->gmc.xgmi.ras = &xgmi_ras;
}

static int gmc_v9_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/*
	 * 9.4.0, 9.4.1 and 9.4.3 don't have XGMI defined
	 * in their IP discovery tables
	 */
	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 0) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 1) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4))
		adev->gmc.xgmi.supported = true;

	if (amdgpu_ip_version(adev, XGMI_HWIP, 0) == IP_VERSION(6, 1, 0)) {
		adev->gmc.xgmi.supported = true;
		adev->gmc.xgmi.connected_to_cpu =
			adev->smuio.funcs->is_host_gpu_xgmi_supported(adev);
	}

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4)) {
		enum amdgpu_pkg_type pkg_type =
			adev->smuio.funcs->get_pkg_type(adev);
		/* On GFXIP 9.4.3. APU, there is no physical VRAM domain present
		 * and the APU, can be in used two possible modes:
		 *  - carveout mode
		 *  - native APU mode
		 * "is_app_apu" can be used to identify the APU in the native
		 * mode.
		 */
#ifdef notyet
		adev->gmc.is_app_apu = (pkg_type == AMDGPU_PKG_TYPE_APU &&
					!pci_resource_len(adev->pdev, 0));
#else
		adev->gmc.is_app_apu = (pkg_type == AMDGPU_PKG_TYPE_APU &&
					!adev->fb_aper_size);
#endif
	}

	gmc_v9_0_set_gmc_funcs(adev);
	gmc_v9_0_set_irq_funcs(adev);
	gmc_v9_0_set_umc_funcs(adev);
	gmc_v9_0_set_mmhub_funcs(adev);
	gmc_v9_0_set_mmhub_ras_funcs(adev);
	gmc_v9_0_set_gfxhub_funcs(adev);
	gmc_v9_0_set_hdp_ras_funcs(adev);
	gmc_v9_0_set_mca_ras_funcs(adev);
	gmc_v9_0_set_xgmi_ras_funcs(adev);

	adev->gmc.shared_aperture_start = 0x2000000000000000ULL;
	adev->gmc.shared_aperture_end =
		adev->gmc.shared_aperture_start + (4ULL << 30) - 1;
	adev->gmc.private_aperture_start = 0x1000000000000000ULL;
	adev->gmc.private_aperture_end =
		adev->gmc.private_aperture_start + (4ULL << 30) - 1;
	adev->gmc.noretry_flags = AMDGPU_VM_NORETRY_FLAGS_TF;

	return 0;
}

static int gmc_v9_0_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_gmc_allocate_vm_inv_eng(adev);
	if (r)
		return r;

	/*
	 * Workaround performance drop issue with VBIOS enables partial
	 * writes, while disables HBM ECC for vega10.
	 */
	if (!amdgpu_sriov_vf(adev) &&
	    (amdgpu_ip_version(adev, UMC_HWIP, 0) == IP_VERSION(6, 0, 0))) {
		if (!(adev->ras_enabled & (1 << AMDGPU_RAS_BLOCK__UMC))) {
			if (adev->df.funcs &&
			    adev->df.funcs->enable_ecc_force_par_wr_rmw)
				adev->df.funcs->enable_ecc_force_par_wr_rmw(adev, false);
		}
	}

	if (!amdgpu_persistent_edc_harvesting_supported(adev)) {
		amdgpu_ras_reset_error_count(adev, AMDGPU_RAS_BLOCK__MMHUB);
		amdgpu_ras_reset_error_count(adev, AMDGPU_RAS_BLOCK__HDP);
	}

	r = amdgpu_gmc_ras_late_init(adev);
	if (r)
		return r;

	return amdgpu_irq_get(adev, &adev->gmc.vm_fault, 0);
}

static void gmc_v9_0_vram_gtt_location(struct amdgpu_device *adev,
					struct amdgpu_gmc *mc)
{
	u64 base = adev->mmhub.funcs->get_fb_location(adev);

	amdgpu_gmc_set_agp_default(adev, mc);

	/* add the xgmi offset of the physical node */
	base += adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;
	if (adev->gmc.xgmi.connected_to_cpu) {
		amdgpu_gmc_sysvm_location(adev, mc);
	} else {
		amdgpu_gmc_vram_location(adev, mc, base);
		amdgpu_gmc_gart_location(adev, mc, AMDGPU_GART_PLACEMENT_BEST_FIT);
		if (!amdgpu_sriov_vf(adev) && (amdgpu_agp == 1))
			amdgpu_gmc_agp_location(adev, mc);
	}
	/* base offset of vram pages */
	adev->vm_manager.vram_base_offset = adev->gfxhub.funcs->get_mc_fb_offset(adev);

	/* XXX: add the xgmi offset of the physical node? */
	adev->vm_manager.vram_base_offset +=
		adev->gmc.xgmi.physical_node_id * adev->gmc.xgmi.node_segment_size;
}

/**
 * gmc_v9_0_mc_init - initialize the memory controller driver params
 *
 * @adev: amdgpu_device pointer
 *
 * Look up the amount of vram, vram width, and decide how to place
 * vram and gart within the GPU's physical address space.
 * Returns 0 for success.
 */
static int gmc_v9_0_mc_init(struct amdgpu_device *adev)
{
	int r;

	/* size in MB on si */
	if (!adev->gmc.is_app_apu) {
		adev->gmc.mc_vram_size =
			adev->nbio.funcs->get_memsize(adev) * 1024ULL * 1024ULL;
	} else {
		DRM_DEBUG("Set mc_vram_size = 0 for APP APU\n");
		adev->gmc.mc_vram_size = 0;
	}
	adev->gmc.real_vram_size = adev->gmc.mc_vram_size;

	if (!(adev->flags & AMD_IS_APU) &&
	    !adev->gmc.xgmi.connected_to_cpu) {
		r = amdgpu_device_resize_fb_bar(adev);
		if (r)
			return r;
	}
	adev->gmc.aper_base = adev->fb_aper_offset;
	adev->gmc.aper_size = adev->fb_aper_size;

#ifdef CONFIG_X86_64
	/*
	 * AMD Accelerated Processing Platform (APP) supporting GPU-HOST xgmi
	 * interface can use VRAM through here as it appears system reserved
	 * memory in host address space.
	 *
	 * For APUs, VRAM is just the stolen system memory and can be accessed
	 * directly.
	 *
	 * Otherwise, use the legacy Host Data Path (HDP) through PCIe BAR.
	 */

	/* check whether both host-gpu and gpu-gpu xgmi links exist */
	if ((!amdgpu_sriov_vf(adev) &&
		(adev->flags & AMD_IS_APU) && !amdgpu_passthrough(adev)) ||
	    (adev->gmc.xgmi.supported &&
	     adev->gmc.xgmi.connected_to_cpu)) {
		adev->gmc.aper_base =
			adev->gfxhub.funcs->get_mc_fb_offset(adev) +
			adev->gmc.xgmi.physical_node_id *
			adev->gmc.xgmi.node_segment_size;
		adev->gmc.aper_size = adev->gmc.real_vram_size;
	}

#endif
	adev->gmc.visible_vram_size = adev->gmc.aper_size;

	/* set the gart size */
	if (amdgpu_gart_size == -1) {
		switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
		case IP_VERSION(9, 0, 1):  /* all engines support GPUVM */
		case IP_VERSION(9, 2, 1):  /* all engines support GPUVM */
		case IP_VERSION(9, 4, 0):
		case IP_VERSION(9, 4, 1):
		case IP_VERSION(9, 4, 2):
		case IP_VERSION(9, 4, 3):
		case IP_VERSION(9, 4, 4):
		default:
			adev->gmc.gart_size = 512ULL << 20;
			break;
		case IP_VERSION(9, 1, 0):   /* DCE SG support */
		case IP_VERSION(9, 2, 2):   /* DCE SG support */
		case IP_VERSION(9, 3, 0):
			adev->gmc.gart_size = 1024ULL << 20;
			break;
		}
	} else {
		adev->gmc.gart_size = (u64)amdgpu_gart_size << 20;
	}

	adev->gmc.gart_size += adev->pm.smu_prv_buffer_size;

	gmc_v9_0_vram_gtt_location(adev, &adev->gmc);

	return 0;
}

static int gmc_v9_0_gart_init(struct amdgpu_device *adev)
{
	int r;

	if (adev->gart.bo) {
		WARN(1, "VEGA10 PCIE GART already initialized\n");
		return 0;
	}

	if (adev->gmc.xgmi.connected_to_cpu) {
		adev->gmc.vmid0_page_table_depth = 1;
		adev->gmc.vmid0_page_table_block_size = 12;
	} else {
		adev->gmc.vmid0_page_table_depth = 0;
		adev->gmc.vmid0_page_table_block_size = 0;
	}

	/* Initialize common gart structure */
	r = amdgpu_gart_init(adev);
	if (r)
		return r;
	adev->gart.table_size = adev->gart.num_gpu_pages * 8;
	adev->gart.gart_pte_flags = AMDGPU_PTE_MTYPE_VG10(0ULL, MTYPE_UC) |
				 AMDGPU_PTE_EXECUTABLE;

	if (!adev->gmc.real_vram_size) {
		dev_info(adev->dev, "Put GART in system memory for APU\n");
		r = amdgpu_gart_table_ram_alloc(adev);
		if (r)
			dev_err(adev->dev, "Failed to allocate GART in system memory\n");
	} else {
		r = amdgpu_gart_table_vram_alloc(adev);
		if (r)
			return r;

		if (adev->gmc.xgmi.connected_to_cpu)
			r = amdgpu_gmc_pdb0_alloc(adev);
	}

	return r;
}

/**
 * gmc_v9_0_save_registers - saves regs
 *
 * @adev: amdgpu_device pointer
 *
 * This saves potential register values that should be
 * restored upon resume
 */
static void gmc_v9_0_save_registers(struct amdgpu_device *adev)
{
	if ((amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(1, 0, 0)) ||
	    (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(1, 0, 1)))
		adev->gmc.sdpif_register = RREG32_SOC15(DCE, 0, mmDCHUBBUB_SDPIF_MMIO_CNTRL_0);
}

static bool gmc_v9_0_validate_partition_info(struct amdgpu_device *adev)
{
	enum amdgpu_memory_partition mode;
	u32 supp_modes;
	bool valid;

	mode = gmc_v9_0_get_memory_partition(adev, &supp_modes);

	/* Mode detected by hardware not present in supported modes */
	if ((mode != UNKNOWN_MEMORY_PARTITION_MODE) &&
	    !(BIT(mode - 1) & supp_modes))
		return false;

	switch (mode) {
	case UNKNOWN_MEMORY_PARTITION_MODE:
	case AMDGPU_NPS1_PARTITION_MODE:
		valid = (adev->gmc.num_mem_partitions == 1);
		break;
	case AMDGPU_NPS2_PARTITION_MODE:
		valid = (adev->gmc.num_mem_partitions == 2);
		break;
	case AMDGPU_NPS4_PARTITION_MODE:
		valid = (adev->gmc.num_mem_partitions == 3 ||
			 adev->gmc.num_mem_partitions == 4);
		break;
	default:
		valid = false;
	}

	return valid;
}

static bool gmc_v9_0_is_node_present(int *node_ids, int num_ids, int nid)
{
	int i;

	/* Check if node with id 'nid' is present in 'node_ids' array */
	for (i = 0; i < num_ids; ++i)
		if (node_ids[i] == nid)
			return true;

	return false;
}

static void
gmc_v9_0_init_acpi_mem_ranges(struct amdgpu_device *adev,
			      struct amdgpu_mem_partition_info *mem_ranges)
{
	struct amdgpu_numa_info numa_info;
	int node_ids[MAX_MEM_RANGES];
	int num_ranges = 0, ret;
	int num_xcc, xcc_id;
	uint32_t xcc_mask;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	xcc_mask = (1U << num_xcc) - 1;

	for_each_inst(xcc_id, xcc_mask)	{
		ret = amdgpu_acpi_get_mem_info(adev, xcc_id, &numa_info);
		if (ret)
			continue;

		if (numa_info.nid == NUMA_NO_NODE) {
			mem_ranges[0].size = numa_info.size;
			mem_ranges[0].numa.node = numa_info.nid;
			num_ranges = 1;
			break;
		}

		if (gmc_v9_0_is_node_present(node_ids, num_ranges,
					     numa_info.nid))
			continue;

		node_ids[num_ranges] = numa_info.nid;
		mem_ranges[num_ranges].numa.node = numa_info.nid;
		mem_ranges[num_ranges].size = numa_info.size;
		++num_ranges;
	}

	adev->gmc.num_mem_partitions = num_ranges;
}

static void
gmc_v9_0_init_sw_mem_ranges(struct amdgpu_device *adev,
			    struct amdgpu_mem_partition_info *mem_ranges)
{
	enum amdgpu_memory_partition mode;
	u32 start_addr = 0, size;
	int i, r, l;

	mode = gmc_v9_0_query_memory_partition(adev);

	switch (mode) {
	case UNKNOWN_MEMORY_PARTITION_MODE:
	case AMDGPU_NPS1_PARTITION_MODE:
		adev->gmc.num_mem_partitions = 1;
		break;
	case AMDGPU_NPS2_PARTITION_MODE:
		adev->gmc.num_mem_partitions = 2;
		break;
	case AMDGPU_NPS4_PARTITION_MODE:
		if (adev->flags & AMD_IS_APU)
			adev->gmc.num_mem_partitions = 3;
		else
			adev->gmc.num_mem_partitions = 4;
		break;
	default:
		adev->gmc.num_mem_partitions = 1;
		break;
	}

	/* Use NPS range info, if populated */
	r = amdgpu_gmc_get_nps_memranges(adev, mem_ranges,
					 adev->gmc.num_mem_partitions);
	if (!r) {
		l = 0;
		for (i = 1; i < adev->gmc.num_mem_partitions; ++i) {
			if (mem_ranges[i].range.lpfn >
			    mem_ranges[i - 1].range.lpfn)
				l = i;
		}

	} else {
		/* Fallback to sw based calculation */
		size = (adev->gmc.real_vram_size + SZ_16M) >> AMDGPU_GPU_PAGE_SHIFT;
		size /= adev->gmc.num_mem_partitions;

		for (i = 0; i < adev->gmc.num_mem_partitions; ++i) {
			mem_ranges[i].range.fpfn = start_addr;
			mem_ranges[i].size =
				((u64)size << AMDGPU_GPU_PAGE_SHIFT);
			mem_ranges[i].range.lpfn = start_addr + size - 1;
			start_addr += size;
		}

		l = adev->gmc.num_mem_partitions - 1;
	}

	/* Adjust the last one */
	mem_ranges[l].range.lpfn =
		(adev->gmc.real_vram_size >> AMDGPU_GPU_PAGE_SHIFT) - 1;
	mem_ranges[l].size =
		adev->gmc.real_vram_size -
		((u64)mem_ranges[l].range.fpfn << AMDGPU_GPU_PAGE_SHIFT);
}

static int gmc_v9_0_init_mem_ranges(struct amdgpu_device *adev)
{
	bool valid;

	adev->gmc.mem_partitions = kcalloc(MAX_MEM_RANGES,
					   sizeof(struct amdgpu_mem_partition_info),
					   GFP_KERNEL);
	if (!adev->gmc.mem_partitions)
		return -ENOMEM;

	/* TODO : Get the range from PSP/Discovery for dGPU */
	if (adev->gmc.is_app_apu)
		gmc_v9_0_init_acpi_mem_ranges(adev, adev->gmc.mem_partitions);
	else
		gmc_v9_0_init_sw_mem_ranges(adev, adev->gmc.mem_partitions);

	if (amdgpu_sriov_vf(adev))
		valid = true;
	else
		valid = gmc_v9_0_validate_partition_info(adev);
	if (!valid) {
		/* TODO: handle invalid case */
		dev_WARN(adev->dev,
			 "Mem ranges not matching with hardware config");
	}

	return 0;
}

static void gmc_v9_4_3_init_vram_info(struct amdgpu_device *adev)
{
	adev->gmc.vram_type = AMDGPU_VRAM_TYPE_HBM;
	adev->gmc.vram_width = 128 * 64;
}

static int gmc_v9_0_sw_init(void *handle)
{
	int r, vram_width = 0, vram_type = 0, vram_vendor = 0, dma_addr_bits;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	unsigned long inst_mask = adev->aid_mask;

	adev->gfxhub.funcs->init(adev);

	adev->mmhub.funcs->init(adev);

	mtx_init(&adev->gmc.invalidate_lock, IPL_NONE);

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4)) {
		gmc_v9_4_3_init_vram_info(adev);
	} else if (!adev->bios) {
		if (adev->flags & AMD_IS_APU) {
			adev->gmc.vram_type = AMDGPU_VRAM_TYPE_DDR4;
			adev->gmc.vram_width = 64 * 64;
		} else {
			adev->gmc.vram_type = AMDGPU_VRAM_TYPE_HBM;
			adev->gmc.vram_width = 128 * 64;
		}
	} else {
		r = amdgpu_atomfirmware_get_vram_info(adev,
			&vram_width, &vram_type, &vram_vendor);
		if (amdgpu_sriov_vf(adev))
			/* For Vega10 SR-IOV, vram_width can't be read from ATOM as RAVEN,
			 * and DF related registers is not readable, seems hardcord is the
			 * only way to set the correct vram_width
			 */
			adev->gmc.vram_width = 2048;
		else if (amdgpu_emu_mode != 1)
			adev->gmc.vram_width = vram_width;

		if (!adev->gmc.vram_width) {
			int chansize, numchan;

			/* hbm memory channel size */
			if (adev->flags & AMD_IS_APU)
				chansize = 64;
			else
				chansize = 128;
			if (adev->df.funcs &&
			    adev->df.funcs->get_hbm_channel_number) {
				numchan = adev->df.funcs->get_hbm_channel_number(adev);
				adev->gmc.vram_width = numchan * chansize;
			}
		}

		adev->gmc.vram_type = vram_type;
		adev->gmc.vram_vendor = vram_vendor;
	}
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(9, 1, 0):
	case IP_VERSION(9, 2, 2):
		set_bit(AMDGPU_GFXHUB(0), adev->vmhubs_mask);
		set_bit(AMDGPU_MMHUB0(0), adev->vmhubs_mask);

		if (adev->rev_id == 0x0 || adev->rev_id == 0x1) {
			amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		} else {
			/* vm_size is 128TB + 512GB for legacy 3-level page support */
			amdgpu_vm_adjust_size(adev, 128 * 1024 + 512, 9, 2, 48);
			adev->gmc.translate_further =
				adev->vm_manager.num_level > 1;
		}
		break;
	case IP_VERSION(9, 0, 1):
	case IP_VERSION(9, 2, 1):
	case IP_VERSION(9, 4, 0):
	case IP_VERSION(9, 3, 0):
	case IP_VERSION(9, 4, 2):
		set_bit(AMDGPU_GFXHUB(0), adev->vmhubs_mask);
		set_bit(AMDGPU_MMHUB0(0), adev->vmhubs_mask);

		/*
		 * To fulfill 4-level page support,
		 * vm size is 256TB (48bit), maximum size of Vega10,
		 * block size 512 (9bit)
		 */

		amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 2))
			adev->gmc.translate_further = adev->vm_manager.num_level > 1;
		break;
	case IP_VERSION(9, 4, 1):
		set_bit(AMDGPU_GFXHUB(0), adev->vmhubs_mask);
		set_bit(AMDGPU_MMHUB0(0), adev->vmhubs_mask);
		set_bit(AMDGPU_MMHUB1(0), adev->vmhubs_mask);

		/* Keep the vm size same with Vega20 */
		amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		adev->gmc.translate_further = adev->vm_manager.num_level > 1;
		break;
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
		bitmap_set(adev->vmhubs_mask, AMDGPU_GFXHUB(0),
				  NUM_XCC(adev->gfx.xcc_mask));

		inst_mask <<= AMDGPU_MMHUB0(0);
		bitmap_or(adev->vmhubs_mask, adev->vmhubs_mask, &inst_mask, 32);

		amdgpu_vm_adjust_size(adev, 256 * 1024, 9, 3, 48);
		adev->gmc.translate_further = adev->vm_manager.num_level > 1;
		break;
	default:
		break;
	}

	/* This interrupt is VMC page fault.*/
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VMC, VMC_1_0__SRCID__VM_FAULT,
				&adev->gmc.vm_fault);
	if (r)
		return r;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 1)) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VMC1, VMC_1_0__SRCID__VM_FAULT,
					&adev->gmc.vm_fault);
		if (r)
			return r;
	}

	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_UTCL2, UTCL2_1_0__SRCID__FAULT,
				&adev->gmc.vm_fault);

	if (r)
		return r;

	if (!amdgpu_sriov_vf(adev) &&
	    !adev->gmc.xgmi.connected_to_cpu &&
	    !adev->gmc.is_app_apu) {
		/* interrupt sent to DF. */
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_DF, 0,
				      &adev->gmc.ecc_irq);
		if (r)
			return r;
	}

	/* Set the internal MC address mask
	 * This is the max address of the GPU's
	 * internal address space.
	 */
	adev->gmc.mc_mask = 0xffffffffffffULL; /* 48 bit MC */

	dma_addr_bits = amdgpu_ip_version(adev, GC_HWIP, 0) >=
					IP_VERSION(9, 4, 2) ?
				48 :
				44;
	r = dma_set_mask_and_coherent(adev->dev, DMA_BIT_MASK(dma_addr_bits));
	if (r) {
		dev_warn(adev->dev, "amdgpu: No suitable DMA available.\n");
		return r;
	}
	adev->need_swiotlb = drm_need_swiotlb(dma_addr_bits);

	r = gmc_v9_0_mc_init(adev);
	if (r)
		return r;

	amdgpu_gmc_get_vbios_allocations(adev);

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4)) {
		r = gmc_v9_0_init_mem_ranges(adev);
		if (r)
			return r;
	}

	/* Memory manager */
	r = amdgpu_bo_init(adev);
	if (r)
		return r;

	r = gmc_v9_0_gart_init(adev);
	if (r)
		return r;

	/*
	 * number of VMs
	 * VMID 0 is reserved for System
	 * amdgpu graphics/compute will use VMIDs 1..n-1
	 * amdkfd will use VMIDs n..15
	 *
	 * The first KFD VMID is 8 for GPUs with graphics, 3 for
	 * compute-only GPUs. On compute-only GPUs that leaves 2 VMIDs
	 * for video processing.
	 */
	adev->vm_manager.first_kfd_vmid =
		(amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 1) ||
		 amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 2) ||
		 amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
		 amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4)) ?
			3 :
			8;

	amdgpu_vm_manager_init(adev);

	gmc_v9_0_save_registers(adev);

	r = amdgpu_gmc_ras_sw_init(adev);
	if (r)
		return r;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4))
		amdgpu_gmc_sysfs_init(adev);

	return 0;
}

static int gmc_v9_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) ||
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 4))
		amdgpu_gmc_sysfs_fini(adev);

	amdgpu_gmc_ras_fini(adev);
	amdgpu_gem_force_release(adev);
	amdgpu_vm_manager_fini(adev);
	if (!adev->gmc.real_vram_size) {
		dev_info(adev->dev, "Put GART in system memory for APU free\n");
		amdgpu_gart_table_ram_free(adev);
	} else {
		amdgpu_gart_table_vram_free(adev);
	}
	amdgpu_bo_free_kernel(&adev->gmc.pdb0_bo, NULL, &adev->gmc.ptr_pdb0);
	amdgpu_bo_fini(adev);

	adev->gmc.num_mem_partitions = 0;
	kfree(adev->gmc.mem_partitions);

	return 0;
}

static void gmc_v9_0_init_golden_registers(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, MMHUB_HWIP, 0)) {
	case IP_VERSION(9, 0, 0):
		if (amdgpu_sriov_vf(adev))
			break;
		fallthrough;
	case IP_VERSION(9, 4, 0):
		soc15_program_register_sequence(adev,
						golden_settings_mmhub_1_0_0,
						ARRAY_SIZE(golden_settings_mmhub_1_0_0));
		soc15_program_register_sequence(adev,
						golden_settings_athub_1_0_0,
						ARRAY_SIZE(golden_settings_athub_1_0_0));
		break;
	case IP_VERSION(9, 1, 0):
	case IP_VERSION(9, 2, 0):
		/* TODO for renoir */
		soc15_program_register_sequence(adev,
						golden_settings_athub_1_0_0,
						ARRAY_SIZE(golden_settings_athub_1_0_0));
		break;
	default:
		break;
	}
}

/**
 * gmc_v9_0_restore_registers - restores regs
 *
 * @adev: amdgpu_device pointer
 *
 * This restores register values, saved at suspend.
 */
void gmc_v9_0_restore_registers(struct amdgpu_device *adev)
{
	if ((amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(1, 0, 0)) ||
	    (amdgpu_ip_version(adev, DCE_HWIP, 0) == IP_VERSION(1, 0, 1))) {
		WREG32_SOC15(DCE, 0, mmDCHUBBUB_SDPIF_MMIO_CNTRL_0, adev->gmc.sdpif_register);
		WARN_ON(adev->gmc.sdpif_register !=
			RREG32_SOC15(DCE, 0, mmDCHUBBUB_SDPIF_MMIO_CNTRL_0));
	}
}

/**
 * gmc_v9_0_gart_enable - gart enable
 *
 * @adev: amdgpu_device pointer
 */
static int gmc_v9_0_gart_enable(struct amdgpu_device *adev)
{
	int r;

	if (adev->gmc.xgmi.connected_to_cpu)
		amdgpu_gmc_init_pdb0(adev);

	if (adev->gart.bo == NULL) {
		dev_err(adev->dev, "No VRAM object for PCIE GART.\n");
		return -EINVAL;
	}

	amdgpu_gtt_mgr_recover(&adev->mman.gtt_mgr);

	if (!adev->in_s0ix) {
		r = adev->gfxhub.funcs->gart_enable(adev);
		if (r)
			return r;
	}

	r = adev->mmhub.funcs->gart_enable(adev);
	if (r)
		return r;

	DRM_INFO("PCIE GART of %uM enabled.\n",
		 (unsigned int)(adev->gmc.gart_size >> 20));
	if (adev->gmc.pdb0_bo)
		DRM_INFO("PDB0 located at 0x%016llX\n",
				(unsigned long long)amdgpu_bo_gpu_offset(adev->gmc.pdb0_bo));
	DRM_INFO("PTB located at 0x%016llX\n",
			(unsigned long long)amdgpu_bo_gpu_offset(adev->gart.bo));

	return 0;
}

static int gmc_v9_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool value;
	int i, r;

	adev->gmc.flush_pasid_uses_kiq = true;

	/* Vega20+XGMI caches PTEs in TC and TLB. Add a heavy-weight TLB flush
	 * (type 2), which flushes both. Due to a race condition with
	 * concurrent memory accesses using the same TLB cache line, we still
	 * need a second TLB flush after this.
	 */
	adev->gmc.flush_tlb_needs_extra_type_2 =
		amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 0) &&
		adev->gmc.xgmi.num_physical_nodes;
	/*
	 * TODO: This workaround is badly documented and had a buggy
	 * implementation. We should probably verify what we do here.
	 */
	adev->gmc.flush_tlb_needs_extra_type_0 =
		amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 3) &&
		adev->rev_id == 0;

	/* The sequence of these two function calls matters.*/
	gmc_v9_0_init_golden_registers(adev);

	if (adev->mode_info.num_crtc) {
		/* Lockout access through VGA aperture*/
		WREG32_FIELD15(DCE, 0, VGA_HDP_CONTROL, VGA_MEMORY_DISABLE, 1);
		/* disable VGA render */
		WREG32_FIELD15(DCE, 0, VGA_RENDER_CONTROL, VGA_VSTATUS_CNTL, 0);
	}

	if (adev->mmhub.funcs->update_power_gating)
		adev->mmhub.funcs->update_power_gating(adev, true);

	adev->hdp.funcs->init_registers(adev);

	/* After HDP is initialized, flush HDP.*/
	amdgpu_device_flush_hdp(adev, NULL);

	if (amdgpu_vm_fault_stop == AMDGPU_VM_FAULT_STOP_ALWAYS)
		value = false;
	else
		value = true;

	if (!amdgpu_sriov_vf(adev)) {
		if (!adev->in_s0ix)
			adev->gfxhub.funcs->set_fault_enable_default(adev, value);
		adev->mmhub.funcs->set_fault_enable_default(adev, value);
	}
	for_each_set_bit(i, adev->vmhubs_mask, AMDGPU_MAX_VMHUBS) {
		if (adev->in_s0ix && (i == AMDGPU_GFXHUB(0)))
			continue;
		gmc_v9_0_flush_gpu_tlb(adev, 0, i, 0);
	}

	if (adev->umc.funcs && adev->umc.funcs->init_registers)
		adev->umc.funcs->init_registers(adev);

	r = gmc_v9_0_gart_enable(adev);
	if (r)
		return r;

	if (amdgpu_emu_mode == 1)
		return amdgpu_gmc_vram_checking(adev);

	return 0;
}

/**
 * gmc_v9_0_gart_disable - gart disable
 *
 * @adev: amdgpu_device pointer
 *
 * This disables all VM page table.
 */
static void gmc_v9_0_gart_disable(struct amdgpu_device *adev)
{
	if (!adev->in_s0ix)
		adev->gfxhub.funcs->gart_disable(adev);
	adev->mmhub.funcs->gart_disable(adev);
}

static int gmc_v9_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	gmc_v9_0_gart_disable(adev);

	if (amdgpu_sriov_vf(adev)) {
		/* full access mode, so don't touch any GMC register */
		DRM_DEBUG("For SRIOV client, shouldn't do anything.\n");
		return 0;
	}

	/*
	 * Pair the operations did in gmc_v9_0_hw_init and thus maintain
	 * a correct cached state for GMC. Otherwise, the "gate" again
	 * operation on S3 resuming will fail due to wrong cached state.
	 */
	if (adev->mmhub.funcs->update_power_gating)
		adev->mmhub.funcs->update_power_gating(adev, false);

	amdgpu_irq_put(adev, &adev->gmc.vm_fault, 0);

	if (adev->gmc.ecc_irq.funcs &&
		amdgpu_ras_is_supported(adev, AMDGPU_RAS_BLOCK__UMC))
		amdgpu_irq_put(adev, &adev->gmc.ecc_irq, 0);

	return 0;
}

static int gmc_v9_0_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return gmc_v9_0_hw_fini(adev);
}

static int gmc_v9_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = gmc_v9_0_hw_init(adev);
	if (r)
		return r;

	amdgpu_vmid_reset_all(adev);

	return 0;
}

static bool gmc_v9_0_is_idle(void *handle)
{
	/* MC is always ready in GMC v9.*/
	return true;
}

static int gmc_v9_0_wait_for_idle(void *handle)
{
	/* There is no need to wait for MC idle in GMC v9.*/
	return 0;
}

static int gmc_v9_0_soft_reset(void *handle)
{
	/* XXX for emulation.*/
	return 0;
}

static int gmc_v9_0_set_clockgating_state(void *handle,
					enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->mmhub.funcs->set_clockgating(adev, state);

	athub_v1_0_set_clockgating(adev, state);

	return 0;
}

static void gmc_v9_0_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->mmhub.funcs->get_clockgating(adev, flags);

	athub_v1_0_get_clockgating(adev, flags);
}

static int gmc_v9_0_set_powergating_state(void *handle,
					enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs gmc_v9_0_ip_funcs = {
	.name = "gmc_v9_0",
	.early_init = gmc_v9_0_early_init,
	.late_init = gmc_v9_0_late_init,
	.sw_init = gmc_v9_0_sw_init,
	.sw_fini = gmc_v9_0_sw_fini,
	.hw_init = gmc_v9_0_hw_init,
	.hw_fini = gmc_v9_0_hw_fini,
	.suspend = gmc_v9_0_suspend,
	.resume = gmc_v9_0_resume,
	.is_idle = gmc_v9_0_is_idle,
	.wait_for_idle = gmc_v9_0_wait_for_idle,
	.soft_reset = gmc_v9_0_soft_reset,
	.set_clockgating_state = gmc_v9_0_set_clockgating_state,
	.set_powergating_state = gmc_v9_0_set_powergating_state,
	.get_clockgating_state = gmc_v9_0_get_clockgating_state,
};

const struct amdgpu_ip_block_version gmc_v9_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_GMC,
	.major = 9,
	.minor = 0,
	.rev = 0,
	.funcs = &gmc_v9_0_ip_funcs,
};
