/*
 * Virtio GPU Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This header is BSD licensed so anyone can use the definitions
 * to implement compatible drivers/servers:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef VIRTIO_GPU_HW_H
#define VIRTIO_GPU_HW_H

#include <sys/types.h>

#define __u8 uint8_t
#define __u32 uint32_t
#define __le16 uint16_t
#define __le32 uint32_t
#define __le64 uint64_t

/*
 * VIRTIO_GPU_CMD_CTX_*
 * VIRTIO_GPU_CMD_*_3D
 */
#define VIRTIO_GPU_F_VIRGL               (1ULL << 0)

/*
 * VIRTIO_GPU_CMD_GET_EDID
 */
#define VIRTIO_GPU_F_EDID                (1ULL << 1)
/*
 * VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID
 */
#define VIRTIO_GPU_F_RESOURCE_UUID       (1ULL << 2)

/*
 * VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB
 */
#define VIRTIO_GPU_F_RESOURCE_BLOB       (1ULL << 3)

enum virtio_gpu_ctrl_type {
	VIRTIO_GPU_UNDEFINED = 0,

	/* 2d commands */
	VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_2D,
	VIRTIO_GPU_CMD_RESOURCE_UNREF,
	VIRTIO_GPU_CMD_SET_SCANOUT,
	VIRTIO_GPU_CMD_RESOURCE_FLUSH,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D,
	VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,
	VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING,
	VIRTIO_GPU_CMD_GET_CAPSET_INFO,
	VIRTIO_GPU_CMD_GET_CAPSET,
	VIRTIO_GPU_CMD_GET_EDID,
	VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB,
	VIRTIO_GPU_CMD_SET_SCANOUT_BLOB,

	/* 3d commands */
	VIRTIO_GPU_CMD_CTX_CREATE = 0x0200,
	VIRTIO_GPU_CMD_CTX_DESTROY,
	VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE,
	VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE,
	VIRTIO_GPU_CMD_RESOURCE_CREATE_3D,
	VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D,
	VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D,
	VIRTIO_GPU_CMD_SUBMIT_3D,
	VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB,
	VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB,

	/* cursor commands */
	VIRTIO_GPU_CMD_UPDATE_CURSOR = 0x0300,
	VIRTIO_GPU_CMD_MOVE_CURSOR,

	/* success responses */
	VIRTIO_GPU_RESP_OK_NODATA = 0x1100,
	VIRTIO_GPU_RESP_OK_DISPLAY_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET_INFO,
	VIRTIO_GPU_RESP_OK_CAPSET,
	VIRTIO_GPU_RESP_OK_EDID,
	VIRTIO_GPU_RESP_OK_RESOURCE_UUID,
	VIRTIO_GPU_RESP_OK_MAP_INFO,

	/* error responses */
	VIRTIO_GPU_RESP_ERR_UNSPEC = 0x1200,
	VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY,
	VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID,
	VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
};

enum virtio_gpu_shm_id {
	VIRTIO_GPU_SHM_ID_UNDEFINED = 0,
	/*
	 * VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB
	 * VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB
	 */
	VIRTIO_GPU_SHM_ID_HOST_VISIBLE = 1
};

#define VIRTIO_GPU_FLAG_FENCE (1 << 0)

struct virtio_gpu_ctrl_hdr {
	__le32 type;
	__le32 flags;
	__le64 fence_id;
	__le32 ctx_id;
	__le32 padding;
} __packed;

/* data passed in the cursor vq */

struct virtio_gpu_cursor_pos {
	__le32 scanout_id;
	__le32 x;
	__le32 y;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_CMD_UPDATE_CURSOR, VIRTIO_GPU_CMD_MOVE_CURSOR */
struct virtio_gpu_update_cursor {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_cursor_pos pos;  /* update & move */
	__le32 resource_id;           /* update only */
	__le32 hot_x;                 /* update only */
	__le32 hot_y;                 /* update only */
	__le32 padding;
} __packed;

/* data passed in the control vq, 2d related */

struct virtio_gpu_rect {
	__le32 x;
	__le32 y;
	__le32 width;
	__le32 height;
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_UNREF */
struct virtio_gpu_resource_unref {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: create a 2d resource with a format */
struct virtio_gpu_resource_create_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 format;
	__le32 width;
	__le32 height;
} __packed;

/* VIRTIO_GPU_CMD_SET_SCANOUT */
struct virtio_gpu_set_scanout {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	__le32 scanout_id;
	__le32 resource_id;
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_FLUSH */
struct virtio_gpu_resource_flush {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	__le32 resource_id;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: simple transfer to_host */
struct virtio_gpu_transfer_to_host_2d {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	__le64 offset;
	__le32 resource_id;
	__le32 padding;
} __packed;

struct virtio_gpu_mem_entry {
	__le64 addr;
	__le32 length;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING */
struct virtio_gpu_resource_attach_backing {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 nr_entries;
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING */
struct virtio_gpu_resource_detach_backing {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_RESP_OK_DISPLAY_INFO */
#define VIRTIO_GPU_MAX_SCANOUTS 16
struct virtio_gpu_resp_display_info {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_display_one {
		struct virtio_gpu_rect r;
		__le32 enabled;
		__le32 flags;
	} pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __packed;

/* data passed in the control vq, 3d related */

struct virtio_gpu_box {
	__le32 x, y, z;
	__le32 w, h, d;
} __packed;

/* VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D, VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D */
struct virtio_gpu_transfer_host_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_box box;
	__le64 offset;
	__le32 resource_id;
	__le32 level;
	__le32 stride;
	__le32 layer_stride;
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_3D */
#define VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP (1 << 0)
struct virtio_gpu_resource_create_3d {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 target;
	__le32 format;
	__le32 bind;
	__le32 width;
	__le32 height;
	__le32 depth;
	__le32 array_size;
	__le32 last_level;
	__le32 nr_samples;
	__le32 flags;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_CMD_CTX_CREATE */
struct virtio_gpu_ctx_create {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 nlen;
	__le32 padding;
	char debug_name[64];
} __packed;

/* VIRTIO_GPU_CMD_CTX_DESTROY */
struct virtio_gpu_ctx_destroy {
	struct virtio_gpu_ctrl_hdr hdr;
} __packed;

/* VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE, VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE */
struct virtio_gpu_ctx_resource {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_CMD_SUBMIT_3D */
struct virtio_gpu_cmd_submit {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 size;
	__le32 padding;
} __packed;

#define VIRTIO_GPU_CAPSET_VIRGL 1
#define VIRTIO_GPU_CAPSET_VIRGL2 2

/* VIRTIO_GPU_CMD_GET_CAPSET_INFO */
struct virtio_gpu_get_capset_info {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 capset_index;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_RESP_OK_CAPSET_INFO */
struct virtio_gpu_resp_capset_info {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 capset_id;
	__le32 capset_max_version;
	__le32 capset_max_size;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_CMD_GET_CAPSET */
struct virtio_gpu_get_capset {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 capset_id;
	__le32 capset_version;
} __packed;

/* VIRTIO_GPU_RESP_OK_CAPSET */
struct virtio_gpu_resp_capset {
	struct virtio_gpu_ctrl_hdr hdr;
	__u8 capset_data[];
} __packed;

/* VIRTIO_GPU_CMD_GET_EDID */
struct virtio_gpu_cmd_get_edid {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 scanout;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_RESP_OK_EDID */
struct virtio_gpu_resp_edid {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 size;
	__le32 padding;
	__u8 edid[1024];
} __packed;

#define VIRTIO_GPU_EVENT_DISPLAY (1 << 0)

struct virtio_gpu_config {
	__le32 events_read;
	__le32 events_clear;
	__le32 num_scanouts;
	__le32 num_capsets;
} __packed;

/* simple formats for fbcon/X use */
enum virtio_gpu_formats {
	VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  = 1,
	VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM  = 2,
	VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM  = 3,
	VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM  = 4,

	VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM  = 67,
	VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM  = 68,

	VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM  = 121,
	VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM  = 134,
};

/* VIRTIO_GPU_CMD_RESOURCE_ASSIGN_UUID */
struct virtio_gpu_resource_assign_uuid {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 padding;
} __packed;

/* VIRTIO_GPU_RESP_OK_RESOURCE_UUID */
struct virtio_gpu_resp_resource_uuid {
	struct virtio_gpu_ctrl_hdr hdr;
	__u8 uuid[16];
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB */
struct virtio_gpu_resource_create_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
#define VIRTIO_GPU_BLOB_MEM_GUEST             0x0001
#define VIRTIO_GPU_BLOB_MEM_HOST3D            0x0002
#define VIRTIO_GPU_BLOB_MEM_HOST3D_GUEST      0x0003

#define VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE     0x0001
#define VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE    0x0002
#define VIRTIO_GPU_BLOB_FLAG_USE_CROSS_DEVICE 0x0004
	/* zero is invalid blob mem */
	__le32 blob_mem;
	__le32 blob_flags;
	__le32 nr_entries;
	__le64 blob_id;
	__le64 size;
	/*
	 * sizeof(nr_entries * virtio_gpu_mem_entry) bytes follow
	 */
} __packed;

/* VIRTIO_GPU_CMD_SET_SCANOUT_BLOB */
struct virtio_gpu_set_scanout_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_rect r;
	__le32 scanout_id;
	__le32 resource_id;
	__le32 width;
	__le32 height;
	__le32 format;
	__le32 padding;
	__le32 strides[4];
	__le32 offsets[4];
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB */
struct virtio_gpu_resource_map_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 padding;
	__le64 offset;
} __packed;

/* VIRTIO_GPU_RESP_OK_MAP_INFO */
#define VIRTIO_GPU_MAP_CACHE_MASK     0x0f
#define VIRTIO_GPU_MAP_CACHE_NONE     0x00
#define VIRTIO_GPU_MAP_CACHE_CACHED   0x01
#define VIRTIO_GPU_MAP_CACHE_UNCACHED 0x02
#define VIRTIO_GPU_MAP_CACHE_WC       0x03
struct virtio_gpu_resp_map_info {
	struct virtio_gpu_ctrl_hdr hdr;
	__u32 map_info;
	__u32 padding;
} __packed;

/* VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB */
struct virtio_gpu_resource_unmap_blob {
	struct virtio_gpu_ctrl_hdr hdr;
	__le32 resource_id;
	__le32 padding;
} __packed;

#endif
