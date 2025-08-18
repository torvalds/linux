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
#define DRM_ROCKET_SUBMIT			0x01
#define DRM_ROCKET_PREP_BO			0x02
#define DRM_ROCKET_FINI_BO			0x03

#define DRM_IOCTL_ROCKET_CREATE_BO		DRM_IOWR(DRM_COMMAND_BASE + DRM_ROCKET_CREATE_BO, struct drm_rocket_create_bo)
#define DRM_IOCTL_ROCKET_SUBMIT			DRM_IOW(DRM_COMMAND_BASE + DRM_ROCKET_SUBMIT, struct drm_rocket_submit)
#define DRM_IOCTL_ROCKET_PREP_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_ROCKET_PREP_BO, struct drm_rocket_prep_bo)
#define DRM_IOCTL_ROCKET_FINI_BO		DRM_IOW(DRM_COMMAND_BASE + DRM_ROCKET_FINI_BO, struct drm_rocket_fini_bo)

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

/**
 * struct drm_rocket_prep_bo - ioctl argument for starting CPU ownership of the BO.
 *
 * Takes care of waiting for any NPU jobs that might still use the NPU and performs cache
 * synchronization.
 */
struct drm_rocket_prep_bo {
	/** Input: GEM handle of the buffer object. */
	__u32 handle;

	/** Reserved, must be zero. */
	__u32 reserved;

	/** Input: Amount of time to wait for NPU jobs. */
	__s64 timeout_ns;
};

/**
 * struct drm_rocket_fini_bo - ioctl argument for finishing CPU ownership of the BO.
 *
 * Synchronize caches for NPU access.
 */
struct drm_rocket_fini_bo {
	/** Input: GEM handle of the buffer object. */
	__u32 handle;

	/** Reserved, must be zero. */
	__u32 reserved;
};

/**
 * struct drm_rocket_task - A task to be run on the NPU
 *
 * A task is the smallest unit of work that can be run on the NPU.
 */
struct drm_rocket_task {
	/** Input: DMA address to NPU mapping of register command buffer */
	__u32 regcmd;

	/** Input: Number of commands in the register command buffer */
	__u32 regcmd_count;
};

/**
 * struct drm_rocket_job - A job to be run on the NPU
 *
 * The kernel will schedule the execution of this job taking into account its
 * dependencies with other jobs. All tasks in the same job will be executed
 * sequentially on the same core, to benefit from memory residency in SRAM.
 */
struct drm_rocket_job {
	/** Input: Pointer to an array of struct drm_rocket_task. */
	__u64 tasks;

	/** Input: Pointer to a u32 array of the BOs that are read by the job. */
	__u64 in_bo_handles;

	/** Input: Pointer to a u32 array of the BOs that are written to by the job. */
	__u64 out_bo_handles;

	/** Input: Number of tasks passed in. */
	__u32 task_count;

	/** Input: Size in bytes of the structs in the @tasks field. */
	__u32 task_struct_size;

	/** Input: Number of input BO handles passed in (size is that times 4). */
	__u32 in_bo_handle_count;

	/** Input: Number of output BO handles passed in (size is that times 4). */
	__u32 out_bo_handle_count;
};

/**
 * struct drm_rocket_submit - ioctl argument for submitting commands to the NPU.
 *
 * The kernel will schedule the execution of these jobs in dependency order.
 */
struct drm_rocket_submit {
	/** Input: Pointer to an array of struct drm_rocket_job. */
	__u64 jobs;

	/** Input: Number of jobs passed in. */
	__u32 job_count;

	/** Input: Size in bytes of the structs in the @jobs field. */
	__u32 job_struct_size;

	/** Reserved, must be zero. */
	__u64 reserved;
};

#if defined(__cplusplus)
}
#endif

#endif /* __DRM_UAPI_ROCKET_ACCEL_H__ */
