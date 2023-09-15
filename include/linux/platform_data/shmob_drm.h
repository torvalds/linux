/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * shmob_drm.h  --  SH Mobile DRM driver
 *
 * Copyright (C) 2012 Renesas Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __SHMOB_DRM_H__
#define __SHMOB_DRM_H__

#include <video/videomode.h>

enum shmob_drm_clk_source {
	SHMOB_DRM_CLK_BUS,
	SHMOB_DRM_CLK_PERIPHERAL,
	SHMOB_DRM_CLK_EXTERNAL,
};

struct shmob_drm_panel_data {
	unsigned int width_mm;		/* Panel width in mm */
	unsigned int height_mm;		/* Panel height in mm */
	struct videomode mode;
};

struct shmob_drm_interface_data {
	unsigned int bus_fmt;		/* MEDIA_BUS_FMT_* */
	unsigned int clk_div;
};

struct shmob_drm_platform_data {
	enum shmob_drm_clk_source clk_source;
	struct shmob_drm_interface_data iface;
	struct shmob_drm_panel_data panel;
};

#endif /* __SHMOB_DRM_H__ */
