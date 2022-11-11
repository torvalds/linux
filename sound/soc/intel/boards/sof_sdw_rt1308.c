// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw_rt1308 - Helpers to handle RT1308 from generic machine driver
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

struct rt1308_platform_data {
	const unsigned char *bq_params;
	const unsigned int bq_params_cnt;
};

static const struct rt1308_platform_data dell_0a5d_platform_data = {
	.bq_params = dell_0a5d_bq_params,
	.bq_params_cnt = ARRAY_SIZE(dell_0a5d_bq_params),
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
};

static int rt1308_add_device_props(struct device *sdw_dev)
{
	struct property_entry props[3] = {};
	struct fwnode_handle *fwnode;
	const struct dmi_system_id *dmi_data;
	const struct rt1308_platform_data *pdata;
	unsigned char params[RT1308_MAX_BQ_REG];
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

static const struct snd_soc_dapm_widget rt1308_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

/*
 * dapm routes for rt1308 will be registered dynamically according
 * to the number of rt1308 used. The first two entries will be registered
 * for one codec case, and the last two entries are also registered
 * if two 1308s are used.
 */
static const struct snd_soc_dapm_route rt1308_map[] = {
	{ "Speaker", NULL, "rt1308-1 SPOL" },
	{ "Speaker", NULL, "rt1308-1 SPOR" },
	{ "Speaker", NULL, "rt1308-2 SPOL" },
	{ "Speaker", NULL, "rt1308-2 SPOR" },
};

static const struct snd_kcontrol_new rt1308_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static int first_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "%s spk:rt1308",
					  card->components);
	if (!card->components)
		return -ENOMEM;

	ret = snd_soc_add_card_controls(card, rt1308_controls,
					ARRAY_SIZE(rt1308_controls));
	if (ret) {
		dev_err(card->dev, "rt1308 controls addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_new_controls(&card->dapm, rt1308_widgets,
					ARRAY_SIZE(rt1308_widgets));
	if (ret) {
		dev_err(card->dev, "rt1308 widgets addition failed: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, rt1308_map, 2);
	if (ret)
		dev_err(rtd->dev, "failed to add first SPK map: %d\n", ret);

	return ret;
}

static int second_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_add_routes(&card->dapm, rt1308_map + 2, 2);
	if (ret)
		dev_err(rtd->dev, "failed to add second SPK map: %d\n", ret);

	return ret;
}

static int all_spk_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;

	ret = first_spk_init(rtd);
	if (ret)
		return ret;

	return second_spk_init(rtd);
}

static int rt1308_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
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
struct snd_soc_ops sof_sdw_rt1308_i2s_ops = {
	.hw_params = rt1308_i2s_hw_params,
};

int sof_sdw_rt1308_exit(struct snd_soc_card *card, struct snd_soc_dai_link *dai_link)
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

int sof_sdw_rt1308_init(struct snd_soc_card *card,
			const struct snd_soc_acpi_link_adr *link,
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
	if (info->amp_num == 1)
		dai_links->init = first_spk_init;

	if (info->amp_num == 2) {
		sdw_dev1 = bus_find_device_by_name(&sdw_bus_type, NULL, dai_links->codecs[0].name);
		if (!sdw_dev1)
			return -EPROBE_DEFER;

		ret = rt1308_add_device_props(sdw_dev1);
		if (ret < 0) {
			put_device(sdw_dev1);
			return ret;
		}
		ctx->amp_dev1 = sdw_dev1;

		sdw_dev2 = bus_find_device_by_name(&sdw_bus_type, NULL, dai_links->codecs[1].name);
		if (!sdw_dev2)
			return -EPROBE_DEFER;

		ret = rt1308_add_device_props(sdw_dev2);
		if (ret < 0) {
			put_device(sdw_dev2);
			return ret;
		}
		ctx->amp_dev2 = sdw_dev2;

		/*
		 * if two 1308s are in one dai link, the init function
		 * in this dai link will be first set for the first speaker,
		 * and it should be reset to initialize all speakers when
		 * the second speaker is found.
		 */
		if (dai_links->init)
			dai_links->init = all_spk_init;
		else
			dai_links->init = second_spk_init;
	}

	return 0;
}
