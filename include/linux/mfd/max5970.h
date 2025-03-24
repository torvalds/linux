/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Device driver for regulators in MAX5970 and MAX5978 IC
 *
 * Copyright (c) 2022 9elements GmbH
 *
 * Author: Patrick Rudolph <patrick.rudolph@9elements.com>
 */

#ifndef _MFD_MAX5970_H
#define _MFD_MAX5970_H

#include <linux/regmap.h>

#define MAX5970_NUM_SWITCHES 2
#define MAX5978_NUM_SWITCHES 1
#define MAX5970_NUM_LEDS     4

#define MAX5970_REG_CURRENT_L(ch)		(0x01 + (ch) * 4)
#define MAX5970_REG_CURRENT_H(ch)		(0x00 + (ch) * 4)
#define MAX5970_REG_VOLTAGE_L(ch)		(0x03 + (ch) * 4)
#define MAX5970_REG_VOLTAGE_H(ch)		(0x02 + (ch) * 4)
#define MAX5970_REG_MON_RANGE			0x18
#define  MAX5970_MON_MASK			0x3
#define  MAX5970_MON(reg, ch)			(((reg) >> ((ch) * 2)) & MAX5970_MON_MASK)
#define  MAX5970_MON_MAX_RANGE_UV		16000000

#define MAX5970_REG_CH_UV_WARN_H(ch)		(0x1A + (ch) * 10)
#define MAX5970_REG_CH_UV_WARN_L(ch)		(0x1B + (ch) * 10)
#define MAX5970_REG_CH_UV_CRIT_H(ch)		(0x1C + (ch) * 10)
#define MAX5970_REG_CH_UV_CRIT_L(ch)		(0x1D + (ch) * 10)
#define MAX5970_REG_CH_OV_WARN_H(ch)		(0x1E + (ch) * 10)
#define MAX5970_REG_CH_OV_WARN_L(ch)		(0x1F + (ch) * 10)
#define MAX5970_REG_CH_OV_CRIT_H(ch)		(0x20 + (ch) * 10)
#define MAX5970_REG_CH_OV_CRIT_L(ch)		(0x21 + (ch) * 10)

#define  MAX5970_VAL2REG_H(x)		(((x) >> 2) & 0xFF)
#define  MAX5970_VAL2REG_L(x)		((x) & 0x3)

#define MAX5970_REG_DAC_FAST(ch)	(0x2E + (ch))

#define MAX5970_FAST2SLOW_RATIO		200

#define MAX5970_REG_STATUS0		0x31
#define  MAX5970_CB_IFAULTF(ch)		(1 << (ch))
#define  MAX5970_CB_IFAULTS(ch)		(1 << ((ch) + 4))

#define MAX5970_REG_STATUS1		0x32
#define  STATUS1_PROT_MASK		0x3
#define  STATUS1_PROT(reg) \
	(((reg) >> 6) & STATUS1_PROT_MASK)
#define  STATUS1_PROT_SHUTDOWN		0
#define  STATUS1_PROT_CLEAR_PG		1
#define  STATUS1_PROT_ALERT_ONLY	2

#define MAX5970_REG_STATUS2		0x33
#define  MAX5970_IRNG_MASK		0x3
#define  MAX5970_IRNG(reg, ch) \
	(((reg) >> ((ch) * 2)) & MAX5970_IRNG_MASK)

#define MAX5970_REG_STATUS3		0x34
#define  MAX5970_STATUS3_ALERT		BIT(4)
#define  MAX5970_STATUS3_PG(ch)		BIT(ch)

#define MAX5970_REG_FAULT0		0x35
#define  UV_STATUS_WARN(ch)		(1 << (ch))
#define  UV_STATUS_CRIT(ch)		(1 << ((ch) + 4))

#define MAX5970_REG_FAULT1		0x36
#define  OV_STATUS_WARN(ch)		(1 << (ch))
#define  OV_STATUS_CRIT(ch)		(1 << ((ch) + 4))

#define MAX5970_REG_FAULT2		0x37
#define  OC_STATUS_WARN(ch)		(1 << (ch))

#define MAX5970_REG_CHXEN		0x3b
#define  CHXEN(ch)			(3 << ((ch) * 2))

#define MAX5970_REG_LED_FLASH		0x43

#define MAX_REGISTERS			0x49
#define ADC_MASK			0x3FF

#endif				/* _MFD_MAX5970_H */
