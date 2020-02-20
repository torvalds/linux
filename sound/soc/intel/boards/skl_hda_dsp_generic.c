// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-18 Intel Corporation.

/*
 * Machine Driver for SKL+ platforms with DSP and iDisp, HDA Codecs
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/hdac_hdmi.h"
#include "skl_hda_dsp_common.h"

static const struct snd_soc_dapm_widget skl_hda_widgets[] = {
	SND_SOC_DAPM_HP("Analog Out", NULL),
	SND_SOC_DAPM_MIC("Analog In", NULL),
	SND_SOC_DAPM_HP("Alt Analog Out", NULL),
	SND_SOC_DAPM_MIC("Alt Analog In", NULL),
	SND_SOC_DAPM_SPK("Digital Out", NULL),
	SND_SOC_DAPM_MIC("Digital In", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route skl_hda_map[] = {
	{ "hifi3", NULL, "iDisp3 Tx"},
	{ "iDisp3 Tx", NULL, "iDisp3_out"},
	{ "hifi2", NULL, "iDisp2 Tx"},
	{ "iDisp2 Tx", NULL, "iDisp2_out"},
	{ "hifi1", NULL, "iDisp1 Tx"},
	{ "iDisp1 Tx", NULL, "iDisp1_out"},

	{ "Analog Out", NULL, "Codec Output Pin1" },
	{ "Digital Out", NULL, "Codec Output Pin2" },
	{ "Alt Analog Out", NULL, "Codec Output Pin3" },

	{ "Codec Input Pin1", NULL, "Analog In" },
	{ "Codec Input Pin2", NULL, "Digital In" },
	{ "Codec Input Pin3", NULL, "Alt Analog In" },

	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},

	/* CODEC BE connections */
	{ "Analog Codec Playback", NULL, "Analog CPU Playback" },
	{ "Analog CPU Playback", NULL, "codec0_out" },
	{ "Digital Codec Playback", NULL, "Digital CPU Playback" },
	{ "Digital CPU Playback", NULL, "codec1_out" },
	{ "Alt Analog Codec Playback", NULL, "Alt Analog CPU Playback" },
	{ "Alt Analog CPU Playback", NULL, "codec2_out" },

	{ "codec0_in", NULL, "Analog CPU Capture" },
	{ "Analog CPU Capture", NULL, "Analog Codec Capture" },
	{ "codec1_in", NULL, "Digital CPU Capture" },
	{ "Digital CPU Capture", NULL, "Digital Codec Capture" },
	{ "codec2_in", NULL, "Alt Analog CPU Capture" },
	{ "Alt Analog CPU Capture", NULL, "Alt Analog Codec Capture" },
};

SND_SOC_DAILINK_DEF(dummy_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("snd-soc-dummy", "snd-soc-dummy-dai")));

static int skl_hda_card_late_probe(struct snd_soc_card *card)
{
	return skl_hda_hdmi_jack_init(card);
}

static int
skl_hda_add_dai_link(struct snd_soc_card *card, struct snd_soc_dai_link *link)
{
	struct skl_hda_private *ctx = snd_soc_card_get_drvdata(card);
	int ret = 0;

	dev_dbg(card->dev, "%s: dai link name - %s\n", __func__, link->name);
	link->platforms->name = ctx->platform_name;
	link->nonatomic = 1;

	if (strstr(link->name, "HDMI")) {
		ret = skl_hda_hdmi_add_pcm(card, ctx->pcm_count);

		if (ret < 0)
			return ret;

		ctx->dai_index++;
	}

	ctx->pcm_count++;
	return ret;
}

static struct snd_soc_card hda_soc_card = {
	.name = "hda-dsp",
	.owner = THIS_MODULE,
	.dai_link = skl_hda_be_dai_links,
	.dapm_widgets = skl_hda_widgets,
	.dapm_routes = skl_hda_map,
	.add_dai_link = skl_hda_add_dai_link,
	.fully_routed = true,
	.late_probe = skl_hda_card_late_probe,
};

static char hda_soc_components[30];

#define IDISP_DAI_COUNT		3
#define HDAC_DAI_COUNT		2
#define DMIC_DAI_COUNT		2

/* there are two routes per iDisp output */
#define IDISP_ROUTE_COUNT	(IDISP_DAI_COUNT * 2)
#define IDISP_CODEC_MASK	0x4

static int skl_hda_fill_card_info(struct snd_soc_acpi_mach_params *mach_params)
{
	struct snd_soc_card *card = &hda_soc_card;
	struct snd_soc_dai_link *dai_link;
	u32 codec_count, codec_mask, idisp_mask;
	int i, num_links, num_route;

	codec_mask = mach_params->codec_mask;
	codec_count = hweight_long(codec_mask);
	idisp_mask = codec_mask & IDISP_CODEC_MASK;

	if (!codec_count || codec_count > 2 ||
	    (codec_count == 2 && !idisp_mask))
		return -EINVAL;

	if (codec_mask == idisp_mask) {
		/* topology with iDisp as the only HDA codec */
		num_links = IDISP_DAI_COUNT + DMIC_DAI_COUNT;
		num_route = IDISP_ROUTE_COUNT;

		/*
		 * rearrange the dai link array and make the
		 * dmic dai links follow idsp dai links for only
		 * num_links of dai links need to be registered
		 * to ASoC.
		 */
		for (i = 0; i < DMIC_DAI_COUNT; i++) {
			skl_hda_be_dai_links[IDISP_DAI_COUNT + i] =
				skl_hda_be_dai_links[IDISP_DAI_COUNT +
					HDAC_DAI_COUNT + i];
		}
	} else {
		/* topology with external and iDisp HDA codecs */
		num_links = ARRAY_SIZE(skl_hda_be_dai_links);
		num_route = ARRAY_SIZE(skl_hda_map);
		card->dapm_widgets = skl_hda_widgets;
		card->num_dapm_widgets = ARRAY_SIZE(skl_hda_widgets);
		if (!idisp_mask) {
			for (i = 0; i < IDISP_DAI_COUNT; i++) {
				skl_hda_be_dai_links[i].codecs = dummy_codec;
				skl_hda_be_dai_links[i].num_codecs =
					ARRAY_SIZE(dummy_codec);
			}
		}
	}

	card->num_links = num_links;
	card->num_dapm_routes = num_route;

	for_each_card_prelinks(card, i, dai_link)
		dai_link->platforms->name = mach_params->platform;

	return 0;
}

static int skl_hda_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	struct skl_hda_private *ctx;
	int ret;

	dev_dbg(&pdev->dev, "%s: entry\n", __func__);

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	mach = (&pdev->dev)->platform_data;
	if (!mach)
		return -EINVAL;

	ret = skl_hda_fill_card_info(&mach->mach_params);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unsupported HDAudio/iDisp configuration found\n");
		return ret;
	}

	ctx->pcm_count = hda_soc_card.num_links;
	ctx->dai_index = 1; /* hdmi codec dai name starts from index 1 */
	ctx->platform_name = mach->mach_params.platform;
	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	hda_soc_card.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&hda_soc_card, ctx);

	if (mach->mach_params.dmic_num > 0) {
		snprintf(hda_soc_components, sizeof(hda_soc_components),
				"cfg-dmics:%d", mach->mach_params.dmic_num);
		hda_soc_card.components = hda_soc_components;
	}

	return devm_snd_soc_register_card(&pdev->dev, &hda_soc_card);
}

static struct platform_driver skl_hda_audio = {
	.probe = skl_hda_audio_probe,
	.driver = {
		.name = "skl_hda_dsp_generic",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(skl_hda_audio)

/* Module information */
MODULE_DESCRIPTION("SKL/KBL/BXT/APL HDA Generic Machine driver");
MODULE_AUTHOR("Rakesh Ughreja <rakesh.a.ughreja@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:skl_hda_dsp_generic");
