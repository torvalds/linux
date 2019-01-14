// SPDX-License-Identifier: GPL-2.0
//
// ASoC simple sound card support
//
// Copyright (C) 2012 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/simple_card.h>
#include <sound/soc-dai.h>
#include <sound/soc.h>

struct simple_card_data {
	struct snd_soc_card snd_card;
	struct simple_dai_props {
		struct asoc_simple_dai cpu_dai;
		struct asoc_simple_dai codec_dai;
		unsigned int mclk_fs;
	} *dai_props;
	unsigned int mclk_fs;
	struct asoc_simple_jack hp_jack;
	struct asoc_simple_jack mic_jack;
	struct snd_soc_dai_link *dai_link;
};

#define simple_priv_to_card(priv) (&(priv)->snd_card)
#define simple_priv_to_props(priv, i) ((priv)->dai_props + (i))
#define simple_priv_to_dev(priv) (simple_priv_to_card(priv)->dev)
#define simple_priv_to_link(priv, i) (simple_priv_to_card(priv)->dai_link + (i))

#define DAI	"sound-dai"
#define CELL	"#sound-dai-cells"
#define PREFIX	"simple-audio-card,"

static int asoc_simple_card_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct simple_card_data *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props =
		simple_priv_to_props(priv, rtd->num);
	int ret;

	ret = asoc_simple_card_clk_enable(&dai_props->cpu_dai);
	if (ret)
		return ret;

	ret = asoc_simple_card_clk_enable(&dai_props->codec_dai);
	if (ret)
		asoc_simple_card_clk_disable(&dai_props->cpu_dai);

	return ret;
}

static void asoc_simple_card_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct simple_card_data *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props =
		simple_priv_to_props(priv, rtd->num);

	asoc_simple_card_clk_disable(&dai_props->cpu_dai);

	asoc_simple_card_clk_disable(&dai_props->codec_dai);
}

static int asoc_simple_set_clk_rate(struct asoc_simple_dai *simple_dai,
				    unsigned long rate)
{
	if (!simple_dai->clk)
		return 0;

	if (clk_get_rate(simple_dai->clk) == rate)
		return 0;

	return clk_set_rate(simple_dai->clk, rate);
}

static int asoc_simple_card_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct simple_card_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props =
		simple_priv_to_props(priv, rtd->num);
	unsigned int mclk, mclk_fs = 0;
	int ret = 0;

	if (priv->mclk_fs)
		mclk_fs = priv->mclk_fs;
	else if (dai_props->mclk_fs)
		mclk_fs = dai_props->mclk_fs;

	if (mclk_fs) {
		mclk = params_rate(params) * mclk_fs;

		ret = asoc_simple_set_clk_rate(&dai_props->codec_dai, mclk);
		if (ret < 0)
			return ret;

		ret = asoc_simple_set_clk_rate(&dai_props->cpu_dai, mclk);
		if (ret < 0)
			return ret;

		ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
					     SND_SOC_CLOCK_IN);
		if (ret && ret != -ENOTSUPP)
			goto err;

		ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
					     SND_SOC_CLOCK_OUT);
		if (ret && ret != -ENOTSUPP)
			goto err;
	}
	return 0;
err:
	return ret;
}

static const struct snd_soc_ops asoc_simple_card_ops = {
	.startup = asoc_simple_card_startup,
	.shutdown = asoc_simple_card_shutdown,
	.hw_params = asoc_simple_card_hw_params,
};

static int asoc_simple_card_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct simple_card_data *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec = rtd->codec_dai;
	struct snd_soc_dai *cpu = rtd->cpu_dai;
	struct simple_dai_props *dai_props =
		simple_priv_to_props(priv, rtd->num);
	int ret;

	ret = asoc_simple_card_init_dai(codec, &dai_props->codec_dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_init_dai(cpu, &dai_props->cpu_dai);
	if (ret < 0)
		return ret;

	return 0;
}

static int asoc_simple_card_dai_link_of(struct device_node *node,
					struct simple_card_data *priv,
					int idx,
					bool is_top_level_node)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, idx);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, idx);
	struct asoc_simple_dai *cpu_dai = &dai_props->cpu_dai;
	struct asoc_simple_dai *codec_dai = &dai_props->codec_dai;
	struct device_node *cpu = NULL;
	struct device_node *plat = NULL;
	struct device_node *codec = NULL;
	char prop[128];
	char *prefix = "";
	int ret, single_cpu;

	/* For single DAI link & old style of DT node */
	if (is_top_level_node)
		prefix = PREFIX;

	snprintf(prop, sizeof(prop), "%scpu", prefix);
	cpu = of_get_child_by_name(node, prop);

	if (!cpu) {
		ret = -EINVAL;
		dev_err(dev, "%s: Can't find %s DT node\n", __func__, prop);
		goto dai_link_of_err;
	}

	snprintf(prop, sizeof(prop), "%splat", prefix);
	plat = of_get_child_by_name(node, prop);

	snprintf(prop, sizeof(prop), "%scodec", prefix);
	codec = of_get_child_by_name(node, prop);

	if (!codec) {
		ret = -EINVAL;
		dev_err(dev, "%s: Can't find %s DT node\n", __func__, prop);
		goto dai_link_of_err;
	}

	ret = asoc_simple_card_parse_daifmt(dev, node, codec,
					    prefix, &dai_link->dai_fmt);
	if (ret < 0)
		goto dai_link_of_err;

	of_property_read_u32(node, "mclk-fs", &dai_props->mclk_fs);

	ret = asoc_simple_card_parse_cpu(cpu, dai_link,
					 DAI, CELL, &single_cpu);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_parse_codec(codec, dai_link, DAI, CELL);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_parse_platform(plat, dai_link, DAI, CELL);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_of_parse_tdm(cpu, cpu_dai);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_of_parse_tdm(codec, codec_dai);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_parse_clk_cpu(dev, cpu, dai_link, cpu_dai);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_parse_clk_codec(dev, codec, dai_link, codec_dai);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_canonicalize_dailink(dai_link);
	if (ret < 0)
		goto dai_link_of_err;

	ret = asoc_simple_card_set_dailink_name(dev, dai_link,
						"%s-%s",
						dai_link->cpu_dai_name,
						dai_link->codec_dai_name);
	if (ret < 0)
		goto dai_link_of_err;

	dai_link->ops = &asoc_simple_card_ops;
	dai_link->init = asoc_simple_card_dai_init;

	asoc_simple_card_canonicalize_cpu(dai_link, single_cpu);

dai_link_of_err:
	of_node_put(cpu);
	of_node_put(codec);

	return ret;
}

static int asoc_simple_card_parse_aux_devs(struct device_node *node,
					   struct simple_card_data *priv)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *aux_node;
	struct snd_soc_card *card = simple_priv_to_card(priv);
	int i, n, len;

	if (!of_find_property(node, PREFIX "aux-devs", &len))
		return 0;		/* Ok to have no aux-devs */

	n = len / sizeof(__be32);
	if (n <= 0)
		return -EINVAL;

	card->aux_dev = devm_kcalloc(dev,
			n, sizeof(*card->aux_dev), GFP_KERNEL);
	if (!card->aux_dev)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		aux_node = of_parse_phandle(node, PREFIX "aux-devs", i);
		if (!aux_node)
			return -EINVAL;
		card->aux_dev[i].codec_of_node = aux_node;
	}

	card->num_aux_devs = n;
	return 0;
}

static int asoc_simple_card_parse_of(struct simple_card_data *priv)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_card *card = simple_priv_to_card(priv);
	struct device_node *dai_link;
	struct device_node *node = dev->of_node;
	int ret;

	if (!node)
		return -EINVAL;

	dai_link = of_get_child_by_name(node, PREFIX "dai-link");

	ret = asoc_simple_card_of_parse_widgets(card, PREFIX);
	if (ret < 0)
		goto card_parse_end;

	ret = asoc_simple_card_of_parse_routing(card, PREFIX, 1);
	if (ret < 0)
		goto card_parse_end;

	/* Factor to mclk, used in hw_params() */
	of_property_read_u32(node, PREFIX "mclk-fs", &priv->mclk_fs);

	/* Single/Muti DAI link(s) & New style of DT node */
	if (dai_link) {
		struct device_node *np = NULL;
		int i = 0;

		for_each_child_of_node(node, np) {
			dev_dbg(dev, "\tlink %d:\n", i);
			ret = asoc_simple_card_dai_link_of(np, priv,
							   i, false);
			if (ret < 0) {
				of_node_put(np);
				goto card_parse_end;
			}
			i++;
		}
	} else {
		/* For single DAI link & old style of DT node */
		ret = asoc_simple_card_dai_link_of(node, priv, 0, true);
		if (ret < 0)
			goto card_parse_end;
	}

	ret = asoc_simple_card_parse_card_name(card, PREFIX);
	if (ret < 0)
		goto card_parse_end;

	ret = asoc_simple_card_parse_aux_devs(node, priv);

card_parse_end:
	of_node_put(dai_link);

	return ret;
}

static int asoc_simple_soc_card_probe(struct snd_soc_card *card)
{
	struct simple_card_data *priv = snd_soc_card_get_drvdata(card);
	int ret;

	ret = asoc_simple_card_init_hp(card, &priv->hp_jack, PREFIX);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_init_mic(card, &priv->mic_jack, PREFIX);
	if (ret < 0)
		return ret;

	return 0;
}

static int asoc_simple_card_probe(struct platform_device *pdev)
{
	struct simple_card_data *priv;
	struct snd_soc_dai_link *dai_link;
	struct simple_dai_props *dai_props;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct snd_soc_card *card;
	int num, ret;

	/* Get the number of DAI links */
	if (np && of_get_child_by_name(np, PREFIX "dai-link"))
		num = of_get_child_count(np);
	else
		num = 1;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dai_props = devm_kcalloc(dev, num, sizeof(*dai_props), GFP_KERNEL);
	dai_link  = devm_kcalloc(dev, num, sizeof(*dai_link), GFP_KERNEL);
	if (!dai_props || !dai_link)
		return -ENOMEM;

	priv->dai_props			= dai_props;
	priv->dai_link			= dai_link;

	/* Init snd_soc_card */
	card = simple_priv_to_card(priv);
	card->owner		= THIS_MODULE;
	card->dev		= dev;
	card->dai_link		= priv->dai_link;
	card->num_links		= num;
	card->probe		= asoc_simple_soc_card_probe;

	if (np && of_device_is_available(np)) {

		ret = asoc_simple_card_parse_of(priv);
		if (ret < 0) {
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "parse error %d\n", ret);
			goto err;
		}

	} else {
		struct asoc_simple_card_info *cinfo;

		cinfo = dev->platform_data;
		if (!cinfo) {
			dev_err(dev, "no info for asoc-simple-card\n");
			return -EINVAL;
		}

		if (!cinfo->name ||
		    !cinfo->codec_dai.name ||
		    !cinfo->codec ||
		    !cinfo->platform ||
		    !cinfo->cpu_dai.name) {
			dev_err(dev, "insufficient asoc_simple_card_info settings\n");
			return -EINVAL;
		}

		card->name		= (cinfo->card) ? cinfo->card : cinfo->name;
		dai_link->name		= cinfo->name;
		dai_link->stream_name	= cinfo->name;
		dai_link->platform_name	= cinfo->platform;
		dai_link->codec_name	= cinfo->codec;
		dai_link->cpu_dai_name	= cinfo->cpu_dai.name;
		dai_link->codec_dai_name = cinfo->codec_dai.name;
		dai_link->dai_fmt	= cinfo->daifmt;
		dai_link->init		= asoc_simple_card_dai_init;
		memcpy(&priv->dai_props->cpu_dai, &cinfo->cpu_dai,
					sizeof(priv->dai_props->cpu_dai));
		memcpy(&priv->dai_props->codec_dai, &cinfo->codec_dai,
					sizeof(priv->dai_props->codec_dai));
	}

	snd_soc_card_set_drvdata(card, priv);

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0)
		goto err;

	return 0;
err:
	asoc_simple_card_clean_reference(card);

	return ret;
}

static int asoc_simple_card_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	return asoc_simple_card_clean_reference(card);
}

static const struct of_device_id asoc_simple_of_match[] = {
	{ .compatible = "simple-audio-card", },
	{},
};
MODULE_DEVICE_TABLE(of, asoc_simple_of_match);

static struct platform_driver asoc_simple_card = {
	.driver = {
		.name = "asoc-simple-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = asoc_simple_of_match,
	},
	.probe = asoc_simple_card_probe,
	.remove = asoc_simple_card_remove,
};

module_platform_driver(asoc_simple_card);

MODULE_ALIAS("platform:asoc-simple-card");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Simple Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
