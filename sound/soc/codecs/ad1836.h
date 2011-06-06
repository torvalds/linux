/*
 * File:         sound/soc/codecs/ad1836.h
 * Based on:
 * Author:       Barry Song <Barry.Song@analog.com>
 *
 * Created:      Aug 04, 2009
 * Description:  definitions for AD1836 registers
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __AD1836_H__
#define __AD1836_H__

#define AD1836_DAC_CTRL1               0
#define AD1836_DAC_POWERDOWN           2
#define AD1836_DAC_SERFMT_MASK	       0xE0
#define AD1836_DAC_SERFMT_PCK256       (0x4 << 5)
#define AD1836_DAC_SERFMT_PCK128       (0x5 << 5)
#define AD1836_DAC_WORD_LEN_MASK       0x18
#define AD1836_DAC_WORD_LEN_OFFSET     3

#define AD1836_DAC_CTRL2               1

/* These macros are one-based. So AD183X_MUTE_LEFT(1) will return the mute bit
 * for the first ADC/DAC */
#define AD1836_MUTE_LEFT(x) (((x) * 2) - 2)
#define AD1836_MUTE_RIGHT(x) (((x) * 2) - 1)

#define AD1836_DAC_L_VOL(x) ((x) * 2)
#define AD1836_DAC_R_VOL(x) (1 + ((x) * 2))

#define AD1836_ADC_CTRL1               12
#define AD1836_ADC_POWERDOWN           7
#define AD1836_ADC_HIGHPASS_FILTER     8

#define AD1836_ADC_CTRL2               13
#define AD1836_ADC_WORD_LEN_MASK       0x30
#define AD1836_ADC_WORD_OFFSET         5
#define AD1836_ADC_SERFMT_MASK	       (7 << 6)
#define AD1836_ADC_SERFMT_PCK256       (0x4 << 6)
#define AD1836_ADC_SERFMT_PCK128       (0x5 << 6)
#define AD1836_ADC_AUX                 (0x6 << 6)

#define AD1836_ADC_CTRL3               14

#define AD1836_NUM_REGS                16

#define AD1836_WORD_LEN_24 0x0
#define AD1836_WORD_LEN_20 0x1
#define AD1836_WORD_LEN_16 0x2

#endif
