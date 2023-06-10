// SPDX-License-Identifier: GPL-2.0
/*
 * PWMDAC dummy codec driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <linux/of.h>

#define DRV_NAME "pwmdac-dit"

#define STUB_RATES	SNDRV_PCM_RATE_8000_192000
#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S8|\
			SNDRV_PCM_FMTBIT_U8|\
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE  | \
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dapm_widget dit_widgets[] = {
	SND_SOC_DAPM_OUTPUT("pwmdac-out"),
};

static const struct snd_soc_dapm_route dit_routes[] = {
	{ "pwmdac-out", NULL, "Playback" },
};

static struct snd_soc_component_driver soc_codec_pwmdac_dit = {
	.dapm_widgets		= dit_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(dit_widgets),
	.dapm_routes		= dit_routes,
	.num_dapm_routes	= ARRAY_SIZE(dit_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct snd_soc_dai_driver dit_stub_dai = {
	.name		= "pwmdac-dit-hifi",
	.playback 	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
};

static int pwmdac_dit_probe(struct platform_device *pdev)
{

	return devm_snd_soc_register_component(&pdev->dev,
			&soc_codec_pwmdac_dit,
			&dit_stub_dai, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id pwmdac_dit_dt_ids[] = {
	{ .compatible = "starfive,jh7110-pwmdac-dit", },
	{ }
};
MODULE_DEVICE_TABLE(of, pwmdac_dit_dt_ids);
#endif

static struct platform_driver pwmdac_dit_driver = {
	.probe		= pwmdac_dit_probe,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = of_match_ptr(pwmdac_dit_dt_ids),
	},
};

static int __init pwmdac_dit_driver_init(void)
{
	return platform_driver_register(&pwmdac_dit_driver);
}

static void pwmdac_dit_driver_exit(void)
{
	platform_driver_unregister(&pwmdac_dit_driver);
}

late_initcall(pwmdac_dit_driver_init);
module_exit(pwmdac_dit_driver_exit);


MODULE_AUTHOR("curry.zhang <curry.zhang@starfivetech.com>");
MODULE_DESCRIPTION("pwmdac dummy codec driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform: starfive-pwmdac dummy codec");
