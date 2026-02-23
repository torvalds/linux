/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Rockchip Electronics Co., Ltd.
 */

#ifndef __INNO_HDMI__
#define __INNO_HDMI__

#include <linux/types.h>

struct device;
struct drm_encoder;
struct drm_display_mode;
struct inno_hdmi;

struct inno_hdmi_plat_ops {
	void (*enable)(struct device *pdev, struct drm_display_mode *mode);
};

struct inno_hdmi_phy_config {
	unsigned long pixelclock;
	u8 pre_emphasis;
	u8 voltage_level_control;
};

struct inno_hdmi_plat_data {
	const struct inno_hdmi_plat_ops *ops;
	struct inno_hdmi_phy_config *phy_configs;
	struct inno_hdmi_phy_config *default_phy_config;
};

struct inno_hdmi *inno_hdmi_bind(struct device *pdev,
				 struct drm_encoder *encoder,
				 const struct inno_hdmi_plat_data *plat_data);
#endif /* __INNO_HDMI__ */
