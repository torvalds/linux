/*
 * Renesas Sampling Rate Convert Sound Card for DPCM
 *
 * Copyright (C) 2015 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * based on ${LINUX}/sound/soc/generic/simple-card.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

struct rsrc_card_of_data {
	const char *prefix;
	const struct snd_soc_dapm_route *routes;
	int num_routes;
};

static const struct snd_soc_dapm_route routes_ssi0_ak4642[] = {
	{"ak4642 Playback", NULL, "DAI0 Playback"},
	{"DAI0 Capture", NULL, "ak4642 Capture"},
};

static const struct rsrc_card_of_data routes_of_ssi0_ak4642 = {
	.prefix		= "ak4642",
	.routes		= routes_ssi0_ak4642,
	.num_routes	= ARRAY_SIZE(routes_ssi0_ak4642),
};

static const struct of_device_id rsrc_card_of_match[] = {
	{ .compatible = "renesas,rsrc-card,lager",	.data = &routes_of_ssi0_ak4642 },
	{ .compatible = "renesas,rsrc-card,koelsch",	.data = &routes_of_ssi0_ak4642 },
	{ .compatible = "renesas,rsrc-card", },
	{},
};
MODULE_DEVICE_TABLE(of, rsrc_card_of_match);

#define DAI_NAME_NUM	32
struct rsrc_card_dai {
	unsigned int fmt;
	unsigned int sysclk;
	struct clk *clk;
	char dai_name[DAI_NAME_NUM];
};

#define IDX_CPU		0
#define IDX_CODEC	1
struct rsrc_card_priv {
	struct snd_soc_card snd_card;
	struct snd_soc_codec_conf codec_conf;
	struct rsrc_card_dai *dai_props;
	struct snd_soc_dai_link *dai_link;
	int dai_num;
	u32 convert_rate;
};

#define rsrc_priv_to_dev(priv) ((priv)->snd_card.dev)
#define rsrc_priv_to_link(priv, i) ((priv)->snd_card.dai_link + (i))
#define rsrc_priv_to_props(priv, i) ((priv)->dai_props + (i))
#define rsrc_dev_to_of_data(dev) (of_match_device(rsrc_card_of_match, (dev))->data)

static int rsrc_card_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rsrc_card_priv *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct rsrc_card_dai *dai_props =
		rsrc_priv_to_props(priv, rtd->num);

	return clk_prepare_enable(dai_props->clk);
}

static void rsrc_card_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct rsrc_card_priv *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct rsrc_card_dai *dai_props =
		rsrc_priv_to_props(priv, rtd->num);

	clk_disable_unprepare(dai_props->clk);
}

static struct snd_soc_ops rsrc_card_ops = {
	.startup = rsrc_card_startup,
	.shutdown = rsrc_card_shutdown,
};

static int rsrc_card_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct rsrc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai;
	struct snd_soc_dai_link *dai_link;
	struct rsrc_card_dai *dai_props;
	int num = rtd->num;
	int ret;

	dai_link	= rsrc_priv_to_link(priv, num);
	dai_props	= rsrc_priv_to_props(priv, num);
	dai		= dai_link->dynamic ?
				rtd->cpu_dai :
				rtd->codec_dai;

	if (dai_props->fmt) {
		ret = snd_soc_dai_set_fmt(dai, dai_props->fmt);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "set_fmt error\n");
			goto err;
		}
	}

	if (dai_props->sysclk) {
		ret = snd_soc_dai_set_sysclk(dai, 0, dai_props->sysclk, 0);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "set_sysclk error\n");
			goto err;
		}
	}

	ret = 0;

err:
	return ret;
}

static int rsrc_card_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					struct snd_pcm_hw_params *params)
{
	struct rsrc_card_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct snd_interval *rate = hw_param_interval(params,
						      SNDRV_PCM_HW_PARAM_RATE);

	if (!priv->convert_rate)
		return 0;

	rate->min = rate->max = priv->convert_rate;

	return 0;
}

static int rsrc_card_parse_daifmt(struct device_node *node,
				  struct device_node *np,
				  struct rsrc_card_priv *priv,
				  int idx, bool is_fe)
{
	struct rsrc_card_dai *dai_props = rsrc_priv_to_props(priv, idx);
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	struct device_node *codec = is_fe ? NULL : np;
	unsigned int daifmt;

	daifmt = snd_soc_of_parse_daifmt(node, NULL,
					 &bitclkmaster, &framemaster);
	daifmt &= ~SND_SOC_DAIFMT_MASTER_MASK;

	if (!bitclkmaster && !framemaster)
		return -EINVAL;

	if (codec == bitclkmaster)
		daifmt |= (codec == framemaster) ?
			SND_SOC_DAIFMT_CBM_CFM : SND_SOC_DAIFMT_CBM_CFS;
	else
		daifmt |= (codec == framemaster) ?
			SND_SOC_DAIFMT_CBS_CFM : SND_SOC_DAIFMT_CBS_CFS;

	dai_props->fmt	= daifmt;

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	return 0;
}

static int rsrc_card_parse_links(struct device_node *np,
				 struct rsrc_card_priv *priv,
				 int idx, bool is_fe)
{
	struct snd_soc_dai_link *dai_link = rsrc_priv_to_link(priv, idx);
	struct rsrc_card_dai *dai_props = rsrc_priv_to_props(priv, idx);
	struct of_phandle_args args;
	int ret;

	/*
	 * Get node via "sound-dai = <&phandle port>"
	 * it will be used as xxx_of_node on soc_bind_dai_link()
	 */
	ret = of_parse_phandle_with_args(np, "sound-dai",
					 "#sound-dai-cells", 0, &args);
	if (ret)
		return ret;

	if (is_fe) {
		/* BE is dummy */
		dai_link->codec_of_node		= NULL;
		dai_link->codec_dai_name	= "snd-soc-dummy-dai";
		dai_link->codec_name		= "snd-soc-dummy";

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;
		dai_link->cpu_of_node		= args.np;
		snd_soc_of_get_dai_name(np, &dai_link->cpu_dai_name);

		/* set dai_name */
		snprintf(dai_props->dai_name, DAI_NAME_NUM, "fe.%s",
			 dai_link->cpu_dai_name);

		/*
		 * In soc_bind_dai_link() will check cpu name after
		 * of_node matching if dai_link has cpu_dai_name.
		 * but, it will never match if name was created by
		 * fmt_single_name() remove cpu_dai_name if cpu_args
		 * was 0. See:
		 *	fmt_single_name()
		 *	fmt_multiple_name()
		 */
		if (!args.args_count)
			dai_link->cpu_dai_name = NULL;
	} else {
		struct device *dev = rsrc_priv_to_dev(priv);
		const struct rsrc_card_of_data *of_data;

		of_data = rsrc_dev_to_of_data(dev);

		/* FE is dummy */
		dai_link->cpu_of_node		= NULL;
		dai_link->cpu_dai_name		= "snd-soc-dummy-dai";
		dai_link->cpu_name		= "snd-soc-dummy";

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= rsrc_card_be_hw_params_fixup;
		dai_link->codec_of_node		= args.np;
		snd_soc_of_get_dai_name(np, &dai_link->codec_dai_name);

		/* additional name prefix */
		if (of_data) {
			priv->codec_conf.of_node = dai_link->codec_of_node;
			priv->codec_conf.name_prefix = of_data->prefix;
		} else {
			snd_soc_of_parse_audio_prefix(&priv->snd_card,
						      &priv->codec_conf,
						      dai_link->codec_of_node,
						      "audio-prefix");
		}

		/* set dai_name */
		snprintf(dai_props->dai_name, DAI_NAME_NUM, "be.%s",
			 dai_link->codec_dai_name);
	}

	/* Simple Card assumes platform == cpu */
	dai_link->platform_of_node	= dai_link->cpu_of_node;
	dai_link->dpcm_playback		= 1;
	dai_link->dpcm_capture		= 1;
	dai_link->name			= dai_props->dai_name;
	dai_link->stream_name		= dai_props->dai_name;
	dai_link->ops			= &rsrc_card_ops;
	dai_link->init			= rsrc_card_dai_init;

	return 0;
}

static int rsrc_card_parse_clk(struct device_node *np,
			       struct rsrc_card_priv *priv,
			       int idx, bool is_fe)
{
	struct snd_soc_dai_link *dai_link = rsrc_priv_to_link(priv, idx);
	struct rsrc_card_dai *dai_props = rsrc_priv_to_props(priv, idx);
	struct clk *clk;
	struct device_node *of_np = is_fe ?	dai_link->cpu_of_node :
						dai_link->codec_of_node;
	u32 val;

	/*
	 * Parse dai->sysclk come from "clocks = <&xxx>"
	 * (if system has common clock)
	 *  or "system-clock-frequency = <xxx>"
	 *  or device's module clock.
	 */
	if (of_property_read_bool(np, "clocks")) {
		clk = of_clk_get(np, 0);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		dai_props->sysclk = clk_get_rate(clk);
		dai_props->clk = clk;
	} else if (!of_property_read_u32(np, "system-clock-frequency", &val)) {
		dai_props->sysclk = val;
	} else {
		clk = of_clk_get(of_np, 0);
		if (!IS_ERR(clk))
			dai_props->sysclk = clk_get_rate(clk);
	}

	return 0;
}

static int rsrc_card_dai_link_of(struct device_node *node,
				 struct device_node *np,
				 struct rsrc_card_priv *priv,
				 int idx)
{
	struct device *dev = rsrc_priv_to_dev(priv);
	struct rsrc_card_dai *dai_props = rsrc_priv_to_props(priv, idx);
	bool is_fe = false;
	int ret;

	if (0 == strcmp(np->name, "cpu"))
		is_fe = true;

	ret = rsrc_card_parse_daifmt(node, np, priv, idx, is_fe);
	if (ret < 0)
		return ret;

	ret = rsrc_card_parse_links(np, priv, idx, is_fe);
	if (ret < 0)
		return ret;

	ret = rsrc_card_parse_clk(np, priv, idx, is_fe);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "\t%s / %04x / %d\n",
		dai_props->dai_name,
		dai_props->fmt,
		dai_props->sysclk);

	return ret;
}

static int rsrc_card_parse_of(struct device_node *node,
			      struct rsrc_card_priv *priv,
			      struct device *dev)
{
	const struct rsrc_card_of_data *of_data = rsrc_dev_to_of_data(dev);
	struct rsrc_card_dai *props;
	struct snd_soc_dai_link *links;
	struct device_node *np;
	int ret;
	int i, num;

	if (!node)
		return -EINVAL;

	num = of_get_child_count(node);
	props = devm_kzalloc(dev, sizeof(*props) * num, GFP_KERNEL);
	links = devm_kzalloc(dev, sizeof(*links) * num, GFP_KERNEL);
	if (!props || !links)
		return -ENOMEM;

	priv->dai_props	= props;
	priv->dai_link	= links;
	priv->dai_num	= num;

	/* Init snd_soc_card */
	priv->snd_card.owner			= THIS_MODULE;
	priv->snd_card.dev			= dev;
	priv->snd_card.dai_link			= priv->dai_link;
	priv->snd_card.num_links		= num;
	priv->snd_card.codec_conf		= &priv->codec_conf;
	priv->snd_card.num_configs		= 1;

	if (of_data) {
		priv->snd_card.of_dapm_routes		= of_data->routes;
		priv->snd_card.num_of_dapm_routes	= of_data->num_routes;
	} else {
		snd_soc_of_parse_audio_routing(&priv->snd_card,
					       "audio-routing");
	}

	/* Parse the card name from DT */
	snd_soc_of_parse_card_name(&priv->snd_card, "card-name");

	/* sampling rate convert */
	of_property_read_u32(node, "convert-rate", &priv->convert_rate);

	dev_dbg(dev, "New rsrc-audio-card: %s (%d)\n",
		priv->snd_card.name ? priv->snd_card.name : "",
		priv->convert_rate);

	i = 0;
	for_each_child_of_node(node, np) {
		ret = rsrc_card_dai_link_of(node, np, priv, i);
		if (ret < 0)
			return ret;
		i++;
	}

	if (!priv->snd_card.name)
		priv->snd_card.name = priv->snd_card.dai_link->name;

	return 0;
}

/* Decrease the reference count of the device nodes */
static int rsrc_card_unref(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	int num_links;

	for (num_links = 0, dai_link = card->dai_link;
	     num_links < card->num_links;
	     num_links++, dai_link++) {
		of_node_put(dai_link->cpu_of_node);
		of_node_put(dai_link->codec_of_node);
	}
	return 0;
}

static int rsrc_card_probe(struct platform_device *pdev)
{
	struct rsrc_card_priv *priv;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int ret;

	/* Allocate the private data */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = rsrc_card_parse_of(np, priv, dev);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "parse error %d\n", ret);
		goto err;
	}

	snd_soc_card_set_drvdata(&priv->snd_card, priv);

	ret = devm_snd_soc_register_card(&pdev->dev, &priv->snd_card);
	if (ret >= 0)
		return ret;
err:
	rsrc_card_unref(&priv->snd_card);

	return ret;
}

static int rsrc_card_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	return rsrc_card_unref(card);
}

static struct platform_driver rsrc_card = {
	.driver = {
		.name = "renesas-src-audio-card",
		.of_match_table = rsrc_card_of_match,
	},
	.probe = rsrc_card_probe,
	.remove = rsrc_card_remove,
};

module_platform_driver(rsrc_card);

MODULE_ALIAS("platform:renesas-src-audio-card");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas Sampling Rate Convert Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
