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

#ifndef _WM8994_H
#define _WM8994_H

/* WM8994 register space */

#define WM8994_LINVOL    0x0f
#define WM8994_RINVOL    0x01
#define WM8994_LOUT1V    0x02
#define WM8994_ROUT1V    0x03
#define WM8994_ADCDAC    0x05
#define WM8994_IFACE     0x07
#define WM8994_SRATE     0x08
#define WM8994_LDAC      0x0a
#define WM8994_RDAC      0x0b
#define WM8994_BASS      0x0c
#define WM8994_TREBLE    0x0d
#define WM8994_RESET     0x00
#define WM8994_3D        0x10
#define WM8994_ALC1      0x11
#define WM8994_ALC2      0x12
#define WM8994_ALC3      0x13
#define WM8994_NGATE     0x14
#define WM8994_LADC      0x15
#define WM8994_RADC      0x16
#define WM8994_ADCTL1    0x17
#define WM8994_ADCTL2    0x18
#define WM8994_PWR1      0x19
#define WM8994_PWR2      0x1a
#define WM8994_ADCTL3    0x1b
#define WM8994_ADCIN     0x1f
#define WM8994_LADCIN    0x20
#define WM8994_RADCIN    0x21
#define WM8994_LOUTM1    0x22
#define WM8994_LOUTM2    0x23
#define WM8994_ROUTM1    0x24
#define WM8994_ROUTM2    0x25
#define WM8994_LOUT2V    0x28
#define WM8994_ROUT2V    0x29
#define WM8994_LPPB      0x43
#define WM8994_NUM_REG   0x44

#define WM8994_SYSCLK	0

extern struct snd_soc_dai wm8994_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8994;
void wm8994_codec_set_volume(unsigned char mode,unsigned char volume);

struct wm8994_platform_data {
    unsigned int mic_input;
    unsigned int micBase_vcc;
    unsigned int bb_input;
    unsigned int bb_output;
    unsigned int frequence;
    unsigned int enable_pin;
    unsigned int headset_pin;
    unsigned int headset_call_vol;
    unsigned int speaker_call_vol;
    unsigned int earpiece_call_vol;
    unsigned int bt_call_vol;
}; 

#endif
