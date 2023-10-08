/* SPDX-License-Identifier: (GPL-2.0+ WITH Linux-syscall-note) OR MIT
 *
 * Rockchip ISP32
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK_ISP32_CONFIG_H
#define _UAPI_RK_ISP32_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/rk-isp3-config.h>

#define RKISP_CMD_GET_TB_HEAD_V32 \
	_IOR('V', BASE_VIDIOC_PRIVATE + 12, struct rkisp32_thunderboot_resmem_head)

#define ISP32_MODULE_DPCC		ISP3X_MODULE_DPCC
#define ISP32_MODULE_BLS		ISP3X_MODULE_BLS
#define ISP32_MODULE_SDG		ISP3X_MODULE_SDG
#define ISP32_MODULE_LSC		ISP3X_MODULE_LSC
#define ISP32_MODULE_AWB_GAIN		ISP3X_MODULE_AWB_GAIN
#define ISP32_MODULE_BDM		ISP3X_MODULE_BDM
#define ISP32_MODULE_CCM		ISP3X_MODULE_CCM
#define ISP32_MODULE_GOC		ISP3X_MODULE_GOC
#define ISP32_MODULE_CPROC		ISP3X_MODULE_CPROC
#define ISP32_MODULE_IE			ISP3X_MODULE_IE
#define ISP32_MODULE_RAWAF		ISP3X_MODULE_RAWAF
#define ISP32_MODULE_RAWAE0		ISP3X_MODULE_RAWAE0
#define ISP32_MODULE_RAWAE1		ISP3X_MODULE_RAWAE1
#define ISP32_MODULE_RAWAE2		ISP3X_MODULE_RAWAE2
#define ISP32_MODULE_RAWAE3		ISP3X_MODULE_RAWAE3
#define ISP32_MODULE_RAWAWB		ISP3X_MODULE_RAWAWB
#define ISP32_MODULE_RAWHIST0		ISP3X_MODULE_RAWHIST0
#define ISP32_MODULE_RAWHIST1		ISP3X_MODULE_RAWHIST1
#define ISP32_MODULE_RAWHIST2		ISP3X_MODULE_RAWHIST2
#define ISP32_MODULE_RAWHIST3		ISP3X_MODULE_RAWHIST3
#define ISP32_MODULE_HDRMGE		ISP3X_MODULE_HDRMGE
#define ISP32_MODULE_RAWNR		ISP3X_MODULE_RAWNR
#define ISP32_MODULE_GIC		ISP3X_MODULE_GIC
#define ISP32_MODULE_DHAZ		ISP3X_MODULE_DHAZ
#define ISP32_MODULE_3DLUT		ISP3X_MODULE_3DLUT
#define ISP32_MODULE_LDCH		ISP3X_MODULE_LDCH
#define ISP32_MODULE_GAIN		ISP3X_MODULE_GAIN
#define ISP32_MODULE_DEBAYER		ISP3X_MODULE_DEBAYER
#define ISP32_MODULE_BAYNR		ISP3X_MODULE_BAYNR
#define ISP32_MODULE_BAY3D		ISP3X_MODULE_BAY3D
#define ISP32_MODULE_YNR		ISP3X_MODULE_YNR
#define ISP32_MODULE_CNR		ISP3X_MODULE_CNR
#define ISP32_MODULE_SHARP		ISP3X_MODULE_SHARP
#define ISP32_MODULE_DRC		ISP3X_MODULE_DRC
#define ISP32_MODULE_CAC		ISP3X_MODULE_CAC
#define ISP32_MODULE_CSM		ISP3X_MODULE_CSM
#define ISP32_MODULE_CGC		ISP3X_MODULE_CGC
#define ISP32_MODULE_VSM		BIT_ULL(45)
#define ISP32_MODULE_RTT_FST		BIT_ULL(62)
#define ISP32_MODULE_FORCE		ISP3X_MODULE_FORCE

/* Measurement types */
#define ISP32_STAT_RAWAWB		ISP3X_STAT_RAWAWB
#define ISP32_STAT_RAWAF		ISP3X_STAT_RAWAF
#define ISP32_STAT_RAWAE0		ISP3X_STAT_RAWAE0
#define ISP32_STAT_RAWAE1		ISP3X_STAT_RAWAE1
#define ISP32_STAT_RAWAE2		ISP3X_STAT_RAWAE2
#define ISP32_STAT_RAWAE3		ISP3X_STAT_RAWAE3
#define ISP32_STAT_RAWHST0		ISP3X_STAT_RAWHST0
#define ISP32_STAT_RAWHST1		ISP3X_STAT_RAWHST1
#define ISP32_STAT_RAWHST2		ISP3X_STAT_RAWHST2
#define ISP32_STAT_RAWHST3		ISP3X_STAT_RAWHST3
#define ISP32_STAT_BLS			ISP3X_STAT_BLS
#define ISP32_STAT_DHAZ			ISP3X_STAT_DHAZ
#define ISP32_STAT_VSM			BIT(18)
#define ISP32_STAT_INFO2DDR		BIT(19)
#define ISP32_STAT_RTT_FST		BIT(31)

#define ISP32_MESH_BUF_NUM		ISP3X_MESH_BUF_NUM

#define ISP32_LSC_GRAD_TBL_SIZE		ISP3X_LSC_GRAD_TBL_SIZE
#define ISP32_LSC_SIZE_TBL_SIZE		ISP3X_LSC_SIZE_TBL_SIZE
#define ISP32_LSC_DATA_TBL_SIZE		ISP3X_LSC_DATA_TBL_SIZE

#define ISP32_DEGAMMA_CURVE_SIZE	ISP3X_DEGAMMA_CURVE_SIZE

#define ISP32_GAIN_IDX_NUM		ISP3X_GAIN_IDX_NUM
#define ISP32_GAIN_LUT_NUM		ISP3X_GAIN_LUT_NUM

#define ISP32_RAWAWB_EXCL_STAT_NUM	ISP3X_RAWAWB_EXCL_STAT_NUM
#define ISP32_RAWAWB_HSTBIN_NUM		ISP3X_RAWAWB_HSTBIN_NUM
#define ISP32_RAWAWB_WEIGHT_NUM		ISP3X_RAWAWB_WEIGHT_NUM
#define ISP32_RAWAWB_SUM_NUM		4
#define ISP32_RAWAWB_RAMDATA_NUM	ISP3X_RAWAWB_RAMDATA_NUM
#define ISP32L_RAWAWB_WEIGHT_NUM	5
#define ISP32L_RAWAWB_RAMDATA_RGB_NUM	25
#define ISP32L_RAWAWB_RAMDATA_WP_NUM	13

#define	ISP32_RAWAEBIG_SUBWIN_NUM	ISP3X_RAWAEBIG_SUBWIN_NUM
#define ISP32_RAWAEBIG_MEAN_NUM		ISP3X_RAWAEBIG_MEAN_NUM
#define ISP32_RAWAELITE_MEAN_NUM	ISP3X_RAWAELITE_MEAN_NUM

#define ISP32_RAWHISTBIG_SUBWIN_NUM	ISP3X_RAWHISTBIG_SUBWIN_NUM
#define ISP32_RAWHISTLITE_SUBWIN_NUM	ISP3X_RAWHISTLITE_SUBWIN_NUM
#define ISP32_HIST_BIN_N_MAX		ISP3X_HIST_BIN_N_MAX
#define ISP32L_HIST_LITE_BIN_N_MAX	64

#define ISP32_RAWAF_CURVE_NUM		ISP3X_RAWAF_CURVE_NUM
#define ISP32_RAWAF_HIIR_COE_NUM	ISP3X_RAWAF_HIIR_COE_NUM
#define ISP32_RAWAF_VFIR_COE_NUM	ISP3X_RAWAF_VFIR_COE_NUM
#define ISP32_RAWAF_WIN_NUM		ISP3X_RAWAF_WIN_NUM
#define ISP32_RAWAF_LINE_NUM		ISP3X_RAWAF_LINE_NUM
#define ISP32_RAWAF_GAMMA_NUM		ISP3X_RAWAF_GAMMA_NUM
#define ISP32_RAWAF_SUMDATA_NUM		ISP3X_RAWAF_SUMDATA_NUM
#define ISP32_RAWAF_VIIR_COE_NUM	3
#define ISP32_RAWAF_GAUS_COE_NUM	9
#define ISP32L_RAWAF_WND_DATA		25

#define ISP32_DPCC_PDAF_POINT_NUM	ISP3X_DPCC_PDAF_POINT_NUM

#define ISP32_HDRMGE_L_CURVE_NUM	ISP3X_HDRMGE_L_CURVE_NUM
#define ISP32_HDRMGE_E_CURVE_NUM	ISP3X_HDRMGE_E_CURVE_NUM

#define ISP32_GIC_SIGMA_Y_NUM		ISP3X_GIC_SIGMA_Y_NUM

#define ISP32_CCM_CURVE_NUM		18

#define ISP32_3DLUT_DATA_NUM		ISP3X_3DLUT_DATA_NUM

#define ISP32_LDCH_MESH_XY_NUM		ISP3X_LDCH_MESH_XY_NUM
#define ISP32_LDCH_BIC_NUM		36

#define ISP32_GAMMA_OUT_MAX_SAMPLES     ISP3X_GAMMA_OUT_MAX_SAMPLES

#define ISP32_DHAZ_SIGMA_IDX_NUM	ISP3X_DHAZ_SIGMA_IDX_NUM
#define ISP32_DHAZ_SIGMA_LUT_NUM	ISP3X_DHAZ_SIGMA_LUT_NUM
#define ISP32_DHAZ_HIST_WR_NUM		ISP3X_DHAZ_HIST_WR_NUM
#define ISP32_DHAZ_ENH_CURVE_NUM	ISP3X_DHAZ_ENH_CURVE_NUM
#define ISP32_DHAZ_HIST_IIR_NUM		ISP3X_DHAZ_HIST_IIR_NUM
#define ISP32_DHAZ_ENH_LUMA_NUM		17

#define ISP32_DRC_Y_NUM			ISP3X_DRC_Y_NUM

#define ISP32_CNR_SIGMA_Y_NUM		ISP3X_CNR_SIGMA_Y_NUM
#define ISP32_CNR_GAUS_COE_NUM		6

#define ISP32_YNR_XY_NUM		ISP3X_YNR_XY_NUM
#define ISP32_YNR_NLM_COE_NUM		6

#define ISP32_BAYNR_XY_NUM		ISP3X_BAYNR_XY_NUM
#define ISP32_BAYNR_GAIN_NUM		16

#define ISP32_BAY3D_XY_NUM		ISP3X_BAY3D_XY_NUM

#define ISP32_SHARP_X_NUM		ISP3X_SHARP_X_NUM
#define ISP32_SHARP_Y_NUM		ISP3X_SHARP_Y_NUM
#define ISP32_SHARP_GAUS_COEF_NUM	ISP3X_SHARP_GAUS_COEF_NUM
#define ISP32_SHARP_GAIN_ADJ_NUM	14
#define ISP32_SHARP_STRENGTH_NUM	22

#define ISP32_CAC_STRENGTH_NUM		ISP3X_CAC_STRENGTH_NUM

#define ISP32_CSM_COEFF_NUM		ISP3X_CSM_COEFF_NUM

struct isp32_ldch_cfg {
	__u8 frm_end_dis;
	__u8 zero_interp_en;
	__u8 sample_avr_en;
	__u8 bic_mode_en;
	__u8 force_map_en;
	__u8 map13p3_en;

	__u8 bicubic[ISP32_LDCH_BIC_NUM];

	__u32 hsize;
	__u32 vsize;
	__s32 buf_fd;
} __attribute__ ((packed));

struct isp32_awb_gain_cfg {
	/* AWB1_GAIN_G */
	__u16 awb1_gain_gb;
	__u16 awb1_gain_gr;
	/* AWB1_GAIN_RB */
	__u16 awb1_gain_b;
	__u16 awb1_gain_r;
	/* AWB0_GAIN0_G */
	__u16 gain0_green_b;
	__u16 gain0_green_r;
	/* AWB0_GAIN0_RB*/
	__u16 gain0_blue;
	__u16 gain0_red;
	/* AWB0_GAIN1_G */
	__u16 gain1_green_b;
	__u16 gain1_green_r;
	/* AWB0_GAIN1_RB*/
	__u16 gain1_blue;
	__u16 gain1_red;
	/* AWB0_GAIN2_G */
	__u16 gain2_green_b;
	__u16 gain2_green_r;
	/* AWB0_GAIN2_RB*/
	__u16 gain2_blue;
	__u16 gain2_red;
} __attribute__ ((packed));

struct isp32_bls_cfg {
	__u8 enable_auto;
	__u8 en_windows;
	__u8 bls1_en;

	__u8 bls_samples;

	struct isp2x_window bls_window1;
	struct isp2x_window bls_window2;
	struct isp2x_bls_fixed_val fixed_val;
	struct isp2x_bls_fixed_val bls1_val;

	__u16 isp_ob_offset;
	__u16 isp_ob_predgain;
	__u32 isp_ob_max;
} __attribute__ ((packed));

struct isp32_ccm_cfg {
	/* CTRL */
	__u8 highy_adjust_dis;
	__u8 enh_adj_en;
	__u8 asym_adj_en;
	/* BOUND_BIT */
	__u8 bound_bit;
	__u8 right_bit;
	/* COEFF0_R */
	__s16 coeff0_r;
	__s16 coeff1_r;
	/* COEFF1_R */
	__s16 coeff2_r;
	__s16 offset_r;
	/* COEFF0_G */
	__s16 coeff0_g;
	__s16 coeff1_g;
	/* COEFF1_G */
	__s16 coeff2_g;
	__s16 offset_g;
	/* COEFF0_B */
	__s16 coeff0_b;
	__s16 coeff1_b;
	/* COEFF1_B */
	__s16 coeff2_b;
	__s16 offset_b;
	/* COEFF0_Y */
	__u16 coeff0_y;
	__u16 coeff1_y;
	/* COEFF1_Y */
	__u16 coeff2_y;
	/* ALP_Y */
	__u16 alp_y[ISP32_CCM_CURVE_NUM];
	/* ENHANCE0 */
	__u16 color_coef0_r2y;
	__u16 color_coef1_g2y;
	/* ENHANCE1 */
	__u16 color_coef2_b2y;
	__u16 color_enh_rat_max;
} __attribute__ ((packed));

struct isp32_debayer_cfg {
	/* CONTROL */
	__u8 filter_g_en;
	__u8 filter_c_en;
	/* G_INTERP */
	__u8 clip_en;
	__u8 dist_scale;
	__u8 thed0;
	__u8 thed1;
	__u8 select_thed;
	__u8 max_ratio;
	/* G_INTERP_FILTER1 */
	__s8 filter1_coe1;
	__s8 filter1_coe2;
	__s8 filter1_coe3;
	__s8 filter1_coe4;
	/* G_INTERP_FILTER2 */
	__s8 filter2_coe1;
	__s8 filter2_coe2;
	__s8 filter2_coe3;
	__s8 filter2_coe4;
	/* C_FILTER_GUIDE_GAUS */
	__s8 guid_gaus_coe0;
	__s8 guid_gaus_coe1;
	__s8 guid_gaus_coe2;
	/* C_FILTER_CE_GAUS */
	__s8 ce_gaus_coe0;
	__s8 ce_gaus_coe1;
	__s8 ce_gaus_coe2;
	/* C_FILTER_ALPHA_GAUS */
	__s8 alpha_gaus_coe0;
	__s8 alpha_gaus_coe1;
	__s8 alpha_gaus_coe2;
	/* C_FILTER_IIR_0 */
	__u8 ce_sgm;
	__u8 exp_shift;
	/* C_FILTER_IIR_1 */
	__u8 wet_clip;
	__u8 wet_ghost;
	/* C_FILTER_BF */
	__u8 bf_clip;
	__u8 bf_curwgt;
	__u16 bf_sgm;
	/* G_INTERP_OFFSET */
	__u16 hf_offset;
	__u16 gain_offset;
	/* G_FILTER_OFFSET */
	__u16 offset;
	/* C_FILTER_LOG_OFFSET */
	__u16 loghf_offset;
	__u16 loggd_offset;
	/* C_FILTER_IIR_0 */
	__u16 wgtslope;
	/* C_FILTER_ALPHA */
	__u16 alpha_offset;
	/* C_FILTER_EDGE */
	__u16 edge_offset;
	__u32 edge_scale;
	/* C_FILTER_ALPHA */
	__u32 alpha_scale;
} __attribute__ ((packed));

struct isp32_baynr_cfg {
	/* BAYNR_CTRL */
	__u8 bay3d_gain_en;
	__u8 lg2_mode;
	__u8 gauss_en;
	__u8 log_bypass;
	/* BAYNR_DGAIN */
	__u16 dgain1;
	__u16 dgain0;
	__u16 dgain2;
	/* BAYNR_PIXDIFF */
	__u16 pix_diff;
	/* BAYNR_THLD */
	__u16 diff_thld;
	__u16 softthld;
	/* BAYNR_W1_STRENG */
	__u16 bltflt_streng;
	__u16 reg_w1;
	/* BAYNR_SIGMA */
	__u16 sigma_x[ISP32_BAYNR_XY_NUM];
	__u16 sigma_y[ISP32_BAYNR_XY_NUM];
	/* BAYNR_WRIT_D */
	__u16 weit_d2;
	__u16 weit_d1;
	__u16 weit_d0;
	/* BAYNR_LG_OFF */
	__u16 lg2_lgoff;
	__u16 lg2_off;
	/* BAYNR_DAT_MAX */
	__u32 dat_max;
	/* BAYNR_SIGOFF */
	__u16 rgain_off;
	__u16 bgain_off;
	/* BAYNR_GAIN */
	__u8 gain_x[ISP32_BAYNR_GAIN_NUM];
	__u16 gain_y[ISP32_BAYNR_GAIN_NUM];
} __attribute__ ((packed));

struct isp32_bay3d_cfg {
	/* BAY3D_CTRL */
	__u8 bypass_en;
	__u8 hibypass_en;
	__u8 lobypass_en;
	__u8 himed_bypass_en;
	__u8 higaus_bypass_en;
	__u8 hiabs_possel;
	__u8 hichnsplit_en;
	__u8 lomed_bypass_en;
	__u8 logaus5_bypass_en;
	__u8 logaus3_bypass_en;
	__u8 glbpk_en;
	__u8 loswitch_protect;
	__u8 bwsaving_en;
	/* BAY3D_CTRL1 */
	__u8 hiwgt_opt_en;
	__u8 hichncor_en;
	__u8 bwopt_gain_dis;
	__u8 lo4x8_en;
	__u8 lo4x4_en;
	__u8 hisig_ind_sel;
	__u8 pksig_ind_sel;
	__u8 iirwr_rnd_en;
	__u8 curds_high_en;
	__u8 higaus3_mode;
	__u8 higaus5x5_en;
	__u8 wgtmix_opt_en;

	/* for isp32_lite */
	__u8 wgtmm_opt_en;
	__u8 wgtmm_sel_en;

	/* BAY3D_SIGGAUS */
	__u8 siggaus0;
	__u8 siggaus1;
	__u8 siggaus2;
	__u8 siggaus3;
	/* BAY3D_KALRATIO */
	__u16 softwgt;
	__u16 hidif_th;
	/* BAY3D_WGTLMT */
	__u16 wgtlmt;
	__u16 wgtratio;
	/* BAY3D_SIG */
	__u16 sig0_x[ISP32_BAY3D_XY_NUM];
	__u16 sig0_y[ISP32_BAY3D_XY_NUM];
	__u16 sig1_x[ISP32_BAY3D_XY_NUM];
	__u16 sig1_y[ISP32_BAY3D_XY_NUM];
	__u16 sig2_x[ISP32_BAY3D_XY_NUM];
	__u16 sig2_y[ISP32_BAY3D_XY_NUM];

	/* LODIF_STAT1 for isp32_lite */
	__u16 wgtmin;

	/* BAY3D_HISIGRAT */
	__u16 hisigrat0;
	__u16 hisigrat1;
	/* BAY3D_HISIGOFF */
	__u16 hisigoff0;
	__u16 hisigoff1;
	/* BAY3D_LOSIG */
	__u16 losigoff;
	__u16 losigrat;
	/* BAY3D_SIGPK */
	__u16 rgain_off;
	__u16 bgain_off;
	/* BAY3D_GLBPK2 */
	__u32 glbpk2;
} __attribute__ ((packed));

struct isp32_ynr_cfg {
	/* YNR_GLOBAL_CTRL */
	__u8 rnr_en;
	__u8 thumb_mix_cur_en;
	__u8 global_gain_alpha;
	__u8 flt1x1_bypass_sel;
	__u8 nlm11x11_bypass;
	__u8 flt1x1_bypass;
	__u8 lgft3x3_bypass;
	__u8 lbft5x5_bypass;
	__u8 bft3x3_bypass;
	/* YNR_RNR_STRENGTH */
	__u8 rnr_strength3[ISP32_YNR_XY_NUM];
	/* YNR_NLM_SIGMA_GAIN */
	__u8 nlm_hi_gain_alpha;
	/* YNR_NLM_COE */
	__u8 nlm_coe[ISP32_YNR_NLM_COE_NUM];

	/* LOWNR_CTRL4 for isp32_lite */
	__u8 frame_add4line;

	__u16 global_gain;

	/* YNR_RNR_MAX_R */
	__u16 rnr_max_r;
	__u16 local_gainscale;
	/* YNR_RNR_CENTER_COOR */
	__u16 rnr_center_coorh;
	__u16 rnr_center_coorv;
	/* YNR_LOCAL_GAIN_CTRL */
	__u16 loclagain_adj_thresh;
	__u16 localgain_adj;
	/* YNR_LOWNR_CTRL0 */
	__u16 low_bf_inv1;
	__u16 low_bf_inv0;
	/* YNR_LOWNR_CTRL1 */
	__u16 low_peak_supress;
	__u16 low_thred_adj;
	/* YNR_LOWNR_CTRL2 */
	__u16 low_dist_adj;
	__u16 low_edge_adj_thresh;
	/* YNR_LOWNR_CTRL3 */
	__u16 low_bi_weight;
	__u16 low_weight;
	__u16 low_center_weight;
	/* YNR_LOWNR_CTRL4 */
	__u16 frame_full_size;
	__u16 lbf_weight_thres;
	/* YNR_GAUSS1_COEFF */
	__u16 low_gauss1_coeff2;
	__u16 low_gauss1_coeff1;
	__u16 low_gauss1_coeff0;
	/* YNR_GAUSS2_COEFF */
	__u16 low_gauss2_coeff2;
	__u16 low_gauss2_coeff1;
	__u16 low_gauss2_coeff0;
	/* YNR_SGM_DX */
	__u16 luma_points_x[ISP32_YNR_XY_NUM];
	/* YNR_LSGM_Y */
	__u16 lsgm_y[ISP32_YNR_XY_NUM];
	/* YNR_NLM_SIGMA_GAIN */
	__u16 nlm_min_sigma;
	__u16 nlm_hi_bf_scale;
	/* YNR_NLM_WEIGHT */
	__u16 nlm_nr_weight;
	__u16 nlm_weight_offset;
	/* YNR_NLM_NR_WEIGHT */
	__u32 nlm_center_weight;
} __attribute__ ((packed));

struct isp32_cnr_cfg {
	/* CNR_CTRL */
	__u8 exgain_bypass;
	__u8 yuv422_mode;
	__u8 thumb_mode;
	__u8 bf3x3_wgt0_sel;
	/* CNR_LBF_WEITD */
	__u8 lbf1x7_weit_d0;
	__u8 lbf1x7_weit_d1;
	__u8 lbf1x7_weit_d2;
	__u8 lbf1x7_weit_d3;
	/* CNR_IIR_PARA1 */
	__u8 iir_uvgain;
	__u8 iir_strength;
	__u8 exp_shift;
	/* CNR_IIR_PARA2 */
	__u8 chroma_ghost;
	__u8 iir_uv_clip;
	/* CNR_GAUS_COE */
	__u8 gaus_coe[ISP32_CNR_GAUS_COE_NUM];
	/* CNR_GAUS_RATIO */
	__u8 bf_wgt_clip;
	/* CNR_BF_PARA1 */
	__u8 uv_gain;
	__u8 bf_ratio;
	/* CNR_SIGMA */
	__u8 sigma_y[ISP32_CNR_SIGMA_Y_NUM];
	/* CNR_IIR_GLOBAL_GAIN */
	__u8 iir_gain_alpha;
	__u8 iir_global_gain;
	/* CNR_EXGAIN */
	__u8 gain_iso;
	__u8 global_gain_alpha;
	__u16 global_gain;
	/* CNR_THUMB1 */
	__u16 thumb_sigma_c;
	__u16 thumb_sigma_y;
	/* CNR_THUMB_BF_RATIO */
	__u16 thumb_bf_ratio;
	/* CNR_IIR_PARA1 */
	__u16 wgt_slope;
	/* CNR_GAUS_RATIO */
	__u16 gaus_ratio;
	__u16 global_alpha;
	/* CNR_BF_PARA1 */
	__u16 sigma_r;
	/* CNR_BF_PARA2 */
	__u16 adj_offset;
	__u16 adj_ratio;
} __attribute__ ((packed));

struct isp32_sharp_cfg {
	/* SHARP_EN */
	__u8 bypass;
	__u8 center_mode;
	__u8 exgain_bypass;
	__u8 radius_ds_mode;
	__u8 noiseclip_mode;

	/* for isp32_lite */
	__u8 clip_hf_mode;
	__u8 add_mode;

	/* SHARP_RATIO */
	__u8 sharp_ratio;
	__u8 bf_ratio;
	__u8 gaus_ratio;
	__u8 pbf_ratio;
	/* SHARP_LUMA_DX */
	__u8 luma_dx[ISP32_SHARP_X_NUM];
	/* SHARP_SIGMA_SHIFT */
	__u8 bf_sigma_shift;
	__u8 pbf_sigma_shift;
	/* SHARP_PBF_COEF */
	__u8 pbf_coef2;
	__u8 pbf_coef1;
	__u8 pbf_coef0;
	/* SHARP_BF_COEF */
	__u8 bf_coef2;
	__u8 bf_coef1;
	__u8 bf_coef0;
	/* SHARP_GAUS_COEF */
	__u8 gaus_coef[ISP32_SHARP_GAUS_COEF_NUM];
	/* SHARP_GAIN */
	__u8 global_gain_alpha;
	__u8 local_gainscale;
	/* SHARP_GAIN_DIS_STRENGTH */
	__u8 strength[ISP32_SHARP_STRENGTH_NUM];
	/* SHARP_TEXTURE */
	__u8 enhance_bit;
	/* SHARP_PBF_SIGMA_INV */
	__u16 pbf_sigma_inv[ISP32_SHARP_Y_NUM];
	/* SHARP_BF_SIGMA_INV */
	__u16 bf_sigma_inv[ISP32_SHARP_Y_NUM];
	/* SHARP_CLIP_HF */
	__u16 clip_hf[ISP32_SHARP_Y_NUM];
	/* SHARP_GAIN */
	__u16 global_gain;
	/* SHARP_GAIN_ADJUST */
	__u16 gain_adj[ISP32_SHARP_GAIN_ADJ_NUM];
	/* SHARP_CENTER */
	__u16 center_wid;
	__u16 center_het;
	/* SHARP_TEXTURE */
	__u16 noise_sigma;
	__u16 noise_strength;

	/* EHF_TH for isp32_lite */
	__u16 ehf_th[ISP32_SHARP_Y_NUM];
	/* CLIP_NEG for isp32_lite */
	__u16 clip_neg[ISP32_SHARP_Y_NUM];
} __attribute__ ((packed));

struct isp32_dhaz_cfg {
	/* DHAZ_CTRL */
	__u8 enh_luma_en;
	__u8 color_deviate_en;
	__u8 round_en;
	__u8 soft_wr_en;
	__u8 enhance_en;
	__u8 air_lc_en;
	__u8 hpara_en;
	__u8 hist_en;
	__u8 dc_en;
	/* DHAZ_ADP0 */
	__u8 yblk_th;
	__u8 yhist_th;
	__u8 dc_max_th;
	__u8 dc_min_th;
	/* DHAZ_ADP2 */
	__u8 tmax_base;
	__u8 dark_th;
	__u8 air_max;
	__u8 air_min;
	/* DHAZ_GAUS */
	__u8 gaus_h2;
	__u8 gaus_h1;
	__u8 gaus_h0;
	/* DHAZ_GAIN_IDX */
	__u8 sigma_idx[ISP32_DHAZ_SIGMA_IDX_NUM];
	/* DHAZ_ADP_HIST1 */
	__u8 hist_gratio;
	__u16 hist_scale;
	/* DHAZ_ADP1 */
	__u8 bright_max;
	__u8 bright_min;
	__u16 wt_max;
	/* DHAZ_ADP_TMAX */
	__u16 tmax_max;
	__u16 tmax_off;
	/* DHAZ_ADP_HIST0 */
	__u8 hist_k;
	__u8 hist_th_off;
	__u16 hist_min;
	/* DHAZ_ENHANCE */
	__u16 enhance_value;
	__u16 enhance_chroma;
	/* DHAZ_IIR0 */
	__u16 iir_wt_sigma;
	__u8 iir_sigma;
	__u8 stab_fnum;
	/* DHAZ_IIR1 */
	__u16 iir_tmax_sigma;
	__u8 iir_air_sigma;
	__u8 iir_pre_wet;
	/* DHAZ_SOFT_CFG0 */
	__u16 cfg_wt;
	__u8 cfg_air;
	__u8 cfg_alpha;
	/* DHAZ_SOFT_CFG1 */
	__u16 cfg_gratio;
	__u16 cfg_tmax;
	/* DHAZ_BF_SIGMA */
	__u16 range_sima;
	__u8 space_sigma_pre;
	__u8 space_sigma_cur;
	/* DHAZ_BF_WET */
	__u16 dc_weitcur;
	__u16 bf_weight;
	/* DHAZ_ENH_CURVE */
	__u16 enh_curve[ISP32_DHAZ_ENH_CURVE_NUM];

	__u16 sigma_lut[ISP32_DHAZ_SIGMA_LUT_NUM];

	__u16 hist_wr[ISP32_DHAZ_HIST_WR_NUM];

	__u16 enh_luma[ISP32_DHAZ_ENH_LUMA_NUM];
} __attribute__ ((packed));

struct isp32_drc_cfg {
	__u8 bypass_en;
	/* DRC_CTRL1 */
	__u8 offset_pow2;
	__u16 compres_scl;
	__u16 position;
	/* DRC_LPRATIO */
	__u16 hpdetail_ratio;
	__u16 lpdetail_ratio;
	__u8 delta_scalein;
	/* DRC_EXPLRATIO */
	__u8 weicur_pix;
	__u8 weipre_frame;
	__u8 bilat_wt_off;
	/* DRC_SIGMA */
	__u8 edge_scl;
	__u8 motion_scl;
	__u16 force_sgm_inv0;
	/* DRC_SPACESGM */
	__u16 space_sgm_inv1;
	__u16 space_sgm_inv0;
	/* DRC_RANESGM */
	__u16 range_sgm_inv1;
	__u16 range_sgm_inv0;
	/* DRC_BILAT */
	__u16 bilat_soft_thd;
	__u8 weig_maxl;
	__u8 weig_bilat;
	__u8 enable_soft_thd;
	/* DRC_IIRWG_GAIN */
	__u8 iir_weight;
	__u16 min_ogain;
	/* DRC_LUM3X2_CTRL */
	__u16 gas_t;
	/* DRC_LUM3X2_GAS */
	__u8 gas_l0;
	__u8 gas_l1;
	__u8 gas_l2;
	__u8 gas_l3;

	__u16 gain_y[ISP32_DRC_Y_NUM];
	__u16 compres_y[ISP32_DRC_Y_NUM];
	__u16 scale_y[ISP32_DRC_Y_NUM];
} __attribute__ ((packed));

struct isp32_hdrmge_cfg {
	__u8 s_base;
	__u8 mode;
	__u8 dbg_mode;
	__u8 each_raw_en;

	__u8 gain2;

	__u8 lm_dif_0p15;
	__u8 lm_dif_0p9;
	__u8 ms_diff_0p15;
	__u8 ms_dif_0p8;

	__u16 gain0_inv;
	__u16 gain0;
	__u16 gain1_inv;
	__u16 gain1;

	__u16 ms_thd1;
	__u16 ms_thd0;
	__u16 ms_scl;
	__u16 lm_thd1;
	__u16 lm_thd0;
	__u16 lm_scl;
	struct isp2x_hdrmge_curve curve;
	__u16 e_y[ISP32_HDRMGE_E_CURVE_NUM];
	__u16 l_raw0[ISP32_HDRMGE_E_CURVE_NUM];
	__u16 l_raw1[ISP32_HDRMGE_E_CURVE_NUM];
	__u16 each_raw_gain0;
	__u16 each_raw_gain1;
} __attribute__ ((packed));

struct isp32_rawawb_meas_cfg {
	__u8 bls2_en;

	__u8 rawawb_sel;
	__u8 bnr2awb_sel;
	__u8 drc2awb_sel;
	/* RAWAWB_CTRL */
	__u8 uv_en0;
	__u8 xy_en0;
	__u8 yuv3d_en0;
	__u8 yuv3d_ls_idx0;
	__u8 yuv3d_ls_idx1;
	__u8 yuv3d_ls_idx2;
	__u8 yuv3d_ls_idx3;
	__u8 in_rshift_to_12bit_en;
	__u8 in_overexposure_check_en;
	__u8 wind_size;
	__u8 rawlsc_bypass_en;
	__u8 light_num;
	__u8 uv_en1;
	__u8 xy_en1;
	__u8 yuv3d_en1;
	__u8 low12bit_val;
	/* RAWAWB_WEIGHT_CURVE_CTRL */
	__u8 wp_luma_wei_en0;
	__u8 wp_luma_wei_en1;
	__u8 wp_blk_wei_en0;
	__u8 wp_blk_wei_en1;
	__u8 wp_hist_xytype;
	/* RAWAWB_MULTIWINDOW_EXC_CTRL */
	__u8 exc_wp_region0_excen;
	__u8 exc_wp_region0_measen;
	__u8 exc_wp_region0_domain;
	__u8 exc_wp_region1_excen;
	__u8 exc_wp_region1_measen;
	__u8 exc_wp_region1_domain;
	__u8 exc_wp_region2_excen;
	__u8 exc_wp_region2_measen;
	__u8 exc_wp_region2_domain;
	__u8 exc_wp_region3_excen;
	__u8 exc_wp_region3_measen;
	__u8 exc_wp_region3_domain;
	__u8 exc_wp_region4_excen;
	__u8 exc_wp_region4_domain;
	__u8 exc_wp_region5_excen;
	__u8 exc_wp_region5_domain;
	__u8 exc_wp_region6_excen;
	__u8 exc_wp_region6_domain;
	__u8 multiwindow_en;
	/* RAWAWB_YWEIGHT_CURVE_XCOOR03 */
	__u8 wp_luma_weicurve_y0;
	__u8 wp_luma_weicurve_y1;
	__u8 wp_luma_weicurve_y2;
	__u8 wp_luma_weicurve_y3;
	/* RAWAWB_YWEIGHT_CURVE_XCOOR47 */
	__u8 wp_luma_weicurve_y4;
	__u8 wp_luma_weicurve_y5;
	__u8 wp_luma_weicurve_y6;
	__u8 wp_luma_weicurve_y7;
	/* RAWAWB_YWEIGHT_CURVE_XCOOR8 */
	__u8 wp_luma_weicurve_y8;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR03 */
	__u8 wp_luma_weicurve_w0;
	__u8 wp_luma_weicurve_w1;
	__u8 wp_luma_weicurve_w2;
	__u8 wp_luma_weicurve_w3;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR47 */
	__u8 wp_luma_weicurve_w4;
	__u8 wp_luma_weicurve_w5;
	__u8 wp_luma_weicurve_w6;
	__u8 wp_luma_weicurve_w7;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR8 */
	__u8 wp_luma_weicurve_w8;
	/* RAWAWB_YUV_X1X2_DIS_0 */
	__u8 dis_x1x2_ls0;
	__u8 rotu0_ls0;
	__u8 rotu1_ls0;
	/* RAWAWB_YUV_INTERP_CURVE_UCOOR_0 */
	__u8 rotu2_ls0;
	__u8 rotu3_ls0;
	__u8 rotu4_ls0;
	__u8 rotu5_ls0;
	/* RAWAWB_YUV_X1X2_DIS_1 */
	__u8 dis_x1x2_ls1;
	__u8 rotu0_ls1;
	__u8 rotu1_ls1;
	/* YUV_INTERP_CURVE_UCOOR_1 */
	__u8 rotu2_ls1;
	__u8 rotu3_ls1;
	__u8 rotu4_ls1;
	__u8 rotu5_ls1;
	/* RAWAWB_YUV_X1X2_DIS_2 */
	__u8 dis_x1x2_ls2;
	__u8 rotu0_ls2;
	__u8 rotu1_ls2;
	/* YUV_INTERP_CURVE_UCOOR_2 */
	__u8 rotu2_ls2;
	__u8 rotu3_ls2;
	__u8 rotu4_ls2;
	__u8 rotu5_ls2;
	/* RAWAWB_YUV_X1X2_DIS_3 */
	__u8 dis_x1x2_ls3;
	__u8 rotu0_ls3;
	__u8 rotu1_ls3;
	__u8 rotu2_ls3;
	__u8 rotu3_ls3;
	__u8 rotu4_ls3;
	__u8 rotu5_ls3;
	/* RAWAWB_EXC_WP_WEIGHT */
	__u8 exc_wp_region0_weight;
	__u8 exc_wp_region1_weight;
	__u8 exc_wp_region2_weight;
	__u8 exc_wp_region3_weight;
	__u8 exc_wp_region4_weight;
	__u8 exc_wp_region5_weight;
	__u8 exc_wp_region6_weight;
	/* RAWAWB_WRAM_DATA */
	__u8 wp_blk_wei_w[ISP32_RAWAWB_WEIGHT_NUM];
	/* RAWAWB_BLK_CTRL */
	__u8 blk_measure_enable;
	__u8 blk_measure_mode;
	__u8 blk_measure_xytype;
	__u8 blk_rtdw_measure_en;
	__u8 blk_measure_illu_idx;

	/* for isp32_lite */
	__u8 ds16x8_mode_en;

	__u8 blk_with_luma_wei_en;
	__u16 in_overexposure_threshold;
	/* RAWAWB_LIMIT_RG_MAX*/
	__u16 r_max;
	__u16 g_max;
	/* RAWAWB_LIMIT_BY_MAX */
	__u16 b_max;
	__u16 y_max;
	/* RAWAWB_LIMIT_RG_MIN */
	__u16 r_min;
	__u16 g_min;
	/* RAWAWB_LIMIT_BY_MIN */
	__u16 b_min;
	__u16 y_min;
	/* RAWAWB_WIN_OFFS */
	__u16 h_offs;
	__u16 v_offs;
	/* RAWAWB_WIN_SIZE */
	__u16 h_size;
	__u16 v_size;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR8 */
	__u16 pre_wbgain_inv_r;
	/* RAWAWB_PRE_WBGAIN_INV */
	__u16 pre_wbgain_inv_g;
	__u16 pre_wbgain_inv_b;
	/* RAWAWB_UV_DETC_VERTEX */
	__u16 vertex0_u_0;
	__u16 vertex0_v_0;

	__u16 vertex1_u_0;
	__u16 vertex1_v_0;

	__u16 vertex2_u_0;
	__u16 vertex2_v_0;

	__u16 vertex3_u_0;
	__u16 vertex3_v_0;

	__u16 vertex0_u_1;
	__u16 vertex0_v_1;

	__u16 vertex1_u_1;
	__u16 vertex1_v_1;

	__u16 vertex2_u_1;
	__u16 vertex2_v_1;

	__u16 vertex3_u_1;
	__u16 vertex3_v_1;

	__u16 vertex0_u_2;
	__u16 vertex0_v_2;

	__u16 vertex1_u_2;
	__u16 vertex1_v_2;

	__u16 vertex2_u_2;
	__u16 vertex2_v_2;

	__u16 vertex3_u_2;
	__u16 vertex3_v_2;

	__u16 vertex0_u_3;
	__u16 vertex0_v_3;

	__u16 vertex1_u_3;
	__u16 vertex1_v_3;

	__u16 vertex2_u_3;
	__u16 vertex2_v_3;

	__u16 vertex3_u_3;
	__u16 vertex3_v_3;
	/* RAWAWB_RGB2XY_WT */
	__u16 wt0;
	__u16 wt1;
	__u16 wt2;
	/* RAWAWB_RGB2XY_MAT */
	__u16 mat0_x;
	__u16 mat0_y;

	__u16 mat1_x;
	__u16 mat1_y;

	__u16 mat2_x;
	__u16 mat2_y;
	/* RAWAWB_XY_DETC_NOR */
	__u16 nor_x0_0;
	__u16 nor_x1_0;
	__u16 nor_y0_0;
	__u16 nor_y1_0;

	__u16 nor_x0_1;
	__u16 nor_x1_1;
	__u16 nor_y0_1;
	__u16 nor_y1_1;

	__u16 nor_x0_2;
	__u16 nor_x1_2;
	__u16 nor_y0_2;
	__u16 nor_y1_2;

	__u16 nor_x0_3;
	__u16 nor_x1_3;
	__u16 nor_y0_3;
	__u16 nor_y1_3;
	/* RAWAWB_XY_DETC_BIG */
	__u16 big_x0_0;
	__u16 big_x1_0;
	__u16 big_y0_0;
	__u16 big_y1_0;

	__u16 big_x0_1;
	__u16 big_x1_1;
	__u16 big_y0_1;
	__u16 big_y1_1;

	__u16 big_x0_2;
	__u16 big_x1_2;
	__u16 big_y0_2;
	__u16 big_y1_2;

	__u16 big_x0_3;
	__u16 big_x1_3;
	__u16 big_y0_3;
	__u16 big_y1_3;
	/* RAWAWB_MULTIWINDOW */
	__u16 multiwindow0_v_offs;
	__u16 multiwindow0_h_offs;
	__u16 multiwindow0_v_size;
	__u16 multiwindow0_h_size;

	__u16 multiwindow1_v_offs;
	__u16 multiwindow1_h_offs;
	__u16 multiwindow1_v_size;
	__u16 multiwindow1_h_size;

	__u16 multiwindow2_v_offs;
	__u16 multiwindow2_h_offs;
	__u16 multiwindow2_v_size;
	__u16 multiwindow2_h_size;

	__u16 multiwindow3_v_offs;
	__u16 multiwindow3_h_offs;
	__u16 multiwindow3_v_size;
	__u16 multiwindow3_h_size;
	/* RAWAWB_EXC_WP_REGION */
	__u16 exc_wp_region0_xu0;
	__u16 exc_wp_region0_xu1;

	__u16 exc_wp_region0_yv0;
	__u16 exc_wp_region0_yv1;

	__u16 exc_wp_region1_xu0;
	__u16 exc_wp_region1_xu1;

	__u16 exc_wp_region1_yv0;
	__u16 exc_wp_region1_yv1;

	__u16 exc_wp_region2_xu0;
	__u16 exc_wp_region2_xu1;

	__u16 exc_wp_region2_yv0;
	__u16 exc_wp_region2_yv1;

	__u16 exc_wp_region3_xu0;
	__u16 exc_wp_region3_xu1;

	__u16 exc_wp_region3_yv0;
	__u16 exc_wp_region3_yv1;

	__u16 exc_wp_region4_xu0;
	__u16 exc_wp_region4_xu1;

	__u16 exc_wp_region4_yv0;
	__u16 exc_wp_region4_yv1;

	__u16 exc_wp_region5_xu0;
	__u16 exc_wp_region5_xu1;

	__u16 exc_wp_region5_yv0;
	__u16 exc_wp_region5_yv1;

	__u16 exc_wp_region6_xu0;
	__u16 exc_wp_region6_xu1;

	__u16 exc_wp_region6_yv0;
	__u16 exc_wp_region6_yv1;
	/* RAWAWB_YUV_RGB2ROTY */
	__u16 rgb2ryuvmat0_y;
	__u16 rgb2ryuvmat1_y;
	__u16 rgb2ryuvmat2_y;
	__u16 rgb2ryuvofs_y;
	/* RAWAWB_YUV_RGB2ROTU */
	__u16 rgb2ryuvmat0_u;
	__u16 rgb2ryuvmat1_u;
	__u16 rgb2ryuvmat2_u;
	__u16 rgb2ryuvofs_u;
	/* RAWAWB_YUV_RGB2ROTV */
	__u16 rgb2ryuvmat0_v;
	__u16 rgb2ryuvmat1_v;
	__u16 rgb2ryuvmat2_v;
	__u16 rgb2ryuvofs_v;
	/* RAWAWB_YUV_X_COOR */
	__u16 coor_x1_ls0_y;
	__u16 vec_x21_ls0_y;
	__u16 coor_x1_ls0_u;
	__u16 vec_x21_ls0_u;
	__u16 coor_x1_ls0_v;
	__u16 vec_x21_ls0_v;

	__u16 coor_x1_ls1_y;
	__u16 vec_x21_ls1_y;
	__u16 coor_x1_ls1_u;
	__u16 vec_x21_ls1_u;
	__u16 coor_x1_ls1_v;
	__u16 vec_x21_ls1_v;

	__u16 coor_x1_ls2_y;
	__u16 vec_x21_ls2_y;
	__u16 coor_x1_ls2_u;
	__u16 vec_x21_ls2_v;
	__u16 coor_x1_ls2_v;
	__u16 vec_x21_ls2_u;

	__u16 coor_x1_ls3_y;
	__u16 vec_x21_ls3_y;
	__u16 coor_x1_ls3_u;
	__u16 vec_x21_ls3_u;
	__u16 coor_x1_ls3_v;
	__u16 vec_x21_ls3_v;
	/* RAWAWB_YUV_INTERP_CURVE_TH */
	__u16 th0_ls0;
	__u16 th1_ls0;
	__u16 th2_ls0;
	__u16 th3_ls0;
	__u16 th4_ls0;
	__u16 th5_ls0;

	__u16 th0_ls1;
	__u16 th1_ls1;
	__u16 th2_ls1;
	__u16 th3_ls1;
	__u16 th4_ls1;
	__u16 th5_ls1;

	__u16 th0_ls2;
	__u16 th1_ls2;
	__u16 th2_ls2;
	__u16 th3_ls2;
	__u16 th4_ls2;
	__u16 th5_ls2;

	__u16 th0_ls3;
	__u16 th1_ls3;
	__u16 th2_ls3;
	__u16 th3_ls3;
	__u16 th4_ls3;
	__u16 th5_ls3;
	/* RAWAWB_UV_DETC_ISLOPE */
	__u32 islope01_0;
	__u32 islope12_0;
	__u32 islope23_0;
	__u32 islope30_0;
	__u32 islope01_1;
	__u32 islope12_1;
	__u32 islope23_1;
	__u32 islope30_1;
	__u32 islope01_2;
	__u32 islope12_2;
	__u32 islope23_2;
	__u32 islope30_2;
	__u32 islope01_3;
	__u32 islope12_3;
	__u32 islope23_3;
	__u32 islope30_3;

	/* WIN_WEIGHT for isp32_lite */
	__u32 win_weight[ISP32L_RAWAWB_WEIGHT_NUM];
	struct isp2x_bls_fixed_val bls2_val;
} __attribute__ ((packed));

struct isp32_rawaf_meas_cfg {
	__u8 rawaf_sel;
	__u8 num_afm_win;
	/* for isp32_lite */
	__u8 bnr2af_sel;

	/* CTRL */
	__u8 gamma_en;
	__u8 gaus_en;
	__u8 v1_fir_sel;
	__u8 hiir_en;
	__u8 viir_en;
	__u8 accu_8bit_mode;
	__u8 ldg_en;
	__u8 h1_fv_mode;
	__u8 h2_fv_mode;
	__u8 v1_fv_mode;
	__u8 v2_fv_mode;
	__u8 ae_mode;
	__u8 y_mode;
	__u8 vldg_sel;
	__u8 sobel_sel;
	__u8 v_dnscl_mode;
	__u8 from_awb;
	__u8 from_ynr;
	__u8 ae_config_use;
	/* for isp32_lite */
	__u8 ae_sel;

	/* for isp32_lite */
	__u8 hiir_left_border_mode;
	__u8 avg_ds_en;
	__u8 avg_ds_mode;
	__u8 h1_acc_mode;
	__u8 h2_acc_mode;
	__u8 v1_acc_mode;
	__u8 v2_acc_mode;

	/* CTRL1 for isp32_lite */
	__s16 bls_offset;
	__u8 bls_en;
	__u8 hldg_dilate_num;

	/* WINA_B */
	struct isp2x_window win[ISP32_RAWAF_WIN_NUM];
	/* INT_LINE */
	__u8 line_num[ISP32_RAWAF_LINE_NUM];
	__u8 line_en[ISP32_RAWAF_LINE_NUM];
	/* THRES */
	__u16 afm_thres;
	/* VAR_SHIFT */
	__u8 afm_var_shift[ISP32_RAWAF_WIN_NUM];
	__u8 lum_var_shift[ISP32_RAWAF_WIN_NUM];
	/* for isp32_lite */
	__u8 tnrin_shift;

	/* HVIIR_VAR_SHIFT */
	__u8 h1iir_var_shift;
	__u8 h2iir_var_shift;
	__u8 v1iir_var_shift;
	__u8 v2iir_var_shift;
	/* GAUS_COE */
	__s8 gaus_coe[ISP32_RAWAF_GAUS_COE_NUM];

	/* GAMMA_Y */
	__u16 gamma_y[ISP32_RAWAF_GAMMA_NUM];
	/* HIIR_THRESH */
	__u16 h_fv_thresh;
	__u16 v_fv_thresh;
	struct isp3x_rawaf_curve curve_h[ISP32_RAWAF_CURVE_NUM];
	struct isp3x_rawaf_curve curve_v[ISP32_RAWAF_CURVE_NUM];
	__s16 h1iir1_coe[ISP32_RAWAF_HIIR_COE_NUM];
	__s16 h1iir2_coe[ISP32_RAWAF_HIIR_COE_NUM];
	__s16 h2iir1_coe[ISP32_RAWAF_HIIR_COE_NUM];
	__s16 h2iir2_coe[ISP32_RAWAF_HIIR_COE_NUM];
	__s16 v1iir_coe[ISP32_RAWAF_VIIR_COE_NUM];
	__s16 v2iir_coe[ISP32_RAWAF_VIIR_COE_NUM];
	__s16 v1fir_coe[ISP32_RAWAF_VFIR_COE_NUM];
	__s16 v2fir_coe[ISP32_RAWAF_VFIR_COE_NUM];
	__u16 highlit_thresh;

	/* CORING_H for isp32_lite */
	__u16 h_fv_limit;
	__u16 h_fv_slope;
	/* CORING_V for isp32_lite */
	__u16 v_fv_limit;
	__u16 v_fv_slope;
} __attribute__ ((packed));

struct isp32_cac_cfg {
	__u8 bypass_en;
	__u8 center_en;
	__u8 clip_g_mode;
	__u8 edge_detect_en;
	__u8 neg_clip0_en;

	__u8 flat_thed_b;
	__u8 flat_thed_r;

	__u8 psf_sft_bit;
	__u16 cfg_num;

	__u16 center_width;
	__u16 center_height;

	__u16 strength[ISP32_CAC_STRENGTH_NUM];

	__u16 offset_b;
	__u16 offset_r;

	__u32 expo_thed_b;
	__u32 expo_thed_r;
	__u32 expo_adj_b;
	__u32 expo_adj_r;

	__u32 hsize;
	__u32 vsize;
	__s32 buf_fd;
} __attribute__ ((packed));

struct isp32_vsm_cfg {
	__u8 h_segments;
	__u8 v_segments;
	__u16 h_offs;
	__u16 v_offs;
	__u16 h_size;
	__u16 v_size;
} __attribute__ ((packed));

struct isp32_isp_other_cfg {
	struct isp32_bls_cfg bls_cfg;
	struct isp2x_dpcc_cfg dpcc_cfg;
	struct isp3x_lsc_cfg lsc_cfg;
	struct isp32_awb_gain_cfg awb_gain_cfg;
	struct isp21_gic_cfg gic_cfg;
	struct isp32_debayer_cfg debayer_cfg;
	struct isp32_ccm_cfg ccm_cfg;
	struct isp3x_gammaout_cfg gammaout_cfg;
	struct isp2x_cproc_cfg cproc_cfg;
	struct isp2x_ie_cfg ie_cfg;
	struct isp2x_sdg_cfg sdg_cfg;
	struct isp32_drc_cfg drc_cfg;
	struct isp32_hdrmge_cfg hdrmge_cfg;
	struct isp32_dhaz_cfg dhaz_cfg;
	struct isp2x_3dlut_cfg isp3dlut_cfg;
	struct isp32_ldch_cfg ldch_cfg;
	struct isp32_baynr_cfg baynr_cfg;
	struct isp32_bay3d_cfg bay3d_cfg;
	struct isp32_ynr_cfg ynr_cfg;
	struct isp32_cnr_cfg cnr_cfg;
	struct isp32_sharp_cfg sharp_cfg;
	struct isp32_cac_cfg cac_cfg;
	struct isp3x_gain_cfg gain_cfg;
	struct isp21_csm_cfg csm_cfg;
	struct isp21_cgc_cfg cgc_cfg;
	struct isp32_vsm_cfg vsm_cfg;
} __attribute__ ((packed));

struct isp32_isp_meas_cfg {
	struct isp32_rawaf_meas_cfg rawaf;
	struct isp32_rawawb_meas_cfg rawawb;
	struct isp2x_rawaelite_meas_cfg rawae0;
	struct isp2x_rawaebig_meas_cfg rawae1;
	struct isp2x_rawaebig_meas_cfg rawae2;
	struct isp2x_rawaebig_meas_cfg rawae3;
	struct isp2x_rawhistlite_cfg rawhist0;
	struct isp2x_rawhistbig_cfg rawhist1;
	struct isp2x_rawhistbig_cfg rawhist2;
	struct isp2x_rawhistbig_cfg rawhist3;
} __attribute__ ((packed));

struct isp32_rawae_meas_data {
	__u32 channelg_xy:12;
	__u32 channelb_xy:10;
	__u32 channelr_xy:10;
} __attribute__ ((packed));

struct isp32_rawaebig_stat0 {
	struct isp32_rawae_meas_data data[ISP32_RAWAEBIG_MEAN_NUM];
	__u32 reserved[3];
} __attribute__ ((packed));

struct isp32_rawaebig_stat1 {
	__u32 sumr[ISP32_RAWAEBIG_SUBWIN_NUM];
	__u32 sumg[ISP32_RAWAEBIG_SUBWIN_NUM];
	__u32 sumb[ISP32_RAWAEBIG_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp32_rawaelite_stat {
	struct isp32_rawae_meas_data data[ISP32_RAWAELITE_MEAN_NUM];
	__u32 reserved[21];
} __attribute__ ((packed));

struct isp32_rawaf_stat {
	struct isp3x_rawaf_ramdata ramdata[ISP32_RAWAF_SUMDATA_NUM];
	__u32 int_state;
	__u32 afm_sum_b;
	__u32 afm_lum_b;
	__u32 highlit_cnt_winb;
	__u32 reserved[18];
} __attribute__ ((packed));

struct isp32_rawawb_ramdata {
	__u64 b:18;
	__u64 g:18;
	__u64 r:18;
	__u64 wp:10;
} __attribute__ ((packed));

struct isp32_rawawb_sum {
	__u32 rgain_nor;
	__u32 bgain_nor;
	__u32 wp_num_nor;
	__u32 wp_num2;

	__u32 rgain_big;
	__u32 bgain_big;
	__u32 wp_num_big;
	__u32 reserved;
} __attribute__ ((packed));

struct isp32_rawawb_sum_exc {
	__u32 rgain_exc;
	__u32 bgain_exc;
	__u32 wp_num_exc;
	__u32 reserved;
} __attribute__ ((packed));

struct isp32_rawawb_meas_stat {
	struct isp32_rawawb_ramdata ramdata[ISP32_RAWAWB_RAMDATA_NUM];
	__u64 reserved;
	struct isp32_rawawb_sum sum[ISP32_RAWAWB_SUM_NUM];
	__u16 yhist_bin[ISP32_RAWAWB_HSTBIN_NUM];
	struct isp32_rawawb_sum_exc sum_exc[ISP32_RAWAWB_EXCL_STAT_NUM];
} __attribute__ ((packed));

struct isp32_vsm_stat {
	__u16 delta_h;
	__u16 delta_v;
} __attribute__ ((packed));

struct isp32_info2ddr_stat {
	__u32 owner;
	__s32 buf_fd;
} __attribute__ ((packed));

struct isp32_isp_params_cfg {
	__u64 module_en_update;
	__u64 module_ens;
	__u64 module_cfg_update;

	__u32 frame_id;
	struct isp32_isp_meas_cfg meas;
	struct isp32_isp_other_cfg others;
} __attribute__ ((packed));

struct isp32_stat {
	struct isp32_rawaebig_stat0 rawae3_0;	/* offset 0 */
	struct isp32_rawaebig_stat0 rawae1_0;	/* offset 0x390 */
	struct isp32_rawaebig_stat0 rawae2_0;	/* offset 0x720 */
	struct isp32_rawaelite_stat rawae0;	/* offset 0xab0 */
	struct isp32_rawaebig_stat1 rawae3_1;
	struct isp32_rawaebig_stat1 rawae1_1;
	struct isp32_rawaebig_stat1 rawae2_1;
	struct isp2x_bls_stat bls;
	struct isp2x_rawhistbig_stat rawhist3;	/* offset 0xc00 */
	struct isp2x_rawhistlite_stat rawhist0;	/* offset 0x1000 */
	struct isp2x_rawhistbig_stat rawhist1;	/* offset 0x1400 */
	struct isp2x_rawhistbig_stat rawhist2;	/* offset 0x1800 */
	struct isp32_rawaf_stat rawaf;		/* offset 0x1c00 */
	struct isp3x_dhaz_stat dhaz;
	struct isp32_vsm_stat vsm;
	struct isp32_info2ddr_stat info2ddr;
	struct isp32_rawawb_meas_stat rawawb;	/* offset 0x2b00 */
} __attribute__ ((packed));

/**
 * struct rkisp32_isp_stat_buffer - Rockchip ISP32 Statistics Meta Data
 *
 * @meas_type: measurement types (ISP3X_STAT_ definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp32_isp_stat_buffer {
	struct isp32_stat params;
	__u32 meas_type;
	__u32 frame_id;
	__u32 params_id;
} __attribute__ ((packed));

struct rkisp32_thunderboot_resmem_head {
	struct rkisp_thunderboot_resmem_head head;
	struct isp32_isp_params_cfg cfg;
} __attribute__ ((packed));

/****************isp32 lite********************/

struct isp32_lite_rawaebig_stat {
	__u32 sumr;
	__u32 sumg;
	__u32 sumb;
	struct isp2x_rawae_meas_data data[ISP32_RAWAEBIG_MEAN_NUM];
} __attribute__ ((packed));

struct isp32_lite_rawawb_meas_stat {
	__u32 ramdata_r[ISP32L_RAWAWB_RAMDATA_RGB_NUM];
	__u32 ramdata_g[ISP32L_RAWAWB_RAMDATA_RGB_NUM];
	__u32 ramdata_b[ISP32L_RAWAWB_RAMDATA_RGB_NUM];
	__u32 ramdata_wpnum0[ISP32L_RAWAWB_RAMDATA_WP_NUM];
	__u32 ramdata_wpnum1[ISP32L_RAWAWB_RAMDATA_WP_NUM];
	struct isp32_rawawb_sum sum[ISP32_RAWAWB_SUM_NUM];
	__u16 yhist_bin[ISP32_RAWAWB_HSTBIN_NUM];
	struct isp32_rawawb_sum_exc sum_exc[ISP32_RAWAWB_EXCL_STAT_NUM];
} __attribute__ ((packed));

struct isp32_lite_rawaf_ramdata {
	__u32 hiir_wnd_data[ISP32L_RAWAF_WND_DATA];
	__u32 viir_wnd_data[ISP32L_RAWAF_WND_DATA];
} __attribute__ ((packed));

struct isp32_lite_rawaf_stat {
	struct isp32_lite_rawaf_ramdata ramdata;
	__u32 int_state;
	__u32 afm_sum_b;
	__u32 afm_lum_b;
	__u32 highlit_cnt_winb;
} __attribute__ ((packed));

struct isp32_lite_rawhistlite_stat {
	__u32 hist_bin[ISP32L_HIST_LITE_BIN_N_MAX];
} __attribute__ ((packed));

struct isp32_lite_stat {
	struct isp2x_bls_stat bls;
	struct isp3x_dhaz_stat dhaz;
	struct isp32_info2ddr_stat info2ddr;
	struct isp2x_rawaelite_stat rawae0;
	struct isp32_lite_rawaebig_stat rawae3;
	struct isp32_lite_rawhistlite_stat rawhist0;
	struct isp2x_rawhistbig_stat rawhist3;
	struct isp32_lite_rawaf_stat rawaf;
	struct isp32_lite_rawawb_meas_stat rawawb;
} __attribute__ ((packed));

struct rkisp32_lite_stat_buffer {
	struct isp32_lite_stat params;
	__u32 meas_type;
	__u32 frame_id;
	__u32 params_id;
} __attribute__ ((packed));
#endif /* _UAPI_RK_ISP32_CONFIG_H */
