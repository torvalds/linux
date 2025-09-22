// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io> */

/*
 * This file is intended to be included multiple times with IOMFB_VER
 * defined to declare DCP firmware version dependent structs.
 */

#ifdef DCP_FW_VER

#include <drm/drm_crtc.h>

#include <linux/types.h>

#include "iomfb.h"
#include "version_utils.h"

struct DCP_FW_NAME(dcp_swap) {
	u64 ts1;
	u64 ts2;
	u64 unk_10[6];
	u64 flags1;
	u64 flags2;

	u32 swap_id;

	u32 surf_ids[SWAP_SURFACES];
	struct dcp_rect src_rect[SWAP_SURFACES];
	u32 surf_flags[SWAP_SURFACES];
	u32 surf_unk[SWAP_SURFACES];
	struct dcp_rect dst_rect[SWAP_SURFACES];
	u32 swap_enabled;
	u32 swap_completed;

	u32 bg_color;
	u8 unk_110[0x1b8];
	u32 unk_2c8;
	u8 unk_2cc[0x14];
	u32 unk_2e0;
#if DCP_FW_VER < DCP_FW_VERSION(13, 2, 0)
	u16 unk_2e2;
#else
	u8 unk_2e2[3];
#endif
	u64 bl_unk;
	u32 bl_value; // min value is 0x10000000
	u8  bl_power; // constant 0x40 for on
	u8 unk_2f3[0x2d];
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u8 unk_320[0x13f];
	u64 unk_1;
#endif
} __packed;

/* Information describing a surface */
struct DCP_FW_NAME(dcp_surface) {
	u8 is_tiled;
	u8 is_tearing_allowed;
	u8 is_premultiplied;
	u32 plane_cnt;
	u32 plane_cnt2;
	u32 format; /* DCP fourcc */
	u32 ycbcr_matrix;
	u8 xfer_func;
	u8 colorspace;
	u32 stride;
	u16 pix_size;
	u8 pel_w;
	u8 pel_h;
	u32 offset;
	u32 width;
	u32 height;
	u32 buf_size;
	u64 protection_opts;
	u32 surface_id;
	struct dcp_component_types comp_types[MAX_PLANES];
	u64 has_comp;
	struct dcp_plane_info planes[MAX_PLANES];
	u64 has_planes;
	u32 compression_info[MAX_PLANES][13];
	u64 has_compr_info;
	u32 unk_num;
	u32 unk_denom;
#if DCP_FW_VER < DCP_FW_VERSION(13, 2, 0)
	u8 padding[7];
#else
	u8 padding[47];
#endif
} __packed;

/* Prototypes */

struct DCP_FW_NAME(dcp_swap_submit_req) {
	struct DCP_FW_NAME(dcp_swap) swap;
	struct DCP_FW_NAME(dcp_surface) surf[SWAP_SURFACES];
	u64 surf_iova[SWAP_SURFACES];
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u64 unk_u64_a[SWAP_SURFACES];
	struct DCP_FW_NAME(dcp_surface) surf2[5];
	u64 surf2_iova[5];
#endif
	u8 unkbool;
	u64 unkdouble;
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u64 unkU64;
	u8 unkbool2;
#endif
	u32 clear; // or maybe switch to default fb?
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u32 unkU32Ptr;
#endif
	u8 swap_null;
	u8 surf_null[SWAP_SURFACES];
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u8 surf2_null[5];
#endif
	u8 unkoutbool_null;
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u8 unkU32Ptr_null;
	u8 unkU32out_null;
#endif
	u8 padding[1];
} __packed;

struct DCP_FW_NAME(dcp_swap_submit_resp) {
	u8 unkoutbool;
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u32 unkU32out;
#endif
	u32 ret;
	u8 padding[3];
} __packed;

struct DCP_FW_NAME(dc_swap_complete_resp) {
	u32 swap_id;
	u8 unkbool;
	u64 swap_data;
#if DCP_FW_VER < DCP_FW_VERSION(13, 2, 0)
	u8 swap_info[0x6c4];
#else
	u8 swap_info[0x6c5];
#endif
	u32 unkint;
	u8 swap_info_null;
} __packed;

struct DCP_FW_NAME(dcp_map_reg_req) {
	char obj[4];
	u32 index;
	u32 flags;
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u8 unk_u64_null;
#endif
	u8 addr_null;
	u8 length_null;
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u8 padding[1];
#else
	u8 padding[2];
#endif
} __packed;

struct DCP_FW_NAME(dcp_map_reg_resp) {
#if DCP_FW_VER >= DCP_FW_VERSION(13, 2, 0)
	u64 dva;
#endif
	u64 addr;
	u64 length;
	u32 ret;
} __packed;


struct apple_dcp;

int DCP_FW_NAME(iomfb_modeset)(struct apple_dcp *dcp,
			       struct drm_crtc_state *crtc_state);
void DCP_FW_NAME(iomfb_flush)(struct apple_dcp *dcp, struct drm_crtc *crtc, struct drm_atomic_state *state);
void DCP_FW_NAME(iomfb_poweron)(struct apple_dcp *dcp);
void DCP_FW_NAME(iomfb_poweroff)(struct apple_dcp *dcp);
void DCP_FW_NAME(iomfb_sleep)(struct apple_dcp *dcp);
void DCP_FW_NAME(iomfb_start)(struct apple_dcp *dcp);
void DCP_FW_NAME(iomfb_shutdown)(struct apple_dcp *dcp);

#endif
