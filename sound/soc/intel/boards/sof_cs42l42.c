// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2021 Intel Corporation.

/*
 * Intel SOF Machine Driver with Cirrus Logic CS42L42 Codec
 * and speaker codec MAX98357A
 */
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/dmi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/soc-acpi.h>
#include <dt-bindings/sound/cs42l42.h>
#include "../common/soc-intel-quirks.h"
#include "sof_board_helpers.h"
#include "sof_maxim_common.h"
#include "sof_ssp_common.h"

#define SOF_CS42L42_SSP_CODEC(quirk)		((quirk) & GENMASK(2, 0))
#define SOF_CS42L42_SSP_CODEC_MASK		(GENMASK(2, 0))
#define SOF_CS42L42_SSP_AMP_SHIFT		4
#define SOF_CS42L42_SSP_AMP_MASK		(GENMASK(6, 4))
#define SOF_CS42L42_SSP_AMP(quirk)	\
	(((quirk) << SOF_CS42L42_SSP_AMP_SHIFT) & SOF_CS42L42_SSP_AMP_MASK)
#define SOF_CS42L42_NUM_HDMIDEV_SHIFT		7
#define SOF_CS42L42_NUM_HDMIDEV_MASK		(GENMASK(9, 7))
#define SOF_CS42L42_NUM_HDMIDEV(quirk)	\
	(((quirk) << SOF_CS42L42_NUM_HDMIDEV_SHIFT) & SOF_CS42L42_NUM_HDMIDEV_MASK)
#define SOF_CS42L42_DAILINK_SHIFT		10
#define SOF_CS42L42_DAILINK_MASK		(GENMASK(24, 10))
#define SOF_CS42L42_DAILINK(link1, link2, link3, link4, link5) \
	((((link1) | ((link2) << 3) | ((link3) << 6) | ((link4) << 9) | ((link5) << 12)) << SOF_CS42L42_DAILINK_SHIFT) & SOF_CS42L42_DAILINK_MASK)
#define SOF_BT_OFFLOAD_PRESENT			BIT(25)
#define SOF_CS42L42_SSP_BT_SHIFT		26
#define SOF_CS42L42_SSP_BT_MASK			(GENMASK(28, 26))
#define SOF_CS42L42_SSP_BT(quirk)	\
	(((quirk) << SOF_CS42L42_SSP_BT_SHIFT) & SOF_CS42L42_SSP_BT_MASK)

enum {
	LINK_NONE = 0,
	LINK_HP = 1,
	LINK_SPK = 2,
	LINK_DMIC = 3,
	LINK_HDMI = 4,
	LINK_BT = 5,
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

/* Default: SSP2 */
static unsigned long sof_cs42l42_quirk = SOF_CS42L42_SSP_CODEC(2);

static int sof_cs42l42_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_jack *jack = &ctx->headset_jack;
	int ret;

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
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return ret;
};

static void sof_cs42l42_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_cs42l42_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int clk_freq, ret;

	clk_freq = sof_dai_get_bclk(rtd); /* BCLK freq */

	if (clk_freq <= 0) {
		dev_err(rtd->dev, "get bclk freq failed: %d\n", clk_freq);
		return -EINVAL;
	}

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0,
				     clk_freq, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	return ret;
}

static const struct snd_soc_ops sof_cs42l42_ops = {
	.hw_params = sof_cs42l42_hw_params,
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	return sof_intel_board_card_late_probe(card);
}

static const struct snd_kcontrol_new sof_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route sof_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{"Headphone Jack", NULL, "HP"},

	/* other jacks */
	{"HS", NULL, "Headset Mic"},
};

/* sof audio machine driver for cs42l42 codec */
static struct snd_soc_card sof_audio_card_cs42l42 = {
	.name = "cs42l42", /* the sof- prefix is added by the core */
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

static struct snd_soc_dai_link_component cs42l42_component[] = {
	{
		.name = "i2c-10134242:00",
		.dai_name = "cs42l42",
	}
};

static struct snd_soc_dai_link *
sof_card_dai_links_create(struct device *dev, enum sof_ssp_codec amp_type,
			  int ssp_codec, int ssp_amp, int ssp_bt,
			  int dmic_be_num, int hdmi_num, bool idisp_codec)
{
	struct snd_soc_dai_link *links;
	int ret;
	int id = 0;
	int link_seq;
	int i;

	links = devm_kcalloc(dev, sof_audio_card_cs42l42.num_links,
			    sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	if (!links)
		goto devm_err;

	link_seq = (sof_cs42l42_quirk & SOF_CS42L42_DAILINK_MASK) >> SOF_CS42L42_DAILINK_SHIFT;

	while (link_seq) {
		int link_type = link_seq & 0x07;

		switch (link_type) {
		case LINK_HP:
			ret = sof_intel_board_set_codec_link(dev, &links[id], id,
							     CODEC_CS42L42,
							     ssp_codec);
			if (ret) {
				dev_err(dev, "fail to create hp codec dai links, ret %d\n",
					ret);
				goto devm_err;
			}

			/* codec-specific fields */
			links[id].codecs = cs42l42_component;
			links[id].num_codecs = ARRAY_SIZE(cs42l42_component);
			links[id].init = sof_cs42l42_init;
			links[id].exit = sof_cs42l42_exit;
			links[id].ops = &sof_cs42l42_ops;

			id++;
			break;
		case LINK_SPK:
			if (amp_type != CODEC_NONE) {
				ret = sof_intel_board_set_ssp_amp_link(dev,
								       &links[id],
								       id,
								       amp_type,
								       ssp_amp);
				if (ret) {
					dev_err(dev, "fail to create spk amp dai links, ret %d\n",
						ret);
					goto devm_err;
				}

				/* codec-specific fields */
				switch (amp_type) {
				case CODEC_MAX98357A:
					max_98357a_dai_link(&links[id]);
					break;
				case CODEC_MAX98360A:
					max_98360a_dai_link(&links[id]);
					break;
				default:
					dev_err(dev, "invalid amp type %d\n",
						amp_type);
					goto devm_err;
				}

				id++;
			}
			break;
		case LINK_DMIC:
			if (dmic_be_num > 0) {
				/* at least we have dmic01 */
				ret = sof_intel_board_set_dmic_link(dev,
								    &links[id],
								    id,
								    SOF_DMIC_01);
				if (ret) {
					dev_err(dev, "fail to create dmic01 link, ret %d\n",
						ret);
					goto devm_err;
				}

				id++;
			}

			if (dmic_be_num > 1) {
				/* set up 2 BE links at most */
				ret = sof_intel_board_set_dmic_link(dev,
								    &links[id],
								    id,
								    SOF_DMIC_16K);
				if (ret) {
					dev_err(dev, "fail to create dmic16k link, ret %d\n",
						ret);
					goto devm_err;
				}

				id++;
			}
			break;
		case LINK_HDMI:
			for (i = 1; i <= hdmi_num; i++) {
				ret = sof_intel_board_set_intel_hdmi_link(dev,
									  &links[id],
									  id, i,
									  idisp_codec);
				if (ret) {
					dev_err(dev, "fail to create hdmi link, ret %d\n",
						ret);
					goto devm_err;
				}

				id++;
			}
			break;
		case LINK_BT:
			if (sof_cs42l42_quirk & SOF_BT_OFFLOAD_PRESENT) {
				ret = sof_intel_board_set_bt_link(dev,
								  &links[id], id,
								  ssp_bt);
				if (ret) {
					dev_err(dev, "fail to create bt offload dai links, ret %d\n",
						ret);
					goto devm_err;
				}

				id++;
			}
			break;
		case LINK_NONE:
			/* caught here if it's not used as terminator in macro */
		default:
			dev_err(dev, "invalid link type %d\n", link_type);
			goto devm_err;
		}

		link_seq >>= 3;
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
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_cs42l42_quirk = (unsigned long)pdev->id_entry->driver_data;

	ctx->codec_type = sof_ssp_detect_codec_type(&pdev->dev);
	ctx->amp_type = sof_ssp_detect_amp_type(&pdev->dev);

	if (soc_intel_is_glk()) {
		ctx->dmic_be_num = 1;
		ctx->hdmi_num = 3;
	} else {
		ctx->dmic_be_num = 2;
		ctx->hdmi_num = (sof_cs42l42_quirk & SOF_CS42L42_NUM_HDMIDEV_MASK) >>
			 SOF_CS42L42_NUM_HDMIDEV_SHIFT;
		/* default number of HDMI DAI's */
		if (!ctx->hdmi_num)
			ctx->hdmi_num = 3;
	}

	if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
		ctx->hdmi.idisp_codec = true;

	dev_dbg(&pdev->dev, "sof_cs42l42_quirk = %lx\n", sof_cs42l42_quirk);

	/* port number of peripherals attached to ssp interface */
	ctx->ssp_bt = (sof_cs42l42_quirk & SOF_CS42L42_SSP_BT_MASK) >>
			SOF_CS42L42_SSP_BT_SHIFT;

	ctx->ssp_amp = (sof_cs42l42_quirk & SOF_CS42L42_SSP_AMP_MASK) >>
			SOF_CS42L42_SSP_AMP_SHIFT;

	ctx->ssp_codec = sof_cs42l42_quirk & SOF_CS42L42_SSP_CODEC_MASK;

	/* compute number of dai links */
	sof_audio_card_cs42l42.num_links = 1 + ctx->dmic_be_num + ctx->hdmi_num;

	if (ctx->amp_type != CODEC_NONE)
		sof_audio_card_cs42l42.num_links++;
	if (sof_cs42l42_quirk & SOF_BT_OFFLOAD_PRESENT) {
		ctx->bt_offload_present = true;
		sof_audio_card_cs42l42.num_links++;
	}

	dai_links = sof_card_dai_links_create(&pdev->dev, ctx->amp_type,
					      ctx->ssp_codec, ctx->ssp_amp,
					      ctx->ssp_bt, ctx->dmic_be_num,
					      ctx->hdmi_num,
					      ctx->hdmi.idisp_codec);
	if (!dai_links)
		return -ENOMEM;

	sof_audio_card_cs42l42.dai_link = dai_links;

	sof_audio_card_cs42l42.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_audio_card_cs42l42,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_audio_card_cs42l42, ctx);

	return devm_snd_soc_register_card(&pdev->dev,
					  &sof_audio_card_cs42l42);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "glk_cs4242_mx98357a",
		.driver_data = (kernel_ulong_t)(SOF_CS42L42_SSP_CODEC(2) |
					SOF_CS42L42_SSP_AMP(1)) |
					SOF_CS42L42_DAILINK(LINK_SPK, LINK_HP, LINK_DMIC, LINK_HDMI, LINK_NONE),
	},
	{
		.name = "jsl_cs4242_mx98360a",
		.driver_data = (kernel_ulong_t)(SOF_CS42L42_SSP_CODEC(0) |
					SOF_CS42L42_SSP_AMP(1)) |
					SOF_CS42L42_DAILINK(LINK_HP, LINK_DMIC, LINK_HDMI, LINK_SPK, LINK_NONE),
	},
	{
		.name = "adl_mx98360a_cs4242",
		.driver_data = (kernel_ulong_t)(SOF_CS42L42_SSP_CODEC(0) |
				SOF_CS42L42_SSP_AMP(1) |
				SOF_CS42L42_NUM_HDMIDEV(4) |
				SOF_BT_OFFLOAD_PRESENT |
				SOF_CS42L42_SSP_BT(2) |
				SOF_CS42L42_DAILINK(LINK_HP, LINK_DMIC, LINK_HDMI, LINK_SPK, LINK_BT)),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_audio = {
	.probe = sof_audio_probe,
	.driver = {
		.name = "sof_cs42l42",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_audio)

/* Module information */
MODULE_DESCRIPTION("SOF Audio Machine driver for CS42L42");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_BOARD_HELPERS);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_MAXIM_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_SSP_COMMON);
