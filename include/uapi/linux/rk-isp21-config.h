/* SPDX-License-Identifier: (GPL-2.0+ WITH Linux-syscall-note) OR MIT
 *
 * Rockchip isp2 driver
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK_ISP21_CONFIG_H
#define _UAPI_RK_ISP21_CONFIG_H

#include <linux/const.h>
#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/rk-isp2-config.h>

#define ISP2X_MODULE_BAYNR			BIT_ULL(36)
#define ISP2X_MODULE_BAY3D			BIT_ULL(37)
#define ISP2X_MODULE_YNR			BIT_ULL(38)
#define ISP2X_MODULE_CNR			BIT_ULL(39)
#define ISP2X_MODULE_SHARP			BIT_ULL(40)
#define ISP2X_MODULE_DRC			BIT_ULL(41)
#define ISP2X_MODULE_CSM			BIT_ULL(43)
#define ISP2X_MODULE_CGC			BIT_ULL(44)

#define ISP21_DHAZ_ENH_CURVE_NUM		17
#define ISP21_DHAZ_HIST_IIR_NUM			64
#define ISP21_RAWAWB_HSTBIN_NUM			8
#define ISP21_RAWAWB_WEIGHT_NUM			225
#define ISP21_DRC_Y_NUM				17
#define ISP21_YNR_XY_NUM			17
#define ISP21_BAYNR_XY_NUM			16
#define ISP21_BAY3D_XY_NUM			16
#define ISP21_SHARP_X_NUM			7
#define ISP21_SHARP_Y_NUM			8
#define ISP21_CSM_COEFF_NUM			9

struct isp21_cgc_cfg {
	__u8 yuv_limit;
	__u8 ratio_en;
} __attribute__ ((packed));

struct isp21_csm_cfg {
	__u8 csm_full_range;
	__u16 csm_y_offset;
	__u16 csm_c_offset;

	__u32 csm_coeff[ISP21_CSM_COEFF_NUM];
} __attribute__ ((packed));

struct isp21_bls_cfg {
	__u8 enable_auto;
	__u8 en_windows;
	__u8 bls1_en;
	struct isp2x_window bls_window1;
	struct isp2x_window bls_window2;
	__u8 bls_samples;
	struct isp2x_bls_fixed_val fixed_val;
	struct isp2x_bls_fixed_val bls1_val;
} __attribute__ ((packed));

struct isp21_awb_gain_cfg {
	__u16 gain0_red;
	__u16 gain0_green_r;
	__u16 gain0_blue;
	__u16 gain0_green_b;
	__u16 gain1_red;
	__u16 gain1_green_r;
	__u16 gain1_blue;
	__u16 gain1_green_b;
	__u16 gain2_red;
	__u16 gain2_green_r;
	__u16 gain2_blue;
	__u16 gain2_green_b;
} __attribute__ ((packed));

struct isp21_gic_cfg {
	__u16 regmingradthrdark2;
	__u16 regmingradthrdark1;
	__u16 regminbusythre;

	__u16 regdarkthre;
	__u16 regmaxcorvboth;
	__u16 regdarktthrehi;

	__u8 regkgrad2dark;
	__u8 regkgrad1dark;
	__u8 regstrengthglobal_fix;
	__u8 regdarkthrestep;
	__u8 regkgrad2;
	__u8 regkgrad1;
	__u8 reggbthre;

	__u16 regmaxcorv;
	__u16 regmingradthr2;
	__u16 regmingradthr1;

	__u8 gr_ratio;
	__u8 noise_scale;
	__u16 noise_base;
	__u16 diff_clip;

	__u16 sigma_y[ISP2X_GIC_SIGMA_Y_NUM];
} __attribute__ ((packed));

struct isp21_baynr_cfg {
	__u8 sw_baynr_gauss_en;
	__u8 sw_baynr_log_bypass;
	__u16 sw_baynr_dgain1;
	__u16 sw_baynr_dgain0;
	__u16 sw_baynr_dgain2;
	__u16 sw_baynr_pix_diff;
	__u16 sw_baynr_diff_thld;
	__u16 sw_baynr_softthld;
	__u16 sw_bltflt_streng;
	__u16 sw_baynr_reg_w1;
	__u16 sw_sigma_x[ISP21_BAYNR_XY_NUM];
	__u16 sw_sigma_y[ISP21_BAYNR_XY_NUM];
	__u16 weit_d2;
	__u16 weit_d1;
	__u16 weit_d0;
} __attribute__ ((packed));

struct isp21_bay3d_cfg {
	__u8 sw_bay3d_exp_sel;
	__u8 sw_bay3d_bypass_en;
	__u8 sw_bay3d_pk_en;
	__u16 sw_bay3d_softwgt;
	__u16 sw_bay3d_sigratio;
	__u32 sw_bay3d_glbpk2;
	__u16 sw_bay3d_exp_str;
	__u16 sw_bay3d_str;
	__u16 sw_bay3d_wgtlmt_h;
	__u16 sw_bay3d_wgtlmt_l;
	__u16 sw_bay3d_sig_x[ISP21_BAY3D_XY_NUM];
	__u16 sw_bay3d_sig_y[ISP21_BAY3D_XY_NUM];
} __attribute__ ((packed));

struct isp21_ynr_cfg {
	__u8 sw_ynr_thumb_mix_cur_en;
	__u8 sw_ynr_global_gain_alpha;
	__u8 sw_ynr_global_gain;
	__u8 sw_ynr_flt1x1_bypass_sel;
	__u8 sw_ynr_sft5x5_bypass;
	__u8 sw_ynr_flt1x1_bypass;
	__u8 sw_ynr_lgft3x3_bypass;
	__u8 sw_ynr_lbft5x5_bypass;
	__u8 sw_ynr_bft3x3_bypass;

	__u16 sw_ynr_rnr_max_r;

	__u16 sw_ynr_low_bf_inv1;
	__u16 sw_ynr_low_bf_inv0;

	__u16 sw_ynr_low_peak_supress;
	__u16 sw_ynr_low_thred_adj;

	__u16 sw_ynr_low_dist_adj;
	__u16 sw_ynr_low_edge_adj_thresh;

	__u16 sw_ynr_low_bi_weight;
	__u16 sw_ynr_low_weight;
	__u16 sw_ynr_low_center_weight;
	__u16 sw_ynr_hi_min_adj;
	__u16 sw_ynr_high_thred_adj;
	__u8 sw_ynr_high_retain_weight;
	__u8 sw_ynr_hi_edge_thed;
	__u8 sw_ynr_base_filter_weight2;
	__u8 sw_ynr_base_filter_weight1;
	__u8 sw_ynr_base_filter_weight0;
	__u16 sw_ynr_low_gauss1_coeff2;
	__u16 sw_ynr_low_gauss1_coeff1;
	__u16 sw_ynr_low_gauss1_coeff0;
	__u16 sw_ynr_low_gauss2_coeff2;
	__u16 sw_ynr_low_gauss2_coeff1;
	__u16 sw_ynr_low_gauss2_coeff0;
	__u8 sw_ynr_direction_weight3;
	__u8 sw_ynr_direction_weight2;
	__u8 sw_ynr_direction_weight1;
	__u8 sw_ynr_direction_weight0;
	__u8 sw_ynr_direction_weight7;
	__u8 sw_ynr_direction_weight6;
	__u8 sw_ynr_direction_weight5;
	__u8 sw_ynr_direction_weight4;
	__u16 sw_ynr_luma_points_x[ISP21_YNR_XY_NUM];
	__u16 sw_ynr_lsgm_y[ISP21_YNR_XY_NUM];
	__u16 sw_ynr_hsgm_y[ISP21_YNR_XY_NUM];
	__u8 sw_ynr_rnr_strength3[ISP21_YNR_XY_NUM];
} __attribute__ ((packed));

struct isp21_cnr_cfg {
	__u8 sw_cnr_thumb_mix_cur_en;
	__u8 sw_cnr_lq_bila_bypass;
	__u8 sw_cnr_hq_bila_bypass;
	__u8 sw_cnr_exgain_bypass;
	__u8 sw_cnr_exgain_mux;
	__u8 sw_cnr_gain_iso;
	__u8 sw_cnr_gain_offset;
	__u8 sw_cnr_gain_1sigma;
	__u8 sw_cnr_gain_uvgain1;
	__u8 sw_cnr_gain_uvgain0;
	__u8 sw_cnr_lmed3_alpha;
	__u8 sw_cnr_lbf5_gain_y;
	__u8 sw_cnr_lbf5_gain_c;
	__u8 sw_cnr_lbf5_weit_d3;
	__u8 sw_cnr_lbf5_weit_d2;
	__u8 sw_cnr_lbf5_weit_d1;
	__u8 sw_cnr_lbf5_weit_d0;
	__u8 sw_cnr_lbf5_weit_d4;
	__u8 sw_cnr_hmed3_alpha;
	__u16 sw_cnr_hbf5_weit_src;
	__u16 sw_cnr_hbf5_min_wgt;
	__u16 sw_cnr_hbf5_sigma;
	__u16 sw_cnr_lbf5_weit_src;
	__u16 sw_cnr_lbf3_sigma;
} __attribute__ ((packed));

struct isp21_sharp_cfg {
	__u8 sw_sharp_bypass;
	__u8 sw_sharp_sharp_ratio;
	__u8 sw_sharp_bf_ratio;
	__u8 sw_sharp_gaus_ratio;
	__u8 sw_sharp_pbf_ratio;
	__u8 sw_sharp_luma_dx[ISP21_SHARP_X_NUM];
	__u16 sw_sharp_pbf_sigma_inv[ISP21_SHARP_Y_NUM];
	__u16 sw_sharp_bf_sigma_inv[ISP21_SHARP_Y_NUM];
	__u8 sw_sharp_bf_sigma_shift;
	__u8 sw_sharp_pbf_sigma_shift;
	__u16 sw_sharp_ehf_th[ISP21_SHARP_Y_NUM];
	__u16 sw_sharp_clip_hf[ISP21_SHARP_Y_NUM];
	__u8 sw_sharp_pbf_coef_2;
	__u8 sw_sharp_pbf_coef_1;
	__u8 sw_sharp_pbf_coef_0;
	__u8 sw_sharp_bf_coef_2;
	__u8 sw_sharp_bf_coef_1;
	__u8 sw_sharp_bf_coef_0;
	__u8 sw_sharp_gaus_coef_2;
	__u8 sw_sharp_gaus_coef_1;
	__u8 sw_sharp_gaus_coef_0;
} __attribute__ ((packed));

struct isp21_ccm_cfg {
	__u8 highy_adjust_dis;
	__u8 bound_bit;

	__s16 coeff0_r;
	__s16 coeff1_r;
	__s16 coeff2_r;
	__s16 offset_r;

	__s16 coeff0_g;
	__s16 coeff1_g;
	__s16 coeff2_g;
	__s16 offset_g;

	__s16 coeff0_b;
	__s16 coeff1_b;
	__s16 coeff2_b;
	__s16 offset_b;

	__u16 coeff0_y;
	__u16 coeff1_y;
	__u16 coeff2_y;

	__u16 alp_y[ISP21_DHAZ_ENH_CURVE_NUM];
} __attribute__ ((packed));

struct isp21_dhaz_cfg {
	__u8 enhance_en;
	__u8 air_lc_en;
	__u8 hpara_en;
	__u8 hist_en;
	__u8 dc_en;

	__u8 yblk_th;
	__u8 yhist_th;
	__u8 dc_max_th;
	__u8 dc_min_th;

	__u16 wt_max;
	__u8 bright_max;
	__u8 bright_min;

	__u8 tmax_base;
	__u8 dark_th;
	__u8 air_max;
	__u8 air_min;

	__u16 tmax_max;
	__u16 tmax_off;

	__u8 hist_k;
	__u8 hist_th_off;
	__u16 hist_min;

	__u16 hist_gratio;
	__u16 hist_scale;

	__u16 enhance_value;
	__u16 enhance_chroma;

	__u16 iir_wt_sigma;
	__u16 iir_sigma;
	__u16 stab_fnum;

	__u16 iir_tmax_sigma;
	__u16 iir_air_sigma;
	__u8 iir_pre_wet;

	__u16 cfg_wt;
	__u16 cfg_air;
	__u16 cfg_alpha;

	__u16 cfg_gratio;
	__u16 cfg_tmax;

	__u16 range_sima;
	__u8 space_sigma_pre;
	__u8 space_sigma_cur;

	__u16 dc_weitcur;
	__u16 bf_weight;

	__u16 enh_curve[ISP21_DHAZ_ENH_CURVE_NUM];

	__u8 gaus_h2;
	__u8 gaus_h1;
	__u8 gaus_h0;
} __attribute__ ((packed));

struct isp21_dhaz_stat {
	__u16 dhaz_adp_air_base;
	__u16 dhaz_adp_wt;

	__u16 dhaz_adp_gratio;
	__u16 dhaz_adp_tmax;

	__u16 h_rgb_iir[ISP21_DHAZ_HIST_IIR_NUM];
} __attribute__ ((packed));

struct isp21_drc_cfg {
	__u8 sw_drc_offset_pow2;
	__u16 sw_drc_compres_scl;
	__u16 sw_drc_position;
	__u16 sw_drc_delta_scalein;
	__u16 sw_drc_hpdetail_ratio;
	__u16 sw_drc_lpdetail_ratio;
	__u8 sw_drc_weicur_pix;
	__u8 sw_drc_weipre_frame;
	__u16 sw_drc_force_sgm_inv0;
	__u8 sw_drc_motion_scl;
	__u8 sw_drc_edge_scl;
	__u16 sw_drc_space_sgm_inv1;
	__u16 sw_drc_space_sgm_inv0;
	__u16 sw_drc_range_sgm_inv1;
	__u16 sw_drc_range_sgm_inv0;
	__u8 sw_drc_weig_maxl;
	__u8 sw_drc_weig_bilat;
	__u16 sw_drc_gain_y[ISP21_DRC_Y_NUM];
	__u16 sw_drc_compres_y[ISP21_DRC_Y_NUM];
	__u16 sw_drc_scale_y[ISP21_DRC_Y_NUM];
	__u16 sw_drc_iir_weight;
	__u16 sw_drc_min_ogain;
} __attribute__ ((packed));

struct isp21_rawawb_meas_cfg {
	__u8 rawawb_sel;
	__u8 sw_rawawb_xy_en0;
	__u8 sw_rawawb_uv_en0;
	__u8 sw_rawawb_xy_en1;
	__u8 sw_rawawb_uv_en1;
	__u8 sw_rawawb_3dyuv_en0;
	__u8 sw_rawawb_3dyuv_en1;
	__u8 sw_rawawb_wp_blk_wei_en0;
	__u8 sw_rawawb_wp_blk_wei_en1;
	__u8 sw_rawawb_wp_luma_wei_en0;
	__u8 sw_rawawb_wp_luma_wei_en1;
	__u8 sw_rawlsc_bypass_en;
	__u8 sw_rawawb_blk_measure_enable;
	__u8 sw_rawawb_blk_measure_mode;
	__u8 sw_rawawb_blk_measure_xytype;
	__u8 sw_rawawb_blk_measure_illu_idx;
	__u8 sw_rawawb_wp_hist_xytype;
	__u8 sw_rawawb_light_num;
	__u8 sw_rawawb_wind_size;
	__u8 sw_rawawb_r_max;
	__u8 sw_rawawb_g_max;
	__u8 sw_rawawb_b_max;
	__u8 sw_rawawb_y_max;
	__u8 sw_rawawb_r_min;
	__u8 sw_rawawb_g_min;
	__u8 sw_rawawb_b_min;
	__u8 sw_rawawb_y_min;
	__u8 sw_rawawb_3dyuv_ls_idx0;
	__u8 sw_rawawb_3dyuv_ls_idx1;
	__u8 sw_rawawb_3dyuv_ls_idx2;
	__u8 sw_rawawb_3dyuv_ls_idx3;
	__u8 sw_rawawb_exc_wp_region0_excen0;
	__u8 sw_rawawb_exc_wp_region0_excen1;
	__u8 sw_rawawb_exc_wp_region0_domain;
	__u8 sw_rawawb_exc_wp_region1_excen0;
	__u8 sw_rawawb_exc_wp_region1_excen1;
	__u8 sw_rawawb_exc_wp_region1_domain;
	__u8 sw_rawawb_exc_wp_region2_excen0;
	__u8 sw_rawawb_exc_wp_region2_excen1;
	__u8 sw_rawawb_exc_wp_region2_domain;
	__u8 sw_rawawb_exc_wp_region3_excen0;
	__u8 sw_rawawb_exc_wp_region3_excen1;
	__u8 sw_rawawb_exc_wp_region3_domain;
	__u8 sw_rawawb_exc_wp_region4_excen0;
	__u8 sw_rawawb_exc_wp_region4_excen1;
	__u8 sw_rawawb_exc_wp_region4_domain;
	__u8 sw_rawawb_exc_wp_region5_excen0;
	__u8 sw_rawawb_exc_wp_region5_excen1;
	__u8 sw_rawawb_exc_wp_region5_domain;
	__u8 sw_rawawb_exc_wp_region6_excen0;
	__u8 sw_rawawb_exc_wp_region6_excen1;
	__u8 sw_rawawb_exc_wp_region6_domain;
	__u8 sw_rawawb_wp_luma_weicurve_y0;
	__u8 sw_rawawb_wp_luma_weicurve_y1;
	__u8 sw_rawawb_wp_luma_weicurve_y2;
	__u8 sw_rawawb_wp_luma_weicurve_y3;
	__u8 sw_rawawb_wp_luma_weicurve_y4;
	__u8 sw_rawawb_wp_luma_weicurve_y5;
	__u8 sw_rawawb_wp_luma_weicurve_y6;
	__u8 sw_rawawb_wp_luma_weicurve_y7;
	__u8 sw_rawawb_wp_luma_weicurve_y8;
	__u8 sw_rawawb_wp_luma_weicurve_w0;
	__u8 sw_rawawb_wp_luma_weicurve_w1;
	__u8 sw_rawawb_wp_luma_weicurve_w2;
	__u8 sw_rawawb_wp_luma_weicurve_w3;
	__u8 sw_rawawb_wp_luma_weicurve_w4;
	__u8 sw_rawawb_wp_luma_weicurve_w5;
	__u8 sw_rawawb_wp_luma_weicurve_w6;
	__u8 sw_rawawb_wp_luma_weicurve_w7;
	__u8 sw_rawawb_wp_luma_weicurve_w8;
	__u8 sw_rawawb_rotu0_ls0;
	__u8 sw_rawawb_rotu1_ls0;
	__u8 sw_rawawb_rotu2_ls0;
	__u8 sw_rawawb_rotu3_ls0;
	__u8 sw_rawawb_rotu4_ls0;
	__u8 sw_rawawb_rotu5_ls0;
	__u8 sw_rawawb_dis_x1x2_ls0;
	__u8 sw_rawawb_rotu0_ls1;
	__u8 sw_rawawb_rotu1_ls1;
	__u8 sw_rawawb_rotu2_ls1;
	__u8 sw_rawawb_rotu3_ls1;
	__u8 sw_rawawb_rotu4_ls1;
	__u8 sw_rawawb_rotu5_ls1;
	__u8 sw_rawawb_dis_x1x2_ls1;
	__u8 sw_rawawb_rotu0_ls2;
	__u8 sw_rawawb_rotu1_ls2;
	__u8 sw_rawawb_rotu2_ls2;
	__u8 sw_rawawb_rotu3_ls2;
	__u8 sw_rawawb_rotu4_ls2;
	__u8 sw_rawawb_rotu5_ls2;
	__u8 sw_rawawb_dis_x1x2_ls2;
	__u8 sw_rawawb_rotu0_ls3;
	__u8 sw_rawawb_rotu1_ls3;
	__u8 sw_rawawb_rotu2_ls3;
	__u8 sw_rawawb_rotu3_ls3;
	__u8 sw_rawawb_rotu4_ls3;
	__u8 sw_rawawb_rotu5_ls3;
	__u8 sw_rawawb_dis_x1x2_ls3;
	__u8 sw_rawawb_blk_rtdw_measure_en;
	__u8 sw_rawawb_blk_with_luma_wei_en;
	__u8 sw_rawawb_wp_blk_wei_w[ISP21_RAWAWB_WEIGHT_NUM];

	__u16 sw_rawawb_h_offs;
	__u16 sw_rawawb_v_offs;
	__u16 sw_rawawb_h_size;
	__u16 sw_rawawb_v_size;
	__u16 sw_rawawb_vertex0_u_0;
	__u16 sw_rawawb_vertex0_v_0;
	__u16 sw_rawawb_vertex1_u_0;
	__u16 sw_rawawb_vertex1_v_0;
	__u16 sw_rawawb_vertex2_u_0;
	__u16 sw_rawawb_vertex2_v_0;
	__u16 sw_rawawb_vertex3_u_0;
	__u16 sw_rawawb_vertex3_v_0;
	__u16 sw_rawawb_vertex0_u_1;
	__u16 sw_rawawb_vertex0_v_1;
	__u16 sw_rawawb_vertex1_u_1;
	__u16 sw_rawawb_vertex1_v_1;
	__u16 sw_rawawb_vertex2_u_1;
	__u16 sw_rawawb_vertex2_v_1;
	__u16 sw_rawawb_vertex3_u_1;
	__u16 sw_rawawb_vertex3_v_1;
	__u16 sw_rawawb_vertex0_u_2;
	__u16 sw_rawawb_vertex0_v_2;
	__u16 sw_rawawb_vertex1_u_2;
	__u16 sw_rawawb_vertex1_v_2;
	__u16 sw_rawawb_vertex2_u_2;
	__u16 sw_rawawb_vertex2_v_2;
	__u16 sw_rawawb_vertex3_u_2;
	__u16 sw_rawawb_vertex3_v_2;
	__u16 sw_rawawb_vertex0_u_3;
	__u16 sw_rawawb_vertex0_v_3;
	__u16 sw_rawawb_vertex1_u_3;
	__u16 sw_rawawb_vertex1_v_3;
	__u16 sw_rawawb_vertex2_u_3;
	__u16 sw_rawawb_vertex2_v_3;
	__u16 sw_rawawb_vertex3_u_3;
	__u16 sw_rawawb_vertex3_v_3;
	__u16 sw_rawawb_vertex0_u_4;
	__u16 sw_rawawb_vertex0_v_4;
	__u16 sw_rawawb_vertex1_u_4;
	__u16 sw_rawawb_vertex1_v_4;
	__u16 sw_rawawb_vertex2_u_4;
	__u16 sw_rawawb_vertex2_v_4;
	__u16 sw_rawawb_vertex3_u_4;
	__u16 sw_rawawb_vertex3_v_4;
	__u16 sw_rawawb_vertex0_u_5;
	__u16 sw_rawawb_vertex0_v_5;
	__u16 sw_rawawb_vertex1_u_5;
	__u16 sw_rawawb_vertex1_v_5;
	__u16 sw_rawawb_vertex2_u_5;
	__u16 sw_rawawb_vertex2_v_5;
	__u16 sw_rawawb_vertex3_u_5;
	__u16 sw_rawawb_vertex3_v_5;
	__u16 sw_rawawb_vertex0_u_6;
	__u16 sw_rawawb_vertex0_v_6;
	__u16 sw_rawawb_vertex1_u_6;
	__u16 sw_rawawb_vertex1_v_6;
	__u16 sw_rawawb_vertex2_u_6;
	__u16 sw_rawawb_vertex2_v_6;
	__u16 sw_rawawb_vertex3_u_6;
	__u16 sw_rawawb_vertex3_v_6;

	__u16 sw_rawawb_wt0;
	__u16 sw_rawawb_wt1;
	__u16 sw_rawawb_wt2;
	__u16 sw_rawawb_mat0_x;
	__u16 sw_rawawb_mat1_x;
	__u16 sw_rawawb_mat2_x;
	__u16 sw_rawawb_mat0_y;
	__u16 sw_rawawb_mat1_y;
	__u16 sw_rawawb_mat2_y;
	__u16 sw_rawawb_nor_x0_0;
	__u16 sw_rawawb_nor_x1_0;
	__u16 sw_rawawb_nor_y0_0;
	__u16 sw_rawawb_nor_y1_0;
	__u16 sw_rawawb_big_x0_0;
	__u16 sw_rawawb_big_x1_0;
	__u16 sw_rawawb_big_y0_0;
	__u16 sw_rawawb_big_y1_0;
	__u16 sw_rawawb_nor_x0_1;
	__u16 sw_rawawb_nor_x1_1;
	__u16 sw_rawawb_nor_y0_1;
	__u16 sw_rawawb_nor_y1_1;
	__u16 sw_rawawb_big_x0_1;
	__u16 sw_rawawb_big_x1_1;
	__u16 sw_rawawb_big_y0_1;
	__u16 sw_rawawb_big_y1_1;
	__u16 sw_rawawb_nor_x0_2;
	__u16 sw_rawawb_nor_x1_2;
	__u16 sw_rawawb_nor_y0_2;
	__u16 sw_rawawb_nor_y1_2;
	__u16 sw_rawawb_big_x0_2;
	__u16 sw_rawawb_big_x1_2;
	__u16 sw_rawawb_big_y0_2;
	__u16 sw_rawawb_big_y1_2;
	__u16 sw_rawawb_nor_x0_3;
	__u16 sw_rawawb_nor_x1_3;
	__u16 sw_rawawb_nor_y0_3;
	__u16 sw_rawawb_nor_y1_3;
	__u16 sw_rawawb_big_x0_3;
	__u16 sw_rawawb_big_x1_3;
	__u16 sw_rawawb_big_y0_3;
	__u16 sw_rawawb_big_y1_3;
	__u16 sw_rawawb_nor_x0_4;
	__u16 sw_rawawb_nor_x1_4;
	__u16 sw_rawawb_nor_y0_4;
	__u16 sw_rawawb_nor_y1_4;
	__u16 sw_rawawb_big_x0_4;
	__u16 sw_rawawb_big_x1_4;
	__u16 sw_rawawb_big_y0_4;
	__u16 sw_rawawb_big_y1_4;
	__u16 sw_rawawb_nor_x0_5;
	__u16 sw_rawawb_nor_x1_5;
	__u16 sw_rawawb_nor_y0_5;
	__u16 sw_rawawb_nor_y1_5;
	__u16 sw_rawawb_big_x0_5;
	__u16 sw_rawawb_big_x1_5;
	__u16 sw_rawawb_big_y0_5;
	__u16 sw_rawawb_big_y1_5;
	__u16 sw_rawawb_nor_x0_6;
	__u16 sw_rawawb_nor_x1_6;
	__u16 sw_rawawb_nor_y0_6;
	__u16 sw_rawawb_nor_y1_6;
	__u16 sw_rawawb_big_x0_6;
	__u16 sw_rawawb_big_x1_6;
	__u16 sw_rawawb_big_y0_6;
	__u16 sw_rawawb_big_y1_6;
	__u16 sw_rawawb_pre_wbgain_inv_r;
	__u16 sw_rawawb_pre_wbgain_inv_g;
	__u16 sw_rawawb_pre_wbgain_inv_b;
	__u16 sw_rawawb_exc_wp_region0_xu0;
	__u16 sw_rawawb_exc_wp_region0_xu1;
	__u16 sw_rawawb_exc_wp_region0_yv0;
	__u16 sw_rawawb_exc_wp_region0_yv1;
	__u16 sw_rawawb_exc_wp_region1_xu0;
	__u16 sw_rawawb_exc_wp_region1_xu1;
	__u16 sw_rawawb_exc_wp_region1_yv0;
	__u16 sw_rawawb_exc_wp_region1_yv1;
	__u16 sw_rawawb_exc_wp_region2_xu0;
	__u16 sw_rawawb_exc_wp_region2_xu1;
	__u16 sw_rawawb_exc_wp_region2_yv0;
	__u16 sw_rawawb_exc_wp_region2_yv1;
	__u16 sw_rawawb_exc_wp_region3_xu0;
	__u16 sw_rawawb_exc_wp_region3_xu1;
	__u16 sw_rawawb_exc_wp_region3_yv0;
	__u16 sw_rawawb_exc_wp_region3_yv1;
	__u16 sw_rawawb_exc_wp_region4_xu0;
	__u16 sw_rawawb_exc_wp_region4_xu1;
	__u16 sw_rawawb_exc_wp_region4_yv0;
	__u16 sw_rawawb_exc_wp_region4_yv1;
	__u16 sw_rawawb_exc_wp_region5_xu0;
	__u16 sw_rawawb_exc_wp_region5_xu1;
	__u16 sw_rawawb_exc_wp_region5_yv0;
	__u16 sw_rawawb_exc_wp_region5_yv1;
	__u16 sw_rawawb_exc_wp_region6_xu0;
	__u16 sw_rawawb_exc_wp_region6_xu1;
	__u16 sw_rawawb_exc_wp_region6_yv0;
	__u16 sw_rawawb_exc_wp_region6_yv1;
	__u16 sw_rawawb_rgb2ryuvmat0_u;
	__u16 sw_rawawb_rgb2ryuvmat1_u;
	__u16 sw_rawawb_rgb2ryuvmat2_u;
	__u16 sw_rawawb_rgb2ryuvofs_u;
	__u16 sw_rawawb_rgb2ryuvmat0_v;
	__u16 sw_rawawb_rgb2ryuvmat1_v;
	__u16 sw_rawawb_rgb2ryuvmat2_v;
	__u16 sw_rawawb_rgb2ryuvofs_v;
	__u16 sw_rawawb_rgb2ryuvmat0_y;
	__u16 sw_rawawb_rgb2ryuvmat1_y;
	__u16 sw_rawawb_rgb2ryuvmat2_y;
	__u16 sw_rawawb_rgb2ryuvofs_y;
	__u16 sw_rawawb_th0_ls0;
	__u16 sw_rawawb_th1_ls0;
	__u16 sw_rawawb_th2_ls0;
	__u16 sw_rawawb_th3_ls0;
	__u16 sw_rawawb_th4_ls0;
	__u16 sw_rawawb_th5_ls0;
	__u16 sw_rawawb_coor_x1_ls0_u;
	__u16 sw_rawawb_coor_x1_ls0_v;
	__u16 sw_rawawb_coor_x1_ls0_y;
	__u16 sw_rawawb_vec_x21_ls0_u;
	__u16 sw_rawawb_vec_x21_ls0_v;
	__u16 sw_rawawb_vec_x21_ls0_y;
	__u16 sw_rawawb_th0_ls1;
	__u16 sw_rawawb_th1_ls1;
	__u16 sw_rawawb_th2_ls1;
	__u16 sw_rawawb_th3_ls1;
	__u16 sw_rawawb_th4_ls1;
	__u16 sw_rawawb_th5_ls1;
	__u16 sw_rawawb_coor_x1_ls1_u;
	__u16 sw_rawawb_coor_x1_ls1_v;
	__u16 sw_rawawb_coor_x1_ls1_y;
	__u16 sw_rawawb_vec_x21_ls1_u;
	__u16 sw_rawawb_vec_x21_ls1_v;
	__u16 sw_rawawb_vec_x21_ls1_y;
	__u16 sw_rawawb_th0_ls2;
	__u16 sw_rawawb_th1_ls2;
	__u16 sw_rawawb_th2_ls2;
	__u16 sw_rawawb_th3_ls2;
	__u16 sw_rawawb_th4_ls2;
	__u16 sw_rawawb_th5_ls2;
	__u16 sw_rawawb_coor_x1_ls2_u;
	__u16 sw_rawawb_coor_x1_ls2_v;
	__u16 sw_rawawb_coor_x1_ls2_y;
	__u16 sw_rawawb_vec_x21_ls2_u;
	__u16 sw_rawawb_vec_x21_ls2_v;
	__u16 sw_rawawb_vec_x21_ls2_y;
	__u16 sw_rawawb_th0_ls3;
	__u16 sw_rawawb_th1_ls3;
	__u16 sw_rawawb_th2_ls3;
	__u16 sw_rawawb_th3_ls3;
	__u16 sw_rawawb_th4_ls3;
	__u16 sw_rawawb_th5_ls3;
	__u16 sw_rawawb_coor_x1_ls3_u;
	__u16 sw_rawawb_coor_x1_ls3_v;
	__u16 sw_rawawb_coor_x1_ls3_y;
	__u16 sw_rawawb_vec_x21_ls3_u;
	__u16 sw_rawawb_vec_x21_ls3_v;
	__u16 sw_rawawb_vec_x21_ls3_y;

	__u32 sw_rawawb_islope01_0;
	__u32 sw_rawawb_islope12_0;
	__u32 sw_rawawb_islope23_0;
	__u32 sw_rawawb_islope30_0;
	__u32 sw_rawawb_islope01_1;
	__u32 sw_rawawb_islope12_1;
	__u32 sw_rawawb_islope23_1;
	__u32 sw_rawawb_islope30_1;
	__u32 sw_rawawb_islope01_2;
	__u32 sw_rawawb_islope12_2;
	__u32 sw_rawawb_islope23_2;
	__u32 sw_rawawb_islope30_2;
	__u32 sw_rawawb_islope01_3;
	__u32 sw_rawawb_islope12_3;
	__u32 sw_rawawb_islope23_3;
	__u32 sw_rawawb_islope30_3;
	__u32 sw_rawawb_islope01_4;
	__u32 sw_rawawb_islope12_4;
	__u32 sw_rawawb_islope23_4;
	__u32 sw_rawawb_islope30_4;
	__u32 sw_rawawb_islope01_5;
	__u32 sw_rawawb_islope12_5;
	__u32 sw_rawawb_islope23_5;
	__u32 sw_rawawb_islope30_5;
	__u32 sw_rawawb_islope01_6;
	__u32 sw_rawawb_islope12_6;
	__u32 sw_rawawb_islope23_6;
	__u32 sw_rawawb_islope30_6;
} __attribute__ ((packed));

struct isp21_isp_other_cfg {
	struct isp21_bls_cfg bls_cfg;
	struct isp2x_dpcc_cfg dpcc_cfg;
	struct isp2x_lsc_cfg lsc_cfg;
	struct isp21_awb_gain_cfg awb_gain_cfg;
	struct isp21_gic_cfg gic_cfg;
	struct isp2x_debayer_cfg debayer_cfg;
	struct isp21_ccm_cfg ccm_cfg;
	struct isp2x_gammaout_cfg gammaout_cfg;
	struct isp2x_cproc_cfg cproc_cfg;
	struct isp2x_ie_cfg ie_cfg;
	struct isp2x_sdg_cfg sdg_cfg;
	struct isp21_drc_cfg drc_cfg;
	struct isp2x_hdrmge_cfg hdrmge_cfg;
	struct isp21_dhaz_cfg dhaz_cfg;
	struct isp2x_3dlut_cfg isp3dlut_cfg;
	struct isp2x_ldch_cfg ldch_cfg;
	struct isp21_baynr_cfg baynr_cfg;
	struct isp21_bay3d_cfg bay3d_cfg;
	struct isp21_ynr_cfg ynr_cfg;
	struct isp21_cnr_cfg cnr_cfg;
	struct isp21_sharp_cfg sharp_cfg;
	struct isp21_csm_cfg csm_cfg;
	struct isp21_cgc_cfg cgc_cfg;
} __attribute__ ((packed));

struct isp21_isp_meas_cfg {
	struct isp2x_siawb_meas_cfg siawb;
	struct isp21_rawawb_meas_cfg rawawb;
	struct isp2x_rawaelite_meas_cfg rawae0;
	struct isp2x_rawaebig_meas_cfg rawae1;
	struct isp2x_rawaebig_meas_cfg rawae2;
	struct isp2x_rawaebig_meas_cfg rawae3;
	struct isp2x_yuvae_meas_cfg yuvae;
	struct isp2x_rawaf_meas_cfg rawaf;
	struct isp2x_siaf_cfg siaf;
	struct isp2x_rawhistlite_cfg rawhist0;
	struct isp2x_rawhistbig_cfg rawhist1;
	struct isp2x_rawhistbig_cfg rawhist2;
	struct isp2x_rawhistbig_cfg rawhist3;
	struct isp2x_sihst_cfg sihst;
} __attribute__ ((packed));

struct isp21_isp_params_cfg {
	__u64 module_en_update;
	__u64 module_ens;
	__u64 module_cfg_update;

	__u32 frame_id;
	struct isp21_isp_meas_cfg meas;
	struct isp21_isp_other_cfg others;
} __attribute__ ((packed));

struct isp21_rawawb_meas_stat {
	__u16 ro_yhist_bin[ISP21_RAWAWB_HSTBIN_NUM];
	__u32 ro_rawawb_sum_rgain_nor[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_rawawb_sum_bgain_nor[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_rawawb_wp_num_nor[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_rawawb_sum_rgain_big[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_rawawb_sum_bgain_big[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_rawawb_wp_num_big[ISP2X_RAWAWB_SUM_NUM];
	struct isp2x_rawawb_ramdata ramdata[ISP2X_RAWAWB_RAMDATA_NUM];
} __attribute__ ((packed));

struct isp21_stat {
	struct isp2x_siawb_stat siawb;
	struct isp21_rawawb_meas_stat rawawb;
	struct isp2x_rawaelite_stat rawae0;
	struct isp2x_rawaebig_stat rawae1;
	struct isp2x_rawaebig_stat rawae2;
	struct isp2x_rawaebig_stat rawae3;
	struct isp2x_yuvae_stat yuvae;
	struct isp2x_rawaf_stat rawaf;
	struct isp2x_siaf_stat siaf;
	struct isp2x_rawhistlite_stat rawhist0;
	struct isp2x_rawhistbig_stat rawhist1;
	struct isp2x_rawhistbig_stat rawhist2;
	struct isp2x_rawhistbig_stat rawhist3;
	struct isp2x_sihst_stat sihst;

	struct isp2x_bls_stat bls;
	struct isp21_dhaz_stat dhaz;
} __attribute__ ((packed));

/**
 * struct rkisp_isp21_stat_buffer - Rockchip ISP2 Statistics Meta Data
 *
 * @meas_type: measurement types (CIFISP_STAT_ definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp_isp21_stat_buffer {
	unsigned int meas_type;
	unsigned int frame_id;
	unsigned int params_id;
	struct isp21_stat params;
} __attribute__ ((packed));

#endif /* _UAPI_RK_ISP21_CONFIG_H */
