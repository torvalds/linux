// SPDX-License-Identifier: GPL-2.0+
//
// Machine driver for AMD ACP Audio engine using ES8336 codec.
//
// Copyright 2023 Marian Postevca <posteuca@mutex.one>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <sound/soc-acpi.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include "../acp-mach.h"
#include "acp3x-es83xx.h"

#define get_mach_priv(card) ((struct acp3x_es83xx_private *)((acp_get_drvdata(card))->mach_priv))

#define DUAL_CHANNEL	2

#define ES83XX_ENABLE_DMIC	BIT(4)
#define ES83XX_48_MHZ_MCLK	BIT(5)

struct acp3x_es83xx_private {
	bool speaker_on;
	bool headphone_on;
	unsigned long quirk;
	struct snd_soc_component *codec;
	struct device *codec_dev;
	struct gpio_desc *gpio_speakers, *gpio_headphone;
	struct acpi_gpio_params enable_spk_gpio, enable_hp_gpio;
	struct acpi_gpio_mapping gpio_mapping[3];
	struct snd_soc_dapm_route mic_map[2];
};

static const unsigned int channels[] = {
	DUAL_CHANNEL,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

#define ES83xx_12288_KHZ_MCLK_FREQ   (48000 * 256)
#define ES83xx_48_MHZ_MCLK_FREQ      (48000 * 1000)

static int acp3x_es83xx_headphone_power_event(struct snd_soc_dapm_widget *w,
					    struct snd_kcontrol *kcontrol, int event);
static int acp3x_es83xx_speaker_power_event(struct snd_soc_dapm_widget *w,
					    struct snd_kcontrol *kcontrol, int event);

static int acp3x_es83xx_codec_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;
	struct acp3x_es83xx_private *priv;
	unsigned int freq;
	int ret;

	runtime = substream->runtime;
	rtd = snd_soc_substream_to_rtd(substream);
	codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	priv = get_mach_priv(rtd->card);

	if (priv->quirk & ES83XX_48_MHZ_MCLK) {
		dev_dbg(priv->codec_dev, "using a 48Mhz MCLK\n");
		freq = ES83xx_48_MHZ_MCLK_FREQ;
	} else {
		dev_dbg(priv->codec_dev, "using a 12.288Mhz MCLK\n");
		freq = ES83xx_12288_KHZ_MCLK_FREQ;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, freq, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);

	return 0;
}

static struct snd_soc_jack es83xx_jack;

static struct snd_soc_jack_pin es83xx_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static const struct snd_soc_dapm_widget acp3x_es83xx_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),

	SND_SOC_DAPM_SUPPLY("Headphone Power", SND_SOC_NOPM, 0, 0,
			    acp3x_es83xx_headphone_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("Speaker Power", SND_SOC_NOPM, 0, 0,
			    acp3x_es83xx_speaker_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
};

static const struct snd_soc_dapm_route acp3x_es83xx_audio_map[] = {
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Headphone", NULL, "Headphone Power"},

	/*
	 * There is no separate speaker output instead the speakers are muxed to
	 * the HP outputs. The mux is controlled Speaker and/or headphone switch.
	 */
	{"Speaker", NULL, "HPOL"},
	{"Speaker", NULL, "HPOR"},
	{"Speaker", NULL, "Speaker Power"},
};


static const struct snd_kcontrol_new acp3x_es83xx_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static int acp3x_es83xx_configure_widgets(struct snd_soc_card *card)
{
	card->dapm_widgets = acp3x_es83xx_widgets;
	card->num_dapm_widgets = ARRAY_SIZE(acp3x_es83xx_widgets);
	card->controls = acp3x_es83xx_controls;
	card->num_controls = ARRAY_SIZE(acp3x_es83xx_controls);
	card->dapm_routes = acp3x_es83xx_audio_map;
	card->num_dapm_routes = ARRAY_SIZE(acp3x_es83xx_audio_map);

	return 0;
}

static int acp3x_es83xx_headphone_power_event(struct snd_soc_dapm_widget *w,
					      struct snd_kcontrol *kcontrol, int event)
{
	struct acp3x_es83xx_private *priv = get_mach_priv(w->dapm->card);

	dev_dbg(priv->codec_dev, "headphone power event = %d\n", event);
	if (SND_SOC_DAPM_EVENT_ON(event))
		priv->headphone_on = true;
	else
		priv->headphone_on = false;

	gpiod_set_value_cansleep(priv->gpio_speakers, priv->speaker_on);
	gpiod_set_value_cansleep(priv->gpio_headphone, priv->headphone_on);

	return 0;
}

static int acp3x_es83xx_speaker_power_event(struct snd_soc_dapm_widget *w,
					    struct snd_kcontrol *kcontrol, int event)
{
	struct acp3x_es83xx_private *priv = get_mach_priv(w->dapm->card);

	dev_dbg(priv->codec_dev, "speaker power event: %d\n", event);
	if (SND_SOC_DAPM_EVENT_ON(event))
		priv->speaker_on = true;
	else
		priv->speaker_on = false;

	gpiod_set_value_cansleep(priv->gpio_speakers, priv->speaker_on);
	gpiod_set_value_cansleep(priv->gpio_headphone, priv->headphone_on);

	return 0;
}

static int acp3x_es83xx_suspend_pre(struct snd_soc_card *card)
{
	struct acp3x_es83xx_private *priv = get_mach_priv(card);

	/* We need to disable the jack in the machine driver suspend
	 * callback so that the CODEC suspend callback actually gets
	 * called. Without doing it, the CODEC suspend/resume
	 * callbacks do not get called if headphones are plugged in.
	 * This is because plugging in headphones keeps some supplies
	 * active, this in turn means that the lowest bias level
	 * that the CODEC can go to is SND_SOC_BIAS_STANDBY.
	 * If components do not set idle_bias_on to true then
	 * their suspend/resume callbacks do not get called.
	 */
	dev_dbg(priv->codec_dev, "card suspend\n");
	snd_soc_component_set_jack(priv->codec, NULL, NULL);
	return 0;
}

static int acp3x_es83xx_resume_post(struct snd_soc_card *card)
{
	struct acp3x_es83xx_private *priv = get_mach_priv(card);

	/* We disabled jack detection in suspend callback,
	 * enable it back.
	 */
	dev_dbg(priv->codec_dev, "card resume\n");
	snd_soc_component_set_jack(priv->codec, &es83xx_jack, NULL);
	return 0;
}

static int acp3x_es83xx_configure_gpios(struct acp3x_es83xx_private *priv)
{
	int ret = 0;

	priv->enable_spk_gpio.crs_entry_index = 0;
	priv->enable_hp_gpio.crs_entry_index = 1;

	priv->enable_spk_gpio.active_low = false;
	priv->enable_hp_gpio.active_low = false;

	priv->gpio_mapping[0].name = "speakers-enable-gpios";
	priv->gpio_mapping[0].data = &priv->enable_spk_gpio;
	priv->gpio_mapping[0].size = 1;
	priv->gpio_mapping[0].quirks = ACPI_GPIO_QUIRK_ONLY_GPIOIO;

	priv->gpio_mapping[1].name = "headphone-enable-gpios";
	priv->gpio_mapping[1].data = &priv->enable_hp_gpio;
	priv->gpio_mapping[1].size = 1;
	priv->gpio_mapping[1].quirks = ACPI_GPIO_QUIRK_ONLY_GPIOIO;

	dev_info(priv->codec_dev, "speaker gpio %d active %s, headphone gpio %d active %s\n",
		 priv->enable_spk_gpio.crs_entry_index,
		 priv->enable_spk_gpio.active_low ? "low" : "high",
		 priv->enable_hp_gpio.crs_entry_index,
		 priv->enable_hp_gpio.active_low ? "low" : "high");
	return ret;
}

static int acp3x_es83xx_configure_mics(struct acp3x_es83xx_private *priv)
{
	int num_routes = 0;
	int i;

	if (!(priv->quirk & ES83XX_ENABLE_DMIC)) {
		priv->mic_map[num_routes].sink = "MIC1";
		priv->mic_map[num_routes].source = "Internal Mic";
		num_routes++;
	}

	priv->mic_map[num_routes].sink = "MIC2";
	priv->mic_map[num_routes].source = "Headset Mic";
	num_routes++;

	for (i = 0; i < num_routes; i++)
		dev_info(priv->codec_dev, "%s is %s\n",
			 priv->mic_map[i].source, priv->mic_map[i].sink);

	return num_routes;
}

static int acp3x_es83xx_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_component *codec = snd_soc_rtd_to_codec(runtime, 0)->component;
	struct snd_soc_card *card = runtime->card;
	struct acp3x_es83xx_private *priv = get_mach_priv(card);
	int ret = 0;
	int num_routes;

	ret = snd_soc_card_jack_new_pins(card, "Headset",
					 SND_JACK_HEADSET | SND_JACK_BTN_0,
					 &es83xx_jack, es83xx_jack_pins,
					 ARRAY_SIZE(es83xx_jack_pins));
	if (ret) {
		dev_err(card->dev, "jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(es83xx_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);

	snd_soc_component_set_jack(codec, &es83xx_jack, NULL);

	priv->codec = codec;
	acp3x_es83xx_configure_gpios(priv);

	ret = devm_acpi_dev_add_driver_gpios(priv->codec_dev, priv->gpio_mapping);
	if (ret)
		dev_warn(priv->codec_dev, "failed to add speaker gpio\n");

	priv->gpio_speakers = gpiod_get_optional(priv->codec_dev, "speakers-enable",
				priv->enable_spk_gpio.active_low ? GPIOD_OUT_LOW : GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpio_speakers)) {
		dev_err(priv->codec_dev, "could not get speakers-enable GPIO\n");
		return PTR_ERR(priv->gpio_speakers);
	}

	priv->gpio_headphone = gpiod_get_optional(priv->codec_dev, "headphone-enable",
				priv->enable_hp_gpio.active_low ? GPIOD_OUT_LOW : GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpio_headphone)) {
		dev_err(priv->codec_dev, "could not get headphone-enable GPIO\n");
		return PTR_ERR(priv->gpio_headphone);
	}

	num_routes = acp3x_es83xx_configure_mics(priv);
	if (num_routes > 0) {
		ret = snd_soc_dapm_add_routes(&card->dapm, priv->mic_map, num_routes);
		if (ret != 0)
			device_remove_software_node(priv->codec_dev);
	}

	return ret;
}

static const struct snd_soc_ops acp3x_es83xx_ops = {
	.startup = acp3x_es83xx_codec_startup,
};


SND_SOC_DAILINK_DEF(codec,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-ESSX8336:00", "ES8316 HiFi")));

static const struct dmi_system_id acp3x_es83xx_dmi_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "HUAWEI"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "KLVL-WXXW"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "M1010"),
		},
		.driver_data = (void *)(ES83XX_ENABLE_DMIC),
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "HUAWEI"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "KLVL-WXX9"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "M1010"),
		},
		.driver_data = (void *)(ES83XX_ENABLE_DMIC),
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "HUAWEI"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "BOM-WXX9"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "M1010"),
		},
		.driver_data = (void *)(ES83XX_ENABLE_DMIC|ES83XX_48_MHZ_MCLK),
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "HUAWEI"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "HVY-WXX9"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "M1020"),
		},
		.driver_data = (void *)(ES83XX_ENABLE_DMIC),
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "HUAWEI"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "HVY-WXX9"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "M1040"),
		},
		.driver_data = (void *)(ES83XX_ENABLE_DMIC),
	},
	{}
};

static int acp3x_es83xx_configure_link(struct snd_soc_card *card, struct snd_soc_dai_link *link)
{
	link->codecs = codec;
	link->num_codecs = ARRAY_SIZE(codec);
	link->init = acp3x_es83xx_init;
	link->ops = &acp3x_es83xx_ops;
	link->dai_fmt = SND_SOC_DAIFMT_I2S
		| SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBP_CFP;

	return 0;
}

static int acp3x_es83xx_probe(struct snd_soc_card *card)
{
	int ret = 0;
	struct device *dev = card->dev;
	const struct dmi_system_id *dmi_id;

	dmi_id = dmi_first_match(acp3x_es83xx_dmi_table);
	if (dmi_id && dmi_id->driver_data) {
		struct acp3x_es83xx_private *priv;
		struct acp_card_drvdata *acp_drvdata;
		struct acpi_device *adev;
		struct device *codec_dev;

		acp_drvdata = (struct acp_card_drvdata *)card->drvdata;

		dev_info(dev, "matched DMI table with this system, trying to register sound card\n");

		adev = acpi_dev_get_first_match_dev(acp_drvdata->acpi_mach->id, NULL, -1);
		if (!adev) {
			dev_err(dev, "Error cannot find '%s' dev\n", acp_drvdata->acpi_mach->id);
			return -ENXIO;
		}

		codec_dev = acpi_get_first_physical_node(adev);
		acpi_dev_put(adev);
		if (!codec_dev) {
			dev_warn(dev, "Error cannot find codec device, will defer probe\n");
			return -EPROBE_DEFER;
		}

		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv) {
			put_device(codec_dev);
			return -ENOMEM;
		}

		priv->codec_dev = codec_dev;
		priv->quirk = (unsigned long)dmi_id->driver_data;
		acp_drvdata->mach_priv = priv;
		dev_info(dev, "successfully probed the sound card\n");
	} else {
		ret = -ENODEV;
		dev_warn(dev, "this system has a ES83xx codec defined in ACPI, but the driver doesn't have this system registered in DMI table\n");
	}
	return ret;
}


void acp3x_es83xx_init_ops(struct acp_mach_ops *ops)
{
	ops->probe = acp3x_es83xx_probe;
	ops->configure_widgets = acp3x_es83xx_configure_widgets;
	ops->configure_link = acp3x_es83xx_configure_link;
	ops->suspend_pre = acp3x_es83xx_suspend_pre;
	ops->resume_post = acp3x_es83xx_resume_post;
}
