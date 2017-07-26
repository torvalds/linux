/*
 *
 * Copyright (c) Fuzhou Rockchip Electronics Co.Ltd
 * Authors:
 *       Mark Yao <yzq@rock-chips.com>
 *
 * base on exynos_drm.h
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _UAPI_ROCKCHIP_DRM_H
#define _UAPI_ROCKCHIP_DRM_H

#include <drm/drm.h>

/* memory type definitions. */
enum drm_rockchip_gem_mem_type {
	/* Physically Continuous memory. */
	ROCKCHIP_BO_CONTIG	= 1 << 0,
	/* cachable mapping. */
	ROCKCHIP_BO_CACHABLE	= 1 << 1,
	/* write-combine mapping. */
	ROCKCHIP_BO_WC		= 1 << 2,
	ROCKCHIP_BO_MASK	= ROCKCHIP_BO_CONTIG | ROCKCHIP_BO_CACHABLE |
				ROCKCHIP_BO_WC
};

enum rockchip_plane_feture {
	ROCKCHIP_DRM_PLANE_FEATURE_SCALE,
	ROCKCHIP_DRM_PLANE_FEATURE_ALPHA,
	ROCKCHIP_DRM_PLANE_FEATURE_HDR2SDR,
	ROCKCHIP_DRM_PLANE_FEATURE_SDR2HDR,
	ROCKCHIP_DRM_PLANE_FEATURE_AFBDC,
	ROCKCHIP_DRM_PLANE_FEATURE_PDAF_POS,
	ROCKCHIP_DRM_PLANE_FEATURE_MAX,
};

enum rockchip_crtc_feture {
	ROCKCHIP_DRM_CRTC_FEATURE_AFBDC,
};

enum rockchip_cabc_mode {
	ROCKCHIP_DRM_CABC_MODE_DISABLE,
	ROCKCHIP_DRM_CABC_MODE_NORMAL,
	ROCKCHIP_DRM_CABC_MODE_LOWPOWER,
	ROCKCHIP_DRM_CABC_MODE_USERSPACE,
};

#endif /* _UAPI_ROCKCHIP_DRM_H */
