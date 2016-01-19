/*
 * ctu.c
 *
 * Copyright (c) 2015 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

#define CTU_NAME_SIZE	16
#define CTU_NAME "ctu"

struct rsnd_ctu {
	struct rsnd_mod mod;
};

#define rsnd_ctu_nr(priv) ((priv)->ctu_nr)
#define for_each_rsnd_ctu(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_ctu_nr(priv)) &&				\
		     ((pos) = (struct rsnd_ctu *)(priv)->ctu + i);	\
	     i++)

#define rsnd_ctu_get(priv, id) ((struct rsnd_ctu *)(priv->ctu) + id)
#define rsnd_ctu_initialize_lock(mod)	__rsnd_ctu_initialize_lock(mod, 1)
#define rsnd_ctu_initialize_unlock(mod)	__rsnd_ctu_initialize_lock(mod, 0)
static void __rsnd_ctu_initialize_lock(struct rsnd_mod *mod, u32 enable)
{
	rsnd_mod_write(mod, CTU_CTUIR, enable);
}

static int rsnd_ctu_probe_(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	return rsnd_cmd_attach(io, rsnd_mod_id(mod) / 4);
}

static int rsnd_ctu_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_power_on(mod);

	rsnd_ctu_initialize_lock(mod);

	rsnd_mod_write(mod, CTU_ADINR, rsnd_get_adinr_chan(mod, io));

	rsnd_ctu_initialize_unlock(mod);

	return 0;
}

static int rsnd_ctu_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_power_off(mod);

	return 0;
}

static struct rsnd_mod_ops rsnd_ctu_ops = {
	.name		= CTU_NAME,
	.probe		= rsnd_ctu_probe_,
	.init		= rsnd_ctu_init,
	.quit		= rsnd_ctu_quit,
};

struct rsnd_mod *rsnd_ctu_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_ctu_nr(priv)))
		id = 0;

	return rsnd_mod_get(rsnd_ctu_get(priv, id));
}

int rsnd_ctu_probe(struct rsnd_priv *priv)
{
	struct device_node *node;
	struct device_node *np;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_ctu *ctu;
	struct clk *clk;
	char name[CTU_NAME_SIZE];
	int i, nr, ret;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv))
		return 0;

	node = rsnd_ctu_of_node(priv);
	if (!node)
		return 0; /* not used is not error */

	nr = of_get_child_count(node);
	if (!nr) {
		ret = -EINVAL;
		goto rsnd_ctu_probe_done;
	}

	ctu = devm_kzalloc(dev, sizeof(*ctu) * nr, GFP_KERNEL);
	if (!ctu) {
		ret = -ENOMEM;
		goto rsnd_ctu_probe_done;
	}

	priv->ctu_nr	= nr;
	priv->ctu	= ctu;

	i = 0;
	ret = 0;
	for_each_child_of_node(node, np) {
		ctu = rsnd_ctu_get(priv, i);

		/*
		 * CTU00, CTU01, CTU02, CTU03 => CTU0
		 * CTU10, CTU11, CTU12, CTU13 => CTU1
		 */
		snprintf(name, CTU_NAME_SIZE, "%s.%d",
			 CTU_NAME, i / 4);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto rsnd_ctu_probe_done;
		}

		ret = rsnd_mod_init(priv, rsnd_mod_get(ctu), &rsnd_ctu_ops,
				    clk, RSND_MOD_CTU, i);
		if (ret)
			goto rsnd_ctu_probe_done;

		i++;
	}


rsnd_ctu_probe_done:
	of_node_put(node);

	return ret;
}

void rsnd_ctu_remove(struct rsnd_priv *priv)
{
	struct rsnd_ctu *ctu;
	int i;

	for_each_rsnd_ctu(ctu, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(ctu));
	}
}
