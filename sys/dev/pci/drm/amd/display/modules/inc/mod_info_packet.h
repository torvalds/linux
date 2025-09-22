/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef MOD_INFO_PACKET_H_
#define MOD_INFO_PACKET_H_

#include "dm_services.h"
#include "mod_shared.h"
//Forward Declarations
struct dc_stream_state;
struct dc_info_packet;
struct mod_vrr_params;

void mod_build_vsc_infopacket(const struct dc_stream_state *stream,
		struct dc_info_packet *info_packet,
		enum dc_color_space cs,
		enum color_transfer_func tf);

void mod_build_hf_vsif_infopacket(const struct dc_stream_state *stream,
		struct dc_info_packet *info_packet);

enum adaptive_sync_type {
	ADAPTIVE_SYNC_TYPE_NONE                  = 0,
	ADAPTIVE_SYNC_TYPE_DP                    = 1,
	FREESYNC_TYPE_PCON_IN_WHITELIST          = 2,
	FREESYNC_TYPE_PCON_NOT_IN_WHITELIST      = 3,
	ADAPTIVE_SYNC_TYPE_EDP                   = 4,
};

enum adaptive_sync_sdp_version {
	AS_SDP_VER_0 = 0x0,
	AS_SDP_VER_1 = 0x1,
	AS_SDP_VER_2 = 0x2,
};

#define AS_DP_SDP_LENGTH (9)

struct frame_duration_op {
	bool          support;
	unsigned char frame_duration_hex;
};

struct AS_Df_params {
	bool   supportMode;
	struct frame_duration_op increase;
	struct frame_duration_op decrease;
};

void mod_build_adaptive_sync_infopacket(const struct dc_stream_state *stream,
		enum adaptive_sync_type asType, const struct AS_Df_params *param,
		struct dc_info_packet *info_packet);

void mod_build_adaptive_sync_infopacket_v2(const struct dc_stream_state *stream,
		const struct AS_Df_params *param, struct dc_info_packet *info_packet);

void mod_build_adaptive_sync_infopacket_v1(struct dc_info_packet *info_packet);

#endif
