// SPDX-License-Identifier: GPL-2.0-only
// This file incorporates work covered by the following copyright notice:
// Copyright (c) 2024 Intel Corporation.

/*
 *  soc_sdw_rt_mf_sdca
 *  - Helpers to handle RT Multifunction Codec from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <sound/soc_sdw_utils.h>

#define CODEC_NAME_SIZE	6

/* dapm routes for RT-SPK will be registered dynamically */
static const struct snd_soc_dapm_route rt712_spk_map[] = {
	{ "Speaker", NULL, "rt712 SPOL" },
	{ "Speaker", NULL, "rt712 SPOR" },
};

static const struct snd_soc_dapm_route rt721_spk_map[] = {
	{ "Speaker", NULL, "rt721 SPK" },
};

static const struct snd_soc_dapm_route rt722_spk_map[] = {
	{ "Speaker", NULL, "rt722 SPK" },
};

/* Structure to map codec names to respective route arrays and sizes */
struct codec_route_map {
	const char *codec_name;
	const struct snd_soc_dapm_route *route_map;
	size_t route_size;
};

/* Codec route maps array */
static const struct codec_route_map codec_routes[] = {
	{ "rt712", rt712_spk_map, ARRAY_SIZE(rt712_spk_map) },
	{ "rt721", rt721_spk_map, ARRAY_SIZE(rt721_spk_map) },
	{ "rt722", rt722_spk_map, ARRAY_SIZE(rt722_spk_map) },
};

static const struct codec_route_map *get_codec_route_map(const char *codec_name)
{
	for (size_t i = 0; i < ARRAY_SIZE(codec_routes); i++) {
		if (strcmp(codec_routes[i].codec_name, codec_name) == 0)
			return &codec_routes[i];
	}
	return NULL;
}

int asoc_sdw_rt_mf_sdca_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	char codec_name[CODEC_NAME_SIZE];
	int ret;

	/* acquire codec name */
	snprintf(codec_name, CODEC_NAME_SIZE, "%s", dai->name);

	/* acquire corresponding route map and size */
	const struct codec_route_map *route_map = get_codec_route_map(codec_name);

	if (!route_map) {
		dev_err(rtd->dev, "failed to get codec name and route map\n");
		return -EINVAL;
	}

	/* Add routes */
	ret = snd_soc_dapm_add_routes(&card->dapm, route_map->route_map, route_map->route_size);
	if (ret)
		dev_err(rtd->dev, "failed to add rt sdca spk map: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS(asoc_sdw_rt_mf_sdca_spk_rtd_init, "SND_SOC_SDW_UTILS");
