/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef _MESON_SND_CARD_H
#define _MESON_SND_CARD_H

struct device_node;
struct platform_device;

struct snd_soc_card;
struct snd_pcm_substream;
struct snd_pcm_hw_params;

#define DT_PREFIX "amlogic,"

struct meson_card_match_data {
	int (*add_link)(struct snd_soc_card *card,
			struct device_node *node,
			int *index);
};

struct meson_card {
	const struct meson_card_match_data *match_data;
	struct snd_soc_card card;
	void **link_data;
};

unsigned int meson_card_parse_daifmt(struct device_node *node,
				     struct device_node *cpu_node);

int meson_card_i2s_set_sysclk(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      unsigned int mclk_fs);

int meson_card_reallocate_links(struct snd_soc_card *card,
				unsigned int num_links);
int meson_card_parse_dai(struct snd_soc_card *card,
			 struct device_node *node,
			 struct device_node **dai_of_node,
			 const char **dai_name);
int meson_card_set_be_link(struct snd_soc_card *card,
			   struct snd_soc_dai_link *link,
			   struct device_node *node);
int meson_card_set_fe_link(struct snd_soc_card *card,
			   struct snd_soc_dai_link *link,
			   struct device_node *node,
			   bool is_playback);

int meson_card_probe(struct platform_device *pdev);
int meson_card_remove(struct platform_device *pdev);

#endif /* _MESON_SND_CARD_H */
