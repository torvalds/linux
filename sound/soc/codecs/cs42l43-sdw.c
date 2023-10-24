// SPDX-License-Identifier: GPL-2.0
//
// CS42L43 CODEC driver SoundWire handling
//
// Copyright (C) 2022-2023 Cirrus Logic, Inc. and
//                         Cirrus Logic International Semiconductor Ltd.

#include <linux/errno.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/sdw.h>
#include <sound/soc-component.h>
#include <sound/soc-dai.h>
#include <sound/soc.h>

#include "cs42l43.h"

int cs42l43_sdw_add_peripheral(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct sdw_slave *sdw = dev_to_sdw_dev(priv->dev->parent);
	struct sdw_stream_config sconfig = {0};
	struct sdw_port_config pconfig = {0};
	int ret;

	if (!sdw_stream)
		return -EINVAL;

	snd_sdw_params_to_config(substream, params, &sconfig, &pconfig);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		pconfig.num = dai->id;
	else
		pconfig.num = dai->id;

	ret = sdw_stream_add_slave(sdw, &sconfig, &pconfig, 1, sdw_stream);
	if (ret) {
		dev_err(priv->dev, "Failed to add sdw stream: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs42l43_sdw_add_peripheral, SND_SOC_CS42L43);

int cs42l43_sdw_remove_peripheral(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(dai->component);
	struct sdw_stream_runtime *sdw_stream = snd_soc_dai_get_dma_data(dai, substream);
	struct sdw_slave *sdw = dev_to_sdw_dev(priv->dev->parent);

	if (!sdw_stream)
		return -EINVAL;

	return sdw_stream_remove_slave(sdw, sdw_stream);
}
EXPORT_SYMBOL_NS_GPL(cs42l43_sdw_remove_peripheral, SND_SOC_CS42L43);

int cs42l43_sdw_set_stream(struct snd_soc_dai *dai, void *sdw_stream, int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs42l43_sdw_set_stream, SND_SOC_CS42L43);

MODULE_DESCRIPTION("CS42L43 CODEC SoundWire Driver");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
