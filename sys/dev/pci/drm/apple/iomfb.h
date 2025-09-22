// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io> */

#ifndef __APPLE_DCPEP_H__
#define __APPLE_DCPEP_H__

#include <linux/types.h>

#include "version_utils.h"

/* Fixed size of shared memory between DCP and AP */
#define DCP_SHMEM_SIZE 0x100000

/* DCP message contexts */
enum dcp_context_id {
	/* Callback */
	DCP_CONTEXT_CB = 0,

	/* Command */
	DCP_CONTEXT_CMD = 2,

	/* Asynchronous */
	DCP_CONTEXT_ASYNC = 3,

	/* Out-of-band callback */
	DCP_CONTEXT_OOBCB = 4,

	/* Out-of-band command */
	DCP_CONTEXT_OOBCMD = 6,

	/* Out-of-band Asynchronous */
	DCP_CONTEXT_OOBASYNC = 7,

	DCP_NUM_CONTEXTS
};

/* RTKit endpoint message types */
enum dcpep_type {
	/* Set shared memory */
	IOMFB_MESSAGE_TYPE_SET_SHMEM = 0,

	/* DCP is initialized */
	IOMFB_MESSAGE_TYPE_INITIALIZED = 1,

	/* Remote procedure call */
	IOMFB_MESSAGE_TYPE_MSG = 2,
};

#define IOMFB_MESSAGE_TYPE	GENMASK_ULL( 3,  0)

/* Message */
#define IOMFB_MSG_LENGTH	GENMASK_ULL(63, 32)
#define IOMFB_MSG_OFFSET	GENMASK_ULL(31, 16)
#define IOMFB_MSG_CONTEXT	GENMASK_ULL(11,  8)
#define IOMFB_MSG_ACK		BIT_ULL(6)

/* Set shmem */
#define IOMFB_SHMEM_DVA		GENMASK_ULL(63, 16)
#define IOMFB_SHMEM_FLAG	GENMASK_ULL( 7,  4)
#define IOMFB_SHMEM_FLAG_VALUE	4

struct dcp_packet_header {
	char tag[4];
	u32 in_len;
	u32 out_len;
} __packed;

#define DCP_IS_NULL(ptr) ((ptr) ? 1 : 0)
#define DCP_PACKET_ALIGNMENT (0x40)

enum iomfb_property_id {
    IOMFB_PROPERTY_NITS = 15, // divide by Brightness_Scale
};

#define IOMFB_BRIGHTNESS_MIN 0x10000000

/* Structures used in v12.0 firmware */

#define SWAP_SURFACES 4
#define MAX_PLANES 3

enum dcp_colorspace {
	DCP_COLORSPACE_BG_SRGB = 0,
	DCP_COLORSPACE_BG_BT2020 = 9,
	DCP_COLORSPACE_NATIVE = 12,
};

enum dcp_xfer_func {
	DCP_XFER_FUNC_SDR = 13,
	DCP_XFER_FUNC_HDR = 16,
};

struct dcp_iouserclient {
	/* Handle for the IOUserClient. macOS sets this to a kernel VA. */
	u64 handle;
	u32 unk;
	u8 flag1;
	u8 flag2;
	u8 padding[2];
} __packed;

struct dcp_rect {
	u32 x;
	u32 y;
	u32 w;
	u32 h;
} __packed;

/*
 * Update background color to struct dcp_swap.bg_color
 */
#define IOMFB_SET_BACKGROUND	BIT(31)

/* Information describing a plane of a planar compressed surface */
struct dcp_plane_info {
	u32 width;
	u32 height;
	u32 base;
	u32 offset;
	u32 stride;
	u32 size;
	u16 tile_size;
	u8 tile_w;
	u8 tile_h;
	u32 unk[13];
} __packed;

struct dcp_component_types {
	u8 count;
	u8 types[7];
} __packed;

struct dcp_allocate_bandwidth_req {
	u64 unk1;
	u64 unk2;
	u64 unk3;
	u8 unk1_null;
	u8 unk2_null;
	u8 padding[8];
} __packed;

struct dcp_allocate_bandwidth_resp {
	u64 unk1;
	u64 unk2;
	u32 ret;
} __packed;

struct dcp_rt_bandwidth {
	u64 unk1;
	u64 reg_scratch;
	u64 reg_doorbell;
	u32 unk2;
	u32 doorbell_bit;
	u32 padding[7];
} __packed;

struct frame_sync_props {
	u8 unk[28];
};

struct dcp_set_frame_sync_props_req {
	struct frame_sync_props props;
	u8 frame_sync_props_null;
	u8 padding[3];
} __packed;

struct dcp_set_frame_sync_props_resp {
	struct frame_sync_props props;
} __packed;

/* Method calls */

enum dcpep_method {
	dcpep_late_init_signal,
	dcpep_setup_video_limits,
	dcpep_set_create_dfb,
	dcpep_start_signal,
	dcpep_swap_start,
	dcpep_swap_submit,
	dcpep_set_display_device,
	dcpep_set_digital_out_mode,
	dcpep_create_default_fb,
	dcpep_set_display_refresh_properties,
	dcpep_flush_supports_power,
	dcpep_set_power_state,
	dcpep_first_client_open,
	dcpep_set_parameter_dcp,
	dcpep_enable_disable_video_power_savings,
	dcpep_is_main_display,
	iomfbep_a131_pmu_service_matched,
	iomfbep_a132_backlight_service_matched,
	iomfbep_a358_vi_set_temperature_hint,
	iomfbep_get_color_remap_mode,
	iomfbep_last_client_close,
	iomfbep_abort_swaps_dcp,
	iomfbep_set_matrix,
	dcpep_num_methods
};

#define IOMFB_METHOD(tag, name) [name] = { #name, tag }

struct dcp_method_entry {
	const char *name;
	char tag[4];
};

#define IOMFB_MAX_CB (1000)
struct apple_dcp;

typedef bool (*iomfb_cb_handler)(struct apple_dcp *, int, void *, void *);

/* Prototypes */

struct dcp_set_digital_out_mode_req {
	u32 color_mode_id;
	u32 timing_mode_id;
} __packed;

struct dcp_map_buf_req {
	u64 buffer;
	u8 unk;
	u8 buf_null;
	u8 vaddr_null;
	u8 dva_null;
} __packed;

struct dcp_map_buf_resp {
	u64 vaddr;
	u64 dva;
	u32 ret;
} __packed;

struct dcp_unmap_buf_resp {
	u64 buffer;
	u64 vaddr;
	u64 dva;
	u8 unk;
	u8 buf_null;
} __packed;

struct dcp_allocate_buffer_req {
	u32 unk0;
	u64 size;
	u32 unk2;
	u8 paddr_null;
	u8 dva_null;
	u8 dva_size_null;
	u8 padding;
} __packed;

struct dcp_allocate_buffer_resp {
	u64 paddr;
	u64 dva;
	u64 dva_size;
	u32 mem_desc_id;
} __packed;

struct dcp_map_physical_req {
	u64 paddr;
	u64 size;
	u32 flags;
	u8 dva_null;
	u8 dva_size_null;
	u8 padding[2];
} __packed;

struct dcp_map_physical_resp {
	u64 dva;
	u64 dva_size;
	u32 mem_desc_id;
} __packed;

struct dcp_swap_start_req {
	u32 swap_id;
	struct dcp_iouserclient client;
	u8 swap_id_null;
	u8 client_null;
	u8 padding[2];
} __packed;

struct dcp_swap_start_resp {
	u32 swap_id;
	struct dcp_iouserclient client;
	u32 ret;
} __packed;

struct dcp_get_uint_prop_req {
	char obj[4];
	char key[0x40];
	u64 value;
	u8 value_null;
	u8 padding[3];
} __packed;

struct dcp_get_uint_prop_resp {
	u64 value;
	u8 ret;
	u8 padding[3];
} __packed;

struct iomfb_sr_set_property_int_req {
	char obj[4];
	char key[0x40];
	u64 value;
	u8 value_null;
	u8 padding[3];
} __packed;

struct iomfb_set_fx_prop_req {
	char obj[4];
	char key[0x40];
	u32 value;
} __packed;

struct dcp_set_power_state_req {
	u64 unklong;
	u8 unkbool;
	u8 unkint_null;
	u8 padding[2];
} __packed;

struct dcp_set_power_state_resp {
	u32 unkint;
	u32 ret;
} __packed;

struct dcp_set_dcpav_prop_chunk_req {
	char data[0x1000];
	u32 offset;
	u32 length;
} __packed;

struct dcp_set_dcpav_prop_end_req {
	char key[0x40];
} __packed;

struct dcp_set_parameter_dcp {
	u32 param;
	u32 value[8];
	u32 count;
} __packed;

struct dcp_swap_complete_intent_gated {
	u32 swap_id;
	u8 unkBool;
	u32 unkInt;
	u32 width;
	u32 height;
} __packed;

struct dcp_read_edt_data_req {
	char key[0x40];
	u32 count;
	u32 value[8];
} __packed;

struct dcp_read_edt_data_resp {
	u32 value[8];
	u8 ret;
} __packed;

struct iomfb_property {
	u32 id;
	u32 value;
} __packed;

struct iomfb_get_color_remap_mode_req {
	u32 mode;
	u8 mode_null;
	u8 padding[3];
} __packed;

struct iomfb_get_color_remap_mode_resp {
	u32 mode;
	u32 ret;
} __packed;

struct iomfb_last_client_close_req {
	u8 unkint_null;
	u8 padding[3];
} __packed;

struct iomfb_last_client_close_resp {
	u32 unkint;
} __packed;

struct io_user_client {
	u64 addr;
	u32 unk;
	u8 flag1;
	u8 flag2;
	u8 pad[2];
} __packed;

struct iomfb_abort_swaps_dcp_req {
	struct io_user_client client;
	u8 client_null;
	u8 pad[3];
} __packed;

struct iomfb_abort_swaps_dcp_resp {
	struct io_user_client client;
	u32 ret;
} __packed;

struct iomfb_set_matrix_req {
	u32 unk_u32; // maybe length?
	u64 r[3];
	u64 g[3];
	u64 b[3];
	u8 matrix_null;
	u8 padding[3];
} __packed;

struct iomfb_set_matrix_resp {
	u32 ret;
} __packed;

struct dcpep_get_tiling_state_req {
	u32 event;
	u32 param;
	u32 value;
	u8 value_null;
	u8 padding[3];
} __packed;

struct dcpep_get_tiling_state_resp {
	u32 value;
	u32 ret;
} __packed;

#endif
