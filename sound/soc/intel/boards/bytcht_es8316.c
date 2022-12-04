// SPDX-License-Identifier: GPL-2.0-only
/*
 *  bytcht_es8316.c - ASoc Machine driver for Intel Baytrail/Cherrytrail
 *                    platforms with Everest ES8316 SoC
 *
 *  Copyright (C) 2017 Endless Mobile, Inc.
 *  Authors: David Yang <yangxiaohua@everest-semi.com>,
 *           Daniel Drake <drake@endlessm.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../atom/sst-atom-controls.h"
#include "../common/soc-intel-quirks.h"

/* jd-inv + terminating entry */
#define MAX_NO_PROPS 2

struct byt_cht_es8316_private {
	struct clk *mclk;
	struct snd_soc_jack jack;
	struct gpio_desc *speaker_en_gpio;
	bool speaker_en;
};

enum {
	BYT_CHT_ES8316_INTMIC_IN1_MAP,
	BYT_CHT_ES8316_INTMIC_IN2_MAP,
};

#define BYT_CHT_ES8316_MAP(quirk)		((quirk) & GENMASK(3, 0))
#define BYT_CHT_ES8316_SSP0			BIT(16)
#define BYT_CHT_ES8316_MONO_SPEAKER		BIT(17)
#define BYT_CHT_ES8316_JD_INVERTED		BIT(18)

static unsigned long quirk;

static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

static void log_quirks(struct device *dev)
{
	if (BYT_CHT_ES8316_MAP(quirk) == BYT_CHT_ES8316_INTMIC_IN1_MAP)
		dev_info(dev, "quirk IN1_MAP enabled");
	if (BYT_CHT_ES8316_MAP(quirk) == BYT_CHT_ES8316_INTMIC_IN2_MAP)
		dev_info(dev, "quirk IN2_MAP enabled");
	if (quirk & BYT_CHT_ES8316_SSP0)
		dev_info(dev, "quirk SSP0 enabled");
	if (quirk & BYT_CHT_ES8316_MONO_SPEAKER)
		dev_info(dev, "quirk MONO_SPEAKER enabled\n");
	if (quirk & BYT_CHT_ES8316_JD_INVERTED)
		dev_info(dev, "quirk JD_INVERTED enabled\n");
}

static int byt_cht_es8316_speaker_power_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct byt_cht_es8316_private *priv = snd_soc_card_get_drvdata(card);

	if (SND_SOC_DAPM_EVENT_ON(event))
		priv->speaker_en = true;
	else
		priv->speaker_en = false;

	gpiod_set_value_cansleep(priv->speaker_en_gpio, priv->speaker_en);

	return 0;
}

static const struct snd_soc_dapm_widget byt_cht_es8316_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),

	SND_SOC_DAPM_SUPPLY("Speaker Power", SND_SOC_NOPM, 0, 0,
			    byt_cht_es8316_speaker_power_event,
			    SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
};

static const struct snd_soc_dapm_route byt_cht_es8316_audio_map[] = {
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},

	/*
	 * There is no separate speaker output instead the speakers are muxed to
	 * the HP outputs. The mux is controlled by the "Speaker Power" supply.
	 */
	{"Speaker", NULL, "HPOL"},
	{"Speaker", NULL, "HPOR"},
	{"Speaker", NULL, "Speaker Power"},
};

static const struct snd_soc_dapm_route byt_cht_es8316_intmic_in1_map[] = {
	{"MIC1", NULL, "Internal Mic"},
	{"MIC2", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route byt_cht_es8316_intmic_in2_map[] = {
	{"MIC2", NULL, "Internal Mic"},
	{"MIC1", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route byt_cht_es8316_ssp0_map[] = {
	{"Playback", NULL, "ssp0 Tx"},
	{"ssp0 Tx", NULL, "modem_out"},
	{"modem_in", NULL, "ssp0 Rx"},
	{"ssp0 Rx", NULL, "Capture"},
};

static const struct snd_soc_dapm_route byt_cht_es8316_ssp2_map[] = {
	{"Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx" },
	{"codec_in1", NULL, "ssp2 Rx" },
	{"ssp2 Rx", NULL, "Capture"},
};

static const struct snd_kcontrol_new byt_cht_es8316_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
};

static struct snd_soc_jack_pin byt_cht_es8316_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static int byt_cht_es8316_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_component *codec = asoc_rtd_to_codec(runtime, 0)->component;
	struct snd_soc_card *card = runtime->card;
	struct byt_cht_es8316_private *priv = snd_soc_card_get_drvdata(card);
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;
	int ret;

	card->dapm.idle_bias_off = true;

	switch (BYT_CHT_ES8316_MAP(quirk)) {
	case BYT_CHT_ES8316_INTMIC_IN1_MAP:
	default:
		custom_map = byt_cht_es8316_intmic_in1_map;
		num_routes = ARRAY_SIZE(byt_cht_es8316_intmic_in1_map);
		break;
	case BYT_CHT_ES8316_INTMIC_IN2_MAP:
		custom_map = byt_cht_es8316_intmic_in2_map;
		num_routes = ARRAY_SIZE(byt_cht_es8316_intmic_in2_map);
		break;
	}
	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	if (quirk & BYT_CHT_ES8316_SSP0) {
		custom_map = byt_cht_es8316_ssp0_map;
		num_routes = ARRAY_SIZE(byt_cht_es8316_ssp0_map);
	} else {
		custom_map = byt_cht_es8316_ssp2_map;
		num_routes = ARRAY_SIZE(byt_cht_es8316_ssp2_map);
	}
	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	/*
	 * The firmware might enable the clock at boot (this information
	 * may or may not be reflected in the enable clock register).
	 * To change the rate we must disable the clock first to cover these
	 * cases. Due to common clock framework restrictions that do not allow
	 * to disable a clock that has not been enabled, we need to enable
	 * the clock first.
	 */
	ret = clk_prepare_enable(priv->mclk);
	if (!ret)
		clk_disable_unprepare(priv->mclk);

	ret = clk_set_rate(priv->mclk, 19200000);
	if (ret)
		dev_err(card->dev, "unable to set MCLK rate\n");

	ret = clk_prepare_enable(priv->mclk);
	if (ret)
		dev_err(card->dev, "unable to enable MCLK\n");

	ret = snd_soc_dai_set_sysclk(asoc_rtd_to_codec(runtime, 0), 0, 19200000,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(card->dev, "can't set codec clock %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new(card, "Headset",
				    SND_JACK_HEADSET | SND_JACK_BTN_0,
				    &priv->jack, byt_cht_es8316_jack_pins,
				    ARRAY_SIZE(byt_cht_es8316_jack_pins));
	if (ret) {
		dev_err(card->dev, "jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(priv->jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_soc_component_set_jack(codec, &priv->jack, NULL);

	return 0;
}

static int byt_cht_es8316_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret, bits;

	/* The DSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	if (quirk & BYT_CHT_ES8316_SSP0) {
		/* set SSP0 to 16-bit */
		params_set_format(params, SNDRV_PCM_FORMAT_S16_LE);
		bits = 16;
	} else {
		/* set SSP2 to 24-bit */
		params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);
		bits = 24;
	}

	/*
	 * Default mode for SSP configuration is TDM 4 slot, override config
	 * with explicit setting to I2S 2ch 24-bit. The word length is set with
	 * dai_set_tdm_slot() since there is no other API exposed
	 */
	ret = snd_soc_dai_set_fmt(asoc_rtd_to_cpu(rtd, 0),
				SND_SOC_DAIFMT_I2S     |
				SND_SOC_DAIFMT_NB_NF   |
				SND_SOC_DAIFMT_CBS_CFS
		);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_cpu(rtd, 0), 0x3, 0x3, 2, bits);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static int byt_cht_es8316_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static const struct snd_soc_ops byt_cht_es8316_aif1_ops = {
	.startup = byt_cht_es8316_aif1_startup,
};

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(media,
	DAILINK_COMP_ARRAY(COMP_CPU("media-cpu-dai")));

SND_SOC_DAILINK_DEF(deepbuffer,
	DAILINK_COMP_ARRAY(COMP_CPU("deepbuffer-cpu-dai")));

SND_SOC_DAILINK_DEF(ssp2_port,
	DAILINK_COMP_ARRAY(COMP_CPU("ssp2-port")));
SND_SOC_DAILINK_DEF(ssp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-ESSX8316:00", "ES8316 HiFi")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("sst-mfld-platform")));

static struct snd_soc_dai_link byt_cht_es8316_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &byt_cht_es8316_aif1_ops,
		SND_SOC_DAILINK_REG(media, dummy, platform),
	},

	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &byt_cht_es8316_aif1_ops,
		SND_SOC_DAILINK_REG(deepbuffer, dummy, platform),
	},

		/* back ends */
	{
		/* Only SSP2 has been tested here, so BYT-CR platforms that
		 * require SSP0 will not work.
		 */
		.name = "SSP2-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = byt_cht_es8316_codec_fixup,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = byt_cht_es8316_init,
		SND_SOC_DAILINK_REG(ssp2_port, ssp2_codec, platform),
	},
};


/* SoC card */
static char codec_name[SND_ACPI_I2C_ID_LEN];
#if !IS_ENABLED(CONFIG_SND_SOC_INTEL_USER_FRIENDLY_LONG_NAMES)
static char long_name[50]; /* = "bytcht-es8316-*-spk-*-mic" */
#endif
static char components_string[32]; /* = "cfg-spk:* cfg-mic:* */

static int byt_cht_es8316_suspend(struct snd_soc_card *card)
{
	struct snd_soc_component *component;

	for_each_card_components(card, component) {
		if (!strcmp(component->name, codec_name)) {
			dev_dbg(component->dev, "disabling jack detect before suspend\n");
			snd_soc_component_set_jack(component, NULL, NULL);
			break;
		}
	}

	return 0;
}

static int byt_cht_es8316_resume(struct snd_soc_card *card)
{
	struct byt_cht_es8316_private *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component;

	for_each_card_components(card, component) {
		if (!strcmp(component->name, codec_name)) {
			dev_dbg(component->dev, "re-enabling jack detect after resume\n");
			snd_soc_component_set_jack(component, &priv->jack, NULL);
			break;
		}
	}

	/*
	 * Some Cherry Trail boards with an ES8316 codec have a bug in their
	 * ACPI tables where the MSSL1680 touchscreen's _PS0 and _PS3 methods
	 * wrongly also set the speaker-enable GPIO to 1/0. Testing has shown
	 * that this really is a bug and the GPIO has no influence on the
	 * touchscreen at all.
	 *
	 * The silead.c touchscreen driver does not support runtime suspend, so
	 * the GPIO can only be changed underneath us during a system suspend.
	 * This resume() function runs from a pm complete() callback, and thus
	 * is guaranteed to run after the touchscreen driver/ACPI-subsys has
	 * brought the touchscreen back up again (and thus changed the GPIO).
	 *
	 * So to work around this we pass GPIOD_FLAGS_BIT_NONEXCLUSIVE when
	 * requesting the GPIO and we set its value here to undo any changes
	 * done by the touchscreen's broken _PS0 ACPI method.
	 */
	gpiod_set_value_cansleep(priv->speaker_en_gpio, priv->speaker_en);

	return 0;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
/* use space before codec name to simplify card ID, and simplify driver name */
#define CARD_NAME "bytcht es8316" /* card name will be 'sof-bytcht es8316' */
#define DRIVER_NAME "SOF"
#else
#define CARD_NAME "bytcht-es8316"
#define DRIVER_NAME NULL /* card name will be used for driver name */
#endif

static struct snd_soc_card byt_cht_es8316_card = {
	.name = CARD_NAME,
	.driver_name = DRIVER_NAME,
	.owner = THIS_MODULE,
	.dai_link = byt_cht_es8316_dais,
	.num_links = ARRAY_SIZE(byt_cht_es8316_dais),
	.dapm_widgets = byt_cht_es8316_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_cht_es8316_widgets),
	.dapm_routes = byt_cht_es8316_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_cht_es8316_audio_map),
	.controls = byt_cht_es8316_controls,
	.num_controls = ARRAY_SIZE(byt_cht_es8316_controls),
	.fully_routed = true,
	.suspend_pre = byt_cht_es8316_suspend,
	.resume_post = byt_cht_es8316_resume,
};

static const struct acpi_gpio_params first_gpio = { 0, 0, false };

static const struct acpi_gpio_mapping byt_cht_es8316_gpios[] = {
	{ "speaker-enable-gpios", &first_gpio, 1 },
	{ },
};

/* Please keep this list alphabetically sorted */
static const struct dmi_system_id byt_cht_es8316_quirk_table[] = {
	{	/* Irbis NB41 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IRBIS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "NB41"),
		},
		.driver_data = (void *)(BYT_CHT_ES8316_SSP0
					| BYT_CHT_ES8316_INTMIC_IN2_MAP
					| BYT_CHT_ES8316_JD_INVERTED),
	},
	{	/* Nanote UMPC-01 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "RWC CO.,LTD"),
			DMI_MATCH(DMI_PRODUCT_NAME, "UMPC-01"),
		},
		.driver_data = (void *)BYT_CHT_ES8316_INTMIC_IN1_MAP,
	},
	{	/* Teclast X98 Plus II */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TECLAST"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X98 Plus II"),
		},
		.driver_data = (void *)(BYT_CHT_ES8316_INTMIC_IN1_MAP
					| BYT_CHT_ES8316_JD_INVERTED),
	},
	{}
};

static int snd_byt_cht_es8316_mc_probe(struct platform_device *pdev)
{
	static const char * const mic_name[] = { "in1", "in2" };
	struct property_entry props[MAX_NO_PROPS] = {};
	struct byt_cht_es8316_private *priv;
	const struct dmi_system_id *dmi_id;
	struct device *dev = &pdev->dev;
	struct snd_soc_acpi_mach *mach;
	const char *platform_name;
	struct acpi_device *adev;
	struct device *codec_dev;
	unsigned int cnt = 0;
	int dai_index = 0;
	int i;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mach = dev->platform_data;
	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(byt_cht_es8316_dais); i++) {
		if (!strcmp(byt_cht_es8316_dais[i].codecs->name,
			    "i2c-ESSX8316:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(codec_name, sizeof(codec_name),
			 "i2c-%s", acpi_dev_name(adev));
		put_device(&adev->dev);
		byt_cht_es8316_dais[dai_index].codecs->name = codec_name;
	}

	/* override plaform name, if required */
	byt_cht_es8316_card.dev = dev;
	platform_name = mach->mach_params.platform;

	ret = snd_soc_fixup_dai_links_platform_name(&byt_cht_es8316_card,
						    platform_name);
	if (ret)
		return ret;

	/* Check for BYTCR or other platform and setup quirks */
	dmi_id = dmi_first_match(byt_cht_es8316_quirk_table);
	if (dmi_id) {
		quirk = (unsigned long)dmi_id->driver_data;
	} else if (soc_intel_is_byt() &&
		   mach->mach_params.acpi_ipc_irq_index == 0) {
		/* On BYTCR default to SSP0, internal-mic-in2-map, mono-spk */
		quirk = BYT_CHT_ES8316_SSP0 | BYT_CHT_ES8316_INTMIC_IN2_MAP |
			BYT_CHT_ES8316_MONO_SPEAKER;
	} else {
		/* Others default to internal-mic-in1-map, mono-speaker */
		quirk = BYT_CHT_ES8316_INTMIC_IN1_MAP |
			BYT_CHT_ES8316_MONO_SPEAKER;
	}
	if (quirk_override != -1) {
		dev_info(dev, "Overriding quirk 0x%lx => 0x%x\n",
			 quirk, quirk_override);
		quirk = quirk_override;
	}
	log_quirks(dev);

	if (quirk & BYT_CHT_ES8316_SSP0)
		byt_cht_es8316_dais[dai_index].cpus->dai_name = "ssp0-port";

	/* get the clock */
	priv->mclk = devm_clk_get(dev, "pmc_plt_clk_3");
	if (IS_ERR(priv->mclk)) {
		ret = PTR_ERR(priv->mclk);
		dev_err(dev, "clk_get pmc_plt_clk_3 failed: %d\n", ret);
		return ret;
	}

	/* get speaker enable GPIO */
	codec_dev = bus_find_device_by_name(&i2c_bus_type, NULL, codec_name);
	if (!codec_dev)
		return -EPROBE_DEFER;

	if (quirk & BYT_CHT_ES8316_JD_INVERTED)
		props[cnt++] = PROPERTY_ENTRY_BOOL("everest,jack-detect-inverted");

	if (cnt) {
		ret = device_add_properties(codec_dev, props);
		if (ret) {
			put_device(codec_dev);
			return ret;
		}
	}

	devm_acpi_dev_add_driver_gpios(codec_dev, byt_cht_es8316_gpios);
	priv->speaker_en_gpio =
		gpiod_get_index(codec_dev, "speaker-enable", 0,
				/* see comment in byt_cht_es8316_resume */
				GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_NONEXCLUSIVE);
	put_device(codec_dev);

	if (IS_ERR(priv->speaker_en_gpio)) {
		ret = PTR_ERR(priv->speaker_en_gpio);
		switch (ret) {
		case -ENOENT:
			priv->speaker_en_gpio = NULL;
			break;
		default:
			dev_err(dev, "get speaker GPIO failed: %d\n", ret);
			fallthrough;
		case -EPROBE_DEFER:
			return ret;
		}
	}

	snprintf(components_string, sizeof(components_string),
		 "cfg-spk:%s cfg-mic:%s",
		 (quirk & BYT_CHT_ES8316_MONO_SPEAKER) ? "1" : "2",
		 mic_name[BYT_CHT_ES8316_MAP(quirk)]);
	byt_cht_es8316_card.components = components_string;
#if !IS_ENABLED(CONFIG_SND_SOC_INTEL_USER_FRIENDLY_LONG_NAMES)
	snprintf(long_name, sizeof(long_name), "bytcht-es8316-%s-spk-%s-mic",
		 (quirk & BYT_CHT_ES8316_MONO_SPEAKER) ? "mono" : "stereo",
		 mic_name[BYT_CHT_ES8316_MAP(quirk)]);
	byt_cht_es8316_card.long_name = long_name;
#endif

	/* register the soc card */
	snd_soc_card_set_drvdata(&byt_cht_es8316_card, priv);

	ret = devm_snd_soc_register_card(dev, &byt_cht_es8316_card);
	if (ret) {
		gpiod_put(priv->speaker_en_gpio);
		dev_err(dev, "snd_soc_register_card failed: %d\n", ret);
		return ret;
	}
	platform_set_drvdata(pdev, &byt_cht_es8316_card);
	return 0;
}

static int snd_byt_cht_es8316_mc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct byt_cht_es8316_private *priv = snd_soc_card_get_drvdata(card);

	gpiod_put(priv->speaker_en_gpio);
	return 0;
}

static struct platform_driver snd_byt_cht_es8316_mc_driver = {
	.driver = {
		.name = "bytcht_es8316",
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
		.pm = &snd_soc_pm_ops,
#endif
	},
	.probe = snd_byt_cht_es8316_mc_probe,
	.remove = snd_byt_cht_es8316_mc_remove,
};

module_platform_driver(snd_byt_cht_es8316_mc_driver);
MODULE_DESCRIPTION("ASoC Intel(R) Baytrail/Cherrytrail Machine driver");
MODULE_AUTHOR("David Yang <yangxiaohua@everest-semi.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcht_es8316");
