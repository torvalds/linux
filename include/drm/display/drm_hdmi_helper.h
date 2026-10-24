/* SPDX-License-Identifier: MIT */

#ifndef DRM_HDMI_HELPER
#define DRM_HDMI_HELPER

#include <linux/hdmi.h>

struct drm_connector;
struct drm_connector_state;
struct drm_display_mode;
struct drm_modeset_acquire_ctx;
enum drm_output_color_format;

void
drm_hdmi_avi_infoframe_colorimetry(struct hdmi_avi_infoframe *frame,
				   const struct drm_connector_state *conn_state);

void
drm_hdmi_avi_infoframe_bars(struct hdmi_avi_infoframe *frame,
			    const struct drm_connector_state *conn_state);

int
drm_hdmi_infoframe_set_hdr_metadata(struct hdmi_drm_infoframe *frame,
				    const struct drm_connector_state *conn_state);

void drm_hdmi_avi_infoframe_content_type(struct hdmi_avi_infoframe *frame,
					 const struct drm_connector_state *conn_state);

unsigned long long
drm_hdmi_compute_mode_clock(const struct drm_display_mode *mode,
			    unsigned int bpc, enum drm_output_color_format fmt);

void
drm_hdmi_acr_get_n_cts(unsigned long long tmds_char_rate,
		       unsigned int sample_rate,
		       unsigned int *out_n,
		       unsigned int *out_cts);

bool
drm_hdmi_mode_needs_scrambling(const struct drm_display_mode *mode,
			       unsigned int bpc, enum drm_output_color_format fmt);

int
drm_connector_hdmi_enable_scrambling(struct drm_connector *connector,
				     const struct drm_connector_state *conn_state);
int
drm_connector_hdmi_disable_scrambling(struct drm_connector *connector);

int
drm_connector_hdmi_sync_scdc(struct drm_connector *connector, bool plugged,
			     struct drm_modeset_acquire_ctx *ctx);

#endif
