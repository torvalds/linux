// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the PCM5102A codec
 *
 * Author:	Florian Meier <florian.meier@koalo.de>
 *		Copyright 2013
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static struct snd_soc_dai_driver pcm5102a_dai = {
	.name = "pcm5102a-hifi",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_384000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE
	},
};

static struct snd_soc_component_driver soc_component_dev_pcm5102a = {
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int pcm5102a_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &soc_component_dev_pcm5102a,
			&pcm5102a_dai, 1);
}

static const struct of_device_id pcm5102a_of_match[] = {
	{ .compatible = "ti,pcm5102a", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm5102a_of_match);

static struct platform_driver pcm5102a_codec_driver = {
	.probe		= pcm5102a_probe,
	.driver		= {
		.name	= "pcm5102a-codec",
		.of_match_table = pcm5102a_of_match,
	},
};

module_platform_driver(pcm5102a_codec_driver);

MODULE_DESCRIPTION("ASoC PCM5102A codec driver");
MODULE_AUTHOR("Florian Meier <florian.meier@koalo.de>");
MODULE_LICENSE("GPL v2");
