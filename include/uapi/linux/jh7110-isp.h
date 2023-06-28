/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * jh7110-isp.h
 *
 * JH7110 ISP driver - user space header file.
 *
 * Copyright © 2023 Starfive Technology Co., Ltd.
 *
 * Author: Su Zejian (zejian.su@starfivetech.com)
 *
 */

#ifndef __JH7110_ISP_H_
#define __JH7110_ISP_H_

#include <linux/v4l2-controls.h>

#define V4L2_CID_USER_JH7110_ISP_WB_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0001)
#define V4L2_CID_USER_JH7110_ISP_CAR_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0002)
#define V4L2_CID_USER_JH7110_ISP_CCM_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0003)
#define V4L2_CID_USER_JH7110_ISP_CFA_SETTING		\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0004)
#define V4L2_CID_USER_JH7110_ISP_CTC_SETTING		\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0005)
#define V4L2_CID_USER_JH7110_ISP_DBC_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0006)
#define V4L2_CID_USER_JH7110_ISP_DNYUV_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0007)
#define V4L2_CID_USER_JH7110_ISP_GMARGB_SETTING		\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0008)
#define V4L2_CID_USER_JH7110_ISP_LCCF_SETTING \
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x0009)
#define V4L2_CID_USER_JH7110_ISP_OBC_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x000a)
#define V4L2_CID_USER_JH7110_ISP_OECF_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x000b)
#define V4L2_CID_USER_JH7110_ISP_R2Y_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x000c)
#define V4L2_CID_USER_JH7110_ISP_SAT_SETTING		\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x000d)
#define V4L2_CID_USER_JH7110_ISP_SHRP_SETTING		\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x000e)
#define V4L2_CID_USER_JH7110_ISP_YCRV_SETTING	\
				(V4L2_CID_USER_JH7110_ISP_BASE + 0x000f)

struct jh7110_isp_wb_gain {
	__u16 gain_r;
	__u16 gain_g;
	__u16 gain_b;
};

struct jh7110_isp_wb_setting {
	__u32 enabled;
	struct jh7110_isp_wb_gain gains;
};

struct jh7110_isp_car_setting {
	__u32 enabled;
};

struct jh7110_isp_ccm_smlow {
	__s32 ccm[3][3];
	__s32 offsets[3];
};

struct jh7110_isp_ccm_setting {
	__u32 enabled;
	struct jh7110_isp_ccm_smlow ccm_smlow;
};

struct jh7110_isp_cfa_params {
	__s32 hv_width;
	__s32 cross_cov;
};

struct jh7110_isp_cfa_setting {
	__u32 enabled;
	struct jh7110_isp_cfa_params settings;
};

struct jh7110_isp_ctc_params {
	__u8 saf_mode;
	__u8 daf_mode;
	__s32 max_gt;
	__s32 min_gt;
};

struct jh7110_isp_ctc_setting {
	__u32 enabled;
	struct jh7110_isp_ctc_params settings;
};

struct jh7110_isp_dbc_params {
	__s32 bad_gt;
	__s32 bad_xt;
};

struct jh7110_isp_dbc_setting {
	__u32 enabled;
	struct jh7110_isp_dbc_params settings;
};

struct jh7110_isp_dnyuv_params {
	__u8 y_sweight[10];
	__u16 y_curve[7];
	__u8 uv_sweight[10];
	__u16 uv_curve[7];
};

struct jh7110_isp_dnyuv_setting {
	__u32 enabled;
	struct jh7110_isp_dnyuv_params settings;
};

struct jh7110_isp_gmargb_point {
	__u16 g_val;
	__u16 sg_val;
};

struct jh7110_isp_gmargb_setting {
	__u32 enabled;
	struct jh7110_isp_gmargb_point curve[15];
};

struct jh7110_isp_lccf_circle {
	__s16 center_x;
	__s16 center_y;
	__u8 radius;
};

struct jh7110_isp_lccf_curve_param {
	__s16 f1;
	__s16 f2;
};

struct jh7110_isp_lccf_setting {
	__u32 enabled;
	struct jh7110_isp_lccf_circle circle;
	struct jh7110_isp_lccf_curve_param r_param;
	struct jh7110_isp_lccf_curve_param gr_param;
	struct jh7110_isp_lccf_curve_param gb_param;
	struct jh7110_isp_lccf_curve_param b_param;
};

struct jh7110_isp_blacklevel_win_size {
	__u32 width;
	__u32 height;
};

struct jh7110_isp_blacklevel_gain {
	__u8 tl_gain;
	__u8 tr_gain;
	__u8 bl_gain;
	__u8 br_gain;
};

struct jh7110_isp_blacklevel_offset {
	__u8 tl_offset;
	__u8 tr_offset;
	__u8 bl_offset;
	__u8 br_offset;
};

struct jh7110_isp_blacklevel_setting {
	__u32 enabled;
	struct jh7110_isp_blacklevel_win_size win_size;
	struct jh7110_isp_blacklevel_gain gain[4];
	struct jh7110_isp_blacklevel_offset offset[4];
};

struct jh7110_isp_oecf_point {
	__u16 x;
	__u16 y;
	__s16 slope;
};

struct jh7110_isp_oecf_setting {
	__u32 enabled;
	struct jh7110_isp_oecf_point r_curve[16];
	struct jh7110_isp_oecf_point gr_curve[16];
	struct jh7110_isp_oecf_point gb_curve[16];
	struct jh7110_isp_oecf_point b_curve[16];
};

struct jh7110_isp_r2y_matrix {
	__s16 m[9];
};

struct jh7110_isp_r2y_setting {
	__u32 enabled;
	struct jh7110_isp_r2y_matrix matrix;
};

struct jh7110_isp_sat_curve {
	__s16 yi_min;
	__s16 yo_ir;
	__s16 yo_min;
	__s16 yo_max;
};

struct jh7110_isp_sat_hue_info {
	__s16 sin;
	__s16 cos;
};

struct jh7110_isp_sat_info {
	__s16 gain_cmab;
	__s16 gain_cmad;
	__s16 threshold_cmb;
	__s16 threshold_cmd;
	__s16 offset_u;
	__s16 offset_v;
	__s16 cmsf;
};

struct jh7110_isp_sat_setting {
	__u32 enabled;
	struct jh7110_isp_sat_curve curve;
	struct jh7110_isp_sat_hue_info hue_info;
	struct jh7110_isp_sat_info sat_info;
};

struct jh7110_isp_sharp_weight {
	__u8 weight[15];
	__u32 recip_wei_sum;
};

struct jh7110_isp_sharp_strength {
	__s16 diff[4];
	__s16 f[4];
};

struct jh7110_isp_sharp_setting {
	__u32 enabled;
	struct jh7110_isp_sharp_weight weight;
	struct jh7110_isp_sharp_strength strength;
	__s8 pdirf;
	__s8 ndirf;
};

struct jh7110_isp_ycrv_curve {
	__s16 y[64];
};

struct jh7110_isp_ycrv_setting {
	__u32 enabled;
	struct jh7110_isp_ycrv_curve curve;
};

#endif
