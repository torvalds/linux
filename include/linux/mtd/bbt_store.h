/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 */

#ifndef __BBT_STORE_H
#define __BBT_STORE_H

#include <linux/mtd/nand.h>

#ifdef CONFIG_MTD_NAND_BBT_USING_FLASH
int nanddev_scan_bbt_in_flash(struct nand_device *nand);
int nanddev_bbt_in_flash_update(struct nand_device *nand);
#else
static inline int nanddev_scan_bbt_in_flash(struct nand_device *nand)
{
	return 0;
}
static inline int nanddev_bbt_in_flash_update(struct nand_device *nand)
{
	return 0;
}
#endif

#endif
