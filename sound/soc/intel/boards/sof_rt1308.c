// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

/*
 * sof_rt1308.c - ASoc Machine driver for Intel platforms
 * with RT1308 codec.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sof.h>
#include "sof_realtek_common.h"

#define SOF_RT1308_SSP_CODEC(quirk)		((quirk) & GENMASK(3, 0))
#define SOF_RT1308_SSP_CODEC_MASK			(GENMASK(3, 0))

/* HDMI capture*/
#define SOF_SSP_HDMI_CAPTURE_PRESENT		BIT(4)
#define SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT		5
#define SOF_NO_OF_HDMI_CAPTURE_SSP_MASK		(GENMASK(6, 5))
#define SOF_NO_OF_HDMI_CAPTURE_SSP(quirk)	\
	(((quirk) << SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT) & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK)

#define SOF_HDMI_CAPTURE_1_SSP_SHIFT		7
#define SOF_HDMI_CAPTURE_1_SSP_MASK		(GENMASK(9, 7))
#define SOF_HDMI_CAPTURE_1_SSP(quirk)	\
	(((quirk) << SOF_HDMI_CAPTURE_1_SSP_SHIFT) & SOF_HDMI_CAPTURE_1_SSP_MASK)

#define SOF_HDMI_CAPTURE_2_SSP_SHIFT		10
#define SOF_HDMI_CAPTURE_2_SSP_MASK		(GENMASK(12, 10))
#define SOF_HDMI_CAPTURE_2_SSP(quirk)	\
	(((quirk) << SOF_HDMI_CAPTURE_2_SSP_SHIFT) & SOF_HDMI_CAPTURE_2_SSP_MASK)

#define SOF_RT1308_SPEAKER_AMP_PRESENT		BIT(13)

/* Default: SSP2  */
static unsigned long sof_rt1308_quirk = SOF_RT1308_SSP_CODEC(2);

static const struct snd_soc_dapm_widget sof_rt1308_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route sof_rt1308_dapm_routes[] = {
	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
};

static struct snd_soc_card sof_rt1308_card = {
	.name         = "rt1308",
	.owner        = THIS_MODULE,
	.dapm_widgets = sof_rt1308_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sof_rt1308_dapm_widgets),
	.dapm_routes = sof_rt1308_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(sof_rt1308_dapm_routes),
	.fully_routed = true,
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

static struct snd_soc_dai_link_component dmic_component[] = {
	{
		.name = "dmic-codec",
		.dai_name = "dmic-hifi",
	}
};

static struct snd_soc_dai_link_component dummy_component[] = {
	{
		.name = "snd-soc-dummy",
		.dai_name = "snd-soc-dummy-dai",
	}
};

static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev,
							  int ssp_codec,
							  int dmic_be_num)
{
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int i, id = 0;

	links = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link) *
					sof_rt1308_card.num_links, GFP_KERNEL);
	cpus = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link_component) *
					sof_rt1308_card.num_links, GFP_KERNEL);
	if (!links || !cpus)
		return NULL;

	/* HDMI-In SSP */
	if (sof_rt1308_quirk & SOF_SSP_HDMI_CAPTURE_PRESENT) {
		int num_of_hdmi_ssp = (sof_rt1308_quirk & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK) >>
				SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT;

		for (i = 1; i <= num_of_hdmi_ssp; i++) {
			int port = (i == 1 ? (sof_rt1308_quirk & SOF_HDMI_CAPTURE_1_SSP_MASK) >>
						SOF_HDMI_CAPTURE_1_SSP_SHIFT :
						(sof_rt1308_quirk & SOF_HDMI_CAPTURE_2_SSP_MASK) >>
						SOF_HDMI_CAPTURE_2_SSP_SHIFT);

			links[id].cpus = &cpus[id];
			links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
								  "SSP%d Pin", port);
			if (!links[id].cpus->dai_name)
				return NULL;
			links[id].name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-HDMI", port);
			if (!links[id].name)
				return NULL;
			links[id].id = id;
			links[id].codecs = dummy_component;
			links[id].num_codecs = ARRAY_SIZE(dummy_component);
			links[id].platforms = platform_component;
			links[id].num_platforms = ARRAY_SIZE(platform_component);
			links[id].dpcm_capture = 1;
			links[id].no_pcm = 1;
			links[id].num_cpus = 1;
			id++;
		}
	}

	/* codec SSP */
	links[id].name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec", ssp_codec);
	if (!links[id].name)
		return NULL;

	links[id].id = id;
	if (sof_rt1308_quirk & SOF_RT1308_SPEAKER_AMP_PRESENT) {
		sof_rt1308_dai_link(&links[id]);
	}
	links[id].platforms = platform_component;
	links[id].num_platforms = ARRAY_SIZE(platform_component);
	links[id].dpcm_playback = 1;
	links[id].no_pcm = 1;
	links[id].cpus = &cpus[id];
	links[id].num_cpus = 1;
	links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", ssp_codec);
	if (!links[id].cpus->dai_name)
		return NULL;

	id++;

	/* dmic */
	if (dmic_be_num > 0) {
		/* at least we have dmic01 */
		links[id].name = "dmic01";
		links[id].cpus = &cpus[id];
		links[id].cpus->dai_name = "DMIC01 Pin";
		if (dmic_be_num > 1) {
			/* set up 2 BE links at most */
			links[id + 1].name = "dmic16k";
			links[id + 1].cpus = &cpus[id + 1];
			links[id + 1].cpus->dai_name = "DMIC16k Pin";
			dmic_be_num = 2;
		}
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

	return links;
}

static int sof_rt1308_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_links;
	struct snd_soc_acpi_mach *mach;
	int dmic_be_num;
	int ret, ssp_codec;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_rt1308_quirk = (unsigned long)pdev->id_entry->driver_data;

	mach = pdev->dev.platform_data;

	dmic_be_num = mach->mach_params.dmic_num;

	ssp_codec = sof_rt1308_quirk & SOF_RT1308_SSP_CODEC_MASK;

	/* set number of dai links */
	sof_rt1308_card.num_links = 1 + dmic_be_num;

	if (sof_rt1308_quirk & SOF_SSP_HDMI_CAPTURE_PRESENT)
		sof_rt1308_card.num_links += (sof_rt1308_quirk & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK) >>
				SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT;

	dai_links = sof_card_dai_links_create(&pdev->dev, ssp_codec, dmic_be_num);
	if (!dai_links)
		return -ENOMEM;

	sof_rt1308_card.dai_link = dai_links;

	sof_rt1308_card.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_rt1308_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_rt1308_card, NULL);

	return devm_snd_soc_register_card(&pdev->dev, &sof_rt1308_card);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof_rt1308",
	},
	{
		.name = "tgl_rt1308_hdmi_ssp",
		.driver_data = (kernel_ulong_t)(SOF_RT1308_SSP_CODEC(2) |
					SOF_NO_OF_HDMI_CAPTURE_SSP(2) |
					SOF_HDMI_CAPTURE_1_SSP(1) |
					SOF_HDMI_CAPTURE_2_SSP(5) |
					SOF_SSP_HDMI_CAPTURE_PRESENT |
					SOF_RT1308_SPEAKER_AMP_PRESENT),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_rt1308_driver = {
	.probe          = sof_rt1308_probe,
	.driver = {
		.name   = "sof_rt1308",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_rt1308_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF + RT1308 Machine driver");
MODULE_AUTHOR("balamurugan.c <balamurugan.c@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_REALTEK_COMMON);
