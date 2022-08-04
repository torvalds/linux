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

#include <drm/drm_mode.h>

enum shmob_drm_clk_source {
	SHMOB_DRM_CLK_BUS,
	SHMOB_DRM_CLK_PERIPHERAL,
	SHMOB_DRM_CLK_EXTERNAL,
};

enum shmob_drm_interface {
	SHMOB_DRM_IFACE_RGB8,		/* 24bpp, 8:8:8 */
	SHMOB_DRM_IFACE_RGB9,		/* 18bpp, 9:9 */
	SHMOB_DRM_IFACE_RGB12A,		/* 24bpp, 12:12 */
	SHMOB_DRM_IFACE_RGB12B,		/* 12bpp */
	SHMOB_DRM_IFACE_RGB16,		/* 16bpp */
	SHMOB_DRM_IFACE_RGB18,		/* 18bpp */
	SHMOB_DRM_IFACE_RGB24,		/* 24bpp */
	SHMOB_DRM_IFACE_YUV422,		/* 16bpp */
	SHMOB_DRM_IFACE_SYS8A,		/* 24bpp, 8:8:8 */
	SHMOB_DRM_IFACE_SYS8B,		/* 18bpp, 8:8:2 */
	SHMOB_DRM_IFACE_SYS8C,		/* 18bpp, 2:8:8 */
	SHMOB_DRM_IFACE_SYS8D,		/* 16bpp, 8:8 */
	SHMOB_DRM_IFACE_SYS9,		/* 18bpp, 9:9 */
	SHMOB_DRM_IFACE_SYS12,		/* 24bpp, 12:12 */
	SHMOB_DRM_IFACE_SYS16A,		/* 16bpp */
	SHMOB_DRM_IFACE_SYS16B,		/* 18bpp, 16:2 */
	SHMOB_DRM_IFACE_SYS16C,		/* 18bpp, 2:16 */
	SHMOB_DRM_IFACE_SYS18,		/* 18bpp */
	SHMOB_DRM_IFACE_SYS24,		/* 24bpp */
};

struct shmob_drm_backlight_data {
	const char *name;
	int max_brightness;
	int (*get_brightness)(void);
	int (*set_brightness)(int brightness);
};

struct shmob_drm_panel_data {
	unsigned int width_mm;		/* Panel width in mm */
	unsigned int height_mm;		/* Panel height in mm */
	struct drm_mode_modeinfo mode;
};

struct shmob_drm_sys_interface_data {
	unsigned int read_latch:6;
	unsigned int read_setup:8;
	unsigned int read_cycle:8;
	unsigned int read_strobe:8;
	unsigned int write_setup:8;
	unsigned int write_cycle:8;
	unsigned int write_strobe:8;
	unsigned int cs_setup:3;
	unsigned int vsync_active_high:1;
	unsigned int vsync_dir_input:1;
};

#define SHMOB_DRM_IFACE_FL_DWPOL (1 << 0) /* Rising edge dot clock data latch */
#define SHMOB_DRM_IFACE_FL_DIPOL (1 << 1) /* Active low display enable */
#define SHMOB_DRM_IFACE_FL_DAPOL (1 << 2) /* Active low display data */
#define SHMOB_DRM_IFACE_FL_HSCNT (1 << 3) /* Disable HSYNC during VBLANK */
#define SHMOB_DRM_IFACE_FL_DWCNT (1 << 4) /* Disable dotclock during blanking */

struct shmob_drm_interface_data {
	enum shmob_drm_interface interface;
	struct shmob_drm_sys_interface_data sys;
	unsigned int clk_div;
	unsigned int flags;
};

struct shmob_drm_platform_data {
	enum shmob_drm_clk_source clk_source;
	struct shmob_drm_interface_data iface;
	struct shmob_drm_panel_data panel;
	struct shmob_drm_backlight_data backlight;
};

#endif /* __SHMOB_DRM_H__ */
