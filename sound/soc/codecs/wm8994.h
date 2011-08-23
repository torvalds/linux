/*
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on WM8994.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _WM8994_H
#define _WM8994_H

/* WM8994 register space */
#define WM8994_RESET     0x00
#define wm8994_SYSCLK_3072M 0
#define wm8994_SYSCLK_12288M 1
#define wm8994_mic_VCC 0x0010
#define WM8994_DELAY 50

/* Sources for AIF1/2 SYSCLK - use with set_dai_sysclk() */
#define WM8994_SYSCLK_MCLK1 0
#define WM8994_SYSCLK_MCLK2 1
#define WM8994_SYSCLK_FLL1  2
#define WM8994_SYSCLK_FLL2  3

#define WM8994_FLL1 1
#define WM8994_FLL2 2


#define call_maxvol 5			//Sound level during a call
#define BT_call_maxvol 15

#define MAX_MIN(min,value,max)		value = ((value>max) ? max: value); \
									value = ((value<min) ? min: value) 

extern struct snd_soc_dai wm8994_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8994;
extern int Headset_isMic(void);
#define ERROR 1
#define TRUE 0

#define SUSPEND 3
#define POWER_ON 2
#define BUSY 1
#define IDLE 0
#endif
