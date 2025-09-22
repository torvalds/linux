/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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
#include "link_encoder.h"
#include "dcn10_link_encoder.h"
#include "stream_encoder.h"
#include "dc_bios_types.h"

#include "gpio_service_interface.h"

#define CTX \
	enc10->base.ctx
#define DC_LOGGER \
	enc10->base.ctx->logger

#define REG(reg)\
	(enc10->link_regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc10->link_shift->field_name, enc10->link_mask->field_name


/*
 * @brief
 * Trigger Source Select
 * ASIC-dependent, actual values for register programming
 */
#define DCN10_DIG_FE_SOURCE_SELECT_INVALID 0x0
#define DCN10_DIG_FE_SOURCE_SELECT_DIGA 0x1
#define DCN10_DIG_FE_SOURCE_SELECT_DIGB 0x2
#define DCN10_DIG_FE_SOURCE_SELECT_DIGC 0x4
#define DCN10_DIG_FE_SOURCE_SELECT_DIGD 0x08
#define DCN10_DIG_FE_SOURCE_SELECT_DIGE 0x10
#define DCN10_DIG_FE_SOURCE_SELECT_DIGF 0x20
#define DCN10_DIG_FE_SOURCE_SELECT_DIGG 0x40

enum {
	DP_MST_UPDATE_MAX_RETRY = 50
};

static const struct link_encoder_funcs dcn10_lnk_enc_funcs = {
	.validate_output_with_stream =
		dcn10_link_encoder_validate_output_with_stream,
	.hw_init = dcn10_link_encoder_hw_init,
	.setup = dcn10_link_encoder_setup,
	.enable_tmds_output = dcn10_link_encoder_enable_tmds_output,
	.enable_dp_output = dcn10_link_encoder_enable_dp_output,
	.enable_dp_mst_output = dcn10_link_encoder_enable_dp_mst_output,
	.disable_output = dcn10_link_encoder_disable_output,
	.dp_set_lane_settings = dcn10_link_encoder_dp_set_lane_settings,
	.dp_set_phy_pattern = dcn10_link_encoder_dp_set_phy_pattern,
	.update_mst_stream_allocation_table =
		dcn10_link_encoder_update_mst_stream_allocation_table,
	.psr_program_dp_dphy_fast_training =
			dcn10_psr_program_dp_dphy_fast_training,
	.psr_program_secondary_packet = dcn10_psr_program_secondary_packet,
	.connect_dig_be_to_fe = dcn10_link_encoder_connect_dig_be_to_fe,
	.enable_hpd = dcn10_link_encoder_enable_hpd,
	.disable_hpd = dcn10_link_encoder_disable_hpd,
	.is_dig_enabled = dcn10_is_dig_enabled,
	.get_dig_frontend = dcn10_get_dig_frontend,
	.get_dig_mode = dcn10_get_dig_mode,
	.destroy = dcn10_link_encoder_destroy,
	.get_max_link_cap = dcn10_link_encoder_get_max_link_cap,
};

static enum bp_result link_transmitter_control(
	struct dcn10_link_encoder *enc10,
	struct bp_transmitter_control *cntl)
{
	enum bp_result result;
	struct dc_bios *bp = enc10->base.ctx->dc_bios;

	result = bp->funcs->transmitter_control(bp, cntl);

	return result;
}

static void enable_phy_bypass_mode(
	struct dcn10_link_encoder *enc10,
	bool enable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset
	 */
	REG_UPDATE(DP_DPHY_CNTL, DPHY_BYPASS, enable);

}

static void disable_prbs_symbols(
	struct dcn10_link_encoder *enc10,
	bool disable)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset
	 */
	REG_UPDATE_4(DP_DPHY_CNTL,
			DPHY_ATEST_SEL_LANE0, disable,
			DPHY_ATEST_SEL_LANE1, disable,
			DPHY_ATEST_SEL_LANE2, disable,
			DPHY_ATEST_SEL_LANE3, disable);
}

static void disable_prbs_mode(
	struct dcn10_link_encoder *enc10)
{
	REG_UPDATE(DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN, 0);
}

static void program_pattern_symbols(
	struct dcn10_link_encoder *enc10,
	uint16_t pattern_symbols[8])
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset
	 */
	REG_SET_3(DP_DPHY_SYM0, 0,
			DPHY_SYM1, pattern_symbols[0],
			DPHY_SYM2, pattern_symbols[1],
			DPHY_SYM3, pattern_symbols[2]);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset
	 */
	REG_SET_3(DP_DPHY_SYM1, 0,
			DPHY_SYM4, pattern_symbols[3],
			DPHY_SYM5, pattern_symbols[4],
			DPHY_SYM6, pattern_symbols[5]);

	/* This register resides in DP back end block;
	 * transmitter is used for the offset
	 */
	REG_SET_2(DP_DPHY_SYM2, 0,
			DPHY_SYM7, pattern_symbols[6],
			DPHY_SYM8, pattern_symbols[7]);
}

static void set_dp_phy_pattern_d102(
	struct dcn10_link_encoder *enc10)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc10, false);

	/* For 10-bit PRBS or debug symbols
	 * please use the following sequence:
	 *
	 * Enable debug symbols on the lanes
	 */
	disable_prbs_symbols(enc10, true);

	/* Disable PRBS mode */
	disable_prbs_mode(enc10);

	/* Program debug symbols to be output */
	{
		uint16_t pattern_symbols[8] = {
			0x2AA, 0x2AA, 0x2AA, 0x2AA,
			0x2AA, 0x2AA, 0x2AA, 0x2AA
		};

		program_pattern_symbols(enc10, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc10, true);
}

static void set_link_training_complete(
	struct dcn10_link_encoder *enc10,
	bool complete)
{
	/* This register resides in DP back end block;
	 * transmitter is used for the offset
	 */
	REG_UPDATE(DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE, complete);

}

void dcn10_link_encoder_set_dp_phy_pattern_training_pattern(
	struct link_encoder *enc,
	uint32_t index)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	/* Write Training Pattern */

	REG_WRITE(DP_DPHY_TRAINING_PATTERN_SEL, index);

	/* Set HW Register Training Complete to false */

	set_link_training_complete(enc10, false);

	/* Disable PHY Bypass mode to output Training Pattern */

	enable_phy_bypass_mode(enc10, false);

	/* Disable PRBS mode */
	disable_prbs_mode(enc10);
}

static void setup_panel_mode(
	struct dcn10_link_encoder *enc10,
	enum dp_panel_mode panel_mode)
{
	uint32_t value;

	if (!REG(DP_DPHY_INTERNAL_CTRL))
		return;

	value = REG_READ(DP_DPHY_INTERNAL_CTRL);

	switch (panel_mode) {
	case DP_PANEL_MODE_EDP:
		value = 0x1;
		break;
	case DP_PANEL_MODE_SPECIAL:
		value = 0x11;
		break;
	default:
		value = 0x0;
		break;
	}

	REG_WRITE(DP_DPHY_INTERNAL_CTRL, value);
}

static void set_dp_phy_pattern_symbol_error(
	struct dcn10_link_encoder *enc10)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc10, false);

	/* program correct panel mode*/
	setup_panel_mode(enc10, DP_PANEL_MODE_DEFAULT);

	/* A PRBS23 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */
	disable_prbs_symbols(enc10, false);

	/* For PRBS23 Set bit DPHY_PRBS_SEL=1 and Set bit DPHY_PRBS_EN=1 */
	REG_UPDATE_2(DP_DPHY_PRBS_CNTL,
			DPHY_PRBS_SEL, 1,
			DPHY_PRBS_EN, 1);

	/* Enable phy bypass mode to enable the test pattern */
	enable_phy_bypass_mode(enc10, true);
}

static void set_dp_phy_pattern_prbs7(
	struct dcn10_link_encoder *enc10)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc10, false);

	/* A PRBS7 pattern is used for most DP electrical measurements. */

	/* Enable PRBS symbols on the lanes */
	disable_prbs_symbols(enc10, false);

	/* For PRBS7 Set bit DPHY_PRBS_SEL=0 and Set bit DPHY_PRBS_EN=1 */
	REG_UPDATE_2(DP_DPHY_PRBS_CNTL,
			DPHY_PRBS_SEL, 0,
			DPHY_PRBS_EN, 1);

	/* Enable phy bypass mode to enable the test pattern */
	enable_phy_bypass_mode(enc10, true);
}

static void set_dp_phy_pattern_80bit_custom(
	struct dcn10_link_encoder *enc10,
	const uint8_t *pattern)
{
	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc10, false);

	/* Enable debug symbols on the lanes */

	disable_prbs_symbols(enc10, true);

	/* Enable PHY bypass mode to enable the test pattern */
	/* TODO is it really needed ? */

	enable_phy_bypass_mode(enc10, true);

	/* Program 80 bit custom pattern */
	{
		uint16_t pattern_symbols[8];

		pattern_symbols[0] =
			((pattern[1] & 0x03) << 8) | pattern[0];
		pattern_symbols[1] =
			((pattern[2] & 0x0f) << 6) | ((pattern[1] >> 2) & 0x3f);
		pattern_symbols[2] =
			((pattern[3] & 0x3f) << 4) | ((pattern[2] >> 4) & 0x0f);
		pattern_symbols[3] =
			(pattern[4] << 2) | ((pattern[3] >> 6) & 0x03);
		pattern_symbols[4] =
			((pattern[6] & 0x03) << 8) | pattern[5];
		pattern_symbols[5] =
			((pattern[7] & 0x0f) << 6) | ((pattern[6] >> 2) & 0x3f);
		pattern_symbols[6] =
			((pattern[8] & 0x3f) << 4) | ((pattern[7] >> 4) & 0x0f);
		pattern_symbols[7] =
			(pattern[9] << 2) | ((pattern[8] >> 6) & 0x03);

		program_pattern_symbols(enc10, pattern_symbols);
	}

	/* Enable phy bypass mode to enable the test pattern */

	enable_phy_bypass_mode(enc10, true);
}

static void set_dp_phy_pattern_hbr2_compliance_cp2520_2(
	struct dcn10_link_encoder *enc10,
	unsigned int cp2520_pattern)
{

	/* previously there is a register DP_HBR2_EYE_PATTERN
	 * that is enabled to get the pattern.
	 * But it does not work with the latest spec change,
	 * so we are programming the following registers manually.
	 *
	 * The following settings have been confirmed
	 * by Nick Chorney and Sandra Liu
	 */

	/* Disable PHY Bypass mode to setup the test pattern */

	enable_phy_bypass_mode(enc10, false);

	/* Setup DIG encoder in DP SST mode */
	enc10->base.funcs->setup(&enc10->base, SIGNAL_TYPE_DISPLAY_PORT);

	/* ensure normal panel mode. */
	setup_panel_mode(enc10, DP_PANEL_MODE_DEFAULT);

	/* no vbid after BS (SR)
	 * DP_LINK_FRAMING_CNTL changed history Sandra Liu
	 * 11000260 / 11000104 / 110000FC
	 */
	REG_UPDATE_3(DP_LINK_FRAMING_CNTL,
			DP_IDLE_BS_INTERVAL, 0xFC,
			DP_VBID_DISABLE, 1,
			DP_VID_ENHANCED_FRAME_MODE, 1);

	/* swap every BS with SR */
	REG_UPDATE(DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_BS_COUNT, 0);

	/* select cp2520 patterns */
	if (REG(DP_DPHY_HBR2_PATTERN_CONTROL))
		REG_UPDATE(DP_DPHY_HBR2_PATTERN_CONTROL,
				DP_DPHY_HBR2_PATTERN_CONTROL, cp2520_pattern);
	else
		/* pre-DCE11 can only generate CP2520 pattern 2 */
		ASSERT(cp2520_pattern == 2);

	/* set link training complete */
	set_link_training_complete(enc10, true);

	/* disable video stream */
	REG_UPDATE(DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, 0);

	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc10, false);
}

static void set_dp_phy_pattern_passthrough_mode(
	struct dcn10_link_encoder *enc10,
	enum dp_panel_mode panel_mode)
{
	/* program correct panel mode */
	setup_panel_mode(enc10, panel_mode);

	/* restore LINK_FRAMING_CNTL and DPHY_SCRAMBLER_BS_COUNT
	 * in case we were doing HBR2 compliance pattern before
	 */
	REG_UPDATE_3(DP_LINK_FRAMING_CNTL,
			DP_IDLE_BS_INTERVAL, 0x2000,
			DP_VBID_DISABLE, 0,
			DP_VID_ENHANCED_FRAME_MODE, 1);

	REG_UPDATE(DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_BS_COUNT, 0x1FF);

	/* set link training complete */
	set_link_training_complete(enc10, true);

	/* Disable PHY Bypass mode to setup the test pattern */
	enable_phy_bypass_mode(enc10, false);

	/* Disable PRBS mode */
	disable_prbs_mode(enc10);
}

/* return value is bit-vector */
static uint8_t get_frontend_source(
	enum engine_id engine)
{
	switch (engine) {
	case ENGINE_ID_DIGA:
		return DCN10_DIG_FE_SOURCE_SELECT_DIGA;
	case ENGINE_ID_DIGB:
		return DCN10_DIG_FE_SOURCE_SELECT_DIGB;
	case ENGINE_ID_DIGC:
		return DCN10_DIG_FE_SOURCE_SELECT_DIGC;
	case ENGINE_ID_DIGD:
		return DCN10_DIG_FE_SOURCE_SELECT_DIGD;
	case ENGINE_ID_DIGE:
		return DCN10_DIG_FE_SOURCE_SELECT_DIGE;
	case ENGINE_ID_DIGF:
		return DCN10_DIG_FE_SOURCE_SELECT_DIGF;
	case ENGINE_ID_DIGG:
		return DCN10_DIG_FE_SOURCE_SELECT_DIGG;
	default:
		ASSERT_CRITICAL(false);
		return DCN10_DIG_FE_SOURCE_SELECT_INVALID;
	}
}

unsigned int dcn10_get_dig_frontend(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	int32_t value;
	enum engine_id result;

	REG_GET(DIG_BE_CNTL, DIG_FE_SOURCE_SELECT, &value);

	switch (value) {
	case DCN10_DIG_FE_SOURCE_SELECT_DIGA:
		result = ENGINE_ID_DIGA;
		break;
	case DCN10_DIG_FE_SOURCE_SELECT_DIGB:
		result = ENGINE_ID_DIGB;
		break;
	case DCN10_DIG_FE_SOURCE_SELECT_DIGC:
		result = ENGINE_ID_DIGC;
		break;
	case DCN10_DIG_FE_SOURCE_SELECT_DIGD:
		result = ENGINE_ID_DIGD;
		break;
	case DCN10_DIG_FE_SOURCE_SELECT_DIGE:
		result = ENGINE_ID_DIGE;
		break;
	case DCN10_DIG_FE_SOURCE_SELECT_DIGF:
		result = ENGINE_ID_DIGF;
		break;
	case DCN10_DIG_FE_SOURCE_SELECT_DIGG:
		result = ENGINE_ID_DIGG;
		break;
	default:
		// invalid source select DIG
		result = ENGINE_ID_UNKNOWN;
	}

	return result;

}

void enc1_configure_encoder(
	struct dcn10_link_encoder *enc10,
	const struct dc_link_settings *link_settings)
{
	/* set number of lanes */
	REG_SET(DP_CONFIG, 0,
			DP_UDI_LANES, link_settings->lane_count - LANE_COUNT_ONE);

	/* setup scrambler */
	REG_UPDATE(DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_ADVANCE, 1);
}

void dcn10_psr_program_dp_dphy_fast_training(struct link_encoder *enc,
			bool exit_link_training_required)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	if (exit_link_training_required)
		REG_UPDATE(DP_DPHY_FAST_TRAINING,
				DPHY_RX_FAST_TRAINING_CAPABLE, 1);
	else {
		REG_UPDATE(DP_DPHY_FAST_TRAINING,
				DPHY_RX_FAST_TRAINING_CAPABLE, 0);
		/*In DCE 11, we are able to pre-program a Force SR register
		 * to be able to trigger SR symbol after 5 idle patterns
		 * transmitted. Upon PSR Exit, DMCU can trigger
		 * DPHY_LOAD_BS_COUNT_START = 1. Upon writing 1 to
		 * DPHY_LOAD_BS_COUNT_START and the internal counter
		 * reaches DPHY_LOAD_BS_COUNT, the next BS symbol will be
		 * replaced by SR symbol once.
		 */

		REG_UPDATE(DP_DPHY_BS_SR_SWAP_CNTL, DPHY_LOAD_BS_COUNT, 0x5);
	}
}

void dcn10_psr_program_secondary_packet(struct link_encoder *enc,
			unsigned int sdp_transmit_line_num_deadline)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	REG_UPDATE_2(DP_SEC_CNTL1,
		DP_SEC_GSP0_LINE_NUM, sdp_transmit_line_num_deadline,
		DP_SEC_GSP0_PRIORITY, 1);
}

bool dcn10_is_dig_enabled(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	uint32_t value;

	REG_GET(DIG_BE_EN_CNTL, DIG_ENABLE, &value);
	return value;
}

static void link_encoder_disable(struct dcn10_link_encoder *enc10)
{
	/* reset training pattern */
	REG_SET(DP_DPHY_TRAINING_PATTERN_SEL, 0,
			DPHY_TRAINING_PATTERN_SEL, 0);

	/* reset training complete */
	REG_UPDATE(DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE, 0);

	/* reset panel mode */
	setup_panel_mode(enc10, DP_PANEL_MODE_DEFAULT);
}

static void hpd_initialize(
	struct dcn10_link_encoder *enc10)
{
	/* Associate HPD with DIG_BE */
	enum hpd_source_id hpd_source = enc10->base.hpd_source;

	REG_UPDATE(DIG_BE_CNTL, DIG_HPD_SELECT, hpd_source);
}

bool dcn10_link_encoder_validate_dvi_output(
	const struct dcn10_link_encoder *enc10,
	enum amd_signal_type connector_signal,
	enum amd_signal_type signal,
	const struct dc_crtc_timing *crtc_timing)
{
	uint32_t max_pixel_clock = TMDS_MAX_PIXEL_CLOCK;

	if (signal == SIGNAL_TYPE_DVI_DUAL_LINK)
		max_pixel_clock *= 2;

	/* This handles the case of HDMI downgrade to DVI we don't want to
	 * we don't want to cap the pixel clock if the DDI is not DVI.
	 */
	if (connector_signal != SIGNAL_TYPE_DVI_DUAL_LINK &&
			connector_signal != SIGNAL_TYPE_DVI_SINGLE_LINK)
		max_pixel_clock = enc10->base.features.max_hdmi_pixel_clock;

	/* DVI only support RGB pixel encoding */
	if (crtc_timing->pixel_encoding != PIXEL_ENCODING_RGB)
		return false;

	/*connect DVI via adpater's HDMI connector*/
	if ((connector_signal == SIGNAL_TYPE_DVI_SINGLE_LINK ||
		connector_signal == SIGNAL_TYPE_HDMI_TYPE_A) &&
		signal != SIGNAL_TYPE_HDMI_TYPE_A &&
		crtc_timing->pix_clk_100hz > (TMDS_MAX_PIXEL_CLOCK * 10))
		return false;
	if (crtc_timing->pix_clk_100hz < (TMDS_MIN_PIXEL_CLOCK * 10))
		return false;

	if (crtc_timing->pix_clk_100hz > (max_pixel_clock * 10))
		return false;

	/* DVI supports 6/8bpp single-link and 10/16bpp dual-link */
	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_666:
	case COLOR_DEPTH_888:
	break;
	case COLOR_DEPTH_101010:
	case COLOR_DEPTH_161616:
		if (signal != SIGNAL_TYPE_DVI_DUAL_LINK)
			return false;
	break;
	default:
		return false;
	}

	return true;
}

static bool dcn10_link_encoder_validate_hdmi_output(
	const struct dcn10_link_encoder *enc10,
	const struct dc_crtc_timing *crtc_timing,
	const struct dc_edid_caps *edid_caps,
	int adjusted_pix_clk_100hz)
{
	enum dc_color_depth max_deep_color =
			enc10->base.features.max_hdmi_deep_color;

	// check pixel clock against edid specified max TMDS clk
	if (edid_caps->max_tmds_clk_mhz != 0 &&
			adjusted_pix_clk_100hz > edid_caps->max_tmds_clk_mhz * 10000)
		return false;

	if (max_deep_color < crtc_timing->display_color_depth)
		return false;

	if (crtc_timing->display_color_depth < COLOR_DEPTH_888)
		return false;
	if (adjusted_pix_clk_100hz < (TMDS_MIN_PIXEL_CLOCK * 10))
		return false;

	if ((adjusted_pix_clk_100hz == 0) ||
		(adjusted_pix_clk_100hz > (enc10->base.features.max_hdmi_pixel_clock * 10)))
		return false;

	/* DCE11 HW does not support 420 */
	if (!enc10->base.features.hdmi_ycbcr420_supported &&
			crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		return false;

	if ((!enc10->base.features.flags.bits.HDMI_6GB_EN ||
			enc10->base.ctx->dc->debug.hdmi20_disable) &&
			adjusted_pix_clk_100hz >= 3000000)
		return false;
	if (enc10->base.ctx->dc->debug.hdmi20_disable &&
		crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		return false;
	return true;
}

bool dcn10_link_encoder_validate_dp_output(
	const struct dcn10_link_encoder *enc10,
	const struct dc_crtc_timing *crtc_timing)
{
	if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420) {
		if (!enc10->base.features.dp_ycbcr420_supported)
			return false;
	}

	return true;
}

void dcn10_link_encoder_construct(
	struct dcn10_link_encoder *enc10,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask)
{
	struct bp_encoder_cap_info bp_cap_info = {0};
	const struct dc_vbios_funcs *bp_funcs = init_data->ctx->dc_bios->funcs;
	enum bp_result result = BP_RESULT_OK;

	enc10->base.funcs = &dcn10_lnk_enc_funcs;
	enc10->base.ctx = init_data->ctx;
	enc10->base.id = init_data->encoder;

	enc10->base.hpd_source = init_data->hpd_source;
	enc10->base.connector = init_data->connector;

	enc10->base.preferred_engine = ENGINE_ID_UNKNOWN;

	enc10->base.features = *enc_features;

	enc10->base.transmitter = init_data->transmitter;

	/* set the flag to indicate whether driver poll the I2C data pin
	 * while doing the DP sink detect
	 */

/*	if (dal_adapter_service_is_feature_supported(as,
		FEATURE_DP_SINK_DETECT_POLL_DATA_PIN))
		enc10->base.features.flags.bits.
			DP_SINK_DETECT_POLL_DATA_PIN = true;*/

	enc10->base.output_signals =
		SIGNAL_TYPE_DVI_SINGLE_LINK |
		SIGNAL_TYPE_DVI_DUAL_LINK |
		SIGNAL_TYPE_LVDS |
		SIGNAL_TYPE_DISPLAY_PORT |
		SIGNAL_TYPE_DISPLAY_PORT_MST |
		SIGNAL_TYPE_EDP |
		SIGNAL_TYPE_HDMI_TYPE_A;

	/* For DCE 8.0 and 8.1, by design, UNIPHY is hardwired to DIG_BE.
	 * SW always assign DIG_FE 1:1 mapped to DIG_FE for non-MST UNIPHY.
	 * SW assign DIG_FE to non-MST UNIPHY first and MST last. So prefer
	 * DIG is per UNIPHY and used by SST DP, eDP, HDMI, DVI and LVDS.
	 * Prefer DIG assignment is decided by board design.
	 * For DCE 8.0, there are only max 6 UNIPHYs, we assume board design
	 * and VBIOS will filter out 7 UNIPHY for DCE 8.0.
	 * By this, adding DIGG should not hurt DCE 8.0.
	 * This will let DCE 8.1 share DCE 8.0 as much as possible
	 */

	enc10->link_regs = link_regs;
	enc10->aux_regs = aux_regs;
	enc10->hpd_regs = hpd_regs;
	enc10->link_shift = link_shift;
	enc10->link_mask = link_mask;

	switch (enc10->base.transmitter) {
	case TRANSMITTER_UNIPHY_A:
		enc10->base.preferred_engine = ENGINE_ID_DIGA;
	break;
	case TRANSMITTER_UNIPHY_B:
		enc10->base.preferred_engine = ENGINE_ID_DIGB;
	break;
	case TRANSMITTER_UNIPHY_C:
		enc10->base.preferred_engine = ENGINE_ID_DIGC;
	break;
	case TRANSMITTER_UNIPHY_D:
		enc10->base.preferred_engine = ENGINE_ID_DIGD;
	break;
	case TRANSMITTER_UNIPHY_E:
		enc10->base.preferred_engine = ENGINE_ID_DIGE;
	break;
	case TRANSMITTER_UNIPHY_F:
		enc10->base.preferred_engine = ENGINE_ID_DIGF;
	break;
	case TRANSMITTER_UNIPHY_G:
		enc10->base.preferred_engine = ENGINE_ID_DIGG;
	break;
	default:
		ASSERT_CRITICAL(false);
		enc10->base.preferred_engine = ENGINE_ID_UNKNOWN;
	}

	/* default to one to mirror Windows behavior */
	enc10->base.features.flags.bits.HDMI_6GB_EN = 1;

	result = bp_funcs->get_encoder_cap_info(enc10->base.ctx->dc_bios,
						enc10->base.id, &bp_cap_info);

	/* Override features with DCE-specific values */
	if (result == BP_RESULT_OK) {
		enc10->base.features.flags.bits.IS_HBR2_CAPABLE =
				bp_cap_info.DP_HBR2_EN;
		enc10->base.features.flags.bits.IS_HBR3_CAPABLE =
				bp_cap_info.DP_HBR3_EN;
		enc10->base.features.flags.bits.HDMI_6GB_EN = bp_cap_info.HDMI_6GB_EN;
		enc10->base.features.flags.bits.DP_IS_USB_C =
				bp_cap_info.DP_IS_USB_C;
	} else {
		DC_LOG_WARNING("%s: Failed to get encoder_cap_info from VBIOS with error code %d!\n",
				__func__,
				result);
	}
	if (enc10->base.ctx->dc->debug.hdmi20_disable) {
		enc10->base.features.flags.bits.HDMI_6GB_EN = 0;
	}
}

bool dcn10_link_encoder_validate_output_with_stream(
	struct link_encoder *enc,
	const struct dc_stream_state *stream)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	bool is_valid;

	//if SCDC (340-600MHz) is disabled, set to HDMI 1.4 timing limit
	if (stream->sink->edid_caps.panel_patch.skip_scdc_overwrite &&
		enc10->base.features.max_hdmi_pixel_clock > 300000)
		enc10->base.features.max_hdmi_pixel_clock = 300000;

	switch (stream->signal) {
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		is_valid = dcn10_link_encoder_validate_dvi_output(
			enc10,
			stream->link->connector_signal,
			stream->signal,
			&stream->timing);
	break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		is_valid = dcn10_link_encoder_validate_hdmi_output(
				enc10,
				&stream->timing,
				&stream->sink->edid_caps,
				stream->phy_pix_clk * 10);
	break;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		is_valid = dcn10_link_encoder_validate_dp_output(
					enc10, &stream->timing);
	break;
	case SIGNAL_TYPE_EDP:
		is_valid = (stream->timing.pixel_encoding == PIXEL_ENCODING_RGB) ? true : false;
	break;
	case SIGNAL_TYPE_VIRTUAL:
		is_valid = true;
		break;
	default:
		is_valid = false;
	break;
	}

	return is_valid;
}

void dcn10_link_encoder_hw_init(
	struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	cntl.action = TRANSMITTER_CONTROL_INIT;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc10->base.transmitter;
	cntl.connector_obj_id = enc10->base.connector;
	cntl.lanes_number = LANE_COUNT_FOUR;
	cntl.coherent = false;
	cntl.hpd_sel = enc10->base.hpd_source;

	if (enc10->base.connector.id == CONNECTOR_ID_EDP)
		cntl.signal = SIGNAL_TYPE_EDP;

	result = link_transmitter_control(enc10, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}

	if (enc10->base.connector.id == CONNECTOR_ID_LVDS) {
		cntl.action = TRANSMITTER_CONTROL_BACKLIGHT_BRIGHTNESS;

		result = link_transmitter_control(enc10, &cntl);

		ASSERT(result == BP_RESULT_OK);

	}
	dcn10_aux_initialize(enc10);

	/* reinitialize HPD.
	 * hpd_initialize() will pass DIG_FE id to HW context.
	 * All other routine within HW context will use fe_engine_offset
	 * as DIG_FE id even caller pass DIG_FE id.
	 * So this routine must be called first.
	 */
	hpd_initialize(enc10);
}

void dcn10_link_encoder_destroy(struct link_encoder **enc)
{
	kfree(TO_DCN10_LINK_ENC(*enc));
	*enc = NULL;
}

void dcn10_link_encoder_setup(
	struct link_encoder *enc,
	enum amd_signal_type signal)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	switch (signal) {
	case SIGNAL_TYPE_EDP:
	case SIGNAL_TYPE_DISPLAY_PORT:
		/* DP SST */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 0);
		break;
	case SIGNAL_TYPE_LVDS:
		/* LVDS */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 1);
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		/* TMDS-DVI */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 2);
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		/* TMDS-HDMI */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 3);
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		/* DP MST */
		REG_UPDATE(DIG_BE_CNTL, DIG_MODE, 5);
		break;
	default:
		ASSERT_CRITICAL(false);
		/* invalid mode ! */
		break;
	}

}

/* TODO: still need depth or just pass in adjusted pixel clock? */
void dcn10_link_encoder_enable_tmds_output(
	struct link_encoder *enc,
	enum clock_source_id clock_source,
	enum dc_color_depth color_depth,
	enum amd_signal_type signal,
	uint32_t pixel_clock)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = enc->preferred_engine;
	cntl.transmitter = enc10->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = signal;
	if (cntl.signal == SIGNAL_TYPE_DVI_DUAL_LINK)
		cntl.lanes_number = 8;
	else
		cntl.lanes_number = 4;

	cntl.hpd_sel = enc10->base.hpd_source;

	cntl.pixel_clock = pixel_clock;
	cntl.color_depth = color_depth;

	result = link_transmitter_control(enc10, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}

void dcn10_link_encoder_enable_tmds_output_with_clk_pattern_wa(
	struct link_encoder *enc,
	enum clock_source_id clock_source,
	enum dc_color_depth color_depth,
	enum amd_signal_type signal,
	uint32_t pixel_clock)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	dcn10_link_encoder_enable_tmds_output(
		enc, clock_source, color_depth, signal, pixel_clock);

	REG_UPDATE(DIG_CLOCK_PATTERN, DIG_CLOCK_PATTERN, 0x1F);
}

/* enables DP PHY output */
void dcn10_link_encoder_enable_dp_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	/* number_of_lanes is used for pixel clock adjust,
	 * but it's not passed to asic_control.
	 * We need to set number of lanes manually.
	 */
	enc1_configure_encoder(enc10, link_settings);

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = enc->preferred_engine;
	cntl.transmitter = enc10->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc10->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate
						* LINK_RATE_REF_FREQ_IN_KHZ;
	/* TODO: check if undefined works */
	cntl.color_depth = COLOR_DEPTH_UNDEFINED;

	result = link_transmitter_control(enc10, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}

/* enables DP PHY output in MST mode */
void dcn10_link_encoder_enable_dp_mst_output(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	enum clock_source_id clock_source)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	/* Enable the PHY */

	/* number_of_lanes is used for pixel clock adjust,
	 * but it's not passed to asic_control.
	 * We need to set number of lanes manually.
	 */
	enc1_configure_encoder(enc10, link_settings);

	cntl.action = TRANSMITTER_CONTROL_ENABLE;
	cntl.engine_id = ENGINE_ID_UNKNOWN;
	cntl.transmitter = enc10->base.transmitter;
	cntl.pll_id = clock_source;
	cntl.signal = SIGNAL_TYPE_DISPLAY_PORT_MST;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc10->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate
						* LINK_RATE_REF_FREQ_IN_KHZ;
	/* TODO: check if undefined works */
	cntl.color_depth = COLOR_DEPTH_UNDEFINED;

	result = link_transmitter_control(enc10, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
	}
}
/*
 * @brief
 * Disable transmitter and its encoder
 */
void dcn10_link_encoder_disable_output(
	struct link_encoder *enc,
	enum amd_signal_type signal)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	if (enc->funcs->is_dig_enabled && !enc->funcs->is_dig_enabled(enc)) {
		/* OF_SKIP_POWER_DOWN_INACTIVE_ENCODER */
	/*in DP_Alt_No_Connect case, we turn off the dig already,
	after excuation the PHY w/a sequence, not allow touch PHY any more*/
		return;
	}
	/* Power-down RX and disable GPU PHY should be paired.
	 * Disabling PHY without powering down RX may cause
	 * symbol lock loss, on which we will get DP Sink interrupt.
	 */

	/* There is a case for the DP active dongles
	 * where we want to disable the PHY but keep RX powered,
	 * for those we need to ignore DP Sink interrupt
	 * by checking lane count that has been set
	 * on the last do_enable_output().
	 */

	/* disable transmitter */
	cntl.action = TRANSMITTER_CONTROL_DISABLE;
	cntl.transmitter = enc10->base.transmitter;
	cntl.hpd_sel = enc10->base.hpd_source;
	cntl.signal = signal;
	cntl.connector_obj_id = enc10->base.connector;

	result = link_transmitter_control(enc10, &cntl);

	if (result != BP_RESULT_OK) {
		DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n",
			__func__);
		BREAK_TO_DEBUGGER();
		return;
	}

	/* disable encoder */
	if (dc_is_dp_signal(signal))
		link_encoder_disable(enc10);
}

void dcn10_link_encoder_dp_set_lane_settings(
	struct link_encoder *enc,
	const struct dc_link_settings *link_settings,
	const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	union dpcd_training_lane_set training_lane_set = { { 0 } };
	int32_t lane = 0;
	struct bp_transmitter_control cntl = { 0 };
	enum bp_result result;

	if (!link_settings) {
		BREAK_TO_DEBUGGER();
		return;
	}

	cntl.action = TRANSMITTER_CONTROL_SET_VOLTAGE_AND_PREEMPASIS;
	cntl.transmitter = enc10->base.transmitter;
	cntl.connector_obj_id = enc10->base.connector;
	cntl.lanes_number = link_settings->lane_count;
	cntl.hpd_sel = enc10->base.hpd_source;
	cntl.pixel_clock = link_settings->link_rate * LINK_RATE_REF_FREQ_IN_KHZ;

	for (lane = 0; lane < link_settings->lane_count; lane++) {
		/* translate lane settings */

		training_lane_set.bits.VOLTAGE_SWING_SET =
				lane_settings[lane].VOLTAGE_SWING;
		training_lane_set.bits.PRE_EMPHASIS_SET =
				lane_settings[lane].PRE_EMPHASIS;

		/* post cursor 2 setting only applies to HBR2 link rate */
		if (link_settings->link_rate == LINK_RATE_HIGH2) {
			/* this is passed to VBIOS
			 * to program post cursor 2 level
			 */
			training_lane_set.bits.POST_CURSOR2_SET =
					lane_settings[lane].POST_CURSOR2;
		}

		cntl.lane_select = lane;
		cntl.lane_settings = training_lane_set.raw;

		/* call VBIOS table to set voltage swing and pre-emphasis */
		result = link_transmitter_control(enc10, &cntl);

		if (result != BP_RESULT_OK) {
			DC_LOG_ERROR("%s: Failed to execute VBIOS command table!\n", __func__);
			BREAK_TO_DEBUGGER();
		}
	}
}

/* set DP PHY test and training patterns */
void dcn10_link_encoder_dp_set_phy_pattern(
	struct link_encoder *enc,
	const struct encoder_set_dp_phy_pattern_param *param)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	switch (param->dp_phy_pattern) {
	case DP_TEST_PATTERN_TRAINING_PATTERN1:
		dcn10_link_encoder_set_dp_phy_pattern_training_pattern(enc, 0);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN2:
		dcn10_link_encoder_set_dp_phy_pattern_training_pattern(enc, 1);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN3:
		dcn10_link_encoder_set_dp_phy_pattern_training_pattern(enc, 2);
		break;
	case DP_TEST_PATTERN_TRAINING_PATTERN4:
		dcn10_link_encoder_set_dp_phy_pattern_training_pattern(enc, 3);
		break;
	case DP_TEST_PATTERN_D102:
		set_dp_phy_pattern_d102(enc10);
		break;
	case DP_TEST_PATTERN_SYMBOL_ERROR:
		set_dp_phy_pattern_symbol_error(enc10);
		break;
	case DP_TEST_PATTERN_PRBS7:
		set_dp_phy_pattern_prbs7(enc10);
		break;
	case DP_TEST_PATTERN_80BIT_CUSTOM:
		set_dp_phy_pattern_80bit_custom(
			enc10, param->custom_pattern);
		break;
	case DP_TEST_PATTERN_CP2520_1:
		set_dp_phy_pattern_hbr2_compliance_cp2520_2(enc10, 1);
		break;
	case DP_TEST_PATTERN_CP2520_2:
		set_dp_phy_pattern_hbr2_compliance_cp2520_2(enc10, 2);
		break;
	case DP_TEST_PATTERN_CP2520_3:
		set_dp_phy_pattern_hbr2_compliance_cp2520_2(enc10, 3);
		break;
	case DP_TEST_PATTERN_VIDEO_MODE: {
		set_dp_phy_pattern_passthrough_mode(
			enc10, param->dp_panel_mode);
		break;
	}

	default:
		/* invalid phy pattern */
		ASSERT_CRITICAL(false);
		break;
	}
}

static void fill_stream_allocation_row_info(
	const struct link_mst_stream_allocation *stream_allocation,
	uint32_t *src,
	uint32_t *slots)
{
	const struct stream_encoder *stream_enc = stream_allocation->stream_enc;

	if (stream_enc) {
		*src = stream_enc->id;
		*slots = stream_allocation->slot_count;
	} else {
		*src = 0;
		*slots = 0;
	}
}

/* programs DP MST VC payload allocation */
void dcn10_link_encoder_update_mst_stream_allocation_table(
	struct link_encoder *enc,
	const struct link_mst_stream_allocation_table *table)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	uint32_t value1 = 0;
	uint32_t value2 = 0;
	uint32_t slots = 0;
	uint32_t src = 0;
	uint32_t retries = 0;

	/* For CZ, there are only 3 pipes. So Virtual channel is up 3.*/

	/* --- Set MSE Stream Attribute -
	 * Setup VC Payload Table on Tx Side,
	 * Issue allocation change trigger
	 * to commit payload on both tx and rx side
	 */

	/* we should clean-up table each time */

	if (table->stream_count >= 1) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[0],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT0,
			DP_MSE_SAT_SRC0, src,
			DP_MSE_SAT_SLOT_COUNT0, slots);

	if (table->stream_count >= 2) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[1],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT0,
			DP_MSE_SAT_SRC1, src,
			DP_MSE_SAT_SLOT_COUNT1, slots);

	if (table->stream_count >= 3) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[2],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT1,
			DP_MSE_SAT_SRC2, src,
			DP_MSE_SAT_SLOT_COUNT2, slots);

	if (table->stream_count >= 4) {
		fill_stream_allocation_row_info(
			&table->stream_allocations[3],
			&src,
			&slots);
	} else {
		src = 0;
		slots = 0;
	}

	REG_UPDATE_2(DP_MSE_SAT1,
			DP_MSE_SAT_SRC3, src,
			DP_MSE_SAT_SLOT_COUNT3, slots);

	/* --- wait for transaction finish */

	/* send allocation change trigger (ACT) ?
	 * this step first sends the ACT,
	 * then double buffers the SAT into the hardware
	 * making the new allocation active on the DP MST mode link
	 */

	/* DP_MSE_SAT_UPDATE:
	 * 0 - No Action
	 * 1 - Update SAT with trigger
	 * 2 - Update SAT without trigger
	 */
	REG_UPDATE(DP_MSE_SAT_UPDATE,
			DP_MSE_SAT_UPDATE, 1);

	/* wait for update to complete
	 * (i.e. DP_MSE_SAT_UPDATE field is reset to 0)
	 * then wait for the transmission
	 * of at least 16 MTP headers on immediate local link.
	 * i.e. DP_MSE_16_MTP_KEEPOUT field (read only) is reset to 0
	 * a value of 1 indicates that DP MST mode
	 * is in the 16 MTP keepout region after a VC has been added.
	 * MST stream bandwidth (VC rate) can be configured
	 * after this bit is cleared
	 */
	do {
		udelay(10);

		REG_READ(DP_MSE_SAT_UPDATE);

		REG_GET(DP_MSE_SAT_UPDATE,
				DP_MSE_SAT_UPDATE, &value1);

		REG_GET(DP_MSE_SAT_UPDATE,
				DP_MSE_16_MTP_KEEPOUT, &value2);

		/* bit field DP_MSE_SAT_UPDATE is set to 1 already */
		if (!value1 && !value2)
			break;
		++retries;
	} while (retries < DP_MST_UPDATE_MAX_RETRY);
}

void dcn10_link_encoder_connect_dig_be_to_fe(
	struct link_encoder *enc,
	enum engine_id engine,
	bool connect)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	uint32_t field;

	if (engine != ENGINE_ID_UNKNOWN) {

		REG_GET(DIG_BE_CNTL, DIG_FE_SOURCE_SELECT, &field);

		if (connect)
			field |= get_frontend_source(engine);
		else
			field &= ~get_frontend_source(engine);

		REG_UPDATE(DIG_BE_CNTL, DIG_FE_SOURCE_SELECT, field);
	}
}


#define HPD_REG(reg)\
	(enc10->hpd_regs->reg)

#define HPD_REG_READ(reg_name) \
		dm_read_reg(CTX, HPD_REG(reg_name))

#define HPD_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_ex(CTX, \
				HPD_REG(reg_name), \
				n, __VA_ARGS__)

#define HPD_REG_UPDATE(reg_name, field, val)	\
		HPD_REG_UPDATE_N(reg_name, 1, \
				FN(reg_name, field), val)

void dcn10_link_encoder_enable_hpd(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	HPD_REG_UPDATE(DC_HPD_CONTROL,
			DC_HPD_EN, 1);
}

void dcn10_link_encoder_disable_hpd(struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);

	HPD_REG_UPDATE(DC_HPD_CONTROL,
			DC_HPD_EN, 0);
}

#define AUX_REG(reg)\
	(enc10->aux_regs->reg)

#define AUX_REG_READ(reg_name) \
		dm_read_reg(CTX, AUX_REG(reg_name))

#define AUX_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_ex(CTX, \
				AUX_REG(reg_name), \
				n, __VA_ARGS__)

#define AUX_REG_UPDATE(reg_name, field, val)	\
		AUX_REG_UPDATE_N(reg_name, 1, \
				FN(reg_name, field), val)

#define AUX_REG_UPDATE_2(reg, f1, v1, f2, v2)	\
		AUX_REG_UPDATE_N(reg, 2,\
				FN(reg, f1), v1,\
				FN(reg, f2), v2)

void dcn10_aux_initialize(struct dcn10_link_encoder *enc10)
{
	enum hpd_source_id hpd_source = enc10->base.hpd_source;

	AUX_REG_UPDATE_2(AUX_CONTROL,
			AUX_HPD_SEL, hpd_source,
			AUX_LS_READ_EN, 0);

	/* 1/4 window (the maximum allowed) */
	AUX_REG_UPDATE(AUX_DPHY_RX_CONTROL0,
			AUX_RX_RECEIVE_WINDOW, 0);
}

enum amd_signal_type dcn10_get_dig_mode(
	struct link_encoder *enc)
{
	struct dcn10_link_encoder *enc10 = TO_DCN10_LINK_ENC(enc);
	uint32_t value;
	REG_GET(DIG_BE_CNTL, DIG_MODE, &value);
	switch (value) {
	case 1:
		return SIGNAL_TYPE_DISPLAY_PORT;
	case 2:
		return SIGNAL_TYPE_DVI_SINGLE_LINK;
	case 3:
		return SIGNAL_TYPE_HDMI_TYPE_A;
	case 5:
		return SIGNAL_TYPE_DISPLAY_PORT_MST;
	default:
		return SIGNAL_TYPE_NONE;
	}
}

void dcn10_link_encoder_get_max_link_cap(struct link_encoder *enc,
	struct dc_link_settings *link_settings)
{
	/* Set Default link settings */
	struct dc_link_settings max_link_cap = {LANE_COUNT_FOUR, LINK_RATE_HIGH,
			LINK_SPREAD_05_DOWNSPREAD_30KHZ, false, 0};

	/* Higher link settings based on feature supported */
	if (enc->features.flags.bits.IS_HBR2_CAPABLE)
		max_link_cap.link_rate = LINK_RATE_HIGH2;

	if (enc->features.flags.bits.IS_HBR3_CAPABLE)
		max_link_cap.link_rate = LINK_RATE_HIGH3;

	if (enc->features.flags.bits.IS_UHBR10_CAPABLE)
		max_link_cap.link_rate = LINK_RATE_UHBR10;

	if (enc->features.flags.bits.IS_UHBR13_5_CAPABLE)
		max_link_cap.link_rate = LINK_RATE_UHBR13_5;

	if (enc->features.flags.bits.IS_UHBR20_CAPABLE)
		max_link_cap.link_rate = LINK_RATE_UHBR20;

	*link_settings = max_link_cap;
}
