/*
 * mix.c
 *
 * Copyright (c) 2015 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 *		    CTUn	MIXn
 *		    +------+	+------+
 * [SRC3 / SRC6] -> |CTU n0| ->	[MIX n0| ->
 * [SRC4 / SRC9] -> |CTU n1| ->	[MIX n1| ->
 * [SRC0 / SRC1] -> |CTU n2| ->	[MIX n2| ->
 * [SRC2 / SRC5] -> |CTU n3| ->	[MIX n3| ->
 *		    +------+	+------+
 *
 * ex)
 *	DAI0 : playback = <&src0 &ctu02 &mix0 &dvc0 &ssi0>;
 *	DAI1 : playback = <&src2 &ctu03 &mix0 &dvc0 &ssi0>;
 *
 * MIX Volume
 *	amixer set "MIX",0  100%  // DAI0 Volume
 *	amixer set "MIX",1  100%  // DAI1 Volume
 *
 * Volume Ramp
 *	amixer set "MIX Ramp Up Rate"   "0.125 dB/1 step"
 *	amixer set "MIX Ramp Down Rate" "4 dB/1 step"
 *	amixer set "MIX Ramp" on
 *	aplay xxx.wav &
 *	amixer set "MIX",0  80%  // DAI0 Volume Down
 *	amixer set "MIX",1 100%  // DAI1 Volume Up
 */

#include "rsnd.h"

#define MIX_NAME_SIZE	16
#define MIX_NAME "mix"

struct rsnd_mix {
	struct rsnd_mod mod;
	struct rsnd_kctrl_cfg_s volumeA; /* MDBAR */
	struct rsnd_kctrl_cfg_s volumeB; /* MDBBR */
	struct rsnd_kctrl_cfg_s volumeC; /* MDBCR */
	struct rsnd_kctrl_cfg_s volumeD; /* MDBDR */
	struct rsnd_kctrl_cfg_s ren;	/* Ramp Enable */
	struct rsnd_kctrl_cfg_s rup;	/* Ramp Rate Up */
	struct rsnd_kctrl_cfg_s rdw;	/* Ramp Rate Down */
	u32 flags;
};

#define ONCE_KCTRL_INITIALIZED		(1 << 0)
#define HAS_VOLA			(1 << 1)
#define HAS_VOLB			(1 << 2)
#define HAS_VOLC			(1 << 3)
#define HAS_VOLD			(1 << 4)

#define VOL_MAX				0x3ff

#define rsnd_mod_to_mix(_mod)	\
	container_of((_mod), struct rsnd_mix, mod)

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

#define rsnd_mix_get_vol(mix, X) \
	rsnd_flags_has(mix, HAS_VOL##X) ? \
		(VOL_MAX - mix->volume##X.cfg.val[0]) : 0
static void rsnd_mix_volume_parameter(struct rsnd_dai_stream *io,
				      struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_mix *mix = rsnd_mod_to_mix(mod);
	u32 volA = rsnd_mix_get_vol(mix, A);
	u32 volB = rsnd_mix_get_vol(mix, B);
	u32 volC = rsnd_mix_get_vol(mix, C);
	u32 volD = rsnd_mix_get_vol(mix, D);

	dev_dbg(dev, "MIX A/B/C/D = %02x/%02x/%02x/%02x\n",
		volA, volB, volC, volD);

	rsnd_mod_write(mod, MIX_MDBAR, volA);
	rsnd_mod_write(mod, MIX_MDBBR, volB);
	rsnd_mod_write(mod, MIX_MDBCR, volC);
	rsnd_mod_write(mod, MIX_MDBDR, volD);
}

static void rsnd_mix_volume_init(struct rsnd_dai_stream *io,
				 struct rsnd_mod *mod)
{
	struct rsnd_mix *mix = rsnd_mod_to_mix(mod);

	rsnd_mod_write(mod, MIX_MIXIR, 1);

	/* General Information */
	rsnd_mod_write(mod, MIX_ADINR, rsnd_runtime_channel_after_ctu(io));

	/* volume step */
	rsnd_mod_write(mod, MIX_MIXMR, mix->ren.cfg.val[0]);
	rsnd_mod_write(mod, MIX_MVPDR, mix->rup.cfg.val[0] << 8 |
				       mix->rdw.cfg.val[0]);

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

static int rsnd_mix_pcm_new(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct snd_soc_pcm_runtime *rtd)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_mix *mix = rsnd_mod_to_mix(mod);
	struct rsnd_mod *src_mod = rsnd_io_to_mod_src(io);
	struct rsnd_kctrl_cfg_s *volume;
	int ret;

	switch (rsnd_mod_id(src_mod)) {
	case 3:
	case 6:	/* MDBAR */
		volume = &mix->volumeA;
		rsnd_flags_set(mix, HAS_VOLA);
		break;
	case 4:
	case 9:	/* MDBBR */
		volume = &mix->volumeB;
		rsnd_flags_set(mix, HAS_VOLB);
		break;
	case 0:
	case 1:	/* MDBCR */
		volume = &mix->volumeC;
		rsnd_flags_set(mix, HAS_VOLC);
		break;
	case 2:
	case 5:	/* MDBDR */
		volume = &mix->volumeD;
		rsnd_flags_set(mix, HAS_VOLD);
		break;
	default:
		dev_err(dev, "unknown SRC is connected\n");
		return -EINVAL;
	}

	/* Volume */
	ret = rsnd_kctrl_new_s(mod, io, rtd,
			       "MIX Playback Volume",
			       rsnd_kctrl_accept_anytime,
			       rsnd_mix_volume_update,
			       volume, VOL_MAX);
	if (ret < 0)
		return ret;
	volume->cfg.val[0] = VOL_MAX;

	if (rsnd_flags_has(mix, ONCE_KCTRL_INITIALIZED))
		return ret;

	/* Ramp */
	ret = rsnd_kctrl_new_s(mod, io, rtd,
			       "MIX Ramp Switch",
			       rsnd_kctrl_accept_anytime,
			       rsnd_mix_volume_update,
			       &mix->ren, 1);
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_e(mod, io, rtd,
			       "MIX Ramp Up Rate",
			       rsnd_kctrl_accept_anytime,
			       rsnd_mix_volume_update,
			       &mix->rup,
			       volume_ramp_rate,
			       VOLUME_RAMP_MAX_MIX);
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_e(mod, io, rtd,
			       "MIX Ramp Down Rate",
			       rsnd_kctrl_accept_anytime,
			       rsnd_mix_volume_update,
			       &mix->rdw,
			       volume_ramp_rate,
			       VOLUME_RAMP_MAX_MIX);

	rsnd_flags_set(mix, ONCE_KCTRL_INITIALIZED);

	return ret;
}

static struct rsnd_mod_ops rsnd_mix_ops = {
	.name		= MIX_NAME,
	.probe		= rsnd_mix_probe_,
	.init		= rsnd_mix_init,
	.quit		= rsnd_mix_quit,
	.pcm_new	= rsnd_mix_pcm_new,
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
