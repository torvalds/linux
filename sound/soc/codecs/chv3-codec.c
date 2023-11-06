// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <sound/soc.h>

static struct snd_soc_dai_driver chv3_codec_dai = {
	.name = "chv3-codec-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 8,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
	},
};

static const struct snd_soc_component_driver soc_component_dev_chv3_codec = {
};

static int chv3_codec_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
		&soc_component_dev_chv3_codec, &chv3_codec_dai, 1);
}

static const struct of_device_id chv3_codec_of_match[] = {
	{ .compatible = "google,chv3-codec", },
	{ }
};

static struct platform_driver chv3_codec_platform_driver = {
	.driver = {
		.name = "chv3-codec",
		.of_match_table = chv3_codec_of_match,
	},
	.probe = chv3_codec_probe,
};
module_platform_driver(chv3_codec_platform_driver);

MODULE_DESCRIPTION("ASoC Chameleon v3 codec driver");
MODULE_AUTHOR("Pawel Anikiel <pan@semihalf.com>");
MODULE_LICENSE("GPL");
