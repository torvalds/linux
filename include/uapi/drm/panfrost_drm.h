/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2018 Broadcom
 * Copyright © 2019 Collabora ltd.
 */
#ifndef _PANFROST_DRM_H_
#define _PANFROST_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_PANFROST_SUBMIT			0x00
#define DRM_PANFROST_WAIT_BO			0x01
#define DRM_PANFROST_CREATE_BO			0x02
#define DRM_PANFROST_MMAP_BO			0x03
#define DRM_PANFROST_GET_PARAM			0x04
#define DRM_PANFROST_GET_BO_OFFSET		0x05

#define DRM_IOCTL_PANFROST_SUBMIT		DRM_IOW(DRM_COMMAND_BASE + DRM_PANFROST_SUBMIT, struct drm_panfrost_submit)
#define DRM_IOCTL_PANFROST_WAIT_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_PANFROST_WAIT_BO, struct drm_panfrost_wait_bo)
#define DRM_IOCTL_PANFROST_CREATE_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_PANFROST_CREATE_BO, struct drm_panfrost_create_bo)
#define DRM_IOCTL_PANFROST_MMAP_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_PANFROST_MMAP_BO, struct drm_panfrost_mmap_bo)
#define DRM_IOCTL_PANFROST_GET_PARAM		DRM_IOWR(DRM_COMMAND_BASE + DRM_PANFROST_GET_PARAM, struct drm_panfrost_get_param)
#define DRM_IOCTL_PANFROST_GET_BO_OFFSET	DRM_IOWR(DRM_COMMAND_BASE + DRM_PANFROST_GET_BO_OFFSET, struct drm_panfrost_get_bo_offset)

#define PANFROST_JD_REQ_FS (1 << 0)
/**
 * struct drm_panfrost_submit - ioctl argument for submitting commands to the 3D
 * engine.
 *
 * This asks the kernel to have the GPU execute a render command list.
 */
struct drm_panfrost_submit {

	/** Address to GPU mapping of job descriptor */
	__u64 jc;

	/** An optional array of sync objects to wait on before starting this job. */
	__u64 in_syncs;

	/** Number of sync objects to wait on before starting this job. */
	__u32 in_sync_count;

	/** An optional sync object to place the completion fence in. */
	__u32 out_sync;

	/** Pointer to a u32 array of the BOs that are referenced by the job. */
	__u64 bo_handles;

	/** Number of BO handles passed in (size is that times 4). */
	__u32 bo_handle_count;

	/** A combination of PANFROST_JD_REQ_* */
	__u32 requirements;
};

/**
 * struct drm_panfrost_wait_bo - ioctl argument for waiting for
 * completion of the last DRM_PANFROST_SUBMIT on a BO.
 *
 * This is useful for cases where multiple processes might be
 * rendering to a BO and you want to wait for all rendering to be
 * completed.
 */
struct drm_panfrost_wait_bo {
	__u32 handle;
	__u32 pad;
	__s64 timeout_ns;	/* absolute */
};

/**
 * struct drm_panfrost_create_bo - ioctl argument for creating Panfrost BOs.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_panfrost_create_bo {
	__u32 size;
	__u32 flags;
	/** Returned GEM handle for the BO. */
	__u32 handle;
	/* Pad, must be zero-filled. */
	__u32 pad;
	/**
	 * Returned offset for the BO in the GPU address space.  This offset
	 * is private to the DRM fd and is valid for the lifetime of the GEM
	 * handle.
	 *
	 * This offset value will always be nonzero, since various HW
	 * units treat 0 specially.
	 */
	__u64 offset;
};

/**
 * struct drm_panfrost_mmap_bo - ioctl argument for mapping Panfrost BOs.
 *
 * This doesn't actually perform an mmap.  Instead, it returns the
 * offset you need to use in an mmap on the DRM device node.  This
 * means that tools like valgrind end up knowing about the mapped
 * memory.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_panfrost_mmap_bo {
	/** Handle for the object being mapped. */
	__u32 handle;
	__u32 flags;
	/** offset into the drm node to use for subsequent mmap call. */
	__u64 offset;
};

enum drm_panfrost_param {
	DRM_PANFROST_PARAM_GPU_PROD_ID,
};

struct drm_panfrost_get_param {
	__u32 param;
	__u32 pad;
	__u64 value;
};

/**
 * Returns the offset for the BO in the GPU address space for this DRM fd.
 * This is the same value returned by drm_panfrost_create_bo, if that was called
 * from this DRM fd.
 */
struct drm_panfrost_get_bo_offset {
	__u32 handle;
	__u32 pad;
	__u64 offset;
};

#if defined(__cplusplus)
}
#endif

#endif /* _PANFROST_DRM_H_ */
