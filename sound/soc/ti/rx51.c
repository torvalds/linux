// SPDX-License-Identifier: GPL-2.0-only
/*
 * rx51.c  --  SoC audio for Nokia RX-51
 *
 * Copyright (C) 2008 - 2009 Nokia Corporation
 *
 * Contact: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *          Eduardo Valentin <eduardo.valentin@nokia.com>
 *          Jarkko Nikula <jarkko.nikula@bitmer.com>
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/platform_data/asoc-ti-mcbsp.h>

#include <asm/mach-types.h>

#include "omap-mcbsp.h"

enum {
	RX51_JACK_DISABLED,
	RX51_JACK_TVOUT,		/* tv-out with stereo output */
	RX51_JACK_HP,			/* headphone: stereo output, no mic */
	RX51_JACK_HS,			/* headset: stereo output with mic */
};

struct rx51_audio_pdata {
	struct gpio_desc *tvout_selection_gpio;
	struct gpio_desc *jack_detection_gpio;
	struct gpio_desc *eci_sw_gpio;
	struct gpio_desc *speaker_amp_gpio;
};

static int rx51_spk_func;
static int rx51_dmic_func;
static int rx51_jack_func;

static void rx51_ext_control(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_card *card = dapm->card;
	struct rx51_audio_pdata *pdata = snd_soc_card_get_drvdata(card);
	int hp = 0, hs = 0, tvout = 0;

	switch (rx51_jack_func) {
	case RX51_JACK_TVOUT:
		tvout = 1;
		hp = 1;
		break;
	case RX51_JACK_HS:
		hs = 1;
		/* fall through */
	case RX51_JACK_HP:
		hp = 1;
		break;
	}

	snd_soc_dapm_mutex_lock(dapm);

	if (rx51_spk_func)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Ext Spk");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Ext Spk");
	if (rx51_dmic_func)
		snd_soc_dapm_enable_pin_unlocked(dapm, "DMic");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "DMic");
	if (hp)
		snd_soc_dapm_enable_pin_unlocked(dapm, "Headphone Jack");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "Headphone Jack");
	if (hs)
		snd_soc_dapm_enable_pin_unlocked(dapm, "HS Mic");
	else
		snd_soc_dapm_disable_pin_unlocked(dapm, "HS Mic");

	gpiod_set_value(pdata->tvout_selection_gpio, tvout);

	snd_soc_dapm_sync_unlocked(dapm);

	snd_soc_dapm_mutex_unlock(dapm);
}

static int rx51_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	snd_pcm_hw_constraint_single(runtime, SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	rx51_ext_control(&card->dapm);

	return 0;
}

static int rx51_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);

	/* Set the codec system clock for DAC and ADC */
	return snd_soc_dai_set_sysclk(codec_dai, 0, 19200000,
				      SND_SOC_CLOCK_IN);
}

static const struct snd_soc_ops rx51_ops = {
	.startup = rx51_startup,
	.hw_params = rx51_hw_params,
};

static int rx51_get_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = rx51_spk_func;

	return 0;
}

static int rx51_set_spk(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (rx51_spk_func == ucontrol->value.enumerated.item[0])
		return 0;

	rx51_spk_func = ucontrol->value.enumerated.item[0];
	rx51_ext_control(&card->dapm);

	return 1;
}

static int rx51_spk_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct rx51_audio_pdata *pdata = snd_soc_card_get_drvdata(card);

	gpiod_set_raw_value_cansleep(pdata->speaker_amp_gpio,
				     !!SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static int rx51_get_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = rx51_dmic_func;

	return 0;
}

static int rx51_set_input(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (rx51_dmic_func == ucontrol->value.enumerated.item[0])
		return 0;

	rx51_dmic_func = ucontrol->value.enumerated.item[0];
	rx51_ext_control(&card->dapm);

	return 1;
}

static int rx51_get_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = rx51_jack_func;

	return 0;
}

static int rx51_set_jack(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);

	if (rx51_jack_func == ucontrol->value.enumerated.item[0])
		return 0;

	rx51_jack_func = ucontrol->value.enumerated.item[0];
	rx51_ext_control(&card->dapm);

	return 1;
}

static struct snd_soc_jack rx51_av_jack;

static struct snd_soc_jack_gpio rx51_av_jack_gpios[] = {
	{
		.name = "avdet-gpio",
		.report = SND_JACK_HEADSET,
		.invert = 1,
		.debounce_time = 200,
	},
};

static const struct snd_soc_dapm_widget aic34_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", rx51_spk_event),
	SND_SOC_DAPM_MIC("DMic", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("HS Mic", NULL),
	SND_SOC_DAPM_LINE("FM Transmitter", NULL),
	SND_SOC_DAPM_SPK("Earphone", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Ext Spk", NULL, "HPLOUT"},
	{"Ext Spk", NULL, "HPROUT"},
	{"Ext Spk", NULL, "HPLCOM"},
	{"Ext Spk", NULL, "HPRCOM"},
	{"FM Transmitter", NULL, "LLOUT"},
	{"FM Transmitter", NULL, "RLOUT"},

	{"Headphone Jack", NULL, "TPA6130A2 HPLEFT"},
	{"Headphone Jack", NULL, "TPA6130A2 HPRIGHT"},
	{"TPA6130A2 LEFTIN", NULL, "LLOUT"},
	{"TPA6130A2 RIGHTIN", NULL, "RLOUT"},

	{"DMic Rate 64", NULL, "DMic"},
	{"DMic", NULL, "Mic Bias"},

	{"b LINE2R", NULL, "MONO_LOUT"},
	{"Earphone", NULL, "b HPLOUT"},

	{"LINE1L", NULL, "HS Mic"},
	{"HS Mic", NULL, "b Mic Bias"},
};

static const char * const spk_function[] = {"Off", "On"};
static const char * const input_function[] = {"ADC", "Digital Mic"};
static const char * const jack_function[] = {
	"Off", "TV-OUT", "Headphone", "Headset"
};

static const struct soc_enum rx51_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(spk_function), spk_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(input_function), input_function),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(jack_function), jack_function),
};

static const struct snd_kcontrol_new aic34_rx51_controls[] = {
	SOC_ENUM_EXT("Speaker Function", rx51_enum[0],
		     rx51_get_spk, rx51_set_spk),
	SOC_ENUM_EXT("Input Select",  rx51_enum[1],
		     rx51_get_input, rx51_set_input),
	SOC_ENUM_EXT("Jack Function", rx51_enum[2],
		     rx51_get_jack, rx51_set_jack),
	SOC_DAPM_PIN_SWITCH("FM Transmitter"),
	SOC_DAPM_PIN_SWITCH("Earphone"),
};

static int rx51_aic34_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct rx51_audio_pdata *pdata = snd_soc_card_get_drvdata(card);
	int err;

	snd_soc_limit_volume(card, "TPA6130A2 Headphone Playback Volume", 42);

	err = omap_mcbsp_st_add_controls(rtd, 2);
	if (err < 0) {
		dev_err(card->dev, "Failed to add MCBSP controls\n");
		return err;
	}

	/* AV jack detection */
	err = snd_soc_card_jack_new(rtd->card, "AV Jack",
				    SND_JACK_HEADSET | SND_JACK_VIDEOOUT,
				    &rx51_av_jack, NULL, 0);
	if (err) {
		dev_err(card->dev, "Failed to add AV Jack\n");
		return err;
	}

	/* prepare gpio for snd_soc_jack_add_gpios */
	rx51_av_jack_gpios[0].gpio = desc_to_gpio(pdata->jack_detection_gpio);
	devm_gpiod_put(card->dev, pdata->jack_detection_gpio);

	err = snd_soc_jack_add_gpios(&rx51_av_jack,
				     ARRAY_SIZE(rx51_av_jack_gpios),
				     rx51_av_jack_gpios);
	if (err) {
		dev_err(card->dev, "Failed to add GPIOs\n");
		return err;
	}

	return err;
}

/* Digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(aic34,
	DAILINK_COMP_ARRAY(COMP_CPU("omap-mcbsp.2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("tlv320aic3x-codec.2-0018",
				      "tlv320aic3x-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("omap-mcbsp.2")));

static struct snd_soc_dai_link rx51_dai[] = {
	{
		.name = "TLV320AIC34",
		.stream_name = "AIC34",
		.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
		.init = rx51_aic34_init,
		.ops = &rx51_ops,
		SND_SOC_DAILINK_REG(aic34),
	},
};

static struct snd_soc_aux_dev rx51_aux_dev[] = {
	{
		.dlc = COMP_AUX("tlv320aic3x-codec.2-0019"),
	},
	{
		.dlc = COMP_AUX("tpa6130a2.2-0060"),
	},
};

static struct snd_soc_codec_conf rx51_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF("tlv320aic3x-codec.2-0019"),
		.name_prefix = "b",
	},
	{
		.dlc = COMP_CODEC_CONF("tpa6130a2.2-0060"),
		.name_prefix = "TPA6130A2",
	},
};

/* Audio card */
static struct snd_soc_card rx51_sound_card = {
	.name = "RX-51",
	.owner = THIS_MODULE,
	.dai_link = rx51_dai,
	.num_links = ARRAY_SIZE(rx51_dai),
	.aux_dev = rx51_aux_dev,
	.num_aux_devs = ARRAY_SIZE(rx51_aux_dev),
	.codec_conf = rx51_codec_conf,
	.num_configs = ARRAY_SIZE(rx51_codec_conf),
	.fully_routed = true,

	.controls = aic34_rx51_controls,
	.num_controls = ARRAY_SIZE(aic34_rx51_controls),
	.dapm_widgets = aic34_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(aic34_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static int rx51_soc_probe(struct platform_device *pdev)
{
	struct rx51_audio_pdata *pdata;
	struct device_node *np = pdev->dev.of_node;
	struct snd_soc_card *card = &rx51_sound_card;
	int err;

	if (!machine_is_nokia_rx51() && !of_machine_is_compatible("nokia,omap3-n900"))
		return -ENODEV;

	card->dev = &pdev->dev;

	if (np) {
		struct device_node *dai_node;

		dai_node = of_parse_phandle(np, "nokia,cpu-dai", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "McBSP node is not provided\n");
			return -EINVAL;
		}
		rx51_dai[0].cpus->dai_name = NULL;
		rx51_dai[0].platforms->name = NULL;
		rx51_dai[0].cpus->of_node = dai_node;
		rx51_dai[0].platforms->of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,audio-codec", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "Codec node is not provided\n");
			return -EINVAL;
		}
		rx51_dai[0].codecs->name = NULL;
		rx51_dai[0].codecs->of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,audio-codec", 1);
		if (!dai_node) {
			dev_err(&pdev->dev, "Auxiliary Codec node is not provided\n");
			return -EINVAL;
		}
		rx51_aux_dev[0].dlc.name = NULL;
		rx51_aux_dev[0].dlc.of_node = dai_node;
		rx51_codec_conf[0].dlc.name = NULL;
		rx51_codec_conf[0].dlc.of_node = dai_node;

		dai_node = of_parse_phandle(np, "nokia,headphone-amplifier", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "Headphone amplifier node is not provided\n");
			return -EINVAL;
		}
		rx51_aux_dev[1].dlc.name = NULL;
		rx51_aux_dev[1].dlc.of_node = dai_node;
		rx51_codec_conf[1].dlc.name = NULL;
		rx51_codec_conf[1].dlc.of_node = dai_node;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	snd_soc_card_set_drvdata(card, pdata);

	pdata->tvout_selection_gpio = devm_gpiod_get(card->dev,
						     "tvout-selection",
						     GPIOD_OUT_LOW);
	if (IS_ERR(pdata->tvout_selection_gpio)) {
		dev_err(card->dev, "could not get tvout selection gpio\n");
		return PTR_ERR(pdata->tvout_selection_gpio);
	}

	pdata->jack_detection_gpio = devm_gpiod_get(card->dev,
						    "jack-detection",
						    GPIOD_ASIS);
	if (IS_ERR(pdata->jack_detection_gpio)) {
		dev_err(card->dev, "could not get jack detection gpio\n");
		return PTR_ERR(pdata->jack_detection_gpio);
	}

	pdata->eci_sw_gpio = devm_gpiod_get(card->dev, "eci-switch",
					    GPIOD_OUT_HIGH);
	if (IS_ERR(pdata->eci_sw_gpio)) {
		dev_err(card->dev, "could not get eci switch gpio\n");
		return PTR_ERR(pdata->eci_sw_gpio);
	}

	pdata->speaker_amp_gpio = devm_gpiod_get(card->dev,
						 "speaker-amplifier",
						 GPIOD_OUT_LOW);
	if (IS_ERR(pdata->speaker_amp_gpio)) {
		dev_err(card->dev, "could not get speaker enable gpio\n");
		return PTR_ERR(pdata->speaker_amp_gpio);
	}

	err = devm_snd_soc_register_card(card->dev, card);
	if (err) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", err);
		return err;
	}

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id rx51_audio_of_match[] = {
	{ .compatible = "nokia,n900-audio", },
	{},
};
MODULE_DEVICE_TABLE(of, rx51_audio_of_match);
#endif

static struct platform_driver rx51_soc_driver = {
	.driver = {
		.name = "rx51-audio",
		.of_match_table = of_match_ptr(rx51_audio_of_match),
	},
	.probe = rx51_soc_probe,
};

module_platform_driver(rx51_soc_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("ALSA SoC Nokia RX-51");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rx51-audio");
