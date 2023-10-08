/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT)
 *
 * Rockchip isp2 driver
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK_ISP2_CONFIG_H
#define _UAPI_RK_ISP2_CONFIG_H

#include <linux/const.h>
#include <linux/types.h>
#include <linux/v4l2-controls.h>

#define RKISP_API_VERSION		KERNEL_VERSION(2, 3, 0)

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

/* for all isp device stop and no power off but resolution change */
#define RKISP_CMD_MULTI_DEV_FORCE_ENUM \
	_IO('V', BASE_VIDIOC_PRIVATE + 13)

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
	_IOR('V', BASE_VIDIOC_PRIVATE + 107, struct rkisp_wrap_info)
/* set wrap line before VIDIOC_S_FMT */
#define RKISP_CMD_SET_WRAP_LINE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 108, struct rkisp_wrap_info)

#define RKISP_CMD_SET_FPS \
	_IOW('V', BASE_VIDIOC_PRIVATE + 109, int)

#define RKISP_CMD_GET_FPS \
	_IOR('V', BASE_VIDIOC_PRIVATE + 110, int)

#define RKISP_CMD_GET_TB_STREAM_INFO \
	_IOR('V', BASE_VIDIOC_PRIVATE + 111, struct rkisp_tb_stream_info)

#define RKISP_CMD_FREE_TB_STREAM_BUF \
	_IO('V', BASE_VIDIOC_PRIVATE + 112)

#define RKISP_CMD_SET_IQTOOL_CONN_ID \
	_IOW('V', BASE_VIDIOC_PRIVATE + 113, int)
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

#define ISP2X_MODULE_DPCC		_BITULL(ISP2X_ID_DPCC)
#define ISP2X_MODULE_BLS		_BITULL(ISP2X_ID_BLS)
#define ISP2X_MODULE_SDG		_BITULL(ISP2X_ID_SDG)
#define ISP2X_MODULE_SIHST		_BITULL(ISP2X_ID_SIHST)
#define ISP2X_MODULE_LSC		_BITULL(ISP2X_ID_LSC)
#define ISP2X_MODULE_AWB_GAIN		_BITULL(ISP2X_ID_AWB_GAIN)
#define ISP2X_MODULE_BDM		_BITULL(ISP2X_ID_BDM)
#define ISP2X_MODULE_CCM		_BITULL(ISP2X_ID_CCM)
#define ISP2X_MODULE_GOC		_BITULL(ISP2X_ID_GOC)
#define ISP2X_MODULE_CPROC		_BITULL(ISP2X_ID_CPROC)
#define ISP2X_MODULE_SIAF		_BITULL(ISP2X_ID_SIAF)
#define ISP2X_MODULE_SIAWB		_BITULL(ISP2X_ID_SIAWB)
#define ISP2X_MODULE_IE			_BITULL(ISP2X_ID_IE)
#define ISP2X_MODULE_YUVAE		_BITULL(ISP2X_ID_YUVAE)
#define ISP2X_MODULE_WDR		_BITULL(ISP2X_ID_WDR)
#define ISP2X_MODULE_RK_IESHARP		_BITULL(ISP2X_ID_RK_IESHARP)
#define ISP2X_MODULE_RAWAF		_BITULL(ISP2X_ID_RAWAF)
#define ISP2X_MODULE_RAWAE0		_BITULL(ISP2X_ID_RAWAE0)
#define ISP2X_MODULE_RAWAE1		_BITULL(ISP2X_ID_RAWAE1)
#define ISP2X_MODULE_RAWAE2		_BITULL(ISP2X_ID_RAWAE2)
#define ISP2X_MODULE_RAWAE3		_BITULL(ISP2X_ID_RAWAE3)
#define ISP2X_MODULE_RAWAWB		_BITULL(ISP2X_ID_RAWAWB)
#define ISP2X_MODULE_RAWHIST0		_BITULL(ISP2X_ID_RAWHIST0)
#define ISP2X_MODULE_RAWHIST1		_BITULL(ISP2X_ID_RAWHIST1)
#define ISP2X_MODULE_RAWHIST2		_BITULL(ISP2X_ID_RAWHIST2)
#define ISP2X_MODULE_RAWHIST3		_BITULL(ISP2X_ID_RAWHIST3)
#define ISP2X_MODULE_HDRMGE		_BITULL(ISP2X_ID_HDRMGE)
#define ISP2X_MODULE_RAWNR		_BITULL(ISP2X_ID_RAWNR)
#define ISP2X_MODULE_HDRTMO		_BITULL(ISP2X_ID_HDRTMO)
#define ISP2X_MODULE_GIC		_BITULL(ISP2X_ID_GIC)
#define ISP2X_MODULE_DHAZ		_BITULL(ISP2X_ID_DHAZ)
#define ISP2X_MODULE_3DLUT		_BITULL(ISP2X_ID_3DLUT)
#define ISP2X_MODULE_LDCH		_BITULL(ISP2X_ID_LDCH)
#define ISP2X_MODULE_GAIN		_BITULL(ISP2X_ID_GAIN)
#define ISP2X_MODULE_DEBAYER		_BITULL(ISP2X_ID_DEBAYER)

#define ISP2X_MODULE_FORCE		_BITULL(ISP2X_ID_MAX)

/*
 * Measurement types
 */
#define ISP2X_STAT_SIAWB		_BITUL(0)
#define ISP2X_STAT_YUVAE		_BITUL(1)
#define ISP2X_STAT_SIAF			_BITUL(2)
#define ISP2X_STAT_SIHST		_BITUL(3)
#define ISP2X_STAT_EMB_DATA		_BITUL(4)
#define ISP2X_STAT_RAWAWB		_BITUL(5)
#define ISP2X_STAT_RAWAF		_BITUL(6)
#define ISP2X_STAT_RAWAE0		_BITUL(7)
#define ISP2X_STAT_RAWAE1		_BITUL(8)
#define ISP2X_STAT_RAWAE2		_BITUL(9)
#define ISP2X_STAT_RAWAE3		_BITUL(10)
#define ISP2X_STAT_RAWHST0		_BITUL(11)
#define ISP2X_STAT_RAWHST1		_BITUL(12)
#define ISP2X_STAT_RAWHST2		_BITUL(13)
#define ISP2X_STAT_RAWHST3		_BITUL(14)
#define ISP2X_STAT_BLS			_BITUL(15)
#define ISP2X_STAT_HDRTMO		_BITUL(16)
#define ISP2X_STAT_DHAZ			_BITUL(17)

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
	RKISP_ISP_NORMAL = _BITUL(0),
	RKISP_ISP_HDR2 = _BITUL(1),
	RKISP_ISP_HDR3 = _BITUL(2),
	RKISP_ISP_COMPR = _BITUL(3),

	/* isp function related */
	RKISP_ISP_BIGMODE = _BITUL(28),
};

struct rkisp_isp_info {
	enum rkisp_isp_mode mode;
	__u32 act_width;
	__u32 act_height;
	__u8 compr_bit;
} __attribute__ ((packed));

enum isp2x_mesh_buf_stat {
	MESH_BUF_INIT = 0,
	MESH_BUF_WAIT2CHIP,
	MESH_BUF_CHIPINUSE,
};

struct rkisp_meshbuf_info {
	__u64 module_id;
	__u32 unite_isp_id;
	__s32 buf_fd[ISP2X_MESH_BUF_NUM];
	__u32 buf_size[ISP2X_MESH_BUF_NUM];
} __attribute__ ((packed));

struct rkisp_meshbuf_size {
	__u64 module_id;
	__u32 unite_isp_id;
	__u32 meas_width;
	__u32 meas_height;
	int buf_cnt;
} __attribute__ ((packed));

struct isp2x_mesh_head {
	enum isp2x_mesh_buf_stat stat;
	__u32 data_oft;
} __attribute__ ((packed));

#define RKISP_CMSK_WIN_MAX 12
#define RKISP_CMSK_WIN_MAX_V30 8
#define RKISP_CMSK_MOSAIC_MODE 0
#define RKISP_CMSK_COVER_MODE 1

/* struct rkisp_cmsk_win
 * Priacy Mask Window configture, support windows
 * RKISP_CMSK_WIN_MAX_V30 for rk3588 support 8 windows, and
 * support for mainpath and selfpath output stream channel.
 *
 * RKISP_CMSK_WIN_MAX for rv1106 support 12 windows, and
 * support for mainpath selfpath and bypasspath output stream channel.
 *
 * mode: 0:mosaic mode, 1:cover mode
 * win_index: window index 0~11. windows overlap, priority win11 > win0.
 * cover_color_y: cover mode effective, share for stream channel when same win_index.
 * cover_color_u: cover mode effective, share for stream channel when same win_index.
 * cover_color_v: cover mode effective, share for stream channel when same win_index.
 *
 * h_offs: window horizontal offset, share for stream channel when same win_index. 2 align.
 * v_offs: window vertical offset, share for stream channel when same win_index. 2 align.
 * h_size: window horizontal size, share for stream channel when same win_index. 8 align for rk3588, 2 align for rv1106.
 * v_size: window vertical size, share for stream channel when same win_index. 8 align for rk3588, 2 align for rv1106.
 */
struct rkisp_cmsk_win {
	unsigned short mode;
	unsigned short win_en;

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
 * mosaic_block: Mosaic block size, 0:8x8 1:16x16 2:32x32 3:64x64, share for all windows
 * width_ro: isp full resolution, h_offs + h_size <= width_ro.
 * height_ro: isp full resolution, v_offs + v_size <= height_ro.
 */
struct rkisp_cmsk_cfg {
	struct rkisp_cmsk_win win[RKISP_CMSK_WIN_MAX];
	unsigned int mosaic_block;
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
	unsigned char stream_id;
} __attribute__ ((packed));

/* struct rkisp_mirror_flip
 * mirror: global for all output stream
 * flip: independent for all output stream
 */
struct rkisp_mirror_flip {
	unsigned char mirror;
	unsigned char flip;
} __attribute__ ((packed));

struct rkisp_wrap_info {
	int width;
	int height;
};

#define RKISP_TB_STREAM_BUF_MAX 5
struct rkisp_tb_stream_buf {
	unsigned int dma_addr;
	unsigned int sequence;
	long long timestamp;
} __attribute__ ((packed));

/* struct rkisp_tb_stream_info
 * frame_size: nv12 frame buf size, bytesperline * height_16align * 1.5
 * buf_max: memory size / frame_size
 * buf_cnt: the num of frame write to buf.
 */
struct rkisp_tb_stream_info {
	unsigned int width;
	unsigned int height;
	unsigned int bytesperline;
	unsigned int frame_size;
	unsigned int buf_max;
	unsigned int buf_cnt;
	struct rkisp_tb_stream_buf buf[RKISP_TB_STREAM_BUF_MAX];
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
	T_TRY = _BITUL(0),
	T_TRY_YES = _BITUL(1),
	T_TRY_NO = _BITUL(2),

	T_START_X1 = _BITUL(4),
	T_START_X2 = _BITUL(5),
	T_START_X3 = _BITUL(6),
	T_START_C = _BITUL(7),
};

struct isp2x_csi_trigger {
	/* timestamp in ns */
	__u64 sof_timestamp;
	__u64 frame_timestamp;
	__u32 frame_id;
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
			__u8 gain2ddr_mode;
		} gain;

		struct {
			__u8 awb2ddr_sel;
		} awb;
	} u;

	__u8 buf_cnt;
	__s32 buf_fd[RKISP_INFO2DDR_BUF_MAX];

	__u32 wsize;
	__u32 vsize;
} __attribute__ ((packed));

struct isp2x_ispgain_buf {
	__u32 gain_dmaidx;
	__u32 mfbc_dmaidx;
	__u32 gain_size;
	__u32 mfbc_size;
	__u32 frame_id;
} __attribute__ ((packed));

struct isp2x_buf_idxfd {
	__u32 buf_num;
	__u32 index[ISP2X_FBCBUF_FD_NUM];
	__s32 dmafd[ISP2X_FBCBUF_FD_NUM];
} __attribute__ ((packed));

struct isp2x_window {
	__u16 h_offs;
	__u16 v_offs;
	__u16 h_size;
	__u16 v_size;
} __attribute__ ((packed));

struct isp2x_bls_fixed_val {
	__s16 r;
	__s16 gr;
	__s16 gb;
	__s16 b;
} __attribute__ ((packed));

struct isp2x_bls_cfg {
	__u8 enable_auto;
	__u8 en_windows;
	struct isp2x_window bls_window1;
	struct isp2x_window bls_window2;
	__u8 bls_samples;
	struct isp2x_bls_fixed_val fixed_val;
} __attribute__ ((packed));

struct isp2x_bls_stat {
	__u16 meas_r;
	__u16 meas_gr;
	__u16 meas_gb;
	__u16 meas_b;
} __attribute__ ((packed));

struct isp2x_dpcc_pdaf_point {
	__u8 y;
	__u8 x;
} __attribute__ ((packed));

struct isp2x_dpcc_cfg {
	/* mode 0x0000 */
	__u8 stage1_enable;
	__u8 grayscale_mode;

	/* output_mode 0x0004 */
	__u8 sw_rk_out_sel;
	__u8 sw_dpcc_output_sel;
	__u8 stage1_rb_3x3;
	__u8 stage1_g_3x3;
	__u8 stage1_incl_rb_center;
	__u8 stage1_incl_green_center;

	/* set_use 0x0008 */
	__u8 stage1_use_fix_set;
	__u8 stage1_use_set_3;
	__u8 stage1_use_set_2;
	__u8 stage1_use_set_1;

	/* methods_set_1 0x000c */
	__u8 sw_rk_red_blue1_en;
	__u8 rg_red_blue1_enable;
	__u8 rnd_red_blue1_enable;
	__u8 ro_red_blue1_enable;
	__u8 lc_red_blue1_enable;
	__u8 pg_red_blue1_enable;
	__u8 sw_rk_green1_en;
	__u8 rg_green1_enable;
	__u8 rnd_green1_enable;
	__u8 ro_green1_enable;
	__u8 lc_green1_enable;
	__u8 pg_green1_enable;

	/* methods_set_2 0x0010 */
	__u8 sw_rk_red_blue2_en;
	__u8 rg_red_blue2_enable;
	__u8 rnd_red_blue2_enable;
	__u8 ro_red_blue2_enable;
	__u8 lc_red_blue2_enable;
	__u8 pg_red_blue2_enable;
	__u8 sw_rk_green2_en;
	__u8 rg_green2_enable;
	__u8 rnd_green2_enable;
	__u8 ro_green2_enable;
	__u8 lc_green2_enable;
	__u8 pg_green2_enable;

	/* methods_set_3 0x0014 */
	__u8 sw_rk_red_blue3_en;
	__u8 rg_red_blue3_enable;
	__u8 rnd_red_blue3_enable;
	__u8 ro_red_blue3_enable;
	__u8 lc_red_blue3_enable;
	__u8 pg_red_blue3_enable;
	__u8 sw_rk_green3_en;
	__u8 rg_green3_enable;
	__u8 rnd_green3_enable;
	__u8 ro_green3_enable;
	__u8 lc_green3_enable;
	__u8 pg_green3_enable;

	/* line_thresh_1 0x0018 */
	__u8 sw_mindis1_rb;
	__u8 sw_mindis1_g;
	__u8 line_thr_1_rb;
	__u8 line_thr_1_g;

	/* line_mad_fac_1 0x001c */
	__u8 sw_dis_scale_min1;
	__u8 sw_dis_scale_max1;
	__u8 line_mad_fac_1_rb;
	__u8 line_mad_fac_1_g;

	/* pg_fac_1 0x0020 */
	__u8 pg_fac_1_rb;
	__u8 pg_fac_1_g;

	/* rnd_thresh_1 0x0024 */
	__u8 rnd_thr_1_rb;
	__u8 rnd_thr_1_g;

	/* rg_fac_1 0x0028 */
	__u8 rg_fac_1_rb;
	__u8 rg_fac_1_g;

	/* line_thresh_2 0x002c */
	__u8 sw_mindis2_rb;
	__u8 sw_mindis2_g;
	__u8 line_thr_2_rb;
	__u8 line_thr_2_g;

	/* line_mad_fac_2 0x0030 */
	__u8 sw_dis_scale_min2;
	__u8 sw_dis_scale_max2;
	__u8 line_mad_fac_2_rb;
	__u8 line_mad_fac_2_g;

	/* pg_fac_2 0x0034 */
	__u8 pg_fac_2_rb;
	__u8 pg_fac_2_g;

	/* rnd_thresh_2 0x0038 */
	__u8 rnd_thr_2_rb;
	__u8 rnd_thr_2_g;

	/* rg_fac_2 0x003c */
	__u8 rg_fac_2_rb;
	__u8 rg_fac_2_g;

	/* line_thresh_3 0x0040 */
	__u8 sw_mindis3_rb;
	__u8 sw_mindis3_g;
	__u8 line_thr_3_rb;
	__u8 line_thr_3_g;

	/* line_mad_fac_3 0x0044 */
	__u8 sw_dis_scale_min3;
	__u8 sw_dis_scale_max3;
	__u8 line_mad_fac_3_rb;
	__u8 line_mad_fac_3_g;

	/* pg_fac_3 0x0048 */
	__u8 pg_fac_3_rb;
	__u8 pg_fac_3_g;

	/* rnd_thresh_3 0x004c */
	__u8 rnd_thr_3_rb;
	__u8 rnd_thr_3_g;

	/* rg_fac_3 0x0050 */
	__u8 rg_fac_3_rb;
	__u8 rg_fac_3_g;

	/* ro_limits 0x0054 */
	__u8 ro_lim_3_rb;
	__u8 ro_lim_3_g;
	__u8 ro_lim_2_rb;
	__u8 ro_lim_2_g;
	__u8 ro_lim_1_rb;
	__u8 ro_lim_1_g;

	/* rnd_offs 0x0058 */
	__u8 rnd_offs_3_rb;
	__u8 rnd_offs_3_g;
	__u8 rnd_offs_2_rb;
	__u8 rnd_offs_2_g;
	__u8 rnd_offs_1_rb;
	__u8 rnd_offs_1_g;

	/* bpt_ctrl 0x005c */
	__u8 bpt_rb_3x3;
	__u8 bpt_g_3x3;
	__u8 bpt_incl_rb_center;
	__u8 bpt_incl_green_center;
	__u8 bpt_use_fix_set;
	__u8 bpt_use_set_3;
	__u8 bpt_use_set_2;
	__u8 bpt_use_set_1;
	__u8 bpt_cor_en;
	__u8 bpt_det_en;

	/* bpt_number 0x0060 */
	__u16 bp_number;

	/* bpt_addr 0x0064 */
	__u16 bp_table_addr;

	/* bpt_data 0x0068 */
	__u16 bpt_v_addr;
	__u16 bpt_h_addr;

	/* bp_cnt 0x006c */
	__u32 bp_cnt;

	/* pdaf_en 0x0070 */
	__u8 sw_pdaf_en;

	/* pdaf_point_en 0x0074 */
	__u8 pdaf_point_en[ISP2X_DPCC_PDAF_POINT_NUM];

	/* pdaf_offset 0x0078 */
	__u16 pdaf_offsety;
	__u16 pdaf_offsetx;

	/* pdaf_wrap 0x007c */
	__u16 pdaf_wrapy;
	__u16 pdaf_wrapx;

	/* pdaf_scope 0x0080 */
	__u16 pdaf_wrapy_num;
	__u16 pdaf_wrapx_num;

	/* pdaf_point_0 0x0084 */
	struct isp2x_dpcc_pdaf_point point[ISP2X_DPCC_PDAF_POINT_NUM];

	/* pdaf_forward_med 0x00a4 */
	__u8 pdaf_forward_med;
} __attribute__ ((packed));

struct isp2x_hdrmge_curve {
	__u16 curve_1[ISP2X_HDRMGE_L_CURVE_NUM];
	__u16 curve_0[ISP2X_HDRMGE_L_CURVE_NUM];
} __attribute__ ((packed));

struct isp2x_hdrmge_cfg {
	__u8 mode;

	__u16 gain0_inv;
	__u16 gain0;

	__u16 gain1_inv;
	__u16 gain1;

	__u8 gain2;

	__u8 lm_dif_0p15;
	__u8 lm_dif_0p9;
	__u8 ms_diff_0p15;
	__u8 ms_dif_0p8;

	struct isp2x_hdrmge_curve curve;
	__u16 e_y[ISP2X_HDRMGE_E_CURVE_NUM];
} __attribute__ ((packed));

struct isp2x_rawnr_cfg {
	__u8 gauss_en;
	__u8 log_bypass;

	__u16 filtpar0;
	__u16 filtpar1;
	__u16 filtpar2;

	__u32 dgain0;
	__u32 dgain1;
	__u32 dgain2;

	__u16 luration[ISP2X_RAWNR_LUMA_RATION_NUM];
	__u16 lulevel[ISP2X_RAWNR_LUMA_RATION_NUM];

	__u32 gauss;
	__u16 sigma;
	__u16 pix_diff;

	__u32 thld_diff;

	__u8 gas_weig_scl2;
	__u8 gas_weig_scl1;
	__u16 thld_chanelw;

	__u16 lamda;

	__u16 fixw0;
	__u16 fixw1;
	__u16 fixw2;
	__u16 fixw3;

	__u32 wlamda0;
	__u32 wlamda1;
	__u32 wlamda2;

	__u16 rgain_filp;
	__u16 bgain_filp;
} __attribute__ ((packed));

struct isp2x_lsc_cfg {
	__u16 r_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];
	__u16 gr_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];
	__u16 gb_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];
	__u16 b_data_tbl[ISP2X_LSC_DATA_TBL_SIZE];

	__u16 x_grad_tbl[ISP2X_LSC_GRAD_TBL_SIZE];
	__u16 y_grad_tbl[ISP2X_LSC_GRAD_TBL_SIZE];

	__u16 x_size_tbl[ISP2X_LSC_SIZE_TBL_SIZE];
	__u16 y_size_tbl[ISP2X_LSC_SIZE_TBL_SIZE];
} __attribute__ ((packed));

enum isp2x_goc_mode {
	ISP2X_GOC_MODE_LOGARITHMIC,
	ISP2X_GOC_MODE_EQUIDISTANT
};

struct isp2x_goc_cfg {
	enum isp2x_goc_mode mode;
	__u8 gamma_y[17];
} __attribute__ ((packed));

struct isp2x_hdrtmo_predict {
	__u8 global_tmo;
	__s32 iir_max;
	__s32 global_tmo_strength;

	__u8 scene_stable;
	__s32 k_rolgmean;
	__s32 iir;
} __attribute__ ((packed));

struct isp2x_hdrtmo_cfg {
	__u16 cnt_vsize;
	__u8 gain_ld_off2;
	__u8 gain_ld_off1;
	__u8 big_en;
	__u8 nobig_en;
	__u8 newhst_en;
	__u8 cnt_mode;

	__u16 expl_lgratio;
	__u8 lgscl_ratio;
	__u8 cfg_alpha;

	__u16 set_gainoff;
	__u16 set_palpha;

	__u16 set_lgmax;
	__u16 set_lgmin;

	__u8 set_weightkey;
	__u16 set_lgmean;

	__u16 set_lgrange1;
	__u16 set_lgrange0;

	__u16 set_lgavgmax;

	__u8 clipgap1_i;
	__u8 clipgap0_i;
	__u8 clipratio1;
	__u8 clipratio0;
	__u8 ratiol;

	__u16 lgscl_inv;
	__u16 lgscl;

	__u16 lgmax;

	__u16 hist_low;
	__u16 hist_min;

	__u8 hist_shift;
	__u16 hist_0p3;
	__u16 hist_high;

	__u16 palpha_lwscl;
	__u16 palpha_lw0p5;
	__u16 palpha_0p18;

	__u16 maxgain;
	__u16 maxpalpha;

	struct isp2x_hdrtmo_predict predict;
} __attribute__ ((packed));

struct isp2x_hdrtmo_stat {
	__u16 lglow;
	__u16 lgmin;
	__u16 lghigh;
	__u16 lgmax;
	__u16 weightkey;
	__u16 lgmean;
	__u16 lgrange1;
	__u16 lgrange0;
	__u16 palpha;
	__u16 lgavgmax;
	__u16 linecnt;
	__u32 min_max[ISP2X_HDRTMO_MINMAX_NUM];
} __attribute__ ((packed));

struct isp2x_gic_cfg {
	__u8 edge_open;

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
	__u16 dnloscale;
	__u16 dnhiscale;
	__u8 reglumapointsstep;

	__u16 gvaluelimitlo;
	__u16 gvaluelimithi;
	__u8 fusionratiohilimt1;

	__u8 regstrength_fix;

	__u16 sigma_y[ISP2X_GIC_SIGMA_Y_NUM];

	__u8 noise_cut_en;
	__u16 noise_coe_a;

	__u16 noise_coe_b;
	__u16 diff_clip;
} __attribute__ ((packed));

struct isp2x_debayer_cfg {
	__u8 filter_c_en;
	__u8 filter_g_en;

	__u8 thed1;
	__u8 thed0;
	__u8 dist_scale;
	__u8 max_ratio;
	__u8 clip_en;

	__s8 filter1_coe5;
	__s8 filter1_coe4;
	__s8 filter1_coe3;
	__s8 filter1_coe2;
	__s8 filter1_coe1;

	__s8 filter2_coe5;
	__s8 filter2_coe4;
	__s8 filter2_coe3;
	__s8 filter2_coe2;
	__s8 filter2_coe1;

	__u16 hf_offset;
	__u8 gain_offset;
	__u8 offset;

	__u8 shift_num;
	__u8 order_max;
	__u8 order_min;
} __attribute__ ((packed));

struct isp2x_ccm_cfg {
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

	__u16 alp_y[ISP2X_CCM_CURVE_NUM];

	__u8 bound_bit;
} __attribute__ ((packed));

struct isp2x_gammaout_cfg {
	__u8 equ_segm;
	__u16 offset;
	__u16 gamma_y[ISP2X_GAMMA_OUT_MAX_SAMPLES];
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
	__u8 enhance_en;
	__u8 hist_chn;
	__u8 hpara_en;
	__u8 hist_en;
	__u8 dc_en;
	__u8 big_en;
	__u8 nobig_en;

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

	__u8 hist_th_off;
	__u8 hist_gratio;

	__u16 hist_min;
	__u16 hist_k;

	__u16 enhance_value;
	__u16 hist_scale;

	__u16 iir_wt_sigma;
	__u16 iir_sigma;
	__u16 stab_fnum;

	__u16 iir_tmax_sigma;
	__u16 iir_air_sigma;

	__u16 cfg_wt;
	__u16 cfg_air;
	__u16 cfg_alpha;

	__u16 cfg_gratio;
	__u16 cfg_tmax;

	__u16 dc_weitcur;
	__u16 dc_thed;

	__u8 sw_dhaz_dc_bf_h3;
	__u8 sw_dhaz_dc_bf_h2;
	__u8 sw_dhaz_dc_bf_h1;
	__u8 sw_dhaz_dc_bf_h0;

	__u8 sw_dhaz_dc_bf_h5;
	__u8 sw_dhaz_dc_bf_h4;

	__u16 air_weitcur;
	__u16 air_thed;

	__u8 air_bf_h2;
	__u8 air_bf_h1;
	__u8 air_bf_h0;

	__u8 gaus_h2;
	__u8 gaus_h1;
	__u8 gaus_h0;

	__u8 conv_t0[ISP2X_DHAZ_CONV_COEFF_NUM];
	__u8 conv_t1[ISP2X_DHAZ_CONV_COEFF_NUM];
	__u8 conv_t2[ISP2X_DHAZ_CONV_COEFF_NUM];
} __attribute__ ((packed));

struct isp2x_dhaz_stat {
	__u16 dhaz_adp_air_base;
	__u16 dhaz_adp_wt;

	__u16 dhaz_adp_gratio;
	__u16 dhaz_adp_tmax;

	__u16 h_r_iir[ISP2X_DHAZ_HIST_IIR_NUM];
	__u16 h_g_iir[ISP2X_DHAZ_HIST_IIR_NUM];
	__u16 h_b_iir[ISP2X_DHAZ_HIST_IIR_NUM];
} __attribute__ ((packed));

struct isp2x_cproc_cfg {
	__u8 c_out_range;
	__u8 y_in_range;
	__u8 y_out_range;
	__u8 contrast;
	__u8 brightness;
	__u8 sat;
	__u8 hue;
} __attribute__ ((packed));

struct isp2x_ie_cfg {
	__u16 effect;
	__u16 color_sel;
	__u16 eff_mat_1;
	__u16 eff_mat_2;
	__u16 eff_mat_3;
	__u16 eff_mat_4;
	__u16 eff_mat_5;
	__u16 eff_tint;
} __attribute__ ((packed));

struct isp2x_rkiesharp_cfg {
	__u8 coring_thr;
	__u8 full_range;
	__u8 switch_avg;
	__u8 yavg_thr[4];
	__u8 delta1[5];
	__u8 delta2[5];
	__u8 maxnumber[5];
	__u8 minnumber[5];
	__u8 gauss_flat_coe[9];
	__u8 gauss_noise_coe[9];
	__u8 gauss_other_coe[9];
	__u8 line1_filter_coe[6];
	__u8 line2_filter_coe[9];
	__u8 line3_filter_coe[6];
	__u16 grad_seq[4];
	__u8 sharp_factor[5];
	__u8 uv_gauss_flat_coe[15];
	__u8 uv_gauss_noise_coe[15];
	__u8 uv_gauss_other_coe[15];
	__u8 lap_mat_coe[9];
} __attribute__ ((packed));

struct isp2x_superimp_cfg {
	__u8 transparency_mode;
	__u8 ref_image;

	__u16 offset_x;
	__u16 offset_y;

	__u8 y_comp;
	__u8 cb_comp;
	__u8 cr_comp;
} __attribute__ ((packed));

struct isp2x_gamma_corr_curve {
	__u16 gamma_y[ISP2X_DEGAMMA_CURVE_SIZE];
} __attribute__ ((packed));

struct isp2x_gamma_curve_x_axis_pnts {
	__u32 gamma_dx0;
	__u32 gamma_dx1;
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
	__u8 dhaz_en;
	__u8 wdr_en;
	__u8 tmo_en;
	__u8 lsc_en;
	__u8 mge_en;

	__u32 mge_gain[ISP2X_GAIN_HDRMGE_GAIN_NUM];
	__u16 idx[ISP2X_GAIN_IDX_NUM];
	__u16 lut[ISP2X_GAIN_LUT_NUM];
} __attribute__ ((packed));

struct isp2x_3dlut_cfg {
	__u8 bypass_en;
	__u32 actual_size;	/* word unit */
	__u16 lut_r[ISP2X_3DLUT_DATA_NUM];
	__u16 lut_g[ISP2X_3DLUT_DATA_NUM];
	__u16 lut_b[ISP2X_3DLUT_DATA_NUM];
} __attribute__ ((packed));

enum isp2x_ldch_buf_stat {
	LDCH_BUF_INIT = 0,
	LDCH_BUF_WAIT2CHIP,
	LDCH_BUF_CHIPINUSE,
};

struct rkisp_ldchbuf_info {
	__s32 buf_fd[ISP2X_LDCH_BUF_NUM];
	__u32 buf_size[ISP2X_LDCH_BUF_NUM];
} __attribute__ ((packed));

struct rkisp_ldchbuf_size {
	__u32 meas_width;
	__u32 meas_height;
} __attribute__ ((packed));

struct isp2x_ldch_head {
	enum isp2x_ldch_buf_stat stat;
	__u32 data_oft;
} __attribute__ ((packed));

struct isp2x_ldch_cfg {
	__u32 hsize;
	__u32 vsize;
	__s32 buf_fd;
} __attribute__ ((packed));

struct isp2x_awb_gain_cfg {
	__u16 gain_red;
	__u16 gain_green_r;
	__u16 gain_blue;
	__u16 gain_green_b;
} __attribute__ ((packed));

struct isp2x_siawb_meas_cfg {
	struct isp2x_window awb_wnd;
	__u8 awb_mode;
	__u8 max_y;
	__u8 min_y;
	__u8 max_csum;
	__u8 min_c;
	__u8 frames;
	__u8 awb_ref_cr;
	__u8 awb_ref_cb;
	__u8 enable_ymax_cmp;
} __attribute__ ((packed));

struct isp2x_rawawb_meas_cfg {
	__u8 rawawb_sel;
	__u8 sw_rawawb_light_num;		/* CTRL */
	__u8 sw_rawawb_wind_size;		/* CTRL */
	__u8 sw_rawawb_c_range;			/* CTRL */
	__u8 sw_rawawb_y_range;			/* CTRL */
	__u8 sw_rawawb_3dyuv_ls_idx3;		/* CTRL */
	__u8 sw_rawawb_3dyuv_ls_idx2;		/* CTRL */
	__u8 sw_rawawb_3dyuv_ls_idx1;		/* CTRL */
	__u8 sw_rawawb_3dyuv_ls_idx0;		/* CTRL */
	__u8 sw_rawawb_xy_en;			/* CTRL */
	__u8 sw_rawawb_uv_en;			/* CTRL */
	__u8 sw_rawlsc_bypass_en;		/* CTRL */
	__u8 sw_rawawb_blk_measure_mode;	/* BLK_CTRL */
	__u8 sw_rawawb_store_wp_flag_ls_idx2;	/* BLK_CTRL */
	__u8 sw_rawawb_store_wp_flag_ls_idx1;	/* BLK_CTRL */
	__u8 sw_rawawb_store_wp_flag_ls_idx0;	/* BLK_CTRL */
	__u16 sw_rawawb_store_wp_th0;		/* BLK_CTRL */
	__u16 sw_rawawb_store_wp_th1;		/* BLK_CTRL */
	__u16 sw_rawawb_store_wp_th2;		/* RAW_CTRL */
	__u16 sw_rawawb_v_offs;			/* WIN_OFFS */
	__u16 sw_rawawb_h_offs;			/* WIN_OFFS */
	__u16 sw_rawawb_v_size;			/* WIN_SIZE */
	__u16 sw_rawawb_h_size;			/* WIN_SIZE */
	__u16 sw_rawawb_g_max;			/* LIMIT_RG_MAX */
	__u16 sw_rawawb_r_max;			/* LIMIT_RG_MAX */
	__u16 sw_rawawb_y_max;			/* LIMIT_BY_MAX */
	__u16 sw_rawawb_b_max;			/* LIMIT_BY_MAX */
	__u16 sw_rawawb_g_min;			/* LIMIT_RG_MIN */
	__u16 sw_rawawb_r_min;			/* LIMIT_RG_MIN */
	__u16 sw_rawawb_y_min;			/* LIMIT_BY_MIN */
	__u16 sw_rawawb_b_min;			/* LIMIT_BY_MIN */
	__u16 sw_rawawb_coeff_y_g;		/* RGB2Y_0 */
	__u16 sw_rawawb_coeff_y_r;		/* RGB2Y_0 */
	__u16 sw_rawawb_coeff_y_b;		/* RGB2Y_1 */
	__u16 sw_rawawb_coeff_u_g;		/* RGB2U_0 */
	__u16 sw_rawawb_coeff_u_r;		/* RGB2U_0 */
	__u16 sw_rawawb_coeff_u_b;		/* RGB2U_1 */
	__u16 sw_rawawb_coeff_v_g;		/* RGB2V_0 */
	__u16 sw_rawawb_coeff_v_r;		/* RGB2V_0 */
	__u16 sw_rawawb_coeff_v_b;		/* RGB2V_1 */
	__u16 sw_rawawb_vertex0_v_0;		/* UV_DETC_VERTEX0_0 */
	__u16 sw_rawawb_vertex0_u_0;		/* UV_DETC_VERTEX0_0 */
	__u16 sw_rawawb_vertex1_v_0;		/* UV_DETC_VERTEX1_0 */
	__u16 sw_rawawb_vertex1_u_0;		/* UV_DETC_VERTEX1_0 */
	__u16 sw_rawawb_vertex2_v_0;		/* UV_DETC_VERTEX2_0 */
	__u16 sw_rawawb_vertex2_u_0;		/* UV_DETC_VERTEX2_0 */
	__u16 sw_rawawb_vertex3_v_0;		/* UV_DETC_VERTEX3_0 */
	__u16 sw_rawawb_vertex3_u_0;		/* UV_DETC_VERTEX3_0 */
	__u32 sw_rawawb_islope01_0;		/* UV_DETC_ISLOPE01_0 */
	__u32 sw_rawawb_islope12_0;		/* UV_DETC_ISLOPE12_0 */
	__u32 sw_rawawb_islope23_0;		/* UV_DETC_ISLOPE23_0 */
	__u32 sw_rawawb_islope30_0;		/* UV_DETC_ISLOPE30_0 */
	__u16 sw_rawawb_vertex0_v_1;		/* UV_DETC_VERTEX0_1 */
	__u16 sw_rawawb_vertex0_u_1;		/* UV_DETC_VERTEX0_1 */
	__u16 sw_rawawb_vertex1_v_1;		/* UV_DETC_VERTEX1_1 */
	__u16 sw_rawawb_vertex1_u_1;		/* UV_DETC_VERTEX1_1 */
	__u16 sw_rawawb_vertex2_v_1;		/* UV_DETC_VERTEX2_1 */
	__u16 sw_rawawb_vertex2_u_1;		/* UV_DETC_VERTEX2_1 */
	__u16 sw_rawawb_vertex3_v_1;		/* UV_DETC_VERTEX3_1 */
	__u16 sw_rawawb_vertex3_u_1;		/* UV_DETC_VERTEX3_1 */
	__u32 sw_rawawb_islope01_1;		/* UV_DETC_ISLOPE01_1 */
	__u32 sw_rawawb_islope12_1;		/* UV_DETC_ISLOPE12_1 */
	__u32 sw_rawawb_islope23_1;		/* UV_DETC_ISLOPE23_1 */
	__u32 sw_rawawb_islope30_1;		/* UV_DETC_ISLOPE30_1 */
	__u16 sw_rawawb_vertex0_v_2;		/* UV_DETC_VERTEX0_2 */
	__u16 sw_rawawb_vertex0_u_2;		/* UV_DETC_VERTEX0_2 */
	__u16 sw_rawawb_vertex1_v_2;		/* UV_DETC_VERTEX1_2 */
	__u16 sw_rawawb_vertex1_u_2;		/* UV_DETC_VERTEX1_2 */
	__u16 sw_rawawb_vertex2_v_2;		/* UV_DETC_VERTEX2_2 */
	__u16 sw_rawawb_vertex2_u_2;		/* UV_DETC_VERTEX2_2 */
	__u16 sw_rawawb_vertex3_v_2;		/* UV_DETC_VERTEX3_2 */
	__u16 sw_rawawb_vertex3_u_2;		/* UV_DETC_VERTEX3_2 */
	__u32 sw_rawawb_islope01_2;		/* UV_DETC_ISLOPE01_2 */
	__u32 sw_rawawb_islope12_2;		/* UV_DETC_ISLOPE12_2 */
	__u32 sw_rawawb_islope23_2;		/* UV_DETC_ISLOPE23_2 */
	__u32 sw_rawawb_islope30_2;		/* UV_DETC_ISLOPE30_2 */
	__u16 sw_rawawb_vertex0_v_3;		/* UV_DETC_VERTEX0_3 */
	__u16 sw_rawawb_vertex0_u_3;		/* UV_DETC_VERTEX0_3 */
	__u16 sw_rawawb_vertex1_v_3;		/* UV_DETC_VERTEX1_3 */
	__u16 sw_rawawb_vertex1_u_3;		/* UV_DETC_VERTEX1_3 */
	__u16 sw_rawawb_vertex2_v_3;		/* UV_DETC_VERTEX2_3 */
	__u16 sw_rawawb_vertex2_u_3;		/* UV_DETC_VERTEX2_3 */
	__u16 sw_rawawb_vertex3_v_3;		/* UV_DETC_VERTEX3_3 */
	__u16 sw_rawawb_vertex3_u_3;		/* UV_DETC_VERTEX3_3 */
	__u32 sw_rawawb_islope01_3;		/* UV_DETC_ISLOPE01_3 */
	__u32 sw_rawawb_islope12_3;		/* UV_DETC_ISLOPE12_3 */
	__u32 sw_rawawb_islope23_3;		/* UV_DETC_ISLOPE23_3 */
	__u32 sw_rawawb_islope30_3;		/* UV_DETC_ISLOPE30_3 */
	__u16 sw_rawawb_vertex0_v_4;		/* UV_DETC_VERTEX0_4 */
	__u16 sw_rawawb_vertex0_u_4;		/* UV_DETC_VERTEX0_4 */
	__u16 sw_rawawb_vertex1_v_4;		/* UV_DETC_VERTEX1_4 */
	__u16 sw_rawawb_vertex1_u_4;		/* UV_DETC_VERTEX1_4 */
	__u16 sw_rawawb_vertex2_v_4;		/* UV_DETC_VERTEX2_4 */
	__u16 sw_rawawb_vertex2_u_4;		/* UV_DETC_VERTEX2_4 */
	__u16 sw_rawawb_vertex3_v_4;		/* UV_DETC_VERTEX3_4 */
	__u16 sw_rawawb_vertex3_u_4;		/* UV_DETC_VERTEX3_4 */
	__u32 sw_rawawb_islope01_4;		/* UV_DETC_ISLOPE01_4 */
	__u32 sw_rawawb_islope12_4;		/* UV_DETC_ISLOPE12_4 */
	__u32 sw_rawawb_islope23_4;		/* UV_DETC_ISLOPE23_4 */
	__u32 sw_rawawb_islope30_4;		/* UV_DETC_ISLOPE30_4 */
	__u16 sw_rawawb_vertex0_v_5;		/* UV_DETC_VERTEX0_5 */
	__u16 sw_rawawb_vertex0_u_5;		/* UV_DETC_VERTEX0_5 */
	__u16 sw_rawawb_vertex1_v_5;		/* UV_DETC_VERTEX1_5 */
	__u16 sw_rawawb_vertex1_u_5;		/* UV_DETC_VERTEX1_5 */
	__u16 sw_rawawb_vertex2_v_5;		/* UV_DETC_VERTEX2_5 */
	__u16 sw_rawawb_vertex2_u_5;		/* UV_DETC_VERTEX2_5 */
	__u16 sw_rawawb_vertex3_v_5;		/* UV_DETC_VERTEX3_5 */
	__u16 sw_rawawb_vertex3_u_5;		/* UV_DETC_VERTEX3_5 */
	__u32 sw_rawawb_islope01_5;		/* UV_DETC_ISLOPE01_5 */
	__u32 sw_rawawb_islope12_5;		/* UV_DETC_ISLOPE10_5 */
	__u32 sw_rawawb_islope23_5;		/* UV_DETC_ISLOPE23_5 */
	__u32 sw_rawawb_islope30_5;		/* UV_DETC_ISLOPE30_5 */
	__u16 sw_rawawb_vertex0_v_6;		/* UV_DETC_VERTEX0_6 */
	__u16 sw_rawawb_vertex0_u_6;		/* UV_DETC_VERTEX0_6 */
	__u16 sw_rawawb_vertex1_v_6;		/* UV_DETC_VERTEX1_6 */
	__u16 sw_rawawb_vertex1_u_6;		/* UV_DETC_VERTEX1_6 */
	__u16 sw_rawawb_vertex2_v_6;		/* UV_DETC_VERTEX2_6 */
	__u16 sw_rawawb_vertex2_u_6;		/* UV_DETC_VERTEX2_6 */
	__u16 sw_rawawb_vertex3_v_6;		/* UV_DETC_VERTEX3_6 */
	__u16 sw_rawawb_vertex3_u_6;		/* UV_DETC_VERTEX3_6 */
	__u32 sw_rawawb_islope01_6;		/* UV_DETC_ISLOPE01_6 */
	__u32 sw_rawawb_islope12_6;		/* UV_DETC_ISLOPE10_6 */
	__u32 sw_rawawb_islope23_6;		/* UV_DETC_ISLOPE23_6 */
	__u32 sw_rawawb_islope30_6;		/* UV_DETC_ISLOPE30_6 */
	__u32 sw_rawawb_b_uv_0;			/* YUV_DETC_B_UV_0 */
	__u32 sw_rawawb_slope_vtcuv_0;		/* YUV_DETC_SLOPE_VTCUV_0 */
	__u32 sw_rawawb_inv_dslope_0;		/* YUV_DETC_INV_DSLOPE_0 */
	__u32 sw_rawawb_slope_ydis_0;		/* YUV_DETC_SLOPE_YDIS_0 */
	__u32 sw_rawawb_b_ydis_0;		/* YUV_DETC_B_YDIS_0 */
	__u32 sw_rawawb_b_uv_1;			/* YUV_DETC_B_UV_1 */
	__u32 sw_rawawb_slope_vtcuv_1;		/* YUV_DETC_SLOPE_VTCUV_1 */
	__u32 sw_rawawb_inv_dslope_1;		/* YUV_DETC_INV_DSLOPE_1 */
	__u32 sw_rawawb_slope_ydis_1;		/* YUV_DETC_SLOPE_YDIS_1 */
	__u32 sw_rawawb_b_ydis_1;		/* YUV_DETC_B_YDIS_1 */
	__u32 sw_rawawb_b_uv_2;			/* YUV_DETC_B_UV_2 */
	__u32 sw_rawawb_slope_vtcuv_2;		/* YUV_DETC_SLOPE_VTCUV_2 */
	__u32 sw_rawawb_inv_dslope_2;		/* YUV_DETC_INV_DSLOPE_2 */
	__u32 sw_rawawb_slope_ydis_2;		/* YUV_DETC_SLOPE_YDIS_2 */
	__u32 sw_rawawb_b_ydis_2;		/* YUV_DETC_B_YDIS_2 */
	__u32 sw_rawawb_b_uv_3;			/* YUV_DETC_B_UV_3 */
	__u32 sw_rawawb_slope_vtcuv_3;		/* YUV_DETC_SLOPE_VTCUV_3 */
	__u32 sw_rawawb_inv_dslope_3;		/* YUV_DETC_INV_DSLOPE_3 */
	__u32 sw_rawawb_slope_ydis_3;		/* YUV_DETC_SLOPE_YDIS_3 */
	__u32 sw_rawawb_b_ydis_3;		/* YUV_DETC_B_YDIS_3 */
	__u32 sw_rawawb_ref_u;			/* YUV_DETC_REF_U */
	__u8 sw_rawawb_ref_v_3;			/* YUV_DETC_REF_V_1 */
	__u8 sw_rawawb_ref_v_2;			/* YUV_DETC_REF_V_1 */
	__u8 sw_rawawb_ref_v_1;			/* YUV_DETC_REF_V_1 */
	__u8 sw_rawawb_ref_v_0;			/* YUV_DETC_REF_V_1 */
	__u16 sw_rawawb_dis1_0;			/* YUV_DETC_DIS01_0 */
	__u16 sw_rawawb_dis0_0;			/* YUV_DETC_DIS01_0 */
	__u16 sw_rawawb_dis3_0;			/* YUV_DETC_DIS23_0 */
	__u16 sw_rawawb_dis2_0;			/* YUV_DETC_DIS23_0 */
	__u16 sw_rawawb_dis5_0;			/* YUV_DETC_DIS45_0 */
	__u16 sw_rawawb_dis4_0;			/* YUV_DETC_DIS45_0 */
	__u8 sw_rawawb_th3_0;			/* YUV_DETC_TH03_0 */
	__u8 sw_rawawb_th2_0;			/* YUV_DETC_TH03_0 */
	__u8 sw_rawawb_th1_0;			/* YUV_DETC_TH03_0 */
	__u8 sw_rawawb_th0_0;			/* YUV_DETC_TH03_0 */
	__u8 sw_rawawb_th5_0;			/* YUV_DETC_TH45_0 */
	__u8 sw_rawawb_th4_0;			/* YUV_DETC_TH45_0 */
	__u16 sw_rawawb_dis1_1;			/* YUV_DETC_DIS01_1 */
	__u16 sw_rawawb_dis0_1;			/* YUV_DETC_DIS01_1 */
	__u16 sw_rawawb_dis3_1;			/* YUV_DETC_DIS23_1 */
	__u16 sw_rawawb_dis2_1;			/* YUV_DETC_DIS23_1 */
	__u16 sw_rawawb_dis5_1;			/* YUV_DETC_DIS45_1 */
	__u16 sw_rawawb_dis4_1;			/* YUV_DETC_DIS45_1 */
	__u8 sw_rawawb_th3_1;			/* YUV_DETC_TH03_1 */
	__u8 sw_rawawb_th2_1;			/* YUV_DETC_TH03_1 */
	__u8 sw_rawawb_th1_1;			/* YUV_DETC_TH03_1 */
	__u8 sw_rawawb_th0_1;			/* YUV_DETC_TH03_1 */
	__u8 sw_rawawb_th5_1;			/* YUV_DETC_TH45_1 */
	__u8 sw_rawawb_th4_1;			/* YUV_DETC_TH45_1 */
	__u16 sw_rawawb_dis1_2;			/* YUV_DETC_DIS01_2 */
	__u16 sw_rawawb_dis0_2;			/* YUV_DETC_DIS01_2 */
	__u16 sw_rawawb_dis3_2;			/* YUV_DETC_DIS23_2 */
	__u16 sw_rawawb_dis2_2;			/* YUV_DETC_DIS23_2 */
	__u16 sw_rawawb_dis5_2;			/* YUV_DETC_DIS45_2 */
	__u16 sw_rawawb_dis4_2;			/* YUV_DETC_DIS45_2 */
	__u8 sw_rawawb_th3_2;			/* YUV_DETC_TH03_2 */
	__u8 sw_rawawb_th2_2;			/* YUV_DETC_TH03_2 */
	__u8 sw_rawawb_th1_2;			/* YUV_DETC_TH03_2 */
	__u8 sw_rawawb_th0_2;			/* YUV_DETC_TH03_2 */
	__u8 sw_rawawb_th5_2;			/* YUV_DETC_TH45_2 */
	__u8 sw_rawawb_th4_2;			/* YUV_DETC_TH45_2 */
	__u16 sw_rawawb_dis1_3;			/* YUV_DETC_DIS01_3 */
	__u16 sw_rawawb_dis0_3;			/* YUV_DETC_DIS01_3 */
	__u16 sw_rawawb_dis3_3;			/* YUV_DETC_DIS23_3 */
	__u16 sw_rawawb_dis2_3;			/* YUV_DETC_DIS23_3 */
	__u16 sw_rawawb_dis5_3;			/* YUV_DETC_DIS45_3 */
	__u16 sw_rawawb_dis4_3;			/* YUV_DETC_DIS45_3 */
	__u8 sw_rawawb_th3_3;			/* YUV_DETC_TH03_3 */
	__u8 sw_rawawb_th2_3;			/* YUV_DETC_TH03_3 */
	__u8 sw_rawawb_th1_3;			/* YUV_DETC_TH03_3 */
	__u8 sw_rawawb_th0_3;			/* YUV_DETC_TH03_3 */
	__u8 sw_rawawb_th5_3;			/* YUV_DETC_TH45_3 */
	__u8 sw_rawawb_th4_3;			/* YUV_DETC_TH45_3 */
	__u16 sw_rawawb_wt1;			/* RGB2XY_WT01 */
	__u16 sw_rawawb_wt0;			/* RGB2XY_WT01 */
	__u16 sw_rawawb_wt2;			/* RGB2XY_WT2 */
	__u16 sw_rawawb_mat0_y;			/* RGB2XY_MAT0_XY */
	__u16 sw_rawawb_mat0_x;			/* RGB2XY_MAT0_XY */
	__u16 sw_rawawb_mat1_y;			/* RGB2XY_MAT1_XY */
	__u16 sw_rawawb_mat1_x;			/* RGB2XY_MAT1_XY */
	__u16 sw_rawawb_mat2_y;			/* RGB2XY_MAT2_XY */
	__u16 sw_rawawb_mat2_x;			/* RGB2XY_MAT2_XY */
	__u16 sw_rawawb_nor_x1_0;		/* XY_DETC_NOR_X_0 */
	__u16 sw_rawawb_nor_x0_0;		/* XY_DETC_NOR_X_0 */
	__u16 sw_rawawb_nor_y1_0;		/* XY_DETC_NOR_Y_0 */
	__u16 sw_rawawb_nor_y0_0;		/* XY_DETC_NOR_Y_0 */
	__u16 sw_rawawb_big_x1_0;		/* XY_DETC_BIG_X_0 */
	__u16 sw_rawawb_big_x0_0;		/* XY_DETC_BIG_X_0 */
	__u16 sw_rawawb_big_y1_0;		/* XY_DETC_BIG_Y_0 */
	__u16 sw_rawawb_big_y0_0;		/* XY_DETC_BIG_Y_0 */
	__u16 sw_rawawb_sma_x1_0;		/* XY_DETC_SMA_X_0 */
	__u16 sw_rawawb_sma_x0_0;		/* XY_DETC_SMA_X_0 */
	__u16 sw_rawawb_sma_y1_0;		/* XY_DETC_SMA_Y_0 */
	__u16 sw_rawawb_sma_y0_0;		/* XY_DETC_SMA_Y_0 */
	__u16 sw_rawawb_nor_x1_1;		/* XY_DETC_NOR_X_1 */
	__u16 sw_rawawb_nor_x0_1;		/* XY_DETC_NOR_X_1 */
	__u16 sw_rawawb_nor_y1_1;		/* XY_DETC_NOR_Y_1 */
	__u16 sw_rawawb_nor_y0_1;		/* XY_DETC_NOR_Y_1 */
	__u16 sw_rawawb_big_x1_1;		/* XY_DETC_BIG_X_1 */
	__u16 sw_rawawb_big_x0_1;		/* XY_DETC_BIG_X_1 */
	__u16 sw_rawawb_big_y1_1;		/* XY_DETC_BIG_Y_1 */
	__u16 sw_rawawb_big_y0_1;		/* XY_DETC_BIG_Y_1 */
	__u16 sw_rawawb_sma_x1_1;		/* XY_DETC_SMA_X_1 */
	__u16 sw_rawawb_sma_x0_1;		/* XY_DETC_SMA_X_1 */
	__u16 sw_rawawb_sma_y1_1;		/* XY_DETC_SMA_Y_1 */
	__u16 sw_rawawb_sma_y0_1;		/* XY_DETC_SMA_Y_1 */
	__u16 sw_rawawb_nor_x1_2;		/* XY_DETC_NOR_X_2 */
	__u16 sw_rawawb_nor_x0_2;		/* XY_DETC_NOR_X_2 */
	__u16 sw_rawawb_nor_y1_2;		/* XY_DETC_NOR_Y_2 */
	__u16 sw_rawawb_nor_y0_2;		/* XY_DETC_NOR_Y_2 */
	__u16 sw_rawawb_big_x1_2;		/* XY_DETC_BIG_X_2 */
	__u16 sw_rawawb_big_x0_2;		/* XY_DETC_BIG_X_2 */
	__u16 sw_rawawb_big_y1_2;		/* XY_DETC_BIG_Y_2 */
	__u16 sw_rawawb_big_y0_2;		/* XY_DETC_BIG_Y_2 */
	__u16 sw_rawawb_sma_x1_2;		/* XY_DETC_SMA_X_2 */
	__u16 sw_rawawb_sma_x0_2;		/* XY_DETC_SMA_X_2 */
	__u16 sw_rawawb_sma_y1_2;		/* XY_DETC_SMA_Y_2 */
	__u16 sw_rawawb_sma_y0_2;		/* XY_DETC_SMA_Y_2 */
	__u16 sw_rawawb_nor_x1_3;		/* XY_DETC_NOR_X_3 */
	__u16 sw_rawawb_nor_x0_3;		/* XY_DETC_NOR_X_3 */
	__u16 sw_rawawb_nor_y1_3;		/* XY_DETC_NOR_Y_3 */
	__u16 sw_rawawb_nor_y0_3;		/* XY_DETC_NOR_Y_3 */
	__u16 sw_rawawb_big_x1_3;		/* XY_DETC_BIG_X_3 */
	__u16 sw_rawawb_big_x0_3;		/* XY_DETC_BIG_X_3 */
	__u16 sw_rawawb_big_y1_3;		/* XY_DETC_BIG_Y_3 */
	__u16 sw_rawawb_big_y0_3;		/* XY_DETC_BIG_Y_3 */
	__u16 sw_rawawb_sma_x1_3;		/* XY_DETC_SMA_X_3 */
	__u16 sw_rawawb_sma_x0_3;		/* XY_DETC_SMA_X_3 */
	__u16 sw_rawawb_sma_y1_3;		/* XY_DETC_SMA_Y_3 */
	__u16 sw_rawawb_sma_y0_3;		/* XY_DETC_SMA_Y_3 */
	__u16 sw_rawawb_nor_x1_4;		/* XY_DETC_NOR_X_4 */
	__u16 sw_rawawb_nor_x0_4;		/* XY_DETC_NOR_X_4 */
	__u16 sw_rawawb_nor_y1_4;		/* XY_DETC_NOR_Y_4 */
	__u16 sw_rawawb_nor_y0_4;		/* XY_DETC_NOR_Y_4 */
	__u16 sw_rawawb_big_x1_4;		/* XY_DETC_BIG_X_4 */
	__u16 sw_rawawb_big_x0_4;		/* XY_DETC_BIG_X_4 */
	__u16 sw_rawawb_big_y1_4;		/* XY_DETC_BIG_Y_4 */
	__u16 sw_rawawb_big_y0_4;		/* XY_DETC_BIG_Y_4 */
	__u16 sw_rawawb_sma_x1_4;		/* XY_DETC_SMA_X_4 */
	__u16 sw_rawawb_sma_x0_4;		/* XY_DETC_SMA_X_4 */
	__u16 sw_rawawb_sma_y1_4;		/* XY_DETC_SMA_Y_4 */
	__u16 sw_rawawb_sma_y0_4;		/* XY_DETC_SMA_Y_4 */
	__u16 sw_rawawb_nor_x1_5;		/* XY_DETC_NOR_X_5 */
	__u16 sw_rawawb_nor_x0_5;		/* XY_DETC_NOR_X_5 */
	__u16 sw_rawawb_nor_y1_5;		/* XY_DETC_NOR_Y_5 */
	__u16 sw_rawawb_nor_y0_5;		/* XY_DETC_NOR_Y_5 */
	__u16 sw_rawawb_big_x1_5;		/* XY_DETC_BIG_X_5 */
	__u16 sw_rawawb_big_x0_5;		/* XY_DETC_BIG_X_5 */
	__u16 sw_rawawb_big_y1_5;		/* XY_DETC_BIG_Y_5 */
	__u16 sw_rawawb_big_y0_5;		/* XY_DETC_BIG_Y_5 */
	__u16 sw_rawawb_sma_x1_5;		/* XY_DETC_SMA_X_5 */
	__u16 sw_rawawb_sma_x0_5;		/* XY_DETC_SMA_X_5 */
	__u16 sw_rawawb_sma_y1_5;		/* XY_DETC_SMA_Y_5 */
	__u16 sw_rawawb_sma_y0_5;		/* XY_DETC_SMA_Y_5 */
	__u16 sw_rawawb_nor_x1_6;		/* XY_DETC_NOR_X_6 */
	__u16 sw_rawawb_nor_x0_6;		/* XY_DETC_NOR_X_6 */
	__u16 sw_rawawb_nor_y1_6;		/* XY_DETC_NOR_Y_6 */
	__u16 sw_rawawb_nor_y0_6;		/* XY_DETC_NOR_Y_6 */
	__u16 sw_rawawb_big_x1_6;		/* XY_DETC_BIG_X_6 */
	__u16 sw_rawawb_big_x0_6;		/* XY_DETC_BIG_X_6 */
	__u16 sw_rawawb_big_y1_6;		/* XY_DETC_BIG_Y_6 */
	__u16 sw_rawawb_big_y0_6;		/* XY_DETC_BIG_Y_6 */
	__u16 sw_rawawb_sma_x1_6;		/* XY_DETC_SMA_X_6 */
	__u16 sw_rawawb_sma_x0_6;		/* XY_DETC_SMA_X_6 */
	__u16 sw_rawawb_sma_y1_6;		/* XY_DETC_SMA_Y_6 */
	__u16 sw_rawawb_sma_y0_6;		/* XY_DETC_SMA_Y_6 */
	__u8 sw_rawawb_multiwindow_en;		/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region6_domain;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region6_measen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region6_excen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region5_domain;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region5_measen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region5_excen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region4_domain;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region4_measen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region4_excen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region3_domain;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region3_measen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region3_excen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region2_domain;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region2_measen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region2_excen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region1_domain;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region1_measen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region1_excen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region0_domain;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region0_measen;	/* MULTIWINDOW_EXC_CTRL */
	__u8 sw_rawawb_exc_wp_region0_excen;	/* MULTIWINDOW_EXC_CTRL */
	__u16 sw_rawawb_multiwindow0_v_offs;	/* MULTIWINDOW0_OFFS */
	__u16 sw_rawawb_multiwindow0_h_offs;	/* MULTIWINDOW0_OFFS */
	__u16 sw_rawawb_multiwindow0_v_size;	/* MULTIWINDOW0_SIZE */
	__u16 sw_rawawb_multiwindow0_h_size;	/* MULTIWINDOW0_SIZE */
	__u16 sw_rawawb_multiwindow1_v_offs;	/* MULTIWINDOW1_OFFS */
	__u16 sw_rawawb_multiwindow1_h_offs;	/* MULTIWINDOW1_OFFS */
	__u16 sw_rawawb_multiwindow1_v_size;	/* MULTIWINDOW1_SIZE */
	__u16 sw_rawawb_multiwindow1_h_size;	/* MULTIWINDOW1_SIZE */
	__u16 sw_rawawb_multiwindow2_v_offs;	/* MULTIWINDOW2_OFFS */
	__u16 sw_rawawb_multiwindow2_h_offs;	/* MULTIWINDOW2_OFFS */
	__u16 sw_rawawb_multiwindow2_v_size;	/* MULTIWINDOW2_SIZE */
	__u16 sw_rawawb_multiwindow2_h_size;	/* MULTIWINDOW2_SIZE */
	__u16 sw_rawawb_multiwindow3_v_offs;	/* MULTIWINDOW3_OFFS */
	__u16 sw_rawawb_multiwindow3_h_offs;	/* MULTIWINDOW3_OFFS */
	__u16 sw_rawawb_multiwindow3_v_size;	/* MULTIWINDOW3_SIZE */
	__u16 sw_rawawb_multiwindow3_h_size;	/* MULTIWINDOW3_SIZE */
	__u16 sw_rawawb_multiwindow4_v_offs;	/* MULTIWINDOW4_OFFS */
	__u16 sw_rawawb_multiwindow4_h_offs;	/* MULTIWINDOW4_OFFS */
	__u16 sw_rawawb_multiwindow4_v_size;	/* MULTIWINDOW4_SIZE */
	__u16 sw_rawawb_multiwindow4_h_size;	/* MULTIWINDOW4_SIZE */
	__u16 sw_rawawb_multiwindow5_v_offs;	/* MULTIWINDOW5_OFFS */
	__u16 sw_rawawb_multiwindow5_h_offs;	/* MULTIWINDOW5_OFFS */
	__u16 sw_rawawb_multiwindow5_v_size;	/* MULTIWINDOW5_SIZE */
	__u16 sw_rawawb_multiwindow5_h_size;	/* MULTIWINDOW5_SIZE */
	__u16 sw_rawawb_multiwindow6_v_offs;	/* MULTIWINDOW6_OFFS */
	__u16 sw_rawawb_multiwindow6_h_offs;	/* MULTIWINDOW6_OFFS */
	__u16 sw_rawawb_multiwindow6_v_size;	/* MULTIWINDOW6_SIZE */
	__u16 sw_rawawb_multiwindow6_h_size;	/* MULTIWINDOW6_SIZE */
	__u16 sw_rawawb_multiwindow7_v_offs;	/* MULTIWINDOW7_OFFS */
	__u16 sw_rawawb_multiwindow7_h_offs;	/* MULTIWINDOW7_OFFS */
	__u16 sw_rawawb_multiwindow7_v_size;	/* MULTIWINDOW7_SIZE */
	__u16 sw_rawawb_multiwindow7_h_size;	/* MULTIWINDOW7_SIZE */
	__u16 sw_rawawb_exc_wp_region0_xu1;	/* EXC_WP_REGION0_XU */
	__u16 sw_rawawb_exc_wp_region0_xu0;	/* EXC_WP_REGION0_XU */
	__u16 sw_rawawb_exc_wp_region0_yv1;	/* EXC_WP_REGION0_YV */
	__u16 sw_rawawb_exc_wp_region0_yv0;	/* EXC_WP_REGION0_YV */
	__u16 sw_rawawb_exc_wp_region1_xu1;	/* EXC_WP_REGION1_XU */
	__u16 sw_rawawb_exc_wp_region1_xu0;	/* EXC_WP_REGION1_XU */
	__u16 sw_rawawb_exc_wp_region1_yv1;	/* EXC_WP_REGION1_YV */
	__u16 sw_rawawb_exc_wp_region1_yv0;	/* EXC_WP_REGION1_YV */
	__u16 sw_rawawb_exc_wp_region2_xu1;	/* EXC_WP_REGION2_XU */
	__u16 sw_rawawb_exc_wp_region2_xu0;	/* EXC_WP_REGION2_XU */
	__u16 sw_rawawb_exc_wp_region2_yv1;	/* EXC_WP_REGION2_YV */
	__u16 sw_rawawb_exc_wp_region2_yv0;	/* EXC_WP_REGION2_YV */
	__u16 sw_rawawb_exc_wp_region3_xu1;	/* EXC_WP_REGION3_XU */
	__u16 sw_rawawb_exc_wp_region3_xu0;	/* EXC_WP_REGION3_XU */
	__u16 sw_rawawb_exc_wp_region3_yv1;	/* EXC_WP_REGION3_YV */
	__u16 sw_rawawb_exc_wp_region3_yv0;	/* EXC_WP_REGION3_YV */
	__u16 sw_rawawb_exc_wp_region4_xu1;	/* EXC_WP_REGION4_XU */
	__u16 sw_rawawb_exc_wp_region4_xu0;	/* EXC_WP_REGION4_XU */
	__u16 sw_rawawb_exc_wp_region4_yv1;	/* EXC_WP_REGION4_YV */
	__u16 sw_rawawb_exc_wp_region4_yv0;	/* EXC_WP_REGION4_YV */
	__u16 sw_rawawb_exc_wp_region5_xu1;	/* EXC_WP_REGION5_XU */
	__u16 sw_rawawb_exc_wp_region5_xu0;	/* EXC_WP_REGION5_XU */
	__u16 sw_rawawb_exc_wp_region5_yv1;	/* EXC_WP_REGION5_YV */
	__u16 sw_rawawb_exc_wp_region5_yv0;	/* EXC_WP_REGION5_YV */
	__u16 sw_rawawb_exc_wp_region6_xu1;	/* EXC_WP_REGION6_XU */
	__u16 sw_rawawb_exc_wp_region6_xu0;	/* EXC_WP_REGION6_XU */
	__u16 sw_rawawb_exc_wp_region6_yv1;	/* EXC_WP_REGION6_YV */
	__u16 sw_rawawb_exc_wp_region6_yv0;	/* EXC_WP_REGION6_YV */
} __attribute__ ((packed));

struct isp2x_rawaebig_meas_cfg {
	__u8 rawae_sel;
	__u8 wnd_num;
	__u8 subwin_en[ISP2X_RAWAEBIG_SUBWIN_NUM];
	struct isp2x_window win;
	struct isp2x_window subwin[ISP2X_RAWAEBIG_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaelite_meas_cfg {
	__u8 rawae_sel;
	__u8 wnd_num;
	struct isp2x_window win;
} __attribute__ ((packed));

struct isp2x_yuvae_meas_cfg {
	__u8 ysel;
	__u8 wnd_num;
	__u8 subwin_en[ISP2X_YUVAE_SUBWIN_NUM];
	struct isp2x_window win;
	struct isp2x_window subwin[ISP2X_YUVAE_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaf_meas_cfg {
	__u8 rawaf_sel;
	__u8 num_afm_win;
	__u8 gaus_en;
	__u8 gamma_en;
	struct isp2x_window win[ISP2X_RAWAF_WIN_NUM];
	__u8 line_en[ISP2X_RAWAF_LINE_NUM];
	__u8 line_num[ISP2X_RAWAF_LINE_NUM];
	__u8 gaus_coe_h2;
	__u8 gaus_coe_h1;
	__u8 gaus_coe_h0;
	__u16 afm_thres;
	__u8 lum_var_shift[ISP2X_RAWAF_WIN_NUM];
	__u8 afm_var_shift[ISP2X_RAWAF_WIN_NUM];
	__u16 gamma_y[ISP2X_RAWAF_GAMMA_NUM];
} __attribute__ ((packed));

struct isp2x_siaf_win_cfg {
	__u8 sum_shift;
	__u8 lum_shift;
	struct isp2x_window win;
} __attribute__ ((packed));

struct isp2x_siaf_cfg {
	__u8 num_afm_win;
	__u32 thres;
	struct isp2x_siaf_win_cfg afm_win[ISP2X_AFM_MAX_WINDOWS];
} __attribute__ ((packed));

struct isp2x_rawhistbig_cfg {
	__u8 wnd_num;
	__u8 data_sel;
	__u8 waterline;
	__u8 mode;
	__u8 stepsize;
	__u8 off;
	__u8 bcc;
	__u8 gcc;
	__u8 rcc;
	struct isp2x_window win;
	__u8 weight[ISP2X_RAWHISTBIG_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_rawhistlite_cfg {
	__u8 data_sel;
	__u8 waterline;
	__u8 mode;
	__u8 stepsize;
	__u8 off;
	__u8 bcc;
	__u8 gcc;
	__u8 rcc;
	struct isp2x_window win;
	__u8 weight[ISP2X_RAWHISTLITE_SUBWIN_NUM];
} __attribute__ ((packed));

struct isp2x_sihst_win_cfg {
	__u8 data_sel;
	__u8 waterline;
	__u8 auto_stop;
	__u8 mode;
	__u8 stepsize;
	struct isp2x_window win;
} __attribute__ ((packed));

struct isp2x_sihst_cfg {
	__u8 wnd_num;
	struct isp2x_sihst_win_cfg win_cfg[ISP2X_SIHIST_WIN_NUM];
	__u8 hist_weight[ISP2X_HIST_WEIGHT_NUM];
} __attribute__ ((packed));

struct isp2x_isp_other_cfg {
	struct isp2x_bls_cfg bls_cfg;
	struct isp2x_dpcc_cfg dpcc_cfg;
	struct isp2x_hdrmge_cfg hdrmge_cfg;
	struct isp2x_rawnr_cfg rawnr_cfg;
	struct isp2x_lsc_cfg lsc_cfg;
	struct isp2x_awb_gain_cfg awb_gain_cfg;
	/* struct isp2x_goc_cfg goc_cfg; */
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
	__u32 fine_integration_time;
	__u32 coarse_integration_time;
	__u32 analog_gain_code_global;
	__u32 digital_gain_global;
	__u32 isp_digital_gain;
} __attribute__ ((packed));

struct sensor_exposure_cfg {
	struct sensor_exposure_s linear_exp;
	struct sensor_exposure_s hdr_exp[3];
} __attribute__ ((packed));

struct isp2x_isp_params_cfg {
	__u64 module_en_update;
	__u64 module_ens;
	__u64 module_cfg_update;

	__u32 frame_id;
	struct isp2x_isp_meas_cfg meas;
	struct isp2x_isp_other_cfg others;
	struct sensor_exposure_cfg exposure;
} __attribute__ ((packed));

struct isp2x_siawb_meas {
	__u32 cnt;
	__u8 mean_y_or_g;
	__u8 mean_cb_or_b;
	__u8 mean_cr_or_r;
} __attribute__ ((packed));

struct isp2x_siawb_stat {
	struct isp2x_siawb_meas awb_mean[ISP2X_AWB_MAX_GRID];
} __attribute__ ((packed));

struct isp2x_rawawb_ramdata {
	__u32 wp;
	__u32 r;
	__u32 g;
	__u32 b;
};

struct isp2x_rawawb_meas_stat {
	__u32 ro_rawawb_sum_r_nor[ISP2X_RAWAWB_SUM_NUM];	/* SUM_R_NOR_0 */
	__u32 ro_rawawb_sum_g_nor[ISP2X_RAWAWB_SUM_NUM];	/* SUM_G_NOR_0 */
	__u32 ro_rawawb_sum_b_nor[ISP2X_RAWAWB_SUM_NUM];	/* SUM_B_NOR_0 */
	__u32 ro_rawawb_wp_num_nor[ISP2X_RAWAWB_SUM_NUM];	/* WP_NUM_NOR_0 */
	__u32 ro_rawawb_sum_r_big[ISP2X_RAWAWB_SUM_NUM];	/* SUM_R_BIG_0 */
	__u32 ro_rawawb_sum_g_big[ISP2X_RAWAWB_SUM_NUM];	/* SUM_G_BIG_0 */
	__u32 ro_rawawb_sum_b_big[ISP2X_RAWAWB_SUM_NUM];	/* SUM_B_BIG_0 */
	__u32 ro_rawawb_wp_num_big[ISP2X_RAWAWB_SUM_NUM];	/* WP_NUM_BIG_0 */
	__u32 ro_rawawb_sum_r_sma[ISP2X_RAWAWB_SUM_NUM];	/* SUM_R_SMA_0 */
	__u32 ro_rawawb_sum_g_sma[ISP2X_RAWAWB_SUM_NUM];	/* SUM_G_SMA_0 */
	__u32 ro_rawawb_sum_b_sma[ISP2X_RAWAWB_SUM_NUM];	/* SUM_B_SMA_0 */
	__u32 ro_rawawb_wp_num_sma[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_sum_r_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_R_NOR_MULTIWINDOW_0 */
	__u32 ro_sum_g_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_G_NOR_MULTIWINDOW_0 */
	__u32 ro_sum_b_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_B_NOR_MULTIWINDOW_0 */
	__u32 ro_wp_nm_nor_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* WP_NM_NOR_MULTIWINDOW_0 */
	__u32 ro_sum_r_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_R_BIG_MULTIWINDOW_0 */
	__u32 ro_sum_g_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_G_BIG_MULTIWINDOW_0 */
	__u32 ro_sum_b_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_B_BIG_MULTIWINDOW_0 */
	__u32 ro_wp_nm_big_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* WP_NM_BIG_MULTIWINDOW_0 */
	__u32 ro_sum_r_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_R_SMA_MULTIWINDOW_0 */
	__u32 ro_sum_g_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_G_SMA_MULTIWINDOW_0 */
	__u32 ro_sum_b_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* SUM_B_SMA_MULTIWINDOW_0 */
	__u32 ro_wp_nm_sma_multiwindow[ISP2X_RAWAWB_MULWD_NUM];	/* WP_NM_SMA_MULTIWINDOW_0 */
	__u32 ro_sum_r_exc[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_sum_g_exc[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_sum_b_exc[ISP2X_RAWAWB_SUM_NUM];
	__u32 ro_wp_nm_exc[ISP2X_RAWAWB_SUM_NUM];
	struct isp2x_rawawb_ramdata ramdata[ISP2X_RAWAWB_RAMDATA_NUM];
} __attribute__ ((packed));

struct isp2x_rawae_meas_data {
	__u16 channelr_xy;
	__u16 channelb_xy;
	__u16 channelg_xy;
};

struct isp2x_rawaebig_stat {
	__u32 sumr[ISP2X_RAWAEBIG_SUBWIN_NUM];
	__u32 sumg[ISP2X_RAWAEBIG_SUBWIN_NUM];
	__u32 sumb[ISP2X_RAWAEBIG_SUBWIN_NUM];
	struct isp2x_rawae_meas_data data[ISP2X_RAWAEBIG_MEAN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaelite_stat {
	struct isp2x_rawae_meas_data data[ISP2X_RAWAELITE_MEAN_NUM];
} __attribute__ ((packed));

struct isp2x_yuvae_stat {
	__u32 ro_yuvae_sumy[ISP2X_YUVAE_SUBWIN_NUM];
	__u8 mean[ISP2X_YUVAE_MEAN_NUM];
} __attribute__ ((packed));

struct isp2x_rawaf_stat {
	__u32 int_state;
	__u32 afm_sum[ISP2X_RAWAF_WIN_NUM];
	__u32 afm_lum[ISP2X_RAWAF_WIN_NUM];
	__u32 ramdata[ISP2X_RAWAF_SUMDATA_NUM];
} __attribute__ ((packed));

struct isp2x_siaf_meas_val {
	__u32 sum;
	__u32 lum;
} __attribute__ ((packed));

struct isp2x_siaf_stat {
	struct isp2x_siaf_meas_val win[ISP2X_AFM_MAX_WINDOWS];
} __attribute__ ((packed));

struct isp2x_rawhistbig_stat {
	__u32 hist_bin[ISP2X_HIST_BIN_N_MAX];
} __attribute__ ((packed));

struct isp2x_rawhistlite_stat {
	__u32 hist_bin[ISP2X_HIST_BIN_N_MAX];
} __attribute__ ((packed));

struct isp2x_sihst_win_stat {
	__u32 hist_bins[ISP2X_SIHIST_BIN_N_MAX];
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

enum {
	RKISP_RTT_MODE_NORMAL = 0,
	RKISP_RTT_MODE_MULTI_FRAME,
	RKISP_RTT_MODE_ONE_FRAME,
};

/**
 * struct rkisp_thunderboot_resmem_head
 */
struct rkisp_thunderboot_resmem_head {
	__u16 enable;
	__u16 complete;
	__u16 frm_total;
	__u16 hdr_mode;
	__u16 rtt_mode;
	__u16 width;
	__u16 height;
	__u16 camera_num;
	__u16 camera_index;
	__u16 md_flag;

	__u32 exp_time[3];
	__u32 exp_gain[3];
	__u32 exp_time_reg[3];
	__u32 exp_gain_reg[3];
} __attribute__ ((packed));

/**
 * struct rkisp_thunderboot_resmem - shared buffer for thunderboot with risc-v side
 */
struct rkisp_thunderboot_resmem {
	__u32 resmem_padr;
	__u32 resmem_size;
} __attribute__ ((packed));

/**
 * struct rkisp_thunderboot_shmem
 */
struct rkisp_thunderboot_shmem {
	__u32 shm_start;
	__u32 shm_size;
	__s32 shm_fd;
} __attribute__ ((packed));

#endif /* _UAPI_RK_ISP2_CONFIG_H */
