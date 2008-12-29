/*
 * wm8900.h  --  WM890 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8900_H
#define _WM8900_H

#define WM8900_FLL 1

#define WM8900_BCLK_DIV   1
#define WM8900_ADC_CLKDIV 2
#define WM8900_DAC_CLKDIV 3
#define WM8900_ADC_LRCLK  4
#define WM8900_DAC_LRCLK  5
#define WM8900_OPCLK_DIV  6
#define WM8900_LRCLK_MODE 7

#define WM8900_BCLK_DIV_1   0x00
#define WM8900_BCLK_DIV_1_5 0x02
#define WM8900_BCLK_DIV_2   0x04
#define WM8900_BCLK_DIV_3   0x06
#define WM8900_BCLK_DIV_4   0x08
#define WM8900_BCLK_DIV_5_5 0x0a
#define WM8900_BCLK_DIV_6   0x0c
#define WM8900_BCLK_DIV_8   0x0e
#define WM8900_BCLK_DIV_11  0x10
#define WM8900_BCLK_DIV_12  0x12
#define WM8900_BCLK_DIV_16  0x14
#define WM8900_BCLK_DIV_22  0x16
#define WM8900_BCLK_DIV_24  0x18
#define WM8900_BCLK_DIV_32  0x1a
#define WM8900_BCLK_DIV_44  0x1c
#define WM8900_BCLK_DIV_48  0x1e

#define WM8900_ADC_CLKDIV_1   0x00
#define WM8900_ADC_CLKDIV_1_5 0x20
#define WM8900_ADC_CLKDIV_2   0x40
#define WM8900_ADC_CLKDIV_3   0x60
#define WM8900_ADC_CLKDIV_4   0x80
#define WM8900_ADC_CLKDIV_5_5 0xa0
#define WM8900_ADC_CLKDIV_6   0xc0

#define WM8900_DAC_CLKDIV_1   0x00
#define WM8900_DAC_CLKDIV_1_5 0x04
#define WM8900_DAC_CLKDIV_2   0x08
#define WM8900_DAC_CLKDIV_3   0x0c
#define WM8900_DAC_CLKDIV_4   0x10
#define WM8900_DAC_CLKDIV_5_5 0x14
#define WM8900_DAC_CLKDIV_6   0x18

extern struct snd_soc_dai wm8900_dai;
extern struct snd_soc_codec_device soc_codec_dev_wm8900;

#endif
