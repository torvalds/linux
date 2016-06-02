/*
 * tas5720.h - ALSA SoC Texas Instruments TAS5720 Mono Audio Amplifier
 *
 * Copyright (C)2015-2016 Texas Instruments Incorporated -  http://www.ti.com
 *
 * Author: Andreas Dannenberg <dannenberg@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __TAS5720_H__
#define __TAS5720_H__

/* Register Address Map */
#define TAS5720_DEVICE_ID_REG		0x00
#define TAS5720_POWER_CTRL_REG		0x01
#define TAS5720_DIGITAL_CTRL1_REG	0x02
#define TAS5720_DIGITAL_CTRL2_REG	0x03
#define TAS5720_VOLUME_CTRL_REG		0x04
#define TAS5720_ANALOG_CTRL_REG		0x06
#define TAS5720_FAULT_REG		0x08
#define TAS5720_DIGITAL_CLIP2_REG	0x10
#define TAS5720_DIGITAL_CLIP1_REG	0x11
#define TAS5720_MAX_REG			TAS5720_DIGITAL_CLIP1_REG

/* TAS5720_DEVICE_ID_REG */
#define TAS5720_DEVICE_ID		0x01

/* TAS5720_POWER_CTRL_REG */
#define TAS5720_DIG_CLIP_MASK		GENMASK(7, 2)
#define TAS5720_SLEEP			BIT(1)
#define TAS5720_SDZ			BIT(0)

/* TAS5720_DIGITAL_CTRL1_REG */
#define TAS5720_HPF_BYPASS		BIT(7)
#define TAS5720_TDM_CFG_SRC		BIT(6)
#define TAS5720_SSZ_DS			BIT(3)
#define TAS5720_SAIF_RIGHTJ_24BIT	(0x0)
#define TAS5720_SAIF_RIGHTJ_20BIT	(0x1)
#define TAS5720_SAIF_RIGHTJ_18BIT	(0x2)
#define TAS5720_SAIF_RIGHTJ_16BIT	(0x3)
#define TAS5720_SAIF_I2S		(0x4)
#define TAS5720_SAIF_LEFTJ		(0x5)
#define TAS5720_SAIF_FORMAT_MASK	GENMASK(2, 0)

/* TAS5720_DIGITAL_CTRL2_REG */
#define TAS5720_MUTE			BIT(4)
#define TAS5720_TDM_SLOT_SEL_MASK	GENMASK(2, 0)

/* TAS5720_ANALOG_CTRL_REG */
#define TAS5720_PWM_RATE_6_3_FSYNC	(0x0 << 4)
#define TAS5720_PWM_RATE_8_4_FSYNC	(0x1 << 4)
#define TAS5720_PWM_RATE_10_5_FSYNC	(0x2 << 4)
#define TAS5720_PWM_RATE_12_6_FSYNC	(0x3 << 4)
#define TAS5720_PWM_RATE_14_7_FSYNC	(0x4 << 4)
#define TAS5720_PWM_RATE_16_8_FSYNC	(0x5 << 4)
#define TAS5720_PWM_RATE_20_10_FSYNC	(0x6 << 4)
#define TAS5720_PWM_RATE_24_12_FSYNC	(0x7 << 4)
#define TAS5720_PWM_RATE_MASK		GENMASK(6, 4)
#define TAS5720_ANALOG_GAIN_19_2DBV	(0x0 << 2)
#define TAS5720_ANALOG_GAIN_20_7DBV	(0x1 << 2)
#define TAS5720_ANALOG_GAIN_23_5DBV	(0x2 << 2)
#define TAS5720_ANALOG_GAIN_26_3DBV	(0x3 << 2)
#define TAS5720_ANALOG_GAIN_MASK	GENMASK(3, 2)
#define TAS5720_ANALOG_GAIN_SHIFT	(0x2)

/* TAS5720_FAULT_REG */
#define TAS5720_OC_THRESH_100PCT	(0x0 << 4)
#define TAS5720_OC_THRESH_75PCT		(0x1 << 4)
#define TAS5720_OC_THRESH_50PCT		(0x2 << 4)
#define TAS5720_OC_THRESH_25PCT		(0x3 << 4)
#define TAS5720_OC_THRESH_MASK		GENMASK(5, 4)
#define TAS5720_CLKE			BIT(3)
#define TAS5720_OCE			BIT(2)
#define TAS5720_DCE			BIT(1)
#define TAS5720_OTE			BIT(0)
#define TAS5720_FAULT_MASK		GENMASK(3, 0)

/* TAS5720_DIGITAL_CLIP1_REG */
#define TAS5720_CLIP1_MASK		GENMASK(7, 2)
#define TAS5720_CLIP1_SHIFT		(0x2)

#endif /* __TAS5720_H__ */
