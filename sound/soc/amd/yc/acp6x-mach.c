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
#include <linux/acpi.h>

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
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D2"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D3"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D4"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21D5"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CF"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CG"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CQ"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CR"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CM"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CN"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CH"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CJ"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CK"),
		}
	},
	{
		.driver_data = &acp6x_card,
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "21CL"),
		}
	},
	{}
};

static int acp6x_probe(struct platform_device *pdev)
{
	const struct dmi_system_id *dmi_id;
	struct acp6x_pdm *machine = NULL;
	struct snd_soc_card *card;
	struct acpi_device *adev;
	int ret;

	/* check the parent device's firmware node has _DSD or not */
	adev = ACPI_COMPANION(pdev->dev.parent);
	if (adev) {
		const union acpi_object *obj;

		if (!acpi_dev_get_property(adev, "AcpDmicConnected", ACPI_TYPE_INTEGER, &obj) &&
		    obj->integer.value == 1)
			platform_set_drvdata(pdev, &acp6x_card);
	}

	/* check for any DMI overrides */
	dmi_id = dmi_first_match(yc_acp_quirk_table);
	if (dmi_id)
		platform_set_drvdata(pdev, dmi_id->driver_data);

	card = platform_get_drvdata(pdev);
	if (!card)
		return -ENODEV;
	dev_info(&pdev->dev, "Enabling ACP DMIC support via %s", dmi_id ? "DMI" : "ACPI");
	acp6x_card.dev = &pdev->dev;

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
