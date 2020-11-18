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

/* enum v4l2_ctrl_type type values */
#define V4L2_CTRL_TYPE_H264_SPS			0x0110
#define V4L2_CTRL_TYPE_H264_PPS			0x0111
#define V4L2_CTRL_TYPE_H264_SCALING_MATRIX	0x0112
#define V4L2_CTRL_TYPE_H264_SLICE_PARAMS	0x0113
#define V4L2_CTRL_TYPE_H264_DECODE_PARAMS	0x0114

enum v4l2_mpeg_video_h264_decode_mode {
	V4L2_MPEG_VIDEO_H264_DECODE_MODE_SLICE_BASED,
	V4L2_MPEG_VIDEO_H264_DECODE_MODE_FRAME_BASED,
};

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
#define V4L2_H264_PPS_FLAG_PIC_SCALING_MATRIX_PRESENT			0x0080

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

struct v4l2_h264_pred_weight_table {
	__u16 luma_log2_weight_denom;
	__u16 chroma_log2_weight_denom;
	struct v4l2_h264_weight_factors weight_factors[2];
};

#define V4L2_H264_SLICE_TYPE_P				0
#define V4L2_H264_SLICE_TYPE_B				1
#define V4L2_H264_SLICE_TYPE_I				2
#define V4L2_H264_SLICE_TYPE_SP				3
#define V4L2_H264_SLICE_TYPE_SI				4

#define V4L2_H264_SLICE_FLAG_FIELD_PIC			0x01
#define V4L2_H264_SLICE_FLAG_BOTTOM_FIELD		0x02
#define V4L2_H264_SLICE_FLAG_DIRECT_SPATIAL_MV_PRED	0x04
#define V4L2_H264_SLICE_FLAG_SP_FOR_SWITCH		0x08

struct v4l2_ctrl_h264_slice_params {
	/* Size in bytes, including header */
	__u32 size;

	/* Offset in bytes to the start of slice in the OUTPUT buffer. */
	__u32 start_byte_offset;

	/* Offset in bits to slice_data() from the beginning of this slice. */
	__u32 header_bit_size;

	__u16 first_mb_in_slice;
	__u8 slice_type;
	__u8 pic_parameter_set_id;
	__u8 colour_plane_id;
	__u8 redundant_pic_cnt;
	__u16 frame_num;
	__u16 idr_pic_id;
	__u16 pic_order_cnt_lsb;
	__s32 delta_pic_order_cnt_bottom;
	__s32 delta_pic_order_cnt0;
	__s32 delta_pic_order_cnt1;

	struct v4l2_h264_pred_weight_table pred_weight_table;
	/* Size in bits of dec_ref_pic_marking() syntax element. */
	__u32 dec_ref_pic_marking_bit_size;
	/* Size in bits of pic order count syntax. */
	__u32 pic_order_cnt_bit_size;

	__u8 cabac_init_idc;
	__s8 slice_qp_delta;
	__s8 slice_qs_delta;
	__u8 disable_deblocking_filter_idc;
	__s8 slice_alpha_c0_offset_div2;
	__s8 slice_beta_offset_div2;
	__u8 num_ref_idx_l0_active_minus1;
	__u8 num_ref_idx_l1_active_minus1;
	__u32 slice_group_change_cycle;

	/*
	 * Entries on each list are indices into
	 * v4l2_ctrl_h264_decode_params.dpb[].
	 */
	__u8 ref_pic_list0[32];
	__u8 ref_pic_list1[32];

	__u32 flags;
};

#define V4L2_H264_DPB_ENTRY_FLAG_VALID		0x01
#define V4L2_H264_DPB_ENTRY_FLAG_ACTIVE		0x02
#define V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM	0x04
#define V4L2_H264_DPB_ENTRY_FLAG_FIELD		0x08
#define V4L2_H264_DPB_ENTRY_FLAG_BOTTOM_FIELD	0x10

struct v4l2_h264_dpb_entry {
	__u64 reference_ts;
	__u16 frame_num;
	__u16 pic_num;
	/* Note that field is indicated by v4l2_buffer.field */
	__s32 top_field_order_cnt;
	__s32 bottom_field_order_cnt;
	__u32 flags; /* V4L2_H264_DPB_ENTRY_FLAG_* */
};

#define V4L2_H264_DECODE_PARAM_FLAG_IDR_PIC	0x01

struct v4l2_ctrl_h264_decode_params {
	struct v4l2_h264_dpb_entry dpb[V4L2_H264_NUM_DPB_ENTRIES];
	__u16 num_slices;
	__u16 nal_ref_idc;
	__s32 top_field_order_cnt;
	__s32 bottom_field_order_cnt;
	__u32 flags; /* V4L2_H264_DECODE_PARAM_FLAG_* */
};

#endif
