// SPDX-License-Identifier: GPL-2.0-only
/*
 *  cht-bsw-max98090.c - ASoc Machine driver for Intel Cherryview-based
 *  platforms Cherrytrail and Braswell, with max98090 & TI codec.
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author: Fang, Yang A <yang.a.fang@intel.com>
 *  This file is modified from cht_bsw_rt5645.c
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/jack.h>
#include "../../codecs/max98090.h"
#include "../atom/sst-atom-controls.h"
#include "../../codecs/ts3a227e.h"

#define CHT_PLAT_CLK_3_HZ	19200000
#define CHT_CODEC_DAI	"HiFi"

#define QUIRK_PMC_PLT_CLK_0				0x01

struct cht_mc_private {
	struct clk *mclk;
	struct snd_soc_jack jack;
	bool ts3a227e_present;
	int quirks;
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
					  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(card);
	int ret;

	/* See the comment in snd_cht_mc_probe() */
	if (ctx->quirks & QUIRK_PMC_PLT_CLK_0)
		return 0;

	codec_dai = snd_soc_card_get_codec_dai(card, CHT_CODEC_DAI);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found; Unable to set platform clock\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		ret = clk_prepare_enable(ctx->mclk);
		if (ret < 0) {
			dev_err(card->dev,
				"could not configure MCLK state");
			return ret;
		}
	} else {
		clk_disable_unprepare(ctx->mclk);
	}

	return 0;
}

static const struct snd_soc_dapm_widget cht_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route cht_audio_map[] = {
	{"IN34", NULL, "Headset Mic"},
	{"Headset Mic", NULL, "MICBIAS"},
	{"DMICL", NULL, "Int Mic"},
	{"Headphone", NULL, "HPL"},
	{"Headphone", NULL, "HPR"},
	{"Ext Spk", NULL, "SPKL"},
	{"Ext Spk", NULL, "SPKR"},
	{"HiFi Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx" },
	{"codec_in1", NULL, "ssp2 Rx" },
	{"ssp2 Rx", NULL, "HiFi Capture"},
	{"Headphone", NULL, "Platform Clock"},
	{"Headset Mic", NULL, "Platform Clock"},
	{"Int Mic", NULL, "Platform Clock"},
	{"Ext Spk", NULL, "Platform Clock"},
};

static const struct snd_kcontrol_new cht_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static int cht_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, M98090_REG_SYSTEM_CLOCK,
				     CHT_PLAT_CLK_3_HZ, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cht_ti_jack_event(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct snd_soc_jack *jack = (struct snd_soc_jack *)data;
	struct snd_soc_dapm_context *dapm = &jack->card->dapm;

	if (event & SND_JACK_MICROPHONE) {
		snd_soc_dapm_force_enable_pin(dapm, "SHDN");
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
		snd_soc_dapm_sync(dapm);
	} else {
		snd_soc_dapm_disable_pin(dapm, "MICBIAS");
		snd_soc_dapm_disable_pin(dapm, "SHDN");
		snd_soc_dapm_sync(dapm);
	}

	return 0;
}

static struct notifier_block cht_jack_nb = {
	.notifier_call = cht_ti_jack_event,
};

static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static struct snd_soc_jack_gpio hs_jack_gpios[] = {
	{
		.name		= "hp",
		.report		= SND_JACK_HEADPHONE | SND_JACK_LINEOUT,
		.debounce_time	= 200,
	},
	{
		.name		= "mic",
		.invert		= 1,
		.report		= SND_JACK_MICROPHONE,
		.debounce_time	= 200,
	},
};

static const struct acpi_gpio_params hp_gpios = { 0, 0, false };
static const struct acpi_gpio_params mic_gpios = { 1, 0, false };

static const struct acpi_gpio_mapping acpi_max98090_gpios[] = {
	{ "hp-gpios", &hp_gpios, 1 },
	{ "mic-gpios", &mic_gpios, 1 },
	{},
};

static int cht_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	int jack_type;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(runtime->card);
	struct snd_soc_jack *jack = &ctx->jack;

	if (ctx->ts3a227e_present) {
		/*
		 * The jack has already been created in the
		 * cht_max98090_headset_init() function.
		 */
		snd_soc_jack_notifier_register(jack, &cht_jack_nb);
		return 0;
	}

	jack_type = SND_JACK_HEADPHONE | SND_JACK_MICROPHONE;

	ret = snd_soc_card_jack_new(runtime->card, "Headset Jack",
				    jack_type, jack,
				    hs_jack_pins, ARRAY_SIZE(hs_jack_pins));
	if (ret) {
		dev_err(runtime->dev, "Headset Jack creation failed %d\n", ret);
		return ret;
	}

	ret = snd_soc_jack_add_gpiods(runtime->card->dev->parent, jack,
				      ARRAY_SIZE(hs_jack_gpios),
				      hs_jack_gpios);
	if (ret) {
		/*
		 * flag error but don't bail if jack detect is broken
		 * due to platform issues or bad BIOS/configuration
		 */
		dev_err(runtime->dev,
			"jack detection gpios not added, error %d\n", ret);
	}

	/* See the comment in snd_cht_mc_probe() */
	if (ctx->quirks & QUIRK_PMC_PLT_CLK_0)
		return 0;

	/*
	 * The firmware might enable the clock at
	 * boot (this information may or may not
	 * be reflected in the enable clock register).
	 * To change the rate we must disable the clock
	 * first to cover these cases. Due to common
	 * clock framework restrictions that do not allow
	 * to disable a clock that has not been enabled,
	 * we need to enable the clock first.
	 */
	ret = clk_prepare_enable(ctx->mclk);
	if (!ret)
		clk_disable_unprepare(ctx->mclk);

	ret = clk_set_rate(ctx->mclk, CHT_PLAT_CLK_3_HZ);

	if (ret)
		dev_err(runtime->dev, "unable to set MCLK rate\n");

	return ret;
}

static int cht_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret = 0;
	unsigned int fmt = 0;

	ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_cpu(rtd, 0), 0x3, 0x3, 2, 16);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set cpu_dai slot fmt: %d\n", ret);
		return ret;
	}

	fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBS_CFS;

	ret = snd_soc_dai_set_fmt(asoc_rtd_to_cpu(rtd, 0), fmt);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set cpu_dai set fmt: %d\n", ret);
		return ret;
	}

	/* The DSP will covert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 16-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S16_LE);
	return 0;
}

static int cht_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static int cht_max98090_headset_init(struct snd_soc_component *component)
{
	struct snd_soc_card *card = component->card;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_jack *jack = &ctx->jack;
	int jack_type;
	int ret;

	/*
	 * TI supports 4 butons headset detection
	 * KEY_MEDIA
	 * KEY_VOICECOMMAND
	 * KEY_VOLUMEUP
	 * KEY_VOLUMEDOWN
	 */
	jack_type = SND_JACK_HEADPHONE | SND_JACK_MICROPHONE |
		    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
		    SND_JACK_BTN_2 | SND_JACK_BTN_3;

	ret = snd_soc_card_jack_new(card, "Headset Jack", jack_type,
				    jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "Headset Jack creation failed %d\n", ret);
		return ret;
	}

	return ts3a227e_enable_jack_detect(component, jack);
}

static const struct snd_soc_ops cht_aif1_ops = {
	.startup = cht_aif1_startup,
};

static const struct snd_soc_ops cht_be_ssp2_ops = {
	.hw_params = cht_aif1_hw_params,
};

static struct snd_soc_aux_dev cht_max98090_headset_dev = {
	.dlc = COMP_AUX("i2c-104C227E:00"),
	.init = cht_max98090_headset_init,
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
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-193C9890:00", "HiFi")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("sst-mfld-platform")));

static struct snd_soc_dai_link cht_dailink[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &cht_aif1_ops,
		SND_SOC_DAILINK_REG(media, dummy, platform),
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &cht_aif1_ops,
		SND_SOC_DAILINK_REG(deepbuffer, dummy, platform),
	},
	/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBS_CFS,
		.init = cht_codec_init,
		.be_hw_params_fixup = cht_codec_fixup,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &cht_be_ssp2_ops,
		SND_SOC_DAILINK_REG(ssp2_port, ssp2_codec, platform),
	},
};

#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
/* use space before codec name to simplify card ID, and simplify driver name */
#define CARD_NAME "bytcht max98090" /* card name will be 'sof-bytcht max98090 */
#define DRIVER_NAME "SOF"
#else
#define CARD_NAME "chtmax98090"
#define DRIVER_NAME NULL /* card name will be used for driver name */
#endif

/* SoC card */
static struct snd_soc_card snd_soc_card_cht = {
	.name = CARD_NAME,
	.driver_name = DRIVER_NAME,
	.owner = THIS_MODULE,
	.dai_link = cht_dailink,
	.num_links = ARRAY_SIZE(cht_dailink),
	.aux_dev = &cht_max98090_headset_dev,
	.num_aux_devs = 1,
	.dapm_widgets = cht_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cht_dapm_widgets),
	.dapm_routes = cht_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cht_audio_map),
	.controls = cht_mc_controls,
	.num_controls = ARRAY_SIZE(cht_mc_controls),
};

static const struct dmi_system_id cht_max98090_quirk_table[] = {
	{
		/* Banjo model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Banjo"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Candy model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Candy"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Clapper model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Clapper"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Cyan model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Cyan"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Enguarde model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Enguarde"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Glimmer model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Glimmer"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Gnawty model Chromebook (Acer Chromebook CB3-111) */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Gnawty"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Heli model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Heli"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Kip model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Kip"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Ninja model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Ninja"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Orco model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Orco"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Quawks model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Quawks"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Rambi model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Rambi"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Squawks model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Squawks"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Sumo model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Sumo"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Swanky model Chromebook (Toshiba Chromebook 2) */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Swanky"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{
		/* Winky model Chromebook */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "Winky"),
		},
		.driver_data = (void *)QUIRK_PMC_PLT_CLK_0,
	},
	{}
};

static int snd_cht_mc_probe(struct platform_device *pdev)
{
	const struct dmi_system_id *dmi_id;
	struct device *dev = &pdev->dev;
	int ret_val = 0;
	struct cht_mc_private *drv;
	const char *mclk_name;
	struct snd_soc_acpi_mach *mach;
	const char *platform_name;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	dmi_id = dmi_first_match(cht_max98090_quirk_table);
	if (dmi_id)
		drv->quirks = (unsigned long)dmi_id->driver_data;

	drv->ts3a227e_present = acpi_dev_found("104C227E");
	if (!drv->ts3a227e_present) {
		/* no need probe TI jack detection chip */
		snd_soc_card_cht.aux_dev = NULL;
		snd_soc_card_cht.num_aux_devs = 0;

		ret_val = devm_acpi_dev_add_driver_gpios(dev->parent,
							 acpi_max98090_gpios);
		if (ret_val)
			dev_dbg(dev, "Unable to add GPIO mapping table\n");
	}

	/* override plaform name, if required */
	snd_soc_card_cht.dev = &pdev->dev;
	mach = pdev->dev.platform_data;
	platform_name = mach->mach_params.platform;

	ret_val = snd_soc_fixup_dai_links_platform_name(&snd_soc_card_cht,
							platform_name);
	if (ret_val)
		return ret_val;

	/* register the soc card */
	snd_soc_card_set_drvdata(&snd_soc_card_cht, drv);

	if (drv->quirks & QUIRK_PMC_PLT_CLK_0)
		mclk_name = "pmc_plt_clk_0";
	else
		mclk_name = "pmc_plt_clk_3";

	drv->mclk = devm_clk_get(&pdev->dev, mclk_name);
	if (IS_ERR(drv->mclk)) {
		dev_err(&pdev->dev,
			"Failed to get MCLK from %s: %ld\n",
			mclk_name, PTR_ERR(drv->mclk));
		return PTR_ERR(drv->mclk);
	}

	/*
	 * Boards which have the MAX98090's clk connected to clk_0 do not seem
	 * to like it if we muck with the clock. If we disable the clock when
	 * it is unused we get "max98090 i2c-193C9890:00: PLL unlocked" errors
	 * and the PLL never seems to lock again.
	 * So for these boards we enable it here once and leave it at that.
	 */
	if (drv->quirks & QUIRK_PMC_PLT_CLK_0) {
		ret_val = clk_prepare_enable(drv->mclk);
		if (ret_val < 0) {
			dev_err(&pdev->dev, "MCLK enable error: %d\n", ret_val);
			return ret_val;
		}
	}

	ret_val = devm_snd_soc_register_card(&pdev->dev, &snd_soc_card_cht);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, &snd_soc_card_cht);
	return ret_val;
}

static int snd_cht_mc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (ctx->quirks & QUIRK_PMC_PLT_CLK_0)
		clk_disable_unprepare(ctx->mclk);

	return 0;
}

static struct platform_driver snd_cht_mc_driver = {
	.driver = {
		.name = "cht-bsw-max98090",
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
		.pm = &snd_soc_pm_ops,
#endif
	},
	.probe = snd_cht_mc_probe,
	.remove = snd_cht_mc_remove,
};

module_platform_driver(snd_cht_mc_driver)

MODULE_DESCRIPTION("ASoC Intel(R) Braswell Machine driver");
MODULE_AUTHOR("Fang, Yang A <yang.a.fang@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cht-bsw-max98090");
