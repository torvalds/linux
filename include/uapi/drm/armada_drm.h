/*
 * Copyright (C) 2012 Russell King
 *  With inspiration from the i915 driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef DRM_ARMADA_IOCTL_H
#define DRM_ARMADA_IOCTL_H

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_ARMADA_GEM_CREATE		0x00
#define DRM_ARMADA_GEM_MMAP		0x02
#define DRM_ARMADA_GEM_PWRITE		0x03

#define ARMADA_IOCTL(dir, name, str) \
	DRM_##dir(DRM_COMMAND_BASE + DRM_ARMADA_##name, struct drm_armada_##str)

struct drm_armada_gem_create {
	uint32_t handle;
	uint32_t size;
};
#define DRM_IOCTL_ARMADA_GEM_CREATE \
	ARMADA_IOCTL(IOWR, GEM_CREATE, gem_create)

struct drm_armada_gem_mmap {
	uint32_t handle;
	uint32_t pad;
	uint64_t offset;
	uint64_t size;
	uint64_t addr;
};
#define DRM_IOCTL_ARMADA_GEM_MMAP \
	ARMADA_IOCTL(IOWR, GEM_MMAP, gem_mmap)

struct drm_armada_gem_pwrite {
	uint64_t ptr;
	uint32_t handle;
	uint32_t offset;
	uint32_t size;
};
#define DRM_IOCTL_ARMADA_GEM_PWRITE \
	ARMADA_IOCTL(IOW, GEM_PWRITE, gem_pwrite)

#if defined(__cplusplus)
}
#endif

#endif
