/*
 * wm8753.h  --  audio driver for WM8753
 *
 * Copyright 2003 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef _WM8753_H
#define _WM8753_H

/* WM8753 register space */

#define WM8753_DAC		0x01
#define WM8753_ADC		0x02
#define WM8753_PCM		0x03
#define WM8753_HIFI		0x04
#define WM8753_IOCTL		0x05
#define WM8753_SRATE1		0x06
#define WM8753_SRATE2		0x07
#define WM8753_LDAC		0x08
#define WM8753_RDAC		0x09
#define WM8753_BASS		0x0a
#define WM8753_TREBLE		0x0b
#define WM8753_ALC1		0x0c
#define WM8753_ALC2		0x0d
#define WM8753_ALC3		0x0e
#define WM8753_NGATE		0x0f
#define WM8753_LADC		0x10
#define WM8753_RADC		0x11
#define WM8753_ADCTL1		0x12
#define WM8753_3D		0x13
#define WM8753_PWR1		0x14
#define WM8753_PWR2		0x15
#define WM8753_PWR3		0x16
#define WM8753_PWR4		0x17
#define WM8753_ID		0x18
#define WM8753_INTPOL		0x19
#define WM8753_INTEN		0x1a
#define WM8753_GPIO1		0x1b
#define WM8753_GPIO2		0x1c
#define WM8753_RESET		0x1f
#define WM8753_RECMIX1		0x20
#define WM8753_RECMIX2		0x21
#define WM8753_LOUTM1		0x22
#define WM8753_LOUTM2		0x23
#define WM8753_ROUTM1		0x24
#define WM8753_ROUTM2		0x25
#define WM8753_MOUTM1		0x26
#define WM8753_MOUTM2		0x27
#define WM8753_LOUT1V		0x28
#define WM8753_ROUT1V		0x29
#define WM8753_LOUT2V		0x2a
#define WM8753_ROUT2V		0x2b
#define WM8753_MOUTV		0x2c
#define WM8753_OUTCTL		0x2d
#define WM8753_ADCIN		0x2e
#define WM8753_INCTL1		0x2f
#define WM8753_INCTL2		0x30
#define WM8753_LINVOL		0x31
#define WM8753_RINVOL		0x32
#define WM8753_MICBIAS		0x33
#define WM8753_CLOCK		0x34
#define WM8753_PLL1CTL1		0x35
#define WM8753_PLL1CTL2		0x36
#define WM8753_PLL1CTL3		0x37
#define WM8753_PLL1CTL4		0x38
#define WM8753_PLL2CTL1		0x39
#define WM8753_PLL2CTL2		0x3a
#define WM8753_PLL2CTL3		0x3b
#define WM8753_PLL2CTL4		0x3c
#define WM8753_BIASCTL		0x3d
#define WM8753_ADCTL2		0x3f

#define WM8753_PLL1			0
#define WM8753_PLL2			1

/* clock inputs */
#define WM8753_MCLK		0
#define WM8753_PCMCLK		1

/* clock divider id's */
#define WM8753_PCMDIV		0
#define WM8753_BCLKDIV		1
#define WM8753_VXCLKDIV		2

/* PCM clock dividers */
#define WM8753_PCM_DIV_1	(0 << 6)
#define WM8753_PCM_DIV_3	(2 << 6)
#define WM8753_PCM_DIV_5_5	(3 << 6)
#define WM8753_PCM_DIV_2	(4 << 6)
#define WM8753_PCM_DIV_4	(5 << 6)
#define WM8753_PCM_DIV_6	(6 << 6)
#define WM8753_PCM_DIV_8	(7 << 6)

/* BCLK clock dividers */
#define WM8753_BCLK_DIV_1	(0 << 3)
#define WM8753_BCLK_DIV_2	(1 << 3)
#define WM8753_BCLK_DIV_4	(2 << 3)
#define WM8753_BCLK_DIV_8	(3 << 3)
#define WM8753_BCLK_DIV_16	(4 << 3)

/* VXCLK clock dividers */
#define WM8753_VXCLK_DIV_1	(0 << 6)
#define WM8753_VXCLK_DIV_2	(1 << 6)
#define WM8753_VXCLK_DIV_4	(2 << 6)
#define WM8753_VXCLK_DIV_8	(3 << 6)
#define WM8753_VXCLK_DIV_16	(4 << 6)

#define WM8753_DAI_HIFI		0
#define WM8753_DAI_VOICE		1

#endif
