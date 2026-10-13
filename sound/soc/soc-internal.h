/* SPDX-License-Identifier: GPL-2.0-only
 *
 * soc-internal.h
 *
 * Copyright (c) 2026 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#ifndef __SOC_INTERNAL_H
#define __SOC_INTERNAL_H

/*
 * This header is for ALSA SoC Framework internal, not for Vender drivers.
 * The ALSA SoC functions for Vender drivers are defined in linux/include/sound/xxx.h
 * as snd_soc_xxx();
 */

/*
 * In soc-core
 */
extern struct mutex client_mutex;

char *snd_soc_fmt_single_name(struct device *dev, int *id);
char *snd_soc_fmt_multiple_name(struct device *dev, struct snd_soc_dai_driver *dai_drv);
int snd_soc_add_controls(struct snd_card *card, struct device *dev,
			 const struct snd_kcontrol_new *controls, int num_controls,
			 const char *prefix, void *data);

/*
 * In soc-dai
 */
void snd_soc_dai_symmetric_set_params(struct snd_soc_dai *dai,
				      struct snd_pcm_hw_params *params);
int snd_soc_dai_symmetric_apply(struct snd_pcm_substream *substream, struct snd_soc_dai *dai);
int snd_soc_dai_symmetric_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);
void snd_soc_dai_symmetric_update(struct snd_pcm_substream *substream);
struct clk *snd_soc_dai_get_bclk(struct snd_soc_dai *dai);
unsigned int snd_soc_dai_get_bclk_ratio(struct snd_soc_dai *dai);
void snd_soc_dai_active_update(struct snd_soc_dai *dai, int stream, int action);

#endif /* __SOC_INTERNAL_H */
