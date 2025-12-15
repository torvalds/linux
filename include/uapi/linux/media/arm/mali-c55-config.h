/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ARM Mali-C55 ISP Driver - Userspace API
 *
 * Copyright (C) 2023 Ideas on Board Oy
 */

#ifndef __UAPI_MALI_C55_CONFIG_H
#define __UAPI_MALI_C55_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/media/v4l2-isp.h>

#define V4L2_CID_MALI_C55_CAPABILITIES	(V4L2_CID_USER_MALI_C55_BASE + 0x0)
#define MALI_C55_GPS_PONG		(1U << 0)
#define MALI_C55_GPS_WDR		(1U << 1)
#define MALI_C55_GPS_COMPRESSION	(1U << 2)
#define MALI_C55_GPS_TEMPER		(1U << 3)
#define MALI_C55_GPS_SINTER_LITE	(1U << 4)
#define MALI_C55_GPS_SINTER		(1U << 5)
#define MALI_C55_GPS_IRIDIX_LTM		(1U << 6)
#define MALI_C55_GPS_IRIDIX_GTM		(1U << 7)
#define MALI_C55_GPS_CNR		(1U << 8)
#define MALI_C55_GPS_FRSCALER		(1U << 9)
#define MALI_C55_GPS_DS_PIPE		(1U << 10)

/*
 * Frames are split into zones of almost equal width and height - a zone is a
 * rectangular tile of a frame. The metering blocks within the ISP collect
 * aggregated statistics per zone. A maximum of 15x15 zones can be configured,
 * and so the statistics buffer within the hardware is sized to accommodate
 * that.
 *
 * The utilised number of zones is runtime configurable.
 */
#define MALI_C55_MAX_ZONES	(15 * 15)

/**
 * struct mali_c55_ae_1024bin_hist - Auto Exposure 1024-bin histogram statistics
 *
 * @bins:	1024 element array of 16-bit pixel counts.
 *
 * The 1024-bin histogram module collects image-global but zone-weighted
 * intensity distributions of pixels in fixed-width bins. The modules can be
 * configured into different "plane modes" which affect the contents of the
 * collected statistics. In plane mode 0, pixel intensities are taken regardless
 * of colour plane into a single 1024-bin histogram with a bin width of 4. In
 * plane mode 1, four 256-bin histograms with a bin width of 16 are collected -
 * one for each CFA colour plane. In plane modes 4, 5, 6 and 7 two 512-bin
 * histograms with a bin width of 8 are collected - in each mode one of the
 * colour planes is collected into the first histogram and all the others are
 * combined into the second. The histograms are stored consecutively in the bins
 * array.
 *
 * The 16-bit pixel counts are stored as a 4-bit exponent in the most
 * significant bits followed by a 12-bit mantissa. Conversion to a usable
 * format can be done according to the following pseudo-code::
 *
 *	if (e == 0) {
 *		bin = m * 2;
 *	} else {
 *		bin = (m + 4096) * 2^e
 *	}
 *
 * where
 *	e is the exponent value in range 0..15
 *	m is the mantissa value in range 0..4095
 *
 * The pixels used in calculating the statistics can be masked using three
 * methods:
 *
 * 1. Pixels can be skipped in X and Y directions independently.
 * 2. Minimum/Maximum intensities can be configured
 * 3. Zones can be differentially weighted, including 0 weighted to mask them
 *
 * The data for this histogram can be collected from different tap points in the
 * ISP depending on configuration - after the white balance or digital gain
 * blocks, or immediately after the input crossbar.
 */
struct mali_c55_ae_1024bin_hist {
	__u16 bins[1024];
} __attribute__((packed));

/**
 * struct mali_c55_ae_5bin_hist - Auto Exposure 5-bin histogram statistics
 *
 * @hist0:	16-bit normalised pixel count for the 0th intensity bin
 * @hist1:	16-bit normalised pixel count for the 1st intensity bin
 * @hist3:	16-bit normalised pixel count for the 3rd intensity bin
 * @hist4:	16-bit normalised pixel count for the 4th intensity bin
 *
 * The ISP generates a 5-bin histogram of normalised pixel counts within bins of
 * pixel intensity for each of 225 possible zones within a frame. The centre bin
 * of the histogram for each zone is not available from the hardware and must be
 * calculated by subtracting the values of hist0, hist1, hist3 and hist4 from
 * 0xffff as in the following equation:
 *
 *	hist2 = 0xffff - (hist0 + hist1 + hist3 + hist4)
 */
struct mali_c55_ae_5bin_hist {
	__u16 hist0;
	__u16 hist1;
	__u16 hist3;
	__u16 hist4;
} __attribute__((packed));

/**
 * struct mali_c55_awb_average_ratios - Auto White Balance colour ratios
 *
 * @avg_rg_gr:	Average R/G or G/R ratio in Q4.8 format.
 * @avg_bg_br:	Average B/G or B/R ratio in Q4.8 format.
 * @num_pixels:	The number of pixels used in the AWB calculation
 *
 * The ISP calculates and collects average colour ratios for each zone in an
 * image and stores them in Q4.8 format (the lowest 8 bits are fractional, with
 * bits [11:8] representing the integer). The exact ratios collected (either
 * R/G, B/G or G/R, B/R) are configurable through the parameters buffer. The
 * value of the 4 high bits is undefined.
 */
struct mali_c55_awb_average_ratios {
	__u16 avg_rg_gr;
	__u16 avg_bg_br;
	__u32 num_pixels;
} __attribute__((packed));

/**
 * struct mali_c55_af_statistics - Auto Focus edge and intensity statistics
 *
 * @intensity_stats:	Packed mantissa and exponent value for pixel intensity
 * @edge_stats:		Packed mantissa and exponent values for edge intensity
 *
 * The ISP collects the squared sum of pixel intensities for each zone within a
 * configurable Region of Interest on the frame. Additionally, the same data are
 * collected after being passed through a bandpass filter which removes high and
 * low frequency components - these are referred to as the edge statistics.
 *
 * The intensity and edge statistics for a zone can be used to calculate the
 * contrast information for a zone
 *
 *	C = E2 / I2
 *
 * Where I2 is the intensity statistic for a zone and E2 is the edge statistic
 * for that zone. Optimum focus is reached when C is at its maximum.
 *
 * The intensity and edge statistics are stored packed into a non-standard 16
 * bit floating point format, where the 7 most significant bits represent the
 * exponent and the 9 least significant bits the mantissa. This format can be
 * unpacked with the following pseudocode::
 *
 *	if (e == 0) {
 *		x = m;
 *	} else {
 *		x = 2^e-1 * (m + 2^9)
 *	}
 *
 * where
 *	e is the exponent value in range 0..127
 *	m is the mantissa value in range 0..511
 */
struct mali_c55_af_statistics {
	__u16 intensity_stats;
	__u16 edge_stats;
} __attribute__((packed));

/**
 * struct mali_c55_stats_buffer - 3A statistics for the mali-c55 ISP
 *
 * @ae_1024bin_hist:		1024-bin frame-global pixel intensity histogram
 * @iridix_1024bin_hist:	Post-Iridix block 1024-bin histogram
 * @ae_5bin_hists:		5-bin pixel intensity histograms for AEC
 * @reserved1:			Undefined buffer space
 * @awb_ratios:			Color balance ratios for Auto White Balance
 * @reserved2:			Undefined buffer space
 * @af_statistics:		Pixel intensity statistics for Auto Focus
 * @reserved3:			Undefined buffer space
 *
 * This struct describes the metering statistics space in the Mali-C55 ISP's
 * hardware in its entirety. The space between each defined area is marked as
 * "unknown" and may not be 0, but should not be used. The @ae_5bin_hists,
 * @awb_ratios and @af_statistics members are arrays of statistics per-zone.
 * The zones are arranged in the array in raster order starting from the top
 * left corner of the image.
 */

struct mali_c55_stats_buffer {
	struct mali_c55_ae_1024bin_hist ae_1024bin_hist;
	struct mali_c55_ae_1024bin_hist iridix_1024bin_hist;
	struct mali_c55_ae_5bin_hist ae_5bin_hists[MALI_C55_MAX_ZONES];
	__u32 reserved1[14];
	struct mali_c55_awb_average_ratios awb_ratios[MALI_C55_MAX_ZONES];
	__u32 reserved2[14];
	struct mali_c55_af_statistics af_statistics[MALI_C55_MAX_ZONES];
	__u32 reserved3[15];
} __attribute__((packed));

/**
 * enum mali_c55_param_block_type - Enumeration of Mali-C55 parameter blocks
 *
 * This enumeration defines the types of Mali-C55 parameters block. Each block
 * configures a specific processing block of the Mali-C55 ISP. The block
 * type allows the driver to correctly interpret the parameters block data.
 *
 * It is the responsibility of userspace to correctly set the type of each
 * parameters block.
 *
 * @MALI_C55_PARAM_BLOCK_SENSOR_OFFS: Sensor pre-shading black level offset
 * @MALI_C55_PARAM_BLOCK_AEXP_HIST: Auto-exposure 1024-bin histogram
 *				    configuration
 * @MALI_C55_PARAM_BLOCK_AEXP_IHIST: Post-Iridix auto-exposure 1024-bin
 *				     histogram configuration
 * @MALI_C55_PARAM_BLOCK_AEXP_HIST_WEIGHTS: Auto-exposure 1024-bin histogram
 *					    weighting
 * @MALI_C55_PARAM_BLOCK_AEXP_IHIST_WEIGHTS: Post-Iridix auto-exposure 1024-bin
 *					     histogram weighting
 * @MALI_C55_PARAM_BLOCK_DIGITAL_GAIN: Digital gain
 * @MALI_C55_PARAM_BLOCK_AWB_GAINS: Auto-white balance gains
 * @MALI_C55_PARAM_BLOCK_AWB_CONFIG: Auto-white balance statistics config
 * @MALI_C55_PARAM_BLOCK_AWB_GAINS_AEXP: Auto-white balance gains for AEXP-0 tap
 * @MALI_C55_PARAM_MESH_SHADING_CONFIG : Mesh shading tables configuration
 * @MALI_C55_PARAM_MESH_SHADING_SELECTION: Mesh shading table selection
 */
enum mali_c55_param_block_type {
	MALI_C55_PARAM_BLOCK_SENSOR_OFFS,
	MALI_C55_PARAM_BLOCK_AEXP_HIST,
	MALI_C55_PARAM_BLOCK_AEXP_IHIST,
	MALI_C55_PARAM_BLOCK_AEXP_HIST_WEIGHTS,
	MALI_C55_PARAM_BLOCK_AEXP_IHIST_WEIGHTS,
	MALI_C55_PARAM_BLOCK_DIGITAL_GAIN,
	MALI_C55_PARAM_BLOCK_AWB_GAINS,
	MALI_C55_PARAM_BLOCK_AWB_CONFIG,
	MALI_C55_PARAM_BLOCK_AWB_GAINS_AEXP,
	MALI_C55_PARAM_MESH_SHADING_CONFIG,
	MALI_C55_PARAM_MESH_SHADING_SELECTION,
};

/**
 * struct mali_c55_params_sensor_off_preshading - offset subtraction for each
 *						  color channel
 *
 * Provides removal of the sensor black level from the sensor data. Separate
 * offsets are provided for each of the four Bayer component color channels
 * which are defaulted to R, Gr, Gb, B.
 *
 * header.type should be set to MALI_C55_PARAM_BLOCK_SENSOR_OFFS from
 * :c:type:`mali_c55_param_block_type` for this block.
 *
 * @header: The Mali-C55 parameters block header
 * @chan00: Offset for color channel 00 (default: R)
 * @chan01: Offset for color channel 01 (default: Gr)
 * @chan10: Offset for color channel 10 (default: Gb)
 * @chan11: Offset for color channel 11 (default: B)
 */
struct mali_c55_params_sensor_off_preshading {
	struct v4l2_isp_params_block_header header;
	__u32 chan00;
	__u32 chan01;
	__u32 chan10;
	__u32 chan11;
};

/**
 * enum mali_c55_aexp_hist_tap_points - Tap points for the AEXP histogram
 * @MALI_C55_AEXP_HIST_TAP_WB: After static white balance
 * @MALI_C55_AEXP_HIST_TAP_FS: After WDR Frame Stitch
 * @MALI_C55_AEXP_HIST_TAP_TPG: After the test pattern generator
 */
enum mali_c55_aexp_hist_tap_points {
	MALI_C55_AEXP_HIST_TAP_WB = 0,
	MALI_C55_AEXP_HIST_TAP_FS,
	MALI_C55_AEXP_HIST_TAP_TPG,
};

/**
 * enum mali_c55_aexp_skip_x - Horizontal pixel skipping
 * @MALI_C55_AEXP_SKIP_X_EVERY_2ND: Collect every 2nd pixel horizontally
 * @MALI_C55_AEXP_SKIP_X_EVERY_3RD: Collect every 3rd pixel horizontally
 * @MALI_C55_AEXP_SKIP_X_EVERY_4TH: Collect every 4th pixel horizontally
 * @MALI_C55_AEXP_SKIP_X_EVERY_5TH: Collect every 5th pixel horizontally
 * @MALI_C55_AEXP_SKIP_X_EVERY_8TH: Collect every 8th pixel horizontally
 * @MALI_C55_AEXP_SKIP_X_EVERY_9TH: Collect every 9th pixel horizontally
 */
enum mali_c55_aexp_skip_x {
	MALI_C55_AEXP_SKIP_X_EVERY_2ND,
	MALI_C55_AEXP_SKIP_X_EVERY_3RD,
	MALI_C55_AEXP_SKIP_X_EVERY_4TH,
	MALI_C55_AEXP_SKIP_X_EVERY_5TH,
	MALI_C55_AEXP_SKIP_X_EVERY_8TH,
	MALI_C55_AEXP_SKIP_X_EVERY_9TH
};

/**
 * enum mali_c55_aexp_skip_y - Vertical pixel skipping
 * @MALI_C55_AEXP_SKIP_Y_ALL: Collect every single pixel vertically
 * @MALI_C55_AEXP_SKIP_Y_EVERY_2ND: Collect every 2nd pixel vertically
 * @MALI_C55_AEXP_SKIP_Y_EVERY_3RD: Collect every 3rd pixel vertically
 * @MALI_C55_AEXP_SKIP_Y_EVERY_4TH: Collect every 4th pixel vertically
 * @MALI_C55_AEXP_SKIP_Y_EVERY_5TH: Collect every 5th pixel vertically
 * @MALI_C55_AEXP_SKIP_Y_EVERY_8TH: Collect every 8th pixel vertically
 * @MALI_C55_AEXP_SKIP_Y_EVERY_9TH: Collect every 9th pixel vertically
 */
enum mali_c55_aexp_skip_y {
	MALI_C55_AEXP_SKIP_Y_ALL,
	MALI_C55_AEXP_SKIP_Y_EVERY_2ND,
	MALI_C55_AEXP_SKIP_Y_EVERY_3RD,
	MALI_C55_AEXP_SKIP_Y_EVERY_4TH,
	MALI_C55_AEXP_SKIP_Y_EVERY_5TH,
	MALI_C55_AEXP_SKIP_Y_EVERY_8TH,
	MALI_C55_AEXP_SKIP_Y_EVERY_9TH
};

/**
 * enum mali_c55_aexp_row_column_offset - Start from the first or second row or
 *					  column
 * @MALI_C55_AEXP_FIRST_ROW_OR_COL:	Start from the first row / column
 * @MALI_C55_AEXP_SECOND_ROW_OR_COL:	Start from the second row / column
 */
enum mali_c55_aexp_row_column_offset {
	MALI_C55_AEXP_FIRST_ROW_OR_COL = 1,
	MALI_C55_AEXP_SECOND_ROW_OR_COL = 2,
};

/**
 * enum mali_c55_aexp_hist_plane_mode - Mode for the AEXP Histograms
 * @MALI_C55_AEXP_HIST_COMBINED: All color planes in one 1024-bin histogram
 * @MALI_C55_AEXP_HIST_SEPARATE: Each color plane in one 256-bin histogram with a bin width of 16
 * @MALI_C55_AEXP_HIST_FOCUS_00: Top left plane in the first bank, rest in second bank
 * @MALI_C55_AEXP_HIST_FOCUS_01: Top right plane in the first bank, rest in second bank
 * @MALI_C55_AEXP_HIST_FOCUS_10: Bottom left plane in the first bank, rest in second bank
 * @MALI_C55_AEXP_HIST_FOCUS_11: Bottom right plane in the first bank, rest in second bank
 *
 * In the "focus" modes statistics are collected into two 512-bin histograms
 * with a bin width of 8. One colour plane is in the first histogram with the
 * remainder combined into the second. The four options represent which of the
 * four positions in a bayer pattern are the focused plane.
 */
enum mali_c55_aexp_hist_plane_mode {
	MALI_C55_AEXP_HIST_COMBINED = 0,
	MALI_C55_AEXP_HIST_SEPARATE = 1,
	MALI_C55_AEXP_HIST_FOCUS_00 = 4,
	MALI_C55_AEXP_HIST_FOCUS_01 = 5,
	MALI_C55_AEXP_HIST_FOCUS_10 = 6,
	MALI_C55_AEXP_HIST_FOCUS_11 = 7,
};

/**
 * struct mali_c55_params_aexp_hist - configuration for AEXP metering hists
 *
 * This struct allows users to configure the 1024-bin AEXP histograms. Broadly
 * speaking the parameters allow you to mask particular regions of the image and
 * to select different kinds of histogram.
 *
 * The skip_x, offset_x, skip_y and offset_y fields allow users to ignore or
 * mask pixels in the frame by their position relative to the top left pixel.
 * First, the skip_y, offset_x and offset_y fields define which of the pixels
 * within each 2x2 region will be counted in the statistics.
 *
 * If skip_y == 0 then two pixels from each covered region will be counted. If
 * both offset_x and offset_y are zero, then the two left-most pixels in each
 * 2x2 pixel region will be counted. Setting offset_x = 1 will discount the top
 * left pixel and count the top right pixel. Setting offset_y = 1 will discount
 * the bottom left pixel and count the bottom right pixel.
 *
 * If skip_y != 0 then only a single pixel from each region covered by the
 * pattern will be counted. In this case offset_x controls whether the pixel
 * that's counted is in the left (if offset_x == 0) or right (if offset_x == 1)
 * column and offset_y controls whether the pixel that's counted is in the top
 * (if offset_y == 0) or bottom (if offset_y == 1) row.
 *
 * The skip_x and skip_y fields control how the 2x2 pixel region is repeated
 * across the image data. The first instance of the region is always in the top
 * left of the image data. The skip_x field controls how many pixels are ignored
 * in the x direction before the pixel masking region is repeated. The skip_y
 * field controls how many pixels are ignored in the y direction before the
 * pixel masking region is repeated.
 *
 * These fields can be used to reduce the number of pixels counted for the
 * statistics, but it's important to be careful to configure them correctly.
 * Some combinations of values will result in colour components from the input
 * data being ignored entirely, for example in the following configuration:
 *
 * skip_x = 0
 * offset_x = 0
 * skip_y = 0
 * offset_y = 0
 *
 * Only the R and Gb components of RGGB data that was input would be collected.
 * Similarly in the following configuration:
 *
 * skip_x = 0
 * offset_x = 0
 * skip_y = 1
 * offset_y = 1
 *
 * Only the Gb component of RGGB data that was input would be collected. To
 * correct things such that all 4 colour components were included it would be
 * necessary to set the skip_x and skip_y fields in a way that resulted in all
 * four colour components being collected:
 *
 * skip_x = 1
 * offset_x = 0
 * skip_y = 1
 * offset_y = 1
 *
 * header.type should be set to one of either MALI_C55_PARAM_BLOCK_AEXP_HIST or
 * MALI_C55_PARAM_BLOCK_AEXP_IHIST from :c:type:`mali_c55_param_block_type`.
 *
 * @header:		The Mali-C55 parameters block header
 * @skip_x:		Horizontal decimation. See enum mali_c55_aexp_skip_x
 * @offset_x:		Skip the first column, or not. See enum mali_c55_aexp_row_column_offset
 * @skip_y:		Vertical decimation. See enum mali_c55_aexp_skip_y
 * @offset_y:		Skip the first row, or not. See enum mali_c55_aexp_row_column_offset
 * @scale_bottom:	Scale pixels in bottom half of intensity range: 0=1x ,1=2x, 2=4x, 4=8x, 4=16x
 * @scale_top:		scale pixels in top half of intensity range: 0=1x ,1=2x, 2=4x, 4=8x, 4=16x
 * @plane_mode:		Plane separation mode. See enum mali_c55_aexp_hist_plane_mode
 * @tap_point:		Tap point for histogram from enum mali_c55_aexp_hist_tap_points.
 *			This parameter is unused for the post-Iridix Histogram
 */
struct mali_c55_params_aexp_hist {
	struct v4l2_isp_params_block_header header;
	__u8 skip_x;
	__u8 offset_x;
	__u8 skip_y;
	__u8 offset_y;
	__u8 scale_bottom;
	__u8 scale_top;
	__u8 plane_mode;
	__u8 tap_point;
};

/**
 * struct mali_c55_params_aexp_weights - Array of weights for AEXP metering
 *
 * This struct allows users to configure the weighting for both of the 1024-bin
 * AEXP histograms. The pixel data collected for each zone is multiplied by the
 * corresponding weight from this array, which may be zero if the intention is
 * to mask off the zone entirely.
 *
 * header.type should be set to one of either MALI_C55_PARAM_BLOCK_AEXP_HIST_WEIGHTS
 * or MALI_C55_PARAM_BLOCK_AEXP_IHIST_WEIGHTS from :c:type:`mali_c55_param_block_type`.
 *
 * @header:		The Mali-C55 parameters block header
 * @nodes_used_horiz:	Number of active zones horizontally [0..15]
 * @nodes_used_vert:	Number of active zones vertically [0..15]
 * @zone_weights:	Zone weighting. Index is row*col where 0,0 is the top
 *			left zone continuing in raster order. Each zone can be
 *			weighted in the range [0..15]. The number of rows and
 *			columns is defined by @nodes_used_vert and
 *			@nodes_used_horiz
 */
struct mali_c55_params_aexp_weights {
	struct v4l2_isp_params_block_header header;
	__u8 nodes_used_horiz;
	__u8 nodes_used_vert;
	__u8 zone_weights[MALI_C55_MAX_ZONES];
};

/**
 * struct mali_c55_params_digital_gain - Digital gain value
 *
 * This struct carries a digital gain value to set in the ISP.
 *
 * header.type should be set to MALI_C55_PARAM_BLOCK_DIGITAL_GAIN from
 * :c:type:`mali_c55_param_block_type` for this block.
 *
 * @header:	The Mali-C55 parameters block header
 * @gain:	The digital gain value to apply, in Q5.8 format.
 */
struct mali_c55_params_digital_gain {
	struct v4l2_isp_params_block_header header;
	__u16 gain;
};

/**
 * enum mali_c55_awb_stats_mode - Statistics mode for AWB
 * @MALI_C55_AWB_MODE_GRBR: Statistics collected as Green/Red and Blue/Red ratios
 * @MALI_C55_AWB_MODE_RGBG: Statistics collected as Red/Green and Blue/Green ratios
 */
enum mali_c55_awb_stats_mode {
	MALI_C55_AWB_MODE_GRBR = 0,
	MALI_C55_AWB_MODE_RGBG,
};

/**
 * struct mali_c55_params_awb_gains - Gain settings for auto white balance
 *
 * This struct allows users to configure the gains for auto-white balance. There
 * are four gain settings corresponding to each colour channel in the bayer
 * domain. Although named generically, the association between the gain applied
 * and the colour channel is done automatically within the ISP depending on the
 * input format, and so the following mapping always holds true::
 *
 *	gain00 = R
 *	gain01 = Gr
 *	gain10 = Gb
 *	gain11 = B
 *
 * All of the gains are stored in Q4.8 format.
 *
 * header.type should be set to one of either MALI_C55_PARAM_BLOCK_AWB_GAINS or
 * MALI_C55_PARAM_BLOCK_AWB_GAINS_AEXP from :c:type:`mali_c55_param_block_type`.
 *
 * @header:	The Mali-C55 parameters block header
 * @gain00:	Multiplier for colour channel 00
 * @gain01:	Multiplier for colour channel 01
 * @gain10:	Multiplier for colour channel 10
 * @gain11:	Multiplier for colour channel 11
 */
struct mali_c55_params_awb_gains {
	struct v4l2_isp_params_block_header header;
	__u16 gain00;
	__u16 gain01;
	__u16 gain10;
	__u16 gain11;
};

/**
 * enum mali_c55_params_awb_tap_points - Tap points for the AWB statistics
 * @MALI_C55_AWB_STATS_TAP_PF: Immediately after the Purple Fringe block
 * @MALI_C55_AWB_STATS_TAP_CNR: Immediately after the CNR block
 */
enum mali_c55_params_awb_tap_points {
	MALI_C55_AWB_STATS_TAP_PF = 0,
	MALI_C55_AWB_STATS_TAP_CNR,
};

/**
 * struct mali_c55_params_awb_config - Stats settings for auto-white balance
 *
 * This struct allows the configuration of the statistics generated for auto
 * white balance. Pixel intensity limits can be set to exclude overly bright or
 * dark regions of an image from the statistics entirely. Colour ratio minima
 * and maxima can be set to discount pixels who's ratios fall outside the
 * defined boundaries; there are two sets of registers to do this - the
 * "min/max" ratios which bound a region and the "high/low" ratios which further
 * trim the upper and lower ratios. For example with the boundaries configured
 * as follows, only pixels whos colour ratios falls into the region marked "A"
 * would be counted::
 *
 *	                                                          cr_high
 *	    2.0 |                                                   |
 *	        |               cb_max --> _________________________v_____
 *	    1.8 |                         |                         \    |
 *	        |                         |                          \   |
 *	    1.6 |                         |                           \  |
 *	        |                         |                            \ |
 *	 c  1.4 |               cb_low -->|\              A             \|<--  cb_high
 *	 b      |                         | \                            |
 *	    1.2 |                         |  \                           |
 *	 r      |                         |   \                          |
 *	 a  1.0 |              cb_min --> |____\_________________________|
 *	 t      |                         ^    ^                         ^
 *	 i  0.8 |                         |    |                         |
 *	 o      |                      cr_min  |                       cr_max
 *	 s  0.6 |                              |
 *	        |                             cr_low
 *	    0.4 |
 *	        |
 *	    0.2 |
 *	        |
 *	    0.0 |_______________________________________________________________
 *	        0.0   0.2   0.4   0.6   0.8   1.0   1.2   1.4   1.6   1.8   2.0
 *	                                   cr ratios
 *
 * header.type should be set to MALI_C55_PARAM_BLOCK_AWB_CONFIG from
 * :c:type:`mali_c55_param_block_type` for this block.
 *
 * @header:		The Mali-C55 parameters block header
 * @tap_point:		The tap point from enum mali_c55_params_awb_tap_points
 * @stats_mode:		AWB statistics collection mode, see :c:type:`mali_c55_awb_stats_mode`
 * @white_level:	Upper pixel intensity (I.E. raw pixel values) limit
 * @black_level:	Lower pixel intensity (I.E. raw pixel values) limit
 * @cr_max:		Maximum R/G ratio (Q4.8 format)
 * @cr_min:		Minimum R/G ratio (Q4.8 format)
 * @cb_max:		Maximum B/G ratio (Q4.8 format)
 * @cb_min:		Minimum B/G ratio (Q4.8 format)
 * @nodes_used_horiz:	Number of active zones horizontally [0..15]
 * @nodes_used_vert:	Number of active zones vertically [0..15]
 * @cr_high:		R/G ratio trim high (Q4.8 format)
 * @cr_low:		R/G ratio trim low (Q4.8 format)
 * @cb_high:		B/G ratio trim high (Q4.8 format)
 * @cb_low:		B/G ratio trim low (Q4.8 format)
 */
struct mali_c55_params_awb_config {
	struct v4l2_isp_params_block_header header;
	__u8 tap_point;
	__u8 stats_mode;
	__u16 white_level;
	__u16 black_level;
	__u16 cr_max;
	__u16 cr_min;
	__u16 cb_max;
	__u16 cb_min;
	__u8 nodes_used_horiz;
	__u8 nodes_used_vert;
	__u16 cr_high;
	__u16 cr_low;
	__u16 cb_high;
	__u16 cb_low;
};

#define MALI_C55_NUM_MESH_SHADING_ELEMENTS 3072

/**
 * struct mali_c55_params_mesh_shading_config - Mesh shading configuration
 *
 * The mesh shading correction module allows programming a separate table of
 * either 16x16 or 32x32 node coefficients for 3 different light sources. The
 * final correction coefficients applied are computed by blending the
 * coefficients from two tables together.
 *
 * A page of 1024 32-bit integers is associated to each colour channel, with
 * pages stored consecutively in memory. Each 32-bit integer packs 3 8-bit
 * correction coefficients for a single node, one for each of the three light
 * sources. The 8 most significant bits are unused. The following table
 * describes the layout::
 *
 *	+----------- Page (Colour Plane) 0 -------------+
 *	| @mesh[i]  | Mesh Point | Bits  | Light Source |
 *	+-----------+------------+-------+--------------+
 *	|         0 |        0,0 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+-----------+------------+-------+--------------+
 *	|         1 |        0,1 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+-----------+------------+-------+--------------+
 *	|       ... |        ... | ...   | ...          |
 *	+-----------+------------+-------+--------------+
 *	|      1023 |      31,31 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+----------- Page (Colour Plane) 1 -------------+
 *	| @mesh[i]  | Mesh Point | Bits  | Light Source |
 *	+-----------+------------+-------+--------------+
 *	|      1024 |        0,0 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+-----------+------------+-------+--------------+
 *	|      1025 |        0,1 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+-----------+------------+-------+--------------+
 *	|       ... |        ... | ...   | ...          |
 *	+-----------+------------+-------+--------------+
 *	|      2047 |      31,31 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+----------- Page (Colour Plane) 2 -------------+
 *	| @mesh[i]  | Mesh Point | Bits  | Light Source |
 *	+-----------+------------+-------+--------------+
 *	|      2048 |        0,0 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+-----------+------------+-------+--------------+
 *	|      2049 |        0,1 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+-----------+------------+-------+--------------+
 *	|       ... |        ... | ...   | ...          |
 *	+-----------+------------+-------+--------------+
 *	|      3071 |      31,31 | 16,23 | LS2          |
 *	|           |            | 08-15 | LS1          |
 *	|           |            | 00-07 | LS0          |
 *	+-----------+------------+-------+--------------+
 *
 * The @mesh_scale member determines the precision and minimum and maximum gain.
 * For example if @mesh_scale is 0 and therefore selects 0 - 2x gain, a value of
 * 0 in a coefficient means 0.0 gain, a value of 128 means 1.0 gain and 255
 * means 2.0 gain.
 *
 * header.type should be set to MALI_C55_PARAM_MESH_SHADING_CONFIG from
 * :c:type:`mali_c55_param_block_type` for this block.
 *
 * @header:		The Mali-C55 parameters block header
 * @mesh_show:		Output the mesh data rather than image data
 * @mesh_scale:		Set the precision and maximum gain range of mesh shading
 *				- 0 = 0-2x gain
 *				- 1 = 0-4x gain
 *				- 2 = 0-8x gain
 *				- 3 = 0-16x gain
 *				- 4 = 1-2x gain
 *				- 5 = 1-3x gain
 *				- 6 = 1-5x gain
 *				- 7 = 1-9x gain
 * @mesh_page_r:	Mesh page select for red colour plane [0..2]
 * @mesh_page_g:	Mesh page select for green colour plane [0..2]
 * @mesh_page_b:	Mesh page select for blue colour plane [0..2]
 * @mesh_width:		Number of horizontal nodes minus 1 [15,31]
 * @mesh_height:	Number of vertical nodes minus 1 [15,31]
 * @mesh:		Mesh shading correction tables
 */
struct mali_c55_params_mesh_shading_config {
	struct v4l2_isp_params_block_header header;
	__u8 mesh_show;
	__u8 mesh_scale;
	__u8 mesh_page_r;
	__u8 mesh_page_g;
	__u8 mesh_page_b;
	__u8 mesh_width;
	__u8 mesh_height;
	__u32 mesh[MALI_C55_NUM_MESH_SHADING_ELEMENTS];
};

/** enum mali_c55_params_mesh_alpha_bank - Mesh shading table bank selection
 * @MALI_C55_MESH_ALPHA_BANK_LS0_AND_LS1 - Select Light Sources 0 and 1
 * @MALI_C55_MESH_ALPHA_BANK_LS1_AND_LS2 - Select Light Sources 1 and 2
 * @MALI_C55_MESH_ALPHA_BANK_LS0_AND_LS2 - Select Light Sources 0 and 2
 */
enum mali_c55_params_mesh_alpha_bank {
	MALI_C55_MESH_ALPHA_BANK_LS0_AND_LS1 = 0,
	MALI_C55_MESH_ALPHA_BANK_LS1_AND_LS2 = 1,
	MALI_C55_MESH_ALPHA_BANK_LS0_AND_LS2 = 4
};

/**
 * struct mali_c55_params_mesh_shading_selection - Mesh table selection
 *
 * The module computes the final correction coefficients by blending the ones
 * from two light source tables, which are selected (independently for each
 * colour channel) by the @mesh_alpha_bank_r/g/b fields.
 *
 * The final blended coefficients for each node are calculated using the
 * following equation:
 *
 *     Final coefficient = (a * LS\ :sub:`b`\ + (256 - a) * LS\ :sub:`a`\) / 256
 *
 * Where a is the @mesh_alpha_r/g/b value, and LS\ :sub:`a`\ and LS\ :sub:`b`\
 * are the node cofficients for the two tables selected by the
 * @mesh_alpha_bank_r/g/b value.
 *
 * The scale of the applied correction may also be controlled by tuning the
 * @mesh_strength member. This is a modifier to the final coefficients which can
 * be used to globally reduce the gains applied.
 *
 * header.type should be set to MALI_C55_PARAM_MESH_SHADING_SELECTION from
 * :c:type:`mali_c55_param_block_type` for this block.
 *
 * @header:		The Mali-C55 parameters block header
 * @mesh_alpha_bank_r:	Red mesh table select (c:type:`enum mali_c55_params_mesh_alpha_bank`)
 * @mesh_alpha_bank_g:	Green mesh table select (c:type:`enum mali_c55_params_mesh_alpha_bank`)
 * @mesh_alpha_bank_b:	Blue mesh table select (c:type:`enum mali_c55_params_mesh_alpha_bank`)
 * @mesh_alpha_r:	Blend coefficient for R [0..255]
 * @mesh_alpha_g:	Blend coefficient for G [0..255]
 * @mesh_alpha_b:	Blend coefficient for B [0..255]
 * @mesh_strength:	Mesh strength in Q4.12 format [0..4096]
 */
struct mali_c55_params_mesh_shading_selection {
	struct v4l2_isp_params_block_header header;
	__u8 mesh_alpha_bank_r;
	__u8 mesh_alpha_bank_g;
	__u8 mesh_alpha_bank_b;
	__u8 mesh_alpha_r;
	__u8 mesh_alpha_g;
	__u8 mesh_alpha_b;
	__u16 mesh_strength;
};

/**
 * define MALI_C55_PARAMS_MAX_SIZE - Maximum size of all Mali C55 Parameters
 *
 * Though the parameters for the Mali-C55 are passed as optional blocks, the
 * driver still needs to know the absolute maximum size so that it can allocate
 * a buffer sized appropriately to accommodate userspace attempting to set all
 * possible parameters in a single frame.
 *
 * Some structs are in this list multiple times. Where that's the case, it just
 * reflects the fact that the same struct can be used with multiple different
 * header types from :c:type:`mali_c55_param_block_type`.
 */
#define MALI_C55_PARAMS_MAX_SIZE				\
	(sizeof(struct mali_c55_params_sensor_off_preshading) +	\
	sizeof(struct mali_c55_params_aexp_hist) +		\
	sizeof(struct mali_c55_params_aexp_weights) +		\
	sizeof(struct mali_c55_params_aexp_hist) +		\
	sizeof(struct mali_c55_params_aexp_weights) +		\
	sizeof(struct mali_c55_params_digital_gain) +		\
	sizeof(struct mali_c55_params_awb_gains) +		\
	sizeof(struct mali_c55_params_awb_config) +		\
	sizeof(struct mali_c55_params_awb_gains) +		\
	sizeof(struct mali_c55_params_mesh_shading_config) +	\
	sizeof(struct mali_c55_params_mesh_shading_selection))

#endif /* __UAPI_MALI_C55_CONFIG_H */
