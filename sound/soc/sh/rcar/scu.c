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

#define rsnd_mod_to_scu(_mod)	\
	container_of((_mod), struct rsnd_scu, mod)

#define for_each_rsnd_scu(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_scu_nr(priv)) &&				\
		     ((pos) = (struct rsnd_scu *)(priv)->scu + i);	\
	     i++)

static int rsnd_scu_init(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "%s.%d init\n", rsnd_mod_name(mod), rsnd_mod_id(mod));

	return 0;
}

static int rsnd_scu_quit(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "%s.%d quit\n", rsnd_mod_name(mod), rsnd_mod_id(mod));

	return 0;
}

static int rsnd_scu_start(struct rsnd_mod *mod,
			  struct rsnd_dai *rdai,
			  struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "%s.%d start\n", rsnd_mod_name(mod), rsnd_mod_id(mod));

	return 0;
}

static int rsnd_scu_stop(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai,
			 struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "%s.%d stop\n", rsnd_mod_name(mod), rsnd_mod_id(mod));

	return 0;
}

static struct rsnd_mod_ops rsnd_scu_ops = {
	.name	= "scu",
	.init	= rsnd_scu_init,
	.quit	= rsnd_scu_quit,
	.start	= rsnd_scu_start,
	.stop	= rsnd_scu_stop,
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
	}

	dev_dbg(dev, "scu probed\n");

	return 0;
}

void rsnd_scu_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
}
