/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Rockchip preisp driver
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RKPREISP_H
#define _UAPI_RKPREISP_H

#include <linux/types.h>

#define PREISP_LSCTBL_SIZE		289

#define PREISP_CMD_SET_HDRAE_EXP	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 0, struct preisp_hdrae_exp_s)

#define PREISP_CMD_SAVE_HDRAE_PARAM	\
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, struct preisp_hdrae_para_s)

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
