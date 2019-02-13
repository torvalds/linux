/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Rockchip preisp driver
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKPREISP_H
#define _UAPI_RKPREISP_H

#include <linux/types.h>

#define PREISP_FW_NAME_LEN		128

#define PREISP_LSCTBL_SIZE		289

#define PREISP_CMD_SET_HDRAE_EXP	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct preisp_hdrae_exp_s)

#define PREISP_CMD_SAVE_HDRAE_PARAM	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, struct preisp_hdrae_para_s)

#define PREISP_POWER_ON		_IO('p',   1)
#define PREISP_POWER_OFF	_IO('p',   2)
#define PREISP_REQUEST_SLEEP	_IOW('p',  3, s32)
#define PREISP_WAKEUP		_IO('p',   4)
#define PREISP_DOWNLOAD_FW	_IOW('p',  5, char[PREISP_FW_NAME_LEN])
#define PREISP_WRITE		_IOW('p',  6, struct preisp_apb_pkt)
#define PREISP_READ		_IOR('p',  7, struct preisp_apb_pkt)
#define PREISP_ST_QUERY		_IOR('p',  8, s32)
#define PREISP_IRQ_REQUEST	_IOW('p',  9, s32)
#define PREISP_SEND_MSG		_IOW('p', 11, s32)
#define PREISP_QUERY_MSG	_IOR('p', 12, s32)
#define PREISP_RECV_MSG		_IOR('p', 13, s32)
#define PREISP_CLIENT_CONNECT	_IOW('p', 15, s32)
#define PREISP_CLIENT_DISCONNECT _IO('p', 16)

struct preisp_apb_pkt {
	s32 data_len;
	s32 addr;
	s32 *data;
};

/**
 * struct preisp_hdrae_para_s - awb and lsc para for preisp
 *
 * @r_gain: awb r gain
 * @b_gain: awb b gain
 * @gr_gain: awb gr gain
 * @gb_gain: awb gb gain
 * @lsc_table: lsc data of gr
 */
struct preisp_hdrae_para_s {
	unsigned short r_gain;
	unsigned short b_gain;
	unsigned short gr_gain;
	unsigned short gb_gain;
	int lsc_table[PREISP_LSCTBL_SIZE];
};

/**
 * struct preisp_hdrae_exp_s - hdrae exposure
 *
 */
struct preisp_hdrae_exp_s {
	unsigned int long_exp_reg;
	unsigned int long_gain_reg;
	unsigned int middle_exp_reg;
	unsigned int middle_gain_reg;
	unsigned int short_exp_reg;
	unsigned int short_gain_reg;
	unsigned int long_exp_val;
	unsigned int long_gain_val;
	unsigned int middle_exp_val;
	unsigned int middle_gain_val;
	unsigned int short_exp_val;
	unsigned int short_gain_val;
};

#endif /* _UAPI_RKPREISP_H */
