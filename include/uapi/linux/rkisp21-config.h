/* SPDX-License-Identifier: (GPL-2.0+ WITH Linux-syscall-note) OR MIT
 *
 * Rockchip isp2 driver
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKISP21_CONFIG_H
#define _UAPI_RKISP21_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>
#include <linux/rkisp2-config.h>

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
	u8 yuv_limit;
	u8 ratio_en;
} __attribute__ ((packed));

struct isp21_csm_cfg {
	u8 csm_full_range;
	u16 csm_y_offset;
	u16 csm_c_offset;

	u32 csm_coeff[ISP21_CSM_COEFF_NUM];
} __attribute__ ((packed));

struct isp21_bls_cfg {
	u8 enable_auto;
	u8 en_windows;
	u8 bls1_en;
	struct isp2x_window bls_window1;
	struct isp2x_window bls_window2;
	u8 bls_samples;
	struct isp2x_bls_fixed_val fixed_val;
	struct isp2x_bls_fixed_val bls1_val;
} __attribute__ ((packed));

struct isp21_awb_gain_cfg {
	u16 gain0_red;
	u16 gain0_green_r;
	u16 gain0_blue;
	u16 gain0_green_b;
	u16 gain1_red;
	u16 gain1_green_r;
	u16 gain1_blue;
	u16 gain1_green_b;
	u16 gain2_red;
	u16 gain2_green_r;
	u16 gain2_blue;
	u16 gain2_green_b;
} __attribute__ ((packed));

struct isp21_gic_cfg {
	u16 regmingradthrdark2;
	u16 regmingradthrdark1;
	u16 regminbusythre;

	u16 regdarkthre;
	u16 regmaxcorvboth;
	u16 regdarktthrehi;

	u8 regkgrad2dark;
	u8 regkgrad1dark;
	u8 regstrengthglobal_fix;
	u8 regdarkthrestep;
	u8 regkgrad2;
	u8 regkgrad1;
	u8 reggbthre;

	u16 regmaxcorv;
	u16 regmingradthr2;
	u16 regmingradthr1;

	u8 gr_ratio;
	u8 noise_scale;
	u16 noise_base;
	u16 diff_clip;

	u16 sigma_y[ISP2X_GIC_SIGMA_Y_NUM];
} __attribute__ ((packed));

struct isp21_baynr_cfg {
	u8 sw_baynr_gauss_en;
	u8 sw_baynr_log_bypass;
	u16 sw_baynr_dgain1;
	u16 sw_baynr_dgain0;
	u16 sw_baynr_dgain2;
	u16 sw_baynr_pix_diff;
	u16 sw_baynr_diff_thld;
	u16 sw_baynr_softthld;
	u16 sw_bltflt_streng;
	u16 sw_baynr_reg_w1;
	u16 sw_sigma_x[ISP21_BAYNR_XY_NUM];
	u16 sw_sigma_y[ISP21_BAYNR_XY_NUM];
	u16 weit_d2;
	u16 weit_d1;
	u16 weit_d0;
} __attribute__ ((packed));

struct isp21_bay3d_cfg {
	u8 sw_bay3d_exp_sel;
	u8 sw_bay3d_bypass_en;
	u8 sw_bay3d_pk_en;
	u16 sw_bay3d_softwgt;
	u16 sw_bay3d_sigratio;
	u32 sw_bay3d_glbpk2;
	u16 sw_bay3d_exp_str;
	u16 sw_bay3d_str;
	u16 sw_bay3d_wgtlmt_h;
	u16 sw_bay3d_wgtlmt_l;
	u16 sw_bay3d_sig_x[ISP21_BAY3D_XY_NUM];
	u16 sw_bay3d_sig_y[ISP21_BAY3D_XY_NUM];
} __attribute__ ((packed));

struct isp21_ynr_cfg {
	u8 sw_ynr_thumb_mix_cur_en;
	u8 sw_ynr_global_gain_alpha;
	u8 sw_ynr_global_gain;
	u8 sw_ynr_flt1x1_bypass_sel;
	u8 sw_ynr_sft5x5_bypass;
	u8 sw_ynr_flt1x1_bypass;
	u8 sw_ynr_lgft3x3_bypass;
	u8 sw_ynr_lbft5x5_bypass;
	u8 sw_ynr_bft3x3_bypass;

	u16 sw_ynr_rnr_max_r;

	u16 sw_ynr_low_bf_inv1;
	u16 sw_ynr_low_bf_inv0;

	u16 sw_ynr_low_peak_supress;
	u16 sw_ynr_low_thred_adj;

	u16 sw_ynr_low_dist_adj;
	u16 sw_ynr_low_edge_adj_thresh;

	u16 sw_ynr_low_bi_weight;
	u16 sw_ynr_low_weight;
	u16 sw_ynr_low_center_weight;
	u16 sw_ynr_hi_min_adj;
	u16 sw_ynr_high_thred_adj;
	u8 sw_ynr_high_retain_weight;
	u8 sw_ynr_hi_edge_thed;
	u8 sw_ynr_base_filter_weight2;
	u8 sw_ynr_base_filter_weight1;
	u8 sw_ynr_base_filter_weight0;
	u16 sw_ynr_low_gauss1_coeff2;
	u16 sw_ynr_low_gauss1_coeff1;
	u16 sw_ynr_low_gauss1_coeff0;
	u16 sw_ynr_low_gauss2_coeff2;
	u16 sw_ynr_low_gauss2_coeff1;
	u16 sw_ynr_low_gauss2_coeff0;
	u8 sw_ynr_direction_weight3;
	u8 sw_ynr_direction_weight2;
	u8 sw_ynr_direction_weight1;
	u8 sw_ynr_direction_weight0;
	u8 sw_ynr_direction_weight7;
	u8 sw_ynr_direction_weight6;
	u8 sw_ynr_direction_weight5;
	u8 sw_ynr_direction_weight4;
	u16 sw_ynr_luma_points_x[ISP21_YNR_XY_NUM];
	u16 sw_ynr_lsgm_y[ISP21_YNR_XY_NUM];
	u16 sw_ynr_hsgm_y[ISP21_YNR_XY_NUM];
	u8 sw_ynr_rnr_strength3[ISP21_YNR_XY_NUM];
} __attribute__ ((packed));

struct isp21_cnr_cfg {
	u8 sw_cnr_thumb_mix_cur_en;
	u8 sw_cnr_lq_bila_bypass;
	u8 sw_cnr_hq_bila_bypass;
	u8 sw_cnr_exgain_bypass;
	u8 sw_cnr_exgain_mux;
	u8 sw_cnr_gain_iso;
	u8 sw_cnr_gain_offset;
	u8 sw_cnr_gain_1sigma;
	u8 sw_cnr_gain_uvgain1;
	u8 sw_cnr_gain_uvgain0;
	u8 sw_cnr_lmed3_alpha;
	u8 sw_cnr_lbf5_gain_y;
	u8 sw_cnr_lbf5_gain_c;
	u8 sw_cnr_lbf5_weit_d3;
	u8 sw_cnr_lbf5_weit_d2;
	u8 sw_cnr_lbf5_weit_d1;
	u8 sw_cnr_lbf5_weit_d0;
	u8 sw_cnr_lbf5_weit_d4;
	u8 sw_cnr_hmed3_alpha;
	u16 sw_cnr_hbf5_weit_src;
	u16 sw_cnr_hbf5_min_wgt;
	u16 sw_cnr_hbf5_sigma;
	u16 sw_cnr_lbf5_weit_src;
	u16 sw_cnr_lbf3_sigma;
} __attribute__ ((packed));

struct isp21_sharp_cfg {
	u8 sw_sharp_bypass;
	u8 sw_sharp_sharp_ratio;
	u8 sw_sharp_bf_ratio;
	u8 sw_sharp_gaus_ratio;
	u8 sw_sharp_pbf_ratio;
	u8 sw_sharp_luma_dx[ISP21_SHARP_X_NUM];
	u16 sw_sharp_pbf_sigma_inv[ISP21_SHARP_Y_NUM];
	u16 sw_sharp_bf_sigma_inv[ISP21_SHARP_Y_NUM];
	u8 sw_sharp_bf_sigma_shift;
	u8 sw_sharp_pbf_sigma_shift;
	u16 sw_sharp_ehf_th[ISP21_SHARP_Y_NUM];
	u16 sw_sharp_clip_hf[ISP21_SHARP_Y_NUM];
	u8 sw_sharp_pbf_coef_2;
	u8 sw_sharp_pbf_coef_1;
	u8 sw_sharp_pbf_coef_0;
	u8 sw_sharp_bf_coef_2;
	u8 sw_sharp_bf_coef_1;
	u8 sw_sharp_bf_coef_0;
	u8 sw_sharp_gaus_coef_2;
	u8 sw_sharp_gaus_coef_1;
	u8 sw_sharp_gaus_coef_0;
} __attribute__ ((packed));

struct isp21_ccm_cfg {
	u8 highy_adjust_dis;
	u8 bound_bit;

	s16 coeff0_r;
	s16 coeff1_r;
	s16 coeff2_r;
	s16 offset_r;

	s16 coeff0_g;
	s16 coeff1_g;
	s16 coeff2_g;
	s16 offset_g;

	s16 coeff0_b;
	s16 coeff1_b;
	s16 coeff2_b;
	s16 offset_b;

	u16 coeff0_y;
	u16 coeff1_y;
	u16 coeff2_y;

	u16 alp_y[ISP21_DHAZ_ENH_CURVE_NUM];
} __attribute__ ((packed));

struct isp21_dhaz_cfg {
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

	u16 enh_curve[ISP21_DHAZ_ENH_CURVE_NUM];

	u8 gaus_h2;
	u8 gaus_h1;
	u8 gaus_h0;
} __attribute__ ((packed));

struct isp21_dhaz_stat {
	u16 dhaz_adp_air_base;
	u16 dhaz_adp_wt;

	u16 dhaz_adp_gratio;
	u16 dhaz_adp_tmax;

	u16 h_rgb_iir[ISP21_DHAZ_HIST_IIR_NUM];
} __attribute__ ((packed));

struct isp21_drc_cfg {
	u8 sw_drc_offset_pow2;
	u16 sw_drc_compres_scl;
	u16 sw_drc_position;
	u16 sw_drc_delta_scalein;
	u16 sw_drc_hpdetail_ratio;
	u16 sw_drc_lpdetail_ratio;
	u8 sw_drc_weicur_pix;
	u8 sw_drc_weipre_frame;
	u16 sw_drc_force_sgm_inv0;
	u8 sw_drc_motion_scl;
	u8 sw_drc_edge_scl;
	u16 sw_drc_space_sgm_inv1;
	u16 sw_drc_space_sgm_inv0;
	u16 sw_drc_range_sgm_inv1;
	u16 sw_drc_range_sgm_inv0;
	u8 sw_drc_weig_maxl;
	u8 sw_drc_weig_bilat;
	u16 sw_drc_gain_y[ISP21_DRC_Y_NUM];
	u16 sw_drc_compres_y[ISP21_DRC_Y_NUM];
	u16 sw_drc_scale_y[ISP21_DRC_Y_NUM];
	u16 sw_drc_iir_weight;
	u16 sw_drc_min_ogain;
} __attribute__ ((packed));

struct isp21_rawawb_meas_cfg {
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
	u8 sw_rawawb_exc_wp_region0_excen0;
	u8 sw_rawawb_exc_wp_region0_excen1;
	u8 sw_rawawb_exc_wp_region0_domain;
	u8 sw_rawawb_exc_wp_region1_excen0;
	u8 sw_rawawb_exc_wp_region1_excen1;
	u8 sw_rawawb_exc_wp_region1_domain;
	u8 sw_rawawb_exc_wp_region2_excen0;
	u8 sw_rawawb_exc_wp_region2_excen1;
	u8 sw_rawawb_exc_wp_region2_domain;
	u8 sw_rawawb_exc_wp_region3_excen0;
	u8 sw_rawawb_exc_wp_region3_excen1;
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
	u8 sw_rawawb_wp_blk_wei_w[ISP21_RAWAWB_WEIGHT_NUM];

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
	u64 module_en_update;
	u64 module_ens;
	u64 module_cfg_update;

	u32 frame_id;
	struct isp21_isp_meas_cfg meas;
	struct isp21_isp_other_cfg others;
} __attribute__ ((packed));

struct isp21_rawawb_meas_stat {
	u16 ro_yhist_bin[ISP21_RAWAWB_HSTBIN_NUM];
	u32 ro_rawawb_sum_rgain_nor[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_sum_bgain_nor[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_wp_num_nor[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_sum_rgain_big[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_sum_bgain_big[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_rawawb_wp_num_big[ISP2X_RAWAWB_SUM_NUM];
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
	struct isp21_stat params;
} __attribute__ ((packed));

#endif /* _UAPI_RKISP21_CONFIG_H */
