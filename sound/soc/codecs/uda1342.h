/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Audio support for NXP UDA1342
 *
 * Copyright (c) 2005 Giorgio Padrin <giorgio@mandarinlogiq.org>
 * Copyright (c) 2024 Binbin Zhou <zhoubinbin@loongson.cn>
 */

#ifndef _UDA1342_H
#define _UDA1342_H

#define UDA1342_CLK		0x00
#define UDA1342_IFACE		0x01
#define UDA1342_PM		0x02
#define UDA1342_AMIX		0x03
#define UDA1342_HP		0x04
#define UDA1342_MVOL		0x11
#define UDA1342_MIXVOL		0x12
#define UDA1342_MODE		0x12
#define UDA1342_DEEMP		0x13
#define UDA1342_MIXER		0x14
#define UDA1342_INTSTAT		0x18
#define UDA1342_DEC		0x20
#define UDA1342_PGA		0x21
#define UDA1342_ADC		0x22
#define UDA1342_AGC		0x23
#define UDA1342_DECSTAT		0x28
#define UDA1342_RESET		0x7f

/* Register flags */
#define R00_EN_ADC		0x0800
#define R00_EN_DEC		0x0400
#define R00_EN_DAC		0x0200
#define R00_EN_INT		0x0100
#define R00_DAC_CLK		0x0010
#define R01_SFORI_I2S		0x0000
#define R01_SFORI_LSB16		0x0100
#define R01_SFORI_LSB18		0x0200
#define R01_SFORI_LSB20		0x0300
#define R01_SFORI_MSB		0x0500
#define R01_SFORI_MASK		0x0700
#define R01_SFORO_I2S		0x0000
#define R01_SFORO_LSB16		0x0001
#define R01_SFORO_LSB18		0x0002
#define R01_SFORO_LSB20		0x0003
#define R01_SFORO_LSB24		0x0004
#define R01_SFORO_MSB		0x0005
#define R01_SFORO_MASK		0x0007
#define R01_SEL_SOURCE		0x0040
#define R01_SIM			0x0010
#define R02_PON_PLL		0x8000
#define R02_PON_HP		0x2000
#define R02_PON_DAC		0x0400
#define R02_PON_BIAS		0x0100
#define R02_EN_AVC		0x0080
#define R02_PON_AVC		0x0040
#define R02_PON_LNA		0x0010
#define R02_PON_PGAL		0x0008
#define R02_PON_ADCL		0x0004
#define R02_PON_PGAR		0x0002
#define R02_PON_ADCR		0x0001
#define R13_MTM			0x4000
#define R14_SILENCE		0x0080
#define R14_SDET_ON		0x0040
#define R21_MT_ADC		0x8000
#define R22_SEL_LNA		0x0008
#define R22_SEL_MIC		0x0004
#define R22_SKIP_DCFIL		0x0002
#define R23_AGC_EN		0x0001

#define UDA1342_DAI_DUPLEX	0 /* playback and capture on single DAI */
#define UDA1342_DAI_PLAYBACK	1 /* playback DAI */
#define UDA1342_DAI_CAPTURE	2 /* capture DAI */

#define STATUS0_DAIFMT_MASK (~(7 << 1))
#define STATUS0_SYSCLK_MASK (~(3 << 4))

#endif /* _UDA1342_H */
