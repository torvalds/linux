/*
 *  bytcr_rt5651.c - ASoc Machine driver for Intel Byt CR platform
 *  (derived from bytcr_rt5640.c)
 *
 *  Copyright (C) 2015 Intel Corp
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <asm/platform_sst_audio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5651.h"
#include "../atom/sst-atom-controls.h"

enum {
	BYT_RT5651_DMIC_MAP,
	BYT_RT5651_IN1_MAP,
	BYT_RT5651_IN2_MAP,
};

#define BYT_RT5651_MAP(quirk)	((quirk) & GENMASK(7, 0))
#define BYT_RT5651_DMIC_EN	BIT(16)
#define BYT_RT5651_MCLK_EN	BIT(17)
#define BYT_RT5651_MCLK_25MHZ	BIT(18)

struct byt_rt5651_private {
	struct clk *mclk;
	struct snd_soc_jack jack;
};

static unsigned long byt_rt5651_quirk = BYT_RT5651_DMIC_MAP |
					BYT_RT5651_DMIC_EN |
					BYT_RT5651_MCLK_EN;

static void log_quirks(struct device *dev)
{
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_DMIC_MAP)
		dev_info(dev, "quirk DMIC_MAP enabled");
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_IN1_MAP)
		dev_info(dev, "quirk IN1_MAP enabled");
	if (BYT_RT5651_MAP(byt_rt5651_quirk) == BYT_RT5651_IN2_MAP)
		dev_info(dev, "quirk IN2_MAP enabled");
	if (byt_rt5651_quirk & BYT_RT5651_DMIC_EN)
		dev_info(dev, "quirk DMIC enabled");
	if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN)
		dev_info(dev, "quirk MCLK_EN enabled");
	if (byt_rt5651_quirk & BYT_RT5651_MCLK_25MHZ)
		dev_info(dev, "quirk MCLK_25MHZ enabled");
}

#define BYT_CODEC_DAI1	"rt5651-aif1"

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	struct byt_rt5651_private *priv = snd_soc_card_get_drvdata(card);
	int ret;

	codec_dai = snd_soc_card_get_codec_dai(card, BYT_CODEC_DAI1);
	if (!codec_dai) {
		dev_err(card->dev,
			"Codec dai not found; Unable to set platform clock\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN) {
			ret = clk_prepare_enable(priv->mclk);
			if (ret < 0) {
				dev_err(card->dev,
					"could not configure MCLK state");
				return ret;
			}
		}
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5651_SCLK_S_PLL1,
					     48000 * 512,
					     SND_SOC_CLOCK_IN);
	} else {
		/*
		 * Set codec clock source to internal clock before
		 * turning off the platform clock. Codec needs clock
		 * for Jack detection and button press
		 */
		ret = snd_soc_dai_set_sysclk(codec_dai, RT5651_SCLK_S_RCCLK,
					     48000 * 512,
					     SND_SOC_CLOCK_IN);
		if (!ret)
			if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN)
				clk_disable_unprepare(priv->mclk);
	}

	if (ret < 0) {
		dev_err(card->dev, "can't set codec sysclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_dapm_widget byt_rt5651_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMD),

};

static const struct snd_soc_dapm_route byt_rt5651_audio_map[] = {
	{"Headphone", NULL, "Platform Clock"},
	{"Headset Mic", NULL, "Platform Clock"},
	{"Internal Mic", NULL, "Platform Clock"},
	{"Speaker", NULL, "Platform Clock"},

	{"AIF1 Playback", NULL, "ssp2 Tx"},
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},
	{"ssp2 Rx", NULL, "AIF1 Capture"},

	{"Headset Mic", NULL, "micbias1"}, /* lowercase for rt5651 */
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Speaker", NULL, "LOUTL"},
	{"Speaker", NULL, "LOUTR"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_dmic_map[] = {
	{"IN2P", NULL, "Headset Mic"},
	{"DMIC L1", NULL, "Internal Mic"},
	{"DMIC R1", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_in1_map[] = {
	{"Internal Mic", NULL, "micbias1"},
	{"IN2P", NULL, "Headset Mic"},
	{"IN1P", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5651_intmic_in2_map[] = {
	{"Internal Mic", NULL, "micbias1"},
	{"IN1P", NULL, "Headset Mic"},
	{"IN2P", NULL, "Internal Mic"},
};

static const struct snd_kcontrol_new byt_rt5651_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static struct snd_soc_jack_pin bytcr_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static int byt_rt5651_aif1_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	snd_soc_dai_set_bclk_ratio(codec_dai, 50);

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5651_SCLK_S_PLL1,
				     params_rate(params) * 512,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec clock %d\n", ret);
		return ret;
	}

	if (!(byt_rt5651_quirk & BYT_RT5651_MCLK_EN)) {
		/* 2x25 bit slots on SSP2 */
		ret = snd_soc_dai_set_pll(codec_dai, 0,
					RT5651_PLL1_S_BCLK1,
					params_rate(params) * 50,
					params_rate(params) * 512);
	} else {
		if (byt_rt5651_quirk & BYT_RT5651_MCLK_25MHZ) {
			ret = snd_soc_dai_set_pll(codec_dai, 0,
						RT5651_PLL1_S_MCLK,
						25000000,
						params_rate(params) * 512);
		} else {
			ret = snd_soc_dai_set_pll(codec_dai, 0,
						RT5651_PLL1_S_MCLK,
						19200000,
						params_rate(params) * 512);
		}
	}

	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}

	return 0;
}

static int byt_rt5651_quirk_cb(const struct dmi_system_id *id)
{
	byt_rt5651_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id byt_rt5651_quirk_table[] = {
	{
		.callback = byt_rt5651_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Circuitco"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Minnowboard Max B3 PLATFORM"),
		},
		.driver_data = (void *)(BYT_RT5651_DMIC_MAP |
					BYT_RT5651_DMIC_EN),
	},
	{
		.callback = byt_rt5651_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "KIANO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "KIANO SlimNote 14.2"),
		},
		.driver_data = (void *)(BYT_RT5651_IN2_MAP),
	},
	{}
};

static int byt_rt5651_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_codec *codec = runtime->codec;
	struct byt_rt5651_private *priv = snd_soc_card_get_drvdata(card);
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;
	int ret;

	card->dapm.idle_bias_off = true;

	switch (BYT_RT5651_MAP(byt_rt5651_quirk)) {
	case BYT_RT5651_IN1_MAP:
		custom_map = byt_rt5651_intmic_in1_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_in1_map);
		break;
	case BYT_RT5651_IN2_MAP:
		custom_map = byt_rt5651_intmic_in2_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_in2_map);
		break;
	default:
		custom_map = byt_rt5651_intmic_dmic_map;
		num_routes = ARRAY_SIZE(byt_rt5651_intmic_dmic_map);
	}
	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

	ret = snd_soc_add_card_controls(card, byt_rt5651_controls,
					ARRAY_SIZE(byt_rt5651_controls));
	if (ret) {
		dev_err(card->dev, "unable to add card controls\n");
		return ret;
	}
	snd_soc_dapm_ignore_suspend(&card->dapm, "Headphone");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Speaker");

	if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN) {
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
		ret = clk_prepare_enable(priv->mclk);
		if (!ret)
			clk_disable_unprepare(priv->mclk);

		if (byt_rt5651_quirk & BYT_RT5651_MCLK_25MHZ)
			ret = clk_set_rate(priv->mclk, 25000000);
		else
			ret = clk_set_rate(priv->mclk, 19200000);

		if (ret)
			dev_err(card->dev, "unable to set MCLK rate\n");
	}

	ret = snd_soc_card_jack_new(runtime->card, "Headset",
				    SND_JACK_HEADSET, &priv->jack,
				    bytcr_jack_pins, ARRAY_SIZE(bytcr_jack_pins));
	if (ret) {
		dev_err(runtime->dev, "Headset jack creation failed %d\n", ret);
		return ret;
	}

	rt5651_set_jack_detect(codec, &priv->jack);

	return ret;
}

static const struct snd_soc_pcm_stream byt_rt5651_dai_params = {
	.formats = SNDRV_PCM_FMTBIT_S24_LE,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
};

static int byt_rt5651_codec_fixup(struct snd_soc_pcm_runtime *rtd,
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

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x3, 0x3, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static const unsigned int rates_48000[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_48000 = {
	.count = ARRAY_SIZE(rates_48000),
	.list  = rates_48000,
};

static int byt_rt5651_aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			&constraints_48000);
}

static const struct snd_soc_ops byt_rt5651_aif1_ops = {
	.startup = byt_rt5651_aif1_startup,
};

static const struct snd_soc_ops byt_rt5651_be_ssp2_ops = {
	.hw_params = byt_rt5651_aif1_hw_params,
};

static struct snd_soc_dai_link byt_rt5651_dais[] = {
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
		.ops = &byt_rt5651_aif1_ops,
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
		.ops = &byt_rt5651_aif1_ops,
	},
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 0,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "rt5651-aif1",
		.codec_name = "i2c-10EC5651:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = byt_rt5651_codec_fixup,
		.ignore_suspend = 1,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.init = byt_rt5651_init,
		.ops = &byt_rt5651_be_ssp2_ops,
	},
};

/* SoC card */
static struct snd_soc_card byt_rt5651_card = {
	.name = "bytcr-rt5651",
	.owner = THIS_MODULE,
	.dai_link = byt_rt5651_dais,
	.num_links = ARRAY_SIZE(byt_rt5651_dais),
	.dapm_widgets = byt_rt5651_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_rt5651_widgets),
	.dapm_routes = byt_rt5651_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_rt5651_audio_map),
	.fully_routed = true,
};

static char byt_rt5651_codec_name[16]; /* i2c-<HID>:00 with HID being 8 chars */

static int snd_byt_rt5651_mc_probe(struct platform_device *pdev)
{
	struct byt_rt5651_private *priv;
	struct snd_soc_acpi_mach *mach;
	const char *i2c_name = NULL;
	int ret_val = 0;
	int dai_index = 0;
	int i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_ATOMIC);
	if (!priv)
		return -ENOMEM;

	/* register the soc card */
	byt_rt5651_card.dev = &pdev->dev;

	mach = byt_rt5651_card.dev->platform_data;
	snd_soc_card_set_drvdata(&byt_rt5651_card, priv);

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(byt_rt5651_dais); i++) {
		if (!strcmp(byt_rt5651_dais[i].codec_name, "i2c-10EC5651:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	i2c_name = snd_soc_acpi_find_name_from_hid(mach->id);
	if (i2c_name) {
		snprintf(byt_rt5651_codec_name, sizeof(byt_rt5651_codec_name),
			"%s%s", "i2c-", i2c_name);

		byt_rt5651_dais[dai_index].codec_name = byt_rt5651_codec_name;
	}

	/* check quirks before creating card */
	dmi_check_system(byt_rt5651_quirk_table);
	log_quirks(&pdev->dev);

	if (byt_rt5651_quirk & BYT_RT5651_MCLK_EN) {
		priv->mclk = devm_clk_get(&pdev->dev, "pmc_plt_clk_3");
		if (IS_ERR(priv->mclk)) {
			dev_err(&pdev->dev,
				"Failed to get MCLK from pmc_plt_clk_3: %ld\n",
				PTR_ERR(priv->mclk));
			/*
			 * Fall back to bit clock usage for -ENOENT (clock not
			 * available likely due to missing dependencies), bail
			 * for all other errors, including -EPROBE_DEFER
			 */
			if (ret_val != -ENOENT)
				return ret_val;
			byt_rt5651_quirk &= ~BYT_RT5651_MCLK_EN;
		}
	}

	ret_val = devm_snd_soc_register_card(&pdev->dev, &byt_rt5651_card);

	if (ret_val) {
		dev_err(&pdev->dev, "devm_snd_soc_register_card failed %d\n",
			ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, &byt_rt5651_card);
	return ret_val;
}

static struct platform_driver snd_byt_rt5651_mc_driver = {
	.driver = {
		.name = "bytcr_rt5651",
	},
	.probe = snd_byt_rt5651_mc_probe,
};

module_platform_driver(snd_byt_rt5651_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail CR Machine driver for RT5651");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcr_rt5651");
