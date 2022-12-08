/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tas2764.h - ALSA SoC Texas Instruments TAS2764 Mono Audio Amplifier
 *
 * Copyright (C) 2020 Texas Instruments Incorporated -  https://www.ti.com
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 */

#ifndef __TAS2764__
#define __TAS2764__

/* Book Control Register */
#define TAS2764_BOOKCTL_PAGE	0
#define TAS2764_BOOKCTL_REG	127
#define TAS2764_REG(page, reg)	((page * 128) + reg)

/* Page */
#define TAS2764_PAGE		TAS2764_REG(0X0, 0x00)
#define TAS2764_PAGE_PAGE_MASK	255

/* Software Reset */
#define TAS2764_SW_RST	TAS2764_REG(0X0, 0x01)
#define TAS2764_RST	BIT(0)

/* Power Control */
#define TAS2764_PWR_CTRL		TAS2764_REG(0X0, 0x02)
#define TAS2764_PWR_CTRL_MASK		GENMASK(1, 0)
#define TAS2764_PWR_CTRL_ACTIVE		0x0
#define TAS2764_PWR_CTRL_MUTE		BIT(0)
#define TAS2764_PWR_CTRL_SHUTDOWN	BIT(1)

#define TAS2764_VSENSE_POWER_EN		3
#define TAS2764_ISENSE_POWER_EN		4

/* DC Blocker Control */
#define TAS2764_DC_BLK0			TAS2764_REG(0x0, 0x04)
#define TAS2764_DC_BLK0_HPF_FREQ_PB_SHIFT  0

/* Digital Volume Control */
#define TAS2764_DVC	TAS2764_REG(0X0, 0x1a)
#define TAS2764_DVC_MAX	0xc9

#define TAS2764_CHNL_0  TAS2764_REG(0X0, 0x03)

/* TDM Configuration Reg0 */
#define TAS2764_TDM_CFG0		TAS2764_REG(0X0, 0x08)
#define TAS2764_TDM_CFG0_SMP_MASK	BIT(5)
#define TAS2764_TDM_CFG0_SMP_48KHZ	0x0
#define TAS2764_TDM_CFG0_SMP_44_1KHZ	BIT(5)
#define TAS2764_TDM_CFG0_MASK		GENMASK(3, 1)
#define TAS2764_TDM_CFG0_44_1_48KHZ	BIT(3)
#define TAS2764_TDM_CFG0_88_2_96KHZ	(BIT(3) | BIT(1))
#define TAS2764_TDM_CFG0_FRAME_START	BIT(0)

/* TDM Configuration Reg1 */
#define TAS2764_TDM_CFG1		TAS2764_REG(0X0, 0x09)
#define TAS2764_TDM_CFG1_MASK		GENMASK(5, 1)
#define TAS2764_TDM_CFG1_51_SHIFT	1
#define TAS2764_TDM_CFG1_RX_MASK	BIT(0)
#define TAS2764_TDM_CFG1_RX_RISING	0x0
#define TAS2764_TDM_CFG1_RX_FALLING	BIT(0)

/* TDM Configuration Reg2 */
#define TAS2764_TDM_CFG2		TAS2764_REG(0X0, 0x0a)
#define TAS2764_TDM_CFG2_RXW_MASK	GENMASK(3, 2)
#define TAS2764_TDM_CFG2_RXW_16BITS	0x0
#define TAS2764_TDM_CFG2_RXW_24BITS	BIT(3)
#define TAS2764_TDM_CFG2_RXW_32BITS	(BIT(3) | BIT(2))
#define TAS2764_TDM_CFG2_RXS_MASK	GENMASK(1, 0)
#define TAS2764_TDM_CFG2_RXS_16BITS	0x0
#define TAS2764_TDM_CFG2_RXS_24BITS	BIT(0)
#define TAS2764_TDM_CFG2_RXS_32BITS	BIT(1)
#define TAS2764_TDM_CFG2_SCFG_SHIFT	4

/* TDM Configuration Reg3 */
#define TAS2764_TDM_CFG3		TAS2764_REG(0X0, 0x0c)
#define TAS2764_TDM_CFG3_RXS_MASK	GENMASK(7, 4)
#define TAS2764_TDM_CFG3_RXS_SHIFT	0x4
#define TAS2764_TDM_CFG3_MASK		GENMASK(3, 0)

/* TDM Configuration Reg5 */
#define TAS2764_TDM_CFG5		TAS2764_REG(0X0, 0x0e)
#define TAS2764_TDM_CFG5_VSNS_MASK	BIT(6)
#define TAS2764_TDM_CFG5_VSNS_ENABLE	BIT(6)
#define TAS2764_TDM_CFG5_50_MASK	GENMASK(5, 0)

/* TDM Configuration Reg6 */
#define TAS2764_TDM_CFG6		TAS2764_REG(0X0, 0x0f)
#define TAS2764_TDM_CFG6_ISNS_MASK	BIT(6)
#define TAS2764_TDM_CFG6_ISNS_ENABLE	BIT(6)
#define TAS2764_TDM_CFG6_50_MASK	GENMASK(5, 0)

/* Interrupt Masks */
#define TAS2764_INT_MASK0               TAS2764_REG(0x0, 0x3b)
#define TAS2764_INT_MASK1               TAS2764_REG(0x0, 0x3c)
#define TAS2764_INT_MASK2               TAS2764_REG(0x0, 0x40)
#define TAS2764_INT_MASK3               TAS2764_REG(0x0, 0x41)
#define TAS2764_INT_MASK4               TAS2764_REG(0x0, 0x3d)

/* Latched Fault Registers */
#define TAS2764_INT_LTCH0               TAS2764_REG(0x0, 0x49)
#define TAS2764_INT_LTCH1               TAS2764_REG(0x0, 0x4a)
#define TAS2764_INT_LTCH1_0             TAS2764_REG(0x0, 0x4b)
#define TAS2764_INT_LTCH2               TAS2764_REG(0x0, 0x4f)
#define TAS2764_INT_LTCH3               TAS2764_REG(0x0, 0x50)
#define TAS2764_INT_LTCH4               TAS2764_REG(0x0, 0x51)

/* Clock/IRQ Settings */
#define TAS2764_INT_CLK_CFG             TAS2764_REG(0x0, 0x5c)
#define TAS2764_INT_CLK_CFG_IRQZ_CLR    BIT(2)

#endif /* __TAS2764__ */
