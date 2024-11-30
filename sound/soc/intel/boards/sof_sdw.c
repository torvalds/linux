// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Intel Corporation

/*
 *  sof_sdw - ASOC Machine driver for Intel SoundWire platforms
 */

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/soc-acpi.h>
#include "sof_sdw_common.h"
#include "../../codecs/rt711.h"

static unsigned long sof_sdw_quirk = RT711_JD1;
static int quirk_override = -1;
module_param_named(quirk, quirk_override, int, 0444);
MODULE_PARM_DESC(quirk, "Board-specific quirk override");

static void log_quirks(struct device *dev)
{
	if (SOC_SDW_JACK_JDSRC(sof_sdw_quirk))
		dev_dbg(dev, "quirk realtek,jack-detect-source %ld\n",
			SOC_SDW_JACK_JDSRC(sof_sdw_quirk));
	if (sof_sdw_quirk & SOC_SDW_FOUR_SPK)
		dev_err(dev, "quirk SOC_SDW_FOUR_SPK enabled but no longer supported\n");
	if (sof_sdw_quirk & SOF_SDW_TGL_HDMI)
		dev_dbg(dev, "quirk SOF_SDW_TGL_HDMI enabled\n");
	if (sof_sdw_quirk & SOC_SDW_PCH_DMIC)
		dev_dbg(dev, "quirk SOC_SDW_PCH_DMIC enabled\n");
	if (SOF_SSP_GET_PORT(sof_sdw_quirk))
		dev_dbg(dev, "SSP port %ld\n",
			SOF_SSP_GET_PORT(sof_sdw_quirk));
	if (sof_sdw_quirk & SOC_SDW_NO_AGGREGATION)
		dev_err(dev, "quirk SOC_SDW_NO_AGGREGATION enabled but no longer supported\n");
	if (sof_sdw_quirk & SOC_SDW_CODEC_SPKR)
		dev_dbg(dev, "quirk SOC_SDW_CODEC_SPKR enabled\n");
	if (sof_sdw_quirk & SOC_SDW_SIDECAR_AMPS)
		dev_dbg(dev, "quirk SOC_SDW_SIDECAR_AMPS enabled\n");
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
		.driver_data = (void *)SOC_SDW_PCH_DMIC,
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
		.driver_data = (void *)SOC_SDW_PCH_DMIC,
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC),
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC |
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
					SOC_SDW_PCH_DMIC |
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
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF6")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF9")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CFA")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
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
		.driver_data = (void *)(SOC_SDW_PCH_DMIC |
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
		.driver_data = (void *)(SOC_SDW_SIDECAR_AMPS),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CE4")
		},
		.driver_data = (void *)(SOC_SDW_SIDECAR_AMPS),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CDB")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CDC")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CDD")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0D36")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF8")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "3838")
		},
		.driver_data = (void *)(SOC_SDW_SIDECAR_AMPS),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "3832")
		},
		.driver_data = (void *)(SOC_SDW_SIDECAR_AMPS),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "380E")
		},
		.driver_data = (void *)(SOC_SDW_SIDECAR_AMPS),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "233C")
		},
		/* Note this quirk excludes the CODEC mic */
		.driver_data = (void *)(SOC_SDW_CODEC_MIC),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "233B")
		},
		.driver_data = (void *)(SOC_SDW_SIDECAR_AMPS),
	},

	/* ArrowLake devices */
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CE8")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF1")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF7")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF0")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF3")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF4")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc"),
			DMI_EXACT_MATCH(DMI_PRODUCT_SKU, "0CF5")
		},
		.driver_data = (void *)(SOC_SDW_CODEC_SPKR),
	},
	/* Pantherlake devices*/
	{
		.callback = sof_sdw_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Intel_ptlrvp"),
		},
		.driver_data = (void *)(SOC_SDW_PCH_DMIC),
	},
	{}
};

static struct snd_soc_dai_link_component platform_component[] = {
	{
		/* name might be overridden during probe */
		.name = "0000:00:1f.3"
	}
};

static const struct snd_soc_ops sdw_ops = {
	.startup = asoc_sdw_startup,
	.prepare = asoc_sdw_prepare,
	.trigger = asoc_sdw_trigger,
	.hw_params = asoc_sdw_hw_params,
	.hw_free = asoc_sdw_hw_free,
	.shutdown = asoc_sdw_shutdown,
};

static const char * const type_strings[] = {"SimpleJack", "SmartAmp", "SmartMic"};

static int create_sdw_dailink(struct snd_soc_card *card,
			      struct asoc_sdw_dailink *sof_dai,
			      struct snd_soc_dai_link **dai_links,
			      int *be_id, struct snd_soc_codec_conf **codec_conf)
{
	struct device *dev = card->dev;
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct intel_mc_ctx *intel_ctx = (struct intel_mc_ctx *)ctx->private;
	struct asoc_sdw_endpoint *sof_end;
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
					   struct asoc_sdw_endpoint, list);

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
				int pin_num = intel_ctx->sdw_pin_index[link_num]++;

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

		asoc_sdw_init_dai_link(dev, *dai_links, be_id, name, playback, capture,
				       cpus, num_cpus, platform_component,
				       ARRAY_SIZE(platform_component), codecs, num_codecs,
				       1, asoc_sdw_rtd_init, &sdw_ops);

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
			       struct asoc_sdw_dailink *sof_dais,
			       struct snd_soc_codec_conf **codec_conf)
{
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct intel_mc_ctx *intel_ctx = (struct intel_mc_ctx *)ctx->private;
	int ret, i;

	for (i = 0; i < SDW_INTEL_MAX_LINKS; i++)
		intel_ctx->sdw_pin_index[i] = SOC_SDW_INTEL_BIDIR_PDI_BASE;

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
			       struct asoc_sdw_codec_info *ssp_info,
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
		if (!name || !cpu_dai_name || !codec_name)
			return -ENOMEM;

		int playback = ssp_info->dais[0].direction[SNDRV_PCM_STREAM_PLAYBACK];
		int capture = ssp_info->dais[0].direction[SNDRV_PCM_STREAM_CAPTURE];

		ret = asoc_sdw_init_simple_dai_link(dev, *dai_links, be_id, name,
						    playback, capture, cpu_dai_name,
						    platform_component->name,
						    ARRAY_SIZE(platform_component), codec_name,
						    ssp_info->dais[0].dai_name, 1, NULL,
						    ssp_info->ops);
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

	ret = asoc_sdw_init_simple_dai_link(dev, *dai_links, be_id, "dmic01",
					    0, 1, // DMIC only supports capture
					    "DMIC01 Pin", platform_component->name,
					    ARRAY_SIZE(platform_component),
					    "dmic-codec", "dmic-hifi", 1,
					    asoc_sdw_dmic_init, NULL);
	if (ret)
		return ret;

	(*dai_links)++;

	ret = asoc_sdw_init_simple_dai_link(dev, *dai_links, be_id, "dmic16k",
					    0, 1, // DMIC only supports capture
					    "DMIC16k Pin", platform_component->name,
					    ARRAY_SIZE(platform_component),
					    "dmic-codec", "dmic-hifi", 1,
					    /* don't call asoc_sdw_dmic_init() twice */
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
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct intel_mc_ctx *intel_ctx = (struct intel_mc_ctx *)ctx->private;
	int i, ret;

	for (i = 0; i < hdmi_num; i++) {
		char *name = devm_kasprintf(dev, GFP_KERNEL, "iDisp%d", i + 1);
		char *cpu_dai_name = devm_kasprintf(dev, GFP_KERNEL, "iDisp%d Pin", i + 1);
		if (!name || !cpu_dai_name)
			return -ENOMEM;

		char *codec_name, *codec_dai_name;

		if (intel_ctx->hdmi.idisp_codec) {
			codec_name = "ehdaudio0D2";
			codec_dai_name = devm_kasprintf(dev, GFP_KERNEL,
							"intel-hdmi-hifi%d", i + 1);
		} else {
			codec_name = "snd-soc-dummy";
			codec_dai_name = "snd-soc-dummy-dai";
		}

		if (!codec_dai_name)
			return -ENOMEM;

		ret = asoc_sdw_init_simple_dai_link(dev, *dai_links, be_id, name,
						    1, 0, // HDMI only supports playback
						    cpu_dai_name, platform_component->name,
						    ARRAY_SIZE(platform_component),
						    codec_name, codec_dai_name, 1,
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
	if (!name || !cpu_dai_name)
		return -ENOMEM;

	int ret;

	ret = asoc_sdw_init_simple_dai_link(dev, *dai_links, be_id, name,
					    1, 1, cpu_dai_name, platform_component->name,
					    ARRAY_SIZE(platform_component),
					    snd_soc_dummy_dlc.name, snd_soc_dummy_dlc.dai_name,
					    1, NULL, NULL);
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
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct intel_mc_ctx *intel_ctx = (struct intel_mc_ctx *)ctx->private;
	struct snd_soc_acpi_mach_params *mach_params = &mach->mach_params;
	struct snd_soc_codec_conf *codec_conf;
	struct asoc_sdw_codec_info *ssp_info;
	struct asoc_sdw_endpoint *sof_ends;
	struct asoc_sdw_dailink *sof_dais;
	int num_devs = 0;
	int num_ends = 0;
	struct snd_soc_dai_link *dai_links;
	int num_links;
	int be_id = 0;
	int hdmi_num;
	unsigned long ssp_mask;
	int ret;

	ret = asoc_sdw_count_sdw_endpoints(card, &num_devs, &num_ends);
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

	ret = asoc_sdw_parse_sdw_endpoints(card, sof_dais, sof_ends, &num_devs);
	if (ret < 0)
		goto err_end;

	sdw_be_num = ret;

	/*
	 * on generic tgl platform, I2S or sdw mode is supported
	 * based on board rework. A ACPI device is registered in
	 * system only when I2S mode is supported, not sdw mode.
	 * Here check ACPI ID to confirm I2S is supported.
	 */
	ssp_info = asoc_sdw_find_codec_info_acpi(mach->id);
	if (ssp_info) {
		ssp_mask = SOF_SSP_GET_PORT(sof_sdw_quirk);
		ssp_num = hweight_long(ssp_mask);
	}

	if (mach_params->codec_mask & IDISP_CODEC_MASK)
		intel_ctx->hdmi.idisp_codec = true;

	if (sof_sdw_quirk & SOF_SDW_TGL_HDMI)
		hdmi_num = SOF_TGL_HDMI_COUNT;
	else
		hdmi_num = SOF_PRE_TGL_HDMI_COUNT;

	/* enable dmic01 & dmic16k */
	if (sof_sdw_quirk & SOC_SDW_PCH_DMIC || mach_params->dmic_num) {
		if (ctx->ignore_internal_dmic)
			dev_warn(dev, "Ignoring PCH DMIC\n");
		else
			dmic_num = 2;
	}
	/*
	 * mach_params->dmic_num will be used to set the cfg-mics value of card->components
	 * string. Overwrite it to the actual number of PCH DMICs used in the device.
	 */
	mach_params->dmic_num = dmic_num;

	if (sof_sdw_quirk & SOF_SSP_BT_OFFLOAD_PRESENT)
		bt_num = 1;

	dev_dbg(dev, "sdw %d, ssp %d, dmic %d, hdmi %d, bt: %d\n",
		sdw_be_num, ssp_num, dmic_num,
		intel_ctx->hdmi.idisp_codec ? hdmi_num : 0, bt_num);

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
	if (dmic_num) {
		ret = create_dmic_dailinks(card, &dai_links, &be_id);
		if (ret)
			goto err_end;
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
	struct asoc_sdw_mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct intel_mc_ctx *intel_ctx = (struct intel_mc_ctx *)ctx->private;
	int ret = 0;

	ret = asoc_sdw_card_late_probe(card);
	if (ret < 0)
		return ret;

	if (intel_ctx->hdmi.idisp_codec)
		ret = sof_sdw_hdmi_card_late_probe(card);

	return ret;
}

static int mc_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = dev_get_platdata(&pdev->dev);
	struct snd_soc_card *card;
	struct asoc_sdw_mc_private *ctx;
	struct intel_mc_ctx *intel_ctx;
	int amp_num = 0, i;
	int ret;

	dev_dbg(&pdev->dev, "Entry\n");

	intel_ctx = devm_kzalloc(&pdev->dev, sizeof(*intel_ctx), GFP_KERNEL);
	if (!intel_ctx)
		return -ENOMEM;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->private = intel_ctx;
	ctx->codec_info_list_count = asoc_sdw_get_codec_info_list_count();
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

	ctx->mc_quirk = sof_sdw_quirk;
	/* reset amp_num to ensure amp_num++ starts from 0 in each probe */
	for (i = 0; i < ctx->codec_info_list_count; i++)
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
	for (i = 0; i < ctx->codec_info_list_count; i++)
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
		asoc_sdw_mc_dailink_exit_loop(card);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static void mc_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	asoc_sdw_mc_dailink_exit_loop(card);
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
	.remove = mc_remove,
	.id_table = mc_id_table,
};

module_platform_driver(sof_sdw_driver);

MODULE_DESCRIPTION("ASoC SoundWire Generic Machine driver");
MODULE_AUTHOR("Bard Liao <yung-chuan.liao@linux.intel.com>");
MODULE_AUTHOR("Rander Wang <rander.wang@linux.intel.com>");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SND_SOC_INTEL_HDA_DSP_COMMON);
MODULE_IMPORT_NS(SND_SOC_SDW_UTILS);
