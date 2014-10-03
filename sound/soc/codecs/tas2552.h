/*
 * tas2552.h - ALSA SoC Texas Instruments TAS2552 Mono Audio Amplifier
 *
 * Copyright (C) 2014 Texas Instruments Incorporated -  http://www.ti.com
 *
 * Author: Dan Murphy <dmurphy@ti.com>
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

#ifndef __TAS2552_H__
#define __TAS2552_H__

/* Register Address Map */
#define TAS2552_DEVICE_STATUS	0x00
#define TAS2552_CFG_1			0x01
#define TAS2552_CFG_2			0x02
#define TAS2552_CFG_3			0x03
#define TAS2552_DOUT			0x04
#define TAS2552_SER_CTRL_1		0x05
#define TAS2552_SER_CTRL_2		0x06
#define TAS2552_OUTPUT_DATA		0x07
#define TAS2552_PLL_CTRL_1		0x08
#define TAS2552_PLL_CTRL_2		0x09
#define TAS2552_PLL_CTRL_3		0x0a
#define TAS2552_BTIP			0x0b
#define TAS2552_BTS_CTRL		0x0c
#define TAS2552_RESERVED_0D		0x0d
#define TAS2552_LIMIT_RATE_HYS	0x0e
#define TAS2552_LIMIT_RELEASE	0x0f
#define TAS2552_LIMIT_INT_COUNT	0x10
#define TAS2552_PDM_CFG			0x11
#define TAS2552_PGA_GAIN		0x12
#define TAS2552_EDGE_RATE_CTRL	0x13
#define TAS2552_BOOST_PT_CTRL	0x14
#define TAS2552_VER_NUM			0x16
#define TAS2552_VBAT_DATA		0x19
#define TAS2552_MAX_REG			0x20

/* CFG1 Register Masks */
#define TAS2552_MUTE_MASK		(1 << 2)
#define TAS2552_SWS_MASK		(1 << 1)
#define TAS2552_WCLK_MASK		0x07
#define TAS2552_CLASSD_EN_MASK	(1 << 7)

/* CFG2 Register Masks */
#define TAS2552_CLASSD_EN		(1 << 7)
#define TAS2552_BOOST_EN		(1 << 6)
#define TAS2552_APT_EN			(1 << 5)
#define TAS2552_PLL_ENABLE		(1 << 3)
#define TAS2552_LIM_EN			(1 << 2)
#define TAS2552_IVSENSE_EN		(1 << 1)

/* CFG3 Register Masks */
#define TAS2552_WORD_CLK_MASK		(1 << 7)
#define TAS2552_BIT_CLK_MASK		(1 << 6)
#define TAS2552_DATA_FORMAT_MASK	(0x11 << 2)

#define TAS2552_DAIFMT_I2S_MASK		0xf3
#define TAS2552_DAIFMT_DSP			(1 << 3)
#define TAS2552_DAIFMT_RIGHT_J		(1 << 4)
#define TAS2552_DAIFMT_LEFT_J		(0x11 << 3)

#define TAS2552_PLL_SRC_MCLK	0x00
#define TAS2552_PLL_SRC_BCLK	(1 << 3)
#define TAS2552_PLL_SRC_IVCLKIN	(1 << 4)
#define TAS2552_PLL_SRC_1_8_FIXED (0x11 << 3)

#define TAS2552_DIN_SRC_SEL_MUTED	0x00
#define TAS2552_DIN_SRC_SEL_LEFT	(1 << 4)
#define TAS2552_DIN_SRC_SEL_RIGHT	(1 << 5)
#define TAS2552_DIN_SRC_SEL_AVG_L_R	(0x11 << 4)

#define TAS2552_PDM_IN_SEL		(1 << 5)
#define TAS2552_I2S_OUT_SEL		(1 << 6)
#define TAS2552_ANALOG_IN_SEL	(1 << 7)

/* CFG3 WCLK Dividers */
#define TAS2552_8KHZ		0x00
#define TAS2552_11_12KHZ	(1 << 1)
#define TAS2552_16KHZ		(1 << 2)
#define TAS2552_22_24KHZ	(1 << 3)
#define TAS2552_32KHZ		(1 << 4)
#define TAS2552_44_48KHZ	(1 << 5)
#define TAS2552_88_96KHZ	(1 << 6)
#define TAS2552_176_192KHZ	(1 << 7)

/* OUTPUT_DATA register */
#define TAS2552_PDM_DATA_I		0x00
#define TAS2552_PDM_DATA_V		(1 << 6)
#define TAS2552_PDM_DATA_I_V	(1 << 7)
#define TAS2552_PDM_DATA_V_I	(0x11 << 6)

/* PDM CFG Register */
#define TAS2552_PDM_DATA_ES_RISE 0x4

#define TAS2552_PDM_PLL_CLK_SEL 0x00
#define TAS2552_PDM_IV_CLK_SEL	(1 << 1)
#define TAS2552_PDM_BCLK_SEL	(1 << 2)
#define TAS2552_PDM_MCLK_SEL	(1 << 3)

/* Boost pass-through register */
#define TAS2552_APT_DELAY_50	0x00
#define TAS2552_APT_DELAY_75	(1 << 1)
#define TAS2552_APT_DELAY_125	(1 << 2)
#define TAS2552_APT_DELAY_200	(1 << 3)

#define TAS2552_APT_THRESH_2_5		0x00
#define TAS2552_APT_THRESH_1_7		(1 << 3)
#define TAS2552_APT_THRESH_1_4_1_1	(1 << 4)
#define TAS2552_APT_THRESH_2_1_7	(0x11 << 2)

/* PLL Control Register */
#define TAS2552_245MHZ_CLK			24576000
#define TAS2552_225MHZ_CLK			22579200
#define TAS2552_PLL_J_MASK			0x7f
#define TAS2552_PLL_D_UPPER_MASK	0x3f
#define TAS2552_PLL_D_LOWER_MASK	0xff
#define TAS2552_PLL_BYPASS_MASK		0x80
#define TAS2552_PLL_BYPASS			0x80

#endif
