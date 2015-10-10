/*
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
/*!
 * @file linux/mxc_dcic.h
 *
 * @brief Global header file for the MXC DCIC driver
 *
 * @ingroup MXC DCIC
 */

#ifndef __LINUX_DCIC_H__
#define __LINUX_DCIC_H__

#include <uapi/linux/mxc_dcic.h>

#define DCICC_IC_ENABLE					0x1
#define DCICC_IC_DISABLE				0x0
#define DCICC_IC_MASK					0x1
#define DCICC_DE_ACTIVE_HIGH			0
#define DCICC_DE_ACTIVE_LOW				(0x1 << 4)
#define DCICC_DE_ACTIVE_MASK			(0x1 << 4)
#define DCICC_HSYNC_POL_ACTIVE_HIGH		0
#define DCICC_HSYNC_POL_ACTIVE_LOW		(0x1 << 5)
#define DCICC_HSYNC_POL_ACTIVE_MASK		(0x1 << 5)
#define DCICC_VSYNC_POL_ACTIVE_HIGH		0
#define DCICC_VSYNC_POL_ACTIVE_LOW		(0x1 << 6)
#define DCICC_VSYNC_POL_ACTIVE_MASK		(0x1 << 6)
#define DCICC_CLK_POL_NO_INVERTED		0
#define DCICC_CLK_POL_INVERTED			(0x1 << 7)
#define DCICC_CLK_POL_INVERTED_MASK		(0x1 << 7)

#define DCICIC_ERROR_INT_DISABLE		1
#define DCICIC_ERROR_INT_ENABLE			0
#define DCICIC_ERROR_INT_MASK_MASK		1
#define DCICIC_FUN_INT_DISABLE			(0x1 << 1)
#define DCICIC_FUN_INT_ENABLE			0
#define DCICIC_FUN_INT_MASK				(0x1 << 1)
#define DCICIC_FREEZE_MASK_CHANGED		0
#define DCICIC_FREEZE_MASK_FORZEN		(0x1 << 3)
#define DCICIC_FREEZE_MASK_MASK			(0x1 << 3)
#define DCICIC_EXT_SIG_EX_DISABLE		0
#define DCICIC_EXT_SIG_EN_ENABLE		(0x1 << 16)
#define DCICIC_EXT_SIG_EN_MASK			(0x1 << 16)

#define DCICS_ROI_MATCH_STAT_MASK		0xFFFF
#define DCICS_EI_STAT_PENDING			(0x1 << 16)
#define DCICS_EI_STAT_NO_PENDING		0
#define DCICS_FI_STAT_PENDING			(0x1 << 17)
#define DCICS_FI_STAT_NO_PENDING		0

#define DCICRC_ROI_START_OFFSET_X_MASK	0x1FFF
#define DCICRC_ROI_START_OFFSET_X_SHIFT	0
#define DCICRC_ROI_START_OFFSET_Y_MASK	(0xFFF << 16)
#define DCICRC_ROI_START_OFFSET_Y_SHIFT	16
#define DCICRC_ROI_CHANGED				0
#define DCICRC_ROI_FROZEN				(0x1 << 30)
#define DCICRC_ROI_ENABLE				(0x1 << 31)
#define DCICRC_ROI_DISABLE				0

#define DCICRS_ROI_END_OFFSET_X_MASK	0x1FFF
#define DCICRS_ROI_END_OFFSET_X_SHIFT	0
#define DCICRS_ROI_END_OFFSET_Y_MASK	(0xFFF << 16)
#define DCICRS_ROI_END_OFFSET_Y_SHIFT	16

struct roi_regs {
	u32 dcicrc;
	u32 dcicrs;
	u32 dcicrrs;
	u32 dcicrcs;
};

struct dcic_regs {
	u32 dcicc;
	u32 dcicic;
	u32 dcics;
	u32 dcic_reserved;
	struct roi_regs ROI[16];
};

struct dcic_mux {
	char dcic[16];
	u32 val;
};

struct bus_mux {
	char name[16];
	int reg;
	int shift;
	int mask;
	int dcic_mux_num;
	const struct dcic_mux *dcics;
};

struct dcic_info {
	int bus_mux_num;
	const struct bus_mux *buses;
};

struct dcic_data {
	struct regmap *regmap;
	struct device *dev;
	struct dcic_regs *regs;
	const struct bus_mux *buses;
	u32 bus_n;
	u32 mux_n;
	struct clk *disp_axi_clk;
	struct clk *dcic_clk;
	struct mutex lock;
	struct completion roi_crc_comp;
	struct class *class;
	int major;
	struct cdev cdev;	/* Char device structure */
	dev_t devt;
	unsigned int result;
};
#endif
