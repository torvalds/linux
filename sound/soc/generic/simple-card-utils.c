// SPDX-License-Identifier: GPL-2.0
//
// simple-card-utils.c
//
// Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <sound/jack.h>
#include <sound/simple_card_utils.h>

void asoc_simple_convert_fixup(struct asoc_simple_data *data,
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
EXPORT_SYMBOL_GPL(asoc_simple_convert_fixup);

void asoc_simple_parse_convert(struct device *dev,
			       struct device_node *np,
			       char *prefix,
			       struct asoc_simple_data *data)
{
	char prop[128];

	if (!prefix)
		prefix = "";

	/* sampling rate convert */
	snprintf(prop, sizeof(prop), "%s%s", prefix, "convert-rate");
	of_property_read_u32(np, prop, &data->convert_rate);

	/* channels transfer */
	snprintf(prop, sizeof(prop), "%s%s", prefix, "convert-channels");
	of_property_read_u32(np, prop, &data->convert_channels);
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_convert);

int asoc_simple_parse_daifmt(struct device *dev,
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

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_daifmt);

int asoc_simple_set_dailink_name(struct device *dev,
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
	}

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_set_dailink_name);

int asoc_simple_parse_card_name(struct snd_soc_card *card,
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

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_card_name);

static int asoc_simple_clk_enable(struct asoc_simple_dai *dai)
{
	if (dai)
		return clk_prepare_enable(dai->clk);

	return 0;
}

static void asoc_simple_clk_disable(struct asoc_simple_dai *dai)
{
	if (dai)
		clk_disable_unprepare(dai->clk);
}

int asoc_simple_parse_clk(struct device *dev,
			  struct device_node *node,
			  struct device_node *dai_of_node,
			  struct asoc_simple_dai *simple_dai,
			  const char *dai_name,
			  struct snd_soc_dai_link_component *dlc)
{
	struct clk *clk;
	u32 val;

	/*
	 * Use snd_soc_dai_link_component instead of legacy style.
	 * It is only for codec, but cpu will be supported in the future.
	 * see
	 *	soc-core.c :: snd_soc_init_multicodec()
	 */
	if (dlc)
		dai_of_node	= dlc->of_node;

	/*
	 * Parse dai->sysclk come from "clocks = <&xxx>"
	 * (if system has common clock)
	 *  or "system-clock-frequency = <xxx>"
	 *  or device's module clock.
	 */
	clk = devm_get_clk_from_child(dev, node, NULL);
	if (!IS_ERR(clk)) {
		simple_dai->sysclk = clk_get_rate(clk);

		simple_dai->clk = clk;
	} else if (!of_property_read_u32(node, "system-clock-frequency", &val)) {
		simple_dai->sysclk = val;
	} else {
		clk = devm_get_clk_from_child(dev, dai_of_node, NULL);
		if (!IS_ERR(clk))
			simple_dai->sysclk = clk_get_rate(clk);
	}

	if (of_property_read_bool(node, "system-clock-direction-out"))
		simple_dai->clk_direction = SND_SOC_CLOCK_OUT;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_clk);

int asoc_simple_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, rtd->num);
	int ret;

	ret = asoc_simple_clk_enable(dai_props->cpu_dai);
	if (ret)
		return ret;

	ret = asoc_simple_clk_enable(dai_props->codec_dai);
	if (ret)
		asoc_simple_clk_disable(dai_props->cpu_dai);

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_startup);

void asoc_simple_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props =
		simple_priv_to_props(priv, rtd->num);

	asoc_simple_clk_disable(dai_props->cpu_dai);

	asoc_simple_clk_disable(dai_props->codec_dai);
}
EXPORT_SYMBOL_GPL(asoc_simple_shutdown);

static int asoc_simple_set_clk_rate(struct asoc_simple_dai *simple_dai,
				    unsigned long rate)
{
	if (!simple_dai)
		return 0;

	if (!simple_dai->clk)
		return 0;

	if (clk_get_rate(simple_dai->clk) == rate)
		return 0;

	return clk_set_rate(simple_dai->clk, rate);
}

int asoc_simple_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props =
		simple_priv_to_props(priv, rtd->num);
	unsigned int mclk, mclk_fs = 0;
	int ret = 0;

	if (dai_props->mclk_fs)
		mclk_fs = dai_props->mclk_fs;

	if (mclk_fs) {
		mclk = params_rate(params) * mclk_fs;

		ret = asoc_simple_set_clk_rate(dai_props->codec_dai, mclk);
		if (ret < 0)
			return ret;

		ret = asoc_simple_set_clk_rate(dai_props->cpu_dai, mclk);
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
EXPORT_SYMBOL_GPL(asoc_simple_hw_params);

int asoc_simple_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				   struct snd_pcm_hw_params *params)
{
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, rtd->num);

	asoc_simple_convert_fixup(&dai_props->adata, params);

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_be_hw_params_fixup);

static int asoc_simple_init_dai(struct snd_soc_dai *dai,
				     struct asoc_simple_dai *simple_dai)
{
	int ret;

	if (!simple_dai)
		return 0;

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

int asoc_simple_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, rtd->num);
	int ret;

	ret = asoc_simple_init_dai(rtd->codec_dai,
				   dai_props->codec_dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_init_dai(rtd->cpu_dai,
				   dai_props->cpu_dai);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_dai_init);

void asoc_simple_canonicalize_platform(struct snd_soc_dai_link *dai_link)
{
	/* Assumes platform == cpu */
	if (!dai_link->platforms->of_node)
		dai_link->platforms->of_node = dai_link->cpu_of_node;
}
EXPORT_SYMBOL_GPL(asoc_simple_canonicalize_platform);

void asoc_simple_canonicalize_cpu(struct snd_soc_dai_link *dai_link,
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
EXPORT_SYMBOL_GPL(asoc_simple_canonicalize_cpu);

int asoc_simple_clean_reference(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	int i;

	for_each_card_prelinks(card, i, dai_link) {
		of_node_put(dai_link->cpu_of_node);
		of_node_put(dai_link->codecs->of_node);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_clean_reference);

int asoc_simple_parse_routing(struct snd_soc_card *card,
			      char *prefix)
{
	struct device_node *node = card->dev->of_node;
	char prop[128];

	if (!prefix)
		prefix = "";

	snprintf(prop, sizeof(prop), "%s%s", prefix, "routing");

	if (!of_property_read_bool(node, prop))
		return 0;

	return snd_soc_of_parse_audio_routing(card, prop);
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_routing);

int asoc_simple_parse_widgets(struct snd_soc_card *card,
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
EXPORT_SYMBOL_GPL(asoc_simple_parse_widgets);

int asoc_simple_parse_pin_switches(struct snd_soc_card *card,
				   char *prefix)
{
	const unsigned int nb_controls_max = 16;
	const char **strings, *control_name;
	struct snd_kcontrol_new *controls;
	struct device *dev = card->dev;
	unsigned int i, nb_controls;
	char prop[128];
	int ret;

	if (!prefix)
		prefix = "";

	snprintf(prop, sizeof(prop), "%s%s", prefix, "pin-switches");

	if (!of_property_read_bool(dev->of_node, prop))
		return 0;

	strings = devm_kcalloc(dev, nb_controls_max,
			       sizeof(*strings), GFP_KERNEL);
	if (!strings)
		return -ENOMEM;

	ret = of_property_read_string_array(dev->of_node, prop,
					    strings, nb_controls_max);
	if (ret < 0)
		return ret;

	nb_controls = (unsigned int)ret;

	controls = devm_kcalloc(dev, nb_controls,
				sizeof(*controls), GFP_KERNEL);
	if (!controls)
		return -ENOMEM;

	for (i = 0; i < nb_controls; i++) {
		control_name = devm_kasprintf(dev, GFP_KERNEL,
					      "%s Switch", strings[i]);
		if (!control_name)
			return -ENOMEM;

		controls[i].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		controls[i].name = control_name;
		controls[i].info = snd_soc_dapm_info_pin_switch;
		controls[i].get = snd_soc_dapm_get_pin_switch;
		controls[i].put = snd_soc_dapm_put_pin_switch;
		controls[i].private_value = (unsigned long)strings[i];
	}

	card->controls = controls;
	card->num_controls = nb_controls;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_pin_switches);

int asoc_simple_init_jack(struct snd_soc_card *card,
			  struct asoc_simple_jack *sjack,
			  int is_hp, char *prefix)
{
	struct device *dev = card->dev;
	enum of_gpio_flags flags;
	char prop[128];
	char *pin_name;
	char *gpio_name;
	int mask;
	int det;

	if (!prefix)
		prefix = "";

	sjack->gpio.gpio = -ENOENT;

	if (is_hp) {
		snprintf(prop, sizeof(prop), "%shp-det-gpio", prefix);
		pin_name	= "Headphones";
		gpio_name	= "Headphone detection";
		mask		= SND_JACK_HEADPHONE;
	} else {
		snprintf(prop, sizeof(prop), "%smic-det-gpio", prefix);
		pin_name	= "Mic Jack";
		gpio_name	= "Mic detection";
		mask		= SND_JACK_MICROPHONE;
	}

	det = of_get_named_gpio_flags(dev->of_node, prop, 0, &flags);
	if (det == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (gpio_is_valid(det)) {
		sjack->pin.pin		= pin_name;
		sjack->pin.mask		= mask;

		sjack->gpio.name	= gpio_name;
		sjack->gpio.report	= mask;
		sjack->gpio.gpio	= det;
		sjack->gpio.invert	= !!(flags & OF_GPIO_ACTIVE_LOW);
		sjack->gpio.debounce_time = 150;

		snd_soc_card_jack_new(card, pin_name, mask,
				      &sjack->jack,
				      &sjack->pin, 1);

		snd_soc_jack_add_gpios(&sjack->jack, 1,
				       &sjack->gpio);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_init_jack);

int asoc_simple_init_priv(struct asoc_simple_priv *priv,
			  struct link_info *li)
{
	struct snd_soc_card *card = simple_priv_to_card(priv);
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link;
	struct simple_dai_props *dai_props;
	struct asoc_simple_dai *dais;
	struct snd_soc_codec_conf *cconf = NULL;
	int i;

	dai_props = devm_kcalloc(dev, li->link, sizeof(*dai_props), GFP_KERNEL);
	dai_link  = devm_kcalloc(dev, li->link, sizeof(*dai_link),  GFP_KERNEL);
	dais      = devm_kcalloc(dev, li->dais, sizeof(*dais),      GFP_KERNEL);
	if (!dai_props || !dai_link || !dais)
		return -ENOMEM;

	if (li->conf) {
		cconf = devm_kcalloc(dev, li->conf, sizeof(*cconf), GFP_KERNEL);
		if (!cconf)
			return -ENOMEM;
	}

	/*
	 * Use snd_soc_dai_link_component instead of legacy style
	 * It is codec only. but cpu/platform will be supported in the future.
	 * see
	 *	soc-core.c :: snd_soc_init_multicodec()
	 *
	 * "platform" might be removed
	 * see
	 *	simple-card-utils.c :: asoc_simple_canonicalize_platform()
	 */
	for (i = 0; i < li->link; i++) {
		dai_link[i].codecs		= &dai_props[i].codecs;
		dai_link[i].num_codecs		= 1;
		dai_link[i].platforms		= &dai_props[i].platforms;
		dai_link[i].num_platforms	= 1;
	}

	priv->dai_props		= dai_props;
	priv->dai_link		= dai_link;
	priv->dais		= dais;
	priv->codec_conf	= cconf;

	card->dai_link		= priv->dai_link;
	card->num_links		= li->link;
	card->codec_conf	= cconf;
	card->num_configs	= li->conf;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_init_priv);

/* Module information */
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_DESCRIPTION("ALSA SoC Simple Card Utils");
MODULE_LICENSE("GPL v2");
