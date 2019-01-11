// SPDX-License-Identifier: GPL-2.0
//
// ASoC audio graph sound card support
//
// Copyright (C) 2016 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
// based on ${LINUX}/sound/soc/generic/simple-card.c

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/simple_card_utils.h>

struct graph_card_data {
	struct snd_soc_card snd_card;
	struct graph_dai_props {
		struct asoc_simple_dai *cpu_dai;
		struct asoc_simple_dai *codec_dai;
		struct snd_soc_dai_link_component codecs; /* single codec */
		struct snd_soc_dai_link_component platform;
		struct asoc_simple_card_data adata;
		struct snd_soc_codec_conf *codec_conf;
		unsigned int mclk_fs;
	} *dai_props;
	struct asoc_simple_jack hp_jack;
	struct asoc_simple_jack mic_jack;
	struct snd_soc_dai_link *dai_link;
	struct asoc_simple_dai *dais;
	struct snd_soc_codec_conf *codec_conf;
	struct gpio_desc *pa_gpio;
};

#define graph_priv_to_card(priv) (&(priv)->snd_card)
#define graph_priv_to_props(priv, i) ((priv)->dai_props + (i))
#define graph_priv_to_dev(priv) (graph_priv_to_card(priv)->dev)
#define graph_priv_to_link(priv, i) (graph_priv_to_card(priv)->dai_link + (i))

#define PREFIX	"audio-graph-card,"

static int asoc_graph_card_outdrv_event(struct snd_soc_dapm_widget *w,
					struct snd_kcontrol *kcontrol,
					int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct graph_card_data *priv = snd_soc_card_get_drvdata(dapm->card);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		gpiod_set_value_cansleep(priv->pa_gpio, 1);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		gpiod_set_value_cansleep(priv->pa_gpio, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dapm_widget asoc_graph_card_dapm_widgets[] = {
	SND_SOC_DAPM_OUT_DRV_E("Amplifier", SND_SOC_NOPM,
			       0, 0, NULL, 0, asoc_graph_card_outdrv_event,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
};

static int asoc_graph_card_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct graph_card_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);
	int ret;

	ret = asoc_simple_card_clk_enable(dai_props->cpu_dai);
	if (ret)
		return ret;

	ret = asoc_simple_card_clk_enable(dai_props->codec_dai);
	if (ret)
		asoc_simple_card_clk_disable(dai_props->cpu_dai);

	return ret;
}

static void asoc_graph_card_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct graph_card_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);

	asoc_simple_card_clk_disable(dai_props->cpu_dai);

	asoc_simple_card_clk_disable(dai_props->codec_dai);
}

static int asoc_graph_card_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct graph_card_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);
	unsigned int mclk, mclk_fs = 0;
	int ret = 0;

	if (dai_props->mclk_fs)
		mclk_fs = dai_props->mclk_fs;

	if (mclk_fs) {
		mclk = params_rate(params) * mclk_fs;
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

static const struct snd_soc_ops asoc_graph_card_ops = {
	.startup = asoc_graph_card_startup,
	.shutdown = asoc_graph_card_shutdown,
	.hw_params = asoc_graph_card_hw_params,
};

static int asoc_graph_card_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct graph_card_data *priv =	snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);
	int ret = 0;

	ret = asoc_simple_card_init_dai(rtd->codec_dai,
					dai_props->codec_dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_init_dai(rtd->cpu_dai,
					dai_props->cpu_dai);
	if (ret < 0)
		return ret;

	return 0;
}

static int asoc_graph_card_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					      struct snd_pcm_hw_params *params)
{
	struct graph_card_data *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);

	asoc_simple_card_convert_fixup(&dai_props->adata, params);

	return 0;
}

static int asoc_graph_card_dai_link_of_dpcm(struct device_node *top,
					    struct device_node *cpu_ep,
					    struct device_node *codec_ep,
					    struct graph_card_data *priv,
					    int *dai_idx, int link_idx,
					    int *conf_idx, int is_cpu)
{
	struct device *dev = graph_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = graph_priv_to_link(priv, link_idx);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, link_idx);
	struct device_node *ep = is_cpu ? cpu_ep : codec_ep;
	struct device_node *port = of_get_parent(ep);
	struct device_node *ports = of_get_parent(port);
	struct device_node *node = of_graph_get_port_parent(ep);
	struct asoc_simple_dai *dai;
	struct snd_soc_dai_link_component *codecs = dai_link->codecs;
	int ret;

	dev_dbg(dev, "link_of DPCM (for %s)\n", is_cpu ? "CPU" : "Codec");

	of_property_read_u32(top,   "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ports, "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(port,  "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ep,    "mclk-fs", &dai_props->mclk_fs);

	asoc_simple_card_parse_convert(dev, top,   NULL,   &dai_props->adata);
	asoc_simple_card_parse_convert(dev, node,  PREFIX, &dai_props->adata);
	asoc_simple_card_parse_convert(dev, ports, NULL,   &dai_props->adata);
	asoc_simple_card_parse_convert(dev, port,  NULL,   &dai_props->adata);
	asoc_simple_card_parse_convert(dev, ep,    NULL,   &dai_props->adata);

	of_node_put(ports);
	of_node_put(port);

	if (is_cpu) {

		/* BE is dummy */
		codecs->of_node		= NULL;
		codecs->dai_name	= "snd-soc-dummy-dai";
		codecs->name		= "snd-soc-dummy";

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		dai =
		dai_props->cpu_dai	= &priv->dais[(*dai_idx)++];

		ret = asoc_simple_card_parse_graph_cpu(ep, dai_link);
		if (ret)
			return ret;

		ret = asoc_simple_card_parse_clk_cpu(dev, ep, dai_link, dai);
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
		struct snd_soc_codec_conf *cconf;

		/* FE is dummy */
		dai_link->cpu_of_node		= NULL;
		dai_link->cpu_dai_name		= "snd-soc-dummy-dai";
		dai_link->cpu_name		= "snd-soc-dummy";

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= asoc_graph_card_be_hw_params_fixup;

		dai =
		dai_props->codec_dai	= &priv->dais[(*dai_idx)++];

		cconf =
		dai_props->codec_conf	= &priv->codec_conf[(*conf_idx)++];

		ret = asoc_simple_card_parse_graph_codec(ep, dai_link);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_parse_clk_codec(dev, ep, dai_link, dai);
		if (ret < 0)
			return ret;

		ret = asoc_simple_card_set_dailink_name(dev, dai_link,
							"be.%s",
							codecs->dai_name);
		if (ret < 0)
			return ret;

		/* check "prefix" from top node */
		snd_soc_of_parse_node_prefix(top, cconf, codecs->of_node,
					      "prefix");
		snd_soc_of_parse_node_prefix(node, cconf, codecs->of_node,
					     PREFIX "prefix");
		snd_soc_of_parse_node_prefix(ports, cconf, codecs->of_node,
					     "prefix");
		snd_soc_of_parse_node_prefix(port, cconf, codecs->of_node,
					     "prefix");
	}

	ret = asoc_simple_card_of_parse_tdm(ep, dai);
	if (ret)
		return ret;

	ret = asoc_simple_card_canonicalize_dailink(dai_link);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_parse_daifmt(dev, cpu_ep, codec_ep,
					    NULL, &dai_link->dai_fmt);
	if (ret < 0)
		return ret;

	dai_link->dpcm_playback		= 1;
	dai_link->dpcm_capture		= 1;
	dai_link->ops			= &asoc_graph_card_ops;
	dai_link->init			= asoc_graph_card_dai_init;

	return 0;
}

static int asoc_graph_card_dai_link_of(struct device_node *top,
					struct device_node *cpu_ep,
					struct device_node *codec_ep,
					struct graph_card_data *priv,
					int *dai_idx, int link_idx)
{
	struct device *dev = graph_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = graph_priv_to_link(priv, link_idx);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, link_idx);
	struct device_node *cpu_port = of_get_parent(cpu_ep);
	struct device_node *codec_port = of_get_parent(codec_ep);
	struct device_node *cpu_ports = of_get_parent(cpu_port);
	struct device_node *codec_ports = of_get_parent(codec_port);
	struct asoc_simple_dai *cpu_dai;
	struct asoc_simple_dai *codec_dai;
	int ret;

	dev_dbg(dev, "link_of\n");

	cpu_dai			=
	dai_props->cpu_dai	= &priv->dais[(*dai_idx)++];
	codec_dai		=
	dai_props->codec_dai	= &priv->dais[(*dai_idx)++];

	/* Factor to mclk, used in hw_params() */
	of_property_read_u32(top,         "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(cpu_ports,   "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(codec_ports, "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(cpu_port,    "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(codec_port,  "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(cpu_ep,      "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(codec_ep,    "mclk-fs", &dai_props->mclk_fs);
	of_node_put(cpu_port);
	of_node_put(cpu_ports);
	of_node_put(codec_port);
	of_node_put(codec_ports);

	ret = asoc_simple_card_parse_daifmt(dev, cpu_ep, codec_ep,
					    NULL, &dai_link->dai_fmt);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_parse_graph_cpu(cpu_ep, dai_link);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_parse_graph_codec(codec_ep, dai_link);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_of_parse_tdm(cpu_ep, cpu_dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_of_parse_tdm(codec_ep, codec_dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_parse_clk_cpu(dev, cpu_ep, dai_link, cpu_dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_parse_clk_codec(dev, codec_ep, dai_link, codec_dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_canonicalize_dailink(dai_link);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_set_dailink_name(dev, dai_link,
						"%s-%s",
						dai_link->cpu_dai_name,
						dai_link->codecs->dai_name);
	if (ret < 0)
		return ret;

	dai_link->ops = &asoc_graph_card_ops;
	dai_link->init = asoc_graph_card_dai_init;

	asoc_simple_card_canonicalize_cpu(dai_link,
		of_graph_get_endpoint_count(dai_link->cpu_of_node) == 1);

	return 0;
}

static int asoc_graph_card_parse_of(struct graph_card_data *priv)
{
	struct of_phandle_iterator it;
	struct device *dev = graph_priv_to_dev(priv);
	struct snd_soc_card *card = graph_priv_to_card(priv);
	struct device_node *top = dev->of_node;
	struct device_node *node = top;
	struct device_node *cpu_port;
	struct device_node *cpu_ep		= NULL;
	struct device_node *codec_ep		= NULL;
	struct device_node *codec_port		= NULL;
	struct device_node *codec_port_old	= NULL;
	int rc, ret;
	int link_idx, dai_idx, conf_idx;
	int cpu;

	ret = asoc_simple_card_of_parse_widgets(card, NULL);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_of_parse_routing(card, NULL);
	if (ret < 0)
		return ret;

	link_idx	= 0;
	dai_idx		= 0;
	conf_idx	= 0;
	codec_port_old	= NULL;
	for (cpu = 1; cpu >= 0; cpu--) {
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
		of_for_each_phandle(&it, rc, node, "dais", NULL, 0) {
			cpu_port = it.node;
			cpu_ep	 = NULL;
			while (1) {
				cpu_ep = of_get_next_child(cpu_port, cpu_ep);
				if (!cpu_ep)
					break;

				codec_ep   = of_graph_get_remote_endpoint(cpu_ep);
				codec_port = of_get_parent(codec_ep);

				of_node_put(codec_ep);
				of_node_put(codec_port);

				dev_dbg(dev, "%pOFf <-> %pOFf\n", cpu_ep, codec_ep);

				if (of_get_child_count(codec_port) > 1) {
					/*
					 * for DPCM sound
					 */
					if (!cpu) {
						if (codec_port_old == codec_port)
							continue;
						codec_port_old = codec_port;
					}
					ret = asoc_graph_card_dai_link_of_dpcm(
						top, cpu_ep, codec_ep, priv,
						&dai_idx, link_idx++,
						&conf_idx, cpu);
				} else if (cpu) {
					/*
					 * for Normal sound
					 */
					ret = asoc_graph_card_dai_link_of(
						top, cpu_ep, codec_ep, priv,
						&dai_idx, link_idx++);
				}
				if (ret < 0)
					return ret;
			}
		}
	}

	return asoc_simple_card_parse_card_name(card, NULL);
}

static void asoc_graph_get_dais_count(struct device *dev,
				      int *link_num,
				      int *dais_num,
				      int *ccnf_num)
{
	struct of_phandle_iterator it;
	struct device_node *node = dev->of_node;
	struct device_node *cpu_port;
	struct device_node *cpu_ep;
	struct device_node *codec_ep;
	struct device_node *codec_port;
	struct device_node *codec_port_old;
	struct device_node *codec_port_old2;
	int rc;

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
	 */
	codec_port_old = NULL;
	codec_port_old2 = NULL;
	of_for_each_phandle(&it, rc, node, "dais", NULL, 0) {
		cpu_port = it.node;
		cpu_ep	 = NULL;
		while (1) {
			cpu_ep = of_get_next_child(cpu_port, cpu_ep);
			if (!cpu_ep)
				break;

			codec_ep = of_graph_get_remote_endpoint(cpu_ep);
			codec_port = of_get_parent(codec_ep);

			of_node_put(codec_ep);
			of_node_put(codec_port);

			(*link_num)++;
			(*dais_num)++;

			if (codec_port_old == codec_port) {
				if (codec_port_old2 != codec_port_old) {
					(*link_num)++;
					(*ccnf_num)++;
				}

				codec_port_old2 = codec_port_old;
				continue;
			}

			(*dais_num)++;
			codec_port_old = codec_port;
		}
	}
}

static int asoc_graph_soc_card_probe(struct snd_soc_card *card)
{
	struct graph_card_data *priv = snd_soc_card_get_drvdata(card);
	int ret;

	ret = asoc_simple_card_init_hp(card, &priv->hp_jack, NULL);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_init_mic(card, &priv->mic_jack, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

static int asoc_graph_card_probe(struct platform_device *pdev)
{
	struct graph_card_data *priv;
	struct snd_soc_dai_link *dai_link;
	struct graph_dai_props *dai_props;
	struct asoc_simple_dai *dais;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct snd_soc_codec_conf *cconf;
	int lnum = 0, dnum = 0, cnum = 0;
	int ret, i;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	asoc_graph_get_dais_count(dev, &lnum, &dnum, &cnum);
	if (!lnum || !dnum)
		return -EINVAL;

	dai_props = devm_kcalloc(dev, lnum, sizeof(*dai_props), GFP_KERNEL);
	dai_link  = devm_kcalloc(dev, lnum, sizeof(*dai_link),  GFP_KERNEL);
	dais      = devm_kcalloc(dev, dnum, sizeof(*dais),      GFP_KERNEL);
	cconf     = devm_kcalloc(dev, cnum, sizeof(*cconf),     GFP_KERNEL);
	if (!dai_props || !dai_link || !dais)
		return -ENOMEM;

	/*
	 * Use snd_soc_dai_link_component instead of legacy style
	 * It is codec only. but cpu/platform will be supported in the future.
	 * see
	 *	soc-core.c :: snd_soc_init_multicodec()
	 */
	for (i = 0; i < lnum; i++) {
		dai_link[i].codecs	= &dai_props[i].codecs;
		dai_link[i].num_codecs	= 1;
		dai_link[i].platform	= &dai_props[i].platform;
	}

	priv->pa_gpio = devm_gpiod_get_optional(dev, "pa", GPIOD_OUT_LOW);
	if (IS_ERR(priv->pa_gpio)) {
		ret = PTR_ERR(priv->pa_gpio);
		dev_err(dev, "failed to get amplifier gpio: %d\n", ret);
		return ret;
	}

	priv->dai_props			= dai_props;
	priv->dai_link			= dai_link;
	priv->dais			= dais;
	priv->codec_conf		= cconf;

	/* Init snd_soc_card */
	card = graph_priv_to_card(priv);
	card->owner		= THIS_MODULE;
	card->dev		= dev;
	card->dai_link		= dai_link;
	card->num_links		= lnum;
	card->dapm_widgets	= asoc_graph_card_dapm_widgets;
	card->num_dapm_widgets	= ARRAY_SIZE(asoc_graph_card_dapm_widgets);
	card->probe		= asoc_graph_soc_card_probe;
	card->codec_conf	= cconf;
	card->num_configs	= cnum;

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
	{ .compatible = "audio-graph-card", },
	{ .compatible = "audio-graph-scu-card", },
	{},
};
MODULE_DEVICE_TABLE(of, asoc_graph_of_match);

static struct platform_driver asoc_graph_card = {
	.driver = {
		.name = "asoc-audio-graph-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = asoc_graph_of_match,
	},
	.probe = asoc_graph_card_probe,
	.remove = asoc_graph_card_remove,
};
module_platform_driver(asoc_graph_card);

MODULE_ALIAS("platform:asoc-audio-graph-card");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Audio Graph Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
