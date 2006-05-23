/*
 *  linux/include/linux/mtd/ndfc.h
 *
 *  Copyright (c) 2006 Thomas Gleixner <tglx@linutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Info:
 *   Contains defines, datastructures for ndfc nand controller
 *
 */
#ifndef __LINUX_MTD_NDFC_H
#define __LINUX_MTD_NDFC_H

/* NDFC Register definitions */
#define NDFC_CMD		0x00
#define NDFC_ALE		0x04
#define NDFC_DATA		0x08
#define NDFC_ECC		0x10
#define NDFC_BCFG0		0x30
#define NDFC_BCFG1		0x34
#define NDFC_BCFG2		0x38
#define NDFC_BCFG3		0x3c
#define NDFC_CCR		0x40
#define NDFC_STAT		0x44
#define NDFC_HWCTL		0x48
#define NDFC_REVID		0x50

#define NDFC_STAT_IS_READY	0x01000000

#define NDFC_CCR_RESET_CE	0x80000000 /* CE Reset */
#define NDFC_CCR_RESET_ECC	0x40000000 /* ECC Reset */
#define NDFC_CCR_RIE		0x20000000 /* Interrupt Enable on Device Rdy */
#define NDFC_CCR_REN		0x10000000 /* Enable wait for Rdy in LinearR */
#define NDFC_CCR_ROMEN		0x08000000 /* Enable ROM In LinearR */
#define NDFC_CCR_ARE		0x04000000 /* Auto-Read Enable */
#define NDFC_CCR_BS(x)		(((x) & 0x3) << 24) /* Select Bank on CE[x] */
#define NDFC_CCR_BS_MASK	0x03000000 /* Select Bank */
#define NDFC_CCR_ARAC0		0x00000000 /* 3 Addr, 1 Col 2 Row 512b page */
#define NDFC_CCR_ARAC1		0x00001000 /* 4 Addr, 1 Col 3 Row 512b page */
#define NDFC_CCR_ARAC2		0x00002000 /* 4 Addr, 2 Col 2 Row 2K page */
#define NDFC_CCR_ARAC3		0x00003000 /* 5 Addr, 2 Col 3 Row 2K page */
#define NDFC_CCR_ARAC_MASK	0x00003000 /* Auto-Read mode Addr Cycles */
#define NDFC_CCR_RPG		0x0000C000 /* Auto-Read Page */
#define NDFC_CCR_EBCC		0x00000004 /* EBC Configuration Completed */
#define NDFC_CCR_DHC		0x00000002 /* Direct Hardware Control Enable */

#define NDFC_BxCFG_EN		0x80000000 /* Bank Enable */
#define NDFC_BxCFG_CED		0x40000000 /* nCE Style */
#define NDFC_BxCFG_SZ_MASK	0x08000000 /* Bank Size */
#define NDFC_BxCFG_SZ_8BIT	0x00000000 /* 8bit */
#define NDFC_BxCFG_SZ_16BIT	0x08000000 /* 16bit */

#define NDFC_MAX_BANKS		4

struct ndfc_controller_settings {
	uint32_t		ccr_settings;
};

struct ndfc_chip_settings {
	uint32_t	bank_settings;
};

#endif
