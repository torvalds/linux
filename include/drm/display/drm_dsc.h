/* SPDX-License-Identifier: MIT
 * Copyright (C) 2018 Intel Corp.
 *
 * Authors:
 * Manasi Navare <manasi.d.navare@intel.com>
 */

#ifndef DRM_DSC_H_
#define DRM_DSC_H_

#include <drm/display/drm_dp.h>

/* VESA Display Stream Compression DSC 1.2 constants */
#define DSC_NUM_BUF_RANGES			15
#define DSC_MUX_WORD_SIZE_8_10_BPC		48
#define DSC_MUX_WORD_SIZE_12_BPC		64
#define DSC_RC_PIXELS_PER_GROUP			3
#define DSC_SCALE_DECREMENT_INTERVAL_MAX	4095
#define DSC_RANGE_BPG_OFFSET_MASK		0x3f

/* DSC Rate Control Constants */
#define DSC_RC_MODEL_SIZE_CONST		    8192
#define DSC_RC_EDGE_FACTOR_CONST	    6
#define DSC_RC_TGT_OFFSET_HI_CONST	    3
#define DSC_RC_TGT_OFFSET_LO_CONST	    3

/* DSC PPS constants and macros */
#define DSC_PPS_VERSION_MAJOR_SHIFT		4
#define DSC_PPS_BPC_SHIFT			4
#define DSC_PPS_MSB_SHIFT			8
#define DSC_PPS_LSB_MASK			(0xFF << 0)
#define DSC_PPS_BPP_HIGH_MASK			(0x3 << 8)
#define DSC_PPS_VBR_EN_SHIFT			2
#define DSC_PPS_SIMPLE422_SHIFT			3
#define DSC_PPS_CONVERT_RGB_SHIFT		4
#define DSC_PPS_BLOCK_PRED_EN_SHIFT		5
#define DSC_PPS_INIT_XMIT_DELAY_HIGH_MASK	(0x3 << 8)
#define DSC_PPS_SCALE_DEC_INT_HIGH_MASK		(0xF << 8)
#define DSC_PPS_RC_TGT_OFFSET_HI_SHIFT		4
#define DSC_PPS_RC_RANGE_MINQP_SHIFT		11
#define DSC_PPS_RC_RANGE_MAXQP_SHIFT		6
#define DSC_PPS_NATIVE_420_SHIFT		1

/**
 * struct drm_dsc_rc_range_parameters - DSC Rate Control range parameters
 *
 * This defines different rate control parameters used by the DSC engine
 * to compress the frame.
 */
struct drm_dsc_rc_range_parameters {
	/**
	 * @range_min_qp: Min Quantization Parameters allowed for this range
	 */
	u8 range_min_qp;
	/**
	 * @range_max_qp: Max Quantization Parameters allowed for this range
	 */
	u8 range_max_qp;
	/**
	 * @range_bpg_offset:
	 * Bits/group offset to apply to target for this group
	 */
	u8 range_bpg_offset;
};

/**
 * struct drm_dsc_config - Parameters required to configure DSC
 *
 * Driver populates this structure with all the parameters required
 * to configure the display stream compression on the source.
 */
struct drm_dsc_config {
	/**
	 * @line_buf_depth:
	 * Bits per component for previous reconstructed line buffer
	 */
	u8 line_buf_depth;
	/**
	 * @bits_per_component: Bits per component to code (8/10/12)
	 */
	u8 bits_per_component;
	/**
	 * @convert_rgb:
	 * Flag to indicate if RGB - YCoCg conversion is needed
	 * True if RGB input, False if YCoCg input
	 */
	bool convert_rgb;
	/**
	 * @slice_count: Number fo slices per line used by the DSC encoder
	 */
	u8 slice_count;
	/**
	 *  @slice_width: Width of each slice in pixels
	 */
	u16 slice_width;
	/**
	 * @slice_height: Slice height in pixels
	 */
	u16 slice_height;
	/**
	 * @simple_422: True if simple 4_2_2 mode is enabled else False
	 */
	bool simple_422;
	/**
	 * @pic_width: Width of the input display frame in pixels
	 */
	u16 pic_width;
	/**
	 * @pic_height: Vertical height of the input display frame
	 */
	u16 pic_height;
	/**
	 * @rc_tgt_offset_high:
	 * Offset to bits/group used by RC to determine QP adjustment
	 */
	u8 rc_tgt_offset_high;
	/**
	 * @rc_tgt_offset_low:
	 * Offset to bits/group used by RC to determine QP adjustment
	 */
	u8 rc_tgt_offset_low;
	/**
	 * @bits_per_pixel:
	 * Target bits per pixel with 4 fractional bits, bits_per_pixel << 4
	 */
	u16 bits_per_pixel;
	/**
	 * @rc_edge_factor:
	 * Factor to determine if an edge is present based on the bits produced
	 */
	u8 rc_edge_factor;
	/**
	 * @rc_quant_incr_limit1:
	 * Slow down incrementing once the range reaches this value
	 */
	u8 rc_quant_incr_limit1;
	/**
	 * @rc_quant_incr_limit0:
	 * Slow down incrementing once the range reaches this value
	 */
	u8 rc_quant_incr_limit0;
	/**
	 * @initial_xmit_delay:
	 * Number of pixels to delay the initial transmission
	 */
	u16 initial_xmit_delay;
	/**
	 * @initial_dec_delay:
	 * Initial decoder delay, number of pixel times that the decoder
	 * accumulates data in its rate buffer before starting to decode
	 * and output pixels.
	 */
	u16  initial_dec_delay;
	/**
	 * @block_pred_enable:
	 * True if block prediction is used to code any groups within the
	 * picture. False if BP not used
	 */
	bool block_pred_enable;
	/**
	 * @first_line_bpg_offset:
	 * Number of additional bits allocated for each group on the first
	 * line of slice.
	 */
	u8 first_line_bpg_offset;
	/**
	 * @initial_offset: Value to use for RC model offset at slice start
	 */
	u16 initial_offset;
	/**
	 * @rc_buf_thresh: Thresholds defining each of the buffer ranges
	 */
	u16 rc_buf_thresh[DSC_NUM_BUF_RANGES - 1];
	/**
	 * @rc_range_params:
	 * Parameters for each of the RC ranges defined in
	 * &struct drm_dsc_rc_range_parameters
	 */
	struct drm_dsc_rc_range_parameters rc_range_params[DSC_NUM_BUF_RANGES];
	/**
	 * @rc_model_size: Total size of RC model
	 */
	u16 rc_model_size;
	/**
	 * @flatness_min_qp: Minimum QP where flatness information is sent
	 */
	u8 flatness_min_qp;
	/**
	 * @flatness_max_qp: Maximum QP where flatness information is sent
	 */
	u8 flatness_max_qp;
	/**
	 * @initial_scale_value: Initial value for the scale factor
	 */
	u8 initial_scale_value;
	/**
	 * @scale_decrement_interval:
	 * Specifies number of group times between decrementing the scale factor
	 * at beginning of a slice.
	 */
	u16 scale_decrement_interval;
	/**
	 * @scale_increment_interval:
	 * Number of group times between incrementing the scale factor value
	 * used at the beginning of a slice.
	 */
	u16 scale_increment_interval;
	/**
	 * @nfl_bpg_offset: Non first line BPG offset to be used
	 */
	u16 nfl_bpg_offset;
	/**
	 * @slice_bpg_offset: BPG offset used to enforce slice bit
	 */
	u16 slice_bpg_offset;
	/**
	 * @final_offset: Final RC linear transformation offset value
	 */
	u16 final_offset;
	/**
	 * @vbr_enable: True if VBR mode is enabled, false if disabled
	 */
	bool vbr_enable;
	/**
	 * @mux_word_size: Mux word size (in bits) for SSM mode
	 */
	u8 mux_word_size;
	/**
	 * @slice_chunk_size:
	 * The (max) size in bytes of the "chunks" that are used in slice
	 * multiplexing.
	 */
	u16 slice_chunk_size;
	/**
	 * @rc_bits: Rate control buffer size in bits
	 */
	u16 rc_bits;
	/**
	 * @dsc_version_minor: DSC minor version
	 */
	u8 dsc_version_minor;
	/**
	 * @dsc_version_major: DSC major version
	 */
	u8 dsc_version_major;
	/**
	 * @native_422: True if Native 4:2:2 supported, else false
	 */
	bool native_422;
	/**
	 * @native_420: True if Native 4:2:0 supported else false.
	 */
	bool native_420;
	/**
	 * @second_line_bpg_offset:
	 * Additional bits/grp for seconnd line of slice for native 4:2:0
	 */
	u8 second_line_bpg_offset;
	/**
	 * @nsl_bpg_offset:
	 * Num of bits deallocated for each grp that is not in second line of
	 * slice
	 */
	u16 nsl_bpg_offset;
	/**
	 * @second_line_offset_adj:
	 * Offset adjustment for second line in Native 4:2:0 mode
	 */
	u16 second_line_offset_adj;
};

/**
 * struct drm_dsc_picture_parameter_set - Represents 128 bytes of
 * 	Picture Parameter Set
 *
 * The VESA DSC standard defines picture parameter set (PPS) which display
 * stream compression encoders must communicate to decoders.
 * The PPS is encapsulated in 128 bytes (PPS 0 through PPS 127). The fields in
 * this structure are as per Table 4.1 in Vesa DSC specification v1.1/v1.2.
 * The PPS fields that span over more than a byte should be stored in Big Endian
 * format.
 */
struct drm_dsc_picture_parameter_set {
	/**
	 * @dsc_version:
	 * PPS0[3:0] - dsc_version_minor: Contains Minor version of DSC
	 * PPS0[7:4] - dsc_version_major: Contains major version of DSC
	 */
	u8 dsc_version;
	/**
	 * @pps_identifier:
	 * PPS1[7:0] - Application specific identifier that can be
	 * used to differentiate between different PPS tables.
	 */
	u8 pps_identifier;
	/**
	 * @pps_reserved:
	 * PPS2[7:0]- RESERVED Byte
	 */
	u8 pps_reserved;
	/**
	 * @pps_3:
	 * PPS3[3:0] - linebuf_depth: Contains linebuffer bit depth used to
	 * generate the bitstream. (0x0 - 16 bits for DSC 1.2, 0x8 - 8 bits,
	 * 0xA - 10 bits, 0xB - 11 bits, 0xC - 12 bits, 0xD - 13 bits,
	 * 0xE - 14 bits for DSC1.2, 0xF - 14 bits for DSC 1.2.
	 * PPS3[7:4] - bits_per_component: Bits per component for the original
	 * pixels of the encoded picture.
	 * 0x0 = 16bpc (allowed only when dsc_version_minor = 0x2)
	 * 0x8 = 8bpc, 0xA = 10bpc, 0xC = 12bpc, 0xE = 14bpc (also
	 * allowed only when dsc_minor_version = 0x2)
	 */
	u8 pps_3;
	/**
	 * @pps_4:
	 * PPS4[1:0] -These are the most significant 2 bits of
	 * compressed BPP bits_per_pixel[9:0] syntax element.
	 * PPS4[2] - vbr_enable: 0 = VBR disabled, 1 = VBR enabled
	 * PPS4[3] - simple_422: Indicates if decoder drops samples to
	 * reconstruct the 4:2:2 picture.
	 * PPS4[4] - Convert_rgb: Indicates if DSC color space conversion is
	 * active.
	 * PPS4[5] - blobk_pred_enable: Indicates if BP is used to code any
	 * groups in picture
	 * PPS4[7:6] - Reseved bits
	 */
	u8 pps_4;
	/**
	 * @bits_per_pixel_low:
	 * PPS5[7:0] - This indicates the lower significant 8 bits of
	 * the compressed BPP bits_per_pixel[9:0] element.
	 */
	u8 bits_per_pixel_low;
	/**
	 * @pic_height:
	 * PPS6[7:0], PPS7[7:0] -pic_height: Specifies the number of pixel rows
	 * within the raster.
	 */
	__be16 pic_height;
	/**
	 * @pic_width:
	 * PPS8[7:0], PPS9[7:0] - pic_width: Number of pixel columns within
	 * the raster.
	 */
	__be16 pic_width;
	/**
	 * @slice_height:
	 * PPS10[7:0], PPS11[7:0] - Slice height in units of pixels.
	 */
	__be16 slice_height;
	/**
	 * @slice_width:
	 * PPS12[7:0], PPS13[7:0] - Slice width in terms of pixels.
	 */
	__be16 slice_width;
	/**
	 * @chunk_size:
	 * PPS14[7:0], PPS15[7:0] - Size in units of bytes of the chunks
	 * that are used for slice multiplexing.
	 */
	__be16 chunk_size;
	/**
	 * @initial_xmit_delay_high:
	 * PPS16[1:0] - Most Significant two bits of initial transmission delay.
	 * It specifies the number of pixel times that the encoder waits before
	 * transmitting data from its rate buffer.
	 * PPS16[7:2] - Reserved
	 */
	u8 initial_xmit_delay_high;
	/**
	 * @initial_xmit_delay_low:
	 * PPS17[7:0] - Least significant 8 bits of initial transmission delay.
	 */
	u8 initial_xmit_delay_low;
	/**
	 * @initial_dec_delay:
	 *
	 * PPS18[7:0], PPS19[7:0] - Initial decoding delay which is the number
	 * of pixel times that the decoder accumulates data in its rate buffer
	 * before starting to decode and output pixels.
	 */
	__be16 initial_dec_delay;
	/**
	 * @pps20_reserved:
	 *
	 * PPS20[7:0] - Reserved
	 */
	u8 pps20_reserved;
	/**
	 * @initial_scale_value:
	 * PPS21[5:0] - Initial rcXformScale factor used at beginning
	 * of a slice.
	 * PPS21[7:6] - Reserved
	 */
	u8 initial_scale_value;
	/**
	 * @scale_increment_interval:
	 * PPS22[7:0], PPS23[7:0] - Number of group times between incrementing
	 * the rcXformScale factor at end of a slice.
	 */
	__be16 scale_increment_interval;
	/**
	 * @scale_decrement_interval_high:
	 * PPS24[3:0] - Higher 4 bits indicating number of group times between
	 * decrementing the rcXformScale factor at beginning of a slice.
	 * PPS24[7:4] - Reserved
	 */
	u8 scale_decrement_interval_high;
	/**
	 * @scale_decrement_interval_low:
	 * PPS25[7:0] - Lower 8 bits of scale decrement interval
	 */
	u8 scale_decrement_interval_low;
	/**
	 * @pps26_reserved:
	 * PPS26[7:0]
	 */
	u8 pps26_reserved;
	/**
	 * @first_line_bpg_offset:
	 * PPS27[4:0] - Number of additional bits that are allocated
	 * for each group on first line of a slice.
	 * PPS27[7:5] - Reserved
	 */
	u8 first_line_bpg_offset;
	/**
	 * @nfl_bpg_offset:
	 * PPS28[7:0], PPS29[7:0] - Number of bits including frac bits
	 * deallocated for each group for groups after the first line of slice.
	 */
	__be16 nfl_bpg_offset;
	/**
	 * @slice_bpg_offset:
	 * PPS30, PPS31[7:0] - Number of bits that are deallocated for each
	 * group to enforce the slice constraint.
	 */
	__be16 slice_bpg_offset;
	/**
	 * @initial_offset:
	 * PPS32,33[7:0] - Initial value for rcXformOffset
	 */
	__be16 initial_offset;
	/**
	 * @final_offset:
	 * PPS34,35[7:0] - Maximum end-of-slice value for rcXformOffset
	 */
	__be16 final_offset;
	/**
	 * @flatness_min_qp:
	 * PPS36[4:0] - Minimum QP at which flatness is signaled and
	 * flatness QP adjustment is made.
	 * PPS36[7:5] - Reserved
	 */
	u8 flatness_min_qp;
	/**
	 * @flatness_max_qp:
	 * PPS37[4:0] - Max QP at which flatness is signalled and
	 * the flatness adjustment is made.
	 * PPS37[7:5] - Reserved
	 */
	u8 flatness_max_qp;
	/**
	 * @rc_model_size:
	 * PPS38,39[7:0] - Number of bits within RC Model.
	 */
	__be16 rc_model_size;
	/**
	 * @rc_edge_factor:
	 * PPS40[3:0] - Ratio of current activity vs, previous
	 * activity to determine presence of edge.
	 * PPS40[7:4] - Reserved
	 */
	u8 rc_edge_factor;
	/**
	 * @rc_quant_incr_limit0:
	 * PPS41[4:0] - QP threshold used in short term RC
	 * PPS41[7:5] - Reserved
	 */
	u8 rc_quant_incr_limit0;
	/**
	 * @rc_quant_incr_limit1:
	 * PPS42[4:0] - QP threshold used in short term RC
	 * PPS42[7:5] - Reserved
	 */
	u8 rc_quant_incr_limit1;
	/**
	 * @rc_tgt_offset:
	 * PPS43[3:0] - Lower end of the variability range around the target
	 * bits per group that is allowed by short term RC.
	 * PPS43[7:4]- Upper end of the variability range around the target
	 * bits per group that i allowed by short term rc.
	 */
	u8 rc_tgt_offset;
	/**
	 * @rc_buf_thresh:
	 * PPS44[7:0] - PPS57[7:0] - Specifies the thresholds in RC model for
	 * the 15 ranges defined by 14 thresholds.
	 */
	u8 rc_buf_thresh[DSC_NUM_BUF_RANGES - 1];
	/**
	 * @rc_range_parameters:
	 * PPS58[7:0] - PPS87[7:0]
	 * Parameters that correspond to each of the 15 ranges.
	 */
	__be16 rc_range_parameters[DSC_NUM_BUF_RANGES];
	/**
	 * @native_422_420:
	 * PPS88[0] - 0 = Native 4:2:2 not used
	 * 1 = Native 4:2:2 used
	 * PPS88[1] - 0 = Native 4:2:0 not use
	 * 1 = Native 4:2:0 used
	 * PPS88[7:2] - Reserved 6 bits
	 */
	u8 native_422_420;
	/**
	 * @second_line_bpg_offset:
	 * PPS89[4:0] - Additional bits/group budget for the
	 * second line of a slice in Native 4:2:0 mode.
	 * Set to 0 if DSC minor version is 1 or native420 is 0.
	 * PPS89[7:5] - Reserved
	 */
	u8 second_line_bpg_offset;
	/**
	 * @nsl_bpg_offset:
	 * PPS90[7:0], PPS91[7:0] - Number of bits that are deallocated
	 * for each group that is not in the second line of a slice.
	 */
	__be16 nsl_bpg_offset;
	/**
	 * @second_line_offset_adj:
	 * PPS92[7:0], PPS93[7:0] - Used as offset adjustment for the second
	 * line in Native 4:2:0 mode.
	 */
	__be16 second_line_offset_adj;
	/**
	 * @pps_long_94_reserved:
	 * PPS 94, 95, 96, 97 - Reserved
	 */
	u32 pps_long_94_reserved;
	/**
	 * @pps_long_98_reserved:
	 * PPS 98, 99, 100, 101 - Reserved
	 */
	u32 pps_long_98_reserved;
	/**
	 * @pps_long_102_reserved:
	 * PPS 102, 103, 104, 105 - Reserved
	 */
	u32 pps_long_102_reserved;
	/**
	 * @pps_long_106_reserved:
	 * PPS 106, 107, 108, 109 - reserved
	 */
	u32 pps_long_106_reserved;
	/**
	 * @pps_long_110_reserved:
	 * PPS 110, 111, 112, 113 - reserved
	 */
	u32 pps_long_110_reserved;
	/**
	 * @pps_long_114_reserved:
	 * PPS 114 - 117 - reserved
	 */
	u32 pps_long_114_reserved;
	/**
	 * @pps_long_118_reserved:
	 * PPS 118 - 121 - reserved
	 */
	u32 pps_long_118_reserved;
	/**
	 * @pps_long_122_reserved:
	 * PPS 122- 125 - reserved
	 */
	u32 pps_long_122_reserved;
	/**
	 * @pps_short_126_reserved:
	 * PPS 126, 127 - reserved
	 */
	__be16 pps_short_126_reserved;
} __packed;

/**
 * struct drm_dsc_pps_infoframe - DSC infoframe carrying the Picture Parameter
 * Set Metadata
 *
 * This structure represents the DSC PPS infoframe required to send the Picture
 * Parameter Set metadata required before enabling VESA Display Stream
 * Compression. This is based on the DP Secondary Data Packet structure and
 * comprises of SDP Header as defined &struct dp_sdp_header in drm_dp_helper.h
 * and PPS payload defined in &struct drm_dsc_picture_parameter_set.
 *
 * @pps_header: Header for PPS as per DP SDP header format of type
 *              &struct dp_sdp_header
 * @pps_payload: PPS payload fields as per DSC specification Table 4-1
 *               as represented in &struct drm_dsc_picture_parameter_set
 */
struct drm_dsc_pps_infoframe {
	struct dp_sdp_header pps_header;
	struct drm_dsc_picture_parameter_set pps_payload;
} __packed;

#endif /* _DRM_DSC_H_ */
