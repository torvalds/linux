/* SPDX-License-Identifier: MIT */

#ifndef __NOVA_DRM_H__
#define __NOVA_DRM_H__

#include "drm.h"

/* DISCLAIMER: Do not use, this is not a stable uAPI.
 *
 * This uAPI serves only testing purposes as long as this driver is still in
 * development. It is required to implement and test infrastructure which is
 * upstreamed in the context of this driver. See also [1].
 *
 * [1] https://lore.kernel.org/dri-devel/Zfsj0_tb-0-tNrJy@cassiopeiae/T/#u
 */

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * NOVA_GETPARAM_VRAM_BAR_SIZE
 *
 * Query the VRAM BAR size in bytes.
 */
#define NOVA_GETPARAM_VRAM_BAR_SIZE	0x1

/**
 * struct drm_nova_getparam - query GPU and driver metadata
 */
struct drm_nova_getparam {
	/**
	 * @param: The identifier of the parameter to query.
	 */
	__u64 param;

	/**
	 * @value: The value for the specified parameter.
	 */
	__u64 value;
};

/**
 * struct drm_nova_gem_create - create a new DRM GEM object
 */
struct drm_nova_gem_create {
	/**
	 * @handle: The handle of the new DRM GEM object.
	 */
	__u32 handle;

	/**
	 * @pad: 32 bit padding, should be 0.
	 */
	__u32 pad;

	/**
	 * @size: The size of the new DRM GEM object.
	 */
	__u64 size;
};

/**
 * struct drm_nova_gem_info - query DRM GEM object metadata
 */
struct drm_nova_gem_info {
	/**
	 * @handle: The handle of the DRM GEM object to query.
	 */
	__u32 handle;

	/**
	 * @pad: 32 bit padding, should be 0.
	 */
	__u32 pad;

	/**
	 * @size: The size of the DRM GEM obejct.
	 */
	__u64 size;
};

#define DRM_NOVA_GETPARAM		0x00
#define DRM_NOVA_GEM_CREATE		0x01
#define DRM_NOVA_GEM_INFO		0x02

/* Note: this is an enum so that it can be resolved by Rust bindgen. */
enum {
	DRM_IOCTL_NOVA_GETPARAM		= DRM_IOWR(DRM_COMMAND_BASE + DRM_NOVA_GETPARAM,
						   struct drm_nova_getparam),
	DRM_IOCTL_NOVA_GEM_CREATE	= DRM_IOWR(DRM_COMMAND_BASE + DRM_NOVA_GEM_CREATE,
						   struct drm_nova_gem_create),
	DRM_IOCTL_NOVA_GEM_INFO		= DRM_IOWR(DRM_COMMAND_BASE + DRM_NOVA_GEM_INFO,
						   struct drm_nova_gem_info),
};

#if defined(__cplusplus)
}
#endif

#endif /* __NOVA_DRM_H__ */
