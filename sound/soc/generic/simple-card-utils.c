/*
 * simple-card-utils.c
 *
 * Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <sound/simple_card_utils.h>

void asoc_simple_card_convert_fixup(struct asoc_simple_card_data *data,
				    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	if (data->convert_rate)
		rate->min =
		rate->max = data->convert_rate;

	if (data->convert_channels)
		channels->min =
		channels->max = data->convert_channels;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_convert_fixup);

void asoc_simple_card_parse_convert(struct device *dev, char *prefix,
				    struct asoc_simple_card_data *data)
{
	struct device_node *np = dev->of_node;
	char prop[128];

	if (!prefix)
		prefix = "";

	/* sampling rate convert */
	snprintf(prop, sizeof(prop), "%s%s", prefix, "convert-rate");
	of_property_read_u32(np, prop, &data->convert_rate);

	/* channels transfer */
	snprintf(prop, sizeof(prop), "%s%s", prefix, "convert-channels");
	of_property_read_u32(np, prop, &data->convert_channels);

	dev_dbg(dev, "convert_rate     %d\n", data->convert_rate);
	dev_dbg(dev, "convert_channels %d\n", data->convert_channels);
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_convert);

int asoc_simple_card_parse_daifmt(struct device *dev,
				  struct device_node *node,
				  struct device_node *codec,
				  char *prefix,
				  unsigned int *retfmt)
{
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int daifmt;

	daifmt = snd_soc_of_parse_daifmt(node, prefix,
					 &bitclkmaster, &framemaster);
	daifmt &= ~SND_SOC_DAIFMT_MASTER_MASK;

	if (!bitclkmaster && !framemaster) {
		/*
		 * No dai-link level and master setting was not found from
		 * sound node level, revert back to legacy DT parsing and
		 * take the settings from codec node.
		 */
		dev_dbg(dev, "Revert to legacy daifmt parsing\n");

		daifmt = snd_soc_of_parse_daifmt(codec, NULL, NULL, NULL) |
			(daifmt & ~SND_SOC_DAIFMT_CLOCK_MASK);
	} else {
		if (codec == bitclkmaster)
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBM_CFM : SND_SOC_DAIFMT_CBM_CFS;
		else
			daifmt |= (codec == framemaster) ?
				SND_SOC_DAIFMT_CBS_CFM : SND_SOC_DAIFMT_CBS_CFS;
	}

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	*retfmt = daifmt;

	dev_dbg(dev, "format : %04x\n", daifmt);

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_daifmt);

int asoc_simple_card_set_dailink_name(struct device *dev,
				      struct snd_soc_dai_link *dai_link,
				      const char *fmt, ...)
{
	va_list ap;
	char *name = NULL;
	int ret = -ENOMEM;

	va_start(ap, fmt);
	name = devm_kvasprintf(dev, GFP_KERNEL, fmt, ap);
	va_end(ap);

	if (name) {
		ret = 0;

		dai_link->name		= name;
		dai_link->stream_name	= name;

		dev_dbg(dev, "name : %s\n", name);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_set_dailink_name);

int asoc_simple_card_parse_card_name(struct snd_soc_card *card,
				     char *prefix)
{
	int ret;

	if (!prefix)
		prefix = "";

	/* Parse the card name from DT */
	ret = snd_soc_of_parse_card_name(card, "label");
	if (ret < 0 || !card->name) {
		char prop[128];

		snprintf(prop, sizeof(prop), "%sname", prefix);
		ret = snd_soc_of_parse_card_name(card, prop);
		if (ret < 0)
			return ret;
	}

	if (!card->name && card->dai_link)
		card->name = card->dai_link->name;

	dev_dbg(card->dev, "Card Name: %s\n", card->name ? card->name : "");

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_card_name);

static void asoc_simple_card_clk_register(struct asoc_simple_dai *dai,
					  struct clk *clk)
{
	dai->clk = clk;
}

int asoc_simple_card_clk_enable(struct asoc_simple_dai *dai)
{
	return clk_prepare_enable(dai->clk);
}
EXPORT_SYMBOL_GPL(asoc_simple_card_clk_enable);

void asoc_simple_card_clk_disable(struct asoc_simple_dai *dai)
{
	clk_disable_unprepare(dai->clk);
}
EXPORT_SYMBOL_GPL(asoc_simple_card_clk_disable);

int asoc_simple_card_parse_clk(struct device *dev,
			       struct device_node *node,
			       struct device_node *dai_of_node,
			       struct asoc_simple_dai *simple_dai,
			       const char *name)
{
	struct clk *clk;
	u32 val;

	/*
	 * Parse dai->sysclk come from "clocks = <&xxx>"
	 * (if system has common clock)
	 *  or "system-clock-frequency = <xxx>"
	 *  or device's module clock.
	 */
	clk = devm_get_clk_from_child(dev, node, NULL);
	if (!IS_ERR(clk)) {
		simple_dai->sysclk = clk_get_rate(clk);

		asoc_simple_card_clk_register(simple_dai, clk);
	} else if (!of_property_read_u32(node, "system-clock-frequency", &val)) {
		simple_dai->sysclk = val;
	} else {
		clk = devm_get_clk_from_child(dev, dai_of_node, NULL);
		if (!IS_ERR(clk))
			simple_dai->sysclk = clk_get_rate(clk);
	}

	if (of_property_read_bool(node, "system-clock-direction-out"))
		simple_dai->clk_direction = SND_SOC_CLOCK_OUT;

	dev_dbg(dev, "%s : sysclk = %d, direction %d\n", name,
		simple_dai->sysclk, simple_dai->clk_direction);

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_clk);

int asoc_simple_card_parse_dai(struct device_node *node,
				    struct device_node **dai_of_node,
				    const char **dai_name,
				    const char *list_name,
				    const char *cells_name,
				    int *is_single_link)
{
	struct of_phandle_args args;
	int ret;

	if (!node)
		return 0;

	/*
	 * Get node via "sound-dai = <&phandle port>"
	 * it will be used as xxx_of_node on soc_bind_dai_link()
	 */
	ret = of_parse_phandle_with_args(node, list_name, cells_name, 0, &args);
	if (ret)
		return ret;

	/* Get dai->name */
	if (dai_name) {
		ret = snd_soc_of_get_dai_name(node, dai_name);
		if (ret < 0)
			return ret;
	}

	*dai_of_node = args.np;

	if (is_single_link)
		*is_single_link = !args.args_count;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_dai);

static int asoc_simple_card_get_dai_id(struct device_node *ep)
{
	struct device_node *node;
	struct device_node *endpoint;
	int i, id;
	int ret;

	ret = snd_soc_get_dai_id(ep);
	if (ret != -ENOTSUPP)
		return ret;

	node = of_graph_get_port_parent(ep);

	/*
	 * Non HDMI sound case, counting port/endpoint on its DT
	 * is enough. Let's count it.
	 */
	i = 0;
	id = -1;
	for_each_endpoint_of_node(node, endpoint) {
		if (endpoint == ep)
			id = i;
		i++;
	}

	of_node_put(node);

	if (id < 0)
		return -ENODEV;

	return id;
}

int asoc_simple_card_parse_graph_dai(struct device_node *ep,
				     struct device_node **dai_of_node,
				     const char **dai_name)
{
	struct device_node *node;
	struct of_phandle_args args;
	int ret;

	if (!ep)
		return 0;
	if (!dai_name)
		return 0;

	node = of_graph_get_port_parent(ep);

	/* Get dai->name */
	args.np		= node;
	args.args[0]	= asoc_simple_card_get_dai_id(ep);
	args.args_count	= (of_graph_get_endpoint_count(node) > 1);

	ret = snd_soc_get_dai_name(&args, dai_name);
	if (ret < 0)
		return ret;

	*dai_of_node = node;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_parse_graph_dai);

int asoc_simple_card_init_dai(struct snd_soc_dai *dai,
			      struct asoc_simple_dai *simple_dai)
{
	int ret;

	if (simple_dai->sysclk) {
		ret = snd_soc_dai_set_sysclk(dai, 0, simple_dai->sysclk,
					     simple_dai->clk_direction);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "simple-card: set_sysclk error\n");
			return ret;
		}
	}

	if (simple_dai->slots) {
		ret = snd_soc_dai_set_tdm_slot(dai,
					       simple_dai->tx_slot_mask,
					       simple_dai->rx_slot_mask,
					       simple_dai->slots,
					       simple_dai->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "simple-card: set_tdm_slot error\n");
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_init_dai);

int asoc_simple_card_canonicalize_dailink(struct snd_soc_dai_link *dai_link)
{
	/* Assumes platform == cpu */
	if (!dai_link->platform_of_node)
		dai_link->platform_of_node = dai_link->cpu_of_node;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_canonicalize_dailink);

void asoc_simple_card_canonicalize_cpu(struct snd_soc_dai_link *dai_link,
				       int is_single_links)
{
	/*
	 * In soc_bind_dai_link() will check cpu name after
	 * of_node matching if dai_link has cpu_dai_name.
	 * but, it will never match if name was created by
	 * fmt_single_name() remove cpu_dai_name if cpu_args
	 * was 0. See:
	 *	fmt_single_name()
	 *	fmt_multiple_name()
	 */
	if (is_single_links)
		dai_link->cpu_dai_name = NULL;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_canonicalize_cpu);

int asoc_simple_card_clean_reference(struct snd_soc_card *card)
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
EXPORT_SYMBOL_GPL(asoc_simple_card_clean_reference);

int asoc_simple_card_of_parse_routing(struct snd_soc_card *card,
				      char *prefix,
				      int optional)
{
	struct device_node *node = card->dev->of_node;
	char prop[128];

	if (!prefix)
		prefix = "";

	snprintf(prop, sizeof(prop), "%s%s", prefix, "routing");

	if (!of_property_read_bool(node, prop)) {
		if (optional)
			return 0;
		return -EINVAL;
	}

	return snd_soc_of_parse_audio_routing(card, prop);
}
EXPORT_SYMBOL_GPL(asoc_simple_card_of_parse_routing);

int asoc_simple_card_of_parse_widgets(struct snd_soc_card *card,
				      char *prefix)
{
	struct device_node *node = card->dev->of_node;
	char prop[128];

	if (!prefix)
		prefix = "";

	snprintf(prop, sizeof(prop), "%s%s", prefix, "widgets");

	if (of_property_read_bool(node, prop))
		return snd_soc_of_parse_audio_simple_widgets(card, prop);

	/* no widgets is not error */
	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_card_of_parse_widgets);

/* Module information */
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_DESCRIPTION("ALSA SoC Simple Card Utils");
MODULE_LICENSE("GPL v2");
