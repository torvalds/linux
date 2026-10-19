// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2020 Intel Corporation
// Copyright (c) 2024 Advanced Micro Devices, Inc.

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
#include <sound/soc_sdw_utils.h>

/*
 * jack_init / jack_exit / most of rtd_init follow soc_sdw_rt_sdca_jack_common.c
 * (same SDCA headset machine-helper pattern). Vendor-specific pieces here are
 * mainly the "senarytech,jd-src" property name and senary_sdca_jack_map[].
 *
 * Keep a Senary-local file for now to mirror the existing RT layout. Sharing
 * parameterised SDCA jack helpers in snd-soc-sdw-utils (jd-src key + common
 * init/exit/rtd_init boilerplate) is intentional follow-up work, out of scope
 * for the sn624x codec series.
 */

/*
 * Note this MUST be called before snd_soc_register_card(), so that the props
 * are in place before the codec component driver's probe function parses them.
 */
static int senary_sdca_jack_add_codec_device_props(struct device *sdw_dev, unsigned long quirk)
{
	struct property_entry props[SOC_SDW_MAX_NO_PROPS] = {};
	struct fwnode_handle *fwnode;
	int ret;

	if (!SOC_SDW_JACK_JDSRC(quirk))
		return 0;

	props[0] = PROPERTY_ENTRY_U32("senarytech,jd-src", SOC_SDW_JACK_JDSRC(quirk));

	fwnode = fwnode_create_software_node(props, NULL);
	if (IS_ERR(fwnode))
		return PTR_ERR(fwnode);

	ret = device_add_software_node(sdw_dev, to_software_node(fwnode));

	fwnode_handle_put(fwnode);

	return ret;
}

static const struct snd_soc_dapm_route senary_sdca_jack_map[] = {
	{ "Headphone", NULL, "sn624x HP" },
	{ "sn624x MIC2", NULL, "Headset Mic" },
};

static struct snd_soc_jack_pin senary_sdca_jack_pins[] = {
	{
		.pin    = "Headphone",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

static const char * const need_sdca_suffix[] = {
	"sn624x"
};

int asoc_sdw_senary_sdca_jack_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = snd_soc_card_to_dapm(card);
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component;
	struct snd_soc_jack *jack;
	int ret;
	int i;

	component = dai->component;
	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s hs:%s",
					  card->components, component->name_prefix);
	if (!card->components)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(need_sdca_suffix); i++) {
		if (!strstr(component->name_prefix, need_sdca_suffix[i]))
			continue;

		card->components = devm_kasprintf(card->dev, GFP_KERNEL,
						  "%s-sdca", card->components);
		if (!card->components)
			return -ENOMEM;

		ret = snd_soc_dapm_add_routes(dapm, senary_sdca_jack_map,
					      ARRAY_SIZE(senary_sdca_jack_map));
		if (ret) {
			dev_err(card->dev,
				"senary sdca jack map addition failed: %d\n", ret);
			return ret;
		}
		break;
	}

	if (i == ARRAY_SIZE(need_sdca_suffix)) {
		dev_err(card->dev, "%s is not supported\n", component->name_prefix);
		return -EINVAL;
	}

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 &ctx->sdw_headset,
					 senary_sdca_jack_pins,
					 ARRAY_SIZE(senary_sdca_jack_pins));
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
EXPORT_SYMBOL_NS(asoc_sdw_senary_sdca_jack_rtd_init, "SND_SOC_SDW_UTILS");

int asoc_sdw_senary_sdca_jack_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (!ctx->headset_codec_dev)
		return 0;

	if (!SOC_SDW_JACK_JDSRC(ctx->mc_quirk))
		return 0;

	device_remove_software_node(ctx->headset_codec_dev);
	put_device(ctx->headset_codec_dev);
	ctx->headset_codec_dev = NULL;

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_senary_sdca_jack_exit, "SND_SOC_SDW_UTILS");

/*
 * Same control flow as asoc_sdw_rt_sdca_jack_init(); see file comment above
 * about deferred helper sharing in snd-soc-sdw-utils.
 */
int asoc_sdw_senary_sdca_jack_init(struct snd_soc_card *card,
				   struct snd_soc_dai_link *dai_links,
				   struct asoc_sdw_codec_info *info,
				   bool playback)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
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

	ret = senary_sdca_jack_add_codec_device_props(sdw_dev, ctx->mc_quirk);
	if (ret < 0) {
		put_device(sdw_dev);
		return ret;
	}
	ctx->headset_codec_dev = sdw_dev;

	return 0;
}
EXPORT_SYMBOL_NS(asoc_sdw_senary_sdca_jack_init, "SND_SOC_SDW_UTILS");
