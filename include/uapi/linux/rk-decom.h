/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT) */
/*
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 */

#ifndef _UAPI_RK_DECOM_H
#define _UAPI_RK_DECOM_H

#include <linux/types.h>
#include <linux/version.h>

#define RK_DECOM_NAME		"rk_decom"

enum rk_decom_mod {
	RK_LZ4_MOD,
	RK_GZIP_MOD,
	RK_ZLIB_MOD,
	RK_DECOM_MOD_MAX,
};

/* input of RK_DECOM_USER */
struct rk_decom_param {
	__u32 mode;
	__u32 dst_max_size;
	__s32 src_fd;
	__s32 dst_fd;
	__u64 decom_data_len;
};

#define  RK_DECOM_MAGIC		'D'
#define  RK_DECOM_USER		_IOWR(RK_DECOM_MAGIC, 101, struct rk_decom_param)

#endif
