/*
 * OF helpers for ALSA SoC
 *
 * Copyright (C) 2008, Secret Lab Technologies Ltd.
 */

#ifndef _INCLUDE_SOC_OF_H_
#define _INCLUDE_SOC_OF_H_

#if defined(CONFIG_SND_SOC_OF_SIMPLE) || defined(CONFIG_SND_SOC_OF_SIMPLE_MODULE)

#include <linux/of.h>
#include <sound/soc.h>

int of_snd_soc_register_codec(struct snd_soc_codec_device *codec_dev,
			      void *codec_data, struct snd_soc_dai *dai,
			      struct device_node *node);

int of_snd_soc_register_platform(struct snd_soc_platform *platform,
				 struct device_node *node,
				 struct snd_soc_dai *cpu_dai);

#endif

#endif /* _INCLUDE_SOC_OF_H_ */
