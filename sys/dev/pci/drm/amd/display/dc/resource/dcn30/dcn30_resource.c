/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */


#include "dm_services.h"
#include "dc.h"

#include "dcn30/dcn30_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn20/dcn20_resource.h"

#include "dcn30_resource.h"

#include "dcn10/dcn10_ipp.h"
#include "dcn30/dcn30_hubbub.h"
#include "dcn30/dcn30_mpc.h"
#include "dcn30/dcn30_hubp.h"
#include "irq/dcn30/irq_service_dcn30.h"
#include "dcn30/dcn30_dpp.h"
#include "dcn30/dcn30_optc.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn30/dcn30_hwseq.h"
#include "dce110/dce110_hwseq.h"
#include "dcn30/dcn30_opp.h"
#include "dcn20/dcn20_dsc.h"
#include "dcn30/dcn30_vpg.h"
#include "dcn30/dcn30_afmt.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn30/dcn30_dio_link_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "virtual/virtual_stream_encoder.h"
#include "dce110/dce110_resource.h"
#include "dml/display_mode_vba.h"
#include "dcn30/dcn30_dccg.h"
#include "dcn10/dcn10_resource.h"
#include "link.h"
#include "dce/dce_panel_cntl.h"

#include "dcn30/dcn30_dwb.h"
#include "dcn30/dcn30_mmhubbub.h"

#include "sienna_cichlid_ip_offset.h"
#include "dcn/dcn_3_0_0_offset.h"
#include "dcn/dcn_3_0_0_sh_mask.h"

#include "nbio/nbio_7_4_offset.h"

#include "dpcs/dpcs_3_0_0_offset.h"
#include "dpcs/dpcs_3_0_0_sh_mask.h"

#include "mmhub/mmhub_2_0_0_offset.h"
#include "mmhub/mmhub_2_0_0_sh_mask.h"

#include "reg_helper.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

#include "dml/dcn30/dcn30_fpu.h"
#include "dml/dcn30/display_mode_vba_30.h"
#include "vm_helper.h"
#include "dcn20/dcn20_vmid.h"
#include "amdgpu_socbb.h"
#include "dc_dmub_srv.h"

#define DC_LOGGER \
	dc->ctx->logger
#define DC_LOGGER_INIT(logger)

enum dcn30_clk_src_array_id {
	DCN30_CLK_SRC_PLL0,
	DCN30_CLK_SRC_PLL1,
	DCN30_CLK_SRC_PLL2,
	DCN30_CLK_SRC_PLL3,
	DCN30_CLK_SRC_PLL4,
	DCN30_CLK_SRC_PLL5,
	DCN30_CLK_SRC_TOTAL
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file
 */

/* DCN */
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(mm ## reg_name ## _BASE_IDX) +  \
					mm ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRI2(reg_name, block, id)\
	.reg_name = BASE(mm ## reg_name ## _BASE_IDX) + \
					mm ## reg_name

#define SRIR(var_name, reg_name, block, id)\
	.var_name = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRII_MPC_RMU(reg_name, block, id)\
	.RMU##_##reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define SRII_DWB(reg_name, temp_name, block, id)\
	.reg_name[id] = BASE(mm ## block ## id ## _ ## temp_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## temp_name

#define SF_DWB2(reg_name, block, id, field_name, post_fix)	\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DCCG_SRII(reg_name, block, id)\
	.block ## _ ## reg_name[id] = BASE(mm ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
	.reg_name[id] = BASE(mm ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
					mm ## reg_name ## _ ## block ## id

/* NBIO */
#define NBIO_BASE_INNER(seg) \
	NBIO_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(mm ## reg_name ## _BASE_IDX) + \
					mm ## reg_name

/* MMHUB */
#define MMHUB_BASE_INNER(seg) \
	MMHUB_BASE__INST0_SEG ## seg

#define MMHUB_BASE(seg) \
	MMHUB_BASE_INNER(seg)

#define MMHUB_SR(reg_name)\
		.reg_name = MMHUB_BASE(mmMM ## reg_name ## _BASE_IDX) + \
					mmMM ## reg_name

/* CLOCK */
#define CLK_BASE_INNER(seg) \
	CLK_BASE__INST0_SEG ## seg

#define CLK_BASE(seg) \
	CLK_BASE_INNER(seg)

#define CLK_SRI(reg_name, block, inst)\
	.reg_name = CLK_BASE(mm ## block ## _ ## inst ## _ ## reg_name ## _BASE_IDX) + \
					mm ## block ## _ ## inst ## _ ## reg_name


static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

#define clk_src_regs(index, pllid)\
[index] = {\
	CS_COMMON_REG_LIST_DCN2_0(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D),
	clk_src_regs(4, E),
	clk_src_regs(5, F)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

#define abm_regs(id)\
[id] = {\
		ABM_DCN30_REG_LIST(id)\
}

static const struct dce_abm_registers abm_regs[] = {
		abm_regs(0),
		abm_regs(1),
		abm_regs(2),
		abm_regs(3),
		abm_regs(4),
		abm_regs(5),
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN30(_MASK)
};



#define audio_regs(id)\
[id] = {\
		AUD_COMMON_REG_LIST(id)\
}

static const struct dce_audio_registers audio_regs[] = {
	audio_regs(0),
	audio_regs(1),
	audio_regs(2),
	audio_regs(3),
	audio_regs(4),
	audio_regs(5),
	audio_regs(6)
};

#define DCE120_AUD_COMMON_MASK_SH_LIST(mask_sh)\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh),\
		AUD_COMMON_MASK_SH_LIST_BASE(mask_sh)

static const struct dce_audio_shift audio_shift = {
		DCE120_AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_audio_mask audio_mask = {
		DCE120_AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define vpg_regs(id)\
[id] = {\
	VPG_DCN3_REG_LIST(id)\
}

static const struct dcn30_vpg_registers vpg_regs[] = {
	vpg_regs(0),
	vpg_regs(1),
	vpg_regs(2),
	vpg_regs(3),
	vpg_regs(4),
	vpg_regs(5),
	vpg_regs(6),
};

static const struct dcn30_vpg_shift vpg_shift = {
	DCN3_VPG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_vpg_mask vpg_mask = {
	DCN3_VPG_MASK_SH_LIST(_MASK)
};

#define afmt_regs(id)\
[id] = {\
	AFMT_DCN3_REG_LIST(id)\
}

static const struct dcn30_afmt_registers afmt_regs[] = {
	afmt_regs(0),
	afmt_regs(1),
	afmt_regs(2),
	afmt_regs(3),
	afmt_regs(4),
	afmt_regs(5),
	afmt_regs(6),
};

static const struct dcn30_afmt_shift afmt_shift = {
	DCN3_AFMT_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_afmt_mask afmt_mask = {
	DCN3_AFMT_MASK_SH_LIST(_MASK)
};

#define stream_enc_regs(id)\
[id] = {\
	SE_DCN3_REG_LIST(id)\
}

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4),
	stream_enc_regs(5)
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN30(_MASK)
};


#define aux_regs(id)\
[id] = {\
	DCN2_AUX_REG_LIST(id)\
}

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4),
		aux_regs(5)
};

#define hpd_regs(id)\
[id] = {\
	HPD_REG_LIST(id)\
}

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
		hpd_regs(2),
		hpd_regs(3),
		hpd_regs(4),
		hpd_regs(5)
};

#define link_regs(id, phyid)\
[id] = {\
	LE_DCN3_REG_LIST(id), \
	UNIPHY_DCN2_REG_LIST(phyid), \
	DPCS_DCN2_REG_LIST(id), \
	SRI(DP_DPHY_INTERNAL_CTRL, DP, id) \
}

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)
};

static const struct dcn10_link_enc_registers link_enc_regs[] = {
	link_regs(0, A),
	link_regs(1, B),
	link_regs(2, C),
	link_regs(3, D),
	link_regs(4, E),
	link_regs(5, F)
};

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN30(__SHIFT),\
	DPCS_DCN2_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN30(_MASK),\
	DPCS_DCN2_MASK_SH_LIST(_MASK)
};


static const struct dce_panel_cntl_registers panel_cntl_regs[] = {
	{ DCN_PANEL_CNTL_REG_LIST() }
};

static const struct dce_panel_cntl_shift panel_cntl_shift = {
	DCE_PANEL_CNTL_MASK_SH_LIST(__SHIFT)
};

static const struct dce_panel_cntl_mask panel_cntl_mask = {
	DCE_PANEL_CNTL_MASK_SH_LIST(_MASK)
};

#define dpp_regs(id)\
[id] = {\
	DPP_REG_LIST_DCN30(id),\
}

static const struct dcn3_dpp_registers dpp_regs[] = {
	dpp_regs(0),
	dpp_regs(1),
	dpp_regs(2),
	dpp_regs(3),
	dpp_regs(4),
	dpp_regs(5),
};

static const struct dcn3_dpp_shift tf_shift = {
		DPP_REG_LIST_SH_MASK_DCN30(__SHIFT)
};

static const struct dcn3_dpp_mask tf_mask = {
		DPP_REG_LIST_SH_MASK_DCN30(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN30(id),\
}

static const struct dcn20_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3),
	opp_regs(4),
	opp_regs(5)
};

static const struct dcn20_opp_shift opp_shift = {
	OPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn20_opp_mask opp_mask = {
	OPP_MASK_SH_LIST_DCN20(_MASK)
};

#define aux_engine_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST0(id), \
	.AUXN_IMPCAL = 0, \
	.AUXP_IMPCAL = 0, \
	.AUX_RESET_MASK = DP_AUX0_AUX_CONTROL__AUX_RESET_MASK, \
}

static const struct dce110_aux_registers aux_engine_regs[] = {
		aux_engine_regs(0),
		aux_engine_regs(1),
		aux_engine_regs(2),
		aux_engine_regs(3),
		aux_engine_regs(4),
		aux_engine_regs(5)
};

#define dwbc_regs_dcn3(id)\
[id] = {\
	DWBC_COMMON_REG_LIST_DCN30(id),\
}

static const struct dcn30_dwbc_registers dwbc30_regs[] = {
	dwbc_regs_dcn3(0),
};

static const struct dcn30_dwbc_shift dwbc30_shift = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_dwbc_mask dwbc30_mask = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define mcif_wb_regs_dcn3(id)\
[id] = {\
	MCIF_WB_COMMON_REG_LIST_DCN30(id),\
}

static const struct dcn30_mmhubbub_registers mcif_wb30_regs[] = {
	mcif_wb_regs_dcn3(0)
};

static const struct dcn30_mmhubbub_shift mcif_wb30_shift = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_mmhubbub_mask mcif_wb30_mask = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define dsc_regsDCN20(id)\
[id] = {\
	DSC_REG_LIST_DCN20(id)\
}

static const struct dcn20_dsc_registers dsc_regs[] = {
	dsc_regsDCN20(0),
	dsc_regsDCN20(1),
	dsc_regsDCN20(2),
	dsc_regsDCN20(3),
	dsc_regsDCN20(4),
	dsc_regsDCN20(5)
};

static const struct dcn20_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

static const struct dcn30_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN3_0(0),
		MPC_REG_LIST_DCN3_0(1),
		MPC_REG_LIST_DCN3_0(2),
		MPC_REG_LIST_DCN3_0(3),
		MPC_REG_LIST_DCN3_0(4),
		MPC_REG_LIST_DCN3_0(5),
		MPC_OUT_MUX_REG_LIST_DCN3_0(0),
		MPC_OUT_MUX_REG_LIST_DCN3_0(1),
		MPC_OUT_MUX_REG_LIST_DCN3_0(2),
		MPC_OUT_MUX_REG_LIST_DCN3_0(3),
		MPC_OUT_MUX_REG_LIST_DCN3_0(4),
		MPC_OUT_MUX_REG_LIST_DCN3_0(5),
		MPC_RMU_GLOBAL_REG_LIST_DCN3AG,
		MPC_RMU_REG_LIST_DCN3AG(0),
		MPC_RMU_REG_LIST_DCN3AG(1),
		MPC_RMU_REG_LIST_DCN3AG(2),
		MPC_DWB_MUX_REG_LIST_DCN3_0(0),
};

static const struct dcn30_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define optc_regs(id)\
[id] = {OPTC_COMMON_REG_LIST_DCN3_0(id)}


static const struct dcn_optc_registers optc_regs[] = {
	optc_regs(0),
	optc_regs(1),
	optc_regs(2),
	optc_regs(3),
	optc_regs(4),
	optc_regs(5)
};

static const struct dcn_optc_shift optc_shift = {
	OPTC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_optc_mask optc_mask = {
	OPTC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define hubp_regs(id)\
[id] = {\
	HUBP_REG_LIST_DCN30(id)\
}

static const struct dcn_hubp2_registers hubp_regs[] = {
		hubp_regs(0),
		hubp_regs(1),
		hubp_regs(2),
		hubp_regs(3),
		hubp_regs(4),
		hubp_regs(5)
};

static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN30(_MASK)
};

static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN30(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN30(_MASK)
};

static const struct dccg_registers dccg_regs = {
		DCCG_REG_LIST_DCN30()
};

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN3(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN3(_MASK)
};

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN30_REG_LIST()
};

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN30_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN30_MASK_SH_LIST(_MASK)
};
#define vmid_regs(id)\
[id] = {\
		DCN20_VMID_REG_LIST(id)\
}

static const struct dcn_vmid_registers vmid_regs[] = {
	vmid_regs(0),
	vmid_regs(1),
	vmid_regs(2),
	vmid_regs(3),
	vmid_regs(4),
	vmid_regs(5),
	vmid_regs(6),
	vmid_regs(7),
	vmid_regs(8),
	vmid_regs(9),
	vmid_regs(10),
	vmid_regs(11),
	vmid_regs(12),
	vmid_regs(13),
	vmid_regs(14),
	vmid_regs(15)
};

static const struct dcn20_vmid_shift vmid_shifts = {
		DCN20_VMID_MASK_SH_LIST(__SHIFT)
};

static const struct dcn20_vmid_mask vmid_masks = {
		DCN20_VMID_MASK_SH_LIST(_MASK)
};

static const struct resource_caps res_cap_dcn3 = {
	.num_timing_generator = 6,
	.num_opp = 6,
	.num_video_plane = 6,
	.num_audio = 6,
	.num_stream_encoder = 6,
	.num_pll = 6,
	.num_dwb = 1,
	.num_ddc = 6,
	.num_vmid = 16,
	.num_mpc_3dlut = 3,
	.num_dsc = 6,
};

static const struct dc_plane_cap plane_cap = {
	.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
	.per_pixel_alpha = true,

	.pixel_format_support = {
			.argb8888 = true,
			.nv12 = true,
			.fp16 = true,
			.p010 = true,
			.ayuv = false,
	},

	.max_upscale_factor = {
			.argb8888 = 16000,
			.nv12 = 16000,
			.fp16 = 16000
	},

	/* 6:1 downscaling ratio: 1000/6 = 166.666 */
	.max_downscale_factor = {
			.argb8888 = 167,
			.nv12 = 167,
			.fp16 = 167
	},
	16,
	16
};

static const struct dc_debug_options debug_defaults_drv = {
	.disable_dmcu = true, //No DMCU on DCN30
	.force_abm_enable = false,
	.timing_trace = false,
	.clock_trace = true,
	.disable_pplib_clock_request = true,
	.pipe_split_policy = MPC_SPLIT_DYNAMIC,
	.force_single_disp_pipe_split = false,
	.disable_dcc = DCC_ENABLE,
	.vsr_support = true,
	.performance_trace = false,
	.max_downscale_src_width = 7680,/*upto 8K*/
	.disable_pplib_wm_range = false,
	.scl_reset_length10 = true,
	.sanity_checks = false,
	.underflow_assert_delay_us = 0xFFFFFFFF,
	.dwb_fi_phase = -1, // -1 = disable,
	.dmub_command_table = true,
	.use_max_lb = true,
	.exit_idle_opt_for_cursor_updates = true,
	.enable_legacy_fast_update = false,
	.using_dml2 = false,
};

static const struct dc_panel_config panel_config_defaults = {
	.psr = {
		.disable_psr = false,
		.disallow_psrsu = false,
		.disallow_replay = false,
	},
};

static void dcn30_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN20_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn30_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn3_dpp *dpp =
		kzalloc(sizeof(struct dcn3_dpp), GFP_KERNEL);

	if (!dpp)
		return NULL;

	if (dpp3_construct(dpp, ctx, inst,
			&dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp);
	return NULL;
}

static struct output_pixel_processor *dcn30_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp =
		kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_opp_construct(opp, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

static struct dce_aux *dcn30_aux_engine_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine =
		kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

	dce110_aux_engine_construct(aux_engine, ctx, inst,
				    SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
				    &aux_engine_regs[inst],
					&aux_mask,
					&aux_shift,
					ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}

#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST_DCN30(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2),
		i2c_inst_regs(3),
		i2c_inst_regs(4),
		i2c_inst_regs(5),
		i2c_inst_regs(6),
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct dce_i2c_hw *dcn30_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}

static struct mpc *dcn30_mpc_create(
		struct dc_context *ctx,
		int num_mpcc,
		int num_rmu)
{
	struct dcn30_mpc *mpc30 = kzalloc(sizeof(struct dcn30_mpc),
					  GFP_KERNEL);

	if (!mpc30)
		return NULL;

	dcn30_mpc_construct(mpc30, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			num_mpcc,
			num_rmu);

	return &mpc30->base;
}

static struct hubbub *dcn30_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub3 = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_KERNEL);

	if (!hubbub3)
		return NULL;

	hubbub3_construct(hubbub3, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask);


	for (i = 0; i < res_cap_dcn3.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub3->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub3->base;
}

static struct timing_generator *dcn30_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance)
{
	struct optc *tgn10 =
		kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &optc_regs[instance];
	tgn10->tg_shift = &optc_shift;
	tgn10->tg_mask = &optc_mask;

	dcn30_timing_generator_init(tgn10);

	return &tgn10->base;
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 600000,
		.hdmi_ycbcr420_supported = true,
		.dp_ycbcr420_supported = true,
		.fec_supported = true,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_HBR3_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_TPS4_CAPABLE = true
};

static struct link_encoder *dcn30_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20)
		return NULL;

	dcn30_link_encoder_construct(enc20,
			enc_init_data,
			&link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter],
			&link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source],
			&le_shift,
			&le_mask);

	return &enc20->enc10.base;
}

static struct panel_cntl *dcn30_panel_cntl_create(const struct panel_cntl_init_data *init_data)
{
	struct dce_panel_cntl *panel_cntl =
		kzalloc(sizeof(struct dce_panel_cntl), GFP_KERNEL);

	if (!panel_cntl)
		return NULL;

	dce_panel_cntl_construct(panel_cntl,
			init_data,
			&panel_cntl_regs[init_data->inst],
			&panel_cntl_shift,
			&panel_cntl_mask);

	return &panel_cntl->base;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	generic_reg_get(ctx, mmDC_PINSTRAPS + BASE(mmDC_PINSTRAPS_BASE_IDX),
		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);

}

static struct audio *dcn30_create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct vpg *dcn30_vpg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn30_vpg *vpg3 = kzalloc(sizeof(struct dcn30_vpg), GFP_KERNEL);

	if (!vpg3)
		return NULL;

	vpg3_construct(vpg3, ctx, inst,
			&vpg_regs[inst],
			&vpg_shift,
			&vpg_mask);

	return &vpg3->base;
}

static struct afmt *dcn30_afmt_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn30_afmt *afmt3 = kzalloc(sizeof(struct dcn30_afmt), GFP_KERNEL);

	if (!afmt3)
		return NULL;

	afmt3_construct(afmt3, ctx, inst,
			&afmt_regs[inst],
			&afmt_shift,
			&afmt_mask);

	return &afmt3->base;
}

static struct stream_encoder *dcn30_stream_encoder_create(enum engine_id eng_id,
							  struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct afmt *afmt;
	int vpg_inst;
	int afmt_inst;

	/* Mapping of VPG, AFMT, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGF) {
		vpg_inst = eng_id;
		afmt_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn30_vpg_create(ctx, vpg_inst);
	afmt = dcn30_afmt_create(ctx, afmt_inst);

	if (!enc1 || !vpg || !afmt) {
		kfree(enc1);
		kfree(vpg);
		kfree(afmt);
		return NULL;
	}

	dcn30_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios,
					eng_id, vpg, afmt,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);

	return &enc1->base;
}

static struct dce_hwseq *dcn30_hwseq_create(struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
	}
	return hws;
}
static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = dcn30_create_audio,
	.create_stream_encoder = dcn30_stream_encoder_create,
	.create_hwseq = dcn30_hwseq_create,
};

static void dcn30_resource_destruct(struct dcn30_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			if (pool->base.stream_enc[i]->vpg != NULL) {
				kfree(DCN30_VPG_FROM_VPG(pool->base.stream_enc[i]->vpg));
				pool->base.stream_enc[i]->vpg = NULL;
			}
			if (pool->base.stream_enc[i]->afmt != NULL) {
				kfree(DCN30_AFMT_FROM_AFMT(pool->base.stream_enc[i]->afmt));
				pool->base.stream_enc[i]->afmt = NULL;
			}
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		if (pool->base.dscs[i] != NULL)
			dcn20_dsc_destroy(&pool->base.dscs[i]);
	}

	if (pool->base.mpc != NULL) {
		kfree(TO_DCN20_MPC(pool->base.mpc));
		pool->base.mpc = NULL;
	}
	if (pool->base.hubbub != NULL) {
		kfree(pool->base.hubbub);
		pool->base.hubbub = NULL;
	}
	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.dpps[i] != NULL)
			dcn30_dpp_destroy(&pool->base.dpps[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.hubps[i] != NULL) {
			kfree(TO_DCN20_HUBP(pool->base.hubps[i]));
			pool->base.hubps[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
		}
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		if (pool->base.engines[i] != NULL)
			dce110_engine_destroy(&pool->base.engines[i]);
		if (pool->base.hw_i2cs[i] != NULL) {
			kfree(pool->base.hw_i2cs[i]);
			pool->base.hw_i2cs[i] = NULL;
		}
		if (pool->base.sw_i2cs[i] != NULL) {
			kfree(pool->base.sw_i2cs[i]);
			pool->base.sw_i2cs[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		if (pool->base.opps[i] != NULL)
			pool->base.opps[i]->funcs->opp_destroy(&pool->base.opps[i]);
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.timing_generators[i] != NULL)	{
			kfree(DCN10TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_dwb; i++) {
		if (pool->base.dwbc[i] != NULL) {
			kfree(TO_DCN30_DWBC(pool->base.dwbc[i]));
			pool->base.dwbc[i] = NULL;
		}
		if (pool->base.mcif_wb[i] != NULL) {
			kfree(TO_DCN30_MMHUBBUB(pool->base.mcif_wb[i]));
			pool->base.mcif_wb[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dcn20_clock_source_destroy(&pool->base.clock_sources[i]);
			pool->base.clock_sources[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_mpc_3dlut; i++) {
		if (pool->base.mpc_lut[i] != NULL) {
			dc_3dlut_func_release(pool->base.mpc_lut[i]);
			pool->base.mpc_lut[i] = NULL;
		}
		if (pool->base.mpc_shaper[i] != NULL) {
			dc_transfer_func_release(pool->base.mpc_shaper[i]);
			pool->base.mpc_shaper[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn20_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}

	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.multiple_abms[i] != NULL)
			dce_abm_destroy(&pool->base.multiple_abms[i]);
	}

	if (pool->base.psr != NULL)
		dmub_psr_destroy(&pool->base.psr);

	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);

	if (pool->base.oem_device != NULL) {
		struct dc *dc = pool->base.oem_device->ctx->dc;

		dc->link_srv->destroy_ddc_service(&pool->base.oem_device);
	}
}

static struct hubp *dcn30_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn20_hubp *hubp2 =
		kzalloc(sizeof(struct dcn20_hubp), GFP_KERNEL);

	if (!hubp2)
		return NULL;

	if (hubp3_construct(hubp2, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

static bool dcn30_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_dwbc *dwbc30 = kzalloc(sizeof(struct dcn30_dwbc),
						    GFP_KERNEL);

		if (!dwbc30) {
			dm_error("DC: failed to create dwbc30!\n");
			return false;
		}

		dcn30_dwbc_construct(dwbc30, ctx,
				&dwbc30_regs[i],
				&dwbc30_shift,
				&dwbc30_mask,
				i);

		pool->dwbc[i] = &dwbc30->base;
	}
	return true;
}

static bool dcn30_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_mmhubbub *mcif_wb30 = kzalloc(sizeof(struct dcn30_mmhubbub),
						    GFP_KERNEL);

		if (!mcif_wb30) {
			dm_error("DC: failed to create mcif_wb30!\n");
			return false;
		}

		dcn30_mmhubbub_construct(mcif_wb30, ctx,
				&mcif_wb30_regs[i],
				&mcif_wb30_shift,
				&mcif_wb30_mask,
				i);

		pool->mcif_wb[i] = &mcif_wb30->base;
	}
	return true;
}

static struct display_stream_compressor *dcn30_dsc_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_dsc *dsc =
		kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);
	return &dsc->base;
}

enum dc_status dcn30_add_stream_to_ctx(struct dc *dc, struct dc_state *new_ctx, struct dc_stream_state *dc_stream)
{

	return dcn20_add_stream_to_ctx(dc, new_ctx, dc_stream);
}

static void dcn30_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn30_resource_pool *dcn30_pool = TO_DCN30_RES_POOL(*pool);

	dcn30_resource_destruct(dcn30_pool);
	kfree(dcn30_pool);
	*pool = NULL;
}

static struct clock_source *dcn30_clock_source_create(
		struct dc_context *ctx,
		struct dc_bios *bios,
		enum clock_source_id id,
		const struct dce110_clk_src_regs *regs,
		bool dp_clk_src)
{
	struct dce110_clk_src *clk_src =
		kzalloc(sizeof(struct dce110_clk_src), GFP_KERNEL);

	if (!clk_src)
		return NULL;

	if (dcn3_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	kfree(clk_src);
	BREAK_TO_DEBUGGER();
	return NULL;
}

int dcn30_populate_dml_pipes_from_context(
	struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes,
	bool fast_validate)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;

	DC_FP_START();
	dcn20_populate_dml_pipes_from_context(dc, context, pipes, fast_validate);
	DC_FP_END();

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		if (!res_ctx->pipe_ctx[i].stream)
			continue;

		pipes[pipe_cnt++].pipe.scale_ratio_depth.lb_depth =
			dm_lb_16;
	}

	return pipe_cnt;
}

void dcn30_populate_dml_writeback_from_context(
	struct dc *dc, struct resource_context *res_ctx, display_e2e_pipe_params_st *pipes)
{
	DC_FP_START();
	dcn30_fpu_populate_dml_writeback_from_context(dc, res_ctx, pipes);
	DC_FP_END();
}

unsigned int dcn30_calc_max_scaled_time(
		unsigned int time_per_pixel,
		enum mmhubbub_wbif_mode mode,
		unsigned int urgent_watermark)
{
	unsigned int time_per_byte = 0;
	unsigned int total_free_entry = 0xb40;
	unsigned int buf_lh_capability;
	unsigned int max_scaled_time;

	if (mode == PACKED_444) /* packed mode 32 bpp */
		time_per_byte = time_per_pixel/4;
	else if (mode == PACKED_444_FP16) /* packed mode 64 bpp */
		time_per_byte = time_per_pixel/8;

	if (time_per_byte == 0)
		time_per_byte = 1;

	buf_lh_capability = (total_free_entry*time_per_byte*32) >> 6; /* time_per_byte is in u6.6*/
	max_scaled_time   = buf_lh_capability - urgent_watermark;
	return max_scaled_time;
}

void dcn30_set_mcif_arb_params(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt)
{
	enum mmhubbub_wbif_mode wbif_mode;
	struct display_mode_lib *dml = &context->bw_ctx.dml;
	struct mcif_arb_params *wb_arb_params;
	int i, j, dwb_pipe;

	/* Writeback MCIF_WB arbitration parameters */
	dwb_pipe = 0;
	for (i = 0; i < dc->res_pool->pipe_count; i++) {

		if (!context->res_ctx.pipe_ctx[i].stream)
			continue;

		for (j = 0; j < MAX_DWB_PIPES; j++) {
			struct dc_writeback_info *writeback_info = &context->res_ctx.pipe_ctx[i].stream->writeback_info[j];

			if (writeback_info->wb_enabled == false)
				continue;

			//wb_arb_params = &context->res_ctx.pipe_ctx[i].stream->writeback_info[j].mcif_arb_params;
			wb_arb_params = &context->bw_ctx.bw.dcn.bw_writeback.mcif_wb_arb[dwb_pipe];

			if (writeback_info->dwb_params.cnv_params.fc_out_format == DWB_OUT_FORMAT_64BPP_ARGB ||
				writeback_info->dwb_params.cnv_params.fc_out_format == DWB_OUT_FORMAT_64BPP_RGBA)
				wbif_mode = PACKED_444_FP16;
			else
				wbif_mode = PACKED_444;

			DC_FP_START();
			dcn30_fpu_set_mcif_arb_params(wb_arb_params, dml, pipes, pipe_cnt, j);
			DC_FP_END();
			wb_arb_params->time_per_pixel = (1000000 << 6) / context->res_ctx.pipe_ctx[i].stream->phy_pix_clk; /* time_per_pixel should be in u6.6 format */
			wb_arb_params->slice_lines = 32;
			wb_arb_params->arbitration_slice = 2; /* irrelevant since there is no YUV output */
			wb_arb_params->max_scaled_time = dcn30_calc_max_scaled_time(wb_arb_params->time_per_pixel,
					wbif_mode,
					wb_arb_params->cli_watermark[0]); /* assume 4 watermark sets have the same value */

			dwb_pipe++;

			if (dwb_pipe >= MAX_DWB_PIPES)
				return;
		}
	}
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};

bool dcn30_acquire_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		int mpcc_id,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper)
{
	int i;
	bool ret = false;
	union dc_3dlut_state *state;

	ASSERT(*lut == NULL && *shaper == NULL);
	*lut = NULL;
	*shaper = NULL;

	for (i = 0; i < pool->res_cap->num_mpc_3dlut; i++) {
		if (!res_ctx->is_mpc_3dlut_acquired[i]) {
			*lut = pool->mpc_lut[i];
			*shaper = pool->mpc_shaper[i];
			state = &pool->mpc_lut[i]->state;
			res_ctx->is_mpc_3dlut_acquired[i] = true;
			state->bits.rmu_idx_valid = 1;
			state->bits.rmu_mux_num = i;
			if (state->bits.rmu_mux_num == 0)
				state->bits.mpc_rmu0_mux = mpcc_id;
			else if (state->bits.rmu_mux_num == 1)
				state->bits.mpc_rmu1_mux = mpcc_id;
			else if (state->bits.rmu_mux_num == 2)
				state->bits.mpc_rmu2_mux = mpcc_id;
			ret = true;
			break;
		}
	}
	return ret;
}

bool dcn30_release_post_bldn_3dlut(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_3dlut **lut,
		struct dc_transfer_func **shaper)
{
	int i;
	bool ret = false;

	for (i = 0; i < pool->res_cap->num_mpc_3dlut; i++) {
		if (pool->mpc_lut[i] == *lut && pool->mpc_shaper[i] == *shaper) {
			res_ctx->is_mpc_3dlut_acquired[i] = false;
			pool->mpc_lut[i]->state.raw = 0;
			*lut = NULL;
			*shaper = NULL;
			ret = true;
			break;
		}
	}
	return ret;
}

static bool is_soc_bounding_box_valid(struct dc *dc)
{
	uint32_t hw_internal_rev = dc->ctx->asic_id.hw_internal_rev;

	if (ASICREV_IS_SIENNA_CICHLID_P(hw_internal_rev))
		return true;

	return false;
}

static bool init_soc_bounding_box(struct dc *dc,
				  struct dcn30_resource_pool *pool)
{
	struct _vcs_dpi_soc_bounding_box_st *loaded_bb = &dcn3_0_soc;
	struct _vcs_dpi_ip_params_st *loaded_ip = &dcn3_0_ip;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (!is_soc_bounding_box_valid(dc)) {
		DC_LOG_ERROR("%s: not valid soc bounding box\n", __func__);
		return false;
	}

	loaded_ip->max_num_otg = pool->base.res_cap->num_timing_generator;
	loaded_ip->max_num_dpp = pool->base.pipe_count;
	loaded_ip->clamp_min_dcfclk = dc->config.clamp_min_dcfclk;
	dcn20_patch_bounding_box(dc, loaded_bb);
	DC_FP_START();
	patch_dcn30_soc_bounding_box(dc, &dcn3_0_soc);
	DC_FP_END();

	return true;
}

static bool dcn30_split_stream_for_mpc_or_odm(
		const struct dc *dc,
		struct resource_context *res_ctx,
		struct pipe_ctx *pri_pipe,
		struct pipe_ctx *sec_pipe,
		bool odm)
{
	int pipe_idx = sec_pipe->pipe_idx;
	const struct resource_pool *pool = dc->res_pool;

	*sec_pipe = *pri_pipe;

	sec_pipe->pipe_idx = pipe_idx;
	sec_pipe->plane_res.mi = pool->mis[pipe_idx];
	sec_pipe->plane_res.hubp = pool->hubps[pipe_idx];
	sec_pipe->plane_res.ipp = pool->ipps[pipe_idx];
	sec_pipe->plane_res.xfm = pool->transforms[pipe_idx];
	sec_pipe->plane_res.dpp = pool->dpps[pipe_idx];
	sec_pipe->plane_res.mpcc_inst = pool->dpps[pipe_idx]->inst;
	sec_pipe->stream_res.dsc = NULL;
	if (odm) {
		if (pri_pipe->next_odm_pipe) {
			ASSERT(pri_pipe->next_odm_pipe != sec_pipe);
			sec_pipe->next_odm_pipe = pri_pipe->next_odm_pipe;
			sec_pipe->next_odm_pipe->prev_odm_pipe = sec_pipe;
		}
		if (pri_pipe->top_pipe && pri_pipe->top_pipe->next_odm_pipe) {
			pri_pipe->top_pipe->next_odm_pipe->bottom_pipe = sec_pipe;
			sec_pipe->top_pipe = pri_pipe->top_pipe->next_odm_pipe;
		}
		if (pri_pipe->bottom_pipe && pri_pipe->bottom_pipe->next_odm_pipe) {
			pri_pipe->bottom_pipe->next_odm_pipe->top_pipe = sec_pipe;
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe->next_odm_pipe;
		}
		pri_pipe->next_odm_pipe = sec_pipe;
		sec_pipe->prev_odm_pipe = pri_pipe;

		if (!sec_pipe->top_pipe)
			sec_pipe->stream_res.opp = pool->opps[pipe_idx];
		else
			sec_pipe->stream_res.opp = sec_pipe->top_pipe->stream_res.opp;
		if (sec_pipe->stream->timing.flags.DSC == 1) {
			dcn20_acquire_dsc(dc, res_ctx, &sec_pipe->stream_res.dsc, pipe_idx);
			ASSERT(sec_pipe->stream_res.dsc);
			if (sec_pipe->stream_res.dsc == NULL)
				return false;
		}
	} else {
		if (pri_pipe->bottom_pipe) {
			ASSERT(pri_pipe->bottom_pipe != sec_pipe);
			sec_pipe->bottom_pipe = pri_pipe->bottom_pipe;
			sec_pipe->bottom_pipe->top_pipe = sec_pipe;
		}
		pri_pipe->bottom_pipe = sec_pipe;
		sec_pipe->top_pipe = pri_pipe;

		ASSERT(pri_pipe->plane_state);
	}

	return true;
}

static struct pipe_ctx *dcn30_find_split_pipe(
		struct dc *dc,
		struct dc_state *context,
		int old_index)
{
	struct pipe_ctx *pipe = NULL;
	int i;

	if (old_index >= 0 && context->res_ctx.pipe_ctx[old_index].stream == NULL) {
		pipe = &context->res_ctx.pipe_ctx[old_index];
		pipe->pipe_idx = old_index;
	}

	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (dc->current_state->res_ctx.pipe_ctx[i].top_pipe == NULL
					&& dc->current_state->res_ctx.pipe_ctx[i].prev_odm_pipe == NULL) {
				if (context->res_ctx.pipe_ctx[i].stream == NULL) {
					pipe = &context->res_ctx.pipe_ctx[i];
					pipe->pipe_idx = i;
					break;
				}
			}
		}

	/*
	 * May need to fix pipes getting tossed from 1 opp to another on flip
	 * Add for debugging transient underflow during topology updates:
	 * ASSERT(pipe);
	 */
	if (!pipe)
		for (i = dc->res_pool->pipe_count - 1; i >= 0; i--) {
			if (context->res_ctx.pipe_ctx[i].stream == NULL) {
				pipe = &context->res_ctx.pipe_ctx[i];
				pipe->pipe_idx = i;
				break;
			}
		}

	return pipe;
}

noinline bool dcn30_internal_validate_bw(
		struct dc *dc,
		struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int *pipe_cnt_out,
		int *vlevel_out,
		bool fast_validate,
		bool allow_self_refresh_only)
{
	bool out = false;
	bool repopulate_pipes = false;
	int split[MAX_PIPES] = { 0 };
	bool merge[MAX_PIPES] = { false };
	bool newly_split[MAX_PIPES] = { false };
	int pipe_cnt, i, pipe_idx, vlevel = 0;
	struct vba_vars_st *vba = &context->bw_ctx.dml.vba;

	ASSERT(pipes);
	if (!pipes)
		return false;

	context->bw_ctx.dml.vba.maxMpcComb = 0;
	context->bw_ctx.dml.vba.VoltageLevel = 0;
	context->bw_ctx.dml.vba.DRAMClockChangeSupport[0][0] = dm_dram_clock_change_vactive;
	dc->res_pool->funcs->update_soc_for_wm_a(dc, context);
	pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, fast_validate);

	if (!pipe_cnt) {
		out = true;
		goto validate_out;
	}

	dml_log_pipe_params(&context->bw_ctx.dml, pipes, pipe_cnt);

	if (!fast_validate || !allow_self_refresh_only) {
		/*
		 * DML favors voltage over p-state, but we're more interested in
		 * supporting p-state over voltage. We can't support p-state in
		 * prefetch mode > 0 so try capping the prefetch mode to start.
		 */
		context->bw_ctx.dml.soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank =
			dm_allow_self_refresh_and_mclk_switch;
		vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);
		/* This may adjust vlevel and maxMpcComb */
		if (vlevel < context->bw_ctx.dml.soc.num_states)
			vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, vlevel, split, merge);
	}
	if (allow_self_refresh_only &&
	    (fast_validate || vlevel == context->bw_ctx.dml.soc.num_states ||
			vba->DRAMClockChangeSupport[vlevel][vba->maxMpcComb] == dm_dram_clock_change_unsupported)) {
		/*
		 * If mode is unsupported or there's still no p-state support
		 * then fall back to favoring voltage.
		 *
		 * We don't actually support prefetch mode 2, so require that we
		 * at least support prefetch mode 1.
		 */
		context->bw_ctx.dml.validate_max_state = fast_validate;
		context->bw_ctx.dml.soc.allow_dram_self_refresh_or_dram_clock_change_in_vblank =
			dm_allow_self_refresh;

		vlevel = dml_get_voltage_level(&context->bw_ctx.dml, pipes, pipe_cnt);
		if (vlevel < context->bw_ctx.dml.soc.num_states) {
			memset(split, 0, sizeof(split));
			memset(merge, 0, sizeof(merge));
			vlevel = dcn20_validate_apply_pipe_split_flags(dc, context, vlevel, split, merge);
		}
		context->bw_ctx.dml.validate_max_state = false;
	}

	dml_log_mode_support_params(&context->bw_ctx.dml);

	if (vlevel == context->bw_ctx.dml.soc.num_states)
		goto validate_fail;

	if (!dc->config.enable_windowed_mpo_odm) {
		for (i = 0, pipe_idx = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
			struct pipe_ctx *mpo_pipe = pipe->bottom_pipe;

			if (!pipe->stream)
				continue;

			/* We only support full screen mpo with ODM */
			if (vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled
					&& pipe->plane_state && mpo_pipe
					&& memcmp(&mpo_pipe->plane_state->clip_rect,
							&pipe->stream->src,
							sizeof(struct rect)) != 0) {
				ASSERT(mpo_pipe->plane_state != pipe->plane_state);
				goto validate_fail;
			}
			pipe_idx++;
		}
	}

	/* merge pipes if necessary */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		/*skip pipes that don't need merging*/
		if (!merge[i])
			continue;

		/* if ODM merge we ignore mpc tree, mpo pipes will have their own flags */
		if (pipe->prev_odm_pipe) {
			/*split off odm pipe*/
			pipe->prev_odm_pipe->next_odm_pipe = pipe->next_odm_pipe;
			if (pipe->next_odm_pipe)
				pipe->next_odm_pipe->prev_odm_pipe = pipe->prev_odm_pipe;

			pipe->bottom_pipe = NULL;
			pipe->next_odm_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			pipe->top_pipe = NULL;
			pipe->prev_odm_pipe = NULL;
			if (pipe->stream_res.dsc)
				dcn20_release_dsc(&context->res_ctx, dc->res_pool, &pipe->stream_res.dsc);
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
			repopulate_pipes = true;
		} else if (pipe->top_pipe && pipe->top_pipe->plane_state == pipe->plane_state) {
			struct pipe_ctx *top_pipe = pipe->top_pipe;
			struct pipe_ctx *bottom_pipe = pipe->bottom_pipe;

			top_pipe->bottom_pipe = bottom_pipe;
			if (bottom_pipe)
				bottom_pipe->top_pipe = top_pipe;

			pipe->top_pipe = NULL;
			pipe->bottom_pipe = NULL;
			pipe->plane_state = NULL;
			pipe->stream = NULL;
			memset(&pipe->plane_res, 0, sizeof(pipe->plane_res));
			memset(&pipe->stream_res, 0, sizeof(pipe->stream_res));
			repopulate_pipes = true;
		} else
			ASSERT(0); /* Should never try to merge master pipe */

	}

	for (i = 0, pipe_idx = -1; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *hsplit_pipe = NULL;
		bool odm;
		int old_index = -1;

		if (!pipe->stream || newly_split[i])
			continue;

		pipe_idx++;
		odm = vba->ODMCombineEnabled[vba->pipe_plane[pipe_idx]] != dm_odm_combine_mode_disabled;

		if (!pipe->plane_state && !odm)
			continue;

		if (split[i]) {
			if (odm) {
				if (split[i] == 4 && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
				else if (old_pipe->next_odm_pipe)
					old_index = old_pipe->next_odm_pipe->pipe_idx;
			} else {
				if (split[i] == 4 && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->bottom_pipe->pipe_idx;
				else if (old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
					old_index = old_pipe->bottom_pipe->pipe_idx;
			}
			hsplit_pipe = dcn30_find_split_pipe(dc, context, old_index);
			ASSERT(hsplit_pipe);
			if (!hsplit_pipe)
				goto validate_fail;

			if (!dcn30_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, hsplit_pipe, odm))
				goto validate_fail;

			newly_split[hsplit_pipe->pipe_idx] = true;
			repopulate_pipes = true;
		}
		if (split[i] == 4) {
			struct pipe_ctx *pipe_4to1;

			if (odm && old_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe &&
						old_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dcn30_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dcn30_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;

			if (odm && old_pipe->next_odm_pipe && old_pipe->next_odm_pipe->next_odm_pipe
					&& old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe)
				old_index = old_pipe->next_odm_pipe->next_odm_pipe->next_odm_pipe->pipe_idx;
			else if (!odm && old_pipe->bottom_pipe && old_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe &&
					old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->plane_state == old_pipe->plane_state)
				old_index = old_pipe->bottom_pipe->bottom_pipe->bottom_pipe->pipe_idx;
			else
				old_index = -1;
			pipe_4to1 = dcn30_find_split_pipe(dc, context, old_index);
			ASSERT(pipe_4to1);
			if (!pipe_4to1)
				goto validate_fail;
			if (!dcn30_split_stream_for_mpc_or_odm(
					dc, &context->res_ctx,
					hsplit_pipe, pipe_4to1, odm))
				goto validate_fail;
			newly_split[pipe_4to1->pipe_idx] = true;
		}
		if (odm)
			dcn20_build_mapped_resource(dc, context, pipe->stream);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state) {
			if (!resource_build_scaling_params(pipe))
				goto validate_fail;
		}
	}

	/* Actual dsc count per stream dsc validation*/
	if (!dcn20_validate_dsc(dc, context)) {
		vba->ValidationStatus[vba->soc.num_states] = DML_FAIL_DSC_VALIDATION_FAILURE;
		goto validate_fail;
	}

	if (repopulate_pipes)
		pipe_cnt = dc->res_pool->funcs->populate_dml_pipes(dc, context, pipes, fast_validate);
	context->bw_ctx.dml.vba.VoltageLevel = vlevel;
	*vlevel_out = vlevel;
	*pipe_cnt_out = pipe_cnt;

	out = true;
	goto validate_out;

validate_fail:
	out = false;

validate_out:
	return out;
}

static int get_refresh_rate(struct dc_state *context)
{
	int refresh_rate = 0;
	int h_v_total = 0;
	struct dc_crtc_timing *timing = NULL;

	if (context == NULL || context->streams[0] == NULL)
		return 0;

	/* check if refresh rate at least 120hz */
	timing = &context->streams[0]->timing;
	if (timing == NULL)
		return 0;

	h_v_total = timing->h_total * timing->v_total;
	if (h_v_total == 0)
		return 0;

	refresh_rate = ((timing->pix_clk_100hz * 100) / (h_v_total)) + 1;
	return refresh_rate;
}

#define MAX_STRETCHED_V_BLANK 500 // in micro-seconds
/*
 * Scaling factor for v_blank stretch calculations considering timing in
 * micro-seconds and pixel clock in 100hz.
 * Note: the parenthesis are necessary to ensure the correct order of
 * operation where V_SCALE is used.
 */
#define V_SCALE (10000 / MAX_STRETCHED_V_BLANK)

static int get_frame_rate_at_max_stretch_100hz(struct dc_state *context)
{
	struct dc_crtc_timing *timing = NULL;
	uint32_t sec_per_100_lines;
	uint32_t max_v_blank;
	uint32_t curr_v_blank;
	uint32_t v_stretch_max;
	uint32_t stretched_frame_pix_cnt;
	uint32_t scaled_stretched_frame_pix_cnt;
	uint32_t scaled_refresh_rate;

	if (context == NULL || context->streams[0] == NULL)
		return 0;

	/* check if refresh rate at least 120hz */
	timing = &context->streams[0]->timing;
	if (timing == NULL)
		return 0;

	sec_per_100_lines = timing->pix_clk_100hz / timing->h_total + 1;
	max_v_blank = sec_per_100_lines / V_SCALE + 1;
	curr_v_blank = timing->v_total - timing->v_addressable;
	v_stretch_max = (max_v_blank > curr_v_blank) ? (max_v_blank - curr_v_blank) : (0);
	stretched_frame_pix_cnt = (v_stretch_max + timing->v_total) * timing->h_total;
	scaled_stretched_frame_pix_cnt = stretched_frame_pix_cnt / 10000;
	scaled_refresh_rate = (timing->pix_clk_100hz) / scaled_stretched_frame_pix_cnt + 1;

	return scaled_refresh_rate;
}

static bool is_refresh_rate_support_mclk_switch_using_fw_based_vblank_stretch(struct dc_state *context)
{
	int refresh_rate_max_stretch_100hz;
	int min_refresh_100hz;

	if (context == NULL || context->streams[0] == NULL)
		return false;

	refresh_rate_max_stretch_100hz = get_frame_rate_at_max_stretch_100hz(context);
	min_refresh_100hz = context->streams[0]->timing.min_refresh_in_uhz / 10000;

	if (refresh_rate_max_stretch_100hz < min_refresh_100hz)
		return false;

	return true;
}

bool dcn30_can_support_mclk_switch_using_fw_based_vblank_stretch(struct dc *dc, struct dc_state *context)
{
	int refresh_rate = 0;
	const int minimum_refreshrate_supported = 120;
	struct dc_stream_status *stream_status = NULL;

	if (context == NULL || context->streams[0] == NULL)
		return false;

	if (context->streams[0]->sink->edid_caps.panel_patch.disable_fams)
		return false;

	if (dc->debug.disable_fams)
		return false;

	if (!dc->caps.dmub_caps.mclk_sw)
		return false;

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching_shut_down)
		return false;

	/* more then 1 monitor connected */
	if (context->stream_count != 1)
		return false;

	refresh_rate = get_refresh_rate(context);
	if (refresh_rate < minimum_refreshrate_supported)
		return false;

	if (!is_refresh_rate_support_mclk_switch_using_fw_based_vblank_stretch(context))
		return false;

	if (!context->streams[0]->allow_freesync)
		return false;

	if (context->streams[0]->vrr_active_variable && (dc->debug.disable_fams_gaming == INGAME_FAMS_DISABLE))
		return false;

	stream_status = dc_state_get_stream_status(context, context->streams[0]);

	if (!stream_status)
		return false;

	stream_status->fpo_in_use = true;

	return true;
}

/*
 * set up FPO watermarks, pstate, dram latency
 */
void dcn30_setup_mclk_switch_using_fw_based_vblank_stretch(struct dc *dc, struct dc_state *context)
{
	ASSERT(dc != NULL && context != NULL);
	if (dc == NULL || context == NULL)
		return;

	/* Set wm_a.pstate so high natural MCLK switches are impossible: 4 seconds */
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = 4U * 1000U * 1000U * 1000U;
}

void dcn30_update_soc_for_wm_a(struct dc *dc, struct dc_state *context)
{
	DC_FP_START();
	dcn30_fpu_update_soc_for_wm_a(dc, context);
	DC_FP_END();
}

void dcn30_calculate_wm_and_dlg(
		struct dc *dc, struct dc_state *context,
		display_e2e_pipe_params_st *pipes,
		int pipe_cnt,
		int vlevel)
{
	DC_FP_START();
	dcn30_fpu_calculate_wm_and_dlg(dc, context, pipes, pipe_cnt, vlevel);
	DC_FP_END();
}

bool dcn30_validate_bandwidth(struct dc *dc,
		struct dc_state *context,
		bool fast_validate)
{
	bool out = false;

	BW_VAL_TRACE_SETUP();

	int vlevel = 0;
	int pipe_cnt = 0;
	display_e2e_pipe_params_st *pipes = kzalloc(dc->res_pool->pipe_count * sizeof(display_e2e_pipe_params_st), GFP_KERNEL);
	DC_LOGGER_INIT(dc->ctx->logger);

	BW_VAL_TRACE_COUNT();

	if (!pipes)
		goto validate_fail;

	DC_FP_START();
	out = dcn30_internal_validate_bw(dc, context, pipes, &pipe_cnt, &vlevel, fast_validate, true);
	DC_FP_END();

	if (pipe_cnt == 0)
		goto validate_out;

	if (!out)
		goto validate_fail;

	BW_VAL_TRACE_END_VOLTAGE_LEVEL();

	if (fast_validate) {
		BW_VAL_TRACE_SKIP(fast);
		goto validate_out;
	}

	DC_FP_START();
	if (dc->res_pool->funcs->calculate_wm_and_dlg)
		dc->res_pool->funcs->calculate_wm_and_dlg(dc, context, pipes, pipe_cnt, vlevel);
	DC_FP_END();

	BW_VAL_TRACE_END_WATERMARKS();

	goto validate_out;

validate_fail:
	DC_LOG_WARNING("Mode Validation Warning: %s failed validation.\n",
		dml_get_status_message(context->bw_ctx.dml.vba.ValidationStatus[context->bw_ctx.dml.vba.soc.num_states]));

	BW_VAL_TRACE_SKIP(fail);
	out = false;

validate_out:
	kfree(pipes);

	BW_VAL_TRACE_FINISH();

	return out;
}

void dcn30_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	unsigned int i, j;
	unsigned int num_states = 0;

	unsigned int dcfclk_mhz[DC__VOLTAGE_STATES] = {0};
	unsigned int dram_speed_mts[DC__VOLTAGE_STATES] = {0};
	unsigned int optimal_uclk_for_dcfclk_sta_targets[DC__VOLTAGE_STATES] = {0};
	unsigned int optimal_dcfclk_for_uclk[DC__VOLTAGE_STATES] = {0};

	unsigned int dcfclk_sta_targets[DC__VOLTAGE_STATES] = {694, 875, 1000, 1200};
	unsigned int num_dcfclk_sta_targets = 4;
	unsigned int num_uclk_states;

	struct dc_bounding_box_max_clk dcn30_bb_max_clk;

	memset(&dcn30_bb_max_clk, 0, sizeof(dcn30_bb_max_clk));

	if (dc->ctx->dc_bios->vram_info.num_chans)
		dcn3_0_soc.num_chans = dc->ctx->dc_bios->vram_info.num_chans;

	DC_FP_START();
	dcn30_fpu_update_dram_channel_width_bytes(dc);
	DC_FP_END();

	if (bw_params->clk_table.entries[0].memclk_mhz) {

		for (i = 0; i < MAX_NUM_DPM_LVL; i++) {
			if (bw_params->clk_table.entries[i].dcfclk_mhz > dcn30_bb_max_clk.max_dcfclk_mhz)
				dcn30_bb_max_clk.max_dcfclk_mhz = bw_params->clk_table.entries[i].dcfclk_mhz;
			if (bw_params->clk_table.entries[i].dispclk_mhz > dcn30_bb_max_clk.max_dispclk_mhz)
				dcn30_bb_max_clk.max_dispclk_mhz = bw_params->clk_table.entries[i].dispclk_mhz;
			if (bw_params->clk_table.entries[i].dppclk_mhz > dcn30_bb_max_clk.max_dppclk_mhz)
				dcn30_bb_max_clk.max_dppclk_mhz = bw_params->clk_table.entries[i].dppclk_mhz;
			if (bw_params->clk_table.entries[i].phyclk_mhz > dcn30_bb_max_clk.max_phyclk_mhz)
				dcn30_bb_max_clk.max_phyclk_mhz = bw_params->clk_table.entries[i].phyclk_mhz;
		}

		DC_FP_START();
		dcn30_fpu_update_max_clk(&dcn30_bb_max_clk);
		DC_FP_END();

		if (dcn30_bb_max_clk.max_dcfclk_mhz > dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			// If max DCFCLK is greater than the max DCFCLK STA target, insert into the DCFCLK STA target array
			dcfclk_sta_targets[num_dcfclk_sta_targets] = dcn30_bb_max_clk.max_dcfclk_mhz;
			num_dcfclk_sta_targets++;
		} else if (dcn30_bb_max_clk.max_dcfclk_mhz < dcfclk_sta_targets[num_dcfclk_sta_targets-1]) {
			// If max DCFCLK is less than the max DCFCLK STA target, cap values and remove duplicates
			for (i = 0; i < num_dcfclk_sta_targets; i++) {
				if (dcfclk_sta_targets[i] > dcn30_bb_max_clk.max_dcfclk_mhz) {
					dcfclk_sta_targets[i] = dcn30_bb_max_clk.max_dcfclk_mhz;
					break;
				}
			}
			// Update size of array since we "removed" duplicates
			num_dcfclk_sta_targets = i + 1;
		}

		num_uclk_states = bw_params->clk_table.num_entries;

		// Calculate optimal dcfclk for each uclk
		for (i = 0; i < num_uclk_states; i++) {
			DC_FP_START();
			dcn30_fpu_get_optimal_dcfclk_fclk_for_uclk(bw_params->clk_table.entries[i].memclk_mhz * 16,
					&optimal_dcfclk_for_uclk[i], NULL);
			DC_FP_END();
			if (optimal_dcfclk_for_uclk[i] < bw_params->clk_table.entries[0].dcfclk_mhz) {
				optimal_dcfclk_for_uclk[i] = bw_params->clk_table.entries[0].dcfclk_mhz;
			}
		}

		// Calculate optimal uclk for each dcfclk sta target
		for (i = 0; i < num_dcfclk_sta_targets; i++) {
			for (j = 0; j < num_uclk_states; j++) {
				if (dcfclk_sta_targets[i] < optimal_dcfclk_for_uclk[j]) {
					optimal_uclk_for_dcfclk_sta_targets[i] =
							bw_params->clk_table.entries[j].memclk_mhz * 16;
					break;
				} else {
					/* condition where (dcfclk_sta_targets[i] >= optimal_dcfclk_for_uclk[j]):
					 * If it just so happens that the memory bandwidth is low enough such that
					 * all the optimal DCFCLK for each UCLK is lower than the smallest DCFCLK STA
					 * target, we need to populate the optimal UCLK for each DCFCLK STA target to
					 * be the max UCLK.
					 */
					if (j == num_uclk_states - 1) {
						optimal_uclk_for_dcfclk_sta_targets[i] =
								bw_params->clk_table.entries[j].memclk_mhz * 16;
					}
				}
			}
		}

		i = 0;
		j = 0;
		// create the final dcfclk and uclk table
		while (i < num_dcfclk_sta_targets && j < num_uclk_states && num_states < DC__VOLTAGE_STATES) {
			if (dcfclk_sta_targets[i] < optimal_dcfclk_for_uclk[j] && i < num_dcfclk_sta_targets) {
				dcfclk_mhz[num_states] = dcfclk_sta_targets[i];
				dram_speed_mts[num_states++] = optimal_uclk_for_dcfclk_sta_targets[i++];
			} else {
				if (j < num_uclk_states && optimal_dcfclk_for_uclk[j] <= dcn30_bb_max_clk.max_dcfclk_mhz) {
					dcfclk_mhz[num_states] = optimal_dcfclk_for_uclk[j];
					dram_speed_mts[num_states++] = bw_params->clk_table.entries[j++].memclk_mhz * 16;
				} else {
					j = num_uclk_states;
				}
			}
		}

		while (i < num_dcfclk_sta_targets && num_states < DC__VOLTAGE_STATES) {
			dcfclk_mhz[num_states] = dcfclk_sta_targets[i];
			dram_speed_mts[num_states++] = optimal_uclk_for_dcfclk_sta_targets[i++];
		}

		while (j < num_uclk_states && num_states < DC__VOLTAGE_STATES &&
				optimal_dcfclk_for_uclk[j] <= dcn30_bb_max_clk.max_dcfclk_mhz) {
			dcfclk_mhz[num_states] = optimal_dcfclk_for_uclk[j];
			dram_speed_mts[num_states++] = bw_params->clk_table.entries[j++].memclk_mhz * 16;
		}

		dcn3_0_soc.num_states = num_states;
		DC_FP_START();
		dcn30_fpu_update_bw_bounding_box(dc, bw_params, &dcn30_bb_max_clk, dcfclk_mhz, dram_speed_mts);
		DC_FP_END();
	}
}

static void dcn30_get_panel_config_defaults(struct dc_panel_config *panel_config)
{
	*panel_config = panel_config_defaults;
}

static const struct resource_funcs dcn30_res_pool_funcs = {
	.destroy = dcn30_destroy_resource_pool,
	.link_enc_create = dcn30_link_encoder_create,
	.panel_cntl_create = dcn30_panel_cntl_create,
	.validate_bandwidth = dcn30_validate_bandwidth,
	.calculate_wm_and_dlg = dcn30_calculate_wm_and_dlg,
	.update_soc_for_wm_a = dcn30_update_soc_for_wm_a,
	.populate_dml_pipes = dcn30_populate_dml_pipes_from_context,
	.acquire_free_pipe_as_secondary_dpp_pipe = dcn20_acquire_free_pipe_for_layer,
	.release_pipe = dcn20_release_pipe,
	.add_stream_to_ctx = dcn30_add_stream_to_ctx,
	.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.populate_dml_writeback_from_context = dcn30_populate_dml_writeback_from_context,
	.set_mcif_arb_params = dcn30_set_mcif_arb_params,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
	.acquire_post_bldn_3dlut = dcn30_acquire_post_bldn_3dlut,
	.release_post_bldn_3dlut = dcn30_release_post_bldn_3dlut,
	.update_bw_bounding_box = dcn30_update_bw_bounding_box,
	.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
	.get_panel_config_defaults = dcn30_get_panel_config_defaults,
};

#define CTX ctx

#define REG(reg_name) \
	(DCN_BASE.instance[0].segment[mm ## reg_name ## _BASE_IDX] + mm ## reg_name)

static uint32_t read_pipe_fuses(struct dc_context *ctx)
{
	uint32_t value = REG_READ(CC_DC_PIPE_DIS);
	/* Support for max 6 pipes */
	value = value & 0x3f;
	return value;
}

static bool dcn30_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn30_resource_pool *pool)
{
	int i;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;
	struct ddc_service_init_data ddc_init_data = {0};
	uint32_t pipe_fuses = read_pipe_fuses(ctx);
	uint32_t num_pipes = 0;

	if (!(pipe_fuses == 0 || pipe_fuses == 0x3e)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: Unexpected fuse recipe for navi2x !\n");
		/* fault to single pipe */
		pipe_fuses = 0x3e;
	}

	DC_FP_START();

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap_dcn3;

	pool->base.funcs = &dcn30_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->base.pipe_count = pool->base.res_cap->num_timing_generator;
	pool->base.mpcc_count = pool->base.res_cap->num_timing_generator;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 100; /*1.4 w/a not applied by default*/
	dc->caps.max_cursor_size = 256;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.mall_size_per_mem_channel = 8;
	/* total size = mall per channel * num channels * 1024 * 1024 */
	dc->caps.mall_size_total = dc->caps.mall_size_per_mem_channel * dc->ctx->dc_bios->vram_info.num_chans * 1048576;
	dc->caps.cursor_cache_size = dc->caps.max_cursor_size * dc->caps.max_cursor_size * 8;

	dc->caps.max_slave_planes = 2;
	dc->caps.max_slave_yuv_planes = 2;
	dc->caps.max_slave_rgb_planes = 2;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;

	/* Color pipeline capabilities */
	dc->caps.color.dpp.dcn_arch = 1;
	dc->caps.color.dpp.input_lut_shared = 0;
	dc->caps.color.dpp.icsc = 1;
	dc->caps.color.dpp.dgam_ram = 0; // must use gamma_corr
	dc->caps.color.dpp.dgam_rom_caps.srgb = 1;
	dc->caps.color.dpp.dgam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.dgam_rom_caps.gamma2_2 = 1;
	dc->caps.color.dpp.dgam_rom_caps.pq = 1;
	dc->caps.color.dpp.dgam_rom_caps.hlg = 1;
	dc->caps.color.dpp.post_csc = 1;
	dc->caps.color.dpp.gamma_corr = 1;
	dc->caps.color.dpp.dgam_rom_for_yuv = 0;

	dc->caps.color.dpp.hw_3d_lut = 1;
	dc->caps.color.dpp.ogam_ram = 1;
	// no OGAM ROM on DCN3
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 1;
	dc->caps.color.mpc.num_3dluts = pool->base.res_cap->num_mpc_3dlut; //3
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;

	dc->caps.dp_hdmi21_pcon_support = true;
	dc->caps.max_v_total = (1 << 15) - 1;
	dc->caps.vtotal_limited_by_fp2 = true;

	/* read VBIOS LTTPR caps */
	{
		if (ctx->dc_bios->funcs->get_lttpr_caps) {
			enum bp_result bp_query_result;
			uint8_t is_vbios_lttpr_enable = 0;

			bp_query_result = ctx->dc_bios->funcs->get_lttpr_caps(ctx->dc_bios, &is_vbios_lttpr_enable);
			dc->caps.vbios_lttpr_enable = (bp_query_result == BP_RESULT_OK) && !!is_vbios_lttpr_enable;
		}

		if (ctx->dc_bios->funcs->get_lttpr_interop) {
			enum bp_result bp_query_result;
			uint8_t is_vbios_interop_enabled = 0;

			bp_query_result = ctx->dc_bios->funcs->get_lttpr_interop(ctx->dc_bios,
					&is_vbios_interop_enabled);
			dc->caps.vbios_lttpr_aware = (bp_query_result == BP_RESULT_OK) && !!is_vbios_interop_enabled;
		}
	}

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;

	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	/* Clock Sources for Pixel Clock*/
	pool->base.clock_sources[DCN30_CLK_SRC_PLL0] =
			dcn30_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN30_CLK_SRC_PLL1] =
			dcn30_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN30_CLK_SRC_PLL2] =
			dcn30_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN30_CLK_SRC_PLL3] =
			dcn30_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCN30_CLK_SRC_PLL4] =
			dcn30_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);
	pool->base.clock_sources[DCN30_CLK_SRC_PLL5] =
			dcn30_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL5,
				&clk_src_regs[5], false);

	pool->base.clk_src_count = DCN30_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn30_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* DCCG */
	pool->base.dccg = dccg30_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* PP Lib and SMU interfaces */
	init_soc_bounding_box(dc, pool);

	num_pipes = dcn3_0_ip.max_num_dpp;

	for (i = 0; i < dcn3_0_ip.max_num_dpp; i++)
		if (pipe_fuses & 1 << i)
			num_pipes--;

	dcn3_0_ip.max_num_dpp = num_pipes;
	dcn3_0_ip.max_num_otg = num_pipes;

	dml_init_instance(&dc->dml, &dcn3_0_soc, &dcn3_0_ip, DML_PROJECT_DCN30);

	/* IRQ */
	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn30_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	/* HUBBUB */
	pool->base.hubbub = dcn30_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	/* HUBPs, DPPs, OPPs and TGs */
	for (i = 0; i < pool->base.pipe_count; i++) {
		pool->base.hubps[i] = dcn30_hubp_create(ctx, i);
		if (pool->base.hubps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create hubps!\n");
			goto create_fail;
		}

		pool->base.dpps[i] = dcn30_dpp_create(ctx, i);
		if (pool->base.dpps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		pool->base.opps[i] = dcn30_opp_create(ctx, i);
		if (pool->base.opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		pool->base.timing_generators[i] = dcn30_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}
	}
	pool->base.timing_generator_count = i;
	/* PSR */
	pool->base.psr = dmub_psr_create(ctx);

	if (pool->base.psr == NULL) {
		dm_error("DC: failed to create PSR obj!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* ABM */
	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		pool->base.multiple_abms[i] = dmub_abm_create(ctx,
				&abm_regs[i],
				&abm_shift,
				&abm_mask);
		if (pool->base.multiple_abms[i] == NULL) {
			dm_error("DC: failed to create abm for pipe %d!\n", i);
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}
	/* MPC and DSC */
	pool->base.mpc = dcn30_mpc_create(ctx, pool->base.mpcc_count, pool->base.res_cap->num_mpc_3dlut);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn30_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* DWB and MMHUBBUB */
	if (!dcn30_dwbc_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}

	if (!dcn30_mmhubbub_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	/* AUX and I2C */
	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn30_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn30_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	/* Audio, Stream Encoders including HPO and virtual, MPC 3D LUTs */
	if (!resource_construct(num_virtual_links, dc, &pool->base,
			&res_create_funcs))
		goto create_fail;

	/* HW Sequencer and Plane caps */
	dcn30_hw_sequencer_construct(dc);

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	if (dc->ctx->dc_bios->fw_info.oem_i2c_present) {
		ddc_init_data.ctx = dc->ctx;
		ddc_init_data.link = NULL;
		ddc_init_data.id.id = dc->ctx->dc_bios->fw_info.oem_i2c_obj_id;
		ddc_init_data.id.enum_id = 0;
		ddc_init_data.id.type = OBJECT_TYPE_GENERIC;
		pool->base.oem_device = dc->link_srv->create_ddc_service(&ddc_init_data);
	} else {
		pool->base.oem_device = NULL;
	}

	DC_FP_END();

	return true;

create_fail:

	DC_FP_END();
	dcn30_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn30_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn30_resource_pool *pool =
		kzalloc(sizeof(struct dcn30_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn30_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
