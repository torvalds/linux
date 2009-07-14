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

#ifndef _WM8988_H
#define _WM8988_H

/* WM8988 register space */

#define WM8988_LINVOL    0x00
#define WM8988_RINVOL    0x01
#define WM8988_LOUT1V    0x02
#define WM8988_ROUT1V    0x03
#define WM8988_ADCDAC    0x05
#define WM8988_IFACE     0x07
#define WM8988_SRATE     0x08
#define WM8988_LDAC      0x0a
#define WM8988_RDAC      0x0b
#define WM8988_BASS      0x0c
#define WM8988_TREBLE    0x0d
#define WM8988_RESET     0x0f
#define WM8988_3D        0x10
#define WM8988_ALC1      0x11
#define WM8988_ALC2      0x12
#define WM8988_ALC3      0x13
#define WM8988_NGATE     0x14
#define WM8988_LADC      0x15
#define WM8988_RADC      0x16
#define WM8988_ADCTL1    0x17
#define WM8988_ADCTL2    0x18
#define WM8988_PWR1      0x19
#define WM8988_PWR2      0x1a
#define WM8988_ADCTL3    0x1b
#define WM8988_ADCIN     0x1f
#define WM8988_LADCIN    0x20
#define WM8988_RADCIN    0x21
#define WM8988_LOUTM1    0x22
#define WM8988_LOUTM2    0x23
#define WM8988_ROUTM1    0x24
#define WM8988_ROUTM2    0x25
#define WM8988_LOUT2V    0x28
#define WM8988_ROUT2V    0x29
#define WM8988_LPPB      0x43
#define WM8988_NUM_REG   0x44

#define WM8988_SYSCLK	0

extern struct snd_soc_dai wm8988_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8988;

#endif
