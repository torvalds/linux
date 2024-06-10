/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Rockchip ISP1 userspace API
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKISP1_CONFIG_H
#define _UAPI_RKISP1_CONFIG_H

#include <linux/types.h>

/* Defect Pixel Cluster Detection */
#define RKISP1_CIF_ISP_MODULE_DPCC		(1U << 0)
/* Black Level Subtraction */
#define RKISP1_CIF_ISP_MODULE_BLS		(1U << 1)
/* Sensor De-gamma */
#define RKISP1_CIF_ISP_MODULE_SDG		(1U << 2)
/* Histogram statistics configuration */
#define RKISP1_CIF_ISP_MODULE_HST		(1U << 3)
/* Lens Shade Control */
#define RKISP1_CIF_ISP_MODULE_LSC		(1U << 4)
/* Auto White Balance Gain */
#define RKISP1_CIF_ISP_MODULE_AWB_GAIN		(1U << 5)
/* Filter */
#define RKISP1_CIF_ISP_MODULE_FLT		(1U << 6)
/* Bayer Demosaic */
#define RKISP1_CIF_ISP_MODULE_BDM		(1U << 7)
/* Cross Talk */
#define RKISP1_CIF_ISP_MODULE_CTK		(1U << 8)
/* Gamma Out Curve */
#define RKISP1_CIF_ISP_MODULE_GOC		(1U << 9)
/* Color Processing */
#define RKISP1_CIF_ISP_MODULE_CPROC		(1U << 10)
/* Auto Focus Control statistics configuration */
#define RKISP1_CIF_ISP_MODULE_AFC		(1U << 11)
/* Auto White Balancing statistics configuration */
#define RKISP1_CIF_ISP_MODULE_AWB		(1U << 12)
/* Image Effect */
#define RKISP1_CIF_ISP_MODULE_IE		(1U << 13)
/* Auto Exposure Control statistics configuration */
#define RKISP1_CIF_ISP_MODULE_AEC		(1U << 14)
/* Wide Dynamic Range */
#define RKISP1_CIF_ISP_MODULE_WDR		(1U << 15)
/* Denoise Pre-Filter */
#define RKISP1_CIF_ISP_MODULE_DPF		(1U << 16)
/* Denoise Pre-Filter Strength */
#define RKISP1_CIF_ISP_MODULE_DPF_STRENGTH	(1U << 17)

#define RKISP1_CIF_ISP_CTK_COEFF_MAX            0x100
#define RKISP1_CIF_ISP_CTK_OFFSET_MAX           0x800

#define RKISP1_CIF_ISP_AE_MEAN_MAX_V10		25
#define RKISP1_CIF_ISP_AE_MEAN_MAX_V12		81
#define RKISP1_CIF_ISP_AE_MEAN_MAX		RKISP1_CIF_ISP_AE_MEAN_MAX_V12

#define RKISP1_CIF_ISP_HIST_BIN_N_MAX_V10	16
#define RKISP1_CIF_ISP_HIST_BIN_N_MAX_V12	32
#define RKISP1_CIF_ISP_HIST_BIN_N_MAX		RKISP1_CIF_ISP_HIST_BIN_N_MAX_V12

#define RKISP1_CIF_ISP_AFM_MAX_WINDOWS          3
#define RKISP1_CIF_ISP_DEGAMMA_CURVE_SIZE       17

#define RKISP1_CIF_ISP_BDM_MAX_TH               0xff

/*
 * Black level compensation
 */
/* maximum value for horizontal start address */
#define RKISP1_CIF_ISP_BLS_START_H_MAX             0x00000fff
/* maximum value for horizontal stop address */
#define RKISP1_CIF_ISP_BLS_STOP_H_MAX              0x00000fff
/* maximum value for vertical start address */
#define RKISP1_CIF_ISP_BLS_START_V_MAX             0x00000fff
/* maximum value for vertical stop address */
#define RKISP1_CIF_ISP_BLS_STOP_V_MAX              0x00000fff
/* maximum is 2^18 = 262144*/
#define RKISP1_CIF_ISP_BLS_SAMPLES_MAX             0x00000012
/* maximum value for fixed black level */
#define RKISP1_CIF_ISP_BLS_FIX_SUB_MAX             0x00000fff
/* minimum value for fixed black level */
#define RKISP1_CIF_ISP_BLS_FIX_SUB_MIN             0xfffff000
/* 13 bit range (signed)*/
#define RKISP1_CIF_ISP_BLS_FIX_MASK                0x00001fff

/*
 * Automatic white balance measurements
 */
#define RKISP1_CIF_ISP_AWB_MAX_GRID                1
#define RKISP1_CIF_ISP_AWB_MAX_FRAMES              7

/*
 * Gamma out
 */
/* Maximum number of color samples supported */
#define RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V10   17
#define RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V12   34
#define RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES       RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V12

/*
 * Lens shade correction
 */
#define RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE        8

/*
 * The following matches the tuning process,
 * not the max capabilities of the chip.
 */
#define RKISP1_CIF_ISP_LSC_SAMPLES_MAX             17

/*
 * Histogram calculation
 */
#define RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE_V10 25
#define RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE_V12 81
#define RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE     RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE_V12

/*
 * Defect Pixel Cluster Correction
 */
#define RKISP1_CIF_ISP_DPCC_METHODS_MAX				3

#define RKISP1_CIF_ISP_DPCC_MODE_STAGE1_ENABLE			(1U << 2)

#define RKISP1_CIF_ISP_DPCC_OUTPUT_MODE_STAGE1_INCL_G_CENTER	(1U << 0)
#define RKISP1_CIF_ISP_DPCC_OUTPUT_MODE_STAGE1_INCL_RB_CENTER	(1U << 1)
#define RKISP1_CIF_ISP_DPCC_OUTPUT_MODE_STAGE1_G_3X3		(1U << 2)
#define RKISP1_CIF_ISP_DPCC_OUTPUT_MODE_STAGE1_RB_3X3		(1U << 3)

/* 0-2 for sets 1-3 */
#define RKISP1_CIF_ISP_DPCC_SET_USE_STAGE1_USE_SET(n)		((n) << 0)
#define RKISP1_CIF_ISP_DPCC_SET_USE_STAGE1_USE_FIX_SET		(1U << 3)

#define RKISP1_CIF_ISP_DPCC_METHODS_SET_PG_GREEN_ENABLE		(1U << 0)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_LC_GREEN_ENABLE		(1U << 1)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_RO_GREEN_ENABLE		(1U << 2)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_RND_GREEN_ENABLE	(1U << 3)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_RG_GREEN_ENABLE		(1U << 4)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_PG_RED_BLUE_ENABLE	(1U << 8)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_LC_RED_BLUE_ENABLE	(1U << 9)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_RO_RED_BLUE_ENABLE	(1U << 10)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_RND_RED_BLUE_ENABLE	(1U << 11)
#define RKISP1_CIF_ISP_DPCC_METHODS_SET_RG_RED_BLUE_ENABLE	(1U << 12)

#define RKISP1_CIF_ISP_DPCC_LINE_THRESH_G(v)			((v) << 0)
#define RKISP1_CIF_ISP_DPCC_LINE_THRESH_RB(v)			((v) << 8)
#define RKISP1_CIF_ISP_DPCC_LINE_MAD_FAC_G(v)			((v) << 0)
#define RKISP1_CIF_ISP_DPCC_LINE_MAD_FAC_RB(v)			((v) << 8)
#define RKISP1_CIF_ISP_DPCC_PG_FAC_G(v)				((v) << 0)
#define RKISP1_CIF_ISP_DPCC_PG_FAC_RB(v)			((v) << 8)
#define RKISP1_CIF_ISP_DPCC_RND_THRESH_G(v)			((v) << 0)
#define RKISP1_CIF_ISP_DPCC_RND_THRESH_RB(v)			((v) << 8)
#define RKISP1_CIF_ISP_DPCC_RG_FAC_G(v)				((v) << 0)
#define RKISP1_CIF_ISP_DPCC_RG_FAC_RB(v)			((v) << 8)

#define RKISP1_CIF_ISP_DPCC_RO_LIMITS_n_G(n, v)			((v) << ((n) * 4))
#define RKISP1_CIF_ISP_DPCC_RO_LIMITS_n_RB(n, v)		((v) << ((n) * 4 + 2))

#define RKISP1_CIF_ISP_DPCC_RND_OFFS_n_G(n, v)			((v) << ((n) * 4))
#define RKISP1_CIF_ISP_DPCC_RND_OFFS_n_RB(n, v)			((v) << ((n) * 4 + 2))

/*
 * Denoising pre filter
 */
#define RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS      17
#define RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS  6

/*
 * Measurement types
 */
#define RKISP1_CIF_ISP_STAT_AWB           (1U << 0)
#define RKISP1_CIF_ISP_STAT_AUTOEXP       (1U << 1)
#define RKISP1_CIF_ISP_STAT_AFM           (1U << 2)
#define RKISP1_CIF_ISP_STAT_HIST          (1U << 3)

/**
 * enum rkisp1_cif_isp_version - ISP variants
 *
 * @RKISP1_V10: Used at least in RK3288 and RK3399.
 * @RKISP1_V11: Declared in the original vendor code, but not used. Same number
 *	of entries in grids and histogram as v10.
 * @RKISP1_V12: Used at least in RK3326 and PX30.
 * @RKISP1_V13: Used at least in RK1808. Same number of entries in grids and
 *	histogram as v12.
 * @RKISP1_V_IMX8MP: Used in at least i.MX8MP. Same number of entries in grids
 *	and histogram as v10.
 */
enum rkisp1_cif_isp_version {
	RKISP1_V10 = 10,
	RKISP1_V11,
	RKISP1_V12,
	RKISP1_V13,
	RKISP1_V_IMX8MP,
};

enum rkisp1_cif_isp_histogram_mode {
	RKISP1_CIF_ISP_HISTOGRAM_MODE_DISABLE,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_RGB_COMBINED,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_R_HISTOGRAM,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_G_HISTOGRAM,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_B_HISTOGRAM,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_Y_HISTOGRAM
};

enum rkisp1_cif_isp_awb_mode_type {
	RKISP1_CIF_ISP_AWB_MODE_MANUAL,
	RKISP1_CIF_ISP_AWB_MODE_RGB,
	RKISP1_CIF_ISP_AWB_MODE_YCBCR
};

enum rkisp1_cif_isp_flt_mode {
	RKISP1_CIF_ISP_FLT_STATIC_MODE,
	RKISP1_CIF_ISP_FLT_DYNAMIC_MODE
};

/**
 * enum rkisp1_cif_isp_exp_ctrl_autostop - stop modes
 * @RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_0: continuous measurement
 * @RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_1: stop measuring after a complete frame
 */
enum rkisp1_cif_isp_exp_ctrl_autostop {
	RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_0 = 0,
	RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_1 = 1,
};

/**
 * enum rkisp1_cif_isp_exp_meas_mode - Exposure measure mode
 * @RKISP1_CIF_ISP_EXP_MEASURING_MODE_0: Y = 16 + 0.25R + 0.5G + 0.1094B
 * @RKISP1_CIF_ISP_EXP_MEASURING_MODE_1: Y = (R + G + B) x (85/256)
 */
enum rkisp1_cif_isp_exp_meas_mode {
	RKISP1_CIF_ISP_EXP_MEASURING_MODE_0,
	RKISP1_CIF_ISP_EXP_MEASURING_MODE_1,
};

/*---------- PART1: Input Parameters ------------*/

/**
 * struct rkisp1_cif_isp_window -  measurement window.
 *
 * Measurements are calculated per window inside the frame.
 * This struct represents a window for a measurement.
 *
 * @h_offs: the horizontal offset of the window from the left of the frame in pixels.
 * @v_offs: the vertical offset of the window from the top of the frame in pixels.
 * @h_size: the horizontal size of the window in pixels
 * @v_size: the vertical size of the window in pixels.
 */
struct rkisp1_cif_isp_window {
	__u16 h_offs;
	__u16 v_offs;
	__u16 h_size;
	__u16 v_size;
};

/**
 * struct rkisp1_cif_isp_bls_fixed_val - BLS fixed subtraction values
 *
 * The values will be subtracted from the sensor
 * values. Therefore a negative value means addition instead of subtraction!
 *
 * @r: Fixed (signed!) subtraction value for Bayer pattern R
 * @gr: Fixed (signed!) subtraction value for Bayer pattern Gr
 * @gb: Fixed (signed!) subtraction value for Bayer pattern Gb
 * @b: Fixed (signed!) subtraction value for Bayer pattern B
 */
struct rkisp1_cif_isp_bls_fixed_val {
	__s16 r;
	__s16 gr;
	__s16 gb;
	__s16 b;
};

/**
 * struct rkisp1_cif_isp_bls_config - Configuration used by black level subtraction
 *
 * @enable_auto: Automatic mode activated means that the measured values
 *		 are subtracted. Otherwise the fixed subtraction
 *		 values will be subtracted.
 * @en_windows: enabled window
 * @bls_window1: Measurement window 1 size
 * @bls_window2: Measurement window 2 size
 * @bls_samples: Set amount of measured pixels for each Bayer position
 *		 (A, B,C and D) to 2^bls_samples.
 * @fixed_val: Fixed subtraction values
 */
struct rkisp1_cif_isp_bls_config {
	__u8 enable_auto;
	__u8 en_windows;
	struct rkisp1_cif_isp_window bls_window1;
	struct rkisp1_cif_isp_window bls_window2;
	__u8 bls_samples;
	struct rkisp1_cif_isp_bls_fixed_val fixed_val;
};

/**
 * struct rkisp1_cif_isp_dpcc_methods_config - DPCC methods set configuration
 *
 * This structure stores the configuration of one set of methods for the DPCC
 * algorithm. Multiple methods can be selected in each set (independently for
 * the Green and Red/Blue components) through the @method field, the result is
 * the logical AND of all enabled methods. The remaining fields set thresholds
 * and factors for each method.
 *
 * @method: Method enable bits (RKISP1_CIF_ISP_DPCC_METHODS_SET_*)
 * @line_thresh: Line threshold (RKISP1_CIF_ISP_DPCC_LINE_THRESH_*)
 * @line_mad_fac: Line Mean Absolute Difference factor (RKISP1_CIF_ISP_DPCC_LINE_MAD_FAC_*)
 * @pg_fac: Peak gradient factor (RKISP1_CIF_ISP_DPCC_PG_FAC_*)
 * @rnd_thresh: Rank Neighbor Difference threshold (RKISP1_CIF_ISP_DPCC_RND_THRESH_*)
 * @rg_fac: Rank gradient factor (RKISP1_CIF_ISP_DPCC_RG_FAC_*)
 */
struct rkisp1_cif_isp_dpcc_methods_config {
	__u32 method;
	__u32 line_thresh;
	__u32 line_mad_fac;
	__u32 pg_fac;
	__u32 rnd_thresh;
	__u32 rg_fac;
};

/**
 * struct rkisp1_cif_isp_dpcc_config - Configuration used by DPCC
 *
 * Configuration used by Defect Pixel Cluster Correction. Three sets of methods
 * can be configured and selected through the @set_use field. The result is the
 * logical OR of all enabled sets.
 *
 * @mode: DPCC mode (RKISP1_CIF_ISP_DPCC_MODE_*)
 * @output_mode: Interpolation output mode (RKISP1_CIF_ISP_DPCC_OUTPUT_MODE_*)
 * @set_use: Methods sets selection (RKISP1_CIF_ISP_DPCC_SET_USE_*)
 * @methods: Methods sets configuration
 * @ro_limits: Rank order limits (RKISP1_CIF_ISP_DPCC_RO_LIMITS_*)
 * @rnd_offs: Differential rank offsets for rank neighbor difference (RKISP1_CIF_ISP_DPCC_RND_OFFS_*)
 */
struct rkisp1_cif_isp_dpcc_config {
	__u32 mode;
	__u32 output_mode;
	__u32 set_use;
	struct rkisp1_cif_isp_dpcc_methods_config methods[RKISP1_CIF_ISP_DPCC_METHODS_MAX];
	__u32 ro_limits;
	__u32 rnd_offs;
};

/**
 * struct rkisp1_cif_isp_gamma_corr_curve - gamma curve point definition y-axis (output).
 *
 * The reset values define a linear curve which has the same effect as bypass. Reset values are:
 * gamma_y[0] = 0x0000, gamma_y[1] = 0x0100, ... gamma_y[15] = 0x0f00, gamma_y[16] = 0xfff
 *
 * @gamma_y: the values for the y-axis of gamma curve points. Each value is 12 bit.
 */
struct rkisp1_cif_isp_gamma_corr_curve {
	__u16 gamma_y[RKISP1_CIF_ISP_DEGAMMA_CURVE_SIZE];
};

/**
 * struct rkisp1_cif_isp_gamma_curve_x_axis_pnts - De-Gamma Curve definition x increments
 *		(sampling points). gamma_dx0 is for the lower samples (1-8), gamma_dx1 is for the
 *		higher samples (9-16). The reset values for both fields is 0x44444444. This means
 *		that each sample is 4 units away from the previous one on the x-axis.
 *
 * @gamma_dx0: gamma curve sample points definitions. Bits 0:2 for sample 1. Bit 3 unused.
 *		Bits 4:6 for sample 2. bit 7 unused ... Bits 28:30 for sample 8. Bit 31 unused
 * @gamma_dx1: gamma curve sample points definitions. Bits 0:2 for sample 9. Bit 3 unused.
 *		Bits 4:6 for sample 10. bit 7 unused ... Bits 28:30 for sample 16. Bit 31 unused
 */
struct rkisp1_cif_isp_gamma_curve_x_axis_pnts {
	__u32 gamma_dx0;
	__u32 gamma_dx1;
};

/**
 * struct rkisp1_cif_isp_sdg_config - Configuration used by sensor degamma
 *
 * @curve_r: gamma curve point definition axis for red
 * @curve_g: gamma curve point definition axis for green
 * @curve_b: gamma curve point definition axis for blue
 * @xa_pnts: x axis increments
 */
struct rkisp1_cif_isp_sdg_config {
	struct rkisp1_cif_isp_gamma_corr_curve curve_r;
	struct rkisp1_cif_isp_gamma_corr_curve curve_g;
	struct rkisp1_cif_isp_gamma_corr_curve curve_b;
	struct rkisp1_cif_isp_gamma_curve_x_axis_pnts xa_pnts;
};

/**
 * struct rkisp1_cif_isp_lsc_config - Configuration used by Lens shading correction
 *
 * @r_data_tbl: sample table red
 * @gr_data_tbl: sample table green (red)
 * @gb_data_tbl: sample table green (blue)
 * @b_data_tbl: sample table blue
 * @x_grad_tbl: gradient table x
 * @y_grad_tbl: gradient table y
 * @x_size_tbl: size table x
 * @y_size_tbl: size table y
 * @config_width: not used at the moment
 * @config_height: not used at the moment
 */
struct rkisp1_cif_isp_lsc_config {
	__u16 r_data_tbl[RKISP1_CIF_ISP_LSC_SAMPLES_MAX][RKISP1_CIF_ISP_LSC_SAMPLES_MAX];
	__u16 gr_data_tbl[RKISP1_CIF_ISP_LSC_SAMPLES_MAX][RKISP1_CIF_ISP_LSC_SAMPLES_MAX];
	__u16 gb_data_tbl[RKISP1_CIF_ISP_LSC_SAMPLES_MAX][RKISP1_CIF_ISP_LSC_SAMPLES_MAX];
	__u16 b_data_tbl[RKISP1_CIF_ISP_LSC_SAMPLES_MAX][RKISP1_CIF_ISP_LSC_SAMPLES_MAX];

	__u16 x_grad_tbl[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	__u16 y_grad_tbl[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];

	__u16 x_size_tbl[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	__u16 y_size_tbl[RKISP1_CIF_ISP_LSC_SECTORS_TBL_SIZE];
	__u16 config_width;
	__u16 config_height;
};

/**
 * struct rkisp1_cif_isp_ie_config - Configuration used by image effects
 *
 * @effect: values from 'enum v4l2_colorfx'. Possible values are: V4L2_COLORFX_SEPIA,
 *		V4L2_COLORFX_SET_CBCR, V4L2_COLORFX_AQUA, V4L2_COLORFX_EMBOSS,
 *		V4L2_COLORFX_SKETCH,   V4L2_COLORFX_BW,   V4L2_COLORFX_NEGATIVE
 * @color_sel: bits 0:2 - colors bitmask (001 - blue, 010 - green, 100 - red).
 *		bits 8:15 - Threshold value of the RGB colors for the color selection effect.
 * @eff_mat_1: 3x3 Matrix Coefficients for Emboss Effect 1
 * @eff_mat_2: 3x3 Matrix Coefficients for Emboss Effect 2
 * @eff_mat_3: 3x3 Matrix Coefficients for Emboss 3/Sketch 1
 * @eff_mat_4: 3x3 Matrix Coefficients for Sketch Effect 2
 * @eff_mat_5: 3x3 Matrix Coefficients for Sketch Effect 3
 * @eff_tint: Chrominance increment values of tint (used for sepia effect)
 */
struct rkisp1_cif_isp_ie_config {
	__u16 effect;
	__u16 color_sel;
	__u16 eff_mat_1;
	__u16 eff_mat_2;
	__u16 eff_mat_3;
	__u16 eff_mat_4;
	__u16 eff_mat_5;
	__u16 eff_tint;
};

/**
 * struct rkisp1_cif_isp_cproc_config - Configuration used by Color Processing
 *
 * @c_out_range: Chrominance pixel clipping range at output.
 *		 (0 for limit, 1 for full)
 * @y_in_range: Luminance pixel clipping range at output.
 * @y_out_range: Luminance pixel clipping range at output.
 * @contrast: 00~ff, 0.0~1.992
 * @brightness: 80~7F, -128~+127
 * @sat: saturation, 00~FF, 0.0~1.992
 * @hue: 80~7F, -90~+87.188
 */
struct rkisp1_cif_isp_cproc_config {
	__u8 c_out_range;
	__u8 y_in_range;
	__u8 y_out_range;
	__u8 contrast;
	__u8 brightness;
	__u8 sat;
	__u8 hue;
};

/**
 * struct rkisp1_cif_isp_awb_meas_config - Configuration for the AWB statistics
 *
 * @awb_mode: the awb meas mode. From enum rkisp1_cif_isp_awb_mode_type.
 * @awb_wnd: white balance measurement window (in pixels)
 * @max_y: only pixels values < max_y contribute to awb measurement, set to 0
 *	   to disable this feature
 * @min_y: only pixels values > min_y contribute to awb measurement
 * @max_csum: Chrominance sum maximum value, only consider pixels with Cb+Cr,
 *	      smaller than threshold for awb measurements
 * @min_c: Chrominance minimum value, only consider pixels with Cb/Cr
 *	   each greater than threshold value for awb measurements
 * @frames: number of frames - 1 used for mean value calculation
 *	    (ucFrames=0 means 1 Frame)
 * @awb_ref_cr: reference Cr value for AWB regulation, target for AWB
 * @awb_ref_cb: reference Cb value for AWB regulation, target for AWB
 * @enable_ymax_cmp: enable Y_MAX compare (Not valid in RGB measurement mode.)
 */
struct rkisp1_cif_isp_awb_meas_config {
	/*
	 * Note: currently the h and v offsets are mapped to grid offsets
	 */
	struct rkisp1_cif_isp_window awb_wnd;
	__u32 awb_mode;
	__u8 max_y;
	__u8 min_y;
	__u8 max_csum;
	__u8 min_c;
	__u8 frames;
	__u8 awb_ref_cr;
	__u8 awb_ref_cb;
	__u8 enable_ymax_cmp;
};

/**
 * struct rkisp1_cif_isp_awb_gain_config - Configuration used by auto white balance gain
 *
 * All fields in this struct are 10 bit, where:
 * 0x100h = 1, unsigned integer value, range 0 to 4 with 8 bit fractional part.
 *
 * out_data_x = ( AWB_GAIN_X * in_data + 128) >> 8
 *
 * @gain_red: gain value for red component.
 * @gain_green_r: gain value for green component in red line.
 * @gain_blue: gain value for blue component.
 * @gain_green_b: gain value for green component in blue line.
 */
struct rkisp1_cif_isp_awb_gain_config {
	__u16 gain_red;
	__u16 gain_green_r;
	__u16 gain_blue;
	__u16 gain_green_b;
};

/**
 * struct rkisp1_cif_isp_flt_config - Configuration used by ISP filtering
 *
 * All 4 threshold fields (thresh_*) are 10 bits.
 * All 6 factor fields (fac_*) are 6 bits.
 *
 * @mode: ISP_FILT_MODE register fields (from enum rkisp1_cif_isp_flt_mode)
 * @grn_stage1: Green filter stage 1 select (range 0x0...0x8)
 * @chr_h_mode: Chroma filter horizontal mode
 * @chr_v_mode: Chroma filter vertical mode
 * @thresh_bl0: If thresh_bl1 < sum_grad < thresh_bl0 then fac_bl0 is selected (blurring th)
 * @thresh_bl1: If sum_grad < thresh_bl1 then fac_bl1 is selected (blurring th)
 * @thresh_sh0: If thresh_sh0 < sum_grad < thresh_sh1 then thresh_sh0 is selected (sharpening th)
 * @thresh_sh1: If thresh_sh1 < sum_grad then thresh_sh1 is selected (sharpening th)
 * @lum_weight: Parameters for luminance weight function.
 * @fac_sh1: filter factor for sharp1 level
 * @fac_sh0: filter factor for sharp0 level
 * @fac_mid: filter factor for mid level and for static filter mode
 * @fac_bl0: filter factor for blur 0 level
 * @fac_bl1: filter factor for blur 1 level (max blur)
 */
struct rkisp1_cif_isp_flt_config {
	__u32 mode;
	__u8 grn_stage1;
	__u8 chr_h_mode;
	__u8 chr_v_mode;
	__u32 thresh_bl0;
	__u32 thresh_bl1;
	__u32 thresh_sh0;
	__u32 thresh_sh1;
	__u32 lum_weight;
	__u32 fac_sh1;
	__u32 fac_sh0;
	__u32 fac_mid;
	__u32 fac_bl0;
	__u32 fac_bl1;
};

/**
 * struct rkisp1_cif_isp_bdm_config - Configuration used by Bayer DeMosaic
 *
 * @demosaic_th: threshold for bayer demosaicing texture detection
 */
struct rkisp1_cif_isp_bdm_config {
	__u8 demosaic_th;
};

/**
 * struct rkisp1_cif_isp_ctk_config - Configuration used by Cross Talk correction
 *
 * @coeff: color correction matrix. Values are 11-bit signed fixed-point numbers with 4 bit integer
 *		and 7 bit fractional part, ranging from -8 (0x400) to +7.992 (0x3FF). 0 is
 *		represented by 0x000 and a coefficient value of 1 as 0x080.
 * @ct_offset: Red, Green, Blue offsets for the crosstalk correction matrix
 */
struct rkisp1_cif_isp_ctk_config {
	__u16 coeff[3][3];
	__u16 ct_offset[3];
};

enum rkisp1_cif_isp_goc_mode {
	RKISP1_CIF_ISP_GOC_MODE_LOGARITHMIC,
	RKISP1_CIF_ISP_GOC_MODE_EQUIDISTANT
};

/**
 * struct rkisp1_cif_isp_goc_config - Configuration used by Gamma Out correction
 *
 * @mode: goc mode (from enum rkisp1_cif_isp_goc_mode)
 * @gamma_y: gamma out curve y-axis for all color components
 *
 * The number of entries of @gamma_y depends on the hardware revision
 * as is reported by the hw_revision field of the struct media_device_info
 * that is returned by ioctl MEDIA_IOC_DEVICE_INFO.
 *
 * V10 has RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V10 entries, V12 has
 * RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES_V12 entries.
 * RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES is equal to the maximum of the two.
 */
struct rkisp1_cif_isp_goc_config {
	__u32 mode;
	__u16 gamma_y[RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES];
};

/**
 * struct rkisp1_cif_isp_hst_config - Configuration for Histogram statistics
 *
 * @mode: histogram mode (from enum rkisp1_cif_isp_histogram_mode)
 * @histogram_predivider: process every stepsize pixel, all other pixels are
 *			  skipped
 * @meas_window: coordinates of the measure window
 * @hist_weight: weighting factor for sub-windows
 *
 * The number of entries of @hist_weight depends on the hardware revision
 * as is reported by the hw_revision field of the struct media_device_info
 * that is returned by ioctl MEDIA_IOC_DEVICE_INFO.
 *
 * V10 has RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE_V10 entries, V12 has
 * RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE_V12 entries.
 * RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE is equal to the maximum of the
 * two.
 */
struct rkisp1_cif_isp_hst_config {
	__u32 mode;
	__u8 histogram_predivider;
	struct rkisp1_cif_isp_window meas_window;
	__u8 hist_weight[RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE];
};

/**
 * struct rkisp1_cif_isp_aec_config - Configuration for Auto Exposure statistics
 *
 * @mode: Exposure measure mode (from enum rkisp1_cif_isp_exp_meas_mode)
 * @autostop: stop mode (from enum rkisp1_cif_isp_exp_ctrl_autostop)
 * @meas_window: coordinates of the measure window
 */
struct rkisp1_cif_isp_aec_config {
	__u32 mode;
	__u32 autostop;
	struct rkisp1_cif_isp_window meas_window;
};

/**
 * struct rkisp1_cif_isp_afc_config - Configuration for the Auto Focus statistics
 *
 * @num_afm_win: max RKISP1_CIF_ISP_AFM_MAX_WINDOWS
 * @afm_win: coordinates of the meas window
 * @thres: threshold used for minimizing the influence of noise
 * @var_shift: the number of bits for the shift operation at the end of the
 *	       calculation chain.
 */
struct rkisp1_cif_isp_afc_config {
	__u8 num_afm_win;
	struct rkisp1_cif_isp_window afm_win[RKISP1_CIF_ISP_AFM_MAX_WINDOWS];
	__u32 thres;
	__u32 var_shift;
};

/**
 * enum rkisp1_cif_isp_dpf_gain_usage - dpf gain usage
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED: don't use any gains in preprocessing stage
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_GAINS: use only the noise function gains from
 *				    registers DPF_NF_GAIN_R, ...
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS:  use only the gains from LSC module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_LSC_GAINS: use the noise function gains and the
 *					gains from LSC module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS: use only the gains from AWB module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS: use the gains from AWB and LSC module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_MAX: upper border (only for an internal evaluation)
 */
enum rkisp1_cif_isp_dpf_gain_usage {
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_LSC_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_MAX
};

/**
 * enum rkisp1_cif_isp_dpf_rb_filtersize - Red and blue filter sizes
 * @RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9: red and blue filter kernel size 13x9
 *				   (means 7x5 active pixel)
 * @RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9: red and blue filter kernel size 9x9
 *				   (means 5x5 active pixel)
 */
enum rkisp1_cif_isp_dpf_rb_filtersize {
	RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9,
	RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9,
};

/**
 * enum rkisp1_cif_isp_dpf_nll_scale_mode - dpf noise level scale mode
 * @RKISP1_CIF_ISP_NLL_SCALE_LINEAR: use a linear scaling
 * @RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC: use a logarithmic scaling
 */
enum rkisp1_cif_isp_dpf_nll_scale_mode {
	RKISP1_CIF_ISP_NLL_SCALE_LINEAR,
	RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC,
};

/**
 * struct rkisp1_cif_isp_dpf_nll - Noise level lookup
 *
 * @coeff: Noise level Lookup coefficient
 * @scale_mode: dpf noise level scale mode (from enum rkisp1_cif_isp_dpf_nll_scale_mode)
 */
struct rkisp1_cif_isp_dpf_nll {
	__u16 coeff[RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS];
	__u32 scale_mode;
};

/**
 * struct rkisp1_cif_isp_dpf_rb_flt - Red blue filter config
 *
 * @fltsize: The filter size for the red and blue pixels
 *	     (from enum rkisp1_cif_isp_dpf_rb_filtersize)
 * @spatial_coeff: Spatial weights
 * @r_enable: enable filter processing for red pixels
 * @b_enable: enable filter processing for blue pixels
 */
struct rkisp1_cif_isp_dpf_rb_flt {
	__u32 fltsize;
	__u8 spatial_coeff[RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS];
	__u8 r_enable;
	__u8 b_enable;
};

/**
 * struct rkisp1_cif_isp_dpf_g_flt - Green filter Configuration
 *
 * @spatial_coeff: Spatial weights
 * @gr_enable: enable filter processing for green pixels in green/red lines
 * @gb_enable: enable filter processing for green pixels in green/blue lines
 */
struct rkisp1_cif_isp_dpf_g_flt {
	__u8 spatial_coeff[RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS];
	__u8 gr_enable;
	__u8 gb_enable;
};

/**
 * struct rkisp1_cif_isp_dpf_gain - Noise function Configuration
 *
 * @mode: dpf gain usage  (from enum rkisp1_cif_isp_dpf_gain_usage)
 * @nf_r_gain: Noise function Gain that replaces the AWB gain for red pixels
 * @nf_b_gain: Noise function Gain that replaces the AWB gain for blue pixels
 * @nf_gr_gain: Noise function Gain that replaces the AWB gain
 *		for green pixels in a red line
 * @nf_gb_gain: Noise function Gain that replaces the AWB gain
 *		for green pixels in a blue line
 */
struct rkisp1_cif_isp_dpf_gain {
	__u32 mode;
	__u16 nf_r_gain;
	__u16 nf_b_gain;
	__u16 nf_gr_gain;
	__u16 nf_gb_gain;
};

/**
 * struct rkisp1_cif_isp_dpf_config - Configuration used by De-noising pre-filter
 *
 * @gain: noise function gain
 * @g_flt: green filter config
 * @rb_flt: red blue filter config
 * @nll: noise level lookup
 */
struct rkisp1_cif_isp_dpf_config {
	struct rkisp1_cif_isp_dpf_gain gain;
	struct rkisp1_cif_isp_dpf_g_flt g_flt;
	struct rkisp1_cif_isp_dpf_rb_flt rb_flt;
	struct rkisp1_cif_isp_dpf_nll nll;
};

/**
 * struct rkisp1_cif_isp_dpf_strength_config - strength of the filter
 *
 * @r: filter strength of the RED filter
 * @g: filter strength of the GREEN filter
 * @b: filter strength of the BLUE filter
 */
struct rkisp1_cif_isp_dpf_strength_config {
	__u8 r;
	__u8 g;
	__u8 b;
};

/**
 * struct rkisp1_cif_isp_isp_other_cfg - Parameters for some blocks in rockchip isp1
 *
 * @dpcc_config: Defect Pixel Cluster Correction config
 * @bls_config: Black Level Subtraction config
 * @sdg_config: sensor degamma config
 * @lsc_config: Lens Shade config
 * @awb_gain_config: Auto White balance gain config
 * @flt_config: filter config
 * @bdm_config: demosaic config
 * @ctk_config: cross talk config
 * @goc_config: gamma out config
 * @bls_config: black level subtraction config
 * @dpf_config: De-noising pre-filter config
 * @dpf_strength_config: dpf strength config
 * @cproc_config: color process config
 * @ie_config: image effects config
 */
struct rkisp1_cif_isp_isp_other_cfg {
	struct rkisp1_cif_isp_dpcc_config dpcc_config;
	struct rkisp1_cif_isp_bls_config bls_config;
	struct rkisp1_cif_isp_sdg_config sdg_config;
	struct rkisp1_cif_isp_lsc_config lsc_config;
	struct rkisp1_cif_isp_awb_gain_config awb_gain_config;
	struct rkisp1_cif_isp_flt_config flt_config;
	struct rkisp1_cif_isp_bdm_config bdm_config;
	struct rkisp1_cif_isp_ctk_config ctk_config;
	struct rkisp1_cif_isp_goc_config goc_config;
	struct rkisp1_cif_isp_dpf_config dpf_config;
	struct rkisp1_cif_isp_dpf_strength_config dpf_strength_config;
	struct rkisp1_cif_isp_cproc_config cproc_config;
	struct rkisp1_cif_isp_ie_config ie_config;
};

/**
 * struct rkisp1_cif_isp_isp_meas_cfg - Rockchip ISP1 Measure Parameters
 *
 * @awb_meas_config: auto white balance config
 * @hst_config: histogram config
 * @aec_config: auto exposure config
 * @afc_config: auto focus config
 */
struct rkisp1_cif_isp_isp_meas_cfg {
	struct rkisp1_cif_isp_awb_meas_config awb_meas_config;
	struct rkisp1_cif_isp_hst_config hst_config;
	struct rkisp1_cif_isp_aec_config aec_config;
	struct rkisp1_cif_isp_afc_config afc_config;
};

/**
 * struct rkisp1_params_cfg - Rockchip ISP1 Input Parameters Meta Data
 *
 * @module_en_update: mask the enable bits of which module should be updated
 * @module_ens: mask the enable value of each module, only update the module
 *		which correspond bit was set in module_en_update
 * @module_cfg_update: mask the config bits of which module should be updated
 * @meas: measurement config
 * @others: other config
 */
struct rkisp1_params_cfg {
	__u32 module_en_update;
	__u32 module_ens;
	__u32 module_cfg_update;

	struct rkisp1_cif_isp_isp_meas_cfg meas;
	struct rkisp1_cif_isp_isp_other_cfg others;
};

/*---------- PART2: Measurement Statistics ------------*/

/**
 * struct rkisp1_cif_isp_awb_meas - AWB measured values
 *
 * @cnt: White pixel count, number of "white pixels" found during last
 *	 measurement
 * @mean_y_or_g: Mean value of Y within window and frames,
 *		 Green if RGB is selected.
 * @mean_cb_or_b: Mean value of Cb within window and frames,
 *		  Blue if RGB is selected.
 * @mean_cr_or_r: Mean value of Cr within window and frames,
 *		  Red if RGB is selected.
 */
struct rkisp1_cif_isp_awb_meas {
	__u32 cnt;
	__u8 mean_y_or_g;
	__u8 mean_cb_or_b;
	__u8 mean_cr_or_r;
};

/**
 * struct rkisp1_cif_isp_awb_stat - statistics automatic white balance data
 *
 * @awb_mean: Mean measured data
 */
struct rkisp1_cif_isp_awb_stat {
	struct rkisp1_cif_isp_awb_meas awb_mean[RKISP1_CIF_ISP_AWB_MAX_GRID];
};

/**
 * struct rkisp1_cif_isp_bls_meas_val - BLS measured values
 *
 * @meas_r: Mean measured value for Bayer pattern R
 * @meas_gr: Mean measured value for Bayer pattern Gr
 * @meas_gb: Mean measured value for Bayer pattern Gb
 * @meas_b: Mean measured value for Bayer pattern B
 */
struct rkisp1_cif_isp_bls_meas_val {
	__u16 meas_r;
	__u16 meas_gr;
	__u16 meas_gb;
	__u16 meas_b;
};

/**
 * struct rkisp1_cif_isp_ae_stat - statistics auto exposure data
 *
 * @exp_mean: Mean luminance value of block xx
 * @bls_val:  BLS measured values
 *
 * The number of entries of @exp_mean depends on the hardware revision
 * as is reported by the hw_revision field of the struct media_device_info
 * that is returned by ioctl MEDIA_IOC_DEVICE_INFO.
 *
 * V10 has RKISP1_CIF_ISP_AE_MEAN_MAX_V10 entries, V12 has
 * RKISP1_CIF_ISP_AE_MEAN_MAX_V12 entries. RKISP1_CIF_ISP_AE_MEAN_MAX is equal
 * to the maximum of the two.
 *
 * Image is divided into 5x5 blocks on V10 and 9x9 blocks on V12.
 */
struct rkisp1_cif_isp_ae_stat {
	__u8 exp_mean[RKISP1_CIF_ISP_AE_MEAN_MAX];
	struct rkisp1_cif_isp_bls_meas_val bls_val;
};

/**
 * struct rkisp1_cif_isp_af_meas_val - AF measured values
 *
 * @sum: sharpness value
 * @lum: luminance value
 */
struct rkisp1_cif_isp_af_meas_val {
	__u32 sum;
	__u32 lum;
};

/**
 * struct rkisp1_cif_isp_af_stat - statistics auto focus data
 *
 * @window: AF measured value of window x
 *
 * The module measures the sharpness in 3 windows of selectable size via
 * register settings(ISP_AFM_*_A/B/C)
 */
struct rkisp1_cif_isp_af_stat {
	struct rkisp1_cif_isp_af_meas_val window[RKISP1_CIF_ISP_AFM_MAX_WINDOWS];
};

/**
 * struct rkisp1_cif_isp_hist_stat - statistics histogram data
 *
 * @hist_bins: measured bin counters. Each bin is a 20 bits unsigned fixed point
 *	       type. Bits 0-4 are the fractional part and bits 5-19 are the
 *	       integer part.
 *
 * The window of the measurements area is divided to 5x5 sub-windows for
 * V10 and to 9x9 sub-windows for V12. The histogram is then computed for each
 * sub-window independently and the final result is a weighted average of the
 * histogram measurements on all sub-windows. The window of the measurements
 * area and the weight of each sub-window are configurable using
 * struct @rkisp1_cif_isp_hst_config.
 *
 * The histogram contains 16 bins in V10 and 32 bins in V12.
 *
 * The number of entries of @hist_bins depends on the hardware revision
 * as is reported by the hw_revision field of the struct media_device_info
 * that is returned by ioctl MEDIA_IOC_DEVICE_INFO.
 *
 * V10 has RKISP1_CIF_ISP_HIST_BIN_N_MAX_V10 entries, V12 has
 * RKISP1_CIF_ISP_HIST_BIN_N_MAX_V12 entries. RKISP1_CIF_ISP_HIST_BIN_N_MAX is
 * equal to the maximum of the two.
 */
struct rkisp1_cif_isp_hist_stat {
	__u32 hist_bins[RKISP1_CIF_ISP_HIST_BIN_N_MAX];
};

/**
 * struct rkisp1_cif_isp_stat - Rockchip ISP1 Statistics Data
 *
 * @awb: statistics data for automatic white balance
 * @ae: statistics data for auto exposure
 * @af: statistics data for auto focus
 * @hist: statistics histogram data
 */
struct rkisp1_cif_isp_stat {
	struct rkisp1_cif_isp_awb_stat awb;
	struct rkisp1_cif_isp_ae_stat ae;
	struct rkisp1_cif_isp_af_stat af;
	struct rkisp1_cif_isp_hist_stat hist;
};

/**
 * struct rkisp1_stat_buffer - Rockchip ISP1 Statistics Meta Data
 *
 * @meas_type: measurement types (RKISP1_CIF_ISP_STAT_* definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp1_stat_buffer {
	__u32 meas_type;
	__u32 frame_id;
	struct rkisp1_cif_isp_stat params;
};

#endif /* _UAPI_RKISP1_CONFIG_H */
