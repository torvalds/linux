// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2010 Maurus Cuelenaere <mcuelenaere@gmail.com>
//
// Based on smdk6410_wm8987.c
//     Copyright 2007 Wolfson Microelectronics PLC. - linux@wolfsonmicro.com
//     Graeme Gregory - graeme.gregory@wolfsonmicro.com

#include <linux/gpio/consumer.h>
#include <linux/module.h>

#include <sound/soc.h>
#include <sound/jack.h>

#include "i2s.h"
#include "../codecs/wm8750.h"

/*
 * WM8987 is register compatible with WM8750, so using that as base driver.
 */

static struct snd_soc_card snd_soc_smartq;

static int smartq_hifi_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int clk = 0;
	int ret;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 96000:
		clk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		clk = 11289600;
		break;
	}

	/* Use PCLK for I2S signal generation */
	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_RCLKSRC_0,
					0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* Gate the RCLK output on PAD */
	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_CDCLK,
					0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	/* set the codec system clock for DAC and ADC */
	ret = snd_soc_dai_set_sysclk(codec_dai, WM8750_SYSCLK, clk,
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * SmartQ WM8987 HiFi DAI operations.
 */
static struct snd_soc_ops smartq_hifi_ops = {
	.hw_params = smartq_hifi_hw_params,
};

static struct snd_soc_jack smartq_jack;

static struct snd_soc_jack_pin smartq_jack_pins[] = {
	/* Disable speaker when headphone is plugged in */
	{
		.pin	= "Internal Speaker",
		.mask	= SND_JACK_HEADPHONE,
	},
};

static struct snd_soc_jack_gpio smartq_jack_gpios[] = {
	{
		.gpio		= -1,
		.name		= "headphone detect",
		.report		= SND_JACK_HEADPHONE,
		.debounce_time	= 200,
	},
};

static const struct snd_kcontrol_new wm8987_smartq_controls[] = {
	SOC_DAPM_PIN_SWITCH("Internal Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static int smartq_speaker_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k,
				int event)
{
	struct gpio_desc *gpio = snd_soc_card_get_drvdata(&snd_soc_smartq);

	gpiod_set_value(gpio, SND_SOC_DAPM_EVENT_OFF(event));

	return 0;
}

static const struct snd_soc_dapm_widget wm8987_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Internal Speaker", smartq_speaker_event),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "LOUT2"},
	{"Headphone Jack", NULL, "ROUT2"},

	{"Internal Speaker", NULL, "LOUT2"},
	{"Internal Speaker", NULL, "ROUT2"},

	{"Mic Bias", NULL, "Internal Mic"},
	{"LINPUT2", NULL, "Mic Bias"},
};

static int smartq_wm8987_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dapm_context *dapm = &rtd->card->dapm;
	int err = 0;

	/* set endpoints to not connected */
	snd_soc_dapm_nc_pin(dapm, "LINPUT1");
	snd_soc_dapm_nc_pin(dapm, "RINPUT1");
	snd_soc_dapm_nc_pin(dapm, "OUT3");
	snd_soc_dapm_nc_pin(dapm, "ROUT1");

	/* Headphone jack detection */
	err = snd_soc_card_jack_new(rtd->card, "Headphone Jack",
				    SND_JACK_HEADPHONE, &smartq_jack,
				    smartq_jack_pins,
				    ARRAY_SIZE(smartq_jack_pins));
	if (err)
		return err;

	err = snd_soc_jack_add_gpios(&smartq_jack,
				     ARRAY_SIZE(smartq_jack_gpios),
				     smartq_jack_gpios);

	return err;
}

static struct snd_soc_dai_link smartq_dai[] = {
	{
		.name		= "wm8987",
		.stream_name	= "SmartQ Hi-Fi",
		.cpu_dai_name	= "samsung-i2s.0",
		.codec_dai_name	= "wm8750-hifi",
		.platform_name	= "samsung-i2s.0",
		.codec_name	= "wm8750.0-0x1a",
		.init		= smartq_wm8987_init,
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBS_CFS,
		.ops		= &smartq_hifi_ops,
	},
};

static struct snd_soc_card snd_soc_smartq = {
	.name = "SmartQ",
	.owner = THIS_MODULE,
	.dai_link = smartq_dai,
	.num_links = ARRAY_SIZE(smartq_dai),

	.dapm_widgets = wm8987_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8987_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.controls = wm8987_smartq_controls,
	.num_controls = ARRAY_SIZE(wm8987_smartq_controls),
};

static int smartq_probe(struct platform_device *pdev)
{
	struct gpio_desc *gpio;
	int ret;

	platform_set_drvdata(pdev, &snd_soc_smartq);

	/* Initialise GPIOs used by amplifiers */
	gpio = devm_gpiod_get(&pdev->dev, "amplifiers shutdown",
			      GPIOD_OUT_HIGH);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "Failed to register GPK12\n");
		ret = PTR_ERR(gpio);
		goto out;
	}
	snd_soc_card_set_drvdata(&snd_soc_smartq, gpio);

	ret = devm_snd_soc_register_card(&pdev->dev, &snd_soc_smartq);
	if (ret)
		dev_err(&pdev->dev, "Failed to register card\n");

out:
	return ret;
}

static struct platform_driver smartq_driver = {
	.driver = {
		.name = "smartq-audio",
	},
	.probe = smartq_probe,
};

module_platform_driver(smartq_driver);

/* Module information */
MODULE_AUTHOR("Maurus Cuelenaere <mcuelenaere@gmail.com>");
MODULE_DESCRIPTION("ALSA SoC SmartQ WM8987");
MODULE_LICENSE("GPL");
