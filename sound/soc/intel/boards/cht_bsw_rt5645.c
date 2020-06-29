// SPDX-License-Identifier: GPL-2.0-only
/*
 *  cht-bsw-rt5645.c - ASoc Machine driver for Intel Cherryview-based platforms
 *                     Cherrytrail and Braswell, with RT5645 codec.
 *
 *  Copyright (C) 2015 Intel Corp
 *  Author: Fang, Yang A <yang.a.fang@intel.com>
 *	        N,Harshapriya <harshapriya.n@intel.com>
 *  This file is modified from cht_bsw_rt5672.c
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5645.h"
#include "../atom/sst-atom-controls.h"
#include "../common/soc-intel-quirks.h"

#define CHT_PLAT_CLK_3_HZ	19200000
#define CHT_CODEC_DAI1	"rt5645-aif1"
#define CHT_CODEC_DAI2	"rt5645-aif2"

struct cht_acpi_card {
	char *codec_id;
	int codec_type;
	struct snd_soc_card *soc_card;
};

struct cht_mc_private {
	struct snd_soc_jack jack;
	struct cht_acpi_card *acpi_card;
	char codec_name[SND_ACPI_I2C_ID_LEN];
	struct clk *mclk;
};

#define CHT_RT5645_MAP(quirk)	((quirk) & GENMASK(7, 0))
#define CHT_RT5645_SSP2_AIF2     BIT(16) /* default is using AIF1  */
#define CHT_RT5645_SSP0_AIF1     BIT(17)
#define CHT_RT5645_SSP0_AIF2     BIT(18)
#define CHT_RT5645_PMC_PLT_CLK_0 BIT(19)

static unsigned long cht_rt5645_quirk = 0;

static void log_quirks(struct device *dev)
{
	if (cht_rt5645_quirk & CHT_RT5645_SSP2_AIF2)
		dev_info(dev, "quirk SSP2_AIF2 enabled");
	if (cht_rt5645_quirk & CHT_RT5645_SSP0_AIF1)
		dev_info(dev, "quirk SSP0_AIF1 enabled");
	if (cht_rt5645_quirk & CHT_RT5645_SSP0_AIF2)
		dev_info(dev, "quirk SSP0_AIF2 enabled");
	if (cht_rt5645_quirk & CHT_RT5645_PMC_PLT_CLK_0)
		dev_info(dev, "quirk PMC_PLT_CLK_0 enabled");
}

static int platform_clock_control(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(card);
	int ret;

	codec_dai = snd_soc_card_get_codec_dai(card, CHT_CODEC_DAI1);
	if (!codec_dai)
		codec_dai = snd_soc_card_get_codec_dai(card, CHT_CODEC_DAI2);

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
		/* Set codec sysclk source to its internal clock because codec PLL will
		 * be off when idle and MCLK will also be off when codec is
		 * runtime suspended. Codec needs clock for jack detection and button
		 * press. MCLK is turned off with clock framework or ACPI.
		 */
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5645_SCLK_S_RCCLK,
					48000 * 512, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "can't set codec sysclk: %d\n", ret);
			return ret;
		}

		clk_disable_unprepare(ctx->mclk);
	}

	return 0;
}

static const struct snd_soc_dapm_widget cht_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_MIC("Int Analog Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			platform_clock_control, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route cht_rt5645_audio_map[] = {
	{"IN1P", NULL, "Headset Mic"},
	{"IN1N", NULL, "Headset Mic"},
	{"DMIC L1", NULL, "Int Mic"},
	{"DMIC R1", NULL, "Int Mic"},
	{"IN2P", NULL, "Int Analog Mic"},
	{"IN2N", NULL, "Int Analog Mic"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Ext Spk", NULL, "SPOL"},
	{"Ext Spk", NULL, "SPOR"},
	{"Headphone", NULL, "Platform Clock"},
	{"Headset Mic", NULL, "Platform Clock"},
	{"Int Mic", NULL, "Platform Clock"},
	{"Int Analog Mic", NULL, "Platform Clock"},
	{"Int Analog Mic", NULL, "micbias1"},
	{"Int Analog Mic", NULL, "micbias2"},
	{"Ext Spk", NULL, "Platform Clock"},
};

static const struct snd_soc_dapm_route cht_rt5650_audio_map[] = {
	{"IN1P", NULL, "Headset Mic"},
	{"IN1N", NULL, "Headset Mic"},
	{"DMIC L2", NULL, "Int Mic"},
	{"DMIC R2", NULL, "Int Mic"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Ext Spk", NULL, "SPOL"},
	{"Ext Spk", NULL, "SPOR"},
	{"Headphone", NULL, "Platform Clock"},
	{"Headset Mic", NULL, "Platform Clock"},
	{"Int Mic", NULL, "Platform Clock"},
	{"Ext Spk", NULL, "Platform Clock"},
};

static const struct snd_soc_dapm_route cht_rt5645_ssp2_aif1_map[] = {
	{"AIF1 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx" },
	{"codec_in1", NULL, "ssp2 Rx" },
	{"ssp2 Rx", NULL, "AIF1 Capture"},
};

static const struct snd_soc_dapm_route cht_rt5645_ssp2_aif2_map[] = {
	{"AIF2 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx" },
	{"codec_in1", NULL, "ssp2 Rx" },
	{"ssp2 Rx", NULL, "AIF2 Capture"},
};

static const struct snd_soc_dapm_route cht_rt5645_ssp0_aif1_map[] = {
	{"AIF1 Playback", NULL, "ssp0 Tx"},
	{"ssp0 Tx", NULL, "modem_out"},
	{"modem_in", NULL, "ssp0 Rx" },
	{"ssp0 Rx", NULL, "AIF1 Capture"},
};

static const struct snd_soc_dapm_route cht_rt5645_ssp0_aif2_map[] = {
	{"AIF2 Playback", NULL, "ssp0 Tx"},
	{"ssp0 Tx", NULL, "modem_out"},
	{"modem_in", NULL, "ssp0 Rx" },
	{"ssp0 Rx", NULL, "AIF2 Capture"},
};

static const struct snd_kcontrol_new cht_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Int Analog Mic"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static struct snd_soc_jack_pin cht_bsw_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static int cht_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int ret;

	/* set codec PLL source to the 19.2MHz platform clock (MCLK) */
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5645_PLL1_S_MCLK,
				  CHT_PLAT_CLK_3_HZ, params_rate(params) * 512);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5645_SCLK_S_PLL1,
				params_rate(params) * 512, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int cht_rt5645_quirk_cb(const struct dmi_system_id *id)
{
	cht_rt5645_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id cht_rt5645_quirk_table[] = {
	{
		/* Strago family Chromebooks */
		.callback = cht_rt5645_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Intel_Strago"),
		},
		.driver_data = (void *)CHT_RT5645_PMC_PLT_CLK_0,
	},
	{
	},
};

static int cht_codec_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct cht_mc_private *ctx = snd_soc_card_get_drvdata(runtime->card);
	struct snd_soc_component *component = asoc_rtd_to_codec(runtime, 0)->component;
	int jack_type;
	int ret;

	if ((cht_rt5645_quirk & CHT_RT5645_SSP2_AIF2) ||
	    (cht_rt5645_quirk & CHT_RT5645_SSP0_AIF2)) {
		/* Select clk_i2s2_asrc as ASRC clock source */
		rt5645_sel_asrc_clk_src(component,
					RT5645_DA_STEREO_FILTER |
					RT5645_DA_MONO_L_FILTER |
					RT5645_DA_MONO_R_FILTER |
					RT5645_AD_STEREO_FILTER,
					RT5645_CLK_SEL_I2S2_ASRC);
	} else {
		/* Select clk_i2s1_asrc as ASRC clock source */
		rt5645_sel_asrc_clk_src(component,
					RT5645_DA_STEREO_FILTER |
					RT5645_DA_MONO_L_FILTER |
					RT5645_DA_MONO_R_FILTER |
					RT5645_AD_STEREO_FILTER,
					RT5645_CLK_SEL_I2S1_ASRC);
	}

	if (cht_rt5645_quirk & CHT_RT5645_SSP2_AIF2) {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					cht_rt5645_ssp2_aif2_map,
					ARRAY_SIZE(cht_rt5645_ssp2_aif2_map));
	} else if (cht_rt5645_quirk & CHT_RT5645_SSP0_AIF1) {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					cht_rt5645_ssp0_aif1_map,
					ARRAY_SIZE(cht_rt5645_ssp0_aif1_map));
	} else if (cht_rt5645_quirk & CHT_RT5645_SSP0_AIF2) {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					cht_rt5645_ssp0_aif2_map,
					ARRAY_SIZE(cht_rt5645_ssp0_aif2_map));
	} else {
		ret = snd_soc_dapm_add_routes(&card->dapm,
					cht_rt5645_ssp2_aif1_map,
					ARRAY_SIZE(cht_rt5645_ssp2_aif1_map));
	}
	if (ret)
		return ret;

	if (ctx->acpi_card->codec_type == CODEC_TYPE_RT5650)
		jack_type = SND_JACK_HEADPHONE | SND_JACK_MICROPHONE |
					SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					SND_JACK_BTN_2 | SND_JACK_BTN_3;
	else
		jack_type = SND_JACK_HEADPHONE | SND_JACK_MICROPHONE;

	ret = snd_soc_card_jack_new(runtime->card, "Headset",
				    jack_type, &ctx->jack,
				    cht_bsw_jack_pins, ARRAY_SIZE(cht_bsw_jack_pins));
	if (ret) {
		dev_err(runtime->dev, "Headset jack creation failed %d\n", ret);
		return ret;
	}

	rt5645_set_jack_detect(component, &ctx->jack, &ctx->jack, &ctx->jack);


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
	int ret;
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The DSP will covert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	if ((cht_rt5645_quirk & CHT_RT5645_SSP0_AIF1) ||
		(cht_rt5645_quirk & CHT_RT5645_SSP0_AIF2)) {

		/* set SSP0 to 16-bit */
		params_set_format(params, SNDRV_PCM_FORMAT_S16_LE);

		/*
		 * Default mode for SSP configuration is TDM 4 slot, override config
		 * with explicit setting to I2S 2ch 16-bit. The word length is set with
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

		ret = snd_soc_dai_set_fmt(asoc_rtd_to_codec(rtd, 0),
					SND_SOC_DAIFMT_I2S     |
					SND_SOC_DAIFMT_NB_NF   |
					SND_SOC_DAIFMT_CBS_CFS
			);
		if (ret < 0) {
			dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
			return ret;
		}

		ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_cpu(rtd, 0), 0x3, 0x3, 2, 16);
		if (ret < 0) {
			dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
			return ret;
		}

	} else {

		/* set SSP2 to 24-bit */
		params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

		/*
		 * Default mode for SSP configuration is TDM 4 slot
		 */
		ret = snd_soc_dai_set_fmt(asoc_rtd_to_codec(rtd, 0),
					SND_SOC_DAIFMT_DSP_B |
					SND_SOC_DAIFMT_IB_NF |
					SND_SOC_DAIFMT_CBS_CFS);
		if (ret < 0) {
			dev_err(rtd->dev, "can't set format to TDM %d\n", ret);
			return ret;
		}

		/* TDM 4 slots 24 bit, set Rx & Tx bitmask to 4 active slots */
		ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_codec(rtd, 0), 0xF, 0xF, 4, 24);
		if (ret < 0) {
			dev_err(rtd->dev, "can't set codec TDM slot %d\n", ret);
			return ret;
		}
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
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5645:00", "rt5645-aif1")));

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
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 0,
		.no_pcm = 1,
		.init = cht_codec_init,
		.be_hw_params_fixup = cht_codec_fixup,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &cht_be_ssp2_ops,
		SND_SOC_DAILINK_REG(ssp2_port, ssp2_codec, platform),
	},
};

/* SoC card */
static struct snd_soc_card snd_soc_card_chtrt5645 = {
	.name = "chtrt5645",
	.owner = THIS_MODULE,
	.dai_link = cht_dailink,
	.num_links = ARRAY_SIZE(cht_dailink),
	.dapm_widgets = cht_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cht_dapm_widgets),
	.dapm_routes = cht_rt5645_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cht_rt5645_audio_map),
	.controls = cht_mc_controls,
	.num_controls = ARRAY_SIZE(cht_mc_controls),
};

static struct snd_soc_card snd_soc_card_chtrt5650 = {
	.name = "chtrt5650",
	.owner = THIS_MODULE,
	.dai_link = cht_dailink,
	.num_links = ARRAY_SIZE(cht_dailink),
	.dapm_widgets = cht_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cht_dapm_widgets),
	.dapm_routes = cht_rt5650_audio_map,
	.num_dapm_routes = ARRAY_SIZE(cht_rt5650_audio_map),
	.controls = cht_mc_controls,
	.num_controls = ARRAY_SIZE(cht_mc_controls),
};

static struct cht_acpi_card snd_soc_cards[] = {
	{"10EC5640", CODEC_TYPE_RT5645, &snd_soc_card_chtrt5645},
	{"10EC5645", CODEC_TYPE_RT5645, &snd_soc_card_chtrt5645},
	{"10EC5648", CODEC_TYPE_RT5645, &snd_soc_card_chtrt5645},
	{"10EC3270", CODEC_TYPE_RT5645, &snd_soc_card_chtrt5645},
	{"10EC5650", CODEC_TYPE_RT5650, &snd_soc_card_chtrt5650},
};

static char cht_rt5645_codec_name[SND_ACPI_I2C_ID_LEN];

struct acpi_chan_package {   /* ACPICA seems to require 64 bit integers */
	u64 aif_value;       /* 1: AIF1, 2: AIF2 */
	u64 mclock_value;    /* usually 25MHz (0x17d7940), ignored */
};

static int snd_cht_mc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = snd_soc_cards[0].soc_card;
	struct snd_soc_acpi_mach *mach;
	const char *platform_name;
	struct cht_mc_private *drv;
	struct acpi_device *adev;
	bool found = false;
	bool is_bytcr = false;
	int dai_index = 0;
	int ret_val = 0;
	int i;
	const char *mclk_name;

	drv = devm_kzalloc(&pdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	mach = pdev->dev.platform_data;

	for (i = 0; i < ARRAY_SIZE(snd_soc_cards); i++) {
		if (acpi_dev_found(snd_soc_cards[i].codec_id) &&
			(!strncmp(snd_soc_cards[i].codec_id, mach->id, 8))) {
			dev_dbg(&pdev->dev,
				"found codec %s\n", snd_soc_cards[i].codec_id);
			card = snd_soc_cards[i].soc_card;
			drv->acpi_card = &snd_soc_cards[i];
			found = true;
			break;
		}
	}

	if (!found) {
		dev_err(&pdev->dev, "No matching HID found in supported list\n");
		return -ENODEV;
	}

	card->dev = &pdev->dev;
	sprintf(drv->codec_name, "i2c-%s:00", drv->acpi_card->codec_id);

	/* set correct codec name */
	for (i = 0; i < ARRAY_SIZE(cht_dailink); i++)
		if (!strcmp(card->dai_link[i].codecs->name,
			    "i2c-10EC5645:00")) {
			card->dai_link[i].codecs->name = drv->codec_name;
			dai_index = i;
		}

	/* fixup codec name based on HID */
	adev = acpi_dev_get_first_match_dev(mach->id, NULL, -1);
	if (adev) {
		snprintf(cht_rt5645_codec_name, sizeof(cht_rt5645_codec_name),
			 "i2c-%s", acpi_dev_name(adev));
		put_device(&adev->dev);
		cht_dailink[dai_index].codecs->name = cht_rt5645_codec_name;
	}

	/*
	 * swap SSP0 if bytcr is detected
	 * (will be overridden if DMI quirk is detected)
	 */
	if (soc_intel_is_byt()) {
		if (mach->mach_params.acpi_ipc_irq_index == 0)
			is_bytcr = true;
	}

	if (is_bytcr) {
		/*
		 * Baytrail CR platforms may have CHAN package in BIOS, try
		 * to find relevant routing quirk based as done on Windows
		 * platforms. We have to read the information directly from the
		 * BIOS, at this stage the card is not created and the links
		 * with the codec driver/pdata are non-existent
		 */

		struct acpi_chan_package chan_package;

		/* format specified: 2 64-bit integers */
		struct acpi_buffer format = {sizeof("NN"), "NN"};
		struct acpi_buffer state = {0, NULL};
		struct snd_soc_acpi_package_context pkg_ctx;
		bool pkg_found = false;

		state.length = sizeof(chan_package);
		state.pointer = &chan_package;

		pkg_ctx.name = "CHAN";
		pkg_ctx.length = 2;
		pkg_ctx.format = &format;
		pkg_ctx.state = &state;
		pkg_ctx.data_valid = false;

		pkg_found = snd_soc_acpi_find_package_from_hid(mach->id,
							       &pkg_ctx);
		if (pkg_found) {
			if (chan_package.aif_value == 1) {
				dev_info(&pdev->dev, "BIOS Routing: AIF1 connected\n");
				cht_rt5645_quirk |= CHT_RT5645_SSP0_AIF1;
			} else  if (chan_package.aif_value == 2) {
				dev_info(&pdev->dev, "BIOS Routing: AIF2 connected\n");
				cht_rt5645_quirk |= CHT_RT5645_SSP0_AIF2;
			} else {
				dev_info(&pdev->dev, "BIOS Routing isn't valid, ignored\n");
				pkg_found = false;
			}
		}

		if (!pkg_found) {
			/* no BIOS indications, assume SSP0-AIF2 connection */
			cht_rt5645_quirk |= CHT_RT5645_SSP0_AIF2;
		}
	}

	/* check quirks before creating card */
	dmi_check_system(cht_rt5645_quirk_table);
	log_quirks(&pdev->dev);

	if ((cht_rt5645_quirk & CHT_RT5645_SSP2_AIF2) ||
	    (cht_rt5645_quirk & CHT_RT5645_SSP0_AIF2))
		cht_dailink[dai_index].codecs->dai_name = "rt5645-aif2";

	if ((cht_rt5645_quirk & CHT_RT5645_SSP0_AIF1) ||
	    (cht_rt5645_quirk & CHT_RT5645_SSP0_AIF2))
		cht_dailink[dai_index].cpus->dai_name = "ssp0-port";

	/* override plaform name, if required */
	platform_name = mach->mach_params.platform;

	ret_val = snd_soc_fixup_dai_links_platform_name(card,
							platform_name);
	if (ret_val)
		return ret_val;

	if (cht_rt5645_quirk & CHT_RT5645_PMC_PLT_CLK_0)
		mclk_name = "pmc_plt_clk_0";
	else
		mclk_name = "pmc_plt_clk_3";

	drv->mclk = devm_clk_get(&pdev->dev, mclk_name);
	if (IS_ERR(drv->mclk)) {
		dev_err(&pdev->dev, "Failed to get MCLK from %s: %ld\n",
			mclk_name, PTR_ERR(drv->mclk));
		return PTR_ERR(drv->mclk);
	}

	snd_soc_card_set_drvdata(card, drv);
	ret_val = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, card);
	return ret_val;
}

static struct platform_driver snd_cht_mc_driver = {
	.driver = {
		.name = "cht-bsw-rt5645",
#if IS_ENABLED(CONFIG_SND_SOC_SOF_BAYTRAIL)
		.pm = &snd_soc_pm_ops,
#endif
	},
	.probe = snd_cht_mc_probe,
};

module_platform_driver(snd_cht_mc_driver)

MODULE_DESCRIPTION("ASoC Intel(R) Braswell Machine driver");
MODULE_AUTHOR("Fang, Yang A,N,Harshapriya");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cht-bsw-rt5645");
