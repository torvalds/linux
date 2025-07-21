/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Tomeu Vizoso
 */
#ifndef __DRM_UAPI_ROCKET_ACCEL_H__
#define __DRM_UAPI_ROCKET_ACCEL_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_ROCKET_CREATE_BO			0x00

#define DRM_IOCTL_ROCKET_CREATE_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_ROCKET_CREATE_BO, struct drm_rocket_create_bo)

/**
 * struct drm_rocket_create_bo - ioctl argument for creating Rocket BOs.
 *
 */
struct drm_rocket_create_bo {
	/** Input: Size of the requested BO. */
	__u32 size;

	/** Output: GEM handle for the BO. */
	__u32 handle;

	/**
	 * Output: DMA address for the BO in the NPU address space.  This address
	 * is private to the DRM fd and is valid for the lifetime of the GEM
	 * handle.
	 */
	__u64 dma_address;

	/** Output: Offset into the drm node to use for subsequent mmap call. */
	__u64 offset;
};

#if defined(__cplusplus)
}
#endif

#endif /* __DRM_UAPI_ROCKET_ACCEL_H__ */
