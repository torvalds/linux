/* include/media/exynos_gscaler.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung EXYNOS SoC Gscaler driver header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXYNOS_GSCALER_H_
#define EXYNOS_GSCALER_H_

#include <media/exynos_camera.h>

/**
 * struct exynos_platform_gscaler - camera host interface platform data
 *
 * @isp_info: properties of camera sensor required for host interface setup
 */
struct exynos_platform_gscaler {
	struct exynos_isp_info *isp_info[MAX_CAMIF_CLIENTS];
	u32 active_cam_index;
	u32 num_clients;
	u32 cam_preview:1;
	u32 cam_camcording:1;
};

extern struct exynos_platform_gscaler exynos_gsc0_default_data;
extern struct exynos_platform_gscaler exynos_gsc1_default_data;
extern struct exynos_platform_gscaler exynos_gsc2_default_data;
extern struct exynos_platform_gscaler exynos_gsc3_default_data;

/**
 * exynos5_gsc_set_parent_clock() = Exynos5 setup function for parent clock.
 * @child: child clock used for gscaler
 * @parent: parent clock used for gscaler
 */
int __init exynos5_gsc_set_parent_clock(const char *child, const char *parent);

/**
 * exynos5_gsc_set_clock_rate() = Exynos5 setup function for clock rate.
 * @clk: name of clock used for gscaler
 * @clk_rate: clock_rate for gscaler clock
 */
int __init exynos5_gsc_set_clock_rate(const char *clk, unsigned long clk_rate);
#endif /* EXYNOS_GSCALER_H_ */
