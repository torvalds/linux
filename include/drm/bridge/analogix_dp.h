/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Analogix DP (Display Port) Core interface driver.
 *
 * Copyright (C) 2015 Rockchip Electronics Co., Ltd.
 */
#ifndef _ANALOGIX_DP_H_
#define _ANALOGIX_DP_H_

#include <drm/drm_crtc.h>
#include <sound/hdmi-codec.h>

struct analogix_dp_device;

enum analogix_dp_devtype {
	EXYNOS_DP,
	RK3288_DP,
	RK3399_EDP,
	RK3568_EDP,
	RK3588_EDP,
};

static inline bool is_rockchip(enum analogix_dp_devtype type)
{
	switch (type) {
	case RK3288_DP:
	case RK3399_EDP:
	case RK3568_EDP:
	case RK3588_EDP:
		return true;
	default:
		return false;
	}
}

struct analogix_dp_plat_data {
	enum analogix_dp_devtype dev_type;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	bool skip_connector;
	bool ssc;

	bool split_mode;
	struct analogix_dp_device *left;
	struct analogix_dp_device *right;

	int (*power_on_start)(struct analogix_dp_plat_data *);
	int (*power_on_end)(struct analogix_dp_plat_data *);
	int (*power_off)(struct analogix_dp_plat_data *);
	int (*attach)(struct analogix_dp_plat_data *, struct drm_bridge *,
		      struct drm_connector *);
	void (*detach)(struct analogix_dp_plat_data *, struct drm_bridge *);
	int (*get_modes)(struct analogix_dp_plat_data *,
			 struct drm_connector *);
	void (*convert_to_split_mode)(struct drm_display_mode *);
	void (*convert_to_origin_mode)(struct drm_display_mode *);
};

int analogix_dp_resume(struct analogix_dp_device *dp);
int analogix_dp_suspend(struct analogix_dp_device *dp);
int analogix_dp_runtime_resume(struct analogix_dp_device *dp);
int analogix_dp_runtime_suspend(struct analogix_dp_device *dp);

struct analogix_dp_device *
analogix_dp_probe(struct device *dev, struct analogix_dp_plat_data *plat_data);
int analogix_dp_bind(struct analogix_dp_device *dp, struct drm_device *drm_dev);
void analogix_dp_unbind(struct analogix_dp_device *dp);
void analogix_dp_remove(struct analogix_dp_device *dp);

int analogix_dp_start_crc(struct drm_connector *connector);
int analogix_dp_stop_crc(struct drm_connector *connector);

int analogix_dp_audio_hw_params(struct analogix_dp_device *dp,
				struct hdmi_codec_daifmt *daifmt,
				struct hdmi_codec_params *params);
void analogix_dp_audio_shutdown(struct analogix_dp_device *dp);
int analogix_dp_audio_startup(struct analogix_dp_device *dp);
int analogix_dp_audio_get_eld(struct analogix_dp_device *dp,
			      u8 *buf, size_t len);
int analogix_dp_loader_protect(struct analogix_dp_device *dp);

#endif /* _ANALOGIX_DP_H_ */
