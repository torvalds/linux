// SPDX-License-Identifier: (GPL-2.0 OR MIT)
//
// Copyright (c) 2020 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "meson-card.h"

struct gx_dai_link_i2s_data {
	unsigned int mclk_fs;
};

/*
 * Base params for the codec to codec links
 * Those will be over-written by the CPU side of the link
 */
static const struct snd_soc_pcm_stream codec_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 5525,
	.rate_max = 192000,
	.channels_min = 1,
	.channels_max = 8,
};

static int gx_card_i2s_be_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct meson_card *priv = snd_soc_card_get_drvdata(rtd->card);
	struct gx_dai_link_i2s_data *be =
		(struct gx_dai_link_i2s_data *)priv->link_data[rtd->num];

	return meson_card_i2s_set_sysclk(substream, params, be->mclk_fs);
}

static const struct snd_soc_ops gx_card_i2s_be_ops = {
	.hw_params = gx_card_i2s_be_hw_params,
};

static int gx_card_parse_i2s(struct snd_soc_card *card,
			     struct device_node *node,
			     int *index)
{
	struct meson_card *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link *link = &card->dai_link[*index];
	struct gx_dai_link_i2s_data *be;

	/* Allocate i2s link parameters */
	be = devm_kzalloc(card->dev, sizeof(*be), GFP_KERNEL);
	if (!be)
		return -ENOMEM;
	priv->link_data[*index] = be;

	/* Setup i2s link */
	link->ops = &gx_card_i2s_be_ops;
	link->dai_fmt = meson_card_parse_daifmt(node, link->cpus->of_node);

	of_property_read_u32(node, "mclk-fs", &be->mclk_fs);

	return 0;
}
/*
static int gx_card_cpu_is_playback_fe(struct device_node *np)
{
	return of_device_is_compatible(np, DT_PREFIX "aiu-i2s-fifo");
}

static int gx_card_cpu_is_i2s_encoder(struct device_node *np)
{
	return of_device_is_compatible(np, DT_PREFIX "aiu-i2s-encode");
}

static int gx_card_cpu_is_codec(struct device_node *np)
{
	printk ("gx card cpu is codec: %s\n", np->full_name);
	return of_device_is_compatible(np, DT_PREFIX "gx-tohdmitx") ||
		of_device_is_compatible(np, DT_PREFIX "gxbb-toacodec");
}
*/
static int gx_card_cpu_identify(struct snd_soc_dai_link_component *c,
				char *match)
{
	if (of_device_is_compatible(c->of_node, "amlogic,aiu-gxbb")) {
		if (strstr(c->dai_name, match))
			return 1;
	}

	printk("gx_card_cpu_identify: dai_name \"%s\" does not match \"%s\"", c->dai_name, match);
	/* dai not matched */
	return 0;
}

static int gx_card_codec_identify(struct snd_soc_dai_link_component *c,
				char *match)
{
	if (of_device_is_compatible(c->of_node, "cirrus,cs4245")) {
		if (strstr(c->dai_name, match))
			return 1;
	}
	printk("gx_card_codec_identify: dai_name \"%s\" does not match \"%s\"", c->dai_name, match);
	/* dai not matched */
	return 0;
}

static int gx_card_add_link(struct snd_soc_card *card, struct device_node *np,
			    int *index)
{
	struct snd_soc_dai_link *dai_link = &card->dai_link[*index];
	struct snd_soc_dai_link_component *cpu;
	int ret;

	cpu = devm_kzalloc(card->dev, sizeof(*cpu), GFP_KERNEL);
	printk("gx_card_add_link: %pr", cpu);
	if (!cpu)
		return -ENOMEM;

	dai_link->cpus = cpu;
	dai_link->num_cpus = 1;

	ret = meson_card_parse_dai(card, np, &dai_link->cpus->of_node,
				   &dai_link->cpus->dai_name);
	if (ret)
		return ret;

	if (gx_card_cpu_identify(dai_link->cpus, "I2S FIFO"))
		ret = meson_card_set_fe_link(card, dai_link, np, true);
	else
		ret = meson_card_set_be_link(card, dai_link, np);

	if (ret)
		return ret;
/*
	if (gx_card_cpu_is_playback_fe(dai_link->cpu_of_node)) {
		ret = meson_card_set_fe_link(card, dai_link, np, true);
		printk ("gx card cpu is fe (0): %s\n", dai_link->cpu_of_node->name);
	}
	else {
		ret = meson_card_set_be_link(card, dai_link, np);
		printk ("gx card cpu is be (0): %s\n", dai_link->cpu_of_node->name);
	}
	if (ret)
		return ret;

	if (gx_card_cpu_is_i2s_encoder(dai_link->cpu_of_node)) {
		printk ("gx card cpu is i2s encoder (0): %s\n", dai_link->cpu_of_node->name);
		ret = gx_card_parse_i2s(card, np, index);
	}
	else if (gx_card_cpu_is_codec(dai_link->cpu_of_node)) {
		printk ("gx card cpu is codec (0): %s\n", dai_link->cpu_of_node->name);
		dai_link->params = &codec_params;	
	}
*/
	/* Check if the cpu is the i2s encoder and parse i2s data */
	if (gx_card_cpu_identify(dai_link->cpus, "I2S Encoder"))
		ret = gx_card_parse_i2s(card, np, index);

	/* Or apply codec to codec params if necessary */
	else if (gx_card_cpu_identify(dai_link->cpus, "CODEC CTRL"))
		dai_link->params = &codec_params;
	/* Added codec CS4245 23/02/2020 */
/*
	else if (gx_card_codec_identify(dai_link->cpus, "cs4245-dai"))
		dai_link->params = &codec_params;
*/
	return ret;
}

static const struct meson_card_match_data gx_card_match_data = {
	.add_link = gx_card_add_link,
};

static const struct of_device_id gx_card_of_match[] = {
	{
		.compatible = "amlogic,gx-sound-card",
		.data = &gx_card_match_data,
	}, {}
};
MODULE_DEVICE_TABLE(of, gx_card_of_match);

static struct platform_driver gx_card_pdrv = {
	.probe = meson_card_probe,
	.remove = meson_card_remove,
	.driver = {
		.name = "meson-gx-sound-card",
		.of_match_table = gx_card_of_match,
	},
};
module_platform_driver(gx_card_pdrv);

MODULE_DESCRIPTION("Amlogic GX ALSA machine driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
