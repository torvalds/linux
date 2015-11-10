/*
 * Renesas R-Car SSIU support
 *
 * Copyright (c) 2015 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

#define SSIU_NAME "ssiu"

struct rsnd_ssiu {
	struct rsnd_mod mod;
};

#define rsnd_ssiu_nr(priv) ((priv)->ssiu_nr)
#define for_each_rsnd_ssiu(pos, priv, i)				\
	for (i = 0;							\
	     (i < rsnd_ssiu_nr(priv)) &&				\
		     ((pos) = ((struct rsnd_ssiu *)(priv)->ssiu + i));	\
	     i++)

static int rsnd_ssiu_init(struct rsnd_mod *mod,
			  struct rsnd_dai_stream *io,
			  struct rsnd_priv *priv)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	int use_busif = rsnd_ssi_use_busif(io);
	int id = rsnd_mod_id(mod);

	/*
	 * SSI_MODE0
	 */
	rsnd_mod_bset(mod, SSI_MODE0, (1 << id), !use_busif << id);

	/*
	 * SSI_MODE1
	 */
	if (rsnd_ssi_is_pin_sharing(io)) {
		int shift = -1;

		switch (id) {
		case 1:
			shift = 0;
			break;
		case 2:
			shift = 2;
			break;
		case 4:
			shift = 16;
			break;
		}

		if (shift >= 0)
			rsnd_mod_bset(mod, SSI_MODE1,
				      0x3 << shift,
				      rsnd_rdai_is_clk_master(rdai) ?
				      0x2 << shift : 0x1 << shift);
	}

	return 0;
}

static struct rsnd_mod_ops rsnd_ssiu_ops_gen1 = {
	.name	= SSIU_NAME,
	.init	= rsnd_ssiu_init,
};

static int rsnd_ssiu_init_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	int ret;

	ret = rsnd_ssiu_init(mod, io, priv);
	if (ret < 0)
		return ret;

	if (rsnd_ssi_use_busif(io)) {
		u32 val = rsnd_get_dalign(mod, io);

		rsnd_mod_write(mod, SSI_BUSIF_ADINR,
			       rsnd_get_adinr_bit(mod, io));
		rsnd_mod_write(mod, SSI_BUSIF_MODE,  1);
		rsnd_mod_write(mod, SSI_BUSIF_DALIGN, val);
	}

	return 0;
}

static int rsnd_ssiu_start_gen2(struct rsnd_mod *mod,
				struct rsnd_dai_stream *io,
				struct rsnd_priv *priv)
{
	if (rsnd_ssi_use_busif(io))
		rsnd_mod_write(mod, SSI_CTRL, 0x1);

	return 0;
}

static int rsnd_ssiu_stop_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	if (rsnd_ssi_use_busif(io))
		rsnd_mod_write(mod, SSI_CTRL, 0);

	return 0;
}

static struct rsnd_mod_ops rsnd_ssiu_ops_gen2 = {
	.name	= SSIU_NAME,
	.init	= rsnd_ssiu_init_gen2,
	.start	= rsnd_ssiu_start_gen2,
	.stop	= rsnd_ssiu_stop_gen2,
};

static struct rsnd_mod *rsnd_ssiu_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_ssiu_nr(priv)))
		id = 0;

	return rsnd_mod_get((struct rsnd_ssiu *)(priv->ssiu) + id);
}

int rsnd_ssiu_attach(struct rsnd_dai_stream *io,
		     struct rsnd_mod *ssi_mod)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_mod *mod = rsnd_ssiu_mod_get(priv, rsnd_mod_id(ssi_mod));

	rsnd_mod_confirm_ssi(ssi_mod);

	return rsnd_dai_connect(mod, io, mod->type);
}

int rsnd_ssiu_probe(struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_ssiu *ssiu;
	static struct rsnd_mod_ops *ops;
	int i, nr, ret;

	/* same number to SSI */
	nr	= priv->ssi_nr;
	ssiu	= devm_kzalloc(dev, sizeof(*ssiu) * nr, GFP_KERNEL);
	if (!ssiu)
		return -ENOMEM;

	priv->ssiu	= ssiu;
	priv->ssiu_nr	= nr;

	if (rsnd_is_gen1(priv))
		ops = &rsnd_ssiu_ops_gen1;
	else
		ops = &rsnd_ssiu_ops_gen2;

	for_each_rsnd_ssiu(ssiu, priv, i) {
		ret = rsnd_mod_init(priv, rsnd_mod_get(ssiu),
				    ops, NULL, RSND_MOD_SSIU, i);
		if (ret)
			return ret;
	}

	return 0;
}

void rsnd_ssiu_remove(struct rsnd_priv *priv)
{
	struct rsnd_ssiu *ssiu;
	int i;

	for_each_rsnd_ssiu(ssiu, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(ssiu));
	}
}
