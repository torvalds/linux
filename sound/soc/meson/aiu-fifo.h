/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2020 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef _MESON_AIU_FIFO_H
#define _MESON_AIU_FIFO_H

struct snd_pcm_hardware;
struct snd_soc_component_driver;
struct snd_soc_dai_driver;
struct clk;
struct snd_pcm_ops;
struct snd_pcm_substream;
struct snd_soc_dai;
struct snd_pcm_hw_params;
struct platform_device;

struct aiu_fifo {
	struct snd_pcm_hardware *pcm;
	unsigned int mem_offset;
	unsigned int fifo_block;
	struct clk *pclk;
	int irq;
};

int aiu_fifo_dai_probe(struct snd_soc_dai *dai);
int aiu_fifo_dai_remove(struct snd_soc_dai *dai);

snd_pcm_uframes_t aiu_fifo_pointer(struct snd_soc_component *component,
				   struct snd_pcm_substream *substream);

int aiu_fifo_trigger(struct snd_pcm_substream *substream, int cmd,
		     struct snd_soc_dai *dai);
int aiu_fifo_prepare(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai);
int aiu_fifo_hw_params(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *params,
		       struct snd_soc_dai *dai);
int aiu_fifo_hw_free(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai);
int aiu_fifo_startup(struct snd_pcm_substream *substream,
		     struct snd_soc_dai *dai);
void aiu_fifo_shutdown(struct snd_pcm_substream *substream,
		       struct snd_soc_dai *dai);
int aiu_fifo_pcm_new(struct snd_soc_pcm_runtime *rtd,
		     struct snd_soc_dai *dai);

#endif /* _MESON_AIU_FIFO_H */
