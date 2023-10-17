// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2023 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/processor.h>
#include <linux/slab.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <asm/intel-family.h>

#define ES8336_CODEC_DAI	"ES8316 HiFi"

struct avs_card_drvdata {
	struct snd_soc_jack jack;
	struct gpio_desc *gpiod;
};

static const struct acpi_gpio_params enable_gpio = { 0, 0, true };

static const struct acpi_gpio_mapping speaker_gpios[] = {
	{ "speaker-enable-gpios", &enable_gpio, 1, ACPI_GPIO_QUIRK_ONLY_GPIOIO },
	{ }
};

static int avs_es8336_speaker_power_event(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct avs_card_drvdata *data;
	bool speaker_en;

	data = snd_soc_card_get_drvdata(card);
	/* As enable_gpio has active_low=true, logic is inverted. */
	speaker_en = !SND_SOC_DAPM_EVENT_ON(event);

	gpiod_set_value_cansleep(data->gpiod, speaker_en);
	return 0;
}

static const struct snd_soc_dapm_widget card_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),

	SND_SOC_DAPM_SUPPLY("Speaker Power", SND_SOC_NOPM, 0, 0,
			    avs_es8336_speaker_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
};

static const struct snd_soc_dapm_route card_routes[] = {
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},

	/*
	 * There is no separate speaker output instead the speakers are muxed to
	 * the HP outputs. The mux is controlled by the "Speaker Power" widget.
	 */
	{"Speaker", NULL, "HPOL"},
	{"Speaker", NULL, "HPOR"},
	{"Speaker", NULL, "Speaker Power"},

	/* Mic route map */
	{"MIC1", NULL, "Internal Mic"},
	{"MIC2", NULL, "Headset Mic"},
};

static const struct snd_kcontrol_new card_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static struct snd_soc_jack_pin card_headset_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static int avs_es8336_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(runtime, 0);
	struct snd_soc_component *component = codec_dai->component;
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_jack_pin *pins;
	struct avs_card_drvdata *data;
	struct gpio_desc *gpiod;
	int num_pins, ret;

	data = snd_soc_card_get_drvdata(card);
	num_pins = ARRAY_SIZE(card_headset_pins);

	pins = devm_kmemdup(card->dev, card_headset_pins, sizeof(*pins) * num_pins, GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	ret = snd_soc_card_jack_new_pins(card, "Headset", SND_JACK_HEADSET | SND_JACK_BTN_0,
					 &data->jack, pins, num_pins);
	if (ret)
		return ret;

	ret = devm_acpi_dev_add_driver_gpios(codec_dai->dev, speaker_gpios);
	if (ret)
		dev_warn(codec_dai->dev, "Unable to add GPIO mapping table\n");

	gpiod = gpiod_get_optional(codec_dai->dev, "speaker-enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpiod))
		return dev_err_probe(codec_dai->dev, PTR_ERR(gpiod), "Get gpiod failed: %ld\n",
				     PTR_ERR(gpiod));

	data->gpiod = gpiod;
	snd_jack_set_key(data->jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_soc_component_set_jack(component, &data->jack, NULL);

	card->dapm.idle_bias_off = true;

	return 0;
}

static void avs_es8336_codec_exit(struct snd_soc_pcm_runtime *runtime)
{
	struct avs_card_drvdata *data = snd_soc_card_get_drvdata(runtime->card);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(runtime, 0);

	snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
	gpiod_put(data->gpiod);
}

static int avs_es8336_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *runtime = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(runtime, 0);
	int clk_freq;
	int ret;

	switch (boot_cpu_data.x86_model) {
	case INTEL_FAM6_KABYLAKE_L:
	case INTEL_FAM6_KABYLAKE:
		clk_freq = 24000000;
		break;
	default:
		clk_freq = 19200000;
		break;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 1, clk_freq, SND_SOC_CLOCK_OUT);
	if (ret < 0)
		dev_err(runtime->dev, "Set codec sysclk failed: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops avs_es8336_ops = {
	.hw_params = avs_es8336_hw_params,
};

static int avs_es8336_be_fixup(struct snd_soc_pcm_runtime *runtime,
			       struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate, *channels;
	struct snd_mask *fmt;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	channels = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will convert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSPN to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_3LE);

	return 0;
}
static int avs_create_dai_link(struct device *dev, const char *platform_name, int ssp_port,
			       struct snd_soc_dai_link **dai_link)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;

	dl = devm_kzalloc(dev, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	platform->name = platform_name;

	dl->name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec", ssp_port);
	dl->cpus = devm_kzalloc(dev, sizeof(*dl->cpus), GFP_KERNEL);
	dl->codecs = devm_kzalloc(dev, sizeof(*dl->codecs), GFP_KERNEL);
	if (!dl->name || !dl->cpus || !dl->codecs)
		return -ENOMEM;

	dl->cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", ssp_port);
	dl->codecs->name = devm_kasprintf(dev, GFP_KERNEL, "i2c-ESSX8336:00");
	dl->codecs->dai_name = devm_kasprintf(dev, GFP_KERNEL, ES8336_CODEC_DAI);
	if (!dl->cpus->dai_name || !dl->codecs->name || !dl->codecs->dai_name)
		return -ENOMEM;

	dl->num_cpus = 1;
	dl->num_codecs = 1;
	dl->platforms = platform;
	dl->num_platforms = 1;
	dl->id = 0;
	dl->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBC_CFC;
	dl->init = avs_es8336_codec_init;
	dl->exit = avs_es8336_codec_exit;
	dl->be_hw_params_fixup = avs_es8336_be_fixup;
	dl->ops = &avs_es8336_ops;
	dl->nonatomic = 1;
	dl->no_pcm = 1;
	dl->dpcm_capture = 1;
	dl->dpcm_playback = 1;

	*dai_link = dl;

	return 0;
}

static int avs_card_suspend_pre(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, ES8336_CODEC_DAI);

	return snd_soc_component_set_jack(codec_dai->component, NULL, NULL);
}

static int avs_card_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_dai *codec_dai = snd_soc_card_get_codec_dai(card, ES8336_CODEC_DAI);
	struct avs_card_drvdata *data = snd_soc_card_get_drvdata(card);

	return snd_soc_component_set_jack(codec_dai->component, &data->jack, NULL);
}

static int avs_es8336_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_acpi_mach *mach;
	struct avs_card_drvdata *data;
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	const char *pname;
	int ssp_port, ret;

	mach = dev_get_platdata(dev);
	pname = mach->mach_params.platform;
	ssp_port = __ffs(mach->mach_params.i2s_link_mask);

	ret = avs_create_dai_link(dev, pname, ssp_port, &dai_link);
	if (ret) {
		dev_err(dev, "Failed to create dai link: %d", ret);
		return ret;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!data || !card)
		return -ENOMEM;

	card->name = "avs_es8336";
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->suspend_pre = avs_card_suspend_pre;
	card->resume_post = avs_card_resume_post;
	card->dai_link = dai_link;
	card->num_links = 1;
	card->controls = card_controls;
	card->num_controls = ARRAY_SIZE(card_controls);
	card->dapm_widgets = card_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(card_widgets);
	card->dapm_routes = card_routes;
	card->num_dapm_routes = ARRAY_SIZE(card_routes);
	card->fully_routed = true;
	snd_soc_card_set_drvdata(card, data);

	ret = snd_soc_fixup_dai_links_platform_name(card, pname);
	if (ret)
		return ret;

	return devm_snd_soc_register_card(dev, card);
}

static struct platform_driver avs_es8336_driver = {
	.probe = avs_es8336_probe,
	.driver = {
		.name = "avs_es8336",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(avs_es8336_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:avs_es8336");
