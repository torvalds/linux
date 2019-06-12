/*
 *  cht_bsw_rt5672.c - ASoc Machine driver for Intel Cherryview-based platforms
 *                     Cherrytrail and Braswell, with RT5672 codec.
 *
 *  Copyright (C) 2014 Intel Corp
 *  Author: Subhransu S. Prusty <subhransu.s.prusty@intel.com>
 *          Mengdong Lin <mengdong.lin@intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5670.h"
#include "../atom/sst-atom-controls.h"


/* The platform clock #3 outputs 19.2Mhz clock to codec as I2S MCLK */
#define CHT_PLAT_CLK_3_HZ	19200000
#define CHT_CODEC_DAI	"rt5670-aif1"

struct cht_mc_private {
	struct snd_soc_jack headset;
	char codec_name[SND_ACPI_I2C_ID_LEN];
	struct clk *mclk;
};

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin cht_bsw_headset_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(card);
	int ret;

	codec_dai = snd_soc_card_get_codec_dai(card, CHT_CODEC_DAI);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found; Unable to set platform clock\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (ctx->mclk) {
			ret = clk_prepare_enable(ctx->mclk);
			if (ret < 0) {
				dev_err(card->dev,
					"could not configure MCLK state");
				return ret;
			}
		}

		/* set codec PLL source to the 19.2MHz platform clock (MCLK) */
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT5670_PLL1_S_MCLK,
				CHT_PLAT_CLK_3_HZ, 48000 * 512);
		if (ret < 0) {
			dev_err(card->dev, "can't set codec pll: %d\n", ret);
			return ret;
		}

		/* set codec sysclk source to PLL */
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5670_SCLK_S_PLL1,
			48000 * 512, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "can't set codec sysclk: %d\n", ret);
			return ret;
		}
	} else {
		/* Set codec sysclk source to its internal clock because codec
		 * PLL will be off when idle and MCLK will also be off by ACPI
		 * when codec is runtime suspended. Codec needs clock for jack
		 * detection and button press.
		 */
		snd_soc_dai_set_sysclk(codec_dai, RT5670_SCLK_S_RCCLK,
				       48000 * 512, SND_SOC_CLOCK_IN);

		if (ctx->mclk)
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
	{"IN1P", NULL, "Headset Mic"},
	{"IN1N", NULL, "Headset Mic"},
	{"DMIC L1", NULL, "Int Mic"},
	{"DMIC R1", NULL, "Int Mic"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Ext Spk", NULL, "SPOLP"},
	{"Ext Spk", NULL, "SPOLN"},
	{"Ext Spk", NULL, "SPORP"},
	{"Ext Spk", NULL, "SPORN"},
	{"AIF1 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
	{"ssp2 Rx", NULL, "AIF1 Capture"},
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
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* set codec PLL source to the 19.2MHz platform clock (MCLK) */
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5670_PLL1_S_MCLK,
				  CHT_PLAT_CLK_3_HZ, params_rate(params) * 512);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	/* set codec sysclk source to PLL */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5670_SCLK_S_PLL1,
				     params_rate(params) * 512,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct acpi_gpio_params headset_gpios = { 0, 0, false };

static const struct acpi_gpio_mapping cht_rt5672_gpios[] = {
	{ "headset-gpios", &headset_gpios, 1 },
	{},
};

static int cht_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_dai *codec_dai = runtime->codec_dai;
	struct snd_soc_component *component = codec_dai->component;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(runtime->card);

	if (devm_acpi_dev_add_driver_gpios(component->dev, cht_rt5672_gpios))
		dev_warn(runtime->dev, "Unable to add GPIO mapping table\n");

	/* Select codec ASRC clock source to track I2S1 clock, because codec
	 * is in slave mode and 100fs I2S format (BCLK = 100 * LRCLK) cannot
	 * be supported by RT5672. Otherwise, ASRC will be disabled and cause
	 * noise.
	 */
	rt5670_sel_asrc_clk_src(component,
				RT5670_DA_STEREO_FILTER
				| RT5670_DA_MONO_L_FILTER
				| RT5670_DA_MONO_R_FILTER
				| RT5670_AD_STEREO_FILTER
				| RT5670_AD_MONO_L_FILTER
				| RT5670_AD_MONO_R_FILTER,
				RT5670_CLK_SEL_I2S1_ASRC);

        ret = snd_soc_card_jack_new(runtime->card, "Headset",
				    SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2,
				    &ctx->headset,
				    cht_bsw_headset_pins,
				    ARRAY_SIZE(cht_bsw_headset_pins));
        if (ret)
                return ret;

	snd_jack_set_key(ctx->headset.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(ctx->headset.jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(ctx->headset.jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);

	rt5670_set_jack_detect(component, &ctx->headset);
	if (ctx->mclk) {
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

		if (ret) {
			dev_err(runtime->dev, "unable to set MCLK rate\n");
			return ret;
		}
	}
	return 0;
}

static int cht_codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret;

	/* The DSP will covert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 24-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	/*
	 * Default mode for SSP configuration is TDM 4 slot
	 */
	ret = snd_soc_dai_set_fmt(rtd->codec_dai,
				  SND_SOC_DAIFMT_DSP_B |
				  SND_SOC_DAIFMT_IB_NF |
				  SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to TDM %d\n", ret);
		return ret;
	}

	/* TDM 4 slots 24 bit, set Rx & Tx bitmask to 4 active slots */
	ret = snd_soc_dai_set_tdm_slot(rtd->codec_dai, 0xF, 0xF, 4, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec TDM slot %d\n", ret);
		return ret;
	}

	return 0;
}

static int cht_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static const struct snd_soc_ops cht_aif1_ops = {
	.startup = cht_aif1_startup,
};

static const struct snd_soc_ops cht_be_ssp2_ops = {
	.hw_params = cht_aif1_hw_params,
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
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5670:00",
				      "rt5670-aif1")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("sst-mfld-platform")));

static struct snd_soc_dai_link cht_dailink[] = {
	/* Front End DAI links */
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

	/* Back End DAI links */
	{
		/* SSP2 - Codec */
		.name = "SSP2-Codec",
		.id = 0,
		.no_pcm = 1,
		.nonatomic = true,
		.init = cht_codec_init,
		.be_hw_params_fixup = cht_codec_fixup,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &cht_be_ssp2_ops,
		SND_SOC_DAILINK_REG(ssp2_port, ssp2_codec, platform),
	},
};

static int cht_suspend_pre(struct snd_soc_card *card)
{
	struct snd_soc_component *component;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(card);

	for_each_card_components(card, component) {
		if (!strncmp(component->name,
			     ctx->codec_name, sizeof(ctx->codec_name))) {

			dev_dbg(component->dev, "disabling jack detect before going to suspend.\n");
			rt5670_jack_suspend(component);
			break;
		}
	}
	return 0;
}

static int cht_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_component *component;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(card);

	for_each_card_components(card, component) {
		if (!strncmp(component->name,
			     ctx->codec_name, sizeof(ctx->codec_name))) {

			dev_dbg(component->dev, "enabling jack detect for resume.\n");
			rt5670_jack_resume(component);
			break;
		}
	}

	return 0;
}

/* SoC card */
static struct snd_soc_card snd_soc_card_cht = {
	.name = "cht-bsw-rt5672",
	.owner = THIS_MODULE,
	.dai_link = cht_dailink,
	.num_links = ARRAY_SIZE(cht_dailink),
	.dapm_widgets = cht_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cht_dapm_widgets),
	.dapm_routes = cht_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cht_audio_map),
	.controls = cht_mc_controls,
	.num_controls = ARRAY_SIZE(cht_mc_controls),
	.suspend_pre = cht_suspend_pre,
	.resume_post = cht_resume_post,
};

#define RT5672_I2C_DEFAULT	"i2c-10EC5670:00"

static int snd_cht_mc_probe(struct platform_device *pdev)
{
	int ret_val = 0;
	struct cht_mc_private *drv;
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	const char *platform_name;
	struct acpi_device *adev;
	int i;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	strcpy(drv->codec_name, RT5672_I2C_DEFAULT);

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(drv->codec_name, sizeof(drv->codec_name),
			 "i2c-%s", acpi_dev_name(adev));
		put_device(&adev->dev);
		for (i = 0; i < ARRAY_SIZE(cht_dailink); i++) {
			if (!strcmp(cht_dailink[i].codecs->name,
				    RT5672_I2C_DEFAULT)) {
				cht_dailink[i].codecs->name = drv->codec_name;
				break;
			}
		}
	}

	/* override plaform name, if required */
	snd_soc_card_cht.dev = &pdev->dev;
	platform_name = mach->mach_params.platform;

	ret_val = snd_soc_fixup_dai_links_platform_name(&snd_soc_card_cht,
							platform_name);
	if (ret_val)
		return ret_val;

	drv->mclk = devm_clk_get(&pdev->dev, "pmc_plt_clk_3");
	if (IS_ERR(drv->mclk)) {
		dev_err(&pdev->dev,
			"Failed to get MCLK from pmc_plt_clk_3: %ld\n",
			PTR_ERR(drv->mclk));
		return PTR_ERR(drv->mclk);
	}
	snd_soc_card_set_drvdata(&snd_soc_card_cht, drv);

	/* register the soc card */
	ret_val = devm_snd_soc_register_card(&pdev->dev, &snd_soc_card_cht);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, &snd_soc_card_cht);
	return ret_val;
}

static struct platform_driver snd_cht_mc_driver = {
	.driver = {
		.name = "cht-bsw-rt5672",
	},
	.probe = snd_cht_mc_probe,
};

module_platform_driver(snd_cht_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail CR Machine driver");
MODULE_AUTHOR("Subhransu S. Prusty, Mengdong Lin");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cht-bsw-rt5672");
