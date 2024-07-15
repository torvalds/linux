// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw - ASOC Machine driver for Intel SoundWire platforms
 */

#include <linux/bitmap.h>
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
		dev_err(dev, "quirk SOF_SDW_FOUR_SPK enabled but no longer supported\n");
	if (sof_sdw_quirk & SOF_SDW_TGL_HDMI)
		dev_dbg(dev, "quirk SOF_SDW_TGL_HDMI enabled\n");
	if (sof_sdw_quirk & SOF_SDW_PCH_DMIC)
		dev_dbg(dev, "quirk SOF_SDW_PCH_DMIC enabled\n");
	if (SOF_SSP_GET_PORT(sof_sdw_quirk))
		dev_dbg(dev, "SSP port %ld\n",
			SOF_SSP_GET_PORT(sof_sdw_quirk));
	if (sof_sdw_quirk & SOF_SDW_NO_AGGREGATION)
		dev_err(dev, "quirk SOF_SDW_NO_AGGREGATION enabled but no longer supported\n");
	if (sof_sdw_quirk & SOF_CODEC_SPKR)
		dev_dbg(dev, "quirk SOF_CODEC_SPKR enabled\n");
	if (sof_sdw_quirk & SOF_SIDECAR_AMPS)
		dev_dbg(dev, "quirk SOF_SIDECAR_AMPS enabled\n");
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
		.driver_data = (void *)(RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0990"),
		},
		.driver_data = (void *)(RT711_JD2),
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
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0A5E")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Volteer"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
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
					SOF_SDW_PCH_DMIC),
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
	{
		/* NUC15 LAPRC710 skews */
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "LAPRC710"),
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
					RT711_JD2),
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
			DMI_MATCH(DMI_BOARD_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_SKU, "0000000000070000"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2_100K),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Brya"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					SOF_SDW_PCH_DMIC |
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
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AF3"),
		},
		/* No Jack */
		.driver_data = (void *)(SOF_SDW_TGL_HDMI),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AFE")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0AFF")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B00")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B01")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B11")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B12")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
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
					RT711_JD2),
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
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0B8C"),
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
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
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C0F")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C10"),
		},
		/* No Jack */
		.driver_data = (void *)(SOF_SDW_TGL_HDMI),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C11")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C40")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0C4F")
		},
		.driver_data = (void *)(SOF_SDW_TGL_HDMI |
					RT711_JD2),
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
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "OMEN Transcend Gaming Laptop"),
		},
		.driver_data = (void *)(RT711_JD2),
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
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CE3")
		},
		.driver_data = (void *)(SOF_SIDECAR_AMPS),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CE4")
		},
		.driver_data = (void *)(SOF_SIDECAR_AMPS),
	},
	{}
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

static const struct snd_soc_dapm_widget generic_dmic_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC", NULL),
};

static const struct snd_soc_dapm_widget generic_jack_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_kcontrol_new generic_jack_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget generic_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_kcontrol_new generic_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static const struct snd_soc_dapm_widget maxim_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_kcontrol_new maxim_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget rt700_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("AMIC", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_kcontrol_new rt700_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphones"),
	SOC_DAPM_PIN_SWITCH("AMIC"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
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
				.rtd_init = rt700_rtd_init,
				.controls = rt700_controls,
				.num_controls = ARRAY_SIZE(rt700_controls),
				.widgets = rt700_widgets,
				.num_widgets = ARRAY_SIZE(rt700_widgets),
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
				.rtd_init = rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
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
				.rtd_init = rt711_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
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
				.rtd_init = rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {true, false},
				.dai_name = "rt712-sdca-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.init = sof_sdw_rt_amp_init,
				.exit = sof_sdw_rt_amp_exit,
				.rtd_init = rt712_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
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
				.rtd_init = rt_dmic_rtd_init,
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
				.rtd_init = rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
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
				.rtd_init = rt_dmic_rtd_init,
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
				.rtd_init = rt_amp_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
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
				.rtd_init = rt_amp_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
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
				.rtd_init = rt_amp_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
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
				.dai_name = "rt715-sdca-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.rtd_init = rt_dmic_rtd_init,
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
				.dai_name = "rt715-sdca-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.rtd_init = rt_dmic_rtd_init,
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
				.rtd_init = rt_dmic_rtd_init,
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
				.rtd_init = rt_dmic_rtd_init,
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x722,
		.version_id = 3,
		.dais = {
			{
				.direction = {true, true},
				.dai_name = "rt722-sdca-aif1",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_JACK_IN_DAI_ID},
				.init = sof_sdw_rt_sdca_jack_init,
				.exit = sof_sdw_rt_sdca_jack_exit,
				.rtd_init = rt_sdca_jack_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {true, false},
				.dai_name = "rt722-sdca-aif2",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				/* No feedback capability is provided by rt722-sdca codec driver*/
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.init = sof_sdw_rt_amp_init,
				.exit = sof_sdw_rt_amp_exit,
				.rtd_init = rt722_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "rt722-sdca-aif3",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.rtd_init = rt_dmic_rtd_init,
			},
		},
		.dai_num = 3,
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
				.rtd_init = maxim_spk_rtd_init,
				.controls = maxim_controls,
				.num_controls = ARRAY_SIZE(maxim_controls),
				.widgets = maxim_widgets,
				.num_widgets = ARRAY_SIZE(maxim_widgets),
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
				.rtd_init = maxim_spk_rtd_init,
				.controls = maxim_controls,
				.num_controls = ARRAY_SIZE(maxim_controls),
				.widgets = maxim_widgets,
				.num_widgets = ARRAY_SIZE(maxim_widgets),
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
				.rtd_init = rt5682_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
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
				.rtd_init = cs_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
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
				.rtd_init = cs42l42_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
		},
		.dai_num = 1,
	},
	{
		.part_id = 0x4243,
		.codec_name = "cs42l43-codec",
		.count_sidecar = bridge_cs35l56_count_sidecar,
		.add_sidecar = bridge_cs35l56_add_sidecar,
		.dais = {
			{
				.direction = {true, false},
				.dai_name = "cs42l43-dp5",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_JACK_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.rtd_init = cs42l43_hs_rtd_init,
				.controls = generic_jack_controls,
				.num_controls = ARRAY_SIZE(generic_jack_controls),
				.widgets = generic_jack_widgets,
				.num_widgets = ARRAY_SIZE(generic_jack_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "cs42l43-dp1",
				.dai_type = SOF_SDW_DAI_TYPE_MIC,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_DMIC_DAI_ID},
				.rtd_init = cs42l43_dmic_rtd_init,
				.widgets = generic_dmic_widgets,
				.num_widgets = ARRAY_SIZE(generic_dmic_widgets),
			},
			{
				.direction = {false, true},
				.dai_name = "cs42l43-dp2",
				.dai_type = SOF_SDW_DAI_TYPE_JACK,
				.dailink = {SDW_UNUSED_DAI_ID, SDW_JACK_IN_DAI_ID},
			},
			{
				.direction = {true, false},
				.dai_name = "cs42l43-dp6",
				.dai_type = SOF_SDW_DAI_TYPE_AMP,
				.dailink = {SDW_AMP_OUT_DAI_ID, SDW_UNUSED_DAI_ID},
				.init = sof_sdw_cs42l43_spk_init,
				.rtd_init = cs42l43_spk_rtd_init,
				.controls = generic_spk_controls,
				.num_controls = ARRAY_SIZE(generic_spk_controls),
				.widgets = generic_spk_widgets,
				.num_widgets = ARRAY_SIZE(generic_spk_widgets),
				.quirk = SOF_CODEC_SPKR | SOF_SIDECAR_AMPS,
			},
		},
		.dai_num = 4,
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
			},
		},
		.dai_num = 1,
	},
};

static struct sof_sdw_codec_info *find_codec_info_part(const u64 adr)
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
			return &codec_info_list[i];

	return NULL;

}

static struct sof_sdw_codec_info *find_codec_info_acpi(const u8 *acpi_id)
{
	int i;

	if (!acpi_id[0])
		return NULL;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++)
		if (!memcmp(codec_info_list[i].acpi_id, acpi_id, ACPI_ID_LEN))
			return &codec_info_list[i];

	return NULL;
}

static struct sof_sdw_codec_info *find_codec_info_dai(const char *dai_name,
						      int *dai_index)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(codec_info_list); i++) {
		for (j = 0; j < codec_info_list[i].dai_num; j++) {
			if (!strcmp(codec_info_list[i].dais[j].dai_name, dai_name)) {
				*dai_index = j;
				return &codec_info_list[i];
			}
		}
	}

	return NULL;
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

static const char *get_codec_name(struct device *dev,
				  const struct sof_sdw_codec_info *codec_info,
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
	else if (is_unique_device(adr_link, sdw_version, mfg_id, part_id,
				  class_id, adr_index))
		return devm_kasprintf(dev, GFP_KERNEL, "sdw:0:%01x:%04x:%04x:%02x",
				      link_id, mfg_id, part_id, class_id);
	else
		return devm_kasprintf(dev, GFP_KERNEL, "sdw:0:%01x:%04x:%04x:%02x:%01x",
				      link_id, mfg_id, part_id, class_id, unique_id);

	return NULL;
}

static int sof_sdw_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct sof_sdw_codec_info *codec_info;
	struct snd_soc_dai *dai;
	int dai_index;
	int ret;
	int i;

	for_each_rtd_codec_dais(rtd, i, dai) {
		codec_info = find_codec_info_dai(dai->name, &dai_index);
		if (!codec_info)
			return -EINVAL;

		/*
		 * A codec dai can be connected to different dai links for capture and playback,
		 * but we only need to call the rtd_init function once.
		 * The rtd_init for each codec dai is independent. So, the order of rtd_init
		 * doesn't matter.
		 */
		if (codec_info->dais[dai_index].rtd_init_done)
			continue;

		/*
		 * Add card controls and dapm widgets for the first codec dai.
		 * The controls and widgets will be used for all codec dais.
		 */

		if (i > 0)
			goto skip_add_controls_widgets;

		if (codec_info->dais[dai_index].controls) {
			ret = snd_soc_add_card_controls(card, codec_info->dais[dai_index].controls,
							codec_info->dais[dai_index].num_controls);
			if (ret) {
				dev_err(card->dev, "%#x controls addition failed: %d\n",
					codec_info->part_id, ret);
				return ret;
			}
		}
		if (codec_info->dais[dai_index].widgets) {
			ret = snd_soc_dapm_new_controls(&card->dapm,
							codec_info->dais[dai_index].widgets,
							codec_info->dais[dai_index].num_widgets);
			if (ret) {
				dev_err(card->dev, "%#x widgets addition failed: %d\n",
					codec_info->part_id, ret);
				return ret;
			}
		}

skip_add_controls_widgets:
		if (codec_info->dais[dai_index].rtd_init) {
			ret = codec_info->dais[dai_index].rtd_init(rtd, dai);
			if (ret)
				return ret;
		}
		codec_info->dais[dai_index].rtd_init_done = true;
	}

	return 0;
}

struct sof_sdw_endpoint {
	struct list_head list;

	u32 link_mask;
	const char *codec_name;
	const char *name_prefix;
	bool include_sidecar;

	struct sof_sdw_codec_info *codec_info;
	const struct sof_sdw_dai_info *dai_info;
};

struct sof_sdw_dailink {
	bool initialised;

	u8 group_id;
	u32 link_mask[SNDRV_PCM_STREAM_LAST + 1];
	int num_devs[SNDRV_PCM_STREAM_LAST + 1];
	struct list_head endpoints;
};

static const char * const type_strings[] = {"SimpleJack", "SmartAmp", "SmartMic"};

static int count_sdw_endpoints(struct snd_soc_card *card, int *num_devs, int *num_ends)
{
	struct device *dev = card->dev;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(dev);
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	const struct snd_soc_acpi_link_adr *adr_link;
	int i;

	for (adr_link = mach_params->links; adr_link->num_adr; adr_link++) {
		*num_devs += adr_link->num_adr;

		for (i = 0; i < adr_link->num_adr; i++)
			*num_ends += adr_link->adr_d[i].num_endpoints;
	}

	dev_dbg(dev, "Found %d devices with %d endpoints\n", *num_devs, *num_ends);

	return 0;
}

static struct sof_sdw_dailink *find_dailink(struct sof_sdw_dailink *dailinks,
					    const struct snd_soc_acpi_endpoint *new)
{
	while (dailinks->initialised) {
		if (new->aggregated && dailinks->group_id == new->group_id)
			return dailinks;

		dailinks++;
	}

	INIT_LIST_HEAD(&dailinks->endpoints);
	dailinks->group_id = new->group_id;
	dailinks->initialised = true;

	return dailinks;
}

static int parse_sdw_endpoints(struct snd_soc_card *card,
			       struct sof_sdw_dailink *sof_dais,
			       struct sof_sdw_endpoint *sof_ends,
			       int *num_devs)
{
	struct device *dev = card->dev;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_acpi_mach *mach = dev_get_platdata(dev);
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	const struct snd_soc_acpi_link_adr *adr_link;
	struct sof_sdw_endpoint *sof_end = sof_ends;
	int num_dais = 0;
	int i, j;
	int ret;

	for (adr_link = mach_params->links; adr_link->num_adr; adr_link++) {
		int num_link_dailinks = 0;

		if (!is_power_of_2(adr_link->mask)) {
			dev_err(dev, "link with multiple mask bits: 0x%x\n",
				adr_link->mask);
			return -EINVAL;
		}

		for (i = 0; i < adr_link->num_adr; i++) {
			const struct snd_soc_acpi_adr_device *adr_dev = &adr_link->adr_d[i];
			struct sof_sdw_codec_info *codec_info;
			const char *codec_name;

			if (!adr_dev->name_prefix) {
				dev_err(dev, "codec 0x%llx does not have a name prefix\n",
					adr_dev->adr);
				return -EINVAL;
			}

			codec_info = find_codec_info_part(adr_dev->adr);
			if (!codec_info)
				return -EINVAL;

			ctx->ignore_pch_dmic |= codec_info->ignore_pch_dmic;

			codec_name = get_codec_name(dev, codec_info, adr_link, i);
			if (!codec_name)
				return -ENOMEM;

			dev_dbg(dev, "Adding prefix %s for %s\n",
				adr_dev->name_prefix, codec_name);

			sof_end->name_prefix = adr_dev->name_prefix;

			if (codec_info->count_sidecar && codec_info->add_sidecar) {
				ret = codec_info->count_sidecar(card, &num_dais, num_devs);
				if (ret)
					return ret;

				sof_end->include_sidecar = true;
			}

			for (j = 0; j < adr_dev->num_endpoints; j++) {
				const struct snd_soc_acpi_endpoint *adr_end;
				const struct sof_sdw_dai_info *dai_info;
				struct sof_sdw_dailink *sof_dai;
				int stream;

				adr_end = &adr_dev->endpoints[j];
				dai_info = &codec_info->dais[adr_end->num];
				sof_dai = find_dailink(sof_dais, adr_end);

				if (dai_info->quirk && !(dai_info->quirk & sof_sdw_quirk))
					continue;

				dev_dbg(dev,
					"Add dev: %d, 0x%llx end: %d, %s, %c/%c to %s: %d\n",
					ffs(adr_link->mask) - 1, adr_dev->adr,
					adr_end->num, type_strings[dai_info->dai_type],
					dai_info->direction[SNDRV_PCM_STREAM_PLAYBACK] ? 'P' : '-',
					dai_info->direction[SNDRV_PCM_STREAM_CAPTURE] ? 'C' : '-',
					adr_end->aggregated ? "group" : "solo",
					adr_end->group_id);

				if (adr_end->num >= codec_info->dai_num) {
					dev_err(dev,
						"%d is too many endpoints for codec: 0x%x\n",
						adr_end->num, codec_info->part_id);
					return -EINVAL;
				}

				for_each_pcm_streams(stream) {
					if (dai_info->direction[stream] &&
					    dai_info->dailink[stream] < 0) {
						dev_err(dev,
							"Invalid dailink id %d for codec: 0x%x\n",
							dai_info->dailink[stream],
							codec_info->part_id);
						return -EINVAL;
					}

					if (dai_info->direction[stream]) {
						num_dais += !sof_dai->num_devs[stream];
						sof_dai->num_devs[stream]++;
						sof_dai->link_mask[stream] |= adr_link->mask;
					}
				}

				num_link_dailinks += !!list_empty(&sof_dai->endpoints);
				list_add_tail(&sof_end->list, &sof_dai->endpoints);

				sof_end->link_mask = adr_link->mask;
				sof_end->codec_name = codec_name;
				sof_end->codec_info = codec_info;
				sof_end->dai_info = dai_info;
				sof_end++;
			}
		}

		ctx->append_dai_type |= (num_link_dailinks > 1);
	}

	return num_dais;
}

static int create_sdw_dailink(struct snd_soc_card *card,
			      struct sof_sdw_dailink *sof_dai,
			      struct snd_soc_dai_link **dai_links,
			      int *be_id, struct snd_soc_codec_conf **codec_conf)
{
	struct device *dev = card->dev;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct sof_sdw_endpoint *sof_end;
	int stream;
	int ret;

	list_for_each_entry(sof_end, &sof_dai->endpoints, list) {
		if (sof_end->name_prefix) {
			(*codec_conf)->dlc.name = sof_end->codec_name;
			(*codec_conf)->name_prefix = sof_end->name_prefix;
			(*codec_conf)++;
		}

		if (sof_end->include_sidecar) {
			ret = sof_end->codec_info->add_sidecar(card, dai_links, codec_conf);
			if (ret)
				return ret;
		}
	}

	for_each_pcm_streams(stream) {
		static const char * const sdw_stream_name[] = {
			"SDW%d-Playback",
			"SDW%d-Capture",
			"SDW%d-Playback-%s",
			"SDW%d-Capture-%s",
		};
		struct snd_soc_dai_link_ch_map *codec_maps;
		struct snd_soc_dai_link_component *codecs;
		struct snd_soc_dai_link_component *cpus;
		int num_cpus = hweight32(sof_dai->link_mask[stream]);
		int num_codecs = sof_dai->num_devs[stream];
		int playback, capture;
		int cur_link = 0;
		int i = 0, j = 0;
		char *name;

		if (!sof_dai->num_devs[stream])
			continue;

		sof_end = list_first_entry(&sof_dai->endpoints,
					   struct sof_sdw_endpoint, list);

		*be_id = sof_end->dai_info->dailink[stream];
		if (*be_id < 0) {
			dev_err(dev, "Invalid dailink id %d\n", *be_id);
			return -EINVAL;
		}

		/* create stream name according to first link id */
		if (ctx->append_dai_type)
			name = devm_kasprintf(dev, GFP_KERNEL,
					      sdw_stream_name[stream + 2],
					      ffs(sof_end->link_mask) - 1,
					      type_strings[sof_end->dai_info->dai_type]);
		else
			name = devm_kasprintf(dev, GFP_KERNEL,
					      sdw_stream_name[stream],
					      ffs(sof_end->link_mask) - 1);
		if (!name)
			return -ENOMEM;

		cpus = devm_kcalloc(dev, num_cpus, sizeof(*cpus), GFP_KERNEL);
		if (!cpus)
			return -ENOMEM;

		codecs = devm_kcalloc(dev, num_codecs, sizeof(*codecs), GFP_KERNEL);
		if (!codecs)
			return -ENOMEM;

		codec_maps = devm_kcalloc(dev, num_codecs, sizeof(*codec_maps), GFP_KERNEL);
		if (!codec_maps)
			return -ENOMEM;

		list_for_each_entry(sof_end, &sof_dai->endpoints, list) {
			if (!sof_end->dai_info->direction[stream])
				continue;

			if (cur_link != sof_end->link_mask) {
				int link_num = ffs(sof_end->link_mask) - 1;
				int pin_num = ctx->sdw_pin_index[link_num]++;

				cur_link = sof_end->link_mask;

				cpus[i].dai_name = devm_kasprintf(dev, GFP_KERNEL,
								  "SDW%d Pin%d",
								  link_num, pin_num);
				if (!cpus[i].dai_name)
					return -ENOMEM;
				i++;
			}

			codec_maps[j].cpu = i - 1;
			codec_maps[j].codec = j;

			codecs[j].name = sof_end->codec_name;
			codecs[j].dai_name = sof_end->dai_info->dai_name;
			j++;
		}

		WARN_ON(i != num_cpus || j != num_codecs);

		playback = (stream == SNDRV_PCM_STREAM_PLAYBACK);
		capture = (stream == SNDRV_PCM_STREAM_CAPTURE);

		init_dai_link(dev, *dai_links, be_id, name, playback, capture,
			      cpus, num_cpus, codecs, num_codecs,
			      sof_sdw_rtd_init, &sdw_ops);

		/*
		 * SoundWire DAILINKs use 'stream' functions and Bank Switch operations
		 * based on wait_for_completion(), tag them as 'nonatomic'.
		 */
		(*dai_links)->nonatomic = true;
		(*dai_links)->ch_maps = codec_maps;

		list_for_each_entry(sof_end, &sof_dai->endpoints, list) {
			if (sof_end->dai_info->init)
				sof_end->dai_info->init(card, *dai_links,
							sof_end->codec_info,
							playback);
		}

		(*dai_links)++;
	}

	return 0;
}

static int create_sdw_dailinks(struct snd_soc_card *card,
			       struct snd_soc_dai_link **dai_links, int *be_id,
			       struct sof_sdw_dailink *sof_dais,
			       struct snd_soc_codec_conf **codec_conf)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	int ret, i;

	for (i = 0; i < SDW_MAX_LINKS; i++)
		ctx->sdw_pin_index[i] = SDW_INTEL_BIDIR_PDI_BASE;

	/* generate DAI links by each sdw link */
	while (sof_dais->initialised) {
		int current_be_id;

		ret = create_sdw_dailink(card, sof_dais, dai_links,
					 &current_be_id, codec_conf);
		if (ret)
			return ret;

		/* Update the be_id to match the highest ID used for SDW link */
		if (*be_id < current_be_id)
			*be_id = current_be_id;

		sof_dais++;
	}

	return 0;
}

static int create_ssp_dailinks(struct snd_soc_card *card,
			       struct snd_soc_dai_link **dai_links, int *be_id,
			       struct sof_sdw_codec_info *ssp_info,
			       unsigned long ssp_mask)
{
	struct device *dev = card->dev;
	int i, j = 0;
	int ret;

	for_each_set_bit(i, &ssp_mask, BITS_PER_TYPE(ssp_mask)) {
		char *name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-Codec", i);
		char *cpu_dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", i);
		char *codec_name = devm_kasprintf(dev, GFP_KERNEL, "i2c-%s:0%d",
						  ssp_info->acpi_id, j++);
		int playback = ssp_info->dais[0].direction[SNDRV_PCM_STREAM_PLAYBACK];
		int capture = ssp_info->dais[0].direction[SNDRV_PCM_STREAM_CAPTURE];

		ret = init_simple_dai_link(dev, *dai_links, be_id, name,
					   playback, capture, cpu_dai_name,
					   codec_name, ssp_info->dais[0].dai_name,
					   NULL, ssp_info->ops);
		if (ret)
			return ret;

		ret = ssp_info->dais[0].init(card, *dai_links, ssp_info, 0);
		if (ret < 0)
			return ret;

		(*dai_links)++;
	}

	return 0;
}

static int create_dmic_dailinks(struct snd_soc_card *card,
				struct snd_soc_dai_link **dai_links, int *be_id)
{
	struct device *dev = card->dev;
	int ret;

	ret = init_simple_dai_link(dev, *dai_links, be_id, "dmic01",
				   0, 1, // DMIC only supports capture
				   "DMIC01 Pin", "dmic-codec", "dmic-hifi",
				   sof_sdw_dmic_init, NULL);
	if (ret)
		return ret;

	(*dai_links)++;

	ret = init_simple_dai_link(dev, *dai_links, be_id, "dmic16k",
				   0, 1, // DMIC only supports capture
				   "DMIC16k Pin", "dmic-codec", "dmic-hifi",
				   /* don't call sof_sdw_dmic_init() twice */
				   NULL, NULL);
	if (ret)
		return ret;

	(*dai_links)++;

	return 0;
}

static int create_hdmi_dailinks(struct snd_soc_card *card,
				struct snd_soc_dai_link **dai_links, int *be_id,
				int hdmi_num)
{
	struct device *dev = card->dev;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	int i, ret;

	for (i = 0; i < hdmi_num; i++) {
		char *name = devm_kasprintf(dev, GFP_KERNEL, "iDisp%d", i + 1);
		char *cpu_dai_name = devm_kasprintf(dev, GFP_KERNEL, "iDisp%d Pin", i + 1);
		char *codec_name, *codec_dai_name;

		if (ctx->hdmi.idisp_codec) {
			codec_name = "ehdaudio0D2";
			codec_dai_name = devm_kasprintf(dev, GFP_KERNEL,
							"intel-hdmi-hifi%d", i + 1);
		} else {
			codec_name = "snd-soc-dummy";
			codec_dai_name = "snd-soc-dummy-dai";
		}

		ret = init_simple_dai_link(dev, *dai_links, be_id, name,
					   1, 0, // HDMI only supports playback
					   cpu_dai_name, codec_name, codec_dai_name,
					   i == 0 ? sof_sdw_hdmi_init : NULL, NULL);
		if (ret)
			return ret;

		(*dai_links)++;
	}

	return 0;
}

static int create_bt_dailinks(struct snd_soc_card *card,
			      struct snd_soc_dai_link **dai_links, int *be_id)
{
	struct device *dev = card->dev;
	int port = (sof_sdw_quirk & SOF_BT_OFFLOAD_SSP_MASK) >>
			SOF_BT_OFFLOAD_SSP_SHIFT;
	char *name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d-BT", port);
	char *cpu_dai_name = devm_kasprintf(dev, GFP_KERNEL, "SSP%d Pin", port);
	int ret;

	ret = init_simple_dai_link(dev, *dai_links, be_id, name,
				   1, 1, cpu_dai_name, snd_soc_dummy_dlc.name,
				   snd_soc_dummy_dlc.dai_name, NULL, NULL);
	if (ret)
		return ret;

	(*dai_links)++;

	return 0;
}

static int sof_card_dai_links_create(struct snd_soc_card *card)
{
	struct device *dev = card->dev;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(card->dev);
	int sdw_be_num = 0, ssp_num = 0, dmic_num = 0, bt_num = 0;
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	struct snd_soc_codec_conf *codec_conf;
	struct sof_sdw_codec_info *ssp_info;
	struct sof_sdw_endpoint *sof_ends;
	struct sof_sdw_dailink *sof_dais;
	int num_devs = 0;
	int num_ends = 0;
	struct snd_soc_dai_link *dai_links;
	int num_links;
	int be_id = 0;
	int hdmi_num;
	unsigned long ssp_mask;
	int ret;

	ret = count_sdw_endpoints(card, &num_devs, &num_ends);
	if (ret < 0) {
		dev_err(dev, "failed to count devices/endpoints: %d\n", ret);
		return ret;
	}

	/* One per DAI link, worst case is a DAI link for every endpoint */
	sof_dais = kcalloc(num_ends, sizeof(*sof_dais), GFP_KERNEL);
	if (!sof_dais)
		return -ENOMEM;

	/* One per endpoint, ie. each DAI on each codec/amp */
	sof_ends = kcalloc(num_ends, sizeof(*sof_ends), GFP_KERNEL);
	if (!sof_ends) {
		ret = -ENOMEM;
		goto err_dai;
	}

	ret = parse_sdw_endpoints(card, sof_dais, sof_ends, &num_devs);
	if (ret < 0)
		goto err_end;

	sdw_be_num = ret;

	/*
	 * on generic tgl platform, I2S or sdw mode is supported
	 * based on board rework. A ACPI device is registered in
	 * system only when I2S mode is supported, not sdw mode.
	 * Here check ACPI ID to confirm I2S is supported.
	 */
	ssp_info = find_codec_info_acpi(mach->id);
	if (ssp_info) {
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

	codec_conf = devm_kcalloc(dev, num_devs, sizeof(*codec_conf), GFP_KERNEL);
	if (!codec_conf) {
		ret = -ENOMEM;
		goto err_end;
	}

	/* allocate BE dailinks */
	num_links = sdw_be_num + ssp_num + dmic_num + hdmi_num + bt_num;
	dai_links = devm_kcalloc(dev, num_links, sizeof(*dai_links), GFP_KERNEL);
	if (!dai_links) {
		ret = -ENOMEM;
		goto err_end;
	}

	card->codec_conf = codec_conf;
	card->num_configs = num_devs;
	card->dai_link = dai_links;
	card->num_links = num_links;

	/* SDW */
	if (sdw_be_num) {
		ret = create_sdw_dailinks(card, &dai_links, &be_id,
					  sof_dais, &codec_conf);
		if (ret)
			goto err_end;
	}

	/* SSP */
	if (ssp_num) {
		ret = create_ssp_dailinks(card, &dai_links, &be_id,
					  ssp_info, ssp_mask);
		if (ret)
			goto err_end;
	}

	/* dmic */
	if (dmic_num > 0) {
		if (ctx->ignore_pch_dmic) {
			dev_warn(dev, "Ignoring PCH DMIC\n");
		} else {
			ret = create_dmic_dailinks(card, &dai_links, &be_id);
			if (ret)
				goto err_end;
		}
	}

	/* HDMI */
	ret = create_hdmi_dailinks(card, &dai_links, &be_id, hdmi_num);
	if (ret)
		goto err_end;

	/* BT */
	if (sof_sdw_quirk & SOF_SSP_BT_OFFLOAD_PRESENT) {
		ret = create_bt_dailinks(card, &dai_links, &be_id);
		if (ret)
			goto err_end;
	}

	WARN_ON(codec_conf != card->codec_conf + card->num_configs);
	WARN_ON(dai_links != card->dai_link + card->num_links);

err_end:
	kfree(sof_ends);
err_dai:
	kfree(sof_dais);

	return ret;
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
			codec_info_list[i].dais[j].rtd_init_done = false;
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
	struct snd_soc_acpi_mach *mach = dev_get_platdata(&pdev->dev);
	struct snd_soc_card *card;
	struct mc_private *ctx;
	int amp_num = 0, i;
	int ret;

	dev_dbg(&pdev->dev, "Entry\n");

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	card = &ctx->card;
	card->dev = &pdev->dev;
	card->name = "soundwire";
	card->owner = THIS_MODULE;
	card->late_probe = sof_sdw_card_late_probe;

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
					  " cfg-amp:%d", amp_num);
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

	/* Register the card */
	ret = devm_snd_soc_register_card(card->dev, card);
	if (ret) {
		dev_err_probe(card->dev, ret, "snd_soc_register_card failed %d\n", ret);
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
