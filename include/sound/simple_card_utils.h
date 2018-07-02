/* SPDX-License-Identifier: GPL-2.0
 *
 * simple_card_utils.h
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */

#ifndef __SIMPLE_CARD_UTILS_H
#define __SIMPLE_CARD_UTILS_H

#include <sound/soc.h>

#define asoc_simple_card_init_hp(card, sjack, prefix) \
	asoc_simple_card_init_jack(card, sjack, 1, prefix)
#define asoc_simple_card_init_mic(card, sjack, prefix) \
	asoc_simple_card_init_jack(card, sjack, 0, prefix)

struct asoc_simple_dai {
	const char *name;
	unsigned int sysclk;
	int clk_direction;
	int slots;
	int slot_width;
	unsigned int tx_slot_mask;
	unsigned int rx_slot_mask;
	struct clk *clk;
};

struct asoc_simple_card_data {
	u32 convert_rate;
	u32 convert_channels;
};

struct asoc_simple_jack {
	struct snd_soc_jack jack;
	struct snd_soc_jack_pin pin;
	struct snd_soc_jack_gpio gpio;
};

int asoc_simple_card_parse_daifmt(struct device *dev,
				  struct device_node *node,
				  struct device_node *codec,
				  char *prefix,
				  unsigned int *retfmt);
__printf(3, 4)
int asoc_simple_card_set_dailink_name(struct device *dev,
				      struct snd_soc_dai_link *dai_link,
				      const char *fmt, ...);
int asoc_simple_card_parse_card_name(struct snd_soc_card *card,
				     char *prefix);

#define asoc_simple_card_parse_clk_cpu(dev, node, dai_link, simple_dai)		\
	asoc_simple_card_parse_clk(dev, node, dai_link->cpu_of_node, simple_dai, \
				   dai_link->cpu_dai_name)
#define asoc_simple_card_parse_clk_codec(dev, node, dai_link, simple_dai)	\
	asoc_simple_card_parse_clk(dev, node, dai_link->codec_of_node, simple_dai,\
				   dai_link->codec_dai_name)
int asoc_simple_card_parse_clk(struct device *dev,
			       struct device_node *node,
			       struct device_node *dai_of_node,
			       struct asoc_simple_dai *simple_dai,
			       const char *name);
int asoc_simple_card_clk_enable(struct asoc_simple_dai *dai);
void asoc_simple_card_clk_disable(struct asoc_simple_dai *dai);

#define asoc_simple_card_parse_cpu(node, dai_link,				\
				   list_name, cells_name, is_single_link)	\
	asoc_simple_card_parse_dai(node, &dai_link->cpu_of_node,		\
		&dai_link->cpu_dai_name, list_name, cells_name, is_single_link)
#define asoc_simple_card_parse_codec(node, dai_link, list_name, cells_name)	\
	asoc_simple_card_parse_dai(node, &dai_link->codec_of_node,		\
		&dai_link->codec_dai_name, list_name, cells_name, NULL)
#define asoc_simple_card_parse_platform(node, dai_link, list_name, cells_name)	\
	asoc_simple_card_parse_dai(node, &dai_link->platform_of_node,		\
		NULL, list_name, cells_name, NULL)
int asoc_simple_card_parse_dai(struct device_node *node,
				  struct device_node **endpoint_np,
				  const char **dai_name,
				  const char *list_name,
				  const char *cells_name,
				  int *is_single_links);

#define asoc_simple_card_parse_graph_cpu(ep, dai_link)			\
	asoc_simple_card_parse_graph_dai(ep, &dai_link->cpu_of_node,	\
					 &dai_link->cpu_dai_name)
#define asoc_simple_card_parse_graph_codec(ep, dai_link)		\
	asoc_simple_card_parse_graph_dai(ep, &dai_link->codec_of_node,	\
					 &dai_link->codec_dai_name)
int asoc_simple_card_parse_graph_dai(struct device_node *ep,
				     struct device_node **endpoint_np,
				     const char **dai_name);

#define asoc_simple_card_of_parse_tdm(np, dai)			\
	snd_soc_of_parse_tdm_slot(np,	&(dai)->tx_slot_mask,	\
					&(dai)->rx_slot_mask,	\
					&(dai)->slots,		\
					&(dai)->slot_width);

int asoc_simple_card_init_dai(struct snd_soc_dai *dai,
			      struct asoc_simple_dai *simple_dai);

int asoc_simple_card_canonicalize_dailink(struct snd_soc_dai_link *dai_link);
void asoc_simple_card_canonicalize_cpu(struct snd_soc_dai_link *dai_link,
				      int is_single_links);

int asoc_simple_card_clean_reference(struct snd_soc_card *card);

void asoc_simple_card_convert_fixup(struct asoc_simple_card_data *data,
				      struct snd_pcm_hw_params *params);
void asoc_simple_card_parse_convert(struct device *dev, char *prefix,
				    struct asoc_simple_card_data *data);

int asoc_simple_card_of_parse_routing(struct snd_soc_card *card,
				      char *prefix,
				      int optional);
int asoc_simple_card_of_parse_widgets(struct snd_soc_card *card,
				      char *prefix);

int asoc_simple_card_init_jack(struct snd_soc_card *card,
			       struct asoc_simple_jack *sjack,
			       int is_hp, char *prefix);

#endif /* __SIMPLE_CARD_UTILS_H */
