/*
 * wm8711.h  --  WM8711 Soc Audio driver
 *
 * Copyright 2006 Wolfson Microelectronics
 *
 * Author: Mike Arthur <linux@wolfsonmicro.com>
 *
 * Based on wm8731.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8711_H
#define _WM8711_H

/* WM8711 register space */

#define WM8711_LOUT1V   0x02
#define WM8711_ROUT1V   0x03
#define WM8711_APANA    0x04
#define WM8711_APDIGI   0x05
#define WM8711_PWR      0x06
#define WM8711_IFACE    0x07
#define WM8711_SRATE    0x08
#define WM8711_ACTIVE   0x09
#define WM8711_RESET	0x0f

#define WM8711_CACHEREGNUM 	8

#define WM8711_SYSCLK	0
#define WM8711_DAI		0

struct wm8711_setup_data {
	unsigned short i2c_address;
};

extern struct snd_soc_dai wm8711_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8711;

#endif
