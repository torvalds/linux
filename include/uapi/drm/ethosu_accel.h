/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2025 Arm, Ltd. */
#ifndef _ETHOSU_DRM_H_
#define _ETHOSU_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * DOC: IOCTL IDs
 *
 * enum drm_ethosu_ioctl_id - IOCTL IDs
 *
 * Place new ioctls at the end, don't re-order, don't replace or remove entries.
 *
 * These IDs are not meant to be used directly. Use the DRM_IOCTL_ETHOSU_xxx
 * definitions instead.
 */
enum drm_ethosu_ioctl_id {
	/** @DRM_ETHOSU_DEV_QUERY: Query device information. */
	DRM_ETHOSU_DEV_QUERY = 0,

	/** @DRM_ETHOSU_BO_CREATE: Create a buffer object. */
	DRM_ETHOSU_BO_CREATE,

	/** @DRM_ETHOSU_BO_WAIT: Wait on a buffer object's fence. */
	DRM_ETHOSU_BO_WAIT,

	/**
	 * @DRM_ETHOSU_BO_MMAP_OFFSET: Get the file offset to pass to
	 * mmap to map a GEM object.
	 */
	DRM_ETHOSU_BO_MMAP_OFFSET,

	/**
	 * @DRM_ETHOSU_CMDSTREAM_BO_CREATE: Create a command stream buffer
	 * object.
	 */
	DRM_ETHOSU_CMDSTREAM_BO_CREATE,

	/** @DRM_ETHOSU_SUBMIT: Submit a job and BOs to run. */
	DRM_ETHOSU_SUBMIT,
};

/**
 * DOC: IOCTL arguments
 */

/**
 * enum drm_ethosu_dev_query_type - Query type
 *
 * Place new types at the end, don't re-order, don't remove or replace.
 */
enum drm_ethosu_dev_query_type {
	/** @DRM_ETHOSU_DEV_QUERY_NPU_INFO: Query NPU information. */
	DRM_ETHOSU_DEV_QUERY_NPU_INFO = 0,
};

/**
 * struct drm_ethosu_gpu_info - NPU information
 *
 * Structure grouping all queryable information relating to the NPU.
 */
struct drm_ethosu_npu_info {
	/** @id : NPU ID. */
	__u32 id;
#define DRM_ETHOSU_ARCH_MAJOR(x)			((x) >> 28)
#define DRM_ETHOSU_ARCH_MINOR(x)			(((x) >> 20) & 0xff)
#define DRM_ETHOSU_ARCH_PATCH(x)			(((x) >> 16) & 0xf)
#define DRM_ETHOSU_PRODUCT_MAJOR(x)		(((x) >> 12) & 0xf)
#define DRM_ETHOSU_VERSION_MAJOR(x)		(((x) >> 8) & 0xf)
#define DRM_ETHOSU_VERSION_MINOR(x)		(((x) >> 4) & 0xff)
#define DRM_ETHOSU_VERSION_STATUS(x)		((x) & 0xf)

	/** @gpu_rev: GPU revision. */
	__u32 config;

	__u32 sram_size;
};

/**
 * struct drm_ethosu_dev_query - Arguments passed to DRM_ETHOSU_IOCTL_DEV_QUERY
 */
struct drm_ethosu_dev_query {
	/** @type: the query type (see drm_ethosu_dev_query_type). */
	__u32 type;

	/**
	 * @size: size of the type being queried.
	 *
	 * If pointer is NULL, size is updated by the driver to provide the
	 * output structure size. If pointer is not NULL, the driver will
	 * only copy min(size, actual_structure_size) bytes to the pointer,
	 * and update the size accordingly. This allows us to extend query
	 * types without breaking userspace.
	 */
	__u32 size;

	/**
	 * @pointer: user pointer to a query type struct.
	 *
	 * Pointer can be NULL, in which case, nothing is copied, but the
	 * actual structure size is returned. If not NULL, it must point to
	 * a location that's large enough to hold size bytes.
	 */
	__u64 pointer;
};

/**
 * enum drm_ethosu_bo_flags - Buffer object flags, passed at creation time.
 */
enum drm_ethosu_bo_flags {
	/**
	 * @DRM_ETHOSU_BO_NO_MMAP: The buffer object will never be CPU-mapped
	 * in userspace.
	 */
	DRM_ETHOSU_BO_NO_MMAP = (1 << 0),
};

/**
 * struct drm_ethosu_bo_create - Arguments passed to DRM_IOCTL_ETHOSU_BO_CREATE.
 */
struct drm_ethosu_bo_create {
	/**
	 * @size: Requested size for the object
	 *
	 * The (page-aligned) allocated size for the object will be returned.
	 */
	__u64 size;

	/**
	 * @flags: Flags. Must be a combination of drm_ethosu_bo_flags flags.
	 */
	__u32 flags;

	/**
	 * @handle: Returned handle for the object.
	 *
	 * Object handles are nonzero.
	 */
	__u32 handle;
};

/**
 * struct drm_ethosu_bo_mmap_offset - Arguments passed to DRM_IOCTL_ETHOSU_BO_MMAP_OFFSET.
 */
struct drm_ethosu_bo_mmap_offset {
	/** @handle: Handle of the object we want an mmap offset for. */
	__u32 handle;

	/** @pad: MBZ. */
	__u32 pad;

	/** @offset: The fake offset to use for subsequent mmap calls. */
	__u64 offset;
};

/**
 * struct drm_ethosu_wait_bo - ioctl argument for waiting for
 * completion of the last DRM_ETHOSU_SUBMIT on a BO.
 *
 * This is useful for cases where multiple processes might be
 * rendering to a BO and you want to wait for all rendering to be
 * completed.
 */
struct drm_ethosu_bo_wait {
	__u32 handle;
	__u32 pad;
	__s64 timeout_ns;	/* absolute */
};

struct drm_ethosu_cmdstream_bo_create {
	/* Size of the data argument. */
	__u32 size;

	/* Flags, currently must be 0. */
	__u32 flags;

	/* Pointer to the data. */
	__u64 data;

	/** Returned GEM handle for the BO. */
	__u32 handle;

	/* Pad, must be 0. */
	__u32 pad;
};

/**
 * struct drm_ethosu_job - A job to be run on the NPU
 *
 * The kernel will schedule the execution of this job taking into account its
 * dependencies with other jobs. All tasks in the same job will be executed
 * sequentially on the same core, to benefit from memory residency in SRAM.
 */
struct drm_ethosu_job {
	/** Input: BO handle for cmdstream. */
	__u32 cmd_bo;

	/** Input: Amount of SRAM to use. */
	__u32 sram_size;

#define ETHOSU_MAX_REGIONS	8
	/** Input: Array of BO handles for each region. */
	__u32 region_bo_handles[ETHOSU_MAX_REGIONS];
};

/**
 * struct drm_ethosu_submit - ioctl argument for submitting commands to the NPU.
 *
 * The kernel will schedule the execution of these jobs in dependency order.
 */
struct drm_ethosu_submit {
	/** Input: Pointer to an array of struct drm_ethosu_job. */
	__u64 jobs;

	/** Input: Number of jobs passed in. */
	__u32 job_count;

	/** Reserved, must be zero. */
	__u32 pad;
};

/**
 * DRM_IOCTL_ETHOSU() - Build a ethosu IOCTL number
 * @__access: Access type. Must be R, W or RW.
 * @__id: One of the DRM_ETHOSU_xxx id.
 * @__type: Suffix of the type being passed to the IOCTL.
 *
 * Don't use this macro directly, use the DRM_IOCTL_ETHOSU_xxx
 * values instead.
 *
 * Return: An IOCTL number to be passed to ioctl() from userspace.
 */
#define DRM_IOCTL_ETHOSU(__access, __id, __type) \
	DRM_IO ## __access(DRM_COMMAND_BASE + DRM_ETHOSU_ ## __id, \
			   struct drm_ethosu_ ## __type)

enum {
	DRM_IOCTL_ETHOSU_DEV_QUERY =
		DRM_IOCTL_ETHOSU(WR, DEV_QUERY, dev_query),
	DRM_IOCTL_ETHOSU_BO_CREATE =
		DRM_IOCTL_ETHOSU(WR, BO_CREATE, bo_create),
	DRM_IOCTL_ETHOSU_BO_WAIT =
		DRM_IOCTL_ETHOSU(WR, BO_WAIT, bo_wait),
	DRM_IOCTL_ETHOSU_BO_MMAP_OFFSET =
		DRM_IOCTL_ETHOSU(WR, BO_MMAP_OFFSET, bo_mmap_offset),
	DRM_IOCTL_ETHOSU_CMDSTREAM_BO_CREATE =
		DRM_IOCTL_ETHOSU(WR, CMDSTREAM_BO_CREATE, cmdstream_bo_create),
	DRM_IOCTL_ETHOSU_SUBMIT =
		DRM_IOCTL_ETHOSU(WR, SUBMIT, submit),
};

#if defined(__cplusplus)
}
#endif

#endif /* _ETHOSU_DRM_H_ */
