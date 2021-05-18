// SPDX-License-Identifier: GPL-2.0
//
// Renesas R-Car CMD support
//
// Copyright (C) 2015 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

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
	static const u32 path[] = {
		[1] = 1 << 0,
		[5] = 1 << 8,
		[6] = 1 << 12,
		[9] = 1 << 15,
	};

	if (!mix && !dvc)
		return 0;

	if (ARRAY_SIZE(path) < rsnd_mod_id(mod) + 1)
		return -ENXIO;

	if (mix) {
		struct rsnd_dai *rdai;
		int i;

		/*
		 * it is assuming that integrater is well understanding about
		 * data path. Here doesn't check impossible connection,
		 * like src2 + src5
		 */
		data = 0;
		for_each_rsnd_dai(rdai, priv, i) {
			struct rsnd_dai_stream *tio = &rdai->playback;
			struct rsnd_mod *src = rsnd_io_to_mod_src(tio);

			if (mix == rsnd_io_to_mod_mix(tio))
				data |= path[rsnd_mod_id(src)];

			tio = &rdai->capture;
			src = rsnd_io_to_mod_src(tio);
			if (mix == rsnd_io_to_mod_mix(tio))
				data |= path[rsnd_mod_id(src)];
		}

	} else {
		struct rsnd_mod *src = rsnd_io_to_mod_src(io);

		static const u8 cmd_case[] = {
			[0] = 0x3,
			[1] = 0x3,
			[2] = 0x4,
			[3] = 0x1,
			[4] = 0x2,
			[5] = 0x4,
			[6] = 0x1,
			[9] = 0x2,
		};

		if (unlikely(!src))
			return -EIO;

		data = path[rsnd_mod_id(src)] |
			cmd_case[rsnd_mod_id(src)] << 16;
	}

	dev_dbg(dev, "ctu/mix path = 0x%08x\n", data);

	rsnd_mod_write(mod, CMD_ROUTE_SLCT, data);
	rsnd_mod_write(mod, CMD_BUSIF_MODE, rsnd_get_busif_shift(io, mod) | 1);
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
	.name		= CMD_NAME,
	.init		= rsnd_cmd_init,
	.start		= rsnd_cmd_start,
	.stop		= rsnd_cmd_stop,
	.get_status	= rsnd_mod_get_status,
};

static struct rsnd_mod *rsnd_cmd_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_cmd_nr(priv)))
		id = 0;

	return rsnd_mod_get((struct rsnd_cmd *)(priv->cmd) + id);
}
int rsnd_cmd_attach(struct rsnd_dai_stream *io, int id)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct rsnd_mod *mod = rsnd_cmd_mod_get(priv, id);

	return rsnd_dai_connect(mod, io, mod->type);
}

int rsnd_cmd_probe(struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_cmd *cmd;
	int i, nr;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv))
		return 0;

	/* same number as DVC */
	nr = priv->dvc_nr;
	if (!nr)
		return 0;

	cmd = devm_kcalloc(dev, nr, sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	priv->cmd_nr	= nr;
	priv->cmd	= cmd;

	for_each_rsnd_cmd(cmd, priv, i) {
		int ret = rsnd_mod_init(priv, rsnd_mod_get(cmd),
					&rsnd_cmd_ops, NULL,
					RSND_MOD_CMD, i);
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
