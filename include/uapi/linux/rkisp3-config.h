/* SPDX-License-Identifier: (GPL-2.0+ WITH Linux-syscall-note) OR MIT
 *
 * Rockchip ISP3
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKISP3_CONFIG_H
#define _UAPI_RKISP3_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/rkisp21-config.h>

#define ISP3X_MODULE_DPCC		ISP2X_MODULE_DPCC
#define ISP3X_MODULE_BLS		ISP2X_MODULE_BLS
#define ISP3X_MODULE_SDG		ISP2X_MODULE_SDG
#define ISP3X_MODULE_LSC		ISP2X_MODULE_LSC
#define ISP3X_MODULE_AWB_GAIN		ISP2X_MODULE_AWB_GAIN
#define ISP3X_MODULE_BDM		ISP2X_MODULE_BDM
#define ISP3X_MODULE_CCM		ISP2X_MODULE_CCM
#define ISP3X_MODULE_GOC		ISP2X_MODULE_GOC
#define ISP3X_MODULE_CPROC		ISP2X_MODULE_CPROC
#define ISP3X_MODULE_IE			ISP2X_MODULE_IE
#define ISP3X_MODULE_RAWAF		ISP2X_MODULE_RAWAF
#define ISP3X_MODULE_RAWAE0		ISP2X_MODULE_RAWAE0
#define ISP3X_MODULE_RAWAE1		ISP2X_MODULE_RAWAE1
#define ISP3X_MODULE_RAWAE2		ISP2X_MODULE_RAWAE2
#define ISP3X_MODULE_RAWAE3		ISP2X_MODULE_RAWAE3
#define ISP3X_MODULE_RAWAWB		ISP2X_MODULE_RAWAWB
#define ISP3X_MODULE_RAWHIST0		ISP2X_MODULE_RAWHIST0
#define ISP3X_MODULE_RAWHIST1		ISP2X_MODULE_RAWHIST1
#define ISP3X_MODULE_RAWHIST2		ISP2X_MODULE_RAWHIST2
#define ISP3X_MODULE_RAWHIST3		ISP2X_MODULE_RAWHIST3
#define ISP3X_MODULE_HDRMGE		ISP2X_MODULE_HDRMGE
#define ISP3X_MODULE_RAWNR		ISP2X_MODULE_RAWNR
#define ISP3X_MODULE_GIC		ISP2X_MODULE_GIC
#define ISP3X_MODULE_DHAZ		ISP2X_MODULE_DHAZ
#define ISP3X_MODULE_3DLUT		ISP2X_MODULE_3DLUT
#define ISP3X_MODULE_LDCH		ISP2X_MODULE_LDCH
#define ISP3X_MODULE_GAIN		ISP2X_MODULE_GAIN
#define ISP3X_MODULE_DEBAYER		ISP2X_MODULE_DEBAYER
#define ISP3X_MODULE_BAYNR		ISP2X_MODULE_BAYNR
#define ISP3X_MODULE_BAY3D		ISP2X_MODULE_BAY3D
#define ISP3X_MODULE_YNR		ISP2X_MODULE_YNR
#define ISP3X_MODULE_CNR		ISP2X_MODULE_CNR
#define ISP3X_MODULE_SHARP		ISP2X_MODULE_SHARP
#define ISP3X_MODULE_DRC		ISP2X_MODULE_DRC
#define ISP3X_MODULE_CAC		BIT_ULL(42)
#define ISP3X_MODULE_CSM		ISP2X_MODULE_CSM
#define ISP3X_MODULE_CGC		ISP2X_MODULE_CGC

#define ISP3X_MODULE_FORCE		ISP2X_MODULE_FORCE

/* Measurement types */
#define ISP3X_STAT_RAWAWB		ISP2X_STAT_RAWAWB
#define ISP3X_STAT_RAWAF		ISP2X_STAT_RAWAF
#define ISP3X_STAT_RAWAE0		ISP2X_STAT_RAWAE0
#define ISP3X_STAT_RAWAE1		ISP2X_STAT_RAWAE1
#define ISP3X_STAT_RAWAE2		ISP2X_STAT_RAWAE2
#define ISP3X_STAT_RAWAE3		ISP2X_STAT_RAWAE3
#define ISP3X_STAT_RAWHST0		ISP2X_STAT_RAWHST0
#define ISP3X_STAT_RAWHST1		ISP2X_STAT_RAWHST1
#define ISP3X_STAT_RAWHST2		ISP2X_STAT_RAWHST2
#define ISP3X_STAT_RAWHST3		ISP2X_STAT_RAWHST3
#define ISP3X_STAT_BLS			ISP2X_STAT_BLS
#define ISP3X_STAT_DHAZ			ISP2X_STAT_DHAZ

#define ISP3X_MESH_BUF_NUM		ISP2X_MESH_BUF_NUM

#define ISP3X_LSC_GRAD_TBL_SIZE		16
#define ISP3X_LSC_SIZE_TBL_SIZE		16
#define ISP3X_LSC_DATA_TBL_SIZE		ISP2X_LSC_DATA_TBL_SIZE

#define ISP3X_DEGAMMA_CURVE_SIZE	ISP2X_DEGAMMA_CURVE_SIZE

#define ISP3X_GAIN_IDX_NUM		ISP2X_GAIN_IDX_NUM
#define ISP3X_GAIN_LUT_NUM		ISP2X_GAIN_LUT_NUM

#define ISP3X_RAWAWB_MULWD_NUM		4
#define ISP3X_RAWAWB_EXCL_STAT_NUM	4
#define ISP3X_RAWAWB_HSTBIN_NUM		ISP21_RAWAWB_HSTBIN_NUM
#define ISP3X_RAWAWB_WEIGHT_NUM		ISP21_RAWAWB_WEIGHT_NUM
#define ISP3X_RAWAWB_SUM_NUM		ISP2X_RAWAWB_SUM_NUM
#define ISP3X_RAWAWB_RAMDATA_NUM	ISP2X_RAWAWB_RAMDATA_NUM

#define	ISP3X_RAWAEBIG_SUBWIN_NUM	ISP2X_RAWAEBIG_SUBWIN_NUM
#define ISP3X_RAWAEBIG_MEAN_NUM		ISP2X_RAWAEBIG_MEAN_NUM
#define ISP3X_RAWAELITE_MEAN_NUM	ISP2X_RAWAELITE_MEAN_NUM

#define ISP3X_RAWHISTBIG_SUBWIN_NUM	ISP2X_RAWHISTBIG_SUBWIN_NUM
#define ISP3X_RAWHISTLITE_SUBWIN_NUM	ISP2X_RAWHISTLITE_SUBWIN_NUM
#define ISP3X_HIST_BIN_N_MAX		ISP2X_HIST_BIN_N_MAX

#define ISP3X_RAWAF_CURVE_NUM		2
#define ISP3X_RAWAF_HIIR_COE_NUM	6
#define ISP3X_RAWAF_V1IIR_COE_NUM	9
#define ISP3X_RAWAF_V2IIR_COE_NUM	3
#define ISP3X_RAWAF_VFIR_COE_NUM	3
#define ISP3X_RAWAF_WIN_NUM		ISP2X_RAWAF_WIN_NUM
#define ISP3X_RAWAF_LINE_NUM		ISP2X_RAWAF_LINE_NUM
#define ISP3X_RAWAF_GAMMA_NUM		ISP2X_RAWAF_GAMMA_NUM
#define ISP3X_RAWAF_SUMDATA_NUM		ISP2X_RAWAF_SUMDATA_NUM

#define ISP3X_DPCC_PDAF_POINT_NUM	ISP2X_DPCC_PDAF_POINT_NUM

#define ISP3X_HDRMGE_L_CURVE_NUM	ISP2X_HDRMGE_L_CURVE_NUM
#define ISP3X_HDRMGE_E_CURVE_NUM	ISP2X_HDRMGE_E_CURVE_NUM

#define ISP3X_GIC_SIGMA_Y_NUM		ISP2X_GIC_SIGMA_Y_NUM

#define ISP3X_CCM_CURVE_NUM		ISP2X_CCM_CURVE_NUM

#define ISP3X_3DLUT_DATA_NUM		ISP2X_3DLUT_DATA_NUM

#define ISP3X_LDCH_MESH_XY_NUM		ISP2X_LDCH_MESH_XY_NUM

#define ISP3X_GAMMA_OUT_MAX_SAMPLES     49

#define ISP3X_DHAZ_SIGMA_IDX_NUM	15
#define ISP3X_DHAZ_SIGMA_LUT_NUM	17
#define ISP3X_DHAZ_HIST_WR_NUM		64
#define ISP3X_DHAZ_ENH_CURVE_NUM	ISP21_DHAZ_ENH_CURVE_NUM
#define ISP3X_DHAZ_HIST_IIR_NUM		ISP21_DHAZ_HIST_IIR_NUM

#define ISP3X_DRC_Y_NUM			ISP21_DRC_Y_NUM

#define ISP3X_CNR_SIGMA_Y_NUM		13

#define ISP3X_YNR_XY_NUM		ISP21_YNR_XY_NUM

#define ISP3X_BAYNR_XY_NUM		ISP21_BAYNR_XY_NUM

#define ISP3X_BAY3D_XY_NUM		ISP21_BAY3D_XY_NUM

#define ISP3X_SHARP_X_NUM		ISP21_SHARP_X_NUM
#define ISP3X_SHARP_Y_NUM		ISP21_SHARP_Y_NUM
#define ISP3X_SHARP_GAUS_COEF_NUM	6

#define ISP3X_CAC_STRENGTH_NUM		22

#define ISP3X_CSM_COEFF_NUM		ISP21_CSM_COEFF_NUM

enum isp3x_unite_id {
	ISP3_LEFT = 0,
	ISP3_RIGHT,
	ISP3_UNITE_MAX,
};

struct isp3x_gammaout_cfg {
	u8 equ_segm;
	u8 finalx4_dense_en;
	u16 offset;
	u16 gamma_y[ISP3X_GAMMA_OUT_MAX_SAMPLES];
} __attribute__ ((packed));

struct isp3x_lsc_cfg {
	u8 sector_16x16;

	u16 r_data_tbl[ISP3X_LSC_DATA_TBL_SIZE];
	u16 gr_data_tbl[ISP3X_LSC_DATA_TBL_SIZE];
	u16 gb_data_tbl[ISP3X_LSC_DATA_TBL_SIZE];
	u16 b_data_tbl[ISP3X_LSC_DATA_TBL_SIZE];

	u16 x_grad_tbl[ISP3X_LSC_GRAD_TBL_SIZE];
	u16 y_grad_tbl[ISP3X_LSC_GRAD_TBL_SIZE];

	u16 x_size_tbl[ISP3X_LSC_SIZE_TBL_SIZE];
	u16 y_size_tbl[ISP3X_LSC_SIZE_TBL_SIZE];
} __attribute__ ((packed));

struct isp3x_baynr_cfg {
	u8 lg2_mode;
	u8 gauss_en;
	u8 log_bypass;

	u16 dgain1;
	u16 dgain0;
	u16 dgain2;

	u16 pix_diff;

	u16 diff_thld;
	u16 softthld;

	u16 bltflt_streng;
	u16 reg_w1;

	u16 sigma_x[ISP3X_BAYNR_XY_NUM];
	u16 sigma_y[ISP3X_BAYNR_XY_NUM];

	u16 weit_d2;
	u16 weit_d1;
	u16 weit_d0;

	u16 lg2_lgoff;
	u16 lg2_off;

	u32 dat_max;
} __attribute__ ((packed));

struct isp3x_bay3d_cfg {
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

	u16 softwgt;
	u16 hidif_th;

	u32 glbpk2;

	u16 wgtlmt;
	u16 wgtratio;

	u16 sig0_x[ISP3X_BAY3D_XY_NUM];
	u16 sig0_y[ISP3X_BAY3D_XY_NUM];
	u16 sig1_x[ISP3X_BAY3D_XY_NUM];
	u16 sig1_y[ISP3X_BAY3D_XY_NUM];
	u16 sig2_x[ISP3X_BAY3D_XY_NUM];
	u16 sig2_y[ISP3X_BAY3D_XY_NUM];
} __attribute__ ((packed));

struct isp3x_ynr_cfg {
	u8 rnr_en;
	u8 thumb_mix_cur_en;
	u8 global_gain_alpha;
	u8 flt1x1_bypass_sel;
	u8 sft5x5_bypass;
	u8 flt1x1_bypass;
	u8 lgft3x3_bypass;
	u8 lbft5x5_bypass;
	u8 bft3x3_bypass;
	u16 global_gain;

	u16 rnr_max_r;
	u16 local_gainscale;

	u16 rnr_center_coorh;
	u16 rnr_center_coorv;

	u16 loclagain_adj_thresh;
	u16 localgain_adj;

	u16 low_bf_inv1;
	u16 low_bf_inv0;

	u16 low_peak_supress;
	u16 low_thred_adj;

	u16 low_dist_adj;
	u16 low_edge_adj_thresh;

	u16 low_bi_weight;
	u16 low_weight;
	u16 low_center_weight;
	u16 hi_min_adj;
	u16 high_thred_adj;
	u8 high_retain_weight;
	u8 hi_edge_thed;
	u8 base_filter_weight2;
	u8 base_filter_weight1;
	u8 base_filter_weight0;
	u16 frame_full_size;
	u16 lbf_weight_thres;
	u16 low_gauss1_coeff2;
	u16 low_gauss1_coeff1;
	u16 low_gauss1_coeff0;
	u16 low_gauss2_coeff2;
	u16 low_gauss2_coeff1;
	u16 low_gauss2_coeff0;
	u8 direction_weight3;
	u8 direction_weight2;
	u8 direction_weight1;
	u8 direction_weight0;
	u8 direction_weight7;
	u8 direction_weight6;
	u8 direction_weight5;
	u8 direction_weight4;
	u16 luma_points_x[ISP3X_YNR_XY_NUM];
	u16 lsgm_y[ISP3X_YNR_XY_NUM];
	u16 hsgm_y[ISP3X_YNR_XY_NUM];
	u8 rnr_strength3[ISP3X_YNR_XY_NUM];
} __attribute__ ((packed));

struct isp3x_cnr_cfg {
	u8 thumb_mix_cur_en;
	u8 lq_bila_bypass;
	u8 hq_bila_bypass;
	u8 exgain_bypass;

	u8 global_gain_alpha;
	u16 global_gain;

	u8 gain_iso;
	u8 gain_offset;
	u8 gain_1sigma;

	u8 gain_uvgain1;
	u8 gain_uvgain0;

	u8 lmed3_alpha;

	u8 lbf5_gain_y;
	u8 lbf5_gain_c;

	u8 lbf5_weit_d3;
	u8 lbf5_weit_d2;
	u8 lbf5_weit_d1;
	u8 lbf5_weit_d0;

	u8 lbf5_weit_d4;

	u8 hmed3_alpha;

	u16 hbf5_weit_src;
	u16 hbf5_min_wgt;
	u16 hbf5_sigma;

	u16 lbf5_weit_src;
	u16 lbf3_sigma;

	u8 sigma_y[ISP3X_CNR_SIGMA_Y_NUM];
} __attribute__ ((packed));

struct isp3x_sharp_cfg {
	u8 bypass;
	u8 center_mode;
	u8 exgain_bypass;

	u8 sharp_ratio;
	u8 bf_ratio;
	u8 gaus_ratio;
	u8 pbf_ratio;

	u8 luma_dx[ISP3X_SHARP_X_NUM];

	u16 pbf_sigma_inv[ISP3X_SHARP_Y_NUM];

	u16 bf_sigma_inv[ISP3X_SHARP_Y_NUM];

	u8 bf_sigma_shift;
	u8 pbf_sigma_shift;

	u16 ehf_th[ISP3X_SHARP_Y_NUM];

	u16 clip_hf[ISP3X_SHARP_Y_NUM];

	u8 pbf_coef2;
	u8 pbf_coef1;
	u8 pbf_coef0;

	u8 bf_coef2;
	u8 bf_coef1;
	u8 bf_coef0;

	u8 gaus_coef[ISP3X_SHARP_GAUS_COEF_NUM];
} __attribute__ ((packed));

struct isp3x_dhaz_cfg {
	u8 round_en;
	u8 soft_wr_en;
	u8 enhance_en;
	u8 air_lc_en;
	u8 hpara_en;
	u8 hist_en;
	u8 dc_en;

	u8 yblk_th;
	u8 yhist_th;
	u8 dc_max_th;
	u8 dc_min_th;

	u16 wt_max;
	u8 bright_max;
	u8 bright_min;

	u8 tmax_base;
	u8 dark_th;
	u8 air_max;
	u8 air_min;

	u16 tmax_max;
	u16 tmax_off;

	u8 hist_k;
	u8 hist_th_off;
	u16 hist_min;

	u16 hist_gratio;
	u16 hist_scale;

	u16 enhance_value;
	u16 enhance_chroma;

	u16 iir_wt_sigma;
	u16 iir_sigma;
	u16 stab_fnum;

	u16 iir_tmax_sigma;
	u16 iir_air_sigma;
	u8 iir_pre_wet;

	u16 cfg_wt;
	u16 cfg_air;
	u16 cfg_alpha;

	u16 cfg_gratio;
	u16 cfg_tmax;

	u16 range_sima;
	u8 space_sigma_pre;
	u8 space_sigma_cur;

	u16 dc_weitcur;
	u16 bf_weight;

	u16 enh_curve[ISP3X_DHAZ_ENH_CURVE_NUM];

	u8 gaus_h2;
	u8 gaus_h1;
	u8 gaus_h0;

	u8 sigma_idx[ISP3X_DHAZ_SIGMA_IDX_NUM];
	u16 sigma_lut[ISP3X_DHAZ_SIGMA_LUT_NUM];

	u16 adp_wt_wr;
	u16 adp_air_wr;

	u16 adp_tmax_wr;
	u16 adp_gratio_wr;

	u16 hist_wr[ISP3X_DHAZ_HIST_WR_NUM];
} __attribute__ ((packed));

struct isp3x_dhaz_stat {
	u32 dhaz_pic_sumh;

	u16 dhaz_adp_air_base;
	u16 dhaz_adp_wt;

	u16 dhaz_adp_gratio;
	u16 dhaz_adp_tmax;

	u16 h_rgb_iir[ISP3X_DHAZ_HIST_IIR_NUM];
} __attribute__ ((packed));

struct isp3x_drc_cfg {
	u8 bypass_en;
	u8 offset_pow2;
	u16 compres_scl;
	u16 position;
	u16 delta_scalein;
	u16 hpdetail_ratio;
	u16 lpdetail_ratio;
	u8 weicur_pix;
	u8 weipre_frame;
	u8 bilat_wt_off;
	u16 force_sgm_inv0;
	u8 motion_scl;
	u8 edge_scl;
	u16 space_sgm_inv1;
	u16 space_sgm_inv0;
	u16 range_sgm_inv1;
	u16 range_sgm_inv0;
	u8 weig_maxl;
	u8 weig_bilat;
	u8 enable_soft_thd;
	u16 bilat_soft_thd;
	u16 gain_y[ISP3X_DRC_Y_NUM];
	u16 compres_y[ISP3X_DRC_Y_NUM];
	u16 scale_y[ISP3X_DRC_Y_NUM];
	u16 wr_cycle;
	u16 iir_weight;
	u16 min_ogain;
} __attribute__ ((packed));

struct isp3x_hdrmge_cfg {
	u8 s_base;
	u8 mode;

	u16 gain0_inv;
	u16 gain0;
	u16 gain1_inv;
	u16 gain1;
	u8 gain2;

	u8 lm_dif_0p15;
	u8 lm_dif_0p9;
	u8 ms_diff_0p15;
	u8 ms_dif_0p8;

	u16 ms_thd1;
	u16 ms_thd0;
	u16 ms_scl;
	u16 lm_thd1;
	u16 lm_thd0;
	u16 lm_scl;
	struct isp2x_hdrmge_curve curve;
	u16 e_y[ISP3X_HDRMGE_E_CURVE_NUM];
} __attribute__ ((packed));

struct isp3x_rawawb_meas_cfg {
	u8 rawawb_sel;
	u8 sw_rawawb_xy_en0;
	u8 sw_rawawb_uv_en0;
	u8 sw_rawawb_xy_en1;
	u8 sw_rawawb_uv_en1;
	u8 sw_rawawb_3dyuv_en0;
	u8 sw_rawawb_3dyuv_en1;
	u8 sw_rawawb_wp_blk_wei_en0;
	u8 sw_rawawb_wp_blk_wei_en1;
	u8 sw_rawawb_wp_luma_wei_en0;
	u8 sw_rawawb_wp_luma_wei_en1;
	u8 sw_rawlsc_bypass_en;
	u8 sw_rawawb_blk_measure_enable;
	u8 sw_rawawb_blk_measure_mode;
	u8 sw_rawawb_blk_measure_xytype;
	u8 sw_rawawb_blk_measure_illu_idx;
	u8 sw_rawawb_wp_hist_xytype;
	u8 sw_rawawb_light_num;
	u8 sw_rawawb_wind_size;
	u8 sw_rawawb_r_max;
	u8 sw_rawawb_g_max;
	u8 sw_rawawb_b_max;
	u8 sw_rawawb_y_max;
	u8 sw_rawawb_r_min;
	u8 sw_rawawb_g_min;
	u8 sw_rawawb_b_min;
	u8 sw_rawawb_y_min;
	u8 sw_rawawb_3dyuv_ls_idx0;
	u8 sw_rawawb_3dyuv_ls_idx1;
	u8 sw_rawawb_3dyuv_ls_idx2;
	u8 sw_rawawb_3dyuv_ls_idx3;
	u8 sw_rawawb_multiwindow_en;
	u8 sw_rawawb_exc_wp_region0_excen0;
	u8 sw_rawawb_exc_wp_region0_excen1;
	u8 sw_rawawb_exc_wp_region0_measen;
	u8 sw_rawawb_exc_wp_region0_domain;
	u8 sw_rawawb_exc_wp_region1_excen0;
	u8 sw_rawawb_exc_wp_region1_excen1;
	u8 sw_rawawb_exc_wp_region1_measen;
	u8 sw_rawawb_exc_wp_region1_domain;
	u8 sw_rawawb_exc_wp_region2_excen0;
	u8 sw_rawawb_exc_wp_region2_excen1;
	u8 sw_rawawb_exc_wp_region2_measen;
	u8 sw_rawawb_exc_wp_region2_domain;
	u8 sw_rawawb_exc_wp_region3_excen0;
	u8 sw_rawawb_exc_wp_region3_excen1;
	u8 sw_rawawb_exc_wp_region3_measen;
	u8 sw_rawawb_exc_wp_region3_domain;
	u8 sw_rawawb_exc_wp_region4_excen0;
	u8 sw_rawawb_exc_wp_region4_excen1;
	u8 sw_rawawb_exc_wp_region4_domain;
	u8 sw_rawawb_exc_wp_region5_excen0;
	u8 sw_rawawb_exc_wp_region5_excen1;
	u8 sw_rawawb_exc_wp_region5_domain;
	u8 sw_rawawb_exc_wp_region6_excen0;
	u8 sw_rawawb_exc_wp_region6_excen1;
	u8 sw_rawawb_exc_wp_region6_domain;
	u8 sw_rawawb_wp_luma_weicurve_y0;
	u8 sw_rawawb_wp_luma_weicurve_y1;
	u8 sw_rawawb_wp_luma_weicurve_y2;
	u8 sw_rawawb_wp_luma_weicurve_y3;
	u8 sw_rawawb_wp_luma_weicurve_y4;
	u8 sw_rawawb_wp_luma_weicurve_y5;
	u8 sw_rawawb_wp_luma_weicurve_y6;
	u8 sw_rawawb_wp_luma_weicurve_y7;
	u8 sw_rawawb_wp_luma_weicurve_y8;
	u8 sw_rawawb_wp_luma_weicurve_w0;
	u8 sw_rawawb_wp_luma_weicurve_w1;
	u8 sw_rawawb_wp_luma_weicurve_w2;
	u8 sw_rawawb_wp_luma_weicurve_w3;
	u8 sw_rawawb_wp_luma_weicurve_w4;
	u8 sw_rawawb_wp_luma_weicurve_w5;
	u8 sw_rawawb_wp_luma_weicurve_w6;
	u8 sw_rawawb_wp_luma_weicurve_w7;
	u8 sw_rawawb_wp_luma_weicurve_w8;
	u8 sw_rawawb_rotu0_ls0;
	u8 sw_rawawb_rotu1_ls0;
	u8 sw_rawawb_rotu2_ls0;
	u8 sw_rawawb_rotu3_ls0;
	u8 sw_rawawb_rotu4_ls0;
	u8 sw_rawawb_rotu5_ls0;
	u8 sw_rawawb_dis_x1x2_ls0;
	u8 sw_rawawb_rotu0_ls1;
	u8 sw_rawawb_rotu1_ls1;
	u8 sw_rawawb_rotu2_ls1;
	u8 sw_rawawb_rotu3_ls1;
	u8 sw_rawawb_rotu4_ls1;
	u8 sw_rawawb_rotu5_ls1;
	u8 sw_rawawb_dis_x1x2_ls1;
	u8 sw_rawawb_rotu0_ls2;
	u8 sw_rawawb_rotu1_ls2;
	u8 sw_rawawb_rotu2_ls2;
	u8 sw_rawawb_rotu3_ls2;
	u8 sw_rawawb_rotu4_ls2;
	u8 sw_rawawb_rotu5_ls2;
	u8 sw_rawawb_dis_x1x2_ls2;
	u8 sw_rawawb_rotu0_ls3;
	u8 sw_rawawb_rotu1_ls3;
	u8 sw_rawawb_rotu2_ls3;
	u8 sw_rawawb_rotu3_ls3;
	u8 sw_rawawb_rotu4_ls3;
	u8 sw_rawawb_rotu5_ls3;
	u8 sw_rawawb_dis_x1x2_ls3;
	u8 sw_rawawb_blk_rtdw_measure_en;
	u8 sw_rawawb_blk_with_luma_wei_en;
	u8 sw_rawawb_wp_blk_wei_w[ISP3X_RAWAWB_WEIGHT_NUM];

	u16 sw_rawawb_h_offs;
	u16 sw_rawawb_v_offs;
	u16 sw_rawawb_h_size;
	u16 sw_rawawb_v_size;
	u16 sw_rawawb_vertex0_u_0;
	u16 sw_rawawb_vertex0_v_0;
	u16 sw_rawawb_vertex1_u_0;
	u16 sw_rawawb_vertex1_v_0;
	u16 sw_rawawb_vertex2_u_0;
	u16 sw_rawawb_vertex2_v_0;
	u16 sw_rawawb_vertex3_u_0;
	u16 sw_rawawb_vertex3_v_0;
	u16 sw_rawawb_vertex0_u_1;
	u16 sw_rawawb_vertex0_v_1;
	u16 sw_rawawb_vertex1_u_1;
	u16 sw_rawawb_vertex1_v_1;
	u16 sw_rawawb_vertex2_u_1;
	u16 sw_rawawb_vertex2_v_1;
	u16 sw_rawawb_vertex3_u_1;
	u16 sw_rawawb_vertex3_v_1;
	u16 sw_rawawb_vertex0_u_2;
	u16 sw_rawawb_vertex0_v_2;
	u16 sw_rawawb_vertex1_u_2;
	u16 sw_rawawb_vertex1_v_2;
	u16 sw_rawawb_vertex2_u_2;
	u16 sw_rawawb_vertex2_v_2;
	u16 sw_rawawb_vertex3_u_2;
	u16 sw_rawawb_vertex3_v_2;
	u16 sw_rawawb_vertex0_u_3;
	u16 sw_rawawb_vertex0_v_3;
	u16 sw_rawawb_vertex1_u_3;
	u16 sw_rawawb_vertex1_v_3;
	u16 sw_rawawb_vertex2_u_3;
	u16 sw_rawawb_vertex2_v_3;
	u16 sw_rawawb_vertex3_u_3;
	u16 sw_rawawb_vertex3_v_3;
	u16 sw_rawawb_vertex0_u_4;
	u16 sw_rawawb_vertex0_v_4;
	u16 sw_rawawb_vertex1_u_4;
	u16 sw_rawawb_vertex1_v_4;
	u16 sw_rawawb_vertex2_u_4;
	u16 sw_rawawb_vertex2_v_4;
	u16 sw_rawawb_vertex3_u_4;
	u16 sw_rawawb_vertex3_v_4;
	u16 sw_rawawb_vertex0_u_5;
	u16 sw_rawawb_vertex0_v_5;
	u16 sw_rawawb_vertex1_u_5;
	u16 sw_rawawb_vertex1_v_5;
	u16 sw_rawawb_vertex2_u_5;
	u16 sw_rawawb_vertex2_v_5;
	u16 sw_rawawb_vertex3_u_5;
	u16 sw_rawawb_vertex3_v_5;
	u16 sw_rawawb_vertex0_u_6;
	u16 sw_rawawb_vertex0_v_6;
	u16 sw_rawawb_vertex1_u_6;
	u16 sw_rawawb_vertex1_v_6;
	u16 sw_rawawb_vertex2_u_6;
	u16 sw_rawawb_vertex2_v_6;
	u16 sw_rawawb_vertex3_u_6;
	u16 sw_rawawb_vertex3_v_6;

	u16 sw_rawawb_wt0;
	u16 sw_rawawb_wt1;
	u16 sw_rawawb_wt2;
	u16 sw_rawawb_mat0_x;
	u16 sw_rawawb_mat1_x;
	u16 sw_rawawb_mat2_x;
	u16 sw_rawawb_mat0_y;
	u16 sw_rawawb_mat1_y;
	u16 sw_rawawb_mat2_y;
	u16 sw_rawawb_nor_x0_0;
	u16 sw_rawawb_nor_x1_0;
	u16 sw_rawawb_nor_y0_0;
	u16 sw_rawawb_nor_y1_0;
	u16 sw_rawawb_big_x0_0;
	u16 sw_rawawb_big_x1_0;
	u16 sw_rawawb_big_y0_0;
	u16 sw_rawawb_big_y1_0;
	u16 sw_rawawb_nor_x0_1;
	u16 sw_rawawb_nor_x1_1;
	u16 sw_rawawb_nor_y0_1;
	u16 sw_rawawb_nor_y1_1;
	u16 sw_rawawb_big_x0_1;
	u16 sw_rawawb_big_x1_1;
	u16 sw_rawawb_big_y0_1;
	u16 sw_rawawb_big_y1_1;
	u16 sw_rawawb_nor_x0_2;
	u16 sw_rawawb_nor_x1_2;
	u16 sw_rawawb_nor_y0_2;
	u16 sw_rawawb_nor_y1_2;
	u16 sw_rawawb_big_x0_2;
	u16 sw_rawawb_big_x1_2;
	u16 sw_rawawb_big_y0_2;
	u16 sw_rawawb_big_y1_2;
	u16 sw_rawawb_nor_x0_3;
	u16 sw_rawawb_nor_x1_3;
	u16 sw_rawawb_nor_y0_3;
	u16 sw_rawawb_nor_y1_3;
	u16 sw_rawawb_big_x0_3;
	u16 sw_rawawb_big_x1_3;
	u16 sw_rawawb_big_y0_3;
	u16 sw_rawawb_big_y1_3;
	u16 sw_rawawb_nor_x0_4;
	u16 sw_rawawb_nor_x1_4;
	u16 sw_rawawb_nor_y0_4;
	u16 sw_rawawb_nor_y1_4;
	u16 sw_rawawb_big_x0_4;
	u16 sw_rawawb_big_x1_4;
	u16 sw_rawawb_big_y0_4;
	u16 sw_rawawb_big_y1_4;
	u16 sw_rawawb_nor_x0_5;
	u16 sw_rawawb_nor_x1_5;
	u16 sw_rawawb_nor_y0_5;
	u16 sw_rawawb_nor_y1_5;
	u16 sw_rawawb_big_x0_5;
	u16 sw_rawawb_big_x1_5;
	u16 sw_rawawb_big_y0_5;
	u16 sw_rawawb_big_y1_5;
	u16 sw_rawawb_nor_x0_6;
	u16 sw_rawawb_nor_x1_6;
	u16 sw_rawawb_nor_y0_6;
	u16 sw_rawawb_nor_y1_6;
	u16 sw_rawawb_big_x0_6;
	u16 sw_rawawb_big_x1_6;
	u16 sw_rawawb_big_y0_6;
	u16 sw_rawawb_big_y1_6;
	u16 sw_rawawb_pre_wbgain_inv_r;
	u16 sw_rawawb_pre_wbgain_inv_g;
	u16 sw_rawawb_pre_wbgain_inv_b;
	u16 sw_rawawb_multiwindow0_v_offs;
	u16 sw_rawawb_multiwindow0_h_offs;
	u16 sw_rawawb_multiwindow0_v_size;
	u16 sw_rawawb_multiwindow0_h_size;
	u16 sw_rawawb_multiwindow1_v_offs;
	u16 sw_rawawb_multiwindow1_h_offs;
	u16 sw_rawawb_multiwindow1_v_size;
	u16 sw_rawawb_multiwindow1_h_size;
	u16 sw_rawawb_multiwindow2_v_offs;
	u16 sw_rawawb_multiwindow2_h_offs;
	u16 sw_rawawb_multiwindow2_v_size;
	u16 sw_rawawb_multiwindow2_h_size;
	u16 sw_rawawb_multiwindow3_v_offs;
	u16 sw_rawawb_multiwindow3_h_offs;
	u16 sw_rawawb_multiwindow3_v_size;
	u16 sw_rawawb_multiwindow3_h_size;
	u16 sw_rawawb_exc_wp_region0_xu0;
	u16 sw_rawawb_exc_wp_region0_xu1;
	u16 sw_rawawb_exc_wp_region0_yv0;
	u16 sw_rawawb_exc_wp_region0_yv1;
	u16 sw_rawawb_exc_wp_region1_xu0;
	u16 sw_rawawb_exc_wp_region1_xu1;
	u16 sw_rawawb_exc_wp_region1_yv0;
	u16 sw_rawawb_exc_wp_region1_yv1;
	u16 sw_rawawb_exc_wp_region2_xu0;
	u16 sw_rawawb_exc_wp_region2_xu1;
	u16 sw_rawawb_exc_wp_region2_yv0;
	u16 sw_rawawb_exc_wp_region2_yv1;
	u16 sw_rawawb_exc_wp_region3_xu0;
	u16 sw_rawawb_exc_wp_region3_xu1;
	u16 sw_rawawb_exc_wp_region3_yv0;
	u16 sw_rawawb_exc_wp_region3_yv1;
	u16 sw_rawawb_exc_wp_region4_xu0;
	u16 sw_rawawb_exc_wp_region4_xu1;
	u16 sw_rawawb_exc_wp_region4_yv0;
	u16 sw_rawawb_exc_wp_region4_yv1;
	u16 sw_rawawb_exc_wp_region5_xu0;
	u16 sw_rawawb_exc_wp_region5_xu1;
	u16 sw_rawawb_exc_wp_region5_yv0;
	u16 sw_rawawb_exc_wp_region5_yv1;
	u16 sw_rawawb_exc_wp_region6_xu0;
	u16 sw_rawawb_exc_wp_region6_xu1;
	u16 sw_rawawb_exc_wp_region6_yv0;
	u16 sw_rawawb_exc_wp_region6_yv1;
	u16 sw_rawawb_rgb2ryuvmat0_u;
	u16 sw_rawawb_rgb2ryuvmat1_u;
	u16 sw_rawawb_rgb2ryuvmat2_u;
	u16 sw_rawawb_rgb2ryuvofs_u;
	u16 sw_rawawb_rgb2ryuvmat0_v;
	u16 sw_rawawb_rgb2ryuvmat1_v;
	u16 sw_rawawb_rgb2ryuvmat2_v;
	u16 sw_rawawb_rgb2ryuvofs_v;
	u16 sw_rawawb_rgb2ryuvmat0_y;
	u16 sw_rawawb_rgb2ryuvmat1_y;
	u16 sw_rawawb_rgb2ryuvmat2_y;
	u16 sw_rawawb_rgb2ryuvofs_y;
	u16 sw_rawawb_th0_ls0;
	u16 sw_rawawb_th1_ls0;
	u16 sw_rawawb_th2_ls0;
	u16 sw_rawawb_th3_ls0;
	u16 sw_rawawb_th4_ls0;
	u16 sw_rawawb_th5_ls0;
	u16 sw_rawawb_coor_x1_ls0_u;
	u16 sw_rawawb_coor_x1_ls0_v;
	u16 sw_rawawb_coor_x1_ls0_y;
	u16 sw_rawawb_vec_x21_ls0_u;
	u16 sw_rawawb_vec_x21_ls0_v;
	u16 sw_rawawb_vec_x21_ls0_y;
	u16 sw_rawawb_th0_ls1;
	u16 sw_rawawb_th1_ls1;
	u16 sw_rawawb_th2_ls1;
	u16 sw_rawawb_th3_ls1;
	u16 sw_rawawb_th4_ls1;
	u16 sw_rawawb_th5_ls1;
	u16 sw_rawawb_coor_x1_ls1_u;
	u16 sw_rawawb_coor_x1_ls1_v;
	u16 sw_rawawb_coor_x1_ls1_y;
	u16 sw_rawawb_vec_x21_ls1_u;
	u16 sw_rawawb_vec_x21_ls1_v;
	u16 sw_rawawb_vec_x21_ls1_y;
	u16 sw_rawawb_th0_ls2;
	u16 sw_rawawb_th1_ls2;
	u16 sw_rawawb_th2_ls2;
	u16 sw_rawawb_th3_ls2;
	u16 sw_rawawb_th4_ls2;
	u16 sw_rawawb_th5_ls2;
	u16 sw_rawawb_coor_x1_ls2_u;
	u16 sw_rawawb_coor_x1_ls2_v;
	u16 sw_rawawb_coor_x1_ls2_y;
	u16 sw_rawawb_vec_x21_ls2_u;
	u16 sw_rawawb_vec_x21_ls2_v;
	u16 sw_rawawb_vec_x21_ls2_y;
	u16 sw_rawawb_th0_ls3;
	u16 sw_rawawb_th1_ls3;
	u16 sw_rawawb_th2_ls3;
	u16 sw_rawawb_th3_ls3;
	u16 sw_rawawb_th4_ls3;
	u16 sw_rawawb_th5_ls3;
	u16 sw_rawawb_coor_x1_ls3_u;
	u16 sw_rawawb_coor_x1_ls3_v;
	u16 sw_rawawb_coor_x1_ls3_y;
	u16 sw_rawawb_vec_x21_ls3_u;
	u16 sw_rawawb_vec_x21_ls3_v;
	u16 sw_rawawb_vec_x21_ls3_y;

	u32 sw_rawawb_islope01_0;
	u32 sw_rawawb_islope12_0;
	u32 sw_rawawb_islope23_0;
	u32 sw_rawawb_islope30_0;
	u32 sw_rawawb_islope01_1;
	u32 sw_rawawb_islope12_1;
	u32 sw_rawawb_islope23_1;
	u32 sw_rawawb_islope30_1;
	u32 sw_rawawb_islope01_2;
	u32 sw_rawawb_islope12_2;
	u32 sw_rawawb_islope23_2;
	u32 sw_rawawb_islope30_2;
	u32 sw_rawawb_islope01_3;
	u32 sw_rawawb_islope12_3;
	u32 sw_rawawb_islope23_3;
	u32 sw_rawawb_islope30_3;
	u32 sw_rawawb_islope01_4;
	u32 sw_rawawb_islope12_4;
	u32 sw_rawawb_islope23_4;
	u32 sw_rawawb_islope30_4;
	u32 sw_rawawb_islope01_5;
	u32 sw_rawawb_islope12_5;
	u32 sw_rawawb_islope23_5;
	u32 sw_rawawb_islope30_5;
	u32 sw_rawawb_islope01_6;
	u32 sw_rawawb_islope12_6;
	u32 sw_rawawb_islope23_6;
	u32 sw_rawawb_islope30_6;
} __attribute__ ((packed));

struct isp3x_rawawb_meas_stat {
	u16 ro_yhist_bin[ISP3X_RAWAWB_HSTBIN_NUM];
	u32 ro_rawawb_sum_rgain_nor[ISP3X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_sum_bgain_nor[ISP3X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_wp_num_nor[ISP3X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_sum_rgain_big[ISP3X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_sum_bgain_big[ISP3X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_wp_num_big[ISP3X_RAWAWB_SUM_NUM];
	u32 ro_wp_num2[ISP3X_RAWAWB_SUM_NUM];
	u32 ro_sum_r_nor_multiwindow[ISP3X_RAWAWB_MULWD_NUM];
	u32 ro_sum_b_nor_multiwindow[ISP3X_RAWAWB_MULWD_NUM];
	u32 ro_wp_nm_nor_multiwindow[ISP3X_RAWAWB_MULWD_NUM];
	u32 ro_sum_r_big_multiwindow[ISP3X_RAWAWB_MULWD_NUM];
	u32 ro_sum_b_big_multiwindow[ISP3X_RAWAWB_MULWD_NUM];
	u32 ro_wp_nm_big_multiwindow[ISP3X_RAWAWB_MULWD_NUM];
	u32 ro_sum_r_exc[ISP3X_RAWAWB_EXCL_STAT_NUM];
	u32 ro_sum_b_exc[ISP3X_RAWAWB_EXCL_STAT_NUM];
	u32 ro_wp_nm_exc[ISP3X_RAWAWB_EXCL_STAT_NUM];
	struct isp2x_rawawb_ramdata ramdata[ISP3X_RAWAWB_RAMDATA_NUM];
} __attribute__ ((packed));

struct isp3x_rawaf_curve {
	u8 ldg_lumth;
	u8 ldg_gain;
	u16 ldg_gslp;
} __attribute__ ((packed));

struct isp3x_rawaf_meas_cfg {
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
	/* WINA_B */
	struct isp2x_window win[ISP3X_RAWAF_WIN_NUM];
	/* INT_LINE */
	u8 line_num[ISP3X_RAWAF_LINE_NUM];
	u8 line_en[ISP3X_RAWAF_LINE_NUM];
	/* THRES */
	u16 afm_thres;
	/* VAR_SHIFT */
	u8 afm_var_shift[ISP3X_RAWAF_WIN_NUM];
	u8 lum_var_shift[ISP3X_RAWAF_WIN_NUM];
	/* HVIIR_VAR_SHIFT */
	u8 h1iir_var_shift;
	u8 h2iir_var_shift;
	u8 v1iir_var_shift;
	u8 v2iir_var_shift;
	/* GAMMA_Y */
	u16 gamma_y[ISP3X_RAWAF_GAMMA_NUM];
	/* HIIR_THRESH */
	u16 h_fv_thresh;
	u16 v_fv_thresh;
	struct isp3x_rawaf_curve curve_h[ISP3X_RAWAF_CURVE_NUM];
	struct isp3x_rawaf_curve curve_v[ISP3X_RAWAF_CURVE_NUM];
	s16 h1iir1_coe[ISP3X_RAWAF_HIIR_COE_NUM];
	s16 h1iir2_coe[ISP3X_RAWAF_HIIR_COE_NUM];
	s16 h2iir1_coe[ISP3X_RAWAF_HIIR_COE_NUM];
	s16 h2iir2_coe[ISP3X_RAWAF_HIIR_COE_NUM];
	s16 v1iir_coe[ISP3X_RAWAF_V1IIR_COE_NUM];
	s16 v2iir_coe[ISP3X_RAWAF_V2IIR_COE_NUM];
	s16 v1fir_coe[ISP3X_RAWAF_VFIR_COE_NUM];
	s16 v2fir_coe[ISP3X_RAWAF_VFIR_COE_NUM];
	u16 highlit_thresh;
} __attribute__ ((packed));

struct isp3x_rawaf_ramdata {
	u32 v1;
	u32 v2;
	u32 h1;
	u32 h2;
} __attribute__ ((packed));

struct isp3x_rawaf_stat {
	u32 int_state;
	u32 afm_sum_b;
	u32 afm_lum_b;
	u32 highlit_cnt_winb;
	struct isp3x_rawaf_ramdata ramdata[ISP3X_RAWAF_SUMDATA_NUM];
} __attribute__ ((packed));

struct isp3x_cac_cfg {
	u8 bypass_en;
	u8 center_en;

	u8 psf_sft_bit;
	u16 cfg_num;

	u16 center_width;
	u16 center_height;

	u16 strength[ISP3X_CAC_STRENGTH_NUM];

	u32 hsize;
	u32 vsize;
	s32 buf_fd;
} __attribute__ ((packed));

struct isp3x_gain_cfg {
	u32 g0;
	u16 g1;
	u16 g2;
} __attribute__ ((packed));

struct isp3x_isp_other_cfg {
	struct isp21_bls_cfg bls_cfg;
	struct isp2x_dpcc_cfg dpcc_cfg;
	struct isp3x_lsc_cfg lsc_cfg;
	struct isp21_awb_gain_cfg awb_gain_cfg;
	struct isp21_gic_cfg gic_cfg;
	struct isp2x_debayer_cfg debayer_cfg;
	struct isp21_ccm_cfg ccm_cfg;
	struct isp3x_gammaout_cfg gammaout_cfg;
	struct isp2x_cproc_cfg cproc_cfg;
	struct isp2x_ie_cfg ie_cfg;
	struct isp2x_sdg_cfg sdg_cfg;
	struct isp3x_drc_cfg drc_cfg;
	struct isp3x_hdrmge_cfg hdrmge_cfg;
	struct isp3x_dhaz_cfg dhaz_cfg;
	struct isp2x_3dlut_cfg isp3dlut_cfg;
	struct isp2x_ldch_cfg ldch_cfg;
	struct isp3x_baynr_cfg baynr_cfg;
	struct isp3x_bay3d_cfg bay3d_cfg;
	struct isp3x_ynr_cfg ynr_cfg;
	struct isp3x_cnr_cfg cnr_cfg;
	struct isp3x_sharp_cfg sharp_cfg;
	struct isp3x_cac_cfg cac_cfg;
	struct isp3x_gain_cfg gain_cfg;
	struct isp21_csm_cfg csm_cfg;
	struct isp21_cgc_cfg cgc_cfg;
} __attribute__ ((packed));

struct isp3x_isp_meas_cfg {
	struct isp3x_rawaf_meas_cfg rawaf;
	struct isp3x_rawawb_meas_cfg rawawb;
	struct isp2x_rawaelite_meas_cfg rawae0;
	struct isp2x_rawaebig_meas_cfg rawae1;
	struct isp2x_rawaebig_meas_cfg rawae2;
	struct isp2x_rawaebig_meas_cfg rawae3;
	struct isp2x_rawhistlite_cfg rawhist0;
	struct isp2x_rawhistbig_cfg rawhist1;
	struct isp2x_rawhistbig_cfg rawhist2;
	struct isp2x_rawhistbig_cfg rawhist3;
} __attribute__ ((packed));

struct isp3x_isp_params_cfg {
	u64 module_en_update;
	u64 module_ens;
	u64 module_cfg_update;

	u32 frame_id;
	struct isp3x_isp_meas_cfg meas;
	struct isp3x_isp_other_cfg others;
} __attribute__ ((packed));

struct isp3x_stat {
	struct isp2x_rawaebig_stat rawae3;
	struct isp2x_rawaebig_stat rawae1;
	struct isp2x_rawaebig_stat rawae2;
	struct isp2x_rawaelite_stat rawae0;
	struct isp2x_rawhistbig_stat rawhist3;
	struct isp2x_rawhistlite_stat rawhist0;
	struct isp2x_rawhistbig_stat rawhist1;
	struct isp2x_rawhistbig_stat rawhist2;
	struct isp3x_rawaf_stat rawaf;
	struct isp3x_rawawb_meas_stat rawawb;
	struct isp3x_dhaz_stat dhaz;
	struct isp2x_bls_stat bls;
} __attribute__ ((packed));

/**
 * struct rkisp3x_isp_stat_buffer - Rockchip ISP3 Statistics Meta Data
 *
 * @meas_type: measurement types (ISP3X_STAT_ definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp3x_isp_stat_buffer {
	u32 meas_type;
	u32 frame_id;
	struct isp3x_stat params;
} __attribute__ ((packed));

#endif /* _UAPI_RKISP3_CONFIG_H */
