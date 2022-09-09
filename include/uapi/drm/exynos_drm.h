/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* exynos_drm.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _UAPI_EXYNOS_DRM_H_
#define _UAPI_EXYNOS_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * User-desired buffer creation information structure.
 *
 * @size: user-desired memory allocation size.
 *	- this size value would be page-aligned internally.
 * @flags: user request for setting memory type or cache attributes.
 * @handle: returned a handle to created gem object.
 *	- this handle will be set by gem module of kernel side.
 */
struct drm_exynos_gem_create {
	__u64 size;
	__u32 flags;
	__u32 handle;
};

/**
 * A structure for getting a fake-offset that can be used with mmap.
 *
 * @handle: handle of gem object.
 * @reserved: just padding to be 64-bit aligned.
 * @offset: a fake-offset of gem object.
 */
struct drm_exynos_gem_map {
	__u32 handle;
	__u32 reserved;
	__u64 offset;
};

/**
 * A structure to gem information.
 *
 * @handle: a handle to gem object created.
 * @flags: flag value including memory type and cache attribute and
 *	this value would be set by driver.
 * @size: size to memory region allocated by gem and this size would
 *	be set by driver.
 */
struct drm_exynos_gem_info {
	__u32 handle;
	__u32 flags;
	__u64 size;
};

/**
 * A structure for user connection request of virtual display.
 *
 * @connection: indicate whether doing connection or not by user.
 * @extensions: if this value is 1 then the vidi driver would need additional
 *	128bytes edid data.
 * @edid: the edid data pointer from user side.
 */
struct drm_exynos_vidi_connection {
	__u32 connection;
	__u32 extensions;
	__u64 edid;
};

/* memory type definitions. */
enum e_drm_exynos_gem_mem_type {
	/* Physically Continuous memory and used as default. */
	EXYNOS_BO_CONTIG	= 0 << 0,
	/* Physically Non-Continuous memory. */
	EXYNOS_BO_NONCONTIG	= 1 << 0,
	/* non-cachable mapping and used as default. */
	EXYNOS_BO_NONCACHABLE	= 0 << 1,
	/* cachable mapping. */
	EXYNOS_BO_CACHABLE	= 1 << 1,
	/* write-combine mapping. */
	EXYNOS_BO_WC		= 1 << 2,
	EXYNOS_BO_MASK		= EXYNOS_BO_NONCONTIG | EXYNOS_BO_CACHABLE |
					EXYNOS_BO_WC
};

struct drm_exynos_g2d_get_ver {
	__u32	major;
	__u32	minor;
};

struct drm_exynos_g2d_cmd {
	__u32	offset;
	__u32	data;
};

enum drm_exynos_g2d_buf_type {
	G2D_BUF_USERPTR = 1 << 31,
};

enum drm_exynos_g2d_event_type {
	G2D_EVENT_NOT,
	G2D_EVENT_NONSTOP,
	G2D_EVENT_STOP,		/* not yet */
};

struct drm_exynos_g2d_userptr {
	unsigned long userptr;
	unsigned long size;
};

struct drm_exynos_g2d_set_cmdlist {
	__u64					cmd;
	__u64					cmd_buf;
	__u32					cmd_nr;
	__u32					cmd_buf_nr;

	/* for g2d event */
	__u64					event_type;
	__u64					user_data;
};

struct drm_exynos_g2d_exec {
	__u64					async;
};

/* Exynos DRM IPP v2 API */

/**
 * Enumerate available IPP hardware modules.
 *
 * @count_ipps: size of ipp_id array / number of ipp modules (set by driver)
 * @reserved: padding
 * @ipp_id_ptr: pointer to ipp_id array or NULL
 */
struct drm_exynos_ioctl_ipp_get_res {
	__u32 count_ipps;
	__u32 reserved;
	__u64 ipp_id_ptr;
};

enum drm_exynos_ipp_format_type {
	DRM_EXYNOS_IPP_FORMAT_SOURCE		= 0x01,
	DRM_EXYNOS_IPP_FORMAT_DESTINATION	= 0x02,
};

struct drm_exynos_ipp_format {
	__u32 fourcc;
	__u32 type;
	__u64 modifier;
};

enum drm_exynos_ipp_capability {
	DRM_EXYNOS_IPP_CAP_CROP		= 0x01,
	DRM_EXYNOS_IPP_CAP_ROTATE	= 0x02,
	DRM_EXYNOS_IPP_CAP_SCALE	= 0x04,
	DRM_EXYNOS_IPP_CAP_CONVERT	= 0x08,
};

/**
 * Get IPP hardware capabilities and supported image formats.
 *
 * @ipp_id: id of IPP module to query
 * @capabilities: bitmask of drm_exynos_ipp_capability (set by driver)
 * @reserved: padding
 * @formats_count: size of formats array (in entries) / number of filled
 *		   formats (set by driver)
 * @formats_ptr: pointer to formats array or NULL
 */
struct drm_exynos_ioctl_ipp_get_caps {
	__u32 ipp_id;
	__u32 capabilities;
	__u32 reserved;
	__u32 formats_count;
	__u64 formats_ptr;
};

enum drm_exynos_ipp_limit_type {
	/* size (horizontal/vertial) limits, in pixels (min, max, alignment) */
	DRM_EXYNOS_IPP_LIMIT_TYPE_SIZE		= 0x0001,
	/* scale ratio (horizonta/vertial), 16.16 fixed point (min, max) */
	DRM_EXYNOS_IPP_LIMIT_TYPE_SCALE		= 0x0002,

	/* image buffer area */
	DRM_EXYNOS_IPP_LIMIT_SIZE_BUFFER	= 0x0001 << 16,
	/* src/dst rectangle area */
	DRM_EXYNOS_IPP_LIMIT_SIZE_AREA		= 0x0002 << 16,
	/* src/dst rectangle area when rotation enabled */
	DRM_EXYNOS_IPP_LIMIT_SIZE_ROTATED	= 0x0003 << 16,

	DRM_EXYNOS_IPP_LIMIT_TYPE_MASK		= 0x000f,
	DRM_EXYNOS_IPP_LIMIT_SIZE_MASK		= 0x000f << 16,
};

struct drm_exynos_ipp_limit_val {
	__u32 min;
	__u32 max;
	__u32 align;
	__u32 reserved;
};

/**
 * IPP module limitation.
 *
 * @type: limit type (see drm_exynos_ipp_limit_type enum)
 * @reserved: padding
 * @h: horizontal limits
 * @v: vertical limits
 */
struct drm_exynos_ipp_limit {
	__u32 type;
	__u32 reserved;
	struct drm_exynos_ipp_limit_val h;
	struct drm_exynos_ipp_limit_val v;
};

/**
 * Get IPP limits for given image format.
 *
 * @ipp_id: id of IPP module to query
 * @fourcc: image format code (see DRM_FORMAT_* in drm_fourcc.h)
 * @modifier: image format modifier (see DRM_FORMAT_MOD_* in drm_fourcc.h)
 * @type: source/destination identifier (drm_exynos_ipp_format_flag enum)
 * @limits_count: size of limits array (in entries) / number of filled entries
 *		 (set by driver)
 * @limits_ptr: pointer to limits array or NULL
 */
struct drm_exynos_ioctl_ipp_get_limits {
	__u32 ipp_id;
	__u32 fourcc;
	__u64 modifier;
	__u32 type;
	__u32 limits_count;
	__u64 limits_ptr;
};

enum drm_exynos_ipp_task_id {
	/* buffer described by struct drm_exynos_ipp_task_buffer */
	DRM_EXYNOS_IPP_TASK_BUFFER		= 0x0001,
	/* rectangle described by struct drm_exynos_ipp_task_rect */
	DRM_EXYNOS_IPP_TASK_RECTANGLE		= 0x0002,
	/* transformation described by struct drm_exynos_ipp_task_transform */
	DRM_EXYNOS_IPP_TASK_TRANSFORM		= 0x0003,
	/* alpha configuration described by struct drm_exynos_ipp_task_alpha */
	DRM_EXYNOS_IPP_TASK_ALPHA		= 0x0004,

	/* source image data (for buffer and rectangle chunks) */
	DRM_EXYNOS_IPP_TASK_TYPE_SOURCE		= 0x0001 << 16,
	/* destination image data (for buffer and rectangle chunks) */
	DRM_EXYNOS_IPP_TASK_TYPE_DESTINATION	= 0x0002 << 16,
};

/**
 * Memory buffer with image data.
 *
 * @id: must be DRM_EXYNOS_IPP_TASK_BUFFER
 * other parameters are same as for AddFB2 generic DRM ioctl
 */
struct drm_exynos_ipp_task_buffer {
	__u32	id;
	__u32	fourcc;
	__u32	width, height;
	__u32	gem_id[4];
	__u32	offset[4];
	__u32	pitch[4];
	__u64	modifier;
};

/**
 * Rectangle for processing.
 *
 * @id: must be DRM_EXYNOS_IPP_TASK_RECTANGLE
 * @reserved: padding
 * @x,@y: left corner in pixels
 * @w,@h: width/height in pixels
 */
struct drm_exynos_ipp_task_rect {
	__u32	id;
	__u32	reserved;
	__u32	x;
	__u32	y;
	__u32	w;
	__u32	h;
};

/**
 * Image tranformation description.
 *
 * @id: must be DRM_EXYNOS_IPP_TASK_TRANSFORM
 * @rotation: DRM_MODE_ROTATE_* and DRM_MODE_REFLECT_* values
 */
struct drm_exynos_ipp_task_transform {
	__u32	id;
	__u32	rotation;
};

/**
 * Image global alpha configuration for formats without alpha values.
 *
 * @id: must be DRM_EXYNOS_IPP_TASK_ALPHA
 * @value: global alpha value (0-255)
 */
struct drm_exynos_ipp_task_alpha {
	__u32	id;
	__u32	value;
};

enum drm_exynos_ipp_flag {
	/* generate DRM event after processing */
	DRM_EXYNOS_IPP_FLAG_EVENT	= 0x01,
	/* dry run, only check task parameters */
	DRM_EXYNOS_IPP_FLAG_TEST_ONLY	= 0x02,
	/* non-blocking processing */
	DRM_EXYNOS_IPP_FLAG_NONBLOCK	= 0x04,
};

#define DRM_EXYNOS_IPP_FLAGS (DRM_EXYNOS_IPP_FLAG_EVENT |\
		DRM_EXYNOS_IPP_FLAG_TEST_ONLY | DRM_EXYNOS_IPP_FLAG_NONBLOCK)

/**
 * Perform image processing described by array of drm_exynos_ipp_task_*
 * structures (parameters array).
 *
 * @ipp_id: id of IPP module to run the task
 * @flags: bitmask of drm_exynos_ipp_flag values
 * @reserved: padding
 * @params_size: size of parameters array (in bytes)
 * @params_ptr: pointer to parameters array or NULL
 * @user_data: (optional) data for drm event
 */
struct drm_exynos_ioctl_ipp_commit {
	__u32 ipp_id;
	__u32 flags;
	__u32 reserved;
	__u32 params_size;
	__u64 params_ptr;
	__u64 user_data;
};

#define DRM_EXYNOS_GEM_CREATE		0x00
#define DRM_EXYNOS_GEM_MAP		0x01
/* Reserved 0x03 ~ 0x05 for exynos specific gem ioctl */
#define DRM_EXYNOS_GEM_GET		0x04
#define DRM_EXYNOS_VIDI_CONNECTION	0x07

/* G2D */
#define DRM_EXYNOS_G2D_GET_VER		0x20
#define DRM_EXYNOS_G2D_SET_CMDLIST	0x21
#define DRM_EXYNOS_G2D_EXEC		0x22

/* Reserved 0x30 ~ 0x33 for obsolete Exynos IPP ioctls */
/* IPP - Image Post Processing */
#define DRM_EXYNOS_IPP_GET_RESOURCES	0x40
#define DRM_EXYNOS_IPP_GET_CAPS		0x41
#define DRM_EXYNOS_IPP_GET_LIMITS	0x42
#define DRM_EXYNOS_IPP_COMMIT		0x43

#define DRM_IOCTL_EXYNOS_GEM_CREATE		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_GEM_CREATE, struct drm_exynos_gem_create)
#define DRM_IOCTL_EXYNOS_GEM_MAP		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_GEM_MAP, struct drm_exynos_gem_map)
#define DRM_IOCTL_EXYNOS_GEM_GET	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_GEM_GET,	struct drm_exynos_gem_info)

#define DRM_IOCTL_EXYNOS_VIDI_CONNECTION	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_VIDI_CONNECTION, struct drm_exynos_vidi_connection)

#define DRM_IOCTL_EXYNOS_G2D_GET_VER		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_GET_VER, struct drm_exynos_g2d_get_ver)
#define DRM_IOCTL_EXYNOS_G2D_SET_CMDLIST	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_SET_CMDLIST, struct drm_exynos_g2d_set_cmdlist)
#define DRM_IOCTL_EXYNOS_G2D_EXEC		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_G2D_EXEC, struct drm_exynos_g2d_exec)

#define DRM_IOCTL_EXYNOS_IPP_GET_RESOURCES	DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_IPP_GET_RESOURCES, \
		struct drm_exynos_ioctl_ipp_get_res)
#define DRM_IOCTL_EXYNOS_IPP_GET_CAPS		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_IPP_GET_CAPS, struct drm_exynos_ioctl_ipp_get_caps)
#define DRM_IOCTL_EXYNOS_IPP_GET_LIMITS		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_IPP_GET_LIMITS, \
		struct drm_exynos_ioctl_ipp_get_limits)
#define DRM_IOCTL_EXYNOS_IPP_COMMIT		DRM_IOWR(DRM_COMMAND_BASE + \
		DRM_EXYNOS_IPP_COMMIT, struct drm_exynos_ioctl_ipp_commit)

/* Exynos specific events */
#define DRM_EXYNOS_G2D_EVENT		0x80000000
#define DRM_EXYNOS_IPP_EVENT		0x80000002

struct drm_exynos_g2d_event {
	struct drm_event	base;
	__u64			user_data;
	__u32			tv_sec;
	__u32			tv_usec;
	__u32			cmdlist_no;
	__u32			reserved;
};

struct drm_exynos_ipp_event {
	struct drm_event	base;
	__u64			user_data;
	__u32			tv_sec;
	__u32			tv_usec;
	__u32			ipp_id;
	__u32			sequence;
	__u64			reserved;
};

#if defined(__cplusplus)
}
#endif

#endif /* _UAPI_EXYNOS_DRM_H_ */
