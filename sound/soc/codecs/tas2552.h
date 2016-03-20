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
#define TAS2552_DEVICE_STATUS		0x00
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
#define TAS2552_LIMIT_RATE_HYS		0x0e
#define TAS2552_LIMIT_RELEASE		0x0f
#define TAS2552_LIMIT_INT_COUNT		0x10
#define TAS2552_PDM_CFG			0x11
#define TAS2552_PGA_GAIN		0x12
#define TAS2552_EDGE_RATE_CTRL		0x13
#define TAS2552_BOOST_APT_CTRL		0x14
#define TAS2552_VER_NUM			0x16
#define TAS2552_VBAT_DATA		0x19
#define TAS2552_MAX_REG			TAS2552_VBAT_DATA

/* CFG1 Register Masks */
#define TAS2552_DEV_RESET		(1 << 0)
#define TAS2552_SWS			(1 << 1)
#define TAS2552_MUTE			(1 << 2)
#define TAS2552_PLL_SRC_MCLK		(0x0 << 4)
#define TAS2552_PLL_SRC_BCLK		(0x1 << 4)
#define TAS2552_PLL_SRC_IVCLKIN		(0x2 << 4)
#define TAS2552_PLL_SRC_1_8_FIXED 	(0x3 << 4)
#define TAS2552_PLL_SRC_MASK	 	TAS2552_PLL_SRC_1_8_FIXED

/* CFG2 Register Masks */
#define TAS2552_CLASSD_EN		(1 << 7)
#define TAS2552_BOOST_EN		(1 << 6)
#define TAS2552_APT_EN			(1 << 5)
#define TAS2552_PLL_ENABLE		(1 << 3)
#define TAS2552_LIM_EN			(1 << 2)
#define TAS2552_IVSENSE_EN		(1 << 1)

/* CFG3 Register Masks */
#define TAS2552_WCLK_FREQ_8KHZ		(0x0 << 0)
#define TAS2552_WCLK_FREQ_11_12KHZ	(0x1 << 0)
#define TAS2552_WCLK_FREQ_16KHZ		(0x2 << 0)
#define TAS2552_WCLK_FREQ_22_24KHZ	(0x3 << 0)
#define TAS2552_WCLK_FREQ_32KHZ		(0x4 << 0)
#define TAS2552_WCLK_FREQ_44_48KHZ	(0x5 << 0)
#define TAS2552_WCLK_FREQ_88_96KHZ	(0x6 << 0)
#define TAS2552_WCLK_FREQ_176_192KHZ	(0x7 << 0)
#define TAS2552_WCLK_FREQ_MASK		TAS2552_WCLK_FREQ_176_192KHZ
#define TAS2552_DIN_SRC_SEL_MUTED	(0x0 << 3)
#define TAS2552_DIN_SRC_SEL_LEFT	(0x1 << 3)
#define TAS2552_DIN_SRC_SEL_RIGHT	(0x2 << 3)
#define TAS2552_DIN_SRC_SEL_AVG_L_R	(0x3 << 3)
#define TAS2552_PDM_IN_SEL		(1 << 5)
#define TAS2552_I2S_OUT_SEL		(1 << 6)
#define TAS2552_ANALOG_IN_SEL		(1 << 7)

/* DOUT Register Masks */
#define TAS2552_SDOUT_TRISTATE		(1 << 2)

/* Serial Interface Control Register Masks */
#define TAS2552_WORDLENGTH_16BIT	(0x0 << 0)
#define TAS2552_WORDLENGTH_20BIT	(0x1 << 0)
#define TAS2552_WORDLENGTH_24BIT	(0x2 << 0)
#define TAS2552_WORDLENGTH_32BIT	(0x3 << 0)
#define TAS2552_WORDLENGTH_MASK		TAS2552_WORDLENGTH_32BIT
#define TAS2552_DATAFORMAT_I2S		(0x0 << 2)
#define TAS2552_DATAFORMAT_DSP		(0x1 << 2)
#define TAS2552_DATAFORMAT_RIGHT_J	(0x2 << 2)
#define TAS2552_DATAFORMAT_LEFT_J	(0x3 << 2)
#define TAS2552_DATAFORMAT_MASK		TAS2552_DATAFORMAT_LEFT_J
#define TAS2552_CLKSPERFRAME_32		(0x0 << 4)
#define TAS2552_CLKSPERFRAME_64		(0x1 << 4)
#define TAS2552_CLKSPERFRAME_128	(0x2 << 4)
#define TAS2552_CLKSPERFRAME_256	(0x3 << 4)
#define TAS2552_CLKSPERFRAME_MASK	TAS2552_CLKSPERFRAME_256
#define TAS2552_BCLKDIR			(1 << 6)
#define TAS2552_WCLKDIR			(1 << 7)

/* OUTPUT_DATA register */
#define TAS2552_DATA_OUT_I_DATA		(0x0)
#define TAS2552_DATA_OUT_V_DATA		(0x1)
#define TAS2552_DATA_OUT_VBAT_DATA	(0x2)
#define TAS2552_DATA_OUT_VBOOST_DATA	(0x3)
#define TAS2552_DATA_OUT_PGA_GAIN	(0x4)
#define TAS2552_DATA_OUT_IV_DATA	(0x5)
#define TAS2552_DATA_OUT_VBAT_VBOOST_GAIN	(0x6)
#define TAS2552_DATA_OUT_DISABLED	(0x7)
#define TAS2552_L_DATA_OUT(x)		((x) << 0)
#define TAS2552_R_DATA_OUT(x)		((x) << 3)
#define TAS2552_PDM_DATA_SEL_I		(0x0 << 6)
#define TAS2552_PDM_DATA_SEL_V		(0x1 << 6)
#define TAS2552_PDM_DATA_SEL_I_V	(0x2 << 6)
#define TAS2552_PDM_DATA_SEL_V_I	(0x3 << 6)
#define TAS2552_PDM_DATA_SEL_MASK	TAS2552_PDM_DATA_SEL_V_I

/* PDM CFG Register */
#define TAS2552_PDM_CLK_SEL_PLL		(0x0 << 0)
#define TAS2552_PDM_CLK_SEL_IVCLKIN	(0x1 << 0)
#define TAS2552_PDM_CLK_SEL_BCLK	(0x2 << 0)
#define TAS2552_PDM_CLK_SEL_MCLK	(0x3 << 0)
#define TAS2552_PDM_CLK_SEL_MASK	TAS2552_PDM_CLK_SEL_MCLK
#define TAS2552_PDM_DATA_ES	 	(1 << 2)

/* Boost Auto-pass through register */
#define TAS2552_APT_DELAY_50		(0x0 << 0)
#define TAS2552_APT_DELAY_75		(0x1 << 0)
#define TAS2552_APT_DELAY_125		(0x2 << 0)
#define TAS2552_APT_DELAY_200		(0x3 << 0)
#define TAS2552_APT_THRESH_05_02	(0x0 << 2)
#define TAS2552_APT_THRESH_10_07	(0x1 << 2)
#define TAS2552_APT_THRESH_14_11	(0x2 << 2)
#define TAS2552_APT_THRESH_20_17	(0x3 << 2)

/* PLL Control Register */
#define TAS2552_PLL_J_MASK		0x7f
#define TAS2552_PLL_D_UPPER(x)		(((x) >> 8) & 0x3f)
#define TAS2552_PLL_D_LOWER(x)		((x) & 0xff)
#define TAS2552_PLL_BYPASS		(1 << 7)

#endif
