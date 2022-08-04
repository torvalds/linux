/* SPDX-License-Identifier: GPL-2.0 */
/*
 * These are the VP8 state controls for use with stateless VP8
 * codec drivers.
 *
 * It turns out that these structs are not stable yet and will undergo
 * more changes. So keep them private until they are stable and ready to
 * become part of the official public API.
 */

#ifndef _VP8_CTRLS_H_
#define _VP8_CTRLS_H_

#include <linux/types.h>

#define V4L2_PIX_FMT_VP8_FRAME v4l2_fourcc('V', 'P', '8', 'F')

#define V4L2_CID_MPEG_VIDEO_VP8_FRAME_HEADER (V4L2_CID_CODEC_BASE + 2000)
#define V4L2_CTRL_TYPE_VP8_FRAME_HEADER 0x301

#define V4L2_VP8_SEGMENT_HEADER_FLAG_ENABLED              0x01
#define V4L2_VP8_SEGMENT_HEADER_FLAG_UPDATE_MAP           0x02
#define V4L2_VP8_SEGMENT_HEADER_FLAG_UPDATE_FEATURE_DATA  0x04
#define V4L2_VP8_SEGMENT_HEADER_FLAG_DELTA_VALUE_MODE     0x08

struct v4l2_vp8_segment_header {
	__s8 quant_update[4];
	__s8 lf_update[4];
	__u8 segment_probs[3];
	__u8 padding;
	__u32 flags;
};

#define V4L2_VP8_LF_HEADER_ADJ_ENABLE	0x01
#define V4L2_VP8_LF_HEADER_DELTA_UPDATE	0x02
#define V4L2_VP8_LF_FILTER_TYPE_SIMPLE	0x04
struct v4l2_vp8_loopfilter_header {
	__s8 ref_frm_delta[4];
	__s8 mb_mode_delta[4];
	__u8 sharpness_level;
	__u8 level;
	__u16 padding;
	__u32 flags;
};

struct v4l2_vp8_quantization_header {
	__u8 y_ac_qi;
	__s8 y_dc_delta;
	__s8 y2_dc_delta;
	__s8 y2_ac_delta;
	__s8 uv_dc_delta;
	__s8 uv_ac_delta;
	__u16 padding;
};

#define V4L2_VP8_COEFF_PROB_CNT 11
#define V4L2_VP8_MV_PROB_CNT 19
struct v4l2_vp8_entropy_header {
	__u8 coeff_probs[4][8][3][V4L2_VP8_COEFF_PROB_CNT];
	__u8 y_mode_probs[4];
	__u8 uv_mode_probs[3];
	__u8 mv_probs[2][V4L2_VP8_MV_PROB_CNT];
	__u8 padding[3];
};

struct v4l2_vp8_entropy_coder_state {
	__u8 range;
	__u8 value;
	__u8 bit_count;
	__u8 padding;
};

#define V4L2_VP8_FRAME_HEADER_FLAG_KEY_FRAME		0x01
#define V4L2_VP8_FRAME_HEADER_FLAG_EXPERIMENTAL		0x02
#define V4L2_VP8_FRAME_HEADER_FLAG_SHOW_FRAME		0x04
#define V4L2_VP8_FRAME_HEADER_FLAG_MB_NO_SKIP_COEFF	0x08
#define V4L2_VP8_FRAME_HEADER_FLAG_SIGN_BIAS_GOLDEN	0x10
#define V4L2_VP8_FRAME_HEADER_FLAG_SIGN_BIAS_ALT	0x20

#define VP8_FRAME_IS_KEY_FRAME(hdr) \
	(!!((hdr)->flags & V4L2_VP8_FRAME_HEADER_FLAG_KEY_FRAME))

struct v4l2_ctrl_vp8_frame_header {
	struct v4l2_vp8_segment_header segment_header;
	struct v4l2_vp8_loopfilter_header lf_header;
	struct v4l2_vp8_quantization_header quant_header;
	struct v4l2_vp8_entropy_header entropy_header;
	struct v4l2_vp8_entropy_coder_state coder_state;

	__u16 width;
	__u16 height;

	__u8 horizontal_scale;
	__u8 vertical_scale;

	__u8 version;
	__u8 prob_skip_false;
	__u8 prob_intra;
	__u8 prob_last;
	__u8 prob_gf;
	__u8 num_dct_parts;

	__u32 first_part_size;
	__u32 first_part_header_bits;
	__u32 dct_part_sizes[8];

	__u64 last_frame_ts;
	__u64 golden_frame_ts;
	__u64 alt_frame_ts;

	__u64 flags;
};

#endif
