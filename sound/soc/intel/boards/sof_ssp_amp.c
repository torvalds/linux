// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

/*
 * sof_ssp_amp.c - ASoc Machine driver for Intel platforms
 * with RT1308/CS35L41 codec.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sof.h>
#include "../../codecs/hdac_hdmi.h"
#include "hda_dsp_common.h"
#include "sof_realtek_common.h"
#include "sof_cirrus_common.h"

#define NAME_SIZE 32

/* SSP port ID for speaker amplifier */
#define SOF_AMPLIFIER_SSP(quirk)		((quirk) & GENMASK(3, 0))
#define SOF_AMPLIFIER_SSP_MASK			(GENMASK(3, 0))

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

/* HDMI playback */
#define SOF_HDMI_PLAYBACK_PRESENT		BIT(13)
#define SOF_NO_OF_HDMI_PLAYBACK_SHIFT		14
#define SOF_NO_OF_HDMI_PLAYBACK_MASK		(GENMASK(16, 14))
#define SOF_NO_OF_HDMI_PLAYBACK(quirk)	\
	(((quirk) << SOF_NO_OF_HDMI_PLAYBACK_SHIFT) & SOF_NO_OF_HDMI_PLAYBACK_MASK)

/* BT audio offload */
#define SOF_SSP_BT_OFFLOAD_PRESENT		BIT(17)
#define SOF_BT_OFFLOAD_SSP_SHIFT		18
#define SOF_BT_OFFLOAD_SSP_MASK			(GENMASK(20, 18))
#define SOF_BT_OFFLOAD_SSP(quirk)	\
	(((quirk) << SOF_BT_OFFLOAD_SSP_SHIFT) & SOF_BT_OFFLOAD_SSP_MASK)

/* Speaker amplifiers */
#define SOF_RT1308_SPEAKER_AMP_PRESENT		BIT(21)
#define SOF_CS35L41_SPEAKER_AMP_PRESENT		BIT(22)

/* Default: SSP2  */
static unsigned long sof_ssp_amp_quirk = SOF_AMPLIFIER_SSP(2);

struct sof_hdmi_pcm {
	struct list_head head;
	struct snd_soc_jack sof_hdmi;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct sof_card_private {
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
	bool idisp_codec;
};

static const struct dmi_system_id chromebook_platforms[] = {
	{
		.ident = "Google Chromebooks",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
		}
	},
	{},
};

static const struct snd_soc_dapm_widget sof_ssp_amp_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route sof_ssp_amp_dapm_routes[] = {
	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component = NULL;
	char jack_name[NAME_SIZE];
	struct sof_hdmi_pcm *pcm;
	int err;
	int i;

	if (!(sof_ssp_amp_quirk & SOF_HDMI_PLAYBACK_PRESENT))
		return 0;

	/* HDMI is not supported by SOF on Baytrail/CherryTrail */
	if (!ctx->idisp_codec)
		return 0;

	if (list_empty(&ctx->hdmi_pcm_list))
		return -EINVAL;

	if (ctx->common_hdmi_codec_drv) {
		pcm = list_first_entry(&ctx->hdmi_pcm_list, struct sof_hdmi_pcm,
				       head);
		component = pcm->codec_dai->component;
		return hda_dsp_hdmi_build_controls(card, component);
	}

	i = 0;
	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &pcm->sof_hdmi);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &pcm->sof_hdmi);
		if (err < 0)
			return err;

		i++;
	}

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}

static struct snd_soc_card sof_ssp_amp_card = {
	.name         = "ssp_amp",
	.owner        = THIS_MODULE,
	.dapm_widgets = sof_ssp_amp_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(sof_ssp_amp_dapm_widgets),
	.dapm_routes = sof_ssp_amp_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(sof_ssp_amp_dapm_routes),
	.fully_routed = true,
	.late_probe = sof_card_late_probe,
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

static int sof_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = asoc_rtd_to_codec(rtd, 0);
	struct sof_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	/* dai_link id is 1:1 mapped to the PCM device */
	pcm->device = rtd->dai_link->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

#define IDISP_CODEC_MASK	0x4

static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev,
							  int ssp_codec,
							  int dmic_be_num,
							  int hdmi_num,
							  bool idisp_codec)
{
	struct snd_soc_dai_link_component *idisp_components;
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int i, id = 0;

	links = devm_kcalloc(dev, sof_ssp_amp_card.num_links,
					sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	cpus = devm_kcalloc(dev, sof_ssp_amp_card.num_links,
					sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!links || !cpus)
		return NULL;

	/* HDMI-In SSP */
	if (sof_ssp_amp_quirk & SOF_SSP_HDMI_CAPTURE_PRESENT) {
		int num_of_hdmi_ssp = (sof_ssp_amp_quirk & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK) >>
				SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT;

		for (i = 1; i <= num_of_hdmi_ssp; i++) {
			int port = (i == 1 ? (sof_ssp_amp_quirk & SOF_HDMI_CAPTURE_1_SSP_MASK) >>
						SOF_HDMI_CAPTURE_1_SSP_SHIFT :
						(sof_ssp_amp_quirk & SOF_HDMI_CAPTURE_2_SSP_MASK) >>
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
	if (sof_ssp_amp_quirk & SOF_RT1308_SPEAKER_AMP_PRESENT) {
		sof_rt1308_dai_link(&links[id]);
	} else if (sof_ssp_amp_quirk & SOF_CS35L41_SPEAKER_AMP_PRESENT) {
		cs35l41_set_dai_link(&links[id]);
	}
	links[id].platforms = platform_component;
	links[id].num_platforms = ARRAY_SIZE(platform_component);
	links[id].dpcm_playback = 1;
	/* feedback from amplifier or firmware-generated echo reference */
	links[id].dpcm_capture = 1;
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

	/* HDMI playback */
	if (sof_ssp_amp_quirk & SOF_HDMI_PLAYBACK_PRESENT) {
		/* HDMI */
		if (hdmi_num > 0) {
			idisp_components = devm_kcalloc(dev,
					   hdmi_num,
					   sizeof(struct snd_soc_dai_link_component),
					   GFP_KERNEL);
			if (!idisp_components)
				goto devm_err;
		}
		for (i = 1; i <= hdmi_num; i++) {
			links[id].name = devm_kasprintf(dev, GFP_KERNEL,
							"iDisp%d", i);
			if (!links[id].name)
				goto devm_err;

			links[id].id = id;
			links[id].cpus = &cpus[id];
			links[id].num_cpus = 1;
			links[id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
								  "iDisp%d Pin", i);
			if (!links[id].cpus->dai_name)
				goto devm_err;

			if (idisp_codec) {
				idisp_components[i - 1].name = "ehdaudio0D2";
				idisp_components[i - 1].dai_name = devm_kasprintf(dev,
										  GFP_KERNEL,
										  "intel-hdmi-hifi%d",
										  i);
				if (!idisp_components[i - 1].dai_name)
					goto devm_err;
			} else {
				idisp_components[i - 1].name = "snd-soc-dummy";
				idisp_components[i - 1].dai_name = "snd-soc-dummy-dai";
			}

			links[id].codecs = &idisp_components[i - 1];
			links[id].num_codecs = 1;
			links[id].platforms = platform_component;
			links[id].num_platforms = ARRAY_SIZE(platform_component);
			links[id].init = sof_hdmi_init;
			links[id].dpcm_playback = 1;
			links[id].no_pcm = 1;
			id++;
		}
	}

	/* BT audio offload */
	if (sof_ssp_amp_quirk & SOF_SSP_BT_OFFLOAD_PRESENT) {
		int port = (sof_ssp_amp_quirk & SOF_BT_OFFLOAD_SSP_MASK) >>
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
		links[id].codecs = dummy_component;
		links[id].num_codecs = ARRAY_SIZE(dummy_component);
		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].dpcm_playback = 1;
		links[id].dpcm_capture = 1;
		links[id].no_pcm = 1;
		links[id].num_cpus = 1;
		id++;
	}

	return links;
devm_err:
	return NULL;
}

static int sof_ssp_amp_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *dai_links;
	struct snd_soc_acpi_mach *mach;
	struct sof_card_private *ctx;
	int dmic_be_num = 0, hdmi_num = 0;
	int ret, ssp_codec;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_ssp_amp_quirk = (unsigned long)pdev->id_entry->driver_data;

	mach = pdev->dev.platform_data;

	if (dmi_check_system(chromebook_platforms) || mach->mach_params.dmic_num > 0)
		dmic_be_num = 2;

	ssp_codec = sof_ssp_amp_quirk & SOF_AMPLIFIER_SSP_MASK;

	/* set number of dai links */
	sof_ssp_amp_card.num_links = 1 + dmic_be_num;

	if (sof_ssp_amp_quirk & SOF_SSP_HDMI_CAPTURE_PRESENT)
		sof_ssp_amp_card.num_links += (sof_ssp_amp_quirk & SOF_NO_OF_HDMI_CAPTURE_SSP_MASK) >>
				SOF_NO_OF_HDMI_CAPTURE_SSP_SHIFT;

	if (sof_ssp_amp_quirk & SOF_HDMI_PLAYBACK_PRESENT) {
		hdmi_num = (sof_ssp_amp_quirk & SOF_NO_OF_HDMI_PLAYBACK_MASK) >>
				SOF_NO_OF_HDMI_PLAYBACK_SHIFT;
		/* default number of HDMI DAI's */
		if (!hdmi_num)
			hdmi_num = 3;

		if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
			ctx->idisp_codec = true;

		sof_ssp_amp_card.num_links += hdmi_num;
	}

	if (sof_ssp_amp_quirk & SOF_SSP_BT_OFFLOAD_PRESENT)
		sof_ssp_amp_card.num_links++;

	dai_links = sof_card_dai_links_create(&pdev->dev, ssp_codec, dmic_be_num, hdmi_num, ctx->idisp_codec);
	if (!dai_links)
		return -ENOMEM;

	sof_ssp_amp_card.dai_link = dai_links;

	/* update codec_conf */
	if (sof_ssp_amp_quirk & SOF_CS35L41_SPEAKER_AMP_PRESENT) {
		cs35l41_set_codec_conf(&sof_ssp_amp_card);
	}

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	sof_ssp_amp_card.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_ssp_amp_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	snd_soc_card_set_drvdata(&sof_ssp_amp_card, ctx);

	return devm_snd_soc_register_card(&pdev->dev, &sof_ssp_amp_card);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof_ssp_amp",
	},
	{
		.name = "tgl_rt1308_hdmi_ssp",
		.driver_data = (kernel_ulong_t)(SOF_AMPLIFIER_SSP(2) |
					SOF_NO_OF_HDMI_CAPTURE_SSP(2) |
					SOF_HDMI_CAPTURE_1_SSP(1) |
					SOF_HDMI_CAPTURE_2_SSP(5) |
					SOF_SSP_HDMI_CAPTURE_PRESENT |
					SOF_RT1308_SPEAKER_AMP_PRESENT),
	},
	{
		.name = "adl_cs35l41",
		.driver_data = (kernel_ulong_t)(SOF_AMPLIFIER_SSP(1) |
					SOF_NO_OF_HDMI_PLAYBACK(4) |
					SOF_HDMI_PLAYBACK_PRESENT |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT |
					SOF_CS35L41_SPEAKER_AMP_PRESENT),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_ssp_amp_driver = {
	.probe          = sof_ssp_amp_probe,
	.driver = {
		.name   = "sof_ssp_amp",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_ssp_amp_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF Amplifier Machine driver");
MODULE_AUTHOR("balamurugan.c <balamurugan.c@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_REALTEK_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_CIRRUS_COMMON);
