// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/hda_codec.h>
#include <sound/hda_i915.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../../codecs/hda.h"

static int avs_create_dai_links(struct device *dev, struct hda_codec *codec, int pcm_count,
				const char *platform_name, struct snd_soc_dai_link **links)
{
	struct snd_soc_dai_link_component *platform;
	struct snd_soc_dai_link *dl;
	struct hda_pcm *pcm;
	const char *cname = dev_name(&codec->core.dev);
	int i;

	dl = devm_kcalloc(dev, pcm_count, sizeof(*dl), GFP_KERNEL);
	platform = devm_kzalloc(dev, sizeof(*platform), GFP_KERNEL);
	if (!dl || !platform)
		return -ENOMEM;

	platform->name = platform_name;
	pcm = list_first_entry(&codec->pcm_list_head, struct hda_pcm, list);

	for (i = 0; i < pcm_count; i++, pcm = list_next_entry(pcm, list)) {
		dl[i].name = devm_kasprintf(dev, GFP_KERNEL, "%s link%d", cname, i);
		if (!dl[i].name)
			return -ENOMEM;

		dl[i].id = i;
		dl[i].nonatomic = 1;
		dl[i].no_pcm = 1;
		dl[i].dpcm_playback = 1;
		dl[i].dpcm_capture = 1;
		dl[i].platforms = platform;
		dl[i].num_platforms = 1;
		dl[i].ignore_pmdown_time = 1;

		dl[i].codecs = devm_kzalloc(dev, sizeof(*dl->codecs), GFP_KERNEL);
		dl[i].cpus = devm_kzalloc(dev, sizeof(*dl->cpus), GFP_KERNEL);
		if (!dl[i].codecs || !dl[i].cpus)
			return -ENOMEM;

		dl[i].cpus->dai_name = devm_kasprintf(dev, GFP_KERNEL, "%s-cpu%d", cname, i);
		if (!dl[i].cpus->dai_name)
			return -ENOMEM;

		dl[i].codecs->name = devm_kstrdup(dev, cname, GFP_KERNEL);
		dl[i].codecs->dai_name = pcm->name;
		dl[i].num_codecs = 1;
		dl[i].num_cpus = 1;
	}

	*links = dl;
	return 0;
}

static int avs_create_dapm_routes(struct device *dev, struct hda_codec *codec, int pcm_count,
				  struct snd_soc_dapm_route **routes, int *num_routes)
{
	struct snd_soc_dapm_route *dr;
	struct hda_pcm *pcm;
	const char *cname = dev_name(&codec->core.dev);
	int i, n = 0;

	/* at max twice the number of pcms */
	dr = devm_kcalloc(dev, pcm_count * 2, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	pcm = list_first_entry(&codec->pcm_list_head, struct hda_pcm, list);

	for (i = 0; i < pcm_count; i++, pcm = list_next_entry(pcm, list)) {
		struct hda_pcm_stream *stream;
		int dir;

		dir = SNDRV_PCM_STREAM_PLAYBACK;
		stream = &pcm->stream[dir];
		if (!stream->substreams)
			goto capture_routes;

		dr[n].sink = devm_kasprintf(dev, GFP_KERNEL, "%s %s", pcm->name,
					    snd_pcm_direction_name(dir));
		dr[n].source = devm_kasprintf(dev, GFP_KERNEL, "%s-cpu%d Tx", cname, i);
		if (!dr[n].sink || !dr[n].source)
			return -ENOMEM;
		n++;

capture_routes:
		dir = SNDRV_PCM_STREAM_CAPTURE;
		stream = &pcm->stream[dir];
		if (!stream->substreams)
			continue;

		dr[n].sink = devm_kasprintf(dev, GFP_KERNEL, "%s-cpu%d Rx", cname, i);
		dr[n].source = devm_kasprintf(dev, GFP_KERNEL, "%s %s", pcm->name,
					      snd_pcm_direction_name(dir));
		if (!dr[n].sink || !dr[n].source)
			return -ENOMEM;
		n++;
	}

	*routes = dr;
	*num_routes = n;
	return 0;
}

/* Should be aligned with SectionPCM's name from topology */
#define FEDAI_NAME_PREFIX "HDMI"

static struct snd_pcm *
avs_card_hdmi_pcm_at(struct snd_soc_card *card, int hdmi_idx)
{
	struct snd_soc_pcm_runtime *rtd;
	int dir = SNDRV_PCM_STREAM_PLAYBACK;

	for_each_card_rtds(card, rtd) {
		struct snd_pcm *spcm;
		int ret, n;

		spcm = rtd->pcm ? rtd->pcm->streams[dir].pcm : NULL;
		if (!spcm || !strstr(spcm->id, FEDAI_NAME_PREFIX))
			continue;

		ret = sscanf(spcm->id, FEDAI_NAME_PREFIX "%d", &n);
		if (ret != 1)
			continue;
		if (n == hdmi_idx)
			return rtd->pcm;
	}

	return NULL;
}

static int avs_card_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_acpi_mach *mach = dev_get_platdata(card->dev);
	struct hda_codec *codec = mach->pdata;
	struct hda_pcm *hpcm;
	/* Topology pcm indexing is 1-based */
	int i = 1;

	list_for_each_entry(hpcm, &codec->pcm_list_head, list) {
		struct snd_pcm *spcm;

		spcm = avs_card_hdmi_pcm_at(card, i);
		if (spcm) {
			hpcm->pcm = spcm;
			hpcm->device = spcm->device;
			dev_info(card->dev, "%s: mapping HDMI converter %d to PCM %d (%p)\n",
				 __func__, i, hpcm->device, spcm);
		} else {
			hpcm->pcm = NULL;
			hpcm->device = SNDRV_PCM_INVALID_DEVICE;
			dev_warn(card->dev, "%s: no PCM in topology for HDMI converter %d\n",
				 __func__, i);
		}
		i++;
	}

	return hda_codec_probe_complete(codec);
}

static int avs_probing_link_init(struct snd_soc_pcm_runtime *rtm)
{
	struct snd_soc_dapm_route *routes;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_dai_link *links = NULL;
	struct snd_soc_card *card = rtm->card;
	struct hda_codec *codec;
	struct hda_pcm *pcm;
	int ret, n, pcm_count = 0;

	mach = dev_get_platdata(card->dev);
	codec = mach->pdata;

	if (list_empty(&codec->pcm_list_head))
		return -EINVAL;
	list_for_each_entry(pcm, &codec->pcm_list_head, list)
		pcm_count++;

	ret = avs_create_dai_links(card->dev, codec, pcm_count, mach->mach_params.platform, &links);
	if (ret < 0) {
		dev_err(card->dev, "create links failed: %d\n", ret);
		return ret;
	}

	for (n = 0; n < pcm_count; n++) {
		ret = snd_soc_add_pcm_runtime(card, &links[n]);
		if (ret < 0) {
			dev_err(card->dev, "add links failed: %d\n", ret);
			return ret;
		}
	}

	ret = avs_create_dapm_routes(card->dev, codec, pcm_count, &routes, &n);
	if (ret < 0) {
		dev_err(card->dev, "create routes failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, routes, n);
	if (ret < 0) {
		dev_err(card->dev, "add routes failed: %d\n", ret);
		return ret;
	}

	return 0;
}

SND_SOC_DAILINK_DEF(dummy, DAILINK_COMP_ARRAY(COMP_DUMMY()));

static struct snd_soc_dai_link probing_link = {
	.name = "probing-LINK",
	.id = -1,
	.nonatomic = 1,
	.no_pcm = 1,
	.dpcm_playback = 1,
	.dpcm_capture = 1,
	.cpus = dummy,
	.num_cpus = ARRAY_SIZE(dummy),
	.init = avs_probing_link_init,
};

static int avs_hdaudio_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *binder;
	struct snd_soc_acpi_mach *mach;
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	struct hda_codec *codec;

	mach = dev_get_platdata(dev);
	codec = mach->pdata;

	/* codec may be unloaded before card's probe() fires */
	if (!device_is_registered(&codec->core.dev))
		return -ENODEV;

	binder = devm_kmemdup(dev, &probing_link, sizeof(probing_link), GFP_KERNEL);
	if (!binder)
		return -ENOMEM;

	binder->platforms = devm_kzalloc(dev, sizeof(*binder->platforms), GFP_KERNEL);
	binder->codecs = devm_kzalloc(dev, sizeof(*binder->codecs), GFP_KERNEL);
	if (!binder->platforms || !binder->codecs)
		return -ENOMEM;

	binder->codecs->name = devm_kstrdup(dev, dev_name(&codec->core.dev), GFP_KERNEL);
	if (!binder->codecs->name)
		return -ENOMEM;

	binder->platforms->name = mach->mach_params.platform;
	binder->num_platforms = 1;
	binder->codecs->dai_name = "codec-probing-DAI";
	binder->num_codecs = 1;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->name = binder->codecs->name;
	card->dev = dev;
	card->owner = THIS_MODULE;
	card->dai_link = binder;
	card->num_links = 1;
	card->fully_routed = true;
	if (hda_codec_is_display(codec))
		card->late_probe = avs_card_late_probe;

	return devm_snd_soc_register_card(dev, card);
}

static struct platform_driver avs_hdaudio_driver = {
	.probe = avs_hdaudio_probe,
	.driver = {
		.name = "avs_hdaudio",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(avs_hdaudio_driver)

MODULE_DESCRIPTION("Intel HD-Audio machine driver");
MODULE_AUTHOR("Cezary Rojewski <cezary.rojewski@intel.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:avs_hdaudio");
