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
#include "../../codecs/hdac_hdmi.h"
#include "../common/soc-intel-quirks.h"
#include "hda_dsp_common.h"
#include "sof_maxim_common.h"

#define NAME_SIZE 32

#define SOF_CS42L42_SSP_CODEC(quirk)		((quirk) & GENMASK(2, 0))
#define SOF_CS42L42_SSP_CODEC_MASK		(GENMASK(2, 0))
#define SOF_SPEAKER_AMP_PRESENT			BIT(3)
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
#define SOF_MAX98357A_SPEAKER_AMP_PRESENT	BIT(25)
#define SOF_MAX98360A_SPEAKER_AMP_PRESENT	BIT(26)

enum {
	LINK_NONE = 0,
	LINK_HP = 1,
	LINK_SPK = 2,
	LINK_DMIC = 3,
	LINK_HDMI = 4,
};

/* Default: SSP2 */
static unsigned long sof_cs42l42_quirk = SOF_CS42L42_SSP_CODEC(2);

struct sof_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_jack hdmi_jack;
	int device;
};

struct sof_card_private {
	struct snd_soc_jack headset_jack;
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
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

static int sof_cs42l42_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_jack *jack = &ctx->headset_jack;
	int ret;

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2 |
				    SND_JACK_BTN_3,
				    jack, NULL, 0);
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
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}

static int sof_cs42l42_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
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

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component = NULL;
	char jack_name[NAME_SIZE];
	struct sof_hdmi_pcm *pcm;
	int err;

	if (list_empty(&ctx->hdmi_pcm_list))
		return -EINVAL;

	if (ctx->common_hdmi_codec_drv) {
		pcm = list_first_entry(&ctx->hdmi_pcm_list, struct sof_hdmi_pcm,
				       head);
		component = pcm->codec_dai->component;
		return hda_dsp_hdmi_build_controls(card, component);
	}

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &pcm->hdmi_jack,
					    NULL, 0);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &pcm->hdmi_jack);
		if (err < 0)
			return err;
	}

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}

static const struct snd_kcontrol_new sof_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_widget dmic_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route sof_map[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{"Headphone Jack", NULL, "HP"},

	/* other jacks */
	{"HS", NULL, "Headset Mic"},
};

static const struct snd_soc_dapm_route dmic_map[] = {
	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
};

static int dmic_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, dmic_widgets,
					ARRAY_SIZE(dmic_widgets));
	if (ret) {
		dev_err(card->dev, "DMic widget addition failed: %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, dmic_map,
				      ARRAY_SIZE(dmic_map));

	if (ret)
		dev_err(card->dev, "DMic map addition failed: %d\n", ret);

	return ret;
}

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

static struct snd_soc_dai_link_component dmic_component[] = {
	{
		.name = "dmic-codec",
		.dai_name = "dmic-hifi",
	}
};

static int create_spk_amp_dai_links(struct device *dev,
				    struct snd_soc_dai_link *links,
				    struct snd_soc_dai_link_component *cpus,
				    int *id, int ssp_amp)
{
	int ret = 0;

	/* speaker amp */
	if (!(sof_cs42l42_quirk & SOF_SPEAKER_AMP_PRESENT))
		return 0;

	links[*id].name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec",
					 ssp_amp);
	if (!links[*id].name) {
		ret = -ENOMEM;
		goto devm_err;
	}

	links[*id].id = *id;

	if (sof_cs42l42_quirk & SOF_MAX98357A_SPEAKER_AMP_PRESENT) {
		max_98357a_dai_link(&links[*id]);
	} else if (sof_cs42l42_quirk & SOF_MAX98360A_SPEAKER_AMP_PRESENT) {
		max_98360a_dai_link(&links[*id]);
	} else {
		dev_err(dev, "no amp defined\n");
		ret = -EINVAL;
		goto devm_err;
	}

	links[*id].platforms = platform_component;
	links[*id].num_platforms = ARRAY_SIZE(platform_component);
	links[*id].dpcm_playback = 1;
	links[*id].no_pcm = 1;
	links[*id].cpus = &cpus[*id];
	links[*id].num_cpus = 1;

	links[*id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
						   "SSP%d Pin", ssp_amp);
	if (!links[*id].cpus->dai_name) {
		ret = -ENOMEM;
		goto devm_err;
	}

	(*id)++;

devm_err:
	return ret;
}

static int create_hp_codec_dai_links(struct device *dev,
				     struct snd_soc_dai_link *links,
				     struct snd_soc_dai_link_component *cpus,
				     int *id, int ssp_codec)
{
	/* codec SSP */
	links[*id].name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec",
					 ssp_codec);
	if (!links[*id].name)
		goto devm_err;

	links[*id].id = *id;
	links[*id].codecs = cs42l42_component;
	links[*id].num_codecs = ARRAY_SIZE(cs42l42_component);
	links[*id].platforms = platform_component;
	links[*id].num_platforms = ARRAY_SIZE(platform_component);
	links[*id].init = sof_cs42l42_init;
	links[*id].exit = sof_cs42l42_exit;
	links[*id].ops = &sof_cs42l42_ops;
	links[*id].dpcm_playback = 1;
	links[*id].dpcm_capture = 1;
	links[*id].no_pcm = 1;
	links[*id].cpus = &cpus[*id];
	links[*id].num_cpus = 1;

	links[*id].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL,
						   "SSP%d Pin",
						   ssp_codec);
	if (!links[*id].cpus->dai_name)
		goto devm_err;

	(*id)++;

	return 0;

devm_err:
	return -ENOMEM;
}

static int create_dmic_dai_links(struct device *dev,
				 struct snd_soc_dai_link *links,
				 struct snd_soc_dai_link_component *cpus,
				 int *id, int dmic_be_num)
{
	int i;

	/* dmic */
	if (dmic_be_num <= 0)
		return 0;

	/* at least we have dmic01 */
	links[*id].name = "dmic01";
	links[*id].cpus = &cpus[*id];
	links[*id].cpus->dai_name = "DMIC01 Pin";
	links[*id].init = dmic_init;
	if (dmic_be_num > 1) {
		/* set up 2 BE links at most */
		links[*id + 1].name = "dmic16k";
		links[*id + 1].cpus = &cpus[*id + 1];
		links[*id + 1].cpus->dai_name = "DMIC16k Pin";
		dmic_be_num = 2;
	}

	for (i = 0; i < dmic_be_num; i++) {
		links[*id].id = *id;
		links[*id].num_cpus = 1;
		links[*id].codecs = dmic_component;
		links[*id].num_codecs = ARRAY_SIZE(dmic_component);
		links[*id].platforms = platform_component;
		links[*id].num_platforms = ARRAY_SIZE(platform_component);
		links[*id].ignore_suspend = 1;
		links[*id].dpcm_capture = 1;
		links[*id].no_pcm = 1;

		(*id)++;
	}

	return 0;
}

static int create_hdmi_dai_links(struct device *dev,
				 struct snd_soc_dai_link *links,
				 struct snd_soc_dai_link_component *cpus,
				 int *id, int hdmi_num)
{
	struct snd_soc_dai_link_component *idisp_components;
	int i;

	/* HDMI */
	if (hdmi_num <= 0)
		return 0;

	idisp_components = devm_kzalloc(dev,
					sizeof(struct snd_soc_dai_link_component) *
					hdmi_num, GFP_KERNEL);
	if (!idisp_components)
		goto devm_err;

	for (i = 1; i <= hdmi_num; i++) {
		links[*id].name = devm_kasprintf(dev, GFP_KERNEL,
						 "iDisp%d", i);
		if (!links[*id].name)
			goto devm_err;

		links[*id].id = *id;
		links[*id].cpus = &cpus[*id];
		links[*id].num_cpus = 1;
		links[*id].cpus->dai_name = devm_kasprintf(dev,
							   GFP_KERNEL,
							   "iDisp%d Pin",
							   i);
		if (!links[*id].cpus->dai_name)
			goto devm_err;

		idisp_components[i - 1].name = "ehdaudio0D2";
		idisp_components[i - 1].dai_name = devm_kasprintf(dev,
								  GFP_KERNEL,
								  "intel-hdmi-hifi%d",
								  i);
		if (!idisp_components[i - 1].dai_name)
			goto devm_err;

		links[*id].codecs = &idisp_components[i - 1];
		links[*id].num_codecs = 1;
		links[*id].platforms = platform_component;
		links[*id].num_platforms = ARRAY_SIZE(platform_component);
		links[*id].init = sof_hdmi_init;
		links[*id].dpcm_playback = 1;
		links[*id].no_pcm = 1;

		(*id)++;
	}

	return 0;

devm_err:
	return -ENOMEM;
}

static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev,
							  int ssp_codec,
							  int ssp_amp,
							  int dmic_be_num,
							  int hdmi_num)
{
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int ret, id = 0, link_seq;

	links = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link) *
			     sof_audio_card_cs42l42.num_links, GFP_KERNEL);
	cpus = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link_component) *
			     sof_audio_card_cs42l42.num_links, GFP_KERNEL);
	if (!links || !cpus)
		goto devm_err;

	link_seq = (sof_cs42l42_quirk & SOF_CS42L42_DAILINK_MASK) >> SOF_CS42L42_DAILINK_SHIFT;

	while (link_seq) {
		int link_type = link_seq & 0x07;

		switch (link_type) {
		case LINK_HP:
			ret = create_hp_codec_dai_links(dev, links, cpus, &id, ssp_codec);
			if (ret < 0) {
				dev_err(dev, "fail to create hp codec dai links, ret %d\n",
					ret);
				goto devm_err;
			}
			break;
		case LINK_SPK:
			ret = create_spk_amp_dai_links(dev, links, cpus, &id, ssp_amp);
			if (ret < 0) {
				dev_err(dev, "fail to create spk amp dai links, ret %d\n",
					ret);
				goto devm_err;
			}
			break;
		case LINK_DMIC:
			ret = create_dmic_dai_links(dev, links, cpus, &id, dmic_be_num);
			if (ret < 0) {
				dev_err(dev, "fail to create dmic dai links, ret %d\n",
					ret);
				goto devm_err;
			}
			break;
		case LINK_HDMI:
			ret = create_hdmi_dai_links(dev, links, cpus, &id, hdmi_num);
			if (ret < 0) {
				dev_err(dev, "fail to create hdmi dai links, ret %d\n",
					ret);
				goto devm_err;
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
	struct snd_soc_dai_link *dai_links;
	struct snd_soc_acpi_mach *mach;
	struct sof_card_private *ctx;
	int dmic_be_num, hdmi_num;
	int ret, ssp_amp, ssp_codec;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_cs42l42_quirk = (unsigned long)pdev->id_entry->driver_data;

	mach = pdev->dev.platform_data;

	if (soc_intel_is_glk()) {
		dmic_be_num = 1;
		hdmi_num = 3;
	} else {
		dmic_be_num = 2;
		hdmi_num = (sof_cs42l42_quirk & SOF_CS42L42_NUM_HDMIDEV_MASK) >>
			 SOF_CS42L42_NUM_HDMIDEV_SHIFT;
		/* default number of HDMI DAI's */
		if (!hdmi_num)
			hdmi_num = 3;
	}

	dev_dbg(&pdev->dev, "sof_cs42l42_quirk = %lx\n", sof_cs42l42_quirk);

	ssp_amp = (sof_cs42l42_quirk & SOF_CS42L42_SSP_AMP_MASK) >>
			SOF_CS42L42_SSP_AMP_SHIFT;

	ssp_codec = sof_cs42l42_quirk & SOF_CS42L42_SSP_CODEC_MASK;

	/* compute number of dai links */
	sof_audio_card_cs42l42.num_links = 1 + dmic_be_num + hdmi_num;

	if (sof_cs42l42_quirk & SOF_SPEAKER_AMP_PRESENT)
		sof_audio_card_cs42l42.num_links++;

	dai_links = sof_card_dai_links_create(&pdev->dev, ssp_codec, ssp_amp,
					      dmic_be_num, hdmi_num);
	if (!dai_links)
		return -ENOMEM;

	sof_audio_card_cs42l42.dai_link = dai_links;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	sof_audio_card_cs42l42.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_audio_card_cs42l42,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	snd_soc_card_set_drvdata(&sof_audio_card_cs42l42, ctx);

	return devm_snd_soc_register_card(&pdev->dev,
					  &sof_audio_card_cs42l42);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "glk_cs4242_mx98357a",
		.driver_data = (kernel_ulong_t)(SOF_CS42L42_SSP_CODEC(2) |
					SOF_SPEAKER_AMP_PRESENT |
					SOF_MAX98357A_SPEAKER_AMP_PRESENT |
					SOF_CS42L42_SSP_AMP(1)) |
					SOF_CS42L42_DAILINK(LINK_SPK, LINK_HP, LINK_DMIC, LINK_HDMI, LINK_NONE),
	},
	{
		.name = "jsl_cs4242_mx98360a",
		.driver_data = (kernel_ulong_t)(SOF_CS42L42_SSP_CODEC(0) |
					SOF_SPEAKER_AMP_PRESENT |
					SOF_MAX98360A_SPEAKER_AMP_PRESENT |
					SOF_CS42L42_SSP_AMP(1)) |
					SOF_CS42L42_DAILINK(LINK_HP, LINK_DMIC, LINK_HDMI, LINK_SPK, LINK_NONE),
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
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_MAXIM_COMMON);
