/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Madhav Chauhan <madhav.chauhan@intel.com>
 *   Jani Nikula <jani.nikula@intel.com>
 */

#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fixed.h>
#include <drm/drm_mipi_dsi.h>

#include "i915_reg.h"
#include "icl_dsi.h"
#include "icl_dsi_regs.h"
#include "intel_atomic.h"
#include "intel_backlight.h"
#include "intel_backlight_regs.h"
#include "intel_combo_phy.h"
#include "intel_combo_phy_regs.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_dsi.h"
#include "intel_dsi_vbt.h"
#include "intel_panel.h"
#include "intel_vdsc.h"
#include "intel_vdsc_regs.h"
#include "skl_scaler.h"
#include "skl_universal_plane.h"

static int header_credits_available(struct intel_display *display,
				    enum transcoder dsi_trans)
{
	return (intel_de_read(display, DSI_CMD_TXCTL(dsi_trans)) & FREE_HEADER_CREDIT_MASK)
		>> FREE_HEADER_CREDIT_SHIFT;
}

static int payload_credits_available(struct intel_display *display,
				     enum transcoder dsi_trans)
{
	return (intel_de_read(display, DSI_CMD_TXCTL(dsi_trans)) & FREE_PLOAD_CREDIT_MASK)
		>> FREE_PLOAD_CREDIT_SHIFT;
}

static bool wait_for_header_credits(struct intel_display *display,
				    enum transcoder dsi_trans, int hdr_credit)
{
	if (wait_for_us(header_credits_available(display, dsi_trans) >=
			hdr_credit, 100)) {
		drm_err(display->drm, "DSI header credits not released\n");
		return false;
	}

	return true;
}

static bool wait_for_payload_credits(struct intel_display *display,
				     enum transcoder dsi_trans, int payld_credit)
{
	if (wait_for_us(payload_credits_available(display, dsi_trans) >=
			payld_credit, 100)) {
		drm_err(display->drm, "DSI payload credits not released\n");
		return false;
	}

	return true;
}

static enum transcoder dsi_port_to_transcoder(enum port port)
{
	if (port == PORT_A)
		return TRANSCODER_DSI_0;
	else
		return TRANSCODER_DSI_1;
}

static void wait_for_cmds_dispatched_to_panel(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct mipi_dsi_device *dsi;
	enum port port;
	enum transcoder dsi_trans;
	int ret;

	/* wait for header/payload credits to be released */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		wait_for_header_credits(display, dsi_trans, MAX_HEADER_CREDIT);
		wait_for_payload_credits(display, dsi_trans, MAX_PLOAD_CREDIT);
	}

	/* send nop DCS command */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi = intel_dsi->dsi_hosts[port]->device;
		dsi->mode_flags |= MIPI_DSI_MODE_LPM;
		dsi->channel = 0;
		ret = mipi_dsi_dcs_nop(dsi);
		if (ret < 0)
			drm_err(display->drm,
				"error sending DCS NOP command\n");
	}

	/* wait for header credits to be released */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		wait_for_header_credits(display, dsi_trans, MAX_HEADER_CREDIT);
	}

	/* wait for LP TX in progress bit to be cleared */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		if (wait_for_us(!(intel_de_read(display, DSI_LP_MSG(dsi_trans)) &
				  LPTX_IN_PROGRESS), 20))
			drm_err(display->drm, "LPTX bit not cleared\n");
	}
}

static int dsi_send_pkt_payld(struct intel_dsi_host *host,
			      const struct mipi_dsi_packet *packet)
{
	struct intel_dsi *intel_dsi = host->intel_dsi;
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	enum transcoder dsi_trans = dsi_port_to_transcoder(host->port);
	const u8 *data = packet->payload;
	u32 len = packet->payload_length;
	int i, j;

	/* payload queue can accept *256 bytes*, check limit */
	if (len > MAX_PLOAD_CREDIT * 4) {
		drm_err(display->drm, "payload size exceeds max queue limit\n");
		return -EINVAL;
	}

	for (i = 0; i < len; i += 4) {
		u32 tmp = 0;

		if (!wait_for_payload_credits(display, dsi_trans, 1))
			return -EBUSY;

		for (j = 0; j < min_t(u32, len - i, 4); j++)
			tmp |= *data++ << 8 * j;

		intel_de_write(display, DSI_CMD_TXPYLD(dsi_trans), tmp);
	}

	return 0;
}

static int dsi_send_pkt_hdr(struct intel_dsi_host *host,
			    const struct mipi_dsi_packet *packet,
			    bool enable_lpdt)
{
	struct intel_dsi *intel_dsi = host->intel_dsi;
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	enum transcoder dsi_trans = dsi_port_to_transcoder(host->port);
	u32 tmp;

	if (!wait_for_header_credits(display, dsi_trans, 1))
		return -EBUSY;

	tmp = intel_de_read(display, DSI_CMD_TXHDR(dsi_trans));

	if (packet->payload)
		tmp |= PAYLOAD_PRESENT;
	else
		tmp &= ~PAYLOAD_PRESENT;

	tmp &= ~VBLANK_FENCE;

	if (enable_lpdt)
		tmp |= LP_DATA_TRANSFER;
	else
		tmp &= ~LP_DATA_TRANSFER;

	tmp &= ~(PARAM_WC_MASK | VC_MASK | DT_MASK);
	tmp |= ((packet->header[0] & VC_MASK) << VC_SHIFT);
	tmp |= ((packet->header[0] & DT_MASK) << DT_SHIFT);
	tmp |= (packet->header[1] << PARAM_WC_LOWER_SHIFT);
	tmp |= (packet->header[2] << PARAM_WC_UPPER_SHIFT);
	intel_de_write(display, DSI_CMD_TXHDR(dsi_trans), tmp);

	return 0;
}

void icl_dsi_frame_update(struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	u32 mode_flags;
	enum port port;

	mode_flags = crtc_state->mode_flags;

	/*
	 * case 1 also covers dual link
	 * In case of dual link, frame update should be set on
	 * DSI_0
	 */
	if (mode_flags & I915_MODE_FLAG_DSI_USE_TE0)
		port = PORT_A;
	else if (mode_flags & I915_MODE_FLAG_DSI_USE_TE1)
		port = PORT_B;
	else
		return;

	intel_de_rmw(display, DSI_CMD_FRMCTL(port), 0,
		     DSI_FRAME_UPDATE_REQUEST);
}

static void dsi_program_swing_and_deemphasis(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum phy phy;
	u32 tmp, mask, val;
	int lane;

	for_each_dsi_phy(phy, intel_dsi->phys) {
		/*
		 * Program voltage swing and pre-emphasis level values as per
		 * table in BSPEC under DDI buffer programing
		 */
		mask = SCALING_MODE_SEL_MASK | RTERM_SELECT_MASK;
		val = SCALING_MODE_SEL(0x2) | TAP2_DISABLE | TAP3_DISABLE |
		      RTERM_SELECT(0x6);
		tmp = intel_de_read(display, ICL_PORT_TX_DW5_LN(0, phy));
		tmp &= ~mask;
		tmp |= val;
		intel_de_write(display, ICL_PORT_TX_DW5_GRP(phy), tmp);
		intel_de_rmw(display, ICL_PORT_TX_DW5_AUX(phy), mask, val);

		mask = SWING_SEL_LOWER_MASK | SWING_SEL_UPPER_MASK |
		       RCOMP_SCALAR_MASK;
		val = SWING_SEL_UPPER(0x2) | SWING_SEL_LOWER(0x2) |
		      RCOMP_SCALAR(0x98);
		tmp = intel_de_read(display, ICL_PORT_TX_DW2_LN(0, phy));
		tmp &= ~mask;
		tmp |= val;
		intel_de_write(display, ICL_PORT_TX_DW2_GRP(phy), tmp);
		intel_de_rmw(display, ICL_PORT_TX_DW2_AUX(phy), mask, val);

		mask = POST_CURSOR_1_MASK | POST_CURSOR_2_MASK |
		       CURSOR_COEFF_MASK;
		val = POST_CURSOR_1(0x0) | POST_CURSOR_2(0x0) |
		      CURSOR_COEFF(0x3f);
		intel_de_rmw(display, ICL_PORT_TX_DW4_AUX(phy), mask, val);

		/* Bspec: must not use GRP register for write */
		for (lane = 0; lane <= 3; lane++)
			intel_de_rmw(display, ICL_PORT_TX_DW4_LN(lane, phy),
				     mask, val);
	}
}

static void configure_dual_link_mode(struct intel_encoder *encoder,
				     const struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	i915_reg_t dss_ctl1_reg, dss_ctl2_reg;
	u32 dss_ctl1;

	/* FIXME: Move all DSS handling to intel_vdsc.c */
	if (DISPLAY_VER(display) >= 12) {
		struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);

		dss_ctl1_reg = ICL_PIPE_DSS_CTL1(crtc->pipe);
		dss_ctl2_reg = ICL_PIPE_DSS_CTL2(crtc->pipe);
	} else {
		dss_ctl1_reg = DSS_CTL1;
		dss_ctl2_reg = DSS_CTL2;
	}

	dss_ctl1 = intel_de_read(display, dss_ctl1_reg);
	dss_ctl1 |= SPLITTER_ENABLE;
	dss_ctl1 &= ~OVERLAP_PIXELS_MASK;
	dss_ctl1 |= OVERLAP_PIXELS(intel_dsi->pixel_overlap);

	if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK) {
		const struct drm_display_mode *adjusted_mode =
					&pipe_config->hw.adjusted_mode;
		u16 hactive = adjusted_mode->crtc_hdisplay;
		u16 dl_buffer_depth;

		dss_ctl1 &= ~DUAL_LINK_MODE_INTERLEAVE;
		dl_buffer_depth = hactive / 2 + intel_dsi->pixel_overlap;

		if (dl_buffer_depth > MAX_DL_BUFFER_TARGET_DEPTH)
			drm_err(display->drm,
				"DL buffer depth exceed max value\n");

		dss_ctl1 &= ~LEFT_DL_BUF_TARGET_DEPTH_MASK;
		dss_ctl1 |= LEFT_DL_BUF_TARGET_DEPTH(dl_buffer_depth);
		intel_de_rmw(display, dss_ctl2_reg, RIGHT_DL_BUF_TARGET_DEPTH_MASK,
			     RIGHT_DL_BUF_TARGET_DEPTH(dl_buffer_depth));
	} else {
		/* Interleave */
		dss_ctl1 |= DUAL_LINK_MODE_INTERLEAVE;
	}

	intel_de_write(display, dss_ctl1_reg, dss_ctl1);
}

/* aka DSI 8X clock */
static int afe_clk(struct intel_encoder *encoder,
		   const struct intel_crtc_state *crtc_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	int bpp;

	if (crtc_state->dsc.compression_enable)
		bpp = fxp_q4_to_int(crtc_state->dsc.compressed_bpp_x16);
	else
		bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);

	return DIV_ROUND_CLOSEST(intel_dsi->pclk * bpp, intel_dsi->lane_count);
}

static void gen11_dsi_program_esc_clk_div(struct intel_encoder *encoder,
					  const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	int afe_clk_khz;
	int theo_word_clk, act_word_clk;
	u32 esc_clk_div_m, esc_clk_div_m_phy;

	afe_clk_khz = afe_clk(encoder, crtc_state);

	if (IS_ALDERLAKE_S(dev_priv) || IS_ALDERLAKE_P(dev_priv)) {
		theo_word_clk = DIV_ROUND_UP(afe_clk_khz, 8 * DSI_MAX_ESC_CLK);
		act_word_clk = max(3, theo_word_clk + (theo_word_clk + 1) % 2);
		esc_clk_div_m = act_word_clk * 8;
		esc_clk_div_m_phy = (act_word_clk - 1) / 2;
	} else {
		esc_clk_div_m = DIV_ROUND_UP(afe_clk_khz, DSI_MAX_ESC_CLK);
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		intel_de_write(display, ICL_DSI_ESC_CLK_DIV(port),
			       esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		intel_de_posting_read(display, ICL_DSI_ESC_CLK_DIV(port));
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		intel_de_write(display, ICL_DPHY_ESC_CLK_DIV(port),
			       esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		intel_de_posting_read(display, ICL_DPHY_ESC_CLK_DIV(port));
	}

	if (IS_ALDERLAKE_S(dev_priv) || IS_ALDERLAKE_P(dev_priv)) {
		for_each_dsi_port(port, intel_dsi->ports) {
			intel_de_write(display, ADL_MIPIO_DW(port, 8),
				       esc_clk_div_m_phy & TX_ESC_CLK_DIV_PHY);
			intel_de_posting_read(display, ADL_MIPIO_DW(port, 8));
		}
	}
}

static void get_dsi_io_power_domains(struct intel_dsi *intel_dsi)
{
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		drm_WARN_ON(display->drm, intel_dsi->io_wakeref[port]);
		intel_dsi->io_wakeref[port] =
			intel_display_power_get(dev_priv,
						port == PORT_A ?
						POWER_DOMAIN_PORT_DDI_IO_A :
						POWER_DOMAIN_PORT_DDI_IO_B);
	}
}

static void gen11_dsi_enable_io_power(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_rmw(display, ICL_DSI_IO_MODECTL(port),
			     0, COMBO_PHY_MODE_DSI);

	get_dsi_io_power_domains(intel_dsi);
}

static void gen11_dsi_power_up_lanes(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum phy phy;

	for_each_dsi_phy(phy, intel_dsi->phys)
		intel_combo_phy_power_up_lanes(dev_priv, phy, true,
					       intel_dsi->lane_count, false);
}

static void gen11_dsi_config_phy_lanes_sequence(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum phy phy;
	u32 tmp;
	int lane;

	/* Step 4b(i) set loadgen select for transmit and aux lanes */
	for_each_dsi_phy(phy, intel_dsi->phys) {
		intel_de_rmw(display, ICL_PORT_TX_DW4_AUX(phy),
			     LOADGEN_SELECT, 0);
		for (lane = 0; lane <= 3; lane++)
			intel_de_rmw(display, ICL_PORT_TX_DW4_LN(lane, phy),
				     LOADGEN_SELECT, lane != 2 ? LOADGEN_SELECT : 0);
	}

	/* Step 4b(ii) set latency optimization for transmit and aux lanes */
	for_each_dsi_phy(phy, intel_dsi->phys) {
		intel_de_rmw(display, ICL_PORT_TX_DW2_AUX(phy),
			     FRC_LATENCY_OPTIM_MASK, FRC_LATENCY_OPTIM_VAL(0x5));
		tmp = intel_de_read(display, ICL_PORT_TX_DW2_LN(0, phy));
		tmp &= ~FRC_LATENCY_OPTIM_MASK;
		tmp |= FRC_LATENCY_OPTIM_VAL(0x5);
		intel_de_write(display, ICL_PORT_TX_DW2_GRP(phy), tmp);

		/* For EHL, TGL, set latency optimization for PCS_DW1 lanes */
		if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv) ||
		    (DISPLAY_VER(display) >= 12)) {
			intel_de_rmw(display, ICL_PORT_PCS_DW1_AUX(phy),
				     LATENCY_OPTIM_MASK, LATENCY_OPTIM_VAL(0));

			tmp = intel_de_read(display,
					    ICL_PORT_PCS_DW1_LN(0, phy));
			tmp &= ~LATENCY_OPTIM_MASK;
			tmp |= LATENCY_OPTIM_VAL(0x1);
			intel_de_write(display, ICL_PORT_PCS_DW1_GRP(phy),
				       tmp);
		}
	}

}

static void gen11_dsi_voltage_swing_program_seq(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	u32 tmp;
	enum phy phy;

	/* clear common keeper enable bit */
	for_each_dsi_phy(phy, intel_dsi->phys) {
		tmp = intel_de_read(display, ICL_PORT_PCS_DW1_LN(0, phy));
		tmp &= ~COMMON_KEEPER_EN;
		intel_de_write(display, ICL_PORT_PCS_DW1_GRP(phy), tmp);
		intel_de_rmw(display, ICL_PORT_PCS_DW1_AUX(phy), COMMON_KEEPER_EN, 0);
	}

	/*
	 * Set SUS Clock Config bitfield to 11b
	 * Note: loadgen select program is done
	 * as part of lane phy sequence configuration
	 */
	for_each_dsi_phy(phy, intel_dsi->phys)
		intel_de_rmw(display, ICL_PORT_CL_DW5(phy), 0,
			     SUS_CLOCK_CONFIG);

	/* Clear training enable to change swing values */
	for_each_dsi_phy(phy, intel_dsi->phys) {
		tmp = intel_de_read(display, ICL_PORT_TX_DW5_LN(0, phy));
		tmp &= ~TX_TRAINING_EN;
		intel_de_write(display, ICL_PORT_TX_DW5_GRP(phy), tmp);
		intel_de_rmw(display, ICL_PORT_TX_DW5_AUX(phy), TX_TRAINING_EN, 0);
	}

	/* Program swing and de-emphasis */
	dsi_program_swing_and_deemphasis(encoder);

	/* Set training enable to trigger update */
	for_each_dsi_phy(phy, intel_dsi->phys) {
		tmp = intel_de_read(display, ICL_PORT_TX_DW5_LN(0, phy));
		tmp |= TX_TRAINING_EN;
		intel_de_write(display, ICL_PORT_TX_DW5_GRP(phy), tmp);
		intel_de_rmw(display, ICL_PORT_TX_DW5_AUX(phy), 0, TX_TRAINING_EN);
	}
}

static void gen11_dsi_enable_ddi_buffer(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		intel_de_rmw(display, DDI_BUF_CTL(port), 0, DDI_BUF_CTL_ENABLE);

		if (wait_for_us(!(intel_de_read(display, DDI_BUF_CTL(port)) &
				  DDI_BUF_IS_IDLE),
				  500))
			drm_err(display->drm, "DDI port:%c buffer idle\n",
				port_name(port));
	}
}

static void
gen11_dsi_setup_dphy_timings(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	enum phy phy;

	/* Program DPHY clock lanes timings */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_write(display, DPHY_CLK_TIMING_PARAM(port),
			       intel_dsi->dphy_reg);

	/* Program DPHY data lanes timings */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_write(display, DPHY_DATA_TIMING_PARAM(port),
			       intel_dsi->dphy_data_lane_reg);

	/*
	 * If DSI link operating at or below an 800 MHz,
	 * TA_SURE should be override and programmed to
	 * a value '0' inside TA_PARAM_REGISTERS otherwise
	 * leave all fields at HW default values.
	 */
	if (DISPLAY_VER(display) == 11) {
		if (afe_clk(encoder, crtc_state) <= 800000) {
			for_each_dsi_port(port, intel_dsi->ports)
				intel_de_rmw(display, DPHY_TA_TIMING_PARAM(port),
					     TA_SURE_MASK,
					     TA_SURE_OVERRIDE | TA_SURE(0));
		}
	}

	if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv)) {
		for_each_dsi_phy(phy, intel_dsi->phys)
			intel_de_rmw(display, ICL_DPHY_CHKN(phy),
				     0, ICL_DPHY_CHKN_AFE_OVER_PPI_STRAP);
	}
}

static void
gen11_dsi_setup_timings(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	/* Program T-INIT master registers */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_rmw(display, ICL_DSI_T_INIT_MASTER(port),
			     DSI_T_INIT_MASTER_MASK, intel_dsi->init_count);

	/* shadow register inside display core */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_write(display, DSI_CLK_TIMING_PARAM(port),
			       intel_dsi->dphy_reg);

	/* shadow register inside display core */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_write(display, DSI_DATA_TIMING_PARAM(port),
			       intel_dsi->dphy_data_lane_reg);

	/* shadow register inside display core */
	if (DISPLAY_VER(display) == 11) {
		if (afe_clk(encoder, crtc_state) <= 800000) {
			for_each_dsi_port(port, intel_dsi->ports) {
				intel_de_rmw(display, DSI_TA_TIMING_PARAM(port),
					     TA_SURE_MASK,
					     TA_SURE_OVERRIDE | TA_SURE(0));
			}
		}
	}
}

static void gen11_dsi_gate_clocks(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	u32 tmp;
	enum phy phy;

	mutex_lock(&display->dpll.lock);
	tmp = intel_de_read(display, ICL_DPCLKA_CFGCR0);
	for_each_dsi_phy(phy, intel_dsi->phys)
		tmp |= ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy);

	intel_de_write(display, ICL_DPCLKA_CFGCR0, tmp);
	mutex_unlock(&display->dpll.lock);
}

static void gen11_dsi_ungate_clocks(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	u32 tmp;
	enum phy phy;

	mutex_lock(&display->dpll.lock);
	tmp = intel_de_read(display, ICL_DPCLKA_CFGCR0);
	for_each_dsi_phy(phy, intel_dsi->phys)
		tmp &= ~ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy);

	intel_de_write(display, ICL_DPCLKA_CFGCR0, tmp);
	mutex_unlock(&display->dpll.lock);
}

static bool gen11_dsi_is_clock_enabled(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	bool clock_enabled = false;
	enum phy phy;
	u32 tmp;

	tmp = intel_de_read(display, ICL_DPCLKA_CFGCR0);

	for_each_dsi_phy(phy, intel_dsi->phys) {
		if (!(tmp & ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy)))
			clock_enabled = true;
	}

	return clock_enabled;
}

static void gen11_dsi_map_pll(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum phy phy;
	u32 val;

	mutex_lock(&display->dpll.lock);

	val = intel_de_read(display, ICL_DPCLKA_CFGCR0);
	for_each_dsi_phy(phy, intel_dsi->phys) {
		val &= ~ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy);
		val |= ICL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll->info->id, phy);
	}
	intel_de_write(display, ICL_DPCLKA_CFGCR0, val);

	for_each_dsi_phy(phy, intel_dsi->phys) {
		val &= ~ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy);
	}
	intel_de_write(display, ICL_DPCLKA_CFGCR0, val);

	intel_de_posting_read(display, ICL_DPCLKA_CFGCR0);

	mutex_unlock(&display->dpll.lock);
}

static void
gen11_dsi_configure_transcoder(struct intel_encoder *encoder,
			       const struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	u32 tmp;
	enum port port;
	enum transcoder dsi_trans;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = intel_de_read(display, DSI_TRANS_FUNC_CONF(dsi_trans));

		if (intel_dsi->eotp_pkt)
			tmp &= ~EOTP_DISABLED;
		else
			tmp |= EOTP_DISABLED;

		/* enable link calibration if freq > 1.5Gbps */
		if (afe_clk(encoder, pipe_config) >= 1500 * 1000) {
			tmp &= ~LINK_CALIBRATION_MASK;
			tmp |= CALIBRATION_ENABLED_INITIAL_ONLY;
		}

		/* configure continuous clock */
		tmp &= ~CONTINUOUS_CLK_MASK;
		if (intel_dsi->clock_stop)
			tmp |= CLK_ENTER_LP_AFTER_DATA;
		else
			tmp |= CLK_HS_CONTINUOUS;

		/* configure buffer threshold limit to minimum */
		tmp &= ~PIX_BUF_THRESHOLD_MASK;
		tmp |= PIX_BUF_THRESHOLD_1_4;

		/* set virtual channel to '0' */
		tmp &= ~PIX_VIRT_CHAN_MASK;
		tmp |= PIX_VIRT_CHAN(0);

		/* program BGR transmission */
		if (intel_dsi->bgr_enabled)
			tmp |= BGR_TRANSMISSION;

		/* select pixel format */
		tmp &= ~PIX_FMT_MASK;
		if (pipe_config->dsc.compression_enable) {
			tmp |= PIX_FMT_COMPRESSED;
		} else {
			switch (intel_dsi->pixel_format) {
			default:
				MISSING_CASE(intel_dsi->pixel_format);
				fallthrough;
			case MIPI_DSI_FMT_RGB565:
				tmp |= PIX_FMT_RGB565;
				break;
			case MIPI_DSI_FMT_RGB666_PACKED:
				tmp |= PIX_FMT_RGB666_PACKED;
				break;
			case MIPI_DSI_FMT_RGB666:
				tmp |= PIX_FMT_RGB666_LOOSE;
				break;
			case MIPI_DSI_FMT_RGB888:
				tmp |= PIX_FMT_RGB888;
				break;
			}
		}

		if (DISPLAY_VER(display) >= 12) {
			if (is_vid_mode(intel_dsi))
				tmp |= BLANKING_PACKET_ENABLE;
		}

		/* program DSI operation mode */
		if (is_vid_mode(intel_dsi)) {
			tmp &= ~OP_MODE_MASK;
			switch (intel_dsi->video_mode) {
			default:
				MISSING_CASE(intel_dsi->video_mode);
				fallthrough;
			case NON_BURST_SYNC_EVENTS:
				tmp |= VIDEO_MODE_SYNC_EVENT;
				break;
			case NON_BURST_SYNC_PULSE:
				tmp |= VIDEO_MODE_SYNC_PULSE;
				break;
			}
		} else {
			/*
			 * FIXME: Retrieve this info from VBT.
			 * As per the spec when dsi transcoder is operating
			 * in TE GATE mode, TE comes from GPIO
			 * which is UTIL PIN for DSI 0.
			 * Also this GPIO would not be used for other
			 * purposes is an assumption.
			 */
			tmp &= ~OP_MODE_MASK;
			tmp |= CMD_MODE_TE_GATE;
			tmp |= TE_SOURCE_GPIO;
		}

		intel_de_write(display, DSI_TRANS_FUNC_CONF(dsi_trans), tmp);
	}

	/* enable port sync mode if dual link */
	if (intel_dsi->dual_link) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			intel_de_rmw(display,
				     TRANS_DDI_FUNC_CTL2(display, dsi_trans),
				     0, PORT_SYNC_MODE_ENABLE);
		}

		/* configure stream splitting */
		configure_dual_link_mode(encoder, pipe_config);
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);

		/* select data lane width */
		tmp = intel_de_read(display,
				    TRANS_DDI_FUNC_CTL(display, dsi_trans));
		tmp &= ~TRANS_DDI_PORT_WIDTH_MASK;
		tmp |= TRANS_DDI_PORT_WIDTH(intel_dsi->lane_count);

		/* select input pipe */
		tmp &= ~TRANS_DDI_EDP_INPUT_MASK;
		switch (pipe) {
		default:
			MISSING_CASE(pipe);
			fallthrough;
		case PIPE_A:
			tmp |= TRANS_DDI_EDP_INPUT_A_ON;
			break;
		case PIPE_B:
			tmp |= TRANS_DDI_EDP_INPUT_B_ONOFF;
			break;
		case PIPE_C:
			tmp |= TRANS_DDI_EDP_INPUT_C_ONOFF;
			break;
		case PIPE_D:
			tmp |= TRANS_DDI_EDP_INPUT_D_ONOFF;
			break;
		}

		/* enable DDI buffer */
		tmp |= TRANS_DDI_FUNC_ENABLE;
		intel_de_write(display,
			       TRANS_DDI_FUNC_CTL(display, dsi_trans), tmp);
	}

	/* wait for link ready */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		if (wait_for_us((intel_de_read(display, DSI_TRANS_FUNC_CONF(dsi_trans)) &
				 LINK_READY), 2500))
			drm_err(display->drm, "DSI link not ready\n");
	}
}

static void
gen11_dsi_set_transcoder_timings(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	enum port port;
	enum transcoder dsi_trans;
	/* horizontal timings */
	u16 htotal, hactive, hsync_start, hsync_end, hsync_size;
	u16 hback_porch;
	/* vertical timings */
	u16 vtotal, vactive, vsync_start, vsync_end, vsync_shift;
	int mul = 1, div = 1;

	/*
	 * Adjust horizontal timings (htotal, hsync_start, hsync_end) to account
	 * for slower link speed if DSC is enabled.
	 *
	 * The compression frequency ratio is the ratio between compressed and
	 * non-compressed link speeds, and simplifies down to the ratio between
	 * compressed and non-compressed bpp.
	 */
	if (crtc_state->dsc.compression_enable) {
		mul = fxp_q4_to_int(crtc_state->dsc.compressed_bpp_x16);
		div = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	}

	hactive = adjusted_mode->crtc_hdisplay;

	if (is_vid_mode(intel_dsi))
		htotal = DIV_ROUND_UP(adjusted_mode->crtc_htotal * mul, div);
	else
		htotal = DIV_ROUND_UP((hactive + 160) * mul, div);

	hsync_start = DIV_ROUND_UP(adjusted_mode->crtc_hsync_start * mul, div);
	hsync_end = DIV_ROUND_UP(adjusted_mode->crtc_hsync_end * mul, div);
	hsync_size  = hsync_end - hsync_start;
	hback_porch = (adjusted_mode->crtc_htotal -
		       adjusted_mode->crtc_hsync_end);
	vactive = adjusted_mode->crtc_vdisplay;

	if (is_vid_mode(intel_dsi)) {
		vtotal = adjusted_mode->crtc_vtotal;
	} else {
		int bpp, line_time_us, byte_clk_period_ns;

		if (crtc_state->dsc.compression_enable)
			bpp = fxp_q4_to_int(crtc_state->dsc.compressed_bpp_x16);
		else
			bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);

		byte_clk_period_ns = 1000000 / afe_clk(encoder, crtc_state);
		line_time_us = (htotal * (bpp / 8) * byte_clk_period_ns) / (1000 * intel_dsi->lane_count);
		vtotal = vactive + DIV_ROUND_UP(400, line_time_us);
	}
	vsync_start = adjusted_mode->crtc_vsync_start;
	vsync_end = adjusted_mode->crtc_vsync_end;
	vsync_shift = hsync_start - htotal / 2;

	if (intel_dsi->dual_link) {
		hactive /= 2;
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
			hactive += intel_dsi->pixel_overlap;
		htotal /= 2;
	}

	/* minimum hactive as per bspec: 256 pixels */
	if (adjusted_mode->crtc_hdisplay < 256)
		drm_err(display->drm, "hactive is less then 256 pixels\n");

	/* if RGB666 format, then hactive must be multiple of 4 pixels */
	if (intel_dsi->pixel_format == MIPI_DSI_FMT_RGB666 && hactive % 4 != 0)
		drm_err(display->drm,
			"hactive pixels are not multiple of 4\n");

	/* program TRANS_HTOTAL register */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		intel_de_write(display, TRANS_HTOTAL(display, dsi_trans),
			       HACTIVE(hactive - 1) | HTOTAL(htotal - 1));
	}

	/* TRANS_HSYNC register to be programmed only for video mode */
	if (is_vid_mode(intel_dsi)) {
		if (intel_dsi->video_mode == NON_BURST_SYNC_PULSE) {
			/* BSPEC: hsync size should be atleast 16 pixels */
			if (hsync_size < 16)
				drm_err(display->drm,
					"hsync size < 16 pixels\n");
		}

		if (hback_porch < 16)
			drm_err(display->drm, "hback porch < 16 pixels\n");

		if (intel_dsi->dual_link) {
			hsync_start /= 2;
			hsync_end /= 2;
		}

		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			intel_de_write(display,
				       TRANS_HSYNC(display, dsi_trans),
				       HSYNC_START(hsync_start - 1) | HSYNC_END(hsync_end - 1));
		}
	}

	/* program TRANS_VTOTAL register */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		/*
		 * FIXME: Programing this by assuming progressive mode, since
		 * non-interlaced info from VBT is not saved inside
		 * struct drm_display_mode.
		 * For interlace mode: program required pixel minus 2
		 */
		intel_de_write(display, TRANS_VTOTAL(display, dsi_trans),
			       VACTIVE(vactive - 1) | VTOTAL(vtotal - 1));
	}

	if (vsync_end < vsync_start || vsync_end > vtotal)
		drm_err(display->drm, "Invalid vsync_end value\n");

	if (vsync_start < vactive)
		drm_err(display->drm, "vsync_start less than vactive\n");

	/* program TRANS_VSYNC register for video mode only */
	if (is_vid_mode(intel_dsi)) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			intel_de_write(display,
				       TRANS_VSYNC(display, dsi_trans),
				       VSYNC_START(vsync_start - 1) | VSYNC_END(vsync_end - 1));
		}
	}

	/*
	 * FIXME: It has to be programmed only for video modes and interlaced
	 * modes. Put the check condition here once interlaced
	 * info available as described above.
	 * program TRANS_VSYNCSHIFT register
	 */
	if (is_vid_mode(intel_dsi)) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			intel_de_write(display,
				       TRANS_VSYNCSHIFT(display, dsi_trans),
				       vsync_shift);
		}
	}

	/*
	 * program TRANS_VBLANK register, should be same as vtotal programmed
	 *
	 * FIXME get rid of these local hacks and do it right,
	 * this will not handle eg. delayed vblank correctly.
	 */
	if (DISPLAY_VER(display) >= 12) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			intel_de_write(display,
				       TRANS_VBLANK(display, dsi_trans),
				       VBLANK_START(vactive - 1) | VBLANK_END(vtotal - 1));
		}
	}
}

static void gen11_dsi_enable_transcoder(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	enum transcoder dsi_trans;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		intel_de_rmw(display, TRANSCONF(display, dsi_trans), 0,
			     TRANSCONF_ENABLE);

		/* wait for transcoder to be enabled */
		if (intel_de_wait_for_set(display, TRANSCONF(display, dsi_trans),
					  TRANSCONF_STATE_ENABLE, 10))
			drm_err(display->drm,
				"DSI transcoder not enabled\n");
	}
}

static void gen11_dsi_setup_timeouts(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	enum transcoder dsi_trans;
	u32 hs_tx_timeout, lp_rx_timeout, ta_timeout, divisor, mul;

	/*
	 * escape clock count calculation:
	 * BYTE_CLK_COUNT = TIME_NS/(8 * UI)
	 * UI (nsec) = (10^6)/Bitrate
	 * TIME_NS = (BYTE_CLK_COUNT * 8 * 10^6)/ Bitrate
	 * ESCAPE_CLK_COUNT  = TIME_NS/ESC_CLK_NS
	 */
	divisor = intel_dsi_tlpx_ns(intel_dsi) * afe_clk(encoder, crtc_state) * 1000;
	mul = 8 * 1000000;
	hs_tx_timeout = DIV_ROUND_UP(intel_dsi->hs_tx_timeout * mul,
				     divisor);
	lp_rx_timeout = DIV_ROUND_UP(intel_dsi->lp_rx_timeout * mul, divisor);
	ta_timeout = DIV_ROUND_UP(intel_dsi->turn_arnd_val * mul, divisor);

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);

		/* program hst_tx_timeout */
		intel_de_rmw(display, DSI_HSTX_TO(dsi_trans),
			     HSTX_TIMEOUT_VALUE_MASK,
			     HSTX_TIMEOUT_VALUE(hs_tx_timeout));

		/* FIXME: DSI_CALIB_TO */

		/* program lp_rx_host timeout */
		intel_de_rmw(display, DSI_LPRX_HOST_TO(dsi_trans),
			     LPRX_TIMEOUT_VALUE_MASK,
			     LPRX_TIMEOUT_VALUE(lp_rx_timeout));

		/* FIXME: DSI_PWAIT_TO */

		/* program turn around timeout */
		intel_de_rmw(display, DSI_TA_TO(dsi_trans),
			     TA_TIMEOUT_VALUE_MASK,
			     TA_TIMEOUT_VALUE(ta_timeout));
	}
}

static void gen11_dsi_config_util_pin(struct intel_encoder *encoder,
				      bool enable)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	u32 tmp;

	/*
	 * used as TE i/p for DSI0,
	 * for dual link/DSI1 TE is from slave DSI1
	 * through GPIO.
	 */
	if (is_vid_mode(intel_dsi) || (intel_dsi->ports & BIT(PORT_B)))
		return;

	tmp = intel_de_read(display, UTIL_PIN_CTL);

	if (enable) {
		tmp |= UTIL_PIN_DIRECTION_INPUT;
		tmp |= UTIL_PIN_ENABLE;
	} else {
		tmp &= ~UTIL_PIN_ENABLE;
	}
	intel_de_write(display, UTIL_PIN_CTL, tmp);
}

static void
gen11_dsi_enable_port_and_phy(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state)
{
	/* step 4a: power up all lanes of the DDI used by DSI */
	gen11_dsi_power_up_lanes(encoder);

	/* step 4b: configure lane sequencing of the Combo-PHY transmitters */
	gen11_dsi_config_phy_lanes_sequence(encoder);

	/* step 4c: configure voltage swing and skew */
	gen11_dsi_voltage_swing_program_seq(encoder);

	/* setup D-PHY timings */
	gen11_dsi_setup_dphy_timings(encoder, crtc_state);

	/* enable DDI buffer */
	gen11_dsi_enable_ddi_buffer(encoder);

	gen11_dsi_gate_clocks(encoder);

	gen11_dsi_setup_timings(encoder, crtc_state);

	/* Since transcoder is configured to take events from GPIO */
	gen11_dsi_config_util_pin(encoder, true);

	/* step 4h: setup DSI protocol timeouts */
	gen11_dsi_setup_timeouts(encoder, crtc_state);

	/* Step (4h, 4i, 4j, 4k): Configure transcoder */
	gen11_dsi_configure_transcoder(encoder, crtc_state);
}

static void gen11_dsi_powerup_panel(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct mipi_dsi_device *dsi;
	enum port port;
	enum transcoder dsi_trans;
	u32 tmp;
	int ret;

	/* set maximum return packet size */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);

		/*
		 * FIXME: This uses the number of DW's currently in the payload
		 * receive queue. This is probably not what we want here.
		 */
		tmp = intel_de_read(display, DSI_CMD_RXCTL(dsi_trans));
		tmp &= NUMBER_RX_PLOAD_DW_MASK;
		/* multiply "Number Rx Payload DW" by 4 to get max value */
		tmp = tmp * 4;
		dsi = intel_dsi->dsi_hosts[port]->device;
		ret = mipi_dsi_set_maximum_return_packet_size(dsi, tmp);
		if (ret < 0)
			drm_err(display->drm,
				"error setting max return pkt size%d\n", tmp);
	}

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_INIT_OTP);

	/* ensure all panel commands dispatched before enabling transcoder */
	wait_for_cmds_dispatched_to_panel(encoder);
}

static void gen11_dsi_pre_pll_enable(struct intel_atomic_state *state,
				     struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state,
				     const struct drm_connector_state *conn_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);

	intel_dsi_wait_panel_power_cycle(intel_dsi);

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_POWER_ON);
	drm_msleep(intel_dsi->panel_on_delay);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DEASSERT_RESET);

	/* step2: enable IO power */
	gen11_dsi_enable_io_power(encoder);

	/* step3: enable DSI PLL */
	gen11_dsi_program_esc_clk_div(encoder, crtc_state);
}

static void gen11_dsi_pre_enable(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *pipe_config,
				 const struct drm_connector_state *conn_state)
{
	/* step3b */
	gen11_dsi_map_pll(encoder, pipe_config);

	/* step4: enable DSI port and DPHY */
	gen11_dsi_enable_port_and_phy(encoder, pipe_config);

	/* step5: program and powerup panel */
	gen11_dsi_powerup_panel(encoder);

	intel_dsc_dsi_pps_write(encoder, pipe_config);

	/* step6c: configure transcoder timings */
	gen11_dsi_set_transcoder_timings(encoder, pipe_config);
}

/*
 * Wa_1409054076:icl,jsl,ehl
 * When pipe A is disabled and MIPI DSI is enabled on pipe B,
 * the AMT KVMR feature will incorrectly see pipe A as enabled.
 * Set 0x42080 bit 23=1 before enabling DSI on pipe B and leave
 * it set while DSI is enabled on pipe B
 */
static void icl_apply_kvmr_pipe_a_wa(struct intel_encoder *encoder,
				     enum pipe pipe, bool enable)
{
	struct intel_display *display = to_intel_display(encoder);

	if (DISPLAY_VER(display) == 11 && pipe == PIPE_B)
		intel_de_rmw(display, CHICKEN_PAR1_1,
			     IGNORE_KVMR_PIPE_A,
			     enable ? IGNORE_KVMR_PIPE_A : 0);
}

/*
 * Wa_16012360555:adl-p
 * SW will have to program the "LP to HS Wakeup Guardband"
 * to account for the repeaters on the HS Request/Ready
 * PPI signaling between the Display engine and the DPHY.
 */
static void adlp_set_lp_hs_wakeup_gb(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	if (DISPLAY_VER(display) == 13) {
		for_each_dsi_port(port, intel_dsi->ports)
			intel_de_rmw(display, TGL_DSI_CHKN_REG(port),
				     TGL_DSI_CHKN_LSHS_GB_MASK,
				     TGL_DSI_CHKN_LSHS_GB(4));
	}
}

static void gen11_dsi_enable(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	/* Wa_1409054076:icl,jsl,ehl */
	icl_apply_kvmr_pipe_a_wa(encoder, crtc->pipe, true);

	/* Wa_16012360555:adl-p */
	adlp_set_lp_hs_wakeup_gb(encoder);

	/* step6d: enable dsi transcoder */
	gen11_dsi_enable_transcoder(encoder);

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DISPLAY_ON);

	/* step7: enable backlight */
	intel_backlight_enable(crtc_state, conn_state);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_BACKLIGHT_ON);

	intel_crtc_vblank_on(crtc_state);
}

static void gen11_dsi_disable_transcoder(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	enum transcoder dsi_trans;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);

		/* disable transcoder */
		intel_de_rmw(display, TRANSCONF(display, dsi_trans),
			     TRANSCONF_ENABLE, 0);

		/* wait for transcoder to be disabled */
		if (intel_de_wait_for_clear(display, TRANSCONF(display, dsi_trans),
					    TRANSCONF_STATE_ENABLE, 50))
			drm_err(display->drm,
				"DSI trancoder not disabled\n");
	}
}

static void gen11_dsi_powerdown_panel(struct intel_encoder *encoder)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DISPLAY_OFF);

	/* ensure cmds dispatched to panel */
	wait_for_cmds_dispatched_to_panel(encoder);
}

static void gen11_dsi_deconfigure_trancoder(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;
	enum transcoder dsi_trans;
	u32 tmp;

	/* disable periodic update mode */
	if (is_cmd_mode(intel_dsi)) {
		for_each_dsi_port(port, intel_dsi->ports)
			intel_de_rmw(display, DSI_CMD_FRMCTL(port),
				     DSI_PERIODIC_FRAME_UPDATE_ENABLE, 0);
	}

	/* put dsi link in ULPS */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = intel_de_read(display, DSI_LP_MSG(dsi_trans));
		tmp |= LINK_ENTER_ULPS;
		tmp &= ~LINK_ULPS_TYPE_LP11;
		intel_de_write(display, DSI_LP_MSG(dsi_trans), tmp);

		if (wait_for_us((intel_de_read(display, DSI_LP_MSG(dsi_trans)) &
				 LINK_IN_ULPS),
				10))
			drm_err(display->drm, "DSI link not in ULPS\n");
	}

	/* disable ddi function */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		intel_de_rmw(display,
			     TRANS_DDI_FUNC_CTL(display, dsi_trans),
			     TRANS_DDI_FUNC_ENABLE, 0);
	}

	/* disable port sync mode if dual link */
	if (intel_dsi->dual_link) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			intel_de_rmw(display,
				     TRANS_DDI_FUNC_CTL2(display, dsi_trans),
				     PORT_SYNC_MODE_ENABLE, 0);
		}
	}
}

static void gen11_dsi_disable_port(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	gen11_dsi_ungate_clocks(encoder);
	for_each_dsi_port(port, intel_dsi->ports) {
		intel_de_rmw(display, DDI_BUF_CTL(port), DDI_BUF_CTL_ENABLE, 0);

		if (wait_for_us((intel_de_read(display, DDI_BUF_CTL(port)) &
				 DDI_BUF_IS_IDLE),
				 8))
			drm_err(display->drm,
				"DDI port:%c buffer not idle\n",
				port_name(port));
	}
	gen11_dsi_gate_clocks(encoder);
}

static void gen11_dsi_disable_io_power(struct intel_encoder *encoder)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		intel_wakeref_t wakeref;

		wakeref = fetch_and_zero(&intel_dsi->io_wakeref[port]);
		intel_display_power_put(dev_priv,
					port == PORT_A ?
					POWER_DOMAIN_PORT_DDI_IO_A :
					POWER_DOMAIN_PORT_DDI_IO_B,
					wakeref);
	}

	/* set mode to DDI */
	for_each_dsi_port(port, intel_dsi->ports)
		intel_de_rmw(display, ICL_DSI_IO_MODECTL(port),
			     COMBO_PHY_MODE_DSI, 0);
}

static void gen11_dsi_disable(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);

	/* step1: turn off backlight */
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_BACKLIGHT_OFF);
	intel_backlight_disable(old_conn_state);
}

static void gen11_dsi_post_disable(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);

	intel_crtc_vblank_off(old_crtc_state);

	/* step2d,e: disable transcoder and wait */
	gen11_dsi_disable_transcoder(encoder);

	/* Wa_1409054076:icl,jsl,ehl */
	icl_apply_kvmr_pipe_a_wa(encoder, crtc->pipe, false);

	/* step2f,g: powerdown panel */
	gen11_dsi_powerdown_panel(encoder);

	/* step2h,i,j: deconfig trancoder */
	gen11_dsi_deconfigure_trancoder(encoder);

	intel_dsc_disable(old_crtc_state);
	skl_scaler_disable(old_crtc_state);

	/* step3: disable port */
	gen11_dsi_disable_port(encoder);

	gen11_dsi_config_util_pin(encoder, false);

	/* step4: disable IO power */
	gen11_dsi_disable_io_power(encoder);

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_ASSERT_RESET);

	drm_msleep(intel_dsi->panel_off_delay);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_POWER_OFF);

	intel_dsi->panel_power_off_time = ktime_get_boottime();
}

static enum drm_mode_status gen11_dsi_mode_valid(struct drm_connector *connector,
						 struct drm_display_mode *mode)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);
	enum drm_mode_status status;

	status = intel_cpu_transcoder_mode_valid(i915, mode);
	if (status != MODE_OK)
		return status;

	/* FIXME: DSC? */
	return intel_dsi_mode_valid(connector, mode);
}

static void gen11_dsi_get_timings(struct intel_encoder *encoder,
				  struct intel_crtc_state *pipe_config)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct drm_display_mode *adjusted_mode =
					&pipe_config->hw.adjusted_mode;

	if (pipe_config->dsc.compressed_bpp_x16) {
		int div = fxp_q4_to_int(pipe_config->dsc.compressed_bpp_x16);
		int mul = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);

		adjusted_mode->crtc_htotal =
			DIV_ROUND_UP(adjusted_mode->crtc_htotal * mul, div);
		adjusted_mode->crtc_hsync_start =
			DIV_ROUND_UP(adjusted_mode->crtc_hsync_start * mul, div);
		adjusted_mode->crtc_hsync_end =
			DIV_ROUND_UP(adjusted_mode->crtc_hsync_end * mul, div);
	}

	if (intel_dsi->dual_link) {
		adjusted_mode->crtc_hdisplay *= 2;
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
			adjusted_mode->crtc_hdisplay -=
						intel_dsi->pixel_overlap;
		adjusted_mode->crtc_htotal *= 2;
	}
	adjusted_mode->crtc_hblank_start = adjusted_mode->crtc_hdisplay;
	adjusted_mode->crtc_hblank_end = adjusted_mode->crtc_htotal;

	if (intel_dsi->operation_mode == INTEL_DSI_VIDEO_MODE) {
		if (intel_dsi->dual_link) {
			adjusted_mode->crtc_hsync_start *= 2;
			adjusted_mode->crtc_hsync_end *= 2;
		}
	}
	adjusted_mode->crtc_vblank_start = adjusted_mode->crtc_vdisplay;
	adjusted_mode->crtc_vblank_end = adjusted_mode->crtc_vtotal;
}

static bool gen11_dsi_is_periodic_cmd_mode(struct intel_dsi *intel_dsi)
{
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	enum transcoder dsi_trans;
	u32 val;

	if (intel_dsi->ports == BIT(PORT_B))
		dsi_trans = TRANSCODER_DSI_1;
	else
		dsi_trans = TRANSCODER_DSI_0;

	val = intel_de_read(display, DSI_TRANS_FUNC_CONF(dsi_trans));
	return (val & DSI_PERIODIC_FRAME_UPDATE_ENABLE);
}

static void gen11_dsi_get_cmd_mode_config(struct intel_dsi *intel_dsi,
					  struct intel_crtc_state *pipe_config)
{
	if (intel_dsi->ports == (BIT(PORT_B) | BIT(PORT_A)))
		pipe_config->mode_flags |= I915_MODE_FLAG_DSI_USE_TE1 |
					    I915_MODE_FLAG_DSI_USE_TE0;
	else if (intel_dsi->ports == BIT(PORT_B))
		pipe_config->mode_flags |= I915_MODE_FLAG_DSI_USE_TE1;
	else
		pipe_config->mode_flags |= I915_MODE_FLAG_DSI_USE_TE0;
}

static void gen11_dsi_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);

	intel_ddi_get_clock(encoder, pipe_config, icl_ddi_combo_get_pll(encoder));

	pipe_config->hw.adjusted_mode.crtc_clock = intel_dsi->pclk;
	if (intel_dsi->dual_link)
		pipe_config->hw.adjusted_mode.crtc_clock *= 2;

	gen11_dsi_get_timings(encoder, pipe_config);
	pipe_config->output_types |= BIT(INTEL_OUTPUT_DSI);
	pipe_config->pipe_bpp = bdw_get_pipe_misc_bpp(crtc);

	/* Get the details on which TE should be enabled */
	if (is_cmd_mode(intel_dsi))
		gen11_dsi_get_cmd_mode_config(intel_dsi, pipe_config);

	if (gen11_dsi_is_periodic_cmd_mode(intel_dsi))
		pipe_config->mode_flags |= I915_MODE_FLAG_DSI_PERIODIC_CMD_MODE;
}

static void gen11_dsi_sync_state(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *intel_crtc;
	enum pipe pipe;

	if (!crtc_state)
		return;

	intel_crtc = to_intel_crtc(crtc_state->uapi.crtc);
	pipe = intel_crtc->pipe;

	/* wa verify 1409054076:icl,jsl,ehl */
	if (DISPLAY_VER(display) == 11 && pipe == PIPE_B &&
	    !(intel_de_read(display, CHICKEN_PAR1_1) & IGNORE_KVMR_PIPE_A))
		drm_dbg_kms(display->drm,
			    "[ENCODER:%d:%s] BIOS left IGNORE_KVMR_PIPE_A cleared with pipe B enabled\n",
			    encoder->base.base.id,
			    encoder->base.name);
}

static int gen11_dsi_dsc_compute_config(struct intel_encoder *encoder,
					struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_dsc_config *vdsc_cfg = &crtc_state->dsc.config;
	int dsc_max_bpc = DISPLAY_VER(display) >= 12 ? 12 : 10;
	bool use_dsc;
	int ret;

	use_dsc = intel_bios_get_dsc_params(encoder, crtc_state, dsc_max_bpc);
	if (!use_dsc)
		return 0;

	if (crtc_state->pipe_bpp < 8 * 3)
		return -EINVAL;

	/* FIXME: split only when necessary */
	if (crtc_state->dsc.slice_count > 1)
		crtc_state->dsc.dsc_split = true;

	/* FIXME: initialize from VBT */
	vdsc_cfg->rc_model_size = DSC_RC_MODEL_SIZE_CONST;

	vdsc_cfg->pic_height = crtc_state->hw.adjusted_mode.crtc_vdisplay;

	ret = intel_dsc_compute_params(crtc_state);
	if (ret)
		return ret;

	/* DSI specific sanity checks on the common code */
	drm_WARN_ON(display->drm, vdsc_cfg->vbr_enable);
	drm_WARN_ON(display->drm, vdsc_cfg->simple_422);
	drm_WARN_ON(display->drm,
		    vdsc_cfg->pic_width % vdsc_cfg->slice_width);
	drm_WARN_ON(display->drm, vdsc_cfg->slice_height < 8);
	drm_WARN_ON(display->drm,
		    vdsc_cfg->pic_height % vdsc_cfg->slice_height);

	ret = drm_dsc_compute_rc_parameters(vdsc_cfg);
	if (ret)
		return ret;

	crtc_state->dsc.compression_enable = true;

	return 0;
}

static int gen11_dsi_compute_config(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config,
				    struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	struct intel_connector *intel_connector = intel_dsi->attached_connector;
	struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	int ret;

	pipe_config->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	ret = intel_panel_compute_config(intel_connector, adjusted_mode);
	if (ret)
		return ret;

	ret = intel_panel_fitting(pipe_config, conn_state);
	if (ret)
		return ret;

	adjusted_mode->flags = 0;

	/* Dual link goes to trancoder DSI'0' */
	if (intel_dsi->ports == BIT(PORT_B))
		pipe_config->cpu_transcoder = TRANSCODER_DSI_1;
	else
		pipe_config->cpu_transcoder = TRANSCODER_DSI_0;

	if (intel_dsi->pixel_format == MIPI_DSI_FMT_RGB888)
		pipe_config->pipe_bpp = 24;
	else
		pipe_config->pipe_bpp = 18;

	pipe_config->clock_set = true;

	if (gen11_dsi_dsc_compute_config(encoder, pipe_config))
		drm_dbg_kms(display->drm, "Attempting to use DSC failed\n");

	pipe_config->port_clock = afe_clk(encoder, pipe_config) / 5;

	/*
	 * In case of TE GATE cmd mode, we
	 * receive TE from the slave if
	 * dual link is enabled
	 */
	if (is_cmd_mode(intel_dsi))
		gen11_dsi_get_cmd_mode_config(intel_dsi, pipe_config);

	return 0;
}

static void gen11_dsi_get_power_domains(struct intel_encoder *encoder,
					struct intel_crtc_state *crtc_state)
{
	get_dsi_io_power_domains(enc_to_intel_dsi(encoder));
}

static bool gen11_dsi_get_hw_state(struct intel_encoder *encoder,
				   enum pipe *pipe)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(encoder);
	enum transcoder dsi_trans;
	intel_wakeref_t wakeref;
	enum port port;
	bool ret = false;
	u32 tmp;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = intel_de_read(display,
				    TRANS_DDI_FUNC_CTL(display, dsi_trans));
		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		case TRANS_DDI_EDP_INPUT_A_ON:
			*pipe = PIPE_A;
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			*pipe = PIPE_B;
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			*pipe = PIPE_C;
			break;
		case TRANS_DDI_EDP_INPUT_D_ONOFF:
			*pipe = PIPE_D;
			break;
		default:
			drm_err(display->drm, "Invalid PIPE input\n");
			goto out;
		}

		tmp = intel_de_read(display, TRANSCONF(display, dsi_trans));
		ret = tmp & TRANSCONF_ENABLE;
	}
out:
	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);
	return ret;
}

static bool gen11_dsi_initial_fastset_check(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state)
{
	if (crtc_state->dsc.compression_enable) {
		drm_dbg_kms(encoder->base.dev, "Forcing full modeset due to DSC being enabled\n");
		crtc_state->uapi.mode_changed = true;

		return false;
	}

	return true;
}

static void gen11_dsi_encoder_destroy(struct drm_encoder *encoder)
{
	intel_encoder_destroy(encoder);
}

static const struct drm_encoder_funcs gen11_dsi_encoder_funcs = {
	.destroy = gen11_dsi_encoder_destroy,
};

static const struct drm_connector_funcs gen11_dsi_connector_funcs = {
	.detect = intel_panel_detect,
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs gen11_dsi_connector_helper_funcs = {
	.get_modes = intel_dsi_get_modes,
	.mode_valid = gen11_dsi_mode_valid,
	.atomic_check = intel_digital_connector_atomic_check,
};

static int gen11_dsi_host_attach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *dsi)
{
	return 0;
}

static int gen11_dsi_host_detach(struct mipi_dsi_host *host,
				 struct mipi_dsi_device *dsi)
{
	return 0;
}

static ssize_t gen11_dsi_host_transfer(struct mipi_dsi_host *host,
				       const struct mipi_dsi_msg *msg)
{
	struct intel_dsi_host *intel_dsi_host = to_intel_dsi_host(host);
	struct mipi_dsi_packet dsi_pkt;
	ssize_t ret;
	bool enable_lpdt = false;

	ret = mipi_dsi_create_packet(&dsi_pkt, msg);
	if (ret < 0)
		return ret;

	if (msg->flags & MIPI_DSI_MSG_USE_LPM)
		enable_lpdt = true;

	/* only long packet contains payload */
	if (mipi_dsi_packet_format_is_long(msg->type)) {
		ret = dsi_send_pkt_payld(intel_dsi_host, &dsi_pkt);
		if (ret < 0)
			return ret;
	}

	/* send packet header */
	ret  = dsi_send_pkt_hdr(intel_dsi_host, &dsi_pkt, enable_lpdt);
	if (ret < 0)
		return ret;

	//TODO: add payload receive code if needed

	ret = sizeof(dsi_pkt.header) + dsi_pkt.payload_length;

	return ret;
}

static const struct mipi_dsi_host_ops gen11_dsi_host_ops = {
	.attach = gen11_dsi_host_attach,
	.detach = gen11_dsi_host_detach,
	.transfer = gen11_dsi_host_transfer,
};

#define ICL_PREPARE_CNT_MAX	0x7
#define ICL_CLK_ZERO_CNT_MAX	0xf
#define ICL_TRAIL_CNT_MAX	0x7
#define ICL_TCLK_PRE_CNT_MAX	0x3
#define ICL_TCLK_POST_CNT_MAX	0x7
#define ICL_HS_ZERO_CNT_MAX	0xf
#define ICL_EXIT_ZERO_CNT_MAX	0x7

static void icl_dphy_param_init(struct intel_dsi *intel_dsi)
{
	struct intel_display *display = to_intel_display(&intel_dsi->base);
	struct intel_connector *connector = intel_dsi->attached_connector;
	struct mipi_config *mipi_config = connector->panel.vbt.dsi.config;
	u32 tlpx_ns;
	u32 prepare_cnt, exit_zero_cnt, clk_zero_cnt, trail_cnt;
	u32 ths_prepare_ns, tclk_trail_ns;
	u32 hs_zero_cnt;
	u32 tclk_pre_cnt;

	tlpx_ns = intel_dsi_tlpx_ns(intel_dsi);

	tclk_trail_ns = max(mipi_config->tclk_trail, mipi_config->ths_trail);
	ths_prepare_ns = max(mipi_config->ths_prepare,
			     mipi_config->tclk_prepare);

	/*
	 * prepare cnt in escape clocks
	 * this field represents a hexadecimal value with a precision
	 * of 1.2 – i.e. the most significant bit is the integer
	 * and the least significant 2 bits are fraction bits.
	 * so, the field can represent a range of 0.25 to 1.75
	 */
	prepare_cnt = DIV_ROUND_UP(ths_prepare_ns * 4, tlpx_ns);
	if (prepare_cnt > ICL_PREPARE_CNT_MAX) {
		drm_dbg_kms(display->drm, "prepare_cnt out of range (%d)\n",
			    prepare_cnt);
		prepare_cnt = ICL_PREPARE_CNT_MAX;
	}

	/* clk zero count in escape clocks */
	clk_zero_cnt = DIV_ROUND_UP(mipi_config->tclk_prepare_clkzero -
				    ths_prepare_ns, tlpx_ns);
	if (clk_zero_cnt > ICL_CLK_ZERO_CNT_MAX) {
		drm_dbg_kms(display->drm,
			    "clk_zero_cnt out of range (%d)\n", clk_zero_cnt);
		clk_zero_cnt = ICL_CLK_ZERO_CNT_MAX;
	}

	/* trail cnt in escape clocks*/
	trail_cnt = DIV_ROUND_UP(tclk_trail_ns, tlpx_ns);
	if (trail_cnt > ICL_TRAIL_CNT_MAX) {
		drm_dbg_kms(display->drm, "trail_cnt out of range (%d)\n",
			    trail_cnt);
		trail_cnt = ICL_TRAIL_CNT_MAX;
	}

	/* tclk pre count in escape clocks */
	tclk_pre_cnt = DIV_ROUND_UP(mipi_config->tclk_pre, tlpx_ns);
	if (tclk_pre_cnt > ICL_TCLK_PRE_CNT_MAX) {
		drm_dbg_kms(display->drm,
			    "tclk_pre_cnt out of range (%d)\n", tclk_pre_cnt);
		tclk_pre_cnt = ICL_TCLK_PRE_CNT_MAX;
	}

	/* hs zero cnt in escape clocks */
	hs_zero_cnt = DIV_ROUND_UP(mipi_config->ths_prepare_hszero -
				   ths_prepare_ns, tlpx_ns);
	if (hs_zero_cnt > ICL_HS_ZERO_CNT_MAX) {
		drm_dbg_kms(display->drm, "hs_zero_cnt out of range (%d)\n",
			    hs_zero_cnt);
		hs_zero_cnt = ICL_HS_ZERO_CNT_MAX;
	}

	/* hs exit zero cnt in escape clocks */
	exit_zero_cnt = DIV_ROUND_UP(mipi_config->ths_exit, tlpx_ns);
	if (exit_zero_cnt > ICL_EXIT_ZERO_CNT_MAX) {
		drm_dbg_kms(display->drm,
			    "exit_zero_cnt out of range (%d)\n",
			    exit_zero_cnt);
		exit_zero_cnt = ICL_EXIT_ZERO_CNT_MAX;
	}

	/* clock lane dphy timings */
	intel_dsi->dphy_reg = (CLK_PREPARE_OVERRIDE |
			       CLK_PREPARE(prepare_cnt) |
			       CLK_ZERO_OVERRIDE |
			       CLK_ZERO(clk_zero_cnt) |
			       CLK_PRE_OVERRIDE |
			       CLK_PRE(tclk_pre_cnt) |
			       CLK_TRAIL_OVERRIDE |
			       CLK_TRAIL(trail_cnt));

	/* data lanes dphy timings */
	intel_dsi->dphy_data_lane_reg = (HS_PREPARE_OVERRIDE |
					 HS_PREPARE(prepare_cnt) |
					 HS_ZERO_OVERRIDE |
					 HS_ZERO(hs_zero_cnt) |
					 HS_TRAIL_OVERRIDE |
					 HS_TRAIL(trail_cnt) |
					 HS_EXIT_OVERRIDE |
					 HS_EXIT(exit_zero_cnt));

	intel_dsi_log_params(intel_dsi);
}

static void icl_dsi_add_properties(struct intel_connector *connector)
{
	const struct drm_display_mode *fixed_mode =
		intel_panel_preferred_fixed_mode(connector);

	intel_attach_scaling_mode_property(&connector->base);

	drm_connector_set_panel_orientation_with_quirk(&connector->base,
						       intel_dsi_get_panel_orientation(connector),
						       fixed_mode->hdisplay,
						       fixed_mode->vdisplay);
}

void icl_dsi_init(struct intel_display *display,
		  const struct intel_bios_encoder_data *devdata)
{
	struct intel_dsi *intel_dsi;
	struct intel_encoder *encoder;
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	enum port port;

	port = intel_bios_encoder_port(devdata);
	if (port == PORT_NONE)
		return;

	intel_dsi = kzalloc(sizeof(*intel_dsi), GFP_KERNEL);
	if (!intel_dsi)
		return;

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(intel_dsi);
		return;
	}

	encoder = &intel_dsi->base;
	intel_dsi->attached_connector = intel_connector;
	connector = &intel_connector->base;

	encoder->devdata = devdata;

	/* register DSI encoder with DRM subsystem */
	drm_encoder_init(display->drm, &encoder->base,
			 &gen11_dsi_encoder_funcs,
			 DRM_MODE_ENCODER_DSI, "DSI %c", port_name(port));

	encoder->pre_pll_enable = gen11_dsi_pre_pll_enable;
	encoder->pre_enable = gen11_dsi_pre_enable;
	encoder->enable = gen11_dsi_enable;
	encoder->disable = gen11_dsi_disable;
	encoder->post_disable = gen11_dsi_post_disable;
	encoder->port = port;
	encoder->get_config = gen11_dsi_get_config;
	encoder->sync_state = gen11_dsi_sync_state;
	encoder->update_pipe = intel_backlight_update;
	encoder->compute_config = gen11_dsi_compute_config;
	encoder->get_hw_state = gen11_dsi_get_hw_state;
	encoder->initial_fastset_check = gen11_dsi_initial_fastset_check;
	encoder->type = INTEL_OUTPUT_DSI;
	encoder->cloneable = 0;
	encoder->pipe_mask = ~0;
	encoder->power_domain = POWER_DOMAIN_PORT_DSI;
	encoder->get_power_domains = gen11_dsi_get_power_domains;
	encoder->disable_clock = gen11_dsi_gate_clocks;
	encoder->is_clock_enabled = gen11_dsi_is_clock_enabled;
	encoder->shutdown = intel_dsi_shutdown;

	/* register DSI connector with DRM subsystem */
	drm_connector_init(display->drm, connector,
			   &gen11_dsi_connector_funcs,
			   DRM_MODE_CONNECTOR_DSI);
	drm_connector_helper_add(connector, &gen11_dsi_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	intel_connector->get_hw_state = intel_connector_get_hw_state;

	/* attach connector to encoder */
	intel_connector_attach_encoder(intel_connector, encoder);

	intel_dsi->panel_power_off_time = ktime_get_boottime();

	intel_bios_init_panel_late(display, &intel_connector->panel, encoder->devdata, NULL);

	mutex_lock(&display->drm->mode_config.mutex);
	intel_panel_add_vbt_lfp_fixed_mode(intel_connector);
	mutex_unlock(&display->drm->mode_config.mutex);

	if (!intel_panel_preferred_fixed_mode(intel_connector)) {
		drm_err(display->drm, "DSI fixed mode info missing\n");
		goto err;
	}

	intel_panel_init(intel_connector, NULL);

	intel_backlight_setup(intel_connector, INVALID_PIPE);

	if (intel_connector->panel.vbt.dsi.config->dual_link)
		intel_dsi->ports = BIT(PORT_A) | BIT(PORT_B);
	else
		intel_dsi->ports = BIT(port);

	if (drm_WARN_ON(display->drm, intel_connector->panel.vbt.dsi.bl_ports & ~intel_dsi->ports))
		intel_connector->panel.vbt.dsi.bl_ports &= intel_dsi->ports;

	if (drm_WARN_ON(display->drm, intel_connector->panel.vbt.dsi.cabc_ports & ~intel_dsi->ports))
		intel_connector->panel.vbt.dsi.cabc_ports &= intel_dsi->ports;

	for_each_dsi_port(port, intel_dsi->ports) {
		struct intel_dsi_host *host;

		host = intel_dsi_host_init(intel_dsi, &gen11_dsi_host_ops, port);
		if (!host)
			goto err;

		intel_dsi->dsi_hosts[port] = host;
	}

	if (!intel_dsi_vbt_init(intel_dsi, MIPI_DSI_GENERIC_PANEL_ID)) {
		drm_dbg_kms(display->drm, "no device found\n");
		goto err;
	}

	icl_dphy_param_init(intel_dsi);

	icl_dsi_add_properties(intel_connector);
	return;

err:
	drm_connector_cleanup(connector);
	drm_encoder_cleanup(&encoder->base);
	kfree(intel_dsi);
	kfree(intel_connector);
}
