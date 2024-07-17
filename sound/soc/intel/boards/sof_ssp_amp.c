// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation

/*
 * sof_ssp_amp.c - ASoc Machine driver for Intel platforms
 * with RT1308/CS35L41 codec.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sof.h>
#include "sof_board_helpers.h"
#include "sof_realtek_common.h"
#include "sof_cirrus_common.h"

/* Driver-specific board quirks: from bit 0 to 7 */
#define SOF_HDMI_PLAYBACK_PRESENT		BIT(0)

/* Default: SSP2  */
static unsigned long sof_ssp_amp_quirk = SOF_SSP_PORT_AMP(2);

static const struct dmi_system_id chromebook_platforms[] = {
	{
		.ident = "Google Chromebooks",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
		}
	},
	{},
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	return sof_intel_board_card_late_probe(card);
}

static struct snd_soc_card sof_ssp_amp_card = {
	.name         = "ssp_amp",
	.owner        = THIS_MODULE,
	.fully_routed = true,
	.late_probe = sof_card_late_probe,
};

/* BE ID defined in sof-tgl-rt1308-hdmi-ssp.m4 */
#define HDMI_IN_BE_ID		0
#define SPK_BE_ID		2
#define DMIC01_BE_ID		3
#define INTEL_HDMI_BE_ID	5
/* extra BE links to support no-hdmi-in boards */
#define DMIC16K_BE_ID		4
#define BT_OFFLOAD_BE_ID	8

#define SSP_AMP_LINK_ORDER	SOF_LINK_ORDER(SOF_LINK_HDMI_IN, \
					SOF_LINK_AMP,            \
					SOF_LINK_DMIC01,         \
					SOF_LINK_DMIC16K,        \
					SOF_LINK_IDISP_HDMI,     \
					SOF_LINK_BT_OFFLOAD,     \
					SOF_LINK_NONE)

#define SSP_AMP_LINK_IDS	SOF_LINK_ORDER(HDMI_IN_BE_ID, \
					SPK_BE_ID,            \
					DMIC01_BE_ID,         \
					DMIC16K_BE_ID,        \
					INTEL_HDMI_BE_ID,     \
					BT_OFFLOAD_BE_ID,     \
					0)

static int
sof_card_dai_links_create(struct device *dev, struct snd_soc_card *card,
			  struct sof_card_private *ctx)
{
	int ret;

	ret = sof_intel_board_set_dai_link(dev, card, ctx);
	if (ret)
		return ret;

	if (ctx->amp_type == CODEC_NONE)
		return 0;

	if (!ctx->amp_link) {
		dev_err(dev, "amp link not available");
		return -EINVAL;
	}

	/* codec-specific fields for speaker amplifier */
	switch (ctx->amp_type) {
	case CODEC_CS35L41:
		cs35l41_set_dai_link(ctx->amp_link);
		break;
	case CODEC_RT1308:
		sof_rt1308_dai_link(ctx->amp_link);
		break;
	default:
		dev_err(dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

	return 0;
}

static int sof_ssp_amp_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach = pdev->dev.platform_data;
	struct sof_card_private *ctx;
	int ret;

	if (pdev->id_entry && pdev->id_entry->driver_data)
		sof_ssp_amp_quirk = (unsigned long)pdev->id_entry->driver_data;

	dev_dbg(&pdev->dev, "sof_ssp_amp_quirk = %lx\n", sof_ssp_amp_quirk);

	/* initialize ctx with board quirk */
	ctx = sof_intel_board_get_ctx(&pdev->dev, sof_ssp_amp_quirk);
	if (!ctx)
		return -ENOMEM;

	if (!dmi_check_system(chromebook_platforms) &&
	    (mach->mach_params.dmic_num == 0))
		ctx->dmic_be_num = 0;

	if (sof_ssp_amp_quirk & SOF_HDMI_PLAYBACK_PRESENT) {
		if (mach->mach_params.codec_mask & IDISP_CODEC_MASK)
			ctx->hdmi.idisp_codec = true;
	} else {
		ctx->hdmi_num = 0;
	}

	ctx->link_order_overwrite = SSP_AMP_LINK_ORDER;

	if (ctx->ssp_mask_hdmi_in) {
		/* the topology supports HDMI-IN uses fixed BE ID for DAI links */
		ctx->link_id_overwrite = SSP_AMP_LINK_IDS;
	}

	/* update dai_link */
	ret = sof_card_dai_links_create(&pdev->dev, &sof_ssp_amp_card, ctx);
	if (ret)
		return ret;

	/* update codec_conf */
	switch (ctx->amp_type) {
	case CODEC_CS35L41:
		cs35l41_set_codec_conf(&sof_ssp_amp_card);
		break;
	case CODEC_RT1308:
	case CODEC_NONE:
		/* no codec conf required */
		break;
	default:
		dev_err(&pdev->dev, "invalid amp type %d\n", ctx->amp_type);
		return -EINVAL;
	}

	sof_ssp_amp_card.dev = &pdev->dev;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&sof_ssp_amp_card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	snd_soc_card_set_drvdata(&sof_ssp_amp_card, ctx);

	return devm_snd_soc_register_card(&pdev->dev, &sof_ssp_amp_card);
}

static const struct platform_device_id board_ids[] = {
	{
		.name = "sof_ssp_amp",
	},
	{
		.name = "tgl_rt1308_hdmi_ssp",
		.driver_data = (kernel_ulong_t)(SOF_SSP_PORT_AMP(2) |
					SOF_SSP_MASK_HDMI_CAPTURE(0x22)),
					/* SSP 1 and SSP 5 are used for HDMI IN */
	},
	{
		.name = "adl_cs35l41",
		.driver_data = (kernel_ulong_t)(SOF_SSP_PORT_AMP(1) |
					SOF_NUM_IDISP_HDMI(4) |
					SOF_HDMI_PLAYBACK_PRESENT |
					SOF_SSP_PORT_BT_OFFLOAD(2) |
					SOF_BT_OFFLOAD_PRESENT),
	},
	{
		.name = "adl_lt6911_hdmi_ssp",
		.driver_data = (kernel_ulong_t)(SOF_SSP_MASK_HDMI_CAPTURE(0x5) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_HDMI_PLAYBACK_PRESENT),
	},
	{
		.name = "rpl_lt6911_hdmi_ssp",
		.driver_data = (kernel_ulong_t)(SOF_SSP_MASK_HDMI_CAPTURE(0x5) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_HDMI_PLAYBACK_PRESENT),
	},
	{
		.name = "mtl_lt6911_hdmi_ssp",
		.driver_data = (kernel_ulong_t)(SOF_SSP_MASK_HDMI_CAPTURE(0x5) |
					/* SSP 0 and SSP 2 are used for HDMI IN */
					SOF_HDMI_PLAYBACK_PRESENT),
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, board_ids);

static struct platform_driver sof_ssp_amp_driver = {
	.probe          = sof_ssp_amp_probe,
	.driver = {
		.name   = "sof_ssp_amp",
		.pm = &snd_soc_pm_ops,
	},
	.id_table = board_ids,
};
module_platform_driver(sof_ssp_amp_driver);

MODULE_DESCRIPTION("ASoC Intel(R) SOF Amplifier Machine driver");
MODULE_AUTHOR("Balamurugan C <balamurugan.c@intel.com>");
MODULE_AUTHOR("Brent Lu <brent.lu@intel.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_BOARD_HELPERS);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_REALTEK_COMMON);
MODULE_IMPORT_NS(SND_SOC_INTEL_SOF_CIRRUS_COMMON);
