/* SPDX-License-Identifier: (GPL-2.0+ WITH Linux-syscall-note) OR MIT
 *
 * Rockchip ISP32
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKISP32_CONFIG_H
#define _UAPI_RKISP32_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/rkisp3-config.h>

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

#define	ISP32_RAWAEBIG_SUBWIN_NUM	ISP3X_RAWAEBIG_SUBWIN_NUM
#define ISP32_RAWAEBIG_MEAN_NUM		ISP3X_RAWAEBIG_MEAN_NUM
#define ISP32_RAWAELITE_MEAN_NUM	ISP3X_RAWAELITE_MEAN_NUM

#define ISP32_RAWHISTBIG_SUBWIN_NUM	ISP3X_RAWHISTBIG_SUBWIN_NUM
#define ISP32_RAWHISTLITE_SUBWIN_NUM	ISP3X_RAWHISTLITE_SUBWIN_NUM
#define ISP32_HIST_BIN_N_MAX		ISP3X_HIST_BIN_N_MAX

#define ISP32_RAWAF_CURVE_NUM		ISP3X_RAWAF_CURVE_NUM
#define ISP32_RAWAF_HIIR_COE_NUM	ISP3X_RAWAF_HIIR_COE_NUM
#define ISP32_RAWAF_VFIR_COE_NUM	ISP3X_RAWAF_VFIR_COE_NUM
#define ISP32_RAWAF_WIN_NUM		ISP3X_RAWAF_WIN_NUM
#define ISP32_RAWAF_LINE_NUM		ISP3X_RAWAF_LINE_NUM
#define ISP32_RAWAF_GAMMA_NUM		ISP3X_RAWAF_GAMMA_NUM
#define ISP32_RAWAF_SUMDATA_NUM		ISP3X_RAWAF_SUMDATA_NUM
#define ISP32_RAWAF_VIIR_COE_NUM	3
#define ISP32_RAWAF_GAUS_COE_NUM	9

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
	u8 frm_end_dis;
	u8 zero_interp_en;
	u8 sample_avr_en;
	u8 bic_mode_en;
	u8 force_map_en;
	u8 map13p3_en;

	u8 bicubic[ISP32_LDCH_BIC_NUM];

	u32 hsize;
	u32 vsize;
	s32 buf_fd;
} __attribute__ ((packed));

struct isp32_awb_gain_cfg {
	/* AWB1_GAIN_G */
	u16 awb1_gain_gb;
	u16 awb1_gain_gr;
	/* AWB1_GAIN_RB */
	u16 awb1_gain_b;
	u16 awb1_gain_r;
	/* AWB0_GAIN0_G */
	u16 gain0_green_b;
	u16 gain0_green_r;
	/* AWB0_GAIN0_RB*/
	u16 gain0_blue;
	u16 gain0_red;
	/* AWB0_GAIN1_G */
	u16 gain1_green_b;
	u16 gain1_green_r;
	/* AWB0_GAIN1_RB*/
	u16 gain1_blue;
	u16 gain1_red;
	/* AWB0_GAIN2_G */
	u16 gain2_green_b;
	u16 gain2_green_r;
	/* AWB0_GAIN2_RB*/
	u16 gain2_blue;
	u16 gain2_red;
} __attribute__ ((packed));

struct isp32_bls_cfg {
	u8 enable_auto;
	u8 en_windows;
	u8 bls1_en;

	u8 bls_samples;

	struct isp2x_window bls_window1;
	struct isp2x_window bls_window2;
	struct isp2x_bls_fixed_val fixed_val;
	struct isp2x_bls_fixed_val bls1_val;

	u16 isp_ob_offset;
	u16 isp_ob_predgain;
	u32 isp_ob_max;
} __attribute__ ((packed));

struct isp32_ccm_cfg {
	/* CTRL */
	u8 highy_adjust_dis;
	u8 enh_adj_en;
	u8 asym_adj_en;
	/* BOUND_BIT */
	u8 bound_bit;
	u8 right_bit;
	/* COEFF0_R */
	s16 coeff0_r;
	s16 coeff1_r;
	/* COEFF1_R */
	s16 coeff2_r;
	s16 offset_r;
	/* COEFF0_G */
	s16 coeff0_g;
	s16 coeff1_g;
	/* COEFF1_G */
	s16 coeff2_g;
	s16 offset_g;
	/* COEFF0_B */
	s16 coeff0_b;
	s16 coeff1_b;
	/* COEFF1_B */
	s16 coeff2_b;
	s16 offset_b;
	/* COEFF0_Y */
	u16 coeff0_y;
	u16 coeff1_y;
	/* COEFF1_Y */
	u16 coeff2_y;
	/* ALP_Y */
	u16 alp_y[ISP32_CCM_CURVE_NUM];
	/* ENHANCE0 */
	u16 color_coef0_r2y;
	u16 color_coef1_g2y;
	/* ENHANCE1 */
	u16 color_coef2_b2y;
	u16 color_enh_rat_max;
} __attribute__ ((packed));

struct isp32_debayer_cfg {
	/* CONTROL */
	u8 filter_g_en;
	u8 filter_c_en;
	/* G_INTERP */
	u8 clip_en;
	u8 dist_scale;
	u8 thed0;
	u8 thed1;
	u8 select_thed;
	u8 max_ratio;
	/* G_INTERP_FILTER1 */
	s8 filter1_coe1;
	s8 filter1_coe2;
	s8 filter1_coe3;
	s8 filter1_coe4;
	/* G_INTERP_FILTER2 */
	s8 filter2_coe1;
	s8 filter2_coe2;
	s8 filter2_coe3;
	s8 filter2_coe4;
	/* C_FILTER_GUIDE_GAUS */
	s8 guid_gaus_coe0;
	s8 guid_gaus_coe1;
	s8 guid_gaus_coe2;
	/* C_FILTER_CE_GAUS */
	s8 ce_gaus_coe0;
	s8 ce_gaus_coe1;
	s8 ce_gaus_coe2;
	/* C_FILTER_ALPHA_GAUS */
	s8 alpha_gaus_coe0;
	s8 alpha_gaus_coe1;
	s8 alpha_gaus_coe2;
	/* C_FILTER_IIR_0 */
	u8 ce_sgm;
	u8 exp_shift;
	/* C_FILTER_IIR_1 */
	u8 wet_clip;
	u8 wet_ghost;
	/* C_FILTER_BF */
	u8 bf_clip;
	u8 bf_curwgt;
	u16 bf_sgm;
	/* G_INTERP_OFFSET */
	u16 hf_offset;
	u16 gain_offset;
	/* G_FILTER_OFFSET */
	u16 offset;
	/* C_FILTER_LOG_OFFSET */
	u16 loghf_offset;
	u16 loggd_offset;
	/* C_FILTER_IIR_0 */
	u16 wgtslope;
	/* C_FILTER_ALPHA */
	u16 alpha_offset;
	/* C_FILTER_EDGE */
	u16 edge_offset;
	u32 edge_scale;
	/* C_FILTER_ALPHA */
	u32 alpha_scale;
} __attribute__ ((packed));

struct isp32_baynr_cfg {
	/* BAYNR_CTRL */
	u8 bay3d_gain_en;
	u8 lg2_mode;
	u8 gauss_en;
	u8 log_bypass;
	/* BAYNR_DGAIN */
	u16 dgain1;
	u16 dgain0;
	u16 dgain2;
	/* BAYNR_PIXDIFF */
	u16 pix_diff;
	/* BAYNR_THLD */
	u16 diff_thld;
	u16 softthld;
	/* BAYNR_W1_STRENG */
	u16 bltflt_streng;
	u16 reg_w1;
	/* BAYNR_SIGMA */
	u16 sigma_x[ISP32_BAYNR_XY_NUM];
	u16 sigma_y[ISP32_BAYNR_XY_NUM];
	/* BAYNR_WRIT_D */
	u16 weit_d2;
	u16 weit_d1;
	u16 weit_d0;
	/* BAYNR_LG_OFF */
	u16 lg2_lgoff;
	u16 lg2_off;
	/* BAYNR_DAT_MAX */
	u32 dat_max;
	/* BAYNR_SIGOFF */
	u16 rgain_off;
	u16 bgain_off;
	/* BAYNR_GAIN */
	u8 gain_x[ISP32_BAYNR_GAIN_NUM];
	u16 gain_y[ISP32_BAYNR_GAIN_NUM];
} __attribute__ ((packed));

struct isp32_bay3d_cfg {
	/* BAY3D_CTRL */
	u8 bypass_en;
	u8 hibypass_en;
	u8 lobypass_en;
	u8 himed_bypass_en;
	u8 higaus_bypass_en;
	u8 hiabs_possel;
	u8 hichnsplit_en;
	u8 lomed_bypass_en;
	u8 logaus5_bypass_en;
	u8 logaus3_bypass_en;
	u8 glbpk_en;
	u8 loswitch_protect;
	u8 bwsaving_en;
	/* BAY3D_CTRL1 */
	u8 hiwgt_opt_en;
	u8 hichncor_en;
	u8 bwopt_gain_dis;
	u8 lo4x8_en;
	u8 lo4x4_en;
	u8 hisig_ind_sel;
	u8 pksig_ind_sel;
	u8 iirwr_rnd_en;
	u8 curds_high_en;
	u8 higaus3_mode;
	u8 higaus5x5_en;
	u8 wgtmix_opt_en;
	/* BAY3D_SIGGAUS */
	u8 siggaus0;
	u8 siggaus1;
	u8 siggaus2;
	u8 siggaus3;
	/* BAY3D_KALRATIO */
	u16 softwgt;
	u16 hidif_th;
	/* BAY3D_WGTLMT */
	u16 wgtlmt;
	u16 wgtratio;
	/* BAY3D_SIG */
	u16 sig0_x[ISP32_BAY3D_XY_NUM];
	u16 sig0_y[ISP32_BAY3D_XY_NUM];
	u16 sig1_x[ISP32_BAY3D_XY_NUM];
	u16 sig1_y[ISP32_BAY3D_XY_NUM];
	u16 sig2_x[ISP32_BAY3D_XY_NUM];
	u16 sig2_y[ISP32_BAY3D_XY_NUM];
	/* BAY3D_HISIGRAT */
	u16 hisigrat0;
	u16 hisigrat1;
	/* BAY3D_HISIGOFF */
	u16 hisigoff0;
	u16 hisigoff1;
	/* BAY3D_LOSIG */
	u16 losigoff;
	u16 losigrat;
	/* BAY3D_SIGPK */
	u16 rgain_off;
	u16 bgain_off;
	/* BAY3D_GLBPK2 */
	u32 glbpk2;
} __attribute__ ((packed));

struct isp32_ynr_cfg {
	/* YNR_GLOBAL_CTRL */
	u8 rnr_en;
	u8 thumb_mix_cur_en;
	u8 global_gain_alpha;
	u8 flt1x1_bypass_sel;
	u8 nlm11x11_bypass;
	u8 flt1x1_bypass;
	u8 lgft3x3_bypass;
	u8 lbft5x5_bypass;
	u8 bft3x3_bypass;
	/* YNR_RNR_STRENGTH */
	u8 rnr_strength3[ISP32_YNR_XY_NUM];
	/* YNR_NLM_SIGMA_GAIN */
	u8 nlm_hi_gain_alpha;
	/* YNR_NLM_COE */
	u8 nlm_coe[ISP32_YNR_NLM_COE_NUM];

	u16 global_gain;

	/* YNR_RNR_MAX_R */
	u16 rnr_max_r;
	u16 local_gainscale;
	/* YNR_RNR_CENTER_COOR */
	u16 rnr_center_coorh;
	u16 rnr_center_coorv;
	/* YNR_LOCAL_GAIN_CTRL */
	u16 loclagain_adj_thresh;
	u16 localgain_adj;
	/* YNR_LOWNR_CTRL0 */
	u16 low_bf_inv1;
	u16 low_bf_inv0;
	/* YNR_LOWNR_CTRL1 */
	u16 low_peak_supress;
	u16 low_thred_adj;
	/* YNR_LOWNR_CTRL2 */
	u16 low_dist_adj;
	u16 low_edge_adj_thresh;
	/* YNR_LOWNR_CTRL3 */
	u16 low_bi_weight;
	u16 low_weight;
	u16 low_center_weight;
	/* YNR_LOWNR_CTRL4 */
	u16 frame_full_size;
	u16 lbf_weight_thres;
	/* YNR_GAUSS1_COEFF */
	u16 low_gauss1_coeff2;
	u16 low_gauss1_coeff1;
	u16 low_gauss1_coeff0;
	/* YNR_GAUSS2_COEFF */
	u16 low_gauss2_coeff2;
	u16 low_gauss2_coeff1;
	u16 low_gauss2_coeff0;
	/* YNR_SGM_DX */
	u16 luma_points_x[ISP32_YNR_XY_NUM];
	/* YNR_LSGM_Y */
	u16 lsgm_y[ISP32_YNR_XY_NUM];
	/* YNR_NLM_SIGMA_GAIN */
	u16 nlm_min_sigma;
	u16 nlm_hi_bf_scale;
	/* YNR_NLM_WEIGHT */
	u16 nlm_nr_weight;
	u16 nlm_weight_offset;
	/* YNR_NLM_NR_WEIGHT */
	u32 nlm_center_weight;
} __attribute__ ((packed));

struct isp32_cnr_cfg {
	/* CNR_CTRL */
	u8 exgain_bypass;
	u8 yuv422_mode;
	u8 thumb_mode;
	u8 bf3x3_wgt0_sel;
	/* CNR_LBF_WEITD */
	u8 lbf1x7_weit_d0;
	u8 lbf1x7_weit_d1;
	u8 lbf1x7_weit_d2;
	u8 lbf1x7_weit_d3;
	/* CNR_IIR_PARA1 */
	u8 iir_uvgain;
	u8 iir_strength;
	u8 exp_shift;
	/* CNR_IIR_PARA2 */
	u8 chroma_ghost;
	u8 iir_uv_clip;
	/* CNR_GAUS_COE */
	u8 gaus_coe[ISP32_CNR_GAUS_COE_NUM];
	/* CNR_GAUS_RATIO */
	u8 bf_wgt_clip;
	/* CNR_BF_PARA1 */
	u8 uv_gain;
	u8 bf_ratio;
	/* CNR_SIGMA */
	u8 sigma_y[ISP32_CNR_SIGMA_Y_NUM];
	/* CNR_IIR_GLOBAL_GAIN */
	u8 iir_gain_alpha;
	u8 iir_global_gain;
	/* CNR_EXGAIN */
	u8 gain_iso;
	u8 global_gain_alpha;
	u16 global_gain;
	/* CNR_THUMB1 */
	u16 thumb_sigma_c;
	u16 thumb_sigma_y;
	/* CNR_THUMB_BF_RATIO */
	u16 thumb_bf_ratio;
	/* CNR_IIR_PARA1 */
	u16 wgt_slope;
	/* CNR_GAUS_RATIO */
	u16 gaus_ratio;
	u16 global_alpha;
	/* CNR_BF_PARA1 */
	u16 sigma_r;
	/* CNR_BF_PARA2 */
	u16 adj_offset;
	u16 adj_ratio;
} __attribute__ ((packed));

struct isp32_sharp_cfg {
	/* SHARP_EN */
	u8 bypass;
	u8 center_mode;
	u8 exgain_bypass;
	u8 radius_ds_mode;
	u8 noiseclip_mode;
	/* SHARP_RATIO */
	u8 sharp_ratio;
	u8 bf_ratio;
	u8 gaus_ratio;
	u8 pbf_ratio;
	/* SHARP_LUMA_DX */
	u8 luma_dx[ISP32_SHARP_X_NUM];
	/* SHARP_SIGMA_SHIFT */
	u8 bf_sigma_shift;
	u8 pbf_sigma_shift;
	/* SHARP_PBF_COEF */
	u8 pbf_coef2;
	u8 pbf_coef1;
	u8 pbf_coef0;
	/* SHARP_BF_COEF */
	u8 bf_coef2;
	u8 bf_coef1;
	u8 bf_coef0;
	/* SHARP_GAUS_COEF */
	u8 gaus_coef[ISP32_SHARP_GAUS_COEF_NUM];
	/* SHARP_GAIN */
	u8 global_gain_alpha;
	u8 local_gainscale;
	/* SHARP_GAIN_DIS_STRENGTH */
	u8 strength[ISP32_SHARP_STRENGTH_NUM];
	/* SHARP_TEXTURE */
	u8 enhance_bit;
	/* SHARP_PBF_SIGMA_INV */
	u16 pbf_sigma_inv[ISP32_SHARP_Y_NUM];
	/* SHARP_BF_SIGMA_INV */
	u16 bf_sigma_inv[ISP32_SHARP_Y_NUM];
	/* SHARP_CLIP_HF */
	u16 clip_hf[ISP32_SHARP_Y_NUM];
	/* SHARP_GAIN */
	u16 global_gain;
	/* SHARP_GAIN_ADJUST */
	u16 gain_adj[ISP32_SHARP_GAIN_ADJ_NUM];
	/* SHARP_CENTER */
	u16 center_wid;
	u16 center_het;
	/* SHARP_TEXTURE */
	u16 noise_sigma;
	u16 noise_strength;
} __attribute__ ((packed));

struct isp32_dhaz_cfg {
	/* DHAZ_CTRL */
	u8 enh_luma_en;
	u8 color_deviate_en;
	u8 round_en;
	u8 soft_wr_en;
	u8 enhance_en;
	u8 air_lc_en;
	u8 hpara_en;
	u8 hist_en;
	u8 dc_en;
	/* DHAZ_ADP0 */
	u8 yblk_th;
	u8 yhist_th;
	u8 dc_max_th;
	u8 dc_min_th;
	/* DHAZ_ADP2 */
	u8 tmax_base;
	u8 dark_th;
	u8 air_max;
	u8 air_min;
	/* DHAZ_GAUS */
	u8 gaus_h2;
	u8 gaus_h1;
	u8 gaus_h0;
	/* DHAZ_GAIN_IDX */
	u8 sigma_idx[ISP32_DHAZ_SIGMA_IDX_NUM];
	/* DHAZ_ADP_HIST1 */
	u8 hist_gratio;
	u16 hist_scale;
	/* DHAZ_ADP1 */
	u8 bright_max;
	u8 bright_min;
	u16 wt_max;
	/* DHAZ_ADP_TMAX */
	u16 tmax_max;
	u16 tmax_off;
	/* DHAZ_ADP_HIST0 */
	u8 hist_k;
	u8 hist_th_off;
	u16 hist_min;
	/* DHAZ_ENHANCE */
	u16 enhance_value;
	u16 enhance_chroma;
	/* DHAZ_IIR0 */
	u16 iir_wt_sigma;
	u8 iir_sigma;
	u8 stab_fnum;
	/* DHAZ_IIR1 */
	u16 iir_tmax_sigma;
	u8 iir_air_sigma;
	u8 iir_pre_wet;
	/* DHAZ_SOFT_CFG0 */
	u16 cfg_wt;
	u8 cfg_air;
	u8 cfg_alpha;
	/* DHAZ_SOFT_CFG1 */
	u16 cfg_gratio;
	u16 cfg_tmax;
	/* DHAZ_BF_SIGMA */
	u16 range_sima;
	u8 space_sigma_pre;
	u8 space_sigma_cur;
	/* DHAZ_BF_WET */
	u16 dc_weitcur;
	u16 bf_weight;
	/* DHAZ_ENH_CURVE */
	u16 enh_curve[ISP32_DHAZ_ENH_CURVE_NUM];

	u16 sigma_lut[ISP32_DHAZ_SIGMA_LUT_NUM];

	u16 hist_wr[ISP32_DHAZ_HIST_WR_NUM];

	u16 enh_luma[ISP32_DHAZ_ENH_LUMA_NUM];
} __attribute__ ((packed));

struct isp32_drc_cfg {
	u8 bypass_en;
	/* DRC_CTRL1 */
	u8 offset_pow2;
	u16 compres_scl;
	u16 position;
	/* DRC_LPRATIO */
	u16 hpdetail_ratio;
	u16 lpdetail_ratio;
	u8 delta_scalein;
	/* DRC_EXPLRATIO */
	u8 weicur_pix;
	u8 weipre_frame;
	u8 bilat_wt_off;
	/* DRC_SIGMA */
	u8 edge_scl;
	u8 motion_scl;
	u16 force_sgm_inv0;
	/* DRC_SPACESGM */
	u16 space_sgm_inv1;
	u16 space_sgm_inv0;
	/* DRC_RANESGM */
	u16 range_sgm_inv1;
	u16 range_sgm_inv0;
	/* DRC_BILAT */
	u16 bilat_soft_thd;
	u8 weig_maxl;
	u8 weig_bilat;
	u8 enable_soft_thd;
	/* DRC_IIRWG_GAIN */
	u8 iir_weight;
	u16 min_ogain;
	/* DRC_LUM3X2_CTRL */
	u16 gas_t;
	/* DRC_LUM3X2_GAS */
	u8 gas_l0;
	u8 gas_l1;
	u8 gas_l2;
	u8 gas_l3;

	u16 gain_y[ISP32_DRC_Y_NUM];
	u16 compres_y[ISP32_DRC_Y_NUM];
	u16 scale_y[ISP32_DRC_Y_NUM];
} __attribute__ ((packed));

struct isp32_hdrmge_cfg {
	u8 s_base;
	u8 mode;
	u8 dbg_mode;
	u8 each_raw_en;

	u8 gain2;

	u8 lm_dif_0p15;
	u8 lm_dif_0p9;
	u8 ms_diff_0p15;
	u8 ms_dif_0p8;

	u16 gain0_inv;
	u16 gain0;
	u16 gain1_inv;
	u16 gain1;

	u16 ms_thd1;
	u16 ms_thd0;
	u16 ms_scl;
	u16 lm_thd1;
	u16 lm_thd0;
	u16 lm_scl;
	struct isp2x_hdrmge_curve curve;
	u16 e_y[ISP32_HDRMGE_E_CURVE_NUM];
	u16 l_raw0[ISP32_HDRMGE_E_CURVE_NUM];
	u16 l_raw1[ISP32_HDRMGE_E_CURVE_NUM];
	u16 each_raw_gain0;
	u16 each_raw_gain1;
} __attribute__ ((packed));

struct isp32_rawawb_meas_cfg {
	u8 bls2_en;

	u8 rawawb_sel;
	u8 bnr2awb_sel;
	u8 drc2awb_sel;
	/* RAWAWB_CTRL */
	u8 uv_en0;
	u8 xy_en0;
	u8 yuv3d_en0;
	u8 yuv3d_ls_idx0;
	u8 yuv3d_ls_idx1;
	u8 yuv3d_ls_idx2;
	u8 yuv3d_ls_idx3;
	u8 in_rshift_to_12bit_en;
	u8 in_overexposure_check_en;
	u8 wind_size;
	u8 rawlsc_bypass_en;
	u8 light_num;
	u8 uv_en1;
	u8 xy_en1;
	u8 yuv3d_en1;
	u8 low12bit_val;
	/* RAWAWB_WEIGHT_CURVE_CTRL */
	u8 wp_luma_wei_en0;
	u8 wp_luma_wei_en1;
	u8 wp_blk_wei_en0;
	u8 wp_blk_wei_en1;
	u8 wp_hist_xytype;
	/* RAWAWB_MULTIWINDOW_EXC_CTRL */
	u8 exc_wp_region0_excen;
	u8 exc_wp_region0_measen;
	u8 exc_wp_region0_domain;
	u8 exc_wp_region1_excen;
	u8 exc_wp_region1_measen;
	u8 exc_wp_region1_domain;
	u8 exc_wp_region2_excen;
	u8 exc_wp_region2_measen;
	u8 exc_wp_region2_domain;
	u8 exc_wp_region3_excen;
	u8 exc_wp_region3_measen;
	u8 exc_wp_region3_domain;
	u8 exc_wp_region4_excen;
	u8 exc_wp_region4_domain;
	u8 exc_wp_region5_excen;
	u8 exc_wp_region5_domain;
	u8 exc_wp_region6_excen;
	u8 exc_wp_region6_domain;
	u8 multiwindow_en;
	/* RAWAWB_YWEIGHT_CURVE_XCOOR03 */
	u8 wp_luma_weicurve_y0;
	u8 wp_luma_weicurve_y1;
	u8 wp_luma_weicurve_y2;
	u8 wp_luma_weicurve_y3;
	/* RAWAWB_YWEIGHT_CURVE_XCOOR47 */
	u8 wp_luma_weicurve_y4;
	u8 wp_luma_weicurve_y5;
	u8 wp_luma_weicurve_y6;
	u8 wp_luma_weicurve_y7;
	/* RAWAWB_YWEIGHT_CURVE_XCOOR8 */
	u8 wp_luma_weicurve_y8;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR03 */
	u8 wp_luma_weicurve_w0;
	u8 wp_luma_weicurve_w1;
	u8 wp_luma_weicurve_w2;
	u8 wp_luma_weicurve_w3;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR47 */
	u8 wp_luma_weicurve_w4;
	u8 wp_luma_weicurve_w5;
	u8 wp_luma_weicurve_w6;
	u8 wp_luma_weicurve_w7;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR8 */
	u8 wp_luma_weicurve_w8;
	/* RAWAWB_YUV_X1X2_DIS_0 */
	u8 dis_x1x2_ls0;
	u8 rotu0_ls0;
	u8 rotu1_ls0;
	/* RAWAWB_YUV_INTERP_CURVE_UCOOR_0 */
	u8 rotu2_ls0;
	u8 rotu3_ls0;
	u8 rotu4_ls0;
	u8 rotu5_ls0;
	/* RAWAWB_YUV_X1X2_DIS_1 */
	u8 dis_x1x2_ls1;
	u8 rotu0_ls1;
	u8 rotu1_ls1;
	/* YUV_INTERP_CURVE_UCOOR_1 */
	u8 rotu2_ls1;
	u8 rotu3_ls1;
	u8 rotu4_ls1;
	u8 rotu5_ls1;
	/* RAWAWB_YUV_X1X2_DIS_2 */
	u8 dis_x1x2_ls2;
	u8 rotu0_ls2;
	u8 rotu1_ls2;
	/* YUV_INTERP_CURVE_UCOOR_2 */
	u8 rotu2_ls2;
	u8 rotu3_ls2;
	u8 rotu4_ls2;
	u8 rotu5_ls2;
	/* RAWAWB_YUV_X1X2_DIS_3 */
	u8 dis_x1x2_ls3;
	u8 rotu0_ls3;
	u8 rotu1_ls3;
	u8 rotu2_ls3;
	u8 rotu3_ls3;
	u8 rotu4_ls3;
	u8 rotu5_ls3;
	/* RAWAWB_EXC_WP_WEIGHT */
	u8 exc_wp_region0_weight;
	u8 exc_wp_region1_weight;
	u8 exc_wp_region2_weight;
	u8 exc_wp_region3_weight;
	u8 exc_wp_region4_weight;
	u8 exc_wp_region5_weight;
	u8 exc_wp_region6_weight;
	/* RAWAWB_WRAM_DATA */
	u8 wp_blk_wei_w[ISP32_RAWAWB_WEIGHT_NUM];
	/* RAWAWB_BLK_CTRL */
	u8 blk_measure_enable;
	u8 blk_measure_mode;
	u8 blk_measure_xytype;
	u8 blk_rtdw_measure_en;
	u8 blk_measure_illu_idx;
	u8 blk_with_luma_wei_en;
	u16 in_overexposure_threshold;
	/* RAWAWB_LIMIT_RG_MAX*/
	u16 r_max;
	u16 g_max;
	/* RAWAWB_LIMIT_BY_MAX */
	u16 b_max;
	u16 y_max;
	/* RAWAWB_LIMIT_RG_MIN */
	u16 r_min;
	u16 g_min;
	/* RAWAWB_LIMIT_BY_MIN */
	u16 b_min;
	u16 y_min;
	/* RAWAWB_WIN_OFFS */
	u16 h_offs;
	u16 v_offs;
	/* RAWAWB_WIN_SIZE */
	u16 h_size;
	u16 v_size;
	/* RAWAWB_YWEIGHT_CURVE_YCOOR8 */
	u16 pre_wbgain_inv_r;
	/* RAWAWB_PRE_WBGAIN_INV */
	u16 pre_wbgain_inv_g;
	u16 pre_wbgain_inv_b;
	/* RAWAWB_UV_DETC_VERTEX */
	u16 vertex0_u_0;
	u16 vertex0_v_0;

	u16 vertex1_u_0;
	u16 vertex1_v_0;

	u16 vertex2_u_0;
	u16 vertex2_v_0;

	u16 vertex3_u_0;
	u16 vertex3_v_0;

	u16 vertex0_u_1;
	u16 vertex0_v_1;

	u16 vertex1_u_1;
	u16 vertex1_v_1;

	u16 vertex2_u_1;
	u16 vertex2_v_1;

	u16 vertex3_u_1;
	u16 vertex3_v_1;

	u16 vertex0_u_2;
	u16 vertex0_v_2;

	u16 vertex1_u_2;
	u16 vertex1_v_2;

	u16 vertex2_u_2;
	u16 vertex2_v_2;

	u16 vertex3_u_2;
	u16 vertex3_v_2;

	u16 vertex0_u_3;
	u16 vertex0_v_3;

	u16 vertex1_u_3;
	u16 vertex1_v_3;

	u16 vertex2_u_3;
	u16 vertex2_v_3;

	u16 vertex3_u_3;
	u16 vertex3_v_3;
	/* RAWAWB_RGB2XY_WT */
	u16 wt0;
	u16 wt1;
	u16 wt2;
	/* RAWAWB_RGB2XY_MAT */
	u16 mat0_x;
	u16 mat0_y;

	u16 mat1_x;
	u16 mat1_y;

	u16 mat2_x;
	u16 mat2_y;
	/* RAWAWB_XY_DETC_NOR */
	u16 nor_x0_0;
	u16 nor_x1_0;
	u16 nor_y0_0;
	u16 nor_y1_0;

	u16 nor_x0_1;
	u16 nor_x1_1;
	u16 nor_y0_1;
	u16 nor_y1_1;

	u16 nor_x0_2;
	u16 nor_x1_2;
	u16 nor_y0_2;
	u16 nor_y1_2;

	u16 nor_x0_3;
	u16 nor_x1_3;
	u16 nor_y0_3;
	u16 nor_y1_3;
	/* RAWAWB_XY_DETC_BIG */
	u16 big_x0_0;
	u16 big_x1_0;
	u16 big_y0_0;
	u16 big_y1_0;

	u16 big_x0_1;
	u16 big_x1_1;
	u16 big_y0_1;
	u16 big_y1_1;

	u16 big_x0_2;
	u16 big_x1_2;
	u16 big_y0_2;
	u16 big_y1_2;

	u16 big_x0_3;
	u16 big_x1_3;
	u16 big_y0_3;
	u16 big_y1_3;
	/* RAWAWB_MULTIWINDOW */
	u16 multiwindow0_v_offs;
	u16 multiwindow0_h_offs;
	u16 multiwindow0_v_size;
	u16 multiwindow0_h_size;

	u16 multiwindow1_v_offs;
	u16 multiwindow1_h_offs;
	u16 multiwindow1_v_size;
	u16 multiwindow1_h_size;

	u16 multiwindow2_v_offs;
	u16 multiwindow2_h_offs;
	u16 multiwindow2_v_size;
	u16 multiwindow2_h_size;

	u16 multiwindow3_v_offs;
	u16 multiwindow3_h_offs;
	u16 multiwindow3_v_size;
	u16 multiwindow3_h_size;
	/* RAWAWB_EXC_WP_REGION */
	u16 exc_wp_region0_xu0;
	u16 exc_wp_region0_xu1;

	u16 exc_wp_region0_yv0;
	u16 exc_wp_region0_yv1;

	u16 exc_wp_region1_xu0;
	u16 exc_wp_region1_xu1;

	u16 exc_wp_region1_yv0;
	u16 exc_wp_region1_yv1;

	u16 exc_wp_region2_xu0;
	u16 exc_wp_region2_xu1;

	u16 exc_wp_region2_yv0;
	u16 exc_wp_region2_yv1;

	u16 exc_wp_region3_xu0;
	u16 exc_wp_region3_xu1;

	u16 exc_wp_region3_yv0;
	u16 exc_wp_region3_yv1;

	u16 exc_wp_region4_xu0;
	u16 exc_wp_region4_xu1;

	u16 exc_wp_region4_yv0;
	u16 exc_wp_region4_yv1;

	u16 exc_wp_region5_xu0;
	u16 exc_wp_region5_xu1;

	u16 exc_wp_region5_yv0;
	u16 exc_wp_region5_yv1;

	u16 exc_wp_region6_xu0;
	u16 exc_wp_region6_xu1;

	u16 exc_wp_region6_yv0;
	u16 exc_wp_region6_yv1;
	/* RAWAWB_YUV_RGB2ROTY */
	u16 rgb2ryuvmat0_y;
	u16 rgb2ryuvmat1_y;
	u16 rgb2ryuvmat2_y;
	u16 rgb2ryuvofs_y;
	/* RAWAWB_YUV_RGB2ROTU */
	u16 rgb2ryuvmat0_u;
	u16 rgb2ryuvmat1_u;
	u16 rgb2ryuvmat2_u;
	u16 rgb2ryuvofs_u;
	/* RAWAWB_YUV_RGB2ROTV */
	u16 rgb2ryuvmat0_v;
	u16 rgb2ryuvmat1_v;
	u16 rgb2ryuvmat2_v;
	u16 rgb2ryuvofs_v;
	/* RAWAWB_YUV_X_COOR */
	u16 coor_x1_ls0_y;
	u16 vec_x21_ls0_y;
	u16 coor_x1_ls0_u;
	u16 vec_x21_ls0_u;
	u16 coor_x1_ls0_v;
	u16 vec_x21_ls0_v;

	u16 coor_x1_ls1_y;
	u16 vec_x21_ls1_y;
	u16 coor_x1_ls1_u;
	u16 vec_x21_ls1_u;
	u16 coor_x1_ls1_v;
	u16 vec_x21_ls1_v;

	u16 coor_x1_ls2_y;
	u16 vec_x21_ls2_y;
	u16 coor_x1_ls2_u;
	u16 vec_x21_ls2_v;
	u16 coor_x1_ls2_v;
	u16 vec_x21_ls2_u;

	u16 coor_x1_ls3_y;
	u16 vec_x21_ls3_y;
	u16 coor_x1_ls3_u;
	u16 vec_x21_ls3_u;
	u16 coor_x1_ls3_v;
	u16 vec_x21_ls3_v;
	/* RAWAWB_YUV_INTERP_CURVE_TH */
	u16 th0_ls0;
	u16 th1_ls0;
	u16 th2_ls0;
	u16 th3_ls0;
	u16 th4_ls0;
	u16 th5_ls0;

	u16 th0_ls1;
	u16 th1_ls1;
	u16 th2_ls1;
	u16 th3_ls1;
	u16 th4_ls1;
	u16 th5_ls1;

	u16 th0_ls2;
	u16 th1_ls2;
	u16 th2_ls2;
	u16 th3_ls2;
	u16 th4_ls2;
	u16 th5_ls2;

	u16 th0_ls3;
	u16 th1_ls3;
	u16 th2_ls3;
	u16 th3_ls3;
	u16 th4_ls3;
	u16 th5_ls3;
	/* RAWAWB_UV_DETC_ISLOPE */
	u32 islope01_0;
	u32 islope12_0;
	u32 islope23_0;
	u32 islope30_0;
	u32 islope01_1;
	u32 islope12_1;
	u32 islope23_1;
	u32 islope30_1;
	u32 islope01_2;
	u32 islope12_2;
	u32 islope23_2;
	u32 islope30_2;
	u32 islope01_3;
	u32 islope12_3;
	u32 islope23_3;
	u32 islope30_3;

	struct isp2x_bls_fixed_val bls2_val;
} __attribute__ ((packed));

struct isp32_rawaf_meas_cfg {
	u8 rawaf_sel;
	u8 num_afm_win;
	/* CTRL */
	u8 gamma_en;
	u8 gaus_en;
	u8 v1_fir_sel;
	u8 hiir_en;
	u8 viir_en;
	u8 accu_8bit_mode;
	u8 ldg_en;
	u8 h1_fv_mode;
	u8 h2_fv_mode;
	u8 v1_fv_mode;
	u8 v2_fv_mode;
	u8 ae_mode;
	u8 y_mode;
	u8 vldg_sel;
	u8 sobel_sel;
	u8 v_dnscl_mode;
	u8 from_awb;
	u8 from_ynr;
	u8 ae_config_use;
	/* WINA_B */
	struct isp2x_window win[ISP32_RAWAF_WIN_NUM];
	/* INT_LINE */
	u8 line_num[ISP32_RAWAF_LINE_NUM];
	u8 line_en[ISP32_RAWAF_LINE_NUM];
	/* THRES */
	u16 afm_thres;
	/* VAR_SHIFT */
	u8 afm_var_shift[ISP32_RAWAF_WIN_NUM];
	u8 lum_var_shift[ISP32_RAWAF_WIN_NUM];
	/* HVIIR_VAR_SHIFT */
	u8 h1iir_var_shift;
	u8 h2iir_var_shift;
	u8 v1iir_var_shift;
	u8 v2iir_var_shift;
	/* GAUS_COE */
	s8 gaus_coe[ISP32_RAWAF_GAUS_COE_NUM];

	/* GAMMA_Y */
	u16 gamma_y[ISP32_RAWAF_GAMMA_NUM];
	/* HIIR_THRESH */
	u16 h_fv_thresh;
	u16 v_fv_thresh;
	struct isp3x_rawaf_curve curve_h[ISP32_RAWAF_CURVE_NUM];
	struct isp3x_rawaf_curve curve_v[ISP32_RAWAF_CURVE_NUM];
	s16 h1iir1_coe[ISP32_RAWAF_HIIR_COE_NUM];
	s16 h1iir2_coe[ISP32_RAWAF_HIIR_COE_NUM];
	s16 h2iir1_coe[ISP32_RAWAF_HIIR_COE_NUM];
	s16 h2iir2_coe[ISP32_RAWAF_HIIR_COE_NUM];
	s16 v1iir_coe[ISP32_RAWAF_VIIR_COE_NUM];
	s16 v2iir_coe[ISP32_RAWAF_VIIR_COE_NUM];
	s16 v1fir_coe[ISP32_RAWAF_VFIR_COE_NUM];
	s16 v2fir_coe[ISP32_RAWAF_VFIR_COE_NUM];
	u16 highlit_thresh;
} __attribute__ ((packed));

struct isp32_cac_cfg {
	u8 bypass_en;
	u8 center_en;
	u8 clip_g_mode;
	u8 edge_detect_en;
	u8 neg_clip0_en;

	u8 flat_thed_b;
	u8 flat_thed_r;

	u8 psf_sft_bit;
	u16 cfg_num;

	u16 center_width;
	u16 center_height;

	u16 strength[ISP32_CAC_STRENGTH_NUM];

	u16 offset_b;
	u16 offset_r;

	u32 expo_thed_b;
	u32 expo_thed_r;
	u32 expo_adj_b;
	u32 expo_adj_r;

	u32 hsize;
	u32 vsize;
	s32 buf_fd;
} __attribute__ ((packed));

struct isp32_vsm_cfg {
	u8 h_segments;
	u8 v_segments;
	u16 h_offs;
	u16 v_offs;
	u16 h_size;
	u16 v_size;
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
	u32 channelg_xy:12;
	u32 channelb_xy:10;
	u32 channelr_xy:10;
} __attribute__ ((packed));

struct isp32_rawaebig_stat0 {
	struct isp32_rawae_meas_data data[ISP32_RAWAEBIG_MEAN_NUM];
	u32 reserved[3];
} __attribute__ ((packed));

struct isp32_rawaebig_stat1 {
	u32 sumr[ISP32_RAWAEBIG_SUBWIN_NUM];
	u32 sumg[ISP32_RAWAEBIG_SUBWIN_NUM];
	u32 sumb[ISP32_RAWAEBIG_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp32_rawaelite_stat {
	struct isp32_rawae_meas_data data[ISP32_RAWAELITE_MEAN_NUM];
	u32 reserved[21];
} __attribute__ ((packed));

struct isp32_rawaf_stat {
	struct isp3x_rawaf_ramdata ramdata[ISP32_RAWAF_SUMDATA_NUM];
	u32 int_state;
	u32 afm_sum_b;
	u32 afm_lum_b;
	u32 highlit_cnt_winb;
	u32 reserved[18];
} __attribute__ ((packed));

struct isp32_rawawb_ramdata {
	u64 b:18;
	u64 g:18;
	u64 r:18;
	u64 wp:10;
} __attribute__ ((packed));

struct isp32_rawawb_sum {
	u32 rgain_nor;
	u32 bgain_nor;
	u32 wp_num_nor;
	u32 wp_num2;

	u32 rgain_big;
	u32 bgain_big;
	u32 wp_num_big;
	u32 reserved;
} __attribute__ ((packed));

struct isp32_rawawb_sum_exc {
	u32 rgain_exc;
	u32 bgain_exc;
	u32 wp_num_exc;
	u32 reserved;
} __attribute__ ((packed));

struct isp32_rawawb_meas_stat {
	struct isp32_rawawb_ramdata ramdata[ISP32_RAWAWB_RAMDATA_NUM];
	u64 reserved;
	struct isp32_rawawb_sum sum[ISP32_RAWAWB_SUM_NUM];
	u16 yhist_bin[ISP32_RAWAWB_HSTBIN_NUM];
	struct isp32_rawawb_sum_exc sum_exc[ISP32_RAWAWB_EXCL_STAT_NUM];
} __attribute__ ((packed));

struct isp32_vsm_stat {
	u16 delta_h;
	u16 delta_v;
} __attribute__ ((packed));

struct isp32_info2ddr_stat {
	u32 owner;
	s32 buf_fd;
} __attribute__ ((packed));

struct isp32_isp_params_cfg {
	u64 module_en_update;
	u64 module_ens;
	u64 module_cfg_update;

	u32 frame_id;
	struct isp32_isp_meas_cfg meas;
	struct isp32_isp_other_cfg others;
} __attribute__ ((packed));

struct isp32_stat {
	struct isp32_rawaebig_stat0 rawae3_0;	//offset 0
	struct isp32_rawaebig_stat0 rawae1_0;	//offset 0x390
	struct isp32_rawaebig_stat0 rawae2_0;	//offset 0x720
	struct isp32_rawaelite_stat rawae0;	//offset 0xab0
	struct isp32_rawaebig_stat1 rawae3_1;
	struct isp32_rawaebig_stat1 rawae1_1;
	struct isp32_rawaebig_stat1 rawae2_1;
	struct isp2x_bls_stat bls;
	struct isp2x_rawhistbig_stat rawhist3;	//offset 0xc00
	struct isp2x_rawhistlite_stat rawhist0;	//offset 0x1000
	struct isp2x_rawhistbig_stat rawhist1;	//offset 0x1400
	struct isp2x_rawhistbig_stat rawhist2;	//offset 0x1800
	struct isp32_rawaf_stat rawaf;		//offset 0x1c00
	struct isp3x_dhaz_stat dhaz;
	struct isp32_vsm_stat vsm;
	struct isp32_info2ddr_stat info2ddr;
	struct isp32_rawawb_meas_stat rawawb;	//offset 0x2b00
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
	u32 meas_type;
	u32 frame_id;
} __attribute__ ((packed));

struct rkisp32_thunderboot_resmem_head {
	struct rkisp_thunderboot_resmem_head head;
	struct isp32_isp_params_cfg cfg;
};
#endif /* _UAPI_RKISP32_CONFIG_H */
