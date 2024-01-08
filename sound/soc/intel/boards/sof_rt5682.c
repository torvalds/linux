// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2019-2020 Intel Corporation.

/*
 * Intel SOF Machine Driver with Realtek rt5682 Codec
 * and speaker codec MAX98357A or RT1015.
 */
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dmi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/rt5682.h>
#include <sound/rt5682s.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt5682.h"
#include "../../codecs/rt5682s.h"
#include "../../codecs/rt5645.h"
#include "../common/soc-intel-quirks.h"
#include "sof_board_helpers.h"
#include "sof_maxim_common.h"
#include "sof_realtek_common.h"
#include "sof_ssp_common.h"

#define SOF_RT5682_SSP_CODEC(quirk)		((quirk) & GENMASK(2, 0))
#define SOF_RT5682_SSP_CODEC_MASK			(GENMASK(2, 0))
#define SOF_RT5682_MCLK_EN			BIT(3)
#define SOF_RT5682_SSP_AMP_SHIFT		6
#define SOF_RT5682_SSP_AMP_MASK                 (GENMASK(8, 6))
#define SOF_RT5682_SSP_AMP(quirk)	\
	(((quirk) << SOF_RT5682_SSP_AMP_SHIFT) & SOF_RT5682_SSP_AMP_MASK)
#define SOF_RT5682_MCLK_BYTCHT_EN		BIT(9)
#define SOF_RT5682_NUM_HDMIDEV_SHIFT		10
#define SOF_RT5682_NUM_HDMIDEV_MASK		(GENMASK(12, 10))
#define SOF_RT5682_NUM_HDMIDEV(quirk)	\
	((quirk << SOF_RT5682_NUM_HDMIDEV_SHIFT) & SOF_RT5682_NUM_HDMIDEV_MASK)

/* BT audio offload: reserve 3 bits for future */
#define SOF_BT_OFFLOAD_SSP_SHIFT		19
#define SOF_BT_OFFLOAD_SSP_MASK		(GENMASK(21, 19))
#define SOF_BT_OFFLOAD_SSP(quirk)	\
	(((quirk) << SOF_BT_OFFLOAD_SSP_SHIFT) & SOF_BT_OFFLOAD_SSP_MASK)
#define SOF_SSP_BT_OFFLOAD_PRESENT		BIT(22)

/* HDMI capture*/
#define SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT  27
#define SOF_SSP_HDMI_CAPTURE_PRESENT_MASK (GENMASK(30, 27))
#define SOF_HDMI_CAPTURE_SSP_MASK(quirk)   \
	(((quirk) << SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT) & SOF_SSP_HDMI_CAPTURE_PRESENT_MASK)

/* Default: MCLK on, MCLK 19.2M, SSP0  */
static unsigned long sof_rt5682_quirk = SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0);

static int sof_rt5682_quirk_cb(const struct dmi_system_id *id)
{
	sof_rt5682_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_rt5682_quirk_table[] = {
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Circuitco"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Minnowboard Max"),
		},
		.driver_data = (void *)(SOF_RT5682_SSP_CODEC(2)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_MATCH(DMI_PRODUCT_NAME, "UP-CHT01"),
		},
		.driver_data = (void *)(SOF_RT5682_SSP_CODEC(2)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "WhiskeyLake Client"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(1)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Hatch"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Ice Lake Client"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Volteer"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98373_ALC5682I_I2S_UP4"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(2) |
					SOF_RT5682_NUM_HDMIDEV(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alder Lake Client Platform"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-ADL_MAX98373_ALC5682I_I2S"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(2) |
					SOF_RT5682_NUM_HDMIDEV(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98390_ALC5682I_I2S"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(2) |
					SOF_RT5682_NUM_HDMIDEV(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Brya"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98360_ALC5682I_I2S_AMP_SSP2"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(2) |
					SOF_RT5682_NUM_HDMIDEV(4)),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Rex"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98360_ALC5682I_I2S"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(2) |
					SOF_RT5682_SSP_AMP(0) |
					SOF_RT5682_NUM_HDMIDEV(3) |
					SOF_BT_OFFLOAD_SSP(1) |
					SOF_SSP_BT_OFFLOAD_PRESENT
					),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Rex"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-MAX98360_ALC5682I_DISCRETE_I2S_BT"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(2) |
					SOF_RT5682_SSP_AMP(0) |
					SOF_RT5682_NUM_HDMIDEV(3) |
					SOF_BT_OFFLOAD_SSP(1) |
					SOF_SSP_BT_OFFLOAD_PRESENT
					),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Rex"),
			DMI_MATCH(DMI_OEM_STRING, "AUDIO-ALC1019_ALC5682I_I2S"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(2) |
					SOF_RT5682_SSP_AMP(0) |
					SOF_RT5682_NUM_HDMIDEV(3)
					),
	},
	{
		.callback = sof_rt5682_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Google_Rex"),
		},
		.driver_data = (void *)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(2) |
					SOF_RT5682_SSP_AMP(0) |
					SOF_RT5682_NUM_HDMIDEV(3) |
					SOF_BT_OFFLOAD_SSP(1) |
					SOF_SSP_BT_OFFLOAD_PRESENT
					),
	},
	{}
};

static struct snd_soc_jack_pin jack_pins[] = {
	{
		.pin    = "Headphone Jack",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

static int sof_rt5682_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_jack *jack = &ctx->headset_jack;
	int extra_jack_data;
	int ret, mclk_freq;

	if (sof_rt5682_quirk & SOF_RT5682_MCLK_EN) {
		mclk_freq = sof_dai_get_mclk(rtd);
		if (mclk_freq <= 0) {
			dev_err(rtd->dev, "invalid mclk freq %d\n", mclk_freq);
			return -EINVAL;
		}

		/* need to enable ASRC function for 24MHz mclk rate */
		if (mclk_freq == 24000000) {
			dev_info(rtd->dev, "enable ASRC\n");

			switch (ctx->codec_type) {
			case CODEC_RT5650:
				rt5645_sel_asrc_clk_src(component,
							RT5645_DA_STEREO_FILTER |
							RT5645_AD_STEREO_FILTER,
							RT5645_CLK_SEL_I2S1_ASRC);
				rt5645_sel_asrc_clk_src(component,
							RT5645_DA_MONO_L_FILTER |
							RT5645_DA_MONO_R_FILTER,
							RT5645_CLK_SEL_I2S2_ASRC);
				break;
			case CODEC_RT5682:
				rt5682_sel_asrc_clk_src(component,
							RT5682_DA_STEREO1_FILTER |
							RT5682_AD_STEREO1_FILTER,
							RT5682_CLK_SEL_I2S1_ASRC);
				break;
			case CODEC_RT5682S:
				rt5682s_sel_asrc_clk_src(component,
							 RT5682S_DA_STEREO1_FILTER |
							 RT5682S_AD_STEREO1_FILTER,
							 RT5682S_CLK_SEL_I2S1_ASRC);
				break;
			default:
				dev_err(rtd->dev, "invalid codec type %d\n",
					ctx->codec_type);
				return -EINVAL;
			}
		}

		if (sof_rt5682_quirk & SOF_RT5682_MCLK_BYTCHT_EN) {
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
			ret = clk_prepare_enable(ctx->rt5682.mclk);
			if (!ret)
				clk_disable_unprepare(ctx->rt5682.mclk);

			ret = clk_set_rate(ctx->rt5682.mclk, 19200000);

			if (ret)
				dev_err(rtd->dev, "unable to set MCLK rate\n");
		}
	}

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 jack,
					 jack_pins,
					 ARRAY_SIZE(jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	if (ctx->codec_type == CODEC_RT5650) {
		extra_jack_data = SND_JACK_MICROPHONE | SND_JACK_BTN_0;
		ret = snd_soc_component_set_jack(component, jack, &extra_jack_data);
	} else
		ret = snd_soc_component_set_jack(component, jack, NULL);

	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return ret;
};

static void sof_rt5682_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_rt5682_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int pll_id, pll_source, pll_in, pll_out, clk_id, ret;

	if (sof_rt5682_quirk & SOF_RT5682_MCLK_EN) {
		if (sof_rt5682_quirk & SOF_RT5682_MCLK_BYTCHT_EN) {
			ret = clk_prepare_enable(ctx->rt5682.mclk);
			if (ret < 0) {
				dev_err(rtd->dev,
					"could not configure MCLK state");
				return ret;
			}
		}

		switch (ctx->codec_type) {
		case CODEC_RT5650:
			pll_source = RT5645_PLL1_S_MCLK;
			break;
		case CODEC_RT5682:
			pll_source = RT5682_PLL1_S_MCLK;
			break;
		case CODEC_RT5682S:
			pll_source = RT5682S_PLL_S_MCLK;
			break;
		default:
			dev_err(rtd->dev, "invalid codec type %d\n",
				ctx->codec_type);
			return -EINVAL;
		}

		/* get the tplg configured mclk. */
		pll_in = sof_dai_get_mclk(rtd);
		if (pll_in <= 0) {
			dev_err(rtd->dev, "invalid mclk freq %d\n", pll_in);
			return -EINVAL;
		}
	} else {
		switch (ctx->codec_type) {
		case CODEC_RT5650:
			pll_source = RT5645_PLL1_S_BCLK1;
			break;
		case CODEC_RT5682:
			pll_source = RT5682_PLL1_S_BCLK1;
			break;
		case CODEC_RT5682S:
			pll_source = RT5682S_PLL_S_BCLK1;
			break;
		default:
			dev_err(rtd->dev, "invalid codec type %d\n",
				ctx->codec_type);
			return -EINVAL;
		}

		pll_in = params_rate(params) * 50;
	}

	switch (ctx->codec_type) {
	case CODEC_RT5650:
		pll_id = 0; /* not used in codec driver */
		clk_id = RT5645_SCLK_S_PLL1;
		break;
	case CODEC_RT5682:
		pll_id = RT5682_PLL1;
		clk_id = RT5682_SCLK_S_PLL1;
		break;
	case CODEC_RT5682S:
		pll_id = RT5682S_PLL2;
		clk_id = RT5682S_SCLK_S_PLL2;
		break;
	default:
		dev_err(rtd->dev, "invalid codec type %d\n", ctx->codec_type);
		return -EINVAL;
	}

	pll_out = params_rate(params) * 512;

	/* when MCLK is 512FS, no need to set PLL configuration additionally. */
	if (pll_in == pll_out) {
		switch (ctx->codec_type) {
		case CODEC_RT5650:
			clk_id = RT5645_SCLK_S_MCLK;
			break;
		case CODEC_RT5682:
			clk_id = RT5682_SCLK_S_MCLK;
			break;
		case CODEC_RT5682S:
			clk_id = RT5682S_SCLK_S_MCLK;
			break;
		default:
			dev_err(rtd->dev, "invalid codec type %d\n",
				ctx->codec_type);
			return -EINVAL;
		}
	} else {
		/* Configure pll for codec */
		ret = snd_soc_dai_set_pll(codec_dai, pll_id, pll_source, pll_in,
					  pll_out);
		if (ret < 0)
			dev_err(rtd->dev, "snd_soc_dai_set_pll err = %d\n", ret);
	}

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, clk_id,
				     pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	/*
	 * slot_width should equal or large than data length, set them
	 * be the same
	 */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x0, 0x0, 2,
				       params_width(params));
	if (ret < 0) {
		dev_err(rtd->dev, "set TDM slot err:%d\n", ret);
		return ret;
	}

	return ret;
}

static struct snd_soc_ops sof_rt5682_ops = {
	.hw_params = sof_rt5682_hw_params,
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_dapm_context *dapm = &card->dapm;
	int err;

	if (ctx->amp_type == CODEC_MAX98373) {
		/* Disable Left and Right Spk pin after boot */
		snd_soc_dapm_disable_pin(dapm, "Left Spk");
		snd_soc_dapm_disable_pin(dapm, "Right Spk");
		err = snd_soc_dapm_sync(dapm);
		if (err < 0)
			return err;
	}

	return sof_intel_board_card_late_probe(card);
}

static const struct snd_kcontrol_new sof_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),

};

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_soc_dapm_route sof_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },

	/* other jacks */
	{ "IN1P", NULL, "Headset Mic" },
};

static const struct snd_soc_dapm_route rt5650_spk_dapm_routes[] = {
	/* speaker */
	{ "Left Spk", NULL, "SPOL" },
	{ "Right Spk", NULL, "SPOR" },
};

static int rt5650_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_add_routes(&card->dapm, rt5650_spk_dapm_routes,
				      ARRAY_SIZE(rt5650_spk_dapm_routes));
	if (ret)
		dev_err(rtd->dev, "fail to add dapm routes, ret=%d\n", ret);

	return ret;
}

/* sof audio machine driver for rt5682 codec */
static struct snd_soc_card sof_audio_card_rt5682 = {
	.name = "rt5682", /* the sof- prefix is added by the core */
	.owner = THIS_MODULE,
	.controls = sof_controls,
	.num_controls = ARRAY_SIZE(sof_controls),
	.dapm_widgets = sof_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sof_widgets),
	.dapm_routes = sof_map,
	.num_dapm_routes = ARRAY_SIZE(sof_map),
	.fully_routed = true,
	.late_probe = sof_card_late_probe,
};

static struct snd_soc_dai_link_component rt5682_component[] = {
	{
		.name = "i2c-10EC5682:00",
		.dai_name = "rt5682-aif1",
	}
};

static struct snd_soc_dai_link_component rt5682s_component[] = {
	{
		.name = "i2c-RTL5682:00",
		.dai_name = "rt5682s-aif1",
	}
};

static struct snd_soc_dai_link_component rt5650_components[] = {
	{
		.name = "i2c-10EC5650:00",
		.dai_name = "rt5645-aif1",
	},
	{
		.name = "i2c-10EC5650:00",
		.dai_name = "rt5645-aif2",
	}
};

static struct snd_soc_dai_link *
sof_card_dai_links_create(struct device *dev, enum sof_ssp_codec codec_type,
			  enum sof_ssp_codec amp_type, int ssp_codec,
			  int ssp_amp, int dmic_be_num, int hdmi_num,
			  bool idisp_codec, bool is_legacy_cpu)
{
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int i;
	int id = 0;
	int ret;
	int hdmi_id_offset = 0;

	links = devm_kcalloc(dev, sof_audio_card_rt5682.num_links,
			    sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	cpus = devm_kcalloc(dev, sof_audio_card_rt5682.num_links,
			    sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!links || !cpus)
		goto devm_err;

	/* codec SSP */
	links[id].name = devm_kasprintf(dev, GFP_KERNEL,
					"SSP%d-Codec", ssp_codec);
	if (!links[id].name)
		goto devm_err;

	links[id].id = id;

	switch (codec_type) {
	case CODEC_RT5650:
		links[id].codecs = &rt5650_components[0];
		links[id].num_codecs = 1;
		break;
	case CODEC_RT5682:
		links[id].codecs = rt5682_component;
		links[id].num_codecs = ARRAY_SIZE(rt5682_component);
		break;
	case CODEC_RT5682S:
		links[id].codecs = rt5682s_component;
		links[id].num_codecs = ARRAY_SIZE(rt5682s_component);
		break;
	default:
		dev_err(dev, "invalid codec type %d\n", codec_type);
		return NULL;
	}

	links[id].platforms = platform_component;
	links[id].num_platforms = ARRAY_SIZE(platform_component);
	links[id].init = sof_rt5682_codec_init;
	links[id].exit = sof_rt5682_codec_exit;
	links[id].ops = &sof_rt5682_ops;
	links[id].dpcm_playback = 1;
	links[id].dpcm_capture = 1;
	links[id].no_pcm = 1;
	links[id].cpus = &cpus[id];
	links[id].num_cpus = 1;
	if (is_legacy_cpu) {
		links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							  "ssp%d-port",
							  ssp_codec);
		if (!links[id].cpus->dai_name)
			goto devm_err;
	} else {
		/*
		 * Currently, On SKL+ platforms MCLK will be turned off in sof
		 * runtime suspended, and it will go into runtime suspended
		 * right after playback is stop. However, rt5682 will output
		 * static noise if sysclk turns off during playback. Set
		 * ignore_pmdown_time to power down rt5682 immediately and
		 * avoid the noise.
		 * It can be removed once we can control MCLK by driver.
		 */
		links[id].ignore_pmdown_time = 1;
		links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							  "SSP%d Pin",
							  ssp_codec);
		if (!links[id].cpus->dai_name)
			goto devm_err;
	}
	id++;

	/* dmic */
	if (dmic_be_num > 0) {
		/* at least we have dmic01 */
		ret = sof_intel_board_set_dmic_link(dev, &links[id], id,
						    SOF_DMIC_01);
		if (ret)
			return NULL;

		id++;
	}

	if (dmic_be_num > 1) {
		/* set up 2 BE links at most */
		ret = sof_intel_board_set_dmic_link(dev, &links[id], id,
						    SOF_DMIC_16K);
		if (ret)
			return NULL;

		id++;
	}

	/* HDMI */
	for (i = 1; i <= hdmi_num; i++) {
		ret = sof_intel_board_set_intel_hdmi_link(dev, &links[id], id,
							  i, idisp_codec);
		if (ret)
			return NULL;

		id++;
	}

	/* speaker amp */
	if (amp_type != CODEC_NONE) {
		links[id].name = devm_kasprintf(dev, GFP_KERNEL,
						"SSP%d-Codec", ssp_amp);
		if (!links[id].name)
			goto devm_err;

		links[id].id = id;

		switch (amp_type) {
		case CODEC_MAX98357A:
			max_98357a_dai_link(&links[id]);
			break;
		case CODEC_MAX98360A:
			max_98360a_dai_link(&links[id]);
			break;
		case CODEC_MAX98373:
			links[id].codecs = max_98373_components;
			links[id].num_codecs = ARRAY_SIZE(max_98373_components);
			links[id].init = max_98373_spk_codec_init;
			links[id].ops = &max_98373_ops;
			break;
		case CODEC_MAX98390:
			max_98390_dai_link(dev, &links[id]);
			break;
		case CODEC_RT1011:
			sof_rt1011_dai_link(&links[id]);
			break;
		case CODEC_RT1015:
			sof_rt1015_dai_link(&links[id]);
			break;
		case CODEC_RT1015P:
			sof_rt1015p_dai_link(&links[id]);
			break;
		case CODEC_RT1019P:
			sof_rt1019p_dai_link(&links[id]);
			break;
		case CODEC_RT5650:
			/* use AIF2 to support speaker pipeline */
			links[id].codecs = &rt5650_components[1];
			links[id].num_codecs = 1;
			links[id].init = rt5650_spk_init;
			links[id].ops = &sof_rt5682_ops;
			break;
		default:
			dev_err(dev, "invalid amp type %d\n", amp_type);
			return NULL;
		}

		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].dpcm_playback = 1;
		/* feedback stream or firmware-generated echo reference */
		links[id].dpcm_capture = 1;

		links[id].no_pcm = 1;
		links[id].cpus = &cpus[id];
		links[id].num_cpus = 1;
		if (is_legacy_cpu) {
			links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
								  "ssp%d-port",
								  ssp_amp);
			if (!links[id].cpus->dai_name)
				goto devm_err;

		} else {
			links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
								  "SSP%d Pin",
								  ssp_amp);
			if (!links[id].cpus->dai_name)
				goto devm_err;
		}
		id++;
	}

	/* BT audio offload */
	if (sof_rt5682_quirk & SOF_SSP_BT_OFFLOAD_PRESENT) {
		int port = (sof_rt5682_quirk & SOF_BT_OFFLOAD_SSP_MASK) >>
				SOF_BT_OFFLOAD_SSP_SHIFT;

		links[id].id = id;
		links[id].cpus = &cpus[id];
		links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
							  "SSP%d Pin", port);
		if (!links[id].cpus->dai_name)
			goto devm_err;
		links[id].name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-BT", port);
		if (!links[id].name)
			goto devm_err;
		links[id].codecs = &snd_soc_dummy_dlc;
		links[id].num_codecs = 1;
		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].dpcm_playback = 1;
		links[id].dpcm_capture = 1;
		links[id].no_pcm = 1;
		links[id].num_cpus = 1;
	}

	/* HDMI-In SSP */
	if (sof_rt5682_quirk & SOF_SSP_HDMI_CAPTURE_PRESENT_MASK) {
		unsigned long hdmi_in_ssp = (sof_rt5682_quirk &
				SOF_SSP_HDMI_CAPTURE_PRESENT_MASK) >>
				SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT;
		int port = 0;

		for_each_set_bit(port, &hdmi_in_ssp, 32) {
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

static int sof_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	struct snd_soc_dai_link *dai_links;
	struct sof_card_private *ctx;
	int ret, ssp_amp, ssp_codec;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_rt5682_quirk = (unsigned long)pdev->id_entry->driver_data;

	dmi_check_system(sof_rt5682_quirk_table);

	ctx->codec_type = sof_ssp_detect_codec_type(&pdev->dev);
	ctx->amp_type = sof_ssp_detect_amp_type(&pdev->dev);

	if (ctx->codec_type == CODEC_RT5650) {
		sof_audio_card_rt5682.name = devm_kstrdup(&pdev->dev, "rt5650",
							  GFP_KERNEL);

		/* create speaker dai link also */
		if (ctx->amp_type == CODEC_NONE)
			ctx->amp_type = CODEC_RT5650;
	}

	if (soc_intel_is_byt() || soc_intel_is_cht()) {
		ctx->rt5682.is_legacy_cpu = true;
		ctx->dmic_be_num = 0;
		/* HDMI is not supported by SOF on Baytrail/CherryTrail */
		ctx->hdmi_num = 0;
		/* default quirk for legacy cpu */
		sof_rt5682_quirk = SOF_RT5682_MCLK_EN |
						SOF_RT5682_MCLK_BYTCHT_EN |
						SOF_RT5682_SSP_CODEC(2);
	} else {
		ctx->dmic_be_num = 2;
		ctx->hdmi_num = (sof_rt5682_quirk & SOF_RT5682_NUM_HDMIDEV_MASK) >>
			 SOF_RT5682_NUM_HDMIDEV_SHIFT;
		/* default number of HDMI DAI's */
		if (!ctx->hdmi_num)
			ctx->hdmi_num = 3;

		if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
			ctx->hdmi.idisp_codec = true;
	}

	/* need to get main clock from pmc */
	if (sof_rt5682_quirk & SOF_RT5682_MCLK_BYTCHT_EN) {
		ctx->rt5682.mclk = devm_clk_get(&pdev->dev, "pmc_plt_clk_3");
		if (IS_ERR(ctx->rt5682.mclk)) {
			ret = PTR_ERR(ctx->rt5682.mclk);

			dev_err(&pdev->dev,
				"Failed to get MCLK from pmc_plt_clk_3: %d\n",
				ret);
			return ret;
		}

		ret = clk_prepare_enable(ctx->rt5682.mclk);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"could not configure MCLK state");
			return ret;
		}
	}

	dev_dbg(&pdev->dev, "sof_rt5682_quirk = %lx\n", sof_rt5682_quirk);

	ssp_amp = (sof_rt5682_quirk & SOF_RT5682_SSP_AMP_MASK) >>
			SOF_RT5682_SSP_AMP_SHIFT;

	ssp_codec = sof_rt5682_quirk & SOF_RT5682_SSP_CODEC_MASK;

	/* compute number of dai links */
	sof_audio_card_rt5682.num_links = 1 + ctx->dmic_be_num + ctx->hdmi_num;

	if (ctx->amp_type != CODEC_NONE)
		sof_audio_card_rt5682.num_links++;

	if (sof_rt5682_quirk & SOF_SSP_BT_OFFLOAD_PRESENT)
		sof_audio_card_rt5682.num_links++;

	if (sof_rt5682_quirk & SOF_SSP_HDMI_CAPTURE_PRESENT_MASK)
		sof_audio_card_rt5682.num_links +=
			hweight32((sof_rt5682_quirk & SOF_SSP_HDMI_CAPTURE_PRESENT_MASK) >>
					SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT);

	dai_links = sof_card_dai_links_create(&pdev->dev, ctx->codec_type,
					      ctx->amp_type, ssp_codec, ssp_amp,
					      ctx->dmic_be_num, ctx->hdmi_num,
					      ctx->hdmi.idisp_codec,
					      ctx->rt5682.is_legacy_cpu);
	if (!dai_links)
		return -ENOMEM;

	sof_audio_card_rt5682.dai_link = dai_links;

	/* update codec_conf */
	switch (ctx->amp_type) {
	case CODEC_MAX98373:
		max_98373_set_codec_conf(&sof_audio_card_rt5682);
		break;
	case CODEC_MAX98390:
		max_98390_set_codec_conf(&pdev->dev, &sof_audio_card_rt5682);
		break;
	case CODEC_RT1011:
		sof_rt1011_codec_conf(&sof_audio_card_rt5682);
		break;
	case CODEC_RT1015:
		sof_rt1015_codec_conf(&sof_audio_card_rt5682);
		break;
	case CODEC_RT1015P:
		sof_rt1015p_codec_conf(&sof_audio_card_rt5682);
		break;
	case CODEC_NONE:
	case CODEC_MAX98357A:
	case CODEC_MAX98360A:
	case CODEC_RT1019P:
	case CODEC_RT5650:
		/* no codec conf required */
		break;
	default:
		dev_err(&pdev->dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

	sof_audio_card_rt5682.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_audio_card_rt5682,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_audio_card_rt5682, ctx);

	return devm_snd_soc_register_card(&pdev->dev,
					  &sof_audio_card_rt5682);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof_rt5682",
	},
	{
		.name = "cml_rt1015_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1)),
	},
	{
		.name = "jsl_rt5682_rt1015",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1)),
	},
	{
		.name = "jsl_rt5682_mx98360",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1)),
	},
	{
		.name = "jsl_rt5682_rt1015p",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1)),
	},
	{
		.name = "jsl_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0)),
	},
	{
		.name = "jsl_rt5650",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1)),
	},
	{
		.name = "tgl_mx98357_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "tgl_rt1011_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "tgl_mx98373_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_mx98373_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_mx98357_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(2) |
					SOF_RT5682_NUM_HDMIDEV(4)),
	},
	{
		.name = "adl_max98390_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_mx98360_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_rt1019_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_rt5682_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(1) |
					SOF_RT5682_NUM_HDMIDEV(3) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_HDMI_CAPTURE_SSP_MASK(0x5)),
	},
	{
		.name = "adl_rt5650",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_mx98357_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(2) |
					SOF_RT5682_NUM_HDMIDEV(4)),
	},
	{
		.name = "rpl_mx98360_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_rt1019_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(4) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "rpl_rt5682_c1_h02",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(1) |
					SOF_RT5682_NUM_HDMIDEV(3) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_HDMI_CAPTURE_SSP_MASK(0x5)),
	},
	{
		.name = "mtl_mx98357_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(3) |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "mtl_mx98360_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(0) |
					SOF_RT5682_SSP_AMP(1) |
					SOF_RT5682_NUM_HDMIDEV(3)),
	},
	{
		.name = "mtl_rt1019_rt5682",
		.driver_data = (kernel_ulong_t)(SOF_RT5682_MCLK_EN |
					SOF_RT5682_SSP_CODEC(2) |
					SOF_RT5682_SSP_AMP(0) |
					SOF_RT5682_NUM_HDMIDEV(3)),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_audio = {
	.probe = sof_audio_probe,
	.driver = {
		.name = "sof_rt5682",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_audio)

/* Module information */
MODULE_DESCRIPTION("SOF Audio Machine driver");
MODULE_AUTHOR("Bard Liao <bard.liao@intel.com>");
MODULE_AUTHOR("Sathya Prakash M R <sathya.prakash.m.r@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_AUTHOR("Mac Chiang <mac.chiang@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_BOARD_HELPERS);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_MAXIM_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_REALTEK_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_SSP_COMMON);
