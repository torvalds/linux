/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm8400.h  --  audio driver for WM8400
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#ifndef _WM8400_CODEC_H
#define _WM8400_CODEC_H

#define WM8400_MCLK_DIV 0
#define WM8400_DACCLK_DIV 1
#define WM8400_ADCCLK_DIV 2
#define WM8400_BCLK_DIV 3

#define WM8400_MCLK_DIV_1 0x400
#define WM8400_MCLK_DIV_2 0x800

#define WM8400_DAC_CLKDIV_1    0x00
#define WM8400_DAC_CLKDIV_1_5  0x04
#define WM8400_DAC_CLKDIV_2    0x08
#define WM8400_DAC_CLKDIV_3    0x0c
#define WM8400_DAC_CLKDIV_4    0x10
#define WM8400_DAC_CLKDIV_5_5  0x14
#define WM8400_DAC_CLKDIV_6    0x18

#define WM8400_ADC_CLKDIV_1    0x00
#define WM8400_ADC_CLKDIV_1_5  0x20
#define WM8400_ADC_CLKDIV_2    0x40
#define WM8400_ADC_CLKDIV_3    0x60
#define WM8400_ADC_CLKDIV_4    0x80
#define WM8400_ADC_CLKDIV_5_5  0xa0
#define WM8400_ADC_CLKDIV_6    0xc0


#define WM8400_BCLK_DIV_1                       (0x0 << 1)
#define WM8400_BCLK_DIV_1_5                     (0x1 << 1)
#define WM8400_BCLK_DIV_2                       (0x2 << 1)
#define WM8400_BCLK_DIV_3                       (0x3 << 1)
#define WM8400_BCLK_DIV_4                       (0x4 << 1)
#define WM8400_BCLK_DIV_5_5                     (0x5 << 1)
#define WM8400_BCLK_DIV_6                       (0x6 << 1)
#define WM8400_BCLK_DIV_8                       (0x7 << 1)
#define WM8400_BCLK_DIV_11                      (0x8 << 1)
#define WM8400_BCLK_DIV_12                      (0x9 << 1)
#define WM8400_BCLK_DIV_16                      (0xA << 1)
#define WM8400_BCLK_DIV_22                      (0xB << 1)
#define WM8400_BCLK_DIV_24                      (0xC << 1)
#define WM8400_BCLK_DIV_32                      (0xD << 1)
#define WM8400_BCLK_DIV_44                      (0xE << 1)
#define WM8400_BCLK_DIV_48                      (0xF << 1)

#endif
