// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2020 Intel Corporation
#include <linux/module.h>
#include <linux/string.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/sof.h>
#include <uapi/sound/asound.h>
#include "../common/soc-intel-quirks.h"
#include "sof_maxim_common.h"

/*
 * Common structures and functions
 */
static const struct snd_kcontrol_new maxim_2spk_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),

};

static const struct snd_soc_dapm_widget maxim_2spk_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

/* helper function to get the number of specific codec */
static unsigned int get_num_codecs(const char *hid)
{
	struct acpi_device *adev;
	unsigned int dev_num = 0;

	for_each_acpi_dev_match(adev, hid, NULL, -1)
		dev_num++;

	return dev_num;
}

/*
 * Maxim MAX98373
 */
#define MAX_98373_PIN_NAME 16

static const struct snd_soc_dapm_route max_98373_dapm_routes[] = {
	/* speaker */
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },
};

static struct snd_soc_codec_conf max_98373_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(MAX_98373_DEV0_NAME),
		.name_prefix = "Right",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX_98373_DEV1_NAME),
		.name_prefix = "Left",
	},
};

static struct snd_soc_dai_link_component max_98373_components[] = {
	{  /* For Right */
		.name = MAX_98373_DEV0_NAME,
		.dai_name = MAX_98373_CODEC_DAI,
	},
	{  /* For Left */
		.name = MAX_98373_DEV1_NAME,
		.dai_name = MAX_98373_CODEC_DAI,
	},
};

/*
 * According to the definition of 'DAI Sel Mux' mixer in max98373.c, rx mask
 * should choose two channels from TDM slots, the LSB of rx mask is left channel
 * and the other one is right channel.
 */
static const struct {
	unsigned int rx;
} max_98373_tdm_mask[] = {
	{.rx = 0x3},
	{.rx = 0x3},
};

/*
 * The tx mask indicates which channel(s) contains output IV-sense data and
 * others should set to Hi-Z. Here we get the channel number from codec's ACPI
 * device property "maxim,vmon-slot-no" and "maxim,imon-slot-no" to generate the
 * mask. Refer to the max98373_slot_config() function in max98373.c codec driver.
 */
static unsigned int max_98373_get_tx_mask(struct device *dev)
{
	int vmon_slot;
	int imon_slot;

	if (device_property_read_u32(dev, "maxim,vmon-slot-no", &vmon_slot))
		vmon_slot = 0;

	if (device_property_read_u32(dev, "maxim,imon-slot-no", &imon_slot))
		imon_slot = 1;

	dev_dbg(dev, "vmon_slot %d imon_slot %d\n", vmon_slot, imon_slot);

	return (0x1 << vmon_slot) | (0x1 << imon_slot);
}

static int max_98373_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_dai *codec_dai;
	int i;
	int tdm_slots;
	unsigned int tx_mask;
	unsigned int tx_mask_used = 0x0;
	int ret = 0;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (i >= ARRAY_SIZE(max_98373_tdm_mask)) {
			dev_err(codec_dai->dev, "only 2 amps are supported\n");
			return -EINVAL;
		}

		switch (dai_link->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_DSP_A:
		case SND_SOC_DAIFMT_DSP_B:
			/* get the tplg configured tdm slot number */
			tdm_slots = sof_dai_get_tdm_slots(rtd);
			if (tdm_slots <= 0) {
				dev_err(rtd->dev, "invalid tdm slots %d\n",
					tdm_slots);
				return -EINVAL;
			}

			/* get the tx mask from ACPI device properties */
			tx_mask = max_98373_get_tx_mask(codec_dai->dev);
			if (!tx_mask)
				return -EINVAL;

			if (tx_mask & tx_mask_used) {
				dev_err(codec_dai->dev, "invalid tx mask 0x%x, used 0x%x\n",
					tx_mask, tx_mask_used);
				return -EINVAL;
			}

			tx_mask_used |= tx_mask;

			/*
			 * check if tdm slot number is too small for channel
			 * allocation
			 */
			if (fls(tx_mask) > tdm_slots) {
				dev_err(codec_dai->dev, "slot mismatch, tx %d slots %d\n",
					fls(tx_mask), tdm_slots);
				return -EINVAL;
			}

			if (fls(max_98373_tdm_mask[i].rx) > tdm_slots) {
				dev_err(codec_dai->dev, "slot mismatch, rx %d slots %d\n",
					fls(max_98373_tdm_mask[i].rx), tdm_slots);
				return -EINVAL;
			}

			dev_dbg(codec_dai->dev, "set tdm slot: tx 0x%x rx 0x%x slots %d width %d\n",
				tx_mask, max_98373_tdm_mask[i].rx,
				tdm_slots, params_width(params));

			ret = snd_soc_dai_set_tdm_slot(codec_dai, tx_mask,
						       max_98373_tdm_mask[i].rx,
						       tdm_slots,
						       params_width(params));
			if (ret < 0) {
				dev_err(codec_dai->dev, "fail to set tdm slot, ret %d\n",
					ret);
				return ret;
			}
			break;
		default:
			dev_dbg(codec_dai->dev, "codec is in I2S mode\n");
			break;
		}
	}
	return 0;
}

static int max_98373_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;
	int j;
	int ret = 0;

	/* set spk pin by playback only */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	for_each_rtd_codec_dais(rtd, j, codec_dai) {
		struct snd_soc_dapm_context *dapm =
				snd_soc_component_get_dapm(cpu_dai->component);
		char pin_name[MAX_98373_PIN_NAME];

		snprintf(pin_name, ARRAY_SIZE(pin_name), "%s Spk",
			 codec_dai->component->name_prefix);

		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			ret = snd_soc_dapm_enable_pin(dapm, pin_name);
			if (!ret)
				snd_soc_dapm_sync(dapm);
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			ret = snd_soc_dapm_disable_pin(dapm, pin_name);
			if (!ret)
				snd_soc_dapm_sync(dapm);
			break;
		default:
			break;
		}
	}

	return ret;
}

static const struct snd_soc_ops max_98373_ops = {
	.hw_params = max_98373_hw_params,
	.trigger = max_98373_trigger,
};

static int max_98373_spk_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	unsigned int num_codecs = get_num_codecs(MAX_98373_ACPI_HID);
	int ret;

	switch (num_codecs) {
	case 2:
		ret = snd_soc_dapm_new_controls(&card->dapm, maxim_2spk_widgets,
						ARRAY_SIZE(maxim_2spk_widgets));
		if (ret) {
			dev_err(rtd->dev, "fail to add max98373 widgets, ret %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_add_card_controls(card, maxim_2spk_kcontrols,
						ARRAY_SIZE(maxim_2spk_kcontrols));
		if (ret) {
			dev_err(rtd->dev, "fail to add max98373 kcontrols, ret %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_dapm_add_routes(&card->dapm, max_98373_dapm_routes,
					      ARRAY_SIZE(max_98373_dapm_routes));
		if (ret) {
			dev_err(rtd->dev, "fail to add max98373 routes, ret %d\n",
				ret);
			return ret;
		}
		break;
	default:
		dev_err(rtd->dev, "max98373: invalid num_codecs %d\n", num_codecs);
		return -EINVAL;
	}

	return ret;
}

void max_98373_dai_link(struct device *dev, struct snd_soc_dai_link *link)
{
	link->codecs = max_98373_components;
	link->num_codecs = ARRAY_SIZE(max_98373_components);
	link->init = max_98373_spk_codec_init;
	link->ops = &max_98373_ops;
}
EXPORT_SYMBOL_NS(max_98373_dai_link, SND_SOC_INTEL_SOF_MAXIM_COMMON);

void max_98373_set_codec_conf(struct snd_soc_card *card)
{
	card->codec_conf = max_98373_codec_conf;
	card->num_configs = ARRAY_SIZE(max_98373_codec_conf);
}
EXPORT_SYMBOL_NS(max_98373_set_codec_conf, SND_SOC_INTEL_SOF_MAXIM_COMMON);

/*
 * Maxim MAX98390
 */
static const struct snd_soc_dapm_route max_98390_dapm_routes[] = {
	/* speaker */
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },
};

static const struct snd_kcontrol_new max_98390_tt_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("TL Spk"),
	SOC_DAPM_PIN_SWITCH("TR Spk"),
};

static const struct snd_soc_dapm_widget max_98390_tt_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("TL Spk", NULL),
	SND_SOC_DAPM_SPK("TR Spk", NULL),
};

static const struct snd_soc_dapm_route max_98390_tt_dapm_routes[] = {
	/* Tweeter speaker */
	{ "TL Spk", NULL, "Tweeter Left BE_OUT" },
	{ "TR Spk", NULL, "Tweeter Right BE_OUT" },
};

static struct snd_soc_codec_conf max_98390_cml_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(MAX_98390_DEV0_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX_98390_DEV1_NAME),
		.name_prefix = "Right",
	},
};

static struct snd_soc_codec_conf max_98390_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(MAX_98390_DEV0_NAME),
		.name_prefix = "Right",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX_98390_DEV1_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX_98390_DEV2_NAME),
		.name_prefix = "Tweeter Right",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX_98390_DEV3_NAME),
		.name_prefix = "Tweeter Left",
	},
};

static struct snd_soc_dai_link_component max_98390_components[] = {
	{
		.name = MAX_98390_DEV0_NAME,
		.dai_name = MAX_98390_CODEC_DAI,
	},
	{
		.name = MAX_98390_DEV1_NAME,
		.dai_name = MAX_98390_CODEC_DAI,
	},
	{
		.name = MAX_98390_DEV2_NAME,
		.dai_name = MAX_98390_CODEC_DAI,
	},
	{
		.name = MAX_98390_DEV3_NAME,
		.dai_name = MAX_98390_CODEC_DAI,
	},
};

static const struct {
	unsigned int tx;
	unsigned int rx;
} max_98390_tdm_mask[] = {
	{.tx = 0x01, .rx = 0x3},
	{.tx = 0x02, .rx = 0x3},
	{.tx = 0x04, .rx = 0x3},
	{.tx = 0x08, .rx = 0x3},
};

static int max_98390_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_dai *codec_dai;
	int i, ret;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (i >= ARRAY_SIZE(max_98390_tdm_mask)) {
			dev_err(codec_dai->dev, "invalid codec index %d\n", i);
			return -ENODEV;
		}

		switch (dai_link->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_DSP_A:
		case SND_SOC_DAIFMT_DSP_B:
			/* 4-slot TDM */
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
						       max_98390_tdm_mask[i].tx,
						       max_98390_tdm_mask[i].rx,
						       4,
						       params_width(params));
			if (ret < 0) {
				dev_err(codec_dai->dev, "fail to set tdm slot, ret %d\n",
					ret);
				return ret;
			}
			break;
		default:
			dev_dbg(codec_dai->dev, "codec is in I2S mode\n");
			break;
		}
	}
	return 0;
}

static int max_98390_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	unsigned int num_codecs = get_num_codecs(MAX_98390_ACPI_HID);
	int ret;

	switch (num_codecs) {
	case 4:
		/* add widgets/controls/dapm for tweeter speakers */
		ret = snd_soc_dapm_new_controls(&card->dapm, max_98390_tt_dapm_widgets,
						ARRAY_SIZE(max_98390_tt_dapm_widgets));
		if (ret) {
			dev_err(rtd->dev, "unable to add tweeter dapm widgets, ret %d\n",
				ret);
			/* Don't need to add routes if widget addition failed */
			return ret;
		}

		ret = snd_soc_add_card_controls(card, max_98390_tt_kcontrols,
						ARRAY_SIZE(max_98390_tt_kcontrols));
		if (ret) {
			dev_err(rtd->dev, "unable to add tweeter controls, ret %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_dapm_add_routes(&card->dapm, max_98390_tt_dapm_routes,
					      ARRAY_SIZE(max_98390_tt_dapm_routes));
		if (ret) {
			dev_err(rtd->dev, "unable to add tweeter dapm routes, ret %d\n",
				ret);
			return ret;
		}

		fallthrough;
	case 2:
		/* add regular speakers dapm route */
		ret = snd_soc_dapm_new_controls(&card->dapm, maxim_2spk_widgets,
						ARRAY_SIZE(maxim_2spk_widgets));
		if (ret) {
			dev_err(rtd->dev, "fail to add max98390 woofer widgets, ret %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_add_card_controls(card, maxim_2spk_kcontrols,
						ARRAY_SIZE(maxim_2spk_kcontrols));
		if (ret) {
			dev_err(rtd->dev, "fail to add max98390 woofer kcontrols, ret %d\n",
				ret);
			return ret;
		}

		ret = snd_soc_dapm_add_routes(&card->dapm, max_98390_dapm_routes,
					      ARRAY_SIZE(max_98390_dapm_routes));
		if (ret) {
			dev_err(rtd->dev, "unable to add dapm routes, ret %d\n",
				ret);
			return ret;
		}
		break;
	default:
		dev_err(rtd->dev, "invalid codec number %d\n", num_codecs);
		return -EINVAL;
	}

	return ret;
}

static const struct snd_soc_ops max_98390_ops = {
	.hw_params = max_98390_hw_params,
};

void max_98390_dai_link(struct device *dev, struct snd_soc_dai_link *link)
{
	unsigned int num_codecs = get_num_codecs(MAX_98390_ACPI_HID);

	link->codecs = max_98390_components;

	switch (num_codecs) {
	case 2:
	case 4:
		link->num_codecs = num_codecs;
		break;
	default:
		dev_err(dev, "invalid codec number %d for %s\n", num_codecs,
			MAX_98390_ACPI_HID);
		break;
	}

	link->init = max_98390_init;
	link->ops = &max_98390_ops;
}
EXPORT_SYMBOL_NS(max_98390_dai_link, SND_SOC_INTEL_SOF_MAXIM_COMMON);

void max_98390_set_codec_conf(struct device *dev, struct snd_soc_card *card)
{
	unsigned int num_codecs = get_num_codecs(MAX_98390_ACPI_HID);

	card->codec_conf = max_98390_codec_conf;

	switch (num_codecs) {
	case 2:
		if (soc_intel_is_cml())
			card->codec_conf = max_98390_cml_codec_conf;

		fallthrough;
	case 4:
		card->num_configs = num_codecs;
		break;
	default:
		dev_err(dev, "invalid codec number %d for %s\n", num_codecs,
			MAX_98390_ACPI_HID);
		break;
	}
}
EXPORT_SYMBOL_NS(max_98390_set_codec_conf, SND_SOC_INTEL_SOF_MAXIM_COMMON);

/*
 * Maxim MAX98357A/MAX98360A
 */
static const struct snd_kcontrol_new max_98357a_kcontrols[] = {
	SOC_DAPM_PIN_SWITCH("Spk"),
};

static const struct snd_soc_dapm_widget max_98357a_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Spk", NULL),
};

static const struct snd_soc_dapm_route max_98357a_dapm_routes[] = {
	/* speaker */
	{"Spk", NULL, "Speaker"},
};

static struct snd_soc_dai_link_component max_98357a_components[] = {
	{
		.name = MAX_98357A_DEV0_NAME,
		.dai_name = MAX_98357A_CODEC_DAI,
	}
};

static struct snd_soc_dai_link_component max_98360a_components[] = {
	{
		.name = MAX_98360A_DEV0_NAME,
		.dai_name = MAX_98357A_CODEC_DAI,
	}
};

static int max_98357a_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, max_98357a_dapm_widgets,
					ARRAY_SIZE(max_98357a_dapm_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add dapm controls, ret %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_add_card_controls(card, max_98357a_kcontrols,
					ARRAY_SIZE(max_98357a_kcontrols));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, max_98357a_dapm_routes,
				      ARRAY_SIZE(max_98357a_dapm_routes));

	if (ret)
		dev_err(rtd->dev, "unable to add dapm routes, ret %d\n", ret);

	return ret;
}

void max_98357a_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = max_98357a_components;
	link->num_codecs = ARRAY_SIZE(max_98357a_components);
	link->init = max_98357a_init;
}
EXPORT_SYMBOL_NS(max_98357a_dai_link, SND_SOC_INTEL_SOF_MAXIM_COMMON);

void max_98360a_dai_link(struct snd_soc_dai_link *link)
{
	link->codecs = max_98360a_components;
	link->num_codecs = ARRAY_SIZE(max_98360a_components);
	link->init = max_98357a_init;
}
EXPORT_SYMBOL_NS(max_98360a_dai_link, SND_SOC_INTEL_SOF_MAXIM_COMMON);

MODULE_DESCRIPTION("ASoC Intel SOF Maxim helpers");
MODULE_LICENSE("GPL");
