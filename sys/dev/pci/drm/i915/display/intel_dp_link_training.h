/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_LINK_TRAINING_H__
#define __INTEL_DP_LINK_TRAINING_H__

#include <drm/display/drm_dp_helper.h>

struct intel_atomic_state;
struct intel_connector;
struct intel_crtc_state;
struct intel_dp;

int intel_dp_read_dprx_caps(struct intel_dp *intel_dp, u8 dpcd[DP_RECEIVER_CAP_SIZE]);
int intel_dp_init_lttpr_and_dprx_caps(struct intel_dp *intel_dp);

void intel_dp_link_training_set_mode(struct intel_dp *intel_dp,
				     int link_rate, bool is_vrr);
void intel_dp_link_training_set_bw(struct intel_dp *intel_dp,
				   int link_bw, int rate_select, int lane_count,
				   bool enhanced_framing);

void intel_dp_get_adjust_train(struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state,
			       enum drm_dp_phy dp_phy,
			       const u8 link_status[DP_LINK_STATUS_SIZE]);
void intel_dp_program_link_training_pattern(struct intel_dp *intel_dp,
					    const struct intel_crtc_state *crtc_state,
					    enum drm_dp_phy dp_phy,
					    u8 dp_train_pat);
void intel_dp_set_signal_levels(struct intel_dp *intel_dp,
				const struct intel_crtc_state *crtc_state,
				enum drm_dp_phy dp_phy);
void intel_dp_start_link_train(struct intel_atomic_state *state,
			       struct intel_dp *intel_dp,
			       const struct intel_crtc_state *crtc_state);
void intel_dp_stop_link_train(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state);

void
intel_dp_dump_link_status(struct intel_dp *intel_dp, enum drm_dp_phy dp_phy,
			  const u8 link_status[DP_LINK_STATUS_SIZE]);

/* Get the TPSx symbol type of the value programmed to DP_TRAINING_PATTERN_SET */
static inline u8 intel_dp_training_pattern_symbol(u8 pattern)
{
	return pattern & ~DP_LINK_SCRAMBLING_DISABLE;
}

void intel_dp_128b132b_sdp_crc16(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state);

void intel_dp_link_training_debugfs_add(struct intel_connector *connector);

#endif /* __INTEL_DP_LINK_TRAINING_H__ */
