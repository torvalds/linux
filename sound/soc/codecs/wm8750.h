/*
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on WM8753.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _WM8750_H
#define _WM8750_H

/* WM8750 register space */

#define WM8750_LINVOL    0x00
#define WM8750_RINVOL    0x01
#define WM8750_LOUT1V    0x02
#define WM8750_ROUT1V    0x03
#define WM8750_ADCDAC    0x05
#define WM8750_IFACE     0x07
#define WM8750_SRATE     0x08
#define WM8750_LDAC      0x0a
#define WM8750_RDAC      0x0b
#define WM8750_BASS      0x0c
#define WM8750_TREBLE    0x0d
#define WM8750_RESET     0x0f
#define WM8750_3D        0x10
#define WM8750_ALC1      0x11
#define WM8750_ALC2      0x12
#define WM8750_ALC3      0x13
#define WM8750_NGATE     0x14
#define WM8750_LADC      0x15
#define WM8750_RADC      0x16
#define WM8750_ADCTL1    0x17
#define WM8750_ADCTL2    0x18
#define WM8750_PWR1      0x19
#define WM8750_PWR2      0x1a
#define WM8750_ADCTL3    0x1b
#define WM8750_ADCIN     0x1f
#define WM8750_LADCIN    0x20
#define WM8750_RADCIN    0x21
#define WM8750_LOUTM1    0x22
#define WM8750_LOUTM2    0x23
#define WM8750_ROUTM1    0x24
#define WM8750_ROUTM2    0x25
#define WM8750_MOUTM1    0x26
#define WM8750_MOUTM2    0x27
#define WM8750_LOUT2V    0x28
#define WM8750_ROUT2V    0x29
#define WM8750_MOUTV     0x2a

#define WM8750_CACHE_REGNUM 0x2a

#define WM8750_SYSCLK	0

struct wm8750_setup_data {
	unsigned short i2c_address;
};

extern struct snd_soc_codec_dai wm8750_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8750;

#endif
