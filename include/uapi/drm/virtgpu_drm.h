/*
 * Copyright 2013 Red Hat
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef VIRTGPU_DRM_H
#define VIRTGPU_DRM_H

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 *
 * Do not use pointers, use __u64 instead for 32 bit / 64 bit user/kernel
 * compatibility Keep fields aligned to their size
 */

#define DRM_VIRTGPU_MAP         0x01
#define DRM_VIRTGPU_EXECBUFFER  0x02
#define DRM_VIRTGPU_GETPARAM    0x03
#define DRM_VIRTGPU_RESOURCE_CREATE 0x04
#define DRM_VIRTGPU_RESOURCE_INFO     0x05
#define DRM_VIRTGPU_TRANSFER_FROM_HOST 0x06
#define DRM_VIRTGPU_TRANSFER_TO_HOST 0x07
#define DRM_VIRTGPU_WAIT     0x08
#define DRM_VIRTGPU_GET_CAPS  0x09
#define DRM_VIRTGPU_RESOURCE_CREATE_BLOB 0x0a
#define DRM_VIRTGPU_CONTEXT_INIT 0x0b

#define VIRTGPU_EXECBUF_FENCE_FD_IN	0x01
#define VIRTGPU_EXECBUF_FENCE_FD_OUT	0x02
#define VIRTGPU_EXECBUF_RING_IDX	0x04
#define VIRTGPU_EXECBUF_FLAGS  (\
		VIRTGPU_EXECBUF_FENCE_FD_IN |\
		VIRTGPU_EXECBUF_FENCE_FD_OUT |\
		VIRTGPU_EXECBUF_RING_IDX |\
		0)

struct drm_virtgpu_map {
	__u64 offset; /* use for mmap system call */
	__u32 handle;
	__u32 pad;
};

#define VIRTGPU_EXECBUF_SYNCOBJ_RESET		0x01
#define VIRTGPU_EXECBUF_SYNCOBJ_FLAGS ( \
		VIRTGPU_EXECBUF_SYNCOBJ_RESET | \
		0)
struct drm_virtgpu_execbuffer_syncobj {
	__u32 handle;
	__u32 flags;
	__u64 point;
};

/* fence_fd is modified on success if VIRTGPU_EXECBUF_FENCE_FD_OUT flag is set. */
struct drm_virtgpu_execbuffer {
	__u32 flags;
	__u32 size;
	__u64 command; /* void* */
	__u64 bo_handles;
	__u32 num_bo_handles;
	__s32 fence_fd; /* in/out fence fd (see VIRTGPU_EXECBUF_FENCE_FD_IN/OUT) */
	__u32 ring_idx; /* command ring index (see VIRTGPU_EXECBUF_RING_IDX) */
	__u32 syncobj_stride; /* size of @drm_virtgpu_execbuffer_syncobj */
	__u32 num_in_syncobjs;
	__u32 num_out_syncobjs;
	__u64 in_syncobjs;
	__u64 out_syncobjs;
};

#define VIRTGPU_PARAM_3D_FEATURES 1 /* do we have 3D features in the hw */
#define VIRTGPU_PARAM_CAPSET_QUERY_FIX 2 /* do we have the capset fix */
#define VIRTGPU_PARAM_RESOURCE_BLOB 3 /* DRM_VIRTGPU_RESOURCE_CREATE_BLOB */
#define VIRTGPU_PARAM_HOST_VISIBLE 4 /* Host blob resources are mappable */
#define VIRTGPU_PARAM_CROSS_DEVICE 5 /* Cross virtio-device resource sharing  */
#define VIRTGPU_PARAM_CONTEXT_INIT 6 /* DRM_VIRTGPU_CONTEXT_INIT */
#define VIRTGPU_PARAM_SUPPORTED_CAPSET_IDs 7 /* Bitmask of supported capability set ids */

struct drm_virtgpu_getparam {
	__u64 param;
	__u64 value;
};

/* NO_BO flags? NO resource flag? */
/* resource flag for y_0_top */
struct drm_virtgpu_resource_create {
	__u32 target;
	__u32 format;
	__u32 bind;
	__u32 width;
	__u32 height;
	__u32 depth;
	__u32 array_size;
	__u32 last_level;
	__u32 nr_samples;
	__u32 flags;
	__u32 bo_handle; /* if this is set - recreate a new resource attached to this bo ? */
	__u32 res_handle;  /* returned by kernel */
	__u32 size;        /* validate transfer in the host */
	__u32 stride;      /* validate transfer in the host */
};

struct drm_virtgpu_resource_info {
	__u32 bo_handle;
	__u32 res_handle;
	__u32 size;
	__u32 blob_mem;
};

struct drm_virtgpu_3d_box {
	__u32 x;
	__u32 y;
	__u32 z;
	__u32 w;
	__u32 h;
	__u32 d;
};

struct drm_virtgpu_3d_transfer_to_host {
	__u32 bo_handle;
	struct drm_virtgpu_3d_box box;
	__u32 level;
	__u32 offset;
	__u32 stride;
	__u32 layer_stride;
};

struct drm_virtgpu_3d_transfer_from_host {
	__u32 bo_handle;
	struct drm_virtgpu_3d_box box;
	__u32 level;
	__u32 offset;
	__u32 stride;
	__u32 layer_stride;
};

#define VIRTGPU_WAIT_NOWAIT 1 /* like it */
struct drm_virtgpu_3d_wait {
	__u32 handle; /* 0 is an invalid handle */
	__u32 flags;
};

struct drm_virtgpu_get_caps {
	__u32 cap_set_id;
	__u32 cap_set_ver;
	__u64 addr;
	__u32 size;
	__u32 pad;
};

struct drm_virtgpu_resource_create_blob {
#define VIRTGPU_BLOB_MEM_GUEST             0x0001
#define VIRTGPU_BLOB_MEM_HOST3D            0x0002
#define VIRTGPU_BLOB_MEM_HOST3D_GUEST      0x0003

#define VIRTGPU_BLOB_FLAG_USE_MAPPABLE     0x0001
#define VIRTGPU_BLOB_FLAG_USE_SHAREABLE    0x0002
#define VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004
	/* zero is invalid blob_mem */
	__u32 blob_mem;
	__u32 blob_flags;
	__u32 bo_handle;
	__u32 res_handle;
	__u64 size;

	/*
	 * for 3D contexts with VIRTGPU_BLOB_MEM_HOST3D_GUEST and
	 * VIRTGPU_BLOB_MEM_HOST3D otherwise, must be zero.
	 */
	__u32 pad;
	__u32 cmd_size;
	__u64 cmd;
	__u64 blob_id;
};

#define VIRTGPU_CONTEXT_PARAM_CAPSET_ID       0x0001
#define VIRTGPU_CONTEXT_PARAM_NUM_RINGS       0x0002
#define VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK 0x0003
struct drm_virtgpu_context_set_param {
	__u64 param;
	__u64 value;
};

struct drm_virtgpu_context_init {
	__u32 num_params;
	__u32 pad;

	/* pointer to drm_virtgpu_context_set_param array */
	__u64 ctx_set_params;
};

/*
 * Event code that's given when VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK is in
 * effect.  The event size is sizeof(drm_event), since there is no additional
 * payload.
 */
#define VIRTGPU_EVENT_FENCE_SIGNALED 0x90000000

#define DRM_IOCTL_VIRTGPU_MAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_MAP, struct drm_virtgpu_map)

#define DRM_IOCTL_VIRTGPU_EXECBUFFER \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_EXECBUFFER,\
		struct drm_virtgpu_execbuffer)

#define DRM_IOCTL_VIRTGPU_GETPARAM \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_GETPARAM,\
		struct drm_virtgpu_getparam)

#define DRM_IOCTL_VIRTGPU_RESOURCE_CREATE			\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_CREATE,	\
		struct drm_virtgpu_resource_create)

#define DRM_IOCTL_VIRTGPU_RESOURCE_INFO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_INFO, \
		 struct drm_virtgpu_resource_info)

#define DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_TRANSFER_FROM_HOST,	\
		struct drm_virtgpu_3d_transfer_from_host)

#define DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_TRANSFER_TO_HOST,	\
		struct drm_virtgpu_3d_transfer_to_host)

#define DRM_IOCTL_VIRTGPU_WAIT				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_WAIT,	\
		struct drm_virtgpu_3d_wait)

#define DRM_IOCTL_VIRTGPU_GET_CAPS \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_GET_CAPS, \
	struct drm_virtgpu_get_caps)

#define DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_CREATE_BLOB,	\
		struct drm_virtgpu_resource_create_blob)

#define DRM_IOCTL_VIRTGPU_CONTEXT_INIT					\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_CONTEXT_INIT,		\
		struct drm_virtgpu_context_init)

#if defined(__cplusplus)
}
#endif

#endif
