// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw - ASOC Machine driver for Intel SoundWire platforms
 */

#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "sof_sdw_common.h"
#include "../../codecs/rt711.h"

unsigned long sof_sdw_quirk = RT711_JD1;
static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

static void log_quirks(struct device *dev)
{
	if (SOF_JACK_JDSRC(sof_sdw_quirk))
		dev_dbg(dev, "quirk realtek,jack-detect-source %ld\n",
			SOF_JACK_JDSRC(sof_sdw_quirk));
	if (sof_sdw_quirk & SOF_SDW_FOUR_SPK)
		dev_dbg(dev, "quirk SOF_SDW_FOUR_SPK enabled\n");
	if (sof_sdw_quirk & SOF_SDW_TGL_HDMI)
		dev_dbg(dev, "quirk SOF_SDW_TGL_HDMI enabled\n");
	if (sof_sdw_quirk & SOF_SDW_PCH_DMIC)
		dev_dbg(dev, "quirk SOF_SDW_PCH_DMIC enabled\n");
	if (SOF_SSP_GET_PORT(sof_sdw_quirk))
		dev_dbg(dev, "SSP port %ld\n",
			SOF_SSP_GET_PORT(sof_sdw_quirk));
	if (sof_sdw_quirk & SOF_SDW_NO_AGGREGATION)
		dev_dbg(dev, "quirk SOF_SDW_NO_AGGREGATION enabled\n");
}

static int sof_sdw_quirk_cb(const struct dmi_system_id *id)
{
	sof_sdw_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_sdw_quirk_table[] = {
	/* CometLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CometLake Client"),
		},
		.driver_data = (void *)SOF_SDW_PCH_DMIC,
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "09C6")
		},
		.driver_data = (void *)RT711_JD2,
	},
	{
		/* early version of SKU 09C6 */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0983")
		},
		.driver_data = (void *)RT711_JD2,
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "098F"),
		},
		.driver_data = (void *)(RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0990"),
		},
		.driver_data = (void *)(RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	/* IceLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Ice Lake Client"),
		},
		.driver_data = (void *)SOF_SDW_PCH_DMIC,
	},
	/* TigerLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME,
				  "Tiger Lake Client Platform"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD1 |
					SOF_SDW_PCH_DMIC |
					SOF_SSP_PORT(SOF_I2S_SSP2)),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A3E")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		/* another SKU of Dell Latitude 9520 */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A3F")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		/* Dell XPS 9710 */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A5D")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A5E")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Volteer"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					SOF_SDW_FOUR_SPK |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Ripto"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					SOF_SDW_FOUR_SPK),
	},
	{
		/*
		 * this entry covers multiple HP SKUs. The family name
		 * does not seem robust enough, so we use a partial
		 * match that ignores the product name suffix
		 * (e.g. 15-eb1xxx, 14t-ea000 or 13-aw2xxx)
		 */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Spectre x360 Conv"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					RT711_JD1),
	},
	{
		/*
		 * this entry covers HP Spectre x360 where the DMI information
		 * changed somehow
		 */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_BOARD_NAME, "8709"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					RT711_JD1),
	},
	{
		/* NUC15 'Bishop County' LAPBC510 and LAPBC710 skews */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LAPBC"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					RT711_JD1),
	},
	{
		/* NUC15 LAPBC710 skews */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "LAPBC710"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					RT711_JD1),
	},
	{
		/* NUC15 'Rooks County' LAPRC510 and LAPRC710 skews */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel(R) Client Systems"),
			DMI_MATCH(DMI_PRODUCT_NAME, "LAPRC"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					RT711_JD2_100K),
	},
	/* TigerLake-SDCA devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A32")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A45")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	/* AlderLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Alder Lake Client Platform"),
		},
		.driver_data = (void *)(RT711_JD2_100K |
					SOF_SDW_TGL_HDMI |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Brya"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
					SOF_SDW_FOUR_SPK |
					SOF_BT_OFFLOAD_SSP(2) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AF0")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AF3"),
		},
		/* No Jack */
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AFE")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AFF")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B00")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B01")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B11")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B12")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B13"),
		},
		/* No Jack */
		.driver_data = (void *)SOF_SDW_TGL_HDMI,
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B14"),
		},
		/* No Jack */
		.driver_data = (void *)SOF_SDW_TGL_HDMI,
	},

	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B29"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B34"),
		},
		/* No Jack */
		.driver_data = (void *)SOF_SDW_TGL_HDMI,
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "OMEN by HP Gaming Laptop 16"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	/* RaptorLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0BDA")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C10"),
		},
		/* No Jack */
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C11")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C40")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C4F")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2 |
					SOF_SDW_FOUR_SPK),
	},
	/* MeteorLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Intel_mtlrvp"),
		},
		.driver_data = (void *)(RT711_JD1),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Meteor Lake Client Platform"),
		},
		.driver_data = (void *)(RT711_JD2_100K),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Rex"),
		},
		.driver_data = (void *)(SOF_SDW_PCH_DMIC |
					SOF_BT_OFFLOAD_SSP(1) |
					SOF_SSP_BT_OFFLOAD_PRESENT),
	},
	/* LunarLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Lunar Lake Client Platform"),
		},
		.driver_data = (void *)(RT711_JD2),
	},
	{}
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

/* these wrappers are only needed to avoid typecast compilation errors */
int sdw_startup(struct snd_pcm_substream *substream)
{
	return sdw_startup_stream(substream);
}

int sdw_prepare(struct snd_pcm_substream *substream)
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

int sdw_trigger(struct snd_pcm_substream *substream, int cmd)
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

int sdw_hw_params(struct snd_pcm_substream *substream,
		  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	int ch = params_channels(params);
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai;
	unsigned int ch_mask;
	int num_codecs;
	int step;
	int i;
	int j;

	if (!rtd->dai_link->codec_ch_maps)
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
	for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
		for_each_rtd_codec_dais(rtd, j, codec_dai) {
			if (rtd->dai_link->codec_ch_maps[j].connected_cpu_id != i)
				continue;
			rtd->dai_link->codec_ch_maps[j].ch_mask = ch_mask << (j * step);
		}
	}
	return 0;
}

int sdw_hw_free(struct snd_pcm_substream *substream)
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

void sdw_shutdown(struct snd_pcm_substream *substream)
{
	sdw_shutdown_stream(substream);
}

static const struct snd_soc_ops sdw_ops = {
	.startup = sdw_startup,
	.prepare = sdw_prepare,
	.trigger = sdw_trigger,
	.hw_params = sdw_hw_params,
	.hw_free = sdw_hw_free,
	.shutdown = sdw_shutdown,
};

static struct sof_sdw_codec_info codec_info_list[] = {
	{
		.part_id = 0x700,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt700-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_rt700_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x711,
		.version_id = 3,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt711-sdca-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_rt_sdca_jack_init,
				.exit = sof_sdw_rt_sdca_jack_exit,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x711,
		.version_id = 2,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt711-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_rt711_init,
				.exit = sof_sdw_rt711_exit,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x712,
		.version_id = 3,
		.dais =	{
			{
				.direction = {true, true},
				.dai_name = "rt712-sdca-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_rt_sdca_jack_init,
				.exit = sof_sdw_rt_sdca_jack_exit,
			},
			{
				.direction = {true, false},
				.dai_name = "rt712-sdca-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.init = sof_sdw_rt712_spk_init,
			},
		},
		.dai_num = 2,
	},
	{
		.part_id = 0x1712,
		.version_id = 3,
		.dais =	{
			{
				.direction = {false, true},
				.dai_name = "rt712-sdca-dmic-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = sof_sdw_rt712_sdca_dmic_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x713,
		.version_id = 3,
		.dais =	{
			{
				.direction = {true, true},
				.dai_name = "rt712-sdca-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_rt_sdca_jack_init,
				.exit = sof_sdw_rt_sdca_jack_exit,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x1713,
		.version_id = 3,
		.dais =	{
			{
				.direction = {false, true},
				.dai_name = "rt712-sdca-dmic-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = sof_sdw_rt712_sdca_dmic_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x1308,
		.acpi_id = "10EC1308",
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "rt1308-aif",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.init = sof_sdw_rt_amp_init,
				.exit = sof_sdw_rt_amp_exit,
			},
		},
		.dai_num = 1,
		.ops = &sof_sdw_rt1308_i2s_ops,
	},
	{
		.part_id = 0x1316,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt1316-aif",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_AMP_IN_DAI_ID},
				.init = sof_sdw_rt_amp_init,
				.exit = sof_sdw_rt_amp_exit,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x1318,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt1318-aif",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_AMP_IN_DAI_ID},
				.init = sof_sdw_rt_amp_init,
				.exit = sof_sdw_rt_amp_exit,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x714,
		.version_id = 3,
		.ignore_pch_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = sof_sdw_rt715_sdca_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x715,
		.version_id = 3,
		.ignore_pch_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = sof_sdw_rt715_sdca_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x714,
		.version_id = 2,
		.ignore_pch_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = sof_sdw_rt715_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x715,
		.version_id = 2,
		.ignore_pch_dmic = true,
		.dais = {
			{
				.direction = {false, true},
				.dai_name = "rt715-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = sof_sdw_rt715_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x8373,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "max98373-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_AMP_IN_DAI_ID},
				.init = sof_sdw_maxim_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x8363,
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "max98363-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.init = sof_sdw_maxim_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x5682,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt5682-sdw",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_rt5682_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x3556,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "cs35l56-sdw1",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_AMP_IN_DAI_ID},
				.init = sof_sdw_cs_amp_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x4242,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "cs42l42-sdw",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_cs42l42_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x4243,
		.codec_name = "cs42l43-codec",
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "cs42l43-dp5",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.init = sof_sdw_cs42l43_hs_init,
			},
			{
				.direction = {false, true},
				.dai_name = "cs42l43-dp1",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = sof_sdw_cs42l43_dmic_init,
			},
			{
				.direction = {false, true},
				.dai_name = "cs42l43-dp2",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_JACK_IN_DAI_ID},
			},
		},
		.dai_num = 3,
	},
	{
		.part_id = 0xaaaa, /* generic codec mockup */
		.version_id = 0,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = NULL,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0xaa55, /* headset codec mockup */
		.version_id = 0,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = NULL,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x55aa, /* amplifier mockup */
		.version_id = 0,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "sdw-mockup-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_AMP_IN_DAI_ID},
				.init = NULL,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x5555,
		.version_id = 0,
		.dais = {
			{
				.dai_name = "sdw-mockup-aif1",
				.direction = {false, true},
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.init = NULL,
			},
		},
		.dai_num = 1,
	},
};

static inline int find_codec_info_part(const u64 adr)
{
	unsigned int part_id, sdw_version;
	int i;

	part_id = SDW_PART_ID(adr);
	sdw_version = SDW_VERSION(adr);
	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		/*
		 * A codec info is for all sdw version with the part id if
		 * version_id is not specified in the codec info.
		 */
		if (part_id == codec_info_list[i].part_id &&
		    (!codec_info_list[i].version_id ||
		     sdw_version == codec_info_list[i].version_id))
			return i;

	return -EINVAL;

}

static inline int find_codec_info_acpi(const u8 *acpi_id)
{
	int i;

	if (!acpi_id[0])
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		if (!memcmp(codec_info_list[i].acpi_id, acpi_id, ACPI_ID_LEN))
			return i;

	return -EINVAL;
}

/*
 * get BE dailink number and CPU DAI number based on sdw link adr.
 * Since some sdw slaves may be aggregated, the CPU DAI number
 * may be larger than the number of BE dailinks.
 */
static int get_dailink_info(struct device *dev,
			    const struct snd_soc_acpi_link_adr *adr_link,
			    int *sdw_be_num, int *codecs_num)
{
	bool group_visited[SDW_MAX_GROUPS];
	bool no_aggregation;
	int i;
	int j;

	no_aggregation = sof_sdw_quirk & SOF_SDW_NO_AGGREGATION;
	*sdw_be_num  = 0;

	if (!adr_link)
		return -EINVAL;

	for (i = 0; i < SDW_MAX_GROUPS; i++)
		group_visited[i] = false;

	for (; adr_link->num_adr; adr_link++) {
		const struct snd_soc_acpi_endpoint *endpoint;
		struct sof_sdw_codec_info *codec_info;
		int codec_index;
		int stream;
		u64 adr;

		/* make sure the link mask has a single bit set */
		if (!is_power_of_2(adr_link->mask))
			return -EINVAL;

		for (i = 0; i < adr_link->num_adr; i++) {
			adr = adr_link->adr_d[i].adr;
			codec_index = find_codec_info_part(adr);
			if (codec_index < 0)
				return codec_index;

			codec_info = &codec_info_list[codec_index];

			*codecs_num += codec_info->dai_num;

			if (!adr_link->adr_d[i].name_prefix) {
				dev_err(dev, "codec 0x%llx does not have a name prefix\n",
					adr_link->adr_d[i].adr);
				return -EINVAL;
			}

			endpoint = adr_link->adr_d[i].endpoints;
			if (endpoint->aggregated && !endpoint->group_id) {
				dev_err(dev, "invalid group id on link %x\n",
					adr_link->mask);
				return -EINVAL;
			}

			for (j = 0; j < codec_info->dai_num; j++) {
				/* count DAI number for playback and capture */
				for_each_pcm_streams(stream) {
					if (!codec_info->dais[j].direction[stream])
						continue;

					/* count BE for each non-aggregated slave or group */
					if (!endpoint->aggregated || no_aggregation ||
					    !group_visited[endpoint->group_id])
						(*sdw_be_num)++;
				}
			}

			if (endpoint->aggregated)
				group_visited[endpoint->group_id] = true;
		}
	}

	return 0;
}

static void init_dai_link(struct device *dev, struct snd_soc_dai_link *dai_links,
			  int *be_id, char *name, int playback, int capture,
			  struct snd_soc_dai_link_component *cpus, int cpus_num,
			  struct snd_soc_dai_link_component *codecs, int codecs_num,
			  int (*init)(struct snd_soc_pcm_runtime *rtd),
			  const struct snd_soc_ops *ops)
{
	dev_dbg(dev, "create dai link %s, id %d\n", name, *be_id);
	dai_links->id = (*be_id)++;
	dai_links->name = name;
	dai_links->platforms = platform_component;
	dai_links->num_platforms = ARRAY_SIZE(platform_component);
	dai_links->no_pcm = 1;
	dai_links->cpus = cpus;
	dai_links->num_cpus = cpus_num;
	dai_links->codecs = codecs;
	dai_links->num_codecs = codecs_num;
	dai_links->dpcm_playback = playback;
	dai_links->dpcm_capture = capture;
	dai_links->init = init;
	dai_links->ops = ops;
}

static int init_simple_dai_link(struct device *dev, struct snd_soc_dai_link *dai_links,
				int *be_id, char *name, int playback, int capture,
				const char *cpu_dai_name,
				const char *codec_name, const char *codec_dai_name,
				int (*init)(struct snd_soc_pcm_runtime *rtd),
				const struct snd_soc_ops *ops)
{
	struct snd_soc_dai_link_component *dlc;

	/* Allocate two DLCs one for the CPU, one for the CODEC */
	dlc = devm_kcalloc(dev, 2, sizeof(*dlc), GFP_KERNEL);
	if (!dlc || !name || !cpu_dai_name || !codec_name || !codec_dai_name)
		return -ENOMEM;

	dlc[0].dai_name = cpu_dai_name;

	dlc[1].name = codec_name;
	dlc[1].dai_name = codec_dai_name;

	init_dai_link(dev, dai_links, be_id, name, playback, capture,
		      &dlc[0], 1, &dlc[1], 1, init, ops);

	return 0;
}

static bool is_unique_device(const struct snd_soc_acpi_link_adr *adr_link,
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

static int fill_sdw_codec_dlc(struct device *dev,
			      const struct snd_soc_acpi_link_adr *adr_link,
			      struct snd_soc_dai_link_component *codec,
			      int adr_index, int dai_index)
{
	unsigned int sdw_version, unique_id, mfg_id, link_id, part_id, class_id;
	u64 adr = adr_link->adr_d[adr_index].adr;
	int codec_index;

	codec_index = find_codec_info_part(adr);
	if (codec_index < 0)
		return codec_index;

	sdw_version = SDW_VERSION(adr);
	link_id = SDW_DISCO_LINK_ID(adr);
	unique_id = SDW_UNIQUE_ID(adr);
	mfg_id = SDW_MFG_ID(adr);
	part_id = SDW_PART_ID(adr);
	class_id = SDW_CLASS_ID(adr);

	if (codec_info_list[codec_index].codec_name)
		codec->name = devm_kstrdup(dev,
					   codec_info_list[codec_index].codec_name,
					   GFP_KERNEL);
	else if (is_unique_device(adr_link, sdw_version, mfg_id, part_id,
				  class_id, adr_index))
		codec->name = devm_kasprintf(dev, GFP_KERNEL,
					     "sdw:%01x:%04x:%04x:%02x", link_id,
					     mfg_id, part_id, class_id);
	else
		codec->name = devm_kasprintf(dev, GFP_KERNEL,
					     "sdw:%01x:%04x:%04x:%02x:%01x", link_id,
					     mfg_id, part_id, class_id, unique_id);

	if (!codec->name)
		return -ENOMEM;

	codec->dai_name = codec_info_list[codec_index].dais[dai_index].dai_name;

	return 0;
}

static int set_codec_init_func(struct snd_soc_card *card,
			       const struct snd_soc_acpi_link_adr *adr_link,
			       struct snd_soc_dai_link *dai_links,
			       bool playback, int group_id, int adr_index, int dai_index)
{
	int i = adr_index;

	do {
		/*
		 * Initialize the codec. If codec is part of an aggregated
		 * group (group_id>0), initialize all codecs belonging to
		 * same group.
		 * The first link should start with adr_link->adr_d[adr_index]
		 * because that is the device that we want to initialize and
		 * we should end immediately if it is not aggregated (group_id=0)
		 */
		for ( ; i < adr_link->num_adr; i++) {
			int codec_index;

			codec_index = find_codec_info_part(adr_link->adr_d[i].adr);
			if (codec_index < 0)
				return codec_index;

			/* The group_id is > 0 iff the codec is aggregated */
			if (adr_link->adr_d[i].endpoints->group_id != group_id)
				continue;

			if (codec_info_list[codec_index].dais[dai_index].init)
				codec_info_list[codec_index].dais[dai_index].init(card,
						adr_link,
						dai_links,
						&codec_info_list[codec_index],
						playback);
			if (!group_id)
				return 0;
		}

		i = 0;
		adr_link++;
	} while (adr_link->mask);

	return 0;
}

/*
 * check endpoint status in slaves and gather link ID for all slaves in
 * the same group to generate different CPU DAI. Now only support
 * one sdw link with all slaves set with only single group id.
 *
 * one slave on one sdw link with aggregated = 0
 * one sdw BE DAI <---> one-cpu DAI <---> one-codec DAI
 *
 * two or more slaves on one sdw link with aggregated = 0
 * one sdw BE DAI  <---> one-cpu DAI <---> multi-codec DAIs
 *
 * multiple links with multiple slaves with aggregated = 1
 * one sdw BE DAI  <---> 1 .. N CPU DAIs <----> 1 .. N codec DAIs
 */
static int get_slave_info(const struct snd_soc_acpi_link_adr *adr_link,
			  struct device *dev, int *cpu_dai_id, int *cpu_dai_num,
			  int *codec_num, unsigned int *group_id,
			  int adr_index)
{
	bool no_aggregation = sof_sdw_quirk & SOF_SDW_NO_AGGREGATION;
	int i;

	if (!adr_link->adr_d[adr_index].endpoints->aggregated || no_aggregation) {
		cpu_dai_id[0] = ffs(adr_link->mask) - 1;
		*cpu_dai_num = 1;
		*codec_num = 1;
		*group_id = 0;
		return 0;
	}

	*codec_num = 0;
	*cpu_dai_num = 0;
	*group_id = adr_link->adr_d[adr_index].endpoints->group_id;

	/* Count endpoints with the same group_id in the adr_link */
	for (; adr_link && adr_link->num_adr; adr_link++) {
		unsigned int link_codecs = 0;

		for (i = 0; i < adr_link->num_adr; i++) {
			if (adr_link->adr_d[i].endpoints->aggregated &&
			    adr_link->adr_d[i].endpoints->group_id == *group_id)
				link_codecs++;
		}

		if (link_codecs) {
			*codec_num += link_codecs;

			if (*cpu_dai_num >= SDW_MAX_CPU_DAIS) {
				dev_err(dev, "cpu_dai_id array overflowed\n");
				return -EINVAL;
			}

			cpu_dai_id[(*cpu_dai_num)++] = ffs(adr_link->mask) - 1;
		}
	}

	return 0;
}

static void set_dailink_map(struct snd_soc_dai_link_codec_ch_map *sdw_codec_ch_maps,
			    int codec_num, int cpu_num)
{
	int step;
	int i;

	step = codec_num / cpu_num;
	for (i = 0; i < codec_num; i++)
		sdw_codec_ch_maps[i].connected_cpu_id = i / step;
}

static const char * const type_strings[] = {"SimpleJack", "SmartAmp", "SmartMic"};

static int create_sdw_dailink(struct snd_soc_card *card, int *link_index,
			      struct snd_soc_dai_link *dai_links, int sdw_be_num,
			      const struct snd_soc_acpi_link_adr *adr_link,
			      struct snd_soc_codec_conf *codec_conf,
			      int codec_count, int *be_id,
			      int *codec_conf_index,
			      bool *ignore_pch_dmic,
			      bool append_dai_type,
			      int adr_index,
			      int dai_index)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct device *dev = card->dev;
	const struct snd_soc_acpi_link_adr *adr_link_next;
	struct snd_soc_dai_link_component *codecs;
	struct snd_soc_dai_link_component *cpus;
	struct sof_sdw_codec_info *codec_info;
	int cpu_dai_id[SDW_MAX_CPU_DAIS];
	int cpu_dai_num;
	unsigned int group_id;
	int codec_dlc_index = 0;
	int codec_index;
	int codec_num;
	int stream;
	int i = 0;
	int j, k;
	int ret;

	ret = get_slave_info(adr_link, dev, cpu_dai_id, &cpu_dai_num, &codec_num,
			     &group_id, adr_index);
	if (ret)
		return ret;

	codecs = devm_kcalloc(dev, codec_num, sizeof(*codecs), GFP_KERNEL);
	if (!codecs)
		return -ENOMEM;

	/* generate codec name on different links in the same group */
	j = adr_index;
	for (adr_link_next = adr_link; adr_link_next && adr_link_next->num_adr &&
	     i < cpu_dai_num; adr_link_next++) {
		/* skip the link excluded by this processed group */
		if (cpu_dai_id[i] != ffs(adr_link_next->mask) - 1)
			continue;

		/* j reset after loop, adr_index only applies to first link */
		for (; j < adr_link_next->num_adr && codec_dlc_index < codec_num; j++) {
			const struct snd_soc_acpi_endpoint *endpoints;

			endpoints = adr_link_next->adr_d[j].endpoints;

			if (group_id && (!endpoints->aggregated ||
					 endpoints->group_id != group_id))
				continue;

			/* sanity check */
			if (*codec_conf_index >= codec_count) {
				dev_err(dev, "codec_conf array overflowed\n");
				return -EINVAL;
			}

			ret = fill_sdw_codec_dlc(dev, adr_link_next,
						 &codecs[codec_dlc_index],
						 j, dai_index);
			if (ret)
				return ret;

			codec_conf[*codec_conf_index].dlc = codecs[codec_dlc_index];
			codec_conf[*codec_conf_index].name_prefix =
					adr_link_next->adr_d[j].name_prefix;

			codec_dlc_index++;
			(*codec_conf_index)++;
		}
		j = 0;

		/* check next link to create codec dai in the processed group */
		i++;
	}

	/* find codec info to create BE DAI */
	codec_index = find_codec_info_part(adr_link->adr_d[adr_index].adr);
	if (codec_index < 0)
		return codec_index;
	codec_info = &codec_info_list[codec_index];

	if (codec_info->ignore_pch_dmic)
		*ignore_pch_dmic = true;

	for_each_pcm_streams(stream) {
		struct snd_soc_dai_link_codec_ch_map *sdw_codec_ch_maps;
		char *name, *cpu_name;
		int playback, capture;
		static const char * const sdw_stream_name[] = {
			"SDW%d-Playback",
			"SDW%d-Capture",
			"SDW%d-Playback-%s",
			"SDW%d-Capture-%s",
		};

		if (!codec_info->dais[dai_index].direction[stream])
			continue;

		*be_id = codec_info->dais[dai_index].dailink[stream];
		if (*be_id < 0) {
			dev_err(dev, "Invalid dailink id %d\n", *be_id);
			return -EINVAL;
		}

		sdw_codec_ch_maps = devm_kcalloc(dev, codec_num,
						 sizeof(*sdw_codec_ch_maps), GFP_KERNEL);
		if (!sdw_codec_ch_maps)
			return -ENOMEM;

		/* create stream name according to first link id */
		if (append_dai_type) {
			name = devm_kasprintf(dev, GFP_KERNEL,
					      sdw_stream_name[stream + 2], cpu_dai_id[0],
					      type_strings[codec_info->dais[dai_index].dai_type]);
		} else {
			name = devm_kasprintf(dev, GFP_KERNEL,
					      sdw_stream_name[stream], cpu_dai_id[0]);
		}
		if (!name)
			return -ENOMEM;

		cpus = devm_kcalloc(dev, cpu_dai_num, sizeof(*cpus), GFP_KERNEL);
		if (!cpus)
			return -ENOMEM;

		/*
		 * generate CPU DAI name base on the sdw link ID and
		 * PIN ID with offset of 2 according to sdw dai driver.
		 */
		for (k = 0; k < cpu_dai_num; k++) {
			cpu_name = devm_kasprintf(dev, GFP_KERNEL,
						  "SDW%d Pin%d", cpu_dai_id[k],
						  ctx->sdw_pin_index[cpu_dai_id[k]]++);
			if (!cpu_name)
				return -ENOMEM;

			cpus[k].dai_name = cpu_name;
		}

		/*
		 * We create sdw dai links at first stage, so link index should
		 * not be larger than sdw_be_num
		 */
		if (*link_index >= sdw_be_num) {
			dev_err(dev, "invalid dai link index %d\n", *link_index);
			return -EINVAL;
		}

		playback = (stream == SNDRV_PCM_STREAM_PLAYBACK);
		capture = (stream == SNDRV_PCM_STREAM_CAPTURE);

		init_dai_link(dev, dai_links + *link_index, be_id, name,
			      playback, capture, cpus, cpu_dai_num, codecs, codec_num,
			      NULL, &sdw_ops);

		/*
		 * SoundWire DAILINKs use 'stream' functions and Bank Switch operations
		 * based on wait_for_completion(), tag them as 'nonatomic'.
		 */
		dai_links[*link_index].nonatomic = true;

		set_dailink_map(sdw_codec_ch_maps, codec_num, cpu_dai_num);
		dai_links[*link_index].codec_ch_maps = sdw_codec_ch_maps;
		ret = set_codec_init_func(card, adr_link, dai_links + (*link_index)++,
					  playback, group_id, adr_index, dai_index);
		if (ret < 0) {
			dev_err(dev, "failed to init codec %d\n", codec_index);
			return ret;
		}
	}

	return 0;
}

static int sof_card_dai_links_create(struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(card->dev);
	int sdw_be_num = 0, ssp_num = 0, dmic_num = 0, bt_num = 0;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	const struct snd_soc_acpi_link_adr *adr_link = mach_params->links;
	bool aggregation = !(sof_sdw_quirk & SOF_SDW_NO_AGGREGATION);
	struct snd_soc_codec_conf *codec_conf;
	bool append_dai_type = false;
	bool ignore_pch_dmic = false;
	int codec_conf_num = 0;
	int codec_conf_index = 0;
	bool group_generated[SDW_MAX_GROUPS] = { };
	int ssp_codec_index, ssp_mask;
	struct snd_soc_dai_link *dai_links;
	int num_links, link_index = 0;
	char *name, *cpu_dai_name;
	char *codec_name, *codec_dai_name;
	int i, j, be_id = 0;
	int codec_index;
	int hdmi_num;
	int ret;

	ret = get_dailink_info(dev, adr_link, &sdw_be_num, &codec_conf_num);
	if (ret < 0) {
		dev_err(dev, "failed to get sdw link info %d\n", ret);
		return ret;
	}

	/*
	 * on generic tgl platform, I2S or sdw mode is supported
	 * based on board rework. A ACPI device is registered in
	 * system only when I2S mode is supported, not sdw mode.
	 * Here check ACPI ID to confirm I2S is supported.
	 */
	ssp_codec_index = find_codec_info_acpi(mach->id);
	if (ssp_codec_index >= 0) {
		ssp_mask = SOF_SSP_GET_PORT(sof_sdw_quirk);
		ssp_num = hweight_long(ssp_mask);
	}

	if (mach_params->codec_mask & IDISP_CODEC_MASK)
		ctx->hdmi.idisp_codec = true;

	if (sof_sdw_quirk & SOF_SDW_TGL_HDMI)
		hdmi_num = SOF_TGL_HDMI_COUNT;
	else
		hdmi_num = SOF_PRE_TGL_HDMI_COUNT;

	/* enable dmic01 & dmic16k */
	if (sof_sdw_quirk & SOF_SDW_PCH_DMIC || mach_params->dmic_num)
		dmic_num = 2;

	if (sof_sdw_quirk & SOF_SSP_BT_OFFLOAD_PRESENT)
		bt_num = 1;

	dev_dbg(dev, "sdw %d, ssp %d, dmic %d, hdmi %d, bt: %d\n",
		sdw_be_num, ssp_num, dmic_num,
		ctx->hdmi.idisp_codec ? hdmi_num : 0, bt_num);

	/* allocate BE dailinks */
	num_links = sdw_be_num + ssp_num + dmic_num + hdmi_num + bt_num;
	dai_links = devm_kcalloc(dev, num_links, sizeof(*dai_links), GFP_KERNEL);
	if (!dai_links)
		return -ENOMEM;

	/* allocate codec conf, will be populated when dailinks are created */
	codec_conf = devm_kcalloc(dev, codec_conf_num, sizeof(*codec_conf),
				  GFP_KERNEL);
	if (!codec_conf)
		return -ENOMEM;

	/* SDW */
	if (!sdw_be_num)
		goto SSP;

	for (i = 0; i < SDW_MAX_LINKS; i++)
		ctx->sdw_pin_index[i] = SDW_INTEL_BIDIR_PDI_BASE;

	for (; adr_link->num_adr; adr_link++) {
		/*
		 * If there are two or more different devices on the same sdw link, we have to
		 * append the codec type to the dai link name to prevent duplicated dai link name.
		 * The same type devices on the same sdw link will be in the same
		 * snd_soc_acpi_adr_device array. They won't be described in different adr_links.
		 */
		for (i = 0; i < adr_link->num_adr; i++) {
			/* find codec info to get dai_num */
			codec_index = find_codec_info_part(adr_link->adr_d[i].adr);
			if (codec_index < 0)
				return codec_index;
			if (codec_info_list[codec_index].dai_num > 1) {
				append_dai_type = true;
				goto out;
			}
			for (j = 0; j < i; j++) {
				if ((SDW_PART_ID(adr_link->adr_d[i].adr) !=
				    SDW_PART_ID(adr_link->adr_d[j].adr)) ||
				    (SDW_MFG_ID(adr_link->adr_d[i].adr) !=
				    SDW_MFG_ID(adr_link->adr_d[j].adr))) {
					append_dai_type = true;
					goto out;
				}
			}
		}
	}
out:

	/* generate DAI links by each sdw link */
	for (adr_link = mach_params->links ; adr_link->num_adr; adr_link++) {
		for (i = 0; i < adr_link->num_adr; i++) {
			const struct snd_soc_acpi_endpoint *endpoint;

			endpoint = adr_link->adr_d[i].endpoints;

			/* this group has been generated */
			if (endpoint->aggregated &&
			    group_generated[endpoint->group_id])
				continue;

			/* find codec info to get dai_num */
			codec_index = find_codec_info_part(adr_link->adr_d[i].adr);
			if (codec_index < 0)
				return codec_index;

			for (j = 0; j < codec_info_list[codec_index].dai_num ; j++) {
				ret = create_sdw_dailink(card, &link_index, dai_links,
							 sdw_be_num, adr_link,
							 codec_conf, codec_conf_num,
							 &be_id, &codec_conf_index,
							 &ignore_pch_dmic, append_dai_type, i, j);
				if (ret < 0) {
					dev_err(dev, "failed to create dai link %d\n", link_index);
					return ret;
				}
			}

			if (aggregation && endpoint->aggregated)
				group_generated[endpoint->group_id] = true;
		}
	}

SSP:
	/* SSP */
	if (!ssp_num)
		goto DMIC;

	for (i = 0, j = 0; ssp_mask; i++, ssp_mask >>= 1) {
		struct sof_sdw_codec_info *info;
		int playback, capture;

		if (!(ssp_mask & 0x1))
			continue;

		info = &codec_info_list[ssp_codec_index];

		name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec", i);
		cpu_dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", i);
		codec_name = devm_kasprintf(dev, GFP_KERNEL, "i2c-%s:0%d",
					    info->acpi_id, j++);

		playback = info->dais[0].direction[SNDRV_PCM_STREAM_PLAYBACK];
		capture = info->dais[0].direction[SNDRV_PCM_STREAM_CAPTURE];

		ret = init_simple_dai_link(dev, dai_links + link_index, &be_id, name,
					   playback, capture, cpu_dai_name,
					   codec_name, info->dais[0].dai_name,
					   NULL, info->ops);
		if (ret)
			return ret;

		ret = info->dais[0].init(card, NULL, dai_links + link_index, info, 0);
		if (ret < 0)
			return ret;

		link_index++;
	}

DMIC:
	/* dmic */
	if (dmic_num > 0) {
		if (ignore_pch_dmic) {
			dev_warn(dev, "Ignoring PCH DMIC\n");
			goto HDMI;
		}

		ret = init_simple_dai_link(dev, dai_links + link_index, &be_id, "dmic01",
					   0, 1, // DMIC only supports capture
					   "DMIC01 Pin", "dmic-codec", "dmic-hifi",
					   sof_sdw_dmic_init, NULL);
		if (ret)
			return ret;

		link_index++;

		ret = init_simple_dai_link(dev, dai_links + link_index, &be_id, "dmic16k",
					   0, 1, // DMIC only supports capture
					   "DMIC16k Pin", "dmic-codec", "dmic-hifi",
					   /* don't call sof_sdw_dmic_init() twice */
					   NULL, NULL);
		if (ret)
			return ret;

		link_index++;
	}

HDMI:
	/* HDMI */
	for (i = 0; i < hdmi_num; i++) {
		name = devm_kasprintf(dev, GFP_KERNEL, "iDisp%d", i + 1);
		cpu_dai_name = devm_kasprintf(dev, GFP_KERNEL, "iDisp%d Pin", i + 1);

		if (ctx->hdmi.idisp_codec) {
			codec_name = "ehdaudio0D2";
			codec_dai_name = devm_kasprintf(dev, GFP_KERNEL,
							"intel-hdmi-hifi%d", i + 1);
		} else {
			codec_name = "snd-soc-dummy";
			codec_dai_name = "snd-soc-dummy-dai";
		}

		ret = init_simple_dai_link(dev, dai_links + link_index, &be_id, name,
					   1, 0, // HDMI only supports playback
					   cpu_dai_name, codec_name, codec_dai_name,
					   i == 0 ? sof_sdw_hdmi_init : NULL, NULL);
		if (ret)
			return ret;

		link_index++;
	}

	if (sof_sdw_quirk & SOF_SSP_BT_OFFLOAD_PRESENT) {
		int port = (sof_sdw_quirk & SOF_BT_OFFLOAD_SSP_MASK) >>
				SOF_BT_OFFLOAD_SSP_SHIFT;

		name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-BT", port);
		cpu_dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", port);

		ret = init_simple_dai_link(dev, dai_links + link_index, &be_id, name,
					   1, 1, cpu_dai_name, snd_soc_dummy_dlc.name,
					   snd_soc_dummy_dlc.dai_name, NULL, NULL);
		if (ret)
			return ret;
	}

	card->dai_link = dai_links;
	card->num_links = num_links;

	card->codec_conf = codec_conf;
	card->num_configs = codec_conf_num;

	return 0;
}

static int sof_sdw_card_late_probe(struct snd_soc_card *card)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	int ret = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++) {
		if (codec_info_list[i].codec_card_late_probe) {
			ret = codec_info_list[i].codec_card_late_probe(card);

			if (ret < 0)
				return ret;
		}
	}

	if (ctx->hdmi.idisp_codec)
		ret = sof_sdw_hdmi_card_late_probe(card);

	return ret;
}

/* SoC card */
static const char sdw_card_long_name[] = "Intel Soundwire SOF";

static struct snd_soc_card card_sof_sdw = {
	.name = "soundwire",
	.owner = THIS_MODULE,
	.late_probe = sof_sdw_card_late_probe,
};

/* helper to get the link that the codec DAI is used */
static struct snd_soc_dai_link *mc_find_codec_dai_used(struct snd_soc_card *card,
						       const char *dai_name)
{
	struct snd_soc_dai_link *dai_link;
	int i;
	int j;

	for_each_card_prelinks(card, i, dai_link) {
		for (j = 0; j < dai_link->num_codecs; j++) {
			/* Check each codec in a link */
			if (!strcmp(dai_link->codecs[j].dai_name, dai_name))
				return dai_link;
		}
	}
	return NULL;
}

static void mc_dailink_exit_loop(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	int ret;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++) {
		for (j = 0; j < codec_info_list[i].dai_num; j++) {
			/* Check each dai in codec_info_lis to see if it is used in the link */
			if (!codec_info_list[i].dais[j].exit)
				continue;
			/*
			 * We don't need to call .exit function if there is no matched
			 * dai link found.
			 */
			dai_link = mc_find_codec_dai_used(card,
							  codec_info_list[i].dais[j].dai_name);
			if (dai_link) {
				/* Do the .exit function if the codec dai is used in the link */
				ret = codec_info_list[i].dais[j].exit(card, dai_link);
				if (ret)
					dev_warn(card->dev,
						 "codec exit failed %d\n",
						 ret);
				break;
			}
		}
	}
}

static int mc_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &card_sof_sdw;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(&pdev->dev);
	struct mc_private *ctx;
	int amp_num = 0, i;
	int ret;

	card->dev = &pdev->dev;

	dev_dbg(card->dev, "Entry\n");

	ctx = devm_kzalloc(card->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	snd_soc_card_set_drvdata(card, ctx);

	dmi_check_system(sof_sdw_quirk_table);

	if (quirk_override != -1) {
		dev_info(card->dev, "Overriding quirk 0x%lx => 0x%x\n",
			 sof_sdw_quirk, quirk_override);
		sof_sdw_quirk = quirk_override;
	}

	log_quirks(card->dev);

	/* reset amp_num to ensure amp_num++ starts from 0 in each probe */
	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		codec_info_list[i].amp_num = 0;

	if (mach->mach_params.subsystem_id_set) {
		snd_soc_card_set_pci_ssid(card,
					  mach->mach_params.subsystem_vendor,
					  mach->mach_params.subsystem_device);
	}

	ret = sof_card_dai_links_create(card);
	if (ret < 0)
		return ret;

	/*
	 * the default amp_num is zero for each codec and
	 * amp_num will only be increased for active amp
	 * codecs on used platform
	 */
	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		amp_num += codec_info_list[i].amp_num;

	card->components = devm_kasprintf(card->dev, GFP_KERNEL,
					  "cfg-spk:%d cfg-amp:%d",
					  (sof_sdw_quirk & SOF_SDW_FOUR_SPK)
					  ? 4 : 2, amp_num);
	if (!card->components)
		return -ENOMEM;

	if (mach->mach_params.dmic_num) {
		card->components = devm_kasprintf(card->dev, GFP_KERNEL,
						  "%s mic:dmic cfg-mics:%d",
						  card->components,
						  mach->mach_params.dmic_num);
		if (!card->components)
			return -ENOMEM;
	}

	card->long_name = sdw_card_long_name;

	/* Register the card */
	ret = devm_snd_soc_register_card(card->dev, card);
	if (ret) {
		dev_err(card->dev, "snd_soc_register_card failed %d\n", ret);
		mc_dailink_exit_loop(card);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static void mc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	mc_dailink_exit_loop(card);
}

static const struct platform_device_id mc_id_table[] = {
	{ "sof_sdw", },
	{}
};
MODULE_DEVICE_TABLE(platform, mc_id_table);

static struct platform_driver sof_sdw_driver = {
	.driver = {
		.name = "sof_sdw",
		.pm = &snd_soc_pm_ops,
	},
	.probe = mc_probe,
	.remove_new = mc_remove,
	.id_table = mc_id_table,
};

module_platform_driver(sof_sdw_driver);

MODULE_DESCRIPTION("ASoC SoundWire Generic Machine driver");
MODULE_AUTHOR("Bard Liao <yung-chuan.liao@linux.intel.com>");
MODULE_AUTHOR("Rander Wang <rander.wang@linux.intel.com>");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_MAXIM_COMMON);
