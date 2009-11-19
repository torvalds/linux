/*
 * soc-util.c  --  ALSA SoC Audio Layer utility functions
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *         Liam Girdwood <lrg@slimlogic.co.uk>
 *         
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

int snd_soc_calc_frame_size(int sample_size, int channels, int tdm_slots)
{
	return sample_size * channels * tdm_slots;
}
EXPORT_SYMBOL_GPL(snd_soc_calc_frame_size);

int snd_soc_params_to_frame_size(struct snd_pcm_hw_params *params)
{
	int sample_size;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
		sample_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
		sample_size = 20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
		sample_size = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_S32_BE:
		sample_size = 32;
		break;
	default:
		return -ENOTSUPP;
	}

	return snd_soc_calc_frame_size(sample_size, params_channels(params),
				       1);
}
EXPORT_SYMBOL_GPL(snd_soc_params_to_frame_size);

int snd_soc_calc_bclk(int fs, int sample_size, int channels, int tdm_slots)
{
	return fs * snd_soc_calc_frame_size(sample_size, channels, tdm_slots);
}
EXPORT_SYMBOL_GPL(snd_soc_calc_bclk);

int snd_soc_params_to_bclk(struct snd_pcm_hw_params *params)
{
	int ret;

	ret = snd_soc_params_to_frame_size(params);

	if (ret > 0)
		return ret * params_rate(params);
	else
		return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_params_to_bclk);
