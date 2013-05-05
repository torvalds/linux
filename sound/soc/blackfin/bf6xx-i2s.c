/*
 * bf6xx-i2s.c - Analog Devices BF6XX i2s interface driver
 *
 * Copyright (c) 2012 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "bf6xx-sport.h"

struct sport_params param;

static int bfin_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct sport_device *sport = snd_soc_dai_get_drvdata(cpu_dai);
	struct device *dev = &sport->pdev->dev;
	int ret = 0;

	param.spctl &= ~(SPORT_CTL_OPMODE | SPORT_CTL_CKRE | SPORT_CTL_FSR
			| SPORT_CTL_LFS | SPORT_CTL_LAFS);
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		param.spctl |= SPORT_CTL_OPMODE | SPORT_CTL_CKRE
			| SPORT_CTL_LFS;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		param.spctl |= SPORT_CTL_FSR;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		param.spctl |= SPORT_CTL_OPMODE | SPORT_CTL_LFS
			| SPORT_CTL_LAFS;
		break;
	default:
		dev_err(dev, "%s: Unknown DAI format type\n", __func__);
		ret = -EINVAL;
		break;
	}

	param.spctl &= ~(SPORT_CTL_ICLK | SPORT_CTL_IFS);
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		ret = -EINVAL;
		break;
	default:
		dev_err(dev, "%s: Unknown DAI master type\n", __func__);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int bfin_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct sport_device *sport = snd_soc_dai_get_drvdata(dai);
	struct device *dev = &sport->pdev->dev;
	int ret = 0;

	param.spctl &= ~SPORT_CTL_SLEN;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		param.spctl |= 0x70;
		sport->wdsize = 1;
	case SNDRV_PCM_FORMAT_S16_LE:
		param.spctl |= 0xf0;
		sport->wdsize = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		param.spctl |= 0x170;
		sport->wdsize = 3;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		param.spctl |= 0x1f0;
		sport->wdsize = 4;
		break;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = sport_set_tx_params(sport, &param);
		if (ret) {
			dev_err(dev, "SPORT tx is busy!\n");
			return ret;
		}
	} else {
		ret = sport_set_rx_params(sport, &param);
		if (ret) {
			dev_err(dev, "SPORT rx is busy!\n");
			return ret;
		}
	}
	return 0;
}

#ifdef CONFIG_PM
static int bfin_i2s_suspend(struct snd_soc_dai *dai)
{
	struct sport_device *sport = snd_soc_dai_get_drvdata(dai);

	if (dai->capture_active)
		sport_rx_stop(sport);
	if (dai->playback_active)
		sport_tx_stop(sport);
	return 0;
}

static int bfin_i2s_resume(struct snd_soc_dai *dai)
{
	struct sport_device *sport = snd_soc_dai_get_drvdata(dai);
	struct device *dev = &sport->pdev->dev;
	int ret;

	ret = sport_set_tx_params(sport, &param);
	if (ret) {
		dev_err(dev, "SPORT tx is busy!\n");
		return ret;
	}
	ret = sport_set_rx_params(sport, &param);
	if (ret) {
		dev_err(dev, "SPORT rx is busy!\n");
		return ret;
	}

	return 0;
}

#else
#define bfin_i2s_suspend NULL
#define bfin_i2s_resume NULL
#endif

#define BFIN_I2S_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
		SNDRV_PCM_RATE_96000)

#define BFIN_I2S_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops bfin_i2s_dai_ops = {
	.hw_params	= bfin_i2s_hw_params,
	.set_fmt	= bfin_i2s_set_dai_fmt,
};

static struct snd_soc_dai_driver bfin_i2s_dai = {
	.suspend = bfin_i2s_suspend,
	.resume = bfin_i2s_resume,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = BFIN_I2S_RATES,
		.formats = BFIN_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = BFIN_I2S_RATES,
		.formats = BFIN_I2S_FORMATS,
	},
	.ops = &bfin_i2s_dai_ops,
};

static const struct snd_soc_component_driver bfin_i2s_component = {
	.name		= "bfin-i2s",
};

static int bfin_i2s_probe(struct platform_device *pdev)
{
	struct sport_device *sport;
	struct device *dev = &pdev->dev;
	int ret;

	sport = sport_create(pdev);
	if (!sport)
		return -ENODEV;

	/* register with the ASoC layers */
	ret = snd_soc_register_component(dev, &bfin_i2s_component,
					 &bfin_i2s_dai, 1);
	if (ret) {
		dev_err(dev, "Failed to register DAI: %d\n", ret);
		sport_delete(sport);
		return ret;
	}
	platform_set_drvdata(pdev, sport);

	return 0;
}

static int bfin_i2s_remove(struct platform_device *pdev)
{
	struct sport_device *sport = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);
	sport_delete(sport);

	return 0;
}

static struct platform_driver bfin_i2s_driver = {
	.probe  = bfin_i2s_probe,
	.remove = bfin_i2s_remove,
	.driver = {
		.name = "bfin-i2s",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(bfin_i2s_driver);

MODULE_DESCRIPTION("Analog Devices BF6XX i2s interface driver");
MODULE_AUTHOR("Scott Jiang <Scott.Jiang.Linux@gmail.com>");
MODULE_LICENSE("GPL v2");
