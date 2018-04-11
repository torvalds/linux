/*
 *  Intel Baytrail SST RT5660 machine driver
 *  Copyright (C) 2016 Shrirang Bagul <shrirang.bagul@canonical.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <asm/cpu_device_id.h>
#include <asm/platform_sst_audio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5660.h"
#include "../atom/sst-atom-controls.h"
#include "../common/sst-dsp.h"

#define BYT_RT5660_MCLK_EN	BIT(17)
#define BYT_RT5660_MCLK_25MHZ	BIT(18)

struct byt_rt5660_private {
	struct clk *mclk;
	struct gpio_desc *gpio_lo_mute;
};

static unsigned long byt_rt5660_quirk = BYT_RT5660_MCLK_EN;

static void log_quirks(struct device *dev)
{
	if (byt_rt5660_quirk & BYT_RT5660_MCLK_EN)
		dev_info(dev, "quirk MCLK_EN enabled");
	if (byt_rt5660_quirk & BYT_RT5660_MCLK_25MHZ)
		dev_info(dev, "quirk MCLK_25MHZ enabled");
}

static int byt_rt5660_event_lineout(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct byt_rt5660_private *priv = snd_soc_card_get_drvdata(card);

	gpiod_set_value_cansleep(priv->gpio_lo_mute,
			!(SND_SOC_DAPM_EVENT_ON(event)));

	return 0;
}

#define BYT_CODEC_DAI1	"rt5660-aif1"

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	struct byt_rt5660_private *priv = snd_soc_card_get_drvdata(card);
	int ret;

	codec_dai = snd_soc_card_get_codec_dai(card, BYT_CODEC_DAI1);
	if (!codec_dai) {
		dev_err(card->dev,
			"Codec dai not found; Unable to set platform clock\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (byt_rt5660_quirk & BYT_RT5660_MCLK_EN) {
			ret = clk_prepare_enable(priv->mclk);
			if (ret < 0) {
				dev_err(card->dev,
					"could not configure MCLK state");
				return ret;
			}
		}
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5660_SCLK_S_PLL1,
					     48000 * 512,
					     SND_SOC_CLOCK_IN);
	} else {
		/*
		 * Set codec clock source to internal clock before
		 * turning off the platform clock. Codec needs clock
		 * for Jack detection and button press
		 */
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5660_SCLK_S_RCCLK,
					     48000 * 512,
					     SND_SOC_CLOCK_IN);
		if (!ret)
			if (byt_rt5660_quirk & BYT_RT5660_MCLK_EN)
				clk_disable_unprepare(priv->mclk);
	}

	if (ret < 0) {
		dev_err(card->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_dapm_widget byt_rt5660_widgets[] = {
	SND_SOC_DAPM_MIC("Line In", NULL),
	SND_SOC_DAPM_LINE("Line Out", byt_rt5660_event_lineout),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route byt_rt5660_audio_map[] = {
	{"IN1P", NULL, "Platform Clock"},
	{"IN2P", NULL, "Platform Clock"},
	{"Line Out", NULL, "Platform Clock"},

	{"IN1P", NULL, "Line In"},
	{"IN2P", NULL, "Line In"},
	{"Line Out", NULL, "LOUTR"},
	{"Line Out", NULL, "LOUTL"},

	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
	{"AIF1 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Rx", NULL, "AIF1 Capture"},
};

static const struct snd_kcontrol_new byt_rt5660_controls[] = {
	SOC_DAPM_PIN_SWITCH("Line In"),
	SOC_DAPM_PIN_SWITCH("Line Out"),
};

static int byt_rt5660_aif1_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	snd_soc_dai_set_bclk_ratio(codec_dai, 50);

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5660_SCLK_S_PLL1,
				     params_rate(params) * 512,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set codec clock %d\n", ret);
		return ret;
	}

	if (!(byt_rt5660_quirk & BYT_RT5660_MCLK_EN)) {
		/* 2x25 bit slots on SSP2 */
		ret = snd_soc_dai_set_pll(codec_dai, 0,
					RT5660_PLL1_S_BCLK,
					params_rate(params) * 50,
					params_rate(params) * 512);
	} else {
		if (byt_rt5660_quirk & BYT_RT5660_MCLK_25MHZ) {
			ret = snd_soc_dai_set_pll(codec_dai, 0,
						RT5660_PLL1_S_MCLK,
						25000000,
						params_rate(params) * 512);
		} else {
			ret = snd_soc_dai_set_pll(codec_dai, 0,
						RT5660_PLL1_S_MCLK,
						19200000,
						params_rate(params) * 512);
		}
	}

	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	return 0;
}

static int byt_rt5660_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct byt_rt5660_private *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_codec *codec = runtime->codec;
	int ret;

	/* Request rt5660 GPIO for lineout mute control */
	priv->gpio_lo_mute = devm_gpiod_get_index(codec->dev,
			"lineout-mute", 0, 0);
	if (IS_ERR(priv->gpio_lo_mute)) {
		dev_err(card->dev, "Can't find GPIO_MUTE# gpio\n");
		return PTR_ERR(priv->gpio_lo_mute);
	}

	ret = gpiod_direction_output(priv->gpio_lo_mute, 1);
	if (ret)
		return ret;

	if (byt_rt5660_quirk & BYT_RT5660_MCLK_EN) {
		/*
		 * The firmware might enable the clock at
		 * boot (this information may or may not
		 * be reflected in the enable clock register).
		 * To change the rate we must disable the clock
		 * first to cover these cases. Due to common
		 * clock framework restrictions that do not allow
		 * to disable a clock that has not been enabled,
		 * we need to enable the clock first.
		 */
		ret = clk_prepare_enable(priv->mclk);
		if (!ret)
			clk_disable_unprepare(priv->mclk);

		if (byt_rt5660_quirk & BYT_RT5660_MCLK_25MHZ)
			ret = clk_set_rate(priv->mclk, 25000000);
		else
			ret = clk_set_rate(priv->mclk, 19200000);

		if (ret)
			dev_err(card->dev, "unable to set MCLK rate\n");
	}

	return ret;
}

static const struct snd_soc_pcm_stream byt_rt5660_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static int byt_rt5660_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret;

	/* The DSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 24-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	/*
	 * Default mode for SSP configuration is TDM 4 slot, override config
	 * with explicit setting to I2S 2ch 24-bit. The word length is set with
	 * dai_set_tdm_slot() since there is no other API exposed
	 */
	ret = snd_soc_dai_set_fmt(rtd->cpu_dai,
			SND_SOC_DAIFMT_I2S     |
			SND_SOC_DAIFMT_NB_NF   |
			SND_SOC_DAIFMT_CBS_CFS
			);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x3, 0x3, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static int byt_rt5660_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static struct snd_soc_ops byt_rt5660_aif1_ops = {
	.startup = byt_rt5660_aif1_startup,
};

static struct snd_soc_ops byt_rt5660_be_ssp2_ops = {
	.hw_params = byt_rt5660_aif1_hw_params,
};

static struct snd_soc_dai_link byt_rt5660_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Baytrail Audio Port",
		.stream_name = "Baytrail Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &byt_rt5660_aif1_ops,
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.cpu_dai_name = "deepbuffer-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &byt_rt5660_aif1_ops,
	},
		/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 0,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "rt5660-aif1",
		.codec_name = "i2c-10EC3277:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = byt_rt5660_codec_fixup,
		.ignore_suspend = 1,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = byt_rt5660_init,
		.ops = &byt_rt5660_be_ssp2_ops,
	},
};

static struct snd_soc_card byt_rt5660_card = {
	.name = "baytrailcraudio",
	.owner = THIS_MODULE,
	.dai_link = byt_rt5660_dais,
	.num_links = ARRAY_SIZE(byt_rt5660_dais),
	.dapm_widgets = byt_rt5660_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_rt5660_widgets),
	.dapm_routes = byt_rt5660_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_rt5660_audio_map),
	.controls = byt_rt5660_controls,
	.num_controls = ARRAY_SIZE(byt_rt5660_controls),
	.fully_routed = true,
};

static int byt_rt5660_probe(struct platform_device *pdev)
{
	int ret_val = 0;
	struct byt_rt5660_private *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_ATOMIC);
	if (!priv)
		return -ENOMEM;

	byt_rt5660_card.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&byt_rt5660_card, priv);

	log_quirks(&pdev->dev);

	if (byt_rt5660_quirk & BYT_RT5660_MCLK_EN) {
		priv->mclk = devm_clk_get(&pdev->dev, "pmc_plt_clk_3");
		if (IS_ERR(priv->mclk)) {
			dev_err(&pdev->dev,
				"Failed to get MCLK from pmc_plt_clk_3: %ld\n",
				PTR_ERR(priv->mclk));
			/*
			 * Fall back to bit clock usage for -ENOENT (clock not
			 * available likely due to missing dependencies), bail
			 * for all other errors, including -EPROBE_DEFER
			 */
			if (ret_val != -ENOENT)
				return ret_val;
			byt_rt5660_quirk &= ~BYT_RT5660_MCLK_EN;
		}
	}

	ret_val = devm_snd_soc_register_card(&pdev->dev, &byt_rt5660_card);

	if (ret_val) {
		dev_err(&pdev->dev, "devm_snd_soc_register_card failed %d\n",
				ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, &byt_rt5660_card);
	return ret_val;
}

static int byt_rt5660_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct byt_rt5660_private *priv = snd_soc_card_get_drvdata(card);

	devm_gpiod_put(&pdev->dev, priv->gpio_lo_mute);

	return 0;
}

static struct platform_driver byt_rt5660_audio = {
	.probe = byt_rt5660_probe,
	.remove = byt_rt5660_remove,
	.driver = {
		.name = "bytcr_rt5660",
	},
};
module_platform_driver(byt_rt5660_audio)

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail CR Machine driver");
MODULE_AUTHOR("Shrirang Bagul");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcr_rt5660");
