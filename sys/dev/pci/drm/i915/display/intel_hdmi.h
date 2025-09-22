/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_HDMI_H__
#define __INTEL_HDMI_H__

#include <linux/types.h>

enum hdmi_infoframe_type;
enum intel_output_format;
enum port;
struct drm_connector;
struct drm_connector_state;
struct drm_encoder;
struct drm_i915_private;
struct intel_connector;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_encoder;
struct intel_hdmi;
union hdmi_infoframe;

bool intel_hdmi_init_connector(struct intel_digital_port *dig_port,
			       struct intel_connector *intel_connector);
bool intel_hdmi_compute_has_hdmi_sink(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state,
				      const struct drm_connector_state *conn_state);
int intel_hdmi_compute_config(struct intel_encoder *encoder,
			      struct intel_crtc_state *pipe_config,
			      struct drm_connector_state *conn_state);
void intel_hdmi_encoder_shutdown(struct intel_encoder *encoder);
bool intel_hdmi_handle_sink_scrambling(struct intel_encoder *encoder,
				       struct drm_connector *connector,
				       bool high_tmds_clock_ratio,
				       bool scrambling);
void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable);
void intel_infoframe_init(struct intel_digital_port *dig_port);
u32 intel_hdmi_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state);
u32 intel_hdmi_infoframe_enable(unsigned int type);
void intel_hdmi_read_gcp_infoframe(struct intel_encoder *encoder,
				   struct intel_crtc_state *crtc_state);
void intel_read_infoframe(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state,
			  enum hdmi_infoframe_type type,
			  union hdmi_infoframe *frame);
bool intel_hdmi_limited_color_range(const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state);
bool intel_hdmi_bpc_possible(const struct intel_crtc_state *crtc_state,
			     int bpc, bool has_hdmi_sink);
int intel_hdmi_tmds_clock(int clock, int bpc, enum intel_output_format sink_format);
int intel_hdmi_dsc_get_bpp(int src_fractional_bpp, int slice_width,
			   int num_slices, int output_format, bool hdmi_all_bpp,
			   int hdmi_max_chunk_bytes);
int intel_hdmi_dsc_get_num_slices(const struct intel_crtc_state *crtc_state,
				  int src_max_slices, int src_max_slice_width,
				  int hdmi_max_slices, int hdmi_throughput);
int intel_hdmi_dsc_get_slice_height(int vactive);

#endif /* __INTEL_HDMI_H__ */
