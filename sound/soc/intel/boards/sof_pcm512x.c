// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2018-2020 Intel Corporation.

/*
 * Intel SOF Machine Driver for Intel platforms with TI PCM512x codec,
 * e.g. Up or Up2 with Hifiberry DAC+ HAT
 */
#include <linux/clk.h>
#include <linux/dmi.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/pcm512x.h"
#include "../common/soc-intel-quirks.h"
#include "hda_dsp_common.h"

#define NAME_SIZE 32

#define SOF_PCM512X_SSP_CODEC(quirk)		((quirk) & GENMASK(3, 0))
#define SOF_PCM512X_SSP_CODEC_MASK			(GENMASK(3, 0))

/* Default: SSP5 */
static unsigned long sof_pcm512x_quirk = SOF_PCM512X_SSP_CODEC(5);

static bool is_legacy_cpu;

struct sof_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct sof_card_private {
	struct list_head hdmi_pcm_list;
};

static int sof_pcm512x_quirk_cb(const struct dmi_system_id *id)
{
	sof_pcm512x_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_pcm512x_quirk_table[] = {
	{
		.callback = sof_pcm512x_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_MATCH(DMI_PRODUCT_NAME, "UP-CHT01"),
		},
		.driver_data = (void *)(SOF_PCM512X_SSP_CODEC(2)),
	},
	{}
};

static int sof_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
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

static int sof_pcm512x_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_EN, 0x08, 0x08);
	snd_soc_component_update_bits(codec, PCM512x_GPIO_OUTPUT_4, 0x0f, 0x02);
	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x08);

	return 0;
}

static int aif1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x08);

	return 0;
}

static void aif1_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x00);
}

static const struct snd_soc_ops sof_pcm512x_ops = {
	.startup = aif1_startup,
	.shutdown = aif1_shutdown,
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
static int sof_card_late_probe(struct snd_soc_card *card)
{
	struct sof_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct sof_hdmi_pcm *pcm;

	/* HDMI is not supported by SOF on Baytrail/CherryTrail */
	if (is_legacy_cpu)
		return 0;

	if (list_empty(&ctx->hdmi_pcm_list))
		return -EINVAL;

	pcm = list_first_entry(&ctx->hdmi_pcm_list, struct sof_hdmi_pcm, head);

	return hda_dsp_hdmi_build_controls(card, pcm->codec_dai->component);
}
#else
static int sof_card_late_probe(struct snd_soc_card *card)
{
	return 0;
}
#endif

static const struct snd_kcontrol_new sof_controls[] = {
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static const struct snd_soc_dapm_widget sof_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_widget dmic_widgets[] = {
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route sof_map[] = {
	/* Speaker */
	{"Ext Spk", NULL, "OUTR"},
	{"Ext Spk", NULL, "OUTL"},
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

/* sof audio machine driver for pcm512x codec */
static struct snd_soc_card sof_audio_card_pcm512x = {
	.name = "pcm512x",
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

SND_SOC_DAILINK_DEF(pcm512x_component,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-104C5122:00", "pcm512x-hifi")));
SND_SOC_DAILINK_DEF(dmic_component,
	DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

static struct snd_soc_dai_link *sof_card_dai_links_create(struct device *dev,
							  int ssp_codec,
							  int dmic_be_num,
							  int hdmi_num)
{
	struct snd_soc_dai_link_component *idisp_components;
	struct snd_soc_dai_link_component *cpus;
	struct snd_soc_dai_link *links;
	int i, id = 0;

	links = devm_kcalloc(dev, sof_audio_card_pcm512x.num_links,
			sizeof(struct snd_soc_dai_link), GFP_KERNEL);
	cpus = devm_kcalloc(dev, sof_audio_card_pcm512x.num_links,
			sizeof(struct snd_soc_dai_link_component), GFP_KERNEL);
	if (!links || !cpus)
		goto devm_err;

	/* codec SSP */
	links[id].name = devm_kasprintf(dev, GFP_KERNEL,
					"SSP%d-Codec", ssp_codec);
	if (!links[id].name)
		goto devm_err;

	links[id].id = id;
	links[id].codecs = pcm512x_component;
	links[id].num_codecs = ARRAY_SIZE(pcm512x_component);
	links[id].platforms = platform_component;
	links[id].num_platforms = ARRAY_SIZE(platform_component);
	links[id].init = sof_pcm512x_codec_init;
	links[id].ops = &sof_pcm512x_ops;
	links[id].nonatomic = true;
	links[id].dpcm_playback = 1;
	/*
	 * capture only supported with specific versions of the Hifiberry DAC+
	 * links[id].dpcm_capture = 1;
	 */
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
		links[id].name = "dmic01";
		links[id].cpus = &cpus[id];
		links[id].cpus->dai_name = "DMIC01 Pin";
		links[id].init = dmic_init;
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

	/* HDMI */
	if (hdmi_num > 0) {
		idisp_components = devm_kcalloc(dev, hdmi_num,
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

		idisp_components[i - 1].name = "ehdaudio0D2";
		idisp_components[i - 1].dai_name = devm_kasprintf(dev,
								  GFP_KERNEL,
								  "intel-hdmi-hifi%d",
								  i);
		if (!idisp_components[i - 1].dai_name)
			goto devm_err;

		links[id].codecs = &idisp_components[i - 1];
		links[id].num_codecs = 1;
		links[id].platforms = platform_component;
		links[id].num_platforms = ARRAY_SIZE(platform_component);
		links[id].init = sof_hdmi_init;
		links[id].dpcm_playback = 1;
		links[id].no_pcm = 1;
		id++;
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
	int ret, ssp_codec;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	hdmi_num = 0;
	if (soc_intel_is_byt() || soc_intel_is_cht()) {
		is_legacy_cpu = true;
		dmic_be_num = 0;
		/* default quirk for legacy cpu */
		sof_pcm512x_quirk = SOF_PCM512X_SSP_CODEC(2);
	} else {
		dmic_be_num = 2;
#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
		hdmi_num = 3;
#endif
	}

	dmi_check_system(sof_pcm512x_quirk_table);

	dev_dbg(&pdev->dev, "sof_pcm512x_quirk = %lx\n", sof_pcm512x_quirk);

	ssp_codec = sof_pcm512x_quirk & SOF_PCM512X_SSP_CODEC_MASK;

	/* compute number of dai links */
	sof_audio_card_pcm512x.num_links = 1 + dmic_be_num + hdmi_num;

	dai_links = sof_card_dai_links_create(&pdev->dev, ssp_codec,
					      dmic_be_num, hdmi_num);
	if (!dai_links)
		return -ENOMEM;

	sof_audio_card_pcm512x.dai_link = dai_links;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	sof_audio_card_pcm512x.dev = &pdev->dev;
	mach = (&pdev->dev)->platform_data;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_audio_card_pcm512x,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_audio_card_pcm512x, ctx);

	return devm_snd_soc_register_card(&pdev->dev,
					  &sof_audio_card_pcm512x);
}

static int sof_pcm512x_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct snd_soc_component *component = NULL;

	for_each_card_components(card, component) {
		if (!strcmp(component->name, pcm512x_component[0].name)) {
			snd_soc_component_set_jack(component, NULL, NULL);
			break;
		}
	}

	return 0;
}

static struct platform_driver sof_audio = {
	.probe = sof_audio_probe,
	.remove = sof_pcm512x_remove,
	.driver = {
		.name = "sof_pcm512x",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(sof_audio)

MODULE_DESCRIPTION("ASoC Intel(R) SOF + PCM512x Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sof_pcm512x");
