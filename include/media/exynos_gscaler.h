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

enum gsc_ip_version {
	IP_VER_GSC_5G,
	IP_VER_GSC_5A,
};

struct exynos_platform_gscaler {
	struct exynos_isp_info *isp_info[MAX_CAMIF_CLIENTS];
	u32 active_cam_index;
	u32 num_clients;
	u32 cam_preview:1;
	u32 cam_camcording:1;
	u32 ip_ver;
};

extern struct exynos_platform_gscaler exynos_gsc0_default_data;
extern struct exynos_platform_gscaler exynos_gsc1_default_data;
extern struct exynos_platform_gscaler exynos_gsc2_default_data;
extern struct exynos_platform_gscaler exynos_gsc3_default_data;

/**
  * exynos5_gsc_set_pdev_name() = Exynos setup function for gscaler pdev name
  * @ id: gscaler device number
  * @ name: pdev name for gscaler
  */
void __init exynos5_gsc_set_pdev_name(int id, char *name);
void __init exynos5_gsc_set_ip_ver(enum gsc_ip_version ver);
#endif /* EXYNOS_GSCALER_H_ */
