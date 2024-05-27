/* SPDX-License-Identifier: MIT */

#ifndef DRM_HDMI_STATE_HELPER_H_
#define DRM_HDMI_STATE_HELPER_H_

struct drm_atomic_state;
struct drm_connector;
struct drm_connector_state;

void __drm_atomic_helper_connector_hdmi_reset(struct drm_connector *connector,
					      struct drm_connector_state *new_conn_state);

int drm_atomic_helper_connector_hdmi_check(struct drm_connector *connector,
					   struct drm_atomic_state *state);

#endif // DRM_HDMI_STATE_HELPER_H_
