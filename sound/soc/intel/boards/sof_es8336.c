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
};

struct sof_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

static const struct acpi_gpio_params enable_gpio0 = { 0, 0, true };
static const struct acpi_gpio_params enable_gpio1 = { 1, 0, true };

static const struct acpi_gpio_mapping acpi_speakers_enable_gpio0[] = {
	{ "speakers-enable-gpios", &enable_gpio0, 1 },
	{ }
};

static const struct acpi_gpio_mapping acpi_speakers_enable_gpio1[] = {
	{ "speakers-enable-gpios", &enable_gpio1, 1 },
};

static const struct acpi_gpio_mapping acpi_enable_both_gpios[] = {
	{ "speakers-enable-gpios", &enable_gpio0, 1 },
	{ "headphone-enable-gpios", &enable_gpio1, 1 },
	{ }
};

static const struct acpi_gpio_mapping acpi_enable_both_gpios_rev_order[] = {
	{ "speakers-enable-gpios", &enable_gpio1, 1 },
	{ "headphone-enable-gpios", &enable_gpio0, 1 },
	{ }
};

static const struct acpi_gpio_mapping *gpio_mapping = acpi_speakers_enable_gpio0;

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

static int sof_es8316_speaker_power_event(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(card);

	if (priv->speaker_en == !SND_SOC_DAPM_EVENT_ON(event))
		return 0;

	priv->speaker_en = !SND_SOC_DAPM_EVENT_ON(event);

	if (SND_SOC_DAPM_EVENT_ON(event))
		msleep(70);

	gpiod_set_value_cansleep(priv->gpio_speakers, priv->speaker_en);

	if (!(quirk & SOF_ES8336_HEADPHONE_GPIO))
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		msleep(70);

	gpiod_set_value_cansleep(priv->gpio_headphone, priv->speaker_en);

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
	struct snd_soc_dai *dai = asoc_rtd_to_codec(runtime, 0);
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
	struct snd_soc_component *codec = asoc_rtd_to_codec(runtime, 0)->component;
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

	ret = snd_soc_card_jack_new(card, "Headset",
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
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_es8336_quirk_cb(const struct dmi_system_id *id)
{
	quirk = (unsigned long)id->driver_data;

	if (quirk & SOF_ES8336_HEADPHONE_GPIO) {
		if (quirk & SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK)
			gpio_mapping = acpi_enable_both_gpios;
		else
			gpio_mapping = acpi_enable_both_gpios_rev_order;
	} else if (quirk & SOF_ES8336_SPEAKERS_EN_GPIO1_QUIRK) {
		gpio_mapping = acpi_speakers_enable_gpio1;
	}

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
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
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
static struct snd_soc_ops sof_es8336_ops = {
	.hw_params = sof_es8336_hw_params,
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
		idisp_components = devm_kzalloc(dev,
						sizeof(struct snd_soc_dai_link_component) *
						hdmi_num, GFP_KERNEL);
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
	unsigned int cnt = 0;
	int dmic_be_num = 0;
	int hdmi_num = 3;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	card = &sof_es8336_card;
	card->dev = dev;

	/* check GPIO DMI quirks */
	dmi_check_system(sof_es8336_quirk_table);

	if (!mach->mach_params.i2s_link_mask) {
		dev_warn(dev, "No I2S link information provided, using SSP0. This may need to be modified with the quirk module parameter\n");
	} else {
		/*
		 * Set configuration based on platform NHLT.
		 * In this machine driver, we can only support one SSP for the
		 * ES8336 link, the else-if below are intentional.
		 * In some cases multiple SSPs can be reported by NHLT, starting MSB-first
		 * seems to pick the right connection.
		 */
		unsigned long ssp = 0;

		if (mach->mach_params.i2s_link_mask & BIT(2))
			ssp = SOF_ES8336_SSP_CODEC(2);
		else if (mach->mach_params.i2s_link_mask & BIT(1))
			ssp = SOF_ES8336_SSP_CODEC(1);
		else  if (mach->mach_params.i2s_link_mask & BIT(0))
			ssp = SOF_ES8336_SSP_CODEC(0);

		quirk |= ssp;
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

	sof_es8336_card.num_links += dmic_be_num + hdmi_num;
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
		put_device(&adev->dev);
		dai_links[0].codecs->name = codec_name;

		/* also fixup codec dai name if relevant */
		if (!strncmp(mach->id, "ESSX8326", SND_ACPI_I2C_ID_LEN))
			dai_links[0].codecs->dai_name = "ES8326 HiFi";
	} else {
		dev_err(dev, "Error cannot find '%s' dev\n", mach->id);
		return -ENXIO;
	}

	ret = snd_soc_fixup_dai_links_platform_name(&sof_es8336_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	codec_dev = acpi_get_first_physical_node(adev);
	if (!codec_dev)
		return -EPROBE_DEFER;
	priv->codec_dev = get_device(codec_dev);

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

static int sof_es8336_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct sof_es8336_private *priv = snd_soc_card_get_drvdata(card);

	gpiod_put(priv->gpio_speakers);
	device_remove_software_node(priv->codec_dev);
	put_device(priv->codec_dev);

	return 0;
}

static struct platform_driver sof_es8336_driver = {
	.driver = {
		.name = "sof-essx8336",
		.pm = &snd_soc_pm_ops,
	},
	.probe = sof_es8336_probe,
	.remove = sof_es8336_remove,
};
module_platform_driver(sof_es8336_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF + ES8336 Machine driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sof-essx8336");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
