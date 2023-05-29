/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT)
 *
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK_ISPP_CONFIG_H
#define _UAPI_RK_ISPP_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define ISPP_API_VERSION		KERNEL_VERSION(1, 8, 0)

#define ISPP_ID_TNR			(0)
#define ISPP_ID_NR			(1)
#define ISPP_ID_SHP			(2)
#define ISPP_ID_FEC			(3)
#define ISPP_ID_ORB			(4)
#define ISPP_ID_MAX			(5)

#define ISPP_MODULE_TNR			BIT(ISPP_ID_TNR)/* 2TO1 */
#define ISPP_MODULE_NR			BIT(ISPP_ID_NR)
#define ISPP_MODULE_SHP			BIT(ISPP_ID_SHP)
#define ISPP_MODULE_FEC			BIT(ISPP_ID_FEC)/* CALIBRATION */
#define ISPP_MODULE_ORB			BIT(ISPP_ID_ORB)
/* extra function */
#define ISPP_MODULE_TNR_3TO1		(BIT(16) | ISPP_MODULE_TNR)
#define ISPP_MODULE_FEC_ST		(BIT(17) | ISPP_MODULE_FEC)/* STABILIZATION */

#define TNR_SIGMA_CURVE_SIZE		17
#define TNR_LUMA_CURVE_SIZE		6
#define TNR_GFCOEF6_SIZE		6
#define TNR_GFCOEF3_SIZE		3
#define TNR_SCALE_YG_SIZE		4
#define TNR_SCALE_YL_SIZE		3
#define TNR_SCALE_CG_SIZE		3
#define TNR_SCALE_Y2CG_SIZE		3
#define TNR_SCALE_CL_SIZE		2
#define TNR_SCALE_Y2CL_SIZE		3
#define TNR_WEIGHT_Y_SIZE		3

#define NR_UVNR_UVGAIN_SIZE		2
#define NR_UVNR_T1FLT_WTQ_SIZE		8
#define NR_UVNR_T2GEN_WTQ_SIZE		4
#define NR_UVNR_T2FLT_WT_SIZE		3
#define NR_YNR_SGM_DX_SIZE		16
#define NR_YNR_SGM_Y_SIZE		17
#define NR_YNR_HWEIT_D_SIZE		20
#define NR_YNR_HGRAD_Y_SIZE		24
#define NR_YNR_HSTV_Y_SIZE		17
#define NR_YNR_CI_SIZE			4
#define NR_YNR_LGAIN_MIN_SIZE		4
#define NR_YNR_LWEIT_FLT_SIZE		4
#define NR_YNR_HGAIN_SGM_SIZE		4
#define NR_YNR_HWEIT_SIZE		4
#define NR_YNR_LWEIT_CMP_SIZE		2
#define NR_YNR_ST_SCALE_SIZE		3

#define SHP_PBF_KERNEL_SIZE		3
#define SHP_MRF_KERNEL_SIZE		6
#define SHP_MBF_KERNEL_SIZE		12
#define SHP_HRF_KERNEL_SIZE		6
#define SHP_HBF_KERNEL_SIZE		3
#define SHP_EDGE_COEF_SIZE		3
#define SHP_EDGE_SMOTH_SIZE		3
#define SHP_EDGE_GAUS_SIZE		6
#define SHP_DOG_KERNEL_SIZE		6
#define SHP_LUM_POINT_SIZE		6
#define SHP_SIGMA_SIZE			8
#define SHP_LUM_CLP_SIZE		8
#define SHP_LUM_MIN_SIZE		8
#define SHP_EDGE_LUM_THED_SIZE		8
#define SHP_CLAMP_SIZE			8
#define SHP_DETAIL_ALPHA_SIZE		8

#define ORB_DATA_NUM			10000
#define ORB_BRIEF_NUM			15
#define ORB_DUMMY_NUM			13

#define FEC_MESH_BUF_MAX		7
#define FEC_MESH_BUF_NUM		2

#define MAX_BUF_IDXFD_NUM		64

/************VIDIOC_PRIVATE*************/
#define RKISPP_CMD_SET_INIT_MODULE	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, int)

#define RKISPP_CMD_GET_FECBUF_INFO	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 1, struct rkispp_fecbuf_info)

#define RKISPP_CMD_SET_FECBUF_SIZE	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 2, struct rkispp_fecbuf_size)

#define RKISPP_CMD_TRIGGER_MODE		\
	_IOW('V', BASE_VIDIOC_PRIVATE + 3, struct rkispp_trigger_mode)

#define RKISPP_CMD_GET_TNRBUF_FD	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 4, struct rkispp_buf_idxfd)

#define RKISPP_CMD_GET_NRBUF_FD		\
	_IOR('V', BASE_VIDIOC_PRIVATE + 5, struct rkispp_buf_idxfd)

/**independent fec video**/
#define RKISPP_CMD_FEC_IN_OUT \
	_IOW('V', BASE_VIDIOC_PRIVATE + 10, struct rkispp_fec_in_out)
#define RKISPP_CMD_FEC_BUF_ADD \
	_IOW('V', BASE_VIDIOC_PRIVATE + 11, int)
#define RKISPP_CMD_FEC_BUF_DEL \
	_IOW('V', BASE_VIDIOC_PRIVATE + 12, int)

/************EVENT_PRIVATE**************/
#define RKISPP_V4L2_EVENT_TNR_COMPLETE  \
	(V4L2_EVENT_PRIVATE_START + 3)

struct rkispp_fec_in_out {
	int in_width;
	int in_height;
	int out_width;
	int out_height;
	int in_fourcc;
	int out_fourcc;
	int in_pic_fd;
	int out_pic_fd;
	int mesh_xint_fd;
	int mesh_xfra_fd;
	int mesh_yint_fd;
	int mesh_yfra_fd;
};

struct rkispp_buf_idxfd {
	__u32 buf_num;
	__u32 index[MAX_BUF_IDXFD_NUM];
	__s32 dmafd[MAX_BUF_IDXFD_NUM];
} __attribute__ ((packed));

struct rkispp_trigger_mode {
	__u32 module;
	__u32 on;
} __attribute__ ((packed));

struct rkispp_tnr_config {
	__u8 opty_en;
	__u8 optc_en;
	__u8 gain_en;
	__u8 pk0_y;
	__u8 pk1_y;
	__u8 pk0_c;
	__u8 pk1_c;
	__u8 glb_gain_cur_sqrt;
	__u8 sigma_x[TNR_SIGMA_CURVE_SIZE - 1];
	__u8 gfcoef_y0[TNR_GFCOEF6_SIZE];
	__u8 gfcoef_y1[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_y2[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_y3[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_yg0[TNR_GFCOEF6_SIZE];
	__u8 gfcoef_yg1[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_yg2[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_yg3[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_yl0[TNR_GFCOEF6_SIZE];
	__u8 gfcoef_yl1[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_yl2[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_cg0[TNR_GFCOEF6_SIZE];
	__u8 gfcoef_cg1[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_cg2[TNR_GFCOEF3_SIZE];
	__u8 gfcoef_cl0[TNR_GFCOEF6_SIZE];
	__u8 gfcoef_cl1[TNR_GFCOEF3_SIZE];
	__u8 weight_y[TNR_WEIGHT_Y_SIZE];

	__u16 glb_gain_cur __attribute__((aligned(2)));
	__u16 glb_gain_nxt;
	__u16 glb_gain_cur_div;
	__u16 txt_th1_y;
	__u16 txt_th0_c;
	__u16 txt_th1_c;
	__u16 txt_thy_dlt;
	__u16 txt_thc_dlt;
	__u16 txt_th0_y;
	__u16 sigma_y[TNR_SIGMA_CURVE_SIZE];
	__u16 luma_curve[TNR_LUMA_CURVE_SIZE];
	__u16 scale_yg[TNR_SCALE_YG_SIZE];
	__u16 scale_yl[TNR_SCALE_YL_SIZE];
	__u16 scale_cg[TNR_SCALE_CG_SIZE];
	__u16 scale_y2cg[TNR_SCALE_Y2CG_SIZE];
	__u16 scale_cl[TNR_SCALE_CL_SIZE];
	__u16 scale_y2cl[TNR_SCALE_Y2CL_SIZE];
} __attribute__ ((packed));

struct rkispp_nr_config {
	__u8 uvnr_step1_en;
	__u8 uvnr_step2_en;
	__u8 nr_gain_en;
	__u8 uvnr_sd32_self_en;
	__u8 uvnr_nobig_en;
	__u8 uvnr_big_en;
	__u8 uvnr_gain_1sigma;
	__u8 uvnr_gain_offset;
	__u8 uvnr_gain_t2gen;
	__u8 uvnr_gain_iso;
	__u8 uvnr_t1gen_m3alpha;
	__u8 uvnr_t1flt_mode;
	__u8 uvnr_t1flt_wtp;
	__u8 uvnr_t2gen_m3alpha;
	__u8 uvnr_t2gen_wtp;
	__u8 uvnr_gain_uvgain[NR_UVNR_UVGAIN_SIZE];
	__u8 uvnr_t1flt_wtq[NR_UVNR_T1FLT_WTQ_SIZE];
	__u8 uvnr_t2gen_wtq[NR_UVNR_T2GEN_WTQ_SIZE];
	__u8 uvnr_t2flt_wtp;
	__u8 uvnr_t2flt_wt[NR_UVNR_T2FLT_WT_SIZE];
	__u8 ynr_sgm_dx[NR_YNR_SGM_DX_SIZE];
	__u8 ynr_lci[NR_YNR_CI_SIZE];
	__u8 ynr_lgain_min[NR_YNR_LGAIN_MIN_SIZE];
	__u8 ynr_lgain_max;
	__u8 ynr_lmerge_bound;
	__u8 ynr_lmerge_ratio;
	__u8 ynr_lweit_flt[NR_YNR_LWEIT_FLT_SIZE];
	__u8 ynr_hlci[NR_YNR_CI_SIZE];
	__u8 ynr_lhci[NR_YNR_CI_SIZE];
	__u8 ynr_hhci[NR_YNR_CI_SIZE];
	__u8 ynr_hgain_sgm[NR_YNR_HGAIN_SGM_SIZE];
	__u8 ynr_hweit_d[NR_YNR_HWEIT_D_SIZE];
	__u8 ynr_hgrad_y[NR_YNR_HGRAD_Y_SIZE];
	__u8 ynr_hmax_adjust;
	__u8 ynr_hstrength;
	__u8 ynr_lweit_cmp[NR_YNR_LWEIT_CMP_SIZE];
	__u8 ynr_lmaxgain_lv4;

	__u16 uvnr_t1flt_msigma __attribute__((aligned(2)));
	__u16 uvnr_t2gen_msigma;
	__u16 uvnr_t2flt_msigma;
	__u16 ynr_lsgm_y[NR_YNR_SGM_Y_SIZE];
	__u16 ynr_hsgm_y[NR_YNR_SGM_Y_SIZE];
	__u16 ynr_hweit[NR_YNR_HWEIT_SIZE];
	__u16 ynr_hstv_y[NR_YNR_HSTV_Y_SIZE];
	__u16 ynr_st_scale[NR_YNR_ST_SCALE_SIZE];
} __attribute__ ((packed));

struct rkispp_sharp_config {
	__u8 rotation;
	__u8 scl_down_v;
	__u8 scl_down_h;
	__u8 tile_ycnt;
	__u8 tile_xcnt;
	__u8 alpha_adp_en;
	__u8 yin_flt_en;
	__u8 edge_avg_en;
	__u8 ehf_th;
	__u8 pbf_ratio;
	__u8 edge_thed;
	__u8 dir_min;
	__u8 pbf_shf_bits;
	__u8 mbf_shf_bits;
	__u8 hbf_shf_bits;
	__u8 m_ratio;
	__u8 h_ratio;
	__u8 pbf_k[SHP_PBF_KERNEL_SIZE];
	__u8 mrf_k[SHP_MRF_KERNEL_SIZE];
	__u8 mbf_k[SHP_MBF_KERNEL_SIZE];
	__u8 hrf_k[SHP_HRF_KERNEL_SIZE];
	__u8 hbf_k[SHP_HBF_KERNEL_SIZE];
	__s8 eg_coef[SHP_EDGE_COEF_SIZE];
	__u8 eg_smoth[SHP_EDGE_SMOTH_SIZE];
	__u8 eg_gaus[SHP_EDGE_GAUS_SIZE];
	__s8 dog_k[SHP_DOG_KERNEL_SIZE];
	__u8 lum_point[SHP_LUM_POINT_SIZE];
	__u8 pbf_sigma[SHP_SIGMA_SIZE];
	__u8 lum_clp_m[SHP_LUM_CLP_SIZE];
	__s8 lum_min_m[SHP_LUM_MIN_SIZE];
	__u8 mbf_sigma[SHP_SIGMA_SIZE];
	__u8 lum_clp_h[SHP_LUM_CLP_SIZE];
	__u8 hbf_sigma[SHP_SIGMA_SIZE];
	__u8 edge_lum_thed[SHP_EDGE_LUM_THED_SIZE];
	__u8 clamp_pos[SHP_CLAMP_SIZE];
	__u8 clamp_neg[SHP_CLAMP_SIZE];
	__u8 detail_alpha[SHP_DETAIL_ALPHA_SIZE];

	__u16 hbf_ratio __attribute__((aligned(2)));
	__u16 smoth_th4;
	__u16 l_alpha;
	__u16 g_alpha;
	__u16 rfl_ratio;
	__u16 rfh_ratio;
} __attribute__ ((packed));

enum rkispp_fecbuf_stat {
	FEC_BUF_INIT = 0,
	FEC_BUF_WAIT2CHIP,
	FEC_BUF_CHIPINUSE,
};

struct rkispp_fecbuf_info {
	__s32 buf_fd[FEC_MESH_BUF_MAX];
	__u32 buf_size[FEC_MESH_BUF_MAX];
} __attribute__ ((packed));

struct rkispp_fecbuf_size {
	__u32 meas_width;
	__u32 meas_height;
	__u32 meas_mode;
	int buf_cnt;
} __attribute__ ((packed));

struct rkispp_fec_head {
	enum rkispp_fecbuf_stat stat;
	__u32 meshxf_oft;
	__u32 meshyf_oft;
	__u32 meshxi_oft;
	__u32 meshyi_oft;
} __attribute__ ((packed));

struct rkispp_fec_config {
	__u8 mesh_density;
	__u8 crop_en;
	__u16 crop_width __attribute__((aligned(2)));
	__u16 crop_height;
	__u32 mesh_size __attribute__((aligned(4)));
	__s32 buf_fd;
} __attribute__ ((packed));

struct rkispp_orb_config {
	__u8 limit_value;
	__u32 max_feature __attribute__((aligned(4)));
} __attribute__ ((packed));

struct rkispp_buf_info {
	/* __s32 fd; */
	__u32 index;
	__u32 size;
} __attribute__ ((packed));

/**
 * struct rkispp_params_cfghead - Rockchip ISPP Input Parameters Meta Data
 *
 * @module_en_update: mask the enable bits of which module  should be updated
 * @module_ens: mask the enable value of each module, only update the module
 * which correspond bit was set in module_en_update
 * @module_cfg_update: mask the config bits of which module  should be updated
 */
struct rkispp_params_cfghead {
	__u32 module_en_update;
	__u32 module_ens;
	__u32 module_cfg_update;

	__u32 frame_id;
} __attribute__ ((packed));

/**
 * struct rkispp_params_tnrcfg - Rockchip ISPP Input Parameters Meta Data
 */
struct rkispp_params_tnrcfg {
	struct rkispp_params_cfghead head;

	struct rkispp_tnr_config tnr_cfg;
	/* struct rkispp_buf_info gain; */
	/* struct rkispp_buf_info image; */
} __attribute__ ((packed));

/**
 * struct rkispp_params_nrcfg - Rockchip ISPP Input Parameters Meta Data
 */
struct rkispp_params_nrcfg {
	struct rkispp_params_cfghead head;

	struct rkispp_nr_config nr_cfg;
	struct rkispp_sharp_config shp_cfg;
	struct rkispp_orb_config orb_cfg;

	struct rkispp_buf_info gain;
	/* struct rkispp_buf_info image; */
} __attribute__ ((packed));

/**
 * struct rkispp_params_feccfg - Rockchip ISPP Input Parameters Meta Data
 */
struct rkispp_params_feccfg {
	struct rkispp_params_cfghead head;

	struct rkispp_fec_config fec_cfg;
	struct rkispp_buf_info image;
} __attribute__ ((packed));

struct rkispp_orb_data {
	__u8 brief[ORB_BRIEF_NUM];
	__u32 y : 13;
	__u32 x : 13;
	__u32 dmy1 : 6;
	__u8 dmy2[ORB_DUMMY_NUM];
} __attribute__ ((packed));

/**
 * struct rkispp_stats_nrbuf - Rockchip ISPP Statistics
 *
 * @meas_type: measurement types
 * @frame_id: frame ID for sync
 * @data: statistics data
 */
struct rkispp_stats_nrbuf {
	struct rkispp_orb_data data[ORB_DATA_NUM];

	__u32 total_num __attribute__((aligned(4)));
	__u32 meas_type;
	__u32 frame_id;

	struct rkispp_buf_info image;
} __attribute__ ((packed));

/**
 * struct rkispp_stats_tnrbuf - Rockchip ISPP Statistics
 *
 * @meas_type: measurement types
 * @frame_id: frame ID for sync
 */
struct rkispp_stats_tnrbuf {
	__u32 meas_type;
	__u32 frame_id;

	struct rkispp_buf_info gain;
	struct rkispp_buf_info gainkg;
	/* struct rkispp_buf_info image; */
} __attribute__ ((packed));

#endif
