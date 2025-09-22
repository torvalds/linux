/* SPDX-License-Identifier: MIT */

#ifndef DRM_HDMI_STATE_HELPER_H_
#define DRM_HDMI_STATE_HELPER_H_

struct drm_atomic_state;
struct drm_connector;
struct drm_connector_state;
struct hdmi_audio_infoframe;

void __drm_atomic_helper_connector_hdmi_reset(struct drm_connector *connector,
					      struct drm_connector_state *new_conn_state);

int drm_atomic_helper_connector_hdmi_check(struct drm_connector *connector,
					   struct drm_atomic_state *state);

int drm_atomic_helper_connector_hdmi_update_audio_infoframe(struct drm_connector *connector,
							    struct hdmi_audio_infoframe *frame);
int drm_atomic_helper_connector_hdmi_clear_audio_infoframe(struct drm_connector *connector);
int drm_atomic_helper_connector_hdmi_update_infoframes(struct drm_connector *connector,
						       struct drm_atomic_state *state);

#endif // DRM_HDMI_STATE_HELPER_H_
