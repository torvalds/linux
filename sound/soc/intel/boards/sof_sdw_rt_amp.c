// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022 Intel Corporation

/*
 *  sof_sdw_rt_amp - Helpers to handle RT1308/RT1316/RT1318 from generic machine driver
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-dapm.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/dmi.h>
#include "sof_sdw_common.h"
#include "sof_sdw_amp_coeff_tables.h"
#include "../../codecs/rt1308.h"

#define CODEC_NAME_SIZE	7

/* choose a larger value to resolve compatibility issues */
#define RT_AMP_MAX_BQ_REG RT1316_MAX_BQ_REG

struct rt_amp_platform_data {
	const unsigned char *bq_params;
	const unsigned int bq_params_cnt;
};

static const struct rt_amp_platform_data dell_0a5d_platform_data = {
	.bq_params = dell_0a5d_bq_params,
	.bq_params_cnt = ARRAY_SIZE(dell_0a5d_bq_params),
};

static const struct rt_amp_platform_data dell_0b00_platform_data = {
	.bq_params = dell_0b00_bq_params,
	.bq_params_cnt = ARRAY_SIZE(dell_0b00_bq_params),
};

static const struct dmi_system_id dmi_platform_data[] = {
	/* CometLake devices */
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0990")
		},
		.driver_data = (void *)&dell_0a5d_platform_data,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "098F")
		},
		.driver_data = (void *)&dell_0a5d_platform_data,
	},
	/* TigerLake devices */
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A5D")
		},
		.driver_data = (void *)&dell_0a5d_platform_data,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A5E")
		},
		.driver_data = (void *)&dell_0a5d_platform_data,
	},
	/* AlderLake devices */
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B00")
		},
		.driver_data = (void *)&dell_0b00_platform_data,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B01")
		},
		.driver_data = (void *)&dell_0b00_platform_data,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AFF")
		},
		.driver_data = (void *)&dell_0b00_platform_data,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AFE")
		},
		.driver_data = (void *)&dell_0b00_platform_data,
	},
	{},
};

static int rt_amp_add_device_props(struct device *sdw_dev)
{
	struct property_entry props[3] = {};
	struct fwnode_handle *fwnode;
	const struct dmi_system_id *dmi_data;
	const struct rt_amp_platform_data *pdata;
	unsigned char params[RT_AMP_MAX_BQ_REG];
	int ret;

	dmi_data = dmi_first_match(dmi_platform_data);
	if (!dmi_data)
		return 0;

	pdata = dmi_data->driver_data;
	memcpy(&params, pdata->bq_params, sizeof(unsigned char) * pdata->bq_params_cnt);

	props[0] = PROPERTY_ENTRY_U8_ARRAY("realtek,bq-params", params);
	props[1] = PROPERTY_ENTRY_U32("realtek,bq-params-cnt", pdata->bq_params_cnt);

	fwnode = fwnode_create_software_node(props, NULL);
	if (IS_ERR(fwnode))
		return PTR_ERR(fwnode);

	ret = device_add_software_node(sdw_dev, to_software_node(fwnode));

	fwnode_handle_put(fwnode);

	return ret;
}

/*
 * dapm routes for rt1308/rt1316/rt1318 will be registered dynamically
 * according to the number of rt1308/rt1316/rt1318 used. The first two
 * entries will be registered for one codec case, and the last two entries
 * are also registered if two 1308s/1316s/1318s are used.
 */
static const struct snd_soc_dapm_route rt1308_map[] = {
	{ "Speaker", NULL, "rt1308-1 SPOL" },
	{ "Speaker", NULL, "rt1308-1 SPOR" },
	{ "Speaker", NULL, "rt1308-2 SPOL" },
	{ "Speaker", NULL, "rt1308-2 SPOR" },
};

static const struct snd_soc_dapm_route rt1316_map[] = {
	{ "Speaker", NULL, "rt1316-1 SPOL" },
	{ "Speaker", NULL, "rt1316-1 SPOR" },
	{ "Speaker", NULL, "rt1316-2 SPOL" },
	{ "Speaker", NULL, "rt1316-2 SPOR" },
};

static const struct snd_soc_dapm_route rt1318_map[] = {
	{ "Speaker", NULL, "rt1318-1 SPOL" },
	{ "Speaker", NULL, "rt1318-1 SPOR" },
	{ "Speaker", NULL, "rt1318-2 SPOL" },
	{ "Speaker", NULL, "rt1318-2 SPOR" },
};

static const struct snd_soc_dapm_route *get_codec_name_and_route(struct snd_soc_dai *dai,
								 char *codec_name)
{
	/* get the codec name */
	snprintf(codec_name, CODEC_NAME_SIZE, "%s", dai->name);

	/* choose the right codec's map  */
	if (strcmp(codec_name, "rt1308") == 0)
		return rt1308_map;
	else if (strcmp(codec_name, "rt1316") == 0)
		return rt1316_map;
	else
		return rt1318_map;
}

int rt_amp_spk_rtd_init(struct snd_soc_pcm_runtime *rtd, struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = rtd->card;
	const struct snd_soc_dapm_route *rt_amp_map;
	char codec_name[CODEC_NAME_SIZE];
	struct snd_soc_dai *codec_dai;
	int ret;
	int i;

	rt_amp_map = get_codec_name_and_route(dai, codec_name);

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:%s",
					  card->components, codec_name);
	if (!card->components)
		return -ENOMEM;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (strstr(codec_dai->component->name_prefix, "-1"))
			ret = snd_soc_dapm_add_routes(&card->dapm, rt_amp_map, 2);
		else if (strstr(codec_dai->component->name_prefix, "-2"))
			ret = snd_soc_dapm_add_routes(&card->dapm, rt_amp_map + 2, 2);
	}

	return ret;
}

static int rt1308_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	int clk_id, clk_freq, pll_out;
	int err;

	clk_id = RT1308_PLL_S_MCLK;
	clk_freq = 38400000;

	pll_out = params_rate(params) * 512;

	/* Set rt1308 pll */
	err = snd_soc_dai_set_pll(codec_dai, 0, clk_id, clk_freq, pll_out);
	if (err < 0) {
		dev_err(card->dev, "Failed to set RT1308 PLL: %d\n", err);
		return err;
	}

	/* Set rt1308 sysclk */
	err = snd_soc_dai_set_sysclk(codec_dai, RT1308_FS_SYS_S_PLL, pll_out,
				     SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(card->dev, "Failed to set RT1308 SYSCLK: %d\n", err);
		return err;
	}

	return 0;
}

/* machine stream operations */
const struct snd_soc_ops sof_sdw_rt1308_i2s_ops = {
	.hw_params = rt1308_i2s_hw_params,
};

int sof_sdw_rt_amp_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);

	if (ctx->amp_dev1) {
		device_remove_software_node(ctx->amp_dev1);
		put_device(ctx->amp_dev1);
	}

	if (ctx->amp_dev2) {
		device_remove_software_node(ctx->amp_dev2);
		put_device(ctx->amp_dev2);
	}

	return 0;
}

int sof_sdw_rt_amp_init(struct snd_soc_card *card,
			struct snd_soc_dai_link *dai_links,
			struct sof_sdw_codec_info *info,
			bool playback)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct device *sdw_dev1, *sdw_dev2;
	int ret;

	/* Count amp number and do init on playback link only. */
	if (!playback)
		return 0;

	info->amp_num++;

	if (info->amp_num == 2) {
		sdw_dev1 = bus_find_device_by_name(&sdw_bus_type, NULL, dai_links->codecs[0].name);
		if (!sdw_dev1)
			return -EPROBE_DEFER;

		ret = rt_amp_add_device_props(sdw_dev1);
		if (ret < 0) {
			put_device(sdw_dev1);
			return ret;
		}
		ctx->amp_dev1 = sdw_dev1;

		sdw_dev2 = bus_find_device_by_name(&sdw_bus_type, NULL, dai_links->codecs[1].name);
		if (!sdw_dev2)
			return -EPROBE_DEFER;

		ret = rt_amp_add_device_props(sdw_dev2);
		if (ret < 0) {
			put_device(sdw_dev2);
			return ret;
		}
		ctx->amp_dev2 = sdw_dev2;
	}

	return 0;
}
