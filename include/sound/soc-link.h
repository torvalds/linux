/* SPDX-License-Identifier: GPL-2.0
 *
 * soc-link.h
 *
 * Copyright (C) 2019 Renesas Electronics Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#ifndef __SOC_LINK_H
#define __SOC_LINK_H

int snd_soc_link_init(struct snd_soc_pcm_runtime *rtd);
int snd_soc_link_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				    struct snd_pcm_hw_params *params);

int snd_soc_link_startup(struct snd_pcm_substream *substream);
void snd_soc_link_shutdown(struct snd_pcm_substream *substream);
int snd_soc_link_prepare(struct snd_pcm_substream *substream);
int snd_soc_link_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params);
void snd_soc_link_hw_free(struct snd_pcm_substream *substream);
int snd_soc_link_trigger(struct snd_pcm_substream *substream, int cmd);

int snd_soc_link_compr_startup(struct snd_compr_stream *cstream);
void snd_soc_link_compr_shutdown(struct snd_compr_stream *cstream);
int snd_soc_link_compr_set_params(struct snd_compr_stream *cstream);

#endif /* __SOC_LINK_H */
