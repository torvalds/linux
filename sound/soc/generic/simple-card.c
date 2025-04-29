// SPDX-License-Identifier: GPL-2.0
//
// ASoC simple sound card support
//
// Copyright (C) 2012 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/simple_card.h>
#include <sound/soc-dai.h>
#include <sound/soc.h>

#define DPCM_SELECTABLE 1

#define DAI	"sound-dai"
#define CELL	"#sound-dai-cells"
#define PREFIX	"simple-audio-card,"

static const struct snd_soc_ops simple_ops = {
	.startup	= simple_util_startup,
	.shutdown	= simple_util_shutdown,
	.hw_params	= simple_util_hw_params,
};

#define simple_ret(priv, ret) _simple_ret(priv, __func__, ret)
static inline int _simple_ret(struct simple_util_priv *priv,
			      const char *func, int ret)
{
	return snd_soc_ret(simple_priv_to_dev(priv), ret, "at %s()\n", func);
}

static int simple_parse_platform(struct simple_util_priv *priv,
				 struct device_node *node,
				 struct snd_soc_dai_link_component *dlc)
{
	struct of_phandle_args args;
	int ret;

	if (!node)
		return 0;

	/*
	 * Get node via "sound-dai = <&phandle port>"
	 * it will be used as xxx_of_node on soc_bind_dai_link()
	 */
	ret = of_parse_phandle_with_args(node, DAI, CELL, 0, &args);
	if (ret)
		return simple_ret(priv, ret);

	/* dai_name is not required and may not exist for plat component */

	dlc->of_node = args.np;

	return 0;
}

static int simple_parse_dai(struct simple_util_priv *priv,
			    struct device_node *node,
			    struct snd_soc_dai_link_component *dlc,
			    int *is_single_link)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct of_phandle_args args;
	struct snd_soc_dai *dai;
	int ret;

	if (!node)
		return 0;

	/*
	 * Get node via "sound-dai = <&phandle port>"
	 * it will be used as xxx_of_node on soc_bind_dai_link()
	 */
	ret = of_parse_phandle_with_args(node, DAI, CELL, 0, &args);
	if (ret)
		goto end;

	/*
	 * Try to find from DAI args
	 */
	dai = snd_soc_get_dai_via_args(&args);
	if (dai) {
		ret = -ENOMEM;
		dlc->dai_name = snd_soc_dai_name_get(dai);
		dlc->dai_args = snd_soc_copy_dai_args(dev, &args);
		if (!dlc->dai_args)
			goto end;

		goto parse_dai_end;
	}

	/*
	 * FIXME
	 *
	 * Here, dlc->dai_name is pointer to CPU/Codec DAI name.
	 * If user unbinded CPU or Codec driver, but not for Sound Card,
	 * dlc->dai_name is keeping unbinded CPU or Codec
	 * driver's pointer.
	 *
	 * If user re-bind CPU or Codec driver again, ALSA SoC will try
	 * to rebind Card via snd_soc_try_rebind_card(), but because of
	 * above reason, it might can't bind Sound Card.
	 * Because Sound Card is pointing to released dai_name pointer.
	 *
	 * To avoid this rebind Card issue,
	 * 1) It needs to alloc memory to keep dai_name eventhough
	 *    CPU or Codec driver was unbinded, or
	 * 2) user need to rebind Sound Card everytime
	 *    if he unbinded CPU or Codec.
	 */
	ret = snd_soc_get_dlc(&args, dlc);
	if (ret < 0)
		goto end;

parse_dai_end:
	if (is_single_link)
		*is_single_link = !args.args_count;
	ret = 0;
end:
	return simple_ret(priv, ret);
}

static void simple_parse_convert(struct device *dev,
				 struct device_node *np,
				 struct simple_util_data *adata)
{
	struct device_node *top = dev->of_node;
	struct device_node *node __free(device_node) = of_get_parent(np);

	simple_util_parse_convert(top,  PREFIX, adata);
	simple_util_parse_convert(node, PREFIX, adata);
	simple_util_parse_convert(node, NULL,   adata);
	simple_util_parse_convert(np,   NULL,   adata);
}

static int simple_parse_node(struct simple_util_priv *priv,
			     struct device_node *np,
			     struct link_info *li,
			     char *prefix,
			     int *cpu)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	struct snd_soc_dai_link_component *dlc;
	struct simple_util_dai *dai;
	int ret;

	if (cpu) {
		dlc = snd_soc_link_to_cpu(dai_link, 0);
		dai = simple_props_to_dai_cpu(dai_props, 0);
	} else {
		dlc = snd_soc_link_to_codec(dai_link, 0);
		dai = simple_props_to_dai_codec(dai_props, 0);
	}

	ret = simple_parse_dai(priv, np, dlc, cpu);
	if (ret)
		goto end;

	ret = simple_util_parse_clk(dev, np, dai, dlc);
	if (ret)
		goto end;

	ret = simple_util_parse_tdm(np, dai);
end:
	return simple_ret(priv, ret);
}

static int simple_link_init(struct simple_util_priv *priv,
			    struct device_node *cpu,
			    struct device_node *codec,
			    struct link_info *li,
			    char *prefix, char *name)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *top = dev->of_node;
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	struct device_node *node __free(device_node) = of_get_parent(cpu);
	enum snd_soc_trigger_order trigger_start = SND_SOC_TRIGGER_ORDER_DEFAULT;
	enum snd_soc_trigger_order trigger_stop  = SND_SOC_TRIGGER_ORDER_DEFAULT;
	bool playback_only = 0, capture_only = 0;
	int ret;

	ret = simple_util_parse_daifmt(dev, node, codec,
				       prefix, &dai_link->dai_fmt);
	if (ret < 0)
		goto end;

	graph_util_parse_link_direction(top,	&playback_only, &capture_only);
	graph_util_parse_link_direction(node,	&playback_only, &capture_only);
	graph_util_parse_link_direction(cpu,	&playback_only, &capture_only);
	graph_util_parse_link_direction(codec,	&playback_only, &capture_only);

	of_property_read_u32(top,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(top,	PREFIX	"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(node,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(node,	PREFIX	"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(cpu,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(cpu,	PREFIX	"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(codec,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(codec,	PREFIX	"mclk-fs", &dai_props->mclk_fs);

	graph_util_parse_trigger_order(priv, top,	&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, node,	&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, cpu,	&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, codec,	&trigger_start, &trigger_stop);

	dai_link->playback_only		= playback_only;
	dai_link->capture_only		= capture_only;

	dai_link->trigger_start		= trigger_start;
	dai_link->trigger_stop		= trigger_stop;

	dai_link->init			= simple_util_dai_init;
	dai_link->ops			= &simple_ops;

	ret = simple_util_set_dailink_name(priv, dai_link, name);
end:
	return simple_ret(priv, ret);
}

static int simple_dai_link_of_dpcm(struct simple_util_priv *priv,
				   struct device_node *np,
				   struct device_node *codec,
				   struct link_info *li,
				   bool is_top)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	struct device_node *top = dev->of_node;
	struct device_node *node __free(device_node) = of_get_parent(np);
	char *prefix = "";
	char dai_name[64];
	int ret;

	dev_dbg(dev, "link_of DPCM (%pOF)\n", np);

	/* For single DAI link & old style of DT node */
	if (is_top)
		prefix = PREFIX;

	if (li->cpu) {
		struct snd_soc_dai_link_component *cpus = snd_soc_link_to_cpu(dai_link, 0);
		struct snd_soc_dai_link_component *platforms = snd_soc_link_to_platform(dai_link, 0);
		int is_single_links = 0;

		/* Codec is dummy */

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		ret = simple_parse_node(priv, np, li, prefix, &is_single_links);
		if (ret < 0)
			goto out_put_node;

		snprintf(dai_name, sizeof(dai_name), "fe.%s", cpus->dai_name);

		simple_util_canonicalize_cpu(cpus, is_single_links);
		simple_util_canonicalize_platform(platforms, cpus);
	} else {
		struct snd_soc_dai_link_component *codecs = snd_soc_link_to_codec(dai_link, 0);
		struct snd_soc_codec_conf *cconf;

		/* CPU is dummy */

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= simple_util_be_hw_params_fixup;

		cconf	= simple_props_to_codec_conf(dai_props, 0);

		ret = simple_parse_node(priv, np, li, prefix, NULL);
		if (ret < 0)
			goto out_put_node;

		snprintf(dai_name, sizeof(dai_name), "be.%s", codecs->dai_name);

		/* check "prefix" from top node */
		snd_soc_of_parse_node_prefix(top, cconf, codecs->of_node,
					      PREFIX "prefix");
		snd_soc_of_parse_node_prefix(node, cconf, codecs->of_node,
					     "prefix");
		snd_soc_of_parse_node_prefix(np, cconf, codecs->of_node,
					     "prefix");
	}

	simple_parse_convert(dev, np, &dai_props->adata);

	ret = simple_link_init(priv, np, codec, li, prefix, dai_name);

out_put_node:
	li->link++;

	return simple_ret(priv, ret);
}

static int simple_dai_link_of(struct simple_util_priv *priv,
			      struct device_node *np,
			      struct device_node *codec,
			      struct link_info *li,
			      bool is_top)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct snd_soc_dai_link_component *cpus = snd_soc_link_to_cpu(dai_link, 0);
	struct snd_soc_dai_link_component *codecs = snd_soc_link_to_codec(dai_link, 0);
	struct snd_soc_dai_link_component *platforms = snd_soc_link_to_platform(dai_link, 0);
	struct device_node *cpu = NULL;
	char dai_name[64];
	char prop[128];
	char *prefix = "";
	int ret, single_cpu = 0;

	cpu  = np;
	struct device_node *node __free(device_node) = of_get_parent(np);

	dev_dbg(dev, "link_of (%pOF)\n", node);

	/* For single DAI link & old style of DT node */
	if (is_top)
		prefix = PREFIX;

	snprintf(prop, sizeof(prop), "%splat", prefix);
	struct device_node *plat __free(device_node)  = of_get_child_by_name(node, prop);

	ret = simple_parse_node(priv, cpu, li, prefix, &single_cpu);
	if (ret < 0)
		goto dai_link_of_err;

	ret = simple_parse_node(priv, codec, li, prefix, NULL);
	if (ret < 0)
		goto dai_link_of_err;

	ret = simple_parse_platform(priv, plat, platforms);
	if (ret < 0)
		goto dai_link_of_err;

	snprintf(dai_name, sizeof(dai_name),
		 "%s-%s", cpus->dai_name, codecs->dai_name);

	simple_util_canonicalize_cpu(cpus, single_cpu);
	simple_util_canonicalize_platform(platforms, cpus);

	ret = simple_link_init(priv, cpu, codec, li, prefix, dai_name);

dai_link_of_err:
	li->link++;

	return simple_ret(priv, ret);
}

static int __simple_for_each_link(struct simple_util_priv *priv,
			struct link_info *li,
			int (*func_noml)(struct simple_util_priv *priv,
					 struct device_node *np,
					 struct device_node *codec,
					 struct link_info *li, bool is_top),
			int (*func_dpcm)(struct simple_util_priv *priv,
					 struct device_node *np,
					 struct device_node *codec,
					 struct link_info *li, bool is_top))
{
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *top = dev->of_node;
	struct device_node *node;
	uintptr_t dpcm_selectable = (uintptr_t)of_device_get_match_data(dev);
	bool is_top = 0;
	int ret = 0;

	/* Check if it has dai-link */
	node = of_get_child_by_name(top, PREFIX "dai-link");
	if (!node) {
		node = of_node_get(top);
		is_top = 1;
	}

	struct device_node *add_devs __free(device_node) = of_get_child_by_name(top, PREFIX "additional-devs");

	/* loop for all dai-link */
	do {
		struct simple_util_data adata;
		int num = of_get_child_count(node);

		/* Skip additional-devs node */
		if (node == add_devs) {
			node = of_get_next_child(top, node);
			continue;
		}

		/* get codec */
		struct device_node *codec __free(device_node) =
			of_get_child_by_name(node, is_top ? PREFIX "codec" : "codec");
		if (!codec) {
			ret = -ENODEV;
			goto error;
		}
		/* get platform */
		struct device_node *plat __free(device_node) =
			of_get_child_by_name(node, is_top ? PREFIX "plat" : "plat");

		/* get convert-xxx property */
		memset(&adata, 0, sizeof(adata));
		for_each_child_of_node_scoped(node, np) {
			if (np == add_devs)
				continue;
			simple_parse_convert(dev, np, &adata);
		}

		/* loop for all CPU/Codec node */
		for_each_child_of_node_scoped(node, np) {
			if (plat == np || add_devs == np)
				continue;
			/*
			 * It is DPCM
			 * if it has many CPUs,
			 * or has convert-xxx property
			 */
			if (dpcm_selectable &&
			    (num > 2 || simple_util_is_convert_required(&adata))) {
				/*
				 * np
				 *	 |1(CPU)|0(Codec)  li->cpu
				 * CPU	 |Pass  |return
				 * Codec |return|Pass
				 */
				if (li->cpu != (np == codec))
					ret = func_dpcm(priv, np, codec, li, is_top);
			/* else normal sound */
			} else {
				/*
				 * np
				 *	 |1(CPU)|0(Codec)  li->cpu
				 * CPU	 |Pass  |return
				 * Codec |return|return
				 */
				if (li->cpu && (np != codec))
					ret = func_noml(priv, np, codec, li, is_top);
			}

			if (ret < 0)
				goto error;
		}

		node = of_get_next_child(top, node);
	} while (!is_top && node);

error:
	of_node_put(node);

	return simple_ret(priv, ret);
}

static int simple_for_each_link(struct simple_util_priv *priv,
				struct link_info *li,
				int (*func_noml)(struct simple_util_priv *priv,
						 struct device_node *np,
						 struct device_node *codec,
						 struct link_info *li, bool is_top),
				int (*func_dpcm)(struct simple_util_priv *priv,
						 struct device_node *np,
						 struct device_node *codec,
						 struct link_info *li, bool is_top))
{
	int ret;
	/*
	 * Detect all CPU first, and Detect all Codec 2nd.
	 *
	 * In Normal sound case, all DAIs are detected
	 * as "CPU-Codec".
	 *
	 * In DPCM sound case,
	 * all CPUs   are detected as "CPU-dummy", and
	 * all Codecs are detected as "dummy-Codec".
	 * To avoid random sub-device numbering,
	 * detect "dummy-Codec" in last;
	 */
	for (li->cpu = 1; li->cpu >= 0; li->cpu--) {
		ret = __simple_for_each_link(priv, li, func_noml, func_dpcm);
		if (ret < 0)
			break;
	}

	return simple_ret(priv, ret);
}

static void simple_depopulate_aux(void *data)
{
	struct simple_util_priv *priv = data;

	of_platform_depopulate(simple_priv_to_dev(priv));
}

static int simple_populate_aux(struct simple_util_priv *priv)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *node __free(device_node) = of_get_child_by_name(dev->of_node, PREFIX "additional-devs");
	int ret;

	if (!node)
		return 0;

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret)
		goto end;

	ret = devm_add_action_or_reset(dev, simple_depopulate_aux, priv);
end:
	return simple_ret(priv, ret);
}

static int simple_parse_of(struct simple_util_priv *priv, struct link_info *li)
{
	struct snd_soc_card *card = simple_priv_to_card(priv);
	int ret;

	ret = simple_util_parse_widgets(card, PREFIX);
	if (ret < 0)
		goto end;

	ret = simple_util_parse_routing(card, PREFIX);
	if (ret < 0)
		goto end;

	ret = simple_util_parse_pin_switches(card, PREFIX);
	if (ret < 0)
		goto end;

	/* Single/Muti DAI link(s) & New style of DT node */
	memset(li, 0, sizeof(*li));
	ret = simple_for_each_link(priv, li,
				   simple_dai_link_of,
				   simple_dai_link_of_dpcm);
	if (ret < 0)
		goto end;

	ret = simple_util_parse_card_name(priv, PREFIX);
	if (ret < 0)
		goto end;

	ret = simple_populate_aux(priv);
	if (ret < 0)
		goto end;

	ret = snd_soc_of_parse_aux_devs(card, PREFIX "aux-devs");
end:
	return simple_ret(priv, ret);
}

static int simple_count_noml(struct simple_util_priv *priv,
			     struct device_node *np,
			     struct device_node *codec,
			     struct link_info *li, bool is_top)
{
	int ret = -EINVAL;

	if (li->link >= SNDRV_MAX_LINKS)
		goto end;

	/*
	 * DON'T REMOVE platforms
	 *
	 * Some CPU might be using soc-generic-dmaengine-pcm. This means CPU and Platform
	 * are different Component, but are sharing same component->dev.
	 * Simple Card had been supported it without special Platform selection.
	 * We need platforms here.
	 *
	 * In case of no Platform, it will be Platform == CPU, but Platform will be
	 * ignored by snd_soc_rtd_add_component().
	 *
	 * see
	 *	simple-card-utils.c :: simple_util_canonicalize_platform()
	 */
	li->num[li->link].cpus		= 1;
	li->num[li->link].platforms	= 1;

	li->num[li->link].codecs	= 1;

	li->link += 1;
	ret = 0;
end:
	return simple_ret(priv, ret);
}

static int simple_count_dpcm(struct simple_util_priv *priv,
			     struct device_node *np,
			     struct device_node *codec,
			     struct link_info *li, bool is_top)
{
	int ret = -EINVAL;

	if (li->link >= SNDRV_MAX_LINKS)
		goto end;

	if (li->cpu) {
		/*
		 * DON'T REMOVE platforms
		 * see
		 *	simple_count_noml()
		 */
		li->num[li->link].cpus		= 1;
		li->num[li->link].platforms	= 1;

		li->link++; /* CPU-dummy */
	} else {
		li->num[li->link].codecs	= 1;

		li->link++; /* dummy-Codec */
	}
	ret = 0;
end:
	return simple_ret(priv, ret);
}

static int simple_get_dais_count(struct simple_util_priv *priv,
				 struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *top = dev->of_node;

	/*
	 * link_num :	number of links.
	 *		CPU-Codec / CPU-dummy / dummy-Codec
	 * dais_num :	number of DAIs
	 * ccnf_num :	number of codec_conf
	 *		same number for "dummy-Codec"
	 *
	 * ex1)
	 * CPU0 --- Codec0	link : 5
	 * CPU1 --- Codec1	dais : 7
	 * CPU2 -/		ccnf : 1
	 * CPU3 --- Codec2
	 *
	 *	=> 5 links = 2xCPU-Codec + 2xCPU-dummy + 1xdummy-Codec
	 *	=> 7 DAIs  = 4xCPU + 3xCodec
	 *	=> 1 ccnf  = 1xdummy-Codec
	 *
	 * ex2)
	 * CPU0 --- Codec0	link : 5
	 * CPU1 --- Codec1	dais : 6
	 * CPU2 -/		ccnf : 1
	 * CPU3 -/
	 *
	 *	=> 5 links = 1xCPU-Codec + 3xCPU-dummy + 1xdummy-Codec
	 *	=> 6 DAIs  = 4xCPU + 2xCodec
	 *	=> 1 ccnf  = 1xdummy-Codec
	 *
	 * ex3)
	 * CPU0 --- Codec0	link : 6
	 * CPU1 -/		dais : 6
	 * CPU2 --- Codec1	ccnf : 2
	 * CPU3 -/
	 *
	 *	=> 6 links = 0xCPU-Codec + 4xCPU-dummy + 2xdummy-Codec
	 *	=> 6 DAIs  = 4xCPU + 2xCodec
	 *	=> 2 ccnf  = 2xdummy-Codec
	 *
	 * ex4)
	 * CPU0 --- Codec0 (convert-rate)	link : 3
	 * CPU1 --- Codec1			dais : 4
	 *					ccnf : 1
	 *
	 *	=> 3 links = 1xCPU-Codec + 1xCPU-dummy + 1xdummy-Codec
	 *	=> 4 DAIs  = 2xCPU + 2xCodec
	 *	=> 1 ccnf  = 1xdummy-Codec
	 */
	if (!top) {
		li->num[0].cpus		= 1;
		li->num[0].codecs	= 1;
		li->num[0].platforms	= 1;

		li->link = 1;
		return 0;
	}

	return simple_for_each_link(priv, li,
				    simple_count_noml,
				    simple_count_dpcm);
}

static int simple_soc_probe(struct snd_soc_card *card)
{
	struct simple_util_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;

	ret = simple_util_init_hp(card, &priv->hp_jack, PREFIX);
	if (ret < 0)
		goto end;

	ret = simple_util_init_mic(card, &priv->mic_jack, PREFIX);
	if (ret < 0)
		goto end;

	ret = simple_util_init_aux_jacks(priv, PREFIX);
end:
	return simple_ret(priv, ret);
}

static int simple_probe(struct platform_device *pdev)
{
	struct simple_util_priv *priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct snd_soc_card *card;
	int ret;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = simple_priv_to_card(priv);
	card->owner		= THIS_MODULE;
	card->dev		= dev;
	card->probe		= simple_soc_probe;
	card->driver_name       = "simple-card";

	ret = -ENOMEM;
	struct link_info *li __free(kfree) = kzalloc(sizeof(*li), GFP_KERNEL);
	if (!li)
		goto end;

	ret = simple_get_dais_count(priv, li);
	if (ret < 0)
		goto end;

	ret = -EINVAL;
	if (!li->link)
		goto end;

	ret = simple_util_init_priv(priv, li);
	if (ret < 0)
		goto end;

	if (np && of_device_is_available(np)) {

		ret = simple_parse_of(priv, li);
		if (ret < 0) {
			dev_err_probe(dev, ret, "parse error\n");
			goto err;
		}

	} else {
		struct simple_util_info *cinfo;
		struct snd_soc_dai_link_component *cpus;
		struct snd_soc_dai_link_component *codecs;
		struct snd_soc_dai_link_component *platform;
		struct snd_soc_dai_link *dai_link = priv->dai_link;
		struct simple_dai_props *dai_props = priv->dai_props;

		ret = -EINVAL;

		cinfo = dev->platform_data;
		if (!cinfo) {
			dev_err(dev, "no info for asoc-simple-card\n");
			goto err;
		}

		if (!cinfo->name ||
		    !cinfo->codec_dai.name ||
		    !cinfo->codec ||
		    !cinfo->platform ||
		    !cinfo->cpu_dai.name) {
			dev_err(dev, "insufficient simple_util_info settings\n");
			goto err;
		}

		cpus			= dai_link->cpus;
		cpus->dai_name		= cinfo->cpu_dai.name;

		codecs			= dai_link->codecs;
		codecs->name		= cinfo->codec;
		codecs->dai_name	= cinfo->codec_dai.name;

		platform		= dai_link->platforms;
		platform->name		= cinfo->platform;

		card->name		= (cinfo->card) ? cinfo->card : cinfo->name;
		dai_link->name		= cinfo->name;
		dai_link->stream_name	= cinfo->name;
		dai_link->dai_fmt	= cinfo->daifmt;
		dai_link->init		= simple_util_dai_init;
		memcpy(dai_props->cpu_dai, &cinfo->cpu_dai,
					sizeof(*dai_props->cpu_dai));
		memcpy(dai_props->codec_dai, &cinfo->codec_dai,
					sizeof(*dai_props->codec_dai));
	}

	snd_soc_card_set_drvdata(card, priv);

	simple_util_debug_info(priv);

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0)
		goto err;

	return 0;
err:
	simple_util_clean_reference(card);
end:
	return dev_err_probe(dev, ret, "parse error\n");
}

static const struct of_device_id simple_of_match[] = {
	{ .compatible = "simple-audio-card", },
	{ .compatible = "simple-scu-audio-card",
	  .data = (void *)DPCM_SELECTABLE },
	{},
};
MODULE_DEVICE_TABLE(of, simple_of_match);

static struct platform_driver simple_card = {
	.driver = {
		.name = "asoc-simple-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = simple_of_match,
	},
	.probe = simple_probe,
	.remove = simple_util_remove,
};

module_platform_driver(simple_card);

MODULE_ALIAS("platform:asoc-simple-card");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Simple Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
