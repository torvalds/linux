/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cs4265.h -- CS4265 ALSA SoC audio driver
 *
 * Copyright 2014 Cirrus Logic, Inc.
 *
 * Author: Paul Handrigan <paul.handrigan@cirrus.com>
 */

#ifndef __CS4265_H__
#define __CS4265_H__

#define CS4265_CHIP_ID				0x1
#define CS4265_CHIP_ID_VAL			0xD0
#define CS4265_CHIP_ID_MASK			0xF0
#define CS4265_REV_ID_MASK			0x0F

#define CS4265_PWRCTL				0x02
#define CS4265_PWRCTL_PDN			1

#define CS4265_DAC_CTL				0x3
#define CS4265_DAC_CTL_MUTE			(1 << 2)
#define CS4265_DAC_CTL_DIF			(3 << 4)

#define CS4265_ADC_CTL				0x4
#define CS4265_ADC_MASTER			1
#define CS4265_ADC_DIF				(1 << 4)
#define CS4265_ADC_FM				(3 << 6)

#define CS4265_MCLK_FREQ			0x5
#define CS4265_MCLK_FREQ_MASK			(7 << 4)

#define CS4265_SIG_SEL				0x6
#define CS4265_SIG_SEL_LOOP			(1 << 1)

#define CS4265_CHB_PGA_CTL			0x7
#define CS4265_CHA_PGA_CTL			0x8

#define CS4265_ADC_CTL2				0x9

#define CS4265_DAC_CHA_VOL			0xA
#define CS4265_DAC_CHB_VOL			0xB

#define CS4265_DAC_CTL2				0xC

#define CS4265_INT_STATUS			0xD
#define CS4265_INT_MASK				0xE
#define CS4265_STATUS_MODE_MSB			0xF
#define CS4265_STATUS_MODE_LSB			0x10

#define CS4265_SPDIF_CTL1			0x11

#define CS4265_SPDIF_CTL2			0x12
#define CS4265_SPDIF_CTL2_MUTE			(1 << 4)
#define CS4265_SPDIF_CTL2_DIF			(3 << 6)

#define CS4265_C_DATA_BUFF			0x13
#define CS4265_MAX_REGISTER			0x2A

#endif
