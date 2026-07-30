/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Dreamchip RPP-X1 ISP Driver - Userspace API
 *
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#ifndef __UAPI_RPP_X1_CONFIG_H
#define __UAPI_RPP_X1_CONFIG_H

#include <linux/media/v4l2-isp.h>

/**
 * struct rppx1_window - Measurement window
 *
 * RPP-X1 measurement window. Different blocks use a window or multiple
 * windows for measurement purposes. This defines a common type for all of
 * them. The number of relevant bits depends on the block where the window is
 * used and is specified in the per-block description
 *
 * @h_offs: horizontal offset from the left of the frame in pixels
 * @v_offs: vertical offset from the top of the frame in pixels
 * @h_size: horizontal size of the window in pixels
 * @v_size: vertical size of the window in pixels
 */
struct rppx1_window {
	__u16 h_offs;
	__u16 v_offs;
	__u16 h_size;
	__u16 v_size;
};

/**
 * enum rppx1_meas_chan - Measurement point for the Histogram and EXM Modules
 *
 * Measurement points for the RPP-X1 Histogram measurement module and Exposure
 * measurement module.
 *
 * All tap points are available for the PRE1/PRE2 pipes. Only
 * RPPX1_MEAS_CHAN_SEL4 and RPPX1_MEAS_CHAN_SEL7 are available for the
 * MAIN_POST pipe.
 *
 * @RPPX1_MEAS_CHAN_SEL0: after input acquisition
 * @RPPX1_MEAS_CHAN_SEL1: after black level subtraction
 * @RPPX1_MEAS_CHAN_SEL2: after sensor gamma linearization
 * @RPPX1_MEAS_CHAN_SEL3: after lens shading correction
 * @RPPX1_MEAS_CHAN_SEL4: after auto white balance gains
 * @RPPX1_MEAS_CHAN_SEL5: after defect pixel correction
 * @RPPX1_MEAS_CHAN_SEL6: after denoise pre-filter
 * @RPPX1_MEAS_CHAN_SEL7: after demosaicing
 */
enum rppx1_meas_chan {
	RPPX1_MEAS_CHAN_SEL0,
	RPPX1_MEAS_CHAN_SEL1,
	RPPX1_MEAS_CHAN_SEL2,
	RPPX1_MEAS_CHAN_SEL3,
	RPPX1_MEAS_CHAN_SEL4,
	RPPX1_MEAS_CHAN_SEL5,
	RPPX1_MEAS_CHAN_SEL6,
	RPPX1_MEAS_CHAN_SEL7,
};

/* ---------------------------------------------------------------------------
 * Parameter Structures
 *
 * The same ISP block might be instantiated in multiple pipeliness and operate
 * on a different bitdepth/precision. For fields of varying length among
 * different instances of the same block, use a data type that can accommodate
 * the larger bitdepth/precision.
 */

/**
 * enum rppx1_params_block_type - RPP-X1 extensible params block types
 *
 * NOTE: Only append to the enumeration as the numbers are uAPI.
 *
 * @RPPX1_PARAMS_BLOCK_TYPE_WBMEAS_POST: AWB Measurement Configuration
 * @RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE1: PRE1 pipe White Balance Gains
 * @RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE2: PRE2 White Balance Gains
 * @RPPX1_PARAMS_BLOCK_TYPE_AWBG_POST: MAIN_POST White Balance Gains
 * @RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE1: PRE1 pipe Exposure Measurement
 * @RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE2: PRE2 pipe Exposure Measurement
 */
enum rppx1_params_block_type {
	RPPX1_PARAMS_BLOCK_TYPE_WBMEAS_POST,
	RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE1,
	RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE2,
	RPPX1_PARAMS_BLOCK_TYPE_AWBG_POST,
	RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE1,
	RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE2,
};

/**
 * enum rppx1_wbmeas_mode - AWB measurement mode
 *
 * @RPPX1_WBMEAS_MODE_YCBCR: YCbCr measurement mode
 * @RPPX1_WBMEAS_MODE_RGB: RGB measurement mode
 */
enum rppx1_wbmeas_mode {
	RPPX1_WBMEAS_MODE_YCBCR,
	RPPX1_WBMEAS_MODE_RGB,
};

/**
 * struct rppx1_wbmeas_params - AWB measurement configuration
 *
 * The Auto-White Balance measurement module is available on the MAIN_POST pipe.
 * It supports two measurement modes, selected by the @mode field. The
 * measurement window is programmed through the @wnd field.
 *
 * To support measurement in YCbCr mode a color conversion matrix with
 * programmable offset is available in the @ccor_coeff and @ccor_offs fields.
 * The color conversion matrix coefficients are represented as 16 bits signed
 * Q4.12 numbers ranging from -8 to +7.99. The per-color channel offsets are
 * represented as 25 bits 2's complement integer numbers ranging from -16777216
 * to +16777215.
 *
 * @header: block header (type = RPPX1_PARAMS_BLOCK_TYPE_WBMEAS_POST)
 * @wnd: measurement window
 * @mode: measurement mode (from enum rppx1_wbmeas_mode)
 * @ymax_cmp: enable Y_MAX compare using @max_y
 * @frames: number of frames for mean value calculation (0 = 1 frame)
 * @reserved: padding
 * @ref_cr_max_r: reference Cr or max red value in RGB mode, 24 bits
 * @ref_cb_max_b: reference Cb or max blue value in RGB mode, 24 bits
 * @min_y_max_g: luminance minimum value or max green value in RGB mode, 24 bits
 * @max_y: luminance maximum value, only valid if @mode is set to YCbCr and
 *	   @ymax_cmp is set to enabled, 24 bits
 * @max_csum: chrominance sum maximum value, 24 bits
 * @min_c: chrominance minimum value, 24 bits
 * @ccor_coeff: coefficients for color conversion matrix, signed 16 bits Q4.6
 * @reserved2: padding
 * @ccor_offs: R-G-B color conversion coefficients, signed 25 bits 2's complement
 * @reserved3: padding
 */
struct rppx1_wbmeas_params {
	struct v4l2_isp_params_block_header header;
	struct rppx1_window wnd;
	__u8 mode;
	__u8 ymax_cmp;
	__u8 frames;
	__u8 reserved;
	__u32 ref_cr_max_r;
	__u32 ref_cb_max_b;
	__u32 min_y_max_g;
	__u32 max_y;
	__u32 max_csum;
	__u32 min_c;
	__u16 ccor_coeff[3][3];
	__u16 reserved2;
	__u32 ccor_offs[3];
	__u32 reserved3;
};

/**
 * struct rppx1_awbg_params  - WB gain configuration
 *
 * The RPP-X1 White Balance Gain module is available in the PRE1 and PRE2
 * pre-fusion pipes and in the MAIN_POST post-fusion pipe. Userspace selects
 * which pipe to operate by setting the @header.type field to
 * RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE1, RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE2
 * or RPPX1_PARAMS_BLOCK_TYPE_AWBG_POST.
 *
 * The White Balance module allows to specify per-color channel gains, expressed
 * as unsigned fixed-point values as 18 bits unsigned integers in Q6.12 format
 * with a maximum of 63.999.
 *
 * @header: block header (type = RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE1 or
 *	    type = RPPX1_PARAMS_BLOCK_TYPE_AWBG_PRE2 or
 *	    type = RPPX1_PARAMS_BLOCK_TYPE_AWBG_POST)
 * @gain_red: gain for red component, 18-bit (unsigned Q6.12)
 * @gain_green_r: gain for green component in red lines, 18-bit (unsigned Q6.12)
 * @gain_blue: gain for blue component, 18-bit (unsigned Q6.12)
 * @gain_green_b: gain for green component in blue lines, 18-bit (unsigned Q6.12)
 */
struct rppx1_awbg_params {
	struct v4l2_isp_params_block_header header;
	__u32 gain_red;
	__u32 gain_green_r;
	__u32 gain_blue;
	__u32 gain_green_b;
};

/**
 * enum rppx1_exm_mode - Exposure measurement mode
 *
 * Exaposure measurement mode selection (RGB/Bayer).
 *
 * @RPPX1_EXP_MEASURING_MODE_DISABLED: no measurement
 * @RPPX1_EXP_MEASURING_MODE_RGB: Y/R/G/B measurement
 * @RPPX1_EXP_MEASURING_MODE_BAYER: Bayer RGB measurement
 */
enum rppx1_exm_mode {
	RPPX1_EXP_MEASURING_MODE_DISABLED,
	RPPX1_EXP_MEASURING_MODE_RGB,
	RPPX1_EXP_MEASURING_MODE_BAYER,
};

/**
 * struct rppx1_exm_params - Exposure measurement configuration
 *
 * The RPP-X1 Exposure measurement unit is available on the PRE1 and PRE2
 * pre-fusion pipes. Userspace selects which pipe to operate by setting
 * the @header.type field to RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE1 or
 * RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE2.
 *
 * Exposure measurement is performed in the RGB or Bayer domain, according to
 * the setting of the @mode field. The exposure measurement tap point is
 * selected according to the value of @channel_sel.
 *
 * The exposure measurement is performed on an input window specified in @wnd.
 * To each color component a programmable weight coefficient is associated.
 * Coefficients are represented as unsigned 8 bits integer values in Q1.7 format
 * ranging from 0 to 1.992.
 *
 * The @last_line fields controls when the exposure measurement completes. It
 * is usually programmed to the value of (@wnd.v_offs + @wnd.v_size + 1).
 *
 * @header: block header (type = RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE1 or
 *	    type = RPPX1_PARAMS_BLOCK_TYPE_EXM_PRE2)
 * @wnd: measurement window coordinates
 * @mode: exposure measure mode (from enum rppx1_exm_mode)
 * @last_line: line number for which the exposure measurement completes
 * @channel_sel: exposure measurement point (see enum rppx1_meas_chan)
 * @coeff_r: coefficient for the red Bayer sample or red color channel, Q1.7
 * @coeff_g_gr: coefficient for the green/red Bayer sample or green color channel, Q1.7
 * @coeff_b: coefficient for the blue Bayer sample or blue color channel, Q1.7
 * @coeff_gb: coefficient for the green/blue Bayer sample, unused in RGB mode, Q1.7
 * @reserved: padding
 */
struct rppx1_exm_params {
	struct v4l2_isp_params_block_header header;
	struct rppx1_window wnd;
	__u32 mode;
	__u32 last_line;
	__u8 channel_sel;
	__u8 coeff_r;
	__u8 coeff_g_gr;
	__u8 coeff_b;
	__u8 coeff_gb;
	__u8 reserved[3];
};

/**
 * RPPX1_PARAMS_MAX_SIZE - Maximum size of all RPP-X1 parameter blocks
 *
 * Some types are reported twice as the same block might be instantiated in
 * multiple pipes.
 */
#define RPPX1_PARAMS_MAX_SIZE						\
	(sizeof(struct rppx1_wbmeas_params)			+	\
	sizeof(struct rppx1_awbg_params)			+	\
	sizeof(struct rppx1_awbg_params)			+	\
	sizeof(struct rppx1_awbg_params)			+	\
	sizeof(struct rppx1_exm_params)				+	\
	sizeof(struct rppx1_exm_params))

/* ---------------------------------------------------------------------------
 * Statistics Structures
 *
 * The same ISP block might be instantiated in multiple pipeliness and operate
 * on a different bitdepth/precision. For fields of varying length among
 * different instances of the same block, use a data type that can accommodate
 * the larger bitdepth/precision.
 */

/**
 * enum rppx1_stats_block_type - RPP-X1 extensible stats block types
 *
 * NOTE: Only append to the enumeration as the numbers are uAPI.
 *
 * @RPPX1_STATS_BLOCK_TYPE_WBMEAS_POST: post-fusion white-balance measurement
 * @RPPX1_STATS_BLOCK_TYPE_EXM_PRE1: pre-fusion pipe1 exposure measurement
 * @RPPX1_STATS_BLOCK_TYPE_EXM_PRE2: pre-fusion pipe2 exposure measurement
 */
enum rppx1_stats_block_type {
	RPPX1_STATS_BLOCK_TYPE_WBMEAS_POST,
	RPPX1_STATS_BLOCK_TYPE_EXM_PRE1,
	RPPX1_STATS_BLOCK_TYPE_EXM_PRE2,
};

/**
 * struct rppx1_wbmeas_stats - AWB statistics
 *
 * @header: block header (type = RPPX1_STATS_BLOCK_TYPE_WBMEAS_POST)
 * @cnt: Number of pixels matched
 * @mean_y_or_g: mean Y (or G in RGB mode) value, 24-bit
 * @mean_cb_or_b: mean Cb (or B in RGB mode) value, 24-bit
 * @mean_cr_or_r: mean Cr (or R in RGB mode) value, 24-bit
 */
struct rppx1_wbmeas_stats {
	struct v4l2_isp_block_header header;
	__u32 cnt;
	__u32 mean_y_or_g;
	__u32 mean_cb_or_b;
	__u32 mean_cr_or_r;
};

/* Exposure Measurement */
#define RPPX1_EXM_NUM_WIN 25

/**
 * struct rppx1_exm_stats - Exposure measurement
 *
 * RPP-X1 exposure measurement calculates the mean value on 25 programmable
 * windows on the input picture.
 *
 * @header: block header (type = RPPX1_STATS_BLOCK_TYPE_EXM_PRE1)
 * @exp_mean: mean luminance values per block, up to 20-bit
 * @reserved: padding
 */
struct rppx1_exm_stats {
	struct v4l2_isp_block_header header;
	__u32 exp_mean[RPPX1_EXM_NUM_WIN];
	__u32 reserved;
};

/**
 * RPPX1_STATS_MAX_SIZE - Maximum size of all RPP-X1 statistics
 *
 * Some types are reported twice as the same block might be instantiated in
 * multiple pipes.
 */
#define RPPX1_STATS_MAX_SIZE						\
	(sizeof(struct rppx1_wbmeas_stats)			+	\
	sizeof(struct rppx1_exm_stats)				+	\
	sizeof(struct rppx1_exm_stats))

#endif /* __UAPI_RPP_X1_CONFIG_H */
