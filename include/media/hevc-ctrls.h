/* SPDX-License-Identifier: GPL-2.0 */
/*
 * These are the HEVC state controls for use with stateless HEVC
 * codec drivers.
 *
 * It turns out that these structs are not stable yet and will undergo
 * more changes. So keep them private until they are stable and ready to
 * become part of the official public API.
 */

#ifndef _HEVC_CTRLS_H_
#define _HEVC_CTRLS_H_

#include <linux/videodev2.h>

/* The pixel format isn't stable at the moment and will likely be renamed. */
#define V4L2_PIX_FMT_HEVC_SLICE v4l2_fourcc('S', '2', '6', '5') /* HEVC parsed slices */

#define V4L2_CID_MPEG_VIDEO_HEVC_SPS		(V4L2_CID_CODEC_BASE + 1008)
#define V4L2_CID_MPEG_VIDEO_HEVC_PPS		(V4L2_CID_CODEC_BASE + 1009)
#define V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS	(V4L2_CID_CODEC_BASE + 1010)
#define V4L2_CID_MPEG_VIDEO_HEVC_DECODE_PARAMS	(V4L2_CID_CODEC_BASE + 1012)
#define V4L2_CID_MPEG_VIDEO_HEVC_DECODE_MODE	(V4L2_CID_CODEC_BASE + 1015)
#define V4L2_CID_MPEG_VIDEO_HEVC_START_CODE	(V4L2_CID_CODEC_BASE + 1016)

/* enum v4l2_ctrl_type type values */
#define V4L2_CTRL_TYPE_HEVC_SPS 0x0120
#define V4L2_CTRL_TYPE_HEVC_PPS 0x0121
#define V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS 0x0122
#define V4L2_CTRL_TYPE_HEVC_DECODE_PARAMS 0x0124

enum v4l2_mpeg_video_hevc_decode_mode {
	V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_SLICE_BASED,
	V4L2_MPEG_VIDEO_HEVC_DECODE_MODE_FRAME_BASED,
};

enum v4l2_mpeg_video_hevc_start_code {
	V4L2_MPEG_VIDEO_HEVC_START_CODE_NONE,
	V4L2_MPEG_VIDEO_HEVC_START_CODE_ANNEX_B,
};

#define V4L2_HEVC_SLICE_TYPE_B	0
#define V4L2_HEVC_SLICE_TYPE_P	1
#define V4L2_HEVC_SLICE_TYPE_I	2

#define V4L2_HEVC_SPS_FLAG_SEPARATE_COLOUR_PLANE		(1ULL << 0)
#define V4L2_HEVC_SPS_FLAG_SCALING_LIST_ENABLED			(1ULL << 1)
#define V4L2_HEVC_SPS_FLAG_AMP_ENABLED				(1ULL << 2)
#define V4L2_HEVC_SPS_FLAG_SAMPLE_ADAPTIVE_OFFSET		(1ULL << 3)
#define V4L2_HEVC_SPS_FLAG_PCM_ENABLED				(1ULL << 4)
#define V4L2_HEVC_SPS_FLAG_PCM_LOOP_FILTER_DISABLED		(1ULL << 5)
#define V4L2_HEVC_SPS_FLAG_LONG_TERM_REF_PICS_PRESENT		(1ULL << 6)
#define V4L2_HEVC_SPS_FLAG_SPS_TEMPORAL_MVP_ENABLED		(1ULL << 7)
#define V4L2_HEVC_SPS_FLAG_STRONG_INTRA_SMOOTHING_ENABLED	(1ULL << 8)

/* The controls are not stable at the moment and will likely be reworked. */
struct v4l2_ctrl_hevc_sps {
	/* ISO/IEC 23008-2, ITU-T Rec. H.265: Sequence parameter set */
	__u16	pic_width_in_luma_samples;
	__u16	pic_height_in_luma_samples;
	__u8	bit_depth_luma_minus8;
	__u8	bit_depth_chroma_minus8;
	__u8	log2_max_pic_order_cnt_lsb_minus4;
	__u8	sps_max_dec_pic_buffering_minus1;
	__u8	sps_max_num_reorder_pics;
	__u8	sps_max_latency_increase_plus1;
	__u8	log2_min_luma_coding_block_size_minus3;
	__u8	log2_diff_max_min_luma_coding_block_size;
	__u8	log2_min_luma_transform_block_size_minus2;
	__u8	log2_diff_max_min_luma_transform_block_size;
	__u8	max_transform_hierarchy_depth_inter;
	__u8	max_transform_hierarchy_depth_intra;
	__u8	pcm_sample_bit_depth_luma_minus1;
	__u8	pcm_sample_bit_depth_chroma_minus1;
	__u8	log2_min_pcm_luma_coding_block_size_minus3;
	__u8	log2_diff_max_min_pcm_luma_coding_block_size;
	__u8	num_short_term_ref_pic_sets;
	__u8	num_long_term_ref_pics_sps;
	__u8	chroma_format_idc;
	__u8	sps_max_sub_layers_minus1;

	__u64	flags;
};

#define V4L2_HEVC_PPS_FLAG_DEPENDENT_SLICE_SEGMENT_ENABLED	(1ULL << 0)
#define V4L2_HEVC_PPS_FLAG_OUTPUT_FLAG_PRESENT			(1ULL << 1)
#define V4L2_HEVC_PPS_FLAG_SIGN_DATA_HIDING_ENABLED		(1ULL << 2)
#define V4L2_HEVC_PPS_FLAG_CABAC_INIT_PRESENT			(1ULL << 3)
#define V4L2_HEVC_PPS_FLAG_CONSTRAINED_INTRA_PRED		(1ULL << 4)
#define V4L2_HEVC_PPS_FLAG_TRANSFORM_SKIP_ENABLED		(1ULL << 5)
#define V4L2_HEVC_PPS_FLAG_CU_QP_DELTA_ENABLED			(1ULL << 6)
#define V4L2_HEVC_PPS_FLAG_PPS_SLICE_CHROMA_QP_OFFSETS_PRESENT	(1ULL << 7)
#define V4L2_HEVC_PPS_FLAG_WEIGHTED_PRED			(1ULL << 8)
#define V4L2_HEVC_PPS_FLAG_WEIGHTED_BIPRED			(1ULL << 9)
#define V4L2_HEVC_PPS_FLAG_TRANSQUANT_BYPASS_ENABLED		(1ULL << 10)
#define V4L2_HEVC_PPS_FLAG_TILES_ENABLED			(1ULL << 11)
#define V4L2_HEVC_PPS_FLAG_ENTROPY_CODING_SYNC_ENABLED		(1ULL << 12)
#define V4L2_HEVC_PPS_FLAG_LOOP_FILTER_ACROSS_TILES_ENABLED	(1ULL << 13)
#define V4L2_HEVC_PPS_FLAG_PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED (1ULL << 14)
#define V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_OVERRIDE_ENABLED	(1ULL << 15)
#define V4L2_HEVC_PPS_FLAG_PPS_DISABLE_DEBLOCKING_FILTER	(1ULL << 16)
#define V4L2_HEVC_PPS_FLAG_LISTS_MODIFICATION_PRESENT		(1ULL << 17)
#define V4L2_HEVC_PPS_FLAG_SLICE_SEGMENT_HEADER_EXTENSION_PRESENT (1ULL << 18)
#define V4L2_HEVC_PPS_FLAG_DEBLOCKING_FILTER_CONTROL_PRESENT	(1ULL << 19)
#define V4L2_HEVC_PPS_FLAG_UNIFORM_SPACING			(1ULL << 20)

struct v4l2_ctrl_hevc_pps {
	/* ISO/IEC 23008-2, ITU-T Rec. H.265: Picture parameter set */
	__u8	num_extra_slice_header_bits;
	__u8	num_ref_idx_l0_default_active_minus1;
	__u8	num_ref_idx_l1_default_active_minus1;
	__s8	init_qp_minus26;
	__u8	diff_cu_qp_delta_depth;
	__s8	pps_cb_qp_offset;
	__s8	pps_cr_qp_offset;
	__u8	num_tile_columns_minus1;
	__u8	num_tile_rows_minus1;
	__u8	column_width_minus1[20];
	__u8	row_height_minus1[22];
	__s8	pps_beta_offset_div2;
	__s8	pps_tc_offset_div2;
	__u8	log2_parallel_merge_level_minus2;

	__u8	padding[4];
	__u64	flags;
};

#define V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_BEFORE	0x01
#define V4L2_HEVC_DPB_ENTRY_RPS_ST_CURR_AFTER	0x02
#define V4L2_HEVC_DPB_ENTRY_RPS_LT_CURR		0x03

#define V4L2_HEVC_DPB_ENTRIES_NUM_MAX		16

struct v4l2_hevc_dpb_entry {
	__u64	timestamp;
	__u8	rps;
	__u8	field_pic;
	__u16	pic_order_cnt[2];
	__u8	padding[2];
};

struct v4l2_hevc_pred_weight_table {
	__s8	delta_luma_weight_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	luma_offset_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	delta_chroma_weight_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];
	__s8	chroma_offset_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];

	__s8	delta_luma_weight_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	luma_offset_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	delta_chroma_weight_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];
	__s8	chroma_offset_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];

	__u8	padding[6];

	__u8	luma_log2_weight_denom;
	__s8	delta_chroma_log2_weight_denom;
};

#define V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_LUMA		(1ULL << 0)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_SAO_CHROMA		(1ULL << 1)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_TEMPORAL_MVP_ENABLED	(1ULL << 2)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_MVD_L1_ZERO			(1ULL << 3)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_CABAC_INIT			(1ULL << 4)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_COLLOCATED_FROM_L0		(1ULL << 5)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_USE_INTEGER_MV		(1ULL << 6)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_DEBLOCKING_FILTER_DISABLED (1ULL << 7)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED (1ULL << 8)
#define V4L2_HEVC_SLICE_PARAMS_FLAG_DEPENDENT_SLICE_SEGMENT	(1ULL << 9)

struct v4l2_ctrl_hevc_slice_params {
	__u32	bit_size;
	__u32	data_bit_offset;

	/* ISO/IEC 23008-2, ITU-T Rec. H.265: NAL unit header */
	__u8	nal_unit_type;
	__u8	nuh_temporal_id_plus1;

	/* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
	__u8	slice_type;
	__u8	colour_plane_id;
	__u16	slice_pic_order_cnt;
	__u8	num_ref_idx_l0_active_minus1;
	__u8	num_ref_idx_l1_active_minus1;
	__u8	collocated_ref_idx;
	__u8	five_minus_max_num_merge_cand;
	__s8	slice_qp_delta;
	__s8	slice_cb_qp_offset;
	__s8	slice_cr_qp_offset;
	__s8	slice_act_y_qp_offset;
	__s8	slice_act_cb_qp_offset;
	__s8	slice_act_cr_qp_offset;
	__s8	slice_beta_offset_div2;
	__s8	slice_tc_offset_div2;

	/* ISO/IEC 23008-2, ITU-T Rec. H.265: Picture timing SEI message */
	__u8	pic_struct;

	/* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
	__u8	ref_idx_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	ref_idx_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];

	__u8	padding[5];

	/* ISO/IEC 23008-2, ITU-T Rec. H.265: Weighted prediction parameter */
	struct v4l2_hevc_pred_weight_table pred_weight_table;

	__u64	flags;
};

#define V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC		0x1
#define V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC		0x2
#define V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR  0x4

struct v4l2_ctrl_hevc_decode_params {
	__s32	pic_order_cnt_val;
	__u8	num_active_dpb_entries;
	struct	v4l2_hevc_dpb_entry dpb[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	num_poc_st_curr_before;
	__u8	num_poc_st_curr_after;
	__u8	num_poc_lt_curr;
	__u8	poc_st_curr_before[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	poc_st_curr_after[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	poc_lt_curr[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u64	flags;
};

/*  MPEG-class control IDs specific to the Hantro driver as defined by V4L2 */
#define V4L2_CID_CODEC_HANTRO_BASE				(V4L2_CTRL_CLASS_CODEC | 0x1200)
/*
 * V4L2_CID_HANTRO_HEVC_SLICE_HEADER_SKIP -
 * the number of data (in bits) to skip in the
 * slice segment header.
 * If non-IDR, the bits to be skipped go from syntax element "pic_output_flag"
 * to before syntax element "slice_temporal_mvp_enabled_flag".
 * If IDR, the skipped bits are just "pic_output_flag"
 * (separate_colour_plane_flag is not supported).
 */
#define V4L2_CID_HANTRO_HEVC_SLICE_HEADER_SKIP	(V4L2_CID_CODEC_HANTRO_BASE + 0)

#endif
