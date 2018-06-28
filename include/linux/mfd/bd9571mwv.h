/*
 * ROHM BD9571MWV-M driver
 *
 * Copyright (C) 2017 Marek Vasut <marek.vasut+renesas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether expressed or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * Based on the TPS65086 driver
 */

#ifndef __LINUX_MFD_BD9571MWV_H
#define __LINUX_MFD_BD9571MWV_H

#include <linux/device.h>
#include <linux/regmap.h>

/* List of registers for BD9571MWV */
#define BD9571MWV_VENDOR_CODE			0x00
#define BD9571MWV_VENDOR_CODE_VAL		0xdb
#define BD9571MWV_PRODUCT_CODE			0x01
#define BD9571MWV_PRODUCT_CODE_VAL		0x60
#define BD9571MWV_PRODUCT_REVISION		0x02

#define BD9571MWV_I2C_FUSA_MODE			0x10
#define BD9571MWV_I2C_MD2_E1_BIT_1		0x11
#define BD9571MWV_I2C_MD2_E1_BIT_2		0x12

#define BD9571MWV_BKUP_MODE_CNT			0x20
#define BD9571MWV_BKUP_MODE_CNT_KEEPON_MASK	GENMASK(3, 0)
#define BD9571MWV_BKUP_MODE_CNT_KEEPON_DDR0	BIT(0)
#define BD9571MWV_BKUP_MODE_CNT_KEEPON_DDR1	BIT(1)
#define BD9571MWV_BKUP_MODE_CNT_KEEPON_DDR0C	BIT(2)
#define BD9571MWV_BKUP_MODE_CNT_KEEPON_DDR1C	BIT(3)
#define BD9571MWV_BKUP_MODE_STATUS		0x21
#define BD9571MWV_BKUP_RECOVERY_CNT		0x22
#define BD9571MWV_BKUP_CTRL_TIM_CNT		0x23
#define BD9571MWV_WAITBKUP_WDT_CNT		0x24
#define BD9571MWV_128H_TIM_CNT			0x26
#define BD9571MWV_QLLM_CNT			0x27

#define BD9571MWV_AVS_SET_MONI			0x31
#define BD9571MWV_AVS_SET_MONI_MASK		0x3
#define BD9571MWV_AVS_VD09_VID(n)		(0x32 + (n))
#define BD9571MWV_AVS_DVFS_VID(n)		(0x36 + (n))

#define BD9571MWV_VD18_VID			0x42
#define BD9571MWV_VD25_VID			0x43
#define BD9571MWV_VD33_VID			0x44

#define BD9571MWV_DVFS_VINIT			0x50
#define BD9571MWV_DVFS_SETVMAX			0x52
#define BD9571MWV_DVFS_BOOSTVID			0x53
#define BD9571MWV_DVFS_SETVID			0x54
#define BD9571MWV_DVFS_MONIVDAC			0x55
#define BD9571MWV_DVFS_PGD_CNT			0x56

#define BD9571MWV_GPIO_DIR			0x60
#define BD9571MWV_GPIO_OUT			0x61
#define BD9571MWV_GPIO_IN			0x62
#define BD9571MWV_GPIO_DEB			0x63
#define BD9571MWV_GPIO_INT_SET			0x64
#define BD9571MWV_GPIO_INT			0x65
#define BD9571MWV_GPIO_INTMASK			0x66

#define BD9571MWV_REG_KEEP(n)			(0x70 + (n))

#define BD9571MWV_PMIC_INTERNAL_STATUS		0x80
#define BD9571MWV_PROT_ERROR_STATUS0		0x81
#define BD9571MWV_PROT_ERROR_STATUS1		0x82
#define BD9571MWV_PROT_ERROR_STATUS2		0x83
#define BD9571MWV_PROT_ERROR_STATUS3		0x84
#define BD9571MWV_PROT_ERROR_STATUS4		0x85

#define BD9571MWV_INT_INTREQ			0x90
#define BD9571MWV_INT_INTREQ_MD1_INT		BIT(0)
#define BD9571MWV_INT_INTREQ_MD2_E1_INT		BIT(1)
#define BD9571MWV_INT_INTREQ_MD2_E2_INT		BIT(2)
#define BD9571MWV_INT_INTREQ_PROT_ERR_INT	BIT(3)
#define BD9571MWV_INT_INTREQ_GP_INT		BIT(4)
#define BD9571MWV_INT_INTREQ_128H_OF_INT	BIT(5)
#define BD9571MWV_INT_INTREQ_WDT_OF_INT		BIT(6)
#define BD9571MWV_INT_INTREQ_BKUP_TRG_INT	BIT(7)
#define BD9571MWV_INT_INTMASK			0x91

#define BD9571MWV_ACCESS_KEY			0xff

/* Define the BD9571MWV IRQ numbers */
enum bd9571mwv_irqs {
	BD9571MWV_IRQ_MD1,
	BD9571MWV_IRQ_MD2_E1,
	BD9571MWV_IRQ_MD2_E2,
	BD9571MWV_IRQ_PROT_ERR,
	BD9571MWV_IRQ_GP,
	BD9571MWV_IRQ_128H_OF,
	BD9571MWV_IRQ_WDT_OF,
	BD9571MWV_IRQ_BKUP_TRG,
};

/**
 * struct bd9571mwv - state holder for the bd9571mwv driver
 *
 * Device data may be used to access the BD9571MWV chip
 */
struct bd9571mwv {
	struct device *dev;
	struct regmap *regmap;

	/* IRQ Data */
	int irq;
	struct regmap_irq_chip_data *irq_data;
};

#endif /* __LINUX_MFD_BD9571MWV_H */
