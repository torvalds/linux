/* SPDX-License-Identifier: MIT */

#ifndef DRM_HDMI_HELPER
#define DRM_HDMI_HELPER

#include <linux/hdmi.h>

struct drm_connector;
struct drm_connector_state;
struct drm_display_mode;

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

#endif
