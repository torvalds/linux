/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Analogix DP (Display Port) Core interface driver.
 *
 * Copyright (C) 2015 Rockchip Electronics Co., Ltd.
 */
#ifndef _ANALOGIX_DP_H_
#define _ANALOGIX_DP_H_

#include <drm/drm_crtc.h>

struct analogix_dp_device;
struct drm_dp_aux;

enum analogix_dp_devtype {
	EXYNOS_DP,
	RK3288_DP,
	RK3399_EDP,
	RK3588_EDP,
};

static inline bool is_rockchip(enum analogix_dp_devtype type)
{
	return type == RK3288_DP || type == RK3399_EDP || type == RK3588_EDP;
}

struct analogix_dp_plat_data {
	enum analogix_dp_devtype dev_type;
	struct drm_panel *panel;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	bool skip_connector;

	int (*power_on)(struct analogix_dp_plat_data *);
	int (*power_off)(struct analogix_dp_plat_data *);
	int (*attach)(struct analogix_dp_plat_data *, struct drm_bridge *,
		      struct drm_connector *);
	int (*get_modes)(struct analogix_dp_plat_data *,
			 struct drm_connector *);
};

int analogix_dp_resume(struct analogix_dp_device *dp);
int analogix_dp_suspend(struct analogix_dp_device *dp);

struct analogix_dp_device *
analogix_dp_probe(struct device *dev, struct analogix_dp_plat_data *plat_data);
int analogix_dp_bind(struct analogix_dp_device *dp, struct drm_device *drm_dev);
void analogix_dp_unbind(struct analogix_dp_device *dp);

int analogix_dp_start_crc(struct drm_connector *connector);
int analogix_dp_stop_crc(struct drm_connector *connector);

struct analogix_dp_plat_data *analogix_dp_aux_to_plat_data(struct drm_dp_aux *aux);
struct drm_dp_aux *analogix_dp_get_aux(struct analogix_dp_device *dp);

#endif /* _ANALOGIX_DP_H_ */
