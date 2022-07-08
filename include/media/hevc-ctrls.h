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

#define V4L2_CID_STATELESS_HEVC_SPS		(V4L2_CID_CODEC_BASE + 1008)
#define V4L2_CID_STATELESS_HEVC_PPS		(V4L2_CID_CODEC_BASE + 1009)
#define V4L2_CID_STATELESS_HEVC_SLICE_PARAMS	(V4L2_CID_CODEC_BASE + 1010)
#define V4L2_CID_STATELESS_HEVC_SCALING_MATRIX	(V4L2_CID_CODEC_BASE + 1011)
#define V4L2_CID_STATELESS_HEVC_DECODE_PARAMS	(V4L2_CID_CODEC_BASE + 1012)
#define V4L2_CID_STATELESS_HEVC_DECODE_MODE	(V4L2_CID_CODEC_BASE + 1015)
#define V4L2_CID_STATELESS_HEVC_START_CODE	(V4L2_CID_CODEC_BASE + 1016)
#define V4L2_CID_STATELESS_HEVC_ENTRY_POINT_OFFSETS (V4L2_CID_CODEC_BASE + 1017)

enum v4l2_stateless_hevc_decode_mode {
	V4L2_STATELESS_HEVC_DECODE_MODE_SLICE_BASED,
	V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
};

enum v4l2_stateless_hevc_start_code {
	V4L2_STATELESS_HEVC_START_CODE_NONE,
	V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
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

/**
 * struct v4l2_ctrl_hevc_sps - ITU-T Rec. H.265: Sequence parameter set
 *
 * @video_parameter_set_id: specifies the value of the
 *			    vps_video_parameter_set_id of the active VPS
 * @seq_parameter_set_id: provides an identifier for the SPS for
 *			  reference by other syntax elements
 * @pic_width_in_luma_samples: specifies the width of each decoded picture
 *			       in units of luma samples
 * @pic_height_in_luma_samples: specifies the height of each decoded picture
 *				in units of luma samples
 * @bit_depth_luma_minus8: this value plus 8 specifies the bit depth of the
 *                         samples of the luma array
 * @bit_depth_chroma_minus8: this value plus 8 specifies the bit depth of the
 *                           samples of the chroma arrays
 * @log2_max_pic_order_cnt_lsb_minus4: this value plus 4 specifies the value
 *                                     of the variable MaxPicOrderCntLsb
 * @sps_max_dec_pic_buffering_minus1: this value plus 1 specifies the maximum
 *                                    required size of the decoded picture
 *                                    buffer for the codec video sequence
 * @sps_max_num_reorder_pics: indicates the maximum allowed number of pictures
 * @sps_max_latency_increase_plus1: not equal to 0 is used to compute the
 *				    value of SpsMaxLatencyPictures array
 * @log2_min_luma_coding_block_size_minus3: this value plus 3 specifies the
 *                                          minimum luma coding block size
 * @log2_diff_max_min_luma_coding_block_size: specifies the difference between
 *					      the maximum and minimum luma
 *					      coding block size
 * @log2_min_luma_transform_block_size_minus2: this value plus 2 specifies the
 *                                             minimum luma transform block size
 * @log2_diff_max_min_luma_transform_block_size: specifies the difference between
 *						 the maximum and minimum luma
 *						 transform block size
 * @max_transform_hierarchy_depth_inter: specifies the maximum hierarchy
 *					 depth for transform units of
 *					 coding units coded in inter
 *					 prediction mode
 * @max_transform_hierarchy_depth_intra: specifies the maximum hierarchy
 *					 depth for transform units of
 *					 coding units coded in intra
 *					 prediction mode
 * @pcm_sample_bit_depth_luma_minus1: this value plus 1 specifies the number of
 *                                    bits used to represent each of PCM sample
 *                                    values of the luma component
 * @pcm_sample_bit_depth_chroma_minus1: this value plus 1 specifies the number
 *                                      of bits used to represent each of PCM
 *                                      sample values of the chroma components
 * @log2_min_pcm_luma_coding_block_size_minus3: this value plus 3 specifies the
 *                                              minimum size of coding blocks
 * @log2_diff_max_min_pcm_luma_coding_block_size: specifies the difference between
 *						  the maximum and minimum size of
 *						  coding blocks
 * @num_short_term_ref_pic_sets: specifies the number of st_ref_pic_set()
 *				 syntax structures included in the SPS
 * @num_long_term_ref_pics_sps:	specifies the number of candidate long-term
 *				reference pictures that are specified in the SPS
 * @chroma_format_idc: specifies the chroma sampling
 * @sps_max_sub_layers_minus1: this value plus 1 specifies the maximum number
 *                             of temporal sub-layers
 * @reserved: padding field. Should be zeroed by applications.
 * @flags: see V4L2_HEVC_SPS_FLAG_{}
 */
struct v4l2_ctrl_hevc_sps {
	__u8	video_parameter_set_id;
	__u8	seq_parameter_set_id;
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

	__u8	reserved[6];
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

/**
 * struct v4l2_ctrl_hevc_pps - ITU-T Rec. H.265: Picture parameter set
 *
 * @pic_parameter_set_id: identifies the PPS for reference by other
 *			  syntax elements
 * @num_extra_slice_header_bits: specifies the number of extra slice header
 *				 bits that are present in the slice header RBSP
 *				 for coded pictures referring to the PPS.
 * @num_ref_idx_l0_default_active_minus1: this value plus 1 specifies the inferred
 *                                        value of num_ref_idx_l0_active_minus1
 * @num_ref_idx_l1_default_active_minus1: this value plus 1 specifies the inferred
 *                                        value of num_ref_idx_l1_active_minus1
 * @init_qp_minus26: this value plus 26 specifies the initial value of SliceQp Y
 *                   for each slice referring to the PPS
 * @diff_cu_qp_delta_depth: specifies the difference between the luma coding
 *			    tree block size and the minimum luma coding block
 *			    size of coding units that convey cu_qp_delta_abs
 *			    and cu_qp_delta_sign_flag
 * @pps_cb_qp_offset: specify the offsets to the luma quantization parameter Cb
 * @pps_cr_qp_offset: specify the offsets to the luma quantization parameter Cr
 * @num_tile_columns_minus1: this value plus 1 specifies the number of tile columns
 *			     partitioning the picture
 * @num_tile_rows_minus1: this value plus 1 specifies the number of tile rows
 *                        partitioning the picture
 * @column_width_minus1: this value plus 1 specifies the width of each tile column
 *                       in units of coding tree blocks
 * @row_height_minus1: this value plus 1 specifies the height of each tile row in
 *		       units of coding tree blocks
 * @pps_beta_offset_div2: specify the default deblocking parameter offsets for
 *			  beta divided by 2
 * @pps_tc_offset_div2: specify the default deblocking parameter offsets for tC
 *			divided by 2
 * @log2_parallel_merge_level_minus2: this value plus 2 specifies the value of
 *                                    the variable Log2ParMrgLevel
 * @reserved: padding field. Should be zeroed by applications.
 * @flags: see V4L2_HEVC_PPS_FLAG_{}
 */
struct v4l2_ctrl_hevc_pps {
	__u8	pic_parameter_set_id;
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
	__u8	reserved;
	__u64	flags;
};

#define V4L2_HEVC_DPB_ENTRY_LONG_TERM_REFERENCE	0x01

#define V4L2_HEVC_SEI_PIC_STRUCT_FRAME				0
#define V4L2_HEVC_SEI_PIC_STRUCT_TOP_FIELD			1
#define V4L2_HEVC_SEI_PIC_STRUCT_BOTTOM_FIELD			2
#define V4L2_HEVC_SEI_PIC_STRUCT_TOP_BOTTOM			3
#define V4L2_HEVC_SEI_PIC_STRUCT_BOTTOM_TOP			4
#define V4L2_HEVC_SEI_PIC_STRUCT_TOP_BOTTOM_TOP			5
#define V4L2_HEVC_SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM		6
#define V4L2_HEVC_SEI_PIC_STRUCT_FRAME_DOUBLING			7
#define V4L2_HEVC_SEI_PIC_STRUCT_FRAME_TRIPLING			8
#define V4L2_HEVC_SEI_PIC_STRUCT_TOP_PAIRED_PREVIOUS_BOTTOM	9
#define V4L2_HEVC_SEI_PIC_STRUCT_BOTTOM_PAIRED_PREVIOUS_TOP	10
#define V4L2_HEVC_SEI_PIC_STRUCT_TOP_PAIRED_NEXT_BOTTOM		11
#define V4L2_HEVC_SEI_PIC_STRUCT_BOTTOM_PAIRED_NEXT_TOP		12

#define V4L2_HEVC_DPB_ENTRIES_NUM_MAX		16

/**
 * struct v4l2_hevc_dpb_entry - HEVC decoded picture buffer entry
 *
 * @timestamp: timestamp of the V4L2 capture buffer to use as reference.
 * @flags: long term flag for the reference frame
 * @field_pic: whether the reference is a field picture or a frame.
 * @reserved: padding field. Should be zeroed by applications.
 * @pic_order_cnt_val: the picture order count of the reference.
 */
struct v4l2_hevc_dpb_entry {
	__u64	timestamp;
	__u8	flags;
	__u8	field_pic;
	__u16	reserved;
	__s32	pic_order_cnt_val;
};

/**
 * struct v4l2_hevc_pred_weight_table - HEVC weighted prediction parameters
 *
 * @delta_luma_weight_l0: the difference of the weighting factor applied
 *			  to the luma prediction value for list 0
 * @luma_offset_l0: the additive offset applied to the luma prediction value
 *		    for list 0
 * @delta_chroma_weight_l0: the difference of the weighting factor applied
 *			    to the chroma prediction values for list 0
 * @chroma_offset_l0: the difference of the additive offset applied to
 *		      the chroma prediction values for list 0
 * @delta_luma_weight_l1: the difference of the weighting factor applied
 *			  to the luma prediction value for list 1
 * @luma_offset_l1: the additive offset applied to the luma prediction value
 *		    for list 1
 * @delta_chroma_weight_l1: the difference of the weighting factor applied
 *			    to the chroma prediction values for list 1
 * @chroma_offset_l1: the difference of the additive offset applied to
 *		      the chroma prediction values for list 1
 * @luma_log2_weight_denom: the base 2 logarithm of the denominator for
 *			    all luma weighting factors
 * @delta_chroma_log2_weight_denom: the difference of the base 2 logarithm
 *				    of the denominator for all chroma
 *				    weighting factors
 */
struct v4l2_hevc_pred_weight_table {
	__s8	delta_luma_weight_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	luma_offset_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	delta_chroma_weight_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];
	__s8	chroma_offset_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];

	__s8	delta_luma_weight_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	luma_offset_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__s8	delta_chroma_weight_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];
	__s8	chroma_offset_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX][2];

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

/**
 * struct v4l2_ctrl_hevc_slice_params - HEVC slice parameters
 *
 * This control is a dynamically sized 1-dimensional array,
 * V4L2_CTRL_FLAG_DYNAMIC_ARRAY flag must be set when using it.
 *
 * @bit_size: size (in bits) of the current slice data
 * @data_bit_offset: offset (in bits) to the video data in the current slice data
 * @num_entry_point_offsets: specifies the number of entry point offset syntax
 *			     elements in the slice header.
 * @nal_unit_type: specifies the coding type of the slice (B, P or I)
 * @nuh_temporal_id_plus1: minus 1 specifies a temporal identifier for the NAL unit
 * @slice_type: see V4L2_HEVC_SLICE_TYPE_{}
 * @colour_plane_id: specifies the colour plane associated with the current slice
 * @slice_pic_order_cnt: specifies the picture order count
 * @num_ref_idx_l0_active_minus1: this value plus 1 specifies the maximum reference
 *                                index for reference picture list 0 that may be
 *                                used to decode the slice
 * @num_ref_idx_l1_active_minus1: this value plus 1 specifies the maximum reference
 *                                index for reference picture list 1 that may be
 *                                used to decode the slice
 * @collocated_ref_idx: specifies the reference index of the collocated picture used
 *			for temporal motion vector prediction
 * @five_minus_max_num_merge_cand: specifies the maximum number of merging
 *				   motion vector prediction candidates supported in
 *				   the slice subtracted from 5
 * @slice_qp_delta: specifies the initial value of QpY to be used for the coding
 *		    blocks in the slice
 * @slice_cb_qp_offset: specifies a difference to be added to the value of pps_cb_qp_offset
 * @slice_cr_qp_offset: specifies a difference to be added to the value of pps_cr_qp_offset
 * @slice_act_y_qp_offset: screen content extension parameters
 * @slice_act_cb_qp_offset: screen content extension parameters
 * @slice_act_cr_qp_offset: screen content extension parameters
 * @slice_beta_offset_div2: specify the deblocking parameter offsets for beta divided by 2
 * @slice_tc_offset_div2: specify the deblocking parameter offsets for tC divided by 2
 * @pic_struct: indicates whether a picture should be displayed as a frame or as one or
 *		more fields
 * @reserved0: padding field. Should be zeroed by applications.
 * @slice_segment_addr: specifies the address of the first coding tree block in
 *			the slice segment
 * @ref_idx_l0: the list of L0 reference elements as indices in the DPB
 * @ref_idx_l1: the list of L1 reference elements as indices in the DPB
 * @short_term_ref_pic_set_size: specifies the size of short-term reference
 *				 pictures included in the SPS
 * @long_term_ref_pic_set_size: specifies the size of long-term reference
 *				picture include in the SPS
 * @pred_weight_table: the prediction weight coefficients for inter-picture
 *		       prediction
 * @reserved1: padding field. Should be zeroed by applications.
 * @flags: see V4L2_HEVC_SLICE_PARAMS_FLAG_{}
 */
struct v4l2_ctrl_hevc_slice_params {
	__u32	bit_size;
	__u32	data_bit_offset;
	__u32	num_entry_point_offsets;
	/* ISO/IEC 23008-2, ITU-T Rec. H.265: NAL unit header */
	__u8	nal_unit_type;
	__u8	nuh_temporal_id_plus1;

	/* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
	__u8	slice_type;
	__u8	colour_plane_id;
	__s32	slice_pic_order_cnt;
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

	__u8	reserved0[3];
	/* ISO/IEC 23008-2, ITU-T Rec. H.265: General slice segment header */
	__u32	slice_segment_addr;
	__u8	ref_idx_l0[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	ref_idx_l1[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u16	short_term_ref_pic_set_size;
	__u16	long_term_ref_pic_set_size;

	/* ISO/IEC 23008-2, ITU-T Rec. H.265: Weighted prediction parameter */
	struct v4l2_hevc_pred_weight_table pred_weight_table;

	__u8	reserved1[2];
	__u64	flags;
};

#define V4L2_HEVC_DECODE_PARAM_FLAG_IRAP_PIC		0x1
#define V4L2_HEVC_DECODE_PARAM_FLAG_IDR_PIC		0x2
#define V4L2_HEVC_DECODE_PARAM_FLAG_NO_OUTPUT_OF_PRIOR  0x4

/**
 * struct v4l2_ctrl_hevc_decode_params - HEVC decode parameters
 *
 * @pic_order_cnt_val: picture order count
 * @short_term_ref_pic_set_size: specifies the size of short-term reference
 *				 pictures set included in the SPS of the first slice
 * @long_term_ref_pic_set_size: specifies the size of long-term reference
 *				pictures set include in the SPS of the first slice
 * @num_active_dpb_entries: the number of entries in dpb
 * @num_poc_st_curr_before: the number of reference pictures in the short-term
 *			    set that come before the current frame
 * @num_poc_st_curr_after: the number of reference pictures in the short-term
 *			   set that come after the current frame
 * @num_poc_lt_curr: the number of reference pictures in the long-term set
 * @poc_st_curr_before: provides the index of the short term before references
 *			in DPB array
 * @poc_st_curr_after: provides the index of the short term after references
 *		       in DPB array
 * @poc_lt_curr: provides the index of the long term references in DPB array
 * @reserved: padding field. Should be zeroed by applications.
 * @dpb: the decoded picture buffer, for meta-data about reference frames
 * @flags: see V4L2_HEVC_DECODE_PARAM_FLAG_{}
 */
struct v4l2_ctrl_hevc_decode_params {
	__s32	pic_order_cnt_val;
	__u16	short_term_ref_pic_set_size;
	__u16	long_term_ref_pic_set_size;
	__u8	num_active_dpb_entries;
	__u8	num_poc_st_curr_before;
	__u8	num_poc_st_curr_after;
	__u8	num_poc_lt_curr;
	__u8	poc_st_curr_before[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	poc_st_curr_after[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	poc_lt_curr[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u8	reserved[4];
	struct	v4l2_hevc_dpb_entry dpb[V4L2_HEVC_DPB_ENTRIES_NUM_MAX];
	__u64	flags;
};

/**
 * struct v4l2_ctrl_hevc_scaling_matrix - HEVC scaling lists parameters
 *
 * @scaling_list_4x4: scaling list is used for the scaling process for
 *		      transform coefficients. The values on each scaling
 *		      list are expected in raster scan order
 * @scaling_list_8x8: scaling list is used for the scaling process for
 *		      transform coefficients. The values on each scaling
 *		      list are expected in raster scan order
 * @scaling_list_16x16: scaling list is used for the scaling process for
 *			transform coefficients. The values on each scaling
 *			list are expected in raster scan order
 * @scaling_list_32x32:	scaling list is used for the scaling process for
 *			transform coefficients. The values on each scaling
 *			list are expected in raster scan order
 * @scaling_list_dc_coef_16x16: scaling list is used for the scaling process
 *				for transform coefficients. The values on each
 *				scaling list are expected in raster scan order.
 * @scaling_list_dc_coef_32x32:	scaling list is used for the scaling process
 *				for transform coefficients. The values on each
 *				scaling list are expected in raster scan order.
 */
struct v4l2_ctrl_hevc_scaling_matrix {
	__u8	scaling_list_4x4[6][16];
	__u8	scaling_list_8x8[6][64];
	__u8	scaling_list_16x16[6][64];
	__u8	scaling_list_32x32[2][64];
	__u8	scaling_list_dc_coef_16x16[6];
	__u8	scaling_list_dc_coef_32x32[2];
};

#endif
