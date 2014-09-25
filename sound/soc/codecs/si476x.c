/*
 * sound/soc/codecs/si476x.c -- Codec driver for SI476X chips
 *
 * Copyright (C) 2012 Innovative Converged Devices(ICD)
 * Copyright (C) 2013 Andrey Smirnov
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <linux/i2c.h>

#include <linux/mfd/si476x-core.h>

enum si476x_audio_registers {
	SI476X_DIGITAL_IO_OUTPUT_FORMAT		= 0x0203,
	SI476X_DIGITAL_IO_OUTPUT_SAMPLE_RATE	= 0x0202,
};

enum si476x_digital_io_output_format {
	SI476X_DIGITAL_IO_SLOT_SIZE_SHIFT	= 11,
	SI476X_DIGITAL_IO_SAMPLE_SIZE_SHIFT	= 8,
};

#define SI476X_DIGITAL_IO_OUTPUT_WIDTH_MASK	((0x7 << SI476X_DIGITAL_IO_SLOT_SIZE_SHIFT) | \
						  (0x7 << SI476X_DIGITAL_IO_SAMPLE_SIZE_SHIFT))
#define SI476X_DIGITAL_IO_OUTPUT_FORMAT_MASK	(0x7e)

enum si476x_daudio_formats {
	SI476X_DAUDIO_MODE_I2S		= (0x0 << 1),
	SI476X_DAUDIO_MODE_DSP_A	= (0x6 << 1),
	SI476X_DAUDIO_MODE_DSP_B	= (0x7 << 1),
	SI476X_DAUDIO_MODE_LEFT_J	= (0x8 << 1),
	SI476X_DAUDIO_MODE_RIGHT_J	= (0x9 << 1),

	SI476X_DAUDIO_MODE_IB		= (1 << 5),
	SI476X_DAUDIO_MODE_IF		= (1 << 6),
};

enum si476x_pcm_format {
	SI476X_PCM_FORMAT_S8		= 2,
	SI476X_PCM_FORMAT_S16_LE	= 4,
	SI476X_PCM_FORMAT_S20_3LE	= 5,
	SI476X_PCM_FORMAT_S24_LE	= 6,
};

static const struct snd_soc_dapm_widget si476x_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("ROUT"),
};

static const struct snd_soc_dapm_route si476x_dapm_routes[] = {
	{ "Capture", NULL, "LOUT" },
	{ "Capture", NULL, "ROUT" },
};

static int si476x_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
				    unsigned int fmt)
{
	struct si476x_core *core = i2c_mfd_cell_to_core(codec_dai->dev);
	int err;
	u16 format = 0;

	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
		return -EINVAL;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		format |= SI476X_DAUDIO_MODE_DSP_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format |= SI476X_DAUDIO_MODE_DSP_B;
		break;
	case SND_SOC_DAIFMT_I2S:
		format |= SI476X_DAUDIO_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		format |= SI476X_DAUDIO_MODE_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format |= SI476X_DAUDIO_MODE_LEFT_J;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			format |= SI476X_DAUDIO_MODE_IB;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			format |= SI476X_DAUDIO_MODE_IB |
				SI476X_DAUDIO_MODE_IF;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			format |= SI476X_DAUDIO_MODE_IB;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			format |= SI476X_DAUDIO_MODE_IF;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	si476x_core_lock(core);

	err = snd_soc_update_bits(codec_dai->codec, SI476X_DIGITAL_IO_OUTPUT_FORMAT,
				  SI476X_DIGITAL_IO_OUTPUT_FORMAT_MASK,
				  format);

	si476x_core_unlock(core);

	if (err < 0) {
		dev_err(codec_dai->codec->dev, "Failed to set output format\n");
		return err;
	}

	return 0;
}

static int si476x_codec_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct si476x_core *core = i2c_mfd_cell_to_core(dai->dev);
	int rate, width, err;

	rate = params_rate(params);
	if (rate < 32000 || rate > 48000) {
		dev_err(dai->codec->dev, "Rate: %d is not supported\n", rate);
		return -EINVAL;
	}

	switch (params_width(params)) {
	case 8:
		width = SI476X_PCM_FORMAT_S8;
		break;
	case 16:
		width = SI476X_PCM_FORMAT_S16_LE;
		break;
	case 20:
		width = SI476X_PCM_FORMAT_S20_3LE;
		break;
	case 24:
		width = SI476X_PCM_FORMAT_S24_LE;
		break;
	default:
		return -EINVAL;
	}

	si476x_core_lock(core);

	err = snd_soc_write(dai->codec, SI476X_DIGITAL_IO_OUTPUT_SAMPLE_RATE,
			    rate);
	if (err < 0) {
		dev_err(dai->codec->dev, "Failed to set sample rate\n");
		goto out;
	}

	err = snd_soc_update_bits(dai->codec, SI476X_DIGITAL_IO_OUTPUT_FORMAT,
				  SI476X_DIGITAL_IO_OUTPUT_WIDTH_MASK,
				  (width << SI476X_DIGITAL_IO_SLOT_SIZE_SHIFT) |
				  (width << SI476X_DIGITAL_IO_SAMPLE_SIZE_SHIFT));
	if (err < 0) {
		dev_err(dai->codec->dev, "Failed to set output width\n");
		goto out;
	}

out:
	si476x_core_unlock(core);

	return err;
}

static struct snd_soc_dai_ops si476x_dai_ops = {
	.hw_params	= si476x_codec_hw_params,
	.set_fmt	= si476x_codec_set_dai_fmt,
};

static struct snd_soc_dai_driver si476x_dai = {
	.name		= "si476x-codec",
	.capture	= {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 2,

		.rates = SNDRV_PCM_RATE_32000 |
		SNDRV_PCM_RATE_44100 |
		SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S8 |
		SNDRV_PCM_FMTBIT_S16_LE |
		SNDRV_PCM_FMTBIT_S20_3LE |
		SNDRV_PCM_FMTBIT_S24_LE
	},
	.ops		= &si476x_dai_ops,
};

static struct regmap *si476x_get_regmap(struct device *dev)
{
	return dev_get_regmap(dev->parent, NULL);
}

static struct snd_soc_codec_driver soc_codec_dev_si476x = {
	.get_regmap = si476x_get_regmap,
	.dapm_widgets = si476x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(si476x_dapm_widgets),
	.dapm_routes = si476x_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(si476x_dapm_routes),
};

static int si476x_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_si476x,
				      &si476x_dai, 1);
}

static int si476x_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

MODULE_ALIAS("platform:si476x-codec");

static struct platform_driver si476x_platform_driver = {
	.driver		= {
		.name	= "si476x-codec",
		.owner	= THIS_MODULE,
	},
	.probe		= si476x_platform_probe,
	.remove		= si476x_platform_remove,
};
module_platform_driver(si476x_platform_driver);

MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("ASoC Si4761/64 codec driver");
MODULE_LICENSE("GPL");
