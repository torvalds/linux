// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation.

/*
 * Intel SOF Machine Driver with es8336 Codec
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "hda_dsp_common.h"

/* jd-inv + terminating entry */
#define MAX_NO_PROPS 2

#define SOF_ES8336_SSP_CODEC(quirk)		((quirk) & GENMASK(3, 0))
#define SOF_ES8336_SSP_CODEC_MASK		(GENMASK(3, 0))

#define SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK	BIT(4)

/* HDMI capture*/
#define SOF_SSP_HDMI_CAPTURE_PRESENT		BIT(14)
#define SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT		15
#define SOF_NO_OF_HDMI_CAPTURE_SSP_MASK		(GENMASK(16, 15))
#define SOF_NO_OF_HDMI_CAPTURE_SSP(quirk)	\
	(((quirk) << SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT) & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK)

#define SOF_HDMI_CAPTURE_1_SSP_SHIFT		7
#define SOF_HDMI_CAPTURE_1_SSP_MASK		(GENMASK(9, 7))
#define SOF_HDMI_CAPTURE_1_SSP(quirk)	\
	(((quirk) << SOF_HDMI_CAPTURE_1_SSP_SHIFT) & SOF_HDMI_CAPTURE_1_SSP_MASK)

#define SOF_HDMI_CAPTURE_2_SSP_SHIFT		10
#define SOF_HDMI_CAPTURE_2_SSP_MASK		(GENMASK(12, 10))
#define SOF_HDMI_CAPTURE_2_SSP(quirk)	\
	(((quirk) << SOF_HDMI_CAPTURE_2_SSP_SHIFT) & SOF_HDMI_CAPTURE_2_SSP_MASK)

#define SOF_ES8336_ENABLE_DMIC			BIT(5)
#define SOF_ES8336_JD_INVERTED			BIT(6)
#define SOF_ES8336_HEADPHONE_GPIO		BIT(7)
#define SOC_ES8336_HEADSET_MIC1			BIT(8)

static unsigned long quirk;

static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

struct sof_es8336_private {
	struct device *codec_dev;
	struct gpio_desc *gpio_speakers, *gpio_headphone;
	struct snd_soc_jack jack;
	struct list_head hdmi_pcm_list;
	bool speaker_en;
	struct delayed_work pcm_pop_work;
};

struct sof_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

static const struct acpi_gpio_params enable_gpio0 = { 0, 0, true };
static const struct acpi_gpio_params enable_gpio1 = { 1, 0, true };

static const struct acpi_gpio_mapping acpi_speakers_enable_gpio0[] = {
	{ "speakers-enable-gpios", &enable_gpio0, 1, ACPI_GPIO_QUIRK_ONLY_GPIOIO },
	{ }
};

static const struct acpi_gpio_mapping acpi_speakers_enable_gpio1[] = {
	{ "speakers-enable-gpios", &enable_gpio1, 1, ACPI_GPIO_QUIRK_ONLY_GPIOIO },
};

static const struct acpi_gpio_mapping acpi_enable_both_gpios[] = {
	{ "speakers-enable-gpios", &enable_gpio0, 1, ACPI_GPIO_QUIRK_ONLY_GPIOIO },
	{ "headphone-enable-gpios", &enable_gpio1, 1, ACPI_GPIO_QUIRK_ONLY_GPIOIO },
	{ }
};

static const struct acpi_gpio_mapping acpi_enable_both_gpios_rev_order[] = {
	{ "speakers-enable-gpios", &enable_gpio1, 1, ACPI_GPIO_QUIRK_ONLY_GPIOIO },
	{ "headphone-enable-gpios", &enable_gpio0, 1, ACPI_GPIO_QUIRK_ONLY_GPIOIO },
	{ }
};

static void log_quirks(struct device *dev)
{
	dev_info(dev, "quirk mask %#lx\n", quirk);
	dev_info(dev, "quirk SSP%ld\n",  SOF_ES8336_SSP_CODEC(quirk));
	if (quirk & SOF_ES8336_ENABLE_DMIC)
		dev_info(dev, "quirk DMIC enabled\n");
	if (quirk & SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK)
		dev_info(dev, "Speakers GPIO1 quirk enabled\n");
	if (quirk & SOF_ES8336_HEADPHONE_GPIO)
		dev_info(dev, "quirk headphone GPIO enabled\n");
	if (quirk & SOF_ES8336_JD_INVERTED)
		dev_info(dev, "quirk JD inverted enabled\n");
	if (quirk & SOC_ES8336_HEADSET_MIC1)
		dev_info(dev, "quirk headset at mic1 port enabled\n");
}

static void pcm_pop_work_events(struct work_struct *work)
{
	struct sof_es8336_private *priv =
		container_of(work, struct sof_es8336_private, pcm_pop_work.work);

	gpiod_set_value_cansleep(priv->gpio_speakers, priv->speaker_en);

	if (quirk & SOF_ES8336_HEADPHONE_GPIO)
		gpiod_set_value_cansleep(priv->gpio_headphone, priv->speaker_en);

}

static int sof_8336_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(card);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		if (priv->speaker_en == false)
			if (substream->stream == 0) {
				cancel_delayed_work(&priv->pcm_pop_work);
				gpiod_set_value_cansleep(priv->gpio_speakers, true);
			}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sof_es8316_speaker_power_event(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(card);

	if (priv->speaker_en == !SND_SOC_DAPM_EVENT_ON(event))
		return 0;

	priv->speaker_en = !SND_SOC_DAPM_EVENT_ON(event);

	queue_delayed_work(system_wq, &priv->pcm_pop_work, msecs_to_jiffies(70));
	return 0;
}

static const struct snd_soc_dapm_widget sof_es8316_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),

	SND_SOC_DAPM_SUPPLY("Speaker Power", SND_SOC_NOPM, 0, 0,
			    sof_es8316_speaker_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
};

static const struct snd_soc_dapm_widget dmic_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route sof_es8316_audio_map[] = {
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},

	/*
	 * There is no separate speaker output instead the speakers are muxed to
	 * the HP outputs. The mux is controlled Speaker and/or headphone switch.
	 */
	{"Speaker", NULL, "HPOL"},
	{"Speaker", NULL, "HPOR"},
	{"Speaker", NULL, "Speaker Power"},
};

static const struct snd_soc_dapm_route sof_es8316_headset_mic2_map[] = {
	{"MIC1", NULL, "Internal Mic"},
	{"MIC2", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route sof_es8316_headset_mic1_map[] = {
	{"MIC2", NULL, "Internal Mic"},
	{"MIC1", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route dmic_map[] = {
	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
};

static const struct snd_kcontrol_new sof_es8316_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static struct snd_soc_jack_pin sof_es8316_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static int dmic_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, dmic_widgets,
					ARRAY_SIZE(dmic_widgets));
	if (ret) {
		dev_err(card->dev, "DMic widget addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, dmic_map,
				      ARRAY_SIZE(dmic_map));
	if (ret)
		dev_err(card->dev, "DMic map addition failed: %d\n", ret);

	return ret;
}

static int sof_hdmi_init(struct snd_soc_pcm_runtime *runtime)
{
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(runtime->card);
	struct snd_soc_dai *dai = snd_soc_rtd_to_codec(runtime, 0);
	struct sof_hdmi_pcm *pcm;

	pcm = devm_kzalloc(runtime->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	/* dai_link id is 1:1 mapped to the PCM device */
	pcm->device = runtime->dai_link->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &priv->hdmi_pcm_list);

	return 0;
}

static int sof_es8316_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_component *codec = snd_soc_rtd_to_codec(runtime, 0)->component;
	struct snd_soc_card *card = runtime->card;
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(card);
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;
	int ret;

	card->dapm.idle_bias_off = true;

	if (quirk & SOC_ES8336_HEADSET_MIC1) {
		custom_map = sof_es8316_headset_mic1_map;
		num_routes = ARRAY_SIZE(sof_es8316_headset_mic1_map);
	} else {
		custom_map = sof_es8316_headset_mic2_map;
		num_routes = ARRAY_SIZE(sof_es8316_headset_mic2_map);
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	ret = snd_soc_card_jack_new_pins(card, "Headset",
					 SND_JACK_HEADSET | SND_JACK_BTN_0,
					 &priv->jack, sof_es8316_jack_pins,
					 ARRAY_SIZE(sof_es8316_jack_pins));
	if (ret) {
		dev_err(card->dev, "jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(priv->jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);

	snd_soc_component_set_jack(codec, &priv->jack, NULL);

	return 0;
}

static void sof_es8316_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_es8336_quirk_cb(const struct dmi_system_id *id)
{
	quirk = (unsigned long)id->driver_data;

	return 1;
}

/*
 * this table should only be used to add GPIO or jack-detection quirks
 * that cannot be detected from ACPI tables. The SSP and DMIC
 * information are providing by the platform driver and are aligned
 * with the topology used.
 *
 * If the GPIO support is missing, the quirk parameter can be used to
 * enable speakers. In that case it's recommended to keep the SSP and DMIC
 * information consistent, overriding the SSP and DMIC can only be done
 * if the topology file is modified as well.
 */
static const struct dmi_system_id sof_es8336_quirk_table[] = {
	{
		.callback = sof_es8336_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IP3 tech"),
			DMI_MATCH(DMI_BOARD_NAME, "WN1"),
		},
		.driver_data = (void *)(SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK)
	},
	{
		.callback = sof_es8336_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HUAWEI"),
			DMI_MATCH(DMI_BOARD_NAME, "BOHB-WAX9-PCB-B2"),
		},
		.driver_data = (void *)(SOF_ES8336_HEADPHONE_GPIO |
					SOC_ES8336_HEADSET_MIC1)
	},
	{}
};

static int sof_es8336_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	const int sysclk = 19200000;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 1, sysclk, SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(rtd->dev, "%s, Failed to set ES8336 SYSCLK: %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

/* machine stream operations */
static const struct snd_soc_ops sof_es8336_ops = {
	.hw_params = sof_es8336_hw_params,
	.trigger = sof_8336_trigger,
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

SND_SOC_DAILINK_DEF(es8336_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-ESSX8336:00", "ES8316 HiFi")));

static struct snd_soc_dai_link_component dmic_component[] = {
	{
		.name = "dmic-codec",
		.dai_name = "dmic-hifi",
	}
};

static int sof_es8336_late_probe(struct snd_soc_card *card)
{
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(card);
	struct sof_hdmi_pcm *pcm;

	if (list_empty(&priv->hdmi_pcm_list))
		return -ENOENT;

	pcm = list_first_entry(&priv->hdmi_pcm_list, struct sof_hdmi_pcm, head);

	return hda_dsp_hdmi_build_controls(card, pcm->codec_dai->component);
}

/* SoC card */
static struct snd_soc_card sof_es8336_card = {
	.name = "essx8336", /* sof- prefix added automatically */
	.owner = THIS_MODULE,
	.dapm_widgets = sof_es8316_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sof_es8316_widgets),
	.dapm_routes = sof_es8316_audio_map,
	.num_dapm_routes = ARRAY_SIZE(sof_es8316_audio_map),
	.controls = sof_es8316_controls,
	.num_controls = ARRAY_SIZE(sof_es8316_controls),
	.fully_routed = true,
	.late_probe = sof_es8336_late_probe,
	.num_links = 1,
};

static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev,
							  int ssp_codec,
							  int dmic_be_num,
							  int hdmi_num)
{
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	struct snd_soc_dai_link_component *idisp_components;
	int hdmi_id_offset = 0;
	int id = 0;
	int i;

	links = devm_kcalloc(dev, sof_es8336_card.num_links,
			     sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	cpus = devm_kcalloc(dev, sof_es8336_card.num_links,
			    sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!links || !cpus)
		goto devm_err;

	/* codec SSP */
	links[id].name = devm_kasprintf(dev, GFP_KERNEL,
					"SSP%d-Codec", ssp_codec);
	if (!links[id].name)
		goto devm_err;

	links[id].id = id;
	links[id].codecs = es8336_codec;
	links[id].num_codecs = ARRAY_SIZE(es8336_codec);
	links[id].platforms = platform_component;
	links[id].num_platforms = ARRAY_SIZE(platform_component);
	links[id].init = sof_es8316_init;
	links[id].exit = sof_es8316_exit;
	links[id].ops = &sof_es8336_ops;
	links[id].nonatomic = true;
	links[id].dpcm_playback = 1;
	links[id].dpcm_capture = 1;
	links[id].no_pcm = 1;
	links[id].cpus = &cpus[id];
	links[id].num_cpus = 1;

	links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
						  "SSP%d Pin",
						  ssp_codec);
	if (!links[id].cpus->dai_name)
		goto devm_err;

	id++;

	/* dmic */
	if (dmic_be_num > 0) {
		/* at least we have dmic01 */
		links[id].name = "dmic01";
		links[id].cpus = &cpus[id];
		links[id].cpus->dai_name = "DMIC01 Pin";
		links[id].init = dmic_init;
		if (dmic_be_num > 1) {
			/* set up 2 BE links at most */
			links[id + 1].name = "dmic16k";
			links[id + 1].cpus = &cpus[id + 1];
			links[id + 1].cpus->dai_name = "DMIC16k Pin";
			dmic_be_num = 2;
		}
	} else {
		/* HDMI dai link starts at 3 according to current topology settings */
		hdmi_id_offset = 2;
	}

	for (i = 0; i < dmic_be_num; i++) {
		links[id].id = id;
		links[id].num_cpus = 1;
		links[id].codecs = dmic_component;
		links[id].num_codecs = ARRAY_SIZE(dmic_component);
		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].ignore_suspend = 1;
		links[id].dpcm_capture = 1;
		links[id].no_pcm = 1;

		id++;
	}

	/* HDMI */
	if (hdmi_num > 0) {
		idisp_components = devm_kcalloc(dev,
						hdmi_num,
						sizeof(struct snd_soc_dai_link_component),
						GFP_KERNEL);
		if (!idisp_components)
			goto devm_err;
	}

	for (i = 1; i <= hdmi_num; i++) {
		links[id].name = devm_kasprintf(dev, GFP_KERNEL,
						"iDisp%d", i);
		if (!links[id].name)
			goto devm_err;

		links[id].id = id + hdmi_id_offset;
		links[id].cpus = &cpus[id];
		links[id].num_cpus = 1;
		links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							  "iDisp%d Pin", i);
		if (!links[id].cpus->dai_name)
			goto devm_err;

		idisp_components[i - 1].name = "ehdaudio0D2";
		idisp_components[i - 1].dai_name = devm_kasprintf(dev,
								  GFP_KERNEL,
								  "intel-hdmi-hifi%d",
								  i);
		if (!idisp_components[i - 1].dai_name)
			goto devm_err;

		links[id].codecs = &idisp_components[i - 1];
		links[id].num_codecs = 1;
		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].init = sof_hdmi_init;
		links[id].dpcm_playback = 1;
		links[id].no_pcm = 1;

		id++;
	}

	/* HDMI-In SSP */
	if (quirk & SOF_SSP_HDMI_CAPTURE_PRESENT) {
		int num_of_hdmi_ssp = (quirk & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK) >>
				SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT;

		for (i = 1; i <= num_of_hdmi_ssp; i++) {
			int port = (i == 1 ? (quirk & SOF_HDMI_CAPTURE_1_SSP_MASK) >>
						SOF_HDMI_CAPTURE_1_SSP_SHIFT :
						(quirk & SOF_HDMI_CAPTURE_2_SSP_MASK) >>
						SOF_HDMI_CAPTURE_2_SSP_SHIFT);

			links[id].cpus = &cpus[id];
			links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
								  "SSP%d Pin", port);
			if (!links[id].cpus->dai_name)
				return NULL;
			links[id].name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-HDMI", port);
			if (!links[id].name)
				return NULL;
			links[id].id = id + hdmi_id_offset;
			links[id].codecs = &snd_soc_dummy_dlc;
			links[id].num_codecs = 1;
			links[id].platforms = platform_component;
			links[id].num_platforms = ARRAY_SIZE(platform_component);
			links[id].dpcm_capture = 1;
			links[id].no_pcm = 1;
			links[id].num_cpus = 1;
			id++;
		}
	}

	return links;

devm_err:
	return NULL;
}

static char soc_components[30];

 /* i2c-<HID>:00 with HID being 8 chars */
static char codec_name[SND_ACPI_I2C_ID_LEN];

static int sof_es8336_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	struct property_entry props[MAX_NO_PROPS] = {};
	struct sof_es8336_private *priv;
	struct fwnode_handle *fwnode;
	struct acpi_device *adev;
	struct snd_soc_dai_link *dai_links;
	struct device *codec_dev;
	const struct acpi_gpio_mapping *gpio_mapping;
	unsigned int cnt = 0;
	int dmic_be_num = 0;
	int hdmi_num = 3;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = &sof_es8336_card;
	card->dev = dev;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		quirk = (unsigned long)pdev->id_entry->driver_data;

	/* check GPIO DMI quirks */
	dmi_check_system(sof_es8336_quirk_table);

	/* Use NHLT configuration only for Non-HDMI capture use case.
	 * Because more than one SSP will be enabled for HDMI capture hence wrong codec
	 * SSP will be set.
	 */
	if (mach->tplg_quirk_mask & SND_SOC_ACPI_TPLG_INTEL_SSP_NUMBER) {
		if (!mach->mach_params.i2s_link_mask) {
			dev_warn(dev, "No I2S link information provided, using SSP0. This may need to be modified with the quirk module parameter\n");
		} else {
			/*
			 * Set configuration based on platform NHLT.
			 * In this machine driver, we can only support one SSP for the
			 * ES8336 link.
			 * In some cases multiple SSPs can be reported by NHLT, starting MSB-first
			 * seems to pick the right connection.
			 */
			unsigned long ssp;

			/* fls returns 1-based results, SSPs indices are 0-based */
			ssp = fls(mach->mach_params.i2s_link_mask) - 1;

			quirk |= ssp;
		}
	}

	if (mach->mach_params.dmic_num)
		quirk |= SOF_ES8336_ENABLE_DMIC;

	if (quirk_override != -1) {
		dev_info(dev, "Overriding quirk 0x%lx => 0x%x\n",
			 quirk, quirk_override);
		quirk = quirk_override;
	}
	log_quirks(dev);

	if (quirk & SOF_ES8336_ENABLE_DMIC)
		dmic_be_num = 2;

	/* compute number of dai links */
	sof_es8336_card.num_links = 1 + dmic_be_num + hdmi_num;

	if (quirk & SOF_SSP_HDMI_CAPTURE_PRESENT)
		sof_es8336_card.num_links += (quirk & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK) >>
				SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT;

	dai_links = sof_card_dai_links_create(dev,
					      SOF_ES8336_SSP_CODEC(quirk),
					      dmic_be_num, hdmi_num);
	if (!dai_links)
		return -ENOMEM;

	sof_es8336_card.dai_link = dai_links;

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(codec_name, sizeof(codec_name),
			 "i2c-%s", acpi_dev_name(adev));
		dai_links[0].codecs->name = codec_name;

		/* also fixup codec dai name if relevant */
		if (!strncmp(mach->id, "ESSX8326", SND_ACPI_I2C_ID_LEN))
			dai_links[0].codecs->dai_name = "ES8326 HiFi";
	} else {
		dev_err(dev, "Error cannot find '%s' dev\n", mach->id);
		return -ENOENT;
	}

	codec_dev = acpi_get_first_physical_node(adev);
	acpi_dev_put(adev);
	if (!codec_dev)
		return -EPROBE_DEFER;
	priv->codec_dev = get_device(codec_dev);

	ret = snd_soc_fixup_dai_links_platform_name(&sof_es8336_card,
						    mach->mach_params.platform);
	if (ret) {
		put_device(codec_dev);
		return ret;
	}

	if (quirk & SOF_ES8336_JD_INVERTED)
		props[cnt++] = PROPERTY_ENTRY_BOOL("everest,jack-detect-inverted");

	if (cnt) {
		fwnode = fwnode_create_software_node(props, NULL);
		if (IS_ERR(fwnode)) {
			put_device(codec_dev);
			return PTR_ERR(fwnode);
		}

		ret = device_add_software_node(codec_dev, to_software_node(fwnode));

		fwnode_handle_put(fwnode);

		if (ret) {
			put_device(codec_dev);
			return ret;
		}
	}

	/* get speaker enable GPIO */
	if (quirk & SOF_ES8336_HEADPHONE_GPIO) {
		if (quirk & SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK)
			gpio_mapping = acpi_enable_both_gpios;
		else
			gpio_mapping = acpi_enable_both_gpios_rev_order;
	} else if (quirk & SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK) {
		gpio_mapping = acpi_speakers_enable_gpio1;
	} else {
		gpio_mapping = acpi_speakers_enable_gpio0;
	}

	ret = devm_acpi_dev_add_driver_gpios(codec_dev, gpio_mapping);
	if (ret)
		dev_warn(codec_dev, "unable to add GPIO mapping table\n");

	priv->gpio_speakers = gpiod_get_optional(codec_dev, "speakers-enable", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpio_speakers)) {
		ret = dev_err_probe(dev, PTR_ERR(priv->gpio_speakers),
				    "could not get speakers-enable GPIO\n");
		goto err_put_codec;
	}

	priv->gpio_headphone = gpiod_get_optional(codec_dev, "headphone-enable", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpio_headphone)) {
		ret = dev_err_probe(dev, PTR_ERR(priv->gpio_headphone),
				    "could not get headphone-enable GPIO\n");
		goto err_put_codec;
	}

	INIT_LIST_HEAD(&priv->hdmi_pcm_list);
	INIT_DELAYED_WORK(&priv->pcm_pop_work,
				pcm_pop_work_events);
	snd_soc_card_set_drvdata(card, priv);

	if (mach->mach_params.dmic_num > 0) {
		snprintf(soc_components, sizeof(soc_components),
			 "cfg-dmics:%d", mach->mach_params.dmic_num);
		card->components = soc_components;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret) {
		gpiod_put(priv->gpio_speakers);
		dev_err(dev, "snd_soc_register_card failed: %d\n", ret);
		goto err_put_codec;
	}
	platform_set_drvdata(pdev, &sof_es8336_card);
	return 0;

err_put_codec:
	device_remove_software_node(priv->codec_dev);
	put_device(codec_dev);
	return ret;
}

static void sof_es8336_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(card);

	cancel_delayed_work_sync(&priv->pcm_pop_work);
	gpiod_put(priv->gpio_speakers);
	device_remove_software_node(priv->codec_dev);
	put_device(priv->codec_dev);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof-essx8336", /* default quirk == 0 */
	},
	{
		.name = "adl_es83x6_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_ES8336_SSP_CODEC(1) |
					SOF_NO_OF_HDMI_CAPTURE_SSP(2) |
					SOF_HDMI_CAPTURE_1_SSP(0) |
					SOF_HDMI_CAPTURE_2_SSP(2) |
					SOF_SSP_HDMI_CAPTURE_PRESENT |
					SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK |
					SOF_ES8336_JD_INVERTED),
	},
	{
		.name = "rpl_es83x6_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_ES8336_SSP_CODEC(1) |
					SOF_NO_OF_HDMI_CAPTURE_SSP(2) |
					SOF_HDMI_CAPTURE_1_SSP(0) |
					SOF_HDMI_CAPTURE_2_SSP(2) |
					SOF_SSP_HDMI_CAPTURE_PRESENT |
					SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK |
					SOF_ES8336_JD_INVERTED),
	},
	{
		.name = "mtl_es83x6_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_ES8336_SSP_CODEC(1) |
					SOF_NO_OF_HDMI_CAPTURE_SSP(2) |
					SOF_HDMI_CAPTURE_1_SSP(0) |
					SOF_HDMI_CAPTURE_2_SSP(2) |
					SOF_SSP_HDMI_CAPTURE_PRESENT |
					SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK |
					SOF_ES8336_JD_INVERTED),
	},
	{
		.name = "arl_es83x6_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_ES8336_SSP_CODEC(1) |
					SOF_NO_OF_HDMI_CAPTURE_SSP(2) |
					SOF_HDMI_CAPTURE_1_SSP(0) |
					SOF_HDMI_CAPTURE_2_SSP(2) |
					SOF_SSP_HDMI_CAPTURE_PRESENT |
					SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK |
					SOF_ES8336_JD_INVERTED),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_es8336_driver = {
	.driver = {
		.name = "sof-essx8336",
		.pm = &snd_soc_pm_ops,
	},
	.probe = sof_es8336_probe,
	.remove_new = sof_es8336_remove,
	.id_table = board_ids,
};
module_platform_driver(sof_es8336_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF + ES8336 Machine driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
