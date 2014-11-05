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
#define RSND_DVC_CHANNELS	2

#define DVC_NAME "dvc"

struct rsnd_dvc_cfg {
	unsigned int max;
	unsigned int size;
	u32 *val;
	const char * const *texts;
};

struct rsnd_dvc_cfg_m {
	struct rsnd_dvc_cfg cfg;
	u32 val[RSND_DVC_CHANNELS];
};

struct rsnd_dvc_cfg_s {
	struct rsnd_dvc_cfg cfg;
	u32 val;
};

struct rsnd_dvc {
	struct rsnd_dvc_platform_info *info; /* rcar_snd.h */
	struct rsnd_mod mod;
	struct clk *clk;
	struct rsnd_dvc_cfg_m volume;
	struct rsnd_dvc_cfg_m mute;
};

#define rsnd_mod_to_dvc(_mod)	\
	container_of((_mod), struct rsnd_dvc, mod)

#define for_each_rsnd_dvc(pos, priv, i)				\
	for ((i) = 0;						\
	     ((i) < rsnd_dvc_nr(priv)) &&			\
	     ((pos) = (struct rsnd_dvc *)(priv)->dvc + i);	\
	     i++)

static void rsnd_dvc_volume_update(struct rsnd_mod *mod)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	u32 dvucr = 0;
	u32 mute = 0;
	int i;

	for (i = 0; i < dvc->mute.cfg.size; i++)
		mute |= (!!dvc->mute.cfg.val[i]) << i;

	/* Disable DVC Register access */
	rsnd_mod_write(mod, DVC_DVUER, 0);

	/* Enable Digital Volume */
	dvucr = 0x100;
	rsnd_mod_write(mod, DVC_VOL0R, dvc->volume.val[0]);
	rsnd_mod_write(mod, DVC_VOL1R, dvc->volume.val[1]);

	/*  Enable Mute */
	if (mute) {
		dvucr |= 0x1;
		rsnd_mod_write(mod, DVC_ZCMCR, mute);
	}

	rsnd_mod_write(mod, DVC_DVUCR, dvucr);

	/* Enable DVC Register access */
	rsnd_mod_write(mod, DVC_DVUER, 1);
}

static int rsnd_dvc_probe_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai *rdai)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);

	dev_dbg(dev, "%s (Gen2) is probed\n", rsnd_mod_name(mod));

	return 0;
}

static int rsnd_dvc_init(struct rsnd_mod *dvc_mod,
			 struct rsnd_dai *rdai)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(dvc_mod);
	struct rsnd_dai_stream *io = rsnd_mod_to_io(dvc_mod);
	struct rsnd_priv *priv = rsnd_mod_to_priv(dvc_mod);
	struct rsnd_mod *src_mod = rsnd_io_to_mod_src(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	int dvc_id = rsnd_mod_id(dvc_mod);
	int src_id = rsnd_mod_id(src_mod);
	u32 route[] = {
		[0] = 0x30000,
		[1] = 0x30001,
		[2] = 0x40000,
		[3] = 0x10000,
		[4] = 0x20000,
		[5] = 0x40100
	};

	if (src_id >= ARRAY_SIZE(route)) {
		dev_err(dev, "DVC%d isn't connected to SRC%d\n", dvc_id, src_id);
		return -EINVAL;
	}

	clk_prepare_enable(dvc->clk);

	/*
	 * fixme
	 * it doesn't support CTU/MIX
	 */
	rsnd_mod_write(dvc_mod, CMD_ROUTE_SLCT, route[src_id]);

	rsnd_mod_write(dvc_mod, DVC_SWRSR, 0);
	rsnd_mod_write(dvc_mod, DVC_SWRSR, 1);

	rsnd_mod_write(dvc_mod, DVC_DVUIR, 1);

	rsnd_mod_write(dvc_mod, DVC_ADINR, rsnd_get_adinr(dvc_mod));

	/* ch0/ch1 Volume */
	rsnd_dvc_volume_update(dvc_mod);

	rsnd_mod_write(dvc_mod, DVC_DVUIR, 0);

	rsnd_adg_set_cmd_timsel_gen2(rdai, dvc_mod, io);

	return 0;
}

static int rsnd_dvc_quit(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai)
{
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);

	clk_disable_unprepare(dvc->clk);

	return 0;
}

static int rsnd_dvc_start(struct rsnd_mod *mod,
			  struct rsnd_dai *rdai)
{
	rsnd_mod_write(mod, CMD_CTRL, 0x10);

	return 0;
}

static int rsnd_dvc_stop(struct rsnd_mod *mod,
			 struct rsnd_dai *rdai)
{
	rsnd_mod_write(mod, CMD_CTRL, 0);

	return 0;
}

static int rsnd_dvc_volume_info(struct snd_kcontrol *kctrl,
			       struct snd_ctl_elem_info *uinfo)
{
	struct rsnd_dvc_cfg *cfg = (struct rsnd_dvc_cfg *)kctrl->private_value;

	if (cfg->texts) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
		uinfo->count = cfg->size;
		uinfo->value.enumerated.items = cfg->max;
		if (uinfo->value.enumerated.item >= cfg->max)
			uinfo->value.enumerated.item = cfg->max - 1;
		strlcpy(uinfo->value.enumerated.name,
			cfg->texts[uinfo->value.enumerated.item],
			sizeof(uinfo->value.enumerated.name));
	} else {
		uinfo->count = cfg->size;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = cfg->max;
		uinfo->type = (cfg->max == 1) ?
			SNDRV_CTL_ELEM_TYPE_BOOLEAN :
			SNDRV_CTL_ELEM_TYPE_INTEGER;
	}

	return 0;
}

static int rsnd_dvc_volume_get(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct rsnd_dvc_cfg *cfg = (struct rsnd_dvc_cfg *)kctrl->private_value;
	int i;

	for (i = 0; i < cfg->size; i++)
		if (cfg->texts)
			ucontrol->value.enumerated.item[i] = cfg->val[i];
		else
			ucontrol->value.integer.value[i] = cfg->val[i];

	return 0;
}

static int rsnd_dvc_volume_put(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct rsnd_mod *mod = snd_kcontrol_chip(kctrl);
	struct rsnd_dvc_cfg *cfg = (struct rsnd_dvc_cfg *)kctrl->private_value;
	int i, change = 0;

	for (i = 0; i < cfg->size; i++) {
		if (cfg->texts) {
			change |= (ucontrol->value.enumerated.item[i] != cfg->val[i]);
			cfg->val[i] = ucontrol->value.enumerated.item[i];
		} else {
			change |= (ucontrol->value.integer.value[i] != cfg->val[i]);
			cfg->val[i] = ucontrol->value.integer.value[i];
		}
	}

	if (change)
		rsnd_dvc_volume_update(mod);

	return change;
}

static int __rsnd_dvc_pcm_new(struct rsnd_mod *mod,
			      struct rsnd_dai *rdai,
			      struct snd_soc_pcm_runtime *rtd,
			      const unsigned char *name,
			      struct rsnd_dvc_cfg *private)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_kcontrol *kctrl;
	struct snd_kcontrol_new knew = {
		.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		= name,
		.info		= rsnd_dvc_volume_info,
		.get		= rsnd_dvc_volume_get,
		.put		= rsnd_dvc_volume_put,
		.private_value	= (unsigned long)private,
	};
	int ret;

	kctrl = snd_ctl_new1(&knew, mod);
	if (!kctrl)
		return -ENOMEM;

	ret = snd_ctl_add(card, kctrl);
	if (ret < 0)
		return ret;

	return 0;
}

static int _rsnd_dvc_pcm_new_m(struct rsnd_mod *mod,
			       struct rsnd_dai *rdai,
			       struct snd_soc_pcm_runtime *rtd,
			       const unsigned char *name,
			       struct rsnd_dvc_cfg_m *private,
			       u32 max)
{
	private->cfg.max	= max;
	private->cfg.size	= RSND_DVC_CHANNELS;
	private->cfg.val	= private->val;
	return __rsnd_dvc_pcm_new(mod, rdai, rtd, name, &private->cfg);
}

static int _rsnd_dvc_pcm_new_s(struct rsnd_mod *mod,
			       struct rsnd_dai *rdai,
			       struct snd_soc_pcm_runtime *rtd,
			       const unsigned char *name,
			       struct rsnd_dvc_cfg_s *private,
			       u32 max)
{
	private->cfg.max	= max;
	private->cfg.size	= 1;
	private->cfg.val	= &private->val;
	return __rsnd_dvc_pcm_new(mod, rdai, rtd, name, &private->cfg);
}

static int _rsnd_dvc_pcm_new_e(struct rsnd_mod *mod,
			       struct rsnd_dai *rdai,
			       struct snd_soc_pcm_runtime *rtd,
			       const unsigned char *name,
			       struct rsnd_dvc_cfg_s *private,
			       const char * const *texts,
			       u32 max)
{
	private->cfg.max	= max;
	private->cfg.size	= 1;
	private->cfg.val	= &private->val;
	private->cfg.texts	= texts;
	return __rsnd_dvc_pcm_new(mod, rdai, rtd, name, &private->cfg);
}

static int rsnd_dvc_pcm_new(struct rsnd_mod *mod,
			    struct rsnd_dai *rdai,
			    struct snd_soc_pcm_runtime *rtd)
{
	struct rsnd_dai_stream *io = rsnd_mod_to_io(mod);
	struct rsnd_dvc *dvc = rsnd_mod_to_dvc(mod);
	int ret;

	/* Volume */
	ret = _rsnd_dvc_pcm_new_m(mod, rdai, rtd,
			rsnd_dai_is_play(rdai, io) ?
			"DVC Out Playback Volume" : "DVC In Capture Volume",
			&dvc->volume, 0x00800000 - 1);
	if (ret < 0)
		return ret;

	/* Mute */
	ret = _rsnd_dvc_pcm_new_m(mod, rdai, rtd,
			rsnd_dai_is_play(rdai, io) ?
			"DVC Out Mute Switch" : "DVC In Mute Switch",
			&dvc->mute, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static struct rsnd_mod_ops rsnd_dvc_ops = {
	.name		= DVC_NAME,
	.probe		= rsnd_dvc_probe_gen2,
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
	int i, nr;

	rsnd_of_parse_dvc(pdev, of_data, priv);

	nr = info->dvc_info_nr;
	if (!nr)
		return 0;

	/* This driver doesn't support Gen1 at this point */
	if (rsnd_is_gen1(priv)) {
		dev_warn(dev, "CMD is not supported on Gen1\n");
		return -EINVAL;
	}

	dvc	= devm_kzalloc(dev, sizeof(*dvc) * nr, GFP_KERNEL);
	if (!dvc) {
		dev_err(dev, "CMD allocate failed\n");
		return -ENOMEM;
	}

	priv->dvc_nr	= nr;
	priv->dvc	= dvc;

	for_each_rsnd_dvc(dvc, priv, i) {
		snprintf(name, RSND_DVC_NAME_SIZE, "%s.%d",
			 DVC_NAME, i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		dvc->info = &info->dvc_info[i];
		dvc->clk  = clk;

		rsnd_mod_init(priv, &dvc->mod, &rsnd_dvc_ops, RSND_MOD_DVC, i);

		dev_dbg(dev, "CMD%d probed\n", i);
	}

	return 0;
}
