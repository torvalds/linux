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
	int channels;
};

#define rsnd_ctu_nr(priv) ((priv)->ctu_nr)
#define for_each_rsnd_ctu(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_ctu_nr(priv)) &&				\
		     ((pos) = (struct rsnd_ctu *)(priv)->ctu + i);	\
	     i++)

#define rsnd_mod_to_ctu(_mod)	\
	container_of((_mod), struct rsnd_ctu, mod)

#define rsnd_ctu_get(priv, id) ((struct rsnd_ctu *)(priv->ctu) + id)

static void rsnd_ctu_activation(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, CTU_SWRSR, 0);
	rsnd_mod_write(mod, CTU_SWRSR, 1);
}

static void rsnd_ctu_halt(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, CTU_CTUIR, 1);
	rsnd_mod_write(mod, CTU_SWRSR, 0);
}

int rsnd_ctu_converted_channel(struct rsnd_mod *mod)
{
	struct rsnd_ctu *ctu = rsnd_mod_to_ctu(mod);

	return ctu->channels;
}

static int rsnd_ctu_probe_(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	return rsnd_cmd_attach(io, rsnd_mod_id(mod) / 4);
}

static void rsnd_ctu_value_init(struct rsnd_dai_stream *io,
			       struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, CTU_CTUIR, 1);

	rsnd_mod_write(mod, CTU_ADINR, rsnd_runtime_channel_original(io));

	rsnd_mod_write(mod, CTU_CPMDR, 0);
	rsnd_mod_write(mod, CTU_SCMDR, 0);
	rsnd_mod_write(mod, CTU_SV00R, 0);
	rsnd_mod_write(mod, CTU_SV01R, 0);
	rsnd_mod_write(mod, CTU_SV02R, 0);
	rsnd_mod_write(mod, CTU_SV03R, 0);
	rsnd_mod_write(mod, CTU_SV04R, 0);
	rsnd_mod_write(mod, CTU_SV05R, 0);
	rsnd_mod_write(mod, CTU_SV06R, 0);
	rsnd_mod_write(mod, CTU_SV07R, 0);

	rsnd_mod_write(mod, CTU_SV10R, 0);
	rsnd_mod_write(mod, CTU_SV11R, 0);
	rsnd_mod_write(mod, CTU_SV12R, 0);
	rsnd_mod_write(mod, CTU_SV13R, 0);
	rsnd_mod_write(mod, CTU_SV14R, 0);
	rsnd_mod_write(mod, CTU_SV15R, 0);
	rsnd_mod_write(mod, CTU_SV16R, 0);
	rsnd_mod_write(mod, CTU_SV17R, 0);

	rsnd_mod_write(mod, CTU_SV20R, 0);
	rsnd_mod_write(mod, CTU_SV21R, 0);
	rsnd_mod_write(mod, CTU_SV22R, 0);
	rsnd_mod_write(mod, CTU_SV23R, 0);
	rsnd_mod_write(mod, CTU_SV24R, 0);
	rsnd_mod_write(mod, CTU_SV25R, 0);
	rsnd_mod_write(mod, CTU_SV26R, 0);
	rsnd_mod_write(mod, CTU_SV27R, 0);

	rsnd_mod_write(mod, CTU_SV30R, 0);
	rsnd_mod_write(mod, CTU_SV31R, 0);
	rsnd_mod_write(mod, CTU_SV32R, 0);
	rsnd_mod_write(mod, CTU_SV33R, 0);
	rsnd_mod_write(mod, CTU_SV34R, 0);
	rsnd_mod_write(mod, CTU_SV35R, 0);
	rsnd_mod_write(mod, CTU_SV36R, 0);
	rsnd_mod_write(mod, CTU_SV37R, 0);

	rsnd_mod_write(mod, CTU_CTUIR, 0);
}

static int rsnd_ctu_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_power_on(mod);

	rsnd_ctu_activation(mod);

	rsnd_ctu_value_init(io, mod);

	return 0;
}

static int rsnd_ctu_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_ctu_halt(mod);

	rsnd_mod_power_off(mod);

	return 0;
}

static int rsnd_ctu_hw_params(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *fe_params)
{
	struct rsnd_ctu *ctu = rsnd_mod_to_ctu(mod);
	struct snd_soc_pcm_runtime *fe = substream->private_data;

	/*
	 * CTU assumes that it is used under DPCM if user want to use
	 * channel transfer. Then, CTU should be FE.
	 * And then, this function will be called *after* BE settings.
	 * this means, each BE already has fixuped hw_params.
	 * see
	 *	dpcm_fe_dai_hw_params()
	 *	dpcm_be_dai_hw_params()
	 */
	ctu->channels = 0;
	if (fe->dai_link->dynamic) {
		struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
		struct device *dev = rsnd_priv_to_dev(priv);
		struct snd_soc_dpcm *dpcm;
		struct snd_pcm_hw_params *be_params;
		int stream = substream->stream;

		list_for_each_entry(dpcm, &fe->dpcm[stream].be_clients, list_be) {
			be_params = &dpcm->hw_params;
			if (params_channels(fe_params) != params_channels(be_params))
				ctu->channels = params_channels(be_params);
		}

		dev_dbg(dev, "CTU convert channels %d\n", ctu->channels);
	}

	return 0;
}

static struct rsnd_mod_ops rsnd_ctu_ops = {
	.name		= CTU_NAME,
	.probe		= rsnd_ctu_probe_,
	.init		= rsnd_ctu_init,
	.quit		= rsnd_ctu_quit,
	.hw_params	= rsnd_ctu_hw_params,
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
				    clk, rsnd_mod_get_status, RSND_MOD_CTU, i);
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
