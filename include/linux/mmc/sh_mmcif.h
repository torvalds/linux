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

#include <linux/platform_device.h>
#include <linux/io.h>

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

#define MMCIF_CE_CMD_SET	0x00000000
#define MMCIF_CE_ARG		0x00000008
#define MMCIF_CE_ARG_CMD12	0x0000000C
#define MMCIF_CE_CMD_CTRL	0x00000010
#define MMCIF_CE_BLOCK_SET	0x00000014
#define MMCIF_CE_CLK_CTRL	0x00000018
#define MMCIF_CE_BUF_ACC	0x0000001C
#define MMCIF_CE_RESP3		0x00000020
#define MMCIF_CE_RESP2		0x00000024
#define MMCIF_CE_RESP1		0x00000028
#define MMCIF_CE_RESP0		0x0000002C
#define MMCIF_CE_RESP_CMD12	0x00000030
#define MMCIF_CE_DATA		0x00000034
#define MMCIF_CE_INT		0x00000040
#define MMCIF_CE_INT_MASK	0x00000044
#define MMCIF_CE_HOST_STS1	0x00000048
#define MMCIF_CE_HOST_STS2	0x0000004C
#define MMCIF_CE_VERSION	0x0000007C

extern inline u32 sh_mmcif_readl(void __iomem *addr, int reg)
{
	return readl(addr + reg);
}

extern inline void sh_mmcif_writel(void __iomem *addr, int reg, u32 val)
{
	writel(val, addr + reg);
}

#endif /* __SH_MMCIF_H__ */
