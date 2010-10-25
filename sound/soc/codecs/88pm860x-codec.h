/*
 * 88pm860x-codec.h -- 88PM860x ALSA SoC Audio Driver
 *
 * Copyright 2010 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __88PM860X_H
#define __88PM860X_H

/* The offset of these registers are 0xb0 */
#define PM860X_PCM_IFACE_1		0x00
#define PM860X_PCM_IFACE_2		0x01
#define PM860X_PCM_IFACE_3		0x02
#define PM860X_PCM_RATE			0x03
#define PM860X_EC_PATH			0x04
#define PM860X_SIDETONE_L_GAIN		0x05
#define PM860X_SIDETONE_R_GAIN		0x06
#define PM860X_SIDETONE_SHIFT		0x07
#define PM860X_ADC_OFFSET_1		0x08
#define PM860X_ADC_OFFSET_2		0x09
#define PM860X_DMIC_DELAY		0x0a

#define PM860X_I2S_IFACE_1		0x0b
#define PM860X_I2S_IFACE_2		0x0c
#define PM860X_I2S_IFACE_3		0x0d
#define PM860X_I2S_IFACE_4		0x0e
#define PM860X_EQUALIZER_N0_1		0x0f
#define PM860X_EQUALIZER_N0_2		0x10
#define PM860X_EQUALIZER_N1_1		0x11
#define PM860X_EQUALIZER_N1_2		0x12
#define PM860X_EQUALIZER_D1_1		0x13
#define PM860X_EQUALIZER_D1_2		0x14
#define PM860X_LOFI_GAIN_LEFT		0x15
#define PM860X_LOFI_GAIN_RIGHT		0x16
#define PM860X_HIFIL_GAIN_LEFT		0x17
#define PM860X_HIFIL_GAIN_RIGHT		0x18
#define PM860X_HIFIR_GAIN_LEFT		0x19
#define PM860X_HIFIR_GAIN_RIGHT		0x1a
#define PM860X_DAC_OFFSET		0x1b
#define PM860X_OFFSET_LEFT_1		0x1c
#define PM860X_OFFSET_LEFT_2		0x1d
#define PM860X_OFFSET_RIGHT_1		0x1e
#define PM860X_OFFSET_RIGHT_2		0x1f
#define PM860X_ADC_ANA_1		0x20
#define PM860X_ADC_ANA_2		0x21
#define PM860X_ADC_ANA_3		0x22
#define PM860X_ADC_ANA_4		0x23
#define PM860X_ANA_TO_ANA		0x24
#define PM860X_HS1_CTRL			0x25
#define PM860X_HS2_CTRL			0x26
#define PM860X_LO1_CTRL			0x27
#define PM860X_LO2_CTRL			0x28
#define PM860X_EAR_CTRL_1		0x29
#define PM860X_EAR_CTRL_2		0x2a
#define PM860X_AUDIO_SUPPLIES_1		0x2b
#define PM860X_AUDIO_SUPPLIES_2		0x2c
#define PM860X_ADC_EN_1			0x2d
#define PM860X_ADC_EN_2			0x2e
#define PM860X_DAC_EN_1			0x2f
#define PM860X_DAC_EN_2			0x31
#define PM860X_AUDIO_CAL_1		0x32
#define PM860X_AUDIO_CAL_2		0x33
#define PM860X_AUDIO_CAL_3		0x34
#define PM860X_AUDIO_CAL_4		0x35
#define PM860X_AUDIO_CAL_5		0x36
#define PM860X_ANA_INPUT_SEL_1		0x37
#define PM860X_ANA_INPUT_SEL_2		0x38

#define PM860X_PCM_IFACE_4		0x39
#define PM860X_I2S_IFACE_5		0x3a

#define PM860X_SHORTS			0x3b
#define PM860X_PLL_ADJ_1		0x3c
#define PM860X_PLL_ADJ_2		0x3d

/* bits definition */
#define PM860X_CLK_DIR_IN		0
#define PM860X_CLK_DIR_OUT		1

#define PM860X_DET_HEADSET		(1 << 0)
#define PM860X_DET_MIC			(1 << 1)
#define PM860X_DET_HOOK			(1 << 2)
#define PM860X_SHORT_HEADSET		(1 << 3)
#define PM860X_SHORT_LINEOUT		(1 << 4)
#define PM860X_DET_MASK			0x1F

extern int pm860x_hs_jack_detect(struct snd_soc_codec *, struct snd_soc_jack *,
				 int, int, int, int);
extern int pm860x_mic_jack_detect(struct snd_soc_codec *, struct snd_soc_jack *,
				  int);

#endif	/* __88PM860X_H */
