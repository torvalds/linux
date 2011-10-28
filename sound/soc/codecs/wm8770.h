/*
 * wm8770.h  --  WM8770 ASoC driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8770_H
#define _WM8770_H

/* Registers */
#define WM8770_VOUT1LVOL                0
#define WM8770_VOUT1RVOL                0x1
#define WM8770_VOUT2LVOL                0x2
#define WM8770_VOUT2RVOL                0x3
#define WM8770_VOUT3LVOL                0x4
#define WM8770_VOUT3RVOL                0x5
#define WM8770_VOUT4LVOL                0x6
#define WM8770_VOUT4RVOL                0x7
#define WM8770_MSALGVOL                 0x8
#define WM8770_DAC1LVOL                 0x9
#define WM8770_DAC1RVOL                 0xa
#define WM8770_DAC2LVOL                 0xb
#define WM8770_DAC2RVOL                 0xc
#define WM8770_DAC3LVOL                 0xd
#define WM8770_DAC3RVOL                 0xe
#define WM8770_DAC4LVOL                 0xf
#define WM8770_DAC4RVOL                 0x10
#define WM8770_MSDIGVOL                 0x11
#define WM8770_DACPHASE                 0x12
#define WM8770_DACCTRL1                 0x13
#define WM8770_DACMUTE                  0x14
#define WM8770_DACCTRL2                 0x15
#define WM8770_IFACECTRL                0x16
#define WM8770_MSTRCTRL                 0x17
#define WM8770_PWDNCTRL                 0x18
#define WM8770_ADCLCTRL                 0x19
#define WM8770_ADCRCTRL                 0x1a
#define WM8770_ADCMUX                   0x1b
#define WM8770_OUTMUX1                  0x1c
#define WM8770_OUTMUX2                  0x1d
#define WM8770_RESET                    0x31

#define WM8770_CACHEREGNUM 0x20

#endif
