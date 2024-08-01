// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2020 Intel Corporation
// Copyright(c) 2024 Advanced Micro Devices, Inc.
/*
 *  soc-sdw-utils.c - common SoundWire machine driver helper functions
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc_sdw_utils.h>

/* these wrappers are only needed to avoid typecast compilation errors */
int asoc_sdw_startup(struct snd_pcm_substream *substream)
{
	return sdw_startup_stream(substream);
}
EXPORT_SYMBOL_NS(asoc_sdw_startup, SND_SOC_SDW_UTILS);

int asoc_sdw_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;

	/* Find stream from first CPU DAI */
	dai = snd_soc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_stream(dai, substream->stream);
	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s\n", dai->name);
		return PTR_ERR(sdw_stream);
	}

	return sdw_prepare_stream(sdw_stream);
}
EXPORT_SYMBOL_NS(asoc_sdw_prepare, SND_SOC_SDW_UTILS);

int asoc_sdw_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;
	int ret;

	/* Find stream from first CPU DAI */
	dai = snd_soc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_stream(dai, substream->stream);
	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s\n", dai->name);
		return PTR_ERR(sdw_stream);
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		ret = sdw_enable_stream(sdw_stream);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		ret = sdw_disable_stream(sdw_stream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		dev_err(rtd->dev, "%s trigger %d failed: %d\n", __func__, cmd, ret);

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_trigger, SND_SOC_SDW_UTILS);

int asoc_sdw_hw_params(struct snd_pcm_substream *substream,
		       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai_link_ch_map *ch_maps;
	int ch = params_channels(params);
	unsigned int ch_mask;
	int num_codecs;
	int step;
	int i;

	if (!rtd->dai_link->ch_maps)
		return 0;

	/* Identical data will be sent to all codecs in playback */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ch_mask = GENMASK(ch - 1, 0);
		step = 0;
	} else {
		num_codecs = rtd->dai_link->num_codecs;

		if (ch < num_codecs || ch % num_codecs != 0) {
			dev_err(rtd->dev, "Channels number %d is invalid when codec number = %d\n",
				ch, num_codecs);
			return -EINVAL;
		}

		ch_mask = GENMASK(ch / num_codecs - 1, 0);
		step = hweight_long(ch_mask);
	}

	/*
	 * The captured data will be combined from each cpu DAI if the dai
	 * link has more than one codec DAIs. Set codec channel mask and
	 * ASoC will set the corresponding channel numbers for each cpu dai.
	 */
	for_each_link_ch_maps(rtd->dai_link, i, ch_maps)
		ch_maps->ch_mask = ch_mask << (i * step);

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_hw_params, SND_SOC_SDW_UTILS);

int asoc_sdw_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct sdw_stream_runtime *sdw_stream;
	struct snd_soc_dai *dai;

	/* Find stream from first CPU DAI */
	dai = snd_soc_rtd_to_cpu(rtd, 0);

	sdw_stream = snd_soc_dai_get_stream(dai, substream->stream);
	if (IS_ERR(sdw_stream)) {
		dev_err(rtd->dev, "no stream found for DAI %s\n", dai->name);
		return PTR_ERR(sdw_stream);
	}

	return sdw_deprepare_stream(sdw_stream);
}
EXPORT_SYMBOL_NS(asoc_sdw_hw_free, SND_SOC_SDW_UTILS);

void asoc_sdw_shutdown(struct snd_pcm_substream *substream)
{
	sdw_shutdown_stream(substream);
}
EXPORT_SYMBOL_NS(asoc_sdw_shutdown, SND_SOC_SDW_UTILS);

static bool asoc_sdw_is_unique_device(const struct snd_soc_acpi_link_adr *adr_link,
				      unsigned int sdw_version,
				      unsigned int mfg_id,
				      unsigned int part_id,
				      unsigned int class_id,
				      int index_in_link)
{
	int i;

	for (i = 0; i < adr_link->num_adr; i++) {
		unsigned int sdw1_version, mfg1_id, part1_id, class1_id;
		u64 adr;

		/* skip itself */
		if (i == index_in_link)
			continue;

		adr = adr_link->adr_d[i].adr;

		sdw1_version = SDW_VERSION(adr);
		mfg1_id = SDW_MFG_ID(adr);
		part1_id = SDW_PART_ID(adr);
		class1_id = SDW_CLASS_ID(adr);

		if (sdw_version == sdw1_version &&
		    mfg_id == mfg1_id &&
		    part_id == part1_id &&
		    class_id == class1_id)
			return false;
	}

	return true;
}

const char *asoc_sdw_get_codec_name(struct device *dev,
				    const struct asoc_sdw_codec_info *codec_info,
				    const struct snd_soc_acpi_link_adr *adr_link,
				    int adr_index)
{
	u64 adr = adr_link->adr_d[adr_index].adr;
	unsigned int sdw_version = SDW_VERSION(adr);
	unsigned int link_id = SDW_DISCO_LINK_ID(adr);
	unsigned int unique_id = SDW_UNIQUE_ID(adr);
	unsigned int mfg_id = SDW_MFG_ID(adr);
	unsigned int part_id = SDW_PART_ID(adr);
	unsigned int class_id = SDW_CLASS_ID(adr);

	if (codec_info->codec_name)
		return devm_kstrdup(dev, codec_info->codec_name, GFP_KERNEL);
	else if (asoc_sdw_is_unique_device(adr_link, sdw_version, mfg_id, part_id,
					   class_id, adr_index))
		return devm_kasprintf(dev, GFP_KERNEL, "sdw:0:%01x:%04x:%04x:%02x",
				      link_id, mfg_id, part_id, class_id);
	else
		return devm_kasprintf(dev, GFP_KERNEL, "sdw:0:%01x:%04x:%04x:%02x:%01x",
				      link_id, mfg_id, part_id, class_id, unique_id);

	return NULL;
}
EXPORT_SYMBOL_NS(asoc_sdw_get_codec_name, SND_SOC_SDW_UTILS);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SoundWire ASoC helpers");
