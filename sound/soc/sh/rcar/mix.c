/*
 * mix.c
 *
 * Copyright (c) 2015 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

#define MIX_NAME_SIZE	16
#define MIX_NAME "mix"

struct rsnd_mix {
	struct rsnd_mix_platform_info *info; /* rcar_snd.h */
	struct rsnd_mod mod;
};

#define rsnd_mix_nr(priv) ((priv)->mix_nr)
#define for_each_rsnd_mix(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_mix_nr(priv)) &&				\
		     ((pos) = (struct rsnd_mix *)(priv)->mix + i);	\
	     i++)


static void rsnd_mix_soft_reset(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, MIX_SWRSR, 0);
	rsnd_mod_write(mod, MIX_SWRSR, 1);
}

#define rsnd_mix_initialize_lock(mod)	__rsnd_mix_initialize_lock(mod, 1)
#define rsnd_mix_initialize_unlock(mod)	__rsnd_mix_initialize_lock(mod, 0)
static void __rsnd_mix_initialize_lock(struct rsnd_mod *mod, u32 enable)
{
	rsnd_mod_write(mod, MIX_MIXIR, enable);
}

static void rsnd_mix_volume_update(struct rsnd_dai_stream *io,
				  struct rsnd_mod *mod)
{

	/* Disable MIX dB setting */
	rsnd_mod_write(mod, MIX_MDBER, 0);

	rsnd_mod_write(mod, MIX_MDBAR, 0);
	rsnd_mod_write(mod, MIX_MDBBR, 0);
	rsnd_mod_write(mod, MIX_MDBCR, 0);
	rsnd_mod_write(mod, MIX_MDBDR, 0);

	/* Enable MIX dB setting */
	rsnd_mod_write(mod, MIX_MDBER, 1);
}

static int rsnd_mix_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_hw_start(mod);

	rsnd_mix_soft_reset(mod);

	rsnd_mix_initialize_lock(mod);

	rsnd_mod_write(mod, MIX_ADINR, rsnd_get_adinr_chan(mod, io));

	rsnd_path_parse(priv, io);

	/* volume step */
	rsnd_mod_write(mod, MIX_MIXMR, 0);
	rsnd_mod_write(mod, MIX_MVPDR, 0);

	rsnd_mix_volume_update(io, mod);

	rsnd_mix_initialize_unlock(mod);

	return 0;
}

static int rsnd_mix_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_hw_stop(mod);

	return 0;
}

static struct rsnd_mod_ops rsnd_mix_ops = {
	.name		= MIX_NAME,
	.init		= rsnd_mix_init,
	.quit		= rsnd_mix_quit,
};

struct rsnd_mod *rsnd_mix_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_mix_nr(priv)))
		id = 0;

	return &((struct rsnd_mix *)(priv->mix) + id)->mod;
}

static void rsnd_of_parse_mix(struct platform_device *pdev,
			      const struct rsnd_of_data *of_data,
			      struct rsnd_priv *priv)
{
	struct device_node *node;
	struct rsnd_mix_platform_info *mix_info;
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct device *dev = &pdev->dev;
	int nr;

	if (!of_data)
		return;

	node = of_get_child_by_name(dev->of_node, "rcar_sound,mix");
	if (!node)
		return;

	nr = of_get_child_count(node);
	if (!nr)
		goto rsnd_of_parse_mix_end;

	mix_info = devm_kzalloc(dev,
				sizeof(struct rsnd_mix_platform_info) * nr,
				GFP_KERNEL);
	if (!mix_info) {
		dev_err(dev, "mix info allocation error\n");
		goto rsnd_of_parse_mix_end;
	}

	info->mix_info		= mix_info;
	info->mix_info_nr	= nr;

rsnd_of_parse_mix_end:
	of_node_put(node);

}

int rsnd_mix_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv)
{
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_mix *mix;
	struct clk *clk;
	char name[MIX_NAME_SIZE];
	int i, nr, ret;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv)) {
		dev_warn(dev, "MIX is not supported on Gen1\n");
		return -EINVAL;
	}

	rsnd_of_parse_mix(pdev, of_data, priv);

	nr = info->mix_info_nr;
	if (!nr)
		return 0;

	mix	= devm_kzalloc(dev, sizeof(*mix) * nr, GFP_KERNEL);
	if (!mix)
		return -ENOMEM;

	priv->mix_nr	= nr;
	priv->mix	= mix;

	for_each_rsnd_mix(mix, priv, i) {
		snprintf(name, MIX_NAME_SIZE, "%s.%d",
			 MIX_NAME, i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		mix->info = &info->mix_info[i];

		ret = rsnd_mod_init(priv, &mix->mod, &rsnd_mix_ops,
				    clk, RSND_MOD_MIX, i);
		if (ret)
			return ret;
	}

	return 0;
}

void rsnd_mix_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
	struct rsnd_mix *mix;
	int i;

	for_each_rsnd_mix(mix, priv, i) {
		rsnd_mod_quit(&mix->mod);
	}
}
