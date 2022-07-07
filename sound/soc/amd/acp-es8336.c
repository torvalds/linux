// SPDX-License-Identifier: GPL-2.0+
/*
 * Machine driver for AMD Stoney platform using ES8336 Codec
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/acpi.h>

#include "../codecs/es8316.h"
#include "acp.h"

#define DUAL_CHANNEL	2
#define DRV_NAME "acp2x_mach"
#define ST_JADEITE	1
#define ES8336_PLL_FREQ (48000 * 256)

static unsigned long acp2x_machine_id;
static struct snd_soc_jack st_jack;
static struct device *codec_dev;
static struct gpio_desc *gpio_pa;

static int sof_es8316_speaker_power_event(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *kcontrol, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		gpiod_set_value_cansleep(gpio_pa, true);
	else
		gpiod_set_value_cansleep(gpio_pa, false);

	return 0;
}

static struct snd_soc_jack_pin st_es8316_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static int st_es8336_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card;
	struct snd_soc_component *codec;

	codec = asoc_rtd_to_codec(rtd, 0)->component;
	card = rtd->card;

	ret = snd_soc_card_jack_new_pins(card, "Headset", SND_JACK_HEADSET | SND_JACK_BTN_0,
					 &st_jack, st_es8316_jack_pins,
					 ARRAY_SIZE(st_es8316_jack_pins));
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}
	snd_jack_set_key(st_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	ret = snd_soc_component_set_jack(codec, &st_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static const unsigned int st_channels[] = {
	DUAL_CHANNEL,
};

static const unsigned int st_rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list st_constraints_rates = {
	.count = ARRAY_SIZE(st_rates),
	.list  = st_rates,
	.mask = 0,
};

static const struct snd_pcm_hw_constraint_list st_constraints_channels = {
	.count = ARRAY_SIZE(st_channels),
	.list = st_channels,
	.mask = 0,
};

static int st_es8336_codec_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_card *card;
	struct acp_platform_info *machine;
	struct snd_soc_dai *codec_dai;
	int ret;

	runtime = substream->runtime;
	rtd = asoc_substream_to_rtd(substream);
	card = rtd->card;
	machine = snd_soc_card_get_drvdata(card);
	codec_dai = asoc_rtd_to_codec(rtd, 0);
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, ES8336_PLL_FREQ, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}
	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &st_constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &st_constraints_rates);

	machine->play_i2s_instance = I2S_MICSP_INSTANCE;
	machine->cap_i2s_instance = I2S_MICSP_INSTANCE;
	machine->capture_channel = CAP_CHANNEL0;
	return 0;
}

static const struct snd_soc_ops st_es8336_ops = {
	.startup = st_es8336_codec_startup,
};

SND_SOC_DAILINK_DEF(designware1,
		    DAILINK_COMP_ARRAY(COMP_CPU("designware-i2s.2.auto")));
SND_SOC_DAILINK_DEF(codec,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-ESSX8336:00", "ES8316 HiFi")));
SND_SOC_DAILINK_DEF(platform,
		    DAILINK_COMP_ARRAY(COMP_PLATFORM("acp_audio_dma.1.auto")));

static struct snd_soc_dai_link st_dai_es8336[] = {
	{
		.name = "amdes8336",
		.stream_name = "ES8336 HiFi Play",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBP_CFP,
		.stop_dma_first = 1,
		.dpcm_capture = 1,
		.dpcm_playback = 1,
		.init = st_es8336_init,
		.ops = &st_es8336_ops,
		SND_SOC_DAILINK_REG(designware1, codec, platform),
	},
};

static const struct snd_soc_dapm_widget st_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),

	SND_SOC_DAPM_SUPPLY("Speaker Power", SND_SOC_NOPM, 0, 0,
			    sof_es8316_speaker_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
};

static const struct snd_soc_dapm_route st_audio_route[] = {
	{"Speaker", NULL, "HPOL"},
	{"Speaker", NULL, "HPOR"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"MIC1", NULL, "Headset Mic"},
	{"MIC2", NULL, "Internal Mic"},
	{"Speaker", NULL, "Speaker Power"},
};

static const struct snd_kcontrol_new st_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static const struct acpi_gpio_params pa_enable_gpio = { 0, 0, false };
static const struct acpi_gpio_mapping acpi_es8336_gpios[] = {
	{ "pa-enable-gpios", &pa_enable_gpio, 1 },
	{ }
};

static int st_es8336_late_probe(struct snd_soc_card *card)
{
	struct acpi_device *adev;
	int ret;

	adev = acpi_dev_get_first_match_dev("ESSX8336", NULL, -1);
	if (adev)
		put_device(&adev->dev);
	codec_dev = acpi_get_first_physical_node(adev);
	if (!codec_dev)
		dev_err(card->dev, "can not find codec dev\n");

	ret = devm_acpi_dev_add_driver_gpios(codec_dev, acpi_es8336_gpios);

	gpio_pa = gpiod_get_optional(codec_dev, "pa-enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpio_pa)) {
		ret = dev_err_probe(card->dev, PTR_ERR(gpio_pa),
				    "could not get pa-enable GPIO\n");
		gpiod_put(gpio_pa);
		put_device(codec_dev);
	}
	return 0;
}

static struct snd_soc_card st_card = {
	.name = "acpes8336",
	.owner = THIS_MODULE,
	.dai_link = st_dai_es8336,
	.num_links = ARRAY_SIZE(st_dai_es8336),
	.dapm_widgets = st_widgets,
	.num_dapm_widgets = ARRAY_SIZE(st_widgets),
	.dapm_routes = st_audio_route,
	.num_dapm_routes = ARRAY_SIZE(st_audio_route),
	.controls = st_mc_controls,
	.num_controls = ARRAY_SIZE(st_mc_controls),
	.late_probe = st_es8336_late_probe,
};

static int st_es8336_quirk_cb(const struct dmi_system_id *id)
{
	acp2x_machine_id = ST_JADEITE;
	return 1;
}

static const struct dmi_system_id st_es8336_quirk_table[] = {
	{
		.callback = st_es8336_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMD"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jadeite"),
		},
	},
	{
		.callback = st_es8336_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "IP3 Technology CO.,Ltd."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "ASN1D"),
		},
	},
	{
		.callback = st_es8336_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Standard"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "ASN10"),
		},
	},
	{}
};

static int st_es8336_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card;
	struct acp_platform_info *machine;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct acp_platform_info), GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	dmi_check_system(st_es8336_quirk_table);
	switch (acp2x_machine_id) {
	case ST_JADEITE:
		card = &st_card;
		st_card.dev = &pdev->dev;
		break;
	default:
		return -ENODEV;
	}

	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);
	ret = devm_snd_soc_register_card(&pdev->dev, &st_card);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "devm_snd_soc_register_card(%s) failed\n",
				     card->name);
	}
	return 0;
}

static int st_es8336_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id st_audio_acpi_match[] = {
	{"AMDI8336", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, st_audio_acpi_match);
#endif

static struct platform_driver st_mach_driver = {
	.driver = {
		.name = "st-es8316",
		.acpi_match_table = ACPI_PTR(st_audio_acpi_match),
		.pm = &snd_soc_pm_ops,
	},
	.probe = st_es8336_probe,
	.remove = st_es8336_remove,
};

module_platform_driver(st_mach_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("st-es8316 audio support");
MODULE_LICENSE("GPL v2");
