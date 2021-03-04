/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Rockchip module information
 * Copyright (C) 2018-2019 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKMODULE_CAMERA_H
#define _UAPI_RKMODULE_CAMERA_H

#include <linux/types.h>
#include <linux/rk-video-format.h>

#define RKMODULE_API_VERSION		KERNEL_VERSION(0, 1, 0x2)

/* using for rv1109 and rv1126 */
#define RKMODULE_EXTEND_LINE		24

#define RKMODULE_NAME_LEN		32
#define RKMODULE_LSCDATA_LEN		441

#define RKMODULE_MAX_VC_CH		4

#define RKMODULE_CAMERA_MODULE_INDEX	"rockchip,camera-module-index"
#define RKMODULE_CAMERA_MODULE_FACING	"rockchip,camera-module-facing"
#define RKMODULE_CAMERA_MODULE_NAME	"rockchip,camera-module-name"
#define RKMODULE_CAMERA_LENS_NAME	"rockchip,camera-module-lens-name"

/* BT.656 & BT.1120 multi channel
 * On which channels it can send video data
 * related with struct rkmodule_bt656_mbus_info
 */
#define RKMODULE_CAMERA_BT656_ID_EN_BITS_1		(0x1)
#define RKMODULE_CAMERA_BT656_ID_EN_BITS_2		(0x3)
#define RKMODULE_CAMERA_BT656_ID_EN_BITS_3		(0x7)
#define RKMODULE_CAMERA_BT656_ID_EN_BITS_4		(0xf)
#define RKMODULE_CAMERA_BT656_PARSE_ID_LSB		BIT(0)
#define RKMODULE_CAMERA_BT656_PARSE_ID_MSB		BIT(1)
#define RKMODULE_CAMERA_BT656_CHANNEL_0			BIT(2)
#define RKMODULE_CAMERA_BT656_CHANNEL_1			BIT(3)
#define RKMODULE_CAMERA_BT656_CHANNEL_2			BIT(4)
#define RKMODULE_CAMERA_BT656_CHANNEL_3			BIT(5)
#define RKMODULE_CAMERA_BT656_CHANNELS			(RKMODULE_CAMERA_BT656_CHANNEL_0 | \
							 RKMODULE_CAMERA_BT656_CHANNEL_1 | \
							 RKMODULE_CAMERA_BT656_CHANNEL_2 | \
							 RKMODULE_CAMERA_BT656_CHANNEL_3)

#define RKMODULE_GET_MODULE_INFO	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, struct rkmodule_inf)

#define RKMODULE_AWB_CFG	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, struct rkmodule_awb_cfg)

#define RKMODULE_AF_CFG	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 2, struct rkmodule_af_cfg)

#define RKMODULE_LSC_CFG	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 3, struct rkmodule_lsc_cfg)

#define RKMODULE_GET_HDR_CFG	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 4, struct rkmodule_hdr_cfg)

#define RKMODULE_SET_HDR_CFG	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 5, struct rkmodule_hdr_cfg)

#define RKMODULE_SET_CONVERSION_GAIN	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 6, __u32)

#define RKMODULE_GET_LVDS_CFG	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 7, struct rkmodule_lvds_cfg)

#define RKMODULE_SET_DPCC_CFG	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 8, struct rkmodule_dpcc_cfg)

#define RKMODULE_GET_NR_SWITCH_THRESHOLD	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 9, struct rkmodule_nr_switch_threshold)

#define RKMODULE_SET_QUICK_STREAM	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 10, __u32)

#define RKMODULE_GET_BT656_INTF_TYPE	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 11, __u32)

#define RKMODULE_GET_VC_FMT_INFO \
    _IOR('V', BASE_VIDIOC_PRIVATE + 12, struct rkmodule_vc_fmt_info)

#define RKMODULE_GET_VC_HOTPLUG_INFO \
    _IOR('V', BASE_VIDIOC_PRIVATE + 13, struct rkmodule_vc_hotplug_info)

#define RKMODULE_GET_START_STREAM_SEQ	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 14, __u32)

#define RKMODULE_GET_VICAP_RST_INFO	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 15, struct rkmodule_vicap_reset_info)

#define RKMODULE_SET_VICAP_RST_INFO	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 16, struct rkmodule_vicap_reset_info)

#define RKMODULE_GET_BT656_MBUS_INFO	\
	_IOR('V', BASE_VIDIOC_PRIVATE + 17, struct rkmodule_bt656_mbus_info)
/**
 * struct rkmodule_base_inf - module base information
 *
 */
struct rkmodule_base_inf {
	char sensor[RKMODULE_NAME_LEN];
	char module[RKMODULE_NAME_LEN];
	char lens[RKMODULE_NAME_LEN];
} __attribute__ ((packed));

/**
 * struct rkmodule_fac_inf - module factory information
 *
 */
struct rkmodule_fac_inf {
	__u32 flag;

	char module[RKMODULE_NAME_LEN];
	char lens[RKMODULE_NAME_LEN];
	__u32 year;
	__u32 month;
	__u32 day;
} __attribute__ ((packed));

/**
 * struct rkmodule_awb_inf - module awb information
 *
 */
struct rkmodule_awb_inf {
	__u32 flag;

	__u32 r_value;
	__u32 b_value;
	__u32 gr_value;
	__u32 gb_value;

	__u32 golden_r_value;
	__u32 golden_b_value;
	__u32 golden_gr_value;
	__u32 golden_gb_value;
} __attribute__ ((packed));

/**
 * struct rkmodule_lsc_inf - module lsc information
 *
 */
struct rkmodule_lsc_inf {
	__u32 flag;

	__u16 lsc_w;
	__u16 lsc_h;
	__u16 decimal_bits;

	__u16 lsc_r[RKMODULE_LSCDATA_LEN];
	__u16 lsc_b[RKMODULE_LSCDATA_LEN];
	__u16 lsc_gr[RKMODULE_LSCDATA_LEN];
	__u16 lsc_gb[RKMODULE_LSCDATA_LEN];
} __attribute__ ((packed));

/**
 * struct rkmodule_af_inf - module af information
 *
 */
struct rkmodule_af_inf {
	__u32 flag;

	__u32 vcm_start;
	__u32 vcm_end;
	__u32 vcm_dir;
} __attribute__ ((packed));

/**
 * struct rkmodule_inf - module information
 *
 */
struct rkmodule_inf {
	struct rkmodule_base_inf base;
	struct rkmodule_fac_inf fac;
	struct rkmodule_awb_inf awb;
	struct rkmodule_lsc_inf lsc;
	struct rkmodule_af_inf af;
} __attribute__ ((packed));

/**
 * struct rkmodule_awb_inf - module awb information
 *
 */
struct rkmodule_awb_cfg {
	__u32 enable;
	__u32 golden_r_value;
	__u32 golden_b_value;
	__u32 golden_gr_value;
	__u32 golden_gb_value;
} __attribute__ ((packed));

/**
 * struct rkmodule_af_cfg
 *
 */
struct rkmodule_af_cfg {
	__u32 enable;
	__u32 vcm_start;
	__u32 vcm_end;
	__u32 vcm_dir;
} __attribute__ ((packed));

/**
 * struct rkmodule_lsc_cfg
 *
 */
struct rkmodule_lsc_cfg {
	__u32 enable;
} __attribute__ ((packed));

/**
 * NO_HDR: linear mode
 * HDR_X2: hdr two frame or line mode
 * HDR_X3: hdr three or line mode
 */
enum rkmodule_hdr_mode {
	NO_HDR = 0,
	HDR_X2 = 5,
	HDR_X3 = 6,
};

/**
 * HDR_NORMAL_VC: hdr frame with diff virtual channels
 * HDR_LINE_CNT: hdr frame with line counter
 * HDR_ID_CODE: hdr frame with identification code
 */
enum hdr_esp_mode {
	HDR_NORMAL_VC = 0,
	HDR_LINE_CNT,
	HDR_ID_CODE,
};

/**
 * lcnt: line counter
 *     padnum: the pixels of padding row
 *     padpix: the payload of padding
 * idcd: identification code
 *     efpix: identification code of Effective line
 *     obpix: identification code of OB line
 */
struct rkmodule_hdr_esp {
	enum hdr_esp_mode mode;
	union {
		struct {
			__u32 padnum;
			__u32 padpix;
		} lcnt;
		struct {
			__u32 efpix;
			__u32 obpix;
		} idcd;
	} val;
};

struct rkmodule_hdr_cfg {
	__u32 hdr_mode;
	struct rkmodule_hdr_esp esp;
} __attribute__ ((packed));

/* sensor lvds sync code
 * sav: start of active video codes
 * eav: end of active video codes
 */
struct rkmodule_sync_code {
	u16 sav;
	u16 eav;
};

/* sensor lvds difference sync code mode
 * LS_FIRST: valid line ls-le or sav-eav
 *	   invalid line fs-fe or sav-eav
 * FS_FIRST: valid line fs-le
 *	   invalid line ls-fe
 * ls: line start
 * le: line end
 * fs: frame start
 * fe: frame end
 * SONY_DOL_HDR_1: sony dol hdr pattern 1
 * SONY_DOL_HDR_2: sony dol hdr pattern 2
 */
enum rkmodule_lvds_mode {
	LS_FIRST = 0,
	FS_FIRST,
	SONY_DOL_HDR_1,
	SONY_DOL_HDR_2
};

/* sync code of different frame type (hdr or linear) for lvds
 * act: valid line sync code
 * blk: invalid line sync code
 */
struct rkmodule_lvds_frm_sync_code {
	struct rkmodule_sync_code act;
	struct rkmodule_sync_code blk;
};

/* sync code for lvds of sensor
 * odd_sync_code: sync code of odd frame id for lvds of sony sensor
 * even_sync_code: sync code of even frame id for lvds of sony sensor
 */
struct rkmodule_lvds_frame_sync_code {
	struct rkmodule_lvds_frm_sync_code odd_sync_code;
	struct rkmodule_lvds_frm_sync_code even_sync_code;
};

/* lvds sync code category of sensor for different operation */
enum rkmodule_lvds_sync_code_group {
	LVDS_CODE_GRP_LINEAR = 0x0,
	LVDS_CODE_GRP_LONG,
	LVDS_CODE_GRP_MEDIUM,
	LVDS_CODE_GRP_SHORT,
	LVDS_CODE_GRP_MAX
};

/* struct rkmodule_lvds_cfg
 * frm_sync_code[index]:
 *  index == LVDS_CODE_GRP_LONG:
 *    sync code for frame of linear mode or for long frame of hdr mode
 *  index == LVDS_CODE_GRP_MEDIUM:
 *    sync code for medium long frame of hdr mode
 *  index == LVDS_CODE_GRP_SHOR:
 *    sync code for short long frame of hdr mode
 */
struct rkmodule_lvds_cfg {
	enum rkmodule_lvds_mode mode;
	struct rkmodule_lvds_frame_sync_code frm_sync_code[LVDS_CODE_GRP_MAX];
} __attribute__ ((packed));

/**
 * struct rkmodule_dpcc_cfg
 * enable: 0 -> disable dpcc, 1 -> enable multiple,
 *         2 -> enable single, 3 -> enable all;
 * cur_single_dpcc: the strength of single dpcc;
 * cur_multiple_dpcc: the strength of multiple dpcc;
 * total_dpcc: the max strength;
 */
struct rkmodule_dpcc_cfg {
	__u32 enable;
	__u32 cur_single_dpcc;
	__u32 cur_multiple_dpcc;
	__u32 total_dpcc;
} __attribute__ ((packed));

/**
 * nr switch by gain
 * direct: 0 -> up_thres LSNR to HSNR, 1 -> up_thres HSNR to LSNR
 * up_thres: threshold of nr change from low gain to high gain
 * down_thres: threshold of nr change from high gain to low gain;
 * div_coeff: Coefficients converted from float to int
 */
struct rkmodule_nr_switch_threshold {
	__u32 direct;
	__u32 up_thres;
	__u32 down_thres;
	__u32 div_coeff;
} __attribute__ ((packed));

/**
 * enum rkmodule_bt656_intf_type
 * to support sony bt656 raw
 */
enum rkmodule_bt656_intf_type {
	BT656_STD_RAW = 0,
	BT656_SONY_RAW,
};

/**
 * struct rkmodule_vc_fmt_info - virtual channels fmt info
 *
 */
struct rkmodule_vc_fmt_info {
	__u32 width[RKMODULE_MAX_VC_CH];
	__u32 height[RKMODULE_MAX_VC_CH];
	__u32 fps[RKMODULE_MAX_VC_CH];
} __attribute__ ((packed));

/**
 * struct rkmodule_vc_hotplug_info - virtual channels hotplug status info
 * detect_status: hotplug status
 *     bit 0~3 means channels id, value : 0 -> plug out, 1 -> plug in.
 */
struct rkmodule_vc_hotplug_info {
	__u8 detect_status;
} __attribute__ ((packed));


/* sensor start stream sequence
 * RKMODULE_START_STREAM_DEFAULT: by default
 * RKMODULE_START_STREAM_BEHIND : sensor start stream should be behind the controller
 * RKMODULE_START_STREAM_FRONT  : sensor start stream should be in front of the controller
 */
enum rkmodule_start_stream_seq {
	RKMODULE_START_STREAM_DEFAULT = 0,
	RKMODULE_START_STREAM_BEHIND,
	RKMODULE_START_STREAM_FRONT,
};

/*
 * the causation to do cif reset work
 */
enum rkmodule_reset_src {
	RKCIF_RESET_SRC_NON = 0x0,
	RKCIF_RESET_SRC_ERR_CSI2,
	RKCIF_RESET_SRC_ERR_LVDS,
	RKICF_RESET_SRC_ERR_CUTOFF,
	RKCIF_RESET_SRC_ERR_HOTPLUG,
	RKCIF_RESET_SRC_ERR_APP,
};

struct rkmodule_vicap_reset_info {
	__u32 is_reset;
	enum rkmodule_reset_src src;
} __attribute__ ((packed));

struct rkmodule_bt656_mbus_info {
	__u32 flags;
	__u32 id_en_bits;
} __attribute__ ((packed));


#endif /* _UAPI_RKMODULE_CAMERA_H */
