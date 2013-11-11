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

struct asoc_simple_dai {
	const char *name;
	unsigned int fmt;
	unsigned int sysclk;
};

struct asoc_simple_card_info {
	const char *name;
	const char *card;
	const char *codec;
	const char *platform;

	unsigned int daifmt;
	struct asoc_simple_dai cpu_dai;
	struct asoc_simple_dai codec_dai;

	/* used in simple-card.c */
	struct snd_soc_dai_link snd_link;
	struct snd_soc_card snd_card;
};

#endif /* __SIMPLE_CARD_H */
