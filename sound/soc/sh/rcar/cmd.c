/*
 * Renesas R-Car CMD support
 *
 * Copyright (C) 2015 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

struct rsnd_cmd {
	struct rsnd_mod mod;
};

#define CMD_NAME "cmd"

#define rsnd_cmd_nr(priv) ((priv)->cmd_nr)
#define for_each_rsnd_cmd(pos, priv, i)					\
	for ((i) = 0;							\
	     ((i) < rsnd_cmd_nr(priv)) &&				\
		     ((pos) = (struct rsnd_cmd *)(priv)->cmd + i);	\
	     i++)

static int rsnd_cmd_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	struct rsnd_mod *dvc = rsnd_io_to_mod_dvc(io);
	struct rsnd_mod *mix = rsnd_io_to_mod_mix(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	u32 data;

	if (!mix && !dvc)
		return 0;

	if (mix) {
		struct rsnd_dai *rdai;
		struct rsnd_mod *src;
		struct rsnd_dai_stream *tio;
		int i;
		u32 path[] = {
			[0] = 0,
			[1] = 1 << 0,
			[2] = 0,
			[3] = 0,
			[4] = 0,
			[5] = 1 << 8
		};

		/*
		 * it is assuming that integrater is well understanding about
		 * data path. Here doesn't check impossible connection,
		 * like src2 + src5
		 */
		data = 0;
		for_each_rsnd_dai(rdai, priv, i) {
			tio = &rdai->playback;
			src = rsnd_io_to_mod_src(tio);
			if (mix == rsnd_io_to_mod_mix(tio))
				data |= path[rsnd_mod_id(src)];

			tio = &rdai->capture;
			src = rsnd_io_to_mod_src(tio);
			if (mix == rsnd_io_to_mod_mix(tio))
				data |= path[rsnd_mod_id(src)];
		}

	} else {
		struct rsnd_mod *src = rsnd_io_to_mod_src(io);

		u32 path[] = {
			[0] = 0x30000,
			[1] = 0x30001,
			[2] = 0x40000,
			[3] = 0x10000,
			[4] = 0x20000,
			[5] = 0x40100
		};

		data = path[rsnd_mod_id(src)];
	}

	dev_dbg(dev, "ctu/mix path = 0x%08x", data);

	rsnd_mod_write(mod, CMD_ROUTE_SLCT, data);
	rsnd_mod_write(mod, CMD_BUSIF_DALIGN, rsnd_get_dalign(mod, io));

	rsnd_adg_set_cmd_timsel_gen2(mod, io);

	return 0;
}

static int rsnd_cmd_start(struct rsnd_mod *mod,
			  struct rsnd_dai_stream *io,
			  struct rsnd_priv *priv)
{
	rsnd_mod_write(mod, CMD_CTRL, 0x10);

	return 0;
}

static int rsnd_cmd_stop(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_write(mod, CMD_CTRL, 0);

	return 0;
}

static struct rsnd_mod_ops rsnd_cmd_ops = {
	.name	= CMD_NAME,
	.init	= rsnd_cmd_init,
	.start	= rsnd_cmd_start,
	.stop	= rsnd_cmd_stop,
};

int rsnd_cmd_attach(struct rsnd_dai_stream *io, int id)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_mod *mod = rsnd_cmd_mod_get(priv, id);

	return rsnd_dai_connect(mod, io, mod->type);
}

struct rsnd_mod *rsnd_cmd_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_cmd_nr(priv)))
		id = 0;

	return rsnd_mod_get((struct rsnd_cmd *)(priv->cmd) + id);
}

int rsnd_cmd_probe(struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_cmd *cmd;
	int i, nr, ret;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv))
		return 0;

	/* same number as DVC */
	nr = priv->dvc_nr;
	if (!nr)
		return 0;

	cmd = devm_kzalloc(dev, sizeof(*cmd) * nr, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	priv->cmd_nr	= nr;
	priv->cmd	= cmd;

	for_each_rsnd_cmd(cmd, priv, i) {
		ret = rsnd_mod_init(priv, rsnd_mod_get(cmd),
				    &rsnd_cmd_ops, NULL,
				    rsnd_mod_get_status, RSND_MOD_CMD, i);
		if (ret)
			return ret;
	}

	return 0;
}

void rsnd_cmd_remove(struct rsnd_priv *priv)
{
	struct rsnd_cmd *cmd;
	int i;

	for_each_rsnd_cmd(cmd, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(cmd));
	}
}
