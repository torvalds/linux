// SPDX-License-Identifier: GPL-2.0
//
// ASoC audio graph SCU sound card support
//
// Copyright (C) 2017 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
// based on
//	${LINUX}/sound/soc/generic/simple-scu-card.c
//	${LINUX}/sound/soc/generic/audio-graph-card.c

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/jack.h>
#include <sound/simple_card_utils.h>

struct graph_card_data {
	struct snd_soc_card snd_card;
	struct snd_soc_codec_conf codec_conf;
	struct graph_dai_props {
		struct asoc_simple_dai dai;
		struct snd_soc_dai_link_component codecs;
		struct snd_soc_dai_link_component platform;
	} *dai_props;
	struct snd_soc_dai_link *dai_link;
	struct asoc_simple_card_data adata;
};

#define graph_priv_to_card(priv) (&(priv)->snd_card)
#define graph_priv_to_props(priv, i) ((priv)->dai_props + (i))
#define graph_priv_to_dev(priv) (graph_priv_to_card(priv)->dev)
#define graph_priv_to_link(priv, i) (graph_priv_to_card(priv)->dai_link + (i))

static int asoc_graph_card_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct graph_card_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);

	return asoc_simple_card_clk_enable(&dai_props->dai);
}

static void asoc_graph_card_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct graph_card_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);

	asoc_simple_card_clk_disable(&dai_props->dai);
}

static const struct snd_soc_ops asoc_graph_card_ops = {
	.startup = asoc_graph_card_startup,
	.shutdown = asoc_graph_card_shutdown,
};

static int asoc_graph_card_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct graph_card_data *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai;
	struct snd_soc_dai_link *dai_link;
	struct graph_dai_props *dai_props;
	int num = rtd->num;

	dai_link	= graph_priv_to_link(priv, num);
	dai_props	= graph_priv_to_props(priv, num);
	dai		= dai_link->dynamic ?
				rtd->cpu_dai :
				rtd->codec_dai;

	return asoc_simple_card_init_dai(dai, &dai_props->dai);
}

static int asoc_graph_card_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					       struct snd_pcm_hw_params *params)
{
	struct graph_card_data *priv = snd_soc_card_get_drvdata(rtd->card);

	asoc_simple_card_convert_fixup(&priv->adata, params);

	return 0;
}

static int asoc_graph_card_dai_link_of(struct device_node *ep,
				       struct graph_card_data *priv,
				       unsigned int daifmt,
				       int idx, int is_fe)
{
	struct device *dev = graph_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = graph_priv_to_link(priv, idx);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, idx);
	struct snd_soc_card *card = graph_priv_to_card(priv);
	int ret;

	if (is_fe) {
		struct snd_soc_dai_link_component *codecs;

		/* BE is dummy */
		codecs			= dai_link->codecs;
		codecs->of_node		= NULL;
		codecs->dai_name	= "snd-soc-dummy-dai";
		codecs->name		= "snd-soc-dummy";

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		ret = asoc_simple_card_parse_graph_cpu(ep, dai_link);
		if (ret)
			return ret;

		ret = asoc_simple_card_parse_clk_cpu(dev, ep, dai_link, &dai_props->dai);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_set_dailink_name(dev, dai_link,
							"fe.%s",
							dai_link->cpu_dai_name);
		if (ret < 0)
			return ret;

		/* card->num_links includes Codec */
		asoc_simple_card_canonicalize_cpu(dai_link,
			of_graph_get_endpoint_count(dai_link->cpu_of_node) == 1);
	} else {
		/* FE is dummy */
		dai_link->cpu_of_node		= NULL;
		dai_link->cpu_dai_name		= "snd-soc-dummy-dai";
		dai_link->cpu_name		= "snd-soc-dummy";

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= asoc_graph_card_be_hw_params_fixup;

		ret = asoc_simple_card_parse_graph_codec(ep, dai_link);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_parse_clk_codec(dev, ep, dai_link, &dai_props->dai);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_set_dailink_name(dev, dai_link,
							"be.%s",
							dai_link->codecs->dai_name);
		if (ret < 0)
			return ret;

		snd_soc_of_parse_audio_prefix(card,
					      &priv->codec_conf,
					      dai_link->codecs->of_node,
					      "prefix");
	}

	ret = asoc_simple_card_of_parse_tdm(ep, &dai_props->dai);
	if (ret)
		return ret;

	ret = asoc_simple_card_canonicalize_dailink(dai_link);
	if (ret < 0)
		return ret;

	dai_link->dai_fmt		= daifmt;
	dai_link->dpcm_playback		= 1;
	dai_link->dpcm_capture		= 1;
	dai_link->ops			= &asoc_graph_card_ops;
	dai_link->init			= asoc_graph_card_dai_init;

	return 0;
}

static int asoc_graph_card_parse_of(struct graph_card_data *priv)
{
	struct of_phandle_iterator it;
	struct device *dev = graph_priv_to_dev(priv);
	struct snd_soc_card *card = graph_priv_to_card(priv);
	struct device_node *node = dev->of_node;
	struct device_node *cpu_port;
	struct device_node *cpu_ep;
	struct device_node *codec_ep;
	struct device_node *codec_port;
	struct device_node *codec_port_old;
	unsigned int daifmt = 0;
	int dai_idx, ret;
	int rc, codec;

	if (!node)
		return -EINVAL;

	/*
	 * we need to consider "widgets", "mclk-fs" around here
	 * see simple-card
	 */

	ret = asoc_simple_card_of_parse_routing(card, NULL, 0);
	if (ret < 0)
		return ret;

	asoc_simple_card_parse_convert(dev, NULL, &priv->adata);

	/*
	 * it supports multi CPU, single CODEC only here
	 * see asoc_graph_get_dais_count
	 */

	/* find 1st codec */
	of_for_each_phandle(&it, rc, node, "dais", NULL, 0) {
		cpu_port = it.node;
		cpu_ep   = of_get_next_child(cpu_port, NULL);
		codec_ep = of_graph_get_remote_endpoint(cpu_ep);

		of_node_put(cpu_ep);
		of_node_put(codec_ep);

		if (!codec_ep)
			continue;

		ret = asoc_simple_card_parse_daifmt(dev, cpu_ep, codec_ep,
							    NULL, &daifmt);
		if (ret < 0) {
			of_node_put(cpu_port);
			goto parse_of_err;
		}
	}

	dai_idx = 0;
	codec_port_old = NULL;
	for (codec = 0; codec < 2; codec++) {
		/*
		 * To listup valid sounds continuously,
		 * detect all CPU-dummy first, and
		 * detect all dummy-Codec second
		 */
		of_for_each_phandle(&it, rc, node, "dais", NULL, 0) {
			cpu_port = it.node;
			cpu_ep   = of_get_next_child(cpu_port, NULL);
			codec_ep = of_graph_get_remote_endpoint(cpu_ep);
			codec_port = of_graph_get_port_parent(codec_ep);

			of_node_put(cpu_ep);
			of_node_put(codec_ep);
			of_node_put(codec_port);

			if (codec) {
				if (codec_port_old == codec_port)
					continue;

				codec_port_old = codec_port;

				/* Back-End (= Codec) */
				ret = asoc_graph_card_dai_link_of(codec_ep, priv, daifmt, dai_idx++, 0);
				if (ret < 0) {
					of_node_put(cpu_port);
					goto parse_of_err;
				}
			} else {
				/* Front-End (= CPU) */
				ret = asoc_graph_card_dai_link_of(cpu_ep, priv, daifmt, dai_idx++, 1);
				if (ret < 0) {
					of_node_put(cpu_port);
					goto parse_of_err;
				}
			}
		}
	}

	ret = asoc_simple_card_parse_card_name(card, NULL);
	if (ret)
		goto parse_of_err;

	ret = 0;

parse_of_err:
	return ret;
}

static int asoc_graph_get_dais_count(struct device *dev)
{
	struct of_phandle_iterator it;
	struct device_node *node = dev->of_node;
	struct device_node *cpu_port;
	struct device_node *cpu_ep;
	struct device_node *codec_ep;
	struct device_node *codec_port;
	struct device_node *codec_port_old;
	int count = 0;
	int rc;

	codec_port_old = NULL;
	of_for_each_phandle(&it, rc, node, "dais", NULL, 0) {
		cpu_port = it.node;
		cpu_ep   = of_get_next_child(cpu_port, NULL);
		codec_ep = of_graph_get_remote_endpoint(cpu_ep);
		codec_port = of_graph_get_port_parent(codec_ep);

		of_node_put(cpu_ep);
		of_node_put(codec_ep);
		of_node_put(codec_port);

		count++;

		if (codec_port_old == codec_port)
			continue;

		count++;
		codec_port_old = codec_port;
	}

	return count;
}

static int asoc_graph_card_probe(struct platform_device *pdev)
{
	struct graph_card_data *priv;
	struct snd_soc_dai_link *dai_link;
	struct graph_dai_props *dai_props;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	int num, ret, i;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	num = asoc_graph_get_dais_count(dev);
	if (num == 0)
		return -EINVAL;

	dai_props = devm_kcalloc(dev, num, sizeof(*dai_props), GFP_KERNEL);
	dai_link  = devm_kcalloc(dev, num, sizeof(*dai_link), GFP_KERNEL);
	if (!dai_props || !dai_link)
		return -ENOMEM;

	/*
	 * Use snd_soc_dai_link_component instead of legacy style
	 * It is codec only. but cpu/platform will be supported in the future.
	 * see
	 *	soc-core.c :: snd_soc_init_multicodec()
	 */
	for (i = 0; i < num; i++) {
		dai_link[i].codecs	= &dai_props[i].codecs;
		dai_link[i].num_codecs	= 1;
		dai_link[i].platform	= &dai_props[i].platform;
	}

	priv->dai_props			= dai_props;
	priv->dai_link			= dai_link;

	/* Init snd_soc_card */
	card = graph_priv_to_card(priv);
	card->owner		= THIS_MODULE;
	card->dev		= dev;
	card->dai_link		= priv->dai_link;
	card->num_links		= num;
	card->codec_conf	= &priv->codec_conf;
	card->num_configs	= 1;

	ret = asoc_graph_card_parse_of(priv);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "parse error %d\n", ret);
		goto err;
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

static int asoc_graph_card_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	return asoc_simple_card_clean_reference(card);
}

static const struct of_device_id asoc_graph_of_match[] = {
	{ .compatible = "audio-graph-scu-card", },
	{},
};
MODULE_DEVICE_TABLE(of, asoc_graph_of_match);

static struct platform_driver asoc_graph_card = {
	.driver = {
		.name = "asoc-audio-graph-scu-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = asoc_graph_of_match,
	},
	.probe = asoc_graph_card_probe,
	.remove = asoc_graph_card_remove,
};
module_platform_driver(asoc_graph_card);

MODULE_ALIAS("platform:asoc-audio-graph-scu-card");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Audio Graph SCU Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
