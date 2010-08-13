/*
 * wm8580.h  --  audio driver for WM8580
 *
 * Copyright 2008 Samsung Electronics.
 * Author: Ryu Euiyoul
 *         ryu.real@gmail.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
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

