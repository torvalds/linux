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

/**
 * enum drm_nova_architecture - GPU architecture identifier
 */
enum drm_nova_architecture {
	NOVA_DRM_ARCHITECTURE_TURING		= 0x16,
	NOVA_DRM_ARCHITECTURE_AMPERE		= 0x17,
	NOVA_DRM_ARCHITECTURE_HOPPER		= 0x18,
	NOVA_DRM_ARCHITECTURE_ADA		= 0x19,
	NOVA_DRM_ARCHITECTURE_BLACKWELL_GB10X	= 0x1a,
	NOVA_DRM_ARCHITECTURE_BLACKWELL_GB20X	= 0x1b,
};

/**
 * enum drm_nova_chipid - opaque GPU chip identifier
 *
 * These values identify the chip a GPU is based on. They are otherwise
 * opaque: userspace must not assume the values carry any particular meaning
 * or encoding, only that they may be compared against this enum.
 */
enum drm_nova_chipid {
	/* Turing */
	NOVA_DRM_CHIPID_TU102			= 0x162,
	NOVA_DRM_CHIPID_TU104			= 0x164,
	NOVA_DRM_CHIPID_TU106			= 0x166,
	NOVA_DRM_CHIPID_TU117			= 0x167,
	NOVA_DRM_CHIPID_TU116			= 0x168,
	/* Ampere */
	NOVA_DRM_CHIPID_GA100			= 0x170,
	NOVA_DRM_CHIPID_GA102			= 0x172,
	NOVA_DRM_CHIPID_GA103			= 0x173,
	NOVA_DRM_CHIPID_GA104			= 0x174,
	NOVA_DRM_CHIPID_GA106			= 0x176,
	NOVA_DRM_CHIPID_GA107			= 0x177,
	/* Hopper */
	NOVA_DRM_CHIPID_GH100			= 0x180,
	/* Ada */
	NOVA_DRM_CHIPID_AD102			= 0x192,
	NOVA_DRM_CHIPID_AD103			= 0x193,
	NOVA_DRM_CHIPID_AD104			= 0x194,
	NOVA_DRM_CHIPID_AD106			= 0x196,
	NOVA_DRM_CHIPID_AD107			= 0x197,
	/* Blackwell GB10x */
	NOVA_DRM_CHIPID_GB100			= 0x1a0,
	NOVA_DRM_CHIPID_GB102			= 0x1a2,
	/* Blackwell GB20x */
	NOVA_DRM_CHIPID_GB202			= 0x1b2,
	NOVA_DRM_CHIPID_GB203			= 0x1b3,
	NOVA_DRM_CHIPID_GB205			= 0x1b5,
	NOVA_DRM_CHIPID_GB206			= 0x1b6,
	NOVA_DRM_CHIPID_GB207			= 0x1b7,
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

/**
 * struct drm_nova_info - query device information
 */
struct drm_nova_info {
	/**
	 * @id: The identifier of the information to query.
	 */
	__u32 id;

	/**
	 * @size: The amount of space allocated by userspace at @data. The kernel
	 * will return the number of bytes it wrote, or the size of the queried
	 * information structure if @data is NULL.
	 */
	__u32 size;

	/**
	 * @data: Pointer to the userspace buffer into which the queried
	 * information will be written. May be NULL, in which case nothing is
	 * written and @size is set to the size of the information structure so
	 * userspace can allocate a suitably sized buffer.
	 */
	__u64 data;
};

/**
 * DRM_NOVA_INFO_GPU
 *
 * Query GPU information. The result is returned in a
 * &struct drm_nova_info_gpu.
 */
#define DRM_NOVA_INFO_GPU		0x00

/**
 * struct drm_nova_info_gpu - GPU information
 */
struct drm_nova_info_gpu {
	/**
	 * @architecture: GPU architecture identifier.
	 *
	 * See &enum drm_nova_architecture for currently known architectures.
	 */
	__u32 architecture;

	/**
	 * @chipid: Opaque GPU chip identifier.
	 *
	 * See &enum drm_nova_chipid for currently known chips.
	 */
	__u32 chipid;

	/**
	 * @vram_size: Amount of usable FB, excluding GSP carveouts and protected
	 * regions.
	 */
	__u64 vram_size;

	/**
	 * @vram_bar_size: Size of the PCI BAR aperture used to access VRAM, in bytes.
	 */
	__u64 vram_bar_size;

	/**
	 * @gpu_name: NUL-terminated full GPU name.
	 */
	__u8 gpu_name[64];

	/**
	 * @gpu_short_name: NUL-terminated short GPU name.
	 */
	__u8 gpu_short_name[64];

	/**
	 * @gpu_gid: 16-byte SHA-1 GPU identifier supplied by GSP-RM.
	 */
	__u8 gpu_gid[16];
};

#define DRM_NOVA_INFO			0x00
#define DRM_NOVA_GEM_CREATE		0x01
#define DRM_NOVA_GEM_INFO		0x02

/* Note: this is an enum so that it can be resolved by Rust bindgen. */
enum {
	DRM_IOCTL_NOVA_INFO		= DRM_IOWR(DRM_COMMAND_BASE + DRM_NOVA_INFO,
						   struct drm_nova_info),
	DRM_IOCTL_NOVA_GEM_CREATE	= DRM_IOWR(DRM_COMMAND_BASE + DRM_NOVA_GEM_CREATE,
						   struct drm_nova_gem_create),
	DRM_IOCTL_NOVA_GEM_INFO		= DRM_IOWR(DRM_COMMAND_BASE + DRM_NOVA_GEM_INFO,
						   struct drm_nova_gem_info),
};

#if defined(__cplusplus)
}
#endif

#endif /* __NOVA_DRM_H__ */
