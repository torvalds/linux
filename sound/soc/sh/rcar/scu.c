/*
 * Renesas R-Car SCU support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

struct rsnd_scu {
	struct rsnd_scu_platform_info *info; /* rcar_snd.h */
	struct rsnd_mod mod;
};

#define rsnd_scu_mode_flags(p) ((p)->info->flags)

/*
 * ADINR
 */
#define OTBL_24		(0 << 16)
#define OTBL_22		(2 << 16)
#define OTBL_20		(4 << 16)
#define OTBL_18		(6 << 16)
#define OTBL_16		(8 << 16)


#define rsnd_mod_to_scu(_mod)	\
	container_of((_mod), struct rsnd_scu, mod)

#define for_each_rsnd_scu(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_scu_nr(priv)) &&				\
		     ((pos) = (struct rsnd_scu *)(priv)->scu + i);	\
	     i++)

static int rsnd_scu_set_route(struct rsnd_priv *priv,
			      struct rsnd_mod *mod,
			      struct rsnd_dai *rdai,
			      struct rsnd_dai_stream *io)
{
	struct scu_route_config {
		u32 mask;
		int shift;
	} routes[] = {
		{ 0xF,  0, }, /* 0 */
		{ 0xF,  4, }, /* 1 */
		{ 0xF,  8, }, /* 2 */
		{ 0x7, 12, }, /* 3 */
		{ 0x7, 16, }, /* 4 */
		{ 0x7, 20, }, /* 5 */
		{ 0x7, 24, }, /* 6 */
		{ 0x3, 28, }, /* 7 */
		{ 0x3, 30, }, /* 8 */
	};

	u32 mask;
	u32 val;
	int shift;
	int id;

	/*
	 * Gen1 only
	 */
	if (!rsnd_is_gen1(priv))
		return 0;

	id = rsnd_mod_id(mod);
	if (id < 0 || id > ARRAY_SIZE(routes))
		return -EIO;

	/*
	 * SRC_ROUTE_SELECT
	 */
	val = rsnd_dai_is_play(rdai, io) ? 0x1 : 0x2;
	val = val		<< routes[id].shift;
	mask = routes[id].mask	<< routes[id].shift;

	rsnd_mod_bset(mod, SRC_ROUTE_SEL, mask, val);

	/*
	 * SRC_TIMING_SELECT
	 */
	shift	= (id % 4) * 8;
	mask	= 0x1F << shift;
	if (8 == id) /* SRU8 is very special */
		val = id << shift;
	else
		val = (id + 1) << shift;

	switch (id / 4) {
	case 0:
		rsnd_mod_bset(mod, SRC_TMG_SEL0, mask, val);
		break;
	case 1:
		rsnd_mod_bset(mod, SRC_TMG_SEL1, mask, val);
		break;
	case 2:
		rsnd_mod_bset(mod, SRC_TMG_SEL2, mask, val);
		break;
	}

	return 0;
}

static int rsnd_scu_set_mode(struct rsnd_priv *priv,
			     struct rsnd_mod *mod,
			     struct rsnd_dai *rdai,
			     struct rsnd_dai_stream *io)
{
	int id = rsnd_mod_id(mod);
	u32 val;

	if (rsnd_is_gen1(priv)) {
		val = (1 << id);
		rsnd_mod_bset(mod, SRC_CTRL, val, val);
	}

	return 0;
}

static int rsnd_scu_set_hpbif(struct rsnd_priv *priv,
			      struct rsnd_mod *mod,
			      struct rsnd_dai *rdai,
			      struct rsnd_dai_stream *io)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	u32 adinr = runtime->channels;

	switch (runtime->sample_bits) {
	case 16:
		adinr |= OTBL_16;
		break;
	case 32:
		adinr |= OTBL_24;
		break;
	default:
		return -EIO;
	}

	rsnd_mod_write(mod, BUSIF_MODE, 1);
	rsnd_mod_write(mod, BUSIF_ADINR, adinr);

	return 0;
}

bool rsnd_scu_hpbif_is_enable(struct rsnd_mod *mod)
{
	struct rsnd_scu *scu = rsnd_mod_to_scu(mod);
	u32 flags = rsnd_scu_mode_flags(scu);

	return !!(flags & RSND_SCU_USE_HPBIF);
}

static int rsnd_scu_start(struct rsnd_mod *mod,
			  struct rsnd_dai *rdai,
			  struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	int ret;

	/*
	 * SCU will be used if it has RSND_SCU_USE_HPBIF flags
	 */
	if (!rsnd_scu_hpbif_is_enable(mod)) {
		/* it use PIO transter */
		dev_dbg(dev, "%s%d is not used\n",
			rsnd_mod_name(mod), rsnd_mod_id(mod));

		return 0;
	}

	/* it use DMA transter */
	ret = rsnd_scu_set_route(priv, mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_scu_set_mode(priv, mod, rdai, io);
	if (ret < 0)
		return ret;

	ret = rsnd_scu_set_hpbif(priv, mod, rdai, io);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "%s%d start\n", rsnd_mod_name(mod), rsnd_mod_id(mod));

	return 0;
}

static struct rsnd_mod_ops rsnd_scu_ops = {
	.name	= "scu",
	.start	= rsnd_scu_start,
};

struct rsnd_mod *rsnd_scu_mod_get(struct rsnd_priv *priv, int id)
{
	BUG_ON(id < 0 || id >= rsnd_scu_nr(priv));

	return &((struct rsnd_scu *)(priv->scu) + id)->mod;
}

int rsnd_scu_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_scu *scu;
	int i, nr;

	/*
	 * init SCU
	 */
	nr	= info->scu_info_nr;
	scu	= devm_kzalloc(dev, sizeof(*scu) * nr, GFP_KERNEL);
	if (!scu) {
		dev_err(dev, "SCU allocate failed\n");
		return -ENOMEM;
	}

	priv->scu_nr	= nr;
	priv->scu	= scu;

	for_each_rsnd_scu(scu, priv, i) {
		rsnd_mod_init(priv, &scu->mod,
			      &rsnd_scu_ops, i);
		scu->info = &info->scu_info[i];

		dev_dbg(dev, "SCU%d probed\n", i);
	}
	dev_dbg(dev, "scu probed\n");

	return 0;
}

void rsnd_scu_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
}
