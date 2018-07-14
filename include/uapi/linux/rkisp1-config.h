// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip isp1 driver
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKISP1_CONFIG_H
#define _UAPI_RKISP1_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define CIFISP_MODULE_DPCC              (1 << 0)
#define CIFISP_MODULE_BLS               (1 << 1)
#define CIFISP_MODULE_SDG               (1 << 2)
#define CIFISP_MODULE_HST               (1 << 3)
#define CIFISP_MODULE_LSC               (1 << 4)
#define CIFISP_MODULE_AWB_GAIN          (1 << 5)
#define CIFISP_MODULE_FLT               (1 << 6)
#define CIFISP_MODULE_BDM               (1 << 7)
#define CIFISP_MODULE_CTK               (1 << 8)
#define CIFISP_MODULE_GOC               (1 << 9)
#define CIFISP_MODULE_CPROC             (1 << 10)
#define CIFISP_MODULE_AFC               (1 << 11)
#define CIFISP_MODULE_AWB               (1 << 12)
#define CIFISP_MODULE_IE                (1 << 13)
#define CIFISP_MODULE_AEC               (1 << 14)
#define CIFISP_MODULE_WDR               (1 << 15)
#define CIFISP_MODULE_DPF               (1 << 16)
#define CIFISP_MODULE_DPF_STRENGTH      (1 << 17)

#define CIFISP_CTK_COEFF_MAX            0x100
#define CIFISP_CTK_OFFSET_MAX           0x800

#define CIFISP_AE_MEAN_MAX              81
#define CIFISP_HIST_BIN_N_MAX           32
#define CIFISP_AFM_MAX_WINDOWS          3
#define CIFISP_DEGAMMA_CURVE_SIZE       17

#define CIFISP_BDM_MAX_TH               0xFF

/*
 * Black level compensation
 */
/* maximum value for horizontal start address */
#define CIFISP_BLS_START_H_MAX             0x00000FFF
/* maximum value for horizontal stop address */
#define CIFISP_BLS_STOP_H_MAX              0x00000FFF
/* maximum value for vertical start address */
#define CIFISP_BLS_START_V_MAX             0x00000FFF
/* maximum value for vertical stop address */
#define CIFISP_BLS_STOP_V_MAX              0x00000FFF
/* maximum is 2^18 = 262144*/
#define CIFISP_BLS_SAMPLES_MAX             0x00000012
/* maximum value for fixed black level */
#define CIFISP_BLS_FIX_SUB_MAX             0x00000FFF
/* minimum value for fixed black level */
#define CIFISP_BLS_FIX_SUB_MIN             0xFFFFF000
/* 13 bit range (signed)*/
#define CIFISP_BLS_FIX_MASK                0x00001FFF

/*
 * Automatic white balance measurments
 */
#define CIFISP_AWB_MAX_GRID                1
#define CIFISP_AWB_MAX_FRAMES              7

/*
 * Gamma out
 */
/* Maximum number of color samples supported */
#define CIFISP_GAMMA_OUT_MAX_SAMPLES       34

/*
 * Lens shade correction
 */
#define CIFISP_LSC_GRAD_TBL_SIZE           8
#define CIFISP_LSC_SIZE_TBL_SIZE           8
/*
 * The following matches the tuning process,
 * not the max capabilities of the chip.
 * Last value unused.
 */
#define	CIFISP_LSC_DATA_TBL_SIZE           290

/*
 * Histogram calculation
 */
/* Last 3 values unused. */
#define CIFISP_HISTOGRAM_WEIGHT_GRIDS_SIZE 81

/*
 * Defect Pixel Cluster Correction
 */
#define CIFISP_DPCC_METHODS_MAX       3

/*
 * Denoising pre filter
 */
#define CIFISP_DPF_MAX_NLF_COEFFS      17
#define CIFISP_DPF_MAX_SPATIAL_COEFFS  6

/*
 * Measurement types
 */
#define CIFISP_STAT_AWB           (1 << 0)
#define CIFISP_STAT_AUTOEXP       (1 << 1)
#define CIFISP_STAT_AFM_FIN       (1 << 2)
#define CIFISP_STAT_HIST          (1 << 3)

enum cifisp_histogram_mode {
	CIFISP_HISTOGRAM_MODE_DISABLE,
	CIFISP_HISTOGRAM_MODE_RGB_COMBINED,
	CIFISP_HISTOGRAM_MODE_R_HISTOGRAM,
	CIFISP_HISTOGRAM_MODE_G_HISTOGRAM,
	CIFISP_HISTOGRAM_MODE_B_HISTOGRAM,
	CIFISP_HISTOGRAM_MODE_Y_HISTOGRAM
};

enum cifisp_awb_mode_type {
	CIFISP_AWB_MODE_MANUAL,
	CIFISP_AWB_MODE_RGB,
	CIFISP_AWB_MODE_YCBCR
};

enum cifisp_flt_mode {
	CIFISP_FLT_STATIC_MODE,
	CIFISP_FLT_DYNAMIC_MODE
};

/**
 * enum cifisp_exp_ctrl_auotostop - stop modes
 * @CIFISP_EXP_CTRL_AUTOSTOP_0: continuous measurement
 * @CIFISP_EXP_CTRL_AUTOSTOP_1: stop measuring after a complete frame
 */
enum cifisp_exp_ctrl_auotostop {
	CIFISP_EXP_CTRL_AUTOSTOP_0 = 0,
	CIFISP_EXP_CTRL_AUTOSTOP_1 = 1,
};

/**
 * enum cifisp_exp_meas_mode - Exposure measure mode
 * @CIFISP_EXP_MEASURING_MODE_0: Y = 16 + 0.25R + 0.5G + 0.1094B
 * @CIFISP_EXP_MEASURING_MODE_1: Y = (R + G + B) x (85/256)
 */
enum cifisp_exp_meas_mode {
	CIFISP_EXP_MEASURING_MODE_0,
	CIFISP_EXP_MEASURING_MODE_1,
};

/*---------- PART1: Input Parameters ------------*/

struct cifisp_window {
	unsigned short h_offs;
	unsigned short v_offs;
	unsigned short h_size;
	unsigned short v_size;
} __attribute__ ((packed));

/**
 * struct cifisp_bls_fixed_val - BLS fixed subtraction values
 *
 * The values will be subtracted from the sensor
 * values. Therefore a negative value means addition instead of subtraction!
 *
 * @r: Fixed (signed!) subtraction value for Bayer pattern R
 * @gr: Fixed (signed!) subtraction value for Bayer pattern Gr
 * @gb: Fixed (signed!) subtraction value for Bayer pattern Gb
 * @b: Fixed (signed!) subtraction value for Bayer pattern B
 */
struct cifisp_bls_fixed_val {
	signed short r;
	signed short gr;
	signed short gb;
	signed short b;
} __attribute__ ((packed));

/**
 * struct cifisp_bls_config - Configuration used by black level subtraction
 *
 * @enable_auto: Automatic mode activated means that the measured values
 * are subtracted.Otherwise the fixed subtraction
 * values will be subtracted.
 * @en_windows: enabled window
 * @bls_window1: Measurement window 1 size
 * @bls_window2: Measurement window 2 size
 * @bls_samples: Set amount of measured pixels for each Bayer position
 * (A, B,C and D) to 2^bls_samples.
 * @cifisp_bls_fixed_val: Fixed subtraction values
 */
struct cifisp_bls_config {
	unsigned char enable_auto;
	unsigned char en_windows;
	struct cifisp_window bls_window1;
	struct cifisp_window bls_window2;
	unsigned char bls_samples;
	struct cifisp_bls_fixed_val fixed_val;
} __attribute__ ((packed));

/**
 * struct cifisp_dpcc_methods_config - Methods Configuration used by Defect Pixel Cluster Correction
 *
 * @method:
 * @line_thresh:
 * @line_mad_fac:
 * @pg_fac:
 * @rnd_thresh:
 * @rg_fac:
 */
struct cifisp_dpcc_methods_config {
	unsigned int method;
	unsigned int line_thresh;
	unsigned int line_mad_fac;
	unsigned int pg_fac;
	unsigned int rnd_thresh;
	unsigned int rg_fac;
} __attribute__ ((packed));

/**
 * struct cifisp_dpcc_methods_config - Configuration used by Defect Pixel Cluster Correction
 *
 * @mode: dpcc output mode
 * @output_mode: whether use hard coded methods
 * @set_use: stage1 methods set
 * @methods: methods config
 * @ro_limits: rank order limits
 * @rnd_offs: differential rank offsets for rank neighbor difference
 */
struct cifisp_dpcc_config {
	unsigned int mode;
	unsigned int output_mode;
	unsigned int set_use;
	struct cifisp_dpcc_methods_config methods[CIFISP_DPCC_METHODS_MAX];
	unsigned int ro_limits;
	unsigned int rnd_offs;
} __attribute__ ((packed));

struct cifisp_gamma_corr_curve {
	unsigned short gamma_y[CIFISP_DEGAMMA_CURVE_SIZE];
} __attribute__ ((packed));

struct cifisp_gamma_curve_x_axis_pnts {
	unsigned int gamma_dx0;
	unsigned int gamma_dx1;
} __attribute__ ((packed));

/**
 * struct cifisp_gamma_corr_curve - Configuration used by sensor degamma
 *
 * @curve_x: gamma curve point definition axis for x
 * @xa_pnts: x increments
 */
struct cifisp_sdg_config {
	struct cifisp_gamma_corr_curve curve_r;
	struct cifisp_gamma_corr_curve curve_g;
	struct cifisp_gamma_corr_curve curve_b;
	struct cifisp_gamma_curve_x_axis_pnts xa_pnts;
} __attribute__ ((packed));

/**
 * struct cifisp_lsc_config - Configuration used by Lens shading correction
 *
 * refer to datasheet for details
 */
struct cifisp_lsc_config {
	unsigned int r_data_tbl[CIFISP_LSC_DATA_TBL_SIZE];
	unsigned int gr_data_tbl[CIFISP_LSC_DATA_TBL_SIZE];
	unsigned int gb_data_tbl[CIFISP_LSC_DATA_TBL_SIZE];
	unsigned int b_data_tbl[CIFISP_LSC_DATA_TBL_SIZE];

	unsigned int x_grad_tbl[CIFISP_LSC_GRAD_TBL_SIZE];
	unsigned int y_grad_tbl[CIFISP_LSC_GRAD_TBL_SIZE];

	unsigned int x_size_tbl[CIFISP_LSC_SIZE_TBL_SIZE];
	unsigned int y_size_tbl[CIFISP_LSC_SIZE_TBL_SIZE];
	unsigned short config_width;
	unsigned short config_height;
} __attribute__ ((packed));

/**
 * struct cifisp_ie_config - Configuration used by image effects
 *
 * @eff_mat_1: 3x3 Matrix Coefficients for Emboss Effect 1
 * @eff_mat_2: 3x3 Matrix Coefficients for Emboss Effect 2
 * @eff_mat_3: 3x3 Matrix Coefficients for Emboss 3/Sketch 1
 * @eff_mat_4: 3x3 Matrix Coefficients for Sketch Effect 2
 * @eff_mat_5: 3x3 Matrix Coefficients for Sketch Effect 3
 * @eff_tint: Chrominance increment values of tint (used for sepia effect)
 */
struct cifisp_ie_config {
	unsigned short effect;
	unsigned short color_sel;
	unsigned short eff_mat_1;
	unsigned short eff_mat_2;
	unsigned short eff_mat_3;
	unsigned short eff_mat_4;
	unsigned short eff_mat_5;
	unsigned short eff_tint;
} __attribute__ ((packed));

/**
 * struct cifisp_cproc_config - Configuration used by Color Processing
 *
 * @c_out_range: Chrominance pixel clipping range at output. (0 for limit, 1 for full)
 * @y_in_range: Luminance pixel clipping range at output.
 * @y_out_range: Luminance pixel clipping range at output.
 * @contrast: 00~ff, 0.0~1.992
 * @brightness: 80~7F, -128~+127
 * @sat: saturation, 00~FF, 0.0~1.992
 * @hue: 80~7F, -90~+87.188
 */
struct cifisp_cproc_config {
	unsigned char c_out_range;
	unsigned char y_in_range;
	unsigned char y_out_range;
	unsigned char contrast;
	unsigned char brightness;
	unsigned char sat;
	unsigned char hue;
} __attribute__ ((packed));

/**
 * struct cifisp_awb_meas_config - Configuration used by auto white balance
 *
 * @awb_wnd: white balance measurement window (in pixels)
 * @max_y: only pixels values < max_y contribute to awb measurement, set to 0 to disable this feature
 * @min_y: only pixels values > min_y contribute to awb measurement
 * @max_csum: Chrominance sum maximum value, only consider pixels with Cb+Cr, smaller than threshold for awb measurements
 * @min_c: Chrominance minimum value, only consider pixels with Cb/Cr each greater than threshold value for awb measurements
 * @frames: number of frames - 1 used for mean value calculation(ucFrames=0 means 1 Frame)
 * @awb_ref_cr: reference Cr value for AWB regulation, target for AWB
 * @awb_ref_cb: reference Cb value for AWB regulation, target for AWB
 */
struct cifisp_awb_meas_config {
	/*
	 * Note: currently the h and v offsets are mapped to grid offsets
	 */
	struct cifisp_window awb_wnd;
	enum cifisp_awb_mode_type awb_mode;
	unsigned char max_y;
	unsigned char min_y;
	unsigned char max_csum;
	unsigned char min_c;
	unsigned char frames;
	unsigned char awb_ref_cr;
	unsigned char awb_ref_cb;
	bool enable_ymax_cmp;
} __attribute__ ((packed));

/**
 * struct cifisp_awb_gain_config - Configuration used by auto white balance gain
 *
 * out_data_x = ( AWB_GEAIN_X * in_data + 128) >> 8
 */
struct cifisp_awb_gain_config {
	unsigned short gain_red;
	unsigned short gain_green_r;
	unsigned short gain_blue;
	unsigned short gain_green_b;
} __attribute__ ((packed));

/**
 * struct cifisp_flt_config - Configuration used by ISP filtering
 *
 * @mode: ISP_FILT_MODE register fields
 * @grn_stage1: ISP_FILT_MODE register fields
 * @chr_h_mode: ISP_FILT_MODE register fields
 * @chr_v_mode: ISP_FILT_MODE register fields
 *
 * refer to datasheet for details.
 */
struct cifisp_flt_config {
	enum cifisp_flt_mode mode;
	unsigned char grn_stage1;
	unsigned char chr_h_mode;
	unsigned char chr_v_mode;
	unsigned int thresh_bl0;
	unsigned int thresh_bl1;
	unsigned int thresh_sh0;
	unsigned int thresh_sh1;
	unsigned int lum_weight;
	unsigned int fac_sh1;
	unsigned int fac_sh0;
	unsigned int fac_mid;
	unsigned int fac_bl0;
	unsigned int fac_bl1;
} __attribute__ ((packed));

/**
 * struct cifisp_bdm_config - Configuration used by Bayer DeMosaic
 *
 * @demosaic_th: threshod for bayer demosaicing texture detection
 */
struct cifisp_bdm_config {
	unsigned char demosaic_th;
} __attribute__ ((packed));

/**
 * struct cifisp_ctk_config - Configuration used by Cross Talk correction
 *
 * @coeff: color correction matrix
 * @ct_offset_b: offset for the crosstalk correction matrix
 */
struct cifisp_ctk_config {
	unsigned short coeff0;
	unsigned short coeff1;
	unsigned short coeff2;
	unsigned short coeff3;
	unsigned short coeff4;
	unsigned short coeff5;
	unsigned short coeff6;
	unsigned short coeff7;
	unsigned short coeff8;
	unsigned short ct_offset_r;
	unsigned short ct_offset_g;
	unsigned short ct_offset_b;
} __attribute__ ((packed));

enum cifisp_goc_mode {
	CIFISP_GOC_MODE_LOGARITHMIC,
	CIFISP_GOC_MODE_EQUIDISTANT
};

/**
 * struct cifisp_goc_config - Configuration used by Gamma Out correction
 *
 * @mode: goc mode
 * @gamma_y: gamma out curve y-axis for all color components
 */
struct cifisp_goc_config {
	enum cifisp_goc_mode mode;
	unsigned short gamma_y[CIFISP_GAMMA_OUT_MAX_SAMPLES];
} __attribute__ ((packed));

/**
 * struct cifisp_hst_config - Configuration used by Histogram
 *
 * @mode: histogram mode
 * @histogram_predivider: process every stepsize pixel, all other pixels are skipped
 * @meas_window: coordinates of the meas window
 * @hist_weight: weighting factor for sub-windows
 */
struct cifisp_hst_config {
	enum cifisp_histogram_mode mode;
	unsigned char histogram_predivider;
	struct cifisp_window meas_window;
	unsigned char hist_weight[CIFISP_HISTOGRAM_WEIGHT_GRIDS_SIZE];
} __attribute__ ((packed));

/**
 * struct cifisp_aec_config - Configuration used by Auto Exposure Control
 *
 * @mode: Exposure measure mode
 * @autostop: stop mode (from enum cifisp_exp_ctrl_auotostop)
 * @meas_window: coordinates of the meas window
 */
struct cifisp_aec_config {
	enum cifisp_exp_meas_mode mode;
	__u32 autostop;
	struct cifisp_window meas_window;
} __attribute__ ((packed));

/**
 * struct cifisp_afc_config - Configuration used by Auto Focus Control
 *
 * @num_afm_win: max CIFISP_AFM_MAX_WINDOWS
 * @afm_win: coordinates of the meas window
 * @thres: threshold used for minimizing the influence of noise
 * @var_shift: the number of bits for the shift operation at the end of the calculation chain.
 */
struct cifisp_afc_config {
	unsigned char num_afm_win;
	struct cifisp_window afm_win[CIFISP_AFM_MAX_WINDOWS];
	unsigned int thres;
	unsigned int var_shift;
} __attribute__ ((packed));

/**
 * enum cifisp_dpf_gain_usage - dpf gain usage
 * @CIFISP_DPF_GAIN_USAGE_DISABLED: don't use any gains in preprocessing stage
 * @CIFISP_DPF_GAIN_USAGE_NF_GAINS: use only the noise function gains from registers DPF_NF_GAIN_R, ...
 * @CIFISP_DPF_GAIN_USAGE_LSC_GAINS:  use only the gains from LSC module
 * @CIFISP_DPF_GAIN_USAGE_NF_LSC_GAINS: use the noise function gains and the gains from LSC module
 * @CIFISP_DPF_GAIN_USAGE_AWB_GAINS: use only the gains from AWB module
 * @CIFISP_DPF_GAIN_USAGE_AWB_LSC_GAINS: use the gains from AWB and LSC module
 * @CIFISP_DPF_GAIN_USAGE_MAX: upper border (only for an internal evaluation)
 */
enum cifisp_dpf_gain_usage {
	CIFISP_DPF_GAIN_USAGE_DISABLED,
	CIFISP_DPF_GAIN_USAGE_NF_GAINS,
	CIFISP_DPF_GAIN_USAGE_LSC_GAINS,
	CIFISP_DPF_GAIN_USAGE_NF_LSC_GAINS,
	CIFISP_DPF_GAIN_USAGE_AWB_GAINS,
	CIFISP_DPF_GAIN_USAGE_AWB_LSC_GAINS,
	CIFISP_DPF_GAIN_USAGE_MAX
};

/**
 * enum cifisp_dpf_gain_usage - dpf gain usage
 * @CIFISP_DPF_RB_FILTERSIZE_13x9: red and blue filter kernel size 13x9 (means 7x5 active pixel)
 * @CIFISP_DPF_RB_FILTERSIZE_9x9: red and blue filter kernel size 9x9 (means 5x5 active pixel)
 */
enum cifisp_dpf_rb_filtersize {
	CIFISP_DPF_RB_FILTERSIZE_13x9,
	CIFISP_DPF_RB_FILTERSIZE_9x9,
};

/**
 * enum cifisp_dpf_nll_scale_mode - dpf noise level scale mode
 * @CIFISP_NLL_SCALE_LINEAR: use a linear scaling
 * @CIFISP_NLL_SCALE_LOGARITHMIC: use a logarithmic scaling
 */
enum cifisp_dpf_nll_scale_mode {
	CIFISP_NLL_SCALE_LINEAR,
	CIFISP_NLL_SCALE_LOGARITHMIC,
};

struct cifisp_dpf_nll {
	unsigned short coeff[CIFISP_DPF_MAX_NLF_COEFFS];
	enum cifisp_dpf_nll_scale_mode scale_mode;
} __attribute__ ((packed));

struct cifisp_dpf_rb_flt {
	enum cifisp_dpf_rb_filtersize fltsize;
	unsigned char spatial_coeff[CIFISP_DPF_MAX_SPATIAL_COEFFS];
	bool r_enable;
	bool b_enable;
} __attribute__ ((packed));

struct cifisp_dpf_g_flt {
	unsigned char spatial_coeff[CIFISP_DPF_MAX_SPATIAL_COEFFS];
	bool gr_enable;
	bool gb_enable;
} __attribute__ ((packed));

struct cifisp_dpf_gain {
	enum cifisp_dpf_gain_usage mode;
	unsigned short nf_r_gain;
	unsigned short nf_b_gain;
	unsigned short nf_gr_gain;
	unsigned short nf_gb_gain;
} __attribute__ ((packed));

/**
 * struct cifisp_dpf_config - Configuration used by De-noising pre-filter
 *
 * @gain: noise function gain
 * @g_flt: green filter config
 * @rb_flt: red blue filter config
 * @nll: noise level lookup
 */
struct cifisp_dpf_config {
	struct cifisp_dpf_gain gain;
	struct cifisp_dpf_g_flt g_flt;
	struct cifisp_dpf_rb_flt rb_flt;
	struct cifisp_dpf_nll nll;
} __attribute__ ((packed));

/**
 * struct cifisp_dpf_strength_config - strength of the filter
 *
 * @r: filter strength of the RED filter
 * @g: filter strength of the GREEN filter
 * @b: filter strength of the BLUE filter
 */
struct cifisp_dpf_strength_config {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} __attribute__ ((packed));

/**
 * struct cifisp_isp_other_cfg - Parameters for some blocks in rockchip isp1
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
 * @bls_config: black level suntraction config
 * @dpf_config: De-noising pre-filter config
 * @dpf_strength_config: dpf strength config
 * @cproc_config: color process config
 * @ie_config: image effects config
 */
struct cifisp_isp_other_cfg {
	struct cifisp_dpcc_config dpcc_config;
	struct cifisp_bls_config bls_config;
	struct cifisp_sdg_config sdg_config;
	struct cifisp_lsc_config lsc_config;
	struct cifisp_awb_gain_config awb_gain_config;
	struct cifisp_flt_config flt_config;
	struct cifisp_bdm_config bdm_config;
	struct cifisp_ctk_config ctk_config;
	struct cifisp_goc_config goc_config;
	struct cifisp_dpf_config dpf_config;
	struct cifisp_dpf_strength_config dpf_strength_config;
	struct cifisp_cproc_config cproc_config;
	struct cifisp_ie_config ie_config;
} __attribute__ ((packed));

/**
 * struct cifisp_isp_meas_cfg - Rockchip ISP1 Measure Parameters
 *
 * @awb_meas_config: auto white balance config
 * @hst_config: histogram config
 * @aec_config: auto exposure config
 * @afc_config: auto focus config
 */
struct cifisp_isp_meas_cfg {
	struct cifisp_awb_meas_config awb_meas_config;
	struct cifisp_hst_config hst_config;
	struct cifisp_aec_config aec_config;
	struct cifisp_afc_config afc_config;
} __attribute__ ((packed));

/**
 * struct rkisp1_isp_params_cfg - Rockchip ISP1 Input Parameters Meta Data
 *
 * @module_en_update: mask the enable bits of which module  should be updated
 * @module_ens: mask the enable value of each module, only update the module
 * which correspond bit was set in module_en_update
 * @module_cfg_update: mask the config bits of which module  should be updated
 * @meas: measurement config
 * @others: other config
 */
struct rkisp1_isp_params_cfg {
	unsigned int module_en_update;
	unsigned int module_ens;
	unsigned int module_cfg_update;

	struct cifisp_isp_meas_cfg meas;
	struct cifisp_isp_other_cfg others;
} __attribute__ ((packed));

/*---------- PART2: Measurement Statistics ------------*/

/**
 * struct cifisp_bls_meas_val - AWB measured values
 *
 * @cnt: White pixel count, number of "white pixels" found during laster measurement
 * @mean_y_or_g: Mean value of Y within window and frames, Green if RGB is selected.
 * @mean_cb_or_b: Mean value of Cb within window and frames, Blue if RGB is selected.
 * @mean_cr_or_r: Mean value of Cr within window and frames, Red if RGB is selected.
 */
struct cifisp_awb_meas {
	unsigned int cnt;
	unsigned char mean_y_or_g;
	unsigned char mean_cb_or_b;
	unsigned char mean_cr_or_r;
} __attribute__ ((packed));

/**
 * struct cifisp_awb_stat - statistics automatic white balance data
 *
 * @awb_mean: Mean measured data
 */
struct cifisp_awb_stat {
	struct cifisp_awb_meas awb_mean[CIFISP_AWB_MAX_GRID];
} __attribute__ ((packed));

/**
 * struct cifisp_bls_meas_val - BLS measured values
 *
 * @meas_r: Mean measured value for Bayer pattern R
 * @meas_gr: Mean measured value for Bayer pattern Gr
 * @meas_gb: Mean measured value for Bayer pattern Gb
 * @meas_b: Mean measured value for Bayer pattern B
 */
struct cifisp_bls_meas_val {
	unsigned short meas_r;
	unsigned short meas_gr;
	unsigned short meas_gb;
	unsigned short meas_b;
} __attribute__ ((packed));

/**
 * struct cifisp_ae_stat - statistics auto exposure data
 *
 * @exp_mean: Mean luminance value of block xx
 * @bls_val: available wit exposure results
 *
 * Image is divided into 5x5 blocks.
 */
struct cifisp_ae_stat {
	unsigned char exp_mean[CIFISP_AE_MEAN_MAX];
	struct cifisp_bls_meas_val bls_val;
} __attribute__ ((packed));

/**
 * struct cifisp_af_meas_val - AF measured values
 *
 * @sum: sharpness, refer to datasheet for definition
 * @lum: luminance, refer to datasheet for definition
 */
struct cifisp_af_meas_val {
	unsigned int sum;
	unsigned int lum;
} __attribute__ ((packed));

/**
 * struct cifisp_af_stat - statistics auto focus data
 *
 * @window: AF measured value of window x
 *
 * The module measures the sharpness in 3 windows of selectable size via
 * register settings(ISP_AFM_*_A/B/C)
 */
struct cifisp_af_stat {
	struct cifisp_af_meas_val window[CIFISP_AFM_MAX_WINDOWS];
} __attribute__ ((packed));

/**
 * struct cifisp_hist_stat - statistics histogram data
 *
 * @hist_bins: measured bin counters
 *
 * Measurement window divided into 25 sub-windows, set
 * with ISP_HIST_XXX
 */
struct cifisp_hist_stat {
	unsigned int hist_bins[CIFISP_HIST_BIN_N_MAX];
} __attribute__ ((packed));

/**
 * struct rkisp1_stat_buffer - Rockchip ISP1 Statistics Data
 *
 * @cifisp_awb_stat: statistics data for automatic white balance
 * @cifisp_ae_stat: statistics data for auto exposure
 * @cifisp_af_stat: statistics data for auto focus
 * @cifisp_hist_stat: statistics histogram data
 */
struct cifisp_stat {
	struct cifisp_awb_stat awb;
	struct cifisp_ae_stat ae;
	struct cifisp_af_stat af;
	struct cifisp_hist_stat hist;
} __attribute__ ((packed));

/**
 * struct rkisp1_stat_buffer - Rockchip ISP1 Statistics Meta Data
 *
 * @meas_type: measurement types (CIFISP_STAT_ definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp1_stat_buffer {
	unsigned int meas_type;
	unsigned int frame_id;
	struct cifisp_stat params;
} __attribute__ ((packed));

#endif /* _UAPI_RKISP1_CONFIG_H */
