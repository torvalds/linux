/*
 * dmic.c  --  SoC audio for Generic Digital MICs
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

static int dmic_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct gpio_desc *dmic_en = snd_soc_dai_get_drvdata(dai);

	if (!dmic_en)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		gpiod_set_value(dmic_en, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		gpiod_set_value(dmic_en, 0);
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops dmic_dai_ops = {
	.trigger	= dmic_daiops_trigger,
};

static struct snd_soc_dai_driver dmic_dai = {
	.name = "dmic-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.formats = SNDRV_PCM_FMTBIT_S32_LE
			| SNDRV_PCM_FMTBIT_S24_LE
			| SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops    = &dmic_dai_ops,
};

static int dmic_codec_probe(struct snd_soc_codec *codec)
{
	struct gpio_desc *dmic_en;

	dmic_en = devm_gpiod_get_optional(codec->dev,
					"dmicen", GPIOD_OUT_LOW);
	if (IS_ERR(dmic_en))
		return PTR_ERR(dmic_en);

	snd_soc_codec_set_drvdata(codec, dmic_en);

	return 0;
}

static const struct snd_soc_dapm_widget dmic_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_OUT("DMIC AIF", "Capture", 0,
			     SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_INPUT("DMic"),
};

static const struct snd_soc_dapm_route intercon[] = {
	{"DMIC AIF", NULL, "DMic"},
};

static const struct snd_soc_codec_driver soc_dmic = {
	.probe = dmic_codec_probe,
	.component_driver = {
		.dapm_widgets		= dmic_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(dmic_dapm_widgets),
		.dapm_routes		= intercon,
		.num_dapm_routes	= ARRAY_SIZE(intercon),
	},
};

static int dmic_dev_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
			&soc_dmic, &dmic_dai, 1);
}

static int dmic_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

MODULE_ALIAS("platform:dmic-codec");

static const struct of_device_id dmic_dev_match[] = {
	{.compatible = "dmic-codec"},
	{}
};

static struct platform_driver dmic_driver = {
	.driver = {
		.name = "dmic-codec",
		.of_match_table = dmic_dev_match,
	},
	.probe = dmic_dev_probe,
	.remove = dmic_dev_remove,
};

module_platform_driver(dmic_driver);

MODULE_DESCRIPTION("Generic DMIC driver");
MODULE_AUTHOR("Liam Girdwood <lrg@slimlogic.co.uk>");
MODULE_LICENSE("GPL");
