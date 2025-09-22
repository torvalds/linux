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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "amdgpu.h"
#include "amdgpu_atombios.h"
#include "amdgpu_ih.h"
#include "amdgpu_uvd.h"
#include "amdgpu_vce.h"
#include "amdgpu_ucode.h"
#include "amdgpu_psp.h"
#include "amdgpu_smu.h"
#include "atom.h"
#include "amd_pcie.h"

#include "gc/gc_12_0_0_offset.h"
#include "gc/gc_12_0_0_sh_mask.h"
#include "mp/mp_14_0_2_offset.h"

#include "soc15.h"
#include "soc15_common.h"
#include "soc24.h"
#include "mxgpu_nv.h"

static const struct amd_ip_funcs soc24_common_ip_funcs;

static const struct amdgpu_video_codec_info vcn_5_0_0_video_codecs_encode_array_vcn0[] = {
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs vcn_5_0_0_video_codecs_encode_vcn0 = {
	.codec_count = ARRAY_SIZE(vcn_5_0_0_video_codecs_encode_array_vcn0),
	.codec_array = vcn_5_0_0_video_codecs_encode_array_vcn0,
};

static const struct amdgpu_video_codec_info vcn_5_0_0_video_codecs_decode_array_vcn0[] = {
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC, 4096, 4096, 52)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC, 8192, 4352, 186)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_JPEG, 16384, 16384, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9, 8192, 4352, 0)},
	{codec_info_build(AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1, 8192, 4352, 0)},
};

static const struct amdgpu_video_codecs vcn_5_0_0_video_codecs_decode_vcn0 = {
	.codec_count = ARRAY_SIZE(vcn_5_0_0_video_codecs_decode_array_vcn0),
	.codec_array = vcn_5_0_0_video_codecs_decode_array_vcn0,
};

static int soc24_query_video_codecs(struct amdgpu_device *adev, bool encode,
				 const struct amdgpu_video_codecs **codecs)
{
	if (adev->vcn.num_vcn_inst == hweight8(adev->vcn.harvest_config))
		return -EINVAL;

	switch (amdgpu_ip_version(adev, UVD_HWIP, 0)) {
	case IP_VERSION(5, 0, 0):
		if (encode)
			*codecs = &vcn_5_0_0_video_codecs_encode_vcn0;
		else
			*codecs = &vcn_5_0_0_video_codecs_decode_vcn0;
		return 0;
	default:
		return -EINVAL;
	}
}

static u32 soc24_get_config_memsize(struct amdgpu_device *adev)
{
	return adev->nbio.funcs->get_memsize(adev);
}

static u32 soc24_get_xclk(struct amdgpu_device *adev)
{
	return adev->clock.spll.reference_freq;
}

void soc24_grbm_select(struct amdgpu_device *adev,
		       u32 me, u32 pipe, u32 queue, u32 vmid)
{
	u32 grbm_gfx_cntl = 0;
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, PIPEID, pipe);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, MEID, me);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, VMID, vmid);
	grbm_gfx_cntl = REG_SET_FIELD(grbm_gfx_cntl, GRBM_GFX_CNTL, QUEUEID, queue);

	WREG32_SOC15(GC, 0, regGRBM_GFX_CNTL, grbm_gfx_cntl);
}

static struct soc15_allowed_register_entry soc24_allowed_read_registers[] = {
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS2)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE0)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE1)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE2)},
	{ SOC15_REG_ENTRY(GC, 0, regGRBM_STATUS_SE3)},
	{ SOC15_REG_ENTRY(SDMA0, 0, regSDMA0_STATUS_REG)},
	{ SOC15_REG_ENTRY(SDMA1, 0, regSDMA1_STATUS_REG)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT2)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_STALLED_STAT3)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPF_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_BUSY_STAT)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_STALLED_STAT1)},
	{ SOC15_REG_ENTRY(GC, 0, regCP_CPC_STATUS)},
	{ SOC15_REG_ENTRY(GC, 0, regGB_ADDR_CONFIG)},
};

static uint32_t soc24_read_indexed_register(struct amdgpu_device *adev,
					    u32 se_num,
					    u32 sh_num,
					    u32 reg_offset)
{
	uint32_t val;

	mutex_lock(&adev->grbm_idx_mutex);
	if (se_num != 0xffffffff || sh_num != 0xffffffff)
		amdgpu_gfx_select_se_sh(adev, se_num, sh_num, 0xffffffff, 0);

	val = RREG32(reg_offset);

	if (se_num != 0xffffffff || sh_num != 0xffffffff)
		amdgpu_gfx_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff, 0);
	mutex_unlock(&adev->grbm_idx_mutex);
	return val;
}

static uint32_t soc24_get_register_value(struct amdgpu_device *adev,
					 bool indexed, u32 se_num,
					 u32 sh_num, u32 reg_offset)
{
	if (indexed) {
		return soc24_read_indexed_register(adev, se_num, sh_num, reg_offset);
	} else {
		if (reg_offset == SOC15_REG_OFFSET(GC, 0, regGB_ADDR_CONFIG) &&
		    adev->gfx.config.gb_addr_config)
			return adev->gfx.config.gb_addr_config;
		return RREG32(reg_offset);
	}
}

static int soc24_read_register(struct amdgpu_device *adev, u32 se_num,
			       u32 sh_num, u32 reg_offset, u32 *value)
{
	uint32_t i;
	struct soc15_allowed_register_entry  *en;

	*value = 0;
	for (i = 0; i < ARRAY_SIZE(soc24_allowed_read_registers); i++) {
		en = &soc24_allowed_read_registers[i];
		if (!adev->reg_offset[en->hwip][en->inst])
			continue;
		else if (reg_offset != (adev->reg_offset[en->hwip][en->inst][en->seg]
					+ en->reg_offset))
			continue;

		*value = soc24_get_register_value(adev,
				soc24_allowed_read_registers[i].grbm_indexed,
				se_num, sh_num, reg_offset);
		return 0;
	}
	return -EINVAL;
}

static enum amd_reset_method
soc24_asic_reset_method(struct amdgpu_device *adev)
{
	if (amdgpu_reset_method == AMD_RESET_METHOD_MODE1 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_MODE2 ||
	    amdgpu_reset_method == AMD_RESET_METHOD_BACO)
		return amdgpu_reset_method;

	if (amdgpu_reset_method != -1)
		dev_warn(adev->dev,
			 "Specified reset method:%d isn't supported, using AUTO instead.\n",
			 amdgpu_reset_method);

	switch (amdgpu_ip_version(adev, MP1_HWIP, 0)) {
	case IP_VERSION(14, 0, 2):
	case IP_VERSION(14, 0, 3):
		return AMD_RESET_METHOD_MODE1;
	default:
		if (amdgpu_dpm_is_baco_supported(adev))
			return AMD_RESET_METHOD_BACO;
		else
			return AMD_RESET_METHOD_MODE1;
	}
}

static int soc24_asic_reset(struct amdgpu_device *adev)
{
	int ret = 0;

	switch (soc24_asic_reset_method(adev)) {
	case AMD_RESET_METHOD_PCI:
		dev_info(adev->dev, "PCI reset\n");
		ret = amdgpu_device_pci_reset(adev);
		break;
	case AMD_RESET_METHOD_BACO:
		dev_info(adev->dev, "BACO reset\n");
		ret = amdgpu_dpm_baco_reset(adev);
		break;
	case AMD_RESET_METHOD_MODE2:
		dev_info(adev->dev, "MODE2 reset\n");
		ret = amdgpu_dpm_mode2_reset(adev);
		break;
	default:
		dev_info(adev->dev, "MODE1 reset\n");
		ret = amdgpu_device_mode1_reset(adev);
		break;
	}

	return ret;
}

static void soc24_program_aspm(struct amdgpu_device *adev)
{
	if (!amdgpu_device_should_use_aspm(adev))
		return;

	if (!(adev->flags & AMD_IS_APU) &&
	    (adev->nbio.funcs->program_aspm))
		adev->nbio.funcs->program_aspm(adev);
}

const struct amdgpu_ip_block_version soc24_common_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_COMMON,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &soc24_common_ip_funcs,
};

static bool soc24_need_full_reset(struct amdgpu_device *adev)
{
	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
	default:
		return true;
	}
}

static bool soc24_need_reset_on_init(struct amdgpu_device *adev)
{
	u32 sol_reg;

	if (adev->flags & AMD_IS_APU)
		return false;

	/* Check sOS sign of life register to confirm sys driver and sOS
	 * are already been loaded.
	 */
	sol_reg = RREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_81);
	if (sol_reg)
		return true;

	return false;
}

static uint64_t soc24_get_pcie_replay_count(struct amdgpu_device *adev)
{
	/* TODO
	 * dummy implement for pcie_replay_count sysfs interface
	 * */
	return 0;
}

static void soc24_init_doorbell_index(struct amdgpu_device *adev)
{
	adev->doorbell_index.kiq = AMDGPU_NAVI10_DOORBELL_KIQ;
	adev->doorbell_index.mec_ring0 = AMDGPU_NAVI10_DOORBELL_MEC_RING0;
	adev->doorbell_index.mec_ring1 = AMDGPU_NAVI10_DOORBELL_MEC_RING1;
	adev->doorbell_index.mec_ring2 = AMDGPU_NAVI10_DOORBELL_MEC_RING2;
	adev->doorbell_index.mec_ring3 = AMDGPU_NAVI10_DOORBELL_MEC_RING3;
	adev->doorbell_index.mec_ring4 = AMDGPU_NAVI10_DOORBELL_MEC_RING4;
	adev->doorbell_index.mec_ring5 = AMDGPU_NAVI10_DOORBELL_MEC_RING5;
	adev->doorbell_index.mec_ring6 = AMDGPU_NAVI10_DOORBELL_MEC_RING6;
	adev->doorbell_index.mec_ring7 = AMDGPU_NAVI10_DOORBELL_MEC_RING7;
	adev->doorbell_index.userqueue_start = AMDGPU_NAVI10_DOORBELL_USERQUEUE_START;
	adev->doorbell_index.userqueue_end = AMDGPU_NAVI10_DOORBELL_USERQUEUE_END;
	adev->doorbell_index.gfx_ring0 = AMDGPU_NAVI10_DOORBELL_GFX_RING0;
	adev->doorbell_index.gfx_ring1 = AMDGPU_NAVI10_DOORBELL_GFX_RING1;
	adev->doorbell_index.gfx_userqueue_start =
		AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_START;
	adev->doorbell_index.gfx_userqueue_end =
		AMDGPU_NAVI10_DOORBELL_GFX_USERQUEUE_END;
	adev->doorbell_index.mes_ring0 = AMDGPU_NAVI10_DOORBELL_MES_RING0;
	adev->doorbell_index.mes_ring1 = AMDGPU_NAVI10_DOORBELL_MES_RING1;
	adev->doorbell_index.sdma_engine[0] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE0;
	adev->doorbell_index.sdma_engine[1] = AMDGPU_NAVI10_DOORBELL_sDMA_ENGINE1;
	adev->doorbell_index.ih = AMDGPU_NAVI10_DOORBELL_IH;
	adev->doorbell_index.vcn.vcn_ring0_1 = AMDGPU_NAVI10_DOORBELL64_VCN0_1;
	adev->doorbell_index.vcn.vcn_ring2_3 = AMDGPU_NAVI10_DOORBELL64_VCN2_3;
	adev->doorbell_index.vcn.vcn_ring4_5 = AMDGPU_NAVI10_DOORBELL64_VCN4_5;
	adev->doorbell_index.vcn.vcn_ring6_7 = AMDGPU_NAVI10_DOORBELL64_VCN6_7;
	adev->doorbell_index.first_non_cp = AMDGPU_NAVI10_DOORBELL64_FIRST_NON_CP;
	adev->doorbell_index.last_non_cp = AMDGPU_NAVI10_DOORBELL64_LAST_NON_CP;

	adev->doorbell_index.max_assignment = AMDGPU_NAVI10_DOORBELL_MAX_ASSIGNMENT << 1;
	adev->doorbell_index.sdma_doorbell_range = 20;
}

static void soc24_pre_asic_init(struct amdgpu_device *adev)
{
}

static int soc24_update_umd_stable_pstate(struct amdgpu_device *adev,
					  bool enter)
{
	if (enter)
		amdgpu_gfx_rlc_enter_safe_mode(adev, 0);
	else
		amdgpu_gfx_rlc_exit_safe_mode(adev, 0);

	if (adev->gfx.funcs->update_perfmon_mgcg)
		adev->gfx.funcs->update_perfmon_mgcg(adev, !enter);

	return 0;
}

static const struct amdgpu_asic_funcs soc24_asic_funcs = {
	.read_bios_from_rom = &amdgpu_soc15_read_bios_from_rom,
	.read_register = &soc24_read_register,
	.reset = &soc24_asic_reset,
	.reset_method = &soc24_asic_reset_method,
	.get_xclk = &soc24_get_xclk,
	.get_config_memsize = &soc24_get_config_memsize,
	.init_doorbell_index = &soc24_init_doorbell_index,
	.need_full_reset = &soc24_need_full_reset,
	.need_reset_on_init = &soc24_need_reset_on_init,
	.get_pcie_replay_count = &soc24_get_pcie_replay_count,
	.supports_baco = &amdgpu_dpm_is_baco_supported,
	.pre_asic_init = &soc24_pre_asic_init,
	.query_video_codecs = &soc24_query_video_codecs,
	.update_umd_stable_pstate = &soc24_update_umd_stable_pstate,
};

static int soc24_common_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->nbio.funcs->set_reg_remap(adev);
	adev->smc_rreg = NULL;
	adev->smc_wreg = NULL;
	adev->pcie_rreg = &amdgpu_device_indirect_rreg;
	adev->pcie_wreg = &amdgpu_device_indirect_wreg;
	adev->pcie_rreg64 = &amdgpu_device_indirect_rreg64;
	adev->pcie_wreg64 = &amdgpu_device_indirect_wreg64;
	adev->pciep_rreg = amdgpu_device_pcie_port_rreg;
	adev->pciep_wreg = amdgpu_device_pcie_port_wreg;
	adev->uvd_ctx_rreg = NULL;
	adev->uvd_ctx_wreg = NULL;
	adev->didt_rreg = NULL;
	adev->didt_wreg = NULL;

	adev->asic_funcs = &soc24_asic_funcs;

	adev->rev_id = amdgpu_device_get_rev_id(adev);
	adev->external_rev_id = 0xff;

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 0, 0):
		adev->cg_flags = AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_REPEATER_FGCG |
			AMD_CG_SUPPORT_GFX_FGCG |
			AMD_CG_SUPPORT_GFX_PERF_CLK |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_HDP_SD |
			AMD_CG_SUPPORT_MC_LS;
		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_VCN_DPG;
		adev->external_rev_id = adev->rev_id + 0x40;
		break;
	case IP_VERSION(12, 0, 1):
		adev->cg_flags = AMD_CG_SUPPORT_GFX_CGCG |
			AMD_CG_SUPPORT_GFX_CGLS |
			AMD_CG_SUPPORT_GFX_MGCG |
			AMD_CG_SUPPORT_GFX_3D_CGCG |
			AMD_CG_SUPPORT_GFX_3D_CGLS |
			AMD_CG_SUPPORT_REPEATER_FGCG |
			AMD_CG_SUPPORT_GFX_FGCG |
			AMD_CG_SUPPORT_GFX_PERF_CLK |
			AMD_CG_SUPPORT_ATHUB_MGCG |
			AMD_CG_SUPPORT_ATHUB_LS |
			AMD_CG_SUPPORT_MC_MGCG |
			AMD_CG_SUPPORT_HDP_SD |
			AMD_CG_SUPPORT_MC_LS;

		adev->pg_flags = AMD_PG_SUPPORT_VCN |
			AMD_PG_SUPPORT_JPEG |
			AMD_PG_SUPPORT_JPEG_DPG |
			AMD_PG_SUPPORT_VCN_DPG;
		adev->external_rev_id = adev->rev_id + 0x50;
		break;
	default:
		/* FIXME: not supported yet */
		return -EINVAL;
	}

	if (amdgpu_sriov_vf(adev)) {
		amdgpu_virt_init_setting(adev);
		xgpu_nv_mailbox_set_irq_funcs(adev);
	}

	return 0;
}

static int soc24_common_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_get_irq(adev);

	/* Enable selfring doorbell aperture late because doorbell BAR
	 * aperture will change if resize BAR successfully in gmc sw_init.
	 */
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, true);

	return 0;
}

static int soc24_common_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_add_irq_id(adev);

	return 0;
}

static int soc24_common_sw_fini(void *handle)
{
	return 0;
}

static int soc24_common_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* enable aspm */
	soc24_program_aspm(adev);
	/* setup nbio registers */
	adev->nbio.funcs->init_registers(adev);
	/* remap HDP registers to a hole in mmio space,
	 * for the purpose of expose those registers
	 * to process space
	 */
	if (adev->nbio.funcs->remap_hdp_registers)
		adev->nbio.funcs->remap_hdp_registers(adev);

	if (adev->df.funcs->hw_init)
		adev->df.funcs->hw_init(adev);

	/* enable the doorbell aperture */
	adev->nbio.funcs->enable_doorbell_aperture(adev, true);

	return 0;
}

static int soc24_common_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* Disable the doorbell aperture and selfring doorbell aperture
	 * separately in hw_fini because soc21_enable_doorbell_aperture
	 * has been removed and there is no need to delay disabling
	 * selfring doorbell.
	 */
	adev->nbio.funcs->enable_doorbell_aperture(adev, false);
	adev->nbio.funcs->enable_doorbell_selfring_aperture(adev, false);

	if (amdgpu_sriov_vf(adev))
		xgpu_nv_mailbox_put_irq(adev);

	return 0;
}

static int soc24_common_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return soc24_common_hw_fini(adev);
}

static int soc24_common_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return soc24_common_hw_init(adev);
}

static bool soc24_common_is_idle(void *handle)
{
	return true;
}

static int soc24_common_wait_for_idle(void *handle)
{
	return 0;
}

static int soc24_common_soft_reset(void *handle)
{
	return 0;
}

static int soc24_common_set_clockgating_state(void *handle,
					      enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (amdgpu_ip_version(adev, NBIO_HWIP, 0)) {
	case IP_VERSION(6, 3, 1):
		adev->nbio.funcs->update_medium_grain_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		adev->nbio.funcs->update_medium_grain_light_sleep(adev,
				state == AMD_CG_STATE_GATE);
		adev->hdp.funcs->update_clock_gating(adev,
				state == AMD_CG_STATE_GATE);
		break;
	default:
		break;
	}
	return 0;
}

static int soc24_common_set_powergating_state(void *handle,
					      enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (amdgpu_ip_version(adev, LSDMA_HWIP, 0)) {
	case IP_VERSION(7, 0, 0):
	case IP_VERSION(7, 0, 1):
		adev->lsdma.funcs->update_memory_power_gating(adev,
				state == AMD_PG_STATE_GATE);
		break;
	default:
		break;
	}

	return 0;
}

static void soc24_common_get_clockgating_state(void *handle, u64 *flags)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->nbio.funcs->get_clockgating_state(adev, flags);

	adev->hdp.funcs->get_clock_gating_state(adev, flags);

	return;
}

static const struct amd_ip_funcs soc24_common_ip_funcs = {
	.name = "soc24_common",
	.early_init = soc24_common_early_init,
	.late_init = soc24_common_late_init,
	.sw_init = soc24_common_sw_init,
	.sw_fini = soc24_common_sw_fini,
	.hw_init = soc24_common_hw_init,
	.hw_fini = soc24_common_hw_fini,
	.suspend = soc24_common_suspend,
	.resume = soc24_common_resume,
	.is_idle = soc24_common_is_idle,
	.wait_for_idle = soc24_common_wait_for_idle,
	.soft_reset = soc24_common_soft_reset,
	.set_clockgating_state = soc24_common_set_clockgating_state,
	.set_powergating_state = soc24_common_set_powergating_state,
	.get_clockgating_state = soc24_common_get_clockgating_state,
};
