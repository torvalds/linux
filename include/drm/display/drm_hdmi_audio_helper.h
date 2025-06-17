/* SPDX-License-Identifier: MIT */

#ifndef DRM_DISPLAY_HDMI_AUDIO_HELPER_H_
#define DRM_DISPLAY_HDMI_AUDIO_HELPER_H_

#include <linux/types.h>

struct drm_connector;
struct drm_connector_hdmi_audio_funcs;

struct device;

int drm_connector_hdmi_audio_init(struct drm_connector *connector,
				  struct device *hdmi_codec_dev,
				  const struct drm_connector_hdmi_audio_funcs *funcs,
				  unsigned int max_i2s_playback_channels,
				  u64 i2s_formats,
				  bool spdif_playback,
				  int sound_dai_port);
void drm_connector_hdmi_audio_plugged_notify(struct drm_connector *connector,
					     bool plugged);

#endif
