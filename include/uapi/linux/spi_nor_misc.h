/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI__SPI_NOR_MISC_H__
#define _UAPI__SPI_NOR_MISC_H__

#include <linux/types.h>

#define SPI_NOR_MAX_ID_LEN	6

struct nor_flash_user_info {
	__u8	id[SPI_NOR_MAX_ID_LEN];
};

#define NOR_BASE	'P'
#define NOR_GET_FLASH_INFO		_IOR(NOR_BASE, 0, struct nor_flash_user_info)

#endif
