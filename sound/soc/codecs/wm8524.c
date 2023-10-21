// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8524.c  --  WM8524 ALSA SoC Audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 * Copyright 2017 NXP
 *
 * Based on WM8523 ALSA SoC Audio driver written by Mark Brown
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#define WM8524_NUM_RATES 7

/* codec private data */
struct wm8524_priv {
	struct gpio_desc *mute;
	unsigned int sysclk;
	unsigned int rate_constraint_list[WM8524_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;
};


static const struct snd_soc_dapm_widget wm8524_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("LINEVOUTL"),
SND_SOC_DAPM_OUTPUT("LINEVOUTR"),
};

static const struct snd_soc_dapm_route wm8524_dapm_routes[] = {
	{ "LINEVOUTL", NULL, "DAC" },
	{ "LINEVOUTR", NULL, "DAC" },
};

static const struct {
	int value;
	int ratio;
} lrclk_ratios[WM8524_NUM_RATES] = {
	{ 1, 128 },
	{ 2, 192 },
	{ 3, 256 },
	{ 4, 384 },
	{ 5, 512 },
	{ 6, 768 },
	{ 7, 1152 },
};

static int wm8524_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8524_priv *wm8524 = snd_soc_component_get_drvdata(component);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!wm8524->sysclk) {
		dev_err(component->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &wm8524->rate_constraint);

	gpiod_set_value_cansleep(wm8524->mute, 1);

	return 0;
}

static void wm8524_shutdown(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8524_priv *wm8524 = snd_soc_component_get_drvdata(component);

	gpiod_set_value_cansleep(wm8524->mute, 0);
}

static int wm8524_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct wm8524_priv *wm8524 = snd_soc_component_get_drvdata(component);
	unsigned int val;
	int i, j = 0;

	wm8524->sysclk = freq;

	wm8524->rate_constraint.count = 0;
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		val = freq / lrclk_ratios[i].ratio;
		/* Check that it's a standard rate since core can't
		 * cope with others and having the odd rates confuses
		 * constraint matching.
		 */
		switch (val) {
		case 8000:
		case 32000:
		case 44100:
		case 48000:
		case 88200:
		case 96000:
		case 176400:
		case 192000:
			dev_dbg(component->dev, "Supported sample rate: %dHz\n",
				val);
			wm8524->rate_constraint_list[j++] = val;
			wm8524->rate_constraint.count++;
			break;
		default:
			dev_dbg(component->dev, "Skipping sample rate: %dHz\n",
				val);
		}
	}

	/* Need at least one supported rate... */
	if (wm8524->rate_constraint.count == 0)
		return -EINVAL;

	return 0;
}

static int wm8524_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	fmt &= (SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_INV_MASK |
		SND_SOC_DAIFMT_MASTER_MASK);

	if (fmt != (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		    SND_SOC_DAIFMT_CBS_CFS)) {
		dev_err(codec_dai->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	return 0;
}

static int wm8524_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct wm8524_priv *wm8524 = snd_soc_component_get_drvdata(dai->component);

	if (wm8524->mute)
		gpiod_set_value_cansleep(wm8524->mute, mute);

	return 0;
}

#define WM8524_RATES SNDRV_PCM_RATE_8000_192000

#define WM8524_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8524_dai_ops = {
	.startup	= wm8524_startup,
	.shutdown	= wm8524_shutdown,
	.set_sysclk	= wm8524_set_dai_sysclk,
	.set_fmt	= wm8524_set_fmt,
	.mute_stream	= wm8524_mute_stream,
};

static struct snd_soc_dai_driver wm8524_dai = {
	.name = "wm8524-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8524_RATES,
		.formats = WM8524_FORMATS,
	},
	.ops = &wm8524_dai_ops,
};

static int wm8524_probe(struct snd_soc_component *component)
{
	struct wm8524_priv *wm8524 = snd_soc_component_get_drvdata(component);

	wm8524->rate_constraint.list = &wm8524->rate_constraint_list[0];
	wm8524->rate_constraint.count =
		ARRAY_SIZE(wm8524->rate_constraint_list);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_wm8524 = {
	.probe			= wm8524_probe,
	.dapm_widgets		= wm8524_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8524_dapm_widgets),
	.dapm_routes		= wm8524_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8524_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct of_device_id wm8524_of_match[] = {
	{ .compatible = "wlf,wm8524" },
	{ /* sentinel*/ }
};
MODULE_DEVICE_TABLE(of, wm8524_of_match);

static int wm8524_codec_probe(struct platform_device *pdev)
{
	struct wm8524_priv *wm8524;
	int ret;

	wm8524 = devm_kzalloc(&pdev->dev, sizeof(struct wm8524_priv),
						  GFP_KERNEL);
	if (wm8524 == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, wm8524);

	wm8524->mute = devm_gpiod_get(&pdev->dev, "wlf,mute", GPIOD_OUT_LOW);
	if (IS_ERR(wm8524->mute)) {
		ret = PTR_ERR(wm8524->mute);
		dev_err_probe(&pdev->dev, ret, "Failed to get mute line\n");
		return ret;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_wm8524, &wm8524_dai, 1);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);

	return ret;
}

static struct platform_driver wm8524_codec_driver = {
	.probe		= wm8524_codec_probe,
	.driver		= {
		.name	= "wm8524-codec",
		.of_match_table = wm8524_of_match,
	},
};
module_platform_driver(wm8524_codec_driver);

MODULE_DESCRIPTION("ASoC WM8524 driver");
MODULE_AUTHOR("Mihai Serban <mihai.serban@nxp.com>");
MODULE_ALIAS("platform:wm8524-codec");
MODULE_LICENSE("GPL");
