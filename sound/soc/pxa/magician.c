// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SoC audio for HTC Magician
 *
 * Copyright (c) 2006 Philipp Zabel <philipp.zabel@gmail.com>
 *
 * based on spitz.c,
 * Authors: Liam Girdwood <lrg@slimlogic.co.uk>
 *          Richard Purdie <richard@openedhand.com>
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/mach-types.h>
#include "../codecs/uda1380.h"
#include "pxa2xx-i2s.h"
#include "pxa-ssp.h"

#define MAGICIAN_MIC       0
#define MAGICIAN_MIC_EXT   1

static int magician_hp_switch;
static int magician_spk_switch = 1;
static int magician_in_sel = MAGICIAN_MIC;

static struct gpio_desc *gpiod_spk_power, *gpiod_ep_power, *gpiod_mic_power;
static struct gpio_desc *gpiod_in_sel0, *gpiod_in_sel1;

static void magician_ext_control(struct snd_soc_dapm_context *dapm)
{

	snd_soc_dapm_mutex_lock(dapm);

	if (magician_spk_switch)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Speaker");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Speaker");
	if (magician_hp_switch)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");

	switch (magician_in_sel) {
	case MAGICIAN_MIC:
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headset Mic");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Call Mic");
		break;
	case MAGICIAN_MIC_EXT:
		snd_soc_dapm_disable_pin_unlocked(dapm, "Call Mic");
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headset Mic");
		break;
	}

	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

static int magician_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	/* check the jack status at stream startup */
	magician_ext_control(&rtd->card->dapm);

	return 0;
}

/*
 * Magician uses SSP port for playback.
 */
static int magician_playback_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	unsigned int width;
	int ret = 0;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_MSB |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_NB_IF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	width = snd_pcm_format_physical_width(params_format(params));
	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 1, 0, 1, width);
	if (ret < 0)
		return ret;

	/* set audio clock as clock source */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA_SSP_CLK_AUDIO, 0,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

/*
 * Magician uses I2S for capture.
 */
static int magician_capture_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int ret = 0;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai,
			SND_SOC_DAIFMT_MSB | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai,
			SND_SOC_DAIFMT_MSB | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		return ret;

	/* set the I2S system clock as output */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_ops magician_capture_ops = {
	.startup = magician_startup,
	.hw_params = magician_capture_hw_params,
};

static const struct snd_soc_ops magician_playback_ops = {
	.startup = magician_startup,
	.hw_params = magician_playback_hw_params,
};

static int magician_get_hp(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = magician_hp_switch;
	return 0;
}

static int magician_set_hp(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (magician_hp_switch == ucontrol->value.integer.value[0])
		return 0;

	magician_hp_switch = ucontrol->value.integer.value[0];
	magician_ext_control(&card->dapm);
	return 1;
}

static int magician_get_spk(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = magician_spk_switch;
	return 0;
}

static int magician_set_spk(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (magician_spk_switch == ucontrol->value.integer.value[0])
		return 0;

	magician_spk_switch = ucontrol->value.integer.value[0];
	magician_ext_control(&card->dapm);
	return 1;
}

static int magician_get_input(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = magician_in_sel;
	return 0;
}

static int magician_set_input(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	if (magician_in_sel == ucontrol->value.enumerated.item[0])
		return 0;

	magician_in_sel = ucontrol->value.enumerated.item[0];

	switch (magician_in_sel) {
	case MAGICIAN_MIC:
		gpiod_set_value(gpiod_in_sel1, 1);
		break;
	case MAGICIAN_MIC_EXT:
		gpiod_set_value(gpiod_in_sel1, 0);
	}

	return 1;
}

static int magician_spk_power(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	gpiod_set_value(gpiod_spk_power, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int magician_hp_power(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	gpiod_set_value(gpiod_ep_power, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int magician_mic_bias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	gpiod_set_value(gpiod_mic_power, SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

/* magician machine dapm widgets */
static const struct snd_soc_dapm_widget uda1380_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", magician_hp_power),
	SND_SOC_DAPM_SPK("Speaker", magician_spk_power),
	SND_SOC_DAPM_MIC("Call Mic", magician_mic_bias),
	SND_SOC_DAPM_MIC("Headset Mic", magician_mic_bias),
};

/* magician machine audio_map */
static const struct snd_soc_dapm_route audio_map[] = {

	/* Headphone connected to VOUTL, VOUTR */
	{"Headphone Jack", NULL, "VOUTL"},
	{"Headphone Jack", NULL, "VOUTR"},

	/* Speaker connected to VOUTL, VOUTR */
	{"Speaker", NULL, "VOUTL"},
	{"Speaker", NULL, "VOUTR"},

	/* Mics are connected to VINM */
	{"VINM", NULL, "Headset Mic"},
	{"VINM", NULL, "Call Mic"},
};

static const char * const input_select[] = {"Call Mic", "Headset Mic"};
static const struct soc_enum magician_in_sel_enum =
	SOC_ENUM_SINGLE_EXT(2, input_select);

static const struct snd_kcontrol_new uda1380_magician_controls[] = {
	SOC_SINGLE_BOOL_EXT("Headphone Switch",
			(unsigned long)&magician_hp_switch,
			magician_get_hp, magician_set_hp),
	SOC_SINGLE_BOOL_EXT("Speaker Switch",
			(unsigned long)&magician_spk_switch,
			magician_get_spk, magician_set_spk),
	SOC_ENUM_EXT("Input Select", magician_in_sel_enum,
			magician_get_input, magician_set_input),
};

/* magician digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(playback,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa-ssp-dai.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("uda1380-codec.0-0018",
				      "uda1380-hifi-playback")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

SND_SOC_DAILINK_DEFS(capture,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-i2s")),
	DAILINK_COMP_ARRAY(COMP_CODEC("uda1380-codec.0-0018",
				      "uda1380-hifi-capture")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link magician_dai[] = {
{
	.name = "uda1380",
	.stream_name = "UDA1380 Playback",
	.ops = &magician_playback_ops,
	SND_SOC_DAILINK_REG(playback),
},
{
	.name = "uda1380",
	.stream_name = "UDA1380 Capture",
	.ops = &magician_capture_ops,
	SND_SOC_DAILINK_REG(capture),
}
};

/* magician audio machine driver */
static struct snd_soc_card snd_soc_card_magician = {
	.name = "Magician",
	.owner = THIS_MODULE,
	.dai_link = magician_dai,
	.num_links = ARRAY_SIZE(magician_dai),

	.controls = uda1380_magician_controls,
	.num_controls = ARRAY_SIZE(uda1380_magician_controls),
	.dapm_widgets = uda1380_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(uda1380_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed = true,
};

static int magician_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	gpiod_spk_power = devm_gpiod_get(dev, "SPK_POWER", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_spk_power))
		return PTR_ERR(gpiod_spk_power);
	gpiod_ep_power = devm_gpiod_get(dev, "EP_POWER", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_ep_power))
		return PTR_ERR(gpiod_ep_power);
	gpiod_mic_power = devm_gpiod_get(dev, "MIC_POWER", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_mic_power))
		return PTR_ERR(gpiod_mic_power);
	gpiod_in_sel0 = devm_gpiod_get(dev, "IN_SEL0", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_in_sel0))
		return PTR_ERR(gpiod_in_sel0);
	gpiod_in_sel1 = devm_gpiod_get(dev, "IN_SEL1", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod_in_sel1))
		return PTR_ERR(gpiod_in_sel1);

	snd_soc_card_magician.dev = &pdev->dev;
	return devm_snd_soc_register_card(&pdev->dev, &snd_soc_card_magician);
}

static struct platform_driver magician_audio_driver = {
	.driver.name = "magician-audio",
	.driver.pm = &snd_soc_pm_ops,
	.probe = magician_audio_probe,
};
module_platform_driver(magician_audio_driver);

MODULE_AUTHOR("Philipp Zabel");
MODULE_DESCRIPTION("ALSA SoC Magician");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:magician-audio");
