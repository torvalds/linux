/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wm8940.h -- WM8940 Soc Audio driver
 */

#ifndef _WM8940_H
#define _WM8940_H

struct wm8940_setup_data {
	/* Vref to analogue output resistance */
#define WM8940_VROI_1K 0
#define WM8940_VROI_30K 1
	unsigned int vroi:1;
};

/* WM8940 register space */
#define WM8940_SOFTRESET	0x00
#define WM8940_POWER1		0x01
#define WM8940_POWER2		0x02
#define WM8940_POWER3		0x03
#define WM8940_IFACE		0x04
#define WM8940_COMPANDINGCTL	0x05
#define WM8940_CLOCK		0x06
#define WM8940_ADDCNTRL		0x07
#define WM8940_GPIO		0x08
#define WM8940_CTLINT		0x09
#define WM8940_DAC		0x0A
#define WM8940_DACVOL		0x0B

#define WM8940_ADC		0x0E
#define WM8940_ADCVOL		0x0F
#define WM8940_NOTCH1		0x10
#define WM8940_NOTCH2		0x11
#define WM8940_NOTCH3		0x12
#define WM8940_NOTCH4		0x13
#define WM8940_NOTCH5		0x14
#define WM8940_NOTCH6		0x15
#define WM8940_NOTCH7		0x16
#define WM8940_NOTCH8		0x17
#define WM8940_DACLIM1		0x18
#define WM8940_DACLIM2		0x19

#define WM8940_ALC1		0x20
#define WM8940_ALC2		0x21
#define WM8940_ALC3		0x22
#define WM8940_NOISEGATE	0x23
#define WM8940_PLLN		0x24
#define WM8940_PLLK1		0x25
#define WM8940_PLLK2		0x26
#define WM8940_PLLK3		0x27

#define WM8940_ALC4		0x2A

#define WM8940_INPUTCTL		0x2C
#define WM8940_PGAGAIN		0x2D

#define WM8940_ADCBOOST		0x2F

#define WM8940_OUTPUTCTL	0x31
#define WM8940_SPKMIX		0x32

#define WM8940_SPKVOL		0x36

#define WM8940_MONOMIX		0x38

#define WM8940_CACHEREGNUM  0x57


/* Clock divider Id's */
#define WM8940_BCLKDIV 0
#define WM8940_MCLKDIV 1
#define WM8940_OPCLKDIV 2

/* MCLK clock dividers */
#define WM8940_MCLKDIV_1	0
#define WM8940_MCLKDIV_1_5	1
#define WM8940_MCLKDIV_2	2
#define WM8940_MCLKDIV_3	3
#define WM8940_MCLKDIV_4	4
#define WM8940_MCLKDIV_6	5
#define WM8940_MCLKDIV_8	6
#define WM8940_MCLKDIV_12	7

/* BCLK clock dividers */
#define WM8940_BCLKDIV_1 0
#define WM8940_BCLKDIV_2 1
#define WM8940_BCLKDIV_4 2
#define WM8940_BCLKDIV_8 3
#define WM8940_BCLKDIV_16 4
#define WM8940_BCLKDIV_32 5

/* PLL Out Dividers */
#define WM8940_OPCLKDIV_1 0
#define WM8940_OPCLKDIV_2 1
#define WM8940_OPCLKDIV_3 2
#define WM8940_OPCLKDIV_4 3

#endif /* _WM8940_H */

