/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT)
 *
 * Rockchip isp2 driver
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKISP2_CONFIG_H
#define _UAPI_RKISP2_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define RKISP_API_VERSION		KERNEL_VERSION(1, 9, 0)

/****************ISP SUBDEV IOCTL*****************************/

#define RKISP_CMD_TRIGGER_READ_BACK \
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct isp2x_csi_trigger)

#define RKISP_CMD_GET_ISP_INFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 1, struct rkisp_isp_info)

#define RKISP_CMD_GET_SHARED_BUF \
	_IOR('V', BASE_VIDIOC_PRIVATE + 2, struct rkisp_thunderboot_resmem)

#define RKISP_CMD_FREE_SHARED_BUF \
	_IO('V', BASE_VIDIOC_PRIVATE + 3)

#define RKISP_CMD_GET_LDCHBUF_INFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 4, struct rkisp_ldchbuf_info)

#define RKISP_CMD_SET_LDCHBUF_SIZE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 5, struct rkisp_ldchbuf_size)

#define RKISP_CMD_GET_SHM_BUFFD \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, struct rkisp_thunderboot_shmem)

#define RKISP_CMD_GET_FBCBUF_FD \
	_IOR('V', BASE_VIDIOC_PRIVATE + 7, struct isp2x_buf_idxfd)

#define RKISP_CMD_GET_MESHBUF_INFO \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 8, struct rkisp_meshbuf_info)

#define RKISP_CMD_SET_MESHBUF_SIZE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 9, struct rkisp_meshbuf_size)

#define RKISP_CMD_INFO2DDR \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 10, struct rkisp_info2ddr)

#define RKISP_CMD_MESHBUF_FREE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 11, long long)

/* BASE_VIDIOC_PRIVATE + 12 for RKISP_CMD_GET_TB_HEAD_V32 */

/****************ISP VIDEO IOCTL******************************/

#define RKISP_CMD_GET_CSI_MEMORY_MODE \
	_IOR('V', BASE_VIDIOC_PRIVATE + 100, int)

#define RKISP_CMD_SET_CSI_MEMORY_MODE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 101, int)

#define RKISP_CMD_GET_CMSK \
	_IOR('V', BASE_VIDIOC_PRIVATE + 102, struct rkisp_cmsk_cfg)

#define RKISP_CMD_SET_CMSK \
	_IOW('V', BASE_VIDIOC_PRIVATE + 103, struct rkisp_cmsk_cfg)

#define RKISP_CMD_GET_STREAM_INFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 104, struct rkisp_stream_info)

#define RKISP_CMD_GET_MIRROR_FLIP \
	_IOR('V', BASE_VIDIOC_PRIVATE + 105, struct rkisp_mirror_flip)

#define RKISP_CMD_SET_MIRROR_FLIP \
	_IOW('V', BASE_VIDIOC_PRIVATE + 106, struct rkisp_mirror_flip)

#define RKISP_CMD_GET_WRAP_LINE \
	_IOR('V', BASE_VIDIOC_PRIVATE + 107, int)
/* set wrap line before VIDIOC_S_FMT */
#define RKISP_CMD_SET_WRAP_LINE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 108, int)

#define RKISP_CMD_SET_FPS \
	_IOW('V', BASE_VIDIOC_PRIVATE + 109, int)

#define RKISP_CMD_GET_FPS \
	_IOR('V', BASE_VIDIOC_PRIVATE + 110, int)
/*************************************************************/

#define ISP2X_ID_DPCC			(0)
#define ISP2X_ID_BLS			(1)
#define ISP2X_ID_SDG			(2)
#define ISP2X_ID_SIHST			(3)
#define ISP2X_ID_LSC			(4)
#define ISP2X_ID_AWB_GAIN		(5)
#define ISP2X_ID_BDM			(7)
#define ISP2X_ID_CCM			(8)
#define ISP2X_ID_GOC			(9)
#define ISP2X_ID_CPROC			(10)
#define ISP2X_ID_SIAF			(11)
#define ISP2X_ID_SIAWB			(12)
#define ISP2X_ID_IE			(13)
#define ISP2X_ID_YUVAE			(14)
#define ISP2X_ID_WDR			(15)
#define ISP2X_ID_RK_IESHARP		(16)
#define ISP2X_ID_RAWAF			(17)
#define ISP2X_ID_RAWAE0			(18)
#define ISP2X_ID_RAWAE1			(19)
#define ISP2X_ID_RAWAE2			(20)
#define ISP2X_ID_RAWAE3			(21)
#define ISP2X_ID_RAWAWB			(22)
#define ISP2X_ID_RAWHIST0		(23)
#define ISP2X_ID_RAWHIST1		(24)
#define ISP2X_ID_RAWHIST2		(25)
#define ISP2X_ID_RAWHIST3		(26)
#define ISP2X_ID_HDRMGE			(27)
#define ISP2X_ID_RAWNR			(28)
#define ISP2X_ID_HDRTMO			(29)
#define ISP2X_ID_GIC			(30)
#define ISP2X_ID_DHAZ			(31)
#define ISP2X_ID_3DLUT			(32)
#define ISP2X_ID_LDCH			(33)
#define ISP2X_ID_GAIN			(34)
#define ISP2X_ID_DEBAYER		(35)
#define ISP2X_ID_MAX			(63)

#define ISP2X_MODULE_DPCC		BIT_ULL(ISP2X_ID_DPCC)
#define ISP2X_MODULE_BLS		BIT_ULL(ISP2X_ID_BLS)
#define ISP2X_MODULE_SDG		BIT_ULL(ISP2X_ID_SDG)
#define ISP2X_MODULE_SIHST		BIT_ULL(ISP2X_ID_SIHST)
#define ISP2X_MODULE_LSC		BIT_ULL(ISP2X_ID_LSC)
#define ISP2X_MODULE_AWB_GAIN		BIT_ULL(ISP2X_ID_AWB_GAIN)
#define ISP2X_MODULE_BDM		BIT_ULL(ISP2X_ID_BDM)
#define ISP2X_MODULE_CCM		BIT_ULL(ISP2X_ID_CCM)
#define ISP2X_MODULE_GOC		BIT_ULL(ISP2X_ID_GOC)
#define ISP2X_MODULE_CPROC		BIT_ULL(ISP2X_ID_CPROC)
#define ISP2X_MODULE_SIAF		BIT_ULL(ISP2X_ID_SIAF)
#define ISP2X_MODULE_SIAWB		BIT_ULL(ISP2X_ID_SIAWB)
#define ISP2X_MODULE_IE			BIT_ULL(ISP2X_ID_IE)
#define ISP2X_MODULE_YUVAE		BIT_ULL(ISP2X_ID_YUVAE)
#define ISP2X_MODULE_WDR		BIT_ULL(ISP2X_ID_WDR)
#define ISP2X_MODULE_RK_IESHARP		BIT_ULL(ISP2X_ID_RK_IESHARP)
#define ISP2X_MODULE_RAWAF		BIT_ULL(ISP2X_ID_RAWAF)
#define ISP2X_MODULE_RAWAE0		BIT_ULL(ISP2X_ID_RAWAE0)
#define ISP2X_MODULE_RAWAE1		BIT_ULL(ISP2X_ID_RAWAE1)
#define ISP2X_MODULE_RAWAE2		BIT_ULL(ISP2X_ID_RAWAE2)
#define ISP2X_MODULE_RAWAE3		BIT_ULL(ISP2X_ID_RAWAE3)
#define ISP2X_MODULE_RAWAWB		BIT_ULL(ISP2X_ID_RAWAWB)
#define ISP2X_MODULE_RAWHIST0		BIT_ULL(ISP2X_ID_RAWHIST0)
#define ISP2X_MODULE_RAWHIST1		BIT_ULL(ISP2X_ID_RAWHIST1)
#define ISP2X_MODULE_RAWHIST2		BIT_ULL(ISP2X_ID_RAWHIST2)
#define ISP2X_MODULE_RAWHIST3		BIT_ULL(ISP2X_ID_RAWHIST3)
#define ISP2X_MODULE_HDRMGE		BIT_ULL(ISP2X_ID_HDRMGE)
#define ISP2X_MODULE_RAWNR		BIT_ULL(ISP2X_ID_RAWNR)
#define ISP2X_MODULE_HDRTMO		BIT_ULL(ISP2X_ID_HDRTMO)
#define ISP2X_MODULE_GIC		BIT_ULL(ISP2X_ID_GIC)
#define ISP2X_MODULE_DHAZ		BIT_ULL(ISP2X_ID_DHAZ)
#define ISP2X_MODULE_3DLUT		BIT_ULL(ISP2X_ID_3DLUT)
#define ISP2X_MODULE_LDCH		BIT_ULL(ISP2X_ID_LDCH)
#define ISP2X_MODULE_GAIN		BIT_ULL(ISP2X_ID_GAIN)
#define ISP2X_MODULE_DEBAYER		BIT_ULL(ISP2X_ID_DEBAYER)

#define ISP2X_MODULE_FORCE		BIT_ULL(ISP2X_ID_MAX)

/*
 * Measurement types
 */
#define ISP2X_STAT_SIAWB		BIT(0)
#define ISP2X_STAT_YUVAE		BIT(1)
#define ISP2X_STAT_SIAF			BIT(2)
#define ISP2X_STAT_SIHST		BIT(3)
#define ISP2X_STAT_EMB_DATA		BIT(4)
#define ISP2X_STAT_RAWAWB		BIT(5)
#define ISP2X_STAT_RAWAF		BIT(6)
#define ISP2X_STAT_RAWAE0		BIT(7)
#define ISP2X_STAT_RAWAE1		BIT(8)
#define ISP2X_STAT_RAWAE2		BIT(9)
#define ISP2X_STAT_RAWAE3		BIT(10)
#define ISP2X_STAT_RAWHST0		BIT(11)
#define ISP2X_STAT_RAWHST1		BIT(12)
#define ISP2X_STAT_RAWHST2		BIT(13)
#define ISP2X_STAT_RAWHST3		BIT(14)
#define ISP2X_STAT_BLS			BIT(15)
#define ISP2X_STAT_HDRTMO		BIT(16)
#define ISP2X_STAT_DHAZ			BIT(17)

#define ISP2X_LSC_GRAD_TBL_SIZE		8
#define ISP2X_LSC_SIZE_TBL_SIZE		8
#define ISP2X_LSC_DATA_TBL_SIZE		290

#define ISP2X_DEGAMMA_CURVE_SIZE	17

#define ISP2X_GAIN_HDRMGE_GAIN_NUM	3
#define ISP2X_GAIN_IDX_NUM		15
#define ISP2X_GAIN_LUT_NUM		17

#define ISP2X_AWB_MAX_GRID		1
#define ISP2X_RAWAWB_SUM_NUM		7
#define ISP2X_RAWAWB_MULWD_NUM		8
#define ISP2X_RAWAWB_RAMDATA_NUM	225

#define ISP2X_RAWAEBIG_SUBWIN_NUM	4
#define ISP2X_RAWAEBIG_MEAN_NUM		225
#define ISP2X_RAWAELITE_MEAN_NUM	25
#define ISP2X_YUVAE_SUBWIN_NUM		4
#define ISP2X_YUVAE_MEAN_NUM		225

#define ISP2X_RAWHISTBIG_SUBWIN_NUM	225
#define ISP2X_RAWHISTLITE_SUBWIN_NUM	25
#define ISP2X_SIHIST_WIN_NUM		1
#define ISP2X_HIST_WEIGHT_NUM		225
#define ISP2X_HIST_BIN_N_MAX		256
#define ISP2X_SIHIST_BIN_N_MAX		32

#define ISP2X_RAWAF_WIN_NUM		2
#define ISP2X_RAWAF_LINE_NUM		5
#define ISP2X_RAWAF_GAMMA_NUM		17
#define ISP2X_RAWAF_SUMDATA_ROW		15
#define ISP2X_RAWAF_SUMDATA_COLUMN	15
#define ISP2X_RAWAF_SUMDATA_NUM		225
#define ISP2X_AFM_MAX_WINDOWS		3

#define ISP2X_DPCC_PDAF_POINT_NUM	16

#define ISP2X_HDRMGE_L_CURVE_NUM	17
#define ISP2X_HDRMGE_E_CURVE_NUM	17

#define ISP2X_RAWNR_LUMA_RATION_NUM	8

#define ISP2X_HDRTMO_MINMAX_NUM		32

#define ISP2X_GIC_SIGMA_Y_NUM		15

#define ISP2X_CCM_CURVE_NUM		17

/* WDR */
#define ISP2X_WDR_SIZE			48

#define ISP2X_DHAZ_CONV_COEFF_NUM	6
#define ISP2X_DHAZ_HIST_IIR_NUM		64

#define ISP2X_GAMMA_OUT_MAX_SAMPLES	45

#define ISP2X_MIPI_LUMA_MEAN_MAX	16
#define ISP2X_MIPI_RAW_MAX		3
#define ISP2X_RAW0_Y_STATE		(1 << 0)
#define ISP2X_RAW1_Y_STATE		(1 << 1)
#define ISP2X_RAW2_Y_STATE		(1 << 2)

#define ISP2X_3DLUT_DATA_NUM		729

#define ISP2X_LDCH_MESH_XY_NUM		0x80000
#define ISP2X_LDCH_BUF_NUM		2

#define ISP2X_THUNDERBOOT_VIDEO_BUF_NUM	30

#define ISP2X_FBCBUF_FD_NUM		64

#define ISP2X_MESH_BUF_NUM		2

enum rkisp_isp_mode {
	/* frame input related */
	RKISP_ISP_NORMAL = BIT(0),
	RKISP_ISP_HDR2 = BIT(1),
	RKISP_ISP_HDR3 = BIT(2),
	RKISP_ISP_COMPR = BIT(3),

	/* isp function related */
	RKISP_ISP_BIGMODE = BIT(28),
};

struct rkisp_isp_info {
	enum rkisp_isp_mode mode;
	u32 act_width;
	u32 act_height;
	u8 compr_bit;
} __attribute__ ((packed));

enum isp2x_mesh_buf_stat {
	MESH_BUF_INIT = 0,
	MESH_BUF_WAIT2CHIP,
	MESH_BUF_CHIPINUSE,
};

struct rkisp_meshbuf_info {
	u64 module_id;
	u32 unite_isp_id;
	s32 buf_fd[ISP2X_MESH_BUF_NUM];
	u32 buf_size[ISP2X_MESH_BUF_NUM];
} __attribute__ ((packed));

struct rkisp_meshbuf_size {
	u64 module_id;
	u32 unite_isp_id;
	u32 meas_width;
	u32 meas_height;
} __attribute__ ((packed));

struct isp2x_mesh_head {
	enum isp2x_mesh_buf_stat stat;
	u32 data_oft;
} __attribute__ ((packed));

#define RKISP_CMSK_WIN_MAX 8
#define RKISP_CMSK_MOSAIC_MODE 0
#define RKISP_CMSK_COVER_MODE 1

/* struct rkisp_cmsk_win
 * Priacy Mask Window configture, support 8 windows, and
 * support for mainpath and selfpath output stream channel.
 *
 * mode: 0:mosaic mode, 1:cover mode
 * win_index: window index 0~7. windows overlap, priority win7 > win0.
 * cover_color_y: cover mode effective, share for stream channel when same win_index.
 * cover_color_u: cover mode effective, share for stream channel when same win_index.
 * cover_color_v: cover mode effective, share for stream channel when same win_index.
 *
 * h_offs: window horizontal offset, share for stream channel when same win_index. 2 align.
 * v_offs: window vertical offset, share for stream channel when same win_index. 2 align.
 * h_size: window horizontal size, share for stream channel when same win_index. 8 align.
 * v_size: window vertical size, share for stream channel when same win_index. 8 align.
 */
struct rkisp_cmsk_win {
	unsigned char mode;
	unsigned char win_en;

	unsigned char cover_color_y;
	unsigned char cover_color_u;
	unsigned char cover_color_v;

	unsigned short h_offs;
	unsigned short v_offs;
	unsigned short h_size;
	unsigned short v_size;
} __attribute__ ((packed));

/* struct rkisp_cmsk_cfg
 * win: priacy mask window
 * width_ro: isp full resolution, h_offs + h_size <= width_ro.
 * height_ro: isp full resolution, v_offs + v_size <= height_ro.
 */
struct rkisp_cmsk_cfg {
	struct rkisp_cmsk_win win[RKISP_CMSK_WIN_MAX];
	unsigned int width_ro;
	unsigned int height_ro;
} __attribute__ ((packed));

/* struct rkisp_stream_info
 * cur_frame_id: stream current frame id
 * input_frame_loss: isp input frame loss num
 * output_frame_loss: stream output frame loss num
 * stream_on: stream on/off
 */
struct rkisp_stream_info {
	unsigned int cur_frame_id;
	unsigned int input_frame_loss;
	unsigned int output_frame_loss;
	unsigned char stream_on;
} __attribute__ ((packed));

/* struct rkisp_mirror_flip
 * mirror: global for all output stream
 * flip: independent for all output stream
 */
struct rkisp_mirror_flip {
	unsigned char mirror;
	unsigned char flip;
} __attribute__ ((packed));

/* trigger event mode
 * T_TRY: trigger maybe with retry
 * T_TRY_YES: trigger to retry
 * T_TRY_NO: trigger no to retry
 *
 * T_START_X1: isp read one frame
 * T_START_X2: isp read hdr two frame
 * T_START_X3: isp read hdr three frame
 * T_START_C: isp read hdr linearised and compressed data
 */
enum isp2x_trigger_mode {
	T_TRY = BIT(0),
	T_TRY_YES = BIT(1),
	T_TRY_NO = BIT(2),

	T_START_X1 = BIT(4),
	T_START_X2 = BIT(5),
	T_START_X3 = BIT(6),
	T_START_C = BIT(7),
};

struct isp2x_csi_trigger {
	/* timestamp in ns */
	u64 sof_timestamp;
	u64 frame_timestamp;
	u32 frame_id;
	int times;
	enum isp2x_trigger_mode mode;
} __attribute__ ((packed));

/* isp csi dmatx/dmarx memory mode
 * 0: raw12/raw10/raw8 8bit memory compact
 * 1: raw12/raw10 16bit memory one pixel
 *    big endian for rv1126/rv1109
 *    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 *    | 3| 2| 1| 0| -| -| -| -|11|10| 9| 8| 7| 6| 5| 4|
 *    little align for rk356x
 *    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 *    | -| -| -| -|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 * 2: raw12/raw10 16bit memory one pixel
 *    big align for rv1126/rv1109/rk356x
 *    |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
 *    |11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0| -| -| -| -|
 */
enum isp_csi_memory {
	CSI_MEM_COMPACT = 0,
	CSI_MEM_WORD_BIG_END = 1,
	CSI_MEM_WORD_LITTLE_ALIGN = 1,
	CSI_MEM_WORD_BIG_ALIGN = 2,
};

#define RKISP_INFO2DDR_BUF_MAX	4
/* 32bit flag for user set to memory after buf used */
#define RKISP_INFO2DDR_BUF_INIT 0x5AA5

enum rkisp_info2ddr_owner {
	RKISP_INFO2DRR_OWNER_NULL,
	RKISP_INFO2DRR_OWNER_GAIN,
	RKISP_INFO2DRR_OWNER_AWB,
};

/* struct rkisp_info2ddr
 * awb and gain debug info write to ddr
 *
 * owner: 0: off, 1: gain, 2: awb.
 * u: gain or awb mode parameters.
 * buf_cnt: buf num to request. return actual result.
 * buf_fd: fd of memory alloc result.
 * wsize: data width to request. if useless to 0. return actual result.
 * vsize: data height to request. if useless to 0. return actual result.
 */
struct rkisp_info2ddr {
	enum rkisp_info2ddr_owner owner;

	union {
		struct {
			u8 gain2ddr_mode;
		} gain;

		struct {
			u8 awb2ddr_sel;
		} awb;
	} u;

	u8 buf_cnt;
	s32 buf_fd[RKISP_INFO2DDR_BUF_MAX];

	u32 wsize;
	u32 vsize;
} __attribute__ ((packed));

struct isp2x_ispgain_buf {
	u32 gain_dmaidx;
	u32 mfbc_dmaidx;
	u32 gain_size;
	u32 mfbc_size;
	u32 frame_id;
} __attribute__ ((packed));

struct isp2x_buf_idxfd {
	u32 buf_num;
	u32 index[ISP2X_FBCBUF_FD_NUM];
	s32 dmafd[ISP2X_FBCBUF_FD_NUM];
} __attribute__ ((packed));

struct isp2x_window {
	u16 h_offs;
	u16 v_offs;
	u16 h_size;
	u16 v_size;
} __attribute__ ((packed));

struct isp2x_bls_fixed_val {
	s16 r;
	s16 gr;
	s16 gb;
	s16 b;
} __attribute__ ((packed));

struct isp2x_bls_cfg {
	u8 enable_auto;
	u8 en_windows;
	struct isp2x_window bls_window1;
	struct isp2x_window bls_window2;
	u8 bls_samples;
	struct isp2x_bls_fixed_val fixed_val;
} __attribute__ ((packed));

struct isp2x_bls_stat {
	u16 meas_r;
	u16 meas_gr;
	u16 meas_gb;
	u16 meas_b;
} __attribute__ ((packed));

struct isp2x_dpcc_pdaf_point {
	u8 y;
	u8 x;
} __attribute__ ((packed));

struct isp2x_dpcc_cfg {
	//mode 0x0000
	u8 stage1_enable;
	u8 grayscale_mode;

	//output_mode 0x0004
	u8 sw_rk_out_sel;
	u8 sw_dpcc_output_sel;
	u8 stage1_rb_3x3;
	u8 stage1_g_3x3;
	u8 stage1_incl_rb_center;
	u8 stage1_incl_green_center;

	//set_use 0x0008
	u8 stage1_use_fix_set;
	u8 stage1_use_set_3;
	u8 stage1_use_set_2;
	u8 stage1_use_set_1;

	//methods_set_1 0x000c
	u8 sw_rk_red_blue1_en;
	u8 rg_red_blue1_enable;
	u8 rnd_red_blue1_enable;
	u8 ro_red_blue1_enable;
	u8 lc_red_blue1_enable;
	u8 pg_red_blue1_enable;
	u8 sw_rk_green1_en;
	u8 rg_green1_enable;
	u8 rnd_green1_enable;
	u8 ro_green1_enable;
	u8 lc_green1_enable;
	u8 pg_green1_enable;

	//methods_set_2 0x0010
	u8 sw_rk_red_blue2_en;
	u8 rg_red_blue2_enable;
	u8 rnd_red_blue2_enable;
	u8 ro_red_blue2_enable;
	u8 lc_red_blue2_enable;
	u8 pg_red_blue2_enable;
	u8 sw_rk_green2_en;
	u8 rg_green2_enable;
	u8 rnd_green2_enable;
	u8 ro_green2_enable;
	u8 lc_green2_enable;
	u8 pg_green2_enable;

	//methods_set_3 0x0014
	u8 sw_rk_red_blue3_en;
	u8 rg_red_blue3_enable;
	u8 rnd_red_blue3_enable;
	u8 ro_red_blue3_enable;
	u8 lc_red_blue3_enable;
	u8 pg_red_blue3_enable;
	u8 sw_rk_green3_en;
	u8 rg_green3_enable;
	u8 rnd_green3_enable;
	u8 ro_green3_enable;
	u8 lc_green3_enable;
	u8 pg_green3_enable;

	//line_thresh_1 0x0018
	u8 sw_mindis1_rb;
	u8 sw_mindis1_g;
	u8 line_thr_1_rb;
	u8 line_thr_1_g;

	//line_mad_fac_1 0x001c
	u8 sw_dis_scale_min1;
	u8 sw_dis_scale_max1;
	u8 line_mad_fac_1_rb;
	u8 line_mad_fac_1_g;

	//pg_fac_1 0x0020
	u8 pg_fac_1_rb;
	u8 pg_fac_1_g;

	//rnd_thresh_1 0x0024
	u8 rnd_thr_1_rb;
	u8 rnd_thr_1_g;

	//rg_fac_1 0x0028
	u8 rg_fac_1_rb;
	u8 rg_fac_1_g;

	//line_thresh_2 0x002c
	u8 sw_mindis2_rb;
	u8 sw_mindis2_g;
	u8 line_thr_2_rb;
	u8 line_thr_2_g;

	//line_mad_fac_2 0x0030
	u8 sw_dis_scale_min2;
	u8 sw_dis_scale_max2;
	u8 line_mad_fac_2_rb;
	u8 line_mad_fac_2_g;

	//pg_fac_2 0x0034
	u8 pg_fac_2_rb;
	u8 pg_fac_2_g;

	//rnd_thresh_2 0x0038
	u8 rnd_thr_2_rb;
	u8 rnd_thr_2_g;

	//rg_fac_2 0x003c
	u8 rg_fac_2_rb;
	u8 rg_fac_2_g;

	//line_thresh_3 0x0040
	u8 sw_mindis3_rb;
	u8 sw_mindis3_g;
	u8 line_thr_3_rb;
	u8 line_thr_3_g;

	//line_mad_fac_3 0x0044
	u8 sw_dis_scale_min3;
	u8 sw_dis_scale_max3;
	u8 line_mad_fac_3_rb;
	u8 line_mad_fac_3_g;

	//pg_fac_3 0x0048
	u8 pg_fac_3_rb;
	u8 pg_fac_3_g;

	//rnd_thresh_3 0x004c
	u8 rnd_thr_3_rb;
	u8 rnd_thr_3_g;

	//rg_fac_3 0x0050
	u8 rg_fac_3_rb;
	u8 rg_fac_3_g;

	//ro_limits 0x0054
	u8 ro_lim_3_rb;
	u8 ro_lim_3_g;
	u8 ro_lim_2_rb;
	u8 ro_lim_2_g;
	u8 ro_lim_1_rb;
	u8 ro_lim_1_g;

	//rnd_offs 0x0058
	u8 rnd_offs_3_rb;
	u8 rnd_offs_3_g;
	u8 rnd_offs_2_rb;
	u8 rnd_offs_2_g;
	u8 rnd_offs_1_rb;
	u8 rnd_offs_1_g;

	//bpt_ctrl 0x005c
	u8 bpt_rb_3x3;
	u8 bpt_g_3x3;
	u8 bpt_incl_rb_center;
	u8 bpt_incl_green_center;
	u8 bpt_use_fix_set;
	u8 bpt_use_set_3;
	u8 bpt_use_set_2;
	u8 bpt_use_set_1;
	u8 bpt_cor_en;
	u8 bpt_det_en;

	//bpt_number 0x0060
	u16 bp_number;

	//bpt_addr 0x0064
	u16 bp_table_addr;

	//bpt_data 0x0068
	u16 bpt_v_addr;
	u16 bpt_h_addr;

	//bp_cnt 0x006c
	u32 bp_cnt;

	//pdaf_en 0x0070
	u8 sw_pdaf_en;

	//pdaf_point_en 0x0074
	u8 pdaf_point_en[ISP2X_DPCC_PDAF_POINT_NUM];

	//pdaf_offset 0x0078
	u16 pdaf_offsety;
	u16 pdaf_offsetx;

	//pdaf_wrap 0x007c
	u16 pdaf_wrapy;
	u16 pdaf_wrapx;

	//pdaf_scope 0x0080
	u16 pdaf_wrapy_num;
	u16 pdaf_wrapx_num;

	//pdaf_point_0 0x0084
	struct isp2x_dpcc_pdaf_point point[ISP2X_DPCC_PDAF_POINT_NUM];

	//pdaf_forward_med 0x00a4
	u8 pdaf_forward_med;
} __attribute__ ((packed));

struct isp2x_hdrmge_curve {
	u16 curve_1[ISP2X_HDRMGE_L_CURVE_NUM];
	u16 curve_0[ISP2X_HDRMGE_L_CURVE_NUM];
} __attribute__ ((packed));

struct isp2x_hdrmge_cfg {
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

	struct isp2x_hdrmge_curve curve;
	u16 e_y[ISP2X_HDRMGE_E_CURVE_NUM];
} __attribute__ ((packed));

struct isp2x_rawnr_cfg {
	u8 gauss_en;
	u8 log_bypass;

	u16 filtpar0;
	u16 filtpar1;
	u16 filtpar2;

	u32 dgain0;
	u32 dgain1;
	u32 dgain2;

	u16 luration[ISP2X_RAWNR_LUMA_RATION_NUM];
	u16 lulevel[ISP2X_RAWNR_LUMA_RATION_NUM];

	u32 gauss;
	u16 sigma;
	u16 pix_diff;

	u32 thld_diff;

	u8 gas_weig_scl2;
	u8 gas_weig_scl1;
	u16 thld_chanelw;

	u16 lamda;

	u16 fixw0;
	u16 fixw1;
	u16 fixw2;
	u16 fixw3;

	u32 wlamda0;
	u32 wlamda1;
	u32 wlamda2;

	u16 rgain_filp;
	u16 bgain_filp;
} __attribute__ ((packed));

struct isp2x_lsc_cfg {
	u16 r_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];
	u16 gr_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];
	u16 gb_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];
	u16 b_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];

	u16 x_grad_tbl[ISP2X_LSC_GRAD_TBL_SIZE];
	u16 y_grad_tbl[ISP2X_LSC_GRAD_TBL_SIZE];

	u16 x_size_tbl[ISP2X_LSC_SIZE_TBL_SIZE];
	u16 y_size_tbl[ISP2X_LSC_SIZE_TBL_SIZE];
} __attribute__ ((packed));

enum isp2x_goc_mode {
	ISP2X_GOC_MODE_LOGARITHMIC,
	ISP2X_GOC_MODE_EQUIDISTANT
};

struct isp2x_goc_cfg {
	enum isp2x_goc_mode mode;
	u8 gamma_y[17];
} __attribute__ ((packed));

struct isp2x_hdrtmo_predict {
	u8 global_tmo;
	s32 iir_max;
	s32 global_tmo_strength;

	u8 scene_stable;
	s32 k_rolgmean;
	s32 iir;
} __attribute__ ((packed));

struct isp2x_hdrtmo_cfg {
	u16 cnt_vsize;
	u8 gain_ld_off2;
	u8 gain_ld_off1;
	u8 big_en;
	u8 nobig_en;
	u8 newhst_en;
	u8 cnt_mode;

	u16 expl_lgratio;
	u8 lgscl_ratio;
	u8 cfg_alpha;

	u16 set_gainoff;
	u16 set_palpha;

	u16 set_lgmax;
	u16 set_lgmin;

	u8 set_weightkey;
	u16 set_lgmean;

	u16 set_lgrange1;
	u16 set_lgrange0;

	u16 set_lgavgmax;

	u8 clipgap1_i;
	u8 clipgap0_i;
	u8 clipratio1;
	u8 clipratio0;
	u8 ratiol;

	u16 lgscl_inv;
	u16 lgscl;

	u16 lgmax;

	u16 hist_low;
	u16 hist_min;

	u8 hist_shift;
	u16 hist_0p3;
	u16 hist_high;

	u16 palpha_lwscl;
	u16 palpha_lw0p5;
	u16 palpha_0p18;

	u16 maxgain;
	u16 maxpalpha;

	struct isp2x_hdrtmo_predict predict;
} __attribute__ ((packed));

struct isp2x_hdrtmo_stat {
	u16 lglow;
	u16 lgmin;
	u16 lghigh;
	u16 lgmax;
	u16 weightkey;
	u16 lgmean;
	u16 lgrange1;
	u16 lgrange0;
	u16 palpha;
	u16 lgavgmax;
	u16 linecnt;
	u32 min_max[ISP2X_HDRTMO_MINMAX_NUM];
} __attribute__ ((packed));

struct isp2x_gic_cfg {
	u8 edge_open;

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
	u16 dnloscale;
	u16 dnhiscale;
	u8 reglumapointsstep;

	u16 gvaluelimitlo;
	u16 gvaluelimithi;
	u8 fusionratiohilimt1;

	u8 regstrength_fix;

	u16 sigma_y[ISP2X_GIC_SIGMA_Y_NUM];

	u8 noise_cut_en;
	u16 noise_coe_a;

	u16 noise_coe_b;
	u16 diff_clip;
} __attribute__ ((packed));

struct isp2x_debayer_cfg {
	u8 filter_c_en;
	u8 filter_g_en;

	u8 thed1;
	u8 thed0;
	u8 dist_scale;
	u8 max_ratio;
	u8 clip_en;

	s8 filter1_coe5;
	s8 filter1_coe4;
	s8 filter1_coe3;
	s8 filter1_coe2;
	s8 filter1_coe1;

	s8 filter2_coe5;
	s8 filter2_coe4;
	s8 filter2_coe3;
	s8 filter2_coe2;
	s8 filter2_coe1;

	u16 hf_offset;
	u8 gain_offset;
	u8 offset;

	u8 shift_num;
	u8 order_max;
	u8 order_min;
} __attribute__ ((packed));

struct isp2x_ccm_cfg {
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

	u16 alp_y[ISP2X_CCM_CURVE_NUM];

	u8 bound_bit;
} __attribute__ ((packed));

struct isp2x_gammaout_cfg {
	u8 equ_segm;
	u16 offset;
	u16 gamma_y[ISP2X_GAMMA_OUT_MAX_SAMPLES];
} __attribute__ ((packed));

enum isp2x_wdr_mode {
	ISP2X_WDR_MODE_BLOCK,
	ISP2X_WDR_MODE_GLOBAL
};

struct isp2x_wdr_cfg {
	enum isp2x_wdr_mode mode;
	unsigned int c_wdr[ISP2X_WDR_SIZE];
} __attribute__ ((packed));

struct isp2x_dhaz_cfg {
	u8 enhance_en;
	u8 hist_chn;
	u8 hpara_en;
	u8 hist_en;
	u8 dc_en;
	u8 big_en;
	u8 nobig_en;

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

	u8 hist_th_off;
	u8 hist_gratio;

	u16 hist_min;
	u16 hist_k;

	u16 enhance_value;
	u16 hist_scale;

	u16 iir_wt_sigma;
	u16 iir_sigma;
	u16 stab_fnum;

	u16 iir_tmax_sigma;
	u16 iir_air_sigma;

	u16 cfg_wt;
	u16 cfg_air;
	u16 cfg_alpha;

	u16 cfg_gratio;
	u16 cfg_tmax;

	u16 dc_weitcur;
	u16 dc_thed;

	u8 sw_dhaz_dc_bf_h3;
	u8 sw_dhaz_dc_bf_h2;
	u8 sw_dhaz_dc_bf_h1;
	u8 sw_dhaz_dc_bf_h0;

	u8 sw_dhaz_dc_bf_h5;
	u8 sw_dhaz_dc_bf_h4;

	u16 air_weitcur;
	u16 air_thed;

	u8 air_bf_h2;
	u8 air_bf_h1;
	u8 air_bf_h0;

	u8 gaus_h2;
	u8 gaus_h1;
	u8 gaus_h0;

	u8 conv_t0[ISP2X_DHAZ_CONV_COEFF_NUM];
	u8 conv_t1[ISP2X_DHAZ_CONV_COEFF_NUM];
	u8 conv_t2[ISP2X_DHAZ_CONV_COEFF_NUM];
} __attribute__ ((packed));

struct isp2x_dhaz_stat {
	u16 dhaz_adp_air_base;
	u16 dhaz_adp_wt;

	u16 dhaz_adp_gratio;
	u16 dhaz_adp_tmax;

	u16 h_r_iir[ISP2X_DHAZ_HIST_IIR_NUM];
	u16 h_g_iir[ISP2X_DHAZ_HIST_IIR_NUM];
	u16 h_b_iir[ISP2X_DHAZ_HIST_IIR_NUM];
} __attribute__ ((packed));

struct isp2x_cproc_cfg {
	u8 c_out_range;
	u8 y_in_range;
	u8 y_out_range;
	u8 contrast;
	u8 brightness;
	u8 sat;
	u8 hue;
} __attribute__ ((packed));

struct isp2x_ie_cfg {
	u16 effect;
	u16 color_sel;
	u16 eff_mat_1;
	u16 eff_mat_2;
	u16 eff_mat_3;
	u16 eff_mat_4;
	u16 eff_mat_5;
	u16 eff_tint;
} __attribute__ ((packed));

struct isp2x_rkiesharp_cfg {
	u8 coring_thr;
	u8 full_range;
	u8 switch_avg;
	u8 yavg_thr[4];
	u8 delta1[5];
	u8 delta2[5];
	u8 maxnumber[5];
	u8 minnumber[5];
	u8 gauss_flat_coe[9];
	u8 gauss_noise_coe[9];
	u8 gauss_other_coe[9];
	u8 line1_filter_coe[6];
	u8 line2_filter_coe[9];
	u8 line3_filter_coe[6];
	u16 grad_seq[4];
	u8 sharp_factor[5];
	u8 uv_gauss_flat_coe[15];
	u8 uv_gauss_noise_coe[15];
	u8 uv_gauss_other_coe[15];
	u8 lap_mat_coe[9];
} __attribute__ ((packed));

struct isp2x_superimp_cfg {
	u8 transparency_mode;
	u8 ref_image;

	u16 offset_x;
	u16 offset_y;

	u8 y_comp;
	u8 cb_comp;
	u8 cr_comp;
} __attribute__ ((packed));

struct isp2x_gamma_corr_curve {
	u16 gamma_y[ISP2X_DEGAMMA_CURVE_SIZE];
} __attribute__ ((packed));

struct isp2x_gamma_curve_x_axis_pnts {
	u32 gamma_dx0;
	u32 gamma_dx1;
} __attribute__ ((packed));

struct isp2x_sdg_cfg {
	struct isp2x_gamma_corr_curve curve_r;
	struct isp2x_gamma_corr_curve curve_g;
	struct isp2x_gamma_corr_curve curve_b;
	struct isp2x_gamma_curve_x_axis_pnts xa_pnts;
} __attribute__ ((packed));

struct isp2x_bdm_config {
	unsigned char demosaic_th;
} __attribute__ ((packed));

struct isp2x_gain_cfg {
	u8 dhaz_en;
	u8 wdr_en;
	u8 tmo_en;
	u8 lsc_en;
	u8 mge_en;

	u32 mge_gain[ISP2X_GAIN_HDRMGE_GAIN_NUM];
	u16 idx[ISP2X_GAIN_IDX_NUM];
	u16 lut[ISP2X_GAIN_LUT_NUM];
} __attribute__ ((packed));

struct isp2x_3dlut_cfg {
	u8 bypass_en;
	u32 actual_size;	// word unit
	u16 lut_r[ISP2X_3DLUT_DATA_NUM];
	u16 lut_g[ISP2X_3DLUT_DATA_NUM];
	u16 lut_b[ISP2X_3DLUT_DATA_NUM];
} __attribute__ ((packed));

enum isp2x_ldch_buf_stat {
	LDCH_BUF_INIT = 0,
	LDCH_BUF_WAIT2CHIP,
	LDCH_BUF_CHIPINUSE,
};

struct rkisp_ldchbuf_info {
	s32 buf_fd[ISP2X_LDCH_BUF_NUM];
	u32 buf_size[ISP2X_LDCH_BUF_NUM];
} __attribute__ ((packed));

struct rkisp_ldchbuf_size {
	u32 meas_width;
	u32 meas_height;
} __attribute__ ((packed));

struct isp2x_ldch_head {
	enum isp2x_ldch_buf_stat stat;
	u32 data_oft;
} __attribute__ ((packed));

struct isp2x_ldch_cfg {
	u32 hsize;
	u32 vsize;
	s32 buf_fd;
} __attribute__ ((packed));

struct isp2x_awb_gain_cfg {
	u16 gain_red;
	u16 gain_green_r;
	u16 gain_blue;
	u16 gain_green_b;
} __attribute__ ((packed));

struct isp2x_siawb_meas_cfg {
	struct isp2x_window awb_wnd;
	u8 awb_mode;
	u8 max_y;
	u8 min_y;
	u8 max_csum;
	u8 min_c;
	u8 frames;
	u8 awb_ref_cr;
	u8 awb_ref_cb;
	u8 enable_ymax_cmp;
} __attribute__ ((packed));

struct isp2x_rawawb_meas_cfg {
	u8 rawawb_sel;
	u8 sw_rawawb_light_num;			//CTRL
	u8 sw_rawawb_wind_size;			//CTRL
	u8 sw_rawawb_c_range;			//CTRL
	u8 sw_rawawb_y_range;			//CTRL
	u8 sw_rawawb_3dyuv_ls_idx3;		//CTRL
	u8 sw_rawawb_3dyuv_ls_idx2;		//CTRL
	u8 sw_rawawb_3dyuv_ls_idx1;		//CTRL
	u8 sw_rawawb_3dyuv_ls_idx0;		//CTRL
	u8 sw_rawawb_xy_en;			//CTRL
	u8 sw_rawawb_uv_en;			//CTRL
	u8 sw_rawlsc_bypass_en;			//CTRL
	u8 sw_rawawb_blk_measure_mode;		//BLK_CTRL
	u8 sw_rawawb_store_wp_flag_ls_idx2;	//BLK_CTRL
	u8 sw_rawawb_store_wp_flag_ls_idx1;	//BLK_CTRL
	u8 sw_rawawb_store_wp_flag_ls_idx0;	//BLK_CTRL
	u16 sw_rawawb_store_wp_th0;		//BLK_CTRL
	u16 sw_rawawb_store_wp_th1;		//BLK_CTRL
	u16 sw_rawawb_store_wp_th2;		//RAW_CTRL
	u16 sw_rawawb_v_offs;			//WIN_OFFS
	u16 sw_rawawb_h_offs;			//WIN_OFFS
	u16 sw_rawawb_v_size;			//WIN_SIZE
	u16 sw_rawawb_h_size;			//WIN_SIZE
	u16 sw_rawawb_g_max;			//LIMIT_RG_MAX
	u16 sw_rawawb_r_max;			//LIMIT_RG_MAX
	u16 sw_rawawb_y_max;			//LIMIT_BY_MAX
	u16 sw_rawawb_b_max;			//LIMIT_BY_MAX
	u16 sw_rawawb_g_min;			//LIMIT_RG_MIN
	u16 sw_rawawb_r_min;			//LIMIT_RG_MIN
	u16 sw_rawawb_y_min;			//LIMIT_BY_MIN
	u16 sw_rawawb_b_min;			//LIMIT_BY_MIN
	u16 sw_rawawb_coeff_y_g;		//RGB2Y_0
	u16 sw_rawawb_coeff_y_r;		//RGB2Y_0
	u16 sw_rawawb_coeff_y_b;		//RGB2Y_1
	u16 sw_rawawb_coeff_u_g;		//RGB2U_0
	u16 sw_rawawb_coeff_u_r;		//RGB2U_0
	u16 sw_rawawb_coeff_u_b;		//RGB2U_1
	u16 sw_rawawb_coeff_v_g;		//RGB2V_0
	u16 sw_rawawb_coeff_v_r;		//RGB2V_0
	u16 sw_rawawb_coeff_v_b;		//RGB2V_1
	u16 sw_rawawb_vertex0_v_0;		//UV_DETC_VERTEX0_0
	u16 sw_rawawb_vertex0_u_0;		//UV_DETC_VERTEX0_0
	u16 sw_rawawb_vertex1_v_0;		//UV_DETC_VERTEX1_0
	u16 sw_rawawb_vertex1_u_0;		//UV_DETC_VERTEX1_0
	u16 sw_rawawb_vertex2_v_0;		//UV_DETC_VERTEX2_0
	u16 sw_rawawb_vertex2_u_0;		//UV_DETC_VERTEX2_0
	u16 sw_rawawb_vertex3_v_0;		//UV_DETC_VERTEX3_0
	u16 sw_rawawb_vertex3_u_0;		//UV_DETC_VERTEX3_0
	u32 sw_rawawb_islope01_0;		//UV_DETC_ISLOPE01_0
	u32 sw_rawawb_islope12_0;		//UV_DETC_ISLOPE12_0
	u32 sw_rawawb_islope23_0;		//UV_DETC_ISLOPE23_0
	u32 sw_rawawb_islope30_0;		//UV_DETC_ISLOPE30_0
	u16 sw_rawawb_vertex0_v_1;		//UV_DETC_VERTEX0_1
	u16 sw_rawawb_vertex0_u_1;		//UV_DETC_VERTEX0_1
	u16 sw_rawawb_vertex1_v_1;		//UV_DETC_VERTEX1_1
	u16 sw_rawawb_vertex1_u_1;		//UV_DETC_VERTEX1_1
	u16 sw_rawawb_vertex2_v_1;		//UV_DETC_VERTEX2_1
	u16 sw_rawawb_vertex2_u_1;		//UV_DETC_VERTEX2_1
	u16 sw_rawawb_vertex3_v_1;		//UV_DETC_VERTEX3_1
	u16 sw_rawawb_vertex3_u_1;		//UV_DETC_VERTEX3_1
	u32 sw_rawawb_islope01_1;		//UV_DETC_ISLOPE01_1
	u32 sw_rawawb_islope12_1;		//UV_DETC_ISLOPE12_1
	u32 sw_rawawb_islope23_1;		//UV_DETC_ISLOPE23_1
	u32 sw_rawawb_islope30_1;		//UV_DETC_ISLOPE30_1
	u16 sw_rawawb_vertex0_v_2;		//UV_DETC_VERTEX0_2
	u16 sw_rawawb_vertex0_u_2;		//UV_DETC_VERTEX0_2
	u16 sw_rawawb_vertex1_v_2;		//UV_DETC_VERTEX1_2
	u16 sw_rawawb_vertex1_u_2;		//UV_DETC_VERTEX1_2
	u16 sw_rawawb_vertex2_v_2;		//UV_DETC_VERTEX2_2
	u16 sw_rawawb_vertex2_u_2;		//UV_DETC_VERTEX2_2
	u16 sw_rawawb_vertex3_v_2;		//UV_DETC_VERTEX3_2
	u16 sw_rawawb_vertex3_u_2;		//UV_DETC_VERTEX3_2
	u32 sw_rawawb_islope01_2;		//UV_DETC_ISLOPE01_2
	u32 sw_rawawb_islope12_2;		//UV_DETC_ISLOPE12_2
	u32 sw_rawawb_islope23_2;		//UV_DETC_ISLOPE23_2
	u32 sw_rawawb_islope30_2;		//UV_DETC_ISLOPE30_2
	u16 sw_rawawb_vertex0_v_3;		//UV_DETC_VERTEX0_3
	u16 sw_rawawb_vertex0_u_3;		//UV_DETC_VERTEX0_3
	u16 sw_rawawb_vertex1_v_3;		//UV_DETC_VERTEX1_3
	u16 sw_rawawb_vertex1_u_3;		//UV_DETC_VERTEX1_3
	u16 sw_rawawb_vertex2_v_3;		//UV_DETC_VERTEX2_3
	u16 sw_rawawb_vertex2_u_3;		//UV_DETC_VERTEX2_3
	u16 sw_rawawb_vertex3_v_3;		//UV_DETC_VERTEX3_3
	u16 sw_rawawb_vertex3_u_3;		//UV_DETC_VERTEX3_3
	u32 sw_rawawb_islope01_3;		//UV_DETC_ISLOPE01_3
	u32 sw_rawawb_islope12_3;		//UV_DETC_ISLOPE12_3
	u32 sw_rawawb_islope23_3;		//UV_DETC_ISLOPE23_3
	u32 sw_rawawb_islope30_3;		//UV_DETC_ISLOPE30_3
	u16 sw_rawawb_vertex0_v_4;		//UV_DETC_VERTEX0_4
	u16 sw_rawawb_vertex0_u_4;		//UV_DETC_VERTEX0_4
	u16 sw_rawawb_vertex1_v_4;		//UV_DETC_VERTEX1_4
	u16 sw_rawawb_vertex1_u_4;		//UV_DETC_VERTEX1_4
	u16 sw_rawawb_vertex2_v_4;		//UV_DETC_VERTEX2_4
	u16 sw_rawawb_vertex2_u_4;		//UV_DETC_VERTEX2_4
	u16 sw_rawawb_vertex3_v_4;		//UV_DETC_VERTEX3_4
	u16 sw_rawawb_vertex3_u_4;		//UV_DETC_VERTEX3_4
	u32 sw_rawawb_islope01_4;		//UV_DETC_ISLOPE01_4
	u32 sw_rawawb_islope12_4;		//UV_DETC_ISLOPE12_4
	u32 sw_rawawb_islope23_4;		//UV_DETC_ISLOPE23_4
	u32 sw_rawawb_islope30_4;		//UV_DETC_ISLOPE30_4
	u16 sw_rawawb_vertex0_v_5;		//UV_DETC_VERTEX0_5
	u16 sw_rawawb_vertex0_u_5;		//UV_DETC_VERTEX0_5
	u16 sw_rawawb_vertex1_v_5;		//UV_DETC_VERTEX1_5
	u16 sw_rawawb_vertex1_u_5;		//UV_DETC_VERTEX1_5
	u16 sw_rawawb_vertex2_v_5;		//UV_DETC_VERTEX2_5
	u16 sw_rawawb_vertex2_u_5;		//UV_DETC_VERTEX2_5
	u16 sw_rawawb_vertex3_v_5;		//UV_DETC_VERTEX3_5
	u16 sw_rawawb_vertex3_u_5;		//UV_DETC_VERTEX3_5
	u32 sw_rawawb_islope01_5;		//UV_DETC_ISLOPE01_5
	u32 sw_rawawb_islope12_5;		//UV_DETC_ISLOPE10_5
	u32 sw_rawawb_islope23_5;		//UV_DETC_ISLOPE23_5
	u32 sw_rawawb_islope30_5;		//UV_DETC_ISLOPE30_5
	u16 sw_rawawb_vertex0_v_6;		//UV_DETC_VERTEX0_6
	u16 sw_rawawb_vertex0_u_6;		//UV_DETC_VERTEX0_6
	u16 sw_rawawb_vertex1_v_6;		//UV_DETC_VERTEX1_6
	u16 sw_rawawb_vertex1_u_6;		//UV_DETC_VERTEX1_6
	u16 sw_rawawb_vertex2_v_6;		//UV_DETC_VERTEX2_6
	u16 sw_rawawb_vertex2_u_6;		//UV_DETC_VERTEX2_6
	u16 sw_rawawb_vertex3_v_6;		//UV_DETC_VERTEX3_6
	u16 sw_rawawb_vertex3_u_6;		//UV_DETC_VERTEX3_6
	u32 sw_rawawb_islope01_6;		//UV_DETC_ISLOPE01_6
	u32 sw_rawawb_islope12_6;		//UV_DETC_ISLOPE10_6
	u32 sw_rawawb_islope23_6;		//UV_DETC_ISLOPE23_6
	u32 sw_rawawb_islope30_6;		//UV_DETC_ISLOPE30_6
	u32 sw_rawawb_b_uv_0;			//YUV_DETC_B_UV_0
	u32 sw_rawawb_slope_vtcuv_0;		//YUV_DETC_SLOPE_VTCUV_0
	u32 sw_rawawb_inv_dslope_0;		//YUV_DETC_INV_DSLOPE_0
	u32 sw_rawawb_slope_ydis_0;		//YUV_DETC_SLOPE_YDIS_0
	u32 sw_rawawb_b_ydis_0;			//YUV_DETC_B_YDIS_0
	u32 sw_rawawb_b_uv_1;			//YUV_DETC_B_UV_1
	u32 sw_rawawb_slope_vtcuv_1;		//YUV_DETC_SLOPE_VTCUV_1
	u32 sw_rawawb_inv_dslope_1;		//YUV_DETC_INV_DSLOPE_1
	u32 sw_rawawb_slope_ydis_1;		//YUV_DETC_SLOPE_YDIS_1
	u32 sw_rawawb_b_ydis_1;			//YUV_DETC_B_YDIS_1
	u32 sw_rawawb_b_uv_2;			//YUV_DETC_B_UV_2
	u32 sw_rawawb_slope_vtcuv_2;		//YUV_DETC_SLOPE_VTCUV_2
	u32 sw_rawawb_inv_dslope_2;		//YUV_DETC_INV_DSLOPE_2
	u32 sw_rawawb_slope_ydis_2;		//YUV_DETC_SLOPE_YDIS_2
	u32 sw_rawawb_b_ydis_2;			//YUV_DETC_B_YDIS_2
	u32 sw_rawawb_b_uv_3;			//YUV_DETC_B_UV_3
	u32 sw_rawawb_slope_vtcuv_3;		//YUV_DETC_SLOPE_VTCUV_3
	u32 sw_rawawb_inv_dslope_3;		//YUV_DETC_INV_DSLOPE_3
	u32 sw_rawawb_slope_ydis_3;		//YUV_DETC_SLOPE_YDIS_3
	u32 sw_rawawb_b_ydis_3;			//YUV_DETC_B_YDIS_3
	u32 sw_rawawb_ref_u;			//YUV_DETC_REF_U
	u8 sw_rawawb_ref_v_3;			//YUV_DETC_REF_V_1
	u8 sw_rawawb_ref_v_2;			//YUV_DETC_REF_V_1
	u8 sw_rawawb_ref_v_1;			//YUV_DETC_REF_V_1
	u8 sw_rawawb_ref_v_0;			//YUV_DETC_REF_V_1
	u16 sw_rawawb_dis1_0;			//YUV_DETC_DIS01_0
	u16 sw_rawawb_dis0_0;			//YUV_DETC_DIS01_0
	u16 sw_rawawb_dis3_0;			//YUV_DETC_DIS23_0
	u16 sw_rawawb_dis2_0;			//YUV_DETC_DIS23_0
	u16 sw_rawawb_dis5_0;			//YUV_DETC_DIS45_0
	u16 sw_rawawb_dis4_0;			//YUV_DETC_DIS45_0
	u8 sw_rawawb_th3_0;			//YUV_DETC_TH03_0
	u8 sw_rawawb_th2_0;			//YUV_DETC_TH03_0
	u8 sw_rawawb_th1_0;			//YUV_DETC_TH03_0
	u8 sw_rawawb_th0_0;			//YUV_DETC_TH03_0
	u8 sw_rawawb_th5_0;			//YUV_DETC_TH45_0
	u8 sw_rawawb_th4_0;			//YUV_DETC_TH45_0
	u16 sw_rawawb_dis1_1;			//YUV_DETC_DIS01_1
	u16 sw_rawawb_dis0_1;			//YUV_DETC_DIS01_1
	u16 sw_rawawb_dis3_1;			//YUV_DETC_DIS23_1
	u16 sw_rawawb_dis2_1;			//YUV_DETC_DIS23_1
	u16 sw_rawawb_dis5_1;			//YUV_DETC_DIS45_1
	u16 sw_rawawb_dis4_1;			//YUV_DETC_DIS45_1
	u8 sw_rawawb_th3_1;			//YUV_DETC_TH03_1
	u8 sw_rawawb_th2_1;			//YUV_DETC_TH03_1
	u8 sw_rawawb_th1_1;			//YUV_DETC_TH03_1
	u8 sw_rawawb_th0_1;			//YUV_DETC_TH03_1
	u8 sw_rawawb_th5_1;			//YUV_DETC_TH45_1
	u8 sw_rawawb_th4_1;			//YUV_DETC_TH45_1
	u16 sw_rawawb_dis1_2;			//YUV_DETC_DIS01_2
	u16 sw_rawawb_dis0_2;			//YUV_DETC_DIS01_2
	u16 sw_rawawb_dis3_2;			//YUV_DETC_DIS23_2
	u16 sw_rawawb_dis2_2;			//YUV_DETC_DIS23_2
	u16 sw_rawawb_dis5_2;			//YUV_DETC_DIS45_2
	u16 sw_rawawb_dis4_2;			//YUV_DETC_DIS45_2
	u8 sw_rawawb_th3_2;			//YUV_DETC_TH03_2
	u8 sw_rawawb_th2_2;			//YUV_DETC_TH03_2
	u8 sw_rawawb_th1_2;			//YUV_DETC_TH03_2
	u8 sw_rawawb_th0_2;			//YUV_DETC_TH03_2
	u8 sw_rawawb_th5_2;			//YUV_DETC_TH45_2
	u8 sw_rawawb_th4_2;			//YUV_DETC_TH45_2
	u16 sw_rawawb_dis1_3;			//YUV_DETC_DIS01_3
	u16 sw_rawawb_dis0_3;			//YUV_DETC_DIS01_3
	u16 sw_rawawb_dis3_3;			//YUV_DETC_DIS23_3
	u16 sw_rawawb_dis2_3;			//YUV_DETC_DIS23_3
	u16 sw_rawawb_dis5_3;			//YUV_DETC_DIS45_3
	u16 sw_rawawb_dis4_3;			//YUV_DETC_DIS45_3
	u8 sw_rawawb_th3_3;			//YUV_DETC_TH03_3
	u8 sw_rawawb_th2_3;			//YUV_DETC_TH03_3
	u8 sw_rawawb_th1_3;			//YUV_DETC_TH03_3
	u8 sw_rawawb_th0_3;			//YUV_DETC_TH03_3
	u8 sw_rawawb_th5_3;			//YUV_DETC_TH45_3
	u8 sw_rawawb_th4_3;			//YUV_DETC_TH45_3
	u16 sw_rawawb_wt1;			//RGB2XY_WT01
	u16 sw_rawawb_wt0;			//RGB2XY_WT01
	u16 sw_rawawb_wt2;			//RGB2XY_WT2
	u16 sw_rawawb_mat0_y;			//RGB2XY_MAT0_XY
	u16 sw_rawawb_mat0_x;			//RGB2XY_MAT0_XY
	u16 sw_rawawb_mat1_y;			//RGB2XY_MAT1_XY
	u16 sw_rawawb_mat1_x;			//RGB2XY_MAT1_XY
	u16 sw_rawawb_mat2_y;			//RGB2XY_MAT2_XY
	u16 sw_rawawb_mat2_x;			//RGB2XY_MAT2_XY
	u16 sw_rawawb_nor_x1_0;			//XY_DETC_NOR_X_0
	u16 sw_rawawb_nor_x0_0;			//XY_DETC_NOR_X_0
	u16 sw_rawawb_nor_y1_0;			//XY_DETC_NOR_Y_0
	u16 sw_rawawb_nor_y0_0;			//XY_DETC_NOR_Y_0
	u16 sw_rawawb_big_x1_0;			//XY_DETC_BIG_X_0
	u16 sw_rawawb_big_x0_0;			//XY_DETC_BIG_X_0
	u16 sw_rawawb_big_y1_0;			//XY_DETC_BIG_Y_0
	u16 sw_rawawb_big_y0_0;			//XY_DETC_BIG_Y_0
	u16 sw_rawawb_sma_x1_0;			//XY_DETC_SMA_X_0
	u16 sw_rawawb_sma_x0_0;			//XY_DETC_SMA_X_0
	u16 sw_rawawb_sma_y1_0;			//XY_DETC_SMA_Y_0
	u16 sw_rawawb_sma_y0_0;			//XY_DETC_SMA_Y_0
	u16 sw_rawawb_nor_x1_1;			//XY_DETC_NOR_X_1
	u16 sw_rawawb_nor_x0_1;			//XY_DETC_NOR_X_1
	u16 sw_rawawb_nor_y1_1;			//XY_DETC_NOR_Y_1
	u16 sw_rawawb_nor_y0_1;			//XY_DETC_NOR_Y_1
	u16 sw_rawawb_big_x1_1;			//XY_DETC_BIG_X_1
	u16 sw_rawawb_big_x0_1;			//XY_DETC_BIG_X_1
	u16 sw_rawawb_big_y1_1;			//XY_DETC_BIG_Y_1
	u16 sw_rawawb_big_y0_1;			//XY_DETC_BIG_Y_1
	u16 sw_rawawb_sma_x1_1;			//XY_DETC_SMA_X_1
	u16 sw_rawawb_sma_x0_1;			//XY_DETC_SMA_X_1
	u16 sw_rawawb_sma_y1_1;			//XY_DETC_SMA_Y_1
	u16 sw_rawawb_sma_y0_1;			//XY_DETC_SMA_Y_1
	u16 sw_rawawb_nor_x1_2;			//XY_DETC_NOR_X_2
	u16 sw_rawawb_nor_x0_2;			//XY_DETC_NOR_X_2
	u16 sw_rawawb_nor_y1_2;			//XY_DETC_NOR_Y_2
	u16 sw_rawawb_nor_y0_2;			//XY_DETC_NOR_Y_2
	u16 sw_rawawb_big_x1_2;			//XY_DETC_BIG_X_2
	u16 sw_rawawb_big_x0_2;			//XY_DETC_BIG_X_2
	u16 sw_rawawb_big_y1_2;			//XY_DETC_BIG_Y_2
	u16 sw_rawawb_big_y0_2;			//XY_DETC_BIG_Y_2
	u16 sw_rawawb_sma_x1_2;			//XY_DETC_SMA_X_2
	u16 sw_rawawb_sma_x0_2;			//XY_DETC_SMA_X_2
	u16 sw_rawawb_sma_y1_2;			//XY_DETC_SMA_Y_2
	u16 sw_rawawb_sma_y0_2;			//XY_DETC_SMA_Y_2
	u16 sw_rawawb_nor_x1_3;			//XY_DETC_NOR_X_3
	u16 sw_rawawb_nor_x0_3;			//XY_DETC_NOR_X_3
	u16 sw_rawawb_nor_y1_3;			//XY_DETC_NOR_Y_3
	u16 sw_rawawb_nor_y0_3;			//XY_DETC_NOR_Y_3
	u16 sw_rawawb_big_x1_3;			//XY_DETC_BIG_X_3
	u16 sw_rawawb_big_x0_3;			//XY_DETC_BIG_X_3
	u16 sw_rawawb_big_y1_3;			//XY_DETC_BIG_Y_3
	u16 sw_rawawb_big_y0_3;			//XY_DETC_BIG_Y_3
	u16 sw_rawawb_sma_x1_3;			//XY_DETC_SMA_X_3
	u16 sw_rawawb_sma_x0_3;			//XY_DETC_SMA_X_3
	u16 sw_rawawb_sma_y1_3;			//XY_DETC_SMA_Y_3
	u16 sw_rawawb_sma_y0_3;			//XY_DETC_SMA_Y_3
	u16 sw_rawawb_nor_x1_4;			//XY_DETC_NOR_X_4
	u16 sw_rawawb_nor_x0_4;			//XY_DETC_NOR_X_4
	u16 sw_rawawb_nor_y1_4;			//XY_DETC_NOR_Y_4
	u16 sw_rawawb_nor_y0_4;			//XY_DETC_NOR_Y_4
	u16 sw_rawawb_big_x1_4;			//XY_DETC_BIG_X_4
	u16 sw_rawawb_big_x0_4;			//XY_DETC_BIG_X_4
	u16 sw_rawawb_big_y1_4;			//XY_DETC_BIG_Y_4
	u16 sw_rawawb_big_y0_4;			//XY_DETC_BIG_Y_4
	u16 sw_rawawb_sma_x1_4;			//XY_DETC_SMA_X_4
	u16 sw_rawawb_sma_x0_4;			//XY_DETC_SMA_X_4
	u16 sw_rawawb_sma_y1_4;			//XY_DETC_SMA_Y_4
	u16 sw_rawawb_sma_y0_4;			//XY_DETC_SMA_Y_4
	u16 sw_rawawb_nor_x1_5;			//XY_DETC_NOR_X_5
	u16 sw_rawawb_nor_x0_5;			//XY_DETC_NOR_X_5
	u16 sw_rawawb_nor_y1_5;			//XY_DETC_NOR_Y_5
	u16 sw_rawawb_nor_y0_5;			//XY_DETC_NOR_Y_5
	u16 sw_rawawb_big_x1_5;			//XY_DETC_BIG_X_5
	u16 sw_rawawb_big_x0_5;			//XY_DETC_BIG_X_5
	u16 sw_rawawb_big_y1_5;			//XY_DETC_BIG_Y_5
	u16 sw_rawawb_big_y0_5;			//XY_DETC_BIG_Y_5
	u16 sw_rawawb_sma_x1_5;			//XY_DETC_SMA_X_5
	u16 sw_rawawb_sma_x0_5;			//XY_DETC_SMA_X_5
	u16 sw_rawawb_sma_y1_5;			//XY_DETC_SMA_Y_5
	u16 sw_rawawb_sma_y0_5;			//XY_DETC_SMA_Y_5
	u16 sw_rawawb_nor_x1_6;			//XY_DETC_NOR_X_6
	u16 sw_rawawb_nor_x0_6;			//XY_DETC_NOR_X_6
	u16 sw_rawawb_nor_y1_6;			//XY_DETC_NOR_Y_6
	u16 sw_rawawb_nor_y0_6;			//XY_DETC_NOR_Y_6
	u16 sw_rawawb_big_x1_6;			//XY_DETC_BIG_X_6
	u16 sw_rawawb_big_x0_6;			//XY_DETC_BIG_X_6
	u16 sw_rawawb_big_y1_6;			//XY_DETC_BIG_Y_6
	u16 sw_rawawb_big_y0_6;			//XY_DETC_BIG_Y_6
	u16 sw_rawawb_sma_x1_6;			//XY_DETC_SMA_X_6
	u16 sw_rawawb_sma_x0_6;			//XY_DETC_SMA_X_6
	u16 sw_rawawb_sma_y1_6;			//XY_DETC_SMA_Y_6
	u16 sw_rawawb_sma_y0_6;			//XY_DETC_SMA_Y_6
	u8 sw_rawawb_multiwindow_en;		//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region6_domain;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region6_measen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region6_excen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region5_domain;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region5_measen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region5_excen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region4_domain;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region4_measen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region4_excen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region3_domain;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region3_measen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region3_excen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region2_domain;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region2_measen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region2_excen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region1_domain;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region1_measen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region1_excen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region0_domain;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region0_measen;	//MULTIWINDOW_EXC_CTRL
	u8 sw_rawawb_exc_wp_region0_excen;	//MULTIWINDOW_EXC_CTRL
	u16 sw_rawawb_multiwindow0_v_offs;	//MULTIWINDOW0_OFFS
	u16 sw_rawawb_multiwindow0_h_offs;	//MULTIWINDOW0_OFFS
	u16 sw_rawawb_multiwindow0_v_size;	//MULTIWINDOW0_SIZE
	u16 sw_rawawb_multiwindow0_h_size;	//MULTIWINDOW0_SIZE
	u16 sw_rawawb_multiwindow1_v_offs;	//MULTIWINDOW1_OFFS
	u16 sw_rawawb_multiwindow1_h_offs;	//MULTIWINDOW1_OFFS
	u16 sw_rawawb_multiwindow1_v_size;	//MULTIWINDOW1_SIZE
	u16 sw_rawawb_multiwindow1_h_size;	//MULTIWINDOW1_SIZE
	u16 sw_rawawb_multiwindow2_v_offs;	//MULTIWINDOW2_OFFS
	u16 sw_rawawb_multiwindow2_h_offs;	//MULTIWINDOW2_OFFS
	u16 sw_rawawb_multiwindow2_v_size;	//MULTIWINDOW2_SIZE
	u16 sw_rawawb_multiwindow2_h_size;	//MULTIWINDOW2_SIZE
	u16 sw_rawawb_multiwindow3_v_offs;	//MULTIWINDOW3_OFFS
	u16 sw_rawawb_multiwindow3_h_offs;	//MULTIWINDOW3_OFFS
	u16 sw_rawawb_multiwindow3_v_size;	//MULTIWINDOW3_SIZE
	u16 sw_rawawb_multiwindow3_h_size;	//MULTIWINDOW3_SIZE
	u16 sw_rawawb_multiwindow4_v_offs;	//MULTIWINDOW4_OFFS
	u16 sw_rawawb_multiwindow4_h_offs;	//MULTIWINDOW4_OFFS
	u16 sw_rawawb_multiwindow4_v_size;	//MULTIWINDOW4_SIZE
	u16 sw_rawawb_multiwindow4_h_size;	//MULTIWINDOW4_SIZE
	u16 sw_rawawb_multiwindow5_v_offs;	//MULTIWINDOW5_OFFS
	u16 sw_rawawb_multiwindow5_h_offs;	//MULTIWINDOW5_OFFS
	u16 sw_rawawb_multiwindow5_v_size;	//MULTIWINDOW5_SIZE
	u16 sw_rawawb_multiwindow5_h_size;	//MULTIWINDOW5_SIZE
	u16 sw_rawawb_multiwindow6_v_offs;	//MULTIWINDOW6_OFFS
	u16 sw_rawawb_multiwindow6_h_offs;	//MULTIWINDOW6_OFFS
	u16 sw_rawawb_multiwindow6_v_size;	//MULTIWINDOW6_SIZE
	u16 sw_rawawb_multiwindow6_h_size;	//MULTIWINDOW6_SIZE
	u16 sw_rawawb_multiwindow7_v_offs;	//MULTIWINDOW7_OFFS
	u16 sw_rawawb_multiwindow7_h_offs;	//MULTIWINDOW7_OFFS
	u16 sw_rawawb_multiwindow7_v_size;	//MULTIWINDOW7_SIZE
	u16 sw_rawawb_multiwindow7_h_size;	//MULTIWINDOW7_SIZE
	u16 sw_rawawb_exc_wp_region0_xu1;	//EXC_WP_REGION0_XU
	u16 sw_rawawb_exc_wp_region0_xu0;	//EXC_WP_REGION0_XU
	u16 sw_rawawb_exc_wp_region0_yv1;	//EXC_WP_REGION0_YV
	u16 sw_rawawb_exc_wp_region0_yv0;	//EXC_WP_REGION0_YV
	u16 sw_rawawb_exc_wp_region1_xu1;	//EXC_WP_REGION1_XU
	u16 sw_rawawb_exc_wp_region1_xu0;	//EXC_WP_REGION1_XU
	u16 sw_rawawb_exc_wp_region1_yv1;	//EXC_WP_REGION1_YV
	u16 sw_rawawb_exc_wp_region1_yv0;	//EXC_WP_REGION1_YV
	u16 sw_rawawb_exc_wp_region2_xu1;	//EXC_WP_REGION2_XU
	u16 sw_rawawb_exc_wp_region2_xu0;	//EXC_WP_REGION2_XU
	u16 sw_rawawb_exc_wp_region2_yv1;	//EXC_WP_REGION2_YV
	u16 sw_rawawb_exc_wp_region2_yv0;	//EXC_WP_REGION2_YV
	u16 sw_rawawb_exc_wp_region3_xu1;	//EXC_WP_REGION3_XU
	u16 sw_rawawb_exc_wp_region3_xu0;	//EXC_WP_REGION3_XU
	u16 sw_rawawb_exc_wp_region3_yv1;	//EXC_WP_REGION3_YV
	u16 sw_rawawb_exc_wp_region3_yv0;	//EXC_WP_REGION3_YV
	u16 sw_rawawb_exc_wp_region4_xu1;	//EXC_WP_REGION4_XU
	u16 sw_rawawb_exc_wp_region4_xu0;	//EXC_WP_REGION4_XU
	u16 sw_rawawb_exc_wp_region4_yv1;	//EXC_WP_REGION4_YV
	u16 sw_rawawb_exc_wp_region4_yv0;	//EXC_WP_REGION4_YV
	u16 sw_rawawb_exc_wp_region5_xu1;	//EXC_WP_REGION5_XU
	u16 sw_rawawb_exc_wp_region5_xu0;	//EXC_WP_REGION5_XU
	u16 sw_rawawb_exc_wp_region5_yv1;	//EXC_WP_REGION5_YV
	u16 sw_rawawb_exc_wp_region5_yv0;	//EXC_WP_REGION5_YV
	u16 sw_rawawb_exc_wp_region6_xu1;	//EXC_WP_REGION6_XU
	u16 sw_rawawb_exc_wp_region6_xu0;	//EXC_WP_REGION6_XU
	u16 sw_rawawb_exc_wp_region6_yv1;	//EXC_WP_REGION6_YV
	u16 sw_rawawb_exc_wp_region6_yv0;	//EXC_WP_REGION6_YV
} __attribute__ ((packed));

struct isp2x_rawaebig_meas_cfg {
	u8 rawae_sel;
	u8 wnd_num;
	u8 subwin_en[ISP2X_RAWAEBIG_SUBWIN_NUM];
	struct isp2x_window win;
	struct isp2x_window subwin[ISP2X_RAWAEBIG_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaelite_meas_cfg {
	u8 rawae_sel;
	u8 wnd_num;
	struct isp2x_window win;
} __attribute__ ((packed));

struct isp2x_yuvae_meas_cfg {
	u8 ysel;
	u8 wnd_num;
	u8 subwin_en[ISP2X_YUVAE_SUBWIN_NUM];
	struct isp2x_window win;
	struct isp2x_window subwin[ISP2X_YUVAE_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaf_meas_cfg {
	u8 rawaf_sel;
	u8 num_afm_win;
	u8 gaus_en;
	u8 gamma_en;
	struct isp2x_window win[ISP2X_RAWAF_WIN_NUM];
	u8 line_en[ISP2X_RAWAF_LINE_NUM];
	u8 line_num[ISP2X_RAWAF_LINE_NUM];
	u8 gaus_coe_h2;
	u8 gaus_coe_h1;
	u8 gaus_coe_h0;
	u16 afm_thres;
	u8 lum_var_shift[ISP2X_RAWAF_WIN_NUM];
	u8 afm_var_shift[ISP2X_RAWAF_WIN_NUM];
	u16 gamma_y[ISP2X_RAWAF_GAMMA_NUM];
} __attribute__ ((packed));

struct isp2x_siaf_win_cfg {
	u8 sum_shift;
	u8 lum_shift;
	struct isp2x_window win;
} __attribute__ ((packed));

struct isp2x_siaf_cfg {
	u8 num_afm_win;
	u32 thres;
	struct isp2x_siaf_win_cfg afm_win[ISP2X_AFM_MAX_WINDOWS];
} __attribute__ ((packed));

struct isp2x_rawhistbig_cfg {
	u8 wnd_num;
	u8 data_sel;
	u8 waterline;
	u8 mode;
	u8 stepsize;
	u8 off;
	u8 bcc;
	u8 gcc;
	u8 rcc;
	struct isp2x_window win;
	u8 weight[ISP2X_RAWHISTBIG_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_rawhistlite_cfg {
	u8 data_sel;
	u8 waterline;
	u8 mode;
	u8 stepsize;
	u8 off;
	u8 bcc;
	u8 gcc;
	u8 rcc;
	struct isp2x_window win;
	u8 weight[ISP2X_RAWHISTLITE_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_sihst_win_cfg {
	u8 data_sel;
	u8 waterline;
	u8 auto_stop;
	u8 mode;
	u8 stepsize;
	struct isp2x_window win;
} __attribute__ ((packed));

struct isp2x_sihst_cfg {
	u8 wnd_num;
	struct isp2x_sihst_win_cfg win_cfg[ISP2X_SIHIST_WIN_NUM];
	u8 hist_weight[ISP2X_HIST_WEIGHT_NUM];
} __attribute__ ((packed));

struct isp2x_isp_other_cfg {
	struct isp2x_bls_cfg bls_cfg;
	struct isp2x_dpcc_cfg dpcc_cfg;
	struct isp2x_hdrmge_cfg hdrmge_cfg;
	struct isp2x_rawnr_cfg rawnr_cfg;
	struct isp2x_lsc_cfg lsc_cfg;
	struct isp2x_awb_gain_cfg awb_gain_cfg;
	//struct isp2x_goc_cfg goc_cfg;
	struct isp2x_gic_cfg gic_cfg;
	struct isp2x_debayer_cfg debayer_cfg;
	struct isp2x_ccm_cfg ccm_cfg;
	struct isp2x_gammaout_cfg gammaout_cfg;
	struct isp2x_wdr_cfg wdr_cfg;
	struct isp2x_cproc_cfg cproc_cfg;
	struct isp2x_ie_cfg ie_cfg;
	struct isp2x_rkiesharp_cfg rkiesharp_cfg;
	struct isp2x_superimp_cfg superimp_cfg;
	struct isp2x_sdg_cfg sdg_cfg;
	struct isp2x_bdm_config bdm_cfg;
	struct isp2x_hdrtmo_cfg hdrtmo_cfg;
	struct isp2x_dhaz_cfg dhaz_cfg;
	struct isp2x_gain_cfg gain_cfg;
	struct isp2x_3dlut_cfg isp3dlut_cfg;
	struct isp2x_ldch_cfg ldch_cfg;
} __attribute__ ((packed));

struct isp2x_isp_meas_cfg {
	struct isp2x_siawb_meas_cfg siawb;
	struct isp2x_rawawb_meas_cfg rawawb;
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

struct sensor_exposure_s {
	u32 fine_integration_time;
	u32 coarse_integration_time;
	u32 analog_gain_code_global;
	u32 digital_gain_global;
	u32 isp_digital_gain;
} __attribute__ ((packed));

struct sensor_exposure_cfg {
	struct sensor_exposure_s linear_exp;
	struct sensor_exposure_s hdr_exp[3];
} __attribute__ ((packed));

struct isp2x_isp_params_cfg {
	u64 module_en_update;
	u64 module_ens;
	u64 module_cfg_update;

	u32 frame_id;
	struct isp2x_isp_meas_cfg meas;
	struct isp2x_isp_other_cfg others;
	struct sensor_exposure_cfg exposure;
} __attribute__ ((packed));

struct isp2x_siawb_meas {
	u32 cnt;
	u8 mean_y_or_g;
	u8 mean_cb_or_b;
	u8 mean_cr_or_r;
} __attribute__ ((packed));

struct isp2x_siawb_stat {
	struct isp2x_siawb_meas awb_mean[ISP2X_AWB_MAX_GRID];
} __attribute__ ((packed));

struct isp2x_rawawb_ramdata {
	u32 wp;
	u32 r;
	u32 g;
	u32 b;
};

struct isp2x_rawawb_meas_stat {
	u32 ro_rawawb_sum_r_nor[ISP2X_RAWAWB_SUM_NUM];		//SUM_R_NOR_0
	u32 ro_rawawb_sum_g_nor[ISP2X_RAWAWB_SUM_NUM];		//SUM_G_NOR_0
	u32 ro_rawawb_sum_b_nor[ISP2X_RAWAWB_SUM_NUM];		//SUM_B_NOR_0
	u32 ro_rawawb_wp_num_nor[ISP2X_RAWAWB_SUM_NUM];		//WP_NUM_NOR_0
	u32 ro_rawawb_sum_r_big[ISP2X_RAWAWB_SUM_NUM];		//SUM_R_BIG_0
	u32 ro_rawawb_sum_g_big[ISP2X_RAWAWB_SUM_NUM];		//SUM_G_BIG_0
	u32 ro_rawawb_sum_b_big[ISP2X_RAWAWB_SUM_NUM];		//SUM_B_BIG_0
	u32 ro_rawawb_wp_num_big[ISP2X_RAWAWB_SUM_NUM];		//WP_NUM_BIG_0
	u32 ro_rawawb_sum_r_sma[ISP2X_RAWAWB_SUM_NUM];		//SUM_R_SMA_0
	u32 ro_rawawb_sum_g_sma[ISP2X_RAWAWB_SUM_NUM];		//SUM_G_SMA_0
	u32 ro_rawawb_sum_b_sma[ISP2X_RAWAWB_SUM_NUM];		//SUM_B_SMA_0
	u32 ro_rawawb_wp_num_sma[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_sum_r_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_R_NOR_MULTIWINDOW_0
	u32 ro_sum_g_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_G_NOR_MULTIWINDOW_0
	u32 ro_sum_b_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_B_NOR_MULTIWINDOW_0
	u32 ro_wp_nm_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//WP_NM_NOR_MULTIWINDOW_0
	u32 ro_sum_r_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_R_BIG_MULTIWINDOW_0
	u32 ro_sum_g_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_G_BIG_MULTIWINDOW_0
	u32 ro_sum_b_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_B_BIG_MULTIWINDOW_0
	u32 ro_wp_nm_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//WP_NM_BIG_MULTIWINDOW_0
	u32 ro_sum_r_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_R_SMA_MULTIWINDOW_0
	u32 ro_sum_g_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_G_SMA_MULTIWINDOW_0
	u32 ro_sum_b_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//SUM_B_SMA_MULTIWINDOW_0
	u32 ro_wp_nm_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	//WP_NM_SMA_MULTIWINDOW_0
	u32 ro_sum_r_exc[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_sum_g_exc[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_sum_b_exc[ISP2X_RAWAWB_SUM_NUM];
	u32 ro_wp_nm_exc[ISP2X_RAWAWB_SUM_NUM];
	struct isp2x_rawawb_ramdata ramdata[ISP2X_RAWAWB_RAMDATA_NUM];
} __attribute__ ((packed));

struct isp2x_rawae_meas_data {
	u16 channelr_xy;
	u16 channelb_xy;
	u16 channelg_xy;
};

struct isp2x_rawaebig_stat {
	u32 sumr[ISP2X_RAWAEBIG_SUBWIN_NUM];
	u32 sumg[ISP2X_RAWAEBIG_SUBWIN_NUM];
	u32 sumb[ISP2X_RAWAEBIG_SUBWIN_NUM];
	struct isp2x_rawae_meas_data data[ISP2X_RAWAEBIG_MEAN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaelite_stat {
	struct isp2x_rawae_meas_data data[ISP2X_RAWAELITE_MEAN_NUM];
} __attribute__ ((packed));

struct isp2x_yuvae_stat {
	u32 ro_yuvae_sumy[ISP2X_YUVAE_SUBWIN_NUM];
	u8 mean[ISP2X_YUVAE_MEAN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaf_stat {
	u32 int_state;
	u32 afm_sum[ISP2X_RAWAF_WIN_NUM];
	u32 afm_lum[ISP2X_RAWAF_WIN_NUM];
	u32 ramdata[ISP2X_RAWAF_SUMDATA_NUM];
} __attribute__ ((packed));

struct isp2x_siaf_meas_val {
	u32 sum;
	u32 lum;
} __attribute__ ((packed));

struct isp2x_siaf_stat {
	struct isp2x_siaf_meas_val win[ISP2X_AFM_MAX_WINDOWS];
} __attribute__ ((packed));

struct isp2x_rawhistbig_stat {
	u32 hist_bin[ISP2X_HIST_BIN_N_MAX];
} __attribute__ ((packed));

struct isp2x_rawhistlite_stat {
	u32 hist_bin[ISP2X_HIST_BIN_N_MAX];
} __attribute__ ((packed));

struct isp2x_sihst_win_stat {
	u32 hist_bins[ISP2X_SIHIST_BIN_N_MAX];
} __attribute__ ((packed));

struct isp2x_sihst_stat {
	struct isp2x_sihst_win_stat win_stat[ISP2X_SIHIST_WIN_NUM];
} __attribute__ ((packed));

struct isp2x_stat {
	struct isp2x_siawb_stat siawb;
	struct isp2x_rawawb_meas_stat rawawb;
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
	struct isp2x_hdrtmo_stat hdrtmo;
	struct isp2x_dhaz_stat dhaz;
} __attribute__ ((packed));

/**
 * struct rkisp_isp2x_stat_buffer - Rockchip ISP2 Statistics Meta Data
 *
 * @meas_type: measurement types (CIFISP_STAT_ definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp_isp2x_stat_buffer {
	unsigned int meas_type;
	unsigned int frame_id;
	struct isp2x_stat params;
} __attribute__ ((packed));

/**
 * struct rkisp_mipi_luma - statistics mipi y statistic
 *
 * @exp_mean: Mean luminance value of block xx
 *
 * Image is divided into 5x5 blocks.
 */
struct rkisp_mipi_luma {
	unsigned int exp_mean[ISP2X_MIPI_LUMA_MEAN_MAX];
} __attribute__ ((packed));

/**
 * struct rkisp_isp2x_luma_buffer - Rockchip ISP1 Statistics Mipi Luma
 *
 * @meas_type: measurement types (CIFISP_STAT_ definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp_isp2x_luma_buffer {
	unsigned int meas_type;
	unsigned int frame_id;
	struct rkisp_mipi_luma luma[ISP2X_MIPI_RAW_MAX];
} __attribute__ ((packed));

/**
 * struct rkisp_thunderboot_resmem_head
 */
struct rkisp_thunderboot_resmem_head {
	u16 enable;
	u16 complete;
	u16 frm_total;
	u16 hdr_mode;
	u16 width;
	u16 height;
	u32 bus_fmt;

	u32 exp_time[3];
	u32 exp_gain[3];
	u32 exp_time_reg[3];
	u32 exp_gain_reg[3];
} __attribute__ ((packed));

/**
 * struct rkisp_thunderboot_resmem - shared buffer for thunderboot with risc-v side
 */
struct rkisp_thunderboot_resmem {
	u32 resmem_padr;
	u32 resmem_size;
} __attribute__ ((packed));

/**
 * struct rkisp_thunderboot_shmem
 */
struct rkisp_thunderboot_shmem {
	u32 shm_start;
	u32 shm_size;
	s32 shm_fd;
} __attribute__ ((packed));

#endif /* _UAPI_RKISP2_CONFIG_H */
