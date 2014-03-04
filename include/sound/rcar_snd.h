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
#define RSND_GEN1_SSI	2

#define RSND_GEN2_SCU	0
#define RSND_GEN2_ADG	1
#define RSND_GEN2_SSIU	2
#define RSND_GEN2_SSI	3

#define RSND_BASE_MAX	4

/*
 * flags
 *
 * 0xAB000000
 *
 * A : clock sharing settings
 * B : SSI direction
 */
#define RSND_SSI_CLK_PIN_SHARE		(1 << 31)
#define RSND_SSI_PLAY			(1 << 24)

#define RSND_SSI(_dma_id, _pio_irq, _flags)		\
{ .dma_id = _dma_id, .pio_irq = _pio_irq, .flags = _flags }
#define RSND_SSI_SET(_dai_id, _dma_id, _pio_irq, _flags)	\
{ .dai_id = _dai_id, .dma_id = _dma_id, .pio_irq = _pio_irq, .flags = _flags }
#define RSND_SSI_UNUSED \
{ .dai_id = -1, .dma_id = -1, .pio_irq = -1, .flags = 0 }

struct rsnd_ssi_platform_info {
	int dai_id;	/* will be removed */
	int dma_id;
	int pio_irq;
	u32 flags;
};

/*
 * flags
 */
#define RSND_SCU_USE_HPBIF		(1 << 31) /* it needs RSND_SSI_DEPENDENT */

#define RSND_SRC(rate, _dma_id)						\
{ .flags = RSND_SCU_USE_HPBIF, .convert_rate = rate, .dma_id = _dma_id, }
#define RSND_SRC_SET(rate, _dma_id)		\
	{ .flags = RSND_SCU_USE_HPBIF, .convert_rate = rate, .dma_id = _dma_id, }
#define RSND_SRC_UNUSED				\
	{ .flags = 0, .convert_rate = 0, .dma_id = 0, }

#define rsnd_scu_platform_info	rsnd_src_platform_info
#define src_info		scu_info
#define src_info_nr		scu_info_nr

struct rsnd_src_platform_info {
	u32 flags;
	u32 convert_rate; /* sampling rate convert */
	int dma_id; /* for Gen2 SCU */
};

struct rsnd_dai_path_info {
	struct rsnd_ssi_platform_info *ssi;
	struct rsnd_src_platform_info *src;
};

struct rsnd_dai_platform_info {
	struct rsnd_dai_path_info playback;
	struct rsnd_dai_path_info capture;
};

/*
 * flags
 *
 * 0x0000000A
 *
 * A : generation
 */
#define RSND_GEN_MASK	(0xF << 0)
#define RSND_GEN1	(1 << 0) /* fixme */
#define RSND_GEN2	(2 << 0) /* fixme */

struct rcar_snd_info {
	u32 flags;
	struct rsnd_ssi_platform_info *ssi_info;
	int ssi_info_nr;
	struct rsnd_src_platform_info *src_info;
	int src_info_nr;
	struct rsnd_dai_platform_info *dai_info;
	int dai_info_nr;
	int (*start)(int id);
	int (*stop)(int id);
};

#endif
