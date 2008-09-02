/*
 * wm8510.h  --  WM8510 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8510_H
#define _WM8510_H

/* WM8510 register space */

#define WM8510_RESET		0x0
#define WM8510_POWER1		0x1
#define WM8510_POWER2		0x2
#define WM8510_POWER3		0x3
#define WM8510_IFACE		0x4
#define WM8510_COMP			0x5
#define WM8510_CLOCK		0x6
#define WM8510_ADD			0x7
#define WM8510_GPIO			0x8
#define WM8510_DAC			0xa
#define WM8510_DACVOL		0xb
#define WM8510_ADC			0xe
#define WM8510_ADCVOL		0xf
#define WM8510_EQ1			0x12
#define WM8510_EQ2			0x13
#define WM8510_EQ3			0x14
#define WM8510_EQ4			0x15
#define WM8510_EQ5			0x16
#define WM8510_DACLIM1		0x18
#define WM8510_DACLIM2		0x19
#define WM8510_NOTCH1		0x1b
#define WM8510_NOTCH2		0x1c
#define WM8510_NOTCH3		0x1d
#define WM8510_NOTCH4		0x1e
#define WM8510_ALC1			0x20
#define WM8510_ALC2			0x21
#define WM8510_ALC3			0x22
#define WM8510_NGATE		0x23
#define WM8510_PLLN			0x24
#define WM8510_PLLK1		0x25
#define WM8510_PLLK2		0x26
#define WM8510_PLLK3		0x27
#define WM8510_ATTEN		0x28
#define WM8510_INPUT		0x2c
#define WM8510_INPPGA		0x2d
#define WM8510_ADCBOOST		0x2f
#define WM8510_OUTPUT		0x31
#define WM8510_SPKMIX		0x32
#define WM8510_SPKVOL		0x36
#define WM8510_MONOMIX		0x38

#define WM8510_CACHEREGNUM 	57

/* Clock divider Id's */
#define WM8510_OPCLKDIV		0
#define WM8510_MCLKDIV		1
#define WM8510_ADCCLK		2
#define WM8510_DACCLK		3
#define WM8510_BCLKDIV		4

/* DAC clock dividers */
#define WM8510_DACCLK_F2	(1 << 3)
#define WM8510_DACCLK_F4	(0 << 3)

/* ADC clock dividers */
#define WM8510_ADCCLK_F2	(1 << 3)
#define WM8510_ADCCLK_F4	(0 << 3)

/* PLL Out dividers */
#define WM8510_OPCLKDIV_1	(0 << 4)
#define WM8510_OPCLKDIV_2	(1 << 4)
#define WM8510_OPCLKDIV_3	(2 << 4)
#define WM8510_OPCLKDIV_4	(3 << 4)

/* BCLK clock dividers */
#define WM8510_BCLKDIV_1	(0 << 2)
#define WM8510_BCLKDIV_2	(1 << 2)
#define WM8510_BCLKDIV_4	(2 << 2)
#define WM8510_BCLKDIV_8	(3 << 2)
#define WM8510_BCLKDIV_16	(4 << 2)
#define WM8510_BCLKDIV_32	(5 << 2)

/* MCLK clock dividers */
#define WM8510_MCLKDIV_1	(0 << 5)
#define WM8510_MCLKDIV_1_5	(1 << 5)
#define WM8510_MCLKDIV_2	(2 << 5)
#define WM8510_MCLKDIV_3	(3 << 5)
#define WM8510_MCLKDIV_4	(4 << 5)
#define WM8510_MCLKDIV_6	(5 << 5)
#define WM8510_MCLKDIV_8	(6 << 5)
#define WM8510_MCLKDIV_12	(7 << 5)

struct wm8510_setup_data {
	int i2c_bus;
	unsigned short i2c_address;
};

extern struct snd_soc_dai wm8510_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8510;

#endif
