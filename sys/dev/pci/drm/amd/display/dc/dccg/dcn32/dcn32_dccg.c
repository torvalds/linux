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

#include "reg_helper.h"
#include "core_types.h"
#include "dcn32_dccg.h"

#define TO_DCN_DCCG(dccg)\
	container_of(dccg, struct dcn_dccg, base)

#define REG(reg) \
	(dccg_dcn->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	dccg_dcn->dccg_shift->field_name, dccg_dcn->dccg_mask->field_name

#define CTX \
	dccg_dcn->base.ctx
#define DC_LOGGER \
	dccg->ctx->logger

static void dccg32_trigger_dio_fifo_resync(
	struct dccg *dccg)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t dispclk_rdivider_value = 0;

	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_RDIVIDER, &dispclk_rdivider_value);

	/* Not valid for the WDIVIDER to be set to 0 */
	if (dispclk_rdivider_value != 0)
		REG_UPDATE(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, dispclk_rdivider_value);
}

static void dccg32_get_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		uint32_t *k1,
		uint32_t *k2)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t val_k1 = PIXEL_RATE_DIV_NA, val_k2 = PIXEL_RATE_DIV_NA;

	*k1 = PIXEL_RATE_DIV_NA;
	*k2 = PIXEL_RATE_DIV_NA;

	switch (otg_inst) {
	case 0:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG0_PIXEL_RATE_DIVK1, &val_k1,
			OTG0_PIXEL_RATE_DIVK2, &val_k2);
		break;
	case 1:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG1_PIXEL_RATE_DIVK1, &val_k1,
			OTG1_PIXEL_RATE_DIVK2, &val_k2);
		break;
	case 2:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG2_PIXEL_RATE_DIVK1, &val_k1,
			OTG2_PIXEL_RATE_DIVK2, &val_k2);
		break;
	case 3:
		REG_GET_2(OTG_PIXEL_RATE_DIV,
			OTG3_PIXEL_RATE_DIVK1, &val_k1,
			OTG3_PIXEL_RATE_DIVK2, &val_k2);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}

	*k1 = val_k1;
	*k2 = val_k2;
}

static void dccg32_set_pixel_rate_div(
		struct dccg *dccg,
		uint32_t otg_inst,
		enum pixel_rate_div k1,
		enum pixel_rate_div k2)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	uint32_t cur_k1 = PIXEL_RATE_DIV_NA;
	uint32_t cur_k2 = PIXEL_RATE_DIV_NA;

	// Don't program 0xF into the register field. Not valid since
	// K1 / K2 field is only 1 / 2 bits wide
	if (k1 == PIXEL_RATE_DIV_NA || k2 == PIXEL_RATE_DIV_NA) {
		BREAK_TO_DEBUGGER();
		return;
	}

	dccg32_get_pixel_rate_div(dccg, otg_inst, &cur_k1, &cur_k2);
	if (k1 == cur_k1 && k2 == cur_k2)
		return;

	switch (otg_inst) {
	case 0:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG0_PIXEL_RATE_DIVK1, k1,
				OTG0_PIXEL_RATE_DIVK2, k2);
		break;
	case 1:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG1_PIXEL_RATE_DIVK1, k1,
				OTG1_PIXEL_RATE_DIVK2, k2);
		break;
	case 2:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG2_PIXEL_RATE_DIVK1, k1,
				OTG2_PIXEL_RATE_DIVK2, k2);
		break;
	case 3:
		REG_UPDATE_2(OTG_PIXEL_RATE_DIV,
				OTG3_PIXEL_RATE_DIVK1, k1,
				OTG3_PIXEL_RATE_DIVK2, k2);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg32_set_dtbclk_p_src(
		struct dccg *dccg,
		enum streamclk_source src,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	uint32_t p_src_sel = 0; /* selects dprefclk */
	if (src == DTBCLK0)
		p_src_sel = 2;  /* selects dtbclk0 */

	switch (otg_inst) {
	case 0:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P0_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P0_SRC_SEL, p_src_sel,
					DTBCLK_P0_EN, 1);
		break;
	case 1:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P1_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P1_SRC_SEL, p_src_sel,
					DTBCLK_P1_EN, 1);
		break;
	case 2:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P2_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P2_SRC_SEL, p_src_sel,
					DTBCLK_P2_EN, 1);
		break;
	case 3:
		if (src == REFCLK)
			REG_UPDATE(DTBCLK_P_CNTL,
					DTBCLK_P3_EN, 0);
		else
			REG_UPDATE_2(DTBCLK_P_CNTL,
					DTBCLK_P3_SRC_SEL, p_src_sel,
					DTBCLK_P3_EN, 1);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}

}

/* Controls the generation of pixel valid for OTG in (OTG -> HPO case) */
static void dccg32_set_dtbclk_dto(
		struct dccg *dccg,
		const struct dtbclk_dto_params *params)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);
	/* DTO Output Rate / Pixel Rate = 1/4 */
	int req_dtbclk_khz = params->pixclk_khz / 4;

	if (params->ref_dtbclk_khz && req_dtbclk_khz) {
		uint32_t modulo, phase;

		// phase / modulo = dtbclk / dtbclk ref
		modulo = params->ref_dtbclk_khz * 1000;
		phase = req_dtbclk_khz * 1000;

		REG_WRITE(DTBCLK_DTO_MODULO[params->otg_inst], modulo);
		REG_WRITE(DTBCLK_DTO_PHASE[params->otg_inst], phase);

		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_ENABLE[params->otg_inst], 1);

		REG_WAIT(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLKDTO_ENABLE_STATUS[params->otg_inst], 1,
				1, 100);

		/* program OTG_PIXEL_RATE_DIV for DIVK1 and DIVK2 fields */
		dccg32_set_pixel_rate_div(dccg, params->otg_inst, PIXEL_RATE_DIV_BY_1, PIXEL_RATE_DIV_BY_1);

		/* The recommended programming sequence to enable DTBCLK DTO to generate
		 * valid pixel HPO DPSTREAM ENCODER, specifies that DTO source select should
		 * be set only after DTO is enabled
		 */
		REG_UPDATE(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				PIPE_DTO_SRC_SEL[params->otg_inst], 2);
	} else {
		REG_UPDATE_2(OTG_PIXEL_RATE_CNTL[params->otg_inst],
				DTBCLK_DTO_ENABLE[params->otg_inst], 0,
				PIPE_DTO_SRC_SEL[params->otg_inst], params->is_hdmi ? 0 : 1);
		REG_WRITE(DTBCLK_DTO_MODULO[params->otg_inst], 0);
		REG_WRITE(DTBCLK_DTO_PHASE[params->otg_inst], 0);
	}
}

static void dccg32_set_valid_pixel_rate(
		struct dccg *dccg,
		int ref_dtbclk_khz,
		int otg_inst,
		int pixclk_khz)
{
	struct dtbclk_dto_params dto_params = {0};

	dto_params.ref_dtbclk_khz = ref_dtbclk_khz;
	dto_params.otg_inst = otg_inst;
	dto_params.pixclk_khz = pixclk_khz;
	dto_params.is_hdmi = true;

	dccg32_set_dtbclk_dto(dccg, &dto_params);
}

static void dccg32_get_dccg_ref_freq(struct dccg *dccg,
		unsigned int xtalin_freq_inKhz,
		unsigned int *dccg_ref_freq_inKhz)
{
	/*
	 * Assume refclk is sourced from xtalin
	 * expect 100MHz
	 */
	*dccg_ref_freq_inKhz = xtalin_freq_inKhz;
	return;
}

static void dccg32_set_dpstreamclk(
		struct dccg *dccg,
		enum streamclk_source src,
		int otg_inst,
		int dp_hpo_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	/* set the dtbclk_p source */
	/* always program refclk as DTBCLK. No use-case expected to require DPREFCLK as refclk */
	dccg32_set_dtbclk_p_src(dccg, DTBCLK0, otg_inst);

	/* enabled to select one of the DTBCLKs for pipe */
	switch (dp_hpo_inst) {
	case 0:
		REG_UPDATE_2(DPSTREAMCLK_CNTL,
			     DPSTREAMCLK0_EN,
			     (src == REFCLK) ? 0 : 1, DPSTREAMCLK0_SRC_SEL, otg_inst);
		break;
	case 1:
		REG_UPDATE_2(DPSTREAMCLK_CNTL, DPSTREAMCLK1_EN,
			     (src == REFCLK) ? 0 : 1, DPSTREAMCLK1_SRC_SEL, otg_inst);
		break;
	case 2:
		REG_UPDATE_2(DPSTREAMCLK_CNTL, DPSTREAMCLK2_EN,
			     (src == REFCLK) ? 0 : 1, DPSTREAMCLK2_SRC_SEL, otg_inst);
		break;
	case 3:
		REG_UPDATE_2(DPSTREAMCLK_CNTL, DPSTREAMCLK3_EN,
			     (src == REFCLK) ? 0 : 1, DPSTREAMCLK3_SRC_SEL, otg_inst);
		break;
	default:
		BREAK_TO_DEBUGGER();
		return;
	}
}

static void dccg32_otg_add_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_ADD_PIXEL[otg_inst], 1);
}

static void dccg32_otg_drop_pixel(struct dccg *dccg,
		uint32_t otg_inst)
{
	struct dcn_dccg *dccg_dcn = TO_DCN_DCCG(dccg);

	REG_UPDATE(OTG_PIXEL_RATE_CNTL[otg_inst],
			OTG_DROP_PIXEL[otg_inst], 1);
}

static const struct dccg_funcs dccg32_funcs = {
	.update_dpp_dto = dccg2_update_dpp_dto,
	.get_dccg_ref_freq = dccg32_get_dccg_ref_freq,
	.dccg_init = dccg31_init,
	.set_dpstreamclk = dccg32_set_dpstreamclk,
	.enable_symclk32_se = dccg31_enable_symclk32_se,
	.disable_symclk32_se = dccg31_disable_symclk32_se,
	.enable_symclk32_le = dccg31_enable_symclk32_le,
	.disable_symclk32_le = dccg31_disable_symclk32_le,
	.set_physymclk = dccg31_set_physymclk,
	.set_dtbclk_dto = dccg32_set_dtbclk_dto,
	.set_valid_pixel_rate = dccg32_set_valid_pixel_rate,
	.set_fifo_errdet_ovr_en = dccg2_set_fifo_errdet_ovr_en,
	.set_audio_dtbclk_dto = dccg31_set_audio_dtbclk_dto,
	.otg_add_pixel = dccg32_otg_add_pixel,
	.otg_drop_pixel = dccg32_otg_drop_pixel,
	.set_pixel_rate_div = dccg32_set_pixel_rate_div,
	.get_pixel_rate_div = dccg32_get_pixel_rate_div,
	.trigger_dio_fifo_resync = dccg32_trigger_dio_fifo_resync,
	.set_dtbclk_p_src = dccg32_set_dtbclk_p_src,
};

struct dccg *dccg32_create(
	struct dc_context *ctx,
	const struct dccg_registers *regs,
	const struct dccg_shift *dccg_shift,
	const struct dccg_mask *dccg_mask)
{
	struct dcn_dccg *dccg_dcn = kzalloc(sizeof(*dccg_dcn), GFP_KERNEL);
	struct dccg *base;

	if (dccg_dcn == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	base = &dccg_dcn->base;
	base->ctx = ctx;
	base->funcs = &dccg32_funcs;

	dccg_dcn->regs = regs;
	dccg_dcn->dccg_shift = dccg_shift;
	dccg_dcn->dccg_mask = dccg_mask;

	return &dccg_dcn->base;
}
