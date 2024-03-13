/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm8580.h  --  audio driver for WM8580
 *
 * Copyright 2008 Samsung Electronics.
 * Author: Ryu Euiyoul
 *         ryu.real@gmail.com
 */

#ifndef _WM8580_H
#define _WM8580_H

#define WM8580_PLLA  1
#define WM8580_PLLB  2

#define WM8580_MCLK       1
#define WM8580_CLKOUTSRC  2

#define WM8580_CLKSRC_MCLK    1
#define WM8580_CLKSRC_PLLA    2
#define WM8580_CLKSRC_PLLB    3
#define WM8580_CLKSRC_OSC     4
#define WM8580_CLKSRC_NONE    5
#define WM8580_CLKSRC_ADCMCLK 6

#define WM8580_DAI_PAIFRX 0
#define WM8580_DAI_PAIFTX 1

#endif

