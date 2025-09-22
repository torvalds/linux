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

#include "dm_services.h"

#include "atom.h"

#include "include/grph_object_id.h"
#include "include/grph_object_defs.h"
#include "include/bios_parser_types.h"

#include "../command_table_helper.h"

static uint8_t encoder_action_to_atom(enum bp_encoder_control_action action)
{
	uint8_t atom_action = 0;

	switch (action) {
	case ENCODER_CONTROL_ENABLE:
		atom_action = ATOM_ENABLE;
		break;
	case ENCODER_CONTROL_DISABLE:
		atom_action = ATOM_DISABLE;
		break;
	case ENCODER_CONTROL_SETUP:
		atom_action = ATOM_ENCODER_CMD_SETUP;
		break;
	case ENCODER_CONTROL_INIT:
		atom_action = ATOM_ENCODER_INIT;
		break;
	default:
		BREAK_TO_DEBUGGER(); /* Unhandle action in driver.!! */
		break;
	}

	return atom_action;
}

static bool engine_bp_to_atom(enum engine_id id, uint32_t *atom_engine_id)
{
	bool result = false;

	if (atom_engine_id != NULL)
		switch (id) {
		case ENGINE_ID_DIGA:
			*atom_engine_id = ASIC_INT_DIG1_ENCODER_ID;
			result = true;
			break;
		case ENGINE_ID_DIGB:
			*atom_engine_id = ASIC_INT_DIG2_ENCODER_ID;
			result = true;
			break;
		case ENGINE_ID_DIGC:
			*atom_engine_id = ASIC_INT_DIG3_ENCODER_ID;
			result = true;
			break;
		case ENGINE_ID_DIGD:
			*atom_engine_id = ASIC_INT_DIG4_ENCODER_ID;
			result = true;
			break;
		case ENGINE_ID_DIGE:
			*atom_engine_id = ASIC_INT_DIG5_ENCODER_ID;
			result = true;
			break;
		case ENGINE_ID_DIGF:
			*atom_engine_id = ASIC_INT_DIG6_ENCODER_ID;
			result = true;
			break;
		case ENGINE_ID_DIGG:
			*atom_engine_id = ASIC_INT_DIG7_ENCODER_ID;
			result = true;
			break;
		case ENGINE_ID_DACA:
			*atom_engine_id = ASIC_INT_DAC1_ENCODER_ID;
			result = true;
			break;
		default:
			break;
		}

	return result;
}

static bool clock_source_id_to_atom(
	enum clock_source_id id,
	uint32_t *atom_pll_id)
{
	bool result = true;

	if (atom_pll_id != NULL)
		switch (id) {
		case CLOCK_SOURCE_ID_PLL0:
			*atom_pll_id = ATOM_PPLL0;
			break;
		case CLOCK_SOURCE_ID_PLL1:
			*atom_pll_id = ATOM_PPLL1;
			break;
		case CLOCK_SOURCE_ID_PLL2:
			*atom_pll_id = ATOM_PPLL2;
			break;
		case CLOCK_SOURCE_ID_EXTERNAL:
			*atom_pll_id = ATOM_PPLL_INVALID;
			break;
		case CLOCK_SOURCE_ID_DFS:
			*atom_pll_id = ATOM_EXT_PLL1;
			break;
		case CLOCK_SOURCE_ID_VCE:
			/* for VCE encoding,
			 * we need to pass in ATOM_PPLL_INVALID
			 */
			*atom_pll_id = ATOM_PPLL_INVALID;
			break;
		case CLOCK_SOURCE_ID_DP_DTO:
			/* When programming DP DTO PLL ID should be invalid */
			*atom_pll_id = ATOM_PPLL_INVALID;
			break;
		case CLOCK_SOURCE_ID_UNDEFINED:
			BREAK_TO_DEBUGGER(); /* check when this will happen! */
			*atom_pll_id = ATOM_PPLL_INVALID;
			result = false;
			break;
		default:
			result = false;
			break;
		}

	return result;
}

static uint8_t clock_source_id_to_atom_phy_clk_src_id(
		enum clock_source_id id)
{
	uint8_t atom_phy_clk_src_id = 0;

	switch (id) {
	case CLOCK_SOURCE_ID_PLL0:
		atom_phy_clk_src_id = ATOM_TRANSMITTER_CONFIG_V5_P0PLL;
		break;
	case CLOCK_SOURCE_ID_PLL1:
		atom_phy_clk_src_id = ATOM_TRANSMITTER_CONFIG_V5_P1PLL;
		break;
	case CLOCK_SOURCE_ID_PLL2:
		atom_phy_clk_src_id = ATOM_TRANSMITTER_CONFIG_V5_P2PLL;
		break;
	case CLOCK_SOURCE_ID_EXTERNAL:
		atom_phy_clk_src_id = ATOM_TRANSMITTER_CONFIG_V5_REFCLK_SRC_EXT;
		break;
	default:
		atom_phy_clk_src_id = ATOM_TRANSMITTER_CONFIG_V5_P1PLL;
		break;
	}

	return atom_phy_clk_src_id >> 2;
}

static uint8_t signal_type_to_atom_dig_mode(enum amd_signal_type s)
{
	uint8_t atom_dig_mode = ATOM_TRANSMITTER_DIGMODE_V5_DP;

	switch (s) {
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_EDP:
		atom_dig_mode = ATOM_TRANSMITTER_DIGMODE_V5_DP;
		break;
	case SIGNAL_TYPE_LVDS:
		atom_dig_mode = ATOM_TRANSMITTER_DIGMODE_V5_LVDS;
		break;
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
	case SIGNAL_TYPE_DVI_DUAL_LINK:
		atom_dig_mode = ATOM_TRANSMITTER_DIGMODE_V5_DVI;
		break;
	case SIGNAL_TYPE_HDMI_TYPE_A:
		atom_dig_mode = ATOM_TRANSMITTER_DIGMODE_V5_HDMI;
		break;
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		atom_dig_mode = ATOM_TRANSMITTER_DIGMODE_V5_DP_MST;
		break;
	default:
		atom_dig_mode = ATOM_TRANSMITTER_DIGMODE_V5_DVI;
		break;
	}

	return atom_dig_mode;
}

static uint8_t hpd_sel_to_atom(enum hpd_source_id id)
{
	uint8_t atom_hpd_sel = 0;

	switch (id) {
	case HPD_SOURCEID1:
		atom_hpd_sel = ATOM_TRANSMITTER_CONFIG_V5_HPD1_SEL;
		break;
	case HPD_SOURCEID2:
		atom_hpd_sel = ATOM_TRANSMITTER_CONFIG_V5_HPD2_SEL;
		break;
	case HPD_SOURCEID3:
		atom_hpd_sel = ATOM_TRANSMITTER_CONFIG_V5_HPD3_SEL;
		break;
	case HPD_SOURCEID4:
		atom_hpd_sel = ATOM_TRANSMITTER_CONFIG_V5_HPD4_SEL;
		break;
	case HPD_SOURCEID5:
		atom_hpd_sel = ATOM_TRANSMITTER_CONFIG_V5_HPD5_SEL;
		break;
	case HPD_SOURCEID6:
		atom_hpd_sel = ATOM_TRANSMITTER_CONFIG_V5_HPD6_SEL;
		break;
	case HPD_SOURCEID_UNKNOWN:
	default:
		atom_hpd_sel = 0;
		break;
	}
	return atom_hpd_sel >> 4;
}

static uint8_t dig_encoder_sel_to_atom(enum engine_id id)
{
	uint8_t atom_dig_encoder_sel = 0;

	switch (id) {
	case ENGINE_ID_DIGA:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGA_SEL;
		break;
	case ENGINE_ID_DIGB:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGB_SEL;
		break;
	case ENGINE_ID_DIGC:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGC_SEL;
		break;
	case ENGINE_ID_DIGD:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGD_SEL;
		break;
	case ENGINE_ID_DIGE:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGE_SEL;
		break;
	case ENGINE_ID_DIGF:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGF_SEL;
		break;
	case ENGINE_ID_DIGG:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGG_SEL;
		break;
	default:
		atom_dig_encoder_sel = ATOM_TRANMSITTER_V5__DIGA_SEL;
		break;
	}

	return atom_dig_encoder_sel;
}

static uint8_t phy_id_to_atom(enum transmitter t)
{
	uint8_t atom_phy_id;

	switch (t) {
	case TRANSMITTER_UNIPHY_A:
		atom_phy_id = ATOM_PHY_ID_UNIPHYA;
		break;
	case TRANSMITTER_UNIPHY_B:
		atom_phy_id = ATOM_PHY_ID_UNIPHYB;
		break;
	case TRANSMITTER_UNIPHY_C:
		atom_phy_id = ATOM_PHY_ID_UNIPHYC;
		break;
	case TRANSMITTER_UNIPHY_D:
		atom_phy_id = ATOM_PHY_ID_UNIPHYD;
		break;
	case TRANSMITTER_UNIPHY_E:
		atom_phy_id = ATOM_PHY_ID_UNIPHYE;
		break;
	case TRANSMITTER_UNIPHY_F:
		atom_phy_id = ATOM_PHY_ID_UNIPHYF;
		break;
	case TRANSMITTER_UNIPHY_G:
		atom_phy_id = ATOM_PHY_ID_UNIPHYG;
		break;
	default:
		atom_phy_id = ATOM_PHY_ID_UNIPHYA;
		break;
	}
	return atom_phy_id;
}

static uint8_t disp_power_gating_action_to_atom(
	enum bp_pipe_control_action action)
{
	uint8_t atom_pipe_action = 0;

	switch (action) {
	case ASIC_PIPE_DISABLE:
		atom_pipe_action = ATOM_DISABLE;
		break;
	case ASIC_PIPE_ENABLE:
		atom_pipe_action = ATOM_ENABLE;
		break;
	case ASIC_PIPE_INIT:
		atom_pipe_action = ATOM_INIT;
		break;
	default:
		BREAK_TO_DEBUGGER(); /* Unhandle action in driver! */
		break;
	}

	return atom_pipe_action;
}

static const struct command_table_helper command_table_helper_funcs = {
	.controller_id_to_atom = dal_cmd_table_helper_controller_id_to_atom,
	.encoder_action_to_atom = encoder_action_to_atom,
	.engine_bp_to_atom = engine_bp_to_atom,
	.clock_source_id_to_atom = clock_source_id_to_atom,
	.clock_source_id_to_atom_phy_clk_src_id =
		clock_source_id_to_atom_phy_clk_src_id,
	.signal_type_to_atom_dig_mode = signal_type_to_atom_dig_mode,
	.hpd_sel_to_atom = hpd_sel_to_atom,
	.dig_encoder_sel_to_atom = dig_encoder_sel_to_atom,
	.phy_id_to_atom = phy_id_to_atom,
	.disp_power_gating_action_to_atom = disp_power_gating_action_to_atom,
	.assign_control_parameter =
		dal_cmd_table_helper_assign_control_parameter,
	.clock_source_id_to_ref_clk_src =
		dal_cmd_table_helper_clock_source_id_to_ref_clk_src,
	.transmitter_bp_to_atom = dal_cmd_table_helper_transmitter_bp_to_atom,
	.encoder_id_to_atom = dal_cmd_table_helper_encoder_id_to_atom,
	.encoder_mode_bp_to_atom =
		dal_cmd_table_helper_encoder_mode_bp_to_atom,
};

const struct command_table_helper *dal_cmd_tbl_helper_dce80_get_table(void)
{
	return &command_table_helper_funcs;
}
