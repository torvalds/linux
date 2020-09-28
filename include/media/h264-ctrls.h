/* SPDX-License-Identifier: GPL-2.0 */
/*
 * These are the H.264 state controls for use with stateless H.264
 * codec drivers.
 *
 * It turns out that these structs are not stable yet and will undergo
 * more changes. So keep them private until they are stable and ready to
 * become part of the official public API.
 */

#ifndef _H264_CTRLS_H_
#define _H264_CTRLS_H_

#include <linux/videodev2.h>

/*
 * Maximum DPB size, as specified by section 'A.3.1 Level limits
 * common to the Baseline, Main, and Extended profiles'.
 */
#define V4L2_H264_NUM_DPB_ENTRIES 16

#define V4L2_H264_REF_LIST_LEN (2 * V4L2_H264_NUM_DPB_ENTRIES)

/* Our pixel format isn't stable at the moment */
#define V4L2_PIX_FMT_H264_SLICE v4l2_fourcc('S', '2', '6', '4') /* H264 parsed slices */

/*
 * This is put insanely high to avoid conflicting with controls that
 * would be added during the phase where those controls are not
 * stable. It should be fixed eventually.
 */
#define V4L2_CID_MPEG_VIDEO_H264_SPS		(V4L2_CID_MPEG_BASE+1000)
#define V4L2_CID_MPEG_VIDEO_H264_PPS		(V4L2_CID_MPEG_BASE+1001)
#define V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX	(V4L2_CID_MPEG_BASE+1002)
#define V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAMS	(V4L2_CID_MPEG_BASE+1003)
#define V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAMS	(V4L2_CID_MPEG_BASE+1004)
#define V4L2_CID_MPEG_VIDEO_H264_DECODE_MODE	(V4L2_CID_MPEG_BASE+1005)
#define V4L2_CID_MPEG_VIDEO_H264_START_CODE	(V4L2_CID_MPEG_BASE+1006)
#define V4L2_CID_MPEG_VIDEO_H264_PRED_WEIGHTS	(V4L2_CID_MPEG_BASE+1007)

/* enum v4l2_ctrl_type type values */
#define V4L2_CTRL_TYPE_H264_SPS			0x0110
#define V4L2_CTRL_TYPE_H264_PPS			0x0111
#define V4L2_CTRL_TYPE_H264_SCALING_MATRIX	0x0112
#define V4L2_CTRL_TYPE_H264_SLICE_PARAMS	0x0113
#define V4L2_CTRL_TYPE_H264_DECODE_PARAMS	0x0114
#define V4L2_CTRL_TYPE_H264_PRED_WEIGHTS	0x0115

/**
 * enum v4l2_mpeg_video_h264_decode_mode - Decoding mode
 *
 * @V4L2_MPEG_VIDEO_H264_DECODE_MODE_SLICE_BASED: indicates that decoding
 * is performed one slice at a time. In this mode,
 * V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAMS must contain the parsed slice
 * parameters and the OUTPUT buffer must contain a single slice.
 * V4L2_BUF_CAP_SUPPORTS_M2M_HOLD_CAPTURE_BUF feature is used
 * in order to support multislice frames.
 * @V4L2_MPEG_VIDEO_H264_DECODE_MODE_FRAME_BASED: indicates that
 * decoding is performed per frame. The OUTPUT buffer must contain
 * all slices and also both fields. This mode is typically supported
 * by device drivers that are able to parse the slice(s) header(s)
 * in hardware. When this mode is selected,
 * V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAMS is not used.
 */
enum v4l2_mpeg_video_h264_decode_mode {
	V4L2_MPEG_VIDEO_H264_DECODE_MODE_SLICE_BASED,
	V4L2_MPEG_VIDEO_H264_DECODE_MODE_FRAME_BASED,
};

/**
 * enum v4l2_mpeg_video_h264_start_code - Start code
 *
 * @V4L2_MPEG_VIDEO_H264_START_CODE_NONE: slices are passed
 * to the driver without any start code.
 * @V4L2_MPEG_VIDEO_H264_START_CODE_ANNEX_B: slices are passed
 * to the driver with an Annex B start code prefix
 * (legal start codes can be 3-bytes 0x000001 or 4-bytes 0x00000001).
 * This mode is typically supported by device drivers that parse
 * the start code in hardware.
 */
enum v4l2_mpeg_video_h264_start_code {
	V4L2_MPEG_VIDEO_H264_START_CODE_NONE,
	V4L2_MPEG_VIDEO_H264_START_CODE_ANNEX_B,
};

#define V4L2_H264_SPS_CONSTRAINT_SET0_FLAG			0x01
#define V4L2_H264_SPS_CONSTRAINT_SET1_FLAG			0x02
#define V4L2_H264_SPS_CONSTRAINT_SET2_FLAG			0x04
#define V4L2_H264_SPS_CONSTRAINT_SET3_FLAG			0x08
#define V4L2_H264_SPS_CONSTRAINT_SET4_FLAG			0x10
#define V4L2_H264_SPS_CONSTRAINT_SET5_FLAG			0x20

#define V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE		0x01
#define V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS	0x02
#define V4L2_H264_SPS_FLAG_DELTA_PIC_ORDER_ALWAYS_ZERO		0x04
#define V4L2_H264_SPS_FLAG_GAPS_IN_FRAME_NUM_VALUE_ALLOWED	0x08
#define V4L2_H264_SPS_FLAG_FRAME_MBS_ONLY			0x10
#define V4L2_H264_SPS_FLAG_MB_ADAPTIVE_FRAME_FIELD		0x20
#define V4L2_H264_SPS_FLAG_DIRECT_8X8_INFERENCE			0x40

/**
 * struct v4l2_ctrl_h264_sps - H264 sequence parameter set
 *
 * All the members on this sequence parameter set structure match the
 * sequence parameter set syntax as specified by the H264 specification.
 *
 * @profile_idc: see H264 specification.
 * @constraint_set_flags: see H264 specification.
 * @level_idc: see H264 specification.
 * @seq_parameter_set_id: see H264 specification.
 * @chroma_format_idc: see H264 specification.
 * @bit_depth_luma_minus8: see H264 specification.
 * @bit_depth_chroma_minus8: see H264 specification.
 * @log2_max_frame_num_minus4: see H264 specification.
 * @pic_order_cnt_type: see H264 specification.
 * @log2_max_pic_order_cnt_lsb_minus4: see H264 specification.
 * @max_num_ref_frames: see H264 specification.
 * @num_ref_frames_in_pic_order_cnt_cycle: see H264 specification.
 * @offset_for_ref_frame: see H264 specification.
 * @offset_for_non_ref_pic: see H264 specification.
 * @offset_for_top_to_bottom_field: see H264 specification.
 * @pic_width_in_mbs_minus1: see H264 specification.
 * @pic_height_in_map_units_minus1: see H264 specification.
 * @flags: see V4L2_H264_SPS_FLAG_{}.
 */
struct v4l2_ctrl_h264_sps {
	__u8 profile_idc;
	__u8 constraint_set_flags;
	__u8 level_idc;
	__u8 seq_parameter_set_id;
	__u8 chroma_format_idc;
	__u8 bit_depth_luma_minus8;
	__u8 bit_depth_chroma_minus8;
	__u8 log2_max_frame_num_minus4;
	__u8 pic_order_cnt_type;
	__u8 log2_max_pic_order_cnt_lsb_minus4;
	__u8 max_num_ref_frames;
	__u8 num_ref_frames_in_pic_order_cnt_cycle;
	__s32 offset_for_ref_frame[255];
	__s32 offset_for_non_ref_pic;
	__s32 offset_for_top_to_bottom_field;
	__u16 pic_width_in_mbs_minus1;
	__u16 pic_height_in_map_units_minus1;
	__u32 flags;
};

#define V4L2_H264_PPS_FLAG_ENTROPY_CODING_MODE				0x0001
#define V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT	0x0002
#define V4L2_H264_PPS_FLAG_WEIGHTED_PRED				0x0004
#define V4L2_H264_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT		0x0008
#define V4L2_H264_PPS_FLAG_CONSTRAINED_INTRA_PRED			0x0010
#define V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT			0x0020
#define V4L2_H264_PPS_FLAG_TRANSFORM_8X8_MODE				0x0040
#define V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT			0x0080

/**
 * struct v4l2_ctrl_h264_pps - H264 picture parameter set
 *
 * Except where noted, all the members on this picture parameter set
 * structure match the sequence parameter set syntax as specified
 * by the H264 specification.
 *
 * In particular, V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT flag
 * has a specific meaning. This flag should be set if a non-flat
 * scaling matrix applies to the picture. In this case, applications
 * are expected to use V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX,
 * to pass the values of the non-flat matrices.
 *
 * @pic_parameter_set_id: see H264 specification.
 * @seq_parameter_set_id: see H264 specification.
 * @num_slice_groups_minus1: see H264 specification.
 * @num_ref_idx_l0_default_active_minus1: see H264 specification.
 * @num_ref_idx_l1_default_active_minus1: see H264 specification.
 * @weighted_bipred_idc: see H264 specification.
 * @pic_init_qp_minus26: see H264 specification.
 * @pic_init_qs_minus26: see H264 specification.
 * @chroma_qp_index_offset: see H264 specification.
 * @second_chroma_qp_index_offset: see H264 specification.
 * @flags: see V4L2_H264_PPS_FLAG_{}.
 */
struct v4l2_ctrl_h264_pps {
	__u8 pic_parameter_set_id;
	__u8 seq_parameter_set_id;
	__u8 num_slice_groups_minus1;
	__u8 num_ref_idx_l0_default_active_minus1;
	__u8 num_ref_idx_l1_default_active_minus1;
	__u8 weighted_bipred_idc;
	__s8 pic_init_qp_minus26;
	__s8 pic_init_qs_minus26;
	__s8 chroma_qp_index_offset;
	__s8 second_chroma_qp_index_offset;
	__u16 flags;
};

/**
 * struct v4l2_ctrl_h264_scaling_matrix - H264 scaling matrices
 *
 * @scaling_list_4x4: scaling matrix after applying the inverse
 * scanning process. Expected list order is Intra Y, Intra Cb,
 * Intra Cr, Inter Y, Inter Cb, Inter Cr. The values on each
 * scaling list are expected in raster scan order.
 * @scaling_list_8x8: scaling matrix after applying the inverse
 * scanning process. Expected list order is Intra Y, Inter Y,
 * Intra Cb, Inter Cb, Intra Cr, Inter Cr. The values on each
 * scaling list are expected in raster scan order.
 *
 * Note that the list order is different for the 4x4 and 8x8
 * matrices as per the H264 specification, see table 7-2 "Assignment
 * of mnemonic names to scaling list indices and specification of
 * fall-back rule".
 */
struct v4l2_ctrl_h264_scaling_matrix {
	__u8 scaling_list_4x4[6][16];
	__u8 scaling_list_8x8[6][64];
};

struct v4l2_h264_weight_factors {
	__s16 luma_weight[32];
	__s16 luma_offset[32];
	__s16 chroma_weight[32][2];
	__s16 chroma_offset[32][2];
};

#define V4L2_H264_CTRL_PRED_WEIGHTS_REQUIRED(pps, slice) \
	((((pps)->flags & V4L2_H264_PPS_FLAG_WEIGHTED_PRED) && \
	 ((slice)->slice_type == V4L2_H264_SLICE_TYPE_P || \
	  (slice)->slice_type == V4L2_H264_SLICE_TYPE_SP)) || \
	 ((pps)->weighted_bipred_idc == 1 && \
	  (slice)->slice_type == V4L2_H264_SLICE_TYPE_B))

/**
 * struct v4l2_ctrl_h264_pred_weights - Prediction weight table
 *
 * Prediction weight table, which matches the syntax specified
 * by the H264 specification.
 *
 * @luma_log2_weight_denom: see H264 specification.
 * @chroma_log2_weight_denom: see H264 specification.
 * @weight_factors: luma and chroma weight factors.
 */
struct v4l2_ctrl_h264_pred_weights {
	__u16 luma_log2_weight_denom;
	__u16 chroma_log2_weight_denom;
	struct v4l2_h264_weight_factors weight_factors[2];
};

#define V4L2_H264_SLICE_TYPE_P				0
#define V4L2_H264_SLICE_TYPE_B				1
#define V4L2_H264_SLICE_TYPE_I				2
#define V4L2_H264_SLICE_TYPE_SP				3
#define V4L2_H264_SLICE_TYPE_SI				4

#define V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED	0x01
#define V4L2_H264_SLICE_FLAG_SP_FOR_SWITCH		0x02

#define V4L2_H264_TOP_FIELD_REF				0x1
#define V4L2_H264_BOTTOM_FIELD_REF			0x2
#define V4L2_H264_FRAME_REF				0x3

/**
 * struct v4l2_h264_reference - H264 picture reference
 *
 * @fields: indicates how the picture is referenced.
 * Valid values are V4L2_H264_{}_REF.
 * @index: index into v4l2_ctrl_h264_decode_params.dpb[].
 */
struct v4l2_h264_reference {
	__u8 fields;
	__u8 index;
};

/**
 * struct v4l2_ctrl_h264_slice_params - H264 slice parameters
 *
 * This structure holds the H264 syntax elements that are specified
 * as non-invariant for the slices in a given frame.
 *
 * Slice invariant syntax elements are contained in struct
 * v4l2_ctrl_h264_decode_params. This is done to reduce the API surface
 * on frame-based decoders, where slice header parsing is done by the
 * hardware.
 *
 * Slice invariant syntax elements are specified in specification section
 * "7.4.3 Slice header semantics".
 *
 * Except where noted, the members on this struct match the slice header syntax.
 *
 * @header_bit_size: offset in bits to slice_data() from the beginning of this slice.
 * @first_mb_in_slice: see H264 specification.
 * @slice_type: see H264 specification.
 * @colour_plane_id: see H264 specification.
 * @redundant_pic_cnt: see H264 specification.
 * @cabac_init_idc: see H264 specification.
 * @slice_qp_delta: see H264 specification.
 * @slice_qs_delta: see H264 specification.
 * @disable_deblocking_filter_idc: see H264 specification.
 * @slice_alpha_c0_offset_div2: see H264 specification.
 * @slice_beta_offset_div2: see H264 specification.
 * @num_ref_idx_l0_active_minus1: see H264 specification.
 * @num_ref_idx_l1_active_minus1: see H264 specification.
 * @reserved: padding field. Should be zeroed by applications.
 * @ref_pic_list0: reference picture list 0 after applying the per-slice modifications.
 * @ref_pic_list1: reference picture list 1 after applying the per-slice modifications.
 * @flags: see V4L2_H264_SLICE_FLAG_{}.
 */
struct v4l2_ctrl_h264_slice_params {
	__u32 header_bit_size;
	__u32 first_mb_in_slice;
	__u8 slice_type;
	__u8 colour_plane_id;
	__u8 redundant_pic_cnt;
	__u8 cabac_init_idc;
	__s8 slice_qp_delta;
	__s8 slice_qs_delta;
	__u8 disable_deblocking_filter_idc;
	__s8 slice_alpha_c0_offset_div2;
	__s8 slice_beta_offset_div2;
	__u8 num_ref_idx_l0_active_minus1;
	__u8 num_ref_idx_l1_active_minus1;

	__u8 reserved;

	struct v4l2_h264_reference ref_pic_list0[V4L2_H264_REF_LIST_LEN];
	struct v4l2_h264_reference ref_pic_list1[V4L2_H264_REF_LIST_LEN];

	__u32 flags;
};

#define V4L2_H264_DPB_ENTRY_FLAG_VALID		0x01
#define V4L2_H264_DPB_ENTRY_FLAG_ACTIVE		0x02
#define V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM	0x04
#define V4L2_H264_DPB_ENTRY_FLAG_FIELD		0x08

/**
 * struct v4l2_h264_dpb_entry - H264 decoded picture buffer entry
 *
 * @reference_ts: timestamp of the V4L2 capture buffer to use as reference.
 * The timestamp refers to the timestamp field in struct v4l2_buffer.
 * Use v4l2_timeval_to_ns() to convert the struct timeval to a __u64.
 * @pic_num: matches PicNum variable assigned during the reference
 * picture lists construction process.
 * @frame_num: frame identifier which matches frame_num syntax element.
 * @fields: indicates how the DPB entry is referenced. Valid values are
 * V4L2_H264_{}_REF.
 * @reserved: padding field. Should be zeroed by applications.
 * @top_field_order_cnt: matches TopFieldOrderCnt picture value.
 * @bottom_field_order_cnt: matches BottomFieldOrderCnt picture value.
 * Note that picture field is indicated by v4l2_buffer.field.
 * @flags: see V4L2_H264_DPB_ENTRY_FLAG_{}.
 */
struct v4l2_h264_dpb_entry {
	__u64 reference_ts;
	__u32 pic_num;
	__u16 frame_num;
	__u8 fields;
	__u8 reserved[5];
	__s32 top_field_order_cnt;
	__s32 bottom_field_order_cnt;
	__u32 flags;
};

#define V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC		0x01
#define V4L2_H264_DECODE_PARAM_FLAG_FIELD_PIC		0x02
#define V4L2_H264_DECODE_PARAM_FLAG_BOTTOM_FIELD	0x04

/**
 * struct v4l2_ctrl_h264_decode_params - H264 decoding parameters
 *
 * @dpb: decoded picture buffer.
 * @nal_ref_idc: slice header syntax element.
 * @frame_num: slice header syntax element.
 * @top_field_order_cnt: matches TopFieldOrderCnt picture value.
 * @bottom_field_order_cnt: matches BottomFieldOrderCnt picture value.
 * Note that picture field is indicated by v4l2_buffer.field.
 * @idr_pic_id: slice header syntax element.
 * @pic_order_cnt_lsb: slice header syntax element.
 * @delta_pic_order_cnt_bottom: slice header syntax element.
 * @delta_pic_order_cnt0: slice header syntax element.
 * @delta_pic_order_cnt1: slice header syntax element.
 * @dec_ref_pic_marking_bit_size: size in bits of dec_ref_pic_marking()
 * syntax element.
 * @pic_order_cnt_bit_size: size in bits of pic order count syntax.
 * @slice_group_change_cycle: slice header syntax element.
 * @reserved: padding field. Should be zeroed by applications.
 * @flags: see V4L2_H264_DECODE_PARAM_FLAG_{}.
 */
struct v4l2_ctrl_h264_decode_params {
	struct v4l2_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES];
	__u16 nal_ref_idc;
	__u16 frame_num;
	__s32 top_field_order_cnt;
	__s32 bottom_field_order_cnt;
	__u16 idr_pic_id;
	__u16 pic_order_cnt_lsb;
	__s32 delta_pic_order_cnt_bottom;
	__s32 delta_pic_order_cnt0;
	__s32 delta_pic_order_cnt1;
	__u32 dec_ref_pic_marking_bit_size;
	__u32 pic_order_cnt_bit_size;
	__u32 slice_group_change_cycle;

	__u32 reserved;
	__u32 flags;
};

#endif
