/*
 * Samsung S5P SoC camera interface driver header
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXYNOS_FLITE_H_
#define EXYNOS_FLITE_H_

#if defined(CONFIG_ARCH_EXYNOS4) && defined(CONFIG_VIDEO_FIMC)
#define MAX_CAMIF_CLIENTS	3
#include <plat/fimc.h>
#else
#include <media/exynos_camera.h>

struct s3c_platform_camera {
	enum cam_bus_type type;
	bool use_isp;
	int inv_pclk;
	int inv_vsync;
	int inv_href;
	int inv_hsync;
};
#endif

/**
 * struct exynos_platform_flite - camera host interface platform data
 *
 * @cam: properties of camera sensor required for host interface setup
 */
struct exynos_platform_flite {
	struct s3c_platform_camera *cam[MAX_CAMIF_CLIENTS];
	struct exynos_isp_info *isp_info[MAX_CAMIF_CLIENTS];
	u32 active_cam_index;
	u32 num_clients;
};

extern struct exynos_platform_flite exynos_flite0_default_data;
extern struct exynos_platform_flite exynos_flite1_default_data;
extern struct exynos_platform_flite exynos_flite2_default_data;
#endif /* EXYNOS_FLITE_H_*/
