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
 * Authors: AMD
 *
 */

#include "reg_helper.h"
#include "dc.h"
#include "dcn401_mpc.h"
#include "dcn10/dcn10_cm_common.h"
#include "basics/conversion.h"
#include "mpc.h"

#define REG(reg)\
	mpc401->mpc_regs->reg

#define CTX \
	mpc401->base.ctx

#undef FN
#define FN(reg_name, field_name) \
	mpc401->mpc_shift->field_name, mpc401->mpc_mask->field_name

static void mpc401_update_3dlut_fast_load_select(struct mpc *mpc, int mpcc_id, int hubp_idx)
{
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	REG_SET(MPCC_MCM_3DLUT_FAST_LOAD_SELECT[mpcc_id], 0, MPCC_MCM_3DLUT_FL_SEL, hubp_idx);
}

static void mpc401_get_3dlut_fast_load_status(struct mpc *mpc, int mpcc_id, uint32_t *done, uint32_t *soft_underflow, uint32_t *hard_underflow)
{
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	REG_GET_3(MPCC_MCM_3DLUT_FAST_LOAD_STATUS[mpcc_id],
			MPCC_MCM_3DLUT_FL_DONE, done,
			MPCC_MCM_3DLUT_FL_SOFT_UNDERFLOW, soft_underflow,
			MPCC_MCM_3DLUT_FL_HARD_UNDERFLOW, hard_underflow);
}

void mpc401_set_movable_cm_location(struct mpc *mpc, enum mpcc_movable_cm_location location, int mpcc_id)
{
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	switch (location) {
	case MPCC_MOVABLE_CM_LOCATION_BEFORE:
		REG_UPDATE(MPCC_MOVABLE_CM_LOCATION_CONTROL[mpcc_id],
				MPCC_MOVABLE_CM_LOCATION_CNTL, 0);
		break;
	case MPCC_MOVABLE_CM_LOCATION_AFTER:
		REG_UPDATE(MPCC_MOVABLE_CM_LOCATION_CONTROL[mpcc_id],
				MPCC_MOVABLE_CM_LOCATION_CNTL, 1);
		break;
	}
}

static enum dc_lut_mode get3dlut_config(
			struct mpc *mpc,
			bool *is_17x17x17,
			bool *is_12bits_color_channel,
			int mpcc_id)
{
	uint32_t i_mode, i_enable_10bits, lut_size;
	enum dc_lut_mode mode;
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	REG_GET(MPCC_MCM_3DLUT_MODE[mpcc_id],
			MPCC_MCM_3DLUT_MODE_CURRENT,  &i_mode);

	REG_GET(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id],
			MPCC_MCM_3DLUT_30BIT_EN, &i_enable_10bits);

	switch (i_mode) {
	case 0:
		mode = LUT_BYPASS;
		break;
	case 1:
		mode = LUT_RAM_A;
		break;
	case 2:
		mode = LUT_RAM_B;
		break;
	default:
		mode = LUT_BYPASS;
		break;
	}
	if (i_enable_10bits > 0)
		*is_12bits_color_channel = false;
	else
		*is_12bits_color_channel = true;

	REG_GET(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_SIZE, &lut_size);

	if (lut_size == 0)
		*is_17x17x17 = true;
	else
		*is_17x17x17 = false;

	return mode;
}

void mpc401_populate_lut(struct mpc *mpc, const enum MCM_LUT_ID id, const union mcm_lut_params params, bool lut_bank_a, int mpcc_id)
{
	const enum dc_lut_mode next_mode = lut_bank_a ? LUT_RAM_A : LUT_RAM_B;
	const struct pwl_params *lut1d = params.pwl;
	const struct pwl_params *lut_shaper = params.pwl;
	bool is_17x17x17;
	bool is_12bits_color_channel;
	const struct dc_rgb *lut0;
	const struct dc_rgb *lut1;
	const struct dc_rgb *lut2;
	const struct dc_rgb *lut3;
	int lut_size0;
	int lut_size;
	const struct tetrahedral_params *lut3d = params.lut3d;

	switch (id) {
	case MCM_LUT_1DLUT:
		if (lut1d == NULL)
			return;

		mpc32_power_on_blnd_lut(mpc, mpcc_id, true);
		mpc32_configure_post1dlut(mpc, mpcc_id, next_mode == LUT_RAM_A);

		if (next_mode == LUT_RAM_A)
			mpc32_program_post1dluta_settings(mpc, mpcc_id, lut1d);
		else
			mpc32_program_post1dlutb_settings(mpc, mpcc_id, lut1d);

		mpc32_program_post1dlut_pwl(
				mpc, mpcc_id, lut1d->rgb_resulted, lut1d->hw_points_num);

		break;
	case MCM_LUT_SHAPER:
		if (lut_shaper == NULL)
			return;
		if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
			mpc32_power_on_shaper_3dlut(mpc, mpcc_id, true);

		mpc32_configure_shaper_lut(mpc, next_mode == LUT_RAM_A, mpcc_id);

		if (next_mode == LUT_RAM_A)
			mpc32_program_shaper_luta_settings(mpc, lut_shaper, mpcc_id);
		else
			mpc32_program_shaper_lutb_settings(mpc, lut_shaper, mpcc_id);

		mpc32_program_shaper_lut(
				mpc, lut_shaper->rgb_resulted, lut_shaper->hw_points_num, mpcc_id);

		mpc32_power_on_shaper_3dlut(mpc, mpcc_id, false);
		break;
	case MCM_LUT_3DLUT:
		if (lut3d == NULL)
			return;

		mpc32_power_on_shaper_3dlut(mpc, mpcc_id, true);

		get3dlut_config(mpc, &is_17x17x17, &is_12bits_color_channel, mpcc_id);

		is_17x17x17 = !lut3d->use_tetrahedral_9;
		is_12bits_color_channel = lut3d->use_12bits;
		if (is_17x17x17) {
			lut0 = lut3d->tetrahedral_17.lut0;
			lut1 = lut3d->tetrahedral_17.lut1;
			lut2 = lut3d->tetrahedral_17.lut2;
			lut3 = lut3d->tetrahedral_17.lut3;
			lut_size0 = sizeof(lut3d->tetrahedral_17.lut0)/
						sizeof(lut3d->tetrahedral_17.lut0[0]);
			lut_size  = sizeof(lut3d->tetrahedral_17.lut1)/
						sizeof(lut3d->tetrahedral_17.lut1[0]);
		} else {
			lut0 = lut3d->tetrahedral_9.lut0;
			lut1 = lut3d->tetrahedral_9.lut1;
			lut2 = lut3d->tetrahedral_9.lut2;
			lut3 = lut3d->tetrahedral_9.lut3;
			lut_size0 = sizeof(lut3d->tetrahedral_9.lut0)/
					sizeof(lut3d->tetrahedral_9.lut0[0]);
			lut_size  = sizeof(lut3d->tetrahedral_9.lut1)/
					sizeof(lut3d->tetrahedral_9.lut1[0]);
			}

		mpc32_select_3dlut_ram(mpc, next_mode,
					is_12bits_color_channel, mpcc_id);
		mpc32_select_3dlut_ram_mask(mpc, 0x1, mpcc_id);
		if (is_12bits_color_channel)
			mpc32_set3dlut_ram12(mpc, lut0, lut_size0, mpcc_id);
		else
			mpc32_set3dlut_ram10(mpc, lut0, lut_size0, mpcc_id);

		mpc32_select_3dlut_ram_mask(mpc, 0x2, mpcc_id);
		if (is_12bits_color_channel)
			mpc32_set3dlut_ram12(mpc, lut1, lut_size, mpcc_id);
		else
			mpc32_set3dlut_ram10(mpc, lut1, lut_size, mpcc_id);

		mpc32_select_3dlut_ram_mask(mpc, 0x4, mpcc_id);
		if (is_12bits_color_channel)
			mpc32_set3dlut_ram12(mpc, lut2, lut_size, mpcc_id);
		else
			mpc32_set3dlut_ram10(mpc, lut2, lut_size, mpcc_id);

		mpc32_select_3dlut_ram_mask(mpc, 0x8, mpcc_id);
		if (is_12bits_color_channel)
			mpc32_set3dlut_ram12(mpc, lut3, lut_size, mpcc_id);
		else
			mpc32_set3dlut_ram10(mpc, lut3, lut_size, mpcc_id);

		if (mpc->ctx->dc->debug.enable_mem_low_power.bits.mpc)
			mpc32_power_on_shaper_3dlut(mpc, mpcc_id, false);

		break;
	}

}

void mpc401_program_lut_mode(
		struct mpc *mpc,
		const enum MCM_LUT_ID id,
		const enum MCM_LUT_XABLE xable,
		bool lut_bank_a,
		int mpcc_id)
{
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	switch (id) {
	case MCM_LUT_3DLUT:
		switch (xable) {
		case MCM_LUT_DISABLE:
			REG_UPDATE(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_MODE, 0);
			break;
		case MCM_LUT_ENABLE:
			REG_UPDATE(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_MODE, lut_bank_a ? 1 : 2);
			break;
		}
		break;
	case MCM_LUT_SHAPER:
		switch (xable) {
		case MCM_LUT_DISABLE:
			REG_UPDATE(MPCC_MCM_SHAPER_CONTROL[mpcc_id], MPCC_MCM_SHAPER_LUT_MODE, 0);
			break;
		case MCM_LUT_ENABLE:
			REG_UPDATE(MPCC_MCM_SHAPER_CONTROL[mpcc_id], MPCC_MCM_SHAPER_LUT_MODE, lut_bank_a ? 1 : 2);
			break;
		}
		break;
	case MCM_LUT_1DLUT:
		switch (xable) {
		case MCM_LUT_DISABLE:
			REG_UPDATE(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
					MPCC_MCM_1DLUT_MODE, 0);
			break;
		case MCM_LUT_ENABLE:
			REG_UPDATE(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
					MPCC_MCM_1DLUT_MODE, 2);
			break;
		}
		REG_UPDATE(MPCC_MCM_1DLUT_CONTROL[mpcc_id],
				MPCC_MCM_1DLUT_SELECT, lut_bank_a ? 0 : 1);
		break;
	}
}

void mpc401_program_lut_read_write_control(struct mpc *mpc, const enum MCM_LUT_ID id, bool lut_bank_a, int mpcc_id)
{
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	switch (id) {
	case MCM_LUT_3DLUT:
		mpc32_select_3dlut_ram_mask(mpc, 0xf, mpcc_id);
		REG_UPDATE(MPCC_MCM_3DLUT_READ_WRITE_CONTROL[mpcc_id], MPCC_MCM_3DLUT_RAM_SEL, lut_bank_a ? 0 : 1);
		break;
	case MCM_LUT_SHAPER:
		mpc32_configure_shaper_lut(mpc, lut_bank_a, mpcc_id);
		break;
	case MCM_LUT_1DLUT:
		mpc32_configure_post1dlut(mpc, lut_bank_a, mpcc_id);
		break;
	}
}

void mpc401_program_3dlut_size(struct mpc *mpc, bool is_17x17x17, int mpcc_id)
{
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	REG_UPDATE(MPCC_MCM_3DLUT_MODE[mpcc_id], MPCC_MCM_3DLUT_SIZE, is_17x17x17 ? 0 : 1);
}

static void program_gamut_remap(
	struct mpc *mpc,
	unsigned int mpcc_id,
	const uint16_t *regval,
	enum mpcc_gamut_remap_id gamut_remap_block_id,
	enum mpcc_gamut_remap_mode_select mode_select)
{
	struct color_matrices_reg gamut_regs;
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	switch (gamut_remap_block_id) {
	case MPCC_OGAM_GAMUT_REMAP:

		if (regval == NULL || mode_select == MPCC_GAMUT_REMAP_MODE_SELECT_0) {
			REG_SET(MPCC_GAMUT_REMAP_MODE[mpcc_id], 0,
				MPCC_GAMUT_REMAP_MODE, mode_select);
			return;
		}

		gamut_regs.shifts.csc_c11 = mpc401->mpc_shift->MPCC_GAMUT_REMAP_C11_A;
		gamut_regs.masks.csc_c11 = mpc401->mpc_mask->MPCC_GAMUT_REMAP_C11_A;
		gamut_regs.shifts.csc_c12 = mpc401->mpc_shift->MPCC_GAMUT_REMAP_C12_A;
		gamut_regs.masks.csc_c12 = mpc401->mpc_mask->MPCC_GAMUT_REMAP_C12_A;

		switch (mode_select) {
		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
			gamut_regs.csc_c11_c12 = REG(MPC_GAMUT_REMAP_C11_C12_A[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_GAMUT_REMAP_C33_C34_A[mpcc_id]);
			break;
		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
			gamut_regs.csc_c11_c12 = REG(MPC_GAMUT_REMAP_C11_C12_B[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_GAMUT_REMAP_C33_C34_B[mpcc_id]);
			break;
		default:
			break;
		}

		cm_helper_program_color_matrices(
			mpc->ctx,
			regval,
			&gamut_regs);

		//select coefficient set to use, set A (MODE_1) or set B (MODE_2)
		REG_SET(MPCC_GAMUT_REMAP_MODE[mpcc_id], 0, MPCC_GAMUT_REMAP_MODE, mode_select);
		break;

	case MPCC_MCM_FIRST_GAMUT_REMAP:
		if (regval == NULL || mode_select == MPCC_GAMUT_REMAP_MODE_SELECT_0) {
			REG_SET(MPCC_MCM_FIRST_GAMUT_REMAP_MODE[mpcc_id], 0,
				MPCC_MCM_FIRST_GAMUT_REMAP_MODE, mode_select);
			return;
		}

		gamut_regs.shifts.csc_c11 = mpc401->mpc_shift->MPCC_MCM_FIRST_GAMUT_REMAP_C11_A;
		gamut_regs.masks.csc_c11 = mpc401->mpc_mask->MPCC_MCM_FIRST_GAMUT_REMAP_C11_A;
		gamut_regs.shifts.csc_c12 = mpc401->mpc_shift->MPCC_MCM_FIRST_GAMUT_REMAP_C12_A;
		gamut_regs.masks.csc_c12 = mpc401->mpc_mask->MPCC_MCM_FIRST_GAMUT_REMAP_C12_A;

		switch (mode_select) {
		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C11_C12_A[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C33_C34_A[mpcc_id]);
			break;
		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C11_C12_B[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C33_C34_B[mpcc_id]);
			break;
		default:
			break;
		}

		cm_helper_program_color_matrices(
			mpc->ctx,
			regval,
			&gamut_regs);

		//select coefficient set to use, set A (MODE_1) or set B (MODE_2)
		REG_SET(MPCC_MCM_FIRST_GAMUT_REMAP_MODE[mpcc_id], 0,
			MPCC_MCM_FIRST_GAMUT_REMAP_MODE, mode_select);
		break;

	case MPCC_MCM_SECOND_GAMUT_REMAP:
		if (regval == NULL || mode_select == MPCC_GAMUT_REMAP_MODE_SELECT_0) {
			REG_SET(MPCC_MCM_SECOND_GAMUT_REMAP_MODE[mpcc_id], 0,
				MPCC_MCM_SECOND_GAMUT_REMAP_MODE, mode_select);
			return;
		}

		gamut_regs.shifts.csc_c11 = mpc401->mpc_shift->MPCC_MCM_SECOND_GAMUT_REMAP_C11_A;
		gamut_regs.masks.csc_c11 = mpc401->mpc_mask->MPCC_MCM_SECOND_GAMUT_REMAP_C11_A;
		gamut_regs.shifts.csc_c12 = mpc401->mpc_shift->MPCC_MCM_SECOND_GAMUT_REMAP_C12_A;
		gamut_regs.masks.csc_c12 = mpc401->mpc_mask->MPCC_MCM_SECOND_GAMUT_REMAP_C12_A;

		switch (mode_select) {
		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C11_C12_A[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C33_C34_A[mpcc_id]);
			break;
		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C11_C12_B[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C33_C34_B[mpcc_id]);
			break;
		default:
			break;
		}

		cm_helper_program_color_matrices(
			mpc->ctx,
			regval,
			&gamut_regs);

		//select coefficient set to use, set A (MODE_1) or set B (MODE_2)
		REG_SET(MPCC_MCM_SECOND_GAMUT_REMAP_MODE[mpcc_id], 0,
			MPCC_MCM_SECOND_GAMUT_REMAP_MODE, mode_select);
		break;

	default:
		break;
	}
}

void mpc401_set_gamut_remap(
	struct mpc *mpc,
	int mpcc_id,
	const struct mpc_grph_gamut_adjustment *adjust)
{
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);
	unsigned int i = 0;
	uint32_t mode_select = 0;

	if (adjust->gamut_adjust_type != GRAPHICS_GAMUT_ADJUST_TYPE_SW) {
		/* Bypass / Disable if type is bypass or hw */
		program_gamut_remap(mpc, mpcc_id, NULL,
			adjust->mpcc_gamut_remap_block_id, MPCC_GAMUT_REMAP_MODE_SELECT_0);
	} else {
		struct fixed31_32 arr_matrix[12];
		uint16_t arr_reg_val[12];

		for (i = 0; i < 12; i++)
			arr_matrix[i] = adjust->temperature_matrix[i];

		convert_float_matrix(arr_reg_val, arr_matrix, 12);

		switch (adjust->mpcc_gamut_remap_block_id) {
		case MPCC_OGAM_GAMUT_REMAP:
			REG_GET(MPCC_GAMUT_REMAP_MODE[mpcc_id],
				MPCC_GAMUT_REMAP_MODE_CURRENT, &mode_select);
			break;
		case MPCC_MCM_FIRST_GAMUT_REMAP:
			REG_GET(MPCC_MCM_FIRST_GAMUT_REMAP_MODE[mpcc_id],
				MPCC_MCM_FIRST_GAMUT_REMAP_MODE_CURRENT, &mode_select);
			break;
		case MPCC_MCM_SECOND_GAMUT_REMAP:
			REG_GET(MPCC_MCM_SECOND_GAMUT_REMAP_MODE[mpcc_id],
				MPCC_MCM_SECOND_GAMUT_REMAP_MODE_CURRENT, &mode_select);
			break;
		default:
			break;
		}

		//If current set in use not set A (MODE_1), then use set A, otherwise use set B
		if (mode_select != MPCC_GAMUT_REMAP_MODE_SELECT_1)
			mode_select = MPCC_GAMUT_REMAP_MODE_SELECT_1;
		else
			mode_select = MPCC_GAMUT_REMAP_MODE_SELECT_2;

		program_gamut_remap(mpc, mpcc_id, arr_reg_val,
			adjust->mpcc_gamut_remap_block_id, mode_select);
	}
}

static void read_gamut_remap(struct mpc *mpc,
	int mpcc_id,
	uint16_t *regval,
	enum mpcc_gamut_remap_id gamut_remap_block_id,
	uint32_t *mode_select)
{
	struct color_matrices_reg gamut_regs = {0};
	struct dcn401_mpc *mpc401 = TO_DCN401_MPC(mpc);

	switch (gamut_remap_block_id) {
	case MPCC_OGAM_GAMUT_REMAP:
		//current coefficient set in use
		REG_GET(MPCC_GAMUT_REMAP_MODE[mpcc_id], MPCC_GAMUT_REMAP_MODE_CURRENT, mode_select);

		gamut_regs.shifts.csc_c11 = mpc401->mpc_shift->MPCC_GAMUT_REMAP_C11_A;
		gamut_regs.masks.csc_c11 = mpc401->mpc_mask->MPCC_GAMUT_REMAP_C11_A;
		gamut_regs.shifts.csc_c12 = mpc401->mpc_shift->MPCC_GAMUT_REMAP_C12_A;
		gamut_regs.masks.csc_c12 = mpc401->mpc_mask->MPCC_GAMUT_REMAP_C12_A;

		switch (*mode_select) {
		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
			gamut_regs.csc_c11_c12 = REG(MPC_GAMUT_REMAP_C11_C12_A[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_GAMUT_REMAP_C33_C34_A[mpcc_id]);
			break;
		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
			gamut_regs.csc_c11_c12 = REG(MPC_GAMUT_REMAP_C11_C12_B[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_GAMUT_REMAP_C33_C34_B[mpcc_id]);
			break;
		default:
			break;
		}
		break;

	case MPCC_MCM_FIRST_GAMUT_REMAP:
		REG_GET(MPCC_MCM_FIRST_GAMUT_REMAP_MODE[mpcc_id],
				MPCC_MCM_FIRST_GAMUT_REMAP_MODE_CURRENT, mode_select);

		gamut_regs.shifts.csc_c11 = mpc401->mpc_shift->MPCC_MCM_FIRST_GAMUT_REMAP_C11_A;
		gamut_regs.masks.csc_c11 = mpc401->mpc_mask->MPCC_MCM_FIRST_GAMUT_REMAP_C11_A;
		gamut_regs.shifts.csc_c12 = mpc401->mpc_shift->MPCC_MCM_FIRST_GAMUT_REMAP_C12_A;
		gamut_regs.masks.csc_c12 = mpc401->mpc_mask->MPCC_MCM_FIRST_GAMUT_REMAP_C12_A;

		switch (*mode_select) {
		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C11_C12_A[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C33_C34_A[mpcc_id]);
			break;
		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C11_C12_B[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_FIRST_GAMUT_REMAP_C33_C34_B[mpcc_id]);
			break;
		default:
			break;
		}
		break;

	case MPCC_MCM_SECOND_GAMUT_REMAP:
		REG_GET(MPCC_MCM_SECOND_GAMUT_REMAP_MODE[mpcc_id],
				MPCC_MCM_SECOND_GAMUT_REMAP_MODE_CURRENT, mode_select);

		gamut_regs.shifts.csc_c11 = mpc401->mpc_shift->MPCC_MCM_SECOND_GAMUT_REMAP_C11_A;
		gamut_regs.masks.csc_c11 = mpc401->mpc_mask->MPCC_MCM_SECOND_GAMUT_REMAP_C11_A;
		gamut_regs.shifts.csc_c12 = mpc401->mpc_shift->MPCC_MCM_SECOND_GAMUT_REMAP_C12_A;
		gamut_regs.masks.csc_c12 = mpc401->mpc_mask->MPCC_MCM_SECOND_GAMUT_REMAP_C12_A;

		switch (*mode_select) {
		case MPCC_GAMUT_REMAP_MODE_SELECT_1:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C11_C12_A[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C33_C34_A[mpcc_id]);
			break;
		case MPCC_GAMUT_REMAP_MODE_SELECT_2:
			gamut_regs.csc_c11_c12 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C11_C12_B[mpcc_id]);
			gamut_regs.csc_c33_c34 = REG(MPC_MCM_SECOND_GAMUT_REMAP_C33_C34_B[mpcc_id]);
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}

	if (*mode_select != MPCC_GAMUT_REMAP_MODE_SELECT_0) {
		cm_helper_read_color_matrices(
			mpc401->base.ctx,
			regval,
			&gamut_regs);
	}
}

void mpc401_get_gamut_remap(struct mpc *mpc,
	int mpcc_id,
	struct mpc_grph_gamut_adjustment *adjust)
{
	uint16_t arr_reg_val[12] = {0};
	uint32_t mode_select = MPCC_GAMUT_REMAP_MODE_SELECT_0;

	read_gamut_remap(mpc, mpcc_id, arr_reg_val, adjust->mpcc_gamut_remap_block_id, &mode_select);

	if (mode_select == MPCC_GAMUT_REMAP_MODE_SELECT_0) {
		adjust->gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_BYPASS;
		return;
	}

	adjust->gamut_adjust_type = GRAPHICS_GAMUT_ADJUST_TYPE_SW;
	convert_hw_matrix(adjust->temperature_matrix,
		arr_reg_val, ARRAY_SIZE(arr_reg_val));
}

static const struct mpc_funcs dcn401_mpc_funcs = {
	.read_mpcc_state = mpc1_read_mpcc_state,
	.insert_plane = mpc1_insert_plane,
	.remove_mpcc = mpc1_remove_mpcc,
	.mpc_init = mpc32_mpc_init,
	.mpc_init_single_inst = mpc3_mpc_init_single_inst,
	.update_blending = mpc2_update_blending,
	.cursor_lock = mpc1_cursor_lock,
	.get_mpcc_for_dpp = mpc1_get_mpcc_for_dpp,
	.wait_for_idle = mpc2_assert_idle_mpcc,
	.assert_mpcc_idle_before_connect = mpc2_assert_mpcc_idle_before_connect,
	.init_mpcc_list_from_hw = mpc1_init_mpcc_list_from_hw,
	.set_denorm =  mpc3_set_denorm,
	.set_denorm_clamp = mpc3_set_denorm_clamp,
	.set_output_csc = mpc3_set_output_csc,
	.set_ocsc_default = mpc3_set_ocsc_default,
	.set_output_gamma = mpc3_set_output_gamma,
	.insert_plane_to_secondary = NULL,
	.remove_mpcc_from_secondary =  NULL,
	.set_dwb_mux = mpc3_set_dwb_mux,
	.disable_dwb_mux = mpc3_disable_dwb_mux,
	.is_dwb_idle = mpc3_is_dwb_idle,
	.set_gamut_remap = mpc401_set_gamut_remap,
	.program_shaper = mpc32_program_shaper,
	.program_3dlut = mpc32_program_3dlut,
	.program_1dlut = mpc32_program_post1dlut,
	.acquire_rmu = NULL,
	.release_rmu = NULL,
	.power_on_mpc_mem_pwr = mpc3_power_on_ogam_lut,
	.get_mpc_out_mux = mpc1_get_mpc_out_mux,
	.set_bg_color = mpc1_set_bg_color,
	.set_movable_cm_location = mpc401_set_movable_cm_location,
	.update_3dlut_fast_load_select = mpc401_update_3dlut_fast_load_select,
	.get_3dlut_fast_load_status = mpc401_get_3dlut_fast_load_status,
	.populate_lut = mpc401_populate_lut,
	.program_lut_read_write_control = mpc401_program_lut_read_write_control,
	.program_lut_mode = mpc401_program_lut_mode,
	.program_3dlut_size = mpc401_program_3dlut_size,
};


void dcn401_mpc_construct(struct dcn401_mpc *mpc401,
	struct dc_context *ctx,
	const struct dcn401_mpc_registers *mpc_regs,
	const struct dcn401_mpc_shift *mpc_shift,
	const struct dcn401_mpc_mask *mpc_mask,
	int num_mpcc,
	int num_rmu)
{
	int i;

	mpc401->base.ctx = ctx;

	mpc401->base.funcs = &dcn401_mpc_funcs;

	mpc401->mpc_regs = mpc_regs;
	mpc401->mpc_shift = mpc_shift;
	mpc401->mpc_mask = mpc_mask;

	mpc401->mpcc_in_use_mask = 0;
	mpc401->num_mpcc = num_mpcc;
	mpc401->num_rmu = num_rmu;

	for (i = 0; i < MAX_MPCC; i++)
		mpc3_init_mpcc(&mpc401->base.mpcc_array[i], i);
}
