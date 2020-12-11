// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation
//
// sof_sdw_max98373 - Helpers to handle 2x MAX98373
// codec devices from generic machine driver

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include "sof_sdw_common.h"
#include "sof_maxim_common.h"

static const struct snd_soc_dapm_widget mx8373_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_kcontrol_new mx8373_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static int spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:mx8373",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_add_card_controls(card, mx8373_controls,
					ARRAY_SIZE(mx8373_controls));
	if (ret) {
		dev_err(card->dev, "mx8373 ctrls addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, mx8373_widgets,
					ARRAY_SIZE(mx8373_widgets));
	if (ret) {
		dev_err(card->dev, "mx8373 widgets addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, max_98373_dapm_routes, 2);
	if (ret)
		dev_err(rtd->dev, "failed to add first SPK map: %d\n", ret);

	return ret;
}

static int max98373_sdw_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* enable max98373 first */
		ret = max98373_trigger(substream, cmd);
		if (ret < 0)
			break;

		ret = sdw_trigger(substream, cmd);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = sdw_trigger(substream, cmd);
		if (ret < 0)
			break;

		ret = max98373_trigger(substream, cmd);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct snd_soc_ops max_98373_sdw_ops = {
	.startup = sdw_startup,
	.prepare = sdw_prepare,
	.trigger = max98373_sdw_trigger,
	.hw_free = sdw_hw_free,
	.shutdown = sdw_shutdown,
};

int sof_sdw_mx8373_init(const struct snd_soc_acpi_link_adr *link,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback)
{
	info->amp_num++;
	if (info->amp_num == 2)
		dai_links->init = spk_init;

	info->late_probe = true;

	dai_links->ops = &max_98373_sdw_ops;

	return 0;
}

int sof_sdw_mx8373_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_dapm_context *dapm = &card->dapm;

	/* Disable Left and Right Spk pin after boot */
	snd_soc_dapm_disable_pin(dapm, "Left Spk");
	snd_soc_dapm_disable_pin(dapm, "Right Spk");
	return snd_soc_dapm_sync(dapm);
}
