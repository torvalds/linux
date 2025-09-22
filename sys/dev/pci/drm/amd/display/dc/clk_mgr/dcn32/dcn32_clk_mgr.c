/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "dccg.h"
#include "clk_mgr_internal.h"
#include "dcn32/dcn32_clk_mgr_smu_msg.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dce100/dce_clk_mgr.h"
#include "dcn31/dcn31_clk_mgr.h"
#include "dcn32/dcn32_clk_mgr.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dm_helpers.h"
#include "link.h"
#include "dc_state_priv.h"
#include "atomfirmware.h"
#include "dcn32_smu13_driver_if.h"

#include "dcn/dcn_3_2_0_offset.h"
#include "dcn/dcn_3_2_0_sh_mask.h"

#include "dml/dcn32/dcn32_fpu.h"

#define DCN_BASE__INST0_SEG1                       0x000000C0

#define mmCLK1_CLK_PLL_REQ                              0x16E37
#define mmCLK1_CLK0_DFS_CNTL                            0x16E69
#define mmCLK1_CLK1_DFS_CNTL                            0x16E6C
#define mmCLK1_CLK2_DFS_CNTL                            0x16E6F
#define mmCLK1_CLK3_DFS_CNTL                            0x16E72
#define mmCLK1_CLK4_DFS_CNTL                            0x16E75

#define mmCLK1_CLK0_CURRENT_CNT                         0x16EE7
#define mmCLK1_CLK1_CURRENT_CNT                         0x16EE8
#define mmCLK1_CLK2_CURRENT_CNT                         0x16EE9
#define mmCLK1_CLK3_CURRENT_CNT                         0x16EEA
#define mmCLK1_CLK4_CURRENT_CNT                         0x16EEB

#define mmCLK4_CLK0_CURRENT_CNT                         0x1B0C9

#define CLK1_CLK_PLL_REQ__FbMult_int_MASK               0x000001ffUL
#define CLK1_CLK_PLL_REQ__PllSpineDiv_MASK              0x0000f000UL
#define CLK1_CLK_PLL_REQ__FbMult_frac_MASK              0xffff0000UL
#define CLK1_CLK_PLL_REQ__FbMult_int__SHIFT             0x00000000
#define CLK1_CLK_PLL_REQ__PllSpineDiv__SHIFT            0x0000000c
#define CLK1_CLK_PLL_REQ__FbMult_frac__SHIFT            0x00000010

#define mmCLK01_CLK0_CLK_PLL_REQ                        0x16E37
#define mmCLK01_CLK0_CLK0_DFS_CNTL                      0x16E64
#define mmCLK01_CLK0_CLK1_DFS_CNTL                      0x16E67
#define mmCLK01_CLK0_CLK2_DFS_CNTL                      0x16E6A
#define mmCLK01_CLK0_CLK3_DFS_CNTL                      0x16E6D
#define mmCLK01_CLK0_CLK4_DFS_CNTL                      0x16E70

#define CLK0_CLK_PLL_REQ__FbMult_int_MASK               0x000001ffL
#define CLK0_CLK_PLL_REQ__PllSpineDiv_MASK              0x0000f000L
#define CLK0_CLK_PLL_REQ__FbMult_frac_MASK              0xffff0000L
#define CLK0_CLK_PLL_REQ__FbMult_int__SHIFT             0x00000000
#define CLK0_CLK_PLL_REQ__PllSpineDiv__SHIFT            0x0000000c
#define CLK0_CLK_PLL_REQ__FbMult_frac__SHIFT            0x00000010

#undef FN
#define FN(reg_name, field_name) \
	clk_mgr->clk_mgr_shift->field_name, clk_mgr->clk_mgr_mask->field_name

#define REG(reg) \
	(clk_mgr->regs->reg)

#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
					reg ## reg_name

#define CLK_SR_DCN32(reg_name)\
	.reg_name = mm ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn32 = {
	CLK_REG_LIST_DCN32()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn32 = {
	CLK_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn32 = {
	CLK_COMMON_MASK_SH_LIST_DCN32(_MASK)
};


#define CLK_SR_DCN321(reg_name, block, inst)\
	.reg_name = mm ## block ## _ ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn321 = {
	CLK_REG_LIST_DCN321()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn321 = {
	CLK_COMMON_MASK_SH_LIST_DCN321(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn321 = {
	CLK_COMMON_MASK_SH_LIST_DCN321(_MASK)
};


/* Query SMU for all clock states for a particular clock */
static void dcn32_init_single_clock(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, unsigned int *entry_0,
		unsigned int *num_levels)
{
	unsigned int i;
	char *entry_i = (char *)entry_0;

	uint32_t ret = dcn30_smu_get_dpm_freq_by_index(clk_mgr, clk, 0xFF);

	if (ret & (1 << 31))
		/* fine-grained, only min and max */
		*num_levels = 2;
	else
		/* discrete, a number of fixed states */
		/* will set num_levels to 0 on failure */
		*num_levels = ret & 0xFF;

	/* if the initial message failed, num_levels will be 0 */
	for (i = 0; i < *num_levels; i++) {
		*((unsigned int *)entry_i) = (dcn30_smu_get_dpm_freq_by_index(clk_mgr, clk, i) & 0xFFFF);
		entry_i += sizeof(clk_mgr->base.bw_params->clk_table.entries[0]);
	}
}

static void dcn32_build_wm_range_table(struct clk_mgr_internal *clk_mgr)
{
	DC_FP_START();
	dcn32_build_wm_range_table_fpu(clk_mgr);
	DC_FP_END();
}

void dcn32_init_clocks(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	unsigned int num_levels;
	struct clk_limit_num_entries *num_entries_per_clk;
	unsigned int i;

	if (!clk_mgr_base->bw_params)
		return;

	num_entries_per_clk = &clk_mgr_base->bw_params->clk_table.num_entries_per_clk;

	memset(&(clk_mgr_base->clks), 0, sizeof(struct dc_clocks));
	clk_mgr_base->clks.p_state_change_support = true;
	clk_mgr_base->clks.prev_p_state_change_support = true;
	clk_mgr_base->clks.fclk_prev_p_state_change_support = true;
	clk_mgr->smu_present = false;
	clk_mgr->dpm_present = false;

	if (!clk_mgr_base->force_smu_not_present && dcn30_smu_get_smu_version(clk_mgr, &clk_mgr->smu_ver))
		clk_mgr->smu_present = true;

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_check_driver_if_version(clk_mgr);
	dcn30_smu_check_msg_header_version(clk_mgr);

	/* DCFCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_DCFCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dcfclk_mhz,
			&num_entries_per_clk->num_dcfclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.dcfclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_DCFCLK);

	/* SOCCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_SOCCLK,
					&clk_mgr_base->bw_params->clk_table.entries[0].socclk_mhz,
					&num_entries_per_clk->num_socclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.socclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_SOCCLK);

	/* DTBCLK */
	if (!clk_mgr->base.ctx->dc->debug.disable_dtb_ref_clk_switch) {
		dcn32_init_single_clock(clk_mgr, PPCLK_DTBCLK,
				&clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz,
				&num_entries_per_clk->num_dtbclk_levels);
		clk_mgr_base->bw_params->dc_mode_limit.dtbclk_mhz =
				dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_DTBCLK);
	}

	/* DISPCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_DISPCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dispclk_mhz,
			&num_entries_per_clk->num_dispclk_levels);
	num_levels = num_entries_per_clk->num_dispclk_levels;
	clk_mgr_base->bw_params->dc_mode_limit.dispclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_DISPCLK);
	//HW recommends limit of 1950 MHz in display clock for all DCN3.2.x
	if (clk_mgr_base->bw_params->dc_mode_limit.dispclk_mhz > 1950)
		clk_mgr_base->bw_params->dc_mode_limit.dispclk_mhz = 1950;

	/* DPPCLK */
	dcn32_init_single_clock(clk_mgr, PPCLK_DPPCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dppclk_mhz,
			&num_entries_per_clk->num_dppclk_levels);
	num_levels = num_entries_per_clk->num_dppclk_levels;
	clk_mgr_base->bw_params->dc_mode_limit.dppclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_DPPCLK);
	//HW recommends limit of 1950 MHz in display clock for all DCN3.2.x
	if (clk_mgr_base->bw_params->dc_mode_limit.dppclk_mhz > 1950)
		clk_mgr_base->bw_params->dc_mode_limit.dppclk_mhz = 1950;

	if (num_entries_per_clk->num_dcfclk_levels &&
			num_entries_per_clk->num_dtbclk_levels &&
			num_entries_per_clk->num_dispclk_levels)
		clk_mgr->dpm_present = true;

	if (clk_mgr_base->ctx->dc->debug.min_disp_clk_khz) {
		for (i = 0; i < num_levels; i++)
			if (clk_mgr_base->bw_params->clk_table.entries[i].dispclk_mhz
					< khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_disp_clk_khz))
				clk_mgr_base->bw_params->clk_table.entries[i].dispclk_mhz
					= khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_disp_clk_khz);
	}
	for (i = 0; i < num_levels; i++)
		if (clk_mgr_base->bw_params->clk_table.entries[i].dispclk_mhz > 1950)
			clk_mgr_base->bw_params->clk_table.entries[i].dispclk_mhz = 1950;

	if (clk_mgr_base->ctx->dc->debug.min_dpp_clk_khz) {
		for (i = 0; i < num_levels; i++)
			if (clk_mgr_base->bw_params->clk_table.entries[i].dppclk_mhz
					< khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_dpp_clk_khz))
				clk_mgr_base->bw_params->clk_table.entries[i].dppclk_mhz
					= khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_dpp_clk_khz);
	}

	for (i = 0; i < num_levels; i++)
		if (clk_mgr_base->bw_params->clk_table.entries[i].dppclk_mhz > 1950)
			clk_mgr_base->bw_params->clk_table.entries[i].dppclk_mhz = 1950;

	/* Get UCLK, update bounding box */
	clk_mgr_base->funcs->get_memclk_states_from_smu(clk_mgr_base);

	/* WM range table */
	dcn32_build_wm_range_table(clk_mgr);
}

static void dcn32_update_clocks_update_dtb_dto(struct clk_mgr_internal *clk_mgr,
			struct dc_state *context,
			int ref_dtbclk_khz)
{
	struct dccg *dccg = clk_mgr->dccg;
	uint32_t tg_mask = 0;
	int i;

	for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct dtbclk_dto_params dto_params = {0};

		/* use mask to program DTO once per tg */
		if (pipe_ctx->stream_res.tg &&
				!(tg_mask & (1 << pipe_ctx->stream_res.tg->inst))) {
			tg_mask |= (1 << pipe_ctx->stream_res.tg->inst);

			dto_params.otg_inst = pipe_ctx->stream_res.tg->inst;
			dto_params.ref_dtbclk_khz = ref_dtbclk_khz;

			dccg->funcs->set_dtbclk_dto(clk_mgr->dccg, &dto_params);
			//dccg->funcs->set_audio_dtbclk_dto(clk_mgr->dccg, &dto_params);
		}
	}
}

/* Since DPPCLK request to PMFW needs to be exact (due to DPP DTO programming),
 * update DPPCLK to be the exact frequency that will be set after the DPPCLK
 * divider is updated. This will prevent rounding issues that could cause DPP
 * refclk and DPP DTO to not match up.
 */
static void dcn32_update_dppclk_dispclk_freq(struct clk_mgr_internal *clk_mgr, struct dc_clocks *new_clocks)
{
	int dpp_divider = 0;
	int disp_divider = 0;

	if (new_clocks->dppclk_khz) {
		dpp_divider = DENTIST_DIVIDER_RANGE_SCALE_FACTOR
				* clk_mgr->base.dentist_vco_freq_khz / new_clocks->dppclk_khz;
		new_clocks->dppclk_khz = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR * clk_mgr->base.dentist_vco_freq_khz) / dpp_divider;
	}
	if (new_clocks->dispclk_khz > 0) {
		disp_divider = DENTIST_DIVIDER_RANGE_SCALE_FACTOR
				* clk_mgr->base.dentist_vco_freq_khz / new_clocks->dispclk_khz;
		new_clocks->dispclk_khz = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR * clk_mgr->base.dentist_vco_freq_khz) / disp_divider;
	}
}

void dcn32_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr,
		struct dc_state *context, bool safe_to_lower)
{
	int i;

	clk_mgr->dccg->ref_dppclk = clk_mgr->base.clks.dppclk_khz;
	for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
		int dpp_inst = 0, dppclk_khz, prev_dppclk_khz;

		dppclk_khz = context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz;

		if (context->res_ctx.pipe_ctx[i].plane_res.dpp)
			dpp_inst = context->res_ctx.pipe_ctx[i].plane_res.dpp->inst;
		else if (!context->res_ctx.pipe_ctx[i].plane_res.dpp && dppclk_khz == 0) {
			/* dpp == NULL && dppclk_khz == 0 is valid because of pipe harvesting.
			 * In this case just continue in loop
			 */
			continue;
		} else if (!context->res_ctx.pipe_ctx[i].plane_res.dpp && dppclk_khz > 0) {
			/* The software state is not valid if dpp resource is NULL and
			 * dppclk_khz > 0.
			 */
			ASSERT(false);
			continue;
		}

		prev_dppclk_khz = clk_mgr->dccg->pipe_dppclk_khz[i];

		if (safe_to_lower || prev_dppclk_khz < dppclk_khz)
			clk_mgr->dccg->funcs->update_dpp_dto(
							clk_mgr->dccg, dpp_inst, dppclk_khz);
	}
}

static void dcn32_update_clocks_update_dentist(
		struct clk_mgr_internal *clk_mgr,
		struct dc_state *context)
{
	uint32_t new_disp_divider = 0;
	uint32_t new_dispclk_wdivider = 0;
	uint32_t old_dispclk_wdivider = 0;
	uint32_t i;
	uint32_t dentist_dispclk_wdivider_readback = 0;
	struct dc *dc = clk_mgr->base.ctx->dc;

	if (clk_mgr->base.clks.dispclk_khz == 0)
		return;

	new_disp_divider = DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz / clk_mgr->base.clks.dispclk_khz;

	new_dispclk_wdivider = dentist_get_did_from_divider(new_disp_divider);
	REG_GET(DENTIST_DISPCLK_CNTL,
			DENTIST_DISPCLK_WDIVIDER, &old_dispclk_wdivider);

	/* When changing divider to or from 127, some extra programming is required to prevent corruption */
	if (old_dispclk_wdivider == 127 && new_dispclk_wdivider != 127) {
		for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
			uint32_t fifo_level;
			struct dccg *dccg = clk_mgr->base.ctx->dc->res_pool->dccg;
			struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;
			int32_t N;
			int32_t j;

			if (!resource_is_pipe_type(pipe_ctx, OTG_MASTER))
				continue;
			/* Virtual encoders don't have this function */
			if (!stream_enc->funcs->get_fifo_cal_average_level)
				continue;
			fifo_level = stream_enc->funcs->get_fifo_cal_average_level(
					stream_enc);
			N = fifo_level / 4;
			dccg->funcs->set_fifo_errdet_ovr_en(
					dccg,
					true);
			for (j = 0; j < N - 4; j++)
				dccg->funcs->otg_drop_pixel(
						dccg,
						pipe_ctx->stream_res.tg->inst);
			dccg->funcs->set_fifo_errdet_ovr_en(
					dccg,
					false);
		}
	} else if (new_dispclk_wdivider == 127 && old_dispclk_wdivider != 127) {
		/* request clock with 126 divider first */
		uint32_t temp_disp_divider = dentist_get_divider_from_did(126);
		uint32_t temp_dispclk_khz = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR * clk_mgr->base.dentist_vco_freq_khz) / temp_disp_divider;

		if (clk_mgr->smu_present)
			/*
			 * SMU uses discrete dispclk presets. We applied
			 * the same formula to increase our dppclk_khz
			 * to the next matching discrete value. By
			 * contract, we should use the preset dispclk
			 * floored in Mhz to describe the intended clock.
			 */
			dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DISPCLK,
					khz_to_mhz_floor(temp_dispclk_khz));

		if (dc->debug.override_dispclk_programming) {
			REG_GET(DENTIST_DISPCLK_CNTL,
					DENTIST_DISPCLK_WDIVIDER, &dentist_dispclk_wdivider_readback);

			if (dentist_dispclk_wdivider_readback != 126) {
				REG_UPDATE(DENTIST_DISPCLK_CNTL,
						DENTIST_DISPCLK_WDIVIDER, 126);
				REG_WAIT(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, 1, 50, 2000);
			}
		}

		for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
			struct dccg *dccg = clk_mgr->base.ctx->dc->res_pool->dccg;
			struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;
			uint32_t fifo_level;
			int32_t N;
			int32_t j;

			if (!resource_is_pipe_type(pipe_ctx, OTG_MASTER))
				continue;
			/* Virtual encoders don't have this function */
			if (!stream_enc->funcs->get_fifo_cal_average_level)
				continue;
			fifo_level = stream_enc->funcs->get_fifo_cal_average_level(
					stream_enc);
			N = fifo_level / 4;
			dccg->funcs->set_fifo_errdet_ovr_en(dccg, true);
			for (j = 0; j < 12 - N; j++)
				dccg->funcs->otg_add_pixel(dccg,
						pipe_ctx->stream_res.tg->inst);
			dccg->funcs->set_fifo_errdet_ovr_en(dccg, false);
		}
	}

	/* do requested DISPCLK updates*/
	if (clk_mgr->smu_present)
		/*
		 * SMU uses discrete dispclk presets. We applied
		 * the same formula to increase our dppclk_khz
		 * to the next matching discrete value. By
		 * contract, we should use the preset dispclk
		 * floored in Mhz to describe the intended clock.
		 */
		dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DISPCLK,
				khz_to_mhz_floor(clk_mgr->base.clks.dispclk_khz));

	if (dc->debug.override_dispclk_programming) {
		REG_GET(DENTIST_DISPCLK_CNTL,
				DENTIST_DISPCLK_WDIVIDER, &dentist_dispclk_wdivider_readback);

		if (dentist_dispclk_wdivider_readback > new_dispclk_wdivider) {
			REG_UPDATE(DENTIST_DISPCLK_CNTL,
					DENTIST_DISPCLK_WDIVIDER, new_dispclk_wdivider);
			REG_WAIT(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, 1, 50, 2000);
		}
	}

}

static int dcn32_get_dispclk_from_dentist(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t dispclk_wdivider;
	int disp_divider;

	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, &dispclk_wdivider);
	disp_divider = dentist_get_divider_from_did(dispclk_wdivider);

	/* Return DISPCLK freq in Khz */
	if (disp_divider)
		return (DENTIST_DIVIDER_RANGE_SCALE_FACTOR * clk_mgr->base.dentist_vco_freq_khz) / disp_divider;

	return 0;
}

static bool dcn32_check_native_scaling(struct pipe_ctx *pipe)
{
	bool is_native_scaling = false;
	int width = pipe->plane_state->src_rect.width;
	int height = pipe->plane_state->src_rect.height;

	if (pipe->stream->timing.h_addressable == width &&
			pipe->stream->timing.v_addressable == height &&
			pipe->plane_state->dst_rect.width == width &&
			pipe->plane_state->dst_rect.height == height)
		is_native_scaling = true;

	return is_native_scaling;
}

static void dcn32_auto_dpm_test_log(
		struct dc_clocks *new_clocks,
		struct clk_mgr_internal *clk_mgr,
		struct dc_state *context)
{
	unsigned int dispclk_khz_reg, dppclk_khz_reg, dprefclk_khz_reg, dcfclk_khz_reg, dtbclk_khz_reg,
				 fclk_khz_reg, mall_ss_size_bytes;
	int dramclk_khz_override, fclk_khz_override, num_fclk_levels;

	struct pipe_ctx *pipe_ctx_list[MAX_PIPES];
	int active_pipe_count = 0;

	for (int i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream && dc_state_get_pipe_subvp_type(context, pipe_ctx) != SUBVP_PHANTOM) {
			pipe_ctx_list[active_pipe_count] = pipe_ctx;
			active_pipe_count++;
		}
	}

	drm_msleep(5);

	mall_ss_size_bytes = context->bw_ctx.bw.dcn.mall_ss_size_bytes;

    dispclk_khz_reg    = REG_READ(CLK1_CLK0_CURRENT_CNT); // DISPCLK
    dppclk_khz_reg     = REG_READ(CLK1_CLK1_CURRENT_CNT); // DPPCLK
    dprefclk_khz_reg   = REG_READ(CLK1_CLK2_CURRENT_CNT); // DPREFCLK
    dcfclk_khz_reg     = REG_READ(CLK1_CLK3_CURRENT_CNT); // DCFCLK
    dtbclk_khz_reg     = REG_READ(CLK1_CLK4_CURRENT_CNT); // DTBCLK
    fclk_khz_reg       = REG_READ(CLK4_CLK0_CURRENT_CNT); // FCLK

    // Overrides for these clocks in case there is no p_state change support
    dramclk_khz_override = new_clocks->dramclk_khz;
    fclk_khz_override = new_clocks->fclk_khz;

    num_fclk_levels = clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_fclk_levels - 1;

    if (!new_clocks->p_state_change_support) {
	    dramclk_khz_override = clk_mgr->base.bw_params->max_memclk_mhz * 1000;
    }
    if (!new_clocks->fclk_p_state_change_support) {
	    fclk_khz_override = clk_mgr->base.bw_params->clk_table.entries[num_fclk_levels].fclk_mhz * 1000;
    }

	////////////////////////////////////////////////////////////////////////////
	//	IMPORTANT: 	When adding more clocks to these logs, do NOT put a newline
	//	 			anywhere other than at the very end of the string.
	//
	//	Formatting example (make sure to have " - " between each entry):
	//
	//				AutoDPMTest: clk1:%d - clk2:%d - clk3:%d - clk4:%d\n"
	////////////////////////////////////////////////////////////////////////////
	if (active_pipe_count > 0 &&
		new_clocks->dramclk_khz > 0 &&
		new_clocks->fclk_khz > 0 &&
		new_clocks->dcfclk_khz > 0 &&
		new_clocks->dppclk_khz > 0) {

		uint32_t pix_clk_list[MAX_PIPES] = {0};
		int p_state_list[MAX_PIPES] = {0};
		int disp_src_width_list[MAX_PIPES] = {0};
		int disp_src_height_list[MAX_PIPES] = {0};
		uint64_t disp_src_refresh_list[MAX_PIPES] = {0};
		bool is_scaled_list[MAX_PIPES] = {0};

		for (int i = 0; i < active_pipe_count; i++) {
			struct pipe_ctx *curr_pipe_ctx = pipe_ctx_list[i];
			uint64_t refresh_rate;

			pix_clk_list[i] = curr_pipe_ctx->stream->timing.pix_clk_100hz;
			p_state_list[i] = curr_pipe_ctx->p_state_type;

			refresh_rate = (curr_pipe_ctx->stream->timing.pix_clk_100hz * (uint64_t)100 +
				curr_pipe_ctx->stream->timing.v_total * (uint64_t)curr_pipe_ctx->stream->timing.h_total - (uint64_t)1);
			refresh_rate = div_u64(refresh_rate, curr_pipe_ctx->stream->timing.v_total);
			refresh_rate = div_u64(refresh_rate, curr_pipe_ctx->stream->timing.h_total);
			disp_src_refresh_list[i] = refresh_rate;

			if (curr_pipe_ctx->plane_state) {
				is_scaled_list[i] = !(dcn32_check_native_scaling(curr_pipe_ctx));
				disp_src_width_list[i] = curr_pipe_ctx->plane_state->src_rect.width;
				disp_src_height_list[i] = curr_pipe_ctx->plane_state->src_rect.height;
			}
		}

		DC_LOG_AUTO_DPM_TEST("AutoDPMTest: dramclk:%d - fclk:%d - "
			"dcfclk:%d - dppclk:%d - dispclk_hw:%d - "
			"dppclk_hw:%d - dprefclk_hw:%d - dcfclk_hw:%d - "
			"dtbclk_hw:%d - fclk_hw:%d - pix_clk_0:%d - pix_clk_1:%d - "
			"pix_clk_2:%d - pix_clk_3:%d - mall_ss_size:%d - p_state_type_0:%d - "
			"p_state_type_1:%d - p_state_type_2:%d - p_state_type_3:%d - "
			"pix_width_0:%d - pix_height_0:%d - refresh_rate_0:%lld - is_scaled_0:%d - "
			"pix_width_1:%d - pix_height_1:%d - refresh_rate_1:%lld - is_scaled_1:%d - "
			"pix_width_2:%d - pix_height_2:%d - refresh_rate_2:%lld - is_scaled_2:%d - "
			"pix_width_3:%d - pix_height_3:%d - refresh_rate_3:%lld - is_scaled_3:%d - LOG_END\n",
			dramclk_khz_override,
			fclk_khz_override,
			new_clocks->dcfclk_khz,
			new_clocks->dppclk_khz,
			dispclk_khz_reg,
			dppclk_khz_reg,
			dprefclk_khz_reg,
			dcfclk_khz_reg,
			dtbclk_khz_reg,
			fclk_khz_reg,
			pix_clk_list[0], pix_clk_list[1], pix_clk_list[3], pix_clk_list[2],
			mall_ss_size_bytes,
			p_state_list[0], p_state_list[1], p_state_list[2], p_state_list[3],
			disp_src_width_list[0], disp_src_height_list[0], disp_src_refresh_list[0], is_scaled_list[0],
			disp_src_width_list[1], disp_src_height_list[1], disp_src_refresh_list[1], is_scaled_list[1],
			disp_src_width_list[2], disp_src_height_list[2], disp_src_refresh_list[2], is_scaled_list[2],
			disp_src_width_list[3], disp_src_height_list[3], disp_src_refresh_list[3], is_scaled_list[3]);
	}
}

static void dcn32_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct dc *dc = clk_mgr_base->ctx->dc;
	int display_count;
	bool update_dppclk = false;
	bool update_dispclk = false;
	bool enter_display_off = false;
	bool dpp_clock_lowered = false;
	struct dmcu *dmcu = clk_mgr_base->ctx->dc->res_pool->dmcu;
	bool force_reset = false;
	bool update_uclk = false, update_fclk = false;
	bool p_state_change_support;
	bool fclk_p_state_change_support;

	if (clk_mgr_base->clks.dispclk_khz == 0 ||
			(dc->debug.force_clock_mode & 0x1)) {
		/* This is from resume or boot up, if forced_clock cfg option used,
		 * we bypass program dispclk and DPPCLK, but need set them for S3.
		 */
		force_reset = true;

		dcn2_read_clocks_from_hw_dentist(clk_mgr_base);

		/* Force_clock_mode 0x1:  force reset the clock even it is the same clock
		 * as long as it is in Passive level.
		 */
	}
	display_count = clk_mgr_helper_get_active_display_cnt(dc, context);

	if (display_count == 0)
		enter_display_off = true;

	if (clk_mgr->smu_present) {
		if (enter_display_off == safe_to_lower)
			dcn30_smu_set_num_of_displays(clk_mgr, display_count);

		clk_mgr_base->clks.fclk_prev_p_state_change_support = clk_mgr_base->clks.fclk_p_state_change_support;

		fclk_p_state_change_support = new_clocks->fclk_p_state_change_support;

		if (should_update_pstate_support(safe_to_lower, fclk_p_state_change_support, clk_mgr_base->clks.fclk_p_state_change_support) &&
				!dc->work_arounds.clock_update_disable_mask.fclk) {
			clk_mgr_base->clks.fclk_p_state_change_support = fclk_p_state_change_support;

			/* To enable FCLK P-state switching, send FCLK_PSTATE_SUPPORTED message to PMFW */
			if (clk_mgr_base->ctx->dce_version != DCN_VERSION_3_21 && clk_mgr_base->clks.fclk_p_state_change_support) {
				/* Handle the code for sending a message to PMFW that FCLK P-state change is supported */
				dcn32_smu_send_fclk_pstate_message(clk_mgr, FCLK_PSTATE_SUPPORTED);
			}
		}

		if (dc->debug.force_min_dcfclk_mhz > 0)
			new_clocks->dcfclk_khz = (new_clocks->dcfclk_khz > (dc->debug.force_min_dcfclk_mhz * 1000)) ?
					new_clocks->dcfclk_khz : (dc->debug.force_min_dcfclk_mhz * 1000);

		if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz) &&
				!dc->work_arounds.clock_update_disable_mask.dcfclk) {
			clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
			dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DCFCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_khz));
		}

		if (should_set_clock(safe_to_lower, new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz) &&
				!dc->work_arounds.clock_update_disable_mask.dcfclk_ds) {
			clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
			dcn30_smu_set_min_deep_sleep_dcef_clk(clk_mgr, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_deep_sleep_khz));
		}

		if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr_base->clks.socclk_khz))
			/* We don't actually care about socclk, don't notify SMU of hard min */
			clk_mgr_base->clks.socclk_khz = new_clocks->socclk_khz;

		clk_mgr_base->clks.prev_p_state_change_support = clk_mgr_base->clks.p_state_change_support;
		clk_mgr_base->clks.prev_num_ways = clk_mgr_base->clks.num_ways;

		if (clk_mgr_base->clks.num_ways != new_clocks->num_ways &&
				clk_mgr_base->clks.num_ways < new_clocks->num_ways) {
			clk_mgr_base->clks.num_ways = new_clocks->num_ways;
			dcn32_smu_send_cab_for_uclk_message(clk_mgr, clk_mgr_base->clks.num_ways);
		}

		p_state_change_support = new_clocks->p_state_change_support;
		if (should_update_pstate_support(safe_to_lower, p_state_change_support, clk_mgr_base->clks.p_state_change_support) &&
				!dc->work_arounds.clock_update_disable_mask.uclk) {
			clk_mgr_base->clks.p_state_change_support = p_state_change_support;

			/* to disable P-State switching, set UCLK min = max */
			if (!clk_mgr_base->clks.p_state_change_support) {
				if (dc->clk_mgr->dc_mode_softmax_enabled) {
					/* On DCN32x we will never have the functional UCLK min above the softmax
					 * since we calculate mode support based on softmax being the max UCLK
					 * frequency.
					 */
					if (dc->debug.disable_dc_mode_overwrite) {
						dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK, dc->clk_mgr->bw_params->max_memclk_mhz);
						dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK, dc->clk_mgr->bw_params->max_memclk_mhz);
					} else
						dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
								dc->clk_mgr->bw_params->dc_mode_softmax_memclk);
				} else {
					dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK, dc->clk_mgr->bw_params->max_memclk_mhz);
				}
			}
		}

		if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching)
			dcn32_smu_wait_for_dmub_ack_mclk(clk_mgr, true);
		else
			dcn32_smu_wait_for_dmub_ack_mclk(clk_mgr, false);

		/* Always update saved value, even if new value not set due to P-State switching unsupported. Also check safe_to_lower for FCLK */
		if (safe_to_lower && (clk_mgr_base->clks.fclk_p_state_change_support != clk_mgr_base->clks.fclk_prev_p_state_change_support)) {
			update_fclk = true;
		}

		if (clk_mgr_base->ctx->dce_version != DCN_VERSION_3_21 && !clk_mgr_base->clks.fclk_p_state_change_support && update_fclk &&
				!dc->work_arounds.clock_update_disable_mask.fclk) {
			/* Handle code for sending a message to PMFW that FCLK P-state change is not supported */
			dcn32_smu_send_fclk_pstate_message(clk_mgr, FCLK_PSTATE_NOTSUPPORTED);
		}

		/* Always update saved value, even if new value not set due to P-State switching unsupported */
		if (should_set_clock(safe_to_lower, new_clocks->dramclk_khz, clk_mgr_base->clks.dramclk_khz) &&
				!dc->work_arounds.clock_update_disable_mask.uclk) {
			clk_mgr_base->clks.dramclk_khz = new_clocks->dramclk_khz;
			update_uclk = true;
		}

		/* set UCLK to requested value if P-State switching is supported, or to re-enable P-State switching */
		if (clk_mgr_base->clks.p_state_change_support &&
				(update_uclk || !clk_mgr_base->clks.prev_p_state_change_support) &&
				!dc->work_arounds.clock_update_disable_mask.uclk) {
			if (dc->clk_mgr->dc_mode_softmax_enabled && dc->debug.disable_dc_mode_overwrite)
				dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK,
						max((int)dc->clk_mgr->bw_params->dc_mode_softmax_memclk, khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz)));

			dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz));
		}

		if (clk_mgr_base->clks.num_ways != new_clocks->num_ways &&
				clk_mgr_base->clks.num_ways > new_clocks->num_ways) {
			clk_mgr_base->clks.num_ways = new_clocks->num_ways;
			dcn32_smu_send_cab_for_uclk_message(clk_mgr, clk_mgr_base->clks.num_ways);
		}
	}

	dcn32_update_dppclk_dispclk_freq(clk_mgr, new_clocks);
	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr_base->clks.dppclk_khz)) {
		if (clk_mgr_base->clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;

		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;

		if (clk_mgr->smu_present && !dpp_clock_lowered)
			/*
			 * SMU uses discrete dppclk presets. We applied
			 * the same formula to increase our dppclk_khz
			 * to the next matching discrete value. By
			 * contract, we should use the preset dppclk
			 * floored in Mhz to describe the intended clock.
			 */
			dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DPPCLK,
					khz_to_mhz_floor(clk_mgr_base->clks.dppclk_khz));

		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;

		update_dispclk = true;
	}

	if (!new_clocks->dtbclk_en) {
		new_clocks->ref_dtbclk_khz = clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz * 1000;
	}

	/* clock limits are received with MHz precision, divide by 1000 to prevent setting clocks at every call */
	if (!dc->debug.disable_dtb_ref_clk_switch &&
			should_set_clock(safe_to_lower, new_clocks->ref_dtbclk_khz / 1000, clk_mgr_base->clks.ref_dtbclk_khz / 1000)) {
		/* DCCG requires KHz precision for DTBCLK */
		clk_mgr_base->clks.ref_dtbclk_khz =
				dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DTBCLK, khz_to_mhz_ceil(new_clocks->ref_dtbclk_khz));

		dcn32_update_clocks_update_dtb_dto(clk_mgr, context, clk_mgr_base->clks.ref_dtbclk_khz);
	}

	if (dc->config.forced_clocks == false || (force_reset && safe_to_lower)) {
		if (dpp_clock_lowered) {
			/* if clock is being lowered, increase DTO before lowering refclk */
			dcn32_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
			dcn32_update_clocks_update_dentist(clk_mgr, context);
			if (clk_mgr->smu_present)
				/*
				 * SMU uses discrete dppclk presets. We applied
				 * the same formula to increase our dppclk_khz
				 * to the next matching discrete value. By
				 * contract, we should use the preset dppclk
				 * floored in Mhz to describe the intended clock.
				 */
				dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DPPCLK,
						khz_to_mhz_floor(clk_mgr_base->clks.dppclk_khz));
		} else {
			/* if clock is being raised, increase refclk before lowering DTO */
			if (update_dppclk || update_dispclk)
				dcn32_update_clocks_update_dentist(clk_mgr, context);
			/* There is a check inside dcn20_update_clocks_update_dpp_dto which ensures
			 * that we do not lower dto when it is not safe to lower. We do not need to
			 * compare the current and new dppclk before calling this function.
			 */
			dcn32_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower);
		}
	}

	if (update_dispclk && dmcu && dmcu->funcs->is_dmcu_initialized(dmcu))
		/*update dmcu for wait_loop count*/
		dmcu->funcs->set_psr_wait_loop(dmcu,
				clk_mgr_base->clks.dispclk_khz / 1000 / 7);

	if (dc->config.enable_auto_dpm_test_logs) {
	    dcn32_auto_dpm_test_log(new_clocks, clk_mgr, context);
	}
}

static uint32_t dcn32_get_vco_frequency_from_reg(struct clk_mgr_internal *clk_mgr)
{
		struct fixed31_32 pll_req;
		uint32_t pll_req_reg = 0;

		/* get FbMult value */
		if (ASICREV_IS_GC_11_0_2(clk_mgr->base.ctx->asic_id.hw_internal_rev))
			pll_req_reg = REG_READ(CLK0_CLK_PLL_REQ);
		else
			pll_req_reg = REG_READ(CLK1_CLK_PLL_REQ);

		/* set up a fixed-point number
		 * this works because the int part is on the right edge of the register
		 * and the frac part is on the left edge
		 */
		pll_req = dc_fixpt_from_int(pll_req_reg & clk_mgr->clk_mgr_mask->FbMult_int);
		pll_req.value |= pll_req_reg & clk_mgr->clk_mgr_mask->FbMult_frac;

		/* multiply by REFCLK period */
		pll_req = dc_fixpt_mul_int(pll_req, clk_mgr->dfs_ref_freq_khz);

		return dc_fixpt_floor(pll_req);
}

static void dcn32_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t dprefclk_did = 0;
	uint32_t dcfclk_did = 0;
	uint32_t dtbclk_did = 0;
	uint32_t dispclk_did = 0;
	uint32_t dppclk_did = 0;
	uint32_t target_div = 0;

	if (ASICREV_IS_GC_11_0_2(clk_mgr->base.ctx->asic_id.hw_internal_rev)) {
		/* DFS Slice 0 is used for DISPCLK */
		dispclk_did = REG_READ(CLK0_CLK0_DFS_CNTL);
		/* DFS Slice 1 is used for DPPCLK */
		dppclk_did = REG_READ(CLK0_CLK1_DFS_CNTL);
		/* DFS Slice 2 is used for DPREFCLK */
		dprefclk_did = REG_READ(CLK0_CLK2_DFS_CNTL);
		/* DFS Slice 3 is used for DCFCLK */
		dcfclk_did = REG_READ(CLK0_CLK3_DFS_CNTL);
		/* DFS Slice 4 is used for DTBCLK */
		dtbclk_did = REG_READ(CLK0_CLK4_DFS_CNTL);
	} else {
		/* DFS Slice 0 is used for DISPCLK */
		dispclk_did = REG_READ(CLK1_CLK0_DFS_CNTL);
		/* DFS Slice 1 is used for DPPCLK */
		dppclk_did = REG_READ(CLK1_CLK1_DFS_CNTL);
		/* DFS Slice 2 is used for DPREFCLK */
		dprefclk_did = REG_READ(CLK1_CLK2_DFS_CNTL);
		/* DFS Slice 3 is used for DCFCLK */
		dcfclk_did = REG_READ(CLK1_CLK3_DFS_CNTL);
		/* DFS Slice 4 is used for DTBCLK */
		dtbclk_did = REG_READ(CLK1_CLK4_DFS_CNTL);
	}

	/* Convert DISPCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dispclk_did);
	//Get dispclk in khz
	regs_and_bypass->dispclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DISPCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dppclk_did);
	//Get dppclk in khz
	regs_and_bypass->dppclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DPREFCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dprefclk_did);
	//Get dprefclk in khz
	regs_and_bypass->dprefclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DCFCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dcfclk_did);
	//Get dcfclk in khz
	regs_and_bypass->dcfclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DTBCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dtbclk_did);
	//Get dtbclk in khz
	regs_and_bypass->dtbclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;
}

static void dcn32_clock_read_ss_info(struct clk_mgr_internal *clk_mgr)
{
	struct dc_bios *bp = clk_mgr->base.ctx->dc_bios;
	int ss_info_num = bp->funcs->get_ss_entry_number(
			bp, AS_SIGNAL_TYPE_GPU_PLL);

	if (ss_info_num) {
		struct spread_spectrum_info info = { { 0 } };
		enum bp_result result = bp->funcs->get_spread_spectrum_info(
				bp, AS_SIGNAL_TYPE_GPU_PLL, 0, &info);

		/* SSInfo.spreadSpectrumPercentage !=0 would be sign
		 * that SS is enabled
		 */
		if (result == BP_RESULT_OK &&
				info.spread_spectrum_percentage != 0) {
			clk_mgr->ss_on_dprefclk = true;
			clk_mgr->dprefclk_ss_divider = info.spread_percentage_divider;

			if (info.type.CENTER_MODE == 0) {
				/* Currently for DP Reference clock we
				 * need only SS percentage for
				 * downspread
				 */
				clk_mgr->dprefclk_ss_percentage =
						info.spread_spectrum_percentage;
			}
		}
	}
}
static void dcn32_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	unsigned int i;
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	WatermarksExternal_t *table = (WatermarksExternal_t *) clk_mgr->wm_range_table;

	if (!clk_mgr->smu_present)
		return;

	if (!table)
		return;

	memset(table, 0, sizeof(*table));

	/* collect valid ranges, place in pmfw table */
	for (i = 0; i < WM_SET_COUNT; i++)
		if (clk_mgr->base.bw_params->wm_table.nv_entries[i].valid) {
			table->Watermarks.WatermarkRow[i].WmSetting = i;
			table->Watermarks.WatermarkRow[i].Flags = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.wm_type;
		}
	dcn30_smu_set_dram_addr_high(clk_mgr, clk_mgr->wm_range_table_addr >> 32);
	dcn30_smu_set_dram_addr_low(clk_mgr, clk_mgr->wm_range_table_addr & 0xFFFFFFFF);
	dcn32_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

/* Set min memclk to minimum, either constrained by the current mode or DPM0 */
static void dcn32_set_hard_min_memclk(struct clk_mgr *clk_mgr_base, bool current_mode)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	if (current_mode) {
		if (clk_mgr_base->clks.p_state_change_support)
			dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz));
		else
			dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					clk_mgr_base->bw_params->max_memclk_mhz);
	} else {
		dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
				clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz);
	}
}

/* Set max memclk to highest DPM value */
static void dcn32_set_hard_max_memclk(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK, clk_mgr_base->bw_params->max_memclk_mhz);
}

/* Get current memclk states, update bounding box */
static void dcn32_get_memclk_states_from_smu(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_limit_num_entries *num_entries_per_clk = &clk_mgr_base->bw_params->clk_table.num_entries_per_clk;
	unsigned int num_levels;

	if (!clk_mgr->smu_present)
		return;

	/* Refresh memclk and fclk states */
	dcn32_init_single_clock(clk_mgr, PPCLK_UCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz,
			&num_entries_per_clk->num_memclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.memclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_UCLK);
	clk_mgr_base->bw_params->dc_mode_softmax_memclk = clk_mgr_base->bw_params->dc_mode_limit.memclk_mhz;

	/* memclk must have at least one level */
	num_entries_per_clk->num_memclk_levels = num_entries_per_clk->num_memclk_levels ? num_entries_per_clk->num_memclk_levels : 1;

	dcn32_init_single_clock(clk_mgr, PPCLK_FCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].fclk_mhz,
			&num_entries_per_clk->num_fclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.fclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_FCLK);

	if (num_entries_per_clk->num_memclk_levels >= num_entries_per_clk->num_fclk_levels) {
		num_levels = num_entries_per_clk->num_memclk_levels;
	} else {
		num_levels = num_entries_per_clk->num_fclk_levels;
	}
	clk_mgr_base->bw_params->max_memclk_mhz =
			clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_memclk_levels - 1].memclk_mhz;
	clk_mgr_base->bw_params->clk_table.num_entries = num_levels ? num_levels : 1;

	if (clk_mgr->dpm_present && !num_levels)
		clk_mgr->dpm_present = false;

	if (!clk_mgr->dpm_present)
		dcn32_patch_dpm_table(clk_mgr_base->bw_params);

	DC_FP_START();
	/* Refresh bounding box */
	clk_mgr_base->ctx->dc->res_pool->funcs->update_bw_bounding_box(
			clk_mgr->base.ctx->dc, clk_mgr_base->bw_params);
	DC_FP_END();
}

static bool dcn32_are_clock_states_equal(struct dc_clocks *a,
					struct dc_clocks *b)
{
	if (a->dispclk_khz != b->dispclk_khz)
		return false;
	else if (a->dppclk_khz != b->dppclk_khz)
		return false;
	else if (a->dcfclk_khz != b->dcfclk_khz)
		return false;
	else if (a->dcfclk_deep_sleep_khz != b->dcfclk_deep_sleep_khz)
		return false;
	else if (a->dramclk_khz != b->dramclk_khz)
		return false;
	else if (a->p_state_change_support != b->p_state_change_support)
		return false;
	else if (a->fclk_p_state_change_support != b->fclk_p_state_change_support)
		return false;

	return true;
}

static void dcn32_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn32_smu_set_pme_workaround(clk_mgr);
}

static bool dcn32_is_smu_present(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	return clk_mgr->smu_present;
}

static void dcn32_set_max_memclk(struct clk_mgr *clk_mgr_base, unsigned int memclk_mhz)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK, memclk_mhz);
}

static void dcn32_set_min_memclk(struct clk_mgr *clk_mgr_base, unsigned int memclk_mhz)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn32_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK, memclk_mhz);
}

static struct clk_mgr_funcs dcn32_funcs = {
		.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
		.get_dtb_ref_clk_frequency = dcn31_get_dtb_ref_freq_khz,
		.update_clocks = dcn32_update_clocks,
		.dump_clk_registers = dcn32_dump_clk_registers,
		.init_clocks = dcn32_init_clocks,
		.notify_wm_ranges = dcn32_notify_wm_ranges,
		.set_hard_min_memclk = dcn32_set_hard_min_memclk,
		.set_hard_max_memclk = dcn32_set_hard_max_memclk,
		.set_max_memclk = dcn32_set_max_memclk,
		.set_min_memclk = dcn32_set_min_memclk,
		.get_memclk_states_from_smu = dcn32_get_memclk_states_from_smu,
		.are_clock_states_equal = dcn32_are_clock_states_equal,
		.enable_pme_wa = dcn32_enable_pme_wa,
		.is_smu_present = dcn32_is_smu_present,
		.get_dispclk_from_dentist = dcn32_get_dispclk_from_dentist,
};

void dcn32_clk_mgr_construct(
		struct dc_context *ctx,
		struct clk_mgr_internal *clk_mgr,
		struct pp_smu_funcs *pp_smu,
		struct dccg *dccg)
{
	struct clk_log_info log_info = {0};

	clk_mgr->base.ctx = ctx;
	clk_mgr->base.funcs = &dcn32_funcs;
	if (ASICREV_IS_GC_11_0_2(clk_mgr->base.ctx->asic_id.hw_internal_rev)) {
		clk_mgr->regs = &clk_mgr_regs_dcn321;
		clk_mgr->clk_mgr_shift = &clk_mgr_shift_dcn321;
		clk_mgr->clk_mgr_mask = &clk_mgr_mask_dcn321;
	} else {
		clk_mgr->regs = &clk_mgr_regs_dcn32;
		clk_mgr->clk_mgr_shift = &clk_mgr_shift_dcn32;
		clk_mgr->clk_mgr_mask = &clk_mgr_mask_dcn32;
	}

	clk_mgr->dccg = dccg;
	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;
	clk_mgr->dfs_ref_freq_khz = 100000;

	/* Changed from DCN3.2_clock_frequency doc to match
	 * dcn32_dump_clk_registers from 4 * dentist_vco_freq_khz /
	 * dprefclk DID divider
	 */
	clk_mgr->base.dprefclk_khz = 716666;
	if (ctx->dc->debug.disable_dtb_ref_clk_switch) {
		//initialize DTB ref clock value if DPM disabled
		if (ctx->dce_version == DCN_VERSION_3_21)
			clk_mgr->base.clks.ref_dtbclk_khz = 477800;
		else
			clk_mgr->base.clks.ref_dtbclk_khz = 268750;
	}


	/* integer part is now VCO frequency in kHz */
	clk_mgr->base.dentist_vco_freq_khz = dcn32_get_vco_frequency_from_reg(clk_mgr);

	/* in case we don't get a value from the register, use default */
	if (clk_mgr->base.dentist_vco_freq_khz == 0)
		clk_mgr->base.dentist_vco_freq_khz = 4300000; /* Updated as per HW docs */

	dcn32_dump_clk_registers(&clk_mgr->base.boot_snapshot, &clk_mgr->base, &log_info);

	if (ctx->dc->debug.disable_dtb_ref_clk_switch &&
			clk_mgr->base.clks.ref_dtbclk_khz != clk_mgr->base.boot_snapshot.dtbclk) {
		clk_mgr->base.clks.ref_dtbclk_khz = clk_mgr->base.boot_snapshot.dtbclk;
	}

	if (clk_mgr->base.boot_snapshot.dprefclk != 0) {
		clk_mgr->base.dprefclk_khz = clk_mgr->base.boot_snapshot.dprefclk;
	}
	dcn32_clock_read_ss_info(clk_mgr);

	clk_mgr->dfs_bypass_enabled = false;

	clk_mgr->smu_present = false;

	clk_mgr->base.bw_params = kzalloc(sizeof(*clk_mgr->base.bw_params), GFP_KERNEL);
	if (!clk_mgr->base.bw_params) {
		BREAK_TO_DEBUGGER();
		return;
	}

	/* need physical address of table to give to PMFW */
	clk_mgr->wm_range_table = dm_helpers_allocate_gpu_mem(clk_mgr->base.ctx,
			DC_MEM_ALLOC_TYPE_GART, sizeof(WatermarksExternal_t),
			&clk_mgr->wm_range_table_addr);
	if (!clk_mgr->wm_range_table) {
		BREAK_TO_DEBUGGER();
		return;
	}
}

void dcn32_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr)
{
	kfree(clk_mgr->base.bw_params);

	if (clk_mgr->wm_range_table)
		dm_helpers_free_gpu_mem(clk_mgr->base.ctx, DC_MEM_ALLOC_TYPE_GART,
				clk_mgr->wm_range_table);
}

