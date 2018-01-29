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

#define PM860X_PCM_IFACE_1		0xb0
#define PM860X_PCM_IFACE_2		0xb1
#define PM860X_PCM_IFACE_3		0xb2
#define PM860X_PCM_RATE			0xb3
#define PM860X_EC_PATH			0xb4
#define PM860X_SIDETONE_L_GAIN		0xb5
#define PM860X_SIDETONE_R_GAIN		0xb6
#define PM860X_SIDETONE_SHIFT		0xb7
#define PM860X_ADC_OFFSET_1		0xb8
#define PM860X_ADC_OFFSET_2		0xb9
#define PM860X_DMIC_DELAY		0xba

#define PM860X_I2S_IFACE_1		0xbb
#define PM860X_I2S_IFACE_2		0xbc
#define PM860X_I2S_IFACE_3		0xbd
#define PM860X_I2S_IFACE_4		0xbe
#define PM860X_EQUALIZER_N0_1		0xbf
#define PM860X_EQUALIZER_N0_2		0xc0
#define PM860X_EQUALIZER_N1_1		0xc1
#define PM860X_EQUALIZER_N1_2		0xc2
#define PM860X_EQUALIZER_D1_1		0xc3
#define PM860X_EQUALIZER_D1_2		0xc4
#define PM860X_LOFI_GAIN_LEFT		0xc5
#define PM860X_LOFI_GAIN_RIGHT		0xc6
#define PM860X_HIFIL_GAIN_LEFT		0xc7
#define PM860X_HIFIL_GAIN_RIGHT		0xc8
#define PM860X_HIFIR_GAIN_LEFT		0xc9
#define PM860X_HIFIR_GAIN_RIGHT		0xca
#define PM860X_DAC_OFFSET		0xcb
#define PM860X_OFFSET_LEFT_1		0xcc
#define PM860X_OFFSET_LEFT_2		0xcd
#define PM860X_OFFSET_RIGHT_1		0xce
#define PM860X_OFFSET_RIGHT_2		0xcf
#define PM860X_ADC_ANA_1		0xd0
#define PM860X_ADC_ANA_2		0xd1
#define PM860X_ADC_ANA_3		0xd2
#define PM860X_ADC_ANA_4		0xd3
#define PM860X_ANA_TO_ANA		0xd4
#define PM860X_HS1_CTRL			0xd5
#define PM860X_HS2_CTRL			0xd6
#define PM860X_LO1_CTRL			0xd7
#define PM860X_LO2_CTRL			0xd8
#define PM860X_EAR_CTRL_1		0xd9
#define PM860X_EAR_CTRL_2		0xda
#define PM860X_AUDIO_SUPPLIES_1		0xdb
#define PM860X_AUDIO_SUPPLIES_2		0xdc
#define PM860X_ADC_EN_1			0xdd
#define PM860X_ADC_EN_2			0xde
#define PM860X_DAC_EN_1			0xdf
#define PM860X_DAC_EN_2			0xe1
#define PM860X_AUDIO_CAL_1		0xe2
#define PM860X_AUDIO_CAL_2		0xe3
#define PM860X_AUDIO_CAL_3		0xe4
#define PM860X_AUDIO_CAL_4		0xe5
#define PM860X_AUDIO_CAL_5		0xe6
#define PM860X_ANA_INPUT_SEL_1		0xe7
#define PM860X_ANA_INPUT_SEL_2		0xe8

#define PM860X_PCM_IFACE_4		0xe9
#define PM860X_I2S_IFACE_5		0xea

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

extern int pm860x_hs_jack_detect(struct snd_soc_component *, struct snd_soc_jack *,
				 int, int, int, int);
extern int pm860x_mic_jack_detect(struct snd_soc_component *, struct snd_soc_jack *,
				  int);

#endif	/* __88PM860X_H */
