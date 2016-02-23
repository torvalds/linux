/*
 * Audio Codec driver supporting:
 *  AD1835A, AD1836, AD1837A, AD1838A, AD1839A
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __AD1836_H__
#define __AD1836_H__

#define AD1836_DAC_CTRL1               0
#define AD1836_DAC_POWERDOWN           2
#define AD1836_DAC_SERFMT_MASK         0xE0
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
#define AD1836_ADC_WORD_OFFSET         4
#define AD1836_ADC_SERFMT_MASK         (7 << 6)
#define AD1836_ADC_SERFMT_PCK256       (0x4 << 6)
#define AD1836_ADC_SERFMT_PCK128       (0x5 << 6)
#define AD1836_ADC_AUX                 (0x6 << 6)

#define AD1836_ADC_CTRL3               14

#define AD1836_NUM_REGS                16

#define AD1836_WORD_LEN_24 0x0
#define AD1836_WORD_LEN_20 0x1
#define AD1836_WORD_LEN_16 0x2

#endif
