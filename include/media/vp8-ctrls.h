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

#define V4L2_CID_MPEG_VIDEO_VP8_FRAME (V4L2_CID_CODEC_BASE + 2000)

#define V4L2_VP8_SEGMENT_FLAG_ENABLED              0x01
#define V4L2_VP8_SEGMENT_FLAG_UPDATE_MAP           0x02
#define V4L2_VP8_SEGMENT_FLAG_UPDATE_FEATURE_DATA  0x04
#define V4L2_VP8_SEGMENT_FLAG_DELTA_VALUE_MODE     0x08

/**
 * struct v4l2_vp8_segment - VP8 segment-based adjustments parameters
 *
 * @quant_update: update values for the segment quantizer.
 * @lf_update: update values for the loop filter level.
 * @segment_probs: branch probabilities of the segment_id decoding tree.
 * @padding: padding field. Should be zeroed by applications.
 * @flags: see V4L2_VP8_SEGMENT_FLAG_{}.
 *
 * This structure contains segment-based adjustments related parameters.
 * See the 'update_segmentation()' part of the frame header syntax,
 * and section '9.3. Segment-Based Adjustments' of the VP8 specification
 * for more details.
 */
struct v4l2_vp8_segment {
	__s8 quant_update[4];
	__s8 lf_update[4];
	__u8 segment_probs[3];
	__u8 padding;
	__u32 flags;
};

#define V4L2_VP8_LF_ADJ_ENABLE	0x01
#define V4L2_VP8_LF_DELTA_UPDATE	0x02
#define V4L2_VP8_LF_FILTER_TYPE_SIMPLE	0x04

/**
 * struct v4l2_vp8_loop_filter - VP8 loop filter parameters
 *
 * @ref_frm_delta: Reference frame signed delta values.
 * @mb_mode_delta: MB prediction mode signed delta values.
 * @sharpness_level: matches sharpness_level syntax element.
 * @level: matches loop_filter_level syntax element.
 * @padding: padding field. Should be zeroed by applications.
 * @flags: see V4L2_VP8_LF_FLAG_{}.
 *
 * This structure contains loop filter related parameters.
 * See the 'mb_lf_adjustments()' part of the frame header syntax,
 * and section '9.4. Loop Filter Type and Levels' of the VP8 specification
 * for more details.
 */
struct v4l2_vp8_loop_filter {
	__s8 ref_frm_delta[4];
	__s8 mb_mode_delta[4];
	__u8 sharpness_level;
	__u8 level;
	__u16 padding;
	__u32 flags;
};

/**
 * struct v4l2_vp8_quantization - VP8 quantizattion indices
 *
 * @y_ac_qi: luma AC coefficient table index.
 * @y_dc_delta: luma DC delta vaue.
 * @y2_dc_delta: y2 block DC delta value.
 * @y2_ac_delta: y2 block AC delta value.
 * @uv_dc_delta: chroma DC delta value.
 * @uv_ac_delta: chroma AC delta value.
 * @padding: padding field. Should be zeroed by applications.

 * This structure contains the quantization indices present
 * in 'quant_indices()' part of the frame header syntax.
 * See section '9.6. Dequantization Indices' of the VP8 specification
 * for more details.
 */
struct v4l2_vp8_quantization {
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

/**
 * struct v4l2_vp8_entropy - VP8 update probabilities
 *
 * @coeff_probs: coefficient probability update values.
 * @y_mode_probs: luma intra-prediction probabilities.
 * @uv_mode_probs: chroma intra-prediction probabilities.
 * @mv_probs: mv decoding probability.
 * @padding: padding field. Should be zeroed by applications.
 *
 * This structure contains the update probabilities present in
 * 'token_prob_update()' and 'mv_prob_update()' part of the frame header.
 * See section '17.2. Probability Updates' of the VP8 specification
 * for more details.
 */
struct v4l2_vp8_entropy {
	__u8 coeff_probs[4][8][3][V4L2_VP8_COEFF_PROB_CNT];
	__u8 y_mode_probs[4];
	__u8 uv_mode_probs[3];
	__u8 mv_probs[2][V4L2_VP8_MV_PROB_CNT];
	__u8 padding[3];
};

/**
 * struct v4l2_vp8_entropy_coder_state - VP8 boolean coder state
 *
 * @range: coder state value for "Range"
 * @value: coder state value for "Value"
 * @bit_count: number of bits left in range "Value".
 * @padding: padding field. Should be zeroed by applications.
 *
 * This structure contains the state for the boolean coder, as
 * explained in section '7. Boolean Entropy Decoder' of the VP8 specification.
 */
struct v4l2_vp8_entropy_coder_state {
	__u8 range;
	__u8 value;
	__u8 bit_count;
	__u8 padding;
};

#define V4L2_VP8_FRAME_FLAG_KEY_FRAME		0x01
#define V4L2_VP8_FRAME_FLAG_EXPERIMENTAL		0x02
#define V4L2_VP8_FRAME_FLAG_SHOW_FRAME		0x04
#define V4L2_VP8_FRAME_FLAG_MB_NO_SKIP_COEFF	0x08
#define V4L2_VP8_FRAME_FLAG_SIGN_BIAS_GOLDEN	0x10
#define V4L2_VP8_FRAME_FLAG_SIGN_BIAS_ALT	0x20

#define V4L2_VP8_FRAME_IS_KEY_FRAME(hdr) \
	(!!((hdr)->flags & V4L2_VP8_FRAME_FLAG_KEY_FRAME))

/**
 * struct v4l2_vp8_frame - VP8 frame parameters
 *
 * @seg: segmentation parameters. See &v4l2_vp8_segment for more details
 * @lf: loop filter parameters. See &v4l2_vp8_loop_filter for more details
 * @quant: quantization parameters. See &v4l2_vp8_quantization for more details
 * @probs: probabilities. See &v4l2_vp9_probabilities for more details
 * @width: frame width.
 * @height: frame height.
 * @horizontal_scale: horizontal scaling factor.
 * @vertical_scale: vertical scaling factor.
 * @version: bitstream version.
 * @prob_skip_false: frame header syntax element.
 * @prob_intra: frame header syntax element.
 * @prob_last: frame header syntax element.
 * @prob_gf: frame header syntax element.
 * @num_dct_parts: number of DCT coefficients partitions.
 * @first_part_size: size of the first partition, i.e. the control partition.
 * @first_part_header_bits: size in bits of the first partition header portion.
 * @dct_part_sizes: DCT coefficients sizes.
 * @last_frame_ts: "last" reference buffer timestamp.
 * The timestamp refers to the timestamp field in struct v4l2_buffer.
 * Use v4l2_timeval_to_ns() to convert the struct timeval to a __u64.
 * @golden_frame_ts: "golden" reference buffer timestamp.
 * @alt_frame_ts: "alt" reference buffer timestamp.
 * @flags: see V4L2_VP8_FRAME_FLAG_{}.
 */
struct v4l2_ctrl_vp8_frame {
	struct v4l2_vp8_segment segment;
	struct v4l2_vp8_loop_filter lf;
	struct v4l2_vp8_quantization quant;
	struct v4l2_vp8_entropy entropy;
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
