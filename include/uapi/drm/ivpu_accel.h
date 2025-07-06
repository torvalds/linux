/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (C) 2020-2025 Intel Corporation
 */

#ifndef __UAPI_IVPU_DRM_H__
#define __UAPI_IVPU_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_IVPU_GET_PARAM		  0x00
#define DRM_IVPU_SET_PARAM		  0x01
#define DRM_IVPU_BO_CREATE		  0x02
#define DRM_IVPU_BO_INFO		  0x03
#define DRM_IVPU_SUBMIT			  0x05
#define DRM_IVPU_BO_WAIT		  0x06
#define DRM_IVPU_METRIC_STREAMER_START	  0x07
#define DRM_IVPU_METRIC_STREAMER_STOP	  0x08
#define DRM_IVPU_METRIC_STREAMER_GET_DATA 0x09
#define DRM_IVPU_METRIC_STREAMER_GET_INFO 0x0a
#define DRM_IVPU_CMDQ_CREATE              0x0b
#define DRM_IVPU_CMDQ_DESTROY             0x0c
#define DRM_IVPU_CMDQ_SUBMIT              0x0d

#define DRM_IOCTL_IVPU_GET_PARAM                                               \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_GET_PARAM, struct drm_ivpu_param)

#define DRM_IOCTL_IVPU_SET_PARAM                                               \
	DRM_IOW(DRM_COMMAND_BASE + DRM_IVPU_SET_PARAM, struct drm_ivpu_param)

#define DRM_IOCTL_IVPU_BO_CREATE                                               \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_BO_CREATE, struct drm_ivpu_bo_create)

#define DRM_IOCTL_IVPU_BO_INFO                                                 \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_BO_INFO, struct drm_ivpu_bo_info)

#define DRM_IOCTL_IVPU_SUBMIT                                                  \
	DRM_IOW(DRM_COMMAND_BASE + DRM_IVPU_SUBMIT, struct drm_ivpu_submit)

#define DRM_IOCTL_IVPU_BO_WAIT                                                 \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_BO_WAIT, struct drm_ivpu_bo_wait)

#define DRM_IOCTL_IVPU_METRIC_STREAMER_START                                   \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_METRIC_STREAMER_START,            \
		 struct drm_ivpu_metric_streamer_start)

#define DRM_IOCTL_IVPU_METRIC_STREAMER_STOP                                    \
	DRM_IOW(DRM_COMMAND_BASE + DRM_IVPU_METRIC_STREAMER_STOP,              \
		struct drm_ivpu_metric_streamer_stop)

#define DRM_IOCTL_IVPU_METRIC_STREAMER_GET_DATA                                \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_METRIC_STREAMER_GET_DATA,         \
		 struct drm_ivpu_metric_streamer_get_data)

#define DRM_IOCTL_IVPU_METRIC_STREAMER_GET_INFO                                \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_METRIC_STREAMER_GET_INFO,         \
		 struct drm_ivpu_metric_streamer_get_data)

#define DRM_IOCTL_IVPU_CMDQ_CREATE                                             \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_CMDQ_CREATE, struct drm_ivpu_cmdq_create)

#define DRM_IOCTL_IVPU_CMDQ_DESTROY                                            \
	DRM_IOW(DRM_COMMAND_BASE + DRM_IVPU_CMDQ_DESTROY, struct drm_ivpu_cmdq_destroy)

#define DRM_IOCTL_IVPU_CMDQ_SUBMIT                                             \
	DRM_IOW(DRM_COMMAND_BASE + DRM_IVPU_CMDQ_SUBMIT, struct drm_ivpu_cmdq_submit)

/**
 * DOC: contexts
 *
 * VPU contexts have private virtual address space, job queues and priority.
 * Each context is identified by an unique ID. Context is created on open().
 */

#define DRM_IVPU_PARAM_DEVICE_ID	    0
#define DRM_IVPU_PARAM_DEVICE_REVISION	    1
#define DRM_IVPU_PARAM_PLATFORM_TYPE	    2
#define DRM_IVPU_PARAM_CORE_CLOCK_RATE	    3
#define DRM_IVPU_PARAM_NUM_CONTEXTS	    4
#define DRM_IVPU_PARAM_CONTEXT_BASE_ADDRESS 5
#define DRM_IVPU_PARAM_CONTEXT_PRIORITY	    6 /* Deprecated */
#define DRM_IVPU_PARAM_CONTEXT_ID	    7
#define DRM_IVPU_PARAM_FW_API_VERSION	    8
#define DRM_IVPU_PARAM_ENGINE_HEARTBEAT	    9
#define DRM_IVPU_PARAM_UNIQUE_INFERENCE_ID  10
#define DRM_IVPU_PARAM_TILE_CONFIG	    11
#define DRM_IVPU_PARAM_SKU		    12
#define DRM_IVPU_PARAM_CAPABILITIES	    13

#define DRM_IVPU_PLATFORM_TYPE_SILICON	    0

/* Deprecated, use DRM_IVPU_JOB_PRIORITY */
#define DRM_IVPU_CONTEXT_PRIORITY_IDLE	    0
#define DRM_IVPU_CONTEXT_PRIORITY_NORMAL    1
#define DRM_IVPU_CONTEXT_PRIORITY_FOCUS	    2
#define DRM_IVPU_CONTEXT_PRIORITY_REALTIME  3

#define DRM_IVPU_JOB_PRIORITY_DEFAULT  0
#define DRM_IVPU_JOB_PRIORITY_IDLE     1
#define DRM_IVPU_JOB_PRIORITY_NORMAL   2
#define DRM_IVPU_JOB_PRIORITY_FOCUS    3
#define DRM_IVPU_JOB_PRIORITY_REALTIME 4

/**
 * DRM_IVPU_CAP_METRIC_STREAMER
 *
 * Metric streamer support. Provides sampling of various hardware performance
 * metrics like DMA bandwidth and cache miss/hits. Can be used for profiling.
 */
#define DRM_IVPU_CAP_METRIC_STREAMER	1
/**
 * DRM_IVPU_CAP_DMA_MEMORY_RANGE
 *
 * Driver has capability to allocate separate memory range
 * accessible by hardware DMA.
 */
#define DRM_IVPU_CAP_DMA_MEMORY_RANGE	2
/**
 * DRM_IVPU_CAP_MANAGE_CMDQ
 *
 * Driver supports explicit command queue operations like command queue create,
 * command queue destroy and submit job on specific command queue.
 */
#define DRM_IVPU_CAP_MANAGE_CMDQ       3

/**
 * struct drm_ivpu_param - Get/Set VPU parameters
 */
struct drm_ivpu_param {
	/**
	 * @param:
	 *
	 * Supported params:
	 *
	 * %DRM_IVPU_PARAM_DEVICE_ID:
	 * PCI Device ID of the VPU device (read-only)
	 *
	 * %DRM_IVPU_PARAM_DEVICE_REVISION:
	 * VPU device revision (read-only)
	 *
	 * %DRM_IVPU_PARAM_PLATFORM_TYPE:
	 * Returns %DRM_IVPU_PLATFORM_TYPE_SILICON on real hardware or device specific
	 * platform type when executing on a simulator or emulator (read-only)
	 *
	 * %DRM_IVPU_PARAM_CORE_CLOCK_RATE:
	 * Maximum frequency of the NPU data processing unit clock (read-only)
	 *
	 * %DRM_IVPU_PARAM_NUM_CONTEXTS:
	 * Maximum number of simultaneously existing contexts (read-only)
	 *
	 * %DRM_IVPU_PARAM_CONTEXT_BASE_ADDRESS:
	 * Lowest VPU virtual address available in the current context (read-only)
	 *
	 * %DRM_IVPU_PARAM_CONTEXT_ID:
	 * Current context ID, always greater than 0 (read-only)
	 *
	 * %DRM_IVPU_PARAM_FW_API_VERSION:
	 * Firmware API version array (read-only)
	 *
	 * %DRM_IVPU_PARAM_ENGINE_HEARTBEAT:
	 * Heartbeat value from an engine (read-only).
	 * Engine ID (i.e. DRM_IVPU_ENGINE_COMPUTE) is given via index.
	 *
	 * %DRM_IVPU_PARAM_UNIQUE_INFERENCE_ID:
	 * Device-unique inference ID (read-only)
	 *
	 * %DRM_IVPU_PARAM_TILE_CONFIG:
	 * VPU tile configuration  (read-only)
	 *
	 * %DRM_IVPU_PARAM_SKU:
	 * VPU SKU ID (read-only)
	 *
	 * %DRM_IVPU_PARAM_CAPABILITIES:
	 * Supported capabilities (read-only)
	 */
	__u32 param;

	/** @index: Index for params that have multiple instances */
	__u32 index;

	/** @value: Param value */
	__u64 value;
};

#define DRM_IVPU_BO_SHAVE_MEM  0x00000001
#define DRM_IVPU_BO_HIGH_MEM   DRM_IVPU_BO_SHAVE_MEM
#define DRM_IVPU_BO_MAPPABLE   0x00000002
#define DRM_IVPU_BO_DMA_MEM    0x00000004

#define DRM_IVPU_BO_CACHED     0x00000000
#define DRM_IVPU_BO_UNCACHED   0x00010000
#define DRM_IVPU_BO_WC	       0x00020000
#define DRM_IVPU_BO_CACHE_MASK 0x00030000

#define DRM_IVPU_BO_FLAGS \
	(DRM_IVPU_BO_HIGH_MEM | \
	 DRM_IVPU_BO_MAPPABLE | \
	 DRM_IVPU_BO_DMA_MEM | \
	 DRM_IVPU_BO_CACHE_MASK)

/**
 * struct drm_ivpu_bo_create - Create BO backed by SHMEM
 *
 * Create GEM buffer object allocated in SHMEM memory.
 */
struct drm_ivpu_bo_create {
	/** @size: The size in bytes of the allocated memory */
	__u64 size;

	/**
	 * @flags:
	 *
	 * Supported flags:
	 *
	 * %DRM_IVPU_BO_HIGH_MEM:
	 *
	 * Allocate VPU address from >4GB range.
	 * Buffer object with vpu address >4GB can be always accessed by the
	 * VPU DMA engine, but some HW generation may not be able to access
	 * this memory from then firmware running on the VPU management processor.
	 * Suitable for input, output and some scratch buffers.
	 *
	 * %DRM_IVPU_BO_MAPPABLE:
	 *
	 * Buffer object can be mapped using mmap().
	 *
	 * %DRM_IVPU_BO_CACHED:
	 *
	 * Allocated BO will be cached on host side (WB) and snooped on the VPU side.
	 * This is the default caching mode.
	 *
	 * %DRM_IVPU_BO_UNCACHED:
	 *
	 * Not supported. Use DRM_IVPU_BO_WC instead.
	 *
	 * %DRM_IVPU_BO_WC:
	 *
	 * Allocated BO will use write combining buffer for writes but reads will be
	 * uncached.
	 */
	__u32 flags;

	/** @handle: Returned GEM object handle */
	__u32 handle;

	/** @vpu_addr: Returned VPU virtual address */
	__u64 vpu_addr;
};

/**
 * struct drm_ivpu_bo_info - Query buffer object info
 */
struct drm_ivpu_bo_info {
	/** @handle: Handle of the queried BO */
	__u32 handle;

	/** @flags: Returned flags used to create the BO */
	__u32 flags;

	/** @vpu_addr: Returned VPU virtual address */
	__u64 vpu_addr;

	/**
	 * @mmap_offset:
	 *
	 * Returned offset to be used in mmap(). 0 in case the BO is not mappable.
	 */
	__u64 mmap_offset;

	/** @size: Returned GEM object size, aligned to PAGE_SIZE */
	__u64 size;
};

/* drm_ivpu_submit engines */
#define DRM_IVPU_ENGINE_COMPUTE 0
#define DRM_IVPU_ENGINE_COPY    1 /* Deprecated */

/**
 * struct drm_ivpu_submit - Submit commands to the VPU
 *
 * Execute a single command buffer on a given VPU engine.
 * Handles to all referenced buffer objects have to be provided in @buffers_ptr.
 *
 * User space may wait on job completion using %DRM_IVPU_BO_WAIT ioctl.
 */
struct drm_ivpu_submit {
	/**
	 * @buffers_ptr:
	 *
	 * A pointer to an u32 array of GEM handles of the BOs required for this job.
	 * The number of elements in the array must be equal to the value given by @buffer_count.
	 *
	 * The first BO is the command buffer. The rest of array has to contain all
	 * BOs referenced from the command buffer.
	 */
	__u64 buffers_ptr;

	/** @buffer_count: Number of elements in the @buffers_ptr */
	__u32 buffer_count;

	/**
	 * @engine: Select the engine this job should be executed on
	 *
	 * %DRM_IVPU_ENGINE_COMPUTE:
	 *
	 * Performs Deep Learning Neural Compute Inference Operations
	 */
	__u32 engine;

	/** @flags: Reserved for future use - must be zero */
	__u32 flags;

	/**
	 * @commands_offset:
	 *
	 * Offset inside the first buffer in @buffers_ptr containing commands
	 * to be executed. The offset has to be 8-byte aligned.
	 */
	__u32 commands_offset;

	/**
	 * @priority:
	 *
	 * Priority to be set for related job command queue, can be one of the following:
	 * %DRM_IVPU_JOB_PRIORITY_DEFAULT
	 * %DRM_IVPU_JOB_PRIORITY_IDLE
	 * %DRM_IVPU_JOB_PRIORITY_NORMAL
	 * %DRM_IVPU_JOB_PRIORITY_FOCUS
	 * %DRM_IVPU_JOB_PRIORITY_REALTIME
	 */
	__u32 priority;
};

/**
 * struct drm_ivpu_cmdq_submit - Submit commands to the VPU using explicit command queue
 *
 * Execute a single command buffer on a given command queue.
 * Handles to all referenced buffer objects have to be provided in @buffers_ptr.
 *
 * User space may wait on job completion using %DRM_IVPU_BO_WAIT ioctl.
 */
struct drm_ivpu_cmdq_submit {
	/**
	 * @buffers_ptr:
	 *
	 * A pointer to an u32 array of GEM handles of the BOs required for this job.
	 * The number of elements in the array must be equal to the value given by @buffer_count.
	 *
	 * The first BO is the command buffer. The rest of array has to contain all
	 * BOs referenced from the command buffer.
	 */
	__u64 buffers_ptr;

	/** @buffer_count: Number of elements in the @buffers_ptr */
	__u32 buffer_count;

	/** @cmdq_id: ID for the command queue where job will be submitted */
	__u32 cmdq_id;

	/** @flags: Reserved for future use - must be zero */
	__u32 flags;

	/**
	 * @commands_offset:
	 *
	 * Offset inside the first buffer in @buffers_ptr containing commands
	 * to be executed. The offset has to be 8-byte aligned.
	 */
	__u32 commands_offset;
};

/* drm_ivpu_bo_wait job status codes */
#define DRM_IVPU_JOB_STATUS_SUCCESS 0
#define DRM_IVPU_JOB_STATUS_ABORTED 256

/**
 * struct drm_ivpu_bo_wait - Wait for BO to become inactive
 *
 * Blocks until a given buffer object becomes inactive.
 * With @timeout_ms set to 0 returns immediately.
 */
struct drm_ivpu_bo_wait {
	/** @handle: Handle to the buffer object to be waited on */
	__u32 handle;

	/** @flags: Reserved for future use - must be zero */
	__u32 flags;

	/** @timeout_ns: Absolute timeout in nanoseconds (may be zero) */
	__s64 timeout_ns;

	/**
	 * @job_status:
	 *
	 * Job status code which is updated after the job is completed.
	 * &DRM_IVPU_JOB_STATUS_SUCCESS or device specific error otherwise.
	 * Valid only if @handle points to a command buffer.
	 */
	__u32 job_status;

	/** @pad: Padding - must be zero */
	__u32 pad;
};

/**
 * struct drm_ivpu_metric_streamer_start - Start collecting metric data
 */
struct drm_ivpu_metric_streamer_start {
	/** @metric_group_mask: Indicates metric streamer instance */
	__u64 metric_group_mask;
	/** @sampling_period_ns: Sampling period in nanoseconds */
	__u64 sampling_period_ns;
	/**
	 * @read_period_samples:
	 *
	 * Number of samples after which user space will try to read the data.
	 * Reading the data after significantly longer period may cause data loss.
	 */
	__u32 read_period_samples;
	/** @sample_size: Returned size of a single sample in bytes */
	__u32 sample_size;
	/** @max_data_size: Returned max @data_size from %DRM_IOCTL_IVPU_METRIC_STREAMER_GET_DATA */
	__u32 max_data_size;
};

/**
 * struct drm_ivpu_metric_streamer_get_data - Copy collected metric data
 */
struct drm_ivpu_metric_streamer_get_data {
	/** @metric_group_mask: Indicates metric streamer instance */
	__u64 metric_group_mask;
	/** @buffer_ptr: A pointer to a destination for the copied data */
	__u64 buffer_ptr;
	/** @buffer_size: Size of the destination buffer */
	__u64 buffer_size;
	/**
	 * @data_size: Returned size of copied metric data
	 *
	 * If the @buffer_size is zero, returns the amount of data ready to be copied.
	 */
	__u64 data_size;
};

/**
 * struct drm_ivpu_cmdq_create - Create command queue for job submission
 */
struct drm_ivpu_cmdq_create {
	/** @cmdq_id: Returned ID of created command queue */
	__u32 cmdq_id;
	/**
	 * @priority:
	 *
	 * Priority to be set for related job command queue, can be one of the following:
	 * %DRM_IVPU_JOB_PRIORITY_DEFAULT
	 * %DRM_IVPU_JOB_PRIORITY_IDLE
	 * %DRM_IVPU_JOB_PRIORITY_NORMAL
	 * %DRM_IVPU_JOB_PRIORITY_FOCUS
	 * %DRM_IVPU_JOB_PRIORITY_REALTIME
	 */
	__u32 priority;
};

/**
 * struct drm_ivpu_cmdq_destroy - Destroy a command queue
 */
struct drm_ivpu_cmdq_destroy {
	/** @cmdq_id: ID of command queue to destroy */
	__u32 cmdq_id;
};

/**
 * struct drm_ivpu_metric_streamer_stop - Stop collecting metric data
 */
struct drm_ivpu_metric_streamer_stop {
	/** @metric_group_mask: Indicates metric streamer instance */
	__u64 metric_group_mask;
};

#if defined(__cplusplus)
}
#endif

#endif /* __UAPI_IVPU_DRM_H__ */
