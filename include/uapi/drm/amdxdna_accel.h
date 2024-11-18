/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#ifndef _UAPI_AMDXDNA_ACCEL_H_
#define _UAPI_AMDXDNA_ACCEL_H_

#include <linux/stddef.h>
#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define AMDXDNA_INVALID_CMD_HANDLE	(~0UL)
#define AMDXDNA_INVALID_ADDR		(~0UL)
#define AMDXDNA_INVALID_CTX_HANDLE	0
#define AMDXDNA_INVALID_BO_HANDLE	0
#define AMDXDNA_INVALID_FENCE_HANDLE	0

enum amdxdna_device_type {
	AMDXDNA_DEV_TYPE_UNKNOWN = -1,
	AMDXDNA_DEV_TYPE_KMQ,
};

enum amdxdna_drm_ioctl_id {
	DRM_AMDXDNA_CREATE_HWCTX,
	DRM_AMDXDNA_DESTROY_HWCTX,
	DRM_AMDXDNA_CONFIG_HWCTX,
	DRM_AMDXDNA_CREATE_BO,
	DRM_AMDXDNA_GET_BO_INFO,
	DRM_AMDXDNA_SYNC_BO,
	DRM_AMDXDNA_EXEC_CMD,
};

/**
 * struct qos_info - QoS information for driver.
 * @gops: Giga operations per second.
 * @fps: Frames per second.
 * @dma_bandwidth: DMA bandwidtha.
 * @latency: Frame response latency.
 * @frame_exec_time: Frame execution time.
 * @priority: Request priority.
 *
 * User program can provide QoS hints to driver.
 */
struct amdxdna_qos_info {
	__u32 gops;
	__u32 fps;
	__u32 dma_bandwidth;
	__u32 latency;
	__u32 frame_exec_time;
	__u32 priority;
};

/**
 * struct amdxdna_drm_create_hwctx - Create hardware context.
 * @ext: MBZ.
 * @ext_flags: MBZ.
 * @qos_p: Address of QoS info.
 * @umq_bo: BO handle for user mode queue(UMQ).
 * @log_buf_bo: BO handle for log buffer.
 * @max_opc: Maximum operations per cycle.
 * @num_tiles: Number of AIE tiles.
 * @mem_size: Size of AIE tile memory.
 * @umq_doorbell: Returned offset of doorbell associated with UMQ.
 * @handle: Returned hardware context handle.
 * @syncobj_handle: Returned syncobj handle for command completion.
 */
struct amdxdna_drm_create_hwctx {
	__u64 ext;
	__u64 ext_flags;
	__u64 qos_p;
	__u32 umq_bo;
	__u32 log_buf_bo;
	__u32 max_opc;
	__u32 num_tiles;
	__u32 mem_size;
	__u32 umq_doorbell;
	__u32 handle;
	__u32 syncobj_handle;
};

/**
 * struct amdxdna_drm_destroy_hwctx - Destroy hardware context.
 * @handle: Hardware context handle.
 * @pad: Structure padding.
 */
struct amdxdna_drm_destroy_hwctx {
	__u32 handle;
	__u32 pad;
};

/**
 * struct amdxdna_cu_config - configuration for one CU
 * @cu_bo: CU configuration buffer bo handle.
 * @cu_func: Function of a CU.
 * @pad: Structure padding.
 */
struct amdxdna_cu_config {
	__u32 cu_bo;
	__u8  cu_func;
	__u8  pad[3];
};

/**
 * struct amdxdna_hwctx_param_config_cu - configuration for CUs in hardware context
 * @num_cus: Number of CUs to configure.
 * @pad: Structure padding.
 * @cu_configs: Array of CU configurations of struct amdxdna_cu_config.
 */
struct amdxdna_hwctx_param_config_cu {
	__u16 num_cus;
	__u16 pad[3];
	struct amdxdna_cu_config cu_configs[] __counted_by(num_cus);
};

enum amdxdna_drm_config_hwctx_param {
	DRM_AMDXDNA_HWCTX_CONFIG_CU,
	DRM_AMDXDNA_HWCTX_ASSIGN_DBG_BUF,
	DRM_AMDXDNA_HWCTX_REMOVE_DBG_BUF,
	DRM_AMDXDNA_HWCTX_CONFIG_NUM
};

/**
 * struct amdxdna_drm_config_hwctx - Configure hardware context.
 * @handle: hardware context handle.
 * @param_type: Value in enum amdxdna_drm_config_hwctx_param. Specifies the
 *              structure passed in via param_val.
 * @param_val: A structure specified by the param_type struct member.
 * @param_val_size: Size of the parameter buffer pointed to by the param_val.
 *		    If param_val is not a pointer, driver can ignore this.
 * @pad: Structure padding.
 *
 * Note: if the param_val is a pointer pointing to a buffer, the maximum size
 * of the buffer is 4KiB(PAGE_SIZE).
 */
struct amdxdna_drm_config_hwctx {
	__u32 handle;
	__u32 param_type;
	__u64 param_val;
	__u32 param_val_size;
	__u32 pad;
};

enum amdxdna_bo_type {
	AMDXDNA_BO_INVALID = 0,
	AMDXDNA_BO_SHMEM,
	AMDXDNA_BO_DEV_HEAP,
	AMDXDNA_BO_DEV,
	AMDXDNA_BO_CMD,
};

/**
 * struct amdxdna_drm_create_bo - Create a buffer object.
 * @flags: Buffer flags. MBZ.
 * @vaddr: User VA of buffer if applied. MBZ.
 * @size: Size in bytes.
 * @type: Buffer type.
 * @handle: Returned DRM buffer object handle.
 */
struct amdxdna_drm_create_bo {
	__u64	flags;
	__u64	vaddr;
	__u64	size;
	__u32	type;
	__u32	handle;
};

/**
 * struct amdxdna_drm_get_bo_info - Get buffer object information.
 * @ext: MBZ.
 * @ext_flags: MBZ.
 * @handle: DRM buffer object handle.
 * @pad: Structure padding.
 * @map_offset: Returned DRM fake offset for mmap().
 * @vaddr: Returned user VA of buffer. 0 in case user needs mmap().
 * @xdna_addr: Returned XDNA device virtual address.
 */
struct amdxdna_drm_get_bo_info {
	__u64 ext;
	__u64 ext_flags;
	__u32 handle;
	__u32 pad;
	__u64 map_offset;
	__u64 vaddr;
	__u64 xdna_addr;
};

/**
 * struct amdxdna_drm_sync_bo - Sync buffer object.
 * @handle: Buffer object handle.
 * @direction: Direction of sync, can be from device or to device.
 * @offset: Offset in the buffer to sync.
 * @size: Size in bytes.
 */
struct amdxdna_drm_sync_bo {
	__u32 handle;
#define SYNC_DIRECT_TO_DEVICE	0U
#define SYNC_DIRECT_FROM_DEVICE	1U
	__u32 direction;
	__u64 offset;
	__u64 size;
};

enum amdxdna_cmd_type {
	AMDXDNA_CMD_SUBMIT_EXEC_BUF = 0,
	AMDXDNA_CMD_SUBMIT_DEPENDENCY,
	AMDXDNA_CMD_SUBMIT_SIGNAL,
};

/**
 * struct amdxdna_drm_exec_cmd - Execute command.
 * @ext: MBZ.
 * @ext_flags: MBZ.
 * @hwctx: Hardware context handle.
 * @type: One of command type in enum amdxdna_cmd_type.
 * @cmd_handles: Array of command handles or the command handle itself
 *               in case of just one.
 * @args: Array of arguments for all command handles.
 * @cmd_count: Number of command handles in the cmd_handles array.
 * @arg_count: Number of arguments in the args array.
 * @seq: Returned sequence number for this command.
 */
struct amdxdna_drm_exec_cmd {
	__u64 ext;
	__u64 ext_flags;
	__u32 hwctx;
	__u32 type;
	__u64 cmd_handles;
	__u64 args;
	__u32 cmd_count;
	__u32 arg_count;
	__u64 seq;
};

#define DRM_IOCTL_AMDXDNA_CREATE_HWCTX \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDXDNA_CREATE_HWCTX, \
		 struct amdxdna_drm_create_hwctx)

#define DRM_IOCTL_AMDXDNA_DESTROY_HWCTX \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDXDNA_DESTROY_HWCTX, \
		 struct amdxdna_drm_destroy_hwctx)

#define DRM_IOCTL_AMDXDNA_CONFIG_HWCTX \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDXDNA_CONFIG_HWCTX, \
		 struct amdxdna_drm_config_hwctx)

#define DRM_IOCTL_AMDXDNA_CREATE_BO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDXDNA_CREATE_BO, \
		 struct amdxdna_drm_create_bo)

#define DRM_IOCTL_AMDXDNA_GET_BO_INFO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDXDNA_GET_BO_INFO, \
		 struct amdxdna_drm_get_bo_info)

#define DRM_IOCTL_AMDXDNA_SYNC_BO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDXDNA_SYNC_BO, \
		 struct amdxdna_drm_sync_bo)

#define DRM_IOCTL_AMDXDNA_EXEC_CMD \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDXDNA_EXEC_CMD, \
		 struct amdxdna_drm_exec_cmd)

#if defined(__cplusplus)
} /* extern c end */
#endif

#endif /* _UAPI_AMDXDNA_ACCEL_H_ */
