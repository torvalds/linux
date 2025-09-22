/* SPDX-License-Identifier: MIT */
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

#ifndef __DC_LINK_ENCODER__DCN35_H__
#define __DC_LINK_ENCODER__DCN35_H__

#include "dcn32/dcn32_dio_link_encoder.h"
#include "dcn30/dcn30_dio_link_encoder.h"
#include "dcn31/dcn31_dio_link_encoder.h"

#define LINK_ENCODER_MASK_SH_LIST_DCN35(mask_sh) \
	LE_SF(DIG0_DIG_BE_EN_CNTL, DIG_BE_ENABLE, mask_sh),\
	LE_SF(DIG0_DIG_BE_CNTL, DIG_RB_SWITCH_EN, mask_sh),\
	LE_SF(DIG0_DIG_BE_CNTL, DIG_HPD_SELECT, mask_sh),\
	LE_SF(DIG0_DIG_BE_CNTL, DIG_FE_SOURCE_SELECT, mask_sh),\
	LE_SF(DIG0_DIG_BE_CLK_CNTL, DIG_BE_MODE, mask_sh),\
	LE_SF(DIG0_DIG_BE_CLK_CNTL, DIG_BE_CLK_EN, mask_sh),\
	LE_SF(DIG0_DIG_BE_CLK_CNTL, DIG_BE_SOFT_RESET, mask_sh),\
	LE_SF(DIG0_DIG_BE_CLK_CNTL, HDCP_SOFT_RESET, mask_sh),\
	LE_SF(DIG0_DIG_BE_CLK_CNTL, DIG_BE_SYMCLK_G_CLOCK_ON, mask_sh),\
	LE_SF(DIG0_DIG_BE_CLK_CNTL, DIG_BE_SYMCLK_G_HDCP_CLOCK_ON, mask_sh),\
	LE_SF(DIG0_DIG_BE_CLK_CNTL, DIG_BE_SYMCLK_G_TMDS_CLOCK_ON, mask_sh),\
	LE_SF(DIG0_DIG_CLOCK_PATTERN, DIG_CLOCK_PATTERN, mask_sh),\
	LE_SF(DIG0_TMDS_CTL_BITS, TMDS_CTL0, mask_sh), \
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_BYPASS, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE0, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE1, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE2, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_ATEST_SEL_LANE3, mask_sh),\
	LE_SF(DP0_DP_DPHY_PRBS_CNTL, DPHY_PRBS_EN, mask_sh),\
	LE_SF(DP0_DP_DPHY_PRBS_CNTL, DPHY_PRBS_SEL, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM0, DPHY_SYM1, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM0, DPHY_SYM2, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM0, DPHY_SYM3, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM1, DPHY_SYM4, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM1, DPHY_SYM5, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM1, DPHY_SYM6, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM2, DPHY_SYM7, mask_sh),\
	LE_SF(DP0_DP_DPHY_SYM2, DPHY_SYM8, mask_sh),\
	LE_SF(DP0_DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_BS_COUNT, mask_sh),\
	LE_SF(DP0_DP_DPHY_SCRAM_CNTL, DPHY_SCRAMBLER_ADVANCE, mask_sh),\
	LE_SF(DP0_DP_DPHY_FAST_TRAINING, DPHY_RX_FAST_TRAINING_CAPABLE, mask_sh),\
	LE_SF(DP0_DP_DPHY_BS_SR_SWAP_CNTL, DPHY_LOAD_BS_COUNT, mask_sh),\
	LE_SF(DP0_DP_DPHY_TRAINING_PATTERN_SEL, DPHY_TRAINING_PATTERN_SEL, mask_sh),\
	LE_SF(DP0_DP_DPHY_HBR2_PATTERN_CONTROL, DP_DPHY_HBR2_PATTERN_CONTROL, mask_sh),\
	LE_SF(DP0_DP_LINK_CNTL, DP_LINK_TRAINING_COMPLETE, mask_sh),\
	LE_SF(DP0_DP_LINK_FRAMING_CNTL, DP_IDLE_BS_INTERVAL, mask_sh),\
	LE_SF(DP0_DP_LINK_FRAMING_CNTL, DP_VBID_DISABLE, mask_sh),\
	LE_SF(DP0_DP_LINK_FRAMING_CNTL, DP_VID_ENHANCED_FRAME_MODE, mask_sh),\
	LE_SF(DP0_DP_VID_STREAM_CNTL, DP_VID_STREAM_ENABLE, mask_sh),\
	LE_SF(DP0_DP_CONFIG, DP_UDI_LANES, mask_sh),\
	LE_SF(DP0_DP_SEC_CNTL1, DP_SEC_GSP0_LINE_NUM, mask_sh),\
	LE_SF(DP0_DP_SEC_CNTL1, DP_SEC_GSP0_PRIORITY, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SRC0, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SRC1, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SLOT_COUNT0, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT0, DP_MSE_SAT_SLOT_COUNT1, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SRC2, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SRC3, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SLOT_COUNT2, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT1, DP_MSE_SAT_SLOT_COUNT3, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT_UPDATE, DP_MSE_SAT_UPDATE, mask_sh),\
	LE_SF(DP0_DP_MSE_SAT_UPDATE, DP_MSE_16_MTP_KEEPOUT, mask_sh),\
	LE_SF(DP_AUX0_AUX_CONTROL, AUX_HPD_SEL, mask_sh),\
	LE_SF(DP_AUX0_AUX_CONTROL, AUX_LS_READ_EN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_RECEIVE_WINDOW, mask_sh),\
	LE_SF(HPD0_DC_HPD_CONTROL, DC_HPD_EN, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_FEC_EN, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_FEC_READY_SHADOW, mask_sh),\
	LE_SF(DP0_DP_DPHY_CNTL, DPHY_FEC_ACTIVE_STATUS, mask_sh),\
	LE_SF(DIG0_TMDS_CTL_BITS, TMDS_CTL0, mask_sh), \
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_START_WINDOW, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_HALF_SYM_DETECT_LEN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_TRANSITION_FILTER_EN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_ALLOW_BELOW_THRESHOLD_PHASE_DETECT, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_ALLOW_BELOW_THRESHOLD_START, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_ALLOW_BELOW_THRESHOLD_STOP, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_PHASE_DETECT_LEN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL0, AUX_RX_DETECTION_THRESHOLD, mask_sh), \
	LE_SF(DP_AUX0_AUX_DPHY_TX_CONTROL, AUX_TX_PRECHARGE_LEN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_TX_CONTROL, AUX_TX_PRECHARGE_SYMBOLS, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_TX_CONTROL, AUX_MODE_DET_CHECK_DELAY, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL1, AUX_RX_PRECHARGE_SKIP, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL1, AUX_RX_TIMEOUT_LEN, mask_sh),\
	LE_SF(DP_AUX0_AUX_DPHY_RX_CONTROL1, AUX_RX_TIMEOUT_LEN_MUL, mask_sh),\
	LE_SF(DIO_LINKA_CNTL, ENC_TYPE_SEL, mask_sh),\
	LE_SF(DIO_LINKA_CNTL, HPO_DP_ENC_SEL, mask_sh),\
	LE_SF(DIO_LINKA_CNTL, HPO_HDMI_ENC_SEL, mask_sh),\
	LE_SF(DIO_CLK_CNTL, DISPCLK_R_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, DISPCLK_G_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, REFCLK_R_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, REFCLK_G_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SOCCLK_G_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLK_FE_R_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLK_FE_G_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLK_R_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLK_G_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, DIO_FGCG_REP_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, DISPCLK_G_HDCP_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLKA_G_HDCP_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLKB_G_HDCP_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLKC_G_HDCP_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLKD_G_HDCP_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLKE_G_HDCP_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLKF_G_HDCP_GATE_DIS, mask_sh),\
	LE_SF(DIO_CLK_CNTL, SYMCLKG_G_HDCP_GATE_DIS, mask_sh)


void dcn35_link_encoder_construct(
	struct dcn20_link_encoder *enc20,
	const struct encoder_init_data *init_data,
	const struct encoder_feature_support *enc_features,
	const struct dcn10_link_enc_registers *link_regs,
	const struct dcn10_link_enc_aux_registers *aux_regs,
	const struct dcn10_link_enc_hpd_registers *hpd_regs,
	const struct dcn10_link_enc_shift *link_shift,
	const struct dcn10_link_enc_mask *link_mask);

void dcn35_link_encoder_init(struct link_encoder *enc);
void dcn35_link_encoder_set_fgcg(struct link_encoder *enc, bool enabled);
bool dcn35_is_dig_enabled(struct link_encoder *enc);

enum amd_signal_type dcn35_get_dig_mode(struct link_encoder *enc);
void dcn35_link_encoder_setup(struct link_encoder *enc, enum amd_signal_type signal);

#endif /* __DC_LINK_ENCODER__DCN35_H__ */
