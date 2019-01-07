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

struct graph_priv {
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

struct link_info {
	int dais; /* number of dai  */
	int link; /* number of link */
	int conf; /* number of codec_conf */
	int cpu;  /* turn for CPU / Codec */
};

#define graph_priv_to_card(priv) (&(priv)->snd_card)
#define graph_priv_to_props(priv, i) ((priv)->dai_props + (i))
#define graph_priv_to_dev(priv) (graph_priv_to_card(priv)->dev)
#define graph_priv_to_link(priv, i) (graph_priv_to_card(priv)->dai_link + (i))

#define PREFIX	"audio-graph-card,"

static int graph_outdrv_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct graph_priv *priv = snd_soc_card_get_drvdata(dapm->card);

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

static const struct snd_soc_dapm_widget graph_dapm_widgets[] = {
	SND_SOC_DAPM_OUT_DRV_E("Amplifier", SND_SOC_NOPM,
			       0, 0, NULL, 0, graph_outdrv_event,
			       SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
};

static int graph_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct graph_priv *priv = snd_soc_card_get_drvdata(rtd->card);
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

static void graph_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct graph_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);

	asoc_simple_card_clk_disable(dai_props->cpu_dai);

	asoc_simple_card_clk_disable(dai_props->codec_dai);
}

static int graph_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct graph_priv *priv = snd_soc_card_get_drvdata(rtd->card);
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

static const struct snd_soc_ops graph_ops = {
	.startup	= graph_startup,
	.shutdown	= graph_shutdown,
	.hw_params	= graph_hw_params,
};

static int graph_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct graph_priv *priv = snd_soc_card_get_drvdata(rtd->card);
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

static int graph_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				    struct snd_pcm_hw_params *params)
{
	struct graph_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, rtd->num);

	asoc_simple_card_convert_fixup(&dai_props->adata, params);

	return 0;
}

static void graph_get_conversion(struct device *dev,
				 struct device_node *ep,
				 struct asoc_simple_card_data *adata)
{
	struct device_node *top = dev->of_node;
	struct device_node *port = of_get_parent(ep);
	struct device_node *ports = of_get_parent(port);
	struct device_node *node = of_graph_get_port_parent(ep);

	asoc_simple_card_parse_convert(dev, top,   NULL,   adata);
	asoc_simple_card_parse_convert(dev, node,  PREFIX, adata);
	asoc_simple_card_parse_convert(dev, ports, NULL,   adata);
	asoc_simple_card_parse_convert(dev, port,  NULL,   adata);
	asoc_simple_card_parse_convert(dev, ep,    NULL,   adata);
}

static int graph_dai_link_of_dpcm(struct graph_priv *priv,
				  struct device_node *cpu_ep,
				  struct device_node *codec_ep,
				  struct link_info *li,
				  int dup_codec)
{
	struct device *dev = graph_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = graph_priv_to_link(priv, li->link);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, li->link);
	struct device_node *top = dev->of_node;
	struct device_node *ep = li->cpu ? cpu_ep : codec_ep;
	struct device_node *port;
	struct device_node *ports;
	struct device_node *node;
	struct asoc_simple_dai *dai;
	struct snd_soc_dai_link_component *codecs = dai_link->codecs;
	int ret;

	/* Do it all CPU endpoint, and 1st Codec endpoint */
	if (!li->cpu && dup_codec)
		return 0;

	port	= of_get_parent(ep);
	ports	= of_get_parent(port);
	node	= of_graph_get_port_parent(ep);

	li->link++;

	dev_dbg(dev, "link_of DPCM (%pOF)\n", ep);

	of_property_read_u32(top,   "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ports, "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(port,  "mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ep,    "mclk-fs", &dai_props->mclk_fs);

	graph_get_conversion(dev, ep, &dai_props->adata);

	of_node_put(ports);
	of_node_put(port);
	of_node_put(node);

	if (li->cpu) {

		/* BE is dummy */
		codecs->of_node		= NULL;
		codecs->dai_name	= "snd-soc-dummy-dai";
		codecs->name		= "snd-soc-dummy";

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		dai =
		dai_props->cpu_dai	= &priv->dais[li->dais++];

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
		dai_link->be_hw_params_fixup	= graph_be_hw_params_fixup;

		dai =
		dai_props->codec_dai	= &priv->dais[li->dais++];

		cconf =
		dai_props->codec_conf	= &priv->codec_conf[li->conf++];

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
	dai_link->ops			= &graph_ops;
	dai_link->init			= graph_dai_init;

	return 0;
}

static int graph_dai_link_of(struct graph_priv *priv,
			     struct device_node *cpu_ep,
			     struct device_node *codec_ep,
			     struct link_info *li)
{
	struct device *dev = graph_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = graph_priv_to_link(priv, li->link);
	struct graph_dai_props *dai_props = graph_priv_to_props(priv, li->link);
	struct device_node *top = dev->of_node;
	struct device_node *cpu_port;
	struct device_node *cpu_ports;
	struct device_node *codec_port;
	struct device_node *codec_ports;
	struct asoc_simple_dai *cpu_dai;
	struct asoc_simple_dai *codec_dai;
	int ret;

	/* Do it only CPU turn */
	if (!li->cpu)
		return 0;

	cpu_port	= of_get_parent(cpu_ep);
	cpu_ports	= of_get_parent(cpu_port);
	codec_port	= of_get_parent(codec_ep);
	codec_ports	= of_get_parent(codec_port);

	dev_dbg(dev, "link_of (%pOF)\n", cpu_ep);

	li->link++;

	cpu_dai			=
	dai_props->cpu_dai	= &priv->dais[li->dais++];
	codec_dai		=
	dai_props->codec_dai	= &priv->dais[li->dais++];

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

	dai_link->ops = &graph_ops;
	dai_link->init = graph_dai_init;

	asoc_simple_card_canonicalize_cpu(dai_link,
		of_graph_get_endpoint_count(dai_link->cpu_of_node) == 1);

	return 0;
}

static int graph_for_each_link(struct graph_priv *priv,
			struct link_info *li,
			int (*func_noml)(struct graph_priv *priv,
					 struct device_node *cpu_ep,
					 struct device_node *codec_ep,
					 struct link_info *li),
			int (*func_dpcm)(struct graph_priv *priv,
					 struct device_node *cpu_ep,
					 struct device_node *codec_ep,
					 struct link_info *li, int dup_codec))
{
	struct of_phandle_iterator it;
	struct device *dev = graph_priv_to_dev(priv);
	struct device_node *node = dev->of_node;
	struct device_node *cpu_port;
	struct device_node *cpu_ep;
	struct device_node *codec_ep;
	struct device_node *codec_port;
	struct device_node *codec_port_old = NULL;
	struct asoc_simple_card_data adata;
	int rc, ret;

	/* loop for all listed CPU port */
	of_for_each_phandle(&it, rc, node, "dais", NULL, 0) {
		cpu_port = it.node;
		cpu_ep	 = NULL;

		/* loop for all CPU endpoint */
		while (1) {
			cpu_ep = of_get_next_child(cpu_port, cpu_ep);
			if (!cpu_ep)
				break;

			/* get codec */
			codec_ep = of_graph_get_remote_endpoint(cpu_ep);
			codec_port = of_get_parent(codec_ep);

			of_node_put(codec_ep);
			of_node_put(codec_port);

			/* get convert-xxx property */
			memset(&adata, 0, sizeof(adata));
			graph_get_conversion(dev, codec_ep, &adata);
			graph_get_conversion(dev, cpu_ep,   &adata);

			/*
			 * It is DPCM
			 * if Codec port has many endpoints,
			 * or has convert-xxx property
			 */
			if ((of_get_child_count(codec_port) > 1) ||
			    adata.convert_rate || adata.convert_channels)
				ret = func_dpcm(priv, cpu_ep, codec_ep, li,
						(codec_port_old == codec_port));
			/* else normal sound */
			else
				ret = func_noml(priv, cpu_ep, codec_ep, li);

			if (ret < 0)
				return ret;

			codec_port_old = codec_port;
		}
	}

	return 0;
}

static int graph_parse_of(struct graph_priv *priv)
{
	struct snd_soc_card *card = graph_priv_to_card(priv);
	struct link_info li;
	int ret;

	ret = asoc_simple_card_of_parse_widgets(card, NULL);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_of_parse_routing(card, NULL);
	if (ret < 0)
		return ret;

	memset(&li, 0, sizeof(li));
	for (li.cpu = 1; li.cpu >= 0; li.cpu--) {
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
		ret = graph_for_each_link(priv, &li,
					  graph_dai_link_of,
					  graph_dai_link_of_dpcm);
		if (ret < 0)
			return ret;
	}

	return asoc_simple_card_parse_card_name(card, NULL);
}

static int graph_count_noml(struct graph_priv *priv,
			    struct device_node *cpu_ep,
			    struct device_node *codec_ep,
			    struct link_info *li)
{
	struct device *dev = graph_priv_to_dev(priv);

	li->link += 1; /* 1xCPU-Codec */
	li->dais += 2; /* 1xCPU + 1xCodec */

	dev_dbg(dev, "Count As Normal\n");

	return 0;
}

static int graph_count_dpcm(struct graph_priv *priv,
			    struct device_node *cpu_ep,
			    struct device_node *codec_ep,
			    struct link_info *li,
			    int dup_codec)
{
	struct device *dev = graph_priv_to_dev(priv);

	li->link++; /* 1xCPU-dummy */
	li->dais++; /* 1xCPU */

	if (!dup_codec) {
		li->link++; /* 1xdummy-Codec */
		li->conf++; /* 1xdummy-Codec */
		li->dais++; /* 1xCodec */
	}

	dev_dbg(dev, "Count As DPCM\n");

	return 0;
}

static void graph_get_dais_count(struct graph_priv *priv,
				 struct link_info *li)
{
	struct device *dev = graph_priv_to_dev(priv);

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
	graph_for_each_link(priv, li,
			    graph_count_noml,
			    graph_count_dpcm);
	dev_dbg(dev, "link %d, dais %d, ccnf %d\n",
		li->link, li->dais, li->conf);
}

static int graph_card_probe(struct snd_soc_card *card)
{
	struct graph_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;

	ret = asoc_simple_card_init_hp(card, &priv->hp_jack, NULL);
	if (ret < 0)
		return ret;

	ret = asoc_simple_card_init_mic(card, &priv->mic_jack, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

static int graph_probe(struct platform_device *pdev)
{
	struct graph_priv *priv;
	struct snd_soc_dai_link *dai_link;
	struct graph_dai_props *dai_props;
	struct asoc_simple_dai *dais;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct snd_soc_codec_conf *cconf;
	struct link_info li;
	int ret, i;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = graph_priv_to_card(priv);
	card->owner		= THIS_MODULE;
	card->dev		= dev;
	card->dapm_widgets	= graph_dapm_widgets;
	card->num_dapm_widgets	= ARRAY_SIZE(graph_dapm_widgets);
	card->probe		= graph_card_probe;

	memset(&li, 0, sizeof(li));
	graph_get_dais_count(priv, &li);
	if (!li.link || !li.dais)
		return -EINVAL;

	dai_props = devm_kcalloc(dev, li.link, sizeof(*dai_props), GFP_KERNEL);
	dai_link  = devm_kcalloc(dev, li.link, sizeof(*dai_link),  GFP_KERNEL);
	dais      = devm_kcalloc(dev, li.dais, sizeof(*dais),      GFP_KERNEL);
	cconf     = devm_kcalloc(dev, li.conf, sizeof(*cconf),     GFP_KERNEL);
	if (!dai_props || !dai_link || !dais)
		return -ENOMEM;

	/*
	 * Use snd_soc_dai_link_component instead of legacy style
	 * It is codec only. but cpu/platform will be supported in the future.
	 * see
	 *	soc-core.c :: snd_soc_init_multicodec()
	 */
	for (i = 0; i < li.link; i++) {
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

	priv->dai_props		= dai_props;
	priv->dai_link		= dai_link;
	priv->dais		= dais;
	priv->codec_conf	= cconf;

	card->dai_link		= dai_link;
	card->num_links		= li.link;
	card->codec_conf	= cconf;
	card->num_configs	= li.conf;

	ret = graph_parse_of(priv);
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

static int graph_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	return asoc_simple_card_clean_reference(card);
}

static const struct of_device_id graph_of_match[] = {
	{ .compatible = "audio-graph-card", },
	{ .compatible = "audio-graph-scu-card", },
	{},
};
MODULE_DEVICE_TABLE(of, graph_of_match);

static struct platform_driver graph_card = {
	.driver = {
		.name = "asoc-audio-graph-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = graph_of_match,
	},
	.probe = graph_probe,
	.remove = graph_remove,
};
module_platform_driver(graph_card);

MODULE_ALIAS("platform:asoc-audio-graph-card");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Audio Graph Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
