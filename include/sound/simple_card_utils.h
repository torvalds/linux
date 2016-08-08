/*
 * simple_card_core.h
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SIMPLE_CARD_CORE_H
#define __SIMPLE_CARD_CORE_H

#include <sound/soc.h>

struct asoc_simple_dai {
	const char *name;
	unsigned int sysclk;
	int slots;
	int slot_width;
	unsigned int tx_slot_mask;
	unsigned int rx_slot_mask;
	struct clk *clk;
};

int asoc_simple_card_parse_daifmt(struct device *dev,
				  struct device_node *node,
				  struct device_node *codec,
				  char *prefix,
				  unsigned int *retfmt);
int asoc_simple_card_set_dailink_name(struct device *dev,
				      struct snd_soc_dai_link *dai_link,
				      const char *fmt, ...);
int asoc_simple_card_parse_card_name(struct snd_soc_card *card,
				     char *prefix);

#define asoc_simple_card_parse_clk_cpu(node, dai_link, simple_dai)		\
	asoc_simple_card_parse_clk(node, dai_link->cpu_of_node, simple_dai)
#define asoc_simple_card_parse_clk_codec(node, dai_link, simple_dai)		\
	asoc_simple_card_parse_clk(node, dai_link->codec_of_node, simple_dai)
int asoc_simple_card_parse_clk(struct device_node *node,
			       struct device_node *dai_of_node,
			       struct asoc_simple_dai *simple_dai);

#endif /* __SIMPLE_CARD_CORE_H */
