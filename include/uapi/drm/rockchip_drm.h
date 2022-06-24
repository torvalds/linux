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
#include <drm/drm_file.h>

/*
 * Send vcnt event instead of blocking,
 * like _DRM_VBLANK_EVENT
 */
#define _DRM_ROCKCHIP_VCNT_EVENT 0x80000000
#define DRM_EVENT_ROCKCHIP_CRTC_VCNT   0xf

/* memory type definitions. */
enum drm_rockchip_gem_mem_type {
	/* Physically Continuous memory. */
	ROCKCHIP_BO_CONTIG	= 1 << 0,
	/* cachable mapping. */
	ROCKCHIP_BO_CACHABLE	= 1 << 1,
	/* write-combine mapping. */
	ROCKCHIP_BO_WC		= 1 << 2,
	ROCKCHIP_BO_SECURE	= 1 << 3,
	/* keep kmap for cma buffer or alloc kmap for other type memory */
	ROCKCHIP_BO_ALLOC_KMAP	= 1 << 4,
	/* alloc page with gfp_dma32 */
	ROCKCHIP_BO_DMA32	= 1 << 5,
	ROCKCHIP_BO_MASK	= ROCKCHIP_BO_CONTIG | ROCKCHIP_BO_CACHABLE |
				ROCKCHIP_BO_WC | ROCKCHIP_BO_SECURE | ROCKCHIP_BO_ALLOC_KMAP |
				ROCKCHIP_BO_DMA32,
};

/**
 * User-desired buffer creation information structure.
 *
 * @size: user-desired memory allocation size.
 * @flags: user request for setting memory type or cache attributes.
 * @handle: returned a handle to created gem object.
 *     - this handle will be set by gem module of kernel side.
 */
struct drm_rockchip_gem_create {
	uint64_t size;
	uint32_t flags;
	uint32_t handle;
};

struct drm_rockchip_gem_phys {
	uint32_t handle;
	uint32_t phy_addr;
};

/**
 * A structure for getting buffer offset.
 *
 * @handle: a pointer to gem object created.
 * @pad: just padding to be 64-bit aligned.
 * @offset: relatived offset value of the memory region allocated.
 *     - this value should be set by user.
 */
struct drm_rockchip_gem_map_off {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
};

/* acquire type definitions. */
enum drm_rockchip_gem_cpu_acquire_type {
	DRM_ROCKCHIP_GEM_CPU_ACQUIRE_SHARED = 0x0,
	DRM_ROCKCHIP_GEM_CPU_ACQUIRE_EXCLUSIVE = 0x1,
};

enum rockchip_crtc_feture {
	ROCKCHIP_DRM_CRTC_FEATURE_ALPHA_SCALE,
	ROCKCHIP_DRM_CRTC_FEATURE_HDR10,
	ROCKCHIP_DRM_CRTC_FEATURE_NEXT_HDR,
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

enum rockchip_cabc_mode {
	ROCKCHIP_DRM_CABC_MODE_DISABLE,
	ROCKCHIP_DRM_CABC_MODE_NORMAL,
	ROCKCHIP_DRM_CABC_MODE_LOWPOWER,
	ROCKCHIP_DRM_CABC_MODE_USERSPACE,
};

struct drm_rockchip_vcnt_event {
	struct drm_pending_event	base;
};

#define DRM_ROCKCHIP_GEM_CREATE		0x00
#define DRM_ROCKCHIP_GEM_MAP_OFFSET	0x01
#define DRM_ROCKCHIP_GEM_CPU_ACQUIRE	0x02
#define DRM_ROCKCHIP_GEM_CPU_RELEASE	0x03
#define DRM_ROCKCHIP_GEM_GET_PHYS	0x04
#define DRM_ROCKCHIP_GET_VCNT_EVENT	0x05

#define DRM_IOCTL_ROCKCHIP_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_ROCKCHIP_GEM_CREATE, struct drm_rockchip_gem_create)

#define DRM_IOCTL_ROCKCHIP_GEM_MAP_OFFSET	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_ROCKCHIP_GEM_MAP_OFFSET, struct drm_rockchip_gem_map_off)

#define DRM_IOCTL_ROCKCHIP_GEM_CPU_ACQUIRE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_ROCKCHIP_GEM_CPU_ACQUIRE, struct drm_rockchip_gem_cpu_acquire)

#define DRM_IOCTL_ROCKCHIP_GEM_CPU_RELEASE	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_ROCKCHIP_GEM_CPU_RELEASE, struct drm_rockchip_gem_cpu_release)

#define DRM_IOCTL_ROCKCHIP_GEM_GET_PHYS		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_ROCKCHIP_GEM_GET_PHYS, struct drm_rockchip_gem_phys)

#define DRM_IOCTL_ROCKCHIP_GET_VCNT_EVENT	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_ROCKCHIP_GET_VCNT_EVENT, union drm_wait_vblank)

#endif /* _UAPI_ROCKCHIP_DRM_H */
