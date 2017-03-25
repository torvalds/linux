/*
 * Analogix DP (Display Port) Core interface driver.
 *
 * Copyright (C) 2015 Rockchip Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#ifndef _ANALOGIX_DP_H_
#define _ANALOGIX_DP_H_

#include <drm/drm_crtc.h>

enum analogix_dp_devtype {
	EXYNOS_DP,
	ROCKCHIP_DP,
};

enum analogix_dp_sub_devtype {
	RK3288_DP,
	RK3368_EDP,
	RK3399_EDP,
};

struct analogix_dp_plat_data {
	enum analogix_dp_devtype dev_type;
	enum analogix_dp_sub_devtype subdev_type;
	struct drm_panel *panel;
	struct drm_encoder *encoder;
	struct drm_connector *connector;

	int (*power_on)(struct analogix_dp_plat_data *);
	int (*power_off)(struct analogix_dp_plat_data *);
	int (*attach)(struct analogix_dp_plat_data *, struct drm_bridge *,
		      struct drm_connector *);
	int (*get_modes)(struct analogix_dp_plat_data *,
			 struct drm_connector *);
};

int analogix_dp_resume(struct device *dev);
int analogix_dp_suspend(struct device *dev);

int analogix_dp_bind(struct device *dev, struct drm_device *drm_dev,
		     struct analogix_dp_plat_data *plat_data);
void analogix_dp_unbind(struct device *dev, struct device *master, void *data);

#endif /* _ANALOGIX_DP_H_ */
