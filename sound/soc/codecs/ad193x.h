/*
 * AD193X Audio Codec driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __AD193X_H__
#define __AD193X_H__

#define AD193X_PLL_CLK_CTRL0    0x800
#define AD193X_PLL_POWERDOWN           0x01
#define AD193X_PLL_INPUT_MASK   (~0x6)
#define AD193X_PLL_INPUT_256    (0 << 1)
#define AD193X_PLL_INPUT_384    (1 << 1)
#define AD193X_PLL_INPUT_512    (2 << 1)
#define AD193X_PLL_INPUT_768    (3 << 1)
#define AD193X_PLL_CLK_CTRL1    0x801
#define AD193X_DAC_CTRL0        0x802
#define AD193X_DAC_POWERDOWN           0x01
#define AD193X_DAC_SERFMT_MASK		0xC0
#define AD193X_DAC_SERFMT_STEREO	(0 << 6)
#define AD193X_DAC_SERFMT_TDM		(1 << 6)
#define AD193X_DAC_CTRL1        0x803
#define AD193X_DAC_2_CHANNELS   0
#define AD193X_DAC_4_CHANNELS   1
#define AD193X_DAC_8_CHANNELS   2
#define AD193X_DAC_16_CHANNELS  3
#define AD193X_DAC_CHAN_SHFT    1
#define AD193X_DAC_CHAN_MASK    (3 << AD193X_DAC_CHAN_SHFT)
#define AD193X_DAC_LCR_MASTER   (1 << 4)
#define AD193X_DAC_BCLK_MASTER  (1 << 5)
#define AD193X_DAC_LEFT_HIGH    (1 << 3)
#define AD193X_DAC_BCLK_INV     (1 << 7)
#define AD193X_DAC_CTRL2        0x804
#define AD193X_DAC_WORD_LEN_SHFT        3
#define AD193X_DAC_WORD_LEN_MASK        0x18
#define AD193X_DAC_MASTER_MUTE  1
#define AD193X_DAC_CHNL_MUTE    0x805
#define AD193X_DACL1_MUTE       0
#define AD193X_DACR1_MUTE       1
#define AD193X_DACL2_MUTE       2
#define AD193X_DACR2_MUTE       3
#define AD193X_DACL3_MUTE       4
#define AD193X_DACR3_MUTE       5
#define AD193X_DACL4_MUTE       6
#define AD193X_DACR4_MUTE       7
#define AD193X_DAC_L1_VOL       0x806
#define AD193X_DAC_R1_VOL       0x807
#define AD193X_DAC_L2_VOL       0x808
#define AD193X_DAC_R2_VOL       0x809
#define AD193X_DAC_L3_VOL       0x80a
#define AD193X_DAC_R3_VOL       0x80b
#define AD193X_DAC_L4_VOL       0x80c
#define AD193X_DAC_R4_VOL       0x80d
#define AD193X_ADC_CTRL0        0x80e
#define AD193X_ADC_POWERDOWN           0x01
#define AD193X_ADC_HIGHPASS_FILTER	1
#define AD193X_ADCL1_MUTE 		2
#define AD193X_ADCR1_MUTE 		3
#define AD193X_ADCL2_MUTE 		4
#define AD193X_ADCR2_MUTE 		5
#define AD193X_ADC_CTRL1        0x80f
#define AD193X_ADC_SERFMT_MASK		0x60
#define AD193X_ADC_SERFMT_STEREO	(0 << 5)
#define AD193X_ADC_SERFMT_TDM		(1 << 5)
#define AD193X_ADC_SERFMT_AUX		(2 << 5)
#define AD193X_ADC_WORD_LEN_MASK	0x3
#define AD193X_ADC_CTRL2        0x810
#define AD193X_ADC_2_CHANNELS   0
#define AD193X_ADC_4_CHANNELS   1
#define AD193X_ADC_8_CHANNELS   2
#define AD193X_ADC_16_CHANNELS  3
#define AD193X_ADC_CHAN_SHFT    4
#define AD193X_ADC_CHAN_MASK    (3 << AD193X_ADC_CHAN_SHFT)
#define AD193X_ADC_LCR_MASTER   (1 << 3)
#define AD193X_ADC_BCLK_MASTER  (1 << 6)
#define AD193X_ADC_LEFT_HIGH    (1 << 2)
#define AD193X_ADC_BCLK_INV     (1 << 1)

#define AD193X_NUM_REGS          17

#endif
