/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __UAPI_IVPU_DRM_H__
#define __UAPI_IVPU_DRM_H__

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_IVPU_DRIVER_MAJOR 1
#define DRM_IVPU_DRIVER_MINOR 0

#define DRM_IVPU_GET_PARAM		  0x00
#define DRM_IVPU_SET_PARAM		  0x01

#define DRM_IOCTL_IVPU_GET_PARAM                                               \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_IVPU_GET_PARAM, struct drm_ivpu_param)

#define DRM_IOCTL_IVPU_SET_PARAM                                               \
	DRM_IOW(DRM_COMMAND_BASE + DRM_IVPU_SET_PARAM, struct drm_ivpu_param)

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
#define DRM_IVPU_PARAM_CONTEXT_PRIORITY	    6
#define DRM_IVPU_PARAM_CONTEXT_ID	    7

#define DRM_IVPU_PLATFORM_TYPE_SILICON	    0

#define DRM_IVPU_CONTEXT_PRIORITY_IDLE	    0
#define DRM_IVPU_CONTEXT_PRIORITY_NORMAL    1
#define DRM_IVPU_CONTEXT_PRIORITY_FOCUS	    2
#define DRM_IVPU_CONTEXT_PRIORITY_REALTIME  3

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
	 * Current PLL frequency (read-only)
	 *
	 * %DRM_IVPU_PARAM_NUM_CONTEXTS:
	 * Maximum number of simultaneously existing contexts (read-only)
	 *
	 * %DRM_IVPU_PARAM_CONTEXT_BASE_ADDRESS:
	 * Lowest VPU virtual address available in the current context (read-only)
	 *
	 * %DRM_IVPU_PARAM_CONTEXT_PRIORITY:
	 * Value of current context scheduling priority (read-write).
	 * See DRM_IVPU_CONTEXT_PRIORITY_* for possible values.
	 *
	 * %DRM_IVPU_PARAM_CONTEXT_ID:
	 * Current context ID, always greater than 0 (read-only)
	 *
	 */
	__u32 param;

	/** @index: Index for params that have multiple instances */
	__u32 index;

	/** @value: Param value */
	__u64 value;
};

#if defined(__cplusplus)
}
#endif

#endif /* __UAPI_IVPU_DRM_H__ */
