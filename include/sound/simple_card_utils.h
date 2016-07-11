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

#endif /* __SIMPLE_CARD_CORE_H */
