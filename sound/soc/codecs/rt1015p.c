// SPDX-License-Identifier: GPL-2.0-only
//
// rt1015p.c  --  RT1015P ALSA SoC audio amplifier driver
//
// Copyright 2020 The Linux Foundation. All rights reserved.

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>

struct rt1015p_priv {
	struct gpio_desc *sdb;
	int sdb_switch;
};

static int rt1015p_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1015p_priv *rt1015p =
		snd_soc_component_get_drvdata(component);

	if (!rt1015p->sdb)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (rt1015p->sdb_switch) {
			gpiod_set_value(rt1015p->sdb, 1);
			dev_dbg(component->dev, "set sdb to 1");
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		gpiod_set_value(rt1015p->sdb, 0);
		dev_dbg(component->dev, "set sdb to 0");
		break;
	}

	return 0;
}

static int rt1015p_sdb_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt1015p_priv *rt1015p =
		snd_soc_component_get_drvdata(component);

	if (event & SND_SOC_DAPM_POST_PMU)
		rt1015p->sdb_switch = 1;
	else if (event & SND_SOC_DAPM_POST_PMD)
		rt1015p->sdb_switch = 0;

	return 0;
}

static const struct snd_soc_dapm_widget rt1015p_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Speaker"),
	SND_SOC_DAPM_OUT_DRV_E("SDB", SND_SOC_NOPM, 0, 0, NULL, 0,
			rt1015p_sdb_event,
			SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route rt1015p_dapm_routes[] = {
	{"SDB", NULL, "HiFi Playback"},
	{"Speaker", NULL, "SDB"},
};

static const struct snd_soc_component_driver rt1015p_component_driver = {
	.dapm_widgets		= rt1015p_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rt1015p_dapm_widgets),
	.dapm_routes		= rt1015p_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rt1015p_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_dai_ops rt1015p_dai_ops = {
	.trigger        = rt1015p_daiops_trigger,
};

static struct snd_soc_dai_driver rt1015p_dai_driver = {
	.name = "HiFi",
	.playback = {
		.stream_name	= "HiFi Playback",
		.formats	= SNDRV_PCM_FMTBIT_S24,
		.rates		= SNDRV_PCM_RATE_48000,
		.channels_min	= 1,
		.channels_max	= 2,
	},
	.ops    = &rt1015p_dai_ops,
};

static int rt1015p_platform_probe(struct platform_device *pdev)
{
	struct rt1015p_priv *rt1015p;

	rt1015p = devm_kzalloc(&pdev->dev, sizeof(*rt1015p), GFP_KERNEL);
	if (!rt1015p)
		return -ENOMEM;

	rt1015p->sdb = devm_gpiod_get_optional(&pdev->dev,
				"sdb", GPIOD_OUT_LOW);
	if (IS_ERR(rt1015p->sdb))
		return PTR_ERR(rt1015p->sdb);

	dev_set_drvdata(&pdev->dev, rt1015p);

	return devm_snd_soc_register_component(&pdev->dev,
			&rt1015p_component_driver,
			&rt1015p_dai_driver, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id rt1015p_device_id[] = {
	{ .compatible = "realtek,rt1015p" },
	{}
};
MODULE_DEVICE_TABLE(of, rt1015p_device_id);
#endif

static struct platform_driver rt1015p_platform_driver = {
	.driver = {
		.name = "rt1015p",
		.of_match_table = of_match_ptr(rt1015p_device_id),
	},
	.probe = rt1015p_platform_probe,
};
module_platform_driver(rt1015p_platform_driver);

MODULE_DESCRIPTION("ASoC RT1015P driver");
MODULE_LICENSE("GPL v2");
