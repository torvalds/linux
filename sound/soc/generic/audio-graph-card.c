// SPDX-License-Identifier: GPL-2.0
//
// ASoC audio graph sound card support
//
// Copyright (C) 2016 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
// based on ${LINUX}/sound/soc/generic/simple-card.c

#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/graph_card.h>

#define DPCM_SELECTABLE 1

#define ep_to_port(ep)	of_get_parent(ep)
static struct device_node *port_to_ports(struct device_node *port)
{
	struct device_node *ports = of_get_parent(port);

	if (!of_node_name_eq(ports, "ports")) {
		of_node_put(ports);
		return NULL;
	}
	return ports;
}

static int graph_outdrv_event(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol,
			      int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct simple_util_priv *priv = snd_soc_card_get_drvdata(dapm->card);

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

static const struct snd_soc_ops graph_ops = {
	.startup	= simple_util_startup,
	.shutdown	= simple_util_shutdown,
	.hw_params	= simple_util_hw_params,
};

static bool soc_component_is_pcm(struct snd_soc_dai_link_component *dlc)
{
	struct snd_soc_dai *dai = snd_soc_find_dai_with_mutex(dlc);

	if (dai && (dai->component->driver->pcm_construct ||
		    (dai->driver->ops && dai->driver->ops->pcm_new)))
		return true;

	return false;
}

static void graph_parse_convert(struct device *dev,
				struct device_node *ep,
				struct simple_util_data *adata)
{
	struct device_node *top = dev->of_node;
	struct device_node *port = ep_to_port(ep);
	struct device_node *ports = port_to_ports(port);
	struct device_node *node = of_graph_get_port_parent(ep);

	simple_util_parse_convert(top,   NULL,   adata);
	simple_util_parse_convert(ports, NULL,   adata);
	simple_util_parse_convert(port,  NULL,   adata);
	simple_util_parse_convert(ep,    NULL,   adata);

	of_node_put(port);
	of_node_put(ports);
	of_node_put(node);
}

static int graph_parse_node(struct simple_util_priv *priv,
			    struct device_node *ep,
			    struct link_info *li,
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

	ret = graph_util_parse_dai(dev, ep, dlc, cpu);
	if (ret < 0)
		return ret;

	ret = simple_util_parse_tdm(ep, dai);
	if (ret < 0)
		return ret;

	ret = simple_util_parse_clk(dev, ep, dai, dlc);
	if (ret < 0)
		return ret;

	return 0;
}

static int graph_link_init(struct simple_util_priv *priv,
			   struct device_node *ep_cpu,
			   struct device_node *ep_codec,
			   struct link_info *li,
			   char *name)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *top = dev->of_node;
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	struct device_node *port_cpu = ep_to_port(ep_cpu);
	struct device_node *port_codec = ep_to_port(ep_codec);
	struct device_node *ports_cpu = port_to_ports(port_cpu);
	struct device_node *ports_codec = port_to_ports(port_codec);
	enum snd_soc_trigger_order trigger_start = SND_SOC_TRIGGER_ORDER_DEFAULT;
	enum snd_soc_trigger_order trigger_stop  = SND_SOC_TRIGGER_ORDER_DEFAULT;
	bool playback_only = 0, capture_only = 0;
	int ret;

	ret = simple_util_parse_daifmt(dev, ep_cpu, ep_codec,
				       NULL, &dai_link->dai_fmt);
	if (ret < 0)
		goto init_end;

	graph_util_parse_link_direction(top,		&playback_only, &capture_only);
	graph_util_parse_link_direction(port_cpu,	&playback_only, &capture_only);
	graph_util_parse_link_direction(port_codec,	&playback_only, &capture_only);
	graph_util_parse_link_direction(ep_cpu,		&playback_only, &capture_only);
	graph_util_parse_link_direction(ep_codec,	&playback_only, &capture_only);

	of_property_read_u32(top,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ports_cpu,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ports_codec,	"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(port_cpu,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(port_codec,	"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ep_cpu,		"mclk-fs", &dai_props->mclk_fs);
	of_property_read_u32(ep_codec,		"mclk-fs", &dai_props->mclk_fs);

	graph_util_parse_trigger_order(priv, top,		&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, ports_cpu,		&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, ports_codec,	&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, port_cpu,		&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, port_cpu,		&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, ep_cpu,		&trigger_start, &trigger_stop);
	graph_util_parse_trigger_order(priv, ep_codec,		&trigger_start, &trigger_stop);

	dai_link->playback_only	= playback_only;
	dai_link->capture_only	= capture_only;

	dai_link->trigger_start	= trigger_start;
	dai_link->trigger_stop	= trigger_stop;

	dai_link->init		= simple_util_dai_init;
	dai_link->ops		= &graph_ops;
	if (priv->ops)
		dai_link->ops	= priv->ops;

	ret = simple_util_set_dailink_name(dev, dai_link, name);
init_end:
	of_node_put(ports_cpu);
	of_node_put(ports_codec);
	of_node_put(port_cpu);
	of_node_put(port_codec);

	return ret;
}

static int graph_dai_link_of_dpcm(struct simple_util_priv *priv,
				  struct device_node *cpu_ep,
				  struct device_node *codec_ep,
				  struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	struct device_node *top = dev->of_node;
	struct device_node *ep = li->cpu ? cpu_ep : codec_ep;
	char dai_name[64];
	int ret;

	dev_dbg(dev, "link_of DPCM (%pOF)\n", ep);

	if (li->cpu) {
		struct snd_soc_card *card = simple_priv_to_card(priv);
		struct snd_soc_dai_link_component *cpus = snd_soc_link_to_cpu(dai_link, 0);
		struct snd_soc_dai_link_component *platforms = snd_soc_link_to_platform(dai_link, 0);
		int is_single_links = 0;

		/* Codec is dummy */

		/* FE settings */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		ret = graph_parse_node(priv, cpu_ep, li, &is_single_links);
		if (ret)
			return ret;

		snprintf(dai_name, sizeof(dai_name),
			 "fe.%pOFP.%s", cpus->of_node, cpus->dai_name);
		/*
		 * In BE<->BE connections it is not required to create
		 * PCM devices at CPU end of the dai link and thus 'no_pcm'
		 * flag needs to be set. It is useful when there are many
		 * BE components and some of these have to be connected to
		 * form a valid audio path.
		 *
		 * For example: FE <-> BE1 <-> BE2 <-> ... <-> BEn where
		 * there are 'n' BE components in the path.
		 */
		if (card->component_chaining && !soc_component_is_pcm(cpus)) {
			dai_link->no_pcm = 1;
			dai_link->be_hw_params_fixup = simple_util_be_hw_params_fixup;
		}

		simple_util_canonicalize_cpu(cpus, is_single_links);
		simple_util_canonicalize_platform(platforms, cpus);
	} else {
		struct snd_soc_codec_conf *cconf = simple_props_to_codec_conf(dai_props, 0);
		struct snd_soc_dai_link_component *codecs = snd_soc_link_to_codec(dai_link, 0);
		struct device_node *port;
		struct device_node *ports;

		/* CPU is dummy */

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= simple_util_be_hw_params_fixup;

		ret = graph_parse_node(priv, codec_ep, li, NULL);
		if (ret < 0)
			return ret;

		snprintf(dai_name, sizeof(dai_name),
			 "be.%pOFP.%s", codecs->of_node, codecs->dai_name);

		/* check "prefix" from top node */
		port  = ep_to_port(ep);
		ports = port_to_ports(port);
		snd_soc_of_parse_node_prefix(top,   cconf, codecs->of_node, "prefix");
		snd_soc_of_parse_node_prefix(ports, cconf, codecs->of_node, "prefix");
		snd_soc_of_parse_node_prefix(port,  cconf, codecs->of_node, "prefix");

		of_node_put(ports);
		of_node_put(port);
	}

	graph_parse_convert(dev, ep, &dai_props->adata);

	snd_soc_dai_link_set_capabilities(dai_link);

	ret = graph_link_init(priv, cpu_ep, codec_ep, li, dai_name);

	li->link++;

	return ret;
}

static int graph_dai_link_of(struct simple_util_priv *priv,
			     struct device_node *cpu_ep,
			     struct device_node *codec_ep,
			     struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct snd_soc_dai_link_component *cpus = snd_soc_link_to_cpu(dai_link, 0);
	struct snd_soc_dai_link_component *codecs = snd_soc_link_to_codec(dai_link, 0);
	struct snd_soc_dai_link_component *platforms = snd_soc_link_to_platform(dai_link, 0);
	char dai_name[64];
	int ret, is_single_links = 0;

	dev_dbg(dev, "link_of (%pOF)\n", cpu_ep);

	ret = graph_parse_node(priv, cpu_ep, li, &is_single_links);
	if (ret < 0)
		return ret;

	ret = graph_parse_node(priv, codec_ep, li, NULL);
	if (ret < 0)
		return ret;

	snprintf(dai_name, sizeof(dai_name),
		 "%s-%s", cpus->dai_name, codecs->dai_name);

	simple_util_canonicalize_cpu(cpus, is_single_links);
	simple_util_canonicalize_platform(platforms, cpus);

	ret = graph_link_init(priv, cpu_ep, codec_ep, li, dai_name);
	if (ret < 0)
		return ret;

	li->link++;

	return 0;
}

static inline bool parse_as_dpcm_link(struct simple_util_priv *priv,
				      struct device_node *codec_port,
				      struct simple_util_data *adata)
{
	if (priv->force_dpcm)
		return true;

	if (!priv->dpcm_selectable)
		return false;

	/*
	 * It is DPCM
	 * if Codec port has many endpoints,
	 * or has convert-xxx property
	 */
	if ((of_get_child_count(codec_port) > 1) ||
	    simple_util_is_convert_required(adata))
		return true;

	return false;
}

static int __graph_for_each_link(struct simple_util_priv *priv,
			struct link_info *li,
			int (*func_noml)(struct simple_util_priv *priv,
					 struct device_node *cpu_ep,
					 struct device_node *codec_ep,
					 struct link_info *li),
			int (*func_dpcm)(struct simple_util_priv *priv,
					 struct device_node *cpu_ep,
					 struct device_node *codec_ep,
					 struct link_info *li))
{
	struct of_phandle_iterator it;
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *node = dev->of_node;
	struct device_node *cpu_port;
	struct device_node *cpu_ep;
	struct device_node *codec_ep;
	struct device_node *codec_port;
	struct device_node *codec_port_old = NULL;
	struct simple_util_data adata;
	int rc, ret = 0;

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
			codec_port = ep_to_port(codec_ep);

			/* get convert-xxx property */
			memset(&adata, 0, sizeof(adata));
			graph_parse_convert(dev, codec_ep, &adata);
			graph_parse_convert(dev, cpu_ep,   &adata);

			/* check if link requires DPCM parsing */
			if (parse_as_dpcm_link(priv, codec_port, &adata)) {
				/*
				 * Codec endpoint can be NULL for pluggable audio HW.
				 * Platform DT can populate the Codec endpoint depending on the
				 * plugged HW.
				 */
				/* Do it all CPU endpoint, and 1st Codec endpoint */
				if (li->cpu ||
				    ((codec_port_old != codec_port) && codec_ep))
					ret = func_dpcm(priv, cpu_ep, codec_ep, li);
			/* else normal sound */
			} else {
				if (li->cpu)
					ret = func_noml(priv, cpu_ep, codec_ep, li);
			}

			of_node_put(codec_ep);
			of_node_put(codec_port);

			if (ret < 0) {
				of_node_put(cpu_ep);
				return ret;
			}

			codec_port_old = codec_port;
		}
	}

	return 0;
}

static int graph_for_each_link(struct simple_util_priv *priv,
			       struct link_info *li,
			       int (*func_noml)(struct simple_util_priv *priv,
						struct device_node *cpu_ep,
						struct device_node *codec_ep,
						struct link_info *li),
			       int (*func_dpcm)(struct simple_util_priv *priv,
						struct device_node *cpu_ep,
						struct device_node *codec_ep,
						struct link_info *li))
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
		ret = __graph_for_each_link(priv, li, func_noml, func_dpcm);
		if (ret < 0)
			break;
	}

	return ret;
}

static int graph_count_noml(struct simple_util_priv *priv,
			    struct device_node *cpu_ep,
			    struct device_node *codec_ep,
			    struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	if (li->link >= SNDRV_MAX_LINKS) {
		dev_err(dev, "too many links\n");
		return -EINVAL;
	}

	/*
	 * DON'T REMOVE platforms
	 * see
	 *	simple-card.c :: simple_count_noml()
	 */
	li->num[li->link].cpus		= 1;
	li->num[li->link].platforms     = 1;

	li->num[li->link].codecs	= 1;

	li->link += 1; /* 1xCPU-Codec */

	dev_dbg(dev, "Count As Normal\n");

	return 0;
}

static int graph_count_dpcm(struct simple_util_priv *priv,
			    struct device_node *cpu_ep,
			    struct device_node *codec_ep,
			    struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	if (li->link >= SNDRV_MAX_LINKS) {
		dev_err(dev, "too many links\n");
		return -EINVAL;
	}

	if (li->cpu) {
		/*
		 * DON'T REMOVE platforms
		 * see
		 *	simple-card.c :: simple_count_noml()
		 */
		li->num[li->link].cpus		= 1;
		li->num[li->link].platforms     = 1;

		li->link++; /* 1xCPU-dummy */
	} else {
		li->num[li->link].codecs	= 1;

		li->link++; /* 1xdummy-Codec */
	}

	dev_dbg(dev, "Count As DPCM\n");

	return 0;
}

static int graph_get_dais_count(struct simple_util_priv *priv,
				struct link_info *li)
{
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
	return graph_for_each_link(priv, li,
				   graph_count_noml,
				   graph_count_dpcm);
}

int audio_graph_parse_of(struct simple_util_priv *priv, struct device *dev)
{
	struct snd_soc_card *card = simple_priv_to_card(priv);
	int ret;

	struct link_info *li __free(kfree) = kzalloc(sizeof(*li), GFP_KERNEL);
	if (!li)
		return -ENOMEM;

	card->owner = THIS_MODULE;
	card->dev = dev;

	ret = graph_get_dais_count(priv, li);
	if (ret < 0)
		return ret;

	if (!li->link)
		return -EINVAL;

	ret = simple_util_init_priv(priv, li);
	if (ret < 0)
		return ret;

	priv->pa_gpio = devm_gpiod_get_optional(dev, "pa", GPIOD_OUT_LOW);
	if (IS_ERR(priv->pa_gpio)) {
		ret = PTR_ERR(priv->pa_gpio);
		dev_err(dev, "failed to get amplifier gpio: %d\n", ret);
		return ret;
	}

	ret = simple_util_parse_widgets(card, NULL);
	if (ret < 0)
		return ret;

	ret = simple_util_parse_routing(card, NULL);
	if (ret < 0)
		return ret;

	memset(li, 0, sizeof(*li));
	ret = graph_for_each_link(priv, li,
				  graph_dai_link_of,
				  graph_dai_link_of_dpcm);
	if (ret < 0)
		goto err;

	ret = simple_util_parse_card_name(card, NULL);
	if (ret < 0)
		goto err;

	snd_soc_card_set_drvdata(card, priv);

	simple_util_debug_info(priv);

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0)
		goto err;

	return 0;

err:
	simple_util_clean_reference(card);

	return dev_err_probe(dev, ret, "parse error\n");
}
EXPORT_SYMBOL_GPL(audio_graph_parse_of);

static int graph_probe(struct platform_device *pdev)
{
	struct simple_util_priv *priv;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = simple_priv_to_card(priv);
	card->dapm_widgets	= graph_dapm_widgets;
	card->num_dapm_widgets	= ARRAY_SIZE(graph_dapm_widgets);
	card->probe		= graph_util_card_probe;

	if (of_device_get_match_data(dev))
		priv->dpcm_selectable = 1;

	return audio_graph_parse_of(priv, dev);
}

static const struct of_device_id graph_of_match[] = {
	{ .compatible = "audio-graph-card", },
	{ .compatible = "audio-graph-scu-card",
	  .data = (void *)DPCM_SELECTABLE },
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
	.remove_new = simple_util_remove,
};
module_platform_driver(graph_card);

MODULE_ALIAS("platform:asoc-audio-graph-card");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Audio Graph Sound Card");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
