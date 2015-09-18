/*
 * Renesas R-Car DVC support
 *
 * Copyright (C) 2014 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "rsnd.h"

#define RSND_DVC_NAME_SIZE	16

#define DVC_NAME "dvc"

struct rsnd_dvc {
	struct rsnd_dvc_platform_info *info; /* rcar_snd.h */
	struct rsnd_mod mod;
	struct rsnd_kctrl_cfg_m volume;
	struct rsnd_kctrl_cfg_m mute;
	struct rsnd_kctrl_cfg_s ren;	/* Ramp Enable */
	struct rsnd_kctrl_cfg_s rup;	/* Ramp Rate Up */
	struct rsnd_kctrl_cfg_s rdown;	/* Ramp Rate Down */
};

#define rsnd_dvc_nr(priv) ((priv)->dvc_nr)
#define rsnd_dvc_of_node(priv) \
	of_get_child_by_name(rsnd_priv_to_dev(priv)->of_node, "rcar_sound,dvc")

#define rsnd_mod_to_dvc(_mod)	\
	container_of((_mod), struct rsnd_dvc, mod)

#define for_each_rsnd_dvc(pos, priv, i)				\
	for ((i) = 0;						\
	     ((i) < rsnd_dvc_nr(priv)) &&			\
	     ((pos) = (struct rsnd_dvc *)(priv)->dvc + i);	\
	     i++)

static const char * const dvc_ramp_rate[] = {
	"128 dB/1 step",	 /* 00000 */
	"64 dB/1 step",		 /* 00001 */
	"32 dB/1 step",		 /* 00010 */
	"16 dB/1 step",		 /* 00011 */
	"8 dB/1 step",		 /* 00100 */
	"4 dB/1 step",		 /* 00101 */
	"2 dB/1 step",		 /* 00110 */
	"1 dB/1 step",		 /* 00111 */
	"0.5 dB/1 step",	 /* 01000 */
	"0.25 dB/1 step",	 /* 01001 */
	"0.125 dB/1 step",	 /* 01010 */
	"0.125 dB/2 steps",	 /* 01011 */
	"0.125 dB/4 steps",	 /* 01100 */
	"0.125 dB/8 steps",	 /* 01101 */
	"0.125 dB/16 steps",	 /* 01110 */
	"0.125 dB/32 steps",	 /* 01111 */
	"0.125 dB/64 steps",	 /* 10000 */
	"0.125 dB/128 steps",	 /* 10001 */
	"0.125 dB/256 steps",	 /* 10010 */
	"0.125 dB/512 steps",	 /* 10011 */
	"0.125 dB/1024 steps",	 /* 10100 */
	"0.125 dB/2048 steps",	 /* 10101 */
	"0.125 dB/4096 steps",	 /* 10110 */
	"0.125 dB/8192 steps",	 /* 10111 */
};

static void rsnd_dvc_soft_reset(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, DVC_SWRSR, 0);
	rsnd_mod_write(mod, DVC_SWRSR, 1);
}

#define rsnd_dvc_initialize_lock(mod)	__rsnd_dvc_initialize_lock(mod, 1)
#define rsnd_dvc_initialize_unlock(mod)	__rsnd_dvc_initialize_lock(mod, 0)
static void __rsnd_dvc_initialize_lock(struct rsnd_mod *mod, u32 enable)
{
	rsnd_mod_write(mod, DVC_DVUIR, enable);
}

static void rsnd_dvc_volume_update(struct rsnd_dai_stream *io,
				   struct rsnd_mod *mod)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	u32 val[RSND_DVC_CHANNELS];
	u32 dvucr = 0;
	u32 mute = 0;
	int i;

	for (i = 0; i < dvc->mute.cfg.size; i++)
		mute |= (!!dvc->mute.cfg.val[i]) << i;

	/* Disable DVC Register access */
	rsnd_mod_write(mod, DVC_DVUER, 0);

	/* Enable Ramp */
	if (dvc->ren.val) {
		dvucr |= 0x10;

		/* Digital Volume Max */
		for (i = 0; i < RSND_DVC_CHANNELS; i++)
			val[i] = dvc->volume.cfg.max;

		rsnd_mod_write(mod, DVC_VRCTR, 0xff);
		rsnd_mod_write(mod, DVC_VRPDR, dvc->rup.val << 8 |
					       dvc->rdown.val);
		/*
		 * FIXME !!
		 * use scale-downed Digital Volume
		 * as Volume Ramp
		 * 7F FFFF -> 3FF
		 */
		rsnd_mod_write(mod, DVC_VRDBR,
			       0x3ff - (dvc->volume.val[0] >> 13));

	} else {
		for (i = 0; i < RSND_DVC_CHANNELS; i++)
			val[i] = dvc->volume.val[i];
	}

	/* Enable Digital Volume */
	dvucr |= 0x100;
	rsnd_mod_write(mod, DVC_VOL0R, val[0]);
	rsnd_mod_write(mod, DVC_VOL1R, val[1]);

	/*  Enable Mute */
	if (mute) {
		dvucr |= 0x1;
		rsnd_mod_write(mod, DVC_ZCMCR, mute);
	}

	rsnd_mod_write(mod, DVC_DVUCR, dvucr);

	/* Enable DVC Register access */
	rsnd_mod_write(mod, DVC_DVUER, 1);
}

static int rsnd_dvc_remove_gen2(struct rsnd_mod *mod,
				struct rsnd_dai_stream *io,
				struct rsnd_priv *priv)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);

	rsnd_kctrl_remove(dvc->volume);
	rsnd_kctrl_remove(dvc->mute);
	rsnd_kctrl_remove(dvc->ren);
	rsnd_kctrl_remove(dvc->rup);
	rsnd_kctrl_remove(dvc->rdown);

	return 0;
}

static int rsnd_dvc_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_hw_start(mod);

	rsnd_dvc_soft_reset(mod);

	rsnd_dvc_initialize_lock(mod);

	rsnd_path_parse(priv, io);

	rsnd_mod_write(mod, DVC_ADINR, rsnd_get_adinr_bit(mod, io));

	/* ch0/ch1 Volume */
	rsnd_dvc_volume_update(io, mod);

	rsnd_adg_set_cmd_timsel_gen2(mod, io);

	return 0;
}

static int rsnd_dvc_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_hw_stop(mod);

	return 0;
}

static int rsnd_dvc_start(struct rsnd_mod *mod,
			  struct rsnd_dai_stream *io,
			  struct rsnd_priv *priv)
{
	rsnd_dvc_initialize_unlock(mod);

	rsnd_mod_write(mod, CMD_CTRL, 0x10);

	return 0;
}

static int rsnd_dvc_stop(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	rsnd_mod_write(mod, CMD_CTRL, 0);

	return 0;
}

static int rsnd_dvc_pcm_new(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct snd_soc_pcm_runtime *rtd)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	int is_play = rsnd_io_is_play(io);
	int ret;

	/* Volume */
	ret = rsnd_kctrl_new_m(mod, io, rtd,
			is_play ?
			"DVC Out Playback Volume" : "DVC In Capture Volume",
			rsnd_dvc_volume_update,
			&dvc->volume, 0x00800000 - 1);
	if (ret < 0)
		return ret;

	/* Mute */
	ret = rsnd_kctrl_new_m(mod, io, rtd,
			is_play ?
			"DVC Out Mute Switch" : "DVC In Mute Switch",
			rsnd_dvc_volume_update,
			&dvc->mute, 1);
	if (ret < 0)
		return ret;

	/* Ramp */
	ret = rsnd_kctrl_new_s(mod, io, rtd,
			is_play ?
			"DVC Out Ramp Switch" : "DVC In Ramp Switch",
			rsnd_dvc_volume_update,
			&dvc->ren, 1);
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_e(mod, io, rtd,
			is_play ?
			"DVC Out Ramp Up Rate" : "DVC In Ramp Up Rate",
			&dvc->rup,
			rsnd_dvc_volume_update,
			dvc_ramp_rate, ARRAY_SIZE(dvc_ramp_rate));
	if (ret < 0)
		return ret;

	ret = rsnd_kctrl_new_e(mod, io, rtd,
			is_play ?
			"DVC Out Ramp Down Rate" : "DVC In Ramp Down Rate",
			&dvc->rdown,
			rsnd_dvc_volume_update,
			dvc_ramp_rate, ARRAY_SIZE(dvc_ramp_rate));

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
	.remove		= rsnd_dvc_remove_gen2,
	.init		= rsnd_dvc_init,
	.quit		= rsnd_dvc_quit,
	.start		= rsnd_dvc_start,
	.stop		= rsnd_dvc_stop,
	.pcm_new	= rsnd_dvc_pcm_new,
};

struct rsnd_mod *rsnd_dvc_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_dvc_nr(priv)))
		id = 0;

	return &((struct rsnd_dvc *)(priv->dvc) + id)->mod;
}

static void rsnd_of_parse_dvc(struct platform_device *pdev,
			      const struct rsnd_of_data *of_data,
			      struct rsnd_priv *priv)
{
	struct device_node *node;
	struct rsnd_dvc_platform_info *dvc_info;
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct device *dev = &pdev->dev;
	int nr;

	if (!of_data)
		return;

	node = of_get_child_by_name(dev->of_node, "rcar_sound,dvc");
	if (!node)
		return;

	nr = of_get_child_count(node);
	if (!nr)
		goto rsnd_of_parse_dvc_end;

	dvc_info = devm_kzalloc(dev,
				sizeof(struct rsnd_dvc_platform_info) * nr,
				GFP_KERNEL);
	if (!dvc_info) {
		dev_err(dev, "dvc info allocation error\n");
		goto rsnd_of_parse_dvc_end;
	}

	info->dvc_info		= dvc_info;
	info->dvc_info_nr	= nr;

rsnd_of_parse_dvc_end:
	of_node_put(node);
}

int rsnd_dvc_probe(struct platform_device *pdev,
		   const struct rsnd_of_data *of_data,
		   struct rsnd_priv *priv)
{
	struct rcar_snd_info *info = rsnd_priv_to_info(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dvc *dvc;
	struct clk *clk;
	char name[RSND_DVC_NAME_SIZE];
	int i, nr, ret;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv)) {
		dev_warn(dev, "CMD is not supported on Gen1\n");
		return -EINVAL;
	}

	rsnd_of_parse_dvc(pdev, of_data, priv);

	nr = info->dvc_info_nr;
	if (!nr)
		return 0;

	dvc	= devm_kzalloc(dev, sizeof(*dvc) * nr, GFP_KERNEL);
	if (!dvc)
		return -ENOMEM;

	priv->dvc_nr	= nr;
	priv->dvc	= dvc;

	for_each_rsnd_dvc(dvc, priv, i) {
		snprintf(name, RSND_DVC_NAME_SIZE, "%s.%d",
			 DVC_NAME, i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		dvc->info = &info->dvc_info[i];

		ret = rsnd_mod_init(priv, &dvc->mod, &rsnd_dvc_ops,
			      clk, RSND_MOD_DVC, i);
		if (ret)
			return ret;
	}

	return 0;
}

void rsnd_dvc_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
	struct rsnd_dvc *dvc;
	int i;

	for_each_rsnd_dvc(dvc, priv, i) {
		rsnd_mod_quit(&dvc->mod);
	}
}
