/*
 * Copyright © 2008 Intel Corporation
 *             2014 Red Hat Inc.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_fixed.h>
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_audio.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_driver.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_hdcp.h"
#include "intel_dp_mst.h"
#include "intel_dp_tunnel.h"
#include "intel_dp_link_training.h"
#include "intel_dpio_phy.h"
#include "intel_hdcp.h"
#include "intel_hotplug.h"
#include "intel_link_bw.h"
#include "intel_psr.h"
#include "intel_vdsc.h"
#include "skl_scaler.h"

static int intel_dp_mst_max_dpt_bpp(const struct intel_crtc_state *crtc_state,
				    bool dsc)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	if (!intel_dp_is_uhbr(crtc_state) || DISPLAY_VER(i915) >= 20 || !dsc)
		return INT_MAX;

	/*
	 * DSC->DPT interface width:
	 *   ICL-MTL: 72 bits (each branch has 72 bits, only left branch is used)
	 *   LNL+:    144 bits (not a bottleneck in any config)
	 *
	 * Bspec/49259 suggests that the FEC overhead needs to be
	 * applied here, though HW people claim that neither this FEC
	 * or any other overhead is applicable here (that is the actual
	 * available_bw is just symbol_clock * 72). However based on
	 * testing on MTL-P the
	 * - DELL U3224KBA display
	 * - Unigraf UCD-500 CTS test sink
	 * devices the
	 * - 5120x2880/995.59Mhz
	 * - 6016x3384/1357.23Mhz
	 * - 6144x3456/1413.39Mhz
	 * modes (all the ones having a DPT limit on the above devices),
	 * both the channel coding efficiency and an additional 3%
	 * overhead needs to be accounted for.
	 */
	return div64_u64(mul_u32_u32(intel_dp_link_symbol_clock(crtc_state->port_clock) * 72,
				     drm_dp_bw_channel_coding_efficiency(true)),
			 mul_u32_u32(adjusted_mode->crtc_clock, 1030000));
}

static int intel_dp_mst_bw_overhead(const struct intel_crtc_state *crtc_state,
				    const struct intel_connector *connector,
				    bool ssc, int dsc_slice_count, int bpp_x16)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	unsigned long flags = DRM_DP_BW_OVERHEAD_MST;
	int overhead;

	flags |= intel_dp_is_uhbr(crtc_state) ? DRM_DP_BW_OVERHEAD_UHBR : 0;
	flags |= ssc ? DRM_DP_BW_OVERHEAD_SSC_REF_CLK : 0;
	flags |= crtc_state->fec_enable ? DRM_DP_BW_OVERHEAD_FEC : 0;

	if (dsc_slice_count)
		flags |= DRM_DP_BW_OVERHEAD_DSC;

	overhead = drm_dp_bw_overhead(crtc_state->lane_count,
				      adjusted_mode->hdisplay,
				      dsc_slice_count,
				      bpp_x16,
				      flags);

	/*
	 * TODO: clarify whether a minimum required by the fixed FEC overhead
	 * in the bspec audio programming sequence is required here.
	 */
	return max(overhead, intel_dp_bw_fec_overhead(crtc_state->fec_enable));
}

static void intel_dp_mst_compute_m_n(const struct intel_crtc_state *crtc_state,
				     const struct intel_connector *connector,
				     int overhead,
				     int bpp_x16,
				     struct intel_link_m_n *m_n)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	/* TODO: Check WA 14013163432 to set data M/N for full BW utilization. */
	intel_link_compute_m_n(bpp_x16, crtc_state->lane_count,
			       adjusted_mode->crtc_clock,
			       crtc_state->port_clock,
			       overhead,
			       m_n);

	m_n->tu = DIV_ROUND_UP_ULL(mul_u32_u32(m_n->data_m, 64), m_n->data_n);
}

static int intel_dp_mst_calc_pbn(int pixel_clock, int bpp_x16, int bw_overhead)
{
	int effective_data_rate =
		intel_dp_effective_data_rate(pixel_clock, bpp_x16, bw_overhead);

	/*
	 * TODO: Use drm_dp_calc_pbn_mode() instead, once it's converted
	 * to calculate PBN with the BW overhead passed to it.
	 */
	return DIV_ROUND_UP(effective_data_rate * 64, 54 * 1000);
}

static int intel_dp_mst_dsc_get_slice_count(const struct intel_connector *connector,
					    const struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int num_joined_pipes = crtc_state->joiner_pipes;

	return intel_dp_dsc_get_slice_count(connector,
					    adjusted_mode->clock,
					    adjusted_mode->hdisplay,
					    num_joined_pipes);
}

static int intel_dp_mst_find_vcpi_slots_for_bpp(struct intel_encoder *encoder,
						struct intel_crtc_state *crtc_state,
						int max_bpp,
						int min_bpp,
						struct link_config_limits *limits,
						struct drm_connector_state *conn_state,
						int step,
						bool dsc)
{
	struct drm_atomic_state *state = crtc_state->uapi.state;
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	struct drm_dp_mst_topology_state *mst_state;
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	int bpp, slots = -EINVAL;
	int dsc_slice_count = 0;
	int max_dpt_bpp;
	int ret = 0;

	mst_state = drm_atomic_get_mst_topology_state(state, &intel_dp->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	crtc_state->lane_count = limits->max_lane_count;
	crtc_state->port_clock = limits->max_rate;

	if (dsc) {
		if (!intel_dp_supports_fec(intel_dp, connector, crtc_state))
			return -EINVAL;

		crtc_state->fec_enable = !intel_dp_is_uhbr(crtc_state);
	}

	mst_state->pbn_div = drm_dp_get_vc_payload_bw(&intel_dp->mst_mgr,
						      crtc_state->port_clock,
						      crtc_state->lane_count);

	max_dpt_bpp = intel_dp_mst_max_dpt_bpp(crtc_state, dsc);
	if (max_bpp > max_dpt_bpp) {
		drm_dbg_kms(&i915->drm, "Limiting bpp to max DPT bpp (%d -> %d)\n",
			    max_bpp, max_dpt_bpp);
		max_bpp = max_dpt_bpp;
	}

	drm_dbg_kms(&i915->drm, "Looking for slots in range min bpp %d max bpp %d\n",
		    min_bpp, max_bpp);

	if (dsc) {
		dsc_slice_count = intel_dp_mst_dsc_get_slice_count(connector, crtc_state);
		if (!dsc_slice_count) {
			drm_dbg_kms(&i915->drm, "Can't get valid DSC slice count\n");

			return -ENOSPC;
		}
	}

	for (bpp = max_bpp; bpp >= min_bpp; bpp -= step) {
		int local_bw_overhead;
		int remote_bw_overhead;
		int link_bpp_x16;
		int remote_tu;
		fixed20_12 pbn;

		drm_dbg_kms(&i915->drm, "Trying bpp %d\n", bpp);

		link_bpp_x16 = fxp_q4_from_int(dsc ? bpp :
					       intel_dp_output_bpp(crtc_state->output_format, bpp));

		local_bw_overhead = intel_dp_mst_bw_overhead(crtc_state, connector,
							     false, dsc_slice_count, link_bpp_x16);
		remote_bw_overhead = intel_dp_mst_bw_overhead(crtc_state, connector,
							      true, dsc_slice_count, link_bpp_x16);

		intel_dp_mst_compute_m_n(crtc_state, connector,
					 local_bw_overhead,
					 link_bpp_x16,
					 &crtc_state->dp_m_n);

		/*
		 * The TU size programmed to the HW determines which slots in
		 * an MTP frame are used for this stream, which needs to match
		 * the payload size programmed to the first downstream branch
		 * device's payload table.
		 *
		 * Note that atm the payload's PBN value DRM core sends via
		 * the ALLOCATE_PAYLOAD side-band message matches the payload
		 * size (which it calculates from the PBN value) it programs
		 * to the first branch device's payload table. The allocation
		 * in the payload table could be reduced though (to
		 * crtc_state->dp_m_n.tu), provided that the driver doesn't
		 * enable SSC on the corresponding link.
		 */
		pbn.full = dfixed_const(intel_dp_mst_calc_pbn(adjusted_mode->crtc_clock,
							      link_bpp_x16,
							      remote_bw_overhead));
		remote_tu = DIV_ROUND_UP(pbn.full, mst_state->pbn_div.full);

		/*
		 * Aligning the TUs ensures that symbols consisting of multiple
		 * (4) symbol cycles don't get split between two consecutive
		 * MTPs, as required by Bspec.
		 * TODO: remove the alignment restriction for 128b/132b links
		 * on some platforms, where Bspec allows this.
		 */
		remote_tu = ALIGN(remote_tu, 4 / crtc_state->lane_count);

		/*
		 * Also align PBNs accordingly, since MST core will derive its
		 * own copy of TU from the PBN in drm_dp_atomic_find_time_slots().
		 * The above comment about the difference between the PBN
		 * allocated for the whole path and the TUs allocated for the
		 * first branch device's link also applies here.
		 */
		pbn.full = remote_tu * mst_state->pbn_div.full;
		crtc_state->pbn = dfixed_trunc(pbn);

		drm_WARN_ON(&i915->drm, remote_tu < crtc_state->dp_m_n.tu);
		crtc_state->dp_m_n.tu = remote_tu;

		slots = drm_dp_atomic_find_time_slots(state, &intel_dp->mst_mgr,
						      connector->port,
						      crtc_state->pbn);
		if (slots == -EDEADLK)
			return slots;

		if (slots >= 0) {
			drm_WARN_ON(&i915->drm, slots != crtc_state->dp_m_n.tu);

			break;
		}
	}

	/* We failed to find a proper bpp/timeslots, return error */
	if (ret)
		slots = ret;

	if (slots < 0) {
		drm_dbg_kms(&i915->drm, "failed finding vcpi slots:%d\n",
			    slots);
	} else {
		if (!dsc)
			crtc_state->pipe_bpp = bpp;
		else
			crtc_state->dsc.compressed_bpp_x16 = fxp_q4_from_int(bpp);
		drm_dbg_kms(&i915->drm, "Got %d slots for pipe bpp %d dsc %d\n", slots, bpp, dsc);
	}

	return slots;
}

static int intel_dp_mst_compute_link_config(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state,
					    struct link_config_limits *limits)
{
	int slots = -EINVAL;

	/*
	 * FIXME: allocate the BW according to link_bpp, which in the case of
	 * YUV420 is only half of the pipe bpp value.
	 */
	slots = intel_dp_mst_find_vcpi_slots_for_bpp(encoder, crtc_state,
						     fxp_q4_to_int(limits->link.max_bpp_x16),
						     fxp_q4_to_int(limits->link.min_bpp_x16),
						     limits,
						     conn_state, 2 * 3, false);

	if (slots < 0)
		return slots;

	return 0;
}

static int intel_dp_dsc_mst_compute_link_config(struct intel_encoder *encoder,
						struct intel_crtc_state *crtc_state,
						struct drm_connector_state *conn_state,
						struct link_config_limits *limits)
{
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	int slots = -EINVAL;
	int i, num_bpc;
	u8 dsc_bpc[3] = {};
	int min_bpp, max_bpp, sink_min_bpp, sink_max_bpp;
	u8 dsc_max_bpc;
	int min_compressed_bpp, max_compressed_bpp;

	/* Max DSC Input BPC for ICL is 10 and for TGL+ is 12 */
	if (DISPLAY_VER(i915) >= 12)
		dsc_max_bpc = min_t(u8, 12, conn_state->max_requested_bpc);
	else
		dsc_max_bpc = min_t(u8, 10, conn_state->max_requested_bpc);

	max_bpp = min_t(u8, dsc_max_bpc * 3, limits->pipe.max_bpp);
	min_bpp = limits->pipe.min_bpp;

	num_bpc = drm_dp_dsc_sink_supported_input_bpcs(connector->dp.dsc_dpcd,
						       dsc_bpc);

	drm_dbg_kms(&i915->drm, "DSC Source supported min bpp %d max bpp %d\n",
		    min_bpp, max_bpp);

	sink_max_bpp = dsc_bpc[0] * 3;
	sink_min_bpp = sink_max_bpp;

	for (i = 1; i < num_bpc; i++) {
		if (sink_min_bpp > dsc_bpc[i] * 3)
			sink_min_bpp = dsc_bpc[i] * 3;
		if (sink_max_bpp < dsc_bpc[i] * 3)
			sink_max_bpp = dsc_bpc[i] * 3;
	}

	drm_dbg_kms(&i915->drm, "DSC Sink supported min bpp %d max bpp %d\n",
		    sink_min_bpp, sink_max_bpp);

	if (min_bpp < sink_min_bpp)
		min_bpp = sink_min_bpp;

	if (max_bpp > sink_max_bpp)
		max_bpp = sink_max_bpp;

	crtc_state->pipe_bpp = max_bpp;

	max_compressed_bpp = intel_dp_dsc_sink_max_compressed_bpp(connector,
								  crtc_state,
								  max_bpp / 3);
	max_compressed_bpp = min(max_compressed_bpp,
				 fxp_q4_to_int(limits->link.max_bpp_x16));

	min_compressed_bpp = intel_dp_dsc_sink_min_compressed_bpp(crtc_state);
	min_compressed_bpp = max(min_compressed_bpp,
				 fxp_q4_to_int_roundup(limits->link.min_bpp_x16));

	drm_dbg_kms(&i915->drm, "DSC Sink supported compressed min bpp %d compressed max bpp %d\n",
		    min_compressed_bpp, max_compressed_bpp);

	/* Align compressed bpps according to our own constraints */
	max_compressed_bpp = intel_dp_dsc_nearest_valid_bpp(i915, max_compressed_bpp,
							    crtc_state->pipe_bpp);
	min_compressed_bpp = intel_dp_dsc_nearest_valid_bpp(i915, min_compressed_bpp,
							    crtc_state->pipe_bpp);

	slots = intel_dp_mst_find_vcpi_slots_for_bpp(encoder, crtc_state, max_compressed_bpp,
						     min_compressed_bpp, limits,
						     conn_state, 1, true);

	if (slots < 0)
		return slots;

	return 0;
}
static int intel_dp_mst_update_slots(struct intel_encoder *encoder,
				     struct intel_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	struct drm_dp_mst_topology_mgr *mgr = &intel_dp->mst_mgr;
	struct drm_dp_mst_topology_state *topology_state;
	u8 link_coding_cap = intel_dp_is_uhbr(crtc_state) ?
		DP_CAP_ANSI_128B132B : DP_CAP_ANSI_8B10B;

	topology_state = drm_atomic_get_mst_topology_state(conn_state->state, mgr);
	if (IS_ERR(topology_state)) {
		drm_dbg_kms(&i915->drm, "slot update failed\n");
		return PTR_ERR(topology_state);
	}

	drm_dp_mst_update_slots(topology_state, link_coding_cap);

	return 0;
}

static int mode_hblank_period_ns(const struct drm_display_mode *mode)
{
	return DIV_ROUND_CLOSEST_ULL(mul_u32_u32(mode->htotal - mode->hdisplay,
						 NSEC_PER_SEC / 1000),
				     mode->crtc_clock);
}

static bool
hblank_expansion_quirk_needs_dsc(const struct intel_connector *connector,
				 const struct intel_crtc_state *crtc_state,
				 const struct link_config_limits *limits)
{
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;
	bool is_uhbr_sink = connector->mst_port &&
			    drm_dp_128b132b_supported(connector->mst_port->dpcd);
	int hblank_limit = is_uhbr_sink ? 500 : 300;

	if (!connector->dp.dsc_hblank_expansion_quirk)
		return false;

	if (is_uhbr_sink && !drm_dp_is_uhbr_rate(limits->max_rate))
		return false;

	if (mode_hblank_period_ns(adjusted_mode) > hblank_limit)
		return false;

	if (!intel_dp_mst_dsc_get_slice_count(connector, crtc_state))
		return false;

	return true;
}

static bool
adjust_limits_for_dsc_hblank_expansion_quirk(const struct intel_connector *connector,
					     const struct intel_crtc_state *crtc_state,
					     struct link_config_limits *limits,
					     bool dsc)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	const struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	int min_bpp_x16 = limits->link.min_bpp_x16;

	if (!hblank_expansion_quirk_needs_dsc(connector, crtc_state, limits))
		return true;

	if (!dsc) {
		if (intel_dp_supports_dsc(connector, crtc_state)) {
			drm_dbg_kms(&i915->drm,
				    "[CRTC:%d:%s][CONNECTOR:%d:%s] DSC needed by hblank expansion quirk\n",
				    crtc->base.base.id, crtc->base.name,
				    connector->base.base.id, connector->base.name);
			return false;
		}

		drm_dbg_kms(&i915->drm,
			    "[CRTC:%d:%s][CONNECTOR:%d:%s] Increasing link min bpp to 24 due to hblank expansion quirk\n",
			    crtc->base.base.id, crtc->base.name,
			    connector->base.base.id, connector->base.name);

		if (limits->link.max_bpp_x16 < fxp_q4_from_int(24))
			return false;

		limits->link.min_bpp_x16 = fxp_q4_from_int(24);

		return true;
	}

	drm_WARN_ON(&i915->drm, limits->min_rate != limits->max_rate);

	if (limits->max_rate < 540000)
		min_bpp_x16 = fxp_q4_from_int(13);
	else if (limits->max_rate < 810000)
		min_bpp_x16 = fxp_q4_from_int(10);

	if (limits->link.min_bpp_x16 >= min_bpp_x16)
		return true;

	drm_dbg_kms(&i915->drm,
		    "[CRTC:%d:%s][CONNECTOR:%d:%s] Increasing link min bpp to " FXP_Q4_FMT " in DSC mode due to hblank expansion quirk\n",
		    crtc->base.base.id, crtc->base.name,
		    connector->base.base.id, connector->base.name,
		    FXP_Q4_ARGS(min_bpp_x16));

	if (limits->link.max_bpp_x16 < min_bpp_x16)
		return false;

	limits->link.min_bpp_x16 = min_bpp_x16;

	return true;
}

static bool
intel_dp_mst_compute_config_limits(struct intel_dp *intel_dp,
				   const struct intel_connector *connector,
				   struct intel_crtc_state *crtc_state,
				   bool dsc,
				   struct link_config_limits *limits)
{
	/*
	 * for MST we always configure max link bw - the spec doesn't
	 * seem to suggest we should do otherwise.
	 */
	limits->min_rate = limits->max_rate =
		intel_dp_max_link_rate(intel_dp);

	limits->min_lane_count = limits->max_lane_count =
		intel_dp_max_lane_count(intel_dp);

	limits->pipe.min_bpp = intel_dp_min_bpp(crtc_state->output_format);
	/*
	 * FIXME: If all the streams can't fit into the link with
	 * their current pipe_bpp we should reduce pipe_bpp across
	 * the board until things start to fit. Until then we
	 * limit to <= 8bpc since that's what was hardcoded for all
	 * MST streams previously. This hack should be removed once
	 * we have the proper retry logic in place.
	 */
	limits->pipe.max_bpp = min(crtc_state->pipe_bpp, 24);

	intel_dp_adjust_compliance_config(intel_dp, crtc_state, limits);

	if (!intel_dp_compute_config_link_bpp_limits(intel_dp,
						     crtc_state,
						     dsc,
						     limits))
		return false;

	return adjust_limits_for_dsc_hblank_expansion_quirk(connector,
							    crtc_state,
							    limits,
							    dsc);
}

static int intel_dp_mst_compute_config(struct intel_encoder *encoder,
				       struct intel_crtc_state *pipe_config,
				       struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_atomic_state *state = to_intel_atomic_state(conn_state->state);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	const struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	struct link_config_limits limits;
	bool dsc_needed, joiner_needs_dsc;
	int ret = 0;

	if (pipe_config->fec_enable &&
	    !intel_dp_supports_fec(intel_dp, connector, pipe_config))
		return -EINVAL;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	if (intel_dp_need_joiner(intel_dp, connector,
				 adjusted_mode->crtc_hdisplay,
				 adjusted_mode->crtc_clock))
		pipe_config->joiner_pipes = GENMASK(crtc->pipe + 1, crtc->pipe);

	pipe_config->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->has_pch_encoder = false;

	joiner_needs_dsc = intel_dp_joiner_needs_dsc(dev_priv, pipe_config->joiner_pipes);

	dsc_needed = joiner_needs_dsc || intel_dp->force_dsc_en ||
		     !intel_dp_mst_compute_config_limits(intel_dp,
							 connector,
							 pipe_config,
							 false,
							 &limits);

	if (!dsc_needed) {
		ret = intel_dp_mst_compute_link_config(encoder, pipe_config,
						       conn_state, &limits);

		if (ret == -EDEADLK)
			return ret;

		if (ret)
			dsc_needed = true;
	}

	/* enable compression if the mode doesn't fit available BW */
	if (dsc_needed) {
		drm_dbg_kms(&dev_priv->drm, "Try DSC (fallback=%s, joiner=%s, force=%s)\n",
			    str_yes_no(ret), str_yes_no(joiner_needs_dsc),
			    str_yes_no(intel_dp->force_dsc_en));

		if (!intel_dp_supports_dsc(connector, pipe_config))
			return -EINVAL;

		if (!intel_dp_mst_compute_config_limits(intel_dp,
							connector,
							pipe_config,
							true,
							&limits))
			return -EINVAL;

		/*
		 * FIXME: As bpc is hardcoded to 8, as mentioned above,
		 * WARN and ignore the debug flag force_dsc_bpc for now.
		 */
		drm_WARN(&dev_priv->drm, intel_dp->force_dsc_bpc, "Cannot Force BPC for MST\n");
		/*
		 * Try to get at least some timeslots and then see, if
		 * we can fit there with DSC.
		 */
		drm_dbg_kms(&dev_priv->drm, "Trying to find VCPI slots in DSC mode\n");

		ret = intel_dp_dsc_mst_compute_link_config(encoder, pipe_config,
							   conn_state, &limits);
		if (ret < 0)
			return ret;

		ret = intel_dp_dsc_compute_config(intel_dp, pipe_config,
						  conn_state, &limits,
						  pipe_config->dp_m_n.tu, false);
	}

	if (ret)
		return ret;

	ret = intel_dp_mst_update_slots(encoder, pipe_config, conn_state);
	if (ret)
		return ret;

	pipe_config->limited_color_range =
		intel_dp_limited_color_range(pipe_config, conn_state);

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_dpio_phy_calc_lane_lat_optim_mask(pipe_config->lane_count);

	intel_dp_audio_compute_config(encoder, pipe_config, conn_state);

	intel_ddi_compute_min_voltage_level(pipe_config);

	intel_psr_compute_config(intel_dp, pipe_config, conn_state);

	return intel_dp_tunnel_atomic_compute_stream_bw(state, intel_dp, connector,
							pipe_config);
}

/*
 * Iterate over all connectors and return a mask of
 * all CPU transcoders streaming over the same DP link.
 */
static unsigned int
intel_dp_mst_transcoder_mask(struct intel_atomic_state *state,
			     struct intel_dp *mst_port)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	const struct intel_digital_connector_state *conn_state;
	struct intel_connector *connector;
	u8 transcoders = 0;
	int i;

	if (DISPLAY_VER(dev_priv) < 12)
		return 0;

	for_each_new_intel_connector_in_state(state, connector, conn_state, i) {
		const struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (connector->mst_port != mst_port || !conn_state->base.crtc)
			continue;

		crtc = to_intel_crtc(conn_state->base.crtc);
		crtc_state = intel_atomic_get_new_crtc_state(state, crtc);

		if (!crtc_state->hw.active)
			continue;

		transcoders |= BIT(crtc_state->cpu_transcoder);
	}

	return transcoders;
}

static u8 get_pipes_downstream_of_mst_port(struct intel_atomic_state *state,
					   struct drm_dp_mst_topology_mgr *mst_mgr,
					   struct drm_dp_mst_port *parent_port)
{
	const struct intel_digital_connector_state *conn_state;
	struct intel_connector *connector;
	u8 mask = 0;
	int i;

	for_each_new_intel_connector_in_state(state, connector, conn_state, i) {
		if (!conn_state->base.crtc)
			continue;

		if (&connector->mst_port->mst_mgr != mst_mgr)
			continue;

		if (connector->port != parent_port &&
		    !drm_dp_mst_port_downstream_of_parent(mst_mgr,
							  connector->port,
							  parent_port))
			continue;

		mask |= BIT(to_intel_crtc(conn_state->base.crtc)->pipe);
	}

	return mask;
}

static int intel_dp_mst_check_fec_change(struct intel_atomic_state *state,
					 struct drm_dp_mst_topology_mgr *mst_mgr,
					 struct intel_link_bw_limits *limits)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	struct intel_crtc *crtc;
	u8 mst_pipe_mask;
	u8 fec_pipe_mask = 0;
	int ret;

	mst_pipe_mask = get_pipes_downstream_of_mst_port(state, mst_mgr, NULL);

	for_each_intel_crtc_in_pipe_mask(&i915->drm, crtc, mst_pipe_mask) {
		struct intel_crtc_state *crtc_state =
			intel_atomic_get_new_crtc_state(state, crtc);

		/* Atomic connector check should've added all the MST CRTCs. */
		if (drm_WARN_ON(&i915->drm, !crtc_state))
			return -EINVAL;

		if (crtc_state->fec_enable)
			fec_pipe_mask |= BIT(crtc->pipe);
	}

	if (!fec_pipe_mask || mst_pipe_mask == fec_pipe_mask)
		return 0;

	limits->force_fec_pipes |= mst_pipe_mask;

	ret = intel_modeset_pipes_in_mask_early(state, "MST FEC",
						mst_pipe_mask);

	return ret ? : -EAGAIN;
}

static int intel_dp_mst_check_bw(struct intel_atomic_state *state,
				 struct drm_dp_mst_topology_mgr *mst_mgr,
				 struct drm_dp_mst_topology_state *mst_state,
				 struct intel_link_bw_limits *limits)
{
	struct drm_dp_mst_port *mst_port;
	u8 mst_port_pipes;
	int ret;

	ret = drm_dp_mst_atomic_check_mgr(&state->base, mst_mgr, mst_state, &mst_port);
	if (ret != -ENOSPC)
		return ret;

	mst_port_pipes = get_pipes_downstream_of_mst_port(state, mst_mgr, mst_port);

	ret = intel_link_bw_reduce_bpp(state, limits,
				       mst_port_pipes, "MST link BW");

	return ret ? : -EAGAIN;
}

/**
 * intel_dp_mst_atomic_check_link - check all modeset MST link configuration
 * @state: intel atomic state
 * @limits: link BW limits
 *
 * Check the link configuration for all modeset MST outputs. If the
 * configuration is invalid @limits will be updated if possible to
 * reduce the total BW, after which the configuration for all CRTCs in
 * @state must be recomputed with the updated @limits.
 *
 * Returns:
 *   - 0 if the confugration is valid
 *   - %-EAGAIN, if the configuration is invalid and @limits got updated
 *     with fallback values with which the configuration of all CRTCs in
 *     @state must be recomputed
 *   - Other negative error, if the configuration is invalid without a
 *     fallback possibility, or the check failed for another reason
 */
int intel_dp_mst_atomic_check_link(struct intel_atomic_state *state,
				   struct intel_link_bw_limits *limits)
{
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_topology_state *mst_state;
	int ret;
	int i;

	for_each_new_mst_mgr_in_state(&state->base, mgr, mst_state, i) {
		ret = intel_dp_mst_check_fec_change(state, mgr, limits);
		if (ret)
			return ret;

		ret = intel_dp_mst_check_bw(state, mgr, mst_state,
					    limits);
		if (ret)
			return ret;
	}

	return 0;
}

static int intel_dp_mst_compute_config_late(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state)
{
	struct intel_atomic_state *state = to_intel_atomic_state(conn_state->state);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;

	/* lowest numbered transcoder will be designated master */
	crtc_state->mst_master_transcoder =
		ffs(intel_dp_mst_transcoder_mask(state, intel_dp)) - 1;

	return 0;
}

/*
 * If one of the connectors in a MST stream needs a modeset, mark all CRTCs
 * that shares the same MST stream as mode changed,
 * intel_modeset_pipe_config()+intel_crtc_check_fastset() will take care to do
 * a fastset when possible.
 *
 * On TGL+ this is required since each stream go through a master transcoder,
 * so if the master transcoder needs modeset, all other streams in the
 * topology need a modeset. All platforms need to add the atomic state
 * for all streams in the topology, since a modeset on one may require
 * changing the MST link BW usage of the others, which in turn needs a
 * recomputation of the corresponding CRTC states.
 */
static int
intel_dp_mst_atomic_topology_check(struct intel_connector *connector,
				   struct intel_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->base.dev);
	struct drm_connector_list_iter connector_list_iter;
	struct intel_connector *connector_iter;
	int ret = 0;

	if (!intel_connector_needs_modeset(state, &connector->base))
		return 0;

	drm_connector_list_iter_begin(&dev_priv->drm, &connector_list_iter);
	for_each_intel_connector_iter(connector_iter, &connector_list_iter) {
		struct intel_digital_connector_state *conn_iter_state;
		struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (connector_iter->mst_port != connector->mst_port ||
		    connector_iter == connector)
			continue;

		conn_iter_state = intel_atomic_get_digital_connector_state(state,
									   connector_iter);
		if (IS_ERR(conn_iter_state)) {
			ret = PTR_ERR(conn_iter_state);
			break;
		}

		if (!conn_iter_state->base.crtc)
			continue;

		crtc = to_intel_crtc(conn_iter_state->base.crtc);
		crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
		if (IS_ERR(crtc_state)) {
			ret = PTR_ERR(crtc_state);
			break;
		}

		ret = drm_atomic_add_affected_planes(&state->base, &crtc->base);
		if (ret)
			break;
		crtc_state->uapi.mode_changed = true;
	}
	drm_connector_list_iter_end(&connector_list_iter);

	return ret;
}

static int
intel_dp_mst_atomic_check(struct drm_connector *connector,
			  struct drm_atomic_state *_state)
{
	struct intel_atomic_state *state = to_intel_atomic_state(_state);
	struct intel_connector *intel_connector =
		to_intel_connector(connector);
	int ret;

	ret = intel_digital_connector_atomic_check(connector, &state->base);
	if (ret)
		return ret;

	ret = intel_dp_mst_atomic_topology_check(intel_connector, state);
	if (ret)
		return ret;

	if (intel_connector_needs_modeset(state, connector)) {
		ret = intel_dp_tunnel_atomic_check_state(state,
							 intel_connector->mst_port,
							 intel_connector);
		if (ret)
			return ret;
	}

	return drm_dp_atomic_release_time_slots(&state->base,
						&intel_connector->mst_port->mst_mgr,
						intel_connector->port);
}

static void clear_act_sent(struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);

	intel_de_write(i915, dp_tp_status_reg(encoder, crtc_state),
		       DP_TP_STATUS_ACT_SENT);
}

static void wait_for_act_sent(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_dp *intel_dp = &intel_mst->primary->dp;

	if (intel_de_wait_for_set(i915, dp_tp_status_reg(encoder, crtc_state),
				  DP_TP_STATUS_ACT_SENT, 1))
		drm_err(&i915->drm, "Timed out waiting for ACT sent\n");

	drm_dp_check_act_status(&intel_dp->mst_mgr);
}

static void intel_mst_disable_dp(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->connector);
	struct drm_i915_private *i915 = to_i915(connector->base.dev);

	drm_dbg_kms(&i915->drm, "active links %d\n",
		    intel_dp->active_mst_links);

	if (intel_dp->active_mst_links == 1)
		intel_dp->link_trained = false;

	intel_hdcp_disable(intel_mst->connector);

	intel_dp_sink_disable_decompression(state, connector, old_crtc_state);
}

static void intel_mst_post_disable_dp(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->connector);
	struct drm_dp_mst_topology_state *old_mst_state =
		drm_atomic_get_old_mst_topology_state(&state->base, &intel_dp->mst_mgr);
	struct drm_dp_mst_topology_state *new_mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst_mgr);
	const struct drm_dp_mst_atomic_payload *old_payload =
		drm_atomic_get_mst_payload_state(old_mst_state, connector->port);
	struct drm_dp_mst_atomic_payload *new_payload =
		drm_atomic_get_mst_payload_state(new_mst_state, connector->port);
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	struct intel_crtc *pipe_crtc;
	bool last_mst_stream;

	intel_dp->active_mst_links--;
	last_mst_stream = intel_dp->active_mst_links == 0;
	drm_WARN_ON(&dev_priv->drm,
		    DISPLAY_VER(dev_priv) >= 12 && last_mst_stream &&
		    !intel_dp_mst_is_master_trans(old_crtc_state));

	for_each_intel_crtc_in_pipe_mask(&dev_priv->drm, pipe_crtc,
					 intel_crtc_joined_pipe_mask(old_crtc_state)) {
		const struct intel_crtc_state *old_pipe_crtc_state =
			intel_atomic_get_old_crtc_state(state, pipe_crtc);

		intel_crtc_vblank_off(old_pipe_crtc_state);
	}

	intel_disable_transcoder(old_crtc_state);

	drm_dp_remove_payload_part1(&intel_dp->mst_mgr, new_mst_state, new_payload);

	clear_act_sent(encoder, old_crtc_state);

	intel_de_rmw(dev_priv,
		     TRANS_DDI_FUNC_CTL(dev_priv, old_crtc_state->cpu_transcoder),
		     TRANS_DDI_DP_VC_PAYLOAD_ALLOC, 0);

	wait_for_act_sent(encoder, old_crtc_state);

	drm_dp_remove_payload_part2(&intel_dp->mst_mgr, new_mst_state,
				    old_payload, new_payload);

	intel_ddi_disable_transcoder_func(old_crtc_state);

	for_each_intel_crtc_in_pipe_mask(&dev_priv->drm, pipe_crtc,
					 intel_crtc_joined_pipe_mask(old_crtc_state)) {
		const struct intel_crtc_state *old_pipe_crtc_state =
			intel_atomic_get_old_crtc_state(state, pipe_crtc);

		intel_dsc_disable(old_pipe_crtc_state);

		if (DISPLAY_VER(dev_priv) >= 9)
			skl_scaler_disable(old_pipe_crtc_state);
		else
			ilk_pfit_disable(old_pipe_crtc_state);
	}

	/*
	 * Power down mst path before disabling the port, otherwise we end
	 * up getting interrupts from the sink upon detecting link loss.
	 */
	drm_dp_send_power_updown_phy(&intel_dp->mst_mgr, connector->port,
				     false);

	/*
	 * BSpec 4287: disable DIP after the transcoder is disabled and before
	 * the transcoder clock select is set to none.
	 */
	intel_dp_set_infoframes(&dig_port->base, false,
				old_crtc_state, NULL);
	/*
	 * From TGL spec: "If multi-stream slave transcoder: Configure
	 * Transcoder Clock Select to direct no clock to the transcoder"
	 *
	 * From older GENs spec: "Configure Transcoder Clock Select to direct
	 * no clock to the transcoder"
	 */
	if (DISPLAY_VER(dev_priv) < 12 || !last_mst_stream)
		intel_ddi_disable_transcoder_clock(old_crtc_state);


	intel_mst->connector = NULL;
	if (last_mst_stream)
		dig_port->base.post_disable(state, &dig_port->base,
						  old_crtc_state, NULL);

	drm_dbg_kms(&dev_priv->drm, "active links %d\n",
		    intel_dp->active_mst_links);
}

static void intel_mst_post_pll_disable_dp(struct intel_atomic_state *state,
					  struct intel_encoder *encoder,
					  const struct intel_crtc_state *old_crtc_state,
					  const struct drm_connector_state *old_conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;

	if (intel_dp->active_mst_links == 0 &&
	    dig_port->base.post_pll_disable)
		dig_port->base.post_pll_disable(state, encoder, old_crtc_state, old_conn_state);
}

static void intel_mst_pre_pll_enable_dp(struct intel_atomic_state *state,
					struct intel_encoder *encoder,
					const struct intel_crtc_state *pipe_config,
					const struct drm_connector_state *conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;

	if (intel_dp->active_mst_links == 0)
		dig_port->base.pre_pll_enable(state, &dig_port->base,
						    pipe_config, NULL);
	else
		/*
		 * The port PLL state needs to get updated for secondary
		 * streams as for the primary stream.
		 */
		intel_ddi_update_active_dpll(state, &dig_port->base,
					     to_intel_crtc(pipe_config->uapi.crtc));
}

static bool intel_mst_probed_link_params_valid(struct intel_dp *intel_dp,
					       int link_rate, int lane_count)
{
	return intel_dp->link.mst_probed_rate == link_rate &&
		intel_dp->link.mst_probed_lane_count == lane_count;
}

static void intel_mst_set_probed_link_params(struct intel_dp *intel_dp,
					     int link_rate, int lane_count)
{
	intel_dp->link.mst_probed_rate = link_rate;
	intel_dp->link.mst_probed_lane_count = lane_count;
}

static void intel_mst_reprobe_topology(struct intel_dp *intel_dp,
				       const struct intel_crtc_state *crtc_state)
{
	if (intel_mst_probed_link_params_valid(intel_dp,
					       crtc_state->port_clock, crtc_state->lane_count))
		return;

	drm_dp_mst_topology_queue_probe(&intel_dp->mst_mgr);

	intel_mst_set_probed_link_params(intel_dp,
					 crtc_state->port_clock, crtc_state->lane_count);
}

static void intel_mst_pre_enable_dp(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *pipe_config,
				    const struct drm_connector_state *conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct drm_dp_mst_topology_state *mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst_mgr);
	int ret;
	bool first_mst_stream;

	/* MST encoders are bound to a crtc, not to a connector,
	 * force the mapping here for get_hw_state.
	 */
	connector->encoder = encoder;
	intel_mst->connector = connector;
	first_mst_stream = intel_dp->active_mst_links == 0;
	drm_WARN_ON(&dev_priv->drm,
		    DISPLAY_VER(dev_priv) >= 12 && first_mst_stream &&
		    !intel_dp_mst_is_master_trans(pipe_config));

	drm_dbg_kms(&dev_priv->drm, "active links %d\n",
		    intel_dp->active_mst_links);

	if (first_mst_stream)
		intel_dp_set_power(intel_dp, DP_SET_POWER_D0);

	drm_dp_send_power_updown_phy(&intel_dp->mst_mgr, connector->port, true);

	intel_dp_sink_enable_decompression(state, connector, pipe_config);

	if (first_mst_stream) {
		dig_port->base.pre_enable(state, &dig_port->base,
						pipe_config, NULL);

		intel_mst_reprobe_topology(intel_dp, pipe_config);
	}

	intel_dp->active_mst_links++;

	ret = drm_dp_add_payload_part1(&intel_dp->mst_mgr, mst_state,
				       drm_atomic_get_mst_payload_state(mst_state, connector->port));
	if (ret < 0)
		intel_dp_queue_modeset_retry_for_link(state, &dig_port->base, pipe_config);

	/*
	 * Before Gen 12 this is not done as part of
	 * dig_port->base.pre_enable() and should be done here. For
	 * Gen 12+ the step in which this should be done is different for the
	 * first MST stream, so it's done on the DDI for the first stream and
	 * here for the following ones.
	 */
	if (DISPLAY_VER(dev_priv) < 12 || !first_mst_stream)
		intel_ddi_enable_transcoder_clock(encoder, pipe_config);

	intel_dsc_dp_pps_write(&dig_port->base, pipe_config);
	intel_ddi_set_dp_msa(pipe_config, conn_state);
}

static void enable_bs_jitter_was(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	u32 clear = 0;
	u32 set = 0;

	if (!IS_ALDERLAKE_P(i915))
		return;

	if (!IS_DISPLAY_STEP(i915, STEP_D0, STEP_FOREVER))
		return;

	/* Wa_14013163432:adlp */
	if (crtc_state->fec_enable || intel_dp_is_uhbr(crtc_state))
		set |= DP_MST_FEC_BS_JITTER_WA(crtc_state->cpu_transcoder);

	/* Wa_14014143976:adlp */
	if (IS_DISPLAY_STEP(i915, STEP_E0, STEP_FOREVER)) {
		if (intel_dp_is_uhbr(crtc_state))
			set |= DP_MST_SHORT_HBLANK_WA(crtc_state->cpu_transcoder);
		else if (crtc_state->fec_enable)
			clear |= DP_MST_SHORT_HBLANK_WA(crtc_state->cpu_transcoder);

		if (crtc_state->fec_enable || intel_dp_is_uhbr(crtc_state))
			set |= DP_MST_DPT_DPTP_ALIGN_WA(crtc_state->cpu_transcoder);
	}

	if (!clear && !set)
		return;

	intel_de_rmw(i915, CHICKEN_MISC_3, clear, set);
}

static void intel_mst_enable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &dig_port->dp;
	struct intel_connector *connector = to_intel_connector(conn_state->connector);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_dp_mst_topology_state *mst_state =
		drm_atomic_get_new_mst_topology_state(&state->base, &intel_dp->mst_mgr);
	enum transcoder trans = pipe_config->cpu_transcoder;
	bool first_mst_stream = intel_dp->active_mst_links == 1;
	struct intel_crtc *pipe_crtc;
	int ret;

	drm_WARN_ON(&dev_priv->drm, pipe_config->has_pch_encoder);

	if (intel_dp_is_uhbr(pipe_config)) {
		const struct drm_display_mode *adjusted_mode =
			&pipe_config->hw.adjusted_mode;
		u64 crtc_clock_hz = KHz(adjusted_mode->crtc_clock);

		intel_de_write(dev_priv, TRANS_DP2_VFREQHIGH(pipe_config->cpu_transcoder),
			       TRANS_DP2_VFREQ_PIXEL_CLOCK(crtc_clock_hz >> 24));
		intel_de_write(dev_priv, TRANS_DP2_VFREQLOW(pipe_config->cpu_transcoder),
			       TRANS_DP2_VFREQ_PIXEL_CLOCK(crtc_clock_hz & 0xffffff));
	}

	enable_bs_jitter_was(pipe_config);

	intel_ddi_enable_transcoder_func(encoder, pipe_config);

	clear_act_sent(encoder, pipe_config);

	intel_de_rmw(dev_priv, TRANS_DDI_FUNC_CTL(dev_priv, trans), 0,
		     TRANS_DDI_DP_VC_PAYLOAD_ALLOC);

	drm_dbg_kms(&dev_priv->drm, "active links %d\n",
		    intel_dp->active_mst_links);

	wait_for_act_sent(encoder, pipe_config);

	if (first_mst_stream)
		intel_ddi_wait_for_fec_status(encoder, pipe_config, true);

	ret = drm_dp_add_payload_part2(&intel_dp->mst_mgr,
				       drm_atomic_get_mst_payload_state(mst_state,
									connector->port));
	if (ret < 0)
		intel_dp_queue_modeset_retry_for_link(state, &dig_port->base, pipe_config);

	if (DISPLAY_VER(dev_priv) >= 12)
		intel_de_rmw(dev_priv, hsw_chicken_trans_reg(dev_priv, trans),
			     FECSTALL_DIS_DPTSTREAM_DPTTG,
			     pipe_config->fec_enable ? FECSTALL_DIS_DPTSTREAM_DPTTG : 0);

	intel_audio_sdp_split_update(pipe_config);

	intel_enable_transcoder(pipe_config);

	for_each_intel_crtc_in_pipe_mask_reverse(&dev_priv->drm, pipe_crtc,
						 intel_crtc_joined_pipe_mask(pipe_config)) {
		const struct intel_crtc_state *pipe_crtc_state =
			intel_atomic_get_new_crtc_state(state, pipe_crtc);

		intel_crtc_vblank_on(pipe_crtc_state);
	}

	intel_hdcp_enable(state, encoder, pipe_config, conn_state);
}

static bool intel_dp_mst_enc_get_hw_state(struct intel_encoder *encoder,
				      enum pipe *pipe)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	*pipe = intel_mst->pipe;
	if (intel_mst->connector)
		return true;
	return false;
}

static void intel_dp_mst_enc_get_config(struct intel_encoder *encoder,
					struct intel_crtc_state *pipe_config)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;

	dig_port->base.get_config(&dig_port->base, pipe_config);
}

static bool intel_dp_mst_initial_fastset_check(struct intel_encoder *encoder,
					       struct intel_crtc_state *crtc_state)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);
	struct intel_digital_port *dig_port = intel_mst->primary;

	return intel_dp_initial_fastset_check(&dig_port->base, crtc_state);
}

static int intel_dp_mst_get_ddc_modes(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_i915_private *i915 = to_i915(intel_connector->base.dev);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	const struct drm_edid *drm_edid;
	int ret;

	if (drm_connector_is_unregistered(connector))
		return intel_connector_update_modes(connector, NULL);

	if (!intel_display_driver_check_access(i915))
		return drm_edid_connector_add_modes(connector);

	drm_edid = drm_dp_mst_edid_read(connector, &intel_dp->mst_mgr, intel_connector->port);

	ret = intel_connector_update_modes(connector, drm_edid);

	drm_edid_free(drm_edid);

	return ret;
}

static int
intel_dp_mst_connector_late_register(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	int ret;

	ret = drm_dp_mst_connector_late_register(connector,
						 intel_connector->port);
	if (ret < 0)
		return ret;

	ret = intel_connector_register(connector);
	if (ret < 0)
		drm_dp_mst_connector_early_unregister(connector,
						      intel_connector->port);

	return ret;
}

static void
intel_dp_mst_connector_early_unregister(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	intel_connector_unregister(connector);
	drm_dp_mst_connector_early_unregister(connector,
					      intel_connector->port);
}

static const struct drm_connector_funcs intel_dp_mst_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_get_property = intel_digital_connector_atomic_get_property,
	.atomic_set_property = intel_digital_connector_atomic_set_property,
	.late_register = intel_dp_mst_connector_late_register,
	.early_unregister = intel_dp_mst_connector_early_unregister,
	.destroy = intel_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_digital_connector_duplicate_state,
};

static int intel_dp_mst_get_modes(struct drm_connector *connector)
{
	return intel_dp_mst_get_ddc_modes(connector);
}

static int
intel_dp_mst_mode_valid_ctx(struct drm_connector *connector,
			    struct drm_display_mode *mode,
			    struct drm_modeset_acquire_ctx *ctx,
			    enum drm_mode_status *status)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	struct drm_dp_mst_topology_mgr *mgr = &intel_dp->mst_mgr;
	struct drm_dp_mst_port *port = intel_connector->port;
	const int min_bpp = 18;
	int max_dotclk = to_i915(connector->dev)->display.cdclk.max_dotclk_freq;
	int max_rate, mode_rate, max_lanes, max_link_clock;
	int ret;
	bool dsc = false, joiner = false;
	u16 dsc_max_compressed_bpp = 0;
	u8 dsc_slice_count = 0;
	int target_clock = mode->clock;

	if (drm_connector_is_unregistered(connector)) {
		*status = MODE_ERROR;
		return 0;
	}

	*status = intel_cpu_transcoder_mode_valid(dev_priv, mode);
	if (*status != MODE_OK)
		return 0;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK) {
		*status = MODE_H_ILLEGAL;
		return 0;
	}

	if (mode->clock < 10000) {
		*status = MODE_CLOCK_LOW;
		return 0;
	}

	max_link_clock = intel_dp_max_link_rate(intel_dp);
	max_lanes = intel_dp_max_lane_count(intel_dp);

	max_rate = intel_dp_max_link_data_rate(intel_dp,
					       max_link_clock, max_lanes);
	mode_rate = intel_dp_link_required(mode->clock, min_bpp);

	/*
	 * TODO:
	 * - Also check if compression would allow for the mode
	 * - Calculate the overhead using drm_dp_bw_overhead() /
	 *   drm_dp_bw_channel_coding_efficiency(), similarly to the
	 *   compute config code, as drm_dp_calc_pbn_mode() doesn't
	 *   account with all the overheads.
	 * - Check here and during compute config the BW reported by
	 *   DFP_Link_Available_Payload_Bandwidth_Number (or the
	 *   corresponding link capabilities of the sink) in case the
	 *   stream is uncompressed for it by the last branch device.
	 */
	if (intel_dp_need_joiner(intel_dp, intel_connector,
				 mode->hdisplay, target_clock)) {
		joiner = true;
		max_dotclk *= 2;
	}

	ret = drm_modeset_lock(&mgr->base.lock, ctx);
	if (ret)
		return ret;

	if (mode_rate > max_rate || mode->clock > max_dotclk ||
	    drm_dp_calc_pbn_mode(mode->clock, min_bpp << 4) > port->full_pbn) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	if (intel_dp_has_dsc(intel_connector)) {
		/*
		 * TBD pass the connector BPC,
		 * for now U8_MAX so that max BPC on that platform would be picked
		 */
		int pipe_bpp = intel_dp_dsc_compute_max_bpp(intel_connector, U8_MAX);

		if (drm_dp_sink_supports_fec(intel_connector->dp.fec_capability)) {
			dsc_max_compressed_bpp =
				intel_dp_dsc_get_max_compressed_bpp(dev_priv,
								    max_link_clock,
								    max_lanes,
								    target_clock,
								    mode->hdisplay,
								    joiner,
								    INTEL_OUTPUT_FORMAT_RGB,
								    pipe_bpp, 64);
			dsc_slice_count =
				intel_dp_dsc_get_slice_count(intel_connector,
							     target_clock,
							     mode->hdisplay,
							     joiner);
		}

		dsc = dsc_max_compressed_bpp && dsc_slice_count;
	}

	if (intel_dp_joiner_needs_dsc(dev_priv, joiner) && !dsc) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	if (mode_rate > max_rate && !dsc) {
		*status = MODE_CLOCK_HIGH;
		return 0;
	}

	*status = intel_mode_valid_max_plane_size(dev_priv, mode, joiner);
	return 0;
}

static struct drm_encoder *intel_mst_atomic_best_encoder(struct drm_connector *connector,
							 struct drm_atomic_state *state)
{
	struct drm_connector_state *connector_state = drm_atomic_get_new_connector_state(state,
											 connector);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	struct intel_crtc *crtc = to_intel_crtc(connector_state->crtc);

	return &intel_dp->mst_encoders[crtc->pipe]->base.base;
}

static int
intel_dp_mst_detect(struct drm_connector *connector,
		    struct drm_modeset_acquire_ctx *ctx, bool force)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;

	if (!intel_display_device_enabled(i915))
		return connector_status_disconnected;

	if (drm_connector_is_unregistered(connector))
		return connector_status_disconnected;

	if (!intel_display_driver_check_access(i915))
		return connector->status;

	return drm_dp_mst_detect_port(connector, ctx, &intel_dp->mst_mgr,
				      intel_connector->port);
}

static const struct drm_connector_helper_funcs intel_dp_mst_connector_helper_funcs = {
	.get_modes = intel_dp_mst_get_modes,
	.mode_valid_ctx = intel_dp_mst_mode_valid_ctx,
	.atomic_best_encoder = intel_mst_atomic_best_encoder,
	.atomic_check = intel_dp_mst_atomic_check,
	.detect_ctx = intel_dp_mst_detect,
};

static void intel_dp_mst_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(to_intel_encoder(encoder));

	drm_encoder_cleanup(encoder);
	kfree(intel_mst);
}

static const struct drm_encoder_funcs intel_dp_mst_enc_funcs = {
	.destroy = intel_dp_mst_encoder_destroy,
};

static bool intel_dp_mst_get_hw_state(struct intel_connector *connector)
{
	if (intel_attached_encoder(connector) && connector->base.state->crtc) {
		enum pipe pipe;
		if (!intel_attached_encoder(connector)->get_hw_state(intel_attached_encoder(connector), &pipe))
			return false;
		return true;
	}
	return false;
}

static int intel_dp_mst_add_properties(struct intel_dp *intel_dp,
				       struct drm_connector *connector,
				       const char *pathprop)
{
	struct drm_i915_private *i915 = to_i915(connector->dev);

	drm_object_attach_property(&connector->base,
				   i915->drm.mode_config.path_property, 0);
	drm_object_attach_property(&connector->base,
				   i915->drm.mode_config.tile_property, 0);

	intel_attach_force_audio_property(connector);
	intel_attach_broadcast_rgb_property(connector);

	/*
	 * Reuse the prop from the SST connector because we're
	 * not allowed to create new props after device registration.
	 */
	connector->max_bpc_property =
		intel_dp->attached_connector->base.max_bpc_property;
	if (connector->max_bpc_property)
		drm_connector_attach_max_bpc_property(connector, 6, 12);

	return drm_connector_set_path_property(connector, pathprop);
}

static void
intel_dp_mst_read_decompression_port_dsc_caps(struct intel_dp *intel_dp,
					      struct intel_connector *connector)
{
	u8 dpcd_caps[DP_RECEIVER_CAP_SIZE];

	if (!connector->dp.dsc_decompression_aux)
		return;

	if (drm_dp_read_dpcd_caps(connector->dp.dsc_decompression_aux, dpcd_caps) < 0)
		return;

	intel_dp_get_dsc_sink_cap(dpcd_caps[DP_DPCD_REV], connector);
}

static bool detect_dsc_hblank_expansion_quirk(const struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(connector->base.dev);
	struct drm_dp_aux *aux = connector->dp.dsc_decompression_aux;
	struct drm_dp_desc desc;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];

	if (!aux)
		return false;

	/*
	 * A logical port's OUI (at least for affected sinks) is all 0, so
	 * instead of that the parent port's OUI is used for identification.
	 */
	if (drm_dp_mst_port_is_logical(connector->port)) {
		aux = drm_dp_mst_aux_for_parent(connector->port);
		if (!aux)
			aux = &connector->mst_port->aux;
	}

	if (drm_dp_read_dpcd_caps(aux, dpcd) < 0)
		return false;

	if (drm_dp_read_desc(aux, &desc, drm_dp_is_branch(dpcd)) < 0)
		return false;

	if (!drm_dp_has_quirk(&desc,
			      DP_DPCD_QUIRK_HBLANK_EXPANSION_REQUIRES_DSC))
		return false;

	/*
	 * UHBR (MST sink) devices requiring this quirk don't advertise the
	 * HBLANK expansion support. Presuming that they perform HBLANK
	 * expansion internally, or are affected by this issue on modes with a
	 * short HBLANK for other reasons.
	 */
	if (!drm_dp_128b132b_supported(dpcd) &&
	    !(dpcd[DP_RECEIVE_PORT_0_CAP_0] & DP_HBLANK_EXPANSION_CAPABLE))
		return false;

	drm_dbg_kms(&i915->drm,
		    "[CONNECTOR:%d:%s] DSC HBLANK expansion quirk detected\n",
		    connector->base.base.id, connector->base.name);

	return true;
}

static struct drm_connector *intel_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
							struct drm_dp_mst_port *port,
							const char *pathprop)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst_mgr);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	enum pipe pipe;
	int ret;

	intel_connector = intel_connector_alloc();
	if (!intel_connector)
		return NULL;

	intel_connector->get_hw_state = intel_dp_mst_get_hw_state;
	intel_connector->sync_state = intel_dp_connector_sync_state;
	intel_connector->mst_port = intel_dp;
	intel_connector->port = port;
	drm_dp_mst_get_port_malloc(port);

	intel_dp_init_modeset_retry_work(intel_connector);

	/*
	 * TODO: The following drm_connector specific initialization belongs
	 * to DRM core, however it happens atm too late in
	 * drm_connector_init(). That function will also expose the connector
	 * to in-kernel users, so it can't be called until the connector is
	 * sufficiently initialized; init the device pointer used by the
	 * following DSC setup, until a fix moving this to DRM core.
	 */
	intel_connector->base.dev = mgr->dev;

	intel_connector->dp.dsc_decompression_aux = drm_dp_mst_dsc_aux_for_port(port);
	intel_dp_mst_read_decompression_port_dsc_caps(intel_dp, intel_connector);
	intel_connector->dp.dsc_hblank_expansion_quirk =
		detect_dsc_hblank_expansion_quirk(intel_connector);

	connector = &intel_connector->base;
	ret = drm_connector_init(dev, connector, &intel_dp_mst_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		drm_dp_mst_put_port_malloc(port);
		intel_connector_free(intel_connector);
		return NULL;
	}

	drm_connector_helper_add(connector, &intel_dp_mst_connector_helper_funcs);

	for_each_pipe(dev_priv, pipe) {
		struct drm_encoder *enc =
			&intel_dp->mst_encoders[pipe]->base.base;

		ret = drm_connector_attach_encoder(&intel_connector->base, enc);
		if (ret)
			goto err;
	}

	ret = intel_dp_mst_add_properties(intel_dp, connector, pathprop);
	if (ret)
		goto err;

	ret = intel_dp_hdcp_init(dig_port, intel_connector);
	if (ret)
		drm_dbg_kms(&dev_priv->drm, "[%s:%d] HDCP MST init failed, skipping.\n",
			    connector->name, connector->base.id);

	return connector;

err:
	drm_connector_cleanup(connector);
	return NULL;
}

static void
intel_dp_mst_poll_hpd_irq(struct drm_dp_mst_topology_mgr *mgr)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst_mgr);

	intel_hpd_trigger_irq(dp_to_dig_port(intel_dp));
}

static const struct drm_dp_mst_topology_cbs mst_cbs = {
	.add_connector = intel_dp_add_mst_connector,
	.poll_hpd_irq = intel_dp_mst_poll_hpd_irq,
};

static struct intel_dp_mst_encoder *
intel_dp_create_fake_mst_encoder(struct intel_digital_port *dig_port, enum pipe pipe)
{
	struct intel_dp_mst_encoder *intel_mst;
	struct intel_encoder *intel_encoder;
	struct drm_device *dev = dig_port->base.base.dev;

	intel_mst = kzalloc(sizeof(*intel_mst), GFP_KERNEL);

	if (!intel_mst)
		return NULL;

	intel_mst->pipe = pipe;
	intel_encoder = &intel_mst->base;
	intel_mst->primary = dig_port;

	drm_encoder_init(dev, &intel_encoder->base, &intel_dp_mst_enc_funcs,
			 DRM_MODE_ENCODER_DPMST, "DP-MST %c", pipe_name(pipe));

	intel_encoder->type = INTEL_OUTPUT_DP_MST;
	intel_encoder->power_domain = dig_port->base.power_domain;
	intel_encoder->port = dig_port->base.port;
	intel_encoder->cloneable = 0;
	/*
	 * This is wrong, but broken userspace uses the intersection
	 * of possible_crtcs of all the encoders of a given connector
	 * to figure out which crtcs can drive said connector. What
	 * should be used instead is the union of possible_crtcs.
	 * To keep such userspace functioning we must misconfigure
	 * this to make sure the intersection is not empty :(
	 */
	intel_encoder->pipe_mask = ~0;

	intel_encoder->compute_config = intel_dp_mst_compute_config;
	intel_encoder->compute_config_late = intel_dp_mst_compute_config_late;
	intel_encoder->disable = intel_mst_disable_dp;
	intel_encoder->post_disable = intel_mst_post_disable_dp;
	intel_encoder->post_pll_disable = intel_mst_post_pll_disable_dp;
	intel_encoder->update_pipe = intel_ddi_update_pipe;
	intel_encoder->pre_pll_enable = intel_mst_pre_pll_enable_dp;
	intel_encoder->pre_enable = intel_mst_pre_enable_dp;
	intel_encoder->enable = intel_mst_enable_dp;
	intel_encoder->audio_enable = intel_audio_codec_enable;
	intel_encoder->audio_disable = intel_audio_codec_disable;
	intel_encoder->get_hw_state = intel_dp_mst_enc_get_hw_state;
	intel_encoder->get_config = intel_dp_mst_enc_get_config;
	intel_encoder->initial_fastset_check = intel_dp_mst_initial_fastset_check;

	return intel_mst;

}

static bool
intel_dp_create_fake_mst_encoders(struct intel_digital_port *dig_port)
{
	struct intel_dp *intel_dp = &dig_port->dp;
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe)
		intel_dp->mst_encoders[pipe] = intel_dp_create_fake_mst_encoder(dig_port, pipe);
	return true;
}

int
intel_dp_mst_encoder_active_links(struct intel_digital_port *dig_port)
{
	return dig_port->dp.active_mst_links;
}

int
intel_dp_mst_encoder_init(struct intel_digital_port *dig_port, int conn_base_id)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	struct intel_dp *intel_dp = &dig_port->dp;
	enum port port = dig_port->base.port;
	int ret;

	if (!HAS_DP_MST(i915) || intel_dp_is_edp(intel_dp))
		return 0;

	if (DISPLAY_VER(i915) < 12 && port == PORT_A)
		return 0;

	if (DISPLAY_VER(i915) < 11 && port == PORT_E)
		return 0;

	intel_dp->mst_mgr.cbs = &mst_cbs;

	/* create encoders */
	intel_dp_create_fake_mst_encoders(dig_port);
	ret = drm_dp_mst_topology_mgr_init(&intel_dp->mst_mgr, &i915->drm,
					   &intel_dp->aux, 16, 3, conn_base_id);
	if (ret) {
		intel_dp->mst_mgr.cbs = NULL;
		return ret;
	}

	return 0;
}

bool intel_dp_mst_source_support(struct intel_dp *intel_dp)
{
	return intel_dp->mst_mgr.cbs;
}

void
intel_dp_mst_encoder_cleanup(struct intel_digital_port *dig_port)
{
	struct intel_dp *intel_dp = &dig_port->dp;

	if (!intel_dp_mst_source_support(intel_dp))
		return;

	drm_dp_mst_topology_mgr_destroy(&intel_dp->mst_mgr);
	/* encoders will get killed by normal cleanup */

	intel_dp->mst_mgr.cbs = NULL;
}

bool intel_dp_mst_is_master_trans(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->mst_master_transcoder == crtc_state->cpu_transcoder;
}

bool intel_dp_mst_is_slave_trans(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->mst_master_transcoder != INVALID_TRANSCODER &&
	       crtc_state->mst_master_transcoder != crtc_state->cpu_transcoder;
}

/**
 * intel_dp_mst_add_topology_state_for_connector - add MST topology state for a connector
 * @state: atomic state
 * @connector: connector to add the state for
 * @crtc: the CRTC @connector is attached to
 *
 * Add the MST topology state for @connector to @state.
 *
 * Returns 0 on success, negative error code on failure.
 */
static int
intel_dp_mst_add_topology_state_for_connector(struct intel_atomic_state *state,
					      struct intel_connector *connector,
					      struct intel_crtc *crtc)
{
	struct drm_dp_mst_topology_state *mst_state;

	if (!connector->mst_port)
		return 0;

	mst_state = drm_atomic_get_mst_topology_state(&state->base,
						      &connector->mst_port->mst_mgr);
	if (IS_ERR(mst_state))
		return PTR_ERR(mst_state);

	mst_state->pending_crtc_mask |= drm_crtc_mask(&crtc->base);

	return 0;
}

/**
 * intel_dp_mst_add_topology_state_for_crtc - add MST topology state for a CRTC
 * @state: atomic state
 * @crtc: CRTC to add the state for
 *
 * Add the MST topology state for @crtc to @state.
 *
 * Returns 0 on success, negative error code on failure.
 */
int intel_dp_mst_add_topology_state_for_crtc(struct intel_atomic_state *state,
					     struct intel_crtc *crtc)
{
	struct drm_connector *_connector;
	struct drm_connector_state *conn_state;
	int i;

	for_each_new_connector_in_state(&state->base, _connector, conn_state, i) {
		struct intel_connector *connector = to_intel_connector(_connector);
		int ret;

		if (conn_state->crtc != &crtc->base)
			continue;

		ret = intel_dp_mst_add_topology_state_for_connector(state, connector, crtc);
		if (ret)
			return ret;
	}

	return 0;
}

static struct intel_connector *
get_connector_in_state_for_crtc(struct intel_atomic_state *state,
				const struct intel_crtc *crtc)
{
	struct drm_connector_state *old_conn_state;
	struct drm_connector_state *new_conn_state;
	struct drm_connector *_connector;
	int i;

	for_each_oldnew_connector_in_state(&state->base, _connector,
					   old_conn_state, new_conn_state, i) {
		struct intel_connector *connector =
			to_intel_connector(_connector);

		if (old_conn_state->crtc == &crtc->base ||
		    new_conn_state->crtc == &crtc->base)
			return connector;
	}

	return NULL;
}

/**
 * intel_dp_mst_crtc_needs_modeset - check if changes in topology need to modeset the given CRTC
 * @state: atomic state
 * @crtc: CRTC for which to check the modeset requirement
 *
 * Check if any change in a MST topology requires a forced modeset on @crtc in
 * this topology. One such change is enabling/disabling the DSC decompression
 * state in the first branch device's UFP DPCD as required by one CRTC, while
 * the other @crtc in the same topology is still active, requiring a full modeset
 * on @crtc.
 */
bool intel_dp_mst_crtc_needs_modeset(struct intel_atomic_state *state,
				     struct intel_crtc *crtc)
{
	const struct intel_connector *crtc_connector;
	const struct drm_connector_state *conn_state;
	const struct drm_connector *_connector;
	int i;

	if (!intel_crtc_has_type(intel_atomic_get_new_crtc_state(state, crtc),
				 INTEL_OUTPUT_DP_MST))
		return false;

	crtc_connector = get_connector_in_state_for_crtc(state, crtc);

	if (!crtc_connector)
		/* None of the connectors in the topology needs modeset */
		return false;

	for_each_new_connector_in_state(&state->base, _connector, conn_state, i) {
		const struct intel_connector *connector =
			to_intel_connector(_connector);
		const struct intel_crtc_state *new_crtc_state;
		const struct intel_crtc_state *old_crtc_state;
		struct intel_crtc *crtc_iter;

		if (connector->mst_port != crtc_connector->mst_port ||
		    !conn_state->crtc)
			continue;

		crtc_iter = to_intel_crtc(conn_state->crtc);

		new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc_iter);
		old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc_iter);

		if (!intel_crtc_needs_modeset(new_crtc_state))
			continue;

		if (old_crtc_state->dsc.compression_enable ==
		    new_crtc_state->dsc.compression_enable)
			continue;
		/*
		 * Toggling the decompression flag because of this stream in
		 * the first downstream branch device's UFP DPCD may reset the
		 * whole branch device. To avoid the reset while other streams
		 * are also active modeset the whole MST topology in this
		 * case.
		 */
		if (connector->dp.dsc_decompression_aux ==
		    &connector->mst_port->aux)
			return true;
	}

	return false;
}

/**
 * intel_dp_mst_prepare_probe - Prepare an MST link for topology probing
 * @intel_dp: DP port object
 *
 * Prepare an MST link for topology probing, programming the target
 * link parameters to DPCD. This step is a requirement of the enumaration
 * of path resources during probing.
 */
void intel_dp_mst_prepare_probe(struct intel_dp *intel_dp)
{
	int link_rate = intel_dp_max_link_rate(intel_dp);
	int lane_count = intel_dp_max_lane_count(intel_dp);
	u8 rate_select;
	u8 link_bw;

	if (intel_dp->link_trained)
		return;

	if (intel_mst_probed_link_params_valid(intel_dp, link_rate, lane_count))
		return;

	intel_dp_compute_rate(intel_dp, link_rate, &link_bw, &rate_select);

	intel_dp_link_training_set_mode(intel_dp, link_rate, false);
	intel_dp_link_training_set_bw(intel_dp, link_bw, rate_select, lane_count,
				      drm_dp_enhanced_frame_cap(intel_dp->dpcd));

	intel_mst_set_probed_link_params(intel_dp, link_rate, lane_count);
}

/*
 * intel_dp_mst_verify_dpcd_state - verify the MST SW enabled state wrt. the DPCD
 * @intel_dp: DP port object
 *
 * Verify if @intel_dp's MST enabled SW state matches the corresponding DPCD
 * state. A long HPD pulse - not long enough to be detected as a disconnected
 * state - could've reset the DPCD state, which requires tearing
 * down/recreating the MST topology.
 *
 * Returns %true if the SW MST enabled and DPCD states match, %false
 * otherwise.
 */
bool intel_dp_mst_verify_dpcd_state(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct intel_connector *connector = intel_dp->attached_connector;
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	int ret;
	u8 val;

	if (!intel_dp->is_mst)
		return true;

	ret = drm_dp_dpcd_readb(intel_dp->mst_mgr.aux, DP_MSTM_CTRL, &val);

	/* Adjust the expected register value for SST + SideBand. */
	if (ret < 0 || val != (DP_MST_EN | DP_UP_REQ_EN | DP_UPSTREAM_IS_SRC)) {
		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s][ENCODER:%d:%s] MST mode got reset, removing topology (ret=%d, ctrl=0x%02x)\n",
			    connector->base.base.id, connector->base.name,
			    encoder->base.base.id, encoder->base.name,
			    ret, val);

		return false;
	}

	return true;
}
