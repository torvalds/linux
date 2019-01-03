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
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/platform_sst_audio.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../atom/sst-atom-controls.h"
#include "../common/sst-dsp.h"

struct byt_cht_es8316_private {
	struct clk *mclk;
	struct snd_soc_jack jack;
};

#define BYT_CHT_ES8316_SSP0			BIT(16)

static int quirk;

static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

static void log_quirks(struct device *dev)
{
	if (quirk & BYT_CHT_ES8316_SSP0)
		dev_info(dev, "quirk SSP0 enabled");
}

static const struct snd_soc_dapm_widget byt_cht_es8316_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),

	/*
	 * The codec supports two analog microphone inputs. I have only
	 * tested MIC1. A DMIC route could also potentially be added
	 * if such functionality is found on another platform.
	 */
	SND_SOC_DAPM_MIC("Microphone 1", NULL),
	SND_SOC_DAPM_MIC("Microphone 2", NULL),
};

static const struct snd_soc_dapm_route byt_cht_es8316_audio_map[] = {
	{"MIC1", NULL, "Microphone 1"},
	{"MIC2", NULL, "Microphone 2"},
	{"MIC1", NULL, "Headset Mic"},

	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
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
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Microphone 1"),
	SOC_DAPM_PIN_SWITCH("Microphone 2"),
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
	struct snd_soc_component *codec = runtime->codec_dai->component;
	struct snd_soc_card *card = runtime->card;
	struct byt_cht_es8316_private *priv = snd_soc_card_get_drvdata(card);
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;
	int ret;

	card->dapm.idle_bias_off = true;

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

	ret = snd_soc_dai_set_sysclk(runtime->codec_dai, 0, 19200000,
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

static const struct snd_soc_pcm_stream byt_cht_es8316_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

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
	ret = snd_soc_dai_set_fmt(rtd->cpu_dai,
				SND_SOC_DAIFMT_I2S     |
				SND_SOC_DAIFMT_NB_NF   |
				SND_SOC_DAIFMT_CBS_CFS
		);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x3, 0x3, 2, bits);
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

static struct snd_soc_dai_link byt_cht_es8316_dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &byt_cht_es8316_aif1_ops,
	},

	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.cpu_dai_name = "deepbuffer-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &byt_cht_es8316_aif1_ops,
	},

		/* back ends */
	{
		/* Only SSP2 has been tested here, so BYT-CR platforms that
		 * require SSP0 will not work.
		 */
		.name = "SSP2-Codec",
		.id = 0,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "ES8316 HiFi",
		.codec_name = "i2c-ESSX8316:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = byt_cht_es8316_codec_fixup,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = byt_cht_es8316_init,
	},
};


/* SoC card */
static char codec_name[SND_ACPI_I2C_ID_LEN];

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

	return 0;
}

static struct snd_soc_card byt_cht_es8316_card = {
	.name = "bytcht-es8316",
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

static const struct x86_cpu_id baytrail_cpu_ids[] = {
	{ X86_VENDOR_INTEL, 6, INTEL_FAM6_ATOM_SILVERMONT }, /* Valleyview */
	{}
};

static int snd_byt_cht_es8316_mc_probe(struct platform_device *pdev)
{
	struct byt_cht_es8316_private *priv;
	struct device *dev = &pdev->dev;
	struct snd_soc_acpi_mach *mach;
	const char *i2c_name = NULL;
	int dai_index = 0;
	int i;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mach = dev->platform_data;
	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(byt_cht_es8316_dais); i++) {
		if (!strcmp(byt_cht_es8316_dais[i].codec_name,
			    "i2c-ESSX8316:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	i2c_name = acpi_dev_get_first_match_name(mach->id, NULL, -1);
	if (i2c_name) {
		snprintf(codec_name, sizeof(codec_name),
			"%s%s", "i2c-", i2c_name);
		byt_cht_es8316_dais[dai_index].codec_name = codec_name;
	}

	/* Check for BYTCR or other platform and setup quirks */
	if (x86_match_cpu(baytrail_cpu_ids) &&
	    mach->mach_params.acpi_ipc_irq_index == 0) {
		/* On BYTCR default to SSP0 */
		quirk = BYT_CHT_ES8316_SSP0;
	} else {
		quirk = 0;
	}
	if (quirk_override != -1) {
		dev_info(dev, "Overriding quirk 0x%x => 0x%x\n", quirk,
			 quirk_override);
		quirk = quirk_override;
	}
	log_quirks(dev);

	if (quirk & BYT_CHT_ES8316_SSP0)
		byt_cht_es8316_dais[dai_index].cpu_dai_name = "ssp0-port";

	/* get the clock */
	priv->mclk = devm_clk_get(dev, "pmc_plt_clk_3");
	if (IS_ERR(priv->mclk)) {
		ret = PTR_ERR(priv->mclk);
		dev_err(dev, "clk_get pmc_plt_clk_3 failed: %d\n", ret);
		return ret;
	}

	/* register the soc card */
	byt_cht_es8316_card.dev = dev;
	snd_soc_card_set_drvdata(&byt_cht_es8316_card, priv);

	ret = devm_snd_soc_register_card(dev, &byt_cht_es8316_card);
	if (ret) {
		dev_err(dev, "snd_soc_register_card failed: %d\n", ret);
		return ret;
	}
	platform_set_drvdata(pdev, &byt_cht_es8316_card);
	return 0;
}

static struct platform_driver snd_byt_cht_es8316_mc_driver = {
	.driver = {
		.name = "bytcht_es8316",
	},
	.probe = snd_byt_cht_es8316_mc_probe,
};

module_platform_driver(snd_byt_cht_es8316_mc_driver);
MODULE_DESCRIPTION("ASoC Intel(R) Baytrail/Cherrytrail Machine driver");
MODULE_AUTHOR("David Yang <yangxiaohua@everest-semi.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcht_es8316");
