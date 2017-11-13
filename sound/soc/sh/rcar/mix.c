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
	struct rsnd_mod mod;
};

#define rsnd_mix_get(priv, id) ((struct rsnd_mix *)(priv->mix) + id)
#define rsnd_mix_nr(priv) ((priv)->mix_nr)
#define for_each_rsnd_mix(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_mix_nr(priv)) &&				\
		     ((pos) = (struct rsnd_mix *)(priv)->mix + i);	\
	     i++)

static void rsnd_mix_activation(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, MIX_SWRSR, 0);
	rsnd_mod_write(mod, MIX_SWRSR, 1);
}

static void rsnd_mix_halt(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, MIX_MIXIR, 1);
	rsnd_mod_write(mod, MIX_SWRSR, 0);
}

static void rsnd_mix_volume_parameter(struct rsnd_dai_stream *io,
				      struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, MIX_MDBAR, 0);
	rsnd_mod_write(mod, MIX_MDBBR, 0);
	rsnd_mod_write(mod, MIX_MDBCR, 0);
	rsnd_mod_write(mod, MIX_MDBDR, 0);
}

static void rsnd_mix_volume_init(struct rsnd_dai_stream *io,
				 struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, MIX_MIXIR, 1);

	/* General Information */
	rsnd_mod_write(mod, MIX_ADINR, rsnd_runtime_channel_after_ctu(io));

	/* volume step */
	rsnd_mod_write(mod, MIX_MIXMR, 0);
	rsnd_mod_write(mod, MIX_MVPDR, 0);

	/* common volume parameter */
	rsnd_mix_volume_parameter(io, mod);

	rsnd_mod_write(mod, MIX_MIXIR, 0);
}

static void rsnd_mix_volume_update(struct rsnd_dai_stream *io,
				  struct rsnd_mod *mod)
{
	/* Disable MIX dB setting */
	rsnd_mod_write(mod, MIX_MDBER, 0);

	/* common volume parameter */
	rsnd_mix_volume_parameter(io, mod);

	/* Enable MIX dB setting */
	rsnd_mod_write(mod, MIX_MDBER, 1);
}

static int rsnd_mix_probe_(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	return rsnd_cmd_attach(io, rsnd_mod_id(mod));
}

static int rsnd_mix_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_power_on(mod);

	rsnd_mix_activation(mod);

	rsnd_mix_volume_init(io, mod);

	rsnd_mix_volume_update(io, mod);

	return 0;
}

static int rsnd_mix_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mix_halt(mod);

	rsnd_mod_power_off(mod);

	return 0;
}

static struct rsnd_mod_ops rsnd_mix_ops = {
	.name		= MIX_NAME,
	.probe		= rsnd_mix_probe_,
	.init		= rsnd_mix_init,
	.quit		= rsnd_mix_quit,
};

struct rsnd_mod *rsnd_mix_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_mix_nr(priv)))
		id = 0;

	return rsnd_mod_get(rsnd_mix_get(priv, id));
}

int rsnd_mix_probe(struct rsnd_priv *priv)
{
	struct device_node *node;
	struct device_node *np;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_mix *mix;
	struct clk *clk;
	char name[MIX_NAME_SIZE];
	int i, nr, ret;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv))
		return 0;

	node = rsnd_mix_of_node(priv);
	if (!node)
		return 0; /* not used is not error */

	nr = of_get_child_count(node);
	if (!nr) {
		ret = -EINVAL;
		goto rsnd_mix_probe_done;
	}

	mix	= devm_kzalloc(dev, sizeof(*mix) * nr, GFP_KERNEL);
	if (!mix) {
		ret = -ENOMEM;
		goto rsnd_mix_probe_done;
	}

	priv->mix_nr	= nr;
	priv->mix	= mix;

	i = 0;
	ret = 0;
	for_each_child_of_node(node, np) {
		mix = rsnd_mix_get(priv, i);

		snprintf(name, MIX_NAME_SIZE, "%s.%d",
			 MIX_NAME, i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			of_node_put(np);
			goto rsnd_mix_probe_done;
		}

		ret = rsnd_mod_init(priv, rsnd_mod_get(mix), &rsnd_mix_ops,
				    clk, rsnd_mod_get_status, RSND_MOD_MIX, i);
		if (ret) {
			of_node_put(np);
			goto rsnd_mix_probe_done;
		}

		i++;
	}

rsnd_mix_probe_done:
	of_node_put(node);

	return ret;
}

void rsnd_mix_remove(struct rsnd_priv *priv)
{
	struct rsnd_mix *mix;
	int i;

	for_each_rsnd_mix(mix, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(mix));
	}
}
