/*
 * ASoC simple sound card support
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SIMPLE_CARD_H
#define __SIMPLE_CARD_H

#include <sound/soc.h>

struct asoc_simple_dai_init_info {
	unsigned int fmt;
	unsigned int cpu_daifmt;
	unsigned int codec_daifmt;
	unsigned int sysclk;
};

struct asoc_simple_card_info {
	const char *name;
	const char *card;
	const char *cpu_dai;
	const char *codec;
	const char *platform;
	const char *codec_dai;
	struct asoc_simple_dai_init_info *init; /* for snd_link.init */

	/* used in simple-card.c */
	struct snd_soc_dai_link snd_link;
	struct snd_soc_card snd_card;
};

#endif /* __SIMPLE_CARD_H */
