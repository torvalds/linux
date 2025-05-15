// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2017 Samsung Electronics Co., Ltd.

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "i2s.h"
#include "i2s-regs.h"

struct odroid_priv {
	struct snd_soc_card card;
	struct clk *clk_i2s_bus;
	struct clk *sclk_i2s;

	/* Spinlock protecting fields below */
	spinlock_t lock;
	unsigned int be_sample_rate;
	bool be_active;
};

static int odroid_card_fe_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_pcm_hw_constraint_single(runtime, SNDRV_PCM_HW_PARAM_CHANNELS, 2);

	return 0;
}

static int odroid_card_fe_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct odroid_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->be_active && priv->be_sample_rate != params_rate(params))
		ret = -EINVAL;
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static const struct snd_soc_ops odroid_card_fe_ops = {
	.startup = odroid_card_fe_startup,
	.hw_params = odroid_card_fe_hw_params,
};

static int odroid_card_be_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct odroid_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	unsigned int pll_freq, rclk_freq, rfs;
	unsigned long flags;
	int ret;

	switch (params_rate(params)) {
	case 64000:
		pll_freq = 196608001U;
		rfs = 384;
		break;
	case 44100:
	case 88200:
		pll_freq = 180633609U;
		rfs = 512;
		break;
	case 32000:
	case 48000:
	case 96000:
		pll_freq = 196608001U;
		rfs = 512;
		break;
	default:
		return -EINVAL;
	}

	ret = clk_set_rate(priv->clk_i2s_bus, pll_freq / 2 + 1);
	if (ret < 0)
		return ret;

	/*
	 *  We add 2 to the rclk_freq value in order to avoid too low clock
	 *  frequency values due to the EPLL output frequency not being exact
	 *  multiple of the audio sampling rate.
	 */
	rclk_freq = params_rate(params) * rfs + 2;

	ret = clk_set_rate(priv->sclk_i2s, rclk_freq);
	if (ret < 0)
		return ret;

	if (rtd->dai_link->num_codecs > 1) {
		struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 1);

		ret = snd_soc_dai_set_sysclk(codec_dai, 0, rclk_freq,
					     SND_SOC_CLOCK_IN);
		if (ret < 0)
			return ret;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->be_sample_rate = params_rate(params);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int odroid_card_be_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct odroid_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		priv->be_active = true;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		priv->be_active = false;
		break;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static const struct snd_soc_ops odroid_card_be_ops = {
	.hw_params = odroid_card_be_hw_params,
	.trigger = odroid_card_be_trigger,
};

/* DAPM routes for backward compatibility with old DTS */
static const struct snd_soc_dapm_route odroid_dapm_routes[] = {
	{ "I2S Playback", NULL, "Mixer DAI TX" },
	{ "HiFi Playback", NULL, "Mixer DAI TX" },
};

SND_SOC_DAILINK_DEFS(primary,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("3830000.i2s")));

SND_SOC_DAILINK_DEFS(mixer,
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(secondary,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("3830000.i2s-sec")));

static struct snd_soc_dai_link odroid_card_dais[] = {
	{
		/* Primary FE <-> BE link */
		.ops = &odroid_card_fe_ops,
		.name = "Primary",
		.stream_name = "Primary",
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(primary),
	}, {
		/* BE <-> CODECs link */
		.name = "I2S Mixer",
		.ops = &odroid_card_be_ops,
		.no_pcm = 1,
		.playback_only = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(mixer),
	}, {
		/* Secondary FE <-> BE link */
		.ops = &odroid_card_fe_ops,
		.name = "Secondary",
		.stream_name = "Secondary",
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(secondary),
	}
};

static int odroid_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *cpu_dai = NULL;
	struct device_node *cpu, *codec;
	struct odroid_priv *priv;
	struct snd_soc_card *card;
	struct snd_soc_dai_link *link, *codec_link;
	int num_pcms, ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = &priv->card;
	card->dev = dev;

	card->owner = THIS_MODULE;
	card->fully_routed = true;

	spin_lock_init(&priv->lock);
	snd_soc_card_set_drvdata(card, priv);

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret < 0)
		return ret;

	if (of_property_present(dev->of_node, "samsung,audio-widgets")) {
		ret = snd_soc_of_parse_audio_simple_widgets(card,
						"samsung,audio-widgets");
		if (ret < 0)
			return ret;
	}

	ret = 0;
	if (of_property_present(dev->of_node, "audio-routing"))
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
	else if (of_property_present(dev->of_node, "samsung,audio-routing"))
		ret = snd_soc_of_parse_audio_routing(card, "samsung,audio-routing");
	if (ret < 0)
		return ret;

	card->dai_link = odroid_card_dais;
	card->num_links = ARRAY_SIZE(odroid_card_dais);

	cpu = of_get_child_by_name(dev->of_node, "cpu");
	codec = of_get_child_by_name(dev->of_node, "codec");
	link = card->dai_link;
	codec_link = &card->dai_link[1];

	/*
	 * For backwards compatibility create the secondary CPU DAI link only
	 * if there are 2 CPU DAI entries in the cpu sound-dai property in DT.
	 * Also add required DAPM routes not available in old DTS.
	 */
	num_pcms = of_count_phandle_with_args(cpu, "sound-dai",
					      "#sound-dai-cells");
	if (num_pcms == 1) {
		card->dapm_routes = odroid_dapm_routes;
		card->num_dapm_routes = ARRAY_SIZE(odroid_dapm_routes);
		card->num_links--;
	}

	for (i = 0; i < num_pcms; i++, link += 2) {
		ret = snd_soc_of_get_dai_name(cpu, &link->cpus->dai_name, i);
		if (ret < 0)
			break;
	}
	if (ret == 0) {
		cpu_dai = of_parse_phandle(cpu, "sound-dai", 0);
		if (!cpu_dai)
			ret = -EINVAL;
	}

	of_node_put(cpu);
	if (ret < 0)
		goto err_put_node;

	ret = snd_soc_of_get_dai_link_codecs(dev, codec, codec_link);
	if (ret < 0)
		goto err_put_cpu_dai;

	/* Set capture capability only for boards with the MAX98090 CODEC */
	if (codec_link->num_codecs > 1) {
		card->dai_link[0].playback_only = 0;
		card->dai_link[1].playback_only = 0;
	}

	priv->sclk_i2s = of_clk_get_by_name(cpu_dai, "i2s_opclk1");
	if (IS_ERR(priv->sclk_i2s)) {
		ret = PTR_ERR(priv->sclk_i2s);
		goto err_put_cpu_dai;
	}

	priv->clk_i2s_bus = of_clk_get_by_name(cpu_dai, "iis");
	if (IS_ERR(priv->clk_i2s_bus)) {
		ret = PTR_ERR(priv->clk_i2s_bus);
		goto err_put_sclk;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0) {
		dev_err_probe(dev, ret, "snd_soc_register_card() failed\n");
		goto err_put_clk_i2s;
	}

	of_node_put(cpu_dai);
	of_node_put(codec);
	return 0;

err_put_clk_i2s:
	clk_put(priv->clk_i2s_bus);
err_put_sclk:
	clk_put(priv->sclk_i2s);
err_put_cpu_dai:
	of_node_put(cpu_dai);
	snd_soc_of_put_dai_link_codecs(codec_link);
err_put_node:
	of_node_put(codec);
	return ret;
}

static void odroid_audio_remove(struct platform_device *pdev)
{
	struct odroid_priv *priv = platform_get_drvdata(pdev);

	snd_soc_of_put_dai_link_codecs(&priv->card.dai_link[1]);
	clk_put(priv->sclk_i2s);
	clk_put(priv->clk_i2s_bus);
}

static const struct of_device_id odroid_audio_of_match[] = {
	{ .compatible	= "hardkernel,odroid-xu3-audio" },
	{ .compatible	= "hardkernel,odroid-xu4-audio" },
	{ .compatible	= "samsung,odroid-xu3-audio" },
	{ .compatible	= "samsung,odroid-xu4-audio" },
	{ },
};
MODULE_DEVICE_TABLE(of, odroid_audio_of_match);

static struct platform_driver odroid_audio_driver = {
	.driver = {
		.name		= "odroid-audio",
		.of_match_table	= odroid_audio_of_match,
		.pm		= &snd_soc_pm_ops,
	},
	.probe	= odroid_audio_probe,
	.remove = odroid_audio_remove,
};
module_platform_driver(odroid_audio_driver);

MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_DESCRIPTION("Odroid XU3/XU4 audio support");
MODULE_LICENSE("GPL v2");
