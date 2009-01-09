/*
 * PCM3008 ALSA SoC Layer
 *
 * Author:	Hugo Villeneuve
 * Copyright (C) 2008 Lyrtech inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_SOC_PCM3008_H
#define __LINUX_SND_SOC_PCM3008_H

struct pcm3008_setup_data {
	unsigned dem0_pin;
	unsigned dem1_pin;
	unsigned pdad_pin;
	unsigned pdda_pin;
};

extern struct snd_soc_codec_device soc_codec_dev_pcm3008;
extern struct snd_soc_dai pcm3008_dai;

#endif
