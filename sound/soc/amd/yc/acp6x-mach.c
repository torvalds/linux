// SPDX-License-Identifier: GPL-2.0+
/*
 * Machine driver for AMD Yellow Carp platform using DMIC
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
 */

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/io.h>
#include <linux/dmi.h>

#include "acp6x.h"

#define DRV_NAME "acp_yc_mach"

SND_SOC_DAILINK_DEF(acp6x_pdm,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp_yc_pdm_dma.0")));

SND_SOC_DAILINK_DEF(dmic_codec,
		    DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec.0",
						  "dmic-hifi")));

SND_SOC_DAILINK_DEF(pdm_platform,
		    DAILINK_COMP_ARRAY(COMP_PLATFORM("acp_yc_pdm_dma.0")));

static struct snd_soc_dai_link acp6x_dai_pdm[] = {
	{
		.name = "acp6x-dmic-capture",
		.stream_name = "DMIC capture",
		.capture_only = 1,
		SND_SOC_DAILINK_REG(acp6x_pdm, dmic_codec, pdm_platform),
	},
};

static struct snd_soc_card acp6x_card = {
	.name = "acp6x",
	.owner = THIS_MODULE,
	.dai_link = acp6x_dai_pdm,
	.num_links = 1,
};

static const struct dmi_system_id yc_acp_quirk_table[] = {
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D2"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D3"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D4"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D5"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CF"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CG"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CQ"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CR"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21AW"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21AX"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21BN"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21BQ"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CH"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CJ"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CK"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CL"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D8"),
		}
	},
	{
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D9"),
		}
	},
	{}
};

static int acp6x_probe(struct platform_device *pdev)
{
	struct acp6x_pdm *machine = NULL;
	struct snd_soc_card *card;
	int ret;
	const struct dmi_system_id *dmi_id;

	dmi_id = dmi_first_match(yc_acp_quirk_table);
	if (!dmi_id)
		return -ENODEV;
	card = &acp6x_card;
	acp6x_card.dev = &pdev->dev;

	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				"snd_soc_register_card(%s) failed\n",
				card->name);
	}
	return 0;
}

static struct platform_driver acp6x_mach_driver = {
	.driver = {
		.name = "acp_yc_mach",
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp6x_probe,
};

module_platform_driver(acp6x_mach_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
