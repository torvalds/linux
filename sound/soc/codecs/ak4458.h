/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Audio driver for AK4458
 *
 * Copyright (C) 2016 Asahi Kasei Microdevices Corporation
 * Copyright 2018 NXP
 */

#ifndef _AK4458_H
#define _AK4458_H

#include <linux/regmap.h>

/* Settings */

#define AK4458_00_CONTROL1			0x00
#define AK4458_01_CONTROL2			0x01
#define AK4458_02_CONTROL3			0x02
#define AK4458_03_LCHATT			0x03
#define AK4458_04_RCHATT			0x04
#define AK4458_05_CONTROL4			0x05
#define AK4458_06_DSD1				0x06
#define AK4458_07_CONTROL5			0x07
#define AK4458_08_SOUND_CONTROL			0x08
#define AK4458_09_DSD2				0x09
#define AK4458_0A_CONTROL6			0x0A
#define AK4458_0B_CONTROL7			0x0B
#define AK4458_0C_CONTROL8			0x0C
#define AK4458_0D_CONTROL9			0x0D
#define AK4458_0E_CONTROL10			0x0E
#define AK4458_0F_L2CHATT			0x0F
#define AK4458_10_R2CHATT			0x10
#define AK4458_11_L3CHATT			0x11
#define AK4458_12_R3CHATT			0x12
#define AK4458_13_L4CHATT			0x13
#define AK4458_14_R4CHATT			0x14

/* Bitfield Definitions */

/* AK4458_00_CONTROL1 (0x00) Fields
 * Addr Register Name  D7     D6    D5    D4    D3    D2    D1    D0
 * 00H  Control 1      ACKS   0     0     0     DIF2  DIF1  DIF0  RSTN
 */

/* Digital Filter (SD, SLOW, SSLOW) */
#define AK4458_SD_MASK		GENMASK(5, 5)
#define AK4458_SLOW_MASK	GENMASK(0, 0)
#define AK4458_SSLOW_MASK	GENMASK(0, 0)

/* DIF2	1 0
 *  x	1 0 MSB justified  Figure 3 (default)
 *  x	1 1 I2S Compliment  Figure 4
 */
#define AK4458_DIF_SHIFT	1
#define AK4458_DIF_MASK		GENMASK(3, 1)

#define AK4458_DIF_16BIT_LSB	(0 << 1)
#define AK4458_DIF_24BIT_I2S	(3 << 1)
#define AK4458_DIF_32BIT_LSB	(5 << 1)
#define AK4458_DIF_32BIT_MSB	(6 << 1)
#define AK4458_DIF_32BIT_I2S	(7 << 1)

/* AK4458_00_CONTROL1 (0x00) D0 bit */
#define AK4458_RSTN_MASK	GENMASK(0, 0)
#define AK4458_RSTN		(0x1 << 0)

/* AK4458_0A_CONTROL6 Mode bits */
#define AK4458_MODE_SHIFT	6
#define AK4458_MODE_MASK	GENMASK(7, 6)
#define AK4458_MODE_NORMAL	(0 << AK4458_MODE_SHIFT)
#define AK4458_MODE_TDM128	(1 << AK4458_MODE_SHIFT)
#define AK4458_MODE_TDM256	(2 << AK4458_MODE_SHIFT)
#define AK4458_MODE_TDM512	(3 << AK4458_MODE_SHIFT)

/* DAC Digital attenuator transition time setting
 * Table 19
 * Mode	ATS1	ATS2	ATT speed
 * 0	0	0	4080/fs
 * 1	0	1	2040/fs
 * 2	1	0	510/fs
 * 3	1	1	255/fs
 * */
#define AK4458_ATS_SHIFT	6
#define AK4458_ATS_MASK		GENMASK(7, 6)

#endif /* _AK4458_H */
