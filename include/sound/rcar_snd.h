/*
 * Renesas R-Car SRU/SCU/SSIU/SSI support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RCAR_SND_H
#define RCAR_SND_H

#include <linux/sh_clk.h>

#define RSND_GEN1_SRU	0
#define RSND_GEN1_ADG	1

#define RSND_GEN2_SRU	0
#define RSND_GEN2_ADG	1

#define RSND_BASE_MAX	2

struct rsnd_scu_platform_info {
	u32 flags;
};

struct rsnd_dai_platform_info {
	int ssi_id_playback;
	int ssi_id_capture;
};

/*
 * flags
 *
 * 0x0000000A
 *
 * A : generation
 */
#define RSND_GEN1	(1 << 0) /* fixme */
#define RSND_GEN2	(2 << 0) /* fixme */

struct rcar_snd_info {
	u32 flags;
	struct rsnd_scu_platform_info *scu_info;
	int scu_info_nr;
	struct rsnd_dai_platform_info *dai_info;
	int dai_info_nr;
	int (*start)(int id);
	int (*stop)(int id);
};

#endif
