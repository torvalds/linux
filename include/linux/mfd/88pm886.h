/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __MFD_88PM886_H
#define __MFD_88PM886_H

#include <linux/i2c.h>
#include <linux/regmap.h>

#define PM886_A1_CHIP_ID		0xa1

#define PM886_IRQ_ONKEY			0

#define PM886_PAGE_OFFSET_REGULATORS	1

#define PM886_REG_ID			0x00

#define PM886_REG_STATUS1		0x01
#define PM886_ONKEY_STS1		BIT(0)

#define PM886_REG_INT_STATUS1		0x05

#define PM886_REG_INT_ENA_1		0x0a
#define PM886_INT_ENA1_ONKEY		BIT(0)

#define PM886_REG_MISC_CONFIG1		0x14
#define PM886_SW_PDOWN			BIT(5)

#define PM886_REG_MISC_CONFIG2		0x15
#define PM886_INT_INV			BIT(0)
#define PM886_INT_CLEAR			BIT(1)
#define PM886_INT_RC			0x00
#define PM886_INT_WC			BIT(1)
#define PM886_INT_MASK_MODE		BIT(2)

#define PM886_REG_RTC_CNT1		0xd1
#define PM886_REG_RTC_CNT2		0xd2
#define PM886_REG_RTC_CNT3		0xd3
#define PM886_REG_RTC_CNT4		0xd4
#define PM886_REG_RTC_SPARE1		0xea
#define PM886_REG_RTC_SPARE2		0xeb
#define PM886_REG_RTC_SPARE3		0xec
#define PM886_REG_RTC_SPARE4		0xed
#define PM886_REG_RTC_SPARE5		0xee
#define PM886_REG_RTC_SPARE6		0xef

#define PM886_REG_BUCK_EN		0x08
#define PM886_REG_LDO_EN1		0x09
#define PM886_REG_LDO_EN2		0x0a
#define PM886_REG_LDO1_VOUT		0x20
#define PM886_REG_LDO2_VOUT		0x26
#define PM886_REG_LDO3_VOUT		0x2c
#define PM886_REG_LDO4_VOUT		0x32
#define PM886_REG_LDO5_VOUT		0x38
#define PM886_REG_LDO6_VOUT		0x3e
#define PM886_REG_LDO7_VOUT		0x44
#define PM886_REG_LDO8_VOUT		0x4a
#define PM886_REG_LDO9_VOUT		0x50
#define PM886_REG_LDO10_VOUT		0x56
#define PM886_REG_LDO11_VOUT		0x5c
#define PM886_REG_LDO12_VOUT		0x62
#define PM886_REG_LDO13_VOUT		0x68
#define PM886_REG_LDO14_VOUT		0x6e
#define PM886_REG_LDO15_VOUT		0x74
#define PM886_REG_LDO16_VOUT		0x7a
#define PM886_REG_BUCK1_VOUT		0xa5
#define PM886_REG_BUCK2_VOUT		0xb3
#define PM886_REG_BUCK3_VOUT		0xc1
#define PM886_REG_BUCK4_VOUT		0xcf
#define PM886_REG_BUCK5_VOUT		0xdd

#define PM886_LDO_VSEL_MASK		0x0f
#define PM886_BUCK_VSEL_MASK		0x7f

struct pm886_chip {
	struct i2c_client *client;
	unsigned int chip_id;
	struct regmap *regmap;
};
#endif /* __MFD_88PM886_H */
