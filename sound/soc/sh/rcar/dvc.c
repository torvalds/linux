// SPDX-License-Identifier: GPL-2.0
//
// Renesas R-Car DVC support
//
// Copyright (C) 2014 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

/*
 * Playback Volume
 *	amixer set "DVC Out" 100%
 *
 * Capture Volume
 *	amixer set "DVC In" 100%
 *
 * Playback Mute
 *	amixer set "DVC Out Mute" on
 *
 * Capture Mute
 *	amixer set "DVC In Mute" on
 *
 * Volume Ramp
 *	amixer set "DVC Out Ramp Up Rate"   "0.125 dB/64 steps"
 *	amixer set "DVC Out Ramp Down Rate" "0.125 dB/512 steps"
 *	amixer set "DVC Out Ramp" on
 *	aplay xxx.wav &
 *	amixer set "DVC Out"  80%  // Volume Down
 *	amixer set "DVC Out" 100%  // Volume Up
 */

#include "rsnd.h"

#define RSND_DVC_NAME_SIZE	16

#define DVC_NAME "dvc"

struct rsnd_dvc {
	struct rsnd_mod mod;
	struct rsnd_kctrl_cfg_m volume;
	struct rsnd_kctrl_cfg_m mute;
	struct rsnd_kctrl_cfg_s ren;	/* Ramp Enable */
	struct rsnd_kctrl_cfg_s rup;	/* Ramp Rate Up */
	struct rsnd_kctrl_cfg_s rdown;	/* Ramp Rate Down */
};

#define rsnd_dvc_get(priv, id) ((struct rsnd_dvc *)(priv->dvc) + id)
#define rsnd_dvc_nr(priv) ((priv)->dvc_nr)

#define rsnd_mod_to_dvc(_mod)	\
	container_of((_mod), struct rsnd_dvc, mod)

#define for_each_rsnd_dvc(pos, priv, i)				\
	for ((i) = 0;						\
	     ((i) < rsnd_dvc_nr(priv)) &&			\
	     ((pos) = (struct rsnd_dvc *)(priv)->dvc + i);	\
	     i++)

static void rsnd_dvc_activation(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, DVC_SWRSR, 0);
	rsnd_mod_write(mod, DVC_SWRSR, 1);
}

static void rsnd_dvc_halt(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, DVC_DVUIR, 1);
	rsnd_mod_write(mod, DVC_SWRSR, 0);
}

#define rsnd_dvc_get_vrpdr(dvc) (rsnd_kctrl_vals(dvc->rup) << 8 | \
				 rsnd_kctrl_vals(dvc->rdown))
#define rsnd_dvc_get_vrdbr(dvc) (0x3ff - (rsnd_kctrl_valm(dvc->volume, 0) >> 13))

static void rsnd_dvc_volume_parameter(struct rsnd_dai_stream *io,
					      struct rsnd_mod *mod)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	u32 val[RSND_MAX_CHANNELS];
	int i;

	/* Enable Ramp */
	if (rsnd_kctrl_vals(dvc->ren))
		for (i = 0; i < RSND_MAX_CHANNELS; i++)
			val[i] = rsnd_kctrl_max(dvc->volume);
	else
		for (i = 0; i < RSND_MAX_CHANNELS; i++)
			val[i] = rsnd_kctrl_valm(dvc->volume, i);

	/* Enable Digital Volume */
	for (i = 0; i < RSND_MAX_CHANNELS; i++)
		rsnd_mod_write(mod, DVC_VOLxR(i), val[i]);
}

static void rsnd_dvc_volume_init(struct rsnd_dai_stream *io,
				 struct rsnd_mod *mod)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	u32 adinr = 0;
	u32 dvucr = 0;
	u32 vrctr = 0;
	u32 vrpdr = 0;
	u32 vrdbr = 0;

	adinr = rsnd_get_adinr_bit(mod, io) |
		rsnd_runtime_channel_after_ctu(io);

	/* Enable Digital Volume, Zero Cross Mute Mode */
	dvucr |= 0x101;

	/* Enable Ramp */
	if (rsnd_kctrl_vals(dvc->ren)) {
		dvucr |= 0x10;

		/*
		 * FIXME !!
		 * use scale-downed Digital Volume
		 * as Volume Ramp
		 * 7F FFFF -> 3FF
		 */
		vrctr = 0xff;
		vrpdr = rsnd_dvc_get_vrpdr(dvc);
		vrdbr = rsnd_dvc_get_vrdbr(dvc);
	}

	/* Initialize operation */
	rsnd_mod_write(mod, DVC_DVUIR, 1);

	/* General Information */
	rsnd_mod_write(mod, DVC_ADINR, adinr);
	rsnd_mod_write(mod, DVC_DVUCR, dvucr);

	/* Volume Ramp Parameter */
	rsnd_mod_write(mod, DVC_VRCTR, vrctr);
	rsnd_mod_write(mod, DVC_VRPDR, vrpdr);
	rsnd_mod_write(mod, DVC_VRDBR, vrdbr);

	/* Digital Volume Function Parameter */
	rsnd_dvc_volume_parameter(io, mod);

	/* cancel operation */
	rsnd_mod_write(mod, DVC_DVUIR, 0);
}

static void rsnd_dvc_volume_update(struct rsnd_dai_stream *io,
				   struct rsnd_mod *mod)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	u32 zcmcr = 0;
	u32 vrpdr = 0;
	u32 vrdbr = 0;
	int i;

	for (i = 0; i < rsnd_kctrl_size(dvc->mute); i++)
		zcmcr |= (!!rsnd_kctrl_valm(dvc->mute, i)) << i;

	if (rsnd_kctrl_vals(dvc->ren)) {
		vrpdr = rsnd_dvc_get_vrpdr(dvc);
		vrdbr = rsnd_dvc_get_vrdbr(dvc);
	}

	/* Disable DVC Register access */
	rsnd_mod_write(mod, DVC_DVUER, 0);

	/* Zero Cross Mute Function */
	rsnd_mod_write(mod, DVC_ZCMCR, zcmcr);

	/* Volume Ramp Function */
	rsnd_mod_write(mod, DVC_VRPDR, vrpdr);
	rsnd_mod_write(mod, DVC_VRDBR, vrdbr);
	/* add DVC_VRWTR here */

	/* Digital Volume Function Parameter */
	rsnd_dvc_volume_parameter(io, mod);

	/* Enable DVC Register access */
	rsnd_mod_write(mod, DVC_DVUER, 1);
}

static int rsnd_dvc_probe_(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	return rsnd_cmd_attach(io, rsnd_mod_id(mod));
}

static int rsnd_dvc_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_power_on(mod);

	rsnd_dvc_activation(mod);

	rsnd_dvc_volume_init(io, mod);

	rsnd_dvc_volume_update(io, mod);

	return 0;
}

static int rsnd_dvc_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_dvc_halt(mod);

	rsnd_mod_power_off(mod);

	return 0;
}

static int rsnd_dvc_pcm_new(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct snd_soc_pcm_runtime *rtd)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	int is_play = rsnd_io_is_play(io);
	int channels = rsnd_rdai_channels_get(rdai);
	int ret;

	/* Volume */
	ret = rsnd_kctrl_new_m(mod, io, rtd,
			is_play ?
			"DVC Out Playback Volume" : "DVC In Capture Volume",
			rsnd_kctrl_accept_anytime,
			rsnd_dvc_volume_update,
			&dvc->volume, channels,
			0x00800000 - 1);
	if (ret < 0)
		return ret;

	/* Mute */
	ret = rsnd_kctrl_new_m(mod, io, rtd,
			is_play ?
			"DVC Out Mute Switch" : "DVC In Mute Switch",
			rsnd_kctrl_accept_anytime,
			rsnd_dvc_volume_update,
			&dvc->mute, channels,
			1);
	if (ret < 0)
		return ret;

	/* Ramp */
	ret = rsnd_kctrl_new_s(mod, io, rtd,
			is_play ?
			"DVC Out Ramp Switch" : "DVC In Ramp Switch",
			rsnd_kctrl_accept_anytime,
			rsnd_dvc_volume_update,
			&dvc->ren, 1);
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_e(mod, io, rtd,
			is_play ?
			"DVC Out Ramp Up Rate" : "DVC In Ramp Up Rate",
			rsnd_kctrl_accept_anytime,
			rsnd_dvc_volume_update,
			&dvc->rup,
			volume_ramp_rate,
			VOLUME_RAMP_MAX_DVC);
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_e(mod, io, rtd,
			is_play ?
			"DVC Out Ramp Down Rate" : "DVC In Ramp Down Rate",
			rsnd_kctrl_accept_anytime,
			rsnd_dvc_volume_update,
			&dvc->rdown,
			volume_ramp_rate,
			VOLUME_RAMP_MAX_DVC);

	if (ret < 0)
		return ret;

	return 0;
}

static struct dma_chan *rsnd_dvc_dma_req(struct rsnd_dai_stream *io,
					 struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);

	return rsnd_dma_request_channel(rsnd_dvc_of_node(priv),
					mod, "tx");
}

static struct rsnd_mod_ops rsnd_dvc_ops = {
	.name		= DVC_NAME,
	.dma_req	= rsnd_dvc_dma_req,
	.probe		= rsnd_dvc_probe_,
	.init		= rsnd_dvc_init,
	.quit		= rsnd_dvc_quit,
	.pcm_new	= rsnd_dvc_pcm_new,
	.get_status	= rsnd_mod_get_status,
};

struct rsnd_mod *rsnd_dvc_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_dvc_nr(priv)))
		id = 0;

	return rsnd_mod_get(rsnd_dvc_get(priv, id));
}

int rsnd_dvc_probe(struct rsnd_priv *priv)
{
	struct device_node *node;
	struct device_node *np;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dvc *dvc;
	struct clk *clk;
	char name[RSND_DVC_NAME_SIZE];
	int i, nr, ret;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv))
		return 0;

	node = rsnd_dvc_of_node(priv);
	if (!node)
		return 0; /* not used is not error */

	nr = of_get_child_count(node);
	if (!nr) {
		ret = -EINVAL;
		goto rsnd_dvc_probe_done;
	}

	dvc	= devm_kcalloc(dev, nr, sizeof(*dvc), GFP_KERNEL);
	if (!dvc) {
		ret = -ENOMEM;
		goto rsnd_dvc_probe_done;
	}

	priv->dvc_nr	= nr;
	priv->dvc	= dvc;

	i = 0;
	ret = 0;
	for_each_child_of_node(node, np) {
		dvc = rsnd_dvc_get(priv, i);

		snprintf(name, RSND_DVC_NAME_SIZE, "%s.%d",
			 DVC_NAME, i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			of_node_put(np);
			goto rsnd_dvc_probe_done;
		}

		ret = rsnd_mod_init(priv, rsnd_mod_get(dvc), &rsnd_dvc_ops,
				    clk, RSND_MOD_DVC, i);
		if (ret) {
			of_node_put(np);
			goto rsnd_dvc_probe_done;
		}

		i++;
	}

rsnd_dvc_probe_done:
	of_node_put(node);

	return ret;
}

void rsnd_dvc_remove(struct rsnd_priv *priv)
{
	struct rsnd_dvc *dvc;
	int i;

	for_each_rsnd_dvc(dvc, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(dvc));
	}
}
