// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw_rt711_sdca - Helpers to handle RT711-SDCA from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include "sof_sdw_common.h"

/*
 * Note this MUST be called before snd_soc_register_card(), so that the props
 * are in place before the codec component driver's probe function parses them.
 */
static int rt_sdca_jack_add_codec_device_props(struct device *sdw_dev)
{
	struct property_entry props[MAX_NO_PROPS] = {};
	struct fwnode_handle *fwnode;
	int ret;

	if (!SOF_JACK_JDSRC(sof_sdw_quirk))
		return 0;

	props[0] = PROPERTY_ENTRY_U32("realtek,jd-src", SOF_JACK_JDSRC(sof_sdw_quirk));

	fwnode = fwnode_create_software_node(props, NULL);
	if (IS_ERR(fwnode))
		return PTR_ERR(fwnode);

	ret = device_add_software_node(sdw_dev, to_software_node(fwnode));

	fwnode_handle_put(fwnode);

	return ret;
}

static const struct snd_soc_dapm_route rt711_sdca_map[] = {
	{ "Headphone", NULL, "rt711 HP" },
	{ "rt711 MIC2", NULL, "Headset Mic" },
};

static const struct snd_soc_dapm_route rt712_sdca_map[] = {
	{ "Headphone", NULL, "rt712 HP" },
	{ "rt712 MIC2", NULL, "Headset Mic" },
};

static const struct snd_soc_dapm_route rt713_sdca_map[] = {
	{ "Headphone", NULL, "rt713 HP" },
	{ "rt713 MIC2", NULL, "Headset Mic" },
};

static const struct snd_soc_dapm_route rt722_sdca_map[] = {
	{ "Headphone", NULL, "rt722 HP" },
	{ "rt722 MIC2", NULL, "Headset Mic" },
};

static struct snd_soc_jack_pin rt_sdca_jack_pins[] = {
	{
		.pin    = "Headphone",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

static const char * const jack_codecs[] = {
	"rt711", "rt712", "rt713", "rt722"
};

/*
 * The sdca suffix is required for rt711 since there are two generations of the same chip.
 * RT713 is an SDCA device but the sdca suffix is required for backwards-compatibility with
 * previous UCM definitions.
 */
static const char * const need_sdca_suffix[] = {
	"rt711", "rt713"
};

int rt_sdca_jack_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai *codec_dai;
	struct snd_soc_component *component;
	struct snd_soc_jack *jack;
	int ret;
	int i;

	codec_dai = get_codec_dai_by_name(rtd, jack_codecs, ARRAY_SIZE(jack_codecs));
	if (!codec_dai)
		return -EINVAL;

	component = codec_dai->component;
	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s hs:%s",
					  card->components, component->name_prefix);
	if (!card->components)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(need_sdca_suffix); i++) {
		if (strstr(codec_dai->name, need_sdca_suffix[i])) {
			/* Add -sdca suffix for existing UCMs */
			card->components = devm_kasprintf(card->dev, GFP_KERNEL,
							  "%s-sdca", card->components);
			if (!card->components)
				return -ENOMEM;
			break;
		}
	}

	if (strstr(component->name_prefix, "rt711")) {
		ret = snd_soc_dapm_add_routes(&card->dapm, rt711_sdca_map,
					      ARRAY_SIZE(rt711_sdca_map));
	} else if (strstr(component->name_prefix, "rt712")) {
		ret = snd_soc_dapm_add_routes(&card->dapm, rt712_sdca_map,
					      ARRAY_SIZE(rt712_sdca_map));
	} else if (strstr(component->name_prefix, "rt713")) {
		ret = snd_soc_dapm_add_routes(&card->dapm, rt713_sdca_map,
					      ARRAY_SIZE(rt713_sdca_map));
	} else if (strstr(component->name_prefix, "rt722")) {
		ret = snd_soc_dapm_add_routes(&card->dapm, rt722_sdca_map,
					      ARRAY_SIZE(rt722_sdca_map));
	} else {
		dev_err(card->dev, "%s is not supported\n", component->name_prefix);
		return -EINVAL;
	}

	if (ret) {
		dev_err(card->dev, "rt sdca jack map addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 &ctx->sdw_headset,
					 rt_sdca_jack_pins,
					 ARRAY_SIZE(rt_sdca_jack_pins));
	if (ret) {
		dev_err(rtd->card->dev, "Headset Jack creation failed: %d\n",
			ret);
		return ret;
	}

	jack = &ctx->sdw_headset;

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, jack, NULL);

	if (ret)
		dev_err(rtd->card->dev, "Headset Jack call-back failed: %d\n",
			ret);

	return ret;
}

int sof_sdw_rt_sdca_jack_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (!ctx->headset_codec_dev)
		return 0;

	if (!SOF_JACK_JDSRC(sof_sdw_quirk))
		return 0;

	device_remove_software_node(ctx->headset_codec_dev);
	put_device(ctx->headset_codec_dev);
	ctx->headset_codec_dev = NULL;

	return 0;
}

int sof_sdw_rt_sdca_jack_init(struct snd_soc_card *card,
			      struct snd_soc_dai_link *dai_links,
			      struct sof_sdw_codec_info *info,
			      bool playback)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct device *sdw_dev;
	int ret;

	/*
	 * Jack detection should be only initialized once for headsets since
	 * the playback/capture is sharing the same jack
	 */
	if (ctx->headset_codec_dev)
		return 0;

	sdw_dev = bus_find_device_by_name(&sdw_bus_type, NULL, dai_links->codecs[0].name);
	if (!sdw_dev)
		return -EPROBE_DEFER;

	ret = rt_sdca_jack_add_codec_device_props(sdw_dev);
	if (ret < 0) {
		put_device(sdw_dev);
		return ret;
	}
	ctx->headset_codec_dev = sdw_dev;

	return 0;
}
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_BOARD_HELPERS);
