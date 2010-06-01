/*
 * include/linux/mmc/sh_mmcif.h
 *
 * platform data for eMMC driver
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 */

#ifndef __SH_MMCIF_H__
#define __SH_MMCIF_H__

/*
 * MMCIF : CE_CLK_CTRL [19:16]
 * 1000 : Peripheral clock / 512
 * 0111 : Peripheral clock / 256
 * 0110 : Peripheral clock / 128
 * 0101 : Peripheral clock / 64
 * 0100 : Peripheral clock / 32
 * 0011 : Peripheral clock / 16
 * 0010 : Peripheral clock / 8
 * 0001 : Peripheral clock / 4
 * 0000 : Peripheral clock / 2
 * 1111 : Peripheral clock (sup_pclk set '1')
 */

struct sh_mmcif_plat_data {
	void (*set_pwr)(struct platform_device *pdev, int state);
	void (*down_pwr)(struct platform_device *pdev);
	u8	sup_pclk;	/* 1 :SH7757, 0: SH7724/SH7372 */
	unsigned long caps;
	u32	ocr;
};

#endif /* __SH_MMCIF_H__ */
